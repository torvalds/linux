// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/hdreg.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/blk-pm.h>
#include <linux/delay.h>
#include <linux/rw_hint.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/string_helpers.h>
#include <linux/slab.h>
#include <linux/sed-opal.h>
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
#include <scsi/scsi_common.h>

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

#define SD_MINORS	16

static void sd_config_discard(struct scsi_disk *, unsigned int);
static void sd_config_write_same(struct scsi_disk *);
static int  sd_revalidate_disk(struct gendisk *);
static void sd_unlock_native_capacity(struct gendisk *disk);
static void sd_shutdown(struct device *);
static void sd_read_capacity(struct scsi_disk *sdkp, unsigned char *buffer);
static void scsi_disk_release(struct device *cdev);

static DEFINE_IDA(sd_index_ida);

static mempool_t *sd_page_pool;
static struct lock_class_key sd_bio_compl_lkclass;

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
	int ct, rcd, wce, sp;
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	char buffer[64];
	char *buffer_data;
	struct scsi_mode_data data;
	struct scsi_sense_hdr sshdr;
	static const char temp[] = "temporary ";
	int len, ret;

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

	ct = sysfs_match_string(sd_cache_types, buf);
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

	if (scsi_mode_sense(sdp, 0x08, 8, 0, buffer, sizeof(buffer), SD_TIMEOUT,
			    sdkp->max_retries, &data, NULL))
		return -EINVAL;
	len = min_t(size_t, sizeof(buffer), data.length - data.header_length -
		  data.block_descriptor_length);
	buffer_data = buffer + data.header_length +
		data.block_descriptor_length;
	buffer_data[2] &= ~0x05;
	buffer_data[2] |= wce << 2 | rcd;
	sp = buffer_data[0] & 0x80 ? 1 : 0;
	buffer_data[0] &= ~0x80;

	/*
	 * Ensure WP, DPOFUA, and RESERVED fields are cleared in
	 * received mode parameter buffer before doing MODE SELECT.
	 */
	data.device_specific = 0;

	ret = scsi_mode_select(sdp, 1, sp, buffer_data, len, SD_TIMEOUT,
			       sdkp->max_retries, &data, &sshdr);
	if (ret) {
		if (ret > 0 && scsi_sense_valid(&sshdr))
			sd_print_sense_hdr(sdkp, &sshdr);
		return -EINVAL;
	}
	sd_revalidate_disk(sdkp->disk);
	return count;
}

static ssize_t
manage_start_stop_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	return sysfs_emit(buf, "%u\n",
			  sdp->manage_system_start_stop &&
			  sdp->manage_runtime_start_stop &&
			  sdp->manage_shutdown);
}
static DEVICE_ATTR_RO(manage_start_stop);

static ssize_t
manage_system_start_stop_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	return sysfs_emit(buf, "%u\n", sdp->manage_system_start_stop);
}

static ssize_t
manage_system_start_stop_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	bool v;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (kstrtobool(buf, &v))
		return -EINVAL;

	sdp->manage_system_start_stop = v;

	return count;
}
static DEVICE_ATTR_RW(manage_system_start_stop);

static ssize_t
manage_runtime_start_stop_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	return sysfs_emit(buf, "%u\n", sdp->manage_runtime_start_stop);
}

static ssize_t
manage_runtime_start_stop_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	bool v;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (kstrtobool(buf, &v))
		return -EINVAL;

	sdp->manage_runtime_start_stop = v;

	return count;
}
static DEVICE_ATTR_RW(manage_runtime_start_stop);

static ssize_t manage_shutdown_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	return sysfs_emit(buf, "%u\n", sdp->manage_shutdown);
}

static ssize_t manage_shutdown_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	bool v;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (kstrtobool(buf, &v))
		return -EINVAL;

	sdp->manage_shutdown = v;

	return count;
}
static DEVICE_ATTR_RW(manage_shutdown);

static ssize_t
allow_restart_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%u\n", sdkp->device->allow_restart);
}

static ssize_t
allow_restart_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	bool v;
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (sdp->type != TYPE_DISK && sdp->type != TYPE_ZBC)
		return -EINVAL;

	if (kstrtobool(buf, &v))
		return -EINVAL;

	sdp->allow_restart = v;

	return count;
}
static DEVICE_ATTR_RW(allow_restart);

static ssize_t
cache_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	int ct = sdkp->RCD + 2*sdkp->WCE;

	return sprintf(buf, "%s\n", sd_cache_types[ct]);
}
static DEVICE_ATTR_RW(cache_type);

static ssize_t
FUA_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%u\n", sdkp->DPOFUA);
}
static DEVICE_ATTR_RO(FUA);

static ssize_t
protection_type_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%u\n", sdkp->protection_type);
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

	if (val <= T10_PI_TYPE3_PROTECTION)
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
		return sprintf(buf, "none\n");

	return sprintf(buf, "%s%u\n", dix ? "dix" : "dif", dif);
}
static DEVICE_ATTR_RO(protection_mode);

static ssize_t
app_tag_own_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%u\n", sdkp->ATO);
}
static DEVICE_ATTR_RO(app_tag_own);

static ssize_t
thin_provisioning_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%u\n", sdkp->lbpme);
}
static DEVICE_ATTR_RO(thin_provisioning);

/* sysfs_match_string() requires dense arrays */
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

	return sprintf(buf, "%s\n", lbp_mode[sdkp->provisioning_mode]);
}

static ssize_t
provisioning_mode_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdp = sdkp->device;
	int mode;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (sd_is_zoned(sdkp)) {
		sd_config_discard(sdkp, SD_LBP_DISABLE);
		return count;
	}

	if (sdp->type != TYPE_DISK)
		return -EINVAL;

	mode = sysfs_match_string(lbp_mode, buf);
	if (mode < 0)
		return -EINVAL;

	sd_config_discard(sdkp, mode);

	return count;
}
static DEVICE_ATTR_RW(provisioning_mode);

/* sysfs_match_string() requires dense arrays */
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

	return sprintf(buf, "%s\n", zeroing_mode[sdkp->zeroing_mode]);
}

static ssize_t
zeroing_mode_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	int mode;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	mode = sysfs_match_string(zeroing_mode, buf);
	if (mode < 0)
		return -EINVAL;

	sdkp->zeroing_mode = mode;

	return count;
}
static DEVICE_ATTR_RW(zeroing_mode);

static ssize_t
max_medium_access_timeouts_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%u\n", sdkp->max_medium_access_timeouts);
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

	return sprintf(buf, "%u\n", sdkp->max_ws_blocks);
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

static ssize_t
zoned_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	if (sdkp->device->type == TYPE_ZBC)
		return sprintf(buf, "host-managed\n");
	if (sdkp->zoned == 1)
		return sprintf(buf, "host-aware\n");
	if (sdkp->zoned == 2)
		return sprintf(buf, "drive-managed\n");
	return sprintf(buf, "none\n");
}
static DEVICE_ATTR_RO(zoned_cap);

static ssize_t
max_retries_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);
	struct scsi_device *sdev = sdkp->device;
	int retries, err;

	err = kstrtoint(buf, 10, &retries);
	if (err)
		return err;

	if (retries == SCSI_CMD_RETRIES_NO_LIMIT || retries <= SD_MAX_RETRIES) {
		sdkp->max_retries = retries;
		return count;
	}

	sdev_printk(KERN_ERR, sdev, "max_retries must be between -1 and %d\n",
		    SD_MAX_RETRIES);
	return -EINVAL;
}

static ssize_t
max_retries_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	return sprintf(buf, "%d\n", sdkp->max_retries);
}

static DEVICE_ATTR_RW(max_retries);

static struct attribute *sd_disk_attrs[] = {
	&dev_attr_cache_type.attr,
	&dev_attr_FUA.attr,
	&dev_attr_allow_restart.attr,
	&dev_attr_manage_start_stop.attr,
	&dev_attr_manage_system_start_stop.attr,
	&dev_attr_manage_runtime_start_stop.attr,
	&dev_attr_manage_shutdown.attr,
	&dev_attr_protection_type.attr,
	&dev_attr_protection_mode.attr,
	&dev_attr_app_tag_own.attr,
	&dev_attr_thin_provisioning.attr,
	&dev_attr_provisioning_mode.attr,
	&dev_attr_zeroing_mode.attr,
	&dev_attr_max_write_same_blocks.attr,
	&dev_attr_max_medium_access_timeouts.attr,
	&dev_attr_zoned_cap.attr,
	&dev_attr_max_retries.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sd_disk);

static struct class sd_disk_class = {
	.name		= "scsi_disk",
	.dev_release	= scsi_disk_release,
	.dev_groups	= sd_disk_groups,
};

/*
 * Don't request a new module, as that could deadlock in multipath
 * environment.
 */
