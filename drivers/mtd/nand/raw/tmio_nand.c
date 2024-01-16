/*
 * Toshiba TMIO NAND flash controller driver
 *
 * Slightly murky pre-git history of the driver:
 *
 * Copyright (c) Ian Molton 2004, 2005, 2008
 *    Original work, independent of sharps code. Included hardware ECC support.
 *    Hard ECC did not work for writes in the early revisions.
 * Copyright (c) Dirk Opfer 2005.
 *    Modifications developed from sharps code but
 *    NOT containing any, ported onto Ians base.
 * Copyright (c) Chris Humbert 2005
 * Copyright (c) Dmitry Baryshkov 2008
 *    Minor fixes
 *
 * Parts copyright Sebastian Carlier
 *
 * This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>

/*--------------------------------------------------------------------------*/

/*
 * NAND Flash Host Controller Configuration Register
 */
#define CCR_COMMAND	0x04	/* w Command				*/
#define CCR_BASE	0x10	/* l NAND Flash Control Reg Base Addr	*/
#define CCR_INTP	0x3d	/* b Interrupt Pin			*/
#define CCR_INTE	0x48	/* b Interrupt Enable			*/
#define CCR_EC		0x4a	/* b Event Control			*/
#define CCR_ICC		0x4c	/* b Internal Clock Control		*/
#define CCR_ECCC	0x5b	/* b ECC Control			*/
#define CCR_NFTC	0x60	/* b NAND Flash Transaction Control	*/
#define CCR_NFM		0x61	/* b NAND Flash Monitor			*/
#define CCR_NFPSC	0x62	/* b NAND Flash Power Supply Control	*/
#define CCR_NFDC	0x63	/* b NAND Flash Detect Control		*/

/*
 * NAND Flash Control Register
 */
#define FCR_DATA	0x00	/* bwl Data Register			*/
#define FCR_MODE	0x04	/* b Mode Register			*/
#define FCR_STATUS	0x05	/* b Status Register			*/
#define FCR_ISR		0x06	/* b Interrupt Status Register		*/
#define FCR_IMR		0x07	/* b Interrupt Mask Register		*/

/* FCR_MODE Register Command List */
#define FCR_MODE_DATA	0x94	/* Data Data_Mode */
#define FCR_MODE_COMMAND 0x95	/* Data Command_Mode */
#define FCR_MODE_ADDRESS 0x96	/* Data Address_Mode */

#define FCR_MODE_HWECC_CALC	0xB4	/* HW-ECC Data */
#define FCR_MODE_HWECC_RESULT	0xD4	/* HW-ECC Calc result Read_Mode */
#define FCR_MODE_HWECC_RESET	0xF4	/* HW-ECC Reset */

#define FCR_MODE_POWER_ON	0x0C	/* Power Supply ON  to SSFDC card */
#define FCR_MODE_POWER_OFF	0x08	/* Power Supply OFF to SSFDC card */

#define FCR_MODE_LED_OFF	0x00	/* LED OFF */
#define FCR_MODE_LED_ON		0x04	/* LED ON */

#define FCR_MODE_EJECT_ON	0x68	/* Ejection events active  */
#define FCR_MODE_EJECT_OFF	0x08	/* Ejection events ignored */

#define FCR_MODE_LOCK		0x6C	/* Lock_Mode. Eject Switch Invalid */
#define FCR_MODE_UNLOCK		0x0C	/* UnLock_Mode. Eject Switch is valid */

#define FCR_MODE_CONTROLLER_ID	0x40	/* Controller ID Read */
#define FCR_MODE_STANDBY	0x00	/* SSFDC card Changes Standby State */

#define FCR_MODE_WE		0x80
#define FCR_MODE_ECC1		0x40
#define FCR_MODE_ECC0		0x20
#define FCR_MODE_CE		0x10
#define FCR_MODE_PCNT1		0x08
#define FCR_MODE_PCNT0		0x04
#define FCR_MODE_ALE		0x02
#define FCR_MODE_CLE		0x01

#define FCR_STATUS_BUSY		0x80

/*--------------------------------------------------------------------------*/

struct tmio_nand {
	struct nand_controller controller;
	struct nand_chip chip;
	struct completion comp;

	struct platform_device *dev;

	void __iomem *ccr;
	void __iomem *fcr;
	unsigned long fcr_base;

	unsigned int irq;

	/* for tmio_nand_read_byte */
	u8			read;
	unsigned read_good:1;
};

static inline struct tmio_nand *mtd_to_tmio(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct tmio_nand, chip);
}


