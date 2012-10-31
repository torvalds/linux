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

#include "disp_iep.h"
#include "de_iep.h"
#include "de_iep_tab.h"
#include "disp_clk.h"
#include "disp_lcd.h"
#include "OSAL_Clock.h"

#ifndef CONFIG_ARCH_SUN5I
#error IEP should only be used on sun5i
#endif

static __hdle h_iepahbclk, h_iepdramclk, h_iepmclk;
static __disp_iep_t giep[2]; /* IEP module parameters */
static __disp_pwrsv_t gpwrsv[2]; /* Power Saving algorithm parameters */
static __u32 *pttab; /* POINTER of LGC tab */
static __u32 printf_cnt; /* for test */

/* power save core */
#define SCENE_CHNG_THR 45
/* enable detetion cause filcker in actual ic test 111230, so disable it. */
#define SCENE_CHANGE_DETECT_DISABLE 1

#define CLK_ON 1
#define CLK_OFF 0
#define RST_INVALID 0
#define RST_VALID 1

//#define DRC_DEFAULT_ENABLE /* Enable drc default */
//#define DRC_DEMO /* when defined DRC_DEFAULT_ENABLE, run DRC in DEMO mode */
/* when BSP_disp_lcd_get_bright() exceed PWRSAVE_PROC_THRES, STOP PWRSAVE. */
#define PWRSAVE_PROC_THRES 85

#define ____SEPARATOR_IEP_DRC_CORE____

