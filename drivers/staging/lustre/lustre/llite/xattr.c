// SPDX-License-Identifier: GPL-2.0
/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/xattr.h>
#include <linux/selinux.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include <obd_support.h>
#include <lustre_dlm.h>

#include "llite_internal.h"

const struct xattr_handler *get_xattr_type(const char *name)
{
	int i = 0;

	while (ll_xattr_handlers[i]) {
		size_t len = strlen(ll_xattr_handlers[i]->prefix);

		if (!strncmp(ll_xattr_handlers[i]->prefix, name, len))
			return ll_xattr_handlers[i];
		i++;
	}
	return NULL;
}

static int xattr_type_filter(struct ll_sb_info *sbi,
			     const struct xattr_handler *handler)
{
	/* No handler means XATTR_OTHER_T */
	if (!handler)
		return -EOPNOTSUPP;

	if ((handler->flags == XATTR_ACL_ACCESS_T ||
	     handler->flags == XATTR_ACL_DEFAULT_T) &&
	   !(sbi->ll_flags & LL_SBI_ACL))
		return -EOPNOTSUPP;

	if (handler->flags == XATTR_USER_T &&
	    !(sbi->ll_flags & LL_SBI_USER_XATTR))
		return -EOPNOTSUPP;

	if (handler->flags == XATTR_TRUSTED_T &&
	    !capable(CFS_CAP_SYS_ADMIN))
		return -EPERM;

	return 0;
}

static int
ll_xattr_set_common(const struct xattr_handler *handler,
		    struct dentry *dentry, struct inode *inode,
		    const char *name, const void *value, size_t size,
		    int flags)
{
	char fullname[strlen(handler->prefix) + strlen(name) + 1];
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *req = NULL;
	const char *pv = value;
	__u64 valid;
	int rc;

	if (flags == XATTR_REPLACE) {
		ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_REMOVEXATTR, 1);
		valid = OBD_MD_FLXATTRRM;
	} else {
		ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_SETXATTR, 1);
		valid = OBD_MD_FLXATTR;
	}

	rc = xattr_type_filter(sbi, handler);
	if (rc)
		return rc;

	if ((handler->flags == XATTR_ACL_ACCESS_T ||
	     handler->flags == XATTR_ACL_DEFAULT_T) &&
	    !inode_owner_or_capable(inode))
		return -EPERM;

	/* b10667: ignore lustre special xattr for now */
	if (!strcmp(name, "hsm") ||
	    ((handler->flags == XATTR_TRUSTED_T && !strcmp(name, "lov")) ||
	     (handler->flags == XATTR_LUSTRE_T && !strcmp(name, "lov"))))
		return 0;

	/* b15587: ignore security.capability xattr for now */
	if ((handler->flags == XATTR_SECURITY_T &&
	     !strcmp(name, "capability")))
		return 0;

	/* LU-549:  Disable security.selinux when selinux is disabled */
	if (handler->flags == XATTR_SECURITY_T && !selinux_is_enabled() &&
	    strcmp(name, "selinux") == 0)
		return -EOPNOTSUPP;

	/*FIXME: enable IMA when the conditions are ready */
	if (handler->flags == XATTR_SECURITY_T &&
	    (!strcmp(name, "ima") || !strcmp(name, "evm")))
		return -EOPNOTSUPP;

	/*
	 * In user.* namespace, only regular files and directories can have
	 * extended attributes.
	 */
	if (handler->flags == XATTR_USER_T) {
		if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
			return -EPERM;
	}

	sprintf(fullname, "%s%s\n", handler->prefix, name);
	rc = md_setxattr(sbi->ll_md_exp, ll_inode2fid(inode),
			 valid, fullname, pv, size, 0, flags,
			 ll_i2suppgid(inode), &req);
	if (rc) {
		if (rc == -EOPNOTSUPP && handler->flags == XATTR_USER_T) {
			LCONSOLE_INFO("Disabling user_xattr feature because it is not supported on the server\n");
			sbi->ll_flags &= ~LL_SBI_USER_XATTR;
		}
		return rc;
	}

	ptlrpc_req_finished(req);
	return 0;
}

static int get_hsm_state(struct inode *inode, u32 *hus_states)
{
	struct md_op_data *op_data;
	struct hsm_user_state *hus;
	int rc;

	hus = kzalloc(sizeof(*hus), GFP_NOFS);
	if (!hus)
		return -ENOMEM;

	op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, hus);
	if (!IS_ERR(op_data)) {
		rc = obd_iocontrol(LL_IOC_HSM_STATE_GET, ll_i2mdexp(inode),
				   sizeof(*op_data), op_data, NULL);
		if (!rc)
			*hus_states = hus->hus_states;
		else
			CDEBUG(D_VFSTRACE, "obd_iocontrol failed. rc = %d\n",
			       rc);

		ll_finish_md_op_data(op_data);
	} else {
		rc = PTR_ERR(op_data);
		CDEBUG(D_VFSTRACE, "Could not prepare the opdata. rc = %d\n",
		       rc);
	}
	kfree(hus);
	return rc;
}

