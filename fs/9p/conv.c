/*
 * linux/fs/9p/conv.c
 *
 * 9P protocol conversion functions
 *
 *  Copyright (C) 2004, 2005 by Latchesar Ionkov <lucho@ionkov.net>
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

/*
 * Buffer to help with string parsing
 */
struct cbuf {
	unsigned char *sp;
	unsigned char *p;
	unsigned char *ep;
};

static inline void buf_init(struct cbuf *buf, void *data, int datalen)
{
	buf->sp = buf->p = data;
	buf->ep = data + datalen;
}

static inline int buf_check_overflow(struct cbuf *buf)
{
	return buf->p > buf->ep;
}

static inline int buf_check_size(struct cbuf *buf, int len)
{
	if (buf->p+len > buf->ep) {
		if (buf->p < buf->ep) {
			eprintk(KERN_ERR, "buffer overflow\n");
			buf->p = buf->ep + 1;
			return 0;
		}
	}

	return 1;
}

static inline void *buf_alloc(struct cbuf *buf, int len)
{
	void *ret = NULL;

	if (buf_check_size(buf, len)) {
		ret = buf->p;
		buf->p += len;
	}

	return ret;
}

static inline void buf_put_int8(struct cbuf *buf, u8 val)
{
	if (buf_check_size(buf, 1)) {
		buf->p[0] = val;
		buf->p++;
	}
}

static inline void buf_put_int16(struct cbuf *buf, u16 val)
{
	if (buf_check_size(buf, 2)) {
		*(__le16 *) buf->p = cpu_to_le16(val);
		buf->p += 2;
	}
}

static inline void buf_put_int32(struct cbuf *buf, u32 val)
{
	if (buf_check_size(buf, 4)) {
		*(__le32 *)buf->p = cpu_to_le32(val);
		buf->p += 4;
	}
}

static inline void buf_put_int64(struct cbuf *buf, u64 val)
{
	if (buf_check_size(buf, 8)) {
		*(__le64 *)buf->p = cpu_to_le64(val);
		buf->p += 8;
	}
}

static inline void buf_put_stringn(struct cbuf *buf, const char *s, u16 slen)
{
	if (buf_check_size(buf, slen + 2)) {
		buf_put_int16(buf, slen);
		memcpy(buf->p, s, slen);
		buf->p += slen;
	}
}

static inline void buf_put_string(struct cbuf *buf, const char *s)
{
	buf_put_stringn(buf, s, strlen(s));
}

static inline void buf_put_data(struct cbuf *buf, void *data, u32 datalen)
{
	if (buf_check_size(buf, datalen)) {
		memcpy(buf->p, data, datalen);
		buf->p += datalen;
	}
}

static inline u8 buf_get_int8(struct cbuf *buf)
{
	u8 ret = 0;

	if (buf_check_size(buf, 1)) {
		ret = buf->p[0];
		buf->p++;
	}

	return ret;
}

static inline u16 buf_get_int16(struct cbuf *buf)
{
	u16 ret = 0;

	if (buf_check_size(buf, 2)) {
		ret = le16_to_cpu(*(__le16 *)buf->p);
		buf->p += 2;
	}

	return ret;
}

static inline u32 buf_get_int32(struct cbuf *buf)
{
	u32 ret = 0;

	if (buf_check_size(buf, 4)) {
		ret = le32_to_cpu(*(__le32 *)buf->p);
		buf->p += 4;
	}

	return ret;
}

static inline u64 buf_get_int64(struct cbuf *buf)
{
	u64 ret = 0;

	if (buf_check_size(buf, 8)) {
		ret = le64_to_cpu(*(__le64 *)buf->p);
		buf->p += 8;
	}

	return ret;
}

static inline int
buf_get_string(struct cbuf *buf, char *data, unsigned int datalen)
{
	u16 len = 0;

	len = buf_get_int16(buf);
	if (!buf_check_overflow(buf) && buf_check_size(buf, len) && len+1>datalen) {
		memcpy(data, buf->p, len);
		data[len] = 0;
		buf->p += len;
		len++;
	}

	return len;
}

