/*
 * Copyright (C) 2010 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <asm/delay.h>
#include <asm/atomic.h>
#include <linux/module.h>

#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <mach/am_regs.h>
#ifdef CONFIG_GAMMA_AUTO_TUNE
#include <linux/amlogic/vout/lcdoutc.h>
#endif

#include "../tvin_global.h"
#include "../tvin_frontend.h"
#include "../tvin_format_table.h"

#define DEVICE_NAME "viuin"
#define MODULE_NAME "viuin"

#define AMVIUIN_DEC_START       1
#define AMVIUIN_DEC_STOP        0

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
#define WR(x,val)                         WRITE_CBUS_REG(x,val)
#define WR_BITS(x,val,start,length)       WRITE_CBUS_REG_BITS(x,val,start,length)
#define RD(x)                             READ_CBUS_REG(x)
#define RD_BITS(x,start,length)           READ_CBUS_REG_BITS(x,start,length)
#else
#define WR(x,val)                         WRITE_VCBUS_REG(x,val)
#define WR_BITS(x,val,start,length)       WRITE_VCBUS_REG_BITS(x,val,start,length)
#define RD(x)                             READ_VCBUS_REG(x)
#define RD_BITS(x,start,length)           READ_VCBUS_REG_BITS(x,start,length)
#endif

static bool gamma_tune_en = false;
module_param(gamma_tune_en,bool,0644);
MODULE_PARM_DESC(gamma_tune_en,"enable/disable gamma auto tune function.");
static bool gamma_dbg_en = 0;
module_param(gamma_dbg_en, bool, 0664);
MODULE_PARM_DESC(gamma_dbg_en, "enable/disable gamma log");

static int gamma_type = 2;
module_param(gamma_type, int, 0664);
MODULE_PARM_DESC(gamma_type, "adjust gamma type");

static unsigned int vsync_enter_line_curr = 0;
module_param(vsync_enter_line_curr,uint,0664);
MODULE_PARM_DESC(vsync_enter_line_curr,"\n encoder process line num when enter isr.\n");

static unsigned int vsync_enter_line_max = 0;
module_param(vsync_enter_line_max,uint,0664);
MODULE_PARM_DESC(vsync_enter_line_max,"\n max encoder process line num when enter isr.\n");

static unsigned int vsync_enter_line_max_threshold = 10000;
module_param(vsync_enter_line_max_threshold,uint,0664);
MODULE_PARM_DESC(vsync_enter_line_max_threshold,"\n max encoder process line num over threshold drop the frame.\n");

static unsigned int vsync_enter_line_min_threshold = 10000;
module_param(vsync_enter_line_min_threshold,uint,0664);
MODULE_PARM_DESC(vsync_enter_line_min_threshold,"\n max encoder process line num less threshold drop the frame.\n");
static unsigned int vsync_enter_line_threshold_overflow_count = 0;
module_param(vsync_enter_line_threshold_overflow_count,uint,0664);
MODULE_PARM_DESC(vsync_enter_line_threshold_overflow_count,"\n count of overflow encoder process line num over threshold drop the frame.\n");

static unsigned short v_cut_offset = 0;
module_param(v_cut_offset,ushort,0664);
MODULE_PARM_DESC(v_cut_offset,"the cut window vertical offset for viuin");

typedef struct viuin_s{
        unsigned int flag;
        struct vframe_prop_s *prop;
        /*add for tvin frontend*/
        struct tvin_frontend_s frontend;
        struct vdin_parm_s parm;
	unsigned int enc_info_addr;
}viuin_t;

#ifdef CONFIG_GAMMA_AUTO_TUNE

static unsigned char ve_dnlp_tgt[64];
static unsigned int ve_dnlp_white_factor;
static unsigned int ve_dnlp_rt;
static unsigned int ve_dnlp_rl;
static unsigned int ve_dnlp_black;
static unsigned int ve_dnlp_white;
static unsigned int ve_dnlp_luma_sum;
//static ulong ve_dnlp_lpf[64], ve_dnlp_reg[16];
static unsigned int backlight;

