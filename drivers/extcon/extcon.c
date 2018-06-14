/*
 * drivers/extcon/extcon.c - External Connector (extcon) framework.
 *
 * Copyright (C) 2015 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * Copyright (C) 2012 Samsung Electronics
 * Author: Donggeun Kim <dg77.kim@samsung.com>
 * Author: MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * based on android/drivers/switch/switch_class.c
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "extcon.h"

#define SUPPORTED_CABLE_MAX	32

struct __extcon_info {
	unsigned int type;
	unsigned int id;
	const char *name;

} extcon_info[] = {
	[EXTCON_NONE] = {
		.type = EXTCON_TYPE_MISC,
		.id = EXTCON_NONE,
		.name = "NONE",
	},

	/* USB external connector */
	[EXTCON_USB] = {
		.type = EXTCON_TYPE_USB,
		.id = EXTCON_USB,
		.name = "USB",
	},
	[EXTCON_USB_HOST] = {
		.type = EXTCON_TYPE_USB,
		.id = EXTCON_USB_HOST,
		.name = "USB-HOST",
	},

	/* Charging external connector */
	[EXTCON_CHG_USB_SDP] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_SDP,
		.name = "SDP",
	},
	[EXTCON_CHG_USB_DCP] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_DCP,
		.name = "DCP",
	},
	[EXTCON_CHG_USB_CDP] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_CDP,
		.name = "CDP",
	},
	[EXTCON_CHG_USB_ACA] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_ACA,
		.name = "ACA",
	},
	[EXTCON_CHG_USB_FAST] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_FAST,
		.name = "FAST-CHARGER",
	},
	[EXTCON_CHG_USB_SLOW] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_SLOW,
		.name = "SLOW-CHARGER",
	},
	[EXTCON_CHG_WPT] = {
		.type = EXTCON_TYPE_CHG,
		.id = EXTCON_CHG_WPT,
		.name = "WPT",
	},
	[EXTCON_CHG_USB_PD] = {
		.type = EXTCON_TYPE_CHG | EXTCON_TYPE_USB,
		.id = EXTCON_CHG_USB_PD,
		.name = "PD",
	},

	/* Jack external connector */
	[EXTCON_JACK_MICROPHONE] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_MICROPHONE,
		.name = "MICROPHONE",
	},
	[EXTCON_JACK_HEADPHONE] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_HEADPHONE,
		.name = "HEADPHONE",
	},
	[EXTCON_JACK_LINE_IN] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_LINE_IN,
		.name = "LINE-IN",
	},
	[EXTCON_JACK_LINE_OUT] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_LINE_OUT,
		.name = "LINE-OUT",
	},
	[EXTCON_JACK_VIDEO_IN] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_VIDEO_IN,
		.name = "VIDEO-IN",
	},
	[EXTCON_JACK_VIDEO_OUT] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_VIDEO_OUT,
		.name = "VIDEO-OUT",
	},
	[EXTCON_JACK_SPDIF_IN] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_SPDIF_IN,
		.name = "SPDIF-IN",
	},
	[EXTCON_JACK_SPDIF_OUT] = {
		.type = EXTCON_TYPE_JACK,
		.id = EXTCON_JACK_SPDIF_OUT,
		.name = "SPDIF-OUT",
	},

	/* Display external connector */
	[EXTCON_DISP_HDMI] = {
		.type = EXTCON_TYPE_DISP,
		.id = EXTCON_DISP_HDMI,
		.name = "HDMI",
	},
	[EXTCON_DISP_MHL] = {
		.type = EXTCON_TYPE_DISP,
		.id = EXTCON_DISP_MHL,
		.name = "MHL",
	},
	[EXTCON_DISP_DVI] = {
		.type = EXTCON_TYPE_DISP,
		.id = EXTCON_DISP_DVI,
		.name = "DVI",
	},
	[EXTCON_DISP_VGA] = {
		.type = EXTCON_TYPE_DISP,
		.id = EXTCON_DISP_VGA,
		.name = "VGA",
	},
	[EXTCON_DISP_DP] = {
		.type = EXTCON_TYPE_DISP | EXTCON_TYPE_USB,
		.id = EXTCON_DISP_DP,
		.name = "DP",
	},
	[EXTCON_DISP_HMD] = {
		.type = EXTCON_TYPE_DISP | EXTCON_TYPE_USB,
		.id = EXTCON_DISP_HMD,
		.name = "HMD",
	},

	/* Miscellaneous external connector */
	[EXTCON_DOCK] = {
		.type = EXTCON_TYPE_MISC,
		.id = EXTCON_DOCK,
		.name = "DOCK",
	},
	[EXTCON_JIG] = {
		.type = EXTCON_TYPE_MISC,
		.id = EXTCON_JIG,
		.name = "JIG",
	},
	[EXTCON_MECHANICAL] = {
		.type = EXTCON_TYPE_MISC,
		.id = EXTCON_MECHANICAL,
		.name = "MECHANICAL",
	},

	{ /* sentinel */ }
};

