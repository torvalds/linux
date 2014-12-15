/*
 * Amlogic Apollo
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <linux/irqreturn.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/osd/osd.h>
#include <linux/amlogic/vout/vout_notify.h>
#ifdef CONFIG_AML_VSYNC_FIQ_ENABLE
#define FIQ_VSYNC
#endif
#include "osd_log.h"
#include "osd_hw_def.h"
#include "osd_clone.h"
#include "osd_prot.h"

static DECLARE_WAIT_QUEUE_HEAD(osd_ext_vsync_wq);
static bool vsync_hit = false;
static bool osd_ext_vf_need_update = false;

static struct vframe_provider_s osd_ext_vf_prov;
static int  g_vf_visual_width;
static int  g_vf_width;
static int  g_vf_height;
static int  g_rotation_width;
static int  g_rotation_height;
extern hw_para_t osd_hw;

static unsigned int filt_coef0[] =   //bicubic
{
	0x00800000,
	0x007f0100,
	0xff7f0200,
	0xfe7f0300,
	0xfd7e0500,
	0xfc7e0600,
	0xfb7d0800,
	0xfb7c0900,
	0xfa7b0b00,
	0xfa7a0dff,
	0xf9790fff,
	0xf97711ff,
	0xf87613ff,
	0xf87416fe,
	0xf87218fe,
	0xf8701afe,
	0xf76f1dfd,
	0xf76d1ffd,
	0xf76b21fd,
	0xf76824fd,
	0xf76627fc,
	0xf76429fc,
	0xf7612cfc,
	0xf75f2ffb,
	0xf75d31fb,
	0xf75a34fb,
	0xf75837fa,
	0xf7553afa,
	0xf8523cfa,
	0xf8503ff9,
	0xf84d42f9,
	0xf84a45f9,
	0xf84848f8
};

static unsigned int filt_coef1[] =  //2 point bilinear
{
	0x00800000,
	0x007e0200,
	0x007c0400,
	0x007a0600,
	0x00780800,
	0x00760a00,
	0x00740c00,
	0x00720e00,
	0x00701000,
	0x006e1200,
	0x006c1400,
	0x006a1600,
	0x00681800,
	0x00661a00,
	0x00641c00,
	0x00621e00,
	0x00602000,
	0x005e2200,
	0x005c2400,
	0x005a2600,
	0x00582800,
	0x00562a00,
	0x00542c00,
	0x00522e00,
	0x00503000,
	0x004e3200,
	0x004c3400,
	0x004a3600,
	0x00483800,
	0x00463a00,
	0x00443c00,
	0x00423e00,
	0x00404000
};

static unsigned int filt_coef2[] =  //2 point bilinear, bank_length == 2
{
	0x80000000,
	0x7e020000,
	0x7c040000,
	0x7a060000,
	0x78080000,
	0x760a0000,
	0x740c0000,
	0x720e0000,
	0x70100000,
	0x6e120000,
	0x6c140000,
	0x6a160000,
	0x68180000,
	0x661a0000,
	0x641c0000,
	0x621e0000,
	0x60200000,
	0x5e220000,
	0x5c240000,
	0x5a260000,
	0x58280000,
	0x562a0000,
	0x542c0000,
	0x522e0000,
	0x50300000,
	0x4e320000,
	0x4c340000,
	0x4a360000,
	0x48380000,
	0x463a0000,
	0x443c0000,
	0x423e0000,
	0x40400000
};

/********************************************************************/
/***********		osd psedu frame provider 			*****************/
/********************************************************************/
static vframe_t *osd_ext_vf_peek(void *arg)
{
	return ((osd_ext_vf_need_update && (vf.width > 0) && (vf.height > 0)) ? &vf : NULL);
}

static vframe_t *osd_ext_vf_get(void *arg)
{
	if (osd_ext_vf_need_update) {
		vf_ext_light_unreg_provider(&osd_ext_vf_prov);
		osd_ext_vf_need_update = false;
		return &vf;
	}
	return NULL;
}

#define PROVIDER_NAME "osd_ext"
static const struct vframe_operations_s osd_ext_vf_provider = {
	.peek = osd_ext_vf_peek,
	.get = osd_ext_vf_get,
	.put = NULL,
};

static unsigned char osd_ext_vf_prov_init = 0;

static inline void osd_ext_update_3d_mode(int enable_osd1, int enable_osd2)
{
	if (enable_osd1) {
		osd1_update_disp_3d_mode();
	}
	if (enable_osd2) {
		osd2_update_disp_3d_mode();
	}
}

static inline void wait_vsync_wakeup(void)
{
	vsync_hit = true;
	wake_up_interruptible(&osd_ext_vsync_wq);
}

static inline void walk_through_update_list(void)
{
	u32 i, j;
	for (i = 0; i < HW_OSD_COUNT; i++) {
		j = 0;
		while (osd_ext_hw.updated[i] && j < 32) {
			if (osd_ext_hw.updated[i] & (1 << j)) {
				osd_ext_hw.reg[i][j].update_func();
				remove_from_update_list(i, j);
			}
			j++;
		}
	}
}

/**********************************************************************/
/**********          osd vsync irq handler              ***************/
/**********************************************************************/
#ifdef FIQ_VSYNC
static irqreturn_t vsync_isr(int irq, void *dev_id)
{
	wait_vsync_wakeup();

	return IRQ_HANDLED;
}
#endif

#ifdef FIQ_VSYNC
static void osd_ext_fiq_isr(void)
#else
static irqreturn_t vsync_isr(int irq, void *dev_id)
#endif
{
#define  	VOUT_ENCI	1
#define   	VOUT_ENCP	2
#define		VOUT_ENCT	3
	unsigned int fb0_cfg_w0, fb1_cfg_w0;
	unsigned int odd_or_even_line;
	unsigned int scan_line_number = 0;
	unsigned char output_type=0;
	u32 data32 = 0;
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
	if (osd_ext_hw.rotate[OSD1].on_off > 0 && osd_ext_hw.rotate[OSD1].angle > 0){
		data32 = ((osd_ext_hw.rotation_pandata[OSD1].y_start + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff)
				| ((osd_ext_hw.rotation_pandata[OSD1].y_end  + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff) << 16 ;
		aml_write_reg32(P_VPU_PROT1_Y_START_END, data32);
	}
#endif
	output_type=aml_read_reg32(P_VPU_VIU_VENC_MUX_CTRL)&0x3;
	osd_ext_hw.scan_mode= SCAN_MODE_PROGRESSIVE;
	switch(output_type)
	{
		case VOUT_ENCP:
			if (aml_read_reg32(P_ENCP_VIDEO_MODE) & (1 << 12)) //1080i
				osd_ext_hw.scan_mode= SCAN_MODE_INTERLACE;
			break;
		case VOUT_ENCI:
			if (aml_read_reg32(P_ENCI_VIDEO_EN) & 1)
				osd_ext_hw.scan_mode = SCAN_MODE_INTERLACE;
			break;
	}


	if (osd_ext_hw.free_scale_enable[OSD1]) {
		osd_ext_hw.scan_mode = SCAN_MODE_PROGRESSIVE;
	}
	if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
		fb0_cfg_w0 = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W0);
		fb1_cfg_w0 = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W0 + REG_OFFSET);
		if (aml_read_reg32(P_ENCP_VIDEO_MODE) & (1 << 12)) {
			/* 1080I */
			scan_line_number = ((aml_read_reg32(P_ENCP_INFO_READ)) & 0x1fff0000) >> 16;
			if ((osd_ext_hw.pandata[OSD1].y_start % 2) == 0) {
				if (scan_line_number >= 562) {
					/* bottom field, odd lines */
					odd_or_even_line = 1;
				} else {
					/* top field, even lines */
					odd_or_even_line = 0;
				}
			} else {
				if (scan_line_number >= 562) {
					/* top field, even lines */
					odd_or_even_line = 0;
				} else {
					/* bottom field, odd lines */
					odd_or_even_line = 1;
				}
			}
		} else {
			if ((osd_ext_hw.pandata[OSD1].y_start % 2) == 0) {
				odd_or_even_line = aml_read_reg32(P_ENCI_INFO_READ) & 1;
			} else {
				odd_or_even_line = !(aml_read_reg32(P_ENCI_INFO_READ) & 1);
			}
		}

		fb0_cfg_w0 &= ~1;
		fb1_cfg_w0 &= ~1;
		fb0_cfg_w0 |= odd_or_even_line;
		fb1_cfg_w0 |= odd_or_even_line;
		aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W0, fb0_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK1_CFG_W0, fb0_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK2_CFG_W0, fb0_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK3_CFG_W0, fb0_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W0 + REG_OFFSET, fb1_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK1_CFG_W0 + REG_OFFSET, fb1_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK2_CFG_W0 + REG_OFFSET, fb1_cfg_w0);
		aml_write_reg32(P_VIU2_OSD1_BLK3_CFG_W0 + REG_OFFSET, fb1_cfg_w0);
	}
	//go through update list
	walk_through_update_list();
	osd_ext_update_3d_mode(osd_ext_hw.mode_3d[OSD1].enable, osd_ext_hw.mode_3d[OSD2].enable);
	if (osd_ext_hw.rotate[OSD1].on_off > 0 && osd_ext_hw.rotate[OSD1].angle > 0){
		aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2,data32);
	}
	if (!vsync_hit) {
#ifdef FIQ_VSYNC
		fiq_bridge_pulse_trigger(&osd_ext_hw.fiq_handle_item);
#else
		wait_vsync_wakeup();
#endif
	}
#ifndef FIQ_VSYNC
	return IRQ_HANDLED;
#endif
}

void osd_ext_wait_vsync_hw(void)
{
	vsync_hit = false;

	wait_event_interruptible_timeout(osd_ext_vsync_wq, vsync_hit, HZ);
}

void osd_ext_set_gbl_alpha_hw(u32 index, u32 gbl_alpha)
{
	if (osd_ext_hw.gbl_alpha[index] != gbl_alpha) {

		osd_ext_hw.gbl_alpha[index] = gbl_alpha;
		add_to_update_list(index, OSD_GBL_ALPHA);

		osd_ext_wait_vsync_hw();
	}
}

u32 osd_ext_get_gbl_alpha_hw(u32 index)
{
	return osd_ext_hw.gbl_alpha[index];
}

void osd_ext_set_colorkey_hw(u32 index, u32 color_index, u32 colorkey)
{
	u8 r = 0, g = 0, b = 0, a = (colorkey & 0xff000000) >> 24;
	u32 data32;

	colorkey &= 0x00ffffff;
	switch (color_index) {
	case COLOR_INDEX_16_655:
		r = (colorkey >> 10 & 0x3f) << 2;
		g = (colorkey >> 5 & 0x1f) << 3;
		b = (colorkey & 0x1f) << 3;
		break;
	case COLOR_INDEX_16_844:
		r = colorkey >> 8 & 0xff;
		g = (colorkey >> 4 & 0xf) << 4;
		b = (colorkey & 0xf) << 4;
		break;
	case COLOR_INDEX_16_565:
		r = (colorkey >> 11 & 0x1f) << 3;
		g = (colorkey >> 5 & 0x3f) << 2;
		b = (colorkey & 0x1f) << 3;
		break;
	case COLOR_INDEX_24_888_B:
		b = colorkey >> 16 & 0xff;
		g = colorkey >> 8 & 0xff;
		r = colorkey & 0xff;
		break;
	case COLOR_INDEX_24_RGB:
	case COLOR_INDEX_YUV_422:
		r = colorkey >> 16 & 0xff;
		g = colorkey >> 8 & 0xff;
		b = colorkey & 0xff;
		break;
	}
	data32 = r << 24 | g << 16 | b << 8 | a;
	if (osd_ext_hw.color_key[index] != data32) {
		osd_ext_hw.color_key[index] = data32;
		amlog_mask_level(LOG_MASK_HARDWARE, LOG_LEVEL_LOW, "bpp:%d--r:0x%x g:0x%x b:0x%x ,a:0x%x\r\n",
				 color_index, r, g, b, a);
		add_to_update_list(index, OSD_COLOR_KEY);

		osd_ext_wait_vsync_hw();
	}

	return;
}

void osd_ext_srckey_enable_hw(u32 index, u8 enable)
{
	if (enable != osd_ext_hw.color_key_enable[index]) {
		osd_ext_hw.color_key_enable[index] = enable;
		add_to_update_list(index, OSD_COLOR_KEY_ENABLE);

		osd_ext_wait_vsync_hw();
	}

}

