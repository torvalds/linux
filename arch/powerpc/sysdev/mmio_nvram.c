/*
 * memory mapped NVRAM
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/machdep.h>
#include <asm/nvram.h>
#include <asm/prom.h>

static void __iomem *mmio_nvram_start;
static long mmio_nvram_len;
static spinlock_t mmio_nvram_lock = SPIN_LOCK_UNLOCKED;

static ssize_t mmio_nvram_read(char *buf, size_t count, loff_t *index)
{
	unsigned long flags;

	if (*index >= mmio_nvram_len)
		return 0;
	if (*index + count > mmio_nvram_len)
		count = mmio_nvram_len - *index;

	spin_lock_irqsave(&mmio_nvram_lock, flags);

	memcpy_fromio(buf, mmio_nvram_start + *index, count);

	spin_unlock_irqrestore(&mmio_nvram_lock, flags);
	
	*index += count;
	return count;
}

static ssize_t mmio_nvram_write(char *buf, size_t count, loff_t *index)
{
	unsigned long flags;

	if (*index >= mmio_nvram_len)
		return 0;
	if (*index + count > mmio_nvram_len)
		count = mmio_nvram_len - *index;

	spin_lock_irqsave(&mmio_nvram_lock, flags);

	memcpy_toio(mmio_nvram_start + *index, buf, count);

	spin_unlock_irqrestore(&mmio_nvram_lock, flags);
	
	*index += count;
	return count;
}

static ssize_t mmio_nvram_get_size(void)
{
	return mmio_nvram_len;
}

int __init mmio_nvram_init(void)
{
	struct device_node *nvram_node;
	unsigned long *buffer;
	int proplen;
	unsigned long nvram_addr;
	int ret;

	ret = -ENODEV;
	nvram_node = of_find_node_by_type(NULL, "nvram");
	if (!nvram_node)
		goto out;

	ret = -EIO;
	buffer = (unsigned long *)get_property(nvram_node, "reg", &proplen);
	if (proplen != 2*sizeof(unsigned long))
		goto out;

	ret = -ENODEV;
	nvram_addr = buffer[0];
	mmio_nvram_len = buffer[1];
	if ( (!mmio_nvram_len) || (!nvram_addr) )
		goto out;

	mmio_nvram_start = ioremap(nvram_addr, mmio_nvram_len);
	if (!mmio_nvram_start)
		goto out;

	printk(KERN_INFO "mmio NVRAM, %luk mapped to %p\n",
	       mmio_nvram_len >> 10, mmio_nvram_start);

	ppc_md.nvram_read	= mmio_nvram_read;
	ppc_md.nvram_write	= mmio_nvram_write;
	ppc_md.nvram_size	= mmio_nvram_get_size;

out:
	of_node_put(nvram_node);
	return ret;
}
