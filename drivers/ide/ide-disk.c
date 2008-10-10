/*
 *  Copyright (C) 1994-1998	   Linus Torvalds & authors (see below)
 *  Copyright (C) 1998-2002	   Linux ATA Development
 *				      Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003		   Red Hat <alan@redhat.com>
 *  Copyright (C) 2003-2005, 2007  Bartlomiej Zolnierkiewicz
 */

/*
 *  Mostly written by Mark Lord <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 * This is the IDE/ATA disk driver, as evolved from hd.c and ide.c.
 */

#define IDEDISK_VERSION	"1.18"

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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/leds.h>

#define _IDE_DISK

#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/div64.h>

#if !defined(CONFIG_DEBUG_BLOCK_EXT_DEVT)
#define IDE_DISK_MINORS		(1 << PARTN_BITS)
#else
#define IDE_DISK_MINORS		0
#endif

struct ide_disk_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;
	unsigned int	openers;	/* protected by BKL for now */
};

static DEFINE_MUTEX(idedisk_ref_mutex);

#define to_ide_disk(obj) container_of(obj, struct ide_disk_obj, kref)

#define ide_disk_g(disk) \
	container_of((disk)->private_data, struct ide_disk_obj, driver)

static void ide_disk_release(struct kref *);

static struct ide_disk_obj *ide_disk_get(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = NULL;

	mutex_lock(&idedisk_ref_mutex);
	idkp = ide_disk_g(disk);
	if (idkp) {
		if (ide_device_get(idkp->drive))
			idkp = NULL;
		else
			kref_get(&idkp->kref);
	}
	mutex_unlock(&idedisk_ref_mutex);
	return idkp;
}

static void ide_disk_put(struct ide_disk_obj *idkp)
{
	ide_drive_t *drive = idkp->drive;

	mutex_lock(&idedisk_ref_mutex);
	kref_put(&idkp->kref, ide_disk_release);
	ide_device_put(drive);
	mutex_unlock(&idedisk_ref_mutex);
}

/*
 * lba_capacity_is_ok() performs a sanity check on the claimed "lba_capacity"
 * value for this drive (from its reported identification information).
 *
 * Returns:	1 if lba_capacity looks sensible
 *		0 otherwise
 *
 * It is called only once for each drive.
 */
static int lba_capacity_is_ok(u16 *id)
{
	unsigned long lba_sects, chs_sects, head, tail;

	/* No non-LBA info .. so valid! */
	if (id[ATA_ID_CYLS] == 0)
		return 1;

	lba_sects = ata_id_u32(id, ATA_ID_LBA_CAPACITY);

	/*
	 * The ATA spec tells large drives to return
	 * C/H/S = 16383/16/63 independent of their size.
	 * Some drives can be jumpered to use 15 heads instead of 16.
	 * Some drives can be jumpered to use 4092 cyls instead of 16383.
	 */
	if ((id[ATA_ID_CYLS] == 16383 ||
	     (id[ATA_ID_CYLS] == 4092 && id[ATA_ID_CUR_CYLS] == 16383)) &&
	    id[ATA_ID_SECTORS] == 63 &&
	    (id[ATA_ID_HEADS] == 15 || id[ATA_ID_HEADS] == 16) &&
	    (lba_sects >= 16383 * 63 * id[ATA_ID_HEADS]))
		return 1;

	chs_sects = id[ATA_ID_CYLS] * id[ATA_ID_HEADS] * id[ATA_ID_SECTORS];

	/* perform a rough sanity check on lba_sects:  within 10% is OK */
	if ((lba_sects - chs_sects) < chs_sects/10)
		return 1;

	/* some drives have the word order reversed */
	head = ((lba_sects >> 16) & 0xffff);
	tail = (lba_sects & 0xffff);
	lba_sects = (head | (tail << 16));
	if ((lba_sects - chs_sects) < chs_sects/10) {
		*(__le32 *)&id[ATA_ID_LBA_CAPACITY] = __cpu_to_le32(lba_sects);
		return 1;	/* lba_capacity is (now) good */
	}

	return 0;	/* lba_capacity value may be bad */
}

static const u8 ide_rw_cmds[] = {
	WIN_MULTREAD,
	WIN_MULTWRITE,
	WIN_MULTREAD_EXT,
	WIN_MULTWRITE_EXT,
	WIN_READ,
	WIN_WRITE,
	WIN_READ_EXT,
	WIN_WRITE_EXT,
	WIN_READDMA,
	WIN_WRITEDMA,
	WIN_READDMA_EXT,
	WIN_WRITEDMA_EXT,
};

