// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Versal True Random Number Generator driver
 * Copyright (c) 2024 - 2025 Advanced Micro Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/rng.h>
#include <crypto/aes.h>

/* TRNG Registers Offsets */
#define TRNG_STATUS_OFFSET			0x4U
#define TRNG_CTRL_OFFSET			0x8U
#define TRNG_EXT_SEED_OFFSET			0x40U
#define TRNG_PER_STRNG_OFFSET			0x80U
#define TRNG_CORE_OUTPUT_OFFSET			0xC0U
#define TRNG_RESET_OFFSET			0xD0U
#define TRNG_OSC_EN_OFFSET			0xD4U

/* Mask values */
#define TRNG_RESET_VAL_MASK			BIT(0)
#define TRNG_OSC_EN_VAL_MASK			BIT(0)
#define TRNG_CTRL_PRNGSRST_MASK			BIT(0)
#define TRNG_CTRL_EUMODE_MASK			BIT(8)
#define TRNG_CTRL_TRSSEN_MASK			BIT(2)
#define TRNG_CTRL_PRNGSTART_MASK		BIT(5)
#define TRNG_CTRL_PRNGXS_MASK			BIT(3)
#define TRNG_CTRL_PRNGMODE_MASK			BIT(7)
#define TRNG_STATUS_DONE_MASK			BIT(0)
#define TRNG_STATUS_QCNT_MASK			GENMASK(11, 9)
#define TRNG_STATUS_QCNT_16_BYTES		0x800

/* Sizes in bytes */
#define TRNG_SEED_LEN_BYTES			48U
#define TRNG_ENTROPY_SEED_LEN_BYTES		64U
#define TRNG_SEC_STRENGTH_SHIFT			5U
#define TRNG_SEC_STRENGTH_BYTES			BIT(TRNG_SEC_STRENGTH_SHIFT)
#define TRNG_BYTES_PER_REG			4U
#define TRNG_RESET_DELAY			10
#define TRNG_NUM_INIT_REGS			12U
#define TRNG_READ_4_WORD			4
#define TRNG_DATA_READ_DELAY			8000

struct xilinx_rng {
	void __iomem *rng_base;
	struct device *dev;
	struct mutex lock;	/* Protect access to TRNG device */
	struct hwrng trng;
};

struct xilinx_rng_ctx {
	struct xilinx_rng *rng;
};

static struct xilinx_rng *xilinx_rng_dev;

static void xtrng_readwrite32(void __iomem *addr, u32 mask, u8 value)
{
	u32 val;

	val = ioread32(addr);
	val = (val & (~mask)) | (mask & value);
	iowrite32(val, addr);
}

static void xtrng_trng_reset(void __iomem *addr)
{
	xtrng_readwrite32(addr + TRNG_RESET_OFFSET, TRNG_RESET_VAL_MASK, TRNG_RESET_VAL_MASK);
	udelay(TRNG_RESET_DELAY);
	xtrng_readwrite32(addr + TRNG_RESET_OFFSET, TRNG_RESET_VAL_MASK, 0);
}

static void xtrng_hold_reset(void __iomem *addr)
{
	xtrng_readwrite32(addr + TRNG_CTRL_OFFSET, TRNG_CTRL_PRNGSRST_MASK,
			  TRNG_CTRL_PRNGSRST_MASK);
	iowrite32(TRNG_RESET_VAL_MASK, addr + TRNG_RESET_OFFSET);
	udelay(TRNG_RESET_DELAY);
}

static void xtrng_softreset(struct xilinx_rng *rng)
{
	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET, TRNG_CTRL_PRNGSRST_MASK,
			  TRNG_CTRL_PRNGSRST_MASK);
	udelay(TRNG_RESET_DELAY);
	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET, TRNG_CTRL_PRNGSRST_MASK, 0);
}

/* Return no. of bytes read */
static size_t xtrng_readblock32(void __iomem *rng_base, __be32 *buf, int blocks32, bool wait)
{
	int read = 0, ret;
	int timeout = 1;
	int i, idx;
	u32 val;

	if (wait)
		timeout = TRNG_DATA_READ_DELAY;

	for (i = 0; i < (blocks32 * 2); i++) {
		/* TRNG core generate data in 16 bytes. Read twice to complete 32 bytes read */
		ret = readl_poll_timeout(rng_base + TRNG_STATUS_OFFSET, val,
					 (val & TRNG_STATUS_QCNT_MASK) ==
					 TRNG_STATUS_QCNT_16_BYTES, !!wait, timeout);
		if (ret)
			break;

		for (idx = 0; idx < TRNG_READ_4_WORD; idx++) {
			*(buf + read) = cpu_to_be32(ioread32(rng_base + TRNG_CORE_OUTPUT_OFFSET));
			read += 1;
		}
	}
	return read * 4;
}