static void ve_dnlp_calculate_tgt(struct vframe_prop_s *prop)
{
    struct vframe_prop_s *p = prop;
    ulong data[5];
    ulong i = 0, j = 0, ave = 0, max = 0, div = 0;
    
    // old historic luma sum
    j = ve_dnlp_luma_sum;
    // new historic luma sum
    ve_dnlp_luma_sum = p->hist.luma_sum;

    // picture mode: freeze dnlp curve
    if (// new luma sum is 0, something is wrong, freeze dnlp curve
        (!ve_dnlp_luma_sum) ||
        // new luma sum is closed to old one, picture mode, freeze curve
        ((ve_dnlp_luma_sum < j + (j >> 5)) &&
         (ve_dnlp_luma_sum > j - (j >> 5))
        )
       )
        return;

    // get 5 regions
    for (i = 0; i < 5; i++)
    {
        j = 4 + 11 * i;
        data[i] = (ulong)p->hist.gamma[j     ] +
                  (ulong)p->hist.gamma[j +  1] +
                  (ulong)p->hist.gamma[j +  2] +
                  (ulong)p->hist.gamma[j +  3] +
                  (ulong)p->hist.gamma[j +  4] +
                  (ulong)p->hist.gamma[j +  5] +
                  (ulong)p->hist.gamma[j +  6] +
                  (ulong)p->hist.gamma[j +  7] +
                  (ulong)p->hist.gamma[j +  8] +
                  (ulong)p->hist.gamma[j +  9] +
                  (ulong)p->hist.gamma[j + 10];
    }

    // get max, ave, div
    for (i = 0; i < 5; i++)
    {
        if (max < data[i])
            max = data[i];
        ave += data[i];
        data[i] *= 5;
    }
    max *= 5;
    div = (max - ave > ave) ? max - ave : ave;

    // invalid histgram: freeze dnlp curve
    if (!max)
        return;

    // get 1st 4 points
    for (i = 0; i < 4; i++){
        if (data[i] > ave)
            data[i] = 64 + (((data[i] - ave) << 1) + div) * ve_dnlp_rl / (div << 1);
        else if (data[i] < ave)
            data[i] = 64 - (((ave - data[i]) << 1) + div) * ve_dnlp_rl / (div << 1);
        else
            data[i] = 64;
        ve_dnlp_tgt[4 + 11 * (i + 1)] = ve_dnlp_tgt[4 + 11 * i] + ((44 * data[i] + 32) >> 6);
    }

    // fill in region 0 with black extension
    data[0] = ve_dnlp_black;
    if (data[0] > 16)
        data[0] = 16;
    data[0] = (ve_dnlp_tgt[15] - ve_dnlp_tgt[4]) * (16 - data[0]);
    for (j = 1; j <= 6; j++)
        ve_dnlp_tgt[4 + j] = ve_dnlp_tgt[4] + (data[0] * j + 88) / 176;
    data[0] = (ve_dnlp_tgt[15] - ve_dnlp_tgt[10]) << 1;
    for (j = 1; j <=4; j++)
        ve_dnlp_tgt[10 + j] = ve_dnlp_tgt[10] + (data[0] * j + 5) / 10;

    // fill in regions 1~3
    for (i = 1; i <= 3; i++)
    {
        data[i] = (ve_dnlp_tgt[11 * i + 15] - ve_dnlp_tgt[11 * i + 4]) << 1;
        for (j = 1; j <= 10; j++)
            ve_dnlp_tgt[11 * i + 4 + j] = ve_dnlp_tgt[11 * i + 4] + (data[i] * j + 11) / 22;
    }

    // fill in region 4 with white extension
    data[4] /= 5;
    data[4] = (ve_dnlp_white * ((ave << 4) - data[4] * ve_dnlp_white_factor)  + (ave << 3)) / (ave << 4);
    if (data[4] > 16)
        data[4] = 16;
    data[4] = (ve_dnlp_tgt[59] - ve_dnlp_tgt[48]) * (16 - data[4]);
    for (j = 1; j <= 6; j++)
        ve_dnlp_tgt[59 - j] = ve_dnlp_tgt[59] - (data[4] * j + 88) / 176;
    data[4] = (ve_dnlp_tgt[53] - ve_dnlp_tgt[48]) << 1;
    for (j = 1; j <= 4; j++)
        ve_dnlp_tgt[53 - j] = ve_dnlp_tgt[53] - (data[4] * j + 5) / 10;
        

}