/*
* PoWeRSAVE alg core
*
* Note: power save mode alg. Dynamic adjust backlight and lgc gain through
* screen content and user backlight setting.
*  - Add SCENE CHANGE DETECT.
*  - Add HANG-UP DETECT: When use PWRSAVE_CORE in LOW referential backlight
* condiction, backlight will flicker. So STOP use PWRSAVE_CORE.
*/
static inline __s32 PWRSAVE_CORE(__u32 sel)
{
	__u32 i;
	__u32 hist_region_num = 8;
	__u32 histcnt[IEP_LH_INTERVAL_NUM];
	__u32 hist[IEP_LH_INTERVAL_NUM], p95;
	__u32 size = 0;
	__u32 min_adj_index;
	__u32 lgcaddr;
	__u32 drc_filter_total = 0, drc_filter_tmp = 0;

	if (BSP_disp_lcd_get_bright(sel) < PWRSAVE_PROC_THRES) {
		memset(gpwrsv[sel].min_adj_index_hist, 255,
		       sizeof(__u8) * IEP_LH_PWRSV_NUM);
		lgcaddr = (__u32) pttab + ((128 - 1) << 9);
		lgcaddr = __pa(lgcaddr);
		/* set "gain=1" tab to lgc */
		DE_IEP_Drc_Set_Lgc_Addr(sel, lgcaddr);

		gdisp.screen[sel].lcd_bright_dimming = 256;
		BSP_disp_lcd_set_bright(sel, BSP_disp_lcd_get_bright(sel), 1);

	} else {
		p95 = 0;

		hist_region_num = (hist_region_num > 8) ?
			8 : IEP_LH_INTERVAL_NUM;

		/* read histogram result */
		DE_IEP_Lh_Get_Cnt_Rec(sel, histcnt);

		for (i = 0; i < IEP_LH_INTERVAL_NUM; i++)
			size += histcnt[i];

		size = (size == 0) ? 1 : size;

		/* calculate some var */
		hist[0] = (histcnt[0] * 100) / size;
		for (i = 1; i < hist_region_num; i++)
			hist[i] = (histcnt[i] * 100) / size + hist[i - 1];

		for (i = 0; i < hist_region_num; i++) {
			if (hist[i] >= 95) {
				p95 = hist_thres_pwrsv[i];
				break;
			}
		}

		/*
		 * sometime, hist[hist_region_num - 1] may less than 95 due to
		 * integer calc
		 */
		if (i == hist_region_num)
			p95 = hist_thres_pwrsv[7];

		min_adj_index = p95;

		//__inf("min_adj_index: %d\n", min_adj_index);

#if SCENE_CHANGE_DETECT_DISABLE
		for (i = 0; i < IEP_LH_PWRSV_NUM - 1; i++) {
			gpwrsv[sel].min_adj_index_hist[i] =
				gpwrsv[sel].min_adj_index_hist[i + 1];
		}
		gpwrsv[sel].min_adj_index_hist[IEP_LH_PWRSV_NUM - 1] =
			min_adj_index;

		for (i = 0; i < IEP_LH_PWRSV_NUM; i++) {
			drc_filter_total += drc_filter[i];
			drc_filter_tmp += drc_filter[i] *
				gpwrsv[sel].min_adj_index_hist[i];
		}
		min_adj_index = drc_filter_tmp / drc_filter_total;
#else
		/*
		 * ADD frame average alg
		 * SCENE CHANGE DETECT
		 */
		if (abs((__s32) min_adj_index -
			gpwrsv[sel].min_adj_index_hist[IEP_LH_PWRSV_NUM - 1]) >
		    SCENE_CHNG_THR) {
			memset(gpwrsv[sel].min_adj_index_hist, min_adj_index,
			       sizeof(__u8) * IEP_LH_PWRSV_NUM);
		} else {
			/* store new gain index, shift history data */
			for (i = 0; i < IEP_LH_PWRSV_NUM - 1; i++) {
				gpwrsv[sel].min_adj_index_hist[i] =
					gpwrsv[sel].min_adj_index_hist[i + 1];
			}
			gpwrsv[sel].min_adj_index_hist[IEP_LH_PWRSV_NUM - 1] =
				min_adj_index;

			for (i = 0; i < IEP_LH_PWRSV_NUM; i++) {
				drc_filter_total += drc_filter[i];
				drc_filter_tmp += drc_filter[i] *
					gpwrsv[sel].min_adj_index_hist[i];
			}
			min_adj_index = drc_filter_tmp / drc_filter_total;
		}

#endif

		min_adj_index = (min_adj_index >= 255) ?
			255 : ((min_adj_index < hist_thres_pwrsv[0]) ?
			       hist_thres_pwrsv[0] : min_adj_index);
		gdisp.screen[sel].lcd_bright_dimming = (min_adj_index + 1);

		BSP_disp_lcd_set_bright(sel, BSP_disp_lcd_get_bright(sel), 1);

		//lgcaddr = (__u32) pwrsv_lgc_tab[min_adj_index - 128];
		lgcaddr = (__u32) pttab + ((min_adj_index - 128) << 9);

		if (printf_cnt == 120) {
			__inf("save backlight power: %d percent\n",
			      (256 - (__u32) min_adj_index) * 100 / 256);
			printf_cnt = 0;
		} else {
			printf_cnt++;
		}

		/* virtual to physcal addr */
		lgcaddr = __pa(lgcaddr);

		DE_IEP_Drc_Set_Lgc_Addr(sel, lgcaddr);
	}

	return 0;
}

#define ____SEPARATOR_IEP_BSP____

/*
 *Enable / disable automatic backlight control function
 */
__s32 BSP_disp_iep_drc_enable(__u32 sel, __bool en)
{
	if (sel == 0) {
		if (en)
			gdisp.screen[sel].iep_status |= DRC_REQUIRED;
		else
			gdisp.screen[sel].iep_status &= ~DRC_REQUIRED;

		Disp_drc_enable(sel, en);
		return DIS_SUCCESS;
	} else {
		return DIS_NOT_SUPPORT;
	}

}

__s32 BSP_disp_iep_get_drc_enable(__u32 sel)
{
	__u32 ret;

	if (sel == 0) {
		if (gdisp.screen[sel].iep_status & DRC_USED) { /* used (ON) */
			ret = 1;
		} else if (!(gdisp.screen[sel].iep_status & DRC_USED) &&
			   (gdisp.screen[sel].iep_status & DRC_REQUIRED)) {
			/* required but not used(ON) */
			ret = 2;
		} else { /* not required and not used (OFF) */
			ret = 0;
		}

		return ret;
	} else {
		return DIS_NOT_SUPPORT;
	}
}

/*
 *On / Off go jitter function between the lines
 */
