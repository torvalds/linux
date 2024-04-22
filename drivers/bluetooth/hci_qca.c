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
#include <linux/devcoredump.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/serdev.h>
#include <linux/mutex.h>
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
#define IBS_BTSOC_TX_IDLE_TIMEOUT_MS	200
#define IBS_HOST_TX_IDLE_TIMEOUT_MS	2000
#define CMD_TRANS_TIMEOUT_MS		100
#define MEMDUMP_TIMEOUT_MS		8000
#define IBS_DISABLE_SSR_TIMEOUT_MS \
	(MEMDUMP_TIMEOUT_MS + FW_DOWNLOAD_TIMEOUT_MS)
#define FW_DOWNLOAD_TIMEOUT_MS		3000

/* susclk rate */
#define SUSCLK_RATE_32KHZ	32768

/* Controller debug log header */
#define QCA_DEBUG_HANDLE	0x2EDC

/* max retry count when init fails */
#define MAX_INIT_RETRIES 3

/* Controller dump header */
#define QCA_SSR_DUMP_HANDLE		0x0108
#define QCA_DUMP_PACKET_SIZE		255
#define QCA_LAST_SEQUENCE_NUM		0xFFFF
#define QCA_CRASHBYTE_PACKET_LEN	1096
#define QCA_MEMDUMP_BYTE		0xFB

enum qca_flags {
	QCA_IBS_DISABLED,
	QCA_DROP_VENDOR_EVENT,
	QCA_SUSPENDING,
	QCA_MEMDUMP_COLLECTION,
	QCA_HW_ERROR_EVENT,
	QCA_SSR_TRIGGERED,
	QCA_BT_OFF,
	QCA_ROM_FW,
	QCA_DEBUGFS_CREATED,
};

enum qca_capabilities {
	QCA_CAP_WIDEBAND_SPEECH = BIT(0),
	QCA_CAP_VALID_LE_STATES = BIT(1),
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

/* Controller memory dump states */
enum qca_memdump_states {
	QCA_MEMDUMP_IDLE,
	QCA_MEMDUMP_COLLECTING,
	QCA_MEMDUMP_COLLECTED,
	QCA_MEMDUMP_TIMEOUT,
};

struct qca_memdump_data {
	char *memdump_buf_head;
	char *memdump_buf_tail;
	u32 current_seq_no;
	u32 received_dump;
	u32 ram_dump_size;
};

struct qca_memdump_event_hdr {
	__u8    evt;
	__u8    plen;
	__u16   opcode;
	__u16   seq_no;
	__u8    reserved;
} __packed;


struct qca_dump_size {
	u32 dump_size;
} __packed;

struct qca_data {
	struct hci_uart *hu;
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
	struct sk_buff_head tx_wait_q;	/* HCI_IBS wait queue	*/
	struct sk_buff_head rx_memdump_q;	/* Memdump wait queue	*/
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
	struct work_struct ctrl_memdump_evt;
	struct delayed_work ctrl_memdump_timeout;
	struct qca_memdump_data *qca_memdump;
	unsigned long flags;
	struct completion drop_ev_comp;
	wait_queue_head_t suspend_wait_q;
	enum qca_memdump_states memdump_state;
	struct mutex hci_memdump_lock;

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
	unsigned int load_uA;
};

struct qca_device_data {
	enum qca_btsoc_type soc_type;
	struct qca_vreg *vregs;
	size_t num_vregs;
	uint32_t capabilities;
};

/*
 * Platform data for the QCA Bluetooth power driver.
 */
struct qca_power {
	struct device *dev;
	struct regulator_bulk_data *vreg_bulk;
	int num_vregs;
	bool vregs_on;
};

struct qca_serdev {
	struct hci_uart	 serdev_hu;
	struct gpio_desc *bt_en;
	struct gpio_desc *sw_ctrl;
	struct clk	 *susclk;
	enum qca_btsoc_type btsoc_type;
	struct qca_power *bt_power;
	u32 init_speed;
	u32 oper_speed;
	bool bdaddr_property_broken;
	const char *firmware_name;
};

static int qca_regulator_enable(struct qca_serdev *qcadev);
static void qca_regulator_disable(struct qca_serdev *qcadev);
static void qca_power_shutdown(struct hci_uart *hu);
static int qca_power_off(struct hci_dev *hdev);
static void qca_controller_memdump(struct work_struct *work);

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
		break;

	case HCI_IBS_RX_VOTE_CLOCK_ON:
		qca->rx_vote = true;
		qca->rx_votes_on++;
		break;

	case HCI_IBS_TX_VOTE_CLOCK_OFF:
		qca->tx_vote = false;
		qca->tx_votes_off++;
		break;

	case HCI_IBS_RX_VOTE_CLOCK_OFF:
		qca->rx_vote = false;
		qca->rx_votes_off++;
		break;

	default:
		BT_ERR("Voting irregularity");
		return;
	}

	new_vote = qca->rx_vote | qca->tx_vote;

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

	/* Don't retransmit the HCI_IBS_WAKE_IND when suspending. */
	if (test_bit(QCA_SUSPENDING, &qca->flags)) {
		spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
		return;
	}

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
	default:
		BT_ERR("Spurious timeout tx state %d", qca->tx_ibs_state);
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	if (retransmit)
		hci_uart_tx_wakeup(hu);
}


static void qca_controller_memdump_timeout(struct work_struct *work)
{
	struct qca_data *qca = container_of(work, struct qca_data,
					ctrl_memdump_timeout.work);
	struct hci_uart *hu = qca->hu;

	mutex_lock(&qca->hci_memdump_lock);
	if (test_bit(QCA_MEMDUMP_COLLECTION, &qca->flags)) {
		qca->memdump_state = QCA_MEMDUMP_TIMEOUT;
		if (!test_bit(QCA_HW_ERROR_EVENT, &qca->flags)) {
			/* Inject hw error event to reset the device
			 * and driver.
			 */
			hci_reset_dev(hu->hdev);
		}
	}

	mutex_unlock(&qca->hci_memdump_lock);
}


