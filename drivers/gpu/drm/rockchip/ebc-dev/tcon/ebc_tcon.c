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

#define REG_LOAD_GLOBAL_EN	0x1

/* ebc register define */
#define EBC_DSP_START		0x0000 //Frame statrt register
#define EBC_EPD_CTRL			0x0004 //EPD control register
#define EBC_DSP_CTRL			0x0008 //Display control register
#define EBC_DSP_HTIMING0		0x000c //H-Timing setting register0
#define EBC_DSP_HTIMING1		0x0010 //H-Timing setting register1
#define EBC_DSP_VTIMING0		0x0014 //V-Timing setting register0
#define EBC_DSP_VTIMING1		0x0018 //V-Timing setting register1
#define EBC_DSP_ACT_INFO		0x001c //ACTIVE width/height
#define EBC_WIN_CTRL			0x0020 //Window ctrl
#define EBC_WIN_MST0			0x0024 //Current win memory start
#define EBC_WIN_MST1			0x0028 //Next win memory start
#define EBC_WIN_VIR			0x002c //Window vir width/height
#define EBC_WIN_ACT			0x0030 //Window act width/height
#define EBC_WIN_DSP			0x0034 //Window dsp width/height
#define EBC_WIN_DSP_ST		0x0038 //Window display start piont
#define EBC_INT_STATUS		0x003c //Interrupt register
#define EBC_VCOM0			0x0040 //VCOM setting register0
#define EBC_VCOM1			0x0044 //VCOM setting register1
#define EBC_VCOM2			0x0048 //VCOM setting register2
#define EBC_VCOM3			0x004c //VCOM setting register3
#define EBC_CONFIG_DONE		0x0050 //Config done register
#define EBC_VNUM				0x0054 //Line flag num
#define EBC_WIN_MST2			0x0058 //Framecount memory start
#define EBC_LUT_DATA_ADDR	0x1000 //lut data address

#define DSP_HTOTAL(x)			UPDATE(x, 27, 16)
#define DSP_HS_END(x)			UPDATE(x, 7, 0)
#define DSP_HACT_END(x)		UPDATE(x, 26, 16)
#define DSP_HACT_ST(x)		UPDATE(x, 7, 0)
#define DSP_VTOTAL(x)			UPDATE(x, 26, 16)
#define DSP_VS_END(x)			UPDATE(x, 7, 0)
#define DSP_VACT_END(x)		UPDATE(x, 26, 16)
#define DSP_VACT_ST(x)		UPDATE(x, 7, 0)
#define DSP_HEIGHT(x)			UPDATE(x, 26, 16)
#define DSP_WIDTH(x)			UPDATE(x, 11, 0)

#define WIN2_FIFO_ALMOST_FULL_LEVEL(x)	UPDATE(x, 27, 19)
#define WIN_EN(x)				UPDATE(x, 18, 18)
#define BURST_REG(x)			UPDATE(x, 12, 10)
#define WIN_FIFO_ALMOST_FULL_LEVEL(x)	UPDATE(x, 9, 2)
#define WIN_FMT(x)			UPDATE(x, 1, 0)

#define WIN_VIR_HEIGHT(x)		UPDATE(x, 31, 16)
#define WIN_VIR_WIDTH(x)		UPDATE(x, 15, 0)
#define WIN_ACT_HEIGHT(x)	UPDATE(x, 26, 16)
#define WIN_ACT_WIDTH(x)		UPDATE(x, 11, 0)
#define WIN_DSP_HEIGHT(x)	UPDATE(x, 26, 16)
#define WIN_DSP_WIDTH(x)		UPDATE(x, 11, 0)
#define WIN_DSP_YST(x)		UPDATE(x, 26, 16)
#define WIN_DSP_XST(x)		UPDATE(x, 11, 0)

#define DSP_OUT_LOW			BIT(31)
#define DSP_EINK_MODE(x)		UPDATE(x, 13, 13)
#define DSP_EINK_MODE_MASK	BIT(13)
#define DSP_SDCE_WIDTH(x)	UPDATE(x, 25, 16)
#define DSP_FRM_TOTAL(x)		UPDATE(x, 9, 2)
#define DSP_FRM_TOTAL_MASK	GENMASK(9, 2)
#define DSP_FRM_START		BIT(0)
#define DSP_FRM_START_MASK	BIT(0)
#define SW_BURST_CTRL		BIT(12)

