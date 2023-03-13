// SPDX-License-Identifier: GPL-2.0
#include <linux/configfs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kstrtox.h>
#include <linux/nls.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget_configfs.h>
#include <linux/usb/webusb.h>
#include "configfs.h"
#include "u_f.h"
#include "u_os_desc.h"

int check_user_usb_string(const char *name,
		struct usb_gadget_strings *stringtab_dev)
{
	u16 num;
	int ret;

	ret = kstrtou16(name, 0, &num);
	if (ret)
		return ret;

	if (!usb_validate_langid(num))
		return -EINVAL;

	stringtab_dev->language = num;
	return 0;
}

#define MAX_NAME_LEN	40
#define MAX_USB_STRING_LANGS 2

static const struct usb_descriptor_header *otg_desc[2];

struct gadget_info {
	struct config_group group;
	struct config_group functions_group;
	struct config_group configs_group;
	struct config_group strings_group;
	struct config_group os_desc_group;
	struct config_group webusb_group;

	struct mutex lock;
	struct usb_gadget_strings *gstrings[MAX_USB_STRING_LANGS + 1];
	struct list_head string_list;
	struct list_head available_func;

	struct usb_composite_driver composite;
	struct usb_composite_dev cdev;
	bool use_os_desc;
	char b_vendor_code;
	char qw_sign[OS_STRING_QW_SIGN_LEN];
	bool use_webusb;
	u16 bcd_webusb_version;
	u8 b_webusb_vendor_code;
	char landing_page[WEBUSB_URL_RAW_MAX_LENGTH];

	spinlock_t spinlock;
	bool unbind;
};

static inline struct gadget_info *to_gadget_info(struct config_item *item)
{
	return container_of(to_config_group(item), struct gadget_info, group);
}

struct config_usb_cfg {
	struct config_group group;
	struct config_group strings_group;
	struct list_head string_list;
	struct usb_configuration c;
	struct list_head func_list;
	struct usb_gadget_strings *gstrings[MAX_USB_STRING_LANGS + 1];
};

static inline struct config_usb_cfg *to_config_usb_cfg(struct config_item *item)
{
	return container_of(to_config_group(item), struct config_usb_cfg,
			group);
}

static inline struct gadget_info *cfg_to_gadget_info(struct config_usb_cfg *cfg)
{
	return container_of(cfg->c.cdev, struct gadget_info, cdev);
}

struct gadget_language {
	struct usb_gadget_strings stringtab_dev;
	struct usb_string strings[USB_GADGET_FIRST_AVAIL_IDX];
	char *manufacturer;
	char *product;
	char *serialnumber;

	struct config_group group;
	struct list_head list;
	struct list_head gadget_strings;
	unsigned int nstrings;
};

struct gadget_config_name {
	struct usb_gadget_strings stringtab_dev;
	struct usb_string strings;
	char *configuration;

	struct config_group group;
	struct list_head list;
};

#define USB_MAX_STRING_WITH_NULL_LEN	(USB_MAX_STRING_LEN+1)

static int usb_string_copy(const char *s, char **s_copy)
{
	int ret;
	char *str;
	char *copy = *s_copy;
	ret = strlen(s);
	if (ret > USB_MAX_STRING_LEN)
		return -EOVERFLOW;

	if (copy) {
		str = copy;
	} else {
		str = kmalloc(USB_MAX_STRING_WITH_NULL_LEN, GFP_KERNEL);
		if (!str)
			return -ENOMEM;
	}
	strcpy(str, s);
	if (str[ret - 1] == '\n')
		str[ret - 1] = '\0';
	*s_copy = str;
	return 0;
}

#define GI_DEVICE_DESC_SIMPLE_R_u8(__name)	\
static ssize_t gadget_dev_desc_##__name##_show(struct config_item *item, \
			char *page)	\
{	\
	return sprintf(page, "0x%02x\n", \
		to_gadget_info(item)->cdev.desc.__name); \
}

#define GI_DEVICE_DESC_SIMPLE_R_u16(__name)	\
static ssize_t gadget_dev_desc_##__name##_show(struct config_item *item, \
			char *page)	\
{	\
	return sprintf(page, "0x%04x\n", \
		le16_to_cpup(&to_gadget_info(item)->cdev.desc.__name)); \
}


#define GI_DEVICE_DESC_SIMPLE_W_u8(_name)		\
static ssize_t gadget_dev_desc_##_name##_store(struct config_item *item, \
		const char *page, size_t len)		\
{							\
	u8 val;						\
	int ret;					\
	ret = kstrtou8(page, 0, &val);			\
	if (ret)					\
		return ret;				\
	to_gadget_info(item)->cdev.desc._name = val;	\
	return len;					\
}

#define GI_DEVICE_DESC_SIMPLE_W_u16(_name)	\
static ssize_t gadget_dev_desc_##_name##_store(struct config_item *item, \
		const char *page, size_t len)		\
{							\
	u16 val;					\
	int ret;					\
	ret = kstrtou16(page, 0, &val);			\
	if (ret)					\
		return ret;				\
	to_gadget_info(item)->cdev.desc._name = cpu_to_le16p(&val);	\
	return len;					\
}

#define GI_DEVICE_DESC_SIMPLE_RW(_name, _type)	\
	GI_DEVICE_DESC_SIMPLE_R_##_type(_name)	\
	GI_DEVICE_DESC_SIMPLE_W_##_type(_name)

GI_DEVICE_DESC_SIMPLE_R_u16(bcdUSB);
GI_DEVICE_DESC_SIMPLE_RW(bDeviceClass, u8);
GI_DEVICE_DESC_SIMPLE_RW(bDeviceSubClass, u8);
GI_DEVICE_DESC_SIMPLE_RW(bDeviceProtocol, u8);
GI_DEVICE_DESC_SIMPLE_RW(bMaxPacketSize0, u8);
GI_DEVICE_DESC_SIMPLE_RW(idVendor, u16);
GI_DEVICE_DESC_SIMPLE_RW(idProduct, u16);
GI_DEVICE_DESC_SIMPLE_R_u16(bcdDevice);

static ssize_t is_valid_bcd(u16 bcd_val)
{
	if ((bcd_val & 0xf) > 9)
		return -EINVAL;
	if (((bcd_val >> 4) & 0xf) > 9)
		return -EINVAL;
	if (((bcd_val >> 8) & 0xf) > 9)
		return -EINVAL;
	if (((bcd_val >> 12) & 0xf) > 9)
		return -EINVAL;
	return 0;
}

static ssize_t gadget_dev_desc_bcdDevice_store(struct config_item *item,
		const char *page, size_t len)
{
	u16 bcdDevice;
	int ret;

	ret = kstrtou16(page, 0, &bcdDevice);
	if (ret)
		return ret;
	ret = is_valid_bcd(bcdDevice);
	if (ret)
		return ret;

	to_gadget_info(item)->cdev.desc.bcdDevice = cpu_to_le16(bcdDevice);
	return len;
}

static ssize_t gadget_dev_desc_bcdUSB_store(struct config_item *item,
		const char *page, size_t len)
{
	u16 bcdUSB;
	int ret;

	ret = kstrtou16(page, 0, &bcdUSB);
	if (ret)
		return ret;
	ret = is_valid_bcd(bcdUSB);
	if (ret)
		return ret;

	to_gadget_info(item)->cdev.desc.bcdUSB = cpu_to_le16(bcdUSB);
	return len;
}

static ssize_t gadget_dev_desc_UDC_show(struct config_item *item, char *page)
{
	struct gadget_info *gi = to_gadget_info(item);
	char *udc_name;
	int ret;

	mutex_lock(&gi->lock);
	udc_name = gi->composite.gadget_driver.udc_name;
	ret = sprintf(page, "%s\n", udc_name ?: "");
	mutex_unlock(&gi->lock);

	return ret;
}

static int unregister_gadget(struct gadget_info *gi)
{
	int ret;

	if (!gi->composite.gadget_driver.udc_name)
		return -ENODEV;

	ret = usb_gadget_unregister_driver(&gi->composite.gadget_driver);
	if (ret)
		return ret;
	kfree(gi->composite.gadget_driver.udc_name);
	gi->composite.gadget_driver.udc_name = NULL;
	return 0;
}

