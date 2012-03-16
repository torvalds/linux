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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/nls.h>
#include <linux/connector.h>
#include <linux/workqueue.h>
#include <linux/hyperv.h>



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
	struct hv_kvp_msg  *kvp_msg; /* current message */
	struct vmbus_channel *recv_channel; /* chn we got the request */
	u64 recv_req_id; /* request ID. */
	void *kvp_context; /* for the channel callback */
} kvp_transaction;

static void kvp_send_key(struct work_struct *dummy);

#define TIMEOUT_FIRED 1

static void kvp_respond_to_host(char *key, char *value, int error);
static void kvp_work_func(struct work_struct *dummy);
static void kvp_register(void);

static DECLARE_DELAYED_WORK(kvp_work, kvp_work_func);
static DECLARE_WORK(kvp_sendkey_work, kvp_send_key);

static struct cb_id kvp_id = { CN_KVP_IDX, CN_KVP_VAL };
static const char kvp_name[] = "kvp_kernel_module";
static u8 *recv_buffer;
/*
 * Register the kernel component with the user-level daemon.
 * As part of this registration, pass the LIC version number.
 */

static void
kvp_register(void)
{

	struct cn_msg *msg;
	struct hv_kvp_msg *kvp_msg;
	char *version;

	msg = kzalloc(sizeof(*msg) + sizeof(struct hv_kvp_msg), GFP_ATOMIC);

	if (msg) {
		kvp_msg = (struct hv_kvp_msg *)msg->data;
		version = kvp_msg->body.kvp_register.version;
		msg->id.idx =  CN_KVP_IDX;
		msg->id.val = CN_KVP_VAL;

		kvp_msg->kvp_hdr.operation = KVP_OP_REGISTER;
		strcpy(version, HV_DRV_VERSION);
		msg->len = sizeof(struct hv_kvp_msg);
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
	kvp_respond_to_host("Unknown key", "Guest timed out", TIMEOUT_FIRED);
}

/*
 * Callback when data is received from user mode.
 */

static void
kvp_cn_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	struct hv_kvp_msg *message;
	struct hv_kvp_msg_enumerate *data;

	message = (struct hv_kvp_msg *)msg->data;
	switch (message->kvp_hdr.operation) {
	case KVP_OP_REGISTER:
		pr_info("KVP: user-mode registering done.\n");
		kvp_register();
		kvp_transaction.active = false;
		hv_kvp_onchannelcallback(kvp_transaction.kvp_context);
		break;

	default:
		data = &message->body.kvp_enum_data;
		/*
		 * Complete the transaction by forwarding the key value
		 * to the host. But first, cancel the timeout.
		 */
		if (cancel_delayed_work_sync(&kvp_work))
			kvp_respond_to_host(data->data.key,
					 data->data.value,
					!strlen(data->data.key));
	}
}

