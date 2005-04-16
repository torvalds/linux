/* fsclient.c: AFS File Server client stubs
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include "fsclient.h"
#include "cmservice.h"
#include "vnode.h"
#include "server.h"
#include "errors.h"
#include "internal.h"

#define FSFETCHSTATUS		132	/* AFS Fetch file status */
#define FSFETCHDATA		130	/* AFS Fetch file data */
#define FSGIVEUPCALLBACKS	147	/* AFS Discard callback promises */
#define FSGETVOLUMEINFO		148	/* AFS Get root volume information */
#define FSGETROOTVOLUME		151	/* AFS Get root volume name */
#define FSLOOKUP		161	/* AFS lookup file in directory */

/*****************************************************************************/
/*
 * map afs abort codes to/from Linux error codes
 * - called with call->lock held
 */
static void afs_rxfs_aemap(struct rxrpc_call *call)
{
	switch (call->app_err_state) {
	case RXRPC_ESTATE_LOCAL_ABORT:
		call->app_abort_code = -call->app_errno;
		break;
	case RXRPC_ESTATE_PEER_ABORT:
		call->app_errno = afs_abort_to_error(call->app_abort_code);
		break;
	default:
		break;
	}
} /* end afs_rxfs_aemap() */

/*****************************************************************************/
/*
 * get the root volume name from a fileserver
 * - this operation doesn't seem to work correctly in OpenAFS server 1.2.2
 */
#if 0
int afs_rxfs_get_root_volume(struct afs_server *server,
			     char *buf, size_t *buflen)
{
	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[2];
	size_t sent;
	int ret;
	u32 param[1];

	DECLARE_WAITQUEUE(myself, current);

	kenter("%p,%p,%u",server, buf, *buflen);

	/* get hold of the fileserver connection */
	ret = afs_server_get_fsconn(server, &conn);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(conn, NULL, NULL, afs_rxfs_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = FSGETROOTVOLUME;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	param[0] = htonl(FSGETROOTVOLUME);

	piov[0].iov_len = sizeof(param);
	piov[0].iov_base = param;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (call->app_call_state != RXRPC_CSTATE_CLNT_RCV_REPLY ||
		    signal_pending(current))
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);

	ret = -EINTR;
	if (signal_pending(current))
		goto abort;

	switch (call->app_call_state) {
	case RXRPC_CSTATE_ERROR:
		ret = call->app_errno;
		kdebug("Got Error: %d", ret);
		goto out_unwait;

	case RXRPC_CSTATE_CLNT_GOT_REPLY:
		/* read the reply */
		kdebug("Got Reply: qty=%d", call->app_ready_qty);

		ret = -EBADMSG;
		if (call->app_ready_qty <= 4)
			goto abort;

		ret = rxrpc_call_read_data(call, NULL, call->app_ready_qty, 0);
		if (ret < 0)
			goto abort;

#if 0
		/* unmarshall the reply */
		bp = buffer;
		for (loop = 0; loop < 65; loop++)
			entry->name[loop] = ntohl(*bp++);
		entry->name[64] = 0;

		entry->type = ntohl(*bp++);
		entry->num_servers = ntohl(*bp++);

		for (loop = 0; loop < 8; loop++)
			entry->servers[loop].addr.s_addr = *bp++;

		for (loop = 0; loop < 8; loop++)
			entry->servers[loop].partition = ntohl(*bp++);

		for (loop = 0; loop < 8; loop++)
			entry->servers[loop].flags = ntohl(*bp++);

		for (loop = 0; loop < 3; loop++)
			entry->volume_ids[loop] = ntohl(*bp++);

		entry->clone_id = ntohl(*bp++);
		entry->flags = ntohl(*bp);
#endif

		/* success */
		ret = 0;
		goto out_unwait;

	default:
		BUG();
	}

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	afs_server_release_fsconn(server, conn);
 out:
	kleave("");
	return ret;
} /* end afs_rxfs_get_root_volume() */
#endif

/*****************************************************************************/
/*
 * get information about a volume
 */
#if 0
int afs_rxfs_get_volume_info(struct afs_server *server,
			     const char *name,
			     struct afs_volume_info *vinfo)
{
	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[3];
	size_t sent;
	int ret;
	u32 param[2], *bp, zero;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%p,%s,%p", server, name, vinfo);