static void sd_default_probe(dev_t devt)
{
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

#ifdef CONFIG_BLK_SED_OPAL
static int sd_sec_submit(void *data, u16 spsp, u8 secp, void *buffer,
		size_t len, bool send)
{
	struct scsi_disk *sdkp = data;
	struct scsi_device *sdev = sdkp->device;
	u8 cdb[12] = { 0, };
	const struct scsi_exec_args exec_args = {
		.req_flags = BLK_MQ_REQ_PM,
	};
	int ret;

	cdb[0] = send ? SECURITY_PROTOCOL_OUT : SECURITY_PROTOCOL_IN;
	cdb[1] = secp;
	put_unaligned_be16(spsp, &cdb[2]);
	put_unaligned_be32(len, &cdb[6]);

	ret = scsi_execute_cmd(sdev, cdb, send ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN,
			       buffer, len, SD_TIMEOUT, sdkp->max_retries,
			       &exec_args);
	return ret <= 0 ? ret : -EIO;
}
#endif /* CONFIG_BLK_SED_OPAL */

/*
 * Look up the DIX operation based on whether the command is read or
 * write and whether dix and dif are enabled.
 */
static unsigned int sd_prot_op(bool write, bool dix, bool dif)
{
	/* Lookup table: bit 2 (write), bit 1 (dix), bit 0 (dif) */
	static const unsigned int ops[] = {	/* wrt dix dif */
		SCSI_PROT_NORMAL,		/*  0	0   0  */
		SCSI_PROT_READ_STRIP,		/*  0	0   1  */
		SCSI_PROT_READ_INSERT,		/*  0	1   0  */
		SCSI_PROT_READ_PASS,		/*  0	1   1  */
		SCSI_PROT_NORMAL,		/*  1	0   0  */
		SCSI_PROT_WRITE_INSERT,		/*  1	0   1  */
		SCSI_PROT_WRITE_STRIP,		/*  1	1   0  */
		SCSI_PROT_WRITE_PASS,		/*  1	1   1  */
	};

	return ops[write << 2 | dix << 1 | dif];
}

/*
 * Returns a mask of the protection flags that are valid for a given DIX
 * operation.
 */
static unsigned int sd_prot_flag_mask(unsigned int prot_op)
{
	static const unsigned int flag_mask[] = {
		[SCSI_PROT_NORMAL]		= 0,

		[SCSI_PROT_READ_STRIP]		= SCSI_PROT_TRANSFER_PI |
						  SCSI_PROT_GUARD_CHECK |
						  SCSI_PROT_REF_CHECK |
						  SCSI_PROT_REF_INCREMENT,

		[SCSI_PROT_READ_INSERT]		= SCSI_PROT_REF_INCREMENT |
						  SCSI_PROT_IP_CHECKSUM,

		[SCSI_PROT_READ_PASS]		= SCSI_PROT_TRANSFER_PI |
						  SCSI_PROT_GUARD_CHECK |
						  SCSI_PROT_REF_CHECK |
						  SCSI_PROT_REF_INCREMENT |
						  SCSI_PROT_IP_CHECKSUM,

		[SCSI_PROT_WRITE_INSERT]	= SCSI_PROT_TRANSFER_PI |
						  SCSI_PROT_REF_INCREMENT,

		[SCSI_PROT_WRITE_STRIP]		= SCSI_PROT_GUARD_CHECK |
						  SCSI_PROT_REF_CHECK |
						  SCSI_PROT_REF_INCREMENT |
						  SCSI_PROT_IP_CHECKSUM,

		[SCSI_PROT_WRITE_PASS]		= SCSI_PROT_TRANSFER_PI |
						  SCSI_PROT_GUARD_CHECK |
						  SCSI_PROT_REF_CHECK |
						  SCSI_PROT_REF_INCREMENT |
						  SCSI_PROT_IP_CHECKSUM,
	};

	return flag_mask[prot_op];
}

static unsigned char sd_setup_protect_cmnd(struct scsi_cmnd *scmd,
					   unsigned int dix, unsigned int dif)
{
	struct request *rq = scsi_cmd_to_rq(scmd);
	struct bio *bio = rq->bio;
	unsigned int prot_op = sd_prot_op(rq_data_dir(rq), dix, dif);
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

	case SD_LBP_FULL:
	case SD_LBP_DISABLE:
		blk_queue_max_discard_sectors(q, 0);
		return;

	case SD_LBP_UNMAP:
		max_blocks = min_not_zero(sdkp->max_unmap_blocks,
					  (u32)SD_MAX_WS16_BLOCKS);
		break;

	case SD_LBP_WS16:
		if (sdkp->device->unmap_limit_for_ws)
			max_blocks = sdkp->max_unmap_blocks;
		else
			max_blocks = sdkp->max_ws_blocks;

		max_blocks = min_not_zero(max_blocks, (u32)SD_MAX_WS16_BLOCKS);
		break;

	case SD_LBP_WS10:
		if (sdkp->device->unmap_limit_for_ws)
			max_blocks = sdkp->max_unmap_blocks;
		else
			max_blocks = sdkp->max_ws_blocks;

		max_blocks = min_not_zero(max_blocks, (u32)SD_MAX_WS10_BLOCKS);
		break;

	case SD_LBP_ZERO:
		max_blocks = min_not_zero(sdkp->max_ws_blocks,
					  (u32)SD_MAX_WS10_BLOCKS);
		break;
	}

	blk_queue_max_discard_sectors(q, max_blocks * (logical_block_size >> 9));
}

static void *sd_set_special_bvec(struct request *rq, unsigned int data_len)
{
	struct page *page;

	page = mempool_alloc(sd_page_pool, GFP_ATOMIC);
	if (!page)
		return NULL;
	clear_highpage(page);
	bvec_set_page(&rq->special_vec, page, data_len, 0);
	rq->rq_flags |= RQF_SPECIAL_PAYLOAD;
	return bvec_virt(&rq->special_vec);
}

static blk_status_t sd_setup_unmap_cmnd(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdp = cmd->device;
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);
	u64 lba = sectors_to_logical(sdp, blk_rq_pos(rq));
	u32 nr_blocks = sectors_to_logical(sdp, blk_rq_sectors(rq));
	unsigned int data_len = 24;
	char *buf;

	buf = sd_set_special_bvec(rq, data_len);
	if (!buf)
		return BLK_STS_RESOURCE;

	cmd->cmd_len = 10;
	cmd->cmnd[0] = UNMAP;
	cmd->cmnd[8] = 24;

	put_unaligned_be16(6 + 16, &buf[0]);
	put_unaligned_be16(16, &buf[2]);
	put_unaligned_be64(lba, &buf[8]);
	put_unaligned_be32(nr_blocks, &buf[16]);

	cmd->allowed = sdkp->max_retries;
	cmd->transfersize = data_len;
	rq->timeout = SD_TIMEOUT;

	return scsi_alloc_sgtables(cmd);
}

static blk_status_t sd_setup_write_same16_cmnd(struct scsi_cmnd *cmd,
		bool unmap)
{
	struct scsi_device *sdp = cmd->device;
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);
	u64 lba = sectors_to_logical(sdp, blk_rq_pos(rq));
	u32 nr_blocks = sectors_to_logical(sdp, blk_rq_sectors(rq));
	u32 data_len = sdp->sector_size;

	if (!sd_set_special_bvec(rq, data_len))
		return BLK_STS_RESOURCE;

	cmd->cmd_len = 16;
	cmd->cmnd[0] = WRITE_SAME_16;
	if (unmap)
		cmd->cmnd[1] = 0x8; /* UNMAP */
	put_unaligned_be64(lba, &cmd->cmnd[2]);
	put_unaligned_be32(nr_blocks, &cmd->cmnd[10]);

	cmd->allowed = sdkp->max_retries;
	cmd->transfersize = data_len;
	rq->timeout = unmap ? SD_TIMEOUT : SD_WRITE_SAME_TIMEOUT;

	return scsi_alloc_sgtables(cmd);
}

static blk_status_t sd_setup_write_same10_cmnd(struct scsi_cmnd *cmd,
		bool unmap)
{
	struct scsi_device *sdp = cmd->device;
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);
	u64 lba = sectors_to_logical(sdp, blk_rq_pos(rq));
	u32 nr_blocks = sectors_to_logical(sdp, blk_rq_sectors(rq));
	u32 data_len = sdp->sector_size;

	if (!sd_set_special_bvec(rq, data_len))
		return BLK_STS_RESOURCE;

	cmd->cmd_len = 10;
	cmd->cmnd[0] = WRITE_SAME;
	if (unmap)
		cmd->cmnd[1] = 0x8; /* UNMAP */
	put_unaligned_be32(lba, &cmd->cmnd[2]);
	put_unaligned_be16(nr_blocks, &cmd->cmnd[7]);

	cmd->allowed = sdkp->max_retries;
	cmd->transfersize = data_len;
	rq->timeout = unmap ? SD_TIMEOUT : SD_WRITE_SAME_TIMEOUT;

	return scsi_alloc_sgtables(cmd);
}

static blk_status_t sd_setup_write_zeroes_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_device *sdp = cmd->device;
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);
	u64 lba = sectors_to_logical(sdp, blk_rq_pos(rq));
	u32 nr_blocks = sectors_to_logical(sdp, blk_rq_sectors(rq));

	if (!(rq->cmd_flags & REQ_NOUNMAP)) {
		switch (sdkp->zeroing_mode) {
		case SD_ZERO_WS16_UNMAP:
			return sd_setup_write_same16_cmnd(cmd, true);
		case SD_ZERO_WS10_UNMAP:
			return sd_setup_write_same10_cmnd(cmd, true);
		}
	}

	if (sdp->no_write_same) {
		rq->rq_flags |= RQF_QUIET;
		return BLK_STS_TARGET;
	}

	if (sdkp->ws16 || lba > 0xffffffff || nr_blocks > 0xffff)
		return sd_setup_write_same16_cmnd(cmd, false);

	return sd_setup_write_same10_cmnd(cmd, false);
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

	if (sdkp->max_ws_blocks &&
	    sdkp->physical_block_size > logical_block_size) {
		/*
		 * Reporting a maximum number of blocks that is not aligned
		 * on the device physical size would cause a large write same
		 * request to be split into physically unaligned chunks by
		 * __blkdev_issue_write_zeroes() even if the caller of this
		 * functions took care to align the large request. So make sure
		 * the maximum reported is aligned to the device physical block
		 * size. This is only an optional optimization for regular
		 * disks, but this is mandatory to avoid failure of large write
		 * same requests directed at sequential write required zones of
		 * host-managed ZBC disks.
		 */
		sdkp->max_ws_blocks =
			round_down(sdkp->max_ws_blocks,
				   bytes_to_logical(sdkp->device,
						    sdkp->physical_block_size));
	}

out:
	blk_queue_max_write_zeroes_sectors(q, sdkp->max_ws_blocks *
					 (logical_block_size >> 9));
}

static blk_status_t sd_setup_flush_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);

	/* flush requests don't perform I/O, zero the S/G table */
	memset(&cmd->sdb, 0, sizeof(cmd->sdb));

	if (cmd->device->use_16_for_sync) {
		cmd->cmnd[0] = SYNCHRONIZE_CACHE_16;
		cmd->cmd_len = 16;
	} else {
		cmd->cmnd[0] = SYNCHRONIZE_CACHE;
		cmd->cmd_len = 10;
	}
	cmd->transfersize = 0;
	cmd->allowed = sdkp->max_retries;

	rq->timeout = rq->q->rq_timeout * SD_FLUSH_TIMEOUT_MULTIPLIER;
	return BLK_STS_OK;
}

/**
 * sd_group_number() - Compute the GROUP NUMBER field
 * @cmd: SCSI command for which to compute the value of the six-bit GROUP NUMBER
 *	field.
 *
 * From SBC-5 r05 (https://www.t10.org/cgi-bin/ac.pl?t=f&f=sbc5r05.pdf):
 * 0: no relative lifetime.
 * 1: shortest relative lifetime.
 * 2: second shortest relative lifetime.
 * 3 - 0x3d: intermediate relative lifetimes.
 * 0x3e: second longest relative lifetime.
 * 0x3f: longest relative lifetime.
 */
static u8 sd_group_number(struct scsi_cmnd *cmd)
{
	const struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);

	if (!sdkp->rscs)
		return 0;

	return min3((u32)rq->write_hint, (u32)sdkp->permanent_stream_count,
		    0x3fu);
}

static blk_status_t sd_setup_rw32_cmnd(struct scsi_cmnd *cmd, bool write,
				       sector_t lba, unsigned int nr_blocks,
				       unsigned char flags, unsigned int dld)
{
	cmd->cmd_len = SD_EXT_CDB_SIZE;
	cmd->cmnd[0]  = VARIABLE_LENGTH_CMD;
	cmd->cmnd[6]  = sd_group_number(cmd);
	cmd->cmnd[7]  = 0x18; /* Additional CDB len */
	cmd->cmnd[9]  = write ? WRITE_32 : READ_32;
	cmd->cmnd[10] = flags;
	cmd->cmnd[11] = dld & 0x07;
	put_unaligned_be64(lba, &cmd->cmnd[12]);
	put_unaligned_be32(lba, &cmd->cmnd[20]); /* Expected Indirect LBA */
	put_unaligned_be32(nr_blocks, &cmd->cmnd[28]);

	return BLK_STS_OK;
}