static void
kvp_send_key(struct work_struct *dummy)
{
	struct cn_msg *msg;
	struct hv_kvp_msg *message;
	struct hv_kvp_msg *in_msg;
	__u8 operation = kvp_transaction.kvp_msg->kvp_hdr.operation;
	__u8 pool = kvp_transaction.kvp_msg->kvp_hdr.pool;
	__u32 val32;
	__u64 val64;

	msg = kzalloc(sizeof(*msg) + sizeof(struct hv_kvp_msg) , GFP_ATOMIC);
	if (!msg)
		return;

	msg->id.idx =  CN_KVP_IDX;
	msg->id.val = CN_KVP_VAL;

	message = (struct hv_kvp_msg *)msg->data;
	message->kvp_hdr.operation = operation;
	message->kvp_hdr.pool = pool;
	in_msg = kvp_transaction.kvp_msg;

	/*
	 * The key/value strings sent from the host are encoded in
	 * in utf16; convert it to utf8 strings.
	 * The host assures us that the utf16 strings will not exceed
	 * the max lengths specified. We will however, reserve room
	 * for the string terminating character - in the utf16s_utf8s()
	 * function we limit the size of the buffer where the converted
	 * string is placed to HV_KVP_EXCHANGE_MAX_*_SIZE -1 to gaurantee
	 * that the strings can be properly terminated!
	 */

	switch (message->kvp_hdr.operation) {
	case KVP_OP_SET:
		switch (in_msg->body.kvp_set.data.value_type) {
		case REG_SZ:
			/*
			 * The value is a string - utf16 encoding.
			 */
			message->body.kvp_set.data.value_size =
				utf16s_to_utf8s(
				(wchar_t *)in_msg->body.kvp_set.data.value,
				in_msg->body.kvp_set.data.value_size,
				UTF16_LITTLE_ENDIAN,
				message->body.kvp_set.data.value,
				HV_KVP_EXCHANGE_MAX_VALUE_SIZE - 1) + 1;
				break;

		case REG_U32:
			/*
			 * The value is a 32 bit scalar.
			 * We save this as a utf8 string.
			 */
			val32 = in_msg->body.kvp_set.data.value_u32;
			message->body.kvp_set.data.value_size =
				sprintf(message->body.kvp_set.data.value,
					"%d", val32) + 1;
			break;

		case REG_U64:
			/*
			 * The value is a 64 bit scalar.
			 * We save this as a utf8 string.
			 */
			val64 = in_msg->body.kvp_set.data.value_u64;
			message->body.kvp_set.data.value_size =
				sprintf(message->body.kvp_set.data.value,
					"%llu", val64) + 1;
			break;

		}
	case KVP_OP_GET:
		message->body.kvp_set.data.key_size =
			utf16s_to_utf8s(
			(wchar_t *)in_msg->body.kvp_set.data.key,
			in_msg->body.kvp_set.data.key_size,
			UTF16_LITTLE_ENDIAN,
			message->body.kvp_set.data.key,
			HV_KVP_EXCHANGE_MAX_KEY_SIZE - 1) + 1;
			break;

	case KVP_OP_DELETE:
		message->body.kvp_delete.key_size =
			utf16s_to_utf8s(
			(wchar_t *)in_msg->body.kvp_delete.key,
			in_msg->body.kvp_delete.key_size,
			UTF16_LITTLE_ENDIAN,
			message->body.kvp_delete.key,
			HV_KVP_EXCHANGE_MAX_KEY_SIZE - 1) + 1;
			break;

	case KVP_OP_ENUMERATE:
		message->body.kvp_enum_data.index =
			in_msg->body.kvp_enum_data.index;
			break;
	}

	msg->len = sizeof(struct hv_kvp_msg);
	cn_netlink_send(msg, 0, GFP_ATOMIC);
	kfree(msg);

	return;
}

/*
 * Send a response back to the host.
 */

static void
kvp_respond_to_host(char *key, char *value, int error)
{
	struct hv_kvp_msg  *kvp_msg;
	struct hv_kvp_exchg_msg_value  *kvp_data;
	char	*key_name;
	struct icmsg_hdr *icmsghdrp;
	int	keylen = 0;
	int	valuelen = 0;
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
		pr_warn("KVP: Transaction not active\n");
		return;
	}
	/*
	 * Copy the global state for completing the transaction. Note that
	 * only one transaction can be active at a time.
	 */

	buf_len = kvp_transaction.recv_len;
	channel = kvp_transaction.recv_channel;
	req_id = kvp_transaction.recv_req_id;

	kvp_transaction.active = false;

	icmsghdrp = (struct icmsg_hdr *)
			&recv_buffer[sizeof(struct vmbuspipe_hdr)];

	if (channel->onchannel_callback == NULL)
		/*
		 * We have raced with util driver being unloaded;
		 * silently return.
		 */
		return;


	/*
	 * If the error parameter is set, terminate the host's enumeration.
	 */
	if (error) {
		/*
		 * Something failed or the we have timedout;
		 * terminate the host-side iteration by returning an error.
		 */
		icmsghdrp->status = HV_E_FAIL;
		goto response_done;
	}

	icmsghdrp->status = HV_S_OK;

	kvp_msg = (struct hv_kvp_msg *)
			&recv_buffer[sizeof(struct vmbuspipe_hdr) +
			sizeof(struct icmsg_hdr)];

	switch (kvp_transaction.kvp_msg->kvp_hdr.operation) {
	case KVP_OP_GET:
		kvp_data = &kvp_msg->body.kvp_get.data;
		goto copy_value;

	case KVP_OP_SET:
	case KVP_OP_DELETE:
		goto response_done;

	default:
		break;
	}

	kvp_data = &kvp_msg->body.kvp_enum_data.data;
	key_name = key;

	/*
	 * The windows host expects the key/value pair to be encoded
	 * in utf16. Ensure that the key/value size reported to the host
	 * will be less than or equal to the MAX size (including the
	 * terminating character).
	 */
	keylen = utf8s_to_utf16s(key_name, strlen(key_name), UTF16_HOST_ENDIAN,
				(wchar_t *) kvp_data->key,
				(HV_KVP_EXCHANGE_MAX_KEY_SIZE / 2) - 2);
	kvp_data->key_size = 2*(keylen + 1); /* utf16 encoding */

