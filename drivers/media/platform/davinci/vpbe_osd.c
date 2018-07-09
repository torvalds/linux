/*
 * Copyright (C) 2007-2010 Texas Instruments Inc
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Andy Lowe (alowe@mvista.com), MontaVista Software
 * - Initial version
 * Murali Karicheri (mkaricheri@gmail.com), Texas Instruments Ltd.
 * - ported to sub device interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>

#ifdef CONFIG_ARCH_DAVINCI
#include <mach/cputype.h>
#include <mach/hardware.h>
#endif

#include <media/davinci/vpss.h>
#include <media/v4l2-device.h>
#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe_osd.h>

#include <linux/io.h>
#include "vpbe_osd_regs.h"

#define MODULE_NAME	"davinci-vpbe-osd"

static const struct platform_device_id vpbe_osd_devtype[] = {
	{
		.name = DM644X_VPBE_OSD_SUBDEV_NAME,
		.driver_data = VPBE_VERSION_1,
	}, {
		.name = DM365_VPBE_OSD_SUBDEV_NAME,
		.driver_data = VPBE_VERSION_2,
	}, {
		.name = DM355_VPBE_OSD_SUBDEV_NAME,
		.driver_data = VPBE_VERSION_3,
	},
	{
		/* sentinel */
	}
};

MODULE_DEVICE_TABLE(platform, vpbe_osd_devtype);

/* register access routines */
static inline u32 osd_read(struct osd_state *sd, u32 offset)
{
	struct osd_state *osd = sd;

	return readl(osd->osd_base + offset);
}

static inline u32 osd_write(struct osd_state *sd, u32 val, u32 offset)
{
	struct osd_state *osd = sd;

	writel(val, osd->osd_base + offset);

	return val;
}

static inline u32 osd_set(struct osd_state *sd, u32 mask, u32 offset)
{
	struct osd_state *osd = sd;

	void __iomem *addr = osd->osd_base + offset;
	u32 val = readl(addr) | mask;

	writel(val, addr);

	return val;
}

static inline u32 osd_clear(struct osd_state *sd, u32 mask, u32 offset)
{
	struct osd_state *osd = sd;

	void __iomem *addr = osd->osd_base + offset;
	u32 val = readl(addr) & ~mask;

	writel(val, addr);

	return val;
}

static inline u32 osd_modify(struct osd_state *sd, u32 mask, u32 val,
				 u32 offset)
{
	struct osd_state *osd = sd;

	void __iomem *addr = osd->osd_base + offset;
	u32 new_val = (readl(addr) & ~mask) | (val & mask);

	writel(new_val, addr);

	return new_val;
}

/* define some macros for layer and pixfmt classification */
#define is_osd_win(layer) (((layer) == WIN_OSD0) || ((layer) == WIN_OSD1))
#define is_vid_win(layer) (((layer) == WIN_VID0) || ((layer) == WIN_VID1))
#define is_rgb_pixfmt(pixfmt) \
	(((pixfmt) == PIXFMT_RGB565) || ((pixfmt) == PIXFMT_RGB888))
#define is_yc_pixfmt(pixfmt) \
	(((pixfmt) == PIXFMT_YCBCRI) || ((pixfmt) == PIXFMT_YCRCBI) || \
	((pixfmt) == PIXFMT_NV12))
#define MAX_WIN_SIZE OSD_VIDWIN0XP_V0X
#define MAX_LINE_LENGTH (OSD_VIDWIN0OFST_V0LO << 5)

/**
 * _osd_dm6446_vid0_pingpong() - field inversion fix for DM6446
 * @sd: ptr to struct osd_state
 * @field_inversion: inversion flag
 * @fb_base_phys: frame buffer address
 * @lconfig: ptr to layer config
 *
 * This routine implements a workaround for the field signal inversion silicon
 * erratum described in Advisory 1.3.8 for the DM6446.  The fb_base_phys and
 * lconfig parameters apply to the vid0 window.  This routine should be called
 * whenever the vid0 layer configuration or start address is modified, or when
 * the OSD field inversion setting is modified.
 * Returns: 1 if the ping-pong buffers need to be toggled in the vsync isr, or
 *          0 otherwise
 */
static int _osd_dm6446_vid0_pingpong(struct osd_state *sd,
				     int field_inversion,
				     unsigned long fb_base_phys,
				     const struct osd_layer_config *lconfig)
{
	struct osd_platform_data *pdata;

	pdata = (struct osd_platform_data *)sd->dev->platform_data;
	if (pdata != NULL && pdata->field_inv_wa_enable) {

		if (!field_inversion || !lconfig->interlaced) {
			osd_write(sd, fb_base_phys & ~0x1F, OSD_VIDWIN0ADR);
			osd_write(sd, fb_base_phys & ~0x1F, OSD_PPVWIN0ADR);
			osd_modify(sd, OSD_MISCCTL_PPSW | OSD_MISCCTL_PPRV, 0,
				   OSD_MISCCTL);
			return 0;
		} else {
			unsigned miscctl = OSD_MISCCTL_PPRV;

			osd_write(sd,
				(fb_base_phys & ~0x1F) - lconfig->line_length,
				OSD_VIDWIN0ADR);
			osd_write(sd,
				(fb_base_phys & ~0x1F) + lconfig->line_length,
				OSD_PPVWIN0ADR);
			osd_modify(sd,
				OSD_MISCCTL_PPSW | OSD_MISCCTL_PPRV, miscctl,
				OSD_MISCCTL);

			return 1;
		}
	}

	return 0;
}

static void _osd_set_field_inversion(struct osd_state *sd, int enable)
{
	unsigned fsinv = 0;

	if (enable)
		fsinv = OSD_MODE_FSINV;

	osd_modify(sd, OSD_MODE_FSINV, fsinv, OSD_MODE);
}

static void _osd_set_blink_attribute(struct osd_state *sd, int enable,
				     enum osd_blink_interval blink)
{
	u32 osdatrmd = 0;

	if (enable) {
		osdatrmd |= OSD_OSDATRMD_BLNK;
		osdatrmd |= blink << OSD_OSDATRMD_BLNKINT_SHIFT;
	}
	/* caller must ensure that OSD1 is configured in attribute mode */
	osd_modify(sd, OSD_OSDATRMD_BLNKINT | OSD_OSDATRMD_BLNK, osdatrmd,
		  OSD_OSDATRMD);
}

static void _osd_set_rom_clut(struct osd_state *sd,
			      enum osd_rom_clut rom_clut)
{
	if (rom_clut == ROM_CLUT0)
		osd_clear(sd, OSD_MISCCTL_RSEL, OSD_MISCCTL);
	else
		osd_set(sd, OSD_MISCCTL_RSEL, OSD_MISCCTL);
}

static void _osd_set_palette_map(struct osd_state *sd,
				 enum osd_win_layer osdwin,
				 unsigned char pixel_value,
				 unsigned char clut_index,
				 enum osd_pix_format pixfmt)
{
	static const int map_2bpp[] = { 0, 5, 10, 15 };
	static const int map_1bpp[] = { 0, 15 };
	int bmp_offset;
	int bmp_shift;
	int bmp_mask;
	int bmp_reg;

	switch (pixfmt) {
	case PIXFMT_1BPP:
		bmp_reg = map_1bpp[pixel_value & 0x1];
		break;
	case PIXFMT_2BPP:
		bmp_reg = map_2bpp[pixel_value & 0x3];
		break;
	case PIXFMT_4BPP:
		bmp_reg = pixel_value & 0xf;
		break;
	default:
		return;
	}

