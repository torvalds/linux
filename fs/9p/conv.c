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
#include <linux/idr.h>
#include <asm/uaccess.h>
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

static int buf_check_size(struct cbuf *buf, int len)
{
	if (buf->p + len > buf->ep) {
		if (buf->p < buf->ep) {
			eprintk(KERN_ERR, "buffer overflow: want %d has %d\n",
				len, (int)(buf->ep - buf->p));
			dump_stack();
			buf->p = buf->ep + 1;
		}

		return 0;
	}

	return 1;
}

static void *buf_alloc(struct cbuf *buf, int len)
{
	void *ret = NULL;

	if (buf_check_size(buf, len)) {
		ret = buf->p;
		buf->p += len;
	}

	return ret;
}

static void buf_put_int8(struct cbuf *buf, u8 val)
{
	if (buf_check_size(buf, 1)) {
		buf->p[0] = val;
		buf->p++;
	}
}

static void buf_put_int16(struct cbuf *buf, u16 val)
{
	if (buf_check_size(buf, 2)) {
		*(__le16 *) buf->p = cpu_to_le16(val);
		buf->p += 2;
	}
}

static void buf_put_int32(struct cbuf *buf, u32 val)
{
	if (buf_check_size(buf, 4)) {
		*(__le32 *)buf->p = cpu_to_le32(val);
		buf->p += 4;
	}
}

static void buf_put_int64(struct cbuf *buf, u64 val)
{
	if (buf_check_size(buf, 8)) {
		*(__le64 *)buf->p = cpu_to_le64(val);
		buf->p += 8;
	}
}

static char *buf_put_stringn(struct cbuf *buf, const char *s, u16 slen)
{
	char *ret;

	ret = NULL;
	if (buf_check_size(buf, slen + 2)) {
		buf_put_int16(buf, slen);
		ret = buf->p;
		memcpy(buf->p, s, slen);
		buf->p += slen;
	}

	return ret;
}

static inline void buf_put_string(struct cbuf *buf, const char *s)
{
	buf_put_stringn(buf, s, strlen(s));
}

static u8 buf_get_int8(struct cbuf *buf)
{
	u8 ret = 0;

	if (buf_check_size(buf, 1)) {
		ret = buf->p[0];
		buf->p++;
	}

	return ret;
}

static u16 buf_get_int16(struct cbuf *buf)
{
	u16 ret = 0;

	if (buf_check_size(buf, 2)) {
		ret = le16_to_cpu(*(__le16 *)buf->p);
		buf->p += 2;
	}

	return ret;
}

static u32 buf_get_int32(struct cbuf *buf)
{
	u32 ret = 0;

	if (buf_check_size(buf, 4)) {
		ret = le32_to_cpu(*(__le32 *)buf->p);
		buf->p += 4;
	}

	return ret;
}

static u64 buf_get_int64(struct cbuf *buf)
{
	u64 ret = 0;

	if (buf_check_size(buf, 8)) {
		ret = le64_to_cpu(*(__le64 *)buf->p);
		buf->p += 8;
	}

	return ret;
}

static void buf_get_str(struct cbuf *buf, struct v9fs_str *vstr)
{
	vstr->len = buf_get_int16(buf);
	if (!buf_check_overflow(buf) && buf_check_size(buf, vstr->len)) {
		vstr->str = buf->p;
		buf->p += vstr->len;
	} else {
		vstr->len = 0;
		vstr->str = NULL;
	}
}

static void buf_get_qid(struct cbuf *bufp, struct v9fs_qid *qid)
{
	qid->type = buf_get_int8(bufp);
	qid->version = buf_get_int32(bufp);
	qid->path = buf_get_int64(bufp);
}

/**
 * v9fs_size_wstat - calculate the size of a variable length stat struct
 * @stat: metadata (stat) structure
 * @extended: non-zero if 9P2000.u
 *
 */

