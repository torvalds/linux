// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth HCI UART driver for Broadcom devices
 *
 *  Copyright (C) 2015  Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/platform_data/x86/apple.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/tty.h>
#include <linux/interrupt.h>
#include <linux/dmi.h>
#include <linux/pm_runtime.h>
#include <linux/serdev.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btbcm.h"
#include "hci_uart.h"

#define BCM_NULL_PKT 0x00
#define BCM_NULL_SIZE 0

#define BCM_LM_DIAG_PKT 0x07
#define BCM_LM_DIAG_SIZE 63

#define BCM_TYPE49_PKT 0x31
#define BCM_TYPE49_SIZE 0

#define BCM_TYPE52_PKT 0x34
#define BCM_TYPE52_SIZE 0

#define BCM_AUTOSUSPEND_DELAY	5000 /* default autosleep delay */

#define BCM_NUM_SUPPLIES 2

/**
 * struct bcm_device - device driver resources
 * @serdev_hu: HCI UART controller struct
 * @list: bcm_device_list node
 * @dev: physical UART slave
 * @name: device name logged by bt_dev_*() functions
 * @device_wakeup: BT_WAKE pin,
 *	assert = Bluetooth device must wake up or remain awake,
 *	deassert = Bluetooth device may sleep when sleep criteria are met
 * @shutdown: BT_REG_ON pin,
 *	power up or power down Bluetooth device internal regulators
 * @set_device_wakeup: callback to toggle BT_WAKE pin
 *	either by accessing @device_wakeup or by calling @btlp
 * @set_shutdown: callback to toggle BT_REG_ON pin
 *	either by accessing @shutdown or by calling @btpu/@btpd
 * @btlp: Apple ACPI method to toggle BT_WAKE pin ("Bluetooth Low Power")
 * @btpu: Apple ACPI method to drive BT_REG_ON pin high ("Bluetooth Power Up")
 * @btpd: Apple ACPI method to drive BT_REG_ON pin low ("Bluetooth Power Down")
 * @txco_clk: external reference frequency clock used by Bluetooth device
 * @lpo_clk: external LPO clock used by Bluetooth device
 * @supplies: VBAT and VDDIO supplies used by Bluetooth device
 * @res_enabled: whether clocks and supplies are prepared and enabled
 * @init_speed: default baudrate of Bluetooth device;
 *	the host UART is initially set to this baudrate so that
 *	it can configure the Bluetooth device for @oper_speed
 * @oper_speed: preferred baudrate of Bluetooth device;
 *	set to 0 if @init_speed is already the preferred baudrate
 * @irq: interrupt triggered by HOST_WAKE_BT pin
 * @irq_active_low: whether @irq is active low
 * @hu: pointer to HCI UART controller struct,
 *	used to disable flow control during runtime suspend and system sleep
 * @is_suspended: whether flow control is currently disabled
 */
struct bcm_device {
	/* Must be the first member, hci_serdev.c expects this. */
	struct hci_uart		serdev_hu;
	struct list_head	list;

	struct device		*dev;

	const char		*name;
	struct gpio_desc	*device_wakeup;
	struct gpio_desc	*shutdown;
	int			(*set_device_wakeup)(struct bcm_device *, bool);
	int			(*set_shutdown)(struct bcm_device *, bool);
#ifdef CONFIG_ACPI
	acpi_handle		btlp, btpu, btpd;
	int			gpio_count;
	int			gpio_int_idx;
#endif

	struct clk		*txco_clk;
	struct clk		*lpo_clk;
	struct regulator_bulk_data supplies[BCM_NUM_SUPPLIES];
	bool			res_enabled;

	u32			init_speed;
	u32			oper_speed;
	int			irq;
	bool			irq_active_low;

#ifdef CONFIG_PM
	struct hci_uart		*hu;
	bool			is_suspended;
#endif
};

/* generic bcm uart resources */
struct bcm_data {
	struct sk_buff		*rx_skb;
	struct sk_buff_head	txq;

	struct bcm_device	*dev;
};

/* List of BCM BT UART devices */
static DEFINE_MUTEX(bcm_device_lock);
static LIST_HEAD(bcm_device_list);

static int irq_polarity = -1;
module_param(irq_polarity, int, 0444);
MODULE_PARM_DESC(irq_polarity, "IRQ polarity 0: active-high 1: active-low");

static inline void host_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	if (hu->serdev)
		serdev_device_set_baudrate(hu->serdev, speed);
	else
		hci_uart_set_baudrate(hu, speed);
}

static int bcm_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	struct hci_dev *hdev = hu->hdev;
	struct sk_buff *skb;
	struct bcm_update_uart_baud_rate param;

	if (speed > 3000000) {
		struct bcm_write_uart_clock_setting clock;

		clock.type = BCM_UART_CLOCK_48MHZ;

		bt_dev_dbg(hdev, "Set Controller clock (%d)", clock.type);

		/* This Broadcom specific command changes the UART's controller
		 * clock for baud rate > 3000000.
		 */
		skb = __hci_cmd_sync(hdev, 0xfc45, 1, &clock, HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			int err = PTR_ERR(skb);
			bt_dev_err(hdev, "BCM: failed to write clock (%d)",
				   err);
			return err;
		}

		kfree_skb(skb);
	}

	bt_dev_dbg(hdev, "Set Controller UART speed to %d bit/s", speed);

	param.zero = cpu_to_le16(0);
	param.baud_rate = cpu_to_le32(speed);

	/* This Broadcom specific command changes the UART's controller baud
	 * rate.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc18, sizeof(param), &param,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		bt_dev_err(hdev, "BCM: failed to write update baudrate (%d)",
			   err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}

/* bcm_device_exists should be protected by bcm_device_lock */
static bool bcm_device_exists(struct bcm_device *device)
{
	struct list_head *p;

#ifdef CONFIG_PM
	/* Devices using serdev always exist */
	if (device && device->hu && device->hu->serdev)
		return true;
#endif

	list_for_each(p, &bcm_device_list) {
		struct bcm_device *dev = list_entry(p, struct bcm_device, list);

		if (device == dev)
			return true;
	}

	return false;
}