static int ll_xattr_set(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *value, size_t size,
			int flags)
{
	LASSERT(inode);
	LASSERT(name);

	CDEBUG(D_VFSTRACE, "VFS Op:inode=" DFID "(%p), xattr %s\n",
	       PFID(ll_inode2fid(inode)), inode, name);

	if (!strcmp(name, "lov")) {
		struct lov_user_md *lump = (struct lov_user_md *)value;
		int op_type = flags == XATTR_REPLACE ? LPROC_LL_REMOVEXATTR :
						       LPROC_LL_SETXATTR;
		int rc = 0;

		ll_stats_ops_tally(ll_i2sbi(inode), op_type, 1);

		if (size != 0 && size < sizeof(struct lov_user_md))
			return -EINVAL;

		/*
		 * It is possible to set an xattr to a "" value of zero size.
		 * For this case we are going to treat it as a removal.
		 */
		if (!size && lump)
			lump = NULL;

		/* Attributes that are saved via getxattr will always have
		 * the stripe_offset as 0.  Instead, the MDS should be
		 * allowed to pick the starting OST index.   b=17846
		 */
		if (lump && lump->lmm_stripe_offset == 0)
			lump->lmm_stripe_offset = -1;

		/* Avoid anyone directly setting the RELEASED flag. */
		if (lump && (lump->lmm_pattern & LOV_PATTERN_F_RELEASED)) {
			/* Only if we have a released flag check if the file
			 * was indeed archived.
			 */
			u32 state = HS_NONE;

			rc = get_hsm_state(inode, &state);
			if (rc)
				return rc;

			if (!(state & HS_ARCHIVED)) {
				CDEBUG(D_VFSTRACE,
				       "hus_states state = %x, pattern = %x\n",
				state, lump->lmm_pattern);
				/*
				 * Here the state is: real file is not
				 * archived but user is requesting to set
				 * the RELEASED flag so we mask off the
				 * released flag from the request
				 */
				lump->lmm_pattern ^= LOV_PATTERN_F_RELEASED;
			}
		}

		if (lump && S_ISREG(inode->i_mode)) {
			__u64 it_flags = FMODE_WRITE;
			int lum_size;

			lum_size = ll_lov_user_md_size(lump);
			if (lum_size < 0 || size < lum_size)
				return 0; /* b=10667: ignore error */

			rc = ll_lov_setstripe_ea_info(inode, dentry, it_flags,
						      lump, lum_size);
			/* b=10667: rc always be 0 here for now */
			rc = 0;
		} else if (S_ISDIR(inode->i_mode)) {
			rc = ll_dir_setstripe(inode, lump, 0);
		}

		return rc;

	} else if (!strcmp(name, "lma") || !strcmp(name, "link")) {
		ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_SETXATTR, 1);
		return 0;
	}

	return ll_xattr_set_common(handler, dentry, inode, name, value, size,
				   flags);
}

int
ll_xattr_list(struct inode *inode, const char *name, int type, void *buffer,
	      size_t size, __u64 valid)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *req = NULL;
	struct mdt_body *body;
	void *xdata;
	int rc;

	if (sbi->ll_xattr_cache_enabled && type != XATTR_ACL_ACCESS_T &&
	    (type != XATTR_SECURITY_T || strcmp(name, "security.selinux"))) {
		rc = ll_xattr_cache_get(inode, name, buffer, size, valid);
		if (rc == -EAGAIN)
			goto getxattr_nocache;
		if (rc < 0)
			goto out_xattr;

		/* Add "system.posix_acl_access" to the list */
		if (lli->lli_posix_acl && valid & OBD_MD_FLXATTRLS) {
			if (size == 0) {
				rc += sizeof(XATTR_NAME_ACL_ACCESS);
			} else if (size - rc >= sizeof(XATTR_NAME_ACL_ACCESS)) {
				memcpy(buffer + rc, XATTR_NAME_ACL_ACCESS,
				       sizeof(XATTR_NAME_ACL_ACCESS));
				rc += sizeof(XATTR_NAME_ACL_ACCESS);
			} else {
				rc = -ERANGE;
				goto out_xattr;
			}
		}
	} else {
getxattr_nocache:
		rc = md_getxattr(sbi->ll_md_exp, ll_inode2fid(inode),
				 valid, name, NULL, 0, size, 0, &req);
		if (rc < 0)
			goto out_xattr;

		body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
		LASSERT(body);

		/* only detect the xattr size */
		if (size == 0) {
			rc = body->mbo_eadatasize;
			goto out;
		}

		if (size < body->mbo_eadatasize) {
			CERROR("server bug: replied size %u > %u\n",
			       body->mbo_eadatasize, (int)size);
			rc = -ERANGE;
			goto out;
		}

		if (body->mbo_eadatasize == 0) {
			rc = -ENODATA;
			goto out;
		}

		/* do not need swab xattr data */
		xdata = req_capsule_server_sized_get(&req->rq_pill, &RMF_EADATA,
						     body->mbo_eadatasize);
		if (!xdata) {
			rc = -EFAULT;
			goto out;
		}

		memcpy(buffer, xdata, body->mbo_eadatasize);
		rc = body->mbo_eadatasize;
	}

