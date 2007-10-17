/*
 * V9FS definitions.
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

/*
  * Session structure provides information for an opened session
  *
  */

struct v9fs_session_info {
	/* options */
	unsigned int maxdata;
	unsigned char flags;	/* session flags */
	unsigned char nodev;	/* set to 1 if no disable device mapping */
	unsigned short debug;	/* debug level */
	unsigned int afid;	/* authentication fid */
	unsigned int cache;	/* cache mode */

	char *options;		/* copy of mount options */
	char *uname;		/* user name to mount as */
	char *aname;		/* name of remote hierarchy being mounted */
	unsigned int dfltuid;	/* default uid/muid for legacy support */
	unsigned int dfltgid;	/* default gid for legacy support */
	u32 uid;		/* if ACCESS_SINGLE, the uid that has access */
	struct p9_trans_module *trans; /* 9p transport */
	struct p9_client *clnt;	/* 9p client */
	struct dentry *debugfs_dir;
};

/* session flags */
enum {
	V9FS_EXTENDED		= 0x01,	/* 9P2000.u */
	V9FS_ACCESS_MASK	= 0x06,	/* access mask */
	V9FS_ACCESS_SINGLE	= 0x02,	/* only one user can access the files */
	V9FS_ACCESS_USER	= 0x04,	/* attache per user */
	V9FS_ACCESS_ANY		= 0x06,	/* use the same attach for all users */
};

/* possible values of ->cache */
/* eventually support loose, tight, time, session, default always none */
enum {
	CACHE_NONE,		/* default */
	CACHE_LOOSE,		/* no consistency */
};

extern struct dentry *v9fs_debugfs_root;

struct p9_fid *v9fs_session_init(struct v9fs_session_info *, const char *,
									char *);
void v9fs_session_close(struct v9fs_session_info *v9ses);
void v9fs_session_cancel(struct v9fs_session_info *v9ses);

#define V9FS_MAGIC 0x01021997

/* other default globals */
#define V9FS_PORT	564
#define V9FS_DEFUSER	"nobody"
#define V9FS_DEFANAME	""
#define V9FS_DEFUID	(-2)
#define V9FS_DEFGID	(-2)

static inline struct v9fs_session_info *v9fs_inode2v9ses(struct inode *inode)
{
	return (inode->i_sb->s_fs_info);
}

static inline int v9fs_extended(struct v9fs_session_info *v9ses)
{
	return v9ses->flags & V9FS_EXTENDED;
}
