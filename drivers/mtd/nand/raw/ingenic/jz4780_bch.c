// SPDX-License-Identifier: GPL-2.0
/*
 * JZ4780 BCH controller driver
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "ingenic_ecc.h"

#define BCH_BHCR			0x0
#define BCH_BHCCR			0x8
#define BCH_BHCNT			0xc
#define BCH_BHDR			0x10
#define BCH_BHPAR0			0x14
#define BCH_BHERR0			0x84
#define BCH_BHINT			0x184
#define BCH_BHINTES			0x188
#define BCH_BHINTEC			0x18c
#define BCH_BHINTE			0x190

#define BCH_BHCR_BSEL_SHIFT		4
#define BCH_BHCR_BSEL_MASK		(0x7f << BCH_BHCR_BSEL_SHIFT)
#define BCH_BHCR_ENCE			BIT(2)
#define BCH_BHCR_INIT			BIT(1)
#define BCH_BHCR_BCHE			BIT(0)

#define BCH_BHCNT_PARITYSIZE_SHIFT	16
#define BCH_BHCNT_PARITYSIZE_MASK	(0x7f << BCH_BHCNT_PARITYSIZE_SHIFT)
#define BCH_BHCNT_BLOCKSIZE_SHIFT	0
#define BCH_BHCNT_BLOCKSIZE_MASK	(0x7ff << BCH_BHCNT_BLOCKSIZE_SHIFT)

#define BCH_BHERR_MASK_SHIFT		16
#define BCH_BHERR_MASK_MASK		(0xffff << BCH_BHERR_MASK_SHIFT)
#define BCH_BHERR_INDEX_SHIFT		0
#define BCH_BHERR_INDEX_MASK		(0x7ff << BCH_BHERR_INDEX_SHIFT)

#define BCH_BHINT_ERRC_SHIFT		24
#define BCH_BHINT_ERRC_MASK		(0x7f << BCH_BHINT_ERRC_SHIFT)
#define BCH_BHINT_TERRC_SHIFT		16
#define BCH_BHINT_TERRC_MASK		(0x7f << BCH_BHINT_TERRC_SHIFT)
#define BCH_BHINT_DECF			BIT(3)
#define BCH_BHINT_ENCF			BIT(2)
#define BCH_BHINT_UNCOR			BIT(1)
#define BCH_BHINT_ERR			BIT(0)

#define BCH_CLK_RATE			(200 * 1000 * 1000)

/* Timeout for BCH calculation/correction. */
#define BCH_TIMEOUT_US			100000

static void jz4780_bch_reset(struct ingenic_ecc *bch,
			     struct ingenic_ecc_params *params, bool encode)
{
	u32 reg;

	/* Clear interrupt status. */
	writel(readl(bch->base + BCH_BHINT), bch->base + BCH_BHINT);

	/* Set up BCH count register. */
	reg = params->size << BCH_BHCNT_BLOCKSIZE_SHIFT;
	reg |= params->bytes << BCH_BHCNT_PARITYSIZE_SHIFT;
	writel(reg, bch->base + BCH_BHCNT);

	/* Initialise and enable BCH. */
	reg = BCH_BHCR_BCHE | BCH_BHCR_INIT;
	reg |= params->strength << BCH_BHCR_BSEL_SHIFT;
	if (encode)
		reg |= BCH_BHCR_ENCE;
	writel(reg, bch->base + BCH_BHCR);
}

static void jz4780_bch_disable(struct ingenic_ecc *bch)
{
	writel(readl(bch->base + BCH_BHINT), bch->base + BCH_BHINT);
	writel(BCH_BHCR_BCHE, bch->base + BCH_BHCCR);
}

static void jz4780_bch_write_data(struct ingenic_ecc *bch, const void *buf,
				  size_t size)
{
	size_t size32 = size / sizeof(u32);
	size_t size8 = size % sizeof(u32);
	const u32 *src32;
	const u8 *src8;

	src32 = (const u32 *)buf;
	while (size32--)
		writel(*src32++, bch->base + BCH_BHDR);

	src8 = (const u8 *)src32;
	while (size8--)
		writeb(*src8++, bch->base + BCH_BHDR);
}

