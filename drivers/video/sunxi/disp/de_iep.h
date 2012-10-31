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

/*
 *  Display engine Image Enhancement Processor registers and interface
 * functions define for aw1625
 */

#ifndef __DE_IEP_H__
#define __DE_IEP_H__

#include "bsp_display.h"

#ifndef CONFIG_ARCH_SUN5I
#error IEP should only be used on sun5i
#endif

#define IEP_DRC_SPA_TAB_LEN	9
#define IEP_DRC_INT_TAB_LEN 256
#define IEP_DRC_LGC_TAB_LEN 256
#define IEP_LH_INTERVAL_NUM 8
#define IEP_LH_THRES_NUM    7

typedef union {
	__u32 dwval;
	struct {
		__u32 en:1;	/* bit0 */
		__u32 r0:7;	/* bit1~7 */
		__u32 mod:2;	/* bit8~9 */
		__u32 r1:6;	/* bit10~15 */
		__u32 sync_edge:1;	/* bit16 */
		__u32 field_parity:1;	/* bit17 */
		__u32 r2:13;	/* bit18~30 */
		__u32 bist_en:1;	/* bit31 */
	} bits;
} __imgehc_gnectl_reg_t;	/* 0x0 */

typedef union {
	__u32 dwval;
	struct {
		__u32 disp_w:12;	/* bit0~11 */
		__u32 r0:4;	/* bit12~15 */
		__u32 disp_h:12;	/* bit16~27 */
		__u32 r1:4;	/* bit31~28 */
	} bits;
} __imgehc_drcsize_reg_t;

typedef union {
	__u32 dwval;
	struct {
		__u32 db_en:1;	/* bit0 */
		__u32 dbrdy_ctl:1;	/* bit1 */
		__u32 r0:6;	/* bit2~7 */
		__u32 win_en:1;	/* bit8 */
		__u32 r1:23;	/* bit9~31 */
	} bits;
} __imgehc_drcctl_reg_t;	/* 0x10 */

typedef union {
	__u32 dwval;
	struct {
		__u32 lgc_addr:32;	/* bit0~31 */
	} bits;
} __imgehc_drclgc_staadd_reg_t;	/* 0x14 */

typedef union {
	__u32 dwval;
	struct {
		__u32 lgc_abslumshf:1;	/* bit0 */
		__u32 adjust_en:1;	/* bit1 */
		__u32 r0:6;	/* bit2~7 */
		__u32 lgc_abslumperval:8;	/* bit8~15 */
		__u32 r1:8;	/* bit16~23 */
		__u32 gain_autoload_dis:1;	/* bit24 */
		__u32 r2:7;	/* bit25~31 */
	} bits;
} __imgehc_drc_set_reg_t;	/* 0x18 */

typedef union {
	__u32 dwval;
	struct {
		__u32 win_left:12;	/* bit0~11 */
		__u32 r0:4;	/* bit12~15 */
		__u32 win_top:12;	/* bit16~27 */
		__u32 r1:4;	/* bit28~31 */
	} bits;
} __imgehc_drc_wp_reg0_t;	/* 0x1c */

typedef union {
	__u32 dwval;
	struct {
		__u32 win_right:12;	/* bit0~11 */
		__u32 r0:4;	/* bit12~15 */
		__u32 win_bottom:12;	/* bit16~27 */
		__u32 r1:4;	/* bit28~31 */
	} bits;
} __imgehc_drc_wp_reg1_t;	/* 0x20 */

typedef union {
	__u32 dwval;
	struct {
		__u32 wb_en:1;	/* bit0 */
		__u32 r0:7;	/* bit1~7 */
		__u32 wb_mode:1;	/* bit8 */
		__u32 r1:7;	/* bit9~15 */
		__u32 wb_ps:1;	/* bit16 */
		__u32 r2:7;	/* bit17~23 */
		__u32 field:1;	/* bit24 */
		__u32 r3:6;	/* bit25~30 */
		__u32 wb_sts:1;	/* bit31 */
	} bits;
} __imgehc_wbctl_reg_t;		/* 0x24 */

typedef union {
	__u32 dwval;
	struct {
		__u32 wb_addr:32;
	} bits;
} __imgehc_wbaddr_reg_t;	/* 0x28 */

typedef union {
	__u32 dwval;
	struct {
		__u32 linestride:32;
	} bits;
} __imgehc_wbline_reg_t;	/* 0x2c */

typedef union {
	__u32 dwval;
	struct {
		__u32 lh_rec_clr:1;	/* bit0 */
		__u32 lh_mod:1;	/* bit1 */
		__u32 r0:30;	/* bit2~31 */
	} bits;
} __imgehc_lhctl_reg_t;		/* 0x30 */