/**
 * struct extcon_cable - An internal data for an external connector.
 * @edev:		the extcon device
 * @cable_index:	the index of this cable in the edev
 * @attr_g:		the attribute group for the cable
 * @attr_name:		"name" sysfs entry
 * @attr_state:		"state" sysfs entry
 * @attrs:		the array pointing to attr_name and attr_state for attr_g
 */
struct extcon_cable {
	struct extcon_dev *edev;
	int cable_index;

	struct attribute_group attr_g;
	struct device_attribute attr_name;
	struct device_attribute attr_state;

	struct attribute *attrs[3]; /* to be fed to attr_g.attrs */

	union extcon_property_value usb_propval[EXTCON_PROP_USB_CNT];
	union extcon_property_value chg_propval[EXTCON_PROP_CHG_CNT];
	union extcon_property_value jack_propval[EXTCON_PROP_JACK_CNT];
	union extcon_property_value disp_propval[EXTCON_PROP_DISP_CNT];

	unsigned long usb_bits[BITS_TO_LONGS(EXTCON_PROP_USB_CNT)];
	unsigned long chg_bits[BITS_TO_LONGS(EXTCON_PROP_CHG_CNT)];
	unsigned long jack_bits[BITS_TO_LONGS(EXTCON_PROP_JACK_CNT)];
	unsigned long disp_bits[BITS_TO_LONGS(EXTCON_PROP_DISP_CNT)];
};

static struct class *extcon_class;

static LIST_HEAD(extcon_dev_list);
static DEFINE_MUTEX(extcon_dev_list_lock);

static int check_mutually_exclusive(struct extcon_dev *edev, u32 new_state)
{
	int i = 0;

	if (!edev->mutually_exclusive)
		return 0;

	for (i = 0; edev->mutually_exclusive[i]; i++) {
		int weight;
		u32 correspondants = new_state & edev->mutually_exclusive[i];

		/* calculate the total number of bits set */
		weight = hweight32(correspondants);
		if (weight > 1)
			return i + 1;
	}

	return 0;
}

static int find_cable_index_by_id(struct extcon_dev *edev, const unsigned int id)
{
	int i;

	/* Find the the index of extcon cable in edev->supported_cable */
	for (i = 0; i < edev->max_supported; i++) {
		if (edev->supported_cable[i] == id)
			return i;
	}

	return -EINVAL;
}

static int get_extcon_type(unsigned int prop)
{
	switch (prop) {
	case EXTCON_PROP_USB_MIN ... EXTCON_PROP_USB_MAX:
		return EXTCON_TYPE_USB;
	case EXTCON_PROP_CHG_MIN ... EXTCON_PROP_CHG_MAX:
		return EXTCON_TYPE_CHG;
	case EXTCON_PROP_JACK_MIN ... EXTCON_PROP_JACK_MAX:
		return EXTCON_TYPE_JACK;
	case EXTCON_PROP_DISP_MIN ... EXTCON_PROP_DISP_MAX:
		return EXTCON_TYPE_DISP;
	default:
		return -EINVAL;
	}
}

static bool is_extcon_attached(struct extcon_dev *edev, unsigned int index)
{
	return !!(edev->state & BIT(index));
}

static bool is_extcon_changed(struct extcon_dev *edev, int index,
				bool new_state)
{
	int state = !!(edev->state & BIT(index));
	return (state != new_state);
}

static bool is_extcon_property_supported(unsigned int id, unsigned int prop)
{
	int type;

	/* Check whether the property is supported or not. */
	type = get_extcon_type(prop);
	if (type < 0)
		return false;

	/* Check whether a specific extcon id supports the property or not. */
	return !!(extcon_info[id].type & type);
}

