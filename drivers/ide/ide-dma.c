/*
 *  linux/drivers/ide/ide-dma.c		Version 4.10	June 9, 2000
 *
 *  Copyright (c) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 *  Special Thanks to Mark for his Six years of work.
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA functions
 * of various PCI chipsets, including the Intel PIIX (i82371FB for
 * the 430 FX chipset), the PIIX3 (i82371SB for the 430 HX/VX and 
 * 440 chipsets), and the PIIX4 (i82371AB for the 430 TX chipset)
 * ("PIIX" stands for "PCI ISA IDE Xcellerator").
 *
 * Pretty much the same code works for other IDE PCI bus-mastering chipsets.
 *
 * DMA is supported for all IDE devices (disk drives, cdroms, tapes, floppies).
 *
 * By default, DMA support is prepared for use, but is currently enabled only
 * for drives which already have DMA enabled (UltraDMA or mode 2 multi/single),
 * or which are recognized as "good" (see table below).  Drives with only mode0
 * or mode1 (multi/single) DMA should also work with this chipset/driver
 * (eg. MC2112A) but are not enabled by default.
 *
 * Use "hdparm -i" to view modes supported by a given drive.
 *
 * The hdparm-3.5 (or later) utility can be used for manually enabling/disabling
 * DMA support, but must be (re-)compiled against this kernel version or later.
 *
 * To enable DMA, use "hdparm -d1 /dev/hd?" on a per-drive basis after booting.
 * If problems arise, ide.c will disable DMA operation after a few retries.
 * This error recovery mechanism works and has been extremely well exercised.
 *
 * IDE drives, depending on their vintage, may support several different modes
 * of DMA operation.  The boot-time modes are indicated with a "*" in
 * the "hdparm -i" listing, and can be changed with *knowledgeable* use of
 * the "hdparm -X" feature.  There is seldom a need to do this, as drives
 * normally power-up with their "best" PIO/DMA modes enabled.
 *
 * Testing has been done with a rather extensive number of drives,
 * with Quantum & Western Digital models generally outperforming the pack,
 * and Fujitsu & Conner (and some Seagate which are really Conner) drives
 * showing more lackluster throughput.
 *
 * Keep an eye on /var/adm/messages for "DMA disabled" messages.
 *
 * Some people have reported trouble with Intel Zappa motherboards.
 * This can be fixed by upgrading the AMI BIOS to version 1.00.04.BS0,
 * available from ftp://ftp.intel.com/pub/bios/10004bs0.exe
 * (thanks to Glen Morrell <glen@spin.Stanford.edu> for researching this).
 *
 * Thanks to "Christopher J. Reimer" <reimer@doe.carleton.ca> for
 * fixing the problem with the BIOS on some Acer motherboards.
 *
 * Thanks to "Benoit Poulot-Cazajous" <poulot@chorus.fr> for testing
 * "TX" chipset compatibility and for providing patches for the "TX" chipset.
 *
 * Thanks to Christian Brunner <chb@muc.de> for taking a good first crack
 * at generic DMA -- his patches were referred to when preparing this code.
 *
 * Most importantly, thanks to Robert Bringman <rob@mars.trion.com>
 * for supplying a Promise UDMA board & WD UDMA drive for this work!
 *
 * And, yes, Intel Zappa boards really *do* use both PIIX IDE ports.
 *
 * ATA-66/100 and recovery functions, I forgot the rest......
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>

#include <asm/io.h>
#include <asm/irq.h>

struct drive_list_entry {
	const char *id_model;
	const char *id_firmware;
};

static const struct drive_list_entry drive_whitelist [] = {

	{ "Micropolis 2112A"	,       "ALL"		},
	{ "CONNER CTMA 4000"	,       "ALL"		},
	{ "CONNER CTT8000-A"	,       "ALL"		},
	{ "ST34342A"		,	"ALL"		},
	{ NULL			,	NULL		}
};

static const struct drive_list_entry drive_blacklist [] = {

	{ "WDC AC11000H"	,	"ALL"		},
	{ "WDC AC22100H"	,	"ALL"		},
	{ "WDC AC32500H"	,	"ALL"		},
	{ "WDC AC33100H"	,	"ALL"		},
	{ "WDC AC31600H"	,	"ALL"		},
	{ "WDC AC32100H"	,	"24.09P07"	},
	{ "WDC AC23200L"	,	"21.10N21"	},
	{ "Compaq CRD-8241B"	,	"ALL"		},
	{ "CRD-8400B"		,	"ALL"		},
	{ "CRD-8480B",			"ALL"		},
	{ "CRD-8482B",			"ALL"		},
 	{ "CRD-84"		,	"ALL"		},
	{ "SanDisk SDP3B"	,	"ALL"		},
	{ "SanDisk SDP3B-64"	,	"ALL"		},
	{ "SANYO CD-ROM CRD"	,	"ALL"		},
	{ "HITACHI CDR-8"	,	"ALL"		},
	{ "HITACHI CDR-8335"	,	"ALL"		},
	{ "HITACHI CDR-8435"	,	"ALL"		},
	{ "Toshiba CD-ROM XM-6202B"	,	"ALL"		},
	{ "CD-532E-A"		,	"ALL"		},
	{ "E-IDE CD-ROM CR-840",	"ALL"		},
	{ "CD-ROM Drive/F5A",	"ALL"		},
	{ "WPI CDD-820",		"ALL"		},
	{ "SAMSUNG CD-ROM SC-148C",	"ALL"		},
	{ "SAMSUNG CD-ROM SC",	"ALL"		},
	{ "SanDisk SDP3B-64"	,	"ALL"		},
	{ "ATAPI CD-ROM DRIVE 40X MAXIMUM",	"ALL"		},
	{ "_NEC DV5800A",               "ALL"           },  
	{ NULL			,	NULL		}

};

/**
 *	in_drive_list	-	look for drive in black/white list
 *	@id: drive identifier
 *	@drive_table: list to inspect
 *
 *	Look for a drive in the blacklist and the whitelist tables
 *	Returns 1 if the drive is found in the table.
 */

