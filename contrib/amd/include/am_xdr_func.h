/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/include/am_xdr_func.h
 *
 */

#ifdef HAVE_FS_NFS3

#define AM_MOUNTVERS3 ((unsigned long)(3))

#define AM_FHSIZE3 64		/* size in bytes of a file handle (v3) */
#define AM_NFS3_WRITEVERFSIZE 8
#define AM_NFS3_CREATEVERFSIZE 8
#define AM_NFS3_COOKIEVERFSIZE 8
#define AM_ACCESS3_READ 0x0001
#define AM_ACCESS3_LOOKUP 0x0002
#define AM_ACCESS3_MODIFY 0x0004
#define AM_ACCESS3_EXTEND 0x0008
#define AM_ACCESS3_DELETE 0x0010
#define AM_ACCESS3_EXECUTE 0x0020
#define AM_FSF3_LINK 0x0001
#define AM_FSF3_SYMLINK 0x0002
#define AM_FSF3_HOMOGENEOUS 0x0008
#define AM_FSF3_CANSETTIME 0x0010

typedef char am_cookieverf3[AM_NFS3_COOKIEVERFSIZE];

typedef uint64 am_cookie3;

/* NFSv3 handle */
struct am_nfs_fh3 {
  u_int am_fh3_length;
  char am_fh3_data[AM_FHSIZE3];
};
typedef struct am_nfs_fh3 am_nfs_fh3;

#define AM_NFSPROC3_LOOKUP ((u_long) 3)
enum am_nfsstat3 {
	AM_NFS3_OK = 0,
	AM_NFS3ERR_PERM = 1,
	AM_NFS3ERR_NOENT = 2,
	AM_NFS3ERR_IO = 5,
	AM_NFS3ERR_NXIO = 6,
	AM_NFS3ERR_ACCES = 13,
	AM_NFS3ERR_EXIST = 17,
	AM_NFS3ERR_XDEV = 18,
	AM_NFS3ERR_NODEV = 19,
	AM_NFS3ERR_NOTDIR = 20,
	AM_NFS3ERR_ISDIR = 21,
	AM_NFS3ERR_INVAL = 22,
	AM_NFS3ERR_FBIG = 27,
	AM_NFS3ERR_NOSPC = 28,
	AM_NFS3ERR_ROFS = 30,
	AM_NFS3ERR_MLINK = 31,
	AM_NFS3ERR_NAMETOOLONG = 63,
	AM_NFS3ERR_NOTEMPTY = 66,
	AM_NFS3ERR_DQUOT = 69,
	AM_NFS3ERR_STALE = 70,
	AM_NFS3ERR_REMOTE = 71,
	AM_NFS3ERR_BADHANDLE = 10001,
	AM_NFS3ERR_NOT_SYNC = 10002,
	AM_NFS3ERR_BAD_COOKIE = 10003,
	AM_NFS3ERR_NOTSUPP = 10004,
	AM_NFS3ERR_TOOSMALL = 10005,
	AM_NFS3ERR_SERVERFAULT = 10006,
	AM_NFS3ERR_BADTYPE = 10007,
	AM_NFS3ERR_JUKEBOX = 10008
};
typedef enum am_nfsstat3 am_nfsstat3;

typedef struct {
  u_int fhandle3_len;
  char *fhandle3_val;
} am_fhandle3;

enum am_mountstat3 {
       AM_MNT3_OK = 0,
       AM_MNT3ERR_PERM = 1,
       AM_MNT3ERR_NOENT = 2,
       AM_MNT3ERR_IO = 5,
       AM_MNT3ERR_ACCES = 13,
       AM_MNT3ERR_NOTDIR = 20,
       AM_MNT3ERR_INVAL = 22,
       AM_MNT3ERR_NAMETOOLONG = 63,
       AM_MNT3ERR_NOTSUPP = 10004,
       AM_MNT3ERR_SERVERFAULT = 10006
};
typedef enum am_mountstat3 am_mountstat3;

struct am_mountres3_ok {
       am_fhandle3 fhandle;
       struct {
               u_int auth_flavors_len;
               int *auth_flavors_val;
       } auth_flavors;
};
typedef struct am_mountres3_ok am_mountres3_ok;

struct am_mountres3 {
       am_mountstat3 fhs_status;
       union {
               am_mountres3_ok mountinfo;
       } mountres3_u;
};
typedef struct am_mountres3 am_mountres3;

typedef char *am_filename3;

struct am_diropargs3 {
	am_nfs_fh3 dir;
	am_filename3 name;
};
typedef struct am_diropargs3 am_diropargs3;

enum am_ftype3 {
  AM_NF3REG = 1,
  AM_NF3DIR = 2,
  AM_NF3BLK = 3,
  AM_NF3CHR = 4,
  AM_NF3LNK = 5,
  AM_NF3SOCK = 6,
  AM_NF3FIFO = 7,
};
typedef enum am_ftype3 am_ftype3;

typedef u_int am_mode3;

typedef u_int am_uid3;

typedef u_int am_gid3;

typedef uint64 am_size3;

typedef uint64 am_fileid3;

struct am_specdata3 {
  u_int specdata1;
  u_int specdata2;
};
typedef struct am_specdata3 am_specdata3;

struct am_nfstime3 {
  u_int seconds;
  u_int nseconds;
};
typedef struct am_nfstime3 am_nfstime3;

struct am_fattr3 {
  am_ftype3 type;
  am_mode3 mode;
  u_int nlink;
  am_uid3 uid;
  am_gid3 gid;
  am_size3 size;
  am_size3 used;
  am_specdata3 rdev;
  uint64 fsid;
  am_fileid3 fileid;
  am_nfstime3 atime;
  am_nfstime3 mtime;
  am_nfstime3 ctime;
};
typedef struct am_fattr3 am_fattr3;

struct am_post_op_attr {
  bool_t attributes_follow;
  union {
    am_fattr3 attributes;
  } am_post_op_attr_u;
};
typedef struct am_post_op_attr am_post_op_attr;

enum am_stable_how {
  AM_UNSTABLE = 0,
  AM_DATA_SYNC = 1,
  AM_FILE_SYNC = 2,
};
typedef enum am_stable_how am_stable_how;

typedef uint64 am_offset3;

typedef u_int am_count3;

struct am_wcc_attr {
  am_size3 size;
  am_nfstime3 mtime;
  am_nfstime3 ctime;
};
typedef struct am_wcc_attr am_wcc_attr;

struct am_pre_op_attr {
  bool_t attributes_follow;
  union {
    am_wcc_attr attributes;
  } am_pre_op_attr_u;
};
typedef struct am_pre_op_attr am_pre_op_attr;

struct am_wcc_data {
  am_pre_op_attr before;
  am_post_op_attr after;
};
typedef struct am_wcc_data am_wcc_data;

