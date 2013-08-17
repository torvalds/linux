/*
 * Sonics Silicon Backplane
 * PCMCIA-Hostbus related functions
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2007-2008 Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/etherdevice.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include "ssb_private.h"


/* Define the following to 1 to enable a printk on each coreswitch. */
#define SSB_VERBOSE_PCMCIACORESWITCH_DEBUG		0


/* PCMCIA configuration registers */
#define SSB_PCMCIA_ADDRESS0		0x2E
#define SSB_PCMCIA_ADDRESS1		0x30
#define SSB_PCMCIA_ADDRESS2		0x32
#define SSB_PCMCIA_MEMSEG		0x34
#define SSB_PCMCIA_SPROMCTL		0x36
#define  SSB_PCMCIA_SPROMCTL_IDLE	0
#define  SSB_PCMCIA_SPROMCTL_WRITE	1
#define  SSB_PCMCIA_SPROMCTL_READ	2
#define  SSB_PCMCIA_SPROMCTL_WRITEEN	4
#define  SSB_PCMCIA_SPROMCTL_WRITEDIS	7
#define  SSB_PCMCIA_SPROMCTL_DONE	8
#define SSB_PCMCIA_SPROM_DATALO		0x38
#define SSB_PCMCIA_SPROM_DATAHI		0x3A
#define SSB_PCMCIA_SPROM_ADDRLO		0x3C
#define SSB_PCMCIA_SPROM_ADDRHI		0x3E

/* Hardware invariants CIS tuples */
#define SSB_PCMCIA_CIS			0x80
#define  SSB_PCMCIA_CIS_ID		0x01
#define  SSB_PCMCIA_CIS_BOARDREV	0x02
#define  SSB_PCMCIA_CIS_PA		0x03
#define   SSB_PCMCIA_CIS_PA_PA0B0_LO	0
#define   SSB_PCMCIA_CIS_PA_PA0B0_HI	1
#define   SSB_PCMCIA_CIS_PA_PA0B1_LO	2
#define   SSB_PCMCIA_CIS_PA_PA0B1_HI	3
#define   SSB_PCMCIA_CIS_PA_PA0B2_LO	4
#define   SSB_PCMCIA_CIS_PA_PA0B2_HI	5
#define   SSB_PCMCIA_CIS_PA_ITSSI	6
#define   SSB_PCMCIA_CIS_PA_MAXPOW	7
#define  SSB_PCMCIA_CIS_OEMNAME		0x04
#define  SSB_PCMCIA_CIS_CCODE		0x05
#define  SSB_PCMCIA_CIS_ANTENNA		0x06
#define  SSB_PCMCIA_CIS_ANTGAIN		0x07
#define  SSB_PCMCIA_CIS_BFLAGS		0x08
#define  SSB_PCMCIA_CIS_LEDS		0x09

/* PCMCIA SPROM size. */
#define SSB_PCMCIA_SPROM_SIZE		256
#define SSB_PCMCIA_SPROM_SIZE_BYTES	(SSB_PCMCIA_SPROM_SIZE * sizeof(u16))


/* Write to a PCMCIA configuration register. */
static int ssb_pcmcia_cfg_write(struct ssb_bus *bus, u8 offset, u8 value)
{
	int res;

	res = pcmcia_write_config_byte(bus->host_pcmcia, offset, value);
	if (unlikely(res != 0))
		return -EBUSY;

	return 0;
}

/* Read from a PCMCIA configuration register. */
static int ssb_pcmcia_cfg_read(struct ssb_bus *bus, u8 offset, u8 *value)
{
	int res;

	res = pcmcia_read_config_byte(bus->host_pcmcia, offset, value);
	if (unlikely(res != 0))
		return -EBUSY;

	return 0;
}