typedef union {
	__u32 dwval;
	struct {
		__u32 lh_thres_val1:8;	/* bit0~7 */
		__u32 lh_thres_val2:8;	/* bit8~15 */
		__u32 lh_thres_val3:8;	/* bit16~23 */
		__u32 lh_thres_val4:8;	/* bit24~31 */
	} bits;
} __imgehc_lhthr_reg0_t;	/* 0x34 */

typedef union {
	__u32 dwval;
	struct {
		__u32 lh_thres_val5:8;	/* bit0~7 */
		__u32 lh_thres_val6:8;	/* bit8~15 */
		__u32 lh_thres_val7:8;	/* bit16~23 */
		__u32 r0:8;	/* bit24~31 */
	} bits;
} __imgehc_lhthr_reg1_t;	/* 0x38 */

typedef union {
	__u32 dwval;
	struct {
		__u32 lh_lum_data:32;	/* bit0~31 */
	} bits;
} __imgehc_lhslum_reg_t;	/*  0x0040 ~ 0x005c */

typedef union {
	__u32 dwval;
	struct {
		__u32 lh_cnt_data:32;	/* bit0~31 */
	} bits;
} __imgehc_lhscnt_reg_t;	/* 0x0060 ~ 0x007c */

typedef union {
	__u32 dwval;
	struct {
		__u32 df_en:1;	/* bit0 */
		__u32 r0:7;	/* bit1~7 */
		__u32 df_y_bypass:1;	/* bit8 */
		__u32 df_u_bypass:1;	/* bit9 */
		__u32 df_v_bypass:1;	/* bit10 */
		__u32 r1:21;	/* bit11~31 */
	} bits;
} __imgehc_dfctl_reg_t;		/* 0x0080 */

typedef union {
	__u32 dwval;
	struct {
		__u32 csc_yg_coff:13;	/* bit0~12 */
		__u32 r0:19;	/* bit13~31 */
	} bits;
} __imgehc_cscygcoff_reg_t;	/* 0xc0~0xc8 */

typedef union {
	__u32 dwval;
	struct {
		__u32 csc_yg_con:14;	/* bit0~13 */
		__u32 r0:18;	/* bit14~31 */
	} bits;
} __imgehc_cscygcon_reg_t;	/* 0xcc */

typedef union {
	__u32 dwval;
	struct {
		__u32 csc_ur_coff:13;	/* bit0~12 */
		__u32 r0:19;	/* bit13~31 */
	} bits;
} __imgehc_cscurcoff_reg_t;	/* 0xd0~0xd8 */

typedef union {
	__u32 dwval;
	struct {
		__u32 csc_ur_con:14;	/* bit0~13 */
		__u32 r0:18;	/* bit14~31 */
	} bits;
} __imgehc_cscurcon_reg_t;	/* 0xdc */

typedef union {
	__u32 dwval;
	struct {
		__u32 csc_vb_coff:13;	/* bit0~12 */
		__u32 r0:19;	/* bit13~31 */
	} bits;
} __imgehc_cscvbcoff_reg_t;	/* 0xe0~0xe8 */

typedef union {
	__u32 dwval;
	struct {
		__u32 csc_vb_con:14;	/* bit0~13 */
		__u32 r0:18;	/* bit14~31 */
	} bits;
} __imgehc_cscvbcon_reg_t;	/* 0xec */

typedef union {
	__u32 dwval;
	struct {
		__u32 spa_coff0:8;	/* bit0~7 */
		__u32 spa_coff1:8;	/* bit8~15 */
		__u32 spa_coff2:8;	/* bit16~23 */
		__u32 r0:8;	/* bit24~31 */
	} bits;
} __imgehc_drcspacoff_reg_t;	/* 0xf0~0xf8 */

typedef union {
	__u32 dwval;
	struct {
		__u32 inten_coff0:8;	/* bit0~7 */
		__u32 inten_coff1:8;	/* bit8~15 */
		__u32 inten_coff2:8;	/* bit16~23 */
		__u32 inten_coff3:8;	/* bit24~31 */
	} bits;
} __imgehc_drcintcoff_reg_t;	/* 0x0100 ~ 0x01fc */

typedef union {
	__u32 dwval;
	struct {
		__u32 lumagain_coff0:16;	/* bit0~15 */
		__u32 lumagain_coff1:16;	/* bit16~31 */
	} bits;
} __imgehc_drclgcoff_reg_t;	/* 0x0200 ~ 0x03fc */

