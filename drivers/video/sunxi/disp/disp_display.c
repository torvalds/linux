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

#include "disp_display.h"
#include "disp_de.h"
#include "disp_lcd.h"
#include "disp_tv.h"
#include "disp_event.h"
#include "disp_sprite.h"
#include "disp_scaler.h"
#include "disp_video.h"
#include "disp_clk.h"
#include "disp_hdmi.h"

__disp_dev_t gdisp;
static bool disp_initialised;

__s32 BSP_disp_init(__disp_bsp_init_para *para)
{
	__u32 i = 0, screen_id = 0;

	memset(&gdisp, 0x00, sizeof(__disp_dev_t));

	for (screen_id = 0; screen_id < 2; screen_id++) {
		gdisp.screen[screen_id].max_layers = 4;
		for (i = 0; i < gdisp.screen[screen_id].max_layers; i++)
			gdisp.screen[screen_id].layer_manage[i].para.prio =
			    IDLE_PRIO;

		gdisp.screen[screen_id].image_output_type = IMAGE_OUTPUT_LCDC;

		gdisp.screen[screen_id].bright = 50;
		gdisp.screen[screen_id].contrast = 50;
		gdisp.screen[screen_id].saturation = 50;
		gdisp.screen[screen_id].hue = 50;

		gdisp.scaler[screen_id].bright = 50;
		gdisp.scaler[screen_id].contrast = 50;
		gdisp.scaler[screen_id].saturation = 50;
		gdisp.scaler[screen_id].hue = 50;

		gdisp.screen[screen_id].lcd_bright = 192;

#ifdef CONFIG_ARCH_SUN5I
		gdisp.screen[screen_id].lcd_bright_dimming = 256;
#endif
	}
	memcpy(&gdisp.init_para, para, sizeof(__disp_bsp_init_para));
	memset(g_video, 0, sizeof(g_video));

	DE_Set_Reg_Base(0, para->base_image0);
	DE_SCAL_Set_Reg_Base(0, para->base_scaler0);
	LCDC_set_reg_base(0, para->base_lcdc0);
	TVE_set_reg_base(0, para->base_tvec0);

#ifdef CONFIG_ARCH_SUN4I
	DE_Set_Reg_Base(1, para->base_image1);
	DE_SCAL_Set_Reg_Base(1, para->base_scaler1);
	LCDC_set_reg_base(1, para->base_lcdc1);
	TVE_set_reg_base(1, para->base_tvec1);
#else
	DE_IEP_Set_Reg_Base(0, para->base_iep);
#endif

#ifdef CONFIG_ARCH_SUN5I
	BSP_disp_close_lcd_backlight(0);
#endif

	disp_pll_init();

	Scaler_Init(0);
	Image_init(0);
	Disp_lcdc_init(0);
	Disp_TVEC_Init(0);

#ifdef CONFIG_ARCH_SUN4I
	Scaler_Init(1);
	Image_init(1);
	Disp_lcdc_init(1);
	Disp_TVEC_Init(1);
#endif

	Display_Hdmi_Init();

#ifdef CONFIG_ARCH_SUN5I
	Disp_iep_init(0);
#endif

	disp_initialised = true;

	return DIS_SUCCESS;
}

__s32 BSP_disp_exit(__u32 mode)
{
	if (!disp_initialised)
		return DIS_SUCCESS;

	if (mode == DISP_EXIT_MODE_CLEAN_ALL) {
		BSP_disp_close();

		Scaler_Exit(0);
		Image_exit(0);
		Disp_lcdc_exit(0);
		Disp_TVEC_Exit(0);

#ifdef CONFIG_ARCH_SUN4I
		Scaler_Exit(1);
		Image_exit(1);
		Disp_lcdc_exit(1);
		Disp_TVEC_Exit(1);
#endif

		Display_Hdmi_Exit();

#ifdef CONFIG_ARCH_SUN5I
		Disp_iep_exit(0);
#endif
	} else if (mode == DISP_EXIT_MODE_CLEAN_PARTLY) {
		disable_irq(INTC_IRQNO_LCDC0);
		free_irq(INTC_IRQNO_LCDC0, NULL);

#ifdef CONFIG_ARCH_SUN4I
		disable_irq(INTC_IRQNO_LCDC1);
		free_irq(INTC_IRQNO_LCDC1, NULL);
#endif

		disable_irq(INTC_IRQNO_SCALER0);
		free_irq(INTC_IRQNO_SCALER0, NULL);

#ifdef CONFIG_ARCH_SUN4I
		disable_irq(INTC_IRQNO_SCALER1);
		free_irq(INTC_IRQNO_SCALER1, NULL);
#endif
	}

	return DIS_SUCCESS;
}

