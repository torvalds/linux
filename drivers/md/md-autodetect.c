// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/init_syscalls.h>
#include <linux/raid/detect.h>
#include <linux/raid/md_u.h>
#include <linux/raid/md_p.h>
#include "md.h"

/*
 * When md (and any require personalities) are compiled into the kernel
 * (not a module), arrays can be assembles are boot time using with AUTODETECT
 * where specially marked partitions are registered with md_autodetect_dev(),
 * and with MD_BOOT where devices to be collected are given on the boot line
 * with md=.....
 * The code for that is here.
 */

#ifdef CONFIG_MD_AUTODETECT
static int __initdata raid_noautodetect;
#else
static int __initdata raid_noautodetect=1;
#endif
static int __initdata raid_autopart;

static struct md_setup_args {
	int minor;
	int partitioned;
	int level;
	int chunk;
	char *device_names;
} md_setup_args[256] __initdata;

static int md_setup_ents __initdata;

/*
 * Parse the command-line parameters given our kernel, but do not
 * actually try to invoke the MD device now; that is handled by
 * md_setup_drive after the low-level disk drivers have initialised.
 *
 * 27/11/1999: Fixed to work correctly with the 2.3 kernel (which
 *             assigns the task of parsing integer arguments to the
 *             invoked program now).  Added ability to initialise all
 *             the MD devices (by specifying multiple "md=" lines)
 *             instead of just one.  -- KTK
 * 18May2000: Added support for persistent-superblock arrays:
 *             md=n,0,factor,fault,device-list   uses RAID0 for device n
 *             md=n,-1,factor,fault,device-list  uses LINEAR for device n
 *             md=n,device-list      reads a RAID superblock from the devices
 *             elements in device-list are read by name_to_kdev_t so can be
 *             a hex number or something like /dev/hda1 /dev/sdb
 * 2001-06-03: Dave Cinege <dcinege@psychosis.com>
 *		Shifted name_to_kdev_t() and related operations to md_set_drive()
 *		for later execution. Rewrote section to make devfs compatible.
 */
static int __init md_setup(char *str)
{
	int minor, level, factor, fault, partitioned = 0;
	char *pername = "";
	char *str1;
	int ent;

	if (*str == 'd') {
		partitioned = 1;
		str++;
	}
	if (get_option(&str, &minor) != 2) {	/* MD Number */
		printk(KERN_WARNING "md: Too few arguments supplied to md=.\n");
		return 0;
	}
	str1 = str;
	for (ent=0 ; ent< md_setup_ents ; ent++)
		if (md_setup_args[ent].minor == minor &&
		    md_setup_args[ent].partitioned == partitioned) {
			printk(KERN_WARNING "md: md=%s%d, Specified more than once. "
			       "Replacing previous definition.\n", partitioned?"d":"", minor);
			break;
		}
	if (ent >= ARRAY_SIZE(md_setup_args)) {
		printk(KERN_WARNING "md: md=%s%d - too many md initialisations\n", partitioned?"d":"", minor);
		return 0;
	}
	if (ent >= md_setup_ents)
		md_setup_ents++;
	switch (get_option(&str, &level)) {	/* RAID level */
	case 2: /* could be 0 or -1.. */
		if (level == 0 || level == LEVEL_LINEAR) {
			if (get_option(&str, &factor) != 2 ||	/* Chunk Size */
					get_option(&str, &fault) != 2) {
				printk(KERN_WARNING "md: Too few arguments supplied to md=.\n");
				return 0;
			}
			md_setup_args[ent].level = level;
			md_setup_args[ent].chunk = 1 << (factor+12);
			if (level ==  LEVEL_LINEAR)
				pername = "linear";
			else
				pername = "raid0";
			break;
		}
		fallthrough;
	case 1: /* the first device is numeric */
		str = str1;
		fallthrough;
	case 0:
		md_setup_args[ent].level = LEVEL_NONE;
		pername="super-block";
	}

	printk(KERN_INFO "md: Will configure md%d (%s) from %s, below.\n",
		minor, pername, str);
	md_setup_args[ent].device_names = str;
	md_setup_args[ent].partitioned = partitioned;
	md_setup_args[ent].minor = minor;

	return 1;
}

