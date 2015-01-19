/*
 * d2d3 char device driver.
 *
 * Copyright (c) 2010 Frank zhao <frank.zhao@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/* Standard Linux headers */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
//#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
/* Amlogic headers */
#include <mach/am_regs.h>
#include <mach/register.h>
#include <linux/amlogic/amports/canvas.h>
/* Local include */
#include "d2d3_drv.h"
#include "d2d3_regs.h"

#define D2D3_GOFILED_VPP         0
#define D2D3_GOFILED_DIPRE     1
#define D2D3_GOFILED_VDIN0     2
#define D2D3_GOFILED_VDIN1     3

static unsigned int depth_hmax = D2D3_CANVAS_MAX_WIDTH;
static unsigned int depth_vmax = D2D3_CANVAS_MAX_HEIGH;
module_param(depth_hmax,uint,0644);
module_param(depth_vmax,uint,0644);
MODULE_PARM_DESC(depth_hmax,"\n the horizontal max size of depth \n");
MODULE_PARM_DESC(depth_vmax,"\n the vertial max size of depth \n");


//reg00~3f
static unsigned int d2d3_reg_table[D2D3_REG_NUM]={
        //0x2b00    0x2b01      0x2b02     0x2b03     0x2b04     0x2b05     0x2b06    0x2b07
        0x81550d1f,0x077f0437,0x077f0437,0x000000ef,0x00000086,0x40000000,0x000000ff,0x001ec8ff,
        //0x2b08    0x2b09      0x2b0a     0x2b0b     0x2b0c     0x2b0d     0x2b0e    0x2b0f
        0x00ff001e,0x04103fc0,0x008700c3,0x40000000,0x08081360,0x0606403f,0x0606403f,0x00012020,
        //0x2b10    0x2b11      0x2b12     0x2b13     0x2b14     0x2b15     0x2b16    0x2b17
        0x00000000,0x08000600,0x18001800,0x08000600,0x000c0c10,0x800c0c10,0x00404013,0x000700f0,
        //0x2b18    0x2b19      0x2b1a     0x2b1b     0x2b1c     0x2b1d     0x2b1e    0x2b1f
        0x000000a3,0xbb78c0c0,0xbb43c0c0,0x42427777,0x60606060,0x06cc0000,0x58381808,0x58381808,
        //0x2b20    0x2b21      0x2b22     0x2b23     0x2b24     0x2b25     0x2b26    0x2b27
        0x00008080,0x00000000,0x00000000,0x00000000,0x0000001df,0x00ef0000,0x00860000,0x008600ef,
        //0x2b28    0x2b29      0x2b2a     0x2b2b     0x2b2c     0x2b2d     0x2b2e    0x2b2f
        0x000001df,0x00ef0000,0x00860000,0x00000000,0x00000004,0x00000000,0x00000000,0x00000100,
        //0x2b30    0x2b31      0x2b32     0x2b33     0x2b34     0x2b35     0x2b36    0x2b37
        0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
        //0x2b38    0x2b39      0x2b3a     0x2b3b     0x2b3c     0x2b3d     0x2b3e    0x2b3f
        0x15641145,0x00007e90,0x00520001,0x00000002,0x00000000,0x00000000,0x00000000,0x00000000,
};

//Initial D2D3 Register
void d2d3_set_def_config(d2d3_param_t *parm)
{
        unsigned short i = 0;
	parm->depth      = 0;
        parm->input_w    = 0;
        parm->input_h    = 0;
        parm->output_w   = 1920;
        parm->output_h   = 1080;
        parm->dpg_path   = D2D3_DPG_MUX_VPP;
        parm->dbr_path   = D2D3_DBR_MUX_VPP;
        parm->dbr_mode   = LINE_INTERLEAVED;
        //enable clk_d2d3_reg
        D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,1,ON_CLK_D2D3_REG_BIT,ON_CLK_D2D3_REG_WID); //d2d3_intf_ctrl0[23:22];
        //load default config as 1080p
        for(i=0; i<D2D3_REG_NUM; i++){
                D2d3_Wr(D2D3_CBUS_BASE + i, d2d3_reg_table[i]);
        }
        return;
}

