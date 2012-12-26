/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "ebios_lcdc_tve.h"
#include "de_lcdc_i.h"
#include "disp_display.h"

static __u32 lcdc_reg_base0;
static __u32 lcdc_reg_base1;

#define ____SEPARATOR_LCDC____

__s32 LCDC_set_reg_base(__u32 sel, __u32 address)
{
	if (sel == 0)
		lcdc_reg_base0 = address;
	else if (sel == 1)
		lcdc_reg_base1 = address;

	return 0;
}

__u32 LCDC_get_reg_base(__u32 sel)
{
	if (sel == 0)
		return lcdc_reg_base0;
	else if (sel == 1)
		return lcdc_reg_base1;

	return 0;
}

__s32 LCDC_init(__u32 sel)
{
	TCON0_close(sel);
	TCON1_close(sel);

	LCDC_enable_int(sel, LCDC_VBI_LCD_EN);
	LCDC_enable_int(sel, LCDC_VBI_HD_EN);
	LCDC_enable_int(sel, LCDC_LTI_LCD_EN);
	LCDC_enable_int(sel, LCDC_LTI_HD_EN);

	TCON0_select_src(sel, LCDC_SRC_DE1);
	TCON1_select_src(sel, LCDC_SRC_DE1);

	LCDC_open(sel);

	return 0;
}

__s32 LCDC_exit(__u32 sel)
{
	LCDC_disable_int(sel, LCDC_VBI_LCD_EN | LCDC_VBI_HD_EN |
			 LCDC_LTI_LCD_EN | LCDC_LTI_HD_EN);
	LCDC_close(sel);
	return 0;
}

void LCDC_open(__u32 sel)
{
	LCDC_SET_BIT(sel, LCDC_DCLK_OFF,
		     LCDC_BIT31 | LCDC_BIT30 | LCDC_BIT29 | LCDC_BIT28);
	LCDC_SET_BIT(sel, LCDC_GCTL_OFF, LCDC_BIT31);
}

void LCDC_close(__u32 sel)
{
	LCDC_CLR_BIT(sel, LCDC_DCLK_OFF,
		     LCDC_BIT31 | LCDC_BIT30 | LCDC_BIT29 | LCDC_BIT28);
	LCDC_CLR_BIT(sel, LCDC_GCTL_OFF, LCDC_BIT31);
}

__s32 LCDC_set_start_delay(__u32 sel, __u32 tcon_index, __u8 delay)
{
	__u32 tmp;

	if (tcon_index == 0) {
		/* clears bits 8:4 */
		tmp = LCDC_RUINT32(sel, LCDC_CTL_OFF) & 0xfffffe0f;
		tmp |= ((delay & 0x1f) << 4);
		LCDC_WUINT32(sel, LCDC_CTL_OFF, tmp);
	} else if (tcon_index == 1) {
		/* clear bit8:4 */
		tmp = LCDC_RUINT32(sel, LCDC_HDTVIF_OFF) & 0xfffffe0f;
		tmp |= ((delay & 0x1f) << 4);
		LCDC_WUINT32(sel, LCDC_HDTVIF_OFF, tmp);
	}
	return 0;
}

__s32 LCDC_get_start_delay(__u32 sel, __u32 tcon_index)
{
	__u32 tmp;

	if (tcon_index == 0) {
		tmp = LCDC_RUINT32(sel, LCDC_CTL_OFF) & 0x000001f0;
		tmp >>= 4;
		return tmp;
	} else if (tcon_index == 1) {
		tmp = LCDC_RUINT32(sel, LCDC_HDTVIF_OFF) & 0x000001f0;
		tmp >>= 4;
		return tmp;
	}

	return 0;
}

__u32 LCDC_get_cur_line(__u32 sel, __u32 tcon_index)
{
	__u32 tmp;

	if (tcon_index == 0) {
		tmp = LCDC_RUINT32(sel, LCDC_DUBUG_OFF) & 0x03ff0000;
		tmp >>= 16;
	} else {
		tmp = LCDC_RUINT32(sel, LCDC_DUBUG_OFF) & 0x00000fff;
	}

	return tmp;
}

__s32 LCDC_set_int_line(__u32 sel, __u32 tcon_index, __u32 num)
{
	__u32 tmp = 0;

	tmp = LCDC_RUINT32(sel, LCDC_GINT0_OFF);

	if (tcon_index == 0) {
		LCDC_CLR_BIT(sel, LCDC_GINT0_OFF, 1 << 29);
		LCDC_INIT_BIT(sel, LCDC_GINT1_OFF, 0x7ff << 16, num << 16);
	} else {
		LCDC_CLR_BIT(sel, LCDC_GINT0_OFF, 1 << 28);
		LCDC_INIT_BIT(sel, LCDC_GINT1_OFF, 0x7ff, num);
	}

	LCDC_WUINT32(sel, LCDC_GINT0_OFF, tmp);

	return 0;
}

__s32 LCDC_enable_int(__u32 sel, __u32 irqsrc)
{
	LCDC_SET_BIT(sel, LCDC_GINT0_OFF, irqsrc);
	return 0;
}

__s32 LCDC_disable_int(__u32 sel, __u32 irqsrc)
{
	LCDC_CLR_BIT(sel, LCDC_GINT0_OFF, irqsrc);
	return 0;
}

__u32 LCDC_query_int(__u32 sel)
{
	__u32 tmp;

	tmp = LCDC_RUINT32(sel, LCDC_GINT0_OFF) & 0x0000f000;

	return tmp;
}

__s32 LCDC_clear_int(__u32 sel, __u32 irqsrc)
{
	LCDC_CLR_BIT(sel, LCDC_GINT0_OFF, irqsrc);
	return 0;
}

__s32 LCDC_get_timing(__u32 sel, __u32 index, __disp_tcon_timing_t *tt)
{
	__u32 reg0, reg1, reg2, reg3;
	__u32 x, y, ht, hbp, vt, vbp, hspw, vspw;

	if (index == 0) {
		reg0 = LCDC_RUINT32(sel, LCDC_BASIC0_OFF);
		reg1 = LCDC_RUINT32(sel, LCDC_BASIC1_OFF);
		reg2 = LCDC_RUINT32(sel, LCDC_BASIC2_OFF);
		reg3 = LCDC_RUINT32(sel, LCDC_BASIC3_OFF);
	} else {
		reg0 = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
		reg1 = LCDC_RUINT32(sel, LCDC_HDTV3_OFF);
		reg2 = LCDC_RUINT32(sel, LCDC_HDTV4_OFF);
		reg3 = LCDC_RUINT32(sel, LCDC_HDTV5_OFF);
	}
	x = (reg0 >> 16) & 0x7ff;
	y = (reg0 >> 0) & 0x7ff;
	ht = (reg1 >> 16) & 0xfff;
	hbp = (reg1 >> 0) & 0xfff;
	vt = (reg2 >> 16) & 0xfff;
	vbp = (reg2 >> 0) & 0xfff;
	hspw = (reg3 >> 16) & 0x3ff;
	vspw = (reg3 >> 0) & 0x3ff;

	/* left margin */
	tt->hor_back_porch = (hbp + 1) - (hspw + 1);
	/* right margin */
	tt->hor_front_porch = (ht + 1) - (x + 1) - (hbp + 1);
	/* upper margin */
	tt->ver_back_porch = (vbp + 1) - (vspw + 1);
	/* lower margin */
	tt->ver_front_porch = (vt / 2) - (y + 1) - (vbp + 1);
	/* hsync_len */
	tt->hor_sync_time = (hspw + 1);
	/* vsync_len */
	tt->ver_sync_time = (vspw + 1);

	return 0;
}

