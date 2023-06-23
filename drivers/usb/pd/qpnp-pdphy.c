// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "usbpd.h"

#define USB_PDPHY_MAX_DATA_OBJ_LEN	28
#define USB_PDPHY_MSG_HDR_LEN		2

/* PD PHY register offsets and bit fields */
#define USB_PDPHY_MSG_CONFIG		0x40
#define MSG_CONFIG_PORT_DATA_ROLE	BIT(3)
#define MSG_CONFIG_PORT_POWER_ROLE	BIT(2)
#define MSG_CONFIG_SPEC_REV_MASK	(BIT(1) | BIT(0))

#define USB_PDPHY_EN_CONTROL		0x46
#define CONTROL_ENABLE			BIT(0)

#define USB_PDPHY_RX_STATUS		0x4A
#define RX_FRAME_TYPE			(BIT(0) | BIT(1) | BIT(2))

#define USB_PDPHY_FRAME_FILTER		0x4C
#define FRAME_FILTER_EN_HARD_RESET	BIT(5)
#define FRAME_FILTER_EN_SOP		BIT(0)

#define USB_PDPHY_TX_SIZE		0x42
#define TX_SIZE_MASK			0xF

#define USB_PDPHY_TX_CONTROL		0x44
#define TX_CONTROL_RETRY_COUNT(n)	(((n) & 0x3) << 5)
#define TX_CONTROL_FRAME_TYPE		(BIT(4) | BIT(3) | BIT(2))
#define TX_CONTROL_FRAME_TYPE_CABLE_RESET (0x1 << 2)
#define TX_CONTROL_SEND_SIGNAL		BIT(1)
#define TX_CONTROL_SEND_MSG		BIT(0)

#define USB_PDPHY_RX_SIZE		0x48

#define USB_PDPHY_RX_ACKNOWLEDGE	0x4B
#define RX_BUFFER_TOKEN			BIT(0)

#define USB_PDPHY_BIST_MODE		0x4E
#define BIST_MODE_MASK			0xF
#define BIST_ENABLE			BIT(7)
#define PD_MSG_BIST			0x3
#define PD_BIST_TEST_DATA_MODE		0x8

#define USB_PDPHY_TX_BUFFER_HDR		0x60
#define USB_PDPHY_TX_BUFFER_DATA	0x62

#define USB_PDPHY_RX_BUFFER		0x80

/* VDD regulator */
#define VDD_PDPHY_VOL_MIN		2800000 /* uV */
#define VDD_PDPHY_VOL_MAX		3300000 /* uV */
#define VDD_PDPHY_HPM_LOAD		3000 /* uA */

/* Message Spec Rev field */
#define PD_MSG_HDR_REV(hdr)		(((hdr) >> 6) & 3)

/* timers */
#define RECEIVER_RESPONSE_TIME		15	/* tReceiverResponse */
#define HARD_RESET_COMPLETE_TIME	5	/* tHardResetComplete */

struct usb_pdphy {
	struct device *dev;
	struct regmap *regmap;

	u16 base;
	struct regulator *vdd_pdphy;

	/* irqs */
	int sig_tx_irq;
	int sig_rx_irq;
	int msg_tx_irq;
	int msg_rx_irq;
	int msg_tx_failed_irq;
	int msg_tx_discarded_irq;
	int msg_rx_discarded_irq;
	bool sig_rx_wake_enabled;
	bool msg_rx_wake_enabled;

	void (*signal_cb)(struct usbpd *pd, enum pd_sig_type sig);
	void (*msg_rx_cb)(struct usbpd *pd, enum pd_sop_type sop,
			  u8 *buf, size_t len);
	void (*shutdown_cb)(struct usbpd *pd);

	/* write waitq */
	wait_queue_head_t tx_waitq;

	bool is_opened;
	int tx_status;
	u8 frame_filter_val;
	bool in_test_data_mode;

	enum data_role data_role;
	enum power_role power_role;

	struct usbpd *usbpd;