inline void d2d3_canvas_init(struct d2d3_dev_s *devp)
{
        unsigned int canvas_max_w = D2D3_CANVAS_MAX_WIDTH;
        unsigned int canvas_max_h = D2D3_CANVAS_MAX_HEIGH;
        devp->dpg_canvas_idx = D2D3_CANVAS_DPG_INDEX;
        devp->dbr_canvas_idx = D2D3_CANVAS_DBR_INDEX;
        canvas_config(devp->dpg_canvas_idx,devp->mem_start,canvas_max_w,canvas_max_h,CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
        canvas_config(devp->dbr_canvas_idx,devp->mem_start+0x100000,canvas_max_w,canvas_max_h,CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);

        D2d3_Wr_reg_bits(D2D3_DWMIF_CTRL,devp->dpg_canvas_idx,0,8);
        D2d3_Wr_reg_bits(D2D3_DRMIF_CTRL,devp->dbr_canvas_idx,0,8);
}

inline void d2d3_update_canvas(d2d3_dev_t *devp)
{
        swap(devp->dpg_canvas_idx,devp->dbr_canvas_idx);
        D2d3_Wr_reg_bits(D2D3_DWMIF_CTRL,devp->dpg_canvas_idx,0,8);
        D2d3_Wr_reg_bits(D2D3_DRMIF_CTRL,devp->dbr_canvas_idx,0,8);
}
/*
 *canvas reverse work with di&vdin
 */
inline void d2d3_canvas_reverse(bool reverse)
{
        if(reverse)
                D2d3_Wr_reg_bits(D2D3_DRMIF_CTRL,3,16,2); //dr_req_en=1
        else//normal
                D2d3_Wr_reg_bits(D2D3_DRMIF_CTRL,0,16,2);
}
inline void d2d3_config_dpg_canvas(struct d2d3_dev_s *devp)
{
        canvas_config(devp->dpg_canvas_idx,devp->dpg_addr, devp->canvas_w,devp->canvas_h,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
}

inline void d2d3_config_dbr_canvas(struct d2d3_dev_s *devp)
{
        canvas_config(devp->dbr_canvas_idx,devp->dbr_addr, devp->canvas_w,devp->canvas_h,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
}

inline void get_real_display_size(unsigned int *w, unsigned int *h)
{
        unsigned int hstart,hend,vstart,vend;
        hstart = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_H_START_END,16,12);
        hend = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_H_START_END,0,12);
        *w = hend -hstart + 1;

        vstart = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_V_START_END,16,12);
        vend = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_V_START_END,0,12);
        *h = vend -vstart + 1;
}
inline int d2d3_depth_adjust(short depth)
{
	unsigned short min_dtp=0,max_dtp=0,d2p_offset=0,mg1=0,mg2=0;

	if(depth > 127 || depth < -127){
		pr_err("[d2d3]%s: the %u over the depth range [-127~127].\n",__func__,depth);
		return 1;
	}

	if(depth >0){
		min_dtp=max_dtp=d2p_offset = depth;
		mg1 = mg2 = (depth>>2);
	}
	else {
		min_dtp=max_dtp=d2p_offset = 255 + depth;
		mg1 = mg2 = (abs(depth))>>2;
	}
	/*adjust the offset of d2p */
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,d2p_offset,D2P_OFFSET_BIT,D2P_OFFSET_WID);
	/*if depth >0 the curve will be protruded, else will be concave*/
	D2d3_Wr(D2D3_MBDG_PARAM_3,(max_dtp<<24)|(min_dtp<<16)|(max_dtp<<8)|min_dtp);
	/*adjust the gain of mbdg*/
	D2d3_Wr_reg_bits(D2D3_DBLD_MG_PARAM,mg1,DB_G1_MG_BIT,DB_G1_MG_WID);
	D2d3_Wr_reg_bits(D2D3_DBLD_MG_PARAM,mg2,DB_G2_MG_BIT,DB_G2_MG_WID);

        return 0;
}

