/**
 * Marvell Bluetooth driver
 *
 * Copyright (C) 2009, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 **/

#include <linux/module.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btmrvl_drv.h"

#define VERSION "1.0"

/*
 * This function is called by interface specific interrupt handler.
 * It updates Power Save & Host Sleep states, and wakes up the main
 * thread.
 */
void btmrvl_interrupt(struct btmrvl_private *priv)
{
	priv->adapter->ps_state = PS_AWAKE;

	priv->adapter->wakeup_tries = 0;

	priv->adapter->int_count++;

	wake_up_interruptible(&priv->main_thread.wait_q);
}
EXPORT_SYMBOL_GPL(btmrvl_interrupt);

bool btmrvl_check_evtpkt(struct btmrvl_private *priv, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *) skb->data;
	struct hci_ev_cmd_complete *ec;
	u16 opcode, ocf, ogf;

	if (hdr->evt == HCI_EV_CMD_COMPLETE) {
		ec = (void *) (skb->data + HCI_EVENT_HDR_SIZE);
		opcode = __le16_to_cpu(ec->opcode);
		ocf = hci_opcode_ocf(opcode);
		ogf = hci_opcode_ogf(opcode);

		if (ocf == BT_CMD_MODULE_CFG_REQ &&
					priv->btmrvl_dev.sendcmdflag) {
			priv->btmrvl_dev.sendcmdflag = false;
			priv->adapter->cmd_complete = true;
			wake_up_interruptible(&priv->adapter->cmd_wait_q);
		}

		if (ogf == OGF) {
			BT_DBG("vendor event skipped: ogf 0x%4.4x", ogf);
			kfree_skb(skb);
			return false;
		}
	}

	return true;
}
EXPORT_SYMBOL_GPL(btmrvl_check_evtpkt);

int btmrvl_process_event(struct btmrvl_private *priv, struct sk_buff *skb)
{
	struct btmrvl_adapter *adapter = priv->adapter;
	struct btmrvl_event *event;
	int ret = 0;

	event = (struct btmrvl_event *) skb->data;
	if (event->ec != 0xff) {
		BT_DBG("Not Marvell Event=%x", event->ec);
		ret = -EINVAL;
		goto exit;
	}

	switch (event->data[0]) {
	case BT_CMD_AUTO_SLEEP_MODE:
		if (!event->data[2]) {
			if (event->data[1] == BT_PS_ENABLE)
				adapter->psmode = 1;
			else
				adapter->psmode = 0;
			BT_DBG("PS Mode:%s",
				(adapter->psmode) ? "Enable" : "Disable");
		} else {
			BT_DBG("PS Mode command failed");
		}
		break;

	case BT_CMD_HOST_SLEEP_CONFIG:
		if (!event->data[3])
			BT_DBG("gpio=%x, gap=%x", event->data[1],
							event->data[2]);
		else
			BT_DBG("HSCFG command failed");
		break;

	case BT_CMD_HOST_SLEEP_ENABLE:
		if (!event->data[1]) {
			adapter->hs_state = HS_ACTIVATED;
			if (adapter->psmode)
				adapter->ps_state = PS_SLEEP;
			wake_up_interruptible(&adapter->cmd_wait_q);
			BT_DBG("HS ACTIVATED!");
		} else {
			BT_DBG("HS Enable failed");
		}
		break;

	case BT_CMD_MODULE_CFG_REQ:
		if (priv->btmrvl_dev.sendcmdflag &&
				event->data[1] == MODULE_BRINGUP_REQ) {
			BT_DBG("EVENT:%s",
				((event->data[2] == MODULE_BROUGHT_UP) ||
				(event->data[2] == MODULE_ALREADY_UP)) ?
				"Bring-up succeed" : "Bring-up failed");

			if (event->length > 3 && event->data[3])
				priv->btmrvl_dev.dev_type = HCI_AMP;
			else
				priv->btmrvl_dev.dev_type = HCI_BREDR;

			BT_DBG("dev_type: %d", priv->btmrvl_dev.dev_type);
		} else if (priv->btmrvl_dev.sendcmdflag &&
				event->data[1] == MODULE_SHUTDOWN_REQ) {
			BT_DBG("EVENT:%s", (event->data[2]) ?
				"Shutdown failed" : "Shutdown succeed");
		} else {
			BT_DBG("BT_CMD_MODULE_CFG_REQ resp for APP");
			ret = -EINVAL;
		}
		break;

	case BT_EVENT_POWER_STATE:
		if (event->data[1] == BT_PS_SLEEP)
			adapter->ps_state = PS_SLEEP;
		BT_DBG("EVENT:%s",
			(adapter->ps_state) ? "PS_SLEEP" : "PS_AWAKE");
		break;

	default:
		BT_DBG("Unknown Event=%d", event->data[0]);
		ret = -EINVAL;
		break;
	}

exit:
	if (!ret)
		kfree_skb(skb);

	return ret;
}
EXPORT_SYMBOL_GPL(btmrvl_process_event);