static const u8 ide_data_phases[] = {
	TASKFILE_MULTI_IN,
	TASKFILE_MULTI_OUT,
	TASKFILE_IN,
	TASKFILE_OUT,
	TASKFILE_IN_DMA,
	TASKFILE_OUT_DMA,
};

static void ide_tf_set_cmd(ide_drive_t *drive, ide_task_t *task, u8 dma)
{
	u8 index, lba48, write;

	lba48 = (task->tf_flags & IDE_TFLAG_LBA48) ? 2 : 0;
	write = (task->tf_flags & IDE_TFLAG_WRITE) ? 1 : 0;

	if (dma)
		index = 8;
	else
		index = drive->mult_count ? 0 : 4;

	task->tf.command = ide_rw_cmds[index + lba48 + write];

	if (dma)
		index = 8; /* fixup index */

	task->data_phase = ide_data_phases[index / 2 + write];
}

/*
 * __ide_do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 */
static ide_startstop_t __ide_do_rw_disk(ide_drive_t *drive, struct request *rq,
					sector_t block)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned int dma	= drive->using_dma;
	u16 nsectors		= (u16)rq->nr_sectors;
	u8 lba48		= (drive->addressing == 1) ? 1 : 0;
	ide_task_t		task;
	struct ide_taskfile	*tf = &task.tf;
	ide_startstop_t		rc;

	if ((hwif->host_flags & IDE_HFLAG_NO_LBA48_DMA) && lba48 && dma) {
		if (block + rq->nr_sectors > 1ULL << 28)
			dma = 0;
		else
			lba48 = 0;
	}

	if (!dma) {
		ide_init_sg_cmd(drive, rq);
		ide_map_sg(drive, rq);
	}

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;

	if (drive->select.b.lba) {
		if (lba48) {
			pr_debug("%s: LBA=0x%012llx\n", drive->name,
					(unsigned long long)block);

			tf->hob_nsect = (nsectors >> 8) & 0xff;
			tf->hob_lbal  = (u8)(block >> 24);
			if (sizeof(block) != 4) {
				tf->hob_lbam = (u8)((u64)block >> 32);
				tf->hob_lbah = (u8)((u64)block >> 40);
			}

			tf->nsect  = nsectors & 0xff;
			tf->lbal   = (u8) block;
			tf->lbam   = (u8)(block >>  8);
			tf->lbah   = (u8)(block >> 16);

			task.tf_flags |= (IDE_TFLAG_LBA48 | IDE_TFLAG_HOB);
		} else {
			tf->nsect  = nsectors & 0xff;
			tf->lbal   = block;
			tf->lbam   = block >>= 8;
			tf->lbah   = block >>= 8;
			tf->device = (block >> 8) & 0xf;
		}
	} else {
		unsigned int sect, head, cyl, track;

		track = (int)block / drive->sect;
		sect  = (int)block % drive->sect + 1;
		head  = track % drive->head;
		cyl   = track / drive->head;

		pr_debug("%s: CHS=%u/%u/%u\n", drive->name, cyl, head, sect);

		tf->nsect  = nsectors & 0xff;
		tf->lbal   = sect;
		tf->lbam   = cyl;
		tf->lbah   = cyl >> 8;
		tf->device = head;
	}

	if (rq_data_dir(rq))
		task.tf_flags |= IDE_TFLAG_WRITE;

	ide_tf_set_cmd(drive, &task, dma);
	if (!dma)
		hwif->data_phase = task.data_phase;
	task.rq = rq;

	rc = do_rw_taskfile(drive, &task);

	if (rc == ide_stopped && dma) {
		/* fallback to PIO */
		task.tf_flags |= IDE_TFLAG_DMA_PIO_FALLBACK;
		ide_tf_set_cmd(drive, &task, 0);
		hwif->data_phase = task.data_phase;
		ide_init_sg_cmd(drive, rq);
		rc = do_rw_taskfile(drive, &task);
	}

	return rc;
}

/*
 * 268435455  == 137439 MB or 28bit limit
 * 320173056  == 163929 MB or 48bit addressing
 * 1073741822 == 549756 MB or 48bit addressing fake drive
 */

static ide_startstop_t ide_do_rw_disk(ide_drive_t *drive, struct request *rq,
				      sector_t block)
{
	ide_hwif_t *hwif = HWIF(drive);

	BUG_ON(drive->blocked);

	if (!blk_fs_request(rq)) {
		blk_dump_rq_flags(rq, "ide_do_rw_disk - bad command");
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}

	ledtrig_ide_activity();

	pr_debug("%s: %sing: block=%llu, sectors=%lu, buffer=0x%08lx\n",
		 drive->name, rq_data_dir(rq) == READ ? "read" : "writ",
		 (unsigned long long)block, rq->nr_sectors,
		 (unsigned long)rq->buffer);

	if (hwif->rw_disk)
		hwif->rw_disk(drive, rq);

	return __ide_do_rw_disk(drive, rq, block);
}