static int xtrng_collect_random_data(struct xilinx_rng *rng, u8 *rand_gen_buf,
				     int no_of_random_bytes, bool wait)
{
	u8 randbuf[TRNG_SEC_STRENGTH_BYTES];
	int byteleft, blocks, count = 0;
	int ret;

	byteleft = no_of_random_bytes & (TRNG_SEC_STRENGTH_BYTES - 1);
	blocks = no_of_random_bytes >> TRNG_SEC_STRENGTH_SHIFT;
	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET, TRNG_CTRL_PRNGSTART_MASK,
			  TRNG_CTRL_PRNGSTART_MASK);
	if (blocks) {
		ret = xtrng_readblock32(rng->rng_base, (__be32 *)rand_gen_buf, blocks, wait);
		if (!ret)
			return 0;
		count += ret;
	}

	if (byteleft) {
		ret = xtrng_readblock32(rng->rng_base, (__be32 *)randbuf, 1, wait);
		if (!ret)
			return count;
		memcpy(rand_gen_buf + (blocks * TRNG_SEC_STRENGTH_BYTES), randbuf, byteleft);
		count += byteleft;
	}

	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET,
			  TRNG_CTRL_PRNGMODE_MASK | TRNG_CTRL_PRNGSTART_MASK, 0U);

	return count;
}

static void xtrng_write_multiple_registers(void __iomem *base_addr, u32 *values, size_t n)
{
	void __iomem *reg_addr;
	size_t i;

	/* Write seed value into EXTERNAL_SEED Registers in big endian format */
	for (i = 0; i < n; i++) {
		reg_addr = (base_addr + ((n - 1 - i) * TRNG_BYTES_PER_REG));
		iowrite32((u32 __force)(cpu_to_be32(values[i])), reg_addr);
	}
}

static void xtrng_enable_entropy(struct xilinx_rng *rng)
{
	iowrite32(TRNG_OSC_EN_VAL_MASK, rng->rng_base + TRNG_OSC_EN_OFFSET);
	xtrng_softreset(rng);
	iowrite32(TRNG_CTRL_EUMODE_MASK | TRNG_CTRL_TRSSEN_MASK, rng->rng_base + TRNG_CTRL_OFFSET);
}

static int xtrng_reseed_internal(struct xilinx_rng *rng)
{
	u8 entropy[TRNG_ENTROPY_SEED_LEN_BYTES];
	u32 val;
	int ret;

	memset(entropy, 0, sizeof(entropy));
	xtrng_enable_entropy(rng);

	/* collect random data to use it as entropy (input for DF) */
	ret = xtrng_collect_random_data(rng, entropy, TRNG_SEED_LEN_BYTES, true);
	if (ret != TRNG_SEED_LEN_BYTES)
		return -EINVAL;

	xtrng_write_multiple_registers(rng->rng_base + TRNG_EXT_SEED_OFFSET,
				       (u32 *)entropy, TRNG_NUM_INIT_REGS);
	/* select reseed operation */
	iowrite32(TRNG_CTRL_PRNGXS_MASK, rng->rng_base + TRNG_CTRL_OFFSET);

	/* Start the reseed operation with above configuration and wait for STATUS.Done bit to be
	 * set. Monitor STATUS.CERTF bit, if set indicates SP800-90B entropy health test has failed.
	 */
	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET, TRNG_CTRL_PRNGSTART_MASK,
			  TRNG_CTRL_PRNGSTART_MASK);

	ret = readl_poll_timeout(rng->rng_base + TRNG_STATUS_OFFSET, val,
				 (val & TRNG_STATUS_DONE_MASK) == TRNG_STATUS_DONE_MASK,
				  1U, 15000U);
	if (ret)
		return ret;

	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET, TRNG_CTRL_PRNGSTART_MASK, 0U);

	return 0;
}

static int xtrng_random_bytes_generate(struct xilinx_rng *rng, u8 *rand_buf_ptr,
				       u32 rand_buf_size, int wait)
{
	int nbytes;
	int ret;

	xtrng_readwrite32(rng->rng_base + TRNG_CTRL_OFFSET,
			  TRNG_CTRL_PRNGMODE_MASK | TRNG_CTRL_PRNGXS_MASK,
			  TRNG_CTRL_PRNGMODE_MASK | TRNG_CTRL_PRNGXS_MASK);
	nbytes = xtrng_collect_random_data(rng, rand_buf_ptr, rand_buf_size, wait);

	ret = xtrng_reseed_internal(rng);
	if (ret) {
		dev_err(rng->dev, "Re-seed fail\n");
		return ret;
	}

	return nbytes;
}

static int xtrng_trng_generate(struct crypto_rng *tfm, const u8 *src, u32 slen,
			       u8 *dst, u32 dlen)
{
	struct xilinx_rng_ctx *ctx = crypto_rng_ctx(tfm);
	int ret;

	mutex_lock(&ctx->rng->lock);
	ret = xtrng_random_bytes_generate(ctx->rng, dst, dlen, true);
	mutex_unlock(&ctx->rng->lock);

	return ret < 0 ? ret : 0;
}