	switch (osdwin) {
	case OSDWIN_OSD0:
		bmp_offset = OSD_W0BMP01 + (bmp_reg >> 1) * sizeof(u32);
		break;
	case OSDWIN_OSD1:
		bmp_offset = OSD_W1BMP01 + (bmp_reg >> 1) * sizeof(u32);
		break;
	default:
		return;
	}

	if (bmp_reg & 1) {
		bmp_shift = 8;
		bmp_mask = 0xff << 8;
	} else {
		bmp_shift = 0;
		bmp_mask = 0xff;
	}

	osd_modify(sd, bmp_mask, clut_index << bmp_shift, bmp_offset);
}

static void _osd_set_rec601_attenuation(struct osd_state *sd,
					enum osd_win_layer osdwin, int enable)
{
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_modify(sd, OSD_OSDWIN0MD_ATN0E,
			  enable ? OSD_OSDWIN0MD_ATN0E : 0,
			  OSD_OSDWIN0MD);
		if (sd->vpbe_type == VPBE_VERSION_1)
			osd_modify(sd, OSD_OSDWIN0MD_ATN0E,
				  enable ? OSD_OSDWIN0MD_ATN0E : 0,
				  OSD_OSDWIN0MD);
		else if ((sd->vpbe_type == VPBE_VERSION_3) ||
			   (sd->vpbe_type == VPBE_VERSION_2))
			osd_modify(sd, OSD_EXTMODE_ATNOSD0EN,
				  enable ? OSD_EXTMODE_ATNOSD0EN : 0,
				  OSD_EXTMODE);
		break;
	case OSDWIN_OSD1:
		osd_modify(sd, OSD_OSDWIN1MD_ATN1E,
			  enable ? OSD_OSDWIN1MD_ATN1E : 0,
			  OSD_OSDWIN1MD);
		if (sd->vpbe_type == VPBE_VERSION_1)
			osd_modify(sd, OSD_OSDWIN1MD_ATN1E,
				  enable ? OSD_OSDWIN1MD_ATN1E : 0,
				  OSD_OSDWIN1MD);
		else if ((sd->vpbe_type == VPBE_VERSION_3) ||
			   (sd->vpbe_type == VPBE_VERSION_2))
			osd_modify(sd, OSD_EXTMODE_ATNOSD1EN,
				  enable ? OSD_EXTMODE_ATNOSD1EN : 0,
				  OSD_EXTMODE);
		break;
	}
}

static void _osd_set_blending_factor(struct osd_state *sd,
				     enum osd_win_layer osdwin,
				     enum osd_blending_factor blend)
{
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_modify(sd, OSD_OSDWIN0MD_BLND0,
			  blend << OSD_OSDWIN0MD_BLND0_SHIFT, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		osd_modify(sd, OSD_OSDWIN1MD_BLND1,
			  blend << OSD_OSDWIN1MD_BLND1_SHIFT, OSD_OSDWIN1MD);
		break;
	}
}

static void _osd_enable_rgb888_pixblend(struct osd_state *sd,
					enum osd_win_layer osdwin)
{

	osd_modify(sd, OSD_MISCCTL_BLDSEL, 0, OSD_MISCCTL);
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_modify(sd, OSD_EXTMODE_OSD0BLDCHR,
			  OSD_EXTMODE_OSD0BLDCHR, OSD_EXTMODE);
		break;
	case OSDWIN_OSD1:
		osd_modify(sd, OSD_EXTMODE_OSD1BLDCHR,
			  OSD_EXTMODE_OSD1BLDCHR, OSD_EXTMODE);
		break;
	}
}

static void _osd_enable_color_key(struct osd_state *sd,
				  enum osd_win_layer osdwin,
				  unsigned colorkey,
				  enum osd_pix_format pixfmt)
{
	switch (pixfmt) {
	case PIXFMT_1BPP:
	case PIXFMT_2BPP:
	case PIXFMT_4BPP:
	case PIXFMT_8BPP:
		if (sd->vpbe_type == VPBE_VERSION_3) {
			switch (osdwin) {
			case OSDWIN_OSD0:
				osd_modify(sd, OSD_TRANSPBMPIDX_BMP0,
					  colorkey <<
					  OSD_TRANSPBMPIDX_BMP0_SHIFT,
					  OSD_TRANSPBMPIDX);
				break;
			case OSDWIN_OSD1:
				osd_modify(sd, OSD_TRANSPBMPIDX_BMP1,
					  colorkey <<
					  OSD_TRANSPBMPIDX_BMP1_SHIFT,
					  OSD_TRANSPBMPIDX);
				break;
			}
		}
		break;
	case PIXFMT_RGB565:
		if (sd->vpbe_type == VPBE_VERSION_1)
			osd_write(sd, colorkey & OSD_TRANSPVAL_RGBTRANS,
				  OSD_TRANSPVAL);
		else if (sd->vpbe_type == VPBE_VERSION_3)
			osd_write(sd, colorkey & OSD_TRANSPVALL_RGBL,
				  OSD_TRANSPVALL);
		break;
	case PIXFMT_YCBCRI:
	case PIXFMT_YCRCBI:
		if (sd->vpbe_type == VPBE_VERSION_3)
			osd_modify(sd, OSD_TRANSPVALU_Y, colorkey,
				   OSD_TRANSPVALU);
		break;
	case PIXFMT_RGB888:
		if (sd->vpbe_type == VPBE_VERSION_3) {
			osd_write(sd, colorkey & OSD_TRANSPVALL_RGBL,
				  OSD_TRANSPVALL);
			osd_modify(sd, OSD_TRANSPVALU_RGBU, colorkey >> 16,
				  OSD_TRANSPVALU);
		}
		break;
	default:
		break;
	}

	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_set(sd, OSD_OSDWIN0MD_TE0, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		osd_set(sd, OSD_OSDWIN1MD_TE1, OSD_OSDWIN1MD);
		break;
	}
}

static void _osd_disable_color_key(struct osd_state *sd,
				   enum osd_win_layer osdwin)
{
	switch (osdwin) {
	case OSDWIN_OSD0:
		osd_clear(sd, OSD_OSDWIN0MD_TE0, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		osd_clear(sd, OSD_OSDWIN1MD_TE1, OSD_OSDWIN1MD);
		break;
	}
}

static void _osd_set_osd_clut(struct osd_state *sd,
			      enum osd_win_layer osdwin,
			      enum osd_clut clut)
{
	u32 winmd = 0;

	switch (osdwin) {
	case OSDWIN_OSD0:
		if (clut == RAM_CLUT)
			winmd |= OSD_OSDWIN0MD_CLUTS0;
		osd_modify(sd, OSD_OSDWIN0MD_CLUTS0, winmd, OSD_OSDWIN0MD);
		break;
	case OSDWIN_OSD1:
		if (clut == RAM_CLUT)
			winmd |= OSD_OSDWIN1MD_CLUTS1;
		osd_modify(sd, OSD_OSDWIN1MD_CLUTS1, winmd, OSD_OSDWIN1MD);
		break;
	}
}

static void _osd_set_zoom(struct osd_state *sd, enum osd_layer layer,
			  enum osd_zoom_factor h_zoom,
			  enum osd_zoom_factor v_zoom)
{
	u32 winmd = 0;