static ssize_t gadget_dev_desc_UDC_store(struct config_item *item,
		const char *page, size_t len)
{
	struct gadget_info *gi = to_gadget_info(item);
	char *name;
	int ret;

	if (strlen(page) < len)
		return -EOVERFLOW;

	name = kstrdup(page, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	if (name[len - 1] == '\n')
		name[len - 1] = '\0';

	mutex_lock(&gi->lock);

	if (!strlen(name)) {
		ret = unregister_gadget(gi);
		if (ret)
			goto err;
		kfree(name);
	} else {
		if (gi->composite.gadget_driver.udc_name) {
			ret = -EBUSY;
			goto err;
		}
		gi->composite.gadget_driver.udc_name = name;
		ret = usb_gadget_register_driver(&gi->composite.gadget_driver);
		if (ret) {
			gi->composite.gadget_driver.udc_name = NULL;
			goto err;
		}
	}
	mutex_unlock(&gi->lock);
	return len;
err:
	kfree(name);
	mutex_unlock(&gi->lock);
	return ret;
}

static ssize_t gadget_dev_desc_max_speed_show(struct config_item *item,
					      char *page)
{
	enum usb_device_speed speed = to_gadget_info(item)->composite.max_speed;

	return sprintf(page, "%s\n", usb_speed_string(speed));
}

static ssize_t gadget_dev_desc_max_speed_store(struct config_item *item,
					       const char *page, size_t len)
{
	struct gadget_info *gi = to_gadget_info(item);

	mutex_lock(&gi->lock);

	/* Prevent changing of max_speed after the driver is binded */
	if (gi->composite.gadget_driver.udc_name)
		goto err;

	if (strncmp(page, "super-speed-plus", 16) == 0)
		gi->composite.max_speed = USB_SPEED_SUPER_PLUS;
	else if (strncmp(page, "super-speed", 11) == 0)
		gi->composite.max_speed = USB_SPEED_SUPER;
	else if (strncmp(page, "high-speed", 10) == 0)
		gi->composite.max_speed = USB_SPEED_HIGH;
	else if (strncmp(page, "full-speed", 10) == 0)
		gi->composite.max_speed = USB_SPEED_FULL;
	else if (strncmp(page, "low-speed", 9) == 0)
		gi->composite.max_speed = USB_SPEED_LOW;
	else
		goto err;

	gi->composite.gadget_driver.max_speed = gi->composite.max_speed;

	mutex_unlock(&gi->lock);
	return len;
err:
	mutex_unlock(&gi->lock);
	return -EINVAL;
}

CONFIGFS_ATTR(gadget_dev_desc_, bDeviceClass);
CONFIGFS_ATTR(gadget_dev_desc_, bDeviceSubClass);
CONFIGFS_ATTR(gadget_dev_desc_, bDeviceProtocol);
CONFIGFS_ATTR(gadget_dev_desc_, bMaxPacketSize0);
CONFIGFS_ATTR(gadget_dev_desc_, idVendor);
CONFIGFS_ATTR(gadget_dev_desc_, idProduct);
CONFIGFS_ATTR(gadget_dev_desc_, bcdDevice);
CONFIGFS_ATTR(gadget_dev_desc_, bcdUSB);
CONFIGFS_ATTR(gadget_dev_desc_, UDC);
CONFIGFS_ATTR(gadget_dev_desc_, max_speed);

static struct configfs_attribute *gadget_root_attrs[] = {
	&gadget_dev_desc_attr_bDeviceClass,
	&gadget_dev_desc_attr_bDeviceSubClass,
	&gadget_dev_desc_attr_bDeviceProtocol,
	&gadget_dev_desc_attr_bMaxPacketSize0,
	&gadget_dev_desc_attr_idVendor,
	&gadget_dev_desc_attr_idProduct,
	&gadget_dev_desc_attr_bcdDevice,
	&gadget_dev_desc_attr_bcdUSB,
	&gadget_dev_desc_attr_UDC,
	&gadget_dev_desc_attr_max_speed,
	NULL,
};

static inline struct gadget_language *to_gadget_language(struct config_item *item)
{
	return container_of(to_config_group(item), struct gadget_language,
			 group);
}

static inline struct gadget_config_name *to_gadget_config_name(
		struct config_item *item)
{
	return container_of(to_config_group(item), struct gadget_config_name,
			 group);
}

static inline struct usb_function_instance *to_usb_function_instance(
		struct config_item *item)
{
	return container_of(to_config_group(item),
			 struct usb_function_instance, group);
}

static void gadget_info_attr_release(struct config_item *item)
{
	struct gadget_info *gi = to_gadget_info(item);

	WARN_ON(!list_empty(&gi->cdev.configs));
	WARN_ON(!list_empty(&gi->string_list));
	WARN_ON(!list_empty(&gi->available_func));
	kfree(gi->composite.gadget_driver.function);
	kfree(gi->composite.gadget_driver.driver.name);
	kfree(gi);
}

static struct configfs_item_operations gadget_root_item_ops = {
	.release                = gadget_info_attr_release,
};

static void gadget_config_attr_release(struct config_item *item)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(item);

	WARN_ON(!list_empty(&cfg->c.functions));
	list_del(&cfg->c.list);
	kfree(cfg->c.label);
	kfree(cfg);
}

static int config_usb_cfg_link(
	struct config_item *usb_cfg_ci,
	struct config_item *usb_func_ci)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(usb_cfg_ci);
	struct gadget_info *gi = cfg_to_gadget_info(cfg);

	struct usb_function_instance *fi =
			to_usb_function_instance(usb_func_ci);
	struct usb_function_instance *a_fi = NULL, *iter;
	struct usb_function *f;
	int ret;

	mutex_lock(&gi->lock);
	/*
	 * Make sure this function is from within our _this_ gadget and not
	 * from another gadget or a random directory.
	 * Also a function instance can only be linked once.
	 */

	if (gi->composite.gadget_driver.udc_name) {
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(iter, &gi->available_func, cfs_list) {
		if (iter != fi)
			continue;
		a_fi = iter;
		break;
	}
	if (!a_fi) {
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(f, &cfg->func_list, list) {
		if (f->fi == fi) {
			ret = -EEXIST;
			goto out;
		}
	}

	f = usb_get_function(fi);
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		goto out;
	}

	/* stash the function until we bind it to the gadget */
	list_add_tail(&f->list, &cfg->func_list);
	ret = 0;
out:
	mutex_unlock(&gi->lock);
	return ret;
}

static void config_usb_cfg_unlink(
	struct config_item *usb_cfg_ci,
	struct config_item *usb_func_ci)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(usb_cfg_ci);
	struct gadget_info *gi = cfg_to_gadget_info(cfg);

	struct usb_function_instance *fi =
			to_usb_function_instance(usb_func_ci);
	struct usb_function *f;

	/*
	 * ideally I would like to forbid to unlink functions while a gadget is
	 * bound to an UDC. Since this isn't possible at the moment, we simply
	 * force an unbind, the function is available here and then we can
	 * remove the function.
	 */
	mutex_lock(&gi->lock);
	if (gi->composite.gadget_driver.udc_name)
		unregister_gadget(gi);
	WARN_ON(gi->composite.gadget_driver.udc_name);

	list_for_each_entry(f, &cfg->func_list, list) {
		if (f->fi == fi) {
			list_del(&f->list);
			usb_put_function(f);
			mutex_unlock(&gi->lock);
			return;
		}
	}
	mutex_unlock(&gi->lock);
	WARN(1, "Unable to locate function to unbind\n");
}

static struct configfs_item_operations gadget_config_item_ops = {
	.release                = gadget_config_attr_release,
	.allow_link             = config_usb_cfg_link,
	.drop_link              = config_usb_cfg_unlink,
};


static ssize_t gadget_config_desc_MaxPower_show(struct config_item *item,
		char *page)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(item);

	return sprintf(page, "%u\n", cfg->c.MaxPower);
}