static int in_drive_list(struct hd_driveid *id, const struct drive_list_entry *drive_table)
{
	for ( ; drive_table->id_model ; drive_table++)
		if ((!strcmp(drive_table->id_model, id->model)) &&
		    ((strstr(drive_table->id_firmware, id->fw_rev)) ||
		     (!strcmp(drive_table->id_firmware, "ALL"))))
			return 1;
	return 0;
}

/**
 *	ide_dma_intr	-	IDE DMA interrupt handler
 *	@drive: the drive the interrupt is for
 *
 *	Handle an interrupt completing a read/write DMA transfer on an 
 *	IDE device
 */
 
ide_startstop_t ide_dma_intr (ide_drive_t *drive)
{
	u8 stat = 0, dma_stat = 0;

	dma_stat = HWIF(drive)->ide_dma_end(drive);
	stat = HWIF(drive)->INB(IDE_STATUS_REG);	/* get drive status */
	if (OK_STAT(stat,DRIVE_READY,drive->bad_wstat|DRQ_STAT)) {
		if (!dma_stat) {
			struct request *rq = HWGROUP(drive)->rq;

			if (rq->rq_disk) {
				ide_driver_t *drv;

				drv = *(ide_driver_t **)rq->rq_disk->private_data;;
				drv->end_request(drive, 1, rq->nr_sectors);
			} else
				ide_end_request(drive, 1, rq->nr_sectors);
			return ide_stopped;
		}
		printk(KERN_ERR "%s: dma_intr: bad DMA status (dma_stat=%x)\n", 
		       drive->name, dma_stat);
	}
	return ide_error(drive, "dma_intr", stat);
}