	/* debug */
	struct dentry *debug_root;
	unsigned int tx_bytes; /* hdr + data */
	unsigned int rx_bytes; /* hdr + data */
	unsigned int sig_tx_cnt;
	unsigned int sig_rx_cnt;
	unsigned int msg_tx_cnt;
	unsigned int msg_rx_cnt;
	unsigned int msg_tx_failed_cnt;
	unsigned int msg_tx_discarded_cnt;
	unsigned int msg_rx_discarded_cnt;
};

static struct usb_pdphy *__pdphy;

static int pdphy_dbg_status(struct seq_file *s, void *p)
{
	struct usb_pdphy *pdphy = s->private;

	seq_printf(s,
		"PD Phy driver status\n"
		"==================================================\n");
	seq_printf(s, "opened:         %10d\n", pdphy->is_opened);
	seq_printf(s, "tx status:      %10d\n", pdphy->tx_status);
	seq_printf(s, "tx bytes:       %10u\n", pdphy->tx_bytes);
	seq_printf(s, "rx bytes:       %10u\n", pdphy->rx_bytes);
	seq_printf(s, "data role:      %10u\n", pdphy->data_role);
	seq_printf(s, "power role:     %10u\n", pdphy->power_role);
	seq_printf(s, "frame filter:   %10u\n", pdphy->frame_filter_val);
	seq_printf(s, "sig tx cnt:     %10u\n", pdphy->sig_tx_cnt);
	seq_printf(s, "sig rx cnt:     %10u\n", pdphy->sig_rx_cnt);
	seq_printf(s, "msg tx cnt:     %10u\n", pdphy->msg_tx_cnt);
	seq_printf(s, "msg rx cnt:     %10u\n", pdphy->msg_rx_cnt);
	seq_printf(s, "msg tx failed cnt:    %10u\n",
			pdphy->msg_tx_failed_cnt);
	seq_printf(s, "msg tx discarded cnt: %10u\n",
			pdphy->msg_tx_discarded_cnt);
	seq_printf(s, "msg rx discarded cnt: %10u\n",
			pdphy->msg_rx_discarded_cnt);

	return 0;
}

static int pdphy_dbg_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, pdphy_dbg_status, inode->i_private);
}

static const struct file_operations status_ops = {
	.owner		= THIS_MODULE,
	.open		= pdphy_dbg_status_open,
	.llseek		= seq_lseek,
	.read		= seq_read,
	.release	= single_release,
};

static void pdphy_create_debugfs_entries(struct usb_pdphy *pdphy)
{
	struct dentry *ent;

	pdphy->debug_root = debugfs_create_dir("usb-pdphy", NULL);
	if (!pdphy->debug_root) {
		dev_warn(pdphy->dev, "Couldn't create debug dir\n");
		return;
	}

	ent = debugfs_create_file("status", 0400, pdphy->debug_root, pdphy,
				  &status_ops);
	if (!ent) {
		dev_warn(pdphy->dev, "Couldn't create status file\n");
		debugfs_remove(pdphy->debug_root);
	}
}

