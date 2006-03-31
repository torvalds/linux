/*
 * (C) Copyright IBM Corp. 2004
 * tape_class.c
 *
 * Tape class device support
 *
 * Author: Stefan Bader <shbader@de.ibm.com>
 * Based on simple class device code by Greg K-H
 */
#include "tape_class.h"

MODULE_AUTHOR("Stefan Bader <shbader@de.ibm.com>");
MODULE_DESCRIPTION(
	"(C) Copyright IBM Corp. 2004   All Rights Reserved.\n"
	"tape_class.c"
);
MODULE_LICENSE("GPL");

static struct class *tape_class;

/*
 * Register a tape device and return a pointer to the cdev structure.
 *
 * device
 *	The pointer to the struct device of the physical (base) device.
 * drivername
 *	The pointer to the drivers name for it's character devices.
 * dev
 *	The intended major/minor number. The major number may be 0 to
 *	get a dynamic major number.
 * fops
 *	The pointer to the drivers file operations for the tape device.
 * devname
 *	The pointer to the name of the character device.
 */
struct tape_class_device *register_tape_dev(
	struct device *		device,
	dev_t			dev,
	struct file_operations *fops,
	char *			device_name,
	char *			mode_name)
{
	struct tape_class_device *	tcd;
	int		rc;
	char *		s;

	tcd = kzalloc(sizeof(struct tape_class_device), GFP_KERNEL);
	if (!tcd)
		return ERR_PTR(-ENOMEM);

	strncpy(tcd->device_name, device_name, TAPECLASS_NAME_LEN);
	for (s = strchr(tcd->device_name, '/'); s; s = strchr(s, '/'))
		*s = '!';
	strncpy(tcd->mode_name, mode_name, TAPECLASS_NAME_LEN);
	for (s = strchr(tcd->mode_name, '/'); s; s = strchr(s, '/'))
		*s = '!';

	tcd->char_device = cdev_alloc();
	if (!tcd->char_device) {
		rc = -ENOMEM;
		goto fail_with_tcd;
	}

	tcd->char_device->owner = fops->owner;
	tcd->char_device->ops   = fops;
	tcd->char_device->dev   = dev;

	rc = cdev_add(tcd->char_device, tcd->char_device->dev, 1);
	if (rc)
		goto fail_with_cdev;

	tcd->class_device = class_device_create(
				tape_class,
				NULL,
				tcd->char_device->dev,
				device,
				"%s", tcd->device_name
			);
	sysfs_create_link(
		&device->kobj,
		&tcd->class_device->kobj,
		tcd->mode_name
	);

	return tcd;

fail_with_cdev:
	cdev_del(tcd->char_device);

fail_with_tcd:
	kfree(tcd);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL(register_tape_dev);

void unregister_tape_dev(struct tape_class_device *tcd)
{
	if (tcd != NULL && !IS_ERR(tcd)) {
		sysfs_remove_link(
			&tcd->class_device->dev->kobj,
			tcd->mode_name
		);
		class_device_destroy(tape_class, tcd->char_device->dev);
		cdev_del(tcd->char_device);
		kfree(tcd);
	}
}
EXPORT_SYMBOL(unregister_tape_dev);


static int __init tape_init(void)
{
	tape_class = class_create(THIS_MODULE, "tape390");

	return 0;
}

static void __exit tape_exit(void)
{
	class_destroy(tape_class);
	tape_class = NULL;
}

postcore_initcall(tape_init);
module_exit(tape_exit);