#define ____SEPARATOR_TCON0____

__s32 TCON0_open(__u32 sel)
{
	LCDC_SET_BIT(sel, LCDC_CTL_OFF, LCDC_BIT31);
	return 0;
}

__s32 TCON0_close(__u32 sel)
{
	LCDC_CLR_BIT(sel, LCDC_CTL_OFF, LCDC_BIT31);
	LCDC_WUINT32(sel, LCDC_IOCTL1_OFF, 0xffffffff);	/* ? */
	return 0;
}

void TCON0_cfg(__u32 sel, __panel_para_t *info)
{
	__u32 vblank_len;
	__u32 lcd_if_reg = 0;
	__u32 lcd_hv_if_tmp = 0;
	__u32 lcd_hv_smode_tmp = 0;

	vblank_len = info->lcd_vt / 2 - info->lcd_y;

	if (vblank_len >= 32)
		info->start_delay = 30;
	else
		info->start_delay = vblank_len - 2;

	switch (info->lcd_if) {
	case LCDC_LCDIF_HV:
		lcd_if_reg = 0;
		break;
	case LCDC_LCDIF_CPU:
		lcd_if_reg = 1;
		break;
	case LCDC_LCDIF_TTL:
		lcd_if_reg = 2;
		break;
	case LCDC_LCDIF_LVDS:
		lcd_if_reg = 0;
		break;
	}
	if (info->lcd_hv_if == 0) {
		lcd_hv_if_tmp = 0;
		lcd_hv_smode_tmp = 0;
	} else if (info->lcd_hv_if == 1) {
		lcd_hv_if_tmp = 1;
		lcd_hv_smode_tmp = 0;
	} else if (info->lcd_hv_if == 2) {
		lcd_hv_if_tmp = 1;
		lcd_hv_smode_tmp = 1;
	}

	LCDC_INIT_BIT(sel, LCDC_CTL_OFF, 0x0ffffff0,
		      (lcd_if_reg << 24) | (info->
					    lcd_swap << 23) | (0 << 20) |
		      (info->start_delay << 4));

	LCDC_SET_BIT(sel, LCDC_DCLK_OFF, (__u32) 1 << 31);

	LCDC_WUINT32(sel, LCDC_BASIC0_OFF,
		     ((info->lcd_x - 1) << 16) | (info->lcd_y - 1));

	LCDC_WUINT32(sel, LCDC_BASIC1_OFF,
		     ((info->lcd_ht - 1) << 16) | (info->lcd_hbp - 1));

	LCDC_WUINT32(sel, LCDC_BASIC2_OFF,
		     (info->lcd_vt << 16) | (info->lcd_vbp - 1));

	if (info->lcd_if == LCDC_LCDIF_HV) {
		__u32 hspw_tmp = info->lcd_hv_hspw;
		__u32 vspw_tmp = info->lcd_hv_vspw;

		if (info->lcd_hv_hspw != 0)
			hspw_tmp--;
		if (info->lcd_hv_vspw != 0)
			vspw_tmp--;
		LCDC_WUINT32(sel, LCDC_BASIC3_OFF, (hspw_tmp << 16) | vspw_tmp);

		LCDC_WUINT32(sel, LCDC_HVIF_OFF,
			     (lcd_hv_if_tmp << 31) | (lcd_hv_smode_tmp << 30) |
			     (info->lcd_hv_srgb_seq0 << 26) |
			     (info->lcd_hv_srgb_seq1 << 24) |
			     (info->lcd_hv_syuv_seq << 22) |
			     (info->lcd_hv_syuv_fdly << 20));
	} else if (info->lcd_if == LCDC_LCDIF_TTL) {
		LCDC_WUINT32(sel, LCDC_TTL0_OFF,
			     (info->lcd_ttl_stvh << 20) |
			     (info->lcd_ttl_stvdl << 10) |
			     (info->lcd_ttl_stvdp));

		LCDC_WUINT32(sel, LCDC_TTL1_OFF,
			     (info->lcd_ttl_ckvt << 30) |
			     (info->lcd_ttl_ckvh << 10) |
			     (info->lcd_ttl_ckvd << 0));

		LCDC_WUINT32(sel, LCDC_TTL2_OFF,
			     (info->lcd_ttl_oevt << 30) |
			     (info->lcd_ttl_oevh << 10) |
			     (info->lcd_ttl_oevd << 0));

		LCDC_WUINT32(sel, LCDC_TTL3_OFF,
			     (info->lcd_ttl_sthh << 26) |
			     (info->lcd_ttl_sthd << 16) |
			     (info->lcd_ttl_oehh << 10) |
			     (info->lcd_ttl_oehd << 0));

		LCDC_WUINT32(sel, LCDC_TTL4_OFF,
			     (info->lcd_ttl_datarate << 23) |
			     (info->lcd_ttl_revsel << 22) |
			     (info->lcd_ttl_datainv_en << 21) |
			     (info->lcd_ttl_datainv_sel << 20) |
			     info->lcd_ttl_revd);

	} else if (info->lcd_if == LCDC_LCDIF_CPU) {
		LCDC_WUINT32(sel, LCDC_CPUIF_OFF,
			     (info->lcd_cpu_if << 29) |
			     (1 << 26));
	} else if (info->lcd_if == LCDC_LCDIF_LVDS) {
		LCDC_WUINT32(sel, LCDC_LVDS_OFF,
			     (info->lcd_lvds_ch << 30) |
			     (0 << 29) | (0 << 28) |
			     (info->lcd_lvds_mode << 27) |
			     (info->lcd_lvds_bitwidth << 26) | (0 << 23));

		if (info->lcd_lvds_io_cross != 0)
			LCDC_SET_BIT(sel, LCDC_LVDS_ANA1,
				     (0x1f << 21) | (0x1f << 5));
	}

	if (info->lcd_frm == LCDC_FRM_RGB666)
		LCDC_CLR_BIT(sel, LCDC_FRM0_OFF, (__u32) 0x7 << 4);
	else if (info->lcd_frm == LCDC_FRM_RGB656)
		LCDC_INIT_BIT(sel, LCDC_FRM0_OFF, 0x7 << 4, 0x5 << 4);
	else
		LCDC_CLR_BIT(sel, LCDC_FRM0_OFF, LCDC_BIT31);

	if (info->lcd_frm == LCDC_FRM_RGB666 ||
	    info->lcd_frm == LCDC_FRM_RGB656) {
		LCDC_WUINT32(sel, LCDC_FRM1_OFF + 0x00, 0x11111111);
		LCDC_WUINT32(sel, LCDC_FRM1_OFF + 0x04, 0x11111111);
		LCDC_WUINT32(sel, LCDC_FRM1_OFF + 0x08, 0x11111111);
		LCDC_WUINT32(sel, LCDC_FRM1_OFF + 0x0c, 0x11111111);
		LCDC_WUINT32(sel, LCDC_FRM1_OFF + 0x10, 0x11111111);
		LCDC_WUINT32(sel, LCDC_FRM1_OFF + 0x14, 0x11111111);
		LCDC_WUINT32(sel, LCDC_FRM2_OFF + 0x00, 0x01010000);
		LCDC_WUINT32(sel, LCDC_FRM2_OFF + 0x04, 0x15151111);
		LCDC_WUINT32(sel, LCDC_FRM2_OFF + 0x08, 0x57575555);
		LCDC_WUINT32(sel, LCDC_FRM2_OFF + 0x0c, 0x7f7f7777);
		LCDC_SET_BIT(sel, LCDC_FRM0_OFF, LCDC_BIT31);
	}

	if (info->lcd_gamma_correction_en) {
		TCON1_set_gamma_table(sel, (__u32) (info->lcd_gamma_tbl), 1024);
		TCON1_set_gamma_Enable(sel, 1);
	}
#ifdef CONFIG_ARCH_SUN4I
	else
		TCON1_set_gamma_Enable(sel, 0);
#endif

	LCDC_WUINT32(sel, LCDC_IOCTL0_OFF, info->lcd_io_cfg0);
	LCDC_WUINT32(sel, LCDC_IOCTL1_OFF, info->lcd_io_cfg1);

	LCDC_set_int_line(sel, 0, info->start_delay + 2);
}

