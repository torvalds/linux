// SPDX-License-Identifier: GPL-2.0-only
/*
 *  This file contains functions assisting in mapping VFS to 9P2000
 *
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/fs_parser.h>
#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include "v9fs.h"
#include "v9fs_vfs.h"
#include "cache.h"

static DEFINE_SPINLOCK(v9fs_sessionlist_lock);
static LIST_HEAD(v9fs_sessionlist);
struct kmem_cache *v9fs_inode_cache;

/*
 * Option Parsing (code inspired by NFS code)
 *  NOTE: each transport will parse its own options
 */

enum {
	/* Mount-point source, we need to handle this explicitly because
	 * the code below accepts unknown args and the vfs layer only handles
	 * source if we rejected it as EINVAL */
	Opt_source,
	/* Options that take integer arguments */
	Opt_debug, Opt_dfltuid, Opt_dfltgid, Opt_afid,
	/* String options */
	Opt_uname, Opt_remotename, Opt_cache, Opt_cachetag,
	/* Options that take no arguments */
	Opt_nodevmap, Opt_noxattr, Opt_directio, Opt_ignoreqv,
	/* Access options */
	Opt_access, Opt_posixacl,
	/* Lock timeout option */
	Opt_locktimeout,

	/* Client options */
	Opt_msize, Opt_trans, Opt_legacy, Opt_version,

	/* fd transport options */
	/* Options that take integer arguments */
	Opt_rfdno, Opt_wfdno,
	/* Options that take no arguments */

	/* rdma transport options */
	/* Options that take integer arguments */
	Opt_rq_depth, Opt_sq_depth, Opt_timeout,

	/* Options for both fd and rdma transports */
	Opt_port, Opt_privport,
};

static const struct constant_table p9_versions[] = {
	{ "9p2000",	p9_proto_legacy },
	{ "9p2000.u",	p9_proto_2000u },
	{ "9p2000.L",	p9_proto_2000L },
	{}
};

/*
 * This structure contains all parameters used for the core code,
 * the client, and all the transports.
 */
const struct fs_parameter_spec v9fs_param_spec[] = {
	fsparam_string	("source",	Opt_source),
	fsparam_u32hex	("debug",	Opt_debug),
	fsparam_uid	("dfltuid",	Opt_dfltuid),
	fsparam_gid	("dfltgid",	Opt_dfltgid),
	fsparam_u32	("afid",	Opt_afid),
	fsparam_string	("uname",	Opt_uname),
	fsparam_string	("aname",	Opt_remotename),
	fsparam_flag	("nodevmap",	Opt_nodevmap),
	fsparam_flag	("noxattr",	Opt_noxattr),
	fsparam_flag	("directio",	Opt_directio),
	fsparam_flag	("ignoreqv",	Opt_ignoreqv),
	fsparam_string	("cache",	Opt_cache),
	fsparam_string	("cachetag",	Opt_cachetag),
	fsparam_string	("access",	Opt_access),
	fsparam_flag	("posixacl",	Opt_posixacl),
	fsparam_u32	("locktimeout",	Opt_locktimeout),

	/* client options */
	fsparam_u32	("msize",	Opt_msize),
	fsparam_flag	("noextend",	Opt_legacy),
	fsparam_string	("trans",	Opt_trans),
	fsparam_enum	("version",	Opt_version, p9_versions),

	/* fd transport options */
	fsparam_u32	("rfdno",	Opt_rfdno),
	fsparam_u32	("wfdno",	Opt_wfdno),

	/* rdma transport options */
	fsparam_u32	("sq",		Opt_sq_depth),
	fsparam_u32	("rq",		Opt_rq_depth),
	fsparam_u32	("timeout",	Opt_timeout),

	/* fd and rdma transprt options */
	fsparam_u32	("port",	Opt_port),
	fsparam_flag	("privport",	Opt_privport),
	{}
};

