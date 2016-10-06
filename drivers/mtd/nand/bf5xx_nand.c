/* linux/drivers/mtd/nand/bf5xx_nand.c
 *
 * Copyright 2006-2008 Analog Devices Inc.
 *	http://blackfin.uclinux.org/
 *	Bryan Wu <bryan.wu@analog.com>
 *
 * Blackfin BF5xx on-chip NAND flash controller driver
 *
 * Derived from drivers/mtd/nand/s3c2410.c
 * Copyright (c) 2007 Ben Dooks <ben@simtec.co.uk>
 *
 * Derived from drivers/mtd/nand/cafe.c
 * Copyright © 2006 Red Hat, Inc.
 * Copyright © 2006 David Woodhouse <dwmw2@infradead.org>
 *
 * Changelog:
 *	12-Jun-2007  Bryan Wu:  Initial version
 *	18-Jul-2007  Bryan Wu:
 *		- ECC_HW and ECC_SW supported
 *		- DMA supported in ECC_HW
 *		- YAFFS tested as rootfs in both ECC_HW and ECC_SW
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/bitops.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/blackfin.h>
#include <asm/dma.h>
#include <asm/cacheflush.h>
#include <asm/nand.h>
#include <asm/portmux.h>

#define DRV_NAME	"bf5xx-nand"
#define DRV_VERSION	"1.2"
#define DRV_AUTHOR	"Bryan Wu <bryan.wu@analog.com>"
#define DRV_DESC	"BF5xx on-chip NAND FLash Controller Driver"

/* NFC_STAT Masks */
#define NBUSY       0x01  /* Not Busy */
#define WB_FULL     0x02  /* Write Buffer Full */
#define PG_WR_STAT  0x04  /* Page Write Pending */
#define PG_RD_STAT  0x08  /* Page Read Pending */
#define WB_EMPTY    0x10  /* Write Buffer Empty */

/* NFC_IRQSTAT Masks */
#define NBUSYIRQ    0x01  /* Not Busy IRQ */
#define WB_OVF      0x02  /* Write Buffer Overflow */
#define WB_EDGE     0x04  /* Write Buffer Edge Detect */
#define RD_RDY      0x08  /* Read Data Ready */
#define WR_DONE     0x10  /* Page Write Done */

/* NFC_RST Masks */
#define ECC_RST     0x01  /* ECC (and NFC counters) Reset */

/* NFC_PGCTL Masks */
#define PG_RD_START 0x01  /* Page Read Start */
#define PG_WR_START 0x02  /* Page Write Start */

#ifdef CONFIG_MTD_NAND_BF5XX_HWECC
static int hardware_ecc = 1;
#else
static int hardware_ecc;
#endif

static const unsigned short bfin_nfc_pin_req[] =
	{P_NAND_CE,
	 P_NAND_RB,
	 P_NAND_D0,
	 P_NAND_D1,
	 P_NAND_D2,
	 P_NAND_D3,
	 P_NAND_D4,
	 P_NAND_D5,
	 P_NAND_D6,
	 P_NAND_D7,
	 P_NAND_WE,
	 P_NAND_RE,
	 P_NAND_CLE,
	 P_NAND_ALE,
	 0};

#ifdef CONFIG_MTD_NAND_BF5XX_BOOTROM_ECC
static int bootrom_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	if (section > 7)
		return -ERANGE;

	oobregion->offset = section * 8;
	oobregion->length = 3;

	return 0;
}

static int bootrom_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	if (section > 7)
		return -ERANGE;

	oobregion->offset = (section * 8) + 3;
	oobregion->length = 5;

	return 0;
}

static const struct mtd_ooblayout_ops bootrom_ooblayout_ops = {
	.ecc = bootrom_ooblayout_ecc,
	.free = bootrom_ooblayout_free,
};
#endif

/*
 * Data structures for bf5xx nand flash controller driver
 */

/* bf5xx nand info */
struct bf5xx_nand_info {
	/* mtd info */
	struct nand_hw_control		controller;
	struct nand_chip		chip;

	/* platform info */
	struct bf5xx_nand_platform	*platform;

	/* device info */
	struct device			*device;

	/* DMA stuff */
	struct completion		dma_completion;
};

