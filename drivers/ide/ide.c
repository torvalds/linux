/*
 *  Copyright (C) 1994-1998	    Linus Torvalds & authors (see below)
 *  Copyrifht (C) 2003-2005, 2007   Bartlomiej Zolnierkiewicz
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
 */

#define	REVISION	"Revision: 7.00alpha2"

#define _IDE_C			/* Tell ide.h it's really us */

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

DEFINE_MUTEX(ide_cfg_mtx);
 __cacheline_aligned_in_smp DEFINE_SPINLOCK(ide_lock);

#ifdef CONFIG_IDEPCI_PCIBUS_ORDER
int ide_scan_direction; /* THIS was formerly 2.2.x pci=reverse */
#endif

int noautodma = 0;

#ifdef CONFIG_BLK_DEV_IDEACPI
int ide_noacpi = 0;
int ide_noacpitfs = 1;
int ide_noacpionboot = 1;
#endif

/*
 * This is declared extern in ide.h, for access by other IDE modules:
 */
ide_hwif_t ide_hwifs[MAX_HWIFS];	/* master data repository */

EXPORT_SYMBOL(ide_hwifs);

/*
 * Do not even *think* about calling this!
 */
void ide_init_port_data(ide_hwif_t *hwif, unsigned int index)
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
		drive->vdma			= 0;
		INIT_LIST_HEAD(&drive->list);
		init_completion(&drive->gendev_rel_comp);
	}
}
EXPORT_SYMBOL_GPL(ide_init_port_data);

static void init_hwif_default(ide_hwif_t *hwif, unsigned int index)
{
	hw_regs_t hw;

	memset(&hw, 0, sizeof(hw_regs_t));

	ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, &hwif->irq);

	memcpy(hwif->io_ports, hw.io_ports, sizeof(hw.io_ports));

	hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		hwif->noprobe = 1;	/* may be overridden by ide_setup() */
#endif
}

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
		ide_init_port_data(hwif, index);
		init_hwif_default(hwif, index);
#if !defined(CONFIG_PPC32) || !defined(CONFIG_PCI)
		hwif->irq =
			ide_init_default_irq(hwif->io_ports[IDE_DATA_OFFSET]);
#endif
	}
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

	/* user supplied value */
	if (idebus_parameter)
		return idebus_parameter;

	/* safe default value for PCI or VESA and PCI*/
	return pci_dev_present(pci_default) ? 33 : 50;
}

ide_hwif_t * ide_find_port(unsigned long base)
{
	ide_hwif_t *hwif;
	int i;

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = &ide_hwifs[i];
		if (hwif->io_ports[IDE_DATA_OFFSET] == base)
			goto found;
	}

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = &ide_hwifs[i];
		if (hwif->io_ports[IDE_DATA_OFFSET] == 0)
			goto found;
	}

	hwif = NULL;
found:
	return hwif;
}

EXPORT_SYMBOL_GPL(ide_find_port);

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

	if (hwif->mmio)
		return 0;
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

	if (hwif->mmio)
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
 *	Restore hwif to a previous state by copying most settings
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

	hwif->host_flags		= tmp_hwif->host_flags;

	hwif->pio_mask			= tmp_hwif->pio_mask;

	hwif->ultra_mask		= tmp_hwif->ultra_mask;
	hwif->mwdma_mask		= tmp_hwif->mwdma_mask;
	hwif->swdma_mask		= tmp_hwif->swdma_mask;

	hwif->cbl			= tmp_hwif->cbl;

	hwif->chipset			= tmp_hwif->chipset;
	hwif->hold			= tmp_hwif->hold;

	hwif->dev			= tmp_hwif->dev;

#ifdef CONFIG_BLK_DEV_IDEPCI
	hwif->cds			= tmp_hwif->cds;
