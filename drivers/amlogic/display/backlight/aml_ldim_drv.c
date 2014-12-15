/*
 * Amlogic Ldim Driver for Meson Chip
 *
 * Author: 
 *
 * Copyright (C) 2012 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <mach/am_regs.h>

#include "aml_ldim_drv.h"

#define AML_LDIM_DEV_NAME            "aml_ldim"
const char ldim_dev_id[] = "ldim-dev";
struct LDReg nPRM;
struct FW_DAT FDat;
int hist_matrix[1024];
#ifndef MAX
#define MAX(a,b)   ((a>b)? a:b)
#endif
#ifndef MIN
#define MIN(a,b)   ((a<b)? a:b)
#endif

#ifndef ABS
#define ABS(a)   ((a<0)? (-a):a)
#endif

#define Wr(adr,val)   WRITE_MPEG_REG(adr,val)
#define Rd(adr)       READ_MPEG_REG(adr)

// round(iX/iB)
int Round(int iX, int iB)
{
    int Rst = 0;

    if(iX==0) {
        Rst = 0;
    }
    else if(iX>0) {
        Rst = (iX+iB/2)/iB;
    }
    else {
        Rst = (iX-iB/2)/iB;
    }
    
    return Rst;
}


static sLDIM_RGB_MODE_Param  ldim_param_rgbMode = { 0, //RGBmapping_demo;
                                                    0, //b_ldlut_ip_mode;   //0: linear, 1: nearest
                                                    0, //g_ldlut_ip_mode;   //0: linear, 1: nearest
                                                    0, //r_ldlut_ip_mode;   //0: linear, 1: nearest
                                                    7, //BkLit_LPFmod; u3:0 no LPF, 1:[1 14 1]/16, ...  
                                                    1, //BackLit_Xtlk; u1 
                                                    1, //BkLit_Intmod; u1 
                                                    1, //BkLUT_Intmod; u1
                                                    0, //BkLit_curmod; u1
                                                    0  //BackLit_mode;  u2: 0 LEFT/RIGHT edge, ...
                                                    };

// static sLDIM_BLK_HVNUM_Param ldim_param_blk_hvnum = {6, //frm_hblank_num u13
//                                                      0, //Reflect_Vnum u3
//                                                      3, //Reflect_Hnum u3
//                                                      8, //BLK_Vnum u4 max 8
//                                                      2  //BLK_Hnum u4 max 8
//                                                      };

static sLDIM_BLK_HVNUM_Param ldim_param_blk_hvnum = {6, //frm_hblank_num u13
                                                     3, //Reflect_Vnum u3
                                                     0, //Reflect_Hnum u3
                                                     8, //BLK_Vnum u4 max 8
                                                     2  //BLK_Hnum u4 max 8
                                                     };

static int ldim_param_vgain = 256; //u12  //
static int ldim_param_hgain = 128; //u12  //
static int ldim_param_bklit_valid = 0; //32bits
static int ldim_param_bklit_celnum = 0;  //u8  //
static int ldim_param_bl_matrix_avg = 0; //u12 DC of whole picture BL to be substract from BL_matrix during modeling (Set by FW daynamically)
static int ldim_param_litgain = 230;  //u12
static int ldim_param_litshft = 3;    //u3 right shif of bits for the all Lit's sum
static int ldim_param_bl_matrix_compen = 0; //u12: DC of whole picture BL to be compensated back to Litfull after the model (Set by FW dynamically)
static int ldim_lut_hdg_lext = 700;
static int ldim_lut_vdg_lext = 257;
static int ldim_lut_vhk_lext = 220;

static int ldim_pic_rowmax = 1080;
static int ldim_pic_colmax = 1920; 

static int ldim_param_frm_rst_pos = 0;;
static int ldim_param_frm_bl_start_pos = 0;

//static int ldim_param_misc_ctrl0 = 0;

// static int gBLK_Hidx_LUT[19] = {-2880, -2880, -2880, -2880, -960, 960, 2880, 4800, 4800, 4800, 4800, 4800, 4800, 4800, 4800, 4800, 4800, 4800, 4800}; //s14
// static int gBLK_Vidx_LUT[19] = {-540,  -405,  -270,  -135,    0, 135,  270,  405,  540,  675,  810,  945, 1080, 1215, 1350, 1485, 1620, 1755, 1890}; //s14
static int gBLK_Hidx_LUT[19] = {-1200, -1200, -1200, -1200, -400, 400, 1200, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000}; //s14
static int gBLK_Vidx_LUT[19] = {-300,  -225,  -150,  -75 ,    0,  75,  150,  225,  300,  375,  450,  525,  600,  675 , 750 , 825 ,  900,  975, 1050}; //s14
static int gHDG_LUT[32] = {660, 608, 568, 520, 480, 436, 404, 372, 348, 324, 300, 280, 260, 240, 224, 208,
                           192, 176, 160, 148, 136, 120, 108,  96,  84,  72,  60,  52,  40,  28,  20,   8}; //u10
static int gVDG_LUT[32] = {256, 255, 254, 251, 248, 244, 238, 232, 225, 218, 210, 201, 192, 183, 173, 163,
                           153, 144, 134, 124, 115, 106,  97,  89,  81,  73,  66,  60,  53,  48,  42,  37}; //u10
static int gVHK_LUT[32] = {201, 174, 155, 141, 122, 110, 101,  94,  88,  84,  80,  76,  74,  71,  69,  67,
                            65,  63,  62,  60,  59,  58,  57,  56,  55,  54,  53,  52,  51,  51,  50,  49}; //u10
static int gMatrix_LUT[64] = {0}; //s12
static int gLD_REFLECT_DGR_LUT[20+20+4] = {0}; //u6: 20 LD_REFLECT_HDGR +  20 LD_REFLECT_VDGR + 4 LD_REFLECT_XDGR 
static int gLD_RGB_IDX_LUT[16] = {0}; //12bits
//static int gLD_G_IDX_LUT[32] = {0}; //12bits
//static int gLD_B_IDX_LUT[32] = {0}; //12bits
static int gLD_NRMW_LUT[16]  = {0};   //4bits
static int gLD_RGB_LUT[3*16*32] = {0};  //12bits 

void LDIM_WR_32Bits(int addr, int data)
{
   Wr(LDIM_BL_ADDR_PORT,addr);
   Wr(LDIM_BL_DATA_PORT,data);
} 

int LDIM_RD_32Bits(int addr)
{
   Wr(LDIM_BL_ADDR_PORT,addr);
   return(Rd(LDIM_BL_DATA_PORT));
} 

void LDIM_wr_reg_bits(int addr, int val, int start, int len)
{
   unsigned int data;
   data = LDIM_RD_32Bits (addr);
   data = (data & (~((1 << len) - 1)<<start))  |  ((val & ((1 << len) -1)) << start);
   LDIM_WR_32Bits(addr, data);
} 

void LDIM_WR_BASE_LUT(int base,int* pData, int size_t, int len)
{
  int i;
  int addr,data;
  int mask,subCnt;
  int cnt;
  
  addr   = base;//(base<<4);
  mask   = (1<<size_t)-1;
  subCnt = 32/size_t;
  cnt  = 0;
  data = 0;
  
  Wr(LDIM_BL_ADDR_PORT,addr);

  for(i=0;i<len;i++)
  {
     //data = (data<<size_t)|(pData[i]&mask);
     data = (data)|((pData[i]&mask)<<(size_t*cnt));
     cnt++;
     if(cnt==subCnt)
     {
       Wr(LDIM_BL_DATA_PORT,data);
       data=0;
       cnt =0;
       addr++;
     }
  }
  if(cnt!=0)
     Wr(LDIM_BL_DATA_PORT,data);
} 


//========================================
void ld_fw_cfg_once()
{
    int k, dlt;
    int hofst=4;
    int vofst=4;
    static int drt_LD_LUT_dg[] = {254, 248, 239, 226, 211, 194, 176, 156, 137, 119, 101, 85, 70, 57, 45, 36,
                                  28, 21, 16, 12, 9, 6, 4, 3, 2, 1, 1, 1, 0, 0, 0, 0};
    // STA1max_Hidx
    //	nPRM->reg_LD_STA1max_Hidx[0]=0;
    //	for (k=1;k<=STA_LEN;k++)
    //	{    nPRM->reg_LD_STA1max_Hidx[k] = ((nPRM->reg_LD_pic_ColMax + (nPRM->reg_LD_BLK_Hnum) - 1)/(nPRM->reg_LD_BLK_Hnum))*k;
    //		 if (nPRM->reg_LD_STA1max_Hidx[k]> 4095)  nPRM->reg_LD_STA1max_Hidx[k] = 4095; // clip U12
    // 	}

    // STA1max_Vidx
    //	nPRM->reg_LD_STA1max_Vidx[0]=0;
    //	for (k=1;k<=STA_LEN;k++)
    //	{
    //		 nPRM->reg_LD_STA1max_Vidx[k] = ((nPRM->reg_LD_pic_RowMax + (nPRM->reg_LD_BLK_Vnum) - 1)/(nPRM->reg_LD_BLK_Vnum))*k;
    //		 if (nPRM->reg_LD_STA1max_Vidx[k]> 4095)  nPRM->reg_LD_STA1max_Vidx[k] = 4095;  // clip to U12
    //	}
	// config LD_STA2max_H/Vidx/LD_STAhist_H/Vidx
    //	for (k=1;k<=STA_LEN;k++)
    //	{
    //		nPRM->reg_LD_STA2max_Hidx[k] = nPRM->reg_LD_STA1max_Hidx[k];
    //		nPRM->reg_LD_STAhist_Hidx[k] = nPRM->reg_LD_STA1max_Hidx[k];
    //		nPRM->reg_LD_STA2max_Vidx[k] = nPRM->reg_LD_STA1max_Vidx[k];
    //		nPRM->reg_LD_STAhist_Vidx[k] = nPRM->reg_LD_STA1max_Vidx[k];
    //	}

    if (ldim_param_rgbMode.BackLit_mode ==0) // Left/right EdgeLit
    { 
    ldim_param_blk_hvnum.Reflect_Vnum = 3;
    ldim_param_blk_hvnum.Reflect_Hnum = 0;
	// config reg_LD_BLK_Hidx
	for (k=0;k<LD_BLK_LEN;k++){
	    dlt = ldim_pic_colmax/ldim_param_blk_hvnum.BLK_Hnum*2;
	    gBLK_Hidx_LUT[k] = -(dlt/2) + (k-hofst)*dlt;
            gBLK_Hidx_LUT[k] =  (gBLK_Hidx_LUT[k]>8191) ? 8191 : (gBLK_Hidx_LUT[k]<-8192) ? (-8192) : gBLK_Hidx_LUT[k]; // Clip to S14
 
	}
	// config reg_LD_BLK_Vidx
	for (k=0;k<LD_BLK_LEN;k++){
	    dlt = ldim_pic_rowmax/ldim_param_blk_hvnum.BLK_Vnum;
            gBLK_Vidx_LUT[k] = 0 + (k-vofst)*dlt;         
            gBLK_Vidx_LUT[k] = (gBLK_Vidx_LUT[k]>8191) ? 8191: (gBLK_Vidx_LUT[k]<-8192) ? (-8192) : gBLK_Vidx_LUT[k]; // Clip to S14
         }	
		
        // configure  Hgain/Vgain
        ldim_param_hgain = (128*1920/ldim_pic_colmax); //u12
        ldim_param_vgain = (256*1080/ldim_pic_rowmax); //u12
        ldim_param_hgain = (ldim_param_hgain > 4095) ? 4095 : ldim_param_hgain;
        ldim_param_vgain = (ldim_param_hgain > 4095) ? 4095 : ldim_param_vgain;
     }
     else if (ldim_param_rgbMode.BackLit_mode ==1) // Top/Bot EdgeLit
     {
     ldim_param_blk_hvnum.Reflect_Vnum = 0;
     ldim_param_blk_hvnum.Reflect_Hnum = 3;

        // config reg_LD_BLK_Hidx
	for (k=0;k<LD_BLK_LEN;k++){
	    dlt = ldim_pic_colmax/ldim_param_blk_hvnum.BLK_Hnum;
	    gBLK_Hidx_LUT[k] =  0 + (k-hofst)*dlt;
            gBLK_Hidx_LUT[k] =  (gBLK_Hidx_LUT[k]>8191) ? 8191: (gBLK_Hidx_LUT[k]<-8192) ? (-8192) : gBLK_Hidx_LUT[k]; // Clip to S14
 
	}
	// config reg_LD_BLK_Vidx
	for (k=0;k<LD_BLK_LEN;k++){
	    dlt = ldim_pic_rowmax/ldim_param_blk_hvnum.BLK_Vnum*2;
            gBLK_Vidx_LUT[k] = -(dlt/2) + (k-vofst)*dlt;         
            gBLK_Vidx_LUT[k] = (gBLK_Vidx_LUT[k]>8191) ? 8191: (gBLK_Vidx_LUT[k]<-8192) ? (-8192) : gBLK_Vidx_LUT[k]; // Clip to S14
         }
         // configure  Hgain/Vgain
        //ldim_param_hgain = (256*1920/ldim_pic_colmax); //u12
        //ldim_param_vgain = (128*1080/ldim_pic_rowmax); //u12
        ldim_param_hgain = (128*1920/ldim_pic_rowmax); //u12
        ldim_param_vgain = (256*1080/ldim_pic_colmax); //u12
        ldim_param_hgain = (ldim_param_hgain > 4095) ? 4095 : ldim_param_hgain;
        ldim_param_vgain = (ldim_param_hgain > 4095) ? 4095 : ldim_param_vgain;
      }
      else // DirectLit
      {
      ldim_param_blk_hvnum.Reflect_Vnum = 2;
      ldim_param_blk_hvnum.Reflect_Hnum = 2;

        // config reg_LD_BLK_Hidx
	for (k=0;k<LD_BLK_LEN;k++){
	    dlt = ldim_pic_colmax/ldim_param_blk_hvnum.BLK_Hnum;
	    gBLK_Hidx_LUT[k] =  0 + (k-hofst)*dlt;
            gBLK_Hidx_LUT[k] =  (gBLK_Hidx_LUT[k]>8191) ? 8191: (gBLK_Hidx_LUT[k]<-8192) ? -8192 : gBLK_Hidx_LUT[k]; // Clip to S14
 
	}
	// config reg_LD_BLK_Vidx
	for (k=0;k<LD_BLK_LEN;k++){
	    dlt = ldim_pic_rowmax/ldim_param_blk_hvnum.BLK_Vnum;
            gBLK_Vidx_LUT[k] =  0 + (k-vofst)*dlt;         
            gBLK_Vidx_LUT[k] = (gBLK_Vidx_LUT[k]>8191) ? 8191: (gBLK_Vidx_LUT[k]<-8192) ? -8192 : gBLK_Vidx_LUT[k]; // Clip to S14
         }
         // configure  Hgain/Vgain
        ldim_param_hgain = (256*1920/ldim_pic_colmax); //u12
        ldim_param_vgain = (424*1080/ldim_pic_rowmax); //u12
        ldim_param_hgain = (ldim_param_hgain > 4095) ? 4095 : ldim_param_hgain;
        ldim_param_vgain = (ldim_param_vgain > 4095) ? 4095 : ldim_param_vgain;
 
	// configure
        for(k=0;k<LD_LUT_LEN;k++)
        {
	   gHDG_LUT[k] = drt_LD_LUT_dg[k];
           gVDG_LUT[k] = drt_LD_LUT_dg[k];
           gVHK_LUT[k] = 64;
	 }  
       }
}

static void ldming_stts(void)
{
     int i;//,l,blkRow,blkCol,R_idx;
     int a[1024];
	 unsigned int cnt = READ_CBUS_REG(ASSIST_SPARE8_REG1);
/*	 nPRM->reg_LD_STA1max_Hidx[0]=0;
	 for (k=1;k<STA_LEN;k++)
     {    nPRM.reg_LD_STAhist_Hidx[k] = ((ldim_pic_colmax + (nPRM.reg_LD_BLK_Hnum) - 1)/(ldim_param_blk_hvnum.BLK_Hnum))*k;
         if (nPRM.reg_LD_STAhist_Hidx[k]> 4095)  nPRM.reg_LD_STAhist_Hidx[k] = 4095; // clip U12
     }
	 nPRM.reg_LD_STA1max_Vidx[0]=0;
	 for (k=1;k<STA_LEN;k++)
	 {
		  nPRM.reg_LD_STAhist_Vidx[k] = ((ldim_pic_rowmax + (nPRM.reg_LD_BLK_Vnum) - 1)/(ldim_param_blk_hvnum.BLK_Vnum))*k;
		  if (nPRM.reg_LD_STAhist_Vidx[k]> 4095)  nPRM.reg_LD_STAhist_Vidx[k] = 4095;  // clip to U12
	 }
	 for (blkRow=0;blkRow<ldim_param_blk_hvnum.BLK_Vnum;blkRow++)
	 {
		 for (blkCol=0; blkCol<ldim_param_blk_hvnum.BLK_Hnum; blkCol++)
		 {
			 for (R_idx=0;R_idx<16;k++)
			 {
			 //nPRM.reg_LD_STAhist_Hidx = nPRM.reg_LD_STAhist_Hidx[blkCol];
			 //nPRM.reg_LD_STAhist_Vidx = nPRM.reg_LD_STAhist_Vidx[blkRow];
			 //Rd_sub_idx = R_idx;
			 //(*(hist_matrix + blkRow*128 + blkCol*16 +R_idx ))= nPRM->reg_LD_STTS_HIST_REGION;
			 }
		 }
	 }
*/
    for(i=0;i<1024;i++)
    	{
	hist_matrix[i] = READ_CBUS_REG(LDIM_STTS_HIST_READ_REGION);
	//a[i] = hist_matrix[i];
    	}
	if(cnt)
		{
		WRITE_CBUS_REG(ASSIST_SPARE8_REG1, 0);
		for(i=0;i<1024;i++)
		printk("\n hist_matrix[%d]=%x",i,hist_matrix[i]);
		}
}

