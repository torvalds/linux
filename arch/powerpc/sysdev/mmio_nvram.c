// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * memory mapped NVRAM
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
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
static DEFINE_SPINLOCK(mmio_nvram_lock);

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

static unsigned char mmio_nvram_read_val(int addr)
{
	unsigned long flags;
	unsigned char val;

	if (addr >= mmio_nvram_len)
		return 0xff;

	spin_lock_irqsave(&mmio_nvram_lock, flags);

	val = ioread8(mmio_nvram_start + addr);

	spin_unlock_irqrestore(&mmio_nvram_lock, flags);

	return val;
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

static void mmio_nvram_write_val(int addr, unsigned char val)
{
	unsigned long flags;

	if (addr < mmio_nvram_len) {
		spin_lock_irqsave(&mmio_nvram_lock, flags);

		iowrite8(val, mmio_nvram_start + addr);

		spin_unlock_irqrestore(&mmio_nvram_lock, flags);
	}
}

static ssize_t mmio_nvram_get_size(void)
{
	return mmio_nvram_len;
}

int __init mmio_nvram_init(void)
{
	struct device_node *nvram_node;
	unsigned long nvram_addr;
	struct resource r;
	int ret;

	nvram_node = of_find_node_by_type(NULL, "nvram");
	if (!nvram_node)
		nvram_node = of_find_compatible_node(NULL, NULL, "nvram");
	if (!nvram_node) {
		printk(KERN_WARNING "nvram: no node found in device-tree\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(nvram_node, 0, &r);
	if (ret) {
		printk(KERN_WARNING "nvram: failed to get address (err %d)\n",
		       ret);
		goto out;
	}
	nvram_addr = r.start;
	mmio_nvram_len = resource_size(&r);
	if ( (!mmio_nvram_len) || (!nvram_addr) ) {
		printk(KERN_WARNING "nvram: address or length is 0\n");
		ret = -EIO;
		goto out;
	}

	mmio_nvram_start = ioremap(nvram_addr, mmio_nvram_len);
	if (!mmio_nvram_start) {
		printk(KERN_WARNING "nvram: failed to ioremap\n");
		ret = -ENOMEM;
		goto out;
	}

	printk(KERN_INFO "mmio NVRAM, %luk at 0x%lx mapped to %p\n",
	       mmio_nvram_len >> 10, nvram_addr, mmio_nvram_start);

	ppc_md.nvram_read_val	= mmio_nvram_read_val;
	ppc_md.nvram_write_val	= mmio_nvram_write_val;
	ppc_md.nvram_read	= mmio_nvram_read;
	ppc_md.nvram_write	= mmio_nvram_write;
	ppc_md.nvram_size	= mmio_nvram_get_size;

out:
	of_node_put(nvram_node);
	return ret;
}