static int is_extcon_property_capability(struct extcon_dev *edev,
				unsigned int id, int index,unsigned int prop)
{
	struct extcon_cable *cable;
	int type, ret;

	/* Check whether the property is supported or not. */
	type = get_extcon_type(prop);
	if (type < 0)
		return type;

	cable = &edev->cables[index];

	switch (type) {
	case EXTCON_TYPE_USB:
		ret = test_bit(prop - EXTCON_PROP_USB_MIN, cable->usb_bits);
		break;
	case EXTCON_TYPE_CHG:
		ret = test_bit(prop - EXTCON_PROP_CHG_MIN, cable->chg_bits);
		break;
	case EXTCON_TYPE_JACK:
		ret = test_bit(prop - EXTCON_PROP_JACK_MIN, cable->jack_bits);
		break;
	case EXTCON_TYPE_DISP:
		ret = test_bit(prop - EXTCON_PROP_DISP_MIN, cable->disp_bits);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void init_property(struct extcon_dev *edev, unsigned int id, int index)
{
	unsigned int type = extcon_info[id].type;
	struct extcon_cable *cable = &edev->cables[index];

	if (EXTCON_TYPE_USB & type)
		memset(cable->usb_propval, 0, sizeof(cable->usb_propval));
	if (EXTCON_TYPE_CHG & type)
		memset(cable->chg_propval, 0, sizeof(cable->chg_propval));
	if (EXTCON_TYPE_JACK & type)
		memset(cable->jack_propval, 0, sizeof(cable->jack_propval));
	if (EXTCON_TYPE_DISP & type)
		memset(cable->disp_propval, 0, sizeof(cable->disp_propval));
}

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int i, count = 0;
	struct extcon_dev *edev = dev_get_drvdata(dev);

	if (edev->max_supported == 0)
		return sprintf(buf, "%u\n", edev->state);

	for (i = 0; i < edev->max_supported; i++) {
		count += sprintf(buf + count, "%s=%d\n",
				extcon_info[edev->supported_cable[i]].name,
				 !!(edev->state & BIT(i)));
	}

	return count;
}
static DEVICE_ATTR_RO(state);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct extcon_dev *edev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", edev->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t cable_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct extcon_cable *cable = container_of(attr, struct extcon_cable,
						  attr_name);
	int i = cable->cable_index;

	return sprintf(buf, "%s\n",
			extcon_info[cable->edev->supported_cable[i]].name);
}

static ssize_t cable_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct extcon_cable *cable = container_of(attr, struct extcon_cable,
						  attr_state);

	int i = cable->cable_index;

	return sprintf(buf, "%d\n",
		extcon_get_state(cable->edev, cable->edev->supported_cable[i]));
}

/**
 * extcon_sync() - Synchronize the state for an external connector.
 * @edev:	the extcon device
 *
 * Note that this function send a notification in order to synchronize
 * the state and property of an external connector.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_sync(struct extcon_dev *edev, unsigned int id)
{
	char name_buf[120];
	char state_buf[120];
	char *prop_buf;
	char *envp[3];
	int env_offset = 0;
	int length;
	int index;
	int state;
	unsigned long flags;

	if (!edev)
		return -EINVAL;

	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	spin_lock_irqsave(&edev->lock, flags);
	state = !!(edev->state & BIT(index));
	spin_unlock_irqrestore(&edev->lock, flags);

	/*
	 * Call functions in a raw notifier chain for the specific one
	 * external connector.
	 */
	raw_notifier_call_chain(&edev->nh[index], state, edev);

	/*
	 * Call functions in a raw notifier chain for the all supported
	 * external connectors.
	 */
	raw_notifier_call_chain(&edev->nh_all, state, edev);

	spin_lock_irqsave(&edev->lock, flags);
	/* This could be in interrupt handler */
	prop_buf = (char *)get_zeroed_page(GFP_ATOMIC);
	if (!prop_buf) {
		/* Unlock early before uevent */
		spin_unlock_irqrestore(&edev->lock, flags);

		dev_err(&edev->dev, "out of memory in extcon_set_state\n");
		kobject_uevent(&edev->dev.kobj, KOBJ_CHANGE);

		return -ENOMEM;
	}

	length = name_show(&edev->dev, NULL, prop_buf);
	if (length > 0) {
		if (prop_buf[length - 1] == '\n')
			prop_buf[length - 1] = 0;
		snprintf(name_buf, sizeof(name_buf), "NAME=%s", prop_buf);
		envp[env_offset++] = name_buf;
	}

	length = state_show(&edev->dev, NULL, prop_buf);
	if (length > 0) {
		if (prop_buf[length - 1] == '\n')
			prop_buf[length - 1] = 0;
		snprintf(state_buf, sizeof(state_buf), "STATE=%s", prop_buf);
		envp[env_offset++] = state_buf;
	}
	envp[env_offset] = NULL;

	/* Unlock early before uevent */
	spin_unlock_irqrestore(&edev->lock, flags);
	kobject_uevent_env(&edev->dev.kobj, KOBJ_CHANGE, envp);
	free_page((unsigned long)prop_buf);

	return 0;
}
EXPORT_SYMBOL_GPL(extcon_sync);

