/*
 * Sonics Silicon Backplane
 * Broadcom MIPS core driver
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/time.h>

#include "ssb_private.h"


static inline u32 mips_read32(struct ssb_mipscore *mcore,
			      u16 offset)
{
	return ssb_read32(mcore->dev, offset);
}

static inline void mips_write32(struct ssb_mipscore *mcore,
				u16 offset,
				u32 value)
{
	ssb_write32(mcore->dev, offset, value);
}

static const u32 ipsflag_irq_mask[] = {
	0,
	SSB_IPSFLAG_IRQ1,
	SSB_IPSFLAG_IRQ2,
	SSB_IPSFLAG_IRQ3,
	SSB_IPSFLAG_IRQ4,
};

static const u32 ipsflag_irq_shift[] = {
	0,
	SSB_IPSFLAG_IRQ1_SHIFT,
	SSB_IPSFLAG_IRQ2_SHIFT,
	SSB_IPSFLAG_IRQ3_SHIFT,
	SSB_IPSFLAG_IRQ4_SHIFT,
};

static inline u32 ssb_irqflag(struct ssb_device *dev)
{
	u32 tpsflag = ssb_read32(dev, SSB_TPSFLAG);
	if (tpsflag)
		return ssb_read32(dev, SSB_TPSFLAG) & SSB_TPSFLAG_BPFLAG;
	else
		/* not irq supported */
		return 0x3f;
}

static struct ssb_device *find_device(struct ssb_device *rdev, int irqflag)
{
	struct ssb_bus *bus = rdev->bus;
	int i;
	for (i = 0; i < bus->nr_devices; i++) {
		struct ssb_device *dev;
		dev = &(bus->devices[i]);
		if (ssb_irqflag(dev) == irqflag)
			return dev;
	}
	return NULL;
}

/* Get the MIPS IRQ assignment for a specified device.
 * If unassigned, 0 is returned.
 * If disabled, 5 is returned.
 * If not supported, 6 is returned.
 */
unsigned int ssb_mips_irq(struct ssb_device *dev)
{
	struct ssb_bus *bus = dev->bus;
	struct ssb_device *mdev = bus->mipscore.dev;
	u32 irqflag;
	u32 ipsflag;
	u32 tmp;
	unsigned int irq;

	irqflag = ssb_irqflag(dev);
	if (irqflag == 0x3f)
		return 6;
	ipsflag = ssb_read32(bus->mipscore.dev, SSB_IPSFLAG);
	for (irq = 1; irq <= 4; irq++) {
		tmp = ((ipsflag & ipsflag_irq_mask[irq]) >> ipsflag_irq_shift[irq]);
		if (tmp == irqflag)
			break;
	}
	if (irq	== 5) {
		if ((1 << irqflag) & ssb_read32(mdev, SSB_INTVEC))
			irq = 0;
	}

	return irq;
}

static void clear_irq(struct ssb_bus *bus, unsigned int irq)
{
	struct ssb_device *dev = bus->mipscore.dev;

	/* Clear the IRQ in the MIPScore backplane registers */
	if (irq == 0) {
		ssb_write32(dev, SSB_INTVEC, 0);
	} else {
		ssb_write32(dev, SSB_IPSFLAG,
			    ssb_read32(dev, SSB_IPSFLAG) |
			    ipsflag_irq_mask[irq]);
	}
}

static void set_irq(struct ssb_device *dev, unsigned int irq)
{
	unsigned int oldirq = ssb_mips_irq(dev);
	struct ssb_bus *bus = dev->bus;
	struct ssb_device *mdev = bus->mipscore.dev;
	u32 irqflag = ssb_irqflag(dev);

	BUG_ON(oldirq == 6);

	dev->irq = irq + 2;

	/* clear the old irq */
	if (oldirq == 0)
		ssb_write32(mdev, SSB_INTVEC, (~(1 << irqflag) & ssb_read32(mdev, SSB_INTVEC)));
	else if (oldirq != 5)
		clear_irq(bus, oldirq);

	/* assign the new one */
	if (irq == 0) {
		ssb_write32(mdev, SSB_INTVEC, ((1 << irqflag) | ssb_read32(mdev, SSB_INTVEC)));
	} else {
		u32 ipsflag = ssb_read32(mdev, SSB_IPSFLAG);
		if ((ipsflag & ipsflag_irq_mask[irq]) != ipsflag_irq_mask[irq]) {
			u32 oldipsflag = (ipsflag & ipsflag_irq_mask[irq]) >> ipsflag_irq_shift[irq];
			struct ssb_device *olddev = find_device(dev, oldipsflag);
			if (olddev)
				set_irq(olddev, 0);
		}
		irqflag <<= ipsflag_irq_shift[irq];
		irqflag |= (ipsflag & ~ipsflag_irq_mask[irq]);
		ssb_write32(mdev, SSB_IPSFLAG, irqflag);
	}
	ssb_dprintk(KERN_INFO PFX
		    "set_irq: core 0x%04x, irq %d => %d\n",
		    dev->id.coreid, oldirq+2, irq+2);
}

