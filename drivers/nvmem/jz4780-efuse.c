// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * JZ4780 EFUSE Memory Support driver
 *
 * Copyright (c) 2017 PrasannaKumar Muralidharan <prasannatsmkumar@gmail.com>
 * Copyright (c) 2020 H. Nikolaus Schaller <hns@goldelico.com>
 */

/*
 * Currently supports JZ4780 efuse which has 8K programmable bit.
 * Efuse is separated into seven segments as below:
 *
 * -----------------------------------------------------------------------
 * | 64 bit | 128 bit | 128 bit | 3520 bit | 8 bit | 2296 bit | 2048 bit |
 * -----------------------------------------------------------------------
 *
 * The rom itself is accessed using a 9 bit address line and an 8 word wide bus
 * which reads/writes based on strobes. The strobe is configured in the config
 * register and is based on number of cycles of the bus clock.
 *
 * Driver supports read only as the writes are done in the Factory.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/timer.h>

#define JZ_EFUCTRL		(0x0)	/* Control Register */
#define JZ_EFUCFG		(0x4)	/* Configure Register*/
#define JZ_EFUSTATE		(0x8)	/* Status Register */
#define JZ_EFUDATA(n)		(0xC + (n) * 4)

/* We read 32 byte chunks to avoid complexity in the driver. */
#define JZ_EFU_READ_SIZE 32

#define EFUCTRL_ADDR_MASK	0x3FF
#define EFUCTRL_ADDR_SHIFT	21
#define EFUCTRL_LEN_MASK	0x1F
#define EFUCTRL_LEN_SHIFT	16
#define EFUCTRL_PG_EN		BIT(15)
#define EFUCTRL_WR_EN		BIT(1)
#define EFUCTRL_RD_EN		BIT(0)

#define EFUCFG_INT_EN		BIT(31)
#define EFUCFG_RD_ADJ_MASK	0xF
#define EFUCFG_RD_ADJ_SHIFT	20
#define EFUCFG_RD_STR_MASK	0xF
#define EFUCFG_RD_STR_SHIFT	16
#define EFUCFG_WR_ADJ_MASK	0xF
#define EFUCFG_WR_ADJ_SHIFT	12
#define EFUCFG_WR_STR_MASK	0xFFF
#define EFUCFG_WR_STR_SHIFT	0

#define EFUSTATE_WR_DONE	BIT(1)
#define EFUSTATE_RD_DONE	BIT(0)

struct jz4780_efuse {
	struct device *dev;
	struct regmap *map;
	struct clk *clk;
};

/* main entry point */
static int jz4780_efuse_read(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	struct jz4780_efuse *efuse = context;

	while (bytes > 0) {
		size_t start = offset & ~(JZ_EFU_READ_SIZE - 1);
		size_t chunk = min(bytes, (start + JZ_EFU_READ_SIZE)
				    - offset);
		char buf[JZ_EFU_READ_SIZE];
		unsigned int tmp;
		u32 ctrl;
		int ret;

		ctrl = (start << EFUCTRL_ADDR_SHIFT)
			| ((JZ_EFU_READ_SIZE - 1) << EFUCTRL_LEN_SHIFT)
			| EFUCTRL_RD_EN;

		regmap_update_bits(efuse->map, JZ_EFUCTRL,
				   (EFUCTRL_ADDR_MASK << EFUCTRL_ADDR_SHIFT) |
				   (EFUCTRL_LEN_MASK << EFUCTRL_LEN_SHIFT) |
				   EFUCTRL_PG_EN | EFUCTRL_WR_EN |
				   EFUCTRL_RD_EN,
				   ctrl);

		ret = regmap_read_poll_timeout(efuse->map, JZ_EFUSTATE,
					       tmp, tmp & EFUSTATE_RD_DONE,
					       1 * MSEC_PER_SEC,
					       50 * MSEC_PER_SEC);
		if (ret < 0) {
			dev_err(efuse->dev, "Time out while reading efuse data");
			return ret;
		}

		ret = regmap_bulk_read(efuse->map, JZ_EFUDATA(0),
				       buf, JZ_EFU_READ_SIZE / sizeof(u32));
		if (ret < 0)
			return ret;

		memcpy(val, &buf[offset - start], chunk);

		val += chunk;
		offset += chunk;
		bytes -= chunk;
	}

	return 0;
}

