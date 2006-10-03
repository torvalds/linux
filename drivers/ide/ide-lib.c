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
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*
 *	IDE library routines. These are plug in code that most 
 *	drivers can use but occasionally may be weird enough
 *	to want to do their own thing with
 *
 *	Add common non I/O op stuff here. Make sure it has proper
 *	kernel-doc function headers or your patch will be rejected
 */
 

/**
 *	ide_xfer_verbose	-	return IDE mode names
 *	@xfer_rate: rate to name
 *
 *	Returns a constant string giving the name of the mode
 *	requested.
 */

char *ide_xfer_verbose (u8 xfer_rate)
{
        switch(xfer_rate) {
                case XFER_UDMA_7:	return("UDMA 7");
                case XFER_UDMA_6:	return("UDMA 6");
                case XFER_UDMA_5:	return("UDMA 5");
                case XFER_UDMA_4:	return("UDMA 4");
                case XFER_UDMA_3:	return("UDMA 3");
                case XFER_UDMA_2:	return("UDMA 2");
                case XFER_UDMA_1:	return("UDMA 1");
                case XFER_UDMA_0:	return("UDMA 0");
                case XFER_MW_DMA_2:	return("MW DMA 2");
                case XFER_MW_DMA_1:	return("MW DMA 1");
                case XFER_MW_DMA_0:	return("MW DMA 0");
                case XFER_SW_DMA_2:	return("SW DMA 2");
                case XFER_SW_DMA_1:	return("SW DMA 1");
                case XFER_SW_DMA_0:	return("SW DMA 0");
                case XFER_PIO_4:	return("PIO 4");
                case XFER_PIO_3:	return("PIO 3");
                case XFER_PIO_2:	return("PIO 2");
                case XFER_PIO_1:	return("PIO 1");
                case XFER_PIO_0:	return("PIO 0");
                case XFER_PIO_SLOW:	return("PIO SLOW");
                default:		return("XFER ERROR");
        }
}

EXPORT_SYMBOL(ide_xfer_verbose);

/**
 *	ide_dma_speed	-	compute DMA speed
 *	@drive: drive
 *	@mode:	modes available
 *
 *	Checks the drive capabilities and returns the speed to use
 *	for the DMA transfer.  Returns 0 if the drive is incapable
 *	of DMA transfers.
 */
 
u8 ide_dma_speed(ide_drive_t *drive, u8 mode)
{
	struct hd_driveid *id   = drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	u8 ultra_mask, mwdma_mask, swdma_mask;
	u8 speed = 0;

	if (drive->media != ide_disk && hwif->atapi_dma == 0)
		return 0;

	/* Capable of UltraDMA modes? */
	ultra_mask = id->dma_ultra & hwif->ultra_mask;

	if (!(id->field_valid & 4))
		mode = 0;	/* fallback to MW/SW DMA if no UltraDMA */

	switch (mode) {
	case 4:
		if (ultra_mask & 0x40) {
			speed = XFER_UDMA_6;
			break;
		}
	case 3:
		if (ultra_mask & 0x20) {
			speed = XFER_UDMA_5;
			break;
		}
	case 2:
		if (ultra_mask & 0x10) {
			speed = XFER_UDMA_4;
			break;
		}
		if (ultra_mask & 0x08) {
			speed = XFER_UDMA_3;
			break;
		}
	case 1:
		if (ultra_mask & 0x04) {
			speed = XFER_UDMA_2;
			break;
		}
		if (ultra_mask & 0x02) {
			speed = XFER_UDMA_1;
			break;
		}
		if (ultra_mask & 0x01) {
			speed = XFER_UDMA_0;
			break;
		}
	case 0:
		mwdma_mask = id->dma_mword & hwif->mwdma_mask;

		if (mwdma_mask & 0x04) {
			speed = XFER_MW_DMA_2;
			break;
		}
		if (mwdma_mask & 0x02) {
			speed = XFER_MW_DMA_1;
			break;
		}
		if (mwdma_mask & 0x01) {
			speed = XFER_MW_DMA_0;
			break;
		}

		swdma_mask = id->dma_1word & hwif->swdma_mask;

		if (swdma_mask & 0x04) {
			speed = XFER_SW_DMA_2;
			break;
		}
		if (swdma_mask & 0x02) {
			speed = XFER_SW_DMA_1;
			break;
		}
		if (swdma_mask & 0x01) {
			speed = XFER_SW_DMA_0;
			break;
		}
	}

	return speed;
}
EXPORT_SYMBOL(ide_dma_speed);