struct am_WRITE3args {
  am_nfs_fh3 file;
  am_offset3 offset;
  am_count3 count;
  am_stable_how stable;
  struct {
    u_int data_len;
    char *data_val;
  } data;
};
typedef struct am_WRITE3args am_WRITE3args;

typedef char am_writeverf3[AM_NFS3_WRITEVERFSIZE];

struct am_WRITE3resok {
  am_wcc_data file_wcc;
  am_count3 count;
  am_stable_how committed;
  am_writeverf3 verf;
};
typedef struct am_WRITE3resok am_WRITE3resok;

struct am_WRITE3resfail {
  am_wcc_data file_wcc;
};
typedef struct am_WRITE3resfail am_WRITE3resfail;

struct am_WRITE3res {
  am_nfsstat3 status;
  union {
    am_WRITE3resok ok;
    am_WRITE3resfail fail;
  } res_u;
};
typedef struct am_WRITE3res am_WRITE3res;

struct am_LOOKUP3args {
  am_diropargs3 what;
};
typedef struct am_LOOKUP3args am_LOOKUP3args;

struct am_LOOKUP3resok {
  am_nfs_fh3 object;
  am_post_op_attr obj_attributes;
  am_post_op_attr dir_attributes;
};
typedef struct am_LOOKUP3resok am_LOOKUP3resok;

struct am_LOOKUP3resfail {
  am_post_op_attr dir_attributes;
};
typedef struct am_LOOKUP3resfail am_LOOKUP3resfail;

struct am_LOOKUP3res {
  am_nfsstat3 status;
  union {
    am_LOOKUP3resok ok;
    am_LOOKUP3resfail fail;
  } res_u;
};
typedef struct am_LOOKUP3res am_LOOKUP3res;

struct am_COMMIT3args {
  am_nfs_fh3 file;
  am_offset3 offset;
  am_count3 count;
};
typedef struct am_COMMIT3args am_COMMIT3args;

struct am_COMMIT3resok {
  am_wcc_data file_wcc;
  am_writeverf3 verf;
};
typedef struct am_COMMIT3resok am_COMMIT3resok;

struct am_COMMIT3resfail {
  am_wcc_data file_wcc;
};
typedef struct am_COMMIT3resfail am_COMMIT3resfail;

struct am_COMMIT3res {
  am_nfsstat3 status;
  union {
    am_COMMIT3resok ok;
    am_COMMIT3resfail fail;
  } res_u;
};
typedef struct am_COMMIT3res am_COMMIT3res;

struct am_ACCESS3args {
  am_nfs_fh3 object;
  u_int access;
};
typedef struct am_ACCESS3args am_ACCESS3args;

struct am_ACCESS3resok {
  am_post_op_attr obj_attributes;
  u_int access;
};
typedef struct am_ACCESS3resok am_ACCESS3resok;

struct am_ACCESS3resfail {
  am_post_op_attr obj_attributes;
};
typedef struct am_ACCESS3resfail am_ACCESS3resfail;

struct am_ACCESS3res {
  am_nfsstat3 status;
  union {
    am_ACCESS3resok ok;
    am_ACCESS3resfail fail;
  } res_u;
};
typedef struct am_ACCESS3res am_ACCESS3res;

struct am_GETATTR3args {
  am_nfs_fh3 object;
};
typedef struct am_GETATTR3args am_GETATTR3args;

struct am_GETATTR3resok {
  am_fattr3 obj_attributes;
};
typedef struct am_GETATTR3resok am_GETATTR3resok;

struct am_GETATTR3res {
  am_nfsstat3 status;
  union {
    am_GETATTR3resok ok;
  } res_u;
};
typedef struct am_GETATTR3res am_GETATTR3res;

enum am_time_how {
  AM_DONT_CHANGE = 0,
  AM_SET_TO_SERVER_TIME = 1,
  AM_SET_TO_CLIENT_TIME = 2,
};
typedef enum am_time_how am_time_how;

struct am_set_mode3 {
  bool_t set_it;
  union {
    am_mode3 mode;
  } am_set_mode3_u;
};
typedef struct am_set_mode3 am_set_mode3;

struct am_set_uid3 {
  bool_t set_it;
  union {
    am_uid3 uid;
  } am_set_uid3_u;
};
typedef struct am_set_uid3 am_set_uid3;

struct am_set_gid3 {
  bool_t set_it;
  union {
    am_gid3 gid;
  } am_set_gid3_u;
};
typedef struct am_set_gid3 am_set_gid3;

struct am_set_size3 {
  bool_t set_it;
  union {
    am_size3 size;
  } am_set_size3_u;
};
typedef struct am_set_size3 am_set_size3;

struct am_set_atime {
  am_time_how set_it;
  union {
    am_nfstime3 atime;
  } am_set_atime_u;
};
typedef struct am_set_atime am_set_atime;

struct am_set_mtime {
  am_time_how set_it;
  union {
    am_nfstime3 mtime;
  } am_set_mtime_u;
};
typedef struct am_set_mtime am_set_mtime;

struct am_sattr3 {
  am_set_mode3 mode;
  am_set_uid3 uid;
  am_set_gid3 gid;
  am_set_size3 size;
  am_set_atime atime;
  am_set_mtime mtime;
};
typedef struct am_sattr3 am_sattr3;

enum am_createmode3 {
  AM_UNCHECKED = 0,
  AM_GUARDED = 1,
  AM_EXCLUSIVE = 2,
};
typedef enum am_createmode3 am_createmode3;

typedef char am_createverf3[AM_NFS3_CREATEVERFSIZE];

struct am_createhow3 {
  am_createmode3 mode;
  union {
    am_sattr3 obj_attributes;
    am_sattr3 g_obj_attributes;
    am_createverf3 verf;
  } am_createhow3_u;
};
typedef struct am_createhow3 am_createhow3;

struct am_CREATE3args {
  am_diropargs3 where;
  am_createhow3 how;
};
typedef struct am_CREATE3args am_CREATE3args;

struct am_post_op_fh3 {
  bool_t handle_follows;
  union {
    am_nfs_fh3 handle;
  } am_post_op_fh3_u;
};
typedef struct am_post_op_fh3 am_post_op_fh3;

struct am_CREATE3resok {
  am_post_op_fh3 obj;
  am_post_op_attr obj_attributes;
  am_wcc_data dir_wcc;
};
typedef struct am_CREATE3resok am_CREATE3resok;

struct am_CREATE3resfail {
  am_wcc_data dir_wcc;
};
typedef struct am_CREATE3resfail am_CREATE3resfail;