void osddev_ext_update_disp_axis_hw(
				 u32 display_h_start,
				 u32 display_h_end,
				 u32 display_v_start,
				 u32 display_v_end,
				 u32 xoffset,
				 u32 yoffset,
				 u32 mode_change,
				 u32 index)
{
	dispdata_t disp_data;
	pandata_t pan_data;

	if (NULL == osd_ext_hw.color_info[index])
		return;
	if (mode_change)	//modify pandata .
	{
		add_to_update_list(index, OSD_COLOR_MODE);
	}
	disp_data.x_start = display_h_start;
	disp_data.y_start = display_v_start;
	disp_data.x_end = display_h_end;
	disp_data.y_end = display_v_end;

	pan_data.x_start = xoffset;
	pan_data.x_end = xoffset + (display_h_end - display_h_start);
	pan_data.y_start = yoffset;
	pan_data.y_end = yoffset + (display_v_end - display_v_start);

	//if output mode change then reset pan ofFfset.
	memcpy(&osd_ext_hw.pandata[index], &pan_data, sizeof(pandata_t));
	memcpy(&osd_ext_hw.dispdata[index], &disp_data, sizeof(dispdata_t));
	add_to_update_list(index, DISP_GEOMETRY);
	osd_ext_wait_vsync_hw();
}

void osd_ext_setup(struct osd_ctl_s *osd_ext_ctl,
		   u32 xoffset,
		   u32 yoffset,
		   u32 xres,
		   u32 yres,
		   u32 xres_virtual,
		   u32 yres_virtual,
		   u32 disp_start_x,
		   u32 disp_start_y,
		   u32 disp_end_x,
		   u32 disp_end_y,
		   u32 fbmem,
		   const color_bit_define_t * color,
		   int index)
{
	u32 w = (color->bpp * xres_virtual + 7) >> 3;
	dispdata_t disp_data;
	pandata_t pan_data;
#ifdef CONFIG_AM_LOGO
	static u32 logo_setup_ok = 0;
#endif

	pan_data.x_start = xoffset;
	pan_data.y_start = yoffset;

	disp_data.x_start = disp_start_x;
	disp_data.y_start = disp_start_y;
	if(likely(osd_ext_hw.free_scale_enable[OSD1] && index==OSD1))
	{
		if(!osd_ext_hw.free_scale_mode[OSD1]){
			pan_data.x_end=xoffset + g_vf_visual_width;
			pan_data.y_end=yoffset + g_vf_height;
			disp_data.x_end=disp_start_x + g_vf_width;
			disp_data.y_end=disp_start_y + g_vf_height;
		}else{
			pan_data.x_end=xoffset + (disp_end_x-disp_start_x);
			pan_data.y_end=yoffset + (disp_end_y-disp_start_y);
			disp_data.x_end=disp_end_x;
			disp_data.y_end=disp_end_y;
		}
	}else{
		pan_data.x_end=xoffset + (disp_end_x-disp_start_x);
		pan_data.y_end=yoffset + (disp_end_y-disp_start_y);
		if (likely(osd_ext_hw.rotate[index].on_off && osd_ext_hw.rotate[index].on_off > 0)){
			disp_data.x_end = disp_start_x + g_rotation_height;
			disp_data.y_end = disp_start_y + g_rotation_width;
		}else{
			disp_data.x_end=disp_end_x;
			disp_data.y_end=disp_end_y;
		}
	}

	if (osd_ext_hw.fb_gem[index].addr != fbmem || osd_ext_hw.fb_gem[index].width != w
	    || osd_ext_hw.fb_gem[index].height != yres_virtual) {
		osd_ext_hw.fb_gem[index].addr = fbmem;
		osd_ext_hw.fb_gem[index].width = w;
		osd_ext_hw.fb_gem[index].height = yres_virtual;

		if(fbmem == 0){
			canvas_config(osd_ext_hw.fb_gem[index].canvas_idx, osd_hw.fb_gem[index].addr,
				osd_ext_hw.fb_gem[index].width, osd_ext_hw.fb_gem[index].height,
				CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
		}else{
			canvas_config(osd_ext_hw.fb_gem[index].canvas_idx, osd_ext_hw.fb_gem[index].addr,
				osd_ext_hw.fb_gem[index].width, osd_ext_hw.fb_gem[index].height,
				CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
		}
	}

	if (color != osd_ext_hw.color_info[index]) {
		osd_ext_hw.color_info[index] = color;
		add_to_update_list(index, OSD_COLOR_MODE);
	}
	//osd blank only control by /sys/class/graphcis/fbx/blank
	/*
	   if(osd_ext_hw.enable[index] == DISABLE)
	   {
	   osd_ext_hw.enable[index]=ENABLE;
	   add_to_update_list(index,OSD_ENABLE);

	   } */
	if (memcmp(&pan_data, &osd_ext_hw.pandata[index], sizeof(pandata_t)) != 0 ||
	    memcmp(&disp_data, &osd_ext_hw.dispdata[index], sizeof(dispdata_t)) != 0) {
		//if (!osd_ext_hw.free_scale_enable[OSD1])	//in free scale mode ,adjust geometry para is abandoned.		{
			memcpy(&osd_ext_hw.pandata[index], &pan_data, sizeof(pandata_t));
			memcpy(&osd_ext_hw.dispdata[index], &disp_data, sizeof(dispdata_t));
			add_to_update_list(index, DISP_GEOMETRY);
	}
#ifdef CONFIG_AM_LOGO
	if (!logo_setup_ok) {
#ifdef FIQ_VSYNC
		osd_ext_fiq_isr();
#else
		vsync_isr(INT_VIU2_VSYNC, NULL);
#endif
		logo_setup_ok++;
	}
#endif

	osd_ext_wait_vsync_hw();
}

void osd_ext_setpal_hw(unsigned regno,
		unsigned red,
		unsigned green,
		unsigned blue,
		unsigned transp,
		int index)
{

	if (regno < 256) {
		u32 pal;
		pal = ((red   & 0xff) << 24) |
			((green & 0xff) << 16) |
			((blue & 0xff) << 8) |
			(transp & 0xff);

		aml_write_reg32(P_VIU2_OSD1_COLOR_ADDR + REG_OFFSET * index, regno);
		aml_write_reg32(P_VIU2_OSD1_COLOR + REG_OFFSET * index, pal);
	}
}

u32 osd_ext_get_osd_ext_order_hw(u32 index)
{
	return osd_ext_hw.osd_ext_order & 0x3;
}

void osd_ext_change_osd_ext_order_hw(u32 index, u32 order)
{
	if ((order != OSD_ORDER_01) && (order != OSD_ORDER_10))
		return;
	osd_ext_hw.osd_ext_order = order;
	add_to_update_list(index, OSD_CHANGE_ORDER);
	osd_ext_wait_vsync_hw();
}

void osd_ext_free_scale_enable_hw(u32 index, u32 enable)
{
	if (osd_ext_hw.free_scale_mode[index]){
		unsigned int hfs_enable = 0;
		unsigned int vfs_enable = 0;

		hfs_enable = (enable&0xffff0000?1:0);
		vfs_enable = (enable&0xffff?1:0);
		osd_ext_hw.free_scale[index].hfs_enable = hfs_enable;
		osd_ext_hw.free_scale[index].vfs_enable = vfs_enable;

		if (hfs_enable ||vfs_enable){
			osd_ext_hw.free_scale_enable[index] = 1;
		}else if (!hfs_enable && !vfs_enable){
			osd_ext_hw.free_scale_enable[index] = 0;
		}

		if (index==OSD1)
		{
			if (osd_ext_hw.free_scale_enable[index])
			{
				if ((osd_ext_hw.free_scale_data[OSD1].x_end > 0) && hfs_enable){
					osd_ext_hw.free_scale_width[OSD1] = osd_ext_hw.free_scale_data[OSD1].x_end - \
									osd_ext_hw.free_scale_data[OSD1].x_start;
				}

				if ((osd_ext_hw.free_scale_data[OSD1].y_end > 0) && vfs_enable){
					osd_ext_hw.free_scale_height[OSD1] = osd_ext_hw.free_scale_data[OSD1].y_end -\
									 osd_ext_hw.free_scale_data[OSD1].y_start;
				}

				add_to_update_list(index,OSD_COLOR_MODE);
				add_to_update_list(index,OSD_FREESCALE_COEF);
				add_to_update_list(index,DISP_GEOMETRY);
				add_to_update_list(index,DISP_FREESCALE_ENABLE);
			}else{
				add_to_update_list(index,OSD_COLOR_MODE);
				add_to_update_list(index,DISP_GEOMETRY);
				add_to_update_list(index,DISP_FREESCALE_ENABLE);
			}
		}else{
			add_to_update_list(OSD2,DISP_GEOMETRY);
			add_to_update_list(OSD2,OSD_COLOR_MODE);
		}

		osd_ext_enable_hw(osd_ext_hw.enable[index],index);
	}else{
	static dispdata_t save_disp_data = { 0, 0, 0, 0 };
#ifdef CONFIG_AM_VIDEO
#ifdef CONFIG_POST_PROCESS_MANAGER
	int mode_changed = 0;
	if ((index == OSD1) && (osd_ext_hw.free_scale_enable[index] != enable))
		mode_changed = 1;
#endif
#endif

	amlog_level(LOG_LEVEL_HIGH, "osd%d free scale %s\r\n", index, enable ? "ENABLE" : "DISABLE");
	osd_ext_hw.free_scale_enable[index] = enable;
	if (index == OSD1) {
		if (enable) {
			osd_ext_vf_need_update = true;
			if ((osd_ext_hw.free_scale_data[OSD1].x_end > 0)
			    && (osd_ext_hw.free_scale_data[OSD1].x_end > 0)) {
				vf.width =
				    osd_ext_hw.free_scale_data[index].x_end -
				    osd_ext_hw.free_scale_data[index].x_start + 1;
				vf.height =
				    osd_ext_hw.free_scale_data[index].y_end -
				    osd_ext_hw.free_scale_data[index].y_start + 1;
			} else {
				vf.width = osd_ext_hw.free_scale_width[OSD1];
				vf.height = osd_ext_hw.free_scale_height[OSD1];
			}
//                      vf.type = (osd_ext_hw.scan_mode==SCAN_MODE_INTERLACE ?VIDTYPE_INTERLACE:VIDTYPE_PROGRESSIVE) | VIDTYPE_VIU2_FIELD;
			vf.type = (VIDTYPE_NO_VIDEO_ENABLE | VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VSCALE_DISABLE);
			vf.ratio_control = DISP_RATIO_FORCECONFIG | DISP_RATIO_NO_KEEPRATIO;
#ifdef CONFIG_AM_VIDEO
			if (osd_ext_vf_prov_init == 0) {
				vf_provider_init(&osd_ext_vf_prov, PROVIDER_NAME, &osd_ext_vf_provider, NULL);
				osd_ext_vf_prov_init = 1;
			}
			vf_reg_provider(&osd_ext_vf_prov);
#endif
			memcpy(&save_disp_data, &osd_ext_hw.dispdata[OSD1], sizeof(dispdata_t));

			g_vf_visual_width=vf.width-1-osd_hw.dispdata[OSD1].x_start ;
			g_vf_width=vf.width-1;
			g_vf_height=vf.height-1;
			osd_ext_hw.dispdata[OSD1].x_end = osd_ext_hw.dispdata[OSD1].x_start + vf.width - 1;
			osd_ext_hw.dispdata[OSD1].y_end = osd_ext_hw.dispdata[OSD1].y_start + vf.height - 1;
			add_to_update_list(OSD1, DISP_GEOMETRY);
			add_to_update_list(OSD1, OSD_COLOR_MODE);
		} else {
			osd_ext_vf_need_update = false;
			if (save_disp_data.x_end <= save_disp_data.x_start ||
			    save_disp_data.y_end <= save_disp_data.y_start) {
				return;
			}
			memcpy(&osd_ext_hw.dispdata[OSD1], &save_disp_data, sizeof(dispdata_t));

			add_to_update_list(OSD1, DISP_GEOMETRY);
			add_to_update_list(OSD1, OSD_COLOR_MODE);
#ifdef CONFIG_AM_VIDEO
			vf_unreg_provider(&osd_ext_vf_prov);
#endif

		}
	} else {
		add_to_update_list(OSD2, DISP_GEOMETRY);
		add_to_update_list(OSD2, OSD_COLOR_MODE);
	}
	osd_ext_enable_hw(osd_ext_hw.enable[index], index);
#ifdef CONFIG_AM_VIDEO
#ifdef CONFIG_POST_PROCESS_MANAGER
	if (mode_changed) {
		//vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_RESET,NULL);
		extern void vf_ppmgr_reset(int type);
		vf_ppmgr_reset(1);
	}
#endif
#endif
	}
}

void osd_ext_get_free_scale_enable_hw(u32 index, u32 * free_scale_enable)
{
	*free_scale_enable = osd_ext_hw.free_scale_enable[index];
}

void osd_ext_free_scale_width_hw(u32 index, u32 width)
{
	osd_ext_hw.free_scale_width[index] = width;
	if (osd_ext_hw.free_scale_enable[index] &&
			(!osd_ext_hw.free_scale_mode[index])) {
		osd_ext_vf_need_update = true;
		vf.width = osd_ext_hw.free_scale_width[index];
	}
}

void osd_ext_get_free_scale_width_hw(u32 index, u32 * free_scale_width)
{
	*free_scale_width = osd_ext_hw.free_scale_width[index];
}

void osd_ext_free_scale_height_hw(u32 index, u32 height)
{
	osd_ext_hw.free_scale_height[index] = height;
	if (osd_ext_hw.free_scale_enable[index] &&
			(!osd_ext_hw.free_scale_mode[index])) {
		osd_ext_vf_need_update = true;
		vf.height = osd_ext_hw.free_scale_height[index];
	}
}

void osd_ext_get_free_scale_height_hw(u32 index, u32 * free_scale_height)
{
	*free_scale_height = osd_ext_hw.free_scale_height[index];
}

void osd_ext_get_free_scale_axis_hw(u32 index, s32 * x0, s32 * y0, s32 * x1, s32 * y1)
{
	*x0 = osd_ext_hw.free_scale_data[index].x_start;
	*y0 = osd_ext_hw.free_scale_data[index].y_start;
	*x1 = osd_ext_hw.free_scale_data[index].x_end;
	*y1 = osd_ext_hw.free_scale_data[index].y_end;
}

void osd_ext_set_free_scale_axis_hw(u32 index, s32 x0, s32 y0, s32 x1, s32 y1)
{
	osd_ext_hw.free_scale_data[index].x_start = x0;
	osd_ext_hw.free_scale_data[index].y_start = y0;
	osd_ext_hw.free_scale_data[index].x_end = x1;
	osd_ext_hw.free_scale_data[index].y_end = y1;
}

void osd_ext_get_scale_axis_hw(u32 index, s32 * x0, s32 * y0, s32 * x1, s32 * y1)
{
	*x0 = osd_ext_hw.scaledata[index].x_start;
	*x1 = osd_ext_hw.scaledata[index].x_end;
	*y0 = osd_ext_hw.scaledata[index].y_start;
	*y1 = osd_ext_hw.scaledata[index].y_end;
}

void osd_ext_set_scale_axis_hw(u32 index, s32 x0, s32 y0, s32 x1, s32 y1)
{
	osd_ext_hw.scaledata[index].x_start = x0;
	osd_ext_hw.scaledata[index].x_end = x1;
	osd_ext_hw.scaledata[index].y_start = y0;
	osd_ext_hw.scaledata[index].y_end = y1;
}

void osd_ext_free_scale_mode_hw(u32 index,u32 freescale_mode)
{
	osd_ext_hw.free_scale_mode[index] = freescale_mode;
}

void osd_ext_get_free_scale_mode_hw(u32 index, u32 *freescale_mode)
{
	*freescale_mode = osd_ext_hw.free_scale_mode[index];
}

void osd_ext_get_window_axis_hw(u32 index, s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
	*x0 = osd_ext_hw.free_dst_data[index].x_start;
	*x1 = osd_ext_hw.free_dst_data[index].x_end;
	*y0 = osd_ext_hw.free_dst_data[index].y_start;
	*y1 = osd_ext_hw.free_dst_data[index].y_end;
}

void osd_ext_set_window_axis_hw(u32 index, s32 x0, s32 y0, s32 x1, s32 y1)
{
	osd_ext_hw.free_dst_data[index].x_start = x0;
	osd_ext_hw.free_dst_data[index].x_end = x1;
	osd_ext_hw.free_dst_data[index].y_start = y0;
	osd_ext_hw.free_dst_data[index].y_end = y1;
}

void osd_ext_get_osd_ext_info_hw(u32 index, s32(*posdval)[4], u32(*posdreg)[5], s32 info_flag)
{
	if (info_flag == 0) {
		posdval[0][0] = osd_ext_hw.pandata[index].x_start;
		posdval[0][1] = osd_ext_hw.pandata[index].x_end;
		posdval[0][2] = osd_ext_hw.pandata[index].y_start;
		posdval[0][3] = osd_ext_hw.pandata[index].y_end;

		posdval[1][0] = osd_ext_hw.dispdata[index].x_start;
		posdval[1][1] = osd_ext_hw.dispdata[index].x_end;
		posdval[1][2] = osd_ext_hw.dispdata[index].y_start;
		posdval[1][3] = osd_ext_hw.dispdata[index].y_end;

		posdval[2][0] = osd_ext_hw.scaledata[index].x_start;
		posdval[2][1] = osd_ext_hw.scaledata[index].x_end;
		posdval[2][2] = osd_ext_hw.scaledata[index].y_start;
		posdval[2][3] = osd_ext_hw.scaledata[index].y_end;
	} else if (info_flag == 1) {
		posdreg[0][0] = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W0);
		posdreg[0][1] = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W1);
		posdreg[0][2] = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W2);
		posdreg[0][3] = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W3);
		posdreg[0][4] = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W4);

		posdreg[1][0] = aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W0);
		posdreg[1][1] = aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W1);
		posdreg[1][2] = aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W2);
		posdreg[1][3] = aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W3);
		posdreg[1][4] = aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W4);
	} else {
		;		//ToDo
	}
}