/**
 *	ide_rate_filter		-	return best speed for mode
 *	@mode: modes available
 *	@speed: desired speed
 *
 *	Given the available DMA/UDMA mode this function returns
 *	the best available speed at or below the speed requested.
 */

u8 ide_rate_filter (u8 mode, u8 speed) 
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	static u8 speed_max[] = {
		XFER_MW_DMA_2, XFER_UDMA_2, XFER_UDMA_4,
		XFER_UDMA_5, XFER_UDMA_6
	};

//	printk("%s: mode 0x%02x, speed 0x%02x\n", __FUNCTION__, mode, speed);

	/* So that we remember to update this if new modes appear */
	BUG_ON(mode > 4);
	return min(speed, speed_max[mode]);
#else /* !CONFIG_BLK_DEV_IDEDMA */
	return min(speed, (u8)XFER_PIO_4);
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

EXPORT_SYMBOL(ide_rate_filter);

int ide_dma_enable (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	return ((int)	((((id->dma_ultra >> 8) & hwif->ultra_mask) ||
			  ((id->dma_mword >> 8) & hwif->mwdma_mask) ||
			  ((id->dma_1word >> 8) & hwif->swdma_mask)) ? 1 : 0));
}

EXPORT_SYMBOL(ide_dma_enable);

/*
 * Standard (generic) timings for PIO modes, from ATA2 specification.
 * These timings are for access to the IDE data port register *only*.
 * Some drives may specify a mode, while also specifying a different
 * value for cycle_time (from drive identification data).
 */
const ide_pio_timings_t ide_pio_timings[6] = {
	{ 70,	165,	600 },	/* PIO Mode 0 */
	{ 50,	125,	383 },	/* PIO Mode 1 */
	{ 30,	100,	240 },	/* PIO Mode 2 */
	{ 30,	80,	180 },	/* PIO Mode 3 with IORDY */
	{ 25,	70,	120 },	/* PIO Mode 4 with IORDY */
	{ 20,	50,	100 }	/* PIO Mode 5 with IORDY (nonstandard) */
};

EXPORT_SYMBOL_GPL(ide_pio_timings);

/*
 * Shared data/functions for determining best PIO mode for an IDE drive.
 * Most of this stuff originally lived in cmd640.c, and changes to the
 * ide_pio_blacklist[] table should be made with EXTREME CAUTION to avoid
 * breaking the fragile cmd640.c support.
 */

/*
 * Black list. Some drives incorrectly report their maximal PIO mode,
 * at least in respect to CMD640. Here we keep info on some known drives.
 */
static struct ide_pio_info {
	const char	*name;
	int		pio;
} ide_pio_blacklist [] = {
/*	{ "Conner Peripherals 1275MB - CFS1275A", 4 }, */
	{ "Conner Peripherals 540MB - CFS540A", 3 },

	{ "WDC AC2700",  3 },
	{ "WDC AC2540",  3 },
	{ "WDC AC2420",  3 },
	{ "WDC AC2340",  3 },
	{ "WDC AC2250",  0 },
	{ "WDC AC2200",  0 },
	{ "WDC AC21200", 4 },
	{ "WDC AC2120",  0 },
	{ "WDC AC2850",  3 },
	{ "WDC AC1270",  3 },
	{ "WDC AC1170",  1 },
	{ "WDC AC1210",  1 },
	{ "WDC AC280",   0 },
/*	{ "WDC AC21000", 4 }, */
	{ "WDC AC31000", 3 },
	{ "WDC AC31200", 3 },
/*	{ "WDC AC31600", 4 }, */

	{ "Maxtor 7131 AT", 1 },
	{ "Maxtor 7171 AT", 1 },
	{ "Maxtor 7213 AT", 1 },
	{ "Maxtor 7245 AT", 1 },
	{ "Maxtor 7345 AT", 1 },
	{ "Maxtor 7546 AT", 3 },
	{ "Maxtor 7540 AV", 3 },

	{ "SAMSUNG SHD-3121A", 1 },
	{ "SAMSUNG SHD-3122A", 1 },
	{ "SAMSUNG SHD-3172A", 1 },

/*	{ "ST51080A", 4 },
 *	{ "ST51270A", 4 },
 *	{ "ST31220A", 4 },
 *	{ "ST31640A", 4 },
 *	{ "ST32140A", 4 },
 *	{ "ST3780A",  4 },
 */
	{ "ST5660A",  3 },
	{ "ST3660A",  3 },
	{ "ST3630A",  3 },
	{ "ST3655A",  3 },
	{ "ST3391A",  3 },
	{ "ST3390A",  1 },
	{ "ST3600A",  1 },
	{ "ST3290A",  0 },
	{ "ST3144A",  0 },
	{ "ST3491A",  1 },	/* reports 3, should be 1 or 2 (depending on */	
				/* drive) according to Seagates FIND-ATA program */

	{ "QUANTUM ELS127A", 0 },
	{ "QUANTUM ELS170A", 0 },
	{ "QUANTUM LPS240A", 0 },
	{ "QUANTUM LPS210A", 3 },
	{ "QUANTUM LPS270A", 3 },
	{ "QUANTUM LPS365A", 3 },
	{ "QUANTUM LPS540A", 3 },
	{ "QUANTUM LIGHTNING 540A", 3 },
	{ "QUANTUM LIGHTNING 730A", 3 },

        { "QUANTUM FIREBALL_540", 3 }, /* Older Quantum Fireballs don't work */
        { "QUANTUM FIREBALL_640", 3 }, 
        { "QUANTUM FIREBALL_1080", 3 },
        { "QUANTUM FIREBALL_1280", 3 },
	{ NULL,	0 }
};