static int pdphy_enable_power(struct usb_pdphy *pdphy, bool on)
{
	int ret = 0;

	dev_dbg(pdphy->dev, "%s turn %s regulator.\n", __func__,
		on ? "on" : "off");

	if (!on)
		goto disable_pdphy_vdd;

	ret = regulator_set_load(pdphy->vdd_pdphy, VDD_PDPHY_HPM_LOAD);
	if (ret < 0) {
		dev_err(pdphy->dev, "Unable to set HPM of vdd_pdphy:%d\n", ret);
		return ret;
	}

	ret = regulator_set_voltage(pdphy->vdd_pdphy, VDD_PDPHY_VOL_MIN,
						VDD_PDPHY_VOL_MAX);
	if (ret) {
		dev_err(pdphy->dev,
				"set voltage failed for vdd_pdphy:%d\n", ret);
		goto put_pdphy_vdd_lpm;
	}

	ret = regulator_enable(pdphy->vdd_pdphy);
	if (ret) {
		dev_err(pdphy->dev, "Unable to enable vdd_pdphy:%d\n", ret);
		goto unset_pdphy_vdd;
	}

	dev_dbg(pdphy->dev, "%s: PD PHY regulator turned ON.\n", __func__);
	return ret;

disable_pdphy_vdd:
	ret = regulator_disable(pdphy->vdd_pdphy);
	if (ret)
		dev_err(pdphy->dev, "Unable to disable vdd_pdphy:%d\n", ret);

unset_pdphy_vdd:
	ret = regulator_set_voltage(pdphy->vdd_pdphy, 0, VDD_PDPHY_VOL_MAX);
	if (ret)
		dev_err(pdphy->dev,
			"Unable to set (0) voltage for vdd_pdphy:%d\n", ret);

put_pdphy_vdd_lpm:
	ret = regulator_set_load(pdphy->vdd_pdphy, 0);
	if (ret < 0)
		dev_err(pdphy->dev, "Unable to set (0) HPM of vdd_pdphy\n");

	return ret;
}

void pdphy_enable_irq(struct usb_pdphy *pdphy, bool enable)
{
	if (enable) {
		enable_irq(pdphy->sig_tx_irq);
		enable_irq(pdphy->sig_rx_irq);
		pdphy->sig_rx_wake_enabled =
			!enable_irq_wake(pdphy->sig_rx_irq);
		enable_irq(pdphy->msg_tx_irq);
		if (!pdphy->in_test_data_mode) {
			enable_irq(pdphy->msg_rx_irq);
			pdphy->msg_rx_wake_enabled =
				!enable_irq_wake(pdphy->msg_rx_irq);
		}
		enable_irq(pdphy->msg_tx_failed_irq);
		enable_irq(pdphy->msg_tx_discarded_irq);
		enable_irq(pdphy->msg_rx_discarded_irq);
		return;
	}

	disable_irq(pdphy->sig_tx_irq);
	disable_irq(pdphy->sig_rx_irq);
	if (pdphy->sig_rx_wake_enabled) {
		disable_irq_wake(pdphy->sig_rx_irq);
		pdphy->sig_rx_wake_enabled = false;
	}
	disable_irq(pdphy->msg_tx_irq);
	if (!pdphy->in_test_data_mode)
		disable_irq(pdphy->msg_rx_irq);
	if (pdphy->msg_rx_wake_enabled) {
		disable_irq_wake(pdphy->msg_rx_irq);
		pdphy->msg_rx_wake_enabled = false;
	}
	disable_irq(pdphy->msg_tx_failed_irq);
	disable_irq(pdphy->msg_tx_discarded_irq);
	disable_irq(pdphy->msg_rx_discarded_irq);
}

static int pdphy_reg_read(struct usb_pdphy *pdphy, u8 *val, u16 addr, int count)
{
	int ret;

	ret = regmap_bulk_read(pdphy->regmap, pdphy->base + addr, val, count);
	if (ret) {
		dev_err(pdphy->dev, "read failed: addr=0x%04x, ret=%d\n",
			pdphy->base + addr, ret);
		return ret;
	}

	return 0;
}

/* Write multiple registers to device with block of data */
static int pdphy_bulk_reg_write(struct usb_pdphy *pdphy, u16 addr,
	const void *val, u8 val_cnt)
{
	int ret;

	ret = regmap_bulk_write(pdphy->regmap, pdphy->base + addr,
			val, val_cnt);
	if (ret) {
		dev_err(pdphy->dev, "bulk write failed: addr=0x%04x, ret=%d\n",
				pdphy->base + addr, ret);
		return ret;
	}

	return 0;
}

/* Writes a single byte to the specified register */
static inline int pdphy_reg_write(struct usb_pdphy *pdphy, u16 addr, u8 val)
{
	return pdphy_bulk_reg_write(pdphy, addr, &val, 1);
}