__s32 BSP_disp_iep_deflicker_enable(__u32 sel, __bool en)
{
	if (sel == 0) {
		if (en)
			gdisp.screen[sel].iep_status |= DE_FLICKER_REQUIRED;
		else
			gdisp.screen[sel].iep_status &= ~DE_FLICKER_REQUIRED;

		Disp_de_flicker_enable(sel, en);
		return DIS_SUCCESS;
	} else {
		return DIS_NOT_SUPPORT;
	}

}

__s32 BSP_disp_iep_get_deflicker_enable(__u32 sel)
{
	__u32 ret;

	if (sel == 0) {
		if (gdisp.screen[sel].iep_status & DE_FLICKER_USED)
			/* used (ON) */
			ret = 1;
		else if (!(gdisp.screen[sel].iep_status & DE_FLICKER_USED) &&
			   (gdisp.screen[sel].iep_status &
			    DE_FLICKER_REQUIRED))
			/* required but not used(ON) */
			ret = 2;
		else /* not required and not used (OFF) */
			ret = 0;

		return ret;
	} else {
		return DIS_NOT_SUPPORT;
	}
}

__s32 BSP_disp_iep_set_demo_win(__u32 sel, __u32 mode, __disp_rect_t *regn)
{
	__u32 scn_width, scn_height;

	if (regn == NULL) {
		DE_WRN("BSP_disp_iep_set_demo_win: parameters invalid!\n");
		return DIS_PARA_FAILED;
	}

	scn_width = BSP_disp_get_screen_width(sel);
	scn_height = BSP_disp_get_screen_height(sel);

	if ((regn->x < 0) || ((regn->x + regn->width) > scn_width) ||
	    (regn->y < 0) || ((regn->y + regn->height) > scn_height)) {
		DE_WRN("BSP_disp_iep_set_demo_win: win_x: %d, win_y: %d, "
		       "win_width: %d, win_height: %d.\n",
		       regn->x, regn->y, regn->width, regn->height);
		DE_WRN("IEP Windows Size Invalid!\n");
		return DIS_PARA_FAILED;
	}

	if (mode == 2) { /* drc */
		memcpy(&giep[sel].drc_win, regn, sizeof(__disp_rect_t));
		DE_INF("BSP_disp_iep_set_demo_win: drc window  win_x: %d, "
		       "win_y: %d, win_width: %d, win_height: %d.\n",
		       giep[sel].drc_win.x, giep[sel].drc_win.y,
		       giep[sel].drc_win.width, giep[sel].drc_win.height);
	} else if (mode == 1) { /* de-flicker */
		memcpy(&giep[sel].deflicker_win, regn, sizeof(__disp_rect_t));
		DE_INF("BSP_disp_iep_set_demo_win: drc window  win_x: %d, "
		       "win_y: %d, win_width: %d, win_height: %d.\n",
		       giep[sel].drc_win.x, giep[sel].drc_win.y,
		       giep[sel].drc_win.width, giep[sel].drc_win.height);
	}
	return DIS_SUCCESS;
}

#define ____SEPARATOR_IEP_MAIN_TASK____

/*
 * en : 0-close when vbi
 * en : 1- open when vbi
 * en : 2-close immediately
 */
__s32 Disp_drc_enable(__u32 sel, __u32 en)
{
	if (sel)
		return -1;

	switch (en) {
	case 0:
		if (gdisp.screen[sel].iep_status & DRC_USED)
			gdisp.screen[sel].iep_status |= DRC_NEED_CLOSED;
		else
			DE_INF("de: DRC hasn't opened yet !\n");
		break;

	case 1:
		if (gdisp.screen[sel].iep_status & DRC_REQUIRED) {
			if ((gdisp.screen[sel].output_type ==
			     DISP_OUTPUT_TYPE_LCD) &&
			    (gdisp.screen[sel].status & LCD_ON)) {
				if (!(gdisp.screen[sel].iep_status &
				      DRC_USED)) {
					Disp_drc_init(sel);
					gdisp.screen[sel].iep_status |=
						DRC_USED;
					DE_INF("de: DRC open now!\n");
				} else {
					DE_INF("de: DRC has already opened "
					       "before!\n");
				}
			} else {
				DE_INF("de: Will OPEN DRC when output to "
				       "LCD!\n");
			}
		} else {
			DE_INF("de: Run DISP_CMD_DRC_ON will open DRC!\n");
		}
		break;

	case 2:
		if (gdisp.screen[sel].iep_status & DRC_USED)
			Disp_drc_close_proc(sel, 0);
		else
			DE_INF("de: DRC hasn't opened yet !\n");

		break;
	}

	return 0;
}

