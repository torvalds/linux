/*
 * Copyright (C) 2009 QUALCOMM Incorporated.
 */

#include "mt9t013.h"
#include <linux/kernel.h>

struct reg_struct const mt9t013_reg_pat[2] = {
	{ /* Preview 2x2 binning 20fps, pclk MHz, MCLK 24MHz */
	/* vt_pix_clk_div:REG=0x0300 update get_snapshot_fps
	* if this change */
	8,

	/* vt_sys_clk_div: REG=0x0302  update get_snapshot_fps
	* if this change */
	1,

	/* pre_pll_clk_div REG=0x0304  update get_snapshot_fps
	* if this change */
	2,

	/* pll_multiplier  REG=0x0306 60 for 30fps preview, 40
	 * for 20fps preview
	 * 46 for 30fps preview, try 47/48 to increase further */
	46,

	/* op_pix_clk_div        REG=0x0308 */
	8,

	/* op_sys_clk_div        REG=0x030A */
	1,

	/* scale_m       REG=0x0404 */
	16,

	/* row_speed     REG=0x3016 */
	0x0111,

	/* x_addr_start  REG=0x3004 */
	8,

	/* x_addr_end    REG=0x3008 */
	2053,

	/* y_addr_start  REG=0x3002 */
	8,

	/* y_addr_end    REG=0x3006 */
	1541,

	/* read_mode     REG=0x3040 */
	0x046C,

	/* x_output_size REG=0x034C */
	1024,

	/* y_output_size REG=0x034E */
	768,

	/* line_length_pck    REG=0x300C */
	2616,

	/* frame_length_lines REG=0x300A */
	916,

	/* coarse_int_time REG=0x3012 */
	16,

	/* fine_int_time   REG=0x3014 */
	1461
	},
	{ /*Snapshot */
	/* vt_pix_clk_div  REG=0x0300 update get_snapshot_fps
	* if this change */
	8,

	/* vt_sys_clk_div  REG=0x0302 update get_snapshot_fps
	* if this change */
	1,

	/* pre_pll_clk_div REG=0x0304 update get_snapshot_fps
	 * if this change */
	2,

	/* pll_multiplier REG=0x0306 50 for 15fps snapshot,
	 * 40 for 10fps snapshot
	 * 46 for 30fps snapshot, try 47/48 to increase further */
	46,

	/* op_pix_clk_div        REG=0x0308 */
	8,

	/* op_sys_clk_div        REG=0x030A */
	1,

	/* scale_m       REG=0x0404 */
	16,

	/* row_speed     REG=0x3016 */
	0x0111,

	/* x_addr_start  REG=0x3004 */
	8,

	/* x_addr_end    REG=0x3008 */
	2071,

	/* y_addr_start  REG=0x3002 */
	8,

	/* y_addr_end    REG=0x3006 */
	1551,

	/* read_mode     REG=0x3040 */
	0x0024,

	/* x_output_size REG=0x034C */
	2064,

	/* y_output_size REG=0x034E */
	1544,

	/* line_length_pck REG=0x300C */
	2952,

	/* frame_length_lines    REG=0x300A */
	1629,

	/* coarse_int_time REG=0x3012 */
	16,

	/* fine_int_time REG=0x3014   */
	733
	}
};

struct mt9t013_i2c_reg_conf mt9t013_test_tbl[] = {
	{ 0x3044, 0x0544 & 0xFBFF },
	{ 0x30CA, 0x0004 | 0x0001 },
	{ 0x30D4, 0x9020 & 0x7FFF },
	{ 0x31E0, 0x0003 & 0xFFFE },
	{ 0x3180, 0x91FF & 0x7FFF },
	{ 0x301A, (0x10CC | 0x8000) & 0xFFF7 },
	{ 0x301E, 0x0000 },
	{ 0x3780, 0x0000 },
};

