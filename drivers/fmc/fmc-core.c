/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fmc.h>

static int fmc_check_version(unsigned long version, const char *name)
{
	if (__FMC_MAJOR(version) != FMC_MAJOR) {
		pr_err("%s: \"%s\" has wrong major (has %li, expected %i)\n",
		       __func__, name, __FMC_MAJOR(version), FMC_MAJOR);
		return -EINVAL;
	}

	if (__FMC_MINOR(version) != FMC_MINOR)
		pr_info("%s: \"%s\" has wrong minor (has %li, expected %i)\n",
		       __func__, name, __FMC_MINOR(version), FMC_MINOR);
	return 0;
}

static int fmc_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* struct fmc_device *fdev = to_fmc_device(dev); */

	/* FIXME: The MODALIAS */
	add_uevent_var(env, "MODALIAS=%s", "fmc");
	return 0;
}

static int fmc_probe(struct device *dev)
{
	struct fmc_driver *fdrv = to_fmc_driver(dev->driver);
	struct fmc_device *fdev = to_fmc_device(dev);

	return fdrv->probe(fdev);
}

static int fmc_remove(struct device *dev)
{
	struct fmc_driver *fdrv = to_fmc_driver(dev->driver);
	struct fmc_device *fdev = to_fmc_device(dev);

	return fdrv->remove(fdev);
}

static void fmc_shutdown(struct device *dev)
{
	/* not implemented but mandatory */
}

static struct bus_type fmc_bus_type = {
	.name = "fmc",
	.match = fmc_match,
	.uevent = fmc_uevent,
	.probe = fmc_probe,
	.remove = fmc_remove,
	.shutdown = fmc_shutdown,
};

static void fmc_release(struct device *dev)
{
	struct fmc_device *fmc = container_of(dev, struct fmc_device, dev);

	kfree(fmc);
}

/*
 * The eeprom is exported in sysfs, through a binary attribute
 */

static ssize_t fmc_read_eeprom(struct file *file, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct device *dev;
	struct fmc_device *fmc;
	int eelen;

	dev = container_of(kobj, struct device, kobj);
	fmc = container_of(dev, struct fmc_device, dev);
	eelen = fmc->eeprom_len;
	if (off > eelen)
		return -ESPIPE;
	if (off == eelen)
		return 0; /* EOF */
	if (off + count > eelen)
		count = eelen - off;
	memcpy(buf, fmc->eeprom + off, count);
	return count;
}

static struct bin_attribute fmc_eeprom_attr = {
	.attr = { .name = "eeprom", .mode = S_IRUGO, },
	.size = 8192, /* more or less standard */
	.read = fmc_read_eeprom,
};

/*
 * Functions for client modules follow
 */

