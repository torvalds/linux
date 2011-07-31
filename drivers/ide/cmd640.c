/*
 *  Copyright (C) 1995-1996  Linus Torvalds & authors (see below)
 */

/*
 *  Original authors:	abramov@cecmow.enet.dec.com (Igor Abramov)
 *			mlord@pobox.com (Mark Lord)
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 *  This file provides support for the advanced features and bugs
 *  of IDE interfaces using the CMD Technologies 0640 IDE interface chip.
 *
 *  These chips are basically fucked by design, and getting this driver
 *  to work on every motherboard design that uses this screwed chip seems
 *  bloody well impossible.  However, we're still trying.
 *
 *  Version 0.97 worked for everybody.
 *
 *  User feedback is essential.  Many thanks to the beta test team:
 *
 *  A.Hartgers@stud.tue.nl, JZDQC@CUNYVM.CUNY.edu, abramov@cecmow.enet.dec.com,
 *  bardj@utopia.ppp.sn.no, bart@gaga.tue.nl, bbol001@cs.auckland.ac.nz,
 *  chrisc@dbass.demon.co.uk, dalecki@namu26.Num.Math.Uni-Goettingen.de,
 *  derekn@vw.ece.cmu.edu, florian@btp2x3.phy.uni-bayreuth.de,
 *  flynn@dei.unipd.it, gadio@netvision.net.il, godzilla@futuris.net,
 *  j@pobox.com, jkemp1@mises.uni-paderborn.de, jtoppe@hiwaay.net,
 *  kerouac@ssnet.com, meskes@informatik.rwth-aachen.de, hzoli@cs.elte.hu,
 *  peter@udgaard.isgtec.com, phil@tazenda.demon.co.uk, roadcapw@cfw.com,
 *  s0033las@sun10.vsz.bme.hu, schaffer@tam.cornell.edu, sjd@slip.net,
 *  steve@ei.org, ulrpeg@bigcomm.gun.de, ism@tardis.ed.ac.uk, mack@cray.com
 *  liug@mama.indstate.edu, and others.
 *
 *  Version 0.01	Initial version, hacked out of ide.c,
 *			and #include'd rather than compiled separately.
 *			This will get cleaned up in a subsequent release.
 *
 *  Version 0.02	Fixes for vlb initialization code, enable prefetch
 *			for versions 'B' and 'C' of chip by default,
 *			some code cleanup.
 *
 *  Version 0.03	Added reset of secondary interface,
 *			and black list for devices which are not compatible
 *			with prefetch mode. Separate function for setting
 *			prefetch is added, possibly it will be called some
 *			day from ioctl processing code.
 *
 *  Version 0.04	Now configs/compiles separate from ide.c
 *
 *  Version 0.05	Major rewrite of interface timing code.
 *			Added new function cmd640_set_mode to set PIO mode
 *			from ioctl call. New drives added to black list.
 *
 *  Version 0.06	More code cleanup. Prefetch is enabled only for
 *			detected hard drives, not included in prefetch
 *			black list.
 *
 *  Version 0.07	Changed to more conservative drive tuning policy.
 *			Unknown drives, which report PIO < 4 are set to
 *			(reported_PIO - 1) if it is supported, or to PIO0.
 *			List of known drives extended by info provided by
 *			CMD at their ftp site.
 *
 *  Version 0.08	Added autotune/noautotune support.
 *
 *  Version 0.09	Try to be smarter about 2nd port enabling.
 *  Version 0.10	Be nice and don't reset 2nd port.
 *  Version 0.11	Try to handle more weird situations.
 *
 *  Version 0.12	Lots of bug fixes from Laszlo Peter
 *			irq unmasking disabled for reliability.
 *			try to be even smarter about the second port.
 *			tidy up source code formatting.
 *  Version 0.13	permit irq unmasking again.
 *  Version 0.90	massive code cleanup, some bugs fixed.
 *			defaults all drives to PIO mode0, prefetch off.
 *			autotune is OFF by default, with compile time flag.
 *			prefetch can be turned OFF/ON using "hdparm -p8/-p9"
 *			 (requires hdparm-3.1 or newer)
 *  Version 0.91	first release to linux-kernel list.
 *  Version 0.92	move initial reg dump to separate callable function
 *			change "readahead" to "prefetch" to avoid confusion
 *  Version 0.95	respect original BIOS timings unless autotuning.
 *			tons of code cleanup and rearrangement.
 *			added CONFIG_BLK_DEV_CMD640_ENHANCED option
 *			prevent use of unmask when prefetch is on
 *  Version 0.96	prevent use of io_32bit when prefetch is off
 *  Version 0.97	fix VLB secondary interface for sjd@slip.net
 *			other minor tune-ups:  0.96 was very good.
 *  Version 0.98	ignore PCI version when disabled by BIOS
 *  Version 0.99	display setup/active/recovery clocks with PIO mode
 *  Version 1.00	Mmm.. cannot depend on PCMD_ENA in all systems
 *  Version 1.01	slow/fast devsel can be selected with "hdparm -p6/-p7"
 *			 ("fast" is necessary for 32bit I/O in some systems)
 *  Version 1.02	fix bug that resulted in slow "setup times"
 *			 (patch courtesy of Zoltan Hidvegi)
 */

