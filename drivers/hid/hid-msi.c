// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for MSI Claw Handheld PC gamepads.
 *
 *  Provides configuration support for the MSI Claw series of handheld PC
 *  gamepads. Multiple iterations of the device firmware has led to some
 *  quirks for how certain attributes are handled. The original firmware
 *  did not support remapping of the M1 (right) and M2 (left) rear paddles.
 *  Additionally, the MCU RAM address for writing configuration data has
 *  changed twice. Checks are done during probe to enumerate these variances.
 *
 *  Copyright (c) 2026 Zhouwang Huang <honjow311@gmail.com>
 *  Copyright (c) 2026 Denis Benato <denis.benato@linux.dev>
 *  Copyright (c) 2026 Valve Corporation
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/kobject.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

#define CLAW_OUTPUT_REPORT_ID	0x0f
#define CLAW_INPUT_REPORT_ID	0x10

#define CLAW_PACKET_SIZE	64

#define CLAW_DINPUT_CFG_INTF_IN	0x82
#define CLAW_XINPUT_CFG_INTF_IN	0x83

enum claw_command_index {
	CLAW_COMMAND_TYPE_NONE =			0x00,
	CLAW_COMMAND_TYPE_READ_PROFILE =		0x04,
	CLAW_COMMAND_TYPE_READ_PROFILE_ACK =		0x05,
	CLAW_COMMAND_TYPE_ACK =				0x06,
	CLAW_COMMAND_TYPE_WRITE_PROFILE_DATA =		0x21,
	CLAW_COMMAND_TYPE_SYNC_TO_ROM =			0x22,
	CLAW_COMMAND_TYPE_SWITCH_MODE =			0x24,
	CLAW_COMMAND_TYPE_READ_GAMEPAD_MODE =		0x26,
	CLAW_COMMAND_TYPE_GAMEPAD_MODE_ACK =		0x27,
	CLAW_COMMAND_TYPE_RESET_DEVICE =		0x28,
};

enum claw_gamepad_mode_index {
	CLAW_GAMEPAD_MODE_XINPUT =	0x01,
	CLAW_GAMEPAD_MODE_DINPUT =	0x02,
	CLAW_GAMEPAD_MODE_DESKTOP =	0x04,
};

static const char * const claw_gamepad_mode_text[] = {
	[CLAW_GAMEPAD_MODE_XINPUT] =	"xinput",
	[CLAW_GAMEPAD_MODE_DINPUT] =	"dinput",
	[CLAW_GAMEPAD_MODE_DESKTOP] =	"desktop",
};

enum claw_mkeys_function_index {
	CLAW_MKEY_FUNCTION_MACRO,
	CLAW_MKEY_FUNCTION_DISABLED,
	CLAW_MKEY_FUNCTION_COMBO,
};

enum claw_mode_field {
	CLAW_FIELD_GAMEPAD_MODE,
	CLAW_FIELD_MKEYS_FUNCTION,
};

static const char * const claw_mkeys_function_text[] = {
	[CLAW_MKEY_FUNCTION_MACRO] =	"macro",
	[CLAW_MKEY_FUNCTION_DISABLED] =	"disabled",
	[CLAW_MKEY_FUNCTION_COMBO] =	"combination",
};

struct claw_command_report {
	u8 report_id;
	u8 padding[2];
	u8 header_tail;
	u8 cmd;
	u8 data[59];
} __packed;

struct claw_drvdata {
	/* MCU General Variables */
	struct completion orphan_ack_complete;
	struct completion send_cmd_complete;
	struct delayed_work cfg_resume;
	struct delayed_work cfg_setup;
	spinlock_t registration_lock; /* Lock for registration read/write */
	struct hid_device *hdev;
	bool orphan_ack_pending;
	struct mutex cfg_mutex; /* mutex for synchronous data */
	spinlock_t cmd_lock; /* Lock for cmd data read/write */
	u8 waiting_cmd;
	int cmd_status;
	u8 ep;