	switch (layer) {
	case WIN_OSD0:
		winmd |= (h_zoom << OSD_OSDWIN0MD_OHZ0_SHIFT);
		winmd |= (v_zoom << OSD_OSDWIN0MD_OVZ0_SHIFT);
		osd_modify(sd, OSD_OSDWIN0MD_OHZ0 | OSD_OSDWIN0MD_OVZ0, winmd,
			  OSD_OSDWIN0MD);
		break;
	case WIN_VID0:
		winmd |= (h_zoom << OSD_VIDWINMD_VHZ0_SHIFT);
		winmd |= (v_zoom << OSD_VIDWINMD_VVZ0_SHIFT);
		osd_modify(sd, OSD_VIDWINMD_VHZ0 | OSD_VIDWINMD_VVZ0, winmd,
			  OSD_VIDWINMD);
		break;
	case WIN_OSD1:
		winmd |= (h_zoom << OSD_OSDWIN1MD_OHZ1_SHIFT);
		winmd |= (v_zoom << OSD_OSDWIN1MD_OVZ1_SHIFT);
		osd_modify(sd, OSD_OSDWIN1MD_OHZ1 | OSD_OSDWIN1MD_OVZ1, winmd,
			  OSD_OSDWIN1MD);
		break;
	case WIN_VID1:
		winmd |= (h_zoom << OSD_VIDWINMD_VHZ1_SHIFT);
		winmd |= (v_zoom << OSD_VIDWINMD_VVZ1_SHIFT);
		osd_modify(sd, OSD_VIDWINMD_VHZ1 | OSD_VIDWINMD_VVZ1, winmd,
			  OSD_VIDWINMD);
		break;
	}
}

static void _osd_disable_layer(struct osd_state *sd, enum osd_layer layer)
{
	switch (layer) {
	case WIN_OSD0:
		osd_clear(sd, OSD_OSDWIN0MD_OACT0, OSD_OSDWIN0MD);
		break;
	case WIN_VID0:
		osd_clear(sd, OSD_VIDWINMD_ACT0, OSD_VIDWINMD);
		break;
	case WIN_OSD1:
		/* disable attribute mode as well as disabling the window */
		osd_clear(sd, OSD_OSDWIN1MD_OASW | OSD_OSDWIN1MD_OACT1,
			  OSD_OSDWIN1MD);
		break;
	case WIN_VID1:
		osd_clear(sd, OSD_VIDWINMD_ACT1, OSD_VIDWINMD);
		break;
	}
}

static void osd_disable_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	if (!win->is_enabled) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return;
	}
	win->is_enabled = 0;

	_osd_disable_layer(sd, layer);

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void _osd_enable_attribute_mode(struct osd_state *sd)
{
	/* enable attribute mode for OSD1 */
	osd_set(sd, OSD_OSDWIN1MD_OASW, OSD_OSDWIN1MD);
}

static void _osd_enable_layer(struct osd_state *sd, enum osd_layer layer)
{
	switch (layer) {
	case WIN_OSD0:
		osd_set(sd, OSD_OSDWIN0MD_OACT0, OSD_OSDWIN0MD);
		break;
	case WIN_VID0:
		osd_set(sd, OSD_VIDWINMD_ACT0, OSD_VIDWINMD);
		break;
	case WIN_OSD1:
		/* enable OSD1 and disable attribute mode */
		osd_modify(sd, OSD_OSDWIN1MD_OASW | OSD_OSDWIN1MD_OACT1,
			  OSD_OSDWIN1MD_OACT1, OSD_OSDWIN1MD);
		break;
	case WIN_VID1:
		osd_set(sd, OSD_VIDWINMD_ACT1, OSD_VIDWINMD);
		break;
	}
}

static int osd_enable_layer(struct osd_state *sd, enum osd_layer layer,
			    int otherwin)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_layer_config *cfg = &win->lconfig;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	/*
	 * use otherwin flag to know this is the other vid window
	 * in YUV420 mode, if is, skip this check
	 */
	if (!otherwin && (!win->is_allocated ||
			!win->fb_base_phys ||
			!cfg->line_length ||
			!cfg->xsize ||
			!cfg->ysize)) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return -1;
	}

	if (win->is_enabled) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return 0;
	}
	win->is_enabled = 1;

	if (cfg->pixfmt != PIXFMT_OSD_ATTR)
		_osd_enable_layer(sd, layer);
	else {
		_osd_enable_attribute_mode(sd);
		_osd_set_blink_attribute(sd, osd->is_blinking, osd->blink);
	}

	spin_unlock_irqrestore(&osd->lock, flags);

	return 0;
}

#define OSD_SRC_ADDR_HIGH4	0x7800000
#define OSD_SRC_ADDR_HIGH7	0x7F0000
#define OSD_SRCADD_OFSET_SFT	23
#define OSD_SRCADD_ADD_SFT	16
#define OSD_WINADL_MASK		0xFFFF
#define OSD_WINOFST_MASK	0x1000
#define VPBE_REG_BASE		0x80000000