EXPORT_SYMBOL_GPL(ide_dma_intr);

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
/**
 *	ide_build_sglist	-	map IDE scatter gather for DMA I/O
 *	@drive: the drive to build the DMA table for
 *	@rq: the request holding the sg list
 *
 *	Perform the PCI mapping magic necessary to access the source or
 *	target buffers of a request via PCI DMA. The lower layers of the
 *	kernel provide the necessary cache management so that we can
 *	operate in a portable fashion
 */

int ide_build_sglist(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct scatterlist *sg = hwif->sg_table;

	if ((rq->flags & REQ_DRIVE_TASKFILE) && rq->nr_sectors > 256)
		BUG();

	ide_map_sg(drive, rq);

	if (rq_data_dir(rq) == READ)
		hwif->sg_dma_direction = PCI_DMA_FROMDEVICE;
	else
		hwif->sg_dma_direction = PCI_DMA_TODEVICE;

	return pci_map_sg(hwif->pci_dev, sg, hwif->sg_nents, hwif->sg_dma_direction);
}

EXPORT_SYMBOL_GPL(ide_build_sglist);

/**
 *	ide_build_dmatable	-	build IDE DMA table
 *
 *	ide_build_dmatable() prepares a dma request. We map the command
 *	to get the pci bus addresses of the buffers and then build up
 *	the PRD table that the IDE layer wants to be fed. The code
 *	knows about the 64K wrap bug in the CS5530.
 *
 *	Returns the number of built PRD entries if all went okay,
 *	returns 0 otherwise.
 *
 *	May also be invoked from trm290.c
 */
 
int ide_build_dmatable (ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned int *table	= hwif->dmatable_cpu;
	unsigned int is_trm290	= (hwif->chipset == ide_trm290) ? 1 : 0;
	unsigned int count = 0;
	int i;
	struct scatterlist *sg;

	hwif->sg_nents = i = ide_build_sglist(drive, rq);

	if (!i)
		return 0;

	sg = hwif->sg_table;
	while (i) {
		u32 cur_addr;
		u32 cur_len;

		cur_addr = sg_dma_address(sg);
		cur_len = sg_dma_len(sg);

		/*
		 * Fill in the dma table, without crossing any 64kB boundaries.
		 * Most hardware requires 16-bit alignment of all blocks,
		 * but the trm290 requires 32-bit alignment.
		 */

		while (cur_len) {
			if (count++ >= PRD_ENTRIES) {
				printk(KERN_ERR "%s: DMA table too small\n", drive->name);
				goto use_pio_instead;
			} else {
				u32 xcount, bcount = 0x10000 - (cur_addr & 0xffff);

				if (bcount > cur_len)
					bcount = cur_len;
				*table++ = cpu_to_le32(cur_addr);
				xcount = bcount & 0xffff;
				if (is_trm290)
					xcount = ((xcount >> 2) - 1) << 16;
				if (xcount == 0x0000) {
	/* 
	 * Most chipsets correctly interpret a length of 0x0000 as 64KB,
	 * but at least one (e.g. CS5530) misinterprets it as zero (!).
	 * So here we break the 64KB entry into two 32KB entries instead.
	 */
					if (count++ >= PRD_ENTRIES) {
						printk(KERN_ERR "%s: DMA table too small\n", drive->name);
						goto use_pio_instead;
					}
					*table++ = cpu_to_le32(0x8000);
					*table++ = cpu_to_le32(cur_addr + 0x8000);
					xcount = 0x8000;
				}
				*table++ = cpu_to_le32(xcount);
				cur_addr += bcount;
				cur_len -= bcount;
			}
		}

		sg++;
		i--;
	}

	if (count) {
		if (!is_trm290)
			*--table |= cpu_to_le32(0x80000000);
		return count;
	}
	printk(KERN_ERR "%s: empty DMA table?\n", drive->name);
use_pio_instead:
	pci_unmap_sg(hwif->pci_dev,
		     hwif->sg_table,
		     hwif->sg_nents,
		     hwif->sg_dma_direction);
	return 0; /* revert to PIO for this request */
}

EXPORT_SYMBOL_GPL(ide_build_dmatable);