/*
 * Queries for true maximum capacity of the drive.
 * Returns maximum LBA address (> 0) of the drive, 0 if failed.
 */
static u64 idedisk_read_native_max_address(ide_drive_t *drive, int lba48)
{
	ide_task_t args;
	struct ide_taskfile *tf = &args.tf;
	u64 addr = 0;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	if (lba48)
		tf->command = WIN_READ_NATIVE_MAX_EXT;
	else
		tf->command = WIN_READ_NATIVE_MAX;
	tf->device  = ATA_LBA;
	args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	if (lba48)
		args.tf_flags |= (IDE_TFLAG_LBA48 | IDE_TFLAG_HOB);
	/* submit command request */
	ide_no_data_taskfile(drive, &args);

	/* if OK, compute maximum address value */
	if ((tf->status & 0x01) == 0)
		addr = ide_get_lba_addr(tf, lba48) + 1;

	return addr;
}

/*
 * Sets maximum virtual LBA address of the drive.
 * Returns new maximum virtual LBA address (> 0) or 0 on failure.
 */
static u64 idedisk_set_max_address(ide_drive_t *drive, u64 addr_req, int lba48)
{
	ide_task_t args;
	struct ide_taskfile *tf = &args.tf;
	u64 addr_set = 0;

	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	tf->lbal     = (addr_req >>  0) & 0xff;
	tf->lbam     = (addr_req >>= 8) & 0xff;
	tf->lbah     = (addr_req >>= 8) & 0xff;
	if (lba48) {
		tf->hob_lbal = (addr_req >>= 8) & 0xff;
		tf->hob_lbam = (addr_req >>= 8) & 0xff;
		tf->hob_lbah = (addr_req >>= 8) & 0xff;
		tf->command  = WIN_SET_MAX_EXT;
	} else {
		tf->device   = (addr_req >>= 8) & 0x0f;
		tf->command  = WIN_SET_MAX;
	}
	tf->device |= ATA_LBA;
	args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	if (lba48)
		args.tf_flags |= (IDE_TFLAG_LBA48 | IDE_TFLAG_HOB);
	/* submit command request */
	ide_no_data_taskfile(drive, &args);
	/* if OK, compute maximum address value */
	if ((tf->status & 0x01) == 0)
		addr_set = ide_get_lba_addr(tf, lba48) + 1;

	return addr_set;
}

static unsigned long long sectors_to_MB(unsigned long long n)
{
	n <<= 9;		/* make it bytes */
	do_div(n, 1000000);	/* make it MB */
	return n;
}

/*
 * Bits 10 of command_set_1 and cfs_enable_1 must be equal,
 * so on non-buggy drives we need test only one.
 * However, we should also check whether these fields are valid.
 */
static inline int idedisk_supports_hpa(const u16 *id)
{
	return (id[ATA_ID_COMMAND_SET_1] & 0x0400) &&
	       (id[ATA_ID_CFS_ENABLE_1] & 0x0400);
}

/*
 * The same here.
 */
static inline int idedisk_supports_lba48(const u16 *id)
{
	return (id[ATA_ID_COMMAND_SET_2] & 0x0400) &&
	       (id[ATA_ID_CFS_ENABLE_2] & 0x0400) &&
	       ata_id_u64(id, ATA_ID_LBA_CAPACITY_2);
}

/*
 * Some disks report total number of sectors instead of
 * maximum sector address.  We list them here.
 */
static const struct drive_list_entry hpa_list[] = {
	{ "ST340823A",	NULL },
	{ "ST320413A",	NULL },
	{ "ST310211A",	NULL },
	{ NULL,		NULL }
};

static void idedisk_check_hpa(ide_drive_t *drive)
{
	unsigned long long capacity, set_max;
	int lba48 = idedisk_supports_lba48(drive->id);

	capacity = drive->capacity64;

	set_max = idedisk_read_native_max_address(drive, lba48);

	if (ide_in_drive_list(drive->id, hpa_list)) {
		/*
		 * Since we are inclusive wrt to firmware revisions do this
		 * extra check and apply the workaround only when needed.
		 */
		if (set_max == capacity + 1)
			set_max--;
	}

	if (set_max <= capacity)
		return;

	printk(KERN_INFO "%s: Host Protected Area detected.\n"
			 "\tcurrent capacity is %llu sectors (%llu MB)\n"
			 "\tnative  capacity is %llu sectors (%llu MB)\n",
			 drive->name,
			 capacity, sectors_to_MB(capacity),
			 set_max, sectors_to_MB(set_max));

	set_max = idedisk_set_max_address(drive, set_max, lba48);

	if (set_max) {
		drive->capacity64 = set_max;
		printk(KERN_INFO "%s: Host Protected Area disabled.\n",
				 drive->name);
	}
}