/**
 * extcon_get_state() - Get the state of an external connector.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_get_state(struct extcon_dev *edev, const unsigned int id)
{
	int index, state;
	unsigned long flags;

	if (!edev)
		return -EINVAL;

	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	spin_lock_irqsave(&edev->lock, flags);
	state = is_extcon_attached(edev, index);
	spin_unlock_irqrestore(&edev->lock, flags);

	return state;
}
EXPORT_SYMBOL_GPL(extcon_get_state);

/**
 * extcon_set_state() - Set the state of an external connector.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @state:	the new state of an external connector.
 *		the default semantics is true: attached / false: detached.
 *
 * Note that this function set the state of an external connector without
 * a notification. To synchronize the state of an external connector,
 * have to use extcon_set_state_sync() and extcon_sync().
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_set_state(struct extcon_dev *edev, unsigned int id, bool state)
{
	unsigned long flags;
	int index, ret = 0;

	if (!edev)
		return -EINVAL;

	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	spin_lock_irqsave(&edev->lock, flags);

	/* Check whether the external connector's state is changed. */
	if (!is_extcon_changed(edev, index, state))
		goto out;

	if (check_mutually_exclusive(edev,
		(edev->state & ~BIT(index)) | (state & BIT(index)))) {
		ret = -EPERM;
		goto out;
	}

	/*
	 * Initialize the value of extcon property before setting
	 * the detached state for an external connector.
	 */
	if (!state)
		init_property(edev, id, index);

	/* Update the state for an external connector. */
	if (state)
		edev->state |= BIT(index);
	else
		edev->state &= ~(BIT(index));
out:
	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_set_state);

/**
 * extcon_set_state_sync() - Set the state of an external connector with sync.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @state:	the new state of external connector.
 *		the default semantics is true: attached / false: detached.
 *
 * Note that this function set the state of external connector
 * and synchronize the state by sending a notification.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_set_state_sync(struct extcon_dev *edev, unsigned int id, bool state)
{
	int ret, index;
	unsigned long flags;

	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	/* Check whether the external connector's state is changed. */
	spin_lock_irqsave(&edev->lock, flags);
	ret = is_extcon_changed(edev, index, state);
	spin_unlock_irqrestore(&edev->lock, flags);
	if (!ret)
		return 0;

	ret = extcon_set_state(edev, id, state);
	if (ret < 0)
		return ret;

	return extcon_sync(edev, id);
}
EXPORT_SYMBOL_GPL(extcon_set_state_sync);

/**
 * extcon_get_property() - Get the property value of an external connector.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @prop:	the property id indicating an extcon property
 * @prop_val:	the pointer which store the value of extcon property
 *
 * Note that when getting the property value of external connector,
 * the external connector should be attached. If detached state, function
 * return 0 without property value. Also, the each property should be
 * included in the list of supported properties according to extcon type.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_get_property(struct extcon_dev *edev, unsigned int id,
				unsigned int prop,
				union extcon_property_value *prop_val)
{
	struct extcon_cable *cable;
	unsigned long flags;
	int index, ret = 0;

	*prop_val = (union extcon_property_value)(0);

	if (!edev)
		return -EINVAL;

	/* Check whether the property is supported or not */
	if (!is_extcon_property_supported(id, prop))
		return -EINVAL;

	/* Find the cable index of external connector by using id */
	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	spin_lock_irqsave(&edev->lock, flags);

	/* Check whether the property is available or not. */
	if (!is_extcon_property_capability(edev, id, index, prop)) {
		spin_unlock_irqrestore(&edev->lock, flags);
		return -EPERM;
	}

	/*
	 * Check whether the external connector is attached.
	 * If external connector is detached, the user can not
	 * get the property value.
	 */
	if (!is_extcon_attached(edev, index)) {
		spin_unlock_irqrestore(&edev->lock, flags);
		return 0;
	}

	cable = &edev->cables[index];

	/* Get the property value according to extcon type */
	switch (prop) {
	case EXTCON_PROP_USB_MIN ... EXTCON_PROP_USB_MAX:
		*prop_val = cable->usb_propval[prop - EXTCON_PROP_USB_MIN];
		break;
	case EXTCON_PROP_CHG_MIN ... EXTCON_PROP_CHG_MAX:
		*prop_val = cable->chg_propval[prop - EXTCON_PROP_CHG_MIN];
		break;
	case EXTCON_PROP_JACK_MIN ... EXTCON_PROP_JACK_MAX:
		*prop_val = cable->jack_propval[prop - EXTCON_PROP_JACK_MIN];
		break;
	case EXTCON_PROP_DISP_MIN ... EXTCON_PROP_DISP_MAX:
		*prop_val = cable->disp_propval[prop - EXTCON_PROP_DISP_MIN];
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_get_property);

