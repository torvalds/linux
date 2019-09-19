// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Bluetooth Software UART Qualcomm protocol
 *
 *  HCI_IBS (HCI In-Band Sleep) is Qualcomm's power management
 *  protocol extension to H4.
 *
 *  Copyright (C) 2007 Texas Instruments, Inc.
 *  Copyright (c) 2010, 2012, 2018 The Linux Foundation. All rights reserved.
 *
 *  Acknowledgements:
 *  This file is based on hci_ll.c, which was...
 *  Written by Ohad Ben-Cohen <ohad@bencohen.org>
 *  which was in turn based on hci_h4.c, which was written
 *  by Maxim Krasnyansky and Marcel Holtmann.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/serdev.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"
#include "btqca.h"

/* HCI_IBS protocol messages */
#define HCI_IBS_SLEEP_IND	0xFE
#define HCI_IBS_WAKE_IND	0xFD
#define HCI_IBS_WAKE_ACK	0xFC
#define HCI_MAX_IBS_SIZE	10

#define IBS_WAKE_RETRANS_TIMEOUT_MS	100
#define IBS_TX_IDLE_TIMEOUT_MS		2000
#define CMD_TRANS_TIMEOUT_MS		100

/* susclk rate */
#define SUSCLK_RATE_32KHZ	32768

/* Controller debug log header */
#define QCA_DEBUG_HANDLE	0x2EDC

enum qca_flags {
	QCA_IBS_ENABLED,
	QCA_DROP_VENDOR_EVENT,
};

/* HCI_IBS transmit side sleep protocol states */
enum tx_ibs_states {
	HCI_IBS_TX_ASLEEP,
	HCI_IBS_TX_WAKING,
	HCI_IBS_TX_AWAKE,
};

/* HCI_IBS receive side sleep protocol states */
enum rx_states {
	HCI_IBS_RX_ASLEEP,
	HCI_IBS_RX_AWAKE,
};

/* HCI_IBS transmit and receive side clock state vote */
enum hci_ibs_clock_state_vote {
	HCI_IBS_VOTE_STATS_UPDATE,
	HCI_IBS_TX_VOTE_CLOCK_ON,
	HCI_IBS_TX_VOTE_CLOCK_OFF,
	HCI_IBS_RX_VOTE_CLOCK_ON,
	HCI_IBS_RX_VOTE_CLOCK_OFF,
};

struct qca_data {
	struct hci_uart *hu;
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
	struct sk_buff_head tx_wait_q;	/* HCI_IBS wait queue	*/
	spinlock_t hci_ibs_lock;	/* HCI_IBS state lock	*/
	u8 tx_ibs_state;	/* HCI_IBS transmit side power state*/
	u8 rx_ibs_state;	/* HCI_IBS receive side power state */
	bool tx_vote;		/* Clock must be on for TX */
	bool rx_vote;		/* Clock must be on for RX */
	struct timer_list tx_idle_timer;
	u32 tx_idle_delay;
	struct timer_list wake_retrans_timer;
	u32 wake_retrans;
	struct workqueue_struct *workqueue;
	struct work_struct ws_awake_rx;
	struct work_struct ws_awake_device;
	struct work_struct ws_rx_vote_off;
	struct work_struct ws_tx_vote_off;
	unsigned long flags;
	struct completion drop_ev_comp;

	/* For debugging purpose */
	u64 ibs_sent_wacks;
	u64 ibs_sent_slps;
	u64 ibs_sent_wakes;
	u64 ibs_recv_wacks;
	u64 ibs_recv_slps;
	u64 ibs_recv_wakes;
	u64 vote_last_jif;
	u32 vote_on_ms;
	u32 vote_off_ms;
	u64 tx_votes_on;
	u64 rx_votes_on;
	u64 tx_votes_off;
	u64 rx_votes_off;
	u64 votes_on;
	u64 votes_off;
};

enum qca_speed_type {
	QCA_INIT_SPEED = 1,
	QCA_OPER_SPEED
};

/*
 * Voltage regulator information required for configuring the
 * QCA Bluetooth chipset
 */
struct qca_vreg {
	const char *name;
	unsigned int min_uV;
	unsigned int max_uV;
	unsigned int load_uA;
};

struct qca_vreg_data {
	enum qca_btsoc_type soc_type;
	struct qca_vreg *vregs;
	size_t num_vregs;
};

/*
 * Platform data for the QCA Bluetooth power driver.
 */
struct qca_power {
	struct device *dev;
	const struct qca_vreg_data *vreg_data;
	struct regulator_bulk_data *vreg_bulk;
	bool vregs_on;
};

struct qca_serdev {
	struct hci_uart	 serdev_hu;
	struct gpio_desc *bt_en;
	struct clk	 *susclk;
	enum qca_btsoc_type btsoc_type;
	struct qca_power *bt_power;
	u32 init_speed;
	u32 oper_speed;
	const char *firmware_name;
};

static int qca_power_setup(struct hci_uart *hu, bool on);
static void qca_power_shutdown(struct hci_uart *hu);
static int qca_power_off(struct hci_dev *hdev);

static enum qca_btsoc_type qca_soc_type(struct hci_uart *hu)
{
	enum qca_btsoc_type soc_type;

	if (hu->serdev) {
		struct qca_serdev *qsd = serdev_device_get_drvdata(hu->serdev);

		soc_type = qsd->btsoc_type;
	} else {
		soc_type = QCA_ROME;
	}

	return soc_type;
}

static const char *qca_get_firmware_name(struct hci_uart *hu)
{
	if (hu->serdev) {
		struct qca_serdev *qsd = serdev_device_get_drvdata(hu->serdev);

		return qsd->firmware_name;
	} else {
		return NULL;
	}
}

static void __serial_clock_on(struct tty_struct *tty)
{
	/* TODO: Some chipset requires to enable UART clock on client
	 * side to save power consumption or manual work is required.
	 * Please put your code to control UART clock here if needed
	 */
}

static void __serial_clock_off(struct tty_struct *tty)
{
	/* TODO: Some chipset requires to disable UART clock on client
	 * side to save power consumption or manual work is required.
	 * Please put your code to control UART clock off here if needed
	 */
}