	/* get hold of the fileserver connection */
	ret = afs_server_get_fsconn(server, &conn);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(conn, NULL, NULL, afs_rxfs_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = FSGETVOLUMEINFO;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	piov[1].iov_len = strlen(name);
	piov[1].iov_base = (char *) name;

	zero = 0;
	piov[2].iov_len = (4 - (piov[1].iov_len & 3)) & 3;
	piov[2].iov_base = &zero;

	param[0] = htonl(FSGETVOLUMEINFO);
	param[1] = htonl(piov[1].iov_len);

	piov[0].iov_len = sizeof(param);
	piov[0].iov_base = param;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 3, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	bp = rxrpc_call_alloc_scratch(call, 64);

	ret = rxrpc_call_read_data(call, bp, 64,
				   RXRPC_CALL_READ_BLOCK |
				   RXRPC_CALL_READ_ALL);
	if (ret < 0) {
		if (ret == -ECONNABORTED) {
			ret = call->app_errno;
			goto out_unwait;
		}
		goto abort;
	}

	/* unmarshall the reply */
	vinfo->vid = ntohl(*bp++);
	vinfo->type = ntohl(*bp++);

	vinfo->type_vids[0] = ntohl(*bp++);
	vinfo->type_vids[1] = ntohl(*bp++);
	vinfo->type_vids[2] = ntohl(*bp++);
	vinfo->type_vids[3] = ntohl(*bp++);
	vinfo->type_vids[4] = ntohl(*bp++);

	vinfo->nservers = ntohl(*bp++);
	vinfo->servers[0].addr.s_addr = *bp++;
	vinfo->servers[1].addr.s_addr = *bp++;
	vinfo->servers[2].addr.s_addr = *bp++;
	vinfo->servers[3].addr.s_addr = *bp++;
	vinfo->servers[4].addr.s_addr = *bp++;
	vinfo->servers[5].addr.s_addr = *bp++;
	vinfo->servers[6].addr.s_addr = *bp++;
	vinfo->servers[7].addr.s_addr = *bp++;

	ret = -EBADMSG;
	if (vinfo->nservers > 8)
		goto abort;

	/* success */
	ret = 0;

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	afs_server_release_fsconn(server, conn);
 out:
	_leave("");
	return ret;

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;

} /* end afs_rxfs_get_volume_info() */
#endif

/*****************************************************************************/
/*
 * fetch the status information for a file
 */
int afs_rxfs_fetch_file_status(struct afs_server *server,
			       struct afs_vnode *vnode,
			       struct afs_volsync *volsync)
{
	struct afs_server_callslot callslot;
	struct rxrpc_call *call;
	struct kvec piov[1];
	size_t sent;
	int ret;
	__be32 *bp;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%p,{%u,%u,%u}",
	       server, vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);

