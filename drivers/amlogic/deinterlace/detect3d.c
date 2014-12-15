/* Standard Linux headers */
#include <linux/kernel.h>
#include <linux/module.h>
/* Amlogic Headers */
#include <mach/am_regs.h>

#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TV)||(MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
/* Local include */
#include <linux/amlogic/tvin/tvin.h>
#include "detect3d.h"

/***************************Local defines**********************************/
#define DET3D_REG_NUM				9	//the number of total register
#define FRAME_MAX				(1<<15)	//Frame cnt max value

#define LR_FMT_SCORE_MAX			(2047)  //range of score in LR detection
#define LR_FMT_SCORE_MIN			(-2047)
#define TB_FMT_SCORE_MAX			(2047)  //range of score in TB detection
#define TB_FMT_SCORE_MIN			(-2047)
#define INTERLACE_FMT_SCORE_MAX			(63)	//range of score in interlace detection
#define INTERLACE_FMT_SCORE_MIN			(-63)
#define CHESSBOAD_FMT_SCORE_MAX			(63)	//range of score in chessboard detection
#define CHESSBOAD_FMT_SCORE_MIN			(-63)

#define LR_SCORE_LOWER_LIMIT			128	//if score > this limit, input is sure to be LR format
#define TB_SCORE_LOWER_LIMIT			128	//if score > this limit, input is sure to be TB format
#define INTERLACE_SCORE_LOWER_LIMIT		30	//if score > this limit, input is sure to be interlace format
#define CHESSBOADE_SCORE_LOWER_LIMIT		30	//if score > this limit, input is sure to be chessboard format

#define NOT_LR_SCORE_UPPER_LIMIT		(-24)	//if score < this limit, input is sure to be not LR format
#define NOT_TB_SCORE_UPPER_LIMIT		(-24)	//if score < this limit, input is sure to be not TB format
#define NOT_INTERLACE_SCORE_UPPER_LIMIT		(-12)	//if score < this limit, input is sure to be not interlace format
#define NOT_CHESSBOAD_SCORE_UPPER_LIMIT		(-12)	//if score < this limit, input is sure to be not chessboard format

#define LR_SYMMETRY_LOWER_LIMIT			44	//if > this limit,input is translational-symmety in left half and right half 
#define TB_SYMMETRY_LOWER_LIMIT			44	//if > this limit,input is translational-symmety in top half and bottom half

static int chessbd_vrate = 29;
static bool det3d_debug = 0;

/***************************Local variables **********************************/
const unsigned int det3d_table[DET3D_REG_NUM] = 
{
	0x00000002,0x00000027,0x00000065,0x00444400,
	0xc8404733,0x00060606,0x00060606,0x0000002a,
	0x0c0c0c0a
};

static struct det3d_info_s det3d_info = {
	-1, 				//nfrm
	DET3D_FMT_NULL,			//tfw_det3d_fmt
	0,				//score_3d_lr
	0,				//score_3d_tb
	{0, 0, 0, 0, 0, 0, 0, 0},	//tscore_3d_lr[8]
	{0, 0, 0, 0, 0, 0, 0, 0},	//tscore_3d_tb[8]
	0,				//tscore_3d_lr_accum
	0,				//tscore_3d_tb_accum
	0,				//score_3d_chs
	0,				//score_3d_int
	{0, 0, 0, 0, 0, 0, 0, 0},	//chs_valid_his[8]
	{0, 0, 0, 0, 0, 0, 0, 0},	//int_valid_his[8]
};

/*
 * Enable and Disable det3d
 * flag == true, enable det3d; flag == false, disable det3d;
 */
void det3d_enable(bool flag)
{
	int i;

	if(flag == 1){
		//disable 3D detection
		WRITE_CBUS_REG_BITS(NR2_SW_EN, 0, DET3D_EN_BIT, DET3D_EN_WID);

		//initalize the registers
		for (i = 0; i < DET3D_REG_NUM; i++){
			WRITE_CBUS_REG((DET3D_BASE_ADD + i), det3d_table[i]);
		}

		//Det 3D interrupt enble
		WRITE_CBUS_REG_BITS(DET3D_MOTN_CFG, 1, DET3D_INTR_EN_BIT, DET3D_INTR_EN_WID);
		//enable 3D detection
		WRITE_CBUS_REG_BITS(NR2_SW_EN, 1, DET3D_EN_BIT, DET3D_EN_WID);
	}else{
		//Det 3D interrupt disable
		WRITE_CBUS_REG_BITS(DET3D_MOTN_CFG, 0, DET3D_INTR_EN_BIT, DET3D_INTR_EN_WID);
		//disable 3D detection
		WRITE_CBUS_REG_BITS(NR2_SW_EN, 0, DET3D_EN_BIT, DET3D_EN_WID);
		memset(&det3d_info,0,sizeof(det3d_info));
	}
}