/* serial_clock_vote needs to be called with the ibs lock held */
static void serial_clock_vote(unsigned long vote, struct hci_uart *hu)
{
	struct qca_data *qca = hu->priv;
	unsigned int diff;

	bool old_vote = (qca->tx_vote | qca->rx_vote);
	bool new_vote;

	switch (vote) {
	case HCI_IBS_VOTE_STATS_UPDATE:
		diff = jiffies_to_msecs(jiffies - qca->vote_last_jif);

		if (old_vote)
			qca->vote_off_ms += diff;
		else
			qca->vote_on_ms += diff;
		return;

	case HCI_IBS_TX_VOTE_CLOCK_ON:
		qca->tx_vote = true;
		qca->tx_votes_on++;
		new_vote = true;
		break;

	case HCI_IBS_RX_VOTE_CLOCK_ON:
		qca->rx_vote = true;
		qca->rx_votes_on++;
		new_vote = true;
		break;

	case HCI_IBS_TX_VOTE_CLOCK_OFF:
		qca->tx_vote = false;
		qca->tx_votes_off++;
		new_vote = qca->rx_vote | qca->tx_vote;
		break;

	case HCI_IBS_RX_VOTE_CLOCK_OFF:
		qca->rx_vote = false;
		qca->rx_votes_off++;
		new_vote = qca->rx_vote | qca->tx_vote;
		break;

	default:
		BT_ERR("Voting irregularity");
		return;
	}

	if (new_vote != old_vote) {
		if (new_vote)
			__serial_clock_on(hu->tty);
		else
			__serial_clock_off(hu->tty);

		BT_DBG("Vote serial clock %s(%s)", new_vote ? "true" : "false",
		       vote ? "true" : "false");

		diff = jiffies_to_msecs(jiffies - qca->vote_last_jif);

		if (new_vote) {
			qca->votes_on++;
			qca->vote_off_ms += diff;
		} else {
			qca->votes_off++;
			qca->vote_on_ms += diff;
		}
		qca->vote_last_jif = jiffies;
	}
}

/* Builds and sends an HCI_IBS command packet.
 * These are very simple packets with only 1 cmd byte.
 */
static int send_hci_ibs_cmd(u8 cmd, struct hci_uart *hu)
{
	int err = 0;
	struct sk_buff *skb = NULL;
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p send hci ibs cmd 0x%x", hu, cmd);

	skb = bt_skb_alloc(1, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("Failed to allocate memory for HCI_IBS packet");
		return -ENOMEM;
	}

	/* Assign HCI_IBS type */
	skb_put_u8(skb, cmd);

	skb_queue_tail(&qca->txq, skb);

	return err;
}

static void qca_wq_awake_device(struct work_struct *work)
{
	struct qca_data *qca = container_of(work, struct qca_data,
					    ws_awake_device);
	struct hci_uart *hu = qca->hu;
	unsigned long retrans_delay;
	unsigned long flags;

	BT_DBG("hu %p wq awake device", hu);

	/* Vote for serial clock */
	serial_clock_vote(HCI_IBS_TX_VOTE_CLOCK_ON, hu);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);

	/* Send wake indication to device */
	if (send_hci_ibs_cmd(HCI_IBS_WAKE_IND, hu) < 0)
		BT_ERR("Failed to send WAKE to device");

	qca->ibs_sent_wakes++;

	/* Start retransmit timer */
	retrans_delay = msecs_to_jiffies(qca->wake_retrans);
	mod_timer(&qca->wake_retrans_timer, jiffies + retrans_delay);

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	/* Actually send the packets */
	hci_uart_tx_wakeup(hu);
}

static void qca_wq_awake_rx(struct work_struct *work)
{
	struct qca_data *qca = container_of(work, struct qca_data,
					    ws_awake_rx);
	struct hci_uart *hu = qca->hu;
	unsigned long flags;

	BT_DBG("hu %p wq awake rx", hu);

	serial_clock_vote(HCI_IBS_RX_VOTE_CLOCK_ON, hu);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);
	qca->rx_ibs_state = HCI_IBS_RX_AWAKE;

	/* Always acknowledge device wake up,
	 * sending IBS message doesn't count as TX ON.
	 */
	if (send_hci_ibs_cmd(HCI_IBS_WAKE_ACK, hu) < 0)
		BT_ERR("Failed to acknowledge device wake up");

	qca->ibs_sent_wacks++;

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	/* Actually send the packets */
	hci_uart_tx_wakeup(hu);
}

static void qca_wq_serial_rx_clock_vote_off(struct work_struct *work)
{
	struct qca_data *qca = container_of(work, struct qca_data,
					    ws_rx_vote_off);
	struct hci_uart *hu = qca->hu;

	BT_DBG("hu %p rx clock vote off", hu);

	serial_clock_vote(HCI_IBS_RX_VOTE_CLOCK_OFF, hu);
}

static void qca_wq_serial_tx_clock_vote_off(struct work_struct *work)
{
	struct qca_data *qca = container_of(work, struct qca_data,
					    ws_tx_vote_off);
	struct hci_uart *hu = qca->hu;

	BT_DBG("hu %p tx clock vote off", hu);

	/* Run HCI tx handling unlocked */
	hci_uart_tx_wakeup(hu);

	/* Now that message queued to tty driver, vote for tty clocks off.
	 * It is up to the tty driver to pend the clocks off until tx done.
	 */
	serial_clock_vote(HCI_IBS_TX_VOTE_CLOCK_OFF, hu);
}

