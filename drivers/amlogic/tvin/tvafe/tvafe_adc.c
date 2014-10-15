/*
 * TVAFE adc device driver.
 *
 * Copyright (c) 2010 Frank zhao <frank.zhao@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/******************************Includes************************************/
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/moduleparam.h>

#include <mach/am_regs.h>

#include <linux/amlogic/tvin/tvin.h>
#include "../vdin/vdin_regs.h"
#include "../tvin_global.h"
#include "../tvin_format_table.h"
#include "tvafe_regs.h"
#include "tvafe_adc.h"

/***************************Local defines**********************************/
/* adc auto adjust defines */
#define AUTO_CLK_VS_CNT                 10 //10 // get stable BD readings after n+1 frames
#define AUTO_PHASE_VS_CNT               2 // get stable AP readings after n+1 frames
#define ADC_WINDOW_H_OFFSET             39 // auto phase window h offset
#define ADC_WINDOW_V_OFFSET             2 // auto phase window v offset
#define MAX_AUTO_CLOCK_ORDER            4 // 1/16 headroom
#define VGA_AUTO_TRY_COUNTER            300 // vga max adjust counter, 3 seconds
// divide window into 7*7 sub-windows & make phase detection on 9 sub-windows
// -------
// -*-*-*-
// -------
// -*-*-*-
// -------
// -*-*-*-
// -------
#define VGA_AUTO_PHASE_H_WIN            7
#define VGA_AUTO_PHASE_V_WIN            7

#define VGA_PHASE_WIN_INDEX_0           0
#define VGA_PHASE_WIN_INDEX_1           1
#define VGA_PHASE_WIN_INDEX_2           2
#define VGA_PHASE_WIN_INDEX_3           3
#define VGA_PHASE_WIN_INDEX_4           4
#define VGA_PHASE_WIN_INDEX_5           5
#define VGA_PHASE_WIN_INDEX_6           6
#define VGA_PHASE_WIN_INDEX_7           7
#define VGA_PHASE_WIN_INDEX_8           8
#define VGA_PHASE_WIN_INDEX_MAX         VGA_PHASE_WIN_INDEX_8

#define VGA_ADC_PHASE_0                  0
#define VGA_ADC_PHASE_1                  1
#define VGA_ADC_PHASE_2                  2
#define VGA_ADC_PHASE_3                  3
#define VGA_ADC_PHASE_4                  4
#define VGA_ADC_PHASE_5                  5
#define VGA_ADC_PHASE_6                  6
#define VGA_ADC_PHASE_7                  7
#define VGA_ADC_PHASE_8                  8
#define VGA_ADC_PHASE_9                  9
#define VGA_ADC_PHASE_10                10
#define VGA_ADC_PHASE_11                11
#define VGA_ADC_PHASE_12                12
#define VGA_ADC_PHASE_13                13
#define VGA_ADC_PHASE_14                14
#define VGA_ADC_PHASE_15                15
#define VGA_ADC_PHASE_16                16
#define VGA_ADC_PHASE_17                17
#define VGA_ADC_PHASE_18                18
#define VGA_ADC_PHASE_19                19
#define VGA_ADC_PHASE_20                20
#define VGA_ADC_PHASE_21                21
#define VGA_ADC_PHASE_22                22
#define VGA_ADC_PHASE_23                23
#define VGA_ADC_PHASE_24                24
#define VGA_ADC_PHASE_25                25
#define VGA_ADC_PHASE_26                26
#define VGA_ADC_PHASE_27                27
#define VGA_ADC_PHASE_28                28
#define VGA_ADC_PHASE_29                29
#define VGA_ADC_PHASE_30                30
#define VGA_ADC_PHASE_31                31
#define VGA_ADC_PHASE_MID               VGA_ADC_PHASE_15
#define VGA_ADC_PHASE_MAX               VGA_ADC_PHASE_31

#define TVAFE_H_MAX                     0xFFF
#define TVAFE_H_MIN                     0x000
#define TVAFE_V_MAX                     0xFFF
#define TVAFE_V_MIN                     0x000

#define TVAFE_VGA_VS_CNT_MAX            200

#define TVAFE_VGA_BD_EN_DELAY           4  //4//4 field delay

#define TVAFE_VGA_CLK_TUNE_RANGE_ORDER  5 // 1/64 h_total
#define TVAFE_VGA_HPOS_TUNE_RANGE_ORDER 6 // 1/64 h_active
#define TVAFE_VGA_VPOS_TUNE_RANGE_ORDER 6 // 1/64 v_active

/* adc mode detection defines */
#define TVIN_FMT_CHG_VGA_VS_CNT_WOBBLE  2
#define TVIN_FMT_CHG_COMP_HS_CNT_WOBBLE 0xffffffff // not to trust
#define TVIN_FMT_CHG_COMP_VS_CNT_WOBBLE 0xffffffff // not to trust
#define TVIN_FMT_CHK_VGA_VS_CNT_WOBBLE  1   ///1
#define TVIN_FMT_CHK_HS_SOG_SW_CNT      5
#define TVIN_FMT_CHK_HS_SOG_DLY_CNT     3
#define TVIN_FMT_CHK_COMP_RST_MAX_CNT   100 /// the difference of two hcnt
#define TVAFE_ADC_RESET_MAX_CNT         3 // ADC reset max counter, avoid mode
// detection error sometimes for component
#define __BAR_DET_ORI 0
typedef enum  {
	BAR_INIT,
	BAR_PRE_DET,
	BAR_TUNE,
	BAR_SUCCESS,
	BAR_FAIL,
}bar_det_status_e;
typedef enum  {
	UNKNOWN,
	IS_BORDER,
	IS_BLACK,
	IS_VALID,
}black_det_e;
static bar_det_status_e b_status;

/***************************Local variables **********************************/
static DEFINE_SPINLOCK(skip_cnt_lock);
/*
*protection for vga vertical de adjustment,if vertical blanking too short
*mybe too short to process one field data
*/

static int vs_width_thr = 2;
module_param(vs_width_thr, int, 0664);
MODULE_PARM_DESC(vs_width_thr, "the mix lines after vsync");

static short vbp_offset = 15;
module_param(vbp_offset, short, 0664);
MODULE_PARM_DESC(vbp_offset, "the mix lines after vsync");

static bool adc_dbg_en = 0;
module_param(adc_dbg_en, bool, 0664);
MODULE_PARM_DESC(adc_dbg_en, "enable/disable adc auto adj debug message");

static bool adc_parm_en = 0;
module_param(adc_parm_en, bool, 0664);
MODULE_PARM_DESC(adc_parm_en, "enable/disable set adc parm message");
static bool adc_fmt_chg_dbg = 0;
module_param(adc_fmt_chg_dbg, bool, 0664);
MODULE_PARM_DESC(adc_fmt_chg_dbg, "enable/disable adc fmt change debug message");

static unsigned rgb_thr = 0x240;
module_param(rgb_thr, uint, 0664);
MODULE_PARM_DESC(rgb_thr, "border detect r&g&b threshold");

static unsigned white_thr_level = 0x36;
module_param(white_thr_level, uint, 0664);
MODULE_PARM_DESC(white_thr_level, "white_thr_level");
static bool enable_dphase = false;
module_param(enable_dphase, bool, 0644);
MODULE_PARM_DESC(enable_dphase,"turn on/off the different phase config");

static unsigned short comp_phase[TVIN_SIG_FMT_COMP_MAX - TVIN_SIG_FMT_VGA_THRESHOLD];
module_param_array(comp_phase, ushort, NULL, 0644);
MODULE_PARM_DESC(comp_phase,"the phase array for different comp fmt");

static unsigned short black_bar_v1 = 100;
module_param(black_bar_v1, ushort, 0644);
MODULE_PARM_DESC(black_bar_v1,"tell if the row is black");

static unsigned short black_bar_white = 5;
module_param(black_bar_white, ushort, 0644);
MODULE_PARM_DESC(black_bar_white,"tell if the row is black");

static unsigned short black_bar_v2 = 4;
module_param(black_bar_v2, ushort, 0644);
MODULE_PARM_DESC(black_bar_v2,"the if the row is bar");

static unsigned short bar_init_cnt = 20;
module_param(bar_init_cnt, ushort, 0644);
MODULE_PARM_DESC(bar_init_cnt,"init_cnt:wait for stabilization(unit:vs)");

static unsigned short bar_pre_det_cnt = 10;
module_param(bar_pre_det_cnt, ushort, 0644);
MODULE_PARM_DESC(bar_pre_det_cnt,"pre_det_cnt:Ensure correct(unit:vs)");

static unsigned short bar_tune_cnt = 3;
module_param(bar_tune_cnt, ushort, 0644);
MODULE_PARM_DESC(bar_tune_cnt,"tune_cnt:Ensure correct(unit:vs)");

static unsigned int bar_width = 4;
module_param(bar_width,int,0644);
MODULE_PARM_DESC(bar_width,"bar det: to decrease when after old border det");

static unsigned int bar_det_count = 15;
module_param(bar_det_count,int,0644);
MODULE_PARM_DESC(bar_det_count,"bar det:step number of bar det");

static unsigned int bar_det_timeout = 300;//vs_cnt
module_param(bar_det_timeout,int,0644);
MODULE_PARM_DESC(bar_det_time,"bar det time:time limit of bar det");

static unsigned int bar_det_delta = 5;
module_param(bar_det_delta,int,0644);
MODULE_PARM_DESC(bar_det_time,"bar det delta:delta limit of bar det");


/*****************************the  version of changing log************************/
static char last_version_s[]="2013-11-4||10-13";
static char version_s[] = "2013-11-4||10-13";
/***************************************************************************/
void get_adc_version(char **ver,char **last_ver)
{
	*ver=version_s;
	*last_ver=last_version_s;
	return ;
}


/*
 * tvafe get adc pll lock status
 */
inline bool tvafe_adc_get_pll_status(void)
{
	return (bool)READ_APB_REG_BITS(ADC_REG_35, PLLLOCKED_BIT, PLLLOCKED_WID);
}

/*
 * tvafe skip frame counter
 */