/* Interpret mount options for cache mode */
static int get_cache_mode(char *s)
{
	int version = -EINVAL;

	if (!strcmp(s, "loose")) {
		version = CACHE_SC_LOOSE;
		p9_debug(P9_DEBUG_9P, "Cache mode: loose\n");
	} else if (!strcmp(s, "fscache")) {
		version = CACHE_SC_FSCACHE;
		p9_debug(P9_DEBUG_9P, "Cache mode: fscache\n");
	} else if (!strcmp(s, "mmap")) {
		version = CACHE_SC_MMAP;
		p9_debug(P9_DEBUG_9P, "Cache mode: mmap\n");
	} else if (!strcmp(s, "readahead")) {
		version = CACHE_SC_READAHEAD;
		p9_debug(P9_DEBUG_9P, "Cache mode: readahead\n");
	} else if (!strcmp(s, "none")) {
		version = CACHE_SC_NONE;
		p9_debug(P9_DEBUG_9P, "Cache mode: none\n");
	} else if (kstrtoint(s, 0, &version) != 0) {
		version = -EINVAL;
		pr_info("Unknown Cache mode or invalid value %s\n", s);
	}
	return version;
}

/*
 * Display the mount options in /proc/mounts.
 */
int v9fs_show_options(struct seq_file *m, struct dentry *root)
{
	struct v9fs_session_info *v9ses = root->d_sb->s_fs_info;

	if (v9ses->debug)
		seq_printf(m, ",debug=%#x", v9ses->debug);
	if (!uid_eq(v9ses->dfltuid, V9FS_DEFUID))
		seq_printf(m, ",dfltuid=%u",
			   from_kuid_munged(&init_user_ns, v9ses->dfltuid));
	if (!gid_eq(v9ses->dfltgid, V9FS_DEFGID))
		seq_printf(m, ",dfltgid=%u",
			   from_kgid_munged(&init_user_ns, v9ses->dfltgid));
	if (v9ses->afid != ~0)
		seq_printf(m, ",afid=%u", v9ses->afid);
	if (strcmp(v9ses->uname, V9FS_DEFUSER) != 0)
		seq_printf(m, ",uname=%s", v9ses->uname);
	if (strcmp(v9ses->aname, V9FS_DEFANAME) != 0)
		seq_printf(m, ",aname=%s", v9ses->aname);
	if (v9ses->nodev)
		seq_puts(m, ",nodevmap");
	if (v9ses->cache)
		seq_printf(m, ",cache=%#x", v9ses->cache);
#ifdef CONFIG_9P_FSCACHE
	if (v9ses->cachetag && (v9ses->cache & CACHE_FSCACHE))
		seq_printf(m, ",cachetag=%s", v9ses->cachetag);
#endif

	switch (v9ses->flags & V9FS_ACCESS_MASK) {
	case V9FS_ACCESS_USER:
		seq_puts(m, ",access=user");
		break;
	case V9FS_ACCESS_ANY:
		seq_puts(m, ",access=any");
		break;
	case V9FS_ACCESS_CLIENT:
		seq_puts(m, ",access=client");
		break;
	case V9FS_ACCESS_SINGLE:
		seq_printf(m, ",access=%u",
			   from_kuid_munged(&init_user_ns, v9ses->uid));
		break;
	}

	if (v9ses->flags & V9FS_IGNORE_QV)
		seq_puts(m, ",ignoreqv");
	if (v9ses->flags & V9FS_DIRECT_IO)
		seq_puts(m, ",directio");
	if (v9ses->flags & V9FS_POSIX_ACL)
		seq_puts(m, ",posixacl");

	if (v9ses->flags & V9FS_NO_XATTR)
		seq_puts(m, ",noxattr");

	return p9_show_client_options(m, v9ses->clnt);
}

/**
 * v9fs_parse_param - parse a mount option into the filesystem context
 * @fc: the filesystem context
 * @param: the parameter to parse
 *
 * Return 0 upon success, -ERRNO upon failure.
 */