static void hci_ibs_tx_idle_timeout(struct timer_list *t)
{
	struct qca_data *qca = from_timer(qca, t, tx_idle_timer);
	struct hci_uart *hu = qca->hu;
	unsigned long flags;

	BT_DBG("hu %p idle timeout in %d state", hu, qca->tx_ibs_state);

	spin_lock_irqsave_nested(&qca->hci_ibs_lock,
				 flags, SINGLE_DEPTH_NESTING);

	switch (qca->tx_ibs_state) {
	case HCI_IBS_TX_AWAKE:
		/* TX_IDLE, go to SLEEP */
		if (send_hci_ibs_cmd(HCI_IBS_SLEEP_IND, hu) < 0) {
			BT_ERR("Failed to send SLEEP to device");
			break;
		}
		qca->tx_ibs_state = HCI_IBS_TX_ASLEEP;
		qca->ibs_sent_slps++;
		queue_work(qca->workqueue, &qca->ws_tx_vote_off);
		break;

	case HCI_IBS_TX_ASLEEP:
	case HCI_IBS_TX_WAKING:
		/* Fall through */

	default:
		BT_ERR("Spurious timeout tx state %d", qca->tx_ibs_state);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
}

static void hci_ibs_wake_retrans_timeout(struct timer_list *t)
{
	struct qca_data *qca = from_timer(qca, t, wake_retrans_timer);
	struct hci_uart *hu = qca->hu;
	unsigned long flags, retrans_delay;
	bool retransmit = false;

	BT_DBG("hu %p wake retransmit timeout in %d state",
		hu, qca->tx_ibs_state);

	spin_lock_irqsave_nested(&qca->hci_ibs_lock,
				 flags, SINGLE_DEPTH_NESTING);

	switch (qca->tx_ibs_state) {
	case HCI_IBS_TX_WAKING:
		/* No WAKE_ACK, retransmit WAKE */
		retransmit = true;
		if (send_hci_ibs_cmd(HCI_IBS_WAKE_IND, hu) < 0) {
			BT_ERR("Failed to acknowledge device wake up");
			break;
		}
		qca->ibs_sent_wakes++;
		retrans_delay = msecs_to_jiffies(qca->wake_retrans);
		mod_timer(&qca->wake_retrans_timer, jiffies + retrans_delay);
		break;

	case HCI_IBS_TX_ASLEEP:
	case HCI_IBS_TX_AWAKE:
		/* Fall through */

	default:
		BT_ERR("Spurious timeout tx state %d", qca->tx_ibs_state);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	if (retransmit)
		hci_uart_tx_wakeup(hu);
}

/* Initialize protocol */
static int qca_open(struct hci_uart *hu)
{
	struct qca_serdev *qcadev;
	struct qca_data *qca;
	int ret;

	BT_DBG("hu %p qca_open", hu);

	if (!hci_uart_has_flow_control(hu))
		return -EOPNOTSUPP;

	qca = kzalloc(sizeof(struct qca_data), GFP_KERNEL);
	if (!qca)
		return -ENOMEM;

	skb_queue_head_init(&qca->txq);
	skb_queue_head_init(&qca->tx_wait_q);
	spin_lock_init(&qca->hci_ibs_lock);
	qca->workqueue = alloc_ordered_workqueue("qca_wq", 0);
	if (!qca->workqueue) {
		BT_ERR("QCA Workqueue not initialized properly");
		kfree(qca);
		return -ENOMEM;
	}

	INIT_WORK(&qca->ws_awake_rx, qca_wq_awake_rx);
	INIT_WORK(&qca->ws_awake_device, qca_wq_awake_device);
	INIT_WORK(&qca->ws_rx_vote_off, qca_wq_serial_rx_clock_vote_off);
	INIT_WORK(&qca->ws_tx_vote_off, qca_wq_serial_tx_clock_vote_off);

	qca->hu = hu;
	init_completion(&qca->drop_ev_comp);

	/* Assume we start with both sides asleep -- extra wakes OK */
	qca->tx_ibs_state = HCI_IBS_TX_ASLEEP;
	qca->rx_ibs_state = HCI_IBS_RX_ASLEEP;

	/* clocks actually on, but we start votes off */
	qca->tx_vote = false;
	qca->rx_vote = false;
	qca->flags = 0;

	qca->ibs_sent_wacks = 0;
	qca->ibs_sent_slps = 0;
	qca->ibs_sent_wakes = 0;
	qca->ibs_recv_wacks = 0;
	qca->ibs_recv_slps = 0;
	qca->ibs_recv_wakes = 0;
	qca->vote_last_jif = jiffies;
	qca->vote_on_ms = 0;
	qca->vote_off_ms = 0;
	qca->votes_on = 0;
	qca->votes_off = 0;
	qca->tx_votes_on = 0;
	qca->tx_votes_off = 0;
	qca->rx_votes_on = 0;
	qca->rx_votes_off = 0;

	hu->priv = qca;

	if (hu->serdev) {

		qcadev = serdev_device_get_drvdata(hu->serdev);
		if (!qca_is_wcn399x(qcadev->btsoc_type)) {
			gpiod_set_value_cansleep(qcadev->bt_en, 1);
			/* Controller needs time to bootup. */
			msleep(150);
		} else {
			hu->init_speed = qcadev->init_speed;
			hu->oper_speed = qcadev->oper_speed;
			ret = qca_power_setup(hu, true);
			if (ret) {
				destroy_workqueue(qca->workqueue);
				kfree_skb(qca->rx_skb);
				hu->priv = NULL;
				kfree(qca);
				return ret;
			}
		}
	}

	timer_setup(&qca->wake_retrans_timer, hci_ibs_wake_retrans_timeout, 0);
	qca->wake_retrans = IBS_WAKE_RETRANS_TIMEOUT_MS;

	timer_setup(&qca->tx_idle_timer, hci_ibs_tx_idle_timeout, 0);
	qca->tx_idle_delay = IBS_TX_IDLE_TIMEOUT_MS;

	BT_DBG("HCI_UART_QCA open, tx_idle_delay=%u, wake_retrans=%u",
	       qca->tx_idle_delay, qca->wake_retrans);

	return 0;
}

static void qca_debugfs_init(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;
	struct dentry *ibs_dir;
	umode_t mode;

	if (!hdev->debugfs)
		return;

	ibs_dir = debugfs_create_dir("ibs", hdev->debugfs);

	/* read only */
	mode = S_IRUGO;
	debugfs_create_u8("tx_ibs_state", mode, ibs_dir, &qca->tx_ibs_state);
	debugfs_create_u8("rx_ibs_state", mode, ibs_dir, &qca->rx_ibs_state);
	debugfs_create_u64("ibs_sent_sleeps", mode, ibs_dir,
			   &qca->ibs_sent_slps);
	debugfs_create_u64("ibs_sent_wakes", mode, ibs_dir,
			   &qca->ibs_sent_wakes);
	debugfs_create_u64("ibs_sent_wake_acks", mode, ibs_dir,
			   &qca->ibs_sent_wacks);
	debugfs_create_u64("ibs_recv_sleeps", mode, ibs_dir,
			   &qca->ibs_recv_slps);
	debugfs_create_u64("ibs_recv_wakes", mode, ibs_dir,
			   &qca->ibs_recv_wakes);
	debugfs_create_u64("ibs_recv_wake_acks", mode, ibs_dir,
			   &qca->ibs_recv_wacks);
	debugfs_create_bool("tx_vote", mode, ibs_dir, &qca->tx_vote);
	debugfs_create_u64("tx_votes_on", mode, ibs_dir, &qca->tx_votes_on);
	debugfs_create_u64("tx_votes_off", mode, ibs_dir, &qca->tx_votes_off);
	debugfs_create_bool("rx_vote", mode, ibs_dir, &qca->rx_vote);
	debugfs_create_u64("rx_votes_on", mode, ibs_dir, &qca->rx_votes_on);
	debugfs_create_u64("rx_votes_off", mode, ibs_dir, &qca->rx_votes_off);
	debugfs_create_u64("votes_on", mode, ibs_dir, &qca->votes_on);
	debugfs_create_u64("votes_off", mode, ibs_dir, &qca->votes_off);
	debugfs_create_u32("vote_on_ms", mode, ibs_dir, &qca->vote_on_ms);
	debugfs_create_u32("vote_off_ms", mode, ibs_dir, &qca->vote_off_ms);

	/* read/write */
	mode = S_IRUGO | S_IWUSR;
	debugfs_create_u32("wake_retrans", mode, ibs_dir, &qca->wake_retrans);
	debugfs_create_u32("tx_idle_delay", mode, ibs_dir,
			   &qca->tx_idle_delay);
}

/* Flush protocol data */
static int qca_flush(struct hci_uart *hu)
{
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p qca flush", hu);

	skb_queue_purge(&qca->tx_wait_q);
	skb_queue_purge(&qca->txq);

	return 0;
}

/* Close protocol */
static int qca_close(struct hci_uart *hu)
{
	struct qca_serdev *qcadev;
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p qca close", hu);

	serial_clock_vote(HCI_IBS_VOTE_STATS_UPDATE, hu);

	skb_queue_purge(&qca->tx_wait_q);
	skb_queue_purge(&qca->txq);
	del_timer(&qca->tx_idle_timer);
	del_timer(&qca->wake_retrans_timer);
	destroy_workqueue(qca->workqueue);
	qca->hu = NULL;

	if (hu->serdev) {
		qcadev = serdev_device_get_drvdata(hu->serdev);
		if (qca_is_wcn399x(qcadev->btsoc_type))
			qca_power_shutdown(hu);
		else
			gpiod_set_value_cansleep(qcadev->bt_en, 0);

	}

	kfree_skb(qca->rx_skb);

	hu->priv = NULL;

	kfree(qca);

	return 0;
}

/* Called upon a wake-up-indication from the device.
 */
static void device_want_to_wakeup(struct hci_uart *hu)
{
	unsigned long flags;
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p want to wake up", hu);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);

	qca->ibs_recv_wakes++;

	switch (qca->rx_ibs_state) {
	case HCI_IBS_RX_ASLEEP:
		/* Make sure clock is on - we may have turned clock off since
		 * receiving the wake up indicator awake rx clock.
		 */
		queue_work(qca->workqueue, &qca->ws_awake_rx);
		spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
		return;

	case HCI_IBS_RX_AWAKE:
		/* Always acknowledge device wake up,
		 * sending IBS message doesn't count as TX ON.
		 */
		if (send_hci_ibs_cmd(HCI_IBS_WAKE_ACK, hu) < 0) {
			BT_ERR("Failed to acknowledge device wake up");
			break;
		}
		qca->ibs_sent_wacks++;
		break;

	default:
		/* Any other state is illegal */
		BT_ERR("Received HCI_IBS_WAKE_IND in rx state %d",
		       qca->rx_ibs_state);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	/* Actually send the packets */
	hci_uart_tx_wakeup(hu);
}

/* Called upon a sleep-indication from the device.
 */
static void device_want_to_sleep(struct hci_uart *hu)
{
	unsigned long flags;
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p want to sleep in %d state", hu, qca->rx_ibs_state);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);

	qca->ibs_recv_slps++;

	switch (qca->rx_ibs_state) {
	case HCI_IBS_RX_AWAKE:
		/* Update state */
		qca->rx_ibs_state = HCI_IBS_RX_ASLEEP;
		/* Vote off rx clock under workqueue */
		queue_work(qca->workqueue, &qca->ws_rx_vote_off);
		break;

	case HCI_IBS_RX_ASLEEP:
		break;

	default:
		/* Any other state is illegal */
		BT_ERR("Received HCI_IBS_SLEEP_IND in rx state %d",
		       qca->rx_ibs_state);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
}

/* Called upon wake-up-acknowledgement from the device
 */
static void device_woke_up(struct hci_uart *hu)
{
	unsigned long flags, idle_delay;
	struct qca_data *qca = hu->priv;
	struct sk_buff *skb = NULL;

	BT_DBG("hu %p woke up", hu);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);

	qca->ibs_recv_wacks++;

	switch (qca->tx_ibs_state) {
	case HCI_IBS_TX_AWAKE:
		/* Expect one if we send 2 WAKEs */
		BT_DBG("Received HCI_IBS_WAKE_ACK in tx state %d",
		       qca->tx_ibs_state);
		break;

	case HCI_IBS_TX_WAKING:
		/* Send pending packets */
		while ((skb = skb_dequeue(&qca->tx_wait_q)))
			skb_queue_tail(&qca->txq, skb);

		/* Switch timers and change state to HCI_IBS_TX_AWAKE */
		del_timer(&qca->wake_retrans_timer);
		idle_delay = msecs_to_jiffies(qca->tx_idle_delay);
		mod_timer(&qca->tx_idle_timer, jiffies + idle_delay);
		qca->tx_ibs_state = HCI_IBS_TX_AWAKE;
		break;

	case HCI_IBS_TX_ASLEEP:
		/* Fall through */

	default:
		BT_ERR("Received HCI_IBS_WAKE_ACK in tx state %d",
		       qca->tx_ibs_state);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	/* Actually send the packets */
	hci_uart_tx_wakeup(hu);
}

/* Enqueue frame for transmittion (padding, crc, etc) may be called from
 * two simultaneous tasklets.
 */
static int qca_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	unsigned long flags = 0, idle_delay;
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p qca enq skb %p tx_ibs_state %d", hu, skb,
	       qca->tx_ibs_state);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);

	/* Don't go to sleep in middle of patch download or
	 * Out-Of-Band(GPIOs control) sleep is selected.
	 */
	if (!test_bit(QCA_IBS_ENABLED, &qca->flags)) {
		skb_queue_tail(&qca->txq, skb);
		spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
		return 0;
	}

	/* Act according to current state */
	switch (qca->tx_ibs_state) {
	case HCI_IBS_TX_AWAKE:
		BT_DBG("Device awake, sending normally");
		skb_queue_tail(&qca->txq, skb);
		idle_delay = msecs_to_jiffies(qca->tx_idle_delay);
		mod_timer(&qca->tx_idle_timer, jiffies + idle_delay);
		break;

	case HCI_IBS_TX_ASLEEP:
		BT_DBG("Device asleep, waking up and queueing packet");
		/* Save packet for later */
		skb_queue_tail(&qca->tx_wait_q, skb);

		qca->tx_ibs_state = HCI_IBS_TX_WAKING;
		/* Schedule a work queue to wake up device */
		queue_work(qca->workqueue, &qca->ws_awake_device);
		break;

	case HCI_IBS_TX_WAKING:
		BT_DBG("Device waking up, queueing packet");
		/* Transient state; just keep packet for later */
		skb_queue_tail(&qca->tx_wait_q, skb);
		break;

	default:
		BT_ERR("Illegal tx state: %d (losing packet)",
		       qca->tx_ibs_state);
		kfree_skb(skb);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	return 0;
}

static int qca_ibs_sleep_ind(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);

	BT_DBG("hu %p recv hci ibs cmd 0x%x", hu, HCI_IBS_SLEEP_IND);

	device_want_to_sleep(hu);

	kfree_skb(skb);
	return 0;
}

static int qca_ibs_wake_ind(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);

	BT_DBG("hu %p recv hci ibs cmd 0x%x", hu, HCI_IBS_WAKE_IND);

	device_want_to_wakeup(hu);

	kfree_skb(skb);
	return 0;
}

