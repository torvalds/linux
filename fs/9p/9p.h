/*
 * linux/fs/9p/9p.h
 *
 * 9P protocol definitions.
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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

#define V9FS_NOTAG	(u16)(~0)
#define V9FS_NOFID	(u32)(~0)
#define V9FS_MAXWELEM	16

/* ample room for Twrite/Rread header (iounit) */
#define V9FS_IOHDRSZ	24

struct v9fs_str {
	u16 len;
	char *str;
};

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
	struct v9fs_str name;
	struct v9fs_str uid;
	struct v9fs_str gid;
	struct v9fs_str muid;
	struct v9fs_str extension;	/* 9p2000.u extensions */
	u32 n_uid;		/* 9p2000.u extensions */
	u32 n_gid;		/* 9p2000.u extensions */
	u32 n_muid;		/* 9p2000.u extensions */
};

/* file metadata (stat) structure used to create Twstat message
   The is similar to v9fs_stat, but the strings don't point to
   the same memory block and should be freed separately
*/
struct v9fs_wstat {
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
};

/* Structures for Protocol Operations */

struct Tversion {
	u32 msize;
	struct v9fs_str version;
};

struct Rversion {
	u32 msize;
	struct v9fs_str version;
};

struct Tauth {
	u32 afid;
	struct v9fs_str uname;
	struct v9fs_str aname;
};

struct Rauth {
	struct v9fs_qid qid;
};

struct Rerror {
	struct v9fs_str error;
	u32 errno;		/* 9p2000.u extension */
};

struct Tflush {
	u16 oldtag;
};

struct Rflush {
};

struct Tattach {
	u32 fid;
	u32 afid;
	struct v9fs_str uname;
	struct v9fs_str aname;
};

struct Rattach {
	struct v9fs_qid qid;
};

struct Twalk {
	u32 fid;
	u32 newfid;
	u16 nwname;
	struct v9fs_str wnames[16];
};

struct Rwalk {
	u16 nwqid;
	struct v9fs_qid wqids[16];
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
	struct v9fs_str name;
	u32 perm;
	u8 mode;
	struct v9fs_str extension;
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
	struct v9fs_stat stat;
};

struct Twstat {
	u32 fid;
	struct v9fs_stat stat;
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
	void *sdata;

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

#define PRINT_FCALL_ERROR(s, fcall) dprintk(DEBUG_ERROR, "%s: %.*s\n", s, \
	fcall?fcall->params.rerror.error.len:0, \
	fcall?fcall->params.rerror.error.str:"");

int v9fs_t_version(struct v9fs_session_info *v9ses, u32 msize,
		   char *version, struct v9fs_fcall **rcall);

int v9fs_t_attach(struct v9fs_session_info *v9ses, char *uname, char *aname,
		  u32 fid, u32 afid, struct v9fs_fcall **rcall);

int v9fs_t_clunk(struct v9fs_session_info *v9ses, u32 fid);

int v9fs_t_stat(struct v9fs_session_info *v9ses, u32 fid,
		struct v9fs_fcall **rcall);

int v9fs_t_wstat(struct v9fs_session_info *v9ses, u32 fid,
		 struct v9fs_wstat *wstat, struct v9fs_fcall **rcall);

int v9fs_t_walk(struct v9fs_session_info *v9ses, u32 fid, u32 newfid,
		char *name, struct v9fs_fcall **rcall);

int v9fs_t_open(struct v9fs_session_info *v9ses, u32 fid, u8 mode,
		struct v9fs_fcall **rcall);

int v9fs_t_remove(struct v9fs_session_info *v9ses, u32 fid,
		  struct v9fs_fcall **rcall);

int v9fs_t_create(struct v9fs_session_info *v9ses, u32 fid, char *name,
	u32 perm, u8 mode, char *extension, struct v9fs_fcall **rcall);

int v9fs_t_read(struct v9fs_session_info *v9ses, u32 fid,
		u64 offset, u32 count, struct v9fs_fcall **rcall);

int v9fs_t_write(struct v9fs_session_info *v9ses, u32 fid, u64 offset,
		 u32 count, const char __user * data,
		 struct v9fs_fcall **rcall);
int v9fs_printfcall(char *, int, struct v9fs_fcall *, int);
