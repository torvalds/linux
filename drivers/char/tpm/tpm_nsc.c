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

/* National definitions */
enum tpm_nsc_addr{
	TPM_NSC_IRQ = 0x07,
	TPM_NSC_BASE0_HI = 0x60,
	TPM_NSC_BASE0_LO = 0x61,
	TPM_NSC_BASE1_HI = 0x62,
	TPM_NSC_BASE1_LO = 0x63
};

enum tpm_nsc_index {
	NSC_LDN_INDEX = 0x07,
	NSC_SID_INDEX = 0x20,
	NSC_LDC_INDEX = 0x30,
	NSC_DIO_INDEX = 0x60,
	NSC_CIO_INDEX = 0x62,
	NSC_IRQ_INDEX = 0x70,
	NSC_ITS_INDEX = 0x71
};

enum tpm_nsc_status_loc {
	NSC_STATUS = 0x01,
	NSC_COMMAND = 0x01,
	NSC_DATA = 0x00
};

/* status bits */
enum tpm_nsc_status {
	NSC_STATUS_OBF = 0x01,	/* output buffer full */
	NSC_STATUS_IBF = 0x02,	/* input buffer full */
	NSC_STATUS_F0 = 0x04,	/* F0 */
	NSC_STATUS_A2 = 0x08,	/* A2 */
	NSC_STATUS_RDY = 0x10,	/* ready to receive command */
	NSC_STATUS_IBR = 0x20	/* ready to receive data */
};

/* command bits */
enum tpm_nsc_cmd_mode {
	NSC_COMMAND_NORMAL = 0x01,	/* normal mode */
	NSC_COMMAND_EOC = 0x03,
	NSC_COMMAND_CANCEL = 0x22
};
/*
 * Wait for a certain status to appear
 */
static int wait_for_stat(struct tpm_chip *chip, u8 mask, u8 val, u8 * data)
{
	unsigned long stop;

	/* status immediately available check */
	*data = inb(chip->vendor.base + NSC_STATUS);
	if ((*data & mask) == val)
		return 0;

	/* wait for status */
	stop = jiffies + 10 * HZ;
	do {
		msleep(TPM_TIMEOUT);
		*data = inb(chip->vendor.base + 1);
		if ((*data & mask) == val)
			return 0;
	}
	while (time_before(jiffies, stop));

	return -EBUSY;
}

static int nsc_wait_for_ready(struct tpm_chip *chip)
{
	int status;
	unsigned long stop;

	/* status immediately available check */
	status = inb(chip->vendor.base + NSC_STATUS);
	if (status & NSC_STATUS_OBF)
		status = inb(chip->vendor.base + NSC_DATA);
	if (status & NSC_STATUS_RDY)
		return 0;

	/* wait for status */
	stop = jiffies + 100;
	do {
		msleep(TPM_TIMEOUT);
		status = inb(chip->vendor.base + NSC_STATUS);
		if (status & NSC_STATUS_OBF)
			status = inb(chip->vendor.base + NSC_DATA);
		if (status & NSC_STATUS_RDY)
			return 0;
	}
	while (time_before(jiffies, stop));

	dev_info(chip->dev, "wait for ready failed\n");
	return -EBUSY;
}


static int tpm_nsc_recv(struct tpm_chip *chip, u8 * buf, size_t count)
{
	u8 *buffer = buf;
	u8 data, *p;
	u32 size;
	__be32 *native_size;

	if (count < 6)
		return -EIO;

	if (wait_for_stat(chip, NSC_STATUS_F0, NSC_STATUS_F0, &data) < 0) {
		dev_err(chip->dev, "F0 timeout\n");
		return -EIO;
	}
	if ((data =
	     inb(chip->vendor.base + NSC_DATA)) != NSC_COMMAND_NORMAL) {
		dev_err(chip->dev, "not in normal mode (0x%x)\n",
			data);
		return -EIO;
	}

	/* read the whole packet */
	for (p = buffer; p < &buffer[count]; p++) {
		if (wait_for_stat
		    (chip, NSC_STATUS_OBF, NSC_STATUS_OBF, &data) < 0) {
			dev_err(chip->dev,
				"OBF timeout (while reading data)\n");
			return -EIO;
		}
		if (data & NSC_STATUS_F0)
			break;
		*p = inb(chip->vendor.base + NSC_DATA);
	}

	if ((data & NSC_STATUS_F0) == 0 &&
	(wait_for_stat(chip, NSC_STATUS_F0, NSC_STATUS_F0, &data) < 0)) {
		dev_err(chip->dev, "F0 not set\n");
		return -EIO;
	}
	if ((data = inb(chip->vendor.base + NSC_DATA)) != NSC_COMMAND_EOC) {
		dev_err(chip->dev,
			"expected end of command(0x%x)\n", data);
		return -EIO;
	}

	native_size = (__force __be32 *) (buf + 2);
	size = be32_to_cpu(*native_size);

	if (count < size)
		return -EIO;

	return size;
}