/*--------------------------------------------------------------------------*/

static void tmio_nand_hwcontrol(struct nand_chip *chip, int cmd,
				unsigned int ctrl)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));

	if (ctrl & NAND_CTRL_CHANGE) {
		u8 mode;

		if (ctrl & NAND_NCE) {
			mode = FCR_MODE_DATA;

			if (ctrl & NAND_CLE)
				mode |=  FCR_MODE_CLE;
			else
				mode &= ~FCR_MODE_CLE;

			if (ctrl & NAND_ALE)
				mode |=  FCR_MODE_ALE;
			else
				mode &= ~FCR_MODE_ALE;
		} else {
			mode = FCR_MODE_STANDBY;
		}

		tmio_iowrite8(mode, tmio->fcr + FCR_MODE);
		tmio->read_good = 0;
	}

	if (cmd != NAND_CMD_NONE)
		tmio_iowrite8(cmd, chip->legacy.IO_ADDR_W);
}

static int tmio_nand_dev_ready(struct nand_chip *chip)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));

	return !(tmio_ioread8(tmio->fcr + FCR_STATUS) & FCR_STATUS_BUSY);
}

static irqreturn_t tmio_irq(int irq, void *__tmio)
{
	struct tmio_nand *tmio = __tmio;

	/* disable RDYREQ interrupt */
	tmio_iowrite8(0x00, tmio->fcr + FCR_IMR);
	complete(&tmio->comp);

	return IRQ_HANDLED;
}

/*
  *The TMIO core has a RDYREQ interrupt on the posedge of #SMRB.
  *This interrupt is normally disabled, but for long operations like
  *erase and write, we enable it to wake us up.  The irq handler
  *disables the interrupt.
 */
static int tmio_nand_wait(struct nand_chip *nand_chip)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(nand_chip));
	long timeout;
	u8 status;

	/* enable RDYREQ interrupt */

	tmio_iowrite8(0x0f, tmio->fcr + FCR_ISR);
	reinit_completion(&tmio->comp);
	tmio_iowrite8(0x81, tmio->fcr + FCR_IMR);

	timeout = 400;
	timeout = wait_for_completion_timeout(&tmio->comp,
					      msecs_to_jiffies(timeout));

	if (unlikely(!tmio_nand_dev_ready(nand_chip))) {
		tmio_iowrite8(0x00, tmio->fcr + FCR_IMR);
		dev_warn(&tmio->dev->dev, "still busy after 400 ms\n");

	} else if (unlikely(!timeout)) {
		tmio_iowrite8(0x00, tmio->fcr + FCR_IMR);
		dev_warn(&tmio->dev->dev, "timeout waiting for interrupt\n");
	}

	nand_status_op(nand_chip, &status);
	return status;
}

/*
  *The TMIO controller combines two 8-bit data bytes into one 16-bit
  *word. This function separates them so nand_base.c works as expected,
  *especially its NAND_CMD_READID routines.
 *
  *To prevent stale data from being read, tmio_nand_hwcontrol() clears
  *tmio->read_good.
 */
static u_char tmio_nand_read_byte(struct nand_chip *chip)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));
	unsigned int data;

	if (tmio->read_good--)
		return tmio->read;

	data = tmio_ioread16(tmio->fcr + FCR_DATA);
	tmio->read = data >> 8;
	return data;
}

/*
  *The TMIO controller converts an 8-bit NAND interface to a 16-bit
  *bus interface, so all data reads and writes must be 16-bit wide.
  *Thus, we implement 16-bit versions of the read, write, and verify
  *buffer functions.
 */
static void
tmio_nand_write_buf(struct nand_chip *chip, const u_char *buf, int len)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));

	tmio_iowrite16_rep(tmio->fcr + FCR_DATA, buf, len >> 1);
}

static void tmio_nand_read_buf(struct nand_chip *chip, u_char *buf, int len)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));

	tmio_ioread16_rep(tmio->fcr + FCR_DATA, buf, len >> 1);
}

static void tmio_nand_enable_hwecc(struct nand_chip *chip, int mode)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));

	tmio_iowrite8(FCR_MODE_HWECC_RESET, tmio->fcr + FCR_MODE);
	tmio_ioread8(tmio->fcr + FCR_DATA);	/* dummy read */
	tmio_iowrite8(FCR_MODE_HWECC_CALC, tmio->fcr + FCR_MODE);
}

