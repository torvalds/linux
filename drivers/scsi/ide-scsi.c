/*
 * Copyright (C) 1996-1999  Gadi Oxman <gadio@netvision.net.il>
 * Copyright (C) 2004-2005  Bartlomiej Zolnierkiewicz
 */

/*
 * Emulation of a SCSI host adapter for IDE ATAPI devices.
 *
 * With this driver, one can use the Linux SCSI drivers instead of the
 * native IDE ATAPI drivers.
 *
 * Ver 0.1   Dec  3 96   Initial version.
 * Ver 0.2   Jan 26 97   Fixed bug in cleanup_module() and added emulation
 *                        of MODE_SENSE_6/MODE_SELECT_6 for cdroms. Thanks
 *                        to Janos Farkas for pointing this out.
 *                       Avoid using bitfields in structures for m68k.
 *                       Added Scatter/Gather and DMA support.
 * Ver 0.4   Dec  7 97   Add support for ATAPI PD/CD drives.
 *                       Use variable timeout for each command.
 * Ver 0.5   Jan  2 98   Fix previous PD/CD support.
 *                       Allow disabling of SCSI-6 to SCSI-10 transformation.
 * Ver 0.6   Jan 27 98   Allow disabling of SCSI command translation layer
 *                        for access through /dev/sg.
 *                       Fix MODE_SENSE_6/MODE_SELECT_6/INQUIRY translation.
 * Ver 0.7   Dec 04 98   Ignore commands where lun != 0 to avoid multiple
 *                        detection of devices with CONFIG_SCSI_MULTI_LUN
 * Ver 0.8   Feb 05 99   Optical media need translation too. Reverse 0.7.
 * Ver 0.9   Jul 04 99   Fix a bug in SG_SET_TRANSFORM.
 * Ver 0.91  Jun 10 02   Fix "off by one" error in transforms
 * Ver 0.92  Dec 31 02   Implement new SCSI mid level API
 */

#define IDESCSI_VERSION "0.92"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/ide.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/sg.h>

#define IDESCSI_DEBUG_LOG		0

#if IDESCSI_DEBUG_LOG
#define debug_log(fmt, args...) \
	printk(KERN_INFO "ide-scsi: " fmt, ## args)
#else
#define debug_log(fmt, args...) do {} while (0)
#endif

/*
 *	SCSI command transformation layer
 */
#define IDESCSI_SG_TRANSFORM		1	/* /dev/sg transformation */

/*
 *	Log flags
 */
#define IDESCSI_LOG_CMD			0	/* Log SCSI commands */

typedef struct ide_scsi_obj {
	ide_drive_t		*drive;
	ide_driver_t		*driver;
	struct gendisk		*disk;
	struct Scsi_Host	*host;

	struct ide_atapi_pc *pc;		/* Current packet command */
	unsigned long transform;		/* SCSI cmd translation layer */
	unsigned long log;			/* log flags */
} idescsi_scsi_t;

static DEFINE_MUTEX(idescsi_ref_mutex);
/* Set by module param to skip cd */
static int idescsi_nocd;

#define ide_scsi_g(disk) \
	container_of((disk)->private_data, struct ide_scsi_obj, driver)

static struct ide_scsi_obj *ide_scsi_get(struct gendisk *disk)
{
	struct ide_scsi_obj *scsi = NULL;

	mutex_lock(&idescsi_ref_mutex);
	scsi = ide_scsi_g(disk);
	if (scsi) {
		if (ide_device_get(scsi->drive))
			scsi = NULL;
		else
			scsi_host_get(scsi->host);
	}
	mutex_unlock(&idescsi_ref_mutex);
	return scsi;
}

static void ide_scsi_put(struct ide_scsi_obj *scsi)
{
	ide_drive_t *drive = scsi->drive;

	mutex_lock(&idescsi_ref_mutex);
	scsi_host_put(scsi->host);
	ide_device_put(drive);
	mutex_unlock(&idescsi_ref_mutex);
}

static inline idescsi_scsi_t *scsihost_to_idescsi(struct Scsi_Host *host)
{
	return (idescsi_scsi_t*) (&host[1]);
}

static inline idescsi_scsi_t *drive_to_idescsi(ide_drive_t *ide_drive)
{
	return scsihost_to_idescsi(ide_drive->driver_data);
}

/*
 *	PIO data transfer routine using the scatter gather table.
 */
static void ide_scsi_io_buffers(ide_drive_t *drive, struct ide_atapi_pc *pc,
				unsigned int bcount, int write)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	xfer_func_t *xf = write ? tp_ops->output_data : tp_ops->input_data;
	char *buf;
	int count;

	while (bcount) {
		count = min(pc->sg->length - pc->b_count, bcount);
		if (PageHighMem(sg_page(pc->sg))) {
			unsigned long flags;

			local_irq_save(flags);
			buf = kmap_atomic(sg_page(pc->sg), KM_IRQ0) +
					  pc->sg->offset;
			xf(drive, NULL, buf + pc->b_count, count);
			kunmap_atomic(buf - pc->sg->offset, KM_IRQ0);
			local_irq_restore(flags);
		} else {
			buf = sg_virt(pc->sg);
			xf(drive, NULL, buf + pc->b_count, count);
		}
		bcount -= count; pc->b_count += count;
		if (pc->b_count == pc->sg->length) {
			if (!--pc->sg_cnt)
				break;
			pc->sg = sg_next(pc->sg);
			pc->b_count = 0;
		}
	}

	if (bcount) {
		printk(KERN_ERR "%s: scatter gather table too small, %s\n",
				drive->name, write ? "padding with zeros"
						   : "discarding data");
		ide_pad_transfer(drive, write, bcount);
	}
}

