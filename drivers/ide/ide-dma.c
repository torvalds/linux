/*
 *  IDE DMA support (including IDE PCI BM-DMA).
 *
 *  Copyright (C) 1995-1998   Mark Lord
 *  Copyright (C) 1999-2000   Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2004, 2007  Bartlomiej Zolnierkiewicz
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  DMA is supported for all IDE devices (disk drives, cdroms, tapes, floppies).
 */

/*
 *  Special Thanks to Mark for his Six years of work.
 */

/*
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
 */

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
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>

static const struct drive_list_entry drive_whitelist [] = {

	{ "Micropolis 2112A"	,       NULL		},
	{ "CONNER CTMA 4000"	,       NULL		},
	{ "CONNER CTT8000-A"	,       NULL		},
	{ "ST34342A"		,	NULL		},
	{ NULL			,	NULL		}
};

static const struct drive_list_entry drive_blacklist [] = {

	{ "WDC AC11000H"	,	NULL 		},
	{ "WDC AC22100H"	,	NULL 		},
	{ "WDC AC32500H"	,	NULL 		},
	{ "WDC AC33100H"	,	NULL 		},
	{ "WDC AC31600H"	,	NULL 		},
	{ "WDC AC32100H"	,	"24.09P07"	},
	{ "WDC AC23200L"	,	"21.10N21"	},
	{ "Compaq CRD-8241B"	,	NULL 		},
	{ "CRD-8400B"		,	NULL 		},
	{ "CRD-8480B",			NULL 		},
	{ "CRD-8482B",			NULL 		},
	{ "CRD-84"		,	NULL 		},
	{ "SanDisk SDP3B"	,	NULL 		},
	{ "SanDisk SDP3B-64"	,	NULL 		},
	{ "SANYO CD-ROM CRD"	,	NULL 		},
	{ "HITACHI CDR-8"	,	NULL 		},
	{ "HITACHI CDR-8335"	,	NULL 		},
	{ "HITACHI CDR-8435"	,	NULL 		},
	{ "Toshiba CD-ROM XM-6202B"	,	NULL 		},
	{ "TOSHIBA CD-ROM XM-1702BC",	NULL 		},
	{ "CD-532E-A"		,	NULL 		},
	{ "E-IDE CD-ROM CR-840",	NULL 		},
	{ "CD-ROM Drive/F5A",	NULL 		},
	{ "WPI CDD-820",		NULL 		},
	{ "SAMSUNG CD-ROM SC-148C",	NULL 		},
	{ "SAMSUNG CD-ROM SC",	NULL 		},
	{ "ATAPI CD-ROM DRIVE 40X MAXIMUM",	NULL 		},
	{ "_NEC DV5800A",               NULL            },
	{ "SAMSUNG CD-ROM SN-124",	"N001" },
	{ "Seagate STT20000A",		NULL  },
	{ "CD-ROM CDR_U200",		"1.09" },
	{ NULL			,	NULL		}

};

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

	dma_stat = drive->hwif->dma_ops->dma_end(drive);
	stat = ide_read_status(drive);

	if (OK_STAT(stat,DRIVE_READY,drive->bad_wstat|DRQ_STAT)) {
		if (!dma_stat) {
			struct request *rq = HWGROUP(drive)->rq;

			task_end_request(drive, rq, stat);
			return ide_stopped;
		}
		printk(KERN_ERR "%s: dma_intr: bad DMA status (dma_stat=%x)\n", 
		       drive->name, dma_stat);
	}
	return ide_error(drive, "dma_intr", stat);
}

EXPORT_SYMBOL_GPL(ide_dma_intr);

static int ide_dma_good_drive(ide_drive_t *drive)
{
	return ide_in_drive_list(drive->id, drive_whitelist);
}

/**
 *	ide_build_sglist	-	map IDE scatter gather for DMA I/O
 *	@drive: the drive to build the DMA table for
 *	@rq: the request holding the sg list
 *
 *	Perform the DMA mapping magic necessary to access the source or
 *	target buffers of a request via DMA.  The lower layers of the
 *	kernel provide the necessary cache management so that we can
 *	operate in a portable fashion.
 */

int ide_build_sglist(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct scatterlist *sg = hwif->sg_table;

	ide_map_sg(drive, rq);

	if (rq_data_dir(rq) == READ)
		hwif->sg_dma_direction = DMA_FROM_DEVICE;
	else
		hwif->sg_dma_direction = DMA_TO_DEVICE;

	return dma_map_sg(hwif->dev, sg, hwif->sg_nents,
			  hwif->sg_dma_direction);
}