void osd_ext_get_block_windows_hw(u32 index, u32 * windows)
{
	memcpy(windows, osd_ext_hw.block_windows[index], sizeof(osd_ext_hw.block_windows[index]));
}

void osd_ext_set_block_windows_hw(u32 index, u32 * windows)
{
	memcpy(osd_ext_hw.block_windows[index], windows, sizeof(osd_ext_hw.block_windows[index]));
	add_to_update_list(index, DISP_GEOMETRY);
	osd_ext_wait_vsync_hw();
}

void osd_ext_get_block_mode_hw(u32 index, u32 * mode)
{
	*mode = osd_ext_hw.block_mode[index];
}

void osd_ext_set_block_mode_hw(u32 index, u32 mode)
{
	osd_ext_hw.block_mode[index] = mode;
	add_to_update_list(index, DISP_GEOMETRY);
	osd_ext_wait_vsync_hw();
}

void osd_ext_enable_3d_mode_hw(int index, int enable)
{
	spin_lock_irqsave(&osd_ext_lock, lock_flags);
	osd_ext_hw.mode_3d[index].enable = enable;
	spin_unlock_irqrestore(&osd_ext_lock, lock_flags);
	if (enable)		//when disable 3d mode ,we should return to stardard state.
	{
		osd_ext_hw.mode_3d[index].left_right = LEFT;
		osd_ext_hw.mode_3d[index].l_start = osd_ext_hw.pandata[index].x_start;
		osd_ext_hw.mode_3d[index].l_end =
		    (osd_ext_hw.pandata[index].x_end + osd_ext_hw.pandata[index].x_start) >> 1;
		osd_ext_hw.mode_3d[index].r_start = osd_ext_hw.mode_3d[index].l_end + 1;
		osd_ext_hw.mode_3d[index].r_end = osd_ext_hw.pandata[index].x_end;
		osd_ext_hw.mode_3d[index].origin_scale.h_enable = osd_ext_hw.scale[index].h_enable;
		osd_ext_hw.mode_3d[index].origin_scale.v_enable = osd_ext_hw.scale[index].v_enable;
		osd_ext_set_2x_scale_hw(index, 1, 0);
	} else {

		osd_ext_set_2x_scale_hw(index, osd_ext_hw.mode_3d[index].origin_scale.h_enable,
					osd_ext_hw.mode_3d[index].origin_scale.v_enable);
	}
}

void osd_ext_enable_hw(int enable, int index)
{
	osd_ext_hw.enable[index] = enable;
	add_to_update_list(index, OSD_ENABLE);

	osd_ext_wait_vsync_hw();
}

void osd_ext_set_2x_scale_hw(u32 index, u16 h_scale_enable, u16 v_scale_enable)
{
	amlog_level(LOG_LEVEL_HIGH, "osd[%d] set scale, h_scale: %s, v_scale: %s\r\n",
		    index, h_scale_enable ? "ENABLE" : "DISABLE", v_scale_enable ? "ENABLE" : "DISABLE");
	amlog_level(LOG_LEVEL_HIGH, "osd[%d].scaledata: %d %d %d %d\n",
		    index,
		    osd_ext_hw.scaledata[index].x_start,
		    osd_ext_hw.scaledata[index].x_end,
			osd_ext_hw.scaledata[index].y_start,
			osd_ext_hw.scaledata[index].y_end);
	amlog_level(LOG_LEVEL_HIGH, "osd[%d].pandata: %d %d %d %d\n",
		    index,
		    osd_ext_hw.pandata[index].x_start,
		    osd_ext_hw.pandata[index].x_end,
		    osd_ext_hw.pandata[index].y_start,
            osd_ext_hw.pandata[index].y_end);

	osd_ext_hw.scale[index].h_enable = h_scale_enable;
	osd_ext_hw.scale[index].v_enable = v_scale_enable;
	add_to_update_list(index, DISP_SCALE_ENABLE);
	add_to_update_list(index, DISP_GEOMETRY);

	osd_ext_wait_vsync_hw();
}

void osd_ext_set_osd_ext_rotate_angle_hw(u32 index, u32 angle)
{
	osd_ext_hw.rotate[index].angle = angle;
	add_to_update_list(index, DISP_OSD_ROTATE);
	osd_ext_wait_vsync_hw();
}

void osd_ext_get_osd_ext_rotate_angle_hw(u32 index,u32 *angle)
{
	*angle = osd_ext_hw.rotate[index].angle;
}

void osd_ext_set_osd_ext_rotate_on_hw(u32 index, u32 on_off)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
	static dispdata_t save_disp_data={0,0,0,0};
	static dispdata_t save_disp_data2={0,0,0,0};
	osd_ext_hw.rotate[index].on_off = on_off;

	if(on_off)
	{
		if(index == OSD1){
			memcpy(&save_disp_data,&osd_ext_hw.dispdata[index],sizeof(dispdata_t));
		}else if(index == OSD2){
			memcpy(&save_disp_data2,&osd_ext_hw.dispdata[index],sizeof(dispdata_t));
		}
		g_rotation_width = osd_ext_hw.rotation_pandata[index].x_end-osd_ext_hw.rotation_pandata[index].x_start;
		g_rotation_height = osd_ext_hw.rotation_pandata[index].y_end-osd_ext_hw.rotation_pandata[index].y_start;
		osd_ext_hw.dispdata[index].x_end = osd_ext_hw.dispdata[OSD1].x_start+g_rotation_height;
		osd_ext_hw.dispdata[index].y_end = osd_ext_hw.dispdata[OSD1].y_start+g_rotation_width;
	}
	else
	{
		aml_set_reg32_mask(P_VPU_SW_RESET, 1<<8);
		aml_clr_reg32_mask(P_VPU_SW_RESET, 1<<8);
		if(index == OSD1){
			aml_set_reg32_mask(P_VIU2_SW_RESET, 1<<0);
			aml_clr_reg32_mask(P_VIU2_SW_RESET, 1<<0);
			aml_set_reg32_mask(P_VIU2_OSD1_FIFO_CTRL_STAT, 1<<0);
			memcpy(&osd_ext_hw.dispdata[index],&save_disp_data,sizeof(dispdata_t));
		}else{
			aml_set_reg32_mask(P_VIU2_SW_RESET, 1<<1);
			aml_clr_reg32_mask(P_VIU2_SW_RESET, 1<<1);
			aml_set_reg32_mask(P_VIU2_OSD1_FIFO_CTRL_STAT, 1<<0);
			memcpy(&osd_ext_hw.dispdata[index],&save_disp_data2,sizeof(dispdata_t));
		}
	}
	add_to_update_list(index,OSD_COLOR_MODE);
	add_to_update_list(index, DISP_GEOMETRY);
	add_to_update_list(index, DISP_OSD_ROTATE);
	osd_ext_wait_vsync_hw();
#endif
}

void osd_ext_get_osd_ext_rotate_on_hw(u32 index,u32 *on_off)
{
	*on_off = osd_ext_hw.rotate[index].on_off;
}