#endif

	hwif->set_pio_mode		= tmp_hwif->set_pio_mode;
	hwif->set_dma_mode		= tmp_hwif->set_dma_mode;
	hwif->mdma_filter		= tmp_hwif->mdma_filter;
	hwif->udma_filter		= tmp_hwif->udma_filter;
	hwif->selectproc		= tmp_hwif->selectproc;
	hwif->reset_poll		= tmp_hwif->reset_poll;
	hwif->pre_reset			= tmp_hwif->pre_reset;
	hwif->resetproc			= tmp_hwif->resetproc;
	hwif->maskproc			= tmp_hwif->maskproc;
	hwif->quirkproc			= tmp_hwif->quirkproc;
	hwif->busproc			= tmp_hwif->busproc;

	hwif->ata_input_data		= tmp_hwif->ata_input_data;
	hwif->ata_output_data		= tmp_hwif->ata_output_data;
	hwif->atapi_input_bytes		= tmp_hwif->atapi_input_bytes;
	hwif->atapi_output_bytes	= tmp_hwif->atapi_output_bytes;

	hwif->dma_host_set		= tmp_hwif->dma_host_set;
	hwif->dma_setup			= tmp_hwif->dma_setup;
	hwif->dma_exec_cmd		= tmp_hwif->dma_exec_cmd;
	hwif->dma_start			= tmp_hwif->dma_start;
	hwif->ide_dma_end		= tmp_hwif->ide_dma_end;
	hwif->ide_dma_test_irq		= tmp_hwif->ide_dma_test_irq;
	hwif->ide_dma_clear_irq		= tmp_hwif->ide_dma_clear_irq;
	hwif->dma_lost_irq		= tmp_hwif->dma_lost_irq;
	hwif->dma_timeout		= tmp_hwif->dma_timeout;

	hwif->OUTB			= tmp_hwif->OUTB;
	hwif->OUTBSYNC			= tmp_hwif->OUTBSYNC;
	hwif->OUTW			= tmp_hwif->OUTW;
	hwif->OUTSW			= tmp_hwif->OUTSW;
	hwif->OUTSL			= tmp_hwif->OUTSL;

	hwif->INB			= tmp_hwif->INB;
	hwif->INW			= tmp_hwif->INW;
	hwif->INSW			= tmp_hwif->INSW;
	hwif->INSL			= tmp_hwif->INSL;

	hwif->sg_max_nents		= tmp_hwif->sg_max_nents;

	hwif->mmio			= tmp_hwif->mmio;
	hwif->rqsize			= tmp_hwif->rqsize;

#ifndef CONFIG_BLK_DEV_IDECS
	hwif->irq			= tmp_hwif->irq;
#endif

	hwif->dma_base			= tmp_hwif->dma_base;
	hwif->dma_command		= tmp_hwif->dma_command;
	hwif->dma_vendor1		= tmp_hwif->dma_vendor1;
	hwif->dma_status		= tmp_hwif->dma_status;
	hwif->dma_vendor3		= tmp_hwif->dma_vendor3;
	hwif->dma_prdtable		= tmp_hwif->dma_prdtable;

	hwif->config_data		= tmp_hwif->config_data;
	hwif->select_data		= tmp_hwif->select_data;
	hwif->extra_base		= tmp_hwif->extra_base;
	hwif->extra_ports		= tmp_hwif->extra_ports;

	hwif->hwif_data			= tmp_hwif->hwif_data;
}

void ide_remove_port_from_hwgroup(ide_hwif_t *hwif)
{
	ide_hwgroup_t *hwgroup = hwif->hwgroup;

	spin_lock_irq(&ide_lock);
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
		ide_hwif_t *g = hwgroup->hwif;

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
	spin_unlock_irq(&ide_lock);
}

/**
 *	ide_unregister		-	free an IDE interface
 *	@index: index of interface (will change soon to a pointer)
 *	@init_default: init default hwif flag
 *	@restore: restore hwif flag
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
 *	the interface will not be reopened (present/vanishing locking
 *	isn't yet done BTW). After we commit to the final kill we
 *	call the cleanup callback with the ide locks held.
 *
 *	Unregister restores the hwif structures to the default state.
 *	This is raving bonkers.
 */