__s32 TCON0_select_src(__u32 sel, enum lcdc_src src)
{
	__u32 tmp;

	tmp = LCDC_RUINT32(sel, LCDC_CTL_OFF);
	tmp = tmp & 0xffbffffc;

	switch (src) {
	case LCDC_SRC_DE1:
		tmp = tmp | 0x00;
		break;
	case LCDC_SRC_DE2:
		tmp = tmp | 0x01;
		break;
	case LCDC_SRC_DMA:
		tmp = tmp | 0x02;
		break;
	case LCDC_SRC_WHITE:
		tmp = tmp | 0x00400003;
		break;
	case LCDC_SRC_BLACK:
		tmp = tmp | 0x03;
		break;
	default:
		pr_warn("%s: unknown source %d\n", __func__, src);
		break;
	}

	LCDC_WUINT32(sel, LCDC_CTL_OFF, tmp);
	return 0;
}

__s32 TCON0_get_width(__u32 sel)
{
	return -1;
}

__s32 TCON0_get_height(__u32 sel)
{
	return -1;
}

__s32 TCON0_set_dclk_div(__u32 sel, __u8 div)
{
	LCDC_INIT_BIT(sel, LCDC_DCLK_OFF, 0xff, div);
	return 0;
}

__u32 TCON0_get_dclk_div(__u32 sel)
{
	__u32 tmp;

	tmp = LCDC_RUINT32(sel, LCDC_DCLK_OFF) & 0xff;

	return tmp;
}

#define ____SEPARATOR_TCON1____

__u32 TCON1_open(__u32 sel)
{
	LCDC_SET_BIT(sel, LCDC_HDTVIF_OFF, LCDC_BIT31);
	return 0;
}

__u32 TCON1_close(__u32 sel)
{
	__u32 tmp;

	LCDC_CLR_BIT(sel, LCDC_HDTVIF_OFF, LCDC_BIT31);

	tmp = LCDC_RUINT32(sel, LCDC_GCTL_OFF);	/* ? */
	tmp &= (~(1 << 0));	/* disable hdif */
	LCDC_WUINT32(sel, LCDC_GCTL_OFF, tmp);

	LCDC_WUINT32(sel, LCDC_IOCTL3_OFF, 0xffffffff);	/* ? */

#ifdef CONFIG_ARCH_SUN5I
	LCDC_CLR_BIT(sel, LCDC_MUX_CTRL, 1 << 0);
#endif

	return 0;
}

__u32 TCON1_cfg(__u32 sel, __tcon1_cfg_t *cfg)
{
	__u32 vblank_len;
	__u32 reg_val;

	vblank_len = cfg->vt / 2 - cfg->src_y - 2;
	if (vblank_len >= 32)
		cfg->start_delay = 30;
	else
		cfg->start_delay = vblank_len - 2; /* was vblank_len - 1 */

	if (cfg->b_remap_if)
		LCDC_SET_BIT(sel, LCDC_GCTL_OFF, LCDC_BIT0);
	else
		LCDC_CLR_BIT(sel, LCDC_GCTL_OFF, LCDC_BIT0);

	reg_val = LCDC_RUINT32(sel, LCDC_HDTVIF_OFF);
	reg_val &= 0xffeffe0f;
	if (cfg->b_interlace)
		reg_val |= (1 << 20);

	reg_val |= ((cfg->start_delay & 0x1f) << 4);

	LCDC_WUINT32(sel, LCDC_HDTVIF_OFF, reg_val);

	LCDC_WUINT32(sel, LCDC_HDTV0_OFF, (((cfg->src_x - 1) & 0xfff) << 16) |
		     ((cfg->src_y - 1) & 0xfff));
	LCDC_WUINT32(sel, LCDC_HDTV1_OFF, (((cfg->scl_x - 1) & 0xfff) << 16) |
		     ((cfg->scl_y - 1) & 0xfff));
	LCDC_WUINT32(sel, LCDC_HDTV2_OFF, (((cfg->out_x - 1) & 0xfff) << 16) |
		     ((cfg->out_y - 1) & 0xfff));
	LCDC_WUINT32(sel, LCDC_HDTV3_OFF, (((cfg->ht - 1) & 0xffff) << 16) |
		     ((cfg->hbp - 1) & 0xfff));
	LCDC_WUINT32(sel, LCDC_HDTV4_OFF, (((cfg->vt) & 0xffff) << 16) |
		     ((cfg->vbp - 1) & 0xfff));
	LCDC_WUINT32(sel, LCDC_HDTV5_OFF, (((cfg->hspw - 1) & 0x3ff) << 16) |
		     ((cfg->vspw - 1) & 0x3ff));
	LCDC_WUINT32(sel, LCDC_IOCTL2_OFF, cfg->io_pol); /* add */
	LCDC_WUINT32(sel, LCDC_IOCTL3_OFF, cfg->io_out); /* add */

	LCDC_set_int_line(sel, 1, cfg->start_delay + 2);

	return 0;
}