static void _osd_start_layer(struct osd_state *sd, enum osd_layer layer,
			     unsigned long fb_base_phys,
			     unsigned long cbcr_ofst)
{

	if (sd->vpbe_type == VPBE_VERSION_1) {
		switch (layer) {
		case WIN_OSD0:
			osd_write(sd, fb_base_phys & ~0x1F, OSD_OSDWIN0ADR);
			break;
		case WIN_VID0:
			osd_write(sd, fb_base_phys & ~0x1F, OSD_VIDWIN0ADR);
			break;
		case WIN_OSD1:
			osd_write(sd, fb_base_phys & ~0x1F, OSD_OSDWIN1ADR);
			break;
		case WIN_VID1:
			osd_write(sd, fb_base_phys & ~0x1F, OSD_VIDWIN1ADR);
			break;
	      }
	} else if (sd->vpbe_type == VPBE_VERSION_3) {
		unsigned long fb_offset_32 =
		    (fb_base_phys - VPBE_REG_BASE) >> 5;

		switch (layer) {
		case WIN_OSD0:
			osd_modify(sd, OSD_OSDWINADH_O0AH,
				  fb_offset_32 >> (OSD_SRCADD_ADD_SFT -
						   OSD_OSDWINADH_O0AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(sd, fb_offset_32 & OSD_OSDWIN0ADL_O0AL,
				  OSD_OSDWIN0ADL);
			break;
		case WIN_VID0:
			osd_modify(sd, OSD_VIDWINADH_V0AH,
				  fb_offset_32 >> (OSD_SRCADD_ADD_SFT -
						   OSD_VIDWINADH_V0AH_SHIFT),
				  OSD_VIDWINADH);
			osd_write(sd, fb_offset_32 & OSD_VIDWIN0ADL_V0AL,
				  OSD_VIDWIN0ADL);
			break;
		case WIN_OSD1:
			osd_modify(sd, OSD_OSDWINADH_O1AH,
				  fb_offset_32 >> (OSD_SRCADD_ADD_SFT -
						   OSD_OSDWINADH_O1AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(sd, fb_offset_32 & OSD_OSDWIN1ADL_O1AL,
				  OSD_OSDWIN1ADL);
			break;
		case WIN_VID1:
			osd_modify(sd, OSD_VIDWINADH_V1AH,
				  fb_offset_32 >> (OSD_SRCADD_ADD_SFT -
						   OSD_VIDWINADH_V1AH_SHIFT),
				  OSD_VIDWINADH);
			osd_write(sd, fb_offset_32 & OSD_VIDWIN1ADL_V1AL,
				  OSD_VIDWIN1ADL);
			break;
		}
	} else if (sd->vpbe_type == VPBE_VERSION_2) {
		struct osd_window_state *win = &sd->win[layer];
		unsigned long fb_offset_32, cbcr_offset_32;

		fb_offset_32 = fb_base_phys - VPBE_REG_BASE;
		if (cbcr_ofst)
			cbcr_offset_32 = cbcr_ofst;
		else
			cbcr_offset_32 = win->lconfig.line_length *
					 win->lconfig.ysize;
		cbcr_offset_32 += fb_offset_32;
		fb_offset_32 = fb_offset_32 >> 5;
		cbcr_offset_32 = cbcr_offset_32 >> 5;
		/*
		 * DM365: start address is 27-bit long address b26 - b23 are
		 * in offset register b12 - b9, and * bit 26 has to be '1'
		 */
		if (win->lconfig.pixfmt == PIXFMT_NV12) {
			switch (layer) {
			case WIN_VID0:
			case WIN_VID1:
				/* Y is in VID0 */
				osd_modify(sd, OSD_VIDWIN0OFST_V0AH,
					 ((fb_offset_32 & OSD_SRC_ADDR_HIGH4) >>
					 (OSD_SRCADD_OFSET_SFT -
					 OSD_WINOFST_AH_SHIFT)) |
					 OSD_WINOFST_MASK, OSD_VIDWIN0OFST);
				osd_modify(sd, OSD_VIDWINADH_V0AH,
					  (fb_offset_32 & OSD_SRC_ADDR_HIGH7) >>
					  (OSD_SRCADD_ADD_SFT -
					  OSD_VIDWINADH_V0AH_SHIFT),
					   OSD_VIDWINADH);
				osd_write(sd, fb_offset_32 & OSD_WINADL_MASK,
					  OSD_VIDWIN0ADL);
				/* CbCr is in VID1 */
				osd_modify(sd, OSD_VIDWIN1OFST_V1AH,
					 ((cbcr_offset_32 &
					 OSD_SRC_ADDR_HIGH4) >>
					 (OSD_SRCADD_OFSET_SFT -
					 OSD_WINOFST_AH_SHIFT)) |
					 OSD_WINOFST_MASK, OSD_VIDWIN1OFST);
				osd_modify(sd, OSD_VIDWINADH_V1AH,
					  (cbcr_offset_32 &
					  OSD_SRC_ADDR_HIGH7) >>
					  (OSD_SRCADD_ADD_SFT -
					  OSD_VIDWINADH_V1AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(sd, cbcr_offset_32 & OSD_WINADL_MASK,
					  OSD_VIDWIN1ADL);
				break;
			default:
				break;
			}
		}

		switch (layer) {
		case WIN_OSD0:
			osd_modify(sd, OSD_OSDWIN0OFST_O0AH,
				 ((fb_offset_32 & OSD_SRC_ADDR_HIGH4) >>
				 (OSD_SRCADD_OFSET_SFT -
				 OSD_WINOFST_AH_SHIFT)) | OSD_WINOFST_MASK,
				  OSD_OSDWIN0OFST);
			osd_modify(sd, OSD_OSDWINADH_O0AH,
				 (fb_offset_32 & OSD_SRC_ADDR_HIGH7) >>
				 (OSD_SRCADD_ADD_SFT -
				 OSD_OSDWINADH_O0AH_SHIFT), OSD_OSDWINADH);
			osd_write(sd, fb_offset_32 & OSD_WINADL_MASK,
					OSD_OSDWIN0ADL);
			break;
		case WIN_VID0:
			if (win->lconfig.pixfmt != PIXFMT_NV12) {
				osd_modify(sd, OSD_VIDWIN0OFST_V0AH,
					 ((fb_offset_32 & OSD_SRC_ADDR_HIGH4) >>
					 (OSD_SRCADD_OFSET_SFT -
					 OSD_WINOFST_AH_SHIFT)) |
					 OSD_WINOFST_MASK, OSD_VIDWIN0OFST);
				osd_modify(sd, OSD_VIDWINADH_V0AH,
					  (fb_offset_32 & OSD_SRC_ADDR_HIGH7) >>
					  (OSD_SRCADD_ADD_SFT -
					  OSD_VIDWINADH_V0AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(sd, fb_offset_32 & OSD_WINADL_MASK,
					  OSD_VIDWIN0ADL);
			}
			break;
		case WIN_OSD1:
			osd_modify(sd, OSD_OSDWIN1OFST_O1AH,
				 ((fb_offset_32 & OSD_SRC_ADDR_HIGH4) >>
				 (OSD_SRCADD_OFSET_SFT -
				 OSD_WINOFST_AH_SHIFT)) | OSD_WINOFST_MASK,
				  OSD_OSDWIN1OFST);
			osd_modify(sd, OSD_OSDWINADH_O1AH,
				  (fb_offset_32 & OSD_SRC_ADDR_HIGH7) >>
				  (OSD_SRCADD_ADD_SFT -
				  OSD_OSDWINADH_O1AH_SHIFT),
				  OSD_OSDWINADH);
			osd_write(sd, fb_offset_32 & OSD_WINADL_MASK,
					OSD_OSDWIN1ADL);
			break;
		case WIN_VID1:
			if (win->lconfig.pixfmt != PIXFMT_NV12) {
				osd_modify(sd, OSD_VIDWIN1OFST_V1AH,
					 ((fb_offset_32 & OSD_SRC_ADDR_HIGH4) >>
					 (OSD_SRCADD_OFSET_SFT -
					 OSD_WINOFST_AH_SHIFT)) |
					 OSD_WINOFST_MASK, OSD_VIDWIN1OFST);
				osd_modify(sd, OSD_VIDWINADH_V1AH,
					  (fb_offset_32 & OSD_SRC_ADDR_HIGH7) >>
					  (OSD_SRCADD_ADD_SFT -
					  OSD_VIDWINADH_V1AH_SHIFT),
					  OSD_VIDWINADH);
				osd_write(sd, fb_offset_32 & OSD_WINADL_MASK,
					  OSD_VIDWIN1ADL);
			}
			break;
		}
	}
}

static void osd_start_layer(struct osd_state *sd, enum osd_layer layer,
			    unsigned long fb_base_phys,
			    unsigned long cbcr_ofst)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_layer_config *cfg = &win->lconfig;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	win->fb_base_phys = fb_base_phys & ~0x1F;
	_osd_start_layer(sd, layer, fb_base_phys, cbcr_ofst);

	if (layer == WIN_VID0) {
		osd->pingpong =
		    _osd_dm6446_vid0_pingpong(sd, osd->field_inversion,
						       win->fb_base_phys,
						       cfg);
	}

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_get_layer_config(struct osd_state *sd, enum osd_layer layer,
				 struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	*lconfig = win->lconfig;

	spin_unlock_irqrestore(&osd->lock, flags);
}

/**
 * try_layer_config() - Try a specific configuration for the layer
 * @sd: ptr to struct osd_state
 * @layer: layer to configure
 * @lconfig: layer configuration to try
 *
 * If the requested lconfig is completely rejected and the value of lconfig on
 * exit is the current lconfig, then try_layer_config() returns 1.  Otherwise,
 * try_layer_config() returns 0.  A return value of 0 does not necessarily mean
 * that the value of lconfig on exit is identical to the value of lconfig on
 * entry, but merely that it represents a change from the current lconfig.
 */
static int try_layer_config(struct osd_state *sd, enum osd_layer layer,
			    struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	int bad_config = 0;

	/* verify that the pixel format is compatible with the layer */
	switch (lconfig->pixfmt) {
	case PIXFMT_1BPP:
	case PIXFMT_2BPP:
	case PIXFMT_4BPP:
	case PIXFMT_8BPP:
	case PIXFMT_RGB565:
		if (osd->vpbe_type == VPBE_VERSION_1)
			bad_config = !is_vid_win(layer);
		break;
	case PIXFMT_YCBCRI:
	case PIXFMT_YCRCBI:
		bad_config = !is_vid_win(layer);
		break;
	case PIXFMT_RGB888:
		if (osd->vpbe_type == VPBE_VERSION_1)
			bad_config = !is_vid_win(layer);
		else if ((osd->vpbe_type == VPBE_VERSION_3) ||
			 (osd->vpbe_type == VPBE_VERSION_2))
			bad_config = !is_osd_win(layer);
		break;
	case PIXFMT_NV12:
		if (osd->vpbe_type != VPBE_VERSION_2)
			bad_config = 1;
		else
			bad_config = is_osd_win(layer);
		break;
	case PIXFMT_OSD_ATTR:
		bad_config = (layer != WIN_OSD1);
		break;
	default:
		bad_config = 1;
		break;
	}
	if (bad_config) {
		/*
		 * The requested pixel format is incompatible with the layer,
		 * so keep the current layer configuration.
		 */
		*lconfig = win->lconfig;
		return bad_config;
	}

	/* DM6446: */
	/* only one OSD window at a time can use RGB pixel formats */
	if ((osd->vpbe_type == VPBE_VERSION_1) &&
	    is_osd_win(layer) && is_rgb_pixfmt(lconfig->pixfmt)) {
		enum osd_pix_format pixfmt;

		if (layer == WIN_OSD0)
			pixfmt = osd->win[WIN_OSD1].lconfig.pixfmt;
		else
			pixfmt = osd->win[WIN_OSD0].lconfig.pixfmt;

		if (is_rgb_pixfmt(pixfmt)) {
			/*
			 * The other OSD window is already configured for an
			 * RGB, so keep the current layer configuration.
			 */
			*lconfig = win->lconfig;
			return 1;
		}
	}

	/* DM6446: only one video window at a time can use RGB888 */
	if ((osd->vpbe_type == VPBE_VERSION_1) && is_vid_win(layer) &&
		lconfig->pixfmt == PIXFMT_RGB888) {
		enum osd_pix_format pixfmt;

		if (layer == WIN_VID0)
			pixfmt = osd->win[WIN_VID1].lconfig.pixfmt;
		else
			pixfmt = osd->win[WIN_VID0].lconfig.pixfmt;

		if (pixfmt == PIXFMT_RGB888) {
			/*
			 * The other video window is already configured for
			 * RGB888, so keep the current layer configuration.
			 */
			*lconfig = win->lconfig;
			return 1;
		}
	}

	/* window dimensions must be non-zero */
	if (!lconfig->line_length || !lconfig->xsize || !lconfig->ysize) {
		*lconfig = win->lconfig;
		return 1;
	}

	/* round line_length up to a multiple of 32 */
	lconfig->line_length = ((lconfig->line_length + 31) / 32) * 32;
	lconfig->line_length =
	    min(lconfig->line_length, (unsigned)MAX_LINE_LENGTH);
	lconfig->xsize = min(lconfig->xsize, (unsigned)MAX_WIN_SIZE);
	lconfig->ysize = min(lconfig->ysize, (unsigned)MAX_WIN_SIZE);
	lconfig->xpos = min(lconfig->xpos, (unsigned)MAX_WIN_SIZE);
	lconfig->ypos = min(lconfig->ypos, (unsigned)MAX_WIN_SIZE);
	lconfig->interlaced = (lconfig->interlaced != 0);
	if (lconfig->interlaced) {
		/* ysize and ypos must be even for interlaced displays */
		lconfig->ysize &= ~1;
		lconfig->ypos &= ~1;
	}

	return 0;
}

static void _osd_disable_vid_rgb888(struct osd_state *sd)
{
	/*
	 * The DM6446 supports RGB888 pixel format in a single video window.
	 * This routine disables RGB888 pixel format for both video windows.
	 * The caller must ensure that neither video window is currently
	 * configured for RGB888 pixel format.
	 */
	if (sd->vpbe_type == VPBE_VERSION_1)
		osd_clear(sd, OSD_MISCCTL_RGBEN, OSD_MISCCTL);
}

static void _osd_enable_vid_rgb888(struct osd_state *sd,
				   enum osd_layer layer)
{
	/*
	 * The DM6446 supports RGB888 pixel format in a single video window.
	 * This routine enables RGB888 pixel format for the specified video
	 * window.  The caller must ensure that the other video window is not
	 * currently configured for RGB888 pixel format, as this routine will
	 * disable RGB888 pixel format for the other window.
	 */
	if (sd->vpbe_type == VPBE_VERSION_1) {
		if (layer == WIN_VID0)
			osd_modify(sd, OSD_MISCCTL_RGBEN | OSD_MISCCTL_RGBWIN,
				  OSD_MISCCTL_RGBEN, OSD_MISCCTL);
		else if (layer == WIN_VID1)
			osd_modify(sd, OSD_MISCCTL_RGBEN | OSD_MISCCTL_RGBWIN,
				  OSD_MISCCTL_RGBEN | OSD_MISCCTL_RGBWIN,
				  OSD_MISCCTL);
	}
}

static void _osd_set_cbcr_order(struct osd_state *sd,
				enum osd_pix_format pixfmt)
{
	/*
	 * The caller must ensure that all windows using YC pixfmt use the same
	 * Cb/Cr order.
	 */
	if (pixfmt == PIXFMT_YCBCRI)
		osd_clear(sd, OSD_MODE_CS, OSD_MODE);
	else if (pixfmt == PIXFMT_YCRCBI)
		osd_set(sd, OSD_MODE_CS, OSD_MODE);
}

static void _osd_set_layer_config(struct osd_state *sd, enum osd_layer layer,
				  const struct osd_layer_config *lconfig)
{
	u32 winmd = 0, winmd_mask = 0, bmw = 0;

	_osd_set_cbcr_order(sd, lconfig->pixfmt);

	switch (layer) {
	case WIN_OSD0:
		if (sd->vpbe_type == VPBE_VERSION_1) {
			winmd_mask |= OSD_OSDWIN0MD_RGB0E;
			if (lconfig->pixfmt == PIXFMT_RGB565)
				winmd |= OSD_OSDWIN0MD_RGB0E;
		} else if ((sd->vpbe_type == VPBE_VERSION_3) ||
		  (sd->vpbe_type == VPBE_VERSION_2)) {
			winmd_mask |= OSD_OSDWIN0MD_BMP0MD;
			switch (lconfig->pixfmt) {
			case PIXFMT_RGB565:
					winmd |= (1 <<
					OSD_OSDWIN0MD_BMP0MD_SHIFT);
					break;
			case PIXFMT_RGB888:
				winmd |= (2 << OSD_OSDWIN0MD_BMP0MD_SHIFT);
				_osd_enable_rgb888_pixblend(sd, OSDWIN_OSD0);
				break;
			case PIXFMT_YCBCRI:
			case PIXFMT_YCRCBI:
				winmd |= (3 << OSD_OSDWIN0MD_BMP0MD_SHIFT);
				break;
			default:
				break;
			}
		}

		winmd_mask |= OSD_OSDWIN0MD_BMW0 | OSD_OSDWIN0MD_OFF0;

		switch (lconfig->pixfmt) {
		case PIXFMT_1BPP:
			bmw = 0;
			break;
		case PIXFMT_2BPP:
			bmw = 1;
			break;
		case PIXFMT_4BPP:
			bmw = 2;
			break;
		case PIXFMT_8BPP:
			bmw = 3;
			break;
		default:
			break;
		}
		winmd |= (bmw << OSD_OSDWIN0MD_BMW0_SHIFT);

		if (lconfig->interlaced)
			winmd |= OSD_OSDWIN0MD_OFF0;

		osd_modify(sd, winmd_mask, winmd, OSD_OSDWIN0MD);
		osd_write(sd, lconfig->line_length >> 5, OSD_OSDWIN0OFST);
		osd_write(sd, lconfig->xpos, OSD_OSDWIN0XP);
		osd_write(sd, lconfig->xsize, OSD_OSDWIN0XL);
		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_OSDWIN0YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_OSDWIN0YL);
		} else {
			osd_write(sd, lconfig->ypos, OSD_OSDWIN0YP);
			osd_write(sd, lconfig->ysize, OSD_OSDWIN0YL);
		}
		break;
	case WIN_VID0:
		winmd_mask |= OSD_VIDWINMD_VFF0;
		if (lconfig->interlaced)
			winmd |= OSD_VIDWINMD_VFF0;

		osd_modify(sd, winmd_mask, winmd, OSD_VIDWINMD);
		osd_write(sd, lconfig->line_length >> 5, OSD_VIDWIN0OFST);
		osd_write(sd, lconfig->xpos, OSD_VIDWIN0XP);
		osd_write(sd, lconfig->xsize, OSD_VIDWIN0XL);
		/*
		 * For YUV420P format the register contents are
		 * duplicated in both VID registers
		 */
		if ((sd->vpbe_type == VPBE_VERSION_2) &&
				(lconfig->pixfmt == PIXFMT_NV12)) {
			/* other window also */
			if (lconfig->interlaced) {
				winmd_mask |= OSD_VIDWINMD_VFF1;
				winmd |= OSD_VIDWINMD_VFF1;
				osd_modify(sd, winmd_mask, winmd,
					  OSD_VIDWINMD);
			}

			osd_modify(sd, OSD_MISCCTL_S420D,
				    OSD_MISCCTL_S420D, OSD_MISCCTL);
			osd_write(sd, lconfig->line_length >> 5,
				  OSD_VIDWIN1OFST);
			osd_write(sd, lconfig->xpos, OSD_VIDWIN1XP);
			osd_write(sd, lconfig->xsize, OSD_VIDWIN1XL);
			/*
			  * if NV21 pixfmt and line length not 32B
			  * aligned (e.g. NTSC), Need to set window
			  * X pixel size to be 32B aligned as well
			  */
			if (lconfig->xsize % 32) {
				osd_write(sd,
					  ((lconfig->xsize + 31) & ~31),
					  OSD_VIDWIN1XL);
				osd_write(sd,
					  ((lconfig->xsize + 31) & ~31),
					  OSD_VIDWIN0XL);
			}
		} else if ((sd->vpbe_type == VPBE_VERSION_2) &&
				(lconfig->pixfmt != PIXFMT_NV12)) {
			osd_modify(sd, OSD_MISCCTL_S420D, ~OSD_MISCCTL_S420D,
						OSD_MISCCTL);
		}

		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_VIDWIN0YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_VIDWIN0YL);
			if ((sd->vpbe_type == VPBE_VERSION_2) &&
				lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos >> 1,
					  OSD_VIDWIN1YP);
				osd_write(sd, lconfig->ysize >> 1,
					  OSD_VIDWIN1YL);
			}
		} else {
			osd_write(sd, lconfig->ypos, OSD_VIDWIN0YP);
			osd_write(sd, lconfig->ysize, OSD_VIDWIN0YL);
			if ((sd->vpbe_type == VPBE_VERSION_2) &&
				lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos, OSD_VIDWIN1YP);
				osd_write(sd, lconfig->ysize, OSD_VIDWIN1YL);
			}
		}
		break;
	case WIN_OSD1:
		/*
		 * The caller must ensure that OSD1 is disabled prior to
		 * switching from a normal mode to attribute mode or from
		 * attribute mode to a normal mode.
		 */
		if (lconfig->pixfmt == PIXFMT_OSD_ATTR) {
			if (sd->vpbe_type == VPBE_VERSION_1) {
				winmd_mask |= OSD_OSDWIN1MD_ATN1E |
				OSD_OSDWIN1MD_RGB1E | OSD_OSDWIN1MD_CLUTS1 |
				OSD_OSDWIN1MD_BLND1 | OSD_OSDWIN1MD_TE1;
			} else {
				winmd_mask |= OSD_OSDWIN1MD_BMP1MD |
				OSD_OSDWIN1MD_CLUTS1 | OSD_OSDWIN1MD_BLND1 |
				OSD_OSDWIN1MD_TE1;
			}
		} else {
			if (sd->vpbe_type == VPBE_VERSION_1) {
				winmd_mask |= OSD_OSDWIN1MD_RGB1E;
				if (lconfig->pixfmt == PIXFMT_RGB565)
					winmd |= OSD_OSDWIN1MD_RGB1E;
			} else if ((sd->vpbe_type == VPBE_VERSION_3)
				   || (sd->vpbe_type == VPBE_VERSION_2)) {
				winmd_mask |= OSD_OSDWIN1MD_BMP1MD;
				switch (lconfig->pixfmt) {
				case PIXFMT_RGB565:
					winmd |=
					    (1 << OSD_OSDWIN1MD_BMP1MD_SHIFT);
					break;
				case PIXFMT_RGB888:
					winmd |=
					    (2 << OSD_OSDWIN1MD_BMP1MD_SHIFT);
					_osd_enable_rgb888_pixblend(sd,
							OSDWIN_OSD1);
					break;
				case PIXFMT_YCBCRI:
				case PIXFMT_YCRCBI:
					winmd |=
					    (3 << OSD_OSDWIN1MD_BMP1MD_SHIFT);
					break;
				default:
					break;
				}
			}

			winmd_mask |= OSD_OSDWIN1MD_BMW1;
			switch (lconfig->pixfmt) {
			case PIXFMT_1BPP:
				bmw = 0;
				break;
			case PIXFMT_2BPP:
				bmw = 1;
				break;
			case PIXFMT_4BPP:
				bmw = 2;
				break;
			case PIXFMT_8BPP:
				bmw = 3;
				break;
			default:
				break;
			}
			winmd |= (bmw << OSD_OSDWIN1MD_BMW1_SHIFT);
		}

		winmd_mask |= OSD_OSDWIN1MD_OFF1;
		if (lconfig->interlaced)
			winmd |= OSD_OSDWIN1MD_OFF1;

		osd_modify(sd, winmd_mask, winmd, OSD_OSDWIN1MD);
		osd_write(sd, lconfig->line_length >> 5, OSD_OSDWIN1OFST);
		osd_write(sd, lconfig->xpos, OSD_OSDWIN1XP);
		osd_write(sd, lconfig->xsize, OSD_OSDWIN1XL);
		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_OSDWIN1YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_OSDWIN1YL);
		} else {
			osd_write(sd, lconfig->ypos, OSD_OSDWIN1YP);
			osd_write(sd, lconfig->ysize, OSD_OSDWIN1YL);
		}
		break;
	case WIN_VID1:
		winmd_mask |= OSD_VIDWINMD_VFF1;
		if (lconfig->interlaced)
			winmd |= OSD_VIDWINMD_VFF1;

		osd_modify(sd, winmd_mask, winmd, OSD_VIDWINMD);
		osd_write(sd, lconfig->line_length >> 5, OSD_VIDWIN1OFST);
		osd_write(sd, lconfig->xpos, OSD_VIDWIN1XP);
		osd_write(sd, lconfig->xsize, OSD_VIDWIN1XL);
		/*
		 * For YUV420P format the register contents are
		 * duplicated in both VID registers
		 */
		if (sd->vpbe_type == VPBE_VERSION_2) {
			if (lconfig->pixfmt == PIXFMT_NV12) {
				/* other window also */
				if (lconfig->interlaced) {
					winmd_mask |= OSD_VIDWINMD_VFF0;
					winmd |= OSD_VIDWINMD_VFF0;
					osd_modify(sd, winmd_mask, winmd,
						  OSD_VIDWINMD);
				}
				osd_modify(sd, OSD_MISCCTL_S420D,
					   OSD_MISCCTL_S420D, OSD_MISCCTL);
				osd_write(sd, lconfig->line_length >> 5,
					  OSD_VIDWIN0OFST);
				osd_write(sd, lconfig->xpos, OSD_VIDWIN0XP);
				osd_write(sd, lconfig->xsize, OSD_VIDWIN0XL);
			} else {
				osd_modify(sd, OSD_MISCCTL_S420D,
					   ~OSD_MISCCTL_S420D, OSD_MISCCTL);
			}
		}

		if (lconfig->interlaced) {
			osd_write(sd, lconfig->ypos >> 1, OSD_VIDWIN1YP);
			osd_write(sd, lconfig->ysize >> 1, OSD_VIDWIN1YL);
			if ((sd->vpbe_type == VPBE_VERSION_2) &&
				lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos >> 1,
					  OSD_VIDWIN0YP);
				osd_write(sd, lconfig->ysize >> 1,
					  OSD_VIDWIN0YL);
			}
		} else {
			osd_write(sd, lconfig->ypos, OSD_VIDWIN1YP);
			osd_write(sd, lconfig->ysize, OSD_VIDWIN1YL);
			if ((sd->vpbe_type == VPBE_VERSION_2) &&
				lconfig->pixfmt == PIXFMT_NV12) {
				osd_write(sd, lconfig->ypos, OSD_VIDWIN0YP);
				osd_write(sd, lconfig->ysize, OSD_VIDWIN0YL);
			}
		}
		break;
	}
}

