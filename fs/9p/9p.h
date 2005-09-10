/*
 * linux/fs/9p/9p.h
 *
 * 9P protocol definitions.
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

/* Message Types */
enum {
	TVERSION = 100,
	RVERSION,
	TAUTH = 102,
	RAUTH,
	TATTACH = 104,
	RATTACH,
	TERROR = 106,
	RERROR,
	TFLUSH = 108,
	RFLUSH,
	TWALK = 110,
	RWALK,
	TOPEN = 112,
	ROPEN,
	TCREATE = 114,
	RCREATE,
	TREAD = 116,
	RREAD,
	TWRITE = 118,
	RWRITE,
	TCLUNK = 120,
	RCLUNK,
	TREMOVE = 122,
	RREMOVE,
	TSTAT = 124,
	RSTAT,
	TWSTAT = 126,
	RWSTAT,
};

/* modes */
enum {
	V9FS_OREAD = 0x00,
	V9FS_OWRITE = 0x01,
	V9FS_ORDWR = 0x02,
	V9FS_OEXEC = 0x03,
	V9FS_OEXCL = 0x04,
	V9FS_OTRUNC = 0x10,
	V9FS_OREXEC = 0x20,
	V9FS_ORCLOSE = 0x40,
	V9FS_OAPPEND = 0x80,
};

/* permissions */
enum {
	V9FS_DMDIR = 0x80000000,
	V9FS_DMAPPEND = 0x40000000,
	V9FS_DMEXCL = 0x20000000,
	V9FS_DMMOUNT = 0x10000000,
	V9FS_DMAUTH = 0x08000000,
	V9FS_DMTMP = 0x04000000,
	V9FS_DMSYMLINK = 0x02000000,
	V9FS_DMLINK = 0x01000000,
	/* 9P2000.u extensions */
	V9FS_DMDEVICE = 0x00800000,
	V9FS_DMNAMEDPIPE = 0x00200000,
	V9FS_DMSOCKET = 0x00100000,
	V9FS_DMSETUID = 0x00080000,
	V9FS_DMSETGID = 0x00040000,
};

/* qid.types */
enum {
	V9FS_QTDIR = 0x80,
	V9FS_QTAPPEND = 0x40,
	V9FS_QTEXCL = 0x20,
	V9FS_QTMOUNT = 0x10,
	V9FS_QTAUTH = 0x08,
	V9FS_QTTMP = 0x04,
	V9FS_QTSYMLINK = 0x02,
	V9FS_QTLINK = 0x01,
	V9FS_QTFILE = 0x00,
};

/* ample room for Twrite/Rread header (iounit) */
#define V9FS_IOHDRSZ	24

/* qids are the unique ID for a file (like an inode */
struct v9fs_qid {
	u8 type;
	u32 version;
	u64 path;
};

/* Plan 9 file metadata (stat) structure */
struct v9fs_stat {
	u16 size;
	u16 type;
	u32 dev;
	struct v9fs_qid qid;
	u32 mode;
	u32 atime;
	u32 mtime;
	u64 length;
	char *name;
	char *uid;
	char *gid;
	char *muid;
	char *extension;	/* 9p2000.u extensions */
	u32 n_uid;		/* 9p2000.u extensions */
	u32 n_gid;		/* 9p2000.u extensions */
	u32 n_muid;		/* 9p2000.u extensions */
	char data[0];
};

/* Structures for Protocol Operations */

struct Tversion {
	u32 msize;
	char *version;
};

struct Rversion {
	u32 msize;
	char *version;
};

struct Tauth {
	u32 afid;
	char *uname;
	char *aname;
};

struct Rauth {
	struct v9fs_qid qid;
};

struct Rerror {
	char *error;
	u32 errno;		/* 9p2000.u extension */
};

struct Tflush {
	u32 oldtag;
};

struct Rflush {
};

struct Tattach {
	u32 fid;
	u32 afid;
	char *uname;
	char *aname;
};

struct Rattach {
	struct v9fs_qid qid;
};