static ssize_t gadget_config_desc_MaxPower_store(struct config_item *item,
		const char *page, size_t len)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(item);
	u16 val;
	int ret;
	ret = kstrtou16(page, 0, &val);
	if (ret)
		return ret;
	if (DIV_ROUND_UP(val, 8) > 0xff)
		return -ERANGE;
	cfg->c.MaxPower = val;
	return len;
}

static ssize_t gadget_config_desc_bmAttributes_show(struct config_item *item,
		char *page)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(item);

	return sprintf(page, "0x%02x\n", cfg->c.bmAttributes);
}

static ssize_t gadget_config_desc_bmAttributes_store(struct config_item *item,
		const char *page, size_t len)
{
	struct config_usb_cfg *cfg = to_config_usb_cfg(item);
	u8 val;
	int ret;
	ret = kstrtou8(page, 0, &val);
	if (ret)
		return ret;
	if (!(val & USB_CONFIG_ATT_ONE))
		return -EINVAL;
	if (val & ~(USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER |
				USB_CONFIG_ATT_WAKEUP))
		return -EINVAL;
	cfg->c.bmAttributes = val;
	return len;
}

CONFIGFS_ATTR(gadget_config_desc_, MaxPower);
CONFIGFS_ATTR(gadget_config_desc_, bmAttributes);

static struct configfs_attribute *gadget_config_attrs[] = {
	&gadget_config_desc_attr_MaxPower,
	&gadget_config_desc_attr_bmAttributes,
	NULL,
};

static const struct config_item_type gadget_config_type = {
	.ct_item_ops	= &gadget_config_item_ops,
	.ct_attrs	= gadget_config_attrs,
	.ct_owner	= THIS_MODULE,
};

static const struct config_item_type gadget_root_type = {
	.ct_item_ops	= &gadget_root_item_ops,
	.ct_attrs	= gadget_root_attrs,
	.ct_owner	= THIS_MODULE,
};

static void composite_init_dev(struct usb_composite_dev *cdev)
{
	spin_lock_init(&cdev->lock);
	INIT_LIST_HEAD(&cdev->configs);
	INIT_LIST_HEAD(&cdev->gstrings);
}