struct am_CREATE3res {
  am_nfsstat3 status;
  union {
    am_CREATE3resok ok;
    am_CREATE3resfail fail;
  } res_u;
};
typedef struct am_CREATE3res am_CREATE3res;

struct am_REMOVE3args {
  am_diropargs3 object;
};
typedef struct am_REMOVE3args am_REMOVE3args;

struct am_REMOVE3resok {
  am_wcc_data dir_wcc;
};
typedef struct am_REMOVE3resok am_REMOVE3resok;

struct am_REMOVE3resfail {
  am_wcc_data dir_wcc;
};
typedef struct am_REMOVE3resfail am_REMOVE3resfail;

struct am_REMOVE3res {
  am_nfsstat3 status;
  union {
    am_REMOVE3resok ok;
    am_REMOVE3resfail fail;
  } res_u;
};
typedef struct am_REMOVE3res am_REMOVE3res;

struct am_READ3args {
  am_nfs_fh3 file;
  am_offset3 offset;
  am_count3 count;
};
typedef struct am_READ3args am_READ3args;

struct am_READ3resok {
  am_post_op_attr file_attributes;
  am_count3 count;
  bool_t eof;
  struct {
    u_int data_len;
    char *data_val;
  } data;
};
typedef struct am_READ3resok am_READ3resok;

struct am_READ3resfail {
  am_post_op_attr file_attributes;
};
typedef struct am_READ3resfail am_READ3resfail;

struct am_READ3res {
  am_nfsstat3 status;
  union {
    am_READ3resok ok;
    am_READ3resfail fail;
  } res_u;
};
typedef struct am_READ3res am_READ3res;

struct am_FSINFO3args {
  am_nfs_fh3 fsroot;
};
typedef struct am_FSINFO3args am_FSINFO3args;

struct am_FSINFO3resok {
  am_post_op_attr obj_attributes;
  u_int rtmax;
  u_int rtpref;
  u_int rtmult;
  u_int wtmax;
  u_int wtpref;
  u_int wtmult;
  u_int dtpref;
  am_size3 maxfilesize;
  am_nfstime3 time_delta;
  u_int properties;
};
typedef struct am_FSINFO3resok am_FSINFO3resok;

struct am_FSINFO3resfail {
  am_post_op_attr obj_attributes;
};
typedef struct am_FSINFO3resfail am_FSINFO3resfail;

struct am_FSINFO3res {
  am_nfsstat3 status;
  union {
    am_FSINFO3resok ok;
    am_FSINFO3resfail fail;
  } res_u;
};
typedef struct am_FSINFO3res am_FSINFO3res;

struct am_FSSTAT3args {
  am_nfs_fh3 fsroot;
};
typedef struct am_FSSTAT3args am_FSSTAT3args;

struct am_FSSTAT3resok {
  am_post_op_attr obj_attributes;
  am_size3 tbytes;
  am_size3 fbytes;
  am_size3 abytes;
  am_size3 tfiles;
  am_size3 ffiles;
  am_size3 afiles;
  u_int invarsec;
};
typedef struct am_FSSTAT3resok am_FSSTAT3resok;

struct am_FSSTAT3resfail {
  am_post_op_attr obj_attributes;
};
typedef struct am_FSSTAT3resfail am_FSSTAT3resfail;

struct am_FSSTAT3res {
  am_nfsstat3 status;
  union {
    am_FSSTAT3resok ok;
    am_FSSTAT3resfail fail;
  } res_u;
};
typedef struct am_FSSTAT3res am_FSSTAT3res;

struct am_PATHCONF3args {
  am_nfs_fh3 object;
};
typedef struct am_PATHCONF3args am_PATHCONF3args;

struct am_PATHCONF3resok {
  am_post_op_attr obj_attributes;
  u_int linkmax;
  u_int name_max;
  bool_t no_trunc;
  bool_t chown_restricted;
  bool_t case_insensitive;
  bool_t case_preserving;
};
typedef struct am_PATHCONF3resok am_PATHCONF3resok;

struct am_PATHCONF3resfail {
  am_post_op_attr obj_attributes;
};
typedef struct am_PATHCONF3resfail am_PATHCONF3resfail;

struct am_PATHCONF3res {
  am_nfsstat3 status;
  union {
    am_PATHCONF3resok ok;
    am_PATHCONF3resfail fail;
  } res_u;
};
typedef struct am_PATHCONF3res am_PATHCONF3res;

typedef char *am_nfspath3;

struct am_symlinkdata3 {
  am_sattr3 symlink_attributes;
  am_nfspath3 symlink_data;
};
typedef struct am_symlinkdata3 am_symlinkdata3;

struct am_SYMLINK3args {
  am_diropargs3 where;
  am_symlinkdata3 symlink;
};
typedef struct am_SYMLINK3args am_SYMLINK3args;

struct am_SYMLINK3resok {
  am_post_op_fh3 obj;
  am_post_op_attr obj_attributes;
  am_wcc_data dir_wcc;
};
typedef struct am_SYMLINK3resok am_SYMLINK3resok;

struct am_SYMLINK3resfail {
  am_wcc_data dir_wcc;
};
typedef struct am_SYMLINK3resfail am_SYMLINK3resfail;

struct am_SYMLINK3res {
  am_nfsstat3 status;
  union {
    am_SYMLINK3resok ok;
    am_SYMLINK3resfail fail;
  } res_u;
};
typedef struct am_SYMLINK3res am_SYMLINK3res;

struct am_READLINK3args {
  am_nfs_fh3 symlink;
};
typedef struct am_READLINK3args am_READLINK3args;

struct am_READLINK3resok {
  am_post_op_attr symlink_attributes;
  am_nfspath3 data;
};
typedef struct am_READLINK3resok am_READLINK3resok;

struct am_READLINK3resfail {
  am_post_op_attr symlink_attributes;
};
typedef struct am_READLINK3resfail am_READLINK3resfail;

struct am_READLINK3res {
  am_nfsstat3 status;
  union {
    am_READLINK3resok ok;
    am_READLINK3resfail fail;
  } res_u;
};
typedef struct am_READLINK3res am_READLINK3res;

struct am_devicedata3 {
  am_sattr3 dev_attributes;
  am_specdata3 spec;
};
typedef struct am_devicedata3 am_devicedata3;

struct am_mknoddata3 {
  am_ftype3 type;
  union {
    am_devicedata3 chr_device;
    am_devicedata3 blk_device;
    am_sattr3 sock_attributes;
    am_sattr3 pipe_attributes;
  } am_mknoddata3_u;
};
typedef struct am_mknoddata3 am_mknoddata3;

struct am_MKNOD3args {
  am_diropargs3 where;
  am_mknoddata3 what;
};
typedef struct am_MKNOD3args am_MKNOD3args;