/**
 * extcon_set_property() - Set the property value of an external connector.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @prop:	the property id indicating an extcon property
 * @prop_val:	the pointer including the new value of extcon property
 *
 * Note that each property should be included in the list of supported
 * properties according to the extcon type.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_set_property(struct extcon_dev *edev, unsigned int id,
				unsigned int prop,
				union extcon_property_value prop_val)
{
	struct extcon_cable *cable;
	unsigned long flags;
	int index, ret = 0;

	if (!edev)
		return -EINVAL;

	/* Check whether the property is supported or not */
	if (!is_extcon_property_supported(id, prop))
		return -EINVAL;

	/* Find the cable index of external connector by using id */
	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	spin_lock_irqsave(&edev->lock, flags);

	/* Check whether the property is available or not. */
	if (!is_extcon_property_capability(edev, id, index, prop)) {
		spin_unlock_irqrestore(&edev->lock, flags);
		return -EPERM;
	}

	cable = &edev->cables[index];

	/* Set the property value according to extcon type */
	switch (prop) {
	case EXTCON_PROP_USB_MIN ... EXTCON_PROP_USB_MAX:
		cable->usb_propval[prop - EXTCON_PROP_USB_MIN] = prop_val;
		break;
	case EXTCON_PROP_CHG_MIN ... EXTCON_PROP_CHG_MAX:
		cable->chg_propval[prop - EXTCON_PROP_CHG_MIN] = prop_val;
		break;
	case EXTCON_PROP_JACK_MIN ... EXTCON_PROP_JACK_MAX:
		cable->jack_propval[prop - EXTCON_PROP_JACK_MIN] = prop_val;
		break;
	case EXTCON_PROP_DISP_MIN ... EXTCON_PROP_DISP_MAX:
		cable->disp_propval[prop - EXTCON_PROP_DISP_MIN] = prop_val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_set_property);

/**
 * extcon_set_property_sync() - Set property of an external connector with sync.
 * @prop_val:	the pointer including the new value of extcon property
 *
 * Note that when setting the property value of external connector,
 * the external connector should be attached. The each property should
 * be included in the list of supported properties according to extcon type.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_set_property_sync(struct extcon_dev *edev, unsigned int id,
				unsigned int prop,
				union extcon_property_value prop_val)
{
	int ret;

	ret = extcon_set_property(edev, id, prop, prop_val);
	if (ret < 0)
		return ret;

	return extcon_sync(edev, id);
}
EXPORT_SYMBOL_GPL(extcon_set_property_sync);

/**
 * extcon_get_property_capability() - Get the capability of the property
 *					for an external connector.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @prop:	the property id indicating an extcon property
 *
 * Returns 1 if the property is available or 0 if not available.
 */
int extcon_get_property_capability(struct extcon_dev *edev, unsigned int id,
					unsigned int prop)
{
	int index;

	if (!edev)
		return -EINVAL;

	/* Check whether the property is supported or not */
	if (!is_extcon_property_supported(id, prop))
		return -EINVAL;

	/* Find the cable index of external connector by using id */
	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	return is_extcon_property_capability(edev, id, index, prop);
}
EXPORT_SYMBOL_GPL(extcon_get_property_capability);

/**
 * extcon_set_property_capability() - Set the capability of the property
 *					for an external connector.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @prop:	the property id indicating an extcon property
 *
 * Note that this function set the capability of the property
 * for an external connector in order to mark the bit in capability
 * bitmap which mean the available state of the property.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_set_property_capability(struct extcon_dev *edev, unsigned int id,
					unsigned int prop)
{
	struct extcon_cable *cable;
	int index, type, ret = 0;

	if (!edev)
		return -EINVAL;

	/* Check whether the property is supported or not. */
	if (!is_extcon_property_supported(id, prop))
		return -EINVAL;

	/* Find the cable index of external connector by using id. */
	index = find_cable_index_by_id(edev, id);
	if (index < 0)
		return index;

	type = get_extcon_type(prop);
	if (type < 0)
		return type;

	cable = &edev->cables[index];

	switch (type) {
	case EXTCON_TYPE_USB:
		__set_bit(prop - EXTCON_PROP_USB_MIN, cable->usb_bits);
		break;
	case EXTCON_TYPE_CHG:
		__set_bit(prop - EXTCON_PROP_CHG_MIN, cable->chg_bits);
		break;
	case EXTCON_TYPE_JACK:
		__set_bit(prop - EXTCON_PROP_JACK_MIN, cable->jack_bits);
		break;
	case EXTCON_TYPE_DISP:
		__set_bit(prop - EXTCON_PROP_DISP_MIN, cable->disp_bits);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_set_property_capability);

/**
 * extcon_get_extcon_dev() - Get the extcon device instance from the name.
 * @extcon_name:	the extcon name provided with extcon_dev_register()
 *
 * Return the pointer of extcon device if success or ERR_PTR(err) if fail.
 */
struct extcon_dev *extcon_get_extcon_dev(const char *extcon_name)
{
	struct extcon_dev *sd;

	if (!extcon_name)
		return ERR_PTR(-EINVAL);

	mutex_lock(&extcon_dev_list_lock);
	list_for_each_entry(sd, &extcon_dev_list, entry) {
		if (!strcmp(sd->name, extcon_name))
			goto out;
	}
	sd = NULL;
out:
	mutex_unlock(&extcon_dev_list_lock);
	return sd;
}
EXPORT_SYMBOL_GPL(extcon_get_extcon_dev);