static void ide_scsi_hex_dump(u8 *data, int len)
{
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_NONE, 16, 1, data, len, 0);
}

static int idescsi_end_request(ide_drive_t *, int, int);

static void ide_scsi_callback(ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	struct ide_atapi_pc *pc = scsi->pc;

	if (pc->flags & PC_FLAG_TIMEDOUT)
		debug_log("%s: got timed out packet %lu at %lu\n", __func__,
			  pc->scsi_cmd->serial_number, jiffies);
		/* end this request now - scsi should retry it*/
	else if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
		printk(KERN_INFO "Packet command completed, %d bytes"
				 " transferred\n", pc->xferred);

	idescsi_end_request(drive, 1, 0);
}

static int idescsi_check_condition(ide_drive_t *drive,
		struct request *failed_cmd)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	struct ide_atapi_pc   *pc;
	struct request *rq;
	u8             *buf;

	/* stuff a sense request in front of our current request */
	pc = kzalloc(sizeof(struct ide_atapi_pc), GFP_ATOMIC);
	rq = blk_get_request(drive->queue, READ, GFP_ATOMIC);
	buf = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_ATOMIC);
	if (!pc || !rq || !buf) {
		kfree(buf);
		if (rq)
			blk_put_request(rq);
		kfree(pc);
		return -ENOMEM;
	}
	rq->special = (char *) pc;
	pc->rq = rq;
	pc->buf = buf;
	pc->c[0] = REQUEST_SENSE;
	pc->c[4] = pc->req_xfer = pc->buf_size = SCSI_SENSE_BUFFERSIZE;
	rq->cmd_type = REQ_TYPE_SENSE;
	rq->cmd_flags |= REQ_PREEMPT;
	pc->timeout = jiffies + WAIT_READY;
	/* NOTE! Save the failed packet command in "rq->buffer" */
	rq->buffer = (void *) failed_cmd->special;
	pc->scsi_cmd = ((struct ide_atapi_pc *) failed_cmd->special)->scsi_cmd;
	if (test_bit(IDESCSI_LOG_CMD, &scsi->log)) {
		printk ("ide-scsi: %s: queue cmd = ", drive->name);
		ide_scsi_hex_dump(pc->c, 6);
	}
	rq->rq_disk = scsi->disk;
	rq->ref_count++;
	memcpy(rq->cmd, pc->c, 12);
	ide_do_drive_cmd(drive, rq);
	return 0;
}

static ide_startstop_t
idescsi_atapi_error(ide_drive_t *drive, struct request *rq, u8 stat, u8 err)
{
	ide_hwif_t *hwif = drive->hwif;

	if (hwif->tp_ops->read_status(hwif) & (ATA_BUSY | ATA_DRQ))
		/* force an abort */
		hwif->tp_ops->exec_command(hwif, ATA_CMD_IDLEIMMEDIATE);

	rq->errors++;

	idescsi_end_request(drive, 0, 0);

	return ide_stopped;
}