__s32 Disp_drc_init(__u32 sel)
{
	__u32 scn_width, scn_height;

	scn_width = BSP_disp_get_screen_width(sel);
	scn_height = BSP_disp_get_screen_height(sel);

	if (sel == 0) {
		/* to prevent BE OUTCSC output YUV when IEP CSC not ready */
		//BSP_disp_cfg_start(sel);
		/* IEP clk */
		iep_clk_open(sel);

		/* another module */
		/* when running drc mode, debe must output yuv format */
#if 0
		DE_BE_Output_Cfg_Csc_Coeff(sel, DISP_OUTPUT_TYPE_TV, 0);
		BSP_disp_set_output_csc(sel, gdisp.screen[sel].output_type, 1);
#endif

		/* IEP module */
		DE_IEP_Set_Mode(sel, 2);
		DE_IEP_Set_Display_Size(sel, scn_width, scn_height);
		//DE_IEP_Set_Csc_Coeff(sel, 3);
		DE_IEP_Drc_Set_Spa_Coeff(sel, spatial_coeff);
		DE_IEP_Drc_Set_Int_Coeff(sel, intensity_coeff);

		/* default: no adjust */
		DE_IEP_Drc_Adjust_Enable(sel, 0);
		/* default: autoload enable */
		DE_IEP_Drc_Set_Lgc_Autoload_Disable(sel, 0);

		DE_IEP_Lh_Set_Mode(sel, 0); /* default: histogram normal mode */
		//DE_IEP_Lh_Set_Mode(sel, 1); /* histogram average mode */

		//DE_IEP_Lh_Set_Thres(sel, hist_thres_drc);
		DE_IEP_Lh_Set_Thres(sel, hist_thres_pwrsv);
		//gpwrsv[sel].user_bl = gdisp.screen[sel].lcd_bright;

		memset(gpwrsv[sel].min_adj_index_hist, 255,
		       sizeof(__u8) * IEP_LH_PWRSV_NUM);

		//giep[sel].drc_en = 1;
		giep[sel].drc_win_en = 1;
#if 0
		giep[sel].drc_win.x = 0;
		giep[sel].drc_win.y = 0;
		giep[sel].drc_win.width = scn_width;
		/*
		 * will clear when drc enable actually, but apps dont know when,
		 * so delete it.
		 */
		giep[sel].drc_win.height = scn_height;
#endif
		/* set 1 to make sure first frame wont get a random lgc table */
		giep[sel].waitframe = 1;
		giep[sel].runframe = 0;

		/* to prevent BE OUTCSC output YUV when IEP CSC not ready */
		//BSP_disp_cfg_finish(sel);

		return 0;
	} else {
		return -1;
	}
}

/*
 * en : 0-close when vbi
 * en : 1- open when vbi
 * en : 2-close immediately
 */