	/* Gamepad Variables */
	enum claw_mkeys_function_index mkeys_function;
	enum claw_gamepad_mode_index gamepad_mode;
	spinlock_t mode_lock; /* Lock for mode data read/write */
	bool gp_registered;
};

static int get_endpoint_address(struct hid_device *hdev)
{
	struct usb_host_endpoint *ep;
	struct usb_interface *intf;

	intf = to_usb_interface(hdev->dev.parent);
	ep = intf->cur_altsetting->endpoint;
	if (ep)
		return ep->desc.bEndpointAddress;

	return -ENODEV;
}

static int claw_gamepad_mode_event(struct claw_drvdata *drvdata,
				   struct claw_command_report *cmd_rep)
{
	if (cmd_rep->data[0] >= ARRAY_SIZE(claw_gamepad_mode_text) ||
	    !claw_gamepad_mode_text[cmd_rep->data[0]] ||
	    cmd_rep->data[1] >= ARRAY_SIZE(claw_mkeys_function_text))
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &drvdata->mode_lock) {
		drvdata->gamepad_mode = cmd_rep->data[0];
		drvdata->mkeys_function = cmd_rep->data[1];
	}

	return 0;
}

static int claw_raw_event(struct claw_drvdata *drvdata, struct hid_report *report,
			  u8 *data, int size)
{
	struct claw_command_report *cmd_rep;
	int ret = 0;

	if (size != CLAW_PACKET_SIZE)
		return 0;

	cmd_rep = (struct claw_command_report *)data;

	if (cmd_rep->report_id != CLAW_INPUT_REPORT_ID || cmd_rep->header_tail != 0x3c)
		return 0;

	dev_dbg(&drvdata->hdev->dev, "Rx data as raw input report: [%*ph]\n",
		CLAW_PACKET_SIZE, data);

	guard(spinlock_irqsave)(&drvdata->cmd_lock);
	switch (cmd_rep->cmd) {
	case CLAW_COMMAND_TYPE_GAMEPAD_MODE_ACK:
		ret = claw_gamepad_mode_event(drvdata, cmd_rep);
		if (drvdata->waiting_cmd == CLAW_COMMAND_TYPE_READ_GAMEPAD_MODE) {
			drvdata->cmd_status = ret;
			complete(&drvdata->send_cmd_complete);
		}

		break;
	case CLAW_COMMAND_TYPE_ACK:
		if (drvdata->orphan_ack_pending) {
			drvdata->orphan_ack_pending = false;
			complete(&drvdata->orphan_ack_complete);
			break;
		}

		if (drvdata->waiting_cmd == CLAW_COMMAND_TYPE_NONE) {
			dev_warn(&drvdata->hdev->dev, "Got unexpected ACK from MCU, ignoring\n");
			break;
		}

		drvdata->cmd_status = 0;
		complete(&drvdata->send_cmd_complete);

		dev_dbg(&drvdata->hdev->dev, "Waiting CMD: %x\n", drvdata->waiting_cmd);

		break;
	default:
		dev_dbg(&drvdata->hdev->dev, "Unknown command: %x\n", cmd_rep->cmd);
		return 0;
	}

	return ret;
}

static int msi_raw_event(struct hid_device *hdev, struct hid_report *report,
			 u8 *data, int size)
{
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);

	if (!drvdata || (drvdata->ep != CLAW_XINPUT_CFG_INTF_IN &&
			 drvdata->ep != CLAW_DINPUT_CFG_INTF_IN))
		return 0;

	return claw_raw_event(drvdata, report, data, size);
}

