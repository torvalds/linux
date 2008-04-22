/*
 *  Copyright (C) 1995-1996  Linus Torvalds & authors (see below)
 */

/*
 *  Original authors:	abramov@cecmow.enet.dec.com (Igor Abramov)
 *  			mlord@pobox.com (Mark Lord)
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

//#define CMD640_DUMP_REGS

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

/*
 * This flag is set in ide.c by the parameter:  ide0=cmd640_vlb
 */
int cmd640_vlb = 0;

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
 * These are initialized to point at the devices we control
 */
static ide_hwif_t  *cmd_hwif0, *cmd_hwif1;
static ide_drive_t *cmd_drives[4];

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

static void put_cmd640_reg_pci1 (u16 reg, u8 val)
{
	outl_p((reg & 0xfc) | cmd640_key, 0xcf8);
	outb_p(val, (reg & 3) | 0xcfc);
}

static u8 get_cmd640_reg_pci1 (u16 reg)
{
	outl_p((reg & 0xfc) | cmd640_key, 0xcf8);
	return inb_p((reg & 3) | 0xcfc);
}

/* PCI method 2 access (from CMD datasheet) */

static void put_cmd640_reg_pci2 (u16 reg, u8 val)
{
	outb_p(0x10, 0xcf8);
	outb_p(val, cmd640_key + reg);
	outb_p(0, 0xcf8);
}

static u8 get_cmd640_reg_pci2 (u16 reg)
{
	u8 b;

	outb_p(0x10, 0xcf8);
	b = inb_p(cmd640_key + reg);
	outb_p(0, 0xcf8);
	return b;
}

/* VLB access */

static void put_cmd640_reg_vlb (u16 reg, u8 val)
{
	outb_p(reg, cmd640_key);
	outb_p(val, cmd640_key + 4);
}

static u8 get_cmd640_reg_vlb (u16 reg)
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
	__put_cmd640_reg(reg,val);
	spin_unlock_irqrestore(&cmd640_lock, flags);
}