/**
 *	ide_scan_pio_blacklist 	-	check for a blacklisted drive
 *	@model: Drive model string
 *
 *	This routine searches the ide_pio_blacklist for an entry
 *	matching the start/whole of the supplied model name.
 *
 *	Returns -1 if no match found.
 *	Otherwise returns the recommended PIO mode from ide_pio_blacklist[].
 */

static int ide_scan_pio_blacklist (char *model)
{
	struct ide_pio_info *p;

	for (p = ide_pio_blacklist; p->name != NULL; p++) {
		if (strncmp(p->name, model, strlen(p->name)) == 0)
			return p->pio;
	}
	return -1;
}

/**
 *	ide_get_best_pio_mode	-	get PIO mode from drive
 *	@driver: drive to consider
 *	@mode_wanted: preferred mode
 *	@max_mode: highest allowed
 *	@d: pio data
 *
 *	This routine returns the recommended PIO settings for a given drive,
 *	based on the drive->id information and the ide_pio_blacklist[].
 *	This is used by most chipset support modules when "auto-tuning".
 *
 *	Drive PIO mode auto selection
 */

u8 ide_get_best_pio_mode (ide_drive_t *drive, u8 mode_wanted, u8 max_mode, ide_pio_data_t *d)
{
	int pio_mode;
	int cycle_time = 0;
	int use_iordy = 0;
	struct hd_driveid* id = drive->id;
	int overridden  = 0;
	int blacklisted = 0;

	if (mode_wanted != 255) {
		pio_mode = mode_wanted;
	} else if (!drive->id) {
		pio_mode = 0;
	} else if ((pio_mode = ide_scan_pio_blacklist(id->model)) != -1) {
		overridden = 1;
		blacklisted = 1;
		use_iordy = (pio_mode > 2);
	} else {
		pio_mode = id->tPIO;
		if (pio_mode > 2) {	/* 2 is maximum allowed tPIO value */
			pio_mode = 2;
			overridden = 1;
		}
		if (id->field_valid & 2) {	  /* drive implements ATA2? */
			if (id->capability & 8) { /* drive supports use_iordy? */
				use_iordy = 1;
				cycle_time = id->eide_pio_iordy;
				if (id->eide_pio_modes & 7) {
					overridden = 0;
					if (id->eide_pio_modes & 4)
						pio_mode = 5;
					else if (id->eide_pio_modes & 2)
						pio_mode = 4;
					else
						pio_mode = 3;
				}
			} else {
				cycle_time = id->eide_pio;
			}
		}

#if 0
		if (drive->id->major_rev_num & 0x0004) printk("ATA-2 ");
#endif

		/*
		 * Conservative "downgrade" for all pre-ATA2 drives
		 */
		if (pio_mode && pio_mode < 4) {
			pio_mode--;
			overridden = 1;
#if 0
			use_iordy = (pio_mode > 2);
#endif
			if (cycle_time && cycle_time < ide_pio_timings[pio_mode].cycle_time)
				cycle_time = 0; /* use standard timing */
		}
	}
	if (pio_mode > max_mode) {
		pio_mode = max_mode;
		cycle_time = 0;
	}
	if (d) {
		d->pio_mode = pio_mode;
		d->cycle_time = cycle_time ? cycle_time : ide_pio_timings[pio_mode].cycle_time;
		d->use_iordy = use_iordy;
		d->overridden = overridden;
		d->blacklisted = blacklisted;
	}
	return pio_mode;
}

