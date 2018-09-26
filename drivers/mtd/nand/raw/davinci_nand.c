/*
 * davinci_nand.c - NAND Flash Driver for DaVinci family chips
 *
 * Copyright © 2006 Texas Instruments.
 *
 * Port to 2.6.23 Copyright © 2008 by:
 *   Sander Huijsen <Shuijsen@optelecom-nkf.com>
 *   Troy Kisky <troy.kisky@boundarydevices.com>
 *   Dirk Behme <Dirk.Behme@gmail.com>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/of.h>

#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/mtd-davinci-aemif.h>

/*
 * This is a device driver for the NAND flash controller found on the
 * various DaVinci family chips.  It handles up to four SoC chipselects,
 * and some flavors of secondary chipselect (e.g. based on A12) as used
 * with multichip packages.
 *
 * The 1-bit ECC hardware is supported, as well as the newer 4-bit ECC
 * available on chips like the DM355 and OMAP-L137 and needed with the
 * more error-prone MLC NAND chips.
 *
 * This driver assumes EM_WAIT connects all the NAND devices' RDY/nBUSY
 * outputs in a "wire-AND" configuration, with no per-chip signals.
 */
struct davinci_nand_info {
	struct nand_chip	chip;

	struct platform_device	*pdev;

	bool			is_readmode;

	void __iomem		*base;
	void __iomem		*vaddr;

	void __iomem		*current_cs;

	uint32_t		mask_chipsel;
	uint32_t		mask_ale;
	uint32_t		mask_cle;

	uint32_t		core_chipsel;

	struct davinci_aemif_timing	*timing;
};

static DEFINE_SPINLOCK(davinci_nand_lock);
static bool ecc4_busy;

static inline struct davinci_nand_info *to_davinci_nand(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct davinci_nand_info, chip);
}

static inline unsigned int davinci_nand_readl(struct davinci_nand_info *info,
		int offset)
{
	return __raw_readl(info->base + offset);
}

static inline void davinci_nand_writel(struct davinci_nand_info *info,
		int offset, unsigned long value)
{
	__raw_writel(value, info->base + offset);
}

/*----------------------------------------------------------------------*/

/*
 * Access to hardware control lines:  ALE, CLE, secondary chipselect.
 */

static void nand_davinci_hwcontrol(struct mtd_info *mtd, int cmd,
				   unsigned int ctrl)
{
	struct davinci_nand_info	*info = to_davinci_nand(mtd);
	void __iomem			*addr = info->current_cs;
	struct nand_chip		*nand = mtd_to_nand(mtd);

	/* Did the control lines change? */
	if (ctrl & NAND_CTRL_CHANGE) {
		if ((ctrl & NAND_CTRL_CLE) == NAND_CTRL_CLE)
			addr += info->mask_cle;
		else if ((ctrl & NAND_CTRL_ALE) == NAND_CTRL_ALE)
			addr += info->mask_ale;

		nand->IO_ADDR_W = addr;
	}

	if (cmd != NAND_CMD_NONE)
		iowrite8(cmd, nand->IO_ADDR_W);
}

static void nand_davinci_select_chip(struct mtd_info *mtd, int chip)
{
	struct davinci_nand_info	*info = to_davinci_nand(mtd);

	info->current_cs = info->vaddr;

	/* maybe kick in a second chipselect */
	if (chip > 0)
		info->current_cs += info->mask_chipsel;

	info->chip.IO_ADDR_W = info->current_cs;
	info->chip.IO_ADDR_R = info->chip.IO_ADDR_W;
}

/*----------------------------------------------------------------------*/

/*
 * 1-bit hardware ECC ... context maintained for each core chipselect
 */

static inline uint32_t nand_davinci_readecc_1bit(struct mtd_info *mtd)
{
	struct davinci_nand_info *info = to_davinci_nand(mtd);

	return davinci_nand_readl(info, NANDF1ECC_OFFSET
			+ 4 * info->core_chipsel);
}