int v9fs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct v9fs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	char *s;
	int r;
	int opt;
	struct p9_client_opts	*clnt = &ctx->client_opts;
	struct p9_fd_opts	*fd_opts = &ctx->fd_opts;
	struct p9_rdma_opts	*rdma_opts = &ctx->rdma_opts;
	struct p9_session_opts	*session_opts = &ctx->session_opts;

	opt = fs_parse(fc, v9fs_param_spec, param, &result);
	if (opt < 0) {
		/*
		 * We might like to report bad mount options here, but
		 * traditionally 9p has ignored unknown mount options
		 */
		if (opt == -ENOPARAM)
			return 0;

		return opt;
	}

	switch (opt) {
	case Opt_source:
		if (fc->source) {
			pr_info("p9: multiple sources not supported\n");
			return -EINVAL;
		}
		fc->source = param->string;
		param->string = NULL;
		break;
	case Opt_debug:
		session_opts->debug = result.uint_32;
#ifdef CONFIG_NET_9P_DEBUG
		p9_debug_level = result.uint_32;
#endif
		break;

	case Opt_dfltuid:
		session_opts->dfltuid = result.uid;
		break;
	case Opt_dfltgid:
		session_opts->dfltgid = result.gid;
		break;
	case Opt_afid:
		session_opts->afid = result.uint_32;
		break;
	case Opt_uname:
		kfree(session_opts->uname);
		session_opts->uname = param->string;
		param->string = NULL;
		break;
	case Opt_remotename:
		kfree(session_opts->aname);
		session_opts->aname = param->string;
		param->string = NULL;
		break;
	case Opt_nodevmap:
		session_opts->nodev = 1;
		break;
	case Opt_noxattr:
		session_opts->flags |= V9FS_NO_XATTR;
		break;
	case Opt_directio:
		session_opts->flags |= V9FS_DIRECT_IO;
		break;
	case Opt_ignoreqv:
		session_opts->flags |= V9FS_IGNORE_QV;
		break;
	case Opt_cachetag:
#ifdef CONFIG_9P_FSCACHE
		kfree(session_opts->cachetag);
		session_opts->cachetag = param->string;
		param->string = NULL;
#endif
		break;
	case Opt_cache:
		r = get_cache_mode(param->string);
		if (r < 0)
			return r;
		session_opts->cache = r;
		break;
	case Opt_access:
		s = param->string;
		session_opts->flags &= ~V9FS_ACCESS_MASK;
		if (strcmp(s, "user") == 0) {
			session_opts->flags |= V9FS_ACCESS_USER;
		} else if (strcmp(s, "any") == 0) {
			session_opts->flags |= V9FS_ACCESS_ANY;
		} else if (strcmp(s, "client") == 0) {
			session_opts->flags |= V9FS_ACCESS_CLIENT;
		} else {
			uid_t uid;

			session_opts->flags |= V9FS_ACCESS_SINGLE;
			r = kstrtouint(s, 10, &uid);
			if (r) {
				pr_info("Unknown access argument %s: %d\n",
					param->string, r);
				return r;
			}
			session_opts->uid = make_kuid(current_user_ns(), uid);
			if (!uid_valid(session_opts->uid)) {
				pr_info("Unknown uid %s\n", s);
				return -EINVAL;
			}
		}
		break;

	case Opt_posixacl:
#ifdef CONFIG_9P_FS_POSIX_ACL
		session_opts->flags |= V9FS_POSIX_ACL;
#else
		p9_debug(P9_DEBUG_ERROR,
			 "Not defined CONFIG_9P_FS_POSIX_ACL. Ignoring posixacl option\n");
#endif
		break;

	case Opt_locktimeout:
		if (result.uint_32 < 1) {
			p9_debug(P9_DEBUG_ERROR,
				 "locktimeout must be a greater than zero integer.\n");
			return -EINVAL;
		}
		session_opts->session_lock_timeout = (long)result.uint_32 * HZ;
		break;

	/* Options for client */
	case Opt_msize:
		if (result.uint_32 < 4096) {
			p9_debug(P9_DEBUG_ERROR, "msize should be at least 4k\n");
			return -EINVAL;
		}
		if (result.uint_32 > INT_MAX) {
			p9_debug(P9_DEBUG_ERROR, "msize too big\n");
			return -EINVAL;
		}
		clnt->msize = result.uint_32;
		break;
	case Opt_trans:
		v9fs_put_trans(clnt->trans_mod);
		clnt->trans_mod = v9fs_get_trans_by_name(param->string);
		if (!clnt->trans_mod) {
			pr_info("Could not find request transport: %s\n",
				param->string);
			return -EINVAL;
		}
		break;
	case Opt_legacy:
		clnt->proto_version = p9_proto_legacy;
		break;
	case Opt_version:
		clnt->proto_version = result.uint_32;
		p9_debug(P9_DEBUG_9P, "Protocol version: %s\n", param->string);
		break;
	/* Options for fd transport */
	case Opt_rfdno:
		fd_opts->rfd = result.uint_32;
		break;
	case Opt_wfdno:
		fd_opts->wfd = result.uint_32;
		break;
	/* Options for rdma transport */
	case Opt_sq_depth:
		rdma_opts->sq_depth = result.uint_32;
		break;
	case Opt_rq_depth:
		rdma_opts->rq_depth = result.uint_32;
		break;
	case Opt_timeout:
		rdma_opts->timeout = result.uint_32;
		break;
	/* Options for both fd and rdma transports */
	case Opt_port:
		fd_opts->port = result.uint_32;
		rdma_opts->port = result.uint_32;
		break;
	case Opt_privport:
		fd_opts->privport = true;
		rdma_opts->port = true;
		break;
	}

	return 0;
}