/* Caller must hold drvdata->cfg_mutex. */
static int __claw_hw_output_report(struct hid_device *hdev, u8 index, u8 *data,
				   size_t len, unsigned int timeout)
{
	unsigned char *dmabuf __free(kfree) = NULL;
	u8 header[] = { CLAW_OUTPUT_REPORT_ID, 0, 0, 0x3c, index };
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	size_t header_size = ARRAY_SIZE(header);
	bool orphaned;
	int ret;

	lockdep_assert_held(&drvdata->cfg_mutex);

	/* If expecting an orphan ack, hold next event until MCU has time to clear it */
	scoped_guard(spinlock_irqsave, &drvdata->cmd_lock)
		orphaned = drvdata->orphan_ack_pending;

	if (orphaned) {
		wait_for_completion_timeout(&drvdata->orphan_ack_complete, msecs_to_jiffies(25));
		scoped_guard(spinlock_irqsave, &drvdata->cmd_lock)
			drvdata->orphan_ack_pending = false;
	}

	if (header_size + len > CLAW_PACKET_SIZE)
		return -EINVAL;

	/* We can't use a devm_alloc reusable buffer without side effects during suspend */
	dmabuf = kzalloc(CLAW_PACKET_SIZE, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	memcpy(dmabuf, header, header_size);
	if (data && len)
		memcpy(dmabuf + header_size, data, len);

	reinit_completion(&drvdata->send_cmd_complete);

	scoped_guard(spinlock_irqsave, &drvdata->cmd_lock) {
		if (timeout) {
			drvdata->waiting_cmd = index;
			drvdata->cmd_status = -ETIMEDOUT;
		} else {
			reinit_completion(&drvdata->orphan_ack_complete);
			drvdata->waiting_cmd = CLAW_COMMAND_TYPE_NONE;
			drvdata->orphan_ack_pending = true;
		}
	}

	dev_dbg(&hdev->dev, "Send data as raw output report: [%*ph]\n",
		CLAW_PACKET_SIZE, dmabuf);

	ret = hid_hw_output_report(hdev, dmabuf, CLAW_PACKET_SIZE);
	if (ret < 0)
		goto err;

	ret = ret == CLAW_PACKET_SIZE ? 0 : -EIO;
	if (ret)
		goto err;

	if (timeout) {
		ret = wait_for_completion_interruptible_timeout(&drvdata->send_cmd_complete,
								msecs_to_jiffies(timeout));

		dev_dbg(&hdev->dev, "Remaining timeout: %u\n", ret);
		ret = ret > 0 ? drvdata->cmd_status : ret ?: -EBUSY;
		if (ret)
			goto err;
	}

	scoped_guard(spinlock_irqsave, &drvdata->cmd_lock)
		drvdata->waiting_cmd = CLAW_COMMAND_TYPE_NONE;

	return ret;

err:
	scoped_guard(spinlock_irqsave, &drvdata->cmd_lock) {
		drvdata->waiting_cmd = CLAW_COMMAND_TYPE_NONE;
		drvdata->orphan_ack_pending = false;
	}
	return ret;
}

static int claw_hw_output_report(struct hid_device *hdev, u8 index, u8 *data,
				 size_t len, unsigned int timeout)
{
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);

	guard(mutex)(&drvdata->cfg_mutex);
	return __claw_hw_output_report(hdev, index, data, len, timeout);
}

static int claw_switch_mode(struct hid_device *hdev, enum claw_mode_field field, u8 val)
{
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	u8 data[2];

	guard(mutex)(&drvdata->cfg_mutex);

	scoped_guard(spinlock_irqsave, &drvdata->mode_lock) {
		switch (field) {
		case CLAW_FIELD_GAMEPAD_MODE:
			data[0] = val;
			data[1] = drvdata->mkeys_function;
			break;
		case CLAW_FIELD_MKEYS_FUNCTION:
			data[0] = drvdata->gamepad_mode;
			data[1] = val;
			break;
		}
	}

	return __claw_hw_output_report(hdev, CLAW_COMMAND_TYPE_SWITCH_MODE, data,
				       ARRAY_SIZE(data), 0);
}

static ssize_t gamepad_mode_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	int i, ret = -EINVAL;

	scoped_guard(spinlock_irqsave, &drvdata->registration_lock) {
		/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
		if (!smp_load_acquire(&drvdata->gp_registered))
			return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(claw_gamepad_mode_text); i++) {
		if (claw_gamepad_mode_text[i] && sysfs_streq(buf, claw_gamepad_mode_text[i])) {
			ret = i;
			break;
		}
	}
	if (ret < 0)
		return ret;

	ret = claw_switch_mode(hdev, CLAW_FIELD_GAMEPAD_MODE, ret);
	if (ret)
		return ret;

	return count;
}

