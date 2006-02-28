/*
 * Description:
 * Device Driver for the Infineon Technologies
 * SLD 9630 TT 1.1 and SLB 9635 TT 1.2 Trusted Platform Module
 * Specifications at www.trustedcomputinggroup.org
 *
 * Copyright (C) 2005, Marcel Selhorst <selhorst@crypto.rub.de>
 * Sirrix AG - security technologies, http://www.sirrix.com and
 * Applied Data Security Group, Ruhr-University Bochum, Germany
 * Project-Homepage: http://www.prosec.rub.de/tpm
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/pnp.h>
#include "tpm.h"

/* Infineon specific definitions */
/* maximum number of WTX-packages */
#define	TPM_MAX_WTX_PACKAGES 	50
/* msleep-Time for WTX-packages */
#define	TPM_WTX_MSLEEP_TIME 	20
/* msleep-Time --> Interval to check status register */
#define	TPM_MSLEEP_TIME 	3
/* gives number of max. msleep()-calls before throwing timeout */
#define	TPM_MAX_TRIES		5000
#define	TPM_INFINEON_DEV_VEN_VALUE	0x15D1

/* These values will be filled after PnP-call */
static int TPM_INF_DATA;
static int TPM_INF_ADDR;
static int TPM_INF_BASE;
static int TPM_INF_ADDR_LEN;
static int TPM_INF_PORT_LEN;

/* TPM header definitions */
enum infineon_tpm_header {
	TPM_VL_VER = 0x01,
	TPM_VL_CHANNEL_CONTROL = 0x07,
	TPM_VL_CHANNEL_PERSONALISATION = 0x0A,
	TPM_VL_CHANNEL_TPM = 0x0B,
	TPM_VL_CONTROL = 0x00,
	TPM_INF_NAK = 0x15,
	TPM_CTRL_WTX = 0x10,
	TPM_CTRL_WTX_ABORT = 0x18,
	TPM_CTRL_WTX_ABORT_ACK = 0x18,
	TPM_CTRL_ERROR = 0x20,
	TPM_CTRL_CHAININGACK = 0x40,
	TPM_CTRL_CHAINING = 0x80,
	TPM_CTRL_DATA = 0x04,
	TPM_CTRL_DATA_CHA = 0x84,
	TPM_CTRL_DATA_CHA_ACK = 0xC4
};

enum infineon_tpm_register {
	WRFIFO = 0x00,
	RDFIFO = 0x01,
	STAT = 0x02,
	CMD = 0x03
};

enum infineon_tpm_command_bits {
	CMD_DIS = 0x00,
	CMD_LP = 0x01,
	CMD_RES = 0x02,
	CMD_IRQC = 0x06
};

enum infineon_tpm_status_bits {
	STAT_XFE = 0x00,
	STAT_LPA = 0x01,
	STAT_FOK = 0x02,
	STAT_TOK = 0x03,
	STAT_IRQA = 0x06,
	STAT_RDA = 0x07
};

/* some outgoing values */
enum infineon_tpm_values {
	CHIP_ID1 = 0x20,
	CHIP_ID2 = 0x21,
	TPM_DAR = 0x30,
	RESET_LP_IRQC_DISABLE = 0x41,
	ENABLE_REGISTER_PAIR = 0x55,
	IOLIMH = 0x60,
	IOLIML = 0x61,
	DISABLE_REGISTER_PAIR = 0xAA,
	IDVENL = 0xF1,
	IDVENH = 0xF2,
	IDPDL = 0xF3,
	IDPDH = 0xF4
};

static int number_of_wtx;

static int empty_fifo(struct tpm_chip *chip, int clear_wrfifo)
{
	int status;
	int check = 0;
	int i;

	if (clear_wrfifo) {
		for (i = 0; i < 4096; i++) {
			status = inb(chip->vendor->base + WRFIFO);
			if (status == 0xff) {
				if (check == 5)
					break;
				else
					check++;
			}
		}
	}
	/* Note: The values which are currently in the FIFO of the TPM
	   are thrown away since there is no usage for them. Usually,
	   this has nothing to say, since the TPM will give its answer
	   immediately or will be aborted anyway, so the data here is
	   usually garbage and useless.
	   We have to clean this, because the next communication with
	   the TPM would be rubbish, if there is still some old data
	   in the Read FIFO.
	 */
	i = 0;
	do {
		status = inb(chip->vendor->base + RDFIFO);
		status = inb(chip->vendor->base + STAT);
		i++;
		if (i == TPM_MAX_TRIES)
			return -EIO;
	} while ((status & (1 << STAT_RDA)) != 0);
	return 0;
}

