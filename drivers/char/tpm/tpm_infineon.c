/*
 * Description:
 * Device Driver for the Infineon Technologies
 * SLD 9630 TT 1.1 and SLB 9635 TT 1.2 Trusted Platform Module
 * Specifications at www.trustedcomputinggroup.org
 *
 * Copyright (C) 2005, Marcel Selhorst <tpmdd@selhorst.net>
 * Sirrix AG - security technologies <tpmdd@sirrix.com> and
 * Applied Data Security Group, Ruhr-University Bochum, Germany
 * Project-Homepage: http://www.trust.rub.de/projects/linux-device-driver-infineon-tpm/ 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/init.h>
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

#define TPM_INF_IO_PORT		0x0
#define TPM_INF_IO_MEM		0x1

#define TPM_INF_ADDR		0x0
#define TPM_INF_DATA		0x1

struct tpm_inf_dev {
	int iotype;

	void __iomem *mem_base;	/* MMIO ioremap'd addr */
	unsigned long map_base;	/* phys MMIO base */
	unsigned long map_size;	/* MMIO region size */
	unsigned int index_off;	/* index register offset */

	unsigned int data_regs;	/* Data registers */
	unsigned int data_size;

	unsigned int config_port;	/* IO Port config index reg */
	unsigned int config_size;
};

static struct tpm_inf_dev tpm_dev;

static inline void tpm_data_out(unsigned char data, unsigned char offset)
{
	if (tpm_dev.iotype == TPM_INF_IO_PORT)
		outb(data, tpm_dev.data_regs + offset);
	else
		writeb(data, tpm_dev.mem_base + tpm_dev.data_regs + offset);
}

static inline unsigned char tpm_data_in(unsigned char offset)
{
	if (tpm_dev.iotype == TPM_INF_IO_PORT)
		return inb(tpm_dev.data_regs + offset);
	else
		return readb(tpm_dev.mem_base + tpm_dev.data_regs + offset);
}

static inline void tpm_config_out(unsigned char data, unsigned char offset)
{
	if (tpm_dev.iotype == TPM_INF_IO_PORT)
		outb(data, tpm_dev.config_port + offset);
	else
		writeb(data, tpm_dev.mem_base + tpm_dev.index_off + offset);
}

static inline unsigned char tpm_config_in(unsigned char offset)
{
	if (tpm_dev.iotype == TPM_INF_IO_PORT)
		return inb(tpm_dev.config_port + offset);
	else
		return readb(tpm_dev.mem_base + tpm_dev.index_off + offset);
}

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
			status = tpm_data_in(WRFIFO);
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
		status = tpm_data_in(RDFIFO);
		status = tpm_data_in(STAT);
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
		status = tpm_data_in(STAT);
		/* check the status-register if wait_for_bit is set */
		if (status & 1 << wait_for_bit)
			break;
		msleep(TPM_MSLEEP_TIME);
	}
	if (i == TPM_MAX_TRIES) {	/* timeout occurs */
		if (wait_for_bit == STAT_XFE)
			dev_err(&chip->dev, "Timeout in wait(STAT_XFE)\n");
		if (wait_for_bit == STAT_RDA)
			dev_err(&chip->dev, "Timeout in wait(STAT_RDA)\n");
		return -EIO;
	}
	return 0;
};

static void wait_and_send(struct tpm_chip *chip, u8 sendbyte)
{
	wait(chip, STAT_XFE);
	tpm_data_out(sendbyte, WRFIFO);
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
	dev_info(&chip->dev, "Granting WTX (%02d / %02d)\n",
		 number_of_wtx, TPM_MAX_WTX_PACKAGES);
	wait_and_send(chip, TPM_VL_VER);
	wait_and_send(chip, TPM_CTRL_WTX);
	wait_and_send(chip, 0x00);
	wait_and_send(chip, 0x00);
	msleep(TPM_WTX_MSLEEP_TIME);
}

