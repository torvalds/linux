/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * The software is provided "as is"; the copyright holders disclaim
 * all warranties and liabilities, to the extent permitted by
 * applicable law.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/fmc.h>

#define FF_EEPROM_SIZE		8192	/* The standard eeprom size */
#define FF_MAX_MEZZANINES	4	/* Fakes a multi-mezzanine carrier */

/* The user can pass up to 4 names of eeprom images to load */
static char *ff_eeprom[FF_MAX_MEZZANINES];
static int ff_nr_eeprom;
module_param_array_named(eeprom, ff_eeprom, charp, &ff_nr_eeprom, 0444);

/* The user can ask for a multi-mezzanine carrier, with the default eeprom */
static int ff_nr_dev = 1;
module_param_named(ndev, ff_nr_dev, int, 0444);


/* Lazily, don't support the "standard" module parameters */

/*
 * Eeprom built from these commands:

	../fru-generator -v fake-vendor -n fake-design-for-testing \
		-s 01234 -p none > IPMI-FRU

	gensdbfs . ../fake-eeprom.bin
*/
static char ff_eeimg[FF_MAX_MEZZANINES][FF_EEPROM_SIZE] = {
	{
	0x01, 0x00, 0x00, 0x01, 0x00, 0x0c, 0x00, 0xf2, 0x01, 0x0b, 0x00, 0xb2,
	0x86, 0x87, 0xcb, 0x66, 0x61, 0x6b, 0x65, 0x2d, 0x76, 0x65, 0x6e, 0x64,
	0x6f, 0x72, 0xd7, 0x66, 0x61, 0x6b, 0x65, 0x2d, 0x64, 0x65, 0x73, 0x69,
	0x67, 0x6e, 0x2d, 0x66, 0x6f, 0x72, 0x2d, 0x74, 0x65, 0x73, 0x74, 0x69,
	0x6e, 0x67, 0xc5, 0x30, 0x31, 0x32, 0x33, 0x34, 0xc4, 0x6e, 0x6f, 0x6e,
	0x65, 0xda, 0x32, 0x30, 0x31, 0x32, 0x2d, 0x31, 0x31, 0x2d, 0x31, 0x39,
	0x20, 0x32, 0x32, 0x3a, 0x34, 0x32, 0x3a, 0x33, 0x30, 0x2e, 0x30, 0x37,
	0x34, 0x30, 0x35, 0x35, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87,
	0x02, 0x02, 0x0d, 0xf7, 0xf8, 0x02, 0xb0, 0x04, 0x74, 0x04, 0xec, 0x04,
	0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x02, 0x02, 0x0d, 0x5c, 0x93, 0x01,
	0x4a, 0x01, 0x39, 0x01, 0x5a, 0x01, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x0b,
	0x02, 0x02, 0x0d, 0x63, 0x8c, 0x00, 0xfa, 0x00, 0xed, 0x00, 0x06, 0x01,
	0x00, 0x00, 0x00, 0x00, 0xa0, 0x0f, 0x01, 0x02, 0x0d, 0xfb, 0xf5, 0x05,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x02, 0x0d, 0xfc, 0xf4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x0d, 0xfd, 0xf3, 0x03,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xfa, 0x82, 0x0b, 0xea, 0x8f, 0xa2, 0x12, 0x00, 0x00, 0x1e, 0x44, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x53, 0x44, 0x42, 0x2d, 0x00, 0x03, 0x01, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x01, 0xc4, 0x46, 0x69, 0x6c, 0x65, 0x44, 0x61, 0x74, 0x61,
	0x2e, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x2e, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc0,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc4, 0x46, 0x69, 0x6c, 0x65,
	0x44, 0x61, 0x74, 0x61, 0x6e, 0x61, 0x6d, 0x65, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x6e, 0x61, 0x6d, 0x65, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdf,
	0x46, 0x69, 0x6c, 0x65, 0x44, 0x61, 0x74, 0x61, 0x49, 0x50, 0x4d, 0x49,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x49, 0x50, 0x4d, 0x49,
	0x2d, 0x46, 0x52, 0x55, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x01, 0x66, 0x61, 0x6b, 0x65, 0x0a,
	},
};

struct ff_dev {
	struct fmc_device *fmc[FF_MAX_MEZZANINES];
	struct device dev;
};

static struct ff_dev *ff_current_dev; /* We have 1 carrier, 1 slot */