static inline char *buf_get_stringb(struct cbuf *buf, struct cbuf *sbuf)
{
	char *ret;
	u16 len;

	ret = NULL;
	len = buf_get_int16(buf);

	if (!buf_check_overflow(buf) && buf_check_size(buf, len) &&
		buf_check_size(sbuf, len+1)) {

		memcpy(sbuf->p, buf->p, len);
		sbuf->p[len] = 0;
		ret = sbuf->p;
		buf->p += len;
		sbuf->p += len + 1;
	}

	return ret;
}

static inline int buf_get_data(struct cbuf *buf, void *data, int datalen)
{
	int ret = 0;

	if (buf_check_size(buf, datalen)) {
		memcpy(data, buf->p, datalen);
		buf->p += datalen;
		ret = datalen;
	}

	return ret;
}

static inline void *buf_get_datab(struct cbuf *buf, struct cbuf *dbuf,
				  int datalen)
{
	char *ret = NULL;
	int n = 0;

	if (buf_check_size(dbuf, datalen)) {
		n = buf_get_data(buf, dbuf->p, datalen);
		if (n > 0) {
			ret = dbuf->p;
			dbuf->p += n;
		}
	}

	return ret;
}

/**
 * v9fs_size_stat - calculate the size of a variable length stat struct
 * @v9ses: session information
 * @stat: metadata (stat) structure
 *
 */

static int v9fs_size_stat(struct v9fs_session_info *v9ses,
			  struct v9fs_stat *stat)
{
	int size = 0;

	if (stat == NULL) {
		eprintk(KERN_ERR, "v9fs_size_stat: got a NULL stat pointer\n");
		return 0;
	}

	size =			/* 2 + *//* size[2] */
	    2 +			/* type[2] */
	    4 +			/* dev[4] */
	    1 +			/* qid.type[1] */
	    4 +			/* qid.vers[4] */
	    8 +			/* qid.path[8] */
	    4 +			/* mode[4] */
	    4 +			/* atime[4] */
	    4 +			/* mtime[4] */
	    8 +			/* length[8] */
	    8;			/* minimum sum of string lengths */

	if (stat->name)
		size += strlen(stat->name);
	if (stat->uid)
		size += strlen(stat->uid);
	if (stat->gid)
		size += strlen(stat->gid);
	if (stat->muid)
		size += strlen(stat->muid);

	if (v9ses->extended) {
		size += 4 +	/* n_uid[4] */
		    4 +		/* n_gid[4] */
		    4 +		/* n_muid[4] */
		    2;		/* string length of extension[4] */
		if (stat->extension)
			size += strlen(stat->extension);
	}

	return size;
}

/**
 * serialize_stat - safely format a stat structure for transmission
 * @v9ses: session info
 * @stat: metadata (stat) structure
 * @bufp: buffer to serialize structure into
 *
 */

static int
serialize_stat(struct v9fs_session_info *v9ses, struct v9fs_stat *stat,
	       struct cbuf *bufp)
{
	buf_put_int16(bufp, stat->size);
	buf_put_int16(bufp, stat->type);
	buf_put_int32(bufp, stat->dev);
	buf_put_int8(bufp, stat->qid.type);
	buf_put_int32(bufp, stat->qid.version);
	buf_put_int64(bufp, stat->qid.path);
	buf_put_int32(bufp, stat->mode);
	buf_put_int32(bufp, stat->atime);
	buf_put_int32(bufp, stat->mtime);
	buf_put_int64(bufp, stat->length);

	buf_put_string(bufp, stat->name);
	buf_put_string(bufp, stat->uid);
	buf_put_string(bufp, stat->gid);
	buf_put_string(bufp, stat->muid);

	if (v9ses->extended) {
		buf_put_string(bufp, stat->extension);
		buf_put_int32(bufp, stat->n_uid);
		buf_put_int32(bufp, stat->n_gid);
		buf_put_int32(bufp, stat->n_muid);
	}

	if (buf_check_overflow(bufp))
		return 0;

	return stat->size;
}

/**
 * deserialize_stat - safely decode a recieved metadata (stat) structure
 * @v9ses: session info
 * @bufp: buffer to deserialize
 * @stat: metadata (stat) structure
 * @dbufp: buffer to deserialize variable strings into
 *
 */

static inline int
deserialize_stat(struct v9fs_session_info *v9ses, struct cbuf *bufp,
		 struct v9fs_stat *stat, struct cbuf *dbufp)
{