/**
 *	ide_destroy_dmatable	-	clean up DMA mapping
 *	@drive: The drive to unmap
 *
 *	Teardown mappings after DMA has completed. This must be called
 *	after the completion of each use of ide_build_dmatable and before
 *	the next use of ide_build_dmatable. Failure to do so will cause
 *	an oops as only one mapping can be live for each target at a given
 *	time.
 */
 
void ide_destroy_dmatable (ide_drive_t *drive)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	struct scatterlist *sg = HWIF(drive)->sg_table;
	int nents = HWIF(drive)->sg_nents;

	pci_unmap_sg(dev, sg, nents, HWIF(drive)->sg_dma_direction);
}

EXPORT_SYMBOL_GPL(ide_destroy_dmatable);

/**
 *	config_drive_for_dma	-	attempt to activate IDE DMA
 *	@drive: the drive to place in DMA mode
 *
 *	If the drive supports at least mode 2 DMA or UDMA of any kind
 *	then attempt to place it into DMA mode. Drives that are known to
 *	support DMA but predate the DMA properties or that are known
 *	to have DMA handling bugs are also set up appropriately based
 *	on the good/bad drive lists.
 */
 
static int config_drive_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);

	if ((id->capability & 1) && hwif->autodma) {
		/*
		 * Enable DMA on any drive that has
		 * UltraDMA (mode 0/1/2/3/4/5/6) enabled
		 */
		if ((id->field_valid & 4) && ((id->dma_ultra >> 8) & 0x7f))
			return hwif->ide_dma_on(drive);
		/*
		 * Enable DMA on any drive that has mode2 DMA
		 * (multi or single) enabled
		 */
		if (id->field_valid & 2)	/* regular DMA */
			if ((id->dma_mword & 0x404) == 0x404 ||
			    (id->dma_1word & 0x404) == 0x404)
				return hwif->ide_dma_on(drive);

		/* Consult the list of known "good" drives */
		if (__ide_dma_good_drive(drive))
			return hwif->ide_dma_on(drive);
	}
//	if (hwif->tuneproc != NULL) hwif->tuneproc(drive, 255);
	return hwif->ide_dma_off_quietly(drive);
}

/**
 *	dma_timer_expiry	-	handle a DMA timeout
 *	@drive: Drive that timed out
 *
 *	An IDE DMA transfer timed out. In the event of an error we ask
 *	the driver to resolve the problem, if a DMA transfer is still
 *	in progress we continue to wait (arguably we need to add a 
 *	secondary 'I don't care what the drive thinks' timeout here)
 *	Finally if we have an interrupt we let it complete the I/O.
 *	But only one time - we clear expiry and if it's still not
 *	completed after WAIT_CMD, we error and retry in PIO.
 *	This can occur if an interrupt is lost or due to hang or bugs.
 */
 
static int dma_timer_expiry (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

	printk(KERN_WARNING "%s: dma_timer_expiry: dma status == 0x%02x\n",
		drive->name, dma_stat);

	if ((dma_stat & 0x18) == 0x18)	/* BUSY Stupid Early Timer !! */
		return WAIT_CMD;

	HWGROUP(drive)->expiry = NULL;	/* one free ride for now */

	/* 1 dmaing, 2 error, 4 intr */
	if (dma_stat & 2)	/* ERROR */
		return -1;

	if (dma_stat & 1)	/* DMAing */
		return WAIT_CMD;

	if (dma_stat & 4)	/* Got an Interrupt */
		return WAIT_CMD;

	return 0;	/* Status is unknown -- reset the bus */
}

/**
 *	__ide_dma_host_off	-	Generic DMA kill
 *	@drive: drive to control
 *
 *	Perform the generic IDE controller DMA off operation. This
 *	works for most IDE bus mastering controllers
 */

int __ide_dma_host_off (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

	hwif->OUTB((dma_stat & ~(1<<(5+unit))), hwif->dma_status);
	return 0;
}

EXPORT_SYMBOL(__ide_dma_host_off);