#define EINK_MODE_SWAP(x)	UPDATE(x, 31, 31)
#define EINK_MODE_FRM_SEL(x)	UPDATE(x, 30, 30)
#define DSP_GD_END(x)			UPDATE(x, 26, 16)
#define DSP_GD_ST(x)			UPDATE(x, 15, 8)
#define DSP_THREE_WIN_MODE(x)		UPDATE(x, 7, 7)
#define THREE_WIN_MODE_MASK		BIT(7)
#define DSP_SDDW_MODE(x)	UPDATE(x, 6, 6)
#define EPD_AUO(x)			UPDATE(x, 5, 5)
#define EPD_PWR(x)			UPDATE(x, 4, 2)
#define EPD_GDRL(x)			UPDATE(x, 1, 1)
#define EPD_SDSHR(x)			UPDATE(x, 0, 0)

#define DSP_SWAP_MODE(x)	UPDATE(x, 31, 30)
#define DSP_SDCLK_DIV(x)		UPDATE(x, 19, 16)
#define DSP_SDCLK_DIV_MASK	GENMASK(19, 16)
#define DSP_VCOM_MODE(x)		UPDATE(x, 27, 27)

#define DSP_UPDATE_MODE(x)	UPDATE(x, 29, 29)
#define DSP_DISPLAY_MODE(x)	UPDATE(x, 28, 28)
#define UPDATE_MODE_MASK	BIT(29)
#define DISPLAY_MODE_MASK	BIT(28)

#define DSP_FRM_INT_NUM(x)	UPDATE(x, 19, 12)
#define FRM_END_INT			BIT(0)
#define DSP_END_INT			BIT(1)
#define DSP_FRM_INT			BIT(2)
#define LINE_FLAG_INT			BIT(3)
#define FRM_END_INT_MASK	BIT(4)
#define DSP_END_INT_MASK	BIT(5)
#define DSP_FRM_INT_MASK	BIT(6)
#define LINE_FLAG_INT_MASK	BIT(7)
#define FRM_END_INT_CLR		BIT(8)
#define DSP_END_INT_CLR		BIT(9)
#define DSP_FRM_INT_CLR		BIT(10)
#define LINE_FLAG_INT_CLR	BIT(11)

enum ebc_win_data_fmt {
	Y_DATA_4BPP = 0,
	Y_DATA_8BPP = 1,
	RGB888 = 2,
	RGB565 = 3,
};

static inline void tcon_write(struct ebc_tcon *tcon, unsigned int reg,
			      unsigned int value)
{
	regmap_write(tcon->regmap_base, reg, value);
}

static inline unsigned int tcon_read(struct ebc_tcon *tcon, unsigned int reg)
{
	unsigned int value;

	regmap_read(tcon->regmap_base, reg, &value);

	return value;
}

static inline void tcon_update_bits(struct ebc_tcon *tcon, unsigned int reg,
				    unsigned int mask, unsigned int val)
{
	regmap_update_bits(tcon->regmap_base, reg, mask, val);
}

static inline void tcon_cfg_done(struct ebc_tcon *tcon)
{
	regmap_write(tcon->regmap_base, EBC_CONFIG_DONE, REG_LOAD_GLOBAL_EN);
}

