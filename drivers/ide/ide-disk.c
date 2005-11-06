/*
 *  linux/drivers/ide/ide-disk.c	Version 1.18	Mar 05, 2003
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 *  Copyright (C) 1998-2002  Linux ATA Development
 *				Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003	     Red Hat <alan@redhat.com>
 */

/*
 *  Mostly written by Mark Lord <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 * This is the IDE/ATA disk driver, as evolved from hd.c and ide.c.
 *
 * Version 1.00		move disk only code from ide.c to ide-disk.c
 *			support optional byte-swapping of all data
 * Version 1.01		fix previous byte-swapping code
 * Version 1.02		remove ", LBA" from drive identification msgs
 * Version 1.03		fix display of id->buf_size for big-endian
 * Version 1.04		add /proc configurable settings and S.M.A.R.T support
 * Version 1.05		add capacity support for ATA3 >= 8GB
 * Version 1.06		get boot-up messages to show full cyl count
 * Version 1.07		disable door-locking if it fails
 * Version 1.08		fixed CHS/LBA translations for ATA4 > 8GB,
 *			process of adding new ATA4 compliance.
 *			fixed problems in allowing fdisk to see
 *			the entire disk.
 * Version 1.09		added increment of rq->sector in ide_multwrite
 *			added UDMA 3/4 reporting
 * Version 1.10		request queue changes, Ultra DMA 100
 * Version 1.11		added 48-bit lba
 * Version 1.12		adding taskfile io access method
 * Version 1.13		added standby and flush-cache for notifier
 * Version 1.14		added acoustic-wcache
 * Version 1.15		convert all calls to ide_raw_taskfile
 *				since args will return register content.
 * Version 1.16		added suspend-resume-checkpower
 * Version 1.17		do flush on standy, do flush on ATA < ATA6
 *			fix wcache setup.
 */

#define IDEDISK_VERSION	"1.18"

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

//#define DEBUG

#include <linux/config.h>
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

#define _IDE_DISK

#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/div64.h>

struct ide_disk_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;
};

static DECLARE_MUTEX(idedisk_ref_sem);

#define to_ide_disk(obj) container_of(obj, struct ide_disk_obj, kref)

#define ide_disk_g(disk) \
	container_of((disk)->private_data, struct ide_disk_obj, driver)

static struct ide_disk_obj *ide_disk_get(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = NULL;

	down(&idedisk_ref_sem);
	idkp = ide_disk_g(disk);
	if (idkp)
		kref_get(&idkp->kref);
	up(&idedisk_ref_sem);
	return idkp;
}

static void ide_disk_release(struct kref *);

