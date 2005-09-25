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

static int tpm_atml_recv(struct tpm_chip *chip, u8 * buf, size_t count)
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
			dev_err(&chip->pci_dev->dev,
				"error reading header\n");
			return -EIO;
		}
		*buf++ = inb(chip->vendor->base);
	}

	/* size of the data received */
	native_size = (__force __be32 *) (hdr + 2);
	size = be32_to_cpu(*native_size);

	if (count < size) {
		dev_err(&chip->pci_dev->dev,
			"Recv size(%d) less than available space\n", size);
		for (; i < size; i++) {	/* clear the waiting data anyway */
			status = inb(chip->vendor->base + 1);
			if ((status & ATML_STATUS_DATA_AVAIL) == 0) {
				dev_err(&chip->pci_dev->dev,
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
			dev_err(&chip->pci_dev->dev,
				"error reading data\n");
			return -EIO;
		}
		*buf++ = inb(chip->vendor->base);
	}

	/* make sure data available is gone */
	status = inb(chip->vendor->base + 1);
	if (status & ATML_STATUS_DATA_AVAIL) {
		dev_err(&chip->pci_dev->dev, "data available is stuck\n");
		return -EIO;
	}

	return size;
}

static int tpm_atml_send(struct tpm_chip *chip, u8 * buf, size_t count)
{
	int i;

	dev_dbg(&chip->pci_dev->dev, "tpm_atml_send: ");
	for (i = 0; i < count; i++) {
		dev_dbg(&chip->pci_dev->dev, "0x%x(%d) ", buf[i], buf[i]);
		outb(buf[i], chip->vendor->base);
	}

	return count;
}

static void tpm_atml_cancel(struct tpm_chip *chip)
{
	outb(ATML_STATUS_ABORT, chip->vendor->base + 1);
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
	0,
};

static struct attribute_group atmel_attr_grp = { .attrs = atmel_attrs };

static struct tpm_vendor_specific tpm_atmel = {
	.recv = tpm_atml_recv,
	.send = tpm_atml_send,
	.cancel = tpm_atml_cancel,
	.req_complete_mask = ATML_STATUS_BUSY | ATML_STATUS_DATA_AVAIL,
	.req_complete_val = ATML_STATUS_DATA_AVAIL,
	.req_canceled = ATML_STATUS_READY,
	.attr_group = &atmel_attr_grp,
	.miscdev = { .fops = &atmel_ops, },
};

static int __devinit tpm_atml_init(struct pci_dev *pci_dev,
				   const struct pci_device_id *pci_id)
{
	u8 version[4];
	int rc = 0;
	int lo, hi;

	if (pci_enable_device(pci_dev))
		return -EIO;

	lo = tpm_read_index(TPM_ADDR, TPM_ATMEL_BASE_ADDR_LO);
	hi = tpm_read_index(TPM_ADDR, TPM_ATMEL_BASE_ADDR_HI);

	tpm_atmel.base = (hi<<8)|lo;
	dev_dbg( &pci_dev->dev, "Operating with base: 0x%x\n", tpm_atmel.base);

	/* verify that it is an Atmel part */
	if (tpm_read_index(TPM_ADDR, 4) != 'A' || tpm_read_index(TPM_ADDR, 5) != 'T'
	    || tpm_read_index(TPM_ADDR, 6) != 'M' || tpm_read_index(TPM_ADDR, 7) != 'L') {
		rc = -ENODEV;
		goto out_err;
	}

	/* query chip for its version number */
	if ((version[0] = tpm_read_index(TPM_ADDR, 0x00)) != 0xFF) {
		version[1] = tpm_read_index(TPM_ADDR, 0x01);
		version[2] = tpm_read_index(TPM_ADDR, 0x02);
		version[3] = tpm_read_index(TPM_ADDR, 0x03);
	} else {
		dev_info(&pci_dev->dev, "version query failed\n");
		rc = -ENODEV;
		goto out_err;
	}

	if ((rc = tpm_register_hardware(pci_dev, &tpm_atmel)) < 0)
		goto out_err;

	dev_info(&pci_dev->dev,
		 "Atmel TPM version %d.%d.%d.%d\n", version[0], version[1],
		 version[2], version[3]);

	return 0;
out_err:
	pci_disable_device(pci_dev);
	return rc;
}

static struct pci_device_id tpm_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_12)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_12)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8111_LPC)},
	{PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6LPC)},
	{0,}
};

MODULE_DEVICE_TABLE(pci, tpm_pci_tbl);

static struct pci_driver atmel_pci_driver = {
	.name = "tpm_atmel",
	.id_table = tpm_pci_tbl,
	.probe = tpm_atml_init,
	.remove = __devexit_p(tpm_remove),
	.suspend = tpm_pm_suspend,
	.resume = tpm_pm_resume,
};

static int __init init_atmel(void)
{
	return pci_register_driver(&atmel_pci_driver);
}

static void __exit cleanup_atmel(void)
{
	pci_unregister_driver(&atmel_pci_driver);
}

module_init(init_atmel);
module_exit(cleanup_atmel);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
