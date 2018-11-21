// SPDX-License-Identifier: GPL-2.0+
/*
 * phy.c -- USB phy handling
 *
 * Copyright (C) 2004-2013 Texas Instruments
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <linux/usb/phy.h>

/* Default current range by charger type. */
#define DEFAULT_SDP_CUR_MIN	2
#define DEFAULT_SDP_CUR_MAX	500
#define DEFAULT_SDP_CUR_MIN_SS	150
#define DEFAULT_SDP_CUR_MAX_SS	900
#define DEFAULT_DCP_CUR_MIN	500
#define DEFAULT_DCP_CUR_MAX	5000
#define DEFAULT_CDP_CUR_MIN	1500
#define DEFAULT_CDP_CUR_MAX	5000
#define DEFAULT_ACA_CUR_MIN	1500
#define DEFAULT_ACA_CUR_MAX	5000

static LIST_HEAD(phy_list);
static DEFINE_SPINLOCK(phy_lock);

struct phy_devm {
	struct usb_phy *phy;
	struct notifier_block *nb;
};

static struct usb_phy *__usb_find_phy(struct list_head *list,
	enum usb_phy_type type)
{
	struct usb_phy  *phy = NULL;

	list_for_each_entry(phy, list, head) {
		if (phy->type != type)
			continue;

		return phy;
	}

	return ERR_PTR(-ENODEV);
}

static struct usb_phy *__of_usb_find_phy(struct device_node *node)
{
	struct usb_phy  *phy;

	if (!of_device_is_available(node))
		return ERR_PTR(-ENODEV);