out_xattr:
	if (rc == -EOPNOTSUPP && type == XATTR_USER_T) {
		LCONSOLE_INFO(
			"%s: disabling user_xattr feature because it is not supported on the server: rc = %d\n",
			ll_get_fsname(inode->i_sb, NULL, 0), rc);
		sbi->ll_flags &= ~LL_SBI_USER_XATTR;
	}
out:
	ptlrpc_req_finished(req);
	return rc;
}

static int ll_xattr_get_common(const struct xattr_handler *handler,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, void *buffer, size_t size)
{
	char fullname[strlen(handler->prefix) + strlen(name) + 1];
	struct ll_sb_info *sbi = ll_i2sbi(inode);
#ifdef CONFIG_FS_POSIX_ACL
	struct ll_inode_info *lli = ll_i2info(inode);
#endif
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode=" DFID "(%p)\n",
	       PFID(ll_inode2fid(inode)), inode);

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_GETXATTR, 1);

	rc = xattr_type_filter(sbi, handler);
	if (rc)
		return rc;

	/* b15587: ignore security.capability xattr for now */
	if ((handler->flags == XATTR_SECURITY_T && !strcmp(name, "capability")))
		return -ENODATA;

	/* LU-549:  Disable security.selinux when selinux is disabled */
	if (handler->flags == XATTR_SECURITY_T && !selinux_is_enabled() &&
	    !strcmp(name, "selinux"))
		return -EOPNOTSUPP;

#ifdef CONFIG_FS_POSIX_ACL
	/* posix acl is under protection of LOOKUP lock. when calling to this,
	 * we just have path resolution to the target inode, so we have great
	 * chance that cached ACL is uptodate.
	 */
	if (handler->flags == XATTR_ACL_ACCESS_T) {
		struct posix_acl *acl;

		spin_lock(&lli->lli_lock);
		acl = posix_acl_dup(lli->lli_posix_acl);
		spin_unlock(&lli->lli_lock);

		if (!acl)
			return -ENODATA;

		rc = posix_acl_to_xattr(&init_user_ns, acl, buffer, size);
		posix_acl_release(acl);
		return rc;
	}
	if (handler->flags == XATTR_ACL_DEFAULT_T && !S_ISDIR(inode->i_mode))
		return -ENODATA;
#endif
	sprintf(fullname, "%s%s\n", handler->prefix, name);
	return ll_xattr_list(inode, fullname, handler->flags, buffer, size,
			     OBD_MD_FLXATTR);
}

static ssize_t ll_getxattr_lov(struct inode *inode, void *buf, size_t buf_size)
{
	ssize_t rc;

	if (S_ISREG(inode->i_mode)) {
		struct cl_object *obj = ll_i2info(inode)->lli_clob;
		struct cl_layout cl = {
			.cl_buf.lb_buf = buf,
			.cl_buf.lb_len = buf_size,
		};
		struct lu_env *env;
		u16 refcheck;

		if (!obj)
			return -ENODATA;

		env = cl_env_get(&refcheck);
		if (IS_ERR(env))
			return PTR_ERR(env);

		rc = cl_object_layout_get(env, obj, &cl);
		if (rc < 0)
			goto out_env;

		if (!cl.cl_size) {
			rc = -ENODATA;
			goto out_env;
		}

		rc = cl.cl_size;

		if (!buf_size)
			goto out_env;

		LASSERT(buf && rc <= buf_size);

		/*
		 * Do not return layout gen for getxattr() since
		 * otherwise it would confuse tar --xattr by
		 * recognizing layout gen as stripe offset when the
		 * file is restored. See LU-2809.
		 */
		((struct lov_mds_md *)buf)->lmm_layout_gen = 0;
out_env:
		cl_env_put(env, &refcheck);

		return rc;
	} else if (S_ISDIR(inode->i_mode)) {
		struct ptlrpc_request *req = NULL;
		struct lov_mds_md *lmm = NULL;
		int lmm_size = 0;

		rc = ll_dir_getstripe(inode, (void **)&lmm, &lmm_size,
				      &req, 0);
		if (rc < 0)
			goto out_req;

		if (!buf_size) {
			rc = lmm_size;
			goto out_req;
		}

		if (buf_size < lmm_size) {
			rc = -ERANGE;
			goto out_req;
		}

		memcpy(buf, lmm, lmm_size);
		rc = lmm_size;
out_req:
		if (req)
			ptlrpc_req_finished(req);

		return rc;
	} else {
		return -ENODATA;
	}
}