void osd_ext_set_prot_canvas_hw(u32 index, s32 x_start, s32 y_start, s32 x_end, s32 y_end)
{
	osd_ext_hw.rotation_pandata[index].x_start = x_start;
	osd_ext_hw.rotation_pandata[index].y_start = y_start;
	osd_ext_hw.rotation_pandata[index].x_end = x_end;
	osd_ext_hw.rotation_pandata[index].y_end = y_end;

	if (osd_ext_hw.rotate[index].on_off && osd_ext_hw.rotate[index].angle > 0) {
		g_rotation_width = osd_ext_hw.rotation_pandata[index].x_end-osd_ext_hw.rotation_pandata[index].x_start;
		g_rotation_height = osd_ext_hw.rotation_pandata[index].y_end-osd_ext_hw.rotation_pandata[index].y_start;
		osd_ext_hw.dispdata[index].x_end = osd_ext_hw.dispdata[OSD1].x_start+g_rotation_height;
		osd_ext_hw.dispdata[index].y_end = osd_ext_hw.dispdata[OSD1].y_start+g_rotation_width;

		add_to_update_list(index, DISP_GEOMETRY);
		add_to_update_list(index, OSD_COLOR_MODE);
	}
}

void osd_ext_get_prot_canvas_hw(u32 index, s32 *x_start, s32 *y_start, s32 *x_end, s32 *y_end)
{
	*x_start = osd_ext_hw.rotation_pandata[index].x_start;
	*y_start = osd_ext_hw.rotation_pandata[index].y_start;
	*x_end = osd_ext_hw.rotation_pandata[index].x_end;
	*y_end = osd_ext_hw.rotation_pandata[index].y_end;
}

void osd_ext_pan_display_hw(unsigned int xoffset, unsigned int yoffset, int index)
{
	long diff_x, diff_y;

#if defined(CONFIG_FB_OSD2_CURSOR)
	if (index >= 1)
#else
	if (index >= 2)
#endif
		return;

	if (xoffset != osd_ext_hw.pandata[index].x_start || yoffset != osd_ext_hw.pandata[index].y_start) {
		diff_x = xoffset - osd_ext_hw.pandata[index].x_start;
		diff_y = yoffset - osd_ext_hw.pandata[index].y_start;

		osd_ext_hw.pandata[index].x_start += diff_x;
		osd_ext_hw.pandata[index].x_end += diff_x;
		osd_ext_hw.pandata[index].y_start += diff_y;
		osd_ext_hw.pandata[index].y_end += diff_y;
		add_to_update_list(index, DISP_GEOMETRY);

		osd_ext_wait_vsync_hw();

		amlog_mask_level(LOG_MASK_HARDWARE, LOG_LEVEL_LOW, "offset[%d-%d]x[%d-%d]y[%d-%d]\n",
				 xoffset, yoffset, osd_ext_hw.pandata[index].x_start, osd_ext_hw.pandata[index].x_end,
				 osd_ext_hw.pandata[index].y_start, osd_ext_hw.pandata[index].y_end);
	}
}

static void osd1_update_disp_scale_enable(void)
{
	if (osd_ext_hw.scale[OSD1].h_enable) {
		aml_set_reg32_mask(P_VIU2_OSD1_BLK0_CFG_W0, 3 << 12);
	} else {
		aml_clr_reg32_mask(P_VIU2_OSD1_BLK0_CFG_W0, 3 << 12);
	}
	if (osd_ext_hw.scan_mode != SCAN_MODE_INTERLACE) {
		if (osd_ext_hw.scale[OSD1].v_enable) {
			aml_set_reg32_mask(P_VIU2_OSD1_BLK0_CFG_W0, 1 << 14);
		} else {
			aml_clr_reg32_mask(P_VIU2_OSD1_BLK0_CFG_W0, 1 << 14);
		}
	}
}

static void osd2_update_disp_scale_enable(void)
{
	if (osd_ext_hw.scale[OSD2].h_enable) {
#if defined(CONFIG_FB_OSD2_CURSOR)
		aml_clr_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 3 << 12);
#else
		aml_set_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 3 << 12);
#endif
	} else {
		aml_clr_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 3 << 12);
	}
	if (osd_ext_hw.scan_mode != SCAN_MODE_INTERLACE) {
		if (osd_ext_hw.scale[OSD2].v_enable) {
#if defined(CONFIG_FB_OSD2_CURSOR)
			aml_clr_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 1 << 14);
#else
			aml_set_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 1 << 14);
#endif
		} else {
			aml_clr_reg32_mask(P_VIU2_OSD2_BLK0_CFG_W0, 1 << 14);
		}
	}

}

static  void  osd1_update_disp_freescale_enable(void)
{
	int hf_phase_step, vf_phase_step;
	int dst_w, dst_h;
	int vsc_ini_rcv_num, vsc_ini_rpt_p0_num;
	int hsc_ini_rcv_num, hsc_ini_rpt_p0_num;

	int hf_bank_len = 4;
	int vf_bank_len = 4;

	hsc_ini_rcv_num = hf_bank_len;
	vsc_ini_rcv_num = vf_bank_len;
	hsc_ini_rpt_p0_num = (hf_bank_len/2 - 1) > 0 ?  (hf_bank_len/2 - 1): 0;
	vsc_ini_rpt_p0_num = (vf_bank_len/2 - 1) > 0 ?  (vf_bank_len/2 - 1): 0;

	dst_w = osd_ext_hw.free_dst_data[OSD1].x_end -osd_ext_hw.free_dst_data[OSD1].x_start+1;
	hf_phase_step = ((osd_ext_hw.free_scale_width[OSD1]+1)<< 18) / dst_w;
	hf_phase_step = (hf_phase_step << 6);

	dst_h = osd_ext_hw.free_dst_data[OSD1].y_end - osd_ext_hw.free_dst_data[OSD1].y_start+1;
	vf_phase_step = ((osd_ext_hw.free_scale_height[OSD1]+1) << 20) / dst_h;
	vf_phase_step = (vf_phase_step << 4);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	aml_set_reg32_bits(P_VPP2_OSD_SC_DUMMY_DATA, 0x00808000, 0, 32);

	if (osd_ext_hw.free_scale_enable[OSD1]){
		aml_set_reg32_bits (P_VPP2_OSD_SC_CTRL0, 1, 3, 1);
		aml_set_reg32_bits (P_VPP2_OSD_SC_CTRL0, OSD1, 0, 2);
		aml_set_reg32_bits (P_VPP2_OSD_SC_CTRL0, 0, 4, 8);
	}else{
		aml_clr_reg32_mask(P_VPP2_OSD_SC_CTRL0, 1<<3);
	}

	if (osd_ext_hw.free_scale_enable[OSD1]){
		aml_set_reg32_bits(P_VPP2_OSD_SCI_WH_M1, osd_ext_hw.free_scale_width[OSD1], 16, 13);
		aml_set_reg32_bits(P_VPP2_OSD_SCI_WH_M1, osd_ext_hw.free_scale_height[OSD1], 0,  13);

		aml_set_reg32_bits(P_VPP2_OSD_SCO_H_START_END, osd_ext_hw.free_dst_data[OSD1].x_start, 16, 12);
		aml_set_reg32_bits(P_VPP2_OSD_SCO_H_START_END, osd_ext_hw.free_dst_data[OSD1].x_end, 0, 12);

		aml_set_reg32_bits(P_VPP2_OSD_SCO_V_START_END, osd_ext_hw.free_dst_data[OSD1].y_start, 16, 12);
		aml_set_reg32_bits(P_VPP2_OSD_SCO_V_START_END, osd_ext_hw.free_dst_data[OSD1].y_end, 0, 12);
	}

	if (osd_ext_hw.free_scale[OSD1].vfs_enable){
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, vf_bank_len, 0, 3);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, vsc_ini_rcv_num, 3, 3);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, vsc_ini_rpt_p0_num, 8, 2);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, 1, 24, 1);
	}else{
		aml_clr_reg32_mask(P_VPP2_OSD_VSC_CTRL0, 1<<24);
	}

	if (osd_ext_hw.free_scale[OSD1].hfs_enable){
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, hf_bank_len, 0, 3);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, hsc_ini_rcv_num, 3, 3);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, hsc_ini_rpt_p0_num, 8, 2);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, 1, 22, 1);
	}else{
		aml_clr_reg32_mask(P_VPP2_OSD_HSC_CTRL0, 1<<22);
	}

	if (osd_ext_hw.free_scale_enable[OSD1]){
		aml_set_reg32_bits(P_VPP2_OSD_HSC_PHASE_STEP, hf_phase_step, 0, 28);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_INI_PHASE, 0, 0, 16);

		aml_set_reg32_bits(P_VPP2_OSD_VSC_PHASE_STEP, vf_phase_step, 0, 28);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_INI_PHASE, 0, 0, 16);
	}

	remove_from_update_list(OSD1,DISP_FREESCALE_ENABLE);
#endif
}

static void osd1_update_coef(void)
{
	int vf_coef_idx = 0;
	int *vf_coef;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	int i;
	int hf_coef_idx = 0;
	int hf_coef_wren = 1;
	int vf_coef_wren = 1;
	int *hf_coef;
#endif
	if (vf_coef_idx == 0){
		vf_coef = filt_coef0;
	}else if (vf_coef_idx == 1){
		vf_coef = filt_coef1;
	}else if (vf_coef_idx == 2){
		vf_coef = filt_coef2;
	}else{
		vf_coef = filt_coef0;
	}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if (vf_coef_wren) {
		aml_set_reg32_bits(P_VPP2_OSD_SCALE_COEF_IDX, 0x0000, 0, 9);
		for (i = 0; i < 33; i++)
		{
			aml_write_reg32(P_VPP2_OSD_SCALE_COEF, vf_coef[i]);
		}
	}

	if (hf_coef_idx == 0){
		hf_coef = filt_coef0;
	}else if (hf_coef_idx == 1){
		hf_coef = filt_coef1;
	}else if (hf_coef_idx == 2){
		hf_coef = filt_coef2;
	}else{
		hf_coef = filt_coef0;
	}

	if (hf_coef_wren) {
		aml_set_reg32_bits(P_VPP2_OSD_SCALE_COEF_IDX, 0x0100, 0, 9);
		for (i = 0; i < 33; i++)
		{
			aml_write_reg32(P_VPP2_OSD_SCALE_COEF, hf_coef[i]);
		}
	}
	remove_from_update_list(OSD1,OSD_FREESCALE_COEF);
#endif
}

static  void  osd2_update_disp_freescale_enable(void)
{
	int hf_phase_step, vf_phase_step;
	int dst_w, dst_h;
	int vsc_ini_rcv_num, vsc_ini_rpt_p0_num;
	int hsc_ini_rcv_num, hsc_ini_rpt_p0_num;

	int hf_bank_len = 4;
	int vf_bank_len = 4;

	hsc_ini_rcv_num = hf_bank_len;
	vsc_ini_rcv_num = vf_bank_len;
	hsc_ini_rpt_p0_num = (hf_bank_len/2 - 1) > 0 ?  (hf_bank_len/2 - 1): 0;
	vsc_ini_rpt_p0_num = (vf_bank_len/2 - 1) > 0 ?  (vf_bank_len/2 - 1): 0;

	dst_w = osd_ext_hw.free_dst_data[OSD2].x_end -osd_ext_hw.free_dst_data[OSD2].x_start+1;
	hf_phase_step = ((osd_ext_hw.free_scale_width[OSD2]+1)<< 18) / dst_w;
	hf_phase_step = (hf_phase_step << 6);

	dst_h = osd_ext_hw.free_dst_data[OSD2].y_end - osd_ext_hw.free_dst_data[OSD2].y_start+1;
	vf_phase_step = ((osd_ext_hw.free_scale_height[OSD2]+1) << 20) / dst_h;
	vf_phase_step = (vf_phase_step << 4);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	aml_set_reg32_bits(P_VPP2_OSD_SC_DUMMY_DATA, 0x00808000, 0, 32);

	if (osd_ext_hw.free_scale_enable[OSD2]){
		aml_set_reg32_bits (P_VPP2_OSD_SC_CTRL0, 1, 3, 1);
		aml_set_reg32_bits (P_VPP2_OSD_SC_CTRL0, OSD2, 0, 2);
		aml_set_reg32_bits (P_VPP2_OSD_SC_CTRL0, 0, 4, 8);
	}else{
		aml_clr_reg32_mask (P_VPP2_OSD_SC_CTRL0, 1<<3);
	}

	if (osd_ext_hw.free_scale_enable[OSD2]){
		aml_set_reg32_bits(P_VPP2_OSD_SCI_WH_M1, osd_ext_hw.free_scale_width[OSD2], 16, 13);
		aml_set_reg32_bits(P_VPP2_OSD_SCI_WH_M1, osd_ext_hw.free_scale_height[OSD2], 0, 13);

		aml_set_reg32_bits(P_VPP2_OSD_SCO_H_START_END, osd_ext_hw.free_dst_data[OSD2].x_start, 16, 12);
		aml_set_reg32_bits(P_VPP2_OSD_SCO_H_START_END, osd_ext_hw.free_dst_data[OSD2].x_end, 0, 12);

		aml_set_reg32_bits(P_VPP2_OSD_SCO_V_START_END, osd_ext_hw.free_dst_data[OSD2].y_start, 16, 12);
		aml_set_reg32_bits(P_VPP2_OSD_SCO_V_START_END, osd_ext_hw.free_dst_data[OSD2].y_end, 0, 12);
	}

	if (osd_ext_hw.free_scale[OSD2].hfs_enable){
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, hf_bank_len, 0, 3);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, hsc_ini_rcv_num, 3, 3);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, hsc_ini_rpt_p0_num, 8, 2);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_CTRL0, 1, 22, 1);
	}else{
		aml_clr_reg32_mask(P_VPP2_OSD_HSC_CTRL0, 1<<22);
	}

	if (osd_ext_hw.free_scale[OSD2].vfs_enable){
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, vf_bank_len, 0, 3);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, vsc_ini_rcv_num, 3, 3);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, vsc_ini_rpt_p0_num, 8, 2);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_CTRL0, 1, 24, 1);
	}else{
		aml_clr_reg32_mask(P_VPP2_OSD_VSC_CTRL0, 1<<24);
	}

	if (osd_ext_hw.free_scale_enable[OSD2]){
		aml_set_reg32_bits(P_VPP2_OSD_HSC_PHASE_STEP, hf_phase_step, 0, 28);
		aml_set_reg32_bits(P_VPP2_OSD_HSC_INI_PHASE, 0, 0, 16);

		aml_set_reg32_bits(P_VPP2_OSD_VSC_PHASE_STEP, vf_phase_step, 0, 28);
		aml_set_reg32_bits(P_VPP2_OSD_VSC_INI_PHASE, 0, 0, 16);
	}
	remove_from_update_list(OSD2,DISP_FREESCALE_ENABLE);
