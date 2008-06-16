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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/idr.h>
#include <net/9p/9p.h>
#include <net/9p/transport.h>
#include <net/9p/client.h>
#include "v9fs.h"
#include "v9fs_vfs.h"

/*
  * Option Parsing (code inspired by NFS code)
  *  NOTE: each transport will parse its own options
  */

enum {
	/* Options that take integer arguments */
	Opt_debug, Opt_dfltuid, Opt_dfltgid, Opt_afid,
	/* String options */
	Opt_uname, Opt_remotename, Opt_trans,
	/* Options that take no arguments */
	Opt_nodevmap,
	/* Cache options */
	Opt_cache_loose,
	/* Access options */
	Opt_access,
	/* Error token */
	Opt_err
};

static match_table_t tokens = {
	{Opt_debug, "debug=%x"},
	{Opt_dfltuid, "dfltuid=%u"},
	{Opt_dfltgid, "dfltgid=%u"},
	{Opt_afid, "afid=%u"},
	{Opt_uname, "uname=%s"},
	{Opt_remotename, "aname=%s"},
	{Opt_nodevmap, "nodevmap"},
	{Opt_cache_loose, "cache=loose"},
	{Opt_cache_loose, "loose"},
	{Opt_access, "access=%s"},
	{Opt_err, NULL}
};

/**
 * v9fs_parse_options - parse mount options into session structure
 * @v9ses: existing v9fs session information
 *
 * Return 0 upon success, -ERRNO upon failure.
 */

