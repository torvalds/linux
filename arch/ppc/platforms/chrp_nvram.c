/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /dev/nvram driver for PPC
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/prom.h>
#include <asm/machdep.h>

static unsigned int nvram_size;
static unsigned char nvram_buf[4];
static DEFINE_SPINLOCK(nvram_lock);

static unsigned char chrp_nvram_read(int addr)
{
	unsigned long done, flags;
	unsigned char ret;

	if (addr >= nvram_size) {
		printk(KERN_DEBUG "%s: read addr %d > nvram_size %u\n",
		       current->comm, addr, nvram_size);
		return 0xff;
	}
	spin_lock_irqsave(&nvram_lock, flags);
	if ((call_rtas("nvram-fetch", 3, 2, &done, addr, __pa(nvram_buf), 1) != 0) || 1 != done)
		ret = 0xff;
	else
		ret = nvram_buf[0];
	spin_unlock_irqrestore(&nvram_lock, flags);

	return ret;
}

static void chrp_nvram_write(int addr, unsigned char val)
{
	unsigned long done, flags;

	if (addr >= nvram_size) {
		printk(KERN_DEBUG "%s: write addr %d > nvram_size %u\n",
		       current->comm, addr, nvram_size);
		return;
	}
	spin_lock_irqsave(&nvram_lock, flags);
	nvram_buf[0] = val;
	if ((call_rtas("nvram-store", 3, 2, &done, addr, __pa(nvram_buf), 1) != 0) || 1 != done)
		printk(KERN_DEBUG "rtas IO error storing 0x%02x at %d", val, addr);
	spin_unlock_irqrestore(&nvram_lock, flags);
}

void __init chrp_nvram_init(void)
{
	struct device_node *nvram;
	unsigned int *nbytes_p, proplen;

	nvram = of_find_node_by_type(NULL, "nvram");
	if (nvram == NULL)
		return;

	nbytes_p = (unsigned int *)get_property(nvram, "#bytes", &proplen);
	if (nbytes_p == NULL || proplen != sizeof(unsigned int))
		return;

	nvram_size = *nbytes_p;

	printk(KERN_INFO "CHRP nvram contains %u bytes\n", nvram_size);
	of_node_put(nvram);

	ppc_md.nvram_read_val = chrp_nvram_read;
	ppc_md.nvram_write_val = chrp_nvram_write;

	return;
}