static int bcm_gpio_set_power(struct bcm_device *dev, bool powered)
{
	int err;

	if (powered && !dev->res_enabled) {
		/* Intel Macs use bcm_apple_get_resources() and don't
		 * have regulator supplies configured.
		 */
		if (dev->supplies[0].supply) {
			err = regulator_bulk_enable(BCM_NUM_SUPPLIES,
						    dev->supplies);
			if (err)
				return err;
		}

		/* LPO clock needs to be 32.768 kHz */
		err = clk_set_rate(dev->lpo_clk, 32768);
		if (err) {
			dev_err(dev->dev, "Could not set LPO clock rate\n");
			goto err_regulator_disable;
		}

		err = clk_prepare_enable(dev->lpo_clk);
		if (err)
			goto err_regulator_disable;

		err = clk_prepare_enable(dev->txco_clk);
		if (err)
			goto err_lpo_clk_disable;
	}

	err = dev->set_shutdown(dev, powered);
	if (err)
		goto err_txco_clk_disable;

	err = dev->set_device_wakeup(dev, powered);
	if (err)
		goto err_revert_shutdown;

	if (!powered && dev->res_enabled) {
		clk_disable_unprepare(dev->txco_clk);
		clk_disable_unprepare(dev->lpo_clk);

		/* Intel Macs use bcm_apple_get_resources() and don't
		 * have regulator supplies configured.
		 */
		if (dev->supplies[0].supply)
			regulator_bulk_disable(BCM_NUM_SUPPLIES,
					       dev->supplies);
	}

	/* wait for device to power on and come out of reset */
	usleep_range(100000, 120000);

	dev->res_enabled = powered;

	return 0;

err_revert_shutdown:
	dev->set_shutdown(dev, !powered);
err_txco_clk_disable:
	if (powered && !dev->res_enabled)
		clk_disable_unprepare(dev->txco_clk);
err_lpo_clk_disable:
	if (powered && !dev->res_enabled)
		clk_disable_unprepare(dev->lpo_clk);
err_regulator_disable:
	if (powered && !dev->res_enabled)
		regulator_bulk_disable(BCM_NUM_SUPPLIES, dev->supplies);
	return err;
}

#ifdef CONFIG_PM
static irqreturn_t bcm_host_wake(int irq, void *data)
{
	struct bcm_device *bdev = data;

	bt_dev_dbg(bdev, "Host wake IRQ");

	pm_runtime_get(bdev->dev);
	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);

	return IRQ_HANDLED;
}

static int bcm_request_irq(struct bcm_data *bcm)
{
	struct bcm_device *bdev = bcm->dev;
	int err;

	mutex_lock(&bcm_device_lock);
	if (!bcm_device_exists(bdev)) {
		err = -ENODEV;
		goto unlock;
	}

	if (bdev->irq <= 0) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	err = devm_request_irq(bdev->dev, bdev->irq, bcm_host_wake,
			       bdev->irq_active_low ? IRQF_TRIGGER_FALLING :
						      IRQF_TRIGGER_RISING,
			       "host_wake", bdev);
	if (err) {
		bdev->irq = err;
		goto unlock;
	}

	device_init_wakeup(bdev->dev, true);

	pm_runtime_set_autosuspend_delay(bdev->dev,
					 BCM_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(bdev->dev);
	pm_runtime_set_active(bdev->dev);
	pm_runtime_enable(bdev->dev);

unlock:
	mutex_unlock(&bcm_device_lock);

	return err;
}

static const struct bcm_set_sleep_mode default_sleep_params = {
	.sleep_mode = 1,	/* 0=Disabled, 1=UART, 2=Reserved, 3=USB */
	.idle_host = 2,		/* idle threshold HOST, in 300ms */
	.idle_dev = 2,		/* idle threshold device, in 300ms */
	.bt_wake_active = 1,	/* BT_WAKE active mode: 1 = high, 0 = low */
	.host_wake_active = 0,	/* HOST_WAKE active mode: 1 = high, 0 = low */
	.allow_host_sleep = 1,	/* Allow host sleep in SCO flag */
	.combine_modes = 1,	/* Combine sleep and LPM flag */
	.tristate_control = 0,	/* Allow tri-state control of UART tx flag */
	/* Irrelevant USB flags */
	.usb_auto_sleep = 0,
	.usb_resume_timeout = 0,
	.break_to_host = 0,
	.pulsed_host_wake = 1,
};

static int bcm_setup_sleep(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;
	struct sk_buff *skb;
	struct bcm_set_sleep_mode sleep_params = default_sleep_params;

	sleep_params.host_wake_active = !bcm->dev->irq_active_low;

	skb = __hci_cmd_sync(hu->hdev, 0xfc27, sizeof(sleep_params),
			     &sleep_params, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		bt_dev_err(hu->hdev, "Sleep VSC failed (%d)", err);
		return err;
	}
	kfree_skb(skb);

	bt_dev_dbg(hu->hdev, "Set Sleep Parameters VSC succeeded");

	return 0;
}
#else
static inline int bcm_request_irq(struct bcm_data *bcm) { return 0; }
static inline int bcm_setup_sleep(struct hci_uart *hu) { return 0; }
#endif

static int bcm_set_diag(struct hci_dev *hdev, bool enable)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct bcm_data *bcm = hu->priv;
	struct sk_buff *skb;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -ENETDOWN;

	skb = bt_skb_alloc(3, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, BCM_LM_DIAG_PKT);
	skb_put_u8(skb, 0xf0);
	skb_put_u8(skb, enable);

	skb_queue_tail(&bcm->txq, skb);
	hci_uart_tx_wakeup(hu);

	return 0;
}