int btmrvl_send_module_cfg_cmd(struct btmrvl_private *priv, int subcmd)
{
	struct sk_buff *skb;
	struct btmrvl_cmd *cmd;
	int ret = 0;

	skb = bt_skb_alloc(sizeof(*cmd), GFP_ATOMIC);
	if (skb == NULL) {
		BT_ERR("No free skb");
		return -ENOMEM;
	}

	cmd = (struct btmrvl_cmd *) skb_put(skb, sizeof(*cmd));
	cmd->ocf_ogf = cpu_to_le16(hci_opcode_pack(OGF, BT_CMD_MODULE_CFG_REQ));
	cmd->length = 1;
	cmd->data[0] = subcmd;

	bt_cb(skb)->pkt_type = MRVL_VENDOR_PKT;

	skb->dev = (void *) priv->btmrvl_dev.hcidev;
	skb_queue_head(&priv->adapter->tx_queue, skb);

	priv->btmrvl_dev.sendcmdflag = true;

	priv->adapter->cmd_complete = false;

	BT_DBG("Queue module cfg Command");

	wake_up_interruptible(&priv->main_thread.wait_q);

	if (!wait_event_interruptible_timeout(priv->adapter->cmd_wait_q,
				priv->adapter->cmd_complete,
				msecs_to_jiffies(WAIT_UNTIL_CMD_RESP))) {
		ret = -ETIMEDOUT;
		BT_ERR("module_cfg_cmd(%x): timeout: %d",
					subcmd, priv->btmrvl_dev.sendcmdflag);
	}

	BT_DBG("module cfg Command done");

	return ret;
}
EXPORT_SYMBOL_GPL(btmrvl_send_module_cfg_cmd);

int btmrvl_send_hscfg_cmd(struct btmrvl_private *priv)
{
	struct sk_buff *skb;
	struct btmrvl_cmd *cmd;

	skb = bt_skb_alloc(sizeof(*cmd), GFP_ATOMIC);
	if (!skb) {
		BT_ERR("No free skb");
		return -ENOMEM;
	}

	cmd = (struct btmrvl_cmd *) skb_put(skb, sizeof(*cmd));
	cmd->ocf_ogf = cpu_to_le16(hci_opcode_pack(OGF,
						   BT_CMD_HOST_SLEEP_CONFIG));
	cmd->length = 2;
	cmd->data[0] = (priv->btmrvl_dev.gpio_gap & 0xff00) >> 8;
	cmd->data[1] = (u8) (priv->btmrvl_dev.gpio_gap & 0x00ff);

	bt_cb(skb)->pkt_type = MRVL_VENDOR_PKT;

	skb->dev = (void *) priv->btmrvl_dev.hcidev;
	skb_queue_head(&priv->adapter->tx_queue, skb);

	BT_DBG("Queue HSCFG Command, gpio=0x%x, gap=0x%x", cmd->data[0],
	       cmd->data[1]);

	return 0;
}
EXPORT_SYMBOL_GPL(btmrvl_send_hscfg_cmd);

