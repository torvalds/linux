/*
 * scsi_wait_scan.c
 *
 * Copyright (C) 2006 James Bottomley <James.Bottomley@SteelEye.com>
 *
 * This is a simple module to wait until all the async scans are
 * complete.  The idea is to use it in initrd/initramfs scripts.  You
 * modprobe it after all the modprobes of the root SCSI drivers and it
 * will wait until they have all finished scanning their busses before
 * allowing the boot to proceed
 */

#include <linux/module.h>
#include <linux/device.h>
#include "scsi_priv.h"

static int __init wait_scan_init(void)
{
	/*
	 * First we need to wait for device probing to finish;
	 * the drivers we just loaded might just still be probing
	 * and might not yet have reached the scsi async scanning
	 */
	wait_for_device_probe();
	return 0;
}

static void __exit wait_scan_exit(void)
{
}

MODULE_DESCRIPTION("SCSI wait for scans");
MODULE_AUTHOR("James Bottomley");
MODULE_LICENSE("GPL");

late_initcall(wait_scan_init);
module_exit(wait_scan_exit);