static int __init match_pci_cmd640_device (void)
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
static int __init probe_for_cmd640_pci1 (void)
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
static int __init probe_for_cmd640_pci2 (void)
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
static int __init probe_for_cmd640_vlb (void)
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
static int __init secondary_port_responding (void)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd640_lock, flags);

	outb_p(0x0a, 0x170 + IDE_SELECT_OFFSET);	/* select drive0 */
	udelay(100);
	if ((inb_p(0x170 + IDE_SELECT_OFFSET) & 0x1f) != 0x0a) {
		outb_p(0x1a, 0x170 + IDE_SELECT_OFFSET); /* select drive1 */
		udelay(100);
		if ((inb_p(0x170 + IDE_SELECT_OFFSET) & 0x1f) != 0x1a) {
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
static void cmd640_dump_regs (void)
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

/*
 * Check whether prefetch is on for a drive,
 * and initialize the unmask flags for safe operation.
 */
static void __init check_prefetch (unsigned int index)
{
	ide_drive_t *drive = cmd_drives[index];
	u8 b = get_cmd640_reg(prefetch_regs[index]);

	if (b & prefetch_masks[index]) {	/* is prefetch off? */
		drive->no_unmask = 0;
		drive->no_io_32bit = 1;
		drive->io_32bit = 0;
	} else {
#if CMD640_PREFETCH_MASKS
		drive->no_unmask = 1;
		drive->unmask = 0;
#endif
		drive->no_io_32bit = 0;
	}
}

/*
 * Figure out which devices we control
 */
static void __init setup_device_ptrs (void)
{
	cmd_hwif0 = &ide_hwifs[0];
	cmd_hwif1 = &ide_hwifs[1];

	cmd_drives[0] = &cmd_hwif0->drives[0];
	cmd_drives[1] = &cmd_hwif0->drives[1];
	cmd_drives[2] = &cmd_hwif1->drives[0];
	cmd_drives[3] = &cmd_hwif1->drives[1];
}

#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED

/*
 * Sets prefetch mode for a drive.
 */
static void set_prefetch_mode (unsigned int index, int mode)
{
	ide_drive_t *drive = cmd_drives[index];
	unsigned long flags;
	int reg = prefetch_regs[index];
	u8 b;

	spin_lock_irqsave(&cmd640_lock, flags);
	b = __get_cmd640_reg(reg);
	if (mode) {	/* want prefetch on? */
#if CMD640_PREFETCH_MASKS
		drive->no_unmask = 1;
		drive->unmask = 0;
#endif
		drive->no_io_32bit = 0;
		b &= ~prefetch_masks[index];	/* enable prefetch */
	} else {
		drive->no_unmask = 0;
		drive->no_io_32bit = 1;
		drive->io_32bit = 0;
		b |= prefetch_masks[index];	/* disable prefetch */
	}
	__put_cmd640_reg(reg, b);
	spin_unlock_irqrestore(&cmd640_lock, flags);
}

/*
 * Dump out current drive clocks settings
 */
static void display_clocks (unsigned int index)
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
static inline u8 pack_nibbles (u8 upper, u8 lower)
{
	return ((upper & 0x0f) << 4) | (lower & 0x0f);
}

/*
 * This routine retrieves the initial drive timings from the chipset.
 */
static void __init retrieve_drive_counts (unsigned int index)
{
	u8 b;

	/*
	 * Get the internal setup timing, and convert to clock count
	 */
	b = get_cmd640_reg(arttim_regs[index]) & ~0x3f;
	switch (b) {
		case 0x00: b = 4; break;
		case 0x80: b = 3; break;
		case 0x40: b = 2; break;
		default:   b = 5; break;
	}
	setup_counts[index] = b;

	/*
	 * Get the active/recovery counts
	 */
	b = get_cmd640_reg(drwtim_regs[index]);
	active_counts[index]   = (b >> 4)   ? (b >> 4)   : 0x10;
	recovery_counts[index] = (b & 0x0f) ? (b & 0x0f) : 0x10;
}


/*
 * This routine writes the prepared setup/active/recovery counts
 * for a drive into the cmd640 chipset registers to active them.
 */
static void program_drive_counts (unsigned int index)
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
		unsigned int mate;
		if (cmd_drives[mate = index ^ 1]->present) {
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
static void cmd640_set_mode (unsigned int index, u8 pio_mode, unsigned int cycle_time)
{
	int setup_time, active_time, recovery_time, clock_time;
	u8 setup_count, active_count, recovery_count, recovery_count2, cycle_count;
	int bus_speed = system_bus_clock();

	if (pio_mode > 5)
		pio_mode = 5;
	setup_time  = ide_pio_timings[pio_mode].setup_time;
	active_time = ide_pio_timings[pio_mode].active_time;
	recovery_time = cycle_time - (setup_time + active_time);
	clock_time = 1000 / bus_speed;
	cycle_count = (cycle_time + clock_time - 1) / clock_time;

	setup_count = (setup_time + clock_time - 1) / clock_time;

	active_count = (active_time + clock_time - 1) / clock_time;
	if (active_count < 2)
		active_count = 2; /* minimum allowed by cmd640 */

	recovery_count = (recovery_time + clock_time - 1) / clock_time;
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
	program_drive_counts (index);
}

static void cmd640_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	unsigned int index = 0, cycle_time;
	u8 b;

	while (drive != cmd_drives[index]) {
		if (++index > 3) {
			printk(KERN_ERR "%s: bad news in %s\n",
					drive->name, __FUNCTION__);
			return;
		}
	}
	switch (pio) {
		case 6: /* set fast-devsel off */
		case 7: /* set fast-devsel on */
			b = get_cmd640_reg(CNTRL) & ~0x27;
			if (pio & 1)
				b |= 0x27;
			put_cmd640_reg(CNTRL, b);
			printk("%s: %sabled cmd640 fast host timing (devsel)\n", drive->name, (pio & 1) ? "en" : "dis");
			return;

		case 8: /* set prefetch off */
		case 9: /* set prefetch on */
			set_prefetch_mode(index, pio & 1);
			printk("%s: %sabled cmd640 prefetch\n", drive->name, (pio & 1) ? "en" : "dis");
			return;
	}

	cycle_time = ide_pio_cycle_time(drive, pio);
	cmd640_set_mode(index, pio, cycle_time);

	printk("%s: selected cmd640 PIO mode%d (%dns)",
		drive->name, pio, cycle_time);

	display_clocks(index);
}

#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */

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
				  IDE_HFLAG_NO_AUTOTUNE |
				  IDE_HFLAG_ABUSE_PREFETCH |
				  IDE_HFLAG_ABUSE_FAST_DEVSEL,
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
	.pio_mask		= ATA_PIO5,
#endif
};

/*
 * Probe for a cmd640 chipset, and initialize it if found.
 */
static int __init cmd640x_init(void)
{
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
	int second_port_toggled = 0;
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */
	int second_port_cmd640 = 0;
	const char *bus_type, *port2;
	unsigned int index;
	u8 b, cfr;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };
	hw_regs_t hw[2];

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
		printk ("ide: bad cmd640 revision: %d\n", cmd640_chip_version);
		return 0;
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
	setup_device_ptrs ();

	ide_init_port_hw(cmd_hwif0, &hw[0]);
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
	cmd_hwif0->set_pio_mode = &cmd640_set_pio_mode;
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */

	idx[0] = cmd_hwif0->index;

	/*
	 * Ensure compatibility by always using the slowest timings
	 * for access to the drive's command register block,
	 * and reset the prefetch burstsize to default (512 bytes).
	 *
	 * Maybe we need a way to NOT do these on *some* systems?
	 */
	put_cmd640_reg(CMDTIM, 0);
	put_cmd640_reg(BRST, 0x40);

	/*
	 * Try to enable the secondary interface, if not already enabled
	 */
	if (cmd_hwif1->drives[0].noprobe && cmd_hwif1->drives[1].noprobe) {
		port2 = "not probed";
	} else {
		b = get_cmd640_reg(CNTRL);
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
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
				second_port_toggled = 1;
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */
				port2 = "enabled";
			} else {
				put_cmd640_reg(CNTRL, b); /* restore original setting */
				port2 = "not responding";
			}
		}
	}

	/*
	 * Initialize data for secondary cmd640 port, if enabled
	 */
	if (second_port_cmd640) {
		ide_init_port_hw(cmd_hwif1, &hw[1]);
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
		cmd_hwif1->set_pio_mode = &cmd640_set_pio_mode;
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */

		idx[1] = cmd_hwif1->index;
	}
	printk(KERN_INFO "%s: %sserialized, secondary interface %s\n", cmd_hwif1->name,
			 second_port_cmd640 ? "" : "not ", port2);

	/*
	 * Establish initial timings/prefetch for all drives.
	 * Do not unnecessarily disturb any prior BIOS setup of these.
	 */
	for (index = 0; index < (2 + (second_port_cmd640 << 1)); index++) {
		ide_drive_t *drive = cmd_drives[index];
#ifdef CONFIG_BLK_DEV_CMD640_ENHANCED
		if (drive->autotune || ((index > 1) && second_port_toggled)) {
	 		/*
	 		 * Reset timing to the slowest speed and turn off prefetch.
			 * This way, the drive identify code has a better chance.
			 */
			setup_counts    [index] = 4;	/* max possible */
			active_counts   [index] = 16;	/* max possible */
			recovery_counts [index] = 16;	/* max possible */
			program_drive_counts (index);
			set_prefetch_mode (index, 0);
			printk("cmd640: drive%d timings/prefetch cleared\n", index);
		} else {
			/*
			 * Record timings/prefetch without changing them.
			 * This preserves any prior BIOS setup.
			 */
			retrieve_drive_counts (index);
			check_prefetch (index);
			printk("cmd640: drive%d timings/prefetch(%s) preserved",
				index, drive->no_io_32bit ? "off" : "on");
			display_clocks(index);
		}
#else
		/*
		 * Set the drive unmask flags to match the prefetch setting
		 */
		check_prefetch (index);
		printk("cmd640: drive%d timings/prefetch(%s) preserved\n",
			index, drive->no_io_32bit ? "off" : "on");
#endif /* CONFIG_BLK_DEV_CMD640_ENHANCED */
	}

#ifdef CMD640_DUMP_REGS
	cmd640_dump_regs();
#endif

	ide_device_add(idx, &cmd640_port_info);

	return 1;
}

module_param_named(probe_vlb, cmd640_vlb, bool, 0);
MODULE_PARM_DESC(probe_vlb, "probe for VLB version of CMD640 chipset");

module_init(cmd640x_init);

MODULE_LICENSE("GPL");