static int osd_set_layer_config(struct osd_state *sd, enum osd_layer layer,
				struct osd_layer_config *lconfig)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	struct osd_layer_config *cfg = &win->lconfig;
	unsigned long flags;
	int reject_config;

	spin_lock_irqsave(&osd->lock, flags);

	reject_config = try_layer_config(sd, layer, lconfig);
	if (reject_config) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return reject_config;
	}

	/* update the current Cb/Cr order */
	if (is_yc_pixfmt(lconfig->pixfmt))
		osd->yc_pixfmt = lconfig->pixfmt;

	/*
	 * If we are switching OSD1 from normal mode to attribute mode or from
	 * attribute mode to normal mode, then we must disable the window.
	 */
	if (layer == WIN_OSD1) {
		if (((lconfig->pixfmt == PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt != PIXFMT_OSD_ATTR)) ||
		  ((lconfig->pixfmt != PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt == PIXFMT_OSD_ATTR))) {
			win->is_enabled = 0;
			_osd_disable_layer(sd, layer);
		}
	}

	_osd_set_layer_config(sd, layer, lconfig);

	if (layer == WIN_OSD1) {
		struct osd_osdwin_state *osdwin_state =
		    &osd->osdwin[OSDWIN_OSD1];

		if ((lconfig->pixfmt != PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt == PIXFMT_OSD_ATTR)) {
			/*
			 * We just switched OSD1 from attribute mode to normal
			 * mode, so we must initialize the CLUT select, the
			 * blend factor, transparency colorkey enable, and
			 * attenuation enable (DM6446 only) bits in the
			 * OSDWIN1MD register.
			 */
			_osd_set_osd_clut(sd, OSDWIN_OSD1,
						   osdwin_state->clut);
			_osd_set_blending_factor(sd, OSDWIN_OSD1,
							  osdwin_state->blend);
			if (osdwin_state->colorkey_blending) {
				_osd_enable_color_key(sd, OSDWIN_OSD1,
							       osdwin_state->
							       colorkey,
							       lconfig->pixfmt);
			} else
				_osd_disable_color_key(sd, OSDWIN_OSD1);
			_osd_set_rec601_attenuation(sd, OSDWIN_OSD1,
						    osdwin_state->
						    rec601_attenuation);
		} else if ((lconfig->pixfmt == PIXFMT_OSD_ATTR) &&
		  (cfg->pixfmt != PIXFMT_OSD_ATTR)) {
			/*
			 * We just switched OSD1 from normal mode to attribute
			 * mode, so we must initialize the blink enable and
			 * blink interval bits in the OSDATRMD register.
			 */
			_osd_set_blink_attribute(sd, osd->is_blinking,
							  osd->blink);
		}
	}

	/*
	 * If we just switched to a 1-, 2-, or 4-bits-per-pixel bitmap format
	 * then configure a default palette map.
	 */
	if ((lconfig->pixfmt != cfg->pixfmt) &&
	  ((lconfig->pixfmt == PIXFMT_1BPP) ||
	  (lconfig->pixfmt == PIXFMT_2BPP) ||
	  (lconfig->pixfmt == PIXFMT_4BPP))) {
		enum osd_win_layer osdwin =
		    ((layer == WIN_OSD0) ? OSDWIN_OSD0 : OSDWIN_OSD1);
		struct osd_osdwin_state *osdwin_state =
		    &osd->osdwin[osdwin];
		unsigned char clut_index;
		unsigned char clut_entries = 0;

		switch (lconfig->pixfmt) {
		case PIXFMT_1BPP:
			clut_entries = 2;
			break;
		case PIXFMT_2BPP:
			clut_entries = 4;
			break;
		case PIXFMT_4BPP:
			clut_entries = 16;
			break;
		default:
			break;
		}
		/*
		 * The default palette map maps the pixel value to the clut
		 * index, i.e. pixel value 0 maps to clut entry 0, pixel value
		 * 1 maps to clut entry 1, etc.
		 */
		for (clut_index = 0; clut_index < 16; clut_index++) {
			osdwin_state->palette_map[clut_index] = clut_index;
			if (clut_index < clut_entries) {
				_osd_set_palette_map(sd, osdwin, clut_index,
						     clut_index,
						     lconfig->pixfmt);
			}
		}
	}

	*cfg = *lconfig;
	/* DM6446: configure the RGB888 enable and window selection */
	if (osd->win[WIN_VID0].lconfig.pixfmt == PIXFMT_RGB888)
		_osd_enable_vid_rgb888(sd, WIN_VID0);
	else if (osd->win[WIN_VID1].lconfig.pixfmt == PIXFMT_RGB888)
		_osd_enable_vid_rgb888(sd, WIN_VID1);
	else
		_osd_disable_vid_rgb888(sd);

	if (layer == WIN_VID0) {
		osd->pingpong =
		    _osd_dm6446_vid0_pingpong(sd, osd->field_inversion,
						       win->fb_base_phys,
						       cfg);
	}

	spin_unlock_irqrestore(&osd->lock, flags);

	return 0;
}