/* Initialize protocol */
static int qca_open(struct hci_uart *hu)
{
	struct qca_serdev *qcadev;
	struct qca_data *qca;

	BT_DBG("hu %p qca_open", hu);

	if (!hci_uart_has_flow_control(hu))
		return -EOPNOTSUPP;

	qca = kzalloc(sizeof(struct qca_data), GFP_KERNEL);
	if (!qca)
		return -ENOMEM;

	skb_queue_head_init(&qca->txq);
	skb_queue_head_init(&qca->tx_wait_q);
	skb_queue_head_init(&qca->rx_memdump_q);
	spin_lock_init(&qca->hci_ibs_lock);
	mutex_init(&qca->hci_memdump_lock);
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
	INIT_WORK(&qca->ctrl_memdump_evt, qca_controller_memdump);
	INIT_DELAYED_WORK(&qca->ctrl_memdump_timeout,
			  qca_controller_memdump_timeout);
	init_waitqueue_head(&qca->suspend_wait_q);

	qca->hu = hu;
	init_completion(&qca->drop_ev_comp);

	/* Assume we start with both sides asleep -- extra wakes OK */
	qca->tx_ibs_state = HCI_IBS_TX_ASLEEP;
	qca->rx_ibs_state = HCI_IBS_RX_ASLEEP;

	qca->vote_last_jif = jiffies;

	hu->priv = qca;

	if (hu->serdev) {
		qcadev = serdev_device_get_drvdata(hu->serdev);

		switch (qcadev->btsoc_type) {
		case QCA_WCN3988:
		case QCA_WCN3990:
		case QCA_WCN3991:
		case QCA_WCN3998:
		case QCA_WCN6750:
			hu->init_speed = qcadev->init_speed;
			break;

		default:
			break;
		}

		if (qcadev->oper_speed)
			hu->oper_speed = qcadev->oper_speed;
	}

	timer_setup(&qca->wake_retrans_timer, hci_ibs_wake_retrans_timeout, 0);
	qca->wake_retrans = IBS_WAKE_RETRANS_TIMEOUT_MS;

	timer_setup(&qca->tx_idle_timer, hci_ibs_tx_idle_timeout, 0);
	qca->tx_idle_delay = IBS_HOST_TX_IDLE_TIMEOUT_MS;

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

	if (test_and_set_bit(QCA_DEBUGFS_CREATED, &qca->flags))
		return;

	ibs_dir = debugfs_create_dir("ibs", hdev->debugfs);

	/* read only */
	mode = 0444;
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
	mode = 0644;
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
	struct qca_data *qca = hu->priv;

	BT_DBG("hu %p qca close", hu);

	serial_clock_vote(HCI_IBS_VOTE_STATS_UPDATE, hu);

	skb_queue_purge(&qca->tx_wait_q);
	skb_queue_purge(&qca->txq);
	skb_queue_purge(&qca->rx_memdump_q);
	destroy_workqueue(qca->workqueue);
	del_timer_sync(&qca->tx_idle_timer);
	del_timer_sync(&qca->wake_retrans_timer);
	qca->hu = NULL;

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

	/* Don't wake the rx up when suspending. */
	if (test_bit(QCA_SUSPENDING, &qca->flags)) {
		spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
		return;
	}

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

	wake_up_interruptible(&qca->suspend_wait_q);

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

	/* Don't react to the wake-up-acknowledgment when suspending. */
	if (test_bit(QCA_SUSPENDING, &qca->flags)) {
		spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);
		return;
	}

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

	if (test_bit(QCA_SSR_TRIGGERED, &qca->flags)) {
		/* As SSR is in progress, ignore the packets */
		bt_dev_dbg(hu->hdev, "SSR is in progress");
		kfree_skb(skb);
		return 0;
	}

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	spin_lock_irqsave(&qca->hci_ibs_lock, flags);

	/* Don't go to sleep in middle of patch download or
	 * Out-Of-Band(GPIOs control) sleep is selected.
	 * Don't wake the device up when suspending.
	 */
	if (test_bit(QCA_IBS_DISABLED, &qca->flags) ||
	    test_bit(QCA_SUSPENDING, &qca->flags)) {
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
		dev_kfree_skb_irq(skb);
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

static void qca_controller_memdump(struct work_struct *work)
{
	struct qca_data *qca = container_of(work, struct qca_data,
					    ctrl_memdump_evt);
	struct hci_uart *hu = qca->hu;
	struct sk_buff *skb;
	struct qca_memdump_event_hdr *cmd_hdr;
	struct qca_memdump_data *qca_memdump = qca->qca_memdump;
	struct qca_dump_size *dump;
	char *memdump_buf;
	char nullBuff[QCA_DUMP_PACKET_SIZE] = { 0 };
	u16 seq_no;
	u32 dump_size;
	u32 rx_size;
	enum qca_btsoc_type soc_type = qca_soc_type(hu);

	while ((skb = skb_dequeue(&qca->rx_memdump_q))) {

		mutex_lock(&qca->hci_memdump_lock);
		/* Skip processing the received packets if timeout detected
		 * or memdump collection completed.
		 */
		if (qca->memdump_state == QCA_MEMDUMP_TIMEOUT ||
		    qca->memdump_state == QCA_MEMDUMP_COLLECTED) {
			mutex_unlock(&qca->hci_memdump_lock);
			return;
		}

		if (!qca_memdump) {
			qca_memdump = kzalloc(sizeof(struct qca_memdump_data),
					      GFP_ATOMIC);
			if (!qca_memdump) {
				mutex_unlock(&qca->hci_memdump_lock);
				return;
			}

			qca->qca_memdump = qca_memdump;
		}

		qca->memdump_state = QCA_MEMDUMP_COLLECTING;
		cmd_hdr = (void *) skb->data;
		seq_no = __le16_to_cpu(cmd_hdr->seq_no);
		skb_pull(skb, sizeof(struct qca_memdump_event_hdr));

		if (!seq_no) {

			/* This is the first frame of memdump packet from
			 * the controller, Disable IBS to recevie dump
			 * with out any interruption, ideally time required for
			 * the controller to send the dump is 8 seconds. let us
			 * start timer to handle this asynchronous activity.
			 */
			set_bit(QCA_IBS_DISABLED, &qca->flags);
			set_bit(QCA_MEMDUMP_COLLECTION, &qca->flags);
			dump = (void *) skb->data;
			dump_size = __le32_to_cpu(dump->dump_size);
			if (!(dump_size)) {
				bt_dev_err(hu->hdev, "Rx invalid memdump size");
				kfree(qca_memdump);
				kfree_skb(skb);
				qca->qca_memdump = NULL;
				mutex_unlock(&qca->hci_memdump_lock);
				return;
			}

			bt_dev_info(hu->hdev, "QCA collecting dump of size:%u",
				    dump_size);
			queue_delayed_work(qca->workqueue,
					   &qca->ctrl_memdump_timeout,
					   msecs_to_jiffies(MEMDUMP_TIMEOUT_MS)
					  );

			skb_pull(skb, sizeof(dump_size));
			memdump_buf = vmalloc(dump_size);
			qca_memdump->ram_dump_size = dump_size;
			qca_memdump->memdump_buf_head = memdump_buf;
			qca_memdump->memdump_buf_tail = memdump_buf;
		}

		memdump_buf = qca_memdump->memdump_buf_tail;

		/* If sequence no 0 is missed then there is no point in
		 * accepting the other sequences.
		 */
		if (!memdump_buf) {
			bt_dev_err(hu->hdev, "QCA: Discarding other packets");
			kfree(qca_memdump);
			kfree_skb(skb);
			qca->qca_memdump = NULL;
			mutex_unlock(&qca->hci_memdump_lock);
			return;
		}

		/* There could be chance of missing some packets from
		 * the controller. In such cases let us store the dummy
		 * packets in the buffer.
		 */
		/* For QCA6390, controller does not lost packets but
		 * sequence number field of packet sometimes has error
		 * bits, so skip this checking for missing packet.
		 */
		while ((seq_no > qca_memdump->current_seq_no + 1) &&
		       (soc_type != QCA_QCA6390) &&
		       seq_no != QCA_LAST_SEQUENCE_NUM) {
			bt_dev_err(hu->hdev, "QCA controller missed packet:%d",
				   qca_memdump->current_seq_no);
			rx_size = qca_memdump->received_dump;
			rx_size += QCA_DUMP_PACKET_SIZE;
			if (rx_size > qca_memdump->ram_dump_size) {
				bt_dev_err(hu->hdev,
					   "QCA memdump received %d, no space for missed packet",
					   qca_memdump->received_dump);
				break;
			}
			memcpy(memdump_buf, nullBuff, QCA_DUMP_PACKET_SIZE);
			memdump_buf = memdump_buf + QCA_DUMP_PACKET_SIZE;
			qca_memdump->received_dump += QCA_DUMP_PACKET_SIZE;
			qca_memdump->current_seq_no++;
		}

		rx_size = qca_memdump->received_dump + skb->len;
		if (rx_size <= qca_memdump->ram_dump_size) {
			if ((seq_no != QCA_LAST_SEQUENCE_NUM) &&
			    (seq_no != qca_memdump->current_seq_no))
				bt_dev_err(hu->hdev,
					   "QCA memdump unexpected packet %d",
					   seq_no);
			bt_dev_dbg(hu->hdev,
				   "QCA memdump packet %d with length %d",
				   seq_no, skb->len);
			memcpy(memdump_buf, (unsigned char *)skb->data,
			       skb->len);
			memdump_buf = memdump_buf + skb->len;
			qca_memdump->memdump_buf_tail = memdump_buf;
			qca_memdump->current_seq_no = seq_no + 1;
			qca_memdump->received_dump += skb->len;
		} else {
			bt_dev_err(hu->hdev,
				   "QCA memdump received %d, no space for packet %d",
				   qca_memdump->received_dump, seq_no);
		}
		qca->qca_memdump = qca_memdump;
		kfree_skb(skb);
		if (seq_no == QCA_LAST_SEQUENCE_NUM) {
			bt_dev_info(hu->hdev,
				    "QCA memdump Done, received %d, total %d",
				    qca_memdump->received_dump,
				    qca_memdump->ram_dump_size);
			memdump_buf = qca_memdump->memdump_buf_head;
			dev_coredumpv(&hu->serdev->dev, memdump_buf,
				      qca_memdump->received_dump, GFP_KERNEL);
			cancel_delayed_work(&qca->ctrl_memdump_timeout);
			kfree(qca->qca_memdump);
			qca->qca_memdump = NULL;
			qca->memdump_state = QCA_MEMDUMP_COLLECTED;
			clear_bit(QCA_MEMDUMP_COLLECTION, &qca->flags);
		}

		mutex_unlock(&qca->hci_memdump_lock);
	}

}

static int qca_controller_memdump_event(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;

	set_bit(QCA_SSR_TRIGGERED, &qca->flags);
	skb_queue_tail(&qca->rx_memdump_q, skb);
	queue_work(qca->workqueue, &qca->ctrl_memdump_evt);

	return 0;
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
	/* We receive chip memory dump as an event packet, With a dedicated
	 * handler followed by a hardware error event. When this event is
	 * received we store dump into a file before closing hci. This
	 * dump will help in triaging the issues.
	 */
	if ((skb->data[0] == HCI_VENDOR_PKT) &&
	    (get_unaligned_be16(skb->data + 2) == QCA_SSR_DUMP_HANDLE))
		return qca_controller_memdump_event(hdev, skb);

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
	switch (qca_soc_type(hu)) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		usleep_range(1000, 10000);
		break;

	default:
		msleep(300);
	}

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
		usleep_range(1000, 10000);

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
	switch (qca_soc_type(hu)) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		if (!qca_get_speed(hu, QCA_INIT_SPEED) &&
		    !qca_get_speed(hu, QCA_OPER_SPEED))
			return -EINVAL;
		break;

	default:
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
		switch (soc_type) {
		case QCA_WCN3988:
		case QCA_WCN3990:
		case QCA_WCN3991:
		case QCA_WCN3998:
		case QCA_WCN6750:
		case QCA_WCN6855:
		case QCA_WCN7850:
			hci_uart_set_flow_control(hu, true);
			break;

		default:
			break;
		}

		switch (soc_type) {
		case QCA_WCN3990:
			reinit_completion(&qca->drop_ev_comp);
			set_bit(QCA_DROP_VENDOR_EVENT, &qca->flags);
			break;

		default:
			break;
		}

		qca_baudrate = qca_get_baudrate_value(speed);
		bt_dev_dbg(hu->hdev, "Set UART speed to %d", speed);
		ret = qca_set_baudrate(hu->hdev, qca_baudrate);
		if (ret)
			goto error;

		host_set_baudrate(hu, speed);

error:
		switch (soc_type) {
		case QCA_WCN3988:
		case QCA_WCN3990:
		case QCA_WCN3991:
		case QCA_WCN3998:
		case QCA_WCN6750:
		case QCA_WCN6855:
		case QCA_WCN7850:
			hci_uart_set_flow_control(hu, false);
			break;

		default:
			break;
		}

		switch (soc_type) {
		case QCA_WCN3990:
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
			break;

		default:
			break;
		}
	}

	return ret;
}

