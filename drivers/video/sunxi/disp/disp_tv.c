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

#include "disp_tv.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_de.h"
#include "disp_lcd.h"
#include "disp_clk.h"
#include "OSAL_Pin.h"

__s32 Disp_Switch_Dram_Mode(__u32 type, __u8 tv_mod)
{
	return DIS_SUCCESS;
}

__s32 Disp_TVEC_Init(__u32 sel)
{
	__s32 ret = 0, value = 0;

	tve_clk_init(sel);
	tve_clk_on(sel);
	TVE_init(sel);
	tve_clk_off(sel);

	gdisp.screen[sel].dac_source[0] = DISP_TV_DAC_SRC_Y;
	gdisp.screen[sel].dac_source[1] = DISP_TV_DAC_SRC_PB;
	gdisp.screen[sel].dac_source[2] = DISP_TV_DAC_SRC_PR;
	gdisp.screen[sel].dac_source[3] = DISP_TV_DAC_SRC_COMPOSITE;

	ret = script_parser_fetch("tv_out_dac_para", "dac_used", &value, 1);
	if (ret < 0) {
		DE_INF("fetch script data tv_out_dac_para.dac_used fail\n");
	} else {
		DE_INF("tv_out_dac_para.dac_used=%d\n", value);

		if (value != 0) {
			__s32 i = 0;
			char sub_key[20];

			for (i = 0; i < 4; i++) {
				sprintf(sub_key, "dac%d_src", i);

				ret = script_parser_fetch("tv_out_dac_para",
							  sub_key, &value, 1);
				if (ret < 0) {
					DE_INF("fetch script data "
					       "tv_out_dac_para.%s fail\n",
					       sub_key);
				} else {
					gdisp.screen[sel].dac_source[i] = value;
					DE_INF("tv_out_dac_para.%s = %d\n",
					       sub_key, value);
				}
			}
		}
	}

	gdisp.screen[sel].tv_mode = DISP_TV_MOD_720P_50HZ;
	return DIS_SUCCESS;
}

__s32 Disp_TVEC_Exit(__u32 sel)
{
	TVE_exit(sel);
	tve_clk_exit(sel);

	return DIS_SUCCESS;
}

__s32 Disp_TVEC_Open(__u32 sel)
{
	TVE_open(sel);
	return DIS_SUCCESS;
}

__s32 Disp_TVEC_Close(__u32 sel)
{
	TVE_dac_disable(sel, 0);
	TVE_dac_disable(sel, 1);
	TVE_dac_disable(sel, 2);
	TVE_dac_disable(sel, 3);

	TVE_close(sel);

	return DIS_SUCCESS;
}

