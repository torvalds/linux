/*
 * ATAPI support.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>

static u8 ide_wait_ireason(ide_drive_t *drive, u8 ireason)
{
	ide_hwif_t *hwif = drive->hwif;
	int retries = 100;

	while (retries-- && ((ireason & CD) == 0 || (ireason & IO))) {
		printk(KERN_ERR "%s: (IO,CoD != (0,1) while issuing "
				"a packet command, retrying\n", drive->name);
		udelay(100);
		ireason = hwif->INB(hwif->io_ports.nsect_addr);
		if (retries == 0) {
			printk(KERN_ERR "%s: (IO,CoD != (0,1) while issuing "
					"a packet command, ignoring\n",
					drive->name);
			ireason |= CD;
			ireason &= ~IO;
		}
	}

	return ireason;
}

ide_startstop_t ide_transfer_pc(ide_drive_t *drive, struct ide_atapi_pc *pc,
				ide_handler_t *handler, unsigned int timeout,
				ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;
	ide_startstop_t startstop;
	u8 ireason;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk(KERN_ERR "%s: Strange, packet command initiated yet "
				"DRQ isn't asserted\n", drive->name);
		return startstop;
	}

	ireason = hwif->INB(hwif->io_ports.nsect_addr);
	if (drive->media == ide_tape && !drive->scsi)
		ireason = ide_wait_ireason(drive, ireason);

	if ((ireason & CD) == 0 || (ireason & IO)) {
		printk(KERN_ERR "%s: (IO,CoD) != (0,1) while issuing "
				"a packet command\n", drive->name);
		return ide_do_reset(drive);
	}

	/* Set the interrupt routine */
	ide_set_handler(drive, handler, timeout, expiry);

	/* Begin DMA, if necessary */
	if (pc->flags & PC_FLAG_DMA_OK) {
		pc->flags |= PC_FLAG_DMA_IN_PROGRESS;
		hwif->dma_ops->dma_start(drive);
	}

	/* Send the actual packet */
	if ((pc->flags & PC_FLAG_ZIP_DRIVE) == 0)
		hwif->output_data(drive, NULL, pc->c, 12);

	return ide_started;
}
EXPORT_SYMBOL_GPL(ide_transfer_pc);

ide_startstop_t ide_issue_pc(ide_drive_t *drive, struct ide_atapi_pc *pc,
			     ide_handler_t *handler, unsigned int timeout,
			     ide_expiry_t *expiry)
{
	ide_hwif_t *hwif = drive->hwif;
	u16 bcount;
	u8 dma = 0;

	/* We haven't transferred any data yet */
	pc->xferred = 0;
	pc->cur_pos = pc->buf;

	/* Request to transfer the entire buffer at once */
	if (drive->media == ide_tape && !drive->scsi)
		bcount = pc->req_xfer;
	else
		bcount = min(pc->req_xfer, 63 * 1024);

	if (pc->flags & PC_FLAG_DMA_ERROR) {
		pc->flags &= ~PC_FLAG_DMA_ERROR;
		ide_dma_off(drive);
	}

	if ((pc->flags & PC_FLAG_DMA_OK) && drive->using_dma) {
		if (drive->scsi)
			hwif->sg_mapped = 1;
		dma = !hwif->dma_ops->dma_setup(drive);
		if (drive->scsi)
			hwif->sg_mapped = 0;
	}

	if (!dma)
		pc->flags &= ~PC_FLAG_DMA_OK;

	ide_pktcmd_tf_load(drive, drive->scsi ? 0 : IDE_TFLAG_OUT_DEVICE,
			   bcount, dma);

	/* Issue the packet command */
	if (pc->flags & PC_FLAG_DRQ_INTERRUPT) {
		ide_execute_command(drive, WIN_PACKETCMD, handler,
				    timeout, NULL);
		return ide_started;
	} else {
		ide_execute_pkt_cmd(drive);
		return (*handler)(drive);
	}
}
EXPORT_SYMBOL_GPL(ide_issue_pc);