static int qca_ibs_wake_ack(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);

	BT_DBG("hu %p recv hci ibs cmd 0x%x", hu, HCI_IBS_WAKE_ACK);

	device_woke_up(hu);

	kfree_skb(skb);
	return 0;
}

static int qca_recv_acl_data(struct hci_dev *hdev, struct sk_buff *skb)
{
	/* We receive debug logs from chip as an ACL packets.
	 * Instead of sending the data to ACL to decode the
	 * received data, we are pushing them to the above layers
	 * as a diagnostic packet.
	 */
	if (get_unaligned_le16(skb->data) == QCA_DEBUG_HANDLE)
		return hci_recv_diag(hdev, skb);

	return hci_recv_frame(hdev, skb);
}

static int qca_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;

	if (test_bit(QCA_DROP_VENDOR_EVENT, &qca->flags)) {
		struct hci_event_hdr *hdr = (void *)skb->data;

		/* For the WCN3990 the vendor command for a baudrate change
		 * isn't sent as synchronous HCI command, because the
		 * controller sends the corresponding vendor event with the
		 * new baudrate. The event is received and properly decoded
		 * after changing the baudrate of the host port. It needs to
		 * be dropped, otherwise it can be misinterpreted as
		 * response to a later firmware download command (also a
		 * vendor command).
		 */

		if (hdr->evt == HCI_EV_VENDOR)
			complete(&qca->drop_ev_comp);

		kfree_skb(skb);

		return 0;
	}

	return hci_recv_frame(hdev, skb);
}

