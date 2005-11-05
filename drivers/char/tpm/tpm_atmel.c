/*
 * Copyright (C) 2004 IBM Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd_devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org	 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 * 
 */

#include <linux/platform_device.h>
#include "tpm.h"

/* Atmel definitions */
enum tpm_atmel_addr {
	TPM_ATMEL_BASE_ADDR_LO = 0x08,
	TPM_ATMEL_BASE_ADDR_HI = 0x09
};

/* write status bits */
enum tpm_atmel_write_status {
	ATML_STATUS_ABORT = 0x01,
	ATML_STATUS_LASTBYTE = 0x04
};
/* read status bits */
enum tpm_atmel_read_status {
	ATML_STATUS_BUSY = 0x01,
	ATML_STATUS_DATA_AVAIL = 0x02,
	ATML_STATUS_REWRITE = 0x04,
	ATML_STATUS_READY = 0x08
};

static int tpm_atml_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	u8 status, *hdr = buf;
	u32 size;
	int i;
	__be32 *native_size;

	/* start reading header */
	if (count < 6)
		return -EIO;

	for (i = 0; i < 6; i++) {
		status = inb(chip->vendor->base + 1);
		if ((status & ATML_STATUS_DATA_AVAIL) == 0) {
			dev_err(chip->dev,
				"error reading header\n");
			return -EIO;
		}
		*buf++ = inb(chip->vendor->base);
	}

	/* size of the data received */
	native_size = (__force __be32 *) (hdr + 2);
	size = be32_to_cpu(*native_size);

	if (count < size) {
		dev_err(chip->dev,
			"Recv size(%d) less than available space\n", size);
		for (; i < size; i++) {	/* clear the waiting data anyway */
			status = inb(chip->vendor->base + 1);
			if ((status & ATML_STATUS_DATA_AVAIL) == 0) {
				dev_err(chip->dev,
					"error reading data\n");
				return -EIO;
			}
		}
		return -EIO;
	}

	/* read all the data available */
	for (; i < size; i++) {
		status = inb(chip->vendor->base + 1);
		if ((status & ATML_STATUS_DATA_AVAIL) == 0) {
			dev_err(chip->dev,
				"error reading data\n");
			return -EIO;
		}
		*buf++ = inb(chip->vendor->base);
	}

	/* make sure data available is gone */
	status = inb(chip->vendor->base + 1);
	if (status & ATML_STATUS_DATA_AVAIL) {
		dev_err(chip->dev, "data available is stuck\n");
		return -EIO;
	}

	return size;
}

static int tpm_atml_send(struct tpm_chip *chip, u8 *buf, size_t count)
{
	int i;

	dev_dbg(chip->dev, "tpm_atml_send:\n");
	for (i = 0; i < count; i++) {
		dev_dbg(chip->dev, "%d 0x%x(%d)\n",  i, buf[i], buf[i]);
		outb(buf[i], chip->vendor->base);
	}

	return count;
}

static void tpm_atml_cancel(struct tpm_chip *chip)
{
	outb(ATML_STATUS_ABORT, chip->vendor->base + 1);
}

static u8 tpm_atml_status(struct tpm_chip *chip)
{
	return inb(chip->vendor->base + 1);
}

static struct file_operations atmel_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_open,
	.read = tpm_read,
	.write = tpm_write,
	.release = tpm_release,
};

static DEVICE_ATTR(pubek, S_IRUGO, tpm_show_pubek, NULL);
static DEVICE_ATTR(pcrs, S_IRUGO, tpm_show_pcrs, NULL);
static DEVICE_ATTR(caps, S_IRUGO, tpm_show_caps, NULL);
static DEVICE_ATTR(cancel, S_IWUSR |S_IWGRP, NULL, tpm_store_cancel);

static struct attribute* atmel_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr,
	NULL,
};

static struct attribute_group atmel_attr_grp = { .attrs = atmel_attrs };

static struct tpm_vendor_specific tpm_atmel = {
	.recv = tpm_atml_recv,
	.send = tpm_atml_send,
	.cancel = tpm_atml_cancel,
	.status = tpm_atml_status,
	.req_complete_mask = ATML_STATUS_BUSY | ATML_STATUS_DATA_AVAIL,
	.req_complete_val = ATML_STATUS_DATA_AVAIL,
	.req_canceled = ATML_STATUS_READY,
	.attr_group = &atmel_attr_grp,
	.miscdev = { .fops = &atmel_ops, },
};

static struct platform_device *pdev;

static void __devexit tpm_atml_remove(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip) {
		release_region(chip->vendor->base, 2);
		tpm_remove_hardware(chip->dev);
	}
}

static struct device_driver atml_drv = {
	.name = "tpm_atmel",
	.bus = &platform_bus_type,
	.owner = THIS_MODULE,
	.suspend = tpm_pm_suspend,
	.resume = tpm_pm_resume,
};

static int __init init_atmel(void)
{
	int rc = 0;
	int lo, hi;

	driver_register(&atml_drv);

	lo = tpm_read_index(TPM_ADDR, TPM_ATMEL_BASE_ADDR_LO);
	hi = tpm_read_index(TPM_ADDR, TPM_ATMEL_BASE_ADDR_HI);

	tpm_atmel.base = (hi<<8)|lo;

	/* verify that it is an Atmel part */
	if (tpm_read_index(TPM_ADDR, 4) != 'A' || tpm_read_index(TPM_ADDR, 5) != 'T'
	    || tpm_read_index(TPM_ADDR, 6) != 'M' || tpm_read_index(TPM_ADDR, 7) != 'L') {
		return -ENODEV;
	}

	/* verify chip version number is 1.1 */
	if (	(tpm_read_index(TPM_ADDR, 0x00) != 0x01) ||
		(tpm_read_index(TPM_ADDR, 0x01) != 0x01 ))
		return -ENODEV;

	pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if ( !pdev )
		return -ENOMEM;

	pdev->name = "tpm_atmel0";
	pdev->id = -1;
	pdev->num_resources = 0;
	pdev->dev.release = tpm_atml_remove;
	pdev->dev.driver = &atml_drv;

	if ((rc = platform_device_register(pdev)) < 0) {
		kfree(pdev);
		pdev = NULL;
		return rc;
	}

	if (request_region(tpm_atmel.base, 2, "tpm_atmel0") == NULL ) {
		platform_device_unregister(pdev);
		kfree(pdev);
		pdev = NULL;
		return -EBUSY;
	}

	if ((rc = tpm_register_hardware(&pdev->dev, &tpm_atmel)) < 0) {
		release_region(tpm_atmel.base, 2);
		platform_device_unregister(pdev);
		kfree(pdev);
		pdev = NULL;
		return rc;
	}

	dev_info(&pdev->dev, "Atmel TPM 1.1, Base Address: 0x%x\n",
			tpm_atmel.base);
	return 0;
}

static void __exit cleanup_atmel(void)
{
	if (pdev) {
		tpm_atml_remove(&pdev->dev);
		platform_device_unregister(pdev);
		kfree(pdev);
		pdev = NULL;
	}

	driver_unregister(&atml_drv);
}

module_init(init_atmel);
module_exit(cleanup_atmel);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