static void Disp_TVEC_DacCfg(__u32 sel, __u8 mode)
{
	__u32 i = 0;

	TVE_dac_disable(sel, 0);
	TVE_dac_disable(sel, 1);
	TVE_dac_disable(sel, 2);
	TVE_dac_disable(sel, 3);

	switch (mode) {
	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_NC:
		for (i = 0; i < 4; i++) {
			if (gdisp.screen[sel].dac_source[i] ==
			    DISP_TV_DAC_SRC_COMPOSITE) {
				TVE_dac_set_source(sel, i,
						   DISP_TV_DAC_SRC_COMPOSITE);
				TVE_dac_enable(sel, i);
				TVE_dac_sel(sel, i, i);
			}
		}
		break;

	case DISP_TV_MOD_NTSC_SVIDEO:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL_M_SVIDEO:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
		for (i = 0; i < 4; i++) {
			if (gdisp.screen[sel].dac_source[i] ==
			    DISP_TV_DAC_SRC_LUMA) {
				TVE_dac_set_source(sel, i,
						   DISP_TV_DAC_SRC_LUMA);
				TVE_dac_enable(sel, i);
				TVE_dac_sel(sel, i, i);
			} else if (gdisp.screen[sel].dac_source[i] ==
				   DISP_TV_DAC_SRC_CHROMA) {
				TVE_dac_set_source(sel, i,
						   DISP_TV_DAC_SRC_CHROMA);
				TVE_dac_enable(sel, i);
				TVE_dac_sel(sel, i, i);
			}
		}
		break;

	case DISP_TV_MOD_480I:
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_480P:
	case DISP_TV_MOD_576P:
	case DISP_TV_MOD_720P_50HZ:
	case DISP_TV_MOD_720P_60HZ:
	case DISP_TV_MOD_1080I_50HZ:
	case DISP_TV_MOD_1080I_60HZ:
	case DISP_TV_MOD_1080P_50HZ:
	case DISP_TV_MOD_1080P_60HZ:
		for (i = 0; i < 4; i++) {
			if (gdisp.screen[sel].dac_source[i] ==
			    DISP_TV_DAC_SRC_Y) {
				TVE_dac_set_source(sel, i,
						   DISP_TV_DAC_SRC_Y);
				TVE_dac_enable(sel, i);
				TVE_dac_sel(sel, i, i);
			} else if (gdisp.screen[sel].dac_source[i] ==
				   DISP_TV_DAC_SRC_PB) {
				TVE_dac_set_source(sel, i,
						   DISP_TV_DAC_SRC_PB);
				TVE_dac_enable(sel, i);
				TVE_dac_sel(sel, i, i);
			} else if (gdisp.screen[sel].dac_source[i] ==
				   DISP_TV_DAC_SRC_PR) {
				TVE_dac_set_source(sel, i,
						   DISP_TV_DAC_SRC_PR);
				TVE_dac_enable(sel, i);
				TVE_dac_sel(sel, i, i);
			} else if (gdisp.screen[sel].dac_source[i] ==
				   DISP_TV_DAC_SRC_COMPOSITE) {
				TVE_dac_set_source(1 - sel, i,
						   DISP_TV_DAC_SRC_COMPOSITE);
				TVE_dac_sel(1 - sel, i, i);
			}
		}
		break;

	default:
		break;
	}
}

__s32 BSP_disp_tv_open(__u32 sel)
{
	if (!(gdisp.screen[sel].status & TV_ON)) {
		__disp_tv_mode_t tv_mod;

		tv_mod = gdisp.screen[sel].tv_mode;

		image_clk_on(sel);
		/*
		 * set image normal channel start bit , because every
		 * de_clk_off( )will reset this bit
		 */
		Image_open(sel);

		disp_clk_cfg(sel, DISP_OUTPUT_TYPE_TV, tv_mod);
		tve_clk_on(sel);
		lcdc_clk_on(sel);

#ifdef CONFIG_ARCH_SUN4I
		BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_TV);
#else
		BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_TV,
					gdisp.screen[sel].
					iep_status & DRC_USED);
#endif
		DE_BE_set_display_size(sel, tv_mode_to_width(tv_mod),
				       tv_mode_to_height(tv_mod));
		DE_BE_Output_Select(sel, sel);

#ifdef CONFIG_ARCH_SUN5I
		DE_BE_Set_Outitl_enable(sel, Disp_get_screen_scan_mode(tv_mod));
		{
			int scaler_index;

			for (scaler_index = 0; scaler_index < 2; scaler_index++)
				if ((gdisp.scaler[scaler_index].
				     status & SCALER_USED)
				    && (gdisp.scaler[scaler_index].
					screen_index == sel)) {
					/* interlace output */
					if (Disp_get_screen_scan_mode(tv_mod) ==
					    1)
						Scaler_Set_Outitl(scaler_index,
								  TRUE);
					else
						Scaler_Set_Outitl(scaler_index,
								  FALSE);
				}
		}
#endif /* CONFIG_ARCH_SUN5I */

		TCON1_set_tv_mode(sel, tv_mod);
		TVE_set_tv_mode(sel, tv_mod);
		Disp_TVEC_DacCfg(sel, tv_mod);

		TCON1_open(sel);
		Disp_TVEC_Open(sel);

		Disp_Switch_Dram_Mode(DISP_OUTPUT_TYPE_TV, tv_mod);