typedef struct {
	__imgehc_gnectl_reg_t gnectl;	/* 0x00 */
	__imgehc_drcsize_reg_t drcsize;	/* 0x04 */
	__u32 r0[2];		/* 0x08~0x0c */
	__imgehc_drcctl_reg_t drcctl;	/* 0x10 */
	__imgehc_drclgc_staadd_reg_t drclgc_addr;	/* 0x14 */
	__imgehc_drc_set_reg_t drc_set;	/* 0x18 */
	__imgehc_drc_wp_reg0_t drc_wp0;	/* 0x1c */
	__imgehc_drc_wp_reg1_t drc_wp1;	/* 0x20 */
	__imgehc_wbctl_reg_t wbctl;	/* 0x24 */
	__imgehc_wbaddr_reg_t wbaddr;	/* 0x28 */
	__imgehc_wbline_reg_t wbline;	/* 0x2c */
	__imgehc_lhctl_reg_t lhctl;	/* 0x30 */
	__imgehc_lhthr_reg0_t lhthr0;	/* 0x34 */
	__imgehc_lhthr_reg1_t lhthr1;	/* 0x38 */
	__u32 r2;		/* 0x3c */
	__imgehc_lhslum_reg_t lhslum[8];	/* 0x40~0x5c */
	__imgehc_lhscnt_reg_t lhscnt[8];	/* 0x0060 ~ 0x007c */
	__imgehc_dfctl_reg_t dfctl;	/* 0x80 */
	__u32 r3[15];		/* 0x84~0xbc */
	__imgehc_cscygcoff_reg_t cscygcoff[3];	/* 0xc0~0xc8 */
	__imgehc_cscygcon_reg_t cscygcon;	/* 0xcc */
	__imgehc_cscurcoff_reg_t cscurcoff[3];	/* 0xd0~0xd8 */
	__imgehc_cscurcon_reg_t cscurcon;	/* 0xdc */
	__imgehc_cscvbcoff_reg_t cscvbcoff[3];	/* 0xe0~0xe8 */
	__imgehc_cscvbcon_reg_t cscvbcon;	/* 0xec */
	__imgehc_drcspacoff_reg_t drcspacoff[3];	/* 0xf0~0xf8 */
	__u32 r4;		/* 0xff */
	__imgehc_drcintcoff_reg_t drcintcoff[64];	/* 0x0100 ~ 0x01fc */
	__imgehc_drclgcoff_reg_t drclgcoff[128];	/* 0x0200 ~ 0x03fc */
} __de_iep_dev_t;

#define ____SEPARATOR_IEP____

__s32 DE_IEP_Set_Reg_Base(__u32 sel, __u32 base);
__u32 DE_IEP_Get_Reg_Base(__u32 sel, __u32 base);
__s32 DE_IEP_Enable(__u32 sel);
__s32 DE_IEP_Disable(__u32 sel);
__s32 DE_IEP_Set_Mode(__u32 sel, __u32 mod);
__s32 DE_IEP_Bist_Enable(__u32 sel, __u32 en);
__s32 DE_IEP_Set_Reg_Refresh_Edge(__u32 sel, __u32 falling);
__s32 DE_IEP_Set_Csc_Coeff(__u32 sel, __u32 mod);
__s32 DE_IEP_Set_Display_Size(__u32 sel, __u32 width, __u32 height);
__s32 DE_IEP_Demo_Win_Enable(__u32 sel, __u32 en);
__u32 DE_IEP_Set_Demo_Win_Para(__u32 sel, __u32 top, __u32 bot, __u32 left,
			       __u32 right);
#define ____SEPARATOR_DRC____
__s32 DE_IEP_Drc_Cfg_Rdy(__u32 sel);
__u32 DE_IEP_Drc_Set_Lgc_Addr(__u32 sel, __u32 addr);
__u32 DE_IEP_Drc_Set_Lgc_Autoload_Disable(__u32 sel, __u32 disable);
__u32 DE_IEP_Drc_Adjust_Enable(__u32 sel, __u32 en);
__u32 DE_IEP_Drc_Set_Adjust_Para(__u32 sel, __u32 abslumperval,
				 __u32 abslumshf);
__u32 DE_IEP_Drc_Set_Spa_Coeff(__u32 sel, __u8 spatab[IEP_DRC_SPA_TAB_LEN]);
__u32 DE_IEP_Drc_Set_Int_Coeff(__u32 sel, __u8 inttab[IEP_DRC_INT_TAB_LEN]);
//__u32 DE_IEP_Drc_Set_Lgc_Coeff(__u32 sel,  __u16 lgctab[IEP_DRC_INT_TAB_LEN]);
#define ____SEPARATOR_LH____
__u32 DE_IEP_Lh_Set_Mode(__u32 sel, __u32 mod);
__u32 DE_IEP_Lh_Clr_Rec(__u32 sel);
__u32 DE_IEP_Lh_Set_Thres(__u32 sel, __u8 thres[IEP_LH_THRES_NUM]);
__u32 DE_IEP_Lh_Get_Sum_Rec(__u32 sel, __u32 *sum);
__u32 DE_IEP_Lh_Get_Cnt_Rec(__u32 sel, __u32 *cnt);
#define ____SEPARATOR_DF____
#endif
