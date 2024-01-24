// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dcss-dev.h"

#define DCSS_DTG_TC_CONTROL_STATUS			0x00
#define   CH3_EN					BIT(0)
#define   CH2_EN					BIT(1)
#define   CH1_EN					BIT(2)
#define   OVL_DATA_MODE					BIT(3)
#define   BLENDER_VIDEO_ALPHA_SEL			BIT(7)
#define   DTG_START					BIT(8)
#define   DBY_MODE_EN					BIT(9)
#define   CH1_ALPHA_SEL					BIT(10)
#define   CSS_PIX_COMP_SWAP_POS				12
#define   CSS_PIX_COMP_SWAP_MASK			GENMASK(14, 12)
#define   DEFAULT_FG_ALPHA_POS				24
#define   DEFAULT_FG_ALPHA_MASK				GENMASK(31, 24)
#define DCSS_DTG_TC_DTG					0x04
#define DCSS_DTG_TC_DISP_TOP				0x08
#define DCSS_DTG_TC_DISP_BOT				0x0C
#define DCSS_DTG_TC_CH1_TOP				0x10
#define DCSS_DTG_TC_CH1_BOT				0x14
#define DCSS_DTG_TC_CH2_TOP				0x18
#define DCSS_DTG_TC_CH2_BOT				0x1C
#define DCSS_DTG_TC_CH3_TOP				0x20
#define DCSS_DTG_TC_CH3_BOT				0x24
#define   TC_X_POS					0
#define   TC_X_MASK					GENMASK(12, 0)
#define   TC_Y_POS					16
#define   TC_Y_MASK					GENMASK(28, 16)
#define DCSS_DTG_TC_CTXLD				0x28
#define   TC_CTXLD_DB_Y_POS				0
#define   TC_CTXLD_DB_Y_MASK				GENMASK(12, 0)
#define   TC_CTXLD_SB_Y_POS				16
#define   TC_CTXLD_SB_Y_MASK				GENMASK(28, 16)
#define DCSS_DTG_TC_CH1_BKRND				0x2C
#define DCSS_DTG_TC_CH2_BKRND				0x30
#define   BKRND_R_Y_COMP_POS				20
#define   BKRND_R_Y_COMP_MASK				GENMASK(29, 20)
#define   BKRND_G_U_COMP_POS				10
#define   BKRND_G_U_COMP_MASK				GENMASK(19, 10)
#define   BKRND_B_V_COMP_POS				0
#define   BKRND_B_V_COMP_MASK				GENMASK(9, 0)
#define DCSS_DTG_BLENDER_DBY_RANGEINV			0x38
#define DCSS_DTG_BLENDER_DBY_RANGEMIN			0x3C
#define DCSS_DTG_BLENDER_DBY_BDP			0x40
#define DCSS_DTG_BLENDER_BKRND_I			0x44
#define DCSS_DTG_BLENDER_BKRND_P			0x48
#define DCSS_DTG_BLENDER_BKRND_T			0x4C
#define DCSS_DTG_LINE0_INT				0x50
#define DCSS_DTG_LINE1_INT				0x54
#define DCSS_DTG_BG_ALPHA_DEFAULT			0x58
#define DCSS_DTG_INT_STATUS				0x5C
#define DCSS_DTG_INT_CONTROL				0x60
#define DCSS_DTG_TC_CH3_BKRND				0x64
#define DCSS_DTG_INT_MASK				0x68
#define   LINE0_IRQ					BIT(0)
#define   LINE1_IRQ					BIT(1)
#define   LINE2_IRQ					BIT(2)
#define   LINE3_IRQ					BIT(3)
#define DCSS_DTG_LINE2_INT				0x6C
#define DCSS_DTG_LINE3_INT				0x70
#define DCSS_DTG_DBY_OL					0x74
#define DCSS_DTG_DBY_BL					0x78
#define DCSS_DTG_DBY_EL					0x7C

struct dcss_dtg {
	struct device *dev;
	struct dcss_ctxld *ctxld;
	void __iomem *base_reg;
	u32 base_ofs;

	u32 ctx_id;

	bool in_use;