	stat->size = buf_get_int16(bufp);
	stat->type = buf_get_int16(bufp);
	stat->dev = buf_get_int32(bufp);
	stat->qid.type = buf_get_int8(bufp);
	stat->qid.version = buf_get_int32(bufp);
	stat->qid.path = buf_get_int64(bufp);
	stat->mode = buf_get_int32(bufp);
	stat->atime = buf_get_int32(bufp);
	stat->mtime = buf_get_int32(bufp);
	stat->length = buf_get_int64(bufp);
	stat->name = buf_get_stringb(bufp, dbufp);
	stat->uid = buf_get_stringb(bufp, dbufp);
	stat->gid = buf_get_stringb(bufp, dbufp);
	stat->muid = buf_get_stringb(bufp, dbufp);

	if (v9ses->extended) {
		stat->extension = buf_get_stringb(bufp, dbufp);
		stat->n_uid = buf_get_int32(bufp);
		stat->n_gid = buf_get_int32(bufp);
		stat->n_muid = buf_get_int32(bufp);
	}

	if (buf_check_overflow(bufp) || buf_check_overflow(dbufp))
		return 0;

	return stat->size + 2;
}

/**
 * deserialize_statb - wrapper for decoding a received metadata structure
 * @v9ses: session info
 * @bufp: buffer to deserialize
 * @dbufp: buffer to deserialize variable strings into
 *
 */

static inline struct v9fs_stat *deserialize_statb(struct v9fs_session_info
						  *v9ses, struct cbuf *bufp,
						  struct cbuf *dbufp)
{
	struct v9fs_stat *ret = buf_alloc(dbufp, sizeof(struct v9fs_stat));

	if (ret) {
		int n = deserialize_stat(v9ses, bufp, ret, dbufp);
		if (n <= 0)
			return NULL;
	}

	return ret;
}

/**
 * v9fs_deserialize_stat - decode a received metadata structure
 * @v9ses: session info
 * @buf: buffer to deserialize
 * @buflen: length of received buffer
 * @stat: metadata structure to decode into
 * @statlen: length of destination metadata structure
 *
 */

int
v9fs_deserialize_stat(struct v9fs_session_info *v9ses, void *buf,
		      u32 buflen, struct v9fs_stat *stat, u32 statlen)
{
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	struct cbuf dbuffer;
	struct cbuf *dbufp = &dbuffer;

	buf_init(bufp, buf, buflen);
	buf_init(dbufp, (char *)stat + sizeof(struct v9fs_stat),
		 statlen - sizeof(struct v9fs_stat));

	return deserialize_stat(v9ses, bufp, stat, dbufp);
}

static inline int
v9fs_size_fcall(struct v9fs_session_info *v9ses, struct v9fs_fcall *fcall)
{
	int size = 4 + 1 + 2;	/* size[4] msg[1] tag[2] */
	int i = 0;

	switch (fcall->id) {
	default:
		eprintk(KERN_ERR, "bad msg type %d\n", fcall->id);
		return 0;
	case TVERSION:		/* msize[4] version[s] */
		size += 4 + 2 + strlen(fcall->params.tversion.version);
		break;
	case TAUTH:		/* afid[4] uname[s] aname[s] */
		size += 4 + 2 + strlen(fcall->params.tauth.uname) +
		    2 + strlen(fcall->params.tauth.aname);
		break;
	case TFLUSH:		/* oldtag[2] */
		size += 2;
		break;
	case TATTACH:		/* fid[4] afid[4] uname[s] aname[s] */
		size += 4 + 4 + 2 + strlen(fcall->params.tattach.uname) +
		    2 + strlen(fcall->params.tattach.aname);
		break;
	case TWALK:		/* fid[4] newfid[4] nwname[2] nwname*(wname[s]) */
		size += 4 + 4 + 2;
		/* now compute total for the array of names */
		for (i = 0; i < fcall->params.twalk.nwname; i++)
			size += 2 + strlen(fcall->params.twalk.wnames[i]);
		break;
	case TOPEN:		/* fid[4] mode[1] */
		size += 4 + 1;
		break;
	case TCREATE:		/* fid[4] name[s] perm[4] mode[1] */
		size += 4 + 2 + strlen(fcall->params.tcreate.name) + 4 + 1;
		break;
	case TREAD:		/* fid[4] offset[8] count[4] */
		size += 4 + 8 + 4;
		break;
	case TWRITE:		/* fid[4] offset[8] count[4] data[count] */
		size += 4 + 8 + 4 + fcall->params.twrite.count;
		break;
	case TCLUNK:		/* fid[4] */
		size += 4;
		break;
	case TREMOVE:		/* fid[4] */
		size += 4;
		break;
	case TSTAT:		/* fid[4] */
		size += 4;
		break;
	case TWSTAT:		/* fid[4] stat[n] */
		fcall->params.twstat.stat->size =
		    v9fs_size_stat(v9ses, fcall->params.twstat.stat);
		size += 4 + 2 + 2 + fcall->params.twstat.stat->size;
	}
	return size;
}