#ifdef CONFIG_ARCH_SUN5I
		Disp_de_flicker_enable(sel, TRUE);
#endif
		{
			user_gpio_set_t gpio_info[1];
			__hdle gpio_pa_shutdown;
			__s32 ret;

			memset(gpio_info, 0, sizeof(user_gpio_set_t));
			ret =
			    script_parser_fetch("audio_para", "audio_pa_ctrl",
						(int *)gpio_info,
						sizeof(user_gpio_set_t) /
						sizeof(int));
			if (ret < 0) {
				DE_WRN("fetch script data "
				       "audio_para.audio_pa_ctrl fail\n");
			} else {
				gpio_pa_shutdown =
					OSAL_GPIO_Request(gpio_info, 1);
				if (!gpio_pa_shutdown) {
					DE_WRN("audio codec_wakeup request "
					       "gpio fail!\n");
				} else {
					OSAL_GPIO_DevWRITE_ONEPIN_DATA
						(gpio_pa_shutdown, 0,
						 "audio_pa_ctrl");
				}
			}
		}
		gdisp.screen[sel].b_out_interlace =
			Disp_get_screen_scan_mode(tv_mod);
		gdisp.screen[sel].status |= TV_ON;
		gdisp.screen[sel].lcdc_status |= LCDC_TCON1_USED;
		gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_TV;

#ifdef CONFIG_ARCH_SUN4I
		Disp_set_out_interlace(sel);