__u32 TCON1_cfg_ex(__u32 sel, __panel_para_t *info)
{
	__tcon1_cfg_t tcon1_cfg;

	tcon1_cfg.b_interlace = 0;
	tcon1_cfg.b_rgb_internal_hd = 0;
	tcon1_cfg.b_rgb_remap_io = 1; /* rgb */
	tcon1_cfg.b_remap_if = 1; /* remap tcon1 to io */
	tcon1_cfg.src_x = info->lcd_x;
	tcon1_cfg.src_y = info->lcd_y;
	tcon1_cfg.scl_x = info->lcd_x;
	tcon1_cfg.scl_y = info->lcd_y;
	tcon1_cfg.out_x = info->lcd_x;
	tcon1_cfg.out_y = info->lcd_y;
	tcon1_cfg.ht = info->lcd_ht;
	tcon1_cfg.hbp = info->lcd_hbp;
	tcon1_cfg.vt = info->lcd_vt;
	tcon1_cfg.vbp = info->lcd_vbp;
	tcon1_cfg.vspw = info->lcd_hv_vspw;
	tcon1_cfg.hspw = info->lcd_hv_hspw;
	tcon1_cfg.io_pol = info->lcd_io_cfg0;
	tcon1_cfg.io_out = info->lcd_io_cfg1;

	TCON1_cfg(sel, &tcon1_cfg);

	return 0;
}

__u32 TCON1_set_hdmi_mode(__u32 sel, __u8 mode)
{
	__tcon1_cfg_t cfg;
	struct __disp_video_timing video_timing;
	int extra_y;

	if (gdisp.init_para.hdmi_get_video_timing(mode, &video_timing) != 0)
		return 0;

	cfg.b_interlace = video_timing.I;
	cfg.src_x = video_timing.INPUTX;
	cfg.src_y = video_timing.INPUTY;
	cfg.scl_x = video_timing.INPUTX;
	cfg.scl_y = video_timing.INPUTY;
	cfg.out_x = video_timing.INPUTX;
	cfg.out_y = video_timing.INPUTY;
	cfg.ht    = video_timing.HT;
	cfg.hbp   = video_timing.HBP;
	cfg.hspw  = video_timing.HPSW;
	cfg.vt    = video_timing.VT;
	cfg.vbp   = video_timing.VBP;
	cfg.vspw  = video_timing.VPSW;
	cfg.io_pol = 0x04000000; /* HDG: unknow bit must be set */
	/*
	 * HDG: Note I'm not sure if I don't have Hsync versus Vsync swapped,
	 * does not matter for standard modes which are NN or PP, but could
	 * impact EDID.
	 */
	if (video_timing.HSYNC)
		cfg.io_pol |= 0x01000000; /* Positive Hsync */
	if (video_timing.VSYNC)
		cfg.io_pol |= 0x02000000; /* Positive Vsync */

	switch (mode) {
	case DISP_TV_MOD_1080P_24HZ_3D_FP:
	case DISP_TV_MOD_720P_50HZ_3D_FP:
	case DISP_TV_MOD_720P_60HZ_3D_FP:
		if (mode == DISP_TV_MOD_1080P_24HZ_3D_FP)
			extra_y = 45;
		else
			extra_y = 30;
		LCDC_WUINT32(sel, LCDC_3DF_A1B, (cfg.vt + 1) << 12);
		LCDC_WUINT32(sel, LCDC_3DF_A1E, (cfg.vt + extra_y) << 12);
		LCDC_WUINT32(sel, LCDC_3DF_D1, 0);
		LCDC_SET_BIT(sel, LCDC_3DF_CTL, 1 << 31);
		cfg.scl_y += extra_y;
		cfg.out_y += extra_y;
		cfg.vt *= 2;
		break;
	}

	if (!cfg.b_interlace)
		cfg.vt *= 2;

	cfg.io_out = 0x00000000;
	cfg.b_rgb_internal_hd = 0;
	cfg.b_rgb_remap_io = 1;	/* rgb */
	cfg.b_remap_if = 1;
	TCON1_cfg(sel, &cfg);
#ifdef CONFIG_ARCH_SUN4I
	TCON_set_hdmi_src(sel);
#endif

	return 0;
}

__u32 TCON1_set_tv_mode(__u32 sel, __u8 mode)
{
	__tcon1_cfg_t cfg;

	switch (mode) {
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL_NC:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
		cfg.b_interlace = 1;
		cfg.src_x = 720;
		cfg.src_y = 288;
		cfg.scl_x = 720;
		cfg.scl_y = 288;
		cfg.out_x = 720;
		cfg.out_y = 288;
		cfg.ht = 864;
		cfg.hbp = 139;
		cfg.vt = 625;
		cfg.vbp = 22;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_480I:
	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_NTSC_SVIDEO:
	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_M_SVIDEO:
		cfg.b_interlace = 1;
		cfg.src_x = 720;
		cfg.src_y = 240;
		cfg.scl_x = 720;
		cfg.scl_y = 240;
		cfg.out_x = 720;
		cfg.out_y = 240;
		cfg.ht = 858;
		cfg.hbp = 118;
		cfg.vt = 525;
		cfg.vbp = 18;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_480P:
		cfg.b_interlace = 0;
		cfg.src_x = 720;
		cfg.src_y = 480;
		cfg.scl_x = 720;
		cfg.scl_y = 480;
		cfg.out_x = 720;
		cfg.out_y = 480;
		cfg.ht = 858;
		cfg.hbp = 118;
		cfg.vt = 1050;
		cfg.vbp = 22;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_576P:
		cfg.b_interlace = 0;
		cfg.src_x = 720;
		cfg.src_y = 576;
		cfg.scl_x = 720;
		cfg.scl_y = 576;
		cfg.out_x = 720;
		cfg.out_y = 576;
		cfg.ht = 864;
		cfg.hbp = 139;
		cfg.vt = 1250;
		cfg.vbp = 22;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_720P_50HZ:
		cfg.b_interlace = 0;
		cfg.src_x = 1280;
		cfg.src_y = 720;
		cfg.scl_x = 1280;
		cfg.scl_y = 720;
		cfg.out_x = 1280;
		cfg.out_y = 720;
		cfg.ht = 1980;
		cfg.hbp = 260;
		cfg.vt = 1500;
		cfg.vbp = 24;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_720P_60HZ:
		cfg.b_interlace = 0;
		cfg.src_x = 1280;
		cfg.src_y = 720;
		cfg.scl_x = 1280;
		cfg.scl_y = 720;
		cfg.out_x = 1280;
		cfg.out_y = 720;
		cfg.ht = 1650;
		cfg.hbp = 260;
		cfg.vt = 1500;
		cfg.vbp = 24;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_1080I_50HZ:
		cfg.b_interlace = 0;
		cfg.src_x = 1920;
		cfg.src_y = 540;
		cfg.scl_x = 1920;
		cfg.scl_y = 540;
		cfg.out_x = 1920;
		cfg.out_y = 540;
		cfg.ht = 2640;
		cfg.hbp = 192;
		cfg.vt = 1125;
		cfg.vbp = 16;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_1080I_60HZ:
		cfg.b_interlace = 1;
		cfg.src_x = 1920;
		cfg.src_y = 540;
		cfg.scl_x = 1920;
		cfg.scl_y = 540;
		cfg.out_x = 1920;
		cfg.out_y = 540;
		cfg.ht = 2200;
		cfg.hbp = 192;
		cfg.vt = 1125;
		cfg.vbp = 16;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_1080P_50HZ:
		cfg.b_interlace = 0;
		cfg.src_x = 1920;
		cfg.src_y = 1080;
		cfg.scl_x = 1920;
		cfg.scl_y = 1080;
		cfg.out_x = 1920;
		cfg.out_y = 1080;
		cfg.ht = 2640;
		cfg.hbp = 192;
		cfg.vt = 2250;
		cfg.vbp = 44;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	case DISP_TV_MOD_1080P_60HZ:
		cfg.b_interlace = 0;
		cfg.src_x = 1920;
		cfg.src_y = 1080;
		cfg.scl_x = 1920;
		cfg.scl_y = 1080;
		cfg.out_x = 1920;
		cfg.out_y = 1080;
		cfg.ht = 2200;
		cfg.hbp = 192;
		cfg.vt = 2250;
		cfg.vbp = 44;
		cfg.vspw = 2;
		cfg.hspw = 2;
		break;

	default:
		return 0;
	}
	cfg.io_pol = 0x00000000;
	cfg.io_out = 0x0fffffff;
	cfg.b_rgb_internal_hd = 0; /* yuv */
	cfg.b_rgb_remap_io = 0;
	cfg.b_remap_if = 0;
	TCON1_cfg(sel, &cfg);

#ifdef CONFIG_ARCH_SUN4I
	TCON_set_tv_src(sel, sel);
#else
	LCDC_SET_BIT(sel, LCDC_MUX_CTRL, 1 << 0);
#endif

	return 0;
}