#endif
}

static void osd2_update_coef(void)
{
	int vf_coef_idx = 0;
	int  *vf_coef;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	int i;
	int hf_coef_idx = 0;
	int vf_coef_wren = 1;
	int hf_coef_wren = 1;
	int *hf_coef;
#endif
	if (vf_coef_idx == 0){
		vf_coef = filt_coef0;
	}else if (vf_coef_idx == 1){
		vf_coef = filt_coef1;
	}else if (vf_coef_idx == 2){
		vf_coef = filt_coef2;
	}else{
		vf_coef = filt_coef0;
	}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if (vf_coef_wren) {
		aml_set_reg32_bits (P_VPP2_OSD_SCALE_COEF_IDX, 0x0000, 0, 9);
		for (i = 0; i < 33; i++)
		{
			aml_write_reg32(P_VPP2_OSD_SCALE_COEF, vf_coef[i]);
		}
	}

	if (hf_coef_idx == 0){
		hf_coef = filt_coef0;
	}else if (hf_coef_idx == 1){
		hf_coef = filt_coef1;
	}else if (hf_coef_idx == 2){
		hf_coef = filt_coef2;
	}else{
		hf_coef = filt_coef0;
	}

	if (hf_coef_wren) {
		aml_set_reg32_bits (P_VPP2_OSD_SCALE_COEF_IDX, 0x0100, 0, 9);
		for (i = 0; i < 33; i++)
		{
			aml_write_reg32(P_VPP2_OSD_SCALE_COEF, hf_coef[i]);
		}
	}
	remove_from_update_list(OSD2,OSD_FREESCALE_COEF);
#endif
}

static void osd1_update_color_mode(void)
{
	u32 data32 = 0;

	if (osd_ext_hw.color_info[OSD1] != NULL) {
		data32 = (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) ? 2 : 0;
		data32 |= aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W0)&0x30007040;
		data32 |= osd_ext_hw.fb_gem[OSD1].canvas_idx << 16;
		if(!osd_ext_hw.rotate[OSD1].on_off)
		data32 |= OSD_DATA_LITTLE_ENDIAN << 15;
		data32 |= osd_ext_hw.color_info[OSD1]->hw_colormat << 2;
		if (osd_ext_hw.color_info[OSD1]->color_index < COLOR_INDEX_YUV_422)
			data32 |= 1 << 7;	/* rgb enable */
		data32 |= osd_ext_hw.color_info[OSD1]->hw_blkmode << 8;	/* osd_ext_blk_mode */
		aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W0, data32);
		aml_write_reg32(P_VIU2_OSD1_BLK1_CFG_W0, data32);
		aml_write_reg32(P_VIU2_OSD1_BLK2_CFG_W0, data32);
		aml_write_reg32(P_VIU2_OSD1_BLK3_CFG_W0, data32);
	}
	remove_from_update_list(OSD1, OSD_COLOR_MODE);
}

static void osd2_update_color_mode(void)
{
	u32 data32 = 0;
	if (osd_ext_hw.color_info[OSD2] != NULL) {
		data32 = (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) ? 2 : 0;
		data32 |= aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W0)&0x30007040;
		data32 |= osd_ext_hw.fb_gem[OSD2].canvas_idx << 16;
		if(!osd_ext_hw.rotate[OSD1].on_off)
		data32 |= OSD_DATA_LITTLE_ENDIAN << 15;
		data32 |= osd_ext_hw.color_info[OSD2]->hw_colormat << 2;
		if (osd_ext_hw.color_info[OSD2]->color_index < COLOR_INDEX_YUV_422)
			data32 |= 1 << 7;	/* rgb enable */
		data32 |= osd_ext_hw.color_info[OSD2]->hw_blkmode << 8;	/* osd_ext_blk_mode */
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W0, data32);
	}
	remove_from_update_list(OSD2, OSD_COLOR_MODE);
}

static void osd1_update_enable(void)
{
	if (osd_ext_hw.free_scale_mode[OSD1]){
		if (osd_ext_hw.enable[OSD1] == ENABLE){
			aml_set_reg32_mask(P_VPP2_MISC,VPP_OSD1_POSTBLEND);
			aml_set_reg32_mask(P_VPP2_MISC,VPP_POSTBLEND_EN);
		}else{
			aml_clr_reg32_mask(P_VPP2_MISC,VPP_OSD1_POSTBLEND);
		}
	}else{
		u32 video_enable = 0;

		video_enable |= aml_read_reg32(P_VPP2_MISC) & VPP_VD1_PREBLEND;
		if (osd_ext_hw.enable[OSD1] == ENABLE) {
			if (osd_ext_hw.free_scale_enable[OSD1]) {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD1_POSTBLEND);
				aml_set_reg32_mask(P_VPP2_MISC, VPP_OSD1_PREBLEND);
				aml_set_reg32_mask(P_VPP2_MISC, VPP_VD1_POSTBLEND);
				aml_set_reg32_mask(P_VPP2_MISC, VPP_PREBLEND_EN);
			} else {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD1_PREBLEND);
				if (!video_enable) {
					aml_clr_reg32_mask(P_VPP2_MISC, VPP_VD1_POSTBLEND);
				}
				aml_set_reg32_mask(P_VPP2_MISC, VPP_OSD1_POSTBLEND);
			}

		} else {
			if (osd_ext_hw.free_scale_enable[OSD1]) {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD1_PREBLEND);
			} else {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD1_POSTBLEND);
			}
		}
	}
	remove_from_update_list(OSD1, OSD_ENABLE);
}

static void osd2_update_enable(void)
{
	if (osd_ext_hw.free_scale_mode[OSD2]){
		if (osd_ext_hw.enable[OSD1] == ENABLE){
			aml_set_reg32_mask(P_VPP2_MISC,VPP_OSD2_POSTBLEND);
			aml_set_reg32_mask(P_VPP2_MISC,VPP_POSTBLEND_EN);
		}else{
			aml_clr_reg32_mask(P_VPP2_MISC,VPP_OSD2_POSTBLEND);
		}
	}else{
		u32 video_enable = 0;

		video_enable |= aml_read_reg32(P_VPP2_MISC) & VPP_VD1_PREBLEND;
		if (osd_ext_hw.enable[OSD2] == ENABLE) {
			if (osd_ext_hw.free_scale_enable[OSD2]) {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD2_POSTBLEND);
				aml_set_reg32_mask(P_VPP2_MISC, VPP_OSD2_PREBLEND);
				aml_set_reg32_mask(P_VPP2_MISC, VPP_VD1_POSTBLEND);
			} else {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD2_PREBLEND);
				if (!video_enable) {
					aml_clr_reg32_mask(P_VPP2_MISC, VPP_VD1_POSTBLEND);
				}
				aml_set_reg32_mask(P_VPP2_MISC, VPP_OSD2_POSTBLEND);
			}

		} else {
			if (osd_ext_hw.free_scale_enable[OSD2]) {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD2_PREBLEND);
			} else {
				aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD2_POSTBLEND);
			}
		}
	}
	remove_from_update_list(OSD2, OSD_ENABLE);
}

static void osd1_update_disp_osd_rotate(void)
{
	unsigned char 	x_rev=0,y_rev=0;
	unsigned char 	bpp=32;
	unsigned int	x_start;
	unsigned int	x_end;
	unsigned int	y_start;
	unsigned int	y_end;
	unsigned int	y_len_m1;

	if( osd_ext_hw.color_info[OSD1]->color_index <=COLOR_INDEX_08_PAL256){
		bpp=8;
	}else if( osd_ext_hw.color_info[OSD1]->color_index <= COLOR_INDEX_16_565){
		bpp=16;
	}else if( osd_ext_hw.color_info[OSD1]->color_index <= COLOR_INDEX_24_RGB){
		bpp=24;
	}else if( osd_ext_hw.color_info[OSD1]->color_index <= COLOR_INDEX_32_ARGB){
		bpp=32;
	}

	switch(osd_ext_hw.rotate[OSD1].angle)
	{
	  case 0://clockwise H flip (dst )
	  x_rev=0;
	  y_rev=0;
	  break;//clockwise
	  case 1:
	  y_rev=1;
	  break;
	  case 2://anti-clockwise
	  x_rev=1;
	  break;
	  case 3://anti-clockwise H flip(dst)
	  x_rev=1;
	  y_rev=1;
	  break;
	}

	x_start = osd_ext_hw.rotation_pandata[OSD1].x_start;
	x_end = osd_ext_hw.rotation_pandata[OSD1].x_end;
	y_start = osd_ext_hw.rotation_pandata[OSD1].y_start;
	y_end = osd_ext_hw.rotation_pandata[OSD1].y_end;
	y_len_m1 = y_end-y_start;

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
	osd_ext_set_prot(
                x_rev,
                y_rev,
                (bpp>>3)-1,				//unsigned char   bytes_per_pixel,
                0,				//unsigned char   conv_422to444,
                OSD_DATA_LITTLE_ENDIAN,		//unsigned char   little_endian,
                HOLD_LINES,			//unsigned int    hold_lines,
                x_start,
                x_end,
                y_start,
                y_end,
                y_len_m1,
                Y_STEP,
                PAT_START_PTR,
                PAT_END_PTR,
                PAT_VAL,
                osd_ext_hw.fb_gem[OSD1].canvas_idx,
                CID_VALUE,
                CID_MODE,
                CUGT,				//urgent bit
                REQ_ONOFF_EN,
                REQ_ON_MAX,
                REQ_OFF_MIN,
                OSD1,
                osd_ext_hw.rotate[OSD1].on_off);
	remove_from_update_list(OSD1, DISP_OSD_ROTATE);
#endif
}

