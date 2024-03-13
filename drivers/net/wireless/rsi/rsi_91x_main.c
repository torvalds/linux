/*
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/firmware.h>
#include <net/rsi_91x.h>
#include "rsi_mgmt.h"
#include "rsi_common.h"
#include "rsi_coex.h"
#include "rsi_hal.h"
#include "rsi_usb.h"

u32 rsi_zone_enabled = /* INFO_ZONE |
			INIT_ZONE |
			MGMT_TX_ZONE |
			MGMT_RX_ZONE |
			DATA_TX_ZONE |
			DATA_RX_ZONE |
			FSM_ZONE |
			ISR_ZONE | */
			ERR_ZONE |
			0;
EXPORT_SYMBOL_GPL(rsi_zone_enabled);

#ifdef CONFIG_RSI_COEX
static struct rsi_proto_ops g_proto_ops = {
	.coex_send_pkt = rsi_coex_send_pkt,
	.get_host_intf = rsi_get_host_intf,
	.set_bt_context = rsi_set_bt_context,
};
#endif

/**
 * rsi_dbg() - This function outputs informational messages.
 * @zone: Zone of interest for output message.
 * @fmt: printf-style format for output message.
 *
 * Return: none
 */
void rsi_dbg(u32 zone, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (zone & rsi_zone_enabled)
		pr_info("%pV", &vaf);
	va_end(args);
}
EXPORT_SYMBOL_GPL(rsi_dbg);

static char *opmode_str(int oper_mode)
{
	switch (oper_mode) {
	case DEV_OPMODE_WIFI_ALONE:
		return "Wi-Fi alone";
	case DEV_OPMODE_BT_ALONE:
		return "BT EDR alone";
	case DEV_OPMODE_BT_LE_ALONE:
		return "BT LE alone";
	case DEV_OPMODE_BT_DUAL:
		return "BT Dual";
	case DEV_OPMODE_STA_BT:
		return "Wi-Fi STA + BT EDR";
	case DEV_OPMODE_STA_BT_LE:
		return "Wi-Fi STA + BT LE";
	case DEV_OPMODE_STA_BT_DUAL:
		return "Wi-Fi STA + BT DUAL";
	case DEV_OPMODE_AP_BT:
		return "Wi-Fi AP + BT EDR";
	case DEV_OPMODE_AP_BT_DUAL:
		return "Wi-Fi AP + BT DUAL";
	}

	return "Unknown";
}

void rsi_print_version(struct rsi_common *common)
{
	rsi_dbg(ERR_ZONE, "================================================\n");
	rsi_dbg(ERR_ZONE, "================ RSI Version Info ==============\n");
	rsi_dbg(ERR_ZONE, "================================================\n");
	rsi_dbg(ERR_ZONE, "FW Version\t: %d.%d.%d\n",
		common->lmac_ver.major, common->lmac_ver.minor,
		common->lmac_ver.release_num);
	rsi_dbg(ERR_ZONE, "Operating mode\t: %d [%s]",
		common->oper_mode, opmode_str(common->oper_mode));
	rsi_dbg(ERR_ZONE, "Firmware file\t: %s", common->priv->fw_file_name);
	rsi_dbg(ERR_ZONE, "================================================\n");
}

/**
 * rsi_prepare_skb() - This function prepares the skb.
 * @common: Pointer to the driver private structure.
 * @buffer: Pointer to the packet data.
 * @pkt_len: Length of the packet.
 * @extended_desc: Extended descriptor.
 *
 * Return: Successfully skb.
 */
static struct sk_buff *rsi_prepare_skb(struct rsi_common *common,
				       u8 *buffer,
				       u32 pkt_len,
				       u8 extended_desc)
{
	struct sk_buff *skb = NULL;
	u8 payload_offset;

	if (WARN(!pkt_len, "%s: Dummy pkt received", __func__))
		return NULL;

	if (pkt_len > (RSI_RCV_BUFFER_LEN * 4)) {
		rsi_dbg(ERR_ZONE, "%s: Pkt size > max rx buf size %d\n",
			__func__, pkt_len);
		pkt_len = RSI_RCV_BUFFER_LEN * 4;
	}

	pkt_len -= extended_desc;
	skb = dev_alloc_skb(pkt_len + FRAME_DESC_SZ);
	if (skb == NULL)
		return NULL;

	payload_offset = (extended_desc + FRAME_DESC_SZ);
	skb_put(skb, pkt_len);
	memcpy((skb->data), (buffer + payload_offset), skb->len);

	return skb;
}