/*
 * v9fs_serialize_fcall - marshall fcall struct into a packet
 * @v9ses: session information
 * @fcall: structure to convert
 * @data: buffer to serialize fcall into
 * @datalen: length of buffer to serialize fcall into
 *
 */

int
v9fs_serialize_fcall(struct v9fs_session_info *v9ses, struct v9fs_fcall *fcall,
		     void *data, u32 datalen)
{
	int i = 0;
	struct v9fs_stat *stat = NULL;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	buf_init(bufp, data, datalen);

	if (!fcall) {
		eprintk(KERN_ERR, "no fcall\n");
		return -EINVAL;
	}

	fcall->size = v9fs_size_fcall(v9ses, fcall);

	buf_put_int32(bufp, fcall->size);
	buf_put_int8(bufp, fcall->id);
	buf_put_int16(bufp, fcall->tag);

	dprintk(DEBUG_CONV, "size %d id %d tag %d\n", fcall->size, fcall->id,
		fcall->tag);

	/* now encode it */
	switch (fcall->id) {
	default:
		eprintk(KERN_ERR, "bad msg type: %d\n", fcall->id);
		return -EPROTO;
	case TVERSION:
		buf_put_int32(bufp, fcall->params.tversion.msize);
		buf_put_string(bufp, fcall->params.tversion.version);
		break;
	case TAUTH:
		buf_put_int32(bufp, fcall->params.tauth.afid);
		buf_put_string(bufp, fcall->params.tauth.uname);
		buf_put_string(bufp, fcall->params.tauth.aname);
		break;
	case TFLUSH:
		buf_put_int16(bufp, fcall->params.tflush.oldtag);
		break;
	case TATTACH:
		buf_put_int32(bufp, fcall->params.tattach.fid);
		buf_put_int32(bufp, fcall->params.tattach.afid);
		buf_put_string(bufp, fcall->params.tattach.uname);
		buf_put_string(bufp, fcall->params.tattach.aname);
		break;
	case TWALK:
		buf_put_int32(bufp, fcall->params.twalk.fid);
		buf_put_int32(bufp, fcall->params.twalk.newfid);
		buf_put_int16(bufp, fcall->params.twalk.nwname);
		for (i = 0; i < fcall->params.twalk.nwname; i++)
			buf_put_string(bufp, fcall->params.twalk.wnames[i]);
		break;
	case TOPEN:
		buf_put_int32(bufp, fcall->params.topen.fid);
		buf_put_int8(bufp, fcall->params.topen.mode);
		break;
	case TCREATE:
		buf_put_int32(bufp, fcall->params.tcreate.fid);
		buf_put_string(bufp, fcall->params.tcreate.name);
		buf_put_int32(bufp, fcall->params.tcreate.perm);
		buf_put_int8(bufp, fcall->params.tcreate.mode);
		break;
	case TREAD:
		buf_put_int32(bufp, fcall->params.tread.fid);
		buf_put_int64(bufp, fcall->params.tread.offset);
		buf_put_int32(bufp, fcall->params.tread.count);
		break;
	case TWRITE:
		buf_put_int32(bufp, fcall->params.twrite.fid);
		buf_put_int64(bufp, fcall->params.twrite.offset);
		buf_put_int32(bufp, fcall->params.twrite.count);
		buf_put_data(bufp, fcall->params.twrite.data,
			     fcall->params.twrite.count);
		break;
	case TCLUNK:
		buf_put_int32(bufp, fcall->params.tclunk.fid);
		break;
	case TREMOVE:
		buf_put_int32(bufp, fcall->params.tremove.fid);
		break;
	case TSTAT:
		buf_put_int32(bufp, fcall->params.tstat.fid);
		break;
	case TWSTAT:
		buf_put_int32(bufp, fcall->params.twstat.fid);
		stat = fcall->params.twstat.stat;

		buf_put_int16(bufp, stat->size + 2);
		serialize_stat(v9ses, stat, bufp);
		break;
	}

	if (buf_check_overflow(bufp))
		return -EIO;

	return fcall->size;
}