inline void d2d3_enable_path(bool enable, d2d3_param_t *parm)
{
        unsigned short dpg_path = parm->dpg_path;
        unsigned short dbr_path  = parm->dbr_path;
        unsigned short dpg_gofield = D2D3_GOFILED_VPP;
        unsigned short dbr_gofield  = D2D3_GOFILED_VPP;
        unsigned short nr_splitter    = 1;
        //dbr source
        D2d3_Wr_reg_bits(VPP_INPUT_CTRL,0,VD1_LEAVE_MOD_BIT,VD1_LEAVE_MOD_WID); //vd1_interleave_mode:  0->no_interleave;
        if(!enable)
        {
                //disable dpg path
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,1,26,2);
                //disable dbr path
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,0,4,2); //d2d3_v1_sel: 1 to select VDR, 2 to select VPP scaler
                D2d3_Wr_reg_bits(VPP_INPUT_CTRL,1,6,3); //vd1_l_sel: 1->vd1(vdr);2->vd2;3->d2d3_l;4->d2d3_r
                //pr_info("[d2d3]%s: disable dpg dbr path.\n",__func__);
        }
        else
        {
                switch(dpg_path){
                        case D2D3_DPG_MUX_VDIN0:
                                dpg_gofield = D2D3_GOFILED_VDIN0;
                                nr_splitter = 1;
                                break;
                        case D2D3_DPG_MUX_VDIN1:
                                dpg_gofield = D2D3_GOFILED_VDIN1;
                                nr_splitter = 1;
                                break;
                        case D2D3_DPG_MUX_NRW:
                                dpg_gofield = D2D3_GOFILED_DIPRE;
                                nr_splitter = 3;
                                break;
                        case D2D3_DPG_MUX_VDR:
                        case D2D3_DPG_MUX_VPP:
                                dpg_gofield = D2D3_GOFILED_VPP;
                                nr_splitter = 1;
                                break;
                        default:
                                break;
                }

                //turn on dpg path

                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,nr_splitter,NRWR_DOUT_SPLITTER_BIT,NRWR_DOUT_SPLITTER_WID);
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,dpg_gofield,V0_GOFLD_SEL_BIT,V0_GOFLD_SEL_WID);
                //enable dpg read nrw
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,dpg_path,D2D3_V0_SEL_BIT,D2D3_V0_SEL_WID);

                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,dbr_gofield,V1_GOFLD_SEL_BIT,V1_GOFLD_SEL_WID);
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,dbr_path,D2D3_V1_SEL_BIT,D2D3_V1_SEL_WID);
                if(D2D3_DBR_MUX_VDR == dbr_path){
                        /*vd1_left select d2d3 left input */
                        D2d3_Wr_reg_bits(VPP_INPUT_CTRL,3,VD1_L_SEL_BIT,VD1_L_SEL_WID);
                        /*turn on vpp 3d scale */
                        D2d3_Wr_reg_bits(VPP_VSC_PHASE_CTRL,2,DOUBLE_LINE_MODE_BIT,DOUBLE_LINE_MODE_WID);
                }
                else{
                        /*turn off vpp 3d scale */
                        D2d3_Wr_reg_bits(VPP_VSC_PHASE_CTRL,0,DOUBLE_LINE_MODE_BIT,DOUBLE_LINE_MODE_WID);
                        /*vd1_left select vdr input*/
                        D2d3_Wr_reg_bits(VPP_INPUT_CTRL,1,VD1_L_SEL_BIT,VD1_L_SEL_WID);
                }
                //pr_info("[d2d3]%s: enable dpg path %u. dbr path %u.\n",__func__,dpg_path,dbr_path);
        }


}


// DBR Function:
//input:
//    DBR_Mode = 0:  the 1st output field is left, field interleave (left/right)
//    DBR_Mode = 1:  the 1st output field is right, field interleave (left/ right)
//    DBR_Mode = 2:  line interleave (left first): lllll|rrrrr|lllll|...
//    DBR_Mode = 3:  half line interleave (left first): lllllrrrrr|lllllrrrrr|... (both left and right are half size)
//    DBR_Mode = 4:  pixel interleave (left first): lrlrlrlr... (both left and right are half size)
//    DBR_Mode = 5:  left/right output with full size : lllllrrrrr|lllllrrrrr