static int ff_reprogram(struct fmc_device *fmc, struct fmc_driver *drv,
			  char *gw)
{
	const struct firmware *fw;
	int ret;

	if (!gw) {
		/* program golden: success */
		fmc->flags &= ~FMC_DEVICE_HAS_CUSTOM;
		fmc->flags |= FMC_DEVICE_HAS_GOLDEN;
		return 0;
	}

	dev_info(&fmc->dev, "reprogramming with %s\n", gw);
	ret = request_firmware(&fw, gw, &fmc->dev);
	if (ret < 0) {
		dev_warn(&fmc->dev, "request firmware \"%s\": error %i\n",
			 gw, ret);
		goto out;
	}
	fmc->flags &= ~FMC_DEVICE_HAS_GOLDEN;
	fmc->flags |= FMC_DEVICE_HAS_CUSTOM;

out:
	release_firmware(fw);
	return ret;
}

static int ff_irq_request(struct fmc_device *fmc, irq_handler_t handler,
			    char *name, int flags)
{
	return -EOPNOTSUPP;
}

/* FIXME: should also have some fake FMC GPIO mapping */


/*
 * This work function is called when we changed the eeprom. It removes the
 * current fmc device and registers a new one, with different identifiers.
 */
static struct ff_dev *ff_dev_create(void); /* defined later */

static void ff_work_fn(struct work_struct *work)
{
	struct ff_dev *ff = ff_current_dev;
	int ret;

	fmc_device_unregister_n(ff->fmc, ff_nr_dev);
	device_unregister(&ff->dev);
	ff_current_dev = NULL;

	ff = ff_dev_create();
	if (IS_ERR(ff)) {
		pr_warning("%s: can't re-create FMC devices\n", __func__);
		return;
	}
	ret = fmc_device_register_n(ff->fmc, ff_nr_dev);
	if (ret < 0) {
		dev_warn(&ff->dev, "can't re-register FMC devices\n");
		device_unregister(&ff->dev);
		return;
	}

	ff_current_dev = ff;
}

static DECLARE_DELAYED_WORK(ff_work, ff_work_fn);


/* low-level i2c */
static int ff_eeprom_read(struct fmc_device *fmc, uint32_t offset,
		void *buf, size_t size)
{
	if (offset > FF_EEPROM_SIZE)
		return -EINVAL;
	if (offset + size > FF_EEPROM_SIZE)
		size = FF_EEPROM_SIZE - offset;
	memcpy(buf, fmc->eeprom + offset, size);
	return size;
}

static int ff_eeprom_write(struct fmc_device *fmc, uint32_t offset,
		    const void *buf, size_t size)
{
	if (offset > FF_EEPROM_SIZE)
		return -EINVAL;
	if (offset + size > FF_EEPROM_SIZE)
		size = FF_EEPROM_SIZE - offset;
	dev_info(&fmc->dev, "write_eeprom: offset %i, size %zi\n",
		 (int)offset, size);
	memcpy(fmc->eeprom + offset, buf, size);
	schedule_delayed_work(&ff_work, HZ * 2); /* remove, replug, in 2s */
	return size;
}

/* i2c operations for fmc */
static int ff_read_ee(struct fmc_device *fmc, int pos, void *data, int len)
{
	if (!(fmc->flags & FMC_DEVICE_HAS_GOLDEN))
		return -EOPNOTSUPP;
	return ff_eeprom_read(fmc, pos, data, len);
}

static int ff_write_ee(struct fmc_device *fmc, int pos,
			 const void *data, int len)
{
	if (!(fmc->flags & FMC_DEVICE_HAS_GOLDEN))
		return -EOPNOTSUPP;
	return ff_eeprom_write(fmc, pos, data, len);
}

/* readl and writel do not do anything. Don't waste RAM with "base" */
static uint32_t ff_readl(struct fmc_device *fmc, int offset)
{
	return 0;
}

static void ff_writel(struct fmc_device *fmc, uint32_t value, int offset)
{
	return;
}

/* validate is useful so fmc-write-eeprom will not reprogram every 2 seconds */
static int ff_validate(struct fmc_device *fmc, struct fmc_driver *drv)
{
	int i;

	if (!drv->busid_n)
		return 0; /* everyhing is valid */
	for (i = 0; i < drv->busid_n; i++)
		if (drv->busid_val[i] == fmc->device_id)
			return i;
	return -ENOENT;
}