void LDIM_Initial(int pic_h, int pic_v, int BLK_Vnum, int BLK_Hnum, int BackLit_mode, int ldim_bl_en, int ldim_hvcnt_bypass)
{
   int i,j;
   int data;
   int matrixNum;
   // static int matrix_dbg[16] = {1600,3000,
   //                       2000,1600,
   //                       2300, 600,
   //                       3000,1500,
   //                       3500,2000,
   //                       4000,2300,
   //                       4095,3000,
   //                       4095,4000};


   static int matrix_dbg_lr[16] = {4095,4095,
                                   4095,4095,
                                   4095,4095,
                                   3884,4095,
                                   2244,3144,
                                   2492,3724,
                                   2092,3524,
                                   2436,3444};

   static int matrix_dbg_lr_1080[16] = {1780, 1780, 4095, 1964, 4012, 2096, 4095, 4095 , 
                                        3512, 4095, 4095, 4095, 2772, 2752, 1780, 1784 };

   static int matrix_dbg_tb[16] = {4095, 2764, 4095, 2004, 4095, 2256, 2540, 2368,
                                   4095, 2424, 4095, 2092, 4095, 4095, 4095, 4095};

   static int matrix_dbg_drt[64] = {4095, 4095, 4095, 3004, 4095, 4095, 4095, 4095, 
                                    4095, 4095, 4095, 2964, 4095, 4095, 4095, 4095, 
                                    4095, 4095, 4095, 2804, 4095, 4095, 4095, 4095, 
                                    4095, 4095, 4095, 2184, 2844, 4095, 4095, 4095, 
                                    
                                    2196, 2384, 2168, 2224, 1636, 2212, 4095, 4095, 
                                    4095, 1844, 2480, 2468, 4095, 1784, 4095, 4095, 
                                    2004, 1316, 2308, 3324, 4095, 2224, 4095, 2864, 
                                    4095, 2472, 2072, 2044, 2008, 2140, 4095, 4095};

   int k,l,blkRow,blkCol,R_idx;

   ldim_pic_rowmax = pic_v;
   ldim_pic_colmax = pic_h; 
   ldim_param_rgbMode.BackLit_mode = BackLit_mode;
   ldim_param_blk_hvnum.BLK_Vnum = BLK_Vnum; //8;
   ldim_param_blk_hvnum.BLK_Hnum = BLK_Hnum; //2;


   //set one time nPRM here
   ldim_param_rgbMode.RGBmapping_demo = 0;
   ldim_param_rgbMode.b_ldlut_ip_mode = 0;
   ldim_param_rgbMode.g_ldlut_ip_mode = 0;
   ldim_param_rgbMode.r_ldlut_ip_mode = 0;
   //nPRM->reg_LD_STA1max_LPF  = 1;                //u1: STA1max statistics on [1 2 1]/4 filtered results
   //nPRM->reg_LD_STA2max_LPF  = 1;                //u1: STA2max statistics on [1 2 1]/4 filtered results
   //nPRM->reg_LD_STAhist_LPF  = 1;                //u1: STAhist statistics on [1 2 1]/4 filtered results

   nPRM.reg_LD_STAhist_Hidx[0]=0;
   for (k=1;k<STA_LEN;k++)
   {	nPRM.reg_LD_STAhist_Hidx[k] = ((ldim_pic_colmax + (ldim_param_blk_hvnum.BLK_Hnum) - 1)/(ldim_param_blk_hvnum.BLK_Hnum))*k;
	   if (nPRM.reg_LD_STAhist_Hidx[k]> 4095)  nPRM.reg_LD_STAhist_Hidx[k] = 4095; // clip U12
   }
   nPRM.reg_LD_STAhist_Vidx[0]=0;
   for (k=1;k<STA_LEN;k++)
   {
		nPRM.reg_LD_STAhist_Vidx[k] = ((ldim_pic_rowmax + (ldim_param_blk_hvnum.BLK_Vnum) - 1)/(ldim_param_blk_hvnum.BLK_Vnum))*k;
		if (nPRM.reg_LD_STAhist_Vidx[k]> 4095)	nPRM.reg_LD_STAhist_Vidx[k] = 4095;  // clip to U12
   }
   WRITE_CBUS_REG(LDIM_STTS_HIST_REGION_IDX, (1 << LOCAL_DIM_STATISTIC_EN_BIT)  |
										     (0 << EOL_EN_BIT) 				    |
										     (1 << LPF_BEFORE_STATISTIC_EN_BIT) |
										     (1 << RD_INDEX_INC_MODE_BIT)
	   );
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,0,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Vidx[0]<<12|nPRM.reg_LD_STAhist_Hidx[0]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,1,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Hidx[2]<<12|nPRM.reg_LD_STAhist_Hidx[1]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,2,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Vidx[2]<<12|nPRM.reg_LD_STAhist_Vidx[1]);   
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,3,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Hidx[4]<<12|nPRM.reg_LD_STAhist_Hidx[3]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,4,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Vidx[4]<<12|nPRM.reg_LD_STAhist_Vidx[3]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,5,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Hidx[6]<<12|nPRM.reg_LD_STAhist_Hidx[5]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,6,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Vidx[6]<<12|nPRM.reg_LD_STAhist_Vidx[5]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,7,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Hidx[8]<<12|nPRM.reg_LD_STAhist_Hidx[7]);
   WRITE_CBUS_REG_BITS(LDIM_STTS_HIST_REGION_IDX,8,BLK_HV_POS_IDXS_BIT,BLK_HV_POS_IDXS_WID);
   WRITE_CBUS_REG(LDIM_STTS_HIST_SET_REGION,nPRM.reg_LD_STAhist_Vidx[8]<<12|nPRM.reg_LD_STAhist_Vidx[7]);