static int tpm_nsc_send(struct tpm_chip *chip, u8 * buf, size_t count)
{
	u8 data;
	int i;

	/*
	 * If we hit the chip with back to back commands it locks up
	 * and never set IBF. Hitting it with this "hammer" seems to
	 * fix it. Not sure why this is needed, we followed the flow
	 * chart in the manual to the letter.
	 */
	outb(NSC_COMMAND_CANCEL, chip->vendor.base + NSC_COMMAND);

	if (nsc_wait_for_ready(chip) != 0)
		return -EIO;

	if (wait_for_stat(chip, NSC_STATUS_IBF, 0, &data) < 0) {
		dev_err(chip->dev, "IBF timeout\n");
		return -EIO;
	}

	outb(NSC_COMMAND_NORMAL, chip->vendor.base + NSC_COMMAND);
	if (wait_for_stat(chip, NSC_STATUS_IBR, NSC_STATUS_IBR, &data) < 0) {
		dev_err(chip->dev, "IBR timeout\n");
		return -EIO;
	}

	for (i = 0; i < count; i++) {
		if (wait_for_stat(chip, NSC_STATUS_IBF, 0, &data) < 0) {
			dev_err(chip->dev,
				"IBF timeout (while writing data)\n");
			return -EIO;
		}
		outb(buf[i], chip->vendor.base + NSC_DATA);
	}

	if (wait_for_stat(chip, NSC_STATUS_IBF, 0, &data) < 0) {
		dev_err(chip->dev, "IBF timeout\n");
		return -EIO;
	}
	outb(NSC_COMMAND_EOC, chip->vendor.base + NSC_COMMAND);

	return count;
}

static void tpm_nsc_cancel(struct tpm_chip *chip)
{
	outb(NSC_COMMAND_CANCEL, chip->vendor.base + NSC_COMMAND);
}

static u8 tpm_nsc_status(struct tpm_chip *chip)
{
	return inb(chip->vendor.base + NSC_STATUS);
}

static struct file_operations nsc_ops = {
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
static DEVICE_ATTR(cancel, S_IWUSR|S_IWGRP, NULL, tpm_store_cancel);

static struct attribute * nsc_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr,
	NULL,
};

static struct attribute_group nsc_attr_grp = { .attrs = nsc_attrs };

static struct tpm_vendor_specific tpm_nsc = {
	.recv = tpm_nsc_recv,
	.send = tpm_nsc_send,
	.cancel = tpm_nsc_cancel,
	.status = tpm_nsc_status,
	.req_complete_mask = NSC_STATUS_OBF,
	.req_complete_val = NSC_STATUS_OBF,
	.req_canceled = NSC_STATUS_RDY,
	.attr_group = &nsc_attr_grp,
	.miscdev = { .fops = &nsc_ops, },
};

static struct platform_device *pdev = NULL;

static void __devexit tpm_nsc_remove(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	if ( chip ) {
		release_region(chip->vendor.base, 2);
		tpm_remove_hardware(chip->dev);
	}
}

static struct device_driver nsc_drv = {
	.name = "tpm_nsc",
	.bus = &platform_bus_type,
	.owner = THIS_MODULE,
	.suspend = tpm_pm_suspend,
	.resume = tpm_pm_resume,
};

