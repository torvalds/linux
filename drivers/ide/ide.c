/*
 *  linux/drivers/ide/ide.c		Version 7.00beta2	Mar 05 2003
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 */

/*
 *  Mostly written by Mark Lord  <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs
 *   (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary:    ide0, port 0x1f0; major=3;  hda is minor=0; hdb is minor=64
 * Secondary:  ide1, port 0x170; major=22; hdc is minor=0; hdd is minor=64
 * Tertiary:   ide2, port 0x???; major=33; hde is minor=0; hdf is minor=64
 * Quaternary: ide3, port 0x???; major=34; hdg is minor=0; hdh is minor=64
 * ...
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@ieee.org)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 *
 *  Version 1.0 ALPHA	initial code, primary i/f working okay
 *  Version 1.3 BETA	dual i/f on shared irq tested & working!
 *  Version 1.4 BETA	added auto probing for irq(s)
 *  Version 1.5 BETA	added ALPHA (untested) support for IDE cd-roms,
 *  ...
 * Version 5.50		allow values as small as 20 for idebus=
 * Version 5.51		force non io_32bit in drive_cmd_intr()
 *			change delay_10ms() to delay_50ms() to fix problems
 * Version 5.52		fix incorrect invalidation of removable devices
 *			add "hdx=slow" command line option
 * Version 5.60		start to modularize the driver; the disk and ATAPI
 *			 drivers can be compiled as loadable modules.
 *			move IDE probe code to ide-probe.c
 *			move IDE disk code to ide-disk.c
 *			add support for generic IDE device subdrivers
 *			add m68k code from Geert Uytterhoeven
 *			probe all interfaces by default
 *			add ioctl to (re)probe an interface
 * Version 6.00		use per device request queues
 *			attempt to optimize shared hwgroup performance
 *			add ioctl to manually adjust bandwidth algorithms
 *			add kerneld support for the probe module
 *			fix bug in ide_error()
 *			fix bug in the first ide_get_lock() call for Atari
 *			don't flush leftover data for ATAPI devices
 * Version 6.01		clear hwgroup->active while the hwgroup sleeps
 *			support HDIO_GETGEO for floppies
 * Version 6.02		fix ide_ack_intr() call
 *			check partition table on floppies
 * Version 6.03		handle bad status bit sequencing in ide_wait_stat()
 * Version 6.10		deleted old entries from this list of updates
 *			replaced triton.c with ide-dma.c generic PCI DMA
 *			added support for BIOS-enabled UltraDMA
 *			rename all "promise" things to "pdc4030"
 *			fix EZ-DRIVE handling on small disks
 * Version 6.11		fix probe error in ide_scan_devices()
 *			fix ancient "jiffies" polling bugs
 *			mask all hwgroup interrupts on each irq entry
 * Version 6.12		integrate ioctl and proc interfaces
 *			fix parsing of "idex=" command line parameter
 * Version 6.13		add support for ide4/ide5 courtesy rjones@orchestream.com
 * Version 6.14		fixed IRQ sharing among PCI devices
 * Version 6.15		added SMP awareness to IDE drivers
 * Version 6.16		fixed various bugs; even more SMP friendly
 * Version 6.17		fix for newest EZ-Drive problem
 * Version 6.18		default unpartitioned-disk translation now "BIOS LBA"
 * Version 6.19		Re-design for a UNIFORM driver for all platforms,
 *			  model based on suggestions from Russell King and
 *			  Geert Uytterhoeven
 *			Promise DC4030VL now supported.
 *			add support for ide6/ide7
 *			delay_50ms() changed to ide_delay_50ms() and exported.
 * Version 6.20		Added/Fixed Generic ATA-66 support and hwif detection.
 *			Added hdx=flash to allow for second flash disk
 *			  detection w/o the hang loop.
 *			Added support for ide8/ide9
 *			Added idex=ata66 for the quirky chipsets that are
 *			  ATA-66 compliant, but have yet to determine a method
 *			  of verification of the 80c cable presence.
 *			  Specifically Promise's PDC20262 chipset.
 * Version 6.21		Fixing/Fixed SMP spinlock issue with insight from an old
 *			  hat that clarified original low level driver design.
 * Version 6.30		Added SMP support; fixed multmode issues.  -ml
 * Version 6.31		Debug Share INTR's and request queue streaming
 *			Native ATA-100 support
 *			Prep for Cascades Project
 * Version 7.00alpha	First named revision of ide rearrange
 *
 *  Some additional driver compile-time options are in ./include/linux/ide.h
 *
 *  To do, in likely order of completion:
 *	- modify kernel to obtain BIOS geometry for drives on 2nd/3rd/4th i/f
 *
 */

#define	REVISION	"Revision: 7.00alpha2"
#define	VERSION		"Id: ide.c 7.00a2 20020906"

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#define _IDE_C			/* Tell ide.h it's really us */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>


/* default maximum number of failures */
#define IDE_DEFAULT_MAX_FAILURES 	1

static const u8 ide_hwif_to_major[] = { IDE0_MAJOR, IDE1_MAJOR,
					IDE2_MAJOR, IDE3_MAJOR,
					IDE4_MAJOR, IDE5_MAJOR,
					IDE6_MAJOR, IDE7_MAJOR,
					IDE8_MAJOR, IDE9_MAJOR };

static int idebus_parameter;	/* holds the "idebus=" parameter */
static int system_bus_speed;	/* holds what we think is VESA/PCI bus speed */
static int initializing;	/* set while initializing built-in drivers */

DECLARE_MUTEX(ide_cfg_sem);
 __cacheline_aligned_in_smp DEFINE_SPINLOCK(ide_lock);

#ifdef CONFIG_BLK_DEV_IDEPCI
static int ide_scan_direction; /* THIS was formerly 2.2.x pci=reverse */
#endif

#ifdef CONFIG_IDEDMA_AUTO
int noautodma = 0;
#else
int noautodma = 1;
#endif

EXPORT_SYMBOL(noautodma);

/*
 * This is declared extern in ide.h, for access by other IDE modules:
 */
ide_hwif_t ide_hwifs[MAX_HWIFS];	/* master data repository */

EXPORT_SYMBOL(ide_hwifs);

/*
 * Do not even *think* about calling this!
 */
static void init_hwif_data(ide_hwif_t *hwif, unsigned int index)
{
	unsigned int unit;

	/* bulk initialize hwif & drive info with zeros */
	memset(hwif, 0, sizeof(ide_hwif_t));

	/* fill in any non-zero initial values */
	hwif->index	= index;
	hwif->major	= ide_hwif_to_major[index];

	hwif->name[0]	= 'i';
	hwif->name[1]	= 'd';
	hwif->name[2]	= 'e';
	hwif->name[3]	= '0' + index;

	hwif->bus_state	= BUSSTATE_ON;

	hwif->atapi_dma = 0;		/* disable all atapi dma */ 
	hwif->ultra_mask = 0x80;	/* disable all ultra */
	hwif->mwdma_mask = 0x80;	/* disable all mwdma */
	hwif->swdma_mask = 0x80;	/* disable all swdma */

	init_completion(&hwif->gendev_rel_comp);

	default_hwif_iops(hwif);
	default_hwif_transport(hwif);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		drive->media			= ide_disk;
		drive->select.all		= (unit<<4)|0xa0;
		drive->hwif			= hwif;
		drive->ctl			= 0x08;
		drive->ready_stat		= READY_STAT;
		drive->bad_wstat		= BAD_W_STAT;
		drive->special.b.recalibrate	= 1;
		drive->special.b.set_geometry	= 1;
		drive->name[0]			= 'h';
		drive->name[1]			= 'd';
		drive->name[2]			= 'a' + (index * MAX_DRIVES) + unit;
		drive->max_failures		= IDE_DEFAULT_MAX_FAILURES;
		drive->using_dma		= 0;
		drive->is_flash			= 0;
		drive->vdma			= 0;
		INIT_LIST_HEAD(&drive->list);
		init_completion(&drive->gendev_rel_comp);
	}
}

static void init_hwif_default(ide_hwif_t *hwif, unsigned int index)
{
	hw_regs_t hw;

	memset(&hw, 0, sizeof(hw_regs_t));

	ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, &hwif->irq);

	memcpy(&hwif->hw, &hw, sizeof(hw));
	memcpy(hwif->io_ports, hw.io_ports, sizeof(hw.io_ports));

	hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		hwif->noprobe = 1;	/* may be overridden by ide_setup() */
