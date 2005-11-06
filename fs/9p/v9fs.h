/*
 * V9FS definitions.
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

/*
  * Idpool structure provides lock and id management
  *
  */

struct v9fs_idpool {
	struct semaphore lock;
	struct idr pool;
};

/*
  * Session structure provides information for an opened session
  *
  */

struct v9fs_session_info {
	/* options */
	unsigned int maxdata;
	unsigned char extended;	/* set to 1 if we are using UNIX extensions */
	unsigned char nodev;	/* set to 1 if no disable device mapping */
	unsigned short port;	/* port to connect to */
	unsigned short debug;	/* debug level */
	unsigned short proto;	/* protocol to use */
	unsigned int afid;	/* authentication fid */
	unsigned int rfdno;	/* read file descriptor number */
	unsigned int wfdno;	/* write file descriptor number */


	char *name;		/* user name to mount as */
	char *remotename;	/* name of remote hierarchy being mounted */
	unsigned int uid;	/* default uid/muid for legacy support */
	unsigned int gid;	/* default gid for legacy support */

	/* book keeping */
	struct v9fs_idpool fidpool;	/* The FID pool for file descriptors */
	struct v9fs_idpool tidpool;	/* The TID pool for transactions ids */

	/* transport information */
	struct v9fs_transport *transport;

	int inprogress;		/* session in progress => true */
	int shutdown;		/* session shutting down. no more attaches. */
	unsigned char session_hung;

	/* mux private data */
	struct v9fs_fcall *curfcall;
	wait_queue_head_t read_wait;
	struct completion fcread;
	struct completion proccmpl;
	struct task_struct *recvproc;

	spinlock_t muxlock;
	struct list_head mux_fcalls;
};

/* possible values of ->proto */
enum {
	PROTO_TCP,
	PROTO_UNIX,
	PROTO_FD,
};

int v9fs_session_init(struct v9fs_session_info *, const char *, char *);
struct v9fs_session_info *v9fs_inode2v9ses(struct inode *);
void v9fs_session_close(struct v9fs_session_info *v9ses);
int v9fs_get_idpool(struct v9fs_idpool *p);
void v9fs_put_idpool(int id, struct v9fs_idpool *p);
void v9fs_session_cancel(struct v9fs_session_info *v9ses);

#define V9FS_MAGIC 0x01021997

/* other default globals */
#define V9FS_PORT		564
#define V9FS_DEFUSER	"nobody"
#define V9FS_DEFANAME	""

/* inital pool sizes for fids and tags */
#define V9FS_START_FIDS 8192
#define V9FS_START_TIDS 256
