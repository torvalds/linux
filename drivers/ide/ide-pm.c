#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/ide.h>

int generic_ide_suspend(struct device *dev, pm_message_t mesg)
{
	ide_drive_t *drive = dev_get_drvdata(dev);
	ide_drive_t *pair = ide_get_pair_dev(drive);
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq;
	struct request_pm_state rqpm;
	int ret;

	if (ide_port_acpi(hwif)) {
		/* call ACPI _GTM only once */
		if ((drive->dn & 1) == 0 || pair == NULL)
			ide_acpi_get_timing(hwif);
	}

	memset(&rqpm, 0, sizeof(rqpm));
	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_PM_SUSPEND;
	rq->special = &rqpm;
	rqpm.pm_step = IDE_PM_START_SUSPEND;
	if (mesg.event == PM_EVENT_PRETHAW)
		mesg.event = PM_EVENT_FREEZE;
	rqpm.pm_state = mesg.event;

	ret = blk_execute_rq(drive->queue, NULL, rq, 0);
	blk_put_request(rq);

	if (ret == 0 && ide_port_acpi(hwif)) {
		/* call ACPI _PS3 only after both devices are suspended */
		if ((drive->dn & 1) || pair == NULL)
			ide_acpi_set_state(hwif, 0);
	}

	return ret;
}

int generic_ide_resume(struct device *dev)
{
	ide_drive_t *drive = dev_get_drvdata(dev);
	ide_drive_t *pair = ide_get_pair_dev(drive);
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq;
	struct request_pm_state rqpm;
	int err;

	if (ide_port_acpi(hwif)) {
		/* call ACPI _PS0 / _STM only once */
		if ((drive->dn & 1) == 0 || pair == NULL) {
			ide_acpi_set_state(hwif, 1);
			ide_acpi_push_timing(hwif);
		}

		ide_acpi_exec_tfs(drive);
	}

	memset(&rqpm, 0, sizeof(rqpm));
	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_PM_RESUME;
	rq->cmd_flags |= REQ_PREEMPT;
	rq->special = &rqpm;
	rqpm.pm_step = IDE_PM_START_RESUME;
	rqpm.pm_state = PM_EVENT_ON;

	err = blk_execute_rq(drive->queue, NULL, rq, 1);
	blk_put_request(rq);

	if (err == 0 && dev->driver) {
		struct ide_driver *drv = to_ide_driver(dev->driver);

		if (drv->resume)
			drv->resume(drive);
	}

	return err;
}

void ide_complete_power_step(ide_drive_t *drive, struct request *rq)
{
	struct request_pm_state *pm = rq->special;

#ifdef DEBUG_PM
	printk(KERN_INFO "%s: complete_power_step(step: %d)\n",
		drive->name, pm->pm_step);
#endif
	if (drive->media != ide_disk)
		return;

	switch (pm->pm_step) {
	case IDE_PM_FLUSH_CACHE:	/* Suspend step 1 (flush cache) */
		if (pm->pm_state == PM_EVENT_FREEZE)
			pm->pm_step = IDE_PM_COMPLETED;
		else
			pm->pm_step = IDE_PM_STANDBY;
		break;
	case IDE_PM_STANDBY:		/* Suspend step 2 (standby) */
		pm->pm_step = IDE_PM_COMPLETED;
		break;
	case IDE_PM_RESTORE_PIO:	/* Resume step 1 (restore PIO) */
		pm->pm_step = IDE_PM_IDLE;
		break;
	case IDE_PM_IDLE:		/* Resume step 2 (idle)*/
		pm->pm_step = IDE_PM_RESTORE_DMA;
		break;
	}
}