static int qca_send_crashbuffer(struct hci_uart *hu)
{
	struct qca_data *qca = hu->priv;
	struct sk_buff *skb;

	skb = bt_skb_alloc(QCA_CRASHBYTE_PACKET_LEN, GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hu->hdev, "Failed to allocate memory for skb packet");
		return -ENOMEM;
	}

	/* We forcefully crash the controller, by sending 0xfb byte for
	 * 1024 times. We also might have chance of losing data, To be
	 * on safer side we send 1096 bytes to the SoC.
	 */
	memset(skb_put(skb, QCA_CRASHBYTE_PACKET_LEN), QCA_MEMDUMP_BYTE,
	       QCA_CRASHBYTE_PACKET_LEN);
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	bt_dev_info(hu->hdev, "crash the soc to collect controller dump");
	skb_queue_tail(&qca->txq, skb);
	hci_uart_tx_wakeup(hu);

	return 0;
}

static void qca_wait_for_dump_collection(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;

	wait_on_bit_timeout(&qca->flags, QCA_MEMDUMP_COLLECTION,
			    TASK_UNINTERRUPTIBLE, MEMDUMP_TIMEOUT_MS);

	clear_bit(QCA_MEMDUMP_COLLECTION, &qca->flags);
}

static void qca_hw_error(struct hci_dev *hdev, u8 code)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;

	set_bit(QCA_SSR_TRIGGERED, &qca->flags);
	set_bit(QCA_HW_ERROR_EVENT, &qca->flags);
	bt_dev_info(hdev, "mem_dump_status: %d", qca->memdump_state);

	if (qca->memdump_state == QCA_MEMDUMP_IDLE) {
		/* If hardware error event received for other than QCA
		 * soc memory dump event, then we need to crash the SOC
		 * and wait here for 8 seconds to get the dump packets.
		 * This will block main thread to be on hold until we
		 * collect dump.
		 */
		set_bit(QCA_MEMDUMP_COLLECTION, &qca->flags);
		qca_send_crashbuffer(hu);
		qca_wait_for_dump_collection(hdev);
	} else if (qca->memdump_state == QCA_MEMDUMP_COLLECTING) {
		/* Let us wait here until memory dump collected or
		 * memory dump timer expired.
		 */
		bt_dev_info(hdev, "waiting for dump to complete");
		qca_wait_for_dump_collection(hdev);
	}

	mutex_lock(&qca->hci_memdump_lock);
	if (qca->memdump_state != QCA_MEMDUMP_COLLECTED) {
		bt_dev_err(hu->hdev, "clearing allocated memory due to memdump timeout");
		if (qca->qca_memdump) {
			vfree(qca->qca_memdump->memdump_buf_head);
			kfree(qca->qca_memdump);
			qca->qca_memdump = NULL;
		}
		qca->memdump_state = QCA_MEMDUMP_TIMEOUT;
		cancel_delayed_work(&qca->ctrl_memdump_timeout);
	}
	mutex_unlock(&qca->hci_memdump_lock);

	if (qca->memdump_state == QCA_MEMDUMP_TIMEOUT ||
	    qca->memdump_state == QCA_MEMDUMP_COLLECTED) {
		cancel_work_sync(&qca->ctrl_memdump_evt);
		skb_queue_purge(&qca->rx_memdump_q);
	}

	clear_bit(QCA_HW_ERROR_EVENT, &qca->flags);
}

