// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include "ebc_tcon.h"

#define HIWORD_UPDATE(x, l, h)	(((x) << (l)) | (GENMASK(h, l) << 16))
#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))

/* eink register define */
#define EINK_IP_ENABLE					0x00
#define EINK_SFT_UPDATE					0x04
#define EINK_SIT_UPDATE					0x08
#define EINK_PRE_IMAGE_BUF_ADDR			0x0c
#define EINK_CUR_IMAGE_BUF_ADDR			0x10
#define EINK_IMAGE_PROCESS_BUF_ADDR	0x14
#define EINK_LINE_DATA_ADDR_OFFSET		0x18
#define EINK_IMAGE_WIDTH					0x1c
#define EINK_IMAGE_HEIGHT				0x20
#define EINK_DATA_FORMAT				0x24
#define EINK_IP_STATUS					0x28
#define EINK_IP_VERSION					0x2c
#define EINK_IP_CLR_INT					0x30
#define EINK_INT_SETTING0					0x34
#define EINK_INT_SETTING1					0x38
#define EINK_INT_SETTING2					0x3c
#define EINK_INT_SETTING3					0x40
#define EINK_INT_SETTING4					0x44
#define EINK_INT_SETTING5					0x48
#define EINK_INT_SETTING6					0x4c
#define EINK_INT_SETTING7					0x50
#define EINK_WF_SETTING0					0x54
#define EINK_WF_SETTING1					0x58
#define EINK_WF_SETTING2					0x5c
#define EINK_WF_SETTING3					0x60
#define EINK_WF_SETTING4					0x64
#define EINK_WF_SETTING5					0x68
#define EINK_WF_SETTING6					0x6c
#define EINK_WF_SETTING7					0x70

struct eink_reg_data {
	int addr;
	int value;
};

static const struct eink_reg_data PANEL_1200x825_INIT[] = {
	{ EINK_SFT_UPDATE, 0x00030001 },
	{ EINK_SIT_UPDATE, 0x00050000 },
	{ EINK_LINE_DATA_ADDR_OFFSET, 0x000004b0 }, //width
	{ EINK_IMAGE_WIDTH, 0x000004af }, //width - 1
	{ EINK_IMAGE_HEIGHT, 0x00000338 }, //height - 1

	{ EINK_INT_SETTING0, 0x0e56676f },
	{ EINK_INT_SETTING1, 0x40674408 },
	{ EINK_INT_SETTING2, 0xd7eb7743 },
	{ EINK_INT_SETTING3, 0x19414d35 },
	{ EINK_INT_SETTING4, 0x12561c00 },
	{ EINK_INT_SETTING5, 0x05552e0a },
	{ EINK_INT_SETTING6, 0x4a400e10 },
	{ EINK_INT_SETTING7, 0x15496e2b },

	{ EINK_WF_SETTING0, 0xb3f33a52 },
	{ EINK_WF_SETTING1, 0x2042b122 },
	{ EINK_WF_SETTING2, 0xbdb0f3be },
	{ EINK_WF_SETTING3, 0xe289a0ca },
	{ EINK_WF_SETTING4, 0xb0d3b2c8 },
	{ EINK_WF_SETTING5, 0x3a32ab20 },
	{ EINK_WF_SETTING6, 0xa69a634c },
	{ EINK_WF_SETTING7, 0xd87af2c0 },
};

static const struct eink_reg_data PANEL_1872x1404_INIT[] = {
	{ EINK_SFT_UPDATE, 0x00030001 },
	{ EINK_SIT_UPDATE, 0x00050000 },
	{ EINK_LINE_DATA_ADDR_OFFSET, 0x00000750 }, //width
	{ EINK_IMAGE_WIDTH, 0x0000074f }, //width - 1
	{ EINK_IMAGE_HEIGHT, 0x0000057c }, //height -1

	{ EINK_INT_SETTING0, 0x0e56676f },
	{ EINK_INT_SETTING1, 0x40674408 },
	{ EINK_INT_SETTING2, 0xb14a4643 },
	{ EINK_INT_SETTING3, 0x19414d35 },
	{ EINK_INT_SETTING4, 0x12561c00 },
	{ EINK_INT_SETTING5, 0x05552e0a },
	{ EINK_INT_SETTING6, 0x4a400e10 },
	{ EINK_INT_SETTING7, 0x15496e2b },

	{ EINK_WF_SETTING0, 0xb3f33a52 },
	{ EINK_WF_SETTING1, 0x2042b122 },
	{ EINK_WF_SETTING2, 0x34b0708b },
	{ EINK_WF_SETTING3, 0xe289a0ca },
	{ EINK_WF_SETTING4, 0xb0d3b2c8 },
	{ EINK_WF_SETTING5, 0x3a32ab20 },
	{ EINK_WF_SETTING6, 0x2f9ae079 },
	{ EINK_WF_SETTING7, 0xd87af2c0 },
};

static inline void tcon_write(struct eink_tcon *tcon, unsigned int reg,
			      unsigned int value)
{
	regmap_write(tcon->regmap_base, reg, value);
}

static inline unsigned int tcon_read(struct eink_tcon *tcon, unsigned int reg)
{
	unsigned int value;

	regmap_read(tcon->regmap_base, reg, &value);

	return value;
}