static void tvafe_adc_set_frame_skip_number(struct tvafe_adc_s *adc, unsigned int frame_number)
{
	unsigned long flags;
	spin_lock_irqsave(&skip_cnt_lock, flags);
	if (adc->skip_frame_cnt < frame_number) {
		adc->skip_frame_cnt = frame_number;
	}
	spin_unlock_irqrestore(&skip_cnt_lock, flags);
}

/*
 * tvafe sikp frame function
 */
bool tvafe_adc_check_frame_skip(struct tvafe_adc_s *adc)
{
	bool ret = false;
//	unsigned long flags;
	//spin_lock_irqsave(&skip_cnt_lock, flags);
	if (adc->skip_frame_cnt > 0) {
		adc->skip_frame_cnt--;
		ret = true;
	}
	else {
		ret = false;
	}
	//spin_unlock_irqrestore(&skip_cnt_lock, flags);

	return ret;
}


/*
 * tvafe set adc clamp parameters
 */
static void tvafe_adc_set_clamp_para(enum tvin_sig_fmt_e fmt)
{
	int clamp_calculate = 0, clamp_range = 0;
	const struct tvin_format_s *fmt_info = tvin_get_fmt_info(fmt);

    if(!fmt_info) {
        pr_err("[tvafe..] %s: error,fmt is null!!! \n",__func__);
        return;
    }

	clamp_range = fmt_info->hs_bp - READ_APB_REG(ADC_REG_03) - 18;
	clamp_calculate = fmt_info->pixel_clk / 200;
	if (clamp_calculate >= 0xff)
		clamp_calculate = 0xff;
	else if (clamp_calculate > clamp_range)
		clamp_calculate = clamp_range;
	WRITE_APB_REG(ADC_REG_04, clamp_calculate);
}

/*
 * tvafe check adc signal status
 */
inline bool tvafe_adc_no_sig(void)
{
	return (READ_APB_REG_BITS(TVFE_DVSS_INDICATOR1, NOSIG_BIT, NOSIG_WID) ? true : false);
}
#if 0
/*
 * tvafe set hsync path: adc to top
 */
static void tvafe_comp_set_sync_path(int sog_flag)
{
	WRITE_APB_REG_BITS(TVFE_SYNCTOP_SFG_MUXCTRL2, (sog_flag? 2:4),
			SMUX_SM_HS_SRC_SEL_BIT, SMUX_SM_HS_SRC_SEL_WID);
}

/*
 * tvafe get hsync path setting: adc to top
 */
static int tvafe_comp_get_sync_path(void)
{
	return ((READ_APB_REG_BITS(TVFE_SYNCTOP_SFG_MUXCTRL2,
					SMUX_SM_HS_SRC_SEL_BIT, SMUX_SM_HS_SRC_SEL_WID)==2)? 1:0);
}
#endif
/*
 * tvafe check mode change status
 */
inline bool tvafe_adc_fmt_chg(struct tvin_parm_s *parm, struct tvafe_adc_s *adc)
{
        const struct tvin_format_s *fmt_info_p;
	enum tvin_sig_fmt_e fmt = parm->info.fmt;
	enum tvin_port_e port = parm->port;
	enum tvin_sig_status_e status = parm->info.status;
	struct tvin_format_s *hw_info = &adc->hw_info;
	unsigned short tmp0 = 0, tmp1 = 0;
	unsigned int   h_cnt_offset = 0, h_cnt_offset1 =0, v_cnt_offset = 0, v_cnt_offset1 = 0;
	unsigned int   hs_cnt_offset = 0, vs_cnt_offset = 0;
	unsigned int   h_cnt_wobble = 0, v_cnt_wobble = 0;
	unsigned int   hs_cnt_wobble = 0, vs_cnt_wobble = 0, flag;
	bool           h_pol_chg = false, v_pol_chg = false;
	bool           h_flag = false, v_flag = false;

	flag = READ_APB_REG_BITS(TVFE_DVSS_INDICATOR1, NOSIG_BIT, NOSIG_WID);
	if (flag)
	{
		if (adc_dbg_en)
			pr_info("[tvafe..] %s: tvafe adc no signal!!! \n",__func__);
		return true;
	}

        fmt_info_p = tvin_get_fmt_info(fmt);
        if(fmt_info_p){
                h_cnt_wobble  = fmt_info_p->h_cnt_offset;
	        v_cnt_wobble  = fmt_info_p->v_cnt_offset;
	        hs_cnt_wobble = fmt_info_p->hs_cnt_offset;
        }else{
                h_cnt_wobble  = 10;
	        v_cnt_wobble  = 10;
                hs_cnt_wobble = 2;
        }

	if ((port >= TVIN_PORT_VGA0) && (port <= TVIN_PORT_VGA7))
	{
		vs_cnt_wobble = TVIN_FMT_CHG_VGA_VS_CNT_WOBBLE;
		flag = READ_APB_REG(TVFE_SYNCTOP_INDICATOR3);

		//h_pol
		h_flag = (flag & (1 << SPOL_H_POL_BIT))? true : false;

		//v_pol
		v_flag = (flag & (1 << SPOL_V_POL_BIT))? true : false;

		if (h_flag)
		{
			if (hw_info->hs_pol == TVIN_SYNC_POL_POSITIVE)
				h_pol_chg = true;
			hw_info->hs_pol = TVIN_SYNC_POL_NEGATIVE;
		}
		else
		{
			if (hw_info->hs_pol == TVIN_SYNC_POL_NEGATIVE)
				h_pol_chg = true;
			hw_info->hs_pol = TVIN_SYNC_POL_POSITIVE;
		}

		if (v_flag)
		{
			if (hw_info->vs_pol == TVIN_SYNC_POL_POSITIVE)
				v_pol_chg = true;
			hw_info->vs_pol = TVIN_SYNC_POL_NEGATIVE;
		}
		else
		{
			if (hw_info->vs_pol == TVIN_SYNC_POL_NEGATIVE)
				v_pol_chg = true;
			hw_info->vs_pol = TVIN_SYNC_POL_POSITIVE;
		}

		// hs_cnt
		tmp0 = (unsigned short)READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR1_HCNT,
				SPOL_HCNT_NEG_BIT, SPOL_HCNT_NEG_WID);
		tmp1 = (unsigned short)READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR1_HCNT,
				SPOL_HCNT_POS_BIT, SPOL_HCNT_POS_WID);
		tmp0 = min(tmp0, tmp1);
		hs_cnt_offset = abs((signed int)hw_info->hs_cnt - (signed int)tmp0);
		hw_info->hs_cnt = tmp0;

		// vs_cnt
		tmp0 = (unsigned short)READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR2_VCNT,
				SPOL_VCNT_NEG_BIT, SPOL_VCNT_NEG_WID);
		tmp1 = (unsigned short)READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR2_VCNT,
				SPOL_VCNT_POS_BIT, SPOL_VCNT_POS_WID);
		tmp0 = min(tmp0, tmp1);
		vs_cnt_offset = abs((signed int)hw_info->vs_width - (signed int)tmp0);
		hw_info->vs_width = tmp0;
		// h_cnt
		tmp0 = READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR4,
				SAM_HCNT_BIT, SAM_HCNT_WID);
		h_cnt_offset = abs((signed int)hw_info->h_cnt - (signed int)tmp0);
		//h_cnt_offset = abs((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_cnt - (signed int)tmp0);
		hw_info->h_cnt = tmp0;
		// v_cnt
		tmp0 = READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR4,
				SAM_VCNT_BIT, SAM_VCNT_WID);
		v_cnt_offset = abs((signed int)hw_info->v_total - (signed int)tmp0);
		//v_cnt_offset = abs((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_total- (signed int)tmp0);
		hw_info->v_total = tmp0;

	}
	else if ((port >= TVIN_PORT_COMP0) && (port <= TVIN_PORT_COMP7))
	{
		hs_cnt_wobble = TVIN_FMT_CHG_COMP_HS_CNT_WOBBLE;
		vs_cnt_wobble = TVIN_FMT_CHG_COMP_VS_CNT_WOBBLE;
		adc->hs_sog_sw_cnt++;

		if(adc->hs_sog_sw_cnt > TVIN_FMT_CHK_HS_SOG_DLY_CNT)
		{
			// h_cnt
			tmp0 = READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR4,
					SAM_HCNT_BIT, SAM_HCNT_WID);

			if ((status == TVIN_SIG_STATUS_STABLE)&&(fmt_info_p))
				hw_info->h_cnt = fmt_info_p->h_cnt;
			h_cnt_offset = abs((signed int)hw_info->h_cnt - (signed int)tmp0);

			adc->hs_sog_sw_cnt = 0;
			tmp1 = READ_APB_REG_BITS(TVFE_SOG_MON_INDICATOR1,
					SOG_CNT_POS_BIT, SOG_CNT_POS_WID);
			tmp1 += READ_APB_REG_BITS(TVFE_SOG_MON_INDICATOR1,
					SOG_CNT_NEG_BIT, SOG_CNT_NEG_WID);
			//the difference between h_cnt  from sog and h_cnt from hs_out2
			h_cnt_offset1 = abs((signed int)hw_info->h_cnt - (signed int)tmp1);
                        hw_info->h_cnt = tmp0;
                        if(h_cnt_offset < h_cnt_offset1){
                                h_cnt_offset = h_cnt_offset1;
                                hw_info->h_cnt = tmp1;
                                if(adc_fmt_chg_dbg)
                                        pr_info("[tvafe..] %s:sogout hcnt %u, hsyncout hcnt %u.\n",__func__,tmp1,tmp0);
                        }

			//pr_info("[tvafe..] %s: hw:%d, hw.hcnt:%d fmt is:%s\n",__func__, tmp0,
			//    hw_info->h_cnt, tvin_sig_fmt_str(fmt));
			/* if the diff exceed max cnt, reset adc for abnormal format */
			if (h_cnt_offset > TVIN_FMT_CHK_COMP_RST_MAX_CNT)
			{
#if 0
				/* avoid mode detection error for component */
				if (devp->adc.adc_reset_cnt++ < TVAFE_ADC_RESET_MAX_CNT)
				{
					tvafe_adc_digital_reset();
					if (adc_dbg_en)
						pr_info("[tvafe..] %s: fmt change!! adc reset clock!!! \n",__func__);
				}
				else
					devp->adc.adc_reset_cnt = TVAFE_ADC_RESET_MAX_CNT;
#else
				tvafe_adc_digital_reset();
				if (adc_dbg_en)
					pr_info("[tvafe..] %s: fmt change!! adc reset clock!!! \n",__func__);
#endif
			}

			// v_cnt
			tmp0 = READ_APB_REG_BITS(TVFE_SYNCTOP_INDICATOR4,SAM_VCNT_BIT, SAM_VCNT_WID);
			v_cnt_offset = abs((signed int)hw_info->v_total - (signed int)tmp0);
                        hw_info->v_total = tmp0;

			if ((status == TVIN_SIG_STATUS_STABLE)&&(fmt_info_p)){
				hw_info->v_total = fmt_info_p->v_total;
				tmp1 = READ_APB_REG_BITS(TVFE_SOG_MON_INDICATOR2,SOG_VTOTAL_BIT, SOG_VTOTAL_WID);
                                /*during vsync sog cnt is double of vsync width*/
                                tmp1 -= hw_info->vs_width;
				v_cnt_offset1= abs((signed int)hw_info->v_total - (signed int)tmp1);
				//chose the correct v_total from sog and v_hs
				if(v_cnt_offset < v_cnt_offset1){
					hw_info->v_total = tmp1;
					v_cnt_offset = v_cnt_offset1;
                                        if(adc_fmt_chg_dbg)
                                        pr_info("[tvafe..] %s:sogout vcnt %u, vsyncout vcnt %u.\n",__func__,tmp1,tmp0);

				}

		        }
	        }
	}
	else
	{
		if (adc_dbg_en)
			pr_err("[tvafe..] wrong input port. \n");
		return false;
	}

	if ((h_cnt_offset > h_cnt_wobble)   ||
			(v_cnt_offset > v_cnt_wobble)   ||
			(hs_cnt_offset > hs_cnt_wobble) ||
			(vs_cnt_offset > vs_cnt_wobble) ||
			h_pol_chg                       ||
			v_pol_chg
	   )
	{
		flag = true;
		if(adc_fmt_chg_dbg)
		{
			if(h_cnt_offset > h_cnt_wobble)
				pr_info("[tvafe..] h_cnt_offset %u > %u h_cnt_wobble fmt change.\n",h_cnt_offset,h_cnt_wobble);
			if(v_cnt_offset > v_cnt_wobble)
				pr_info("[tvafe..] v_cnt_offset %u > %u v_cnt_wobble fmt change.\n",v_cnt_offset,v_cnt_wobble);
			if(hs_cnt_offset > hs_cnt_wobble)
				pr_info("[tvafe..] hs_cnt_offset %u.hs_cnt_offset > hs_cnt_wobble fmt change.\n",hs_cnt_offset);
			if(vs_cnt_offset > vs_cnt_wobble)
				pr_info("[tvafe..] vs_cnt_offset %u > %u vs_cnt_wobble fmt change.\n",vs_cnt_offset,vs_cnt_wobble);
			if(h_pol_chg)
				pr_info("[tvafe..] h_pol_chg fmt change.\n");
			if(v_pol_chg)
				pr_info("[tvafe..] v_pol_chg fmt change.\n");
		}
	}
	else
		flag = false;


	return flag;
}