static int idescsi_end_request (ide_drive_t *drive, int uptodate, int nrsecs)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	struct request *rq = HWGROUP(drive)->rq;
	struct ide_atapi_pc *pc = (struct ide_atapi_pc *) rq->special;
	int log = test_bit(IDESCSI_LOG_CMD, &scsi->log);
	struct Scsi_Host *host;
	int errors = rq->errors;
	unsigned long flags;

	if (!blk_special_request(rq) && !blk_sense_request(rq)) {
		ide_end_request(drive, uptodate, nrsecs);
		return 0;
	}
	ide_end_drive_cmd (drive, 0, 0);
	if (blk_sense_request(rq)) {
		struct ide_atapi_pc *opc = (struct ide_atapi_pc *) rq->buffer;
		if (log) {
			printk ("ide-scsi: %s: wrap up check %lu, rst = ", drive->name, opc->scsi_cmd->serial_number);
			ide_scsi_hex_dump(pc->buf, 16);
		}
		memcpy((void *) opc->scsi_cmd->sense_buffer, pc->buf,
			SCSI_SENSE_BUFFERSIZE);
		kfree(pc->buf);
		kfree(pc);
		blk_put_request(rq);
		pc = opc;
		rq = pc->rq;
		pc->scsi_cmd->result = (CHECK_CONDITION << 1) |
				(((pc->flags & PC_FLAG_TIMEDOUT) ?
				  DID_TIME_OUT :
				  DID_OK) << 16);
	} else if (pc->flags & PC_FLAG_TIMEDOUT) {
		if (log)
			printk (KERN_WARNING "ide-scsi: %s: timed out for %lu\n",
					drive->name, pc->scsi_cmd->serial_number);
		pc->scsi_cmd->result = DID_TIME_OUT << 16;
	} else if (errors >= ERROR_MAX) {
		pc->scsi_cmd->result = DID_ERROR << 16;
		if (log)
			printk ("ide-scsi: %s: I/O error for %lu\n", drive->name, pc->scsi_cmd->serial_number);
	} else if (errors) {
		if (log)
			printk ("ide-scsi: %s: check condition for %lu\n", drive->name, pc->scsi_cmd->serial_number);
		if (!idescsi_check_condition(drive, rq))
			/* we started a request sense, so we'll be back, exit for now */
			return 0;
		pc->scsi_cmd->result = (CHECK_CONDITION << 1) | (DID_OK << 16);
	} else {
		pc->scsi_cmd->result = DID_OK << 16;
	}
	host = pc->scsi_cmd->device->host;
	spin_lock_irqsave(host->host_lock, flags);
	pc->done(pc->scsi_cmd);
	spin_unlock_irqrestore(host->host_lock, flags);
	kfree(pc);
	blk_put_request(rq);
	scsi->pc = NULL;
	return 0;
}

static inline unsigned long get_timeout(struct ide_atapi_pc *pc)
{
	return max_t(unsigned long, WAIT_CMD, pc->timeout - jiffies);
}

static int idescsi_expiry(ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	struct ide_atapi_pc   *pc   = scsi->pc;

	debug_log("%s called for %lu at %lu\n", __func__,
		  pc->scsi_cmd->serial_number, jiffies);

	pc->flags |= PC_FLAG_TIMEDOUT;

	return 0;					/* we do not want the ide subsystem to retry */
}

/*
 *	Our interrupt handler.
 */
static ide_startstop_t idescsi_pc_intr (ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	struct ide_atapi_pc *pc = scsi->pc;

	return ide_pc_intr(drive, pc, idescsi_pc_intr, get_timeout(pc),
			   idescsi_expiry, NULL, NULL, NULL,
			   ide_scsi_io_buffers);
}

static ide_startstop_t idescsi_transfer_pc(ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);

	return ide_transfer_pc(drive, scsi->pc, idescsi_pc_intr,
			       get_timeout(scsi->pc), idescsi_expiry);
}

static inline int idescsi_set_direction(struct ide_atapi_pc *pc)
{
	switch (pc->c[0]) {
		case READ_6: case READ_10: case READ_12:
			pc->flags &= ~PC_FLAG_WRITING;
			return 0;
		case WRITE_6: case WRITE_10: case WRITE_12:
			pc->flags |= PC_FLAG_WRITING;
			return 0;
		default:
			return 1;
	}
}