/**
 * rsi_read_pkt() - This function reads frames from the card.
 * @common: Pointer to the driver private structure.
 * @rx_pkt: Received pkt.
 * @rcv_pkt_len: Received pkt length. In case of USB it is 0.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_read_pkt(struct rsi_common *common, u8 *rx_pkt, s32 rcv_pkt_len)
{
	u8 *frame_desc = NULL, extended_desc = 0;
	u32 index, length = 0, queueno = 0;
	u16 actual_length = 0, offset;
	struct sk_buff *skb = NULL;
#ifdef CONFIG_RSI_COEX
	u8 bt_pkt_type;
#endif

	index = 0;
	do {
		frame_desc = &rx_pkt[index];
		actual_length = *(u16 *)&frame_desc[0];
		offset = *(u16 *)&frame_desc[2];
		if (!rcv_pkt_len && offset >
			RSI_MAX_RX_USB_PKT_SIZE - FRAME_DESC_SZ)
			goto fail;

		queueno = rsi_get_queueno(frame_desc, offset);
		length = rsi_get_length(frame_desc, offset);

		/* Extended descriptor is valid for WLAN queues only */
		if (queueno == RSI_WIFI_DATA_Q || queueno == RSI_WIFI_MGMT_Q)
			extended_desc = rsi_get_extended_desc(frame_desc,
							      offset);

		switch (queueno) {
		case RSI_COEX_Q:
#ifdef CONFIG_RSI_COEX
			if (common->coex_mode > 1)
				rsi_coex_recv_pkt(common, frame_desc + offset);
			else
#endif
				rsi_mgmt_pkt_recv(common,
						  (frame_desc + offset));
			break;

		case RSI_WIFI_DATA_Q:
			skb = rsi_prepare_skb(common,
					      (frame_desc + offset),
					      length,
					      extended_desc);
			if (skb == NULL)
				goto fail;

			rsi_indicate_pkt_to_os(common, skb);
			break;

		case RSI_WIFI_MGMT_Q:
			rsi_mgmt_pkt_recv(common, (frame_desc + offset));
			break;

#ifdef CONFIG_RSI_COEX
		case RSI_BT_MGMT_Q:
		case RSI_BT_DATA_Q:
#define BT_RX_PKT_TYPE_OFST	14
#define BT_CARD_READY_IND	0x89
			bt_pkt_type = frame_desc[offset + BT_RX_PKT_TYPE_OFST];
			if (bt_pkt_type == BT_CARD_READY_IND) {
				rsi_dbg(INFO_ZONE, "BT Card ready recvd\n");
				if (common->fsm_state == FSM_MAC_INIT_DONE)
					rsi_attach_bt(common);
				else
					common->bt_defer_attach = true;
			} else {
				if (common->bt_adapter)
					rsi_bt_ops.recv_pkt(common->bt_adapter,
							frame_desc + offset);
			}
			break;
#endif

		default:
			rsi_dbg(ERR_ZONE, "%s: pkt from invalid queue: %d\n",
				__func__,   queueno);
			goto fail;
		}

		index  += actual_length;
		rcv_pkt_len -= actual_length;
	} while (rcv_pkt_len > 0);

	return 0;
fail:
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(rsi_read_pkt);

/**
 * rsi_tx_scheduler_thread() - This function is a kernel thread to send the
 *			       packets to the device.
 * @common: Pointer to the driver private structure.
 *
 * Return: None.
 */
static void rsi_tx_scheduler_thread(struct rsi_common *common)
{
	struct rsi_hw *adapter = common->priv;
	u32 timeout = EVENT_WAIT_FOREVER;

	do {
		if (adapter->determine_event_timeout)
			timeout = adapter->determine_event_timeout(adapter);
		rsi_wait_event(&common->tx_thread.event, timeout);
		rsi_reset_event(&common->tx_thread.event);

		if (common->init_done)
			rsi_core_qos_processor(common);
	} while (atomic_read(&common->tx_thread.thread_done) == 0);
	kthread_complete_and_exit(&common->tx_thread.completion, 0);
}

#ifdef CONFIG_RSI_COEX
enum rsi_host_intf rsi_get_host_intf(void *priv)
{
	struct rsi_common *common = (struct rsi_common *)priv;

	return common->priv->rsi_host_intf;
}

void rsi_set_bt_context(void *priv, void *bt_context)
{
	struct rsi_common *common = (struct rsi_common *)priv;

	common->bt_adapter = bt_context;
}
#endif

void rsi_attach_bt(struct rsi_common *common)
{
#ifdef CONFIG_RSI_COEX
	if (rsi_bt_ops.attach(common, &g_proto_ops))
		rsi_dbg(ERR_ZONE,
			"Failed to attach BT module\n");
#endif
}

/**
 * rsi_91x_init() - This function initializes os interface operations.
 * @oper_mode: One of DEV_OPMODE_*.
 *
 * Return: Pointer to the adapter structure on success, NULL on failure .
 */
