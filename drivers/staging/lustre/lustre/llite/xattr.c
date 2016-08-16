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
#include <linux/selinux.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd_support.h"
#include "../include/lustre_lite.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_ver.h"
#include "../include/lustre_eacl.h"

#include "llite_internal.h"

#define XATTR_USER_T	    (1)
#define XATTR_TRUSTED_T	 (2)
#define XATTR_SECURITY_T	(3)
#define XATTR_ACL_ACCESS_T      (4)
#define XATTR_ACL_DEFAULT_T     (5)
#define XATTR_LUSTRE_T	  (6)
#define XATTR_OTHER_T	   (7)

static
int get_xattr_type(const char *name)
{
	if (!strcmp(name, XATTR_NAME_POSIX_ACL_ACCESS))
		return XATTR_ACL_ACCESS_T;

	if (!strcmp(name, XATTR_NAME_POSIX_ACL_DEFAULT))
		return XATTR_ACL_DEFAULT_T;

	if (!strncmp(name, XATTR_USER_PREFIX,
		     sizeof(XATTR_USER_PREFIX) - 1))
		return XATTR_USER_T;

	if (!strncmp(name, XATTR_TRUSTED_PREFIX,
		     sizeof(XATTR_TRUSTED_PREFIX) - 1))
		return XATTR_TRUSTED_T;

	if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1))
		return XATTR_SECURITY_T;

	if (!strncmp(name, XATTR_LUSTRE_PREFIX,
		     sizeof(XATTR_LUSTRE_PREFIX) - 1))
		return XATTR_LUSTRE_T;

	return XATTR_OTHER_T;
}

static
int xattr_type_filter(struct ll_sb_info *sbi, int xattr_type)
{
	if ((xattr_type == XATTR_ACL_ACCESS_T ||
	     xattr_type == XATTR_ACL_DEFAULT_T) &&
	   !(sbi->ll_flags & LL_SBI_ACL))
		return -EOPNOTSUPP;

	if (xattr_type == XATTR_USER_T && !(sbi->ll_flags & LL_SBI_USER_XATTR))
		return -EOPNOTSUPP;
	if (xattr_type == XATTR_TRUSTED_T && !capable(CFS_CAP_SYS_ADMIN))
		return -EPERM;
	if (xattr_type == XATTR_OTHER_T)
		return -EOPNOTSUPP;

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

	rc = xattr_type_filter(sbi, handler->flags);
	if (rc)
		return rc;

	if ((handler->flags == XATTR_ACL_ACCESS_T ||
	     handler->flags == XATTR_ACL_DEFAULT_T) &&
	    !inode_owner_or_capable(inode))
		return -EPERM;

	/* b10667: ignore lustre special xattr for now */
	if ((handler->flags == XATTR_TRUSTED_T && !strcmp(name, "lov")) ||
	    (handler->flags == XATTR_LUSTRE_T && !strcmp(name, "lov")))
		return 0;

	/* b15587: ignore security.capability xattr for now */
	if ((handler->flags == XATTR_SECURITY_T &&
	     !strcmp(name, "capability")))
		return 0;

	/* LU-549:  Disable security.selinux when selinux is disabled */
	if (handler->flags == XATTR_SECURITY_T && !selinux_is_enabled() &&
	    strcmp(name, "selinux") == 0)
		return -EOPNOTSUPP;

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

static int ll_xattr_set(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *value, size_t size,
			int flags)
{
	LASSERT(inode);
	LASSERT(name);

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p), xattr %s\n",
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

		if (lump && S_ISREG(inode->i_mode)) {
			__u64 it_flags = FMODE_WRITE;
			int lum_size = (lump->lmm_magic == LOV_USER_MAGIC_V1) ?
				sizeof(*lump) : sizeof(struct lov_user_md_v3);

			rc = ll_lov_setstripe_ea_info(inode, dentry, it_flags,
						      lump, lum_size);
			/* b10667: rc always be 0 here for now */
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

static int
ll_xattr_list(struct inode *inode, const char *name, int type, void *buffer,
	      size_t size, __u64 valid)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *req = NULL;
	struct mdt_body *body;
	void *xdata;
	int rc;

	if (sbi->ll_xattr_cache_enabled && type != XATTR_ACL_ACCESS_T) {
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
			rc = body->eadatasize;
			goto out;
		}

		if (size < body->eadatasize) {
			CERROR("server bug: replied size %u > %u\n",
			       body->eadatasize, (int)size);
			rc = -ERANGE;
			goto out;
		}

		if (body->eadatasize == 0) {
			rc = -ENODATA;
			goto out;
		}

		/* do not need swab xattr data */
		xdata = req_capsule_server_sized_get(&req->rq_pill, &RMF_EADATA,
						     body->eadatasize);
		if (!xdata) {
			rc = -EFAULT;
			goto out;
		}

		memcpy(buffer, xdata, body->eadatasize);
		rc = body->eadatasize;
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
	struct ll_inode_info *lli = ll_i2info(inode);
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p)\n",
	       PFID(ll_inode2fid(inode)), inode);

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_GETXATTR, 1);

	rc = xattr_type_filter(sbi, handler->flags);
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

