/*
 * Sonics Silicon Backplane
 * PCMCIA-Hostbus related functions
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2007 Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include "ssb_private.h"


/* Define the following to 1 to enable a printk on each coreswitch. */
#define SSB_VERBOSE_PCMCIACORESWITCH_DEBUG		0


int ssb_pcmcia_switch_coreidx(struct ssb_bus *bus,
			      u8 coreidx)
{
	struct pcmcia_device *pdev = bus->host_pcmcia;
	int err;
	int attempts = 0;
	u32 cur_core;
	conf_reg_t reg;
	u32 addr;
	u32 read_addr;

	addr = (coreidx * SSB_CORE_SIZE) + SSB_ENUM_BASE;
	while (1) {
		reg.Action = CS_WRITE;
		reg.Offset = 0x2E;
		reg.Value = (addr & 0x0000F000) >> 12;
		err = pcmcia_access_configuration_register(pdev, &reg);
		if (err != CS_SUCCESS)
			goto error;
		reg.Offset = 0x30;
		reg.Value = (addr & 0x00FF0000) >> 16;
		err = pcmcia_access_configuration_register(pdev, &reg);
		if (err != CS_SUCCESS)
			goto error;
		reg.Offset = 0x32;
		reg.Value = (addr & 0xFF000000) >> 24;
		err = pcmcia_access_configuration_register(pdev, &reg);
		if (err != CS_SUCCESS)
			goto error;

		read_addr = 0;

		reg.Action = CS_READ;
		reg.Offset = 0x2E;
		err = pcmcia_access_configuration_register(pdev, &reg);
		if (err != CS_SUCCESS)
			goto error;
		read_addr |= ((u32)(reg.Value & 0x0F)) << 12;
		reg.Offset = 0x30;
		err = pcmcia_access_configuration_register(pdev, &reg);
		if (err != CS_SUCCESS)
			goto error;
		read_addr |= ((u32)reg.Value) << 16;
		reg.Offset = 0x32;
		err = pcmcia_access_configuration_register(pdev, &reg);
		if (err != CS_SUCCESS)
			goto error;
		read_addr |= ((u32)reg.Value) << 24;

		cur_core = (read_addr - SSB_ENUM_BASE) / SSB_CORE_SIZE;
		if (cur_core == coreidx)
			break;

		if (attempts++ > SSB_BAR0_MAX_RETRIES)
			goto error;
		udelay(10);
	}

	return 0;
error:
	ssb_printk(KERN_ERR PFX "Failed to switch to core %u\n", coreidx);
	return -ENODEV;
}

int ssb_pcmcia_switch_core(struct ssb_bus *bus,
			   struct ssb_device *dev)
{
	int err;

#if SSB_VERBOSE_PCMCIACORESWITCH_DEBUG
	ssb_printk(KERN_INFO PFX
		   "Switching to %s core, index %d\n",
		   ssb_core_name(dev->id.coreid),
		   dev->core_index);
#endif

	err = ssb_pcmcia_switch_coreidx(bus, dev->core_index);
	if (!err)
		bus->mapped_device = dev;

	return err;
}

int ssb_pcmcia_switch_segment(struct ssb_bus *bus, u8 seg)
{
	int attempts = 0;
	conf_reg_t reg;
	int res;

	SSB_WARN_ON((seg != 0) && (seg != 1));
	reg.Offset = 0x34;
	reg.Function = 0;
	while (1) {
		reg.Action = CS_WRITE;
		reg.Value = seg;
		res = pcmcia_access_configuration_register(bus->host_pcmcia, &reg);
		if (unlikely(res != CS_SUCCESS))
			goto error;
		reg.Value = 0xFF;
		reg.Action = CS_READ;
		res = pcmcia_access_configuration_register(bus->host_pcmcia, &reg);
		if (unlikely(res != CS_SUCCESS))
			goto error;

		if (reg.Value == seg)
			break;

		if (unlikely(attempts++ > SSB_BAR0_MAX_RETRIES))
			goto error;
		udelay(10);
	}
	bus->mapped_pcmcia_seg = seg;

	return 0;
error:
	ssb_printk(KERN_ERR PFX "Failed to switch pcmcia segment\n");
	return -ENODEV;
}