struct rsi_hw *rsi_91x_init(u16 oper_mode)
{
	struct rsi_hw *adapter = NULL;
	struct rsi_common *common = NULL;
	u8 ii = 0;

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	adapter->priv = kzalloc(sizeof(*common), GFP_KERNEL);
	if (adapter->priv == NULL) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of memory\n",
			__func__);
		kfree(adapter);
		return NULL;
	} else {
		common = adapter->priv;
		common->priv = adapter;
	}

	for (ii = 0; ii < NUM_SOFT_QUEUES; ii++)
		skb_queue_head_init(&common->tx_queue[ii]);

	rsi_init_event(&common->tx_thread.event);
	mutex_init(&common->mutex);
	mutex_init(&common->tx_lock);
	mutex_init(&common->rx_lock);
	mutex_init(&common->tx_bus_mutex);

	if (rsi_create_kthread(common,
			       &common->tx_thread,
			       rsi_tx_scheduler_thread,
			       "Tx-Thread")) {
		rsi_dbg(ERR_ZONE, "%s: Unable to init tx thrd\n", __func__);
		goto err;
	}

	rsi_default_ps_params(adapter);
	init_bgscan_params(common);
	spin_lock_init(&adapter->ps_lock);
	timer_setup(&common->roc_timer, rsi_roc_timeout, 0);
	init_completion(&common->wlan_init_completion);
	adapter->device_model = RSI_DEV_9113;
	common->oper_mode = oper_mode;

	/* Determine coex mode */
	switch (common->oper_mode) {
	case DEV_OPMODE_STA_BT_DUAL:
	case DEV_OPMODE_STA_BT:
	case DEV_OPMODE_STA_BT_LE:
	case DEV_OPMODE_BT_ALONE:
	case DEV_OPMODE_BT_LE_ALONE:
	case DEV_OPMODE_BT_DUAL:
		common->coex_mode = 2;
		break;
	case DEV_OPMODE_AP_BT_DUAL:
	case DEV_OPMODE_AP_BT:
		common->coex_mode = 4;
		break;
	case DEV_OPMODE_WIFI_ALONE:
		common->coex_mode = 1;
		break;
	default:
		common->oper_mode = 1;
		common->coex_mode = 1;
	}
	rsi_dbg(INFO_ZONE, "%s: oper_mode = %d, coex_mode = %d\n",
		__func__, common->oper_mode, common->coex_mode);

	adapter->device_model = RSI_DEV_9113;
#ifdef CONFIG_RSI_COEX
	if (common->coex_mode > 1) {
		if (rsi_coex_attach(common)) {
			rsi_dbg(ERR_ZONE, "Failed to init coex module\n");
			rsi_kill_thread(&common->tx_thread);
			goto err;
		}
	}
#endif

	common->init_done = true;
	return adapter;

err:
	kfree(common);
	kfree(adapter);
	return NULL;
}
EXPORT_SYMBOL_GPL(rsi_91x_init);

/**
 * rsi_91x_deinit() - This function de-intializes os intf operations.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: None.
 */
void rsi_91x_deinit(struct rsi_hw *adapter)
{
	struct rsi_common *common = adapter->priv;
	u8 ii;

	rsi_dbg(INFO_ZONE, "%s: Performing deinit os ops\n", __func__);

	rsi_kill_thread(&common->tx_thread);

	for (ii = 0; ii < NUM_SOFT_QUEUES; ii++)
		skb_queue_purge(&common->tx_queue[ii]);

#ifdef CONFIG_RSI_COEX
	if (common->coex_mode > 1) {
		if (common->bt_adapter) {
			rsi_bt_ops.detach(common->bt_adapter);
			common->bt_adapter = NULL;
		}
		rsi_coex_detach(common);
	}
#endif

	common->init_done = false;

	kfree(common);
	kfree(adapter->rsi_dev);
	kfree(adapter);
}
EXPORT_SYMBOL_GPL(rsi_91x_deinit);

/**
 * rsi_91x_hal_module_init() - This function is invoked when the module is
 *			       loaded into the kernel.
 *			       It registers the client driver.
 * @void: Void.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_91x_hal_module_init(void)
{
	rsi_dbg(INIT_ZONE, "%s: Module init called\n", __func__);
	return 0;
}

/**
 * rsi_91x_hal_module_exit() - This function is called at the time of
 *			       removing/unloading the module.
 *			       It unregisters the client driver.
 * @void: Void.
 *
 * Return: None.
 */
static void rsi_91x_hal_module_exit(void)
{
	rsi_dbg(INIT_ZONE, "%s: Module exit called\n", __func__);
}

module_init(rsi_91x_hal_module_init);
module_exit(rsi_91x_hal_module_exit);
MODULE_AUTHOR("Redpine Signals Inc");
MODULE_DESCRIPTION("Station driver for RSI 91x devices");
MODULE_VERSION("0.1");
MODULE_LICENSE("Dual BSD/GPL");