static void print_irq(struct ssb_device *dev, unsigned int irq)
{
	int i;
	static const char *irq_name[] = {"2(S)", "3", "4", "5", "6", "D", "I"};
	ssb_dprintk(KERN_INFO PFX
		"core 0x%04x, irq :", dev->id.coreid);
	for (i = 0; i <= 6; i++) {
		ssb_dprintk(" %s%s", irq_name[i], i==irq?"*":" ");
	}
	ssb_dprintk("\n");
}

static void dump_irq(struct ssb_bus *bus)
{
	int i;
	for (i = 0; i < bus->nr_devices; i++) {
		struct ssb_device *dev;
		dev = &(bus->devices[i]);
		print_irq(dev, ssb_mips_irq(dev));
	}
}

static void ssb_mips_serial_init(struct ssb_mipscore *mcore)
{
	struct ssb_bus *bus = mcore->dev->bus;

	if (bus->extif.dev)
		mcore->nr_serial_ports = ssb_extif_serial_init(&bus->extif, mcore->serial_ports);
	else if (bus->chipco.dev)
		mcore->nr_serial_ports = ssb_chipco_serial_init(&bus->chipco, mcore->serial_ports);
	else
		mcore->nr_serial_ports = 0;
}

static void ssb_mips_flash_detect(struct ssb_mipscore *mcore)
{
	struct ssb_bus *bus = mcore->dev->bus;

	mcore->flash_buswidth = 2;
	if (bus->chipco.dev) {
		mcore->flash_window = 0x1c000000;
		mcore->flash_window_size = 0x02000000;
		if ((ssb_read32(bus->chipco.dev, SSB_CHIPCO_FLASH_CFG)
		               & SSB_CHIPCO_CFG_DS16) == 0)
			mcore->flash_buswidth = 1;
	} else {
		mcore->flash_window = 0x1fc00000;
		mcore->flash_window_size = 0x00400000;
	}
}

u32 ssb_cpu_clock(struct ssb_mipscore *mcore)
{
	struct ssb_bus *bus = mcore->dev->bus;
	u32 pll_type, n, m, rate = 0;

	if (bus->extif.dev) {
		ssb_extif_get_clockcontrol(&bus->extif, &pll_type, &n, &m);
	} else if (bus->chipco.dev) {
		ssb_chipco_get_clockcpu(&bus->chipco, &pll_type, &n, &m);
	} else
		return 0;

	if ((pll_type == SSB_PLLTYPE_5) || (bus->chip_id == 0x5365)) {
		rate = 200000000;
	} else {
		rate = ssb_calc_clock_rate(pll_type, n, m);
	}

	if (pll_type == SSB_PLLTYPE_6) {
		rate *= 2;
	}

	return rate;
}

void ssb_mipscore_init(struct ssb_mipscore *mcore)
{
	struct ssb_bus *bus;
	struct ssb_device *dev;
	unsigned long hz, ns;
	unsigned int irq, i;

	if (!mcore->dev)
		return; /* We don't have a MIPS core */

	ssb_dprintk(KERN_INFO PFX "Initializing MIPS core...\n");

	bus = mcore->dev->bus;
	hz = ssb_clockspeed(bus);
	if (!hz)
		hz = 100000000;
	ns = 1000000000 / hz;

	if (bus->extif.dev)
		ssb_extif_timing_init(&bus->extif, ns);
	else if (bus->chipco.dev)
		ssb_chipco_timing_init(&bus->chipco, ns);

	/* Assign IRQs to all cores on the bus, start with irq line 2, because serial usually takes 1 */
	for (irq = 2, i = 0; i < bus->nr_devices; i++) {
		int mips_irq;
		dev = &(bus->devices[i]);
		mips_irq = ssb_mips_irq(dev);
		if (mips_irq > 4)
			dev->irq = 0;
		else
			dev->irq = mips_irq + 2;
		if (dev->irq > 5)
			continue;
		switch (dev->id.coreid) {
		case SSB_DEV_USB11_HOST:
			/* shouldn't need a separate irq line for non-4710, most of them have a proper
			 * external usb controller on the pci */
			if ((bus->chip_id == 0x4710) && (irq <= 4)) {
				set_irq(dev, irq++);
			}
			break;
		case SSB_DEV_PCI:
		case SSB_DEV_ETHERNET:
		case SSB_DEV_ETHERNET_GBIT:
		case SSB_DEV_80211:
		case SSB_DEV_USB20_HOST:
			/* These devices get their own IRQ line if available, the rest goes on IRQ0 */
			if (irq <= 4) {
				set_irq(dev, irq++);
				break;
			}
			/* fallthrough */
		case SSB_DEV_EXTIF:
			set_irq(dev, 0);
			break;
		}
	}
	ssb_dprintk(KERN_INFO PFX "after irq reconfiguration\n");
	dump_irq(bus);

	ssb_mips_serial_init(mcore);
	ssb_mips_flash_detect(mcore);
}