__s32 Disp_de_flicker_enable(__u32 sel, __u32 en)
{
	__disp_tv_mode_t tv_mode;
	__u32 scan_mode;
	__u32 scaler_index;

	tv_mode = gdisp.screen[sel].tv_mode;
	scan_mode = gdisp.screen[sel].b_out_interlace;

	if (sel)
		return -1;

	switch (en) {
	case 0:
		if (gdisp.screen[sel].iep_status & DE_FLICKER_USED) {
			BSP_disp_cfg_start(sel);

			for (scaler_index = 0; scaler_index < 2;
			     scaler_index++) {
				if ((gdisp.scaler[scaler_index].status &
				     SCALER_USED) &&
				    (gdisp.scaler[scaler_index].screen_index ==
				     sel)) {
					Scaler_Set_Outitl(scaler_index, TRUE);
					gdisp.scaler[scaler_index].b_reg_change
						= TRUE;
				}
			}

			/* must do in sun5i */
			DE_BE_Set_Outitl_enable(sel, TRUE);

			gdisp.screen[sel].iep_status &= DE_FLICKER_NEED_CLOSED;

			BSP_disp_cfg_finish(sel);
		} else {
			DE_INF("de: De-flicker hasn't opened yet!\n");
		}
		break;

	case 1:
		/* when set DISP_CMD_DE_FLICKER_ON before */
		if (gdisp.screen[sel].iep_status & DE_FLICKER_REQUIRED) {
			if ((gdisp.screen[sel].output_type ==
			     DISP_OUTPUT_TYPE_TV) && scan_mode &&
			    (gdisp.screen[sel].status & TV_ON))	{
				/* when interlaced tv on */
				if (!(gdisp.screen[sel].iep_status &
				      DE_FLICKER_USED)) {
					BSP_disp_cfg_start(sel);

					/* config defe to fit de-flicker */
					for (scaler_index = 0;
					     scaler_index < 2;
					     scaler_index++) {
						if ((gdisp.scaler[scaler_index].
						     status & SCALER_USED) &&
						    (gdisp.scaler[scaler_index].
						     screen_index == sel)) {
							Scaler_Set_Outitl
								(scaler_index,
								 FALSE);
							gdisp.scaler
								[scaler_index].
								b_reg_change
								= TRUE;
						}
					}

					/* config debe to fit de-flicker */
					/* must do in sun5i */
					DE_BE_Set_Outitl_enable(sel, FALSE);

					Disp_de_flicker_init(sel);
					gdisp.screen[sel].iep_status |=
						DE_FLICKER_USED;

					BSP_disp_cfg_finish(sel);
				} else {
					DE_INF("de: De-flicker has already "
					       "opened before !\n");
				}
			} else {
				DE_INF("de: Will OPEN de-flicker when output to"
				       " interlaced device !\n");
			}
		} else {
			DE_INF("de: Run DISP_CMD_DE_FLICKER_ON will open "
			       "de-flicker !\n");
		}
		break;

	case 2:
		if (gdisp.screen[sel].iep_status & DE_FLICKER_USED) {
			BSP_disp_cfg_start(sel);

			for (scaler_index = 0; scaler_index < 2;
			     scaler_index++) {
				if ((gdisp.scaler[scaler_index].status &
				     SCALER_USED) &&
				    (gdisp.scaler[scaler_index].screen_index ==
				     sel)) {
					Scaler_Set_Outitl(scaler_index, TRUE);
					gdisp.scaler[scaler_index].
						b_reg_change = TRUE;
				}
			}

			/* must do in sun5i */
			DE_BE_Set_Outitl_enable(sel, TRUE);

			Disp_de_flicker_close_proc(sel, 1);

			BSP_disp_cfg_finish(sel);
		} else {
			DE_INF("de: De-flicker hasn't opened yet!\n");
		}
		break;

	}

	return 0;
}

__s32 Disp_de_flicker_init(__u32 sel)
{
	__u32 scn_width, scn_height;

	scn_width = BSP_disp_get_screen_width(sel);
	scn_height = BSP_disp_get_screen_height(sel);

	if (sel == 0) {
		DE_IEP_Set_Mode(sel, 1);
		DE_IEP_Set_Display_Size(sel, scn_width, scn_height);
		DE_IEP_Set_Csc_Coeff(sel, 1);

		giep[sel].deflicker_win_en = 1;

		giep[sel].deflicker_win.x = 0;
		giep[sel].deflicker_win.y = 0;
		giep[sel].deflicker_win.width = scn_width;
		giep[sel].deflicker_win.height = scn_height;

		return 0;
	} else {
		return -1;
	}
}

#define ____SEPARATOR_IEP_CLK____