#endif
}

extern void ide_arm_init(void);

/*
 * init_ide_data() sets reasonable default values into all fields
 * of all instances of the hwifs and drives, but only on the first call.
 * Subsequent calls have no effect (they don't wipe out anything).
 *
 * This routine is normally called at driver initialization time,
 * but may also be called MUCH earlier during kernel "command-line"
 * parameter processing.  As such, we cannot depend on any other parts
 * of the kernel (such as memory allocation) to be functioning yet.
 *
 * This is too bad, as otherwise we could dynamically allocate the
 * ide_drive_t structs as needed, rather than always consuming memory
 * for the max possible number (MAX_HWIFS * MAX_DRIVES) of them.
 *
 * FIXME: We should stuff the setup data into __init and copy the
 * relevant hwifs/allocate them properly during boot.
 */
#define MAGIC_COOKIE 0x12345678
static void __init init_ide_data (void)
{
	ide_hwif_t *hwif;
	unsigned int index;
	static unsigned long magic_cookie = MAGIC_COOKIE;

	if (magic_cookie != MAGIC_COOKIE)
		return;		/* already initialized */
	magic_cookie = 0;

	/* Initialise all interface structures */
	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = &ide_hwifs[index];
		init_hwif_data(hwif, index);
		init_hwif_default(hwif, index);
#if !defined(CONFIG_PPC32) || !defined(CONFIG_PCI)
		hwif->irq = hwif->hw.irq =
			ide_init_default_irq(hwif->io_ports[IDE_DATA_OFFSET]);
#endif
	}
#ifdef CONFIG_IDE_ARM
	initializing = 1;
	ide_arm_init();
	initializing = 0;
#endif
}

/**
 *	ide_system_bus_speed	-	guess bus speed
 *
 *	ide_system_bus_speed() returns what we think is the system VESA/PCI
 *	bus speed (in MHz). This is used for calculating interface PIO timings.
 *	The default is 40 for known PCI systems, 50 otherwise.
 *	The "idebus=xx" parameter can be used to override this value.
 *	The actual value to be used is computed/displayed the first time
 *	through. Drivers should only use this as a last resort.
 *
 *	Returns a guessed speed in MHz.
 */

static int ide_system_bus_speed(void)
{
#ifdef CONFIG_PCI
	static struct pci_device_id pci_default[] = {
		{ PCI_DEVICE(PCI_ANY_ID, PCI_ANY_ID) },
		{ }
	};
#else
#define pci_default 0
#endif /* CONFIG_PCI */

	if (!system_bus_speed) {
		if (idebus_parameter) {
			/* user supplied value */
			system_bus_speed = idebus_parameter;
		} else if (pci_dev_present(pci_default)) {
			/* safe default value for PCI */
			system_bus_speed = 33;
		} else {
			/* safe default value for VESA and PCI */
			system_bus_speed = 50;
		}
		printk(KERN_INFO "ide: Assuming %dMHz system bus speed "
			"for PIO modes%s\n", system_bus_speed,
			idebus_parameter ? "" : "; override with idebus=xx");
	}
	return system_bus_speed;
}

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *proc_ide_root;
#endif

static struct resource* hwif_request_region(ide_hwif_t *hwif,
					    unsigned long addr, int num)
{
	struct resource *res = request_region(addr, num, hwif->name);

	if (!res)
		printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX not free.\n",
				hwif->name, addr, addr+num-1);
	return res;
}

/**
 *	ide_hwif_request_regions - request resources for IDE
 *	@hwif: interface to use
 *
 *	Requests all the needed resources for an interface.
 *	Right now core IDE code does this work which is deeply wrong.
 *	MMIO leaves it to the controller driver,
 *	PIO will migrate this way over time.
 */

int ide_hwif_request_regions(ide_hwif_t *hwif)
{
	unsigned long addr;
	unsigned int i;

	if (hwif->mmio == 2)
		return 0;
	BUG_ON(hwif->mmio == 1);
	addr = hwif->io_ports[IDE_CONTROL_OFFSET];
	if (addr && !hwif_request_region(hwif, addr, 1))
		goto control_region_busy;
	hwif->straight8 = 0;
	addr = hwif->io_ports[IDE_DATA_OFFSET];
	if ((addr | 7) == hwif->io_ports[IDE_STATUS_OFFSET]) {
		if (!hwif_request_region(hwif, addr, 8))
			goto data_region_busy;
		hwif->straight8 = 1;
		return 0;
	}
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		addr = hwif->io_ports[i];
		if (!hwif_request_region(hwif, addr, 1)) {
			while (--i)
				release_region(addr, 1);
			goto data_region_busy;
		}
	}
	return 0;

data_region_busy:
	addr = hwif->io_ports[IDE_CONTROL_OFFSET];
	if (addr)
		release_region(addr, 1);
control_region_busy:
	/* If any errors are return, we drop the hwif interface. */
	return -EBUSY;
}

/**
 *	ide_hwif_release_regions - free IDE resources
 *
 *	Note that we only release the standard ports,
 *	and do not even try to handle any extra ports
 *	allocated for weird IDE interface chipsets.
 *
 *	Note also that we don't yet handle mmio resources here. More
 *	importantly our caller should be doing this so we need to 
 *	restructure this as a helper function for drivers.
 */

void ide_hwif_release_regions(ide_hwif_t *hwif)
{
	u32 i = 0;

	if (hwif->mmio == 2)
		return;
	if (hwif->io_ports[IDE_CONTROL_OFFSET])
		release_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1);
	if (hwif->straight8) {
		release_region(hwif->io_ports[IDE_DATA_OFFSET], 8);
		return;
	}
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		if (hwif->io_ports[i])
			release_region(hwif->io_ports[i], 1);
}

/**
 *	ide_hwif_restore	-	restore hwif to template
 *	@hwif: hwif to update
 *	@tmp_hwif: template
 *
 *	Restore hwif to a previous state by copying most settngs
 *	from the template.
 */

static void ide_hwif_restore(ide_hwif_t *hwif, ide_hwif_t *tmp_hwif)
{
	hwif->hwgroup			= tmp_hwif->hwgroup;

	hwif->gendev.parent		= tmp_hwif->gendev.parent;

	hwif->proc			= tmp_hwif->proc;

	hwif->major			= tmp_hwif->major;
	hwif->straight8			= tmp_hwif->straight8;
	hwif->bus_state			= tmp_hwif->bus_state;

	hwif->atapi_dma			= tmp_hwif->atapi_dma;
	hwif->ultra_mask		= tmp_hwif->ultra_mask;
	hwif->mwdma_mask		= tmp_hwif->mwdma_mask;
	hwif->swdma_mask		= tmp_hwif->swdma_mask;

	hwif->chipset			= tmp_hwif->chipset;
	hwif->hold			= tmp_hwif->hold;

#ifdef CONFIG_BLK_DEV_IDEPCI
	hwif->pci_dev			= tmp_hwif->pci_dev;
	hwif->cds			= tmp_hwif->cds;
#endif

	hwif->tuneproc			= tmp_hwif->tuneproc;
	hwif->speedproc			= tmp_hwif->speedproc;
	hwif->selectproc		= tmp_hwif->selectproc;
	hwif->reset_poll		= tmp_hwif->reset_poll;
	hwif->pre_reset			= tmp_hwif->pre_reset;
	hwif->resetproc			= tmp_hwif->resetproc;
	hwif->intrproc			= tmp_hwif->intrproc;
	hwif->maskproc			= tmp_hwif->maskproc;
	hwif->quirkproc			= tmp_hwif->quirkproc;
	hwif->busproc			= tmp_hwif->busproc;

	hwif->ata_input_data		= tmp_hwif->ata_input_data;
	hwif->ata_output_data		= tmp_hwif->ata_output_data;
	hwif->atapi_input_bytes		= tmp_hwif->atapi_input_bytes;
	hwif->atapi_output_bytes	= tmp_hwif->atapi_output_bytes;

	hwif->dma_setup			= tmp_hwif->dma_setup;
	hwif->dma_exec_cmd		= tmp_hwif->dma_exec_cmd;
	hwif->dma_start			= tmp_hwif->dma_start;
	hwif->ide_dma_end		= tmp_hwif->ide_dma_end;
	hwif->ide_dma_check		= tmp_hwif->ide_dma_check;
	hwif->ide_dma_on		= tmp_hwif->ide_dma_on;
	hwif->ide_dma_off_quietly	= tmp_hwif->ide_dma_off_quietly;
	hwif->ide_dma_test_irq		= tmp_hwif->ide_dma_test_irq;
	hwif->ide_dma_host_on		= tmp_hwif->ide_dma_host_on;
	hwif->ide_dma_host_off		= tmp_hwif->ide_dma_host_off;
	hwif->ide_dma_lostirq		= tmp_hwif->ide_dma_lostirq;
	hwif->ide_dma_timeout		= tmp_hwif->ide_dma_timeout;

	hwif->OUTB			= tmp_hwif->OUTB;
	hwif->OUTBSYNC			= tmp_hwif->OUTBSYNC;
	hwif->OUTW			= tmp_hwif->OUTW;
	hwif->OUTL			= tmp_hwif->OUTL;
	hwif->OUTSW			= tmp_hwif->OUTSW;
	hwif->OUTSL			= tmp_hwif->OUTSL;

	hwif->INB			= tmp_hwif->INB;
	hwif->INW			= tmp_hwif->INW;
	hwif->INL			= tmp_hwif->INL;
	hwif->INSW			= tmp_hwif->INSW;
	hwif->INSL			= tmp_hwif->INSL;

	hwif->sg_max_nents		= tmp_hwif->sg_max_nents;

	hwif->mmio			= tmp_hwif->mmio;
	hwif->rqsize			= tmp_hwif->rqsize;
	hwif->no_lba48			= tmp_hwif->no_lba48;

#ifndef CONFIG_BLK_DEV_IDECS
	hwif->irq			= tmp_hwif->irq;
#endif

	hwif->dma_base			= tmp_hwif->dma_base;
	hwif->dma_master		= tmp_hwif->dma_master;
	hwif->dma_command		= tmp_hwif->dma_command;
	hwif->dma_vendor1		= tmp_hwif->dma_vendor1;
	hwif->dma_status		= tmp_hwif->dma_status;
	hwif->dma_vendor3		= tmp_hwif->dma_vendor3;
	hwif->dma_prdtable		= tmp_hwif->dma_prdtable;

	hwif->dma_extra			= tmp_hwif->dma_extra;
	hwif->config_data		= tmp_hwif->config_data;
	hwif->select_data		= tmp_hwif->select_data;
	hwif->autodma			= tmp_hwif->autodma;
	hwif->udma_four			= tmp_hwif->udma_four;
	hwif->no_dsc			= tmp_hwif->no_dsc;

	hwif->hwif_data			= tmp_hwif->hwif_data;
}