static int bcm_open(struct hci_uart *hu)
{
	struct bcm_data *bcm;
	struct list_head *p;
	int err;

	bt_dev_dbg(hu->hdev, "hu %p", hu);

	if (!hci_uart_has_flow_control(hu))
		return -EOPNOTSUPP;

	bcm = kzalloc(sizeof(*bcm), GFP_KERNEL);
	if (!bcm)
		return -ENOMEM;

	skb_queue_head_init(&bcm->txq);

	hu->priv = bcm;

	mutex_lock(&bcm_device_lock);

	if (hu->serdev) {
		bcm->dev = serdev_device_get_drvdata(hu->serdev);
		goto out;
	}

	if (!hu->tty->dev)
		goto out;

	list_for_each(p, &bcm_device_list) {
		struct bcm_device *dev = list_entry(p, struct bcm_device, list);

		/* Retrieve saved bcm_device based on parent of the
		 * platform device (saved during device probe) and
		 * parent of tty device used by hci_uart
		 */
		if (hu->tty->dev->parent == dev->dev->parent) {
			bcm->dev = dev;
#ifdef CONFIG_PM
			dev->hu = hu;
#endif
			break;
		}
	}

out:
	if (bcm->dev) {
		hci_uart_set_flow_control(hu, true);
		hu->init_speed = bcm->dev->init_speed;
		hu->oper_speed = bcm->dev->oper_speed;
		err = bcm_gpio_set_power(bcm->dev, true);
		hci_uart_set_flow_control(hu, false);
		if (err)
			goto err_unset_hu;
	}

	mutex_unlock(&bcm_device_lock);
	return 0;

err_unset_hu:
#ifdef CONFIG_PM
	if (!hu->serdev)
		bcm->dev->hu = NULL;
#endif
	mutex_unlock(&bcm_device_lock);
	hu->priv = NULL;
	kfree(bcm);
	return err;
}

static int bcm_close(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;
	struct bcm_device *bdev = NULL;
	int err;

	bt_dev_dbg(hu->hdev, "hu %p", hu);

	/* Protect bcm->dev against removal of the device or driver */
	mutex_lock(&bcm_device_lock);

	if (hu->serdev) {
		bdev = serdev_device_get_drvdata(hu->serdev);
	} else if (bcm_device_exists(bcm->dev)) {
		bdev = bcm->dev;
#ifdef CONFIG_PM
		bdev->hu = NULL;
#endif
	}

	if (bdev) {
		if (IS_ENABLED(CONFIG_PM) && bdev->irq > 0) {
			devm_free_irq(bdev->dev, bdev->irq, bdev);
			device_init_wakeup(bdev->dev, false);
			pm_runtime_disable(bdev->dev);
		}

		err = bcm_gpio_set_power(bdev, false);
		if (err)
			bt_dev_err(hu->hdev, "Failed to power down");
		else
			pm_runtime_set_suspended(bdev->dev);
	}
	mutex_unlock(&bcm_device_lock);

	skb_queue_purge(&bcm->txq);
	kfree_skb(bcm->rx_skb);
	kfree(bcm);

	hu->priv = NULL;
	return 0;
}

static int bcm_flush(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;

	bt_dev_dbg(hu->hdev, "hu %p", hu);

	skb_queue_purge(&bcm->txq);

	return 0;
}

static int bcm_setup(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;
	char fw_name[64];
	const struct firmware *fw;
	unsigned int speed;
	int err;

	bt_dev_dbg(hu->hdev, "hu %p", hu);

	hu->hdev->set_diag = bcm_set_diag;
	hu->hdev->set_bdaddr = btbcm_set_bdaddr;

	err = btbcm_initialize(hu->hdev, fw_name, sizeof(fw_name), false);
	if (err)
		return err;

	err = request_firmware(&fw, fw_name, &hu->hdev->dev);
	if (err < 0) {
		bt_dev_info(hu->hdev, "BCM: Patch %s not found", fw_name);
		return 0;
	}

	err = btbcm_patchram(hu->hdev, fw);
	if (err) {
		bt_dev_info(hu->hdev, "BCM: Patch failed (%d)", err);
		goto finalize;
	}

	/* Init speed if any */
	if (hu->init_speed)
		speed = hu->init_speed;
	else if (hu->proto->init_speed)
		speed = hu->proto->init_speed;
	else
		speed = 0;

	if (speed)
		host_set_baudrate(hu, speed);

	/* Operational speed if any */
	if (hu->oper_speed)
		speed = hu->oper_speed;
	else if (hu->proto->oper_speed)
		speed = hu->proto->oper_speed;
	else
		speed = 0;

	if (speed) {
		err = bcm_set_baudrate(hu, speed);
		if (!err)
			host_set_baudrate(hu, speed);
	}

finalize:
	release_firmware(fw);

	err = btbcm_finalize(hu->hdev);
	if (err)
		return err;

	if (!bcm_request_irq(bcm))
		err = bcm_setup_sleep(hu);

	return err;
}