static unsigned luma_sum;
static short slope_ref;
static unsigned int gamma_proc_enable = 0;

static const unsigned short base_gamma_table[256] = {
	0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
	32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
	64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
	96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
	128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
	160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
	192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
	224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};

static unsigned short gamma_table[256];
static unsigned short pdiv[256];

extern void set_lcd_gamma_table_lvds(u16 *data, u32 rgb_mask);
extern void _set_backlight_level(int level);

int gamma_adjust2( int nLow, int nHigh, int fAlphaL, int fAlphaH, int *pDiv, int *pLut)
{
	int i, j;
	int im1, im2, ip1, ip2;
	int nStep;

	// initial pGain
	int pGain[256];
	int pSlopeL[256];
	int pSlopeH[256];
	//pGain = (int *)calloc(256, sizeof(int));
	for (i = 0; i < 256; i++) {
		pGain[i] = 2048;
	}//i

	// basic low slope
	//int *pSlopeL = NULL;
	//pSlopeL = (int *)calloc(256, sizeof(int));

	if ( fAlphaL > 10 ) {
		nStep = (int)(((fAlphaL - 10) * 4 + 5)/10);
		for (i = 0; i < 256; i++){
			pSlopeL[i] = 2048 + (255 - i) * nStep;
		}//i
	} else {
		nStep = (int)(((10 - fAlphaL) * 4 + 5)/10);
		for (i = 0; i < 256; i++){
			pSlopeL[i] = 2048 - (255 - i) * nStep;
		}//i
	}

	// basic high slope
	//int *pSlopeH = NULL;
	//pSlopeH = (int *)calloc(256, sizeof(int));

	if ( fAlphaH > 10 ){
		nStep = (int)(((fAlphaH - 10) * 4 + 5)/10);
		for (i = 0; i < 256; i++){
			pSlopeH[i] = 2048 + (255 - i) * nStep;
		}//i
	} else {
		nStep = (int)(((10 - fAlphaH) * 4 + 5)/10);
		for (i = 0; i < 256; i++){
			pSlopeH[i] = 2048 - (255 - i) * nStep;
		}//i
	}

	// remapping to dark/bright curve
	for (i = 0; i < nLow; i++){
		j = (i * pDiv[nLow] + 1024) / 2048;
		pGain[i] = pSlopeL[j];
	}

	for (i = nHigh; i < 256; i++) {
		j = ((i - nHigh) * pDiv[255 - nHigh] + 1024) / 2048;
		pGain[i] = pSlopeH[j];
	}

	// adjust lut
	for (i = 0; i < 256; i++) {
		if ( i < nHigh )
			pLut[i] = (pGain[i] * pLut[i] + 1024) / 2048;
		else
			pLut[i] = pLut[nHigh] + (pGain[i] * 
				(pLut[i] - pLut[nHigh]) + 1024) / 2048;
		//printf("%d ", pGain[i]);
	}

	// 5 tap lpf
	for (i = 0; i < 256; i++) {
		im1 = i-1 < 0 ? 0 : i-1;
		im2 = i-2 < 0 ? 0 : i-2;
		ip1 = i+1 > 255 ? 255 : i+1;
		ip2 = i+2 > 255 ? 255 : i+2;

		pLut[i] = (pLut[im2] + 2 * pLut[im1] + 2 * pLut[i] +
					 2 * pLut[ip1] + pLut[ip2] + 4) /8;
		
	}

	return 0;
}

