// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS Cache Manager Service
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ip.h>
#include "internal.h"
#include "afs_cm.h"
#include "protocol_yfs.h"

static int afs_deliver_cb_init_call_back_state(struct afs_call *);
static int afs_deliver_cb_init_call_back_state3(struct afs_call *);
static int afs_deliver_cb_probe(struct afs_call *);
static int afs_deliver_cb_callback(struct afs_call *);
static int afs_deliver_cb_probe_uuid(struct afs_call *);
static int afs_deliver_cb_tell_me_about_yourself(struct afs_call *);
static void afs_cm_destructor(struct afs_call *);
static void SRXAFSCB_CallBack(struct work_struct *);
static void SRXAFSCB_InitCallBackState(struct work_struct *);
static void SRXAFSCB_Probe(struct work_struct *);
static void SRXAFSCB_ProbeUuid(struct work_struct *);
static void SRXAFSCB_TellMeAboutYourself(struct work_struct *);

static int afs_deliver_yfs_cb_callback(struct afs_call *);

#define CM_NAME(name) \
	char afs_SRXCB##name##_name[] __tracepoint_string =	\
		"CB." #name

/*
 * CB.CallBack operation type
 */
static CM_NAME(CallBack);
static const struct afs_call_type afs_SRXCBCallBack = {
	.name		= afs_SRXCBCallBack_name,
	.deliver	= afs_deliver_cb_callback,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_CallBack,
};

/*
 * CB.InitCallBackState operation type
 */
static CM_NAME(InitCallBackState);
static const struct afs_call_type afs_SRXCBInitCallBackState = {
	.name		= afs_SRXCBInitCallBackState_name,
	.deliver	= afs_deliver_cb_init_call_back_state,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_InitCallBackState,
};

/*
 * CB.InitCallBackState3 operation type
 */
static CM_NAME(InitCallBackState3);
static const struct afs_call_type afs_SRXCBInitCallBackState3 = {
	.name		= afs_SRXCBInitCallBackState3_name,
	.deliver	= afs_deliver_cb_init_call_back_state3,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_InitCallBackState,
};

/*
 * CB.Probe operation type
 */
static CM_NAME(Probe);
static const struct afs_call_type afs_SRXCBProbe = {
	.name		= afs_SRXCBProbe_name,
	.deliver	= afs_deliver_cb_probe,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_Probe,
};

/*
 * CB.ProbeUuid operation type
 */
static CM_NAME(ProbeUuid);
static const struct afs_call_type afs_SRXCBProbeUuid = {
	.name		= afs_SRXCBProbeUuid_name,
	.deliver	= afs_deliver_cb_probe_uuid,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_ProbeUuid,
};

/*
 * CB.TellMeAboutYourself operation type
 */
static CM_NAME(TellMeAboutYourself);
static const struct afs_call_type afs_SRXCBTellMeAboutYourself = {
	.name		= afs_SRXCBTellMeAboutYourself_name,
	.deliver	= afs_deliver_cb_tell_me_about_yourself,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_TellMeAboutYourself,
};

/*
 * YFS CB.CallBack operation type
 */
static CM_NAME(YFS_CallBack);
static const struct afs_call_type afs_SRXYFSCB_CallBack = {
	.name		= afs_SRXCBYFS_CallBack_name,
	.deliver	= afs_deliver_yfs_cb_callback,
	.destructor	= afs_cm_destructor,
	.work		= SRXAFSCB_CallBack,
};

/*
 * route an incoming cache manager call
 * - return T if supported, F if not
 */