/*
 * set mode
 */
__s32 TCON1_set_vga_mode(__u32 sel, __u8 mode)
{
	__tcon1_cfg_t cfg;

	switch (mode) {
	case DISP_VGA_H640_V480:
		cfg.src_x = cfg.scl_x = cfg.out_x = 640; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 480; /* VA */
		cfg.ht = 0x320; /* HT - 1 = -1 */
		cfg.hbp = 0x90; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 0x41a; /* VT * 2 = * 2 */
		cfg.vbp = 0x22; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 0x2; /* VS - 1 = -1 */
		cfg.hspw = 0x60; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H800_V600:
		cfg.src_x = cfg.scl_x = cfg.out_x = 800; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 600; /* VA */
		cfg.ht = 0x420; /* HT - 1 = -1 */
		cfg.hbp = 0xd8; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 0x4e8; /* VT * 2 = * 2 */
		cfg.vbp = 0x1a; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 0x4; /* VS - 1 = -1 */
		cfg.hspw = 0x80; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H1024_V768:
		cfg.src_x = cfg.scl_x = cfg.out_x = 1024;
		cfg.src_y = cfg.scl_y = cfg.out_y = 768;
		cfg.ht = 1344; /* HT - 1 = 1344 - 1 */
		cfg.hbp = 296; /* HS + HBP - 1 = 136 + 160 - 1 */
		cfg.vt = 1612; /* VT * 2 = 806 * 2 */
		cfg.vbp = 34; /* VS + VBP - 1 = 6 + 29 - 1 */
		cfg.vspw = 6; /* VS - 1 = 6 - 1 */
		cfg.hspw = 136;	/* HS - 1 = 136 - 1 */
		break;
	case DISP_VGA_H1280_V1024:
		cfg.src_x = cfg.scl_x = cfg.out_x = 1280; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 1024; /* VA */
		cfg.ht = 0x698; /* HT - 1 = -1 */
		cfg.hbp = 0x168; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 0x854; /* VT * 2 = * 2 */
		cfg.vbp = 0x28; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 0x3; /* VS - 1 = -1 */
		cfg.hspw = 0x70; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H1360_V768:
		cfg.src_x = cfg.scl_x = cfg.out_x = 1360; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 768; /* VA */
		cfg.ht = 0x700; /* HT - 1 = -1 */
		cfg.hbp = 0x170; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 0x636; /* VT * 2 = * 2 */
		cfg.vbp = 0x17; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 0x6; /* VS - 1 = -1 */
		cfg.hspw = 0x70; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H1440_V900:
		cfg.src_x = cfg.scl_x = cfg.out_x = 1440; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 900; /* VA */
		cfg.ht = 0x770; /* HT - 1 = -1 */
		cfg.hbp = 0x180; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 0x74c; /* VT * 2 = * 2 */
		cfg.vbp = 0x1e; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 0x6; /* VS - 1 = -1 */
		cfg.hspw = 0x98; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H1680_V1050:
		cfg.src_x = cfg.scl_x = cfg.out_x = 1680; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 1050; /* VA */
		cfg.ht = 2240; /* HT - 1 = -1 */
		cfg.hbp = 464; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 2178; /* VT * 2 = * 2 */
		cfg.vbp = 35; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 6; /* VS - 1 = -1 */
		cfg.hspw = 176; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H1920_V1080_RB:
		cfg.src_x = cfg.scl_x = cfg.out_x = 1920; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 1080; /* VA */
		cfg.ht = 2017; /* HT - 1 = -1 */
		cfg.hbp = 63; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 2222; /* VT * 2 = * 2 */
		cfg.vbp = 27; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 5; /* VS - 1 = -1 */
		cfg.hspw = 32; /* HS - 1 = -1 */
		break;
	case DISP_VGA_H1920_V1080: /* TBD */
		cfg.src_x = cfg.scl_x = cfg.out_x = 1920; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 1080; /* VA */
		cfg.ht = 2200; /* HT - 1 = -1 */
		cfg.hbp = 148 + 44; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 1125 * 2; /* VT * 2 = * 2 */
		cfg.vbp = 36 + 5; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 5; /* VS - 1 = -1 */
		cfg.hspw = 44; /* HS - 1 = -1 */
		cfg.io_pol = 0x03000000;
		break;
	case DISP_VGA_H1280_V720: /* TBD */
		cfg.src_x = cfg.scl_x = cfg.out_x = 1280; /* HA */
		cfg.src_y = cfg.scl_y = cfg.out_y = 720; /* VA */
		cfg.ht = 1650; /* HT - 1 = -1 */
		cfg.hbp = 220 + 40; /* HS + HBP - 1 = +- 1 */
		cfg.vt = 750 * 2; /* VT * 2 = * 2 */
		cfg.vbp = 5 + 20; /* VS + VBP - 1 = +- 1 */
		cfg.vspw = 5; /* VS - 1 = -1 */
		cfg.hspw = 40; /* HS - 1 = -1 */
		cfg.io_pol = 0x03000000;
		break;
	default:
		return 0;
	}
	cfg.b_interlace = 0;
	cfg.io_pol = 0x00000000;
	cfg.io_out = 0x0cffffff; /* hs vs is use */
	cfg.b_rgb_internal_hd = 1; /* rgb */
	cfg.b_rgb_remap_io = 0;
	cfg.b_remap_if = 1;
	TCON1_cfg(sel, &cfg);

#ifdef CONFIG_ARCH_SUN4I
	TCON_set_tv_src(sel, sel);
#endif

	return 0;
}