/**
 * extcon_register_notifier() - Register a notifier block to get notified by
 *				any state changes from the extcon.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @nb:		a notifier block to be registered
 *
 * Note that the second parameter given to the callback of nb (val) is
 * the current state of an external connector and the third pameter
 * is the pointer of extcon device.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_register_notifier(struct extcon_dev *edev, unsigned int id,
			     struct notifier_block *nb)
{
	unsigned long flags;
	int ret, idx = -EINVAL;

	if (!edev || !nb)
		return -EINVAL;

	idx = find_cable_index_by_id(edev, id);
	if (idx < 0)
		return idx;

	spin_lock_irqsave(&edev->lock, flags);
	ret = raw_notifier_chain_register(&edev->nh[idx], nb);
	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_register_notifier);

/**
 * extcon_unregister_notifier() - Unregister a notifier block from the extcon.
 * @edev:	the extcon device
 * @id:		the unique id indicating an external connector
 * @nb:		a notifier block to be registered
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_unregister_notifier(struct extcon_dev *edev, unsigned int id,
				struct notifier_block *nb)
{
	unsigned long flags;
	int ret, idx;

	if (!edev || !nb)
		return -EINVAL;

	idx = find_cable_index_by_id(edev, id);
	if (idx < 0)
		return idx;

	spin_lock_irqsave(&edev->lock, flags);
	ret = raw_notifier_chain_unregister(&edev->nh[idx], nb);
	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_unregister_notifier);

/**
 * extcon_register_notifier_all() - Register a notifier block for all connectors.
 * @edev:	the extcon device
 * @nb:		a notifier block to be registered
 *
 * Note that this function registers a notifier block in order to receive
 * the state change of all supported external connectors from extcon device.
 * And the second parameter given to the callback of nb (val) is
 * the current state and the third pameter is the pointer of extcon device.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_register_notifier_all(struct extcon_dev *edev,
				struct notifier_block *nb)
{
	unsigned long flags;
	int ret;

	if (!edev || !nb)
		return -EINVAL;

	spin_lock_irqsave(&edev->lock, flags);
	ret = raw_notifier_chain_register(&edev->nh_all, nb);
	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_register_notifier_all);

/**
 * extcon_unregister_notifier_all() - Unregister a notifier block from extcon.
 * @edev:	the extcon device
 * @nb:		a notifier block to be registered
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_unregister_notifier_all(struct extcon_dev *edev,
				struct notifier_block *nb)
{
	unsigned long flags;
	int ret;

	if (!edev || !nb)
		return -EINVAL;

	spin_lock_irqsave(&edev->lock, flags);
	ret = raw_notifier_chain_unregister(&edev->nh_all, nb);
	spin_unlock_irqrestore(&edev->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(extcon_unregister_notifier_all);

static struct attribute *extcon_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(extcon);

static int create_extcon_class(void)
{
	if (!extcon_class) {
		extcon_class = class_create(THIS_MODULE, "extcon");
		if (IS_ERR(extcon_class))
			return PTR_ERR(extcon_class);
		extcon_class->dev_groups = extcon_groups;
	}

	return 0;
}

static void extcon_dev_release(struct device *dev)
{
}

static const char *muex_name = "mutually_exclusive";
static void dummy_sysfs_dev_release(struct device *dev)
{
}

/*
 * extcon_dev_allocate() - Allocate the memory of extcon device.
 * @supported_cable:	the array of the supported external connectors
 *			ending with EXTCON_NONE.
 *
 * Note that this function allocates the memory for extcon device 
 * and initialize default setting for the extcon device.
 *
 * Returns the pointer memory of allocated extcon_dev if success
 * or ERR_PTR(err) if fail.
 */
struct extcon_dev *extcon_dev_allocate(const unsigned int *supported_cable)
{
	struct extcon_dev *edev;

	if (!supported_cable)
		return ERR_PTR(-EINVAL);

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return ERR_PTR(-ENOMEM);

	edev->max_supported = 0;
	edev->supported_cable = supported_cable;

	return edev;
}

/*
 * extcon_dev_free() - Free the memory of extcon device.
 * @edev:	the extcon device
 */
void extcon_dev_free(struct extcon_dev *edev)
{
	kfree(edev);
}
EXPORT_SYMBOL_GPL(extcon_dev_free);

/**
 * extcon_dev_register() - Register an new extcon device
 * @edev:	the extcon device to be registered
 *
 * Among the members of edev struct, please set the "user initializing data"
 * do not set the values of "internal data", which are initialized by
 * this function.
 *
 * Note that before calling this funciton, have to allocate the memory
 * of an extcon device by using the extcon_dev_allocate(). And the extcon
 * dev should include the supported_cable information.
 *
 * Returns 0 if success or error number if fail.
 */