static void ide_disk_put(struct ide_disk_obj *idkp)
{
	down(&idedisk_ref_sem);
	kref_put(&idkp->kref, ide_disk_release);
	up(&idedisk_ref_sem);
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
static int lba_capacity_is_ok (struct hd_driveid *id)
{
	unsigned long lba_sects, chs_sects, head, tail;

	/* No non-LBA info .. so valid! */
	if (id->cyls == 0)
		return 1;

	/*
	 * The ATA spec tells large drives to return
	 * C/H/S = 16383/16/63 independent of their size.
	 * Some drives can be jumpered to use 15 heads instead of 16.
	 * Some drives can be jumpered to use 4092 cyls instead of 16383.
	 */
	if ((id->cyls == 16383
	     || (id->cyls == 4092 && id->cur_cyls == 16383)) &&
	    id->sectors == 63 &&
	    (id->heads == 15 || id->heads == 16) &&
	    (id->lba_capacity >= 16383*63*id->heads))
		return 1;

	lba_sects   = id->lba_capacity;
	chs_sects   = id->cyls * id->heads * id->sectors;

	/* perform a rough sanity check on lba_sects:  within 10% is OK */
	if ((lba_sects - chs_sects) < chs_sects/10)
		return 1;

	/* some drives have the word order reversed */
	head = ((lba_sects >> 16) & 0xffff);
	tail = (lba_sects & 0xffff);
	lba_sects = (head | (tail << 16));
	if ((lba_sects - chs_sects) < chs_sects/10) {
		id->lba_capacity = lba_sects;
		return 1;	/* lba_capacity is (now) good */
	}

	return 0;	/* lba_capacity value may be bad */
}

/*
 * __ide_do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 */
static ide_startstop_t __ide_do_rw_disk(ide_drive_t *drive, struct request *rq, sector_t block)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned int dma	= drive->using_dma;
	u8 lba48		= (drive->addressing == 1) ? 1 : 0;
	task_ioreg_t command	= WIN_NOP;
	ata_nsector_t		nsectors;

	nsectors.all		= (u16) rq->nr_sectors;

	if (hwif->no_lba48_dma && lba48 && dma) {
		if (block + rq->nr_sectors > 1ULL << 28)
			dma = 0;
		else
			lba48 = 0;
	}

	if (!dma) {
		ide_init_sg_cmd(drive, rq);
		ide_map_sg(drive, rq);
	}

	if (IDE_CONTROL_REG)
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);

	/* FIXME: SELECT_MASK(drive, 0) ? */

	if (drive->select.b.lba) {
		if (lba48) {
			task_ioreg_t tasklets[10];

			pr_debug("%s: LBA=0x%012llx\n", drive->name, block);

			tasklets[0] = 0;
			tasklets[1] = 0;
			tasklets[2] = nsectors.b.low;
			tasklets[3] = nsectors.b.high;
			tasklets[4] = (task_ioreg_t) block;
			tasklets[5] = (task_ioreg_t) (block>>8);
			tasklets[6] = (task_ioreg_t) (block>>16);
			tasklets[7] = (task_ioreg_t) (block>>24);
			if (sizeof(block) == 4) {
				tasklets[8] = (task_ioreg_t) 0;
				tasklets[9] = (task_ioreg_t) 0;
			} else {
				tasklets[8] = (task_ioreg_t)((u64)block >> 32);
				tasklets[9] = (task_ioreg_t)((u64)block >> 40);
			}
#ifdef DEBUG
			printk("%s: 0x%02x%02x 0x%02x%02x%02x%02x%02x%02x\n",
				drive->name, tasklets[3], tasklets[2],
				tasklets[9], tasklets[8], tasklets[7],
				tasklets[6], tasklets[5], tasklets[4]);
#endif
			hwif->OUTB(tasklets[1], IDE_FEATURE_REG);
			hwif->OUTB(tasklets[3], IDE_NSECTOR_REG);
			hwif->OUTB(tasklets[7], IDE_SECTOR_REG);
			hwif->OUTB(tasklets[8], IDE_LCYL_REG);
			hwif->OUTB(tasklets[9], IDE_HCYL_REG);

			hwif->OUTB(tasklets[0], IDE_FEATURE_REG);
			hwif->OUTB(tasklets[2], IDE_NSECTOR_REG);
			hwif->OUTB(tasklets[4], IDE_SECTOR_REG);
			hwif->OUTB(tasklets[5], IDE_LCYL_REG);
			hwif->OUTB(tasklets[6], IDE_HCYL_REG);
			hwif->OUTB(0x00|drive->select.all,IDE_SELECT_REG);
		} else {
			hwif->OUTB(0x00, IDE_FEATURE_REG);
			hwif->OUTB(nsectors.b.low, IDE_NSECTOR_REG);
			hwif->OUTB(block, IDE_SECTOR_REG);
			hwif->OUTB(block>>=8, IDE_LCYL_REG);
			hwif->OUTB(block>>=8, IDE_HCYL_REG);
			hwif->OUTB(((block>>8)&0x0f)|drive->select.all,IDE_SELECT_REG);
		}
	} else {
		unsigned int sect,head,cyl,track;
		track = (int)block / drive->sect;
		sect  = (int)block % drive->sect + 1;
		hwif->OUTB(sect, IDE_SECTOR_REG);
		head  = track % drive->head;
		cyl   = track / drive->head;

		pr_debug("%s: CHS=%u/%u/%u\n", drive->name, cyl, head, sect);

		hwif->OUTB(0x00, IDE_FEATURE_REG);
		hwif->OUTB(nsectors.b.low, IDE_NSECTOR_REG);
		hwif->OUTB(cyl, IDE_LCYL_REG);
		hwif->OUTB(cyl>>8, IDE_HCYL_REG);
		hwif->OUTB(head|drive->select.all,IDE_SELECT_REG);
	}

	if (dma) {
		if (!hwif->dma_setup(drive)) {
			if (rq_data_dir(rq)) {
				command = lba48 ? WIN_WRITEDMA_EXT : WIN_WRITEDMA;
				if (drive->vdma)
					command = lba48 ? WIN_WRITE_EXT: WIN_WRITE;
			} else {
				command = lba48 ? WIN_READDMA_EXT : WIN_READDMA;
				if (drive->vdma)
					command = lba48 ? WIN_READ_EXT: WIN_READ;
			}
			hwif->dma_exec_cmd(drive, command);
			hwif->dma_start(drive);
			return ide_started;
		}
		/* fallback to PIO */
		ide_init_sg_cmd(drive, rq);
	}

	if (rq_data_dir(rq) == READ) {

		if (drive->mult_count) {
			hwif->data_phase = TASKFILE_MULTI_IN;
			command = lba48 ? WIN_MULTREAD_EXT : WIN_MULTREAD;
		} else {
			hwif->data_phase = TASKFILE_IN;
			command = lba48 ? WIN_READ_EXT : WIN_READ;
		}

		ide_execute_command(drive, command, &task_in_intr, WAIT_CMD, NULL);
		return ide_started;
	} else {
		if (drive->mult_count) {
			hwif->data_phase = TASKFILE_MULTI_OUT;
			command = lba48 ? WIN_MULTWRITE_EXT : WIN_MULTWRITE;
		} else {
			hwif->data_phase = TASKFILE_OUT;
			command = lba48 ? WIN_WRITE_EXT : WIN_WRITE;
		}

		/* FIXME: ->OUTBSYNC ? */
		hwif->OUTB(command, IDE_COMMAND_REG);

		return pre_task_out_intr(drive, rq);
	}
}

/*
 * 268435455  == 137439 MB or 28bit limit
 * 320173056  == 163929 MB or 48bit addressing
 * 1073741822 == 549756 MB or 48bit addressing fake drive
 */

static ide_startstop_t ide_do_rw_disk (ide_drive_t *drive, struct request *rq, sector_t block)
{
	ide_hwif_t *hwif = HWIF(drive);

	BUG_ON(drive->blocked);

	if (!blk_fs_request(rq)) {
		blk_dump_rq_flags(rq, "ide_do_rw_disk - bad command");
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}

	pr_debug("%s: %sing: block=%llu, sectors=%lu, buffer=0x%08lx\n",
		 drive->name, rq_data_dir(rq) == READ ? "read" : "writ",
		 block, rq->nr_sectors, (unsigned long)rq->buffer);

	if (hwif->rw_disk)
		hwif->rw_disk(drive, rq);

	return __ide_do_rw_disk(drive, rq, block);
}

/*
 * Queries for true maximum capacity of the drive.
 * Returns maximum LBA address (> 0) of the drive, 0 if failed.
 */
static unsigned long idedisk_read_native_max_address(ide_drive_t *drive)
{
	ide_task_t args;
	unsigned long addr = 0;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_READ_NATIVE_MAX;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		addr = ((args.tfRegister[IDE_SELECT_OFFSET] & 0x0f) << 24)
		     | ((args.tfRegister[  IDE_HCYL_OFFSET]       ) << 16)
		     | ((args.tfRegister[  IDE_LCYL_OFFSET]       ) <<  8)
		     | ((args.tfRegister[IDE_SECTOR_OFFSET]       ));
		addr++;	/* since the return value is (maxlba - 1), we add 1 */
	}
	return addr;
}

static unsigned long long idedisk_read_native_max_address_ext(ide_drive_t *drive)
{
	ide_task_t args;
	unsigned long long addr = 0;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));

	args.tfRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_READ_NATIVE_MAX_EXT;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
        /* submit command request */
        ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		u32 high = (args.hobRegister[IDE_HCYL_OFFSET] << 16) |
			   (args.hobRegister[IDE_LCYL_OFFSET] <<  8) |
			    args.hobRegister[IDE_SECTOR_OFFSET];
		u32 low  = ((args.tfRegister[IDE_HCYL_OFFSET])<<16) |
			   ((args.tfRegister[IDE_LCYL_OFFSET])<<8) |
			    (args.tfRegister[IDE_SECTOR_OFFSET]);
		addr = ((__u64)high << 24) | low;
		addr++;	/* since the return value is (maxlba - 1), we add 1 */
	}
	return addr;
}

/*
 * Sets maximum virtual LBA address of the drive.
 * Returns new maximum virtual LBA address (> 0) or 0 on failure.
 */
static unsigned long idedisk_set_max_address(ide_drive_t *drive, unsigned long addr_req)
{
	ide_task_t args;
	unsigned long addr_set = 0;
	
	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SECTOR_OFFSET]	= ((addr_req >>  0) & 0xff);
	args.tfRegister[IDE_LCYL_OFFSET]	= ((addr_req >>  8) & 0xff);
	args.tfRegister[IDE_HCYL_OFFSET]	= ((addr_req >> 16) & 0xff);
	args.tfRegister[IDE_SELECT_OFFSET]	= ((addr_req >> 24) & 0x0f) | 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SET_MAX;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);
	/* if OK, read new maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		addr_set = ((args.tfRegister[IDE_SELECT_OFFSET] & 0x0f) << 24)
			 | ((args.tfRegister[  IDE_HCYL_OFFSET]       ) << 16)
			 | ((args.tfRegister[  IDE_LCYL_OFFSET]       ) <<  8)
			 | ((args.tfRegister[IDE_SECTOR_OFFSET]       ));
		addr_set++;
	}
	return addr_set;
}

static unsigned long long idedisk_set_max_address_ext(ide_drive_t *drive, unsigned long long addr_req)
{
	ide_task_t args;
	unsigned long long addr_set = 0;

	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SECTOR_OFFSET]	= ((addr_req >>  0) & 0xff);
	args.tfRegister[IDE_LCYL_OFFSET]	= ((addr_req >>= 8) & 0xff);
	args.tfRegister[IDE_HCYL_OFFSET]	= ((addr_req >>= 8) & 0xff);
	args.tfRegister[IDE_SELECT_OFFSET]      = 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SET_MAX_EXT;
	args.hobRegister[IDE_SECTOR_OFFSET]	= (addr_req >>= 8) & 0xff;
	args.hobRegister[IDE_LCYL_OFFSET]	= (addr_req >>= 8) & 0xff;
	args.hobRegister[IDE_HCYL_OFFSET]	= (addr_req >>= 8) & 0xff;
	args.hobRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.hobRegister[IDE_CONTROL_OFFSET_HOB]= (drive->ctl|0x80);
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);
	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		u32 high = (args.hobRegister[IDE_HCYL_OFFSET] << 16) |
			   (args.hobRegister[IDE_LCYL_OFFSET] <<  8) |
			    args.hobRegister[IDE_SECTOR_OFFSET];
		u32 low  = ((args.tfRegister[IDE_HCYL_OFFSET])<<16) |
			   ((args.tfRegister[IDE_LCYL_OFFSET])<<8) |
			    (args.tfRegister[IDE_SECTOR_OFFSET]);
		addr_set = ((__u64)high << 24) | low;
		addr_set++;
	}
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
static inline int idedisk_supports_hpa(const struct hd_driveid *id)
{
	return (id->command_set_1 & 0x0400) && (id->cfs_enable_1 & 0x0400);
}

/*
 * The same here.
 */
static inline int idedisk_supports_lba48(const struct hd_driveid *id)
{
	return (id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400)
	       && id->lba_capacity_2;
}

static inline void idedisk_check_hpa(ide_drive_t *drive)
{
	unsigned long long capacity, set_max;
	int lba48 = idedisk_supports_lba48(drive->id);

	capacity = drive->capacity64;
	if (lba48)
		set_max = idedisk_read_native_max_address_ext(drive);
	else
		set_max = idedisk_read_native_max_address(drive);

	if (set_max <= capacity)
		return;

	printk(KERN_INFO "%s: Host Protected Area detected.\n"
			 "\tcurrent capacity is %llu sectors (%llu MB)\n"
			 "\tnative  capacity is %llu sectors (%llu MB)\n",
			 drive->name,
			 capacity, sectors_to_MB(capacity),
			 set_max, sectors_to_MB(set_max));

	if (lba48)
		set_max = idedisk_set_max_address_ext(drive, set_max);
	else
		set_max = idedisk_set_max_address(drive, set_max);
	if (set_max) {
		drive->capacity64 = set_max;
		printk(KERN_INFO "%s: Host Protected Area disabled.\n",
				 drive->name);
	}
}

/*
 * Compute drive->capacity, the full capacity of the drive
 * Called with drive->id != NULL.
 *
 * To compute capacity, this uses either of
 *
 *    1. CHS value set by user       (whatever user sets will be trusted)
 *    2. LBA value from target drive (require new ATA feature)
 *    3. LBA value from system BIOS  (new one is OK, old one may break)
 *    4. CHS value from system BIOS  (traditional style)
 *
 * in above order (i.e., if value of higher priority is available,
 * reset will be ignored).
 */
static void init_idedisk_capacity (ide_drive_t  *drive)
{
	struct hd_driveid *id = drive->id;
	/*
	 * If this drive supports the Host Protected Area feature set,
	 * then we may need to change our opinion about the drive's capacity.
	 */
	int hpa = idedisk_supports_hpa(id);

	if (idedisk_supports_lba48(id)) {
		/* drive speaks 48-bit LBA */
		drive->select.b.lba = 1;
		drive->capacity64 = id->lba_capacity_2;
		if (hpa)
			idedisk_check_hpa(drive);
	} else if ((id->capability & 2) && lba_capacity_is_ok(id)) {
		/* drive speaks 28-bit LBA */
		drive->select.b.lba = 1;
		drive->capacity64 = id->lba_capacity;
		if (hpa)
			idedisk_check_hpa(drive);
	} else {
		/* drive speaks boring old 28-bit CHS */
		drive->capacity64 = drive->cyl * drive->head * drive->sect;
	}
}

static sector_t idedisk_capacity (ide_drive_t *drive)
{
	return drive->capacity64 - drive->sect0;
}

#ifdef CONFIG_PROC_FS

static int smart_enable(ide_drive_t *drive)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_ENABLE;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	return ide_raw_taskfile(drive, &args, NULL);
}

static int get_smart_values(ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_READ_VALUES;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= IDE_DRIVE_TASK_IN;
	args.data_phase				= TASKFILE_IN;
	args.handler				= &task_in_intr;
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf);
}

static int get_smart_thresholds(ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_READ_THRESHOLDS;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= IDE_DRIVE_TASK_IN;
	args.data_phase				= TASKFILE_IN;
	args.handler				= &task_in_intr;
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf);
}

static int proc_idedisk_read_cache
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	if (drive->id_read)
		len = sprintf(out,"%i\n", drive->id->buf_size / 2);
	else
		len = sprintf(out,"(none)\n");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_idedisk_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t*drive = (ide_drive_t *)data;
	int len;

	len = sprintf(page,"%llu\n", (long long)idedisk_capacity(drive));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_idedisk_read_smart_thresholds
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (!get_smart_thresholds(drive, page)) {
		unsigned short *val = (unsigned short *) page;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_idedisk_read_smart_values
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (!get_smart_values(drive, page)) {
		unsigned short *val = (unsigned short *) page;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t idedisk_proc[] = {
	{ "cache",		S_IFREG|S_IRUGO,	proc_idedisk_read_cache,		NULL },
	{ "capacity",		S_IFREG|S_IRUGO,	proc_idedisk_read_capacity,		NULL },
	{ "geometry",		S_IFREG|S_IRUGO,	proc_ide_read_geometry,			NULL },
	{ "smart_values",	S_IFREG|S_IRUSR,	proc_idedisk_read_smart_values,		NULL },
	{ "smart_thresholds",	S_IFREG|S_IRUSR,	proc_idedisk_read_smart_thresholds,	NULL },
	{ NULL, 0, NULL, NULL }
};

#else

#define	idedisk_proc	NULL

#endif	/* CONFIG_PROC_FS */

static void idedisk_end_flush(request_queue_t *q, struct request *flush_rq)
{
	ide_drive_t *drive = q->queuedata;
	struct request *rq = flush_rq->end_io_data;
	int good_sectors = rq->hard_nr_sectors;
	int bad_sectors;
	sector_t sector;

	if (flush_rq->errors & ABRT_ERR) {
		printk(KERN_ERR "%s: barrier support doesn't work\n", drive->name);
		blk_queue_ordered(drive->queue, QUEUE_ORDERED_NONE);
		blk_queue_issue_flush_fn(drive->queue, NULL);
		good_sectors = 0;
	} else if (flush_rq->errors) {
		good_sectors = 0;
		if (blk_barrier_preflush(rq)) {
			sector = ide_get_error_location(drive,flush_rq->buffer);
			if ((sector >= rq->hard_sector) &&
			    (sector < rq->hard_sector + rq->hard_nr_sectors))
				good_sectors = sector - rq->hard_sector;
		}
	}

	if (flush_rq->errors)
		printk(KERN_ERR "%s: failed barrier write: "
				"sector=%Lx(good=%d/bad=%d)\n",
				drive->name, (unsigned long long)rq->sector,
				good_sectors,
				(int) (rq->hard_nr_sectors-good_sectors));

	bad_sectors = rq->hard_nr_sectors - good_sectors;

	if (good_sectors)
		__ide_end_request(drive, rq, 1, good_sectors);
	if (bad_sectors)
		__ide_end_request(drive, rq, 0, bad_sectors);
}

static int idedisk_prepare_flush(request_queue_t *q, struct request *rq)
{
	ide_drive_t *drive = q->queuedata;

	if (!drive->wcache)
		return 0;

	memset(rq->cmd, 0, sizeof(rq->cmd));

	if (ide_id_has_flush_cache_ext(drive->id) &&
	    (drive->capacity64 >= (1UL << 28)))
		rq->cmd[0] = WIN_FLUSH_CACHE_EXT;
	else
		rq->cmd[0] = WIN_FLUSH_CACHE;


	rq->flags |= REQ_DRIVE_TASK | REQ_SOFTBARRIER;
	rq->buffer = rq->cmd;
	return 1;
}

static int idedisk_issue_flush(request_queue_t *q, struct gendisk *disk,
			       sector_t *error_sector)
{
	ide_drive_t *drive = q->queuedata;
	struct request *rq;
	int ret;

	if (!drive->wcache)
		return 0;

	rq = blk_get_request(q, WRITE, __GFP_WAIT);

	idedisk_prepare_flush(q, rq);

	ret = blk_execute_rq(q, disk, rq, 0);

	/*
	 * if we failed and caller wants error offset, get it
	 */
	if (ret && error_sector)
		*error_sector = ide_get_error_location(drive, rq->cmd);

	blk_put_request(rq);
	return ret;
}

/*
 * This is tightly woven into the driver->do_special can not touch.
 * DON'T do it again until a total personality rewrite is committed.
 */
static int set_multcount(ide_drive_t *drive, int arg)
{
	struct request rq;

	if (drive->special.b.set_multmode)
		return -EBUSY;
	ide_init_drive_cmd (&rq);
	rq.flags = REQ_DRIVE_CMD;
	drive->mult_req = arg;
	drive->special.b.set_multmode = 1;
	(void) ide_do_drive_cmd (drive, &rq, ide_wait);
	return (drive->mult_count == arg) ? 0 : -EIO;
}

static int set_nowerr(ide_drive_t *drive, int arg)
{
	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->nowerr = arg;
	drive->bad_wstat = arg ? BAD_R_STAT : BAD_W_STAT;
	spin_unlock_irq(&ide_lock);
	return 0;
}

static int write_cache(ide_drive_t *drive, int arg)
{
	ide_task_t args;
	int err;

	if (!ide_id_has_flush_cache(drive->id))
		return 1;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= (arg) ?
			SETFEATURES_EN_WCACHE : SETFEATURES_DIS_WCACHE;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SETFEATURES;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;

	err = ide_raw_taskfile(drive, &args, NULL);
	if (err)
		return err;

	drive->wcache = arg;
	return 0;
}

static int do_idedisk_flushcache (ide_drive_t *drive)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	if (ide_id_has_flush_cache_ext(drive->id))
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_FLUSH_CACHE_EXT;
	else
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_FLUSH_CACHE;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	return ide_raw_taskfile(drive, &args, NULL);
}

static int set_acoustic (ide_drive_t *drive, int arg)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= (arg) ? SETFEATURES_EN_AAM :
							  SETFEATURES_DIS_AAM;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= arg;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SETFEATURES;
	args.command_type = IDE_DRIVE_TASK_NO_DATA;
	args.handler	  = &task_no_data_intr;
	ide_raw_taskfile(drive, &args, NULL);
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
	drive->addressing =  0;

	if (HWIF(drive)->no_lba48)
		return 0;

	if (!idedisk_supports_lba48(drive->id))
                return -EIO;
	drive->addressing = arg;
	return 0;
}

static void idedisk_add_settings(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	ide_add_setting(drive,	"bios_cyl",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	65535,				1,	1,	&drive->bios_cyl,		NULL);
	ide_add_setting(drive,	"bios_head",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	255,				1,	1,	&drive->bios_head,		NULL);
	ide_add_setting(drive,	"bios_sect",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	63,				1,	1,	&drive->bios_sect,		NULL);
	ide_add_setting(drive,	"address",		SETTING_RW,					HDIO_GET_ADDRESS,	HDIO_SET_ADDRESS,	TYPE_INTA,	0,	2,				1,	1,	&drive->addressing,	set_lba_addressing);
	ide_add_setting(drive,	"bswap",		SETTING_READ,					-1,			-1,			TYPE_BYTE,	0,	1,				1,	1,	&drive->bswap,			NULL);
	ide_add_setting(drive,	"multcount",		id ? SETTING_RW : SETTING_READ,			HDIO_GET_MULTCOUNT,	HDIO_SET_MULTCOUNT,	TYPE_BYTE,	0,	id ? id->max_multsect : 0,	1,	1,	&drive->mult_count,		set_multcount);
	ide_add_setting(drive,	"nowerr",		SETTING_RW,					HDIO_GET_NOWERR,	HDIO_SET_NOWERR,	TYPE_BYTE,	0,	1,				1,	1,	&drive->nowerr,			set_nowerr);
	ide_add_setting(drive,	"lun",			SETTING_RW,					-1,			-1,			TYPE_INT,	0,	7,				1,	1,	&drive->lun,			NULL);
	ide_add_setting(drive,	"wcache",		SETTING_RW,					HDIO_GET_WCACHE,	HDIO_SET_WCACHE,	TYPE_BYTE,	0,	1,				1,	1,	&drive->wcache,			write_cache);
	ide_add_setting(drive,	"acoustic",		SETTING_RW,					HDIO_GET_ACOUSTIC,	HDIO_SET_ACOUSTIC,	TYPE_BYTE,	0,	254,				1,	1,	&drive->acoustic,		set_acoustic);
 	ide_add_setting(drive,	"failures",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	65535,				1,	1,	&drive->failures,		NULL);
 	ide_add_setting(drive,	"max_failures",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	65535,				1,	1,	&drive->max_failures,		NULL);
}

static void idedisk_setup (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	unsigned long long capacity;
	int barrier;

	idedisk_add_settings(drive);

	if (drive->id_read == 0)
		return;

	/*
	 * CompactFlash cards and their brethern look just like hard drives
	 * to us, but they are removable and don't have a doorlock mechanism.
	 */
	if (drive->removable && !(drive->is_flash)) {
		/*
		 * Removable disks (eg. SYQUEST); ignore 'WD' drives 
		 */
		if (id->model[0] != 'W' || id->model[1] != 'D') {
			drive->doorlocking = 1;
		}
	}

	(void)set_lba_addressing(drive, 1);

	if (drive->addressing == 1) {
		ide_hwif_t *hwif = HWIF(drive);
		int max_s = 2048;

		if (max_s > hwif->rqsize)
			max_s = hwif->rqsize;

		blk_queue_max_sectors(drive->queue, max_s);
	}

	printk(KERN_INFO "%s: max request size: %dKiB\n", drive->name, drive->queue->max_sectors / 2);

	/* calculate drive capacity, and select LBA if possible */
	init_idedisk_capacity (drive);

	/* limit drive capacity to 137GB if LBA48 cannot be used */
	if (drive->addressing == 0 && drive->capacity64 > 1ULL << 28) {
		printk(KERN_WARNING "%s: cannot use LBA48 - full capacity "
		       "%llu sectors (%llu MB)\n",
		       drive->name, (unsigned long long)drive->capacity64,
		       sectors_to_MB(drive->capacity64));
		drive->capacity64 = 1ULL << 28;
	}

	if (drive->hwif->no_lba48_dma && drive->addressing) {
		if (drive->capacity64 > 1ULL << 28) {
			printk(KERN_INFO "%s: cannot use LBA48 DMA - PIO mode will"
					 " be used for accessing sectors > %u\n",
					 drive->name, 1 << 28);
		} else
			drive->addressing = 0;
	}

	/*
	 * if possible, give fdisk access to more of the drive,
	 * by correcting bios_cyls:
	 */
	capacity = idedisk_capacity (drive);
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
	if (id->buf_size)
		printk (" w/%dKiB Cache", id->buf_size/2);

	printk(", CHS=%d/%d/%d", 
	       drive->bios_cyl, drive->bios_head, drive->bios_sect);
	if (drive->using_dma)
		ide_dma_verbose(drive);
	printk("\n");

	drive->no_io_32bit = id->dword_io ? 1 : 0;

	/* write cache enabled? */
	if ((id->csfo & 1) || (id->cfs_enable_1 & (1 << 5)))
		drive->wcache = 1;

	write_cache(drive, 1);

	/*
	 * We must avoid issuing commands a drive does not understand
	 * or we may crash it. We check flush cache is supported. We also
	 * check we have the LBA48 flush cache if the drive capacity is
	 * too large. By this time we have trimmed the drive capacity if
	 * LBA48 is not available so we don't need to recheck that.
	 */
	barrier = 0;
	if (ide_id_has_flush_cache(id))
		barrier = 1;
	if (drive->addressing == 1) {
		/* Can't issue the correct flush ? */
		if (capacity > (1ULL << 28) && !ide_id_has_flush_cache_ext(id))
			barrier = 0;
	}

	printk(KERN_INFO "%s: cache flushes %ssupported\n",
		drive->name, barrier ? "" : "not ");
	if (barrier) {
		blk_queue_ordered(drive->queue, QUEUE_ORDERED_FLUSH);
		drive->queue->prepare_flush_fn = idedisk_prepare_flush;
		drive->queue->end_flush_fn = idedisk_end_flush;
		blk_queue_issue_flush_fn(drive->queue, idedisk_issue_flush);
	}
}

static void ide_cacheflush_p(ide_drive_t *drive)
{
	if (!drive->wcache || !ide_id_has_flush_cache(drive->id))
		return;

	if (do_idedisk_flushcache(drive))
		printk(KERN_INFO "%s: wcache flush failed!\n", drive->name);
}

static int ide_disk_remove(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	struct ide_disk_obj *idkp = drive->driver_data;
	struct gendisk *g = idkp->disk;

	ide_cacheflush_p(drive);

	ide_unregister_subdriver(drive, idkp->driver);

	del_gendisk(g);

	ide_disk_put(idkp);

	return 0;
}

static void ide_disk_release(struct kref *kref)
{
	struct ide_disk_obj *idkp = to_ide_disk(kref);
	ide_drive_t *drive = idkp->drive;
	struct gendisk *g = idkp->disk;

	drive->driver_data = NULL;
	drive->devfs_name[0] = '\0';
	g->private_data = NULL;
	put_disk(g);
	kfree(idkp);
}

