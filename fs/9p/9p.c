/*
 *  linux/fs/9p/9p.c
 *
 *  This file contains functions to perform synchronous 9P calls
 *
 *  Copyright (C) 2004 by Latchesar Ionkov <lucho@ionkov.net>
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
#include "conv.h"
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
	       char *version, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "msize: %d version: %s\n", msize, version);
	tc = v9fs_create_tversion(msize, version);

	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	      u32 fid, u32 afid, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall* tc;

	dprintk(DEBUG_9P, "uname '%s' aname '%s' fid %d afid %d\n", uname,
		aname, fid, afid);

	tc = v9fs_create_tattach(fid, afid, uname, aname);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
}

static void v9fs_t_clunk_cb(void *a, struct v9fs_fcall *tc,
	struct v9fs_fcall *rc, int err)
{
	int fid;
	struct v9fs_session_info *v9ses;

	if (err)
		return;

	fid = tc->params.tclunk.fid;
	kfree(tc);

	if (!rc)
		return;

	v9ses = a;
	if (rc->id == RCLUNK)
		v9fs_put_idpool(fid, &v9ses->fidpool);

	kfree(rc);
}

/**
 * v9fs_t_clunk - release a fid (finish a transaction)
 * @v9ses: 9P2000 session information
 * @fid: fid to release
 * @fcall: pointer to response fcall pointer
 *
 */

int
v9fs_t_clunk(struct v9fs_session_info *v9ses, u32 fid)
{
	int ret;
	struct v9fs_fcall *tc, *rc;

	dprintk(DEBUG_9P, "fid %d\n", fid);

	rc = NULL;
	tc = v9fs_create_tclunk(fid);
	if (!IS_ERR(tc))
		ret = v9fs_mux_rpc(v9ses->mux, tc, &rc);
	else
		ret = PTR_ERR(tc);

	if (ret)
		dprintk(DEBUG_ERROR, "failed fid %d err %d\n", fid, ret);

	v9fs_t_clunk_cb(v9ses, tc, rc, ret);
	return ret;
}

/**
 * v9fs_v9fs_t_flush - flush a pending transaction
 * @v9ses: 9P2000 session information
 * @tag: tid to release
 *
 */

int v9fs_t_flush(struct v9fs_session_info *v9ses, u16 oldtag)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "oldtag %d\n", oldtag);

	tc = v9fs_create_tflush(oldtag);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, NULL);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
}

/**
 * v9fs_t_stat - read a file's meta-data
 * @v9ses: 9P2000 session information
 * @fid: fid pointing to file or directory to get info about
 * @fcall: pointer to response fcall
 *
 */

int
v9fs_t_stat(struct v9fs_session_info *v9ses, u32 fid, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "fid %d\n", fid);

	ret = -ENOMEM;
	tc = v9fs_create_tstat(fid);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	     struct v9fs_wstat *wstat, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "fid %d\n", fid);

	tc = v9fs_create_twstat(fid, wstat, v9ses->extended);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	    char *name, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;
	int nwname;

	dprintk(DEBUG_9P, "fid %d newfid %d wname '%s'\n", fid, newfid, name);

	if (name)
		nwname = 1;
	else
		nwname = 0;

	tc = v9fs_create_twalk(fid, newfid, nwname, &name);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	    struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "fid %d mode %d\n", fid, mode);

	tc = v9fs_create_topen(fid, mode);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	      struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "fid %d\n", fid);

	tc = v9fs_create_tremove(fid);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	      u32 perm, u8 mode, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc;

	dprintk(DEBUG_9P, "fid %d name '%s' perm %x mode %d\n",
		fid, name, perm, mode);

	tc = v9fs_create_tcreate(fid, name, perm, mode);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, rcp);
		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
	    u32 count, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc, *rc;

	dprintk(DEBUG_9P, "fid %d offset 0x%llux count 0x%x\n", fid,
		(long long unsigned) offset, count);

	tc = v9fs_create_tread(fid, offset, count);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, &rc);
		if (!ret)
			ret = rc->params.rread.count;
		if (rcp)
			*rcp = rc;
		else
			kfree(rc);

		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
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
v9fs_t_write(struct v9fs_session_info *v9ses, u32 fid, u64 offset, u32 count,
	const char __user *data, struct v9fs_fcall **rcp)
{
	int ret;
	struct v9fs_fcall *tc, *rc;

	dprintk(DEBUG_9P, "fid %d offset 0x%llux count 0x%x\n", fid,
		(long long unsigned) offset, count);

	tc = v9fs_create_twrite(fid, offset, count, data);
	if (!IS_ERR(tc)) {
		ret = v9fs_mux_rpc(v9ses->mux, tc, &rc);

		if (!ret)
			ret = rc->params.rwrite.count;
		if (rcp)
			*rcp = rc;
		else
			kfree(rc);

		kfree(tc);
	} else
		ret = PTR_ERR(tc);

	return ret;
}