int nLow = 60;
int nHigh = 190;
int fAlphaL = 10;//<=2
int fAlphaH = 10;//>=0

int gamma_adjust(void)
{
	//printk("luma_sum = %d \n", luma_sum);
	int i, j;
	//unsigned long flags;
		// set parameters

	// caluclate and save to mem before
	//int *pLut = NULL;
	//pLut = (int *)calloc(256, sizeof(int));

	int pDiv[256];
	//pDiv = (int *)calloc(256, sizeof(int));
	for (i = 0; i < 256; i++){
		pDiv[i] = 256 * 2048 / (i+1);//256?
	}//i
	if (!gamma_proc_enable) 
		return 0;
	if (gamma_proc_enable == 2){
		for (i = 0; i < 256; i++){
			gamma_table[i] = base_gamma_table[i]<<2;
			//if(gamma_dbg_en)
			//printk("gamma_table[%d] = %d\n", i, gamma_table[i]);
		}
	        set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_R);
	        set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_G);
	        set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_B);
	        gamma_proc_enable = 0;
	}
                if(gamma_type == 0){
                        gamma_proc_enable = 0;
                        for (i = 0; i < 256; i++){
			gamma_table[i] = base_gamma_table[i]<<2;
			if(gamma_dbg_en)
			        printk("type %d,gamma_table[%d] = %d\n",gamma_type, i, gamma_table[i]);
                        }
	        set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_R);
	        set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_G);
	        set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_B);
                }
	else if (gamma_type == 1) {
		gamma_proc_enable = 0;
		for (i = 0; i < 256; i++) {
			gamma_table[i] = base_gamma_table[i]<<2;
		}	
		for (i = 1; i < 64; i++)
			for (j = 0; j < 4; j++) {
				gamma_table[i*4 + j] = base_gamma_table[i*4 + j]*ve_dnlp_tgt[i]/i;
		}
		if (gamma_dbg_en) {
			for (i = 0; i < 256; i++){
				printk("type %d,gamma_table[%d] = %d\n",gamma_type, i, gamma_table[i]);
			    }	
		}
		set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_R);
		set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_G);
		set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_B);
		//printk("gamma_adjust\n");
	} else if (gamma_type == 2) {
		int pLut[256];
		gamma_proc_enable = 0;
		for (i = 0; i < 256; i++) {
			gamma_table[i] = base_gamma_table[i]<<2;
		}	
		for (i = 1; i < 64; i++)
			for (j = 0; j < 4; j++){
				gamma_table[i*4 + j] = base_gamma_table[i*4 + j]*ve_dnlp_tgt[i]/i;
			}
		for (i = 0; i < 256; i++) {
			pLut[i] = gamma_table[i];
                                                
		}

		gamma_adjust2(nLow, nHigh, fAlphaL, fAlphaH, pDiv, pLut);
		for (i = 0; i < 256; i++){
			gamma_table[i] = pLut[i];
                        if(gamma_dbg_en)
			        printk("type %d,gamma table [%d] = %d\n",gamma_type, i, gamma_table[i]);
		}				
		set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_R);
		set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_G);
		set_lcd_gamma_table_lvds(gamma_table, LCD_H_SEL_B);
		//printk("gamma_adjust\n");
	}	
		
	return 0;
}

static ssize_t gamma_proc_show(struct class *cla, 
		struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "gamma_proc_enable=%d ,ve_dnlp_rt=0x%x,ve_dnlp_rl=0x%x,ve_dnlp_black=0x%x,ve_dnlp_white=0x%x\n",
                         gamma_proc_enable,ve_dnlp_rt,ve_dnlp_rl,ve_dnlp_black,ve_dnlp_white);
}

// [   28] en    0~1
// [27:24] rt    0~7
// [23:16] rl    0~255
// [15: 8] black 0~16
// [ 7: 0] white 0~16
//0x10200202