static void qca_cmd_timeout(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;

	set_bit(QCA_SSR_TRIGGERED, &qca->flags);
	if (qca->memdump_state == QCA_MEMDUMP_IDLE) {
		set_bit(QCA_MEMDUMP_COLLECTION, &qca->flags);
		qca_send_crashbuffer(hu);
		qca_wait_for_dump_collection(hdev);
	} else if (qca->memdump_state == QCA_MEMDUMP_COLLECTING) {
		/* Let us wait here until memory dump collected or
		 * memory dump timer expired.
		 */
		bt_dev_info(hdev, "waiting for dump to complete");
		qca_wait_for_dump_collection(hdev);
	}

	mutex_lock(&qca->hci_memdump_lock);
	if (qca->memdump_state != QCA_MEMDUMP_COLLECTED) {
		qca->memdump_state = QCA_MEMDUMP_TIMEOUT;
		if (!test_bit(QCA_HW_ERROR_EVENT, &qca->flags)) {
			/* Inject hw error event to reset the device
			 * and driver.
			 */
			hci_reset_dev(hu->hdev);
		}
	}
	mutex_unlock(&qca->hci_memdump_lock);
}

static bool qca_wakeup(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	bool wakeup;

	if (!hu->serdev)
		return true;

	/* BT SoC attached through the serial bus is handled by the serdev driver.
	 * So we need to use the device handle of the serdev driver to get the
	 * status of device may wakeup.
	 */
	wakeup = device_may_wakeup(&hu->serdev->ctrl->dev);
	bt_dev_dbg(hu->hdev, "wakeup status : %d", wakeup);

	return wakeup;
}

static int qca_regulator_init(struct hci_uart *hu)
{
	enum qca_btsoc_type soc_type = qca_soc_type(hu);
	struct qca_serdev *qcadev;
	int ret;
	bool sw_ctrl_state;

	/* Check for vregs status, may be hci down has turned
	 * off the voltage regulator.
	 */
	qcadev = serdev_device_get_drvdata(hu->serdev);
	if (!qcadev->bt_power->vregs_on) {
		serdev_device_close(hu->serdev);
		ret = qca_regulator_enable(qcadev);
		if (ret)
			return ret;

		ret = serdev_device_open(hu->serdev);
		if (ret) {
			bt_dev_err(hu->hdev, "failed to open port");
			return ret;
		}
	}

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
		/* Forcefully enable wcn399x to enter in to boot mode. */
		host_set_baudrate(hu, 2400);
		ret = qca_send_power_pulse(hu, false);
		if (ret)
			return ret;
		break;

	default:
		break;
	}

	/* For wcn6750 need to enable gpio bt_en */
	if (qcadev->bt_en) {
		gpiod_set_value_cansleep(qcadev->bt_en, 0);
		msleep(50);
		gpiod_set_value_cansleep(qcadev->bt_en, 1);
		msleep(50);
		if (qcadev->sw_ctrl) {
			sw_ctrl_state = gpiod_get_value_cansleep(qcadev->sw_ctrl);
			bt_dev_dbg(hu->hdev, "SW_CTRL is %d", sw_ctrl_state);
		}
	}

	qca_set_speed(hu, QCA_INIT_SPEED);

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
		ret = qca_send_power_pulse(hu, true);
		if (ret)
			return ret;
		break;

	default:
		break;
	}

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

