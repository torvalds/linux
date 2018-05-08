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
#ifdef CONFIG_BCM47XX
#include <linux/bcm47xx_nvram.h>
#endif

enum bcma_boot_dev {
	BCMA_BOOT_DEV_UNK = 0,
	BCMA_BOOT_DEV_ROM,
	BCMA_BOOT_DEV_PARALLEL,
	BCMA_BOOT_DEV_SERIAL,
	BCMA_BOOT_DEV_NAND,
};

/* The 47162a0 hangs when reading MIPS DMP registers registers */
static inline bool bcma_core_mips_bcm47162a0_quirk(struct bcma_device *dev)
{
	return dev->bus->chipinfo.id == BCMA_CHIP_ID_BCM47162 &&
	       dev->bus->chipinfo.rev == 0 && dev->id.id == BCMA_CORE_MIPS_74K;
}

/* The 5357b0 hangs when reading USB20H DMP registers */
static inline bool bcma_core_mips_bcm5357b0_quirk(struct bcma_device *dev)
{
	return (dev->bus->chipinfo.id == BCMA_CHIP_ID_BCM5357 ||
		dev->bus->chipinfo.id == BCMA_CHIP_ID_BCM4749) &&
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

	if (flag)
		return flag & 0x1F;
	else
		return 0x3f;
}

/* Get the MIPS IRQ assignment for a specified device.
 * If unassigned, 0 is returned.
 * If disabled, 5 is returned.
 * If not supported, 6 is returned.
 */
unsigned int bcma_core_mips_irq(struct bcma_device *dev)
{
	struct bcma_device *mdev = dev->bus->drv_mips.core;
	u32 irqflag;
	unsigned int irq;

	irqflag = bcma_core_mips_irqflag(dev);
	if (irqflag == 0x3f)
		return 6;

	for (irq = 0; irq <= 4; irq++)
		if (bcma_read32(mdev, BCMA_MIPS_MIPS74K_INTMASK(irq)) &
		    (1 << irqflag))
			return irq;

	return 5;
}

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
	else if (oldirq != 5)
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(oldirq), 0);

	/* assign the new one */
	if (irq == 0) {
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(0),
			    bcma_read32(mdev, BCMA_MIPS_MIPS74K_INTMASK(0)) |
			    (1 << irqflag));
	} else {
		u32 irqinitmask = bcma_read32(mdev,
					      BCMA_MIPS_MIPS74K_INTMASK(irq));
		if (irqinitmask) {
			struct bcma_device *core;

			/* backplane irq line is in use, find out who uses
			 * it and set user to irq 0
			 */
			list_for_each_entry(core, &bus->cores, list) {
				if ((1 << bcma_core_mips_irqflag(core)) ==
				    irqinitmask) {
					bcma_core_mips_set_irq(core, 0);
					break;
				}
			}
		}
		bcma_write32(mdev, BCMA_MIPS_MIPS74K_INTMASK(irq),
			     1 << irqflag);
	}

	bcma_debug(bus, "set_irq: core 0x%04x, irq %d => %d\n",
		   dev->id.id, oldirq <= 4 ? oldirq + 2 : 0, irq + 2);
}

static void bcma_core_mips_set_irq_name(struct bcma_bus *bus, unsigned int irq,
					u16 coreid, u8 unit)
{
	struct bcma_device *core;

	core = bcma_find_core_unit(bus, coreid, unit);
	if (!core) {
		bcma_warn(bus,
			  "Can not find core (id: 0x%x, unit %i) for IRQ configuration.\n",
			  coreid, unit);
		return;
	}

	bcma_core_mips_set_irq(core, irq);
}

static void bcma_core_mips_print_irq(struct bcma_device *dev, unsigned int irq)
{
	int i;
	static const char *irq_name[] = {"2(S)", "3", "4", "5", "6", "D", "I"};
	char interrupts[25];
	char *ints = interrupts;

	for (i = 0; i < ARRAY_SIZE(irq_name); i++)
		ints += sprintf(ints, " %s%c",
				irq_name[i], i == irq ? '*' : ' ');

	bcma_debug(dev->bus, "core 0x%04x, irq:%s\n", dev->id.id, interrupts);
}