struct am_MKNOD3resok {
  am_post_op_fh3 obj;
  am_post_op_attr obj_attributes;
  am_wcc_data dir_wcc;
};
typedef struct am_MKNOD3resok am_MKNOD3resok;

struct am_MKNOD3resfail {
  am_wcc_data dir_wcc;
};
typedef struct am_MKNOD3resfail am_MKNOD3resfail;

struct am_MKNOD3res {
  am_nfsstat3 status;
  union {
    am_MKNOD3resok ok;
    am_MKNOD3resfail fail;
  } res_u;
};
typedef struct am_MKNOD3res am_MKNOD3res;

struct am_MKDIR3args {
  am_diropargs3 where;
  am_sattr3 attributes;
};
typedef struct am_MKDIR3args am_MKDIR3args;

struct am_MKDIR3resok {
  am_post_op_fh3 obj;
  am_post_op_attr obj_attributes;
  am_wcc_data dir_wcc;
};
typedef struct am_MKDIR3resok am_MKDIR3resok;

struct am_MKDIR3resfail {
  am_wcc_data dir_wcc;
};
typedef struct am_MKDIR3resfail am_MKDIR3resfail;

struct am_MKDIR3res {
  am_nfsstat3 status;
  union {
    am_MKDIR3resok ok;
    am_MKDIR3resfail fail;
  } res_u;
};
typedef struct am_MKDIR3res am_MKDIR3res;

struct am_RMDIR3args {
  am_diropargs3 object;
};
typedef struct am_RMDIR3args am_RMDIR3args;

struct am_RMDIR3resok {
  am_wcc_data dir_wcc;
};
typedef struct am_RMDIR3resok am_RMDIR3resok;

struct am_RMDIR3resfail {
  am_wcc_data dir_wcc;
};
typedef struct am_RMDIR3resfail am_RMDIR3resfail;

struct am_RMDIR3res {
  am_nfsstat3 status;
  union {
    am_RMDIR3resok ok;
    am_RMDIR3resfail fail;
  } res_u;
};
typedef struct am_RMDIR3res am_RMDIR3res;

struct am_RENAME3args {
  am_diropargs3 from;
  am_diropargs3 to;
};
typedef struct am_RENAME3args am_RENAME3args;

struct am_RENAME3resok {
  am_wcc_data fromdir_wcc;
  am_wcc_data todir_wcc;
};
typedef struct am_RENAME3resok am_RENAME3resok;

struct am_RENAME3resfail {
  am_wcc_data fromdir_wcc;
  am_wcc_data todir_wcc;
};
typedef struct am_RENAME3resfail am_RENAME3resfail;

struct am_RENAME3res {
  am_nfsstat3 status;
  union {
    am_RENAME3resok ok;
    am_RENAME3resfail fail;
  } res_u;
};
typedef struct am_RENAME3res am_RENAME3res;

struct am_READDIRPLUS3args {
  am_nfs_fh3 dir;
  am_cookie3 cookie;
  am_cookieverf3 cookieverf;
  am_count3 dircount;
  am_count3 maxcount;
};
typedef struct am_READDIRPLUS3args am_READDIRPLUS3args;

struct am_entryplus3 {
  am_fileid3 fileid;
  am_filename3 name;
  am_cookie3 cookie;
  am_post_op_attr name_attributes;
  am_post_op_fh3 name_handle;
  struct am_entryplus3 *nextentry;
};
typedef struct am_entryplus3 am_entryplus3;

struct am_dirlistplus3 {
  am_entryplus3 *entries;
  bool_t eof;
};
typedef struct am_dirlistplus3 am_dirlistplus3;

struct am_READDIRPLUS3resok {
  am_post_op_attr dir_attributes;
  am_cookieverf3 cookieverf;
  am_dirlistplus3 reply;
};
typedef struct am_READDIRPLUS3resok am_READDIRPLUS3resok;

struct am_READDIRPLUS3resfail {
  am_post_op_attr dir_attributes;
};
typedef struct am_READDIRPLUS3resfail am_READDIRPLUS3resfail;

struct am_READDIRPLUS3res {
  am_nfsstat3 status;
  union {
    am_READDIRPLUS3resok ok;
    am_READDIRPLUS3resfail fail;
  } res_u;
};
typedef struct am_READDIRPLUS3res am_READDIRPLUS3res;

struct am_READDIR3args {
  am_nfs_fh3 dir;
  am_cookie3 cookie;
  am_cookieverf3 cookieverf;
  am_count3 count;
};
typedef struct am_READDIR3args am_READDIR3args;

struct am_entry3 {
  am_fileid3 fileid;
  am_filename3 name;
  am_cookie3 cookie;
  struct am_entry3 *nextentry;
};
typedef struct am_entry3 am_entry3;

struct am_dirlist3 {
  am_entry3 *entries;
  bool_t eof;
};
typedef struct am_dirlist3 am_dirlist3;

struct am_READDIR3resok {
  am_post_op_attr dir_attributes;
  am_cookieverf3 cookieverf;
  am_dirlist3 reply;
};
typedef struct am_READDIR3resok am_READDIR3resok;

struct am_READDIR3resfail {
  am_post_op_attr dir_attributes;
};
typedef struct am_READDIR3resfail am_READDIR3resfail;

struct am_READDIR3res {
  am_nfsstat3 status;
  union {
    am_READDIR3resok ok;
    am_READDIR3resfail fail;
  } res_u;
};
typedef struct am_READDIR3res am_READDIR3res;

struct am_LINK3args {
  am_nfs_fh3 file;
  am_diropargs3 link;
};
typedef struct am_LINK3args am_LINK3args;

struct am_LINK3resok {
  am_post_op_attr file_attributes;
  am_wcc_data linkdir_wcc;
};
typedef struct am_LINK3resok am_LINK3resok;

struct am_LINK3resfail {
  am_post_op_attr file_attributes;
  am_wcc_data linkdir_wcc;
};
typedef struct am_LINK3resfail am_LINK3resfail;

struct am_LINK3res {
  am_nfsstat3 status;
  union {
    am_LINK3resok ok;
    am_LINK3resfail fail;
  } res_u;
};
typedef struct am_LINK3res am_LINK3res;

struct am_sattrguard3 {
  bool_t check;
  union {
    am_nfstime3 obj_ctime;
  } am_sattrguard3_u;
};
typedef struct am_sattrguard3 am_sattrguard3;

struct am_SETATTR3args {
  am_nfs_fh3 object;
  am_sattr3 new_attributes;
  am_sattrguard3 guard;
};
typedef struct am_SETATTR3args am_SETATTR3args;

struct am_SETATTR3resok {
  am_wcc_data obj_wcc;
};
typedef struct am_SETATTR3resok am_SETATTR3resok;

