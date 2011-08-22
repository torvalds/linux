/*
 * Broadcom specific AMBA
 * Broadcom MIPS32 74K core driver
 *
 * Copyright 2009, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <mb@bu3sch.de>
 * Copyright 2010, Bernhard Loos <bernhardloos@googlemail.com>
 * Copyright 2011, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"

#include <linux/bcma/bcma.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/time.h>

/* The 47162a0 hangs when reading MIPS DMP registers registers */
static inline bool bcma_core_mips_bcm47162a0_quirk(struct bcma_device *dev)
{
	return dev->bus->chipinfo.id == 47162 && dev->bus->chipinfo.rev == 0 &&
	       dev->id.id == BCMA_CORE_MIPS_74K;
}

/* The 5357b0 hangs when reading USB20H DMP registers */
static inline bool bcma_core_mips_bcm5357b0_quirk(struct bcma_device *dev)
{
	return (dev->bus->chipinfo.id == 0x5357 ||
		dev->bus->chipinfo.id == 0x4749) &&
	       dev->bus->chipinfo.pkg == 11 &&
	       dev->id.id == BCMA_CORE_USB20_HOST;
}

static inline u32 mips_read32(struct bcma_drv_mips *mcore,
			      u16 offset)
{
	return bcma_read32(mcore->core, offset);
}

static inline void mips_write32(struct bcma_drv_mips *mcore,
				u16 offset,
				u32 value)
{
	bcma_write32(mcore->core, offset, value);
}

static const u32 ipsflag_irq_mask[] = {
	0,
	BCMA_MIPS_IPSFLAG_IRQ1,
	BCMA_MIPS_IPSFLAG_IRQ2,
	BCMA_MIPS_IPSFLAG_IRQ3,
	BCMA_MIPS_IPSFLAG_IRQ4,
};

static const u32 ipsflag_irq_shift[] = {
	0,
	BCMA_MIPS_IPSFLAG_IRQ1_SHIFT,
	BCMA_MIPS_IPSFLAG_IRQ2_SHIFT,
	BCMA_MIPS_IPSFLAG_IRQ3_SHIFT,
	BCMA_MIPS_IPSFLAG_IRQ4_SHIFT,
};

static u32 bcma_core_mips_irqflag(struct bcma_device *dev)
{
	u32 flag;

	if (bcma_core_mips_bcm47162a0_quirk(dev))
		return dev->core_index;
	if (bcma_core_mips_bcm5357b0_quirk(dev))
		return dev->core_index;
	flag = bcma_aread32(dev, BCMA_MIPS_OOBSELOUTA30);

	return flag & 0x1F;
}

/* Get the MIPS IRQ assignment for a specified device.
 * If unassigned, 0 is returned.
 */
unsigned int bcma_core_mips_irq(struct bcma_device *dev)
{
	struct bcma_device *mdev = dev->bus->drv_mips.core;
	u32 irqflag;
	unsigned int irq;

	irqflag = bcma_core_mips_irqflag(dev);

	for (irq = 1; irq <= 4; irq++)
		if (bcma_read32(mdev, BCMA_MIPS_MIPS74K_INTMASK(irq)) &
		    (1 << irqflag))
			return irq;

	return 0;
}
EXPORT_SYMBOL(bcma_core_mips_irq);

static void bcma_core_mips_set_irq(struct bcma_device *dev, unsigned int irq)
{
	unsigned int oldirq = bcma_core_mips_irq(dev);
	struct bcma_bus *bus = dev->bus;
	struct bcma_device *mdev = bus->drv_mips.core;
	u32 irqflag;

	irqflag = bcma_core_mips_irqflag(dev);
	BUG_ON(oldirq == 6);

	dev->irq = irq + 2;

	/* clear the old irq */
	if (oldirq == 0)
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(0),
			    bcma_read32(mdev, BCMA_MIPS_MIPS74K_INTMASK(0)) &
			    ~(1 << irqflag));
	else
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(irq), 0);

	/* assign the new one */
	if (irq == 0) {
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(0),
			    bcma_read32(mdev, BCMA_MIPS_MIPS74K_INTMASK(0)) |
			    (1 << irqflag));
	} else {
		u32 oldirqflag = bcma_read32(mdev,
					     BCMA_MIPS_MIPS74K_INTMASK(irq));
		if (oldirqflag) {
			struct bcma_device *core;

			/* backplane irq line is in use, find out who uses
			 * it and set user to irq 0
			 */
			list_for_each_entry_reverse(core, &bus->cores, list) {
				if ((1 << bcma_core_mips_irqflag(core)) ==
				    oldirqflag) {
					bcma_core_mips_set_irq(core, 0);
					break;
				}
			}
		}
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(irq),
			     1 << irqflag);
	}

	pr_info("set_irq: core 0x%04x, irq %d => %d\n",
		dev->id.id, oldirq + 2, irq + 2);
}