void d2d3_enable_dbr(unsigned  int size_x, unsigned int size_y,unsigned int scale_x,
                unsigned int scale_y, d2d3_dbr_mode_t dbr_mode,d2d3_scu18_param_t sSCU18Param)
{
        unsigned int dbr_d2p_smode, dbr_d2p_out_mode;
        unsigned int dbr_d2p_lomode, dbr_d2p_1dtolr, dbr_ddd_hhalf, dbr_d2p_lar, dbr_d2p_lr_switch;
        unsigned int dbr_scu18_rep_en, dbr_ddd_out_mode, dbr_ddd_lomode;
        unsigned int szxScaled,szyScaled;
        unsigned int scu18_hstep,scu18_vstep;
        unsigned int scale_x_act;
        unsigned int dwTemp;

        //dbr source
        D2d3_Wr_reg_bits(D2D3_INTF_LENGTH,size_x-1,V1_LINE_LENGTHM1_BIT,V1_LINE_LENGTHM1_WID);

        D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,DBR_EN_BIT,DBR_EN_WID); //Set dbr_en = 1

        //0x02:{szx_m1(16),szy_m1(0)}
        D2d3_Wr(D2D3_DBR_OUTPIC_SIZE,((size_x-1)<<16) | (size_y-1)); //image size

        switch(dbr_mode){
                case FIELD_INTERLEAVED_LEFT_RIGHT:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 3;
                                dbr_d2p_lomode = 0;
                                dbr_d2p_1dtolr = 0;
                                dbr_ddd_hhalf = 0;
                                dbr_d2p_lar    = 0;
                                dbr_d2p_lr_switch = 1;
                                dbr_scu18_rep_en = 0;
                        break;
                case FIELD_INTERLEAVED_RIGHT_LEFT:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 3;
                                dbr_d2p_lomode = 0;
                                dbr_d2p_1dtolr = 0;
                                dbr_ddd_hhalf = 0;
                                dbr_d2p_lar   = 1;
                                dbr_d2p_lr_switch = 1;
                                dbr_scu18_rep_en = 0;
                        break;
                case LINE_INTERLEAVED:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 1;
                                dbr_d2p_lomode = 0;
                                dbr_d2p_1dtolr = 0;
                                dbr_ddd_hhalf = 0;
                                dbr_d2p_lar   = 0;
                                dbr_d2p_lr_switch = 0;
                                dbr_scu18_rep_en = 0;
                        break;
                case HALF_LINE_INTERLEAVED:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 1;
                                dbr_d2p_lomode = 3;
                                dbr_d2p_1dtolr = 0;
                                dbr_ddd_hhalf = 1;
                                dbr_d2p_lar   = 0;
                                dbr_d2p_lr_switch = 0;
                                dbr_scu18_rep_en = 1;
                        break;
                case HALF_LINE_INTERLEAVED_DOUBLE_SIZE:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 0;
                                dbr_d2p_lomode = 2;
                                dbr_d2p_1dtolr = 1;
                                dbr_ddd_hhalf = 1;
                                dbr_d2p_lar   = 0;
                                dbr_d2p_lr_switch = 0;
                                dbr_scu18_rep_en = 0;
                        break;
                case PIXEL_INTERLEAVED:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 1;
                                dbr_d2p_lomode = 3;
                                dbr_d2p_1dtolr = 0;
                                dbr_ddd_hhalf = 0;
                                dbr_d2p_lar   = 0;
                                dbr_d2p_lr_switch = 0;
                                dbr_scu18_rep_en = 1;
                        break;
                default:
                                dbr_d2p_smode = 3;
                                dbr_d2p_out_mode = 1;
                                dbr_d2p_lomode = 0;
                                dbr_d2p_1dtolr = 0;
                                dbr_ddd_hhalf = 0;
                                dbr_d2p_lar   = 0;
                                dbr_d2p_lr_switch = 0;
                                dbr_scu18_rep_en = 0;
                        break;
        }

        dbr_ddd_out_mode = dbr_d2p_out_mode;
        dbr_ddd_lomode   = dbr_d2p_lomode;
        //d2p neg = 1
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,1,D2P_NEG_BIT,D2P_NEG_WID);
        //smode
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,dbr_d2p_smode,D2P_SMODE_BIT,D2P_SMODE_WID);
        //d2p out mode
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,dbr_d2p_out_mode,D2P_OUT_MODE_BIT,D2P_OUT_MODE_WID);

        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,dbr_d2p_1dtolr,D2P_1DTOLR_BIT,D2P_1DTOLR_WID);

        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,dbr_d2p_lr_switch,D2P_LR_SWITCH_BIT,D2P_LR_SWITCH_WID);
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,dbr_d2p_lar,D2P_LAR_BIT,D2P_LAR_WID);
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,0,D2P_WRAP_EN_BIT,D2P_WRAP_EN_WID);

        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,dbr_d2p_lomode,D2P_LOMODE_BIT,D2P_LOMODE_WID);

        //set bound default left 8 pixel&right 8 pixel
        D2d3_Wr_reg_bits(D2D3_D2P_PARAM_1,8,D2P_BRDWID_BIT,D2P_BRDWID_WID);

        dwTemp = dbr_ddd_lomode | (dbr_ddd_out_mode<<2) | (dbr_ddd_hhalf<<4) |
                (0<<6) | (0 <<7);
        D2d3_Wr(D2D3_DBR_DDD_CTRL,dwTemp);

        D2d3_Wr_reg_bits(D2D3_DBR_LRDMX_CTRL,1,LR_MERGE_BIT,LR_MERGE_WID); //set lr_merge

        if(sSCU18Param.dbr_scu18_step_en == 0)
        {
                scale_x_act = (dbr_ddd_hhalf) ? scale_x-1 : scale_x;
                D2d3_Wr_reg_bits(D2D3_SCALER_CTRL,scale_x_act|(scale_y<<2),4,8-4);  //Set SCD18 factor:
        }
        else
        {
                scu18_hstep = (dbr_ddd_hhalf) ? ((sSCU18Param.dbr_scu18_isize_x*2*0x100)/size_x) : ((sSCU18Param.dbr_scu18_isize_x*0x100)/size_x);
                scu18_vstep = ((sSCU18Param.dbr_scu18_isize_y*0x100)/size_y);
                D2d3_Wr(D2D3_SCU18_INPIC_SIZE,((sSCU18Param.dbr_scu18_isize_y)<<16) | (sSCU18Param.dbr_scu18_isize_x));
                D2d3_Wr(D2D3_SCU18_STEP,((1<<16)|(scu18_hstep<<8)|scu18_vstep));
        }
        D2d3_Wr_reg_bits(D2D3_SCALER_CTRL,dbr_scu18_rep_en,8,1);  //Set SCD18 factor:
        D2d3_Wr_reg_bits(D2D3_SCALER_CTRL,sSCU18Param.dbr_scu18_iniph_h|(sSCU18Param.dbr_scu18_iniph_v<<8),16,16); //set phase

        //=== DRMIFf ===
        if(sSCU18Param.dbr_scu18_step_en == 0){
                szxScaled = (size_x+(1<<scale_x)-1)>>scale_x;
                szyScaled = (size_y+(1<<scale_y)-1)>>scale_y;
        }
        else{
                szxScaled = sSCU18Param.dbr_scu18_isize_x;
                szyScaled = sSCU18Param.dbr_scu18_isize_y;
        }
        //0x29:{drmif_end_x(16),drmif_start_x(0)}
        dwTemp = (0x0) | ((szxScaled-1)<<16); //drmif_start_x=0x0; drmif_end_x=szxScaled-1;
        D2d3_Wr(D2D3_DRMIF_HPOS, (0x0)|((szxScaled-1)<<16));

        //0x2a:{drmif_end_y(16),drmif_start_y(0)}
        D2d3_Wr(D2D3_DRMIF_VPOS,(0x0)|((szyScaled-1)<<16));

        D2d3_Wr_reg_bits(D2D3_DRMIF_CTRL,1,DR_REQ_EN_BIT,DR_REQ_EN_WID); //dr_req_en=1
}