EXPORT_SYMBOL_GPL(ide_build_sglist);

#ifdef CONFIG_BLK_DEV_IDEDMA_SFF
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

		sg = sg_next(sg);
		i--;
	}

	if (count) {
		if (!is_trm290)
			*--table |= cpu_to_le32(0x80000000);
		return count;
	}

	printk(KERN_ERR "%s: empty DMA table?\n", drive->name);

use_pio_instead:
	ide_destroy_dmatable(drive);

	return 0; /* revert to PIO for this request */
}

EXPORT_SYMBOL_GPL(ide_build_dmatable);
#endif

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
	ide_hwif_t *hwif = drive->hwif;

	dma_unmap_sg(hwif->dev, hwif->sg_table, hwif->sg_nents,
		     hwif->sg_dma_direction);
}

EXPORT_SYMBOL_GPL(ide_destroy_dmatable);

#ifdef CONFIG_BLK_DEV_IDEDMA_SFF
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
	ide_hwif_t *hwif = drive->hwif;
	struct hd_driveid *id = drive->id;

	if (drive->media != ide_disk) {
		if (hwif->host_flags & IDE_HFLAG_NO_ATAPI_DMA)
			return 0;
	}

	/*
	 * Enable DMA on any drive that has
	 * UltraDMA (mode 0/1/2/3/4/5/6) enabled
	 */
	if ((id->field_valid & 4) && ((id->dma_ultra >> 8) & 0x7f))
		return 1;

	/*
	 * Enable DMA on any drive that has mode2 DMA
	 * (multi or single) enabled
	 */
	if (id->field_valid & 2)	/* regular DMA */
		if ((id->dma_mword & 0x404) == 0x404 ||
		    (id->dma_1word & 0x404) == 0x404)
			return 1;

	/* Consult the list of known "good" drives */
	if (ide_dma_good_drive(drive))
		return 1;

	return 0;
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
 *	ide_dma_host_set	-	Enable/disable DMA on a host
 *	@drive: drive to control
 *
 *	Enable/disable DMA on an IDE controller following generic
 *	bus-mastering IDE controller behaviour.
 */

void ide_dma_host_set(ide_drive_t *drive, int on)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

	if (on)
		dma_stat |= (1 << (5 + unit));
	else
		dma_stat &= ~(1 << (5 + unit));

	hwif->OUTB(dma_stat, hwif->dma_status);
}

EXPORT_SYMBOL_GPL(ide_dma_host_set);
#endif /* CONFIG_BLK_DEV_IDEDMA_SFF  */

/**
 *	ide_dma_off_quietly	-	Generic DMA kill
 *	@drive: drive to control
 *
 *	Turn off the current DMA on this IDE controller. 
 */

void ide_dma_off_quietly(ide_drive_t *drive)
{
	drive->using_dma = 0;
	ide_toggle_bounce(drive, 0);

	drive->hwif->dma_ops->dma_host_set(drive, 0);
}

EXPORT_SYMBOL(ide_dma_off_quietly);

/**
 *	ide_dma_off	-	disable DMA on a device
 *	@drive: drive to disable DMA on
 *
 *	Disable IDE DMA for a device on this IDE controller.
 *	Inform the user that DMA has been disabled.
 */

void ide_dma_off(ide_drive_t *drive)
{
	printk(KERN_INFO "%s: DMA disabled\n", drive->name);
	ide_dma_off_quietly(drive);
}

EXPORT_SYMBOL(ide_dma_off);

/**
 *	ide_dma_on		-	Enable DMA on a device
 *	@drive: drive to enable DMA on
 *
 *	Enable IDE DMA for a device on this IDE controller.
 */

void ide_dma_on(ide_drive_t *drive)
{
	drive->using_dma = 1;
	ide_toggle_bounce(drive, 1);

	drive->hwif->dma_ops->dma_host_set(drive, 1);
}

#ifdef CONFIG_BLK_DEV_IDEDMA_SFF
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
	if (hwif->mmio)
		writel(hwif->dmatable_dma,
		       (void __iomem *)(hwif->dma_base + ATA_DMA_TABLE_OFS));
	else
		outl(hwif->dmatable_dma, hwif->dma_base + ATA_DMA_TABLE_OFS);

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

void ide_dma_exec_cmd(ide_drive_t *drive, u8 command)
{
	/* issue cmd to drive */
	ide_execute_command(drive, command, &ide_dma_intr, 2*WAIT_CMD, dma_timer_expiry);
}
EXPORT_SYMBOL_GPL(ide_dma_exec_cmd);

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
int ide_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

	/* return 1 if INTR asserted */
	if ((dma_stat & 4) == 4)
		return 1;
	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "%s: (%s) called while not waiting\n",
			drive->name, __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(ide_dma_test_irq);
