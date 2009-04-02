#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/ide.h>
#include <linux/bitops.h>

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
		struct device *dev = drive->hwif->dev;

		if (dev && dev->dma_mask)
			addr = *dev->dma_mask;
	}

	if (drive->queue)
		blk_queue_bounce_limit(drive->queue, addr);
}

static void ide_dump_opcode(ide_drive_t *drive)
{
	struct request *rq = drive->hwif->rq;
	struct ide_cmd *cmd = NULL;

	if (!rq)
		return;

	if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE)
		cmd = rq->special;

	printk(KERN_ERR "ide: failed opcode was: ");
	if (cmd == NULL)
		printk(KERN_CONT "unknown\n");
	else
		printk(KERN_CONT "0x%02x\n", cmd->tf.command);
}

u64 ide_get_lba_addr(struct ide_taskfile *tf, int lba48)
{
	u32 high, low;

	if (lba48)
		high = (tf->hob_lbah << 16) | (tf->hob_lbam << 8) |
			tf->hob_lbal;
	else
		high = tf->device & 0xf;
	low  = (tf->lbah << 16) | (tf->lbam << 8) | tf->lbal;

	return ((u64)high << 24) | low;
}
EXPORT_SYMBOL_GPL(ide_get_lba_addr);

static void ide_dump_sector(ide_drive_t *drive)
{
	struct ide_cmd cmd;
	struct ide_taskfile *tf = &cmd.tf;
	u8 lba48 = !!(drive->dev_flags & IDE_DFLAG_LBA48);

	memset(&cmd, 0, sizeof(cmd));
	if (lba48)
		cmd.tf_flags = IDE_TFLAG_IN_LBA | IDE_TFLAG_IN_HOB_LBA |
				IDE_TFLAG_LBA48;
	else
		cmd.tf_flags = IDE_TFLAG_IN_LBA | IDE_TFLAG_IN_DEVICE;

	drive->hwif->tp_ops->tf_read(drive, &cmd);

	if (lba48 || (tf->device & ATA_LBA))
		printk(KERN_CONT ", LBAsect=%llu",
			(unsigned long long)ide_get_lba_addr(tf, lba48));
	else
		printk(KERN_CONT ", CHS=%d/%d/%d", (tf->lbah << 8) + tf->lbam,
			tf->device & 0xf, tf->lbal);
}

static void ide_dump_ata_error(ide_drive_t *drive, u8 err)
{
	printk(KERN_ERR "{ ");
	if (err & ATA_ABORTED)
		printk(KERN_CONT "DriveStatusError ");
	if (err & ATA_ICRC)
		printk(KERN_CONT "%s",
			(err & ATA_ABORTED) ? "BadCRC " : "BadSector ");
	if (err & ATA_UNC)
		printk(KERN_CONT "UncorrectableError ");
	if (err & ATA_IDNF)
		printk(KERN_CONT "SectorIdNotFound ");
	if (err & ATA_TRK0NF)
		printk(KERN_CONT "TrackZeroNotFound ");
	if (err & ATA_AMNF)
		printk(KERN_CONT "AddrMarkNotFound ");
	printk(KERN_CONT "}");
	if ((err & (ATA_BBK | ATA_ABORTED)) == ATA_BBK ||
	    (err & (ATA_UNC | ATA_IDNF | ATA_AMNF))) {
		struct request *rq = drive->hwif->rq;

		ide_dump_sector(drive);

		if (rq)
			printk(KERN_CONT ", sector=%llu",
			       (unsigned long long)rq->sector);
	}
	printk(KERN_CONT "\n");
}

static void ide_dump_atapi_error(ide_drive_t *drive, u8 err)
{
	printk(KERN_ERR "{ ");
	if (err & ATAPI_ILI)
		printk(KERN_CONT "IllegalLengthIndication ");
	if (err & ATAPI_EOM)
		printk(KERN_CONT "EndOfMedia ");
	if (err & ATA_ABORTED)
		printk(KERN_CONT "AbortedCommand ");
	if (err & ATA_MCR)
		printk(KERN_CONT "MediaChangeRequested ");
	if (err & ATAPI_LFS)
		printk(KERN_CONT "LastFailedSense=0x%02x ",
			(err & ATAPI_LFS) >> 4);
	printk(KERN_CONT "}\n");
}

/**
 *	ide_dump_status		-	translate ATA/ATAPI error
 *	@drive: drive that status applies to
 *	@msg: text message to print
 *	@stat: status byte to decode
 *
 *	Error reporting, in human readable form (luxurious, but a memory hog).
 *	Combines the drive name, message and status byte to provide a
 *	user understandable explanation of the device error.
 */

u8 ide_dump_status(ide_drive_t *drive, const char *msg, u8 stat)
{
	u8 err = 0;

	printk(KERN_ERR "%s: %s: status=0x%02x { ", drive->name, msg, stat);
	if (stat & ATA_BUSY)
		printk(KERN_CONT "Busy ");
	else {
		if (stat & ATA_DRDY)
			printk(KERN_CONT "DriveReady ");
		if (stat & ATA_DF)
			printk(KERN_CONT "DeviceFault ");
		if (stat & ATA_DSC)
			printk(KERN_CONT "SeekComplete ");
		if (stat & ATA_DRQ)
			printk(KERN_CONT "DataRequest ");
		if (stat & ATA_CORR)
			printk(KERN_CONT "CorrectedError ");
		if (stat & ATA_IDX)
			printk(KERN_CONT "Index ");
		if (stat & ATA_ERR)
			printk(KERN_CONT "Error ");
	}
	printk(KERN_CONT "}\n");
	if ((stat & (ATA_BUSY | ATA_ERR)) == ATA_ERR) {
		err = ide_read_error(drive);
		printk(KERN_ERR "%s: %s: error=0x%02x ", drive->name, msg, err);
		if (drive->media == ide_disk)
			ide_dump_ata_error(drive, err);
		else
			ide_dump_atapi_error(drive, err);
	}
	ide_dump_opcode(drive);
	return err;
}
EXPORT_SYMBOL(ide_dump_status);