struct Twalk {
	u32 fid;
	u32 newfid;
	u32 nwname;
	char **wnames;
};

struct Rwalk {
	u32 nwqid;
	struct v9fs_qid *wqids;
};

struct Topen {
	u32 fid;
	u8 mode;
};

struct Ropen {
	struct v9fs_qid qid;
	u32 iounit;
};

struct Tcreate {
	u32 fid;
	char *name;
	u32 perm;
	u8 mode;
};

struct Rcreate {
	struct v9fs_qid qid;
	u32 iounit;
};

struct Tread {
	u32 fid;
	u64 offset;
	u32 count;
};

struct Rread {
	u32 count;
	u8 *data;
};

struct Twrite {
	u32 fid;
	u64 offset;
	u32 count;
	u8 *data;
};

struct Rwrite {
	u32 count;
};

struct Tclunk {
	u32 fid;
};

struct Rclunk {
};

struct Tremove {
	u32 fid;
};

struct Rremove {
};

struct Tstat {
	u32 fid;
};

struct Rstat {
	struct v9fs_stat *stat;
};

struct Twstat {
	u32 fid;
	struct v9fs_stat *stat;
};

struct Rwstat {
};

/*
  * fcall is the primary packet structure
  *
  */

struct v9fs_fcall {
	u32 size;
	u8 id;
	u16 tag;

	union {
		struct Tversion tversion;
		struct Rversion rversion;
		struct Tauth tauth;
		struct Rauth rauth;
		struct Rerror rerror;
		struct Tflush tflush;
		struct Rflush rflush;
		struct Tattach tattach;
		struct Rattach rattach;
		struct Twalk twalk;
		struct Rwalk rwalk;
		struct Topen topen;
		struct Ropen ropen;
		struct Tcreate tcreate;
		struct Rcreate rcreate;
		struct Tread tread;
		struct Rread rread;
		struct Twrite twrite;
		struct Rwrite rwrite;
		struct Tclunk tclunk;
		struct Rclunk rclunk;
		struct Tremove tremove;
		struct Rremove rremove;
		struct Tstat tstat;
		struct Rstat rstat;
		struct Twstat twstat;
		struct Rwstat rwstat;
	} params;
};

#define FCALL_ERROR(fcall) (fcall ? fcall->params.rerror.error : "")

int v9fs_t_version(struct v9fs_session_info *v9ses, u32 msize,
		   char *version, struct v9fs_fcall **rcall);

int v9fs_t_attach(struct v9fs_session_info *v9ses, char *uname, char *aname,
		  u32 fid, u32 afid, struct v9fs_fcall **rcall);

int v9fs_t_clunk(struct v9fs_session_info *v9ses, u32 fid,
		 struct v9fs_fcall **rcall);

int v9fs_t_flush(struct v9fs_session_info *v9ses, u16 oldtag);

int v9fs_t_stat(struct v9fs_session_info *v9ses, u32 fid,
		struct v9fs_fcall **rcall);

int v9fs_t_wstat(struct v9fs_session_info *v9ses, u32 fid,
		 struct v9fs_stat *stat, struct v9fs_fcall **rcall);

int v9fs_t_walk(struct v9fs_session_info *v9ses, u32 fid, u32 newfid,
		char *name, struct v9fs_fcall **rcall);

int v9fs_t_open(struct v9fs_session_info *v9ses, u32 fid, u8 mode,
		struct v9fs_fcall **rcall);

int v9fs_t_remove(struct v9fs_session_info *v9ses, u32 fid,
		  struct v9fs_fcall **rcall);

int v9fs_t_create(struct v9fs_session_info *v9ses, u32 fid, char *name,
		  u32 perm, u8 mode, struct v9fs_fcall **rcall);

int v9fs_t_read(struct v9fs_session_info *v9ses, u32 fid,
		u64 offset, u32 count, struct v9fs_fcall **rcall);

int v9fs_t_write(struct v9fs_session_info *v9ses, u32 fid, u64 offset,
		 u32 count, void *data, struct v9fs_fcall **rcall);