static struct config_group *function_make(
		struct config_group *group,
		const char *name)
{
	struct gadget_info *gi;
	struct usb_function_instance *fi;
	char buf[MAX_NAME_LEN];
	char *func_name;
	char *instance_name;
	int ret;

	ret = snprintf(buf, MAX_NAME_LEN, "%s", name);
	if (ret >= MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	func_name = buf;
	instance_name = strchr(func_name, '.');
	if (!instance_name) {
		pr_err("Unable to locate . in FUNC.INSTANCE\n");
		return ERR_PTR(-EINVAL);
	}
	*instance_name = '\0';
	instance_name++;

	fi = usb_get_function_instance(func_name);
	if (IS_ERR(fi))
		return ERR_CAST(fi);

	ret = config_item_set_name(&fi->group.cg_item, "%s", name);
	if (ret) {
		usb_put_function_instance(fi);
		return ERR_PTR(ret);
	}
	if (fi->set_inst_name) {
		ret = fi->set_inst_name(fi, instance_name);
		if (ret) {
			usb_put_function_instance(fi);
			return ERR_PTR(ret);
		}
	}

	gi = container_of(group, struct gadget_info, functions_group);

	mutex_lock(&gi->lock);
	list_add_tail(&fi->cfs_list, &gi->available_func);
	mutex_unlock(&gi->lock);
	return &fi->group;
}

static void function_drop(
		struct config_group *group,
		struct config_item *item)
{
	struct usb_function_instance *fi = to_usb_function_instance(item);
	struct gadget_info *gi;

	gi = container_of(group, struct gadget_info, functions_group);

	mutex_lock(&gi->lock);
	list_del(&fi->cfs_list);
	mutex_unlock(&gi->lock);
	config_item_put(item);
}

static struct configfs_group_operations functions_ops = {
	.make_group     = &function_make,
	.drop_item      = &function_drop,
};

static const struct config_item_type functions_type = {
	.ct_group_ops   = &functions_ops,
	.ct_owner       = THIS_MODULE,
};

GS_STRINGS_RW(gadget_config_name, configuration);

static struct configfs_attribute *gadget_config_name_langid_attrs[] = {
	&gadget_config_name_attr_configuration,
	NULL,
};

static void gadget_config_name_attr_release(struct config_item *item)
{
	struct gadget_config_name *cn = to_gadget_config_name(item);

	kfree(cn->configuration);

	list_del(&cn->list);
	kfree(cn);
}

USB_CONFIG_STRING_RW_OPS(gadget_config_name);
USB_CONFIG_STRINGS_LANG(gadget_config_name, config_usb_cfg);

static struct config_group *config_desc_make(
		struct config_group *group,
		const char *name)
{
	struct gadget_info *gi;
	struct config_usb_cfg *cfg;
	char buf[MAX_NAME_LEN];
	char *num_str;
	u8 num;
	int ret;

	gi = container_of(group, struct gadget_info, configs_group);
	ret = snprintf(buf, MAX_NAME_LEN, "%s", name);
	if (ret >= MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	num_str = strchr(buf, '.');
	if (!num_str) {
		pr_err("Unable to locate . in name.bConfigurationValue\n");
		return ERR_PTR(-EINVAL);
	}

	*num_str = '\0';
	num_str++;

	if (!strlen(buf))
		return ERR_PTR(-EINVAL);

	ret = kstrtou8(num_str, 0, &num);
	if (ret)
		return ERR_PTR(ret);

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);
	cfg->c.label = kstrdup(buf, GFP_KERNEL);
	if (!cfg->c.label) {
		ret = -ENOMEM;
		goto err;
	}
	cfg->c.bConfigurationValue = num;
	cfg->c.MaxPower = CONFIG_USB_GADGET_VBUS_DRAW;
	cfg->c.bmAttributes = USB_CONFIG_ATT_ONE;
	INIT_LIST_HEAD(&cfg->string_list);
	INIT_LIST_HEAD(&cfg->func_list);

	config_group_init_type_name(&cfg->group, name,
				&gadget_config_type);

	config_group_init_type_name(&cfg->strings_group, "strings",
			&gadget_config_name_strings_type);
	configfs_add_default_group(&cfg->strings_group, &cfg->group);

	ret = usb_add_config_only(&gi->cdev, &cfg->c);
	if (ret)
		goto err;

	return &cfg->group;
err:
	kfree(cfg->c.label);
	kfree(cfg);
	return ERR_PTR(ret);
}

static void config_desc_drop(
		struct config_group *group,
		struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations config_desc_ops = {
	.make_group     = &config_desc_make,
	.drop_item      = &config_desc_drop,
};

static const struct config_item_type config_desc_type = {
	.ct_group_ops   = &config_desc_ops,
	.ct_owner       = THIS_MODULE,
};

GS_STRINGS_RW(gadget_language, manufacturer);
GS_STRINGS_RW(gadget_language, product);
GS_STRINGS_RW(gadget_language, serialnumber);

static struct configfs_attribute *gadget_language_langid_attrs[] = {
	&gadget_language_attr_manufacturer,
	&gadget_language_attr_product,
	&gadget_language_attr_serialnumber,
	NULL,
};

static void gadget_language_attr_release(struct config_item *item)
{
	struct gadget_language *gs = to_gadget_language(item);

	kfree(gs->manufacturer);
	kfree(gs->product);
	kfree(gs->serialnumber);

	list_del(&gs->list);
	kfree(gs);
}

static struct configfs_item_operations gadget_language_langid_item_ops = {
	.release                = gadget_language_attr_release,
};

static ssize_t gadget_string_id_show(struct config_item *item, char *page)
{
	struct gadget_string *string = to_gadget_string(item);
	int ret;

	ret = sprintf(page, "%u\n", string->usb_string.id);
	return ret;
}
CONFIGFS_ATTR_RO(gadget_string_, id);

static ssize_t gadget_string_s_show(struct config_item *item, char *page)
{
	struct gadget_string *string = to_gadget_string(item);
	int ret;

	ret = snprintf(page, sizeof(string->string), "%s\n", string->string);
	return ret;
}

static ssize_t gadget_string_s_store(struct config_item *item, const char *page,
				     size_t len)
{
	struct gadget_string *string = to_gadget_string(item);
	int size = min(sizeof(string->string), len + 1);

	if (len > USB_MAX_STRING_LEN)
		return -EINVAL;

	return strscpy(string->string, page, size);
}
CONFIGFS_ATTR(gadget_string_, s);

static struct configfs_attribute *gadget_string_attrs[] = {
	&gadget_string_attr_id,
	&gadget_string_attr_s,
	NULL,
};

static void gadget_string_release(struct config_item *item)
{
	struct gadget_string *string = to_gadget_string(item);

	kfree(string);
}

static struct configfs_item_operations gadget_string_item_ops = {
	.release	= gadget_string_release,
};

static const struct config_item_type gadget_string_type = {
	.ct_item_ops	= &gadget_string_item_ops,
	.ct_attrs	= gadget_string_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *gadget_language_string_make(struct config_group *group,
						       const char *name)
{
	struct gadget_language *language;
	struct gadget_string *string;

	language = to_gadget_language(&group->cg_item);

	string = kzalloc(sizeof(*string), GFP_KERNEL);
	if (!string)
		return ERR_PTR(-ENOMEM);

	string->usb_string.id = language->nstrings++;
	string->usb_string.s = string->string;
	list_add_tail(&string->list, &language->gadget_strings);

	config_item_init_type_name(&string->item, name, &gadget_string_type);

	return &string->item;
}

static void gadget_language_string_drop(struct config_group *group,
					struct config_item *item)
{
	struct gadget_language *language;
	struct gadget_string *string;
	unsigned int i = USB_GADGET_FIRST_AVAIL_IDX;

	language = to_gadget_language(&group->cg_item);
	string = to_gadget_string(item);

	list_del(&string->list);
	language->nstrings--;

	/* Reset the ids for the language's strings to guarantee a continuous set */
	list_for_each_entry(string, &language->gadget_strings, list)
		string->usb_string.id = i++;
}

static struct configfs_group_operations gadget_language_langid_group_ops = {
	.make_item		= gadget_language_string_make,
	.drop_item		= gadget_language_string_drop,
};

static struct config_item_type gadget_language_type = {
	.ct_item_ops	= &gadget_language_langid_item_ops,
	.ct_group_ops	= &gadget_language_langid_group_ops,
	.ct_attrs	= gadget_language_langid_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *gadget_language_make(struct config_group *group,
						 const char *name)
{
	struct gadget_info *gi;
	struct gadget_language *gs;
	struct gadget_language *new;
	int langs = 0;
	int ret;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);

	ret = check_user_usb_string(name, &new->stringtab_dev);
	if (ret)
		goto err;
	config_group_init_type_name(&new->group, name,
				    &gadget_language_type);

	gi = container_of(group, struct gadget_info, strings_group);
	ret = -EEXIST;
	list_for_each_entry(gs, &gi->string_list, list) {
		if (gs->stringtab_dev.language == new->stringtab_dev.language)
			goto err;
		langs++;
	}
	ret = -EOVERFLOW;
	if (langs >= MAX_USB_STRING_LANGS)
		goto err;

	list_add_tail(&new->list, &gi->string_list);
	INIT_LIST_HEAD(&new->gadget_strings);

	/* We have the default manufacturer, product and serialnumber strings */
	new->nstrings = 3;
	return &new->group;
err:
	kfree(new);
	return ERR_PTR(ret);
}

static void gadget_language_drop(struct config_group *group,
				 struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations gadget_language_group_ops = {
	.make_group     = &gadget_language_make,
	.drop_item      = &gadget_language_drop,
};

static struct config_item_type gadget_language_strings_type = {
	.ct_group_ops   = &gadget_language_group_ops,
	.ct_owner       = THIS_MODULE,
};

static inline struct gadget_info *webusb_item_to_gadget_info(
		struct config_item *item)
{
	return container_of(to_config_group(item),
			struct gadget_info, webusb_group);
}

static ssize_t webusb_use_show(struct config_item *item, char *page)
{
	return sysfs_emit(page, "%d\n",
			webusb_item_to_gadget_info(item)->use_webusb);
}

static ssize_t webusb_use_store(struct config_item *item, const char *page,
				 size_t len)
{
	struct gadget_info *gi = webusb_item_to_gadget_info(item);
	int ret;
	bool use;

	ret = kstrtobool(page, &use);
	if (ret)
		return ret;

	mutex_lock(&gi->lock);
	gi->use_webusb = use;
	mutex_unlock(&gi->lock);

	return len;
}

static ssize_t webusb_bcdVersion_show(struct config_item *item, char *page)
{
	return sysfs_emit(page, "0x%04x\n",
					webusb_item_to_gadget_info(item)->bcd_webusb_version);
}

static ssize_t webusb_bcdVersion_store(struct config_item *item,
		const char *page, size_t len)
{
	struct gadget_info *gi = webusb_item_to_gadget_info(item);
	u16 bcdVersion;
	int ret;

	ret = kstrtou16(page, 0, &bcdVersion);
	if (ret)
		return ret;

	ret = is_valid_bcd(bcdVersion);
	if (ret)
		return ret;

	mutex_lock(&gi->lock);
	gi->bcd_webusb_version = bcdVersion;
	mutex_unlock(&gi->lock);

	return len;
}

static ssize_t webusb_bVendorCode_show(struct config_item *item, char *page)
{
	return sysfs_emit(page, "0x%02x\n",
			webusb_item_to_gadget_info(item)->b_webusb_vendor_code);
}

static ssize_t webusb_bVendorCode_store(struct config_item *item,
					   const char *page, size_t len)
{
	struct gadget_info *gi = webusb_item_to_gadget_info(item);
	int ret;
	u8 b_vendor_code;

	ret = kstrtou8(page, 0, &b_vendor_code);
	if (ret)
		return ret;

	mutex_lock(&gi->lock);
	gi->b_webusb_vendor_code = b_vendor_code;
	mutex_unlock(&gi->lock);

	return len;
}

static ssize_t webusb_landingPage_show(struct config_item *item, char *page)
{
	return sysfs_emit(page, "%s\n", webusb_item_to_gadget_info(item)->landing_page);
}

static ssize_t webusb_landingPage_store(struct config_item *item, const char *page,
				     size_t len)
{
	struct gadget_info *gi = webusb_item_to_gadget_info(item);
	unsigned int bytes_to_strip = 0;
	int l = len;

	if (page[l - 1] == '\n') {
		--l;
		++bytes_to_strip;
	}

	if (l > sizeof(gi->landing_page)) {
		pr_err("webusb: landingPage URL too long\n");
		return -EINVAL;
	}

	// validation
	if (strncasecmp(page, "https://",  8) == 0)
		bytes_to_strip = 8;
	else if (strncasecmp(page, "http://", 7) == 0)
		bytes_to_strip = 7;
	else
		bytes_to_strip = 0;

	if (l > U8_MAX - WEBUSB_URL_DESCRIPTOR_HEADER_LENGTH + bytes_to_strip) {
		pr_err("webusb: landingPage URL %d bytes too long for given URL scheme\n",
			l - U8_MAX + WEBUSB_URL_DESCRIPTOR_HEADER_LENGTH - bytes_to_strip);
		return -EINVAL;
	}

	mutex_lock(&gi->lock);
	// ensure 0 bytes are set, in case the new landing page is shorter then the old one.
	memcpy_and_pad(gi->landing_page, sizeof(gi->landing_page), page, l, 0);
	mutex_unlock(&gi->lock);

	return len;
}

CONFIGFS_ATTR(webusb_, use);
CONFIGFS_ATTR(webusb_, bVendorCode);
CONFIGFS_ATTR(webusb_, bcdVersion);
CONFIGFS_ATTR(webusb_, landingPage);

static struct configfs_attribute *webusb_attrs[] = {
	&webusb_attr_use,
	&webusb_attr_bcdVersion,
	&webusb_attr_bVendorCode,
	&webusb_attr_landingPage,
	NULL,
};

static struct config_item_type webusb_type = {
	.ct_attrs	= webusb_attrs,
	.ct_owner	= THIS_MODULE,
};

static inline struct gadget_info *os_desc_item_to_gadget_info(
		struct config_item *item)
{
	return container_of(to_config_group(item),
			struct gadget_info, os_desc_group);
}

static ssize_t os_desc_use_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n",
			os_desc_item_to_gadget_info(item)->use_os_desc);
}

static ssize_t os_desc_use_store(struct config_item *item, const char *page,
				 size_t len)
{
	struct gadget_info *gi = os_desc_item_to_gadget_info(item);
	int ret;
	bool use;

	ret = kstrtobool(page, &use);
	if (ret)
		return ret;

	mutex_lock(&gi->lock);
	gi->use_os_desc = use;
	mutex_unlock(&gi->lock);

	return len;
}

static ssize_t os_desc_b_vendor_code_show(struct config_item *item, char *page)
{
	return sprintf(page, "0x%02x\n",
			os_desc_item_to_gadget_info(item)->b_vendor_code);
}

static ssize_t os_desc_b_vendor_code_store(struct config_item *item,
					   const char *page, size_t len)
{
	struct gadget_info *gi = os_desc_item_to_gadget_info(item);
	int ret;
	u8 b_vendor_code;

	ret = kstrtou8(page, 0, &b_vendor_code);
	if (ret)
		return ret;

	mutex_lock(&gi->lock);
	gi->b_vendor_code = b_vendor_code;
	mutex_unlock(&gi->lock);

	return len;
}

static ssize_t os_desc_qw_sign_show(struct config_item *item, char *page)
{
	struct gadget_info *gi = os_desc_item_to_gadget_info(item);
	int res;

	res = utf16s_to_utf8s((wchar_t *) gi->qw_sign, OS_STRING_QW_SIGN_LEN,
			      UTF16_LITTLE_ENDIAN, page, PAGE_SIZE - 1);
	page[res++] = '\n';

	return res;
}

static ssize_t os_desc_qw_sign_store(struct config_item *item, const char *page,
				     size_t len)
{
	struct gadget_info *gi = os_desc_item_to_gadget_info(item);
	int res, l;

	l = min((int)len, OS_STRING_QW_SIGN_LEN >> 1);
	if (page[l - 1] == '\n')
		--l;

	mutex_lock(&gi->lock);
	res = utf8s_to_utf16s(page, l,
			      UTF16_LITTLE_ENDIAN, (wchar_t *) gi->qw_sign,
			      OS_STRING_QW_SIGN_LEN);
	if (res > 0)
		res = len;
	mutex_unlock(&gi->lock);

	return res;
}

CONFIGFS_ATTR(os_desc_, use);
CONFIGFS_ATTR(os_desc_, b_vendor_code);
CONFIGFS_ATTR(os_desc_, qw_sign);

static struct configfs_attribute *os_desc_attrs[] = {
	&os_desc_attr_use,
	&os_desc_attr_b_vendor_code,
	&os_desc_attr_qw_sign,
	NULL,
};

static int os_desc_link(struct config_item *os_desc_ci,
			struct config_item *usb_cfg_ci)
{
	struct gadget_info *gi = os_desc_item_to_gadget_info(os_desc_ci);
	struct usb_composite_dev *cdev = &gi->cdev;
	struct config_usb_cfg *c_target = to_config_usb_cfg(usb_cfg_ci);
	struct usb_configuration *c = NULL, *iter;
	int ret;

	mutex_lock(&gi->lock);
	list_for_each_entry(iter, &cdev->configs, list) {
		if (iter != &c_target->c)
			continue;
		c = iter;
		break;
	}
	if (!c) {
		ret = -EINVAL;
		goto out;
	}

	if (cdev->os_desc_config) {
		ret = -EBUSY;
		goto out;
	}

	cdev->os_desc_config = &c_target->c;
	ret = 0;

out:
	mutex_unlock(&gi->lock);
	return ret;
}

static void os_desc_unlink(struct config_item *os_desc_ci,
			  struct config_item *usb_cfg_ci)
{
	struct gadget_info *gi = os_desc_item_to_gadget_info(os_desc_ci);
	struct usb_composite_dev *cdev = &gi->cdev;

	mutex_lock(&gi->lock);
	if (gi->composite.gadget_driver.udc_name)
		unregister_gadget(gi);
	cdev->os_desc_config = NULL;
	WARN_ON(gi->composite.gadget_driver.udc_name);
	mutex_unlock(&gi->lock);
}

static struct configfs_item_operations os_desc_ops = {
	.allow_link		= os_desc_link,
	.drop_link		= os_desc_unlink,
};

static struct config_item_type os_desc_type = {
	.ct_item_ops	= &os_desc_ops,
	.ct_attrs	= os_desc_attrs,
	.ct_owner	= THIS_MODULE,
};

static inline struct usb_os_desc_ext_prop
*to_usb_os_desc_ext_prop(struct config_item *item)
{
	return container_of(item, struct usb_os_desc_ext_prop, item);
}

static ssize_t ext_prop_type_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", to_usb_os_desc_ext_prop(item)->type);
}

static ssize_t ext_prop_type_store(struct config_item *item,
				   const char *page, size_t len)
{
	struct usb_os_desc_ext_prop *ext_prop = to_usb_os_desc_ext_prop(item);
	struct usb_os_desc *desc = to_usb_os_desc(ext_prop->item.ci_parent);
	u8 type;
	int ret;

	ret = kstrtou8(page, 0, &type);
	if (ret)
		return ret;

	if (type < USB_EXT_PROP_UNICODE || type > USB_EXT_PROP_UNICODE_MULTI)
		return -EINVAL;

	if (desc->opts_mutex)
		mutex_lock(desc->opts_mutex);

	if ((ext_prop->type == USB_EXT_PROP_BINARY ||
	    ext_prop->type == USB_EXT_PROP_LE32 ||
	    ext_prop->type == USB_EXT_PROP_BE32) &&
	    (type == USB_EXT_PROP_UNICODE ||
	    type == USB_EXT_PROP_UNICODE_ENV ||
	    type == USB_EXT_PROP_UNICODE_LINK))
		ext_prop->data_len <<= 1;
	else if ((ext_prop->type == USB_EXT_PROP_UNICODE ||
		   ext_prop->type == USB_EXT_PROP_UNICODE_ENV ||
		   ext_prop->type == USB_EXT_PROP_UNICODE_LINK) &&
		   (type == USB_EXT_PROP_BINARY ||
		   type == USB_EXT_PROP_LE32 ||
		   type == USB_EXT_PROP_BE32))
		ext_prop->data_len >>= 1;
	ext_prop->type = type;

	if (desc->opts_mutex)
		mutex_unlock(desc->opts_mutex);
	return len;
}

static ssize_t ext_prop_data_show(struct config_item *item, char *page)
{
	struct usb_os_desc_ext_prop *ext_prop = to_usb_os_desc_ext_prop(item);
	int len = ext_prop->data_len;

	if (ext_prop->type == USB_EXT_PROP_UNICODE ||
	    ext_prop->type == USB_EXT_PROP_UNICODE_ENV ||
	    ext_prop->type == USB_EXT_PROP_UNICODE_LINK)
		len >>= 1;
	memcpy(page, ext_prop->data, len);

	return len;
}

static ssize_t ext_prop_data_store(struct config_item *item,
				   const char *page, size_t len)
{
	struct usb_os_desc_ext_prop *ext_prop = to_usb_os_desc_ext_prop(item);
	struct usb_os_desc *desc = to_usb_os_desc(ext_prop->item.ci_parent);
	char *new_data;
	size_t ret_len = len;

	if (page[len - 1] == '\n' || page[len - 1] == '\0')
		--len;
	new_data = kmemdup(page, len, GFP_KERNEL);
	if (!new_data)
		return -ENOMEM;

	if (desc->opts_mutex)
		mutex_lock(desc->opts_mutex);
	kfree(ext_prop->data);
	ext_prop->data = new_data;
	desc->ext_prop_len -= ext_prop->data_len;
	ext_prop->data_len = len;
	desc->ext_prop_len += ext_prop->data_len;
	if (ext_prop->type == USB_EXT_PROP_UNICODE ||
	    ext_prop->type == USB_EXT_PROP_UNICODE_ENV ||
	    ext_prop->type == USB_EXT_PROP_UNICODE_LINK) {
		desc->ext_prop_len -= ext_prop->data_len;
		ext_prop->data_len <<= 1;
		ext_prop->data_len += 2;
		desc->ext_prop_len += ext_prop->data_len;
	}
	if (desc->opts_mutex)
		mutex_unlock(desc->opts_mutex);
	return ret_len;
}

CONFIGFS_ATTR(ext_prop_, type);
CONFIGFS_ATTR(ext_prop_, data);

static struct configfs_attribute *ext_prop_attrs[] = {
	&ext_prop_attr_type,
	&ext_prop_attr_data,
	NULL,
};

static void usb_os_desc_ext_prop_release(struct config_item *item)
{
	struct usb_os_desc_ext_prop *ext_prop = to_usb_os_desc_ext_prop(item);

	kfree(ext_prop); /* frees a whole chunk */
}

static struct configfs_item_operations ext_prop_ops = {
	.release		= usb_os_desc_ext_prop_release,
};

static struct config_item *ext_prop_make(
		struct config_group *group,
		const char *name)
{
	struct usb_os_desc_ext_prop *ext_prop;
	struct config_item_type *ext_prop_type;
	struct usb_os_desc *desc;
	char *vlabuf;

	vla_group(data_chunk);
	vla_item(data_chunk, struct usb_os_desc_ext_prop, ext_prop, 1);
	vla_item(data_chunk, struct config_item_type, ext_prop_type, 1);

	vlabuf = kzalloc(vla_group_size(data_chunk), GFP_KERNEL);
	if (!vlabuf)
		return ERR_PTR(-ENOMEM);

	ext_prop = vla_ptr(vlabuf, data_chunk, ext_prop);
	ext_prop_type = vla_ptr(vlabuf, data_chunk, ext_prop_type);

	desc = container_of(group, struct usb_os_desc, group);
	ext_prop_type->ct_item_ops = &ext_prop_ops;
	ext_prop_type->ct_attrs = ext_prop_attrs;
	ext_prop_type->ct_owner = desc->owner;

	config_item_init_type_name(&ext_prop->item, name, ext_prop_type);

	ext_prop->name = kstrdup(name, GFP_KERNEL);
	if (!ext_prop->name) {
		kfree(vlabuf);
		return ERR_PTR(-ENOMEM);
	}
	desc->ext_prop_len += 14;
	ext_prop->name_len = 2 * strlen(ext_prop->name) + 2;
	if (desc->opts_mutex)
		mutex_lock(desc->opts_mutex);
	desc->ext_prop_len += ext_prop->name_len;
	list_add_tail(&ext_prop->entry, &desc->ext_prop);
	++desc->ext_prop_count;
	if (desc->opts_mutex)
		mutex_unlock(desc->opts_mutex);

	return &ext_prop->item;
}

static void ext_prop_drop(struct config_group *group, struct config_item *item)
{
	struct usb_os_desc_ext_prop *ext_prop = to_usb_os_desc_ext_prop(item);
	struct usb_os_desc *desc = to_usb_os_desc(&group->cg_item);

	if (desc->opts_mutex)
		mutex_lock(desc->opts_mutex);
	list_del(&ext_prop->entry);
	--desc->ext_prop_count;
	kfree(ext_prop->name);
	desc->ext_prop_len -= (ext_prop->name_len + ext_prop->data_len + 14);
	if (desc->opts_mutex)
		mutex_unlock(desc->opts_mutex);
	config_item_put(item);
}

static struct configfs_group_operations interf_grp_ops = {
	.make_item	= &ext_prop_make,
	.drop_item	= &ext_prop_drop,
};

static ssize_t interf_grp_compatible_id_show(struct config_item *item,
					     char *page)
{
	memcpy(page, to_usb_os_desc(item)->ext_compat_id, 8);
	return 8;
}

static ssize_t interf_grp_compatible_id_store(struct config_item *item,
					      const char *page, size_t len)
{
	struct usb_os_desc *desc = to_usb_os_desc(item);
	int l;

	l = min_t(int, 8, len);
	if (page[l - 1] == '\n')
		--l;
	if (desc->opts_mutex)
		mutex_lock(desc->opts_mutex);
	memcpy(desc->ext_compat_id, page, l);

	if (desc->opts_mutex)
		mutex_unlock(desc->opts_mutex);

	return len;
}

static ssize_t interf_grp_sub_compatible_id_show(struct config_item *item,
						 char *page)
{
	memcpy(page, to_usb_os_desc(item)->ext_compat_id + 8, 8);
	return 8;
}

static ssize_t interf_grp_sub_compatible_id_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct usb_os_desc *desc = to_usb_os_desc(item);
	int l;

	l = min_t(int, 8, len);
	if (page[l - 1] == '\n')
		--l;
	if (desc->opts_mutex)
		mutex_lock(desc->opts_mutex);
	memcpy(desc->ext_compat_id + 8, page, l);

	if (desc->opts_mutex)
		mutex_unlock(desc->opts_mutex);

	return len;
}