/*
   ld_fw_cfg_once();

   for (i=0;i<20;i++)
      gLD_REFLECT_DGR_LUT[i] = 32;
      //gLD_REFLECT_DGR_LUT[i] = i+1;
   for (i=0;i<20;i++)
      gLD_REFLECT_DGR_LUT[20+i] = 32;
      //gLD_REFLECT_DGR_LUT[20+i] = 20-i;
   for (i=0;i<4;i++)
      gLD_REFLECT_DGR_LUT[40+i] = 32;
      //gLD_REFLECT_DGR_LUT[40+i] = i+1;

   matrixNum = ldim_param_blk_hvnum.BLK_Vnum*ldim_param_blk_hvnum.BLK_Hnum;

   for(i=0;i<16;i++)
   {  gLD_RGB_IDX_LUT[i] =  4095-256*i;
      gLD_NRMW_LUT[i]   = 8;
      for(j=0;j<32;j++) 
      {
        //gLD_RGB_LUT[i*32+j] = (32*(j+1)*16)/(16-i);//(64*(j+1)*32)/(16-i); //
        //gLD_RGB_LUT[i*32+j] = (64*(j+1)*32)/(16-i); //
        gLD_RGB_LUT[i*32+j] = Round((64*(j+1)*32), (16-i)); //
        if (gLD_RGB_LUT[i*32+j]>4095)
          gLD_RGB_LUT[i*32+j] = 4095;
      }
   }
   
   for(i=0;i<16;i++)
   {  
      for(j=0;j<32;j++) 
        gLD_RGB_LUT[16*32+i*32+j] = gLD_RGB_LUT[i*32+j];
   }
   
   for(i=0;i<16;i++)
   {  
      for(j=0;j<32;j++) 
        gLD_RGB_LUT[2*16*32+i*32+j] = gLD_RGB_LUT[i*32+j];
   }

   ldim_param_bklit_valid = 0;
   for(i=0;i<32;i++)
     ldim_param_bklit_valid = ldim_param_bklit_valid | (1<<i);

   //
   ldim_param_bklit_celnum = (ldim_pic_colmax + 31)/32 + 1;
   //
   //ldim_param_bl_matrix_avg = get_blMtxAvg(matrixNum,1);
   //ldim_param_bl_matrix_compen = ldim_param_bl_matrix_avg;
   //
  
   //enable the CBUS configure the RAM
   //LD_MISC_CTRL0  {ram_clk_gate_en,2'h0,ldlut_ram_sel,ram_clk_sel,reg_hvcnt_bypass,reg_ldim_bl_en,soft_bl_start,reg_soft_rst)
   data = LDIM_RD_32Bits(LD_MISC_CTRL0);
   data = data & (~(3<<4));
   LDIM_WR_32Bits(LD_MISC_CTRL0,data);
    
   //change here: gBLK_Hidx_LUT: s14*19
   LDIM_WR_BASE_LUT(LD_BLK_HIDX_BASE,gBLK_Hidx_LUT,16,19);
   
   //change here: gBLK_Vidx_LUT: s14*19
   LDIM_WR_BASE_LUT(LD_BLK_VIDX_BASE,gBLK_Vidx_LUT,16,19);
 
   //change here: gHDG_LUT: u10*32
   LDIM_WR_BASE_LUT(LD_LUT_HDG_BASE,gHDG_LUT,16,32);
 
   //change here: gVHK_LUT: u10*32
   LDIM_WR_BASE_LUT(LD_LUT_VHK_BASE,gVHK_LUT,16,32);
   
   //change here: gVDG_LUT: u10*32
   LDIM_WR_BASE_LUT(LD_LUT_VDG_BASE,gVDG_LUT,16,32);
   
   //gMatrix_LUT: s12*100
   LDIM_WR_BASE_LUT(LD_MATRIX_R0_BASE,&gMatrix_LUT[0] ,16,32);
   LDIM_WR_BASE_LUT(LD_MATRIX_R1_BASE,&gMatrix_LUT[32],16,32);

   //gLD_REFLECT_DGR_LUT: u6 * (20+20+4)
   LDIM_WR_BASE_LUT(LD_REFLECT_DGR_BASE,gLD_REFLECT_DGR_LUT,8,44);

   //gLD_RGB_LUT: 12 * 3*16*32
   LDIM_WR_BASE_LUT(LD_RGB_LUT_BASE,gLD_RGB_LUT,16,32*16*3);

   //gLD_RGB_IDX_LUT: 12 * 16
   LDIM_WR_BASE_LUT(LD_RGB_IDX_BASE,gLD_RGB_IDX_LUT,16,16);
   
   //gLD_BRM_LUT: 4 * 16
   LDIM_WR_BASE_LUT(LD_RGB_NRMW_BASE,gLD_NRMW_LUT,4,16);

   ////=============================

   //LD_FRM_SIZE
   data = ((ldim_pic_rowmax&0xfff)<<16) | (ldim_pic_colmax&0xfff);
   LDIM_WR_32Bits(LD_FRM_SIZE,data);

   //LD_RGB_MOD
   data = ((ldim_param_rgbMode.RGBmapping_demo &0x1) <<19) |
          ((ldim_param_rgbMode.b_ldlut_ip_mode &0x1) <<18) |
          ((ldim_param_rgbMode.g_ldlut_ip_mode &0x1) <<17) |
          ((ldim_param_rgbMode.r_ldlut_ip_mode &0x1) <<16) |
          ((ldim_param_rgbMode.BkLit_LPFmod &0x7)    <<12) |
          ((ldim_param_litshft  &0x7)    <<8)              |
          ((ldim_param_rgbMode.BackLit_Xtlk &0x1)    <<7)  |
          ((ldim_param_rgbMode.BkLit_Intmod &0x1)    <<6)  |
          ((ldim_param_rgbMode.BkLUT_Intmod &0x1)    <<5)  |
          ((ldim_param_rgbMode.BkLit_curmod &0x1)    <<4)  |
          ((ldim_param_rgbMode.BackLit_mode &0x3)      )  ;
    LDIM_WR_32Bits(LD_RGB_MOD,data); 
  
   //LD_BLK_HVNUM
    data = ((ldim_param_blk_hvnum.frm_hblank_num &0xfff) << 16) |
           ((ldim_param_blk_hvnum.Reflect_Vnum & 0x7)    << 12) |
           ((ldim_param_blk_hvnum.Reflect_Hnum & 0x7)    << 8 ) |
           ((ldim_param_blk_hvnum.BLK_Vnum & 0xf )       << 4 ) |
           ((ldim_param_blk_hvnum.BLK_Hnum & 0xf )            ) ;   
    LDIM_WR_32Bits(LD_BLK_HVNUM,data);

    //LD_HVGAIN
    data = ((ldim_param_vgain&0xfff)<<16) | (ldim_param_hgain&0xfff);
    LDIM_WR_32Bits(LD_HVGAIN,data);

    //LD_BKLIT_VLD 
    LDIM_WR_32Bits(LD_BKLIT_VLD,ldim_param_bklit_valid);

    //LD_BKLIT_PARAM
    data = ((ldim_param_bklit_celnum&0xff)<<16)|(ldim_param_bl_matrix_avg&0xfff);
    LDIM_WR_32Bits(LD_BKLIT_PARAM,data);

    //LD_LUT_XDG_LEXT
    data =((ldim_lut_vdg_lext&0x3ff)<<20)| ((ldim_lut_vhk_lext&0x3ff)<<10) |
          (ldim_lut_hdg_lext&0x3ff);
    LDIM_WR_32Bits(LD_LUT_XDG_LEXT,data);

    //LD_LIT_GAIN_COMP
    data = ((ldim_param_litgain&0xfff)<<16) | (ldim_param_bl_matrix_compen & 0xfff);
    LDIM_WR_32Bits(LD_LIT_GAIN_COMP,data);
   
    //LD_FRM_RST_POS
    ldim_param_frm_rst_pos = (16<<16) | (3); //h=16,v=3
    data = ldim_param_frm_rst_pos;
    LDIM_WR_32Bits(LD_FRM_RST_POS,data);

    //LD_FRM_BL_START_POS
    ldim_param_frm_bl_start_pos = (16<<16) | (4);
    data = ldim_param_frm_bl_start_pos;
    LDIM_WR_32Bits(LD_FRM_BL_START_POS,data);

    //LD_MISC_CTRL0 {ram_clk_gate_en,2'h0,ldlut_ram_sel,ram_clk_sel,reg_hvcnt_bypass,reg_ldim_bl_en,soft_bl_start,reg_soft_rst)
    data = (0<<1)| (ldim_bl_en<<2) |  (ldim_hvcnt_bypass<<3) | (3<<4) | (1<<8); //ldim_param_misc_ctrl0;
    LDIM_WR_32Bits(LD_MISC_CTRL0,data);
*/
}

