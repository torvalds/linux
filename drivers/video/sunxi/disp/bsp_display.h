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

#ifndef __EBSP_DISPLAY_H__
#define __EBSP_DISPLAY_H__

#include "linux/kernel.h"
#include "linux/mm.h"
#include <linux/uaccess.h>
#include <asm/memory.h>
#include <linux/unistd.h>
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h> /* wake_up_process() */
#include <linux/kthread.h> /* kthread_create(), kthread_run() */
#include <linux/err.h> /* IS_ERR(), PTR_ERR() */
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h"
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <plat/sys_config.h>
#include <mach/clock.h>
#include <mach/aw_ccu.h>
#include <mach/system.h>
#include <linux/types.h>

typedef unsigned int __hdle;

#include <video/sunxi_disp_ioctl.h>

#define __inf(msg, ...) pr_debug("[DISP] " msg, ##__VA_ARGS__)
#define __wrn(msg, ...) pr_warn("[DISP] " msg, ##__VA_ARGS__)

typedef struct {
	__u32 base_image0;
	__u32 base_image1;
	__u32 base_scaler0;
	__u32 base_scaler1;
	__u32 base_lcdc0;
	__u32 base_lcdc1;
	__u32 base_tvec0;
	__u32 base_tvec1;
	__u32 base_pioc;
	__u32 base_sdram;
	__u32 base_ccmu;
	__u32 base_pwm;
#ifdef CONFIG_ARCH_SUN5I
	__u32 base_iep;
#endif

	void (*tve_interrup) (__u32 sel);
	 __s32(*hdmi_set_mode) (__disp_tv_mode_t mode);
	 __s32(*hdmi_set_videomode) (const struct __disp_video_timing *mode);
	 __s32(*hdmi_wait_edid) (void);
	 __s32(*Hdmi_open) (void);
	 __s32(*Hdmi_close) (void);
	 __s32(*hdmi_mode_support) (__disp_tv_mode_t mode);
	 __s32(*hdmi_get_video_timing) (__disp_tv_mode_t mode,
				struct __disp_video_timing *video_timing);
	 __s32(*hdmi_get_HPD_status) (void);
	 __s32(*hdmi_set_pll) (__u32 pll, __u32 clk);
	 __s32(*disp_int_process) (__u32 sel);
} __disp_bsp_init_para;

extern __s32 BSP_disp_clk_on(__u32 type);
extern __s32 BSP_disp_clk_off(__u32 type);
extern __s32 BSP_disp_init(__disp_bsp_init_para *para);
extern __s32 BSP_disp_exit(__u32 mode);
extern __s32 BSP_disp_open(void);
extern __s32 BSP_disp_close(void);
extern __s32 BSP_disp_print_reg(__bool b_force_on, __u32 id);
extern __s32 BSP_disp_cmd_cache(__u32 sel);
extern __s32 BSP_disp_cmd_submit(__u32 sel);
extern __s32 BSP_disp_set_bk_color(__u32 sel, __disp_color_t *color);
extern __s32 BSP_disp_get_bk_color(__u32 sel, __disp_color_t *color);
extern __s32 BSP_disp_set_color_key(__u32 sel, __disp_colorkey_t *ck_mode);
extern __s32 BSP_disp_get_color_key(__u32 sel, __disp_colorkey_t *ck_mode);
extern __s32 BSP_disp_set_palette_table(__u32 sel, __u32 *pbuffer,
					__u32 offset, __u32 size);
extern __s32 BSP_disp_get_palette_table(__u32 sel, __u32 *pbuffer,
					__u32 offset, __u32 size);
extern __s32 BSP_disp_get_screen_height(__u32 sel);
extern __s32 BSP_disp_get_screen_width(__u32 sel);
extern __s32 BSP_disp_get_output_type(__u32 sel);
extern __s32 BSP_disp_gamma_correction_enable(__u32 sel);
extern __s32 BSP_disp_gamma_correction_disable(__u32 sel);
extern __s32 BSP_disp_set_bright(__u32 sel, __u32 bright);
extern __s32 BSP_disp_get_bright(__u32 sel);
extern __s32 BSP_disp_set_contrast(__u32 sel, __u32 contrast);
extern __s32 BSP_disp_get_contrast(__u32 sel);
extern __s32 BSP_disp_set_saturation(__u32 sel, __u32 saturation);
extern __s32 BSP_disp_get_saturation(__u32 sel);
extern __s32 BSP_disp_set_hue(__u32 sel, __u32 hue);
extern __s32 BSP_disp_get_hue(__u32 sel);
#ifdef CONFIG_ARCH_SUN4I
extern __s32 BSP_disp_enhance_enable(__u32 sel, __bool enable);
extern __s32 BSP_disp_get_enhance_enable(__u32 sel);
#endif
extern __s32 BSP_disp_capture_screen(__u32 sel,
				     __disp_capture_screen_para_t *para);