static void osd2_update_disp_osd_rotate(void)
{
	unsigned char 	x_rev=0,y_rev=0;
	unsigned char 	bpp=32;
	unsigned int	x_start;
	unsigned int	x_end;
	unsigned int	y_start;
	unsigned int	y_end;
	unsigned int	y_len_m1;

	if( osd_ext_hw.color_info[OSD2]->color_index <=COLOR_INDEX_08_PAL256){
		bpp=8;
	}else if( osd_ext_hw.color_info[OSD2]->color_index <= COLOR_INDEX_16_565){
		bpp=16;
	}else if( osd_ext_hw.color_info[OSD2]->color_index <= COLOR_INDEX_24_RGB){
		bpp=24;
	}else if( osd_ext_hw.color_info[OSD2]->color_index <= COLOR_INDEX_32_ARGB){
		bpp=32;
	}
	switch(osd_ext_hw.rotate[OSD2].angle)
	{
	  case 0://clockwise H flip (dst )
	  x_rev=0;
	  y_rev=0;
	  break;//clockwise
	  case 1:
	  y_rev=1;
	  break;
	  case 2://anti-clockwise
	  x_rev=1;
      break;
	  case 3://anti-clockwise H flip(dst)
	  x_rev=1;
	  y_rev=1;
	  break;
	}

	x_start = osd_ext_hw.rotation_pandata[OSD2].x_start;
	x_end = osd_ext_hw.rotation_pandata[OSD2].x_end;
	y_start = osd_ext_hw.rotation_pandata[OSD2].y_start;
	y_end = osd_ext_hw.rotation_pandata[OSD2].y_end;
	y_len_m1 = y_end-y_start;

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
	osd_ext_set_prot(
                x_rev,
                y_rev,
                (bpp>>3)-1,				//unsigned char   bytes_per_pixel,
                0,				//unsigned char   conv_422to444,
                OSD_DATA_LITTLE_ENDIAN,		//unsigned char   little_endian,
                HOLD_LINES,			//unsigned int    hold_lines,
                x_start,
                x_end,
                y_start,
                y_end,
                y_len_m1,
                Y_STEP,
                PAT_START_PTR,
                PAT_END_PTR,
                PAT_VAL,
                osd_ext_hw.fb_gem[OSD2].canvas_idx,
                CID_VALUE,
                CID_MODE,
                CUGT,				//urgent bit
                REQ_ONOFF_EN,
                REQ_ON_MAX,
                REQ_OFF_MIN,
                OSD2,
                osd_ext_hw.rotate[OSD2].on_off);
    remove_from_update_list(OSD2, DISP_OSD_ROTATE);
#endif
}

static void osd1_update_color_key(void)
{
	aml_write_reg32(P_VIU2_OSD1_TCOLOR_AG0, osd_ext_hw.color_key[OSD1]);
	remove_from_update_list(OSD1, OSD_COLOR_KEY);
}

static void osd2_update_color_key(void)
{
	aml_write_reg32(P_VIU2_OSD2_TCOLOR_AG0, osd_ext_hw.color_key[OSD2]);
	remove_from_update_list(OSD2, OSD_COLOR_KEY);
}

static void osd1_update_color_key_enable(void)
{
	u32 data32;

	data32 = aml_read_reg32(P_VIU2_OSD1_BLK0_CFG_W0);
	data32 &= ~(1 << 6);
	data32 |= (osd_ext_hw.color_key_enable[OSD1] << 6);
	aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W0, data32);
	aml_write_reg32(P_VIU2_OSD1_BLK1_CFG_W0, data32);
	aml_write_reg32(P_VIU2_OSD1_BLK2_CFG_W0, data32);
	aml_write_reg32(P_VIU2_OSD1_BLK3_CFG_W0, data32);
	remove_from_update_list(OSD1, OSD_COLOR_KEY_ENABLE);
}

static void osd2_update_color_key_enable(void)
{
	u32 data32;

	data32 = aml_read_reg32(P_VIU2_OSD2_BLK0_CFG_W0);
	data32 &= ~(1 << 6);
	data32 |= (osd_ext_hw.color_key_enable[OSD2] << 6);
	aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W0, data32);
	remove_from_update_list(OSD2, OSD_COLOR_KEY_ENABLE);
}

static void osd1_update_gbl_alpha(void)
{

	u32 data32 = aml_read_reg32(P_VIU2_OSD1_CTRL_STAT);
	data32 &= ~(0x1ff << 12);
	data32 |= osd_ext_hw.gbl_alpha[OSD1] << 12;
	aml_write_reg32(P_VIU2_OSD1_CTRL_STAT, data32);
	remove_from_update_list(OSD1, OSD_GBL_ALPHA);
}

static void osd2_update_gbl_alpha(void)
{

	u32 data32 = aml_read_reg32(P_VIU2_OSD2_CTRL_STAT);
	data32 &= ~(0x1ff << 12);
	data32 |= osd_ext_hw.gbl_alpha[OSD2] << 12;
	aml_write_reg32(P_VIU2_OSD2_CTRL_STAT, data32);
	remove_from_update_list(OSD2, OSD_GBL_ALPHA);
}

static void osd2_update_order(void)
{
	switch (osd_ext_hw.osd_ext_order) {
	case OSD_ORDER_01:
		aml_clr_reg32_mask(P_VPP2_MISC, VPP_POST_FG_OSD2 | VPP_PRE_FG_OSD2);
		break;
	case OSD_ORDER_10:
		aml_set_reg32_mask(P_VPP2_MISC, VPP_POST_FG_OSD2 | VPP_PRE_FG_OSD2);
		break;
	default:
		break;
	}
	remove_from_update_list(OSD2, OSD_CHANGE_ORDER);
}

static void osd1_update_order(void)
{
	switch (osd_ext_hw.osd_ext_order) {
	case OSD_ORDER_01:
		aml_clr_reg32_mask(P_VPP2_MISC, VPP_POST_FG_OSD2 | VPP_PRE_FG_OSD2);
		break;
	case OSD_ORDER_10:
		aml_set_reg32_mask(P_VPP2_MISC, VPP_POST_FG_OSD2 | VPP_PRE_FG_OSD2);
		break;
	default:
		break;
	}
	remove_from_update_list(OSD1, OSD_CHANGE_ORDER);
}

static void osd_ext_block_update_disp_geometry(u32 index)
{
	u32 data32;
	u32 data_w1, data_w2, data_w3, data_w4;
	u32 coef[4][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };
	u32 xoff, yoff;
	u32 i;

	switch (osd_ext_hw.block_mode[index] & HW_OSD_BLOCK_LAYOUT_MASK) {
	case HW_OSD_BLOCK_LAYOUT_HORIZONTAL:
		yoff =
		    ((osd_ext_hw.pandata[index].y_end & 0x1fff) - (osd_ext_hw.pandata[index].y_start & 0x1fff) +
		     1) >> 2;
		data_w1 =
		    (osd_ext_hw.pandata[index].x_start & 0x1fff) | (osd_ext_hw.pandata[index].x_end & 0x1fff) << 16;
		data_w3 =
		    (osd_ext_hw.dispdata[index].x_start & 0xfff) | (osd_ext_hw.dispdata[index].x_end & 0xfff) << 16;
		for (i = 0; i < 4; i++) {
			if (i == 3) {
				data_w2 = ((osd_ext_hw.pandata[index].y_start + yoff * i) & 0x1fff)
				    | (osd_ext_hw.pandata[index].y_end & 0x1fff) << 16;
				data_w4 = ((osd_ext_hw.dispdata[index].y_start + yoff * i) & 0xfff)
				    | (osd_ext_hw.dispdata[index].y_end & 0xfff) << 16;
			} else {
				data_w2 = ((osd_ext_hw.pandata[index].y_start + yoff * i) & 0x1fff)
				    | ((osd_ext_hw.pandata[index].y_start + yoff * (i + 1) - 1) & 0x1fff) << 16;
				data_w4 = ((osd_ext_hw.dispdata[index].y_start + yoff * i) & 0xfff)
				    | ((osd_ext_hw.dispdata[index].y_start + yoff * (i + 1) - 1) & 0xfff) << 16;
			}
			if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
				data32 = data_w4;
				data_w4 = ((data32 & 0xfff) >> 1) | ((((((data32 >> 16) & 0xfff) + 1) >> 1) - 1) << 16);
			}

			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1 + (i << 4), data_w1);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2 + (i << 4), data_w2);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3 + (i << 4), data_w3);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4 + (i << 2), data_w4);

			osd_ext_hw.block_windows[index][i << 1] = data_w1;
			osd_ext_hw.block_windows[index][(i << 1) + 1] = data_w2;
		}
		break;
	case HW_OSD_BLOCK_LAYOUT_VERTICAL:
		xoff =
		    ((osd_ext_hw.pandata[index].x_end & 0x1fff) - (osd_ext_hw.pandata[index].x_start & 0x1fff) +
		     1) >> 2;
		data_w2 =
		    (osd_ext_hw.pandata[index].y_start & 0x1fff) | (osd_ext_hw.pandata[index].y_end & 0x1fff) << 16;
		data_w4 =
		    (osd_ext_hw.dispdata[index].y_start & 0xfff) | (osd_ext_hw.dispdata[index].y_end & 0xfff) << 16;
		if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
			data32 = data_w4;
			data_w4 = ((data32 & 0xfff) >> 1) | ((((((data32 >> 16) & 0xfff) + 1) >> 1) - 1) << 16);
		}
		for (i = 0; i < 4; i++) {
			data_w1 = ((osd_ext_hw.pandata[index].x_start + xoff * i) & 0x1fff)
			    | ((osd_ext_hw.pandata[index].x_start + xoff * (i + 1) - 1) & 0x1fff) << 16;
			data_w3 = ((osd_ext_hw.dispdata[index].x_start + xoff * i) & 0xfff)
			    | ((osd_ext_hw.dispdata[index].x_start + xoff * (i + 1) - 1) & 0xfff) << 16;

			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1 + (i << 4), data_w1);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2 + (i << 4), data_w2);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3 + (i << 4), data_w3);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4 + (i << 2), data_w4);

			osd_ext_hw.block_windows[index][i << 1] = data_w1;
			osd_ext_hw.block_windows[index][(i << 1) + 1] = data_w2;
		}
		break;
	case HW_OSD_BLOCK_LAYOUT_GRID:
		xoff =
		    ((osd_ext_hw.pandata[index].x_end & 0x1fff) - (osd_ext_hw.pandata[index].x_start & 0x1fff) +
		     1) >> 1;
		yoff =
		    ((osd_ext_hw.pandata[index].y_end & 0x1fff) - (osd_ext_hw.pandata[index].y_start & 0x1fff) +
		     1) >> 1;
		for (i = 0; i < 4; i++) {
			data_w1 = ((osd_ext_hw.pandata[index].x_start + xoff * coef[i][0]) & 0x1fff)
			    | ((osd_ext_hw.pandata[index].x_start + xoff * (coef[i][0] + 1) - 1) & 0x1fff) << 16;
			data_w2 = ((osd_ext_hw.pandata[index].y_start + yoff * coef[i][1]) & 0x1fff)
			    | ((osd_ext_hw.pandata[index].y_start + yoff * (coef[i][1] + 1) - 1) & 0x1fff) << 16;
			data_w3 = ((osd_ext_hw.dispdata[index].x_start + xoff * coef[i][0]) & 0xfff)
			    | ((osd_ext_hw.dispdata[index].x_start + xoff * (coef[i][0] + 1) - 1) & 0xfff) << 16;
			data_w4 = ((osd_ext_hw.dispdata[index].y_start + yoff * coef[i][1]) & 0xfff)
			    | ((osd_ext_hw.dispdata[index].y_start + yoff * (coef[i][1] + 1) - 1) & 0xfff) << 16;

			if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
				data32 = data_w4;
				data_w4 = ((data32 & 0xfff) >> 1) | ((((((data32 >> 16) & 0xfff) + 1) >> 1) - 1) << 16);
			}
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1 + (i << 4), data_w1);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2 + (i << 4), data_w2);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3 + (i << 4), data_w3);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4 + (i << 2), data_w4);

			osd_ext_hw.block_windows[index][i << 1] = data_w1;
			osd_ext_hw.block_windows[index][(i << 1) + 1] = data_w2;
		}
		break;
	case HW_OSD_BLOCK_LAYOUT_CUSTOMER:
		for (i = 0; i < 4; i++) {
			if (((osd_ext_hw.block_windows[index][i << 1] >> 16) & 0x1fff) >
			    osd_ext_hw.pandata[index].x_end) {
				osd_ext_hw.block_windows[index][i << 1] =
				    (osd_ext_hw.block_windows[index][i << 1] & 0x1fff)
				    | ((osd_ext_hw.pandata[index].x_end & 0x1fff) << 16);
			}
			data_w1 = osd_ext_hw.block_windows[index][i << 1] & 0x1fff1fff;
			data_w2 =
			    ((osd_ext_hw.pandata[index].y_start & 0x1fff) +
			     (osd_ext_hw.block_windows[index][(i << 1) + 1] & 0x1fff))
			    | (((osd_ext_hw.pandata[index].y_start & 0x1fff) << 16) +
			       (osd_ext_hw.block_windows[index][(i << 1) + 1] & 0x1fff0000));
			data_w3 = (osd_ext_hw.dispdata[index].x_start + (data_w1 & 0xfff))
			    | (((osd_ext_hw.dispdata[index].x_start & 0xfff) << 16) + (data_w1 & 0xfff0000));
			data_w4 =
			    (osd_ext_hw.dispdata[index].y_start +
			     (osd_ext_hw.block_windows[index][(i << 1) + 1] & 0xfff))
			    | (((osd_ext_hw.dispdata[index].y_start & 0xfff) << 16) +
			       (osd_ext_hw.block_windows[index][(i << 1) + 1] & 0xfff0000));
			if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
				data32 = data_w4;
				data_w4 = ((data32 & 0xfff) >> 1) | ((((((data32 >> 16) & 0xfff) + 1) >> 1) - 1) << 16);
			}
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1 + (i << 4), data_w1);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2 + (i << 4), data_w2);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3 + (i << 4), data_w3);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4 + (i << 2), data_w4);
		}
		break;

	default:
		amlog_level(LOG_LEVEL_HIGH, "ERROR block_mode: 0x%x\n", osd_ext_hw.block_mode[index]);
		break;
	}
}