static int idescsi_map_sg(ide_drive_t *drive, struct ide_atapi_pc *pc)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg, *scsi_sg;
	int segments;

	if (!pc->req_xfer || pc->req_xfer % 1024)
		return 1;

	if (idescsi_set_direction(pc))
		return 1;

	sg = hwif->sg_table;
	scsi_sg = scsi_sglist(pc->scsi_cmd);
	segments = scsi_sg_count(pc->scsi_cmd);

	if (segments > hwif->sg_max_nents)
		return 1;

	hwif->sg_nents = segments;
	memcpy(sg, scsi_sg, sizeof(*sg) * segments);

	return 0;
}

static ide_startstop_t idescsi_issue_pc(ide_drive_t *drive,
		struct ide_atapi_pc *pc)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);

	/* Set the current packet command */
	scsi->pc = pc;

	return ide_issue_pc(drive, pc, idescsi_transfer_pc,
			    get_timeout(pc), idescsi_expiry);
}

/*
 *	idescsi_do_request is our request handling function.
 */
static ide_startstop_t idescsi_do_request (ide_drive_t *drive, struct request *rq, sector_t block)
{
	debug_log("dev: %s, cmd: %x, errors: %d\n", rq->rq_disk->disk_name,
		  rq->cmd[0], rq->errors);
	debug_log("sector: %ld, nr_sectors: %ld, current_nr_sectors: %d\n",
		  rq->sector, rq->nr_sectors, rq->current_nr_sectors);

	if (blk_sense_request(rq) || blk_special_request(rq)) {
		struct ide_atapi_pc *pc = (struct ide_atapi_pc *)rq->special;

		if (drive->using_dma && !idescsi_map_sg(drive, pc))
			pc->flags |= PC_FLAG_DMA_OK;

		return idescsi_issue_pc(drive, pc);
	}
	blk_dump_rq_flags(rq, "ide-scsi: unsup command");
	idescsi_end_request (drive, 0, 0);
	return ide_stopped;
}

#ifdef CONFIG_IDE_PROC_FS
#define ide_scsi_devset_get(name, field) \
static int get_##name(ide_drive_t *drive) \
{ \
	idescsi_scsi_t *scsi = drive_to_idescsi(drive); \
	return scsi->field; \
}

#define ide_scsi_devset_set(name, field) \
static int set_##name(ide_drive_t *drive, int arg) \
{ \
	idescsi_scsi_t *scsi = drive_to_idescsi(drive); \
	scsi->field = arg; \
	return 0; \
}

#define ide_scsi_devset_rw(_name, _min, _max, _field) \
ide_scsi_devset_get(_name, _field); \
ide_scsi_devset_set(_name, _field); \
IDE_DEVSET(_name, S_RW, _min, _max, get_##_name, set_##_name)

ide_devset_rw(bios_cyl,		0, 1023, bios_cyl);
ide_devset_rw(bios_head,	0,  255, bios_head);
ide_devset_rw(bios_sect,	0,   63, bios_sect);

ide_scsi_devset_rw(transform,	0,    3, transform);
ide_scsi_devset_rw(log,		0,    1, log);

static const struct ide_devset *idescsi_settings[] = {
	&ide_devset_bios_cyl,
	&ide_devset_bios_head,
	&ide_devset_bios_sect,
	&ide_devset_log,
	&ide_devset_transform,
	NULL
};
#endif

/*
 *	Driver initialization.
 */
static void idescsi_setup (ide_drive_t *drive, idescsi_scsi_t *scsi)
{
	if ((drive->id[ATA_ID_CONFIG] & 0x0060) == 0x20)
		set_bit(IDE_AFLAG_DRQ_INTERRUPT, &drive->atapi_flags);
	clear_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
#if IDESCSI_DEBUG_LOG
	set_bit(IDESCSI_LOG_CMD, &scsi->log);
#endif /* IDESCSI_DEBUG_LOG */

	drive->pc_callback = ide_scsi_callback;

	ide_proc_register_driver(drive, scsi->driver);
}

static void ide_scsi_remove(ide_drive_t *drive)
{
	struct Scsi_Host *scsihost = drive->driver_data;
	struct ide_scsi_obj *scsi = scsihost_to_idescsi(scsihost);
	struct gendisk *g = scsi->disk;

	scsi_remove_host(scsihost);
	ide_proc_unregister_driver(drive, scsi->driver);

	ide_unregister_region(g);

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);

	ide_scsi_put(scsi);

	drive->scsi = 0;
}

static int ide_scsi_probe(ide_drive_t *);