static void v9fs_apply_options(struct v9fs_session_info *v9ses,
		  struct fs_context *fc)
{
	struct v9fs_context	*ctx = fc->fs_private;

	v9ses->debug = ctx->session_opts.debug;
	v9ses->dfltuid = ctx->session_opts.dfltuid;
	v9ses->dfltgid = ctx->session_opts.dfltgid;
	v9ses->afid = ctx->session_opts.afid;
	v9ses->uname = ctx->session_opts.uname;
	ctx->session_opts.uname = NULL;
	v9ses->aname = ctx->session_opts.aname;
	ctx->session_opts.aname = NULL;
	v9ses->nodev = ctx->session_opts.nodev;
	/*
	 * Note that we must |= flags here as session_init already
	 * set basic flags. This adds in flags from parsed options.
	 */
	v9ses->flags |= ctx->session_opts.flags;
#ifdef CONFIG_9P_FSCACHE
	v9ses->cachetag = ctx->session_opts.cachetag;
	ctx->session_opts.cachetag = NULL;
#endif
	v9ses->cache = ctx->session_opts.cache;
	v9ses->uid = ctx->session_opts.uid;
	v9ses->session_lock_timeout = ctx->session_opts.session_lock_timeout;
}

/**
 * v9fs_session_init - initialize session
 * @v9ses: session information structure
 * @fc: the filesystem mount context
 *
 */

struct p9_fid *v9fs_session_init(struct v9fs_session_info *v9ses,
		  struct fs_context *fc)
{
	struct p9_fid *fid;
	int rc = -ENOMEM;

	init_rwsem(&v9ses->rename_sem);

	v9ses->clnt = p9_client_create(fc);
	if (IS_ERR(v9ses->clnt)) {
		rc = PTR_ERR(v9ses->clnt);
		p9_debug(P9_DEBUG_ERROR, "problem initializing 9p client\n");
		goto err_names;
	}

	/*
	 * Initialize flags on the real v9ses. v9fs_apply_options below
	 * will |= the additional flags from parsed options.
	 */
	v9ses->flags = V9FS_ACCESS_USER;

	if (p9_is_proto_dotl(v9ses->clnt)) {
		v9ses->flags = V9FS_ACCESS_CLIENT;
		v9ses->flags |= V9FS_PROTO_2000L;
	} else if (p9_is_proto_dotu(v9ses->clnt)) {
		v9ses->flags |= V9FS_PROTO_2000U;
	}

	v9fs_apply_options(v9ses, fc);

	v9ses->maxdata = v9ses->clnt->msize - P9_IOHDRSZ;