static void bcma_core_mips_dump_irq(struct bcma_bus *bus)
{
	struct bcma_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		bcma_core_mips_print_irq(core, bcma_core_mips_irq(core));
	}
}

u32 bcma_cpu_clock(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus = mcore->core->bus;

	if (bus->drv_cc.capabilities & BCMA_CC_CAP_PMU)
		return bcma_pmu_get_cpu_clock(&bus->drv_cc);

	bcma_err(bus, "No PMU available, need this to get the cpu clock\n");
	return 0;
}
EXPORT_SYMBOL(bcma_cpu_clock);

static enum bcma_boot_dev bcma_boot_dev(struct bcma_bus *bus)
{
	struct bcma_drv_cc *cc = &bus->drv_cc;
	u8 cc_rev = cc->core->id.rev;

	if (cc_rev == 42) {
		struct bcma_device *core;

		core = bcma_find_core(bus, BCMA_CORE_NS_ROM);
		if (core) {
			switch (bcma_aread32(core, BCMA_IOST) &
				BCMA_NS_ROM_IOST_BOOT_DEV_MASK) {
			case BCMA_NS_ROM_IOST_BOOT_DEV_NOR:
				return BCMA_BOOT_DEV_SERIAL;
			case BCMA_NS_ROM_IOST_BOOT_DEV_NAND:
				return BCMA_BOOT_DEV_NAND;
			case BCMA_NS_ROM_IOST_BOOT_DEV_ROM:
			default:
				return BCMA_BOOT_DEV_ROM;
			}
		}
	} else {
		if (cc_rev == 38) {
			if (cc->status & BCMA_CC_CHIPST_5357_NAND_BOOT)
				return BCMA_BOOT_DEV_NAND;
			else if (cc->status & BIT(5))
				return BCMA_BOOT_DEV_ROM;
		}

		if ((cc->capabilities & BCMA_CC_CAP_FLASHT) ==
		    BCMA_CC_FLASHT_PARA)
			return BCMA_BOOT_DEV_PARALLEL;
		else
			return BCMA_BOOT_DEV_SERIAL;
	}

	return BCMA_BOOT_DEV_SERIAL;
}

static void bcma_core_mips_nvram_init(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus = mcore->core->bus;
	enum bcma_boot_dev boot_dev;

	/* Determine flash type this SoC boots from */
	boot_dev = bcma_boot_dev(bus);
	switch (boot_dev) {
	case BCMA_BOOT_DEV_PARALLEL:
	case BCMA_BOOT_DEV_SERIAL:
#ifdef CONFIG_BCM47XX
		bcm47xx_nvram_init_from_mem(BCMA_SOC_FLASH2,
					    BCMA_SOC_FLASH2_SZ);
#endif
		break;
	case BCMA_BOOT_DEV_NAND:
#ifdef CONFIG_BCM47XX
		bcm47xx_nvram_init_from_mem(BCMA_SOC_FLASH1,
					    BCMA_SOC_FLASH1_SZ);
#endif
		break;
	default:
		break;
	}
}

void bcma_core_mips_early_init(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus = mcore->core->bus;

	if (mcore->early_setup_done)
		return;

	bcma_chipco_serial_init(&bus->drv_cc);
	bcma_core_mips_nvram_init(mcore);

	mcore->early_setup_done = true;
}

static void bcma_fix_i2s_irqflag(struct bcma_bus *bus)
{
	struct bcma_device *cpu, *pcie, *i2s;

	/* Fixup the interrupts in 4716/4748 for i2s core (2010 Broadcom SDK)
	 * (IRQ flags > 7 are ignored when setting the interrupt masks)
	 */
	if (bus->chipinfo.id != BCMA_CHIP_ID_BCM4716 &&
	    bus->chipinfo.id != BCMA_CHIP_ID_BCM4748)
		return;

	cpu = bcma_find_core(bus, BCMA_CORE_MIPS_74K);
	pcie = bcma_find_core(bus, BCMA_CORE_PCIE);
	i2s = bcma_find_core(bus, BCMA_CORE_I2S);
	if (cpu && pcie && i2s &&
	    bcma_aread32(cpu, BCMA_MIPS_OOBSELINA74) == 0x08060504 &&
	    bcma_aread32(pcie, BCMA_MIPS_OOBSELINA74) == 0x08060504 &&
	    bcma_aread32(i2s, BCMA_MIPS_OOBSELOUTA30) == 0x88) {
		bcma_awrite32(cpu, BCMA_MIPS_OOBSELINA74, 0x07060504);
		bcma_awrite32(pcie, BCMA_MIPS_OOBSELINA74, 0x07060504);
		bcma_awrite32(i2s, BCMA_MIPS_OOBSELOUTA30, 0x87);
		bcma_debug(bus,
			   "Moved i2s interrupt to oob line 7 instead of 8\n");
	}
}