static void osd1_update_disp_geometry(void)
{
	u32 data32;

	/* enable osd multi block */
	if (osd_ext_hw.block_mode[OSD1]) {
		osd_ext_block_update_disp_geometry(OSD1);
		data32 = aml_read_reg32(P_VIU2_OSD1_CTRL_STAT);
		data32 &= 0xfffffff0;
		data32 |= (osd_ext_hw.block_mode[OSD1] & HW_OSD_BLOCK_ENABLE_MASK);
		aml_write_reg32(P_VIU2_OSD1_CTRL_STAT, data32);
	} else {
		if (osd_ext_hw.rotate[OSD1].on_off > 0 && osd_ext_hw.rotate[OSD1].angle > 0){
			data32 = (osd_ext_hw.dispdata[OSD1].x_start & 0xfff) | ((osd_ext_hw.dispdata[OSD1].x_start+g_rotation_height) & 0xfff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3, data32);
			data32 = (osd_ext_hw.dispdata[OSD1].y_start & 0xfff) | ((osd_ext_hw.dispdata[OSD1].y_start+g_rotation_width) & 0xfff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4, data32);
		}else{
			data32 = (osd_ext_hw.dispdata[OSD1].x_start & 0xfff) | (osd_ext_hw.dispdata[OSD1].x_end & 0xfff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3, data32);
			if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
				data32 = ((osd_ext_hw.dispdata[OSD1].y_start >> 1) & 0xfff) | ((((osd_ext_hw.dispdata[OSD1].y_end + 1) >> 1) -1) & 0xfff) << 16;
			} else {
				data32 = (osd_ext_hw.dispdata[OSD1].y_start & 0xfff) | (osd_ext_hw.dispdata[OSD1].y_end & 0xfff) << 16;
			}
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4, data32);
		}
		/* enable osd 2x scale */
		if (osd_ext_hw.scale[OSD1].h_enable || osd_ext_hw.scale[OSD1].v_enable) {
			data32 = (osd_ext_hw.scaledata[OSD1].x_start & 0x1fff) | (osd_ext_hw.scaledata[OSD1].x_end & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1, data32);
			data32 = ((osd_ext_hw.scaledata[OSD1].y_start + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff)
			    | ((osd_ext_hw.scaledata[OSD1].y_end + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2, data32);
			/* adjust display x-axis */
			if (osd_ext_hw.scale[OSD1].h_enable) {
				data32 = (osd_ext_hw.dispdata[OSD1].x_start & 0xfff)|((osd_ext_hw.dispdata[OSD1].x_start +
				      (osd_ext_hw.scaledata[OSD1].x_end - osd_ext_hw.scaledata[OSD1].x_start) * 2 + 1) & 0xfff) << 16;
				aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W3, data32);
			}

			/* adjust display y-axis */
			if (osd_ext_hw.scale[OSD1].v_enable) {
				if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
					data32 = ((osd_ext_hw.dispdata[OSD1].y_start >> 1) & 0xfff)|(((((osd_ext_hw.dispdata[OSD1].y_start +
						 (osd_ext_hw.scaledata[OSD1].y_end -osd_ext_hw.scaledata[OSD1].y_start) * 2) + 1) >> 1) -1) & 0xfff) << 16;
				} else {
					data32 = (osd_ext_hw.dispdata[OSD1].y_start & 0xfff)|(((osd_ext_hw.dispdata[OSD1].y_start +
						 (osd_ext_hw.scaledata[OSD1].y_end -osd_ext_hw.scaledata[OSD1].y_start) * 2)) & 0xfff) << 16;
				}
				aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W4, data32);
			}
		} else if (osd_ext_hw.free_scale_enable[OSD1]
			   && (osd_ext_hw.free_scale_data[OSD1].x_end > 0)
			   && (osd_ext_hw.free_scale_data[OSD1].y_end > 0)) {
			/* enable osd free scale */
			data32 = (osd_ext_hw.free_scale_data[OSD1].x_start & 0x1fff) |
						(osd_ext_hw.free_scale_data[OSD1].x_end & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1, data32);
			data32 = ((osd_ext_hw.free_scale_data[OSD1].y_start + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff) |
				((osd_ext_hw.free_scale_data[OSD1].y_end + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2, data32);
		} else if (osd_ext_hw.rotate[OSD1].on_off > 0
				&& osd_ext_hw.rotate[OSD1].angle > 0){
			/* enable osd rotation */
			data32 = (osd_ext_hw.rotation_pandata[OSD1].x_start & 0x1fff) | (osd_ext_hw.rotation_pandata[OSD1].x_end & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1,data32);
			/*data32 = ((osd_ext_hw.rotation_pandata[OSD1].y_start + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff)
					| ((osd_ext_hw.rotation_pandata[OSD1].y_end  + osd_ext_hw.pandata[OSD1].y_start) & 0x1fff) << 16 ;
			aml_write_reg32(P_VPU_PROT1_Y_START_END, data32);
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2,data32);*/
		}else{
			/* normal mode */
			data32 =
			    (osd_ext_hw.pandata[OSD1].x_start & 0x1fff) | (osd_ext_hw.pandata[OSD1].x_end & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1, data32);
			data32 =
			    (osd_ext_hw.pandata[OSD1].y_start & 0x1fff) | (osd_ext_hw.pandata[OSD1].y_end & 0x1fff) << 16;
			aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W2, data32);
		}
		data32 = aml_read_reg32(P_VIU2_OSD1_CTRL_STAT);
		data32 &= 0xfffffff0;
		data32 |= HW_OSD_BLOCK_ENABLE_0;
		aml_write_reg32(P_VIU2_OSD1_CTRL_STAT, data32);
	}

	remove_from_update_list(OSD1, DISP_GEOMETRY);
}

static void osd2_update_disp_geometry(void)
{
	u32 data32;
	data32 = (osd_ext_hw.dispdata[OSD2].x_start & 0xfff) | (osd_ext_hw.dispdata[OSD2].x_end & 0xfff) << 16;
	aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W3, data32);
	if (osd_ext_hw.scan_mode == SCAN_MODE_INTERLACE) {
		data32 =
		    ((osd_ext_hw.dispdata[OSD2].
		      y_start >> 1) & 0xfff) | ((((osd_ext_hw.dispdata[OSD2].y_end + 1) >> 1) - 1) & 0xfff) << 16;
	} else {
		data32 = (osd_ext_hw.dispdata[OSD2].y_start & 0xfff) | (osd_ext_hw.dispdata[OSD2].y_end & 0xfff) << 16;
	}
	aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W4, data32);

	if (osd_ext_hw.scale[OSD2].h_enable || osd_ext_hw.scale[OSD2].v_enable) {
#if defined(CONFIG_FB_OSD2_CURSOR)
		data32 = (osd_ext_hw.pandata[OSD2].x_start & 0x1fff) | (osd_ext_hw.pandata[OSD2].x_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W1, data32);
		data32 = (osd_ext_hw.pandata[OSD2].y_start & 0x1fff) | (osd_ext_hw.pandata[OSD2].y_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W2, data32);
#else
		data32 =
		    (osd_ext_hw.scaledata[OSD2].x_start & 0x1fff) | (osd_ext_hw.scaledata[OSD2].x_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W1, data32);
		data32 = ((osd_ext_hw.scaledata[OSD2].y_start + osd_ext_hw.pandata[OSD2].y_start) & 0x1fff)
		    | ((osd_ext_hw.scaledata[OSD2].y_end + osd_ext_hw.pandata[OSD2].y_start) & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W2, data32);
#endif
	} else {
		data32 = (osd_ext_hw.pandata[OSD2].x_start & 0x1fff) | (osd_ext_hw.pandata[OSD2].x_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W1, data32);
		data32 = (osd_ext_hw.pandata[OSD2].y_start & 0x1fff) | (osd_ext_hw.pandata[OSD2].y_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W2, data32);
	}
	remove_from_update_list(OSD2, DISP_GEOMETRY);
}

static void osd1_update_disp_3d_mode(void)
{
	/*step 1 . set pan data */
	u32 data32;

	if (osd_ext_hw.mode_3d[OSD1].left_right == LEFT) {
		data32 = (osd_ext_hw.mode_3d[OSD1].l_start & 0x1fff) | (osd_ext_hw.mode_3d[OSD1].l_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1, data32);
	} else {
		data32 = (osd_ext_hw.mode_3d[OSD1].r_start & 0x1fff) | (osd_ext_hw.mode_3d[OSD1].r_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD1_BLK0_CFG_W1, data32);
	}
	osd_ext_hw.mode_3d[OSD1].left_right ^= 1;
}

static void osd2_update_disp_3d_mode(void)
{
	u32 data32;

	if (osd_ext_hw.mode_3d[OSD2].left_right == LEFT) {
		data32 = (osd_ext_hw.mode_3d[OSD2].l_start & 0x1fff) | (osd_ext_hw.mode_3d[OSD2].l_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W1, data32);
	} else {
		data32 = (osd_ext_hw.mode_3d[OSD2].r_start & 0x1fff) | (osd_ext_hw.mode_3d[OSD2].r_end & 0x1fff) << 16;
		aml_write_reg32(P_VIU2_OSD2_BLK0_CFG_W1, data32);
	}
	osd_ext_hw.mode_3d[OSD2].left_right ^= 1;
}

void osd_ext_init_hw(u32 logo_loaded)
{
	u32 group, idx, data32;

	for (group = 0; group < HW_OSD_COUNT; group++)
		for (idx = 0; idx < HW_REG_INDEX_MAX; idx++) {
			osd_ext_hw.reg[group][idx].update_func = hw_func_array[group][idx];
		}
	osd_ext_hw.updated[OSD1] = 0;
	osd_ext_hw.updated[OSD2] = 0;
	//here we will init default value ,these value only set once .
	if (!logo_loaded) {
		data32 = 1;         // Set DDR request priority to be urgent
		data32 |= 4 << 5;   // hold_fifo_lines
		data32 |= 3 << 10;  // burst_len_sel: 3=64
		data32 |= 32 << 12; // fifo_depth_val: 32*8=256

		aml_write_reg32(P_VIU2_OSD1_FIFO_CTRL_STAT, data32);
		aml_write_reg32(P_VIU2_OSD2_FIFO_CTRL_STAT, data32);

		aml_set_reg32_mask(P_VPP2_MISC, VPP_POSTBLEND_EN);
		aml_clr_reg32_mask(P_VPP2_MISC, VPP_PREBLEND_EN);
		aml_clr_reg32_mask(P_VPP2_MISC, VPP_OSD1_POSTBLEND | VPP_OSD2_POSTBLEND);
		data32 = 0x1 << 0;	// osd_ext_blk_enable
		data32 |= OSD_GLOBAL_ALPHA_DEF << 12;
		data32 |= (1 << 21);
		aml_write_reg32(P_VIU2_OSD1_CTRL_STAT, data32);
		aml_write_reg32(P_VIU2_OSD2_CTRL_STAT, data32);
	}
#if defined(CONFIG_FB_OSD2_CURSOR)
	aml_set_reg32_mask(P_VPP2_MISC, VPP_POST_FG_OSD2 | VPP_PRE_FG_OSD2);
	osd_ext_hw.osd_ext_order = OSD_ORDER_10;
#else
	aml_clr_reg32_mask(P_VPP2_MISC, VPP_POST_FG_OSD2 | VPP_PRE_FG_OSD2);
	osd_ext_hw.osd_ext_order = OSD_ORDER_01;
#endif

	osd_ext_hw.enable[OSD2] = osd_ext_hw.enable[OSD1] = DISABLE;
	osd_ext_hw.fb_gem[OSD1].canvas_idx = OSD3_CANVAS_INDEX;
	osd_ext_hw.fb_gem[OSD2].canvas_idx = OSD4_CANVAS_INDEX;
	osd_ext_hw.gbl_alpha[OSD1] = OSD_GLOBAL_ALPHA_DEF;
	osd_ext_hw.gbl_alpha[OSD2] = OSD_GLOBAL_ALPHA_DEF;
	osd_ext_hw.color_info[OSD1] = NULL;
	osd_ext_hw.color_info[OSD2] = NULL;
	vf.width = vf.height = 0;
	osd_ext_hw.color_key[OSD1] = osd_ext_hw.color_key[OSD2] = 0xffffffff;
	osd_ext_hw.free_scale_enable[OSD1] = osd_ext_hw.free_scale_enable[OSD2] = 0;
	osd_ext_hw.scale[OSD1].h_enable = osd_ext_hw.scale[OSD1].v_enable = 0;
	osd_ext_hw.scale[OSD2].h_enable = osd_ext_hw.scale[OSD2].v_enable = 0;
	osd_ext_hw.mode_3d[OSD2].enable = osd_ext_hw.mode_3d[OSD1].enable = 0;
	osd_ext_hw.block_mode[OSD1] = osd_ext_hw.block_mode[OSD2] = 0;
	osd_ext_hw.rotation_pandata[OSD1].x_start = osd_ext_hw.rotation_pandata[OSD1].y_start = 0;
	osd_ext_hw.rotation_pandata[OSD2].x_start = osd_ext_hw.rotation_pandata[OSD2].y_start = 0;
	memset(osd_ext_hw.rotate,0,sizeof(osd_rotate_t));

#ifdef FIQ_VSYNC
	osd_ext_hw.fiq_handle_item.handle = vsync_isr;
	osd_ext_hw.fiq_handle_item.key = (u32) vsync_isr;
	osd_ext_hw.fiq_handle_item.name = "osd_ext_vsync";
	if (register_fiq_bridge_handle(&osd_ext_hw.fiq_handle_item))
#else
	if (request_irq(INT_VIU2_VSYNC, &vsync_isr, IRQF_SHARED, "am_osd_ext_vsync", osd_ext_setup))
#endif
	{
		amlog_level(LOG_LEVEL_HIGH, "can't request irq for vsync\r\n");
	}
#ifdef FIQ_VSYNC
	request_fiq(INT_VIU2_VSYNC, &osd_ext_fiq_isr);
#endif

	return;
}