static ssize_t gamepad_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	int ret, i;

	scoped_guard(spinlock_irqsave, &drvdata->registration_lock) {
		/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
		if (!smp_load_acquire(&drvdata->gp_registered))
			return -ENODEV;
	}

	ret = claw_hw_output_report(hdev, CLAW_COMMAND_TYPE_READ_GAMEPAD_MODE, NULL, 0, 25);
	if (ret)
		return ret;

	scoped_guard(spinlock_irqsave, &drvdata->mode_lock)
		i = drvdata->gamepad_mode;

	if (!claw_gamepad_mode_text[i] || claw_gamepad_mode_text[i][0] == '\0')
		return sysfs_emit(buf, "unsupported\n");

	return sysfs_emit(buf, "%s\n", claw_gamepad_mode_text[i]);
}
static DEVICE_ATTR_RW(gamepad_mode);

static ssize_t gamepad_mode_index_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(claw_gamepad_mode_text); i++) {
		if (!claw_gamepad_mode_text[i] || claw_gamepad_mode_text[i][0] == '\0')
			continue;
		count += sysfs_emit_at(buf, count, "%s ", claw_gamepad_mode_text[i]);
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}
static DEVICE_ATTR_RO(gamepad_mode_index);

static ssize_t mkeys_function_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	int i, ret = -EINVAL;

	scoped_guard(spinlock_irqsave, &drvdata->registration_lock) {
		/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
		if (!smp_load_acquire(&drvdata->gp_registered))
			return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(claw_mkeys_function_text); i++) {
		if (claw_mkeys_function_text[i] && sysfs_streq(buf, claw_mkeys_function_text[i])) {
			ret = i;
			break;
		}
	}
	if (ret < 0)
		return ret;

	ret = claw_switch_mode(hdev, CLAW_FIELD_MKEYS_FUNCTION, ret);
	if (ret)
		return ret;

	return count;
}

static ssize_t mkeys_function_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	int ret, i;

	scoped_guard(spinlock_irqsave, &drvdata->registration_lock) {
		/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
		if (!smp_load_acquire(&drvdata->gp_registered))
			return -ENODEV;
	}

	ret = claw_hw_output_report(hdev, CLAW_COMMAND_TYPE_READ_GAMEPAD_MODE, NULL, 0, 25);
	if (ret)
		return ret;

	scoped_guard(spinlock_irqsave, &drvdata->mode_lock)
		i = drvdata->mkeys_function;

	if (i >= ARRAY_SIZE(claw_mkeys_function_text))
		return sysfs_emit(buf, "unsupported\n");

	return sysfs_emit(buf, "%s\n", claw_mkeys_function_text[i]);
}
static DEVICE_ATTR_RW(mkeys_function);

static ssize_t mkeys_function_index_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	int i, count = 0;

	for (i = 0; i < ARRAY_SIZE(claw_mkeys_function_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", claw_mkeys_function_text[i]);

	if (count)
		buf[count - 1] = '\n';

	return count;
}
static DEVICE_ATTR_RO(mkeys_function_index);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	bool val;
	int ret;

	scoped_guard(spinlock_irqsave, &drvdata->registration_lock) {
		/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
		if (!smp_load_acquire(&drvdata->gp_registered))
			return -ENODEV;
	}

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	if (!val)
		return -EINVAL;

	ret = claw_hw_output_report(hdev, CLAW_COMMAND_TYPE_RESET_DEVICE, NULL, 0, 0);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(reset);

static umode_t claw_gamepad_attr_is_visible(struct kobject *kobj, struct attribute *attr,
					    int n)
{
	struct hid_device *hdev = to_hid_device(kobj_to_dev(kobj));
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);

	if (!drvdata) {
		dev_warn(&hdev->dev,
			 "Failed to get drvdata from kobj. Gamepad attributes are not available.\n");
		return 0;
	}

	return attr->mode;
}

