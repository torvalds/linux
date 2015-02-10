/*
 *  drivers/extcon/extcon_class.c
 *
 *  External connector (extcon) class driver
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
 *
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/of.h>

/*
 * extcon_cable_name suggests the standard cable names for commonly used
 * cable types.
 *
 * However, please do not use extcon_cable_name directly for extcon_dev
 * struct's supported_cable pointer unless your device really supports
 * every single port-type of the following cable names. Please choose cable
 * names that are actually used in your extcon device.
 */
const char extcon_cable_name[][CABLE_NAME_MAX + 1] = {
	[EXTCON_USB]		= "USB",
	[EXTCON_USB_HOST]	= "USB-Host",
	[EXTCON_TA]		= "TA",
	[EXTCON_FAST_CHARGER]	= "Fast-charger",
	[EXTCON_SLOW_CHARGER]	= "Slow-charger",
	[EXTCON_CHARGE_DOWNSTREAM]	= "Charge-downstream",
	[EXTCON_HDMI]		= "HDMI",
	[EXTCON_MHL]		= "MHL",
	[EXTCON_DVI]		= "DVI",
	[EXTCON_VGA]		= "VGA",
	[EXTCON_DOCK]		= "Dock",
	[EXTCON_LINE_IN]	= "Line-in",
	[EXTCON_LINE_OUT]	= "Line-out",
	[EXTCON_MIC_IN]		= "Microphone",
	[EXTCON_HEADPHONE_OUT]	= "Headphone",
	[EXTCON_SPDIF_IN]	= "SPDIF-in",
	[EXTCON_SPDIF_OUT]	= "SPDIF-out",
	[EXTCON_VIDEO_IN]	= "Video-in",
	[EXTCON_VIDEO_OUT]	= "Video-out",
	[EXTCON_MECHANICAL]	= "Mechanical",
};

static struct class *extcon_class;
#if defined(CONFIG_ANDROID)
static struct class_compat *switch_class;
#endif /* CONFIG_ANDROID */

static LIST_HEAD(extcon_dev_list);
static DEFINE_MUTEX(extcon_dev_list_lock);

/**
 * check_mutually_exclusive - Check if new_state violates mutually_exclusive
 *			      condition.
 * @edev:	the extcon device
 * @new_state:	new cable attach status for @edev
 *
 * Returns 0 if nothing violates. Returns the index + 1 for the first
 * violated condition.
 */
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

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int i, count = 0;
	struct extcon_dev *edev = dev_get_drvdata(dev);

	if (edev->print_state) {
		int ret = edev->print_state(edev, buf);

		if (ret >= 0)
			return ret;
		/* Use default if failed */
	}

	if (edev->max_supported == 0)
		return sprintf(buf, "%u\n", edev->state);

	for (i = 0; i < SUPPORTED_CABLE_MAX; i++) {
		if (!edev->supported_cable[i])
			break;
		count += sprintf(buf + count, "%s=%d\n",
				 edev->supported_cable[i],
				 !!(edev->state & (1 << i)));
	}

	return count;
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	u32 state;
	ssize_t ret = 0;
	struct extcon_dev *edev = dev_get_drvdata(dev);

	ret = sscanf(buf, "0x%x", &state);
	if (ret == 0)
		ret = -EINVAL;
	else
		ret = extcon_set_state(edev, state);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(state);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct extcon_dev *edev = dev_get_drvdata(dev);

	/* Optional callback given by the user */
	if (edev->print_name) {
		int ret = edev->print_name(edev, buf);
		if (ret >= 0)
			return ret;
	}

	return sprintf(buf, "%s\n", dev_name(&edev->dev));
}
static DEVICE_ATTR_RO(name);

static ssize_t cable_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct extcon_cable *cable = container_of(attr, struct extcon_cable,
						  attr_name);

	return sprintf(buf, "%s\n",
		       cable->edev->supported_cable[cable->cable_index]);
}

static ssize_t cable_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct extcon_cable *cable = container_of(attr, struct extcon_cable,
						  attr_state);

	return sprintf(buf, "%d\n",
		       extcon_get_cable_state_(cable->edev,
					       cable->cable_index));
}

/**
 * extcon_update_state() - Update the cable attach states of the extcon device
 *			   only for the masked bits.
 * @edev:	the extcon device
 * @mask:	the bit mask to designate updated bits.
 * @state:	new cable attach status for @edev
 *
 * Changing the state sends uevent with environment variable containing
 * the name of extcon device (envp[0]) and the state output (envp[1]).
 * Tizen uses this format for extcon device to get events from ports.
 * Android uses this format as well.
 *
 * Note that the notifier provides which bits are changed in the state
 * variable with the val parameter (second) to the callback.
 */
