/*
 * osd_uld.c - OSD Upper Layer Driver
 *
 * A Linux driver module that registers as a SCSI ULD and probes
 * for OSD type SCSI devices.
 * It's main function is to export osd devices to in-kernel users like
 * osdfs and pNFS-objects-LD. It also provides one ioctl for running
 * in Kernel tests.
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/namei.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/major.h>
#include <linux/file.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_ioctl.h>

#include <scsi/osd_initiator.h>
#include <scsi/osd_sec.h>

#include "osd_debug.h"

#ifndef TYPE_OSD
#  define TYPE_OSD 0x11
#endif

#ifndef SCSI_OSD_MAJOR
#  define SCSI_OSD_MAJOR 260
#endif
#define SCSI_OSD_MAX_MINOR MINORMASK

static const char osd_name[] = "osd";
static const char *osd_version_string = "open-osd 0.2.1";

MODULE_AUTHOR("Boaz Harrosh <bharrosh@panasas.com>");
MODULE_DESCRIPTION("open-osd Upper-Layer-Driver osd.ko");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(SCSI_OSD_MAJOR);
MODULE_ALIAS_SCSI_DEVICE(TYPE_OSD);

struct osd_uld_device {
	int minor;
	struct device class_dev;
	struct cdev cdev;
	struct osd_dev od;
	struct osd_dev_info odi;
	struct gendisk *disk;
};

struct osd_dev_handle {
	struct osd_dev od;
	struct file *file;
	struct osd_uld_device *oud;
} ;

static DEFINE_IDA(osd_minor_ida);

static struct class osd_uld_class = {
	.owner		= THIS_MODULE,
	.name		= "scsi_osd",
};

/*
 * Char Device operations
 */

static int osd_uld_open(struct inode *inode, struct file *file)
{
	struct osd_uld_device *oud = container_of(inode->i_cdev,
					struct osd_uld_device, cdev);

	get_device(&oud->class_dev);
	/* cache osd_uld_device on file handle */
	file->private_data = oud;
	OSD_DEBUG("osd_uld_open %p\n", oud);
	return 0;
}

static int osd_uld_release(struct inode *inode, struct file *file)
{
	struct osd_uld_device *oud = file->private_data;

	OSD_DEBUG("osd_uld_release %p\n", file->private_data);
	file->private_data = NULL;
	put_device(&oud->class_dev);
	return 0;
}

/* FIXME: Only one vector for now */
unsigned g_test_ioctl;
do_test_fn *g_do_test;

int osduld_register_test(unsigned ioctl, do_test_fn *do_test)
{
	if (g_test_ioctl)
		return -EINVAL;

	g_test_ioctl = ioctl;
	g_do_test = do_test;
	return 0;
}
EXPORT_SYMBOL(osduld_register_test);

void osduld_unregister_test(unsigned ioctl)
{
	if (ioctl == g_test_ioctl) {
		g_test_ioctl = 0;
		g_do_test = NULL;
	}
}
EXPORT_SYMBOL(osduld_unregister_test);

static do_test_fn *_find_ioctl(unsigned cmd)
{
	if (g_test_ioctl == cmd)
		return g_do_test;
	else
		return NULL;
}

