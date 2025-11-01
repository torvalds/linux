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
#include <linux/parser.h>
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
	/* Error token */
	Opt_err
};

static const match_table_t tokens = {
	{Opt_debug, "debug=%x"},
	{Opt_dfltuid, "dfltuid=%u"},
	{Opt_dfltgid, "dfltgid=%u"},
	{Opt_afid, "afid=%u"},
	{Opt_uname, "uname=%s"},
	{Opt_remotename, "aname=%s"},
	{Opt_nodevmap, "nodevmap"},
	{Opt_noxattr, "noxattr"},
	{Opt_directio, "directio"},
	{Opt_ignoreqv, "ignoreqv"},
	{Opt_cache, "cache=%s"},
	{Opt_cachetag, "cachetag=%s"},
	{Opt_access, "access=%s"},
	{Opt_posixacl, "posixacl"},
	{Opt_locktimeout, "locktimeout=%u"},
	{Opt_err, NULL}
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
		seq_printf(m, ",debug=%x", v9ses->debug);
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
		seq_printf(m, ",cache=%x", v9ses->cache);
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
 * v9fs_parse_options - parse mount options into session structure
 * @v9ses: existing v9fs session information
 * @opts: The mount option string
 *
 * Return 0 upon success, -ERRNO upon failure.
 */

static int v9fs_parse_options(struct v9fs_session_info *v9ses, char *opts)
{
	char *options, *tmp_options;
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int option = 0;
	char *s;
	int ret = 0;

	/* setup defaults */
	v9ses->afid = ~0;
	v9ses->debug = 0;
	v9ses->cache = CACHE_NONE;
#ifdef CONFIG_9P_FSCACHE
	v9ses->cachetag = NULL;
#endif
	v9ses->session_lock_timeout = P9_LOCK_TIMEOUT;

	if (!opts)
		return 0;

	tmp_options = kstrdup(opts, GFP_KERNEL);
	if (!tmp_options) {
		ret = -ENOMEM;
		goto fail_option_alloc;
	}
	options = tmp_options;

	while ((p = strsep(&options, ",")) != NULL) {
		int token, r;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_debug:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
			} else {
				v9ses->debug = option;
#ifdef CONFIG_NET_9P_DEBUG
				p9_debug_level = option;
#endif
			}
			break;

		case Opt_dfltuid:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
				continue;
			}
			v9ses->dfltuid = make_kuid(current_user_ns(), option);
			if (!uid_valid(v9ses->dfltuid)) {
				p9_debug(P9_DEBUG_ERROR,
					 "uid field, but not a uid?\n");
				ret = -EINVAL;
			}
			break;
		case Opt_dfltgid:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
				continue;
			}
			v9ses->dfltgid = make_kgid(current_user_ns(), option);
			if (!gid_valid(v9ses->dfltgid)) {
				p9_debug(P9_DEBUG_ERROR,
					 "gid field, but not a gid?\n");
				ret = -EINVAL;
			}
			break;
		case Opt_afid:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
			} else {
				v9ses->afid = option;
			}
			break;
		case Opt_uname:
			kfree(v9ses->uname);
			v9ses->uname = match_strdup(&args[0]);
			if (!v9ses->uname) {
				ret = -ENOMEM;
				goto free_and_return;
			}
			break;
		case Opt_remotename:
			kfree(v9ses->aname);
			v9ses->aname = match_strdup(&args[0]);
			if (!v9ses->aname) {
				ret = -ENOMEM;
				goto free_and_return;
			}
			break;
		case Opt_nodevmap:
			v9ses->nodev = 1;
			break;
		case Opt_noxattr:
			v9ses->flags |= V9FS_NO_XATTR;
			break;
		case Opt_directio:
			v9ses->flags |= V9FS_DIRECT_IO;
			break;
		case Opt_ignoreqv:
			v9ses->flags |= V9FS_IGNORE_QV;
			break;
		case Opt_cachetag:
#ifdef CONFIG_9P_FSCACHE
			kfree(v9ses->cachetag);
			v9ses->cachetag = match_strdup(&args[0]);
			if (!v9ses->cachetag) {
				ret = -ENOMEM;
				goto free_and_return;
			}