bool afs_cm_incoming_call(struct afs_call *call)
{
	_enter("{%u, CB.OP %u}", call->service_id, call->operation_ID);

	call->epoch = rxrpc_kernel_get_epoch(call->net->socket, call->rxcall);

	switch (call->operation_ID) {
	case CBCallBack:
		call->type = &afs_SRXCBCallBack;
		return true;
	case CBInitCallBackState:
		call->type = &afs_SRXCBInitCallBackState;
		return true;
	case CBInitCallBackState3:
		call->type = &afs_SRXCBInitCallBackState3;
		return true;
	case CBProbe:
		call->type = &afs_SRXCBProbe;
		return true;
	case CBProbeUuid:
		call->type = &afs_SRXCBProbeUuid;
		return true;
	case CBTellMeAboutYourself:
		call->type = &afs_SRXCBTellMeAboutYourself;
		return true;
	case YFSCBCallBack:
		if (call->service_id != YFS_CM_SERVICE)
			return false;
		call->type = &afs_SRXYFSCB_CallBack;
		return true;
	default:
		return false;
	}
}

/*
 * Record a probe to the cache manager from a server.
 */
static int afs_record_cm_probe(struct afs_call *call, struct afs_server *server)
{
	_enter("");

	if (test_bit(AFS_SERVER_FL_HAVE_EPOCH, &server->flags) &&
	    !test_bit(AFS_SERVER_FL_PROBING, &server->flags)) {
		if (server->cm_epoch == call->epoch)
			return 0;

		if (!server->probe.said_rebooted) {
			pr_notice("kAFS: FS rebooted %pU\n", &server->uuid);
			server->probe.said_rebooted = true;
		}
	}

	spin_lock(&server->probe_lock);

	if (!test_bit(AFS_SERVER_FL_HAVE_EPOCH, &server->flags)) {
		server->cm_epoch = call->epoch;
		server->probe.cm_epoch = call->epoch;
		goto out;
	}

	if (server->probe.cm_probed &&
	    call->epoch != server->probe.cm_epoch &&
	    !server->probe.said_inconsistent) {
		pr_notice("kAFS: FS endpoints inconsistent %pU\n",
			  &server->uuid);
		server->probe.said_inconsistent = true;
	}

	if (!server->probe.cm_probed || call->epoch == server->cm_epoch)
		server->probe.cm_epoch = server->cm_epoch;

out:
	server->probe.cm_probed = true;
	spin_unlock(&server->probe_lock);
	return 0;
}

/*
 * Find the server record by peer address and record a probe to the cache
 * manager from a server.
 */
static int afs_find_cm_server_by_peer(struct afs_call *call)
{
	struct sockaddr_rxrpc srx;
	struct afs_server *server;

	rxrpc_kernel_get_peer(call->net->socket, call->rxcall, &srx);

	server = afs_find_server(call->net, &srx);
	if (!server) {
		trace_afs_cm_no_server(call, &srx);
		return 0;
	}

	call->server = server;
	return afs_record_cm_probe(call, server);
}

/*
 * Find the server record by server UUID and record a probe to the cache
 * manager from a server.
 */
static int afs_find_cm_server_by_uuid(struct afs_call *call,
				      struct afs_uuid *uuid)
{
	struct afs_server *server;

	rcu_read_lock();
	server = afs_find_server_by_uuid(call->net, call->request);
	rcu_read_unlock();
	if (!server) {
		trace_afs_cm_no_server_u(call, call->request);
		return 0;
	}

	call->server = server;
	return afs_record_cm_probe(call, server);
}

/*
 * Clean up a cache manager call.
 */
static void afs_cm_destructor(struct afs_call *call)
{
	kfree(call->buffer);
	call->buffer = NULL;
}

/*
 * The server supplied a list of callbacks that it wanted to break.
 */
static void SRXAFSCB_CallBack(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);

	_enter("");

	/* We need to break the callbacks before sending the reply as the
	 * server holds up change visibility till it receives our reply so as
	 * to maintain cache coherency.
	 */
	if (call->server) {
		trace_afs_server(call->server, atomic_read(&call->server->usage),
				 afs_server_trace_callback);
		afs_break_callbacks(call->server, call->count, call->request);
	}

	afs_send_empty_reply(call);
	afs_put_call(call);
	_leave("");
}