/**
 *	__ide_dma_host_off_quietly	-	Generic DMA kill
 *	@drive: drive to control
 *
 *	Turn off the current DMA on this IDE controller. 
 */

int __ide_dma_off_quietly (ide_drive_t *drive)
{
	drive->using_dma = 0;
	ide_toggle_bounce(drive, 0);

	if (HWIF(drive)->ide_dma_host_off(drive))
		return 1;

	return 0;
}

EXPORT_SYMBOL(__ide_dma_off_quietly);
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */

/**
 *	__ide_dma_off	-	disable DMA on a device
 *	@drive: drive to disable DMA on
 *
 *	Disable IDE DMA for a device on this IDE controller.
 *	Inform the user that DMA has been disabled.
 */

int __ide_dma_off (ide_drive_t *drive)
{
	printk(KERN_INFO "%s: DMA disabled\n", drive->name);
	return HWIF(drive)->ide_dma_off_quietly(drive);
}

EXPORT_SYMBOL(__ide_dma_off);

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
/**
 *	__ide_dma_host_on	-	Enable DMA on a host
 *	@drive: drive to enable for DMA
 *
 *	Enable DMA on an IDE controller following generic bus mastering
 *	IDE controller behaviour
 */
 
int __ide_dma_host_on (ide_drive_t *drive)
{
	if (drive->using_dma) {
		ide_hwif_t *hwif	= HWIF(drive);
		u8 unit			= (drive->select.b.unit & 0x01);
		u8 dma_stat		= hwif->INB(hwif->dma_status);

		hwif->OUTB((dma_stat|(1<<(5+unit))), hwif->dma_status);
		return 0;
	}
	return 1;
}

EXPORT_SYMBOL(__ide_dma_host_on);

/**
 *	__ide_dma_on		-	Enable DMA on a device
 *	@drive: drive to enable DMA on
 *
 *	Enable IDE DMA for a device on this IDE controller.
 */
 
int __ide_dma_on (ide_drive_t *drive)
{
	/* consult the list of known "bad" drives */
	if (__ide_dma_bad_drive(drive))
		return 1;

	drive->using_dma = 1;
	ide_toggle_bounce(drive, 1);

	if (HWIF(drive)->ide_dma_host_on(drive))
		return 1;

	return 0;
}

EXPORT_SYMBOL(__ide_dma_on);

/**
 *	__ide_dma_check		-	check DMA setup
 *	@drive: drive to check
 *
 *	Don't use - due for extermination
 */
 
int __ide_dma_check (ide_drive_t *drive)
{
	return config_drive_for_dma(drive);
}

EXPORT_SYMBOL(__ide_dma_check);

/**
 *	ide_dma_setup	-	begin a DMA phase
 *	@drive: target device
 *
 *	Build an IDE DMA PRD (IDE speak for scatter gather table)
 *	and then set up the DMA transfer registers for a device
 *	that follows generic IDE PCI DMA behaviour. Controllers can
 *	override this function if they need to
 *
 *	Returns 0 on success. If a PIO fallback is required then 1
 *	is returned. 
 */

int ide_dma_setup(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned int reading;
	u8 dma_stat;

	if (rq_data_dir(rq))
		reading = 0;
	else
		reading = 1 << 3;

	/* fall back to pio! */
	if (!ide_build_dmatable(drive, rq)) {
		ide_map_sg(drive, rq);
		return 1;
	}

	/* PRD table */
	hwif->OUTL(hwif->dmatable_dma, hwif->dma_prdtable);

	/* specify r/w */
	hwif->OUTB(reading, hwif->dma_command);

	/* read dma_status for INTR & ERROR flags */
	dma_stat = hwif->INB(hwif->dma_status);

	/* clear INTR & ERROR flags */
	hwif->OUTB(dma_stat|6, hwif->dma_status);
	drive->waiting_for_dma = 1;
	return 0;
}

EXPORT_SYMBOL_GPL(ide_dma_setup);