static void nand_davinci_hwctl_1bit(struct mtd_info *mtd, int mode)
{
	struct davinci_nand_info *info;
	uint32_t nandcfr;
	unsigned long flags;

	info = to_davinci_nand(mtd);

	/* Reset ECC hardware */
	nand_davinci_readecc_1bit(mtd);

	spin_lock_irqsave(&davinci_nand_lock, flags);

	/* Restart ECC hardware */
	nandcfr = davinci_nand_readl(info, NANDFCR_OFFSET);
	nandcfr |= BIT(8 + info->core_chipsel);
	davinci_nand_writel(info, NANDFCR_OFFSET, nandcfr);

	spin_unlock_irqrestore(&davinci_nand_lock, flags);
}

/*
 * Read hardware ECC value and pack into three bytes
 */
static int nand_davinci_calculate_1bit(struct mtd_info *mtd,
				      const u_char *dat, u_char *ecc_code)
{
	unsigned int ecc_val = nand_davinci_readecc_1bit(mtd);
	unsigned int ecc24 = (ecc_val & 0x0fff) | ((ecc_val & 0x0fff0000) >> 4);

	/* invert so that erased block ecc is correct */
	ecc24 = ~ecc24;
	ecc_code[0] = (u_char)(ecc24);
	ecc_code[1] = (u_char)(ecc24 >> 8);
	ecc_code[2] = (u_char)(ecc24 >> 16);

	return 0;
}

static int nand_davinci_correct_1bit(struct mtd_info *mtd, u_char *dat,
				     u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	uint32_t eccNand = read_ecc[0] | (read_ecc[1] << 8) |
					  (read_ecc[2] << 16);
	uint32_t eccCalc = calc_ecc[0] | (calc_ecc[1] << 8) |
					  (calc_ecc[2] << 16);
	uint32_t diff = eccCalc ^ eccNand;

	if (diff) {
		if ((((diff >> 12) ^ diff) & 0xfff) == 0xfff) {
			/* Correctable error */
			if ((diff >> (12 + 3)) < chip->ecc.size) {
				dat[diff >> (12 + 3)] ^= BIT((diff >> 12) & 7);
				return 1;
			} else {
				return -EBADMSG;
			}
		} else if (!(diff & (diff - 1))) {
			/* Single bit ECC error in the ECC itself,
			 * nothing to fix */
			return 1;
		} else {
			/* Uncorrectable error */
			return -EBADMSG;
		}

	}
	return 0;
}

/*----------------------------------------------------------------------*/

/*
 * 4-bit hardware ECC ... context maintained over entire AEMIF
 *
 * This is a syndrome engine, but we avoid NAND_ECC_HW_SYNDROME
 * since that forces use of a problematic "infix OOB" layout.
 * Among other things, it trashes manufacturer bad block markers.
 * Also, and specific to this hardware, it ECC-protects the "prepad"
 * in the OOB ... while having ECC protection for parts of OOB would
 * seem useful, the current MTD stack sometimes wants to update the
 * OOB without recomputing ECC.
 */

static void nand_davinci_hwctl_4bit(struct mtd_info *mtd, int mode)
{
	struct davinci_nand_info *info = to_davinci_nand(mtd);
	unsigned long flags;
	u32 val;

	/* Reset ECC hardware */
	davinci_nand_readl(info, NAND_4BIT_ECC1_OFFSET);

	spin_lock_irqsave(&davinci_nand_lock, flags);

	/* Start 4-bit ECC calculation for read/write */
	val = davinci_nand_readl(info, NANDFCR_OFFSET);
	val &= ~(0x03 << 4);
	val |= (info->core_chipsel << 4) | BIT(12);
	davinci_nand_writel(info, NANDFCR_OFFSET, val);

	info->is_readmode = (mode == NAND_ECC_READ);

	spin_unlock_irqrestore(&davinci_nand_lock, flags);
}

/* Read raw ECC code after writing to NAND. */
static void
nand_davinci_readecc_4bit(struct davinci_nand_info *info, u32 code[4])
{
	const u32 mask = 0x03ff03ff;

	code[0] = davinci_nand_readl(info, NAND_4BIT_ECC1_OFFSET) & mask;
	code[1] = davinci_nand_readl(info, NAND_4BIT_ECC2_OFFSET) & mask;
	code[2] = davinci_nand_readl(info, NAND_4BIT_ECC3_OFFSET) & mask;
	code[3] = davinci_nand_readl(info, NAND_4BIT_ECC4_OFFSET) & mask;
}