extern int fiq_register(int irq, void (*handler)(void));
extern int fiq_unregister(int irq, void (*handler)(void));

static int aml_ldim_open(struct inode *inode, struct file *file)
{
    /* @todo */
    return 0;
}

static int aml_ldim_release(struct inode *inode, struct file *file)
{
    /* @todo */
    return 0;
}

static int aml_ldim_ioctl(struct file *file,unsigned int cmd, unsigned long arg)
{
    /* @todo */
    return 0;
}

static const struct file_operations aml_ldim_fops = {
    .owner          = THIS_MODULE,
    .open           = aml_ldim_open,
    .release        = aml_ldim_release,
    .unlocked_ioctl = aml_ldim_ioctl,
};

static struct tasklet_struct ldim_tasklet;
static dev_t aml_ldim_devno;
static struct class* aml_ldim_clsp;
static struct cdev*  aml_ldim_cdevp;

static void aml_ldim_tasklet(ulong data)
{

printk("\n 111 \n");
}

/*
 * vsync fiq handler
 */
static irqreturn_t vsync_isr(int irq, void *dev_id)
{
	ldming_stts();//printk("\n 222 \n");//fiq_bridge_pulse_trigger(&ldim_vsync_fiq_bridge);
	return IRQ_HANDLED;
}

