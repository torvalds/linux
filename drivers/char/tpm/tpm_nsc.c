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

/* National definitions */
#define	TPM_NSC_BASE			0x360
#define	TPM_NSC_IRQ			0x07

#define	NSC_LDN_INDEX			0x07
#define	NSC_SID_INDEX			0x20
#define	NSC_LDC_INDEX			0x30
#define	NSC_DIO_INDEX			0x60
#define	NSC_CIO_INDEX			0x62
#define	NSC_IRQ_INDEX			0x70
#define	NSC_ITS_INDEX			0x71

#define	NSC_STATUS			0x01
#define	NSC_COMMAND			0x01
#define	NSC_DATA			0x00

/* status bits */
#define	NSC_STATUS_OBF			0x01	/* output buffer full */
#define	NSC_STATUS_IBF			0x02	/* input buffer full */
#define	NSC_STATUS_F0			0x04	/* F0 */
#define	NSC_STATUS_A2			0x08	/* A2 */
#define	NSC_STATUS_RDY			0x10	/* ready to receive command */
#define	NSC_STATUS_IBR			0x20	/* ready to receive data */

/* command bits */
#define	NSC_COMMAND_NORMAL		0x01	/* normal mode */
#define	NSC_COMMAND_EOC			0x03
#define	NSC_COMMAND_CANCEL		0x22

/*
 * Wait for a certain status to appear
 */
static int wait_for_stat(struct tpm_chip *chip, u8 mask, u8 val, u8 * data)
{
	int expired = 0;
	struct timer_list status_timer =
	    TIMER_INITIALIZER(tpm_time_expired, jiffies + 10 * HZ,
			      (unsigned long) &expired);

	/* status immediately available check */
	*data = inb(chip->vendor->base + NSC_STATUS);
	if ((*data & mask) == val)
		return 0;

	/* wait for status */
	add_timer(&status_timer);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(TPM_TIMEOUT);
		*data = inb(chip->vendor->base + 1);
		if ((*data & mask) == val) {
			del_singleshot_timer_sync(&status_timer);
			return 0;
		}
	}
	while (!expired);

	return -EBUSY;
}

static int nsc_wait_for_ready(struct tpm_chip *chip)
{
	int status;
	int expired = 0;
	struct timer_list status_timer =
	    TIMER_INITIALIZER(tpm_time_expired, jiffies + 100,
			      (unsigned long) &expired);

	/* status immediately available check */
	status = inb(chip->vendor->base + NSC_STATUS);
	if (status & NSC_STATUS_OBF)
		status = inb(chip->vendor->base + NSC_DATA);
	if (status & NSC_STATUS_RDY)
		return 0;

	/* wait for status */
	add_timer(&status_timer);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(TPM_TIMEOUT);
		status = inb(chip->vendor->base + NSC_STATUS);
		if (status & NSC_STATUS_OBF)
			status = inb(chip->vendor->base + NSC_DATA);
		if (status & NSC_STATUS_RDY) {
			del_singleshot_timer_sync(&status_timer);
			return 0;
		}
	}
	while (!expired);

	dev_info(&chip->pci_dev->dev, "wait for ready failed\n");
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
		dev_err(&chip->pci_dev->dev, "F0 timeout\n");
		return -EIO;
	}
	if ((data =
	     inb(chip->vendor->base + NSC_DATA)) != NSC_COMMAND_NORMAL) {
		dev_err(&chip->pci_dev->dev, "not in normal mode (0x%x)\n",
			data);
		return -EIO;
	}

	/* read the whole packet */
	for (p = buffer; p < &buffer[count]; p++) {
		if (wait_for_stat
		    (chip, NSC_STATUS_OBF, NSC_STATUS_OBF, &data) < 0) {
			dev_err(&chip->pci_dev->dev,
				"OBF timeout (while reading data)\n");
			return -EIO;
		}
		if (data & NSC_STATUS_F0)
			break;
		*p = inb(chip->vendor->base + NSC_DATA);
	}

	if ((data & NSC_STATUS_F0) == 0) {
		dev_err(&chip->pci_dev->dev, "F0 not set\n");
		return -EIO;
	}
	if ((data = inb(chip->vendor->base + NSC_DATA)) != NSC_COMMAND_EOC) {
		dev_err(&chip->pci_dev->dev,
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
	outb(NSC_COMMAND_CANCEL, chip->vendor->base + NSC_COMMAND);

	if (nsc_wait_for_ready(chip) != 0)
		return -EIO;

	if (wait_for_stat(chip, NSC_STATUS_IBF, 0, &data) < 0) {
		dev_err(&chip->pci_dev->dev, "IBF timeout\n");
		return -EIO;
	}

	outb(NSC_COMMAND_NORMAL, chip->vendor->base + NSC_COMMAND);
	if (wait_for_stat(chip, NSC_STATUS_IBR, NSC_STATUS_IBR, &data) < 0) {
		dev_err(&chip->pci_dev->dev, "IBR timeout\n");
		return -EIO;
	}

	for (i = 0; i < count; i++) {
		if (wait_for_stat(chip, NSC_STATUS_IBF, 0, &data) < 0) {
			dev_err(&chip->pci_dev->dev,
				"IBF timeout (while writing data)\n");
			return -EIO;
		}
		outb(buf[i], chip->vendor->base + NSC_DATA);
	}

	if (wait_for_stat(chip, NSC_STATUS_IBF, 0, &data) < 0) {
		dev_err(&chip->pci_dev->dev, "IBF timeout\n");
		return -EIO;
	}
	outb(NSC_COMMAND_EOC, chip->vendor->base + NSC_COMMAND);

	return count;
}

static void tpm_nsc_cancel(struct tpm_chip *chip)
{
	outb(NSC_COMMAND_CANCEL, chip->vendor->base + NSC_COMMAND);
}

static struct file_operations nsc_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_open,
	.read = tpm_read,
	.write = tpm_write,
	.release = tpm_release,
};