/* [Lens shading 85 Percent TL84] */
struct mt9t013_i2c_reg_conf mt9t013_lc_tbl[] = {
	{ 0x360A, 0x0290 }, /* P_RD_P0Q0 */
	{ 0x360C, 0xC92D }, /* P_RD_P0Q1 */
	{ 0x360E, 0x0771 }, /* P_RD_P0Q2 */
	{ 0x3610, 0xE38C }, /* P_RD_P0Q3 */
	{ 0x3612, 0xD74F }, /* P_RD_P0Q4 */
	{ 0x364A, 0x168C }, /* P_RD_P1Q0 */
	{ 0x364C, 0xCACB }, /* P_RD_P1Q1 */
	{ 0x364E, 0x8C4C }, /* P_RD_P1Q2 */
	{ 0x3650, 0x0BEA }, /* P_RD_P1Q3 */
	{ 0x3652, 0xDC0F }, /* P_RD_P1Q4 */
	{ 0x368A, 0x70B0 }, /* P_RD_P2Q0 */
	{ 0x368C, 0x200B }, /* P_RD_P2Q1 */
	{ 0x368E, 0x30B2 }, /* P_RD_P2Q2 */
	{ 0x3690, 0xD04F }, /* P_RD_P2Q3 */
	{ 0x3692, 0xACF5 }, /* P_RD_P2Q4 */
	{ 0x36CA, 0xF7C9 }, /* P_RD_P3Q0 */
	{ 0x36CC, 0x2AED }, /* P_RD_P3Q1 */
	{ 0x36CE, 0xA652 }, /* P_RD_P3Q2 */
	{ 0x36D0, 0x8192 }, /* P_RD_P3Q3 */
	{ 0x36D2, 0x3A15 }, /* P_RD_P3Q4 */
	{ 0x370A, 0xDA30 }, /* P_RD_P4Q0 */
	{ 0x370C, 0x2E2F }, /* P_RD_P4Q1 */
	{ 0x370E, 0xBB56 }, /* P_RD_P4Q2 */
	{ 0x3710, 0x8195 }, /* P_RD_P4Q3 */
	{ 0x3712, 0x02F9 }, /* P_RD_P4Q4 */
	{ 0x3600, 0x0230 }, /* P_GR_P0Q0 */
	{ 0x3602, 0x58AD }, /* P_GR_P0Q1 */
	{ 0x3604, 0x18D1 }, /* P_GR_P0Q2 */
	{ 0x3606, 0x260D }, /* P_GR_P0Q3 */
	{ 0x3608, 0xF530 }, /* P_GR_P0Q4 */
	{ 0x3640, 0x17EB }, /* P_GR_P1Q0 */
	{ 0x3642, 0x3CAB }, /* P_GR_P1Q1 */
	{ 0x3644, 0x87CE }, /* P_GR_P1Q2 */
	{ 0x3646, 0xC02E }, /* P_GR_P1Q3 */
	{ 0x3648, 0xF48F }, /* P_GR_P1Q4 */
	{ 0x3680, 0x5350 }, /* P_GR_P2Q0 */
	{ 0x3682, 0x7EAF }, /* P_GR_P2Q1 */
	{ 0x3684, 0x4312 }, /* P_GR_P2Q2 */
	{ 0x3686, 0xC652 }, /* P_GR_P2Q3 */
	{ 0x3688, 0xBC15 }, /* P_GR_P2Q4 */
	{ 0x36C0, 0xB8AD }, /* P_GR_P3Q0 */
	{ 0x36C2, 0xBDCD }, /* P_GR_P3Q1 */
	{ 0x36C4, 0xE4B2 }, /* P_GR_P3Q2 */
	{ 0x36C6, 0xB50F }, /* P_GR_P3Q3 */
	{ 0x36C8, 0x5B95 }, /* P_GR_P3Q4 */
	{ 0x3700, 0xFC90 }, /* P_GR_P4Q0 */
	{ 0x3702, 0x8C51 }, /* P_GR_P4Q1 */
	{ 0x3704, 0xCED6 }, /* P_GR_P4Q2 */
	{ 0x3706, 0xB594 }, /* P_GR_P4Q3 */
	{ 0x3708, 0x0A39 }, /* P_GR_P4Q4 */
	{ 0x3614, 0x0230 }, /* P_BL_P0Q0 */
	{ 0x3616, 0x160D }, /* P_BL_P0Q1 */
	{ 0x3618, 0x08D1 }, /* P_BL_P0Q2 */
	{ 0x361A, 0x98AB }, /* P_BL_P0Q3 */
	{ 0x361C, 0xEA50 }, /* P_BL_P0Q4 */
	{ 0x3654, 0xB4EA }, /* P_BL_P1Q0 */
	{ 0x3656, 0xEA6C }, /* P_BL_P1Q1 */
	{ 0x3658, 0xFE08 }, /* P_BL_P1Q2 */
	{ 0x365A, 0x2C6E }, /* P_BL_P1Q3 */
	{ 0x365C, 0xEB0E }, /* P_BL_P1Q4 */
	{ 0x3694, 0x6DF0 }, /* P_BL_P2Q0 */
	{ 0x3696, 0x3ACF }, /* P_BL_P2Q1 */
	{ 0x3698, 0x3E0F }, /* P_BL_P2Q2 */
	{ 0x369A, 0xB2B1 }, /* P_BL_P2Q3 */
	{ 0x369C, 0xC374 }, /* P_BL_P2Q4 */
	{ 0x36D4, 0xF2AA }, /* P_BL_P3Q0 */
	{ 0x36D6, 0x8CCC }, /* P_BL_P3Q1 */
	{ 0x36D8, 0xDEF2 }, /* P_BL_P3Q2 */
	{ 0x36DA, 0xFA11 }, /* P_BL_P3Q3 */
	{ 0x36DC, 0x42F5 }, /* P_BL_P3Q4 */
	{ 0x3714, 0xF4F1 }, /* P_BL_P4Q0 */
	{ 0x3716, 0xF6F0 }, /* P_BL_P4Q1 */
	{ 0x3718, 0x8FD6 }, /* P_BL_P4Q2 */
	{ 0x371A, 0xEA14 }, /* P_BL_P4Q3 */
	{ 0x371C, 0x6338 }, /* P_BL_P4Q4 */
	{ 0x361E, 0x0350 }, /* P_GB_P0Q0 */
	{ 0x3620, 0x91AE }, /* P_GB_P0Q1 */
	{ 0x3622, 0x0571 }, /* P_GB_P0Q2 */
	{ 0x3624, 0x100D }, /* P_GB_P0Q3 */
	{ 0x3626, 0xCA70 }, /* P_GB_P0Q4 */
	{ 0x365E, 0xE6CB }, /* P_GB_P1Q0 */
	{ 0x3660, 0x50ED }, /* P_GB_P1Q1 */
	{ 0x3662, 0x3DAE }, /* P_GB_P1Q2 */
	{ 0x3664, 0xAA4F }, /* P_GB_P1Q3 */
	{ 0x3666, 0xDC50 }, /* P_GB_P1Q4 */
	{ 0x369E, 0x5470 }, /* P_GB_P2Q0 */
	{ 0x36A0, 0x1F6E }, /* P_GB_P2Q1 */
	{ 0x36A2, 0x6671 }, /* P_GB_P2Q2 */
	{ 0x36A4, 0xC010 }, /* P_GB_P2Q3 */
	{ 0x36A6, 0x8DF5 }, /* P_GB_P2Q4 */
	{ 0x36DE, 0x0B0C }, /* P_GB_P3Q0 */
	{ 0x36E0, 0x84CE }, /* P_GB_P3Q1 */
	{ 0x36E2, 0x8493 }, /* P_GB_P3Q2 */
	{ 0x36E4, 0xA610 }, /* P_GB_P3Q3 */
	{ 0x36E6, 0x50B5 }, /* P_GB_P3Q4 */
	{ 0x371E, 0x9651 }, /* P_GB_P4Q0 */
	{ 0x3720, 0x1EAB }, /* P_GB_P4Q1 */
	{ 0x3722, 0xAF76 }, /* P_GB_P4Q2 */
	{ 0x3724, 0xE4F4 }, /* P_GB_P4Q3 */
	{ 0x3726, 0x79F8 }, /* P_GB_P4Q4 */
	{ 0x3782, 0x0410 }, /* POLY_ORIGIN_C */
	{ 0x3784, 0x0320 }, /* POLY_ORIGIN_R  */
	{ 0x3780, 0x8000 } /* POLY_SC_ENABLE */
};

struct mt9t013_reg mt9t013_regs = {
	.reg_pat = &mt9t013_reg_pat[0],
	.reg_pat_size = ARRAY_SIZE(mt9t013_reg_pat),
	.ttbl = &mt9t013_test_tbl[0],
	.ttbl_size = ARRAY_SIZE(mt9t013_test_tbl),
	.lctbl = &mt9t013_lc_tbl[0],
	.lctbl_size = ARRAY_SIZE(mt9t013_lc_tbl),
	.rftbl = &mt9t013_lc_tbl[0],	/* &mt9t013_rolloff_tbl[0], */
	.rftbl_size = ARRAY_SIZE(mt9t013_lc_tbl)
};