/*
 * tvafe reset digital module of adc
 */
void tvafe_adc_digital_reset()
{
	WRITE_APB_REG(ADC_REG_21, 1);
	WRITE_APB_REG(ADC_REG_21, 5);
	WRITE_APB_REG(ADC_REG_21, 7);

	//tvafe_adc_set_frame_skip_number(3);
}

/*
 * tvafe search format number
 */
inline enum tvin_sig_fmt_e tvafe_adc_search_mode(struct tvin_parm_s *parm, struct tvafe_adc_s *adc)
{
	enum tvin_port_e port = parm->port;
	struct tvin_format_s *hw_info = &adc->hw_info;
	enum tvin_sig_fmt_e index     = TVIN_SIG_FMT_NULL;
	enum tvin_sig_fmt_e index_min = TVIN_SIG_FMT_NULL;
	enum tvin_sig_fmt_e index_max = TVIN_SIG_FMT_NULL;
	//unsigned int hcnt = 0;

	if ((port >= TVIN_PORT_VGA0) && (port <= TVIN_PORT_VGA7))
	{
		index_min = TVIN_SIG_FMT_VGA_512X384P_60HZ_D147;
		index_max = TVIN_SIG_FMT_VGA_MAX;
		for (index=index_min; index < index_max; index++)
		{
			if (tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_cnt == 0)
				continue;
			if (abs((signed int)hw_info->h_cnt- (signed int)tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_cnt) <= tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_cnt_offset)
			{
				if(abs((signed int)hw_info->v_total- (signed int)tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_total) <= tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_cnt_offset)
				{
					if(abs((signed int)hw_info->hs_cnt - (signed int)tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_cnt) <= tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_cnt_offset)
					{
						if(abs((signed int)hw_info->vs_width - (signed int)tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width) <= vs_width_thr)
						{
							if((hw_info->hs_pol == tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_pol) &&
									(hw_info->vs_pol == tvin_vga_fmt_tbl[index-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_pol))
								break;
						}
					}
				}
			}
		}

	}
	else if((port >= TVIN_PORT_COMP0) && (port <= TVIN_PORT_COMP7))
	{
		index_min = TVIN_SIG_FMT_COMP_480P_60HZ_D000;
		index_max = TVIN_SIG_FMT_COMP_MAX;
		for (index=index_min; index < index_max; index++)
		{
			if (tvin_comp_fmt_tbl[index-TVIN_SIG_FMT_COMP_480P_60HZ_D000].h_cnt == 0)
				continue;
			if (abs((signed int)hw_info->h_cnt- (signed int)tvin_comp_fmt_tbl[index-TVIN_SIG_FMT_COMP_480P_60HZ_D000].h_cnt) <= tvin_comp_fmt_tbl[index-TVIN_SIG_FMT_COMP_480P_60HZ_D000].h_cnt_offset)
			{
				if(abs((signed int)hw_info->v_total- (signed int)tvin_comp_fmt_tbl[index-TVIN_SIG_FMT_COMP_480P_60HZ_D000].v_total) <= tvin_comp_fmt_tbl[index-TVIN_SIG_FMT_COMP_480P_60HZ_D000].v_cnt_offset)
				{
					break;
				}
			}
		}

	}

	if (adc_dbg_en)
		pr_info("[tvafe..] %s h_cnt= %d, v_total= %d, hs_cnt=%d, vs_width= %d, hs_pol= %d, vs_pol= %d \n", \
				__func__, hw_info->h_cnt, hw_info->v_total, hw_info->hs_cnt, \
				hw_info->vs_width, hw_info->hs_pol, hw_info->vs_pol);
	if (index >= index_max)
	{
		index = TVIN_SIG_FMT_NULL;
	}

	adc->adc_reset_cnt = 0;

	return index;
}

/*
 * tvafe set border detection window size
 */
static void tvafe_adc_set_bd_window(enum tvin_sig_fmt_e fmt)
{
	unsigned int tmp = 0;

	tmp = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_bp - ADC_WINDOW_H_OFFSET;
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL1, tmp, BD_HSTART_BIT, BD_HSTART_WID);
	//tmp += tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active + ADC_WINDOW_H_OFFSET + ADC_WINDOW_H_OFFSET;
	tmp = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1;
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL1, tmp, BD_HEND_BIT, BD_HEND_WID);
	tmp = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_bp - ADC_WINDOW_V_OFFSET;
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL2, tmp, BD_VSTART_BIT, BD_VSTART_WID);
	//tmp += tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active + ADC_WINDOW_V_OFFSET + ADC_WINDOW_V_OFFSET;
	tmp = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_total - 1;
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL2, tmp, BD_VEND_BIT, BD_VEND_WID);
}

/*
 * tvafe set auto phase detection window size
 */
static void tvafe_adc_set_ap_window(enum tvin_sig_fmt_e fmt, unsigned char idx)
{
	unsigned int hh = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active / VGA_AUTO_PHASE_H_WIN;
	unsigned int vv = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active / VGA_AUTO_PHASE_V_WIN;
	unsigned int hs = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width +
		tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_bp    +
		(((idx%3) << 1) + 1)*hh;
	unsigned int he = hs + hh - 1;
	unsigned int vs = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width +
		tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_bp    +
		(((idx/3) << 1) + 1)*vv;
	unsigned int ve = vs + vv - 1;

	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, hs, AP_HSTART_BIT, AP_HSTART_WID);
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, he, AP_HEND_BIT,   AP_HEND_WID  );
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL2, vs, AP_VSTART_BIT, AP_VSTART_WID);
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL2, ve, AP_VEND_BIT,   AP_VEND_WID  );
}

/*
 * tvafe set adc clock parameter
 */
static void tvafe_vga_set_clock(unsigned int clock)
{
	unsigned int tmp;

	tmp = (clock >> 4) & 0x000000FF;
	WRITE_APB_REG_BITS(ADC_REG_01, tmp, PLLDIVRATIO_MSB_BIT, PLLDIVRATIO_MSB_WID);
	tmp = clock & 0x0000000F;
	WRITE_APB_REG_BITS(ADC_REG_02, tmp, PLLDIVRATIO_LSB_BIT, PLLDIVRATIO_LSB_WID);

	//tvafe_adc_set_frame_skip_number(2);

	//reset adc digital pll
	//tvafe_adc_digital_reset();

	return;
}

/*
 * tvafe get adc clock parameter
 */
static unsigned int tvafe_vga_get_clock(void)
{
	unsigned int data;

	data = READ_APB_REG_BITS(ADC_REG_01,
			PLLDIVRATIO_MSB_BIT, PLLDIVRATIO_MSB_WID) << 4;
	data |= READ_APB_REG_BITS(ADC_REG_02,
			PLLDIVRATIO_LSB_BIT, PLLDIVRATIO_LSB_WID);

	return data;
}

