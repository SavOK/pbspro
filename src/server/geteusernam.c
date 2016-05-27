/*
 * Copyright (C) 1994-2016 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */
/**
 * @brief
 * geteusernam.c	- Functions relating to effective user name
 *
 *	Included public functions are:
 *
 *	determine_euser
 *	determine_egroup
 *	set_objexid
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#ifndef WIN32
#include <sys/param.h>
#include <grp.h>
#include <pwd.h>
#endif
#include "pbs_ifl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include "win.h"
#include "user.h"
#endif

#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "reservation.h"
#include "server.h"
#include "pbs_error.h"
#include "pbs_nodes.h"
#include "svrfunc.h"

/* External Data */

extern char server_host[];

/**
 * @brief	
 *  	determine_euser - determine the effective user name
 *
 *  	Determine with which "user name" this object is to be associated
 *  	 1. from user_list use name with host name that matches this host
 *  	 2. from user_list use name with no host specification
 *  	 3. user owner name
 *
 * @see
 *		set_objexid
 *
 * @param[in]	objtype - object type
 * @param[in]	pobj -  ptr to object to link in
 * @param[in]	pattr - pointer to User_List attribute
 * @param[in,out]	isowner - Return: 1  if object's owner chosen as euser
 *
 * @return pointer to the user name
 * 
 */

static char *
determine_euser(void *pobj, int objtype, attribute *pattr, int *isowner)
{
	char	*hit = 0;
	int	 i;
	struct array_strings *parst;
	char	*pn;
	char	*ptr;
	int	idx_owner;
	attribute *objattrs;
	static char username[PBS_MAXUSER+1];
#ifdef WIN32
	extern int read_cred(job *pjob, char **cred, size_t *len);
	extern int decrypt_pwd(char *crypted, size_t len, char **passwd);
#endif
	memset(username,'\0', sizeof(username));

	/* set index and pointers based on object type */
	if (objtype == JOB_OBJECT) {
		idx_owner = (int)JOB_ATR_job_owner;
		objattrs = &((job *)pobj)->ji_wattr[0];
	} else {
		idx_owner = (int)RESV_ATR_resv_owner;
		objattrs = &((resc_resv *)pobj)->ri_wattr[0];
	}

	/* search the User_List attribute */

	if ((pattr->at_flags & ATR_VFLAG_SET) &&
		(parst = pattr->at_val.at_arst)  ) {
		*isowner = 0;
		for (i=0; i<parst->as_usedptr; i++) {
			pn = parst->as_string[i];
			ptr = strchr(pn, '@');
			if (ptr) {	/* if has host specification, check for the complete host name, if host name is incorrect, hit is not set */
				if (!strcasecmp(server_host, ptr+1)) {
					hit = pn;	/* option 1. */
					break;
				}
			} else {	/* wildcard host (null) */
				hit = pn;	/* option 2. */
			}
		}
	}
	if (!(pattr->at_flags & ATR_VFLAG_SET)) {	/* if no user is specified, default to the object owner ( 3.) */
		hit = objattrs[idx_owner].at_val.at_str;
		*isowner = 1;
	}

	/* copy user name into return buffer and strip off host name only when hit is set 
	 * i.e. when either no user is specified(in this case, default the job to the object owner)
	 * or a user is provided with the correct host name.
	 * If not set, job can't be run as no user to run the job */
	if (hit) {
		cvrt_fqn_to_name(hit, username);
	}

	

	return (username);
}

/**
 * @brief
 * 		determine_egroup - determine the effective group name
 *
 * 		Determine (for this host) with which "group name" this object is to be
 * 		associated
 *
 * @par	Functionality:
 *  	1. from group_list use name with host name that matches this host
 *  	2. from group_list use name with no host specification
 *  	3. NULL, not specified
 *
 *	@see
 *		set_objexid
 *
 * @param[in]	objtype - object type
 * @param[in]	pobj -  ptr to object to link in
 * @param[in]	pattr - pointer to group_list attribute
 *
 * Returns pointer to the group name or a NULL pointer
 */

static char *
determine_egroup(void *pobj, int objtype, attribute *pattr)
{
	char	*hit = 0;
	int	 i;
	struct array_strings *parst;
	char	*pn;
	char	*ptr;
	static char groupname[PBS_MAXUSER+1];

	/* search the group-list attribute */

	if ((pattr->at_flags & ATR_VFLAG_SET) &&
		(parst = pattr->at_val.at_arst)) {
		for (i=0; i<parst->as_usedptr; i++) {
			pn = parst->as_string[i];
			ptr = strchr(pn, '@');
			if (ptr) {	/* has host specification */
				if (!strncasecmp(server_host, ptr+1, strlen(ptr+1))) {
					hit = pn;	/* option 1. */
					break;
				}
			} else {	/* wildcard host (null) */
				hit = pn;	/* option 2. */
			}
		}
	}
	if (!hit) 	/* nothing sepecified, return null */
		return ((char *)0);

	/* copy group name into return buffer, strip host name */
	cvrt_fqn_to_name(hit, groupname);
	return (groupname);
}