#ifdef CONFIG_IDE_PROC_FS
static ide_proc_entry_t idescsi_proc[] = {
	{ "capacity", S_IFREG|S_IRUGO, proc_ide_read_capacity, NULL },
	{ NULL, 0, NULL, NULL }
};
#endif

static ide_driver_t idescsi_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-scsi",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_scsi_probe,
	.remove			= ide_scsi_remove,
	.version		= IDESCSI_VERSION,
	.media			= ide_scsi,
	.supports_dsc_overlap	= 0,
	.do_request		= idescsi_do_request,
	.end_request		= idescsi_end_request,
	.error                  = idescsi_atapi_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idescsi_proc,
	.settings		= idescsi_settings,
#endif
};

static int idescsi_ide_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_scsi_obj *scsi;

	if (!(scsi = ide_scsi_get(disk)))
		return -ENXIO;

	return 0;
}

static int idescsi_ide_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_scsi_obj *scsi = ide_scsi_g(disk);

	ide_scsi_put(scsi);

	return 0;
}

static int idescsi_ide_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct ide_scsi_obj *scsi = ide_scsi_g(bdev->bd_disk);
	return generic_ide_ioctl(scsi->drive, file, bdev, cmd, arg);
}

static struct block_device_operations idescsi_ops = {
	.owner		= THIS_MODULE,
	.open		= idescsi_ide_open,
	.release	= idescsi_ide_release,
	.ioctl		= idescsi_ide_ioctl,
};

static int idescsi_slave_configure(struct scsi_device * sdp)
{
	/* Configure detected device */
	sdp->use_10_for_rw = 1;
	sdp->use_10_for_ms = 1;
	scsi_adjust_queue_depth(sdp, MSG_SIMPLE_TAG, sdp->host->cmd_per_lun);
	return 0;
}

static const char *idescsi_info (struct Scsi_Host *host)
{
	return "SCSI host adapter emulation for IDE ATAPI devices";
}

static int idescsi_ioctl (struct scsi_device *dev, int cmd, void __user *arg)
{
	idescsi_scsi_t *scsi = scsihost_to_idescsi(dev->host);

	if (cmd == SG_SET_TRANSFORM) {
		if (arg)
			set_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
		else
			clear_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
		return 0;
	} else if (cmd == SG_GET_TRANSFORM)
		return put_user(test_bit(IDESCSI_SG_TRANSFORM, &scsi->transform), (int __user *) arg);
	return -EINVAL;
}

static int idescsi_queue (struct scsi_cmnd *cmd,
		void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = cmd->device->host;
	idescsi_scsi_t *scsi = scsihost_to_idescsi(host);
	ide_drive_t *drive = scsi->drive;
	struct request *rq = NULL;
	struct ide_atapi_pc *pc = NULL;
	int write = cmd->sc_data_direction == DMA_TO_DEVICE;

	if (!drive) {
		scmd_printk (KERN_ERR, cmd, "drive not present\n");
		goto abort;
	}
	scsi = drive_to_idescsi(drive);
	pc = kmalloc(sizeof(struct ide_atapi_pc), GFP_ATOMIC);
	rq = blk_get_request(drive->queue, write, GFP_ATOMIC);
	if (rq == NULL || pc == NULL) {
		printk (KERN_ERR "ide-scsi: %s: out of memory\n", drive->name);
		goto abort;
	}

	memset (pc->c, 0, 12);
	pc->flags = 0;
	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		pc->flags |= PC_FLAG_WRITING;
	pc->rq = rq;
	memcpy (pc->c, cmd->cmnd, cmd->cmd_len);
	pc->buf = NULL;
	pc->sg = scsi_sglist(cmd);
	pc->sg_cnt = scsi_sg_count(cmd);
	pc->b_count = 0;
	pc->req_xfer = pc->buf_size = scsi_bufflen(cmd);
	pc->scsi_cmd = cmd;
	pc->done = done;
	pc->timeout = jiffies + cmd->request->timeout;

	if (test_bit(IDESCSI_LOG_CMD, &scsi->log)) {
		printk ("ide-scsi: %s: que %lu, cmd = ", drive->name, cmd->serial_number);
		ide_scsi_hex_dump(cmd->cmnd, cmd->cmd_len);
		if (memcmp(pc->c, cmd->cmnd, cmd->cmd_len)) {
			printk ("ide-scsi: %s: que %lu, tsl = ", drive->name, cmd->serial_number);
			ide_scsi_hex_dump(pc->c, 12);
		}
	}

	rq->special = (char *) pc;
	rq->cmd_type = REQ_TYPE_SPECIAL;
	spin_unlock_irq(host->host_lock);
	rq->ref_count++;
	memcpy(rq->cmd, pc->c, 12);
	blk_execute_rq_nowait(drive->queue, scsi->disk, rq, 0, NULL);
	spin_lock_irq(host->host_lock);
	return 0;
abort:
	kfree (pc);
	if (rq)
		blk_put_request(rq);
	cmd->result = DID_ERROR << 16;
	done(cmd);
	return 0;
}

