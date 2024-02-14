// SPDX-License-Identifier: GPL-2.0
/*
 * JZ4725B BCH controller driver
 *
 * Copyright (C) 2019 Paul Cercueil <paul@crapouillou.net>
 *
 * Based on jz4780_bch.c
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "ingenic_ecc.h"

#define BCH_BHCR			0x0
#define BCH_BHCSR			0x4
#define BCH_BHCCR			0x8
#define BCH_BHCNT			0xc
#define BCH_BHDR			0x10
#define BCH_BHPAR0			0x14
#define BCH_BHERR0			0x28
#define BCH_BHINT			0x24
#define BCH_BHINTES			0x3c
#define BCH_BHINTEC			0x40
#define BCH_BHINTE			0x38

#define BCH_BHCR_ENCE			BIT(3)
#define BCH_BHCR_BSEL			BIT(2)
#define BCH_BHCR_INIT			BIT(1)
#define BCH_BHCR_BCHE			BIT(0)

#define BCH_BHCNT_DEC_COUNT_SHIFT	16
#define BCH_BHCNT_DEC_COUNT_MASK	(0x3ff << BCH_BHCNT_DEC_COUNT_SHIFT)
#define BCH_BHCNT_ENC_COUNT_SHIFT	0
#define BCH_BHCNT_ENC_COUNT_MASK	(0x3ff << BCH_BHCNT_ENC_COUNT_SHIFT)

#define BCH_BHERR_INDEX0_SHIFT		0
#define BCH_BHERR_INDEX0_MASK		(0x1fff << BCH_BHERR_INDEX0_SHIFT)
#define BCH_BHERR_INDEX1_SHIFT		16
#define BCH_BHERR_INDEX1_MASK		(0x1fff << BCH_BHERR_INDEX1_SHIFT)

#define BCH_BHINT_ERRC_SHIFT		28
#define BCH_BHINT_ERRC_MASK		(0xf << BCH_BHINT_ERRC_SHIFT)
#define BCH_BHINT_TERRC_SHIFT		16
#define BCH_BHINT_TERRC_MASK		(0x7f << BCH_BHINT_TERRC_SHIFT)
#define BCH_BHINT_ALL_0			BIT(5)
#define BCH_BHINT_ALL_F			BIT(4)
#define BCH_BHINT_DECF			BIT(3)
#define BCH_BHINT_ENCF			BIT(2)
#define BCH_BHINT_UNCOR			BIT(1)
#define BCH_BHINT_ERR			BIT(0)

/* Timeout for BCH calculation/correction. */
#define BCH_TIMEOUT_US			100000

static inline void jz4725b_bch_config_set(struct ingenic_ecc *bch, u32 cfg)
{
	writel(cfg, bch->base + BCH_BHCSR);
}

static inline void jz4725b_bch_config_clear(struct ingenic_ecc *bch, u32 cfg)
{
	writel(cfg, bch->base + BCH_BHCCR);
}

static int jz4725b_bch_reset(struct ingenic_ecc *bch,
			     struct ingenic_ecc_params *params, bool calc_ecc)
{
	u32 reg, max_value;

	/* Clear interrupt status. */
	writel(readl(bch->base + BCH_BHINT), bch->base + BCH_BHINT);

	/* Initialise and enable BCH. */
	jz4725b_bch_config_clear(bch, 0x1f);
	jz4725b_bch_config_set(bch, BCH_BHCR_BCHE);

	if (params->strength == 8)
		jz4725b_bch_config_set(bch, BCH_BHCR_BSEL);
	else
		jz4725b_bch_config_clear(bch, BCH_BHCR_BSEL);

	if (calc_ecc) /* calculate ECC from data */
		jz4725b_bch_config_set(bch, BCH_BHCR_ENCE);
	else /* correct data from ECC */
		jz4725b_bch_config_clear(bch, BCH_BHCR_ENCE);

	jz4725b_bch_config_set(bch, BCH_BHCR_INIT);

	max_value = BCH_BHCNT_ENC_COUNT_MASK >> BCH_BHCNT_ENC_COUNT_SHIFT;
	if (params->size > max_value)
		return -EINVAL;

	max_value = BCH_BHCNT_DEC_COUNT_MASK >> BCH_BHCNT_DEC_COUNT_SHIFT;
	if (params->size + params->bytes > max_value)
		return -EINVAL;

	/* Set up BCH count register. */
	reg = params->size << BCH_BHCNT_ENC_COUNT_SHIFT;
	reg |= (params->size + params->bytes) << BCH_BHCNT_DEC_COUNT_SHIFT;
	writel(reg, bch->base + BCH_BHCNT);

	return 0;
}

static void jz4725b_bch_disable(struct ingenic_ecc *bch)
{
	/* Clear interrupts */
	writel(readl(bch->base + BCH_BHINT), bch->base + BCH_BHINT);

	/* Disable the hardware */
	jz4725b_bch_config_clear(bch, BCH_BHCR_BCHE);
}

static void jz4725b_bch_write_data(struct ingenic_ecc *bch, const u8 *buf,
				   size_t size)
{
	while (size--)
		writeb(*buf++, bch->base + BCH_BHDR);
}