void ide_unregister(unsigned int index, int init_default, int restore)
{
	ide_drive_t *drive;
	ide_hwif_t *hwif, *g;
	static ide_hwif_t tmp_hwif; /* protected by ide_cfg_mtx */
	ide_hwgroup_t *hwgroup;
	int irq_count = 0, unit;

	BUG_ON(index >= MAX_HWIFS);

	BUG_ON(in_interrupt());
	BUG_ON(irqs_disabled());
	mutex_lock(&ide_cfg_mtx);
	spin_lock_irq(&ide_lock);
	hwif = &ide_hwifs[index];
	if (!hwif->present)
		goto abort;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &hwif->drives[unit];
		if (!drive->present)
			continue;
		spin_unlock_irq(&ide_lock);
		device_unregister(&drive->gendev);
		wait_for_completion(&drive->gendev_rel_comp);
		spin_lock_irq(&ide_lock);
	}
	hwif->present = 0;

	spin_unlock_irq(&ide_lock);

	ide_proc_unregister_port(hwif);

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

	ide_remove_port_from_hwgroup(hwif);

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
		hwif->dma_command = 0;
		hwif->dma_vendor1 = 0;
		hwif->dma_status = 0;
		hwif->dma_vendor3 = 0;
		hwif->dma_prdtable = 0;

		hwif->extra_base  = 0;
		hwif->extra_ports = 0;
	}

	/*
	 * Note that we only release the standard ports,
	 * and do not even try to handle any extra ports
	 * allocated for weird IDE interface chipsets.
	 */
	ide_hwif_release_regions(hwif);

	/* copy original settings */
	tmp_hwif = *hwif;

	/* restore hwif data to pristine status */
	ide_init_port_data(hwif, index);

	if (init_default)
		init_hwif_default(hwif, index);

	if (restore)
		ide_hwif_restore(hwif, &tmp_hwif);

abort:
	spin_unlock_irq(&ide_lock);
	mutex_unlock(&ide_cfg_mtx);
}

EXPORT_SYMBOL(ide_unregister);

void ide_init_port_hw(ide_hwif_t *hwif, hw_regs_t *hw)
{
	memcpy(hwif->io_ports, hw->io_ports, sizeof(hwif->io_ports));
	hwif->irq = hw->irq;
	hwif->noprobe = 0;
	hwif->chipset = hw->chipset;
	hwif->gendev.parent = hw->dev;
	hwif->ack_intr = hw->ack_intr;
}
EXPORT_SYMBOL_GPL(ide_init_port_hw);

ide_hwif_t *ide_deprecated_find_port(unsigned long base)
{
	ide_hwif_t *hwif;
	int i;

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = &ide_hwifs[i];
		if (hwif->io_ports[IDE_DATA_OFFSET] == base)
			goto found;
	}

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = &ide_hwifs[i];
		if (hwif->hold)
			continue;
		if (!hwif->present && hwif->mate == NULL)
			goto found;
	}

	hwif = NULL;
found:
	return hwif;
}
EXPORT_SYMBOL_GPL(ide_deprecated_find_port);

/**
 *	ide_register_hw		-	register IDE interface
 *	@hw: hardware registers
 *	@quirkproc: quirkproc function
 *	@hwifp: pointer to returned hwif
 *
 *	Register an IDE interface, specifying exactly the registers etc.
 *
 *	Returns -1 on error.
 */

int ide_register_hw(hw_regs_t *hw, void (*quirkproc)(ide_drive_t *),
		    ide_hwif_t **hwifp)
{
	int index, retry = 1;
	ide_hwif_t *hwif;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	do {
		hwif = ide_deprecated_find_port(hw->io_ports[IDE_DATA_OFFSET]);
		index = hwif->index;
		if (hwif)
			goto found;
		for (index = 0; index < MAX_HWIFS; index++)
			ide_unregister(index, 1, 1);
	} while (retry--);
	return -1;
found:
	if (hwif->present)
		ide_unregister(index, 0, 1);
	else if (!hwif->hold)
		ide_init_port_data(hwif, index);

	ide_init_port_hw(hwif, hw);
	hwif->quirkproc = quirkproc;

	idx[0] = index;

	ide_device_add(idx, NULL);

	if (hwifp)
		*hwifp = hwif;

	return hwif->present ? index : -1;
}

EXPORT_SYMBOL(ide_register_hw);

/*
 *	Locks for IDE setting functionality
 */

DEFINE_MUTEX(ide_setting_mtx);

EXPORT_SYMBOL_GPL(ide_setting_mtx);

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

int set_io_32bit(ide_drive_t *drive, int arg)
{
	if (drive->no_io_32bit)
		return -EPERM;

	if (arg < 0 || arg > 1 + (SUPPORT_VLB_SYNC << 1))
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;

	drive->io_32bit = arg;

	spin_unlock_irq(&ide_lock);

	return 0;
}

static int set_ksettings(ide_drive_t *drive, int arg)
{
	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->keep_settings = arg;
	spin_unlock_irq(&ide_lock);

	return 0;
}