/* Writes to the specified register limited by the bit mask */
static int pdphy_masked_write(struct usb_pdphy *pdphy, u16 addr,
	u8 mask, u8 val)
{
	int ret;

	ret = regmap_update_bits(pdphy->regmap, pdphy->base + addr, mask, val);
	if (ret) {
		dev_err(pdphy->dev, "write failed: addr=0x%04x, ret=%d\n",
				pdphy->base + addr, ret);
		return ret;
	}

	return 0;
}

int pd_phy_update_roles(enum data_role dr, enum power_role pr)
{
	struct usb_pdphy *pdphy = __pdphy;

	return pdphy_masked_write(pdphy, USB_PDPHY_MSG_CONFIG,
		(MSG_CONFIG_PORT_DATA_ROLE | MSG_CONFIG_PORT_POWER_ROLE),
		((dr == DR_DFP ? MSG_CONFIG_PORT_DATA_ROLE : 0) |
		 (pr == PR_SRC ? MSG_CONFIG_PORT_POWER_ROLE : 0)));
}
EXPORT_SYMBOL(pd_phy_update_roles);

int pd_phy_update_frame_filter(u8 frame_filter_val)
{
	struct usb_pdphy *pdphy = __pdphy;

	return pdphy_reg_write(pdphy, USB_PDPHY_FRAME_FILTER, frame_filter_val);
}
EXPORT_SYMBOL(pd_phy_update_frame_filter);

int pd_phy_open(struct pd_phy_params *params)
{
	int ret;
	struct usb_pdphy *pdphy = __pdphy;

	if (!pdphy) {
		pr_err("%s: pdphy not found\n", __func__);
		return -ENODEV;
	}

	if (pdphy->is_opened) {
		dev_err(pdphy->dev, "%s: already opened\n", __func__);
		return -EBUSY;
	}

	pdphy->signal_cb = params->signal_cb;
	pdphy->msg_rx_cb = params->msg_rx_cb;
	pdphy->shutdown_cb = params->shutdown_cb;
	pdphy->data_role = params->data_role;
	pdphy->power_role = params->power_role;
	pdphy->frame_filter_val = params->frame_filter_val;

	dev_dbg(pdphy->dev, "%s: DR %x PR %x frame filter val %x\n", __func__,
		pdphy->data_role, pdphy->power_role, pdphy->frame_filter_val);

	ret = pdphy_enable_power(pdphy, true);
	if (ret)
		return ret;

	/* update data and power role to be used in GoodCRC generation */
	ret = pd_phy_update_roles(pdphy->data_role, pdphy->power_role);
	if (ret)
		return ret;

	/* PD 2.0  phy */
	ret = pdphy_masked_write(pdphy, USB_PDPHY_MSG_CONFIG,
			MSG_CONFIG_SPEC_REV_MASK, USBPD_REV_20);
	if (ret)
		return ret;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_EN_CONTROL, 0);
	if (ret)
		return ret;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_EN_CONTROL, CONTROL_ENABLE);
	if (ret)
		return ret;

	/* update frame filter */
	ret = pdphy_reg_write(pdphy, USB_PDPHY_FRAME_FILTER,
			pdphy->frame_filter_val);
	if (ret)
		return ret;

	/* initialize Rx buffer ownership to PDPHY HW */
	ret = pdphy_reg_write(pdphy, USB_PDPHY_RX_ACKNOWLEDGE, 0);
	if (ret)
		return ret;

	pdphy->is_opened = true;
	pdphy_enable_irq(pdphy, true);

	return ret;
}
EXPORT_SYMBOL(pd_phy_open);