static void init_idedisk_capacity(ide_drive_t *drive)
{
	u16 *id = drive->id;
	/*
	 * If this drive supports the Host Protected Area feature set,
	 * then we may need to change our opinion about the drive's capacity.
	 */
	int hpa = idedisk_supports_hpa(id);

	if (idedisk_supports_lba48(id)) {
		/* drive speaks 48-bit LBA */
		drive->select.b.lba = 1;
		drive->capacity64 = ata_id_u64(id, ATA_ID_LBA_CAPACITY_2);
		if (hpa)
			idedisk_check_hpa(drive);
	} else if (ata_id_has_lba(id) && lba_capacity_is_ok(id)) {
		/* drive speaks 28-bit LBA */
		drive->select.b.lba = 1;
		drive->capacity64 = ata_id_u32(id, ATA_ID_LBA_CAPACITY);
		if (hpa)
			idedisk_check_hpa(drive);
	} else {
		/* drive speaks boring old 28-bit CHS */
		drive->capacity64 = drive->cyl * drive->head * drive->sect;
	}
}

static sector_t idedisk_capacity(ide_drive_t *drive)
{
	return drive->capacity64 - drive->sect0;
}

#ifdef CONFIG_IDE_PROC_FS
static int smart_enable(ide_drive_t *drive)
{
	ide_task_t args;
	struct ide_taskfile *tf = &args.tf;

	memset(&args, 0, sizeof(ide_task_t));
	tf->feature = SMART_ENABLE;
	tf->lbam    = SMART_LCYL_PASS;
	tf->lbah    = SMART_HCYL_PASS;
	tf->command = WIN_SMART;
	args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	return ide_no_data_taskfile(drive, &args);
}

static int get_smart_data(ide_drive_t *drive, u8 *buf, u8 sub_cmd)
{
	ide_task_t args;
	struct ide_taskfile *tf = &args.tf;

	memset(&args, 0, sizeof(ide_task_t));
	tf->feature = sub_cmd;
	tf->nsect   = 0x01;
	tf->lbam    = SMART_LCYL_PASS;
	tf->lbah    = SMART_HCYL_PASS;
	tf->command = WIN_SMART;
	args.tf_flags	= IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	args.data_phase	= TASKFILE_IN;
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf, 1);
}

static int proc_idedisk_read_cache
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	if (drive->id_read)
		len = sprintf(out, "%i\n", drive->id[ATA_ID_BUF_SIZE] / 2);
	else
		len = sprintf(out, "(none)\n");

	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_idedisk_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t*drive = (ide_drive_t *)data;
	int len;

	len = sprintf(page, "%llu\n", (long long)idedisk_capacity(drive));

	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_idedisk_read_smart(char *page, char **start, off_t off,
				   int count, int *eof, void *data, u8 sub_cmd)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (get_smart_data(drive, page, sub_cmd) == 0) {
		unsigned short *val = (unsigned short *) page;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val),
				       (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}

	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_idedisk_read_sv
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_idedisk_read_smart(page, start, off, count, eof, data,
				       SMART_READ_VALUES);
}

static int proc_idedisk_read_st
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_idedisk_read_smart(page, start, off, count, eof, data,
				       SMART_READ_THRESHOLDS);
}

static ide_proc_entry_t idedisk_proc[] = {
	{ "cache",	  S_IFREG|S_IRUGO, proc_idedisk_read_cache,    NULL },
	{ "capacity",	  S_IFREG|S_IRUGO, proc_idedisk_read_capacity, NULL },
	{ "geometry",	  S_IFREG|S_IRUGO, proc_ide_read_geometry,     NULL },
	{ "smart_values", S_IFREG|S_IRUSR, proc_idedisk_read_sv,       NULL },
	{ "smart_thresholds", S_IFREG|S_IRUSR, proc_idedisk_read_st,   NULL },
	{ NULL, 0, NULL, NULL }
};
#endif	/* CONFIG_IDE_PROC_FS */

static void idedisk_prepare_flush(struct request_queue *q, struct request *rq)
{
	ide_drive_t *drive = q->queuedata;
	ide_task_t *task = kmalloc(sizeof(*task), GFP_ATOMIC);

	/* FIXME: map struct ide_taskfile on rq->cmd[] */
	BUG_ON(task == NULL);

	memset(task, 0, sizeof(*task));
	if (ide_id_has_flush_cache_ext(drive->id) &&
	    (drive->capacity64 >= (1UL << 28)))
		task->tf.command = WIN_FLUSH_CACHE_EXT;
	else
		task->tf.command = WIN_FLUSH_CACHE;
	task->tf_flags	 = IDE_TFLAG_OUT_TF | IDE_TFLAG_OUT_DEVICE |
			   IDE_TFLAG_DYN;
	task->data_phase = TASKFILE_NO_DATA;

	rq->cmd_type = REQ_TYPE_ATA_TASKFILE;
	rq->cmd_flags |= REQ_SOFTBARRIER;
	rq->special = task;
}

