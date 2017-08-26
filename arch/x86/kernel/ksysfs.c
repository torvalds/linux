/*
 * Architecture specific sysfs attributes in /sys/kernel
 *
 * Copyright (C) 2007, Intel Corp.
 *      Huang Ying <ying.huang@intel.com>
 * Copyright (C) 2013, 2013 Red Hat, Inc.
 *      Dave Young <dyoung@redhat.com>
 *
 * This file is released under the GPLv2
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/io.h>

#include <asm/setup.h>

static ssize_t version_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%04x\n", boot_params.hdr.version);
}

static struct kobj_attribute boot_params_version_attr = __ATTR_RO(version);

static ssize_t boot_params_data_read(struct file *fp, struct kobject *kobj,
				     struct bin_attribute *bin_attr,
				     char *buf, loff_t off, size_t count)
{
	memcpy(buf, (void *)&boot_params + off, count);
	return count;
}

static struct bin_attribute boot_params_data_attr = {
	.attr = {
		.name = "data",
		.mode = S_IRUGO,
	},
	.read = boot_params_data_read,
	.size = sizeof(boot_params),
};

static struct attribute *boot_params_version_attrs[] = {
	&boot_params_version_attr.attr,
	NULL,
};

static struct bin_attribute *boot_params_data_attrs[] = {
	&boot_params_data_attr,
	NULL,
};

static const struct attribute_group boot_params_attr_group = {
	.attrs = boot_params_version_attrs,
	.bin_attrs = boot_params_data_attrs,
};

static int kobj_to_setup_data_nr(struct kobject *kobj, int *nr)
{
	const char *name;

	name = kobject_name(kobj);
	return kstrtoint(name, 10, nr);
}

static int get_setup_data_paddr(int nr, u64 *paddr)
{
	int i = 0;
	struct setup_data *data;
	u64 pa_data = boot_params.hdr.setup_data;

	while (pa_data) {
		if (nr == i) {
			*paddr = pa_data;
			return 0;
		}
		data = memremap(pa_data, sizeof(*data), MEMREMAP_WB);
		if (!data)
			return -ENOMEM;

		pa_data = data->next;
		memunmap(data);
		i++;
	}
	return -EINVAL;
}

static int __init get_setup_data_size(int nr, size_t *size)
{
	int i = 0;
	struct setup_data *data;
	u64 pa_data = boot_params.hdr.setup_data;

	while (pa_data) {
		data = memremap(pa_data, sizeof(*data), MEMREMAP_WB);
		if (!data)
			return -ENOMEM;
		if (nr == i) {
			*size = data->len;
			memunmap(data);
			return 0;
		}

		pa_data = data->next;
		memunmap(data);
		i++;
	}
	return -EINVAL;
}

static ssize_t type_show(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	int nr, ret;
	u64 paddr;
	struct setup_data *data;

	ret = kobj_to_setup_data_nr(kobj, &nr);
	if (ret)
		return ret;

	ret = get_setup_data_paddr(nr, &paddr);
	if (ret)
		return ret;
	data = memremap(paddr, sizeof(*data), MEMREMAP_WB);
	if (!data)
		return -ENOMEM;

	ret = sprintf(buf, "0x%x\n", data->type);
	memunmap(data);
	return ret;
}

static ssize_t setup_data_data_read(struct file *fp,
				    struct kobject *kobj,
				    struct bin_attribute *bin_attr,
				    char *buf,
				    loff_t off, size_t count)
{
	int nr, ret = 0;
	u64 paddr;
	struct setup_data *data;
	void *p;

	ret = kobj_to_setup_data_nr(kobj, &nr);
	if (ret)
		return ret;

	ret = get_setup_data_paddr(nr, &paddr);
	if (ret)
		return ret;
	data = memremap(paddr, sizeof(*data), MEMREMAP_WB);
	if (!data)
		return -ENOMEM;

	if (off > data->len) {
		ret = -EINVAL;
		goto out;
	}

	if (count > data->len - off)
		count = data->len - off;

	if (!count)
		goto out;

	ret = count;
	p = memremap(paddr + sizeof(*data), data->len, MEMREMAP_WB);
	if (!p) {
		ret = -ENOMEM;
		goto out;
	}
	memcpy(buf, p + off, count);
	memunmap(p);
out:
	memunmap(data);
	return ret;
}

static struct kobj_attribute type_attr = __ATTR_RO(type);

static struct bin_attribute data_attr __ro_after_init = {
	.attr = {
		.name = "data",
		.mode = S_IRUGO,
	},
	.read = setup_data_data_read,
};

static struct attribute *setup_data_type_attrs[] = {
	&type_attr.attr,
	NULL,
};

static struct bin_attribute *setup_data_data_attrs[] = {
	&data_attr,
	NULL,
};

static const struct attribute_group setup_data_attr_group = {
	.attrs = setup_data_type_attrs,
	.bin_attrs = setup_data_data_attrs,
};

static int __init create_setup_data_node(struct kobject *parent,
					 struct kobject **kobjp, int nr)
{
	int ret = 0;
	size_t size;
	struct kobject *kobj;
	char name[16]; /* should be enough for setup_data nodes numbers */
	snprintf(name, 16, "%d", nr);

	kobj = kobject_create_and_add(name, parent);
	if (!kobj)
		return -ENOMEM;

	ret = get_setup_data_size(nr, &size);
	if (ret)
		goto out_kobj;

	data_attr.size = size;
	ret = sysfs_create_group(kobj, &setup_data_attr_group);
	if (ret)
		goto out_kobj;
	*kobjp = kobj;

	return 0;
