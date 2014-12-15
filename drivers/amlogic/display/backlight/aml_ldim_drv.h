/*
 * AMLOGIC Ldim
 *
 * Author:
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/cdev.h>

#define AML_LDIM_MODULE_NAME "aml_ldim"
#define AML_LDIM_DRIVER_NAME "aml_ldim"
#define AML_LDIM_DEVICE_NAME "aml_ldim"
#define AML_LDIM_CLASS_NAME  "aml_ldim"

#define STA_LEN 9
#define BLK_LEN 17
#define LUT_LEN 32

#define BLKHMAX 8
#define BLKVMAX 8

struct LDReg
{
	//---------------------------------------------------------------------------------------------------------------------------------------------------
	// general parameters
	int reg_LD_pic_RowMax;
    int reg_LD_pic_ColMax;
	int reg_LD_pic_YUVsum[3];         // only output u16*3, (internal ACC will be u32x3)
    int reg_LD_pic_RGBsum[3];
	
	// -------------------------------------------------------------------------------------------------------------------------------------------------
    // Statistic options
	
	int reg_LD_BLK_Vnum;          //u4: Maximum to 10
    int reg_LD_BLK_Hnum;          //u4: Maximum to 10

    int reg_LD_STA1max_LPF;		   //u1: STA1max statistics on [1 2 1]/4 filtered results
	int reg_LD_STA2max_LPF;        //u1: STA2max statistics on [1 2 1]/4 filtered results
	int reg_LD_STAhist_LPF;        //u1: STAhist statistics on [1 2 1]/4 filtered results
    int reg_LD_STA1max_Hdlt;      // u2: (2^x) extra pixels into Max calculation
    int reg_LD_STA1max_Vdlt;      // u4: extra pixels into Max calculation vertically
    int reg_LD_STA2max_Hdlt;      // u2: (2^x) extra pixels into Max calculation
    int reg_LD_STA2max_Vdlt;      // u4: extra pixels into Max calculation vertically
    int reg_LD_STA1max_Hidx[STA_LEN];  // U12* 9
    int reg_LD_STA1max_Vidx[STA_LEN];  // u12x 9
    int reg_LD_STA2max_Hidx[STA_LEN];  // U12* 9
    int reg_LD_STA2max_Vidx[STA_LEN];  // u12x 9

    int reg_LD_STAhist_mode;      //u3: histogram statistics on XX separately 20bits*16bins:  0: R-only,  1:G-only 2:B-only 3:Y-only; 4: MAX(R,G,B), 5/6/7: R&G&B

    int reg_LD_STAhist_Hidx[STA_LEN];  // U12* 9
    int reg_LD_STAhist_Vidx[STA_LEN];  // u12x 9	

	// -----------------------------------------------------------------------------------------------------------------------------------------------------------
    // Backlit Modeling registers	
	int BL_matrix[BLKVMAX*BLKHMAX];  // Define the RAM Matrix    
    int reg_LD_BackLit_Xtlk;         //u1: 0 no block to block Xtalk model needed;   1: Xtalk model needed
    int reg_LD_BackLit_mode;         //u2: 0- LEFT/RIGHT Edge Lit; 1- Top/Bot Edge Lit; 2 - DirectLit modeled H/V independant; 3- DirectLit modeled HV Circle distribution
    int reg_LD_Reflect_Hnum;         //u3: numbers of band reflection considered in Horizontal direction; 0~4
    int reg_LD_Reflect_Vnum;         //u3: numbers of band reflection considered in Horizontal direction; 0~4
    int reg_LD_BkLit_curmod;         //u1: 0: H/V separately, 1 Circle distribution
    int reg_LD_BkLUT_Intmod;         //u1: 0: linear interpolation, 1 cubical interpolation
    int reg_LD_BkLit_Intmod;         //u1: 0: linear interpolation, 1 cubical interpolation
    int reg_LD_BkLit_LPFmod;         //u3: 0: no LPF, 1:[1 14 1]/16;2:[1 6 1]/8; 3: [1 2 1]/4; 4:[9 14 9]/32  5/6/7: [5 6 5]/16;
    int reg_LD_BkLit_Celnum;         //u8: 0: 1920~ 61
    int reg_BL_matrix_AVG;           //u12: DC of whole picture BL to be substract from BL_matrix during modeling (Set by FW daynamically)
    int reg_BL_matrix_Compensate;    //u12: DC of whole picture BL to be compensated back to Litfull after the model (Set by FW dynamically);
    int reg_LD_Reflect_Hdgr[20];    //20*u6:  cells 1~20 for H Gains of different dist of Left/Right;
    int reg_LD_Reflect_Vdgr[20];    //20*u6:  cells 1~20 for V Gains of different dist of Top/Bot;
    int reg_LD_Reflect_Xdgr[4];     // 4*u6:
    int reg_LD_Vgain;               //u12
    int reg_LD_Hgain;               //u12
    int reg_LD_Litgain;             //u12
    int reg_LD_Litshft;             //u3   right shif of bits for the all Lit's sum
    int reg_LD_BkLit_valid[32];     //u1x32: valid bits for the 32 cell Bklit to contribut to current position (refer to the backlit padding pattern)
	// region division index  1      2     3    4    5(0)  6(1) 7(2) 8(3) 9(4)  10(5)11(6)12(7)13(8) 14(9)15(10) 16   17   18   19
    int reg_LD_BLK_Hidx[BLK_LEN]; // S14* 17
    int reg_LD_BLK_Vidx[BLK_LEN]; // S14* 17
	// Define the RAM Matrix
    int reg_LD_LUT_Hdg[LUT_LEN];
    int reg_LD_LUT_Vdg[LUT_LEN];
    int reg_LD_LUT_VHk[LUT_LEN];

	// adding three cells for left boundary extend during Cubic interpolation
	int reg_LD_LUT_Hdg_LEXT;
    int reg_LD_LUT_Vdg_LEXT;
    int reg_LD_LUT_VHk_LEXT;

	//-----------------------------------------------------------------------------------------------------------------------------------
    // Register used in RGB remapping
    int reg_LD_RGBmapping_demo;              //u2: 0 no demo mode 1: display BL_fulpel on RGB
    int reg_LD_X_LUT_interp_mode[3];         //U1 0: using linear interpolation between to neighbour LUT; 1: use the nearest LUT results
    int X_idx[1][16];                        // Changed to 16 Lits define 32 Bin LUT to save cost
    int X_nrm[1][16];                        // Changed to 16 Lits define 32 Bin LUT to save cost
    int X_lut[3][16][32];                    // Changed to 16 Lits define 32 Bin LUT to save cost

	//------------------------------------------------------------------------------------------------------------------------------------
	// only do the Lit modleing on the AC part
    int fw_LD_BLEst_ACmode; //u2: 0: est on BLmatrix; 1: est on (BL-DC); 2: est on (BL-MIN); 3: est on (BL-MAX)
};

struct FW_DAT
{
    // for temporary Firmware algorithm
    int *TF_BL_alpha;
    int *last_YUVsum;
    int *last_RGBsum;
    int *last_STA1_MaxRGB;
    int *SF_BL_matrix;
    int *TF_BL_matrix;
};
/*
typedef struct Ldim_dev_s{
    int reg_LD_pic_RowMax;
	int reg_LD_pic_ColMax;
	int reg_LD_BLK_Vnum;
	int reg_LD_BLK_Hnum;
	int reg_LD_BackLit_mode;
	int reg_LD_BkLit_Celnum;
	int reg_LD_STAhist_Hidx[];
	int reg_LD_STAhist_Vidx[];
	int reg_LD_Reflect_Hnum;
	int reg_LD_Reflect_Vnum;
	int reg_LD_BLK_Hidx[];
	int reg_LD_BLK_Vidx[];
	int reg_LD_Hgain;
	int reg_LD_Vgain;
	int reg_LD_STAhist_LPF;
	int reg_LD_X_LUT_interp_mode[0];
	int reg_LD_X_LUT_interp_mode[1];
	int reg_LD_X_LUT_interp_mode[2];
	
}Ldim_dev_t;*/