static int __init init_nsc(void)
{
	int rc = 0;
	int lo, hi;
	int nscAddrBase = TPM_ADDR;


	/* verify that it is a National part (SID) */
	if (tpm_read_index(TPM_ADDR, NSC_SID_INDEX) != 0xEF) {
		nscAddrBase = (tpm_read_index(TPM_SUPERIO_ADDR, 0x2C)<<8)|
			(tpm_read_index(TPM_SUPERIO_ADDR, 0x2B)&0xFE);
		if (tpm_read_index(nscAddrBase, NSC_SID_INDEX) != 0xF6)
			return -ENODEV;
	}

	driver_register(&nsc_drv);

	hi = tpm_read_index(nscAddrBase, TPM_NSC_BASE0_HI);
	lo = tpm_read_index(nscAddrBase, TPM_NSC_BASE0_LO);
	tpm_nsc.base = (hi<<8) | lo;

	/* enable the DPM module */
	tpm_write_index(nscAddrBase, NSC_LDC_INDEX, 0x01);

	pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!pdev) {
		rc = -ENOMEM;
		goto err_unreg_drv;
	}

	pdev->name = "tpm_nscl0";
	pdev->id = -1;
	pdev->num_resources = 0;
	pdev->dev.release = tpm_nsc_remove;
	pdev->dev.driver = &nsc_drv;

	if ((rc = platform_device_register(pdev)) < 0)
		goto err_free_dev;

	if (request_region(tpm_nsc.base, 2, "tpm_nsc0") == NULL ) {
		rc = -EBUSY;
		goto err_unreg_dev;
	}

	if ((rc = tpm_register_hardware(&pdev->dev, &tpm_nsc)) < 0)
		goto err_rel_reg;

	dev_dbg(&pdev->dev, "NSC TPM detected\n");
	dev_dbg(&pdev->dev,
		"NSC LDN 0x%x, SID 0x%x, SRID 0x%x\n",
		tpm_read_index(nscAddrBase,0x07), tpm_read_index(nscAddrBase,0x20),
		tpm_read_index(nscAddrBase,0x27));
	dev_dbg(&pdev->dev,
		"NSC SIOCF1 0x%x SIOCF5 0x%x SIOCF6 0x%x SIOCF8 0x%x\n",
		tpm_read_index(nscAddrBase,0x21), tpm_read_index(nscAddrBase,0x25),
		tpm_read_index(nscAddrBase,0x26), tpm_read_index(nscAddrBase,0x28));
	dev_dbg(&pdev->dev, "NSC IO Base0 0x%x\n",
		(tpm_read_index(nscAddrBase,0x60) << 8) | tpm_read_index(nscAddrBase,0x61));
	dev_dbg(&pdev->dev, "NSC IO Base1 0x%x\n",
		(tpm_read_index(nscAddrBase,0x62) << 8) | tpm_read_index(nscAddrBase,0x63));
	dev_dbg(&pdev->dev, "NSC Interrupt number and wakeup 0x%x\n",
		tpm_read_index(nscAddrBase,0x70));
	dev_dbg(&pdev->dev, "NSC IRQ type select 0x%x\n",
		tpm_read_index(nscAddrBase,0x71));
	dev_dbg(&pdev->dev,
		"NSC DMA channel select0 0x%x, select1 0x%x\n",
		tpm_read_index(nscAddrBase,0x74), tpm_read_index(nscAddrBase,0x75));
	dev_dbg(&pdev->dev,
		"NSC Config "
		"0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		tpm_read_index(nscAddrBase,0xF0), tpm_read_index(nscAddrBase,0xF1),
		tpm_read_index(nscAddrBase,0xF2), tpm_read_index(nscAddrBase,0xF3),
		tpm_read_index(nscAddrBase,0xF4), tpm_read_index(nscAddrBase,0xF5),
		tpm_read_index(nscAddrBase,0xF6), tpm_read_index(nscAddrBase,0xF7),
		tpm_read_index(nscAddrBase,0xF8), tpm_read_index(nscAddrBase,0xF9));

	dev_info(&pdev->dev,
		 "NSC TPM revision %d\n",
		 tpm_read_index(nscAddrBase, 0x27) & 0x1F);

	return 0;

err_rel_reg:
	release_region(tpm_nsc.base, 2);
err_unreg_dev:
	platform_device_unregister(pdev);
err_free_dev:
	kfree(pdev);
err_unreg_drv:
	driver_unregister(&nsc_drv);
	return rc;
}

static void __exit cleanup_nsc(void)
{
	if (pdev) {
		tpm_nsc_remove(&pdev->dev);
		platform_device_unregister(pdev);
		kfree(pdev);
		pdev = NULL;
	}

	driver_unregister(&nsc_drv);
}

module_init(init_nsc);
module_exit(cleanup_nsc);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