#define CMD640_PREFETCH_MASKS 1

/*#define CMD640_DUMP_REGS */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#define DRV_NAME "cmd640"

static int cmd640_vlb;

/*
 * CMD640 specific registers definition.
 */

#define VID		0x00
#define DID		0x02
#define PCMD		0x04
#define   PCMD_ENA	0x01
#define PSTTS		0x06
#define REVID		0x08
#define PROGIF		0x09
#define SUBCL		0x0a
#define BASCL		0x0b
#define BaseA0		0x10
#define BaseA1		0x14
#define BaseA2		0x18
#define BaseA3		0x1c
#define INTLINE		0x3c
#define INPINE		0x3d

#define	CFR		0x50
#define   CFR_DEVREV		0x03
#define   CFR_IDE01INTR		0x04
#define	  CFR_DEVID		0x18
#define	  CFR_AT_VESA_078h	0x20
#define	  CFR_DSA1		0x40
#define	  CFR_DSA0		0x80

#define CNTRL		0x51
#define	  CNTRL_DIS_RA0		0x40
#define   CNTRL_DIS_RA1		0x80
#define	  CNTRL_ENA_2ND		0x08

#define	CMDTIM		0x52
#define	ARTTIM0		0x53
#define	DRWTIM0		0x54
#define ARTTIM1 	0x55
#define DRWTIM1		0x56
#define ARTTIM23	0x57
#define   ARTTIM23_DIS_RA2	0x04
#define   ARTTIM23_DIS_RA3	0x08
#define   ARTTIM23_IDE23INTR	0x10
#define DRWTIM23	0x58
#define BRST		0x59

/*
 * Registers and masks for easy access by drive index:
 */
static u8 prefetch_regs[4]  = {CNTRL, CNTRL, ARTTIM23, ARTTIM23};
static u8 prefetch_masks[4] = {CNTRL_DIS_RA0, CNTRL_DIS_RA1, ARTTIM23_DIS_RA2, ARTTIM23_DIS_RA3};

#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED

static u8 arttim_regs[4] = {ARTTIM0, ARTTIM1, ARTTIM23, ARTTIM23};
static u8 drwtim_regs[4] = {DRWTIM0, DRWTIM1, DRWTIM23, DRWTIM23};

/*
 * Current cmd640 timing values for each drive.
 * The defaults for each are the slowest possible timings.
 */
static u8 setup_counts[4]    = {4, 4, 4, 4};     /* Address setup count (in clocks) */
static u8 active_counts[4]   = {16, 16, 16, 16}; /* Active count   (encoded) */
static u8 recovery_counts[4] = {16, 16, 16, 16}; /* Recovery count (encoded) */

#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */

static DEFINE_SPINLOCK(cmd640_lock);

/*
 * Interface to access cmd640x registers
 */
static unsigned int cmd640_key;
static void (*__put_cmd640_reg)(u16 reg, u8 val);
static u8 (*__get_cmd640_reg)(u16 reg);

/*
 * This is read from the CFR reg, and is used in several places.
 */
static unsigned int cmd640_chip_version;

/*
 * The CMD640x chip does not support DWORD config write cycles, but some
 * of the BIOSes use them to implement the config services.
 * Therefore, we must use direct IO instead.
 */

/* PCI method 1 access */