//#define LDIM_STTS_HIST_REGION_IDX                  0x1aa0
#define LOCAL_DIM_STATISTIC_EN_BIT          31
#define LOCAL_DIM_STATISTIC_EN_WID           1
#define EOL_EN_BIT                          28
#define EOL_EN_WID                           1

	//0: 17 pix, 1: 9 pix, 2: 5 pix, 3: 3 pix, 4: 0 pix
#define HOVLP_NUM_SEL_BIT                   21
#define HOVLP_NUM_SEL_WID                    2 
#define LPF_BEFORE_STATISTIC_EN_BIT         20
#define LPF_BEFORE_STATISTIC_EN_WID          1

#define BLK_HV_POS_IDXS_BIT                 16
#define BLK_HV_POS_IDXS_WID                  4
#define RD_INDEX_INC_MODE_BIT               14
#define RD_INDEX_INC_MODE_WID                2
#define REGION_RD_SUB_INDEX_BIT              8
#define REGION_RD_SUB_INDEX_WID              4
#define REGION_RD_INDEX_BIT                  0
#define REGION_RD_INDEX_WID                  7

//each base has 16 address space
#define LD_CFG_BASE          0x00
#define LD_RGB_IDX_BASE      0x10
#define LD_RGB_NRMW_BASE     0x40
#define LD_BLK_HIDX_BASE     0x50
#define LD_BLK_VIDX_BASE     0x60
#define LD_LUT_HDG_BASE      0x70
#define LD_LUT_VHK_BASE      0x80
#define LD_LUT_VDG_BASE      0x90
#define LD_MATRIX_R0_BASE    0xa0
#define LD_MATRIX_R1_BASE    0xb0
#define LD_REFLECT_DGR_BASE  0xf0