__s32 TCON1_select_src(__u32 sel, enum lcdc_src src)
{
	__u32 tv_tmp;

	tv_tmp = LCDC_RUINT32(sel, LCDC_HDTVIF_OFF);

	tv_tmp = tv_tmp & 0xfffffffc;

	switch (src) {
	case LCDC_SRC_DE1:
		tv_tmp = tv_tmp | 0x00;
		break;
	case LCDC_SRC_DE2:
		tv_tmp = tv_tmp | 0x01;
		break;
	case LCDC_SRC_BLUE:
		tv_tmp = tv_tmp | 0x02;
		break;
	default:
		pr_warn("%s: unknown source %d\n", __func__, src);
		break;
	}

	LCDC_WUINT32(sel, LCDC_HDTVIF_OFF, tv_tmp);

	return 0;
}

/*
 * ???
 */
__bool TCON1_in_valid_regn(__u32 sel, __u32 juststd)
{
	__u32 readval;
	__u32 SY2;
	__u32 VT;

	readval = LCDC_RUINT32(sel, LCDC_HDTV4_OFF);
	VT = (readval & 0xffff0000) >> 17;

	readval = LCDC_RUINT32(sel, LCDC_DUBUG_OFF);
	SY2 = (readval) & 0xfff;

	if ((SY2 < juststd) || (SY2 > VT))
		return 1;
	else
		return 0;

}

__s32 TCON1_get_width(__u32 sel)
{
	return -1;
}

__s32 TCON1_get_height(__u32 sel)
{
	return -1;
}

/*
 * add nex time
 */
__s32 TCON1_set_gamma_table(__u32 sel, __u32 address, __u32 size)
{
	__u32 tmp;

	__s32 *pmem_align_dest;
	__s32 *pmem_align_src;
	__s32 *pmem_dest_cur;

	tmp = LCDC_RUINT32(sel, LCDC_GCTL_OFF);
	/* disable gamma correction sel */
	LCDC_WUINT32(sel, LCDC_GCTL_OFF, tmp & (~(1 << 30)));

	pmem_dest_cur = (__s32 *)
		(LCDC_get_reg_base(sel) + LCDC_GAMMA_TABLE_OFF);
	pmem_align_src = (__s32 *) address;
	pmem_align_dest = pmem_dest_cur + (size >> 2);

	while (pmem_dest_cur < pmem_align_dest)
		*(volatile __u32 *)pmem_dest_cur++ = *pmem_align_src++;

	LCDC_WUINT32(sel, LCDC_GCTL_OFF, tmp);

	return 0;
}

__s32 TCON1_set_gamma_Enable(__u32 sel, __bool enable)
{
	__u32 tmp;

	tmp = LCDC_RUINT32(sel, LCDC_GCTL_OFF);
	if (enable)
		LCDC_WUINT32(sel, LCDC_GCTL_OFF, tmp | (1 << 30));
	else
		LCDC_WUINT32(sel, LCDC_GCTL_OFF, tmp & (~(1 << 30)));

	return 0;
}

#define ____SEPARATOR_CPU____

#if 0
__asm void my_stmia(int addr, int data1, int data2)
{
	stmia r0!, {r1,r2}
	BX    lr
}
#endif

#ifdef UNUSED
static void
LCD_CPU_Burst_Write(__u32 sel, int addr, int data1, int data2)
{
	//my_stmia(LCDC_GET_REG_BASE(sel) + addr,data1,data2);
}
#endif /* UNUSED */

static __u32
LCD_CPU_Busy(__u32 sel)
{
#ifdef CONFIG_ARCH_SUN4I
	volatile __u32 i;
	__u32 counter = 0;
	__u32 reg_val;

	LCDC_SET_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT0);
	for (i = 0; i < 80; i++)
		;

	while (1) {
		reg_val = LCDC_RUINT32(sel, LCDC_CPUIF_OFF);
		if (reg_val & 0x00c00000) {
			if (counter > 200)
				return 0;
			else
				counter++;
		} else {
			return 0;
		}
	}
#else
	return LCDC_RUINT32(sel, LCDC_CPUIF_OFF) & (LCDC_BIT23 | LCDC_BIT22);
#endif /* CONFIG_ARCH_SUN4I */
}

static void
LCD_CPU_WR_INDEX_24b(__u32 sel, __u32 index)
{
	while (LCD_CPU_Busy(sel)) /* check wr finish */
		;
	LCDC_CLR_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT25); /* ca = 0 */
	while (LCD_CPU_Busy(sel)) /* check wr finish */
		;
	/* write data on 8080 bus */
	LCDC_WUINT32(sel, LCDC_CPUWR_OFF, index);

#if 0
	while (LCD_CPU_Busy(sel)) /* check wr finish */
		;
#endif
}

static void
LCD_CPU_WR_DATA_24b(__u32 sel, __u32 data)
{
	while (LCD_CPU_Busy(sel)) /* check wr finish */
		;
	LCDC_SET_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT25); /* ca = 1 */
	while (LCD_CPU_Busy(sel)) /* check wr finish */
		;
	LCDC_WUINT32(sel, LCDC_CPUWR_OFF, data);

#if 0
	while (LCD_CPU_Busy(sel)) /* check wr finish */
		;
#endif
}

static void
LCD_CPU_WR_24b(__u32 sel, __u32 index, __u32 data)
{
	LCD_CPU_WR_INDEX_24b(sel, index);
	LCD_CPU_WR_DATA_24b(sel, data);
}

#ifdef UNUSED
static void
LCD_CPU_RD_24b(__u32 sel, __u32 index, __u32 *data)
{
}
#endif

/*
 * 16bit
 */
static __u32
LCD_CPU_IO_extend_16b(__u32 value)
{
	return ((value & 0xfc00) << 8) |
		((value & 0x0300) << 6) |
		((value & 0x00e0) << 5) |
		((value & 0x001f) << 3);
}

#ifdef UNUSED
static __u32
LCD_CPU_IO_shrink_16b(__u32 value)
{
	return ((value & 0xfc0000) >> 8) |
		((value & 0x00c000) >> 6) |
		((value & 0x001c00) >> 5) |
		((value & 0x0000f8) >> 3);
}
#endif

void LCD_CPU_WR(__u32 sel, __u32 index, __u32 data)
{
	LCD_CPU_WR_24b(sel, LCD_CPU_IO_extend_16b(index),
		       LCD_CPU_IO_extend_16b(data));
}
EXPORT_SYMBOL(LCD_CPU_WR);

void LCD_CPU_WR_INDEX(__u32 sel, __u32 index)
{
	LCD_CPU_WR_INDEX_24b(sel, LCD_CPU_IO_extend_16b(index));
}
EXPORT_SYMBOL(LCD_CPU_WR_INDEX);

void LCD_CPU_WR_DATA(__u32 sel, __u32 data)
{
	LCD_CPU_WR_DATA_24b(sel, LCD_CPU_IO_extend_16b(data));
}
EXPORT_SYMBOL(LCD_CPU_WR_DATA);

void LCD_CPU_RD(__u32 sel, __u32 index, __u32 *data)
{
}

void LCD_CPU_AUTO_FLUSH(__u32 sel, __u8 en)
{
	if (en == 0)
		LCDC_CLR_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT28);
	else
		LCDC_SET_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT28);
}
EXPORT_SYMBOL(LCD_CPU_AUTO_FLUSH);