/*
 * deliver request data to a CB.CallBack call
 */
static int afs_deliver_cb_callback(struct afs_call *call)
{
	struct afs_callback_break *cb;
	__be32 *bp;
	int ret, loop;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;

		/* extract the FID array and its count in two steps */
		/* fall through */
	case 1:
		_debug("extract FID count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("FID count: %u", call->count);
		if (call->count > AFSCBMAX)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_cb_fid_count);

		call->buffer = kmalloc(array3_size(call->count, 3, 4),
				       GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		afs_extract_to_buf(call, call->count * 3 * 4);
		call->unmarshall++;

		/* Fall through */
	case 2:
		_debug("extract FID array");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		_debug("unmarshall FID array");
		call->request = kcalloc(call->count,
					sizeof(struct afs_callback_break),
					GFP_KERNEL);
		if (!call->request)
			return -ENOMEM;

		cb = call->request;
		bp = call->buffer;
		for (loop = call->count; loop > 0; loop--, cb++) {
			cb->fid.vid	= ntohl(*bp++);
			cb->fid.vnode	= ntohl(*bp++);
			cb->fid.unique	= ntohl(*bp++);
		}

		afs_extract_to_tmp(call);
		call->unmarshall++;

		/* extract the callback array and its count in two steps */
		/* fall through */
	case 3:
		_debug("extract CB count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count2 = ntohl(call->tmp);
		_debug("CB count: %u", call->count2);
		if (call->count2 != call->count && call->count2 != 0)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_cb_count);
		call->_iter = &call->iter;
		iov_iter_discard(&call->iter, READ, call->count2 * 3 * 4);
		call->unmarshall++;

		/* Fall through */
	case 4:
		_debug("extract discard %zu/%u",
		       iov_iter_count(&call->iter), call->count2 * 3 * 4);

		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		call->unmarshall++;
	case 5:
		break;
	}

	if (!afs_check_call_state(call, AFS_CALL_SV_REPLYING))
		return afs_io_error(call, afs_io_error_cm_reply);

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	return afs_find_cm_server_by_peer(call);
}

/*
 * allow the fileserver to request callback state (re-)initialisation
 */
static void SRXAFSCB_InitCallBackState(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);

	_enter("{%p}", call->server);

	if (call->server)
		afs_init_callback_state(call->server);
	afs_send_empty_reply(call);
	afs_put_call(call);
	_leave("");
}

/*
 * deliver request data to a CB.InitCallBackState call
 */
static int afs_deliver_cb_init_call_back_state(struct afs_call *call)
{
	int ret;

	_enter("");

	afs_extract_discard(call, 0);
	ret = afs_extract_data(call, false);
	if (ret < 0)
		return ret;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	return afs_find_cm_server_by_peer(call);
}

/*
 * deliver request data to a CB.InitCallBackState3 call
 */
static int afs_deliver_cb_init_call_back_state3(struct afs_call *call)
{
	struct afs_uuid *r;
	unsigned loop;
	__be32 *b;
	int ret;

	_enter("");

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->buffer = kmalloc_array(11, sizeof(__be32), GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		afs_extract_to_buf(call, 11 * sizeof(__be32));
		call->unmarshall++;

		/* Fall through */
	case 1:
		_debug("extract UUID");
		ret = afs_extract_data(call, false);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		_debug("unmarshall UUID");
		call->request = kmalloc(sizeof(struct afs_uuid), GFP_KERNEL);
		if (!call->request)
			return -ENOMEM;

		b = call->buffer;
		r = call->request;
		r->time_low			= b[0];
		r->time_mid			= htons(ntohl(b[1]));
		r->time_hi_and_version		= htons(ntohl(b[2]));
		r->clock_seq_hi_and_reserved 	= ntohl(b[3]);
		r->clock_seq_low		= ntohl(b[4]);

		for (loop = 0; loop < 6; loop++)
			r->node[loop] = ntohl(b[loop + 5]);

		call->unmarshall++;

	case 2:
		break;
	}

	if (!afs_check_call_state(call, AFS_CALL_SV_REPLYING))
		return afs_io_error(call, afs_io_error_cm_reply);

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	return afs_find_cm_server_by_uuid(call, call->request);
}