#define QCA_IBS_SLEEP_IND_EVENT \
	.type = HCI_IBS_SLEEP_IND, \
	.hlen = 0, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = HCI_MAX_IBS_SIZE

#define QCA_IBS_WAKE_IND_EVENT \
	.type = HCI_IBS_WAKE_IND, \
	.hlen = 0, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = HCI_MAX_IBS_SIZE

#define QCA_IBS_WAKE_ACK_EVENT \
	.type = HCI_IBS_WAKE_ACK, \
	.hlen = 0, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = HCI_MAX_IBS_SIZE

static const struct h4_recv_pkt qca_recv_pkts[] = {
	{ H4_RECV_ACL,             .recv = qca_recv_acl_data },
	{ H4_RECV_SCO,             .recv = hci_recv_frame    },
	{ H4_RECV_EVENT,           .recv = qca_recv_event    },
	{ QCA_IBS_WAKE_IND_EVENT,  .recv = qca_ibs_wake_ind  },
	{ QCA_IBS_WAKE_ACK_EVENT,  .recv = qca_ibs_wake_ack  },
	{ QCA_IBS_SLEEP_IND_EVENT, .recv = qca_ibs_sleep_ind },
};

static int qca_recv(struct hci_uart *hu, const void *data, int count)
{
	struct qca_data *qca = hu->priv;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;

	qca->rx_skb = h4_recv_buf(hu->hdev, qca->rx_skb, data, count,
				  qca_recv_pkts, ARRAY_SIZE(qca_recv_pkts));
	if (IS_ERR(qca->rx_skb)) {
		int err = PTR_ERR(qca->rx_skb);
		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		qca->rx_skb = NULL;
		return err;
	}

	return count;
}

static struct sk_buff *qca_dequeue(struct hci_uart *hu)
{
	struct qca_data *qca = hu->priv;

	return skb_dequeue(&qca->txq);
}

static uint8_t qca_get_baudrate_value(int speed)
{
	switch (speed) {
	case 9600:
		return QCA_BAUDRATE_9600;
	case 19200:
		return QCA_BAUDRATE_19200;
	case 38400:
		return QCA_BAUDRATE_38400;
	case 57600:
		return QCA_BAUDRATE_57600;
	case 115200:
		return QCA_BAUDRATE_115200;
	case 230400:
		return QCA_BAUDRATE_230400;
	case 460800:
		return QCA_BAUDRATE_460800;
	case 500000:
		return QCA_BAUDRATE_500000;
	case 921600:
		return QCA_BAUDRATE_921600;
	case 1000000:
		return QCA_BAUDRATE_1000000;
	case 2000000:
		return QCA_BAUDRATE_2000000;
	case 3000000:
		return QCA_BAUDRATE_3000000;
	case 3200000:
		return QCA_BAUDRATE_3200000;
	case 3500000:
		return QCA_BAUDRATE_3500000;
	default:
		return QCA_BAUDRATE_115200;
	}
}

