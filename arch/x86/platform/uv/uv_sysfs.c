/*
 * This file supports the /sys/firmware/sgi_uv interfaces for SGI UV.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *  Copyright (c) Russ Anderson
 */

#include <linux/device.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv.h>

struct kobject *sgi_uv_kobj;

static ssize_t partition_id_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%ld\n", sn_partition_id);
}

static ssize_t coherence_id_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%ld\n", partition_coherence_id());
}

static struct kobj_attribute partition_id_attr =
	__ATTR(partition_id, S_IRUGO, partition_id_show, NULL);

static struct kobj_attribute coherence_id_attr =
	__ATTR(coherence_id, S_IRUGO, coherence_id_show, NULL);


static int __init sgi_uv_sysfs_init(void)
{
	unsigned long ret;

	if (!is_uv_system())
		return -ENODEV;

	if (!sgi_uv_kobj)
		sgi_uv_kobj = kobject_create_and_add("sgi_uv", firmware_kobj);
	if (!sgi_uv_kobj) {
		printk(KERN_WARNING "kobject_create_and_add sgi_uv failed\n");
		return -EINVAL;
	}

	ret = sysfs_create_file(sgi_uv_kobj, &partition_id_attr.attr);
	if (ret) {
		printk(KERN_WARNING "sysfs_create_file partition_id failed\n");
		return ret;
	}

	ret = sysfs_create_file(sgi_uv_kobj, &coherence_id_attr.attr);
	if (ret) {
		printk(KERN_WARNING "sysfs_create_file coherence_id failed\n");
		return ret;
	}

	return 0;
}

device_initcall(sgi_uv_sysfs_init);
