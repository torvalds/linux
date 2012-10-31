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
#include "disp_display_i.h"
#include "disp_lcd.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_de.h"
#include "disp_clk.h"
#include "OSAL_Pin.h"

static __lcd_flow_t open_flow[2];
static __lcd_flow_t close_flow[2];
__panel_para_t gpanel_info[2];
static __lcd_panel_fun_t lcd_panel_fun[2];

static void
LCD_get_reg_bases(__reg_bases_t *para)
{
	para->base_lcdc0 = gdisp.init_para.base_lcdc0;
	para->base_lcdc1 = gdisp.init_para.base_lcdc1;
	para->base_pioc = gdisp.init_para.base_pioc;
	para->base_ccmu = gdisp.init_para.base_ccmu;
	para->base_pwm = gdisp.init_para.base_pwm;
}
EXPORT_SYMBOL(LCD_get_reg_bases);

static void
Lcd_Panel_Parameter_Check(__u32 sel)
{
	__panel_para_t *info;
	__u32 cycle_num = 1;
	__u32 Lcd_Panel_Err_Flag = 0;
	__u32 Lcd_Panel_Wrn_Flag = 0;
	__u32 Disp_Driver_Bug_Flag = 0;

	__u32 lcd_fclk_frq;
	__u32 lcd_clk_div;

	info = &(gpanel_info[sel]);

	if (info->lcd_if == 0 && info->lcd_hv_if == 1 &&
	    info->lcd_hv_smode == 0)
		cycle_num = 3;
	else if (info->lcd_if == 0 && info->lcd_hv_if == 1 &&
		 info->lcd_hv_smode == 1)
		cycle_num = 2;
	else if (info->lcd_if == 1 && info->lcd_cpu_if == 1)
		cycle_num = 3;
	else if (info->lcd_if == 1 && info->lcd_cpu_if == 2)
		cycle_num = 2;
	else if (info->lcd_if == 1 && info->lcd_cpu_if == 3)
		cycle_num = 2;
	else if (info->lcd_if == 1 && info->lcd_cpu_if == 5)
		cycle_num = 2;
	else if (info->lcd_if == 1 && info->lcd_cpu_if == 6)
		cycle_num = 3;
	else if (info->lcd_if == 1 && info->lcd_cpu_if == 7)
		cycle_num = 2;
	else
		cycle_num = 1;

	if (info->lcd_hbp > info->lcd_hv_hspw)
		;
	else
		Lcd_Panel_Err_Flag |= BIT0;

	if (info->lcd_vbp > info->lcd_hv_vspw)
		;
	else
		Lcd_Panel_Err_Flag |= BIT1;

	if (info->lcd_ht >= (info->lcd_hbp + info->lcd_x * cycle_num + 4))
		;
	else
		Lcd_Panel_Err_Flag |= BIT2;

	if ((info->lcd_vt / 2) >= (info->lcd_vbp + info->lcd_y + 2))
		;
	else
		Lcd_Panel_Err_Flag |= BIT3;

	lcd_clk_div = TCON0_get_dclk_div(sel);
	if (lcd_clk_div >= 6) {
		;
	} else if ((lcd_clk_div == 5) || (lcd_clk_div == 4) ||
		   (lcd_clk_div == 2)) {
		if ((info->lcd_io_cfg0 != 0x00000000) &&
		    (info->lcd_io_cfg0 != 0x04000000))
			Lcd_Panel_Err_Flag |= BIT10;
	} else
		Disp_Driver_Bug_Flag |= 1;

	if ((info->lcd_if == 1 && info->lcd_cpu_if == 0) ||
	    (info->lcd_if == 3 && info->lcd_lvds_bitwidth == 1)) {
		if (info->lcd_frm != 1)
			Lcd_Panel_Wrn_Flag |= BIT0;
	} else if (info->lcd_if == 1 && info->lcd_cpu_if == 4) {
		if (info->lcd_frm != 2)
			Lcd_Panel_Wrn_Flag |= BIT1;
	}

	lcd_fclk_frq = (info->lcd_dclk_freq * 1000 * 1000) /
		((info->lcd_vt / 2) * info->lcd_ht);
	if (lcd_fclk_frq < 50 || lcd_fclk_frq > 70)
		Lcd_Panel_Wrn_Flag |= BIT2;

	if (Lcd_Panel_Err_Flag != 0 || Lcd_Panel_Wrn_Flag != 0) {
		if (Lcd_Panel_Err_Flag != 0) {
			__u32 i;

			for (i = 0; i < 200; i++)
				DE_WRN("*** Lcd in danger...\n");
		}

		DE_WRN("*******************************************************"
		       "**********\n");
		DE_WRN("***\n");
		DE_WRN("*** LCD Panel Parameter Check\n");
		DE_WRN("***\n");
		DE_WRN("***             by dulianping\n");
		DE_WRN("***\n");
		DE_WRN("*******************************************************"
		       "**********\n");

		DE_WRN("***\n");
		DE_WRN("*** Interface:");
		if (info->lcd_if == 0 && info->lcd_hv_if == 0) {
			DE_WRN("*** Parallel HV Panel\n");
		} else if (info->lcd_if == 0 && info->lcd_hv_if == 1) {
			DE_WRN("*** Serial HV Panel\n");
		} else if (info->lcd_if == 0 && info->lcd_hv_if == 2) {
			DE_WRN("*** Serial YUV Panel\n");
		} else if (info->lcd_if == 3 && info->lcd_lvds_bitwidth == 0) {
			DE_WRN("*** 24Bit LVDS Panel\n");
		} else if (info->lcd_if == 3 && info->lcd_lvds_bitwidth == 1) {
			DE_WRN("*** 18Bit LVDS Panel\n");
		} else if (info->lcd_if == 1 && info->lcd_cpu_if == 0) {
			DE_WRN("*** 18Bit CPU Panel\n");
		} else if (info->lcd_if == 1 && info->lcd_cpu_if == 4) {
			DE_WRN("*** 16Bit CPU Panel\n");
		} else {
			DE_WRN("\n");
			DE_WRN("*** lcd_if:     %d\n", info->lcd_if);
			DE_WRN("*** lcd_hv_if:  %d\n", info->lcd_hv_if);
			DE_WRN("*** lcd_cpu_if: %d\n", info->lcd_cpu_if);
		}

		if (info->lcd_frm == 0)
			DE_WRN("*** Lcd Frm Disable\n");
		else if (info->lcd_frm == 1)
			DE_WRN("*** Lcd Frm to RGB666\n");
		else if (info->lcd_frm == 2)
			DE_WRN("*** Lcd Frm to RGB565\n");

		DE_WRN("***\n");
		DE_WRN("*** Timing:\n");
		DE_WRN("*** lcd_x:      %d\n", info->lcd_x);
		DE_WRN("*** lcd_y:      %d\n", info->lcd_y);
		DE_WRN("*** lcd_ht:     %d\n", info->lcd_ht);
		DE_WRN("*** lcd_hbp:    %d\n", info->lcd_hbp);
		DE_WRN("*** lcd_vt:     %d\n", info->lcd_vt);
		DE_WRN("*** lcd_vbp:    %d\n", info->lcd_vbp);
		DE_WRN("*** lcd_hspw:   %d\n", info->lcd_hv_hspw);
		DE_WRN("*** lcd_vspw:   %d\n", info->lcd_hv_vspw);
		DE_WRN("*** lcd_frame_frq:  %dHz\n", lcd_fclk_frq);

		/* Print Error */
		DE_WRN("***\n");
		if (Lcd_Panel_Err_Flag & BIT0)
			DE_WRN("*** Err01: Violate \"lcd_hbp > lcd_hspw\"\n");

		if (Lcd_Panel_Err_Flag & BIT1)
			DE_WRN("*** Err02: Violate \"lcd_vbp > lcd_vspw\"\n");

		if (Lcd_Panel_Err_Flag & BIT2)
			DE_WRN("*** Err03: Violate \"lcd_ht >= "
			       "(lcd_hbp+lcd_x*%d+4)\"\n", cycle_num);

		if (Lcd_Panel_Err_Flag & BIT3)
			DE_WRN("*** Err04: Violate \"(lcd_vt/2) >= "
			       "(lcd_vbp+lcd_y+2)\"\n");

		if (Lcd_Panel_Err_Flag & BIT10)
			DE_WRN("*** Err10: Violate \"lcd_io_cfg0\", "
			       "use \"0x00000000\" or \"0x04000000\"");

		if (Lcd_Panel_Wrn_Flag & BIT0)
			DE_WRN("*** WRN01: Recommend \"lcd_frm = 1\"\n");

		if (Lcd_Panel_Wrn_Flag & BIT1)
			DE_WRN("*** WRN02: Recommend \"lcd_frm = 2\"\n");

		if (Lcd_Panel_Wrn_Flag & BIT2)
			DE_WRN("*** WRN03: Recommend \"lcd_dclk_frq = %d\"\n",
			       ((info->lcd_vt / 2) * info->lcd_ht) * 60 /
			       (1000 * 1000));
		DE_WRN("***\n");

		if (Lcd_Panel_Err_Flag != 0) {
			__u32 image_base_addr;
			__u32 reg_value = 0;

			image_base_addr = DE_Get_Reg_Base(sel);

			/* set background color */
			sys_put_wvalue(image_base_addr + 0x804, 0xffff00ff);

			reg_value = sys_get_wvalue(image_base_addr + 0x800);
			/* close all layer */
			sys_put_wvalue(image_base_addr + 0x800,
				       reg_value & 0xfffff0ff);

			LCD_delay_ms(2000);
			/* set background color */
			sys_put_wvalue(image_base_addr + 0x804, 0x00000000);
			/* open layer */
			sys_put_wvalue(image_base_addr + 0x800, reg_value);

			DE_WRN("*** Try new parameters, you can make it "
			       "pass!\n");
		}
		DE_WRN("*** LCD Panel Parameter Check End\n");
		DE_WRN("*******************************************************"
		       "**********\n");
	}
}