void d2d3_enable_dpg(unsigned int size_x, unsigned int size_y, unsigned int scale_x,
                unsigned int  scale_y, unsigned int xsize_depth,unsigned int ysize_depth)
{
        D2d3_Wr_reg_bits(D2D3_INTF_LENGTH,size_x-1,V0_LINE_LENGTHM1_BIT,V0_LINE_LENGTHM1_WID); //use to generate eol

        D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,DPG_EN_BIT,DPG_EN_WID); //Set dpg_en = 1

        D2d3_Wr(D2D3_DPG_INPIC_SIZE,((size_x-1)<<16) | (size_y-1)); //set image size of DPG

        //scu18
        D2d3_Wr_reg_bits(D2D3_SCALER_CTRL,scale_x|(scale_y<<2),0,4);
        D2d3_Wr_reg_bits(D2D3_SCALER_CTRL,0,11,12-11);
        D2d3_Wr(D2D3_DGEN_WIN_HOR,(0<<16) | (xsize_depth-1));
        D2d3_Wr(D2D3_DGEN_WIN_VER,(0<<16) | (ysize_depth-1)); //set Windows of DPG

        //=== Configure the DPG Models ===
        //cpdg
        D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,2,1);
        D2d3_Wr_reg_bits(D2D3_CG_PARAM_2,ysize_depth,16,16); //set cg_vpos_thr:    szyScaled
        //mpdg
        D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,3,1);
        D2d3_Wr_reg_bits(D2D3_MBDG_PARAM_0,xsize_depth/2,16,24-16);
        D2d3_Wr_reg_bits(D2D3_MBDG_PARAM_1,ysize_depth/2,16,24-16);
        D2d3_Wr(D2D3_MBDG_PARAM_2,((xsize_depth/2-1)) | ((xsize_depth/2-1)<<8) | ((ysize_depth/2-1)<<16) | ((ysize_depth/2-1)<<24));

        //lpdg

        //dbld

        //dwmif
        D2d3_Wr(D2D3_DWMIF_HPOS,(0x0)|((xsize_depth-1)<<16));
        D2d3_Wr(D2D3_DWMIF_VPOS,(0x0)|((ysize_depth-1)<<16));
        D2d3_Wr(D2D3_DWMIF_SIZE,(xsize_depth-1)|((ysize_depth-1)<<16));
        D2d3_Wr_reg_bits(D2D3_DWMIF_CTRL,1,8,1); //dw_req_en = 1;

}