#endif
			break;
		case Opt_cache:
			s = match_strdup(&args[0]);
			if (!s) {
				ret = -ENOMEM;
				p9_debug(P9_DEBUG_ERROR,
					 "problem allocating copy of cache arg\n");
				goto free_and_return;
			}
			r = get_cache_mode(s);
			if (r < 0)
				ret = r;
			else
				v9ses->cache = r;

			kfree(s);
			break;

		case Opt_access:
			s = match_strdup(&args[0]);
			if (!s) {
				ret = -ENOMEM;
				p9_debug(P9_DEBUG_ERROR,
					 "problem allocating copy of access arg\n");
				goto free_and_return;
			}

			v9ses->flags &= ~V9FS_ACCESS_MASK;
			if (strcmp(s, "user") == 0)
				v9ses->flags |= V9FS_ACCESS_USER;
			else if (strcmp(s, "any") == 0)
				v9ses->flags |= V9FS_ACCESS_ANY;
			else if (strcmp(s, "client") == 0) {
				v9ses->flags |= V9FS_ACCESS_CLIENT;
			} else {
				uid_t uid;

				v9ses->flags |= V9FS_ACCESS_SINGLE;
				r = kstrtouint(s, 10, &uid);
				if (r) {
					ret = r;
					pr_info("Unknown access argument %s: %d\n",
						s, r);
					kfree(s);
					continue;
				}
				v9ses->uid = make_kuid(current_user_ns(), uid);
				if (!uid_valid(v9ses->uid)) {
					ret = -EINVAL;
					pr_info("Unknown uid %s\n", s);
				}
			}

			kfree(s);
			break;

		case Opt_posixacl:
#ifdef CONFIG_9P_FS_POSIX_ACL
			v9ses->flags |= V9FS_POSIX_ACL;
#else
			p9_debug(P9_DEBUG_ERROR,
				 "Not defined CONFIG_9P_FS_POSIX_ACL. Ignoring posixacl option\n");
#endif
			break;

		case Opt_locktimeout:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
				continue;
			}
			if (option < 1) {
				p9_debug(P9_DEBUG_ERROR,
					 "locktimeout must be a greater than zero integer.\n");
				ret = -EINVAL;
				continue;
			}
			v9ses->session_lock_timeout = (long)option * HZ;
			break;

		default:
			continue;
		}
	}

free_and_return:
	kfree(tmp_options);
fail_option_alloc:
	return ret;
}

/**
 * v9fs_session_init - initialize session
 * @v9ses: session information structure
 * @dev_name: device being mounted
 * @data: options
 *
 */

struct p9_fid *v9fs_session_init(struct v9fs_session_info *v9ses,
		  const char *dev_name, char *data)
{
	struct p9_fid *fid;
	int rc = -ENOMEM;

	v9ses->uname = kstrdup(V9FS_DEFUSER, GFP_KERNEL);
	if (!v9ses->uname)
		goto err_names;

	v9ses->aname = kstrdup(V9FS_DEFANAME, GFP_KERNEL);
	if (!v9ses->aname)
		goto err_names;
	init_rwsem(&v9ses->rename_sem);

	v9ses->uid = INVALID_UID;
	v9ses->dfltuid = V9FS_DEFUID;
	v9ses->dfltgid = V9FS_DEFGID;

	v9ses->clnt = p9_client_create(dev_name, data);
	if (IS_ERR(v9ses->clnt)) {
		rc = PTR_ERR(v9ses->clnt);
		p9_debug(P9_DEBUG_ERROR, "problem initializing 9p client\n");
		goto err_names;
	}

	v9ses->flags = V9FS_ACCESS_USER;

	if (p9_is_proto_dotl(v9ses->clnt)) {
		v9ses->flags = V9FS_ACCESS_CLIENT;
		v9ses->flags |= V9FS_PROTO_2000L;
	} else if (p9_is_proto_dotu(v9ses->clnt)) {
		v9ses->flags |= V9FS_PROTO_2000U;
	}

	rc = v9fs_parse_options(v9ses, data);
	if (rc < 0)
		goto err_clnt;

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
		rc = v9fs_cache_session_get_cookie(v9ses, dev_name);
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