struct am_SETATTR3resfail {
  am_wcc_data obj_wcc;
};
typedef struct am_SETATTR3resfail am_SETATTR3resfail;

struct am_SETATTR3res {
  am_nfsstat3 status;
  union {
    am_SETATTR3resok ok;
    am_SETATTR3resfail fail;
  } res_u;
};
typedef struct am_SETATTR3res am_SETATTR3res;
#endif /* HAVE_FS_NFS3 */

/*
 * Multi-protocol NFS file handle
 */
union am_nfs_handle {
				/* placeholder for V4 file handle */
#ifdef HAVE_FS_NFS3
  am_nfs_fh3		v3;	/* NFS version 3 handle */
#endif /* HAVE_FS_NFS3 */
  am_nfs_fh		v2;	/* NFS version 2 handle */
};
typedef union am_nfs_handle am_nfs_handle_t;


/*
 * Definitions of all possible xdr functions that are otherwise
 * not defined elsewhere.
 */

#ifndef _AM_XDR_FUNC_H
#define _AM_XDR_FUNC_H

#ifndef HAVE_XDR_ATTRSTAT
bool_t xdr_attrstat(XDR *xdrs, nfsattrstat *objp);
#endif /* not HAVE_XDR_ATTRSTAT */
#ifndef HAVE_XDR_CREATEARGS
bool_t xdr_createargs(XDR *xdrs, nfscreateargs *objp);
#endif /* not HAVE_XDR_CREATEARGS */
#ifndef HAVE_XDR_DIRLIST
bool_t xdr_dirlist(XDR *xdrs, nfsdirlist *objp);
#endif /* not HAVE_XDR_DIRLIST */
#ifndef HAVE_XDR_DIROPARGS
bool_t xdr_diropargs(XDR *xdrs, nfsdiropargs *objp);
#endif /* not HAVE_XDR_DIROPARGS */
#ifndef HAVE_XDR_DIROPOKRES
bool_t xdr_diropokres(XDR *xdrs, nfsdiropokres *objp);
#endif /* not HAVE_XDR_DIROPOKRES */
#ifndef HAVE_XDR_DIROPRES
bool_t xdr_diropres(XDR *xdrs, nfsdiropres *objp);
#endif /* not HAVE_XDR_DIROPRES */
#ifndef HAVE_XDR_DIRPATH
bool_t xdr_dirpath(XDR *xdrs, dirpath *objp);
#endif /* not HAVE_XDR_DIRPATH */
#ifndef HAVE_XDR_ENTRY
bool_t xdr_entry(XDR *xdrs, nfsentry *objp);
#endif /* not HAVE_XDR_ENTRY */
#ifndef HAVE_XDR_EXPORTNODE
bool_t xdr_exportnode(XDR *xdrs, exportnode *objp);
#endif /* not HAVE_XDR_EXPORTNODE */
#ifndef HAVE_XDR_EXPORTS
bool_t xdr_exports(XDR *xdrs, exports *objp);
#endif /* not HAVE_XDR_EXPORTS */
#ifndef HAVE_XDR_FATTR
bool_t xdr_fattr(XDR *xdrs, nfsfattr *objp);
#endif /* not HAVE_XDR_FATTR */
#ifndef HAVE_XDR_FHANDLE
bool_t xdr_fhandle(XDR *xdrs, fhandle objp);
#endif /* not HAVE_XDR_FHANDLE */
#ifndef HAVE_XDR_FHSTATUS
bool_t xdr_fhstatus(XDR *xdrs, fhstatus *objp);
#endif /* not HAVE_XDR_FHSTATUS */
#ifndef HAVE_XDR_FILENAME
bool_t xdr_filename(XDR *xdrs, filename *objp);
#endif /* not HAVE_XDR_FILENAME */
#ifndef HAVE_XDR_FTYPE
bool_t xdr_ftype(XDR *xdrs, nfsftype *objp);
#endif /* not HAVE_XDR_FTYPE */
#ifndef HAVE_XDR_GROUPNODE
bool_t xdr_groupnode(XDR *xdrs, groupnode *objp);
#endif /* not HAVE_XDR_GROUPNODE */
#ifndef HAVE_XDR_GROUPS
bool_t xdr_groups(XDR *xdrs, groups *objp);
#endif /* not HAVE_XDR_GROUPS */
#ifndef HAVE_XDR_LINKARGS
bool_t xdr_linkargs(XDR *xdrs, nfslinkargs *objp);
#endif /* not HAVE_XDR_LINKARGS */
#ifndef HAVE_XDR_MOUNTBODY
bool_t xdr_mountbody(XDR *xdrs, mountbody *objp);
#endif /* not HAVE_XDR_MOUNTBODY */
#ifndef HAVE_XDR_MOUNTLIST
bool_t xdr_mountlist(XDR *xdrs, mountlist *objp);
#endif /* not HAVE_XDR_MOUNTLIST */
#ifndef HAVE_XDR_NAME
bool_t xdr_name(XDR *xdrs, name *objp);
#endif /* not HAVE_XDR_NAME */
#ifndef HAVE_XDR_NFS_FH
bool_t xdr_nfs_fh(XDR *xdrs, am_nfs_fh *objp);
#endif /* not HAVE_XDR_NFS_FH */
#ifndef HAVE_XDR_NFSCOOKIE
bool_t xdr_nfscookie(XDR *xdrs, nfscookie objp);
#endif /* not HAVE_XDR_NFSCOOKIE */
#ifndef HAVE_XDR_NFSPATH
bool_t xdr_nfspath(XDR *xdrs, nfspath *objp);
#endif /* not HAVE_XDR_NFSPATH */
#ifndef HAVE_XDR_NFSSTAT
bool_t xdr_nfsstat(XDR *xdrs, nfsstat *objp);
#endif /* not HAVE_XDR_NFSSTAT */
#ifndef HAVE_XDR_NFSTIME
bool_t xdr_nfstime(XDR *xdrs, nfstime *objp);
#endif /* not HAVE_XDR_NFSTIME */
#ifndef HAVE_XDR_POINTER
bool_t xdr_pointer(register XDR *xdrs, char **objpp, u_int obj_size, XDRPROC_T_TYPE xdr_obj);
#endif /* not HAVE_XDR_POINTER */
#ifndef HAVE_XDR_READARGS
bool_t xdr_readargs(XDR *xdrs, nfsreadargs *objp);
#endif /* not HAVE_XDR_READARGS */
#ifndef HAVE_XDR_READDIRARGS
bool_t xdr_readdirargs(XDR *xdrs, nfsreaddirargs *objp);
#endif /* not HAVE_XDR_READDIRARGS */
#ifndef HAVE_XDR_READDIRRES
bool_t xdr_readdirres(XDR *xdrs, nfsreaddirres *objp);
#endif /* not HAVE_XDR_READDIRRES */
#ifndef HAVE_XDR_READLINKRES
bool_t xdr_readlinkres(XDR *xdrs, nfsreadlinkres *objp);
#endif /* not HAVE_XDR_READLINKRES */
#ifndef HAVE_XDR_READOKRES
bool_t xdr_readokres(XDR *xdrs, nfsreadokres *objp);
#endif /* not HAVE_XDR_READOKRES */
#ifndef HAVE_XDR_READRES
bool_t xdr_readres(XDR *xdrs, nfsreadres *objp);
#endif /* not HAVE_XDR_READRES */
#ifndef HAVE_XDR_RENAMEARGS
bool_t xdr_renameargs(XDR *xdrs, nfsrenameargs *objp);
#endif /* not HAVE_XDR_RENAMEARGS */
#ifndef HAVE_XDR_SATTR
bool_t xdr_sattr(XDR *xdrs, nfssattr *objp);
#endif /* not HAVE_XDR_SATTR */
#ifndef HAVE_XDR_SATTRARGS
bool_t xdr_sattrargs(XDR *xdrs, nfssattrargs *objp);
#endif /* not HAVE_XDR_SATTRARGS */
#ifndef HAVE_XDR_STATFSOKRES
bool_t xdr_statfsokres(XDR *xdrs, nfsstatfsokres *objp);
#endif /* not HAVE_XDR_STATFSOKRES */
#ifndef HAVE_XDR_STATFSRES
bool_t xdr_statfsres(XDR *xdrs, nfsstatfsres *objp);
#endif /* not HAVE_XDR_STATFSRES */
#ifndef HAVE_XDR_SYMLINKARGS
bool_t xdr_symlinkargs(XDR *xdrs, nfssymlinkargs *objp);
#endif /* not HAVE_XDR_SYMLINKARGS */
#ifndef HAVE_XDR_WRITEARGS
bool_t xdr_writeargs(XDR *xdrs, nfswriteargs *objp);
#endif /* not HAVE_XDR_WRITEARGS */