#if defined(CONFIG_FB_OSD2_CURSOR)
void osd_ext_cursor_hw(s16 x, s16 y, s16 xstart, s16 ystart, u32 osd_ext_w, u32 osd_ext_h, int index)
{
	dispdata_t disp_tmp;

	if (index != 1)
		return;

	memcpy(&disp_tmp, &osd_ext_hw.dispdata[OSD1], sizeof(dispdata_t));
	if (osd_ext_hw.scale[OSD2].h_enable && (osd_ext_hw.scaledata[OSD2].x_start > 0)
	    && (osd_ext_hw.scaledata[OSD2].x_end > 0)) {
		x = x * osd_ext_hw.scaledata[OSD2].x_end / osd_ext_hw.scaledata[OSD2].x_start;
		if (osd_ext_hw.scaledata[OSD2].x_end > osd_ext_hw.scaledata[OSD2].x_start) {
			disp_tmp.x_start =
			    osd_ext_hw.dispdata[OSD1].x_start * osd_ext_hw.scaledata[OSD2].x_end /
			    osd_ext_hw.scaledata[OSD2].x_start;
			disp_tmp.x_end =
			    osd_ext_hw.dispdata[OSD1].x_end * osd_ext_hw.scaledata[OSD2].x_end /
			    osd_ext_hw.scaledata[OSD2].x_start;
		}
	}

	if (osd_ext_hw.scale[OSD2].v_enable && (osd_ext_hw.scaledata[OSD2].y_start > 0)
	    && (osd_ext_hw.scaledata[OSD2].y_end > 0)) {
		y = y * osd_ext_hw.scaledata[OSD2].y_end / osd_ext_hw.scaledata[OSD2].y_start;
		if (osd_ext_hw.scaledata[OSD2].y_end > osd_ext_hw.scaledata[OSD2].y_start) {
			disp_tmp.y_start =
			    osd_ext_hw.dispdata[OSD1].y_start * osd_ext_hw.scaledata[OSD2].y_end /
			    osd_ext_hw.scaledata[OSD2].y_start;
			disp_tmp.y_end =
			    osd_ext_hw.dispdata[OSD1].y_end * osd_ext_hw.scaledata[OSD2].y_end /
			    osd_ext_hw.scaledata[OSD2].y_start;
		}
	}

	x += xstart;
	y += ystart;
	/**
	 * Use pandata to show a partial cursor when it is at the edge because the
	 * registers can't have negative values and because we need to manually
	 * clip the cursor when it is past the edge.  The edge is hardcoded
	 * to the OSD0 area.
	 */
	osd_ext_hw.dispdata[OSD2].x_start = x;
	osd_ext_hw.dispdata[OSD2].y_start = y;
	if (x < disp_tmp.x_start) {
		// if negative position, set osd to 0,y and pan.
		if ((disp_tmp.x_start - x) < osd_ext_w) {
			osd_ext_hw.pandata[OSD2].x_start = disp_tmp.x_start - x;
			osd_ext_hw.pandata[OSD2].x_end = osd_ext_w - 1;
		}
		osd_ext_hw.dispdata[OSD2].x_start = 0;
	} else {
		osd_ext_hw.pandata[OSD2].x_start = 0;
		if (x + osd_ext_w > disp_tmp.x_end) {
			// if past positive edge, set osd to inside of the edge and pan.
			if (x < osd_ext_hw.dispdata[OSD1].x_end)
				osd_ext_hw.pandata[OSD2].x_end = disp_tmp.x_end - x;
		} else {
			osd_ext_hw.pandata[OSD2].x_end = osd_ext_w - 1;
		}
	}
	if (y < disp_tmp.y_start) {
		if ((disp_tmp.y_start - y) < osd_ext_h) {
			osd_ext_hw.pandata[OSD2].y_start = disp_tmp.y_start - y;
			osd_ext_hw.pandata[OSD2].y_end = osd_ext_h - 1;
		}
		osd_ext_hw.dispdata[OSD2].y_start = 0;
	} else {
		osd_ext_hw.pandata[OSD2].y_start = 0;
		if (y + osd_ext_h > disp_tmp.y_end) {
			if (y < disp_tmp.y_end)
				osd_ext_hw.pandata[OSD2].y_end = disp_tmp.y_end - y;
		} else {
			osd_ext_hw.pandata[OSD2].y_end = osd_ext_h - 1;
		}
	}
	osd_ext_hw.dispdata[OSD2].x_end =
	    osd_ext_hw.dispdata[OSD2].x_start + osd_ext_hw.pandata[OSD2].x_end - osd_ext_hw.pandata[OSD2].x_start;
	osd_ext_hw.dispdata[OSD2].y_end =
	    osd_ext_hw.dispdata[OSD2].y_start + osd_ext_hw.pandata[OSD2].y_end - osd_ext_hw.pandata[OSD2].y_start;
	add_to_update_list(OSD2, DISP_GEOMETRY);
}
#endif				//CONFIG_FB_OSD2_CURSOR

void osd_ext_suspend_hw(void)
{
	osd_ext_hw.reg_status_save = aml_read_reg32(P_VPP2_MISC) & OSD_RELATIVE_BITS;

	aml_clr_reg32_mask(P_VPP2_MISC, OSD_RELATIVE_BITS);

	printk("osd_ext_suspended\n");

	return;
}

void osd_ext_resume_hw(void)
{
	aml_set_reg32_mask(P_VPP2_MISC, osd_ext_hw.reg_status_save);

	printk("osd_ext_resumed\n");

	return;
}

void osd_ext_clone_pan(u32 index)
{
	s32 offset = 0;
	s32 height_osd0 = 0;
	s32 height_osd2 = 0;

	height_osd0 = osd_hw.pandata[index].y_end - osd_hw.pandata[index].y_start + 1;
	height_osd2 = osd_ext_hw.pandata[index].y_end - osd_ext_hw.pandata[index].y_start + 1;

	if (osd_ext_hw.clone[index]) {
		if (osd_ext_hw.rotate[index].on_off > 0 && osd_ext_hw.rotate[index].angle > 0){
			if (osd_hw.pandata[index].y_start < height_osd0){
				if (osd_ext_hw.pandata[index].y_start >= height_osd2){
					offset -= g_rotation_height + 1;
				}else{
					offset = 0;
				}
			}else{
				if (osd_ext_hw.pandata[index].y_start < height_osd2){
					offset += g_rotation_height + 1;
				}else{
					offset = 0;
				}
			}
		}else{
			if (osd_hw.pandata[index].y_start < height_osd0){
				if (osd_ext_hw.pandata[index].y_start >= height_osd2){
					offset -= osd_ext_hw.pandata[index].y_end - osd_ext_hw.pandata[index].y_start + 1;
				}else{
					offset = 0;
				}
			}else{
				if (osd_ext_hw.pandata[index].y_start < height_osd2){
					offset += osd_ext_hw.pandata[index].y_end - osd_ext_hw.pandata[index].y_start + 1;
				}else{
					offset = 0;
				}
			}
		}
		osd_ext_hw.pandata[index].y_start += offset;
		osd_ext_hw.pandata[index].y_end += offset;
		if (osd_ext_hw.angle[index]) {
			osd_ext_clone_update_pan(osd_ext_hw.pandata[index].y_start ? 1: 0);
		}
		add_to_update_list(index, DISP_GEOMETRY);
		osd_ext_wait_vsync_hw();
	}
}

void osd_ext_get_angle_hw(u32 index, u32 * angle)
{
	amlog_level(LOG_LEVEL_HIGH, "get osd_ext[%d]->angle: %d\n", index, osd_ext_hw.angle[index]);
	*angle = osd_ext_hw.angle[index];
}

void osd_ext_set_angle_hw(u32 index, u32 angle)
{
#ifndef OSD_EXT_GE2D_CLONE_SUPPORT
	printk("++ osd_clone depends on GE2D module!\n");
	return;
#endif

	if(angle > 4) {
		printk("++ invalid angle: %d\n", angle);
		return;
	}

	if (osd_ext_hw.clone[index] == 0) {
		printk("++ set osd_ext[%d]->angle: %d->%d\n", index, osd_ext_hw.angle[index], angle);
		osd_ext_clone_set_angle(angle);
		osd_ext_hw.angle[index] = angle;
	} else if (!((osd_ext_hw.angle[index] == 0) || (angle == 0))) {
		printk("++ set osd_ext[%d]->angle: %d->%d\n", index, osd_ext_hw.angle[index], angle);
		osd_ext_clone_set_angle(angle);
		osd_ext_hw.angle[index] = angle;
		osd_ext_clone_pan(index);
	}
}
	
void osd_ext_get_clone_hw(u32 index, u32 * clone)
{
	amlog_level(LOG_LEVEL_HIGH, "get osd_ext[%d]->clone: %d\n", index, osd_ext_hw.clone[index]);
	*clone = osd_ext_hw.clone[index];
}

void osd_ext_set_clone_hw(u32 index, u32 clone)
{
	static const color_bit_define_t *color_info[HW_OSD_COUNT] = {};
	static pandata_t pandata[HW_OSD_COUNT] = {};

	printk("++ set osd_ext[%d]->clone: %d->%d\n", index, osd_ext_hw.clone[index], clone);
	osd_ext_hw.clone[index] = clone;

	if (osd_ext_hw.clone[index]) {
		if (osd_ext_hw.angle[index]) {
			osd_ext_hw.color_info[index] = osd_hw.color_info[index];
			osd_ext_clone_task_start();
		} else {
			color_info[index] = osd_ext_hw.color_info[index];
			osd_ext_hw.color_info[index] = osd_hw.color_info[index];
			memcpy(&pandata, &osd_ext_hw.pandata[index], sizeof(pandata_t));
			canvas_update_addr(osd_ext_hw.fb_gem[index].canvas_idx, osd_hw.fb_gem[index].addr);
		}
	} else {
		if (osd_ext_hw.angle[index]) {
			osd_ext_clone_task_stop();
		} else {
			color_info[index] = osd_ext_hw.color_info[index];
			canvas_update_addr(osd_ext_hw.fb_gem[index].canvas_idx, osd_ext_hw.fb_gem[index].addr);
			osd_ext_hw.color_info[index] = color_info[index];
			memcpy(&osd_ext_hw.pandata[index], &pandata, sizeof(pandata_t));
		}
	}
	add_to_update_list(index, OSD_COLOR_MODE);
}