/* Terminate read ECC; or return ECC (as bytes) of data written to NAND. */
static int nand_davinci_calculate_4bit(struct mtd_info *mtd,
		const u_char *dat, u_char *ecc_code)
{
	struct davinci_nand_info *info = to_davinci_nand(mtd);
	u32 raw_ecc[4], *p;
	unsigned i;

	/* After a read, terminate ECC calculation by a dummy read
	 * of some 4-bit ECC register.  ECC covers everything that
	 * was read; correct() just uses the hardware state, so
	 * ecc_code is not needed.
	 */
	if (info->is_readmode) {
		davinci_nand_readl(info, NAND_4BIT_ECC1_OFFSET);
		return 0;
	}

	/* Pack eight raw 10-bit ecc values into ten bytes, making
	 * two passes which each convert four values (in upper and
	 * lower halves of two 32-bit words) into five bytes.  The
	 * ROM boot loader uses this same packing scheme.
	 */
	nand_davinci_readecc_4bit(info, raw_ecc);
	for (i = 0, p = raw_ecc; i < 2; i++, p += 2) {
		*ecc_code++ =   p[0]        & 0xff;
		*ecc_code++ = ((p[0] >>  8) & 0x03) | ((p[0] >> 14) & 0xfc);
		*ecc_code++ = ((p[0] >> 22) & 0x0f) | ((p[1] <<  4) & 0xf0);
		*ecc_code++ = ((p[1] >>  4) & 0x3f) | ((p[1] >> 10) & 0xc0);
		*ecc_code++ =  (p[1] >> 18) & 0xff;
	}

	return 0;
}

/* Correct up to 4 bits in data we just read, using state left in the
 * hardware plus the ecc_code computed when it was first written.
 */
static int nand_davinci_correct_4bit(struct mtd_info *mtd,
		u_char *data, u_char *ecc_code, u_char *null)
{
	int i;
	struct davinci_nand_info *info = to_davinci_nand(mtd);
	unsigned short ecc10[8];
	unsigned short *ecc16;
	u32 syndrome[4];
	u32 ecc_state;
	unsigned num_errors, corrected;
	unsigned long timeo;

	/* Unpack ten bytes into eight 10 bit values.  We know we're
	 * little-endian, and use type punning for less shifting/masking.
	 */
	if (WARN_ON(0x01 & (uintptr_t)ecc_code))
		return -EINVAL;
	ecc16 = (unsigned short *)ecc_code;

	ecc10[0] =  (ecc16[0] >>  0) & 0x3ff;
	ecc10[1] = ((ecc16[0] >> 10) & 0x3f) | ((ecc16[1] << 6) & 0x3c0);
	ecc10[2] =  (ecc16[1] >>  4) & 0x3ff;
	ecc10[3] = ((ecc16[1] >> 14) & 0x3)  | ((ecc16[2] << 2) & 0x3fc);
	ecc10[4] =  (ecc16[2] >>  8)         | ((ecc16[3] << 8) & 0x300);
	ecc10[5] =  (ecc16[3] >>  2) & 0x3ff;
	ecc10[6] = ((ecc16[3] >> 12) & 0xf)  | ((ecc16[4] << 4) & 0x3f0);
	ecc10[7] =  (ecc16[4] >>  6) & 0x3ff;

	/* Tell ECC controller about the expected ECC codes. */
	for (i = 7; i >= 0; i--)
		davinci_nand_writel(info, NAND_4BIT_ECC_LOAD_OFFSET, ecc10[i]);

	/* Allow time for syndrome calculation ... then read it.
	 * A syndrome of all zeroes 0 means no detected errors.
	 */
	davinci_nand_readl(info, NANDFSR_OFFSET);
	nand_davinci_readecc_4bit(info, syndrome);
	if (!(syndrome[0] | syndrome[1] | syndrome[2] | syndrome[3]))
		return 0;

	/*
	 * Clear any previous address calculation by doing a dummy read of an
	 * error address register.
	 */
	davinci_nand_readl(info, NAND_ERR_ADD1_OFFSET);

	/* Start address calculation, and wait for it to complete.
	 * We _could_ start reading more data while this is working,
	 * to speed up the overall page read.
	 */
	davinci_nand_writel(info, NANDFCR_OFFSET,
			davinci_nand_readl(info, NANDFCR_OFFSET) | BIT(13));

	/*
	 * ECC_STATE field reads 0x3 (Error correction complete) immediately
	 * after setting the 4BITECC_ADD_CALC_START bit. So if you immediately
	 * begin trying to poll for the state, you may fall right out of your
	 * loop without any of the correction calculations having taken place.
	 * The recommendation from the hardware team is to initially delay as
	 * long as ECC_STATE reads less than 4. After that, ECC HW has entered
	 * correction state.
	 */
	timeo = jiffies + usecs_to_jiffies(100);
	do {
		ecc_state = (davinci_nand_readl(info,
				NANDFSR_OFFSET) >> 8) & 0x0f;
		cpu_relax();
	} while ((ecc_state < 4) && time_before(jiffies, timeo));

	for (;;) {
		u32	fsr = davinci_nand_readl(info, NANDFSR_OFFSET);

		switch ((fsr >> 8) & 0x0f) {
		case 0:		/* no error, should not happen */
			davinci_nand_readl(info, NAND_ERR_ERRVAL1_OFFSET);
			return 0;
		case 1:		/* five or more errors detected */
			davinci_nand_readl(info, NAND_ERR_ERRVAL1_OFFSET);
			return -EBADMSG;
		case 2:		/* error addresses computed */
		case 3:
			num_errors = 1 + ((fsr >> 16) & 0x03);
			goto correct;
		default:	/* still working on it */
			cpu_relax();
			continue;
		}
	}

correct:
	/* correct each error */
	for (i = 0, corrected = 0; i < num_errors; i++) {
		int error_address, error_value;

		if (i > 1) {
			error_address = davinci_nand_readl(info,
						NAND_ERR_ADD2_OFFSET);
			error_value = davinci_nand_readl(info,
						NAND_ERR_ERRVAL2_OFFSET);
		} else {
			error_address = davinci_nand_readl(info,
						NAND_ERR_ADD1_OFFSET);
			error_value = davinci_nand_readl(info,
						NAND_ERR_ERRVAL1_OFFSET);
		}

		if (i & 1) {
			error_address >>= 16;
			error_value >>= 16;
		}
		error_address &= 0x3ff;
		error_address = (512 + 7) - error_address;

		if (error_address < 512) {
			data[error_address] ^= error_value;
			corrected++;
		}
	}

	return corrected;
}