static int qca_power_on(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	enum qca_btsoc_type soc_type = qca_soc_type(hu);
	struct qca_serdev *qcadev;
	struct qca_data *qca = hu->priv;
	int ret = 0;

	/* Non-serdev device usually is powered by external power
	 * and don't need additional action in driver for power on
	 */
	if (!hu->serdev)
		return 0;

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		ret = qca_regulator_init(hu);
		break;

	default:
		qcadev = serdev_device_get_drvdata(hu->serdev);
		if (qcadev->bt_en) {
			gpiod_set_value_cansleep(qcadev->bt_en, 1);
			/* Controller needs time to bootup. */
			msleep(150);
		}
	}

	clear_bit(QCA_BT_OFF, &qca->flags);
	return ret;
}

static int qca_setup(struct hci_uart *hu)
{
	struct hci_dev *hdev = hu->hdev;
	struct qca_data *qca = hu->priv;
	unsigned int speed, qca_baudrate = QCA_BAUDRATE_115200;
	unsigned int retries = 0;
	enum qca_btsoc_type soc_type = qca_soc_type(hu);
	const char *firmware_name = qca_get_firmware_name(hu);
	int ret;
	struct qca_btsoc_version ver;
	struct qca_serdev *qcadev;
	const char *soc_name;

	ret = qca_check_speeds(hu);
	if (ret)
		return ret;

	clear_bit(QCA_ROM_FW, &qca->flags);
	/* Patch downloading has to be done without IBS mode */
	set_bit(QCA_IBS_DISABLED, &qca->flags);

	/* Enable controller to do both LE scan and BR/EDR inquiry
	 * simultaneously.
	 */
	set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
		soc_name = "wcn399x";
		break;

	case QCA_WCN6750:
		soc_name = "wcn6750";
		break;

	case QCA_WCN6855:
		soc_name = "wcn6855";
		break;

	case QCA_WCN7850:
		soc_name = "wcn7850";
		break;

	default:
		soc_name = "ROME/QCA6390";
	}
	bt_dev_info(hdev, "setting up %s", soc_name);

	qca->memdump_state = QCA_MEMDUMP_IDLE;

retry:
	ret = qca_power_on(hdev);
	if (ret)
		goto out;

	clear_bit(QCA_SSR_TRIGGERED, &qca->flags);

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		set_bit(HCI_QUIRK_USE_BDADDR_PROPERTY, &hdev->quirks);

		qcadev = serdev_device_get_drvdata(hu->serdev);
		if (qcadev->bdaddr_property_broken)
			set_bit(HCI_QUIRK_BDADDR_PROPERTY_BROKEN, &hdev->quirks);

		hci_set_aosp_capable(hdev);

		ret = qca_read_soc_version(hdev, &ver, soc_type);
		if (ret)
			goto out;
		break;

	default:
		qca_set_speed(hu, QCA_INIT_SPEED);
	}

	/* Setup user speed if needed */
	speed = qca_get_speed(hu, QCA_OPER_SPEED);
	if (speed) {
		ret = qca_set_speed(hu, QCA_OPER_SPEED);
		if (ret)
			goto out;

		qca_baudrate = qca_get_baudrate_value(speed);
	}

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		break;

	default:
		/* Get QCA version information */
		ret = qca_read_soc_version(hdev, &ver, soc_type);
		if (ret)
			goto out;
	}

	/* Setup patch / NVM configurations */
	ret = qca_uart_setup(hdev, qca_baudrate, soc_type, ver,
			firmware_name);
	if (!ret) {
		clear_bit(QCA_IBS_DISABLED, &qca->flags);
		qca_debugfs_init(hdev);
		hu->hdev->hw_error = qca_hw_error;
		hu->hdev->cmd_timeout = qca_cmd_timeout;
		hu->hdev->wakeup = qca_wakeup;
	} else if (ret == -ENOENT) {
		/* No patch/nvm-config found, run with original fw/config */
		set_bit(QCA_ROM_FW, &qca->flags);
		ret = 0;
	} else if (ret == -EAGAIN) {
		/*
		 * Userspace firmware loader will return -EAGAIN in case no
		 * patch/nvm-config is found, so run with original fw/config.
		 */
		set_bit(QCA_ROM_FW, &qca->flags);
		ret = 0;
	}

out:
	if (ret && retries < MAX_INIT_RETRIES) {
		bt_dev_warn(hdev, "Retry BT power ON:%d", retries);
		qca_power_shutdown(hu);
		if (hu->serdev) {
			serdev_device_close(hu->serdev);
			ret = serdev_device_open(hu->serdev);
			if (ret) {
				bt_dev_err(hdev, "failed to open port");
				return ret;
			}
		}
		retries++;
		goto retry;
	}

	/* Setup bdaddr */
	if (soc_type == QCA_ROME)
		hu->hdev->set_bdaddr = qca_set_bdaddr_rome;
	else
		hu->hdev->set_bdaddr = qca_set_bdaddr;

	return ret;
}

static const struct hci_uart_proto qca_proto = {
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

static const struct qca_device_data qca_soc_data_wcn3988 __maybe_unused = {
	.soc_type = QCA_WCN3988,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 15000  },
		{ "vddxo", 80000  },
		{ "vddrf", 300000 },
		{ "vddch0", 450000 },
	},
	.num_vregs = 4,
};

static const struct qca_device_data qca_soc_data_wcn3990 __maybe_unused = {
	.soc_type = QCA_WCN3990,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 15000  },
		{ "vddxo", 80000  },
		{ "vddrf", 300000 },
		{ "vddch0", 450000 },
	},
	.num_vregs = 4,
};

static const struct qca_device_data qca_soc_data_wcn3991 __maybe_unused = {
	.soc_type = QCA_WCN3991,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 15000  },
		{ "vddxo", 80000  },
		{ "vddrf", 300000 },
		{ "vddch0", 450000 },
	},
	.num_vregs = 4,
	.capabilities = QCA_CAP_WIDEBAND_SPEECH | QCA_CAP_VALID_LE_STATES,
};

static const struct qca_device_data qca_soc_data_wcn3998 __maybe_unused = {
	.soc_type = QCA_WCN3998,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 10000  },
		{ "vddxo", 80000  },
		{ "vddrf", 300000 },
		{ "vddch0", 450000 },
	},
	.num_vregs = 4,
};

static const struct qca_device_data qca_soc_data_qca6390 __maybe_unused = {
	.soc_type = QCA_QCA6390,
	.num_vregs = 0,
	.capabilities = QCA_CAP_WIDEBAND_SPEECH | QCA_CAP_VALID_LE_STATES,
};

