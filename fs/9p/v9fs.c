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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/parser.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "transport.h"
#include "mux.h"

/* TODO: sysfs or debugfs interface */
int v9fs_debug_level = 0;	/* feature-rific global debug level  */

/*
  * Option Parsing (code inspired by NFS code)
  *
  */

enum {
	/* Options that take integer arguments */
	Opt_port, Opt_msize, Opt_uid, Opt_gid, Opt_afid, Opt_debug,
	Opt_rfdno, Opt_wfdno,
	/* String options */
	Opt_uname, Opt_remotename,
	/* Options that take no arguments */
	Opt_legacy, Opt_nodevmap, Opt_unix, Opt_tcp, Opt_fd,
	/* Error token */
	Opt_err
};

static match_table_t tokens = {
	{Opt_port, "port=%u"},
	{Opt_msize, "msize=%u"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_afid, "afid=%u"},
	{Opt_rfdno, "rfdno=%u"},
	{Opt_wfdno, "wfdno=%u"},
	{Opt_debug, "debug=%x"},
	{Opt_uname, "uname=%s"},
	{Opt_remotename, "aname=%s"},
	{Opt_unix, "proto=unix"},
	{Opt_tcp, "proto=tcp"},
	{Opt_fd, "proto=fd"},
	{Opt_tcp, "tcp"},
	{Opt_unix, "unix"},
	{Opt_fd, "fd"},
	{Opt_legacy, "noextend"},
	{Opt_nodevmap, "nodevmap"},
	{Opt_err, NULL}
};

/*
 *  Parse option string.
 */

/**
 * v9fs_parse_options - parse mount options into session structure
 * @options: options string passed from mount
 * @v9ses: existing v9fs session information
 *
 */

static void v9fs_parse_options(char *options, struct v9fs_session_info *v9ses)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	int ret;

	/* setup defaults */
	v9ses->port = V9FS_PORT;
	v9ses->maxdata = 9000;
	v9ses->proto = PROTO_TCP;
	v9ses->extended = 1;
	v9ses->afid = ~0;
	v9ses->debug = 0;
	v9ses->rfdno = ~0;
	v9ses->wfdno = ~0;

	if (!options)
		return;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		if (token < Opt_uname) {
			if ((ret = match_int(&args[0], &option)) < 0) {
				dprintk(DEBUG_ERROR,
					"integer field, but no integer?\n");
				continue;
			}

		}
		switch (token) {
		case Opt_port:
			v9ses->port = option;
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
		case Opt_rfdno:
			v9ses->rfdno = option;
			break;
		case Opt_wfdno:
			v9ses->wfdno = option;
			break;
		case Opt_debug:
			v9ses->debug = option;
			break;
		case Opt_tcp:
			v9ses->proto = PROTO_TCP;
			break;
		case Opt_unix:
			v9ses->proto = PROTO_UNIX;
			break;
		case Opt_fd:
			v9ses->proto = PROTO_FD;
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
		default:
			continue;
		}
	}
}

/**
 * v9fs_inode2v9ses - safely extract v9fs session info from super block
 * @inode: inode to extract information from
 *
 * Paranoid function to extract v9ses information from superblock,
 * if anything is missing it will report an error.
 *
 */

struct v9fs_session_info *v9fs_inode2v9ses(struct inode *inode)
{
	return (inode->i_sb->s_fs_info);
}

/**
 * v9fs_get_idpool - allocate numeric id from pool
 * @p - pool to allocate from
 *
 * XXX - This seems to be an awful generic function, should it be in idr.c with
 *            the lock included in struct idr?
 */

int v9fs_get_idpool(struct v9fs_idpool *p)
{
	int i = 0;
	int error;

retry:
	if (idr_pre_get(&p->pool, GFP_KERNEL) == 0)
		return 0;

	if (down_interruptible(&p->lock) == -EINTR) {
		eprintk(KERN_WARNING, "Interrupted while locking\n");
		return -1;
	}

	/* no need to store exactly p, we just need something non-null */
	error = idr_get_new(&p->pool, p, &i);
	up(&p->lock);

	if (error == -EAGAIN)
		goto retry;
	else if (error)
		return -1;

	return i;
}