static struct nvmem_config jz4780_efuse_nvmem_config = {
	.name = "jz4780-efuse",
	.size = 1024,
	.word_size = 1,
	.stride = 1,
	.owner = THIS_MODULE,
	.reg_read = jz4780_efuse_read,
};

static const struct regmap_config jz4780_efuse_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = JZ_EFUDATA(7),
};

static void clk_disable_unprepare_helper(void *clock)
{
	clk_disable_unprepare(clock);
}

static int jz4780_efuse_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;
	struct jz4780_efuse *efuse;
	struct nvmem_config cfg;
	unsigned long clk_rate;
	unsigned long rd_adj;
	unsigned long rd_strobe;
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	int ret;

	efuse = devm_kzalloc(dev, sizeof(*efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	efuse->map = devm_regmap_init_mmio(dev, regs,
					   &jz4780_efuse_regmap_config);
	if (IS_ERR(efuse->map))
		return PTR_ERR(efuse->map);

	efuse->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(efuse->clk))
		return PTR_ERR(efuse->clk);

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev,
				       clk_disable_unprepare_helper,
				       efuse->clk);
	if (ret < 0)
		return ret;

	clk_rate = clk_get_rate(efuse->clk);

	efuse->dev = dev;

	/*
	 * rd_adj and rd_strobe are 4 bit values
	 * conditions:
	 *   bus clk_period * (rd_adj + 1) > 6.5ns
	 *   bus clk_period * (rd_adj + 5 + rd_strobe) > 35ns
	 *   i.e. rd_adj >= 6.5ns / clk_period
	 *   i.e. rd_strobe >= 35 ns / clk_period - 5 - rd_adj + 1
	 * constants:
	 *   1 / 6.5ns == 153846154 Hz
	 *   1 / 35ns == 28571429 Hz
	 */

	rd_adj = clk_rate / 153846154;
	rd_strobe = clk_rate / 28571429 - 5 - rd_adj + 1;

	if (rd_adj > EFUCFG_RD_ADJ_MASK ||
	    rd_strobe > EFUCFG_RD_STR_MASK) {
		dev_err(&pdev->dev, "Cannot set clock configuration\n");
		return -EINVAL;
	}

	regmap_update_bits(efuse->map, JZ_EFUCFG,
			   (EFUCFG_RD_ADJ_MASK << EFUCFG_RD_ADJ_SHIFT) |
			   (EFUCFG_RD_STR_MASK << EFUCFG_RD_STR_SHIFT),
			   (rd_adj << EFUCFG_RD_ADJ_SHIFT) |
			   (rd_strobe << EFUCFG_RD_STR_SHIFT));

	cfg = jz4780_efuse_nvmem_config;
	cfg.dev = &pdev->dev;
	cfg.priv = efuse;

	nvmem = devm_nvmem_register(dev, &cfg);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id jz4780_efuse_match[] = {
	{ .compatible = "ingenic,jz4780-efuse" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, jz4780_efuse_match);

static struct platform_driver jz4780_efuse_driver = {
	.probe  = jz4780_efuse_probe,
	.driver = {
		.name = "jz4780-efuse",
		.of_match_table = jz4780_efuse_match,
	},
};
module_platform_driver(jz4780_efuse_driver);

MODULE_AUTHOR("PrasannaKumar Muralidharan <prasannatsmkumar@gmail.com>");
MODULE_AUTHOR("H. Nikolaus Schaller <hns@goldelico.com>");
MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("Ingenic JZ4780 efuse driver");
MODULE_LICENSE("GPL v2");