int btmrvl_enable_ps(struct btmrvl_private *priv)
{
	struct sk_buff *skb;
	struct btmrvl_cmd *cmd;

	skb = bt_skb_alloc(sizeof(*cmd), GFP_ATOMIC);
	if (skb == NULL) {
		BT_ERR("No free skb");
		return -ENOMEM;
	}

	cmd = (struct btmrvl_cmd *) skb_put(skb, sizeof(*cmd));
	cmd->ocf_ogf = cpu_to_le16(hci_opcode_pack(OGF,
					BT_CMD_AUTO_SLEEP_MODE));
	cmd->length = 1;

	if (priv->btmrvl_dev.psmode)
		cmd->data[0] = BT_PS_ENABLE;
	else
		cmd->data[0] = BT_PS_DISABLE;

	bt_cb(skb)->pkt_type = MRVL_VENDOR_PKT;

	skb->dev = (void *) priv->btmrvl_dev.hcidev;
	skb_queue_head(&priv->adapter->tx_queue, skb);

	BT_DBG("Queue PSMODE Command:%d", cmd->data[0]);

	return 0;
}
EXPORT_SYMBOL_GPL(btmrvl_enable_ps);

int btmrvl_enable_hs(struct btmrvl_private *priv)
{
	struct sk_buff *skb;
	struct btmrvl_cmd *cmd;
	int ret = 0;

	skb = bt_skb_alloc(sizeof(*cmd), GFP_ATOMIC);
	if (skb == NULL) {
		BT_ERR("No free skb");
		return -ENOMEM;
	}

	cmd = (struct btmrvl_cmd *) skb_put(skb, sizeof(*cmd));
	cmd->ocf_ogf = cpu_to_le16(hci_opcode_pack(OGF, BT_CMD_HOST_SLEEP_ENABLE));
	cmd->length = 0;

	bt_cb(skb)->pkt_type = MRVL_VENDOR_PKT;

	skb->dev = (void *) priv->btmrvl_dev.hcidev;
	skb_queue_head(&priv->adapter->tx_queue, skb);

	BT_DBG("Queue hs enable Command");

	wake_up_interruptible(&priv->main_thread.wait_q);

	if (!wait_event_interruptible_timeout(priv->adapter->cmd_wait_q,
			priv->adapter->hs_state,
			msecs_to_jiffies(WAIT_UNTIL_HS_STATE_CHANGED))) {
		ret = -ETIMEDOUT;
		BT_ERR("timeout: %d, %d,%d", priv->adapter->hs_state,
						priv->adapter->ps_state,
						priv->adapter->wakeup_tries);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(btmrvl_enable_hs);

int btmrvl_prepare_command(struct btmrvl_private *priv)
{
	int ret = 0;

	if (priv->btmrvl_dev.hscfgcmd) {
		priv->btmrvl_dev.hscfgcmd = 0;
		btmrvl_send_hscfg_cmd(priv);
	}

	if (priv->btmrvl_dev.pscmd) {
		priv->btmrvl_dev.pscmd = 0;
		btmrvl_enable_ps(priv);
	}

	if (priv->btmrvl_dev.hscmd) {
		priv->btmrvl_dev.hscmd = 0;

		if (priv->btmrvl_dev.hsmode) {
			ret = btmrvl_enable_hs(priv);
		} else {
			ret = priv->hw_wakeup_firmware(priv);
			priv->adapter->hs_state = HS_DEACTIVATED;
		}
	}

	return ret;
}

static int btmrvl_tx_pkt(struct btmrvl_private *priv, struct sk_buff *skb)
{
	int ret = 0;

	if (!skb || !skb->data)
		return -EINVAL;

	if (!skb->len || ((skb->len + BTM_HEADER_LEN) > BTM_UPLD_SIZE)) {
		BT_ERR("Tx Error: Bad skb length %d : %d",
						skb->len, BTM_UPLD_SIZE);
		return -EINVAL;
	}

	if (skb_headroom(skb) < BTM_HEADER_LEN) {
		struct sk_buff *tmp = skb;

		skb = skb_realloc_headroom(skb, BTM_HEADER_LEN);
		if (!skb) {
			BT_ERR("Tx Error: realloc_headroom failed %d",
				BTM_HEADER_LEN);
			skb = tmp;
			return -EINVAL;
		}

		kfree_skb(tmp);
	}

	skb_push(skb, BTM_HEADER_LEN);

	/* header type: byte[3]
	 * HCI_COMMAND = 1, ACL_DATA = 2, SCO_DATA = 3, 0xFE = Vendor
	 * header length: byte[2][1][0]
	 */

	skb->data[0] = (skb->len & 0x0000ff);
	skb->data[1] = (skb->len & 0x00ff00) >> 8;
	skb->data[2] = (skb->len & 0xff0000) >> 16;
	skb->data[3] = bt_cb(skb)->pkt_type;

	if (priv->hw_host_to_card)
		ret = priv->hw_host_to_card(priv, skb->data, skb->len);

	return ret;
}

static void btmrvl_init_adapter(struct btmrvl_private *priv)
{
	skb_queue_head_init(&priv->adapter->tx_queue);

	priv->adapter->ps_state = PS_AWAKE;

	init_waitqueue_head(&priv->adapter->cmd_wait_q);
}

static void btmrvl_free_adapter(struct btmrvl_private *priv)
{
	skb_queue_purge(&priv->adapter->tx_queue);

	kfree(priv->adapter);

	priv->adapter = NULL;
}

static int btmrvl_ioctl(struct hci_dev *hdev,
				unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int btmrvl_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct btmrvl_private *priv = NULL;

	BT_DBG("type=%d, len=%d", skb->pkt_type, skb->len);

	if (!hdev) {
		BT_ERR("Frame for unknown HCI device");
		return -ENODEV;
	}

	priv = hci_get_drvdata(hdev);

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		BT_ERR("Failed testing HCI_RUNING, flags=%lx", hdev->flags);
		print_hex_dump_bytes("data: ", DUMP_PREFIX_OFFSET,
							skb->data, skb->len);
		return -EBUSY;
	}

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}

	skb_queue_tail(&priv->adapter->tx_queue, skb);

	wake_up_interruptible(&priv->main_thread.wait_q);

	return 0;
}