static int xtrng_trng_seed(struct crypto_rng *tfm, const u8 *seed, unsigned int slen)
{
	return 0;
}

static int xtrng_trng_init(struct crypto_tfm *rtfm)
{
	struct xilinx_rng_ctx *ctx = crypto_tfm_ctx(rtfm);

	ctx->rng = xilinx_rng_dev;

	return 0;
}

static struct rng_alg xtrng_trng_alg = {
	.generate = xtrng_trng_generate,
	.seed = xtrng_trng_seed,
	.seedsize = 0,
	.base = {
		.cra_name = "stdrng",
		.cra_driver_name = "xilinx-trng",
		.cra_priority = 300,
		.cra_ctxsize = sizeof(struct xilinx_rng_ctx),
		.cra_module = THIS_MODULE,
		.cra_init = xtrng_trng_init,
	},
};

static int xtrng_hwrng_trng_read(struct hwrng *hwrng, void *data, size_t max, bool wait)
{
	u8 buf[TRNG_SEC_STRENGTH_BYTES];
	struct xilinx_rng *rng;
	int ret = -EINVAL, i = 0;

	rng = container_of(hwrng, struct xilinx_rng, trng);
	/* Return in case wait not set and lock not available. */
	if (!mutex_trylock(&rng->lock) && !wait)
		return 0;
	else if (!mutex_is_locked(&rng->lock) && wait)
		mutex_lock(&rng->lock);

	while (i < max) {
		ret = xtrng_random_bytes_generate(rng, buf, TRNG_SEC_STRENGTH_BYTES, wait);
		if (ret < 0)
			break;

		memcpy(data + i, buf, min_t(int, ret, (max - i)));
		i += min_t(int, ret, (max - i));
	}
	mutex_unlock(&rng->lock);

	return ret;
}

static int xtrng_hwrng_register(struct hwrng *trng)
{
	int ret;

	trng->name = "Xilinx Versal Crypto Engine TRNG";
	trng->read = xtrng_hwrng_trng_read;

	ret = hwrng_register(trng);
	if (ret)
		pr_err("Fail to register the TRNG\n");

	return ret;
}

static void xtrng_hwrng_unregister(struct hwrng *trng)
{
	hwrng_unregister(trng);
}

static int xtrng_probe(struct platform_device *pdev)
{
	struct xilinx_rng *rng;
	int ret;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->dev = &pdev->dev;
	rng->rng_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rng->rng_base)) {
		dev_err(&pdev->dev, "Failed to map resource %ld\n", PTR_ERR(rng->rng_base));
		return PTR_ERR(rng->rng_base);
	}

	xtrng_trng_reset(rng->rng_base);
	ret = xtrng_reseed_internal(rng);
	if (ret) {
		dev_err(&pdev->dev, "TRNG Seed fail\n");
		return ret;
	}

	xilinx_rng_dev = rng;
	mutex_init(&rng->lock);
	ret = crypto_register_rng(&xtrng_trng_alg);
	if (ret) {
		dev_err(&pdev->dev, "Crypto Random device registration failed: %d\n", ret);
		return ret;
	}
	ret = xtrng_hwrng_register(&rng->trng);
	if (ret) {
		dev_err(&pdev->dev, "HWRNG device registration failed: %d\n", ret);
		goto crypto_rng_free;
	}
	platform_set_drvdata(pdev, rng);

	return 0;

crypto_rng_free:
	crypto_unregister_rng(&xtrng_trng_alg);

	return ret;
}

static void xtrng_remove(struct platform_device *pdev)
{
	struct xilinx_rng *rng;
	u32 zero[TRNG_NUM_INIT_REGS] = { };

	rng = platform_get_drvdata(pdev);
	xtrng_hwrng_unregister(&rng->trng);
	crypto_unregister_rng(&xtrng_trng_alg);
	xtrng_write_multiple_registers(rng->rng_base + TRNG_EXT_SEED_OFFSET, zero,
				       TRNG_NUM_INIT_REGS);
	xtrng_write_multiple_registers(rng->rng_base + TRNG_PER_STRNG_OFFSET, zero,
				       TRNG_NUM_INIT_REGS);
	xtrng_hold_reset(rng->rng_base);
	xilinx_rng_dev = NULL;
}

static const struct of_device_id xtrng_of_match[] = {
	{ .compatible = "xlnx,versal-trng", },
	{},
};

MODULE_DEVICE_TABLE(of, xtrng_of_match);

static struct platform_driver xtrng_driver = {
	.driver = {
		.name = "xlnx,versal-trng",
		.of_match_table	= xtrng_of_match,
	},
	.probe = xtrng_probe,
	.remove = xtrng_remove,
};

module_platform_driver(xtrng_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harsh Jain <h.jain@amd.com>");
MODULE_AUTHOR("Mounika Botcha <mounika.botcha@amd.com>");
MODULE_DESCRIPTION("True Random Number Generator Driver");