static struct tpm_vendor_specific tpm_nsc = {
	.recv = tpm_nsc_recv,
	.send = tpm_nsc_send,
	.cancel = tpm_nsc_cancel,
	.req_complete_mask = NSC_STATUS_OBF,
	.req_complete_val = NSC_STATUS_OBF,
	.base = TPM_NSC_BASE,
	.miscdev = { .fops = &nsc_ops, },
	
};

static int __devinit tpm_nsc_init(struct pci_dev *pci_dev,
				  const struct pci_device_id *pci_id)
{
	int rc = 0;

	if (pci_enable_device(pci_dev))
		return -EIO;

	if (tpm_lpc_bus_init(pci_dev, TPM_NSC_BASE)) {
		rc = -ENODEV;
		goto out_err;
	}

	/* verify that it is a National part (SID) */
	if (tpm_read_index(NSC_SID_INDEX) != 0xEF) {
		rc = -ENODEV;
		goto out_err;
	}

	dev_dbg(&pci_dev->dev, "NSC TPM detected\n");
	dev_dbg(&pci_dev->dev,
		"NSC LDN 0x%x, SID 0x%x, SRID 0x%x\n",
		tpm_read_index(0x07), tpm_read_index(0x20),
		tpm_read_index(0x27));
	dev_dbg(&pci_dev->dev,
		"NSC SIOCF1 0x%x SIOCF5 0x%x SIOCF6 0x%x SIOCF8 0x%x\n",
		tpm_read_index(0x21), tpm_read_index(0x25),
		tpm_read_index(0x26), tpm_read_index(0x28));
	dev_dbg(&pci_dev->dev, "NSC IO Base0 0x%x\n",
		(tpm_read_index(0x60) << 8) | tpm_read_index(0x61));
	dev_dbg(&pci_dev->dev, "NSC IO Base1 0x%x\n",
		(tpm_read_index(0x62) << 8) | tpm_read_index(0x63));
	dev_dbg(&pci_dev->dev, "NSC Interrupt number and wakeup 0x%x\n",
		tpm_read_index(0x70));
	dev_dbg(&pci_dev->dev, "NSC IRQ type select 0x%x\n",
		tpm_read_index(0x71));
	dev_dbg(&pci_dev->dev,
		"NSC DMA channel select0 0x%x, select1 0x%x\n",
		tpm_read_index(0x74), tpm_read_index(0x75));
	dev_dbg(&pci_dev->dev,
		"NSC Config "
		"0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		tpm_read_index(0xF0), tpm_read_index(0xF1),
		tpm_read_index(0xF2), tpm_read_index(0xF3),
		tpm_read_index(0xF4), tpm_read_index(0xF5),
		tpm_read_index(0xF6), tpm_read_index(0xF7),
		tpm_read_index(0xF8), tpm_read_index(0xF9));

	dev_info(&pci_dev->dev,
		 "NSC PC21100 TPM revision %d\n",
		 tpm_read_index(0x27) & 0x1F);

	if (tpm_read_index(NSC_LDC_INDEX) == 0)
		dev_info(&pci_dev->dev, ": NSC TPM not active\n");

	/* select PM channel 1 */
	tpm_write_index(NSC_LDN_INDEX, 0x12);
	tpm_read_index(NSC_LDN_INDEX);

	/* disable the DPM module */
	tpm_write_index(NSC_LDC_INDEX, 0);
	tpm_read_index(NSC_LDC_INDEX);

	/* set the data register base addresses */
	tpm_write_index(NSC_DIO_INDEX, TPM_NSC_BASE >> 8);
	tpm_write_index(NSC_DIO_INDEX + 1, TPM_NSC_BASE);
	tpm_read_index(NSC_DIO_INDEX);
	tpm_read_index(NSC_DIO_INDEX + 1);

	/* set the command register base addresses */
	tpm_write_index(NSC_CIO_INDEX, (TPM_NSC_BASE + 1) >> 8);
	tpm_write_index(NSC_CIO_INDEX + 1, (TPM_NSC_BASE + 1));
	tpm_read_index(NSC_DIO_INDEX);
	tpm_read_index(NSC_DIO_INDEX + 1);

	/* set the interrupt number to be used for the host interface */
	tpm_write_index(NSC_IRQ_INDEX, TPM_NSC_IRQ);
	tpm_write_index(NSC_ITS_INDEX, 0x00);
	tpm_read_index(NSC_IRQ_INDEX);

	/* enable the DPM module */
	tpm_write_index(NSC_LDC_INDEX, 0x01);
	tpm_read_index(NSC_LDC_INDEX);

	if ((rc = tpm_register_hardware(pci_dev, &tpm_nsc)) < 0)
		goto out_err;

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
	{PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8111_LPC)},
	{0,}
};

MODULE_DEVICE_TABLE(pci, tpm_pci_tbl);

static struct pci_driver nsc_pci_driver = {
	.name = "tpm_nsc",
	.id_table = tpm_pci_tbl,
	.probe = tpm_nsc_init,
	.remove = __devexit_p(tpm_remove),
	.suspend = tpm_pm_suspend,
	.resume = tpm_pm_resume,
};

static int __init init_nsc(void)
{
	return pci_register_driver(&nsc_pci_driver);
}

static void __exit cleanup_nsc(void)
{
	pci_unregister_driver(&nsc_pci_driver);
}

module_init(init_nsc);
module_exit(cleanup_nsc);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