static void ide_dma_exec_cmd(ide_drive_t *drive, u8 command)
{
	/* issue cmd to drive */
	ide_execute_command(drive, command, &ide_dma_intr, 2*WAIT_CMD, dma_timer_expiry);
}

void ide_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_cmd		= hwif->INB(hwif->dma_command);

	/* Note that this is done *after* the cmd has
	 * been issued to the drive, as per the BM-IDE spec.
	 * The Promise Ultra33 doesn't work correctly when
	 * we do this part before issuing the drive cmd.
	 */
	/* start DMA */
	hwif->OUTB(dma_cmd|1, hwif->dma_command);
	hwif->dma = 1;
	wmb();
}

EXPORT_SYMBOL_GPL(ide_dma_start);

/* returns 1 on error, 0 otherwise */
int __ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_stat = 0, dma_cmd = 0;

	drive->waiting_for_dma = 0;
	/* get dma_command mode */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop DMA */
	hwif->OUTB(dma_cmd&~1, hwif->dma_command);
	/* get DMA status */
	dma_stat = hwif->INB(hwif->dma_status);
	/* clear the INTR & ERROR bits */
	hwif->OUTB(dma_stat|6, hwif->dma_status);
	/* purge DMA mappings */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	hwif->dma = 0;
	wmb();
	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;
}

EXPORT_SYMBOL(__ide_dma_end);

/* returns 1 if dma irq issued, 0 otherwise */
static int __ide_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

#if 0  /* do not set unless you know what you are doing */
	if (dma_stat & 4) {
		u8 stat = hwif->INB(IDE_STATUS_REG);
		hwif->OUTB(hwif->dma_status, dma_stat & 0xE4);
	}
#endif
	/* return 1 if INTR asserted */
	if ((dma_stat & 4) == 4)
		return 1;
	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "%s: (%s) called while not waiting\n",
			drive->name, __FUNCTION__);
	return 0;
}
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */

int __ide_dma_bad_drive (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	int blacklist = in_drive_list(id, drive_blacklist);
	if (blacklist) {
		printk(KERN_WARNING "%s: Disabling (U)DMA for %s (blacklisted)\n",
				    drive->name, id->model);
		return blacklist;
	}
	return 0;
}

EXPORT_SYMBOL(__ide_dma_bad_drive);

int __ide_dma_good_drive (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	return in_drive_list(id, drive_whitelist);
}

EXPORT_SYMBOL(__ide_dma_good_drive);

int ide_use_dma(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = drive->hwif;

	/* consult the list of known "bad" drives */
	if (__ide_dma_bad_drive(drive))
		return 0;

	/* capable of UltraDMA modes */
	if (id->field_valid & 4) {
		if (hwif->ultra_mask & id->dma_ultra)
			return 1;
	}

	/* capable of regular DMA modes */
	if (id->field_valid & 2) {
		if (hwif->mwdma_mask & id->dma_mword)
			return 1;
		if (hwif->swdma_mask & id->dma_1word)
			return 1;
	}

	/* consult the list of known "good" drives */
	if (__ide_dma_good_drive(drive) && id->eide_dma_time < 150)
		return 1;

	return 0;
}

EXPORT_SYMBOL_GPL(ide_use_dma);

void ide_dma_verbose(ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);

	if (id->field_valid & 4) {
		if ((id->dma_ultra >> 8) && (id->dma_mword >> 8))
			goto bug_dma_off;
		if (id->dma_ultra & ((id->dma_ultra >> 8) & hwif->ultra_mask)) {
			if (((id->dma_ultra >> 11) & 0x1F) &&
			    eighty_ninty_three(drive)) {
				if ((id->dma_ultra >> 15) & 1) {
					printk(", UDMA(mode 7)");
				} else if ((id->dma_ultra >> 14) & 1) {
					printk(", UDMA(133)");
				} else if ((id->dma_ultra >> 13) & 1) {
					printk(", UDMA(100)");
				} else if ((id->dma_ultra >> 12) & 1) {
					printk(", UDMA(66)");
				} else if ((id->dma_ultra >> 11) & 1) {
					printk(", UDMA(44)");
				} else
					goto mode_two;
			} else {
		mode_two:
				if ((id->dma_ultra >> 10) & 1) {
					printk(", UDMA(33)");
				} else if ((id->dma_ultra >> 9) & 1) {
					printk(", UDMA(25)");
				} else if ((id->dma_ultra >> 8) & 1) {
					printk(", UDMA(16)");
				}
			}
		} else {
			printk(", (U)DMA");	/* Can be BIOS-enabled! */
		}
	} else if (id->field_valid & 2) {
		if ((id->dma_mword >> 8) && (id->dma_1word >> 8))
			goto bug_dma_off;
		printk(", DMA");
	} else if (id->field_valid & 1) {
		printk(", BUG");
	}
	return;