static int v9fs_size_wstat(struct v9fs_wstat *wstat, int extended)
{
	int size = 0;

	if (wstat == NULL) {
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

	if (wstat->name)
		size += strlen(wstat->name);
	if (wstat->uid)
		size += strlen(wstat->uid);
	if (wstat->gid)
		size += strlen(wstat->gid);
	if (wstat->muid)
		size += strlen(wstat->muid);

	if (extended) {
		size += 4 +	/* n_uid[4] */
		    4 +		/* n_gid[4] */
		    4 +		/* n_muid[4] */
		    2;		/* string length of extension[4] */
		if (wstat->extension)
			size += strlen(wstat->extension);
	}

	return size;
}

/**
 * buf_get_stat - safely decode a recieved metadata (stat) structure
 * @bufp: buffer to deserialize
 * @stat: metadata (stat) structure
 * @extended: non-zero if 9P2000.u
 *
 */

static void
buf_get_stat(struct cbuf *bufp, struct v9fs_stat *stat, int extended)
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
	buf_get_str(bufp, &stat->name);
	buf_get_str(bufp, &stat->uid);
	buf_get_str(bufp, &stat->gid);
	buf_get_str(bufp, &stat->muid);

	if (extended) {
		buf_get_str(bufp, &stat->extension);
		stat->n_uid = buf_get_int32(bufp);
		stat->n_gid = buf_get_int32(bufp);
		stat->n_muid = buf_get_int32(bufp);
	}
}

/**
 * v9fs_deserialize_stat - decode a received metadata structure
 * @buf: buffer to deserialize
 * @buflen: length of received buffer
 * @stat: metadata structure to decode into
 * @extended: non-zero if 9P2000.u
 *
 * Note: stat will point to the buf region.
 */

int
v9fs_deserialize_stat(void *buf, u32 buflen, struct v9fs_stat *stat,
		int extended)
{
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	unsigned char *p;

	buf_init(bufp, buf, buflen);
	p = bufp->p;
	buf_get_stat(bufp, stat, extended);

	if (buf_check_overflow(bufp))
		return 0;
	else
		return bufp->p - p;
}

/**
 * deserialize_fcall - unmarshal a response
 * @buf: recieved buffer
 * @buflen: length of received buffer
 * @rcall: fcall structure to populate
 * @rcalllen: length of fcall structure to populate
 * @extended: non-zero if 9P2000.u
 *
 */

int
v9fs_deserialize_fcall(void *buf, u32 buflen, struct v9fs_fcall *rcall,
		       int extended)
{

	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	int i = 0;

	buf_init(bufp, buf, buflen);

	rcall->size = buf_get_int32(bufp);
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
		buf_get_str(bufp, &rcall->params.rversion.version);
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
		if (rcall->params.rwalk.nwqid > V9FS_MAXWELEM) {
			eprintk(KERN_ERR, "Rwalk with more than %d qids: %d\n",
				V9FS_MAXWELEM, rcall->params.rwalk.nwqid);
			return -EPROTO;
		}

		for (i = 0; i < rcall->params.rwalk.nwqid; i++)
			buf_get_qid(bufp, &rcall->params.rwalk.wqids[i]);
		break;
	case ROPEN:
		buf_get_qid(bufp, &rcall->params.ropen.qid);
		rcall->params.ropen.iounit = buf_get_int32(bufp);
		break;
	case RCREATE:
		buf_get_qid(bufp, &rcall->params.rcreate.qid);
		rcall->params.rcreate.iounit = buf_get_int32(bufp);
		break;
	case RREAD:
		rcall->params.rread.count = buf_get_int32(bufp);
		rcall->params.rread.data = bufp->p;
		buf_check_size(bufp, rcall->params.rread.count);
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
		buf_get_stat(bufp, &rcall->params.rstat.stat, extended);
		break;
	case RWSTAT:
		break;
	case RERROR:
		buf_get_str(bufp, &rcall->params.rerror.error);
		if (extended)
			rcall->params.rerror.errno = buf_get_int16(bufp);
		break;
	}

	if (buf_check_overflow(bufp)) {
		dprintk(DEBUG_ERROR, "buffer overflow\n");
		return -EIO;
	}

	return bufp->p - bufp->sp;
}

static inline void v9fs_put_int8(struct cbuf *bufp, u8 val, u8 * p)
{
	*p = val;
	buf_put_int8(bufp, val);
}

static inline void v9fs_put_int16(struct cbuf *bufp, u16 val, u16 * p)
{
	*p = val;
	buf_put_int16(bufp, val);
}

static inline void v9fs_put_int32(struct cbuf *bufp, u32 val, u32 * p)
{
	*p = val;
	buf_put_int32(bufp, val);
}

static inline void v9fs_put_int64(struct cbuf *bufp, u64 val, u64 * p)
{
	*p = val;
	buf_put_int64(bufp, val);
}