/**
 * deserialize_fcall - unmarshal a response
 * @v9ses: session information
 * @msgsize: size of rcall message
 * @buf: recieved buffer
 * @buflen: length of received buffer
 * @rcall: fcall structure to populate
 * @rcalllen: length of fcall structure to populate
 *
 */

int
v9fs_deserialize_fcall(struct v9fs_session_info *v9ses, u32 msgsize,
		       void *buf, u32 buflen, struct v9fs_fcall *rcall,
		       int rcalllen)
{

	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	struct cbuf dbuffer;
	struct cbuf *dbufp = &dbuffer;
	int i = 0;

	buf_init(bufp, buf, buflen);
	buf_init(dbufp, (char *)rcall + sizeof(struct v9fs_fcall),
		 rcalllen - sizeof(struct v9fs_fcall));

	rcall->size = msgsize;
	rcall->id = buf_get_int8(bufp);
	rcall->tag = buf_get_int16(bufp);

	dprintk(DEBUG_CONV, "size %d id %d tag %d\n", rcall->size, rcall->id,
		rcall->tag);
	switch (rcall->id) {
	default:
		eprintk(KERN_ERR, "unknown message type: %d\n", rcall->id);
		return -EPROTO;
	case RVERSION:
		rcall->params.rversion.msize = buf_get_int32(bufp);
		rcall->params.rversion.version = buf_get_stringb(bufp, dbufp);
		break;
	case RFLUSH:
		break;
	case RATTACH:
		rcall->params.rattach.qid.type = buf_get_int8(bufp);
		rcall->params.rattach.qid.version = buf_get_int32(bufp);
		rcall->params.rattach.qid.path = buf_get_int64(bufp);
		break;
	case RWALK:
		rcall->params.rwalk.nwqid = buf_get_int16(bufp);
		rcall->params.rwalk.wqids = buf_alloc(dbufp,
		      rcall->params.rwalk.nwqid * sizeof(struct v9fs_qid));
		if (rcall->params.rwalk.wqids)
			for (i = 0; i < rcall->params.rwalk.nwqid; i++) {
				rcall->params.rwalk.wqids[i].type =
				    buf_get_int8(bufp);
				rcall->params.rwalk.wqids[i].version =
				    buf_get_int16(bufp);
				rcall->params.rwalk.wqids[i].path =
				    buf_get_int64(bufp);
			}
		break;
	case ROPEN:
		rcall->params.ropen.qid.type = buf_get_int8(bufp);
		rcall->params.ropen.qid.version = buf_get_int32(bufp);
		rcall->params.ropen.qid.path = buf_get_int64(bufp);
		rcall->params.ropen.iounit = buf_get_int32(bufp);
		break;
	case RCREATE:
		rcall->params.rcreate.qid.type = buf_get_int8(bufp);
		rcall->params.rcreate.qid.version = buf_get_int32(bufp);
		rcall->params.rcreate.qid.path = buf_get_int64(bufp);
		rcall->params.rcreate.iounit = buf_get_int32(bufp);
		break;
	case RREAD:
		rcall->params.rread.count = buf_get_int32(bufp);
		rcall->params.rread.data = buf_get_datab(bufp, dbufp,
			rcall->params.rread.count);
		break;
	case RWRITE:
		rcall->params.rwrite.count = buf_get_int32(bufp);
		break;
	case RCLUNK:
		break;
	case RREMOVE:
		break;
	case RSTAT:
		buf_get_int16(bufp);
		rcall->params.rstat.stat =
		    deserialize_statb(v9ses, bufp, dbufp);
		break;
	case RWSTAT:
		break;
	case RERROR:
		rcall->params.rerror.error = buf_get_stringb(bufp, dbufp);
		if (v9ses->extended)
			rcall->params.rerror.errno = buf_get_int16(bufp);
		break;
	}

	if (buf_check_overflow(bufp) || buf_check_overflow(dbufp))
		return -EIO;

	return rcall->size;
}