void LCD_CPU_DMA_FLUSH(__u32 sel, __u8 en)
{
	if (en == 0)
		LCDC_CLR_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT27);
	else
		LCDC_SET_BIT(sel, LCDC_CPUIF_OFF, LCDC_BIT27);
}

void LCD_XY_SWAP(__u32 sel)
{
	__u32 reg, x, y;

	reg = LCDC_RUINT32(sel, LCDC_BASIC0_OFF);
	y = reg & 0x7ff;
	x = (reg >> 16) & 0x7ff;
	LCDC_WUINT32(sel, LCDC_BASIC0_OFF, (y << 16) | x);
}

__s32 LCD_LVDS_open(__u32 sel)
{
	__u32 i;

	LCDC_SET_BIT(sel, LCDC_LVDS_OFF, (__u32) 1 << 31);
	LCDC_SET_BIT(sel, LCDC_LVDS_ANA0, 0x3F310000);
	LCDC_SET_BIT(sel, LCDC_LVDS_ANA0, 1 << 22);
	for (i = 0; i < 1200; i++) /* 1200ns */
		;
	LCDC_SET_BIT(sel, LCDC_LVDS_ANA1, 0x1f << 26 | 0x1f << 10);
	for (i = 0; i < 120; i++) /* 120ns */
		;
	LCDC_SET_BIT(sel, LCDC_LVDS_ANA1, 0x1f << 16 | 0x1f << 00);
	LCDC_SET_BIT(sel, LCDC_LVDS_ANA0, 1 << 22);

	return 0;
}

__s32 LCD_LVDS_close(__u32 sel)
{
	LCDC_CLR_BIT(sel, LCDC_LVDS_ANA1, 0x1f << 16 | 0x1f << 00);
	LCDC_CLR_BIT(sel, LCDC_LVDS_ANA1, 0x1f << 26 | 0x1f << 10);
	LCDC_CLR_BIT(sel, LCDC_LVDS_ANA0, 1 << 22);
	LCDC_CLR_BIT(sel, LCDC_LVDS_ANA0, 0x3F310000);
	LCDC_CLR_BIT(sel, LCDC_LVDS_OFF, (__u32) 1 << 31);
	return 0;
}

#ifdef CONFIG_ARCH_SUN4I
#define ____TCON_MUX_CTL____

__u8 TCON_mux_init(void)
{
	LCDC_CLR_BIT(0, LCDC_MUX_CTRL, LCDC_BIT31);
	LCDC_INIT_BIT(0, LCDC_MUX_CTRL, 0xf << 4, 0 << 4);
	LCDC_INIT_BIT(0, LCDC_MUX_CTRL, 0xf, 1);
	return 0;
}

__u8 TCON_set_hdmi_src(__u8 src)
{
	LCDC_INIT_BIT(0, LCDC_MUX_CTRL, 0x3 << 8, src << 8);
	return 0;
}

__u8 TCON_set_tv_src(__u32 tv_index, __u8 src)
{
	if (tv_index == 0)
		LCDC_INIT_BIT(0, LCDC_MUX_CTRL, 0x3 << 4, src << 4);
	else
		LCDC_INIT_BIT(0, LCDC_MUX_CTRL, 0x3 << 0, src << 0);

	return 0;
}
#endif /* CONFIG_ARCH_SUN4I */

#ifdef UNUSED
#define ____TCON_CEU____

static __u32 range_cut(__s32 *x_value, __s32 x_min, __s32 x_max)
{
	if (*x_value > x_max) {
		*x_value = x_max;
		return 1;
	} else if (*x_value < x_min) {
		*x_value = x_min;
		return 1;
	} else
		return 0;
}

static void rect_multi(__s32 *dest, __s32 *src1, __s32 *src2)
{
	__u32 x, y, z;
	__s64 val_int64;

	for (x = 0; x < 4; x++)
		for (y = 0; y < 4; y++) {
			val_int64 = 0;
			for (z = 0; z < 4; z++)
				val_int64 += (__s64)
					src1[x * 4 + z] * src2[z * 4 + y];
			val_int64 = (val_int64 + 512) >> 10;
			dest[x * 4 + y] = val_int64;
		}
}

static __s32 reg_corr(__s32 val, __u32 bit)
{
	if (val >= 0)
		return val;
	else
		return (bit) | (__u32) (-val);
}

static void rect_ceu_pro(__s32 *p_rect, __s32 b, __s32 c, __s32 s, __s32 h)
{
	const __u8 table_sin[91] = {
		0, 2, 4, 7, 9, 11, 13, 16, 18, 20,
		22, 24, 27, 29, 31, 33, 35, 37, 40, 42,
		44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
		64, 66, 68, 70, 72, 73, 75, 77, 79, 81,
		82, 84, 86, 87, 89, 91, 92, 94, 95, 97,
		98, 99, 101, 102, 104, 105, 106, 107, 109, 110,
		111, 112, 113, 114, 115, 116, 117, 118, 119, 119,
		120, 121, 122, 122, 123, 124, 124, 125, 125, 126,
		126, 126, 127, 127, 127, 128, 128, 128, 128, 128,
		128
	};

	const __s32 f_csh = 1024;
	const __s32 f_sh = 8;
	__s32 h1 = 0, h2 = 0, h3 = 0, h4 = 0;

	if (h >= 0 && h < 90) {
		h1 = table_sin[90 - h];
		h2 = table_sin[h];
		h3 = -table_sin[h];
		h4 = table_sin[90 - h];
	} else if (h >= 90 && h < 180) {
		h1 = -table_sin[h - 90];
		h2 = table_sin[180 - h];
		h3 = -table_sin[180 - h];
		h4 = -table_sin[h - 90];
	} else if (h >= 180 && h < 270) {
		h1 = -table_sin[270 - h];
		h2 = -table_sin[h - 180];
		h3 = table_sin[h - 180];
		h4 = -table_sin[270 - h];
	} else if (h >= 270 && h <= 360) {
		h1 = table_sin[h - 270];
		h2 = -table_sin[360 - h];
		h3 = table_sin[360 - h];
		h4 = table_sin[h - 270];
	}

	p_rect[0] = c * f_sh;
	p_rect[1] = 0;
	p_rect[2] = 0;
	p_rect[3] = -16 * c * f_sh + (b + 16) * f_csh;
	p_rect[4] = 0;
	p_rect[5] = (c * s * h1) >> 11;
	p_rect[6] = (c * s * h2) >> 11;
	p_rect[7] = 128 * (1 * f_csh - p_rect[5] - p_rect[6]);
	p_rect[8] = 0;
	p_rect[9] = (c * s * h3) >> 11;
	p_rect[10] = (c * s * h4) >> 11;
	p_rect[11] = 128 * (1 * f_csh - p_rect[9] - p_rect[10]);
	p_rect[12] = 0;
	p_rect[13] = 0;
	p_rect[14] = 0;
	p_rect[15] = 1024;
}