static blk_status_t sd_setup_rw16_cmnd(struct scsi_cmnd *cmd, bool write,
				       sector_t lba, unsigned int nr_blocks,
				       unsigned char flags, unsigned int dld)
{
	cmd->cmd_len  = 16;
	cmd->cmnd[0]  = write ? WRITE_16 : READ_16;
	cmd->cmnd[1]  = flags | ((dld >> 2) & 0x01);
	cmd->cmnd[14] = ((dld & 0x03) << 6) | sd_group_number(cmd);
	cmd->cmnd[15] = 0;
	put_unaligned_be64(lba, &cmd->cmnd[2]);
	put_unaligned_be32(nr_blocks, &cmd->cmnd[10]);

	return BLK_STS_OK;
}

static blk_status_t sd_setup_rw10_cmnd(struct scsi_cmnd *cmd, bool write,
				       sector_t lba, unsigned int nr_blocks,
				       unsigned char flags)
{
	cmd->cmd_len = 10;
	cmd->cmnd[0] = write ? WRITE_10 : READ_10;
	cmd->cmnd[1] = flags;
	cmd->cmnd[6] = sd_group_number(cmd);
	cmd->cmnd[9] = 0;
	put_unaligned_be32(lba, &cmd->cmnd[2]);
	put_unaligned_be16(nr_blocks, &cmd->cmnd[7]);

	return BLK_STS_OK;
}

static blk_status_t sd_setup_rw6_cmnd(struct scsi_cmnd *cmd, bool write,
				      sector_t lba, unsigned int nr_blocks,
				      unsigned char flags)
{
	/* Avoid that 0 blocks gets translated into 256 blocks. */
	if (WARN_ON_ONCE(nr_blocks == 0))
		return BLK_STS_IOERR;

	if (unlikely(flags & 0x8)) {
		/*
		 * This happens only if this drive failed 10byte rw
		 * command with ILLEGAL_REQUEST during operation and
		 * thus turned off use_10_for_rw.
		 */
		scmd_printk(KERN_ERR, cmd, "FUA write on READ/WRITE(6) drive\n");
		return BLK_STS_IOERR;
	}

	cmd->cmd_len = 6;
	cmd->cmnd[0] = write ? WRITE_6 : READ_6;
	cmd->cmnd[1] = (lba >> 16) & 0x1f;
	cmd->cmnd[2] = (lba >> 8) & 0xff;
	cmd->cmnd[3] = lba & 0xff;
	cmd->cmnd[4] = nr_blocks;
	cmd->cmnd[5] = 0;

	return BLK_STS_OK;
}

/*
 * Check if a command has a duration limit set. If it does, and the target
 * device supports CDL and the feature is enabled, return the limit
 * descriptor index to use. Return 0 (no limit) otherwise.
 */
static int sd_cdl_dld(struct scsi_disk *sdkp, struct scsi_cmnd *scmd)
{
	struct scsi_device *sdp = sdkp->device;
	int hint;

	if (!sdp->cdl_supported || !sdp->cdl_enable)
		return 0;

	/*
	 * Use "no limit" if the request ioprio does not specify a duration
	 * limit hint.
	 */
	hint = IOPRIO_PRIO_HINT(req_get_ioprio(scsi_cmd_to_rq(scmd)));
	if (hint < IOPRIO_HINT_DEV_DURATION_LIMIT_1 ||
	    hint > IOPRIO_HINT_DEV_DURATION_LIMIT_7)
		return 0;

	return (hint - IOPRIO_HINT_DEV_DURATION_LIMIT_1) + 1;
}

static blk_status_t sd_setup_read_write_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct scsi_device *sdp = cmd->device;
	struct scsi_disk *sdkp = scsi_disk(rq->q->disk);
	sector_t lba = sectors_to_logical(sdp, blk_rq_pos(rq));
	sector_t threshold;
	unsigned int nr_blocks = sectors_to_logical(sdp, blk_rq_sectors(rq));
	unsigned int mask = logical_to_sectors(sdp, 1) - 1;
	bool write = rq_data_dir(rq) == WRITE;
	unsigned char protect, fua;
	unsigned int dld;
	blk_status_t ret;
	unsigned int dif;
	bool dix;

	ret = scsi_alloc_sgtables(cmd);
	if (ret != BLK_STS_OK)
		return ret;

	ret = BLK_STS_IOERR;
	if (!scsi_device_online(sdp) || sdp->changed) {
		scmd_printk(KERN_ERR, cmd, "device offline or changed\n");
		goto fail;
	}

	if (blk_rq_pos(rq) + blk_rq_sectors(rq) > get_capacity(rq->q->disk)) {
		scmd_printk(KERN_ERR, cmd, "access beyond end of device\n");
		goto fail;
	}

	if ((blk_rq_pos(rq) & mask) || (blk_rq_sectors(rq) & mask)) {
		scmd_printk(KERN_ERR, cmd, "request not aligned to the logical block size\n");
		goto fail;
	}

	/*
	 * Some SD card readers can't handle accesses which touch the
	 * last one or two logical blocks. Split accesses as needed.
	 */
	threshold = sdkp->capacity - SD_LAST_BUGGY_SECTORS;

	if (unlikely(sdp->last_sector_bug && lba + nr_blocks > threshold)) {
		if (lba < threshold) {
			/* Access up to the threshold but not beyond */
			nr_blocks = threshold - lba;
		} else {
			/* Access only a single logical block */
			nr_blocks = 1;
		}
	}

	if (req_op(rq) == REQ_OP_ZONE_APPEND) {
		ret = sd_zbc_prepare_zone_append(cmd, &lba, nr_blocks);
		if (ret)
			goto fail;
	}

	fua = rq->cmd_flags & REQ_FUA ? 0x8 : 0;
	dix = scsi_prot_sg_count(cmd);
	dif = scsi_host_dif_capable(cmd->device->host, sdkp->protection_type);
	dld = sd_cdl_dld(sdkp, cmd);

	if (dif || dix)
		protect = sd_setup_protect_cmnd(cmd, dix, dif);
	else
		protect = 0;

	if (protect && sdkp->protection_type == T10_PI_TYPE2_PROTECTION) {
		ret = sd_setup_rw32_cmnd(cmd, write, lba, nr_blocks,
					 protect | fua, dld);
	} else if (sdp->use_16_for_rw || (nr_blocks > 0xffff)) {
		ret = sd_setup_rw16_cmnd(cmd, write, lba, nr_blocks,
					 protect | fua, dld);
	} else if ((nr_blocks > 0xff) || (lba > 0x1fffff) ||
		   sdp->use_10_for_rw || protect || rq->write_hint) {
		ret = sd_setup_rw10_cmnd(cmd, write, lba, nr_blocks,
					 protect | fua);
	} else {
		ret = sd_setup_rw6_cmnd(cmd, write, lba, nr_blocks,
					protect | fua);
	}

	if (unlikely(ret != BLK_STS_OK))
		goto fail;

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	cmd->transfersize = sdp->sector_size;
	cmd->underflow = nr_blocks << 9;
	cmd->allowed = sdkp->max_retries;
	cmd->sdb.length = nr_blocks * sdp->sector_size;

	SCSI_LOG_HLQUEUE(1,
			 scmd_printk(KERN_INFO, cmd,
				     "%s: block=%llu, count=%d\n", __func__,
				     (unsigned long long)blk_rq_pos(rq),
				     blk_rq_sectors(rq)));
	SCSI_LOG_HLQUEUE(2,
			 scmd_printk(KERN_INFO, cmd,
				     "%s %d/%u 512 byte blocks.\n",
				     write ? "writing" : "reading", nr_blocks,
				     blk_rq_sectors(rq)));

	/*
	 * This indicates that the command is ready from our end to be queued.
	 */
	return BLK_STS_OK;
fail:
	scsi_free_sgtables(cmd);
	return ret;
}

static blk_status_t sd_init_command(struct scsi_cmnd *cmd)
{
	struct request *rq = scsi_cmd_to_rq(cmd);

	switch (req_op(rq)) {
	case REQ_OP_DISCARD:
		switch (scsi_disk(rq->q->disk)->provisioning_mode) {
		case SD_LBP_UNMAP:
			return sd_setup_unmap_cmnd(cmd);
		case SD_LBP_WS16:
			return sd_setup_write_same16_cmnd(cmd, true);
		case SD_LBP_WS10:
			return sd_setup_write_same10_cmnd(cmd, true);
		case SD_LBP_ZERO:
			return sd_setup_write_same10_cmnd(cmd, false);
		default:
			return BLK_STS_TARGET;
		}
	case REQ_OP_WRITE_ZEROES:
		return sd_setup_write_zeroes_cmnd(cmd);
	case REQ_OP_FLUSH:
		return sd_setup_flush_cmnd(cmd);
	case REQ_OP_READ:
	case REQ_OP_WRITE:
	case REQ_OP_ZONE_APPEND:
		return sd_setup_read_write_cmnd(cmd);
	case REQ_OP_ZONE_RESET:
		return sd_zbc_setup_zone_mgmt_cmnd(cmd, ZO_RESET_WRITE_POINTER,
						   false);
	case REQ_OP_ZONE_RESET_ALL:
		return sd_zbc_setup_zone_mgmt_cmnd(cmd, ZO_RESET_WRITE_POINTER,
						   true);
	case REQ_OP_ZONE_OPEN:
		return sd_zbc_setup_zone_mgmt_cmnd(cmd, ZO_OPEN_ZONE, false);
	case REQ_OP_ZONE_CLOSE:
		return sd_zbc_setup_zone_mgmt_cmnd(cmd, ZO_CLOSE_ZONE, false);
	case REQ_OP_ZONE_FINISH:
		return sd_zbc_setup_zone_mgmt_cmnd(cmd, ZO_FINISH_ZONE, false);
	default:
		WARN_ON_ONCE(1);
		return BLK_STS_NOTSUPP;
	}
}

static void sd_uninit_command(struct scsi_cmnd *SCpnt)
{
	struct request *rq = scsi_cmd_to_rq(SCpnt);

	if (rq->rq_flags & RQF_SPECIAL_PAYLOAD)
		mempool_free(rq->special_vec.bv_page, sd_page_pool);
}

static bool sd_need_revalidate(struct gendisk *disk, struct scsi_disk *sdkp)
{
	if (sdkp->device->removable || sdkp->write_prot) {
		if (disk_check_media_change(disk))
			return true;
	}

	/*
	 * Force a full rescan after ioctl(BLKRRPART).  While the disk state has
	 * nothing to do with partitions, BLKRRPART is used to force a full
	 * revalidate after things like a format for historical reasons.
	 */
	return test_bit(GD_NEED_PART_SCAN, &disk->state);
}

/**
 *	sd_open - open a scsi disk device
 *	@disk: disk to open
 *	@mode: open mode
 *
 *	Returns 0 if successful. Returns a negated errno value in case 
 *	of error.
 *
 *	Note: This can be called from a user context (e.g. fsck(1) )
 *	or from within the kernel (e.g. as a result of a mount(1) ).
 *	In the latter case @inode and @filp carry an abridged amount
 *	of information as noted above.
 *
 *	Locking: called with disk->open_mutex held.
 **/