/*
 * Conversion functions
 */
static struct bf5xx_nand_info *mtd_to_nand_info(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct bf5xx_nand_info,
			    chip);
}

static struct bf5xx_nand_info *to_nand_info(struct platform_device *pdev)
{
	return platform_get_drvdata(pdev);
}

static struct bf5xx_nand_platform *to_nand_plat(struct platform_device *pdev)
{
	return dev_get_platdata(&pdev->dev);
}

/*
 * struct nand_chip interface function pointers
 */

/*
 * bf5xx_nand_hwcontrol
 *
 * Issue command and address cycles to the chip
 */
static void bf5xx_nand_hwcontrol(struct mtd_info *mtd, int cmd,
				   unsigned int ctrl)
{
	if (cmd == NAND_CMD_NONE)
		return;

	while (bfin_read_NFC_STAT() & WB_FULL)
		cpu_relax();

	if (ctrl & NAND_CLE)
		bfin_write_NFC_CMD(cmd);
	else if (ctrl & NAND_ALE)
		bfin_write_NFC_ADDR(cmd);
	SSYNC();
}

/*
 * bf5xx_nand_devready()
 *
 * returns 0 if the nand is busy, 1 if it is ready
 */
static int bf5xx_nand_devready(struct mtd_info *mtd)
{
	unsigned short val = bfin_read_NFC_STAT();

	if ((val & NBUSY) == NBUSY)
		return 1;
	else
		return 0;
}

/*
 * ECC functions
 * These allow the bf5xx to use the controller's ECC
 * generator block to ECC the data as it passes through
 */

/*
 * ECC error correction function
 */
static int bf5xx_nand_correct_data_256(struct mtd_info *mtd, u_char *dat,
					u_char *read_ecc, u_char *calc_ecc)
{
	struct bf5xx_nand_info *info = mtd_to_nand_info(mtd);
	u32 syndrome[5];
	u32 calced, stored;
	int i;
	unsigned short failing_bit, failing_byte;
	u_char data;

	calced = calc_ecc[0] | (calc_ecc[1] << 8) | (calc_ecc[2] << 16);
	stored = read_ecc[0] | (read_ecc[1] << 8) | (read_ecc[2] << 16);

	syndrome[0] = (calced ^ stored);

	/*
	 * syndrome 0: all zero
	 * No error in data
	 * No action
	 */
	if (!syndrome[0] || !calced || !stored)
		return 0;

	/*
	 * sysdrome 0: only one bit is one
	 * ECC data was incorrect
	 * No action
	 */
	if (hweight32(syndrome[0]) == 1) {
		dev_err(info->device, "ECC data was incorrect!\n");
		return -EBADMSG;
	}

	syndrome[1] = (calced & 0x7FF) ^ (stored & 0x7FF);
	syndrome[2] = (calced & 0x7FF) ^ ((calced >> 11) & 0x7FF);
	syndrome[3] = (stored & 0x7FF) ^ ((stored >> 11) & 0x7FF);
	syndrome[4] = syndrome[2] ^ syndrome[3];

	for (i = 0; i < 5; i++)
		dev_info(info->device, "syndrome[%d] 0x%08x\n", i, syndrome[i]);

	dev_info(info->device,
		"calced[0x%08x], stored[0x%08x]\n",
		calced, stored);

	/*
	 * sysdrome 0: exactly 11 bits are one, each parity
	 * and parity' pair is 1 & 0 or 0 & 1.
	 * 1-bit correctable error
	 * Correct the error
	 */
	if (hweight32(syndrome[0]) == 11 && syndrome[4] == 0x7FF) {
		dev_info(info->device,
			"1-bit correctable error, correct it.\n");
		dev_info(info->device,
			"syndrome[1] 0x%08x\n", syndrome[1]);

		failing_bit = syndrome[1] & 0x7;
		failing_byte = syndrome[1] >> 0x3;
		data = *(dat + failing_byte);
		data = data ^ (0x1 << failing_bit);
		*(dat + failing_byte) = data;

		return 1;
	}

	/*
	 * sysdrome 0: random data
	 * More than 1-bit error, non-correctable error
	 * Discard data, mark bad block
	 */
	dev_err(info->device,
		"More than 1-bit error, non-correctable error.\n");
	dev_err(info->device,
		"Please discard data, mark bad block\n");

	return -EBADMSG;
}