int fmc_driver_register(struct fmc_driver *drv)
{
	if (fmc_check_version(drv->version, drv->driver.name))
		return -EINVAL;
	drv->driver.bus = &fmc_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(fmc_driver_register);

void fmc_driver_unregister(struct fmc_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(fmc_driver_unregister);

/*
 * When a device set is registered, all eeproms must be read
 * and all FRUs must be parsed
 */
int fmc_device_register_n(struct fmc_device **devs, int n)
{
	struct fmc_device *fmc, **devarray;
	uint32_t device_id;
	int i, ret = 0;

	if (n < 1)
		return 0;

	/* Check the version of the first data structure (function prints) */
	if (fmc_check_version(devs[0]->version, devs[0]->carrier_name))
		return -EINVAL;

	devarray = kmemdup(devs, n * sizeof(*devs), GFP_KERNEL);
	if (!devarray)
		return -ENOMEM;

	/* Make all other checks before continuing, for all devices */
	for (i = 0; i < n; i++) {
		fmc = devarray[i];
		if (!fmc->hwdev) {
			pr_err("%s: device nr. %i has no hwdev pointer\n",
			       __func__, i);
			ret = -EINVAL;
			break;
		}
		if (fmc->flags == FMC_DEVICE_NO_MEZZANINE) {
			dev_info(fmc->hwdev, "absent mezzanine in slot %d\n",
				 fmc->slot_id);
			continue;
		}
		if (!fmc->eeprom) {
			dev_err(fmc->hwdev, "no eeprom provided for slot %i\n",
				fmc->slot_id);
			ret = -EINVAL;
		}
		if (!fmc->eeprom_addr) {
			dev_err(fmc->hwdev, "no eeprom_addr for slot %i\n",
				fmc->slot_id);
			ret = -EINVAL;
		}
		if (!fmc->carrier_name || !fmc->carrier_data ||
		    !fmc->device_id) {
			dev_err(fmc->hwdev,
				"deivce nr %i: carrier name, "
				"data or dev_id not set\n", i);
			ret = -EINVAL;
		}
		if (ret)
			break;

	}
	if (ret) {
		kfree(devarray);
		return ret;
	}

	/* Validation is ok. Now init and register the devices */
	for (i = 0; i < n; i++) {
		fmc = devarray[i];

		if (fmc->flags == FMC_DEVICE_NO_MEZZANINE)
			continue; /* dev_info already done above */

		fmc->nr_slots = n; /* each slot must know how many are there */
		fmc->devarray = devarray;

		device_initialize(&fmc->dev);
		fmc->dev.release = fmc_release;
		fmc->dev.parent = fmc->hwdev;

		/* Fill the identification stuff (may fail) */
		fmc_fill_id_info(fmc);

		fmc->dev.bus = &fmc_bus_type;

		/* Name from mezzanine info or carrier info. Or 0,1,2.. */
		device_id = fmc->device_id;
		if (!fmc->mezzanine_name)
			dev_set_name(&fmc->dev, "fmc-%04x", device_id);
		else
			dev_set_name(&fmc->dev, "%s-%04x", fmc->mezzanine_name,
				     device_id);
		ret = device_add(&fmc->dev);
		if (ret < 0) {
			dev_err(fmc->hwdev, "Slot %i: Failed in registering "
				"\"%s\"\n", fmc->slot_id, fmc->dev.kobj.name);
			goto out;
		}
		ret = sysfs_create_bin_file(&fmc->dev.kobj, &fmc_eeprom_attr);
		if (ret < 0) {
			dev_err(&fmc->dev, "Failed in registering eeprom\n");
			goto out1;
		}
		/* This device went well, give information to the user */
		fmc_dump_eeprom(fmc);
		fmc_dump_sdb(fmc);
	}
	return 0;

out1:
	device_del(&fmc->dev);
out:
	fmc_free_id_info(fmc);
	put_device(&fmc->dev);

	kfree(devarray);
	for (i--; i >= 0; i--) {
		sysfs_remove_bin_file(&devs[i]->dev.kobj, &fmc_eeprom_attr);
		device_del(&devs[i]->dev);
		fmc_free_id_info(devs[i]);
		put_device(&devs[i]->dev);
	}
	return ret;

}
EXPORT_SYMBOL(fmc_device_register_n);

int fmc_device_register(struct fmc_device *fmc)
{
	return fmc_device_register_n(&fmc, 1);
}
EXPORT_SYMBOL(fmc_device_register);

void fmc_device_unregister_n(struct fmc_device **devs, int n)
{
	int i;

	if (n < 1)
		return;

	/* Free devarray first, not used by the later loop */
	kfree(devs[0]->devarray);

	for (i = 0; i < n; i++) {
		if (devs[i]->flags == FMC_DEVICE_NO_MEZZANINE)
			continue;
		sysfs_remove_bin_file(&devs[i]->dev.kobj, &fmc_eeprom_attr);
		device_del(&devs[i]->dev);
		fmc_free_id_info(devs[i]);
		put_device(&devs[i]->dev);
	}
}
EXPORT_SYMBOL(fmc_device_unregister_n);

void fmc_device_unregister(struct fmc_device *fmc)
{
	fmc_device_unregister_n(&fmc, 1);
}
EXPORT_SYMBOL(fmc_device_unregister);

/* Init and exit are trivial */
static int fmc_init(void)
{
	return bus_register(&fmc_bus_type);
}

static void fmc_exit(void)
{
	bus_unregister(&fmc_bus_type);
}

module_init(fmc_init);
module_exit(fmc_exit);

MODULE_LICENSE("GPL");