/**
 * v9fs_put_idpool - release numeric id from pool
 * @p - pool to allocate from
 *
 * XXX - This seems to be an awful generic function, should it be in idr.c with
 *            the lock included in struct idr?
 */

void v9fs_put_idpool(int id, struct v9fs_idpool *p)
{
	if (down_interruptible(&p->lock) == -EINTR) {
		eprintk(KERN_WARNING, "Interrupted while locking\n");
		return;
	}
	idr_remove(&p->pool, id);
	up(&p->lock);
}

/**
 * v9fs_check_idpool - check if the specified id is available
 * @id - id to check
 * @p - pool
 */
int v9fs_check_idpool(int id, struct v9fs_idpool *p)
{
	return idr_find(&p->pool, id) != NULL;
}

/**
 * v9fs_session_init - initialize session
 * @v9ses: session information structure
 * @dev_name: device being mounted
 * @data: options
 *
 */

int
v9fs_session_init(struct v9fs_session_info *v9ses,
		  const char *dev_name, char *data)
{
	struct v9fs_fcall *fcall = NULL;
	struct v9fs_transport *trans_proto;
	int n = 0;
	int newfid = -1;
	int retval = -EINVAL;
	struct v9fs_str *version;

	v9ses->name = __getname();
	if (!v9ses->name)
		return -ENOMEM;

	v9ses->remotename = __getname();
	if (!v9ses->remotename) {
		__putname(v9ses->name);
		return -ENOMEM;
	}

	strcpy(v9ses->name, V9FS_DEFUSER);
	strcpy(v9ses->remotename, V9FS_DEFANAME);

	v9fs_parse_options(data, v9ses);

	/* set global debug level */
	v9fs_debug_level = v9ses->debug;

	/* id pools that are session-dependent: fids and tags */
	idr_init(&v9ses->fidpool.pool);
	init_MUTEX(&v9ses->fidpool.lock);

	switch (v9ses->proto) {
	case PROTO_TCP:
		trans_proto = &v9fs_trans_tcp;
		break;
	case PROTO_UNIX:
		trans_proto = &v9fs_trans_unix;
		*v9ses->remotename = 0;
		break;
	case PROTO_FD:
		trans_proto = &v9fs_trans_fd;
		*v9ses->remotename = 0;
		break;
	default:
		printk(KERN_ERR "v9fs: Bad mount protocol %d\n", v9ses->proto);
		retval = -ENOPROTOOPT;
		goto SessCleanUp;
	};

	v9ses->transport = kmalloc(sizeof(*v9ses->transport), GFP_KERNEL);
	if (!v9ses->transport) {
		retval = -ENOMEM;
		goto SessCleanUp;
	}

	memmove(v9ses->transport, trans_proto, sizeof(*v9ses->transport));

	if ((retval = v9ses->transport->init(v9ses, dev_name, data)) < 0) {
		eprintk(KERN_ERR, "problem initializing transport\n");
		goto SessCleanUp;
	}

	v9ses->inprogress = 0;
	v9ses->shutdown = 0;
	v9ses->session_hung = 0;

	v9ses->mux = v9fs_mux_init(v9ses->transport, v9ses->maxdata + V9FS_IOHDRSZ,
		&v9ses->extended);

	if (IS_ERR(v9ses->mux)) {
		retval = PTR_ERR(v9ses->mux);
		v9ses->mux = NULL;
		dprintk(DEBUG_ERROR, "problem initializing mux\n");
		goto SessCleanUp;
	}

	if (v9ses->afid == ~0) {
		if (v9ses->extended)
			retval =
			    v9fs_t_version(v9ses, v9ses->maxdata, "9P2000.u",
					   &fcall);
		else
			retval = v9fs_t_version(v9ses, v9ses->maxdata, "9P2000",
						&fcall);

		if (retval < 0) {
			dprintk(DEBUG_ERROR, "v9fs_t_version failed\n");
			goto FreeFcall;
		}

		version = &fcall->params.rversion.version;
		if (version->len==8 && !memcmp(version->str, "9P2000.u", 8)) {
			dprintk(DEBUG_9P, "9P2000 UNIX extensions enabled\n");
			v9ses->extended = 1;
		} else if (version->len==6 && !memcmp(version->str, "9P2000", 6)) {
			dprintk(DEBUG_9P, "9P2000 legacy mode enabled\n");
			v9ses->extended = 0;
		} else {
			retval = -EREMOTEIO;
			goto FreeFcall;
		}

		n = fcall->params.rversion.msize;
		kfree(fcall);

		if (n < v9ses->maxdata)
			v9ses->maxdata = n;
	}

	newfid = v9fs_get_idpool(&v9ses->fidpool);
	if (newfid < 0) {
		eprintk(KERN_WARNING, "couldn't allocate FID\n");
		retval = -ENOMEM;
		goto SessCleanUp;
	}
	/* it is a little bit ugly, but we have to prevent newfid */
	/* being the same as afid, so if it is, get a new fid     */
	if (v9ses->afid != ~0 && newfid == v9ses->afid) {
		newfid = v9fs_get_idpool(&v9ses->fidpool);
		if (newfid < 0) {
			eprintk(KERN_WARNING, "couldn't allocate FID\n");
			retval = -ENOMEM;
			goto SessCleanUp;
		}
	}

	if ((retval =
	     v9fs_t_attach(v9ses, v9ses->name, v9ses->remotename, newfid,
			   v9ses->afid, NULL))
	    < 0) {
		dprintk(DEBUG_ERROR, "cannot attach\n");
		goto SessCleanUp;
	}

	if (v9ses->afid != ~0) {
		dprintk(DEBUG_ERROR, "afid not equal to ~0\n");
		if (v9fs_t_clunk(v9ses, v9ses->afid))
			dprintk(DEBUG_ERROR, "clunk failed\n");
	}

	return newfid;

      FreeFcall:
	kfree(fcall);

      SessCleanUp:
	v9fs_session_close(v9ses);
	return retval;
}