static int tcon_enable(struct ebc_tcon *tcon, struct ebc_panel *panel)
{
	clk_prepare_enable(tcon->hclk);
	clk_prepare_enable(tcon->dclk);
	pm_runtime_get_sync(tcon->dev);

	/* panel timing and win info config */
	tcon_write(tcon, EBC_DSP_HTIMING0,
				DSP_HTOTAL(panel->lsl + panel->lbl + panel->ldl + panel->lel) | DSP_HS_END(panel->lsl + 2));
	tcon_write(tcon, EBC_DSP_HTIMING1,
				DSP_HACT_END(panel->lsl + panel->lbl + panel->ldl) | DSP_HACT_ST(panel->lsl + panel->lbl - 1));
	tcon_write(tcon, EBC_DSP_VTIMING0,
				DSP_VTOTAL(panel->fsl + panel->fbl + panel->fdl + panel->fel) | DSP_VS_END(panel->fsl));
	tcon_write(tcon, EBC_DSP_VTIMING1,
				DSP_VACT_END(panel->fsl + panel->fbl + panel->fdl) | DSP_VACT_ST(panel->fsl + panel->fbl));
	tcon_write(tcon, EBC_DSP_ACT_INFO, DSP_HEIGHT(panel->height) | DSP_WIDTH(panel->width));
	tcon_write(tcon, EBC_WIN_VIR, WIN_VIR_HEIGHT(panel->vir_height) | WIN_VIR_WIDTH(panel->vir_width));
	tcon_write(tcon, EBC_WIN_ACT, WIN_ACT_HEIGHT(panel->height) | WIN_ACT_WIDTH(panel->width));
	tcon_write(tcon, EBC_WIN_DSP, WIN_DSP_HEIGHT(panel->height) | WIN_DSP_WIDTH(panel->width));
	tcon_write(tcon, EBC_WIN_DSP_ST, WIN_DSP_YST(panel->fsl + panel->fbl) | WIN_DSP_XST(panel->lsl + panel->lbl));

	/* win2 fifo is 512x128, win fifo is 256x128, we set fifo almost value (fifo_size - 16)
	*  burst_reg = 7 mean ahb burst is incr16
	*/
	tcon_write(tcon, EBC_WIN_CTRL, WIN2_FIFO_ALMOST_FULL_LEVEL(496) | WIN_EN(1)
							| BURST_REG(7) | WIN_FIFO_ALMOST_FULL_LEVEL(240) | WIN_FMT(Y_DATA_4BPP));

	/*
	 * EBC_EPD_CTRL info:
	 * DSP_GD_ST: GCLK rising edge point(SCLK), which count from the rasing edge of hsync(spec is wrong, count
	 * from rasing edge of hsync, not falling edge of hsync)
	 * DSP_GD_END : GCLK falling edge point(SCLK), which count from the rasing edge of hsync
	 * DSP_THREE_WIN_MODE: 0: lut mode or direct mode; 1: three win mode
	 * DSP_SDDW_MODE: 0: 8 bit data output; 1: 16 bit data output
	 * EPD_AUO: 0: EINK; 1:AUO
	 * EPD_GDRL: gate scanning direction: 1: button to top 0: top to button
	 * EPD_SDSHR: source scanning direction 1: right to left 0: left to right
	 */
	tcon_write(tcon, EBC_EPD_CTRL, EINK_MODE_SWAP(1)
				| DSP_GD_ST(panel->lsl + panel->gdck_sta)
				| DSP_GD_END(panel->lsl + panel->gdck_sta + panel->lgonl)
				| DSP_THREE_WIN_MODE(0)
				| DSP_SDDW_MODE(!!panel->panel_16bit)
				| EPD_AUO(0)
				| EPD_GDRL(1)
				| EPD_SDSHR(1));
	tcon_write(tcon, EBC_DSP_START, DSP_SDCE_WIDTH(panel->ldl) | SW_BURST_CTRL);
	tcon_write(tcon, EBC_DSP_CTRL,
				DSP_SWAP_MODE(panel->panel_16bit ? 2 : 3) | DSP_VCOM_MODE(1) | DSP_SDCLK_DIV(panel->panel_16bit ? 7 : 3));
	tcon_cfg_done(tcon);

	enable_irq(tcon->irq);

	return 0;
}

static void tcon_disable(struct ebc_tcon *tcon)
{
	disable_irq(tcon->irq);
	/* output low */
	tcon_update_bits(tcon, EBC_DSP_START, DSP_OUT_LOW | DSP_FRM_START_MASK, DSP_OUT_LOW);

	pm_runtime_put_sync(tcon->dev);
	clk_disable_unprepare(tcon->dclk);
	clk_disable_unprepare(tcon->hclk);
}

static void tcon_dsp_mode_set(struct ebc_tcon *tcon, int update_mode, int display_mode, int three_win_mode, int eink_mode)
{
	tcon_update_bits(tcon, EBC_DSP_CTRL, UPDATE_MODE_MASK | DISPLAY_MODE_MASK,
						DSP_UPDATE_MODE(!!update_mode) | DSP_DISPLAY_MODE(!!display_mode));
	tcon_update_bits(tcon, EBC_EPD_CTRL, THREE_WIN_MODE_MASK,
						DSP_THREE_WIN_MODE(!!three_win_mode));
	/* always set frm start bit 0 before real frame start */
	tcon_update_bits(tcon, EBC_DSP_START, DSP_EINK_MODE_MASK | DSP_FRM_START_MASK, DSP_EINK_MODE(!!eink_mode));
	tcon_cfg_done(tcon);
}

static void tcon_image_addr_set(struct ebc_tcon *tcon, u32 pre_image_addr, u32 cur_image_addr)
{
	tcon_write(tcon, EBC_WIN_MST0, pre_image_addr);
	tcon_write(tcon, EBC_WIN_MST1, cur_image_addr);
	tcon_cfg_done(tcon);
}

