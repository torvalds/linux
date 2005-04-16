/* vlclient.c: AFS Volume Location Service client
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
#include "server.h"
#include "volume.h"
#include "vlclient.h"
#include "kafsasyncd.h"
#include "kafstimod.h"
#include "errors.h"
#include "internal.h"

#define VLGETENTRYBYID		503	/* AFS Get Cache Entry By ID operation ID */
#define VLGETENTRYBYNAME	504	/* AFS Get Cache Entry By Name operation ID */
#define VLPROBE			514	/* AFS Probe Volume Location Service operation ID */

static void afs_rxvl_get_entry_by_id_attn(struct rxrpc_call *call);
static void afs_rxvl_get_entry_by_id_error(struct rxrpc_call *call);

/*****************************************************************************/
/*
 * map afs VL abort codes to/from Linux error codes
 * - called with call->lock held
 */
static void afs_rxvl_aemap(struct rxrpc_call *call)
{
	int err;

	_enter("{%u,%u,%d}",
	       call->app_err_state, call->app_abort_code, call->app_errno);

	switch (call->app_err_state) {
	case RXRPC_ESTATE_LOCAL_ABORT:
		call->app_abort_code = -call->app_errno;
		return;

	case RXRPC_ESTATE_PEER_ABORT:
		switch (call->app_abort_code) {
		case AFSVL_IDEXIST:		err = -EEXIST;		break;
		case AFSVL_IO:			err = -EREMOTEIO;	break;
		case AFSVL_NAMEEXIST:		err = -EEXIST;		break;
		case AFSVL_CREATEFAIL:		err = -EREMOTEIO;	break;
		case AFSVL_NOENT:		err = -ENOMEDIUM;	break;
		case AFSVL_EMPTY:		err = -ENOMEDIUM;	break;
		case AFSVL_ENTDELETED:		err = -ENOMEDIUM;	break;
		case AFSVL_BADNAME:		err = -EINVAL;		break;
		case AFSVL_BADINDEX:		err = -EINVAL;		break;
		case AFSVL_BADVOLTYPE:		err = -EINVAL;		break;
		case AFSVL_BADSERVER:		err = -EINVAL;		break;
		case AFSVL_BADPARTITION:	err = -EINVAL;		break;
		case AFSVL_REPSFULL:		err = -EFBIG;		break;
		case AFSVL_NOREPSERVER:		err = -ENOENT;		break;
		case AFSVL_DUPREPSERVER:	err = -EEXIST;		break;
		case AFSVL_RWNOTFOUND:		err = -ENOENT;		break;
		case AFSVL_BADREFCOUNT:		err = -EINVAL;		break;
		case AFSVL_SIZEEXCEEDED:	err = -EINVAL;		break;
		case AFSVL_BADENTRY:		err = -EINVAL;		break;
		case AFSVL_BADVOLIDBUMP:	err = -EINVAL;		break;
		case AFSVL_IDALREADYHASHED:	err = -EINVAL;		break;
		case AFSVL_ENTRYLOCKED:		err = -EBUSY;		break;
		case AFSVL_BADVOLOPER:		err = -EBADRQC;		break;
		case AFSVL_BADRELLOCKTYPE:	err = -EINVAL;		break;
		case AFSVL_RERELEASE:		err = -EREMOTEIO;	break;
		case AFSVL_BADSERVERFLAG:	err = -EINVAL;		break;
		case AFSVL_PERM:		err = -EACCES;		break;
		case AFSVL_NOMEM:		err = -EREMOTEIO;	break;
		default:
			err = afs_abort_to_error(call->app_abort_code);
			break;
		}
		call->app_errno = err;
		return;

	default:
		return;
	}
} /* end afs_rxvl_aemap() */

#if 0
/*****************************************************************************/
/*
 * probe a volume location server to see if it is still alive -- unused
 */
static int afs_rxvl_probe(struct afs_server *server, int alloc_flags)
{
	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[1];
	size_t sent;
	int ret;
	__be32 param[1];

	DECLARE_WAITQUEUE(myself, current);

	/* get hold of the vlserver connection */
	ret = afs_server_get_vlconn(server, &conn);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(conn, NULL, NULL, afs_rxvl_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = VLPROBE;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	param[0] = htonl(VLPROBE);
	piov[0].iov_len = sizeof(param);
	piov[0].iov_base = param;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET,
				    alloc_flags, 0, &sent);
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

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	rxrpc_put_connection(conn);
 out:
	return ret;

} /* end afs_rxvl_probe() */
#endif

/*****************************************************************************/
/*
 * look up a volume location database entry by name
 */
