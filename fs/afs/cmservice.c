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

#if 0
struct workqueue_struct *afs_cm_workqueue;
#endif  /*  0  */

static int afs_deliver_cb_init_call_back_state(struct afs_call *,
					       struct sk_buff *, bool);
static int afs_deliver_cb_init_call_back_state3(struct afs_call *,
						struct sk_buff *, bool);
static int afs_deliver_cb_probe(struct afs_call *, struct sk_buff *, bool);
static int afs_deliver_cb_callback(struct afs_call *, struct sk_buff *, bool);
static int afs_deliver_cb_probe_uuid(struct afs_call *, struct sk_buff *, bool);
static int afs_deliver_cb_tell_me_about_yourself(struct afs_call *,
						 struct sk_buff *, bool);
static void afs_cm_destructor(struct afs_call *);

/*
 * CB.CallBack operation type
 */
static const struct afs_call_type afs_SRXCBCallBack = {
	.name		= "CB.CallBack",
	.deliver	= afs_deliver_cb_callback,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_cm_destructor,
};

/*
 * CB.InitCallBackState operation type
 */
static const struct afs_call_type afs_SRXCBInitCallBackState = {
	.name		= "CB.InitCallBackState",
	.deliver	= afs_deliver_cb_init_call_back_state,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_cm_destructor,
};

/*
 * CB.InitCallBackState3 operation type
 */
static const struct afs_call_type afs_SRXCBInitCallBackState3 = {
	.name		= "CB.InitCallBackState3",
	.deliver	= afs_deliver_cb_init_call_back_state3,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_cm_destructor,
};

/*
 * CB.Probe operation type
 */
static const struct afs_call_type afs_SRXCBProbe = {
	.name		= "CB.Probe",
	.deliver	= afs_deliver_cb_probe,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_cm_destructor,
};

/*
 * CB.ProbeUuid operation type
 */
static const struct afs_call_type afs_SRXCBProbeUuid = {
	.name		= "CB.ProbeUuid",
	.deliver	= afs_deliver_cb_probe_uuid,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_cm_destructor,
};

/*
 * CB.TellMeAboutYourself operation type
 */
static const struct afs_call_type afs_SRXCBTellMeAboutYourself = {
	.name		= "CB.TellMeAboutYourself",
	.deliver	= afs_deliver_cb_tell_me_about_yourself,
	.abort_to_error	= afs_abort_to_error,
	.destructor	= afs_cm_destructor,
};

/*
 * route an incoming cache manager call
 * - return T if supported, F if not
 */
bool afs_cm_incoming_call(struct afs_call *call)
{
	u32 operation_id = ntohl(call->operation_ID);

	_enter("{CB.OP %u}", operation_id);

	switch (operation_id) {
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

	afs_put_server(call->server);
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
	_leave("");
}

/*
 * deliver request data to a CB.CallBack call
 */
static int afs_deliver_cb_callback(struct afs_call *call, struct sk_buff *skb,
				   bool last)
{
	struct afs_callback *cb;
	struct afs_server *server;
	struct in_addr addr;
	__be32 *bp;
	u32 tmp;
	int ret, loop;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->unmarshall++;

		/* extract the FID array and its count in two steps */
	case 1:
		_debug("extract FID count");
		ret = afs_extract_data(call, skb, last, &call->tmp, 4);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

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
		ret = afs_extract_data(call, skb, last, call->buffer,
				       call->count * 3 * 4);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

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
		ret = afs_extract_data(call, skb, last, &call->tmp, 4);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		tmp = ntohl(call->tmp);
		_debug("CB count: %u", tmp);
		if (tmp != call->count && tmp != 0)
			return -EBADMSG;
		call->offset = 0;
		call->unmarshall++;
		if (tmp == 0)
			goto empty_cb_array;

	case 4:
		_debug("extract CB array");
		ret = afs_extract_data(call, skb, last, call->request,
				       call->count * 3 * 4);
		switch (ret) {
		case 0:		break;
		case -EAGAIN:	return 0;
		default:	return ret;
		}

		_debug("unmarshall CB array");
		cb = call->request;
		bp = call->buffer;
		for (loop = call->count; loop > 0; loop--, cb++) {
			cb->version	= ntohl(*bp++);
			cb->expiry	= ntohl(*bp++);
			cb->type	= ntohl(*bp++);
		}

	empty_cb_array:
		call->offset = 0;
		call->unmarshall++;

	case 5:
		_debug("trailer");
		if (skb->len != 0)
			return -EBADMSG;
		break;
	}

	if (!last)
		return 0;

	call->state = AFS_CALL_REPLYING;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	memcpy(&addr, &ip_hdr(skb)->saddr, 4);
	server = afs_find_server(&addr);
	if (!server)
		return -ENOTCONN;
	call->server = server;

	INIT_WORK(&call->work, SRXAFSCB_CallBack);
	schedule_work(&call->work);
	return 0;
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
	_leave("");
}