#else
static inline int config_drive_for_dma(ide_drive_t *drive) { return 0; }
#endif /* CONFIG_BLK_DEV_IDEDMA_SFF */

int __ide_dma_bad_drive (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	int blacklist = ide_in_drive_list(id, drive_blacklist);
	if (blacklist) {
		printk(KERN_WARNING "%s: Disabling (U)DMA for %s (blacklisted)\n",
				    drive->name, id->model);
		return blacklist;
	}
	return 0;
}

EXPORT_SYMBOL(__ide_dma_bad_drive);

static const u8 xfer_mode_bases[] = {
	XFER_UDMA_0,
	XFER_MW_DMA_0,
	XFER_SW_DMA_0,
};

static unsigned int ide_get_mode_mask(ide_drive_t *drive, u8 base, u8 req_mode)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_port_ops *port_ops = hwif->port_ops;
	unsigned int mask = 0;

	switch(base) {
	case XFER_UDMA_0:
		if ((id->field_valid & 4) == 0)
			break;

		if (port_ops && port_ops->udma_filter)
			mask = port_ops->udma_filter(drive);
		else
			mask = hwif->ultra_mask;
		mask &= id->dma_ultra;

		/*
		 * avoid false cable warning from eighty_ninty_three()
		 */
		if (req_mode > XFER_UDMA_2) {
			if ((mask & 0x78) && (eighty_ninty_three(drive) == 0))
				mask &= 0x07;
		}
		break;
	case XFER_MW_DMA_0:
		if ((id->field_valid & 2) == 0)
			break;
		if (port_ops && port_ops->mdma_filter)
			mask = port_ops->mdma_filter(drive);
		else
			mask = hwif->mwdma_mask;
		mask &= id->dma_mword;
		break;
	case XFER_SW_DMA_0:
		if (id->field_valid & 2) {
			mask = id->dma_1word & hwif->swdma_mask;
		} else if (id->tDMA) {
			/*
			 * ide_fix_driveid() doesn't convert ->tDMA to the
			 * CPU endianness so we need to do it here
			 */
			u8 mode = le16_to_cpu(id->tDMA);

			/*
			 * if the mode is valid convert it to the mask
			 * (the maximum allowed mode is XFER_SW_DMA_2)
			 */
			if (mode <= 2)
				mask = ((2 << mode) - 1) & hwif->swdma_mask;
		}
		break;
	default:
		BUG();
		break;
	}

	return mask;
}

/**
 *	ide_find_dma_mode	-	compute DMA speed
 *	@drive: IDE device
 *	@req_mode: requested mode
 *
 *	Checks the drive/host capabilities and finds the speed to use for
 *	the DMA transfer.  The speed is then limited by the requested mode.
 *
 *	Returns 0 if the drive/host combination is incapable of DMA transfers
 *	or if the requested mode is not a DMA mode.
 */

u8 ide_find_dma_mode(ide_drive_t *drive, u8 req_mode)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned int mask;
	int x, i;
	u8 mode = 0;

	if (drive->media != ide_disk) {
		if (hwif->host_flags & IDE_HFLAG_NO_ATAPI_DMA)
			return 0;
	}

	for (i = 0; i < ARRAY_SIZE(xfer_mode_bases); i++) {
		if (req_mode < xfer_mode_bases[i])
			continue;
		mask = ide_get_mode_mask(drive, xfer_mode_bases[i], req_mode);
		x = fls(mask) - 1;
		if (x >= 0) {
			mode = xfer_mode_bases[i] + x;
			break;
		}
	}

	if (hwif->chipset == ide_acorn && mode == 0) {
		/*
		 * is this correct?
		 */
		if (ide_dma_good_drive(drive) && drive->id->eide_dma_time < 150)
			mode = XFER_MW_DMA_1;
	}

	mode = min(mode, req_mode);

	printk(KERN_INFO "%s: %s mode selected\n", drive->name,
			  mode ? ide_xfer_verbose(mode) : "no DMA");

	return mode;
}

EXPORT_SYMBOL_GPL(ide_find_dma_mode);