static void put_cmd640_reg_pci1(u16 reg, u8 val)
{
	outl_p((reg & 0xfc) | cmd640_key, 0xcf8);
	outb_p(val, (reg & 3) | 0xcfc);
}

static u8 get_cmd640_reg_pci1(u16 reg)
{
	outl_p((reg & 0xfc) | cmd640_key, 0xcf8);
	return inb_p((reg & 3) | 0xcfc);
}

/* PCI method 2 access (from CMD datasheet) */

static void put_cmd640_reg_pci2(u16 reg, u8 val)
{
	outb_p(0x10, 0xcf8);
	outb_p(val, cmd640_key + reg);
	outb_p(0, 0xcf8);
}

static u8 get_cmd640_reg_pci2(u16 reg)
{
	u8 b;

	outb_p(0x10, 0xcf8);
	b = inb_p(cmd640_key + reg);
	outb_p(0, 0xcf8);
	return b;
}

/* VLB access */

static void put_cmd640_reg_vlb(u16 reg, u8 val)
{
	outb_p(reg, cmd640_key);
	outb_p(val, cmd640_key + 4);
}

static u8 get_cmd640_reg_vlb(u16 reg)
{
	outb_p(reg, cmd640_key);
	return inb_p(cmd640_key + 4);
}

static u8 get_cmd640_reg(u16 reg)
{
	unsigned long flags;
	u8 b;

	spin_lock_irqsave(&cmd640_lock, flags);
	b = __get_cmd640_reg(reg);
	spin_unlock_irqrestore(&cmd640_lock, flags);
	return b;
}

static void put_cmd640_reg(u16 reg, u8 val)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd640_lock, flags);
	__put_cmd640_reg(reg, val);
	spin_unlock_irqrestore(&cmd640_lock, flags);
}

static int __init match_pci_cmd640_device(void)
{
	const u8 ven_dev[4] = {0x95, 0x10, 0x40, 0x06};
	unsigned int i;
	for (i = 0; i < 4; i++) {
		if (get_cmd640_reg(i) != ven_dev[i])
			return 0;
	}
#ifdef STUPIDLY_TRUST_BROKEN_PCMD_ENA_BIT
	if ((get_cmd640_reg(PCMD) & PCMD_ENA) == 0) {
		printk("ide: cmd640 on PCI disabled by BIOS\n");
		return 0;
	}
#endif /* STUPIDLY_TRUST_BROKEN_PCMD_ENA_BIT */
	return 1; /* success */
}

/*
 * Probe for CMD640x -- pci method 1
 */
static int __init probe_for_cmd640_pci1(void)
{
	__get_cmd640_reg = get_cmd640_reg_pci1;
	__put_cmd640_reg = put_cmd640_reg_pci1;
	for (cmd640_key = 0x80000000;
	     cmd640_key <= 0x8000f800;
	     cmd640_key += 0x800) {
		if (match_pci_cmd640_device())
			return 1; /* success */
	}
	return 0;
}

/*
 * Probe for CMD640x -- pci method 2
 */
static int __init probe_for_cmd640_pci2(void)
{
	__get_cmd640_reg = get_cmd640_reg_pci2;
	__put_cmd640_reg = put_cmd640_reg_pci2;
	for (cmd640_key = 0xc000; cmd640_key <= 0xcf00; cmd640_key += 0x100) {
		if (match_pci_cmd640_device())
			return 1; /* success */
	}
	return 0;
}

/*
 * Probe for CMD640x -- vlb
 */
static int __init probe_for_cmd640_vlb(void)
{
	u8 b;

	__get_cmd640_reg = get_cmd640_reg_vlb;
	__put_cmd640_reg = put_cmd640_reg_vlb;
	cmd640_key = 0x178;
	b = get_cmd640_reg(CFR);
	if (b == 0xff || b == 0x00 || (b & CFR_AT_VESA_078h)) {
		cmd640_key = 0x78;
		b = get_cmd640_reg(CFR);
		if (b == 0xff || b == 0x00 || !(b & CFR_AT_VESA_078h))
			return 0;
	}
	return 1; /* success */
}

/*
 *  Returns 1 if an IDE interface/drive exists at 0x170,
 *  Returns 0 otherwise.
 */