/*
 * deliver request data to a CB.InitCallBackState call
 */
static int afs_deliver_cb_init_call_back_state(struct afs_call *call,
					       struct sk_buff *skb,
					       bool last)
{
	struct afs_server *server;
	struct in_addr addr;

	_enter(",{%u},%d", skb->len, last);

	if (skb->len > 0)
		return -EBADMSG;
	if (!last)
		return 0;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	memcpy(&addr, &ip_hdr(skb)->saddr, 4);
	server = afs_find_server(&addr);
	if (!server)
		return -ENOTCONN;
	call->server = server;

	INIT_WORK(&call->work, SRXAFSCB_InitCallBackState);
	schedule_work(&call->work);
	return 0;
}

/*
 * deliver request data to a CB.InitCallBackState3 call
 */
static int afs_deliver_cb_init_call_back_state3(struct afs_call *call,
						struct sk_buff *skb,
						bool last)
{
	struct afs_server *server;
	struct in_addr addr;

	_enter(",{%u},%d", skb->len, last);

	if (!last)
		return 0;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	/* we'll need the file server record as that tells us which set of
	 * vnodes to operate upon */
	memcpy(&addr, &ip_hdr(skb)->saddr, 4);
	server = afs_find_server(&addr);
	if (!server)
		return -ENOTCONN;
	call->server = server;

	INIT_WORK(&call->work, SRXAFSCB_InitCallBackState);
	schedule_work(&call->work);
	return 0;
}

/*
 * allow the fileserver to see if the cache manager is still alive
 */
static void SRXAFSCB_Probe(struct work_struct *work)
{
	struct afs_call *call = container_of(work, struct afs_call, work);

	_enter("");
	afs_send_empty_reply(call);
	_leave("");
}

/*
 * deliver request data to a CB.Probe call
 */
static int afs_deliver_cb_probe(struct afs_call *call, struct sk_buff *skb,
				bool last)
{
	_enter(",{%u},%d", skb->len, last);

	if (skb->len > 0)
		return -EBADMSG;
	if (!last)
		return 0;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	INIT_WORK(&call->work, SRXAFSCB_Probe);
	schedule_work(&call->work);
	return 0;
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


	if (memcmp(r, &afs_uuid, sizeof(afs_uuid)) == 0)
		reply.match = htonl(0);
	else
		reply.match = htonl(1);

	afs_send_simple_reply(call, &reply, sizeof(reply));
	_leave("");
}

/*
 * deliver request data to a CB.ProbeUuid call
 */
static int afs_deliver_cb_probe_uuid(struct afs_call *call, struct sk_buff *skb,
				     bool last)
{
	struct afs_uuid *r;
	unsigned loop;
	__be32 *b;
	int ret;

	_enter("{%u},{%u},%d", call->unmarshall, skb->len, last);

	if (skb->len > 0)
		return -EBADMSG;
	if (!last)
		return 0;

	switch (call->unmarshall) {
	case 0:
		call->offset = 0;
		call->buffer = kmalloc(11 * sizeof(__be32), GFP_KERNEL);
		if (!call->buffer)
			return -ENOMEM;
		call->unmarshall++;

	case 1:
		_debug("extract UUID");
		ret = afs_extract_data(call, skb, last, call->buffer,
				       11 * sizeof(__be32));
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
		_debug("trailer");
		if (skb->len != 0)
			return -EBADMSG;
		break;
	}

	if (!last)
		return 0;

	call->state = AFS_CALL_REPLYING;

	INIT_WORK(&call->work, SRXAFSCB_ProbeUuid);
	schedule_work(&call->work);
	return 0;
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

	reply.ia.uuid[0] = htonl(afs_uuid.time_low);
	reply.ia.uuid[1] = htonl(afs_uuid.time_mid);
	reply.ia.uuid[2] = htonl(afs_uuid.time_hi_and_version);
	reply.ia.uuid[3] = htonl((s8) afs_uuid.clock_seq_hi_and_reserved);
	reply.ia.uuid[4] = htonl((s8) afs_uuid.clock_seq_low);
	for (loop = 0; loop < 6; loop++)
		reply.ia.uuid[loop + 5] = htonl((s8) afs_uuid.node[loop]);

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

	_leave("");
}

/*
 * deliver request data to a CB.TellMeAboutYourself call
 */
static int afs_deliver_cb_tell_me_about_yourself(struct afs_call *call,
						 struct sk_buff *skb, bool last)
{
	_enter(",{%u},%d", skb->len, last);

	if (skb->len > 0)
		return -EBADMSG;
	if (!last)
		return 0;

	/* no unmarshalling required */
	call->state = AFS_CALL_REPLYING;

	INIT_WORK(&call->work, SRXAFSCB_TellMeAboutYourself);
	schedule_work(&call->work);
	return 0;
}