//*********************************************************************
//      function to enable the D2D3 (DPG and DBR)
//*********************************************************************
//static void C_D2D3_Config(int dpg_size_x, int dpg_size_y, int dpg_scale_x, int dpg_scale_y, int d2d3_flow_mode, int DBR_Mode, int dbr_size_x, int dbr_size_y)
//void d2d3_config(d2d3_param_t *d2d3_devp,int isize_x, int isize_y, int iscale_x, int iscale_y,
//                  int osize_x, int osize_y, int oscale_x, int oscale_y,
//                  int dpg_path,int dbr_path, int dbr_mode)
void d2d3_config(d2d3_dev_t *d2d3_devp,d2d3_param_t *parm)
{
        unsigned int DepthSize_x,DepthSize_y;
        d2d3_scu18_param_t scu18;

        unsigned int isize_x,isize_y;
        unsigned short iscale_x=0, iscale_y=0;

        unsigned int osize_x,osize_y;
        unsigned short oscale_x=0, oscale_y=0;

        unsigned short dpg_path = parm->dpg_path;
        unsigned short dbr_path = parm->dbr_path;

        isize_x = (dpg_path==D2D3_DPG_MUX_NRW) ? parm->input_w : (dpg_path==D2D3_DPG_MUX_VDR) ?  parm->input_w  : parm->output_w;
        isize_y = (dpg_path==D2D3_DPG_MUX_NRW) ? (parm->input_h>>1) : (dpg_path==D2D3_DPG_MUX_VDR) ? (parm->input_h) : parm->output_h;
        osize_x = (dbr_path==D2D3_DBR_MUX_VDR) ? parm->input_w : parm->output_w;
        osize_y = (dbr_path==D2D3_DBR_MUX_VDR) ? parm->input_h : parm->output_h;

        if((D2D3_DPG_MUX_VPP == dpg_path)&&(D2D3_DBR_MUX_VPP == dbr_path))
        {
                iscale_x = iscale_y = oscale_x = oscale_y = 3;
        }else if((D2D3_DPG_MUX_VDR == dpg_path)&&(D2D3_DBR_MUX_VDR == dbr_path)){
                iscale_x = 1;
                DepthSize_x = isize_x>>iscale_x;
                while(DepthSize_x > depth_hmax){
                        DepthSize_x>>=1;
                        iscale_x++;
                }
                iscale_y = oscale_x = oscale_y = iscale_x;
        }else if((D2D3_DPG_MUX_VDR == dpg_path)&&(D2D3_DBR_MUX_VPP == dbr_path)){
                iscale_x = 1;
                DepthSize_x = isize_x>>iscale_x;
                while(DepthSize_x > depth_hmax){
                        DepthSize_x>>=1;
                        iscale_x++;
                }
                iscale_y = iscale_x;
        }else if((D2D3_DPG_MUX_NRW == dpg_path)&&(D2D3_DBR_MUX_VDR== dbr_path)){
                iscale_x = 1;
                DepthSize_x = isize_x>>iscale_x;
                while(DepthSize_x > depth_hmax){
                        DepthSize_x>>=1;
                        iscale_x++;
                }
                iscale_y = iscale_x;
        }
        DepthSize_x  = (isize_x+(1<<iscale_x)-1)>>iscale_x;
        DepthSize_y  = (isize_y+(1<<iscale_y)-1)>>iscale_y;
        pr_info("[d2d3]%s: dpg input size: %u x %u,depth size: %u x %u.down scaler factor %u x %u.\n",
                        __func__,parm->input_w,parm->input_h,DepthSize_x,DepthSize_y,iscale_x,iscale_y);

        //because the format of NRW is interlaced, so scale_y should minus 1
        d2d3_enable_dpg(isize_x, isize_y, iscale_x, iscale_y,DepthSize_x,DepthSize_y);

        /*if dpg and dbr is not at the same access point such as vpp,will enable scu18 */
        if(parm->dbr_path == D2D3_DBR_MUX_VPP) {
                //image1 input from vpp scaler
                scu18.dbr_scu18_step_en = 1;
                scu18.dbr_scu18_isize_x = DepthSize_x;
                scu18.dbr_scu18_isize_y = DepthSize_y;
                scu18.dbr_scu18_iniph_h = 0;
                scu18.dbr_scu18_iniph_v = 0;
        }
        else {
                scu18.dbr_scu18_step_en = 0;
        }
        pr_info("[d2d3]%s: output size %u x %u,dbr up scale factor %u x %u.\n",__func__,osize_x,osize_y,oscale_x,oscale_y);
        d2d3_enable_dbr(osize_x,osize_y,oscale_x,oscale_y,parm->dbr_mode,scu18);

        //enable DPG/DBR clock and DBR read REQ
        D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,0,1);
        D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,1,1);
        D2d3_Wr_reg_bits(D2D3_DRMIF_CTRL,1,8,9-8); //dr_req_en=1

        //clear  D2D3_INT
        //0x24:{x_rev(17),y_rev(16),dw_done_clr(15),dw_little_endian(14),dw_pic_struct(12),dw_urgent(11),
        //dw_clr_wrrsp(10),dw_canvas_wr(9),dw_req_en(8),dw_canvas_index(0)}
        D2d3_Wr_reg_bits(D2D3_DWMIF_CTRL,1,15,16-15); //dw_done_clr = 1;
        D2d3_Wr_reg_bits(D2D3_DWMIF_CTRL,0,15,16-15); //dw_done_clr = 0;

}
/*
 *disable d2d3 gate clock&disable d2d3 path
 */
