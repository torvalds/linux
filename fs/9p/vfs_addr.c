// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contians vfs address (mmap) ops for 9P2000.
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/netfs.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <trace/events/netfs.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "cache.h"
#include "fid.h"

static void v9fs_upload_to_server(struct netfs_io_subrequest *subreq)
{
	struct inode *inode = subreq->rreq->inode;
	struct v9fs_inode __maybe_unused *v9inode = V9FS_I(inode);
	struct p9_fid *fid = subreq->rreq->netfs_priv;
	int err;

	trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
	p9_client_write(fid, subreq->start, &subreq->io_iter, &err);
	netfs_write_subrequest_terminated(subreq, err < 0 ? err : subreq->len,
					  false);
}

static void v9fs_upload_to_server_worker(struct work_struct *work)
{
	struct netfs_io_subrequest *subreq =
		container_of(work, struct netfs_io_subrequest, work);

	v9fs_upload_to_server(subreq);
}

/*
 * Set up write requests for a writeback slice.  We need to add a write request
 * for each write we want to make.
 */
static void v9fs_create_write_requests(struct netfs_io_request *wreq, loff_t start, size_t len)
{
	struct netfs_io_subrequest *subreq;

	subreq = netfs_create_write_request(wreq, NETFS_UPLOAD_TO_SERVER,
					    start, len, v9fs_upload_to_server_worker);
	if (subreq)
		netfs_queue_write_request(subreq);
}

/**
 * v9fs_issue_read - Issue a read from 9P
 * @subreq: The read to make
 */
static void v9fs_issue_read(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	struct p9_fid *fid = rreq->netfs_priv;
	int total, err;

	total = p9_client_read(fid, subreq->start + subreq->transferred,
			       &subreq->io_iter, &err);

	/* if we just extended the file size, any portion not in
	 * cache won't be on server and is zeroes */
	__set_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags);

	netfs_subreq_terminated(subreq, err ?: total, false);
}

/**
 * v9fs_init_request - Initialise a request
 * @rreq: The read request
 * @file: The file being read from
 */
static int v9fs_init_request(struct netfs_io_request *rreq, struct file *file)
{
	struct p9_fid *fid;
	bool writing = (rreq->origin == NETFS_READ_FOR_WRITE ||
			rreq->origin == NETFS_WRITEBACK ||
			rreq->origin == NETFS_WRITETHROUGH ||
			rreq->origin == NETFS_LAUNDER_WRITE ||
			rreq->origin == NETFS_UNBUFFERED_WRITE ||
			rreq->origin == NETFS_DIO_WRITE);

	if (file) {
		fid = file->private_data;
		BUG_ON(!fid);
		p9_fid_get(fid);
	} else {
		fid = v9fs_fid_find_inode(rreq->inode, writing, INVALID_UID, true);
		if (!fid) {
			WARN_ONCE(1, "folio expected an open fid inode->i_private=%p\n",
				  rreq->inode->i_private);
			return -EINVAL;
		}
	}

	/* we might need to read from a fid that was opened write-only
	 * for read-modify-write of page cache, use the writeback fid
	 * for that */
	WARN_ON(rreq->origin == NETFS_READ_FOR_WRITE && !(fid->mode & P9_ORDWR));
	rreq->netfs_priv = fid;
	return 0;
}

/**
 * v9fs_free_request - Cleanup request initialized by v9fs_init_rreq
 * @rreq: The I/O request to clean up
 */
static void v9fs_free_request(struct netfs_io_request *rreq)
{
	struct p9_fid *fid = rreq->netfs_priv;

	p9_fid_put(fid);
}

const struct netfs_request_ops v9fs_req_ops = {
	.init_request		= v9fs_init_request,
	.free_request		= v9fs_free_request,
	.issue_read		= v9fs_issue_read,
	.create_write_requests	= v9fs_create_write_requests,
};

const struct address_space_operations v9fs_addr_operations = {
	.read_folio		= netfs_read_folio,
	.readahead		= netfs_readahead,
	.dirty_folio		= netfs_dirty_folio,
	.release_folio		= netfs_release_folio,
	.invalidate_folio	= netfs_invalidate_folio,
	.launder_folio		= netfs_launder_folio,
	.direct_IO		= noop_direct_IO,
	.writepages		= netfs_writepages,
};
