/*
 * Copyright (c) 2003-2004 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/mach-types.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/arch/map.h>
#include <asm/arch/bast-map.h>
#include <asm/arch/bast-irq.h>

#define DRV_NAME "bast-ide"

static int __init bastide_register(unsigned int base, unsigned int aux, int irq)
{
	ide_hwif_t *hwif;
	hw_regs_t hw;
	int i;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	memset(&hw, 0, sizeof(hw));

	base += BAST_IDE_CS;
	aux  += BAST_IDE_CS;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw.io_ports[i] = (unsigned long)base;
		base += 0x20;
	}

	hw.io_ports[IDE_CONTROL_OFFSET] = aux + (6 * 0x20);
	hw.irq = irq;

	hwif = ide_find_port();
	if (hwif == NULL)
		goto out;

	i = hwif->index;

	if (hwif->present)
		ide_unregister(i);
	else
		ide_init_port_data(hwif, i);

	ide_init_port_hw(hwif, &hw);
	hwif->port_ops = NULL;

	idx[0] = i;

	ide_device_add(idx, NULL);
out:
	return 0;
}

static int __init bastide_init(void)
{
	unsigned long base = BAST_VA_IDEPRI + BAST_IDE_CS;

	/* we can treat the VR1000 and the BAST the same */

	if (!(machine_is_bast() || machine_is_vr1000()))
		return 0;

	printk("BAST: IDE driver, (c) 2003-2004 Simtec Electronics\n");

	if (!request_mem_region(base, 0x400000, DRV_NAME)) {
		printk(KERN_ERR "%s: resources busy\n", DRV_NAME);
		return -EBUSY;
	}

	bastide_register(BAST_VA_IDEPRI, BAST_VA_IDEPRIAUX, IRQ_IDE0);
	bastide_register(BAST_VA_IDESEC, BAST_VA_IDESECAUX, IRQ_IDE1);

	return 0;
}

module_init(bastide_init);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simtec BAST / Thorcom VR1000 IDE driver");