static int btmrvl_flush(struct hci_dev *hdev)
{
	struct btmrvl_private *priv = hci_get_drvdata(hdev);

	skb_queue_purge(&priv->adapter->tx_queue);

	return 0;
}

static int btmrvl_close(struct hci_dev *hdev)
{
	struct btmrvl_private *priv = hci_get_drvdata(hdev);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	skb_queue_purge(&priv->adapter->tx_queue);

	return 0;
}

static int btmrvl_open(struct hci_dev *hdev)
{
	set_bit(HCI_RUNNING, &hdev->flags);

	return 0;
}

/*
 * This function handles the event generated by firmware, rx data
 * received from firmware, and tx data sent from kernel.
 */
static int btmrvl_service_main_thread(void *data)
{
	struct btmrvl_thread *thread = data;
	struct btmrvl_private *priv = thread->priv;
	struct btmrvl_adapter *adapter = priv->adapter;
	wait_queue_t wait;
	struct sk_buff *skb;
	ulong flags;

	init_waitqueue_entry(&wait, current);

	for (;;) {
		add_wait_queue(&thread->wait_q, &wait);

		set_current_state(TASK_INTERRUPTIBLE);

		if (adapter->wakeup_tries ||
				((!adapter->int_count) &&
				(!priv->btmrvl_dev.tx_dnld_rdy ||
				skb_queue_empty(&adapter->tx_queue)))) {
			BT_DBG("main_thread is sleeping...");
			schedule();
		}

		set_current_state(TASK_RUNNING);

		remove_wait_queue(&thread->wait_q, &wait);

		BT_DBG("main_thread woke up");

		if (kthread_should_stop()) {
			BT_DBG("main_thread: break from main thread");
			break;
		}

		spin_lock_irqsave(&priv->driver_lock, flags);
		if (adapter->int_count) {
			adapter->int_count = 0;
			spin_unlock_irqrestore(&priv->driver_lock, flags);
			priv->hw_process_int_status(priv);
		} else if (adapter->ps_state == PS_SLEEP &&
					!skb_queue_empty(&adapter->tx_queue)) {
			spin_unlock_irqrestore(&priv->driver_lock, flags);
			adapter->wakeup_tries++;
			priv->hw_wakeup_firmware(priv);
			continue;
		} else {
			spin_unlock_irqrestore(&priv->driver_lock, flags);
		}

		if (adapter->ps_state == PS_SLEEP)
			continue;

		if (!priv->btmrvl_dev.tx_dnld_rdy)
			continue;

		skb = skb_dequeue(&adapter->tx_queue);
		if (skb) {
			if (btmrvl_tx_pkt(priv, skb))
				priv->btmrvl_dev.hcidev->stat.err_tx++;
			else
				priv->btmrvl_dev.hcidev->stat.byte_tx += skb->len;

			kfree_skb(skb);
		}
	}

	return 0;
}