static int qca_set_baudrate(struct hci_dev *hdev, uint8_t baudrate)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;
	struct sk_buff *skb;
	u8 cmd[] = { 0x01, 0x48, 0xFC, 0x01, 0x00 };

	if (baudrate > QCA_BAUDRATE_3200000)
		return -EINVAL;

	cmd[4] = baudrate;

	skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hdev, "Failed to allocate baudrate packet");
		return -ENOMEM;
	}

	/* Assign commands to change baudrate and packet type. */
	skb_put_data(skb, cmd, sizeof(cmd));
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;

	skb_queue_tail(&qca->txq, skb);
	hci_uart_tx_wakeup(hu);

	/* Wait for the baudrate change request to be sent */

	while (!skb_queue_empty(&qca->txq))
		usleep_range(100, 200);

	if (hu->serdev)
		serdev_device_wait_until_sent(hu->serdev,
		      msecs_to_jiffies(CMD_TRANS_TIMEOUT_MS));

	/* Give the controller time to process the request */
	if (qca_is_wcn399x(qca_soc_type(hu)))
		msleep(10);
	else
		msleep(300);

	return 0;
}

static inline void host_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	if (hu->serdev)
		serdev_device_set_baudrate(hu->serdev, speed);
	else
		hci_uart_set_baudrate(hu, speed);
}

static int qca_send_power_pulse(struct hci_uart *hu, bool on)
{
	int ret;
	int timeout = msecs_to_jiffies(CMD_TRANS_TIMEOUT_MS);
	u8 cmd = on ? QCA_WCN3990_POWERON_PULSE : QCA_WCN3990_POWEROFF_PULSE;

	/* These power pulses are single byte command which are sent
	 * at required baudrate to wcn3990. On wcn3990, we have an external
	 * circuit at Tx pin which decodes the pulse sent at specific baudrate.
	 * For example, wcn3990 supports RF COEX antenna for both Wi-Fi/BT
	 * and also we use the same power inputs to turn on and off for
	 * Wi-Fi/BT. Powering up the power sources will not enable BT, until
	 * we send a power on pulse at 115200 bps. This algorithm will help to
	 * save power. Disabling hardware flow control is mandatory while
	 * sending power pulses to SoC.
	 */
	bt_dev_dbg(hu->hdev, "sending power pulse %02x to controller", cmd);

	serdev_device_write_flush(hu->serdev);
	hci_uart_set_flow_control(hu, true);
	ret = serdev_device_write_buf(hu->serdev, &cmd, sizeof(cmd));
	if (ret < 0) {
		bt_dev_err(hu->hdev, "failed to send power pulse %02x", cmd);
		return ret;
	}

	serdev_device_wait_until_sent(hu->serdev, timeout);
	hci_uart_set_flow_control(hu, false);

	/* Give to controller time to boot/shutdown */
	if (on)
		msleep(100);
	else
		msleep(10);

	return 0;
}

static unsigned int qca_get_speed(struct hci_uart *hu,
				  enum qca_speed_type speed_type)
{
	unsigned int speed = 0;

	if (speed_type == QCA_INIT_SPEED) {
		if (hu->init_speed)
			speed = hu->init_speed;
		else if (hu->proto->init_speed)
			speed = hu->proto->init_speed;
	} else {
		if (hu->oper_speed)
			speed = hu->oper_speed;
		else if (hu->proto->oper_speed)
			speed = hu->proto->oper_speed;
	}

	return speed;
}

static int qca_check_speeds(struct hci_uart *hu)
{
	if (qca_is_wcn399x(qca_soc_type(hu))) {
		if (!qca_get_speed(hu, QCA_INIT_SPEED) &&
		    !qca_get_speed(hu, QCA_OPER_SPEED))
			return -EINVAL;
	} else {
		if (!qca_get_speed(hu, QCA_INIT_SPEED) ||
		    !qca_get_speed(hu, QCA_OPER_SPEED))
			return -EINVAL;
	}

	return 0;
}

static int qca_set_speed(struct hci_uart *hu, enum qca_speed_type speed_type)
{
	unsigned int speed, qca_baudrate;
	struct qca_data *qca = hu->priv;
	int ret = 0;

	if (speed_type == QCA_INIT_SPEED) {
		speed = qca_get_speed(hu, QCA_INIT_SPEED);
		if (speed)
			host_set_baudrate(hu, speed);
	} else {
		enum qca_btsoc_type soc_type = qca_soc_type(hu);

		speed = qca_get_speed(hu, QCA_OPER_SPEED);
		if (!speed)
			return 0;

		/* Disable flow control for wcn3990 to deassert RTS while
		 * changing the baudrate of chip and host.
		 */
		if (qca_is_wcn399x(soc_type))
			hci_uart_set_flow_control(hu, true);

		if (soc_type == QCA_WCN3990) {
			reinit_completion(&qca->drop_ev_comp);
			set_bit(QCA_DROP_VENDOR_EVENT, &qca->flags);
		}

		qca_baudrate = qca_get_baudrate_value(speed);
		bt_dev_dbg(hu->hdev, "Set UART speed to %d", speed);
		ret = qca_set_baudrate(hu->hdev, qca_baudrate);
		if (ret)
			goto error;

		host_set_baudrate(hu, speed);

error:
		if (qca_is_wcn399x(soc_type))
			hci_uart_set_flow_control(hu, false);

		if (soc_type == QCA_WCN3990) {
			/* Wait for the controller to send the vendor event
			 * for the baudrate change command.
			 */
			if (!wait_for_completion_timeout(&qca->drop_ev_comp,
						 msecs_to_jiffies(100))) {
				bt_dev_err(hu->hdev,
					   "Failed to change controller baudrate\n");
				ret = -ETIMEDOUT;
			}

			clear_bit(QCA_DROP_VENDOR_EVENT, &qca->flags);
		}
	}

	return ret;
}