/*
 * tvafe set adc phase parameter
 */
static void tvafe_vga_set_phase(unsigned int phase)
{
	WRITE_APB_REG_BITS(ADC_REG_56, phase, CLKPHASEADJ_BIT, CLKPHASEADJ_WID);

	//tvafe_adc_set_frame_skip_number(1);

	//reset adc digital pll
	//tvafe_adc_digital_reset();  //removed for auto phase bug
	return;
}

/*
 * tvafe get adc phase parameter
 */
static unsigned int tvafe_vga_get_phase(void)
{
	return READ_APB_REG_BITS(ADC_REG_56, CLKPHASEADJ_BIT, CLKPHASEADJ_WID);
}

/*
 * tvafe set adc h-position parameter
 */
void tvafe_vga_set_h_pos(unsigned int hs, unsigned int he)
{
	WRITE_APB_REG_BITS(TVFE_DEG_H,   hs, DEG_HSTART_BIT, DEG_HSTART_WID);
	WRITE_APB_REG_BITS(TVFE_DEG_H,   he, DEG_HEND_BIT,   DEG_HEND_WID  );

	return;
}

/*
 * tvafe get adc h-position parameter
 */
static unsigned int tvafe_vga_get_h_pos(void)
{
	return READ_APB_REG_BITS(TVFE_DEG_H, DEG_HSTART_BIT, DEG_HSTART_WID);
}

/*
 * tvafe set adc v-position parameter
 */
void tvafe_vga_set_v_pos(unsigned int vs, unsigned int ve, enum tvin_scan_mode_e scan_mode)
{
	unsigned int offset = (scan_mode == TVIN_SCAN_MODE_PROGRESSIVE) ? 0 : 1;
	WRITE_APB_REG_BITS(TVFE_DEG_VODD,  vs,          DEG_VSTART_ODD_BIT,  DEG_VSTART_ODD_WID );
	WRITE_APB_REG_BITS(TVFE_DEG_VODD ,  ve,          DEG_VEND_ODD_BIT,    DEG_VEND_ODD_WID   );
	WRITE_APB_REG_BITS(TVFE_DEG_VEVEN, vs + offset, DEG_VSTART_EVEN_BIT, DEG_VSTART_EVEN_WID);
	WRITE_APB_REG_BITS(TVFE_DEG_VEVEN, ve + offset, DEG_VEND_EVEN_BIT,   DEG_VEND_EVEN_WID  );
}

/*
 * tvafe get adc v-position parameter
 */
static unsigned int tvafe_vga_get_v_pos(void)
{
	return READ_APB_REG_BITS(TVFE_DEG_VODD, DEG_VSTART_ODD_BIT, DEG_VSTART_ODD_WID);
}

/*
 * tvafe enable border detection function
 */
static void tvafe_vga_border_detect_enable(void)
{
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 1, BD_DET_EN_BIT, BD_DET_EN_WID);
}

/*
 * tvafe disenable border detection function
 */
static void tvafe_vga_border_detect_disable(void)
{
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 0, BD_DET_EN_BIT, BD_DET_EN_WID);
}

/*
 * tvafe enable auto phase function
 */
static void tvafe_vga_auto_phase_enable(void)
{
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 1, AUTOPHASE_EN_BIT, AUTOPHASE_EN_WID);
}

/*
 * tvafe disenable auto phase function
 */
static void tvafe_vga_auto_phase_disable(void)
{
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 0, AUTOPHASE_EN_BIT, AUTOPHASE_EN_WID);
}

/*
 * tvafe init border detection function
 */
static void tvafe_vga_border_detect_init(enum tvin_sig_fmt_e fmt)
{
	//diable border detect
	tvafe_vga_border_detect_disable();
	// pix_thr = 4 (pix-val > pix_thr => valid pixel)
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL3, rgb_thr/*0x10*/,
			BD_R_TH_BIT, BD_R_TH_WID);
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL5, rgb_thr/*0x10*/,
			BD_G_TH_BIT, BD_G_TH_WID);
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL5, rgb_thr/*0x10*/,
			BD_B_TH_BIT, BD_B_TH_WID);
	// pix_val > pix_thr => valid pixel
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 1,
			BD_DET_METHOD_BIT, BD_DET_METHOD_WID);
	// line_thr = 1/16 of h_active (valid pixels > line_thr => valid line)
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL3, (tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active)>>5/*(tvin_fmt_tbl[fmt].h_active)>>4*/,
			BD_VLD_LN_TH_BIT, BD_VLD_LN_TH_WID);
	// line_thr enable
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL3, 1,
			BD_VALID_LN_EN_BIT, BD_VALID_LN_EN_WID);
	// continuous border detection mode
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL4, 1,
			BD_LIMITED_FLD_RECORD_BIT, BD_LIMITED_FLD_RECORD_WID);
	WRITE_APB_REG_BITS(TVFE_BD_MUXCTRL4, 2,
			BD_FLD_CD_NUM_BIT, BD_FLD_CD_NUM_WID);
	// set a large window
	tvafe_adc_set_bd_window( fmt);
	//enable border detect
	tvafe_vga_border_detect_enable();
}

/*
 * tvafe init auto phase function
 */
static void tvafe_vga_auto_phase_init( enum tvin_sig_fmt_e fmt, unsigned char idx)
{
	//disable auto phase
	tvafe_vga_auto_phase_disable();
	// use diff value
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 1,
			AP_DIFF_SEL_BIT, AP_DIFF_SEL_WID);
	// use window
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL1, 0,
			AP_SPECIFIC_POINT_OUT_BIT, AP_SPECIFIC_POINT_OUT_WID);
	// coring_thr = 4 (diff > coring_thr => valid diff)
	WRITE_APB_REG_BITS(TVFE_AP_MUXCTRL3, 0x10,
			AP_CORING_TH_BIT, AP_CORING_TH_WID);
	// set auto phase window
	tvafe_adc_set_ap_window(fmt, idx);
	//enable auto phase
	tvafe_vga_auto_phase_enable();
}

/*
 * tvafe get the diff value of auto phase
 */
static unsigned int tvafe_vga_get_ap_diff(void)
{

	unsigned int sum_r = READ_APB_REG(TVFE_AP_INDICATOR1);
	unsigned int sum_g = READ_APB_REG(TVFE_AP_INDICATOR2);
	unsigned int sum_b = READ_APB_REG(TVFE_AP_INDICATOR3);

	if (sum_r < sum_g)
		return max(sum_g,sum_b);
	else
		return max(sum_r,sum_b);
}

/*
 * tvafe get h border value after border detection
 */
static void tvafe_vga_get_h_border(struct tvafe_vga_auto_s *vga_auto)
{
	unsigned int r_left_hcnt = 0, r_right_hcnt = 0;
	unsigned int g_left_hcnt = 0, g_right_hcnt = 0;
	unsigned int b_left_hcnt = 0, b_right_hcnt = 0;

	struct tvafe_vga_border_s *bd = &vga_auto->border;

	r_right_hcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR14 ,
			BD_R_RIGHT_HCNT_BIT, BD_R_RIGHT_HCNT_WID);
	r_left_hcnt  = READ_APB_REG_BITS(TVFE_AP_INDICATOR14,
			BD_R_LEFT_HCNT_BIT,  BD_R_LEFT_HCNT_WID );
	g_right_hcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR16,
			BD_G_RIGHT_HCNT_BIT, BD_G_RIGHT_HCNT_WID);
	g_left_hcnt  = READ_APB_REG_BITS(TVFE_AP_INDICATOR16,
			BD_G_LEFT_HCNT_BIT,  BD_G_LEFT_HCNT_WID );
	b_right_hcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR18,
			BD_B_RIGHT_HCNT_BIT, BD_B_RIGHT_HCNT_WID);
	b_left_hcnt  = READ_APB_REG_BITS(TVFE_AP_INDICATOR18,
			BD_B_LEFT_HCNT_BIT,  BD_B_LEFT_HCNT_WID );
	if(adc_dbg_en)
		pr_info("[tvafe..]%s r_left %u.g_left %u.b_left %u.r_right %u.g_right %u.b_right %u.\n",__func__,r_left_hcnt,
				g_left_hcnt, b_left_hcnt,r_right_hcnt,g_right_hcnt,b_right_hcnt);

	bd->hstart = min(r_left_hcnt, g_left_hcnt);
	bd->hstart = min(bd->hstart, b_left_hcnt);

	bd->hend = max(r_right_hcnt, g_right_hcnt);
	bd->hend = max(bd->hend, b_right_hcnt);

}

/*
 * tvafe get v border value after border detection
 */
static void tvafe_vga_get_v_border(struct tvafe_vga_auto_s *vga_auto)
{
	unsigned int r_top_vcnt = 0, r_bot_vcnt = 0;
	unsigned int g_top_vcnt = 0, g_bot_vcnt = 0;
	unsigned int b_top_vcnt = 0, b_bot_vcnt = 0;
	struct tvafe_vga_border_s *bd = &vga_auto->border;

	r_top_vcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR13, BD_R_TOP_VCNT_BIT, BD_R_TOP_VCNT_WID);
	r_bot_vcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR13, BD_R_BOT_VCNT_BIT, BD_R_BOT_VCNT_WID);
	g_top_vcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR15, BD_G_TOP_VCNT_BIT, BD_G_TOP_VCNT_WID);
	g_bot_vcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR15, BD_G_BOT_VCNT_BIT, BD_G_BOT_VCNT_WID);
	b_top_vcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR17, BD_B_TOP_VCNT_BIT, BD_B_TOP_VCNT_WID);
	b_bot_vcnt = READ_APB_REG_BITS(TVFE_AP_INDICATOR17, BD_B_BOT_VCNT_BIT, BD_B_BOT_VCNT_WID);
	if(adc_dbg_en)
		pr_info("[tvafe..]%s r_start %u.g_start %u.b_start%u.r_end %u.g_end %u.b_end%u.\n",__func__,r_top_vcnt,
				g_top_vcnt, b_top_vcnt,r_bot_vcnt,g_bot_vcnt,b_bot_vcnt);

	bd->vstart = min(r_top_vcnt, g_top_vcnt);
	bd->vstart = min(bd->vstart, b_top_vcnt);

	bd->vend  = max(r_bot_vcnt, g_bot_vcnt);
	bd->vend  = max(bd->vend, b_bot_vcnt);

}

