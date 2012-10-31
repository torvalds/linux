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

#include "de_iep.h"

#ifndef CONFIG_ARCH_SUN5I
#error IEP should only be used on sun5i
#endif

static __de_iep_dev_t *iep_dev;

#define ____SEPARATOR_IEP____

__s32 DE_IEP_Set_Reg_Base(__u32 sel, __u32 base)
{
	if (sel == 0) {
		iep_dev = (__de_iep_dev_t *) base;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Get_Reg_Base(__u32 sel, __u32 base)
{
	__u32 ret = 0;
	if (sel == 0) {
		ret = (__u32) iep_dev;
		return ret;
	} else {
		return 0;
	}
}

__s32 DE_IEP_Enable(__u32 sel)
{
	if (sel == 0) {
		iep_dev->gnectl.bits.en = 1;
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Disable(__u32 sel)
{
	if (sel == 0) {
		iep_dev->gnectl.bits.en = 0;
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Set_Mode(__u32 sel, __u32 mod)
{
	if (sel == 0) {
		iep_dev->gnectl.bits.mod = mod;
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Bist_Enable(__u32 sel, __u32 en)
{
	if (sel == 0) {
		iep_dev->gnectl.bits.bist_en = en;
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Set_Reg_Refresh_Edge(__u32 sel, __u32 falling)
{
	if (sel == 0) {
		iep_dev->gnectl.bits.sync_edge = falling;
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Set_Csc_Coeff(__u32 sel, __u32 mod)
{
	if (sel == 0) {
		if (mod == 2) { /* yuv2rgb for drc mode */
			/* bt709 full range(to fit output CSC in DEBE ) */
			iep_dev->cscygcoff[0].bits.csc_yg_coff = 0x4a7;
			iep_dev->cscygcoff[1].bits.csc_yg_coff = 0x000;
			iep_dev->cscygcoff[2].bits.csc_yg_coff = 0x72c;
			iep_dev->cscygcon.bits.csc_yg_con = 0x307d;
			iep_dev->cscurcoff[0].bits.csc_ur_coff = 0x4a7;
			iep_dev->cscurcoff[1].bits.csc_ur_coff = 0x1f25;
			iep_dev->cscurcoff[2].bits.csc_ur_coff = 0x1ddd;
			iep_dev->cscurcon.bits.csc_ur_con = 0x4cf;
			iep_dev->cscvbcoff[0].bits.csc_vb_coff = 0x4a7;
			iep_dev->cscvbcoff[1].bits.csc_vb_coff = 0x875;
			iep_dev->cscvbcoff[2].bits.csc_vb_coff = 0x000;
			iep_dev->cscvbcon.bits.csc_vb_con = 0x2dea;
		} else if (mod == 3) {
#if 0
			/* IGB to RGB */
			iep_dev->cscygcoff[0].bits.csc_yg_coff = 0x0c00;
			iep_dev->cscygcoff[1].bits.csc_yg_coff = 0x1c00;
			iep_dev->cscygcoff[2].bits.csc_yg_coff = 0x1c00;
			iep_dev->cscygcon.bits.csc_yg_con = 0x0000;
			iep_dev->cscurcoff[0].bits.csc_ur_coff = 0x0000;
			iep_dev->cscurcoff[1].bits.csc_ur_coff = 0x0400;
			iep_dev->cscurcoff[2].bits.csc_ur_coff = 0x0000;
			iep_dev->cscurcon.bits.csc_ur_con = 0x0000;
			iep_dev->cscvbcoff[0].bits.csc_vb_coff = 0x0000;
			iep_dev->cscvbcoff[1].bits.csc_vb_coff = 0x0000;
			iep_dev->cscvbcoff[2].bits.csc_vb_coff = 0x0400;
			iep_dev->cscvbcon.bits.csc_vb_con = 0x0000;
#else
			/* YUV2RGB when Er = 19%, Eg = 65%, Eb = 16%. */
			iep_dev->cscygcoff[0].bits.csc_yg_coff = 0x0400;
			iep_dev->cscygcoff[1].bits.csc_yg_coff = 0x0000;
			iep_dev->cscygcoff[2].bits.csc_yg_coff = 0x067B;
			iep_dev->cscygcon.bits.csc_yg_con = 0x330A;
			iep_dev->cscurcoff[0].bits.csc_ur_coff = 0x0400;
			iep_dev->cscurcoff[1].bits.csc_ur_coff = 0x1E59;
			iep_dev->cscurcoff[2].bits.csc_ur_coff = 0x1E1B;
			iep_dev->cscurcon.bits.csc_ur_con = 0x0719;
			iep_dev->cscvbcoff[0].bits.csc_vb_coff = 0x0400;
			iep_dev->cscvbcoff[1].bits.csc_vb_coff = 0x06B8;
			iep_dev->cscvbcoff[2].bits.csc_vb_coff = 0x0000;
			iep_dev->cscvbcon.bits.csc_vb_con = 0x328F;

#endif
		} else { /* yuv2yuv       for de-flicker mode */
			iep_dev->cscygcoff[0].bits.csc_yg_coff = 0x400;
			iep_dev->cscygcoff[1].bits.csc_yg_coff = 0x000;
			iep_dev->cscygcoff[2].bits.csc_yg_coff = 0x000;
			iep_dev->cscygcon.bits.csc_yg_con = 0x000;
			iep_dev->cscurcoff[0].bits.csc_ur_coff = 0x000;
			iep_dev->cscurcoff[1].bits.csc_ur_coff = 0x400;
			iep_dev->cscurcoff[2].bits.csc_ur_coff = 0x000;
			iep_dev->cscurcon.bits.csc_ur_con = 0x000;
			iep_dev->cscvbcoff[0].bits.csc_vb_coff = 0x000;
			iep_dev->cscvbcoff[1].bits.csc_vb_coff = 0x000;
			iep_dev->cscvbcoff[2].bits.csc_vb_coff = 0x400;
			iep_dev->cscvbcon.bits.csc_vb_con = 0x000;
		}
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Set_Display_Size(__u32 sel, __u32 width, __u32 height)
{
	if (sel == 0) {
		iep_dev->drcsize.bits.disp_w = width - 1;
		iep_dev->drcsize.bits.disp_h = height - 1;
		return 0;
	} else {
		return -1;
	}
}

__s32 DE_IEP_Demo_Win_Enable(__u32 sel, __u32 en)
{
	if (sel == 0) {
		iep_dev->drcctl.bits.win_en = en;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Set_Demo_Win_Para(__u32 sel, __u32 top, __u32 bot, __u32 left,
			       __u32 right)
{
	if (sel == 0) {
		iep_dev->drc_wp0.bits.win_left = left;
		iep_dev->drc_wp0.bits.win_top = top;
		iep_dev->drc_wp1.bits.win_right = right;
		iep_dev->drc_wp1.bits.win_bottom = bot;
		return 0;
	} else {
		return -1;
	}
}

#define ____SEPARATOR_DRC____

__s32 DE_IEP_Drc_Cfg_Rdy(__u32 sel)
{
	if (sel == 0) {
		iep_dev->drcctl.bits.dbrdy_ctl = 1;
		iep_dev->drcctl.bits.db_en = 1;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Drc_Set_Lgc_Addr(__u32 sel, __u32 addr)
{
	if (sel == 0) {
		iep_dev->drclgc_addr.bits.lgc_addr = addr;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Drc_Set_Lgc_Autoload_Disable(__u32 sel, __u32 disable)
{
	if (sel == 0) {
		iep_dev->drc_set.bits.gain_autoload_dis = disable;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Drc_Adjust_Enable(__u32 sel, __u32 en)
{
	if (sel == 0) {
		iep_dev->drc_set.bits.adjust_en = en;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Drc_Set_Adjust_Para(__u32 sel, __u32 abslumperval, __u32 abslumshf)
{
	if (sel == 0) {
		iep_dev->drc_set.bits.lgc_abslumshf = abslumshf;
		iep_dev->drc_set.bits.lgc_abslumperval = abslumperval;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Drc_Set_Spa_Coeff(__u32 sel, __u8 spatab[IEP_DRC_SPA_TAB_LEN])
{
	if (sel == 0) {
		iep_dev->drcspacoff[0].bits.spa_coff0 = spatab[0];
		iep_dev->drcspacoff[0].bits.spa_coff1 = spatab[1];
		iep_dev->drcspacoff[0].bits.spa_coff2 = spatab[2];
		iep_dev->drcspacoff[1].bits.spa_coff0 = spatab[3];
		iep_dev->drcspacoff[1].bits.spa_coff1 = spatab[4];
		iep_dev->drcspacoff[1].bits.spa_coff2 = spatab[5];
		iep_dev->drcspacoff[2].bits.spa_coff0 = spatab[6];
		iep_dev->drcspacoff[2].bits.spa_coff1 = spatab[7];
		iep_dev->drcspacoff[2].bits.spa_coff2 = spatab[8];

		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Drc_Set_Int_Coeff(__u32 sel, __u8 inttab[IEP_DRC_INT_TAB_LEN])
{
	__u32 i;

	if (sel == 0) {
		for (i = 0; i < IEP_DRC_INT_TAB_LEN / 4; i++) {
			iep_dev->drcintcoff[i].bits.inten_coff0 = inttab[4 * i];
			iep_dev->drcintcoff[i].bits.inten_coff1 =
			    inttab[4 * i + 1];
			iep_dev->drcintcoff[i].bits.inten_coff2 =
			    inttab[4 * i + 2];
			iep_dev->drcintcoff[i].bits.inten_coff3 =
			    inttab[4 * i + 3];
		}

		return 0;
	} else {
		return -1;
	}
}

#if 0
__u32 DE_IEP_Drc_Set_Lgc_Coeff(__u32 sel, __u16 lgctab[IEP_DRC_INT_TAB_LEN])
{
	__u32 i;

	if (sel == 0)
		return 0;
	else
		return -1;
}
#endif

#define ____SEPARATOR_LH____

__u32 DE_IEP_Lh_Set_Mode(__u32 sel, __u32 mod)
{
	if (sel == 0) {
		/* 0:current frame case; 1:average case */
		iep_dev->lhctl.bits.lh_mod = mod;
		return 0;
	} else {
		return -1;
	}

}

__u32 DE_IEP_Lh_Clr_Rec(__u32 sel)
{
	if (sel == 0) {
		iep_dev->lhctl.bits.lh_rec_clr = 1;
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Lh_Set_Thres(__u32 sel, __u8 thres[])
{
	if (sel == 0) {
		iep_dev->lhthr0.bits.lh_thres_val1 = thres[0];
		iep_dev->lhthr0.bits.lh_thres_val2 = thres[1];
		iep_dev->lhthr0.bits.lh_thres_val3 = thres[2];
		iep_dev->lhthr0.bits.lh_thres_val4 = thres[3];
		iep_dev->lhthr1.bits.lh_thres_val5 = thres[4];
		iep_dev->lhthr1.bits.lh_thres_val6 = thres[5];
		iep_dev->lhthr1.bits.lh_thres_val7 = thres[6];
		return 0;
	} else {
		return -1;
	}
}

__u32 DE_IEP_Lh_Get_Sum_Rec(__u32 sel, __u32 *sum)
{
	__u32 i;

	if (sel == 0) {
		for (i = 0; i < IEP_LH_INTERVAL_NUM; i++)
			*sum++ = iep_dev->lhslum[i].bits.lh_lum_data;
		return 0;
	}

	return -1;
}

__u32 DE_IEP_Lh_Get_Cnt_Rec(__u32 sel, __u32 *cnt)
{
	__u32 i;

	if (sel == 0)
		for (i = 0; i < IEP_LH_INTERVAL_NUM; i++)
			*cnt++ = iep_dev->lhscnt[i].bits.lh_cnt_data;

	return 0;
}

#define ____SEPARATOR_DF____