static int sd_open(struct gendisk *disk, blk_mode_t mode)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdev = sdkp->device;
	int retval;

	if (scsi_device_get(sdev))
		return -ENXIO;

	SCSI_LOG_HLQUEUE(3, sd_printk(KERN_INFO, sdkp, "sd_open\n"));

	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	retval = -ENXIO;
	if (!scsi_block_when_processing_errors(sdev))
		goto error_out;

	if (sd_need_revalidate(disk, sdkp))
		sd_revalidate_disk(disk);

	/*
	 * If the drive is empty, just let the open fail.
	 */
	retval = -ENOMEDIUM;
	if (sdev->removable && !sdkp->media_present &&
	    !(mode & BLK_OPEN_NDELAY))
		goto error_out;

	/*
	 * If the device has the write protect tab set, have the open fail
	 * if the user expects to be able to write to the thing.
	 */
	retval = -EROFS;
	if (sdkp->write_prot && (mode & BLK_OPEN_WRITE))
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
	scsi_device_put(sdev);
	return retval;	
}

/**
 *	sd_release - invoked when the (last) close(2) is called on this
 *	scsi disk.
 *	@disk: disk to release
 *
 *	Returns 0. 
 *
 *	Note: may block (uninterruptible) if error recovery is underway
 *	on this disk.
 *
 *	Locking: called with disk->open_mutex held.
 **/