static inline void tcon_update_bits(struct eink_tcon *tcon, unsigned int reg,
				    unsigned int mask, unsigned int val)
{
	regmap_update_bits(tcon->regmap_base, reg, mask, val);
}

static int tcon_enable(struct eink_tcon *tcon, struct ebc_panel *panel)
{
	int reg_num = 0;
	int i;
	const struct eink_reg_data *pre_init_reg;

	clk_prepare_enable(tcon->pclk);
	clk_prepare_enable(tcon->hclk);
	pm_runtime_get_sync(tcon->dev);

	if ((panel->width == 1872) && (panel->height == 1404)) {
		pre_init_reg = PANEL_1872x1404_INIT;
		reg_num = ARRAY_SIZE(PANEL_1872x1404_INIT);
	} else if ((panel->width == 1200) && (panel->height == 825)) {
		pre_init_reg = PANEL_1200x825_INIT;
		reg_num = ARRAY_SIZE(PANEL_1200x825_INIT);
	} else {
		pre_init_reg = PANEL_1872x1404_INIT;
		reg_num = ARRAY_SIZE(PANEL_1872x1404_INIT);
	}
	for (i = 0; i < reg_num; i++) {
		tcon_write(tcon, pre_init_reg[i].addr, pre_init_reg[i].value);
	}

	enable_irq(tcon->irq);

	return 0;
}

static void tcon_disable(struct eink_tcon *tcon)
{
	disable_irq(tcon->irq);

	pm_runtime_put_sync(tcon->dev);
	clk_disable_unprepare(tcon->hclk);
	clk_disable_unprepare(tcon->pclk);
}

static void tcon_image_addr_set(struct eink_tcon *tcon, u32 pre_image_buf_addr,
				u32 cur_image_buf_addr, u32 image_process_buf_addr)
{
	tcon_write(tcon, EINK_PRE_IMAGE_BUF_ADDR, pre_image_buf_addr);
	tcon_write(tcon, EINK_CUR_IMAGE_BUF_ADDR, cur_image_buf_addr);
	tcon_write(tcon, EINK_IMAGE_PROCESS_BUF_ADDR, image_process_buf_addr);
}

static void tcon_frame_start(struct eink_tcon *tcon)
{
	tcon_write(tcon, EINK_IP_ENABLE, 1);
}

static irqreturn_t tcon_irq_hanlder(int irq, void *dev_id)
{
	struct eink_tcon *tcon = (struct eink_tcon *)dev_id;
	u32 intr_status;

	intr_status = tcon_read(tcon, EINK_IP_STATUS);

	if (intr_status & 0x1) {
		tcon_update_bits(tcon, EINK_IP_CLR_INT, 0x1, 0x1);

		if (tcon->dsp_end_callback)
			tcon->dsp_end_callback();
	}

	return IRQ_HANDLED;
}

static struct regmap_config eink_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int eink_tcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct eink_tcon *tcon;
	struct resource *res;
	int ret;

	tcon = devm_kzalloc(dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tcon->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(tcon->regs))
		return PTR_ERR(tcon->regs);

	tcon->len = resource_size(res);
	eink_regmap_config.max_register = resource_size(res) - 4;
	eink_regmap_config.name = "rockchip,eink_tcon";
	tcon->regmap_base = devm_regmap_init_mmio(dev, tcon->regs, &eink_regmap_config);
	if (IS_ERR(tcon->regmap_base))
		return PTR_ERR(tcon->regmap_base);

	tcon->hclk = devm_clk_get(dev, "hclk");
	if (IS_ERR(tcon->hclk)) {
		ret = PTR_ERR(tcon->hclk);
		dev_err(dev, "failed to get hclk clock: %d\n", ret);
		return ret;
	}

	tcon->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(tcon->pclk)) {
		ret = PTR_ERR(tcon->pclk);
		dev_err(dev, "failed to get dclk clock: %d\n", ret);
		return ret;
	}

	tcon->irq = platform_get_irq(pdev, 0);
	if (tcon->irq < 0) {
		dev_err(dev, "No IRQ resource!\n");
		return tcon->irq;
	}

	irq_set_status_flags(tcon->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(dev, tcon->irq, tcon_irq_hanlder,
			       0, dev_name(dev), tcon);
	if (ret < 0) {
		dev_err(dev, "failed to requeset irq: %d\n", ret);
		return ret;
	}

	tcon->dev = dev;
	tcon->enable = tcon_enable;
	tcon->disable = tcon_disable;
	tcon->image_addr_set = tcon_image_addr_set;
	tcon->frame_start = tcon_frame_start;
	platform_set_drvdata(pdev, tcon);

	pm_runtime_enable(dev);

	return 0;
}

static int eink_tcon_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id eink_tcon_of_match[] = {
	{ .compatible = "rockchip,rk3568-eink-tcon" },
	{}
};
MODULE_DEVICE_TABLE(of, eink_tcon_of_match);

static struct platform_driver eink_tcon_driver = {
	.driver = {
		.name = "rk-eink-tcon",
		.of_match_table = eink_tcon_of_match,
	},
	.probe = eink_tcon_probe,
	.remove = eink_tcon_remove,
};
module_platform_driver(eink_tcon_driver);

MODULE_AUTHOR("Zorro Liu <zorro.liu@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP EINK tcon driver");
MODULE_LICENSE("GPL v2");