/**
 *	ide_unregister		-	free an ide interface
 *	@index: index of interface (will change soon to a pointer)
 *
 *	Perform the final unregister of an IDE interface. At the moment
 *	we don't refcount interfaces so this will also get split up.
 *
 *	Locking:
 *	The caller must not hold the IDE locks
 *	The drive present/vanishing is not yet properly locked
 *	Take care with the callbacks. These have been split to avoid
 *	deadlocking the IDE layer. The shutdown callback is called
 *	before we take the lock and free resources. It is up to the
 *	caller to be sure there is no pending I/O here, and that
 *	the interfce will not be reopened (present/vanishing locking
 *	isnt yet done btw). After we commit to the final kill we
 *	call the cleanup callback with the ide locks held.
 *
 *	Unregister restores the hwif structures to the default state.
 *	This is raving bonkers.
 */

void ide_unregister(unsigned int index)
{
	ide_drive_t *drive;
	ide_hwif_t *hwif, *g;
	static ide_hwif_t tmp_hwif; /* protected by ide_cfg_sem */
	ide_hwgroup_t *hwgroup;
	int irq_count = 0, unit;

	BUG_ON(index >= MAX_HWIFS);

	BUG_ON(in_interrupt());
	BUG_ON(irqs_disabled());
	down(&ide_cfg_sem);
	spin_lock_irq(&ide_lock);
	hwif = &ide_hwifs[index];
	if (!hwif->present)
		goto abort;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &hwif->drives[unit];
		if (!drive->present) {
			if (drive->devfs_name[0] != '\0') {
				devfs_remove(drive->devfs_name);
				drive->devfs_name[0] = '\0';
			}
			continue;
		}
		spin_unlock_irq(&ide_lock);
		device_unregister(&drive->gendev);
		wait_for_completion(&drive->gendev_rel_comp);
		spin_lock_irq(&ide_lock);
	}
	hwif->present = 0;

	spin_unlock_irq(&ide_lock);

	destroy_proc_ide_interface(hwif);

	hwgroup = hwif->hwgroup;
	/*
	 * free the irq if we were the only hwif using it
	 */
	g = hwgroup->hwif;
	do {
		if (g->irq == hwif->irq)
			++irq_count;
		g = g->next;
	} while (g != hwgroup->hwif);
	if (irq_count == 1)
		free_irq(hwif->irq, hwgroup);

	spin_lock_irq(&ide_lock);
	/*
	 * Note that we only release the standard ports,
	 * and do not even try to handle any extra ports
	 * allocated for weird IDE interface chipsets.
	 */
	ide_hwif_release_regions(hwif);

	/*
	 * Remove us from the hwgroup, and free
	 * the hwgroup if we were the only member
	 */
	if (hwif->next == hwif) {
		BUG_ON(hwgroup->hwif != hwif);
		kfree(hwgroup);
	} else {
		/* There is another interface in hwgroup.
		 * Unlink us, and set hwgroup->drive and ->hwif to
		 * something sane.
		 */
		g = hwgroup->hwif;
		while (g->next != hwif)
			g = g->next;
		g->next = hwif->next;
		if (hwgroup->hwif == hwif) {
			/* Chose a random hwif for hwgroup->hwif.
			 * It's guaranteed that there are no drives
			 * left in the hwgroup.
			 */
			BUG_ON(hwgroup->drive != NULL);
			hwgroup->hwif = g;
		}
		BUG_ON(hwgroup->hwif == hwif);
	}

	/* More messed up locking ... */
	spin_unlock_irq(&ide_lock);
	device_unregister(&hwif->gendev);
	wait_for_completion(&hwif->gendev_rel_comp);

	/*
	 * Remove us from the kernel's knowledge
	 */
	blk_unregister_region(MKDEV(hwif->major, 0), MAX_DRIVES<<PARTN_BITS);
	kfree(hwif->sg_table);
	unregister_blkdev(hwif->major, hwif->name);
	spin_lock_irq(&ide_lock);

	if (hwif->dma_base) {
		(void) ide_release_dma(hwif);

		hwif->dma_base = 0;
		hwif->dma_master = 0;
		hwif->dma_command = 0;
		hwif->dma_vendor1 = 0;
		hwif->dma_status = 0;
		hwif->dma_vendor3 = 0;
		hwif->dma_prdtable = 0;
	}

	/* copy original settings */
	tmp_hwif = *hwif;

	/* restore hwif data to pristine status */
	init_hwif_data(hwif, index);
	init_hwif_default(hwif, index);

	ide_hwif_restore(hwif, &tmp_hwif);

abort:
	spin_unlock_irq(&ide_lock);
	up(&ide_cfg_sem);
}

EXPORT_SYMBOL(ide_unregister);


/**
 *	ide_setup_ports 	-	set up IDE interface ports
 *	@hw: register descriptions
 *	@base: base register
 *	@offsets: table of register offsets
 *	@ctrl: control register
 *	@ack_irq: IRQ ack
 *	@irq: interrupt lie
 *
 *	Setup hw_regs_t structure described by parameters.  You
 *	may set up the hw structure yourself OR use this routine to
 *	do it for you. This is basically a helper
 *
 */
 
void ide_setup_ports (	hw_regs_t *hw,
			unsigned long base, int *offsets,
			unsigned long ctrl, unsigned long intr,
			ide_ack_intr_t *ack_intr,
/*
 *			ide_io_ops_t *iops,
 */
			int irq)
{
	int i;

	for (i = 0; i < IDE_NR_PORTS; i++) {
		if (offsets[i] == -1) {
			switch(i) {
				case IDE_CONTROL_OFFSET:
					hw->io_ports[i] = ctrl;
					break;
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
				case IDE_IRQ_OFFSET:
					hw->io_ports[i] = intr;
					break;
#endif /* (CONFIG_AMIGA) || (CONFIG_MAC) */
				default:
					hw->io_ports[i] = 0;
					break;
			}
		} else {
			hw->io_ports[i] = base + offsets[i];
		}
	}
	hw->irq = irq;
	hw->dma = NO_DMA;
	hw->ack_intr = ack_intr;
/*
 *	hw->iops = iops;
 */
}