/*
 * Read cbus reg signed bits
 * length must be <= 31
 */

int read_cbus_reg_signed_bits(unsigned int reg, unsigned int startbit, unsigned int length)
{
	int val;
	int tmp = 1;

	if(length > 31)
		length = 31;
	val = READ_CBUS_REG_BITS(reg, startbit, length);
	tmp = tmp << (length - 1);

	//pr_info("len = %d, unsigned value = %d, signed value = %d",length,val,((val >= tmp )?(val - (tmp << 1)):val));
	return ((val >= tmp )?(val - (tmp << 1)):val);
}

/*
 * accumulate the frame to frame scores
 */
static void det3d_accumulate_score(int lr_score, int tb_score, int int_score, int chessbd_score)
{
	int tmp1 = 0;
	int tmp2 = 0;
	int m;

	// accumulate the frame to frame scores         
	if (det3d_info.nfrm < 1){
		det3d_info.score_3d_lr = lr_score;
		det3d_info.score_3d_tb = tb_score;

		//initialize the temporary score buffer
		det3d_info.tscore_3d_lr[0] = lr_score;  
		det3d_info.tscore_3d_tb[0] = tb_score;
		det3d_info.chs_valid_his[0]= chessbd_score; 
		det3d_info.int_valid_his[0]= int_score;
		det3d_info.tscore_3d_lr_accum = (lr_score <= 0) ? 1 : 0;
		det3d_info.tscore_3d_tb_accum = (tb_score <= 0) ? 1 : 0;
	}else{
		det3d_info.score_3d_lr = det3d_info.score_3d_lr + lr_score;
		det3d_info.score_3d_tb = det3d_info.score_3d_tb + tb_score;

		for (m = 7; m > 0; m--){
			det3d_info.tscore_3d_lr[m]  = det3d_info.tscore_3d_lr[m - 1]; // optimized needed???
			det3d_info.tscore_3d_tb[m]  = det3d_info.tscore_3d_tb[m - 1];
			det3d_info.chs_valid_his[m] = (chessbd_score == det3d_info.chs_valid_his[0])? det3d_info.chs_valid_his[m - 1]:0;
			det3d_info.int_valid_his[m] = (int_score == det3d_info.int_valid_his[0])? det3d_info.int_valid_his[m - 1]:0;
		}
		//--------------------------------------------
		//detection result agreement in frame to frame
		//--------------------------------------------
		det3d_info.tscore_3d_lr[0] = lr_score;  
		det3d_info.tscore_3d_tb[0] = tb_score;        
		det3d_info.tscore_3d_lr_accum = det3d_info.tscore_3d_lr_accum + (lr_score <= 0) - (det3d_info.tscore_3d_lr[5] <= 0);
		det3d_info.tscore_3d_tb_accum = det3d_info.tscore_3d_tb_accum + (tb_score <= 0) - (det3d_info.tscore_3d_tb[5] <= 0);
		// Clip to s12
		det3d_info.score_3d_lr = (det3d_info.score_3d_lr > LR_FMT_SCORE_MAX) ? 1800: det3d_info.score_3d_lr; 
		det3d_info.score_3d_lr = (det3d_info.score_3d_lr < LR_FMT_SCORE_MIN) ? -1800: det3d_info.score_3d_lr;
		det3d_info.score_3d_tb = (det3d_info.score_3d_tb > TB_FMT_SCORE_MAX) ? 1800: det3d_info.score_3d_tb; 
		det3d_info.score_3d_tb = (det3d_info.score_3d_tb < TB_FMT_SCORE_MIN) ? -1800: det3d_info.score_3d_tb;
		
		//-------------------------------------
		// Chessboard frame to frame agreement
		//-------------------------------------
		det3d_info.score_3d_chs    = ((chessbd_score == det3d_info.chs_valid_his[0]))? det3d_info.score_3d_chs: 0;  
		//det3d_info.score_3d_int    = ((int_score == det3d_info.int_valid_his[0]))? det3d_info.score_3d_int: 0;            
		det3d_info.chs_valid_his[0]= chessbd_score; 
		det3d_info.int_valid_his[0]= int_score;  


		for (m = 0; m < 8; m++){
			tmp1= tmp1 + (det3d_info.chs_valid_his[m]); 
			tmp2= tmp2 + (det3d_info.int_valid_his[m]);
		}        
		det3d_info.score_3d_chs = det3d_info.score_3d_chs + tmp1;
		det3d_info.score_3d_int = det3d_info.score_3d_int + tmp2;
		if(det3d_debug)
			pr_info("%s input(%d,%d),output (%d,%d).\n",__func__,chessbd_score,int_score,
					det3d_info.score_3d_chs,det3d_info.score_3d_int);
		// cliping to s7
		det3d_info.score_3d_chs = (det3d_info.score_3d_chs > CHESSBOAD_FMT_SCORE_MAX) ? CHESSBOAD_FMT_SCORE_MAX : det3d_info.score_3d_chs;
		det3d_info.score_3d_chs = (det3d_info.score_3d_chs < CHESSBOAD_FMT_SCORE_MIN) ? CHESSBOAD_FMT_SCORE_MIN : det3d_info.score_3d_chs;
		det3d_info.score_3d_int = (det3d_info.score_3d_int > INTERLACE_FMT_SCORE_MAX) ? INTERLACE_FMT_SCORE_MAX : det3d_info.score_3d_int;
		det3d_info.score_3d_int = (det3d_info.score_3d_int < INTERLACE_FMT_SCORE_MIN) ? INTERLACE_FMT_SCORE_MIN : det3d_info.score_3d_int;        
	}

}