int set_using_dma(ide_drive_t *drive, int arg)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	ide_hwif_t *hwif = drive->hwif;
	int err = -EPERM;

	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (!drive->id || !(drive->id->capability & 1))
		goto out;

	if (hwif->dma_host_set == NULL)
		goto out;

	err = -EBUSY;
	if (ide_spin_wait_hwgroup(drive))
		goto out;
	/*
	 * set ->busy flag, unlock and let it ride
	 */
	hwif->hwgroup->busy = 1;
	spin_unlock_irq(&ide_lock);

	err = 0;

	if (arg) {
		if (ide_set_dma(drive))
			err = -EIO;
	} else
		ide_dma_off(drive);

	/*
	 * lock, clear ->busy flag and unlock before leaving
	 */
	spin_lock_irq(&ide_lock);
	hwif->hwgroup->busy = 0;
	spin_unlock_irq(&ide_lock);
out:
	return err;
#else
	if (arg < 0 || arg > 1)
		return -EINVAL;

	return -EPERM;
#endif
}

int set_pio_mode(ide_drive_t *drive, int arg)
{
	struct request rq;

	if (arg < 0 || arg > 255)
		return -EINVAL;

	if (drive->hwif->set_pio_mode == NULL)
		return -ENOSYS;

	if (drive->special.b.set_tune)
		return -EBUSY;

	ide_init_drive_cmd(&rq);
	rq.cmd_type = REQ_TYPE_ATA_TASKFILE;

	drive->tune_req = (u8) arg;
	drive->special.b.set_tune = 1;
	(void) ide_do_drive_cmd(drive, &rq, ide_wait);
	return 0;
}

static int set_unmaskirq(ide_drive_t *drive, int arg)
{
	if (drive->no_unmask)
		return -EPERM;

	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->unmask = arg;
	spin_unlock_irq(&ide_lock);

	return 0;
}

/**
 *	system_bus_clock	-	clock guess
 *
 *	External version of the bus clock guess used by very old IDE drivers
 *	for things like VLB timings. Should not be used.
 */

int system_bus_clock (void)
{
	return system_bus_speed;
}

EXPORT_SYMBOL(system_bus_clock);

static int generic_ide_suspend(struct device *dev, pm_message_t mesg)
{
	ide_drive_t *drive = dev->driver_data;
	ide_hwif_t *hwif = HWIF(drive);
	struct request rq;
	struct request_pm_state rqpm;
	ide_task_t args;
	int ret;

	/* Call ACPI _GTM only once */
	if (!(drive->dn % 2))
		ide_acpi_get_timing(hwif);

	memset(&rq, 0, sizeof(rq));
	memset(&rqpm, 0, sizeof(rqpm));
	memset(&args, 0, sizeof(args));
	rq.cmd_type = REQ_TYPE_PM_SUSPEND;
	rq.special = &args;
	rq.data = &rqpm;
	rqpm.pm_step = ide_pm_state_start_suspend;
	if (mesg.event == PM_EVENT_PRETHAW)
		mesg.event = PM_EVENT_FREEZE;
	rqpm.pm_state = mesg.event;

	ret = ide_do_drive_cmd(drive, &rq, ide_wait);
	/* only call ACPI _PS3 after both drivers are suspended */
	if (!ret && (((drive->dn % 2) && hwif->drives[0].present
		 && hwif->drives[1].present)
		 || !hwif->drives[0].present
		 || !hwif->drives[1].present))
		ide_acpi_set_state(hwif, 0);
	return ret;
}

static int generic_ide_resume(struct device *dev)
{
	ide_drive_t *drive = dev->driver_data;
	ide_hwif_t *hwif = HWIF(drive);
	struct request rq;
	struct request_pm_state rqpm;
	ide_task_t args;
	int err;

	/* Call ACPI _STM only once */
	if (!(drive->dn % 2)) {
		ide_acpi_set_state(hwif, 1);
		ide_acpi_push_timing(hwif);
	}

	ide_acpi_exec_tfs(drive);

	memset(&rq, 0, sizeof(rq));
	memset(&rqpm, 0, sizeof(rqpm));
	memset(&args, 0, sizeof(args));
	rq.cmd_type = REQ_TYPE_PM_RESUME;
	rq.special = &args;
	rq.data = &rqpm;
	rqpm.pm_step = ide_pm_state_start_resume;
	rqpm.pm_state = PM_EVENT_ON;

	err = ide_do_drive_cmd(drive, &rq, ide_head_wait);

	if (err == 0 && dev->driver) {
		ide_driver_t *drv = to_ide_driver(dev->driver);

		if (drv->resume)
			drv->resume(drive);
	}

	return err;
}