	u32 dis_ulc_x;
	u32 dis_ulc_y;

	u32 control_status;
	u32 alpha;
	u32 alpha_cfg;

	int ctxld_kick_irq;
	bool ctxld_kick_irq_en;
};

static void dcss_dtg_write(struct dcss_dtg *dtg, u32 val, u32 ofs)
{
	if (!dtg->in_use)
		dcss_writel(val, dtg->base_reg + ofs);

	dcss_ctxld_write(dtg->ctxld, dtg->ctx_id,
			 val, dtg->base_ofs + ofs);
}

static irqreturn_t dcss_dtg_irq_handler(int irq, void *data)
{
	struct dcss_dtg *dtg = data;
	u32 status;

	status = dcss_readl(dtg->base_reg + DCSS_DTG_INT_STATUS);

	if (!(status & LINE0_IRQ))
		return IRQ_NONE;

	dcss_ctxld_kick(dtg->ctxld);

	dcss_writel(status & LINE0_IRQ, dtg->base_reg + DCSS_DTG_INT_CONTROL);

	return IRQ_HANDLED;
}

static int dcss_dtg_irq_config(struct dcss_dtg *dtg,
			       struct platform_device *pdev)
{
	int ret;

	dtg->ctxld_kick_irq = platform_get_irq_byname(pdev, "ctxld_kick");
	if (dtg->ctxld_kick_irq < 0)
		return dtg->ctxld_kick_irq;

	dcss_update(0, LINE0_IRQ | LINE1_IRQ,
		    dtg->base_reg + DCSS_DTG_INT_MASK);

	ret = request_irq(dtg->ctxld_kick_irq, dcss_dtg_irq_handler,
			  0, "dcss_ctxld_kick", dtg);
	if (ret) {
		dev_err(dtg->dev, "dtg: irq request failed.\n");
		return ret;
	}

	disable_irq(dtg->ctxld_kick_irq);

	dtg->ctxld_kick_irq_en = false;

	return 0;
}

int dcss_dtg_init(struct dcss_dev *dcss, unsigned long dtg_base)
{
	int ret = 0;
	struct dcss_dtg *dtg;

	dtg = devm_kzalloc(dcss->dev, sizeof(*dtg), GFP_KERNEL);
	if (!dtg)
		return -ENOMEM;

	dcss->dtg = dtg;
	dtg->dev = dcss->dev;
	dtg->ctxld = dcss->ctxld;

	dtg->base_reg = devm_ioremap(dtg->dev, dtg_base, SZ_4K);
	if (!dtg->base_reg) {
		dev_err(dtg->dev, "dtg: unable to remap dtg base\n");
		return -ENOMEM;
	}

	dtg->base_ofs = dtg_base;
	dtg->ctx_id = CTX_DB;

	dtg->alpha = 255;

	dtg->control_status |= OVL_DATA_MODE | BLENDER_VIDEO_ALPHA_SEL |
		((dtg->alpha << DEFAULT_FG_ALPHA_POS) & DEFAULT_FG_ALPHA_MASK);

	ret = dcss_dtg_irq_config(dtg, to_platform_device(dtg->dev));

	return ret;
}

void dcss_dtg_exit(struct dcss_dtg *dtg)
{
	free_irq(dtg->ctxld_kick_irq, dtg);
}