static void sd_release(struct gendisk *disk)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdev = sdkp->device;

	SCSI_LOG_HLQUEUE(3, sd_printk(KERN_INFO, sdkp, "sd_release\n"));

	if (atomic_dec_return(&sdkp->openers) == 0 && sdev->removable) {
		if (scsi_block_when_processing_errors(sdev))
			scsi_set_medium_removal(sdev, SCSI_REMOVAL_ALLOW);
	}

	scsi_device_put(sdev);
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
 *	@mode: open mode
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
static int sd_ioctl(struct block_device *bdev, blk_mode_t mode,
		    unsigned int cmd, unsigned long arg)
{
	struct gendisk *disk = bdev->bd_disk;
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdp = sdkp->device;
	void __user *p = (void __user *)arg;
	int error;
    
	SCSI_LOG_IOCTL(1, sd_printk(KERN_INFO, sdkp, "sd_ioctl: disk=%s, "
				    "cmd=0x%x\n", disk->disk_name, cmd));

	if (bdev_is_partition(bdev) && !capable(CAP_SYS_RAWIO))
		return -ENOIOCTLCMD;

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */
	error = scsi_ioctl_block_when_processing_errors(sdp, cmd,
			(mode & BLK_OPEN_NDELAY));
	if (error)
		return error;

	if (is_sed_ioctl(cmd))
		return sed_ioctl(sdkp->opal_dev, cmd, p);
	return scsi_ioctl(sdp, mode & BLK_OPEN_WRITE, cmd, p);
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
	struct scsi_disk *sdkp = disk->private_data;
	struct scsi_device *sdp;
	int retval;
	bool disk_changed;

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

		retval = scsi_test_unit_ready(sdp, SD_TIMEOUT, sdkp->max_retries,
					      &sshdr);

		/* failed to execute TUR, assume media not present */
		if (retval < 0 || host_byte(retval)) {
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
	disk_changed = sdp->changed;
	sdp->changed = 0;
	return disk_changed ? DISK_EVENT_MEDIA_CHANGE : 0;
}

static int sd_sync_cache(struct scsi_disk *sdkp)
{
	int res;
	struct scsi_device *sdp = sdkp->device;
	const int timeout = sdp->request_queue->rq_timeout
		* SD_FLUSH_TIMEOUT_MULTIPLIER;
	/* Leave the rest of the command zero to indicate flush everything. */
	const unsigned char cmd[16] = { sdp->use_16_for_sync ?
				SYNCHRONIZE_CACHE_16 : SYNCHRONIZE_CACHE };
	struct scsi_sense_hdr sshdr;
	struct scsi_failure failure_defs[] = {
		{
			.allowed = 3,
			.result = SCMD_FAILURE_RESULT_ANY,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = failure_defs,
	};
	const struct scsi_exec_args exec_args = {
		.req_flags = BLK_MQ_REQ_PM,
		.sshdr = &sshdr,
		.failures = &failures,
	};

	if (!scsi_device_online(sdp))
		return -ENODEV;

	res = scsi_execute_cmd(sdp, cmd, REQ_OP_DRV_IN, NULL, 0, timeout,
			       sdkp->max_retries, &exec_args);
	if (res) {
		sd_print_result(sdkp, "Synchronize Cache(10) failed", res);

		if (res < 0)
			return res;

		if (scsi_status_is_check_condition(res) &&
		    scsi_sense_valid(&sshdr)) {
			sd_print_sense_hdr(sdkp, &sshdr);

			/* we need to evaluate the error return  */
			if (sshdr.asc == 0x3a ||	/* medium not present */
			    sshdr.asc == 0x20 ||	/* invalid command */
			    (sshdr.asc == 0x74 && sshdr.ascq == 0x71))	/* drive is password locked */
				/* this is no error here */
				return 0;
			/*
			 * This drive doesn't support sync and there's not much
			 * we can do because this is called during shutdown
			 * or suspend so just return success so those operations
			 * can proceed.
			 */
			if (sshdr.sense_key == ILLEGAL_REQUEST)
				return 0;
		}

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

	sd_revalidate_disk(sdkp->disk);
}

static int sd_get_unique_id(struct gendisk *disk, u8 id[16],
		enum blk_unique_id type)
{
	struct scsi_device *sdev = scsi_disk(disk)->device;
	const struct scsi_vpd *vpd;
	const unsigned char *d;
	int ret = -ENXIO, len;

	rcu_read_lock();
	vpd = rcu_dereference(sdev->vpd_pg83);
	if (!vpd)
		goto out_unlock;

	ret = -EINVAL;
	for (d = vpd->data + 4; d < vpd->data + vpd->len; d += d[3] + 4) {
		/* we only care about designators with LU association */
		if (((d[1] >> 4) & 0x3) != 0x00)
			continue;
		if ((d[1] & 0xf) != type)
			continue;

		/*
		 * Only exit early if a 16-byte descriptor was found.  Otherwise
		 * keep looking as one with more entropy might still show up.
		 */
		len = d[3];
		if (len != 8 && len != 12 && len != 16)
			continue;
		ret = len;
		memcpy(id, d + 4, len);
		if (len == 16)
			break;
	}
out_unlock:
	rcu_read_unlock();
	return ret;
}

static int sd_scsi_to_pr_err(struct scsi_sense_hdr *sshdr, int result)
{
	switch (host_byte(result)) {
	case DID_TRANSPORT_MARGINAL:
	case DID_TRANSPORT_DISRUPTED:
	case DID_BUS_BUSY:
		return PR_STS_RETRY_PATH_FAILURE;
	case DID_NO_CONNECT:
		return PR_STS_PATH_FAILED;
	case DID_TRANSPORT_FAILFAST:
		return PR_STS_PATH_FAST_FAILED;
	}

	switch (status_byte(result)) {
	case SAM_STAT_RESERVATION_CONFLICT:
		return PR_STS_RESERVATION_CONFLICT;
	case SAM_STAT_CHECK_CONDITION:
		if (!scsi_sense_valid(sshdr))
			return PR_STS_IOERR;

		if (sshdr->sense_key == ILLEGAL_REQUEST &&
		    (sshdr->asc == 0x26 || sshdr->asc == 0x24))
			return -EINVAL;

		fallthrough;
	default:
		return PR_STS_IOERR;
	}
}

static int sd_pr_in_command(struct block_device *bdev, u8 sa,
			    unsigned char *data, int data_len)
{
	struct scsi_disk *sdkp = scsi_disk(bdev->bd_disk);
	struct scsi_device *sdev = sdkp->device;
	struct scsi_sense_hdr sshdr;
	u8 cmd[10] = { PERSISTENT_RESERVE_IN, sa };
	struct scsi_failure failure_defs[] = {
		{
			.sense = UNIT_ATTENTION,
			.asc = SCMD_FAILURE_ASC_ANY,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.allowed = 5,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = failure_defs,
	};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
		.failures = &failures,
	};
	int result;

	put_unaligned_be16(data_len, &cmd[7]);

	result = scsi_execute_cmd(sdev, cmd, REQ_OP_DRV_IN, data, data_len,
				  SD_TIMEOUT, sdkp->max_retries, &exec_args);
	if (scsi_status_is_check_condition(result) &&
	    scsi_sense_valid(&sshdr)) {
		sdev_printk(KERN_INFO, sdev, "PR command failed: %d\n", result);
		scsi_print_sense_hdr(sdev, NULL, &sshdr);
	}

	if (result <= 0)
		return result;

	return sd_scsi_to_pr_err(&sshdr, result);
}

static int sd_pr_read_keys(struct block_device *bdev, struct pr_keys *keys_info)
{
	int result, i, data_offset, num_copy_keys;
	u32 num_keys = keys_info->num_keys;
	int data_len = num_keys * 8 + 8;
	u8 *data;

	data = kzalloc(data_len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	result = sd_pr_in_command(bdev, READ_KEYS, data, data_len);
	if (result)
		goto free_data;

	keys_info->generation = get_unaligned_be32(&data[0]);
	keys_info->num_keys = get_unaligned_be32(&data[4]) / 8;

	data_offset = 8;
	num_copy_keys = min(num_keys, keys_info->num_keys);

	for (i = 0; i < num_copy_keys; i++) {
		keys_info->keys[i] = get_unaligned_be64(&data[data_offset]);
		data_offset += 8;
	}

free_data:
	kfree(data);
	return result;
}

static int sd_pr_read_reservation(struct block_device *bdev,
				  struct pr_held_reservation *rsv)
{
	struct scsi_disk *sdkp = scsi_disk(bdev->bd_disk);
	struct scsi_device *sdev = sdkp->device;
	u8 data[24] = { };
	int result, len;

	result = sd_pr_in_command(bdev, READ_RESERVATION, data, sizeof(data));
	if (result)
		return result;

	len = get_unaligned_be32(&data[4]);
	if (!len)
		return 0;

	/* Make sure we have at least the key and type */
	if (len < 14) {
		sdev_printk(KERN_INFO, sdev,
			    "READ RESERVATION failed due to short return buffer of %d bytes\n",
			    len);
		return -EINVAL;
	}

	rsv->generation = get_unaligned_be32(&data[0]);
	rsv->key = get_unaligned_be64(&data[8]);
	rsv->type = scsi_pr_type_to_block(data[21] & 0x0f);
	return 0;
}

static int sd_pr_out_command(struct block_device *bdev, u8 sa, u64 key,
			     u64 sa_key, enum scsi_pr_type type, u8 flags)
{
	struct scsi_disk *sdkp = scsi_disk(bdev->bd_disk);
	struct scsi_device *sdev = sdkp->device;
	struct scsi_sense_hdr sshdr;
	struct scsi_failure failure_defs[] = {
		{
			.sense = UNIT_ATTENTION,
			.asc = SCMD_FAILURE_ASC_ANY,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.allowed = 5,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = failure_defs,
	};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
		.failures = &failures,
	};
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

	result = scsi_execute_cmd(sdev, cmd, REQ_OP_DRV_OUT, &data,
				  sizeof(data), SD_TIMEOUT, sdkp->max_retries,
				  &exec_args);

	if (scsi_status_is_check_condition(result) &&
	    scsi_sense_valid(&sshdr)) {
		sdev_printk(KERN_INFO, sdev, "PR command failed: %d\n", result);
		scsi_print_sense_hdr(sdev, NULL, &sshdr);
	}

	if (result <= 0)
		return result;

	return sd_scsi_to_pr_err(&sshdr, result);
}

static int sd_pr_register(struct block_device *bdev, u64 old_key, u64 new_key,
		u32 flags)
{
	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;
	return sd_pr_out_command(bdev, (flags & PR_FL_IGNORE_KEY) ? 0x06 : 0x00,
			old_key, new_key, 0,
			(1 << 0) /* APTPL */);
}

static int sd_pr_reserve(struct block_device *bdev, u64 key, enum pr_type type,
		u32 flags)
{
	if (flags)
		return -EOPNOTSUPP;
	return sd_pr_out_command(bdev, 0x01, key, 0,
				 block_pr_type_to_scsi(type), 0);
}

static int sd_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	return sd_pr_out_command(bdev, 0x02, key, 0,
				 block_pr_type_to_scsi(type), 0);
}

static int sd_pr_preempt(struct block_device *bdev, u64 old_key, u64 new_key,
		enum pr_type type, bool abort)
{
	return sd_pr_out_command(bdev, abort ? 0x05 : 0x04, old_key, new_key,
				 block_pr_type_to_scsi(type), 0);
}

static int sd_pr_clear(struct block_device *bdev, u64 key)
{
	return sd_pr_out_command(bdev, 0x03, key, 0, 0, 0);
}

static const struct pr_ops sd_pr_ops = {
	.pr_register	= sd_pr_register,
	.pr_reserve	= sd_pr_reserve,
	.pr_release	= sd_pr_release,
	.pr_preempt	= sd_pr_preempt,
	.pr_clear	= sd_pr_clear,
	.pr_read_keys	= sd_pr_read_keys,
	.pr_read_reservation = sd_pr_read_reservation,
};

static void scsi_disk_free_disk(struct gendisk *disk)
{
	struct scsi_disk *sdkp = scsi_disk(disk);

	put_device(&sdkp->disk_dev);
}

static const struct block_device_operations sd_fops = {
	.owner			= THIS_MODULE,
	.open			= sd_open,
	.release		= sd_release,
	.ioctl			= sd_ioctl,
	.getgeo			= sd_getgeo,
	.compat_ioctl		= blkdev_compat_ptr_ioctl,
	.check_events		= sd_check_events,
	.unlock_native_capacity	= sd_unlock_native_capacity,
	.report_zones		= sd_zbc_report_zones,
	.get_unique_id		= sd_get_unique_id,
	.free_disk		= scsi_disk_free_disk,
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
	struct scsi_disk *sdkp = scsi_disk(scsi_cmd_to_rq(scmd)->q->disk);

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
	struct scsi_disk *sdkp = scsi_disk(scsi_cmd_to_rq(scmd)->q->disk);
	struct scsi_device *sdev = scmd->device;

	if (!scsi_device_online(sdev) ||
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
		mutex_lock(&sdev->state_mutex);
		scsi_device_set_state(sdev, SDEV_OFFLINE);
		mutex_unlock(&sdev->state_mutex);

		return SUCCESS;
	}

	return eh_disp;
}

static unsigned int sd_completed_bytes(struct scsi_cmnd *scmd)
{
	struct request *req = scsi_cmd_to_rq(scmd);
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
	struct request *req = scsi_cmd_to_rq(SCpnt);
	struct scsi_disk *sdkp = scsi_disk(req->q->disk);
	int sense_valid = 0;
	int sense_deferred = 0;

	switch (req_op(req)) {
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_RESET_ALL:
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
		if (!result) {
			good_bytes = blk_rq_bytes(req);
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
			scsi_print_command(SCpnt);
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

	if (!scsi_status_is_check_condition(result) &&
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
		good_bytes = sd_zbc_complete(SCpnt, good_bytes, &sshdr);

	SCSI_LOG_HLCOMPLETE(1, scmd_printk(KERN_INFO, SCpnt,
					   "sd_done: completed %d of %d bytes\n",
					   good_bytes, scsi_bufflen(SCpnt)));

	return good_bytes;
}

/*
 * spinup disk - called only in sd_revalidate_disk()
 */
static void
sd_spinup_disk(struct scsi_disk *sdkp)
{
	static const u8 cmd[10] = { TEST_UNIT_READY };
	unsigned long spintime_expire = 0;
	int spintime, sense_valid = 0;
	unsigned int the_result;
	struct scsi_sense_hdr sshdr;
	struct scsi_failure failure_defs[] = {
		/* Do not retry Medium Not Present */
		{
			.sense = UNIT_ATTENTION,
			.asc = 0x3A,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense = NOT_READY,
			.asc = 0x3A,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		/* Retry when scsi_status_is_good would return false 3 times */
		{
			.result = SCMD_FAILURE_STAT_ANY,
			.allowed = 3,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = failure_defs,
	};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
		.failures = &failures,
	};

	spintime = 0;

	/* Spin up drives, as required.  Only do this at boot time */
	/* Spinup needs to be done for module loads too. */
	do {
		bool media_was_present = sdkp->media_present;

		scsi_failures_reset_retries(&failures);

		the_result = scsi_execute_cmd(sdkp->device, cmd, REQ_OP_DRV_IN,
					      NULL, 0, SD_TIMEOUT,
					      sdkp->max_retries, &exec_args);


		if (the_result > 0) {
			/*
			 * If the drive has indicated to us that it doesn't
			 * have any media in it, don't bother with any more
			 * polling.
			 */
			if (media_not_present(sdkp, &sshdr)) {
				if (media_was_present)
					sd_printk(KERN_NOTICE, sdkp,
						  "Media removed, stopped polling\n");
				return;
			}
			sense_valid = scsi_sense_valid(&sshdr);
		}

		if (!scsi_status_is_check_condition(the_result)) {
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
			if (sshdr.asc == 4 && sshdr.ascq == 0x1b)
				break;	/* sanitize in progress */
			if (sshdr.asc == 4 && sshdr.ascq == 0x24)
				break;	/* depopulation in progress */
			if (sshdr.asc == 4 && sshdr.ascq == 0x25)
				break;	/* depopulation restoration in progress */
			/*
			 * Issue command to spin up drive when not ready
			 */
			if (!spintime) {
				/* Return immediately and start spin cycle */
				const u8 start_cmd[10] = {
					[0] = START_STOP,
					[1] = 1,
					[4] = sdkp->device->start_stop_pwr_cond ?
						0x11 : 1,
				};

				sd_printk(KERN_NOTICE, sdkp, "Spinning up disk...");
				scsi_execute_cmd(sdkp->device, start_cmd,
						 REQ_OP_DRV_IN, NULL, 0,
						 SD_TIMEOUT, sdkp->max_retries,
						 &exec_args);
				spintime_expire = jiffies + 100 * HZ;
				spintime = 1;
			}
			/* Wait 1 second for next try */
			msleep(1000);
			printk(KERN_CONT ".");

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
			printk(KERN_CONT "ready\n");
		else
			printk(KERN_CONT "not responding...\n");
	}
}

/*
 * Determine whether disk supports Data Integrity Field.
 */
static int sd_read_protection_type(struct scsi_disk *sdkp, unsigned char *buffer)
{
	struct scsi_device *sdp = sdkp->device;
	u8 type;

	if (scsi_device_protection(sdp) == 0 || (buffer[12] & 1) == 0) {
		sdkp->protection_type = 0;
		return 0;
	}

	type = ((buffer[12] >> 1) & 7) + 1; /* P_TYPE 0 = Type 1 */

	if (type > T10_PI_TYPE3_PROTECTION) {
		sd_printk(KERN_ERR, sdkp, "formatted with unsupported"	\
			  " protection type %u. Disabling disk!\n",
			  type);
		sdkp->protection_type = 0;
		return -ENODEV;
	}

	sdkp->protection_type = type;

	return 0;
}

static void sd_config_protection(struct scsi_disk *sdkp)
{
	struct scsi_device *sdp = sdkp->device;

	sd_dif_config_host(sdkp);

	if (!sdkp->protection_type)
		return;

	if (!scsi_host_dif_capable(sdp->host, sdkp->protection_type)) {
		sd_first_printk(KERN_NOTICE, sdkp,
				"Disabling DIF Type %u protection\n",
				sdkp->protection_type);
		sdkp->protection_type = 0;
	}

	sd_first_printk(KERN_NOTICE, sdkp, "Enabling DIF Type %u protection\n",
			sdkp->protection_type);
}

static void read_capacity_error(struct scsi_disk *sdkp, struct scsi_device *sdp,
			struct scsi_sense_hdr *sshdr, int sense_valid,
			int the_result)
{
	if (sense_valid)
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

static int read_capacity_16(struct scsi_disk *sdkp, struct scsi_device *sdp,
						unsigned char *buffer)
{
	unsigned char cmd[16];
	struct scsi_sense_hdr sshdr;
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
	};
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

		the_result = scsi_execute_cmd(sdp, cmd, REQ_OP_DRV_IN,
					      buffer, RC16_LEN, SD_TIMEOUT,
					      sdkp->max_retries, &exec_args);
		if (the_result > 0) {
			if (media_not_present(sdkp, &sshdr))
				return -ENODEV;

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
	static const u8 cmd[10] = { READ_CAPACITY };
	struct scsi_sense_hdr sshdr;
	struct scsi_failure failure_defs[] = {
		/* Do not retry Medium Not Present */
		{
			.sense = UNIT_ATTENTION,
			.asc = 0x3A,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense = NOT_READY,
			.asc = 0x3A,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		 /* Device reset might occur several times so retry a lot */
		{
			.sense = UNIT_ATTENTION,
			.asc = 0x29,
			.allowed = READ_CAPACITY_RETRIES_ON_RESET,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		/* Any other error not listed above retry 3 times */
		{
			.result = SCMD_FAILURE_RESULT_ANY,
			.allowed = 3,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = failure_defs,
	};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
		.failures = &failures,
	};
	int sense_valid = 0;
	int the_result;
	sector_t lba;
	unsigned sector_size;

	memset(buffer, 0, 8);

	the_result = scsi_execute_cmd(sdp, cmd, REQ_OP_DRV_IN, buffer,
				      8, SD_TIMEOUT, sdkp->max_retries,
				      &exec_args);

	if (the_result > 0) {
		sense_valid = scsi_sense_valid(&sshdr);

		if (media_not_present(sdkp, &sshdr))
			return -ENODEV;
	}

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
			/* Remember that READ CAPACITY(16) succeeded */
			sdp->try_rc_10_first = 0;
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

	if (!sdkp->first_scan && old_capacity == sdkp->capacity)
		return;

	string_get_size(sdkp->capacity, sector_size,
			STRING_UNITS_2, cap_str_2, sizeof(cap_str_2));
	string_get_size(sdkp->capacity, sector_size,
			STRING_UNITS_10, cap_str_10, sizeof(cap_str_10));

	sd_printk(KERN_NOTICE, sdkp,
		  "%llu %d-byte logical blocks: (%s/%s)\n",
		  (unsigned long long)sdkp->capacity,
		  sector_size, cap_str_10, cap_str_2);

	if (sdkp->physical_block_size != sector_size)
		sd_printk(KERN_NOTICE, sdkp,
			  "%u-byte physical blocks\n",
			  sdkp->physical_block_size);
}

/* called with buffer of length 512 */
static inline int
sd_do_mode_sense(struct scsi_disk *sdkp, int dbd, int modepage,
		 unsigned char *buffer, int len, struct scsi_mode_data *data,
		 struct scsi_sense_hdr *sshdr)
{
	/*
	 * If we must use MODE SENSE(10), make sure that the buffer length
	 * is at least 8 bytes so that the mode sense header fits.
	 */
	if (sdkp->device->use_10_for_ms && len < 8)
		len = 8;

	return scsi_mode_sense(sdkp->device, dbd, modepage, 0, buffer, len,
			       SD_TIMEOUT, sdkp->max_retries, data, sshdr);
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
		res = sd_do_mode_sense(sdkp, 0, 0x3F, buffer, 192, &data, NULL);
	} else {
		/*
		 * First attempt: ask for all pages (0x3F), but only 4 bytes.
		 * We have to start carefully: some devices hang if we ask
		 * for more than is available.
		 */
		res = sd_do_mode_sense(sdkp, 0, 0x3F, buffer, 4, &data, NULL);

		/*
		 * Second attempt: ask for page 0 When only page 0 is
		 * implemented, a request for page 3F may return Sense Key
		 * 5: Illegal Request, Sense Code 24: Invalid field in
		 * CDB.
		 */
		if (res < 0)
			res = sd_do_mode_sense(sdkp, 0, 0, buffer, 4, &data, NULL);

		/*
		 * Third attempt: ask 255 bytes, as we did earlier.
		 */
		if (res < 0)
			res = sd_do_mode_sense(sdkp, 0, 0x3F, buffer, 255,
					       &data, NULL);
	}

	if (res < 0) {
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
	res = sd_do_mode_sense(sdkp, dbd, modepage, buffer, first_len,
			&data, &sshdr);

	if (res < 0)
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
		res = sd_do_mode_sense(sdkp, dbd, modepage, buffer, len,
				&data, &sshdr);

	if (!res) {
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

		sd_first_printk(KERN_WARNING, sdkp,
				"No Caching mode page found\n");
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
	if (res == -EIO && scsi_sense_valid(&sshdr) &&
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
		sd_first_printk(KERN_WARNING, sdkp,
				"Assuming drive cache: write through\n");
		sdkp->WCE = 0;
	}
	sdkp->RCD = 0;
	sdkp->DPOFUA = 0;
}

static bool sd_is_perm_stream(struct scsi_disk *sdkp, unsigned int stream_id)
{
	u8 cdb[16] = { SERVICE_ACTION_IN_16, SAI_GET_STREAM_STATUS };
	struct {
		struct scsi_stream_status_header h;
		struct scsi_stream_status s;
	} buf;
	struct scsi_device *sdev = sdkp->device;
	struct scsi_sense_hdr sshdr;
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
	};
	int res;

	put_unaligned_be16(stream_id, &cdb[4]);
	put_unaligned_be32(sizeof(buf), &cdb[10]);

	res = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_IN, &buf, sizeof(buf),
			       SD_TIMEOUT, sdkp->max_retries, &exec_args);
	if (res < 0)
		return false;
	if (scsi_status_is_check_condition(res) && scsi_sense_valid(&sshdr))
		sd_print_sense_hdr(sdkp, &sshdr);
	if (res)
		return false;
	if (get_unaligned_be32(&buf.h.len) < sizeof(struct scsi_stream_status))
		return false;
	return buf.h.stream_status[0].perm;
}

static void sd_read_io_hints(struct scsi_disk *sdkp, unsigned char *buffer)
{
	struct scsi_device *sdp = sdkp->device;
	const struct scsi_io_group_descriptor *desc, *start, *end;
	struct scsi_sense_hdr sshdr;
	struct scsi_mode_data data;
	int res;

	res = scsi_mode_sense(sdp, /*dbd=*/0x8, /*modepage=*/0x0a,
			      /*subpage=*/0x05, buffer, SD_BUF_SIZE, SD_TIMEOUT,
			      sdkp->max_retries, &data, &sshdr);
	if (res < 0)
		return;
	start = (void *)buffer + data.header_length + 16;
	end = (void *)buffer + ALIGN_DOWN(data.header_length + data.length,
					  sizeof(*end));
	/*
	 * From "SBC-5 Constrained Streams with Data Lifetimes": Device severs
	 * should assign the lowest numbered stream identifiers to permanent
	 * streams.
	 */
	for (desc = start; desc < end; desc++)
		if (!desc->st_enble || !sd_is_perm_stream(sdkp, desc - start))
			break;
	sdkp->permanent_stream_count = desc - start;
	if (sdkp->rscs && sdkp->permanent_stream_count < 2)
		sd_printk(KERN_INFO, sdkp,
			  "Unexpected: RSCS has been set and the permanent stream count is %u\n",
			  sdkp->permanent_stream_count);
	else if (sdkp->permanent_stream_count)
		sd_printk(KERN_INFO, sdkp, "permanent stream count = %d\n",
			  sdkp->permanent_stream_count);
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

	res = scsi_mode_sense(sdp, 1, 0x0a, 0, buffer, 36, SD_TIMEOUT,
			      sdkp->max_retries, &data, &sshdr);

	if (res < 0 || !data.header_length ||
	    data.length < 6) {
		sd_first_printk(KERN_WARNING, sdkp,
			  "getting Control mode page failed, assume no ATO\n");

		if (res == -EIO && scsi_sense_valid(&sshdr))
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
	struct scsi_vpd *vpd;

	rcu_read_lock();

	vpd = rcu_dereference(sdkp->device->vpd_pgb0);
	if (!vpd || vpd->len < 16)
		goto out;

	sdkp->min_xfer_blocks = get_unaligned_be16(&vpd->data[6]);
	sdkp->max_xfer_blocks = get_unaligned_be32(&vpd->data[8]);
	sdkp->opt_xfer_blocks = get_unaligned_be32(&vpd->data[12]);

	if (vpd->len >= 64) {
		unsigned int lba_count, desc_count;

		sdkp->max_ws_blocks = (u32)get_unaligned_be64(&vpd->data[36]);

		if (!sdkp->lbpme)
			goto out;

		lba_count = get_unaligned_be32(&vpd->data[20]);
		desc_count = get_unaligned_be32(&vpd->data[24]);

		if (lba_count && desc_count)
			sdkp->max_unmap_blocks = lba_count;

		sdkp->unmap_granularity = get_unaligned_be32(&vpd->data[28]);

		if (vpd->data[32] & 0x80)
			sdkp->unmap_alignment =
				get_unaligned_be32(&vpd->data[32]) & ~(1 << 31);

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
			else
				sd_config_discard(sdkp, SD_LBP_DISABLE);
		}
	}

 out:
	rcu_read_unlock();
}

/* Parse the Block Limits Extension VPD page (0xb7) */
static void sd_read_block_limits_ext(struct scsi_disk *sdkp)
{
	struct scsi_vpd *vpd;

	rcu_read_lock();
	vpd = rcu_dereference(sdkp->device->vpd_pgb7);
	if (vpd && vpd->len >= 2)
		sdkp->rscs = vpd->data[5] & 1;
	rcu_read_unlock();
}

/**
 * sd_read_block_characteristics - Query block dev. characteristics
 * @sdkp: disk to query
 */
static void sd_read_block_characteristics(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;
	struct scsi_vpd *vpd;
	u16 rot;

	rcu_read_lock();
	vpd = rcu_dereference(sdkp->device->vpd_pgb1);

	if (!vpd || vpd->len < 8) {
		rcu_read_unlock();
	        return;
	}

	rot = get_unaligned_be16(&vpd->data[4]);
	sdkp->zoned = (vpd->data[8] >> 4) & 3;
	rcu_read_unlock();

	if (rot == 1) {
		blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
		blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, q);
	}


#ifdef CONFIG_BLK_DEV_ZONED /* sd_probe rejects ZBD devices early otherwise */
	if (sdkp->device->type == TYPE_ZBC) {
		/*
		 * Host-managed.
		 */
		disk_set_zoned(sdkp->disk);

		/*
		 * Per ZBC and ZAC specifications, writes in sequential write
		 * required zones of host-managed devices must be aligned to
		 * the device physical block size.
		 */
		blk_queue_zone_write_granularity(q, sdkp->physical_block_size);
	} else {
		/*
		 * Host-aware devices are treated as conventional.
		 */
		WARN_ON_ONCE(blk_queue_is_zoned(q));
	}
#endif /* CONFIG_BLK_DEV_ZONED */

	if (!sdkp->first_scan)
		return;

	if (blk_queue_is_zoned(q))
		sd_printk(KERN_NOTICE, sdkp, "Host-managed zoned block device\n");
	else if (sdkp->zoned == 1)
		sd_printk(KERN_NOTICE, sdkp, "Host-aware SMR disk used as regular disk\n");
	else if (sdkp->zoned == 2)
		sd_printk(KERN_NOTICE, sdkp, "Drive-managed SMR disk\n");
}

/**
 * sd_read_block_provisioning - Query provisioning VPD page
 * @sdkp: disk to query
 */
static void sd_read_block_provisioning(struct scsi_disk *sdkp)
{
	struct scsi_vpd *vpd;

	if (sdkp->lbpme == 0)
		return;

	rcu_read_lock();
	vpd = rcu_dereference(sdkp->device->vpd_pgb2);

	if (!vpd || vpd->len < 8) {
		rcu_read_unlock();
		return;
	}

	sdkp->lbpvpd	= 1;
	sdkp->lbpu	= (vpd->data[5] >> 7) & 1; /* UNMAP */
	sdkp->lbpws	= (vpd->data[5] >> 6) & 1; /* WRITE SAME(16) w/ UNMAP */
	sdkp->lbpws10	= (vpd->data[5] >> 5) & 1; /* WRITE SAME(10) w/ UNMAP */
	rcu_read_unlock();
}

static void sd_read_write_same(struct scsi_disk *sdkp, unsigned char *buffer)
{
	struct scsi_device *sdev = sdkp->device;

	if (sdev->host->no_write_same) {
		sdev->no_write_same = 1;

		return;
	}

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE, INQUIRY, 0) < 0) {
		struct scsi_vpd *vpd;

		sdev->no_report_opcodes = 1;

		/* Disable WRITE SAME if REPORT SUPPORTED OPERATION
		 * CODES is unsupported and the device has an ATA
		 * Information VPD page (SAT).
		 */
		rcu_read_lock();
		vpd = rcu_dereference(sdev->vpd_pg89);
		if (vpd)
			sdev->no_write_same = 1;
		rcu_read_unlock();
	}

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE, WRITE_SAME_16, 0) == 1)
		sdkp->ws16 = 1;

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE, WRITE_SAME, 0) == 1)
		sdkp->ws10 = 1;
}

static void sd_read_security(struct scsi_disk *sdkp, unsigned char *buffer)
{
	struct scsi_device *sdev = sdkp->device;

	if (!sdev->security_supported)
		return;

	if (scsi_report_opcode(sdev, buffer, SD_BUF_SIZE,
			SECURITY_PROTOCOL_IN, 0) == 1 &&
	    scsi_report_opcode(sdev, buffer, SD_BUF_SIZE,
			SECURITY_PROTOCOL_OUT, 0) == 1)
		sdkp->security = 1;
}

static inline sector_t sd64_to_sectors(struct scsi_disk *sdkp, u8 *buf)
{
	return logical_to_sectors(sdkp->device, get_unaligned_be64(buf));
}

/**
 * sd_read_cpr - Query concurrent positioning ranges
 * @sdkp:	disk to query
 */
static void sd_read_cpr(struct scsi_disk *sdkp)
{
	struct blk_independent_access_ranges *iars = NULL;
	unsigned char *buffer = NULL;
	unsigned int nr_cpr = 0;
	int i, vpd_len, buf_len = SD_BUF_SIZE;
	u8 *desc;

	/*
	 * We need to have the capacity set first for the block layer to be
	 * able to check the ranges.
	 */
	if (sdkp->first_scan)
		return;

	if (!sdkp->capacity)
		goto out;

	/*
	 * Concurrent Positioning Ranges VPD: there can be at most 256 ranges,
	 * leading to a maximum page size of 64 + 256*32 bytes.
	 */
	buf_len = 64 + 256*32;
	buffer = kmalloc(buf_len, GFP_KERNEL);
	if (!buffer || scsi_get_vpd_page(sdkp->device, 0xb9, buffer, buf_len))
		goto out;

	/* We must have at least a 64B header and one 32B range descriptor */
	vpd_len = get_unaligned_be16(&buffer[2]) + 4;
	if (vpd_len > buf_len || vpd_len < 64 + 32 || (vpd_len & 31)) {
		sd_printk(KERN_ERR, sdkp,
			  "Invalid Concurrent Positioning Ranges VPD page\n");
		goto out;
	}

	nr_cpr = (vpd_len - 64) / 32;
	if (nr_cpr == 1) {
		nr_cpr = 0;
		goto out;
	}

	iars = disk_alloc_independent_access_ranges(sdkp->disk, nr_cpr);
	if (!iars) {
		nr_cpr = 0;
		goto out;
	}

	desc = &buffer[64];
	for (i = 0; i < nr_cpr; i++, desc += 32) {
		if (desc[0] != i) {
			sd_printk(KERN_ERR, sdkp,
				"Invalid Concurrent Positioning Range number\n");
			nr_cpr = 0;
			break;
		}

		iars->ia_range[i].sector = sd64_to_sectors(sdkp, desc + 8);
		iars->ia_range[i].nr_sectors = sd64_to_sectors(sdkp, desc + 16);
	}

out:
	disk_set_independent_access_ranges(sdkp->disk, iars);
	if (nr_cpr && sdkp->nr_actuators != nr_cpr) {
		sd_printk(KERN_NOTICE, sdkp,
			  "%u concurrent positioning ranges\n", nr_cpr);
		sdkp->nr_actuators = nr_cpr;
	}

	kfree(buffer);
}

static bool sd_validate_min_xfer_size(struct scsi_disk *sdkp)
{
	struct scsi_device *sdp = sdkp->device;
	unsigned int min_xfer_bytes =
		logical_to_bytes(sdp, sdkp->min_xfer_blocks);

	if (sdkp->min_xfer_blocks == 0)
		return false;

	if (min_xfer_bytes & (sdkp->physical_block_size - 1)) {
		sd_first_printk(KERN_WARNING, sdkp,
				"Preferred minimum I/O size %u bytes not a " \
				"multiple of physical block size (%u bytes)\n",
				min_xfer_bytes, sdkp->physical_block_size);
		sdkp->min_xfer_blocks = 0;
		return false;
	}

	sd_first_printk(KERN_INFO, sdkp, "Preferred minimum I/O size %u bytes\n",
			min_xfer_bytes);
	return true;
}

/*
 * Determine the device's preferred I/O size for reads and writes
 * unless the reported value is unreasonably small, large, not a
 * multiple of the physical block size, or simply garbage.
 */
static bool sd_validate_opt_xfer_size(struct scsi_disk *sdkp,
				      unsigned int dev_max)
{
	struct scsi_device *sdp = sdkp->device;
	unsigned int opt_xfer_bytes =
		logical_to_bytes(sdp, sdkp->opt_xfer_blocks);
	unsigned int min_xfer_bytes =
		logical_to_bytes(sdp, sdkp->min_xfer_blocks);

	if (sdkp->opt_xfer_blocks == 0)
		return false;

	if (sdkp->opt_xfer_blocks > dev_max) {
		sd_first_printk(KERN_WARNING, sdkp,
				"Optimal transfer size %u logical blocks " \
				"> dev_max (%u logical blocks)\n",
				sdkp->opt_xfer_blocks, dev_max);
		return false;
	}

	if (sdkp->opt_xfer_blocks > SD_DEF_XFER_BLOCKS) {
		sd_first_printk(KERN_WARNING, sdkp,
				"Optimal transfer size %u logical blocks " \
				"> sd driver limit (%u logical blocks)\n",
				sdkp->opt_xfer_blocks, SD_DEF_XFER_BLOCKS);
		return false;
	}

	if (opt_xfer_bytes < PAGE_SIZE) {
		sd_first_printk(KERN_WARNING, sdkp,
				"Optimal transfer size %u bytes < " \
				"PAGE_SIZE (%u bytes)\n",
				opt_xfer_bytes, (unsigned int)PAGE_SIZE);
		return false;
	}

	if (min_xfer_bytes && opt_xfer_bytes % min_xfer_bytes) {
		sd_first_printk(KERN_WARNING, sdkp,
				"Optimal transfer size %u bytes not a " \
				"multiple of preferred minimum block " \
				"size (%u bytes)\n",
				opt_xfer_bytes, min_xfer_bytes);
		return false;
	}

	if (opt_xfer_bytes & (sdkp->physical_block_size - 1)) {
		sd_first_printk(KERN_WARNING, sdkp,
				"Optimal transfer size %u bytes not a " \
				"multiple of physical block size (%u bytes)\n",
				opt_xfer_bytes, sdkp->physical_block_size);
		return false;
	}

	sd_first_printk(KERN_INFO, sdkp, "Optimal transfer size %u bytes\n",
			opt_xfer_bytes);
	return true;
}

static void sd_read_block_zero(struct scsi_disk *sdkp)
{
	unsigned int buf_len = sdkp->device->sector_size;
	char *buffer, cmd[10] = { };

	buffer = kmalloc(buf_len, GFP_KERNEL);
	if (!buffer)
		return;

	cmd[0] = READ_10;
	put_unaligned_be32(0, &cmd[2]); /* Logical block address 0 */
	put_unaligned_be16(1, &cmd[7]);	/* Transfer 1 logical block */

	scsi_execute_cmd(sdkp->device, cmd, REQ_OP_DRV_IN, buffer, buf_len,
			 SD_TIMEOUT, sdkp->max_retries, NULL);
	kfree(buffer);
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
		/*
		 * Some USB/UAS devices return generic values for mode pages
		 * until the media has been accessed. Trigger a READ operation
		 * to force the device to populate mode pages.
		 */
		if (sdp->read_before_ms)
			sd_read_block_zero(sdkp);
		/*
		 * set the default to rotational.  All non-rotational devices
		 * support the block characteristics VPD page, which will
		 * cause this to be updated correctly and any device which
		 * doesn't support it should be treated as rotational.
		 */
		blk_queue_flag_clear(QUEUE_FLAG_NONROT, q);
		blk_queue_flag_set(QUEUE_FLAG_ADD_RANDOM, q);

		if (scsi_device_supports_vpd(sdp)) {
			sd_read_block_provisioning(sdkp);
			sd_read_block_limits(sdkp);
			sd_read_block_limits_ext(sdkp);
			sd_read_block_characteristics(sdkp);
			sd_zbc_read_zones(sdkp, buffer);
			sd_read_cpr(sdkp);
		}

		sd_print_capacity(sdkp, old_capacity);

		sd_read_write_protect_flag(sdkp, buffer);
		sd_read_cache_type(sdkp, buffer);
		sd_read_io_hints(sdkp, buffer);
		sd_read_app_tag_own(sdkp, buffer);
		sd_read_write_same(sdkp, buffer);
		sd_read_security(sdkp, buffer);
		sd_config_protection(sdkp);
	}

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

	if (sd_validate_min_xfer_size(sdkp))
		blk_queue_io_min(sdkp->disk->queue,
				 logical_to_bytes(sdp, sdkp->min_xfer_blocks));
	else
		blk_queue_io_min(sdkp->disk->queue, 0);

	if (sd_validate_opt_xfer_size(sdkp, dev_max)) {
		q->limits.io_opt = logical_to_bytes(sdp, sdkp->opt_xfer_blocks);
		rw_max = logical_to_sectors(sdp, sdkp->opt_xfer_blocks);
	} else {
		q->limits.io_opt = 0;
		rw_max = min_not_zero(logical_to_sectors(sdp, dev_max),
				      (sector_t)BLK_DEF_MAX_SECTORS_CAP);
	}

	/*
	 * Limit default to SCSI host optimal sector limit if set. There may be
	 * an impact on performance for when the size of a request exceeds this
	 * host limit.
	 */
	rw_max = min_not_zero(rw_max, sdp->host->opt_sectors);

	/* Do not exceed controller limit */
	rw_max = min(rw_max, queue_max_hw_sectors(q));

	/*
	 * Only update max_sectors if previously unset or if the current value
	 * exceeds the capabilities of the hardware.
	 */
	if (sdkp->first_scan ||
	    q->limits.max_sectors > q->limits.max_dev_sectors ||
	    q->limits.max_sectors > q->limits.max_hw_sectors)
		q->limits.max_sectors = rw_max;

	sdkp->first_scan = 0;

	set_capacity_and_notify(disk, logical_to_sectors(sdp, sdkp->capacity));
	sd_config_write_same(sdkp);
	kfree(buffer);

	/*
	 * For a zoned drive, revalidating the zones can be done only once
	 * the gendisk capacity is set. So if this fails, set back the gendisk
	 * capacity to 0.
	 */
	if (sd_zbc_revalidate_zones(sdkp))
		set_capacity_and_notify(disk, 0);

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

	if (!IS_ENABLED(CONFIG_BLK_DEV_ZONED) && sdp->type == TYPE_ZBC) {
		sdev_printk(KERN_WARNING, sdp,
			    "Unsupported ZBC host-managed device.\n");
		goto out;
	}

	SCSI_LOG_HLQUEUE(3, sdev_printk(KERN_INFO, sdp,
					"sd_probe\n"));

	error = -ENOMEM;
	sdkp = kzalloc(sizeof(*sdkp), GFP_KERNEL);
	if (!sdkp)
		goto out;

	gd = blk_mq_alloc_disk_for_queue(sdp->request_queue,
					 &sd_bio_compl_lkclass);
	if (!gd)
		goto out_free;

	index = ida_alloc(&sd_index_ida, GFP_KERNEL);
	if (index < 0) {
		sdev_printk(KERN_WARNING, sdp, "sd_probe: memory exhausted.\n");
		goto out_put;
	}

	error = sd_format_disk_name("sd", index, gd->disk_name, DISK_NAME_LEN);
	if (error) {
		sdev_printk(KERN_WARNING, sdp, "SCSI disk (sd) name length exceeded.\n");
		goto out_free_index;
	}

	sdkp->device = sdp;
	sdkp->disk = gd;
	sdkp->index = index;
	sdkp->max_retries = SD_MAX_RETRIES;
	atomic_set(&sdkp->openers, 0);
	atomic_set(&sdkp->device->ioerr_cnt, 0);

	if (!sdp->request_queue->rq_timeout) {
		if (sdp->type != TYPE_MOD)
			blk_queue_rq_timeout(sdp->request_queue, SD_TIMEOUT);
		else
			blk_queue_rq_timeout(sdp->request_queue,
					     SD_MOD_TIMEOUT);
	}

	device_initialize(&sdkp->disk_dev);
	sdkp->disk_dev.parent = get_device(dev);
	sdkp->disk_dev.class = &sd_disk_class;
	dev_set_name(&sdkp->disk_dev, "%s", dev_name(dev));

	error = device_add(&sdkp->disk_dev);
	if (error) {
		put_device(&sdkp->disk_dev);
		goto out;
	}

	dev_set_drvdata(dev, sdkp);

	gd->major = sd_major((index & 0xf0) >> 4);
	gd->first_minor = ((index & 0xf) << 4) | (index & 0xfff00);
	gd->minors = SD_MINORS;

	gd->fops = &sd_fops;
	gd->private_data = sdkp;

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

	if (sdp->removable) {
		gd->flags |= GENHD_FL_REMOVABLE;
		gd->events |= DISK_EVENT_MEDIA_CHANGE;
		gd->event_flags = DISK_EVENT_FLAG_POLL | DISK_EVENT_FLAG_UEVENT;
	}

	blk_pm_runtime_init(sdp->request_queue, dev);
	if (sdp->rpm_autosuspend) {
		pm_runtime_set_autosuspend_delay(dev,
			sdp->host->rpm_autosuspend_delay);
	}

	error = device_add_disk(dev, gd, NULL);
	if (error) {
		put_device(&sdkp->disk_dev);
		put_disk(gd);
		goto out;
	}

	if (sdkp->security) {
		sdkp->opal_dev = init_opal_dev(sdkp, &sd_sec_submit);
		if (sdkp->opal_dev)
			sd_printk(KERN_NOTICE, sdkp, "supports TCG Opal\n");
	}

	sd_printk(KERN_NOTICE, sdkp, "Attached SCSI %sdisk\n",
		  sdp->removable ? "removable " : "");
	scsi_autopm_put_device(sdp);

	return 0;

 out_free_index:
	ida_free(&sd_index_ida, index);
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
	struct scsi_disk *sdkp = dev_get_drvdata(dev);

	scsi_autopm_get_device(sdkp->device);

	device_del(&sdkp->disk_dev);
	del_gendisk(sdkp->disk);
	if (!sdkp->suspended)
		sd_shutdown(dev);

	put_disk(sdkp->disk);
	return 0;
}

static void scsi_disk_release(struct device *dev)
{
	struct scsi_disk *sdkp = to_scsi_disk(dev);

	ida_free(&sd_index_ida, sdkp->index);
	sd_zbc_free_zone_info(sdkp);
	put_device(&sdkp->device->sdev_gendev);
	free_opal_dev(sdkp->opal_dev);

	kfree(sdkp);
}

static int sd_start_stop_device(struct scsi_disk *sdkp, int start)
{
	unsigned char cmd[6] = { START_STOP };	/* START_VALID */
	struct scsi_sense_hdr sshdr;
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
		.req_flags = BLK_MQ_REQ_PM,
	};
	struct scsi_device *sdp = sdkp->device;
	int res;

	if (start)
		cmd[4] |= 1;	/* START */

	if (sdp->start_stop_pwr_cond)
		cmd[4] |= start ? 1 << 4 : 3 << 4;	/* Active or Standby */

	if (!scsi_device_online(sdp))
		return -ENODEV;

	res = scsi_execute_cmd(sdp, cmd, REQ_OP_DRV_IN, NULL, 0, SD_TIMEOUT,
			       sdkp->max_retries, &exec_args);
	if (res) {
		sd_print_result(sdkp, "Start/Stop Unit failed", res);
		if (res > 0 && scsi_sense_valid(&sshdr)) {
			sd_print_sense_hdr(sdkp, &sshdr);
			/* 0x3a is medium not present */
			if (sshdr.asc == 0x3a)
				res = 0;
		}
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
		sd_sync_cache(sdkp);
	}

	if ((system_state != SYSTEM_RESTART &&
	     sdkp->device->manage_system_start_stop) ||
	    (system_state == SYSTEM_POWER_OFF &&
	     sdkp->device->manage_shutdown)) {
		sd_printk(KERN_NOTICE, sdkp, "Stopping disk\n");
		sd_start_stop_device(sdkp, 0);
	}
}

static inline bool sd_do_start_stop(struct scsi_device *sdev, bool runtime)
{
	return (sdev->manage_system_start_stop && !runtime) ||
		(sdev->manage_runtime_start_stop && runtime);
}

static int sd_suspend_common(struct device *dev, bool runtime)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);
	int ret = 0;

	if (!sdkp)	/* E.g.: runtime suspend following sd_remove() */
		return 0;

	if (sdkp->WCE && sdkp->media_present) {
		if (!sdkp->device->silence_suspend)
			sd_printk(KERN_NOTICE, sdkp, "Synchronizing SCSI cache\n");
		ret = sd_sync_cache(sdkp);
		/* ignore OFFLINE device */
		if (ret == -ENODEV)
			return 0;

		if (ret)
			return ret;
	}

	if (sd_do_start_stop(sdkp->device, runtime)) {
		if (!sdkp->device->silence_suspend)
			sd_printk(KERN_NOTICE, sdkp, "Stopping disk\n");
		/* an error is not worth aborting a system sleep */
		ret = sd_start_stop_device(sdkp, 0);
		if (!runtime)
			ret = 0;
	}

	if (!ret)
		sdkp->suspended = true;

	return ret;
}