/*----------------------------------------------------------------------*/

/*
 * NOTE:  NAND boot requires ALE == EM_A[1], CLE == EM_A[2], so that's
 * how these chips are normally wired.  This translates to both 8 and 16
 * bit busses using ALE == BIT(3) in byte addresses, and CLE == BIT(4).
 *
 * For now we assume that configuration, or any other one which ignores
 * the two LSBs for NAND access ... so we can issue 32-bit reads/writes
 * and have that transparently morphed into multiple NAND operations.
 */
static void nand_davinci_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if ((0x03 & ((uintptr_t)buf)) == 0 && (0x03 & len) == 0)
		ioread32_rep(chip->IO_ADDR_R, buf, len >> 2);
	else if ((0x01 & ((uintptr_t)buf)) == 0 && (0x01 & len) == 0)
		ioread16_rep(chip->IO_ADDR_R, buf, len >> 1);
	else
		ioread8_rep(chip->IO_ADDR_R, buf, len);
}

static void nand_davinci_write_buf(struct mtd_info *mtd,
		const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if ((0x03 & ((uintptr_t)buf)) == 0 && (0x03 & len) == 0)
		iowrite32_rep(chip->IO_ADDR_R, buf, len >> 2);
	else if ((0x01 & ((uintptr_t)buf)) == 0 && (0x01 & len) == 0)
		iowrite16_rep(chip->IO_ADDR_R, buf, len >> 1);
	else
		iowrite8_rep(chip->IO_ADDR_R, buf, len);
}

/*
 * Check hardware register for wait status. Returns 1 if device is ready,
 * 0 if it is still busy.
 */
static int nand_davinci_dev_ready(struct mtd_info *mtd)
{
	struct davinci_nand_info *info = to_davinci_nand(mtd);

	return davinci_nand_readl(info, NANDFSR_OFFSET) & BIT(0);
}

/*----------------------------------------------------------------------*/

/* An ECC layout for using 4-bit ECC with small-page flash, storing
 * ten ECC bytes plus the manufacturer's bad block marker byte, and
 * and not overlapping the default BBT markers.
 */