/*
 * allow the fileserver to see if the cache manager is still alive
 */
static void SRXAFSCB_Probe(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);

	_enter("");
	afs_send_empty_reply(call);
	afs_put_call(call);
	_leave("");
}

/*
 * deliver request data to a CB.Probe call
 */
static int afs_deliver_cb_probe(struct afs_call *call)
{
	int ret;

	_enter("");

	afs_extract_discard(call, 0);
	ret = afs_extract_data(call, false);
	if (ret < 0)
		return ret;

	if (!afs_check_call_state(call, AFS_CALL_SV_REPLYING))
		return afs_io_error(call, afs_io_error_cm_reply);
	return afs_find_cm_server_by_peer(call);
}

/*
 * allow the fileserver to quickly find out if the fileserver has been rebooted
 */
static void SRXAFSCB_ProbeUuid(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);
	struct afs_uuid *r = call->request;

	struct {
		__be32	match;
	} reply;

	_enter("");

	if (memcmp(r, &call->net->uuid, sizeof(call->net->uuid)) == 0)
		reply.match = htonl(0);
	else
		reply.match = htonl(1);

	afs_send_simple_reply(call, &reply, sizeof(reply));
	afs_put_call(call);
	_leave("");
}

/*
 * deliver request data to a CB.ProbeUuid call
 */
static int afs_deliver_cb_probe_uuid(struct afs_call *call)
{
	struct afs_uuid *r;
	unsigned loop;
	__be32 *b;
	int ret;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->buffer = kmalloc_array(11, sizeof(__be32), GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		afs_extract_to_buf(call, 11 * sizeof(__be32));
		call->unmarshall++;

		/* Fall through */
	case 1:
		_debug("extract UUID");
		ret = afs_extract_data(call, false);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		_debug("unmarshall UUID");
		call->request = kmalloc(sizeof(struct afs_uuid), GFP_KERNEL);
		if (!call->request)
			return -ENOMEM;

		b = call->buffer;
		r = call->request;
		r->time_low			= b[0];
		r->time_mid			= htons(ntohl(b[1]));
		r->time_hi_and_version		= htons(ntohl(b[2]));
		r->clock_seq_hi_and_reserved 	= ntohl(b[3]);
		r->clock_seq_low		= ntohl(b[4]);

		for (loop = 0; loop < 6; loop++)
			r->node[loop] = ntohl(b[loop + 5]);

		call->unmarshall++;

	case 2:
		break;
	}

	if (!afs_check_call_state(call, AFS_CALL_SV_REPLYING))
		return afs_io_error(call, afs_io_error_cm_reply);
	return afs_find_cm_server_by_uuid(call, call->request);
}

/*
 * allow the fileserver to ask about the cache manager's capabilities
 */