	list_for_each_entry(phy, &phy_list, head) {
		if (node != phy->dev->of_node)
			continue;

		return phy;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static void usb_phy_set_default_current(struct usb_phy *usb_phy)
{
	usb_phy->chg_cur.sdp_min = DEFAULT_SDP_CUR_MIN;
	usb_phy->chg_cur.sdp_max = DEFAULT_SDP_CUR_MAX;
	usb_phy->chg_cur.dcp_min = DEFAULT_DCP_CUR_MIN;
	usb_phy->chg_cur.dcp_max = DEFAULT_DCP_CUR_MAX;
	usb_phy->chg_cur.cdp_min = DEFAULT_CDP_CUR_MIN;
	usb_phy->chg_cur.cdp_max = DEFAULT_CDP_CUR_MAX;
	usb_phy->chg_cur.aca_min = DEFAULT_ACA_CUR_MIN;
	usb_phy->chg_cur.aca_max = DEFAULT_ACA_CUR_MAX;
}

/**
 * usb_phy_notify_charger_work - notify the USB charger state
 * @work - the charger work to notify the USB charger state
 *
 * This work can be issued when USB charger state has been changed or
 * USB charger current has been changed, then we can notify the current
 * what can be drawn to power user and the charger state to userspace.
 *
 * If we get the charger type from extcon subsystem, we can notify the
 * charger state to power user automatically by usb_phy_get_charger_type()
 * issuing from extcon subsystem.
 *
 * If we get the charger type from ->charger_detect() instead of extcon
 * subsystem, the usb phy driver should issue usb_phy_set_charger_state()
 * to set charger state when the charger state has been changed.
 */
static void usb_phy_notify_charger_work(struct work_struct *work)
{
	struct usb_phy *usb_phy = container_of(work, struct usb_phy, chg_work);
	char uchger_state[50] = { 0 };
	char *envp[] = { uchger_state, NULL };
	unsigned int min, max;

	switch (usb_phy->chg_state) {
	case USB_CHARGER_PRESENT:
		usb_phy_get_charger_current(usb_phy, &min, &max);

		atomic_notifier_call_chain(&usb_phy->notifier, max, usb_phy);
		snprintf(uchger_state, ARRAY_SIZE(uchger_state),
			 "USB_CHARGER_STATE=%s", "USB_CHARGER_PRESENT");
		break;
	case USB_CHARGER_ABSENT:
		usb_phy_set_default_current(usb_phy);

		atomic_notifier_call_chain(&usb_phy->notifier, 0, usb_phy);
		snprintf(uchger_state, ARRAY_SIZE(uchger_state),
			 "USB_CHARGER_STATE=%s", "USB_CHARGER_ABSENT");
		break;
	default:
		dev_warn(usb_phy->dev, "Unknown USB charger state: %d\n",
			 usb_phy->chg_state);
		return;
	}

	kobject_uevent_env(&usb_phy->dev->kobj, KOBJ_CHANGE, envp);
}

static void __usb_phy_get_charger_type(struct usb_phy *usb_phy)
{
	if (extcon_get_state(usb_phy->edev, EXTCON_CHG_USB_SDP) > 0) {
		usb_phy->chg_type = SDP_TYPE;
		usb_phy->chg_state = USB_CHARGER_PRESENT;
	} else if (extcon_get_state(usb_phy->edev, EXTCON_CHG_USB_CDP) > 0) {
		usb_phy->chg_type = CDP_TYPE;
		usb_phy->chg_state = USB_CHARGER_PRESENT;
	} else if (extcon_get_state(usb_phy->edev, EXTCON_CHG_USB_DCP) > 0) {
		usb_phy->chg_type = DCP_TYPE;
		usb_phy->chg_state = USB_CHARGER_PRESENT;
	} else if (extcon_get_state(usb_phy->edev, EXTCON_CHG_USB_ACA) > 0) {
		usb_phy->chg_type = ACA_TYPE;
		usb_phy->chg_state = USB_CHARGER_PRESENT;
	} else {
		usb_phy->chg_type = UNKNOWN_TYPE;
		usb_phy->chg_state = USB_CHARGER_ABSENT;
	}

	schedule_work(&usb_phy->chg_work);
}

/**
 * usb_phy_get_charger_type - get charger type from extcon subsystem
 * @nb -the notifier block to determine charger type
 * @state - the cable state
 * @data - private data
 *
 * Determin the charger type from extcon subsystem which also means the
 * charger state has been chaned, then we should notify this event.
 */
static int usb_phy_get_charger_type(struct notifier_block *nb,
				    unsigned long state, void *data)
{
	struct usb_phy *usb_phy = container_of(nb, struct usb_phy, type_nb);

	__usb_phy_get_charger_type(usb_phy);
	return NOTIFY_OK;
}

/**
 * usb_phy_set_charger_current - set the USB charger current
 * @usb_phy - the USB phy to be used
 * @mA - the current need to be set
 *
 * Usually we only change the charger default current when USB finished the
 * enumeration as one SDP charger. As one SDP charger, usb_phy_set_power()
 * will issue this function to change charger current when after setting USB
 * configuration, or suspend/resume USB. For other type charger, we should
 * use the default charger current and we do not suggest to issue this function
 * to change the charger current.
 *
 * When USB charger current has been changed, we need to notify the power users.
 */
void usb_phy_set_charger_current(struct usb_phy *usb_phy, unsigned int mA)
{
	switch (usb_phy->chg_type) {
	case SDP_TYPE:
		if (usb_phy->chg_cur.sdp_max == mA)
			return;

		usb_phy->chg_cur.sdp_max = (mA > DEFAULT_SDP_CUR_MAX_SS) ?
			DEFAULT_SDP_CUR_MAX_SS : mA;
		break;
	case DCP_TYPE:
		if (usb_phy->chg_cur.dcp_max == mA)
			return;

		usb_phy->chg_cur.dcp_max = (mA > DEFAULT_DCP_CUR_MAX) ?
			DEFAULT_DCP_CUR_MAX : mA;
		break;
	case CDP_TYPE:
		if (usb_phy->chg_cur.cdp_max == mA)
			return;

		usb_phy->chg_cur.cdp_max = (mA > DEFAULT_CDP_CUR_MAX) ?
			DEFAULT_CDP_CUR_MAX : mA;
		break;
	case ACA_TYPE:
		if (usb_phy->chg_cur.aca_max == mA)
			return;

		usb_phy->chg_cur.aca_max = (mA > DEFAULT_ACA_CUR_MAX) ?
			DEFAULT_ACA_CUR_MAX : mA;
		break;
	default:
		return;
	}

	schedule_work(&usb_phy->chg_work);
}
EXPORT_SYMBOL_GPL(usb_phy_set_charger_current);

/**
 * usb_phy_get_charger_current - get the USB charger current
 * @usb_phy - the USB phy to be used
 * @min - the minimum current
 * @max - the maximum current
 *
 * Usually we will notify the maximum current to power user, but for some
 * special case, power user also need the minimum current value. Then the
 * power user can issue this function to get the suitable current.
 */
void usb_phy_get_charger_current(struct usb_phy *usb_phy,
				 unsigned int *min, unsigned int *max)
{
	switch (usb_phy->chg_type) {
	case SDP_TYPE:
		*min = usb_phy->chg_cur.sdp_min;
		*max = usb_phy->chg_cur.sdp_max;
		break;
	case DCP_TYPE:
		*min = usb_phy->chg_cur.dcp_min;
		*max = usb_phy->chg_cur.dcp_max;
		break;
	case CDP_TYPE:
		*min = usb_phy->chg_cur.cdp_min;
		*max = usb_phy->chg_cur.cdp_max;
		break;
	case ACA_TYPE:
		*min = usb_phy->chg_cur.aca_min;
		*max = usb_phy->chg_cur.aca_max;
		break;
	default:
		*min = 0;
		*max = 0;
		break;
	}
}
EXPORT_SYMBOL_GPL(usb_phy_get_charger_current);

/**
 * usb_phy_set_charger_state - set the USB charger state
 * @usb_phy - the USB phy to be used
 * @state - the new state need to be set for charger
 *
 * The usb phy driver can issue this function when the usb phy driver
 * detected the charger state has been changed, in this case the charger
 * type should be get from ->charger_detect().
 */
void usb_phy_set_charger_state(struct usb_phy *usb_phy,
			       enum usb_charger_state state)
{
	if (usb_phy->chg_state == state || !usb_phy->charger_detect)
		return;

	usb_phy->chg_state = state;
	if (usb_phy->chg_state == USB_CHARGER_PRESENT)
		usb_phy->chg_type = usb_phy->charger_detect(usb_phy);
	else
		usb_phy->chg_type = UNKNOWN_TYPE;

	schedule_work(&usb_phy->chg_work);
}
EXPORT_SYMBOL_GPL(usb_phy_set_charger_state);

static void devm_usb_phy_release(struct device *dev, void *res)
{
	struct usb_phy *phy = *(struct usb_phy **)res;

	usb_put_phy(phy);
}

static void devm_usb_phy_release2(struct device *dev, void *_res)
{
	struct phy_devm *res = _res;

	if (res->nb)
		usb_unregister_notifier(res->phy, res->nb);
	usb_put_phy(res->phy);
}

static int devm_usb_phy_match(struct device *dev, void *res, void *match_data)
{
	struct usb_phy **phy = res;

	return *phy == match_data;
}

static void usb_charger_init(struct usb_phy *usb_phy)
{
	usb_phy->chg_type = UNKNOWN_TYPE;
	usb_phy->chg_state = USB_CHARGER_DEFAULT;
	usb_phy_set_default_current(usb_phy);
	INIT_WORK(&usb_phy->chg_work, usb_phy_notify_charger_work);
}

static int usb_add_extcon(struct usb_phy *x)
{
	int ret;

	if (of_property_read_bool(x->dev->of_node, "extcon")) {
		x->edev = extcon_get_edev_by_phandle(x->dev, 0);
		if (IS_ERR(x->edev))
			return PTR_ERR(x->edev);

		x->id_edev = extcon_get_edev_by_phandle(x->dev, 1);
		if (IS_ERR(x->id_edev)) {
			x->id_edev = NULL;
			dev_info(x->dev, "No separate ID extcon device\n");
		}

		if (x->vbus_nb.notifier_call) {
			ret = devm_extcon_register_notifier(x->dev, x->edev,
							    EXTCON_USB,
							    &x->vbus_nb);
			if (ret < 0) {
				dev_err(x->dev,
					"register VBUS notifier failed\n");
				return ret;
			}
		} else {
			x->type_nb.notifier_call = usb_phy_get_charger_type;

			ret = devm_extcon_register_notifier(x->dev, x->edev,
							    EXTCON_CHG_USB_SDP,
							    &x->type_nb);
			if (ret) {
				dev_err(x->dev,
					"register extcon USB SDP failed.\n");
				return ret;
			}

			ret = devm_extcon_register_notifier(x->dev, x->edev,
							    EXTCON_CHG_USB_CDP,
							    &x->type_nb);
			if (ret) {
				dev_err(x->dev,
					"register extcon USB CDP failed.\n");
				return ret;
			}

			ret = devm_extcon_register_notifier(x->dev, x->edev,
							    EXTCON_CHG_USB_DCP,
							    &x->type_nb);
			if (ret) {
				dev_err(x->dev,
					"register extcon USB DCP failed.\n");
				return ret;
			}

			ret = devm_extcon_register_notifier(x->dev, x->edev,
							    EXTCON_CHG_USB_ACA,
							    &x->type_nb);
			if (ret) {
				dev_err(x->dev,
					"register extcon USB ACA failed.\n");
				return ret;
			}
		}

		if (x->id_nb.notifier_call) {
			struct extcon_dev *id_ext;

			if (x->id_edev)
				id_ext = x->id_edev;
			else
				id_ext = x->edev;

			ret = devm_extcon_register_notifier(x->dev, id_ext,
							    EXTCON_USB_HOST,
							    &x->id_nb);
			if (ret < 0) {
				dev_err(x->dev,
					"register ID notifier failed\n");
				return ret;
			}
		}
	}

	if (x->type_nb.notifier_call)
		__usb_phy_get_charger_type(x);

	return 0;
}

/**
 * devm_usb_get_phy - find the USB PHY
 * @dev - device that requests this phy
 * @type - the type of the phy the controller requires
 *
 * Gets the phy using usb_get_phy(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *devm_usb_get_phy(struct device *dev, enum usb_phy_type type)
{
	struct usb_phy **ptr, *phy;

	ptr = devres_alloc(devm_usb_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = usb_get_phy(type);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy);

/**
 * usb_get_phy - find the USB PHY
 * @type - the type of the phy the controller requires
 *
 * Returns the phy driver, after getting a refcount to it; or
 * -ENODEV if there is no such phy.  The caller is responsible for
 * calling usb_put_phy() to release that count.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *usb_get_phy(enum usb_phy_type type)
{
	struct usb_phy	*phy = NULL;
	unsigned long	flags;

	spin_lock_irqsave(&phy_lock, flags);

	phy = __usb_find_phy(&phy_list, type);
	if (IS_ERR(phy) || !try_module_get(phy->dev->driver->owner)) {
		pr_debug("PHY: unable to find transceiver of type %s\n",
			usb_phy_type_string(type));
		if (!IS_ERR(phy))
			phy = ERR_PTR(-ENODEV);

		goto err0;
	}

	get_device(phy->dev);

err0:
	spin_unlock_irqrestore(&phy_lock, flags);

	return phy;
}
EXPORT_SYMBOL_GPL(usb_get_phy);

/**
 * devm_usb_get_phy_by_node - find the USB PHY by device_node
 * @dev - device that requests this phy
 * @node - the device_node for the phy device.
 * @nb - a notifier_block to register with the phy.
 *
 * Returns the phy driver associated with the given device_node,
 * after getting a refcount to it, -ENODEV if there is no such phy or
 * -EPROBE_DEFER if the device is not yet loaded. While at that, it
 * also associates the device with
 * the phy using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 *
 * For use by peripheral drivers for devices related to a phy,
 * such as a charger.
 */
struct  usb_phy *devm_usb_get_phy_by_node(struct device *dev,
					  struct device_node *node,
					  struct notifier_block *nb)
{
	struct usb_phy	*phy = ERR_PTR(-ENOMEM);
	struct phy_devm	*ptr;
	unsigned long	flags;

	ptr = devres_alloc(devm_usb_phy_release2, sizeof(*ptr), GFP_KERNEL);
	if (!ptr) {
		dev_dbg(dev, "failed to allocate memory for devres\n");
		goto err0;
	}

	spin_lock_irqsave(&phy_lock, flags);

	phy = __of_usb_find_phy(node);
	if (IS_ERR(phy)) {
		devres_free(ptr);
		goto err1;
	}

	if (!try_module_get(phy->dev->driver->owner)) {
		phy = ERR_PTR(-ENODEV);
		devres_free(ptr);
		goto err1;
	}
	if (nb)
		usb_register_notifier(phy, nb);
	ptr->phy = phy;
	ptr->nb = nb;
	devres_add(dev, ptr);

	get_device(phy->dev);

err1:
	spin_unlock_irqrestore(&phy_lock, flags);

err0:

	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy_by_node);

/**
 * devm_usb_get_phy_by_phandle - find the USB PHY by phandle
 * @dev - device that requests this phy
 * @phandle - name of the property holding the phy phandle value
 * @index - the index of the phy
 *
 * Returns the phy driver associated with the given phandle value,
 * after getting a refcount to it, -ENODEV if there is no such phy or
 * -EPROBE_DEFER if there is a phandle to the phy, but the device is
 * not yet loaded. While at that, it also associates the device with
 * the phy using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *devm_usb_get_phy_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	struct device_node *node;
	struct usb_phy	*phy;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, phandle, index);
	if (!node) {
		dev_dbg(dev, "failed to get %s phandle in %pOF node\n", phandle,
			dev->of_node);
		return ERR_PTR(-ENODEV);
	}
	phy = devm_usb_get_phy_by_node(dev, node, NULL);
	of_node_put(node);
	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy_by_phandle);

/**
 * devm_usb_put_phy - release the USB PHY
 * @dev - device that wants to release this phy
 * @phy - the phy returned by devm_usb_get_phy()
 *
 * destroys the devres associated with this phy and invokes usb_put_phy
 * to release the phy.
 *
 * For use by USB host and peripheral drivers.
 */
void devm_usb_put_phy(struct device *dev, struct usb_phy *phy)
{
	int r;

	r = devres_destroy(dev, devm_usb_phy_release, devm_usb_phy_match, phy);
	dev_WARN_ONCE(dev, r, "couldn't find PHY resource\n");
}
EXPORT_SYMBOL_GPL(devm_usb_put_phy);

/**
 * usb_put_phy - release the USB PHY
 * @x: the phy returned by usb_get_phy()
 *
 * Releases a refcount the caller received from usb_get_phy().
 *
 * For use by USB host and peripheral drivers.
 */
void usb_put_phy(struct usb_phy *x)
{
	if (x) {
		struct module *owner = x->dev->driver->owner;

		put_device(x->dev);
		module_put(owner);
	}
}
EXPORT_SYMBOL_GPL(usb_put_phy);

/**
 * usb_add_phy - declare the USB PHY
 * @x: the USB phy to be used; or NULL
 * @type - the type of this PHY
 *
 * This call is exclusively for use by phy drivers, which
 * coordinate the activities of drivers for host and peripheral
 * controllers, and in some cases for VBUS current regulation.
 */
int usb_add_phy(struct usb_phy *x, enum usb_phy_type type)
{
	int		ret = 0;
	unsigned long	flags;
	struct usb_phy	*phy;

	if (x->type != USB_PHY_TYPE_UNDEFINED) {
		dev_err(x->dev, "not accepting initialized PHY %s\n", x->label);
		return -EINVAL;
	}

	usb_charger_init(x);
	ret = usb_add_extcon(x);
	if (ret)
		return ret;

	ATOMIC_INIT_NOTIFIER_HEAD(&x->notifier);

	spin_lock_irqsave(&phy_lock, flags);

	list_for_each_entry(phy, &phy_list, head) {
		if (phy->type == type) {
			ret = -EBUSY;
			dev_err(x->dev, "transceiver type %s already exists\n",
						usb_phy_type_string(type));
			goto out;
		}
	}

	x->type = type;
	list_add_tail(&x->head, &phy_list);

out:
	spin_unlock_irqrestore(&phy_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_add_phy);

/**
 * usb_add_phy_dev - declare the USB PHY
 * @x: the USB phy to be used; or NULL
 *
 * This call is exclusively for use by phy drivers, which
 * coordinate the activities of drivers for host and peripheral
 * controllers, and in some cases for VBUS current regulation.
 */
int usb_add_phy_dev(struct usb_phy *x)
{
	unsigned long flags;
	int ret;

	if (!x->dev) {
		dev_err(x->dev, "no device provided for PHY\n");
		return -EINVAL;
	}

	usb_charger_init(x);
	ret = usb_add_extcon(x);
	if (ret)
		return ret;

	ATOMIC_INIT_NOTIFIER_HEAD(&x->notifier);

	spin_lock_irqsave(&phy_lock, flags);
	list_add_tail(&x->head, &phy_list);
	spin_unlock_irqrestore(&phy_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_add_phy_dev);

/**
 * usb_remove_phy - remove the OTG PHY
 * @x: the USB OTG PHY to be removed;
 *
 * This reverts the effects of usb_add_phy
 */
void usb_remove_phy(struct usb_phy *x)
{
	unsigned long	flags;

	spin_lock_irqsave(&phy_lock, flags);
	if (x)
		list_del(&x->head);
	spin_unlock_irqrestore(&phy_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_remove_phy);

/**
 * usb_phy_set_event - set event to phy event
 * @x: the phy returned by usb_get_phy();
 *
 * This sets event to phy event
 */
void usb_phy_set_event(struct usb_phy *x, unsigned long event)
{
	x->last_event = event;
}
EXPORT_SYMBOL_GPL(usb_phy_set_event);