static struct attribute *claw_gamepad_attrs[] = {
	&dev_attr_gamepad_mode.attr,
	&dev_attr_gamepad_mode_index.attr,
	&dev_attr_mkeys_function.attr,
	&dev_attr_mkeys_function_index.attr,
	&dev_attr_reset.attr,
	NULL,
};

static const struct attribute_group claw_gamepad_attr_group = {
	.attrs = claw_gamepad_attrs,
	.is_visible = claw_gamepad_attr_is_visible,
};

static void cfg_setup_fn(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct claw_drvdata *drvdata = container_of(dwork, struct claw_drvdata, cfg_setup);
	int ret;

	ret = claw_hw_output_report(drvdata->hdev, CLAW_COMMAND_TYPE_READ_GAMEPAD_MODE,
				    NULL, 0, 25);
	if (ret) {
		dev_err(&drvdata->hdev->dev,
			"Failed to setup device, can't read gamepad mode: %d\n", ret);
		return;
	}

	/* Add sysfs attributes after we get the device state */
	ret = device_add_group(&drvdata->hdev->dev, &claw_gamepad_attr_group);
	if (ret) {
		dev_err(&drvdata->hdev->dev,
			"Failed to setup device, can't create gamepad attrs: %d\n", ret);
		return;
	}
	scoped_guard(spinlock_irqsave, &drvdata->registration_lock)
		/* Pairs with smp_load_acquire in attribute show/store functions */
		smp_store_release(&drvdata->gp_registered, true);

	kobject_uevent(&drvdata->hdev->dev.kobj, KOBJ_CHANGE);
}

static void cfg_resume_fn(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct claw_drvdata *drvdata = container_of(dwork, struct claw_drvdata, cfg_resume);

	guard(spinlock_irqsave)(&drvdata->registration_lock);
	/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
	if (!smp_load_acquire(&drvdata->gp_registered))
		schedule_delayed_work(&drvdata->cfg_setup, msecs_to_jiffies(500));
}

static int claw_probe(struct hid_device *hdev, u8 ep)
{
	struct claw_drvdata *drvdata;
	int ret;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->gamepad_mode = CLAW_GAMEPAD_MODE_XINPUT;
	drvdata->hdev = hdev;
	drvdata->ep = ep;

	mutex_init(&drvdata->cfg_mutex);
	spin_lock_init(&drvdata->registration_lock);
	spin_lock_init(&drvdata->cmd_lock);
	spin_lock_init(&drvdata->mode_lock);
	init_completion(&drvdata->orphan_ack_complete);
	init_completion(&drvdata->send_cmd_complete);
	INIT_DELAYED_WORK(&drvdata->cfg_resume, &cfg_resume_fn);
	INIT_DELAYED_WORK(&drvdata->cfg_setup, &cfg_setup_fn);

	/* For control interface: open the HID transport for sending commands. */
	ret = hid_hw_open(hdev);
	if (ret)
		return ret;

	hid_set_drvdata(hdev, drvdata);
	schedule_delayed_work(&drvdata->cfg_setup, msecs_to_jiffies(500));

	return 0;
}

static int msi_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	u8 ep;

	if (!hid_is_usb(hdev)) {
		ret = -ENODEV;
		goto err_probe;
	}

	ret = hid_parse(hdev);
	if (ret)
		goto err_probe;

	/* Set quirk to create separate input devices per HID application */
	hdev->quirks |= HID_QUIRK_INPUT_PER_APP | HID_QUIRK_MULTI_INPUT;
	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		goto err_probe;

	/* For non-control interfaces (keyboard/mouse), allow userspace to grab the devices. */
	ret = get_endpoint_address(hdev);
	if (ret < 0)
		goto err_stop_hw;

	ep = ret;
	if (ep == CLAW_XINPUT_CFG_INTF_IN || ep == CLAW_DINPUT_CFG_INTF_IN) {
		ret = claw_probe(hdev, ep);
		if (ret)
			goto err_stop_hw;
	}

	return 0;

err_stop_hw:
	hid_hw_stop(hdev);