static int qca_wcn3990_init(struct hci_uart *hu)
{
	struct qca_serdev *qcadev;
	int ret;

	/* Check for vregs status, may be hci down has turned
	 * off the voltage regulator.
	 */
	qcadev = serdev_device_get_drvdata(hu->serdev);
	if (!qcadev->bt_power->vregs_on) {
		serdev_device_close(hu->serdev);
		ret = qca_power_setup(hu, true);
		if (ret)
			return ret;

		ret = serdev_device_open(hu->serdev);
		if (ret) {
			bt_dev_err(hu->hdev, "failed to open port");
			return ret;
		}
	}

	/* Forcefully enable wcn3990 to enter in to boot mode. */
	host_set_baudrate(hu, 2400);
	ret = qca_send_power_pulse(hu, false);
	if (ret)
		return ret;

	qca_set_speed(hu, QCA_INIT_SPEED);
	ret = qca_send_power_pulse(hu, true);
	if (ret)
		return ret;

	/* Now the device is in ready state to communicate with host.
	 * To sync host with device we need to reopen port.
	 * Without this, we will have RTS and CTS synchronization
	 * issues.
	 */
	serdev_device_close(hu->serdev);
	ret = serdev_device_open(hu->serdev);
	if (ret) {
		bt_dev_err(hu->hdev, "failed to open port");
		return ret;
	}

	hci_uart_set_flow_control(hu, false);

	return 0;
}

static int qca_setup(struct hci_uart *hu)
{
	struct hci_dev *hdev = hu->hdev;
	struct qca_data *qca = hu->priv;
	unsigned int speed, qca_baudrate = QCA_BAUDRATE_115200;
	enum qca_btsoc_type soc_type = qca_soc_type(hu);
	const char *firmware_name = qca_get_firmware_name(hu);
	int ret;
	int soc_ver = 0;

	ret = qca_check_speeds(hu);
	if (ret)
		return ret;

	/* Patch downloading has to be done without IBS mode */
	clear_bit(QCA_IBS_ENABLED, &qca->flags);

	if (qca_is_wcn399x(soc_type)) {
		bt_dev_info(hdev, "setting up wcn3990");

		/* Enable NON_PERSISTENT_SETUP QUIRK to ensure to execute
		 * setup for every hci up.
		 */
		set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);
		set_bit(HCI_QUIRK_USE_BDADDR_PROPERTY, &hdev->quirks);
		hu->hdev->shutdown = qca_power_off;
		ret = qca_wcn3990_init(hu);
		if (ret)
			return ret;

		ret = qca_read_soc_version(hdev, &soc_ver);
		if (ret)
			return ret;
	} else {
		bt_dev_info(hdev, "ROME setup");
		qca_set_speed(hu, QCA_INIT_SPEED);
	}

	/* Setup user speed if needed */
	speed = qca_get_speed(hu, QCA_OPER_SPEED);
	if (speed) {
		ret = qca_set_speed(hu, QCA_OPER_SPEED);
		if (ret)
			return ret;

		qca_baudrate = qca_get_baudrate_value(speed);
	}

	if (!qca_is_wcn399x(soc_type)) {
		/* Get QCA version information */
		ret = qca_read_soc_version(hdev, &soc_ver);
		if (ret)
			return ret;
	}

	bt_dev_info(hdev, "QCA controller version 0x%08x", soc_ver);
	/* Setup patch / NVM configurations */
	ret = qca_uart_setup(hdev, qca_baudrate, soc_type, soc_ver,
			firmware_name);
	if (!ret) {
		set_bit(QCA_IBS_ENABLED, &qca->flags);
		qca_debugfs_init(hdev);
	} else if (ret == -ENOENT) {
		/* No patch/nvm-config found, run with original fw/config */
		ret = 0;
	} else if (ret == -EAGAIN) {
		/*
		 * Userspace firmware loader will return -EAGAIN in case no
		 * patch/nvm-config is found, so run with original fw/config.
		 */
		ret = 0;
	}

	/* Setup bdaddr */
	if (qca_is_wcn399x(soc_type))
		hu->hdev->set_bdaddr = qca_set_bdaddr;
	else
		hu->hdev->set_bdaddr = qca_set_bdaddr_rome;

	return ret;
}

static struct hci_uart_proto qca_proto = {
	.id		= HCI_UART_QCA,
	.name		= "QCA",
	.manufacturer	= 29,
	.init_speed	= 115200,
	.oper_speed	= 3000000,
	.open		= qca_open,
	.close		= qca_close,
	.flush		= qca_flush,
	.setup		= qca_setup,
	.recv		= qca_recv,
	.enqueue	= qca_enqueue,
	.dequeue	= qca_dequeue,
};

static const struct qca_vreg_data qca_soc_data_wcn3990 = {
	.soc_type = QCA_WCN3990,
	.vregs = (struct qca_vreg []) {
		{ "vddio",   1800000, 1900000,  15000  },
		{ "vddxo",   1800000, 1900000,  80000  },
		{ "vddrf",   1300000, 1350000,  300000 },
		{ "vddch0",  3300000, 3400000,  450000 },
	},
	.num_vregs = 4,
};

static const struct qca_vreg_data qca_soc_data_wcn3998 = {
	.soc_type = QCA_WCN3998,
	.vregs = (struct qca_vreg []) {
		{ "vddio",   1800000, 1900000,  10000  },
		{ "vddxo",   1800000, 1900000,  80000  },
		{ "vddrf",   1300000, 1352000,  300000 },
		{ "vddch0",  3300000, 3300000,  450000 },
	},
	.num_vregs = 4,
};

static void qca_power_shutdown(struct hci_uart *hu)
{
	struct qca_data *qca = hu->priv;
	unsigned long flags;

	/* From this point we go into power off state. But serial port is
	 * still open, stop queueing the IBS data and flush all the buffered
	 * data in skb's.
	 */
	spin_lock_irqsave(&qca->hci_ibs_lock, flags);
	clear_bit(QCA_IBS_ENABLED, &qca->flags);
	qca_flush(hu);
	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	host_set_baudrate(hu, 2400);
	qca_send_power_pulse(hu, false);
	qca_power_setup(hu, false);
}

static int qca_power_off(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);

	/* Perform pre shutdown command */
	qca_send_pre_shutdown_cmd(hdev);

	qca_power_shutdown(hu);
	return 0;
}

static int qca_enable_regulator(struct qca_vreg vregs,
				struct regulator *regulator)
{
	int ret;

	ret = regulator_set_voltage(regulator, vregs.min_uV,
				    vregs.max_uV);
	if (ret)
		return ret;

	if (vregs.load_uA)
		ret = regulator_set_load(regulator,
					 vregs.load_uA);

	if (ret)
		return ret;

	return regulator_enable(regulator);

}

static void qca_disable_regulator(struct qca_vreg vregs,
				  struct regulator *regulator)
{
	regulator_disable(regulator);
	regulator_set_voltage(regulator, 0, vregs.max_uV);
	if (vregs.load_uA)
		regulator_set_load(regulator, 0);

}