#define BCM_RECV_LM_DIAG \
	.type = BCM_LM_DIAG_PKT, \
	.hlen = BCM_LM_DIAG_SIZE, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = BCM_LM_DIAG_SIZE

#define BCM_RECV_NULL \
	.type = BCM_NULL_PKT, \
	.hlen = BCM_NULL_SIZE, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = BCM_NULL_SIZE

#define BCM_RECV_TYPE49 \
	.type = BCM_TYPE49_PKT, \
	.hlen = BCM_TYPE49_SIZE, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = BCM_TYPE49_SIZE

#define BCM_RECV_TYPE52 \
	.type = BCM_TYPE52_PKT, \
	.hlen = BCM_TYPE52_SIZE, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = BCM_TYPE52_SIZE

static const struct h4_recv_pkt bcm_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = hci_recv_frame },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = hci_recv_frame },
	{ BCM_RECV_LM_DIAG, .recv = hci_recv_diag  },
	{ BCM_RECV_NULL,    .recv = hci_recv_diag  },
	{ BCM_RECV_TYPE49,  .recv = hci_recv_diag  },
	{ BCM_RECV_TYPE52,  .recv = hci_recv_diag  },
};

static int bcm_recv(struct hci_uart *hu, const void *data, int count)
{
	struct bcm_data *bcm = hu->priv;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;

	bcm->rx_skb = h4_recv_buf(hu->hdev, bcm->rx_skb, data, count,
				  bcm_recv_pkts, ARRAY_SIZE(bcm_recv_pkts));
	if (IS_ERR(bcm->rx_skb)) {
		int err = PTR_ERR(bcm->rx_skb);
		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		bcm->rx_skb = NULL;
		return err;
	} else if (!bcm->rx_skb) {
		/* Delay auto-suspend when receiving completed packet */
		mutex_lock(&bcm_device_lock);
		if (bcm->dev && bcm_device_exists(bcm->dev)) {
			pm_runtime_get(bcm->dev->dev);
			pm_runtime_mark_last_busy(bcm->dev->dev);
			pm_runtime_put_autosuspend(bcm->dev->dev);
		}
		mutex_unlock(&bcm_device_lock);
	}

	return count;
}

static int bcm_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct bcm_data *bcm = hu->priv;

	bt_dev_dbg(hu->hdev, "hu %p skb %p", hu, skb);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&bcm->txq, skb);

	return 0;
}

static struct sk_buff *bcm_dequeue(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;
	struct sk_buff *skb = NULL;
	struct bcm_device *bdev = NULL;

	mutex_lock(&bcm_device_lock);

	if (bcm_device_exists(bcm->dev)) {
		bdev = bcm->dev;
		pm_runtime_get_sync(bdev->dev);
		/* Shall be resumed here */
	}

	skb = skb_dequeue(&bcm->txq);

	if (bdev) {
		pm_runtime_mark_last_busy(bdev->dev);
		pm_runtime_put_autosuspend(bdev->dev);
	}

	mutex_unlock(&bcm_device_lock);

	return skb;
}

#ifdef CONFIG_PM
static int bcm_suspend_device(struct device *dev)
{
	struct bcm_device *bdev = dev_get_drvdata(dev);
	int err;

	bt_dev_dbg(bdev, "");

	if (!bdev->is_suspended && bdev->hu) {
		hci_uart_set_flow_control(bdev->hu, true);

		/* Once this returns, driver suspends BT via GPIO */
		bdev->is_suspended = true;
	}

	/* Suspend the device */
	err = bdev->set_device_wakeup(bdev, false);
	if (err) {
		if (bdev->is_suspended && bdev->hu) {
			bdev->is_suspended = false;
			hci_uart_set_flow_control(bdev->hu, false);
		}
		return -EBUSY;
	}

	bt_dev_dbg(bdev, "suspend, delaying 15 ms");
	msleep(15);

	return 0;
}

