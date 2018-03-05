/*
 *
 *  Bluetooth HCI UART driver for Broadcom devices
 *
 *  Copyright (C) 2015  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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

#define BCM_AUTOSUSPEND_DELAY	5000 /* default autosleep delay */

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
 * @clk: clock used by Bluetooth device
 * @clk_enabled: whether @clk is prepared and enabled
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
#endif

	struct clk		*clk;
	bool			clk_enabled;

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

	if (powered && !IS_ERR(dev->clk) && !dev->clk_enabled) {
		err = clk_prepare_enable(dev->clk);
		if (err)
			return err;
	}

	err = dev->set_shutdown(dev, powered);
	if (err)
		goto err_clk_disable;

	err = dev->set_device_wakeup(dev, powered);
	if (err)
		goto err_revert_shutdown;

	if (!powered && !IS_ERR(dev->clk) && dev->clk_enabled)
		clk_disable_unprepare(dev->clk);

	dev->clk_enabled = powered;

	return 0;

err_revert_shutdown:
	dev->set_shutdown(dev, !powered);
err_clk_disable:
	if (powered && !IS_ERR(dev->clk) && !dev->clk_enabled)
		clk_disable_unprepare(dev->clk);
	return err;
}

#ifdef CONFIG_PM
static irqreturn_t bcm_host_wake(int irq, void *data)
{
	struct bcm_device *bdev = data;

	bt_dev_dbg(bdev, "Host wake IRQ");

	pm_request_resume(bdev->dev);

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
	.pulsed_host_wake = 0,
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

	bcm = kzalloc(sizeof(*bcm), GFP_KERNEL);
	if (!bcm)
		return -ENOMEM;

	skb_queue_head_init(&bcm->txq);

	hu->priv = bcm;

	mutex_lock(&bcm_device_lock);

	if (hu->serdev) {
		err = serdev_device_open(hu->serdev);
		if (err)
			goto err_free;

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
		hu->init_speed = bcm->dev->init_speed;
		hu->oper_speed = bcm->dev->oper_speed;
		err = bcm_gpio_set_power(bcm->dev, true);
		if (err)
			goto err_unset_hu;
	}

	mutex_unlock(&bcm_device_lock);
	return 0;

err_unset_hu:
	if (hu->serdev)
		serdev_device_close(hu->serdev);
#ifdef CONFIG_PM
	else
		bcm->dev->hu = NULL;
#endif
err_free:
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
		serdev_device_close(hu->serdev);
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

	err = btbcm_initialize(hu->hdev, fw_name, sizeof(fw_name));
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

static const struct h4_recv_pkt bcm_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = hci_recv_frame },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = hci_recv_frame },
	{ BCM_RECV_LM_DIAG, .recv = hci_recv_diag  },
	{ BCM_RECV_NULL,    .recv = hci_recv_diag  },
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
		if (bcm->dev && bcm_device_exists(bcm->dev))
			pm_request_resume(bcm->dev->dev);
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

static const struct acpi_gpio_params int_last_device_wakeup_gpios = { 0, 0, false };
static const struct acpi_gpio_params int_last_shutdown_gpios = { 1, 0, false };
static const struct acpi_gpio_params int_last_host_wakeup_gpios = { 2, 0, false };

static const struct acpi_gpio_mapping acpi_bcm_int_last_gpios[] = {
	{ "device-wakeup-gpios", &int_last_device_wakeup_gpios, 1 },
	{ "shutdown-gpios", &int_last_shutdown_gpios, 1 },
	{ "host-wakeup-gpios", &int_last_host_wakeup_gpios, 1 },
	{ },
};

static const struct acpi_gpio_params int_first_host_wakeup_gpios = { 0, 0, false };
static const struct acpi_gpio_params int_first_device_wakeup_gpios = { 1, 0, false };
static const struct acpi_gpio_params int_first_shutdown_gpios = { 2, 0, false };