static int idescsi_eh_abort (struct scsi_cmnd *cmd)
{
	idescsi_scsi_t *scsi  = scsihost_to_idescsi(cmd->device->host);
	ide_drive_t    *drive = scsi->drive;
	int		busy;
	int             ret   = FAILED;

	/* In idescsi_eh_abort we try to gently pry our command from the ide subsystem */

	if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
		printk (KERN_WARNING "ide-scsi: abort called for %lu\n", cmd->serial_number);

	if (!drive) {
		printk (KERN_WARNING "ide-scsi: Drive not set in idescsi_eh_abort\n");
		WARN_ON(1);
		goto no_drive;
	}

	/* First give it some more time, how much is "right" is hard to say :-( */

	busy = ide_wait_not_busy(HWIF(drive), 100);	/* FIXME - uses mdelay which causes latency? */
	if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
		printk (KERN_WARNING "ide-scsi: drive did%s become ready\n", busy?" not":"");

	spin_lock_irq(&ide_lock);

	/* If there is no pc running we're done (our interrupt took care of it) */
	if (!scsi->pc) {
		ret = SUCCESS;
		goto ide_unlock;
	}

	/* It's somewhere in flight. Does ide subsystem agree? */
	if (scsi->pc->scsi_cmd->serial_number == cmd->serial_number && !busy &&
	    elv_queue_empty(drive->queue) && HWGROUP(drive)->rq != scsi->pc->rq) {
		/*
		 * FIXME - not sure this condition can ever occur
		 */
		printk (KERN_ERR "ide-scsi: cmd aborted!\n");

		if (blk_sense_request(scsi->pc->rq))
			kfree(scsi->pc->buf);
		/* we need to call blk_put_request twice. */
		blk_put_request(scsi->pc->rq);
		blk_put_request(scsi->pc->rq);
		kfree(scsi->pc);
		scsi->pc = NULL;

		ret = SUCCESS;
	}

ide_unlock:
	spin_unlock_irq(&ide_lock);
no_drive:
	if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
		printk (KERN_WARNING "ide-scsi: abort returns %s\n", ret == SUCCESS?"success":"failed");

	return ret;
}

static int idescsi_eh_reset (struct scsi_cmnd *cmd)
{
	struct request *req;
	idescsi_scsi_t *scsi  = scsihost_to_idescsi(cmd->device->host);
	ide_drive_t    *drive = scsi->drive;
	int             ready = 0;
	int             ret   = SUCCESS;

	/* In idescsi_eh_reset we forcefully remove the command from the ide subsystem and reset the device. */

	if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
		printk (KERN_WARNING "ide-scsi: reset called for %lu\n", cmd->serial_number);

	if (!drive) {
		printk (KERN_WARNING "ide-scsi: Drive not set in idescsi_eh_reset\n");
		WARN_ON(1);
		return FAILED;
	}

	spin_lock_irq(cmd->device->host->host_lock);
	spin_lock(&ide_lock);

	if (!scsi->pc || (req = scsi->pc->rq) != HWGROUP(drive)->rq || !HWGROUP(drive)->handler) {
		printk (KERN_WARNING "ide-scsi: No active request in idescsi_eh_reset\n");
		spin_unlock(&ide_lock);
		spin_unlock_irq(cmd->device->host->host_lock);
		return FAILED;
	}

	/* kill current request */
	if (__blk_end_request(req, -EIO, 0))
		BUG();
	if (blk_sense_request(req))
		kfree(scsi->pc->buf);
	kfree(scsi->pc);
	scsi->pc = NULL;
	blk_put_request(req);

	/* now nuke the drive queue */
	while ((req = elv_next_request(drive->queue))) {
		if (__blk_end_request(req, -EIO, 0))
			BUG();
	}

	HWGROUP(drive)->rq = NULL;
	HWGROUP(drive)->handler = NULL;
	HWGROUP(drive)->busy = 1;		/* will set this to zero when ide reset finished */
	spin_unlock(&ide_lock);

	ide_do_reset(drive);

	/* ide_do_reset starts a polling handler which restarts itself every 50ms until the reset finishes */

	do {
		spin_unlock_irq(cmd->device->host->host_lock);
		msleep(50);
		spin_lock_irq(cmd->device->host->host_lock);
	} while ( HWGROUP(drive)->handler );

	ready = drive_is_ready(drive);
	HWGROUP(drive)->busy--;
	if (!ready) {
		printk (KERN_ERR "ide-scsi: reset failed!\n");
		ret = FAILED;
	}

	spin_unlock_irq(cmd->device->host->host_lock);
	return ret;
}