CONFIGFS_ATTR(interf_grp_, compatible_id);
CONFIGFS_ATTR(interf_grp_, sub_compatible_id);

static struct configfs_attribute *interf_grp_attrs[] = {
	&interf_grp_attr_compatible_id,
	&interf_grp_attr_sub_compatible_id,
	NULL
};

struct config_group *usb_os_desc_prepare_interf_dir(
		struct config_group *parent,
		int n_interf,
		struct usb_os_desc **desc,
		char **names,
		struct module *owner)
{
	struct config_group *os_desc_group;
	struct config_item_type *os_desc_type, *interface_type;

	vla_group(data_chunk);
	vla_item(data_chunk, struct config_group, os_desc_group, 1);
	vla_item(data_chunk, struct config_item_type, os_desc_type, 1);
	vla_item(data_chunk, struct config_item_type, interface_type, 1);

	char *vlabuf = kzalloc(vla_group_size(data_chunk), GFP_KERNEL);
	if (!vlabuf)
		return ERR_PTR(-ENOMEM);

	os_desc_group = vla_ptr(vlabuf, data_chunk, os_desc_group);
	os_desc_type = vla_ptr(vlabuf, data_chunk, os_desc_type);
	interface_type = vla_ptr(vlabuf, data_chunk, interface_type);