static int tmio_nand_calculate_ecc(struct nand_chip *chip, const u_char *dat,
				   u_char *ecc_code)
{
	struct tmio_nand *tmio = mtd_to_tmio(nand_to_mtd(chip));
	unsigned int ecc;

	tmio_iowrite8(FCR_MODE_HWECC_RESULT, tmio->fcr + FCR_MODE);

	ecc = tmio_ioread16(tmio->fcr + FCR_DATA);
	ecc_code[1] = ecc;	/* 000-255 LP7-0 */
	ecc_code[0] = ecc >> 8;	/* 000-255 LP15-8 */
	ecc = tmio_ioread16(tmio->fcr + FCR_DATA);
	ecc_code[2] = ecc;	/* 000-255 CP5-0,11b */
	ecc_code[4] = ecc >> 8;	/* 256-511 LP7-0 */
	ecc = tmio_ioread16(tmio->fcr + FCR_DATA);
	ecc_code[3] = ecc;	/* 256-511 LP15-8 */
	ecc_code[5] = ecc >> 8;	/* 256-511 CP5-0,11b */

	tmio_iowrite8(FCR_MODE_DATA, tmio->fcr + FCR_MODE);
	return 0;
}

static int tmio_nand_correct_data(struct nand_chip *chip, unsigned char *buf,
				  unsigned char *read_ecc,
				  unsigned char *calc_ecc)
{
	int r0, r1;

	/* assume ecc.size = 512 and ecc.bytes = 6 */
	r0 = rawnand_sw_hamming_correct(chip, buf, read_ecc, calc_ecc);
	if (r0 < 0)
		return r0;
	r1 = rawnand_sw_hamming_correct(chip, buf + 256, read_ecc + 3,
					calc_ecc + 3);
	if (r1 < 0)
		return r1;
	return r0 + r1;
}

static int tmio_hw_init(struct platform_device *dev, struct tmio_nand *tmio)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	int ret;

	if (cell->enable) {
		ret = cell->enable(dev);
		if (ret)
			return ret;
	}

	/* (4Ch) CLKRUN Enable    1st spcrunc */
	tmio_iowrite8(0x81, tmio->ccr + CCR_ICC);

	/* (10h)BaseAddress    0x1000 spba.spba2 */
	tmio_iowrite16(tmio->fcr_base, tmio->ccr + CCR_BASE);
	tmio_iowrite16(tmio->fcr_base >> 16, tmio->ccr + CCR_BASE + 2);

	/* (04h)Command Register I/O spcmd */
	tmio_iowrite8(0x02, tmio->ccr + CCR_COMMAND);

	/* (62h) Power Supply Control ssmpwc */
	/* HardPowerOFF - SuspendOFF - PowerSupplyWait_4MS */
	tmio_iowrite8(0x02, tmio->ccr + CCR_NFPSC);

	/* (63h) Detect Control ssmdtc */
	tmio_iowrite8(0x02, tmio->ccr + CCR_NFDC);

	/* Interrupt status register clear sintst */
	tmio_iowrite8(0x0f, tmio->fcr + FCR_ISR);

	/* After power supply, Media are reset smode */
	tmio_iowrite8(FCR_MODE_POWER_ON, tmio->fcr + FCR_MODE);
	tmio_iowrite8(FCR_MODE_COMMAND, tmio->fcr + FCR_MODE);
	tmio_iowrite8(NAND_CMD_RESET, tmio->fcr + FCR_DATA);

	/* Standby Mode smode */
	tmio_iowrite8(FCR_MODE_STANDBY, tmio->fcr + FCR_MODE);

	mdelay(5);

	return 0;
}

static void tmio_hw_stop(struct platform_device *dev, struct tmio_nand *tmio)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);

	tmio_iowrite8(FCR_MODE_POWER_OFF, tmio->fcr + FCR_MODE);
	if (cell->disable)
		cell->disable(dev);
}

static int tmio_attach_chip(struct nand_chip *chip)
{
	if (chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_ON_HOST)
		return 0;

	chip->ecc.size = 512;
	chip->ecc.bytes = 6;
	chip->ecc.strength = 2;
	chip->ecc.hwctl = tmio_nand_enable_hwecc;
	chip->ecc.calculate = tmio_nand_calculate_ecc;
	chip->ecc.correct = tmio_nand_correct_data;

	return 0;
}

static const struct nand_controller_ops tmio_ops = {
	.attach_chip = tmio_attach_chip,
};

