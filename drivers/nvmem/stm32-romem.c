// SPDX-License-Identifier: GPL-2.0
/*
 * STM32 Factory-programmed memory read access driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com> for STMicroelectronics.
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/tee_drv.h>

#include "stm32-bsec-optee-ta.h"

/* BSEC secure service access from non-secure */
#define STM32_SMC_BSEC			0x82001003
#define STM32_SMC_READ_SHADOW		0x01
#define STM32_SMC_PROG_OTP		0x02
#define STM32_SMC_WRITE_SHADOW		0x03
#define STM32_SMC_READ_OTP		0x04

/* shadow registers offset */
#define STM32MP15_BSEC_DATA0		0x200

struct stm32_romem_cfg {
	int size;
	u8 lower;
	bool ta;
};

struct stm32_romem_priv {
	void __iomem *base;
	struct nvmem_config cfg;
	u8 lower;
	struct tee_context *ctx;
};

static int stm32_romem_read(void *context, unsigned int offset, void *buf,
			    size_t bytes)
{
	struct stm32_romem_priv *priv = context;
	u8 *buf8 = buf;
	int i;

	for (i = offset; i < offset + bytes; i++)
		*buf8++ = readb_relaxed(priv->base + i);

	return 0;
}

static int stm32_bsec_smc(u8 op, u32 otp, u32 data, u32 *result)
{
#if IS_ENABLED(CONFIG_HAVE_ARM_SMCCC)
	struct arm_smccc_res res;

	arm_smccc_smc(STM32_SMC_BSEC, op, otp, data, 0, 0, 0, 0, &res);
	if (res.a0)
		return -EIO;

	if (result)
		*result = (u32)res.a1;

	return 0;
#else
	return -ENXIO;
#endif
}

static int stm32_bsec_read(void *context, unsigned int offset, void *buf,
			   size_t bytes)
{
	struct stm32_romem_priv *priv = context;
	struct device *dev = priv->cfg.dev;
	u32 roffset, rbytes, val;
	u8 *buf8 = buf, *val8 = (u8 *)&val;
	int i, j = 0, ret, skip_bytes, size;

	/* Round unaligned access to 32-bits */
	roffset = rounddown(offset, 4);
	skip_bytes = offset & 0x3;
	rbytes = roundup(bytes + skip_bytes, 4);

	if (roffset + rbytes > priv->cfg.size)
		return -EINVAL;

	for (i = roffset; (i < roffset + rbytes); i += 4) {
		u32 otp = i >> 2;

		if (otp < priv->lower) {
			/* read lower data from shadow registers */
			val = readl_relaxed(
				priv->base + STM32MP15_BSEC_DATA0 + i);
		} else {
			ret = stm32_bsec_smc(STM32_SMC_READ_SHADOW, otp, 0,
					     &val);
			if (ret) {
				dev_err(dev, "Can't read data%d (%d)\n", otp,
					ret);
				return ret;
			}
		}
		/* skip first bytes in case of unaligned read */
		if (skip_bytes)
			size = min(bytes, (size_t)(4 - skip_bytes));
		else
			size = min(bytes, (size_t)4);
		memcpy(&buf8[j], &val8[skip_bytes], size);
		bytes -= size;
		j += size;
		skip_bytes = 0;
	}

	return 0;
}

static int stm32_bsec_write(void *context, unsigned int offset, void *buf,
			    size_t bytes)
{
	struct stm32_romem_priv *priv = context;
	struct device *dev = priv->cfg.dev;
	u32 *buf32 = buf;
	int ret, i;

	/* Allow only writing complete 32-bits aligned words */
	if ((bytes % 4) || (offset % 4))
		return -EINVAL;

	for (i = offset; i < offset + bytes; i += 4) {
		ret = stm32_bsec_smc(STM32_SMC_PROG_OTP, i >> 2, *buf32++,
				     NULL);
		if (ret) {
			dev_err(dev, "Can't write data%d (%d)\n", i >> 2, ret);
			return ret;
		}
	}

	if (offset + bytes >= priv->lower * 4)
		dev_warn(dev, "Update of upper OTPs with ECC protection (word programming, only once)\n");

	return 0;
}

static int stm32_bsec_pta_read(void *context, unsigned int offset, void *buf,
			       size_t bytes)
{
	struct stm32_romem_priv *priv = context;

	return stm32_bsec_optee_ta_read(priv->ctx, offset, buf, bytes);
}