static void jz4780_bch_read_parity(struct ingenic_ecc *bch, void *buf,
				   size_t size)
{
	size_t size32 = size / sizeof(u32);
	size_t size8 = size % sizeof(u32);
	u32 *dest32;
	u8 *dest8;
	u32 val, offset = 0;

	dest32 = (u32 *)buf;
	while (size32--) {
		*dest32++ = readl(bch->base + BCH_BHPAR0 + offset);
		offset += sizeof(u32);
	}

	dest8 = (u8 *)dest32;
	val = readl(bch->base + BCH_BHPAR0 + offset);
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

static bool jz4780_bch_wait_complete(struct ingenic_ecc *bch, unsigned int irq,
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
	ret = readl_poll_timeout(bch->base + BCH_BHINT, reg,
				 (reg & irq) == irq, 0, BCH_TIMEOUT_US);
	if (ret)
		return false;

	if (status)
		*status = reg;

	writel(reg, bch->base + BCH_BHINT);
	return true;
}

static int jz4780_calculate(struct ingenic_ecc *bch,
			    struct ingenic_ecc_params *params,
			    const u8 *buf, u8 *ecc_code)
{
	int ret = 0;

	mutex_lock(&bch->lock);

	jz4780_bch_reset(bch, params, true);
	jz4780_bch_write_data(bch, buf, params->size);

	if (jz4780_bch_wait_complete(bch, BCH_BHINT_ENCF, NULL)) {
		jz4780_bch_read_parity(bch, ecc_code, params->bytes);
	} else {
		dev_err(bch->dev, "timed out while calculating ECC\n");
		ret = -ETIMEDOUT;
	}

	jz4780_bch_disable(bch);
	mutex_unlock(&bch->lock);
	return ret;
}

static int jz4780_correct(struct ingenic_ecc *bch,
			  struct ingenic_ecc_params *params,
			  u8 *buf, u8 *ecc_code)
{
	u32 reg, mask, index;
	int i, ret, count;

	mutex_lock(&bch->lock);

	jz4780_bch_reset(bch, params, false);
	jz4780_bch_write_data(bch, buf, params->size);
	jz4780_bch_write_data(bch, ecc_code, params->bytes);

	if (!jz4780_bch_wait_complete(bch, BCH_BHINT_DECF, &reg)) {
		dev_err(bch->dev, "timed out while correcting data\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	if (reg & BCH_BHINT_UNCOR) {
		dev_warn(bch->dev, "uncorrectable ECC error\n");
		ret = -EBADMSG;
		goto out;
	}

	/* Correct any detected errors. */
	if (reg & BCH_BHINT_ERR) {
		count = (reg & BCH_BHINT_ERRC_MASK) >> BCH_BHINT_ERRC_SHIFT;
		ret = (reg & BCH_BHINT_TERRC_MASK) >> BCH_BHINT_TERRC_SHIFT;

		for (i = 0; i < count; i++) {
			reg = readl(bch->base + BCH_BHERR0 + (i * 4));
			mask = (reg & BCH_BHERR_MASK_MASK) >>
						BCH_BHERR_MASK_SHIFT;
			index = (reg & BCH_BHERR_INDEX_MASK) >>
						BCH_BHERR_INDEX_SHIFT;
			buf[(index * 2) + 0] ^= mask;
			buf[(index * 2) + 1] ^= mask >> 8;
		}
	} else {
		ret = 0;
	}

out:
	jz4780_bch_disable(bch);
	mutex_unlock(&bch->lock);
	return ret;
}

static int jz4780_bch_probe(struct platform_device *pdev)
{
	struct ingenic_ecc *bch;
	int ret;

	ret = ingenic_ecc_probe(pdev);
	if (ret)
		return ret;

	bch = platform_get_drvdata(pdev);
	clk_set_rate(bch->clk, BCH_CLK_RATE);

	return 0;
}

static const struct ingenic_ecc_ops jz4780_bch_ops = {
	.disable = jz4780_bch_disable,
	.calculate = jz4780_calculate,
	.correct = jz4780_correct,
};

static const struct of_device_id jz4780_bch_dt_match[] = {
	{ .compatible = "ingenic,jz4780-bch", .data = &jz4780_bch_ops },
	{},
};
MODULE_DEVICE_TABLE(of, jz4780_bch_dt_match);

static struct platform_driver jz4780_bch_driver = {
	.probe		= jz4780_bch_probe,
	.driver	= {
		.name	= "jz4780-bch",
		.of_match_table = of_match_ptr(jz4780_bch_dt_match),
	},
};
module_platform_driver(jz4780_bch_driver);

MODULE_AUTHOR("Alex Smith <alex@alex-smith.me.uk>");
MODULE_AUTHOR("Harvey Hunt <harveyhuntnexus@gmail.com>");
MODULE_DESCRIPTION("Ingenic JZ4780 BCH error correction driver");
MODULE_LICENSE("GPL v2");