static int ll_xattr_get(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, void *buffer, size_t size)
{
	LASSERT(inode);
	LASSERT(name);

	CDEBUG(D_VFSTRACE, "VFS Op:inode=" DFID "(%p), xattr %s\n",
	       PFID(ll_inode2fid(inode)), inode, name);

	if (!strcmp(name, "lov")) {
		ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_GETXATTR, 1);

		return ll_getxattr_lov(inode, buffer, size);
	}

	return ll_xattr_get_common(handler, dentry, inode, name, buffer, size);
}

ssize_t ll_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = d_inode(dentry);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	char *xattr_name;
	ssize_t rc, rc2;
	size_t len, rem;

	LASSERT(inode);

	CDEBUG(D_VFSTRACE, "VFS Op:inode=" DFID "(%p)\n",
	       PFID(ll_inode2fid(inode)), inode);

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_LISTXATTR, 1);

	rc = ll_xattr_list(inode, NULL, XATTR_OTHER_T, buffer, size,
			   OBD_MD_FLXATTRLS);
	if (rc < 0)
		return rc;
	/*
	 * If we're being called to get the size of the xattr list
	 * (buf_size == 0) then just assume that a lustre.lov xattr
	 * exists.
	 */
	if (!size)
		return rc + sizeof(XATTR_LUSTRE_LOV);

	xattr_name = buffer;
	rem = rc;

	while (rem > 0) {
		len = strnlen(xattr_name, rem - 1) + 1;
		rem -= len;
		if (!xattr_type_filter(sbi, get_xattr_type(xattr_name))) {
			/* Skip OK xattr type leave it in buffer */
			xattr_name += len;
			continue;
		}

		/*
		 * Move up remaining xattrs in buffer
		 * removing the xattr that is not OK
		 */
		memmove(xattr_name, xattr_name + len, rem);
		rc -= len;
	}

	rc2 = ll_getxattr_lov(inode, NULL, 0);
	if (rc2 == -ENODATA)
		return rc;

	if (rc2 < 0)
		return rc2;

	if (size < rc + sizeof(XATTR_LUSTRE_LOV))
		return -ERANGE;

	memcpy(buffer + rc, XATTR_LUSTRE_LOV, sizeof(XATTR_LUSTRE_LOV));

	return rc + sizeof(XATTR_LUSTRE_LOV);
}

static const struct xattr_handler ll_user_xattr_handler = {
	.prefix = XATTR_USER_PREFIX,
	.flags = XATTR_USER_T,
	.get = ll_xattr_get_common,
	.set = ll_xattr_set_common,
};

static const struct xattr_handler ll_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.flags = XATTR_TRUSTED_T,
	.get = ll_xattr_get,
	.set = ll_xattr_set,
};

static const struct xattr_handler ll_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.flags = XATTR_SECURITY_T,
	.get = ll_xattr_get_common,
	.set = ll_xattr_set_common,
};

static const struct xattr_handler ll_acl_access_xattr_handler = {
	.prefix = XATTR_NAME_POSIX_ACL_ACCESS,
	.flags = XATTR_ACL_ACCESS_T,
	.get = ll_xattr_get_common,
	.set = ll_xattr_set_common,
};

static const struct xattr_handler ll_acl_default_xattr_handler = {
	.prefix = XATTR_NAME_POSIX_ACL_DEFAULT,
	.flags = XATTR_ACL_DEFAULT_T,
	.get = ll_xattr_get_common,
	.set = ll_xattr_set_common,
};

static const struct xattr_handler ll_lustre_xattr_handler = {
	.prefix = XATTR_LUSTRE_PREFIX,
	.flags = XATTR_LUSTRE_T,
	.get = ll_xattr_get,
	.set = ll_xattr_set,
};

const struct xattr_handler *ll_xattr_handlers[] = {
	&ll_user_xattr_handler,
	&ll_trusted_xattr_handler,
	&ll_security_xattr_handler,
#ifdef CONFIG_FS_POSIX_ACL
	&ll_acl_access_xattr_handler,
	&ll_acl_default_xattr_handler,
#endif
	&ll_lustre_xattr_handler,
	NULL,
};