	if (!v9fs_proto_dotl(v9ses) &&
	    ((v9ses->flags & V9FS_ACCESS_MASK) == V9FS_ACCESS_CLIENT)) {
		/*
		 * We support ACCESS_CLIENT only for dotl.
		 * Fall back to ACCESS_USER
		 */
		v9ses->flags &= ~V9FS_ACCESS_MASK;
		v9ses->flags |= V9FS_ACCESS_USER;
	}
	/* FIXME: for legacy mode, fall back to V9FS_ACCESS_ANY */
	if (!(v9fs_proto_dotu(v9ses) || v9fs_proto_dotl(v9ses)) &&
		((v9ses->flags&V9FS_ACCESS_MASK) == V9FS_ACCESS_USER)) {

		v9ses->flags &= ~V9FS_ACCESS_MASK;
		v9ses->flags |= V9FS_ACCESS_ANY;
		v9ses->uid = INVALID_UID;
	}
	if (!v9fs_proto_dotl(v9ses) ||
		!((v9ses->flags & V9FS_ACCESS_MASK) == V9FS_ACCESS_CLIENT)) {
		/*
		 * We support ACL checks on client only if the protocol is
		 * 9P2000.L and access is V9FS_ACCESS_CLIENT.
		 */
		v9ses->flags &= ~V9FS_ACL_MASK;
	}

	fid = p9_client_attach(v9ses->clnt, NULL, v9ses->uname, INVALID_UID,
							v9ses->aname);
	if (IS_ERR(fid)) {
		rc = PTR_ERR(fid);
		p9_debug(P9_DEBUG_ERROR, "cannot attach\n");
		goto err_clnt;
	}

	if ((v9ses->flags & V9FS_ACCESS_MASK) == V9FS_ACCESS_SINGLE)
		fid->uid = v9ses->uid;
	else
		fid->uid = INVALID_UID;

#ifdef CONFIG_9P_FSCACHE
	/* register the session for caching */
	if (v9ses->cache & CACHE_FSCACHE) {
		rc = v9fs_cache_session_get_cookie(v9ses, fc->source);
		if (rc < 0)
			goto err_clnt;
	}
#endif
	spin_lock(&v9fs_sessionlist_lock);
	list_add(&v9ses->slist, &v9fs_sessionlist);
	spin_unlock(&v9fs_sessionlist_lock);

	return fid;

err_clnt:
#ifdef CONFIG_9P_FSCACHE
	kfree(v9ses->cachetag);
#endif
	p9_client_destroy(v9ses->clnt);
err_names:
	kfree(v9ses->uname);
	kfree(v9ses->aname);
	return ERR_PTR(rc);
}

/**
 * v9fs_session_close - shutdown a session
 * @v9ses: session information structure
 *
 */

void v9fs_session_close(struct v9fs_session_info *v9ses)
{
	if (v9ses->clnt) {
		p9_client_destroy(v9ses->clnt);
		v9ses->clnt = NULL;
	}

#ifdef CONFIG_9P_FSCACHE
	fscache_relinquish_volume(v9fs_session_cache(v9ses), NULL, false);
	kfree(v9ses->cachetag);
#endif
	kfree(v9ses->uname);
	kfree(v9ses->aname);

	spin_lock(&v9fs_sessionlist_lock);
	list_del(&v9ses->slist);
	spin_unlock(&v9fs_sessionlist_lock);
}

/**
 * v9fs_session_cancel - terminate a session
 * @v9ses: session to terminate
 *
 * mark transport as disconnected and cancel all pending requests.
 */

void v9fs_session_cancel(struct v9fs_session_info *v9ses)
{
	p9_debug(P9_DEBUG_ERROR, "cancel session %p\n", v9ses);
	p9_client_disconnect(v9ses->clnt);
}

/**
 * v9fs_session_begin_cancel - Begin terminate of a session
 * @v9ses: session to terminate
 *
 * After this call we don't allow any request other than clunk.
 */

void v9fs_session_begin_cancel(struct v9fs_session_info *v9ses)
{
	p9_debug(P9_DEBUG_ERROR, "begin cancel session %p\n", v9ses);
	p9_client_begin_disconnect(v9ses->clnt);
}

static struct kobject *v9fs_kobj;

#ifdef CONFIG_9P_FSCACHE
/*
 * List caches associated with a session
 */
