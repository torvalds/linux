/*
 *  linux/fs/9p/v9fs.c
 *
 *  This file contains functions assisting in mapping VFS to 9P2000
 *
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/idr.h>
#include <linux/slab.h>
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
	Opt_nodevmap,
	/* Cache options */
	Opt_cache_loose, Opt_fscache, Opt_mmap,
	/* Access options */
	Opt_access, Opt_posixacl,
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
	{Opt_cache, "cache=%s"},
	{Opt_cache_loose, "loose"},
	{Opt_fscache, "fscache"},
	{Opt_mmap, "mmap"},
	{Opt_cachetag, "cachetag=%s"},
	{Opt_access, "access=%s"},
	{Opt_posixacl, "posixacl"},
	{Opt_err, NULL}
};

/* Interpret mount options for cache mode */
static int get_cache_mode(char *s)
{
	int version = -EINVAL;

	if (!strcmp(s, "loose")) {
		version = CACHE_LOOSE;
		p9_debug(P9_DEBUG_9P, "Cache mode: loose\n");
	} else if (!strcmp(s, "fscache")) {
		version = CACHE_FSCACHE;
		p9_debug(P9_DEBUG_9P, "Cache mode: fscache\n");
	} else if (!strcmp(s, "mmap")) {
		version = CACHE_MMAP;
		p9_debug(P9_DEBUG_9P, "Cache mode: mmap\n");
	} else if (!strcmp(s, "none")) {
		version = CACHE_NONE;
		p9_debug(P9_DEBUG_9P, "Cache mode: none\n");
	} else
		pr_info("Unknown Cache mode %s\n", s);
	return version;
}

/**
 * v9fs_parse_options - parse mount options into session structure
 * @v9ses: existing v9fs session information
 *
 * Return 0 upon success, -ERRNO upon failure.
 */

static int v9fs_parse_options(struct v9fs_session_info *v9ses, char *opts)
{
	char *options, *tmp_options;
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int option = 0;
	char *s, *e;
	int ret = 0;

	/* setup defaults */
	v9ses->afid = ~0;
	v9ses->debug = 0;
	v9ses->cache = CACHE_NONE;
#ifdef CONFIG_9P_FSCACHE
	v9ses->cachetag = NULL;
#endif

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
				continue;
			}
			v9ses->debug = option;
#ifdef CONFIG_NET_9P_DEBUG
			p9_debug_level = option;
#endif
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
				continue;
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
				continue;
			}
			break;
		case Opt_afid:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
				continue;
			}
			v9ses->afid = option;
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
		case Opt_cache_loose:
			v9ses->cache = CACHE_LOOSE;
			break;
		case Opt_fscache:
			v9ses->cache = CACHE_FSCACHE;
			break;
		case Opt_mmap:
			v9ses->cache = CACHE_MMAP;
			break;
		case Opt_cachetag:
