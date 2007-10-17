/*
 *  linux/fs/9p/v9fs.c
 *
 *  This file contains functions assisting in mapping VFS to 9P2000
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
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
#include <net/9p/conn.h>
#include <net/9p/client.h>
#include "v9fs.h"
#include "v9fs_vfs.h"

/*
 * Dynamic Transport Registration Routines
 *
 */

static LIST_HEAD(v9fs_trans_list);
static struct p9_trans_module *v9fs_default_trans;

/**
 * v9fs_register_trans - register a new transport with 9p
 * @m - structure describing the transport module and entry points
 *
 */
void v9fs_register_trans(struct p9_trans_module *m)
{
	list_add_tail(&m->list, &v9fs_trans_list);
	if (m->def)
		v9fs_default_trans = m;
}
EXPORT_SYMBOL(v9fs_register_trans);

/**
 * v9fs_match_trans - match transport versus registered transports
 * @arg: string identifying transport
 *
 */
static struct p9_trans_module *v9fs_match_trans(const substring_t *name)
{
	struct list_head *p;
	struct p9_trans_module *t = NULL;

	list_for_each(p, &v9fs_trans_list) {
		t = list_entry(p, struct p9_trans_module, list);
		if (strncmp(t->name, name->from, name->to-name->from) == 0) {
			P9_DPRINTK(P9_DEBUG_TRANS, "trans=%s\n", t->name);
			break;
		}
	}
	return t;
}

/*
  * Option Parsing (code inspired by NFS code)
  *  NOTE: each transport will parse its own options
  */

enum {
	/* Options that take integer arguments */
	Opt_debug, Opt_msize, Opt_uid, Opt_gid, Opt_afid,
	/* String options */
	Opt_uname, Opt_remotename, Opt_trans,
	/* Options that take no arguments */
	Opt_legacy, Opt_nodevmap,
	/* Cache options */
	Opt_cache_loose,
	/* Error token */
	Opt_err
};

static match_table_t tokens = {
	{Opt_debug, "debug=%x"},
	{Opt_msize, "msize=%u"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_afid, "afid=%u"},
	{Opt_uname, "uname=%s"},
	{Opt_remotename, "aname=%s"},
	{Opt_trans, "trans=%s"},
	{Opt_legacy, "noextend"},
	{Opt_nodevmap, "nodevmap"},
	{Opt_cache_loose, "cache=loose"},
	{Opt_cache_loose, "loose"},
	{Opt_err, NULL}
};

/**
 * v9fs_parse_options - parse mount options into session structure
 * @options: options string passed from mount
 * @v9ses: existing v9fs session information
 *
 */

static void v9fs_parse_options(struct v9fs_session_info *v9ses)
{
	char *options = v9ses->options;
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int option;
	int ret;

	/* setup defaults */
	v9ses->maxdata = 8192;
	v9ses->extended = 1;
	v9ses->afid = ~0;
	v9ses->debug = 0;
	v9ses->cache = 0;
	v9ses->trans = v9fs_default_trans;

	if (!options)
		return;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		if (token < Opt_uname) {
			if ((ret = match_int(&args[0], &option)) < 0) {
				P9_DPRINTK(P9_DEBUG_ERROR,
					"integer field, but no integer?\n");
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
		case Opt_msize:
			v9ses->maxdata = option;
			break;
		case Opt_uid:
			v9ses->uid = option;
			break;
		case Opt_gid:
			v9ses->gid = option;
			break;
		case Opt_afid:
			v9ses->afid = option;
			break;
		case Opt_trans:
			v9ses->trans = v9fs_match_trans(&args[0]);
			break;
		case Opt_uname:
			match_strcpy(v9ses->name, &args[0]);
			break;
		case Opt_remotename:
			match_strcpy(v9ses->remotename, &args[0]);
			break;
		case Opt_legacy:
			v9ses->extended = 0;
			break;
		case Opt_nodevmap:
			v9ses->nodev = 1;
			break;
		case Opt_cache_loose:
			v9ses->cache = CACHE_LOOSE;
			break;
		default:
			continue;
		}
	}
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
	struct p9_trans *trans = NULL;
	struct p9_fid *fid;

	v9ses->name = __getname();
	if (!v9ses->name)
		return ERR_PTR(-ENOMEM);

	v9ses->remotename = __getname();
	if (!v9ses->remotename) {
		__putname(v9ses->name);
		return ERR_PTR(-ENOMEM);
	}

	strcpy(v9ses->name, V9FS_DEFUSER);
	strcpy(v9ses->remotename, V9FS_DEFANAME);

	v9ses->options = kstrdup(data, GFP_KERNEL);
	v9fs_parse_options(v9ses);

	if ((v9ses->trans == NULL) && !list_empty(&v9fs_trans_list))
		v9ses->trans = list_first_entry(&v9fs_trans_list,
		 struct p9_trans_module, list);

	if (v9ses->trans == NULL) {
		retval = -EPROTONOSUPPORT;
		P9_DPRINTK(P9_DEBUG_ERROR,
				"No transport defined or default transport\n");
		goto error;
	}

	trans = v9ses->trans->create(dev_name, v9ses->options);
	if (IS_ERR(trans)) {
		retval = PTR_ERR(trans);
		trans = NULL;
		goto error;
	}
	if ((v9ses->maxdata+P9_IOHDRSZ) > v9ses->trans->maxsize)
		v9ses->maxdata = v9ses->trans->maxsize-P9_IOHDRSZ;

	v9ses->clnt = p9_client_create(trans, v9ses->maxdata+P9_IOHDRSZ,
		v9ses->extended);

	if (IS_ERR(v9ses->clnt)) {
		retval = PTR_ERR(v9ses->clnt);
		v9ses->clnt = NULL;
		P9_DPRINTK(P9_DEBUG_ERROR, "problem initializing 9p client\n");
		goto error;
	}

	fid = p9_client_attach(v9ses->clnt, NULL, v9ses->name,
							v9ses->remotename);
	if (IS_ERR(fid)) {
		retval = PTR_ERR(fid);
		fid = NULL;
		P9_DPRINTK(P9_DEBUG_ERROR, "cannot attach\n");
		goto error;
	}

	return fid;

error:
	v9fs_session_close(v9ses);
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

	__putname(v9ses->name);
	__putname(v9ses->remotename);
	kfree(v9ses->options);
}

/**
 * v9fs_session_cancel - mark transport as disconnected
 * 	and cancel all pending requests.
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