static const struct qca_device_data qca_soc_data_wcn6750 __maybe_unused = {
	.soc_type = QCA_WCN6750,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 5000 },
		{ "vddaon", 26000 },
		{ "vddbtcxmx", 126000 },
		{ "vddrfacmn", 12500 },
		{ "vddrfa0p8", 102000 },
		{ "vddrfa1p7", 302000 },
		{ "vddrfa1p2", 257000 },
		{ "vddrfa2p2", 1700000 },
		{ "vddasd", 200 },
	},
	.num_vregs = 9,
	.capabilities = QCA_CAP_WIDEBAND_SPEECH | QCA_CAP_VALID_LE_STATES,
};

static const struct qca_device_data qca_soc_data_wcn6855 = {
	.soc_type = QCA_WCN6855,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 5000 },
		{ "vddbtcxmx", 126000 },
		{ "vddrfacmn", 12500 },
		{ "vddrfa0p8", 102000 },
		{ "vddrfa1p7", 302000 },
		{ "vddrfa1p2", 257000 },
	},
	.num_vregs = 6,
	.capabilities = QCA_CAP_WIDEBAND_SPEECH | QCA_CAP_VALID_LE_STATES,
};

static const struct qca_device_data qca_soc_data_wcn7850 __maybe_unused = {
	.soc_type = QCA_WCN7850,
	.vregs = (struct qca_vreg []) {
		{ "vddio", 5000 },
		{ "vddaon", 26000 },
		{ "vdddig", 126000 },
		{ "vddrfa0p8", 102000 },
		{ "vddrfa1p2", 257000 },
		{ "vddrfa1p9", 302000 },
	},
	.num_vregs = 6,
	.capabilities = QCA_CAP_WIDEBAND_SPEECH | QCA_CAP_VALID_LE_STATES,
};

static void qca_power_shutdown(struct hci_uart *hu)
{
	struct qca_serdev *qcadev;
	struct qca_data *qca = hu->priv;
	unsigned long flags;
	enum qca_btsoc_type soc_type = qca_soc_type(hu);
	bool sw_ctrl_state;

	/* From this point we go into power off state. But serial port is
	 * still open, stop queueing the IBS data and flush all the buffered
	 * data in skb's.
	 */
	spin_lock_irqsave(&qca->hci_ibs_lock, flags);
	set_bit(QCA_IBS_DISABLED, &qca->flags);
	qca_flush(hu);
	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	/* Non-serdev device usually is powered by external power
	 * and don't need additional action in driver for power down
	 */
	if (!hu->serdev)
		return;

	qcadev = serdev_device_get_drvdata(hu->serdev);

	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
		host_set_baudrate(hu, 2400);
		qca_send_power_pulse(hu, false);
		qca_regulator_disable(qcadev);
		break;

	case QCA_WCN6750:
	case QCA_WCN6855:
		gpiod_set_value_cansleep(qcadev->bt_en, 0);
		msleep(100);
		qca_regulator_disable(qcadev);
		if (qcadev->sw_ctrl) {
			sw_ctrl_state = gpiod_get_value_cansleep(qcadev->sw_ctrl);
			bt_dev_dbg(hu->hdev, "SW_CTRL is %d", sw_ctrl_state);
		}
		break;

	default:
		gpiod_set_value_cansleep(qcadev->bt_en, 0);
	}

	set_bit(QCA_BT_OFF, &qca->flags);
}

static int qca_power_off(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct qca_data *qca = hu->priv;
	enum qca_btsoc_type soc_type = qca_soc_type(hu);

	hu->hdev->hw_error = NULL;
	hu->hdev->cmd_timeout = NULL;

	del_timer_sync(&qca->wake_retrans_timer);
	del_timer_sync(&qca->tx_idle_timer);

	/* Stop sending shutdown command if soc crashes. */
	if (soc_type != QCA_ROME
		&& qca->memdump_state == QCA_MEMDUMP_IDLE) {
		qca_send_pre_shutdown_cmd(hdev);
		usleep_range(8000, 10000);
	}

	qca_power_shutdown(hu);
	return 0;
}

static int qca_regulator_enable(struct qca_serdev *qcadev)
{
	struct qca_power *power = qcadev->bt_power;
	int ret;

	/* Already enabled */
	if (power->vregs_on)
		return 0;

	BT_DBG("enabling %d regulators)", power->num_vregs);

	ret = regulator_bulk_enable(power->num_vregs, power->vreg_bulk);
	if (ret)
		return ret;

	power->vregs_on = true;

	ret = clk_prepare_enable(qcadev->susclk);
	if (ret)
		qca_regulator_disable(qcadev);

	return ret;
}

static void qca_regulator_disable(struct qca_serdev *qcadev)
{
	struct qca_power *power;

	if (!qcadev)
		return;

	power = qcadev->bt_power;

	/* Already disabled? */
	if (!power->vregs_on)
		return;

	regulator_bulk_disable(power->num_vregs, power->vreg_bulk);
	power->vregs_on = false;

	clk_disable_unprepare(qcadev->susclk);
}

static int qca_init_regulators(struct qca_power *qca,
				const struct qca_vreg *vregs, size_t num_vregs)
{
	struct regulator_bulk_data *bulk;
	int ret;
	int i;

	bulk = devm_kcalloc(qca->dev, num_vregs, sizeof(*bulk), GFP_KERNEL);
	if (!bulk)
		return -ENOMEM;

	for (i = 0; i < num_vregs; i++)
		bulk[i].supply = vregs[i].name;

	ret = devm_regulator_bulk_get(qca->dev, num_vregs, bulk);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_vregs; i++) {
		ret = regulator_set_load(bulk[i].consumer, vregs[i].load_uA);
		if (ret)
			return ret;
	}

	qca->vreg_bulk = bulk;
	qca->num_vregs = num_vregs;

	return 0;
}