/*
 * This is tightly woven into the driver->do_special can not touch.
 * DON'T do it again until a total personality rewrite is committed.
 */
static int set_multcount(ide_drive_t *drive, int arg)
{
	struct request *rq;
	int error;

	if (arg < 0 || arg > (drive->id[ATA_ID_MAX_MULTSECT] & 0xff))
		return -EINVAL;

	if (drive->special.b.set_multmode)
		return -EBUSY;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_ATA_TASKFILE;

	drive->mult_req = arg;
	drive->special.b.set_multmode = 1;
	error = blk_execute_rq(drive->queue, NULL, rq, 0);
	blk_put_request(rq);

	return (drive->mult_count == arg) ? 0 : -EIO;
}

static int set_nowerr(ide_drive_t *drive, int arg)
{
	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->nowerr = arg;
	drive->bad_wstat = arg ? BAD_R_STAT : BAD_W_STAT;
	spin_unlock_irq(&ide_lock);
	return 0;
}

static void update_ordered(ide_drive_t *drive)
{
	u16 *id = drive->id;
	unsigned ordered = QUEUE_ORDERED_NONE;
	prepare_flush_fn *prep_fn = NULL;

	if (drive->wcache) {
		unsigned long long capacity;
		int barrier;
		/*
		 * We must avoid issuing commands a drive does not
		 * understand or we may crash it. We check flush cache
		 * is supported. We also check we have the LBA48 flush
		 * cache if the drive capacity is too large. By this
		 * time we have trimmed the drive capacity if LBA48 is
		 * not available so we don't need to recheck that.
		 */
		capacity = idedisk_capacity(drive);
		barrier = ide_id_has_flush_cache(id) && !drive->noflush &&
			(drive->addressing == 0 || capacity <= (1ULL << 28) ||
			 ide_id_has_flush_cache_ext(id));

		printk(KERN_INFO "%s: cache flushes %ssupported\n",
		       drive->name, barrier ? "" : "not ");

		if (barrier) {
			ordered = QUEUE_ORDERED_DRAIN_FLUSH;
			prep_fn = idedisk_prepare_flush;
		}
	} else
		ordered = QUEUE_ORDERED_DRAIN;

	blk_queue_ordered(drive->queue, ordered, prep_fn);
}

static int write_cache(ide_drive_t *drive, int arg)
{
	ide_task_t args;
	int err = 1;

	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ide_id_has_flush_cache(drive->id)) {
		memset(&args, 0, sizeof(ide_task_t));
		args.tf.feature = arg ?
			SETFEATURES_EN_WCACHE : SETFEATURES_DIS_WCACHE;
		args.tf.command = WIN_SETFEATURES;
		args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
		err = ide_no_data_taskfile(drive, &args);
		if (err == 0)
			drive->wcache = arg;
	}

	update_ordered(drive);

	return err;
}

static int do_idedisk_flushcache(ide_drive_t *drive)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	if (ide_id_has_flush_cache_ext(drive->id))
		args.tf.command = WIN_FLUSH_CACHE_EXT;
	else
		args.tf.command = WIN_FLUSH_CACHE;
	args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	return ide_no_data_taskfile(drive, &args);
}

static int set_acoustic(ide_drive_t *drive, int arg)
{
	ide_task_t args;

	if (arg < 0 || arg > 254)
		return -EINVAL;

	memset(&args, 0, sizeof(ide_task_t));
	args.tf.feature = arg ? SETFEATURES_EN_AAM : SETFEATURES_DIS_AAM;
	args.tf.nsect   = arg;
	args.tf.command = WIN_SETFEATURES;
	args.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	ide_no_data_taskfile(drive, &args);
	drive->acoustic = arg;
	return 0;
}

/*
 * drive->addressing:
 *	0: 28-bit
 *	1: 48-bit
 *	2: 48-bit capable doing 28-bit
 */
static int set_lba_addressing(ide_drive_t *drive, int arg)
{
	if (arg < 0 || arg > 2)
		return -EINVAL;

	drive->addressing =  0;

	if (drive->hwif->host_flags & IDE_HFLAG_NO_LBA48)
		return 0;

	if (!idedisk_supports_lba48(drive->id))
		return -EIO;
	drive->addressing = arg;
	return 0;
}