static int __init secondary_port_responding(void)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd640_lock, flags);

	outb_p(0x0a, 0x176);	/* select drive0 */
	udelay(100);
	if ((inb_p(0x176) & 0x1f) != 0x0a) {
		outb_p(0x1a, 0x176); /* select drive1 */
		udelay(100);
		if ((inb_p(0x176) & 0x1f) != 0x1a) {
			spin_unlock_irqrestore(&cmd640_lock, flags);
			return 0; /* nothing responded */
		}
	}
	spin_unlock_irqrestore(&cmd640_lock, flags);
	return 1; /* success */
}

#ifdef CMD640_DUMP_REGS
/*
 * Dump out all cmd640 registers.  May be called from ide.c
 */
static void cmd640_dump_regs(void)
{
	unsigned int reg = cmd640_vlb ? 0x50 : 0x00;

	/* Dump current state of chip registers */
	printk("ide: cmd640 internal register dump:");
	for (; reg <= 0x59; reg++) {
		if (!(reg & 0x0f))
			printk("\n%04x:", reg);
		printk(" %02x", get_cmd640_reg(reg));
	}
	printk("\n");
}
#endif

static void __set_prefetch_mode(ide_drive_t *drive, int mode)
{
	if (mode) {	/* want prefetch on? */
#if CMD640_PREFETCH_MASKS
		drive->dev_flags |= IDE_DFLAG_NO_UNMASK;
		drive->dev_flags &= ~IDE_DFLAG_UNMASK;
#endif
		drive->dev_flags &= ~IDE_DFLAG_NO_IO_32BIT;
	} else {
		drive->dev_flags &= ~IDE_DFLAG_NO_UNMASK;
		drive->dev_flags |= IDE_DFLAG_NO_IO_32BIT;
		drive->io_32bit = 0;
	}
}

#ifndef CONFIG_BLK_DEV_CMD640_ENHANCED
/*
 * Check whether prefetch is on for a drive,
 * and initialize the unmask flags for safe operation.
 */
static void __init check_prefetch(ide_drive_t *drive, unsigned int index)
{
	u8 b = get_cmd640_reg(prefetch_regs[index]);

	__set_prefetch_mode(drive, (b & prefetch_masks[index]) ? 0 : 1);
}
#else

/*
 * Sets prefetch mode for a drive.
 */
static void set_prefetch_mode(ide_drive_t *drive, unsigned int index, int mode)
{
	unsigned long flags;
	int reg = prefetch_regs[index];
	u8 b;

	spin_lock_irqsave(&cmd640_lock, flags);
	b = __get_cmd640_reg(reg);
	__set_prefetch_mode(drive, mode);
	if (mode)
		b &= ~prefetch_masks[index];	/* enable prefetch */
	else
		b |= prefetch_masks[index];	/* disable prefetch */
	__put_cmd640_reg(reg, b);
	spin_unlock_irqrestore(&cmd640_lock, flags);
}

/*
 * Dump out current drive clocks settings
 */
static void display_clocks(unsigned int index)
{
	u8 active_count, recovery_count;

	active_count = active_counts[index];
	if (active_count == 1)
		++active_count;
	recovery_count = recovery_counts[index];
	if (active_count > 3 && recovery_count == 1)
		++recovery_count;
	if (cmd640_chip_version > 1)
		recovery_count += 1;  /* cmd640b uses (count + 1)*/
	printk(", clocks=%d/%d/%d\n", setup_counts[index], active_count, recovery_count);
}

/*
 * Pack active and recovery counts into single byte representation
 * used by controller
 */
static inline u8 pack_nibbles(u8 upper, u8 lower)
{
	return ((upper & 0x0f) << 4) | (lower & 0x0f);
}

/*
 * This routine writes the prepared setup/active/recovery counts
 * for a drive into the cmd640 chipset registers to active them.
 */
