/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Whamcloud, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _LUSTREAPI_H_
#define _LUSTREAPI_H_

/** \defgroup llapi llapi
 *
 * @{
 */

#include <lustre/lustre_user.h>

typedef void (*llapi_cb_t)(char *obd_type_name, char *obd_name, char *obd_uuid, void *args);

/* lustreapi message severity level */
enum llapi_message_level {
	LLAPI_MSG_OFF    = 0,
	LLAPI_MSG_FATAL  = 1,
	LLAPI_MSG_ERROR  = 2,
	LLAPI_MSG_WARN   = 3,
	LLAPI_MSG_NORMAL = 4,
	LLAPI_MSG_INFO   = 5,
	LLAPI_MSG_DEBUG  = 6,
	LLAPI_MSG_MAX
};

/* the bottom three bits reserved for llapi_message_level */
#define LLAPI_MSG_MASK	  0x00000007
#define LLAPI_MSG_NO_ERRNO      0x00000010

extern void llapi_msg_set_level(int level);
extern void llapi_error(int level, int rc, char *fmt, ...);
#define llapi_err_noerrno(level, fmt, a...)			     \
	llapi_error((level) | LLAPI_MSG_NO_ERRNO, 0, fmt, ## a)
extern void llapi_printf(int level, char *fmt, ...);
extern int llapi_file_create(const char *name, unsigned long long stripe_size,
			     int stripe_offset, int stripe_count,
			     int stripe_pattern);
extern int llapi_file_open(const char *name, int flags, int mode,
			   unsigned long long stripe_size, int stripe_offset,
			   int stripe_count, int stripe_pattern);
extern int llapi_file_create_pool(const char *name,
				  unsigned long long stripe_size,
				  int stripe_offset, int stripe_count,
				  int stripe_pattern, char *pool_name);
extern int llapi_file_open_pool(const char *name, int flags, int mode,
				unsigned long long stripe_size,
				int stripe_offset, int stripe_count,
				int stripe_pattern, char *pool_name);
extern int llapi_poollist(const char *name);
extern int llapi_get_poollist(const char *name, char **poollist, int list_size,
			      char *buffer, int buffer_size);
extern int llapi_get_poolmembers(const char *poolname, char **members,
				 int list_size, char *buffer, int buffer_size);
extern int llapi_file_get_stripe(const char *path, struct lov_user_md *lum);
#define HAVE_LLAPI_FILE_LOOKUP
extern int llapi_file_lookup(int dirfd, const char *name);

#define VERBOSE_COUNT      0x1
#define VERBOSE_SIZE       0x2
#define VERBOSE_OFFSET     0x4
#define VERBOSE_POOL       0x8
#define VERBOSE_DETAIL     0x10
#define VERBOSE_OBJID      0x20
#define VERBOSE_GENERATION 0x40
#define VERBOSE_MDTINDEX   0x80
#define VERBOSE_ALL	(VERBOSE_COUNT | VERBOSE_SIZE | VERBOSE_OFFSET | \
			    VERBOSE_POOL | VERBOSE_OBJID | VERBOSE_GENERATION)

struct find_param {
	unsigned int maxdepth;
	time_t  atime;
	time_t  mtime;
	time_t  ctime;
	int     asign;  /* cannot be bitfields due to using pointers to */
	int     csign;  /* access them during argument parsing. */
	int     msign;
	int     type;
	int	     size_sign:2,	/* these need to be signed values */
			stripesize_sign:2,
			stripecount_sign:2;
	unsigned long long size;
	unsigned long long size_units;
	uid_t uid;
	gid_t gid;

	unsigned long   zeroend:1,
			recursive:1,
			exclude_pattern:1,
			exclude_type:1,
			exclude_obd:1,
			exclude_mdt:1,
			exclude_gid:1,
			exclude_uid:1,
			check_gid:1,	    /* group ID */
			check_uid:1,	    /* user ID */
			check_pool:1,	   /* LOV pool name */
			check_size:1,	   /* file size */
			exclude_pool:1,
			exclude_size:1,
			exclude_atime:1,
			exclude_mtime:1,
			exclude_ctime:1,
			get_lmv:1,	      /* get MDT list from LMV */
			raw:1,		  /* do not fill in defaults */
			check_stripesize:1,     /* LOV stripe size */
			exclude_stripesize:1,
			check_stripecount:1,    /* LOV stripe count */
			exclude_stripecount:1;