#ifdef CONFIG_IDE_PROC_FS
static void idedisk_add_settings(ide_drive_t *drive)
{
	ide_add_setting(drive, "bios_cyl", SETTING_RW, TYPE_INT, 0, 65535, 1, 1,
			&drive->bios_cyl, NULL);
	ide_add_setting(drive, "bios_head", SETTING_RW, TYPE_BYTE, 0, 255, 1, 1,
			&drive->bios_head, NULL);
	ide_add_setting(drive, "bios_sect", SETTING_RW, TYPE_BYTE, 0, 63, 1, 1,
			&drive->bios_sect, NULL);
	ide_add_setting(drive, "address", SETTING_RW, TYPE_BYTE, 0, 2, 1, 1,
			&drive->addressing, set_lba_addressing);
	ide_add_setting(drive, "multcount", SETTING_RW, TYPE_BYTE, 0,
			drive->id[ATA_ID_MAX_MULTSECT] & 0xff, 1, 1,
			&drive->mult_count, set_multcount);
	ide_add_setting(drive, "nowerr", SETTING_RW, TYPE_BYTE, 0, 1, 1, 1,
			&drive->nowerr, set_nowerr);
	ide_add_setting(drive, "lun", SETTING_RW, TYPE_INT, 0, 7, 1, 1,
			&drive->lun, NULL);
	ide_add_setting(drive, "wcache", SETTING_RW, TYPE_BYTE, 0, 1, 1, 1,
			&drive->wcache, write_cache);
	ide_add_setting(drive, "acoustic", SETTING_RW, TYPE_BYTE, 0, 254, 1, 1,
			&drive->acoustic, set_acoustic);
	ide_add_setting(drive, "failures", SETTING_RW, TYPE_INT, 0, 65535, 1, 1,
			&drive->failures, NULL);
	ide_add_setting(drive, "max_failures", SETTING_RW, TYPE_INT, 0, 65535,
			1, 1, &drive->max_failures, NULL);
}
#else
static inline void idedisk_add_settings(ide_drive_t *drive) { ; }
#endif

static void idedisk_setup(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u16 *id = drive->id;
	char *m = (char *)&id[ATA_ID_PROD];
	unsigned long long capacity;

	idedisk_add_settings(drive);

	if (drive->id_read == 0)
		return;

	if (drive->removable) {
		/*
		 * Removable disks (eg. SYQUEST); ignore 'WD' drives
		 */
		if (m[0] != 'W' || m[1] != 'D')
			drive->doorlocking = 1;
	}

	(void)set_lba_addressing(drive, 1);

	if (drive->addressing == 1) {
		int max_s = 2048;

		if (max_s > hwif->rqsize)
			max_s = hwif->rqsize;

		blk_queue_max_sectors(drive->queue, max_s);
	}

	printk(KERN_INFO "%s: max request size: %dKiB\n", drive->name,
			 drive->queue->max_sectors / 2);

	/* calculate drive capacity, and select LBA if possible */
	init_idedisk_capacity(drive);

	/* limit drive capacity to 137GB if LBA48 cannot be used */
	if (drive->addressing == 0 && drive->capacity64 > 1ULL << 28) {
		printk(KERN_WARNING "%s: cannot use LBA48 - full capacity "
		       "%llu sectors (%llu MB)\n",
		       drive->name, (unsigned long long)drive->capacity64,
		       sectors_to_MB(drive->capacity64));
		drive->capacity64 = 1ULL << 28;
	}

	if ((hwif->host_flags & IDE_HFLAG_NO_LBA48_DMA) && drive->addressing) {
		if (drive->capacity64 > 1ULL << 28) {
			printk(KERN_INFO "%s: cannot use LBA48 DMA - PIO mode"
					 " will be used for accessing sectors "
					 "> %u\n", drive->name, 1 << 28);
		} else
			drive->addressing = 0;
	}

	/*
	 * if possible, give fdisk access to more of the drive,
	 * by correcting bios_cyls:
	 */
	capacity = idedisk_capacity(drive);

	if (!drive->forced_geom) {

		if (idedisk_supports_lba48(drive->id)) {
			/* compatibility */
			drive->bios_sect = 63;
			drive->bios_head = 255;
		}

		if (drive->bios_sect && drive->bios_head) {
			unsigned int cap0 = capacity; /* truncate to 32 bits */
			unsigned int cylsz, cyl;

			if (cap0 != capacity)
				drive->bios_cyl = 65535;
			else {
				cylsz = drive->bios_sect * drive->bios_head;
				cyl = cap0 / cylsz;
				if (cyl > 65535)
					cyl = 65535;
				if (cyl > drive->bios_cyl)
					drive->bios_cyl = cyl;
			}
		}
	}
	printk(KERN_INFO "%s: %llu sectors (%llu MB)",
			 drive->name, capacity, sectors_to_MB(capacity));

	/* Only print cache size when it was specified */
	if (id[ATA_ID_BUF_SIZE])
		printk(KERN_CONT " w/%dKiB Cache", id[ATA_ID_BUF_SIZE] / 2);

	printk(KERN_CONT ", CHS=%d/%d/%d\n",
			 drive->bios_cyl, drive->bios_head, drive->bios_sect);

	/* write cache enabled? */
	if ((id[ATA_ID_CSFO] & 1) || ata_id_wcache_enabled(id))
		drive->wcache = 1;

	write_cache(drive, 1);
}