static int hwecc4_ooblayout_small_ecc(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *oobregion)
{
	if (section > 2)
		return -ERANGE;

	if (!section) {
		oobregion->offset = 0;
		oobregion->length = 5;
	} else if (section == 1) {
		oobregion->offset = 6;
		oobregion->length = 2;
	} else {
		oobregion->offset = 13;
		oobregion->length = 3;
	}

	return 0;
}

static int hwecc4_ooblayout_small_free(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *oobregion)
{
	if (section > 1)
		return -ERANGE;

	if (!section) {
		oobregion->offset = 8;
		oobregion->length = 5;
	} else {
		oobregion->offset = 16;
		oobregion->length = mtd->oobsize - 16;
	}

	return 0;
}

static const struct mtd_ooblayout_ops hwecc4_small_ooblayout_ops = {
	.ecc = hwecc4_ooblayout_small_ecc,
	.free = hwecc4_ooblayout_small_free,
};

#if defined(CONFIG_OF)
static const struct of_device_id davinci_nand_of_match[] = {
	{.compatible = "ti,davinci-nand", },
	{.compatible = "ti,keystone-nand", },
	{},
};
MODULE_DEVICE_TABLE(of, davinci_nand_of_match);

static struct davinci_nand_pdata
	*nand_davinci_get_pdata(struct platform_device *pdev)
{
	if (!dev_get_platdata(&pdev->dev) && pdev->dev.of_node) {
		struct davinci_nand_pdata *pdata;
		const char *mode;
		u32 prop;

		pdata =  devm_kzalloc(&pdev->dev,
				sizeof(struct davinci_nand_pdata),
				GFP_KERNEL);
		pdev->dev.platform_data = pdata;
		if (!pdata)
			return ERR_PTR(-ENOMEM);
		if (!of_property_read_u32(pdev->dev.of_node,
			"ti,davinci-chipselect", &prop))
			pdata->core_chipsel = prop;
		else
			return ERR_PTR(-EINVAL);

		if (!of_property_read_u32(pdev->dev.of_node,
			"ti,davinci-mask-ale", &prop))
			pdata->mask_ale = prop;
		if (!of_property_read_u32(pdev->dev.of_node,
			"ti,davinci-mask-cle", &prop))
			pdata->mask_cle = prop;
		if (!of_property_read_u32(pdev->dev.of_node,
			"ti,davinci-mask-chipsel", &prop))
			pdata->mask_chipsel = prop;
		if (!of_property_read_string(pdev->dev.of_node,
			"ti,davinci-ecc-mode", &mode)) {
			if (!strncmp("none", mode, 4))
				pdata->ecc_mode = NAND_ECC_NONE;
			if (!strncmp("soft", mode, 4))
				pdata->ecc_mode = NAND_ECC_SOFT;
			if (!strncmp("hw", mode, 2))
				pdata->ecc_mode = NAND_ECC_HW;
		}
		if (!of_property_read_u32(pdev->dev.of_node,
			"ti,davinci-ecc-bits", &prop))
			pdata->ecc_bits = prop;

		if (!of_property_read_u32(pdev->dev.of_node,
			"ti,davinci-nand-buswidth", &prop) && prop == 16)
			pdata->options |= NAND_BUSWIDTH_16;

		if (of_property_read_bool(pdev->dev.of_node,
			"ti,davinci-nand-use-bbt"))
			pdata->bbt_options = NAND_BBT_USE_FLASH;

		/*
		 * Since kernel v4.8, this driver has been fixed to enable
		 * use of 4-bit hardware ECC with subpages and verified on
		 * TI's keystone EVMs (K2L, K2HK and K2E).
		 * However, in the interest of not breaking systems using
		 * existing UBI partitions, sub-page writes are not being
		 * (re)enabled. If you want to use subpage writes on Keystone
		 * platforms (i.e. do not have any existing UBI partitions),
		 * then use "ti,davinci-nand" as the compatible in your
		 * device-tree file.
		 */
		if (of_device_is_compatible(pdev->dev.of_node,
					    "ti,keystone-nand")) {
			pdata->options |= NAND_NO_SUBPAGE_WRITE;
		}
	}

	return dev_get_platdata(&pdev->dev);
}
#else
static struct davinci_nand_pdata
	*nand_davinci_get_pdata(struct platform_device *pdev)
{
	return dev_get_platdata(&pdev->dev);
}
#endif