/*
 * tvafe vsync counter
 */
inline void tvafe_vga_vs_cnt(struct tvafe_adc_s *adc)
{
	if (++adc->vga_auto.vs_cnt > TVAFE_VGA_VS_CNT_MAX)
		adc->vga_auto.vs_cnt = TVAFE_VGA_VS_CNT_MAX;
}

/*
 * tvafe adc clock adjustment for vga auto adjust function
 */
static void tvafe_vga_auto_clock_adj(unsigned int clk, signed int diff)
{
	if (diff > 0)
		clk -= (abs(diff) + 1) >> 1;
	if (diff < 0)
		clk += (abs(diff) + 1) >> 1;
	tvafe_vga_set_clock(clk);
	// disable border detect
	tvafe_vga_border_detect_disable();
	// enable border detect
	//tvafe_vga_border_detect_enable();
}

/*
 * tvafe vga auto clcok funtion
 */
static void tvafe_vga_auto_clock_handler(enum tvin_sig_fmt_e fmt, struct tvafe_adc_s *adc)
{
	unsigned int clk = 0;
	signed int diff = 0;

	struct tvafe_vga_auto_s *vga_auto = &adc->vga_auto;
	struct tvafe_vga_parm_s *vga_parm = &adc->vga_parm;

	//signal stable

	switch (vga_auto->clk_state)
	{
		case VGA_CLK_IDLE:
			break;
		case VGA_CLK_INIT:
			//tvafe_vga_set_phase(VGA_ADC_PHASE_MID);
			//tvafe_vga_set_clock(tvin_vga_fmt_tbl[tvinfo->fmt - TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total-1);  //set spec clock value
			vga_parm->phase = VGA_ADC_PHASE_MID;
			vga_parm->clk_step = 0;
			tvafe_vga_border_detect_init( fmt);
			vga_auto->adj_cnt = 0;
			vga_auto->adj_dir = 0;
			vga_auto->clk_state = VGA_CLK_END;//VGA_CLK_ROUGH_ADJ;
			vga_auto->vs_cnt  = 0;
			break;
		case VGA_CLK_ROUGH_ADJ:
			diff = 0;
			if (vga_auto->vs_cnt > AUTO_CLK_VS_CNT)
			{
				// get H border
				tvafe_vga_get_h_border(vga_auto);
				// get current clk
				clk = tvafe_vga_get_clock();
				if (adc_dbg_en)
					pr_info("[tvafe..] %s: auto clock start, org_clk=%d \n",__func__, clk);
				// calculate new clk
				clk = (((clk * (unsigned int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active) << 8) / (vga_auto->border.hend - vga_auto->border.hstart + 1) + 128) >> 8;
				if (adc_dbg_en)
					pr_info("[tvafe..] %s: auto clock start, init_clk=%d \n",__func__, clk);
				// if clk is too far from spec, then return error
				if ((clk > ((tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1) + (tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total >> MAX_AUTO_CLOCK_ORDER))) ||
						(clk < ((tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1) - (tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total >> MAX_AUTO_CLOCK_ORDER)))
				   )
				{
					vga_auto->clk_state = VGA_CLK_EXCEPTION;
				}
				else
				{
					tvafe_vga_auto_clock_adj(clk, diff);
					//tvafe_vga_border_detect_disable();
					vga_auto->clk_state = VGA_CLK_FINE_ADJ;
				}
				vga_auto->vs_cnt = 0;
			}
			break;
		case VGA_CLK_FINE_ADJ:
			if (++vga_auto->adj_cnt > VGA_AUTO_TRY_COUNTER)
			{
				vga_auto->clk_state = VGA_CLK_EXCEPTION;
			}
			else
			{
				//delay about 4 field for border detection
				if (vga_auto->vs_cnt == TVAFE_VGA_BD_EN_DELAY)
				{
					// disable border detect
					tvafe_vga_border_detect_enable();
				}
				if (vga_auto->vs_cnt > AUTO_CLK_VS_CNT)
				{
					//vga_auto->vs_cnt = 0;
					// get H border
					tvafe_vga_get_h_border(vga_auto);
					// get diff
					diff = (signed int)vga_auto->border.hend - (signed int)vga_auto->border.hstart + (signed int)1 - (signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active;
					// get clk
					clk = tvafe_vga_get_clock();
					if (!diff)
					{
						vga_auto->clk_state = VGA_CLK_END;
					}
					if (diff > 0)
					{
						if (vga_auto->adj_dir == 1)
						{
							if (clk > (tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1))
							{
								tvafe_vga_auto_clock_adj( clk, diff);
							}
							vga_auto->clk_state = VGA_CLK_END;
						}
						else
						{
							tvafe_vga_auto_clock_adj(clk, diff);
							vga_auto->adj_dir = -1;
						}
					}
					if (diff < 0)
					{
						if (vga_auto->adj_dir == -1)
						{
							if (clk < (tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1))
							{
								tvafe_vga_auto_clock_adj(clk, diff);
							}
							vga_auto->clk_state = VGA_CLK_END;
						}
						else
						{
							tvafe_vga_auto_clock_adj(clk, diff);
							vga_auto->adj_dir = 1;
						}
					}
					tvafe_adc_set_frame_skip_number(adc, 2);
					vga_auto->vs_cnt = 0;
				}
			}
			break;
		case VGA_CLK_EXCEPTION: //stop auto
			// disable border detect
			if (adc_dbg_en)
				pr_info("[tvafe..] %s: auto clock error!!! \n",__func__);
			tvafe_vga_set_clock(tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1);  //set spec clock value
			tvafe_adc_set_frame_skip_number(adc, 2);
			tvafe_vga_border_detect_disable();
			adc->cmd_status = TVAFE_CMD_STATUS_FAILED;
			vga_auto->clk_state = VGA_CLK_IDLE;
			break;
		case VGA_CLK_END: //start auto phase
			// disable border detect
			if (adc_dbg_en)
				pr_info("[tvafe..] %s: auto clock successful, last_clk=%d \n",__func__,
						tvafe_vga_get_clock());
			tvafe_vga_border_detect_disable();
			vga_auto->phase_state = VGA_PHASE_INIT;
			vga_auto->clk_state = VGA_CLK_IDLE;
			break;
		default:
			break;
	}

	return;
}

black_det_e tvin_vdin_get_bar(bool is_left, ushort v_active)
{
	uint val1=0, val2=0;
	black_det_e ret = UNKNOWN;
	if(is_left){
		val1 = READ_CBUS_REG_BITS(VDIN_BLKBAR_IND_LEFT1_CNT,BLKBAR_LEFT1_CNT_BIT,BLKBAR_LEFT1_CNT_WID);
		val2 = READ_CBUS_REG_BITS(VDIN_BLKBAR_IND_LEFT2_CNT,BLKBAR_LEFT2_CNT_BIT,BLKBAR_LEFT2_CNT_WID);
	}else{
		val2 = READ_CBUS_REG_BITS(VDIN_BLKBAR_IND_RIGHT1_CNT,BLKBAR_RIGHT1_CNT_BIT,BLKBAR_RIGHT1_CNT_WID);
		val1 = READ_CBUS_REG_BITS(VDIN_BLKBAR_IND_RIGHT2_CNT,BLKBAR_RIGHT2_CNT_BIT,BLKBAR_RIGHT2_CNT_WID);
	}
	if(abs(val1-val2)> v_active/black_bar_v2){
		if(val1 > val2){
			ret = IS_BORDER;
		}else{
			ret = IS_VALID;
		}
	}else{
		if(val1+val2 >= 2*min(v_active*black_bar_v1/100, v_active-black_bar_white)){
			ret = IS_BLACK;
		}else{
			ret = IS_VALID;
		}
	}
	if(adc_dbg_en)
		pr_info("%s: is_left=%d, val1=%d, val2=%d, ret=%d\n",__func__,is_left,val1,val2,ret);
	return ret;
}
void tvin_vdin_bar_detect(enum tvin_sig_fmt_e fmt, struct tvafe_adc_s *adc)
{
        const struct tvin_format_s *fmt_info = tvin_get_fmt_info(fmt);
	black_det_e l_status = UNKNOWN,r_status = UNKNOWN;
	static signed short l_step,r_step;
	static  unsigned int barhstart, barhend;
	static uint dir;
	static uint pre_status,cur_status;
	static uint time_out = 0;

        if(!fmt_info){
                pr_info("[tvafe]%s: null pointer error.\n",__func__);
                return;
        }
	if((b_status>BAR_INIT)&&(time_out++ > bar_det_timeout)){
		if(adc_dbg_en)
			pr_info("%s: time out !!\n",__func__);
		b_status = BAR_FAIL;
	}

	switch(b_status){
	case BAR_INIT:
		if(adc->vga_auto.vs_cnt < bar_init_cnt)
			break;
		adc->vga_auto.vs_cnt = 0;
		l_step = 0;
		r_step = 0;
		dir = 0;
		time_out = 0;
		cur_status = 0;
		pre_status = 0;
		barhstart = READ_CBUS_REG_BITS(VDIN_BLKBAR_H_START_END,BLKBAR_HSTART_BIT,BLKBAR_HSTART_WID);
		barhend   = READ_CBUS_REG_BITS(VDIN_BLKBAR_H_START_END,BLKBAR_HEND_BIT,BLKBAR_HEND_WID);
		if(adc_dbg_en)
			pr_info("[BAR_INIT]%s: barhstart=[%d],barhend=[%d],hpos_step=[%d].\n",__func__,barhstart,barhend,adc->vga_parm.hpos_step);
		b_status = BAR_PRE_DET;
		break;
	case BAR_PRE_DET:
		if(adc_dbg_en)
			pr_info("[BAR_DET_PRE]: cur_st = 0x%x, pre_st = 0x%x,vs_cnt =%3d\n",cur_status,pre_status,adc->vga_auto.vs_cnt);
		if(adc->vga_auto.vs_cnt < bar_pre_det_cnt){
			r_status = tvin_vdin_get_bar(0,fmt_info->v_active);
			l_status = tvin_vdin_get_bar(1,fmt_info->v_active);
			cur_status = (l_status<<8)|r_status;
			if(cur_status != pre_status){
				adc->vga_auto.vs_cnt = 0;
				pre_status = cur_status;
			}
			break;
		}
		adc->vga_auto.vs_cnt = 0;
		r_status = cur_status&0xff;
		l_status = cur_status>>8;
		if((l_status==IS_BLACK) && (r_status==IS_VALID)){
			dir = 1;
			b_status = BAR_TUNE;
		}else if((l_status==IS_VALID) && (r_status==IS_BLACK)){
			dir = 0;
			b_status = BAR_TUNE;
		}else if(l_status==IS_BORDER){
			r_step = 1;
			b_status = BAR_SUCCESS;
		}else if(r_status==IS_BORDER){
			l_step = 1;
			b_status = BAR_SUCCESS;
		}else if((l_status==IS_VALID) && (r_status==IS_VALID)){
			b_status = BAR_SUCCESS;
		}else{
			if(adc_dbg_en)
				pr_info("%s: Bad pattern!!\n",__func__);
			b_status = BAR_FAIL;
		}
		break;

	case BAR_TUNE:
		if(adc_dbg_en)
			pr_info("BAR_TUNE:vs_cnt =%3d\n",adc->vga_auto.vs_cnt);
		if(adc->vga_auto.vs_cnt<bar_tune_cnt){
			cur_status = tvin_vdin_get_bar(dir,fmt_info->v_active);
			if(cur_status != pre_status){
				pre_status = cur_status;
				adc->vga_auto.vs_cnt =  0;
			}
			break;
		}
		adc->vga_auto.vs_cnt = 0;
		if(cur_status == IS_BORDER){
			dir ? r_step++ : l_step++;
			b_status = BAR_SUCCESS;
			break;
		}else{
			dir ? r_step++ : l_step++;
		}
		if((adc->vga_parm.hpos_step + (r_step - l_step) < (signed short)1-(signed short)fmt_info->hs_bp)
			|| (adc->vga_parm.hpos_step + (r_step - l_step) > (signed short)(fmt_info->h_total-fmt_info->hs_width-fmt_info->hs_bp-fmt_info->h_active-1))){
			if(adc_dbg_en)
				pr_info("out of range:hpos_step = %d, r_step = %d, l_step = %d\n",
				adc->vga_parm.hpos_step,r_step,l_step);
			b_status = BAR_FAIL;
			break;
		}

		WRITE_CBUS_REG(VDIN_BLKBAR_H_START_END,(barhend-l_step)|(barhstart+r_step)<<16);
		cur_status = UNKNOWN;
		pre_status = UNKNOWN;
		b_status = BAR_TUNE;
		break;

	case BAR_SUCCESS:
		time_out = 0;
		adc->vga_parm.hpos_step += r_step - l_step;
		if(adc_dbg_en)
			pr_info("success: hpos_step = %d, r_step = %d, l_step = %d\n",
			adc->vga_parm.hpos_step,r_step,l_step);
		adc->cmd_status = TVAFE_CMD_STATUS_SUCCESSFUL;
		adc->vga_auto.phase_state = VGA_PHASE_IDLE;
		break;

	case BAR_FAIL:
		time_out = 0;
		if(adc_dbg_en)
			pr_info("%s: FAIL\n",__func__);
		adc->cmd_status = TVAFE_CMD_STATUS_SUCCESSFUL;
		adc->vga_auto.phase_state = VGA_PHASE_IDLE;
		break;
	default:
		break;
	}
}

void tvin_vdin_H_bar_detect(enum tvin_sig_fmt_e fmt, struct tvafe_adc_s *adc)
{
        unsigned int barhstart=0, barhend=0,hleft1=0,hleft2=0,hright1=0,hright2=0;
        const struct tvin_format_s *fmt_info_p;
		static unsigned int cnt=4;
        fmt_info_p = tvin_get_fmt_info(fmt);
        if(!fmt_info_p){
                pr_info("[tvafe]%s: null pointer error.\n",__func__);
                return;
        }
	//tvafe_adc_set_frame_skip_number(adc, 4);
        barhstart = READ_CBUS_REG_BITS(VDIN_BLKBAR_H_START_END,BLKBAR_HSTART_BIT,BLKBAR_HSTART_WID);
        barhend   = READ_CBUS_REG_BITS(VDIN_BLKBAR_H_START_END,BLKBAR_HEND_BIT,BLKBAR_HEND_WID);

        if(barhstart > 8){
                adc->cmd_status = TVAFE_CMD_STATUS_SUCCESSFUL;
                pr_err("[vdin..]%s: vdin border detection failed.\n",__func__);
				adc->vga_parm.hpos_step+=bar_width;
        }

       // barhend--;
       if(!(cnt--)){
        	barhstart++;
			cnt=4;
       	}

        hleft1 = READ_CBUS_REG_BITS(VDIN_BLKBAR_IND_LEFT1_CNT,BLKBAR_LEFT1_CNT_BIT,BLKBAR_LEFT1_CNT_WID);
        hleft2 = READ_CBUS_REG_BITS(VDIN_BLKBAR_IND_LEFT2_CNT,BLKBAR_LEFT2_CNT_BIT,BLKBAR_LEFT2_CNT_WID);

		if(hleft1 >= (fmt_info_p->v_active>>black_bar_v1) && (hleft2 <= fmt_info_p->v_active>>black_bar_v2)){
                adc->cmd_status = TVAFE_CMD_STATUS_SUCCESSFUL;
				if(cnt==4)
                	adc->vga_parm.hpos_step += (barhstart);
				else
					adc->vga_parm.hpos_step += (barhstart+1);
                if(adc_dbg_en)
                        pr_info("[vdin..]%s: detect end left border barstart=%u.\n",__func__,barhstart);

        }

        if(adc->cmd_status == TVAFE_CMD_STATUS_SUCCESSFUL){
                //reset adc digital pll after vga auto done
				//tvafe_vga_border_detect_disable();
				adc->vga_auto.phase_state = VGA_PHASE_IDLE;
	        //tvafe_adc_set_frame_skip_number(adc, 4);
        } else {
                WRITE_CBUS_REG(VDIN_BLKBAR_H_START_END,barhend|barhstart<<16);
        }
        if(adc_dbg_en)
                pr_info("[vdin..]%s:cnt %d hleft1 cnt %u, hleft2 cnt %u, hright1 cnt %u,hright2 cnt %u,barhend %u,barhstart %u.\n",
                        __func__,cnt,hleft1,hleft2,hright1,hright2,barhend,barhstart);
}
/*
*use vdin black bar detection to detect border
*/
void tvin_vdin_bbar_init(enum tvin_sig_fmt_e fmt)
{
	const struct tvin_format_s *fmt_info_p;
	fmt_info_p = tvin_get_fmt_info(fmt);
	if(!fmt_info_p){
	        pr_info("[tvafe]%s: null pointer error.\n",__func__);
	        return;
	}
	b_status = BAR_INIT;
	//disable reset
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,0, BLKBAR_DET_SOFT_RST_N_BIT, BLKBAR_DET_SOFT_RST_N_WID);

	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,white_thr_level, BLKBAR_BLK_LVL_BIT, BLKBAR_BLK_LVL_WID);
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,2, BLKBAR_DIN_SEL_BIT, BLKBAR_DIN_SEL_WID);
	WRITE_CBUS_REG(VDIN_SCIN_HEIGHTM1,(fmt_info_p->v_active - 1));

	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,1, BLKBAR_SW_STAT_EN_BIT, BLKBAR_SW_STAT_EN_WID);
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,1, BLKBAR_H_WIDTH_BIT, BLKBAR_H_WIDTH_WID);
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_H_START_END,(fmt_info_p->h_active - 1), BLKBAR_HEND_BIT, BLKBAR_HEND_WID);
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_H_START_END,0, BLKBAR_HSTART_BIT, BLKBAR_HSTART_WID);
	// win_ve
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_V_START_END,(fmt_info_p->v_active - 1), BLKBAR_VEND_BIT, BLKBAR_VEND_WID);
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_V_START_END,0, BLKBAR_VSTART_BIT, BLKBAR_VSTART_WID);
	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,1, BLKBAR_DET_TOP_EN_BIT, BLKBAR_DET_TOP_EN_WID);

	WRITE_CBUS_REG_BITS(VDIN_BLKBAR_CTRL0,1, BLKBAR_DET_SOFT_RST_N_BIT, BLKBAR_DET_SOFT_RST_N_WID);
	// manual reset, rst = 0 & 1, raising edge mode
}
/*
 * tvafe vga auto phase funtion
 */