/**
 * @brief
 * 		set_objexid - validate and set the object's effective/execution username
 *		and its effective/execution group name attributes.  For jobs, these
 *		are the attributes at positions JOB_ATR_euser and JOB_ATR_egroup in
 *		the attribute array ji_wattr[] of the job structure.
 *		For reservations, they	are the attributes at positions
 *		RESV_ATR_euser and RESV_ATR_egroup in array ri_wattr of the
 *		resc_resv structure.
 *
 * @par	Functionality:
 *		1.  Determine the effective/execution user_name.
 *		1a. Get the password entry for that username.
 *		1b. Uid of 0 (superuser) is not allowed, might cause root-rot
 *		1c. Determine if the object's owner name is permitted to map
 *	    to the effective/execution username
 *		1d. Set the object's effective/execution user_name to the name
 *	    that got determined in the above steps
 *		2.  Determine the effective/execution group name.
 *		2a. Determine if the effective/execution user_name belongs to
 *	    this effective/execution group
 *		2b. Set JOB_ATR_egroup to the execution group name.
 *		2b. Set the object's effective/execution group_name to the name
 *	    that got determined in the above step
 *
 * @see
 *		modify_job_attr, req_resvSub and svr_chkque
 *
 * @param[in]	objtype - object type
 * @param[in]	pobj -  ptr to object to link in
 * @param[in]	pattr - pointer to attribute structure
 * 						which contains group_List attributes and User_List attributes
 *
 * @returns	 int
 * @retval	 0	- if everything got determined and set appropriately else,
 * @retval	non-zero	- error number if something went awry
 */