static void program_drive_counts(ide_drive_t *drive, unsigned int index)
{
	unsigned long flags;
	u8 setup_count    = setup_counts[index];
	u8 active_count   = active_counts[index];
	u8 recovery_count = recovery_counts[index];

	/*
	 * Set up address setup count and drive read/write timing registers.
	 * Primary interface has individual count/timing registers for
	 * each drive.  Secondary interface has one common set of registers,
	 * so we merge the timings, using the slowest value for each timing.
	 */
	if (index > 1) {
		ide_drive_t *peer = ide_get_pair_dev(drive);
		unsigned int mate = index ^ 1;

		if (peer) {
			if (setup_count < setup_counts[mate])
				setup_count = setup_counts[mate];
			if (active_count < active_counts[mate])
				active_count = active_counts[mate];
			if (recovery_count < recovery_counts[mate])
				recovery_count = recovery_counts[mate];
		}
	}

	/*
	 * Convert setup_count to internal chipset representation
	 */
	switch (setup_count) {
	case 4:	 setup_count = 0x00; break;
	case 3:	 setup_count = 0x80; break;
	case 1:
	case 2:	 setup_count = 0x40; break;
	default: setup_count = 0xc0; /* case 5 */
	}

	/*
	 * Now that everything is ready, program the new timings
	 */
	spin_lock_irqsave(&cmd640_lock, flags);
	/*
	 * Program the address_setup clocks into ARTTIM reg,
	 * and then the active/recovery counts into the DRWTIM reg
	 * (this converts counts of 16 into counts of zero -- okay).
	 */
	setup_count |= __get_cmd640_reg(arttim_regs[index]) & 0x3f;
	__put_cmd640_reg(arttim_regs[index], setup_count);
	__put_cmd640_reg(drwtim_regs[index], pack_nibbles(active_count, recovery_count));
	spin_unlock_irqrestore(&cmd640_lock, flags);
}

/*
 * Set a specific pio_mode for a drive
 */
static void cmd640_set_mode(ide_drive_t *drive, unsigned int index,
			    u8 pio_mode, unsigned int cycle_time)
{
	struct ide_timing *t;
	int setup_time, active_time, recovery_time, clock_time;
	u8 setup_count, active_count, recovery_count, recovery_count2, cycle_count;
	int bus_speed;

	if (cmd640_vlb)
		bus_speed = ide_vlb_clk ? ide_vlb_clk : 50;
	else
		bus_speed = ide_pci_clk ? ide_pci_clk : 33;

	if (pio_mode > 5)
		pio_mode = 5;

	t = ide_timing_find_mode(XFER_PIO_0 + pio_mode);
	setup_time  = t->setup;
	active_time = t->active;

	recovery_time = cycle_time - (setup_time + active_time);
	clock_time = 1000 / bus_speed;
	cycle_count = DIV_ROUND_UP(cycle_time, clock_time);

	setup_count = DIV_ROUND_UP(setup_time, clock_time);

	active_count = DIV_ROUND_UP(active_time, clock_time);
	if (active_count < 2)
		active_count = 2; /* minimum allowed by cmd640 */

	recovery_count = DIV_ROUND_UP(recovery_time, clock_time);
	recovery_count2 = cycle_count - (setup_count + active_count);
	if (recovery_count2 > recovery_count)
		recovery_count = recovery_count2;
	if (recovery_count < 2)
		recovery_count = 2; /* minimum allowed by cmd640 */
	if (recovery_count > 17) {
		active_count += recovery_count - 17;
		recovery_count = 17;
	}
	if (active_count > 16)
		active_count = 16; /* maximum allowed by cmd640 */
	if (cmd640_chip_version > 1)
		recovery_count -= 1;  /* cmd640b uses (count + 1)*/
	if (recovery_count > 16)
		recovery_count = 16; /* maximum allowed by cmd640 */

	setup_counts[index]    = setup_count;
	active_counts[index]   = active_count;
	recovery_counts[index] = recovery_count;

	/*
	 * In a perfect world, we might set the drive pio mode here
	 * (using WIN_SETFEATURE) before continuing.
	 *
	 * But we do not, because:
	 *	1) this is the wrong place to do it (proper is do_special() in ide.c)
	 * 	2) in practice this is rarely, if ever, necessary
	 */
	program_drive_counts(drive, index);
}