static int davinci_nand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct davinci_nand_info *info = to_davinci_nand(mtd);
	struct davinci_nand_pdata *pdata = nand_davinci_get_pdata(info->pdev);
	int ret = 0;

	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	switch (info->chip.ecc.mode) {
	case NAND_ECC_NONE:
		pdata->ecc_bits = 0;
		break;
	case NAND_ECC_SOFT:
		pdata->ecc_bits = 0;
		/*
		 * This driver expects Hamming based ECC when ecc_mode is set
		 * to NAND_ECC_SOFT. Force ecc.algo to NAND_ECC_HAMMING to
		 * avoid adding an extra ->ecc_algo field to
		 * davinci_nand_pdata.
		 */
		info->chip.ecc.algo = NAND_ECC_HAMMING;
		break;
	case NAND_ECC_HW:
		if (pdata->ecc_bits == 4) {
			/*
			 * No sanity checks:  CPUs must support this,
			 * and the chips may not use NAND_BUSWIDTH_16.
			 */

			/* No sharing 4-bit hardware between chipselects yet */
			spin_lock_irq(&davinci_nand_lock);
			if (ecc4_busy)
				ret = -EBUSY;
			else
				ecc4_busy = true;
			spin_unlock_irq(&davinci_nand_lock);

			if (ret == -EBUSY)
				return ret;

			info->chip.ecc.calculate = nand_davinci_calculate_4bit;
			info->chip.ecc.correct = nand_davinci_correct_4bit;
			info->chip.ecc.hwctl = nand_davinci_hwctl_4bit;
			info->chip.ecc.bytes = 10;
			info->chip.ecc.options = NAND_ECC_GENERIC_ERASED_CHECK;
			info->chip.ecc.algo = NAND_ECC_BCH;
		} else {
			/* 1bit ecc hamming */
			info->chip.ecc.calculate = nand_davinci_calculate_1bit;
			info->chip.ecc.correct = nand_davinci_correct_1bit;
			info->chip.ecc.hwctl = nand_davinci_hwctl_1bit;
			info->chip.ecc.bytes = 3;
			info->chip.ecc.algo = NAND_ECC_HAMMING;
		}
		info->chip.ecc.size = 512;
		info->chip.ecc.strength = pdata->ecc_bits;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Update ECC layout if needed ... for 1-bit HW ECC, the default
	 * is OK, but it allocates 6 bytes when only 3 are needed (for
	 * each 512 bytes).  For the 4-bit HW ECC, that default is not
	 * usable:  10 bytes are needed, not 6.
	 */
	if (pdata->ecc_bits == 4) {
		int chunks = mtd->writesize / 512;

		if (!chunks || mtd->oobsize < 16) {
			dev_dbg(&info->pdev->dev, "too small\n");
			return -EINVAL;
		}

		/* For small page chips, preserve the manufacturer's
		 * badblock marking data ... and make sure a flash BBT
		 * table marker fits in the free bytes.
		 */
		if (chunks == 1) {
			mtd_set_ooblayout(mtd, &hwecc4_small_ooblayout_ops);
		} else if (chunks == 4 || chunks == 8) {
			mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
			info->chip.ecc.mode = NAND_ECC_HW_OOB_FIRST;
		} else {
			return -EIO;
		}
	}

	return ret;
}

static const struct nand_controller_ops davinci_nand_controller_ops = {
	.attach_chip = davinci_nand_attach_chip,
};