static void jz4725b_bch_read_parity(struct ingenic_ecc *bch, u8 *buf,
				    size_t size)
{
	size_t size32 = size / sizeof(u32);
	size_t size8 = size % sizeof(u32);
	u32 *dest32;
	u8 *dest8;
	u32 val, offset = 0;

	dest32 = (u32 *)buf;
	while (size32--) {
		*dest32++ = readl_relaxed(bch->base + BCH_BHPAR0 + offset);
		offset += sizeof(u32);
	}

	dest8 = (u8 *)dest32;
	val = readl_relaxed(bch->base + BCH_BHPAR0 + offset);
	switch (size8) {
	case 3:
		dest8[2] = (val >> 16) & 0xff;
		fallthrough;
	case 2:
		dest8[1] = (val >> 8) & 0xff;
		fallthrough;
	case 1:
		dest8[0] = val & 0xff;
		break;
	}
}

static int jz4725b_bch_wait_complete(struct ingenic_ecc *bch, unsigned int irq,
				     u32 *status)
{
	u32 reg;
	int ret;

	/*
	 * While we could use interrupts here and sleep until the operation
	 * completes, the controller works fairly quickly (usually a few
	 * microseconds) and so the overhead of sleeping until we get an
	 * interrupt quite noticeably decreases performance.
	 */
	ret = readl_relaxed_poll_timeout(bch->base + BCH_BHINT, reg,
					 reg & irq, 0, BCH_TIMEOUT_US);
	if (ret)
		return ret;

	if (status)
		*status = reg;

	writel(reg, bch->base + BCH_BHINT);

	return 0;
}

static int jz4725b_calculate(struct ingenic_ecc *bch,
			     struct ingenic_ecc_params *params,
			     const u8 *buf, u8 *ecc_code)
{
	int ret;

	mutex_lock(&bch->lock);

	ret = jz4725b_bch_reset(bch, params, true);
	if (ret) {
		dev_err(bch->dev, "Unable to init BCH with given parameters\n");
		goto out_disable;
	}

	jz4725b_bch_write_data(bch, buf, params->size);

	ret = jz4725b_bch_wait_complete(bch, BCH_BHINT_ENCF, NULL);
	if (ret) {
		dev_err(bch->dev, "timed out while calculating ECC\n");
		goto out_disable;
	}

	jz4725b_bch_read_parity(bch, ecc_code, params->bytes);

out_disable:
	jz4725b_bch_disable(bch);
	mutex_unlock(&bch->lock);

	return ret;
}

static int jz4725b_correct(struct ingenic_ecc *bch,
			   struct ingenic_ecc_params *params,
			   u8 *buf, u8 *ecc_code)
{
	u32 reg, errors, bit;
	unsigned int i;
	int ret;

	mutex_lock(&bch->lock);

	ret = jz4725b_bch_reset(bch, params, false);
	if (ret) {
		dev_err(bch->dev, "Unable to init BCH with given parameters\n");
		goto out;
	}

	jz4725b_bch_write_data(bch, buf, params->size);
	jz4725b_bch_write_data(bch, ecc_code, params->bytes);

	ret = jz4725b_bch_wait_complete(bch, BCH_BHINT_DECF, &reg);
	if (ret) {
		dev_err(bch->dev, "timed out while correcting data\n");
		goto out;
	}

	if (reg & (BCH_BHINT_ALL_F | BCH_BHINT_ALL_0)) {
		/* Data and ECC is all 0xff or 0x00 - nothing to correct */
		ret = 0;
		goto out;
	}

	if (reg & BCH_BHINT_UNCOR) {
		/* Uncorrectable ECC error */
		ret = -EBADMSG;
		goto out;
	}

	errors = (reg & BCH_BHINT_ERRC_MASK) >> BCH_BHINT_ERRC_SHIFT;

	/* Correct any detected errors. */
	for (i = 0; i < errors; i++) {
		if (i & 1) {
			bit = (reg & BCH_BHERR_INDEX1_MASK) >> BCH_BHERR_INDEX1_SHIFT;
		} else {
			reg = readl(bch->base + BCH_BHERR0 + (i * 4));
			bit = (reg & BCH_BHERR_INDEX0_MASK) >> BCH_BHERR_INDEX0_SHIFT;
		}

		buf[(bit >> 3)] ^= BIT(bit & 0x7);
	}

out:
	jz4725b_bch_disable(bch);
	mutex_unlock(&bch->lock);

	return ret;
}

static const struct ingenic_ecc_ops jz4725b_bch_ops = {
	.disable = jz4725b_bch_disable,
	.calculate = jz4725b_calculate,
	.correct = jz4725b_correct,
};

static const struct of_device_id jz4725b_bch_dt_match[] = {
	{ .compatible = "ingenic,jz4725b-bch", .data = &jz4725b_bch_ops },
	{},
};
MODULE_DEVICE_TABLE(of, jz4725b_bch_dt_match);

static struct platform_driver jz4725b_bch_driver = {
	.probe		= ingenic_ecc_probe,
	.driver	= {
		.name	= "jz4725b-bch",
		.of_match_table = jz4725b_bch_dt_match,
	},
};
module_platform_driver(jz4725b_bch_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("Ingenic JZ4725B BCH controller driver");
MODULE_LICENSE("GPL v2");
