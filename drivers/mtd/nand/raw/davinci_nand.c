// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * davinci_nand.c - NAND Flash Driver for DaVinci family chips
 *
 * Copyright © 2006 Texas Instruments.
 *
 * Port to 2.6.23 Copyright © 2008 by:
 *   Sander Huijsen <Shuijsen@optelecom-nkf.com>
 *   Troy Kisky <troy.kisky@boundarydevices.com>
 *   Dirk Behme <Dirk.Behme@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/iopoll.h>
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
	struct nand_controller	controller;
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
 * 1-bit hardware ECC ... context maintained for each core chipselect
 */

static inline uint32_t nand_davinci_readecc_1bit(struct mtd_info *mtd)
{
	struct davinci_nand_info *info = to_davinci_nand(mtd);

	return davinci_nand_readl(info, NANDF1ECC_OFFSET
			+ 4 * info->core_chipsel);
}

static void nand_davinci_hwctl_1bit(struct nand_chip *chip, int mode)
{
	struct davinci_nand_info *info;
	uint32_t nandcfr;
	unsigned long flags;

	info = to_davinci_nand(nand_to_mtd(chip));

	/* Reset ECC hardware */
	nand_davinci_readecc_1bit(nand_to_mtd(chip));

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
static int nand_davinci_calculate_1bit(struct nand_chip *chip,
				       const u_char *dat, u_char *ecc_code)
{
	unsigned int ecc_val = nand_davinci_readecc_1bit(nand_to_mtd(chip));
	unsigned int ecc24 = (ecc_val & 0x0fff) | ((ecc_val & 0x0fff0000) >> 4);

	/* invert so that erased block ecc is correct */
	ecc24 = ~ecc24;
	ecc_code[0] = (u_char)(ecc24);
	ecc_code[1] = (u_char)(ecc24 >> 8);
	ecc_code[2] = (u_char)(ecc24 >> 16);

	return 0;
}

static int nand_davinci_correct_1bit(struct nand_chip *chip, u_char *dat,
				     u_char *read_ecc, u_char *calc_ecc)
{
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
 * This is a syndrome engine, but we avoid NAND_ECC_PLACEMENT_INTERLEAVED
 * since that forces use of a problematic "infix OOB" layout.
 * Among other things, it trashes manufacturer bad block markers.
 * Also, and specific to this hardware, it ECC-protects the "prepad"
 * in the OOB ... while having ECC protection for parts of OOB would
 * seem useful, the current MTD stack sometimes wants to update the
 * OOB without recomputing ECC.
 */

static void nand_davinci_hwctl_4bit(struct nand_chip *chip, int mode)
{
	struct davinci_nand_info *info = to_davinci_nand(nand_to_mtd(chip));
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
static int nand_davinci_calculate_4bit(struct nand_chip *chip,
				       const u_char *dat, u_char *ecc_code)
{
	struct davinci_nand_info *info = to_davinci_nand(nand_to_mtd(chip));
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
static int nand_davinci_correct_4bit(struct nand_chip *chip, u_char *data,
				     u_char *ecc_code, u_char *null)
{
	int i;
	struct davinci_nand_info *info = to_davinci_nand(nand_to_mtd(chip));
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

/**
 * nand_read_page_hwecc_oob_first - hw ecc, read oob first
 * @chip: nand chip info structure
 * @buf: buffer to store read data
 * @oob_required: caller requires OOB data read to chip->oob_poi
 * @page: page number to read
 *
 * Hardware ECC for large page chips, require OOB to be read first. For this
 * ECC mode, the write_page method is re-used from ECC_HW. These methods
 * read/write ECC from the OOB area, unlike the ECC_HW_SYNDROME support with
 * multiple ECC steps, follows the "infix ECC" scheme and reads/writes ECC from
 * the data area, by overwriting the NAND manufacturer bad block markings.
 */
static int nand_davinci_read_page_hwecc_oob_first(struct nand_chip *chip,
						  uint8_t *buf,
						  int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int i, eccsize = chip->ecc.size, ret;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	uint8_t *p = buf;
	uint8_t *ecc_code = chip->ecc.code_buf;
	uint8_t *ecc_calc = chip->ecc.calc_buf;
	unsigned int max_bitflips = 0;

	/* Read the OOB area first */
	ret = nand_read_oob_op(chip, page, 0, chip->oob_poi, mtd->oobsize);
	if (ret)
		return ret;

	ret = nand_read_page_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	ret = mtd_ooblayout_get_eccbytes(mtd, ecc_code, chip->oob_poi, 0,
					 chip->ecc.total);
	if (ret)
		return ret;

	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize) {
		int stat;

		chip->ecc.hwctl(chip, NAND_ECC_READ);

		ret = nand_read_data_op(chip, p, eccsize, false, false);
		if (ret)
			return ret;

		chip->ecc.calculate(chip, p, &ecc_calc[i]);

		stat = chip->ecc.correct(chip, p, &ecc_code[i], NULL);
		if (stat == -EBADMSG &&
		    (chip->ecc.options & NAND_ECC_GENERIC_ERASED_CHECK)) {
			/* check for empty pages with bitflips */
			stat = nand_check_erased_ecc_chunk(p, eccsize,
							   &ecc_code[i],
							   eccbytes, NULL, 0,
							   chip->ecc.strength);
		}

		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}
	return max_bitflips;
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
				pdata->engine_type = NAND_ECC_ENGINE_TYPE_NONE;
			if (!strncmp("soft", mode, 4))
				pdata->engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
			if (!strncmp("hw", mode, 2))
				pdata->engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;
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

	/* Use board-specific ECC config */
	chip->ecc.engine_type = pdata->engine_type;
	chip->ecc.placement = pdata->ecc_placement;

	switch (chip->ecc.engine_type) {
	case NAND_ECC_ENGINE_TYPE_NONE:
		pdata->ecc_bits = 0;
		break;
	case NAND_ECC_ENGINE_TYPE_SOFT:
		pdata->ecc_bits = 0;
		/*
		 * This driver expects Hamming based ECC when engine_type is set
		 * to NAND_ECC_ENGINE_TYPE_SOFT. Force ecc.algo to
		 * NAND_ECC_ALGO_HAMMING to avoid adding an extra ->ecc_algo
		 * field to davinci_nand_pdata.
		 */
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
		break;
	case NAND_ECC_ENGINE_TYPE_ON_HOST:
		if (pdata->ecc_bits == 4) {
			int chunks = mtd->writesize / 512;

			if (!chunks || mtd->oobsize < 16) {
				dev_dbg(&info->pdev->dev, "too small\n");
				return -EINVAL;
			}

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

			chip->ecc.calculate = nand_davinci_calculate_4bit;
			chip->ecc.correct = nand_davinci_correct_4bit;
			chip->ecc.hwctl = nand_davinci_hwctl_4bit;
			chip->ecc.bytes = 10;
			chip->ecc.options = NAND_ECC_GENERIC_ERASED_CHECK;
			chip->ecc.algo = NAND_ECC_ALGO_BCH;

			/*
			 * Update ECC layout if needed ... for 1-bit HW ECC, the
			 * default is OK, but it allocates 6 bytes when only 3
			 * are needed (for each 512 bytes). For 4-bit HW ECC,
			 * the default is not usable: 10 bytes needed, not 6.
			 *
			 * For small page chips, preserve the manufacturer's
			 * badblock marking data ... and make sure a flash BBT
			 * table marker fits in the free bytes.
			 */
			if (chunks == 1) {
				mtd_set_ooblayout(mtd,
						  &hwecc4_small_ooblayout_ops);
			} else if (chunks == 4 || chunks == 8) {
				mtd_set_ooblayout(mtd,
						  nand_get_large_page_ooblayout());
				chip->ecc.read_page = nand_davinci_read_page_hwecc_oob_first;
			} else {
				return -EIO;
			}
		} else {
			/* 1bit ecc hamming */
			chip->ecc.calculate = nand_davinci_calculate_1bit;
			chip->ecc.correct = nand_davinci_correct_1bit;
			chip->ecc.hwctl = nand_davinci_hwctl_1bit;
			chip->ecc.bytes = 3;
			chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
		}
		chip->ecc.size = 512;
		chip->ecc.strength = pdata->ecc_bits;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void nand_davinci_data_in(struct davinci_nand_info *info, void *buf,
				 unsigned int len, bool force_8bit)
{
	u32 alignment = ((uintptr_t)buf | len) & 3;

	if (force_8bit || (alignment & 1))
		ioread8_rep(info->current_cs, buf, len);
	else if (alignment & 3)
		ioread16_rep(info->current_cs, buf, len >> 1);
	else
		ioread32_rep(info->current_cs, buf, len >> 2);
}

static void nand_davinci_data_out(struct davinci_nand_info *info,
				  const void *buf, unsigned int len,
				  bool force_8bit)
{
	u32 alignment = ((uintptr_t)buf | len) & 3;

	if (force_8bit || (alignment & 1))
		iowrite8_rep(info->current_cs, buf, len);
	else if (alignment & 3)
		iowrite16_rep(info->current_cs, buf, len >> 1);
	else
		iowrite32_rep(info->current_cs, buf, len >> 2);
}

static int davinci_nand_exec_instr(struct davinci_nand_info *info,
				   const struct nand_op_instr *instr)
{
	unsigned int i, timeout_us;
	u32 status;
	int ret;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		iowrite8(instr->ctx.cmd.opcode,
			 info->current_cs + info->mask_cle);
		break;

	case NAND_OP_ADDR_INSTR:
		for (i = 0; i < instr->ctx.addr.naddrs; i++) {
			iowrite8(instr->ctx.addr.addrs[i],
				 info->current_cs + info->mask_ale);
		}
		break;

	case NAND_OP_DATA_IN_INSTR:
		nand_davinci_data_in(info, instr->ctx.data.buf.in,
				     instr->ctx.data.len,
				     instr->ctx.data.force_8bit);
		break;

	case NAND_OP_DATA_OUT_INSTR:
		nand_davinci_data_out(info, instr->ctx.data.buf.out,
				      instr->ctx.data.len,
				      instr->ctx.data.force_8bit);
		break;

	case NAND_OP_WAITRDY_INSTR:
		timeout_us = instr->ctx.waitrdy.timeout_ms * 1000;
		ret = readl_relaxed_poll_timeout(info->base + NANDFSR_OFFSET,
						 status, status & BIT(0), 100,
						 timeout_us);
		if (ret)
			return ret;

		break;
	}

	if (instr->delay_ns)
		ndelay(instr->delay_ns);

	return 0;
}

static int davinci_nand_exec_op(struct nand_chip *chip,
				const struct nand_operation *op,
				bool check_only)
{
	struct davinci_nand_info *info = to_davinci_nand(nand_to_mtd(chip));
	unsigned int i;

	if (check_only)
		return 0;

	info->current_cs = info->vaddr + (op->cs * info->mask_chipsel);

	for (i = 0; i < op->ninstrs; i++) {
		int ret;

		ret = davinci_nand_exec_instr(info, &op->instrs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct nand_controller_ops davinci_nand_controller_ops = {
	.attach_chip = davinci_nand_attach_chip,
	.exec_op = davinci_nand_exec_op,
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

	spin_lock_irq(&davinci_nand_lock);

	/* put CSxNAND into NAND mode */
	val = davinci_nand_readl(info, NANDFCR_OFFSET);
	val |= BIT(info->core_chipsel);
	davinci_nand_writel(info, NANDFCR_OFFSET, val);

	spin_unlock_irq(&davinci_nand_lock);

	/* Scan to find existence of the device(s) */
	nand_controller_init(&info->controller);
	info->controller.ops = &davinci_nand_controller_ops;
	info->chip.controller = &info->controller;
	ret = nand_scan(&info->chip, pdata->mask_chipsel ? 2 : 1);
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
	struct nand_chip *chip = &info->chip;
	int ret;

	spin_lock_irq(&davinci_nand_lock);
	if (chip->ecc.placement == NAND_ECC_PLACEMENT_INTERLEAVED)
		ecc4_busy = false;
	spin_unlock_irq(&davinci_nand_lock);

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);

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

