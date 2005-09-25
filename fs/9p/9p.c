/*
 *  linux/fs/9p/9p.c
 *
 *  This file contains functions 9P2000 functions
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "mux.h"

/**
 * v9fs_t_version - negotiate protocol parameters with sever
 * @v9ses: 9P2000 session information
 * @msize: requested max size packet
 * @version: requested version.extension string
 * @fcall: pointer to response fcall pointer
 *
 */

int
v9fs_t_version(struct v9fs_session_info *v9ses, u32 msize,
	       char *version, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "msize: %d version: %s\n", msize, version);
	msg.id = TVERSION;
	msg.params.tversion.msize = msize;
	msg.params.tversion.version = version;

	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_attach - mount the server
 * @v9ses: 9P2000 session information
 * @uname: user name doing the attach
 * @aname: remote name being attached to
 * @fid: mount fid to attatch to root node
 * @afid: authentication fid (in this case result key)
 * @fcall: pointer to response fcall pointer
 *
 */

int
v9fs_t_attach(struct v9fs_session_info *v9ses, char *uname, char *aname,
	      u32 fid, u32 afid, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "uname '%s' aname '%s' fid %d afid %d\n", uname,
		aname, fid, afid);
	msg.id = TATTACH;
	msg.params.tattach.fid = fid;
	msg.params.tattach.afid = afid;
	msg.params.tattach.uname = uname;
	msg.params.tattach.aname = aname;

	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_clunk - release a fid (finish a transaction)
 * @v9ses: 9P2000 session information
 * @fid: fid to release
 * @fcall: pointer to response fcall pointer
 *
 */

int
v9fs_t_clunk(struct v9fs_session_info *v9ses, u32 fid,
	     struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "fid %d\n", fid);
	msg.id = TCLUNK;
	msg.params.tclunk.fid = fid;

	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_v9fs_t_flush - flush a pending transaction
 * @v9ses: 9P2000 session information
 * @tag: tid to release
 *
 */

int v9fs_t_flush(struct v9fs_session_info *v9ses, u16 tag)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "oldtag %d\n", tag);
	msg.id = TFLUSH;
	msg.params.tflush.oldtag = tag;
	return v9fs_mux_rpc(v9ses, &msg, NULL);
}

/**
 * v9fs_t_stat - read a file's meta-data
 * @v9ses: 9P2000 session information
 * @fid: fid pointing to file or directory to get info about
 * @fcall: pointer to response fcall
 *
 */

int
v9fs_t_stat(struct v9fs_session_info *v9ses, u32 fid, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "fid %d\n", fid);
	if (fcall)
		*fcall = NULL;

	msg.id = TSTAT;
	msg.params.tstat.fid = fid;
	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_wstat - write a file's meta-data
 * @v9ses: 9P2000 session information
 * @fid: fid pointing to file or directory to write info about
 * @stat: metadata
 * @fcall: pointer to response fcall
 *
 */

int
v9fs_t_wstat(struct v9fs_session_info *v9ses, u32 fid,
	     struct v9fs_stat *stat, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "fid %d length %d\n", fid, (int)stat->length);
	msg.id = TWSTAT;
	msg.params.twstat.fid = fid;
	msg.params.twstat.stat = stat;

	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_walk - walk a fid to a new file or directory
 * @v9ses: 9P2000 session information
 * @fid: fid to walk
 * @newfid: new fid (for clone operations)
 * @name: path to walk fid to
 * @fcall: pointer to response fcall
 *
 */

/* TODO: support multiple walk */

int
v9fs_t_walk(struct v9fs_session_info *v9ses, u32 fid, u32 newfid,
	    char *name, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "fid %d newfid %d wname '%s'\n", fid, newfid, name);
	msg.id = TWALK;
	msg.params.twalk.fid = fid;
	msg.params.twalk.newfid = newfid;

	if (name) {
		msg.params.twalk.nwname = 1;
		msg.params.twalk.wnames = &name;
	} else {
		msg.params.twalk.nwname = 0;
	}

	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_open - open a file
 *
 * @v9ses - 9P2000 session information
 * @fid - fid to open
 * @mode - mode to open file (R, RW, etc)
 * @fcall - pointer to response fcall
 *
 */