bug_dma_off:
	printk(", BUG DMA OFF");
	hwif->ide_dma_off_quietly(drive);
	return;
}

EXPORT_SYMBOL(ide_dma_verbose);

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
int __ide_dma_lostirq (ide_drive_t *drive)
{
	printk("%s: DMA interrupt recovery\n", drive->name);
	return 1;
}

EXPORT_SYMBOL(__ide_dma_lostirq);

int __ide_dma_timeout (ide_drive_t *drive)
{
	printk(KERN_ERR "%s: timeout waiting for DMA\n", drive->name);
	if (HWIF(drive)->ide_dma_test_irq(drive))
		return 0;

	return HWIF(drive)->ide_dma_end(drive);
}

EXPORT_SYMBOL(__ide_dma_timeout);

/*
 * Needed for allowing full modular support of ide-driver
 */
static int ide_release_dma_engine(ide_hwif_t *hwif)
{
	if (hwif->dmatable_cpu) {
		pci_free_consistent(hwif->pci_dev,
				    PRD_ENTRIES * PRD_BYTES,
				    hwif->dmatable_cpu,
				    hwif->dmatable_dma);
		hwif->dmatable_cpu = NULL;
	}
	return 1;
}

static int ide_release_iomio_dma(ide_hwif_t *hwif)
{
	if ((hwif->dma_extra) && (hwif->channel == 0))
		release_region((hwif->dma_base + 16), hwif->dma_extra);
	release_region(hwif->dma_base, 8);
	if (hwif->dma_base2)
		release_region(hwif->dma_base, 8);
	return 1;
}

/*
 * Needed for allowing full modular support of ide-driver
 */
int ide_release_dma (ide_hwif_t *hwif)
{
	if (hwif->mmio == 2)
		return 1;
	if (hwif->chipset == ide_etrax100)
		return 1;

	ide_release_dma_engine(hwif);
	return ide_release_iomio_dma(hwif);
}

static int ide_allocate_dma_engine(ide_hwif_t *hwif)
{
	hwif->dmatable_cpu = pci_alloc_consistent(hwif->pci_dev,
						  PRD_ENTRIES * PRD_BYTES,
						  &hwif->dmatable_dma);

	if (hwif->dmatable_cpu)
		return 0;

	printk(KERN_ERR "%s: -- Error, unable to allocate%s DMA table(s).\n",
			hwif->cds->name, !hwif->dmatable_cpu ? " CPU" : "");

	ide_release_dma_engine(hwif);
	return 1;
}

static int ide_mapped_mmio_dma(ide_hwif_t *hwif, unsigned long base, unsigned int ports)
{
	printk(KERN_INFO "    %s: MMIO-DMA ", hwif->name);

	hwif->dma_base = base;
	if (hwif->cds->extra && hwif->channel == 0)
		hwif->dma_extra = hwif->cds->extra;

	if(hwif->mate)
		hwif->dma_master = (hwif->channel) ? hwif->mate->dma_base : base;
	else
		hwif->dma_master = base;
	return 0;
}