void dcss_dtg_sync_set(struct dcss_dtg *dtg, struct videomode *vm)
{
	struct dcss_dev *dcss = dcss_drv_dev_to_dcss(dtg->dev);
	u16 dtg_lrc_x, dtg_lrc_y;
	u16 dis_ulc_x, dis_ulc_y;
	u16 dis_lrc_x, dis_lrc_y;
	u32 sb_ctxld_trig, db_ctxld_trig;
	u32 pixclock = vm->pixelclock;
	u32 actual_clk;

	dtg_lrc_x = vm->hfront_porch + vm->hback_porch + vm->hsync_len +
		    vm->hactive - 1;
	dtg_lrc_y = vm->vfront_porch + vm->vback_porch + vm->vsync_len +
		    vm->vactive - 1;
	dis_ulc_x = vm->hsync_len + vm->hback_porch - 1;
	dis_ulc_y = vm->vsync_len + vm->vfront_porch + vm->vback_porch - 1;
	dis_lrc_x = vm->hsync_len + vm->hback_porch + vm->hactive - 1;
	dis_lrc_y = vm->vsync_len + vm->vfront_porch + vm->vback_porch +
		    vm->vactive - 1;

	clk_disable_unprepare(dcss->pix_clk);
	clk_set_rate(dcss->pix_clk, vm->pixelclock);
	clk_prepare_enable(dcss->pix_clk);

	actual_clk = clk_get_rate(dcss->pix_clk);
	if (pixclock != actual_clk) {
		dev_info(dtg->dev,
			 "Pixel clock set to %u kHz instead of %u kHz.\n",
			 (actual_clk / 1000), (pixclock / 1000));
	}

	dcss_dtg_write(dtg, ((dtg_lrc_y << TC_Y_POS) | dtg_lrc_x),
		       DCSS_DTG_TC_DTG);
	dcss_dtg_write(dtg, ((dis_ulc_y << TC_Y_POS) | dis_ulc_x),
		       DCSS_DTG_TC_DISP_TOP);
	dcss_dtg_write(dtg, ((dis_lrc_y << TC_Y_POS) | dis_lrc_x),
		       DCSS_DTG_TC_DISP_BOT);

	dtg->dis_ulc_x = dis_ulc_x;
	dtg->dis_ulc_y = dis_ulc_y;

	sb_ctxld_trig = ((0 * dis_lrc_y / 100) << TC_CTXLD_SB_Y_POS) &
							TC_CTXLD_SB_Y_MASK;
	db_ctxld_trig = ((99 * dis_lrc_y / 100) << TC_CTXLD_DB_Y_POS) &
							TC_CTXLD_DB_Y_MASK;

	dcss_dtg_write(dtg, sb_ctxld_trig | db_ctxld_trig, DCSS_DTG_TC_CTXLD);

	/* vblank trigger */
	dcss_dtg_write(dtg, 0, DCSS_DTG_LINE1_INT);

	/* CTXLD trigger */
	dcss_dtg_write(dtg, ((90 * dis_lrc_y) / 100) << 16, DCSS_DTG_LINE0_INT);
}

void dcss_dtg_plane_pos_set(struct dcss_dtg *dtg, int ch_num,
			    int px, int py, int pw, int ph)
{
	u16 p_ulc_x, p_ulc_y;
	u16 p_lrc_x, p_lrc_y;

	p_ulc_x = dtg->dis_ulc_x + px;
	p_ulc_y = dtg->dis_ulc_y + py;
	p_lrc_x = p_ulc_x + pw;
	p_lrc_y = p_ulc_y + ph;

	if (!px && !py && !pw && !ph) {
		dcss_dtg_write(dtg, 0, DCSS_DTG_TC_CH1_TOP + 0x8 * ch_num);
		dcss_dtg_write(dtg, 0, DCSS_DTG_TC_CH1_BOT + 0x8 * ch_num);
	} else {
		dcss_dtg_write(dtg, ((p_ulc_y << TC_Y_POS) | p_ulc_x),
			       DCSS_DTG_TC_CH1_TOP + 0x8 * ch_num);
		dcss_dtg_write(dtg, ((p_lrc_y << TC_Y_POS) | p_lrc_x),
			       DCSS_DTG_TC_CH1_BOT + 0x8 * ch_num);
	}
}

bool dcss_dtg_global_alpha_changed(struct dcss_dtg *dtg, int ch_num, int alpha)
{
	if (ch_num)
		return false;

	return alpha != dtg->alpha;
}

void dcss_dtg_plane_alpha_set(struct dcss_dtg *dtg, int ch_num,
			      const struct drm_format_info *format, int alpha)
{
	/* we care about alpha only when channel 0 is concerned */
	if (ch_num)
		return;

	/*
	 * Use global alpha if pixel format does not have alpha channel or the
	 * user explicitly chose to use global alpha (i.e. alpha is not OPAQUE).
	 */
	if (!format->has_alpha || alpha != 255)
		dtg->alpha_cfg = (alpha << DEFAULT_FG_ALPHA_POS) & DEFAULT_FG_ALPHA_MASK;
	else /* use per-pixel alpha otherwise */
		dtg->alpha_cfg = CH1_ALPHA_SEL;

	dtg->alpha = alpha;
}