	/* get hold of the fileserver connection */
	ret = afs_server_request_callslot(server, &callslot);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(callslot.conn, NULL, NULL, afs_rxfs_aemap,
				&call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = FSFETCHSTATUS;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	bp = rxrpc_call_alloc_scratch(call, 16);
	bp[0] = htonl(FSFETCHSTATUS);
	bp[1] = htonl(vnode->fid.vid);
	bp[2] = htonl(vnode->fid.vnode);
	bp[3] = htonl(vnode->fid.unique);

	piov[0].iov_len = 16;
	piov[0].iov_base = bp;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	bp = rxrpc_call_alloc_scratch(call, 120);

	ret = rxrpc_call_read_data(call, bp, 120,
				   RXRPC_CALL_READ_BLOCK |
				   RXRPC_CALL_READ_ALL);
	if (ret < 0) {
		if (ret == -ECONNABORTED) {
			ret = call->app_errno;
			goto out_unwait;
		}
		goto abort;
	}

	/* unmarshall the reply */
	vnode->status.if_version	= ntohl(*bp++);
	vnode->status.type		= ntohl(*bp++);
	vnode->status.nlink		= ntohl(*bp++);
	vnode->status.size		= ntohl(*bp++);
	vnode->status.version		= ntohl(*bp++);
	vnode->status.author		= ntohl(*bp++);
	vnode->status.owner		= ntohl(*bp++);
	vnode->status.caller_access	= ntohl(*bp++);
	vnode->status.anon_access	= ntohl(*bp++);
	vnode->status.mode		= ntohl(*bp++);
	vnode->status.parent.vid	= vnode->fid.vid;
	vnode->status.parent.vnode	= ntohl(*bp++);
	vnode->status.parent.unique	= ntohl(*bp++);
	bp++; /* seg size */
	vnode->status.mtime_client	= ntohl(*bp++);
	vnode->status.mtime_server	= ntohl(*bp++);
	bp++; /* group */
	bp++; /* sync counter */
	vnode->status.version |= ((unsigned long long) ntohl(*bp++)) << 32;
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */

	vnode->cb_version		= ntohl(*bp++);
	vnode->cb_expiry		= ntohl(*bp++);
	vnode->cb_type			= ntohl(*bp++);

	if (volsync) {
		volsync->creation	= ntohl(*bp++);
		bp++; /* spare2 */
		bp++; /* spare3 */
		bp++; /* spare4 */
		bp++; /* spare5 */
		bp++; /* spare6 */
	}

	/* success */
	ret = 0;

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	afs_server_release_callslot(server, &callslot);
 out:
	_leave("");
	return ret;

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;
} /* end afs_rxfs_fetch_file_status() */

/*****************************************************************************/
/*
 * fetch the contents of a file or directory
 */
int afs_rxfs_fetch_file_data(struct afs_server *server,
			     struct afs_vnode *vnode,
			     struct afs_rxfs_fetch_descriptor *desc,
			     struct afs_volsync *volsync)
{
	struct afs_server_callslot callslot;
	struct rxrpc_call *call;
	struct kvec piov[1];
	size_t sent;
	int ret;
	__be32 *bp;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%p,{fid={%u,%u,%u},sz=%Zu,of=%lu}",
	       server,
	       desc->fid.vid,
	       desc->fid.vnode,
	       desc->fid.unique,
	       desc->size,
	       desc->offset);

	/* get hold of the fileserver connection */
	ret = afs_server_request_callslot(server, &callslot);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(callslot.conn, NULL, NULL, afs_rxfs_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = FSFETCHDATA;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	bp = rxrpc_call_alloc_scratch(call, 24);
	bp[0] = htonl(FSFETCHDATA);
	bp[1] = htonl(desc->fid.vid);
	bp[2] = htonl(desc->fid.vnode);
	bp[3] = htonl(desc->fid.unique);
	bp[4] = htonl(desc->offset);
	bp[5] = htonl(desc->size);

	piov[0].iov_len = 24;
	piov[0].iov_base = bp;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the data count to arrive */
	ret = rxrpc_call_read_data(call, bp, 4, RXRPC_CALL_READ_BLOCK);
	if (ret < 0)
		goto read_failed;

	desc->actual = ntohl(bp[0]);
	if (desc->actual != desc->size) {
		ret = -EBADMSG;
		goto abort;
	}

	/* call the app to read the actual data */
	rxrpc_call_reset_scratch(call);

	ret = rxrpc_call_read_data(call, desc->buffer, desc->actual,
				   RXRPC_CALL_READ_BLOCK);
	if (ret < 0)
		goto read_failed;

	/* wait for the rest of the reply to completely arrive */
	rxrpc_call_reset_scratch(call);
	bp = rxrpc_call_alloc_scratch(call, 120);

	ret = rxrpc_call_read_data(call, bp, 120,
				   RXRPC_CALL_READ_BLOCK |
				   RXRPC_CALL_READ_ALL);
	if (ret < 0)
		goto read_failed;

	/* unmarshall the reply */
	vnode->status.if_version	= ntohl(*bp++);
	vnode->status.type		= ntohl(*bp++);
	vnode->status.nlink		= ntohl(*bp++);
	vnode->status.size		= ntohl(*bp++);
	vnode->status.version		= ntohl(*bp++);
	vnode->status.author		= ntohl(*bp++);
	vnode->status.owner		= ntohl(*bp++);
	vnode->status.caller_access	= ntohl(*bp++);
	vnode->status.anon_access	= ntohl(*bp++);
	vnode->status.mode		= ntohl(*bp++);
	vnode->status.parent.vid	= desc->fid.vid;
	vnode->status.parent.vnode	= ntohl(*bp++);
	vnode->status.parent.unique	= ntohl(*bp++);
	bp++; /* seg size */
	vnode->status.mtime_client	= ntohl(*bp++);
	vnode->status.mtime_server	= ntohl(*bp++);
	bp++; /* group */
	bp++; /* sync counter */
	vnode->status.version |= ((unsigned long long) ntohl(*bp++)) << 32;
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */

	vnode->cb_version		= ntohl(*bp++);
	vnode->cb_expiry		= ntohl(*bp++);
	vnode->cb_type			= ntohl(*bp++);

	if (volsync) {
		volsync->creation	= ntohl(*bp++);
		bp++; /* spare2 */
		bp++; /* spare3 */
		bp++; /* spare4 */
		bp++; /* spare5 */
		bp++; /* spare6 */
	}

	/* success */
	ret = 0;

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq,&myself);
	rxrpc_put_call(call);
 out_put_conn:
	afs_server_release_callslot(server, &callslot);
 out:
	_leave(" = %d", ret);
	return ret;