static struct fmc_operations ff_fmc_operations = {
	.read32 =		ff_readl,
	.write32 =		ff_writel,
	.reprogram =		ff_reprogram,
	.irq_request =		ff_irq_request,
	.read_ee =		ff_read_ee,
	.write_ee =		ff_write_ee,
	.validate =		ff_validate,
};

/* This device is kmalloced: release it */
static void ff_dev_release(struct device *dev)
{
	struct ff_dev *ff = container_of(dev, struct ff_dev, dev);
	kfree(ff);
}

static struct fmc_device ff_template_fmc = {
	.version = FMC_VERSION,
	.owner = THIS_MODULE,
	.carrier_name = "fake-fmc-carrier",
	.device_id = 0xf001, /* fool */
	.eeprom_len = sizeof(ff_eeimg[0]),
	.memlen = 0x1000, /* 4k, to show something */
	.op = &ff_fmc_operations,
	.hwdev = NULL, /* filled at creation time */
	.flags = FMC_DEVICE_HAS_GOLDEN,
};

static struct ff_dev *ff_dev_create(void)
{
	struct ff_dev *ff;
	struct fmc_device *fmc;
	int i, ret;

	ff = kzalloc(sizeof(*ff), GFP_KERNEL);
	if (!ff)
		return ERR_PTR(-ENOMEM);
	dev_set_name(&ff->dev, "fake-fmc-carrier");
	ff->dev.release = ff_dev_release;

	ret = device_register(&ff->dev);
	if (ret < 0) {
		put_device(&ff->dev);
		return ERR_PTR(ret);
	}

	/* Create fmc structures that refer to this new "hw" device */
	for (i = 0; i < ff_nr_dev; i++) {
		fmc = kmemdup(&ff_template_fmc, sizeof(ff_template_fmc),
			      GFP_KERNEL);
		fmc->hwdev = &ff->dev;
		fmc->carrier_data = ff;
		fmc->nr_slots = ff_nr_dev;
		/* the following fields are different for each slot */
		fmc->eeprom = ff_eeimg[i];
		fmc->eeprom_addr = 0x50 + 2 * i;
		fmc->slot_id = i;
		ff->fmc[i] = fmc;
		/* increment the identifier, each must be different */
		ff_template_fmc.device_id++;
	}
	return ff;
}

/* init and exit */
static int ff_init(void)
{
	struct ff_dev *ff;
	const struct firmware *fw;
	int i, len, ret = 0;

	/* Replicate the default eeprom for the max number of mezzanines */
	for (i = 1; i < FF_MAX_MEZZANINES; i++)
		memcpy(ff_eeimg[i], ff_eeimg[0], sizeof(ff_eeimg[0]));

	if (ff_nr_eeprom > ff_nr_dev)
		ff_nr_dev = ff_nr_eeprom;

	ff = ff_dev_create();
	if (IS_ERR(ff))
		return PTR_ERR(ff);

	/* If the user passed "eeprom=" as a parameter, fetch them */
	for (i = 0; i < ff_nr_eeprom; i++) {
		if (!strlen(ff_eeprom[i]))
			continue;
		ret = request_firmware(&fw, ff_eeprom[i], &ff->dev);
		if (ret < 0) {
			dev_err(&ff->dev, "Mezzanine %i: can't load \"%s\" "
				"(error %i)\n", i, ff_eeprom[i], -ret);
		} else {
			len = min_t(size_t, fw->size, (size_t)FF_EEPROM_SIZE);
			memcpy(ff_eeimg[i], fw->data, len);
			release_firmware(fw);
			dev_info(&ff->dev, "Mezzanine %i: eeprom \"%s\"\n", i,
				ff_eeprom[i]);
		}
	}

	ret = fmc_device_register_n(ff->fmc, ff_nr_dev);
	if (ret) {
		device_unregister(&ff->dev);
		return ret;
	}
	ff_current_dev = ff;
	return ret;
}

static void ff_exit(void)
{
	if (ff_current_dev) {
		fmc_device_unregister_n(ff_current_dev->fmc, ff_nr_dev);
		device_unregister(&ff_current_dev->dev);
	}
	cancel_delayed_work_sync(&ff_work);
}

module_init(ff_init);
module_exit(ff_exit);

MODULE_LICENSE("Dual BSD/GPL");