#ifdef CONFIG_9P_FSCACHE
			v9ses->cachetag = match_strdup(&args[0]);
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
			ret = get_cache_mode(s);
			if (ret == -EINVAL) {
				kfree(s);
				goto free_and_return;
			}

			v9ses->cache = ret;
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
				uid = simple_strtoul(s, &e, 10);
				if (*e != '\0') {
					ret = -EINVAL;
					pr_info("Unknown access argument %s\n",
						s);
					kfree(s);
					goto free_and_return;
				}
				v9ses->uid = make_kuid(current_user_ns(), uid);
				if (!uid_valid(v9ses->uid)) {
					ret = -EINVAL;
					pr_info("Uknown uid %s\n", s);
					kfree(s);
					goto free_and_return;
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

	rc = bdi_setup_and_register(&v9ses->bdi, "9p");
	if (rc)
		goto err_names;

	v9ses->uid = INVALID_UID;
	v9ses->dfltuid = V9FS_DEFUID;
	v9ses->dfltgid = V9FS_DEFGID;

	v9ses->clnt = p9_client_create(dev_name, data);
	if (IS_ERR(v9ses->clnt)) {
		rc = PTR_ERR(v9ses->clnt);
		p9_debug(P9_DEBUG_ERROR, "problem initializing 9p client\n");
		goto err_bdi;
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
	/*FIXME !! */
	/* for legacy mode, fall back to V9FS_ACCESS_ANY */
	if (!(v9fs_proto_dotu(v9ses) || v9fs_proto_dotl(v9ses)) &&
		((v9ses->flags&V9FS_ACCESS_MASK) == V9FS_ACCESS_USER)) {

		v9ses->flags &= ~V9FS_ACCESS_MASK;
		v9ses->flags |= V9FS_ACCESS_ANY;
		v9ses->uid = INVALID_UID;
	}
	if (!v9fs_proto_dotl(v9ses) ||
		!((v9ses->flags & V9FS_ACCESS_MASK) == V9FS_ACCESS_CLIENT)) {
		/*
		 * We support ACL checks on clinet only if the protocol is
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
	v9fs_cache_session_get_cookie(v9ses);
#endif
	spin_lock(&v9fs_sessionlist_lock);
	list_add(&v9ses->slist, &v9fs_sessionlist);
	spin_unlock(&v9fs_sessionlist_lock);

	return fid;

err_clnt:
	p9_client_destroy(v9ses->clnt);
err_bdi:
	bdi_destroy(&v9ses->bdi);
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
	if (v9ses->fscache) {
		v9fs_cache_session_put_cookie(v9ses);
		kfree(v9ses->cachetag);
	}
#endif
	kfree(v9ses->uname);
	kfree(v9ses->aname);

	bdi_destroy(&v9ses->bdi);

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

void v9fs_session_cancel(struct v9fs_session_info *v9ses) {
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

extern int v9fs_error_init(void);

static struct kobject *v9fs_kobj;

#ifdef CONFIG_9P_FSCACHE
/**
 * caches_show - list caches associated with a session
 *
 * Returns the size of buffer written.
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
			n = snprintf(buf, limit, "%s\n", v9ses->cachetag);
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

static struct attribute_group v9fs_attr_group = {
	.attrs = v9fs_attrs,
};

/**
 * v9fs_sysfs_init - Initialize the v9fs sysfs interface
 *
 */

static int __init v9fs_sysfs_init(void)
{
	v9fs_kobj = kobject_create_and_add("9p", fs_kobj);
	if (!v9fs_kobj)
		return -ENOMEM;

	if (sysfs_create_group(v9fs_kobj, &v9fs_attr_group)) {
		kobject_put(v9fs_kobj);
		return -ENOMEM;
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
#ifdef CONFIG_9P_FSCACHE
	v9inode->fscache = NULL;
#endif
	memset(&v9inode->qid, 0, sizeof(v9inode->qid));
	inode_init_once(&v9inode->vfs_inode);
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
					      SLAB_MEM_SPREAD|SLAB_ACCOUNT),
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

static int v9fs_cache_register(void)
{
	int ret;
	ret = v9fs_init_inode_cache();
	if (ret < 0)
		return ret;
#ifdef CONFIG_9P_FSCACHE
	ret = fscache_register_netfs(&v9fs_cache_netfs);
	if (ret < 0)
		v9fs_destroy_inode_cache();
#endif
	return ret;
}

static void v9fs_cache_unregister(void)
{
	v9fs_destroy_inode_cache();
#ifdef CONFIG_9P_FSCACHE
	fscache_unregister_netfs(&v9fs_cache_netfs);
#endif
}

/**
 * init_v9fs - Initialize module
 *
 */

static int __init init_v9fs(void)
{
	int err;
	pr_info("Installing v9fs 9p2000 file system support\n");
	/* TODO: Setup list of registered trasnport modules */

	err = v9fs_cache_register();
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
	v9fs_cache_unregister();

	return err;
}

/**
 * exit_v9fs - shutdown module
 *
 */

static void __exit exit_v9fs(void)
{
	v9fs_sysfs_cleanup();
	v9fs_cache_unregister();
	unregister_filesystem(&v9fs_fs_type);
}

module_init(init_v9fs)
module_exit(exit_v9fs)

MODULE_AUTHOR("Latchesar Ionkov <lucho@ionkov.net>");
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_AUTHOR("Ron Minnich <rminnich@lanl.gov>");
MODULE_LICENSE("GPL");
