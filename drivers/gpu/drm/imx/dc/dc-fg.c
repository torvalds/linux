// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <drm/drm_modes.h>

#include "dc-de.h"
#include "dc-drv.h"

#define FGSTCTRL		0x8
#define  FGSYNCMODE_MASK	GENMASK(2, 1)
#define  FGSYNCMODE(x)		FIELD_PREP(FGSYNCMODE_MASK, (x))
#define  SHDEN			BIT(0)

#define HTCFG1			0xc
#define  HTOTAL(x)		FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define  HACT(x)		FIELD_PREP(GENMASK(13, 0), (x))

#define HTCFG2			0x10
#define  HSEN			BIT(31)
#define  HSBP(x)		FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define  HSYNC(x)		FIELD_PREP(GENMASK(13, 0), ((x) - 1))

#define VTCFG1			0x14
#define  VTOTAL(x)		FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define  VACT(x)		FIELD_PREP(GENMASK(13, 0), (x))

#define VTCFG2			0x18
#define  VSEN			BIT(31)
#define  VSBP(x)		FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define  VSYNC(x)		FIELD_PREP(GENMASK(13, 0), ((x) - 1))

#define PKICKCONFIG		0x2c
#define SKICKCONFIG		0x30
#define  EN			BIT(31)
#define  ROW(x)			FIELD_PREP(GENMASK(29, 16), (x))
#define  COL(x)			FIELD_PREP(GENMASK(13, 0), (x))

#define PACFG			0x54
#define SACFG			0x58
#define  STARTY(x)		FIELD_PREP(GENMASK(29, 16), ((x) + 1))
#define  STARTX(x)		FIELD_PREP(GENMASK(13, 0), ((x) + 1))

#define FGINCTRL		0x5c
#define FGINCTRLPANIC		0x60
#define  FGDM_MASK		GENMASK(2, 0)
#define  ENPRIMALPHA		BIT(3)
#define  ENSECALPHA		BIT(4)

#define FGCCR			0x64
#define  CCGREEN(x)		FIELD_PREP(GENMASK(19, 10), (x))

#define FGENABLE		0x68
#define  FGEN			BIT(0)

#define FGSLR			0x6c
#define  SHDTOKGEN		BIT(0)

#define FGTIMESTAMP		0x74
#define  FRAMEINDEX(x)		FIELD_GET(GENMASK(31, 14), (x))
#define  LINEINDEX(x)		FIELD_GET(GENMASK(13, 0), (x))

#define FGCHSTAT		0x78
#define  SECSYNCSTAT		BIT(24)
#define  SFIFOEMPTY		BIT(16)

#define FGCHSTATCLR		0x7c
#define  CLRSECSTAT		BIT(16)

enum dc_fg_syncmode {
	FG_SYNCMODE_OFF,	/* No side-by-side synchronization. */
};

enum dc_fg_dm {
	FG_DM_CONSTCOL = 0x1,	/* Constant Color Background is shown. */
	FG_DM_SEC_ON_TOP = 0x5,	/* Both inputs overlaid with secondary on top. */
};

static const struct dc_subdev_info dc_fg_info[] = {
	{ .reg_start = 0x5618b800, .id = 0, },
	{ .reg_start = 0x5618d400, .id = 1, },
};

static const struct regmap_range dc_fg_regmap_write_ranges[] = {
	regmap_reg_range(FGSTCTRL, VTCFG2),
	regmap_reg_range(PKICKCONFIG, SKICKCONFIG),
	regmap_reg_range(PACFG, FGSLR),
	regmap_reg_range(FGCHSTATCLR, FGCHSTATCLR),
};

static const struct regmap_range dc_fg_regmap_read_ranges[] = {
	regmap_reg_range(FGSTCTRL, VTCFG2),
	regmap_reg_range(PKICKCONFIG, SKICKCONFIG),
	regmap_reg_range(PACFG, FGENABLE),
	regmap_reg_range(FGTIMESTAMP, FGCHSTAT),
};