static int bf5xx_nand_correct_data(struct mtd_info *mtd, u_char *dat,
					u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	int ret, bitflips = 0;

	ret = bf5xx_nand_correct_data_256(mtd, dat, read_ecc, calc_ecc);
	if (ret < 0)
		return ret;

	bitflips = ret;

	/* If ecc size is 512, correct second 256 bytes */
	if (chip->ecc.size == 512) {
		dat += 256;
		read_ecc += 3;
		calc_ecc += 3;
		ret = bf5xx_nand_correct_data_256(mtd, dat, read_ecc, calc_ecc);
		if (ret < 0)
			return ret;

		bitflips += ret;
	}

	return bitflips;
}

static void bf5xx_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	return;
}

static int bf5xx_nand_calculate_ecc(struct mtd_info *mtd,
		const u_char *dat, u_char *ecc_code)
{
	struct bf5xx_nand_info *info = mtd_to_nand_info(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);
	u16 ecc0, ecc1;
	u32 code[2];
	u8 *p;

	/* first 3 bytes ECC code for 256 page size */
	ecc0 = bfin_read_NFC_ECC0();
	ecc1 = bfin_read_NFC_ECC1();

	code[0] = (ecc0 & 0x7ff) | ((ecc1 & 0x7ff) << 11);

	dev_dbg(info->device, "returning ecc 0x%08x\n", code[0]);

	p = (u8 *) code;
	memcpy(ecc_code, p, 3);

	/* second 3 bytes ECC code for 512 ecc size */
	if (chip->ecc.size == 512) {
		ecc0 = bfin_read_NFC_ECC2();
		ecc1 = bfin_read_NFC_ECC3();
		code[1] = (ecc0 & 0x7ff) | ((ecc1 & 0x7ff) << 11);

		/* second 3 bytes in ecc_code for second 256
		 * bytes of 512 page size
		 */
		p = (u8 *) (code + 1);
		memcpy((ecc_code + 3), p, 3);
		dev_dbg(info->device, "returning ecc 0x%08x\n", code[1]);
	}

	return 0;
}

/*
 * PIO mode for buffer writing and reading
 */
static void bf5xx_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;
	unsigned short val;

	/*
	 * Data reads are requested by first writing to NFC_DATA_RD
	 * and then reading back from NFC_READ.
	 */
	for (i = 0; i < len; i++) {
		while (bfin_read_NFC_STAT() & WB_FULL)
			cpu_relax();

		/* Contents do not matter */
		bfin_write_NFC_DATA_RD(0x0000);
		SSYNC();

		while ((bfin_read_NFC_IRQSTAT() & RD_RDY) != RD_RDY)
			cpu_relax();

		buf[i] = bfin_read_NFC_READ();

		val = bfin_read_NFC_IRQSTAT();
		val |= RD_RDY;
		bfin_write_NFC_IRQSTAT(val);
		SSYNC();
	}
}

static uint8_t bf5xx_nand_read_byte(struct mtd_info *mtd)
{
	uint8_t val;

	bf5xx_nand_read_buf(mtd, &val, 1);

	return val;
}

static void bf5xx_nand_write_buf(struct mtd_info *mtd,
				const uint8_t *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		while (bfin_read_NFC_STAT() & WB_FULL)
			cpu_relax();

		bfin_write_NFC_DATA_WR(buf[i]);
		SSYNC();
	}
}

static void bf5xx_nand_read_buf16(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;
	u16 *p = (u16 *) buf;
	len >>= 1;

	/*
	 * Data reads are requested by first writing to NFC_DATA_RD
	 * and then reading back from NFC_READ.
	 */
	bfin_write_NFC_DATA_RD(0x5555);

	SSYNC();

	for (i = 0; i < len; i++)
		p[i] = bfin_read_NFC_READ();
}

static void bf5xx_nand_write_buf16(struct mtd_info *mtd,
				const uint8_t *buf, int len)
{
	int i;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i = 0; i < len; i++)
		bfin_write_NFC_DATA_WR(p[i]);

	SSYNC();
}