copy_value:
	valuelen = utf8s_to_utf16s(value, strlen(value), UTF16_HOST_ENDIAN,
				(wchar_t *) kvp_data->value,
				(HV_KVP_EXCHANGE_MAX_VALUE_SIZE / 2) - 2);
	kvp_data->value_size = 2*(valuelen + 1); /* utf16 encoding */

	/*
	 * If the utf8s to utf16s conversion failed; notify host
	 * of the error.
	 */
	if ((keylen < 0) || (valuelen < 0))
		icmsghdrp->status = HV_E_FAIL;

	kvp_data->value_type = REG_SZ; /* all our values are strings */

response_done:
	icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION | ICMSGHDRFLAG_RESPONSE;

	vmbus_sendpacket(channel, recv_buffer, buf_len, req_id,
				VM_PKT_DATA_INBAND, 0);

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

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop = NULL;

	if (kvp_transaction.active) {
		/*
		 * We will defer processing this callback once
		 * the current transaction is complete.
		 */
		kvp_transaction.kvp_context = context;
		return;
	}

	vmbus_recvpacket(channel, recv_buffer, PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		icmsghdrp = (struct icmsg_hdr *)&recv_buffer[
			sizeof(struct vmbuspipe_hdr)];

		if (icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) {
			vmbus_prep_negotiate_resp(icmsghdrp, negop, recv_buffer);
		} else {
			kvp_msg = (struct hv_kvp_msg *)&recv_buffer[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			/*
			 * Stash away this global state for completing the
			 * transaction; note transactions are serialized.
			 */

			kvp_transaction.recv_len = recvlen;
			kvp_transaction.recv_channel = channel;
			kvp_transaction.recv_req_id = requestid;
			kvp_transaction.active = true;
			kvp_transaction.kvp_msg = kvp_msg;

			/*
			 * Get the information from the
			 * user-mode component.
			 * component. This transaction will be
			 * completed when we get the value from
			 * the user-mode component.
			 * Set a timeout to deal with
			 * user-mode not responding.
			 */
			schedule_work(&kvp_sendkey_work);
			schedule_delayed_work(&kvp_work, 5*HZ);

			return;

		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION
			| ICMSGHDRFLAG_RESPONSE;

		vmbus_sendpacket(channel, recv_buffer,
				       recvlen, requestid,
				       VM_PKT_DATA_INBAND, 0);
	}

}

int
hv_kvp_init(struct hv_util_service *srv)
{
	int err;

	err = cn_add_callback(&kvp_id, kvp_name, kvp_cn_callback);
	if (err)
		return err;
	recv_buffer = srv->recv_buffer;

	/*
	 * When this driver loads, the user level daemon that
	 * processes the host requests may not yet be running.
	 * Defer processing channel callbacks until the daemon
	 * has registered.
	 */
	kvp_transaction.active = true;

	return 0;
}

void hv_kvp_deinit(void)
{
	cn_del_callback(&kvp_id);
	cancel_delayed_work_sync(&kvp_work);
	cancel_work_sync(&kvp_sendkey_work);
}