int generic_ide_ioctl(ide_drive_t *drive, struct file *file, struct block_device *bdev,
			unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	ide_driver_t *drv;
	void __user *p = (void __user *)arg;
	int err = 0, (*setfunc)(ide_drive_t *, int);
	u8 *val;

	switch (cmd) {
	case HDIO_GET_32BIT:	    val = &drive->io_32bit;	 goto read_val;
	case HDIO_GET_KEEPSETTINGS: val = &drive->keep_settings; goto read_val;
	case HDIO_GET_UNMASKINTR:   val = &drive->unmask;	 goto read_val;
	case HDIO_GET_DMA:	    val = &drive->using_dma;	 goto read_val;
	case HDIO_SET_32BIT:	    setfunc = set_io_32bit;	 goto set_val;
	case HDIO_SET_KEEPSETTINGS: setfunc = set_ksettings;	 goto set_val;
	case HDIO_SET_PIO_MODE:	    setfunc = set_pio_mode;	 goto set_val;
	case HDIO_SET_UNMASKINTR:   setfunc = set_unmaskirq;	 goto set_val;
	case HDIO_SET_DMA:	    setfunc = set_using_dma;	 goto set_val;
	}

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
					drive->nice1 << IDE_NICE_1,
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
			if (ide_register_hw(&hw, NULL, NULL) == -1)
				return -EIO;
			return 0;
		}
	        case HDIO_UNREGISTER_HWIF:
			if (!capable(CAP_SYS_RAWIO)) return -EACCES;
			/* (arg > MAX_HWIFS) checked in function */
			ide_unregister(arg, 1, 1);
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

			if (HWGROUP(drive)->resetting) {
				spin_unlock_irqrestore(&ide_lock, flags);
				return -EBUSY;
			}

			ide_abort(drive, "drive reset");

			BUG_ON(HWGROUP(drive)->handler);
				
			/* Ensure nothing gets queued after we
			   drop the lock. Reset will clear the busy */
		   
			HWGROUP(drive)->busy = 1;
			spin_unlock_irqrestore(&ide_lock, flags);
			(void) ide_do_reset(drive);

			return 0;
		}

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

read_val:
	mutex_lock(&ide_setting_mtx);
	spin_lock_irqsave(&ide_lock, flags);
	err = *val;
	spin_unlock_irqrestore(&ide_lock, flags);
	mutex_unlock(&ide_setting_mtx);
	return err >= 0 ? put_user(err, (long __user *)arg) : err;

set_val:
	if (bdev != bdev->bd_contains)
		err = -EINVAL;
	else {
		if (!capable(CAP_SYS_ADMIN))
			err = -EACCES;
		else {
			mutex_lock(&ide_setting_mtx);
			err = setfunc(drive, arg);
			mutex_unlock(&ide_setting_mtx);
		}
	}
	return err;
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

extern int probe_ali14xx;
extern int probe_umc8672;
extern int probe_dtc2278;
extern int probe_ht6560b;
extern int probe_qd65xx;
extern int cmd640_vlb;

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
		goto obsolete_option;
	}

#ifdef CONFIG_IDEPCI_PCIBUS_ORDER
	if (!strcmp(s, "ide=reverse")) {
		ide_scan_direction = 1;
		printk(" : Enabled support for IDE inverse scan order.\n");
		return 1;
	}
#endif

#ifdef CONFIG_BLK_DEV_IDEACPI
	if (!strcmp(s, "ide=noacpi")) {
		//printk(" : Disable IDE ACPI support.\n");
		ide_noacpi = 1;
		return 1;
	}
	if (!strcmp(s, "ide=acpigtf")) {
		//printk(" : Enable IDE ACPI _GTF support.\n");
		ide_noacpitfs = 0;
		return 1;
	}
	if (!strcmp(s, "ide=acpionboot")) {
		//printk(" : Call IDE ACPI methods on boot.\n");
		ide_noacpionboot = 0;
		return 1;
	}