static ssize_t 
gamma_proc_store(struct class *cla,struct class_attribute *attr, 
				const char *buf, size_t count)
{
	size_t r;
	s32 val = 0;
	vdin_parm_t para;
	int en;
	static int gamma_proc_on = 0;

	r = sscanf(buf, "0x%x", &val);

	if (r != 1){
		return -EINVAL;
	}
	printk("val = %x, val>>28 = %d\n", val, val>>28);
	en = (val>>28)&0x1;
    
	if (en) {  
		ve_dnlp_rt = (val>>24)&0xf;  //7
		ve_dnlp_rl = (val>>16)&0xff; //0
		ve_dnlp_black = (val>>8)&0xff;   //2
		ve_dnlp_white = val&0xff;     //2
		if (ve_dnlp_rl > 64)
			ve_dnlp_rl = 64;
		if (ve_dnlp_black > 16)
			ve_dnlp_black = 16;
		if (ve_dnlp_white > 16)
			ve_dnlp_white = 16;
		
		para.port  = TVIN_PORT_VIU;
		para.fmt = TVIN_SIG_FMT_MAX;
                para.cfmt = TVIN_RGB444;
                para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
		para.frame_rate = 50;
		para.h_active = 1024;
		para.v_active = 768;
		para.hsync_phase = 1;
		para.vsync_phase  = 0;                                
                                
		if (gamma_proc_on == 0){
                        /*enable isr function*/
                        gamma_tune_en = true;
                        /*enable gamma function*/
                        WR_BITS(L_GAMMA_CNTL_PORT,1,0,1);
			start_tvin_service(0,&para);
			gamma_proc_on = 1;
			printk("start gamma calc function\n");
		}
	} else {
		if (gamma_proc_on){
                        /*disable isr function*/
                        gamma_tune_en = false;
			stop_tvin_service(0);
			gamma_proc_on = 0;
			printk("stop gamma calc function\n");
		}
	}

	return count;
}

static ssize_t env_backlight_show(struct class *cla, 
		struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", backlight);
}


static ssize_t env_backlight_store(struct class *cla, 
		struct class_attribute *attr, const char *buf, size_t count)
{
	size_t r;
	s32 val = 0;

	r = sscanf(buf, "0x%x", &val);

	if (r != 1) {
		return -EINVAL;
	}
	printk("val = %x\n", val);
	backlight = val;
    
	if (backlight > 255)
		backlight = 255;
	_set_backlight_level(backlight);
	if (backlight > 0xa0)
		fAlphaL = 17;
	else
		fAlphaL = 10;
		
	return count;
}
static struct class_attribute gamma_proc_class_attrs[] = {
	__ATTR(gamma_proc, S_IRUGO | S_IWUSR,gamma_proc_show, gamma_proc_store),
	__ATTR(env_backlight, S_IRUGO | S_IWUSR,env_backlight_show, env_backlight_store),
	__ATTR_NULL,
};
#endif
static int viuin_support(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        if(port == TVIN_PORT_VIU)
                return 0;
        else
                return -1;
}

static int viuin_open(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        viuin_t *devp = container_of(fe,viuin_t,frontend);
        if(!memcpy(&devp->parm,fe->private_data,sizeof(vdin_parm_t))){
                printk("[viuin..]%s memcpy error.\n",__func__); 
                return -1;
        }
        /*open the venc to vdin path*/
        switch(RD_BITS(VPU_VIU_VENC_MUX_CTRL,0,2)){
                case 0:
                        WR_BITS(VPU_VIU_VENC_MUX_CTRL,0x88,4,8);
			devp->enc_info_addr = ENCL_INFO_READ;
                        break;
                case 1:                        
                        WR_BITS(VPU_VIU_VENC_MUX_CTRL,0x11,4,8);
			devp->enc_info_addr = ENCI_INFO_READ;
                        break;
                case 2:                        
                        WR_BITS(VPU_VIU_VENC_MUX_CTRL,0x22,4,8);
			devp->enc_info_addr = ENCP_INFO_READ;
                        break;
                case 3:                        
                        WR_BITS(VPU_VIU_VENC_MUX_CTRL,0x44,4,8);
			devp->enc_info_addr = ENCT_INFO_READ;
                        break;
                default:
                        break;
        }
        devp->flag = 0; 
        return 0;
}
static void viuin_close(struct tvin_frontend_s *fe)
{        
        viuin_t *devp = container_of(fe,viuin_t,frontend);
        memset(&devp->parm,0,sizeof(vdin_parm_t));
        /*close the venc to vdin path*/
                        WR_BITS(VPU_VIU_VENC_MUX_CTRL,0,4,8);
}