	os_desc_type->ct_owner = owner;
	config_group_init_type_name(os_desc_group, "os_desc", os_desc_type);
	configfs_add_default_group(os_desc_group, parent);

	interface_type->ct_group_ops = &interf_grp_ops;
	interface_type->ct_attrs = interf_grp_attrs;
	interface_type->ct_owner = owner;

	while (n_interf--) {
		struct usb_os_desc *d;

		d = desc[n_interf];
		d->owner = owner;
		config_group_init_type_name(&d->group, "", interface_type);
		config_item_set_name(&d->group.cg_item, "interface.%s",
				     names[n_interf]);
		configfs_add_default_group(&d->group, os_desc_group);
	}

	return os_desc_group;
}
EXPORT_SYMBOL(usb_os_desc_prepare_interf_dir);

static int configfs_do_nothing(struct usb_composite_dev *cdev)
{
	WARN_ON(1);
	return -EINVAL;
}

int composite_dev_prepare(struct usb_composite_driver *composite,
		struct usb_composite_dev *dev);

int composite_os_desc_req_prepare(struct usb_composite_dev *cdev,
				  struct usb_ep *ep0);

static void purge_configs_funcs(struct gadget_info *gi)
{
	struct usb_configuration	*c;

	list_for_each_entry(c, &gi->cdev.configs, list) {
		struct usb_function *f, *tmp;
		struct config_usb_cfg *cfg;

		cfg = container_of(c, struct config_usb_cfg, c);

		list_for_each_entry_safe_reverse(f, tmp, &c->functions, list) {

			list_move(&f->list, &cfg->func_list);
			if (f->unbind) {
				dev_dbg(&gi->cdev.gadget->dev,
					"unbind function '%s'/%p\n",
					f->name, f);
				f->unbind(c, f);
			}
		}
		c->next_interface_id = 0;
		memset(c->interface, 0, sizeof(c->interface));
		c->superspeed_plus = 0;
		c->superspeed = 0;
		c->highspeed = 0;
		c->fullspeed = 0;
	}
}