void bcma_core_mips_init(struct bcma_drv_mips *mcore)
{
	struct bcma_bus *bus;
	struct bcma_device *core;
	bus = mcore->core->bus;

	if (mcore->setup_done)
		return;

	bcma_debug(bus, "Initializing MIPS core...\n");

	bcma_core_mips_early_init(mcore);

	bcma_fix_i2s_irqflag(bus);

	switch (bus->chipinfo.id) {
	case BCMA_CHIP_ID_BCM4716:
	case BCMA_CHIP_ID_BCM4748:
		bcma_core_mips_set_irq_name(bus, 1, BCMA_CORE_80211, 0);
		bcma_core_mips_set_irq_name(bus, 2, BCMA_CORE_MAC_GBIT, 0);
		bcma_core_mips_set_irq_name(bus, 3, BCMA_CORE_USB20_HOST, 0);
		bcma_core_mips_set_irq_name(bus, 4, BCMA_CORE_PCIE, 0);
		bcma_core_mips_set_irq_name(bus, 0, BCMA_CORE_CHIPCOMMON, 0);
		bcma_core_mips_set_irq_name(bus, 0, BCMA_CORE_I2S, 0);
		break;
	case BCMA_CHIP_ID_BCM5356:
	case BCMA_CHIP_ID_BCM47162:
	case BCMA_CHIP_ID_BCM53572:
		bcma_core_mips_set_irq_name(bus, 1, BCMA_CORE_80211, 0);
		bcma_core_mips_set_irq_name(bus, 2, BCMA_CORE_MAC_GBIT, 0);
		bcma_core_mips_set_irq_name(bus, 0, BCMA_CORE_CHIPCOMMON, 0);
		break;
	case BCMA_CHIP_ID_BCM5357:
	case BCMA_CHIP_ID_BCM4749:
		bcma_core_mips_set_irq_name(bus, 1, BCMA_CORE_80211, 0);
		bcma_core_mips_set_irq_name(bus, 2, BCMA_CORE_MAC_GBIT, 0);
		bcma_core_mips_set_irq_name(bus, 3, BCMA_CORE_USB20_HOST, 0);
		bcma_core_mips_set_irq_name(bus, 0, BCMA_CORE_CHIPCOMMON, 0);
		bcma_core_mips_set_irq_name(bus, 0, BCMA_CORE_I2S, 0);
		break;
	case BCMA_CHIP_ID_BCM4706:
		bcma_core_mips_set_irq_name(bus, 1, BCMA_CORE_PCIE, 0);
		bcma_core_mips_set_irq_name(bus, 2, BCMA_CORE_4706_MAC_GBIT,
					    0);
		bcma_core_mips_set_irq_name(bus, 3, BCMA_CORE_PCIE, 1);
		bcma_core_mips_set_irq_name(bus, 4, BCMA_CORE_USB20_HOST, 0);
		bcma_core_mips_set_irq_name(bus, 0, BCMA_CORE_4706_CHIPCOMMON,
					    0);
		break;
	default:
		list_for_each_entry(core, &bus->cores, list) {
			core->irq = bcma_core_irq(core, 0);
		}
		bcma_err(bus,
			 "Unknown device (0x%x) found, can not configure IRQs\n",
			 bus->chipinfo.id);
	}
	bcma_debug(bus, "IRQ reconfiguration done\n");
	bcma_core_mips_dump_irq(bus);

	mcore->setup_done = true;
}