/**
 *	ide_register_hw_with_fixup	-	register IDE interface
 *	@hw: hardware registers
 *	@hwifp: pointer to returned hwif
 *	@fixup: fixup function
 *
 *	Register an IDE interface, specifying exactly the registers etc.
 *	Set init=1 iff calling before probes have taken place.
 *
 *	Returns -1 on error.
 */

int ide_register_hw_with_fixup(hw_regs_t *hw, ide_hwif_t **hwifp, void(*fixup)(ide_hwif_t *hwif))
{
	int index, retry = 1;
	ide_hwif_t *hwif;

	do {
		for (index = 0; index < MAX_HWIFS; ++index) {
			hwif = &ide_hwifs[index];
			if (hwif->hw.io_ports[IDE_DATA_OFFSET] == hw->io_ports[IDE_DATA_OFFSET])
				goto found;
		}
		for (index = 0; index < MAX_HWIFS; ++index) {
			hwif = &ide_hwifs[index];
			if (hwif->hold)
				continue;
			if ((!hwif->present && !hwif->mate && !initializing) ||
			    (!hwif->hw.io_ports[IDE_DATA_OFFSET] && initializing))
				goto found;
		}
		for (index = 0; index < MAX_HWIFS; index++)
			ide_unregister(index);
	} while (retry--);
	return -1;
found:
	if (hwif->present)
		ide_unregister(index);
	else if (!hwif->hold) {
		init_hwif_data(hwif, index);
		init_hwif_default(hwif, index);
	}
	if (hwif->present)
		return -1;
	memcpy(&hwif->hw, hw, sizeof(*hw));
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->hw.io_ports));
	hwif->irq = hw->irq;
	hwif->noprobe = 0;
	hwif->chipset = hw->chipset;
	hwif->gendev.parent = hw->dev;

	if (!initializing) {
		probe_hwif_init_with_fixup(hwif, fixup);
		create_proc_ide_interfaces();
	}

	if (hwifp)
		*hwifp = hwif;

	return (initializing || hwif->present) ? index : -1;
}

EXPORT_SYMBOL(ide_register_hw_with_fixup);

int ide_register_hw(hw_regs_t *hw, ide_hwif_t **hwifp)
{
	return ide_register_hw_with_fixup(hw, hwifp, NULL);
}

EXPORT_SYMBOL(ide_register_hw);

/*
 *	Locks for IDE setting functionality
 */

DECLARE_MUTEX(ide_setting_sem);

/**
 *	__ide_add_setting	-	add an ide setting option
 *	@drive: drive to use
 *	@name: setting name
 *	@rw: true if the function is read write
 *	@read_ioctl: function to call on read
 *	@write_ioctl: function to call on write
 *	@data_type: type of data
 *	@min: range minimum
 *	@max: range maximum
 *	@mul_factor: multiplication scale
 *	@div_factor: divison scale
 *	@data: private data field
 *	@set: setting
 *	@auto_remove: setting auto removal flag
 *
 *	Removes the setting named from the device if it is present.
 *	The function takes the settings_lock to protect against 
 *	parallel changes. This function must not be called from IRQ
 *	context. Returns 0 on success or -1 on failure.
 *
 *	BUGS: This code is seriously over-engineered. There is also
 *	magic about how the driver specific features are setup. If
 *	a driver is attached we assume the driver settings are auto
 *	remove.
 */

static int __ide_add_setting(ide_drive_t *drive, const char *name, int rw, int read_ioctl, int write_ioctl, int data_type, int min, int max, int mul_factor, int div_factor, void *data, ide_procset_t *set, int auto_remove)
{
	ide_settings_t **p = (ide_settings_t **) &drive->settings, *setting = NULL;

	down(&ide_setting_sem);
	while ((*p) && strcmp((*p)->name, name) < 0)
		p = &((*p)->next);
	if ((setting = kzalloc(sizeof(*setting), GFP_KERNEL)) == NULL)
		goto abort;
	if ((setting->name = kmalloc(strlen(name) + 1, GFP_KERNEL)) == NULL)
		goto abort;
	strcpy(setting->name, name);
	setting->rw = rw;
	setting->read_ioctl = read_ioctl;
	setting->write_ioctl = write_ioctl;
	setting->data_type = data_type;
	setting->min = min;
	setting->max = max;
	setting->mul_factor = mul_factor;
	setting->div_factor = div_factor;
	setting->data = data;
	setting->set = set;
	
	setting->next = *p;
	if (auto_remove)
		setting->auto_remove = 1;
	*p = setting;
	up(&ide_setting_sem);
	return 0;
abort:
	up(&ide_setting_sem);
	kfree(setting);
	return -1;
}

int ide_add_setting(ide_drive_t *drive, const char *name, int rw, int read_ioctl, int write_ioctl, int data_type, int min, int max, int mul_factor, int div_factor, void *data, ide_procset_t *set)
{
	return __ide_add_setting(drive, name, rw, read_ioctl, write_ioctl, data_type, min, max, mul_factor, div_factor, data, set, 1);
}

EXPORT_SYMBOL(ide_add_setting);

/**
 *	__ide_remove_setting	-	remove an ide setting option
 *	@drive: drive to use
 *	@name: setting name
 *
 *	Removes the setting named from the device if it is present.
 *	The caller must hold the setting semaphore.
 */
 
static void __ide_remove_setting (ide_drive_t *drive, char *name)
{
	ide_settings_t **p, *setting;

	p = (ide_settings_t **) &drive->settings;

	while ((*p) && strcmp((*p)->name, name))
		p = &((*p)->next);
	if ((setting = (*p)) == NULL)
		return;

	(*p) = setting->next;
	
	kfree(setting->name);
	kfree(setting);
}

/**
 *	ide_find_setting_by_ioctl	-	find a drive specific ioctl
 *	@drive: drive to scan
 *	@cmd: ioctl command to handle
 *
 *	Scan's the device setting table for a matching entry and returns
 *	this or NULL if no entry is found. The caller must hold the
 *	setting semaphore
 */
 
static ide_settings_t *ide_find_setting_by_ioctl (ide_drive_t *drive, int cmd)
{
	ide_settings_t *setting = drive->settings;

	while (setting) {
		if (setting->read_ioctl == cmd || setting->write_ioctl == cmd)
			break;
		setting = setting->next;
	}
	
	return setting;
}

/**
 *	ide_find_setting_by_name	-	find a drive specific setting
 *	@drive: drive to scan
 *	@name: setting name
 *
 *	Scan's the device setting table for a matching entry and returns
 *	this or NULL if no entry is found. The caller must hold the
 *	setting semaphore
 */
 
ide_settings_t *ide_find_setting_by_name (ide_drive_t *drive, char *name)
{
	ide_settings_t *setting = drive->settings;

	while (setting) {
		if (strcmp(setting->name, name) == 0)
			break;
		setting = setting->next;
	}
	return setting;
}

/**
 *	auto_remove_settings	-	remove driver specific settings
 *	@drive: drive
 *
 *	Automatically remove all the driver specific settings for this
 *	drive. This function may sleep and must not be called from IRQ
 *	context. The caller must hold ide_setting_sem.
 */
 
static void auto_remove_settings (ide_drive_t *drive)
{
	ide_settings_t *setting;
repeat:
	setting = drive->settings;
	while (setting) {
		if (setting->auto_remove) {
			__ide_remove_setting(drive, setting->name);
			goto repeat;
		}
		setting = setting->next;
	}
}

/**
 *	ide_read_setting	-	read an IDE setting
 *	@drive: drive to read from
 *	@setting: drive setting
 *
 *	Read a drive setting and return the value. The caller
 *	must hold the ide_setting_sem when making this call.
 *
 *	BUGS: the data return and error are the same return value
 *	so an error -EINVAL and true return of the same value cannot
 *	be told apart
 */
 
int ide_read_setting (ide_drive_t *drive, ide_settings_t *setting)
{
	int		val = -EINVAL;
	unsigned long	flags;

	if ((setting->rw & SETTING_READ)) {
		spin_lock_irqsave(&ide_lock, flags);
		switch(setting->data_type) {
			case TYPE_BYTE:
				val = *((u8 *) setting->data);
				break;
			case TYPE_SHORT:
				val = *((u16 *) setting->data);
				break;
			case TYPE_INT:
			case TYPE_INTA:
				val = *((u32 *) setting->data);
				break;
		}
		spin_unlock_irqrestore(&ide_lock, flags);
	}
	return val;
}

