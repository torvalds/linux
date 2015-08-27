/*
 * class-dual-role.c
 *
 * Copyright (C) 2015 Google, Inc.
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

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/usb/class-dual-role.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/types.h>

#define DUAL_ROLE_NOTIFICATION_TIMEOUT 2000

static ssize_t dual_role_store_property(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);
static ssize_t dual_role_show_property(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);

#define DUAL_ROLE_ATTR(_name)				\
{							\
	.attr = { .name = #_name },			\
	.show = dual_role_show_property,		\
	.store = dual_role_store_property,		\
}

static struct device_attribute dual_role_attrs[] = {
	DUAL_ROLE_ATTR(supported_modes),
	DUAL_ROLE_ATTR(mode),
	DUAL_ROLE_ATTR(power_role),
	DUAL_ROLE_ATTR(data_role),
	DUAL_ROLE_ATTR(powers_vconn),
};

struct class *dual_role_class;
EXPORT_SYMBOL_GPL(dual_role_class);

static struct device_type dual_role_dev_type;

static char *kstrdupcase(const char *str, gfp_t gfp, bool to_upper)
{
	char *ret, *ustr;

	ustr = ret = kmalloc(strlen(str) + 1, gfp);

	if (!ret)
		return NULL;

	while (*str)
		*ustr++ = to_upper ? toupper(*str++) : tolower(*str++);

	*ustr = 0;

	return ret;
}

static void dual_role_changed_work(struct work_struct *work)
{
	struct dual_role_phy_instance *dual_role =
	    container_of(work, struct dual_role_phy_instance,
			 changed_work);

	dev_dbg(&dual_role->dev, "%s\n", __func__);
	kobject_uevent(&dual_role->dev.kobj, KOBJ_CHANGE);
}

void dual_role_instance_changed(struct dual_role_phy_instance *dual_role)
{
	dev_dbg(&dual_role->dev, "%s\n", __func__);
	pm_wakeup_event(&dual_role->dev, DUAL_ROLE_NOTIFICATION_TIMEOUT);
	schedule_work(&dual_role->changed_work);
}
EXPORT_SYMBOL_GPL(dual_role_instance_changed)

int dual_role_get_property(struct dual_role_phy_instance *dual_role,
			   enum dual_role_property prop,
			   unsigned int *val)
{
	return dual_role->desc->get_property(dual_role, prop, val);
}
EXPORT_SYMBOL_GPL(dual_role_get_property);

int dual_role_set_property(struct dual_role_phy_instance *dual_role,
			   enum dual_role_property prop,
			   const unsigned int *val)
{
	if (!dual_role->desc->set_property)
		return -ENODEV;

	return dual_role->desc->set_property(dual_role, prop, val);
}
EXPORT_SYMBOL_GPL(dual_role_set_property);

int dual_role_property_is_writeable(struct dual_role_phy_instance *dual_role,
				    enum dual_role_property prop)
{
	if (!dual_role->desc->property_is_writeable)
		return -ENODEV;

	return dual_role->desc->property_is_writeable(dual_role, prop);
}
EXPORT_SYMBOL_GPL(dual_role_property_is_writeable);

static void dual_role_dev_release(struct device *dev)
{
	struct dual_role_phy_instance *dual_role =
	    container_of(dev, struct dual_role_phy_instance, dev);
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dual_role);
}

static struct dual_role_phy_instance *__must_check
__dual_role_register(struct device *parent,
		     const struct dual_role_phy_desc *desc)
{
	struct device *dev;
	struct dual_role_phy_instance *dual_role;
	int rc;

	dual_role = kzalloc(sizeof(*dual_role), GFP_KERNEL);
	if (!dual_role)
		return ERR_PTR(-ENOMEM);

	dev = &dual_role->dev;

	device_initialize(dev);

	dev->class = dual_role_class;
	dev->type = &dual_role_dev_type;
	dev->parent = parent;
	dev->release = dual_role_dev_release;
	dev_set_drvdata(dev, dual_role);
	dual_role->desc = desc;

	rc = dev_set_name(dev, "%s", desc->name);
	if (rc)
		goto dev_set_name_failed;

	INIT_WORK(&dual_role->changed_work, dual_role_changed_work);

	rc = device_init_wakeup(dev, true);
	if (rc)
		goto wakeup_init_failed;

	rc = device_add(dev);
	if (rc)
		goto device_add_failed;

	dual_role_instance_changed(dual_role);

	return dual_role;

device_add_failed:
	device_init_wakeup(dev, false);
wakeup_init_failed:
dev_set_name_failed:
	put_device(dev);
	kfree(dual_role);

	return ERR_PTR(rc);
}

static void dual_role_instance_unregister(struct dual_role_phy_instance
					  *dual_role)
{
	cancel_work_sync(&dual_role->changed_work);
	device_init_wakeup(&dual_role->dev, false);
	device_unregister(&dual_role->dev);
}

static void devm_dual_role_release(struct device *dev, void *res)
{
	struct dual_role_phy_instance **dual_role = res;

	dual_role_instance_unregister(*dual_role);
}

struct dual_role_phy_instance *__must_check
devm_dual_role_instance_register(struct device *parent,
				 const struct dual_role_phy_desc *desc)
{
	struct dual_role_phy_instance **ptr, *dual_role;

	ptr = devres_alloc(devm_dual_role_release, sizeof(*ptr), GFP_KERNEL);

	if (!ptr)
		return ERR_PTR(-ENOMEM);
	dual_role = __dual_role_register(parent, desc);
	if (IS_ERR(dual_role)) {
		devres_free(ptr);
	} else {
		*ptr = dual_role;
		devres_add(parent, ptr);
	}
	return dual_role;
}
EXPORT_SYMBOL_GPL(devm_dual_role_instance_register);

static int devm_dual_role_match(struct device *dev, void *res, void *data)
{
	struct dual_role_phy_instance **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

void devm_dual_role_instance_unregister(struct device *dev,
					struct dual_role_phy_instance
					*dual_role)
{
	int rc;

	rc = devres_release(dev, devm_dual_role_release,
			    devm_dual_role_match, dual_role);
	WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_dual_role_instance_unregister);

void *dual_role_get_drvdata(struct dual_role_phy_instance *dual_role)
{
	return dual_role->drv_data;
}
EXPORT_SYMBOL_GPL(dual_role_get_drvdata);

/***************** Device attribute functions **************************/

