/*
 * D2D3 register bit-field definition
 * Sorted by the appearing order of registers in register.h.
 *
 * Author: bai kele <kele.bai@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _D2D3_REGS_H
#define _D2D3_REGS_H

//#define D2D3_INTF_LENGTH                0x1a08
	#define V1_LINE_LENGTHM1_BIT		16
	#define V1_LINE_LENGTHM1_WID		13
	#define V0_LINE_LENGTHM1_BIT		0
	#define V0_LINE_LENGTHM1_WID		13

//#define D2D3_INTF_CTRL0                 0x1a09
	#define VD0_DOUT_SPLITTER_BIT		30
	#define VD0_DOUT_SPLITTER_WID		2
	#define VD1_DOUT_SPLITTER_BIT		28
	#define VD1_DOUT_SPLITTER_WID		2
	#define NRWR_DOUT_SPLITTER_BIT	        26
	#define NRWR_DOUT_SPLITTER_WID		2
	#define ON_CLK_D2D3_REG_BIT		23
	#define ON_CLK_D2D3_REG_WID		1
	#define ON_CLK_D2D3_BIT			22
	#define ON_CLK_D2D3_WID			1
	#define V1_GO_LINE_BIT			21
	#define V1_GO_LINE_WID			1
	#define V1_GO_FIELD_BIT			20
	#define V1_GO_FIELD_WID			1
	#define V0_GO_FIELD_BIT			19
	#define V0_GO_FIELD_WID			1
	#define V1_GOFLD_SEL_BIT		16
	#define V1_GOFLD_SEL_WID		3
	#define V0_GOFLD_SEL_BIT		13
	#define V0_GOFLD_SEL_WID 		3
	#define HOLE_LINES_DEPTH_BIT		6
	#define HOLE_LINES_DEPTH_WID		7
	#define D2D3_V1_SEL_BIT			4
	#define D2D3_V1_SEL_WID			2
	#define USE_VDIN_EOL_BIT		3
	#define USE_VDIN_EOL_WID		1
	#define D2D3_V0_SEL_BIT			0
	#define D2D3_V0_SEL_WID			3
//#define VPP_VSC_PHASE_CTRL      0x1d0d
        #define DOUBLE_LINE_MODE_BIT            17
        #define DOUBLE_LINE_MODE_WID            2
        #define OUTPUT_MODE_BIT                 16
        #define OUTPUT_MODE_WID                 1

//#define VPP_INPUT_CTRL                0x1dab     //default 12'h440
	#define VD2_SEL_BIT				9
	#define VD2_SEL_WID				3
	#define VD1_L_SEL_BIT				6
	#define VD1_L_SEL_WID				3
	#define VD1_R_SEL_BIT				3
	#define VD1_R_SEL_WID				3
	#define VD1_LEAVE_MOD_BIT			0
	#define VD1_LEAVE_MOD_WID			3


#define D2D3_REG_NUM                    0x40

#define D2D3_CBUS_BASE                0x2b00         // D2D3 address space


//#define D2D3_GLB_CTRL            0x2b00
	#define RD_LOCK_EN_BIT		        31
	#define RD_LOCK_EN_WID			1
	#define SW_RST_NOBUF_BIT		30
	#define SW_RST_NOBUF_WID		1
	#define  CLK_AUTO_DIS_BIT		28
	#define  CLK_AUTO_DIS_WID		2
	#define CLK_CTRL_BIT			16
	#define CLK_CTRL_WID			12
	#define GLB_CTRL_RS1_BIT		12
	#define GLB_CTRL_RS1_WID		4
	#define  LO_CHROMA_SIGN_BIT	        11
	#define LO_CHROMA_SIGN_WID		1
	#define  RO_CHROMA_SIGN_BIT	        10
	#define  RO_CHROMA_SIGN_WID		1
	#define vI0_CHROMA_SIGN_BIT	        9
	#define VI0_CHROMA_SIGN_WID		1
	#define VI1_CHROMA_SIGN_BIT	        8
	#define VI1_CHROMA_SIGN_WID		1
	#define GLB_CTRL_RS2_BIT		5
	#define GLB_CTRL_RS2_WID		3
	#define lG_EN_BIT			4
	#define lG_EN_WID			1
	#define MG_EN_BIT			3
	#define MG_EN_WID			1
	#define CG_EN_BIT			2
	#define CG_EN_WID			1
	#define DBR_EN_BIT			1
	#define DBR_EN_WID			1
	#define DPG_EN_BIT			0
	#define  DPG_EN_WID			1


//#define D2D3_DPG_INPIC_SIZE      0x2b01
	#define SZX_VI_M1_BIT		        16
	#define SZX_VI_M1_WID			16
	#define SZY_VI_M1_BIT		        0
	#define SZY_VI_M1_WID			16


//#define D2D3_DBR_OUTPIC_SIZE     0x2b02
	#define SZX_VO_M1_BIT		        16
	#define SZX_VO_M1_WID			16
	#define SZY_VO_M1_BIT		        0
	#define SZY_VO_M1_WID			16

//#define D2D3_DGEN_WIN_HOR        0x2b03
	#define DG_WIN_X_START_BIT	        16
	#define DG_WIN_X_START_WID		16
	#define DG_WIN_X_END_BIT		0
	#define DG_WIN_X_END_WID		16

//#define D2D3_DGEN_WIN_VER        0x2b04
	#define DG_WIN_Y_START_BIT		16
	#define DG_WIN_Y_START_WID		16
	#define DG_WIN_Y_END_BIT		0
	#define DG_WIN_Y_END_WID		16

//#define D2D3_PRE_SCD_H           0x2b05
	#define  SCD81_HPHS_STEP_BIT	        16
	#define  SCD81_HPHS_STEP_WID		16
	#define SCD81_HPHS_INI_BIT	        0
	#define SCD81_HPHS_INI_WID		16

//#define D2D3_SCALER_CTRL         0x2b06
	#define SCU18_INIPH_BIT		        16
	#define SCU18_INIPH_WID			16
	#define SCALER_RS_BIT			12
	#define SCALER_RS_WID			4
	#define SCD81_PREDROP_EN_BIT	        11
	#define SCD81_PREDROP_EN_WID		1
	#define CG_CSC_SEL_BIT			9
	#define CG_CSC_SEL_WID			2
	#define SCU18_REP_EN_BIT		8
	#define SCU18_REP_EN_WID		1
	#define SCU18_FACTOR_BIT		4
	#define SCU18_FACTOR_WID		4
	#define SCD81_FACTOR_BIT		0
	#define SCD81_FACTOR_WID		4

//#define D2D3_CG_THRESHOLD_1      0x2b07
	#define CG_RPG_DTH_BIT		        24
	#define CG_RPG_DTH_WID			8
	#define  CG_RPG_UTH_BIT		        16
	#define CG_RPG_UTH_WID			8
	#define CG_LUM_DTH_BIT		        8
	#define CG_LUM_DTH_WID			8
	#define CG_LUM_UTH_BIT		        0
	#define CG_LUM_UTH_WID			8

//#define D2D3_CG_THRESHOLD_2      0x2b08
	#define  CG_RPB_DTH_BIT		        24
	#define  CG_RPB_DTH_WID			8
	#define CG_RPB_UTH_BIT		        16
	#define CG_RPB_UTH_WID			8
	#define CG_BPG_DTH_BIT		        8
	#define CG_BPG_DTH_WID			8
	#define CG_BPG_UTH_BIT		        0
	#define CG_BPG_UTH_WID			8

//#define D2D3_CG_PARAM_1          0x2b09
	#define CG_VP_REL_K_BIT		        24
	#define CG_VP_REL_K_WID			8
	#define CG_VP_Y_THR_BIT		        16
	#define CG_VP_Y_THR_WID			8
	#define G_MEET_DVAL_BIT		        8
	#define G_MEET_DVAL_WID			8
	#define CG_UNMT_DVAL_BIT		0
	#define CG_UNMT_DVAL_WID		8

//#define D2D3_CG_PARAM_2          0x2b0a
	#define CG_VPOS_THR_BIT		        16
	#define CG_VPOS_THR_WID			16
	#define CG_PARAM_RS_BIT		        8
	#define CG_PARAM_RS_WID			8
	#define CG_VPOS_EN_BIT		        7
	#define CG_VPOS_EN_WID			1
	#define CG_VPOS_ADPT_EN_BIT	        6
	#define CG_VPOS_ADPT_EN_WID		1
	#define CG_LPF_BYPASS_BIT		4
	#define CG_LPF_BYPASS_WID		2
	#define CG_VP_REL_S_BIT		        0
	#define  CG_VP_REL_S_WID		4

//#define D2D3_PRE_SCD_V           0x2b0b
	#define SCD81_VPHS_STEP_BIT		16
	#define SCD81_VPHS_STEP_WID		16
	#define  SCD81_VPHS_INI_BIT		0
	#define SCD81_VPHS_INI_WID		16

//#define D2D3_D2P_PARAM_1         0x2b0c
	#define D2P_BRDWID_BIT			24
	#define D2P_BRDWID_WID			8
	#define D2P_PARAM_RS_BIT		22
	#define D2P_PARAM_RS_WID		2
	#define D2P_LOMODE_BIT			20
	#define D2P_LOMODE_WID			2
	#define D2P_NEG_BIT			19
	#define D2P_NEG_WID			1
	#define D2P_PARAM_RS2_BIT		18
	#define D2P_PARAM_RS2_WID		1
	#define  D2P_WRAP_EN_BIT		17
	#define  D2P_WRAP_EN_WID		1
	#define D2P_LAR_BIT			16
	#define D2P_LAR_WID			1
	#define D2P_LR_SWITCH_BIT		15
	#define D2P_LR_SWITCH_WID		1
	#define D2P_1DTOLR_BIT			14
	#define D2P_1DTOLR_WID			1
	#define D2P_OUT_MODE_BIT		12
	#define D2P_OUT_MODE_WID		2
	#define D2P_SMODE_BIT			8
	#define D2P_SMODE_WID			4
	#define D2P_OFFSET_BIT			0
	#define D2P_OFFSET_WID			8

//#define D2D3_D2P_PARAM_2         0x2b0d
	#define  D2P_PG0_BIT 		        24
	#define D2P_PG0_WID			8
	#define  D2P_PG1_BIT		        16
	#define  D2P_PG1_WID			8
	#define D2P_PT_BIT			8
	#define D2P_PT_WID			8
	#define D2P_PLIMIT_BIT		        0
	#define D2P_PLIMIT_WID		        8

//#define D2D3_D2P_PARAM_3         0x2b0e
	#define D2P_NG0_BIT		        24
	#define D2P_NG0_WID			8
	#define D2P_NG1_BIT		        16
	#define D2P_NG1_WID			8
	#define D2P_NT_BIT			8
	#define D2P_NT_WID			8
	#define D2P_NLIMIT_BIT		        0
	#define D2P_NLIMIT_WID		        8

//#define D2D3_SCU18_STEP          0x2b0f
	#define SCU18_STEP_RS_BIT		17
	#define SCU18_STEP_RS_WID		15
	#define SCU18_STEP_EN_BIT		16
	#define SCU18_STEP_EN_WID		1
	#define SCU18_HPHS_STEP_BIT	        8
	#define SCU18_HPHS_STEP_WID		8
	#define SCU18_VPHS_STEP_BIT	        0
	#define SCU18_VPHS_STEP_WID		8

//#define D2D3_DPF_LPF_CTRL        0x2b10
	#define DPF_LPF_CTRL_RS1_BIT		22
	#define DPF_LPF_CTRL_RS1_WID		10
	#define DB_LPF_BPCOEFF_BIT		20
	#define DB_LPF_BPCOEFF_WID		2
	#define   LG_LPF_BPCOEFF_BIT		18
	#define  LG_LPF_BPCOEFF_WID		2
	#define CG_LPF_BPCOEFF_BIT		16
	#define CG_LPF_BPCOEFF_WID		2
	#define DPF_LPF_CTRL_RS2_BIT		10
	#define DPF_LPF_CTRL_RS2_WID		6
	#define DB_LPF_BYPASS_BIT		8
	#define DB_LPF_BYPASS_WID		2
	#define LG_LPF_BYPASS_BIT		6
	#define LG_LPF_BYPASS_WID		2
	#define  LG_KC_BIT			0
	#define  LG_KC_WID			6

//#define D2D3_DBLD_CG_PARAM       0x2b11
	#define  DB_G2_CG_BIT			24
	#define  DB_G2_CG_WID			8
	#define DB_O2_CG_BIT			16
	#define DB_O2_CG_WID			8
	#define DB_G1_CG_BIT			8
	#define DB_G1_CG_WID			8
	#define DB_O1_CG_BIT			0
	#define DB_O1_CG_WID			8

//#define D2D3_DBLD_MG_PARAM       0x2b12
	#define  DB_G2_MG_BIT			24
	#define  DB_G2_MG_WID			8
	#define DB_O2_MG_BIT			16
	#define DB_O2_MG_WID			8
	#define DB_G1_MG_BIT			8
	#define DB_G1_MG_WID			8
	#define DB_O1_MG_BIT			0
	#define DB_O1_MG_WID			8

//#define D2D3_DBLD_LG_PARAM       0x2b13
	#define DB_G2_LG_BIT			24
	#define DB_G2_LG_WID			8
	#define DB_O2_LG_BIT			16
	#define DB_O2_LG_WID			8
	#define  DB_G1_LG_BIT			8
	#define  DB_G1_LG_WID			8
	#define DB_O1_LG_BIT			0
	#define DB_O1_LG_WID			8

//#define D2D3_DBLD_LPF_HCOEFF     0x2b14
	#define DB_FACTOR_BIT			24
	#define DB_FACTOR_WID			8
	#define  DB_HF_A_BIT			16
	#define DB_HF_A_WID			8
	#define DB_HF_B_BIT			8
	#define DB_HF_B_WID			8
	#define DB_HF_C_BIT			0
	#define DB_HF_C_WID			8

//#define D2D3_DBLD_LPF_VCOEFF     0x2b15
	#define DB_OWIN_FILL_BIT		24
	#define DB_OWIN_FILL_WID 		8
	#define  DB_VF_A_BIT			16
	#define  DB_VF_A_WID			8
	#define DB_VF_B_BIT			8
	#define DB_VF_B_WID			8
	#define DB_VF_C_BIT			0
	#define DB_VF_C_WID			8

//#define D2D3_DBLD_PATH_CTRL      0x2b16
	#define  HIST_DEPTH_IDX_BIT		28
	#define  HIST_DEPTH_IDX_WID		4
	#define DBLD_PATH_RS_BIT		26
	#define DBLD_PATH_RS_WID		2
	#define MBDG_DEP_NEG_BIT		25
	#define MBDG_DEP_NEG_WID		1
	#define  LBDG_DEP_NEG_BIT		24
	#define  LBDG_DEP_NEG_WID		1
	#define DB_F1_CTRL_BIT			16
	#define DB_F1_CTRL_WID			8
	#define DB_F2_CTRL_BIT			8
	#define DB_F2_CTRL_WID			8
	#define DB_FIFO0_SEL_BIT		4
	#define DB_FIFO0_SEL_WID		4
	#define DB_FIFO1_SEL_BIT		4
	#define DB_FIFO1_SEL_WID		4

//#define D2D3_SCU18_INPIC_SIZE    0x2b17
	#define SZY_SCUI_BIT		        16
	#define SZY_SCUI_WID			16
	#define SZX_SCUI_BIT		        0
	#define SZX_SCUI_WID			16

//#define D2D3_MBDG_CTRL           0x2b18
	#define MBDG_CTRL_RS_BIT		18
	#define MBDG_CTRL_RS_WID		14
	#define MG_VP_EN_BIT			17
	#define MG_VP_EN_WID			1
	#define MG_SW_EN_BIT			16
	#define MG_SW_EN_WID			1
	#define MG_OWIN_FILL_BIT		8
	#define MG_OWIN_FILL_WID		8
	#define MG_IIR_EN_BIT			7
	#define MG_IIR_EN_WID			1
	#define MG_IIR_BIT			0
	#define MG_IIR_WID			7

//#define D2D3_MBDG_PARAM_0        0x2b19
	#define  MG_DTL_PXL_LEFT_BIT		28
	#define  MG_DTL_PXL_LEFT_WID		4
	#define MG_DTL_PXL_RIGHT_BIT		24
	#define MG_DTL_PXL_RIGHT_WID		4
	#define MG_CX_SW_BIT			16
	#define MG_CX_SW_WID			8
	#define MG_UX_SW_BIT			8
	#define MG_UX_SW_WID			8
	#define MG_DX_SW_BIT			0
	#define MG_DX_SW_WID			8

//#define D2D3_MBDG_PARAM_1        0x2b1a
	#define MG_DTL_PXL_UP_BIT		28
	#define MG_DTL_PXL_UP_WID		4
	#define MG_DTL_PXL_DN_BIT		24
	#define MG_DTL_PXL_DN_WID		4
	#define MG_CY_SW_BIT			16
	#define MG_CY_SW_WID			8
	#define MG_UY_SW_BIT			8
	#define  MG_UY_SW_WID			8
	#define  MG_DY_SW_BIT			0
	#define MG_DY_SW_WID			8

//#define D2D3_MBDG_PARAM_2        0x2b1b
	#define MG_DTL_LN_UP_BIT		24
	#define MG_DTL_LN_UP_WID		8
	#define MG_DTL_LN_DN_BIT		16
	#define MG_DTL_LN_DN_WID		8
	#define MG_DTL_LN_LEFT_BIT		8
	#define MG_DTL_LN_LEFT_WID		8
	#define MG_DTL_LN_RIGHT_BIT		0
	#define MG_DTL_LN_RIGHT_WID		8

//#define D2D3_MBDG_PARAM_3        0x2b1c
	#define MG_Y_MAX_BIT		        24
	#define MG_Y_MAX_WID			8
	#define MG_Y_MIN_BIT		        16
	#define MG_Y_MIN_WID			8
	#define MG_X_MAX_BIT		        8
	#define MG_X_MAX_WID			8
	#define MG_X_MIN_BIT		        0
	#define MG_X_MIN_WID			8

//#define D2D3_MBDG_PARAM_4        0x2b1d
	#define MBDG_PARM_4RS_BIT		27
	#define MBDG_PARM_4RS_WID		5
	#define MG_Y_ADP_EN_BIT			26
	#define MG_Y_ADAPT_EN_WID		1
	#define MG_XMM_ADP_EN_BIT		25
	#define MG_XMM_ADP_EN_WID		1
	#define MG_X_ADP_EN_BIT			24
	#define MG_X_ADP_EN_WID			1
	#define  MG_YTRANS_1_BIT		20
	#define MG_YTRANS_1_WID			4
	#define  MG_XTRANS_1_BIT		16
	#define  MG_XTRANS_1_WID		4
	#define MG_YK_0_BIT			8
	#define MG_YK_0_WID			8
	#define MG_XK_0_BIT			0
	#define MG_XK_0_WID			8

//#define D2D3_MBDG_PARAM_5        0x2b1e
	#define  MG_YSU3_BIT		        24
	#define  MG_YSU3_WID			8
	#define MG_YSU2_BIT		        16
	#define MG_YSU2_WID			8
	#define MG_YSU1_BIT		        8
	#define MG_YSU1_WID			8
	#define MG_YSU0_BIT		        0
	#define MG_YSU0_WID			8

//#define D2D3_MBDG_PARAM_6        0x2b1f
	#define MG_XSU3_BIT		        24
	#define MG_XSU3_WID			8
	#define  MG_XSU2_BIT		        16
	#define  MG_XSU2_WID			8
	#define MG_XSU1_BIT		        8
	#define MG_XSU1_WID			8
	#define  MG_XSU0_BIT		        0
	#define  MG_XSU0_WID			8

//#define D2D3_MBDG_PARAM_7        0x2b20
	#define MBDG_PARM_7RS_BIT	        16
	#define MBDG_PARM_7RS_WID		16
	#define MG_XSU4_BIT			8
	#define MG_XSU4_WID			8
	#define MG_YSU4_BIT			0
	#define MG_YSU4_WID			8

//#define D2D3_DBG_CTRL            0x2b23
	#define DBG_HSCNT_SEL_BIT		28
	#define DBG_HSCNT_SEL_WID		4
	#define DBG_CTRL_RS_BIT		        25
	#define DBG_CTRL_RS_WID			3
	#define DBG_DBR_EN_BIT		        24
	#define DBG_DBR_EN_WID			1
	#define DBG_FORCE_DATA_BIT	        16
	#define DBG_FORCE_DATA_WID		8
	#define DBG_BLD_CTRL_BIT		12
	#define DBG_BLD_CTRL_WID		4
	#define DBG_MG_CTRL_BIT		        8
	#define DBG_MG_CTRL_WID			4
	#define DBG_CG_CTRL_BIT		        4
	#define DBG_CG_CTRL_WID			4
	#define  DBG_LG_CTRL_BIT		0
	#define DBG_LG_CTRL_WID			4

//------------------------------------------------------------------------------
// DWMIF registers
//------------------------------------------------------------------------------

//#define D2D3_DWMIF_CTRL          0x2b24
	#define DWMIF_CTRL_RS_BIT		18
	#define DWMIF_CTRL_RS_WID 		14
	#define  DW_X_REV_BIT			17
	#define  DW_X_REV_WID			1
	#define DW_Y_REV_BIT			16
	#define DW_Y_REV_WID			1
	#define DW_DONE_CLR_BIT		        15
	#define DW_DONE_CLR_WID			1
	#define DW_LITTLE_ENDIAN_BIT	        14
	#define DW_LITTLE_ENDIAN_WID		1
	#define  DW_PIC_STRUCT_BIT	        12
	#define  DW_PIC_STRUCT_WID		2
	#define DW_URGENT_BIT			11
	#define DW_URGENT_WID			1
	#define DW_CLR_WRRSP_BIT		10
	#define DW_CLR_WRRSP_WID		1
	#define DW_CANVAS_WR_BIT		9
	#define DW_CANVAS_WR_WID		1
	#define DW_REQ_EN_BIT			8
	#define DW_REQ_EN_WID			1
	#define DW_CANVAS_INDEX_BIT	        0
	#define DW_CANVAS_INDEX_WID		8

//#define D2D3_DWMIF_HPOS          0x2b25
	#define DWMIF_HPOS_RS1_BIT	        31
	#define DWMIF_HPOS_RS1_WID	        1
	#define  DW_END_X_BIT			16
	#define  DW_END_X_WID			5
	#define DWMIF_HPOS_RS2_BIT		15
	#define DWMIF_HPOS_RS2_WID		1
	#define  DW_START_X_BIT		        0
	#define  DW_START_X_WID		        15

//#define D2D3_DWMIF_VPOS          0x2b26
	#define DWMIF_VPOS_RS1_BIT		29
	#define DWMIF_VPOS_RS1_WID		3
	#define  DW_END_Y_BIT			16
	#define  DW_END_Y_WID			13
	#define DWMIF_VPOS_RS2_BIT	        13
	#define DWMIF_VPOS_RS2_WID	        3
	#define  DW_START_Y_BIT		        0
	#define  DW_START_Y_WID		        13

//#define D2D3_DWMIF_SIZE          0x2b27
	#define DWMIF_SIZE_RS1_BIT		28
	#define DWMIF_SIZE_RS1_WID		4
	#define  DW_VSIZEM1_BIT		        16
	#define  DW_VSIZEM1_WID		        12
	#define DWMIF_SIZE_RS2_BIT	        12
	#define DWMIF_SIZE_RS2_WID	        4
	#define DW_HSIZEM1_BIT		        0
	#define  DW_HSIZEM1_WID		        12

//------------------------------------------------------------------------------
// DRMIF registers
//------------------------------------------------------------------------------

//#define D2D3_DRMIF_CTRL          0x2b28
	#define DRMIF_CTRL_RS_BIT		18
	#define DRMIF_CTRL_RS_WID		14
	#define DR_Y_REV_BIT		        17
	#define DR_Y_REV_WID		        1
	#define DR_X_REV_BIT		        16
	#define DR_X_REV_WID		        1
	#define DR_CLR_FIFO_ERR_BIT	        15
	#define DR_CLR_FIFO_ERR_WID	        1
	#define DR_LITTLE_ENDIAN_BIT	        14
	#define DR_LITTLE_ENDIAN_WID	        1
	#define DR_PIC_STRUCT_BIT		12
	#define DR_PIC_STRUCT_WID		2
	#define DR_URGENT_BIT			11
	#define DR_URGENT_WID			1
	#define DR_BURST_SIZE_BIT		9
	#define DR_BURST_SIZE_WID		2
	#define DR_REQ_EN_BIT			8
	#define DR_REQ_EN_WID			1
	#define DR_CANVAS_INDEX_BIT		0
	#define DR_CANVAS_INDEX_WID		8

//#define D2D3_DRMIF_HPOS          0x2b29
	#define DRMIF_HPOS_RS1_BIT		31
	#define DRMIF_HPOS_RS1_WID		0
	#define DR_END_X_BIT			16
	#define DR_END_X_WID			15
	#define DRMIF_HPOS_RS2_BIT	        15
	#define DRMIF_HPOS_RS2_WID	        1
	#define DR_START_X_BIT		        0
	#define DR_START_X_WID		        15

//#define D2D3_DRMIF_VPOS          0x2b2a
	#define DRMIF_VPOS_RS1_BIT		29
	#define DRMIF_VPOS_RS1_WID		3
	#define  DR_END_Y_BIT		        16
	#define  DR_END_Y_WID		        13
	#define DRMIF_VPOS_RS2_BIT	        13
	#define DRMIF_VPOS_RS2_WID	        3
	#define DR_START_Y_BIT	                0
	#define DR_START_Y_WID	                13

//------------------------------------------------------------------------------
// PDR registers
// ddd: parallax based render
//------------------------------------------------------------------------------

//#define D2D3_DBR_DDD_CTRL        0x2b2c
	#define DBR_DDD_RS_BIT		        8
	#define DBR_DDD_RS_WID 		        24
	#define DDD_BRDLPF_EN_BIT		7
	#define DDD_BRDLPF_EN_WID	        1
	#define DDD_EXTN_BLACK_BIT	        6
	#define DDD_EXTN_BLACK_WID	        1
	#define DDD_WRAP_EN_BIT		        5
	#define DDD_WRAP_EN_WID		        1
	#define DDD_HHALF_BIT			4
	#define DDD_HHALF_WID			1
	#define DDD_OUT_MODE_BIT		2
	#define DDD_OUT_MODE_WID		2
	#define  DDD_LOMODE_BIT		        0
	#define  DDD_LOMODE_WID		        2


//#define D2D3_DBR_DDD_DBG         0x2b2d
	#define DBR_DDD_DBG_RS_BIT		0
	#define DBR_DDD_DBG_RS_WID		32


//------------------------------------------------------------------------------
// LRDMX registers
//------------------------------------------------------------------------------

//#define D2D3_DBR_LRDMX_CTRL      0x2b2f
	#define DBR_LRDMX_RS_BIT		9
	#define DBR_LRDMX_RS_WID		23
	#define  LR_MERGE_BIT			8
	#define  LR_MERGE_WID			1
	#define LRD_FF0_SEL_BIT		        6
	#define LRD_FF0_SEL_WID		        2
	#define  LRD_FF1_SEL_BIT		4
	#define  LRD_FF1_SEL_WID		2
	#define LRD_LOUT_SEL_BIT		2
	#define LRD_LOUT_SEL_WID		2
	#define LRD_ROUT_SEL_BIT		0
	#define LRD_ROUT_SEL_WID		2

//-----------------------------------------------------------------------
// Read Only registers
//-----------------------------------------------------------------------
//#define D2D3_CBDG_STATUS_1       0x2b30
	#define  RO_CG_VPREL_BIT		24
	#define  RO_CG_VPREL_WID		8
	#define RO_CG_VPX_BIT			12
	#define RO_CG_VPX_WID			12
	#define RO_CG_VPY_BIT			0
	#define RO_CG_VPY_WID			12

//#define D2D3_MBDG_STATUS_1       0x2b31
	#define  STATS1_MG_CX_BIT	        24
	#define  STATS1_MG_CX_WID	        8
	#define RO_MG_UX_BIT		        16
	#define RO_MG_UX_WID		        8
	#define RO_MG_DX_BIT		        8
	#define RO_MG_DX_WID		        8
	#define RO_MG_MINX_BIT		        0
	#define RO_MG_MINX_WID		        8

//#define D2D3_MBDG_STATUS_2       0x2b32
	#define STATS2_MG_CY_BIT		24
	#define STATS2_MG_CY_WID		8
	#define  RO_MG_UY_BIT			16
	#define  RO_MG_UY_WID			8
	#define   RO_MG_DY_BIT			8
	#define   RO_MG_DY_WID		        8
	#define  RO_MG_MINY_BIT		        0
	#define  RO_MG_MINY_WID		        8

//#define D2D3_MBDG_STATUS_3       0x2b33
	#define RO_WRAP_STATUS_BIT		31
	#define RO_WRAP_STATUS_WID		1
	#define MBDG_STATS3_RS_BIT		8
	#define MBDG_STATS3_RS_WID		23
	#define STATS3_MG_CY_BIT		4
	#define STATS3_MG_CY_WID		4
	#define STATS3_RO_MG_CX_BIT		0
	#define STATS3_RO_MG_CX_WID		4

//#define D2D3_MBDG_STATUS_4       0x2b34
	#define MBDG_STATS4_RS_BIT		21
	#define MBDG_STATS4_RS_WID	        11
	#define RO_MG_SUM_U_BIT		        0
	#define RO_MG_SUM_U_WID		        21

//#define D2D3_MBDG_STATUS_5       0x2b35
	#define MBDG_STATS5_RS_BIT 		21
	#define MBDG_STATS5_RS_WID 		11
	#define RO_MG_SUM_D_BIT		        0
	#define RO_MG_SUM_D_WID		        21

//#define D2D3_MBDG_STATUS_6       0x2b36
	#define MBDG_STATS6_RS_BIT		21
	#define MBDG_STATS6_RS_WID		11
	#define RO_MG_SUM_L_BIT			0
	#define RO_MG_SUM_L_WID		        21

//#define D2D3_MBDG_STATUS_7       0x2b37
	#define MBDG_STATS7_RS_BIT		21
	#define MBDG_STATS7_RS_WID		11
	#define  RO_MG_SUM_R_BIT		0
	#define  RO_MG_SUM_R_WID		21

//#define D2D3_DBG_STATUS_1        0x2b38
	#define DBG_HANDSHAKE_RO0_BIT		0
	#define  DBG_HANDSHAKE_RO0_WID		32

//#define D2D3_DBG_STATUS_2        0x2b39
	#define  DBG_HSCNT_BIT		        0
	#define  DBG_HSCNT_WID		        32

//#define D2D3_DRMIF_STATUS        0x2b3a
	#define DRMIF_STATS_BIT		        0
	#define DRMIF_STATS_WID		        32

//#define D2D3_DWMIF_STATUS        0x2b3b
	#define  DWMIF_STATS_BIT		2
	#define DWMIF_STATS_WID		        30
	#define D2D3_STATS0_BIT		        0
	#define D2D3_STATS0_WID		        2

//#define D2D3_CBDG_STATUS_2       0x2b3c
	#define CBDG_STATS2_RS_BIT	        24
	#define CBDG_STATS2_RS_WID 	        8
	#define RO_MEET_SUM_BIT		        0
	#define RO_MEET_SUM_WID		        24

//#define D2D3_DBLD_STATUS         0x2b3d
	#define  DBLD_STATS_BIT
	#define  DBLD_STATS_WID
	#define RO_HIST_DEPTH_BIT
	#define RO_HIST_DEPTH_WID

//#define D2D3_RESEV_STATUS1       0x2b3e
	#define RESEV_STATS1_RS_BIT	        0
	#define RESEV_STATS1_RS_WID	        32

//#define D2D3_RESEV_STATUS2       0x2b3f
	#define RESEV_STATS2_RS_BIT  		0
	#define RESEV_STATS2_RS_WID		32

#endif
