/*
 * An implementation of key value pair (KVP) functionality for Linux.
 *
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include <linux/net.h>
#include <linux/nls.h>
#include <linux/connector.h>
#include <linux/workqueue.h>

#include "logging.h"
#include "hv_api.h"
#include "vmbus.h"
#include "vmbus_packet_format.h"
#include "vmbus_channel_interface.h"
#include "version_info.h"
#include "channel.h"
#include "vmbus_private.h"
#include "vmbus_api.h"
#include "utils.h"
#include "hv_kvp.h"



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
} kvp_transaction;

static int kvp_send_key(int index);

static void kvp_respond_to_host(char *key, char *value, int error);
static void kvp_work_func(struct work_struct *dummy);
static void kvp_register(void);

static DECLARE_DELAYED_WORK(kvp_work, kvp_work_func);

static struct cb_id kvp_id = { CN_KVP_IDX, CN_KVP_VAL };
static const char kvp_name[] = "kvp_kernel_module";
static int timeout_fired;
static u8 *recv_buffer;
/*
 * Register the kernel component with the user-level daemon.
 * As part of this registration, pass the LIC version number.
 */

static void
kvp_register(void)
{

	struct cn_msg *msg;

	msg = kzalloc(sizeof(*msg) + strlen(HV_DRV_VERSION) + 1 , GFP_ATOMIC);

	if (msg) {
		msg->id.idx =  CN_KVP_IDX;
		msg->id.val = CN_KVP_VAL;
		msg->seq = KVP_REGISTER;
		strcpy(msg->data, HV_DRV_VERSION);
		msg->len = strlen(HV_DRV_VERSION) + 1;
		cn_netlink_send(msg, 0, GFP_ATOMIC);
		kfree(msg);
	}
}
static void
kvp_work_func(struct work_struct *dummy)
{
	/*
	 * If the timer fires, the user-mode component has not responded;
	 * process the pending transaction.
	 */
	kvp_respond_to_host("Unknown key", "Guest timed out", timeout_fired);
	timeout_fired = 1;
}

/*
 * Callback when data is received from user mode.
 */

static void
kvp_cn_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	struct hv_ku_msg *message;

	message = (struct hv_ku_msg *)msg->data;
	if (msg->seq == KVP_REGISTER) {
		printk(KERN_INFO "KVP: user-mode registering done.\n");
		kvp_register();
	}

	if (msg->seq == KVP_USER_SET) {
		/*
		 * Complete the transaction by forwarding the key value
		 * to the host. But first, cancel the timeout.
		 */
		if (cancel_delayed_work_sync(&kvp_work))
			kvp_respond_to_host(message->kvp_key,
						message->kvp_value,
						!strlen(message->kvp_key));
	}
}

static int
kvp_send_key(int index)
{
	struct cn_msg *msg;

	msg = kzalloc(sizeof(*msg) + sizeof(struct hv_kvp_msg) , GFP_ATOMIC);

	if (msg) {
		msg->id.idx =  CN_KVP_IDX;
		msg->id.val = CN_KVP_VAL;
		msg->seq = KVP_KERNEL_GET;
		((struct hv_ku_msg *)msg->data)->kvp_index = index;
		msg->len = sizeof(struct hv_ku_msg);
		cn_netlink_send(msg, 0, GFP_ATOMIC);
		kfree(msg);
		return 0;
	}
	return 1;
}

/*
 * Send a response back to the host.
 */