static int sd_suspend_system(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return sd_suspend_common(dev, false);
}

static int sd_suspend_runtime(struct device *dev)
{
	return sd_suspend_common(dev, true);
}

static int sd_resume(struct device *dev, bool runtime)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);
	int ret;

	if (!sdkp)	/* E.g.: runtime resume at the start of sd_probe() */
		return 0;

	if (!sd_do_start_stop(sdkp->device, runtime)) {
		sdkp->suspended = false;
		return 0;
	}

	sd_printk(KERN_NOTICE, sdkp, "Starting disk\n");
	ret = sd_start_stop_device(sdkp, 1);
	if (!ret) {
		opal_unlock_from_suspend(sdkp->opal_dev);
		sdkp->suspended = false;
	}

	return ret;
}

static int sd_resume_system(struct device *dev)
{
	if (pm_runtime_suspended(dev)) {
		struct scsi_disk *sdkp = dev_get_drvdata(dev);
		struct scsi_device *sdp = sdkp ? sdkp->device : NULL;

		if (sdp && sdp->force_runtime_start_on_system_start)
			pm_request_resume(dev);

		return 0;
	}

	return sd_resume(dev, false);
}

static int sd_resume_runtime(struct device *dev)
{
	struct scsi_disk *sdkp = dev_get_drvdata(dev);
	struct scsi_device *sdp;

	if (!sdkp)	/* E.g.: runtime resume at the start of sd_probe() */
		return 0;

	sdp = sdkp->device;

	if (sdp->ignore_media_change) {
		/* clear the device's sense data */
		static const u8 cmd[10] = { REQUEST_SENSE };
		const struct scsi_exec_args exec_args = {
			.req_flags = BLK_MQ_REQ_PM,
		};

		if (scsi_execute_cmd(sdp, cmd, REQ_OP_DRV_IN, NULL, 0,
				     sdp->request_queue->rq_timeout, 1,
				     &exec_args))
			sd_printk(KERN_NOTICE, sdkp,
				  "Failed to clear sense data\n");
	}

	return sd_resume(dev, true);
}