static void tcon_frame_addr_set(struct ebc_tcon *tcon, u32 frame_addr)
{
	tcon_write(tcon, EBC_WIN_MST2, frame_addr);
	tcon_cfg_done(tcon);
}

static int tcon_lut_data_set(struct ebc_tcon *tcon, unsigned int *lut_data, int frame_count, int lut_32)
{
	int i;
	int lut_size;

	if ((!lut_32 && frame_count > 256) || (lut_32 && frame_count > 64)) {
		dev_err(tcon->dev, "frame count over flow\n");
		return -1;
	}

	if (lut_32)
		lut_size = frame_count * 64;
	else
		lut_size = frame_count * 16;

	for (i = 0; i < lut_size; i++) {
		tcon_write(tcon, EBC_LUT_DATA_ADDR + (i * 4), lut_data[i]);
	}
	tcon_cfg_done(tcon);

	return 0;
}

static void tcon_frame_start(struct ebc_tcon *tcon, int frame_total)
{
	tcon_write(tcon, EBC_INT_STATUS, LINE_FLAG_INT_MASK | DSP_FRM_INT_MASK | FRM_END_INT_MASK);
	/* always set frm start bit 0 before real frame start */
	tcon_update_bits(tcon, EBC_DSP_START, DSP_FRM_TOTAL_MASK | DSP_FRM_START_MASK, DSP_FRM_TOTAL(frame_total - 1));
	tcon_cfg_done(tcon);

	tcon_update_bits(tcon, EBC_DSP_START, DSP_FRM_START_MASK, DSP_FRM_START);
}

static irqreturn_t tcon_irq_hanlder(int irq, void *dev_id)
{
	struct ebc_tcon *tcon = (struct ebc_tcon *)dev_id;
	u32 intr_status;

	intr_status = tcon_read(tcon, EBC_INT_STATUS);

	if (intr_status & DSP_END_INT) {
		tcon_update_bits(tcon, EBC_INT_STATUS, DSP_END_INT_CLR, DSP_END_INT_CLR);

		if (tcon->dsp_end_callback)
			tcon->dsp_end_callback();
	}

	return IRQ_HANDLED;
}

static bool tcon_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EBC_DSP_START:
	case EBC_EPD_CTRL:
	case EBC_DSP_CTRL:
		return false;
	}

	return true;
}

static struct regmap_config ebc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tcon_is_volatile_reg,
};

static int ebc_tcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ebc_tcon *tcon;
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
	ebc_regmap_config.max_register = resource_size(res) - 4;
	ebc_regmap_config.name = "rockchip,ebc_tcon";
	tcon->regmap_base = devm_regmap_init_mmio(dev, tcon->regs, &ebc_regmap_config);
	if (IS_ERR(tcon->regmap_base))
		return PTR_ERR(tcon->regmap_base);

	tcon->hclk = devm_clk_get(dev, "hclk");
	if (IS_ERR(tcon->hclk)) {
		ret = PTR_ERR(tcon->hclk);
		dev_err(dev, "failed to get hclk clock: %d\n", ret);
		return ret;
	}

	tcon->dclk = devm_clk_get(dev, "dclk");
	if (IS_ERR(tcon->dclk)) {
		ret = PTR_ERR(tcon->dclk);
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
	tcon->dsp_mode_set = tcon_dsp_mode_set;
	tcon->image_addr_set = tcon_image_addr_set;
	tcon->frame_addr_set = tcon_frame_addr_set;
	tcon->lut_data_set = tcon_lut_data_set;
	tcon->frame_start = tcon_frame_start;
	platform_set_drvdata(pdev, tcon);

	pm_runtime_enable(dev);

	return 0;
}

static int ebc_tcon_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id ebc_tcon_of_match[] = {
	{ .compatible = "rockchip,rk3568-ebc-tcon" },
	{}
};
MODULE_DEVICE_TABLE(of, ebc_tcon_of_match);

static struct platform_driver ebc_tcon_driver = {
	.driver = {
		.name = "rk-ebc-tcon",
		.of_match_table = ebc_tcon_of_match,
	},
	.probe = ebc_tcon_probe,
	.remove = ebc_tcon_remove,
};
module_platform_driver(ebc_tcon_driver);

MODULE_AUTHOR("Zorro Liu <zorro.liu@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP EBC tcon driver");
MODULE_LICENSE("GPL v2");