static void cmd640_set_pio_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	unsigned int index = 0, cycle_time;
	const u8 pio = drive->pio_mode - XFER_PIO_0;
	u8 b;

	switch (pio) {
	case 6: /* set fast-devsel off */
	case 7: /* set fast-devsel on */
		b = get_cmd640_reg(CNTRL) & ~0x27;
		if (pio & 1)
			b |= 0x27;
		put_cmd640_reg(CNTRL, b);
		printk("%s: %sabled cmd640 fast host timing (devsel)\n",
			drive->name, (pio & 1) ? "en" : "dis");
		return;
	case 8: /* set prefetch off */
	case 9: /* set prefetch on */
		set_prefetch_mode(drive, index, pio & 1);
		printk("%s: %sabled cmd640 prefetch\n",
			drive->name, (pio & 1) ? "en" : "dis");
		return;
	}

	cycle_time = ide_pio_cycle_time(drive, pio);
	cmd640_set_mode(drive, index, pio, cycle_time);

	printk("%s: selected cmd640 PIO mode%d (%dns)",
		drive->name, pio, cycle_time);

	display_clocks(index);
}
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */

static void __init cmd640_init_dev(ide_drive_t *drive)
{
	unsigned int i = drive->hwif->channel * 2 + (drive->dn & 1);

#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
	/*
	 * Reset timing to the slowest speed and turn off prefetch.
	 * This way, the drive identify code has a better chance.
	 */
	setup_counts[i]    =  4;	/* max possible */
	active_counts[i]   = 16;	/* max possible */
	recovery_counts[i] = 16;	/* max possible */
	program_drive_counts(drive, i);
	set_prefetch_mode(drive, i, 0);
	printk(KERN_INFO DRV_NAME ": drive%d timings/prefetch cleared\n", i);
#else
	/*
	 * Set the drive unmask flags to match the prefetch setting.
	 */
	check_prefetch(drive, i);
	printk(KERN_INFO DRV_NAME ": drive%d timings/prefetch(%s) preserved\n",
		i, (drive->dev_flags & IDE_DFLAG_NO_IO_32BIT) ? "off" : "on");
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */
}

static int cmd640_test_irq(ide_hwif_t *hwif)
{
	int irq_reg		= hwif->channel ? ARTTIM23 : CFR;
	u8  irq_mask		= hwif->channel ? ARTTIM23_IDE23INTR :
						  CFR_IDE01INTR;
	u8  irq_stat		= get_cmd640_reg(irq_reg);

	return (irq_stat & irq_mask) ? 1 : 0;
}

static const struct ide_port_ops cmd640_port_ops = {
	.init_dev		= cmd640_init_dev,
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
	.set_pio_mode		= cmd640_set_pio_mode,
#endif
	.test_irq		= cmd640_test_irq,
};

static int pci_conf1(void)
{
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&cmd640_lock, flags);
	outb(0x01, 0xCFB);
	tmp = inl(0xCF8);
	outl(0x80000000, 0xCF8);
	if (inl(0xCF8) == 0x80000000) {
		outl(tmp, 0xCF8);
		spin_unlock_irqrestore(&cmd640_lock, flags);
		return 1;
	}
	outl(tmp, 0xCF8);
	spin_unlock_irqrestore(&cmd640_lock, flags);
	return 0;
}

static int pci_conf2(void)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd640_lock, flags);
	outb(0x00, 0xCFB);
	outb(0x00, 0xCF8);
	outb(0x00, 0xCFA);
	if (inb(0xCF8) == 0x00 && inb(0xCF8) == 0x00) {
		spin_unlock_irqrestore(&cmd640_lock, flags);
		return 1;
	}
	spin_unlock_irqrestore(&cmd640_lock, flags);
	return 0;
}

static const struct ide_port_info cmd640_port_info __initdata = {
	.chipset		= ide_cmd640,
	.host_flags		= IDE_HFLAG_SERIALIZE |
				  IDE_HFLAG_NO_DMA |
				  IDE_HFLAG_ABUSE_PREFETCH |
				  IDE_HFLAG_ABUSE_FAST_DEVSEL,
	.port_ops		= &cmd640_port_ops,
	.pio_mask		= ATA_PIO5,
};

static int cmd640x_init_one(unsigned long base, unsigned long ctl)
{
	if (!request_region(base, 8, DRV_NAME)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX not free.\n",
				DRV_NAME, base, base + 7);
		return -EBUSY;
	}

	if (!request_region(ctl, 1, DRV_NAME)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX not free.\n",
				DRV_NAME, ctl);
		release_region(base, 8);
		return -EBUSY;
	}

	return 0;
}