	int     verbose;
	int     quiet;

	/* regular expression */
	char   *pattern;

	char   *print_fmt;

	struct  obd_uuid       *obduuid;
	int		     num_obds;
	int		     num_alloc_obds;
	int		     obdindex;
	int		    *obdindexes;

	struct  obd_uuid       *mdtuuid;
	int		     num_mdts;
	int		     num_alloc_mdts;
	int		     mdtindex;
	int		    *mdtindexes;
	int		     file_mdtindex;

	int	lumlen;
	struct  lov_user_mds_data *lmd;

	char poolname[LOV_MAXPOOLNAME + 1];

	int			fp_lmv_count;
	struct lmv_user_md	*fp_lmv_md;

	unsigned long long stripesize;
	unsigned long long stripesize_units;
	unsigned long long stripecount;

	/* In-process parameters. */
	unsigned long   got_uuids:1,
			obds_printed:1,
			have_fileinfo:1;	/* file attrs and LOV xattr */
	unsigned int    depth;
	dev_t	   st_dev;
};

extern int llapi_ostlist(char *path, struct find_param *param);
extern int llapi_uuid_match(char *real_uuid, char *search_uuid);
extern int llapi_getstripe(char *path, struct find_param *param);
extern int llapi_find(char *path, struct find_param *param);

extern int llapi_file_fget_mdtidx(int fd, int *mdtidx);
extern int llapi_dir_create_pool(const char *name, int flags, int stripe_offset,
				 int stripe_count, int stripe_pattern,
				 char *poolname);
int llapi_direntry_remove(char *dname);
extern int llapi_obd_statfs(char *path, __u32 type, __u32 index,
		     struct obd_statfs *stat_buf,
		     struct obd_uuid *uuid_buf);
extern int llapi_ping(char *obd_type, char *obd_name);
extern int llapi_target_check(int num_types, char **obd_types, char *dir);
extern int llapi_file_get_lov_uuid(const char *path, struct obd_uuid *lov_uuid);
extern int llapi_file_get_lmv_uuid(const char *path, struct obd_uuid *lmv_uuid);
extern int llapi_file_fget_lov_uuid(int fd, struct obd_uuid *lov_uuid);
extern int llapi_lov_get_uuids(int fd, struct obd_uuid *uuidp, int *ost_count);
extern int llapi_lmv_get_uuids(int fd, struct obd_uuid *uuidp, int *mdt_count);
extern int llapi_is_lustre_mnttype(const char *type);
extern int llapi_search_ost(char *fsname, char *poolname, char *ostname);
extern int llapi_get_obd_count(char *mnt, int *count, int is_mdt);
extern int parse_size(char *optarg, unsigned long long *size,
		      unsigned long long *size_units, int bytes_spec);
extern int llapi_search_mounts(const char *pathname, int index,
			       char *mntdir, char *fsname);
extern int llapi_search_fsname(const char *pathname, char *fsname);
extern int llapi_getname(const char *path, char *buf, size_t size);

extern void llapi_ping_target(char *obd_type, char *obd_name,
			      char *obd_uuid, void *args);

extern int llapi_search_rootpath(char *pathname, const char *fsname);

struct mntent;
#define HAVE_LLAPI_IS_LUSTRE_MNT
extern int llapi_is_lustre_mnt(struct mntent *mnt);
extern int llapi_quotachown(char *path, int flag);
extern int llapi_quotacheck(char *mnt, int check_type);
extern int llapi_poll_quotacheck(char *mnt, struct if_quotacheck *qchk);
extern int llapi_quotactl(char *mnt, struct if_quotactl *qctl);
extern int llapi_target_iterate(int type_num, char **obd_type, void *args,
				llapi_cb_t cb);