static void osd_init_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	enum osd_win_layer osdwin;
	struct osd_osdwin_state *osdwin_state;
	struct osd_layer_config *cfg = &win->lconfig;
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	win->is_enabled = 0;
	_osd_disable_layer(sd, layer);

	win->h_zoom = ZOOM_X1;
	win->v_zoom = ZOOM_X1;
	_osd_set_zoom(sd, layer, win->h_zoom, win->v_zoom);

	win->fb_base_phys = 0;
	_osd_start_layer(sd, layer, win->fb_base_phys, 0);

	cfg->line_length = 0;
	cfg->xsize = 0;
	cfg->ysize = 0;
	cfg->xpos = 0;
	cfg->ypos = 0;
	cfg->interlaced = 0;
	switch (layer) {
	case WIN_OSD0:
	case WIN_OSD1:
		osdwin = (layer == WIN_OSD0) ? OSDWIN_OSD0 : OSDWIN_OSD1;
		osdwin_state = &osd->osdwin[osdwin];
		/*
		 * Other code relies on the fact that OSD windows default to a
		 * bitmap pixel format when they are deallocated, so don't
		 * change this default pixel format.
		 */
		cfg->pixfmt = PIXFMT_8BPP;
		_osd_set_layer_config(sd, layer, cfg);
		osdwin_state->clut = RAM_CLUT;
		_osd_set_osd_clut(sd, osdwin, osdwin_state->clut);
		osdwin_state->colorkey_blending = 0;
		_osd_disable_color_key(sd, osdwin);
		osdwin_state->blend = OSD_8_VID_0;
		_osd_set_blending_factor(sd, osdwin, osdwin_state->blend);
		osdwin_state->rec601_attenuation = 0;
		_osd_set_rec601_attenuation(sd, osdwin,
						     osdwin_state->
						     rec601_attenuation);
		if (osdwin == OSDWIN_OSD1) {
			osd->is_blinking = 0;
			osd->blink = BLINK_X1;
		}
		break;
	case WIN_VID0:
	case WIN_VID1:
		cfg->pixfmt = osd->yc_pixfmt;
		_osd_set_layer_config(sd, layer, cfg);
		break;
	}

	spin_unlock_irqrestore(&osd->lock, flags);
}

