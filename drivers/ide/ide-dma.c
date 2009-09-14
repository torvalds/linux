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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ide.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

static const struct drive_list_entry drive_whitelist[] = {
	{ "Micropolis 2112A"	,       NULL		},
	{ "CONNER CTMA 4000"	,       NULL		},
	{ "CONNER CTT8000-A"	,       NULL		},
	{ "ST34342A"		,	NULL		},
	{ NULL			,	NULL		}
};

static const struct drive_list_entry drive_blacklist[] = {
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

ide_startstop_t ide_dma_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &hwif->cmd;
	u8 stat = 0, dma_stat = 0;

	drive->waiting_for_dma = 0;
	dma_stat = hwif->dma_ops->dma_end(drive);
	ide_dma_unmap_sg(drive, cmd);
	stat = hwif->tp_ops->read_status(hwif);

	if (OK_STAT(stat, DRIVE_READY, drive->bad_wstat | ATA_DRQ)) {
		if (!dma_stat) {
			if ((cmd->tf_flags & IDE_TFLAG_FS) == 0)
				ide_finish_cmd(drive, cmd, stat);
			else
				ide_complete_rq(drive, 0,
						blk_rq_sectors(cmd->rq) << 9);
			return ide_stopped;
		}
		printk(KERN_ERR "%s: %s: bad DMA status (0x%02x)\n",
			drive->name, __func__, dma_stat);
	}
	return ide_error(drive, "dma_intr", stat);
}

int ide_dma_good_drive(ide_drive_t *drive)
{
	return ide_in_drive_list(drive->id, drive_whitelist);
}

/**
 *	ide_dma_map_sg	-	map IDE scatter gather for DMA I/O
 *	@drive: the drive to map the DMA table for
 *	@cmd: command
 *
 *	Perform the DMA mapping magic necessary to access the source or
 *	target buffers of a request via DMA.  The lower layers of the
 *	kernel provide the necessary cache management so that we can
 *	operate in a portable fashion.
 */

static int ide_dma_map_sg(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;
	int i;

	if (cmd->tf_flags & IDE_TFLAG_WRITE)
		cmd->sg_dma_direction = DMA_TO_DEVICE;
	else
		cmd->sg_dma_direction = DMA_FROM_DEVICE;

	i = dma_map_sg(hwif->dev, sg, cmd->sg_nents, cmd->sg_dma_direction);
	if (i) {
		cmd->orig_sg_nents = cmd->sg_nents;
		cmd->sg_nents = i;
	}

	return i;
}

/**
 *	ide_dma_unmap_sg	-	clean up DMA mapping
 *	@drive: The drive to unmap
 *
 *	Teardown mappings after DMA has completed. This must be called
 *	after the completion of each use of ide_build_dmatable and before
 *	the next use of ide_build_dmatable. Failure to do so will cause
 *	an oops as only one mapping can be live for each target at a given
 *	time.
 */

void ide_dma_unmap_sg(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;

	dma_unmap_sg(hwif->dev, hwif->sg_table, cmd->orig_sg_nents,
		     cmd->sg_dma_direction);
}
EXPORT_SYMBOL_GPL(ide_dma_unmap_sg);

/**
 *	ide_dma_off_quietly	-	Generic DMA kill
 *	@drive: drive to control
 *
 *	Turn off the current DMA on this IDE controller.
 */

void ide_dma_off_quietly(ide_drive_t *drive)
{
	drive->dev_flags &= ~IDE_DFLAG_USING_DMA;
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
	drive->dev_flags |= IDE_DFLAG_USING_DMA;
	ide_toggle_bounce(drive, 1);

	drive->hwif->dma_ops->dma_host_set(drive, 1);
}

