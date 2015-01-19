#ifndef _DET3D_H
#define _DET3D_H

#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TV)||(MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
//***************************************************************************
//******** DET3D REGISTERS ********
//***************************************************************************
#define DET3D_BASE_ADD						0x1734

//#define NR2_SW_EN						0x174f
    #define DET3D_EN_BIT					5
    #define DET3D_EN_WID					1

//#define DET3D_MOTN_CFG					0x1734  
    #define DET3D_INTR_EN_BIT					16
    #define DET3D_INTR_EN_WID					1
    #define DET3D_MOTION_MODE_BIT				8
    #define DET3D_MOTION_MODE_WID				2
    #define DET3D_MOTION_CORE_RATE_BIT				4
    #define DET3D_MOTION_CORE_RATE_WID				4
    #define DET3D_MOTION_CORE_THRD_BIT				0
    #define DET3D_MOTION_CORE_THRD_WID				4

//#define DET3D_CB_CFG						0x1735
    #define DET3D_CHESSBD_NHV_OFST_BIT				4
    #define DET3D_CHESSBD_NHV_OFST_WID				4
    #define DET3D_CHESSBD_HV_OFST_BIT				0
    #define DET3D_CHESSBD_HV_OFST_WID				4

//#define DET3D_SPLT_CFG					0x1736
    #define DET3D_SPLITVALID_RATIO_BIT				4
    #define DET3D_SPLITVALID_RATIO_WID				4
    #define DET3D_AVGLDX_RATIO_BIT				0
    #define DET3D_AVGLDX_RATIO_WID				4

//#define DET3D_HV_MUTE						0x1737
    #define DET3D_EDGE_VER_MUTE_BIT				20
    #define DET3D_EDGE_VER_MUTE_WID				4
    #define DET3D_EDGE_HOR_MUTE_BIT				16
    #define DET3D_EDGE_HOR_MUTE_WID				4
    #define DET3D_CHESSBD_VER_MUTE_BIT				12
    #define DET3D_CHESSBD_VER_MUTE_WID				4
    #define DET3D_CHESSBD_HOR_MUTE_BIT				8
    #define DET3D_CHESSBD_HOR_MUTE_WID				4
    #define DET3D_STA8X8_VER_MUTE_BIT				4
    #define DET3D_STA8X8_VER_MUTE_WID				4
    #define DET3D_STA8X8_HOR_MUTE_BIT				0
    #define DET3D_STA8X8_HOR_MUTE_WID				4

//#define DET3D_MAT_STA_P1M1					0x1738
    #define DET3D_STA8X8_P1_K0_R8_BIT				24
    #define DET3D_STA8X8_P1_K0_R8_WID				8
    #define DET3D_STA8X8_P1_K1_R7_BIT				16
    #define DET3D_STA8X8_P1_K1_R7_WID				8
    #define DET3D_STA8X8_M1_K0_R6_BIT				8
    #define DET3D_STA8X8_M1_K0_R6_WID				8
    #define DET3D_STA8X8_M1_K1_R6_BIT				0
    #define DET3D_STA8X8_M1_K1_R6_WID				8

//#define DET3D_MAT_STA_P1TH					0x1739
    #define DET3D_STAYUV_P1_TH_L4_BIT				16
    #define DET3D_STAYUV_P1_TH_L4_WID				8
    #define DET3D_STAEDG_P1_TH_L4_BIT				8
    #define DET3D_STAEDG_P1_TH_L4_WID				8
    #define DET3D_STAMOT_P1_TH_L4_BIT				0
    #define DET3D_STAMOT_P1_TH_L4_WID				8

//#define DET3D_MAT_STA_M1TH					0x173a
    #define DET3D_STAYUV_M1_TH_L4_BIT				16
    #define DET3D_STAYUV_M1_TH_L4_WID				8
    #define DET3D_STAEDG_M1_TH_L4_BIT				8
    #define DET3D_STAEDG_M1_TH_L4_WID				8
    #define DET3D_STAMOT_M1_TH_L4_BIT				0
    #define DET3D_STAMOT_M1_TH_L4_WID				8