static int ide_iomio_dma(ide_hwif_t *hwif, unsigned long base, unsigned int ports)
{
	printk(KERN_INFO "    %s: BM-DMA at 0x%04lx-0x%04lx",
		hwif->name, base, base + ports - 1);
	if (!request_region(base, ports, hwif->name)) {
		printk(" -- Error, ports in use.\n");
		return 1;
	}
	hwif->dma_base = base;
	if ((hwif->cds->extra) && (hwif->channel == 0)) {
		request_region(base+16, hwif->cds->extra, hwif->cds->name);
		hwif->dma_extra = hwif->cds->extra;
	}
	
	if(hwif->mate)
		hwif->dma_master = (hwif->channel) ? hwif->mate->dma_base : base;
	else
		hwif->dma_master = base;
	if (hwif->dma_base2) {
		if (!request_region(hwif->dma_base2, ports, hwif->name))
		{
			printk(" -- Error, secondary ports in use.\n");
			release_region(base, ports);
			return 1;
		}
	}
	return 0;
}

static int ide_dma_iobase(ide_hwif_t *hwif, unsigned long base, unsigned int ports)
{
	if (hwif->mmio == 2)
		return ide_mapped_mmio_dma(hwif, base,ports);
	BUG_ON(hwif->mmio == 1);
	return ide_iomio_dma(hwif, base, ports);
}

/*
 * This can be called for a dynamically installed interface. Don't __init it
 */
void ide_setup_dma (ide_hwif_t *hwif, unsigned long dma_base, unsigned int num_ports)
{
	if (ide_dma_iobase(hwif, dma_base, num_ports))
		return;

	if (ide_allocate_dma_engine(hwif)) {
		ide_release_dma(hwif);
		return;
	}

	if (!(hwif->dma_command))
		hwif->dma_command	= hwif->dma_base;
	if (!(hwif->dma_vendor1))
		hwif->dma_vendor1	= (hwif->dma_base + 1);
	if (!(hwif->dma_status))
		hwif->dma_status	= (hwif->dma_base + 2);
	if (!(hwif->dma_vendor3))
		hwif->dma_vendor3	= (hwif->dma_base + 3);
	if (!(hwif->dma_prdtable))
		hwif->dma_prdtable	= (hwif->dma_base + 4);

	if (!hwif->ide_dma_off_quietly)
		hwif->ide_dma_off_quietly = &__ide_dma_off_quietly;
	if (!hwif->ide_dma_host_off)
		hwif->ide_dma_host_off = &__ide_dma_host_off;
	if (!hwif->ide_dma_on)
		hwif->ide_dma_on = &__ide_dma_on;
	if (!hwif->ide_dma_host_on)
		hwif->ide_dma_host_on = &__ide_dma_host_on;
	if (!hwif->ide_dma_check)
		hwif->ide_dma_check = &__ide_dma_check;
	if (!hwif->dma_setup)
		hwif->dma_setup = &ide_dma_setup;
	if (!hwif->dma_exec_cmd)
		hwif->dma_exec_cmd = &ide_dma_exec_cmd;
	if (!hwif->dma_start)
		hwif->dma_start = &ide_dma_start;
	if (!hwif->ide_dma_end)
		hwif->ide_dma_end = &__ide_dma_end;
	if (!hwif->ide_dma_test_irq)
		hwif->ide_dma_test_irq = &__ide_dma_test_irq;
	if (!hwif->ide_dma_timeout)
		hwif->ide_dma_timeout = &__ide_dma_timeout;
	if (!hwif->ide_dma_lostirq)
		hwif->ide_dma_lostirq = &__ide_dma_lostirq;

	if (hwif->chipset != ide_trm290) {
		u8 dma_stat = hwif->INB(hwif->dma_status);
		printk(", BIOS settings: %s:%s, %s:%s",
		       hwif->drives[0].name, (dma_stat & 0x20) ? "DMA" : "pio",
		       hwif->drives[1].name, (dma_stat & 0x40) ? "DMA" : "pio");
	}
	printk("\n");

	if (!(hwif->dma_master))
		BUG();
}

EXPORT_SYMBOL_GPL(ide_setup_dma);
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */
