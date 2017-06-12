/*
 *      sd.c Copyright (C) 1992 Drew Eckhardt
 *           Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *      Linux scsi disk driver
 *              Initial versions: Drew Eckhardt
 *              Subsequent revisions: Eric Youngdale
 *	Modification history:
 *       - Drew Eckhardt <drew@colorado.edu> original
 *       - Eric Youngdale <eric@andante.org> add scatter-gather, multiple 
 *         outstanding request, and other enhancements.
 *         Support loadable low-level scsi drivers.
 *       - Jirka Hanika <geo@ff.cuni.cz> support more scsi disks using 
 *         eight major numbers.
 *       - Richard Gooch <rgooch@atnf.csiro.au> support devfs.
 *	 - Torben Mathiasen <tmm@image.dk> Resource allocation fixes in 
 *	   sd_init and cleanups.
 *	 - Alex Davis <letmein@erols.com> Fix problem where partition info
 *	   not being read in sd_open. Fix problem where removable media 
 *	   could be ejected after sd_open.
 *	 - Douglas Gilbert <dgilbert@interlog.com> cleanup for lk 2.5.x
 *	 - Badari Pulavarty <pbadari@us.ibm.com>, Matthew Wilcox 
 *	   <willy@debian.org>, Kurt Garloff <garloff@suse.de>: 
 *	   Support 32k/1M disks.
 *
 *	Logging policy (needs CONFIG_SCSI_LOGGING defined):
 *	 - setting up transfer: SCSI_LOG_HLQUEUE levels 1 and 2
 *	 - end of transfer (bh + scsi_lib): SCSI_LOG_HLCOMPLETE level 1
 *	 - entering sd_ioctl: SCSI_LOG_IOCTL level 1
 *	 - entering other commands: SCSI_LOG_HLQUEUE level 3
 *	Note: when the logging level is set by the user, it must be greater
 *	than the level indicated above to trigger output.	
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/string_helpers.h>
#include <linux/async.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pr.h>
#include <linux/t10-pi.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsicam.h>

#include "sd.h"
#include "scsi_priv.h"
#include "scsi_logging.h"

MODULE_AUTHOR("Eric Youngdale");
MODULE_DESCRIPTION("SCSI disk (sd) driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK0_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK1_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK2_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK3_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK4_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK5_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK6_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK7_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK8_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK9_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK10_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK11_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK12_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK13_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK14_MAJOR);
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_DISK15_MAJOR);
MODULE_ALIAS_SCSI_DEVICE(TYPE_DISK);
MODULE_ALIAS_SCSI_DEVICE(TYPE_MOD);
MODULE_ALIAS_SCSI_DEVICE(TYPE_RBC);
MODULE_ALIAS_SCSI_DEVICE(TYPE_ZBC);

#if !defined(CONFIG_DEBUG_BLOCK_EXT_DEVT)
#define SD_MINORS	16
#else
#define SD_MINORS	0
#endif

static void sd_config_discard(struct scsi_disk *, unsigned int);
static void sd_config_write_same(struct scsi_disk *);
static int  sd_revalidate_disk(struct gendisk *);
static void sd_unlock_native_capacity(struct gendisk *disk);
static int  sd_probe(struct device *);
static int  sd_remove(struct device *);
static void sd_shutdown(struct device *);
static int sd_suspend_system(struct device *);
static int sd_suspend_runtime(struct device *);
static int sd_resume(struct device *);
static void sd_rescan(struct device *);
static int sd_init_command(struct scsi_cmnd *SCpnt);
static void sd_uninit_command(struct scsi_cmnd *SCpnt);
static int sd_done(struct scsi_cmnd *);
static void sd_eh_reset(struct scsi_cmnd *);
static int sd_eh_action(struct scsi_cmnd *, int);
static void sd_read_capacity(struct scsi_disk *sdkp, unsigned char *buffer);
static void scsi_disk_release(struct device *cdev);
static void sd_print_sense_hdr(struct scsi_disk *, struct scsi_sense_hdr *);
static void sd_print_result(const struct scsi_disk *, const char *, int);

static DEFINE_SPINLOCK(sd_index_lock);
static DEFINE_IDA(sd_index_ida);

/* This semaphore is used to mediate the 0->1 reference get in the
 * face of object destruction (i.e. we can't allow a get on an
 * object after last put) */
static DEFINE_MUTEX(sd_ref_mutex);

static struct kmem_cache *sd_cdb_cache;
static mempool_t *sd_cdb_pool;

static const char *sd_cache_types[] = {
	"write through", "none", "write back",
	"write back, no read (daft)"
};

static void sd_set_flush_flag(struct scsi_disk *sdkp)
{
	bool wc = false, fua = false;

	if (sdkp->WCE) {
		wc = true;
		if (sdkp->DPOFUA)
			fua = true;
	}

	blk_queue_write_cache(sdkp->disk->queue, wc, fua);
}

static ssize_t
cache_type_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	int i, ct = -1, rcd, wce, sp;
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	char buffer[64];
	char *buffer_data;
	struct scsi_mode_data data;
	struct scsi_sense_hdr sshdr;
	static const char temp[] = "temporary ";
	int len;

	if (sdp->type != TYPE_DISK && sdp->type != TYPE_ZBC)
		/* no cache control on RBC devices; theoretically they
		 * can do it, but there's probably so many exceptions
		 * it's not worth the risk */
		return -EINVAL;

	if (strncmp(buf, temp, sizeof(temp) - 1) == 0) {
		buf += sizeof(temp) - 1;
		sdkp->cache_override = 1;
	} else {
		sdkp->cache_override = 0;
	}

	for (i = 0; i < ARRAY_SIZE(sd_cache_types); i++) {
		len = strlen(sd_cache_types[i]);
		if (strncmp(sd_cache_types[i], buf, len) == 0 &&
		    buf[len] == '\n') {
			ct = i;
			break;
		}
	}
	if (ct < 0)
		return -EINVAL;
	rcd = ct & 0x01 ? 1 : 0;
	wce = (ct & 0x02) && !sdkp->write_prot ? 1 : 0;

	if (sdkp->cache_override) {
		sdkp->WCE = wce;
		sdkp->RCD = rcd;
		sd_set_flush_flag(sdkp);
		return count;
	}

	if (scsi_mode_sense(sdp, 0x08, 8, buffer, sizeof(buffer), SD_TIMEOUT,
			    SD_MAX_RETRIES, &data, NULL))
		return -EINVAL;
	len = min_t(size_t, sizeof(buffer), data.length - data.header_length -
		  data.block_descriptor_length);
	buffer_data = buffer + data.header_length +
		data.block_descriptor_length;
	buffer_data[2] &= ~0x05;
	buffer_data[2] |= wce << 2 | rcd;
	sp = buffer_data[0] & 0x80 ? 1 : 0;
	buffer_data[0] &= ~0x80;

	if (scsi_mode_select(sdp, 1, sp, 8, buffer_data, len, SD_TIMEOUT,
			     SD_MAX_RETRIES, &data, &sshdr)) {
		if (scsi_sense_valid(&sshdr))
			sd_print_sense_hdr(sdkp, &sshdr);
		return -EINVAL;
	}
	revalidate_disk(sdkp->disk);
	return count;
}

static ssize_t
manage_start_stop_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	return snprintf(buf, 20, "%u\n", sdp->manage_start_stop);
}

static ssize_t
manage_start_stop_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	sdp->manage_start_stop = simple_strtoul(buf, NULL, 10);

	return count;
}
static DEVICE_ATTR_RW(manage_start_stop);

static ssize_t
allow_restart_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 40, "%d\n", sdkp->device->allow_restart);
}

static ssize_t
allow_restart_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (sdp->type != TYPE_DISK && sdp->type != TYPE_ZBC)
		return -EINVAL;

	sdp->allow_restart = simple_strtoul(buf, NULL, 10);

	return count;
}
static DEVICE_ATTR_RW(allow_restart);

static ssize_t
cache_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	int ct = sdkp->RCD + 2*sdkp->WCE;

	return snprintf(buf, 40, "%s\n", sd_cache_types[ct]);
}
static DEVICE_ATTR_RW(cache_type);

static ssize_t
FUA_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%u\n", sdkp->DPOFUA);
}
static DEVICE_ATTR_RO(FUA);

static ssize_t
protection_type_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%u\n", sdkp->protection_type);
}

static ssize_t
protection_type_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	unsigned int val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	err = kstrtouint(buf, 10, &val);

	if (err)
		return err;

	if (val >= 0 && val <= T10_PI_TYPE3_PROTECTION)
		sdkp->protection_type = val;

	return count;
}
static DEVICE_ATTR_RW(protection_type);

static ssize_t
protection_mode_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	unsigned int dif, dix;

	dif = scsi_host_dif_capable(sdp->host, sdkp->protection_type);
	dix = scsi_host_dix_capable(sdp->host, sdkp->protection_type);

	if (!dix && scsi_host_dix_capable(sdp->host, T10_PI_TYPE0_PROTECTION)) {
		dif = 0;
		dix = 1;
	}

	if (!dif && !dix)
		return snprintf(buf, 20, "none\n");

	return snprintf(buf, 20, "%s%u\n", dix ? "dix" : "dif", dif);
}
static DEVICE_ATTR_RO(protection_mode);

static ssize_t
app_tag_own_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%u\n", sdkp->ATO);
}
static DEVICE_ATTR_RO(app_tag_own);

static ssize_t
thin_provisioning_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%u\n", sdkp->lbpme);
}
static DEVICE_ATTR_RO(thin_provisioning);

static const char *lbp_mode[] = {
	[SD_LBP_FULL]		= "full",
	[SD_LBP_UNMAP]		= "unmap",
	[SD_LBP_WS16]		= "writesame_16",
	[SD_LBP_WS10]		= "writesame_10",
	[SD_LBP_ZERO]		= "writesame_zero",
	[SD_LBP_DISABLE]	= "disabled",
};

static ssize_t
provisioning_mode_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%s\n", lbp_mode[sdkp->provisioning_mode]);
}

static ssize_t
provisioning_mode_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (sd_is_zoned(sdkp)) {
		sd_config_discard(sdkp, SD_LBP_DISABLE);
		return count;
	}

	if (sdp->type != TYPE_DISK)
		return -EINVAL;

	if (!strncmp(buf, lbp_mode[SD_LBP_UNMAP], 20))
		sd_config_discard(sdkp, SD_LBP_UNMAP);
	else if (!strncmp(buf, lbp_mode[SD_LBP_WS16], 20))
		sd_config_discard(sdkp, SD_LBP_WS16);
	else if (!strncmp(buf, lbp_mode[SD_LBP_WS10], 20))
		sd_config_discard(sdkp, SD_LBP_WS10);
	else if (!strncmp(buf, lbp_mode[SD_LBP_ZERO], 20))
		sd_config_discard(sdkp, SD_LBP_ZERO);
	else if (!strncmp(buf, lbp_mode[SD_LBP_DISABLE], 20))
		sd_config_discard(sdkp, SD_LBP_DISABLE);
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(provisioning_mode);

static const char *zeroing_mode[] = {
	[SD_ZERO_WRITE]		= "write",
	[SD_ZERO_WS]		= "writesame",
	[SD_ZERO_WS16_UNMAP]	= "writesame_16_unmap",
	[SD_ZERO_WS10_UNMAP]	= "writesame_10_unmap",
};

static ssize_t
zeroing_mode_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%s\n", zeroing_mode[sdkp->zeroing_mode]);
}

static ssize_t
zeroing_mode_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!strncmp(buf, zeroing_mode[SD_ZERO_WRITE], 20))
		sdkp->zeroing_mode = SD_ZERO_WRITE;
	else if (!strncmp(buf, zeroing_mode[SD_ZERO_WS], 20))
		sdkp->zeroing_mode = SD_ZERO_WS;
	else if (!strncmp(buf, zeroing_mode[SD_ZERO_WS16_UNMAP], 20))
		sdkp->zeroing_mode = SD_ZERO_WS16_UNMAP;
	else if (!strncmp(buf, zeroing_mode[SD_ZERO_WS10_UNMAP], 20))
		sdkp->zeroing_mode = SD_ZERO_WS10_UNMAP;
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(zeroing_mode);

static ssize_t
max_medium_access_timeouts_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%u\n", sdkp->max_medium_access_timeouts);
}

static ssize_t
max_medium_access_timeouts_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	err = kstrtouint(buf, 10, &sdkp->max_medium_access_timeouts);

	return err ? err : count;
}
static DEVICE_ATTR_RW(max_medium_access_timeouts);

static ssize_t
max_write_same_blocks_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return snprintf(buf, 20, "%u\n", sdkp->max_ws_blocks);
}

static ssize_t
max_write_same_blocks_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	unsigned long max;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (sdp->type != TYPE_DISK && sdp->type != TYPE_ZBC)
		return -EINVAL;

	err = kstrtoul(buf, 10, &max);

	if (err)
		return err;

	if (max == 0)
		sdp->no_write_same = 1;
	else if (max <= SD_MAX_WS16_BLOCKS) {
		sdp->no_write_same = 0;
		sdkp->max_ws_blocks = max;
	}

	sd_config_write_same(sdkp);

	return count;
}
static DEVICE_ATTR_RW(max_write_same_blocks);

static struct attribute *sd_disk_attrs[] = {
	&dev_attr_cache_type.attr,
	&dev_attr_FUA.attr,
	&dev_attr_allow_restart.attr,
	&dev_attr_manage_start_stop.attr,
	&dev_attr_protection_type.attr,
	&dev_attr_protection_mode.attr,
	&dev_attr_app_tag_own.attr,
	&dev_attr_thin_provisioning.attr,
	&dev_attr_provisioning_mode.attr,
	&dev_attr_zeroing_mode.attr,
	&dev_attr_max_write_same_blocks.attr,
	&dev_attr_max_medium_access_timeouts.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sd_disk);

static struct class sd_disk_class = {
	.name		= "scsi_disk",
	.owner		= THIS_MODULE,
	.dev_release	= scsi_disk_release,
	.dev_groups	= sd_disk_groups,
};

static const struct dev_pm_ops sd_pm_ops = {
	.suspend		= sd_suspend_system,
	.resume			= sd_resume,
	.poweroff		= sd_suspend_system,
	.restore		= sd_resume,
	.runtime_suspend	= sd_suspend_runtime,
	.runtime_resume		= sd_resume,
};

static struct scsi_driver sd_template = {
	.gendrv = {
		.name		= "sd",
		.owner		= THIS_MODULE,
		.probe		= sd_probe,
		.remove		= sd_remove,
		.shutdown	= sd_shutdown,
		.pm		= &sd_pm_ops,
	},
	.rescan			= sd_rescan,
	.init_command		= sd_init_command,
	.uninit_command		= sd_uninit_command,
	.done			= sd_done,
	.eh_action		= sd_eh_action,
	.eh_reset		= sd_eh_reset,
};

/*
 * Dummy kobj_map->probe function.
 * The default ->probe function will call modprobe, which is
 * pointless as this module is already loaded.
 */
static struct kobject *sd_default_probe(dev_t devt, int *partno, void *data)
{
	return NULL;
}

/*
 * Device no to disk mapping:
 * 
 *       major         disc2     disc  p1
 *   |............|.............|....|....| <- dev_t
 *    31        20 19          8 7  4 3  0
 * 
 * Inside a major, we have 16k disks, however mapped non-
 * contiguously. The first 16 disks are for major0, the next
 * ones with major1, ... Disk 256 is for major0 again, disk 272 
 * for major1, ... 
 * As we stay compatible with our numbering scheme, we can reuse 
 * the well-know SCSI majors 8, 65--71, 136--143.
 */
static int sd_major(int major_idx)
{
	switch (major_idx) {
	case 0:
		return SCSI_DISK0_MAJOR;
	case 1 ... 7:
		return SCSI_DISK1_MAJOR + major_idx - 1;
	case 8 ... 15:
		return SCSI_DISK8_MAJOR + major_idx - 8;
	default:
		BUG();
		return 0;	/* shut up gcc */
	}
}

static struct scsi_disk *scsi_disk_get(struct gendisk *disk)
{
	struct scsi_disk *sdkp = NULL;

	mutex_lock(&sd_ref_mutex);

	if (disk->private_data) {
		sdkp = scsi_disk(disk);
		if (scsi_device_get(sdkp->device) == 0)
			get_device(&sdkp->dev);
		else
			sdkp = NULL;
	}
	mutex_unlock(&sd_ref_mutex);
	return sdkp;
}

static void scsi_disk_put(struct scsi_disk *sdkp)
{
	struct scsi_device *sdev = sdkp->device;

	mutex_lock(&sd_ref_mutex);
	put_device(&sdkp->dev);
	scsi_device_put(sdev);
	mutex_unlock(&sd_ref_mutex);
}

static unsigned char sd_setup_protect_cmnd(struct scsi_cmnd *scmd,
					   unsigned int dix, unsigned int dif)
{
	struct bio *bio = scmd->request->bio;
	unsigned int prot_op = sd_prot_op(rq_data_dir(scmd->request), dix, dif);
	unsigned int protect = 0;

	if (dix) {				/* DIX Type 0, 1, 2, 3 */
		if (bio_integrity_flagged(bio, BIP_IP_CHECKSUM))
			scmd->prot_flags |= SCSI_PROT_IP_CHECKSUM;

		if (bio_integrity_flagged(bio, BIP_CTRL_NOCHECK) == false)
			scmd->prot_flags |= SCSI_PROT_GUARD_CHECK;
	}

	if (dif != T10_PI_TYPE3_PROTECTION) {	/* DIX/DIF Type 0, 1, 2 */
		scmd->prot_flags |= SCSI_PROT_REF_INCREMENT;

		if (bio_integrity_flagged(bio, BIP_CTRL_NOCHECK) == false)
			scmd->prot_flags |= SCSI_PROT_REF_CHECK;
	}

	if (dif) {				/* DIX/DIF Type 1, 2, 3 */
		scmd->prot_flags |= SCSI_PROT_TRANSFER_PI;

		if (bio_integrity_flagged(bio, BIP_DISK_NOCHECK))
			protect = 3 << 5;	/* Disable target PI checking */
		else
			protect = 1 << 5;	/* Enable target PI checking */
	}

	scsi_set_prot_op(scmd, prot_op);
	scsi_set_prot_type(scmd, dif);
	scmd->prot_flags &= sd_prot_flag_mask(prot_op);

	return protect;
}

static void sd_config_discard(struct scsi_disk *sdkp, unsigned int mode)
{
	struct request_queue *q = sdkp->disk->queue;
	unsigned int logical_block_size = sdkp->device->sector_size;
	unsigned int max_blocks = 0;

	q->limits.discard_alignment =
		sdkp->unmap_alignment * logical_block_size;
	q->limits.discard_granularity =
		max(sdkp->physical_block_size,
		    sdkp->unmap_granularity * logical_block_size);
	sdkp->provisioning_mode = mode;

	switch (mode) {

	case SD_LBP_DISABLE:
		blk_queue_max_discard_sectors(q, 0);
		queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD, q);
		return;

	case SD_LBP_UNMAP:
		max_blocks = min_not_zero(sdkp->max_unmap_blocks,
					  (u32)SD_MAX_WS16_BLOCKS);
		break;

	case SD_LBP_WS16:
		max_blocks = min_not_zero(sdkp->max_ws_blocks,
					  (u32)SD_MAX_WS16_BLOCKS);
		break;

	case SD_LBP_WS10:
		max_blocks = min_not_zero(sdkp->max_ws_blocks,
					  (u32)SD_MAX_WS10_BLOCKS);
		break;

	case SD_LBP_ZERO:
		max_blocks = min_not_zero(sdkp->max_ws_blocks,
					  (u32)SD_MAX_WS10_BLOCKS);
		break;
	}

	blk_queue_max_discard_sectors(q, max_blocks * (logical_block_size >> 9));
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, q);
}

static int sd_setup_unmap_cmnd(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdp = cmd->device;
	struct request *rq = cmd->request;
	u64 sector = blk_rq_pos(rq) >> (ilog2(sdp->sector_size) - 9);
	u32 nr_sectors = blk_rq_sectors(rq) >> (ilog2(sdp->sector_size) - 9);
	unsigned int data_len = 24;
	char *buf;

	rq->special_vec.bv_page = alloc_page(GFP_ATOMIC | __GFP_ZERO);
	if (!rq->special_vec.bv_page)
		return BLKPREP_DEFER;
	rq->special_vec.bv_offset = 0;
	rq->special_vec.bv_len = data_len;
	rq->rq_flags |= RQF_SPECIAL_PAYLOAD;

	cmd->cmd_len = 10;
	cmd->cmnd[0] = UNMAP;
	cmd->cmnd[8] = 24;

	buf = page_address(rq->special_vec.bv_page);
	put_unaligned_be16(6 + 16, &buf[0]);
	put_unaligned_be16(16, &buf[2]);
	put_unaligned_be64(sector, &buf[8]);
	put_unaligned_be32(nr_sectors, &buf[16]);

	cmd->allowed = SD_MAX_RETRIES;
	cmd->transfersize = data_len;
	rq->timeout = SD_TIMEOUT;
	scsi_req(rq)->resid_len = data_len;

	return scsi_init_io(cmd);
}

static int sd_setup_write_same16_cmnd(struct scsi_cmnd *cmd, bool unmap)
{
	struct scsi_device *sdp = cmd->device;
	struct request *rq = cmd->request;
	u64 sector = blk_rq_pos(rq) >> (ilog2(sdp->sector_size) - 9);
	u32 nr_sectors = blk_rq_sectors(rq) >> (ilog2(sdp->sector_size) - 9);
	u32 data_len = sdp->sector_size;

	rq->special_vec.bv_page = alloc_page(GFP_ATOMIC | __GFP_ZERO);
	if (!rq->special_vec.bv_page)
		return BLKPREP_DEFER;
	rq->special_vec.bv_offset = 0;
	rq->special_vec.bv_len = data_len;
	rq->rq_flags |= RQF_SPECIAL_PAYLOAD;

	cmd->cmd_len = 16;
	cmd->cmnd[0] = WRITE_SAME_16;
	if (unmap)
		cmd->cmnd[1] = 0x8; /* UNMAP */
	put_unaligned_be64(sector, &cmd->cmnd[2]);
	put_unaligned_be32(nr_sectors, &cmd->cmnd[10]);

	cmd->allowed = SD_MAX_RETRIES;
	cmd->transfersize = data_len;
	rq->timeout = unmap ? SD_TIMEOUT : SD_WRITE_SAME_TIMEOUT;
	scsi_req(rq)->resid_len = data_len;

	return scsi_init_io(cmd);
}

static int sd_setup_write_same10_cmnd(struct scsi_cmnd *cmd, bool unmap)
{
	struct scsi_device *sdp = cmd->device;
	struct request *rq = cmd->request;
	u64 sector = blk_rq_pos(rq) >> (ilog2(sdp->sector_size) - 9);
	u32 nr_sectors = blk_rq_sectors(rq) >> (ilog2(sdp->sector_size) - 9);
	u32 data_len = sdp->sector_size;

	rq->special_vec.bv_page = alloc_page(GFP_ATOMIC | __GFP_ZERO);
	if (!rq->special_vec.bv_page)
		return BLKPREP_DEFER;
	rq->special_vec.bv_offset = 0;
	rq->special_vec.bv_len = data_len;
	rq->rq_flags |= RQF_SPECIAL_PAYLOAD;

	cmd->cmd_len = 10;
	cmd->cmnd[0] = WRITE_SAME;
	if (unmap)
		cmd->cmnd[1] = 0x8; /* UNMAP */
	put_unaligned_be32(sector, &cmd->cmnd[2]);
	put_unaligned_be16(nr_sectors, &cmd->cmnd[7]);

	cmd->allowed = SD_MAX_RETRIES;
	cmd->transfersize = data_len;
	rq->timeout = unmap ? SD_TIMEOUT : SD_WRITE_SAME_TIMEOUT;
	scsi_req(rq)->resid_len = data_len;

	return scsi_init_io(cmd);
}

static int sd_setup_write_zeroes_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;
	struct scsi_device *sdp = cmd->device;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	u64 sector = blk_rq_pos(rq) >> (ilog2(sdp->sector_size) - 9);
	u32 nr_sectors = blk_rq_sectors(rq) >> (ilog2(sdp->sector_size) - 9);
	int ret;

	if (!(rq->cmd_flags & REQ_NOUNMAP)) {
		switch (sdkp->zeroing_mode) {
		case SD_ZERO_WS16_UNMAP:
			ret = sd_setup_write_same16_cmnd(cmd, true);
			goto out;
		case SD_ZERO_WS10_UNMAP:
			ret = sd_setup_write_same10_cmnd(cmd, true);
			goto out;
		}
	}

	if (sdp->no_write_same)
		return BLKPREP_INVALID;

	if (sdkp->ws16 || sector > 0xffffffff || nr_sectors > 0xffff)
		ret = sd_setup_write_same16_cmnd(cmd, false);
	else
		ret = sd_setup_write_same10_cmnd(cmd, false);

out:
	if (sd_is_zoned(sdkp) && ret == BLKPREP_OK)
		return sd_zbc_write_lock_zone(cmd);

	return ret;
}

static void sd_config_write_same(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;
	unsigned int logical_block_size = sdkp->device->sector_size;

	if (sdkp->device->no_write_same) {
		sdkp->max_ws_blocks = 0;
		goto out;
	}

	/* Some devices can not handle block counts above 0xffff despite
	 * supporting WRITE SAME(16). Consequently we default to 64k
	 * blocks per I/O unless the device explicitly advertises a
	 * bigger limit.
	 */
	if (sdkp->max_ws_blocks > SD_MAX_WS10_BLOCKS)
		sdkp->max_ws_blocks = min_not_zero(sdkp->max_ws_blocks,
						   (u32)SD_MAX_WS16_BLOCKS);
	else if (sdkp->ws16 || sdkp->ws10 || sdkp->device->no_report_opcodes)
		sdkp->max_ws_blocks = min_not_zero(sdkp->max_ws_blocks,
						   (u32)SD_MAX_WS10_BLOCKS);
	else {
		sdkp->device->no_write_same = 1;
		sdkp->max_ws_blocks = 0;
	}

	if (sdkp->lbprz && sdkp->lbpws)
		sdkp->zeroing_mode = SD_ZERO_WS16_UNMAP;
	else if (sdkp->lbprz && sdkp->lbpws10)
		sdkp->zeroing_mode = SD_ZERO_WS10_UNMAP;
	else if (sdkp->max_ws_blocks)
		sdkp->zeroing_mode = SD_ZERO_WS;
	else
		sdkp->zeroing_mode = SD_ZERO_WRITE;

out:
	blk_queue_max_write_same_sectors(q, sdkp->max_ws_blocks *
					 (logical_block_size >> 9));
	blk_queue_max_write_zeroes_sectors(q, sdkp->max_ws_blocks *
					 (logical_block_size >> 9));
}

/**
 * sd_setup_write_same_cmnd - write the same data to multiple blocks
 * @cmd: command to prepare
 *
 * Will set up either WRITE SAME(10) or WRITE SAME(16) depending on
 * the preference indicated by the target device.
 **/
static int sd_setup_write_same_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;
	struct scsi_device *sdp = cmd->device;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	struct bio *bio = rq->bio;
	sector_t sector = blk_rq_pos(rq);
	unsigned int nr_sectors = blk_rq_sectors(rq);
	unsigned int nr_bytes = blk_rq_bytes(rq);
	int ret;

	if (sdkp->device->no_write_same)
		return BLKPREP_INVALID;

	BUG_ON(bio_offset(bio) || bio_iovec(bio).bv_len != sdp->sector_size);

	if (sd_is_zoned(sdkp)) {
		ret = sd_zbc_write_lock_zone(cmd);
		if (ret != BLKPREP_OK)
			return ret;
	}

	sector >>= ilog2(sdp->sector_size) - 9;
	nr_sectors >>= ilog2(sdp->sector_size) - 9;

	rq->timeout = SD_WRITE_SAME_TIMEOUT;

	if (sdkp->ws16 || sector > 0xffffffff || nr_sectors > 0xffff) {
		cmd->cmd_len = 16;
		cmd->cmnd[0] = WRITE_SAME_16;
		put_unaligned_be64(sector, &cmd->cmnd[2]);
		put_unaligned_be32(nr_sectors, &cmd->cmnd[10]);
	} else {
		cmd->cmd_len = 10;
		cmd->cmnd[0] = WRITE_SAME;
		put_unaligned_be32(sector, &cmd->cmnd[2]);
		put_unaligned_be16(nr_sectors, &cmd->cmnd[7]);
	}

	cmd->transfersize = sdp->sector_size;
	cmd->allowed = SD_MAX_RETRIES;

	/*
	 * For WRITE SAME the data transferred via the DATA OUT buffer is
	 * different from the amount of data actually written to the target.
	 *
	 * We set up __data_len to the amount of data transferred via the
	 * DATA OUT buffer so that blk_rq_map_sg sets up the proper S/G list
	 * to transfer a single sector of data first, but then reset it to
	 * the amount of data to be written right after so that the I/O path
	 * knows how much to actually write.
	 */
	rq->__data_len = sdp->sector_size;
	ret = scsi_init_io(cmd);
	rq->__data_len = nr_bytes;

	if (sd_is_zoned(sdkp) && ret != BLKPREP_OK)
		sd_zbc_write_unlock_zone(cmd);

	return ret;
}

static int sd_setup_flush_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;

	/* flush requests don't perform I/O, zero the S/G table */
	memset(&cmd->sdb, 0, sizeof(cmd->sdb));

	cmd->cmnd[0] = SYNCHRONIZE_CACHE;
	cmd->cmd_len = 10;
	cmd->transfersize = 0;
	cmd->allowed = SD_MAX_RETRIES;

	rq->timeout = rq->q->rq_timeout * SD_FLUSH_TIMEOUT_MULTIPLIER;
	return BLKPREP_OK;
}

static int sd_setup_read_write_cmnd(struct scsi_cmnd *SCpnt)
{
	struct request *rq = SCpnt->request;
	struct scsi_device *sdp = SCpnt->device;
	struct gendisk *disk = rq->rq_disk;
	struct scsi_disk *sdkp = scsi_disk(disk);
	sector_t block = blk_rq_pos(rq);
	sector_t threshold;
	unsigned int this_count = blk_rq_sectors(rq);
	unsigned int dif, dix;
	bool zoned_write = sd_is_zoned(sdkp) && rq_data_dir(rq) == WRITE;
	int ret;
	unsigned char protect;

	if (zoned_write) {
		ret = sd_zbc_write_lock_zone(SCpnt);
		if (ret != BLKPREP_OK)
			return ret;
	}

	ret = scsi_init_io(SCpnt);
	if (ret != BLKPREP_OK)
		goto out;
	SCpnt = rq->special;

	/* from here on until we're complete, any goto out
	 * is used for a killable error condition */
	ret = BLKPREP_KILL;

	SCSI_LOG_HLQUEUE(1,
		scmd_printk(KERN_INFO, SCpnt,
			"%s: block=%llu, count=%d\n",
			__func__, (unsigned long long)block, this_count));

	if (!sdp || !scsi_device_online(sdp) ||
	    block + blk_rq_sectors(rq) > get_capacity(disk)) {
		SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt,
						"Finishing %u sectors\n",
						blk_rq_sectors(rq)));
		SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt,
						"Retry with 0x%p\n", SCpnt));
		goto out;
	}

	if (sdp->changed) {
		/*
		 * quietly refuse to do anything to a changed disc until 
		 * the changed bit has been reset
		 */
		/* printk("SCSI disk has been changed or is not present. Prohibiting further I/O.\n"); */
		goto out;
	}

	/*
	 * Some SD card readers can't handle multi-sector accesses which touch
	 * the last one or two hardware sectors.  Split accesses as needed.
	 */
	threshold = get_capacity(disk) - SD_LAST_BUGGY_SECTORS *
		(sdp->sector_size / 512);

	if (unlikely(sdp->last_sector_bug && block + this_count > threshold)) {
		if (block < threshold) {
			/* Access up to the threshold but not beyond */
			this_count = threshold - block;
		} else {
			/* Access only a single hardware sector */
			this_count = sdp->sector_size / 512;
		}
	}

	SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt, "block=%llu\n",
					(unsigned long long)block));

	/*
	 * If we have a 1K hardware sectorsize, prevent access to single
	 * 512 byte sectors.  In theory we could handle this - in fact
	 * the scsi cdrom driver must be able to handle this because
	 * we typically use 1K blocksizes, and cdroms typically have
	 * 2K hardware sectorsizes.  Of course, things are simpler
	 * with the cdrom, since it is read-only.  For performance
	 * reasons, the filesystems should be able to handle this
	 * and not force the scsi disk driver to use bounce buffers
	 * for this.
	 */
	if (sdp->sector_size == 1024) {
		if ((block & 1) || (blk_rq_sectors(rq) & 1)) {
			scmd_printk(KERN_ERR, SCpnt,
				    "Bad block number requested\n");
			goto out;
		} else {
			block = block >> 1;
			this_count = this_count >> 1;
		}
	}
	if (sdp->sector_size == 2048) {
		if ((block & 3) || (blk_rq_sectors(rq) & 3)) {
			scmd_printk(KERN_ERR, SCpnt,
				    "Bad block number requested\n");
			goto out;
		} else {
			block = block >> 2;
			this_count = this_count >> 2;
		}
	}
	if (sdp->sector_size == 4096) {
		if ((block & 7) || (blk_rq_sectors(rq) & 7)) {
			scmd_printk(KERN_ERR, SCpnt,
				    "Bad block number requested\n");
			goto out;
		} else {
			block = block >> 3;
			this_count = this_count >> 3;
		}
	}
	if (rq_data_dir(rq) == WRITE) {
		SCpnt->cmnd[0] = WRITE_6;

		if (blk_integrity_rq(rq))
			sd_dif_prepare(SCpnt);

	} else if (rq_data_dir(rq) == READ) {
		SCpnt->cmnd[0] = READ_6;
	} else {
		scmd_printk(KERN_ERR, SCpnt, "Unknown command %d\n", req_op(rq));
		goto out;
	}

	SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt,
					"%s %d/%u 512 byte blocks.\n",
					(rq_data_dir(rq) == WRITE) ?
					"writing" : "reading", this_count,
					blk_rq_sectors(rq)));

	dix = scsi_prot_sg_count(SCpnt);
	dif = scsi_host_dif_capable(SCpnt->device->host, sdkp->protection_type);

	if (dif || dix)
		protect = sd_setup_protect_cmnd(SCpnt, dix, dif);
	else
		protect = 0;

	if (protect && sdkp->protection_type == T10_PI_TYPE2_PROTECTION) {
		SCpnt->cmnd = mempool_alloc(sd_cdb_pool, GFP_ATOMIC);

		if (unlikely(SCpnt->cmnd == NULL)) {
			ret = BLKPREP_DEFER;
			goto out;
		}

		SCpnt->cmd_len = SD_EXT_CDB_SIZE;
		memset(SCpnt->cmnd, 0, SCpnt->cmd_len);
		SCpnt->cmnd[0] = VARIABLE_LENGTH_CMD;
		SCpnt->cmnd[7] = 0x18;
		SCpnt->cmnd[9] = (rq_data_dir(rq) == READ) ? READ_32 : WRITE_32;
		SCpnt->cmnd[10] = protect | ((rq->cmd_flags & REQ_FUA) ? 0x8 : 0);

		/* LBA */
		SCpnt->cmnd[12] = sizeof(block) > 4 ? (unsigned char) (block >> 56) & 0xff : 0;
		SCpnt->cmnd[13] = sizeof(block) > 4 ? (unsigned char) (block >> 48) & 0xff : 0;
		SCpnt->cmnd[14] = sizeof(block) > 4 ? (unsigned char) (block >> 40) & 0xff : 0;
		SCpnt->cmnd[15] = sizeof(block) > 4 ? (unsigned char) (block >> 32) & 0xff : 0;
		SCpnt->cmnd[16] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[17] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[18] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[19] = (unsigned char) block & 0xff;

		/* Expected Indirect LBA */
		SCpnt->cmnd[20] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[21] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[22] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[23] = (unsigned char) block & 0xff;

		/* Transfer length */
		SCpnt->cmnd[28] = (unsigned char) (this_count >> 24) & 0xff;
		SCpnt->cmnd[29] = (unsigned char) (this_count >> 16) & 0xff;
		SCpnt->cmnd[30] = (unsigned char) (this_count >> 8) & 0xff;
		SCpnt->cmnd[31] = (unsigned char) this_count & 0xff;
	} else if (sdp->use_16_for_rw || (this_count > 0xffff)) {
		SCpnt->cmnd[0] += READ_16 - READ_6;
		SCpnt->cmnd[1] = protect | ((rq->cmd_flags & REQ_FUA) ? 0x8 : 0);
		SCpnt->cmnd[2] = sizeof(block) > 4 ? (unsigned char) (block >> 56) & 0xff : 0;
		SCpnt->cmnd[3] = sizeof(block) > 4 ? (unsigned char) (block >> 48) & 0xff : 0;
		SCpnt->cmnd[4] = sizeof(block) > 4 ? (unsigned char) (block >> 40) & 0xff : 0;
		SCpnt->cmnd[5] = sizeof(block) > 4 ? (unsigned char) (block >> 32) & 0xff : 0;
		SCpnt->cmnd[6] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[7] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[8] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[9] = (unsigned char) block & 0xff;
		SCpnt->cmnd[10] = (unsigned char) (this_count >> 24) & 0xff;
		SCpnt->cmnd[11] = (unsigned char) (this_count >> 16) & 0xff;
		SCpnt->cmnd[12] = (unsigned char) (this_count >> 8) & 0xff;
		SCpnt->cmnd[13] = (unsigned char) this_count & 0xff;
		SCpnt->cmnd[14] = SCpnt->cmnd[15] = 0;
	} else if ((this_count > 0xff) || (block > 0x1fffff) ||
		   scsi_device_protection(SCpnt->device) ||
		   SCpnt->device->use_10_for_rw) {
		SCpnt->cmnd[0] += READ_10 - READ_6;
		SCpnt->cmnd[1] = protect | ((rq->cmd_flags & REQ_FUA) ? 0x8 : 0);
		SCpnt->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[5] = (unsigned char) block & 0xff;
		SCpnt->cmnd[6] = SCpnt->cmnd[9] = 0;
		SCpnt->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
		SCpnt->cmnd[8] = (unsigned char) this_count & 0xff;
	} else {
		if (unlikely(rq->cmd_flags & REQ_FUA)) {
			/*
			 * This happens only if this drive failed
			 * 10byte rw command with ILLEGAL_REQUEST
			 * during operation and thus turned off
			 * use_10_for_rw.
			 */
			scmd_printk(KERN_ERR, SCpnt,
				    "FUA write on READ/WRITE(6) drive\n");
			goto out;
		}

		SCpnt->cmnd[1] |= (unsigned char) ((block >> 16) & 0x1f);
		SCpnt->cmnd[2] = (unsigned char) ((block >> 8) & 0xff);
		SCpnt->cmnd[3] = (unsigned char) block & 0xff;
		SCpnt->cmnd[4] = (unsigned char) this_count;
		SCpnt->cmnd[5] = 0;
	}
	SCpnt->sdb.length = this_count * sdp->sector_size;

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	SCpnt->transfersize = sdp->sector_size;
	SCpnt->underflow = this_count << 9;
	SCpnt->allowed = SD_MAX_RETRIES;

	/*
	 * This indicates that the command is ready from our end to be
	 * queued.
	 */
	ret = BLKPREP_OK;
 out:
	if (zoned_write && ret != BLKPREP_OK)
		sd_zbc_write_unlock_zone(SCpnt);

	return ret;
}

static int sd_init_command(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;

	switch (req_op(rq)) {
	case REQ_OP_DISCARD:
		switch (scsi_disk(rq->rq_disk)->provisioning_mode) {
		case SD_LBP_UNMAP:
			return sd_setup_unmap_cmnd(cmd);
		case SD_LBP_WS16:
			return sd_setup_write_same16_cmnd(cmd, true);
		case SD_LBP_WS10:
			return sd_setup_write_same10_cmnd(cmd, true);
		case SD_LBP_ZERO:
			return sd_setup_write_same10_cmnd(cmd, false);
		default:
			return BLKPREP_INVALID;
		}
	case REQ_OP_WRITE_ZEROES:
		return sd_setup_write_zeroes_cmnd(cmd);
	case REQ_OP_WRITE_SAME:
		return sd_setup_write_same_cmnd(cmd);
	case REQ_OP_FLUSH:
		return sd_setup_flush_cmnd(cmd);
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		return sd_setup_read_write_cmnd(cmd);
	case REQ_OP_ZONE_REPORT:
		return sd_zbc_setup_report_cmnd(cmd);
	case REQ_OP_ZONE_RESET:
		return sd_zbc_setup_reset_cmnd(cmd);
	default:
		BUG();
	}
}

static void sd_uninit_command(struct scsi_cmnd *SCpnt)
{
	struct request *rq = SCpnt->request;

	if (rq->rq_flags & RQF_SPECIAL_PAYLOAD)
		__free_page(rq->special_vec.bv_page);

	if (SCpnt->cmnd != scsi_req(rq)->cmd) {
		mempool_free(SCpnt->cmnd, sd_cdb_pool);
		SCpnt->cmnd = NULL;
		SCpnt->cmd_len = 0;
	}
}

/**
 *	sd_open - open a scsi disk device
 *	@bdev: Block device of the scsi disk to open
 *	@mode: FMODE_* mask
 *
 *	Returns 0 if successful. Returns a negated errno value in case 
 *	of error.
 *
 *	Note: This can be called from a user context (e.g. fsck(1) )
 *	or from within the kernel (e.g. as a result of a mount(1) ).
 *	In the latter case @inode and @filp carry an abridged amount
 *	of information as noted above.
 *
 *	Locking: called with bdev->bd_mutex held.
 **/
static int sd_open(struct block_device *bdev, fmode_t mode)
{
	struct scsi_disk *sdkp = scsi_disk_get(bdev->bd_disk);
	struct scsi_device *sdev;
	int retval;

	if (!sdkp)
		return -ENXIO;

	SCSI_LOG_HLQUEUE(3, sd_printk(KERN_INFO, sdkp, "sd_open\n"));

	sdev = sdkp->device;

	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	retval = -ENXIO;
	if (!scsi_block_when_processing_errors(sdev))
		goto error_out;

	if (sdev->removable || sdkp->write_prot)
		check_disk_change(bdev);

	/*
	 * If the drive is empty, just let the open fail.
	 */
	retval = -ENOMEDIUM;
	if (sdev->removable && !sdkp->media_present && !(mode & FMODE_NDELAY))
		goto error_out;

	/*
	 * If the device has the write protect tab set, have the open fail
	 * if the user expects to be able to write to the thing.
	 */
	retval = -EROFS;
	if (sdkp->write_prot && (mode & FMODE_WRITE))
		goto error_out;

	/*
	 * It is possible that the disk changing stuff resulted in
	 * the device being taken offline.  If this is the case,
	 * report this to the user, and don't pretend that the
	 * open actually succeeded.
	 */
	retval = -ENXIO;
	if (!scsi_device_online(sdev))
		goto error_out;

	if ((atomic_inc_return(&sdkp->openers) == 1) && sdev->removable) {
		if (scsi_block_when_processing_errors(sdev))
			scsi_set_medium_removal(sdev, SCSI_REMOVAL_PREVENT);
	}

	return 0;

error_out:
	scsi_disk_put(sdkp);
	return retval;	
}

/**
 *	sd_release - invoked when the (last) close(2) is called on this
 *	scsi disk.
 *	@disk: disk to release
 *	@mode: FMODE_* mask
 *
 *	Returns 0. 
 *
 *	Note: may block (uninterruptible) if error recovery is underway
 *	on this disk.
 *
 *	Locking: called with bdev->bd_mutex held.
 **/
static void sd_release(struct gendisk *disk, fmode_t mode)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdev = sdkp->device;

	SCSI_LOG_HLQUEUE(3, sd_printk(KERN_INFO, sdkp, "sd_release\n"));

	if (atomic_dec_return(&sdkp->openers) == 0 && sdev->removable) {
		if (scsi_block_when_processing_errors(sdev))
			scsi_set_medium_removal(sdev, SCSI_REMOVAL_ALLOW);
	}

	/*
	 * XXX and what if there are packets in flight and this close()
	 * XXX is followed by a "rmmod sd_mod"?
	 */

	scsi_disk_put(sdkp);
}

static int sd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct scsi_disk *sdkp = scsi_disk(bdev->bd_disk);
	struct scsi_device *sdp = sdkp->device;
	struct Scsi_Host *host = sdp->host;
	sector_t capacity = logical_to_sectors(sdp, sdkp->capacity);
	int diskinfo[4];

	/* default to most commonly used values */
	diskinfo[0] = 0x40;	/* 1 << 6 */
	diskinfo[1] = 0x20;	/* 1 << 5 */
	diskinfo[2] = capacity >> 11;

	/* override with calculated, extended default, or driver values */
	if (host->hostt->bios_param)
		host->hostt->bios_param(sdp, bdev, capacity, diskinfo);
	else
		scsicam_bios_param(bdev, capacity, diskinfo);

	geo->heads = diskinfo[0];
	geo->sectors = diskinfo[1];
	geo->cylinders = diskinfo[2];
	return 0;
}

/**
 *	sd_ioctl - process an ioctl
 *	@bdev: target block device
 *	@mode: FMODE_* mask
 *	@cmd: ioctl command number
 *	@arg: this is third argument given to ioctl(2) system call.
 *	Often contains a pointer.
 *
 *	Returns 0 if successful (some ioctls return positive numbers on
 *	success as well). Returns a negated errno value in case of error.
 *
 *	Note: most ioctls are forward onto the block subsystem or further
 *	down in the scsi subsystem.
 **/
static int sd_ioctl(struct block_device *bdev, fmode_t mode,
		    unsigned int cmd, unsigned long arg)
{
	struct gendisk *disk = bdev->bd_disk;
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdp = sdkp->device;
	void __user *p = (void __user *)arg;
	int error;
    
	SCSI_LOG_IOCTL(1, sd_printk(KERN_INFO, sdkp, "sd_ioctl: disk=%s, "
				    "cmd=0x%x\n", disk->disk_name, cmd));

	error = scsi_verify_blk_ioctl(bdev, cmd);
	if (error < 0)
		return error;

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */
	error = scsi_ioctl_block_when_processing_errors(sdp, cmd,
			(mode & FMODE_NDELAY) != 0);
	if (error)
		goto out;

	/*
	 * Send SCSI addressing ioctls directly to mid level, send other
	 * ioctls to block level and then onto mid level if they can't be
	 * resolved.
	 */
	switch (cmd) {
		case SCSI_IOCTL_GET_IDLUN:
		case SCSI_IOCTL_GET_BUS_NUMBER:
			error = scsi_ioctl(sdp, cmd, p);
			break;
		default:
			error = scsi_cmd_blk_ioctl(bdev, mode, cmd, p);
			if (error != -ENOTTY)
				break;
			error = scsi_ioctl(sdp, cmd, p);
			break;
	}
out:
	return error;
}

static void set_media_not_present(struct scsi_disk *sdkp)
{
	if (sdkp->media_present)
		sdkp->device->changed = 1;

	if (sdkp->device->removable) {
		sdkp->media_present = 0;
		sdkp->capacity = 0;
	}
}

static int media_not_present(struct scsi_disk *sdkp,
			     struct scsi_sense_hdr *sshdr)
{
	if (!scsi_sense_valid(sshdr))
		return 0;

	/* not invoked for commands that could return deferred errors */
	switch (sshdr->sense_key) {
	case UNIT_ATTENTION:
	case NOT_READY:
		/* medium not present */
		if (sshdr->asc == 0x3A) {
			set_media_not_present(sdkp);
			return 1;
		}
	}
	return 0;
}

/**
 *	sd_check_events - check media events
 *	@disk: kernel device descriptor
 *	@clearing: disk events currently being cleared
 *
 *	Returns mask of DISK_EVENT_*.
 *
 *	Note: this function is invoked from the block subsystem.
 **/
static unsigned int sd_check_events(struct gendisk *disk, unsigned int clearing)
{
	struct scsi_disk *sdkp = scsi_disk_get(disk);
	struct scsi_device *sdp;
	int retval;

	if (!sdkp)
		return 0;

	sdp = sdkp->device;
	SCSI_LOG_HLQUEUE(3, sd_printk(KERN_INFO, sdkp, "sd_check_events\n"));

	/*
	 * If the device is offline, don't send any commands - just pretend as
	 * if the command failed.  If the device ever comes back online, we
	 * can deal with it then.  It is only because of unrecoverable errors
	 * that we would ever take a device offline in the first place.
	 */
	if (!scsi_device_online(sdp)) {
		set_media_not_present(sdkp);
		goto out;
	}

	/*
	 * Using TEST_UNIT_READY enables differentiation between drive with
	 * no cartridge loaded - NOT READY, drive with changed cartridge -
	 * UNIT ATTENTION, or with same cartridge - GOOD STATUS.
	 *
	 * Drives that auto spin down. eg iomega jaz 1G, will be started
	 * by sd_spinup_disk() from sd_revalidate_disk(), which happens whenever
	 * sd_revalidate() is called.
	 */
	if (scsi_block_when_processing_errors(sdp)) {
		struct scsi_sense_hdr sshdr = { 0, };

		retval = scsi_test_unit_ready(sdp, SD_TIMEOUT, SD_MAX_RETRIES,
					      &sshdr);

		/* failed to execute TUR, assume media not present */
		if (host_byte(retval)) {
			set_media_not_present(sdkp);
			goto out;
		}

		if (media_not_present(sdkp, &sshdr))
			goto out;
	}

	/*
	 * For removable scsi disk we have to recognise the presence
	 * of a disk in the drive.
	 */
	if (!sdkp->media_present)
		sdp->changed = 1;
	sdkp->media_present = 1;
out:
	/*
	 * sdp->changed is set under the following conditions:
	 *
	 *	Medium present state has changed in either direction.
	 *	Device has indicated UNIT_ATTENTION.
	 */
	retval = sdp->changed ? DISK_EVENT_MEDIA_CHANGE : 0;
	sdp->changed = 0;
	scsi_disk_put(sdkp);
	return retval;
}

static int sd_sync_cache(struct scsi_disk *sdkp, struct scsi_sense_hdr *sshdr)
{
	int retries, res;
	struct scsi_device *sdp = sdkp->device;
	const int timeout = sdp->request_queue->rq_timeout
		* SD_FLUSH_TIMEOUT_MULTIPLIER;
	struct scsi_sense_hdr my_sshdr;

	if (!scsi_device_online(sdp))
		return -ENODEV;

	/* caller might not be interested in sense, but we need it */
	if (!sshdr)
		sshdr = &my_sshdr;

	for (retries = 3; retries > 0; --retries) {
		unsigned char cmd[10] = { 0 };

		cmd[0] = SYNCHRONIZE_CACHE;
		/*
		 * Leave the rest of the command zero to indicate
		 * flush everything.
		 */
		res = scsi_execute(sdp, cmd, DMA_NONE, NULL, 0, NULL, sshdr,
				timeout, SD_MAX_RETRIES, 0, RQF_PM, NULL);
		if (res == 0)
			break;
	}

	if (res) {
		sd_print_result(sdkp, "Synchronize Cache(10) failed", res);

		if (driver_byte(res) & DRIVER_SENSE)
			sd_print_sense_hdr(sdkp, sshdr);

		/* we need to evaluate the error return  */
		if (scsi_sense_valid(sshdr) &&
			(sshdr->asc == 0x3a ||	/* medium not present */
			 sshdr->asc == 0x20))	/* invalid command */
				/* this is no error here */
				return 0;

		switch (host_byte(res)) {
		/* ignore errors due to racing a disconnection */
		case DID_BAD_TARGET:
		case DID_NO_CONNECT:
			return 0;
		/* signal the upper layer it might try again */
		case DID_BUS_BUSY:
		case DID_IMM_RETRY:
		case DID_REQUEUE:
		case DID_SOFT_ERROR:
			return -EBUSY;
		default:
			return -EIO;
		}
	}
	return 0;
}

static void sd_rescan(struct device *dev)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);

	revalidate_disk(sdkp->disk);
}


#ifdef CONFIG_COMPAT
/* 
 * This gets directly called from VFS. When the ioctl 
 * is not recognized we go back to the other translation paths. 
 */
static int sd_compat_ioctl(struct block_device *bdev, fmode_t mode,
			   unsigned int cmd, unsigned long arg)
{
	struct scsi_device *sdev = scsi_disk(bdev->bd_disk)->device;
	int error;

	error = scsi_ioctl_block_when_processing_errors(sdev, cmd,
			(mode & FMODE_NDELAY) != 0);
	if (error)
		return error;
	       
	/* 
	 * Let the static ioctl translation table take care of it.
	 */
	if (!sdev->host->hostt->compat_ioctl)
		return -ENOIOCTLCMD; 
	return sdev->host->hostt->compat_ioctl(sdev, cmd, (void __user *)arg);
}
#endif

static char sd_pr_type(enum pr_type type)
{
	switch (type) {
	case PR_WRITE_EXCLUSIVE:
		return 0x01;
	case PR_EXCLUSIVE_ACCESS:
		return 0x03;
	case PR_WRITE_EXCLUSIVE_REG_ONLY:
		return 0x05;
	case PR_EXCLUSIVE_ACCESS_REG_ONLY:
		return 0x06;
	case PR_WRITE_EXCLUSIVE_ALL_REGS:
		return 0x07;
	case PR_EXCLUSIVE_ACCESS_ALL_REGS:
		return 0x08;
	default:
		return 0;
	}
};

static int sd_pr_command(struct block_device *bdev, u8 sa,
		u64 key, u64 sa_key, u8 type, u8 flags)
{
	struct scsi_device *sdev = scsi_disk(bdev->bd_disk)->device;
	struct scsi_sense_hdr sshdr;
	int result;
	u8 cmd[16] = { 0, };
	u8 data[24] = { 0, };

	cmd[0] = PERSISTENT_RESERVE_OUT;
	cmd[1] = sa;
	cmd[2] = type;
	put_unaligned_be32(sizeof(data), &cmd[5]);

	put_unaligned_be64(key, &data[0]);
	put_unaligned_be64(sa_key, &data[8]);
	data[20] = flags;

	result = scsi_execute_req(sdev, cmd, DMA_TO_DEVICE, &data, sizeof(data),
			&sshdr, SD_TIMEOUT, SD_MAX_RETRIES, NULL);

	if ((driver_byte(result) & DRIVER_SENSE) &&
	    (scsi_sense_valid(&sshdr))) {
		sdev_printk(KERN_INFO, sdev, "PR command failed: %d\n", result);
		scsi_print_sense_hdr(sdev, NULL, &sshdr);
	}

	return result;
}

static int sd_pr_register(struct block_device *bdev, u64 old_key, u64 new_key,
		u32 flags)
{
	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;
	return sd_pr_command(bdev, (flags & PR_FL_IGNORE_KEY) ? 0x06 : 0x00,
			old_key, new_key, 0,
			(1 << 0) /* APTPL */);
}

static int sd_pr_reserve(struct block_device *bdev, u64 key, enum pr_type type,
		u32 flags)
{
	if (flags)
		return -EOPNOTSUPP;
	return sd_pr_command(bdev, 0x01, key, 0, sd_pr_type(type), 0);
}

static int sd_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	return sd_pr_command(bdev, 0x02, key, 0, sd_pr_type(type), 0);
}

static int sd_pr_preempt(struct block_device *bdev, u64 old_key, u64 new_key,
		enum pr_type type, bool abort)
{
	return sd_pr_command(bdev, abort ? 0x05 : 0x04, old_key, new_key,
			     sd_pr_type(type), 0);
}

static int sd_pr_clear(struct block_device *bdev, u64 key)
{
	return sd_pr_command(bdev, 0x03, key, 0, 0, 0);
}

static const struct pr_ops sd_pr_ops = {
	.pr_register	= sd_pr_register,
	.pr_reserve	= sd_pr_reserve,
	.pr_release	= sd_pr_release,
	.pr_preempt	= sd_pr_preempt,
	.pr_clear	= sd_pr_clear,
};

static const struct block_device_operations sd_fops = {
	.owner			= THIS_MODULE,
	.open			= sd_open,
	.release		= sd_release,
	.ioctl			= sd_ioctl,
	.getgeo			= sd_getgeo,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= sd_compat_ioctl,
#endif
	.check_events		= sd_check_events,
	.revalidate_disk	= sd_revalidate_disk,
	.unlock_native_capacity	= sd_unlock_native_capacity,
	.pr_ops			= &sd_pr_ops,
};

/**
 *	sd_eh_reset - reset error handling callback
 *	@scmd:		sd-issued command that has failed
 *
 *	This function is called by the SCSI midlayer before starting
 *	SCSI EH. When counting medium access failures we have to be
 *	careful to register it only only once per device and SCSI EH run;
 *	there might be several timed out commands which will cause the
 *	'max_medium_access_timeouts' counter to trigger after the first
 *	SCSI EH run already and set the device to offline.
 *	So this function resets the internal counter before starting SCSI EH.
 **/
static void sd_eh_reset(struct scsi_cmnd *scmd)
{
	struct scsi_disk *sdkp = scsi_disk(scmd->request->rq_disk);

	/* New SCSI EH run, reset gate variable */
	sdkp->ignore_medium_access_errors = false;
}

/**
 *	sd_eh_action - error handling callback
 *	@scmd:		sd-issued command that has failed
 *	@eh_disp:	The recovery disposition suggested by the midlayer
 *
 *	This function is called by the SCSI midlayer upon completion of an
 *	error test command (currently TEST UNIT READY). The result of sending
 *	the eh command is passed in eh_disp.  We're looking for devices that
 *	fail medium access commands but are OK with non access commands like
 *	test unit ready (so wrongly see the device as having a successful
 *	recovery)
 **/
static int sd_eh_action(struct scsi_cmnd *scmd, int eh_disp)
{
	struct scsi_disk *sdkp = scsi_disk(scmd->request->rq_disk);

	if (!scsi_device_online(scmd->device) ||
	    !scsi_medium_access_command(scmd) ||
	    host_byte(scmd->result) != DID_TIME_OUT ||
	    eh_disp != SUCCESS)
		return eh_disp;

	/*
	 * The device has timed out executing a medium access command.
	 * However, the TEST UNIT READY command sent during error
	 * handling completed successfully. Either the device is in the
	 * process of recovering or has it suffered an internal failure
	 * that prevents access to the storage medium.
	 */
	if (!sdkp->ignore_medium_access_errors) {
		sdkp->medium_access_timed_out++;
		sdkp->ignore_medium_access_errors = true;
	}

	/*
	 * If the device keeps failing read/write commands but TEST UNIT
	 * READY always completes successfully we assume that medium
	 * access is no longer possible and take the device offline.
	 */
	if (sdkp->medium_access_timed_out >= sdkp->max_medium_access_timeouts) {
		scmd_printk(KERN_ERR, scmd,
			    "Medium access timeout failure. Offlining disk!\n");
		scsi_device_set_state(scmd->device, SDEV_OFFLINE);

		return SUCCESS;
	}

	return eh_disp;
}

static unsigned int sd_completed_bytes(struct scsi_cmnd *scmd)
{
	struct request *req = scmd->request;
	struct scsi_device *sdev = scmd->device;
	unsigned int transferred, good_bytes;
	u64 start_lba, end_lba, bad_lba;

	/*
	 * Some commands have a payload smaller than the device logical
	 * block size (e.g. INQUIRY on a 4K disk).
	 */
	if (scsi_bufflen(scmd) <= sdev->sector_size)
		return 0;

	/* Check if we have a 'bad_lba' information */
	if (!scsi_get_sense_info_fld(scmd->sense_buffer,
				     SCSI_SENSE_BUFFERSIZE,
				     &bad_lba))
		return 0;

	/*
	 * If the bad lba was reported incorrectly, we have no idea where
	 * the error is.
	 */
	start_lba = sectors_to_logical(sdev, blk_rq_pos(req));
	end_lba = start_lba + bytes_to_logical(sdev, scsi_bufflen(scmd));
	if (bad_lba < start_lba || bad_lba >= end_lba)
		return 0;

	/*
	 * resid is optional but mostly filled in.  When it's unused,
	 * its value is zero, so we assume the whole buffer transferred
	 */
	transferred = scsi_bufflen(scmd) - scsi_get_resid(scmd);

	/* This computation should always be done in terms of the
	 * resolution of the device's medium.
	 */
	good_bytes = logical_to_bytes(sdev, bad_lba - start_lba);

	return min(good_bytes, transferred);
}

/**
 *	sd_done - bottom half handler: called when the lower level
 *	driver has completed (successfully or otherwise) a scsi command.
 *	@SCpnt: mid-level's per command structure.
 *
 *	Note: potentially run from within an ISR. Must not block.
 **/
static int sd_done(struct scsi_cmnd *SCpnt)
{
	int result = SCpnt->result;
	unsigned int good_bytes = result ? 0 : scsi_bufflen(SCpnt);
	unsigned int sector_size = SCpnt->device->sector_size;
	unsigned int resid;
	struct scsi_sense_hdr sshdr;
	struct scsi_disk *sdkp = scsi_disk(SCpnt->request->rq_disk);
	struct request *req = SCpnt->request;
	int sense_valid = 0;
	int sense_deferred = 0;

	switch (req_op(req)) {
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE_SAME:
	case REQ_OP_ZONE_RESET:
		if (!result) {
			good_bytes = blk_rq_bytes(req);
			scsi_set_resid(SCpnt, 0);
		} else {
			good_bytes = 0;
			scsi_set_resid(SCpnt, blk_rq_bytes(req));
		}
		break;
	case REQ_OP_ZONE_REPORT:
		if (!result) {
			good_bytes = scsi_bufflen(SCpnt)
				- scsi_get_resid(SCpnt);
			scsi_set_resid(SCpnt, 0);
		} else {
			good_bytes = 0;
			scsi_set_resid(SCpnt, blk_rq_bytes(req));
		}
		break;
	default:
		/*
		 * In case of bogus fw or device, we could end up having
		 * an unaligned partial completion. Check this here and force
		 * alignment.
		 */
		resid = scsi_get_resid(SCpnt);
		if (resid & (sector_size - 1)) {
			sd_printk(KERN_INFO, sdkp,
				"Unaligned partial completion (resid=%u, sector_sz=%u)\n",
				resid, sector_size);
			resid = min(scsi_bufflen(SCpnt),
				    round_up(resid, sector_size));
			scsi_set_resid(SCpnt, resid);
		}
	}

	if (result) {
		sense_valid = scsi_command_normalize_sense(SCpnt, &sshdr);
		if (sense_valid)
			sense_deferred = scsi_sense_is_deferred(&sshdr);
	}
	sdkp->medium_access_timed_out = 0;

	if (driver_byte(result) != DRIVER_SENSE &&
	    (!sense_valid || sense_deferred))
		goto out;

	switch (sshdr.sense_key) {
	case HARDWARE_ERROR:
	case MEDIUM_ERROR:
		good_bytes = sd_completed_bytes(SCpnt);
		break;
	case RECOVERED_ERROR:
		good_bytes = scsi_bufflen(SCpnt);
		break;
	case NO_SENSE:
		/* This indicates a false check condition, so ignore it.  An
		 * unknown amount of data was transferred so treat it as an
		 * error.
		 */
		SCpnt->result = 0;
		memset(SCpnt->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		break;
	case ABORTED_COMMAND:
		if (sshdr.asc == 0x10)  /* DIF: Target detected corruption */
			good_bytes = sd_completed_bytes(SCpnt);
		break;
	case ILLEGAL_REQUEST:
		switch (sshdr.asc) {
		case 0x10:	/* DIX: Host detected corruption */
			good_bytes = sd_completed_bytes(SCpnt);
			break;
		case 0x20:	/* INVALID COMMAND OPCODE */
		case 0x24:	/* INVALID FIELD IN CDB */
			switch (SCpnt->cmnd[0]) {
			case UNMAP:
				sd_config_discard(sdkp, SD_LBP_DISABLE);
				break;
			case WRITE_SAME_16:
			case WRITE_SAME:
				if (SCpnt->cmnd[1] & 8) { /* UNMAP */
					sd_config_discard(sdkp, SD_LBP_DISABLE);
				} else {
					sdkp->device->no_write_same = 1;
					sd_config_write_same(sdkp);
					req->__data_len = blk_rq_bytes(req);
					req->rq_flags |= RQF_QUIET;
				}
				break;
			}
		}
		break;
	default:
		break;
	}

 out:
	if (sd_is_zoned(sdkp))
		sd_zbc_complete(SCpnt, good_bytes, &sshdr);

	SCSI_LOG_HLCOMPLETE(1, scmd_printk(KERN_INFO, SCpnt,
					   "sd_done: completed %d of %d bytes\n",
					   good_bytes, scsi_bufflen(SCpnt)));

	if (rq_data_dir(SCpnt->request) == READ && scsi_prot_sg_count(SCpnt))
		sd_dif_complete(SCpnt, good_bytes);

	return good_bytes;
}

/*
 * spinup disk - called only in sd_revalidate_disk()
 */
static void
sd_spinup_disk(struct scsi_disk *sdkp)
{
	unsigned char cmd[10];
	unsigned long spintime_expire = 0;
	int retries, spintime;
	unsigned int the_result;
	struct scsi_sense_hdr sshdr;
	int sense_valid = 0;

	spintime = 0;

	/* Spin up drives, as required.  Only do this at boot time */
	/* Spinup needs to be done for module loads too. */
	do {
		retries = 0;

		do {
			cmd[0] = TEST_UNIT_READY;
			memset((void *) &cmd[1], 0, 9);

			the_result = scsi_execute_req(sdkp->device, cmd,
						      DMA_NONE, NULL, 0,
						      &sshdr, SD_TIMEOUT,
						      SD_MAX_RETRIES, NULL);

			/*
			 * If the drive has indicated to us that it
			 * doesn't have any media in it, don't bother
			 * with any more polling.
			 */
			if (media_not_present(sdkp, &sshdr))
				return;

			if (the_result)
				sense_valid = scsi_sense_valid(&sshdr);
			retries++;
		} while (retries < 3 && 
			 (!scsi_status_is_good(the_result) ||
			  ((driver_byte(the_result) & DRIVER_SENSE) &&
			  sense_valid && sshdr.sense_key == UNIT_ATTENTION)));

		if ((driver_byte(the_result) & DRIVER_SENSE) == 0) {
			/* no sense, TUR either succeeded or failed
			 * with a status error */
			if(!spintime && !scsi_status_is_good(the_result)) {
				sd_print_result(sdkp, "Test Unit Ready failed",
						the_result);
			}
			break;
		}

		/*
		 * The device does not want the automatic start to be issued.
		 */
		if (sdkp->device->no_start_on_add)
			break;

		if (sense_valid && sshdr.sense_key == NOT_READY) {
			if (sshdr.asc == 4 && sshdr.ascq == 3)
				break;	/* manual intervention required */
			if (sshdr.asc == 4 && sshdr.ascq == 0xb)
				break;	/* standby */
			if (sshdr.asc == 4 && sshdr.ascq == 0xc)
				break;	/* unavailable */
			/*
			 * Issue command to spin up drive when not ready
			 */
			if (!spintime) {
				sd_printk(KERN_NOTICE, sdkp, "Spinning up disk...");
				cmd[0] = START_STOP;
				cmd[1] = 1;	/* Return immediately */
				memset((void *) &cmd[2], 0, 8);
				cmd[4] = 1;	/* Start spin cycle */
				if (sdkp->device->start_stop_pwr_cond)
					cmd[4] |= 1 << 4;
				scsi_execute_req(sdkp->device, cmd, DMA_NONE,
						 NULL, 0, &sshdr,
						 SD_TIMEOUT, SD_MAX_RETRIES,
						 NULL);
				spintime_expire = jiffies + 100 * HZ;
				spintime = 1;
			}
			/* Wait 1 second for next try */
			msleep(1000);
			printk(".");

		/*
		 * Wait for USB flash devices with slow firmware.
		 * Yes, this sense key/ASC combination shouldn't
		 * occur here.  It's characteristic of these devices.
		 */
		} else if (sense_valid &&
				sshdr.sense_key == UNIT_ATTENTION &&
				sshdr.asc == 0x28) {
			if (!spintime) {
				spintime_expire = jiffies + 5 * HZ;
				spintime = 1;
			}
			/* Wait 1 second for next try */
			msleep(1000);
		} else {
			/* we don't understand the sense code, so it's
			 * probably pointless to loop */
			if(!spintime) {
				sd_printk(KERN_NOTICE, sdkp, "Unit Not Ready\n");
				sd_print_sense_hdr(sdkp, &sshdr);
			}
			break;
		}
				
	} while (spintime && time_before_eq(jiffies, spintime_expire));

	if (spintime) {
		if (scsi_status_is_good(the_result))
			printk("ready\n");
		else
			printk("not responding...\n");
	}
}

/*
 * Determine whether disk supports Data Integrity Field.
 */
static int sd_read_protection_type(struct scsi_disk *sdkp, unsigned char *buffer)
{
	struct scsi_device *sdp = sdkp->device;
	u8 type;
	int ret = 0;

	if (scsi_device_protection(sdp) == 0 || (buffer[12] & 1) == 0)
		return ret;

	type = ((buffer[12] >> 1) & 7) + 1; /* P_TYPE 0 = Type 1 */

	if (type > T10_PI_TYPE3_PROTECTION)
		ret = -ENODEV;
	else if (scsi_host_dif_capable(sdp->host, type))
		ret = 1;

	if (sdkp->first_scan || type != sdkp->protection_type)
		switch (ret) {
		case -ENODEV:
			sd_printk(KERN_ERR, sdkp, "formatted with unsupported" \
				  " protection type %u. Disabling disk!\n",
				  type);
			break;
		case 1:
			sd_printk(KERN_NOTICE, sdkp,
				  "Enabling DIF Type %u protection\n", type);
			break;
		case 0:
			sd_printk(KERN_NOTICE, sdkp,
				  "Disabling DIF Type %u protection\n", type);
			break;
		}

	sdkp->protection_type = type;

	return ret;
}

static void read_capacity_error(struct scsi_disk *sdkp, struct scsi_device *sdp,
			struct scsi_sense_hdr *sshdr, int sense_valid,
			int the_result)
{
	if (driver_byte(the_result) & DRIVER_SENSE)
		sd_print_sense_hdr(sdkp, sshdr);
	else
		sd_printk(KERN_NOTICE, sdkp, "Sense not available.\n");

	/*
	 * Set dirty bit for removable devices if not ready -
	 * sometimes drives will not report this properly.
	 */
	if (sdp->removable &&
	    sense_valid && sshdr->sense_key == NOT_READY)
		set_media_not_present(sdkp);

	/*
	 * We used to set media_present to 0 here to indicate no media
	 * in the drive, but some drives fail read capacity even with
	 * media present, so we can't do that.
	 */
	sdkp->capacity = 0; /* unknown mapped to zero - as usual */
}

#define RC16_LEN 32
#if RC16_LEN > SD_BUF_SIZE
#error RC16_LEN must not be more than SD_BUF_SIZE
#endif

#define READ_CAPACITY_RETRIES_ON_RESET	10

/*
 * Ensure that we don't overflow sector_t when CONFIG_LBDAF is not set
 * and the reported logical block size is bigger than 512 bytes. Note
 * that last_sector is a u64 and therefore logical_to_sectors() is not
 * applicable.
 */
static bool sd_addressable_capacity(u64 lba, unsigned int sector_size)
{
	u64 last_sector = (lba + 1ULL) << (ilog2(sector_size) - 9);

	if (sizeof(sector_t) == 4 && last_sector > U32_MAX)
		return false;

	return true;
}

static int read_capacity_16(struct scsi_disk *sdkp, struct scsi_device *sdp,
						unsigned char *buffer)
{
	unsigned char cmd[16];
	struct scsi_sense_hdr sshdr;
	int sense_valid = 0;
	int the_result;
	int retries = 3, reset_retries = READ_CAPACITY_RETRIES_ON_RESET;
	unsigned int alignment;
	unsigned long long lba;
	unsigned sector_size;

	if (sdp->no_read_capacity_16)
		return -EINVAL;

	do {
		memset(cmd, 0, 16);
		cmd[0] = SERVICE_ACTION_IN_16;
		cmd[1] = SAI_READ_CAPACITY_16;
		cmd[13] = RC16_LEN;
		memset(buffer, 0, RC16_LEN);

		the_result = scsi_execute_req(sdp, cmd, DMA_FROM_DEVICE,
					buffer, RC16_LEN, &sshdr,
					SD_TIMEOUT, SD_MAX_RETRIES, NULL);

		if (media_not_present(sdkp, &sshdr))
			return -ENODEV;

		if (the_result) {
			sense_valid = scsi_sense_valid(&sshdr);
			if (sense_valid &&
			    sshdr.sense_key == ILLEGAL_REQUEST &&
			    (sshdr.asc == 0x20 || sshdr.asc == 0x24) &&
			    sshdr.ascq == 0x00)
				/* Invalid Command Operation Code or
				 * Invalid Field in CDB, just retry
				 * silently with RC10 */
				return -EINVAL;
			if (sense_valid &&
			    sshdr.sense_key == UNIT_ATTENTION &&
			    sshdr.asc == 0x29 && sshdr.ascq == 0x00)
				/* Device reset might occur several times,
				 * give it one more chance */
				if (--reset_retries > 0)
					continue;
		}
		retries--;

	} while (the_result && retries);

	if (the_result) {
		sd_print_result(sdkp, "Read Capacity(16) failed", the_result);
		read_capacity_error(sdkp, sdp, &sshdr, sense_valid, the_result);
		return -EINVAL;
	}

	sector_size = get_unaligned_be32(&buffer[8]);
	lba = get_unaligned_be64(&buffer[0]);

	if (sd_read_protection_type(sdkp, buffer) < 0) {
		sdkp->capacity = 0;
		return -ENODEV;
	}

	if (!sd_addressable_capacity(lba, sector_size)) {
		sd_printk(KERN_ERR, sdkp, "Too big for this kernel. Use a "
			"kernel compiled with support for large block "
			"devices.\n");
		sdkp->capacity = 0;
		return -EOVERFLOW;
	}

	/* Logical blocks per physical block exponent */
	sdkp->physical_block_size = (1 << (buffer[13] & 0xf)) * sector_size;

	/* RC basis */
	sdkp->rc_basis = (buffer[12] >> 4) & 0x3;

	/* Lowest aligned logical block */
	alignment = ((buffer[14] & 0x3f) << 8 | buffer[15]) * sector_size;
	blk_queue_alignment_offset(sdp->request_queue, alignment);
	if (alignment && sdkp->first_scan)
		sd_printk(KERN_NOTICE, sdkp,
			  "physical block alignment offset: %u\n", alignment);

	if (buffer[14] & 0x80) { /* LBPME */
		sdkp->lbpme = 1;

		if (buffer[14] & 0x40) /* LBPRZ */
			sdkp->lbprz = 1;

		sd_config_discard(sdkp, SD_LBP_WS16);
	}

	sdkp->capacity = lba + 1;
	return sector_size;
}

static int read_capacity_10(struct scsi_disk *sdkp, struct scsi_device *sdp,
						unsigned char *buffer)
{
	unsigned char cmd[16];
	struct scsi_sense_hdr sshdr;
	int sense_valid = 0;
	int the_result;
	int retries = 3, reset_retries = READ_CAPACITY_RETRIES_ON_RESET;
	sector_t lba;
	unsigned sector_size;

	do {
		cmd[0] = READ_CAPACITY;
		memset(&cmd[1], 0, 9);
		memset(buffer, 0, 8);

		the_result = scsi_execute_req(sdp, cmd, DMA_FROM_DEVICE,
					buffer, 8, &sshdr,
					SD_TIMEOUT, SD_MAX_RETRIES, NULL);

		if (media_not_present(sdkp, &sshdr))
			return -ENODEV;

		if (the_result) {
			sense_valid = scsi_sense_valid(&sshdr);
			if (sense_valid &&
			    sshdr.sense_key == UNIT_ATTENTION &&
			    sshdr.asc == 0x29 && sshdr.ascq == 0x00)
				/* Device reset might occur several times,
				 * give it one more chance */
				if (--reset_retries > 0)
					continue;
		}
		retries--;

	} while (the_result && retries);

	if (the_result) {
		sd_print_result(sdkp, "Read Capacity(10) failed", the_result);
		read_capacity_error(sdkp, sdp, &sshdr, sense_valid, the_result);
		return -EINVAL;
	}

	sector_size = get_unaligned_be32(&buffer[4]);
	lba = get_unaligned_be32(&buffer[0]);

	if (sdp->no_read_capacity_16 && (lba == 0xffffffff)) {
		/* Some buggy (usb cardreader) devices return an lba of
		   0xffffffff when the want to report a size of 0 (with
		   which they really mean no media is present) */
		sdkp->capacity = 0;
		sdkp->physical_block_size = sector_size;
		return sector_size;
	}

	if (!sd_addressable_capacity(lba, sector_size)) {
		sd_printk(KERN_ERR, sdkp, "Too big for this kernel. Use a "
			"kernel compiled with support for large block "
			"devices.\n");
		sdkp->capacity = 0;
		return -EOVERFLOW;
	}

	sdkp->capacity = lba + 1;
	sdkp->physical_block_size = sector_size;
	return sector_size;
}

static int sd_try_rc16_first(struct scsi_device *sdp)
{
	if (sdp->host->max_cmd_len < 16)
		return 0;
	if (sdp->try_rc_10_first)
		return 0;
	if (sdp->scsi_level > SCSI_SPC_2)
		return 1;
	if (scsi_device_protection(sdp))
		return 1;
	return 0;
}

/*
 * read disk capacity
 */
static void
sd_read_capacity(struct scsi_disk *sdkp, unsigned char *buffer)
{
	int sector_size;
	struct scsi_device *sdp = sdkp->device;

	if (sd_try_rc16_first(sdp)) {
		sector_size = read_capacity_16(sdkp, sdp, buffer);
		if (sector_size == -EOVERFLOW)
			goto got_data;
		if (sector_size == -ENODEV)
			return;
		if (sector_size < 0)
			sector_size = read_capacity_10(sdkp, sdp, buffer);
		if (sector_size < 0)
			return;
	} else {
		sector_size = read_capacity_10(sdkp, sdp, buffer);
		if (sector_size == -EOVERFLOW)
			goto got_data;
		if (sector_size < 0)
			return;
		if ((sizeof(sdkp->capacity) > 4) &&
		    (sdkp->capacity > 0xffffffffULL)) {
			int old_sector_size = sector_size;
			sd_printk(KERN_NOTICE, sdkp, "Very big device. "
					"Trying to use READ CAPACITY(16).\n");
			sector_size = read_capacity_16(sdkp, sdp, buffer);
			if (sector_size < 0) {
				sd_printk(KERN_NOTICE, sdkp,
					"Using 0xffffffff as device size\n");
				sdkp->capacity = 1 + (sector_t) 0xffffffff;
				sector_size = old_sector_size;
				goto got_data;
			}
		}
	}

	/* Some devices are known to return the total number of blocks,
	 * not the highest block number.  Some devices have versions
	 * which do this and others which do not.  Some devices we might
	 * suspect of doing this but we don't know for certain.
	 *
	 * If we know the reported capacity is wrong, decrement it.  If
	 * we can only guess, then assume the number of blocks is even
	 * (usually true but not always) and err on the side of lowering
	 * the capacity.
	 */
	if (sdp->fix_capacity ||
	    (sdp->guess_capacity && (sdkp->capacity & 0x01))) {
		sd_printk(KERN_INFO, sdkp, "Adjusting the sector count "
				"from its reported value: %llu\n",
				(unsigned long long) sdkp->capacity);
		--sdkp->capacity;
	}

got_data:
	if (sector_size == 0) {
		sector_size = 512;
		sd_printk(KERN_NOTICE, sdkp, "Sector size 0 reported, "
			  "assuming 512.\n");
	}

	if (sector_size != 512 &&
	    sector_size != 1024 &&
	    sector_size != 2048 &&
	    sector_size != 4096) {
		sd_printk(KERN_NOTICE, sdkp, "Unsupported sector size %d.\n",
			  sector_size);
		/*
		 * The user might want to re-format the drive with
		 * a supported sectorsize.  Once this happens, it
		 * would be relatively trivial to set the thing up.
		 * For this reason, we leave the thing in the table.
		 */
		sdkp->capacity = 0;
		/*
		 * set a bogus sector size so the normal read/write
		 * logic in the block layer will eventually refuse any
		 * request on this device without tripping over power
		 * of two sector size assumptions
		 */
		sector_size = 512;
	}
	blk_queue_logical_block_size(sdp->request_queue, sector_size);
	blk_queue_physical_block_size(sdp->request_queue,
				      sdkp->physical_block_size);
	sdkp->device->sector_size = sector_size;

	if (sdkp->capacity > 0xffffffff)
		sdp->use_16_for_rw = 1;

}

/*
 * Print disk capacity
 */
static void
sd_print_capacity(struct scsi_disk *sdkp,
		  sector_t old_capacity)
{
	int sector_size = sdkp->device->sector_size;
	char cap_str_2[10], cap_str_10[10];

	string_get_size(sdkp->capacity, sector_size,
			STRING_UNITS_2, cap_str_2, sizeof(cap_str_2));
	string_get_size(sdkp->capacity, sector_size,
			STRING_UNITS_10, cap_str_10,
			sizeof(cap_str_10));

	if (sdkp->first_scan || old_capacity != sdkp->capacity) {
		sd_printk(KERN_NOTICE, sdkp,
			  "%llu %d-byte logical blocks: (%s/%s)\n",
			  (unsigned long long)sdkp->capacity,
			  sector_size, cap_str_10, cap_str_2);

		if (sdkp->physical_block_size != sector_size)
			sd_printk(KERN_NOTICE, sdkp,
				  "%u-byte physical blocks\n",
				  sdkp->physical_block_size);

		sd_zbc_print_zones(sdkp);
	}
}

/* called with buffer of length 512 */
static inline int
sd_do_mode_sense(struct scsi_device *sdp, int dbd, int modepage,
		 unsigned char *buffer, int len, struct scsi_mode_data *data,
		 struct scsi_sense_hdr *sshdr)
{
	return scsi_mode_sense(sdp, dbd, modepage, buffer, len,
			       SD_TIMEOUT, SD_MAX_RETRIES, data,
			       sshdr);
}

/*
 * read write protect setting, if possible - called only in sd_revalidate_disk()
 * called with buffer of length SD_BUF_SIZE
 */
static void
sd_read_write_protect_flag(struct scsi_disk *sdkp, unsigned char *buffer)
{
	int res;
	struct scsi_device *sdp = sdkp->device;
	struct scsi_mode_data data;
	int old_wp = sdkp->write_prot;

	set_disk_ro(sdkp->disk, 0);
	if (sdp->skip_ms_page_3f) {
		sd_first_printk(KERN_NOTICE, sdkp, "Assuming Write Enabled\n");
		return;
	}

	if (sdp->use_192_bytes_for_3f) {
		res = sd_do_mode_sense(sdp, 0, 0x3F, buffer, 192, &data, NULL);
	} else {
		/*
		 * First attempt: ask for all pages (0x3F), but only 4 bytes.
		 * We have to start carefully: some devices hang if we ask
		 * for more than is available.
		 */
		res = sd_do_mode_sense(sdp, 0, 0x3F, buffer, 4, &data, NULL);

		/*
		 * Second attempt: ask for page 0 When only page 0 is
		 * implemented, a request for page 3F may return Sense Key
		 * 5: Illegal Request, Sense Code 24: Invalid field in
		 * CDB.
		 */
		if (!scsi_status_is_good(res))
			res = sd_do_mode_sense(sdp, 0, 0, buffer, 4, &data, NULL);

		/*
		 * Third attempt: ask 255 bytes, as we did earlier.
		 */
		if (!scsi_status_is_good(res))
			res = sd_do_mode_sense(sdp, 0, 0x3F, buffer, 255,
					       &data, NULL);
	}

	if (!scsi_status_is_good(res)) {
		sd_first_printk(KERN_WARNING, sdkp,
			  "Test WP failed, assume Write Enabled\n");
	} else {
		sdkp->write_prot = ((data.device_specific & 0x80) != 0);
		set_disk_ro(sdkp->disk, sdkp->write_prot);
		if (sdkp->first_scan || old_wp != sdkp->write_prot) {
			sd_printk(KERN_NOTICE, sdkp, "Write Protect is %s\n",
				  sdkp->write_prot ? "on" : "off");
			sd_printk(KERN_DEBUG, sdkp, "Mode Sense: %4ph\n", buffer);
		}
	}
}

/*
 * sd_read_cache_type - called only from sd_revalidate_disk()
 * called with buffer of length SD_BUF_SIZE
 */
static void
sd_read_cache_type(struct scsi_disk *sdkp, unsigned char *buffer)
{
	int len = 0, res;
	struct scsi_device *sdp = sdkp->device;

	int dbd;
	int modepage;
	int first_len;
	struct scsi_mode_data data;
	struct scsi_sense_hdr sshdr;
	int old_wce = sdkp->WCE;
	int old_rcd = sdkp->RCD;
	int old_dpofua = sdkp->DPOFUA;


	if (sdkp->cache_override)
		return;

	first_len = 4;
	if (sdp->skip_ms_page_8) {
		if (sdp->type == TYPE_RBC)
			goto defaults;
		else {
			if (sdp->skip_ms_page_3f)
				goto defaults;
			modepage = 0x3F;
			if (sdp->use_192_bytes_for_3f)
				first_len = 192;
			dbd = 0;
		}
	} else if (sdp->type == TYPE_RBC) {
		modepage = 6;
		dbd = 8;
	} else {
		modepage = 8;
		dbd = 0;
	}

	/* cautiously ask */
	res = sd_do_mode_sense(sdp, dbd, modepage, buffer, first_len,
			&data, &sshdr);

	if (!scsi_status_is_good(res))
		goto bad_sense;

	if (!data.header_length) {
		modepage = 6;
		first_len = 0;
		sd_first_printk(KERN_ERR, sdkp,
				"Missing header in MODE_SENSE response\n");
	}

	/* that went OK, now ask for the proper length */
	len = data.length;

	/*
	 * We're only interested in the first three bytes, actually.
	 * But the data cache page is defined for the first 20.
	 */
	if (len < 3)
		goto bad_sense;
	else if (len > SD_BUF_SIZE) {
		sd_first_printk(KERN_NOTICE, sdkp, "Truncating mode parameter "
			  "data from %d to %d bytes\n", len, SD_BUF_SIZE);
		len = SD_BUF_SIZE;
	}
	if (modepage == 0x3F && sdp->use_192_bytes_for_3f)
		len = 192;

	/* Get the data */
	if (len > first_len)
		res = sd_do_mode_sense(sdp, dbd, modepage, buffer, len,
				&data, &sshdr);

	if (scsi_status_is_good(res)) {
		int offset = data.header_length + data.block_descriptor_length;

		while (offset < len) {
			u8 page_code = buffer[offset] & 0x3F;
			u8 spf       = buffer[offset] & 0x40;

			if (page_code == 8 || page_code == 6) {
				/* We're interested only in the first 3 bytes.
				 */
				if (len - offset <= 2) {
					sd_first_printk(KERN_ERR, sdkp,
						"Incomplete mode parameter "
							"data\n");
					goto defaults;
				} else {
					modepage = page_code;
					goto Page_found;
				}
			} else {
				/* Go to the next page */
				if (spf && len - offset > 3)
					offset += 4 + (buffer[offset+2] << 8) +
						buffer[offset+3];
				else if (!spf && len - offset > 1)
					offset += 2 + buffer[offset+1];
				else {
					sd_first_printk(KERN_ERR, sdkp,
							"Incomplete mode "
							"parameter data\n");
					goto defaults;
				}
			}
		}

		sd_first_printk(KERN_ERR, sdkp, "No Caching mode page found\n");
		goto defaults;

	Page_found:
		if (modepage == 8) {
			sdkp->WCE = ((buffer[offset + 2] & 0x04) != 0);
			sdkp->RCD = ((buffer[offset + 2] & 0x01) != 0);
		} else {
			sdkp->WCE = ((buffer[offset + 2] & 0x01) == 0);
			sdkp->RCD = 0;
		}

		sdkp->DPOFUA = (data.device_specific & 0x10) != 0;
		if (sdp->broken_fua) {
			sd_first_printk(KERN_NOTICE, sdkp, "Disabling FUA\n");
			sdkp->DPOFUA = 0;
		} else if (sdkp->DPOFUA && !sdkp->device->use_10_for_rw &&
			   !sdkp->device->use_16_for_rw) {
			sd_first_printk(KERN_NOTICE, sdkp,
				  "Uses READ/WRITE(6), disabling FUA\n");
			sdkp->DPOFUA = 0;
		}

		/* No cache flush allowed for write protected devices */
		if (sdkp->WCE && sdkp->write_prot)
			sdkp->WCE = 0;

		if (sdkp->first_scan || old_wce != sdkp->WCE ||
		    old_rcd != sdkp->RCD || old_dpofua != sdkp->DPOFUA)
			sd_printk(KERN_NOTICE, sdkp,
				  "Write cache: %s, read cache: %s, %s\n",
				  sdkp->WCE ? "enabled" : "disabled",
				  sdkp->RCD ? "disabled" : "enabled",
				  sdkp->DPOFUA ? "supports DPO and FUA"
				  : "doesn't support DPO or FUA");

		return;
	}

bad_sense:
	if (scsi_sense_valid(&sshdr) &&
	    sshdr.sense_key == ILLEGAL_REQUEST &&
	    sshdr.asc == 0x24 && sshdr.ascq == 0x0)
		/* Invalid field in CDB */
		sd_first_printk(KERN_NOTICE, sdkp, "Cache data unavailable\n");
	else
		sd_first_printk(KERN_ERR, sdkp,
				"Asking for cache data failed\n");

defaults:
	if (sdp->wce_default_on) {
		sd_first_printk(KERN_NOTICE, sdkp,
				"Assuming drive cache: write back\n");
		sdkp->WCE = 1;
	} else {
		sd_first_printk(KERN_ERR, sdkp,
				"Assuming drive cache: write through\n");
		sdkp->WCE = 0;
	}
	sdkp->RCD = 0;
	sdkp->DPOFUA = 0;
}

/*
 * The ATO bit indicates whether the DIF application tag is available
 * for use by the operating system.
 */
static void sd_read_app_tag_own(struct scsi_disk *sdkp, unsigned char *buffer)
{
	int res, offset;
	struct scsi_device *sdp = sdkp->device;
	struct scsi_mode_data data;
	struct scsi_sense_hdr sshdr;

	if (sdp->type != TYPE_DISK && sdp->type != TYPE_ZBC)
		return;

	if (sdkp->protection_type == 0)
		return;

	res = scsi_mode_sense(sdp, 1, 0x0a, buffer, 36, SD_TIMEOUT,
			      SD_MAX_RETRIES, &data, &sshdr);

	if (!scsi_status_is_good(res) || !data.header_length ||
	    data.length < 6) {
		sd_first_printk(KERN_WARNING, sdkp,
			  "getting Control mode page failed, assume no ATO\n");

		if (scsi_sense_valid(&sshdr))
			sd_print_sense_hdr(sdkp, &sshdr);

		return;
	}

	offset = data.header_length + data.block_descriptor_length;

	if ((buffer[offset] & 0x3f) != 0x0a) {
		sd_first_printk(KERN_ERR, sdkp, "ATO Got wrong page\n");
		return;
	}

	if ((buffer[offset + 5] & 0x80) == 0)
		return;

	sdkp->ATO = 1;

	return;
}

/**
 * sd_read_block_limits - Query disk device for preferred I/O sizes.
 * @sdkp: disk to query
 */
static void sd_read_block_limits(struct scsi_disk *sdkp)
{
	unsigned int sector_sz = sdkp->device->sector_size;
	const int vpd_len = 64;
	unsigned char *buffer = kmalloc(vpd_len, GFP_KERNEL);

	if (!buffer ||
	    /* Block Limits VPD */
	    scsi_get_vpd_page(sdkp->device, 0xb0, buffer, vpd_len))
		goto out;

	blk_queue_io_min(sdkp->disk->queue,
			 get_unaligned_be16(&buffer[6]) * sector_sz);

	sdkp->max_xfer_blocks = get_unaligned_be32(&buffer[8]);
	sdkp->opt_xfer_blocks = get_unaligned_be32(&buffer[12]);

	if (buffer[3] == 0x3c) {
		unsigned int lba_count, desc_count;

		sdkp->max_ws_blocks = (u32)get_unaligned_be64(&buffer[36]);

		if (!sdkp->lbpme)
			goto out;

		lba_count = get_unaligned_be32(&buffer[20]);
		desc_count = get_unaligned_be32(&buffer[24]);

		if (lba_count && desc_count)
			sdkp->max_unmap_blocks = lba_count;

		sdkp->unmap_granularity = get_unaligned_be32(&buffer[28]);

		if (buffer[32] & 0x80)
			sdkp->unmap_alignment =
				get_unaligned_be32(&buffer[32]) & ~(1 << 31);

		if (!sdkp->lbpvpd) { /* LBP VPD page not provided */

			if (sdkp->max_unmap_blocks)
				sd_config_discard(sdkp, SD_LBP_UNMAP);
			else
				sd_config_discard(sdkp, SD_LBP_WS16);

		} else {	/* LBP VPD page tells us what to use */
			if (sdkp->lbpu && sdkp->max_unmap_blocks)
				sd_config_discard(sdkp, SD_LBP_UNMAP);
			else if (sdkp->lbpws)
				sd_config_discard(sdkp, SD_LBP_WS16);
			else if (sdkp->lbpws10)
				sd_config_discard(sdkp, SD_LBP_WS10);
			else if (sdkp->lbpu && sdkp->max_unmap_blocks)
				sd_config_discard(sdkp, SD_LBP_UNMAP);
			else
				sd_config_discard(sdkp, SD_LBP_DISABLE);
		}
	}

 out:
	kfree(buffer);
}

/**
 * sd_read_block_characteristics - Query block dev. characteristics
 * @sdkp: disk to query
 */
static void sd_read_block_characteristics(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;
	unsigned char *buffer;
	u16 rot;
	const int vpd_len = 64;

	buffer = kmalloc(vpd_len, GFP_KERNEL);

	if (!buffer ||
	    /* Block Device Characteristics VPD */
	    scsi_get_vpd_page(sdkp->device, 0xb1, buffer, vpd_len))
		goto out;

	rot = get_unaligned_be16(&buffer[4]);

	if (rot == 1) {
		queue_flag_set_unlocked(QUEUE_FLAG_NONROT, q);
		queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, q);
	}

	if (sdkp->device->type == TYPE_ZBC) {
		/* Host-managed */
		q->limits.zoned = BLK_ZONED_HM;
	} else {
		sdkp->zoned = (buffer[8] >> 4) & 3;
		if (sdkp->zoned == 1)
			/* Host-aware */
			q->limits.zoned = BLK_ZONED_HA;
		else
			/*
			 * Treat drive-managed devices as
			 * regular block devices.
			 */
			q->limits.zoned = BLK_ZONED_NONE;
	}
	if (blk_queue_is_zoned(q) && sdkp->first_scan)
		sd_printk(KERN_NOTICE, sdkp, "Host-%s zoned block device\n",
		      q->limits.zoned == BLK_ZONED_HM ? "managed" : "aware");

 out:
	kfree(buffer);
}

/**
 * sd_read_block_provisioning - Query provisioning VPD page
 * @sdkp: disk to query
 */
static void sd_read_block_provisioning(struct scsi_disk *sdkp)
{
	unsigned char *buffer;
	const int vpd_len = 8;

	if (sdkp->lbpme == 0)
		return;

	buffer = kmalloc(vpd_len, GFP_KERNEL);

	if (!buffer || scsi_get_vpd_page(sdkp->device, 0xb2, buffer, vpd_len))
		goto out;

	sdkp->lbpvpd	= 1;
	sdkp->lbpu	= (buffer[5] >> 7) & 1;	/* UNMAP */
	sdkp->lbpws	= (buffer[5] >> 6) & 1;	/* WRITE SAME(16) with UNMAP */
	sdkp->lbpws10	= (buffer[5] >> 5) & 1;	/* WRITE SAME(10) with UNMAP */

 out:
	kfree(buffer);
}

static void sd_read_write_same(struct scsi_disk *sdkp, unsigned char *buffer)
{
	struct scsi_device *sdev = sdkp->device;

	if (sdev->host->no_write_same) {
		sdev->no_write_same = 1;

		return;
	}

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE, INQUIRY) < 0) {
		/* too large values might cause issues with arcmsr */
		int vpd_buf_len = 64;

		sdev->no_report_opcodes = 1;

		/* Disable WRITE SAME if REPORT SUPPORTED OPERATION
		 * CODES is unsupported and the device has an ATA
		 * Information VPD page (SAT).
		 */
		if (!scsi_get_vpd_page(sdev, 0x89, buffer, vpd_buf_len))
			sdev->no_write_same = 1;
	}

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE, WRITE_SAME_16) == 1)
		sdkp->ws16 = 1;

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE, WRITE_SAME) == 1)
		sdkp->ws10 = 1;
}

/**
 *	sd_revalidate_disk - called the first time a new disk is seen,
 *	performs disk spin up, read_capacity, etc.
 *	@disk: struct gendisk we care about
 **/
static int sd_revalidate_disk(struct gendisk *disk)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdp = sdkp->device;
	struct request_queue *q = sdkp->disk->queue;
	sector_t old_capacity = sdkp->capacity;
	unsigned char *buffer;
	unsigned int dev_max, rw_max;

	SCSI_LOG_HLQUEUE(3, sd_printk(KERN_INFO, sdkp,
				      "sd_revalidate_disk\n"));

	/*
	 * If the device is offline, don't try and read capacity or any
	 * of the other niceties.
	 */
	if (!scsi_device_online(sdp))
		goto out;

	buffer = kmalloc(SD_BUF_SIZE, GFP_KERNEL);
	if (!buffer) {
		sd_printk(KERN_WARNING, sdkp, "sd_revalidate_disk: Memory "
			  "allocation failure.\n");
		goto out;
	}

	sd_spinup_disk(sdkp);

	/*
	 * Without media there is no reason to ask; moreover, some devices
	 * react badly if we do.
	 */
	if (sdkp->media_present) {
		sd_read_capacity(sdkp, buffer);

		if (scsi_device_supports_vpd(sdp)) {
			sd_read_block_provisioning(sdkp);
			sd_read_block_limits(sdkp);
			sd_read_block_characteristics(sdkp);
			sd_zbc_read_zones(sdkp, buffer);
		}

		sd_print_capacity(sdkp, old_capacity);

		sd_read_write_protect_flag(sdkp, buffer);
		sd_read_cache_type(sdkp, buffer);
		sd_read_app_tag_own(sdkp, buffer);
		sd_read_write_same(sdkp, buffer);
	}

	sdkp->first_scan = 0;

	/*
	 * We now have all cache related info, determine how we deal
	 * with flush requests.
	 */
	sd_set_flush_flag(sdkp);

	/* Initial block count limit based on CDB TRANSFER LENGTH field size. */
	dev_max = sdp->use_16_for_rw ? SD_MAX_XFER_BLOCKS : SD_DEF_XFER_BLOCKS;

	/* Some devices report a maximum block count for READ/WRITE requests. */
	dev_max = min_not_zero(dev_max, sdkp->max_xfer_blocks);
	q->limits.max_dev_sectors = logical_to_sectors(sdp, dev_max);

	/*
	 * Use the device's preferred I/O size for reads and writes
	 * unless the reported value is unreasonably small, large, or
	 * garbage.
	 */
	if (sdkp->opt_xfer_blocks &&
	    sdkp->opt_xfer_blocks <= dev_max &&
	    sdkp->opt_xfer_blocks <= SD_DEF_XFER_BLOCKS &&
	    logical_to_bytes(sdp, sdkp->opt_xfer_blocks) >= PAGE_SIZE) {
		q->limits.io_opt = logical_to_bytes(sdp, sdkp->opt_xfer_blocks);
		rw_max = logical_to_sectors(sdp, sdkp->opt_xfer_blocks);
	} else
		rw_max = min_not_zero(logical_to_sectors(sdp, dev_max),
				      (sector_t)BLK_DEF_MAX_SECTORS);

	/* Combine with controller limits */
	q->limits.max_sectors = min(rw_max, queue_max_hw_sectors(q));

	set_capacity(disk, logical_to_sectors(sdp, sdkp->capacity));
	sd_config_write_same(sdkp);
	kfree(buffer);

 out:
	return 0;
}

/**
 *	sd_unlock_native_capacity - unlock native capacity
 *	@disk: struct gendisk to set capacity for
 *
 *	Block layer calls this function if it detects that partitions
 *	on @disk reach beyond the end of the device.  If the SCSI host
 *	implements ->unlock_native_capacity() method, it's invoked to
 *	give it a chance to adjust the device capacity.
 *
 *	CONTEXT:
 *	Defined by block layer.  Might sleep.
 */
static void sd_unlock_native_capacity(struct gendisk *disk)
{
	struct scsi_device *sdev = scsi_disk(disk)->device;

	if (sdev->host->hostt->unlock_native_capacity)
		sdev->host->hostt->unlock_native_capacity(sdev);
}

/**
 *	sd_format_disk_name - format disk name
 *	@prefix: name prefix - ie. "sd" for SCSI disks
 *	@index: index of the disk to format name for
 *	@buf: output buffer
 *	@buflen: length of the output buffer
 *
 *	SCSI disk names starts at sda.  The 26th device is sdz and the
 *	27th is sdaa.  The last one for two lettered suffix is sdzz
 *	which is followed by sdaaa.
 *
 *	This is basically 26 base counting with one extra 'nil' entry
 *	at the beginning from the second digit on and can be
 *	determined using similar method as 26 base conversion with the
 *	index shifted -1 after each digit is computed.
 *
 *	CONTEXT:
 *	Don't care.
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int sd_format_disk_name(char *prefix, int index, char *buf, int buflen)
{
	const int base = 'z' - 'a' + 1;
	char *begin = buf + strlen(prefix);
	char *end = buf + buflen;
	char *p;
	int unit;

	p = end - 1;
	*p = '\0';
	unit = base;
	do {
		if (p == begin)
			return -EINVAL;
		*--p = 'a' + (index % unit);
		index = (index / unit) - 1;
	} while (index >= 0);

	memmove(begin, p, end - p);
	memcpy(buf, prefix, strlen(prefix));

	return 0;
}

/*
 * The asynchronous part of sd_probe
 */