static void tvafe_vga_auto_phase_handler(enum tvin_sig_fmt_e fmt, struct tvafe_adc_s *adc)
{
	unsigned int sum = 0, hs = 0, he = 0, vs = 0, ve = 0;
	struct tvafe_vga_auto_s *vga_auto = &adc->vga_auto;
	struct tvafe_vga_parm_s *vga_parm = &adc->vga_parm;
        const struct tvin_format_s *fmt_info = tvin_get_fmt_info(fmt);

        if(!fmt_info){
                pr_info("[tvafe]%s: null pointer error.\n",__func__);
                return;
        }

	switch (vga_auto->phase_state) {
		case VGA_PHASE_IDLE:
			break;
		case VGA_PHASE_INIT:
			vga_auto->adj_cnt         = 0;
			vga_auto->ap_max_diff     = 0;
			vga_auto->ap_pha_index    = VGA_ADC_PHASE_0;
			vga_auto->ap_phamax_index = VGA_ADC_PHASE_0;
			vga_auto->ap_win_index    = VGA_PHASE_WIN_INDEX_0;
			vga_auto->ap_winmax_index = VGA_PHASE_WIN_INDEX_0;
			//tvafe_vga_set_phase(vga_auto->ap_pha_index);
			vga_parm->phase = (unsigned short)vga_auto->ap_pha_index;
			tvafe_vga_auto_phase_init( fmt, vga_auto->ap_win_index);
			if (vga_auto->vs_cnt > AUTO_CLK_VS_CNT)
			{
				vga_auto->phase_state = VGA_PHASE_SEARCH_WIN;
				vga_auto->vs_cnt = 0;
			}
			break;
		case VGA_PHASE_SEARCH_WIN:
			if (++vga_auto->adj_cnt > VGA_AUTO_TRY_COUNTER)
			{
				vga_auto->phase_state = VGA_PHASE_EXCEPTION;
			}
			else
			{
				if (vga_auto->vs_cnt > AUTO_PHASE_VS_CNT)
				{

					vga_auto->vs_cnt = 0;
					sum = tvafe_vga_get_ap_diff();
					if (vga_auto->ap_max_diff < sum)
					{
						vga_auto->ap_max_diff = sum;
						vga_auto->ap_winmax_index = vga_auto->ap_win_index;
					}
					if (unlikely(++vga_auto->ap_win_index > VGA_PHASE_WIN_INDEX_MAX))
					{
						tvafe_adc_set_ap_window(fmt, vga_auto->ap_winmax_index);
						vga_auto->ap_max_diff = 0;
						vga_auto->phase_state = VGA_PHASE_ADJ;
					}
					else
						tvafe_adc_set_ap_window(fmt, vga_auto->ap_win_index);
				}
			}
			break;
		case VGA_PHASE_ADJ:
			if (++vga_auto->adj_cnt > VGA_AUTO_TRY_COUNTER)
			{
				vga_auto->phase_state = VGA_PHASE_EXCEPTION;
			}
			else
			{
				if (vga_auto->vs_cnt > AUTO_PHASE_VS_CNT)
				{
					vga_auto->vs_cnt = 0;
					sum = tvafe_vga_get_ap_diff();
					if (adc_dbg_en)
						printk("sum=%d,vga_auto->ap_pha_index=%d\n",sum,vga_auto->ap_pha_index);
					if (vga_auto->ap_max_diff <= sum)
					{
						vga_auto->ap_max_diff = sum;
						vga_auto->ap_phamax_index = vga_auto->ap_pha_index;
					}
					if (++vga_auto->ap_pha_index > VGA_ADC_PHASE_MAX)
					{
						//tvafe_vga_set_phase(vga_auto->ap_phamax_index);
						//tvafe_adc_digital_reset();  //added for phase abnormal bug.
						vga_parm->phase = (unsigned short)vga_auto->ap_phamax_index;
						//enable border detect
						tvafe_vga_auto_phase_disable();
						tvafe_vga_border_detect_enable();
						tvafe_vga_border_detect_init(fmt);
						vga_auto->phase_state = VGA_PHASE_END;
					if (adc_dbg_en)
						printk("End:sum=%d,vga_auto->ap_phamax_index=%d\n",sum,vga_auto->ap_phamax_index);
					}
					else
						tvafe_vga_set_phase(vga_auto->ap_pha_index);
						//vga_parm->phase = (unsigned short)vga_auto->ap_pha_index;
				}
			}
			break;
		case VGA_PHASE_EXCEPTION: //stop auto
			// disable auto phase
			if (adc_dbg_en)
				pr_info("[tvafe..] %s: auto phase error!!! \n",__func__);
			tvafe_vga_auto_phase_disable();
			adc->cmd_status = TVAFE_CMD_STATUS_FAILED;
			vga_auto->phase_state = VGA_PHASE_IDLE;
		case VGA_PHASE_END: //auto position
			/*if(vga_auto->vs_cnt == 10)
			  {
			  tvafe_vga_border_detect_enable();
			  tvafe_vga_border_detect_init(fmt);
			  }*/
			if (vga_auto->vs_cnt > AUTO_CLK_VS_CNT)
			{
				//vga_auto->vs_cnt = 0;
				tvafe_vga_get_h_border(vga_auto);
				tvafe_vga_get_v_border(vga_auto);
				if (adc_dbg_en)
					pr_info("[tvafe..] %s:border detect end ! ve: %d,vs: %d,he: %d,hs: %d\n",
							__func__,vga_auto->border.vend,vga_auto->border.vstart,
							vga_auto->border.hend,vga_auto->border.hstart);
				if(((vga_auto->border.hend - vga_auto->border.hstart + 1) > fmt_info->h_active) &&
					(vga_auto->border.hend >= (fmt_info->hs_front+fmt_info->hs_width + fmt_info->hs_bp + fmt_info->h_active - 10))
					)
				{
					hs = fmt_info->hs_width + fmt_info->hs_bp;
					he = hs + fmt_info->h_active - 1;

				}

				else if (vga_auto->border.hstart < (fmt_info->hs_width + fmt_info->hs_bp))
				{
					hs = vga_auto->border.hstart;
					he = hs + fmt_info->h_active - 1;
				}
				else if (((vga_auto->border.hend - vga_auto->border.hstart + 1) >= fmt_info->h_active) ||
						(vga_auto->border.hend > (fmt_info->hs_width + fmt_info->hs_bp + fmt_info->h_active - 1))
					)
				{
					he = vga_auto->border.hend;
					hs = he - fmt_info->h_active + 1;
				}
				else
				{
					hs = fmt_info->hs_width + fmt_info->hs_bp;
					he = hs + fmt_info->h_active - 1;
				}
				if (((vga_auto->border.vend - vga_auto->border.vstart + 1) >= fmt_info->v_active) ||
						(vga_auto->border.vend > (fmt_info->vs_width + fmt_info->vs_bp + fmt_info->v_active - 1))
				   )
				{
					ve = vga_auto->border.vend;
					vs = ve - fmt_info->v_active + 1;
				}
				else if (vga_auto->border.vstart < (fmt_info->vs_width + fmt_info->vs_bp))
				{
					vs = vga_auto->border.vstart;
					ve = vs + fmt_info->v_active - 1;
				}
				else
				{
					vs = fmt_info->vs_width + fmt_info->vs_bp;
					ve = vs + fmt_info->v_active - 1;
				}
				if (adc_dbg_en)
					pr_info("[tvafe..] %s: auto phase finish,phase:%d,hs:%d,he:%d,vs:%d,ve:%d\n",__func__,
							tvafe_vga_get_phase(), hs, he, vs, ve);
				//update phase information in frame struct
				//vga_parm->phase = (unsigned short)vga_auto->ap_phamax_index;
				//tvafe_vga_set_h_pos(hs, he);
				/*vga_parm->hpos_step =   (signed short)he
					+ (signed short)1
					- (signed short)fmt_info->hs_width
					- (signed short)fmt_info->hs_bp
					- (signed short)fmt_info->h_active;
				*///tvafe_vga_set_v_pos(vs, ve, tvin_vga_fmt_tbl[tvinfo->fmt - TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].scan_mode);//tvafe_top_get_scan_mode());
				//due to bar det ,it must adj the hs
				vga_parm->vpos_step =   (signed short)ve
					+ (signed short)1
					- (signed short)fmt_info->vs_width
					- (signed short)fmt_info->vs_bp
					- (signed short)fmt_info->v_active;
				// disable border detect

				//tvafe_vga_border_detect_disable();
				// disable auto phase
				tvafe_vga_auto_phase_disable();
				//adc->cmd_status = TVAFE_CMD_STATUS_SUCCESSFUL;
				tvafe_adc_set_frame_skip_number(adc ,4);
				vga_parm->hpos_step = 0;//vga_parm->hpos_step -= bar_width;
				//tvafe_adc_set_frame_skip_number(adc, 10);

				vga_auto->phase_state = VGA_BORDER_DET_INIT;
				vga_auto->vs_cnt = 0;
			}
			break;
		case VGA_BORDER_DET_INIT:
			//if(vga_auto->vs_cnt > 30){
				//tvafe_adc_set_frame_skip_number(adc, 10);
				tvafe_adc_digital_reset();
				tvin_vdin_bbar_init(fmt);
				vga_auto->phase_state = VGA_BORDER_DET;
				vga_auto->vs_cnt = 0;
			//}
			break;
                case VGA_BORDER_DET:
                        if(vga_auto->vs_cnt > 20){
                                vga_auto->vs_cnt  = 0;
                                vga_auto->phase_state = VGA_VDIN_BORDER_DET;
                        }
                        break;
		default:
			break;
	}

	return;
}