/**
 * v9fs_session_close - shutdown a session
 * @v9ses: session information structure
 *
 */

void v9fs_session_close(struct v9fs_session_info *v9ses)
{
	if (v9ses->mux) {
		v9fs_mux_destroy(v9ses->mux);
		v9ses->mux = NULL;
	}

	if (v9ses->transport) {
		v9ses->transport->close(v9ses->transport);
		kfree(v9ses->transport);
		v9ses->transport = NULL;
	}

	__putname(v9ses->name);
	__putname(v9ses->remotename);
}

/**
 * v9fs_session_cancel - mark transport as disconnected
 * 	and cancel all pending requests.
 */
void v9fs_session_cancel(struct v9fs_session_info *v9ses) {
	dprintk(DEBUG_ERROR, "cancel session %p\n", v9ses);
	v9ses->transport->status = Disconnected;
	v9fs_mux_cancel(v9ses->mux, -EIO);
}

extern int v9fs_error_init(void);

/**
 * v9fs_init - Initialize module
 *
 */

static int __init init_v9fs(void)
{
	int ret;

	v9fs_error_init();

	printk(KERN_INFO "Installing v9fs 9P2000 file system support\n");

	ret = v9fs_mux_global_init();
	if (!ret)
		ret = register_filesystem(&v9fs_fs_type);

	return ret;
}

/**
 * v9fs_init - shutdown module
 *
 */

static void __exit exit_v9fs(void)
{
	v9fs_mux_global_exit();
	unregister_filesystem(&v9fs_fs_type);
}

module_init(init_v9fs)
module_exit(exit_v9fs)

MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_AUTHOR("Ron Minnich <rminnich@lanl.gov>");
MODULE_LICENSE("GPL");