 read_failed:
	if (ret == -ECONNABORTED) {
		ret = call->app_errno;
		goto out_unwait;
	}

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;

} /* end afs_rxfs_fetch_file_data() */

/*****************************************************************************/
/*
 * ask the AFS fileserver to discard a callback request on a file
 */
int afs_rxfs_give_up_callback(struct afs_server *server,
			      struct afs_vnode *vnode)
{
	struct afs_server_callslot callslot;
	struct rxrpc_call *call;
	struct kvec piov[1];
	size_t sent;
	int ret;
	__be32 *bp;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%p,{%u,%u,%u}",
	       server, vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);

	/* get hold of the fileserver connection */
	ret = afs_server_request_callslot(server, &callslot);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(callslot.conn, NULL, NULL, afs_rxfs_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = FSGIVEUPCALLBACKS;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	bp = rxrpc_call_alloc_scratch(call, (1 + 4 + 4) * 4);

	piov[0].iov_len = (1 + 4 + 4) * 4;
	piov[0].iov_base = bp;

	*bp++ = htonl(FSGIVEUPCALLBACKS);
	*bp++ = htonl(1);
	*bp++ = htonl(vnode->fid.vid);
	*bp++ = htonl(vnode->fid.vnode);
	*bp++ = htonl(vnode->fid.unique);
	*bp++ = htonl(1);
	*bp++ = htonl(vnode->cb_version);
	*bp++ = htonl(vnode->cb_expiry);
	*bp++ = htonl(vnode->cb_type);

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (call->app_call_state != RXRPC_CSTATE_CLNT_RCV_REPLY ||
		    signal_pending(current))
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);

	ret = -EINTR;
	if (signal_pending(current))
		goto abort;

	switch (call->app_call_state) {
	case RXRPC_CSTATE_ERROR:
		ret = call->app_errno;
		goto out_unwait;

	case RXRPC_CSTATE_CLNT_GOT_REPLY:
		ret = 0;
		goto out_unwait;

	default:
		BUG();
	}

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	afs_server_release_callslot(server, &callslot);
 out:
	_leave("");
	return ret;

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;
} /* end afs_rxfs_give_up_callback() */

/*****************************************************************************/
/*
 * look a filename up in a directory
 * - this operation doesn't seem to work correctly in OpenAFS server 1.2.2
 */
#if 0
int afs_rxfs_lookup(struct afs_server *server,
		    struct afs_vnode *dir,
		    const char *filename,
		    struct afs_vnode *vnode,
		    struct afs_volsync *volsync)
{
	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[3];
	size_t sent;
	int ret;
	u32 *bp, zero;

	DECLARE_WAITQUEUE(myself, current);

	kenter("%p,{%u,%u,%u},%s",
	       server, fid->vid, fid->vnode, fid->unique, filename);