static int wait(struct tpm_chip *chip, int wait_for_bit)
{
	int status;
	int i;
	for (i = 0; i < TPM_MAX_TRIES; i++) {
		status = inb(chip->vendor->base + STAT);
		/* check the status-register if wait_for_bit is set */
		if (status & 1 << wait_for_bit)
			break;
		msleep(TPM_MSLEEP_TIME);
	}
	if (i == TPM_MAX_TRIES) {	/* timeout occurs */
		if (wait_for_bit == STAT_XFE)
			dev_err(chip->dev, "Timeout in wait(STAT_XFE)\n");
		if (wait_for_bit == STAT_RDA)
			dev_err(chip->dev, "Timeout in wait(STAT_RDA)\n");
		return -EIO;
	}
	return 0;
};

static void wait_and_send(struct tpm_chip *chip, u8 sendbyte)
{
	wait(chip, STAT_XFE);
	outb(sendbyte, chip->vendor->base + WRFIFO);
}

    /* Note: WTX means Waiting-Time-Extension. Whenever the TPM needs more
       calculation time, it sends a WTX-package, which has to be acknowledged
       or aborted. This usually occurs if you are hammering the TPM with key
       creation. Set the maximum number of WTX-packages in the definitions
       above, if the number is reached, the waiting-time will be denied
       and the TPM command has to be resend.
     */

static void tpm_wtx(struct tpm_chip *chip)
{
	number_of_wtx++;
	dev_info(chip->dev, "Granting WTX (%02d / %02d)\n",
		 number_of_wtx, TPM_MAX_WTX_PACKAGES);
	wait_and_send(chip, TPM_VL_VER);
	wait_and_send(chip, TPM_CTRL_WTX);
	wait_and_send(chip, 0x00);
	wait_and_send(chip, 0x00);
	msleep(TPM_WTX_MSLEEP_TIME);
}

static void tpm_wtx_abort(struct tpm_chip *chip)
{
	dev_info(chip->dev, "Aborting WTX\n");
	wait_and_send(chip, TPM_VL_VER);
	wait_and_send(chip, TPM_CTRL_WTX_ABORT);
	wait_and_send(chip, 0x00);
	wait_and_send(chip, 0x00);
	number_of_wtx = 0;
	msleep(TPM_WTX_MSLEEP_TIME);
}

static int tpm_inf_recv(struct tpm_chip *chip, u8 * buf, size_t count)
{
	int i;
	int ret;
	u32 size = 0;
	number_of_wtx = 0;

recv_begin:
	/* start receiving header */
	for (i = 0; i < 4; i++) {
		ret = wait(chip, STAT_RDA);
		if (ret)
			return -EIO;
		buf[i] = inb(chip->vendor->base + RDFIFO);
	}

	if (buf[0] != TPM_VL_VER) {
		dev_err(chip->dev,
			"Wrong transport protocol implementation!\n");
		return -EIO;
	}

	if (buf[1] == TPM_CTRL_DATA) {
		/* size of the data received */
		size = ((buf[2] << 8) | buf[3]);

		for (i = 0; i < size; i++) {
			wait(chip, STAT_RDA);
			buf[i] = inb(chip->vendor->base + RDFIFO);
		}

		if ((size == 0x6D00) && (buf[1] == 0x80)) {
			dev_err(chip->dev, "Error handling on vendor layer!\n");
			return -EIO;
		}

		for (i = 0; i < size; i++)
			buf[i] = buf[i + 6];

		size = size - 6;
		return size;
	}

	if (buf[1] == TPM_CTRL_WTX) {
		dev_info(chip->dev, "WTX-package received\n");
		if (number_of_wtx < TPM_MAX_WTX_PACKAGES) {
			tpm_wtx(chip);
			goto recv_begin;
		} else {
			tpm_wtx_abort(chip);
			goto recv_begin;
		}
	}

	if (buf[1] == TPM_CTRL_WTX_ABORT_ACK) {
		dev_info(chip->dev, "WTX-abort acknowledged\n");
		return size;
	}

	if (buf[1] == TPM_CTRL_ERROR) {
		dev_err(chip->dev, "ERROR-package received:\n");
		if (buf[4] == TPM_INF_NAK)
			dev_err(chip->dev,
				"-> Negative acknowledgement"
				" - retransmit command!\n");
		return -EIO;
	}
	return -EIO;
}