int extcon_dev_register(struct extcon_dev *edev)
{
	int ret, index = 0;
	static atomic_t edev_no = ATOMIC_INIT(-1);

	if (!extcon_class) {
		ret = create_extcon_class();
		if (ret < 0)
			return ret;
	}

	if (!edev || !edev->supported_cable)
		return -EINVAL;

	for (; edev->supported_cable[index] != EXTCON_NONE; index++);

	edev->max_supported = index;
	if (index > SUPPORTED_CABLE_MAX) {
		dev_err(&edev->dev,
			"exceed the maximum number of supported cables\n");
		return -EINVAL;
	}

	edev->dev.class = extcon_class;
	edev->dev.release = extcon_dev_release;

	edev->name = dev_name(edev->dev.parent);
	if (IS_ERR_OR_NULL(edev->name)) {
		dev_err(&edev->dev,
			"extcon device name is null\n");
		return -EINVAL;
	}
	dev_set_name(&edev->dev, "extcon%lu",
			(unsigned long)atomic_inc_return(&edev_no));

	if (edev->max_supported) {
		char buf[10];
		char *str;
		struct extcon_cable *cable;

		edev->cables = kzalloc(sizeof(struct extcon_cable) *
				       edev->max_supported, GFP_KERNEL);
		if (!edev->cables) {
			ret = -ENOMEM;
			goto err_sysfs_alloc;
		}
		for (index = 0; index < edev->max_supported; index++) {
			cable = &edev->cables[index];

			snprintf(buf, 10, "cable.%d", index);
			str = kzalloc(sizeof(char) * (strlen(buf) + 1),
				      GFP_KERNEL);
			if (!str) {
				for (index--; index >= 0; index--) {
					cable = &edev->cables[index];
					kfree(cable->attr_g.name);
				}
				ret = -ENOMEM;

				goto err_alloc_cables;
			}
			strcpy(str, buf);

			cable->edev = edev;
			cable->cable_index = index;
			cable->attrs[0] = &cable->attr_name.attr;
			cable->attrs[1] = &cable->attr_state.attr;
			cable->attrs[2] = NULL;
			cable->attr_g.name = str;
			cable->attr_g.attrs = cable->attrs;

			sysfs_attr_init(&cable->attr_name.attr);
			cable->attr_name.attr.name = "name";
			cable->attr_name.attr.mode = 0444;
			cable->attr_name.show = cable_name_show;

			sysfs_attr_init(&cable->attr_state.attr);
			cable->attr_state.attr.name = "state";
			cable->attr_state.attr.mode = 0444;
			cable->attr_state.show = cable_state_show;
		}
	}

	if (edev->max_supported && edev->mutually_exclusive) {
		char buf[80];
		char *name;

		/* Count the size of mutually_exclusive array */
		for (index = 0; edev->mutually_exclusive[index]; index++)
			;

		edev->attrs_muex = kzalloc(sizeof(struct attribute *) *
					   (index + 1), GFP_KERNEL);
		if (!edev->attrs_muex) {
			ret = -ENOMEM;
			goto err_muex;
		}

		edev->d_attrs_muex = kzalloc(sizeof(struct device_attribute) *
					     index, GFP_KERNEL);
		if (!edev->d_attrs_muex) {
			ret = -ENOMEM;
			kfree(edev->attrs_muex);
			goto err_muex;
		}

		for (index = 0; edev->mutually_exclusive[index]; index++) {
			sprintf(buf, "0x%x", edev->mutually_exclusive[index]);
			name = kzalloc(sizeof(char) * (strlen(buf) + 1),
				       GFP_KERNEL);
			if (!name) {
				for (index--; index >= 0; index--) {
					kfree(edev->d_attrs_muex[index].attr.
					      name);
				}
				kfree(edev->d_attrs_muex);
				kfree(edev->attrs_muex);
				ret = -ENOMEM;
				goto err_muex;
			}
			strcpy(name, buf);
			sysfs_attr_init(&edev->d_attrs_muex[index].attr);
			edev->d_attrs_muex[index].attr.name = name;
			edev->d_attrs_muex[index].attr.mode = 0000;
			edev->attrs_muex[index] = &edev->d_attrs_muex[index]
							.attr;
		}
		edev->attr_g_muex.name = muex_name;
		edev->attr_g_muex.attrs = edev->attrs_muex;

	}

	if (edev->max_supported) {
		edev->extcon_dev_type.groups =
			kzalloc(sizeof(struct attribute_group *) *
				(edev->max_supported + 2), GFP_KERNEL);
		if (!edev->extcon_dev_type.groups) {
			ret = -ENOMEM;
			goto err_alloc_groups;
		}

		edev->extcon_dev_type.name = dev_name(&edev->dev);
		edev->extcon_dev_type.release = dummy_sysfs_dev_release;

		for (index = 0; index < edev->max_supported; index++)
			edev->extcon_dev_type.groups[index] =
				&edev->cables[index].attr_g;
		if (edev->mutually_exclusive)
			edev->extcon_dev_type.groups[index] =
				&edev->attr_g_muex;

		edev->dev.type = &edev->extcon_dev_type;
	}

	ret = device_register(&edev->dev);
	if (ret) {
		put_device(&edev->dev);
		goto err_dev;
	}

	spin_lock_init(&edev->lock);
	edev->nh = devm_kcalloc(&edev->dev, edev->max_supported,
				sizeof(*edev->nh), GFP_KERNEL);
	if (!edev->nh) {
		ret = -ENOMEM;
		goto err_dev;
	}

	for (index = 0; index < edev->max_supported; index++)
		RAW_INIT_NOTIFIER_HEAD(&edev->nh[index]);

	RAW_INIT_NOTIFIER_HEAD(&edev->nh_all);

	dev_set_drvdata(&edev->dev, edev);
	edev->state = 0;

	mutex_lock(&extcon_dev_list_lock);
	list_add(&edev->entry, &extcon_dev_list);
	mutex_unlock(&extcon_dev_list_lock);

	return 0;

err_dev:
	if (edev->max_supported)
		kfree(edev->extcon_dev_type.groups);
err_alloc_groups:
	if (edev->max_supported && edev->mutually_exclusive) {
		for (index = 0; edev->mutually_exclusive[index]; index++)
			kfree(edev->d_attrs_muex[index].attr.name);
		kfree(edev->d_attrs_muex);
		kfree(edev->attrs_muex);
	}
err_muex:
	for (index = 0; index < edev->max_supported; index++)
		kfree(edev->cables[index].attr_g.name);
err_alloc_cables:
	if (edev->max_supported)
		kfree(edev->cables);
err_sysfs_alloc:
	return ret;
}
EXPORT_SYMBOL_GPL(extcon_dev_register);