int extcon_update_state(struct extcon_dev *edev, u32 mask, u32 state)
{
	char name_buf[120];
	char state_buf[120];
	char *prop_buf;
	char *envp[3];
	int env_offset = 0;
	int length;
	unsigned long flags;

	spin_lock_irqsave(&edev->lock, flags);

	if (edev->state != ((edev->state & ~mask) | (state & mask))) {
		u32 old_state = edev->state;

		if (check_mutually_exclusive(edev, (edev->state & ~mask) |
						   (state & mask))) {
			spin_unlock_irqrestore(&edev->lock, flags);
			return -EPERM;
		}

		edev->state &= ~mask;
		edev->state |= state & mask;

		raw_notifier_call_chain(&edev->nh, old_state, edev);
		/* This could be in interrupt handler */
		prop_buf = (char *)get_zeroed_page(GFP_ATOMIC);
		if (prop_buf) {
			length = name_show(&edev->dev, NULL, prop_buf);
			if (length > 0) {
				if (prop_buf[length - 1] == '\n')
					prop_buf[length - 1] = 0;
				snprintf(name_buf, sizeof(name_buf),
					"NAME=%s", prop_buf);
				envp[env_offset++] = name_buf;
			}
			length = state_show(&edev->dev, NULL, prop_buf);
			if (length > 0) {
				if (prop_buf[length - 1] == '\n')
					prop_buf[length - 1] = 0;
				snprintf(state_buf, sizeof(state_buf),
					"STATE=%s", prop_buf);
				envp[env_offset++] = state_buf;
			}
			envp[env_offset] = NULL;
			/* Unlock early before uevent */
			spin_unlock_irqrestore(&edev->lock, flags);

			kobject_uevent_env(&edev->dev.kobj, KOBJ_CHANGE, envp);
			free_page((unsigned long)prop_buf);
		} else {
			/* Unlock early before uevent */
			spin_unlock_irqrestore(&edev->lock, flags);

			dev_err(&edev->dev, "out of memory in extcon_set_state\n");
			kobject_uevent(&edev->dev.kobj, KOBJ_CHANGE);
		}
	} else {
		/* No changes */
		spin_unlock_irqrestore(&edev->lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(extcon_update_state);

/**
 * extcon_set_state() - Set the cable attach states of the extcon device.
 * @edev:	the extcon device
 * @state:	new cable attach status for @edev
 *
 * Note that notifier provides which bits are changed in the state
 * variable with the val parameter (second) to the callback.
 */
int extcon_set_state(struct extcon_dev *edev, u32 state)
{
	return extcon_update_state(edev, 0xffffffff, state);
}
EXPORT_SYMBOL_GPL(extcon_set_state);

/**
 * extcon_find_cable_index() - Get the cable index based on the cable name.
 * @edev:	the extcon device that has the cable.
 * @cable_name:	cable name to be searched.
 *
 * Note that accessing a cable state based on cable_index is faster than
 * cable_name because using cable_name induces a loop with strncmp().
 * Thus, when get/set_cable_state is repeatedly used, using cable_index
 * is recommended.
 */
int extcon_find_cable_index(struct extcon_dev *edev, const char *cable_name)
{
	int i;

	if (edev->supported_cable) {
		for (i = 0; edev->supported_cable[i]; i++) {
			if (!strncmp(edev->supported_cable[i],
				cable_name, CABLE_NAME_MAX))
				return i;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(extcon_find_cable_index);

/**
 * extcon_get_cable_state_() - Get the status of a specific cable.
 * @edev:	the extcon device that has the cable.
 * @index:	cable index that can be retrieved by extcon_find_cable_index().
 */
int extcon_get_cable_state_(struct extcon_dev *edev, int index)
{
	if (index < 0 || (edev->max_supported && edev->max_supported <= index))
		return -EINVAL;

	return !!(edev->state & (1 << index));
}
EXPORT_SYMBOL_GPL(extcon_get_cable_state_);

/**
 * extcon_get_cable_state() - Get the status of a specific cable.
 * @edev:	the extcon device that has the cable.
 * @cable_name:	cable name.
 *
 * Note that this is slower than extcon_get_cable_state_.
 */
int extcon_get_cable_state(struct extcon_dev *edev, const char *cable_name)
{
	return extcon_get_cable_state_(edev, extcon_find_cable_index
						(edev, cable_name));
}
EXPORT_SYMBOL_GPL(extcon_get_cable_state);

/**
 * extcon_set_cable_state_() - Set the status of a specific cable.
 * @edev:		the extcon device that has the cable.
 * @index:		cable index that can be retrieved by
 *			extcon_find_cable_index().
 * @cable_state:	the new cable status. The default semantics is
 *			true: attached / false: detached.
 */
int extcon_set_cable_state_(struct extcon_dev *edev,
			int index, bool cable_state)
{
	u32 state;

	if (index < 0 || (edev->max_supported && edev->max_supported <= index))
		return -EINVAL;

	state = cable_state ? (1 << index) : 0;
	return extcon_update_state(edev, 1 << index, state);
}
EXPORT_SYMBOL_GPL(extcon_set_cable_state_);

/**
 * extcon_set_cable_state() - Set the status of a specific cable.
 * @edev:		the extcon device that has the cable.
 * @cable_name:		cable name.
 * @cable_state:	the new cable status. The default semantics is
 *			true: attached / false: detached.
 *
 * Note that this is slower than extcon_set_cable_state_.
 */
int extcon_set_cable_state(struct extcon_dev *edev,
			const char *cable_name, bool cable_state)
{
	return extcon_set_cable_state_(edev, extcon_find_cable_index
					(edev, cable_name), cable_state);
}
EXPORT_SYMBOL_GPL(extcon_set_cable_state);

/**
 * extcon_get_extcon_dev() - Get the extcon device instance from the name
 * @extcon_name:	The extcon name provided with extcon_dev_register()
 */
struct extcon_dev *extcon_get_extcon_dev(const char *extcon_name)
{
	struct extcon_dev *sd;

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

static int _call_per_cable(struct notifier_block *nb, unsigned long val,
			   void *ptr)
{
	struct extcon_specific_cable_nb *obj = container_of(nb,
			struct extcon_specific_cable_nb, internal_nb);
	struct extcon_dev *edev = ptr;

	if ((val & (1 << obj->cable_index)) !=
	    (edev->state & (1 << obj->cable_index))) {
		bool cable_state = true;

		obj->previous_value = val;

		if (val & (1 << obj->cable_index))
			cable_state = false;

		return obj->user_nb->notifier_call(obj->user_nb,
				cable_state, ptr);
	}

	return NOTIFY_OK;
}

/**
 * extcon_register_interest() - Register a notifier for a state change of a
 *				specific cable, not an entier set of cables of a
 *				extcon device.
 * @obj:		an empty extcon_specific_cable_nb object to be returned.
 * @extcon_name:	the name of extcon device.
 *			if NULL, extcon_register_interest will register
 *			every cable with the target cable_name given.
 * @cable_name:		the target cable name.
 * @nb:			the notifier block to get notified.
 *
 * Provide an empty extcon_specific_cable_nb. extcon_register_interest() sets
 * the struct for you.
 *
 * extcon_register_interest is a helper function for those who want to get
 * notification for a single specific cable's status change. If a user wants
 * to get notification for any changes of all cables of a extcon device,
 * he/she should use the general extcon_register_notifier().
 *
 * Note that the second parameter given to the callback of nb (val) is
 * "old_state", not the current state. The current state can be retrieved
 * by looking at the third pameter (edev pointer)'s state value.
 */
int extcon_register_interest(struct extcon_specific_cable_nb *obj,
			     const char *extcon_name, const char *cable_name,
			     struct notifier_block *nb)
{
	if (!obj || !cable_name || !nb)
		return -EINVAL;

	if (extcon_name) {
		obj->edev = extcon_get_extcon_dev(extcon_name);
		if (!obj->edev)
			return -ENODEV;

		obj->cable_index = extcon_find_cable_index(obj->edev,
							  cable_name);
		if (obj->cable_index < 0)
			return obj->cable_index;

		obj->user_nb = nb;

		obj->internal_nb.notifier_call = _call_per_cable;

		return raw_notifier_chain_register(&obj->edev->nh,
						  &obj->internal_nb);
	} else {
		struct class_dev_iter iter;
		struct extcon_dev *extd;
		struct device *dev;

		if (!extcon_class)
			return -ENODEV;
		class_dev_iter_init(&iter, extcon_class, NULL, NULL);
		while ((dev = class_dev_iter_next(&iter))) {
			extd = dev_get_drvdata(dev);

			if (extcon_find_cable_index(extd, cable_name) < 0)
				continue;

			class_dev_iter_exit(&iter);
			return extcon_register_interest(obj, extd->name,
						cable_name, nb);
		}

		return -ENODEV;
	}
}
EXPORT_SYMBOL_GPL(extcon_register_interest);

/**
 * extcon_unregister_interest() - Unregister the notifier registered by
 *				  extcon_register_interest().
 * @obj:	the extcon_specific_cable_nb object returned by
 *		extcon_register_interest().
 */
int extcon_unregister_interest(struct extcon_specific_cable_nb *obj)
{
	if (!obj)
		return -EINVAL;

	return raw_notifier_chain_unregister(&obj->edev->nh, &obj->internal_nb);
}
EXPORT_SYMBOL_GPL(extcon_unregister_interest);

/**
 * extcon_register_notifier() - Register a notifiee to get notified by
 *				any attach status changes from the extcon.
 * @edev:	the extcon device.
 * @nb:		a notifier block to be registered.
 *
 * Note that the second parameter given to the callback of nb (val) is
 * "old_state", not the current state. The current state can be retrieved
 * by looking at the third pameter (edev pointer)'s state value.
 */
int extcon_register_notifier(struct extcon_dev *edev,
			struct notifier_block *nb)
{
	return raw_notifier_chain_register(&edev->nh, nb);
}
EXPORT_SYMBOL_GPL(extcon_register_notifier);

/**
 * extcon_unregister_notifier() - Unregister a notifiee from the extcon device.
 * @edev:	the extcon device.
 * @nb:		a registered notifier block to be unregistered.
 */
int extcon_unregister_notifier(struct extcon_dev *edev,
			struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&edev->nh, nb);
}
EXPORT_SYMBOL_GPL(extcon_unregister_notifier);

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

#if defined(CONFIG_ANDROID)
		switch_class = class_compat_register("switch");
		if (WARN(!switch_class, "cannot allocate"))
			return -ENOMEM;
#endif /* CONFIG_ANDROID */
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
 * @supported_cable:	Array of supported cable names ending with NULL.
 *			If supported_cable is NULL, cable name related APIs
 *			are disabled.
 *
 * This function allocates the memory for extcon device without allocating
 * memory in each extcon provider driver and initialize default setting for
 * extcon device.
 *
 * Return the pointer of extcon device if success or ERR_PTR(err) if fail
 */
struct extcon_dev *extcon_dev_allocate(const char **supported_cable)
{
	struct extcon_dev *edev;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return ERR_PTR(-ENOMEM);

	edev->max_supported = 0;
	edev->supported_cable = supported_cable;

	return edev;
}

/*
 * extcon_dev_free() - Free the memory of extcon device.
 * @edev:	the extcon device to free
 */
void extcon_dev_free(struct extcon_dev *edev)
{
	kfree(edev);
}
EXPORT_SYMBOL_GPL(extcon_dev_free);

static int devm_extcon_dev_match(struct device *dev, void *res, void *data)
{
	struct extcon_dev **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

static void devm_extcon_dev_release(struct device *dev, void *res)
{
	extcon_dev_free(*(struct extcon_dev **)res);
}

/**
 * devm_extcon_dev_allocate - Allocate managed extcon device
 * @dev:		device owning the extcon device being created
 * @supported_cable:	Array of supported cable names ending with NULL.
 *			If supported_cable is NULL, cable name related APIs
 *			are disabled.
 *
 * This function manages automatically the memory of extcon device using device
 * resource management and simplify the control of freeing the memory of extcon
 * device.
 *
 * Returns the pointer memory of allocated extcon_dev if success
 * or ERR_PTR(err) if fail
 */
struct extcon_dev *devm_extcon_dev_allocate(struct device *dev,
					    const char **supported_cable)
{
	struct extcon_dev **ptr, *edev;

	ptr = devres_alloc(devm_extcon_dev_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	edev = extcon_dev_allocate(supported_cable);
	if (IS_ERR(edev)) {
		devres_free(ptr);
		return edev;
	}

	edev->dev.parent = dev;

	*ptr = edev;
	devres_add(dev, ptr);

	return edev;
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_allocate);

void devm_extcon_dev_free(struct device *dev, struct extcon_dev *edev)
{
	WARN_ON(devres_release(dev, devm_extcon_dev_release,
			       devm_extcon_dev_match, edev));
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_free);

/**
 * extcon_dev_register() - Register a new extcon device
 * @edev	: the new extcon device (should be allocated before calling)
 *
 * Among the members of edev struct, please set the "user initializing data"
 * in any case and set the "optional callbacks" if required. However, please
 * do not set the values of "internal data", which are initialized by
 * this function.
 */
int extcon_dev_register(struct extcon_dev *edev)
{
	int ret, index = 0;

	if (!extcon_class) {
		ret = create_extcon_class();
		if (ret < 0)
			return ret;
	}

	if (edev->supported_cable) {
		/* Get size of array */
		for (index = 0; edev->supported_cable[index]; index++)
			;
		edev->max_supported = index;
	} else {
		edev->max_supported = 0;
	}

	if (index > SUPPORTED_CABLE_MAX) {
		dev_err(&edev->dev, "extcon: maximum number of supported cables exceeded.\n");
		return -EINVAL;
	}

	edev->dev.class = extcon_class;
	edev->dev.release = extcon_dev_release;

	edev->name = edev->name ? edev->name : dev_name(edev->dev.parent);
	if (IS_ERR_OR_NULL(edev->name)) {
		dev_err(&edev->dev,
			"extcon device name is null\n");
		return -EINVAL;
	}
	dev_set_name(&edev->dev, "%s", edev->name);

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
#if defined(CONFIG_ANDROID)
	if (switch_class)
		ret = class_compat_create_link(switch_class, &edev->dev, NULL);
#endif /* CONFIG_ANDROID */

	spin_lock_init(&edev->lock);

	RAW_INIT_NOTIFIER_HEAD(&edev->nh);

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
 * @edev:	the extcon device instance to be unregistered.
 *
 * Note that this does not call kfree(edev) because edev was not allocated
 * by this class.
 */
void extcon_dev_unregister(struct extcon_dev *edev)
{
	int index;

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

#if defined(CONFIG_ANDROID)
	if (switch_class)
		class_compat_remove_link(switch_class, &edev->dev, NULL);
#endif
	put_device(&edev->dev);
}
EXPORT_SYMBOL_GPL(extcon_dev_unregister);

static void devm_extcon_dev_unreg(struct device *dev, void *res)
{
	extcon_dev_unregister(*(struct extcon_dev **)res);
}

/**
 * devm_extcon_dev_register() - Resource-managed extcon_dev_register()
 * @dev:	device to allocate extcon device
 * @edev:	the new extcon device to register
 *
 * Managed extcon_dev_register() function. If extcon device is attached with
 * this function, that extcon device is automatically unregistered on driver
 * detach. Internally this function calls extcon_dev_register() function.
 * To get more information, refer that function.
 *
 * If extcon device is registered with this function and the device needs to be
 * unregistered separately, devm_extcon_dev_unregister() should be used.
 *
 * Returns 0 if success or negaive error number if failure.
 */
int devm_extcon_dev_register(struct device *dev, struct extcon_dev *edev)
{
	struct extcon_dev **ptr;
	int ret;

	ptr = devres_alloc(devm_extcon_dev_unreg, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = extcon_dev_register(edev);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	*ptr = edev;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_register);

/**
 * devm_extcon_dev_unregister() - Resource-managed extcon_dev_unregister()
 * @dev:	device the extcon belongs to
 * @edev:	the extcon device to unregister
 *
 * Unregister extcon device that is registered with devm_extcon_dev_register()
 * function.
 */
void devm_extcon_dev_unregister(struct device *dev, struct extcon_dev *edev)
{
	WARN_ON(devres_release(dev, devm_extcon_dev_unreg,
			       devm_extcon_dev_match, edev));
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_unregister);

#ifdef CONFIG_OF
/*
 * extcon_get_edev_by_phandle - Get the extcon device from devicetree
 * @dev - instance to the given device
 * @index - index into list of extcon_dev
 *
 * return the instance of extcon device
 */
struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev, int index)
{
	struct device_node *node;
	struct extcon_dev *edev;

	if (!dev->of_node) {
		dev_err(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, "extcon", index);
	if (!node) {
		dev_err(dev, "failed to get phandle in %s node\n",
			dev->of_node->full_name);
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&extcon_dev_list_lock);
	list_for_each_entry(edev, &extcon_dev_list, entry) {
		if (edev->dev.parent && edev->dev.parent->of_node == node) {
			mutex_unlock(&extcon_dev_list_lock);
			return edev;
		}
	}
	mutex_unlock(&extcon_dev_list_lock);

	return ERR_PTR(-EPROBE_DEFER);
}
#else
struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev, int index)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(extcon_get_edev_by_phandle);

static int __init extcon_class_init(void)
{
	return create_extcon_class();
}
module_init(extcon_class_init);

static void __exit extcon_class_exit(void)
{
#if defined(CONFIG_ANDROID)
	class_compat_unregister(switch_class);
#endif
	class_destroy(extcon_class);
}
module_exit(extcon_class_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("External connector (extcon) class driver");
MODULE_LICENSE("GPL");