int pd_phy_signal(enum pd_sig_type sig)
{
	u8 val;
	int ret;
	struct usb_pdphy *pdphy = __pdphy;

	dev_dbg(pdphy->dev, "%s: type %d\n", __func__, sig);

	if (!pdphy) {
		pr_err("%s: pdphy not found\n", __func__);
		return -ENODEV;
	}

	if (!pdphy->is_opened) {
		dev_dbg(pdphy->dev, "%s: pdphy disabled\n", __func__);
		return -ENODEV;
	}

	pdphy->tx_status = -EINPROGRESS;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_CONTROL, 0);
	if (ret)
		return ret;

	usleep_range(2, 3);

	val = (sig == CABLE_RESET_SIG ? TX_CONTROL_FRAME_TYPE_CABLE_RESET : 0)
		| TX_CONTROL_SEND_SIGNAL;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_CONTROL, val);
	if (ret)
		return ret;

	ret = wait_event_interruptible_hrtimeout(pdphy->tx_waitq,
		pdphy->tx_status != -EINPROGRESS,
		ms_to_ktime(HARD_RESET_COMPLETE_TIME));
	if (ret) {
		dev_err(pdphy->dev, "%s: failed ret %d\n", __func__, ret);
		return ret;
	}

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_CONTROL, 0);

	if (pdphy->tx_status)
		return pdphy->tx_status;

	if (sig == HARD_RESET_SIG)
		/* Frame filter is reconfigured in pd_phy_open() */
		return pdphy_reg_write(pdphy, USB_PDPHY_FRAME_FILTER, 0);

	return 0;
}
EXPORT_SYMBOL(pd_phy_signal);

int pd_phy_write(u16 hdr, const u8 *data, size_t data_len, enum pd_sop_type sop)
{
	u8 val;
	int ret;
	size_t total_len = data_len + USB_PDPHY_MSG_HDR_LEN;
	struct usb_pdphy *pdphy = __pdphy;
	unsigned int msg_rx_cnt;

	if (!pdphy) {
		pr_err("%s: pdphy not found\n", __func__);
		return -ENODEV;
	}

	msg_rx_cnt = pdphy->msg_rx_cnt;

	if (!pdphy->is_opened) {
		dev_dbg(pdphy->dev, "%s: pdphy disabled\n", __func__);
		return -ENODEV;
	}

	dev_dbg(pdphy->dev, "%s: hdr %x frame sop_type %d\n",
			__func__, hdr, sop);

	if (data_len > USB_PDPHY_MAX_DATA_OBJ_LEN) {
		dev_err(pdphy->dev, "%s: invalid data object len %zu\n",
			__func__, data_len);
		return -EINVAL;
	}

	ret = pdphy_reg_read(pdphy, &val, USB_PDPHY_RX_ACKNOWLEDGE, 1);
	if (ret || val) {
		dev_err(pdphy->dev, "%s: RX message pending\n", __func__);
		return -EBUSY;
	}

	pdphy->tx_status = -EINPROGRESS;

	/* write 2 byte SOP message header */
	ret = pdphy_bulk_reg_write(pdphy, USB_PDPHY_TX_BUFFER_HDR, (u8 *)&hdr,
			USB_PDPHY_MSG_HDR_LEN);
	if (ret)
		return ret;

	if (data && data_len) {
		print_hex_dump_debug("tx data obj:", DUMP_PREFIX_NONE, 32, 4,
				data, data_len, false);

		/* write data objects of SOP message */
		ret = pdphy_bulk_reg_write(pdphy, USB_PDPHY_TX_BUFFER_DATA,
				data, data_len);
		if (ret)
			return ret;
	}

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_SIZE, total_len - 1);
	if (ret)
		return ret;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_CONTROL, 0);
	if (ret)
		return ret;

	usleep_range(2, 3);

	val = (sop << 2) | TX_CONTROL_SEND_MSG;

	/* nRetryCount == 2 for PD 3.0, 3 for PD 2.0 */
	if (PD_MSG_HDR_REV(hdr) == USBPD_REV_30)
		val |= TX_CONTROL_RETRY_COUNT(2);
	else
		val |= TX_CONTROL_RETRY_COUNT(3);

	if (msg_rx_cnt != pdphy->msg_rx_cnt) {
		dev_err(pdphy->dev, "%s: RX message arrived\n", __func__);
		return -EBUSY;
	}

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_CONTROL, val);
	if (ret)
		return ret;

	ret = wait_event_interruptible_hrtimeout(pdphy->tx_waitq,
		pdphy->tx_status != -EINPROGRESS,
		ms_to_ktime(RECEIVER_RESPONSE_TIME));
	if (ret) {
		dev_err(pdphy->dev, "%s: failed ret %d\n", __func__, ret);
		return ret;
	}

	if (!pdphy->tx_status)
		pdphy->tx_bytes += data_len + USB_PDPHY_MSG_HDR_LEN;

	return pdphy->tx_status ? pdphy->tx_status : 0;
}
EXPORT_SYMBOL(pd_phy_write);