/*
 * NFS3 XDR FUNCTIONS:
 */
#ifdef HAVE_FS_NFS3
#define AM_NFS3_NULL 0
void * am_nfs3_null_3(void *, CLIENT *);
void * am_nfs3_null_3_svc(void *, struct svc_req *);
#define AM_NFS3_GETATTR 1
am_GETATTR3res * am_nfs3_getattr_3(am_GETATTR3args *, CLIENT *);
am_GETATTR3res * am_nfs3_getattr_3_svc(am_GETATTR3args *, struct svc_req *);
#define AM_NFS3_SETATTR 2
am_SETATTR3res * am_nfs3_setattr_3(am_SETATTR3args *, CLIENT *);
am_SETATTR3res * am_nfs3_setattr_3_svc(am_SETATTR3args *, struct svc_req *);
#define AM_NFS3_LOOKUP 3
am_LOOKUP3res * am_nfs3_lookup_3(am_LOOKUP3args *, CLIENT *);
am_LOOKUP3res * am_nfs3_lookup_3_svc(am_LOOKUP3args *, struct svc_req *);
#define AM_NFS3_ACCESS 4
am_ACCESS3res * am_nfs3_access_3(am_ACCESS3args *, CLIENT *);
am_ACCESS3res * am_nfs3_access_3_svc(am_ACCESS3args *, struct svc_req *);
#define AM_NFS3_READLINK 5
am_READLINK3res * am_nfs3_readlink_3(am_READLINK3args *, CLIENT *);
am_READLINK3res * am_nfs3_readlink_3_svc(am_READLINK3args *, struct svc_req *);
#define AM_NFS3_READ 6
am_READ3res * am_nfs3_read_3(am_READ3args *, CLIENT *);
am_READ3res * am_nfs3_read_3_svc(am_READ3args *, struct svc_req *);
#define AM_NFS3_WRITE 7
am_WRITE3res * am_nfs3_write_3(am_WRITE3args *, CLIENT *);
am_WRITE3res * am_nfs3_write_3_svc(am_WRITE3args *, struct svc_req *);
#define AM_NFS3_CREATE 8
am_CREATE3res * am_nfs3_create_3(am_CREATE3args *, CLIENT *);
am_CREATE3res * am_nfs3_create_3_svc(am_CREATE3args *, struct svc_req *);
#define AM_NFS3_MKDIR 9
am_MKDIR3res * am_nfs3_mkdir_3(am_MKDIR3args *, CLIENT *);
am_MKDIR3res * am_nfs3_mkdir_3_svc(am_MKDIR3args *, struct svc_req *);
#define AM_NFS3_SYMLINK 10
am_SYMLINK3res * am_nfs3_symlink_3(am_SYMLINK3args *, CLIENT *);
am_SYMLINK3res * am_nfs3_symlink_3_svc(am_SYMLINK3args *, struct svc_req *);
#define AM_NFS3_MKNOD 11
am_MKNOD3res * am_nfs3_mknod_3(am_MKNOD3args *, CLIENT *);
am_MKNOD3res * am_nfs3_mknod_3_svc(am_MKNOD3args *, struct svc_req *);
#define AM_NFS3_REMOVE 12
am_REMOVE3res * am_nfs3_remove_3(am_REMOVE3args *, CLIENT *);
am_REMOVE3res * am_nfs3_remove_3_svc(am_REMOVE3args *, struct svc_req *);
#define AM_NFS3_RMDIR 13
am_RMDIR3res * am_nfs3_rmdir_3(am_RMDIR3args *, CLIENT *);
am_RMDIR3res * am_nfs3_rmdir_3_svc(am_RMDIR3args *, struct svc_req *);
#define AM_NFS3_RENAME 14
am_RENAME3res * am_nfs3_rename_3(am_RENAME3args *, CLIENT *);
am_RENAME3res * am_nfs3_rename_3_svc(am_RENAME3args *, struct svc_req *);
#define AM_NFS3_LINK 15
am_LINK3res * am_nfs3_link_3(am_LINK3args *, CLIENT *);
am_LINK3res * am_nfs3_link_3_svc(am_LINK3args *, struct svc_req *);
#define AM_NFS3_READDIR 16
am_READDIR3res * am_nfs3_readdir_3(am_READDIR3args *, CLIENT *);
am_READDIR3res * am_nfs3_readdir_3_svc(am_READDIR3args *, struct svc_req *);
#define AM_NFS3_READDIRPLUS 17
am_READDIRPLUS3res * am_nfs3_readdirplus_3(am_READDIRPLUS3args *, CLIENT *);
am_READDIRPLUS3res * am_nfs3_readdirplus_3_svc(am_READDIRPLUS3args *, struct svc_req *);
#define AM_NFS3_FSSTAT 18
am_FSSTAT3res * am_nfs3_fsstat_3(am_FSSTAT3args *, CLIENT *);
am_FSSTAT3res * am_nfs3_fsstat_3_svc(am_FSSTAT3args *, struct svc_req *);
#define AM_NFS3_FSINFO 19
am_FSINFO3res * am_nfs3_fsinfo_3(am_FSINFO3args *, CLIENT *);
am_FSINFO3res * am_nfs3_fsinfo_3_svc(am_FSINFO3args *, struct svc_req *);
#define AM_NFS3_PATHCONF 20
am_PATHCONF3res * am_nfs3_pathconf_3(am_PATHCONF3args *, CLIENT *);
am_PATHCONF3res * am_nfs3_pathconf_3_svc(am_PATHCONF3args *, struct svc_req *);
#define AM_NFS3_COMMIT 21
am_COMMIT3res * am_nfs3_commit_3(am_COMMIT3args *, CLIENT *);
am_COMMIT3res * am_nfs3_commit_3_svc(am_COMMIT3args *, struct svc_req *);
int nfs_program_3_freeresult (SVCXPRT *, xdrproc_t, caddr_t);

