/*
 * V9FS definitions.
 *
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
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

/**
 * enum p9_session_flags - option flags for each 9P session
 * @V9FS_PROTO_2000U: whether or not to use 9P2000.u extensions
 * @V9FS_PROTO_2000L: whether or not to use 9P2000.l extensions
 * @V9FS_ACCESS_SINGLE: only the mounting user can access the hierarchy
 * @V9FS_ACCESS_USER: a new attach will be issued for every user (default)
 * @V9FS_ACCESS_ANY: use a single attach for all users
 * @V9FS_ACCESS_MASK: bit mask of different ACCESS options
 *
 * Session flags reflect options selected by users at mount time
 */
enum p9_session_flags {
	V9FS_PROTO_2000U	= 0x01,
	V9FS_PROTO_2000L	= 0x02,
	V9FS_ACCESS_SINGLE	= 0x04,
	V9FS_ACCESS_USER	= 0x08,
	V9FS_ACCESS_ANY		= 0x0C,
	V9FS_ACCESS_MASK	= 0x0C,
};

/* possible values of ->cache */
/**
 * enum p9_cache_modes - user specified cache preferences
 * @CACHE_NONE: do not cache data, dentries, or directory contents (default)
 * @CACHE_LOOSE: cache data, dentries, and directory contents w/no consistency
 *
 * eventually support loose, tight, time, session, default always none
 */

enum p9_cache_modes {
	CACHE_NONE,
	CACHE_LOOSE,
	CACHE_FSCACHE,
};

/**
 * struct v9fs_session_info - per-instance session information
 * @flags: session options of type &p9_session_flags
 * @nodev: set to 1 to disable device mapping
 * @debug: debug level
 * @afid: authentication handle
 * @cache: cache mode of type &p9_cache_modes
 * @cachetag: the tag of the cache associated with this session
 * @fscache: session cookie associated with FS-Cache
 * @options: copy of options string given by user
 * @uname: string user name to mount hierarchy as
 * @aname: mount specifier for remote hierarchy
 * @maxdata: maximum data to be sent/recvd per protocol message
 * @dfltuid: default numeric userid to mount hierarchy as
 * @dfltgid: default numeric groupid to mount hierarchy as
 * @uid: if %V9FS_ACCESS_SINGLE, the numeric uid which mounted the hierarchy
 * @clnt: reference to 9P network client instantiated for this session
 * @slist: reference to list of registered 9p sessions
 *
 * This structure holds state for each session instance established during
 * a sys_mount() .
 *
 * Bugs: there seems to be a lot of state which could be condensed and/or
 * removed.
 */

struct v9fs_session_info {
	/* options */
	unsigned char flags;
	unsigned char nodev;
	unsigned short debug;
	unsigned int afid;
	unsigned int cache;
#ifdef CONFIG_9P_FSCACHE
	char *cachetag;
	struct fscache_cookie *fscache;
#endif

	char *uname;		/* user name to mount as */
	char *aname;		/* name of remote hierarchy being mounted */
	unsigned int maxdata;	/* max data for client interface */
	unsigned int dfltuid;	/* default uid/muid for legacy support */
	unsigned int dfltgid;	/* default gid for legacy support */
	u32 uid;		/* if ACCESS_SINGLE, the uid that has access */
	struct p9_client *clnt;	/* 9p client */
	struct list_head slist; /* list of sessions registered with v9fs */
};

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

static inline int v9fs_proto_dotu(struct v9fs_session_info *v9ses)
{
	return v9ses->flags & V9FS_PROTO_2000U;
}

static inline int v9fs_proto_dotl(struct v9fs_session_info *v9ses)
{
	return v9ses->flags & V9FS_PROTO_2000L;
}