static void SRXAFSCB_TellMeAboutYourself(struct work_struct *work)
{
	struct afs_interface *ifs;
	struct afs_call *call = container_of(work, struct afs_call, work);
	int loop, nifs;

	struct {
		struct /* InterfaceAddr */ {
			__be32 nifs;
			__be32 uuid[11];
			__be32 ifaddr[32];
			__be32 netmask[32];
			__be32 mtu[32];
		} ia;
		struct /* Capabilities */ {
			__be32 capcount;
			__be32 caps[1];
		} cap;
	} reply;

	_enter("");

	nifs = 0;
	ifs = kcalloc(32, sizeof(*ifs), GFP_KERNEL);
	if (ifs) {
		nifs = afs_get_ipv4_interfaces(call->net, ifs, 32, false);
		if (nifs < 0) {
			kfree(ifs);
			ifs = NULL;
			nifs = 0;
		}
	}

	memset(&reply, 0, sizeof(reply));
	reply.ia.nifs = htonl(nifs);

	reply.ia.uuid[0] = call->net->uuid.time_low;
	reply.ia.uuid[1] = htonl(ntohs(call->net->uuid.time_mid));
	reply.ia.uuid[2] = htonl(ntohs(call->net->uuid.time_hi_and_version));
	reply.ia.uuid[3] = htonl((s8) call->net->uuid.clock_seq_hi_and_reserved);
	reply.ia.uuid[4] = htonl((s8) call->net->uuid.clock_seq_low);
	for (loop = 0; loop < 6; loop++)
		reply.ia.uuid[loop + 5] = htonl((s8) call->net->uuid.node[loop]);

	if (ifs) {
		for (loop = 0; loop < nifs; loop++) {
			reply.ia.ifaddr[loop] = ifs[loop].address.s_addr;
			reply.ia.netmask[loop] = ifs[loop].netmask.s_addr;
			reply.ia.mtu[loop] = htonl(ifs[loop].mtu);
		}
		kfree(ifs);
	}

	reply.cap.capcount = htonl(1);
	reply.cap.caps[0] = htonl(AFS_CAP_ERROR_TRANSLATION);
	afs_send_simple_reply(call, &reply, sizeof(reply));
	afs_put_call(call);
	_leave("");
}

/*
 * deliver request data to a CB.TellMeAboutYourself call
 */
static int afs_deliver_cb_tell_me_about_yourself(struct afs_call *call)
{
	int ret;

	_enter("");

	afs_extract_discard(call, 0);
	ret = afs_extract_data(call, false);
	if (ret < 0)
		return ret;

	if (!afs_check_call_state(call, AFS_CALL_SV_REPLYING))
		return afs_io_error(call, afs_io_error_cm_reply);
	return afs_find_cm_server_by_peer(call);
}

/*
 * deliver request data to a YFS CB.CallBack call
 */
static int afs_deliver_yfs_cb_callback(struct afs_call *call)
{
	struct afs_callback_break *cb;
	struct yfs_xdr_YFSFid *bp;
	size_t size;
	int ret, loop;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		afs_extract_to_tmp(call);
		call->unmarshall++;

		/* extract the FID array and its count in two steps */
		/* Fall through */
	case 1:
		_debug("extract FID count");
		ret = afs_extract_data(call, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("FID count: %u", call->count);
		if (call->count > YFSCBMAX)
			return afs_protocol_error(call, -EBADMSG,
						  afs_eproto_cb_fid_count);

		size = array_size(call->count, sizeof(struct yfs_xdr_YFSFid));
		call->buffer = kmalloc(size, GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		afs_extract_to_buf(call, size);
		call->unmarshall++;

		/* Fall through */
	case 2:
		_debug("extract FID array");
		ret = afs_extract_data(call, false);
		if (ret < 0)
			return ret;

		_debug("unmarshall FID array");
		call->request = kcalloc(call->count,
					sizeof(struct afs_callback_break),
					GFP_KERNEL);
		if (!call->request)
			return -ENOMEM;

		cb = call->request;
		bp = call->buffer;
		for (loop = call->count; loop > 0; loop--, cb++) {
			cb->fid.vid	= xdr_to_u64(bp->volume);
			cb->fid.vnode	= xdr_to_u64(bp->vnode.lo);
			cb->fid.vnode_hi = ntohl(bp->vnode.hi);
			cb->fid.unique	= ntohl(bp->vnode.unique);
			bp++;
		}

		afs_extract_to_tmp(call);
		call->unmarshall++;

	case 3:
		break;
	}

	if (!afs_check_call_state(call, AFS_CALL_SV_REPLYING))
		return afs_io_error(call, afs_io_error_cm_reply);

	/* We'll need the file server record as that tells us which set of
	 * vnodes to operate upon.
	 */
	return afs_find_cm_server_by_peer(call);
}