ide_startstop_t ide_start_power_step(ide_drive_t *drive, struct request *rq)
{
	struct request_pm_state *pm = rq->special;
	struct ide_cmd cmd = { };

	switch (pm->pm_step) {
	case IDE_PM_FLUSH_CACHE:	/* Suspend step 1 (flush cache) */
		if (drive->media != ide_disk)
			break;
		/* Not supported? Switch to next step now. */
		if (ata_id_flush_enabled(drive->id) == 0 ||
		    (drive->dev_flags & IDE_DFLAG_WCACHE) == 0) {
			ide_complete_power_step(drive, rq);
			return ide_stopped;
		}
		if (ata_id_flush_ext_enabled(drive->id))
			cmd.tf.command = ATA_CMD_FLUSH_EXT;
		else
			cmd.tf.command = ATA_CMD_FLUSH;
		goto out_do_tf;
	case IDE_PM_STANDBY:		/* Suspend step 2 (standby) */
		cmd.tf.command = ATA_CMD_STANDBYNOW1;
		goto out_do_tf;
	case IDE_PM_RESTORE_PIO:	/* Resume step 1 (restore PIO) */
		ide_set_max_pio(drive);
		/*
		 * skip IDE_PM_IDLE for ATAPI devices
		 */
		if (drive->media != ide_disk)
			pm->pm_step = IDE_PM_RESTORE_DMA;
		else
			ide_complete_power_step(drive, rq);
		return ide_stopped;
	case IDE_PM_IDLE:		/* Resume step 2 (idle) */
		cmd.tf.command = ATA_CMD_IDLEIMMEDIATE;
		goto out_do_tf;
	case IDE_PM_RESTORE_DMA:	/* Resume step 3 (restore DMA) */
		/*
		 * Right now, all we do is call ide_set_dma(drive),
		 * we could be smarter and check for current xfer_speed
		 * in struct drive etc...
		 */
		if (drive->hwif->dma_ops == NULL)
			break;
		/*
		 * TODO: respect IDE_DFLAG_USING_DMA
		 */
		ide_set_dma(drive);
		break;
	}

	pm->pm_step = IDE_PM_COMPLETED;

	return ide_stopped;

out_do_tf:
	cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;
	cmd.protocol = ATA_PROT_NODATA;

	return do_rw_taskfile(drive, &cmd);
}

/**
 *	ide_complete_pm_rq - end the current Power Management request
 *	@drive: target drive
 *	@rq: request
 *
 *	This function cleans up the current PM request and stops the queue
 *	if necessary.
 */
void ide_complete_pm_rq(ide_drive_t *drive, struct request *rq)
{
	struct request_queue *q = drive->queue;
	struct request_pm_state *pm = rq->special;
	unsigned long flags;

	ide_complete_power_step(drive, rq);
	if (pm->pm_step != IDE_PM_COMPLETED)
		return;

#ifdef DEBUG_PM
	printk("%s: completing PM request, %s\n", drive->name,
	       blk_pm_suspend_request(rq) ? "suspend" : "resume");
#endif
	spin_lock_irqsave(q->queue_lock, flags);
	if (blk_pm_suspend_request(rq))
		blk_stop_queue(q);
	else
		drive->dev_flags &= ~IDE_DFLAG_BLOCKED;
	spin_unlock_irqrestore(q->queue_lock, flags);

	drive->hwif->rq = NULL;

	if (blk_end_request(rq, 0, 0))
		BUG();
}

void ide_check_pm_state(ide_drive_t *drive, struct request *rq)
{
	struct request_pm_state *pm = rq->special;

	if (blk_pm_suspend_request(rq) &&
	    pm->pm_step == IDE_PM_START_SUSPEND)
		/* Mark drive blocked when starting the suspend sequence. */
		drive->dev_flags |= IDE_DFLAG_BLOCKED;
	else if (blk_pm_resume_request(rq) &&
		 pm->pm_step == IDE_PM_START_RESUME) {
		/*
		 * The first thing we do on wakeup is to wait for BSY bit to
		 * go away (with a looong timeout) as a drive on this hwif may
		 * just be POSTing itself.
		 * We do that before even selecting as the "other" device on
		 * the bus may be broken enough to walk on our toes at this
		 * point.
		 */
		ide_hwif_t *hwif = drive->hwif;
		const struct ide_tp_ops *tp_ops = hwif->tp_ops;
		struct request_queue *q = drive->queue;
		unsigned long flags;
		int rc;
#ifdef DEBUG_PM
		printk("%s: Wakeup request inited, waiting for !BSY...\n", drive->name);
#endif
		rc = ide_wait_not_busy(hwif, 35000);
		if (rc)
			printk(KERN_WARNING "%s: bus not ready on wakeup\n", drive->name);
		tp_ops->dev_select(drive);
		tp_ops->write_devctl(hwif, ATA_DEVCTL_OBS);
		rc = ide_wait_not_busy(hwif, 100000);
		if (rc)
			printk(KERN_WARNING "%s: drive not ready on wakeup\n", drive->name);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}