/*
 * tvafe enable vga suto adjust function
 */
int tvafe_vga_auto_adjust_enable(struct tvafe_adc_s *adc)
{
	int ret = 0;

	if (adc->cmd_status  == TVAFE_CMD_STATUS_IDLE)
	{
		adc->cmd_status           = TVAFE_CMD_STATUS_PROCESSING;
		adc->vga_auto.clk_state   = VGA_CLK_INIT;
		adc->vga_auto.phase_state = VGA_PHASE_IDLE;
		adc->auto_enable          = true;
	}
	else
	{
		adc->cmd_status           = TVAFE_CMD_STATUS_FAILED;
		adc->vga_auto.clk_state   = VGA_CLK_IDLE;
		adc->vga_auto.phase_state = VGA_PHASE_IDLE;
		adc->auto_enable          = false;
		ret = -EAGAIN;
	}

	return ret;
}

/*
 * tvafe disenable vga suto adjust function
 */
void tvafe_vga_auto_adjust_disable(struct tvafe_adc_s *adc)
{
	struct tvafe_vga_auto_s *vga_auto = &adc->vga_auto;

	if (adc->cmd_status == TVAFE_CMD_STATUS_PROCESSING)
	{
		adc->cmd_status       = TVAFE_CMD_STATUS_TERMINATED;
		vga_auto->clk_state   = VGA_CLK_IDLE;
		vga_auto->phase_state = VGA_PHASE_IDLE;
		adc->auto_enable      = false;
	}
}

/*
 * tvafe vga suto adjust function
 */