//#define DET3D_MAT_STA_RSFT					0x173b
    #define DET3D_STAYUV_RSHFT_BIT				4
    #define DET3D_STAYUV_RSHFT_WID				2
    #define DET3D_STAEDG_RSHFT_BIT				2
    #define DET3D_STAEDG_RSHFT_WID				2
    #define DET3D_STAMOT_RSHFT_BIT				0
    #define DET3D_STAMOT_RSHFT_WID				2

//#define DET3D_MAT_SYMTC_TH					0x173c
    #define DET3D_STALUM_SYMTC_TH_BIT				24
    #define DET3D_STALUM_SYMTC_TH_WID				8
    #define DET3D_STACHR_SYMTC_TH_BIT				16
    #define DET3D_STACHR_SYMTC_TH_WID				8
    #define DET3D_STAEDG_SYMTC_TH_BIT				8
    #define DET3D_STAEDG_SYMTC_TH_WID				8
    #define DET3D_STAMOT_SYMTC_TH_BIT				0
    #define DET3D_STAMOT_SYMTC_TH_WID				8

//#define DET3D_RO_DET_CB_HOR					0x173d
    #define DET3D_CHESSBD_NHOR_VALUE_BIT			16
    #define DET3D_CHESSBD_NHOR_VALUE_WID			16
    #define DET3D_CHESSBD_HOR_VALUE_BIT				0
    #define DET3D_CHESSBD_HOR_VALUE_WID				16

//#define DET3D_RO_DET_CB_VER					0x173e
    #define DET3D_CHESSBD_NVER_VALUE_BIT			16
    #define DET3D_CHESSBD_NVER_VALUE_WID			16
    #define DET3D_CHESSBD_VER_VALUE_BIT				0
    #define DET3D_CHESSBD_VER_VALUE_WID				16

//#define DET3D_RO_SPLT_HT					0x173f
    #define DET3D_SPLIT_HT_VAILID_BIT				24
    #define DET3D_SPLIT_HT_VAILID_WID				1
    #define DET3D_SPLIT_HT_PXNUM_BIT				16
    #define DET3D_SPLIT_HT_PXNUM_WID				5
    #define DET3D_SPLIT_HT_IDXX4_BIT				0
    #define DET3D_SPLIT_HT_IDXX4_WID				10

//#define DET3D_RO_SPLT_HB					0x1780
    #define DET3D_SPLIT_HB_VAILID_BIT				24
    #define DET3D_SPLIT_HB_VAILID_WID				1
    #define DET3D_SPLIT_HB_PXNUM_BIT				16
    #define DET3D_SPLIT_HB_PXNUM_WID				5
    #define DET3D_SPLIT_HB_IDXX4_BIT				0
    #define DET3D_SPLIT_HB_IDXX4_WID				10

//#define DET3D_RO_SPLT_VL					0x1781
    #define DET3D_SPLIT_VL_VAILID_BIT				24
    #define DET3D_SPLIT_VL_VAILID_WID				1
    #define DET3D_SPLIT_VL_PXNUM_BIT				16
    #define DET3D_SPLIT_VL_PXNUM_WID				5
    #define DET3D_SPLIT_VL_IDXX4_BIT				0
    #define DET3D_SPLIT_VL_IDXX4_WID				10

//#define DET3D_RO_SPLT_VR					0x1782
    #define DET3D_SPLIT_VR_VAILID_BIT				24
    #define DET3D_SPLIT_VR_VAILID_WID				1
    #define DET3D_SPLIT_VR_PXNUM_BIT				16
    #define DET3D_SPLIT_VR_PXNUM_WID				5
    #define DET3D_SPLIT_VR_IDXX4_BIT				0
    #define DET3D_SPLIT_VR_IDXX4_WID				10