static void
kvp_respond_to_host(char *key, char *value, int error)
{
	struct hv_kvp_msg  *kvp_msg;
	struct hv_kvp_msg_enumerate  *kvp_data;
	char	*key_name;
	struct icmsg_hdr *icmsghdrp;
	int	keylen, valuelen;
	u32	buf_len;
	struct vmbus_channel *channel;
	u64	req_id;

	/*
	 * If a transaction is not active; log and return.
	 */

	if (!kvp_transaction.active) {
		/*
		 * This is a spurious call!
		 */
		printk(KERN_WARNING "KVP: Transaction not active\n");
		return;
	}
	/*
	 * Copy the global state for completing the transaction. Note that
	 * only one transaction can be active at a time.
	 */

	buf_len = kvp_transaction.recv_len;
	channel = kvp_transaction.recv_channel;
	req_id = kvp_transaction.recv_req_id;

	icmsghdrp = (struct icmsg_hdr *)
			&recv_buffer[sizeof(struct vmbuspipe_hdr)];
	kvp_msg = (struct hv_kvp_msg *)
			&recv_buffer[sizeof(struct vmbuspipe_hdr) +
			sizeof(struct icmsg_hdr)];
	kvp_data = &kvp_msg->kvp_data;
	key_name = key;

	/*
	 * If the error parameter is set, terminate the host's enumeration.
	 */
	if (error) {
		/*
		 * We don't support this index or the we have timedout;
		 * terminate the host-side iteration by returning an error.
		 */
		icmsghdrp->status = HV_E_FAIL;
		goto response_done;
	}

	/*
	 * The windows host expects the key/value pair to be encoded
	 * in utf16.
	 */
	keylen = utf8s_to_utf16s(key_name, strlen(key_name),
				(wchar_t *)kvp_data->data.key);
	kvp_data->data.key_size = 2*(keylen + 1); /* utf16 encoding */
	valuelen = utf8s_to_utf16s(value, strlen(value),
				(wchar_t *)kvp_data->data.value);
	kvp_data->data.value_size = 2*(valuelen + 1); /* utf16 encoding */

	kvp_data->data.value_type = REG_SZ; /* all our values are strings */
	icmsghdrp->status = HV_S_OK;

response_done:
	icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION | ICMSGHDRFLAG_RESPONSE;

	vmbus_sendpacket(channel, recv_buffer, buf_len, req_id,
				VM_PKT_DATA_INBAND, 0);

	kvp_transaction.active = false;
}

/*
 * This callback is invoked when we get a KVP message from the host.
 * The host ensures that only one KVP transaction can be active at a time.
 * KVP implementation in Linux needs to forward the key to a user-mde
 * component to retrive the corresponding value. Consequently, we cannot
 * respond to the host in the conext of this callback. Since the host
 * guarantees that at most only one transaction can be active at a time,
 * we stash away the transaction state in a set of global variables.
 */

void hv_kvp_onchannelcallback(void *context)
{
	struct vmbus_channel *channel = context;
	u32 recvlen;
	u64 requestid;

	struct hv_kvp_msg *kvp_msg;
	struct hv_kvp_msg_enumerate *kvp_data;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;


	if (kvp_transaction.active)
		return;


	vmbus_recvpacket(channel, recv_buffer, PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		DPRINT_DBG(VMBUS, "KVP packet: len=%d, requestid=%lld",
			   recvlen, requestid);

		icmsghdrp = (struct icmsg_hdr *)&recv_buffer[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			prep_negotiate_resp(icmsghdrp, negop, recv_buffer);
		} else {
			kvp_msg = (struct hv_kvp_msg *)&recv_buffer[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			kvp_data = &kvp_msg->kvp_data;

			/*
			 * We only support the "get" operation on
			 * "KVP_POOL_AUTO" pool.
			 */

			if ((kvp_msg->kvp_hdr.pool != KVP_POOL_AUTO) ||
				(kvp_msg->kvp_hdr.operation !=
				KVP_OP_ENUMERATE)) {
				icmsghdrp->status = HV_E_FAIL;
				goto callback_done;
			}

			/*
			 * Stash away this global state for completing the
			 * transaction; note transactions are serialized.
			 */
			kvp_transaction.recv_len = recvlen;
			kvp_transaction.recv_channel = channel;
			kvp_transaction.recv_req_id = requestid;
			kvp_transaction.active = true;

			/*
			 * Get the information from the
			 * user-mode component.
			 * component. This transaction will be
			 * completed when we get the value from
			 * the user-mode component.
			 * Set a timeout to deal with
			 * user-mode not responding.
			 */
			kvp_send_key(kvp_data->index);
			schedule_delayed_work(&kvp_work, 100);

			return;

		}

callback_done:

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, recv_buffer,
				       recvlen, requestid,
				       VM_PKT_DATA_INBAND, 0);
	}

}

int
hv_kvp_init(void)
{
	int err;

	err = cn_add_callback(&kvp_id, kvp_name, kvp_cn_callback);
	if (err)
		return err;
	recv_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!recv_buffer)
		return -ENOMEM;

	return 0;
}

void hv_kvp_deinit(void)
{
	cn_del_callback(&kvp_id);
	cancel_delayed_work_sync(&kvp_work);
	kfree(recv_buffer);
}