/**
 *	ide_spin_wait_hwgroup	-	wait for group
 *	@drive: drive in the group
 *
 *	Wait for an IDE device group to go non busy and then return
 *	holding the ide_lock which guards the hwgroup->busy status
 *	and right to use it.
 */

int ide_spin_wait_hwgroup (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned long timeout = jiffies + (3 * HZ);

	spin_lock_irq(&ide_lock);

	while (hwgroup->busy) {
		unsigned long lflags;
		spin_unlock_irq(&ide_lock);
		local_irq_set(lflags);
		if (time_after(jiffies, timeout)) {
			local_irq_restore(lflags);
			printk(KERN_ERR "%s: channel busy\n", drive->name);
			return -EBUSY;
		}
		local_irq_restore(lflags);
		spin_lock_irq(&ide_lock);
	}
	return 0;
}

EXPORT_SYMBOL(ide_spin_wait_hwgroup);

/**
 *	ide_write_setting	-	read an IDE setting
 *	@drive: drive to read from
 *	@setting: drive setting
 *	@val: value
 *
 *	Write a drive setting if it is possible. The caller
 *	must hold the ide_setting_sem when making this call.
 *
 *	BUGS: the data return and error are the same return value
 *	so an error -EINVAL and true return of the same value cannot
 *	be told apart
 *
 *	FIXME:  This should be changed to enqueue a special request
 *	to the driver to change settings, and then wait on a sema for completion.
 *	The current scheme of polling is kludgy, though safe enough.
 */

int ide_write_setting (ide_drive_t *drive, ide_settings_t *setting, int val)
{
	int i;
	u32 *p;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!(setting->rw & SETTING_WRITE))
		return -EPERM;
	if (val < setting->min || val > setting->max)
		return -EINVAL;
	if (setting->set)
		return setting->set(drive, val);
	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	switch (setting->data_type) {
		case TYPE_BYTE:
			*((u8 *) setting->data) = val;
			break;
		case TYPE_SHORT:
			*((u16 *) setting->data) = val;
			break;
		case TYPE_INT:
			*((u32 *) setting->data) = val;
			break;
		case TYPE_INTA:
			p = (u32 *) setting->data;
			for (i = 0; i < 1 << PARTN_BITS; i++, p++)
				*p = val;
			break;
	}
	spin_unlock_irq(&ide_lock);
	return 0;
}

static int set_io_32bit(ide_drive_t *drive, int arg)
{
	drive->io_32bit = arg;
#ifdef CONFIG_BLK_DEV_DTC2278
	if (HWIF(drive)->chipset == ide_dtc2278)
		HWIF(drive)->drives[!drive->select.b.unit].io_32bit = arg;
#endif /* CONFIG_BLK_DEV_DTC2278 */
	return 0;
}

static int set_using_dma (ide_drive_t *drive, int arg)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (!drive->id || !(drive->id->capability & 1))
		return -EPERM;
	if (HWIF(drive)->ide_dma_check == NULL)
		return -EPERM;
	if (arg) {
		if (HWIF(drive)->ide_dma_check(drive)) return -EIO;
		if (HWIF(drive)->ide_dma_on(drive)) return -EIO;
	} else {
		if (__ide_dma_off(drive))
			return -EIO;
	}
	return 0;
#else
	return -EPERM;
#endif
}

static int set_pio_mode (ide_drive_t *drive, int arg)
{
	struct request rq;

	if (!HWIF(drive)->tuneproc)
		return -ENOSYS;
	if (drive->special.b.set_tune)
		return -EBUSY;
	ide_init_drive_cmd(&rq);
	drive->tune_req = (u8) arg;
	drive->special.b.set_tune = 1;
	(void) ide_do_drive_cmd(drive, &rq, ide_wait);
	return 0;
}

static int set_xfer_rate (ide_drive_t *drive, int arg)
{
	int err = ide_wait_cmd(drive,
			WIN_SETFEATURES, (u8) arg,
			SETFEATURES_XFER, 0, NULL);

	if (!err && arg) {
		ide_set_xfer_rate(drive, (u8) arg);
		ide_driveid_update(drive);
	}
	return err;
}

/**
 *	ide_add_generic_settings	-	generic ide settings
 *	@drive: drive being configured
 *
 *	Add the generic parts of the system settings to the /proc files and
 *	ioctls for this IDE device. The caller must not be holding the
 *	ide_setting_sem.
 */

void ide_add_generic_settings (ide_drive_t *drive)
{
/*
 *			  drive		setting name		read/write access				read ioctl		write ioctl		data type	min	max				mul_factor	div_factor	data pointer			set function
 */
	__ide_add_setting(drive,	"io_32bit",		drive->no_io_32bit ? SETTING_READ : SETTING_RW,	HDIO_GET_32BIT,		HDIO_SET_32BIT,		TYPE_BYTE,	0,	1 + (SUPPORT_VLB_SYNC << 1),	1,		1,		&drive->io_32bit,		set_io_32bit,	0);
	__ide_add_setting(drive,	"keepsettings",		SETTING_RW,					HDIO_GET_KEEPSETTINGS,	HDIO_SET_KEEPSETTINGS,	TYPE_BYTE,	0,	1,				1,		1,		&drive->keep_settings,		NULL,		0);
	__ide_add_setting(drive,	"nice1",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	1,				1,		1,		&drive->nice1,			NULL,		0);
	__ide_add_setting(drive,	"pio_mode",		SETTING_WRITE,					-1,			HDIO_SET_PIO_MODE,	TYPE_BYTE,	0,	255,				1,		1,		NULL,				set_pio_mode,	0);
	__ide_add_setting(drive,	"unmaskirq",		drive->no_unmask ? SETTING_READ : SETTING_RW,	HDIO_GET_UNMASKINTR,	HDIO_SET_UNMASKINTR,	TYPE_BYTE,	0,	1,				1,		1,		&drive->unmask,			NULL,		0);
	__ide_add_setting(drive,	"using_dma",		SETTING_RW,					HDIO_GET_DMA,		HDIO_SET_DMA,		TYPE_BYTE,	0,	1,				1,		1,		&drive->using_dma,		set_using_dma,	0);
	__ide_add_setting(drive,	"init_speed",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	70,				1,		1,		&drive->init_speed,		NULL,		0);
	__ide_add_setting(drive,	"current_speed",	SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	70,				1,		1,		&drive->current_speed,		set_xfer_rate,	0);
	__ide_add_setting(drive,	"number",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	3,				1,		1,		&drive->dn,			NULL,		0);
}

/**
 *	system_bus_clock	-	clock guess
 *
 *	External version of the bus clock guess used by very old IDE drivers
 *	for things like VLB timings. Should not be used.
 */

int system_bus_clock (void)
{
	return((int) ((!system_bus_speed) ? ide_system_bus_speed() : system_bus_speed ));
}

EXPORT_SYMBOL(system_bus_clock);