/*
 * detect 3D format
 * execute one or more frame after init;
 */
enum det3d_fmt_e det3d_fmt_detect(void) 
{
	//FW registers
	int chessbd_hor_rate = 31;// 8bits: norm to 16
	//int chessbd_ver_rate = 31;// 8bits: norm to 16
	int chessbd_hor_thrd = 4; // 8bits: 
	int chessbd_ver_thrd = 4; // 8bits: 

	// local FW variables
	int m;
	int tmp_sp_lr, tmp_sp_tb;   
	int tmp_lr, tmp_tb, tmp1, tmp2; 
	int tmp_chs, tmp_int;
	int tmp_symtc_lr, tmp_symtc_tb, tmp_frmstill;  
	int chessbd_hor_valid, chessbd_ver_valid;
	//int dump_valid = 0;
	//static int call_first = 1;  

	if (det3d_info.nfrm < (FRAME_MAX)) 
		det3d_info.nfrm++;
	else              
		det3d_info.nfrm = 256; // reset to smaller number

	// Split line contribution
	tmp_sp_lr = READ_CBUS_REG_BITS(DET3D_RO_SPLT_HT, DET3D_SPLIT_HT_VAILID_BIT, DET3D_SPLIT_HT_VAILID_WID)
		+ READ_CBUS_REG_BITS(DET3D_RO_SPLT_HB, DET3D_SPLIT_HB_VAILID_BIT, DET3D_SPLIT_HB_VAILID_WID);
	tmp_sp_tb = READ_CBUS_REG_BITS(DET3D_RO_SPLT_VL, DET3D_SPLIT_VL_VAILID_BIT, DET3D_SPLIT_VL_VAILID_WID)
		+ READ_CBUS_REG_BITS(DET3D_RO_SPLT_VR, DET3D_SPLIT_VR_VAILID_BIT, DET3D_SPLIT_VR_VAILID_WID);

	// protect static graphics pattern
	if ((tmp_sp_lr == 2)&&(tmp_sp_tb == 2)) {
		tmp_sp_lr = -1; 
		tmp_sp_tb = -1; // -1: bias towards 2D; 0: keep previous status
	}