static long osd_uld_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct osd_uld_device *oud = file->private_data;
	int ret;
	do_test_fn *do_test;

	do_test = _find_ioctl(cmd);
	if (do_test)
		ret = do_test(&oud->od, cmd, arg);
	else {
		OSD_ERR("Unknown ioctl %d: osd_uld_device=%p\n", cmd, oud);
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

static const struct file_operations osd_fops = {
	.owner          = THIS_MODULE,
	.open           = osd_uld_open,
	.release        = osd_uld_release,
	.unlocked_ioctl = osd_uld_ioctl,
	.llseek		= noop_llseek,
};

struct osd_dev *osduld_path_lookup(const char *name)
{
	struct osd_uld_device *oud;
	struct osd_dev_handle *odh;
	struct file *file;
	int error;

	if (!name || !*name) {
		OSD_ERR("Mount with !path || !*path\n");
		return ERR_PTR(-EINVAL);
	}

	odh = kzalloc(sizeof(*odh), GFP_KERNEL);
	if (unlikely(!odh))
		return ERR_PTR(-ENOMEM);

	file = filp_open(name, O_RDWR, 0);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto free_od;
	}

	if (file->f_op != &osd_fops){
		error = -EINVAL;
		goto close_file;
	}

	oud = file->private_data;

	odh->od = oud->od;
	odh->file = file;
	odh->oud = oud;

	return &odh->od;

close_file:
	fput(file);
free_od:
	kfree(odh);
	return ERR_PTR(error);
}
EXPORT_SYMBOL(osduld_path_lookup);

static inline bool _the_same_or_null(const u8 *a1, unsigned a1_len,
				     const u8 *a2, unsigned a2_len)
{
	if (!a2_len) /* User string is Empty means don't care */
		return true;

	if (a1_len != a2_len)
		return false;

	return 0 == memcmp(a1, a2, a1_len);
}

struct find_oud_t {
	const struct osd_dev_info *odi;
	struct device *dev;
	struct osd_uld_device *oud;
} ;

int _mach_odi(struct device *dev, void *find_data)
{
	struct osd_uld_device *oud = container_of(dev, struct osd_uld_device,
						  class_dev);
	struct find_oud_t *fot = find_data;
	const struct osd_dev_info *odi = fot->odi;

	if (_the_same_or_null(oud->odi.systemid, oud->odi.systemid_len,
			      odi->systemid, odi->systemid_len) &&
	    _the_same_or_null(oud->odi.osdname, oud->odi.osdname_len,
			      odi->osdname, odi->osdname_len)) {
		OSD_DEBUG("found device sysid_len=%d osdname=%d\n",
			  odi->systemid_len, odi->osdname_len);
		fot->oud = oud;
		return 1;
	} else {
		return 0;
	}
}

/* osduld_info_lookup - Loop through all devices, return the requested osd_dev.
 *
 * if @odi->systemid_len and/or @odi->osdname_len are zero, they act as a don't
 * care. .e.g if they're both zero /dev/osd0 is returned.
 */
struct osd_dev *osduld_info_lookup(const struct osd_dev_info *odi)
{
	struct find_oud_t find = {.odi = odi};

