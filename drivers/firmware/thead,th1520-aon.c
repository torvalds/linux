// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (c) 2024 Samsung Electronics Co., Ltd.
 * Author: Michal Wilczynski <m.wilczynski@samsung.com>
 */

#include <linux/device.h>
#include <linux/firmware/thead/thead,th1520-aon.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/slab.h>

#define MAX_RX_TIMEOUT (msecs_to_jiffies(3000))
#define MAX_TX_TIMEOUT 500

struct th1520_aon_chan {
	struct mbox_chan *ch;
	struct th1520_aon_rpc_ack_common ack_msg;
	struct mbox_client cl;
	struct completion done;

	/* make sure only one RPC is performed at a time */
	struct mutex transaction_lock;
};

struct th1520_aon_msg_req_set_resource_power_mode {
	struct th1520_aon_rpc_msg_hdr hdr;
	u16 resource;
	u16 mode;
	u16 reserved[10];
} __packed __aligned(1);

/*
 * This type is used to indicate error response for most functions.
 */
enum th1520_aon_error_codes {
	LIGHT_AON_ERR_NONE = 0, /* Success */
	LIGHT_AON_ERR_VERSION = 1, /* Incompatible API version */
	LIGHT_AON_ERR_CONFIG = 2, /* Configuration error */
	LIGHT_AON_ERR_PARM = 3, /* Bad parameter */
	LIGHT_AON_ERR_NOACCESS = 4, /* Permission error (no access) */
	LIGHT_AON_ERR_LOCKED = 5, /* Permission error (locked) */
	LIGHT_AON_ERR_UNAVAILABLE = 6, /* Unavailable (out of resources) */
	LIGHT_AON_ERR_NOTFOUND = 7, /* Not found */
	LIGHT_AON_ERR_NOPOWER = 8, /* No power */
	LIGHT_AON_ERR_IPC = 9, /* Generic IPC error */
	LIGHT_AON_ERR_BUSY = 10, /* Resource is currently busy/active */
	LIGHT_AON_ERR_FAIL = 11, /* General I/O failure */
	LIGHT_AON_ERR_LAST
};

static int th1520_aon_linux_errmap[LIGHT_AON_ERR_LAST] = {
	0, /* LIGHT_AON_ERR_NONE */
	-EINVAL, /* LIGHT_AON_ERR_VERSION */
	-EINVAL, /* LIGHT_AON_ERR_CONFIG */
	-EINVAL, /* LIGHT_AON_ERR_PARM */
	-EACCES, /* LIGHT_AON_ERR_NOACCESS */
	-EACCES, /* LIGHT_AON_ERR_LOCKED */
	-ERANGE, /* LIGHT_AON_ERR_UNAVAILABLE */
	-EEXIST, /* LIGHT_AON_ERR_NOTFOUND */
	-EPERM, /* LIGHT_AON_ERR_NOPOWER */
	-EPIPE, /* LIGHT_AON_ERR_IPC */
	-EBUSY, /* LIGHT_AON_ERR_BUSY */
	-EIO, /* LIGHT_AON_ERR_FAIL */
};

static inline int th1520_aon_to_linux_errno(int errno)
{
	if (errno >= LIGHT_AON_ERR_NONE && errno < LIGHT_AON_ERR_LAST)
		return th1520_aon_linux_errmap[errno];

	return -EIO;
}

static void th1520_aon_rx_callback(struct mbox_client *c, void *rx_msg)
{
	struct th1520_aon_chan *aon_chan =
		container_of(c, struct th1520_aon_chan, cl);
	struct th1520_aon_rpc_msg_hdr *hdr =
		(struct th1520_aon_rpc_msg_hdr *)rx_msg;
	u8 recv_size = sizeof(struct th1520_aon_rpc_msg_hdr) + hdr->size;

	if (recv_size != sizeof(struct th1520_aon_rpc_ack_common)) {
		dev_err(c->dev, "Invalid ack size, not completing\n");
		return;
	}

	memcpy(&aon_chan->ack_msg, rx_msg, recv_size);
	complete(&aon_chan->done);
}

/**
 * th1520_aon_call_rpc() - Send an RPC request to the TH1520 AON subsystem
 * @aon_chan: Pointer to the AON channel structure
 * @msg: Pointer to the message (RPC payload) that will be sent
 *
 * This function sends an RPC message to the TH1520 AON subsystem via mailbox.
 * It takes the provided @msg buffer, formats it with version and service flags,
 * then blocks until the RPC completes or times out. The completion is signaled
 * by the `aon_chan->done` completion, which is waited upon for a duration
 * defined by `MAX_RX_TIMEOUT`.
 *
 * Return:
 * * 0 on success
 * * -ETIMEDOUT if the RPC call times out
 * * A negative error code if the mailbox send fails or if AON responds with
 *   a non-zero error code (converted via th1520_aon_to_linux_errno()).
 */