void dcss_dtg_css_set(struct dcss_dtg *dtg)
{
	dtg->control_status |=
			(0x5 << CSS_PIX_COMP_SWAP_POS) & CSS_PIX_COMP_SWAP_MASK;
}

void dcss_dtg_enable(struct dcss_dtg *dtg)
{
	dtg->control_status |= DTG_START;

	dtg->control_status &= ~(CH1_ALPHA_SEL | DEFAULT_FG_ALPHA_MASK);
	dtg->control_status |= dtg->alpha_cfg;

	dcss_dtg_write(dtg, dtg->control_status, DCSS_DTG_TC_CONTROL_STATUS);

	dtg->in_use = true;
}

void dcss_dtg_shutoff(struct dcss_dtg *dtg)
{
	dtg->control_status &= ~DTG_START;

	dcss_writel(dtg->control_status,
		    dtg->base_reg + DCSS_DTG_TC_CONTROL_STATUS);

	dtg->in_use = false;
}

bool dcss_dtg_is_enabled(struct dcss_dtg *dtg)
{
	return dtg->in_use;
}

void dcss_dtg_ch_enable(struct dcss_dtg *dtg, int ch_num, bool en)
{
	u32 ch_en_map[] = {CH1_EN, CH2_EN, CH3_EN};
	u32 control_status;

	control_status = dtg->control_status & ~ch_en_map[ch_num];
	control_status |= en ? ch_en_map[ch_num] : 0;

	control_status &= ~(CH1_ALPHA_SEL | DEFAULT_FG_ALPHA_MASK);
	control_status |= dtg->alpha_cfg;

	if (dtg->control_status != control_status)
		dcss_dtg_write(dtg, control_status, DCSS_DTG_TC_CONTROL_STATUS);

	dtg->control_status = control_status;
}

void dcss_dtg_vblank_irq_enable(struct dcss_dtg *dtg, bool en)
{
	u32 status;
	u32 mask = en ? LINE1_IRQ : 0;

	if (en) {
		status = dcss_readl(dtg->base_reg + DCSS_DTG_INT_STATUS);
		dcss_writel(status & LINE1_IRQ,
			    dtg->base_reg + DCSS_DTG_INT_CONTROL);
	}

	dcss_update(mask, LINE1_IRQ, dtg->base_reg + DCSS_DTG_INT_MASK);
}

void dcss_dtg_ctxld_kick_irq_enable(struct dcss_dtg *dtg, bool en)
{
	u32 status;
	u32 mask = en ? LINE0_IRQ : 0;

	if (en) {
		status = dcss_readl(dtg->base_reg + DCSS_DTG_INT_STATUS);

		if (!dtg->ctxld_kick_irq_en) {
			dcss_writel(status & LINE0_IRQ,
				    dtg->base_reg + DCSS_DTG_INT_CONTROL);
			enable_irq(dtg->ctxld_kick_irq);
			dtg->ctxld_kick_irq_en = true;
			dcss_update(mask, LINE0_IRQ,
				    dtg->base_reg + DCSS_DTG_INT_MASK);
		}

		return;
	}

	if (!dtg->ctxld_kick_irq_en)
		return;

	disable_irq_nosync(dtg->ctxld_kick_irq);
	dtg->ctxld_kick_irq_en = false;

	dcss_update(mask, LINE0_IRQ, dtg->base_reg + DCSS_DTG_INT_MASK);
}

void dcss_dtg_vblank_irq_clear(struct dcss_dtg *dtg)
{
	dcss_update(LINE1_IRQ, LINE1_IRQ, dtg->base_reg + DCSS_DTG_INT_CONTROL);
}

bool dcss_dtg_vblank_irq_valid(struct dcss_dtg *dtg)
{
	return !!(dcss_readl(dtg->base_reg + DCSS_DTG_INT_STATUS) & LINE1_IRQ);
}