/* port type */
static char *supported_modes_text[] = {
	"ufp dfp", "dfp", "ufp"
};

/* current mode */
static char *mode_text[] = {
	"ufp", "dfp", "none"
};

/* Power role */
static char *pr_text[] = {
	"source", "sink", "none"
};

/* Data role */
static char *dr_text[] = {
	"host", "device", "none"
};

/* Vconn supply */
static char *vconn_supply_text[] = {
	"n", "y"
};

static ssize_t dual_role_show_property(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct dual_role_phy_instance *dual_role = dev_get_drvdata(dev);
	const ptrdiff_t off = attr - dual_role_attrs;
	unsigned int value;

	if (off == DUAL_ROLE_PROP_SUPPORTED_MODES) {
		value = dual_role->desc->supported_modes;
	} else {
		ret = dual_role_get_property(dual_role, off, &value);

		if (ret < 0) {
			if (ret == -ENODATA)
				dev_dbg(dev,
					"driver has no data for `%s' property\n",
					attr->attr.name);
			else if (ret != -ENODEV)
				dev_err(dev,
					"driver failed to report `%s' property: %zd\n",
					attr->attr.name, ret);
			return ret;
		}
	}

	if (off == DUAL_ROLE_PROP_SUPPORTED_MODES) {
		BUILD_BUG_ON(DUAL_ROLE_PROP_SUPPORTED_MODES_TOTAL !=
			ARRAY_SIZE(supported_modes_text));
		if (value < DUAL_ROLE_PROP_SUPPORTED_MODES_TOTAL)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					supported_modes_text[value]);
		else
			return -EIO;
	} else if (off == DUAL_ROLE_PROP_MODE) {
		BUILD_BUG_ON(DUAL_ROLE_PROP_MODE_TOTAL !=
			ARRAY_SIZE(mode_text));
		if (value < DUAL_ROLE_PROP_MODE_TOTAL)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					mode_text[value]);
		else
			return -EIO;
	} else if (off == DUAL_ROLE_PROP_PR) {
		BUILD_BUG_ON(DUAL_ROLE_PROP_PR_TOTAL != ARRAY_SIZE(pr_text));
		if (value < DUAL_ROLE_PROP_PR_TOTAL)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					pr_text[value]);
		else
			return -EIO;
	} else if (off == DUAL_ROLE_PROP_DR) {
		BUILD_BUG_ON(DUAL_ROLE_PROP_DR_TOTAL != ARRAY_SIZE(dr_text));
		if (value < DUAL_ROLE_PROP_DR_TOTAL)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					dr_text[value]);
		else
			return -EIO;
	} else if (off == DUAL_ROLE_PROP_VCONN_SUPPLY) {
		BUILD_BUG_ON(DUAL_ROLE_PROP_VCONN_SUPPLY_TOTAL !=
				ARRAY_SIZE(vconn_supply_text));
		if (value < DUAL_ROLE_PROP_VCONN_SUPPLY_TOTAL)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					vconn_supply_text[value]);
		else
			return -EIO;
	} else
		return -EIO;
}

static ssize_t dual_role_store_property(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	ssize_t ret;
	struct dual_role_phy_instance *dual_role = dev_get_drvdata(dev);
	const ptrdiff_t off = attr - dual_role_attrs;
	unsigned int value;
	int total, i;
	char *dup_buf, **text_array;
	bool result = false;

	dup_buf = kstrdupcase(buf, GFP_KERNEL, false);
	switch (off) {
	case DUAL_ROLE_PROP_MODE:
		total = DUAL_ROLE_PROP_MODE_TOTAL;
		text_array = mode_text;
		break;
	case DUAL_ROLE_PROP_PR:
		total = DUAL_ROLE_PROP_PR_TOTAL;
		text_array = pr_text;
		break;
	case DUAL_ROLE_PROP_DR:
		total = DUAL_ROLE_PROP_DR_TOTAL;
		text_array = dr_text;
		break;
	case DUAL_ROLE_PROP_VCONN_SUPPLY:
		ret = strtobool(dup_buf, &result);
		value = result;
		if (!ret)
			goto setprop;
	default:
		ret = -EINVAL;
		goto error;
	}

	for (i = 0; i <= total; i++) {
		if (i == total) {
			ret = -ENOTSUPP;
			goto error;
		}
		if (!strncmp(*(text_array + i), dup_buf,
			     strlen(*(text_array + i)))) {
			value = i;
			break;
		}
	}

setprop:
	ret = dual_role->desc->set_property(dual_role, off, &value);

error:
	kfree(dup_buf);

	if (ret < 0)
		return ret;

	return count;
}

static umode_t dual_role_attr_is_visible(struct kobject *kobj,
					 struct attribute *attr, int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct dual_role_phy_instance *dual_role = dev_get_drvdata(dev);
	umode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
	int i;

	if (attrno == DUAL_ROLE_PROP_SUPPORTED_MODES)
		return mode;

	for (i = 0; i < dual_role->desc->num_properties; i++) {
		int property = dual_role->desc->properties[i];

		if (property == attrno) {
			if (dual_role->desc->property_is_writeable &&
			    dual_role_property_is_writeable(dual_role, property)
			    > 0)
				mode |= S_IWUSR;

			return mode;
		}
	}

	return 0;
}

static struct attribute *__dual_role_attrs[ARRAY_SIZE(dual_role_attrs) + 1];

static struct attribute_group dual_role_attr_group = {
	.attrs = __dual_role_attrs,
	.is_visible = dual_role_attr_is_visible,
};

static const struct attribute_group *dual_role_attr_groups[] = {
	&dual_role_attr_group,
	NULL,
};

void dual_role_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = dual_role_attr_groups;

	for (i = 0; i < ARRAY_SIZE(dual_role_attrs); i++)
		__dual_role_attrs[i] = &dual_role_attrs[i].attr;
}

int dual_role_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct dual_role_phy_instance *dual_role = dev_get_drvdata(dev);
	int ret = 0, j;
	char *prop_buf;
	char *attrname;

	dev_dbg(dev, "uevent\n");

	if (!dual_role || !dual_role->desc) {
		dev_dbg(dev, "No dual_role phy yet\n");
		return ret;
	}

	dev_dbg(dev, "DUAL_ROLE_NAME=%s\n", dual_role->desc->name);

	ret = add_uevent_var(env, "DUAL_ROLE_NAME=%s", dual_role->desc->name);
	if (ret)
		return ret;

	prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!prop_buf)
		return -ENOMEM;

	for (j = 0; j < dual_role->desc->num_properties; j++) {
		struct device_attribute *attr;
		char *line;

		attr = &dual_role_attrs[dual_role->desc->properties[j]];

		ret = dual_role_show_property(dev, attr, prop_buf);
		if (ret == -ENODEV || ret == -ENODATA) {
			ret = 0;
			continue;
		}

		if (ret < 0)
			goto out;
		line = strnchr(prop_buf, PAGE_SIZE, '\n');
		if (line)
			*line = 0;

		attrname = kstrdupcase(attr->attr.name, GFP_KERNEL, true);
		if (!attrname)
			ret = -ENOMEM;

		dev_dbg(dev, "prop %s=%s\n", attrname, prop_buf);

		ret = add_uevent_var(env, "DUAL_ROLE_%s=%s", attrname,
				     prop_buf);
		kfree(attrname);
		if (ret)
			goto out;
	}

out:
	free_page((unsigned long)prop_buf);

	return ret;
}

/******************* Module Init ***********************************/

static int __init dual_role_class_init(void)
{
	dual_role_class = class_create(THIS_MODULE, "dual_role_usb");

	if (IS_ERR(dual_role_class))
		return PTR_ERR(dual_role_class);

	dual_role_class->dev_uevent = dual_role_uevent;
	dual_role_init_attrs(&dual_role_dev_type);

	return 0;
}

static void __exit dual_role_class_exit(void)
{
	class_destroy(dual_role_class);
}

subsys_initcall(dual_role_class_init);
module_exit(dual_role_class_exit);