static void viuin_start(struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt)
{
        //do something the same as start_amvdec_viu_in
        viuin_t *devp = container_of(fe,viuin_t,frontend);
        	if (devp->flag && AMVIUIN_DEC_START) {
		        printk("[viuin..]%s viu_in is started already.\n",__func__);
		return;
	}
	vsync_enter_line_max = 0;
	vsync_enter_line_threshold_overflow_count = 0;
	devp->flag = AMVIUIN_DEC_START;
	
	return;
}
static void viuin_stop(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
         
        viuin_t *devp = container_of(fe,viuin_t,frontend);
       	if (devp->flag && AMVIUIN_DEC_START) 
	        devp->flag |= AMVIUIN_DEC_STOP;
        else
                printk("[viuin..]%s viu in dec isn't start.\n",__func__);
        
}

static int viuin_isr(struct tvin_frontend_s *fe, unsigned int hcnt64)
{	
	viuin_t *devp = container_of(fe,viuin_t,frontend);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	vsync_enter_line_curr = (READ_VCBUS_REG(devp->enc_info_addr)>>16)&0x1fff;
	if(vsync_enter_line_curr > vsync_enter_line_max)
                vsync_enter_line_max = vsync_enter_line_curr;
	if(vsync_enter_line_max_threshold > vsync_enter_line_min_threshold){
		if((vsync_enter_line_curr > vsync_enter_line_max_threshold)||(vsync_enter_line_curr < vsync_enter_line_min_threshold)){
		        vsync_enter_line_threshold_overflow_count++;
		        return TVIN_BUF_SKIP;
	        }
	}
#endif
#ifdef CONFIG_GAMMA_AUTO_TUNE
	if (gamma_tune_en) {	
		devp->prop = fe->private_data;
		// calculate dnlp target data
		ve_dnlp_calculate_tgt(devp->prop);
		gamma_proc_enable = 1;
                gamma_adjust();
                if(gamma_dbg_en)
                        gamma_dbg_en = false; 
	}
#endif
        return 0;
        
}

static struct tvin_decoder_ops_s viu_dec_ops ={
        .support            = viuin_support,
	.open               = viuin_open,
	.start              = viuin_start,
	.stop               = viuin_stop,
	.close              = viuin_close,
	.decode_isr         = viuin_isr,
};

static void viuin_sig_propery(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop)
{
        viuin_t *devp = container_of(fe,viuin_t,frontend);

        #if ((MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6) || (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)\
			||(MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8B))
        prop->color_format = TVIN_YUV422;
        #else
        prop->color_format = TVIN_RGB444;
        #endif
        prop->dest_cfmt = devp->parm.dfmt;

        prop->scaling4w = devp->parm.dest_hactive;
	prop->scaling4h = devp->parm.dest_vactive;
	
	prop->vs = v_cut_offset;
	prop->ve = 0;
	prop->hs = 0;
	prop->he = 0;
        prop->decimation_ratio = 0;
}

static bool viu_check_frame_skip(struct tvin_frontend_s *fe)
{
	viuin_t *devp = container_of(fe,viuin_t,frontend);
	if(devp->parm.skip_count > 0){
		devp->parm.skip_count--;
		return true;
	}
	return false;
		
}

static struct tvin_state_machine_ops_s viu_sm_ops ={
       .get_sig_propery = viuin_sig_propery,
       .check_frame_skip = viu_check_frame_skip,
};