static const struct regmap_access_table dc_fg_regmap_write_table = {
	.yes_ranges = dc_fg_regmap_write_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fg_regmap_write_ranges),
};

static const struct regmap_access_table dc_fg_regmap_read_table = {
	.yes_ranges = dc_fg_regmap_read_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fg_regmap_read_ranges),
};

static const struct regmap_config dc_fg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_fg_regmap_write_table,
	.rd_table = &dc_fg_regmap_read_table,
	.max_register = FGCHSTATCLR,
};

static inline void dc_fg_enable_shden(struct dc_fg *fg)
{
	regmap_write_bits(fg->reg, FGSTCTRL, SHDEN, SHDEN);
}

static inline void dc_fg_syncmode(struct dc_fg *fg, enum dc_fg_syncmode mode)
{
	regmap_write_bits(fg->reg, FGSTCTRL, FGSYNCMODE_MASK, FGSYNCMODE(mode));
}

void dc_fg_cfg_videomode(struct dc_fg *fg, struct drm_display_mode *m)
{
	u32 hact, htotal, hsync, hsbp;
	u32 vact, vtotal, vsync, vsbp;
	u32 kick_row, kick_col;
	int ret;

	hact = m->crtc_hdisplay;
	htotal = m->crtc_htotal;
	hsync = m->crtc_hsync_end - m->crtc_hsync_start;
	hsbp = m->crtc_htotal - m->crtc_hsync_start;

	vact = m->crtc_vdisplay;
	vtotal = m->crtc_vtotal;
	vsync = m->crtc_vsync_end - m->crtc_vsync_start;
	vsbp = m->crtc_vtotal - m->crtc_vsync_start;

	/* video mode */
	regmap_write(fg->reg, HTCFG1, HACT(hact)   | HTOTAL(htotal));
	regmap_write(fg->reg, HTCFG2, HSYNC(hsync) | HSBP(hsbp) | HSEN);
	regmap_write(fg->reg, VTCFG1, VACT(vact)   | VTOTAL(vtotal));
	regmap_write(fg->reg, VTCFG2, VSYNC(vsync) | VSBP(vsbp) | VSEN);

	kick_col = hact + 1;
	kick_row = vact;

	/* pkickconfig */
	regmap_write(fg->reg, PKICKCONFIG, COL(kick_col) | ROW(kick_row) | EN);

	/* skikconfig */
	regmap_write(fg->reg, SKICKCONFIG, COL(kick_col) | ROW(kick_row) | EN);

	/* primary and secondary area position configuration */
	regmap_write(fg->reg, PACFG, STARTX(0) | STARTY(0));
	regmap_write(fg->reg, SACFG, STARTX(0) | STARTY(0));

	/* alpha */
	regmap_write_bits(fg->reg, FGINCTRL,      ENPRIMALPHA | ENSECALPHA, 0);
	regmap_write_bits(fg->reg, FGINCTRLPANIC, ENPRIMALPHA | ENSECALPHA, 0);

	/* constant color is green(used in panic mode)  */
	regmap_write(fg->reg, FGCCR, CCGREEN(0x3ff));

	ret = clk_set_rate(fg->clk_disp, m->clock * HZ_PER_KHZ);
	if (ret < 0)
		dev_err(fg->dev, "failed to set display clock rate: %d\n", ret);
}

static inline void dc_fg_displaymode(struct dc_fg *fg, enum dc_fg_dm mode)
{
	regmap_write_bits(fg->reg, FGINCTRL, FGDM_MASK, mode);
}

static inline void dc_fg_panic_displaymode(struct dc_fg *fg, enum dc_fg_dm mode)
{
	regmap_write_bits(fg->reg, FGINCTRLPANIC, FGDM_MASK, mode);
}

void dc_fg_enable(struct dc_fg *fg)
{
	regmap_write(fg->reg, FGENABLE, FGEN);
}

void dc_fg_disable(struct dc_fg *fg)
{
	regmap_write(fg->reg, FGENABLE, 0);
}