static int tpm_inf_send(struct tpm_chip *chip, u8 * buf, size_t count)
{
	int i;
	int ret;
	u8 count_high, count_low, count_4, count_3, count_2, count_1;

	/* Disabling Reset, LP and IRQC */
	outb(RESET_LP_IRQC_DISABLE, chip->vendor->base + CMD);

	ret = empty_fifo(chip, 1);
	if (ret) {
		dev_err(chip->dev, "Timeout while clearing FIFO\n");
		return -EIO;
	}

	ret = wait(chip, STAT_XFE);
	if (ret)
		return -EIO;

	count_4 = (count & 0xff000000) >> 24;
	count_3 = (count & 0x00ff0000) >> 16;
	count_2 = (count & 0x0000ff00) >> 8;
	count_1 = (count & 0x000000ff);
	count_high = ((count + 6) & 0xffffff00) >> 8;
	count_low = ((count + 6) & 0x000000ff);

	/* Sending Header */
	wait_and_send(chip, TPM_VL_VER);
	wait_and_send(chip, TPM_CTRL_DATA);
	wait_and_send(chip, count_high);
	wait_and_send(chip, count_low);

	/* Sending Data Header */
	wait_and_send(chip, TPM_VL_VER);
	wait_and_send(chip, TPM_VL_CHANNEL_TPM);
	wait_and_send(chip, count_4);
	wait_and_send(chip, count_3);
	wait_and_send(chip, count_2);
	wait_and_send(chip, count_1);

	/* Sending Data */
	for (i = 0; i < count; i++) {
		wait_and_send(chip, buf[i]);
	}
	return count;
}

static void tpm_inf_cancel(struct tpm_chip *chip)
{
	/*
	   Since we are using the legacy mode to communicate
	   with the TPM, we have no cancel functions, but have
	   a workaround for interrupting the TPM through WTX.
	 */
}

static u8 tpm_inf_status(struct tpm_chip *chip)
{
	return inb(chip->vendor->base + STAT);
}

static DEVICE_ATTR(pubek, S_IRUGO, tpm_show_pubek, NULL);
static DEVICE_ATTR(pcrs, S_IRUGO, tpm_show_pcrs, NULL);
static DEVICE_ATTR(caps, S_IRUGO, tpm_show_caps, NULL);
static DEVICE_ATTR(cancel, S_IWUSR | S_IWGRP, NULL, tpm_store_cancel);

static struct attribute *inf_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr,
	NULL,
};

static struct attribute_group inf_attr_grp = {.attrs = inf_attrs };

static struct file_operations inf_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_open,
	.read = tpm_read,
	.write = tpm_write,
	.release = tpm_release,
};

static struct tpm_vendor_specific tpm_inf = {
	.recv = tpm_inf_recv,
	.send = tpm_inf_send,
	.cancel = tpm_inf_cancel,
	.status = tpm_inf_status,
	.req_complete_mask = 0,
	.req_complete_val = 0,
	.attr_group = &inf_attr_grp,
	.miscdev = {.fops = &inf_ops,},
};

static const struct pnp_device_id tpm_pnp_tbl[] = {
	/* Infineon TPMs */
	{"IFX0101", 0},
	{"IFX0102", 0},
	{"", 0}
};

MODULE_DEVICE_TABLE(pnp, tpm_pnp_tbl);