int
set_objexid(void *pobj, int objtype, attribute *attrry)
{
	int		 addflags = 0;
	int		 isowner;
	attribute	*pattr;
	char		*puser;
	char		*pgrpn;
	char		*owner;
	int		idx_ul,	idx_gl;
	int		idx_owner, idx_euser, idx_egroup;
	int		idx_acct;
	int		bad_euser, bad_egrp;
	attribute	*objattrs;
	attribute_def	*obj_attr_def;
	attribute	*paclRoot;	/*future: aclRoot resv != aclRoot job*/
#ifdef WIN32
	char		user_s[PBS_MAXHOSTNAME+ MAXNAMLEN+3];
	char		*p = NULL;
	char		*p0 = NULL;
	int		ch = '\\';
	SID     *sid;
	char            *defgrp = NULL;
#else
	char	       **pmem;
	struct group	*gpent;
	struct passwd	*pwent;
	char		 gname[PBS_MAXGRPN+1];
#endif

	/* determine index values and pointers based on object type */
	if (objtype == JOB_OBJECT) {
		idx_ul = (int)JOB_ATR_userlst;
		idx_gl = (int)JOB_ATR_grouplst;
		idx_owner = (int)JOB_ATR_job_owner;
		idx_euser = (int)JOB_ATR_euser;
		idx_egroup = (int)JOB_ATR_egroup;
		idx_acct = (int)JOB_ATR_account;
		obj_attr_def = job_attr_def;
		objattrs = ((job *)pobj)->ji_wattr;
		owner = ((job *)pobj)->ji_wattr[idx_owner].at_val.at_str;
		paclRoot = &server.sv_attr[(int)SRV_ATR_AclRoot];
		bad_euser = PBSE_BADUSER;
		bad_egrp = PBSE_BADGRP;
	} else {
		idx_ul = (int)RESV_ATR_userlst;
		idx_gl = (int)RESV_ATR_grouplst;
		idx_owner = (int)RESV_ATR_resv_owner;
		idx_euser = (int)RESV_ATR_euser;
		idx_egroup = (int)RESV_ATR_egroup;
		idx_acct = (int)RESV_ATR_account;
		obj_attr_def = resv_attr_def;
		objattrs = ((resc_resv *)pobj)->ri_wattr;
		owner = ((resc_resv *)pobj)->ri_wattr[idx_owner].at_val.at_str;
		paclRoot = &server.sv_attr[(int)SRV_ATR_AclRoot];
		bad_euser = PBSE_R_UID;
		bad_egrp = PBSE_R_GID;
	}

	/* if passed in "User_List" attribute is set use it - this may
	 * be a newly modified one.
	 * if not set, fall back to the object's User_List, which may
	 * actually be the same as what is passed into this function
	 */

	if ((attrry + idx_ul)->at_flags & ATR_VFLAG_SET)
		pattr = attrry + idx_ul;
	else
		pattr = &objattrs[idx_ul];

	if ((puser = determine_euser(pobj, objtype, pattr, &isowner)) == (char *)0)
		return (bad_euser);


#ifdef WIN32
	if (isAdminPrivilege(puser)) { /* equivalent of root */
		if ((paclRoot->at_flags & ATR_VFLAG_SET) == 0)
			return (bad_euser); /* root not allowed */
		if (acl_check(paclRoot, owner, ACL_User) == 0)
			return (bad_euser); /* root not allowed */
	}
#else
	pwent = getpwnam(puser);
	if (pwent == (struct passwd *)0) {
		if (!server.sv_attr[(int)SRV_ATR_FlatUID].at_val.at_long)
			return (bad_euser);
	} else if (pwent->pw_uid == 0) {
		if ((paclRoot->at_flags & ATR_VFLAG_SET) == 0)
			return (bad_euser); /* root not allowed */
		if (acl_check(paclRoot, owner, ACL_User) == 0)
			return (bad_euser); /* root not allowed */
	}
#endif

	if (!isowner || !server.sv_attr[(int)SRV_ATR_FlatUID].at_val.at_long) {
#ifdef WIN32
		if ( (server.sv_attr[SRV_ATR_ssignon_enable].at_flags &      \
                                                          ATR_VFLAG_SET) && \
                    (server.sv_attr[SRV_ATR_ssignon_enable].at_val.at_long  \
                                                                     == 1) ) {
			/* read/cache user password */
			cache_usertoken_and_homedir(puser, NULL, 0,
				user_read_password, (char *)puser, pbs_decrypt_pwd, 0);
		} else {
			/* read/cache job password */
			cache_usertoken_and_homedir(puser, NULL, 0,
				read_cred, (job *)pobj, pbs_decrypt_pwd, 0);
		}
#endif
		if (site_check_user_map(pobj, objtype, puser) == -1)
			return (bad_euser);
	}

	pattr = &objattrs[idx_euser];
	obj_attr_def[idx_euser].at_free(pattr);
	obj_attr_def[idx_euser].at_decode(pattr, (char *)0, (char *)0, puser);

#ifndef WIN32
	if (pwent != NULL) {
#endif

		/* if account (qsub -A) is not specified, set to empty string */

		pattr = &objattrs[idx_acct];
		if ((pattr->at_flags & ATR_VFLAG_SET) == 0) {
			(void)obj_attr_def[idx_acct].at_decode(pattr,
				(char *)0, (char *)0, "\0");
		}

		/*
		 * now figure out (for this host) the effective/execute "group name"
		 * for the object.
		 * PBS requires that each group have an entry in the group file,
		 * see the admin guide for the reason why...
		 *
		 * use the passed group_list if set, may be a newly modified one.
		 * if it isn't set, use the object's group_list, which may in fact
		 * be same as what was passed
		 */

		if ((attrry + idx_gl)->at_flags & ATR_VFLAG_SET)
			pattr = attrry + idx_gl;
		else
			pattr = &objattrs[idx_gl];
		if ((pgrpn = determine_egroup(pobj, objtype, pattr)) != NULL) {

			/* user specified a group, group must exists and either	   */
			/* must be user's primary group	 or the user must be in it */


#ifdef WIN32
			if ((sid=getgrpsid(pgrpn)) == NULL)
				return (bad_egrp);               /* no such group */
			(void)LocalFree(sid);
#else
			gpent = getgrnam(pgrpn);
			if (gpent == (struct group *)0) {
				if (pwent != NULL)	/* no such group is allowed */
					return (bad_egrp);	/* only when no user (flatuid)*/

			} else if (gpent->gr_gid != pwent->pw_gid) {  /* not primary */
				pmem = gpent->gr_mem;
				while (*pmem) {
					if (!strcmp(puser, *pmem))
						break;
					++pmem;
				}
				if (*pmem == 0)
					return (bad_egrp);	/* user not in group */
			}
#endif
			addflags = ATR_VFLAG_SET;

		} else {

			/* Use user login group */

#ifdef WIN32
			if ((defgrp=getdefgrpname(puser)) == NULL)
				return (bad_egrp);      /* set to a group that ALL users belong to as default */
			pgrpn = defgrp;
#else

			gpent = getgrgid(pwent->pw_gid);
			if (gpent != (struct group *)0) {
				pgrpn = gpent->gr_name;		/* use group name */
			} else {
				(void)sprintf(gname, "%d", pwent->pw_gid);
				pgrpn = gname;		/* turn gid into string */
			}
#endif

			/*
			 * setting the DEFAULT flag is a "kludy" way to keep MOM from
			 * having to do an unneeded look up of the group file.
			 * We needed to have JOB_ATR_egroup set for the server but
			 * MOM only wants it if it is not the login group, so there!
			 */
			addflags = ATR_VFLAG_SET | ATR_VFLAG_DEFLT;
		}

#ifndef WIN32
	} else {

		/*
		 * null password entry,
		 * set group to "default" and set default for Mom to use login group
		 */

		pgrpn = "-default-";
		addflags = ATR_VFLAG_SET | ATR_VFLAG_DEFLT;

	}
#endif


	pattr = attrry + idx_egroup;
	obj_attr_def[idx_egroup].at_free(pattr);

	if (addflags != 0) {
		obj_attr_def[idx_egroup].at_decode(pattr, (char *)0, (char *)0, pgrpn);
		pattr->at_flags |= addflags;
	}

#ifdef WIN32
	if (defgrp)
		(void)free(defgrp);
#endif
	return (0);
}