void d2d3_enable_hw(bool enable)
{
        if(enable){
                D2d3_Wr_reg_bits(D2D3_GLB_CTRL,0,CLK_CTRL_BIT,CLK_CTRL_WID);
                //disable d2d3 clock
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,1,ON_CLK_D2D3_BIT,ON_CLK_D2D3_WID);
        }else {
                //soft reset d2d3 unit
        	D2d3_Wr_reg_bits(D2D3_GLB_CTRL,1,SW_RST_NOBUF_BIT,SW_RST_NOBUF_WID);

                // [27:26]  Enable d2d3 register clock    = 00/(auto, off, on, on)
                // [ 25:24]  Disable dpg clock        = 01/(auto, off, on, on)
                // [ 23:22]  Disable cbdg clock      = 01/(auto, off, on, on)
                // [ 21:20]  Disable mbdg clock     = 01/(auto, off, on, on)
                // [ 19:18]  Disable lbdg clock       = 01/(auto, off, on, on)
                // [17:16]  Disable dbr clock          = 01/(auto, off!!!!!!!!)
                D2d3_Wr_reg_bits(D2D3_GLB_CTRL,0x155,CLK_CTRL_BIT,CLK_CTRL_WID);
                //disable d2d3 clock
                D2d3_Wr_reg_bits(D2D3_INTF_CTRL0,0,ON_CLK_D2D3_BIT,ON_CLK_D2D3_WID);
        }
}