static int ide_tune_dma(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 speed;

	if (noautodma || drive->nodma || (drive->id->capability & 1) == 0)
		return 0;

	/* consult the list of known "bad" drives */
	if (__ide_dma_bad_drive(drive))
		return 0;

	if (ide_id_dma_bug(drive))
		return 0;

	if (hwif->host_flags & IDE_HFLAG_TRUST_BIOS_FOR_DMA)
		return config_drive_for_dma(drive);

	speed = ide_max_dma_mode(drive);

	if (!speed)
		return 0;

	if (ide_set_dma_mode(drive, speed))
		return 0;

	return 1;
}

static int ide_dma_check(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	int vdma = (hwif->host_flags & IDE_HFLAG_VDMA)? 1 : 0;

	if (!vdma && ide_tune_dma(drive))
		return 0;

	/* TODO: always do PIO fallback */
	if (hwif->host_flags & IDE_HFLAG_TRUST_BIOS_FOR_DMA)
		return -1;

	ide_set_max_pio(drive);

	return vdma ? 0 : -1;
}

int ide_id_dma_bug(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	if (id->field_valid & 4) {
		if ((id->dma_ultra >> 8) && (id->dma_mword >> 8))
			goto err_out;
	} else if (id->field_valid & 2) {
		if ((id->dma_mword >> 8) && (id->dma_1word >> 8))
			goto err_out;
	}
	return 0;
err_out:
	printk(KERN_ERR "%s: bad DMA info in identify block\n", drive->name);
	return 1;
}

int ide_set_dma(ide_drive_t *drive)
{
	int rc;

	/*
	 * Force DMAing for the beginning of the check.
	 * Some chipsets appear to do interesting
	 * things, if not checked and cleared.
	 *   PARANOIA!!!
	 */
	ide_dma_off_quietly(drive);

	rc = ide_dma_check(drive);
	if (rc)
		return rc;

	ide_dma_on(drive);

	return 0;
}

void ide_check_dma_crc(ide_drive_t *drive)
{
	u8 mode;

	ide_dma_off_quietly(drive);
	drive->crc_count = 0;
	mode = drive->current_speed;
	/*
	 * Don't try non Ultra-DMA modes without iCRC's.  Force the
	 * device to PIO and make the user enable SWDMA/MWDMA modes.
	 */
	if (mode > XFER_UDMA_0 && mode <= XFER_UDMA_7)
		mode--;
	else
		mode = XFER_PIO_4;
	ide_set_xfer_rate(drive, mode);
	if (drive->current_speed >= XFER_SW_DMA_0)
		ide_dma_on(drive);
}

#ifdef CONFIG_BLK_DEV_IDEDMA_SFF
void ide_dma_lost_irq (ide_drive_t *drive)
{
	printk("%s: DMA interrupt recovery\n", drive->name);
}

EXPORT_SYMBOL(ide_dma_lost_irq);

void ide_dma_timeout (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	printk(KERN_ERR "%s: timeout waiting for DMA\n", drive->name);

	if (hwif->dma_ops->dma_test_irq(drive))
		return;

	hwif->dma_ops->dma_end(drive);
}

EXPORT_SYMBOL(ide_dma_timeout);

void ide_release_dma_engine(ide_hwif_t *hwif)
{
	if (hwif->dmatable_cpu) {
		struct pci_dev *pdev = to_pci_dev(hwif->dev);

		pci_free_consistent(pdev, PRD_ENTRIES * PRD_BYTES,
				    hwif->dmatable_cpu, hwif->dmatable_dma);
		hwif->dmatable_cpu = NULL;
	}
}

int ide_allocate_dma_engine(ide_hwif_t *hwif)
{
	struct pci_dev *pdev = to_pci_dev(hwif->dev);

	hwif->dmatable_cpu = pci_alloc_consistent(pdev,
						  PRD_ENTRIES * PRD_BYTES,
						  &hwif->dmatable_dma);

	if (hwif->dmatable_cpu)
		return 0;

	printk(KERN_ERR "%s: -- Error, unable to allocate DMA table.\n",
			hwif->name);

	return 1;
}
EXPORT_SYMBOL_GPL(ide_allocate_dma_engine);

static const struct ide_dma_ops sff_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= __ide_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_timeout		= ide_dma_timeout,
	.dma_lost_irq		= ide_dma_lost_irq,
};

void ide_setup_dma(ide_hwif_t *hwif, unsigned long base)
{
	hwif->dma_base = base;

	if (!hwif->dma_command)
		hwif->dma_command	= hwif->dma_base + 0;
	if (!hwif->dma_status)
		hwif->dma_status	= hwif->dma_base + 2;

	hwif->dma_ops = &sff_dma_ops;
}

EXPORT_SYMBOL_GPL(ide_setup_dma);
#endif /* CONFIG_BLK_DEV_IDEDMA_SFF */
