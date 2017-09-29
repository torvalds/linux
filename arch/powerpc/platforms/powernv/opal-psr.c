/*
 * PowerNV OPAL Power-Shift-Ratio interface
 *
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)     "opal-psr: " fmt

#include <linux/of.h>
#include <linux/kobject.h>
#include <linux/slab.h>

#include <asm/opal.h>

DEFINE_MUTEX(psr_mutex);

static struct kobject *psr_kobj;

struct psr_attr {
	u32 handle;
	struct kobj_attribute attr;
} *psr_attrs;

static ssize_t psr_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	struct psr_attr *psr_attr = container_of(attr, struct psr_attr, attr);
	struct opal_msg msg;
	int psr, ret, token;

	token = opal_async_get_token_interruptible();
	if (token < 0) {
		pr_devel("Failed to get token\n");
		return token;
	}

	ret = mutex_lock_interruptible(&psr_mutex);
	if (ret)
		goto out_token;

	ret = opal_get_power_shift_ratio(psr_attr->handle, token,
					    (u32 *)__pa(&psr));
	switch (ret) {
	case OPAL_ASYNC_COMPLETION:
		ret = opal_async_wait_response(token, &msg);
		if (ret) {
			pr_devel("Failed to wait for the async response\n");
			ret = -EIO;
			goto out;
		}
		ret = opal_error_code(opal_get_async_rc(msg));
		if (!ret) {
			ret = sprintf(buf, "%u\n", be32_to_cpu(psr));
			if (ret < 0)
				ret = -EIO;
		}
		break;
	case OPAL_SUCCESS:
		ret = sprintf(buf, "%u\n", be32_to_cpu(psr));
		if (ret < 0)
			ret = -EIO;
		break;
	default:
		ret = opal_error_code(ret);
	}

out:
	mutex_unlock(&psr_mutex);
out_token:
	opal_async_release_token(token);
	return ret;
}

static ssize_t psr_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	struct psr_attr *psr_attr = container_of(attr, struct psr_attr, attr);
	struct opal_msg msg;
	int psr, ret, token;

	ret = kstrtoint(buf, 0, &psr);
	if (ret)
		return ret;

	token = opal_async_get_token_interruptible();
	if (token < 0) {
		pr_devel("Failed to get token\n");
		return token;
	}

	ret = mutex_lock_interruptible(&psr_mutex);
	if (ret)
		goto out_token;

	ret = opal_set_power_shift_ratio(psr_attr->handle, token, psr);
	switch (ret) {
	case OPAL_ASYNC_COMPLETION:
		ret = opal_async_wait_response(token, &msg);
		if (ret) {
			pr_devel("Failed to wait for the async response\n");
			ret = -EIO;
			goto out;
		}
		ret = opal_error_code(opal_get_async_rc(msg));
		if (!ret)
			ret = count;
		break;
	case OPAL_SUCCESS:
		ret = count;
		break;
	default:
		ret = opal_error_code(ret);
	}

out:
	mutex_unlock(&psr_mutex);
out_token:
	opal_async_release_token(token);
	return ret;
}

void __init opal_psr_init(void)
{
	struct device_node *psr, *node;
	int i = 0;

	psr = of_find_compatible_node(NULL, NULL,
				      "ibm,opal-power-shift-ratio");
	if (!psr) {
		pr_devel("Power-shift-ratio node not found\n");
		return;
	}

	psr_attrs = kcalloc(of_get_child_count(psr), sizeof(struct psr_attr),
			    GFP_KERNEL);
	if (!psr_attrs)
		return;

	psr_kobj = kobject_create_and_add("psr", opal_kobj);
	if (!psr_kobj) {
		pr_warn("Failed to create psr kobject\n");
		goto out;
	}

	for_each_child_of_node(psr, node) {
		if (of_property_read_u32(node, "handle",
					 &psr_attrs[i].handle))
			goto out_kobj;

		sysfs_attr_init(&psr_attrs[i].attr.attr);
		if (of_property_read_string(node, "label",
					    &psr_attrs[i].attr.attr.name))
			goto out_kobj;
		psr_attrs[i].attr.attr.mode = 0664;
		psr_attrs[i].attr.show = psr_show;
		psr_attrs[i].attr.store = psr_store;
		if (sysfs_create_file(psr_kobj, &psr_attrs[i].attr.attr)) {
			pr_devel("Failed to create psr sysfs file %s\n",
				 psr_attrs[i].attr.attr.name);
			goto out_kobj;
		}
		i++;
	}

	return;
out_kobj:
	kobject_put(psr_kobj);
out:
	kfree(psr_attrs);
}