static void __init md_setup_drive(struct md_setup_args *args)
{
	char *devname = args->device_names;
	dev_t devices[MD_SB_DISKS + 1], mdev;
	struct mdu_array_info_s ainfo = { };
	struct mddev *mddev;
	int err = 0, i;
	char name[16];

	if (args->partitioned) {
		mdev = MKDEV(mdp_major, args->minor << MdpMinorShift);
		sprintf(name, "md_d%d", args->minor);
	} else {
		mdev = MKDEV(MD_MAJOR, args->minor);
		sprintf(name, "md%d", args->minor);
	}

	for (i = 0; i < MD_SB_DISKS && devname != NULL; i++) {
		struct kstat stat;
		char *p;
		char comp_name[64];
		dev_t dev;

		p = strchr(devname, ',');
		if (p)
			*p++ = 0;

		if (early_lookup_bdev(devname, &dev))
			dev = 0;
		if (strncmp(devname, "/dev/", 5) == 0)
			devname += 5;
		snprintf(comp_name, 63, "/dev/%s", devname);
		if (init_stat(comp_name, &stat, 0) == 0 && S_ISBLK(stat.mode))
			dev = new_decode_dev(stat.rdev);
		if (!dev) {
			pr_warn("md: Unknown device name: %s\n", devname);
			break;
		}

		devices[i] = dev;
		devname = p;
	}
	devices[i] = 0;

	if (!i)
		return;

	pr_info("md: Loading %s: %s\n", name, args->device_names);

	mddev = md_alloc(mdev, name);
	if (IS_ERR(mddev)) {
		pr_err("md: md_alloc failed - cannot start array %s\n", name);
		return;
	}

	err = mddev_lock(mddev);
	if (err) {
		pr_err("md: failed to lock array %s\n", name);
		goto out_mddev_put;
	}

	if (!list_empty(&mddev->disks) || mddev->raid_disks) {
		pr_warn("md: Ignoring %s, already autodetected. (Use raid=noautodetect)\n",
		       name);
		goto out_unlock;
	}

	if (args->level != LEVEL_NONE) {
		/* non-persistent */
		ainfo.level = args->level;
		ainfo.md_minor = args->minor;
		ainfo.not_persistent = 1;
		ainfo.state = (1 << MD_SB_CLEAN);
		ainfo.chunk_size = args->chunk;
		while (devices[ainfo.raid_disks])
			ainfo.raid_disks++;
	}

	err = md_set_array_info(mddev, &ainfo);

	for (i = 0; i <= MD_SB_DISKS && devices[i]; i++) {
		struct mdu_disk_info_s dinfo = {
			.major	= MAJOR(devices[i]),
			.minor	= MINOR(devices[i]),
		};

		if (args->level != LEVEL_NONE) {
			dinfo.number = i;
			dinfo.raid_disk = i;
			dinfo.state =
				(1 << MD_DISK_ACTIVE) | (1 << MD_DISK_SYNC);
		}

		md_add_new_disk(mddev, &dinfo);
	}

	if (!err)
		err = do_md_run(mddev);
	if (err)
		pr_warn("md: starting %s failed\n", name);
out_unlock:
	mddev_unlock(mddev);
out_mddev_put:
	mddev_put(mddev);
}

static int __init raid_setup(char *str)
{
	int len, pos;

	len = strlen(str) + 1;
	pos = 0;

	while (pos < len) {
		char *comma = strchr(str+pos, ',');
		int wlen;
		if (comma)
			wlen = (comma-str)-pos;
		else	wlen = (len-1)-pos;

		if (!strncmp(str, "noautodetect", wlen))
			raid_noautodetect = 1;
		if (!strncmp(str, "autodetect", wlen))
			raid_noautodetect = 0;
		if (strncmp(str, "partitionable", wlen)==0)
			raid_autopart = 1;
		if (strncmp(str, "part", wlen)==0)
			raid_autopart = 1;
		pos += wlen+1;
	}
	return 1;
}

__setup("raid=", raid_setup);
__setup("md=", md_setup);

static void __init autodetect_raid(void)
{
	/*
	 * Since we don't want to detect and use half a raid array, we need to
	 * wait for the known devices to complete their probing
	 */
	printk(KERN_INFO "md: Waiting for all devices to be available before autodetect\n");
	printk(KERN_INFO "md: If you don't use raid, use raid=noautodetect\n");

	wait_for_device_probe();
	md_autostart_arrays(raid_autopart);
}

void __init md_run_setup(void)
{
	int ent;

	if (raid_noautodetect)
		printk(KERN_INFO "md: Skipping autodetection of RAID arrays. (raid=autodetect will force)\n");
	else
		autodetect_raid();

	for (ent = 0; ent < md_setup_ents; ent++)
		md_setup_drive(&md_setup_args[ent]);
}