static int bcm_resume_device(struct device *dev)
{
	struct bcm_device *bdev = dev_get_drvdata(dev);
	int err;

	bt_dev_dbg(bdev, "");

	err = bdev->set_device_wakeup(bdev, true);
	if (err) {
		dev_err(dev, "Failed to power up\n");
		return err;
	}

	bt_dev_dbg(bdev, "resume, delaying 15 ms");
	msleep(15);

	/* When this executes, the device has woken up already */
	if (bdev->is_suspended && bdev->hu) {
		bdev->is_suspended = false;

		hci_uart_set_flow_control(bdev->hu, false);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
/* suspend callback */
static int bcm_suspend(struct device *dev)
{
	struct bcm_device *bdev = dev_get_drvdata(dev);
	int error;

	bt_dev_dbg(bdev, "suspend: is_suspended %d", bdev->is_suspended);

	/*
	 * When used with a device instantiated as platform_device, bcm_suspend
	 * can be called at any time as long as the platform device is bound,
	 * so it should use bcm_device_lock to protect access to hci_uart
	 * and device_wake-up GPIO.
	 */
	mutex_lock(&bcm_device_lock);

	if (!bdev->hu)
		goto unlock;

	if (pm_runtime_active(dev))
		bcm_suspend_device(dev);

	if (device_may_wakeup(dev) && bdev->irq > 0) {
		error = enable_irq_wake(bdev->irq);
		if (!error)
			bt_dev_dbg(bdev, "BCM irq: enabled");
	}

unlock:
	mutex_unlock(&bcm_device_lock);

	return 0;
}

/* resume callback */
static int bcm_resume(struct device *dev)
{
	struct bcm_device *bdev = dev_get_drvdata(dev);
	int err = 0;

	bt_dev_dbg(bdev, "resume: is_suspended %d", bdev->is_suspended);

	/*
	 * When used with a device instantiated as platform_device, bcm_resume
	 * can be called at any time as long as platform device is bound,
	 * so it should use bcm_device_lock to protect access to hci_uart
	 * and device_wake-up GPIO.
	 */
	mutex_lock(&bcm_device_lock);

	if (!bdev->hu)
		goto unlock;

	if (device_may_wakeup(dev) && bdev->irq > 0) {
		disable_irq_wake(bdev->irq);
		bt_dev_dbg(bdev, "BCM irq: disabled");
	}

	err = bcm_resume_device(dev);

unlock:
	mutex_unlock(&bcm_device_lock);

	if (!err) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return 0;
}
#endif

/* Some firmware reports an IRQ which does not work (wrong pin in fw table?) */
static const struct dmi_system_id bcm_broken_irq_dmi_table[] = {
	{
		.ident = "Meegopad T08",
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR,
					"To be filled by OEM."),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "T3 MRD"),
			DMI_EXACT_MATCH(DMI_BOARD_VERSION, "V1.1"),
		},
	},
	{ }
};

#ifdef CONFIG_ACPI
static const struct acpi_gpio_params first_gpio = { 0, 0, false };
static const struct acpi_gpio_params second_gpio = { 1, 0, false };
static const struct acpi_gpio_params third_gpio = { 2, 0, false };

static const struct acpi_gpio_mapping acpi_bcm_int_last_gpios[] = {
	{ "device-wakeup-gpios", &first_gpio, 1 },
	{ "shutdown-gpios", &second_gpio, 1 },
	{ "host-wakeup-gpios", &third_gpio, 1 },
	{ },
};

static const struct acpi_gpio_mapping acpi_bcm_int_first_gpios[] = {
	{ "host-wakeup-gpios", &first_gpio, 1 },
	{ "device-wakeup-gpios", &second_gpio, 1 },
	{ "shutdown-gpios", &third_gpio, 1 },
	{ },
};

static int bcm_resource(struct acpi_resource *ares, void *data)
{
	struct bcm_device *dev = data;
	struct acpi_resource_extended_irq *irq;
	struct acpi_resource_gpio *gpio;
	struct acpi_resource_uart_serialbus *sb;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		irq = &ares->data.extended_irq;
		if (irq->polarity != ACPI_ACTIVE_LOW)
			dev_info(dev->dev, "ACPI Interrupt resource is active-high, this is usually wrong, treating the IRQ as active-low\n");
		dev->irq_active_low = true;
		break;

	case ACPI_RESOURCE_TYPE_GPIO:
		gpio = &ares->data.gpio;
		if (gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT) {
			dev->gpio_int_idx = dev->gpio_count;
			dev->irq_active_low = gpio->polarity == ACPI_ACTIVE_LOW;
		}
		dev->gpio_count++;
		break;

	case ACPI_RESOURCE_TYPE_SERIAL_BUS:
		sb = &ares->data.uart_serial_bus;
		if (sb->type == ACPI_RESOURCE_SERIAL_TYPE_UART) {
			dev->init_speed = sb->default_baud_rate;
			dev->oper_speed = 4000000;
		}
		break;

	default:
		break;
	}

	return 0;
}

static int bcm_apple_set_device_wakeup(struct bcm_device *dev, bool awake)
{
	if (ACPI_FAILURE(acpi_execute_simple_method(dev->btlp, NULL, !awake)))
		return -EIO;

	return 0;
}

static int bcm_apple_set_shutdown(struct bcm_device *dev, bool powered)
{
	if (ACPI_FAILURE(acpi_evaluate_object(powered ? dev->btpu : dev->btpd,
					      NULL, NULL, NULL)))
		return -EIO;

	return 0;
}

static int bcm_apple_get_resources(struct bcm_device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev->dev);
	const union acpi_object *obj;

	if (!adev ||
	    ACPI_FAILURE(acpi_get_handle(adev->handle, "BTLP", &dev->btlp)) ||
	    ACPI_FAILURE(acpi_get_handle(adev->handle, "BTPU", &dev->btpu)) ||
	    ACPI_FAILURE(acpi_get_handle(adev->handle, "BTPD", &dev->btpd)))
		return -ENODEV;

	if (!acpi_dev_get_property(adev, "baud", ACPI_TYPE_BUFFER, &obj) &&
	    obj->buffer.length == 8)
		dev->init_speed = *(u64 *)obj->buffer.pointer;

	dev->set_device_wakeup = bcm_apple_set_device_wakeup;
	dev->set_shutdown = bcm_apple_set_shutdown;

	return 0;
}
#else
static inline int bcm_apple_get_resources(struct bcm_device *dev)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_ACPI */

static int bcm_gpio_set_device_wakeup(struct bcm_device *dev, bool awake)
{
	gpiod_set_value_cansleep(dev->device_wakeup, awake);
	return 0;
}

static int bcm_gpio_set_shutdown(struct bcm_device *dev, bool powered)
{
	gpiod_set_value_cansleep(dev->shutdown, powered);
	return 0;
}

/* Try a bunch of names for TXCO */
static struct clk *bcm_get_txco(struct device *dev)
{
	struct clk *clk;