	// 8x8 statistics scores for being LR and TB   
	tmp_lr = tmp_sp_lr
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_LUMA_LR, DET3D_LUMA_LR_SUM_BIT, DET3D_LUMA_LR_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_CHRV_LR, DET3D_CHRV_LR_SUM_BIT, DET3D_CHRV_LR_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_CHRU_LR, DET3D_CHRU_LR_SUM_BIT, DET3D_CHRU_LR_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_HEDG_LR, DET3D_HEDG_LR_SUM_BIT, DET3D_HEDG_LR_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_VEDG_LR, DET3D_VEDG_LR_SUM_BIT, DET3D_VEDG_LR_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_MOTN_LR, DET3D_MOTN_LR_SUM_BIT, DET3D_MOTN_LR_SUM_WID);

	tmp_tb = tmp_sp_tb
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_LUMA_TB, DET3D_LUMA_TB_SUM_BIT,DET3D_LUMA_TB_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_CHRV_TB, DET3D_CHRV_TB_SUM_BIT,DET3D_CHRV_TB_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_CHRU_TB, DET3D_CHRU_TB_SUM_BIT,DET3D_CHRU_TB_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_HEDG_TB, DET3D_HEDG_TB_SUM_BIT,DET3D_HEDG_TB_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_VEDG_TB, DET3D_VEDG_TB_SUM_BIT,DET3D_VEDG_TB_SUM_WID)
		+ read_cbus_reg_signed_bits(DET3D_RO_MAT_MOTN_TB, DET3D_MOTN_TB_SUM_BIT,DET3D_MOTN_TB_SUM_WID);

	// 8x8 statistics for being purely symetrical
	tmp_symtc_lr = 0;
	tmp_symtc_tb = 0;

	for (m=0; m<8; m++){
		tmp_symtc_lr = tmp_symtc_lr 
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_LUMA_LR, DET3D_LUMA_LR_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_CHRU_LR, DET3D_CHRU_LR_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_CHRV_LR, DET3D_CHRV_LR_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_HEDG_LR, DET3D_HEDG_LR_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_VEDG_LR, DET3D_VEDG_LR_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_MOTN_LR, DET3D_MOTN_LR_SYMTC_BIT + m, 1);
		tmp_symtc_tb = tmp_symtc_tb
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_LUMA_TB, DET3D_LUMA_TB_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_CHRU_TB, DET3D_CHRU_TB_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_CHRV_TB, DET3D_CHRV_TB_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_HEDG_TB, DET3D_HEDG_TB_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_VEDG_TB, DET3D_VEDG_TB_SYMTC_BIT + m, 1)
			+ READ_CBUS_REG_BITS(DET3D_RO_MAT_MOTN_TB, DET3D_MOTN_TB_SYMTC_BIT + m, 1);
	}

	tmp_symtc_lr = tmp_symtc_lr > LR_SYMMETRY_LOWER_LIMIT;     
	tmp_symtc_tb = tmp_symtc_tb > TB_SYMMETRY_LOWER_LIMIT; 
	tmp_frmstill = READ_CBUS_REG_BITS(DET3D_RO_FRM_MOTN, DET3D_FRAME_MOTION_BIT, DET3D_FRAME_MOTION_WID) < 100;

	// if FrmStill && score>=0, force score decrease 1 
	if (tmp_frmstill && (tmp_lr >= -1)) 
		tmp_lr = tmp_lr - 1; 
	if (tmp_frmstill && (tmp_tb >= -1)) 
		tmp_tb = tmp_tb - 1; 

	// if FrmStill && symtc && score>=0, force score to 0/-1 
	//if (tmp_frmstill&&tmp_symtc_lr&&(tmp_lr>=0)) tmp_lr = 0; 
	//if (tmp_frmstill&&tmp_symtc_tb&&(tmp_tb>=0)) tmp_tb = 0;     

	//ChessBoard/interlace mode score
	tmp1 = READ_CBUS_REG_BITS(DET3D_RO_DET_CB_HOR, DET3D_CHESSBD_HOR_VALUE_BIT, DET3D_CHESSBD_HOR_VALUE_WID);
	tmp2 = READ_CBUS_REG_BITS(DET3D_RO_DET_CB_HOR, DET3D_CHESSBD_NHOR_VALUE_BIT, DET3D_CHESSBD_NHOR_VALUE_WID);
	chessbd_hor_valid = tmp1 > (((tmp2 * chessbd_hor_rate) >> 4) + chessbd_hor_thrd);

	tmp1 = READ_CBUS_REG_BITS(DET3D_RO_DET_CB_VER, DET3D_CHESSBD_VER_VALUE_BIT, DET3D_CHESSBD_VER_VALUE_WID);
	tmp2 = READ_CBUS_REG_BITS(DET3D_RO_DET_CB_VER, DET3D_CHESSBD_NVER_VALUE_BIT, DET3D_CHESSBD_NVER_VALUE_WID);	
	chessbd_ver_valid = tmp1 > (((tmp2 * chessbd_vrate) >> 4) + chessbd_ver_thrd);  

	tmp_chs = chessbd_hor_valid & chessbd_ver_valid;
	tmp_chs = 2 * tmp_chs - 1;
	tmp_int = (chessbd_hor_valid==0) & chessbd_ver_valid;
	tmp_int = 2 * tmp_int - 1;
	
	det3d_accumulate_score(tmp_lr, tmp_tb, tmp_int, tmp_chs);

	// quick reset to get faster converse
	if (((tmp_lr > 8) && (tmp_tb < -8) && (det3d_info.score_3d_lr < -127)) || ((det3d_info.tscore_3d_lr_accum >= 4) && (det3d_info.score_3d_lr > 63)))
		det3d_info.score_3d_lr = 0;

	if (((tmp_tb > 8) && (tmp_lr < -8) && (det3d_info.score_3d_tb < -127)) || ((det3d_info.tscore_3d_tb_accum >= 4) && (det3d_info.score_3d_tb > 63)))
		det3d_info.score_3d_tb = 0;     

	if ((det3d_info.score_3d_chs > CHESSBOADE_SCORE_LOWER_LIMIT) && (det3d_info.score_3d_int < NOT_INTERLACE_SCORE_UPPER_LIMIT)){
		det3d_info.tfw_det3d_fmt= TVIN_TFMT_3D_DET_CHESSBOARD;
		det3d_info.score_3d_lr = 0;
		det3d_info.score_3d_tb = 0;
	}else if ((det3d_info.score_3d_int > INTERLACE_SCORE_LOWER_LIMIT) && (det3d_info.score_3d_chs < NOT_CHESSBOAD_SCORE_UPPER_LIMIT)){
		det3d_info.tfw_det3d_fmt= TVIN_TFMT_3D_DET_INTERLACE;
		det3d_info.score_3d_lr = 0;
		det3d_info.score_3d_tb = 0;
	}else if ((det3d_info.score_3d_lr > LR_SCORE_LOWER_LIMIT) && (det3d_info.score_3d_lr > det3d_info.score_3d_tb)){
		det3d_info.tfw_det3d_fmt = TVIN_TFMT_3D_LRH_OLOR;
	}else if ((det3d_info.score_3d_tb > TB_SCORE_LOWER_LIMIT) && (det3d_info.score_3d_lr < det3d_info.score_3d_tb)){
		det3d_info.tfw_det3d_fmt = TVIN_TFMT_3D_TB;
	}else if ((det3d_info.score_3d_tb < NOT_LR_SCORE_UPPER_LIMIT) && (det3d_info.score_3d_lr < NOT_TB_SCORE_UPPER_LIMIT)){
		det3d_info.tfw_det3d_fmt = TVIN_TFMT_2D;
	}else{ 
		// keep previous status
		det3d_info.tfw_det3d_fmt = det3d_info.tfw_det3d_fmt;

		if ((det3d_info.score_3d_lr > LR_SCORE_LOWER_LIMIT) && (det3d_info.score_3d_tb > TB_SCORE_LOWER_LIMIT)){ 
			// if both LR and TB detected, reset the score
			det3d_info.score_3d_lr = 0;
			det3d_info.score_3d_tb = 0;
		}
	}  

#if defined DER3D_DEBUG_EN
	pr_info("det3d:frame = %d, 3D_fmt = %d, score_3d_lr = %d, score_3d_tb = %d, score_3d_int = %d, score_3d_chs = %d",
			det3d_info.nfrm, det3d_info.tfw_det3d_fmt, det3d_info.score_3d_lr, det3d_info.score_3d_tb, det3d_info.score_3d_int, det3d_info.score_3d_chs);
#endif
	return det3d_info.tfw_det3d_fmt;
}

module_param(chessbd_vrate,int,0644);
MODULE_PARM_DESC(chessbd_vrate,"\n the chessboard 3d fmt vertical rate \n");
module_param(det3d_debug,bool,0644);
MODULE_PARM_DESC(det3d_debug,"\n print the information of 3d detection \n");
#endif