int ssb_pcmcia_switch_coreidx(struct ssb_bus *bus,
			      u8 coreidx)
{
	int err;
	int attempts = 0;
	u32 cur_core;
	u32 addr;
	u32 read_addr;
	u8 val;

	addr = (coreidx * SSB_CORE_SIZE) + SSB_ENUM_BASE;
	while (1) {
		err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_ADDRESS0,
					   (addr & 0x0000F000) >> 12);
		if (err)
			goto error;
		err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_ADDRESS1,
					   (addr & 0x00FF0000) >> 16);
		if (err)
			goto error;
		err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_ADDRESS2,
					   (addr & 0xFF000000) >> 24);
		if (err)
			goto error;

		read_addr = 0;

		err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_ADDRESS0, &val);
		if (err)
			goto error;
		read_addr |= ((u32)(val & 0x0F)) << 12;
		err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_ADDRESS1, &val);
		if (err)
			goto error;
		read_addr |= ((u32)val) << 16;
		err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_ADDRESS2, &val);
		if (err)
			goto error;
		read_addr |= ((u32)val) << 24;

		cur_core = (read_addr - SSB_ENUM_BASE) / SSB_CORE_SIZE;
		if (cur_core == coreidx)
			break;

		err = -ETIMEDOUT;
		if (attempts++ > SSB_BAR0_MAX_RETRIES)
			goto error;
		udelay(10);
	}

	return 0;
error:
	ssb_printk(KERN_ERR PFX "Failed to switch to core %u\n", coreidx);
	return err;
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
	int err;
	u8 val;

	SSB_WARN_ON((seg != 0) && (seg != 1));
	while (1) {
		err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_MEMSEG, seg);
		if (err)
			goto error;
		err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_MEMSEG, &val);
		if (err)
			goto error;
		if (val == seg)
			break;

		err = -ETIMEDOUT;
		if (unlikely(attempts++ > SSB_BAR0_MAX_RETRIES))
			goto error;
		udelay(10);
	}
	bus->mapped_pcmcia_seg = seg;

	return 0;
error:
	ssb_printk(KERN_ERR PFX "Failed to switch pcmcia segment\n");
	return err;
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

