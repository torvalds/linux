/* AFS Cache Manager Service
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ip.h>
#include "internal.h"
#include "afs_cm.h"

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

#define CM_NAME(name) \
	const char afs_SRXCB##name##_name[] __tracepoint_string =	\
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
 * route an incoming cache manager call
 * - return T if supported, F if not
 */
bool afs_cm_incoming_call(struct afs_call *call)
{
	_enter("{CB.OP %u}", call->operation_ID);

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
	case CBTellMeAboutYourself:
		call->type = &afs_SRXCBTellMeAboutYourself;
		return true;
	default:
		return false;
	}
}

/*
 * clean up a cache manager call
 */
static void afs_cm_destructor(struct afs_call *call)
{
	_enter("");

	/* Break the callbacks here so that we do it after the final ACK is
	 * received.  The step number here must match the final number in
	 * afs_deliver_cb_callback().
	 */
	if (call->unmarshall == 5) {
		ASSERT(call->server && call->count && call->request);
		afs_break_callbacks(call->server, call->count, call->request);
	}

	afs_put_server(call->net, call->server);
	call->server = NULL;
	kfree(call->buffer);
	call->buffer = NULL;
}

/*
 * allow the fileserver to see if the cache manager is still alive
 */
static void SRXAFSCB_CallBack(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);

	_enter("");

	/* be sure to send the reply *before* attempting to spam the AFS server
	 * with FSFetchStatus requests on the vnodes with broken callbacks lest
	 * the AFS server get into a vicious cycle of trying to break further
	 * callbacks because it hadn't received completion of the CBCallBack op
	 * yet */
	afs_send_empty_reply(call);

	afs_break_callbacks(call->server, call->count, call->request);
	afs_put_call(call);
	_leave("");
}

/*
 * deliver request data to a CB.CallBack call
 */
static int afs_deliver_cb_callback(struct afs_call *call)
{
	struct sockaddr_rxrpc srx;
	struct afs_callback *cb;
	struct afs_server *server;
	__be32 *bp;
	int ret, loop;

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		rxrpc_kernel_get_peer(call->net->socket, call->rxcall, &srx);
		call->offset = 0;
		call->unmarshall++;

		/* extract the FID array and its count in two steps */
	case 1:
		_debug("extract FID count");
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		call->count = ntohl(call->tmp);
		_debug("FID count: %u", call->count);
		if (call->count > AFSCBMAX)
			return -EBADMSG;

		call->buffer = kmalloc(call->count * 3 * 4, GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		call->offset = 0;
		call->unmarshall++;

	case 2:
		_debug("extract FID array");
		ret = afs_extract_data(call, call->buffer,
				       call->count * 3 * 4, true);
		if (ret < 0)
			return ret;

		_debug("unmarshall FID array");
		call->request = kcalloc(call->count,
					sizeof(struct afs_callback),
					GFP_KERNEL);
		if (!call->request)
			return -ENOMEM;

		cb = call->request;
		bp = call->buffer;
		for (loop = call->count; loop > 0; loop--, cb++) {
			cb->fid.vid	= ntohl(*bp++);
			cb->fid.vnode	= ntohl(*bp++);
			cb->fid.unique	= ntohl(*bp++);
			cb->type	= AFSCM_CB_UNTYPED;
		}

		call->offset = 0;
		call->unmarshall++;

		/* extract the callback array and its count in two steps */
	case 3:
		_debug("extract CB count");
		ret = afs_extract_data(call, &call->tmp, 4, true);
		if (ret < 0)
			return ret;

		call->count2 = ntohl(call->tmp);
		_debug("CB count: %u", call->count2);
		if (call->count2 != call->count && call->count2 != 0)
			return -EBADMSG;
		call->offset = 0;
		call->unmarshall++;

	case 4:
		_debug("extract CB array");
		ret = afs_extract_data(call, call->buffer,
				       call->count2 * 3 * 4, false);
		if (ret < 0)
			return ret;

		_debug("unmarshall CB array");
		cb = call->request;
		bp = call->buffer;
		for (loop = call->count2; loop > 0; loop--, cb++) {
			cb->version	= ntohl(*bp++);
			cb->expiry	= ntohl(*bp++);
			cb->type	= ntohl(*bp++);
		}

		call->offset = 0;
		call->unmarshall++;

		/* Record that the message was unmarshalled successfully so
		 * that the call destructor can know do the callback breaking
		 * work, even if the final ACK isn't received.
		 *
		 * If the step number changes, then afs_cm_destructor() must be
		 * updated also.
		 */
		call->unmarshall++;
	case 5:
		break;
	}

	call->state = AFS_CALL_REPLYING;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	server = afs_find_server(call->net, &srx);
	if (!server)
		return -ENOTCONN;
	call->server = server;

	return afs_queue_call_work(call);
}

/*
 * allow the fileserver to request callback state (re-)initialisation
 */
static void SRXAFSCB_InitCallBackState(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);

	_enter("{%p}", call->server);

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
	struct sockaddr_rxrpc srx;
	struct afs_server *server;
	int ret;

	_enter("");

	rxrpc_kernel_get_peer(call->net->socket, call->rxcall, &srx);

	ret = afs_extract_data(call, NULL, 0, false);
	if (ret < 0)
		return ret;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	server = afs_find_server(call->net, &srx);
	if (!server)
		return -ENOTCONN;
	call->server = server;

	return afs_queue_call_work(call);
}

/*
 * deliver request data to a CB.InitCallBackState3 call
 */
static int afs_deliver_cb_init_call_back_state3(struct afs_call *call)
{
	struct sockaddr_rxrpc srx;
	struct afs_server *server;
	struct afs_uuid *r;
	unsigned loop;
	__be32 *b;
	int ret;

	_enter("");

	rxrpc_kernel_get_peer(call->net->socket, call->rxcall, &srx);

	_enter("{%u}", call->unmarshall);

	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->buffer = kmalloc(11 * sizeof(__be32), GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		call->unmarshall++;

	case 1:
		_debug("extract UUID");
		ret = afs_extract_data(call, call->buffer,
				       11 * sizeof(__be32), false);
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

		call->offset = 0;
		call->unmarshall++;

	case 2:
		break;
	}

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	server = afs_find_server(call->net, &srx);
	if (!server)
		return -ENOTCONN;
	call->server = server;

	return afs_queue_call_work(call);
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

	ret = afs_extract_data(call, NULL, 0, false);
	if (ret < 0)
		return ret;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	return afs_queue_call_work(call);
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
		call->offset = 0;
		call->buffer = kmalloc(11 * sizeof(__be32), GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		call->unmarshall++;

	case 1:
		_debug("extract UUID");
		ret = afs_extract_data(call, call->buffer,
				       11 * sizeof(__be32), false);
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
		r->time_low			= ntohl(b[0]);
		r->time_mid			= ntohl(b[1]);
		r->time_hi_and_version		= ntohl(b[2]);
		r->clock_seq_hi_and_reserved 	= ntohl(b[3]);
		r->clock_seq_low		= ntohl(b[4]);

		for (loop = 0; loop < 6; loop++)
			r->node[loop] = ntohl(b[loop + 5]);

		call->offset = 0;
		call->unmarshall++;

	case 2:
		break;
	}

	call->state = AFS_CALL_REPLYING;

	return afs_queue_call_work(call);
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
		nifs = afs_get_ipv4_interfaces(ifs, 32, false);
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

	ret = afs_extract_data(call, NULL, 0, false);
	if (ret < 0)
		return ret;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	return afs_queue_call_work(call);
}