bool_t xdr_am_fhandle3(XDR *xdrs, am_fhandle3 *objp);
bool_t xdr_am_mountstat3(XDR *xdrs, am_mountstat3 *objp);
bool_t xdr_am_mountres3_ok(XDR *xdrs, am_mountres3_ok *objp);
bool_t xdr_am_mountres3(XDR *xdrs, am_mountres3 *objp);
bool_t xdr_am_diropargs3(XDR *xdrs, am_diropargs3 *objp);
bool_t xdr_am_filename3(XDR *xdrs, am_filename3 *objp);
bool_t xdr_am_LOOKUP3args(XDR *xdrs, am_LOOKUP3args *objp);
bool_t xdr_am_LOOKUP3res(XDR *xdrs, am_LOOKUP3res *objp);
bool_t xdr_am_LOOKUP3resfail(XDR *xdrs, am_LOOKUP3resfail *objp);
bool_t xdr_am_LOOKUP3resok(XDR *xdrs, am_LOOKUP3resok *objp);
bool_t xdr_am_nfsstat3(XDR *xdrs, am_nfsstat3 *objp);
bool_t xdr_am_nfs_fh3(XDR *xdrs, am_nfs_fh3 *objp);
bool_t xdr_am_cookieverf3 (XDR *, am_cookieverf3);
bool_t xdr_uint64 (XDR *, uint64*);
bool_t xdr_am_cookie3 (XDR *, am_cookie3*);
bool_t xdr_am_nfs_fh3 (XDR *, am_nfs_fh3*);
bool_t xdr_am_nfsstat3 (XDR *, am_nfsstat3*);
bool_t xdr_am_filename3 (XDR *, am_filename3*);
bool_t xdr_am_diropargs3 (XDR *, am_diropargs3*);
bool_t xdr_am_ftype3 (XDR *, am_ftype3*);
bool_t xdr_am_mode3 (XDR *, am_mode3*);
bool_t xdr_am_uid3 (XDR *, am_uid3*);
bool_t xdr_am_gid3 (XDR *, am_gid3*);
bool_t xdr_am_size3 (XDR *, am_size3*);
bool_t xdr_am_fileid3 (XDR *, am_fileid3*);
bool_t xdr_am_specdata3 (XDR *, am_specdata3*);
bool_t xdr_am_nfstime3 (XDR *, am_nfstime3*);
bool_t xdr_am_fattr3 (XDR *, am_fattr3*);
bool_t xdr_am_post_op_attr (XDR *, am_post_op_attr*);
bool_t xdr_am_stable_how (XDR *, am_stable_how*);
bool_t xdr_am_offset3 (XDR *, am_offset3*);
bool_t xdr_am_count3 (XDR *, am_count3*);
bool_t xdr_am_wcc_attr (XDR *, am_wcc_attr*);
bool_t xdr_am_pre_op_attr (XDR *, am_pre_op_attr*);
bool_t xdr_am_wcc_data (XDR *, am_wcc_data*);
bool_t xdr_am_WRITE3args (XDR *, am_WRITE3args*);
bool_t xdr_am_writeverf3 (XDR *, am_writeverf3);
bool_t xdr_am_WRITE3resok (XDR *, am_WRITE3resok*);
bool_t xdr_am_WRITE3resfail (XDR *, am_WRITE3resfail*);
bool_t xdr_am_WRITE3res (XDR *, am_WRITE3res*);
bool_t xdr_am_LOOKUP3args (XDR *, am_LOOKUP3args*);
bool_t xdr_am_LOOKUP3resok (XDR *, am_LOOKUP3resok*);
bool_t xdr_am_LOOKUP3resfail (XDR *, am_LOOKUP3resfail*);
bool_t xdr_am_LOOKUP3res (XDR *, am_LOOKUP3res*);
bool_t xdr_am_COMMIT3args (XDR *, am_COMMIT3args*);
bool_t xdr_am_COMMIT3resok (XDR *, am_COMMIT3resok*);
bool_t xdr_am_COMMIT3resfail (XDR *, am_COMMIT3resfail*);
bool_t xdr_am_COMMIT3res (XDR *, am_COMMIT3res*);
bool_t xdr_am_ACCESS3args (XDR *, am_ACCESS3args*);
bool_t xdr_am_ACCESS3resok (XDR *, am_ACCESS3resok*);
bool_t xdr_am_ACCESS3resfail (XDR *, am_ACCESS3resfail*);
bool_t xdr_am_ACCESS3res (XDR *, am_ACCESS3res*);
bool_t xdr_am_GETATTR3args (XDR *, am_GETATTR3args*);
bool_t xdr_am_GETATTR3resok (XDR *, am_GETATTR3resok*);
bool_t xdr_am_GETATTR3res (XDR *, am_GETATTR3res*);
bool_t xdr_am_time_how (XDR *, am_time_how*);
bool_t xdr_am_set_mode3 (XDR *, am_set_mode3*);
bool_t xdr_am_set_uid3 (XDR *, am_set_uid3*);
bool_t xdr_am_set_gid3 (XDR *, am_set_gid3*);
bool_t xdr_am_set_size3 (XDR *, am_set_size3*);
bool_t xdr_am_set_atime (XDR *, am_set_atime*);
bool_t xdr_am_set_mtime (XDR *, am_set_mtime*);
bool_t xdr_am_sattr3 (XDR *, am_sattr3*);
bool_t xdr_am_createmode3 (XDR *, am_createmode3*);
bool_t xdr_am_createverf3 (XDR *, am_createverf3);
bool_t xdr_am_createhow3 (XDR *, am_createhow3*);
bool_t xdr_am_CREATE3args (XDR *, am_CREATE3args*);
bool_t xdr_am_post_op_fh3 (XDR *, am_post_op_fh3*);
bool_t xdr_am_CREATE3resok (XDR *, am_CREATE3resok*);
bool_t xdr_am_CREATE3resfail (XDR *, am_CREATE3resfail*);
bool_t xdr_am_CREATE3res (XDR *, am_CREATE3res*);
bool_t xdr_am_REMOVE3args (XDR *, am_REMOVE3args*);
bool_t xdr_am_REMOVE3resok (XDR *, am_REMOVE3resok*);
bool_t xdr_am_REMOVE3resfail (XDR *, am_REMOVE3resfail*);
bool_t xdr_am_REMOVE3res (XDR *, am_REMOVE3res*);
bool_t xdr_am_READ3args (XDR *, am_READ3args*);
bool_t xdr_am_READ3resok (XDR *, am_READ3resok*);
bool_t xdr_am_READ3resfail (XDR *, am_READ3resfail*);
bool_t xdr_am_READ3res (XDR *, am_READ3res*);
bool_t xdr_am_FSINFO3args (XDR *, am_FSINFO3args*);
bool_t xdr_am_FSINFO3resok (XDR *, am_FSINFO3resok*);
bool_t xdr_am_FSINFO3resfail (XDR *, am_FSINFO3resfail*);
bool_t xdr_am_FSINFO3res (XDR *, am_FSINFO3res*);
bool_t xdr_am_FSSTAT3args (XDR *, am_FSSTAT3args*);
bool_t xdr_am_FSSTAT3resok (XDR *, am_FSSTAT3resok*);
bool_t xdr_am_FSSTAT3resfail (XDR *, am_FSSTAT3resfail*);
bool_t xdr_am_FSSTAT3res (XDR *, am_FSSTAT3res*);
bool_t xdr_am_PATHCONF3args (XDR *, am_PATHCONF3args*);
bool_t xdr_am_PATHCONF3resok (XDR *, am_PATHCONF3resok*);
bool_t xdr_am_PATHCONF3resfail (XDR *, am_PATHCONF3resfail*);
bool_t xdr_am_PATHCONF3res (XDR *, am_PATHCONF3res*);
bool_t xdr_am_nfspath3 (XDR *, am_nfspath3*);
bool_t xdr_am_symlinkdata3 (XDR *, am_symlinkdata3*);
bool_t xdr_am_SYMLINK3args (XDR *, am_SYMLINK3args*);
bool_t xdr_am_SYMLINK3resok (XDR *, am_SYMLINK3resok*);
bool_t xdr_am_SYMLINK3resfail (XDR *, am_SYMLINK3resfail*);
bool_t xdr_am_SYMLINK3res (XDR *, am_SYMLINK3res*);
bool_t xdr_am_READLINK3args (XDR *, am_READLINK3args*);
bool_t xdr_am_READLINK3resok (XDR *, am_READLINK3resok*);
bool_t xdr_am_READLINK3resfail (XDR *, am_READLINK3resfail*);
bool_t xdr_am_READLINK3res (XDR *, am_READLINK3res*);
bool_t xdr_am_devicedata3 (XDR *, am_devicedata3*);
bool_t xdr_am_mknoddata3 (XDR *, am_mknoddata3*);
bool_t xdr_am_MKNOD3args (XDR *, am_MKNOD3args*);
bool_t xdr_am_MKNOD3resok (XDR *, am_MKNOD3resok*);
bool_t xdr_am_MKNOD3resfail (XDR *, am_MKNOD3resfail*);
bool_t xdr_am_MKNOD3res (XDR *, am_MKNOD3res*);
bool_t xdr_am_MKDIR3args (XDR *, am_MKDIR3args*);
bool_t xdr_am_MKDIR3resok (XDR *, am_MKDIR3resok*);
bool_t xdr_am_MKDIR3resfail (XDR *, am_MKDIR3resfail*);
bool_t xdr_am_MKDIR3res (XDR *, am_MKDIR3res*);
bool_t xdr_am_RMDIR3args (XDR *, am_RMDIR3args*);
bool_t xdr_am_RMDIR3resok (XDR *, am_RMDIR3resok*);
bool_t xdr_am_RMDIR3resfail (XDR *, am_RMDIR3resfail*);
bool_t xdr_am_RMDIR3res (XDR *, am_RMDIR3res*);
bool_t xdr_am_RENAME3args (XDR *, am_RENAME3args*);
bool_t xdr_am_RENAME3resok (XDR *, am_RENAME3resok*);
bool_t xdr_am_RENAME3resfail (XDR *, am_RENAME3resfail*);
bool_t xdr_am_RENAME3res (XDR *, am_RENAME3res*);
bool_t xdr_am_READDIRPLUS3args (XDR *, am_READDIRPLUS3args*);
bool_t xdr_am_entryplus3 (XDR *, am_entryplus3*);
bool_t xdr_am_dirlistplus3 (XDR *, am_dirlistplus3*);
bool_t xdr_am_READDIRPLUS3resok (XDR *, am_READDIRPLUS3resok*);
bool_t xdr_am_READDIRPLUS3resfail (XDR *, am_READDIRPLUS3resfail*);
bool_t xdr_am_READDIRPLUS3res (XDR *, am_READDIRPLUS3res*);
bool_t xdr_am_READDIR3args (XDR *, am_READDIR3args*);
bool_t xdr_am_entry3 (XDR *, am_entry3*);
bool_t xdr_am_dirlist3 (XDR *, am_dirlist3*);
bool_t xdr_am_READDIR3resok (XDR *, am_READDIR3resok*);
bool_t xdr_am_READDIR3resfail (XDR *, am_READDIR3resfail*);
bool_t xdr_am_READDIR3res (XDR *, am_READDIR3res*);
bool_t xdr_am_LINK3args (XDR *, am_LINK3args*);
bool_t xdr_am_LINK3resok (XDR *, am_LINK3resok*);
bool_t xdr_am_LINK3resfail (XDR *, am_LINK3resfail*);
bool_t xdr_am_LINK3res (XDR *, am_LINK3res*);
bool_t xdr_am_sattrguard3 (XDR *, am_sattrguard3*);
bool_t xdr_am_SETATTR3args (XDR *, am_SETATTR3args*);
bool_t xdr_am_SETATTR3resok (XDR *, am_SETATTR3resok*);
bool_t xdr_am_SETATTR3resfail (XDR *, am_SETATTR3resfail*);
bool_t xdr_am_SETATTR3res (XDR *, am_SETATTR3res*);
#endif /* HAVE_FS_NFS3 */

#endif /* not _AM_XDR_FUNC_H */