static int generic_ide_suspend(struct device *dev, pm_message_t state)
{
	ide_drive_t *drive = dev->driver_data;
	struct request rq;
	struct request_pm_state rqpm;
	ide_task_t args;

	memset(&rq, 0, sizeof(rq));
	memset(&rqpm, 0, sizeof(rqpm));
	memset(&args, 0, sizeof(args));
	rq.flags = REQ_PM_SUSPEND;
	rq.special = &args;
	rq.pm = &rqpm;
	rqpm.pm_step = ide_pm_state_start_suspend;
	rqpm.pm_state = state.event;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

static int generic_ide_resume(struct device *dev)
{
	ide_drive_t *drive = dev->driver_data;
	struct request rq;
	struct request_pm_state rqpm;
	ide_task_t args;

	memset(&rq, 0, sizeof(rq));
	memset(&rqpm, 0, sizeof(rqpm));
	memset(&args, 0, sizeof(args));
	rq.flags = REQ_PM_RESUME;
	rq.special = &args;
	rq.pm = &rqpm;
	rqpm.pm_step = ide_pm_state_start_resume;
	rqpm.pm_state = PM_EVENT_ON;

	return ide_do_drive_cmd(drive, &rq, ide_head_wait);
}

int generic_ide_ioctl(ide_drive_t *drive, struct file *file, struct block_device *bdev,
			unsigned int cmd, unsigned long arg)
{
	ide_settings_t *setting;
	ide_driver_t *drv;
	int err = 0;
	void __user *p = (void __user *)arg;

	down(&ide_setting_sem);
	if ((setting = ide_find_setting_by_ioctl(drive, cmd)) != NULL) {
		if (cmd == setting->read_ioctl) {
			err = ide_read_setting(drive, setting);
			up(&ide_setting_sem);
			return err >= 0 ? put_user(err, (long __user *)arg) : err;
		} else {
			if (bdev != bdev->bd_contains)
				err = -EINVAL;
			else
				err = ide_write_setting(drive, setting, arg);
			up(&ide_setting_sem);
			return err;
		}
	}
	up(&ide_setting_sem);

	switch (cmd) {
		case HDIO_OBSOLETE_IDENTITY:
		case HDIO_GET_IDENTITY:
			if (bdev != bdev->bd_contains)
				return -EINVAL;
			if (drive->id_read == 0)
				return -ENOMSG;
			if (copy_to_user(p, drive->id, (cmd == HDIO_GET_IDENTITY) ? sizeof(*drive->id) : 142))
				return -EFAULT;
			return 0;

		case HDIO_GET_NICE:
			return put_user(drive->dsc_overlap	<<	IDE_NICE_DSC_OVERLAP	|
					drive->atapi_overlap	<<	IDE_NICE_ATAPI_OVERLAP	|
					drive->nice0		<< 	IDE_NICE_0		|
					drive->nice1		<<	IDE_NICE_1		|
					drive->nice2		<<	IDE_NICE_2,
					(long __user *) arg);

#ifdef CONFIG_IDE_TASK_IOCTL
		case HDIO_DRIVE_TASKFILE:
		        if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			switch(drive->media) {
				case ide_disk:
					return ide_taskfile_ioctl(drive, cmd, arg);
				default:
					return -ENOMSG;
			}
#endif /* CONFIG_IDE_TASK_IOCTL */

		case HDIO_DRIVE_CMD:
			if (!capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_cmd_ioctl(drive, cmd, arg);

		case HDIO_DRIVE_TASK:
			if (!capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_task_ioctl(drive, cmd, arg);

		case HDIO_SCAN_HWIF:
		{
			hw_regs_t hw;
			int args[3];
			if (!capable(CAP_SYS_RAWIO)) return -EACCES;
			if (copy_from_user(args, p, 3 * sizeof(int)))
				return -EFAULT;
			memset(&hw, 0, sizeof(hw));
			ide_init_hwif_ports(&hw, (unsigned long) args[0],
					    (unsigned long) args[1], NULL);
			hw.irq = args[2];
			if (ide_register_hw(&hw, NULL) == -1)
				return -EIO;
			return 0;
		}
	        case HDIO_UNREGISTER_HWIF:
			if (!capable(CAP_SYS_RAWIO)) return -EACCES;
			/* (arg > MAX_HWIFS) checked in function */
			ide_unregister(arg);
			return 0;
		case HDIO_SET_NICE:
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP) | (1 << IDE_NICE_1))))
				return -EPERM;
			drive->dsc_overlap = (arg >> IDE_NICE_DSC_OVERLAP) & 1;
			drv = *(ide_driver_t **)bdev->bd_disk->private_data;
			if (drive->dsc_overlap && !drv->supports_dsc_overlap) {
				drive->dsc_overlap = 0;
				return -EPERM;
			}
			drive->nice1 = (arg >> IDE_NICE_1) & 1;
			return 0;
		case HDIO_DRIVE_RESET:
		{
			unsigned long flags;
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			
			/*
			 *	Abort the current command on the
			 *	group if there is one, taking
			 *	care not to allow anything else
			 *	to be queued and to die on the
			 *	spot if we miss one somehow
			 */

			spin_lock_irqsave(&ide_lock, flags);

			ide_abort(drive, "drive reset");

			if(HWGROUP(drive)->handler)
				BUG();
				
			/* Ensure nothing gets queued after we
			   drop the lock. Reset will clear the busy */
		   
			HWGROUP(drive)->busy = 1;
			spin_unlock_irqrestore(&ide_lock, flags);
			(void) ide_do_reset(drive);

			return 0;
		}

		case CDROMEJECT:
		case CDROMCLOSETRAY:
			return scsi_cmd_ioctl(file, bdev->bd_disk, cmd, p);

		case HDIO_GET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (put_user(HWIF(drive)->bus_state, (long __user *)arg))
				return -EFAULT;
			return 0;

		case HDIO_SET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (HWIF(drive)->busproc)
				return HWIF(drive)->busproc(drive, (int)arg);
			return -EOPNOTSUPP;
		default:
			return -EINVAL;
	}
}

EXPORT_SYMBOL(generic_ide_ioctl);

/*
 * stridx() returns the offset of c within s,
 * or -1 if c is '\0' or not found within s.
 */
static int __init stridx (const char *s, char c)
{
	char *i = strchr(s, c);
	return (i && c) ? i - s : -1;
}

/*
 * match_parm() does parsing for ide_setup():
 *
 * 1. the first char of s must be '='.
 * 2. if the remainder matches one of the supplied keywords,
 *     the index (1 based) of the keyword is negated and returned.
 * 3. if the remainder is a series of no more than max_vals numbers
 *     separated by commas, the numbers are saved in vals[] and a
 *     count of how many were saved is returned.  Base10 is assumed,
 *     and base16 is allowed when prefixed with "0x".
 * 4. otherwise, zero is returned.
 */
static int __init match_parm (char *s, const char *keywords[], int vals[], int max_vals)
{
	static const char *decimal = "0123456789";
	static const char *hex = "0123456789abcdef";
	int i, n;

	if (*s++ == '=') {
		/*
		 * Try matching against the supplied keywords,
		 * and return -(index+1) if we match one
		 */
		if (keywords != NULL) {
			for (i = 0; *keywords != NULL; ++i) {
				if (!strcmp(s, *keywords++))
					return -(i+1);
			}
		}
		/*
		 * Look for a series of no more than "max_vals"
		 * numeric values separated by commas, in base10,
		 * or base16 when prefixed with "0x".
		 * Return a count of how many were found.
		 */
		for (n = 0; (i = stridx(decimal, *s)) >= 0;) {
			vals[n] = i;
			while ((i = stridx(decimal, *++s)) >= 0)
				vals[n] = (vals[n] * 10) + i;
			if (*s == 'x' && !vals[n]) {
				while ((i = stridx(hex, *++s)) >= 0)
					vals[n] = (vals[n] * 0x10) + i;
			}
			if (++n == max_vals)
				break;
			if (*s == ',' || *s == ';')
				++s;
		}
		if (!*s)
			return n;
	}
	return 0;	/* zero = nothing matched */
}

#ifdef CONFIG_BLK_DEV_ALI14XX
static int __initdata probe_ali14xx;
extern int ali14xx_init(void);
#endif
#ifdef CONFIG_BLK_DEV_UMC8672
static int __initdata probe_umc8672;
extern int umc8672_init(void);
#endif
#ifdef CONFIG_BLK_DEV_DTC2278
static int __initdata probe_dtc2278;
extern int dtc2278_init(void);
#endif
#ifdef CONFIG_BLK_DEV_HT6560B
static int __initdata probe_ht6560b;
extern int ht6560b_init(void);
#endif
#ifdef CONFIG_BLK_DEV_QD65XX
static int __initdata probe_qd65xx;
extern int qd65xx_init(void);
#endif

static int __initdata is_chipset_set[MAX_HWIFS];

/*
 * ide_setup() gets called VERY EARLY during initialization,
 * to handle kernel "command line" strings beginning with "hdx=" or "ide".
 *
 * Remember to update Documentation/ide.txt if you change something here.
 */