static void bcma_core_mips_print_irq(struct bcma_device *dev, unsigned int irq)
{
	int i;
	static const char *irq_name[] = {"2(S)", "3", "4", "5", "6", "D", "I"};
	printk(KERN_INFO KBUILD_MODNAME ": core 0x%04x, irq :", dev->id.id);
	for (i = 0; i <= 6; i++)
		printk(" %s%s", irq_name[i], i == irq ? "*" : " ");
	printk("\n");
}

static void bcma_core_mips_dump_irq(struct bcma_bus *bus)
{
	struct bcma_device *core;

	list_for_each_entry_reverse(core, &bus->cores, list) {
		bcma_core_mips_print_irq(core, bcma_core_mips_irq(core));
	}
}

u32 bcma_cpu_clock(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus = mcore->core->bus;

	if (bus->drv_cc.capabilities & BCMA_CC_CAP_PMU)
		return bcma_pmu_get_clockcpu(&bus->drv_cc);

	pr_err("No PMU available, need this to get the cpu clock\n");
	return 0;
}
EXPORT_SYMBOL(bcma_cpu_clock);

static void bcma_core_mips_flash_detect(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus = mcore->core->bus;

	switch (bus->drv_cc.capabilities & BCMA_CC_CAP_FLASHT) {
	case BCMA_CC_FLASHT_STSER:
	case BCMA_CC_FLASHT_ATSER:
		pr_err("Serial flash not supported.\n");
		break;
	case BCMA_CC_FLASHT_PARA:
		pr_info("found parallel flash.\n");
		bus->drv_cc.pflash.window = 0x1c000000;
		bus->drv_cc.pflash.window_size = 0x02000000;

		if ((bcma_read32(bus->drv_cc.core, BCMA_CC_FLASH_CFG) &
		     BCMA_CC_FLASH_CFG_DS) == 0)
			bus->drv_cc.pflash.buswidth = 1;
		else
			bus->drv_cc.pflash.buswidth = 2;
		break;
	default:
		pr_err("flash not supported.\n");
	}
}

void bcma_core_mips_init(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus;
	struct bcma_device *core;
	bus = mcore->core->bus;

	pr_info("Initializing MIPS core...\n");

	if (!mcore->setup_done)
		mcore->assigned_irqs = 1;

	/* Assign IRQs to all cores on the bus */
	list_for_each_entry_reverse(core, &bus->cores, list) {
		int mips_irq;
		if (core->irq)
			continue;

		mips_irq = bcma_core_mips_irq(core);
		if (mips_irq > 4)
			core->irq = 0;
		else
			core->irq = mips_irq + 2;
		if (core->irq > 5)
			continue;
		switch (core->id.id) {
		case BCMA_CORE_PCI:
		case BCMA_CORE_PCIE:
		case BCMA_CORE_ETHERNET:
		case BCMA_CORE_ETHERNET_GBIT:
		case BCMA_CORE_MAC_GBIT:
		case BCMA_CORE_80211:
		case BCMA_CORE_USB20_HOST:
			/* These devices get their own IRQ line if available,
			 * the rest goes on IRQ0
			 */
			if (mcore->assigned_irqs <= 4)
				bcma_core_mips_set_irq(core,
						       mcore->assigned_irqs++);
			break;
		}
	}
	pr_info("IRQ reconfiguration done\n");
	bcma_core_mips_dump_irq(bus);

	if (mcore->setup_done)
		return;

	bcma_chipco_serial_init(&bus->drv_cc);
	bcma_core_mips_flash_detect(mcore);
	mcore->setup_done = true;
}