static void osd_release_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	if (!win->is_allocated) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&osd->lock, flags);
	osd_init_layer(sd, layer);
	spin_lock_irqsave(&osd->lock, flags);

	win->is_allocated = 0;

	spin_unlock_irqrestore(&osd->lock, flags);
}

static int osd_request_layer(struct osd_state *sd, enum osd_layer layer)
{
	struct osd_state *osd = sd;
	struct osd_window_state *win = &osd->win[layer];
	unsigned long flags;

	spin_lock_irqsave(&osd->lock, flags);

	if (win->is_allocated) {
		spin_unlock_irqrestore(&osd->lock, flags);
		return -1;
	}
	win->is_allocated = 1;

	spin_unlock_irqrestore(&osd->lock, flags);

	return 0;
}

static void _osd_init(struct osd_state *sd)
{
	osd_write(sd, 0, OSD_MODE);
	osd_write(sd, 0, OSD_VIDWINMD);
	osd_write(sd, 0, OSD_OSDWIN0MD);
	osd_write(sd, 0, OSD_OSDWIN1MD);
	osd_write(sd, 0, OSD_RECTCUR);
	osd_write(sd, 0, OSD_MISCCTL);
	if (sd->vpbe_type == VPBE_VERSION_3) {
		osd_write(sd, 0, OSD_VBNDRY);
		osd_write(sd, 0, OSD_EXTMODE);
		osd_write(sd, OSD_MISCCTL_DMANG, OSD_MISCCTL);
	}
}