__s32 iep_clk_init(__u32 sel)
{
	h_iepahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_IEP);
	h_iepdramclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_SDRAM_IEP);
	h_iepmclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_IEP);

	OSAL_CCMU_MclkReset(h_iepmclk, RST_INVALID);
	OSAL_CCMU_MclkOnOff(h_iepahbclk, CLK_ON);

	g_clk_status |= CLK_IEP_AHB_ON;
	return DIS_SUCCESS;
}

__s32 iep_clk_exit(__u32 sel)
{
	OSAL_CCMU_MclkReset(h_iepmclk, RST_VALID);

	if (g_clk_status & CLK_IEP_DRAM_ON)
		OSAL_CCMU_MclkOnOff(h_iepdramclk, CLK_OFF);

	if (g_clk_status & CLK_IEP_MOD_ON)
		OSAL_CCMU_MclkOnOff(h_iepmclk, CLK_OFF);

	OSAL_CCMU_MclkOnOff(h_iepahbclk, CLK_OFF);

	OSAL_CCMU_CloseMclk(h_iepahbclk);
	OSAL_CCMU_CloseMclk(h_iepdramclk);
	OSAL_CCMU_CloseMclk(h_iepmclk);

	g_clk_status &= ~(CLK_IEP_AHB_ON | CLK_IEP_MOD_ON | CLK_IEP_DRAM_ON);
	return DIS_SUCCESS;
}

__s32 iep_clk_open(__u32 sel)
{
	OSAL_CCMU_MclkOnOff(h_iepmclk, CLK_ON);
	OSAL_CCMU_MclkOnOff(h_iepdramclk, CLK_ON);

	g_clk_status |= CLK_IEP_MOD_ON | CLK_IEP_DRAM_ON;
	return DIS_SUCCESS;
}

__s32 iep_clk_close(__u32 sel)
{
	OSAL_CCMU_MclkOnOff(h_iepmclk, CLK_OFF);
	OSAL_CCMU_MclkOnOff(h_iepdramclk, CLK_OFF);

	g_clk_status &= ~(CLK_IEP_MOD_ON | CLK_IEP_DRAM_ON);
	return DIS_SUCCESS;
}

#define ____SEPARATOR_IEP_INIT_EXIT____

__s32 Disp_iep_init(__u32 sel)
{

#ifdef DRC_DEFAULT_ENABLE
	__disp_rect_t regn;
#endif

	memset(giep, 0, sizeof(giep));
	memset(gpwrsv, 0, sizeof(gpwrsv));

	if (sel == 0) {
		iep_clk_init(sel);
		pttab = kmalloc(sizeof(pwrsv_lgc_tab), GFP_KERNEL | __GFP_ZERO);

		memcpy(pttab, pwrsv_lgc_tab, sizeof(pwrsv_lgc_tab));

#ifdef DRC_DEFAULT_ENABLE
#ifdef DRC_DEMO
		regn.x = BSP_disp_get_screen_width(sel) / 2;
		regn.y = 0;
		regn.width = BSP_disp_get_screen_width(sel) / 2;
		regn.height = BSP_disp_get_screen_height(sel);
#else
		regn.x = 0;
		regn.y = 0;
		regn.width = BSP_disp_get_screen_width(sel);
		regn.height = BSP_disp_get_screen_height(sel);
#endif
		BSP_disp_iep_drc_enable(sel, 1);
		BSP_disp_iep_set_demo_win(sel, 2, &regn);
#endif

		return 0;
	} else {
		return -1;
	}
}

__s32 Disp_iep_exit(__u32 sel)
{
	if (sel == 0) {
		iep_clk_exit(sel);
		kfree(pttab);
		return 0;
	} else {
		return -1;
	}
}

#define ____SEPARATOR_IEP_INT_PROC____

__s32 IEP_Operation_In_Vblanking(__u32 sel, __u32 tcon_index)
{
	/* if use DMA mode for LCD panel?? */
	if (gpanel_info[sel].tcon_index == tcon_index) {
		if (gdisp.screen[sel].iep_status & DRC_NEED_CLOSED)
			Disp_drc_close_proc(sel, tcon_index);
		else if (gdisp.screen[sel].iep_status & DRC_USED)
			Disp_drc_proc(sel, tcon_index);
	} else if (tcon_index == 1) {
		if (gdisp.screen[sel].iep_status & DE_FLICKER_NEED_CLOSED)
			Disp_de_flicker_close_proc(sel, tcon_index);
		else if (gdisp.screen[sel].iep_status & DE_FLICKER_USED)
			Disp_de_flicker_proc(sel, tcon_index);
	}

	return DIS_SUCCESS;
}