static struct usb_string *
configfs_attach_gadget_strings(struct gadget_info *gi)
{
	struct usb_gadget_strings **gadget_strings;
	struct gadget_language *language;
	struct gadget_string *string;
	unsigned int nlangs = 0;
	struct list_head *iter;
	struct usb_string *us;
	unsigned int i = 0;
	int nstrings = -1;
	unsigned int j;

	list_for_each(iter, &gi->string_list)
		nlangs++;

	/* Bail out early if no languages are configured */
	if (!nlangs)
		return NULL;

	gadget_strings = kcalloc(nlangs + 1, /* including NULL terminator */
				 sizeof(struct usb_gadget_strings *), GFP_KERNEL);
	if (!gadget_strings)
		return ERR_PTR(-ENOMEM);

	list_for_each_entry(language, &gi->string_list, list) {
		struct usb_string *stringtab;

		if (nstrings == -1) {
			nstrings = language->nstrings;
		} else if (nstrings != language->nstrings) {
			pr_err("languages must contain the same number of strings\n");
			us = ERR_PTR(-EINVAL);
			goto cleanup;
		}

		stringtab = kcalloc(language->nstrings + 1, sizeof(struct usb_string),
				    GFP_KERNEL);
		if (!stringtab) {
			us = ERR_PTR(-ENOMEM);
			goto cleanup;
		}

		stringtab[USB_GADGET_MANUFACTURER_IDX].id = USB_GADGET_MANUFACTURER_IDX;
		stringtab[USB_GADGET_MANUFACTURER_IDX].s = language->manufacturer;
		stringtab[USB_GADGET_PRODUCT_IDX].id = USB_GADGET_PRODUCT_IDX;
		stringtab[USB_GADGET_PRODUCT_IDX].s = language->product;
		stringtab[USB_GADGET_SERIAL_IDX].id = USB_GADGET_SERIAL_IDX;
		stringtab[USB_GADGET_SERIAL_IDX].s = language->serialnumber;

		j = USB_GADGET_FIRST_AVAIL_IDX;
		list_for_each_entry(string, &language->gadget_strings, list) {
			memcpy(&stringtab[j], &string->usb_string, sizeof(struct usb_string));
			j++;
		}

		language->stringtab_dev.strings = stringtab;
		gadget_strings[i] = &language->stringtab_dev;
		i++;
	}

	us = usb_gstrings_attach(&gi->cdev, gadget_strings, nstrings);

cleanup:
	list_for_each_entry(language, &gi->string_list, list) {
		kfree(language->stringtab_dev.strings);
		language->stringtab_dev.strings = NULL;
	}

	kfree(gadget_strings);

	return us;
}

static int configfs_composite_bind(struct usb_gadget *gadget,
		struct usb_gadget_driver *gdriver)
{
	struct usb_composite_driver     *composite = to_cdriver(gdriver);
	struct gadget_info		*gi = container_of(composite,
						struct gadget_info, composite);
	struct usb_composite_dev	*cdev = &gi->cdev;
	struct usb_configuration	*c;
	struct usb_string		*s;
	unsigned			i;
	int				ret;

	/* the gi->lock is hold by the caller */
	gi->unbind = 0;
	cdev->gadget = gadget;
	set_gadget_data(gadget, cdev);
	ret = composite_dev_prepare(composite, cdev);
	if (ret)
		return ret;
	/* and now the gadget bind */
	ret = -EINVAL;

	if (list_empty(&gi->cdev.configs)) {
		pr_err("Need at least one configuration in %s.\n",
				gi->composite.name);
		goto err_comp_cleanup;
	}


	list_for_each_entry(c, &gi->cdev.configs, list) {
		struct config_usb_cfg *cfg;

		cfg = container_of(c, struct config_usb_cfg, c);
		if (list_empty(&cfg->func_list)) {
			pr_err("Config %s/%d of %s needs at least one function.\n",
			      c->label, c->bConfigurationValue,
			      gi->composite.name);
			goto err_comp_cleanup;
		}
	}

	/* init all strings */
	if (!list_empty(&gi->string_list)) {
		s = configfs_attach_gadget_strings(gi);
		if (IS_ERR(s)) {
			ret = PTR_ERR(s);
			goto err_comp_cleanup;
		}

		gi->cdev.desc.iManufacturer = s[USB_GADGET_MANUFACTURER_IDX].id;
		gi->cdev.desc.iProduct = s[USB_GADGET_PRODUCT_IDX].id;
		gi->cdev.desc.iSerialNumber = s[USB_GADGET_SERIAL_IDX].id;

		gi->cdev.usb_strings = s;
	}

	if (gi->use_webusb) {
		cdev->use_webusb = true;
		cdev->bcd_webusb_version = gi->bcd_webusb_version;
		cdev->b_webusb_vendor_code = gi->b_webusb_vendor_code;
		memcpy(cdev->landing_page, gi->landing_page, WEBUSB_URL_RAW_MAX_LENGTH);
	}

	if (gi->use_os_desc) {
		cdev->use_os_string = true;
		cdev->b_vendor_code = gi->b_vendor_code;
		memcpy(cdev->qw_sign, gi->qw_sign, OS_STRING_QW_SIGN_LEN);
	}