void dc_fg_shdtokgen(struct dc_fg *fg)
{
	regmap_write(fg->reg, FGSLR, SHDTOKGEN);
}

u32 dc_fg_get_frame_index(struct dc_fg *fg)
{
	u32 val;

	regmap_read(fg->reg, FGTIMESTAMP, &val);

	return FRAMEINDEX(val);
}

u32 dc_fg_get_line_index(struct dc_fg *fg)
{
	u32 val;

	regmap_read(fg->reg, FGTIMESTAMP, &val);

	return LINEINDEX(val);
}

bool dc_fg_wait_for_frame_index_moving(struct dc_fg *fg)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u32 frame_index, last_frame_index;

	frame_index = dc_fg_get_frame_index(fg);
	do {
		last_frame_index = frame_index;
		frame_index = dc_fg_get_frame_index(fg);
	} while (last_frame_index == frame_index &&
		 time_before(jiffies, timeout));

	return last_frame_index != frame_index;
}

bool dc_fg_secondary_requests_to_read_empty_fifo(struct dc_fg *fg)
{
	u32 val;

	regmap_read(fg->reg, FGCHSTAT, &val);

	return !!(val & SFIFOEMPTY);
}

void dc_fg_secondary_clear_channel_status(struct dc_fg *fg)
{
	regmap_write(fg->reg, FGCHSTATCLR, CLRSECSTAT);
}

int dc_fg_wait_for_secondary_syncup(struct dc_fg *fg)
{
	unsigned int val;

	return regmap_read_poll_timeout(fg->reg, FGCHSTAT, val,
					val & SECSYNCSTAT, 5, 100000);
}

void dc_fg_enable_clock(struct dc_fg *fg)
{
	int ret;

	ret = clk_prepare_enable(fg->clk_disp);
	if (ret)
		dev_err(fg->dev, "failed to enable display clock: %d\n", ret);
}

void dc_fg_disable_clock(struct dc_fg *fg)
{
	clk_disable_unprepare(fg->clk_disp);
}

enum drm_mode_status dc_fg_check_clock(struct dc_fg *fg, int clk_khz)
{
	unsigned long rounded_rate;

	rounded_rate = clk_round_rate(fg->clk_disp, clk_khz * HZ_PER_KHZ);

	if (rounded_rate != clk_khz * HZ_PER_KHZ)
		return MODE_NOCLOCK;

	return MODE_OK;
}

void dc_fg_init(struct dc_fg *fg)
{
	dc_fg_enable_shden(fg);
	dc_fg_syncmode(fg, FG_SYNCMODE_OFF);
	dc_fg_displaymode(fg, FG_DM_SEC_ON_TOP);
	dc_fg_panic_displaymode(fg, FG_DM_CONSTCOL);
}

static int dc_fg_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dc_drm_device *dc_drm = data;
	struct resource *res;
	void __iomem *base;
	struct dc_fg *fg;
	int id;

	fg = devm_kzalloc(dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fg->reg = devm_regmap_init_mmio(dev, base, &dc_fg_regmap_config);
	if (IS_ERR(fg->reg))
		return PTR_ERR(fg->reg);

	fg->clk_disp = devm_clk_get(dev, NULL);
	if (IS_ERR(fg->clk_disp))
		return dev_err_probe(dev, PTR_ERR(fg->clk_disp),
				     "failed to get display clock\n");

	id = dc_subdev_get_id(dc_fg_info, ARRAY_SIZE(dc_fg_info), res);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	fg->dev = dev;
	dc_drm->fg[id] = fg;

	return 0;
}

static const struct component_ops dc_fg_ops = {
	.bind = dc_fg_bind,
};

static int dc_fg_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_fg_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_fg_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_fg_ops);
}

static const struct of_device_id dc_fg_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-framegen" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_fg_dt_ids);

struct platform_driver dc_fg_driver = {
	.probe = dc_fg_probe,
	.remove = dc_fg_remove,
	.driver = {
		.name = "imx8-dc-framegen",
		.suppress_bind_attrs = true,
		.of_match_table = dc_fg_dt_ids,
	},
};