static int __init ide_setup(char *s)
{
	int i, vals[3];
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	unsigned int hw, unit;
	const char max_drive = 'a' + ((MAX_HWIFS * MAX_DRIVES) - 1);
	const char max_hwif  = '0' + (MAX_HWIFS - 1);

	
	if (strncmp(s,"hd",2) == 0 && s[2] == '=')	/* hd= is for hd.c   */
		return 0;				/* driver and not us */

	if (strncmp(s,"ide",3) && strncmp(s,"idebus",6) && strncmp(s,"hd",2))
		return 0;

	printk(KERN_INFO "ide_setup: %s", s);
	init_ide_data ();

#ifdef CONFIG_BLK_DEV_IDEDOUBLER
	if (!strcmp(s, "ide=doubler")) {
		extern int ide_doubler;

		printk(" : Enabled support for IDE doublers\n");
		ide_doubler = 1;
		return 1;
	}
#endif /* CONFIG_BLK_DEV_IDEDOUBLER */

	if (!strcmp(s, "ide=nodma")) {
		printk(" : Prevented DMA\n");
		noautodma = 1;
		return 1;
	}

#ifdef CONFIG_BLK_DEV_IDEPCI
	if (!strcmp(s, "ide=reverse")) {
		ide_scan_direction = 1;
		printk(" : Enabled support for IDE inverse scan order.\n");
		return 1;
	}
#endif /* CONFIG_BLK_DEV_IDEPCI */

	/*
	 * Look for drive options:  "hdx="
	 */
	if (s[0] == 'h' && s[1] == 'd' && s[2] >= 'a' && s[2] <= max_drive) {
		const char *hd_words[] = {
			"none", "noprobe", "nowerr", "cdrom", "serialize",
			"autotune", "noautotune", "minus8", "swapdata", "bswap",
			"minus11", "remap", "remap63", "scsi", NULL };
		unit = s[2] - 'a';
		hw   = unit / MAX_DRIVES;
		unit = unit % MAX_DRIVES;
		hwif = &ide_hwifs[hw];
		drive = &hwif->drives[unit];
		if (strncmp(s + 4, "ide-", 4) == 0) {
			strlcpy(drive->driver_req, s + 4, sizeof(drive->driver_req));
			goto done;
		}
		switch (match_parm(&s[3], hd_words, vals, 3)) {
			case -1: /* "none" */
			case -2: /* "noprobe" */
				drive->noprobe = 1;
				goto done;
			case -3: /* "nowerr" */
				drive->bad_wstat = BAD_R_STAT;
				hwif->noprobe = 0;
				goto done;
			case -4: /* "cdrom" */
				drive->present = 1;
				drive->media = ide_cdrom;
				/* an ATAPI device ignores DRDY */
				drive->ready_stat = 0;
				hwif->noprobe = 0;
				goto done;
			case -5: /* "serialize" */
				printk(" -- USE \"ide%d=serialize\" INSTEAD", hw);
				goto do_serialize;
			case -6: /* "autotune" */
				drive->autotune = IDE_TUNE_AUTO;
				goto obsolete_option;
			case -7: /* "noautotune" */
				drive->autotune = IDE_TUNE_NOAUTO;
				goto obsolete_option;
			case -9: /* "swapdata" */
			case -10: /* "bswap" */
				drive->bswap = 1;
				goto done;
			case -12: /* "remap" */
				drive->remap_0_to_1 = 1;
				goto done;
			case -13: /* "remap63" */
				drive->sect0 = 63;
				goto done;
			case -14: /* "scsi" */
				drive->scsi = 1;
				goto done;
			case 3: /* cyl,head,sect */
				drive->media	= ide_disk;
				drive->ready_stat = READY_STAT;
				drive->cyl	= drive->bios_cyl  = vals[0];
				drive->head	= drive->bios_head = vals[1];
				drive->sect	= drive->bios_sect = vals[2];
				drive->present	= 1;
				drive->forced_geom = 1;
				hwif->noprobe = 0;
				goto done;
			default:
				goto bad_option;
		}
	}

	if (s[0] != 'i' || s[1] != 'd' || s[2] != 'e')
		goto bad_option;
	/*
	 * Look for bus speed option:  "idebus="
	 */
	if (s[3] == 'b' && s[4] == 'u' && s[5] == 's') {
		if (match_parm(&s[6], NULL, vals, 1) != 1)
			goto bad_option;
		if (vals[0] >= 20 && vals[0] <= 66) {
			idebus_parameter = vals[0];
		} else
			printk(" -- BAD BUS SPEED! Expected value from 20 to 66");
		goto done;
	}
	/*
	 * Look for interface options:  "idex="
	 */
	if (s[3] >= '0' && s[3] <= max_hwif) {
		/*
		 * Be VERY CAREFUL changing this: note hardcoded indexes below
		 * (-8, -9, -10) are reserved to ease the hardcoding.
		 */
		static const char *ide_words[] = {
			"noprobe", "serialize", "autotune", "noautotune", 
			"reset", "dma", "ata66", "minus8", "minus9",
			"minus10", "four", "qd65xx", "ht6560b", "cmd640_vlb",
			"dtc2278", "umc8672", "ali14xx", NULL };
		hw = s[3] - '0';
		hwif = &ide_hwifs[hw];
		i = match_parm(&s[4], ide_words, vals, 3);

		/*
		 * Cryptic check to ensure chipset not already set for hwif.
		 * Note: we can't depend on hwif->chipset here.
		 */
		if ((i >= -18 && i <= -11) || (i > 0 && i <= 3)) {
			/* chipset already specified */
			if (is_chipset_set[hw])
				goto bad_option;
			if (i > -18 && i <= -11) {
				/* these drivers are for "ide0=" only */
				if (hw != 0)
					goto bad_hwif;
				/* chipset already specified for 2nd port */
				if (is_chipset_set[hw+1])
					goto bad_option;
			}
			is_chipset_set[hw] = 1;
			printk("\n");
		}

		switch (i) {
#ifdef CONFIG_BLK_DEV_ALI14XX
			case -17: /* "ali14xx" */
				probe_ali14xx = 1;
				goto done;
#endif
#ifdef CONFIG_BLK_DEV_UMC8672
			case -16: /* "umc8672" */
				probe_umc8672 = 1;
				goto done;
#endif
#ifdef CONFIG_BLK_DEV_DTC2278
			case -15: /* "dtc2278" */
				probe_dtc2278 = 1;
				goto done;
#endif
#ifdef CONFIG_BLK_DEV_CMD640
			case -14: /* "cmd640_vlb" */
			{
				extern int cmd640_vlb; /* flag for cmd640.c */
				cmd640_vlb = 1;
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_HT6560B
			case -13: /* "ht6560b" */
				probe_ht6560b = 1;
				goto done;
#endif
#ifdef CONFIG_BLK_DEV_QD65XX
			case -12: /* "qd65xx" */
				probe_qd65xx = 1;
				goto done;
#endif
#ifdef CONFIG_BLK_DEV_4DRIVES
			case -11: /* "four" drives on one set of ports */
			{
				ide_hwif_t *mate = &ide_hwifs[hw^1];
				mate->drives[0].select.all ^= 0x20;
				mate->drives[1].select.all ^= 0x20;
				hwif->chipset = mate->chipset = ide_4drives;
				mate->irq = hwif->irq;
				memcpy(mate->io_ports, hwif->io_ports, sizeof(hwif->io_ports));
				goto do_serialize;
			}
#endif /* CONFIG_BLK_DEV_4DRIVES */
			case -10: /* minus10 */
			case -9: /* minus9 */
			case -8: /* minus8 */
				goto bad_option;
			case -7: /* ata66 */
#ifdef CONFIG_BLK_DEV_IDEPCI
				hwif->udma_four = 1;
				goto obsolete_option;
#else
				goto bad_hwif;
#endif
			case -6: /* dma */
				hwif->autodma = 1;
				goto obsolete_option;
			case -5: /* "reset" */
				hwif->reset = 1;
				goto obsolete_option;
			case -4: /* "noautotune" */
				hwif->drives[0].autotune = IDE_TUNE_NOAUTO;
				hwif->drives[1].autotune = IDE_TUNE_NOAUTO;
				goto obsolete_option;
			case -3: /* "autotune" */
				hwif->drives[0].autotune = IDE_TUNE_AUTO;
				hwif->drives[1].autotune = IDE_TUNE_AUTO;
				goto obsolete_option;
			case -2: /* "serialize" */
			do_serialize:
				hwif->mate = &ide_hwifs[hw^1];
				hwif->mate->mate = hwif;
				hwif->serialized = hwif->mate->serialized = 1;
				goto obsolete_option;

			case -1: /* "noprobe" */
				hwif->noprobe = 1;
				goto done;

			case 1:	/* base */
				vals[1] = vals[0] + 0x206; /* default ctl */
			case 2: /* base,ctl */
				vals[2] = 0;	/* default irq = probe for it */
			case 3: /* base,ctl,irq */
				hwif->hw.irq = vals[2];
				ide_init_hwif_ports(&hwif->hw, (unsigned long) vals[0], (unsigned long) vals[1], &hwif->irq);
				memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
				hwif->irq      = vals[2];
				hwif->noprobe  = 0;
				hwif->chipset  = ide_forced;
				goto obsolete_option;

			case 0: goto bad_option;
			default:
				printk(" -- SUPPORT NOT CONFIGURED IN THIS KERNEL\n");
				return 1;
		}
	}
bad_option:
	printk(" -- BAD OPTION\n");
	return 1;
obsolete_option:
	printk(" -- OBSOLETE OPTION, WILL BE REMOVED SOON!\n");
	return 1;
bad_hwif:
	printk("-- NOT SUPPORTED ON ide%d", hw);
done:
	printk("\n");
	return 1;
}

extern void pnpide_init(void);
extern void h8300_ide_init(void);

/*
 * probe_for_hwifs() finds/initializes "known" IDE interfaces
 */
static void __init probe_for_hwifs (void)
{
#ifdef CONFIG_BLK_DEV_IDEPCI
	ide_scan_pcibus(ide_scan_direction);
#endif /* CONFIG_BLK_DEV_IDEPCI */

#ifdef CONFIG_ETRAX_IDE
	{
		extern void init_e100_ide(void);
		init_e100_ide();
	}
#endif /* CONFIG_ETRAX_IDE */
#ifdef CONFIG_BLK_DEV_CMD640
	{
		extern void ide_probe_for_cmd640x(void);
		ide_probe_for_cmd640x();
	}
#endif /* CONFIG_BLK_DEV_CMD640 */
#ifdef CONFIG_BLK_DEV_IDE_PMAC
	{
		extern void pmac_ide_probe(void);
		pmac_ide_probe();
	}
#endif /* CONFIG_BLK_DEV_IDE_PMAC */
#ifdef CONFIG_BLK_DEV_GAYLE
	{
		extern void gayle_init(void);
		gayle_init();
	}
#endif /* CONFIG_BLK_DEV_GAYLE */
#ifdef CONFIG_BLK_DEV_FALCON_IDE
	{
		extern void falconide_init(void);
		falconide_init();
	}
#endif /* CONFIG_BLK_DEV_FALCON_IDE */
#ifdef CONFIG_BLK_DEV_MAC_IDE
	{
		extern void macide_init(void);
		macide_init();
	}
#endif /* CONFIG_BLK_DEV_MAC_IDE */
#ifdef CONFIG_BLK_DEV_Q40IDE
	{
		extern void q40ide_init(void);
		q40ide_init();
	}
#endif /* CONFIG_BLK_DEV_Q40IDE */
#ifdef CONFIG_BLK_DEV_BUDDHA
	{
		extern void buddha_init(void);
		buddha_init();
	}
#endif /* CONFIG_BLK_DEV_BUDDHA */
#ifdef CONFIG_BLK_DEV_IDEPNP
	pnpide_init();
#endif
#ifdef CONFIG_H8300
	h8300_ide_init();
#endif
}

void ide_register_subdriver(ide_drive_t *drive, ide_driver_t *driver)
{
#ifdef CONFIG_PROC_FS
	ide_add_proc_entries(drive->proc, driver->proc, drive);
#endif
}

EXPORT_SYMBOL(ide_register_subdriver);

/**
 *	ide_unregister_subdriver	-	disconnect drive from driver
 *	@drive: drive to unplug
 *	@driver: driver
 *
 *	Disconnect a drive from the driver it was attached to and then
 *	clean up the various proc files and other objects attached to it.
 *
 *	Takes ide_setting_sem and ide_lock.
 *	Caller must hold none of the locks.
 */

void ide_unregister_subdriver(ide_drive_t *drive, ide_driver_t *driver)
{
	unsigned long flags;
	
	down(&ide_setting_sem);
	spin_lock_irqsave(&ide_lock, flags);
#ifdef CONFIG_PROC_FS
	ide_remove_proc_entries(drive->proc, driver->proc);
#endif
	auto_remove_settings(drive);
	spin_unlock_irqrestore(&ide_lock, flags);
	up(&ide_setting_sem);
}

EXPORT_SYMBOL(ide_unregister_subdriver);

/*
 * Probe module
 */

EXPORT_SYMBOL(ide_lock);

static int ide_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static char *media_string(ide_drive_t *drive)
{
	switch (drive->media) {
	case ide_disk:
		return "disk";
	case ide_cdrom:
		return "cdrom";
	case ide_tape:
		return "tape";
	case ide_floppy:
		return "floppy";
	default:
		return "UNKNOWN";
	}
}

static ssize_t media_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", media_string(drive));
}