static int qca_power_setup(struct hci_uart *hu, bool on)
{
	struct qca_vreg *vregs;
	struct regulator_bulk_data *vreg_bulk;
	struct qca_serdev *qcadev;
	int i, num_vregs, ret = 0;

	qcadev = serdev_device_get_drvdata(hu->serdev);
	if (!qcadev || !qcadev->bt_power || !qcadev->bt_power->vreg_data ||
	    !qcadev->bt_power->vreg_bulk)
		return -EINVAL;

	vregs = qcadev->bt_power->vreg_data->vregs;
	vreg_bulk = qcadev->bt_power->vreg_bulk;
	num_vregs = qcadev->bt_power->vreg_data->num_vregs;
	BT_DBG("on: %d", on);
	if (on && !qcadev->bt_power->vregs_on) {
		for (i = 0; i < num_vregs; i++) {
			ret = qca_enable_regulator(vregs[i],
						   vreg_bulk[i].consumer);
			if (ret)
				break;
		}

		if (ret) {
			BT_ERR("failed to enable regulator:%s", vregs[i].name);
			/* turn off regulators which are enabled */
			for (i = i - 1; i >= 0; i--)
				qca_disable_regulator(vregs[i],
						      vreg_bulk[i].consumer);
		} else {
			qcadev->bt_power->vregs_on = true;
		}
	} else if (!on && qcadev->bt_power->vregs_on) {
		/* turn off regulator in reverse order */
		i = qcadev->bt_power->vreg_data->num_vregs - 1;
		for ( ; i >= 0; i--)
			qca_disable_regulator(vregs[i], vreg_bulk[i].consumer);

		qcadev->bt_power->vregs_on = false;
	}

	return ret;
}

static int qca_init_regulators(struct qca_power *qca,
				const struct qca_vreg *vregs, size_t num_vregs)
{
	int i;

	qca->vreg_bulk = devm_kcalloc(qca->dev, num_vregs,
				      sizeof(struct regulator_bulk_data),
				      GFP_KERNEL);
	if (!qca->vreg_bulk)
		return -ENOMEM;

	for (i = 0; i < num_vregs; i++)
		qca->vreg_bulk[i].supply = vregs[i].name;

	return devm_regulator_bulk_get(qca->dev, num_vregs, qca->vreg_bulk);
}

static int qca_serdev_probe(struct serdev_device *serdev)
{
	struct qca_serdev *qcadev;
	const struct qca_vreg_data *data;
	int err;

	qcadev = devm_kzalloc(&serdev->dev, sizeof(*qcadev), GFP_KERNEL);
	if (!qcadev)
		return -ENOMEM;

	qcadev->serdev_hu.serdev = serdev;
	data = of_device_get_match_data(&serdev->dev);
	serdev_device_set_drvdata(serdev, qcadev);
	device_property_read_string(&serdev->dev, "firmware-name",
					 &qcadev->firmware_name);
	if (data && qca_is_wcn399x(data->soc_type)) {
		qcadev->btsoc_type = data->soc_type;
		qcadev->bt_power = devm_kzalloc(&serdev->dev,
						sizeof(struct qca_power),
						GFP_KERNEL);
		if (!qcadev->bt_power)
			return -ENOMEM;

		qcadev->bt_power->dev = &serdev->dev;
		qcadev->bt_power->vreg_data = data;
		err = qca_init_regulators(qcadev->bt_power, data->vregs,
					  data->num_vregs);
		if (err) {
			BT_ERR("Failed to init regulators:%d", err);
			goto out;
		}

		qcadev->bt_power->vregs_on = false;

		device_property_read_u32(&serdev->dev, "max-speed",
					 &qcadev->oper_speed);
		if (!qcadev->oper_speed)
			BT_DBG("UART will pick default operating speed");

		err = hci_uart_register_device(&qcadev->serdev_hu, &qca_proto);
		if (err) {
			BT_ERR("wcn3990 serdev registration failed");
			goto out;
		}
	} else {
		qcadev->btsoc_type = QCA_ROME;
		qcadev->bt_en = devm_gpiod_get(&serdev->dev, "enable",
					       GPIOD_OUT_LOW);
		if (IS_ERR(qcadev->bt_en)) {
			dev_err(&serdev->dev, "failed to acquire enable gpio\n");
			return PTR_ERR(qcadev->bt_en);
		}

		qcadev->susclk = devm_clk_get(&serdev->dev, NULL);
		if (IS_ERR(qcadev->susclk)) {
			dev_err(&serdev->dev, "failed to acquire clk\n");
			return PTR_ERR(qcadev->susclk);
		}

		err = clk_set_rate(qcadev->susclk, SUSCLK_RATE_32KHZ);
		if (err)
			return err;

		err = clk_prepare_enable(qcadev->susclk);
		if (err)
			return err;

		err = hci_uart_register_device(&qcadev->serdev_hu, &qca_proto);
		if (err)
			clk_disable_unprepare(qcadev->susclk);
	}

out:	return err;

}

static void qca_serdev_remove(struct serdev_device *serdev)
{
	struct qca_serdev *qcadev = serdev_device_get_drvdata(serdev);

	if (qca_is_wcn399x(qcadev->btsoc_type))
		qca_power_shutdown(&qcadev->serdev_hu);
	else
		clk_disable_unprepare(qcadev->susclk);

	hci_uart_unregister_device(&qcadev->serdev_hu);
}

static const struct of_device_id qca_bluetooth_of_match[] = {
	{ .compatible = "qcom,qca6174-bt" },
	{ .compatible = "qcom,wcn3990-bt", .data = &qca_soc_data_wcn3990},
	{ .compatible = "qcom,wcn3998-bt", .data = &qca_soc_data_wcn3998},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qca_bluetooth_of_match);

static struct serdev_device_driver qca_serdev_driver = {
	.probe = qca_serdev_probe,
	.remove = qca_serdev_remove,
	.driver = {
		.name = "hci_uart_qca",
		.of_match_table = qca_bluetooth_of_match,
	},
};

int __init qca_init(void)
{
	serdev_device_driver_register(&qca_serdev_driver);

	return hci_uart_register_proto(&qca_proto);
}

int __exit qca_deinit(void)
{
	serdev_device_driver_unregister(&qca_serdev_driver);

	return hci_uart_unregister_proto(&qca_proto);
}