out_kobj:
	kobject_put(kobj);
	return ret;
}

static void __init cleanup_setup_data_node(struct kobject *kobj)
{
	sysfs_remove_group(kobj, &setup_data_attr_group);
	kobject_put(kobj);
}

static int __init get_setup_data_total_num(u64 pa_data, int *nr)
{
	int ret = 0;
	struct setup_data *data;

	*nr = 0;
	while (pa_data) {
		*nr += 1;
		data = memremap(pa_data, sizeof(*data), MEMREMAP_WB);
		if (!data) {
			ret = -ENOMEM;
			goto out;
		}
		pa_data = data->next;
		memunmap(data);
	}

out:
	return ret;
}

static int __init create_setup_data_nodes(struct kobject *parent)
{
	struct kobject *setup_data_kobj, **kobjp;
	u64 pa_data;
	int i, j, nr, ret = 0;

	pa_data = boot_params.hdr.setup_data;
	if (!pa_data)
		return 0;

	setup_data_kobj = kobject_create_and_add("setup_data", parent);
	if (!setup_data_kobj) {
		ret = -ENOMEM;
		goto out;
	}

	ret = get_setup_data_total_num(pa_data, &nr);
	if (ret)
		goto out_setup_data_kobj;

	kobjp = kmalloc(sizeof(*kobjp) * nr, GFP_KERNEL);
	if (!kobjp) {
		ret = -ENOMEM;
		goto out_setup_data_kobj;
	}

	for (i = 0; i < nr; i++) {
		ret = create_setup_data_node(setup_data_kobj, kobjp + i, i);
		if (ret)
			goto out_clean_nodes;
	}

	kfree(kobjp);
	return 0;

out_clean_nodes:
	for (j = i - 1; j > 0; j--)
		cleanup_setup_data_node(*(kobjp + j));
	kfree(kobjp);
out_setup_data_kobj:
	kobject_put(setup_data_kobj);
out:
	return ret;
}

static int __init boot_params_ksysfs_init(void)
{
	int ret;
	struct kobject *boot_params_kobj;

	boot_params_kobj = kobject_create_and_add("boot_params",
						  kernel_kobj);
	if (!boot_params_kobj) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sysfs_create_group(boot_params_kobj, &boot_params_attr_group);
	if (ret)
		goto out_boot_params_kobj;

	ret = create_setup_data_nodes(boot_params_kobj);
	if (ret)
		goto out_create_group;

	return 0;
out_create_group:
	sysfs_remove_group(boot_params_kobj, &boot_params_attr_group);
out_boot_params_kobj:
	kobject_put(boot_params_kobj);
out:
	return ret;
}

arch_initcall(boot_params_ksysfs_init);