static int ll_xattr_get(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, void *buffer, size_t size)
{
	LASSERT(inode);
	LASSERT(name);

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p), xattr %s\n",
	       PFID(ll_inode2fid(inode)), inode, name);

	if (!strcmp(name, "lov")) {
		struct lov_stripe_md *lsm;
		struct lov_user_md *lump;
		struct lov_mds_md *lmm = NULL;
		struct ptlrpc_request *request = NULL;
		int rc = 0, lmmsize = 0;

		ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_GETXATTR, 1);

		if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
			return -ENODATA;

		if (size == 0 && S_ISDIR(inode->i_mode)) {
			/* XXX directory EA is fix for now, optimize to save
			 * RPC transfer
			 */
			rc = sizeof(struct lov_user_md);
			goto out;
		}

		lsm = ccc_inode_lsm_get(inode);
		if (!lsm) {
			if (S_ISDIR(inode->i_mode)) {
				rc = ll_dir_getstripe(inode, (void **)&lmm,
						      &lmmsize, &request, 0);
			} else {
				rc = -ENODATA;
			}
		} else {
			/* LSM is present already after lookup/getattr call.
			 * we need to grab layout lock once it is implemented
			 */
			rc = obd_packmd(ll_i2dtexp(inode), &lmm, lsm);
			lmmsize = rc;
		}
		ccc_inode_lsm_put(inode, lsm);

		if (rc < 0)
			goto out;

		if (size == 0) {
			/* used to call ll_get_max_mdsize() forward to get
			 * the maximum buffer size, while some apps (such as
			 * rsync 3.0.x) care much about the exact xattr value
			 * size
			 */
			rc = lmmsize;
			goto out;
		}

		if (size < lmmsize) {
			CERROR("server bug: replied size %d > %d for %pd (%s)\n",
			       lmmsize, (int)size, dentry, name);
			rc = -ERANGE;
			goto out;
		}

		lump = buffer;
		memcpy(lump, lmm, lmmsize);
		/* do not return layout gen for getxattr otherwise it would
		 * confuse tar --xattr by recognizing layout gen as stripe
		 * offset when the file is restored. See LU-2809.
		 */
		lump->lmm_layout_gen = 0;

		rc = lmmsize;
out:
		if (request)
			ptlrpc_req_finished(request);
		else if (lmm)
			obd_free_diskmd(ll_i2dtexp(inode), &lmm);
		return rc;
	}

	return ll_xattr_get_common(handler, dentry, inode, name, buffer, size);
}

ssize_t ll_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = d_inode(dentry);
	int rc = 0, rc2 = 0;
	struct lov_mds_md *lmm = NULL;
	struct ptlrpc_request *request = NULL;
	int lmmsize;

	LASSERT(inode);

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p)\n",
	       PFID(ll_inode2fid(inode)), inode);

	ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_LISTXATTR, 1);

	rc = ll_xattr_list(inode, NULL, XATTR_OTHER_T, buffer, size,
			   OBD_MD_FLXATTRLS);
	if (rc < 0)
		goto out;

	if (buffer) {
		struct ll_sb_info *sbi = ll_i2sbi(inode);
		char *xattr_name = buffer;
		int xlen, rem = rc;

		while (rem > 0) {
			xlen = strnlen(xattr_name, rem - 1) + 1;
			rem -= xlen;
			if (xattr_type_filter(sbi,
					get_xattr_type(xattr_name)) == 0) {
				/* skip OK xattr type
				 * leave it in buffer
				 */
				xattr_name += xlen;
				continue;
			}
			/* move up remaining xattrs in buffer
			 * removing the xattr that is not OK
			 */
			memmove(xattr_name, xattr_name + xlen, rem);
			rc -= xlen;
		}
	}
	if (S_ISREG(inode->i_mode)) {
		if (!ll_i2info(inode)->lli_has_smd)
			rc2 = -1;
	} else if (S_ISDIR(inode->i_mode)) {
		rc2 = ll_dir_getstripe(inode, (void **)&lmm, &lmmsize,
				       &request, 0);
	}

	if (rc2 < 0) {
		rc2 = 0;
		goto out;
	} else if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)) {
		const int prefix_len = sizeof(XATTR_LUSTRE_PREFIX) - 1;
		const size_t name_len   = sizeof("lov") - 1;
		const size_t total_len  = prefix_len + name_len + 1;

		if (((rc + total_len) > size) && buffer) {
			ptlrpc_req_finished(request);
			return -ERANGE;
		}

		if (buffer) {
			buffer += rc;
			memcpy(buffer, XATTR_LUSTRE_PREFIX, prefix_len);
			memcpy(buffer + prefix_len, "lov", name_len);
			buffer[prefix_len + name_len] = '\0';
		}
		rc2 = total_len;
	}
out:
	ptlrpc_req_finished(request);
	rc = rc + rc2;

	return rc;
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