static void
v9fs_put_str(struct cbuf *bufp, char *data, struct v9fs_str *str)
{
	int len;
	char *s;

	if (data)
		len = strlen(data);
	else
		len = 0;

	s = buf_put_stringn(bufp, data, len);
	if (str) {
		str->len = len;
		str->str = s;
	}
}

static int
v9fs_put_user_data(struct cbuf *bufp, const char __user * data, int count,
		   unsigned char **pdata)
{
	*pdata = buf_alloc(bufp, count);
	return copy_from_user(*pdata, data, count);
}

static void
v9fs_put_wstat(struct cbuf *bufp, struct v9fs_wstat *wstat,
	       struct v9fs_stat *stat, int statsz, int extended)
{
	v9fs_put_int16(bufp, statsz, &stat->size);
	v9fs_put_int16(bufp, wstat->type, &stat->type);
	v9fs_put_int32(bufp, wstat->dev, &stat->dev);
	v9fs_put_int8(bufp, wstat->qid.type, &stat->qid.type);
	v9fs_put_int32(bufp, wstat->qid.version, &stat->qid.version);
	v9fs_put_int64(bufp, wstat->qid.path, &stat->qid.path);
	v9fs_put_int32(bufp, wstat->mode, &stat->mode);
	v9fs_put_int32(bufp, wstat->atime, &stat->atime);
	v9fs_put_int32(bufp, wstat->mtime, &stat->mtime);
	v9fs_put_int64(bufp, wstat->length, &stat->length);

	v9fs_put_str(bufp, wstat->name, &stat->name);
	v9fs_put_str(bufp, wstat->uid, &stat->uid);
	v9fs_put_str(bufp, wstat->gid, &stat->gid);
	v9fs_put_str(bufp, wstat->muid, &stat->muid);

	if (extended) {
		v9fs_put_str(bufp, wstat->extension, &stat->extension);
		v9fs_put_int32(bufp, wstat->n_uid, &stat->n_uid);
		v9fs_put_int32(bufp, wstat->n_gid, &stat->n_gid);
		v9fs_put_int32(bufp, wstat->n_muid, &stat->n_muid);
	}
}

static struct v9fs_fcall *
v9fs_create_common(struct cbuf *bufp, u32 size, u8 id)
{
	struct v9fs_fcall *fc;

	size += 4 + 1 + 2;	/* size[4] id[1] tag[2] */
	fc = kmalloc(sizeof(struct v9fs_fcall) + size, GFP_KERNEL);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->sdata = (char *)fc + sizeof(*fc);

	buf_init(bufp, (char *)fc->sdata, size);
	v9fs_put_int32(bufp, size, &fc->size);
	v9fs_put_int8(bufp, id, &fc->id);
	v9fs_put_int16(bufp, V9FS_NOTAG, &fc->tag);

	return fc;
}

void v9fs_set_tag(struct v9fs_fcall *fc, u16 tag)
{
	fc->tag = tag;
	*(__le16 *) (fc->sdata + 5) = cpu_to_le16(tag);
}

struct v9fs_fcall *v9fs_create_tversion(u32 msize, char *version)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 2 + strlen(version);	/* msize[4] version[s] */
	fc = v9fs_create_common(bufp, size, TVERSION);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, msize, &fc->params.tversion.msize);
	v9fs_put_str(bufp, version, &fc->params.tversion.version);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

#if 0
struct v9fs_fcall *v9fs_create_tauth(u32 afid, char *uname, char *aname)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 2 + strlen(uname) + 2 + strlen(aname);	/* afid[4] uname[s] aname[s] */
	fc = v9fs_create_common(bufp, size, TAUTH);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, afid, &fc->params.tauth.afid);
	v9fs_put_str(bufp, uname, &fc->params.tauth.uname);
	v9fs_put_str(bufp, aname, &fc->params.tauth.aname);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}
#endif  /*  0  */