int
v9fs_t_open(struct v9fs_session_info *v9ses, u32 fid, u8 mode,
	    struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;
	long errorno = -1;

	dprintk(DEBUG_9P, "fid %d mode %d\n", fid, mode);
	msg.id = TOPEN;
	msg.params.topen.fid = fid;
	msg.params.topen.mode = mode;

	errorno = v9fs_mux_rpc(v9ses, &msg, fcall);

	return errorno;
}

/**
 * v9fs_t_remove - remove a file or directory
 * @v9ses: 9P2000 session information
 * @fid: fid to remove
 * @fcall: pointer to response fcall
 *
 */

int
v9fs_t_remove(struct v9fs_session_info *v9ses, u32 fid,
	      struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "fid %d\n", fid);
	msg.id = TREMOVE;
	msg.params.tremove.fid = fid;
	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_create - create a file or directory
 * @v9ses: 9P2000 session information
 * @fid: fid to create
 * @name: name of the file or directory to create
 * @perm: permissions to create with
 * @mode: mode to open file (R, RW, etc)
 * @fcall: pointer to response fcall
 *
 */

int
v9fs_t_create(struct v9fs_session_info *v9ses, u32 fid, char *name,
	      u32 perm, u8 mode, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;

	dprintk(DEBUG_9P, "fid %d name '%s' perm %x mode %d\n",
		fid, name, perm, mode);

	msg.id = TCREATE;
	msg.params.tcreate.fid = fid;
	msg.params.tcreate.name = name;
	msg.params.tcreate.perm = perm;
	msg.params.tcreate.mode = mode;

	return v9fs_mux_rpc(v9ses, &msg, fcall);
}

/**
 * v9fs_t_read - read data
 * @v9ses: 9P2000 session information
 * @fid: fid to read from
 * @offset: offset to start read at
 * @count: how many bytes to read
 * @fcall: pointer to response fcall (with data)
 *
 */

int
v9fs_t_read(struct v9fs_session_info *v9ses, u32 fid, u64 offset,
	    u32 count, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;
	struct v9fs_fcall *rc = NULL;
	long errorno = -1;

	dprintk(DEBUG_9P, "fid %d offset 0x%lx count 0x%x\n", fid,
		(long unsigned int)offset, count);
	msg.id = TREAD;
	msg.params.tread.fid = fid;
	msg.params.tread.offset = offset;
	msg.params.tread.count = count;
	errorno = v9fs_mux_rpc(v9ses, &msg, &rc);

	if (!errorno) {
		errorno = rc->params.rread.count;
		dump_data(rc->params.rread.data, rc->params.rread.count);
	}

	if (fcall)
		*fcall = rc;
	else
		kfree(rc);

	return errorno;
}

/**
 * v9fs_t_write - write data
 * @v9ses: 9P2000 session information
 * @fid: fid to write to
 * @offset: offset to start write at
 * @count: how many bytes to write
 * @fcall: pointer to response fcall
 *
 */

int
v9fs_t_write(struct v9fs_session_info *v9ses, u32 fid,
	     u64 offset, u32 count, void *data, struct v9fs_fcall **fcall)
{
	struct v9fs_fcall msg;
	struct v9fs_fcall *rc = NULL;
	long errorno = -1;

	dprintk(DEBUG_9P, "fid %d offset 0x%llx count 0x%x\n", fid,
		(unsigned long long)offset, count);
	dump_data(data, count);

	msg.id = TWRITE;
	msg.params.twrite.fid = fid;
	msg.params.twrite.offset = offset;
	msg.params.twrite.count = count;
	msg.params.twrite.data = data;

	errorno = v9fs_mux_rpc(v9ses, &msg, &rc);

	if (!errorno)
		errorno = rc->params.rwrite.count;

	if (fcall)
		*fcall = rc;
	else
		kfree(rc);

	return errorno;
}