static struct class_attribute aml_ldim_class_attrs[] = {
	//__ATTR(gamma_proc, S_IRUGO | S_IWUSR, 
	//	gamma_proc_show, gamma_proc_store),
	//__ATTR(env_backlight, S_IRUGO | S_IWUSR, 
	//	env_backlight_show, env_backlight_store),
	__ATTR_NULL,
};
static int aml_ldim_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	ret = alloc_chrdev_region(&aml_ldim_devno, 0, 1, AML_LDIM_DEVICE_NAME);
	if(ret < 0){
	  pr_err(KERN_ERR"amaudio: faild to alloc major number\n");
	  ret = - ENODEV;
	  goto err;
	}
	
	aml_ldim_clsp = class_create(THIS_MODULE, "aml_ldim");
	if(IS_ERR(aml_ldim_clsp)){
		ret = PTR_ERR(aml_ldim_clsp);
		return ret;
	}
	for(i = 0; aml_ldim_class_attrs[i].attr.name; i++){
		if(class_create_file(aml_ldim_clsp, 
				&aml_ldim_class_attrs[i]) < 0)
			goto err1;
	}

	aml_ldim_cdevp = kmalloc(sizeof(struct cdev), GFP_KERNEL);
	if(!aml_ldim_cdevp){
	  pr_err(KERN_ERR"aml_ldim: failed to allocate memory\n");
	  ret = -ENOMEM;
	  goto err2;
	}

	/* connect the file operations with cdev */
	cdev_init(aml_ldim_cdevp, &aml_ldim_fops);
	aml_ldim_cdevp->owner = THIS_MODULE;

	/* connect the major/minor number to the cdev */
	ret = cdev_add(aml_ldim_cdevp, aml_ldim_devno, 1);
	if (ret) {
		pr_err("aml_ldim: failed to add device\n");
		/* @todo do with error */
		goto err3;
	}
	LDIM_Initial(1920,1080,8,2,0,1,0);
    ret = request_irq(INT_VIU_VSYNC, &vsync_isr,
                    IRQF_SHARED, "ldim_vsync",
                    (void *)ldim_dev_id);
	//fiq_register(INT_VIU_VSYNC, &ldim_vsync_fisr);
	tasklet_init(&ldim_tasklet, aml_ldim_tasklet, 0);
	//tasklet_init(&vdin_devp[i]->isr_tasklet, vdin_isr_tasklet, (unsigned long)vdin_devp[i]);
	//tasklet_disable(&vdin_devp[i]->isr_tasklet);

	pr_info("%s, driver probe ok\n", __func__);
	return 0;