__s32 Disp_drc_proc(__u32 sel, __u32 tcon_index)
{
	__u32 top, bot, left, right;
	__u32 lgcaddr;

	if (sel == 0) {
		if (giep[sel].runframe < giep[sel].waitframe) {
			/*
			 * first  frame, wont get the valid histogram, so open
			 * a "zero" window
			 */
			top = 0;
			bot = 0;
			left = 0;
			right = 0;

			DE_IEP_Set_Demo_Win_Para(sel, top, bot, left, right);
			DE_IEP_Demo_Win_Enable(sel, 1);	/* enable here */
			/* 12-04-01 debug flicker in LCD opening */
			DE_IEP_Set_Csc_Coeff(sel, 3);
			BSP_disp_set_output_csc(sel,
						gdisp.screen[sel].output_type,
						1);

			lgcaddr = (__u32) pttab + ((128 - 1) << 9);
			lgcaddr = __pa(lgcaddr);
			/* set "gain=1" tab to lgc */
			DE_IEP_Drc_Set_Lgc_Addr(sel, lgcaddr);
			DE_IEP_Enable(sel); /* enable here */
#if 0
			DE_INF("waitting for runframe %d up to%d!\n",
			       giep.runframe, giep.waitframe);
#endif
			giep[sel].runframe++;
		} else {
			if (giep[sel].drc_win_en) {
				/* convert rectangle to register */
				top = giep[sel].drc_win.y;
				bot = giep[sel].drc_win.y +
					giep[sel].drc_win.height - 1;
				left = giep[sel].drc_win.x;
				right = giep[sel].drc_win.x +
					giep[sel].drc_win.width - 1;

				DE_IEP_Set_Demo_Win_Para(sel, top, bot, left,
							 right);
			}
			/* BACKLIGHT Control ALG */
			PWRSAVE_CORE(sel);
		}

		return 0;
	} else {
		return -1;
	}
}

__s32 Disp_drc_close_proc(__u32 sel, __u32 tcon_index)
{
	if (sel == 0) {
		/* IEP module */
		DE_IEP_Disable(sel);

		/* another module */
		BSP_disp_set_output_csc(sel, gdisp.screen[sel].output_type, 0);

		/* IEP clk */
		iep_clk_close(sel);

		gdisp.screen[sel].iep_status &= ~(DRC_USED | DRC_NEED_CLOSED);

		gdisp.screen[sel].lcd_bright_dimming = 256;
		BSP_disp_lcd_set_bright(sel, BSP_disp_lcd_get_bright(sel), 1);
		return 0;
	} else {
		return -1;
	}
}

__s32 Disp_de_flicker_proc(__u32 sel, __u32 tcon_index)
{
	__u32 top, bot, left, right;

	if (sel == 0) {
		if (giep[sel].deflicker_win_en) {
			top = giep[sel].deflicker_win.y;
			bot = giep[sel].deflicker_win.y +
				giep[sel].deflicker_win.height - 1;
			left = giep[sel].deflicker_win.x;
			right =  giep[sel].deflicker_win.x +
				giep[sel].deflicker_win.width - 1;

			DE_IEP_Set_Demo_Win_Para(sel, top, bot, left, right);
			DE_IEP_Demo_Win_Enable(sel, 1);
		}

		DE_IEP_Enable(sel);

		return 0;
	} else {
		return -1;
	}
}

__s32 Disp_de_flicker_close_proc(__u32 sel, __u32 tcon_index)
{
	if (sel == 0) {
		/* IEP module */
		DE_IEP_Disable(sel);

		/* IEP clk */
		iep_clk_close(sel);

		gdisp.screen[sel].iep_status &=
			~(DE_FLICKER_NEED_CLOSED | DE_FLICKER_USED);

		return 0;
	} else {
		return -1;
	}
}