/*
 * DMA functions for buffer writing and reading
 */
static irqreturn_t bf5xx_nand_dma_irq(int irq, void *dev_id)
{
	struct bf5xx_nand_info *info = dev_id;

	clear_dma_irqstat(CH_NFC);
	disable_dma(CH_NFC);
	complete(&info->dma_completion);

	return IRQ_HANDLED;
}

static void bf5xx_nand_dma_rw(struct mtd_info *mtd,
				uint8_t *buf, int is_read)
{
	struct bf5xx_nand_info *info = mtd_to_nand_info(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);
	unsigned short val;

	dev_dbg(info->device, " mtd->%p, buf->%p, is_read %d\n",
			mtd, buf, is_read);

	/*
	 * Before starting a dma transfer, be sure to invalidate/flush
	 * the cache over the address range of your DMA buffer to
	 * prevent cache coherency problems. Otherwise very subtle bugs
	 * can be introduced to your driver.
	 */
	if (is_read)
		invalidate_dcache_range((unsigned int)buf,
				(unsigned int)(buf + chip->ecc.size));
	else
		flush_dcache_range((unsigned int)buf,
				(unsigned int)(buf + chip->ecc.size));

	/*
	 * This register must be written before each page is
	 * transferred to generate the correct ECC register
	 * values.
	 */
	bfin_write_NFC_RST(ECC_RST);
	SSYNC();
	while (bfin_read_NFC_RST() & ECC_RST)
		cpu_relax();

	disable_dma(CH_NFC);
	clear_dma_irqstat(CH_NFC);

	/* setup DMA register with Blackfin DMA API */
	set_dma_config(CH_NFC, 0x0);
	set_dma_start_addr(CH_NFC, (unsigned long) buf);

	/* The DMAs have different size on BF52x and BF54x */
#ifdef CONFIG_BF52x
	set_dma_x_count(CH_NFC, (chip->ecc.size >> 1));
	set_dma_x_modify(CH_NFC, 2);
	val = DI_EN | WDSIZE_16;
#endif

#ifdef CONFIG_BF54x
	set_dma_x_count(CH_NFC, (chip->ecc.size >> 2));
	set_dma_x_modify(CH_NFC, 4);
	val = DI_EN | WDSIZE_32;
#endif
	/* setup write or read operation */
	if (is_read)
		val |= WNR;
	set_dma_config(CH_NFC, val);
	enable_dma(CH_NFC);

	/* Start PAGE read/write operation */
	if (is_read)
		bfin_write_NFC_PGCTL(PG_RD_START);
	else
		bfin_write_NFC_PGCTL(PG_WR_START);
	wait_for_completion(&info->dma_completion);
}