extern __s32 BSP_disp_set_screen_size(__u32 sel, __disp_rectsz_t *size);
#ifdef CONFIG_ARCH_SUN4I
extern __s32 BSP_disp_set_output_csc(__u32 sel, __disp_output_type_t type);
extern __s32 BSP_disp_de_flicker_enable(__u32 sel, __bool b_en);
#else
extern __s32 BSP_disp_set_output_csc(__u32 sel, __u32 out_type, __u32 drc_en);
#endif
extern __s32 BSP_disp_layer_request(__u32 sel, __disp_layer_work_mode_t mode);
extern __s32 BSP_disp_layer_release(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_open(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_close(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_framebuffer(__u32 sel, __u32 hid,
					    __disp_fb_t *fbinfo);
extern __s32 BSP_disp_layer_get_framebuffer(__u32 sel, __u32 hid,
					    __disp_fb_t *fbinfo);
extern __s32 BSP_disp_layer_set_src_window(__u32 sel, __u32 hid,
					   __disp_rect_t *regn);
extern __s32 BSP_disp_layer_get_src_window(__u32 sel, __u32 hid,
					   __disp_rect_t *regn);
extern __s32 BSP_disp_layer_set_screen_window(__u32 sel, __u32 hid,
					      __disp_rect_t *regn);
extern __s32 BSP_disp_layer_get_screen_window(__u32 sel, __u32 hid,
					      __disp_rect_t *regn);
extern __s32 BSP_disp_layer_set_para(__u32 sel, __u32 hid,
				     __disp_layer_info_t *layer_para);
extern __s32 BSP_disp_layer_get_para(__u32 sel, __u32 hid,
				     __disp_layer_info_t *layer_para);
extern __s32 BSP_disp_layer_set_top(__u32 sel, __u32 handle);
extern __s32 BSP_disp_layer_set_bottom(__u32 sel, __u32 handle);
extern __s32 BSP_disp_layer_set_alpha_value(__u32 sel, __u32 hid, __u8 value);
extern __s32 BSP_disp_layer_get_alpha_value(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_alpha_enable(__u32 sel, __u32 hid, __bool enable);
extern __s32 BSP_disp_layer_get_alpha_enable(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_pipe(__u32 sel, __u32 hid, __u8 pipe);
extern __s32 BSP_disp_layer_get_pipe(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_piro(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_colorkey_enable(__u32 sel, __u32 hid,
					    __bool enable);
extern __s32 BSP_disp_layer_get_colorkey_enable(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_smooth(__u32 sel, __u32 hid,
				       __disp_video_smooth_t mode);
extern __s32 BSP_disp_layer_get_smooth(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_bright(__u32 sel, __u32 hid, __u32 bright);
extern __s32 BSP_disp_layer_set_contrast(__u32 sel, __u32 hid, __u32 contrast);
extern __s32 BSP_disp_layer_set_saturation(__u32 sel, __u32 hid,
					   __u32 saturation);
extern __s32 BSP_disp_layer_set_hue(__u32 sel, __u32 hid, __u32 hue);
extern __s32 BSP_disp_layer_enhance_enable(__u32 sel, __u32 hid, __bool enable);
extern __s32 BSP_disp_layer_get_bright(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_contrast(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_saturation(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_hue(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_enhance_enable(__u32 sel, __u32 hid);

extern __s32 BSP_disp_layer_vpp_enable(__u32 sel, __u32 hid, __bool enable);
extern __s32 BSP_disp_layer_get_vpp_enable(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_luma_sharp_level(__u32 sel, __u32 hid,
						 __u32 level);
extern __s32 BSP_disp_layer_get_luma_sharp_level(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_chroma_sharp_level(__u32 sel, __u32 hid,
						   __u32 level);
extern __s32 BSP_disp_layer_get_chroma_sharp_level(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_white_exten_level(__u32 sel, __u32 hid,
						  __u32 level);
extern __s32 BSP_disp_layer_get_white_exten_level(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_black_exten_level(__u32 sel, __u32 hid,
						  __u32 level);
extern __s32 BSP_disp_layer_get_black_exten_level(__u32 sel, __u32 hid);

extern __s32 BSP_disp_scaler_get_smooth(__u32 sel);
extern __s32 BSP_disp_scaler_set_smooth(__u32 sel, __disp_video_smooth_t mode);
extern __s32 BSP_disp_scaler_request(void);
extern __s32 BSP_disp_scaler_release(__u32 handle);
extern __s32 BSP_disp_scaler_start(__u32 handle, __disp_scaler_para_t *scl);

extern __s32 BSP_disp_hwc_enable(__u32 sel, __bool enable);
extern __s32 BSP_disp_hwc_set_pos(__u32 sel, __disp_pos_t *pos);
extern __s32 BSP_disp_hwc_get_pos(__u32 sel, __disp_pos_t *pos);
extern __s32 BSP_disp_hwc_set_framebuffer(__u32 sel,
					  __disp_hwc_pattern_t *patmem);
extern __s32 BSP_disp_hwc_set_palette(__u32 sel, void *palette, __u32 offset,
				      __u32 palette_size);

extern __s32 BSP_disp_video_set_fb(__u32 sel, __u32 hid,
				   __disp_video_fb_t *in_addr);
extern __s32 BSP_disp_video_get_frame_id(__u32 sel, __u32 hid);
extern __s32 BSP_disp_video_get_dit_info(__u32 sel, __u32 hid,
					 __disp_dit_info_t *dit_info);
extern __s32 BSP_disp_video_start(__u32 sel, __u32 hid);
extern __s32 BSP_disp_video_stop(__u32 sel, __u32 hid);

extern __s32 BSP_disp_lcd_open_before(__u32 sel);
extern __s32 BSP_disp_lcd_open_after(__u32 sel);
extern __lcd_flow_t *BSP_disp_lcd_get_open_flow(__u32 sel);
extern __s32 BSP_disp_lcd_close_befor(__u32 sel);
extern __s32 BSP_disp_lcd_close_after(__u32 sel);
extern __lcd_flow_t *BSP_disp_lcd_get_close_flow(__u32 sel);
extern __s32 BSP_disp_lcd_xy_switch(__u32 sel, __s32 mode);
extern __s32 BSP_disp_set_gamma_table(__u32 sel, __u32 *gamtbl_addr,
				      __u32 gamtbl_size);
#ifdef CONFIG_ARCH_SUN4I
extern __s32 BSP_disp_lcd_set_bright(__u32 sel, __u32 bright);
#else
extern __s32 BSP_disp_lcd_set_bright(__u32 sel, __u32 bright, __u32 from_iep);
#endif
extern __s32 BSP_disp_lcd_get_bright(__u32 sel);
extern __s32 BSP_disp_lcd_set_src(__u32 sel, __disp_lcdc_src_t src);
extern __s32 LCD_PWM_EN(__u32 sel, __bool b_en);
extern __s32 LCD_BL_EN(__u32 sel, __bool b_en);
extern __s32 BSP_disp_lcd_user_defined_func(__u32 sel, __u32 para1, __u32 para2,
					    __u32 para3);
extern __s32 BSP_disp_get_videomode(__u32 sel, struct fb_videomode *videomode);
extern __u32 BSP_disp_get_cur_line(__u32 sel);
#ifdef CONFIG_ARCH_SUN5I
extern __s32 BSP_disp_close_lcd_backlight(__u32 sel);
#endif

extern __s32 BSP_disp_tv_open(__u32 sel);
extern __s32 BSP_disp_tv_close(__u32 sel);
extern __s32 BSP_disp_tv_set_mode(__u32 sel, __disp_tv_mode_t tv_mod);
extern __s32 BSP_disp_tv_get_mode(__u32 sel);
extern __s32 BSP_disp_tv_get_interface(__u32 sel);
extern __s32 BSP_disp_tv_auto_check_enable(__u32 sel);
extern __s32 BSP_disp_tv_auto_check_disable(__u32 sel);
extern __s32 BSP_disp_tv_set_src(__u32 sel, __disp_lcdc_src_t src);
extern __s32 BSP_disp_tv_get_dac_status(__u32 sel, __u32 index);
extern __s32 BSP_disp_tv_set_dac_source(__u32 sel, __u32 index,
					__disp_tv_dac_source source);
extern __s32 BSP_disp_tv_get_dac_source(__u32 sel, __u32 index);

extern __s32 BSP_disp_hdmi_open(__u32 sel, __u32 wait_edid);
extern __s32 BSP_disp_hdmi_close(__u32 sel);
extern __s32 BSP_disp_hdmi_set_mode(__u32 sel, __disp_tv_mode_t mode);
extern __s32 BSP_disp_set_videomode(__u32 sel,
		const struct fb_videomode *mode);
extern __s32 BSP_disp_hdmi_get_mode(__u32 sel);
extern __s32 BSP_disp_hdmi_check_support_mode(__u32 sel, __u8 mode);
extern __s32 BSP_disp_hdmi_get_hpd_status(__u32 sel);
extern __s32 BSP_disp_hdmi_set_src(__u32 sel, __disp_lcdc_src_t src);
extern __s32 BSP_disp_set_hdmi_func(__disp_hdmi_func *func);

extern __s32 BSP_disp_vga_open(__u32 sel);
extern __s32 BSP_disp_vga_close(__u32 sel);
extern __s32 BSP_disp_vga_set_mode(__u32 sel, __disp_vga_mode_t mode);
extern __s32 BSP_disp_vga_get_mode(__u32 sel);
extern __s32 BSP_disp_vga_set_src(__u32 sel, __disp_lcdc_src_t src);

extern __s32 BSP_disp_sprite_init(__u32 sel);
extern __s32 BSP_disp_sprite_exit(__u32 sel);
extern __s32 BSP_disp_sprite_open(__u32 sel);
extern __s32 BSP_disp_sprite_close(__u32 sel);
extern __s32 BSP_disp_sprite_alpha_enable(__u32 sel);
extern __s32 BSP_disp_sprite_alpha_disable(__u32 sel);
extern __s32 BSP_disp_sprite_get_alpha_enable(__u32 sel);
extern __s32 BSP_disp_sprite_set_alpha_vale(__u32 sel, __u32 alpha);
extern __s32 BSP_disp_sprite_get_alpha_value(__u32 sel);
extern __s32 BSP_disp_sprite_set_format(__u32 sel, __disp_pixel_fmt_t format,
					__disp_pixel_seq_t pixel_seq);
extern __s32 BSP_disp_sprite_set_palette_table(__u32 sel, __u32 *buffer,
					       __u32 offset, __u32 size);
extern __s32 BSP_disp_sprite_set_order(__u32 sel, __s32 hid, __s32 dst_hid);
extern __s32 BSP_disp_sprite_get_top_block(__u32 sel);
extern __s32 BSP_disp_sprite_get_bottom_block(__u32 sel);
extern __s32 BSP_disp_sprite_get_block_number(__u32 sel);
extern __s32 BSP_disp_sprite_block_request(__u32 sel,
					   __disp_sprite_block_para_t *para);
extern __s32 BSP_disp_sprite_block_release(__u32 sel, __s32 hid);
extern __s32 BSP_disp_sprite_block_set_screen_win(__u32 sel, __s32 hid,
						  __disp_rect_t *scn_win);
extern __s32 BSP_disp_sprite_block_get_srceen_win(__u32 sel, __s32 hid,
						  __disp_rect_t *scn_win);
extern __s32 BSP_disp_sprite_block_set_src_win(__u32 sel, __s32 hid,
					       __disp_rect_t *scn_win);
extern __s32 BSP_disp_sprite_block_get_src_win(__u32 sel, __s32 hid,
					       __disp_rect_t *scn_win);
extern __s32 BSP_disp_sprite_block_set_framebuffer(__u32 sel, __s32 hid,
						   __disp_fb_t *fb);
extern __s32 BSP_disp_sprite_block_get_framebufer(__u32 sel, __s32 hid,
						  __disp_fb_t *fb);
extern __s32 BSP_disp_sprite_block_set_top(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_set_bottom(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_get_pre_block(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_get_next_block(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_get_prio(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_open(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_close(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_set_para(__u32 sel, __u32 hid,
					    __disp_sprite_block_para_t *para);
extern __s32 BSP_disp_sprite_block_get_para(__u32 sel, __u32 hid,
					    __disp_sprite_block_para_t *para);

#ifdef CONFIG_ARCH_SUN5I
extern __s32 BSP_disp_iep_deflicker_enable(__u32 sel, __bool en);
extern __s32 BSP_disp_iep_get_deflicker_enable(__u32 sel);
extern __s32 BSP_disp_iep_drc_enable(__u32 sel, __bool en);
extern __s32 BSP_disp_iep_get_drc_enable(__u32 sel);
extern __s32 BSP_disp_iep_set_demo_win(__u32 sel, __u32 mode,
				       __disp_rect_t *regn);
#endif

__s32 Display_set_fb_timing(__u32 sel);

/* symbol exists in dev_disp.c */
int sunxi_is_version_A(void);

#endif