struct v9fs_fcall *
v9fs_create_tattach(u32 fid, u32 afid, char *uname, char *aname)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 4 + 2 + strlen(uname) + 2 + strlen(aname);	/* fid[4] afid[4] uname[s] aname[s] */
	fc = v9fs_create_common(bufp, size, TATTACH);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.tattach.fid);
	v9fs_put_int32(bufp, afid, &fc->params.tattach.afid);
	v9fs_put_str(bufp, uname, &fc->params.tattach.uname);
	v9fs_put_str(bufp, aname, &fc->params.tattach.aname);

      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_tflush(u16 oldtag)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 2;		/* oldtag[2] */
	fc = v9fs_create_common(bufp, size, TFLUSH);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int16(bufp, oldtag, &fc->params.tflush.oldtag);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_twalk(u32 fid, u32 newfid, u16 nwname,
				     char **wnames)
{
	int i, size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	if (nwname > V9FS_MAXWELEM) {
		dprintk(DEBUG_ERROR, "nwname > %d\n", V9FS_MAXWELEM);
		return NULL;
	}

	size = 4 + 4 + 2;	/* fid[4] newfid[4] nwname[2] ... */
	for (i = 0; i < nwname; i++) {
		size += 2 + strlen(wnames[i]);	/* wname[s] */
	}

	fc = v9fs_create_common(bufp, size, TWALK);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.twalk.fid);
	v9fs_put_int32(bufp, newfid, &fc->params.twalk.newfid);
	v9fs_put_int16(bufp, nwname, &fc->params.twalk.nwname);
	for (i = 0; i < nwname; i++) {
		v9fs_put_str(bufp, wnames[i], &fc->params.twalk.wnames[i]);
	}

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_topen(u32 fid, u8 mode)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 1;		/* fid[4] mode[1] */
	fc = v9fs_create_common(bufp, size, TOPEN);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.topen.fid);
	v9fs_put_int8(bufp, mode, &fc->params.topen.mode);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_tcreate(u32 fid, char *name, u32 perm, u8 mode,
	char *extension, int extended)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 2 + strlen(name) + 4 + 1;	/* fid[4] name[s] perm[4] mode[1] */
	if (extended && extension!=NULL)
		size += 2 + strlen(extension);	/* extension[s] */

	fc = v9fs_create_common(bufp, size, TCREATE);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.tcreate.fid);
	v9fs_put_str(bufp, name, &fc->params.tcreate.name);
	v9fs_put_int32(bufp, perm, &fc->params.tcreate.perm);
	v9fs_put_int8(bufp, mode, &fc->params.tcreate.mode);
	if (extended)
		v9fs_put_str(bufp, extension, &fc->params.tcreate.extension);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_tread(u32 fid, u64 offset, u32 count)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 8 + 4;	/* fid[4] offset[8] count[4] */
	fc = v9fs_create_common(bufp, size, TREAD);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.tread.fid);
	v9fs_put_int64(bufp, offset, &fc->params.tread.offset);
	v9fs_put_int32(bufp, count, &fc->params.tread.count);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_twrite(u32 fid, u64 offset, u32 count,
				      const char __user * data)
{
	int size, err;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4 + 8 + 4 + count;	/* fid[4] offset[8] count[4] data[count] */
	fc = v9fs_create_common(bufp, size, TWRITE);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.twrite.fid);
	v9fs_put_int64(bufp, offset, &fc->params.twrite.offset);
	v9fs_put_int32(bufp, count, &fc->params.twrite.count);
	err = v9fs_put_user_data(bufp, data, count, &fc->params.twrite.data);
	if (err) {
		kfree(fc);
		fc = ERR_PTR(err);
	}

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_tclunk(u32 fid)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4;		/* fid[4] */
	fc = v9fs_create_common(bufp, size, TCLUNK);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.tclunk.fid);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_tremove(u32 fid)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4;		/* fid[4] */
	fc = v9fs_create_common(bufp, size, TREMOVE);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.tremove.fid);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_tstat(u32 fid)
{
	int size;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	size = 4;		/* fid[4] */
	fc = v9fs_create_common(bufp, size, TSTAT);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.tstat.fid);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}

struct v9fs_fcall *v9fs_create_twstat(u32 fid, struct v9fs_wstat *wstat,
				      int extended)
{
	int size, statsz;
	struct v9fs_fcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	statsz = v9fs_size_wstat(wstat, extended);
	size = 4 + 2 + 2 + statsz;	/* fid[4] stat[n] */
	fc = v9fs_create_common(bufp, size, TWSTAT);
	if (IS_ERR(fc))
		goto error;

	v9fs_put_int32(bufp, fid, &fc->params.twstat.fid);
	buf_put_int16(bufp, statsz + 2);
	v9fs_put_wstat(bufp, wstat, &fc->params.twstat.stat, statsz, extended);

	if (buf_check_overflow(bufp)) {
		kfree(fc);
		fc = ERR_PTR(-ENOMEM);
	}
      error:
	return fc;
}