/*
 * Probe for a cmd640 chipset, and initialize it if found.
 */
static int __init cmd640x_init(void)
{
	int second_port_cmd640 = 0, rc;
	const char *bus_type, *port2;
	u8 b, cfr;
	struct ide_hw hw[2], *hws[2];

	if (cmd640_vlb && probe_for_cmd640_vlb()) {
		bus_type = "VLB";
	} else {
		cmd640_vlb = 0;
		/* Find out what kind of PCI probing is supported otherwise
		   Justin Gibbs will sulk.. */
		if (pci_conf1() && probe_for_cmd640_pci1())
			bus_type = "PCI (type1)";
		else if (pci_conf2() && probe_for_cmd640_pci2())
			bus_type = "PCI (type2)";
		else
			return 0;
	}
	/*
	 * Undocumented magic (there is no 0x5b reg in specs)
	 */
	put_cmd640_reg(0x5b, 0xbd);
	if (get_cmd640_reg(0x5b) != 0xbd) {
		printk(KERN_ERR "ide: cmd640 init failed: wrong value in reg 0x5b\n");
		return 0;
	}
	put_cmd640_reg(0x5b, 0);

#ifdef CMD640_DUMP_REGS
	cmd640_dump_regs();
#endif

	/*
	 * Documented magic begins here
	 */
	cfr = get_cmd640_reg(CFR);
	cmd640_chip_version = cfr & CFR_DEVREV;
	if (cmd640_chip_version == 0) {
		printk("ide: bad cmd640 revision: %d\n", cmd640_chip_version);
		return 0;
	}

	rc = cmd640x_init_one(0x1f0, 0x3f6);
	if (rc)
		return rc;

	rc = cmd640x_init_one(0x170, 0x376);
	if (rc) {
		release_region(0x3f6, 1);
		release_region(0x1f0, 8);
		return rc;
	}

	memset(&hw, 0, sizeof(hw));

	ide_std_init_ports(&hw[0], 0x1f0, 0x3f6);
	hw[0].irq = 14;

	ide_std_init_ports(&hw[1], 0x170, 0x376);
	hw[1].irq = 15;

	printk(KERN_INFO "cmd640: buggy cmd640%c interface on %s, config=0x%02x"
			 "\n", 'a' + cmd640_chip_version - 1, bus_type, cfr);

	/*
	 * Initialize data for primary port
	 */
	hws[0] = &hw[0];

	/*
	 * Ensure compatibility by always using the slowest timings
	 * for access to the drive's command register block,
	 * and reset the prefetch burstsize to default (512 bytes).
	 *
	 * Maybe we need a way to NOT do these on *some* systems?
	 */
	put_cmd640_reg(CMDTIM, 0);
	put_cmd640_reg(BRST, 0x40);

	b = get_cmd640_reg(CNTRL);

	/*
	 * Try to enable the secondary interface, if not already enabled
	 */
	if (secondary_port_responding()) {
		if ((b & CNTRL_ENA_2ND)) {
			second_port_cmd640 = 1;
			port2 = "okay";
		} else if (cmd640_vlb) {
			second_port_cmd640 = 1;
			port2 = "alive";
		} else
			port2 = "not cmd640";
	} else {
		put_cmd640_reg(CNTRL, b ^ CNTRL_ENA_2ND); /* toggle the bit */
		if (secondary_port_responding()) {
			second_port_cmd640 = 1;
			port2 = "enabled";
		} else {
			put_cmd640_reg(CNTRL, b); /* restore original setting */
			port2 = "not responding";
		}
	}

	/*
	 * Initialize data for secondary cmd640 port, if enabled
	 */
	if (second_port_cmd640)
		hws[1] = &hw[1];

	printk(KERN_INFO "cmd640: %sserialized, secondary interface %s\n",
			 second_port_cmd640 ? "" : "not ", port2);

#ifdef CMD640_DUMP_REGS
	cmd640_dump_regs();
#endif

	return ide_host_add(&cmd640_port_info, hws, second_port_cmd640 ? 2 : 1,
			    NULL);
}

module_param_named(probe_vlb, cmd640_vlb, bool, 0);
MODULE_PARM_DESC(probe_vlb, "probe for VLB version of CMD640 chipset");

module_init(cmd640x_init);

MODULE_LICENSE("GPL");
