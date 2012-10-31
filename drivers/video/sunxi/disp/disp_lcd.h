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

#ifndef __DISP_LCD_H__
#define __DISP_LCD_H__

extern __panel_para_t gpanel_info[2];

__s32 Disp_lcdc_init(__u32 sel);
__s32 Disp_lcdc_exit(__u32 sel);

__s32 Disp_lcdc_pin_cfg(__u32 sel, __disp_output_type_t out_type, __u32 bon);
__u32 Disp_get_screen_scan_mode(__disp_tv_mode_t tv_mode);

__u32 tv_mode_to_width(__disp_tv_mode_t mode);
__u32 tv_mode_to_height(__disp_tv_mode_t mode);
__u32 vga_mode_to_width(__disp_vga_mode_t mode);
__u32 vga_mode_to_height(__disp_vga_mode_t mode);

void LCD_delay_ms(__u32 ms);
void LCD_delay_us(__u32 ns);

void TCON_open(__u32 sel);
void TCON_close(__u32 sel);
__s32 LCD_PWM_EN(__u32 sel, __bool b_en);
__s32 LCD_BL_EN(__u32 sel, __bool b_en);
__s32 LCD_POWER_EN(__u32 sel, __bool b_en);

__s32 LCD_GPIO_request(__u32 sel, __u32 io_index);
__s32 LCD_GPIO_release(__u32 sel, __u32 io_index);
__s32 LCD_GPIO_set_attr(__u32 sel, __u32 io_index, __bool b_output);
__s32 LCD_GPIO_read(__u32 sel, __u32 io_index);
__s32 LCD_GPIO_write(__u32 sel, __u32 io_index, __u32 data);

__s32 pwm_set_para(__u32 channel, __pwm_info_t *pwm_info);
__s32 pwm_get_para(__u32 channel, __pwm_info_t *pwm_info);

void LCD_set_panel_funs(__lcd_panel_fun_t *lcd0_cfg,
			__lcd_panel_fun_t *lcd1_cfg);

void LCD_OPEN_FUNC(__u32 sel, LCD_FUNC func, __u32 delay);
void LCD_CLOSE_FUNC(__u32 sel, LCD_FUNC func, __u32 delay);
__s32 LCD_POWER_EN(__u32 sel, __bool b_en);

#endif