	/* New explicit name */
	clk = devm_clk_get(dev, "txco");
	if (!IS_ERR(clk) || PTR_ERR(clk) == -EPROBE_DEFER)
		return clk;

	/* Deprecated name */
	clk = devm_clk_get(dev, "extclk");
	if (!IS_ERR(clk) || PTR_ERR(clk) == -EPROBE_DEFER)
		return clk;

	/* Original code used no name at all */
	return devm_clk_get(dev, NULL);
}

static int bcm_get_resources(struct bcm_device *dev)
{
	const struct dmi_system_id *dmi_id;
	int err;

	dev->name = dev_name(dev->dev);

	if (x86_apple_machine && !bcm_apple_get_resources(dev))
		return 0;

	dev->txco_clk = bcm_get_txco(dev->dev);

	/* Handle deferred probing */
	if (dev->txco_clk == ERR_PTR(-EPROBE_DEFER))
		return PTR_ERR(dev->txco_clk);

	/* Ignore all other errors as before */
	if (IS_ERR(dev->txco_clk))
		dev->txco_clk = NULL;

	dev->lpo_clk = devm_clk_get(dev->dev, "lpo");
	if (dev->lpo_clk == ERR_PTR(-EPROBE_DEFER))
		return PTR_ERR(dev->lpo_clk);

	if (IS_ERR(dev->lpo_clk))
		dev->lpo_clk = NULL;

	/* Check if we accidentally fetched the lpo clock twice */
	if (dev->lpo_clk && clk_is_match(dev->lpo_clk, dev->txco_clk)) {
		devm_clk_put(dev->dev, dev->txco_clk);
		dev->txco_clk = NULL;
	}

	dev->device_wakeup = devm_gpiod_get_optional(dev->dev, "device-wakeup",
						     GPIOD_OUT_LOW);
	if (IS_ERR(dev->device_wakeup))
		return PTR_ERR(dev->device_wakeup);

	dev->shutdown = devm_gpiod_get_optional(dev->dev, "shutdown",
						GPIOD_OUT_LOW);
	if (IS_ERR(dev->shutdown))
		return PTR_ERR(dev->shutdown);

	dev->set_device_wakeup = bcm_gpio_set_device_wakeup;
	dev->set_shutdown = bcm_gpio_set_shutdown;

	dev->supplies[0].supply = "vbat";
	dev->supplies[1].supply = "vddio";
	err = devm_regulator_bulk_get(dev->dev, BCM_NUM_SUPPLIES,
				      dev->supplies);
	if (err)
		return err;

	/* IRQ can be declared in ACPI table as Interrupt or GpioInt */
	if (dev->irq <= 0) {
		struct gpio_desc *gpio;

		gpio = devm_gpiod_get_optional(dev->dev, "host-wakeup",
					       GPIOD_IN);
		if (IS_ERR(gpio))
			return PTR_ERR(gpio);

		dev->irq = gpiod_to_irq(gpio);
	}

	dmi_id = dmi_first_match(bcm_broken_irq_dmi_table);
	if (dmi_id) {
		dev_info(dev->dev, "%s: Has a broken IRQ config, disabling IRQ support / runtime-pm\n",
			 dmi_id->ident);
		dev->irq = 0;
	}

	dev_dbg(dev->dev, "BCM irq: %d\n", dev->irq);
	return 0;
}

#ifdef CONFIG_ACPI
static int bcm_acpi_probe(struct bcm_device *dev)
{
	LIST_HEAD(resources);
	const struct acpi_gpio_mapping *gpio_mapping = acpi_bcm_int_last_gpios;
	struct resource_entry *entry;
	int ret;

	/* Retrieve UART ACPI info */
	dev->gpio_int_idx = -1;
	ret = acpi_dev_get_resources(ACPI_COMPANION(dev->dev),
				     &resources, bcm_resource, dev);
	if (ret < 0)
		return ret;

	resource_list_for_each_entry(entry, &resources) {
		if (resource_type(entry->res) == IORESOURCE_IRQ) {
			dev->irq = entry->res->start;
			break;
		}
	}
	acpi_dev_free_resource_list(&resources);

	/* If the DSDT uses an Interrupt resource for the IRQ, then there are
	 * only 2 GPIO resources, we use the irq-last mapping for this, since
	 * we already have an irq the 3th / last mapping will not be used.
	 */
	if (dev->irq)
		gpio_mapping = acpi_bcm_int_last_gpios;
	else if (dev->gpio_int_idx == 0)
		gpio_mapping = acpi_bcm_int_first_gpios;
	else if (dev->gpio_int_idx == 2)
		gpio_mapping = acpi_bcm_int_last_gpios;
	else
		dev_warn(dev->dev, "Unexpected ACPI gpio_int_idx: %d\n",
			 dev->gpio_int_idx);

	/* Warn if our expectations are not met. */
	if (dev->gpio_count != (dev->irq ? 2 : 3))
		dev_warn(dev->dev, "Unexpected number of ACPI GPIOs: %d\n",
			 dev->gpio_count);

	ret = devm_acpi_dev_add_driver_gpios(dev->dev, gpio_mapping);
	if (ret)
		return ret;

	if (irq_polarity != -1) {
		dev->irq_active_low = irq_polarity;
		dev_warn(dev->dev, "Overwriting IRQ polarity to active %s by module-param\n",
			 dev->irq_active_low ? "low" : "high");
	}

	return 0;
}
#else
static int bcm_acpi_probe(struct bcm_device *dev)
{
	return -EINVAL;
}
#endif /* CONFIG_ACPI */