static void bf5xx_nand_dma_read_buf(struct mtd_info *mtd,
					uint8_t *buf, int len)
{
	struct bf5xx_nand_info *info = mtd_to_nand_info(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	dev_dbg(info->device, "mtd->%p, buf->%p, int %d\n", mtd, buf, len);

	if (len == chip->ecc.size)
		bf5xx_nand_dma_rw(mtd, buf, 1);
	else
		bf5xx_nand_read_buf(mtd, buf, len);
}

static void bf5xx_nand_dma_write_buf(struct mtd_info *mtd,
				const uint8_t *buf, int len)
{
	struct bf5xx_nand_info *info = mtd_to_nand_info(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	dev_dbg(info->device, "mtd->%p, buf->%p, len %d\n", mtd, buf, len);

	if (len == chip->ecc.size)
		bf5xx_nand_dma_rw(mtd, (uint8_t *)buf, 0);
	else
		bf5xx_nand_write_buf(mtd, buf, len);
}

static int bf5xx_nand_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
		uint8_t *buf, int oob_required, int page)
{
	bf5xx_nand_read_buf(mtd, buf, mtd->writesize);
	bf5xx_nand_read_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

static int bf5xx_nand_write_page_raw(struct mtd_info *mtd,
		struct nand_chip *chip,	const uint8_t *buf, int oob_required,
		int page)
{
	bf5xx_nand_write_buf(mtd, buf, mtd->writesize);
	bf5xx_nand_write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

/*
 * System initialization functions
 */
static int bf5xx_nand_dma_init(struct bf5xx_nand_info *info)
{
	int ret;

	/* Do not use dma */
	if (!hardware_ecc)
		return 0;

	init_completion(&info->dma_completion);

	/* Request NFC DMA channel */
	ret = request_dma(CH_NFC, "BF5XX NFC driver");
	if (ret < 0) {
		dev_err(info->device, " unable to get DMA channel\n");
		return ret;
	}

#ifdef CONFIG_BF54x
	/* Setup DMAC1 channel mux for NFC which shared with SDH */
	bfin_write_DMAC1_PERIMUX(bfin_read_DMAC1_PERIMUX() & ~1);
	SSYNC();
#endif

	set_dma_callback(CH_NFC, bf5xx_nand_dma_irq, info);

	/* Turn off the DMA channel first */
	disable_dma(CH_NFC);
	return 0;
}

static void bf5xx_nand_dma_remove(struct bf5xx_nand_info *info)
{
	/* Free NFC DMA channel */
	if (hardware_ecc)
		free_dma(CH_NFC);
}

/*
 * BF5XX NFC hardware initialization
 *  - pin mux setup
 *  - clear interrupt status
 */
static int bf5xx_nand_hw_init(struct bf5xx_nand_info *info)
{
	int err = 0;
	unsigned short val;
	struct bf5xx_nand_platform *plat = info->platform;

	/* setup NFC_CTL register */
	dev_info(info->device,
		"data_width=%d, wr_dly=%d, rd_dly=%d\n",
		(plat->data_width ? 16 : 8),
		plat->wr_dly, plat->rd_dly);

	val = (1 << NFC_PG_SIZE_OFFSET) |
		(plat->data_width << NFC_NWIDTH_OFFSET) |
		(plat->rd_dly << NFC_RDDLY_OFFSET) |
		(plat->wr_dly << NFC_WRDLY_OFFSET);
	dev_dbg(info->device, "NFC_CTL is 0x%04x\n", val);

	bfin_write_NFC_CTL(val);
	SSYNC();

	/* clear interrupt status */
	bfin_write_NFC_IRQMASK(0x0);
	SSYNC();
	val = bfin_read_NFC_IRQSTAT();
	bfin_write_NFC_IRQSTAT(val);
	SSYNC();

	/* DMA initialization  */
	if (bf5xx_nand_dma_init(info))
		err = -ENXIO;

	return err;
}

/*
 * Device management interface
 */
static int bf5xx_nand_add_partition(struct bf5xx_nand_info *info)
{
	struct mtd_info *mtd = nand_to_mtd(&info->chip);
	struct mtd_partition *parts = info->platform->partitions;
	int nr = info->platform->nr_partitions;

	return mtd_device_register(mtd, parts, nr);
}

static int bf5xx_nand_remove(struct platform_device *pdev)
{
	struct bf5xx_nand_info *info = to_nand_info(pdev);

	/* first thing we need to do is release all our mtds
	 * and their partitions, then go through freeing the
	 * resources used
	 */
	nand_release(nand_to_mtd(&info->chip));

	peripheral_free_list(bfin_nfc_pin_req);
	bf5xx_nand_dma_remove(info);

	return 0;
}

static int bf5xx_nand_scan(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	int ret;

	ret = nand_scan_ident(mtd, 1, NULL);
	if (ret)
		return ret;

	if (hardware_ecc) {
		/*
		 * for nand with page size > 512B, think it as several sections with 512B
		 */
		if (likely(mtd->writesize >= 512)) {
			chip->ecc.size = 512;
			chip->ecc.bytes = 6;
			chip->ecc.strength = 2;
		} else {
			chip->ecc.size = 256;
			chip->ecc.bytes = 3;
			chip->ecc.strength = 1;
			bfin_write_NFC_CTL(bfin_read_NFC_CTL() & ~(1 << NFC_PG_SIZE_OFFSET));
			SSYNC();
		}
	}

	return	nand_scan_tail(mtd);
}

/*
 * bf5xx_nand_probe
 *
 * called by device layer when it finds a device matching
 * one our driver can handled. This code checks to see if
 * it can allocate all necessary resources then calls the
 * nand layer to look for devices
 */
static int bf5xx_nand_probe(struct platform_device *pdev)
{
	struct bf5xx_nand_platform *plat = to_nand_plat(pdev);
	struct bf5xx_nand_info *info = NULL;
	struct nand_chip *chip = NULL;
	struct mtd_info *mtd = NULL;
	int err = 0;

	dev_dbg(&pdev->dev, "(%p)\n", pdev);

	if (!plat) {
		dev_err(&pdev->dev, "no platform specific information\n");
		return -EINVAL;
	}

	if (peripheral_request_list(bfin_nfc_pin_req, DRV_NAME)) {
		dev_err(&pdev->dev, "requesting Peripherals failed\n");
		return -EFAULT;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		err = -ENOMEM;
		goto out_err;
	}

	platform_set_drvdata(pdev, info);

	spin_lock_init(&info->controller.lock);
	init_waitqueue_head(&info->controller.wq);

	info->device     = &pdev->dev;
	info->platform   = plat;

	/* initialise chip data struct */
	chip = &info->chip;
	mtd = nand_to_mtd(&info->chip);

	if (plat->data_width)
		chip->options |= NAND_BUSWIDTH_16;

	chip->options |= NAND_CACHEPRG | NAND_SKIP_BBTSCAN;

	chip->read_buf = (plat->data_width) ?
		bf5xx_nand_read_buf16 : bf5xx_nand_read_buf;
	chip->write_buf = (plat->data_width) ?
		bf5xx_nand_write_buf16 : bf5xx_nand_write_buf;

	chip->read_byte    = bf5xx_nand_read_byte;

	chip->cmd_ctrl     = bf5xx_nand_hwcontrol;
	chip->dev_ready    = bf5xx_nand_devready;

	nand_set_controller_data(chip, mtd);
	chip->controller   = &info->controller;

	chip->IO_ADDR_R    = (void __iomem *) NFC_READ;
	chip->IO_ADDR_W    = (void __iomem *) NFC_DATA_WR;

	chip->chip_delay   = 0;

	/* initialise mtd info data struct */
	mtd->dev.parent = &pdev->dev;

	/* initialise the hardware */
	err = bf5xx_nand_hw_init(info);
	if (err)
		goto out_err;

	/* setup hardware ECC data struct */
	if (hardware_ecc) {
#ifdef CONFIG_MTD_NAND_BF5XX_BOOTROM_ECC
		mtd_set_ooblayout(mtd, &bootrom_ooblayout_ops);
#endif
		chip->read_buf      = bf5xx_nand_dma_read_buf;
		chip->write_buf     = bf5xx_nand_dma_write_buf;
		chip->ecc.calculate = bf5xx_nand_calculate_ecc;
		chip->ecc.correct   = bf5xx_nand_correct_data;
		chip->ecc.mode	    = NAND_ECC_HW;
		chip->ecc.hwctl	    = bf5xx_nand_enable_hwecc;
		chip->ecc.read_page_raw = bf5xx_nand_read_page_raw;
		chip->ecc.write_page_raw = bf5xx_nand_write_page_raw;
	} else {
		chip->ecc.mode	    = NAND_ECC_SOFT;
		chip->ecc.algo	= NAND_ECC_HAMMING;
	}

	/* scan hardware nand chip and setup mtd info data struct */
	if (bf5xx_nand_scan(mtd)) {
		err = -ENXIO;
		goto out_err_nand_scan;
	}

#ifdef CONFIG_MTD_NAND_BF5XX_BOOTROM_ECC
	chip->badblockpos = 63;
#endif

	/* add NAND partition */
	bf5xx_nand_add_partition(info);

	dev_dbg(&pdev->dev, "initialised ok\n");
	return 0;

out_err_nand_scan:
	bf5xx_nand_dma_remove(info);
out_err:
	peripheral_free_list(bfin_nfc_pin_req);

	return err;
}

/* driver device registration */
static struct platform_driver bf5xx_nand_driver = {
	.probe		= bf5xx_nand_probe,
	.remove		= bf5xx_nand_remove,
	.driver		= {
		.name	= DRV_NAME,
	},
};

module_platform_driver(bf5xx_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_ALIAS("platform:" DRV_NAME);