static __s32
LCD_get_panel_para(__u32 sel, __panel_para_t *info)
{
	__s32 ret = 0;
	char primary_key[20];
	__s32 value = 0;
	__u32 i = 0;

	sprintf(primary_key, "lcd%d_para", sel);

	memset(info, 0, sizeof(__panel_para_t));

	ret = script_parser_fetch(primary_key, "lcd_x", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_x fail\n", primary_key);
	} else {
		info->lcd_x = value;
		DE_INF("lcd_x = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_y", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_y fail\n", primary_key);
	} else {
		info->lcd_y = value;
		DE_INF("lcd_y = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_dclk_freq", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_dclk_freq fail\n",
		       primary_key);
	} else {
		info->lcd_dclk_freq = value;
		DE_INF("lcd_dclk_freq = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_pwm_not_used", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_pwm_not_used fail\n",
		       primary_key);
	} else {
		info->lcd_pwm_not_used = value;
		DE_INF("lcd_pwm_not_used = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_pwm_ch", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_pwm_ch fail\n", primary_key);
	} else {
		info->lcd_pwm_ch = value;
		DE_INF("lcd_pwm_ch = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_pwm_freq", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_pwm_freq fail\n", primary_key);
	} else {
		info->lcd_pwm_freq = value;
		DE_INF("lcd_pwm_freq = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_pwm_pol", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_pwm_pol fail\n", primary_key);
	} else {
		info->lcd_pwm_pol = value;
		DE_INF("lcd_pwm_pol = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_if", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_if fail\n", primary_key);
	} else {
		info->lcd_if = value;
		DE_INF("lcd_if = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hbp", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_hbp fail\n", primary_key);
	} else {
		info->lcd_hbp = value;
		DE_INF("lcd_hbp = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_ht", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_ht fail\n", primary_key);
	} else {
		info->lcd_ht = value;
		DE_INF("lcd_ht = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_vbp", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_vbp fail\n", primary_key);
	} else {
		info->lcd_vbp = value;
		DE_INF("lcd_vbp = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_vt", &value, 1);
	if (ret < 0) {
		DE_WRN("fetch script data %s.lcd_vt fail\n", primary_key);
	} else {
		info->lcd_vt = value;
		DE_INF("lcd_vt = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hv_if", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_hv_if fail\n", primary_key);
	} else {
		info->lcd_hv_if = value;
		DE_INF("lcd_hv_if = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hv_smode", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_hv_smode fail\n", primary_key);
	} else {
		info->lcd_hv_smode = value;
		DE_INF("lcd_hv_smode = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hv_s888_if", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_hv_s888_if fail\n",
		       primary_key);
	} else {
		info->lcd_hv_s888_if = value;
		DE_INF("lcd_hv_s888_if = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hv_syuv_if", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_hv_syuv_if fail\n",
		       primary_key);
	} else {
		info->lcd_hv_syuv_if = value;
		DE_INF("lcd_hv_syuv_if = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hv_vspw", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_hv_vspw fail\n", primary_key);
	} else {
		info->lcd_hv_vspw = value;
		DE_INF("lcd_hv_vspw = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_hv_hspw", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_hv_hspw fail\n", primary_key);
	} else {
		info->lcd_hv_hspw = value;
		DE_INF("lcd_hv_hspw = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_lvds_ch", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_lvds_ch fail\n", primary_key);
	} else {
		info->lcd_lvds_ch = value;
		DE_INF("lcd_lvds_ch = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_lvds_mode", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_lvds_mode fail\n",
		       primary_key);
	} else {
		info->lcd_lvds_mode = value;
		DE_INF("lcd_lvds_mode = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_lvds_bitwidth", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_lvds_bitwidth fail\n",
		       primary_key);
	} else {
		info->lcd_lvds_bitwidth = value;
		DE_INF("lcd_lvds_bitwidth = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_lvds_io_cross", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_lvds_io_cross fail\n",
		       primary_key);
	} else {
		info->lcd_lvds_io_cross = value;
		DE_INF("lcd_lvds_io_cross = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_cpu_if", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_cpu_if fail\n", primary_key);
	} else {
		info->lcd_cpu_if = value;
		DE_INF("lcd_cpu_if = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_frm", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_frm fail\n", primary_key);
	} else {
		info->lcd_frm = value;
		DE_INF("lcd_frm = %d\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_io_cfg0", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_io_cfg0 fail\n", primary_key);
	} else {
		info->lcd_io_cfg0 = value;
		DE_INF("lcd_io_cfg0 = 0x%08x\n", value);
	}

	ret = script_parser_fetch(primary_key, "lcd_gamma_correction_en",
				  &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data %s.lcd_gamma_correction_en fail\n",
		       primary_key);
	} else {
		info->lcd_gamma_correction_en = value;
		DE_INF("lcd_gamma_correction_en = %d\n", value);
	}

	if (info->lcd_gamma_correction_en) {
		for (i = 0; i < 256; i++) {
			char name[20];

			sprintf(name, "lcd_gamma_tbl_%d", i);

			ret = script_parser_fetch(primary_key, name, &value, 1);
			if (ret < 0) {
				info->lcd_gamma_tbl[i] =
				    (i << 16) | (i << 8) | i;
				DE_INF("fetch script data %s.%s fail\n",
				       primary_key, name);
			} else {
				info->lcd_gamma_tbl[i] = value;
				DE_INF("%s = 0x%x\n", name, value);
			}
		}
	}
	return 0;
}

static void
LCD_get_sys_config(__u32 sel, __disp_lcd_cfg_t *lcd_cfg)
{
	char io_name[28][20] = {
		"lcdd0", "lcdd1", "lcdd2", "lcdd3", "lcdd4", "lcdd5",
		"lcdd6", "lcdd7", "lcdd8", "lcdd9", "lcdd10", "lcdd11",
		"lcdd12", "lcdd13", "lcdd14", "lcdd15", "lcdd16", "lcdd17",
		"lcdd18", "lcdd19", "lcdd20", "lcdd21", "lcdd22", "lcdd23",
		"lcdclk", "lcdde", "lcdhsync", "lcdvsync"};
	user_gpio_set_t *gpio_info;
	int value = 1;
	char primary_key[20], sub_name[20];
	int i = 0;
	int ret;

	sprintf(primary_key, "lcd%d_para", sel);

	/* lcd_used */
	ret = script_parser_fetch(primary_key, "lcd_used", &value, 1);
	if (ret < 0) {
		DE_WRN("%s.lcd_used not exit\n", primary_key);
		lcd_cfg->lcd_used = 0;
	} else {
		DE_INF("%s.lcd_used = %d\n", primary_key, value);
		lcd_cfg->lcd_used = value;
	}

	/* lcd_bl_en */
	lcd_cfg->lcd_bl_en_used = 0;
	value = 1;
	ret = script_parser_fetch(primary_key, "lcd_bl_en_used", &value, 1);
	if (value == 0) {
		DE_INF("%s.lcd_bl_en is not used\n", primary_key);
	} else {
		gpio_info = &(lcd_cfg->lcd_bl_en);
		ret = script_parser_fetch(primary_key, "lcd_bl_en",
					  (int *)gpio_info,
					  sizeof(user_gpio_set_t) /
					  sizeof(int));
		if (ret < 0) {
			DE_INF("%s.lcd_bl_en not exist\n", primary_key);
		} else {
			DE_INF("%s.lcd_bl_en gpio_port=%d,gpio_port_num:%d, "
			       "data:%d\n", primary_key, gpio_info->port,
			       gpio_info->port_num, gpio_info->data);
			lcd_cfg->lcd_bl_en_used = 1;
		}
	}

	/* lcd_power */
	lcd_cfg->lcd_power_used = 0;
	value = 1;
	ret = script_parser_fetch(primary_key, "lcd_power_used", &value, 1);
	if (value == 0) {
		DE_INF("%s.lcd_power is not used\n", primary_key);
	} else {
		gpio_info = &(lcd_cfg->lcd_power);
		ret = script_parser_fetch(primary_key, "lcd_power",
					  (int *)gpio_info,
					  sizeof(user_gpio_set_t) /
					  sizeof(int));
		if (ret < 0) {
			DE_INF("%s.lcd_power not exist\n", primary_key);
		} else {
			DE_INF("%s.lcd_power gpio_port=%d,gpio_port_num:%d, "
			       "data:%d\n", primary_key, gpio_info->port,
			       gpio_info->port_num, gpio_info->data);
			lcd_cfg->lcd_power_used = 1;
		}
	}

	/* lcd_pwm */
	lcd_cfg->lcd_pwm_used = 0;
	value = 1;
	ret = script_parser_fetch(primary_key, "lcd_pwm_used", &value, 1);
	if (value == 0) {
		DE_INF("%s.lcd_pwm is not used\n", primary_key);
	} else {
		gpio_info = &(lcd_cfg->lcd_pwm);
		ret = script_parser_fetch(primary_key, "lcd_pwm",
					  (int *)gpio_info,
					  sizeof(user_gpio_set_t) /
					  sizeof(int));
		if (ret < 0) {
			DE_INF("%s.lcd_pwm not exist\n", primary_key);
		} else {
			DE_INF("%s.lcd_pwm gpio_port=%d,gpio_port_num:%d, "
			       "data:%d\n", primary_key, gpio_info->port,
			       gpio_info->port_num, gpio_info->data);
			lcd_cfg->lcd_pwm_used = 1;
		}
	}

	/* lcd_gpio */
	for (i = 0; i < 4; i++) {
		sprintf(sub_name, "lcd_gpio_%d", i);

		gpio_info = &(lcd_cfg->lcd_gpio[i]);
		ret = script_parser_fetch(primary_key, sub_name,
					  (int *)gpio_info,
					  sizeof(user_gpio_set_t) /
					  sizeof(int));
		if (ret < 0) {
			DE_INF("%s.%s not exist\n", primary_key, sub_name);
			lcd_cfg->lcd_gpio_used[i] = 0;
		} else {
			DE_INF("%s.%s gpio_port=%d,gpio_port_num:%d, "
			       "mul_sel:%d\n", primary_key, sub_name,
			       gpio_info->port, gpio_info->port_num,
			       gpio_info->mul_sel);
			lcd_cfg->lcd_gpio_used[i] = 1;
		}
	}

	/* lcd io */
	for (i = 0; i < 28; i++) {
		gpio_info = &(lcd_cfg->lcd_io[i]);
		ret = script_parser_fetch(primary_key, io_name[i],
					  (int *)gpio_info,
					  sizeof(user_gpio_set_t) /
					  sizeof(int));
		if (ret < 0) {
			DE_INF("%s.%s not exist\n", primary_key, io_name[i]);
			lcd_cfg->lcd_io_used[i] = 0;
		} else {
			DE_INF("%s.%s gpio_port=%d,gpio_port_num:%d, "
			       "mul_sel:%d\n", primary_key, io_name[i],
			       gpio_info->port, gpio_info->port_num,
			       gpio_info->mul_sel);
			lcd_cfg->lcd_io_used[i] = 1;
		}
	}

	/* init_brightness */
	sprintf(primary_key, "disp_init");
	sprintf(sub_name, "lcd%d_bright", sel);

	ret = script_parser_fetch(primary_key, sub_name, &value, 1);
	if (ret < 0) {
		DE_INF("%s.%s not exit\n", primary_key, sub_name);
		lcd_cfg->init_bright = 192;
	} else {
		DE_INF("%s.%s = %d\n", primary_key, sub_name, value);
		if (value > 256)
			value = 256;

		lcd_cfg->init_bright = value;
	}
}

void LCD_delay_ms(__u32 ms)
{
	__u32 timeout = ms * HZ / 1000;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(timeout);
}
EXPORT_SYMBOL(LCD_delay_ms);

void LCD_delay_us(__u32 us)
{
	udelay(us);
}
EXPORT_SYMBOL(LCD_delay_us);

void
LCD_OPEN_FUNC(__u32 sel, LCD_FUNC func, __u32 delay)
{
	open_flow[sel].func[open_flow[sel].func_num].func = func;
	open_flow[sel].func[open_flow[sel].func_num].delay = delay;
	open_flow[sel].func_num++;
}
EXPORT_SYMBOL(LCD_OPEN_FUNC);

void
LCD_CLOSE_FUNC(__u32 sel, LCD_FUNC func, __u32 delay)
{
	close_flow[sel].func[close_flow[sel].func_num].func = func;
	close_flow[sel].func[close_flow[sel].func_num].delay = delay;
	close_flow[sel].func_num++;
}
EXPORT_SYMBOL(LCD_CLOSE_FUNC);

void
TCON_open(__u32 sel)
{
	if (gpanel_info[sel].tcon_index == 0) {
		TCON0_open(sel);
		gdisp.screen[sel].lcdc_status |= LCDC_TCON0_USED;
	} else {
		TCON1_open(sel);
		gdisp.screen[sel].lcdc_status |= LCDC_TCON1_USED;
	}

	if (gpanel_info[sel].lcd_if == 3)
		LCD_LVDS_open(sel);
}
EXPORT_SYMBOL(TCON_open);

void
TCON_close(__u32 sel)
{
	if (gpanel_info[sel].lcd_if == 3)
		LCD_LVDS_close(sel);

	if (gpanel_info[sel].tcon_index == 0) {
		TCON0_close(sel);
		gdisp.screen[sel].lcdc_status &= ~LCDC_TCON0_USED;
	} else {
		TCON1_close(sel);
		gdisp.screen[sel].lcdc_status &= ~LCDC_TCON1_USED;
	}
}
EXPORT_SYMBOL(TCON_close);

static __u32 pwm_read_reg(__u32 offset)
{
	__u32 value = 0;

	value = sys_get_wvalue(gdisp.init_para.base_pwm + offset);

	return value;
}

static __s32 pwm_write_reg(__u32 offset, __u32 value)
{
	sys_put_wvalue(gdisp.init_para.base_pwm + offset, value);

#ifdef CONFIG_ARCH_SUN4I
	LCD_delay_ms(20);
#endif

	return 0;
}

static __s32
pwm_enable(__u32 channel, __bool b_en)
{
	__u32 tmp = 0;
	__hdle hdl;

	if (gdisp.screen[channel].lcd_cfg.lcd_pwm_used) {
		user_gpio_set_t gpio_info[1];

		memcpy(gpio_info, &(gdisp.screen[channel].lcd_cfg.lcd_pwm),
		       sizeof(user_gpio_set_t));

		if (b_en)
			gpio_info->mul_sel = 2;
		else
			gpio_info->mul_sel = 0;

		hdl = OSAL_GPIO_Request(gpio_info, 1);
		OSAL_GPIO_Release(hdl, 2);
	}

	if (channel == 0) {
		tmp = pwm_read_reg(0x200);
		if (b_en)
			tmp |= (1 << 4);
		else
			tmp &= (~(1 << 4));

		pwm_write_reg(0x200, tmp);
	} else {
		tmp = pwm_read_reg(0x200);
		if (b_en)
			tmp |= (1 << 19);
		else
			tmp &= (~(1 << 19));

		pwm_write_reg(0x200, tmp);
	}

	gdisp.pwm[channel].enable = b_en;

	return 0;
}
EXPORT_SYMBOL(pwm_enable);

#ifdef CONFIG_ARCH_SUN4I
/*
 * channel: pwm channel,0/1
 * pwm_info->freq:  pwm freq, in hz
 * pwm_info->active_state: 0:low level; 1:high level
 */
__s32 pwm_set_para(__u32 channel, __pwm_info_t *pwm_info)
{
	__u32 pre_scal[10] = { 120, 180, 240, 360, 480,
			       12000, 24000, 36000, 48000, 72000 };
	__u32 pre_scal_id = 0, entire_cycle = 256, active_cycle = 192;
	__u32 i = 0, tmp = 0;
	__u32 freq;

	freq = 1000000 / pwm_info->period_ns;

	if (freq > 200000) {
		DE_WRN("pwm preq is large then 200khz, fix to 200khz\n");
		freq = 200000;
	}

	if (freq > 781) {
		pre_scal_id = 0;
		entire_cycle =
		    (24000000 / pre_scal[pre_scal_id] + (freq / 2)) / freq;
		DE_INF("pre_scal:%d, entire_cycle:%d, pwm_freq:%d\n",
		       pre_scal[i], entire_cycle,
		       24000000 / pre_scal[pre_scal_id] / entire_cycle);
	} else {
		for (i = 0; i < 10; i++) {
			__u32 pwm_freq = 0;

			pwm_freq = 24000000 / (pre_scal[i] * 256);
			if (abs(pwm_freq - freq) < abs(tmp - freq)) {
				tmp = pwm_freq;
				pre_scal_id = i;
				entire_cycle = 256;
				DE_INF("pre_scal:%d, entire_cycle:%d, "
				       "pwm_freq:%d\n", pre_scal[i], 256,
				       pwm_freq);
				DE_INF("----%d\n", tmp);
			}
		}
	}
	active_cycle = (pwm_info->duty_ns * entire_cycle +
			(pwm_info->period_ns / 2)) / pwm_info->period_ns;

	gdisp.pwm[channel].enable = pwm_info->enable;
	gdisp.pwm[channel].freq = freq;
	gdisp.pwm[channel].pre_scal = pre_scal[pre_scal_id];
	gdisp.pwm[channel].active_state = pwm_info->active_state;
	gdisp.pwm[channel].duty_ns = pwm_info->duty_ns;
	gdisp.pwm[channel].period_ns = pwm_info->period_ns;
	gdisp.pwm[channel].entire_cycle = entire_cycle;
	gdisp.pwm[channel].active_cycle = active_cycle;

	if (pre_scal_id >= 5)
		pre_scal_id += 3;

	if (channel == 0) {
		pwm_write_reg(0x204, ((entire_cycle - 1) << 16) | active_cycle);

		tmp = pwm_read_reg(0x200) & 0xffffff00;
		/*
		 * bit6: gating the special clock for pwm0
		 * bit5: pwm0: active state is high level
		 */
		tmp |= (1 << 6) | (pwm_info->active_state << 5) | pre_scal_id;
		pwm_write_reg(0x200, tmp);
	} else {
		pwm_write_reg(0x208, ((entire_cycle - 1) << 16) | active_cycle);

		tmp = pwm_read_reg(0x200) & 0xff807fff;
		/*
		 * bit21: gating the special clock for pwm1
		 * bit20: pwm1:  active state is high level
		 */
		tmp |= (1 << 21) | (pwm_info->active_state << 20) |
			(pre_scal_id << 15);
		pwm_write_reg(0x200, tmp);
	}

	pwm_enable(channel, pwm_info->enable);

	return 0;
}
#else
/*
 * channel: pwm channel,0/1
 * pwm_info->freq:  pwm freq, in hz
 * pwm_info->active_state: 0:low level; 1:high level
 */
__s32 pwm_set_para(__u32 channel, __pwm_info_t *pwm_info)
{
	__u32 pre_scal[11][2] = {
		{1, 0xf},
		{120, 0}, {180, 1}, {240, 2}, {360, 3}, {480, 4},
		{12000, 8}, {24000, 9}, {36000, 0xa},
		{48000, 0xb}, {72000, 0xc}
	};
	__u32 pre_scal_id = 0, entire_cycle = 16, active_cycle = 12;
	__u32 i = 0, j = 0, tmp = 0;
	__u32 freq;

	freq = 1000000 / pwm_info->period_ns;

	if (freq > 366) {
		pre_scal_id = 0;
		entire_cycle = 24000000 / freq;
	} else {
		for (i = 1; i < 11; i++) {
			for (j = 16;; j += 16) {
				__u32 pwm_freq = 0;

				pwm_freq = 24000000 / (pre_scal[i][0] * j);
				if (abs(pwm_freq - freq) < abs(tmp - freq)) {
					tmp = pwm_freq;
					pre_scal_id = i;
					entire_cycle = j;
					DE_INF("pre_scal:%d, entire_cycle:%d, "
					       "pwm_freq:%d\n",
					       pre_scal[i][0], j, pwm_freq);
					DE_INF("----%d\n", tmp);
				} else if ((tmp < freq) && (pwm_freq < tmp)) {
					break;
				}
			}
		}
	}

	active_cycle = (pwm_info->duty_ns * entire_cycle +
			(pwm_info->period_ns / 2)) / pwm_info->period_ns;

	gdisp.pwm[channel].enable = pwm_info->enable;
	gdisp.pwm[channel].freq = freq;
	gdisp.pwm[channel].pre_scal = pre_scal[pre_scal_id][0];
	gdisp.pwm[channel].active_state = pwm_info->active_state;
	gdisp.pwm[channel].duty_ns = pwm_info->duty_ns;
	gdisp.pwm[channel].period_ns = pwm_info->period_ns;
	gdisp.pwm[channel].entire_cycle = entire_cycle;
	gdisp.pwm[channel].active_cycle = active_cycle;

	if (channel == 0) {
		pwm_write_reg(0x204, ((entire_cycle - 1) << 16) | active_cycle);

		tmp = pwm_read_reg(0x200) & 0xffffff00;
		/*
		 * bit6: gating the special clock for pwm0
		 * bit5: pwm0: active state is high level
		 */
		tmp |= ((1 << 6) | (pwm_info->active_state << 5) |
			pre_scal[pre_scal_id][1]);
		pwm_write_reg(0x200, tmp);
	} else {
		pwm_write_reg(0x208, ((entire_cycle - 1) << 16) | active_cycle);

		tmp = pwm_read_reg(0x200) & 0xff807fff;
		/*
		 * bit21: gating the special clock for pwm1
		 * bit20: pwm1: active state is high level
		 */
		tmp |= ((1 << 21) | (pwm_info->active_state << 20) |
			(pre_scal[pre_scal_id][1] << 15));
		pwm_write_reg(0x200, tmp);
	}

	pwm_enable(channel, pwm_info->enable);

	return 0;
}
#endif /* CONFIG_ARCH_SUN4I */
EXPORT_SYMBOL(pwm_set_para);

__s32 pwm_get_para(__u32 channel, __pwm_info_t *pwm_info)
{
	pwm_info->enable = gdisp.pwm[channel].enable;
	pwm_info->active_state = gdisp.pwm[channel].active_state;
	pwm_info->duty_ns = gdisp.pwm[channel].duty_ns;
	pwm_info->period_ns = gdisp.pwm[channel].period_ns;

	return 0;
}
EXPORT_SYMBOL(pwm_get_para);

static __s32
pwm_set_duty_ns(__u32 channel, __u32 duty_ns)
{
	__u32 active_cycle = 0;
	__u32 tmp;

	active_cycle = (duty_ns * gdisp.pwm[channel].entire_cycle +
			(gdisp.pwm[channel].period_ns / 2)) /
		gdisp.pwm[channel].period_ns;

	if (channel == 0) {
		tmp = pwm_read_reg(0x204);
		pwm_write_reg(0x204, (tmp & 0xffff0000) | active_cycle);
	} else {
		tmp = pwm_read_reg(0x208);
		pwm_write_reg(0x208, (tmp & 0xffff0000) | active_cycle);
	}

	gdisp.pwm[channel].duty_ns = duty_ns;

	/* DE_INF("%d,%d,%d,%d\n", duty_ns, gdisp.pwm[channel].period_ns, "
	   "active_cycle, gdisp.pwm[channel].entire_cycle); */

	return 0;
}
EXPORT_SYMBOL(pwm_set_duty_ns);

__s32 LCD_PWM_EN(__u32 sel, __bool b_en)
{
	if (gdisp.screen[sel].lcd_cfg.lcd_pwm_used) {
		user_gpio_set_t gpio_info[1];
		__hdle hdl;

		memcpy(gpio_info, &(gdisp.screen[sel].lcd_cfg.lcd_pwm),
		       sizeof(user_gpio_set_t));

		if (!sunxi_is_version_A() &&
		    (gpanel_info[sel].lcd_pwm_not_used == 0)) {
			if (b_en)
				pwm_enable(gpanel_info[sel].lcd_pwm_ch, b_en);
			else {
				gpio_info->mul_sel = 0;
				hdl = OSAL_GPIO_Request(gpio_info, 1);
				OSAL_GPIO_Release(hdl, 2);
			}
		} else {
			if (b_en != gpanel_info[sel].lcd_pwm_pol) {
				gpio_info->mul_sel = 1;
				gpio_info->data = 1;
				hdl = OSAL_GPIO_Request(gpio_info, 1);
				OSAL_GPIO_Release(hdl, 2);
			} else {
				gpio_info->mul_sel = 1;
				gpio_info->data = 0;
				hdl = OSAL_GPIO_Request(gpio_info, 1);
				OSAL_GPIO_Release(hdl, 2);
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(LCD_PWM_EN);

__s32 LCD_BL_EN(__u32 sel, __bool b_en)
{
	user_gpio_set_t gpio_info[1];
	__hdle hdl;

	if (gdisp.screen[sel].lcd_cfg.lcd_bl_en_used) {
		memcpy(gpio_info, &(gdisp.screen[sel].lcd_cfg.lcd_bl_en),
		       sizeof(user_gpio_set_t));

		if (!b_en)
			gpio_info->data = (gpio_info->data == 0) ? 1 : 0;

		hdl = OSAL_GPIO_Request(gpio_info, 1);
		OSAL_GPIO_Release(hdl, 2);
	}

	return 0;
}
EXPORT_SYMBOL(LCD_BL_EN);

__s32
LCD_POWER_EN(__u32 sel, __bool b_en)
{
	user_gpio_set_t gpio_info[1];
	__hdle hdl;

	if (gdisp.screen[sel].lcd_cfg.lcd_power_used) {
		memcpy(gpio_info, &(gdisp.screen[sel].lcd_cfg.lcd_power),
		       sizeof(user_gpio_set_t));

		if (!b_en)
			gpio_info->data = (gpio_info->data == 0) ? 1 : 0;

		hdl = OSAL_GPIO_Request(gpio_info, 1);
		OSAL_GPIO_Release(hdl, 2);
	}

	return 0;
}
EXPORT_SYMBOL(LCD_POWER_EN);

__s32
LCD_GPIO_request(__u32 sel, __u32 io_index)
{
	return 0;
}
EXPORT_SYMBOL(LCD_GPIO_request);

__s32
LCD_GPIO_release(__u32 sel, __u32 io_index)
{
	return 0;
}
EXPORT_SYMBOL(LCD_GPIO_release);

__s32
LCD_GPIO_set_attr(__u32 sel, __u32 io_index, __bool b_output)
{
	char gpio_name[20];

	sprintf(gpio_name, "lcd_gpio_%d", io_index);
	return OSAL_GPIO_DevSetONEPIN_IO_STATUS(gdisp.screen[sel].
						gpio_hdl[io_index], b_output,
						gpio_name);
}
EXPORT_SYMBOL(LCD_GPIO_set_attr);

__s32
LCD_GPIO_read(__u32 sel, __u32 io_index)
{
	char gpio_name[20];

	sprintf(gpio_name, "lcd_gpio_%d", io_index);
	return OSAL_GPIO_DevREAD_ONEPIN_DATA(gdisp.screen[sel].
					     gpio_hdl[io_index], gpio_name);
}
EXPORT_SYMBOL(LCD_GPIO_read);

__s32
LCD_GPIO_write(__u32 sel, __u32 io_index, __u32 data)
{
	char gpio_name[20];

	sprintf(gpio_name, "lcd_gpio_%d", io_index);
	return OSAL_GPIO_DevWRITE_ONEPIN_DATA(gdisp.screen[sel].
					      gpio_hdl[io_index], data,
					      gpio_name);
}
EXPORT_SYMBOL(LCD_GPIO_write);

static __s32
LCD_GPIO_init(__u32 sel)
{
	__u32 i = 0;

	for (i = 0; i < 4; i++) {
		gdisp.screen[sel].gpio_hdl[i] = 0;

		if (gdisp.screen[sel].lcd_cfg.lcd_gpio_used[i]) {
			user_gpio_set_t gpio_info[1];

			memcpy(gpio_info,
			       &(gdisp.screen[sel].lcd_cfg.lcd_gpio[i]),
			       sizeof(user_gpio_set_t));
			gdisp.screen[sel].gpio_hdl[i] =
				OSAL_GPIO_Request(gpio_info, 1);
		}
	}

	return 0;
}

static __s32
LCD_GPIO_exit(__u32 sel)
{
	__u32 i = 0;

	for (i = 0; i < 4; i++)
		if (gdisp.screen[sel].gpio_hdl[i])
			OSAL_GPIO_Release(gdisp.screen[sel].gpio_hdl[i], 2);

	return 0;
}

static void
LCD_CPU_register_irq(__u32 sel, void (*Lcd_cpuisr_proc) (void))
{
	gdisp.screen[sel].LCD_CPUIF_ISR = Lcd_cpuisr_proc;
}
EXPORT_SYMBOL(LCD_CPU_register_irq);

__s32 Disp_lcdc_pin_cfg(__u32 sel, __disp_output_type_t out_type, __u32 bon)
{
	if (out_type == DISP_OUTPUT_TYPE_LCD) {
		__hdle lcd_pin_hdl;
		int i;

		for (i = 0; i < 28; i++) {
			if (gdisp.screen[sel].lcd_cfg.lcd_io_used[i]) {
				user_gpio_set_t gpio_info[1];

				memcpy(gpio_info,
				       &(gdisp.screen[sel].lcd_cfg.lcd_io[i]),
				       sizeof(user_gpio_set_t));
				if (!bon)
					gpio_info->mul_sel = 0;
				else {
					if ((gpanel_info[sel].lcd_if == 3) &&
					    (gpio_info->mul_sel == 2))
						gpio_info->mul_sel = 3;
				}
				lcd_pin_hdl = OSAL_GPIO_Request(gpio_info, 1);
				OSAL_GPIO_Release(lcd_pin_hdl, 2);
			}
		}
	} else if (out_type == DISP_OUTPUT_TYPE_VGA) {
		__u32 reg_start = 0;
		__u32 tmp = 0;

		if (sel == 0)
			reg_start = gdisp.init_para.base_pioc + 0x6c;
		else
			reg_start = gdisp.init_para.base_pioc + 0xfc;

		if (bon) {
			tmp = sys_get_wvalue(reg_start + 0x0c) & 0xffff00ff;
			sys_put_wvalue(reg_start + 0x0c, tmp | 0x00002200);
		} else {
			tmp = sys_get_wvalue(reg_start + 0x0c) & 0xffff00ff;
			sys_put_wvalue(reg_start + 0x0c, tmp);
		}
	}

	return DIS_SUCCESS;
}

static irqreturn_t Disp_lcdc_event_proc(int irq, void *parg)
{
	__u32 lcdc_flags;
	__u32 sel = (__u32) parg;

	lcdc_flags = LCDC_query_int(sel);
	LCDC_clear_int(sel, lcdc_flags);

	if (lcdc_flags & LCDC_VBI_LCD)
		LCD_vbi_event_proc(sel, 0);

	if (lcdc_flags & LCDC_VBI_HD)
		LCD_vbi_event_proc(sel, 1);

	return OSAL_IRQ_RETURN;
}

__s32 Disp_lcdc_init(__u32 sel)
{
	irqreturn_t ret;

	LCD_get_sys_config(sel, &(gdisp.screen[sel].lcd_cfg));

	lcdc_clk_init(sel);
	lvds_clk_init();
	lcdc_clk_on(sel); /* ??need to be open */
	LCDC_init(sel);
	lcdc_clk_off(sel);

	if (sel == 0)
		ret = request_irq(INTC_IRQNO_LCDC0, Disp_lcdc_event_proc,
				  IRQF_DISABLED, "sunxi lcd0", (void *)sel);
	else
		ret = request_irq(INTC_IRQNO_LCDC1, Disp_lcdc_event_proc,
				  IRQF_DISABLED, "sunxi lcd1", (void *)sel);

	if (gdisp.screen[sel].lcd_cfg.lcd_used) {
		if (lcd_panel_fun[sel].cfg_panel_info)
			lcd_panel_fun[sel].cfg_panel_info(&gpanel_info[sel]);
		else
			LCD_get_panel_para(sel, &gpanel_info[sel]);

		gpanel_info[sel].tcon_index = 0;

		if (!sunxi_is_version_A() &&
		    (gpanel_info[sel].lcd_pwm_not_used == 0)) {
			__pwm_info_t pwm_info;

			pwm_info.enable = 0;
			pwm_info.active_state = 1;
			pwm_info.period_ns =
			    1000000 / gpanel_info[sel].lcd_pwm_freq;
			if (gpanel_info[sel].lcd_pwm_pol == 0)
				pwm_info.duty_ns =
				    (gdisp.screen[sel].lcd_cfg.init_bright *
				     pwm_info.period_ns) / 256;
			else
				pwm_info.duty_ns =
				    ((256 -
				      gdisp.screen[sel].lcd_cfg.init_bright) *
				     pwm_info.period_ns) / 256;

			pwm_set_para(gpanel_info[sel].lcd_pwm_ch, &pwm_info);
		}
		LCD_GPIO_init(sel);
	}

	return DIS_SUCCESS;
}

__s32 Disp_lcdc_exit(__u32 sel)
{
	if (sel == 0) {
		disable_irq(INTC_IRQNO_LCDC0);
		free_irq(INTC_IRQNO_LCDC0, (void *)sel);
	} else if (sel == 1) {
		disable_irq(INTC_IRQNO_LCDC1);
		free_irq(INTC_IRQNO_LCDC1, (void *)sel);
	}

	LCDC_exit(sel);

	lcdc_clk_exit(sel);

	LCD_GPIO_exit(sel);

	return DIS_SUCCESS;
}

__u32 tv_mode_to_width(__disp_tv_mode_t mode)
{
	__u32 width = 0;

	switch (mode) {
	case DISP_TV_MOD_480I:
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_480P:
	case DISP_TV_MOD_576P:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_NTSC_SVIDEO:
	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_M_SVIDEO:
	case DISP_TV_MOD_PAL_NC:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
		width = 720;
		break;
	case DISP_TV_MOD_720P_50HZ:
	case DISP_TV_MOD_720P_60HZ:
	case DISP_TV_MOD_720P_50HZ_3D_FP:
	case DISP_TV_MOD_720P_60HZ_3D_FP:
		width = 1280;
		break;
	case DISP_TV_MOD_1080I_50HZ:
	case DISP_TV_MOD_1080I_60HZ:
	case DISP_TV_MOD_1080P_24HZ:
	case DISP_TV_MOD_1080P_50HZ:
	case DISP_TV_MOD_1080P_60HZ:
	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		width = 1920;
		break;
	default:
		width = 0;
		break;
	}

	return width;
}

__u32 tv_mode_to_height(__disp_tv_mode_t mode)
{
	__u32 height = 0;

	switch (mode) {
	case DISP_TV_MOD_480I:
	case DISP_TV_MOD_480P:
	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_NTSC_SVIDEO:
	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_M_SVIDEO:
		height = 480;
		break;
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_576P:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL_NC:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
		height = 576;
		break;
	case DISP_TV_MOD_720P_50HZ:
	case DISP_TV_MOD_720P_60HZ:
		height = 720;
		break;
	case DISP_TV_MOD_1080I_50HZ:
	case DISP_TV_MOD_1080I_60HZ:
	case DISP_TV_MOD_1080P_24HZ:
	case DISP_TV_MOD_1080P_50HZ:
	case DISP_TV_MOD_1080P_60HZ:
		height = 1080;
		break;
	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		height = 1080 * 2;
		break;
	case DISP_TV_MOD_720P_50HZ_3D_FP:
	case DISP_TV_MOD_720P_60HZ_3D_FP:
		height = 720 * 2;
		break;
	default:
		height = 0;
		break;
	}

	return height;
}

__u32 vga_mode_to_width(__disp_vga_mode_t mode)
{
	__u32 width = 0;

	switch (mode) {
	case DISP_VGA_H1680_V1050:
		width = 1680;
		break;
	case DISP_VGA_H1440_V900:
		width = 1440;
		break;
	case DISP_VGA_H1360_V768:
		width = 1360;
		break;
	case DISP_VGA_H1280_V1024:
		width = 1280;
		break;
	case DISP_VGA_H1024_V768:
		width = 1024;
		break;
	case DISP_VGA_H800_V600:
		width = 800;
		break;
	case DISP_VGA_H640_V480:
		width = 640;
		break;
	case DISP_VGA_H1440_V900_RB:
		width = 1440;
		break;
	case DISP_VGA_H1680_V1050_RB:
		width = 1680;
		break;
	case DISP_VGA_H1920_V1080_RB:
	case DISP_VGA_H1920_V1080:
		width = 1920;
		break;
	case DISP_VGA_H1280_V720:
		width = 1280;
		break;
	default:
		width = 0;
		break;
	}

	return width;
}

__u32 vga_mode_to_height(__disp_vga_mode_t mode)
{
	__u32 height = 0;

	switch (mode) {
	case DISP_VGA_H1680_V1050:
		height = 1050;
		break;
	case DISP_VGA_H1440_V900:
		height = 900;
		break;
	case DISP_VGA_H1360_V768:
		height = 768;
		break;
	case DISP_VGA_H1280_V1024:
		height = 1024;
		break;
	case DISP_VGA_H1024_V768:
		height = 768;
		break;
	case DISP_VGA_H800_V600:
		height = 600;
		break;
	case DISP_VGA_H640_V480:
		height = 480;
		break;
	case DISP_VGA_H1440_V900_RB:
		height = 1440;
		break;
	case DISP_VGA_H1680_V1050_RB:
		height = 1050;
		break;
	case DISP_VGA_H1920_V1080_RB:
	case DISP_VGA_H1920_V1080:
		height = 1080;
		break;
	case DISP_VGA_H1280_V720:
		height = 720;
		break;
	default:
		height = 0;
		break;
	}

	return height;
}

/*
 * return 0: progressive scan mode; return 1: interlace scan mode
 */
__u32 Disp_get_screen_scan_mode(__disp_tv_mode_t tv_mode)
{
	__u32 ret = 0;

	switch (tv_mode) {
	case DISP_TV_MOD_480I:
	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_NTSC_SVIDEO:
	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_M_SVIDEO:
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL_NC:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
	case DISP_TV_MOD_1080I_50HZ:
	case DISP_TV_MOD_1080I_60HZ:
		ret = 1;
	default:
		break;
	}

	return ret;
}

__s32 BSP_disp_get_screen_width(__u32 sel)
{
	__u32 width = 0;

	if ((gdisp.screen[sel].status & LCD_ON) ||
	    (gdisp.screen[sel].status & TV_ON) ||
	    (gdisp.screen[sel].status & HDMI_ON) ||
	    (gdisp.screen[sel].status & VGA_ON)) {
		width = DE_BE_get_display_width(sel);
	} else {
		width = gpanel_info[sel].lcd_x;
	}

	return width;
}

__s32 BSP_disp_get_screen_height(__u32 sel)
{
	__u32 height = 0;

	if ((gdisp.screen[sel].status & LCD_ON) ||
	    (gdisp.screen[sel].status & TV_ON) ||
	    (gdisp.screen[sel].status & HDMI_ON) ||
	    (gdisp.screen[sel].status & VGA_ON)) {
		height = DE_BE_get_display_height(sel);
	} else {
		height = gpanel_info[sel].lcd_y;
	}

	return height;
}

__s32 BSP_disp_get_output_type(__u32 sel)
{
	if (gdisp.screen[sel].status & TV_ON)
		return (__s32) DISP_OUTPUT_TYPE_TV;

	if (gdisp.screen[sel].status & LCD_ON)
		return (__s32) DISP_OUTPUT_TYPE_LCD;

	if (gdisp.screen[sel].status & HDMI_ON)
		return (__s32) DISP_OUTPUT_TYPE_HDMI;

	if (gdisp.screen[sel].status & VGA_ON)
		return (__s32) DISP_OUTPUT_TYPE_VGA;

	return (__s32) DISP_OUTPUT_TYPE_NONE;
}

__s32 BSP_disp_get_frame_rate(__u32 sel)
{
	__s32 frame_rate = 60;

	if (gdisp.screen[sel].output_type & DISP_OUTPUT_TYPE_LCD) {
		frame_rate = (gpanel_info[sel].lcd_dclk_freq * 1000000) /
			(gpanel_info[sel].lcd_ht *
			 (gpanel_info[sel].lcd_vt / 2));
	} else if (gdisp.screen[sel].output_type & DISP_OUTPUT_TYPE_TV) {
		switch (gdisp.screen[sel].tv_mode) {
		case DISP_TV_MOD_480I:
		case DISP_TV_MOD_480P:
		case DISP_TV_MOD_NTSC:
		case DISP_TV_MOD_NTSC_SVIDEO:
		case DISP_TV_MOD_PAL_M:
		case DISP_TV_MOD_PAL_M_SVIDEO:
		case DISP_TV_MOD_720P_60HZ:
		case DISP_TV_MOD_1080I_60HZ:
		case DISP_TV_MOD_1080P_60HZ:
			frame_rate = 60;
			break;
		case DISP_TV_MOD_576I:
		case DISP_TV_MOD_576P:
		case DISP_TV_MOD_PAL:
		case DISP_TV_MOD_PAL_SVIDEO:
		case DISP_TV_MOD_PAL_NC:
		case DISP_TV_MOD_PAL_NC_SVIDEO:
		case DISP_TV_MOD_720P_50HZ:
		case DISP_TV_MOD_1080I_50HZ:
		case DISP_TV_MOD_1080P_50HZ:
			frame_rate = 50;
			break;
		default:
			break;
		}
	} else if (gdisp.screen[sel].output_type & DISP_OUTPUT_TYPE_HDMI) {
		switch (gdisp.screen[sel].hdmi_mode) {
		case DISP_TV_MOD_480I:
		case DISP_TV_MOD_480P:
		case DISP_TV_MOD_720P_60HZ:
		case DISP_TV_MOD_1080I_60HZ:
		case DISP_TV_MOD_1080P_60HZ:
		case DISP_TV_MOD_720P_60HZ_3D_FP:
			frame_rate = 60;
			break;
		case DISP_TV_MOD_576I:
		case DISP_TV_MOD_576P:
		case DISP_TV_MOD_720P_50HZ:
		case DISP_TV_MOD_1080I_50HZ:
		case DISP_TV_MOD_1080P_50HZ:
		case DISP_TV_MOD_720P_50HZ_3D_FP:
			frame_rate = 50;
			break;
		case DISP_TV_MOD_1080P_24HZ:
		case DISP_TV_MOD_1080P_24HZ_3D_FP:
			frame_rate = 24;
			break;
		default:
			break;
		}
	} else if (gdisp.screen[sel].output_type & DISP_OUTPUT_TYPE_VGA) {
		frame_rate = 60;
	}

	return frame_rate;
}

__s32 BSP_disp_lcd_open_before(__u32 sel)
{
	disp_clk_cfg(sel, DISP_OUTPUT_TYPE_LCD, DIS_NULL);
	lcdc_clk_on(sel);
	image_clk_on(sel);

	/*
	 * set image normal channel start bit, because every de_clk_off( )
	 * will reset this bit
	 */
	Image_open(sel);
	Disp_lcdc_pin_cfg(sel, DISP_OUTPUT_TYPE_LCD, 1);

	if (gpanel_info[sel].tcon_index == 0)
		TCON0_cfg(sel, (__panel_para_t *) &gpanel_info[sel]);
	else
		TCON1_cfg_ex(sel, (__panel_para_t *) &gpanel_info[sel]);

#ifdef CONFIG_ARCH_SUN4I
	BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_LCD);
#else
	BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_LCD,
				gdisp.screen[sel].iep_status & DRC_USED);
#endif
	DE_BE_set_display_size(sel, gpanel_info[sel].lcd_x,
			       gpanel_info[sel].lcd_y);
	DE_BE_Output_Select(sel, sel);

	open_flow[sel].func_num = 0;
	lcd_panel_fun[sel].cfg_open_flow(sel);

	return DIS_SUCCESS;
}

__s32 BSP_disp_lcd_open_after(__u32 sel)
{
	//esMEM_SwitchDramWorkMode(DRAM_WORK_MODE_LCD);
	gdisp.screen[sel].b_out_interlace = 0;
	gdisp.screen[sel].status |= LCD_ON;
	gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_LCD;
	Lcd_Panel_Parameter_Check(sel);
#ifdef CONFIG_ARCH_SUN5I
	Disp_drc_enable(sel, TRUE);
#endif

	Display_set_fb_timing(sel);

	return DIS_SUCCESS;
}

__lcd_flow_t *BSP_disp_lcd_get_open_flow(__u32 sel)
{
	return &open_flow[sel];
}

__s32 BSP_disp_lcd_close_befor(__u32 sel)
{
	close_flow[sel].func_num = 0;
	lcd_panel_fun[sel].cfg_close_flow(sel);
#ifdef CONFIG_ARCH_SUN5I
	/* must close immediately, cause vbi may not come */
	Disp_drc_enable(sel, 2);
#endif

	gdisp.screen[sel].status &= ~LCD_ON;
	gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_NONE;
	return DIS_SUCCESS;
}

__s32 BSP_disp_lcd_close_after(__u32 sel)
{
	Image_close(sel);

	Disp_lcdc_pin_cfg(sel, DISP_OUTPUT_TYPE_LCD, 0);
	image_clk_off(sel);
	lcdc_clk_off(sel);

	gdisp.screen[sel].pll_use_status &=
		(gdisp.screen[sel].pll_use_status == VIDEO_PLL0_USED) ?
		~VIDEO_PLL0_USED : ~VIDEO_PLL1_USED;

	return DIS_SUCCESS;
}

__lcd_flow_t *BSP_disp_lcd_get_close_flow(__u32 sel)
{
	return &close_flow[sel];
}

__s32 BSP_disp_lcd_xy_switch(__u32 sel, __s32 mode)
{
	if (gdisp.screen[sel].LCD_CPUIF_XY_Swap != NULL) {
		LCD_CPU_AUTO_FLUSH(sel, 0);
		LCD_XY_SWAP(sel);
		(*gdisp.screen[sel].LCD_CPUIF_XY_Swap)(mode);
		LCD_CPU_AUTO_FLUSH(sel, 1);
	}

	return DIS_SUCCESS;
}

/*
 * sun4i: 0-16
 * sun5i: 0-256
 */
__s32 BSP_disp_lcd_set_bright(__u32 sel, __u32 bright
#ifdef CONFIG_ARCH_SUN5I
			      , __u32 from_iep
#endif
)
{
	__u32 duty_ns;

	if (!sunxi_is_version_A() && (gpanel_info[sel].lcd_pwm_not_used == 0)) {
#ifdef CONFIG_ARCH_SUN4I
		if (bright != 0)
			bright += 1;
#endif

#ifdef CONFIG_ARCH_SUN4I
		if (gpanel_info[sel].lcd_pwm_pol == 0)
			duty_ns =
			    (bright *
			     gdisp.pwm[gpanel_info[sel].lcd_pwm_ch].period_ns +
			     128) / 256;
		else
			duty_ns =
			    ((256 -
			      bright) *
			     gdisp.pwm[gpanel_info[sel].lcd_pwm_ch].period_ns +
			     128) / 256;
#else
		if (gpanel_info[sel].lcd_pwm_pol == 0)
			duty_ns =
			    (bright * gdisp.screen[sel].lcd_bright_dimming *
			     gdisp.pwm[gpanel_info[sel].lcd_pwm_ch].period_ns /
			     256 + 128) / 256;
		else
			duty_ns =
			    ((256 -
			      bright * gdisp.screen[sel].lcd_bright_dimming /
			      256) *
			     gdisp.pwm[gpanel_info[sel].lcd_pwm_ch].period_ns +
			     128) / 256;
#endif

		pwm_set_duty_ns(gpanel_info[sel].lcd_pwm_ch, duty_ns);
	}
#ifdef CONFIG_ARCH_SUN5I
	if (!from_iep)
#endif
		gdisp.screen[sel].lcd_bright = bright;

	return DIS_SUCCESS;
}

__s32 BSP_disp_lcd_get_bright(__u32 sel)
{
	return gdisp.screen[sel].lcd_bright;
}

__s32 BSP_disp_set_gamma_table(__u32 sel, __u32 *gamtbl_addr,
			       __u32 gamtbl_size)
{
	if ((gamtbl_addr == NULL) || (gamtbl_size > 1024)) {
		DE_WRN("para invalid in BSP_disp_set_gamma_table\n");
		return DIS_FAIL;
	}

	TCON1_set_gamma_table(sel, (__u32) (gamtbl_addr), gamtbl_size);

	return DIS_SUCCESS;
}

__s32 BSP_disp_gamma_correction_enable(__u32 sel)
{
	TCON1_set_gamma_Enable(sel, TRUE);

	return DIS_SUCCESS;
}

__s32 BSP_disp_gamma_correction_disable(__u32 sel)
{
	TCON1_set_gamma_Enable(sel, FALSE);

	return DIS_SUCCESS;
}

__s32 BSP_disp_lcd_set_src(__u32 sel, __disp_lcdc_src_t src)
{
	switch (src) {
	case DISP_LCDC_SRC_DE_CH1:
		TCON0_select_src(sel, LCDC_SRC_DE1);
		break;

	case DISP_LCDC_SRC_DE_CH2:
		TCON0_select_src(sel, LCDC_SRC_DE2);
		break;

	case DISP_LCDC_SRC_DMA:
		TCON0_select_src(sel, LCDC_SRC_DMA);
		break;

	case DISP_LCDC_SRC_WHITE:
		TCON0_select_src(sel, LCDC_SRC_WHITE);
		break;

	case DISP_LCDC_SRC_BLACK:
		TCON0_select_src(sel, LCDC_SRC_BLACK);
		break;

	default:
		DE_WRN("not supported lcdc src:%d in BSP_disp_tv_set_src\n",
		       src);
		return DIS_NOT_SUPPORT;
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_lcd_user_defined_func(__u32 sel, __u32 para1, __u32 para2,
				     __u32 para3)
{
	return lcd_panel_fun[sel].lcd_user_defined_func(sel, para1, para2,
							para3);
}

void LCD_set_panel_funs(__lcd_panel_fun_t *lcd0_cfg,
			__lcd_panel_fun_t *lcd1_cfg)
{
	memset(&lcd_panel_fun[0], 0, sizeof(__lcd_panel_fun_t));
	memset(&lcd_panel_fun[1], 0, sizeof(__lcd_panel_fun_t));

	lcd_panel_fun[0].cfg_panel_info = lcd0_cfg->cfg_panel_info;
	lcd_panel_fun[0].cfg_open_flow = lcd0_cfg->cfg_open_flow;
	lcd_panel_fun[0].cfg_close_flow = lcd0_cfg->cfg_close_flow;
	lcd_panel_fun[0].lcd_user_defined_func =
		lcd0_cfg->lcd_user_defined_func;
	lcd_panel_fun[1].cfg_panel_info = lcd1_cfg->cfg_panel_info;
	lcd_panel_fun[1].cfg_open_flow = lcd1_cfg->cfg_open_flow;
	lcd_panel_fun[1].cfg_close_flow = lcd1_cfg->cfg_close_flow;
	lcd_panel_fun[1].lcd_user_defined_func =
		lcd1_cfg->lcd_user_defined_func;
}
EXPORT_SYMBOL(LCD_set_panel_funs);

__s32 BSP_disp_get_timing(__u32 sel, __disp_tcon_timing_t *tt)
{
	memset(tt, 0, sizeof(__disp_tcon_timing_t));

	if (gdisp.screen[sel].status & LCD_ON) {
		LCDC_get_timing(sel, 0, tt);
		tt->pixel_clk = gpanel_info[sel].lcd_dclk_freq * 1000;
	} else if ((gdisp.screen[sel].status & TV_ON) ||
		   (gdisp.screen[sel].status & HDMI_ON)) {
		__disp_tv_mode_t mode = gdisp.screen[sel].tv_mode;

		LCDC_get_timing(sel, 1, tt);
		tt->pixel_clk =
			(clk_tab.tv_clk_tab[mode].tve_clk /
			 clk_tab.tv_clk_tab[mode].pre_scale) / 1000;
	} else if (gdisp.screen[sel].status & VGA_ON) {
		__disp_vga_mode_t mode = gdisp.screen[sel].vga_mode;

		LCDC_get_timing(sel, 1, tt);
		tt->pixel_clk =
			(clk_tab.vga_clk_tab[mode].tve_clk /
			 clk_tab.vga_clk_tab[mode].pre_scale) / 1000;
	} else {
		DE_INF("get timing fail because device is not output !\n");
		return -1;
	}

	return 0;
}

__u32 BSP_disp_get_cur_line(__u32 sel)
{
	__u32 line = 0;

	if (gdisp.screen[sel].status & LCD_ON) {
		line = LCDC_get_cur_line(sel, 0);
	} else if ((gdisp.screen[sel].status & TV_ON) ||
		   (gdisp.screen[sel].status & HDMI_ON) ||
		   (gdisp.screen[sel].status & VGA_ON)) {
		line = LCDC_get_cur_line(sel, 1);
	}

	return line;
}

#ifdef CONFIG_ARCH_SUN5I
__s32 BSP_disp_close_lcd_backlight(__u32 sel)
{
	user_gpio_set_t gpio_info[1];
	__hdle hdl;
	int value, ret;
	char primary_key[20];
	sprintf(primary_key, "lcd%d_para", sel);
	value = 1;
	ret = script_parser_fetch(primary_key, "lcd_bl_en_used", &value, 1);
	if (value == 0) {
		DE_INF("%s.lcd_bl_en is not used\n", primary_key);
	} else {
		ret =
		    script_parser_fetch(primary_key, "lcd_bl_en",
					(int *)gpio_info,
					sizeof(user_gpio_set_t) / sizeof(int));
		if (ret < 0) {
			DE_INF("%s.lcd_bl_en not exist\n", primary_key);
		} else {
			gpio_info->data = (gpio_info->data == 0) ? 1 : 0;
			hdl = OSAL_GPIO_Request(gpio_info, 1);
			OSAL_GPIO_Release(hdl, 2);
		}
	}
	value = 1;
	ret = script_parser_fetch(primary_key, "lcd_pwm_used", &value, 1);
	if (value == 0) {
		DE_INF("%s.lcd_pwm is not used\n", primary_key);
	} else {
		ret = script_parser_fetch(primary_key, "lcd_pwm",
					  (int *)gpio_info,
					  sizeof(user_gpio_set_t) /
					  sizeof(int));
		if (ret < 0) {
			DE_INF("%s.lcd_pwm not exist\n", primary_key);
		} else {
			gpio_info->mul_sel = 0;
			hdl = OSAL_GPIO_Request(gpio_info, 1);
			OSAL_GPIO_Release(hdl, 2);
		}
	}
	return 0;
}
#endif /* CONFIG_ARCH_SUN5I */