static int bcm_of_probe(struct bcm_device *bdev)
{
	device_property_read_u32(bdev->dev, "max-speed", &bdev->oper_speed);
	return 0;
}

static int bcm_probe(struct platform_device *pdev)
{
	struct bcm_device *dev;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &pdev->dev;
	dev->irq = platform_get_irq(pdev, 0);

	if (has_acpi_companion(&pdev->dev)) {
		ret = bcm_acpi_probe(dev);
		if (ret)
			return ret;
	}

	ret = bcm_get_resources(dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dev);

	dev_info(&pdev->dev, "%s device registered.\n", dev->name);

	/* Place this instance on the device list */
	mutex_lock(&bcm_device_lock);
	list_add_tail(&dev->list, &bcm_device_list);
	mutex_unlock(&bcm_device_lock);

	ret = bcm_gpio_set_power(dev, false);
	if (ret)
		dev_err(&pdev->dev, "Failed to power down\n");

	return 0;
}

static int bcm_remove(struct platform_device *pdev)
{
	struct bcm_device *dev = platform_get_drvdata(pdev);

	mutex_lock(&bcm_device_lock);
	list_del(&dev->list);
	mutex_unlock(&bcm_device_lock);

	dev_info(&pdev->dev, "%s device unregistered.\n", dev->name);

	return 0;
}

static const struct hci_uart_proto bcm_proto = {
	.id		= HCI_UART_BCM,
	.name		= "Broadcom",
	.manufacturer	= 15,
	.init_speed	= 115200,
	.open		= bcm_open,
	.close		= bcm_close,
	.flush		= bcm_flush,
	.setup		= bcm_setup,
	.set_baudrate	= bcm_set_baudrate,
	.recv		= bcm_recv,
	.enqueue	= bcm_enqueue,
	.dequeue	= bcm_dequeue,
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id bcm_acpi_match[] = {
	{ "BCM2E00" },
	{ "BCM2E01" },
	{ "BCM2E02" },
	{ "BCM2E03" },
	{ "BCM2E04" },
	{ "BCM2E05" },
	{ "BCM2E06" },
	{ "BCM2E07" },
	{ "BCM2E08" },
	{ "BCM2E09" },
	{ "BCM2E0A" },
	{ "BCM2E0B" },
	{ "BCM2E0C" },
	{ "BCM2E0D" },
	{ "BCM2E0E" },
	{ "BCM2E0F" },
	{ "BCM2E10" },
	{ "BCM2E11" },
	{ "BCM2E12" },
	{ "BCM2E13" },
	{ "BCM2E14" },
	{ "BCM2E15" },
	{ "BCM2E16" },
	{ "BCM2E17" },
	{ "BCM2E18" },
	{ "BCM2E19" },
	{ "BCM2E1A" },
	{ "BCM2E1B" },
	{ "BCM2E1C" },
	{ "BCM2E1D" },
	{ "BCM2E1F" },
	{ "BCM2E20" },
	{ "BCM2E21" },
	{ "BCM2E22" },
	{ "BCM2E23" },
	{ "BCM2E24" },
	{ "BCM2E25" },
	{ "BCM2E26" },
	{ "BCM2E27" },
	{ "BCM2E28" },
	{ "BCM2E29" },
	{ "BCM2E2A" },
	{ "BCM2E2B" },
	{ "BCM2E2C" },
	{ "BCM2E2D" },
	{ "BCM2E2E" },
	{ "BCM2E2F" },
	{ "BCM2E30" },
	{ "BCM2E31" },
	{ "BCM2E32" },
	{ "BCM2E33" },
	{ "BCM2E34" },
	{ "BCM2E35" },
	{ "BCM2E36" },
	{ "BCM2E37" },
	{ "BCM2E38" },
	{ "BCM2E39" },
	{ "BCM2E3A" },
	{ "BCM2E3B" },
	{ "BCM2E3C" },
	{ "BCM2E3D" },
	{ "BCM2E3E" },
	{ "BCM2E3F" },
	{ "BCM2E40" },
	{ "BCM2E41" },
	{ "BCM2E42" },
	{ "BCM2E43" },
	{ "BCM2E44" },
	{ "BCM2E45" },
	{ "BCM2E46" },
	{ "BCM2E47" },
	{ "BCM2E48" },
	{ "BCM2E49" },
	{ "BCM2E4A" },
	{ "BCM2E4B" },
	{ "BCM2E4C" },
	{ "BCM2E4D" },
	{ "BCM2E4E" },
	{ "BCM2E4F" },
	{ "BCM2E50" },
	{ "BCM2E51" },
	{ "BCM2E52" },
	{ "BCM2E53" },
	{ "BCM2E54" },
	{ "BCM2E55" },
	{ "BCM2E56" },
	{ "BCM2E57" },
	{ "BCM2E58" },
	{ "BCM2E59" },
	{ "BCM2E5A" },
	{ "BCM2E5B" },
	{ "BCM2E5C" },
	{ "BCM2E5D" },
	{ "BCM2E5E" },
	{ "BCM2E5F" },
	{ "BCM2E60" },
	{ "BCM2E61" },
	{ "BCM2E62" },
	{ "BCM2E63" },
	{ "BCM2E64" },
	{ "BCM2E65" },
	{ "BCM2E66" },
	{ "BCM2E67" },
	{ "BCM2E68" },
	{ "BCM2E69" },
	{ "BCM2E6B" },
	{ "BCM2E6D" },
	{ "BCM2E6E" },
	{ "BCM2E6F" },
	{ "BCM2E70" },
	{ "BCM2E71" },
	{ "BCM2E72" },
	{ "BCM2E73" },
	{ "BCM2E74" },
	{ "BCM2E75" },
	{ "BCM2E76" },
	{ "BCM2E77" },
	{ "BCM2E78" },
	{ "BCM2E79" },
	{ "BCM2E7A" },
	{ "BCM2E7B" },
	{ "BCM2E7C" },
	{ "BCM2E7D" },
	{ "BCM2E7E" },
	{ "BCM2E7F" },
	{ "BCM2E80" },
	{ "BCM2E81" },
	{ "BCM2E82" },
	{ "BCM2E83" },
	{ "BCM2E84" },
	{ "BCM2E85" },
	{ "BCM2E86" },
	{ "BCM2E87" },
	{ "BCM2E88" },
	{ "BCM2E89" },
	{ "BCM2E8A" },
	{ "BCM2E8B" },
	{ "BCM2E8C" },
	{ "BCM2E8D" },
	{ "BCM2E8E" },
	{ "BCM2E90" },
	{ "BCM2E92" },
	{ "BCM2E93" },
	{ "BCM2E94" },
	{ "BCM2E95" },
	{ "BCM2E96" },
	{ "BCM2E97" },
	{ "BCM2E98" },
	{ "BCM2E99" },
	{ "BCM2E9A" },
	{ "BCM2E9B" },
	{ "BCM2E9C" },
	{ "BCM2E9D" },
	{ "BCM2EA0" },
	{ "BCM2EA1" },
	{ "BCM2EA2" },
	{ "BCM2EA3" },
	{ "BCM2EA4" },
	{ "BCM2EA5" },
	{ "BCM2EA6" },
	{ "BCM2EA7" },
	{ "BCM2EA8" },
	{ "BCM2EA9" },
	{ "BCM2EAA" },
	{ "BCM2EAB" },
	{ "BCM2EAC" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, bcm_acpi_match);
#endif

/* suspend and resume callbacks */
static const struct dev_pm_ops bcm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bcm_suspend, bcm_resume)
	SET_RUNTIME_PM_OPS(bcm_suspend_device, bcm_resume_device, NULL)
};