static int qca_serdev_probe(struct serdev_device *serdev)
{
	struct qca_serdev *qcadev;
	struct hci_dev *hdev;
	const struct qca_device_data *data;
	int err;
	bool power_ctrl_enabled = true;

	qcadev = devm_kzalloc(&serdev->dev, sizeof(*qcadev), GFP_KERNEL);
	if (!qcadev)
		return -ENOMEM;

	qcadev->serdev_hu.serdev = serdev;
	data = device_get_match_data(&serdev->dev);
	serdev_device_set_drvdata(serdev, qcadev);
	device_property_read_string(&serdev->dev, "firmware-name",
					 &qcadev->firmware_name);
	device_property_read_u32(&serdev->dev, "max-speed",
				 &qcadev->oper_speed);
	if (!qcadev->oper_speed)
		BT_DBG("UART will pick default operating speed");

	qcadev->bdaddr_property_broken = device_property_read_bool(&serdev->dev,
			"qcom,local-bd-address-broken");

	if (data)
		qcadev->btsoc_type = data->soc_type;
	else
		qcadev->btsoc_type = QCA_ROME;

	switch (qcadev->btsoc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		qcadev->bt_power = devm_kzalloc(&serdev->dev,
						sizeof(struct qca_power),
						GFP_KERNEL);
		if (!qcadev->bt_power)
			return -ENOMEM;

		qcadev->bt_power->dev = &serdev->dev;
		err = qca_init_regulators(qcadev->bt_power, data->vregs,
					  data->num_vregs);
		if (err) {
			BT_ERR("Failed to init regulators:%d", err);
			return err;
		}

		qcadev->bt_power->vregs_on = false;

		qcadev->bt_en = devm_gpiod_get_optional(&serdev->dev, "enable",
					       GPIOD_OUT_LOW);
		if (IS_ERR(qcadev->bt_en) &&
		    (data->soc_type == QCA_WCN6750 ||
		     data->soc_type == QCA_WCN6855)) {
			dev_err(&serdev->dev, "failed to acquire BT_EN gpio\n");
			return PTR_ERR(qcadev->bt_en);
		}

		if (!qcadev->bt_en)
			power_ctrl_enabled = false;

		qcadev->sw_ctrl = devm_gpiod_get_optional(&serdev->dev, "swctrl",
					       GPIOD_IN);
		if (IS_ERR(qcadev->sw_ctrl) &&
		    (data->soc_type == QCA_WCN6750 ||
		     data->soc_type == QCA_WCN6855 ||
		     data->soc_type == QCA_WCN7850)) {
			dev_err(&serdev->dev, "failed to acquire SW_CTRL gpio\n");
			return PTR_ERR(qcadev->sw_ctrl);
		}

		qcadev->susclk = devm_clk_get_optional(&serdev->dev, NULL);
		if (IS_ERR(qcadev->susclk)) {
			dev_err(&serdev->dev, "failed to acquire clk\n");
			return PTR_ERR(qcadev->susclk);
		}

		err = hci_uart_register_device(&qcadev->serdev_hu, &qca_proto);
		if (err) {
			BT_ERR("wcn3990 serdev registration failed");
			return err;
		}
		break;

	default:
		qcadev->bt_en = devm_gpiod_get_optional(&serdev->dev, "enable",
					       GPIOD_OUT_LOW);
		if (IS_ERR(qcadev->bt_en)) {
			dev_err(&serdev->dev, "failed to acquire enable gpio\n");
			return PTR_ERR(qcadev->bt_en);
		}

		if (!qcadev->bt_en)
			power_ctrl_enabled = false;

		qcadev->susclk = devm_clk_get_optional(&serdev->dev, NULL);
		if (IS_ERR(qcadev->susclk)) {
			dev_warn(&serdev->dev, "failed to acquire clk\n");
			return PTR_ERR(qcadev->susclk);
		}
		err = clk_set_rate(qcadev->susclk, SUSCLK_RATE_32KHZ);
		if (err)
			return err;

		err = clk_prepare_enable(qcadev->susclk);
		if (err)
			return err;

		err = hci_uart_register_device(&qcadev->serdev_hu, &qca_proto);
		if (err) {
			BT_ERR("Rome serdev registration failed");
			clk_disable_unprepare(qcadev->susclk);
			return err;
		}
	}

	hdev = qcadev->serdev_hu.hdev;

	if (power_ctrl_enabled) {
		set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);
		hdev->shutdown = qca_power_off;
	}

	if (data) {
		/* Wideband speech support must be set per driver since it can't
		 * be queried via hci. Same with the valid le states quirk.
		 */
		if (data->capabilities & QCA_CAP_WIDEBAND_SPEECH)
			set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED,
				&hdev->quirks);

		if (data->capabilities & QCA_CAP_VALID_LE_STATES)
			set_bit(HCI_QUIRK_VALID_LE_STATES, &hdev->quirks);
	}

	return 0;
}

static void qca_serdev_remove(struct serdev_device *serdev)
{
	struct qca_serdev *qcadev = serdev_device_get_drvdata(serdev);
	struct qca_power *power = qcadev->bt_power;

	switch (qcadev->btsoc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		if (power->vregs_on) {
			qca_power_shutdown(&qcadev->serdev_hu);
			break;
		}
		fallthrough;

	default:
		if (qcadev->susclk)
			clk_disable_unprepare(qcadev->susclk);
	}

	hci_uart_unregister_device(&qcadev->serdev_hu);
}

static void qca_serdev_shutdown(struct device *dev)
{
	int ret;
	int timeout = msecs_to_jiffies(CMD_TRANS_TIMEOUT_MS);
	struct serdev_device *serdev = to_serdev_device(dev);
	struct qca_serdev *qcadev = serdev_device_get_drvdata(serdev);
	struct hci_uart *hu = &qcadev->serdev_hu;
	struct hci_dev *hdev = hu->hdev;
	struct qca_data *qca = hu->priv;
	const u8 ibs_wake_cmd[] = { 0xFD };
	const u8 edl_reset_soc_cmd[] = { 0x01, 0x00, 0xFC, 0x01, 0x05 };

	if (qcadev->btsoc_type == QCA_QCA6390) {
		if (test_bit(QCA_BT_OFF, &qca->flags) ||
		    !test_bit(HCI_RUNNING, &hdev->flags))
			return;

		serdev_device_write_flush(serdev);
		ret = serdev_device_write_buf(serdev, ibs_wake_cmd,
					      sizeof(ibs_wake_cmd));
		if (ret < 0) {
			BT_ERR("QCA send IBS_WAKE_IND error: %d", ret);
			return;
		}
		serdev_device_wait_until_sent(serdev, timeout);
		usleep_range(8000, 10000);

		serdev_device_write_flush(serdev);
		ret = serdev_device_write_buf(serdev, edl_reset_soc_cmd,
					      sizeof(edl_reset_soc_cmd));
		if (ret < 0) {
			BT_ERR("QCA send EDL_RESET_REQ error: %d", ret);
			return;
		}
		serdev_device_wait_until_sent(serdev, timeout);
		usleep_range(8000, 10000);
	}
}