static const struct acpi_gpio_mapping acpi_bcm_int_first_gpios[] = {
	{ "device-wakeup-gpios", &int_first_device_wakeup_gpios, 1 },
	{ "shutdown-gpios", &int_first_shutdown_gpios, 1 },
	{ "host-wakeup-gpios", &int_first_host_wakeup_gpios, 1 },
	{ },
};

#ifdef CONFIG_ACPI
/* IRQ polarity of some chipsets are not defined correctly in ACPI table. */
static const struct dmi_system_id bcm_active_low_irq_dmi_table[] = {
	{
		.ident = "Asus T100TA",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR,
					"ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100TA"),
		},
	},
	{
		.ident = "Asus T100CHI",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR,
					"ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100CHI"),
		},
	},
	{	/* Handle ThinkPad 8 tablets with BCM2E55 chipset ACPI ID */
		.ident = "Lenovo ThinkPad 8",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "ThinkPad 8"),
		},
	},
	{
		.ident = "MINIX Z83-4",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "MINIX"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Z83-4"),
		},
	},
	{ }
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
		dev->irq_active_low = irq->polarity == ACPI_ACTIVE_LOW;
		break;

	case ACPI_RESOURCE_TYPE_GPIO:
		gpio = &ares->data.gpio;
		if (gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT)
			dev->irq_active_low = gpio->polarity == ACPI_ACTIVE_LOW;
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
	gpiod_set_value(dev->device_wakeup, awake);
	return 0;
}

static int bcm_gpio_set_shutdown(struct bcm_device *dev, bool powered)
{
	gpiod_set_value(dev->shutdown, powered);
	return 0;
}

static int bcm_get_resources(struct bcm_device *dev)
{
	dev->name = dev_name(dev->dev);

	if (x86_apple_machine && !bcm_apple_get_resources(dev))
		return 0;

	dev->clk = devm_clk_get(dev->dev, NULL);

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

	/* IRQ can be declared in ACPI table as Interrupt or GpioInt */
	if (dev->irq <= 0) {
		struct gpio_desc *gpio;

		gpio = devm_gpiod_get_optional(dev->dev, "host-wakeup",
					       GPIOD_IN);
		if (IS_ERR(gpio))
			return PTR_ERR(gpio);

		dev->irq = gpiod_to_irq(gpio);
	}

	dev_dbg(dev->dev, "BCM irq: %d\n", dev->irq);
	return 0;
}

#ifdef CONFIG_ACPI
static int bcm_acpi_probe(struct bcm_device *dev)
{
	LIST_HEAD(resources);
	const struct dmi_system_id *dmi_id;
	const struct acpi_gpio_mapping *gpio_mapping = acpi_bcm_int_last_gpios;
	const struct acpi_device_id *id;
	struct resource_entry *entry;
	int ret;

	/* Retrieve GPIO data */
	id = acpi_match_device(dev->dev->driver->acpi_match_table, dev->dev);
	if (id)
		gpio_mapping = (const struct acpi_gpio_mapping *) id->driver_data;

	ret = devm_acpi_dev_add_driver_gpios(dev->dev, gpio_mapping);
	if (ret)
		return ret;

	/* Retrieve UART ACPI info */
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

	dmi_id = dmi_first_match(bcm_active_low_irq_dmi_table);
	if (dmi_id) {
		dev_warn(dev->dev, "%s: Overwriting IRQ polarity to active low",
			    dmi_id->ident);
		dev->irq_active_low = true;
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
	{ "BCM2E1A", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E39", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E3A", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E3D", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E3F", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E40", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E54", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E55", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E64", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E65", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E67", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E71", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E72", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E7B", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E7C", (kernel_ulong_t)&acpi_bcm_int_last_gpios },
	{ "BCM2E7E", (kernel_ulong_t)&acpi_bcm_int_first_gpios },
	{ "BCM2E95", (kernel_ulong_t)&acpi_bcm_int_first_gpios },
	{ "BCM2E96", (kernel_ulong_t)&acpi_bcm_int_first_gpios },
	{ "BCM2EA4", (kernel_ulong_t)&acpi_bcm_int_first_gpios },
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
	{ .compatible = "brcm,bcm43438-bt" },
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
