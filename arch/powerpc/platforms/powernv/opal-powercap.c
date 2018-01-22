/*
 * PowerNV OPAL Powercap interface
 *
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)     "opal-powercap: " fmt

#include <linux/of.h>
#include <linux/kobject.h>
#include <linux/slab.h>

#include <asm/opal.h>

DEFINE_MUTEX(powercap_mutex);

static struct kobject *powercap_kobj;

struct powercap_attr {
	u32 handle;
	struct kobj_attribute attr;
};

static struct pcap {
	struct attribute_group pg;
	struct powercap_attr *pattrs;
} *pcaps;

static ssize_t powercap_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	struct powercap_attr *pcap_attr = container_of(attr,
						struct powercap_attr, attr);
	struct opal_msg msg;
	u32 pcap;
	int ret, token;

	token = opal_async_get_token_interruptible();
	if (token < 0) {
		pr_devel("Failed to get token\n");
		return token;
	}

	ret = mutex_lock_interruptible(&powercap_mutex);
	if (ret)
		goto out_token;

	ret = opal_get_powercap(pcap_attr->handle, token, (u32 *)__pa(&pcap));
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
			ret = sprintf(buf, "%u\n", be32_to_cpu(pcap));
			if (ret < 0)
				ret = -EIO;
		}
		break;
	case OPAL_SUCCESS:
		ret = sprintf(buf, "%u\n", be32_to_cpu(pcap));
		if (ret < 0)
			ret = -EIO;
		break;
	default:
		ret = opal_error_code(ret);
	}

out:
	mutex_unlock(&powercap_mutex);
out_token:
	opal_async_release_token(token);
	return ret;
}

static ssize_t powercap_store(struct kobject *kobj,
			      struct kobj_attribute *attr, const char *buf,
			      size_t count)
{
	struct powercap_attr *pcap_attr = container_of(attr,
						struct powercap_attr, attr);
	struct opal_msg msg;
	u32 pcap;
	int ret, token;

	ret = kstrtoint(buf, 0, &pcap);
	if (ret)
		return ret;

	token = opal_async_get_token_interruptible();
	if (token < 0) {
		pr_devel("Failed to get token\n");
		return token;
	}

	ret = mutex_lock_interruptible(&powercap_mutex);
	if (ret)
		goto out_token;

	ret = opal_set_powercap(pcap_attr->handle, token, pcap);
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
	mutex_unlock(&powercap_mutex);
out_token:
	opal_async_release_token(token);
	return ret;
}

static void powercap_add_attr(int handle, const char *name,
			      struct powercap_attr *attr)
{
	attr->handle = handle;
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = powercap_show;
}

void __init opal_powercap_init(void)
{
	struct device_node *powercap, *node;
	int i = 0;

	powercap = of_find_compatible_node(NULL, NULL, "ibm,opal-powercap");
	if (!powercap) {
		pr_devel("Powercap node not found\n");
		return;
	}

	pcaps = kcalloc(of_get_child_count(powercap), sizeof(*pcaps),
			GFP_KERNEL);
	if (!pcaps)
		return;

	powercap_kobj = kobject_create_and_add("powercap", opal_kobj);
	if (!powercap_kobj) {
		pr_warn("Failed to create powercap kobject\n");
		goto out_pcaps;
	}

	i = 0;
	for_each_child_of_node(powercap, node) {
		u32 cur, min, max;
		int j = 0;
		bool has_cur = false, has_min = false, has_max = false;

		if (!of_property_read_u32(node, "powercap-min", &min)) {
			j++;
			has_min = true;
		}

		if (!of_property_read_u32(node, "powercap-max", &max)) {
			j++;
			has_max = true;
		}

		if (!of_property_read_u32(node, "powercap-current", &cur)) {
			j++;
			has_cur = true;
		}

		pcaps[i].pattrs = kcalloc(j, sizeof(struct powercap_attr),
					  GFP_KERNEL);
		if (!pcaps[i].pattrs)
			goto out_pcaps_pattrs;

		pcaps[i].pg.attrs = kcalloc(j + 1, sizeof(struct attribute *),
					    GFP_KERNEL);
		if (!pcaps[i].pg.attrs) {
			kfree(pcaps[i].pattrs);
			goto out_pcaps_pattrs;
		}

		j = 0;
		pcaps[i].pg.name = node->name;
		if (has_min) {
			powercap_add_attr(min, "powercap-min",
					  &pcaps[i].pattrs[j]);
			pcaps[i].pg.attrs[j] = &pcaps[i].pattrs[j].attr.attr;
			j++;
		}

		if (has_max) {
			powercap_add_attr(max, "powercap-max",
					  &pcaps[i].pattrs[j]);
			pcaps[i].pg.attrs[j] = &pcaps[i].pattrs[j].attr.attr;
			j++;
		}

		if (has_cur) {
			powercap_add_attr(cur, "powercap-current",
					  &pcaps[i].pattrs[j]);
			pcaps[i].pattrs[j].attr.attr.mode |= 0220;
			pcaps[i].pattrs[j].attr.store = powercap_store;
			pcaps[i].pg.attrs[j] = &pcaps[i].pattrs[j].attr.attr;
			j++;
		}

		if (sysfs_create_group(powercap_kobj, &pcaps[i].pg)) {
			pr_warn("Failed to create powercap attribute group %s\n",
				pcaps[i].pg.name);
			goto out_pcaps_pattrs;
		}
		i++;
	}

	return;

out_pcaps_pattrs:
	while (--i >= 0) {
		kfree(pcaps[i].pattrs);
		kfree(pcaps[i].pg.attrs);
	}
	kobject_put(powercap_kobj);
out_pcaps:
	kfree(pcaps);
}