	if (gadget_is_otg(gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(gadget);
		if (!usb_desc) {
			ret = -ENOMEM;
			goto err_comp_cleanup;
		}
		usb_otg_descriptor_init(gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	/* Go through all configs, attach all functions */
	list_for_each_entry(c, &gi->cdev.configs, list) {
		struct config_usb_cfg *cfg;
		struct usb_function *f;
		struct usb_function *tmp;
		struct gadget_config_name *cn;

		if (gadget_is_otg(gadget))
			c->descriptors = otg_desc;

		cfg = container_of(c, struct config_usb_cfg, c);
		if (!list_empty(&cfg->string_list)) {
			i = 0;
			list_for_each_entry(cn, &cfg->string_list, list) {
				cfg->gstrings[i] = &cn->stringtab_dev;
				cn->stringtab_dev.strings = &cn->strings;
				cn->strings.s = cn->configuration;
				i++;
			}
			cfg->gstrings[i] = NULL;
			s = usb_gstrings_attach(&gi->cdev, cfg->gstrings, 1);
			if (IS_ERR(s)) {
				ret = PTR_ERR(s);
				goto err_comp_cleanup;
			}
			c->iConfiguration = s[0].id;
		}

		list_for_each_entry_safe(f, tmp, &cfg->func_list, list) {
			list_del(&f->list);
			ret = usb_add_function(c, f);
			if (ret) {
				list_add(&f->list, &cfg->func_list);
				goto err_purge_funcs;
			}
		}
		ret = usb_gadget_check_config(cdev->gadget);
		if (ret)
			goto err_purge_funcs;

		usb_ep_autoconfig_reset(cdev->gadget);
	}
	if (cdev->use_os_string) {
		ret = composite_os_desc_req_prepare(cdev, gadget->ep0);
		if (ret)
			goto err_purge_funcs;
	}

	usb_ep_autoconfig_reset(cdev->gadget);
	return 0;

err_purge_funcs:
	purge_configs_funcs(gi);
err_comp_cleanup:
	composite_dev_cleanup(cdev);
	return ret;
}

static void configfs_composite_unbind(struct usb_gadget *gadget)
{
	struct usb_composite_dev	*cdev;
	struct gadget_info		*gi;
	unsigned long flags;

	/* the gi->lock is hold by the caller */

	cdev = get_gadget_data(gadget);
	gi = container_of(cdev, struct gadget_info, cdev);
	spin_lock_irqsave(&gi->spinlock, flags);
	gi->unbind = 1;
	spin_unlock_irqrestore(&gi->spinlock, flags);

	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
	purge_configs_funcs(gi);
	composite_dev_cleanup(cdev);
	usb_ep_autoconfig_reset(cdev->gadget);
	spin_lock_irqsave(&gi->spinlock, flags);
	cdev->gadget = NULL;
	cdev->deactivations = 0;
	gadget->deactivated = false;
	set_gadget_data(gadget, NULL);
	spin_unlock_irqrestore(&gi->spinlock, flags);
}

static int configfs_composite_setup(struct usb_gadget *gadget,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev;
	struct gadget_info *gi;
	unsigned long flags;
	int ret;

	cdev = get_gadget_data(gadget);
	if (!cdev)
		return 0;

	gi = container_of(cdev, struct gadget_info, cdev);
	spin_lock_irqsave(&gi->spinlock, flags);
	cdev = get_gadget_data(gadget);
	if (!cdev || gi->unbind) {
		spin_unlock_irqrestore(&gi->spinlock, flags);
		return 0;
	}

	ret = composite_setup(gadget, ctrl);
	spin_unlock_irqrestore(&gi->spinlock, flags);
	return ret;
}

static void configfs_composite_disconnect(struct usb_gadget *gadget)
{
	struct usb_composite_dev *cdev;
	struct gadget_info *gi;
	unsigned long flags;

	cdev = get_gadget_data(gadget);
	if (!cdev)
		return;

	gi = container_of(cdev, struct gadget_info, cdev);
	spin_lock_irqsave(&gi->spinlock, flags);
	cdev = get_gadget_data(gadget);
	if (!cdev || gi->unbind) {
		spin_unlock_irqrestore(&gi->spinlock, flags);
		return;
	}

	composite_disconnect(gadget);
	spin_unlock_irqrestore(&gi->spinlock, flags);
}

static void configfs_composite_reset(struct usb_gadget *gadget)
{
	struct usb_composite_dev *cdev;
	struct gadget_info *gi;
	unsigned long flags;

	cdev = get_gadget_data(gadget);
	if (!cdev)
		return;

	gi = container_of(cdev, struct gadget_info, cdev);
	spin_lock_irqsave(&gi->spinlock, flags);
	cdev = get_gadget_data(gadget);
	if (!cdev || gi->unbind) {
		spin_unlock_irqrestore(&gi->spinlock, flags);
		return;
	}

	composite_reset(gadget);
	spin_unlock_irqrestore(&gi->spinlock, flags);
}

static void configfs_composite_suspend(struct usb_gadget *gadget)
{
	struct usb_composite_dev *cdev;
	struct gadget_info *gi;
	unsigned long flags;

	cdev = get_gadget_data(gadget);
	if (!cdev)
		return;

	gi = container_of(cdev, struct gadget_info, cdev);
	spin_lock_irqsave(&gi->spinlock, flags);
	cdev = get_gadget_data(gadget);
	if (!cdev || gi->unbind) {
		spin_unlock_irqrestore(&gi->spinlock, flags);
		return;
	}

	composite_suspend(gadget);
	spin_unlock_irqrestore(&gi->spinlock, flags);
}

static void configfs_composite_resume(struct usb_gadget *gadget)
{
	struct usb_composite_dev *cdev;
	struct gadget_info *gi;
	unsigned long flags;

	cdev = get_gadget_data(gadget);
	if (!cdev)
		return;

	gi = container_of(cdev, struct gadget_info, cdev);
	spin_lock_irqsave(&gi->spinlock, flags);
	cdev = get_gadget_data(gadget);
	if (!cdev || gi->unbind) {
		spin_unlock_irqrestore(&gi->spinlock, flags);
		return;
	}

	composite_resume(gadget);
	spin_unlock_irqrestore(&gi->spinlock, flags);
}

static const struct usb_gadget_driver configfs_driver_template = {
	.bind           = configfs_composite_bind,
	.unbind         = configfs_composite_unbind,

	.setup          = configfs_composite_setup,
	.reset          = configfs_composite_reset,
	.disconnect     = configfs_composite_disconnect,

	.suspend	= configfs_composite_suspend,
	.resume		= configfs_composite_resume,

	.max_speed	= USB_SPEED_SUPER_PLUS,
	.driver = {
		.owner          = THIS_MODULE,
	},
	.match_existing_only = 1,
};

static struct config_group *gadgets_make(
		struct config_group *group,
		const char *name)
{
	struct gadget_info *gi;

	gi = kzalloc(sizeof(*gi), GFP_KERNEL);
	if (!gi)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&gi->group, name, &gadget_root_type);

	config_group_init_type_name(&gi->functions_group, "functions",
			&functions_type);
	configfs_add_default_group(&gi->functions_group, &gi->group);

	config_group_init_type_name(&gi->configs_group, "configs",
			&config_desc_type);
	configfs_add_default_group(&gi->configs_group, &gi->group);

	config_group_init_type_name(&gi->strings_group, "strings",
			&gadget_language_strings_type);
	configfs_add_default_group(&gi->strings_group, &gi->group);

	config_group_init_type_name(&gi->os_desc_group, "os_desc",
			&os_desc_type);
	configfs_add_default_group(&gi->os_desc_group, &gi->group);

	config_group_init_type_name(&gi->webusb_group, "webusb",
			&webusb_type);
	configfs_add_default_group(&gi->webusb_group, &gi->group);

	gi->composite.bind = configfs_do_nothing;
	gi->composite.unbind = configfs_do_nothing;
	gi->composite.suspend = NULL;
	gi->composite.resume = NULL;
	gi->composite.max_speed = USB_SPEED_SUPER_PLUS;

	spin_lock_init(&gi->spinlock);
	mutex_init(&gi->lock);
	INIT_LIST_HEAD(&gi->string_list);
	INIT_LIST_HEAD(&gi->available_func);

	composite_init_dev(&gi->cdev);
	gi->cdev.desc.bLength = USB_DT_DEVICE_SIZE;
	gi->cdev.desc.bDescriptorType = USB_DT_DEVICE;
	gi->cdev.desc.bcdDevice = cpu_to_le16(get_default_bcdDevice());

	gi->composite.gadget_driver = configfs_driver_template;

	gi->composite.gadget_driver.driver.name = kasprintf(GFP_KERNEL,
							    "configfs-gadget.%s", name);
	if (!gi->composite.gadget_driver.driver.name)
		goto err;

	gi->composite.gadget_driver.function = kstrdup(name, GFP_KERNEL);
	gi->composite.name = gi->composite.gadget_driver.function;

	if (!gi->composite.gadget_driver.function)
		goto out_free_driver_name;

	return &gi->group;

out_free_driver_name:
	kfree(gi->composite.gadget_driver.driver.name);
err:
	kfree(gi);
	return ERR_PTR(-ENOMEM);
}

static void gadgets_drop(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations gadgets_ops = {
	.make_group     = &gadgets_make,
	.drop_item      = &gadgets_drop,
};

static const struct config_item_type gadgets_type = {
	.ct_group_ops   = &gadgets_ops,
	.ct_owner       = THIS_MODULE,
};

static struct configfs_subsystem gadget_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "usb_gadget",
			.ci_type = &gadgets_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(gadget_subsys.su_mutex),
};

void unregister_gadget_item(struct config_item *item)
{
	struct gadget_info *gi = to_gadget_info(item);

	mutex_lock(&gi->lock);
	unregister_gadget(gi);
	mutex_unlock(&gi->lock);
}
EXPORT_SYMBOL_GPL(unregister_gadget_item);

static int __init gadget_cfs_init(void)
{
	int ret;

	config_group_init(&gadget_subsys.su_group);

	ret = configfs_register_subsystem(&gadget_subsys);
	return ret;
}
module_init(gadget_cfs_init);

static void __exit gadget_cfs_exit(void)
{
	configfs_unregister_subsystem(&gadget_subsys);
}
module_exit(gadget_cfs_exit);