#endif

		Display_set_fb_timing(sel);
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_tv_close(__u32 sel)
{
	if (gdisp.screen[sel].status & TV_ON) {
		Image_close(sel);
		TCON1_close(sel);
		Disp_TVEC_Close(sel);

		tve_clk_off(sel);
		image_clk_off(sel);
		lcdc_clk_off(sel);

#ifdef CONFIG_ARCH_SUN5I
		/* must close immediately, because vbi may not come */
		Disp_de_flicker_enable(sel, 2);
		DE_BE_Set_Outitl_enable(sel, FALSE);
		{
			int scaler_index;

			for (scaler_index = 0; scaler_index < 2; scaler_index++)
				if ((gdisp.scaler[scaler_index].status &
				     SCALER_USED) &&
				    (gdisp.scaler[scaler_index].screen_index ==
				     sel))
					Scaler_Set_Outitl(scaler_index, FALSE);
		}
#endif /* CONFIG_ARCH_SUN5I */

		{
			user_gpio_set_t gpio_info[1];
			__hdle gpio_pa_shutdown;
			__s32 ret;

			memset(gpio_info, 0, sizeof(user_gpio_set_t));
			ret =
			    script_parser_fetch("audio_para", "audio_pa_ctrl",
						(int *)gpio_info,
						sizeof(user_gpio_set_t) /
						sizeof(int));
			if (ret < 0) {
				DE_WRN("fetch script data "
				       "audio_para.audio_pa_ctrl fail\n");
			} else {
				gpio_pa_shutdown =
					OSAL_GPIO_Request(gpio_info, 1);
				if (!gpio_pa_shutdown) {
					DE_WRN("audio codec_wakeup request "
					       "gpio fail!\n");
				} else {
					OSAL_GPIO_DevWRITE_ONEPIN_DATA
						(gpio_pa_shutdown, 1,
						 "audio_pa_ctrl");
				}
			}
		}

		gdisp.screen[sel].b_out_interlace = 0;
		gdisp.screen[sel].status &= ~TV_ON;
		gdisp.screen[sel].lcdc_status &= ~LCDC_TCON1_USED;
		gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_NONE;
		gdisp.screen[sel].pll_use_status &=
		    ((gdisp.screen[sel].pll_use_status == VIDEO_PLL0_USED) ?
		     ~VIDEO_PLL0_USED : ~VIDEO_PLL1_USED);

#ifdef CONFIG_ARCH_SUN4I
		Disp_set_out_interlace(sel);
#endif
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_tv_set_mode(__u32 sel, __disp_tv_mode_t tv_mod)
{
	if (tv_mod >= DISP_TV_MODE_NUM) {
		DE_WRN("unsupported tv mode:%d in BSP_disp_tv_set_mode\n",
		       tv_mod);
		return DIS_FAIL;
	}

	gdisp.screen[sel].tv_mode = tv_mod;
	gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_TV;
	return DIS_SUCCESS;
}

__s32 BSP_disp_tv_get_mode(__u32 sel)
{
	return gdisp.screen[sel].tv_mode;
}

__s32 BSP_disp_tv_get_interface(__u32 sel)
{
	__u8 dac[4] = { 0 };
	__s32 i = 0;
	__u32 ret = DISP_TV_NONE;

	for (i = 0; i < 4; i++) {
		dac[i] = TVE_get_dac_status(i);
		if (dac[i] > 1) {
			DE_WRN("dac %d short to ground\n", i);
			dac[i] = 0;
		}

		if ((gdisp.screen[sel].dac_source[i] ==
		     DISP_TV_DAC_SRC_COMPOSITE) && dac[i] == 1) {
			ret |= DISP_TV_CVBS;
		} else if ((gdisp.screen[sel].dac_source[i] ==
			    DISP_TV_DAC_SRC_Y) && dac[i] == 1) {
			ret |= DISP_TV_YPBPR;
		} else if ((gdisp.screen[sel].dac_source[i] ==
			    DISP_TV_DAC_SRC_LUMA) && dac[i] == 1) {
			ret |= DISP_TV_SVIDEO;
		}
	}

	return ret;
}

__s32 BSP_disp_tv_get_dac_status(__u32 sel, __u32 index)
{
	return TVE_get_dac_status(index);
}

__s32 BSP_disp_tv_set_dac_source(__u32 sel, __u32 index,
				 __disp_tv_dac_source source)
{
	gdisp.screen[sel].dac_source[index] = source;

	if (gdisp.screen[sel].status & TV_ON)
		Disp_TVEC_DacCfg(sel, gdisp.screen[sel].tv_mode);

	return 0;
}

__s32 BSP_disp_tv_get_dac_source(__u32 sel, __u32 index)
{
	return (__s32) gdisp.screen[sel].dac_source[index];
}

__s32 BSP_disp_tv_auto_check_enable(__u32 sel)
{
	TVE_dac_autocheck_enable(sel, 0);
	TVE_dac_autocheck_enable(sel, 1);
	TVE_dac_autocheck_enable(sel, 2);
	TVE_dac_autocheck_enable(sel, 3);

	return DIS_SUCCESS;
}

__s32 BSP_disp_tv_auto_check_disable(__u32 sel)
{
	TVE_dac_autocheck_disable(sel, 0);
	TVE_dac_autocheck_disable(sel, 1);
	TVE_dac_autocheck_disable(sel, 2);
	TVE_dac_autocheck_disable(sel, 3);

	return DIS_SUCCESS;
}

__s32 BSP_disp_tv_set_src(__u32 sel, __disp_lcdc_src_t src)
{
	switch (src) {
	case DISP_LCDC_SRC_DE_CH1:
		TCON1_select_src(sel, LCDC_SRC_DE1);
		break;

	case DISP_LCDC_SRC_DE_CH2:
		TCON1_select_src(sel, LCDC_SRC_DE2);
		break;

	case DISP_LCDC_SRC_BLUT:
		TCON1_select_src(sel, LCDC_SRC_BLUE);
		break;

	default:
		DE_WRN("not supported lcdc src:%d in BSP_disp_tv_set_src\n",
		       src);
		return DIS_NOT_SUPPORT;
	}
	return DIS_SUCCESS;
}