static int nand_davinci_probe(struct platform_device *pdev)
{
	struct davinci_nand_pdata	*pdata;
	struct davinci_nand_info	*info;
	struct resource			*res1;
	struct resource			*res2;
	void __iomem			*vaddr;
	void __iomem			*base;
	int				ret;
	uint32_t			val;
	struct mtd_info			*mtd;

	pdata = nand_davinci_get_pdata(pdev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	/* insist on board-specific configuration */
	if (!pdata)
		return -ENODEV;

	/* which external chipselect will we be managing? */
	if (pdata->core_chipsel < 0 || pdata->core_chipsel > 3)
		return -ENODEV;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	res1 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res1 || !res2) {
		dev_err(&pdev->dev, "resource missing\n");
		return -EINVAL;
	}

	vaddr = devm_ioremap_resource(&pdev->dev, res1);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	/*
	 * This registers range is used to setup NAND settings. In case with
	 * TI AEMIF driver, the same memory address range is requested already
	 * by AEMIF, so we cannot request it twice, just ioremap.
	 * The AEMIF and NAND drivers not use the same registers in this range.
	 */
	base = devm_ioremap(&pdev->dev, res2->start, resource_size(res2));
	if (!base) {
		dev_err(&pdev->dev, "ioremap failed for resource %pR\n", res2);
		return -EADDRNOTAVAIL;
	}

	info->pdev		= pdev;
	info->base		= base;
	info->vaddr		= vaddr;

	mtd			= nand_to_mtd(&info->chip);
	mtd->dev.parent		= &pdev->dev;
	nand_set_flash_node(&info->chip, pdev->dev.of_node);

	info->chip.IO_ADDR_R	= vaddr;
	info->chip.IO_ADDR_W	= vaddr;
	info->chip.chip_delay	= 0;
	info->chip.select_chip	= nand_davinci_select_chip;

	/* options such as NAND_BBT_USE_FLASH */
	info->chip.bbt_options	= pdata->bbt_options;
	/* options such as 16-bit widths */
	info->chip.options	= pdata->options;
	info->chip.bbt_td	= pdata->bbt_td;
	info->chip.bbt_md	= pdata->bbt_md;
	info->timing		= pdata->timing;

	info->current_cs	= info->vaddr;
	info->core_chipsel	= pdata->core_chipsel;
	info->mask_chipsel	= pdata->mask_chipsel;

	/* use nandboot-capable ALE/CLE masks by default */
	info->mask_ale		= pdata->mask_ale ? : MASK_ALE;
	info->mask_cle		= pdata->mask_cle ? : MASK_CLE;

	/* Set address of hardware control function */
	info->chip.cmd_ctrl	= nand_davinci_hwcontrol;
	info->chip.dev_ready	= nand_davinci_dev_ready;

	/* Speed up buffer I/O */
	info->chip.read_buf     = nand_davinci_read_buf;
	info->chip.write_buf    = nand_davinci_write_buf;

	/* Use board-specific ECC config */
	info->chip.ecc.mode	= pdata->ecc_mode;

	spin_lock_irq(&davinci_nand_lock);

	/* put CSxNAND into NAND mode */
	val = davinci_nand_readl(info, NANDFCR_OFFSET);
	val |= BIT(info->core_chipsel);
	davinci_nand_writel(info, NANDFCR_OFFSET, val);

	spin_unlock_irq(&davinci_nand_lock);

	/* Scan to find existence of the device(s) */
	info->chip.dummy_controller.ops = &davinci_nand_controller_ops;
	ret = nand_scan(mtd, pdata->mask_chipsel ? 2 : 1);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "no NAND chip(s) found\n");
		return ret;
	}

	if (pdata->parts)
		ret = mtd_device_register(mtd, pdata->parts, pdata->nr_parts);
	else
		ret = mtd_device_register(mtd, NULL, 0);
	if (ret < 0)
		goto err_cleanup_nand;

	val = davinci_nand_readl(info, NRCSR_OFFSET);
	dev_info(&pdev->dev, "controller rev. %d.%d\n",
	       (val >> 8) & 0xff, val & 0xff);

	return 0;

err_cleanup_nand:
	nand_cleanup(&info->chip);

	return ret;
}

static int nand_davinci_remove(struct platform_device *pdev)
{
	struct davinci_nand_info *info = platform_get_drvdata(pdev);

	spin_lock_irq(&davinci_nand_lock);
	if (info->chip.ecc.mode == NAND_ECC_HW_SYNDROME)
		ecc4_busy = false;
	spin_unlock_irq(&davinci_nand_lock);

	nand_release(nand_to_mtd(&info->chip));

	return 0;
}

static struct platform_driver nand_davinci_driver = {
	.probe		= nand_davinci_probe,
	.remove		= nand_davinci_remove,
	.driver		= {
		.name	= "davinci_nand",
		.of_match_table = of_match_ptr(davinci_nand_of_match),
	},
};
MODULE_ALIAS("platform:davinci_nand");

module_platform_driver(nand_davinci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("Davinci NAND flash driver");