EXPORT_SYMBOL_GPL(ide_get_best_pio_mode);

/**
 *	ide_toggle_bounce	-	handle bounce buffering
 *	@drive: drive to update
 *	@on: on/off boolean
 *
 *	Enable or disable bounce buffering for the device. Drives move
 *	between PIO and DMA and that changes the rules we need.
 */
 
void ide_toggle_bounce(ide_drive_t *drive, int on)
{
	u64 addr = BLK_BOUNCE_HIGH;	/* dma64_addr_t */

	if (!PCI_DMA_BUS_IS_PHYS) {
		addr = BLK_BOUNCE_ANY;
	} else if (on && drive->media == ide_disk) {
		if (HWIF(drive)->pci_dev)
			addr = HWIF(drive)->pci_dev->dma_mask;
	}

	if (drive->queue)
		blk_queue_bounce_limit(drive->queue, addr);
}

/**
 *	ide_set_xfer_rate	-	set transfer rate
 *	@drive: drive to set
 *	@speed: speed to attempt to set
 *	
 *	General helper for setting the speed of an IDE device. This
 *	function knows about user enforced limits from the configuration
 *	which speedproc() does not.  High level drivers should never
 *	invoke speedproc() directly.
 */
 
int ide_set_xfer_rate(ide_drive_t *drive, u8 rate)
{
#ifndef CONFIG_BLK_DEV_IDEDMA
	rate = min(rate, (u8) XFER_PIO_4);
#endif
	if(HWIF(drive)->speedproc)
		return HWIF(drive)->speedproc(drive, rate);
	else
		return -1;
}

EXPORT_SYMBOL_GPL(ide_set_xfer_rate);

static void ide_dump_opcode(ide_drive_t *drive)
{
	struct request *rq;
	u8 opcode = 0;
	int found = 0;

	spin_lock(&ide_lock);
	rq = NULL;
	if (HWGROUP(drive))
		rq = HWGROUP(drive)->rq;
	spin_unlock(&ide_lock);
	if (!rq)
		return;
	if (rq->cmd_type == REQ_TYPE_ATA_CMD ||
	    rq->cmd_type == REQ_TYPE_ATA_TASK) {
		char *args = rq->buffer;
		if (args) {
			opcode = args[0];
			found = 1;
		}
	} else if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE) {
		ide_task_t *args = rq->special;
		if (args) {
			task_struct_t *tf = (task_struct_t *) args->tfRegister;
			opcode = tf->command;
			found = 1;
		}
	}

	printk("ide: failed opcode was: ");
	if (!found)
		printk("unknown\n");
	else
		printk("0x%02x\n", opcode);
}