static int tmio_probe(struct platform_device *dev)
{
	struct tmio_nand_data *data = dev_get_platdata(&dev->dev);
	struct resource *fcr = platform_get_resource(dev,
			IORESOURCE_MEM, 0);
	struct resource *ccr = platform_get_resource(dev,
			IORESOURCE_MEM, 1);
	int irq = platform_get_irq(dev, 0);
	struct tmio_nand *tmio;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	int retval;

	if (data == NULL)
		dev_warn(&dev->dev, "NULL platform data!\n");

	if (!ccr || !fcr)
		return -EINVAL;

	tmio = devm_kzalloc(&dev->dev, sizeof(*tmio), GFP_KERNEL);
	if (!tmio)
		return -ENOMEM;

	init_completion(&tmio->comp);

	tmio->dev = dev;

	platform_set_drvdata(dev, tmio);
	nand_chip = &tmio->chip;
	mtd = nand_to_mtd(nand_chip);
	mtd->name = "tmio-nand";
	mtd->dev.parent = &dev->dev;

	nand_controller_init(&tmio->controller);
	tmio->controller.ops = &tmio_ops;
	nand_chip->controller = &tmio->controller;

	tmio->ccr = devm_ioremap(&dev->dev, ccr->start, resource_size(ccr));
	if (!tmio->ccr)
		return -EIO;

	tmio->fcr_base = fcr->start & 0xfffff;
	tmio->fcr = devm_ioremap(&dev->dev, fcr->start, resource_size(fcr));
	if (!tmio->fcr)
		return -EIO;

	retval = tmio_hw_init(dev, tmio);
	if (retval)
		return retval;

	/* Set address of NAND IO lines */
	nand_chip->legacy.IO_ADDR_R = tmio->fcr;
	nand_chip->legacy.IO_ADDR_W = tmio->fcr;

	/* Set address of hardware control function */
	nand_chip->legacy.cmd_ctrl = tmio_nand_hwcontrol;
	nand_chip->legacy.dev_ready = tmio_nand_dev_ready;
	nand_chip->legacy.read_byte = tmio_nand_read_byte;
	nand_chip->legacy.write_buf = tmio_nand_write_buf;
	nand_chip->legacy.read_buf = tmio_nand_read_buf;

	if (data)
		nand_chip->badblock_pattern = data->badblock_pattern;

	/* 15 us command delay time */
	nand_chip->legacy.chip_delay = 15;

	retval = devm_request_irq(&dev->dev, irq, &tmio_irq, 0,
				  dev_name(&dev->dev), tmio);
	if (retval) {
		dev_err(&dev->dev, "request_irq error %d\n", retval);
		goto err_irq;
	}

	tmio->irq = irq;
	nand_chip->legacy.waitfunc = tmio_nand_wait;

	/* Scan to find existence of the device */
	retval = nand_scan(nand_chip, 1);
	if (retval)
		goto err_irq;

	/* Register the partitions */
	retval = mtd_device_parse_register(mtd,
					   data ? data->part_parsers : NULL,
					   NULL,
					   data ? data->partition : NULL,
					   data ? data->num_partitions : 0);
	if (!retval)
		return retval;

	nand_cleanup(nand_chip);

err_irq:
	tmio_hw_stop(dev, tmio);
	return retval;
}

static int tmio_remove(struct platform_device *dev)
{
	struct tmio_nand *tmio = platform_get_drvdata(dev);
	struct nand_chip *chip = &tmio->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
	tmio_hw_stop(dev, tmio);
	return 0;
}

#ifdef CONFIG_PM
static int tmio_suspend(struct platform_device *dev, pm_message_t state)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);

	if (cell->suspend)
		cell->suspend(dev);

	tmio_hw_stop(dev, platform_get_drvdata(dev));
	return 0;
}

static int tmio_resume(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);

	/* FIXME - is this required or merely another attack of the broken
	 * SHARP platform? Looks suspicious.
	 */
	tmio_hw_init(dev, platform_get_drvdata(dev));

	if (cell->resume)
		cell->resume(dev);

	return 0;
}
#else
#define tmio_suspend NULL
#define tmio_resume NULL
#endif

static struct platform_driver tmio_driver = {
	.driver.name	= "tmio-nand",
	.driver.owner	= THIS_MODULE,
	.probe		= tmio_probe,
	.remove		= tmio_remove,
	.suspend	= tmio_suspend,
	.resume		= tmio_resume,
};

module_platform_driver(tmio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ian Molton, Dirk Opfer, Chris Humbert, Dmitry Baryshkov");
MODULE_DESCRIPTION("NAND flash driver on Toshiba Mobile IO controller");
MODULE_ALIAS("platform:tmio-nand");