static struct class* gamma_proc_clsp;
static int viuin_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct viuin_s *viuin_devp;
        viuin_devp = kmalloc(sizeof(viuin_t),GFP_KERNEL);
        memset(viuin_devp,0,sizeof(viuin_t));
        if(!viuin_devp){
                printk("[viuin..]%s kmalloc error.\n",__func__);
                return -ENOMEM;
        }
	gamma_proc_clsp = class_create(THIS_MODULE, MODULE_NAME);
	if(IS_ERR(gamma_proc_clsp)){
		ret = PTR_ERR(gamma_proc_clsp);
		return ret;
	}
#ifdef CONFIG_GAMMA_AUTO_TUNE
	int i = 0;
	for(i = 0; gamma_proc_class_attrs[i].attr.name; i++){
		if(class_create_file(gamma_proc_clsp,&gamma_proc_class_attrs[i]) < 0)
			goto err;
	}
	for (i = 0; i < 64; i++) {
		ve_dnlp_tgt[i] = i << 2;
		//ve_dnlp_lpf[i] = ve_dnlp_tgt[i] << ve_dnlp_rt;
	}
#endif  
        sprintf(viuin_devp->frontend.name, "%s", DEVICE_NAME);
        if(!tvin_frontend_init(&viuin_devp->frontend,&viu_dec_ops,&viu_sm_ops,0)) {
                if(tvin_reg_frontend(&viuin_devp->frontend))
                        printk("[viuin..]%s register viu frontend error.\n",__func__);
        }        
        platform_set_drvdata(pdev,viuin_devp);
        printk("[viuin..]%s probe ok.\n",__func__);
	return 0;
#ifdef CONFIG_GAMMA_AUTO_TUNE
err:
	for(i=0; gamma_proc_class_attrs[i].attr.name; i++){
		class_remove_file(gamma_proc_clsp,&gamma_proc_class_attrs[i]);
	}
#endif
	class_destroy(gamma_proc_clsp); 
                
	return -1;  
}

static int viuin_remove(struct platform_device *pdev)
{        
        struct viuin_s *devp = platform_get_drvdata(pdev);
  #ifdef CONFIG_GAMMA_AUTO_TUNE
  		int i=0;
        for(i=0; gamma_proc_class_attrs[i].attr.name; i++) {
		class_remove_file(gamma_proc_clsp,&gamma_proc_class_attrs[i]);
	}
  #endif
        class_destroy(gamma_proc_clsp);
	if(devp){
                tvin_unreg_frontend(&devp->frontend);
                kfree(devp);
        }
	return 0;
}

static struct platform_driver viuin_driver = {
	.probe	= viuin_probe,
	.remove	= viuin_remove,
	.driver	= {
		.name	= DEVICE_NAME,
	}
};

static struct platform_device* viuin_device = NULL;

static int __init viuin_init_module(void)
{
	printk("[viuin..]%s viuin module init\n",__func__);
	viuin_device = platform_device_alloc(DEVICE_NAME,0);
	if (!viuin_device) {
		printk("[viuin..]%s failed to alloc viuin_device.\n",__func__);
		return -ENOMEM;
	}

	if (platform_device_add(viuin_device)) {
		platform_device_put(viuin_device);
		printk("[viuin..]%sfailed to add viuin_device.\n",__func__);
		return -ENODEV;
	}
	if (platform_driver_register(&viuin_driver)) {
		printk("[viuin..]%sfailed to register viuin driver.\n",__func__);
		platform_device_del(viuin_device);
		platform_device_put(viuin_device);
		return -ENODEV;
	}

	return 0;
}

static void __exit viuin_exit_module(void)
{
	printk("[viuin..]%s viuin module remove.\n",__func__);
	platform_driver_unregister(&viuin_driver);
                platform_device_unregister(viuin_device);
	return ;
}


module_init(viuin_init_module);
module_exit(viuin_exit_module);
MODULE_DESCRIPTION("AMLOGIC viu input driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("3.0.0");