static ssize_t caches_show(struct kobject *kobj,
			   struct kobj_attribute *attr,
			   char *buf)
{
	ssize_t n = 0, count = 0, limit = PAGE_SIZE;
	struct v9fs_session_info *v9ses;

	spin_lock(&v9fs_sessionlist_lock);
	list_for_each_entry(v9ses, &v9fs_sessionlist, slist) {
		if (v9ses->cachetag) {
			n = snprintf(buf + count, limit, "%s\n", v9ses->cachetag);
			if (n < 0) {
				count = n;
				break;
			}

			count += n;
			limit -= n;
		}
	}

	spin_unlock(&v9fs_sessionlist_lock);
	return count;
}

static struct kobj_attribute v9fs_attr_cache = __ATTR_RO(caches);
#endif /* CONFIG_9P_FSCACHE */

static struct attribute *v9fs_attrs[] = {
#ifdef CONFIG_9P_FSCACHE
	&v9fs_attr_cache.attr,
#endif
	NULL,
};

static const struct attribute_group v9fs_attr_group = {
	.attrs = v9fs_attrs,
};

/**
 * v9fs_sysfs_init - Initialize the v9fs sysfs interface
 *
 */

static int __init v9fs_sysfs_init(void)
{
	int ret;

	v9fs_kobj = kobject_create_and_add("9p", fs_kobj);
	if (!v9fs_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(v9fs_kobj, &v9fs_attr_group);
	if (ret) {
		kobject_put(v9fs_kobj);
		return ret;
	}

	return 0;
}

/**
 * v9fs_sysfs_cleanup - Unregister the v9fs sysfs interface
 *
 */

static void v9fs_sysfs_cleanup(void)
{
	sysfs_remove_group(v9fs_kobj, &v9fs_attr_group);
	kobject_put(v9fs_kobj);
}

static void v9fs_inode_init_once(void *foo)
{
	struct v9fs_inode *v9inode = (struct v9fs_inode *)foo;

	memset(&v9inode->qid, 0, sizeof(v9inode->qid));
	inode_init_once(&v9inode->netfs.inode);
}

/**
 * v9fs_init_inode_cache - initialize a cache for 9P
 * Returns 0 on success.
 */
static int v9fs_init_inode_cache(void)
{
	v9fs_inode_cache = kmem_cache_create("v9fs_inode_cache",
					  sizeof(struct v9fs_inode),
					  0, (SLAB_RECLAIM_ACCOUNT|
					      SLAB_ACCOUNT),
					  v9fs_inode_init_once);
	if (!v9fs_inode_cache)
		return -ENOMEM;

	return 0;
}

/**
 * v9fs_destroy_inode_cache - destroy the cache of 9P inode
 *
 */
static void v9fs_destroy_inode_cache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(v9fs_inode_cache);
}

/**
 * init_v9fs - Initialize module
 *
 */

static int __init init_v9fs(void)
{
	int err;

	pr_info("Installing v9fs 9p2000 file system support\n");
	/* TODO: Setup list of registered transport modules */

	err = v9fs_init_inode_cache();
	if (err < 0) {
		pr_err("Failed to register v9fs for caching\n");
		return err;
	}

	err = v9fs_sysfs_init();
	if (err < 0) {
		pr_err("Failed to register with sysfs\n");
		goto out_cache;
	}
	err = register_filesystem(&v9fs_fs_type);
	if (err < 0) {
		pr_err("Failed to register filesystem\n");
		goto out_sysfs_cleanup;
	}

	return 0;

out_sysfs_cleanup:
	v9fs_sysfs_cleanup();

out_cache:
	v9fs_destroy_inode_cache();

	return err;
}

/**
 * exit_v9fs - shutdown module
 *
 */

static void __exit exit_v9fs(void)
{
	v9fs_sysfs_cleanup();
	v9fs_destroy_inode_cache();
	unregister_filesystem(&v9fs_fs_type);
}

module_init(init_v9fs)
module_exit(exit_v9fs)

MODULE_AUTHOR("Latchesar Ionkov <lucho@ionkov.net>");
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_AUTHOR("Ron Minnich <rminnich@lanl.gov>");
MODULE_DESCRIPTION("9P Client File System");
MODULE_LICENSE("GPL");