//#define DET3D_RO_MAT_LUMA_LR					0x1783
    #define DET3D_LUMA_LR_SUM_BIT				24
    #define DET3D_LUMA_LR_SUM_WID				5
    #define DET3D_LUMA_LR_SYMTC_BIT				16
    #define DET3D_LUMA_LR_SYMTC_WID				8
    #define DET3D_LUMA_LR_SCORE_BIT				0
    #define DET3D_LUMA_LR_SCORE_WID				16

//#define DET3D_RO_MAT_LUMA_TB					0x1784
    #define DET3D_LUMA_TB_SUM_BIT				24
    #define DET3D_LUMA_TB_SUM_WID				5
    #define DET3D_LUMA_TB_SYMTC_BIT				16
    #define DET3D_LUMA_TB_SYMTC_WID				8
    #define DET3D_LUMA_TB_SCORE_BIT				0
    #define DET3D_LUMA_TB_SCORE_WID				16

//#define DET3D_RO_MAT_CHRU_LR					0x1785
    #define DET3D_CHRU_LR_SUM_BIT				24
    #define DET3D_CHRU_LR_SUM_WID				5
    #define DET3D_CHRU_LR_SYMTC_BIT				16
    #define DET3D_CHRU_LR_SYMTC_WID				8
    #define DET3D_CHRU_LR_SCORE_BIT				0
    #define DET3D_CHRU_LR_SCORE_WID				16

//#define DET3D_RO_MAT_CHRU_TB					0x1786
    #define DET3D_CHRU_TB_SUM_BIT				24
    #define DET3D_CHRU_TB_SUM_WID				5
    #define DET3D_CHRU_TB_SYMTC_BIT				16
    #define DET3D_CHRU_TB_SYMTC_WID				8
    #define DET3D_CHRU_TB_SCORE_BIT				0
    #define DET3D_CHRU_TB_SCORE_WID				16

//#define DET3D_RO_MAT_CHRV_LR					0x1787
    #define DET3D_CHRV_LR_SUM_BIT				24
    #define DET3D_CHRV_LR_SUM_WID				5
    #define DET3D_CHRV_LR_SYMTC_BIT				16
    #define DET3D_CHRV_LR_SYMTC_WID				8
    #define DET3D_CHRV_LR_SCORE_BIT				0
    #define DET3D_CHRV_LR_SCORE_WID				16

//#define DET3D_RO_MAT_CHRV_TB					0x1788
    #define DET3D_CHRV_TB_SUM_BIT				24
    #define DET3D_CHRV_TB_SUM_WID				5
    #define DET3D_CHRV_TB_SYMTC_BIT				16
    #define DET3D_CHRV_TB_SYMTC_WID				8
    #define DET3D_CHRV_TB_SCORE_BIT				0
    #define DET3D_CHRV_TB_SCORE_WID				16

//#define DET3D_RO_MAT_HEDG_LR					0x1789
    #define DET3D_HEDG_LR_SUM_BIT				24
    #define DET3D_HEDG_LR_SUM_WID				5
    #define DET3D_HEDG_LR_SYMTC_BIT				16
    #define DET3D_HEDG_LR_SYMTC_WID				8
    #define DET3D_HEDG_LR_SCORE_BIT				0
    #define DET3D_HEDG_LR_SCORE_WID				16

//#define DET3D_RO_MAT_HEDG_TB					0x178a
    #define DET3D_HEDG_TB_SUM_BIT				24
    #define DET3D_HEDG_TB_SUM_WID				5
    #define DET3D_HEDG_TB_SYMTC_BIT				16
    #define DET3D_HEDG_TB_SYMTC_WID				8
    #define DET3D_HEDG_TB_SCORE_BIT				0
    #define DET3D_HEDG_TB_SCORE_WID				16