#endif /* CONFIG_BLK_DEV_IDEACPI */

	/*
	 * Look for drive options:  "hdx="
	 */
	if (s[0] == 'h' && s[1] == 'd' && s[2] >= 'a' && s[2] <= max_drive) {
		const char *hd_words[] = {
			"none", "noprobe", "nowerr", "cdrom", "nodma",
			"autotune", "noautotune", "-8", "-9", "-10",
			"noflush", "remap", "remap63", "scsi", NULL };
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
			case -5: /* nodma */
				drive->nodma = 1;
				goto done;
			case -6: /* "autotune" */
				drive->autotune = IDE_TUNE_AUTO;
				goto obsolete_option;
			case -7: /* "noautotune" */
				drive->autotune = IDE_TUNE_NOAUTO;
				goto obsolete_option;
			case -11: /* noflush */
				drive->noflush = 1;
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
			"noprobe", "serialize", "minus3", "minus4",
			"reset", "minus6", "ata66", "minus8", "minus9",
			"minus10", "four", "qd65xx", "ht6560b", "cmd640_vlb",
			"dtc2278", "umc8672", "ali14xx", NULL };

		hw_regs_t hwregs;

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
				cmd640_vlb = 1;
				goto done;
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
				hwif->mate = mate;
				mate->mate = hwif;
				hwif->serialized = mate->serialized = 1;
				goto obsolete_option;
			}
#endif /* CONFIG_BLK_DEV_4DRIVES */
			case -10: /* minus10 */
			case -9: /* minus9 */
			case -8: /* minus8 */
			case -6:
			case -4:
			case -3:
				goto bad_option;
			case -7: /* ata66 */
#ifdef CONFIG_BLK_DEV_IDEPCI
				/*
				 * Use ATA_CBL_PATA40_SHORT so drive side
				 * cable detection is also overriden.
				 */
				hwif->cbl = ATA_CBL_PATA40_SHORT;
				goto obsolete_option;
#else
				goto bad_hwif;
#endif
			case -5: /* "reset" */
				hwif->reset = 1;
				goto obsolete_option;
			case -2: /* "serialize" */
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
				memset(&hwregs, 0, sizeof(hwregs));
				ide_init_hwif_ports(&hwregs, vals[0], vals[1], &hwif->irq);
				memcpy(hwif->io_ports, hwregs.io_ports, sizeof(hwif->io_ports));
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
	case ide_optical:
		return "optical";
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

static ssize_t model_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", drive->id->model);
}

static ssize_t firmware_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", drive->id->fw_rev);
}

static ssize_t serial_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", drive->id->serial_no);
}

static struct device_attribute ide_dev_attrs[] = {
	__ATTR_RO(media),
	__ATTR_RO(drivename),
	__ATTR_RO(modalias),
	__ATTR_RO(model),
	__ATTR_RO(firmware),
	__ATTR(serial, 0400, serial_show, NULL),
	__ATTR_NULL
};

static int ide_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	ide_drive_t *drive = to_ide_device(dev);

	add_uevent_var(env, "MEDIA=%s", media_string(drive));
	add_uevent_var(env, "DRIVENAME=%s", drive->name);
	add_uevent_var(env, "MODALIAS=ide:m-%s", media_string(drive));
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
	int ret;

	printk(KERN_INFO "Uniform Multi-Platform E-IDE driver " REVISION "\n");
	system_bus_speed = ide_system_bus_speed();

	printk(KERN_INFO "ide: Assuming %dMHz system bus speed "
			 "for PIO modes%s\n", system_bus_speed,
			idebus_parameter ? "" : "; override with idebus=xx");

	ret = bus_register(&ide_bus_type);
	if (ret < 0) {
		printk(KERN_WARNING "IDE: bus_register error: %d\n", ret);
		return ret;
	}

	init_ide_data();

	proc_ide_create();

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

int __init init_module (void)
{
	parse_options(options);
	return ide_init();
}

void __exit cleanup_module (void)
{
	int index;

	for (index = 0; index < MAX_HWIFS; ++index)
		ide_unregister(index, 0, 0);

	proc_ide_destroy();

	bus_unregister(&ide_bus_type);
}

#else /* !MODULE */

__setup("", ide_setup);

module_init(ide_init);

#endif /* MODULE */
