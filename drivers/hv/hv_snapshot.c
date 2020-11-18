// SPDX-License-Identifier: GPL-2.0-only
/*
 * An implementation of host initiated guest snapshot.
 *
 * Copyright (C) 2013, Microsoft, Inc.
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/nls.h>
#include <linux/connector.h>
#include <linux/workqueue.h>
#include <linux/hyperv.h>
#include <asm/hyperv-tlfs.h>

#include "hyperv_vmbus.h"
#include "hv_utils_transport.h"

#define VSS_MAJOR  5
#define VSS_MINOR  0
#define VSS_VERSION    (VSS_MAJOR << 16 | VSS_MINOR)

#define VSS_VER_COUNT 1
static const int vss_versions[] = {
	VSS_VERSION
};

#define FW_VER_COUNT 1
static const int fw_versions[] = {
	UTIL_FW_VERSION
};

/*
 * Timeout values are based on expecations from host
 */
#define VSS_FREEZE_TIMEOUT (15 * 60)

/*
 * Global state maintained for transaction that is being processed. For a class
 * of integration services, including the "VSS service", the specified protocol
 * is a "request/response" protocol which means that there can only be single
 * outstanding transaction from the host at any given point in time. We use
 * this to simplify memory management in this driver - we cache and process
 * only one message at a time.
 *
 * While the request/response protocol is guaranteed by the host, we further
 * ensure this by serializing packet processing in this driver - we do not
 * read additional packets from the VMBUs until the current packet is fully
 * handled.
 */

static struct {
	int state;   /* hvutil_device_state */
	int recv_len; /* number of bytes received. */
	struct vmbus_channel *recv_channel; /* chn we got the request */
	u64 recv_req_id; /* request ID. */
	struct hv_vss_msg  *msg; /* current message */
} vss_transaction;


static void vss_respond_to_host(int error);

/*
 * This state maintains the version number registered by the daemon.
 */
static int dm_reg_value;

static const char vss_devname[] = "vmbus/hv_vss";
static __u8 *recv_buffer;
static struct hvutil_transport *hvt;

static void vss_timeout_func(struct work_struct *dummy);
static void vss_handle_request(struct work_struct *dummy);

static DECLARE_DELAYED_WORK(vss_timeout_work, vss_timeout_func);
static DECLARE_WORK(vss_handle_request_work, vss_handle_request);

static void vss_poll_wrapper(void *channel)
{
	/* Transaction is finished, reset the state here to avoid races. */
	vss_transaction.state = HVUTIL_READY;
	tasklet_schedule(&((struct vmbus_channel *)channel)->callback_event);
}

/*
 * Callback when data is received from user mode.
 */

static void vss_timeout_func(struct work_struct *dummy)
{
	/*
	 * Timeout waiting for userspace component to reply happened.
	 */
	pr_warn("VSS: timeout waiting for daemon to reply\n");
	vss_respond_to_host(HV_E_FAIL);

	hv_poll_channel(vss_transaction.recv_channel, vss_poll_wrapper);
}

static void vss_register_done(void)
{
	hv_poll_channel(vss_transaction.recv_channel, vss_poll_wrapper);
	pr_debug("VSS: userspace daemon registered\n");
}

static int vss_handle_handshake(struct hv_vss_msg *vss_msg)
{
	u32 our_ver = VSS_OP_REGISTER1;

	switch (vss_msg->vss_hdr.operation) {
	case VSS_OP_REGISTER:
		/* Daemon doesn't expect us to reply */
		dm_reg_value = VSS_OP_REGISTER;
		break;
	case VSS_OP_REGISTER1:
		/* Daemon expects us to reply with our own version */
		if (hvutil_transport_send(hvt, &our_ver, sizeof(our_ver),
					  vss_register_done))
			return -EFAULT;
		dm_reg_value = VSS_OP_REGISTER1;
		break;
	default:
		return -EINVAL;
	}
	pr_info("VSS: userspace daemon ver. %d connected\n", dm_reg_value);
	return 0;
}