static int ide_disk_probe(struct device *dev);

static void ide_device_shutdown(struct device *dev)
{
	ide_drive_t *drive = container_of(dev, ide_drive_t, gendev);

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

	printk("Shutdown: %s\n", drive->name);
	dev->bus->suspend(dev, PMSG_SUSPEND);
}

static ide_driver_t idedisk_driver = {
	.owner			= THIS_MODULE,
	.gen_driver = {
		.name		= "ide-disk",
		.bus		= &ide_bus_type,
		.probe		= ide_disk_probe,
		.remove		= ide_disk_remove,
		.shutdown	= ide_device_shutdown,
	},
	.version		= IDEDISK_VERSION,
	.media			= ide_disk,
	.supports_dsc_overlap	= 0,
	.do_request		= ide_do_rw_disk,
	.end_request		= ide_end_request,
	.error			= __ide_error,
	.abort			= __ide_abort,
	.proc			= idedisk_proc,
};

static int idedisk_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_disk_obj *idkp;
	ide_drive_t *drive;

	if (!(idkp = ide_disk_get(disk)))
		return -ENXIO;

	drive = idkp->drive;

	drive->usage++;
	if (drive->removable && drive->usage == 1) {
		ide_task_t args;
		memset(&args, 0, sizeof(ide_task_t));
		args.tfRegister[IDE_COMMAND_OFFSET] = WIN_DOORLOCK;
		args.command_type = IDE_DRIVE_TASK_NO_DATA;
		args.handler	  = &task_no_data_intr;
		check_disk_change(inode->i_bdev);
		/*
		 * Ignore the return code from door_lock,
		 * since the open() has already succeeded,
		 * and the door_lock is irrelevant at this point.
		 */
		if (drive->doorlocking && ide_raw_taskfile(drive, &args, NULL))
			drive->doorlocking = 0;
	}
	return 0;
}

static int idedisk_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_disk_obj *idkp = ide_disk_g(disk);
	ide_drive_t *drive = idkp->drive;

	if (drive->usage == 1)
		ide_cacheflush_p(drive);
	if (drive->removable && drive->usage == 1) {
		ide_task_t args;
		memset(&args, 0, sizeof(ide_task_t));
		args.tfRegister[IDE_COMMAND_OFFSET] = WIN_DOORUNLOCK;
		args.command_type = IDE_DRIVE_TASK_NO_DATA;
		args.handler	  = &task_no_data_intr;
		if (drive->doorlocking && ide_raw_taskfile(drive, &args, NULL))
			drive->doorlocking = 0;
	}
	drive->usage--;

	ide_disk_put(idkp);

	return 0;
}

static int idedisk_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct ide_disk_obj *idkp = ide_disk_g(bdev->bd_disk);
	return generic_ide_ioctl(idkp->drive, file, bdev, cmd, arg);
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
	.owner		= THIS_MODULE,
	.open		= idedisk_open,
	.release	= idedisk_release,
	.ioctl		= idedisk_ioctl,
	.media_changed	= idedisk_media_changed,
	.revalidate_disk= idedisk_revalidate_disk
};

MODULE_DESCRIPTION("ATA DISK Driver");

static int ide_disk_probe(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	struct ide_disk_obj *idkp;
	struct gendisk *g;

	/* strstr("foo", "") is non-NULL */
	if (!strstr("ide-disk", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_disk)
		goto failed;

	idkp = kmalloc(sizeof(*idkp), GFP_KERNEL);
	if (!idkp)
		goto failed;

	g = alloc_disk_node(1 << PARTN_BITS,
			hwif_to_node(drive->hwif));
	if (!g)
		goto out_free_idkp;

	ide_init_disk(g, drive);

	ide_register_subdriver(drive, &idedisk_driver);

	memset(idkp, 0, sizeof(*idkp));

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

	g->minors = 1 << PARTN_BITS;
	strcpy(g->devfs_name, drive->devfs_name);
	g->driverfs_dev = &drive->gendev;
	g->flags = drive->removable ? GENHD_FL_REMOVABLE : 0;
	set_capacity(g, idedisk_capacity(drive));
	g->fops = &idedisk_ops;
	add_disk(g);
	return 0;

out_free_idkp:
	kfree(idkp);
failed:
	return -ENODEV;
}

static void __exit idedisk_exit (void)
{
	driver_unregister(&idedisk_driver.gen_driver);
}

static int idedisk_init (void)
{
	return driver_register(&idedisk_driver.gen_driver);
}

module_init(idedisk_init);
module_exit(idedisk_exit);
MODULE_LICENSE("GPL");