err_probe:
	return dev_err_probe(&hdev->dev, ret, "Failed to init device\n");
}

static void claw_remove(struct hid_device *hdev)
{
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);
	bool gp_registered;

	if (!drvdata)
		return;

	cancel_delayed_work_sync(&drvdata->cfg_resume);
	cancel_delayed_work_sync(&drvdata->cfg_setup);

	scoped_guard(spinlock_irqsave, &drvdata->registration_lock) {
		/* Pairs with smp_store_release from cfg_setup_fn in system_wq context */
		gp_registered = smp_load_acquire(&drvdata->gp_registered);
		/* Pairs with smp_load_acquire in attribute show/store functions */
		smp_store_release(&drvdata->gp_registered, false);
	}

	if (gp_registered)
		device_remove_group(&hdev->dev, &claw_gamepad_attr_group);

	hid_hw_close(hdev);
}

static void msi_remove(struct hid_device *hdev)
{
	int ret;
	u8 ep;

	/* Safe assumption. SET_INTERFACE ioctl can't be used while driver is bound */
	ret = get_endpoint_address(hdev);
	if (ret <= 0)
		goto hw_stop;

	ep = ret;
	if (ep == CLAW_XINPUT_CFG_INTF_IN || ep == CLAW_DINPUT_CFG_INTF_IN)
		claw_remove(hdev);

hw_stop:
	hid_hw_stop(hdev);
}

static int claw_resume(struct hid_device *hdev)
{
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);

	if (!drvdata)
		return -ENODEV;

	/* MCU can take up to 500ms to be ready after resume */
	schedule_delayed_work(&drvdata->cfg_resume, msecs_to_jiffies(500));
	return 0;
}

static int msi_resume(struct hid_device *hdev)
{
	int ret;
	u8 ep;

	/* Safe assumption. SET_INTERFACE ioctl can't be used while driver is bound */
	ret = get_endpoint_address(hdev);
	if (ret <= 0)
		return 0;

	ep = ret;
	if (ep == CLAW_XINPUT_CFG_INTF_IN || ep == CLAW_DINPUT_CFG_INTF_IN)
		return claw_resume(hdev);

	return 0;
}

static int claw_suspend(struct hid_device *hdev)
{
	struct claw_drvdata *drvdata = hid_get_drvdata(hdev);

	if (!drvdata)
		return -ENODEV;

	cancel_delayed_work_sync(&drvdata->cfg_resume);
	cancel_delayed_work_sync(&drvdata->cfg_setup);

	return 0;
}

static int msi_suspend(struct hid_device *hdev, pm_message_t msg)
{
	int ret;
	u8 ep;

	/* Safe assumption. SET_INTERFACE ioctl can't be used while driver is bound */
	ret = get_endpoint_address(hdev);
	if (ret <= 0)
		return 0;

	ep = ret;
	if (ep == CLAW_XINPUT_CFG_INTF_IN || ep == CLAW_DINPUT_CFG_INTF_IN)
		return claw_suspend(hdev);

	return 0;
}

static const struct hid_device_id msi_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MSI_2, USB_DEVICE_ID_MSI_CLAW_XINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MSI_2, USB_DEVICE_ID_MSI_CLAW_DINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MSI_2, USB_DEVICE_ID_MSI_CLAW_DESKTOP) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MSI_2, USB_DEVICE_ID_MSI_CLAW_BIOS) },
	{ }
};
MODULE_DEVICE_TABLE(hid, msi_devices);

static struct hid_driver msi_driver = {
	.name		= "hid-msi",
	.id_table	= msi_devices,
	.raw_event	= msi_raw_event,
	.probe		= msi_probe,
	.remove		= msi_remove,
	.resume		= pm_ptr(msi_resume),
	.suspend	= pm_ptr(msi_suspend),
};
module_hid_driver(msi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Denis Benato <denis.benato@linux.dev>");
MODULE_AUTHOR("Zhouwang Huang <honjow311@gmail.com>");
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("HID driver for MSI Claw Handheld PC gamepads");