void tvafe_vga_auto_adjust_handler(struct tvin_parm_s *parm, struct tvafe_adc_s *adc)
{
	enum tvin_port_e port = parm->port;
	enum tvin_sig_fmt_e fmt = parm->info.fmt;

	if (((port >= TVIN_PORT_VGA0) && (port <= TVIN_PORT_VGA7)) &&
			(adc->auto_enable == 1))
	{
		//auto clock handler
		tvafe_vga_auto_clock_handler(fmt, adc);

		// auto phase handler after auto clock
		tvafe_vga_auto_phase_handler(fmt, adc);

		if ((adc->cmd_status == TVAFE_CMD_STATUS_FAILED) ||
				(adc->cmd_status == TVAFE_CMD_STATUS_SUCCESSFUL)
		   )
			adc->auto_enable = 0;
	}

}


static void tvafe_adc_clear(unsigned int val, unsigned int clear)
{
	unsigned int i=0;

	for (i=0; i<ADC_REG_NUM; i++)
	{
		if (clear)
		{
			WRITE_APB_REG((ADC_BASE_ADD+i)<<2, ((i == 0x21) ? val : 0));
		}
		else
		{
			WRITE_APB_REG(ADC_REG_21, val);
		}
	}
}

/*
 * tvafe adc Reg table configure for vga/comp/cvbs
 */
void tvafe_adc_configure(enum tvin_sig_fmt_e fmt)
{
	int i = 0;
	const unsigned char *buff_t = NULL;

	/*adc reset*/
	tvafe_adc_clear(TVAFE_ADC_CONFIGURE_INIT, 1);
	tvafe_adc_clear(TVAFE_ADC_CONFIGURE_NORMAL, 1);
	tvafe_adc_clear(TVAFE_ADC_CONFIGURE_RESET_ON, 1);

	if (fmt < TVIN_SIG_FMT_VGA_MAX && fmt > TVIN_SIG_FMT_NULL) // VGA formats
	{
		buff_t = adc_vga_table[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147];
	}
	else if (fmt < TVIN_SIG_FMT_COMP_MAX && fmt > TVIN_SIG_FMT_VGA_THRESHOLD) // Component formats
	{
		buff_t = adc_component_table[fmt-TVIN_SIG_FMT_COMP_480P_60HZ_D000];
	}
	// CVBS formats
        else if(fmt < TVIN_SIG_FMT_CVBS_MAX && fmt > TVIN_SIG_FMT_HDMI_THRESHOLD)
	{
		buff_t = adc_cvbs_table;
	}
        else
        {
                pr_err("[tvafe..]%s: invaild fmt %s.\n",__func__,tvin_sig_fmt_str(fmt));
                return;
        }

	for (i=0; i<ADC_REG_NUM; i++)
	{
		WRITE_APB_REG(((ADC_BASE_ADD+i)<<2), (unsigned int)(buff_t[i]));
	}
	//set componet different phase base on board design
	if(fmt > TVIN_SIG_FMT_VGA_MAX && fmt < TVIN_SIG_FMT_COMP_MAX && enable_dphase)
	{
		WRITE_APB_REG_BITS(ADC_REG_56, comp_phase[fmt-TVIN_SIG_FMT_VGA_THRESHOLD -1],CLKPHASEADJ_BIT,CLKPHASEADJ_WID);
	}
	//for adc calibration clamping duration setting
	if (fmt < TVIN_SIG_FMT_COMP_MAX)
	{
		tvafe_adc_set_clamp_para(fmt);
	}

	/* adc config normal */
	tvafe_adc_clear(TVAFE_ADC_CONFIGURE_NORMAL, 0);
#ifdef TVAFE_DEBUG_PIN_ENABLE
	//debug setting
	// diable other mux on test pins 0~27 & 30
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_0 , READ_CBUS_REG(PERIPHS_PIN_MUX_0 )&0xcff0ffdf);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_1 , READ_CBUS_REG(PERIPHS_PIN_MUX_1 )&0xfc017fff);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_2 , READ_CBUS_REG(PERIPHS_PIN_MUX_2 )&0xe001ffff);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_3 , READ_CBUS_REG(PERIPHS_PIN_MUX_3 )&0xfc000000);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_4 , READ_CBUS_REG(PERIPHS_PIN_MUX_4 )&0xff8007ff);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_6 , READ_CBUS_REG(PERIPHS_PIN_MUX_6 )&0xffffffbf);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_7 , READ_CBUS_REG(PERIPHS_PIN_MUX_7 )&0xff00003f);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_10, READ_CBUS_REG(PERIPHS_PIN_MUX_10)&0xffffffb3);
	// enable TVFE_TEST mux on test pins 0~27 & 30
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_9 , 0x4fffffff);//
#endif

}
void tvafe_adc_comphase_pr()
{
	int i = 0;
	for(i=0; i<TVIN_SIG_FMT_COMP_MAX-TVIN_SIG_FMT_VGA_THRESHOLD-1; i++)
		printk("[tvafe..] %s phase 0x%x.\n",tvin_sig_fmt_str(i+TVIN_SIG_FMT_VGA_MAX+1), comp_phase[i]);
}

void tvafe_adc_set_deparam( struct tvin_parm_s *parm, struct tvafe_adc_s *adc)
{
	enum tvin_sig_fmt_e fmt = parm->info.fmt;
	//tvafe_vga_set_clock(tvin_vga_fmt_tbl[fmt - TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1);
	tvafe_vga_set_h_pos( tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_bp, tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_bp + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active - 1);
	tvafe_vga_set_v_pos( tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_bp + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width, tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_bp + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active - 1,tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].scan_mode);
	if (adc_parm_en)
		pr_info("[tvafe..] %s: set default parameters \n",__func__);
}
/*
 * tvafe vga paramters adjustment: h-position, v-position, phase, clock
 */
void tvafe_adc_set_param(struct tvin_parm_s *parm, struct tvafe_adc_s *adc)
{
	enum tvin_sig_fmt_e fmt = parm->info.fmt;
	struct tvafe_vga_parm_s *vga_parm = &adc->vga_parm;
	signed int data = 0;
	signed int step = 0;
	unsigned int tmp = 0;
	unsigned int hs = 0;
	unsigned int he = 0;
	unsigned int vs = 0;
	unsigned int ve = 0;
#if 1  //disable manual clock
	data = (signed int)(vga_parm->clk_step);// * (tvin_fmt_tbl[fmt].h_total - 1)>>12);  //htotal/2^12
	tmp = (unsigned int)((signed int)(tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1) + data);
	if (tmp != tvafe_vga_get_clock())
	{
		tvafe_vga_set_clock(tmp);
		if(!adc->auto_enable)
			tvafe_adc_set_frame_skip_number(adc, 2);
		if (adc_parm_en)
			pr_info("[tvafe..] %s: set clk=%d \n",__func__, tmp);
	}
#endif
	// phase
	if (vga_parm->phase > VGA_ADC_PHASE_MAX)
		vga_parm->phase = VGA_ADC_PHASE_MAX;
	tmp = vga_parm->phase;
	if (tmp != tvafe_vga_get_phase())
	{
		tvafe_vga_set_phase(tmp);
		/*removed and keep adc_15 7'b 0 in m2c*/
		  tvafe_adc_digital_reset();
		  //tvafe_adc_set_frame_skip_number(adc, 1);
		if (adc_parm_en)
			pr_info("[tvafe..] %s: set phase=%d \n",__func__, tmp);
	}
	// hpos
	step = (signed int)vga_parm->hpos_step;
	data = (signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_bp;
	if (step + data < 0)
	{
		hs = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width + (unsigned int)((data + step)%8 + 8);
		he = (unsigned int)((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width + (signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active
				+ data + step) - 1;
	}
	else
	{
		hs = (unsigned int)((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width + data + step);
		he = hs + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_active - 1;
		//avoid he is bigger than h total
		he = he > (tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1)?(tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1):he;
	}
	if (adc_parm_en)
		pr_info("[tvafe..] %s: step %d,set hs=%d \n",__func__,step, hs);
	tvafe_vga_set_h_pos(hs, he);
	// vpos
	step = (signed int)vga_parm->vpos_step;
	data = (signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_bp;
	if (step + data - vbp_offset < 0)
	{
		vs = tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width + vbp_offset;
		ve = (unsigned int)((signed int)vs + (signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active + data + step - vbp_offset) - 1;
	}//avoid ve is bigger than v total
	else if((1 == tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_front)&&(0<step))
	{
		vs = (unsigned int)((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width + data +1);
		ve = vs + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active - 1;
	}
	else if(step +1 > tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_front)
	{
		vs = (unsigned int)((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width + data + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_front -1);
		ve = vs + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active - 1;
		if (adc_parm_en)
			pr_info("[tvafe..] %s: vstep %u>vs_front enable %u cut window.\n",__func__,step,tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_front);
	}
	else
	{
		vs = (unsigned int)((signed int)tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width + data + step);
		ve = vs + tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].v_active - 1;
	}
	if (adc_parm_en)
		pr_info("[tvafe..] %s: step %d,set vs=%d ve=%d\n",__func__,step,vs,ve);
	tvafe_vga_set_v_pos(vs, ve, tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].scan_mode);

	tvafe_adc_set_frame_skip_number(adc, 2);

}

/*
 * tvafe get vga paramters: h-position, v-position, phase, clock
 */
void tvafe_adc_get_param(struct tvin_parm_s *parm, struct tvafe_adc_s *adc)
{
	enum tvin_sig_fmt_e fmt = parm->info.fmt;
	struct tvafe_vga_parm_s *vga_parm = &adc->vga_parm;

	vga_parm->clk_step  = (signed short)(tvafe_vga_get_clock() -
			(tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].h_total - 1));
	vga_parm->phase     = tvafe_vga_get_phase();
	vga_parm->hpos_step = (signed short)(tvafe_vga_get_h_pos() -
			tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_width   -
			tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].hs_bp);
	vga_parm->vpos_step = (signed short)(tvafe_vga_get_v_pos() -
			tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_width   -
			tvin_vga_fmt_tbl[fmt-TVIN_SIG_FMT_VGA_512X384P_60HZ_D147].vs_bp);

	if (adc_parm_en)
		pr_info("[tvafe..] %s: get clk=%d  phase=%d  h_step=%d  v_step=%d\n",__func__,
				vga_parm->clk_step, vga_parm->phase, vga_parm->hpos_step, vga_parm->vpos_step);

}