static int __devinit tpm_inf_pnp_probe(struct pnp_dev *dev,
				       const struct pnp_device_id *dev_id)
{
	int rc = 0;
	u8 iol, ioh;
	int vendorid[2];
	int version[2];
	int productid[2];
	char chipname[20];

	/* read IO-ports through PnP */
	if (pnp_port_valid(dev, 0) && pnp_port_valid(dev, 1) &&
	    !(pnp_port_flags(dev, 0) & IORESOURCE_DISABLED)) {
		TPM_INF_ADDR = pnp_port_start(dev, 0);
		TPM_INF_ADDR_LEN = pnp_port_len(dev, 0);
		TPM_INF_DATA = (TPM_INF_ADDR + 1);
		TPM_INF_BASE = pnp_port_start(dev, 1);
		TPM_INF_PORT_LEN = pnp_port_len(dev, 1);
		if ((TPM_INF_PORT_LEN < 4) || (TPM_INF_ADDR_LEN < 2)) {
			rc = -EINVAL;
			goto err_last;
		}
		dev_info(&dev->dev, "Found %s with ID %s\n",
			 dev->name, dev_id->id);
		if (!((TPM_INF_BASE >> 8) & 0xff)) {
			rc = -EINVAL;
			goto err_last;
		}
		/* publish my base address and request region */
		tpm_inf.base = TPM_INF_BASE;
		if (request_region
		    (tpm_inf.base, TPM_INF_PORT_LEN, "tpm_infineon0") == NULL) {
			rc = -EINVAL;
			goto err_last;
		}
		if (request_region(TPM_INF_ADDR, TPM_INF_ADDR_LEN,
				"tpm_infineon0") == NULL) {
			rc = -EINVAL;
			goto err_last;
		}
	} else {
		rc = -EINVAL;
		goto err_last;
	}

	/* query chip for its vendor, its version number a.s.o. */
	outb(ENABLE_REGISTER_PAIR, TPM_INF_ADDR);
	outb(IDVENL, TPM_INF_ADDR);
	vendorid[1] = inb(TPM_INF_DATA);
	outb(IDVENH, TPM_INF_ADDR);
	vendorid[0] = inb(TPM_INF_DATA);
	outb(IDPDL, TPM_INF_ADDR);
	productid[1] = inb(TPM_INF_DATA);
	outb(IDPDH, TPM_INF_ADDR);
	productid[0] = inb(TPM_INF_DATA);
	outb(CHIP_ID1, TPM_INF_ADDR);
	version[1] = inb(TPM_INF_DATA);
	outb(CHIP_ID2, TPM_INF_ADDR);
	version[0] = inb(TPM_INF_DATA);

	switch ((productid[0] << 8) | productid[1]) {
	case 6:
		snprintf(chipname, sizeof(chipname), " (SLD 9630 TT 1.1)");
		break;
	case 11:
		snprintf(chipname, sizeof(chipname), " (SLB 9635 TT 1.2)");
		break;
	default:
		snprintf(chipname, sizeof(chipname), " (unknown chip)");
		break;
	}

	if ((vendorid[0] << 8 | vendorid[1]) == (TPM_INFINEON_DEV_VEN_VALUE)) {

		/* configure TPM with IO-ports */
		outb(IOLIMH, TPM_INF_ADDR);
		outb(((tpm_inf.base >> 8) & 0xff), TPM_INF_DATA);
		outb(IOLIML, TPM_INF_ADDR);
		outb((tpm_inf.base & 0xff), TPM_INF_DATA);

		/* control if IO-ports are set correctly */
		outb(IOLIMH, TPM_INF_ADDR);
		ioh = inb(TPM_INF_DATA);
		outb(IOLIML, TPM_INF_ADDR);
		iol = inb(TPM_INF_DATA);

		if ((ioh << 8 | iol) != tpm_inf.base) {
			dev_err(&dev->dev,
				"Could not set IO-ports to 0x%lx\n",
				tpm_inf.base);
			rc = -EIO;
			goto err_release_region;
		}

		/* activate register */
		outb(TPM_DAR, TPM_INF_ADDR);
		outb(0x01, TPM_INF_DATA);
		outb(DISABLE_REGISTER_PAIR, TPM_INF_ADDR);

		/* disable RESET, LP and IRQC */
		outb(RESET_LP_IRQC_DISABLE, tpm_inf.base + CMD);

		/* Finally, we're done, print some infos */
		dev_info(&dev->dev, "TPM found: "
			 "config base 0x%x, "
			 "io base 0x%x, "
			 "chip version %02x%02x, "
			 "vendor id %x%x (Infineon), "
			 "product id %02x%02x"
			 "%s\n",
			 TPM_INF_ADDR,
			 TPM_INF_BASE,
			 version[0], version[1],
			 vendorid[0], vendorid[1],
			 productid[0], productid[1], chipname);

		rc = tpm_register_hardware(&dev->dev, &tpm_inf);
		if (rc < 0) {
			rc = -ENODEV;
			goto err_release_region;
		}
		return 0;
	} else {
		rc = -ENODEV;
		goto err_release_region;
	}

err_release_region:
	release_region(tpm_inf.base, TPM_INF_PORT_LEN);
	release_region(TPM_INF_ADDR, TPM_INF_ADDR_LEN);

err_last:
	return rc;
}

static __devexit void tpm_inf_pnp_remove(struct pnp_dev *dev)
{
	struct tpm_chip *chip = pnp_get_drvdata(dev);

	if (chip) {
		release_region(chip->vendor->base, TPM_INF_PORT_LEN);
		tpm_remove_hardware(chip->dev);
	}
}

static struct pnp_driver tpm_inf_pnp = {
	.name = "tpm_inf_pnp",
	.driver = {
		.owner = THIS_MODULE,
		.suspend = tpm_pm_suspend,
		.resume = tpm_pm_resume,
	},
	.id_table = tpm_pnp_tbl,
	.probe = tpm_inf_pnp_probe,
	.remove = tpm_inf_pnp_remove,
};

static int __init init_inf(void)
{
	return pnp_register_driver(&tpm_inf_pnp);
}

static void __exit cleanup_inf(void)
{
	pnp_unregister_driver(&tpm_inf_pnp);
}

module_init(init_inf);
module_exit(cleanup_inf);

MODULE_AUTHOR("Marcel Selhorst <selhorst@crypto.rub.de>");
MODULE_DESCRIPTION("Driver for Infineon TPM SLD 9630 TT 1.1 / SLB 9635 TT 1.2");
MODULE_VERSION("1.7");
MODULE_LICENSE("GPL");