void pd_phy_close(void)
{
	int ret;
	struct usb_pdphy *pdphy = __pdphy;

	if (!pdphy) {
		pr_err("%s: pdphy not found\n", __func__);
		return;
	}

	if (!pdphy->is_opened) {
		dev_err(pdphy->dev, "%s: not opened\n", __func__);
		return;
	}

	pdphy->is_opened = false;
	pdphy_enable_irq(pdphy, false);

	pdphy->tx_status = -ESHUTDOWN;

	wake_up_all(&pdphy->tx_waitq);

	pdphy_reg_write(pdphy, USB_PDPHY_BIST_MODE, 0);
	pdphy->in_test_data_mode = false;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_TX_CONTROL, 0);
	if (ret)
		return;

	ret = pdphy_reg_write(pdphy, USB_PDPHY_EN_CONTROL, 0);
	if (ret)
		return;

	pdphy_enable_power(pdphy, false);
}
EXPORT_SYMBOL(pd_phy_close);

struct pd_phy_ops pdphy_ops = {
	.open			= pd_phy_open,
	.write			= pd_phy_write,
	.close			= pd_phy_close,
	.signal			= pd_phy_signal,
	.update_roles		= pd_phy_update_roles,
	.update_frame_filter	= pd_phy_update_frame_filter,
};

static irqreturn_t pdphy_msg_tx_irq(int irq, void *data)
{
	struct usb_pdphy *pdphy = data;

	/* TX already aborted by received signal */
	if (pdphy->tx_status != -EINPROGRESS)
		return IRQ_HANDLED;

	if (irq == pdphy->msg_tx_irq) {
		pdphy->msg_tx_cnt++;
		pdphy->tx_status = 0;
	} else if (irq == pdphy->msg_tx_discarded_irq) {
		pdphy->msg_tx_discarded_cnt++;
		pdphy->tx_status = -EBUSY;
	} else if (irq == pdphy->msg_tx_failed_irq) {
		pdphy->msg_tx_failed_cnt++;
		pdphy->tx_status = -EFAULT;
	} else {
		dev_err(pdphy->dev, "spurious irq #%d received\n", irq);
		return IRQ_NONE;
	}

	wake_up(&pdphy->tx_waitq);

	return IRQ_HANDLED;
}

static irqreturn_t pdphy_msg_rx_discarded_irq(int irq, void *data)
{
	struct usb_pdphy *pdphy = data;

	pdphy->msg_rx_discarded_cnt++;

	return IRQ_HANDLED;
}

static irqreturn_t pdphy_sig_rx_irq_thread(int irq, void *data)
{
	u8 rx_status, frame_type;
	int ret;
	struct usb_pdphy *pdphy = data;

	pdphy->sig_rx_cnt++;

	ret = pdphy_reg_read(pdphy, &rx_status, USB_PDPHY_RX_STATUS, 1);
	if (ret)
		goto done;

	frame_type = rx_status & RX_FRAME_TYPE;
	if (frame_type != HARD_RESET_SIG) {
		dev_err(pdphy->dev, "%s:unsupported frame type %d\n",
			__func__, frame_type);
		goto done;
	}

	/* Frame filter is reconfigured in pd_phy_open() */
	ret = pdphy_reg_write(pdphy, USB_PDPHY_FRAME_FILTER, 0);

	if (pdphy->signal_cb)
		pdphy->signal_cb(pdphy->usbpd, frame_type);

	if (pdphy->tx_status == -EINPROGRESS) {
		pdphy->tx_status = -EBUSY;
		wake_up(&pdphy->tx_waitq);
	}
done:
	return IRQ_HANDLED;
}