/**
 * extcon_dev_unregister() - Unregister the extcon device.
 * @edev:	the extcon device to be unregistered.
 *
 * Note that this does not call kfree(edev) because edev was not allocated
 * by this class.
 */
void extcon_dev_unregister(struct extcon_dev *edev)
{
	int index;

	if (!edev)
		return;

	mutex_lock(&extcon_dev_list_lock);
	list_del(&edev->entry);
	mutex_unlock(&extcon_dev_list_lock);

	if (IS_ERR_OR_NULL(get_device(&edev->dev))) {
		dev_err(&edev->dev, "Failed to unregister extcon_dev (%s)\n",
				dev_name(&edev->dev));
		return;
	}

	device_unregister(&edev->dev);

	if (edev->mutually_exclusive && edev->max_supported) {
		for (index = 0; edev->mutually_exclusive[index];
				index++)
			kfree(edev->d_attrs_muex[index].attr.name);
		kfree(edev->d_attrs_muex);
		kfree(edev->attrs_muex);
	}

	for (index = 0; index < edev->max_supported; index++)
		kfree(edev->cables[index].attr_g.name);

	if (edev->max_supported) {
		kfree(edev->extcon_dev_type.groups);
		kfree(edev->cables);
	}

	put_device(&edev->dev);
}
EXPORT_SYMBOL_GPL(extcon_dev_unregister);

#ifdef CONFIG_OF
/*
 * extcon_get_edev_by_phandle - Get the extcon device from devicetree.
 * @dev		: the instance to the given device
 * @index	: the index into list of extcon_dev
 *
 * Return the pointer of extcon device if success or ERR_PTR(err) if fail.
 */
struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev, int index)
{
	struct device_node *node;
	struct extcon_dev *edev;

	if (!dev)
		return ERR_PTR(-EINVAL);

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, "extcon", index);
	if (!node) {
		dev_dbg(dev, "failed to get phandle in %pOF node\n",
			dev->of_node);
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&extcon_dev_list_lock);
	list_for_each_entry(edev, &extcon_dev_list, entry) {
		if (edev->dev.parent && edev->dev.parent->of_node == node) {
			mutex_unlock(&extcon_dev_list_lock);
			of_node_put(node);
			return edev;
		}
	}
	mutex_unlock(&extcon_dev_list_lock);
	of_node_put(node);

	return ERR_PTR(-EPROBE_DEFER);
}
#else
struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev, int index)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(extcon_get_edev_by_phandle);

/**
 * extcon_get_edev_name() - Get the name of the extcon device.
 * @edev:	the extcon device
 */
const char *extcon_get_edev_name(struct extcon_dev *edev)
{
	return !edev ? NULL : edev->name;
}

static int __init extcon_class_init(void)
{
	return create_extcon_class();
}
module_init(extcon_class_init);

static void __exit extcon_class_exit(void)
{
	class_destroy(extcon_class);
}
module_exit(extcon_class_exit);

MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("External Connector (extcon) framework");
MODULE_LICENSE("GPL v2");