static int stm32_bsec_pta_write(void *context, unsigned int offset, void *buf,
				size_t bytes)
{
	struct stm32_romem_priv *priv = context;

	return stm32_bsec_optee_ta_write(priv->ctx, priv->lower, offset, buf, bytes);
}

static bool stm32_bsec_smc_check(void)
{
	u32 val;
	int ret;

	/* check that the OP-TEE support the BSEC SMC (legacy mode) */
	ret = stm32_bsec_smc(STM32_SMC_READ_SHADOW, 0, 0, &val);

	return !ret;
}

static bool optee_presence_check(void)
{
	struct device_node *np;
	bool tee_detected = false;

	/* check that the OP-TEE node is present and available. */
	np = of_find_compatible_node(NULL, NULL, "linaro,optee-tz");
	if (np && of_device_is_available(np))
		tee_detected = true;
	of_node_put(np);

	return tee_detected;
}

static int stm32_romem_probe(struct platform_device *pdev)
{
	const struct stm32_romem_cfg *cfg;
	struct device *dev = &pdev->dev;
	struct stm32_romem_priv *priv;
	struct resource *res;
	int rc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->cfg.name = "stm32-romem";
	priv->cfg.word_size = 1;
	priv->cfg.stride = 1;
	priv->cfg.dev = dev;
	priv->cfg.priv = priv;
	priv->cfg.owner = THIS_MODULE;
	priv->cfg.type = NVMEM_TYPE_OTP;
	priv->cfg.add_legacy_fixed_of_cells = true;

	priv->lower = 0;

	cfg = (const struct stm32_romem_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;
	if (!cfg) {
		priv->cfg.read_only = true;
		priv->cfg.size = resource_size(res);
		priv->cfg.reg_read = stm32_romem_read;
	} else {
		priv->cfg.size = cfg->size;
		priv->lower = cfg->lower;
		if (cfg->ta || optee_presence_check()) {
			rc = stm32_bsec_optee_ta_open(&priv->ctx);
			if (rc) {
				/* wait for OP-TEE client driver to be up and ready */
				if (rc == -EPROBE_DEFER)
					return -EPROBE_DEFER;
				/* BSEC PTA is required or SMC not supported */
				if (cfg->ta || !stm32_bsec_smc_check())
					return rc;
			}
		}
		if (priv->ctx) {
			rc = devm_add_action_or_reset(dev, stm32_bsec_optee_ta_close, priv->ctx);
			if (rc) {
				dev_err(dev, "devm_add_action_or_reset() failed (%d)\n", rc);
				return rc;
			}
			priv->cfg.reg_read = stm32_bsec_pta_read;
			priv->cfg.reg_write = stm32_bsec_pta_write;
		} else {
			priv->cfg.reg_read = stm32_bsec_read;
			priv->cfg.reg_write = stm32_bsec_write;
		}
	}

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &priv->cfg));
}

/*
 * STM32MP15/13 BSEC OTP regions: 4096 OTP bits (with 3072 effective bits)
 * => 96 x 32-bits data words
 * - Lower: 1K bits, 2:1 redundancy, incremental bit programming
 *   => 32 (x 32-bits) lower shadow registers = words 0 to 31
 * - Upper: 2K bits, ECC protection, word programming only
 *   => 64 (x 32-bits) = words 32 to 95
 */
static const struct stm32_romem_cfg stm32mp15_bsec_cfg = {
	.size = 384,
	.lower = 32,
	.ta = false,
};

static const struct stm32_romem_cfg stm32mp13_bsec_cfg = {
	.size = 384,
	.lower = 32,
	.ta = true,
};

static const struct of_device_id stm32_romem_of_match[] __maybe_unused = {
	{ .compatible = "st,stm32f4-otp", }, {
		.compatible = "st,stm32mp15-bsec",
		.data = (void *)&stm32mp15_bsec_cfg,
	}, {
		.compatible = "st,stm32mp13-bsec",
		.data = (void *)&stm32mp13_bsec_cfg,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, stm32_romem_of_match);

static struct platform_driver stm32_romem_driver = {
	.probe = stm32_romem_probe,
	.driver = {
		.name = "stm32-romem",
		.of_match_table = of_match_ptr(stm32_romem_of_match),
	},
};
module_platform_driver(stm32_romem_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 RO-MEM");
MODULE_ALIAS("platform:nvmem-stm32-romem");
MODULE_LICENSE("GPL v2");