	/* get hold of the fileserver connection */
	ret = afs_server_get_fsconn(server, &conn);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(conn, NULL, NULL, afs_rxfs_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = FSLOOKUP;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq,&myself);

	/* marshall the parameters */
	bp = rxrpc_call_alloc_scratch(call, 20);

	zero = 0;

	piov[0].iov_len = 20;
	piov[0].iov_base = bp;
	piov[1].iov_len = strlen(filename);
	piov[1].iov_base = (char *) filename;
	piov[2].iov_len = (4 - (piov[1].iov_len & 3)) & 3;
	piov[2].iov_base = &zero;

	*bp++ = htonl(FSLOOKUP);
	*bp++ = htonl(dirfid->vid);
	*bp++ = htonl(dirfid->vnode);
	*bp++ = htonl(dirfid->unique);
	*bp++ = htonl(piov[1].iov_len);

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 3, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	bp = rxrpc_call_alloc_scratch(call, 220);

	ret = rxrpc_call_read_data(call, bp, 220,
				   RXRPC_CALL_READ_BLOCK |
				   RXRPC_CALL_READ_ALL);
	if (ret < 0) {
		if (ret == -ECONNABORTED) {
			ret = call->app_errno;
			goto out_unwait;
		}
		goto abort;
	}

	/* unmarshall the reply */
	fid->vid		= ntohl(*bp++);
	fid->vnode		= ntohl(*bp++);
	fid->unique		= ntohl(*bp++);

	vnode->status.if_version	= ntohl(*bp++);
	vnode->status.type		= ntohl(*bp++);
	vnode->status.nlink		= ntohl(*bp++);
	vnode->status.size		= ntohl(*bp++);
	vnode->status.version		= ntohl(*bp++);
	vnode->status.author		= ntohl(*bp++);
	vnode->status.owner		= ntohl(*bp++);
	vnode->status.caller_access	= ntohl(*bp++);
	vnode->status.anon_access	= ntohl(*bp++);
	vnode->status.mode		= ntohl(*bp++);
	vnode->status.parent.vid	= dirfid->vid;
	vnode->status.parent.vnode	= ntohl(*bp++);
	vnode->status.parent.unique	= ntohl(*bp++);
	bp++; /* seg size */
	vnode->status.mtime_client	= ntohl(*bp++);
	vnode->status.mtime_server	= ntohl(*bp++);
	bp++; /* group */
	bp++; /* sync counter */
	vnode->status.version |= ((unsigned long long) ntohl(*bp++)) << 32;
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */

	dir->status.if_version		= ntohl(*bp++);
	dir->status.type		= ntohl(*bp++);
	dir->status.nlink		= ntohl(*bp++);
	dir->status.size		= ntohl(*bp++);
	dir->status.version		= ntohl(*bp++);
	dir->status.author		= ntohl(*bp++);
	dir->status.owner		= ntohl(*bp++);
	dir->status.caller_access	= ntohl(*bp++);
	dir->status.anon_access		= ntohl(*bp++);
	dir->status.mode		= ntohl(*bp++);
	dir->status.parent.vid		= dirfid->vid;
	dir->status.parent.vnode	= ntohl(*bp++);
	dir->status.parent.unique	= ntohl(*bp++);
	bp++; /* seg size */
	dir->status.mtime_client	= ntohl(*bp++);
	dir->status.mtime_server	= ntohl(*bp++);
	bp++; /* group */
	bp++; /* sync counter */
	dir->status.version |= ((unsigned long long) ntohl(*bp++)) << 32;
	bp++; /* spare2 */
	bp++; /* spare3 */
	bp++; /* spare4 */

	callback->fid		= *fid;
	callback->version	= ntohl(*bp++);
	callback->expiry	= ntohl(*bp++);
	callback->type		= ntohl(*bp++);

	if (volsync) {
		volsync->creation	= ntohl(*bp++);
		bp++; /* spare2 */
		bp++; /* spare3 */
		bp++; /* spare4 */
		bp++; /* spare5 */
		bp++; /* spare6 */
	}

	/* success */
	ret = 0;

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	afs_server_release_fsconn(server, conn);
 out:
	kleave("");
	return ret;

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;
} /* end afs_rxfs_lookup() */
#endif