static irqreturn_t pdphy_sig_tx_irq_thread(int irq, void *data)
{
	struct usb_pdphy *pdphy = data;

	/* in case of exit from BIST Carrier Mode 2, clear BIST_MODE */
	pdphy_reg_write(pdphy, USB_PDPHY_BIST_MODE, 0);

	pdphy->sig_tx_cnt++;
	pdphy->tx_status = 0;
	wake_up(&pdphy->tx_waitq);

	return IRQ_HANDLED;
}

static int pd_phy_bist_mode(u8 bist_mode)
{
	struct usb_pdphy *pdphy = __pdphy;

	dev_dbg(pdphy->dev, "%s: enter BIST mode %d\n", __func__, bist_mode);

	pdphy_reg_write(pdphy, USB_PDPHY_BIST_MODE, 0);

	udelay(5);

	return pdphy_masked_write(pdphy, USB_PDPHY_BIST_MODE,
			BIST_MODE_MASK | BIST_ENABLE, bist_mode | BIST_ENABLE);
}

static irqreturn_t pdphy_msg_rx_irq(int irq, void *data)
{
	u8 size, rx_status, frame_type;
	u8 buf[32];
	int ret;
	struct usb_pdphy *pdphy = data;

	pdphy->msg_rx_cnt++;

	ret = pdphy_reg_read(pdphy, &size, USB_PDPHY_RX_SIZE, 1);
	if (ret)
		goto done;

	if (!size || size > 31) {
		dev_err(pdphy->dev, "%s: invalid size %d\n", __func__, size);
		goto done;
	}

	ret = pdphy_reg_read(pdphy, &rx_status, USB_PDPHY_RX_STATUS, 1);
	if (ret)
		goto done;

	frame_type = rx_status & RX_FRAME_TYPE;
	if (frame_type == SOPII_MSG) {
		dev_err(pdphy->dev, "%s:unsupported frame type %d\n",
			__func__, frame_type);
		goto done;
	}

	ret = pdphy_reg_read(pdphy, buf, USB_PDPHY_RX_BUFFER, size + 1);
	if (ret)
		goto done;

	/* ack to change ownership of rx buffer back to PDPHY RX HW */
	pdphy_reg_write(pdphy, USB_PDPHY_RX_ACKNOWLEDGE, 0);

	if (((buf[0] & 0xf) == PD_MSG_BIST) && !(buf[1] & 0x80) && size >= 5) {
		u8 mode = buf[5] >> 4; /* [31:28] of 1st data object */

		pd_phy_bist_mode(mode);
		pdphy_reg_write(pdphy, USB_PDPHY_RX_ACKNOWLEDGE, 0);

		if (mode == PD_BIST_TEST_DATA_MODE) {
			pdphy->in_test_data_mode = true;
			disable_irq_nosync(irq);
		}
		goto done;
	}

	if (pdphy->msg_rx_cb)
		pdphy->msg_rx_cb(pdphy->usbpd, frame_type, buf, size + 1);

	print_hex_dump_debug("rx msg:", DUMP_PREFIX_NONE, 32, 4, buf, size + 1,
		false);
	pdphy->rx_bytes += size + 1;
done:
	return IRQ_HANDLED;
}