static int select_core_and_segment(struct ssb_device *dev,
				   u16 *offset)
{
	struct ssb_bus *bus = dev->bus;
	int err;
	u8 need_segment;

	if (*offset >= 0x800) {
		*offset -= 0x800;
		need_segment = 1;
	} else
		need_segment = 0;

	if (unlikely(dev != bus->mapped_device)) {
		err = ssb_pcmcia_switch_core(bus, dev);
		if (unlikely(err))
			return err;
	}
	if (unlikely(need_segment != bus->mapped_pcmcia_seg)) {
		err = ssb_pcmcia_switch_segment(bus, need_segment);
		if (unlikely(err))
			return err;
	}

	return 0;
}

static u16 ssb_pcmcia_read16(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	int err;
	u16 value = 0xFFFF;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (likely(!err))
		value = readw(bus->mmio + offset);
	spin_unlock_irqrestore(&bus->bar_lock, flags);

	return value;
}

static u32 ssb_pcmcia_read32(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	int err;
	u32 lo = 0xFFFFFFFF, hi = 0xFFFFFFFF;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (likely(!err)) {
		lo = readw(bus->mmio + offset);
		hi = readw(bus->mmio + offset + 2);
	}
	spin_unlock_irqrestore(&bus->bar_lock, flags);

	return (lo | (hi << 16));
}

static void ssb_pcmcia_write16(struct ssb_device *dev, u16 offset, u16 value)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (likely(!err))
		writew(value, bus->mmio + offset);
	mmiowb();
	spin_unlock_irqrestore(&bus->bar_lock, flags);
}

static void ssb_pcmcia_write32(struct ssb_device *dev, u16 offset, u32 value)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (likely(!err)) {
		writew((value & 0x0000FFFF), bus->mmio + offset);
		writew(((value & 0xFFFF0000) >> 16), bus->mmio + offset + 2);
	}
	mmiowb();
	spin_unlock_irqrestore(&bus->bar_lock, flags);
}

/* Not "static", as it's used in main.c */
const struct ssb_bus_ops ssb_pcmcia_ops = {
	.read16		= ssb_pcmcia_read16,
	.read32		= ssb_pcmcia_read32,
	.write16	= ssb_pcmcia_write16,
	.write32	= ssb_pcmcia_write32,
};

#include <linux/etherdevice.h>
int ssb_pcmcia_get_invariants(struct ssb_bus *bus,
			      struct ssb_init_invariants *iv)
{
	//TODO
	random_ether_addr(iv->sprom.il0mac);
	return 0;
}

int ssb_pcmcia_init(struct ssb_bus *bus)
{
	conf_reg_t reg;
	int err;

	if (bus->bustype != SSB_BUSTYPE_PCMCIA)
		return 0;

	/* Switch segment to a known state and sync
	 * bus->mapped_pcmcia_seg with hardware state. */
	ssb_pcmcia_switch_segment(bus, 0);

	/* Init IRQ routing */
	reg.Action = CS_READ;
	reg.Function = 0;
	if (bus->chip_id == 0x4306)
		reg.Offset = 0x00;
	else
		reg.Offset = 0x80;
	err = pcmcia_access_configuration_register(bus->host_pcmcia, &reg);
	if (err != CS_SUCCESS)
		goto error;
	reg.Action = CS_WRITE;
	reg.Value |= 0x04 | 0x01;
	err = pcmcia_access_configuration_register(bus->host_pcmcia, &reg);
	if (err != CS_SUCCESS)
		goto error;

	return 0;
error:
	return -ENODEV;
}