static int v9fs_parse_options(struct v9fs_session_info *v9ses)
{
	char *options;
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int option = 0;
	char *s, *e;
	int ret = 0;

	/* setup defaults */
	v9ses->afid = ~0;
	v9ses->debug = 0;
	v9ses->cache = 0;

	if (!v9ses->options)
		return 0;

	options = kstrdup(v9ses->options, GFP_KERNEL);
	if (!options) {
		P9_DPRINTK(P9_DEBUG_ERROR,
			   "failed to allocate copy of option string\n");
		return -ENOMEM;
	}

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		if (token < Opt_uname) {
			int r = match_int(&args[0], &option);
			if (r < 0) {
				P9_DPRINTK(P9_DEBUG_ERROR,
					"integer field, but no integer?\n");
				ret = r;
				continue;
			}
		}
		switch (token) {
		case Opt_debug:
			v9ses->debug = option;
#ifdef CONFIG_NET_9P_DEBUG
			p9_debug_level = option;
#endif
			break;

		case Opt_dfltuid:
			v9ses->dfltuid = option;
			break;
		case Opt_dfltgid:
			v9ses->dfltgid = option;
			break;
		case Opt_afid:
			v9ses->afid = option;
			break;
		case Opt_uname:
			match_strlcpy(v9ses->uname, &args[0], PATH_MAX);
			break;
		case Opt_remotename:
			match_strlcpy(v9ses->aname, &args[0], PATH_MAX);
			break;
		case Opt_nodevmap:
			v9ses->nodev = 1;
			break;
		case Opt_cache_loose:
			v9ses->cache = CACHE_LOOSE;
			break;

		case Opt_access:
			s = match_strdup(&args[0]);
			if (!s) {
				P9_DPRINTK(P9_DEBUG_ERROR,
					   "failed to allocate copy"
					   " of option argument\n");
				ret = -ENOMEM;
				break;
			}
			v9ses->flags &= ~V9FS_ACCESS_MASK;
			if (strcmp(s, "user") == 0)
				v9ses->flags |= V9FS_ACCESS_USER;
			else if (strcmp(s, "any") == 0)
				v9ses->flags |= V9FS_ACCESS_ANY;
			else {
				v9ses->flags |= V9FS_ACCESS_SINGLE;
				v9ses->uid = simple_strtol(s, &e, 10);
				if (*e != '\0')
					v9ses->uid = ~0;
			}
			kfree(s);
			break;

		default:
			continue;
		}
	}
	kfree(options);
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
	int retval = -EINVAL;
	struct p9_fid *fid;
	int rc;

	v9ses->uname = __getname();
	if (!v9ses->uname)
		return ERR_PTR(-ENOMEM);

	v9ses->aname = __getname();
	if (!v9ses->aname) {
		__putname(v9ses->uname);
		return ERR_PTR(-ENOMEM);
	}

	v9ses->flags = V9FS_EXTENDED | V9FS_ACCESS_USER;
	strcpy(v9ses->uname, V9FS_DEFUSER);
	strcpy(v9ses->aname, V9FS_DEFANAME);
	v9ses->uid = ~0;
	v9ses->dfltuid = V9FS_DEFUID;
	v9ses->dfltgid = V9FS_DEFGID;
	if (data) {
		v9ses->options = kstrdup(data, GFP_KERNEL);
		if (!v9ses->options) {
			P9_DPRINTK(P9_DEBUG_ERROR,
			   "failed to allocate copy of option string\n");
			retval = -ENOMEM;
			goto error;
		}
	}

	rc = v9fs_parse_options(v9ses);
	if (rc < 0) {
		retval = rc;
		goto error;
	}

	v9ses->clnt = p9_client_create(dev_name, v9ses->options);

	if (IS_ERR(v9ses->clnt)) {
		retval = PTR_ERR(v9ses->clnt);
		v9ses->clnt = NULL;
		P9_DPRINTK(P9_DEBUG_ERROR, "problem initializing 9p client\n");
		goto error;
	}

	if (!v9ses->clnt->dotu)
		v9ses->flags &= ~V9FS_EXTENDED;

	v9ses->maxdata = v9ses->clnt->msize;

	/* for legacy mode, fall back to V9FS_ACCESS_ANY */
	if (!v9fs_extended(v9ses) &&
		((v9ses->flags&V9FS_ACCESS_MASK) == V9FS_ACCESS_USER)) {

		v9ses->flags &= ~V9FS_ACCESS_MASK;
		v9ses->flags |= V9FS_ACCESS_ANY;
		v9ses->uid = ~0;
	}

	fid = p9_client_attach(v9ses->clnt, NULL, v9ses->uname, ~0,
							v9ses->aname);
	if (IS_ERR(fid)) {
		retval = PTR_ERR(fid);
		fid = NULL;
		P9_DPRINTK(P9_DEBUG_ERROR, "cannot attach\n");
		goto error;
	}

	if ((v9ses->flags & V9FS_ACCESS_MASK) == V9FS_ACCESS_SINGLE)
		fid->uid = v9ses->uid;
	else
		fid->uid = ~0;

	return fid;

error:
	return ERR_PTR(retval);
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

	__putname(v9ses->uname);
	__putname(v9ses->aname);
	kfree(v9ses->options);
}

/**
 * v9fs_session_cancel - terminate a session
 * @v9ses: session to terminate
 *
 * mark transport as disconnected and cancel all pending requests.
 */

void v9fs_session_cancel(struct v9fs_session_info *v9ses) {
	P9_DPRINTK(P9_DEBUG_ERROR, "cancel session %p\n", v9ses);
	p9_client_disconnect(v9ses->clnt);
}

extern int v9fs_error_init(void);

/**
 * v9fs_init - Initialize module
 *
 */

static int __init init_v9fs(void)
{
	printk(KERN_INFO "Installing v9fs 9p2000 file system support\n");
	/* TODO: Setup list of registered trasnport modules */
	return register_filesystem(&v9fs_fs_type);
}

/**
 * v9fs_init - shutdown module
 *
 */

static void __exit exit_v9fs(void)
{
	unregister_filesystem(&v9fs_fs_type);
}

module_init(init_v9fs)
module_exit(exit_v9fs)

MODULE_AUTHOR("Latchesar Ionkov <lucho@ionkov.net>");
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_AUTHOR("Ron Minnich <rminnich@lanl.gov>");
MODULE_LICENSE("GPL");