static void ide_cacheflush_p(ide_drive_t *drive)
{
	if (!drive->wcache || !ide_id_has_flush_cache(drive->id))
		return;

	if (do_idedisk_flushcache(drive))
		printk(KERN_INFO "%s: wcache flush failed!\n", drive->name);
}

static void ide_disk_remove(ide_drive_t *drive)
{
	struct ide_disk_obj *idkp = drive->driver_data;
	struct gendisk *g = idkp->disk;

	ide_proc_unregister_driver(drive, idkp->driver);

	del_gendisk(g);

	ide_cacheflush_p(drive);

	ide_disk_put(idkp);
}

static void ide_disk_release(struct kref *kref)
{
	struct ide_disk_obj *idkp = to_ide_disk(kref);
	ide_drive_t *drive = idkp->drive;
	struct gendisk *g = idkp->disk;

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(idkp);
}

static int ide_disk_probe(ide_drive_t *drive);

/*
 * On HPA drives the capacity needs to be
 * reinitilized on resume otherwise the disk
 * can not be used and a hard reset is required
 */
static void ide_disk_resume(ide_drive_t *drive)
{
	if (idedisk_supports_hpa(drive->id))
		init_idedisk_capacity(drive);
}

static void ide_device_shutdown(ide_drive_t *drive)
{
#ifdef	CONFIG_ALPHA
	/* On Alpha, halt(8) doesn't actually turn the machine off,
	   it puts you into the sort of firmware monitor. Typically,
	   it's used to boot another kernel image, so it's not much
	   different from reboot(8). Therefore, we don't need to
	   spin down the disk in this case, especially since Alpha
	   firmware doesn't handle disks in standby mode properly.
	   On the other hand, it's reasonably safe to turn the power
	   off when the shutdown process reaches the firmware prompt,
	   as the firmware initialization takes rather long time -
	   at least 10 seconds, which should be sufficient for
	   the disk to expire its write cache. */
	if (system_state != SYSTEM_POWER_OFF) {
#else
	if (system_state == SYSTEM_RESTART) {
#endif
		ide_cacheflush_p(drive);
		return;
	}

	printk(KERN_INFO "Shutdown: %s\n", drive->name);

	drive->gendev.bus->suspend(&drive->gendev, PMSG_SUSPEND);
}

static ide_driver_t idedisk_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-disk",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_disk_probe,
	.remove			= ide_disk_remove,
	.resume			= ide_disk_resume,
	.shutdown		= ide_device_shutdown,
	.version		= IDEDISK_VERSION,
	.media			= ide_disk,
	.supports_dsc_overlap	= 0,
	.do_request		= ide_do_rw_disk,
	.end_request		= ide_end_request,
	.error			= __ide_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idedisk_proc,
#endif
};

static int idedisk_set_doorlock(ide_drive_t *drive, int on)
{
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf.command = on ? WIN_DOORLOCK : WIN_DOORUNLOCK;
	task.tf_flags = IDE_TFLAG_TF | IDE_TFLAG_DEVICE;

	return ide_no_data_taskfile(drive, &task);
}

static int idedisk_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_disk_obj *idkp;
	ide_drive_t *drive;

	idkp = ide_disk_get(disk);
	if (idkp == NULL)
		return -ENXIO;

	drive = idkp->drive;

	idkp->openers++;

	if (drive->removable && idkp->openers == 1) {
		check_disk_change(inode->i_bdev);
		/*
		 * Ignore the return code from door_lock,
		 * since the open() has already succeeded,
		 * and the door_lock is irrelevant at this point.
		 */
		if (drive->doorlocking && idedisk_set_doorlock(drive, 1))
			drive->doorlocking = 0;
	}
	return 0;
}

static int idedisk_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_disk_obj *idkp = ide_disk_g(disk);
	ide_drive_t *drive = idkp->drive;

	if (idkp->openers == 1)
		ide_cacheflush_p(drive);

	if (drive->removable && idkp->openers == 1) {
		if (drive->doorlocking && idedisk_set_doorlock(drive, 0))
			drive->doorlocking = 0;
	}

	idkp->openers--;

	ide_disk_put(idkp);

	return 0;
}