static u8 ide_dump_ata_status(ide_drive_t *drive, const char *msg, u8 stat)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	u8 err = 0;

	local_irq_save(flags);
	printk("%s: %s: status=0x%02x { ", drive->name, msg, stat);
	if (stat & BUSY_STAT)
		printk("Busy ");
	else {
		if (stat & READY_STAT)	printk("DriveReady ");
		if (stat & WRERR_STAT)	printk("DeviceFault ");
		if (stat & SEEK_STAT)	printk("SeekComplete ");
		if (stat & DRQ_STAT)	printk("DataRequest ");
		if (stat & ECC_STAT)	printk("CorrectedError ");
		if (stat & INDEX_STAT)	printk("Index ");
		if (stat & ERR_STAT)	printk("Error ");
	}
	printk("}\n");
	if ((stat & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = hwif->INB(IDE_ERROR_REG);
		printk("%s: %s: error=0x%02x { ", drive->name, msg, err);
		if (err & ABRT_ERR)	printk("DriveStatusError ");
		if (err & ICRC_ERR)
			printk((err & ABRT_ERR) ? "BadCRC " : "BadSector ");
		if (err & ECC_ERR)	printk("UncorrectableError ");
		if (err & ID_ERR)	printk("SectorIdNotFound ");
		if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
		if (err & MARK_ERR)	printk("AddrMarkNotFound ");
		printk("}");
		if ((err & (BBD_ERR | ABRT_ERR)) == BBD_ERR ||
		    (err & (ECC_ERR|ID_ERR|MARK_ERR))) {
			if (drive->addressing == 1) {
				__u64 sectors = 0;
				u32 low = 0, high = 0;
				low = ide_read_24(drive);
				hwif->OUTB(drive->ctl|0x80, IDE_CONTROL_REG);
				high = ide_read_24(drive);
				sectors = ((__u64)high << 24) | low;
				printk(", LBAsect=%llu, high=%d, low=%d",
				       (unsigned long long) sectors,
				       high, low);
			} else {
				u8 cur = hwif->INB(IDE_SELECT_REG);
				if (cur & 0x40) {	/* using LBA? */
					printk(", LBAsect=%ld", (unsigned long)
					 ((cur&0xf)<<24)
					 |(hwif->INB(IDE_HCYL_REG)<<16)
					 |(hwif->INB(IDE_LCYL_REG)<<8)
					 | hwif->INB(IDE_SECTOR_REG));
				} else {
					printk(", CHS=%d/%d/%d",
					 (hwif->INB(IDE_HCYL_REG)<<8) +
					  hwif->INB(IDE_LCYL_REG),
					  cur & 0xf,
					  hwif->INB(IDE_SECTOR_REG));
				}
			}
			if (HWGROUP(drive) && HWGROUP(drive)->rq)
				printk(", sector=%llu",
					(unsigned long long)HWGROUP(drive)->rq->sector);
		}
		printk("\n");
	}
	ide_dump_opcode(drive);
	local_irq_restore(flags);
	return err;
}

/**
 *	ide_dump_atapi_status       -       print human readable atapi status
 *	@drive: drive that status applies to
 *	@msg: text message to print
 *	@stat: status byte to decode
 *
 *	Error reporting, in human readable form (luxurious, but a memory hog).
 */

static u8 ide_dump_atapi_status(ide_drive_t *drive, const char *msg, u8 stat)
{
	unsigned long flags;

	atapi_status_t status;
	atapi_error_t error;

	status.all = stat;
	error.all = 0;
	local_irq_save(flags);
	printk("%s: %s: status=0x%02x { ", drive->name, msg, stat);
	if (status.b.bsy)
		printk("Busy ");
	else {
		if (status.b.drdy)	printk("DriveReady ");
		if (status.b.df)	printk("DeviceFault ");
		if (status.b.dsc)	printk("SeekComplete ");
		if (status.b.drq)	printk("DataRequest ");
		if (status.b.corr)	printk("CorrectedError ");
		if (status.b.idx)	printk("Index ");
		if (status.b.check)	printk("Error ");
	}
	printk("}\n");
	if (status.b.check && !status.b.bsy) {
		error.all = HWIF(drive)->INB(IDE_ERROR_REG);
		printk("%s: %s: error=0x%02x { ", drive->name, msg, error.all);
		if (error.b.ili)	printk("IllegalLengthIndication ");
		if (error.b.eom)	printk("EndOfMedia ");
		if (error.b.abrt)	printk("AbortedCommand ");
		if (error.b.mcr)	printk("MediaChangeRequested ");
		if (error.b.sense_key)	printk("LastFailedSense=0x%02x ",
						error.b.sense_key);
		printk("}\n");
	}
	ide_dump_opcode(drive);
	local_irq_restore(flags);
	return error.all;
}

/**
 *	ide_dump_status		-	translate ATA/ATAPI error
 *	@drive: drive the error occured on
 *	@msg: information string
 *	@stat: status byte
 *
 *	Error reporting, in human readable form (luxurious, but a memory hog).
 *	Combines the drive name, message and status byte to provide a
 *	user understandable explanation of the device error.
 */

u8 ide_dump_status(ide_drive_t *drive, const char *msg, u8 stat)
{
	if (drive->media == ide_disk)
		return ide_dump_ata_status(drive, msg, stat);
	return ide_dump_atapi_status(drive, msg, stat);
}

EXPORT_SYMBOL(ide_dump_status);