	find.dev = class_find_device(&osd_uld_class, NULL, &find, _mach_odi);
	if (likely(find.dev)) {
		struct osd_dev_handle *odh = kzalloc(sizeof(*odh), GFP_KERNEL);

		if (unlikely(!odh)) {
			put_device(find.dev);
			return ERR_PTR(-ENOMEM);
		}

		odh->od = find.oud->od;
		odh->oud = find.oud;

		return &odh->od;
	}

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL(osduld_info_lookup);

void osduld_put_device(struct osd_dev *od)
{
	if (od && !IS_ERR(od)) {
		struct osd_dev_handle *odh =
				container_of(od, struct osd_dev_handle, od);
		struct osd_uld_device *oud = odh->oud;

		BUG_ON(od->scsi_device != oud->od.scsi_device);

		/* If scsi has released the device (logout), and exofs has last
		 * reference on oud it will be freed by above osd_uld_release
		 * within fput below. But this will oops in cdev_release which
		 * is called after the fops->release. A get_/put_ pair makes
		 * sure we have a cdev for the duration of fput
		 */
		if (odh->file) {
			get_device(&oud->class_dev);
			fput(odh->file);
		}
		put_device(&oud->class_dev);
		kfree(odh);
	}
}
EXPORT_SYMBOL(osduld_put_device);

const struct osd_dev_info *osduld_device_info(struct osd_dev *od)
{
	struct osd_dev_handle *odh =
				container_of(od, struct osd_dev_handle, od);
	return &odh->oud->odi;
}
EXPORT_SYMBOL(osduld_device_info);

bool osduld_device_same(struct osd_dev *od, const struct osd_dev_info *odi)
{
	struct osd_dev_handle *odh =
				container_of(od, struct osd_dev_handle, od);
	struct osd_uld_device *oud = odh->oud;

	return (oud->odi.systemid_len == odi->systemid_len) &&
		_the_same_or_null(oud->odi.systemid, oud->odi.systemid_len,
				 odi->systemid, odi->systemid_len) &&
		(oud->odi.osdname_len == odi->osdname_len) &&
		_the_same_or_null(oud->odi.osdname, oud->odi.osdname_len,
				  odi->osdname, odi->osdname_len);
}
EXPORT_SYMBOL(osduld_device_same);

/*
 * Scsi Device operations
 */

static int __detect_osd(struct osd_uld_device *oud)
{
	struct scsi_device *scsi_device = oud->od.scsi_device;
	char caps[OSD_CAP_LEN];
	int error;

	/* sending a test_unit_ready as first command seems to be needed
	 * by some targets
	 */
	OSD_DEBUG("start scsi_test_unit_ready %p %p %p\n",
			oud, scsi_device, scsi_device->request_queue);
	error = scsi_test_unit_ready(scsi_device, 10*HZ, 5, NULL);
	if (error)
		OSD_ERR("warning: scsi_test_unit_ready failed\n");

	osd_sec_init_nosec_doall_caps(caps, &osd_root_object, false, true);
	if (osd_auto_detect_ver(&oud->od, caps, &oud->odi))
		return -ENODEV;

	return 0;
}

static void __remove(struct device *dev)
{
	struct osd_uld_device *oud = container_of(dev, struct osd_uld_device,
						  class_dev);
	struct scsi_device *scsi_device = oud->od.scsi_device;

	kfree(oud->odi.osdname);

	if (oud->cdev.owner)
		cdev_del(&oud->cdev);

	osd_dev_fini(&oud->od);
	scsi_device_put(scsi_device);

	OSD_INFO("osd_remove %s\n",
		 oud->disk ? oud->disk->disk_name : NULL);

	if (oud->disk)
		put_disk(oud->disk);
	ida_remove(&osd_minor_ida, oud->minor);

	kfree(oud);
}

static int osd_probe(struct device *dev)
{
	struct scsi_device *scsi_device = to_scsi_device(dev);
	struct gendisk *disk;
	struct osd_uld_device *oud;
	int minor;
	int error;

	if (scsi_device->type != TYPE_OSD)
		return -ENODEV;

	do {
		if (!ida_pre_get(&osd_minor_ida, GFP_KERNEL))
			return -ENODEV;

		error = ida_get_new(&osd_minor_ida, &minor);
	} while (error == -EAGAIN);

	if (error)
		return error;
	if (minor >= SCSI_OSD_MAX_MINOR) {
		error = -EBUSY;
		goto err_retract_minor;
	}

	error = -ENOMEM;
	oud = kzalloc(sizeof(*oud), GFP_KERNEL);
	if (NULL == oud)
		goto err_retract_minor;

	dev_set_drvdata(dev, oud);
	oud->minor = minor;

	/* allocate a disk and set it up */
	/* FIXME: do we need this since sg has already done that */
	disk = alloc_disk(1);
	if (!disk) {
		OSD_ERR("alloc_disk failed\n");
		goto err_free_osd;
	}
	disk->major = SCSI_OSD_MAJOR;
	disk->first_minor = oud->minor;
	sprintf(disk->disk_name, "osd%d", oud->minor);
	oud->disk = disk;

	/* hold one more reference to the scsi_device that will get released
	 * in __release, in case a logout is happening while fs is mounted
	 */
	scsi_device_get(scsi_device);
	osd_dev_init(&oud->od, scsi_device);

	/* Detect the OSD Version */
	error = __detect_osd(oud);
	if (error) {
		OSD_ERR("osd detection failed, non-compatible OSD device\n");
		goto err_put_disk;
	}

	/* init the char-device for communication with user-mode */
	cdev_init(&oud->cdev, &osd_fops);
	oud->cdev.owner = THIS_MODULE;
	error = cdev_add(&oud->cdev,
			 MKDEV(SCSI_OSD_MAJOR, oud->minor), 1);
	if (error) {
		OSD_ERR("cdev_add failed\n");
		goto err_put_disk;
	}

	/* class device member */
	oud->class_dev.devt = oud->cdev.dev;
	oud->class_dev.class = &osd_uld_class;
	oud->class_dev.parent = dev;
	oud->class_dev.release = __remove;
	error = dev_set_name(&oud->class_dev, "%s", disk->disk_name);
	if (error) {
		OSD_ERR("dev_set_name failed => %d\n", error);
		goto err_put_cdev;
	}

	error = device_register(&oud->class_dev);
	if (error) {
		OSD_ERR("device_register failed => %d\n", error);
		goto err_put_cdev;
	}

	get_device(&oud->class_dev);

	OSD_INFO("osd_probe %s\n", disk->disk_name);
	return 0;

err_put_cdev:
	cdev_del(&oud->cdev);
err_put_disk:
	scsi_device_put(scsi_device);
	put_disk(disk);
err_free_osd:
	dev_set_drvdata(dev, NULL);
	kfree(oud);
err_retract_minor:
	ida_remove(&osd_minor_ida, minor);
	return error;
}

static int osd_remove(struct device *dev)
{
	struct scsi_device *scsi_device = to_scsi_device(dev);
	struct osd_uld_device *oud = dev_get_drvdata(dev);

	if (!oud || (oud->od.scsi_device != scsi_device)) {
		OSD_ERR("Half cooked osd-device %p,%p || %p!=%p",
			dev, oud, oud ? oud->od.scsi_device : NULL,
			scsi_device);
	}

	device_unregister(&oud->class_dev);

	put_device(&oud->class_dev);
	return 0;
}

/*
 * Global driver and scsi registration
 */

static struct scsi_driver osd_driver = {
	.owner			= THIS_MODULE,
	.gendrv = {
		.name		= osd_name,
		.probe		= osd_probe,
		.remove		= osd_remove,
	}
};

static int __init osd_uld_init(void)
{
	int err;

	err = class_register(&osd_uld_class);
	if (err) {
		OSD_ERR("Unable to register sysfs class => %d\n", err);
		return err;
	}

	err = register_chrdev_region(MKDEV(SCSI_OSD_MAJOR, 0),
				     SCSI_OSD_MAX_MINOR, osd_name);
	if (err) {
		OSD_ERR("Unable to register major %d for osd ULD => %d\n",
			SCSI_OSD_MAJOR, err);
		goto err_out;
	}

	err = scsi_register_driver(&osd_driver.gendrv);
	if (err) {
		OSD_ERR("scsi_register_driver failed => %d\n", err);
		goto err_out_chrdev;
	}

	OSD_INFO("LOADED %s\n", osd_version_string);
	return 0;

err_out_chrdev:
	unregister_chrdev_region(MKDEV(SCSI_OSD_MAJOR, 0), SCSI_OSD_MAX_MINOR);
err_out:
	class_unregister(&osd_uld_class);
	return err;
}

static void __exit osd_uld_exit(void)
{
	scsi_unregister_driver(&osd_driver.gendrv);
	unregister_chrdev_region(MKDEV(SCSI_OSD_MAJOR, 0), SCSI_OSD_MAX_MINOR);
	class_unregister(&osd_uld_class);
	OSD_INFO("UNLOADED %s\n", osd_version_string);
}

module_init(osd_uld_init);
module_exit(osd_uld_exit);