static void sd_probe_async(void *data, async_cookie_t cookie)
{
	struct scsi_disk *sdkp = data;
	struct scsi_device *sdp;
	struct gendisk *gd;
	u32 index;
	struct device *dev;

	sdp = sdkp->device;
	gd = sdkp->disk;
	index = sdkp->index;
	dev = &sdp->sdev_gendev;

	gd->major = sd_major((index & 0xf0) >> 4);
	gd->first_minor = ((index & 0xf) << 4) | (index & 0xfff00);
	gd->minors = SD_MINORS;

	gd->fops = &sd_fops;
	gd->private_data = &sdkp->driver;
	gd->queue = sdkp->device->request_queue;

	/* defaults, until the device tells us otherwise */
	sdp->sector_size = 512;
	sdkp->capacity = 0;
	sdkp->media_present = 1;
	sdkp->write_prot = 0;
	sdkp->cache_override = 0;
	sdkp->WCE = 0;
	sdkp->RCD = 0;
	sdkp->ATO = 0;
	sdkp->first_scan = 1;
	sdkp->max_medium_access_timeouts = SD_MAX_MEDIUM_TIMEOUTS;

	sd_revalidate_disk(gd);

	gd->flags = GENHD_FL_EXT_DEVT;
	if (sdp->removable) {
		gd->flags |= GENHD_FL_REMOVABLE;
		gd->events |= DISK_EVENT_MEDIA_CHANGE;
	}

	blk_pm_runtime_init(sdp->request_queue, dev);
	device_add_disk(dev, gd);
	if (sdkp->capacity)
		sd_dif_config_host(sdkp);

	sd_revalidate_disk(gd);

	sd_printk(KERN_NOTICE, sdkp, "Attached SCSI %sdisk\n",
		  sdp->removable ? "removable " : "");
	scsi_autopm_put_device(sdp);
	put_device(&sdkp->dev);
}

/**
 *	sd_probe - called during driver initialization and whenever a
 *	new scsi device is attached to the system. It is called once
 *	for each scsi device (not just disks) present.
 *	@dev: pointer to device object
 *
 *	Returns 0 if successful (or not interested in this scsi device 
 *	(e.g. scanner)); 1 when there is an error.
 *
 *	Note: this function is invoked from the scsi mid-level.
 *	This function sets up the mapping between a given 
 *	<host,channel,id,lun> (found in sdp) and new device name 
 *	(e.g. /dev/sda). More precisely it is the block device major 
 *	and minor number that is chosen here.
 *
 *	Assume sd_probe is not re-entrant (for time being)
 *	Also think about sd_probe() and sd_remove() running coincidentally.
 **/
static int sd_probe(struct device *dev)
{
	struct scsi_device *sdp = to_scsi_device(dev);
	struct scsi_disk *sdkp;
	struct gendisk *gd;
	int index;
	int error;

	scsi_autopm_get_device(sdp);
	error = -ENODEV;
	if (sdp->type != TYPE_DISK &&
	    sdp->type != TYPE_ZBC &&
	    sdp->type != TYPE_MOD &&
	    sdp->type != TYPE_RBC)
		goto out;

#ifndef CONFIG_BLK_DEV_ZONED
	if (sdp->type == TYPE_ZBC)
		goto out;
#endif
	SCSI_LOG_HLQUEUE(3, sdev_printk(KERN_INFO, sdp,
					"sd_probe\n"));

	error = -ENOMEM;
	sdkp = kzalloc(sizeof(*sdkp), GFP_KERNEL);
	if (!sdkp)
		goto out;

	gd = alloc_disk(SD_MINORS);
	if (!gd)
		goto out_free;

	do {
		if (!ida_pre_get(&sd_index_ida, GFP_KERNEL))
			goto out_put;

		spin_lock(&sd_index_lock);
		error = ida_get_new(&sd_index_ida, &index);
		spin_unlock(&sd_index_lock);
	} while (error == -EAGAIN);

	if (error) {
		sdev_printk(KERN_WARNING, sdp, "sd_probe: memory exhausted.\n");
		goto out_put;
	}

	error = sd_format_disk_name("sd", index, gd->disk_name, DISK_NAME_LEN);
	if (error) {
		sdev_printk(KERN_WARNING, sdp, "SCSI disk (sd) name length exceeded.\n");
		goto out_free_index;
	}

	sdkp->device = sdp;
	sdkp->driver = &sd_template;
	sdkp->disk = gd;
	sdkp->index = index;
	atomic_set(&sdkp->openers, 0);
	atomic_set(&sdkp->device->ioerr_cnt, 0);

	if (!sdp->request_queue->rq_timeout) {
		if (sdp->type != TYPE_MOD)
			blk_queue_rq_timeout(sdp->request_queue, SD_TIMEOUT);
		else
			blk_queue_rq_timeout(sdp->request_queue,
					     SD_MOD_TIMEOUT);
	}

	device_initialize(&sdkp->dev);
	sdkp->dev.parent = dev;
	sdkp->dev.class = &sd_disk_class;
	dev_set_name(&sdkp->dev, "%s", dev_name(dev));

	error = device_add(&sdkp->dev);
	if (error)
		goto out_free_index;

	get_device(dev);
	dev_set_drvdata(dev, sdkp);

	get_device(&sdkp->dev);	/* prevent release before async_schedule */
	async_schedule_domain(sd_probe_async, sdkp, &scsi_sd_probe_domain);

	return 0;

 out_free_index:
	spin_lock(&sd_index_lock);
	ida_remove(&sd_index_ida, index);
	spin_unlock(&sd_index_lock);
 out_put:
	put_disk(gd);
 out_free:
	kfree(sdkp);
 out:
	scsi_autopm_put_device(sdp);
	return error;
}

/**
 *	sd_remove - called whenever a scsi disk (previously recognized by
 *	sd_probe) is detached from the system. It is called (potentially
 *	multiple times) during sd module unload.
 *	@dev: pointer to device object
 *
 *	Note: this function is invoked from the scsi mid-level.
 *	This function potentially frees up a device name (e.g. /dev/sdc)
 *	that could be re-used by a subsequent sd_probe().
 *	This function is not called when the built-in sd driver is "exit-ed".
 **/
static int sd_remove(struct device *dev)
{
	struct scsi_disk *sdkp;
	dev_t devt;

	sdkp = dev_get_drvdata(dev);
	devt = disk_devt(sdkp->disk);
	scsi_autopm_get_device(sdkp->device);

	async_synchronize_full_domain(&scsi_sd_pm_domain);
	async_synchronize_full_domain(&scsi_sd_probe_domain);
	device_del(&sdkp->dev);
	del_gendisk(sdkp->disk);
	sd_shutdown(dev);

	sd_zbc_remove(sdkp);

	blk_register_region(devt, SD_MINORS, NULL,
			    sd_default_probe, NULL, NULL);

	mutex_lock(&sd_ref_mutex);
	dev_set_drvdata(dev, NULL);
	put_device(&sdkp->dev);
	mutex_unlock(&sd_ref_mutex);

	return 0;
}

/**
 *	scsi_disk_release - Called to free the scsi_disk structure
 *	@dev: pointer to embedded class device
 *
 *	sd_ref_mutex must be held entering this routine.  Because it is
 *	called on last put, you should always use the scsi_disk_get()
 *	scsi_disk_put() helpers which manipulate the semaphore directly
 *	and never do a direct put_device.
 **/
static void scsi_disk_release(struct device *dev)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct gendisk *disk = sdkp->disk;
	
	spin_lock(&sd_index_lock);
	ida_remove(&sd_index_ida, sdkp->index);
	spin_unlock(&sd_index_lock);

	disk->private_data = NULL;
	put_disk(disk);
	put_device(&sdkp->device->sdev_gendev);

	kfree(sdkp);
}

static int sd_start_stop_device(struct scsi_disk *sdkp, int start)
{
	unsigned char cmd[6] = { START_STOP };	/* START_VALID */
	struct scsi_sense_hdr sshdr;
	struct scsi_device *sdp = sdkp->device;
	int res;

	if (start)
		cmd[4] |= 1;	/* START */

	if (sdp->start_stop_pwr_cond)
		cmd[4] |= start ? 1 << 4 : 3 << 4;	/* Active or Standby */

	if (!scsi_device_online(sdp))
		return -ENODEV;

	res = scsi_execute(sdp, cmd, DMA_NONE, NULL, 0, NULL, &sshdr,
			SD_TIMEOUT, SD_MAX_RETRIES, 0, RQF_PM, NULL);
	if (res) {
		sd_print_result(sdkp, "Start/Stop Unit failed", res);
		if (driver_byte(res) & DRIVER_SENSE)
			sd_print_sense_hdr(sdkp, &sshdr);
		if (scsi_sense_valid(&sshdr) &&
			/* 0x3a is medium not present */
			sshdr.asc == 0x3a)
			res = 0;
	}

	/* SCSI error codes must not go to the generic layer */
	if (res)
		return -EIO;

	return 0;
}

/*
 * Send a SYNCHRONIZE CACHE instruction down to the device through
 * the normal SCSI command structure.  Wait for the command to
 * complete.
 */
static void sd_shutdown(struct device *dev)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);

	if (!sdkp)
		return;         /* this can happen */

	if (pm_runtime_suspended(dev))
		return;

	if (sdkp->WCE && sdkp->media_present) {
		sd_printk(KERN_NOTICE, sdkp, "Synchronizing SCSI cache\n");
		sd_sync_cache(sdkp, NULL);
	}

	if (system_state != SYSTEM_RESTART && sdkp->device->manage_start_stop) {
		sd_printk(KERN_NOTICE, sdkp, "Stopping disk\n");
		sd_start_stop_device(sdkp, 0);
	}
}

static int sd_suspend_common(struct device *dev, bool ignore_stop_errors)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);
	struct scsi_sense_hdr sshdr;
	int ret = 0;

	if (!sdkp)	/* E.g.: runtime suspend following sd_remove() */
		return 0;

	if (sdkp->WCE && sdkp->media_present) {
		sd_printk(KERN_NOTICE, sdkp, "Synchronizing SCSI cache\n");
		ret = sd_sync_cache(sdkp, &sshdr);

		if (ret) {
			/* ignore OFFLINE device */
			if (ret == -ENODEV)
				return 0;

			if (!scsi_sense_valid(&sshdr) ||
			    sshdr.sense_key != ILLEGAL_REQUEST)
				return ret;

			/*
			 * sshdr.sense_key == ILLEGAL_REQUEST means this drive
			 * doesn't support sync. There's not much to do and
			 * suspend shouldn't fail.
			 */
			 ret = 0;
		}
	}

	if (sdkp->device->manage_start_stop) {
		sd_printk(KERN_NOTICE, sdkp, "Stopping disk\n");
		/* an error is not worth aborting a system sleep */
		ret = sd_start_stop_device(sdkp, 0);
		if (ignore_stop_errors)
			ret = 0;
	}

	return ret;
}

static int sd_suspend_system(struct device *dev)
{
	return sd_suspend_common(dev, true);
}

static int sd_suspend_runtime(struct device *dev)
{
	return sd_suspend_common(dev, false);
}

static int sd_resume(struct device *dev)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);

	if (!sdkp)	/* E.g.: runtime resume at the start of sd_probe() */
		return 0;

	if (!sdkp->device->manage_start_stop)
		return 0;

	sd_printk(KERN_NOTICE, sdkp, "Starting disk\n");
	return sd_start_stop_device(sdkp, 1);
}

/**
 *	init_sd - entry point for this driver (both when built in or when
 *	a module).
 *
 *	Note: this function registers this driver with the scsi mid-level.
 **/
static int __init init_sd(void)
{
	int majors = 0, i, err;

	SCSI_LOG_HLQUEUE(3, printk("init_sd: sd driver entry point\n"));

	for (i = 0; i < SD_MAJORS; i++) {
		if (register_blkdev(sd_major(i), "sd") != 0)
			continue;
		majors++;
		blk_register_region(sd_major(i), SD_MINORS, NULL,
				    sd_default_probe, NULL, NULL);
	}

	if (!majors)
		return -ENODEV;

	err = class_register(&sd_disk_class);
	if (err)
		goto err_out;

	sd_cdb_cache = kmem_cache_create("sd_ext_cdb", SD_EXT_CDB_SIZE,
					 0, 0, NULL);
	if (!sd_cdb_cache) {
		printk(KERN_ERR "sd: can't init extended cdb cache\n");
		err = -ENOMEM;
		goto err_out_class;
	}

	sd_cdb_pool = mempool_create_slab_pool(SD_MEMPOOL_SIZE, sd_cdb_cache);
	if (!sd_cdb_pool) {
		printk(KERN_ERR "sd: can't init extended cdb pool\n");
		err = -ENOMEM;
		goto err_out_cache;
	}

	err = scsi_register_driver(&sd_template.gendrv);
	if (err)
		goto err_out_driver;

	return 0;

err_out_driver:
	mempool_destroy(sd_cdb_pool);

err_out_cache:
	kmem_cache_destroy(sd_cdb_cache);

err_out_class:
	class_unregister(&sd_disk_class);
err_out:
	for (i = 0; i < SD_MAJORS; i++)
		unregister_blkdev(sd_major(i), "sd");
	return err;
}

/**
 *	exit_sd - exit point for this driver (when it is a module).
 *
 *	Note: this function unregisters this driver from the scsi mid-level.
 **/
static void __exit exit_sd(void)
{
	int i;

	SCSI_LOG_HLQUEUE(3, printk("exit_sd: exiting sd driver\n"));

	scsi_unregister_driver(&sd_template.gendrv);
	mempool_destroy(sd_cdb_pool);
	kmem_cache_destroy(sd_cdb_cache);

	class_unregister(&sd_disk_class);

	for (i = 0; i < SD_MAJORS; i++) {
		blk_unregister_region(sd_major(i), SD_MINORS);
		unregister_blkdev(sd_major(i), "sd");
	}
}

module_init(init_sd);
module_exit(exit_sd);

static void sd_print_sense_hdr(struct scsi_disk *sdkp,
			       struct scsi_sense_hdr *sshdr)
{
	scsi_print_sense_hdr(sdkp->device,
			     sdkp->disk ? sdkp->disk->disk_name : NULL, sshdr);
}

static void sd_print_result(const struct scsi_disk *sdkp, const char *msg,
			    int result)
{
	const char *hb_string = scsi_hostbyte_string(result);
	const char *db_string = scsi_driverbyte_string(result);

	if (hb_string || db_string)
		sd_printk(KERN_INFO, sdkp,
			  "%s: Result: hostbyte=%s driverbyte=%s\n", msg,
			  hb_string ? hb_string : "invalid",
			  db_string ? db_string : "invalid");
	else
		sd_printk(KERN_INFO, sdkp,
			  "%s: Result: hostbyte=0x%02x driverbyte=0x%02x\n",
			  msg, host_byte(result), driver_byte(result));
}