static u8 ssb_pcmcia_read8(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	int err;
	u8 value = 0xFF;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (likely(!err))
		value = readb(bus->mmio + offset);
	spin_unlock_irqrestore(&bus->bar_lock, flags);

	return value;
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

#ifdef CONFIG_SSB_BLOCKIO
static void ssb_pcmcia_block_read(struct ssb_device *dev, void *buffer,
				  size_t count, u16 offset, u8 reg_width)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	void __iomem *addr = bus->mmio + offset;
	int err;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (unlikely(err)) {
		memset(buffer, 0xFF, count);
		goto unlock;
	}
	switch (reg_width) {
	case sizeof(u8): {
		u8 *buf = buffer;

		while (count) {
			*buf = __raw_readb(addr);
			buf++;
			count--;
		}
		break;
	}
	case sizeof(u16): {
		__le16 *buf = buffer;

		SSB_WARN_ON(count & 1);
		while (count) {
			*buf = (__force __le16)__raw_readw(addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		__le16 *buf = buffer;

		SSB_WARN_ON(count & 3);
		while (count) {
			*buf = (__force __le16)__raw_readw(addr);
			buf++;
			*buf = (__force __le16)__raw_readw(addr + 2);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		SSB_WARN_ON(1);
	}
unlock:
	spin_unlock_irqrestore(&bus->bar_lock, flags);
}
#endif /* CONFIG_SSB_BLOCKIO */

static void ssb_pcmcia_write8(struct ssb_device *dev, u16 offset, u8 value)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (likely(!err))
		writeb(value, bus->mmio + offset);
	mmiowb();
	spin_unlock_irqrestore(&bus->bar_lock, flags);
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

#ifdef CONFIG_SSB_BLOCKIO
static void ssb_pcmcia_block_write(struct ssb_device *dev, const void *buffer,
				   size_t count, u16 offset, u8 reg_width)
{
	struct ssb_bus *bus = dev->bus;
	unsigned long flags;
	void __iomem *addr = bus->mmio + offset;
	int err;

	spin_lock_irqsave(&bus->bar_lock, flags);
	err = select_core_and_segment(dev, &offset);
	if (unlikely(err))
		goto unlock;
	switch (reg_width) {
	case sizeof(u8): {
		const u8 *buf = buffer;

		while (count) {
			__raw_writeb(*buf, addr);
			buf++;
			count--;
		}
		break;
	}
	case sizeof(u16): {
		const __le16 *buf = buffer;

		SSB_WARN_ON(count & 1);
		while (count) {
			__raw_writew((__force u16)(*buf), addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		const __le16 *buf = buffer;

		SSB_WARN_ON(count & 3);
		while (count) {
			__raw_writew((__force u16)(*buf), addr);
			buf++;
			__raw_writew((__force u16)(*buf), addr + 2);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		SSB_WARN_ON(1);
	}
unlock:
	mmiowb();
	spin_unlock_irqrestore(&bus->bar_lock, flags);
}
#endif /* CONFIG_SSB_BLOCKIO */

/* Not "static", as it's used in main.c */
const struct ssb_bus_ops ssb_pcmcia_ops = {
	.read8		= ssb_pcmcia_read8,
	.read16		= ssb_pcmcia_read16,
	.read32		= ssb_pcmcia_read32,
	.write8		= ssb_pcmcia_write8,
	.write16	= ssb_pcmcia_write16,
	.write32	= ssb_pcmcia_write32,
#ifdef CONFIG_SSB_BLOCKIO
	.block_read	= ssb_pcmcia_block_read,
	.block_write	= ssb_pcmcia_block_write,
#endif
};

static int ssb_pcmcia_sprom_command(struct ssb_bus *bus, u8 command)
{
	unsigned int i;
	int err;
	u8 value;

	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROMCTL, command);
	if (err)
		return err;
	for (i = 0; i < 1000; i++) {
		err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_SPROMCTL, &value);
		if (err)
			return err;
		if (value & SSB_PCMCIA_SPROMCTL_DONE)
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

/* offset is the 16bit word offset */
static int ssb_pcmcia_sprom_read(struct ssb_bus *bus, u16 offset, u16 *value)
{
	int err;
	u8 lo, hi;

	offset *= 2; /* Make byte offset */

	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROM_ADDRLO,
				   (offset & 0x00FF));
	if (err)
		return err;
	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROM_ADDRHI,
				   (offset & 0xFF00) >> 8);
	if (err)
		return err;
	err = ssb_pcmcia_sprom_command(bus, SSB_PCMCIA_SPROMCTL_READ);
	if (err)
		return err;
	err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_SPROM_DATALO, &lo);
	if (err)
		return err;
	err = ssb_pcmcia_cfg_read(bus, SSB_PCMCIA_SPROM_DATAHI, &hi);
	if (err)
		return err;
	*value = (lo | (((u16)hi) << 8));

	return 0;
}

/* offset is the 16bit word offset */
static int ssb_pcmcia_sprom_write(struct ssb_bus *bus, u16 offset, u16 value)
{
	int err;

	offset *= 2; /* Make byte offset */

	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROM_ADDRLO,
				   (offset & 0x00FF));
	if (err)
		return err;
	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROM_ADDRHI,
				   (offset & 0xFF00) >> 8);
	if (err)
		return err;
	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROM_DATALO,
				   (value & 0x00FF));
	if (err)
		return err;
	err = ssb_pcmcia_cfg_write(bus, SSB_PCMCIA_SPROM_DATAHI,
				   (value & 0xFF00) >> 8);
	if (err)
		return err;
	err = ssb_pcmcia_sprom_command(bus, SSB_PCMCIA_SPROMCTL_WRITE);
	if (err)
		return err;
	msleep(20);

	return 0;
}

/* Read the SPROM image. bufsize is in 16bit words. */
static int ssb_pcmcia_sprom_read_all(struct ssb_bus *bus, u16 *sprom)
{
	int err, i;

	for (i = 0; i < SSB_PCMCIA_SPROM_SIZE; i++) {
		err = ssb_pcmcia_sprom_read(bus, i, &sprom[i]);
		if (err)
			return err;
	}

	return 0;
}

/* Write the SPROM image. size is in 16bit words. */
static int ssb_pcmcia_sprom_write_all(struct ssb_bus *bus, const u16 *sprom)
{
	int i, err;
	bool failed = 0;
	size_t size = SSB_PCMCIA_SPROM_SIZE;

	ssb_printk(KERN_NOTICE PFX
		   "Writing SPROM. Do NOT turn off the power! "
		   "Please stand by...\n");
	err = ssb_pcmcia_sprom_command(bus, SSB_PCMCIA_SPROMCTL_WRITEEN);
	if (err) {
		ssb_printk(KERN_NOTICE PFX
			   "Could not enable SPROM write access.\n");
		return -EBUSY;
	}
	ssb_printk(KERN_NOTICE PFX "[ 0%%");
	msleep(500);
	for (i = 0; i < size; i++) {
		if (i == size / 4)
			ssb_printk("25%%");
		else if (i == size / 2)
			ssb_printk("50%%");
		else if (i == (size * 3) / 4)
			ssb_printk("75%%");
		else if (i % 2)
			ssb_printk(".");
		err = ssb_pcmcia_sprom_write(bus, i, sprom[i]);
		if (err) {
			ssb_printk(KERN_NOTICE PFX
				   "Failed to write to SPROM.\n");
			failed = 1;
			break;
		}
	}
	err = ssb_pcmcia_sprom_command(bus, SSB_PCMCIA_SPROMCTL_WRITEDIS);
	if (err) {
		ssb_printk(KERN_NOTICE PFX
			   "Could not disable SPROM write access.\n");
		failed = 1;
	}
	msleep(500);
	if (!failed) {
		ssb_printk("100%% ]\n");
		ssb_printk(KERN_NOTICE PFX "SPROM written.\n");
	}

	return failed ? -EBUSY : 0;
}

static int ssb_pcmcia_sprom_check_crc(const u16 *sprom, size_t size)
{
	//TODO
	return 0;
}

#define GOTO_ERROR_ON(condition, description) do {	\
	if (unlikely(condition)) {			\
		error_description = description;	\
		goto error;				\
	}						\
  } while (0)

static int ssb_pcmcia_get_mac(struct pcmcia_device *p_dev,
			tuple_t *tuple,
			void *priv)
{
	struct ssb_sprom *sprom = priv;

	if (tuple->TupleData[0] != CISTPL_FUNCE_LAN_NODE_ID)
		return -EINVAL;
	if (tuple->TupleDataLen != ETH_ALEN + 2)
		return -EINVAL;
	if (tuple->TupleData[1] != ETH_ALEN)
		return -EINVAL;
	memcpy(sprom->il0mac, &tuple->TupleData[2], ETH_ALEN);
	return 0;
};

static int ssb_pcmcia_do_get_invariants(struct pcmcia_device *p_dev,
					tuple_t *tuple,
					void *priv)
{
	struct ssb_init_invariants *iv = priv;
	struct ssb_sprom *sprom = &iv->sprom;
	struct ssb_boardinfo *bi = &iv->boardinfo;
	const char *error_description;

	GOTO_ERROR_ON(tuple->TupleDataLen < 1, "VEN tpl < 1");
	switch (tuple->TupleData[0]) {
	case SSB_PCMCIA_CIS_ID:
		GOTO_ERROR_ON((tuple->TupleDataLen != 5) &&
			      (tuple->TupleDataLen != 7),
			      "id tpl size");
		bi->vendor = tuple->TupleData[1] |
			((u16)tuple->TupleData[2] << 8);
		break;
	case SSB_PCMCIA_CIS_BOARDREV:
		GOTO_ERROR_ON(tuple->TupleDataLen != 2,
			"boardrev tpl size");
		sprom->board_rev = tuple->TupleData[1];
		break;
	case SSB_PCMCIA_CIS_PA:
		GOTO_ERROR_ON((tuple->TupleDataLen != 9) &&
			(tuple->TupleDataLen != 10),
			"pa tpl size");
		sprom->pa0b0 = tuple->TupleData[1] |
			((u16)tuple->TupleData[2] << 8);
		sprom->pa0b1 = tuple->TupleData[3] |
			((u16)tuple->TupleData[4] << 8);
		sprom->pa0b2 = tuple->TupleData[5] |
			((u16)tuple->TupleData[6] << 8);
		sprom->itssi_a = tuple->TupleData[7];
		sprom->itssi_bg = tuple->TupleData[7];
		sprom->maxpwr_a = tuple->TupleData[8];
		sprom->maxpwr_bg = tuple->TupleData[8];
		break;
	case SSB_PCMCIA_CIS_OEMNAME:
		/* We ignore this. */
		break;
	case SSB_PCMCIA_CIS_CCODE:
		GOTO_ERROR_ON(tuple->TupleDataLen != 2,
			"ccode tpl size");
		sprom->country_code = tuple->TupleData[1];
		break;
	case SSB_PCMCIA_CIS_ANTENNA:
		GOTO_ERROR_ON(tuple->TupleDataLen != 2,
			"ant tpl size");
		sprom->ant_available_a = tuple->TupleData[1];
		sprom->ant_available_bg = tuple->TupleData[1];
		break;
	case SSB_PCMCIA_CIS_ANTGAIN:
		GOTO_ERROR_ON(tuple->TupleDataLen != 2,
			"antg tpl size");
		sprom->antenna_gain.a0 = tuple->TupleData[1];
		sprom->antenna_gain.a1 = tuple->TupleData[1];
		sprom->antenna_gain.a2 = tuple->TupleData[1];
		sprom->antenna_gain.a3 = tuple->TupleData[1];
		break;
	case SSB_PCMCIA_CIS_BFLAGS:
		GOTO_ERROR_ON((tuple->TupleDataLen != 3) &&
			(tuple->TupleDataLen != 5),
			"bfl tpl size");
		sprom->boardflags_lo = tuple->TupleData[1] |
			((u16)tuple->TupleData[2] << 8);
		break;
	case SSB_PCMCIA_CIS_LEDS:
		GOTO_ERROR_ON(tuple->TupleDataLen != 5,
			"leds tpl size");
		sprom->gpio0 = tuple->TupleData[1];
		sprom->gpio1 = tuple->TupleData[2];
		sprom->gpio2 = tuple->TupleData[3];
		sprom->gpio3 = tuple->TupleData[4];
		break;
	}
	return -ENOSPC; /* continue with next entry */

error:
	ssb_printk(KERN_ERR PFX
		   "PCMCIA: Failed to fetch device invariants: %s\n",
		   error_description);
	return -ENODEV;
}


int ssb_pcmcia_get_invariants(struct ssb_bus *bus,
			      struct ssb_init_invariants *iv)
{
	struct ssb_sprom *sprom = &iv->sprom;
	int res;

	memset(sprom, 0xFF, sizeof(*sprom));
	sprom->revision = 1;
	sprom->boardflags_lo = 0;
	sprom->boardflags_hi = 0;

	/* First fetch the MAC address. */
	res = pcmcia_loop_tuple(bus->host_pcmcia, CISTPL_FUNCE,
				ssb_pcmcia_get_mac, sprom);
	if (res != 0) {
		ssb_printk(KERN_ERR PFX
			"PCMCIA: Failed to fetch MAC address\n");
		return -ENODEV;
	}

	/* Fetch the vendor specific tuples. */
	res = pcmcia_loop_tuple(bus->host_pcmcia, SSB_PCMCIA_CIS,
				ssb_pcmcia_do_get_invariants, iv);
	if ((res == 0) || (res == -ENOSPC))
		return 0;

	ssb_printk(KERN_ERR PFX
			"PCMCIA: Failed to fetch device invariants\n");
	return -ENODEV;
}

static ssize_t ssb_pcmcia_attr_sprom_show(struct device *pcmciadev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct pcmcia_device *pdev =
		container_of(pcmciadev, struct pcmcia_device, dev);
	struct ssb_bus *bus;

	bus = ssb_pcmcia_dev_to_bus(pdev);
	if (!bus)
		return -ENODEV;

	return ssb_attr_sprom_show(bus, buf,
				   ssb_pcmcia_sprom_read_all);
}

static ssize_t ssb_pcmcia_attr_sprom_store(struct device *pcmciadev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct pcmcia_device *pdev =
		container_of(pcmciadev, struct pcmcia_device, dev);
	struct ssb_bus *bus;

	bus = ssb_pcmcia_dev_to_bus(pdev);
	if (!bus)
		return -ENODEV;

	return ssb_attr_sprom_store(bus, buf, count,
				    ssb_pcmcia_sprom_check_crc,
				    ssb_pcmcia_sprom_write_all);
}

static DEVICE_ATTR(ssb_sprom, 0600,
		   ssb_pcmcia_attr_sprom_show,
		   ssb_pcmcia_attr_sprom_store);

static int ssb_pcmcia_cor_setup(struct ssb_bus *bus, u8 cor)
{
	u8 val;
	int err;

	err = ssb_pcmcia_cfg_read(bus, cor, &val);
	if (err)
		return err;
	val &= ~COR_SOFT_RESET;
	val |= COR_FUNC_ENA | COR_IREQ_ENA | COR_LEVEL_REQ;
	err = ssb_pcmcia_cfg_write(bus, cor, val);
	if (err)
		return err;
	msleep(40);

	return 0;
}

/* Initialize the PCMCIA hardware. This is called on Init and Resume. */
int ssb_pcmcia_hardware_setup(struct ssb_bus *bus)
{
	int err;

	if (bus->bustype != SSB_BUSTYPE_PCMCIA)
		return 0;

	/* Switch segment to a known state and sync
	 * bus->mapped_pcmcia_seg with hardware state. */
	ssb_pcmcia_switch_segment(bus, 0);
	/* Init the COR register. */
	err = ssb_pcmcia_cor_setup(bus, CISREG_COR);
	if (err)
		return err;
	/* Some cards also need this register to get poked. */
	err = ssb_pcmcia_cor_setup(bus, CISREG_COR + 0x80);
	if (err)
		return err;

	return 0;
}

void ssb_pcmcia_exit(struct ssb_bus *bus)
{
	if (bus->bustype != SSB_BUSTYPE_PCMCIA)
		return;

	device_remove_file(&bus->host_pcmcia->dev, &dev_attr_ssb_sprom);
}

int ssb_pcmcia_init(struct ssb_bus *bus)
{
	int err;

	if (bus->bustype != SSB_BUSTYPE_PCMCIA)
		return 0;

	err = ssb_pcmcia_hardware_setup(bus);
	if (err)
		goto error;

	bus->sprom_size = SSB_PCMCIA_SPROM_SIZE;
	mutex_init(&bus->sprom_mutex);
	err = device_create_file(&bus->host_pcmcia->dev, &dev_attr_ssb_sprom);
	if (err)
		goto error;

	return 0;
error:
	ssb_printk(KERN_ERR PFX "Failed to initialize PCMCIA host device\n");
	return err;
}