__s32 BSP_disp_open(void)
{
	return DIS_SUCCESS;
}

__s32 BSP_disp_close(void)
{
	__u32 sel = 0;

	if (!disp_initialised)
		return DIS_SUCCESS;

	for (sel = 0; sel < 2; sel++) {
		Image_close(sel);
		if (gdisp.scaler[sel].status & SCALER_USED)
			Scaler_close(sel);

		if (gdisp.screen[sel].lcdc_status & LCDC_TCON0_USED) {
			TCON0_close(sel);
			LCDC_close(sel);
		} else if (gdisp.screen[sel].lcdc_status & LCDC_TCON1_USED) {
			TCON1_close(sel);
			LCDC_close(sel);
		} else if (gdisp.screen[sel].status & (TV_ON | VGA_ON)) {
			TVE_close(sel);
		}
	}

	for (sel = 0; sel < 2; sel++) {
		gdisp.screen[sel].status &=
			~(IMAGE_USED | LCD_ON | TV_ON | VGA_ON | HDMI_ON);
		gdisp.screen[sel].lcdc_status &=
			~(LCDC_TCON0_USED & LCDC_TCON1_USED);
	}

	return DIS_SUCCESS;
}

__s32 BSP_disp_print_reg(__bool b_force_on, __u32 id)
{
	__u32 base = 0, size = 0;
	__u32 i = 0;
	unsigned char str[20];

	switch (id) {
	case DISP_REG_SCALER0:
		base = gdisp.init_para.base_scaler0;
		size = 0xa18;
		sprintf(str, "scaler0:\n");
		break;

	case DISP_REG_SCALER1:
		base = gdisp.init_para.base_scaler1;
		size = 0xa18;
		sprintf(str, "scaler1:\n");
		break;

	case DISP_REG_IMAGE0:
		base = gdisp.init_para.base_image0 + 0x800;
		size = 0xdff - 0x800;
		sprintf(str, "image0:\n");
		break;

	case DISP_REG_IMAGE1:
		base = gdisp.init_para.base_image1 + 0x800;
		size = 0xdff - 0x800;
		sprintf(str, "image1:\n");
		break;
	case DISP_REG_LCDC0:
		base = gdisp.init_para.base_lcdc0;
		size = 0x800;
		sprintf(str, "lcdc0:\n");
		break;

	case DISP_REG_LCDC1:
		base = gdisp.init_para.base_lcdc1;
		size = 0x800;
		sprintf(str, "lcdc1:\n");
		break;

	case DISP_REG_TVEC0:
		base = gdisp.init_para.base_tvec0;
		size = 0x20c;
		sprintf(str, "tvec0:\n");
		break;

	case DISP_REG_TVEC1:
		base = gdisp.init_para.base_tvec1;
		size = 0x20c;
		sprintf(str, "tvec1:\n");
		break;

	case DISP_REG_CCMU:
		base = gdisp.init_para.base_ccmu;
#ifdef CONFIG_ARCH_SUN4I
		size = 0x158;
#else
		size = 0x164;
#endif
		sprintf(str, "ccmu:\n");
		break;

	case DISP_REG_PIOC:
		base = gdisp.init_para.base_pioc;
		size = 0x228;
		sprintf(str, "pioc:\n");
		break;

	case DISP_REG_PWM:
		base = gdisp.init_para.base_pwm + 0x200;
		size = 0x0c;
		sprintf(str, "pwm:\n");
		break;

	default:
		return DIS_FAIL;
	}

	if (b_force_on)
		DE_WRN("%s", str);
	else
		DE_INF("%s", str);

	for (i = 0; i < size; i += 16) {
		__u32 reg[4];

		reg[0] = sys_get_wvalue(base + i);
		reg[1] = sys_get_wvalue(base + i + 4);
		reg[2] = sys_get_wvalue(base + i + 8);
		reg[3] = sys_get_wvalue(base + i + 12);

		if (b_force_on)
			DE_WRN("0x%08x:%08x,%08x:%08x,%08x\n", base + i,
			       reg[0], reg[1], reg[2], reg[3]);
		else
			DE_INF("0x%08x:%08x,%08x:%08x,%08x\n", base + i, reg[0],
			       reg[1], reg[2], reg[3]);
	}

	return DIS_SUCCESS;
}