static void osd_set_left_margin(struct osd_state *sd, u32 val)
{
	osd_write(sd, val, OSD_BASEPX);
}

static void osd_set_top_margin(struct osd_state *sd, u32 val)
{
	osd_write(sd, val, OSD_BASEPY);
}

static int osd_initialize(struct osd_state *osd)
{
	if (osd == NULL)
		return -ENODEV;
	_osd_init(osd);

	/* set default Cb/Cr order */
	osd->yc_pixfmt = PIXFMT_YCBCRI;

	if (osd->vpbe_type == VPBE_VERSION_3) {
		/*
		 * ROM CLUT1 on the DM355 is similar (identical?) to ROM CLUT0
		 * on the DM6446, so make ROM_CLUT1 the default on the DM355.
		 */
		osd->rom_clut = ROM_CLUT1;
	}

	_osd_set_field_inversion(osd, osd->field_inversion);
	_osd_set_rom_clut(osd, osd->rom_clut);

	osd_init_layer(osd, WIN_OSD0);
	osd_init_layer(osd, WIN_VID0);
	osd_init_layer(osd, WIN_OSD1);
	osd_init_layer(osd, WIN_VID1);

	return 0;
}

static const struct vpbe_osd_ops osd_ops = {
	.initialize = osd_initialize,
	.request_layer = osd_request_layer,
	.release_layer = osd_release_layer,
	.enable_layer = osd_enable_layer,
	.disable_layer = osd_disable_layer,
	.set_layer_config = osd_set_layer_config,
	.get_layer_config = osd_get_layer_config,
	.start_layer = osd_start_layer,
	.set_left_margin = osd_set_left_margin,
	.set_top_margin = osd_set_top_margin,
};

static int osd_probe(struct platform_device *pdev)
{
	const struct platform_device_id *pdev_id;
	struct osd_state *osd;
	struct resource *res;

	pdev_id = platform_get_device_id(pdev);
	if (!pdev_id)
		return -EINVAL;

	osd = devm_kzalloc(&pdev->dev, sizeof(struct osd_state), GFP_KERNEL);
	if (osd == NULL)
		return -ENOMEM;


	osd->dev = &pdev->dev;
	osd->vpbe_type = pdev_id->driver_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	osd->osd_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(osd->osd_base))
		return PTR_ERR(osd->osd_base);

	osd->osd_base_phys = res->start;
	osd->osd_size = resource_size(res);
	spin_lock_init(&osd->lock);
	osd->ops = osd_ops;
	platform_set_drvdata(pdev, osd);
	dev_notice(osd->dev, "OSD sub device probe success\n");

	return 0;
}

static int osd_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver osd_driver = {
	.probe		= osd_probe,
	.remove		= osd_remove,
	.driver		= {
		.name	= MODULE_NAME,
	},
	.id_table	= vpbe_osd_devtype
};

module_platform_driver(osd_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DaVinci OSD Manager Driver");
MODULE_AUTHOR("Texas Instruments");