static void lcd_ceu(__u32 r2y_type, __u32 cen_type, __u32 y2r_type, __s32 b,
		    __s32 c, __s32 s, __s32 h, __s32 *p_coff)
{
	const __s32 rect_1[16] = {
		1024, 0, 0, 0,
		0, 1024, 0, 0,
		0, 0, 1024, 0,
		0, 0, 0, 1024
	};

	const __s32 rect_r2y_sd[16] = {
		263, 516, 100, 16384,
		-152, -298, 450, 131072,
		450, -377, -73, 131072,
		0, 0, 0, 1024
	};

	const __s32 rect_r2y_hd[16] = {
		187, 629, 63, 16384,
		-103, -346, 450, 131072,
		450, -409, -41, 131072,
		0, 0, 0, 1024
	};

	const __s32 rect_y2r_sd[16] = {
		1192, 0, 1634, -228262,
		1192, -400, -833, 138740,
		1192, 2066, 0, -283574,
		0, 0, 0, 1024
	};

	const __s32 rect_y2r_hd[16] = {
		1192, 0, 1836, -254083,
		1192, -218, -547, 78840,
		1192, 2166, 0, -296288,
		0, 0, 0, 1024
	};

	__s32 rect_tmp0[16];
	__s32 rect_tmp1[16];

	__s32 *p_rect = 0;
	__s32 *p_r2y = 0;
	__s32 *p_y2r = 0;
	__s32 *p_ceu = 0;
	__u32 i = 0;

	if (r2y_type) {
		if (r2y_type == 1)
			p_r2y = (__s32 *) rect_r2y_sd;
		else if (r2y_type == 2)
			p_r2y = (__s32 *) rect_r2y_hd;
		p_rect = p_r2y;
	} else
		p_rect = (__s32 *) rect_1;

	if (cen_type) {
		range_cut(&b, -600, 600);
		range_cut(&c, 0, 300);
		range_cut(&s, 0, 300);
		range_cut(&h, 0, 360);
		p_ceu = rect_tmp1;
		rect_ceu_pro(p_ceu, b, c, s, h);
		rect_multi(rect_tmp0, p_ceu, p_rect);
		p_rect = rect_tmp0;
	}

	if (y2r_type) {
		if (y2r_type == 1)
			p_y2r = (__s32 *) rect_y2r_sd;
		else if (y2r_type == 2)
			p_y2r = (__s32 *) rect_y2r_hd;
		rect_multi(rect_tmp1, p_y2r, p_rect);
		p_rect = rect_tmp1;
	}

#if 0
	const __s32 rect_srgb_warm[16] = {
		1280,	   0,	   0,	   0,
		0,	1024,	   0,	   0,
		0,	   0,	 819,	   0,
		0,	   0,      0,	1024
	};

	const __s32 rect_srgb_cool[16] = {
		819,	   0,	   0,	   0,
		0,	1024,	   0,	   0,
		0,	   0,	1280,	   0,
		0,	   0,      0,	1024
	};

	if (srgb_type) {
		if (srgb_type == 1)
			p_srgb == (__s32 *)rect_srgb_warm;
		else if (srgb_type == 2)
			p_srgb == (__s32 *)rect_srgb_cool;
		rect_multi(rect_tmp0, p_srgb, p_rect);
		p_rect = rect_tmp0;
	}
#endif

	for (i = 0; i < 12; i++)
		*(p_coff + i) = *(p_rect + i);
}

/*
 * lcdc color enhance
 *
 * parameters:
 * sel:  sel tcon
 * func: 0:disable
 *       1:rgb->rgb
 *       2:yuv->yuv
 * b:    brightness [-600:600]
 * c:    contrast [0:300]
 * s:    saturation [0:300]
 * h:    hue [0:360]
 */
static void
LCDC_ceu(__u32 sel, __u32 func, __s32 b, __s32 c, __s32 s, __s32 h)
{
	__s32 ceu_coff[12];
	__u32 error;

	if (func == 1 || func == 2) {
		if (func == 1) {
			lcd_ceu(1, 1, 1, b, c, s, h, ceu_coff);
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x40, 0x000000ff);
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x44, 0x000000ff);
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x48, 0x000000ff);
		} else if (func == 2) {
			lcd_ceu(0, 1, 0, b, c, s, h, ceu_coff);
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x40, 0x000000eb);
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x44, 0x000000f0);
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x48, 0x000000f0);
		}

		ceu_coff[0] = (ceu_coff[0] + 2) >> 2;
		ceu_coff[1] = (ceu_coff[1] + 2) >> 2;
		ceu_coff[2] = (ceu_coff[2] + 2) >> 2;
		ceu_coff[3] = (ceu_coff[3] + 32) >> 6;
		ceu_coff[4] = (ceu_coff[4] + 2) >> 2;
		ceu_coff[5] = (ceu_coff[5] + 2) >> 2;
		ceu_coff[6] = (ceu_coff[6] + 2) >> 2;
		ceu_coff[7] = (ceu_coff[7] + 32) >> 6;
		ceu_coff[8] = (ceu_coff[8] + 2) >> 2;
		ceu_coff[9] = (ceu_coff[9] + 2) >> 2;
		ceu_coff[10] = (ceu_coff[10] + 2) >> 2;
		ceu_coff[11] = (ceu_coff[11] + 32) >> 6;

		error = 0;
		error |= range_cut(ceu_coff + 0, -4095, 4095);
		error |= range_cut(ceu_coff + 1, -4095, 4095);
		error |= range_cut(ceu_coff + 2, -4095, 4095);
		error |= range_cut(ceu_coff + 3, -262143, 262143);
		error |= range_cut(ceu_coff + 4, -4095, 4095);
		error |= range_cut(ceu_coff + 5, -4095, 4095);
		error |= range_cut(ceu_coff + 6, -4095, 4095);
		error |= range_cut(ceu_coff + 7, -262143, 262143);
		error |= range_cut(ceu_coff + 8, -4095, 4095);
		error |= range_cut(ceu_coff + 9, -4095, 4095);
		error |= range_cut(ceu_coff + 10, -4095, 4095);
		error |= range_cut(ceu_coff + 11, -262143, 262143);

		if (error) {
			LCDC_CLR_BIT(sel, LCDC_CEU_OFF, (__u32) 1 << 31);
			return;
		} else {
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x10,
				     reg_corr(ceu_coff[0], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x14,
				     reg_corr(ceu_coff[1], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x18,
				     reg_corr(ceu_coff[2], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x1c,
				     reg_corr(ceu_coff[3], 1 << 18));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x20,
				     reg_corr(ceu_coff[4], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x24,
				     reg_corr(ceu_coff[5], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x28,
				     reg_corr(ceu_coff[6], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x2c,
				     reg_corr(ceu_coff[7], 1 << 18));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x30,
				     reg_corr(ceu_coff[8], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x34,
				     reg_corr(ceu_coff[9], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x38,
				     reg_corr(ceu_coff[10], 1 << 12));
			LCDC_WUINT32(sel, LCDC_CEU_OFF + 0x3c,
				     reg_corr(ceu_coff[11], 1 << 18));
			LCDC_SET_BIT(sel, LCDC_CEU_OFF, (__u32) 1 << 31);
		}
	} else {
		LCDC_CLR_BIT(sel, LCDC_CEU_OFF, (__u32) 1 << 31);
	}
}
#endif