static const struct dev_pm_ops sd_pm_ops = {
	.suspend		= sd_suspend_system,
	.resume			= sd_resume_system,
	.poweroff		= sd_suspend_system,
	.restore		= sd_resume_system,
	.runtime_suspend	= sd_suspend_runtime,
	.runtime_resume		= sd_resume_runtime,
};

static struct scsi_driver sd_template = {
	.gendrv = {
		.name		= "sd",
		.owner		= THIS_MODULE,
		.probe		= sd_probe,
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
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
		if (__register_blkdev(sd_major(i), "sd", sd_default_probe))
			continue;
		majors++;
	}

	if (!majors)
		return -ENODEV;

	err = class_register(&sd_disk_class);
	if (err)
		goto err_out;

	sd_page_pool = mempool_create_page_pool(SD_MEMPOOL_SIZE, 0);
	if (!sd_page_pool) {
		printk(KERN_ERR "sd: can't init discard page pool\n");
		err = -ENOMEM;
		goto err_out_class;
	}

	err = scsi_register_driver(&sd_template.gendrv);
	if (err)
		goto err_out_driver;

	return 0;

err_out_driver:
	mempool_destroy(sd_page_pool);
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
	mempool_destroy(sd_page_pool);

	class_unregister(&sd_disk_class);

	for (i = 0; i < SD_MAJORS; i++)
		unregister_blkdev(sd_major(i), "sd");
}

module_init(init_sd);
module_exit(exit_sd);

void sd_print_sense_hdr(struct scsi_disk *sdkp, struct scsi_sense_hdr *sshdr)
{
	scsi_print_sense_hdr(sdkp->device,
			     sdkp->disk ? sdkp->disk->disk_name : NULL, sshdr);
}

void sd_print_result(const struct scsi_disk *sdkp, const char *msg, int result)
{
	const char *hb_string = scsi_hostbyte_string(result);

	if (hb_string)
		sd_printk(KERN_INFO, sdkp,
			  "%s: Result: hostbyte=%s driverbyte=%s\n", msg,
			  hb_string ? hb_string : "invalid",
			  "DRIVER_OK");
	else
		sd_printk(KERN_INFO, sdkp,
			  "%s: Result: hostbyte=0x%02x driverbyte=%s\n",
			  msg, host_byte(result), "DRIVER_OK");
}