static int vss_on_msg(void *msg, int len)
{
	struct hv_vss_msg *vss_msg = (struct hv_vss_msg *)msg;

	if (len != sizeof(*vss_msg)) {
		pr_debug("VSS: Message size does not match length\n");
		return -EINVAL;
	}

	if (vss_msg->vss_hdr.operation == VSS_OP_REGISTER ||
	    vss_msg->vss_hdr.operation == VSS_OP_REGISTER1) {
		/*
		 * Don't process registration messages if we're in the middle
		 * of a transaction processing.
		 */
		if (vss_transaction.state > HVUTIL_READY) {
			pr_debug("VSS: Got unexpected registration request\n");
			return -EINVAL;
		}

		return vss_handle_handshake(vss_msg);
	} else if (vss_transaction.state == HVUTIL_USERSPACE_REQ) {
		vss_transaction.state = HVUTIL_USERSPACE_RECV;

		if (vss_msg->vss_hdr.operation == VSS_OP_HOT_BACKUP)
			vss_transaction.msg->vss_cf.flags =
				VSS_HBU_NO_AUTO_RECOVERY;

		if (cancel_delayed_work_sync(&vss_timeout_work)) {
			vss_respond_to_host(vss_msg->error);
			/* Transaction is finished, reset the state. */
			hv_poll_channel(vss_transaction.recv_channel,
					vss_poll_wrapper);
		}
	} else {
		/* This is a spurious call! */
		pr_debug("VSS: Transaction not active\n");
		return -EINVAL;
	}
	return 0;
}

static void vss_send_op(void)
{
	int op = vss_transaction.msg->vss_hdr.operation;
	int rc;
	struct hv_vss_msg *vss_msg;

	/* The transaction state is wrong. */
	if (vss_transaction.state != HVUTIL_HOSTMSG_RECEIVED) {
		pr_debug("VSS: Unexpected attempt to send to daemon\n");
		return;
	}

	vss_msg = kzalloc(sizeof(*vss_msg), GFP_KERNEL);
	if (!vss_msg)
		return;

	vss_msg->vss_hdr.operation = op;

	vss_transaction.state = HVUTIL_USERSPACE_REQ;

	schedule_delayed_work(&vss_timeout_work, op == VSS_OP_FREEZE ?
			VSS_FREEZE_TIMEOUT * HZ : HV_UTIL_TIMEOUT * HZ);

	rc = hvutil_transport_send(hvt, vss_msg, sizeof(*vss_msg), NULL);
	if (rc) {
		pr_warn("VSS: failed to communicate to the daemon: %d\n", rc);
		if (cancel_delayed_work_sync(&vss_timeout_work)) {
			vss_respond_to_host(HV_E_FAIL);
			vss_transaction.state = HVUTIL_READY;
		}
	}

	kfree(vss_msg);
}