//#define DET3D_RO_MAT_VEDG_LR					0x178b
    #define DET3D_VEDG_LR_SUM_BIT				24
    #define DET3D_VEDG_LR_SUM_WID				5
    #define DET3D_VEDG_LR_SYMTC_BIT				16
    #define DET3D_VEDG_LR_SYMTC_WID				8
    #define DET3D_VEDG_LR_SCORE_BIT				0
    #define DET3D_VEDG_LR_SCORE_WID				16

//#define DET3D_RO_MAT_VEDG_TB					0x178c
    #define DET3D_VEDG_TB_SUM_BIT				24
    #define DET3D_VEDG_TB_SUM_WID				5
    #define DET3D_VEDG_TB_SYMTC_BIT				16
    #define DET3D_VEDG_TB_SYMTC_WID				8
    #define DET3D_VEDG_TB_SCORE_BIT				0
    #define DET3D_VEDG_TB_SCORE_WID				16

//#define DET3D_RO_MAT_MOTN_LR					0x178d
    #define DET3D_MOTN_LR_SUM_BIT				24
    #define DET3D_MOTN_LR_SUM_WID				5
    #define DET3D_MOTN_LR_SYMTC_BIT				16
    #define DET3D_MOTN_LR_SYMTC_WID				8
    #define DET3D_MOTN_LR_SCORE_BIT				0
    #define DET3D_MOTN_LR_SCORE_WID				16

//#define DET3D_RO_MAT_MOTN_TB					0x178e
    #define DET3D_MOTN_TB_SUM_BIT				24
    #define DET3D_MOTN_TB_SUM_WID				5
    #define DET3D_MOTN_TB_SYMTC_BIT				16
    #define DET3D_MOTN_TB_SYMTC_WID				8
    #define DET3D_MOTN_TB_SCORE_BIT				0
   #define DET3D_MOTN_TB_SCORE_WID				16

//#define DET3D_RO_FRM_MOTN					0x178f
   #define DET3D_FRAME_MOTION_BIT				0
   #define DET3D_FRAME_MOTION_WID				16

//#define DET3D_RAMRD_ADDR_PORT					0x179a

//#define DET3D_RAMRD_DATA_PORT					0x179b

//***************************************************************************
//*** STRUCTURE DEFINATIONS *********************************************
//***************************************************************************
typedef struct det3d_info_s {
	//Frame counter, max number is defined in FRAME_MAX macro
	int nfrm;

	//3D format 
	int tfw_det3d_fmt;

	//signed number, when the score is close to 0, it is means we can not confirm if input is LR format;
	//the smaller this score is (when < 0), the more likely input is not LR format;
	//the larger this score is (when > 0), the more likely input is  LR format;
	int score_3d_lr;
	int score_3d_tb;

	//the last 8 frame score_3d_lr, tscore_3d_lr[0] is caculated from current frame data;
	int tscore_3d_lr[8];
	int tscore_3d_tb[8];

	//
	int tscore_3d_lr_accum;
	int tscore_3d_tb_accum; 

	//
	int score_3d_chs;
	int score_3d_int;

	//the last 8 frame valid score_3d_chs, chs_valid_his[0] is caculated from current frame data;
	int chs_valid_his[8];
	int int_valid_his[8];

}det3d_info_t;

//***************************************************************************
//****ENUM DEFINATIONS *********************************************
//***************************************************************************
/*@to modify if it is defined in tvin.h*/
typedef enum det3d_fmt_e {
	DET3D_FMT_NULL = 0,
	DET3D_FMT_LR,
	DET3D_FMT_TB,
	DET3D_FMT_INTERLACE,
	DET3D_FMT_CHESSBOARD,
}det3d_fmt_t;

//****************************************************************************
//******** GLOBAL FUNCTION CLAIM ********
//****************************************************************************
extern void det3d_enable(bool flag);
extern enum det3d_fmt_e det3d_fmt_detect(void);

#endif  // _DET3D_H


#endif  // _DET3D_H
