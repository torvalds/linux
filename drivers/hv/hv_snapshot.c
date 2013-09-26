/*
 * An implementation of host initiated guest snapshot.
 *
 *
 * Copyright (C) 2013, Microsoft, Inc.
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/nls.h>
#include <linux/connector.h>
#include <linux/workqueue.h>
#include <linux/hyperv.h>

#define VSS_MAJOR  5
#define VSS_MINOR  0
#define VSS_MAJOR_MINOR    (VSS_MAJOR << 16 | VSS_MINOR)



/*
 * Global state maintained for transaction that is being processed.
 * Note that only one transaction can be active at any point in time.
 *
 * This state is set when we receive a request from the host; we
 * cleanup this state when the transaction is completed - when we respond
 * to the host with the key value.
 */

static struct {
	bool active; /* transaction status - active or not */
	int recv_len; /* number of bytes received. */
	struct vmbus_channel *recv_channel; /* chn we got the request */
	u64 recv_req_id; /* request ID. */
	struct hv_vss_msg  *msg; /* current message */
} vss_transaction;


static void vss_respond_to_host(int error);

static struct cb_id vss_id = { CN_VSS_IDX, CN_VSS_VAL };
static const char vss_name[] = "vss_kernel_module";
static __u8 *recv_buffer;

static void vss_send_op(struct work_struct *dummy);
static DECLARE_WORK(vss_send_op_work, vss_send_op);

/*
 * Callback when data is received from user mode.
 */

static void
vss_cn_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	struct hv_vss_msg *vss_msg;

	vss_msg = (struct hv_vss_msg *)msg->data;

	if (vss_msg->vss_hdr.operation == VSS_OP_REGISTER) {
		pr_info("VSS daemon registered\n");
		vss_transaction.active = false;
		if (vss_transaction.recv_channel != NULL)
			hv_vss_onchannelcallback(vss_transaction.recv_channel);
		return;

	}
	vss_respond_to_host(vss_msg->error);
}


static void vss_send_op(struct work_struct *dummy)
{
	int op = vss_transaction.msg->vss_hdr.operation;
	struct cn_msg *msg;
	struct hv_vss_msg *vss_msg;

	msg = kzalloc(sizeof(*msg) + sizeof(*vss_msg), GFP_ATOMIC);
	if (!msg)
		return;

	vss_msg = (struct hv_vss_msg *)msg->data;

	msg->id.idx =  CN_VSS_IDX;
	msg->id.val = CN_VSS_VAL;

	vss_msg->vss_hdr.operation = op;
	msg->len = sizeof(struct hv_vss_msg);

	cn_netlink_send(msg, 0, GFP_ATOMIC);
	kfree(msg);

	return;
}

/*
 * Send a response back to the host.
 */

static void
vss_respond_to_host(int error)
{
	struct icmsg_hdr *icmsghdrp;
	u32	buf_len;
	struct vmbus_channel *channel;
	u64	req_id;

	/*
	 * If a transaction is not active; log and return.
	 */

	if (!vss_transaction.active) {
		/*
		 * This is a spurious call!
		 */
		pr_warn("VSS: Transaction not active\n");
		return;
	}
	/*
	 * Copy the global state for completing the transaction. Note that
	 * only one transaction can be active at a time.
	 */

	buf_len = vss_transaction.recv_len;
	channel = vss_transaction.recv_channel;
	req_id = vss_transaction.recv_req_id;
	vss_transaction.active = false;

	icmsghdrp = (struct icmsg_hdr *)
			&recv_buffer[sizeof(struct vmbuspipe_hdr)];

	if (channel->onchannel_callback == NULL)
		/*
		 * We have raced with util driver being unloaded;
		 * silently return.
		 */
		return;

	icmsghdrp->status = error;

	icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION | ICMSGHDRFLAG_RESPONSE;

	vmbus_sendpacket(channel, recv_buffer, buf_len, req_id,
				VM_PKT_DATA_INBAND, 0);

}

/*
 * This callback is invoked when we get a VSS message from the host.
 * The host ensures that only one VSS transaction can be active at a time.
 */

void hv_vss_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u32 recvlen;
	u64 requestid;
	struct hv_vss_msg *vss_msg;


	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	if (vss_transaction.active) {
		/*
		 * We will defer processing this callback once
		 * the current transaction is complete.
		 */
		vss_transaction.recv_channel = channel;
		return;
	}

	vmbus_recvpacket(channel, recv_buffer, PAGE_SIZE * 2, &recvlen,
			 &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&recv_buffer[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			vmbus_prep_negotiate_resp(icmsghdrp, negop,
				 recv_buffer, UTIL_FW_MAJOR_MINOR,
				 VSS_MAJOR_MINOR);
		} else {
			vss_msg = (struct hv_vss_msg *)&recv_buffer[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			/*
			 * Stash away this global state for completing the
			 * transaction; note transactions are serialized.
			 */

			vss_transaction.recv_len = recvlen;
			vss_transaction.recv_channel = channel;
			vss_transaction.recv_req_id = requestid;
			vss_transaction.active = true;
			vss_transaction.msg = (struct hv_vss_msg *)vss_msg;

			switch (vss_msg->vss_hdr.operation) {
				/*
				 * Initiate a "freeze/thaw"
				 * operation in the guest.
				 * We respond to the host once
				 * the operation is complete.
				 *
				 * We send the message to the
				 * user space daemon and the
				 * operation is performed in
				 * the daemon.
				 */
			case VSS_OP_FREEZE:
			case VSS_OP_THAW:
				schedule_work(&vss_send_op_work);
				return;

			case VSS_OP_HOT_BACKUP:
				vss_msg->vss_cf.flags =
					 VSS_HBU_NO_AUTO_RECOVERY;
				vss_respond_to_host(0);
				return;

			case VSS_OP_GET_DM_INFO:
				vss_msg->dm_info.flags = 0;
				vss_respond_to_host(0);
				return;

			default:
				vss_respond_to_host(0);
				return;

			}

		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, recv_buffer,
				       recvlen, requestid,
				       VM_PKT_DATA_INBAND, 0);
	}

}

int
hv_vss_init(struct hv_util_service *srv)
{
	int err;

	err = cn_add_callback(&vss_id, vss_name, vss_cn_callback);
	if (err)
		return err;
	recv_buffer = srv->recv_buffer;

	/*
	 * When this driver loads, the user level daemon that
	 * processes the host requests may not yet be running.
	 * Defer processing channel callbacks until the daemon
	 * has registered.
	 */
	vss_transaction.active = true;
	return 0;
}

void hv_vss_deinit(void)
{
	cn_del_callback(&vss_id);
	cancel_work_sync(&vss_send_op_work);
}