static void tpm_wtx_abort(struct tpm_chip *chip)
{
	dev_info(&chip->dev, "Aborting WTX\n");
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
		buf[i] = tpm_data_in(RDFIFO);
	}

	if (buf[0] != TPM_VL_VER) {
		dev_err(&chip->dev,
			"Wrong transport protocol implementation!\n");
		return -EIO;
	}

	if (buf[1] == TPM_CTRL_DATA) {
		/* size of the data received */
		size = ((buf[2] << 8) | buf[3]);

		for (i = 0; i < size; i++) {
			wait(chip, STAT_RDA);
			buf[i] = tpm_data_in(RDFIFO);
		}

		if ((size == 0x6D00) && (buf[1] == 0x80)) {
			dev_err(&chip->dev, "Error handling on vendor layer!\n");
			return -EIO;
		}

		for (i = 0; i < size; i++)
			buf[i] = buf[i + 6];

		size = size - 6;
		return size;
	}

	if (buf[1] == TPM_CTRL_WTX) {
		dev_info(&chip->dev, "WTX-package received\n");
		if (number_of_wtx < TPM_MAX_WTX_PACKAGES) {
			tpm_wtx(chip);
			goto recv_begin;
		} else {
			tpm_wtx_abort(chip);
			goto recv_begin;
		}
	}

	if (buf[1] == TPM_CTRL_WTX_ABORT_ACK) {
		dev_info(&chip->dev, "WTX-abort acknowledged\n");
		return size;
	}

	if (buf[1] == TPM_CTRL_ERROR) {
		dev_err(&chip->dev, "ERROR-package received:\n");
		if (buf[4] == TPM_INF_NAK)
			dev_err(&chip->dev,
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
	tpm_data_out(RESET_LP_IRQC_DISABLE, CMD);

	ret = empty_fifo(chip, 1);
	if (ret) {
		dev_err(&chip->dev, "Timeout while clearing FIFO\n");
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
	return tpm_data_in(STAT);
}

static const struct tpm_class_ops tpm_inf = {
	.recv = tpm_inf_recv,
	.send = tpm_inf_send,
	.cancel = tpm_inf_cancel,
	.status = tpm_inf_status,
	.req_complete_mask = 0,
	.req_complete_val = 0,
};

static const struct pnp_device_id tpm_inf_pnp_tbl[] = {
	/* Infineon TPMs */
	{"IFX0101", 0},
	{"IFX0102", 0},
	{"", 0}
};

MODULE_DEVICE_TABLE(pnp, tpm_inf_pnp_tbl);

static int tpm_inf_pnp_probe(struct pnp_dev *dev,
				       const struct pnp_device_id *dev_id)
{
	int rc = 0;
	u8 iol, ioh;
	int vendorid[2];
	int version[2];
	int productid[2];
	char chipname[20];
	struct tpm_chip *chip;

	/* read IO-ports through PnP */
	if (pnp_port_valid(dev, 0) && pnp_port_valid(dev, 1) &&
	    !(pnp_port_flags(dev, 0) & IORESOURCE_DISABLED)) {

		tpm_dev.iotype = TPM_INF_IO_PORT;

		tpm_dev.config_port = pnp_port_start(dev, 0);
		tpm_dev.config_size = pnp_port_len(dev, 0);
		tpm_dev.data_regs = pnp_port_start(dev, 1);
		tpm_dev.data_size = pnp_port_len(dev, 1);
		if ((tpm_dev.data_size < 4) || (tpm_dev.config_size < 2)) {
			rc = -EINVAL;
			goto err_last;
		}
		dev_info(&dev->dev, "Found %s with ID %s\n",
			 dev->name, dev_id->id);
		if (!((tpm_dev.data_regs >> 8) & 0xff)) {
			rc = -EINVAL;
			goto err_last;
		}
		/* publish my base address and request region */
		if (request_region(tpm_dev.data_regs, tpm_dev.data_size,
				   "tpm_infineon0") == NULL) {
			rc = -EINVAL;
			goto err_last;
		}
		if (request_region(tpm_dev.config_port, tpm_dev.config_size,
				   "tpm_infineon0") == NULL) {
			release_region(tpm_dev.data_regs, tpm_dev.data_size);
			rc = -EINVAL;
			goto err_last;
		}
	} else if (pnp_mem_valid(dev, 0) &&
		   !(pnp_mem_flags(dev, 0) & IORESOURCE_DISABLED)) {

		tpm_dev.iotype = TPM_INF_IO_MEM;

		tpm_dev.map_base = pnp_mem_start(dev, 0);
		tpm_dev.map_size = pnp_mem_len(dev, 0);

		dev_info(&dev->dev, "Found %s with ID %s\n",
			 dev->name, dev_id->id);

		/* publish my base address and request region */
		if (request_mem_region(tpm_dev.map_base, tpm_dev.map_size,
				       "tpm_infineon0") == NULL) {
			rc = -EINVAL;
			goto err_last;
		}

		tpm_dev.mem_base = ioremap(tpm_dev.map_base, tpm_dev.map_size);
		if (tpm_dev.mem_base == NULL) {
			release_mem_region(tpm_dev.map_base, tpm_dev.map_size);
			rc = -EINVAL;
			goto err_last;
		}

		/*
		 * The only known MMIO based Infineon TPM system provides
		 * a single large mem region with the device config
		 * registers at the default TPM_ADDR.  The data registers
		 * seem like they could be placed anywhere within the MMIO
		 * region, but lets just put them at zero offset.
		 */
		tpm_dev.index_off = TPM_ADDR;
		tpm_dev.data_regs = 0x0;
	} else {
		rc = -EINVAL;
		goto err_last;
	}

	/* query chip for its vendor, its version number a.s.o. */
	tpm_config_out(ENABLE_REGISTER_PAIR, TPM_INF_ADDR);
	tpm_config_out(IDVENL, TPM_INF_ADDR);
	vendorid[1] = tpm_config_in(TPM_INF_DATA);
	tpm_config_out(IDVENH, TPM_INF_ADDR);
	vendorid[0] = tpm_config_in(TPM_INF_DATA);
	tpm_config_out(IDPDL, TPM_INF_ADDR);
	productid[1] = tpm_config_in(TPM_INF_DATA);
	tpm_config_out(IDPDH, TPM_INF_ADDR);
	productid[0] = tpm_config_in(TPM_INF_DATA);
	tpm_config_out(CHIP_ID1, TPM_INF_ADDR);
	version[1] = tpm_config_in(TPM_INF_DATA);
	tpm_config_out(CHIP_ID2, TPM_INF_ADDR);
	version[0] = tpm_config_in(TPM_INF_DATA);

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
		tpm_config_out(IOLIMH, TPM_INF_ADDR);
		tpm_config_out((tpm_dev.data_regs >> 8) & 0xff, TPM_INF_DATA);
		tpm_config_out(IOLIML, TPM_INF_ADDR);
		tpm_config_out((tpm_dev.data_regs & 0xff), TPM_INF_DATA);

		/* control if IO-ports are set correctly */
		tpm_config_out(IOLIMH, TPM_INF_ADDR);
		ioh = tpm_config_in(TPM_INF_DATA);
		tpm_config_out(IOLIML, TPM_INF_ADDR);
		iol = tpm_config_in(TPM_INF_DATA);

		if ((ioh << 8 | iol) != tpm_dev.data_regs) {
			dev_err(&dev->dev,
				"Could not set IO-data registers to 0x%x\n",
				tpm_dev.data_regs);
			rc = -EIO;
			goto err_release_region;
		}

		/* activate register */
		tpm_config_out(TPM_DAR, TPM_INF_ADDR);
		tpm_config_out(0x01, TPM_INF_DATA);
		tpm_config_out(DISABLE_REGISTER_PAIR, TPM_INF_ADDR);

		/* disable RESET, LP and IRQC */
		tpm_data_out(RESET_LP_IRQC_DISABLE, CMD);

		/* Finally, we're done, print some infos */
		dev_info(&dev->dev, "TPM found: "
			 "config base 0x%lx, "
			 "data base 0x%lx, "
			 "chip version 0x%02x%02x, "
			 "vendor id 0x%x%x (Infineon), "
			 "product id 0x%02x%02x"
			 "%s\n",
			 tpm_dev.iotype == TPM_INF_IO_PORT ?
			 tpm_dev.config_port :
			 tpm_dev.map_base + tpm_dev.index_off,
			 tpm_dev.iotype == TPM_INF_IO_PORT ?
			 tpm_dev.data_regs :
			 tpm_dev.map_base + tpm_dev.data_regs,
			 version[0], version[1],
			 vendorid[0], vendorid[1],
			 productid[0], productid[1], chipname);

		chip = tpmm_chip_alloc(&dev->dev, &tpm_inf);
		if (IS_ERR(chip)) {
			rc = PTR_ERR(chip);
			goto err_release_region;
		}

		rc = tpm_chip_register(chip);
		if (rc)
			goto err_release_region;

		return 0;
	} else {
		rc = -ENODEV;
		goto err_release_region;
	}

err_release_region:
	if (tpm_dev.iotype == TPM_INF_IO_PORT) {
		release_region(tpm_dev.data_regs, tpm_dev.data_size);
		release_region(tpm_dev.config_port, tpm_dev.config_size);
	} else {
		iounmap(tpm_dev.mem_base);
		release_mem_region(tpm_dev.map_base, tpm_dev.map_size);
	}

err_last:
	return rc;
}

static void tpm_inf_pnp_remove(struct pnp_dev *dev)
{
	struct tpm_chip *chip = pnp_get_drvdata(dev);

	tpm_chip_unregister(chip);

	if (tpm_dev.iotype == TPM_INF_IO_PORT) {
		release_region(tpm_dev.data_regs, tpm_dev.data_size);
		release_region(tpm_dev.config_port,
			       tpm_dev.config_size);
	} else {
		iounmap(tpm_dev.mem_base);
		release_mem_region(tpm_dev.map_base, tpm_dev.map_size);
	}
}

#ifdef CONFIG_PM_SLEEP
static int tpm_inf_resume(struct device *dev)
{
	/* Re-configure TPM after suspending */
	tpm_config_out(ENABLE_REGISTER_PAIR, TPM_INF_ADDR);
	tpm_config_out(IOLIMH, TPM_INF_ADDR);
	tpm_config_out((tpm_dev.data_regs >> 8) & 0xff, TPM_INF_DATA);
	tpm_config_out(IOLIML, TPM_INF_ADDR);
	tpm_config_out((tpm_dev.data_regs & 0xff), TPM_INF_DATA);
	/* activate register */
	tpm_config_out(TPM_DAR, TPM_INF_ADDR);
	tpm_config_out(0x01, TPM_INF_DATA);
	tpm_config_out(DISABLE_REGISTER_PAIR, TPM_INF_ADDR);
	/* disable RESET, LP and IRQC */
	tpm_data_out(RESET_LP_IRQC_DISABLE, CMD);
	return tpm_pm_resume(dev);
}
#endif
static SIMPLE_DEV_PM_OPS(tpm_inf_pm, tpm_pm_suspend, tpm_inf_resume);

static struct pnp_driver tpm_inf_pnp_driver = {
	.name = "tpm_inf_pnp",
	.id_table = tpm_inf_pnp_tbl,
	.probe = tpm_inf_pnp_probe,
	.remove = tpm_inf_pnp_remove,
	.driver = {
		.pm = &tpm_inf_pm,
	}
};

module_pnp_driver(tpm_inf_pnp_driver);

MODULE_AUTHOR("Marcel Selhorst <tpmdd@sirrix.com>");
MODULE_DESCRIPTION("Driver for Infineon TPM SLD 9630 TT 1.1 / SLB 9635 TT 1.2");
MODULE_VERSION("1.9.2");
MODULE_LICENSE("GPL");