err3:
	 kfree(aml_ldim_cdevp);
err2:
	for(i=0; aml_ldim_class_attrs[i].attr.name; i++){
		class_remove_file(aml_ldim_clsp, 
				&aml_ldim_class_attrs[i]);
	}
	class_destroy(aml_ldim_clsp); 
err1:
    unregister_chrdev_region(aml_ldim_devno, 1);
err:
  return ret;

	return -1;  
}

static int aml_ldim_remove(struct platform_device *pdev)
{
    int i;

	tasklet_kill(&ldim_tasklet);
	free_irq(INT_VIU_VSYNC, (void *)ldim_dev_id);
	//fiq_unregister(INT_VIU_VSYNC, &ldim_vsync_fisr);
	cdev_del(aml_ldim_cdevp);
	kfree(aml_ldim_cdevp);
	for(i=0; aml_ldim_class_attrs[i].attr.name; i++){
		class_remove_file(aml_ldim_clsp, 
				&aml_ldim_class_attrs[i]);
	}
	class_destroy(aml_ldim_clsp); 
    unregister_chrdev_region(aml_ldim_devno, 1);

    pr_info("%s, driver remove ok\n", __func__);
	return 0;
}

static struct platform_driver aml_ldim_driver = {
    .driver = {
        .name = "aml_ldim",
        .owner  = THIS_MODULE,
    },
    .probe = aml_ldim_probe,
    .remove = aml_ldim_remove,
};

static int __init aml_ldim_init(void)
{
    pr_info("%s, register platform driver...\n", __func__);
    return platform_driver_register(&aml_ldim_driver);
}

static void __exit aml_ldim_exit(void)
{
    platform_driver_unregister(&aml_ldim_driver);
    pr_info("%s, platform driver unregistered ok\n", __func__);
}


module_init(aml_ldim_init);
module_exit(aml_ldim_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Driver for ldim");
MODULE_LICENSE("GPL");