static void vss_handle_request(struct work_struct *dummy)
{
	switch (vss_transaction.msg->vss_hdr.operation) {
	/*
	 * Initiate a "freeze/thaw" operation in the guest.
	 * We respond to the host once the operation is complete.
	 *
	 * We send the message to the user space daemon and the operation is
	 * performed in the daemon.
	 */
	case VSS_OP_THAW:
	case VSS_OP_FREEZE:
	case VSS_OP_HOT_BACKUP:
		if (vss_transaction.state < HVUTIL_READY) {
			/* Userspace is not registered yet */
			pr_debug("VSS: Not ready for request.\n");
			vss_respond_to_host(HV_E_FAIL);
			return;
		}

		pr_debug("VSS: Received request for op code: %d\n",
			vss_transaction.msg->vss_hdr.operation);
		vss_transaction.state = HVUTIL_HOSTMSG_RECEIVED;
		vss_send_op();
		return;
	case VSS_OP_GET_DM_INFO:
		vss_transaction.msg->dm_info.flags = 0;
		break;
	default:
		break;
	}

	vss_respond_to_host(0);
	hv_poll_channel(vss_transaction.recv_channel, vss_poll_wrapper);
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
	 * Copy the global state for completing the transaction. Note that
	 * only one transaction can be active at a time.
	 */

	buf_len = vss_transaction.recv_len;
	channel = vss_transaction.recv_channel;
	req_id = vss_transaction.recv_req_id;

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
	int vss_srv_version;

	struct icmsg_hdr *icmsghdrp;

	if (vss_transaction.state > HVUTIL_READY)
		return;

	vmbus_recvpacket(channel, recv_buffer, HV_HYP_PAGE_SIZE * 2, &recvlen,
			 &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&recv_buffer[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			if (vmbus_prep_negotiate_resp(icmsghdrp,
				 recv_buffer, fw_versions, FW_VER_COUNT,
				 vss_versions, VSS_VER_COUNT,
				 NULL, &vss_srv_version)) {

				pr_info("VSS IC version %d.%d\n",
					vss_srv_version >> 16,
					vss_srv_version & 0xFFFF);
			}
		} else {
			vss_msg = (struct hv_vss_msg *)&recv_buffer[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			/*
			 * Stash away this global state for completing the
			 * transaction; note transactions are serialized.
			 */

			vss_transaction.recv_len = recvlen;
			vss_transaction.recv_req_id = requestid;
			vss_transaction.msg = (struct hv_vss_msg *)vss_msg;

			schedule_work(&vss_handle_request_work);
			return;
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, recv_buffer,
				       recvlen, requestid,
				       VM_PKT_DATA_INBAND, 0);
	}

}

static void vss_on_reset(void)
{
	if (cancel_delayed_work_sync(&vss_timeout_work))
		vss_respond_to_host(HV_E_FAIL);
	vss_transaction.state = HVUTIL_DEVICE_INIT;
}

int
hv_vss_init(struct hv_util_service *srv)
{
	if (vmbus_proto_version < VERSION_WIN8_1) {
		pr_warn("Integration service 'Backup (volume snapshot)'"
			" not supported on this host version.\n");
		return -ENOTSUPP;
	}
	recv_buffer = srv->recv_buffer;
	vss_transaction.recv_channel = srv->channel;

	/*
	 * When this driver loads, the user level daemon that
	 * processes the host requests may not yet be running.
	 * Defer processing channel callbacks until the daemon
	 * has registered.
	 */
	vss_transaction.state = HVUTIL_DEVICE_INIT;

	hvt = hvutil_transport_init(vss_devname, CN_VSS_IDX, CN_VSS_VAL,
				    vss_on_msg, vss_on_reset);
	if (!hvt) {
		pr_warn("VSS: Failed to initialize transport\n");
		return -EFAULT;
	}

	return 0;
}

static void hv_vss_cancel_work(void)
{
	cancel_delayed_work_sync(&vss_timeout_work);
	cancel_work_sync(&vss_handle_request_work);
}

int hv_vss_pre_suspend(void)
{
	struct vmbus_channel *channel = vss_transaction.recv_channel;
	struct hv_vss_msg *vss_msg;

	/*
	 * Fake a THAW message for the user space daemon in case the daemon
	 * has frozen the file systems. It doesn't matter if there is already
	 * a message pending to be delivered to the user space since we force
	 * vss_transaction.state to be HVUTIL_READY, so the user space daemon's
	 * write() will fail with EINVAL (see vss_on_msg()), and the daemon
	 * will reset the device by closing and re-opening it.
	 */
	vss_msg = kzalloc(sizeof(*vss_msg), GFP_KERNEL);
	if (!vss_msg)
		return -ENOMEM;

	tasklet_disable(&channel->callback_event);

	vss_msg->vss_hdr.operation = VSS_OP_THAW;

	/* Cancel any possible pending work. */
	hv_vss_cancel_work();

	/* We don't care about the return value. */
	hvutil_transport_send(hvt, vss_msg, sizeof(*vss_msg), NULL);

	kfree(vss_msg);

	vss_transaction.state = HVUTIL_READY;

	/* tasklet_enable() will be called in hv_vss_pre_resume(). */
	return 0;
}

int hv_vss_pre_resume(void)
{
	struct vmbus_channel *channel = vss_transaction.recv_channel;

	tasklet_enable(&channel->callback_event);

	return 0;
}

void hv_vss_deinit(void)
{
	vss_transaction.state = HVUTIL_DEVICE_DYING;

	hv_vss_cancel_work();

	hvutil_transport_destroy(hvt);
}
