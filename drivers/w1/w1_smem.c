/*
 * 	w1_smem.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>

#include "w1.h"
#include "w1_io.h"
#include "w1_int.h"
#include "w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, 64bit memory family.");

static ssize_t w1_smem_read_name(struct device *, char *);
static ssize_t w1_smem_read_val(struct device *, char *);
static ssize_t w1_smem_read_bin(struct kobject *, char *, loff_t, size_t);

static struct w1_family_ops w1_smem_fops = {
	.rname = &w1_smem_read_name,
	.rbin = &w1_smem_read_bin,
	.rval = &w1_smem_read_val,
	.rvalname = "id",
};

static ssize_t w1_smem_read_name(struct device *dev, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	return sprintf(buf, "%s\n", sl->name);
}

static ssize_t w1_smem_read_val(struct device *dev, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int i;
	ssize_t count = 0;
	
	for (i = 0; i < 9; ++i)
		count += sprintf(buf + count, "%02x ", ((u8 *)&sl->reg_num)[i]);
	count += sprintf(buf + count, "\n");

	return count;
}

static ssize_t w1_smem_read_bin(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = container_of(container_of(kobj, struct device, kobj),
			      			struct w1_slave, dev);
	int i;

	atomic_inc(&sl->refcnt);
	if (down_interruptible(&sl->master->mutex)) {
		count = 0;
		goto out_dec;
	}

	if (off > W1_SLAVE_DATA_SIZE) {
		count = 0;
		goto out;
	}
	if (off + count > W1_SLAVE_DATA_SIZE) {
		count = 0;
		goto out;
	}
	for (i = 0; i < 9; ++i)
		count += sprintf(buf + count, "%02x ", ((u8 *)&sl->reg_num)[i]);
	count += sprintf(buf + count, "\n");
	
out:
	up(&sl->master->mutex);
out_dec:
	atomic_dec(&sl->refcnt);

	return count;
}

static struct w1_family w1_smem_family = {
	.fid = W1_FAMILY_SMEM,
	.fops = &w1_smem_fops,
};

static int __init w1_smem_init(void)
{
	return w1_register_family(&w1_smem_family);
}

static void __exit w1_smem_fini(void)
{
	w1_unregister_family(&w1_smem_family);
}

module_init(w1_smem_init);
module_exit(w1_smem_fini);