static ssize_t drivename_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", drive->name);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "ide:m-%s\n", media_string(drive));
}

static struct device_attribute ide_dev_attrs[] = {
	__ATTR_RO(media),
	__ATTR_RO(drivename),
	__ATTR_RO(modalias),
	__ATTR_NULL
};

static int ide_uevent(struct device *dev, char **envp, int num_envp,
		      char *buffer, int buffer_size)
{
	ide_drive_t *drive = to_ide_device(dev);
	int i = 0;
	int length = 0;

	add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &length,
		       "MEDIA=%s", media_string(drive));
	add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &length,
		       "DRIVENAME=%s", drive->name);
	add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &length,
		       "MODALIAS=ide:m-%s", media_string(drive));
	envp[i] = NULL;
	return 0;
}

static int generic_ide_probe(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	ide_driver_t *drv = to_ide_driver(dev->driver);

	return drv->probe ? drv->probe(drive) : -ENODEV;
}

static int generic_ide_remove(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	ide_driver_t *drv = to_ide_driver(dev->driver);

	if (drv->remove)
		drv->remove(drive);

	return 0;
}

static void generic_ide_shutdown(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	ide_driver_t *drv = to_ide_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(drive);
}

struct bus_type ide_bus_type = {
	.name		= "ide",
	.match		= ide_bus_match,
	.uevent		= ide_uevent,
	.probe		= generic_ide_probe,
	.remove		= generic_ide_remove,
	.shutdown	= generic_ide_shutdown,
	.dev_attrs	= ide_dev_attrs,
	.suspend	= generic_ide_suspend,
	.resume		= generic_ide_resume,
};

EXPORT_SYMBOL_GPL(ide_bus_type);

/*
 * This is gets invoked once during initialization, to set *everything* up
 */
static int __init ide_init(void)
{
	printk(KERN_INFO "Uniform Multi-Platform E-IDE driver " REVISION "\n");
	devfs_mk_dir("ide");
	system_bus_speed = ide_system_bus_speed();

	bus_register(&ide_bus_type);

	init_ide_data();

#ifdef CONFIG_PROC_FS
	proc_ide_root = proc_mkdir("ide", NULL);
#endif

#ifdef CONFIG_BLK_DEV_ALI14XX
	if (probe_ali14xx)
		(void)ali14xx_init();
#endif
#ifdef CONFIG_BLK_DEV_UMC8672
	if (probe_umc8672)
		(void)umc8672_init();
#endif
#ifdef CONFIG_BLK_DEV_DTC2278
	if (probe_dtc2278)
		(void)dtc2278_init();
#endif
#ifdef CONFIG_BLK_DEV_HT6560B
	if (probe_ht6560b)
		(void)ht6560b_init();
#endif
#ifdef CONFIG_BLK_DEV_QD65XX
	if (probe_qd65xx)
		(void)qd65xx_init();
#endif

	initializing = 1;
	/* Probe for special PCI and other "known" interface chipsets. */
	probe_for_hwifs();
	initializing = 0;

#ifdef CONFIG_PROC_FS
	proc_ide_create();
#endif
	return 0;
}

#ifdef MODULE
static char *options = NULL;
module_param(options, charp, 0);
MODULE_LICENSE("GPL");

static void __init parse_options (char *line)
{
	char *next = line;

	if (line == NULL || !*line)
		return;
	while ((line = next) != NULL) {
 		if ((next = strchr(line,' ')) != NULL)
			*next++ = 0;
		if (!ide_setup(line))
			printk (KERN_INFO "Unknown option '%s'\n", line);
	}
}

int init_module (void)
{
	parse_options(options);
	return ide_init();
}

void cleanup_module (void)
{
	int index;

	for (index = 0; index < MAX_HWIFS; ++index)
		ide_unregister(index);

#ifdef CONFIG_PROC_FS
	proc_ide_destroy();
#endif
	devfs_remove("ide");

	bus_unregister(&ide_bus_type);
}

#else /* !MODULE */

__setup("", ide_setup);

module_init(ide_init);

#endif /* MODULE */