int th1520_aon_call_rpc(struct th1520_aon_chan *aon_chan, void *msg)
{
	struct th1520_aon_rpc_msg_hdr *hdr = msg;
	int ret;

	mutex_lock(&aon_chan->transaction_lock);
	reinit_completion(&aon_chan->done);

	RPC_SET_VER(hdr, TH1520_AON_RPC_VERSION);
	RPC_SET_SVC_ID(hdr, hdr->svc);
	RPC_SET_SVC_FLAG_MSG_TYPE(hdr, RPC_SVC_MSG_TYPE_DATA);
	RPC_SET_SVC_FLAG_ACK_TYPE(hdr, RPC_SVC_MSG_NEED_ACK);

	ret = mbox_send_message(aon_chan->ch, msg);
	if (ret < 0) {
		dev_err(aon_chan->cl.dev, "RPC send msg failed: %d\n", ret);
		goto out;
	}

	if (!wait_for_completion_timeout(&aon_chan->done, MAX_RX_TIMEOUT)) {
		dev_err(aon_chan->cl.dev, "RPC send msg timeout\n");
		mutex_unlock(&aon_chan->transaction_lock);
		return -ETIMEDOUT;
	}

	ret = aon_chan->ack_msg.err_code;

out:
	mutex_unlock(&aon_chan->transaction_lock);

	return th1520_aon_to_linux_errno(ret);
}
EXPORT_SYMBOL_GPL(th1520_aon_call_rpc);

/**
 * th1520_aon_power_update() - Change power state of a resource via TH1520 AON
 * @aon_chan: Pointer to the AON channel structure
 * @rsrc: Resource ID whose power state needs to be updated
 * @power_on: Boolean indicating whether the resource should be powered on (true)
 *            or powered off (false)
 *
 * This function requests the TH1520 AON subsystem to set the power mode of the
 * given resource (@rsrc) to either on or off. It constructs the message in
 * `struct th1520_aon_msg_req_set_resource_power_mode` and then invokes
 * th1520_aon_call_rpc() to make the request. If the AON call fails, an error
 * message is logged along with the specific return code.
 *
 * Return:
 * * 0 on success
 * * A negative error code in case of failures (propagated from
 *   th1520_aon_call_rpc()).
 */
int th1520_aon_power_update(struct th1520_aon_chan *aon_chan, u16 rsrc,
			    bool power_on)
{
	struct th1520_aon_msg_req_set_resource_power_mode msg = {};
	struct th1520_aon_rpc_msg_hdr *hdr = &msg.hdr;
	int ret;

	hdr->svc = TH1520_AON_RPC_SVC_PM;
	hdr->func = TH1520_AON_PM_FUNC_SET_RESOURCE_POWER_MODE;
	hdr->size = TH1520_AON_RPC_MSG_NUM;

	RPC_SET_BE16(&msg.resource, 0, rsrc);
	RPC_SET_BE16(&msg.resource, 2,
		     (power_on ? TH1520_AON_PM_PW_MODE_ON :
				 TH1520_AON_PM_PW_MODE_OFF));

	ret = th1520_aon_call_rpc(aon_chan, &msg);
	if (ret)
		dev_err(aon_chan->cl.dev, "failed to power %s resource %d ret %d\n",
			power_on ? "up" : "off", rsrc, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(th1520_aon_power_update);

/**
 * th1520_aon_init() - Initialize TH1520 AON firmware protocol interface
 * @dev: Device pointer for the AON subsystem
 *
 * This function initializes the TH1520 AON firmware protocol interface by:
 * - Allocating and initializing the AON channel structure
 * - Setting up the mailbox client
 * - Requesting the AON mailbox channel
 * - Initializing synchronization primitives
 *
 * Return:
 * * Valid pointer to th1520_aon_chan structure on success
 * * ERR_PTR(-ENOMEM) if memory allocation fails
 * * ERR_PTR() with other negative error codes from mailbox operations
 */
struct th1520_aon_chan *th1520_aon_init(struct device *dev)
{
	struct th1520_aon_chan *aon_chan;
	struct mbox_client *cl;
	int ret;

	aon_chan = kzalloc(sizeof(*aon_chan), GFP_KERNEL);
	if (!aon_chan)
		return ERR_PTR(-ENOMEM);

	cl = &aon_chan->cl;
	cl->dev = dev;
	cl->tx_block = true;
	cl->tx_tout = MAX_TX_TIMEOUT;
	cl->rx_callback = th1520_aon_rx_callback;

	aon_chan->ch = mbox_request_channel_byname(cl, "aon");
	if (IS_ERR(aon_chan->ch)) {
		dev_err(dev, "Failed to request aon mbox chan\n");
		ret = PTR_ERR(aon_chan->ch);
		kfree(aon_chan);
		return ERR_PTR(ret);
	}

	mutex_init(&aon_chan->transaction_lock);
	init_completion(&aon_chan->done);

	return aon_chan;
}
EXPORT_SYMBOL_GPL(th1520_aon_init);

/**
 * th1520_aon_deinit() - Clean up TH1520 AON firmware protocol interface
 * @aon_chan: Pointer to the AON channel structure to clean up
 *
 * This function cleans up resources allocated by th1520_aon_init():
 * - Frees the mailbox channel
 * - Frees the AON channel
 */
void th1520_aon_deinit(struct th1520_aon_chan *aon_chan)
{
	mbox_free_channel(aon_chan->ch);
	kfree(aon_chan);
}
EXPORT_SYMBOL_GPL(th1520_aon_deinit);

MODULE_AUTHOR("Michal Wilczynski <m.wilczynski@samsung.com>");
MODULE_DESCRIPTION("T-HEAD TH1520 Always-On firmware protocol library");
MODULE_LICENSE("GPL");