int __ide_dma_bad_drive(ide_drive_t *drive)
{
	u16 *id = drive->id;

	int blacklist = ide_in_drive_list(id, drive_blacklist);
	if (blacklist) {
		printk(KERN_WARNING "%s: Disabling (U)DMA for %s (blacklisted)\n",
				    drive->name, (char *)&id[ATA_ID_PROD]);
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
	u16 *id = drive->id;
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_port_ops *port_ops = hwif->port_ops;
	unsigned int mask = 0;

	switch (base) {
	case XFER_UDMA_0:
		if ((id[ATA_ID_FIELD_VALID] & 4) == 0)
			break;
		mask = id[ATA_ID_UDMA_MODES];
		if (port_ops && port_ops->udma_filter)
			mask &= port_ops->udma_filter(drive);
		else
			mask &= hwif->ultra_mask;

		/*
		 * avoid false cable warning from eighty_ninty_three()
		 */
		if (req_mode > XFER_UDMA_2) {
			if ((mask & 0x78) && (eighty_ninty_three(drive) == 0))
				mask &= 0x07;
		}
		break;
	case XFER_MW_DMA_0:
		mask = id[ATA_ID_MWDMA_MODES];

		/* Also look for the CF specific MWDMA modes... */
		if (ata_id_is_cfa(id) && (id[ATA_ID_CFA_MODES] & 0x38)) {
			u8 mode = ((id[ATA_ID_CFA_MODES] & 0x38) >> 3) - 1;

			mask |= ((2 << mode) - 1) << 3;
		}

		if (port_ops && port_ops->mdma_filter)
			mask &= port_ops->mdma_filter(drive);
		else
			mask &= hwif->mwdma_mask;
		break;
	case XFER_SW_DMA_0:
		mask = id[ATA_ID_SWDMA_MODES];
		if (!(mask & ATA_SWDMA2) && (id[ATA_ID_OLD_DMA_MODES] >> 8)) {
			u8 mode = id[ATA_ID_OLD_DMA_MODES] >> 8;

			/*
			 * if the mode is valid convert it to the mask
			 * (the maximum allowed mode is XFER_SW_DMA_2)
			 */
			if (mode <= 2)
				mask = (2 << mode) - 1;
		}
		mask &= hwif->swdma_mask;
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
		if (ide_dma_good_drive(drive) &&
		    drive->id[ATA_ID_EIDE_DMA_TIME] < 150)
			mode = XFER_MW_DMA_1;
	}

	mode = min(mode, req_mode);

	printk(KERN_INFO "%s: %s mode selected\n", drive->name,
			  mode ? ide_xfer_verbose(mode) : "no DMA");

	return mode;
}

static int ide_tune_dma(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 speed;

	if (ata_id_has_dma(drive->id) == 0 ||
	    (drive->dev_flags & IDE_DFLAG_NODMA))
		return 0;

	/* consult the list of known "bad" drives */
	if (__ide_dma_bad_drive(drive))
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

	if (ide_tune_dma(drive))
		return 0;

	/* TODO: always do PIO fallback */
	if (hwif->host_flags & IDE_HFLAG_TRUST_BIOS_FOR_DMA)
		return -1;

	ide_set_max_pio(drive);

	return -1;
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

void ide_dma_lost_irq(ide_drive_t *drive)
{
	printk(KERN_ERR "%s: DMA interrupt recovery\n", drive->name);
}
EXPORT_SYMBOL_GPL(ide_dma_lost_irq);

/*
 * un-busy the port etc, and clear any pending DMA status. we want to
 * retry the current request in pio mode instead of risking tossing it
 * all away
 */
ide_startstop_t ide_dma_timeout_retry(ide_drive_t *drive, int error)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_dma_ops *dma_ops = hwif->dma_ops;
	struct ide_cmd *cmd = &hwif->cmd;
	struct request *rq;
	ide_startstop_t ret = ide_stopped;

	/*
	 * end current dma transaction
	 */

	if (error < 0) {
		printk(KERN_WARNING "%s: DMA timeout error\n", drive->name);
		drive->waiting_for_dma = 0;
		(void)dma_ops->dma_end(drive);
		ide_dma_unmap_sg(drive, cmd);
		ret = ide_error(drive, "dma timeout error",
				hwif->tp_ops->read_status(hwif));
	} else {
		printk(KERN_WARNING "%s: DMA timeout retry\n", drive->name);
		if (dma_ops->dma_clear)
			dma_ops->dma_clear(drive);
		printk(KERN_ERR "%s: timeout waiting for DMA\n", drive->name);
		if (dma_ops->dma_test_irq(drive) == 0) {
			ide_dump_status(drive, "DMA timeout",
					hwif->tp_ops->read_status(hwif));
			drive->waiting_for_dma = 0;
			(void)dma_ops->dma_end(drive);
			ide_dma_unmap_sg(drive, cmd);
		}
	}

	/*
	 * disable dma for now, but remember that we did so because of
	 * a timeout -- we'll reenable after we finish this next request
	 * (or rather the first chunk of it) in pio.
	 */
	drive->dev_flags |= IDE_DFLAG_DMA_PIO_RETRY;
	drive->retry_pio++;
	ide_dma_off_quietly(drive);

	/*
	 * un-busy drive etc and make sure request is sane
	 */
	rq = hwif->rq;
	if (rq) {
		hwif->rq = NULL;
		rq->errors = 0;
	}
	return ret;
}

void ide_release_dma_engine(ide_hwif_t *hwif)
{
	if (hwif->dmatable_cpu) {
		int prd_size = hwif->prd_max_nents * hwif->prd_ent_size;

		dma_free_coherent(hwif->dev, prd_size,
				  hwif->dmatable_cpu, hwif->dmatable_dma);
		hwif->dmatable_cpu = NULL;
	}
}
EXPORT_SYMBOL_GPL(ide_release_dma_engine);

int ide_allocate_dma_engine(ide_hwif_t *hwif)
{
	int prd_size;

	if (hwif->prd_max_nents == 0)
		hwif->prd_max_nents = PRD_ENTRIES;
	if (hwif->prd_ent_size == 0)
		hwif->prd_ent_size = PRD_BYTES;

	prd_size = hwif->prd_max_nents * hwif->prd_ent_size;

	hwif->dmatable_cpu = dma_alloc_coherent(hwif->dev, prd_size,
						&hwif->dmatable_dma,
						GFP_ATOMIC);
	if (hwif->dmatable_cpu == NULL) {
		printk(KERN_ERR "%s: unable to allocate PRD table\n",
			hwif->name);
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ide_allocate_dma_engine);

int ide_dma_prepare(ide_drive_t *drive, struct ide_cmd *cmd)
{
	const struct ide_dma_ops *dma_ops = drive->hwif->dma_ops;

	if ((drive->dev_flags & IDE_DFLAG_USING_DMA) == 0 ||
	    (dma_ops->dma_check && dma_ops->dma_check(drive, cmd)))
		goto out;
	ide_map_sg(drive, cmd);
	if (ide_dma_map_sg(drive, cmd) == 0)
		goto out_map;
	if (dma_ops->dma_setup(drive, cmd))
		goto out_dma_unmap;
	drive->waiting_for_dma = 1;
	return 0;
out_dma_unmap:
	ide_dma_unmap_sg(drive, cmd);
out_map:
	ide_map_sg(drive, cmd);
out:
	return 1;
}