int afs_rxvl_get_entry_by_name(struct afs_server *server,
			       const char *volname,
			       unsigned volnamesz,
			       struct afs_cache_vlocation *entry)
{
	DECLARE_WAITQUEUE(myself, current);

	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[3];
	unsigned tmp;
	size_t sent;
	int ret, loop;
	__be32 *bp, param[2], zero;

	_enter(",%*.*s,%u,", volnamesz, volnamesz, volname, volnamesz);

	memset(entry, 0, sizeof(*entry));

	/* get hold of the vlserver connection */
	ret = afs_server_get_vlconn(server, &conn);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(conn, NULL, NULL, afs_rxvl_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = VLGETENTRYBYNAME;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	piov[1].iov_len = volnamesz;
	piov[1].iov_base = (char *) volname;

	zero = 0;
	piov[2].iov_len = (4 - (piov[1].iov_len & 3)) & 3;
	piov[2].iov_base = &zero;

	param[0] = htonl(VLGETENTRYBYNAME);
	param[1] = htonl(piov[1].iov_len);

	piov[0].iov_len = sizeof(param);
	piov[0].iov_base = param;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 3, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	bp = rxrpc_call_alloc_scratch(call, 384);

	ret = rxrpc_call_read_data(call, bp, 384,
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
	for (loop = 0; loop < 64; loop++)
		entry->name[loop] = ntohl(*bp++);
	bp++; /* final NUL */

	bp++; /* type */
	entry->nservers = ntohl(*bp++);

	for (loop = 0; loop < 8; loop++)
		entry->servers[loop].s_addr = *bp++;

	bp += 8; /* partition IDs */

	for (loop = 0; loop < 8; loop++) {
		tmp = ntohl(*bp++);
		if (tmp & AFS_VLSF_RWVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_RW;
		if (tmp & AFS_VLSF_ROVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_RO;
		if (tmp & AFS_VLSF_BACKVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_BAK;
	}

	entry->vid[0] = ntohl(*bp++);
	entry->vid[1] = ntohl(*bp++);
	entry->vid[2] = ntohl(*bp++);

	bp++; /* clone ID */

	tmp = ntohl(*bp++); /* flags */
	if (tmp & AFS_VLF_RWEXISTS)
		entry->vidmask |= AFS_VOL_VTM_RW;
	if (tmp & AFS_VLF_ROEXISTS)
		entry->vidmask |= AFS_VOL_VTM_RO;
	if (tmp & AFS_VLF_BACKEXISTS)
		entry->vidmask |= AFS_VOL_VTM_BAK;

	ret = -ENOMEDIUM;
	if (!entry->vidmask)
		goto abort;

	/* success */
	entry->rtime = get_seconds();
	ret = 0;

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	rxrpc_put_connection(conn);
 out:
	_leave(" = %d", ret);
	return ret;

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;
} /* end afs_rxvl_get_entry_by_name() */

/*****************************************************************************/
/*
 * look up a volume location database entry by ID
 */
int afs_rxvl_get_entry_by_id(struct afs_server *server,
			     afs_volid_t volid,
			     afs_voltype_t voltype,
			     struct afs_cache_vlocation *entry)
{
	DECLARE_WAITQUEUE(myself, current);

	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[1];
	unsigned tmp;
	size_t sent;
	int ret, loop;
	__be32 *bp, param[3];

	_enter(",%x,%d,", volid, voltype);

	memset(entry, 0, sizeof(*entry));

	/* get hold of the vlserver connection */
	ret = afs_server_get_vlconn(server, &conn);
	if (ret < 0)
		goto out;

	/* create a call through that connection */
	ret = rxrpc_create_call(conn, NULL, NULL, afs_rxvl_aemap, &call);
	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		goto out_put_conn;
	}
	call->app_opcode = VLGETENTRYBYID;

	/* we want to get event notifications from the call */
	add_wait_queue(&call->waitq, &myself);

	/* marshall the parameters */
	param[0] = htonl(VLGETENTRYBYID);
	param[1] = htonl(volid);
	param[2] = htonl(voltype);

	piov[0].iov_len = sizeof(param);
	piov[0].iov_base = param;

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0)
		goto abort;

	/* wait for the reply to completely arrive */
	bp = rxrpc_call_alloc_scratch(call, 384);

	ret = rxrpc_call_read_data(call, bp, 384,
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
	for (loop = 0; loop < 64; loop++)
		entry->name[loop] = ntohl(*bp++);
	bp++; /* final NUL */

	bp++; /* type */
	entry->nservers = ntohl(*bp++);

	for (loop = 0; loop < 8; loop++)
		entry->servers[loop].s_addr = *bp++;

	bp += 8; /* partition IDs */

	for (loop = 0; loop < 8; loop++) {
		tmp = ntohl(*bp++);
		if (tmp & AFS_VLSF_RWVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_RW;
		if (tmp & AFS_VLSF_ROVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_RO;
		if (tmp & AFS_VLSF_BACKVOL)
			entry->srvtmask[loop] |= AFS_VOL_VTM_BAK;
	}

	entry->vid[0] = ntohl(*bp++);
	entry->vid[1] = ntohl(*bp++);
	entry->vid[2] = ntohl(*bp++);

	bp++; /* clone ID */

	tmp = ntohl(*bp++); /* flags */
	if (tmp & AFS_VLF_RWEXISTS)
		entry->vidmask |= AFS_VOL_VTM_RW;
	if (tmp & AFS_VLF_ROEXISTS)
		entry->vidmask |= AFS_VOL_VTM_RO;
	if (tmp & AFS_VLF_BACKEXISTS)
		entry->vidmask |= AFS_VOL_VTM_BAK;

	ret = -ENOMEDIUM;
	if (!entry->vidmask)
		goto abort;

#if 0 /* TODO: remove */
	entry->nservers = 3;
	entry->servers[0].s_addr = htonl(0xac101249);
	entry->servers[1].s_addr = htonl(0xac101243);
	entry->servers[2].s_addr = htonl(0xac10125b /*0xac10125b*/);

	entry->srvtmask[0] = AFS_VOL_VTM_RO;
	entry->srvtmask[1] = AFS_VOL_VTM_RO;
	entry->srvtmask[2] = AFS_VOL_VTM_RO | AFS_VOL_VTM_RW;
#endif

	/* success */
	entry->rtime = get_seconds();
	ret = 0;

 out_unwait:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&call->waitq, &myself);
	rxrpc_put_call(call);
 out_put_conn:
	rxrpc_put_connection(conn);
 out:
	_leave(" = %d", ret);
	return ret;

 abort:
	set_current_state(TASK_UNINTERRUPTIBLE);
	rxrpc_call_abort(call, ret);
	schedule();
	goto out_unwait;
} /* end afs_rxvl_get_entry_by_id() */

/*****************************************************************************/
/*
 * look up a volume location database entry by ID asynchronously
 */
int afs_rxvl_get_entry_by_id_async(struct afs_async_op *op,
				   afs_volid_t volid,
				   afs_voltype_t voltype)
{
	struct rxrpc_connection *conn;
	struct rxrpc_call *call;
	struct kvec piov[1];
	size_t sent;
	int ret;
	__be32 param[3];

	_enter(",%x,%d,", volid, voltype);

	/* get hold of the vlserver connection */
	ret = afs_server_get_vlconn(op->server, &conn);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	/* create a call through that connection */
	ret = rxrpc_create_call(conn,
				afs_rxvl_get_entry_by_id_attn,
				afs_rxvl_get_entry_by_id_error,
				afs_rxvl_aemap,
				&op->call);
	rxrpc_put_connection(conn);

	if (ret < 0) {
		printk("kAFS: Unable to create call: %d\n", ret);
		_leave(" = %d", ret);
		return ret;
	}

	op->call->app_opcode = VLGETENTRYBYID;
	op->call->app_user = op;

	call = op->call;
	rxrpc_get_call(call);

	/* send event notifications from the call to kafsasyncd */
	afs_kafsasyncd_begin_op(op);

	/* marshall the parameters */
	param[0] = htonl(VLGETENTRYBYID);
	param[1] = htonl(volid);
	param[2] = htonl(voltype);

	piov[0].iov_len = sizeof(param);
	piov[0].iov_base = param;

	/* allocate result read buffer in scratch space */
	call->app_scr_ptr = rxrpc_call_alloc_scratch(op->call, 384);

	/* send the parameters to the server */
	ret = rxrpc_call_write_data(call, 1, piov, RXRPC_LAST_PACKET, GFP_NOFS,
				    0, &sent);
	if (ret < 0) {
		rxrpc_call_abort(call, ret); /* handle from kafsasyncd */
		ret = 0;
		goto out;
	}

	/* wait for the reply to completely arrive */
	ret = rxrpc_call_read_data(call, call->app_scr_ptr, 384, 0);
	switch (ret) {
	case 0:
	case -EAGAIN:
	case -ECONNABORTED:
		ret = 0;
		break;	/* all handled by kafsasyncd */

	default:
		rxrpc_call_abort(call, ret); /* make kafsasyncd handle it */
		ret = 0;
		break;
	}

 out:
	rxrpc_put_call(call);
	_leave(" = %d", ret);
	return ret;

} /* end afs_rxvl_get_entry_by_id_async() */

/*****************************************************************************/
/*
 * attend to the asynchronous get VLDB entry by ID
 */
int afs_rxvl_get_entry_by_id_async2(struct afs_async_op *op,
				    struct afs_cache_vlocation *entry)
{
	__be32 *bp;
	__u32 tmp;
	int loop, ret;

	_enter("{op=%p cst=%u}", op, op->call->app_call_state);

	memset(entry, 0, sizeof(*entry));

	if (op->call->app_call_state == RXRPC_CSTATE_COMPLETE) {
		/* operation finished */
		afs_kafsasyncd_terminate_op(op);

		bp = op->call->app_scr_ptr;

		/* unmarshall the reply */
		for (loop = 0; loop < 64; loop++)
			entry->name[loop] = ntohl(*bp++);
		bp++; /* final NUL */

		bp++; /* type */
		entry->nservers = ntohl(*bp++);

		for (loop = 0; loop < 8; loop++)
			entry->servers[loop].s_addr = *bp++;

		bp += 8; /* partition IDs */

		for (loop = 0; loop < 8; loop++) {
			tmp = ntohl(*bp++);
			if (tmp & AFS_VLSF_RWVOL)
				entry->srvtmask[loop] |= AFS_VOL_VTM_RW;
			if (tmp & AFS_VLSF_ROVOL)
				entry->srvtmask[loop] |= AFS_VOL_VTM_RO;
			if (tmp & AFS_VLSF_BACKVOL)
				entry->srvtmask[loop] |= AFS_VOL_VTM_BAK;
		}

		entry->vid[0] = ntohl(*bp++);
		entry->vid[1] = ntohl(*bp++);
		entry->vid[2] = ntohl(*bp++);

		bp++; /* clone ID */

		tmp = ntohl(*bp++); /* flags */
		if (tmp & AFS_VLF_RWEXISTS)
			entry->vidmask |= AFS_VOL_VTM_RW;
		if (tmp & AFS_VLF_ROEXISTS)
			entry->vidmask |= AFS_VOL_VTM_RO;
		if (tmp & AFS_VLF_BACKEXISTS)
			entry->vidmask |= AFS_VOL_VTM_BAK;

		ret = -ENOMEDIUM;
		if (!entry->vidmask) {
			rxrpc_call_abort(op->call, ret);
			goto done;
		}

#if 0 /* TODO: remove */
		entry->nservers = 3;
		entry->servers[0].s_addr = htonl(0xac101249);
		entry->servers[1].s_addr = htonl(0xac101243);
		entry->servers[2].s_addr = htonl(0xac10125b /*0xac10125b*/);

		entry->srvtmask[0] = AFS_VOL_VTM_RO;
		entry->srvtmask[1] = AFS_VOL_VTM_RO;
		entry->srvtmask[2] = AFS_VOL_VTM_RO | AFS_VOL_VTM_RW;
#endif

		/* success */
		entry->rtime = get_seconds();
		ret = 0;
		goto done;
	}

	if (op->call->app_call_state == RXRPC_CSTATE_ERROR) {
		/* operation error */
		ret = op->call->app_errno;
		goto done;
	}

	_leave(" = -EAGAIN");
	return -EAGAIN;

 done:
	rxrpc_put_call(op->call);
	op->call = NULL;
	_leave(" = %d", ret);
	return ret;
} /* end afs_rxvl_get_entry_by_id_async2() */

/*****************************************************************************/
/*
 * handle attention events on an async get-entry-by-ID op
 * - called from krxiod
 */
static void afs_rxvl_get_entry_by_id_attn(struct rxrpc_call *call)
{
	struct afs_async_op *op = call->app_user;

	_enter("{op=%p cst=%u}", op, call->app_call_state);

	switch (call->app_call_state) {
	case RXRPC_CSTATE_COMPLETE:
		afs_kafsasyncd_attend_op(op);
		break;
	case RXRPC_CSTATE_CLNT_RCV_REPLY:
		if (call->app_async_read)
			break;
	case RXRPC_CSTATE_CLNT_GOT_REPLY:
		if (call->app_read_count == 0)
			break;
		printk("kAFS: Reply bigger than expected"
		       " {cst=%u asyn=%d mark=%Zu rdy=%Zu pr=%u%s}",
		       call->app_call_state,
		       call->app_async_read,
		       call->app_mark,
		       call->app_ready_qty,
		       call->pkt_rcv_count,
		       call->app_last_rcv ? " last" : "");

		rxrpc_call_abort(call, -EBADMSG);
		break;
	default:
		BUG();
	}

	_leave("");

} /* end afs_rxvl_get_entry_by_id_attn() */

/*****************************************************************************/
/*
 * handle error events on an async get-entry-by-ID op
 * - called from krxiod
 */
static void afs_rxvl_get_entry_by_id_error(struct rxrpc_call *call)
{
	struct afs_async_op *op = call->app_user;

	_enter("{op=%p cst=%u}", op, call->app_call_state);

	afs_kafsasyncd_attend_op(op);

	_leave("");

} /* end afs_rxvl_get_entry_by_id_error() */