#define LD_RGB_LUT_BASE      0x2000
 

#define LD_FRM_SIZE         0x0  
#define LD_RGB_MOD          0x1
#define LD_BLK_HVNUM        0x2
#define LD_HVGAIN           0x3
#define LD_BKLIT_VLD        0x4
#define LD_BKLIT_PARAM      0x5
#define LD_LUT_XDG_LEXT     0x6
#define LD_LIT_GAIN_COMP    0x7
#define LD_FRM_RST_POS      0x8
#define LD_FRM_BL_START_POS 0x9
#define LD_MISC_CTRL0       0xa 

#define LD_BLK_LEN   17
#define LD_LUT_LEN   32

typedef struct _ldim_rgb_mode{
    int RGBmapping_demo;  //u2: 0 no demo mode 1: display BL_fulpel on RGB
    int b_ldlut_ip_mode;  //U1 0: using linear interpolation between to neighbour LUT; 1: use the nearest LUT results
    int g_ldlut_ip_mode;
    int r_ldlut_ip_mode;
    int BkLit_LPFmod;  //u3: 0: no LPF, 1:[1 14 1]/16;2:[1 6 1]/8; 3: [1 2 1]/4; 4:[9 14 9]/32  5/6/7: [5 6 5]/16;
    int BackLit_Xtlk;  //u1: 0 no block to block Xtalk model needed; 1: Xtalk model needed
    int BkLit_Intmod;  //u1: 0: linear interpolation, 1 cubical interpolation
    int BkLUT_Intmod;  //u1: 0: linear interpolation, 1 cubical interpolation
    int BkLit_curmod;  //u1: 0: H/V separately, 1 Circle distribution
    int BackLit_mode;  //u2: 0- LEFT/RIGHT Edge Lit; 1- Top/Bot Edge Lit; 2 - DirectLit modeled H/V independant;
                       //    3- DirectLit modeled HV Circle distribution
} sLDIM_RGB_MODE_Param;

typedef struct _ldim_blk_hvnum{
    int frm_hblank_num; //12bits
    int Reflect_Vnum;   //3bits   //u3: numbers of band reflection considered in Horizontal direction; 0~4
    int Reflect_Hnum;   //3bits   //u3: numbers of band reflection considered in Horizontal direction; 0~4
    int BLK_Vnum;       //4bits
    int BLK_Hnum;       //4bits  
} sLDIM_BLK_HVNUM_Param;

void LDIM_Initial(int pic_h, int pic_v, int BLK_Vnum, int BLK_Hnum, int BackLit_mode, int ldim_bl_en, int ldim_hvcnt_bypass);
void LDIM_Updata_LUT(int blk_idx_update, int hdg_vhk_vdg_update, int bl_matrix_update, int bl_matrix_AVG_update, int reflect_dgr_update, int ld_rgb_update); 
int  get_blMtxAvg(int size, int mode);
void ld_fw_cfg_once(void);
//int  ldim_round(int ix, int ib);