static struct platform_driver bcm_driver = {
	.probe = bcm_probe,
	.remove = bcm_remove,
	.driver = {
		.name = "hci_bcm",
		.acpi_match_table = ACPI_PTR(bcm_acpi_match),
		.pm = &bcm_pm_ops,
	},
};

static int bcm_serdev_probe(struct serdev_device *serdev)
{
	struct bcm_device *bcmdev;
	int err;

	bcmdev = devm_kzalloc(&serdev->dev, sizeof(*bcmdev), GFP_KERNEL);
	if (!bcmdev)
		return -ENOMEM;

	bcmdev->dev = &serdev->dev;
#ifdef CONFIG_PM
	bcmdev->hu = &bcmdev->serdev_hu;
#endif
	bcmdev->serdev_hu.serdev = serdev;
	serdev_device_set_drvdata(serdev, bcmdev);

	if (has_acpi_companion(&serdev->dev))
		err = bcm_acpi_probe(bcmdev);
	else
		err = bcm_of_probe(bcmdev);
	if (err)
		return err;

	err = bcm_get_resources(bcmdev);
	if (err)
		return err;

	if (!bcmdev->shutdown) {
		dev_warn(&serdev->dev,
			 "No reset resource, using default baud rate\n");
		bcmdev->oper_speed = bcmdev->init_speed;
	}

	err = bcm_gpio_set_power(bcmdev, false);
	if (err)
		dev_err(&serdev->dev, "Failed to power down\n");

	return hci_uart_register_device(&bcmdev->serdev_hu, &bcm_proto);
}

static void bcm_serdev_remove(struct serdev_device *serdev)
{
	struct bcm_device *bcmdev = serdev_device_get_drvdata(serdev);

	hci_uart_unregister_device(&bcmdev->serdev_hu);
}

#ifdef CONFIG_OF
static const struct of_device_id bcm_bluetooth_of_match[] = {
	{ .compatible = "brcm,bcm20702a1" },
	{ .compatible = "brcm,bcm4345c5" },
	{ .compatible = "brcm,bcm4330-bt" },
	{ .compatible = "brcm,bcm43438-bt" },
	{ .compatible = "brcm,bcm43540-bt" },
	{ .compatible = "brcm,bcm4335a0" },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm_bluetooth_of_match);
#endif

static struct serdev_device_driver bcm_serdev_driver = {
	.probe = bcm_serdev_probe,
	.remove = bcm_serdev_remove,
	.driver = {
		.name = "hci_uart_bcm",
		.of_match_table = of_match_ptr(bcm_bluetooth_of_match),
		.acpi_match_table = ACPI_PTR(bcm_acpi_match),
		.pm = &bcm_pm_ops,
	},
};

int __init bcm_init(void)
{
	/* For now, we need to keep both platform device
	 * driver (ACPI generated) and serdev driver (DT).
	 */
	platform_driver_register(&bcm_driver);
	serdev_device_driver_register(&bcm_serdev_driver);

	return hci_uart_register_proto(&bcm_proto);
}

int __exit bcm_deinit(void)
{
	platform_driver_unregister(&bcm_driver);
	serdev_device_driver_unregister(&bcm_serdev_driver);

	return hci_uart_unregister_proto(&bcm_proto);
}