int btmrvl_register_hdev(struct btmrvl_private *priv)
{
	struct hci_dev *hdev = NULL;
	int ret;

	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can not allocate HCI device");
		goto err_hdev;
	}

	priv->btmrvl_dev.hcidev = hdev;
	hci_set_drvdata(hdev, priv);

	hdev->bus = HCI_SDIO;
	hdev->open = btmrvl_open;
	hdev->close = btmrvl_close;
	hdev->flush = btmrvl_flush;
	hdev->send = btmrvl_send_frame;
	hdev->ioctl = btmrvl_ioctl;

	btmrvl_send_module_cfg_cmd(priv, MODULE_BRINGUP_REQ);

	hdev->dev_type = priv->btmrvl_dev.dev_type;

	ret = hci_register_dev(hdev);
	if (ret < 0) {
		BT_ERR("Can not register HCI device");
		goto err_hci_register_dev;
	}

#ifdef CONFIG_DEBUG_FS
	btmrvl_debugfs_init(hdev);
#endif

	return 0;

err_hci_register_dev:
	hci_free_dev(hdev);

err_hdev:
	/* Stop the thread servicing the interrupts */
	kthread_stop(priv->main_thread.task);

	btmrvl_free_adapter(priv);
	kfree(priv);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(btmrvl_register_hdev);

struct btmrvl_private *btmrvl_add_card(void *card)
{
	struct btmrvl_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		BT_ERR("Can not allocate priv");
		goto err_priv;
	}

	priv->adapter = kzalloc(sizeof(*priv->adapter), GFP_KERNEL);
	if (!priv->adapter) {
		BT_ERR("Allocate buffer for btmrvl_adapter failed!");
		goto err_adapter;
	}

	btmrvl_init_adapter(priv);

	BT_DBG("Starting kthread...");
	priv->main_thread.priv = priv;
	spin_lock_init(&priv->driver_lock);

	init_waitqueue_head(&priv->main_thread.wait_q);
	priv->main_thread.task = kthread_run(btmrvl_service_main_thread,
				&priv->main_thread, "btmrvl_main_service");

	priv->btmrvl_dev.card = card;
	priv->btmrvl_dev.tx_dnld_rdy = true;

	return priv;

err_adapter:
	kfree(priv);

err_priv:
	return NULL;
}
EXPORT_SYMBOL_GPL(btmrvl_add_card);

int btmrvl_remove_card(struct btmrvl_private *priv)
{
	struct hci_dev *hdev;

	hdev = priv->btmrvl_dev.hcidev;

	wake_up_interruptible(&priv->adapter->cmd_wait_q);

	kthread_stop(priv->main_thread.task);

#ifdef CONFIG_DEBUG_FS
	btmrvl_debugfs_remove(hdev);
#endif

	hci_unregister_dev(hdev);

	hci_free_dev(hdev);

	priv->btmrvl_dev.hcidev = NULL;

	btmrvl_free_adapter(priv);

	kfree(priv);

	return 0;
}
EXPORT_SYMBOL_GPL(btmrvl_remove_card);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell Bluetooth driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL v2");