static int idedisk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ide_disk_obj *idkp = ide_disk_g(bdev->bd_disk);
	ide_drive_t *drive = idkp->drive;

	geo->heads = drive->bios_head;
	geo->sectors = drive->bios_sect;
	geo->cylinders = (u16)drive->bios_cyl; /* truncate */
	return 0;
}

static int idedisk_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	struct block_device *bdev = inode->i_bdev;
	struct ide_disk_obj *idkp = ide_disk_g(bdev->bd_disk);
	ide_drive_t *drive = idkp->drive;
	int err, (*setfunc)(ide_drive_t *, int);
	u8 *val;

	switch (cmd) {
	case HDIO_GET_ADDRESS:	 val = &drive->addressing;	goto read_val;
	case HDIO_GET_MULTCOUNT: val = &drive->mult_count;	goto read_val;
	case HDIO_GET_NOWERR:	 val = &drive->nowerr;		goto read_val;
	case HDIO_GET_WCACHE:	 val = &drive->wcache;		goto read_val;
	case HDIO_GET_ACOUSTIC:	 val = &drive->acoustic;	goto read_val;
	case HDIO_SET_ADDRESS:	 setfunc = set_lba_addressing;	goto set_val;
	case HDIO_SET_MULTCOUNT: setfunc = set_multcount;	goto set_val;
	case HDIO_SET_NOWERR:	 setfunc = set_nowerr;		goto set_val;
	case HDIO_SET_WCACHE:	 setfunc = write_cache;		goto set_val;
	case HDIO_SET_ACOUSTIC:	 setfunc = set_acoustic;	goto set_val;
	}

	return generic_ide_ioctl(drive, file, bdev, cmd, arg);

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

static int idedisk_media_changed(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = ide_disk_g(disk);
	ide_drive_t *drive = idkp->drive;

	/* do not scan partitions twice if this is a removable device */
	if (drive->attach) {
		drive->attach = 0;
		return 0;
	}
	/* if removable, always assume it was changed */
	return drive->removable;
}

static int idedisk_revalidate_disk(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = ide_disk_g(disk);
	set_capacity(disk, idedisk_capacity(idkp->drive));
	return 0;
}

static struct block_device_operations idedisk_ops = {
	.owner			= THIS_MODULE,
	.open			= idedisk_open,
	.release		= idedisk_release,
	.ioctl			= idedisk_ioctl,
	.getgeo			= idedisk_getgeo,
	.media_changed		= idedisk_media_changed,
	.revalidate_disk	= idedisk_revalidate_disk
};

MODULE_DESCRIPTION("ATA DISK Driver");

static int ide_disk_probe(ide_drive_t *drive)
{
	struct ide_disk_obj *idkp;
	struct gendisk *g;

	/* strstr("foo", "") is non-NULL */
	if (!strstr("ide-disk", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_disk)
		goto failed;

	idkp = kzalloc(sizeof(*idkp), GFP_KERNEL);
	if (!idkp)
		goto failed;

	g = alloc_disk_node(IDE_DISK_MINORS, hwif_to_node(drive->hwif));
	if (!g)
		goto out_free_idkp;

	ide_init_disk(g, drive);

	ide_proc_register_driver(drive, &idedisk_driver);

	kref_init(&idkp->kref);

	idkp->drive = drive;
	idkp->driver = &idedisk_driver;
	idkp->disk = g;

	g->private_data = &idkp->driver;

	drive->driver_data = idkp;

	idedisk_setup(drive);
	if ((!drive->head || drive->head > 16) && !drive->select.b.lba) {
		printk(KERN_ERR "%s: INVALID GEOMETRY: %d PHYSICAL HEADS?\n",
			drive->name, drive->head);
		drive->attach = 0;
	} else
		drive->attach = 1;

	g->minors = IDE_DISK_MINORS;
	g->driverfs_dev = &drive->gendev;
	g->flags |= GENHD_FL_EXT_DEVT;
	if (drive->removable)
		g->flags |= GENHD_FL_REMOVABLE;
	set_capacity(g, idedisk_capacity(drive));
	g->fops = &idedisk_ops;
	add_disk(g);
	return 0;

out_free_idkp:
	kfree(idkp);
failed:
	return -ENODEV;
}

static void __exit idedisk_exit(void)
{
	driver_unregister(&idedisk_driver.gen_driver);
}

static int __init idedisk_init(void)
{
	return driver_register(&idedisk_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-disk*");
module_init(idedisk_init);
module_exit(idedisk_exit);
MODULE_LICENSE("GPL");