static int pdphy_request_irq(struct usb_pdphy *pdphy,
				struct device_node *node,
				int *irq_num, const char *irq_name,
				irqreturn_t (irq_handler)(int irq, void *data),
				irqreturn_t (thread_fn)(int irq, void *data),
				int flags)
{
	int ret;

	*irq_num = of_irq_get_byname(node, irq_name);
	if (*irq_num < 0) {
		dev_err(pdphy->dev, "Unable to get %s irq\n", irq_name);
		ret = -ENXIO;
	}

	irq_set_status_flags(*irq_num, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(pdphy->dev, *irq_num, irq_handler,
			thread_fn, flags, irq_name, pdphy);
	if (ret < 0) {
		dev_err(pdphy->dev, "Unable to request %s irq: %d\n",
				irq_name, ret);
		ret = -ENXIO;
	}

	return 0;
}

static int pdphy_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int base;
	struct usb_pdphy *pdphy;

	pdphy = devm_kzalloc(&pdev->dev, sizeof(*pdphy), GFP_KERNEL);
	if (!pdphy)
		return -ENOMEM;

	pdphy->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pdphy->regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	dev_set_drvdata(&pdev->dev, pdphy);

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &base);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get reg base address ret = %d\n",
			ret);
		return ret;
	}

	pdphy->base = base;
	pdphy->dev = &pdev->dev;

	init_waitqueue_head(&pdphy->tx_waitq);

	pdphy->vdd_pdphy = devm_regulator_get(&pdev->dev, "vdd-pdphy");
	if (IS_ERR(pdphy->vdd_pdphy)) {
		dev_err(&pdev->dev, "unable to get vdd-pdphy\n");
		return PTR_ERR(pdphy->vdd_pdphy);
	}

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->sig_tx_irq, "sig-tx", NULL,
		pdphy_sig_tx_irq_thread, (IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->sig_rx_irq, "sig-rx", NULL,
		pdphy_sig_rx_irq_thread, (IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->msg_tx_irq, "msg-tx", pdphy_msg_tx_irq,
		NULL, (IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->msg_rx_irq, "msg-rx", pdphy_msg_rx_irq,
		NULL, (IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->msg_tx_failed_irq, "msg-tx-failed", pdphy_msg_tx_irq,
		NULL, (IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->msg_tx_discarded_irq, "msg-tx-discarded",
		pdphy_msg_tx_irq, NULL,
		(IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	ret = pdphy_request_irq(pdphy, pdev->dev.of_node,
		&pdphy->msg_rx_discarded_irq, "msg-rx-discarded",
		pdphy_msg_rx_discarded_irq, NULL,
		(IRQF_TRIGGER_RISING | IRQF_ONESHOT));
	if (ret < 0)
		return ret;

	/* usbpd_create() could call back to us, so have __pdphy ready */
	__pdphy = pdphy;

	pdphy->usbpd = usbpd_create(&pdev->dev, &pdphy_ops);
	if (IS_ERR(pdphy->usbpd)) {
		dev_err(&pdev->dev, "usbpd_create failed: %ld\n",
				PTR_ERR(pdphy->usbpd));
		__pdphy = NULL;
		return PTR_ERR(pdphy->usbpd);
	}

	pdphy_create_debugfs_entries(pdphy);

	return 0;
}

static int pdphy_remove(struct platform_device *pdev)
{
	struct usb_pdphy *pdphy = platform_get_drvdata(pdev);

	debugfs_remove_recursive(pdphy->debug_root);
	usbpd_destroy(pdphy->usbpd);

	if (pdphy->is_opened)
		pd_phy_close();

	__pdphy = NULL;

	return 0;
}

static void pdphy_shutdown(struct platform_device *pdev)
{
	struct usb_pdphy *pdphy = platform_get_drvdata(pdev);

	/* let protocol engine shutdown the pdphy synchronously */
	if (pdphy->shutdown_cb)
		pdphy->shutdown_cb(pdphy->usbpd);
}

static const struct of_device_id pdphy_match_table[] = {
	{
		.compatible	 = "qcom,qpnp-pdphy",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, pdphy_match_table);

static struct platform_driver pdphy_driver = {
	 .driver	 = {
		 .name			= "qpnp-pdphy",
		 .of_match_table	= pdphy_match_table,
	 },
	 .probe		= pdphy_probe,
	 .remove	= pdphy_remove,
	 .shutdown	= pdphy_shutdown,
};

module_platform_driver(pdphy_driver);

MODULE_DESCRIPTION("QPNP PD PHY Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:qpnp-pdphy");