static int __maybe_unused qca_suspend(struct device *dev)
{
	struct serdev_device *serdev = to_serdev_device(dev);
	struct qca_serdev *qcadev = serdev_device_get_drvdata(serdev);
	struct hci_uart *hu = &qcadev->serdev_hu;
	struct qca_data *qca = hu->priv;
	unsigned long flags;
	bool tx_pending = false;
	int ret = 0;
	u8 cmd;
	u32 wait_timeout = 0;

	set_bit(QCA_SUSPENDING, &qca->flags);

	/* if BT SoC is running with default firmware then it does not
	 * support in-band sleep
	 */
	if (test_bit(QCA_ROM_FW, &qca->flags))
		return 0;

	/* During SSR after memory dump collection, controller will be
	 * powered off and then powered on.If controller is powered off
	 * during SSR then we should wait until SSR is completed.
	 */
	if (test_bit(QCA_BT_OFF, &qca->flags) &&
	    !test_bit(QCA_SSR_TRIGGERED, &qca->flags))
		return 0;

	if (test_bit(QCA_IBS_DISABLED, &qca->flags) ||
	    test_bit(QCA_SSR_TRIGGERED, &qca->flags)) {
		wait_timeout = test_bit(QCA_SSR_TRIGGERED, &qca->flags) ?
					IBS_DISABLE_SSR_TIMEOUT_MS :
					FW_DOWNLOAD_TIMEOUT_MS;

		/* QCA_IBS_DISABLED flag is set to true, During FW download
		 * and during memory dump collection. It is reset to false,
		 * After FW download complete.
		 */
		wait_on_bit_timeout(&qca->flags, QCA_IBS_DISABLED,
			    TASK_UNINTERRUPTIBLE, msecs_to_jiffies(wait_timeout));

		if (test_bit(QCA_IBS_DISABLED, &qca->flags)) {
			bt_dev_err(hu->hdev, "SSR or FW download time out");
			ret = -ETIMEDOUT;
			goto error;
		}
	}

	cancel_work_sync(&qca->ws_awake_device);
	cancel_work_sync(&qca->ws_awake_rx);

	spin_lock_irqsave_nested(&qca->hci_ibs_lock,
				 flags, SINGLE_DEPTH_NESTING);

	switch (qca->tx_ibs_state) {
	case HCI_IBS_TX_WAKING:
		del_timer(&qca->wake_retrans_timer);
		fallthrough;
	case HCI_IBS_TX_AWAKE:
		del_timer(&qca->tx_idle_timer);

		serdev_device_write_flush(hu->serdev);
		cmd = HCI_IBS_SLEEP_IND;
		ret = serdev_device_write_buf(hu->serdev, &cmd, sizeof(cmd));

		if (ret < 0) {
			BT_ERR("Failed to send SLEEP to device");
			break;
		}

		qca->tx_ibs_state = HCI_IBS_TX_ASLEEP;
		qca->ibs_sent_slps++;
		tx_pending = true;
		break;

	case HCI_IBS_TX_ASLEEP:
		break;

	default:
		BT_ERR("Spurious tx state %d", qca->tx_ibs_state);
		ret = -EINVAL;
		break;
	}

	spin_unlock_irqrestore(&qca->hci_ibs_lock, flags);

	if (ret < 0)
		goto error;

	if (tx_pending) {
		serdev_device_wait_until_sent(hu->serdev,
					      msecs_to_jiffies(CMD_TRANS_TIMEOUT_MS));
		serial_clock_vote(HCI_IBS_TX_VOTE_CLOCK_OFF, hu);
	}

	/* Wait for HCI_IBS_SLEEP_IND sent by device to indicate its Tx is going
	 * to sleep, so that the packet does not wake the system later.
	 */
	ret = wait_event_interruptible_timeout(qca->suspend_wait_q,
			qca->rx_ibs_state == HCI_IBS_RX_ASLEEP,
			msecs_to_jiffies(IBS_BTSOC_TX_IDLE_TIMEOUT_MS));
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto error;
	}

	return 0;

error:
	clear_bit(QCA_SUSPENDING, &qca->flags);

	return ret;
}

static int __maybe_unused qca_resume(struct device *dev)
{
	struct serdev_device *serdev = to_serdev_device(dev);
	struct qca_serdev *qcadev = serdev_device_get_drvdata(serdev);
	struct hci_uart *hu = &qcadev->serdev_hu;
	struct qca_data *qca = hu->priv;

	clear_bit(QCA_SUSPENDING, &qca->flags);

	return 0;
}

static SIMPLE_DEV_PM_OPS(qca_pm_ops, qca_suspend, qca_resume);

#ifdef CONFIG_OF
static const struct of_device_id qca_bluetooth_of_match[] = {
	{ .compatible = "qcom,qca6174-bt" },
	{ .compatible = "qcom,qca6390-bt", .data = &qca_soc_data_qca6390},
	{ .compatible = "qcom,qca9377-bt" },
	{ .compatible = "qcom,wcn3988-bt", .data = &qca_soc_data_wcn3988},
	{ .compatible = "qcom,wcn3990-bt", .data = &qca_soc_data_wcn3990},
	{ .compatible = "qcom,wcn3991-bt", .data = &qca_soc_data_wcn3991},
	{ .compatible = "qcom,wcn3998-bt", .data = &qca_soc_data_wcn3998},
	{ .compatible = "qcom,wcn6750-bt", .data = &qca_soc_data_wcn6750},
	{ .compatible = "qcom,wcn6855-bt", .data = &qca_soc_data_wcn6855},
	{ .compatible = "qcom,wcn7850-bt", .data = &qca_soc_data_wcn7850},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qca_bluetooth_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id qca_bluetooth_acpi_match[] = {
	{ "QCOM6390", (kernel_ulong_t)&qca_soc_data_qca6390 },
	{ "DLA16390", (kernel_ulong_t)&qca_soc_data_qca6390 },
	{ "DLB16390", (kernel_ulong_t)&qca_soc_data_qca6390 },
	{ "DLB26390", (kernel_ulong_t)&qca_soc_data_qca6390 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, qca_bluetooth_acpi_match);
#endif


static struct serdev_device_driver qca_serdev_driver = {
	.probe = qca_serdev_probe,
	.remove = qca_serdev_remove,
	.driver = {
		.name = "hci_uart_qca",
		.of_match_table = of_match_ptr(qca_bluetooth_of_match),
		.acpi_match_table = ACPI_PTR(qca_bluetooth_acpi_match),
		.shutdown = qca_serdev_shutdown,
		.pm = &qca_pm_ops,
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