static int idescsi_bios(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int *parm)
{
	idescsi_scsi_t *idescsi = scsihost_to_idescsi(sdev->host);
	ide_drive_t *drive = idescsi->drive;

	if (drive->bios_cyl && drive->bios_head && drive->bios_sect) {
		parm[0] = drive->bios_head;
		parm[1] = drive->bios_sect;
		parm[2] = drive->bios_cyl;
	}
	return 0;
}

static struct scsi_host_template idescsi_template = {
	.module			= THIS_MODULE,
	.name			= "idescsi",
	.info			= idescsi_info,
	.slave_configure        = idescsi_slave_configure,
	.ioctl			= idescsi_ioctl,
	.queuecommand		= idescsi_queue,
	.eh_abort_handler	= idescsi_eh_abort,
	.eh_host_reset_handler  = idescsi_eh_reset,
	.bios_param		= idescsi_bios,
	.can_queue		= 40,
	.this_id		= -1,
	.sg_tablesize		= 256,
	.cmd_per_lun		= 5,
	.max_sectors		= 128,
	.use_clustering		= DISABLE_CLUSTERING,
	.emulated		= 1,
	.proc_name		= "ide-scsi",
};

static int ide_scsi_probe(ide_drive_t *drive)
{
	idescsi_scsi_t *idescsi;
	struct Scsi_Host *host;
	struct gendisk *g;
	static int warned;
	int err = -ENOMEM;
	u16 last_lun;

	if (!warned && drive->media == ide_cdrom) {
		printk(KERN_WARNING "ide-scsi is deprecated for cd burning! Use ide-cd and give dev=/dev/hdX as device\n");
		warned = 1;
	}

	if (idescsi_nocd && drive->media == ide_cdrom)
		return -ENODEV;

	if (!strstr("ide-scsi", drive->driver_req) ||
	    drive->media == ide_disk ||
	    !(host = scsi_host_alloc(&idescsi_template,sizeof(idescsi_scsi_t))))
		return -ENODEV;

	drive->scsi = 1;

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_host_put;

	ide_init_disk(g, drive);

	host->max_id = 1;

	last_lun = drive->id[ATA_ID_LAST_LUN];
	if (last_lun)
		debug_log("%s: last_lun=%u\n", drive->name, last_lun);

	if ((last_lun & 7) != 7)
		host->max_lun = (last_lun & 7) + 1;
	else
		host->max_lun = 1;

	drive->driver_data = host;
	idescsi = scsihost_to_idescsi(host);
	idescsi->drive = drive;
	idescsi->driver = &idescsi_driver;
	idescsi->host = host;
	idescsi->disk = g;
	g->private_data = &idescsi->driver;
	err = 0;
	idescsi_setup(drive, idescsi);
	g->fops = &idescsi_ops;
	ide_register_region(g);
	err = scsi_add_host(host, &drive->gendev);
	if (!err) {
		scsi_scan_host(host);
		return 0;
	}
	/* fall through on error */
	ide_unregister_region(g);
	ide_proc_unregister_driver(drive, &idescsi_driver);

	put_disk(g);
out_host_put:
	drive->scsi = 0;
	scsi_host_put(host);
	return err;
}

static int __init init_idescsi_module(void)
{
	return driver_register(&idescsi_driver.gen_driver);
}

static void __exit exit_idescsi_module(void)
{
	driver_unregister(&idescsi_driver.gen_driver);
}

module_param(idescsi_nocd, int, 0600);
MODULE_PARM_DESC(idescsi_nocd, "Disable handling of CD-ROMs so they may be driven by ide-cd");
module_init(init_idescsi_module);
module_exit(exit_idescsi_module);
MODULE_LICENSE("GPL");