extern int llapi_get_connect_flags(const char *mnt, __u64 *flags);
extern int llapi_lsetfacl(int argc, char *argv[]);
extern int llapi_lgetfacl(int argc, char *argv[]);
extern int llapi_rsetfacl(int argc, char *argv[]);
extern int llapi_rgetfacl(int argc, char *argv[]);
extern int llapi_cp(int argc, char *argv[]);
extern int llapi_ls(int argc, char *argv[]);
extern int llapi_fid2path(const char *device, const char *fidstr, char *path,
			  int pathlen, long long *recno, int *linkno);
extern int llapi_path2fid(const char *path, lustre_fid *fid);
extern int llapi_fd2fid(const int fd, lustre_fid *fid);

extern int llapi_get_version(char *buffer, int buffer_size, char **version);
extern int llapi_get_data_version(int fd, __u64 *data_version, __u64 flags);
extern int llapi_hsm_state_get(const char *path, struct hsm_user_state *hus);
extern int llapi_hsm_state_set(const char *path, __u64 setmask, __u64 clearmask,
			       __u32 archive_id);

extern int llapi_create_volatile_idx(char *directory, int idx, int mode);
static inline int llapi_create_volatile(char *directory, int mode)
{
	return llapi_create_volatile_idx(directory, -1, mode);
}


extern int llapi_fswap_layouts(const int fd1, const int fd2,
			       __u64 dv1, __u64 dv2, __u64 flags);
extern int llapi_swap_layouts(const char *path1, const char *path2,
			      __u64 dv1, __u64 dv2, __u64 flags);

/* Changelog interface.  priv is private state, managed internally
   by these functions */
#define CHANGELOG_FLAG_FOLLOW 0x01   /* Not yet implemented */
#define CHANGELOG_FLAG_BLOCK  0x02   /* Blocking IO makes sense in case of
   slow user parsing of the records, but it also prevents us from cleaning
   up if the records are not consumed. */

/* Records received are in extentded format now, though most of them are still
 * written in disk in changelog_rec format (to save space and time), it's
 * converted to extented format in the lustre api to ease changelog analysis. */
#define HAVE_CHANGELOG_EXTEND_REC 1

extern int llapi_changelog_start(void **priv, int flags, const char *mdtname,
				 long long startrec);
extern int llapi_changelog_fini(void **priv);
extern int llapi_changelog_recv(void *priv, struct changelog_ext_rec **rech);
extern int llapi_changelog_free(struct changelog_ext_rec **rech);
/* Allow records up to endrec to be destroyed; requires registered id. */
extern int llapi_changelog_clear(const char *mdtname, const char *idstr,
				 long long endrec);

/* HSM copytool interface.
 * priv is private state, managed internally by these functions
 */
struct hsm_copytool_private;
extern int llapi_hsm_copytool_start(struct hsm_copytool_private **priv,
				    char *fsname, int flags,
				    int archive_count, int *archives);
extern int llapi_hsm_copytool_fini(struct hsm_copytool_private **priv);
extern int llapi_hsm_copytool_recv(struct hsm_copytool_private *priv,
				   struct hsm_action_list **hal, int *msgsize);
extern int llapi_hsm_copytool_free(struct hsm_action_list **hal);
extern int llapi_hsm_copy_start(char *mnt, struct hsm_copy *copy,
				const struct hsm_action_item *hai);
extern int llapi_hsm_copy_end(char *mnt, struct hsm_copy *copy,
			      const struct hsm_progress *hp);
extern int llapi_hsm_progress(char *mnt, struct hsm_progress *hp);
extern int llapi_hsm_import(const char *dst, int archive, struct stat *st,
			    unsigned long long stripe_size, int stripe_offset,
			    int stripe_count, int stripe_pattern,
			    char *pool_name, lustre_fid *newfid);

/* HSM user interface */
extern struct hsm_user_request *llapi_hsm_user_request_alloc(int itemcount,
							     int data_len);
extern int llapi_hsm_request(char *mnt, struct hsm_user_request *request);
extern int llapi_hsm_current_action(const char *path,
				    struct hsm_current_action *hca);
/** @} llapi */

#endif
