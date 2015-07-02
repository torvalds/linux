/*
 * Amlogic Meson HDMI Transmitter Driver
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

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>
//#include <linux/amports/canvas.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/am_regs.h>
#include <mach/clock.h>
#include <mach/power_gate.h>
#include <linux/clk.h>
#include <mach/clock.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/enc_clk_config.h>
#include <mach/io.h>
#include <mach/register.h>
#ifdef CONFIG_PANEL_IT6681
#include <linux/it6681.h>
#endif
#include <linux/amlogic/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_cec.h>

#if 0   //todo
#include "../hdmi_tx_hdcp.h"
#include "../hdmi_tx_compliance.h"
#endif
#include <mach/hdmi_tx_reg.h>
#include "tvenc_conf.h"
#ifdef Wr
#undef Wr
#endif
#ifdef Rd
#undef Rd
#endif
#define Wr(reg,val) WRITE_MPEG_REG(reg,val)
#define Rd(reg)   READ_MPEG_REG(reg)
#define Wr_reg_bits(reg, val, start, len) \
  Wr(reg, (Rd(reg) & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start)))

#define EDID_RAM_ADDR_SIZE      (4*128)

static void hdmi_audio_init(unsigned char spdif_flag);
static void hdmitx_dump_tvenc_reg(int cur_VIC, int printk_flag);

static void hdmi_phy_suspend(void);
static void hdmi_phy_wakeup(hdmitx_dev_t* hdmitx_device);

unsigned char hdmi_pll_mode = 0; /* 1, use external clk as hdmi pll source */
static unsigned char aud_para = 0x49;

#define HSYNC_POLARITY      1                       // HSYNC polarity: active high 
#define VSYNC_POLARITY      1                       // VSYNC polarity: active high
#define TX_INPUT_COLOR_DEPTH    0                   // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
#define TX_INPUT_COLOR_FORMAT   1                   // Pixel format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
#define TX_INPUT_COLOR_RANGE    0                   // Pixel range: 0=16-235/240; 1=16-240; 2=1-254; 3=0-255.


#define TX_OUTPUT_COLOR_RANGE   0                   // Pixel range: 0=16-235/240; 1=16-240; 2=1-254; 3=0-255.

#if 1
//spdif
#define TX_I2S_SPDIF        0                       // 0=SPDIF; 1=I2S.
#define TX_I2S_8_CHANNEL    0                       // 0=I2S 2-channel; 1=I2S 4 x 2-channel.
#else
//i2s 8 channel
#define TX_I2S_SPDIF        1                       // 0=SPDIF; 1=I2S.
#define TX_I2S_8_CHANNEL    1                       // 0=I2S 2-channel; 1=I2S 4 x 2-channel.
#endif

//static struct tasklet_struct EDID_tasklet;
static unsigned delay_flag = 0;
static unsigned serial_reg_val=0x1; //0x22;
static unsigned char i2s_to_spdif_flag=1;   // if current channel number is larger than 2ch, using i2s
static unsigned color_depth_f=0;
static unsigned color_space_f=0;
static unsigned char new_reset_sequence_flag=1;
static unsigned char power_mode=1;
static unsigned char power_off_vdac_flag=0;
    /* 0, do not use fixed tvenc val for all mode; 1, use fixed tvenc val mode for 480i; 2, use fixed tvenc val mode for all modes */
static unsigned char use_tvenc_conf_flag=1;

static unsigned char cur_vout_index = 1; //CONFIG_AM_TV_OUTPUT2

static unsigned long modulo(unsigned long a, unsigned long b)
{
    if (a >= b) {
        return(a-b);
    } else {
        return(a);
    }
}
        
static signed int to_signed(unsigned int a)
{
    if (a <= 7) {
        return(a);
    } else {
        return(a-16);
    }
}

static void delay_us (int us)
{
    //udelay(us);
    if(delay_flag&0x1)
        mdelay((us+999)/1000);
    else
    udelay(us);
} /* delay_us */

static irqreturn_t intr_handler(int irq, void *dev_instance)
{
    unsigned int data32;
    hdmitx_dev_t* hdmitx_device = (hdmitx_dev_t*)dev_instance;
    data32 = hdmi_rd_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT); 
    hdmi_print(IMP, SYS "irq %x\n", data32);
    if(hdmitx_device->hpd_lock == 1) {
        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR, 0xf);
        hdmi_print(IMP, HPD "HDMI hpd locked\n");
        return IRQ_HANDLED;
    }
    if(hdmitx_device->internal_mode_change == 1){     // if the irq from the internal mode change, just do nothing and return
        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR, 0x7);
        hdmi_print(IMP, SYS "hdmitx: ignore irq\n");
        return IRQ_HANDLED;
    }

    WRITE_MPEG_REG(HHI_GCLK_MPEG2, READ_MPEG_REG(HHI_GCLK_MPEG2) | (1<<4));     //Enable HDMI PCLK
    
    if (data32 & (1 << 1)) { //HPD falling
        hdmitx_device->vic_count = 0;
        hdmi_wr_only_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR,  1 << 1); //clear HPD falling interrupt in hdmi module 
        hdmitx_device->hpd_event = 2;
    }
    if (data32 & (1 << 0)) {  //HPD rising
        hdmi_wr_only_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR,  1 << 0); //clear HPD rising interrupt in hdmi module
        // If HPD asserts, then start DDC transaction
        if (hdmi_rd_reg(TX_HDCP_ST_EDID_STATUS) & (1<<1)) {
            // Start DDC transaction
            hdmitx_device->cur_edid_block=0;
            hdmitx_device->cur_phy_block_ptr=0;
            hdmitx_device->hpd_event = 1;
        // Error if HPD deasserts
        } else {
            hdmi_print(ERR, HPD "HPD deasserts!\n");
        }
    }
    if (data32 & (1 << 2)) { //TX EDID interrupt
        if((hdmitx_device->cur_edid_block+2)<=EDID_MAX_BLOCK){
            int ii, jj;
            for(jj=0;jj<2;jj++){
                for(ii=0;ii<128;ii++){
                    hdmitx_device->EDID_buf[hdmitx_device->cur_edid_block*128+ii]
                        =hdmi_rd_reg(0x600+hdmitx_device->cur_phy_block_ptr*128+ii);
                }
                hdmitx_device->cur_edid_block++;
                hdmitx_device->cur_phy_block_ptr++;
                hdmitx_device->cur_phy_block_ptr=hdmitx_device->cur_phy_block_ptr&0x3;
            }
        }        
//#ifndef AML_A3
//        /*walkaround: manually clear EDID interrupt*/
//        hdmi_wr_reg(TX_HDCP_EDID_CONFIG, hdmi_rd_reg(TX_HDCP_EDID_CONFIG) | (1<<1)); 
//        hdmi_wr_reg(TX_HDCP_EDID_CONFIG, hdmi_rd_reg(TX_HDCP_EDID_CONFIG) & ~(1<<1)); 
//        /**/
//#endif        
        //tasklet_schedule(&EDID_tasklet);
        hdmi_wr_only_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR,  1 << 2); //clear EDID rising interrupt in hdmi module 
    }
    if (!((data32 == 1) || (data32 == 2) || (data32 == 4))) {
        hdmi_print(ERR, SYS "Unkown HDMI Interrupt Source\n");
        hdmi_wr_only_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR,  data32); //clear unkown interrupt in hdmi module 
    }
//#ifdef AML_A3
    hdmi_rd_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR); // A read to allow the interrupt cleared in hdmi_module before next action
    hdmi_wr_only_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR, 0xf); //clear HPD falling interrupt in hdmi module
//#endif

    //aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR, 1 << 25);  //clear hdmi_tx interrupt
    return IRQ_HANDLED;
}

/*
 * mode: 1 means Progressive;  0 means interlaced
 */
static void enc_vpu_bridge_reset(int mode)
{
    unsigned int wr_clk = 0;

    printk("%s[%d]\n", __func__, __LINE__);
    wr_clk = (aml_read_reg32(P_VPU_HDMI_SETTING) & 0xf00) >> 8;
    if(mode) {
        aml_write_reg32(P_ENCP_VIDEO_EN, 0);
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 0, 0, 2);  // [    0] src_sel_enci: Disable ENCP output to HDMI
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 0, 8, 4);  // [    0] src_sel_enci: Disable ENCP output to HDMI
        mdelay(1);
        aml_write_reg32(P_ENCP_VIDEO_EN, 1);
        mdelay(1);
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, wr_clk, 8, 4);
        mdelay(1);
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 2, 0, 2);  // [    0] src_sel_enci: Enable ENCP output to HDMI
    } else {
        aml_write_reg32(P_ENCI_VIDEO_EN, 0);
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 0, 0, 2);  // [    0] src_sel_enci: Disable ENCI output to HDMI
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 0, 8, 4);  // [    0] src_sel_enci: Disable ENCP output to HDMI
        mdelay(1);
        aml_write_reg32(P_ENCI_VIDEO_EN, 1);
        mdelay(1);
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, wr_clk, 8, 4);
        mdelay(1);
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 1, 0, 2);  // [    0] src_sel_enci: Enable ENCI output to HDMI
    }
}

static void hdmi_tvenc1080i_set(Hdmi_tx_video_para_t* param)
{
    unsigned long VFIFO2VD_TO_HDMI_LATENCY = 2; // Annie 01Sep2011: Change value from 3 to 2, due to video encoder path delay change.
    unsigned long TOTAL_PIXELS, PIXEL_REPEAT_HDMI, PIXEL_REPEAT_VENC, ACTIVE_PIXELS;
    unsigned FRONT_PORCH = 88, HSYNC_PIXELS, ACTIVE_LINES = 0, INTERLACE_MODE, TOTAL_LINES, SOF_LINES, VSYNC_LINES;
    unsigned LINES_F0, LINES_F1 = 563,BACK_PORCH, EOF_LINES = 2, TOTAL_FRAMES;

    unsigned long total_pixels_venc ;
    unsigned long active_pixels_venc;
    unsigned long front_porch_venc  ;
    unsigned long hsync_pixels_venc ;

    unsigned long de_h_begin, de_h_end;
    unsigned long de_v_begin_even, de_v_end_even, de_v_begin_odd, de_v_end_odd;
    unsigned long hs_begin, hs_end;
    unsigned long vs_adjust;
    unsigned long vs_bline_evn, vs_eline_evn, vs_bline_odd, vs_eline_odd;
    unsigned long vso_begin_evn, vso_begin_odd;
    
    if(param->VIC==HDMI_1080i60){
         INTERLACE_MODE     = 1;
         PIXEL_REPEAT_VENC  = 1;
         PIXEL_REPEAT_HDMI  = 0;
         ACTIVE_PIXELS  =     (1920*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
         ACTIVE_LINES   =     (1080/(1+INTERLACE_MODE));    // Number of active lines per field.
         LINES_F0           = 562;
         LINES_F1           = 563;
         FRONT_PORCH        = 88;
         HSYNC_PIXELS       = 44;
         BACK_PORCH         = 148;
         EOF_LINES          = 2;
         VSYNC_LINES        = 5;
         SOF_LINES          = 15;
         TOTAL_FRAMES       = 4;
    }
    else if(param->VIC==HDMI_1080i50){
         INTERLACE_MODE     = 1;
         PIXEL_REPEAT_VENC  = 1;
         PIXEL_REPEAT_HDMI  = 0;
         ACTIVE_PIXELS  =     (1920*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
         ACTIVE_LINES   =     (1080/(1+INTERLACE_MODE));    // Number of active lines per field.
         LINES_F0           = 562;
         LINES_F1           = 563;
         FRONT_PORCH        = 528;
         HSYNC_PIXELS       = 44;
         BACK_PORCH         = 148;
         EOF_LINES          = 2;
         VSYNC_LINES        = 5;
         SOF_LINES          = 15;
         TOTAL_FRAMES       = 4;
    }
    TOTAL_PIXELS =(FRONT_PORCH+HSYNC_PIXELS+BACK_PORCH+ACTIVE_PIXELS); // Number of total pixels per line.
    TOTAL_LINES  =(LINES_F0+(LINES_F1*INTERLACE_MODE));                // Number of total lines per frame.

    total_pixels_venc = (TOTAL_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 2200 / 1 * 2 = 4400
    active_pixels_venc= (ACTIVE_PIXELS / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 1920 / 1 * 2 = 3840
    front_porch_venc  = (FRONT_PORCH   / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 88   / 1 * 2 = 176
    hsync_pixels_venc = (HSYNC_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 44   / 1 * 2 = 88

    aml_write_reg32(P_ENCP_VIDEO_MODE, aml_read_reg32(P_ENCP_VIDEO_MODE)|(1<<14)); // cfg_de_v = 1

    // Program DE timing
    de_h_begin = modulo(aml_read_reg32(P_ENCP_VIDEO_HAVON_BEGIN) + VFIFO2VD_TO_HDMI_LATENCY,  total_pixels_venc); // (383 + 3) % 4400 = 386
    de_h_end   = modulo(de_h_begin + active_pixels_venc,                        total_pixels_venc); // (386 + 3840) % 4400 = 4226
    aml_write_reg32(P_ENCP_DE_H_BEGIN, de_h_begin);    // 386
    aml_write_reg32(P_ENCP_DE_H_END,   de_h_end);      // 4226
    // Program DE timing for even field
    de_v_begin_even = aml_read_reg32(P_ENCP_VIDEO_VAVON_BLINE);       // 20
    de_v_end_even   = de_v_begin_even + ACTIVE_LINES;   // 20 + 540 = 560
    aml_write_reg32(P_ENCP_DE_V_BEGIN_EVEN,de_v_begin_even);   // 20
    aml_write_reg32(P_ENCP_DE_V_END_EVEN,  de_v_end_even);     // 560
    // Program DE timing for odd field if needed
    if (INTERLACE_MODE) {
        // Calculate de_v_begin_odd according to enc480p_timing.v:
        //wire[10:0]    cfg_ofld_vavon_bline    = {{7{ofld_vavon_ofst1 [3]}},ofld_vavon_ofst1 [3:0]} + cfg_video_vavon_bline    + ofld_line;
        de_v_begin_odd  = to_signed((aml_read_reg32(P_ENCP_VIDEO_OFLD_VOAV_OFST) & 0xf0)>>4) + de_v_begin_even + (TOTAL_LINES-1)/2; // 1 + 20 + (1125-1)/2 = 583
        de_v_end_odd    = de_v_begin_odd + ACTIVE_LINES;    // 583 + 540 = 1123
        aml_write_reg32(P_ENCP_DE_V_BEGIN_ODD, de_v_begin_odd);// 583
        aml_write_reg32(P_ENCP_DE_V_END_ODD,   de_v_end_odd);  // 1123
    }

    // Program Hsync timing
    if (de_h_end + front_porch_venc >= total_pixels_venc) {
        hs_begin    = de_h_end + front_porch_venc - total_pixels_venc; // 4226 + 176 - 4400 = 2





        vs_adjust   = 1;
    } else {
        hs_begin    = de_h_end + front_porch_venc;
        vs_adjust   = 0;
    }
    hs_end  = modulo(hs_begin + hsync_pixels_venc,   total_pixels_venc); // (2 + 88) % 4400 = 90
    aml_write_reg32(P_ENCP_DVI_HSO_BEGIN,  hs_begin);  // 2
    aml_write_reg32(P_ENCP_DVI_HSO_END,    hs_end);    // 90
    
    // Program Vsync timing for even field
    if (de_v_begin_even >= SOF_LINES + VSYNC_LINES + (1-vs_adjust)) {
        vs_bline_evn = de_v_begin_even - SOF_LINES - VSYNC_LINES - (1-vs_adjust); // 20 - 15 - 5 - 0 = 0
    } else {
        vs_bline_evn = TOTAL_LINES + de_v_begin_even - SOF_LINES - VSYNC_LINES - (1-vs_adjust);
    }
    vs_eline_evn = modulo(vs_bline_evn + VSYNC_LINES, TOTAL_LINES); // (0 + 5) % 1125 = 5
    aml_write_reg32(P_ENCP_DVI_VSO_BLINE_EVN, vs_bline_evn);   // 0
    aml_write_reg32(P_ENCP_DVI_VSO_ELINE_EVN, vs_eline_evn);   // 5
    vso_begin_evn = hs_begin; // 2
    aml_write_reg32(P_ENCP_DVI_VSO_BEGIN_EVN, vso_begin_evn);  // 2
    aml_write_reg32(P_ENCP_DVI_VSO_END_EVN,   vso_begin_evn);  // 2
    // Program Vsync timing for odd field if needed
    if (INTERLACE_MODE) {
        vs_bline_odd = de_v_begin_odd-1 - SOF_LINES - VSYNC_LINES;  // 583-1 - 15 - 5   = 562
        vs_eline_odd = de_v_begin_odd-1 - SOF_LINES;                // 583-1 - 15       = 567
        vso_begin_odd   = modulo(hs_begin + (total_pixels_venc>>1), total_pixels_venc); // (2 + 4400/2) % 4400 = 2202
        aml_write_reg32(P_ENCP_DVI_VSO_BLINE_ODD, vs_bline_odd);   // 562
        aml_write_reg32(P_ENCP_DVI_VSO_ELINE_ODD, vs_eline_odd);   // 567
        aml_write_reg32(P_ENCP_DVI_VSO_BEGIN_ODD, vso_begin_odd);  // 2202
        aml_write_reg32(P_ENCP_DVI_VSO_END_ODD,   vso_begin_odd);  // 2202
    }

    // Annie 01Sep2011: Register VENC_DVI_SETTING and VENC_DVI_SETTING_MORE are no long valid, use VPU_HDMI_SETTING instead.
    aml_write_reg32(P_VPU_HDMI_SETTING, (0                                 << 0) | // [    0] src_sel_enci
                         (0                                 << 1) | // [    1] src_sel_encp
                         (HSYNC_POLARITY                    << 2) | // [    2] inv_hsync. 1=Invert Hsync polarity.
                         (VSYNC_POLARITY                    << 3) | // [    3] inv_vsync. 1=Invert Vsync polarity.
                         (0                                 << 4) | // [    4] inv_dvi_clk. 1=Invert clock to external DVI, (clock invertion exists at internal HDMI).
                         (((TX_INPUT_COLOR_FORMAT==0)?1:0)  << 5) | // [ 7: 5] data_comp_map. Input data is CrYCb(BRG), map the output data to desired format:
                                                                    //                          0=output CrYCb(BRG);
                                                                    //                          1=output YCbCr(RGB);
                                                                    //                          2=output YCrCb(RBG);
                                                                    //                          3=output CbCrY(GBR);
                                                                    //                          4=output CbYCr(GRB);
                                                                    //                          5=output CrCbY(BGR);
                                                                    //                          6,7=Rsrv.
#ifdef DOUBLE_CLK_720P_1080I
                         (0                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
#else                         
                         (1                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
#endif                         
                         (0                                 <<12)   // [15:12] rd_rate. 0=A read every clk2; 1=A read every 2 clk2; ...; 15=A read every 16 clk2.
    );
    aml_set_reg32_bits(P_VPU_HDMI_SETTING, 1, 1, 1);  // [    1] src_sel_encp: Enable ENCP output to HDMI
}    

static void hdmi_tvenc4k2k_set(Hdmi_tx_video_para_t* param)
{
    unsigned long VFIFO2VD_TO_HDMI_LATENCY = 2; // Annie 01Sep2011: Change value from 3 to 2, due to video encoder path delay change.
    unsigned long TOTAL_PIXELS = 4400, PIXEL_REPEAT_HDMI, PIXEL_REPEAT_VENC, ACTIVE_PIXELS = 3840;
    unsigned FRONT_PORCH = 1020, HSYNC_PIXELS, ACTIVE_LINES = 2160, INTERLACE_MODE, TOTAL_LINES, SOF_LINES, VSYNC_LINES;
    unsigned LINES_F0 = 2250, LINES_F1 = 2250,BACK_PORCH, EOF_LINES = 8, TOTAL_FRAMES;

    unsigned long total_pixels_venc ;
    unsigned long active_pixels_venc;
    unsigned long front_porch_venc  ;
    unsigned long hsync_pixels_venc ;

    unsigned long de_h_begin, de_h_end;
    unsigned long de_v_begin_even, de_v_end_even, de_v_begin_odd, de_v_end_odd;
    unsigned long hs_begin, hs_end;
    unsigned long vs_adjust;
    unsigned long vs_bline_evn, vs_eline_evn, vs_bline_odd, vs_eline_odd;
    unsigned long vso_begin_evn, vso_begin_odd;

    if(param->VIC==HDMI_4k2k_30){
         INTERLACE_MODE     = 0;
         PIXEL_REPEAT_VENC  = 0;
         PIXEL_REPEAT_HDMI  = 0;
         ACTIVE_PIXELS  =     (3840*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
         ACTIVE_LINES   =     (2160/(1+INTERLACE_MODE));    // Number of active lines per field.
         LINES_F0           = 2250;
         LINES_F1           = 2250;
         FRONT_PORCH        = 176;
         HSYNC_PIXELS       = 88;
         BACK_PORCH         = 296;
         EOF_LINES          = 8 + 1;
         VSYNC_LINES        = 10;
         SOF_LINES          = 72 + 1;
         TOTAL_FRAMES       = 3;
    }
    else if(param->VIC==HDMI_4k2k_25){
         INTERLACE_MODE     = 0;
         PIXEL_REPEAT_VENC  = 0;
         PIXEL_REPEAT_HDMI  = 0;
         ACTIVE_PIXELS  =     (3840*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
         ACTIVE_LINES   =     (2160/(1+INTERLACE_MODE));    // Number of active lines per field.
         LINES_F0           = 2250;
         LINES_F1           = 2250;
         FRONT_PORCH        = 1056;
         HSYNC_PIXELS       = 88;
         BACK_PORCH         = 296;
         EOF_LINES          = 8 + 1;
         VSYNC_LINES        = 10;
         SOF_LINES          = 72 + 1;
         TOTAL_FRAMES       = 3;
    }
    else if(param->VIC==HDMI_4k2k_24){
         INTERLACE_MODE     = 0;
         PIXEL_REPEAT_VENC  = 0;
         PIXEL_REPEAT_HDMI  = 0;
         ACTIVE_PIXELS  =     (3840*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
         ACTIVE_LINES   =     (2160/(1+INTERLACE_MODE));    // Number of active lines per field.
         LINES_F0           = 2250;
         LINES_F1           = 2250;
         FRONT_PORCH        = 1276;
         HSYNC_PIXELS       = 88;
         BACK_PORCH         = 296;
         EOF_LINES          = 8 + 1;
         VSYNC_LINES        = 10;
         SOF_LINES          = 72 + 1;
         TOTAL_FRAMES       = 3;
    }
    else if(param->VIC==HDMI_4k2k_smpte_24){
         INTERLACE_MODE     = 0;
         PIXEL_REPEAT_VENC  = 0;
         PIXEL_REPEAT_HDMI  = 0;
         ACTIVE_PIXELS  =     (4096*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
         ACTIVE_LINES   =     (2160/(1+INTERLACE_MODE));    // Number of active lines per field.
         LINES_F0           = 2250;
         LINES_F1           = 2250;
         FRONT_PORCH        = 1020;
         HSYNC_PIXELS       = 88;
         BACK_PORCH         = 296;
         EOF_LINES          = 8 + 1;
         VSYNC_LINES        = 10;
         SOF_LINES          = 72 + 1;
         TOTAL_FRAMES       = 3;
    }
    else {
        // nothing
    }

    TOTAL_PIXELS       = (FRONT_PORCH+HSYNC_PIXELS+BACK_PORCH+ACTIVE_PIXELS); // Number of total pixels per line.
    TOTAL_LINES        = (LINES_F0+(LINES_F1*INTERLACE_MODE));                // Number of total lines per frame.

    total_pixels_venc = (TOTAL_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC);
    active_pixels_venc= (ACTIVE_PIXELS / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC);
    front_porch_venc  = (FRONT_PORCH   / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC);
    hsync_pixels_venc = (HSYNC_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC);

    de_h_begin = modulo(aml_read_reg32(P_ENCP_VIDEO_HAVON_BEGIN) + VFIFO2VD_TO_HDMI_LATENCY,  total_pixels_venc);
    de_h_end   = modulo(de_h_begin + active_pixels_venc,                        total_pixels_venc);
    aml_write_reg32(P_ENCP_DE_H_BEGIN, de_h_begin);
    aml_write_reg32(P_ENCP_DE_H_END,   de_h_end);
    // Program DE timing for even field
    de_v_begin_even = aml_read_reg32(P_ENCP_VIDEO_VAVON_BLINE);
    de_v_end_even   = modulo(de_v_begin_even + ACTIVE_LINES, TOTAL_LINES);
    aml_write_reg32(P_ENCP_DE_V_BEGIN_EVEN,de_v_begin_even);
    aml_write_reg32(P_ENCP_DE_V_END_EVEN,  de_v_end_even);
    // Program DE timing for odd field if needed
    if (INTERLACE_MODE) {
        // Calculate de_v_begin_odd according to enc480p_timing.v:
        //wire[10:0]    cfg_ofld_vavon_bline    = {{7{ofld_vavon_ofst1 [3]}},ofld_vavon_ofst1 [3:0]} + cfg_video_vavon_bline    + ofld_line;
        de_v_begin_odd  = to_signed((aml_read_reg32(P_ENCP_VIDEO_OFLD_VOAV_OFST) & 0xf0)>>4) + de_v_begin_even + (TOTAL_LINES-1)/2;
        de_v_end_odd    = modulo(de_v_begin_odd + ACTIVE_LINES, TOTAL_LINES);
        aml_write_reg32(P_ENCP_DE_V_BEGIN_ODD, de_v_begin_odd);
        aml_write_reg32(P_ENCP_DE_V_END_ODD,   de_v_end_odd);
    }

    // Program Hsync timing
    if (de_h_end + front_porch_venc >= total_pixels_venc) {
        hs_begin    = de_h_end + front_porch_venc - total_pixels_venc;
        vs_adjust   = 1;
    } else {
        hs_begin    = de_h_end + front_porch_venc;
        vs_adjust   = 1;
    }
    hs_end  = modulo(hs_begin + hsync_pixels_venc,   total_pixels_venc);
    aml_write_reg32(P_ENCP_DVI_HSO_BEGIN,  hs_begin);
    aml_write_reg32(P_ENCP_DVI_HSO_END,    hs_end);
    
    // Program Vsync timing for even field
    if (de_v_begin_even >= SOF_LINES + VSYNC_LINES + (1-vs_adjust)) {
        vs_bline_evn = de_v_begin_even - SOF_LINES - VSYNC_LINES - (1-vs_adjust);
    } else {
        vs_bline_evn = TOTAL_LINES + de_v_begin_even - SOF_LINES - VSYNC_LINES - (1-vs_adjust);
    }
    vs_eline_evn = modulo(vs_bline_evn + VSYNC_LINES, TOTAL_LINES);
    aml_write_reg32(P_ENCP_DVI_VSO_BLINE_EVN, vs_bline_evn);
    aml_write_reg32(P_ENCP_DVI_VSO_ELINE_EVN, vs_eline_evn);
    vso_begin_evn = hs_begin;
    aml_write_reg32(P_ENCP_DVI_VSO_BEGIN_EVN, vso_begin_evn);
    aml_write_reg32(P_ENCP_DVI_VSO_END_EVN,   vso_begin_evn);
    // Program Vsync timing for odd field if needed
    if (INTERLACE_MODE) {
        vs_bline_odd = de_v_begin_odd-1 - SOF_LINES - VSYNC_LINES;
        vs_eline_odd = de_v_begin_odd-1 - SOF_LINES;
        vso_begin_odd   = modulo(hs_begin + (total_pixels_venc>>1), total_pixels_venc);
        aml_write_reg32(P_ENCP_DVI_VSO_BLINE_ODD, vs_bline_odd);
        aml_write_reg32(P_ENCP_DVI_VSO_ELINE_ODD, vs_eline_odd);
        aml_write_reg32(P_ENCP_DVI_VSO_BEGIN_ODD, vso_begin_odd);
        aml_write_reg32(P_ENCP_DVI_VSO_END_ODD,   vso_begin_odd);
    }
    aml_write_reg32(P_VPU_HDMI_SETTING, (0                                 << 0) | // [    0] src_sel_enci
                         (0                                 << 1) | // [    1] src_sel_encp
                         (HSYNC_POLARITY                    << 2) | // [    2] inv_hsync. 1=Invert Hsync polarity.
                         (VSYNC_POLARITY                    << 3) | // [    3] inv_vsync. 1=Invert Vsync polarity.
                         (0                                 << 4) | // [    4] inv_dvi_clk. 1=Invert clock to external DVI, (clock invertion exists at internal HDMI).
                         (((TX_INPUT_COLOR_FORMAT==0)?1:0)  << 5) | // [ 7: 5] data_comp_map. Input data is CrYCb(BRG), map the output data to desired format:
                                                                    //                          0=output CrYCb(BRG);
                                                                    //                          1=output YCbCr(RGB);
                                                                    //                          2=output YCrCb(RBG);
                                                                    //                          3=output CbCrY(GBR);
                                                                    //                          4=output CbYCr(GRB);
                                                                    //                          5=output CrCbY(BGR);
                                                                    //                          6,7=Rsrv.
                         (0                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
                         (0                                 <<12)   // [15:12] rd_rate. 0=A read every clk2; 1=A read every 2 clk2; ...; 15=A read every 16 clk2.
    );
    aml_set_reg32_bits(P_VPU_HDMI_SETTING, 1, 1, 1);  // [    1] src_sel_encp: Enable ENCP output to HDMI
    aml_write_reg32(P_ENCP_VIDEO_EN, 1); // Enable VENC
}

static void hdmi_tvenc480i_set(Hdmi_tx_video_para_t* param)
{
    unsigned long VFIFO2VD_TO_HDMI_LATENCY = 1; // Annie 01Sep2011: Change value from 2 to 1, due to video encoder path delay change.
    unsigned long TOTAL_PIXELS, PIXEL_REPEAT_HDMI, PIXEL_REPEAT_VENC, ACTIVE_PIXELS;
    unsigned FRONT_PORCH = 38, HSYNC_PIXELS = 124, ACTIVE_LINES = 0, INTERLACE_MODE, TOTAL_LINES, SOF_LINES, VSYNC_LINES;
    unsigned LINES_F0 = 262, LINES_F1 = 263, BACK_PORCH = 114, EOF_LINES = 2, TOTAL_FRAMES;

    unsigned long total_pixels_venc ;
    unsigned long active_pixels_venc;
    unsigned long front_porch_venc  ;
    unsigned long hsync_pixels_venc ;

    unsigned long de_h_begin, de_h_end;
    unsigned long de_v_begin_even, de_v_end_even, de_v_begin_odd, de_v_end_odd;
    unsigned long hs_begin, hs_end;
    unsigned long vs_adjust;
    unsigned long vs_bline_evn, vs_eline_evn, vs_bline_odd, vs_eline_odd;
    unsigned long vso_begin_evn, vso_begin_odd;

    switch(param->VIC) {
    case HDMI_480i60:
    case HDMI_480i60_16x9:
    case HDMI_480i60_16x9_rpt:
        INTERLACE_MODE     = 1;
        PIXEL_REPEAT_VENC  = 1;
        PIXEL_REPEAT_HDMI  = 1;
        ACTIVE_PIXELS      = (720*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES       = (480/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0           = 262;
        LINES_F1           = 263;
        FRONT_PORCH        = 38;
        HSYNC_PIXELS       = 124;
        BACK_PORCH         = 114;
        EOF_LINES          = 4;
        VSYNC_LINES        = 3;
        SOF_LINES          = 15;
        TOTAL_FRAMES       = 4;
        break;
    case HDMI_576i50:
    case HDMI_576i50_16x9:
    case HDMI_576i50_16x9_rpt:
        INTERLACE_MODE     = 1;
        PIXEL_REPEAT_VENC  = 1;
        PIXEL_REPEAT_HDMI  = 1;
        ACTIVE_PIXELS      = (720*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES       = (576/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0           = 312;
        LINES_F1           = 313;
        FRONT_PORCH        = 24;
        HSYNC_PIXELS       = 126;
        BACK_PORCH         = 138;
        EOF_LINES          = 2;
        VSYNC_LINES        = 3;
        SOF_LINES          = 19;
        TOTAL_FRAMES       = 4;
        break;
    default:
        break;
    }

    TOTAL_PIXELS =(FRONT_PORCH+HSYNC_PIXELS+BACK_PORCH+ACTIVE_PIXELS); // Number of total pixels per line.
    TOTAL_LINES  =(LINES_F0+(LINES_F1*INTERLACE_MODE));                // Number of total lines per frame.

    total_pixels_venc = (TOTAL_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 1716 / 2 * 2 = 1716
    active_pixels_venc= (ACTIVE_PIXELS / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 1440 / 2 * 2 = 1440
    front_porch_venc  = (FRONT_PORCH   / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 38   / 2 * 2 = 38
    hsync_pixels_venc = (HSYNC_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 124  / 2 * 2 = 124

    // Annie 01Sep2011: Comment out the following 2 lines. Because ENCP is not used for 480i and 576i.
    //Wr(ENCP_VIDEO_MODE,Rd(ENCP_VIDEO_MODE)|(1<<14)); // cfg_de_v = 1

    // Program DE timing
    // Annie 01Sep2011: for 480/576i, replace VFIFO2VD_PIXEL_START with ENCI_VFIFO2VD_PIXEL_START.
    de_h_begin = modulo(aml_read_reg32(P_ENCI_VFIFO2VD_PIXEL_START) + VFIFO2VD_TO_HDMI_LATENCY,   total_pixels_venc); // (233 + 2) % 1716 = 235
    de_h_end   = modulo(de_h_begin + active_pixels_venc, total_pixels_venc); // (235 + 1440) % 1716 = 1675
    aml_write_reg32(P_ENCI_DE_H_BEGIN, de_h_begin);    // 235
    aml_write_reg32(P_ENCI_DE_H_END,   de_h_end);      // 1675

    // Annie 01Sep2011: for 480/576i, replace VFIFO2VD_LINE_TOP/BOT_START with ENCI_VFIFO2VD_LINE_TOP/BOT_START.
    de_v_begin_even = aml_read_reg32(P_ENCI_VFIFO2VD_LINE_TOP_START);      // 17
    de_v_end_even   = de_v_begin_even + ACTIVE_LINES;   // 17 + 240 = 257
    de_v_begin_odd  = aml_read_reg32(P_ENCI_VFIFO2VD_LINE_BOT_START);      // 18
    de_v_end_odd    = de_v_begin_odd + ACTIVE_LINES;    // 18 + 480/2 = 258
    aml_write_reg32(P_ENCI_DE_V_BEGIN_EVEN,de_v_begin_even);   // 17
    aml_write_reg32(P_ENCI_DE_V_END_EVEN,  de_v_end_even);     // 257
    aml_write_reg32(P_ENCI_DE_V_BEGIN_ODD, de_v_begin_odd);    // 18
    aml_write_reg32(P_ENCI_DE_V_END_ODD,   de_v_end_odd);      // 258

    // Program Hsync timing
    if (de_h_end + front_porch_venc >= total_pixels_venc) {
        hs_begin    = de_h_end + front_porch_venc - total_pixels_venc;
        vs_adjust   = 1;
    } else {
        hs_begin    = de_h_end + front_porch_venc; // 1675 + 38 = 1713
        vs_adjust   = 0;
    }
    hs_end  = modulo(hs_begin + hsync_pixels_venc,   total_pixels_venc); // (1713 + 124) % 1716 = 121
    aml_write_reg32(P_ENCI_DVI_HSO_BEGIN,  hs_begin);  // 1713
    aml_write_reg32(P_ENCI_DVI_HSO_END,    hs_end);    // 121
    
    // Program Vsync timing for even field
    if (de_v_end_odd-1 + EOF_LINES + vs_adjust >= LINES_F1) {
        vs_bline_evn = de_v_end_odd-1 + EOF_LINES + vs_adjust - LINES_F1;
        vs_eline_evn = vs_bline_evn + VSYNC_LINES;
        aml_write_reg32(P_ENCI_DVI_VSO_BLINE_EVN, vs_bline_evn);
        //vso_bline_evn_reg_wr_cnt ++;
        aml_write_reg32(P_ENCI_DVI_VSO_ELINE_EVN, vs_eline_evn);
        //vso_eline_evn_reg_wr_cnt ++;
        aml_write_reg32(P_ENCI_DVI_VSO_BEGIN_EVN, hs_begin);
        aml_write_reg32(P_ENCI_DVI_VSO_END_EVN,   hs_begin);
    } else {
        vs_bline_odd = de_v_end_odd-1 + EOF_LINES + vs_adjust; // 258-1 + 4 + 0 = 261
        aml_write_reg32(P_ENCI_DVI_VSO_BLINE_ODD, vs_bline_odd); // 261
        //vso_bline_odd_reg_wr_cnt ++;
        aml_write_reg32(P_ENCI_DVI_VSO_BEGIN_ODD, hs_begin);  // 1713
        if (vs_bline_odd + VSYNC_LINES >= LINES_F1) {
            vs_eline_evn = vs_bline_odd + VSYNC_LINES - LINES_F1; // 261 + 3 - 263 = 1
            aml_write_reg32(P_ENCI_DVI_VSO_ELINE_EVN, vs_eline_evn);   // 1
            //vso_eline_evn_reg_wr_cnt ++;
            aml_write_reg32(P_ENCI_DVI_VSO_END_EVN,   hs_begin);       // 1713
        } else {
            vs_eline_odd = vs_bline_odd + VSYNC_LINES;
            aml_write_reg32(P_ENCI_DVI_VSO_ELINE_ODD, vs_eline_odd);
            //vso_eline_odd_reg_wr_cnt ++;
            aml_write_reg32(P_ENCI_DVI_VSO_END_ODD,   hs_begin);
        }
    }
    // Program Vsync timing for odd field
    if (de_v_end_even-1 + EOF_LINES + 1 >= LINES_F0) {
        vs_bline_odd = de_v_end_even-1 + EOF_LINES + 1 - LINES_F0;
        vs_eline_odd = vs_bline_odd + VSYNC_LINES;
        aml_write_reg32(P_ENCI_DVI_VSO_BLINE_ODD, vs_bline_odd);
        //vso_bline_odd_reg_wr_cnt ++;
        aml_write_reg32(P_ENCI_DVI_VSO_ELINE_ODD, vs_eline_odd);
        //vso_eline_odd_reg_wr_cnt ++;
        vso_begin_odd   = modulo(hs_begin + (total_pixels_venc>>1), total_pixels_venc);
        aml_write_reg32(P_ENCI_DVI_VSO_BEGIN_ODD, vso_begin_odd);
        aml_write_reg32(P_ENCI_DVI_VSO_END_ODD,   vso_begin_odd);
    } else {
        vs_bline_evn = de_v_end_even-1 + EOF_LINES + 1; // 257-1 + 4 + 1 = 261
        aml_write_reg32(P_ENCI_DVI_VSO_BLINE_EVN, vs_bline_evn); // 261
        //vso_bline_evn_reg_wr_cnt ++;
        vso_begin_evn   = modulo(hs_begin + (total_pixels_venc>>1), total_pixels_venc);   // (1713 + 1716/2) % 1716 = 855
        aml_write_reg32(P_ENCI_DVI_VSO_BEGIN_EVN, vso_begin_evn);  // 855
        if (vs_bline_evn + VSYNC_LINES >= LINES_F0) {
            vs_eline_odd = vs_bline_evn + VSYNC_LINES - LINES_F0; // 261 + 3 - 262 = 2
            aml_write_reg32(P_ENCI_DVI_VSO_ELINE_ODD, vs_eline_odd);   // 2
            //vso_eline_odd_reg_wr_cnt ++;
            aml_write_reg32(P_ENCI_DVI_VSO_END_ODD,   vso_begin_evn);  // 855
        } else {
            vs_eline_evn = vs_bline_evn + VSYNC_LINES;
            aml_write_reg32(P_ENCI_DVI_VSO_ELINE_EVN, vs_eline_evn);
            //vso_eline_evn_reg_wr_cnt ++;
            aml_write_reg32(P_ENCI_DVI_VSO_END_EVN,   vso_begin_evn);
        }
    }

    // Check if there are duplicate or missing timing settings
    //if ((vso_bline_evn_reg_wr_cnt != 1) || (vso_bline_odd_reg_wr_cnt != 1) ||
    //    (vso_eline_evn_reg_wr_cnt != 1) || (vso_eline_odd_reg_wr_cnt != 1)) {
        //stimulus_print("[TEST.C] Error: Multiple or missing timing settings on reg ENCI_DVI_VSO_B(E)LINE_EVN(ODD)!\n");
        //stimulus_finish_fail(1);
    //}

    // Annie 01Sep2011: Register VENC_DVI_SETTING and VENC_DVI_SETTING_MORE are no long valid, use VPU_HDMI_SETTING instead.
    aml_write_reg32(P_VPU_HDMI_SETTING, (0                                 << 0) | // [    0] src_sel_enci
                         (0                                 << 1) | // [    1] src_sel_encp
                         (0                                 << 2) | // [    2] inv_hsync. 1=Invert Hsync polarity.
                         (0                                 << 3) | // [    3] inv_vsync. 1=Invert Vsync polarity.
                         (0                                 << 4) | // [    4] inv_dvi_clk. 1=Invert clock to external DVI, (clock invertion exists at internal HDMI).
                         (((TX_INPUT_COLOR_FORMAT==0)?1:0)  << 5) | // [ 7: 5] data_comp_map. Input data is CrYCb(BRG), map the output data to desired format:
                                                                    //                          0=output CrYCb(BRG);
                                                                    //                          1=output YCbCr(RGB);
                                                                    //                          2=output YCrCb(RBG);
                                                                    //                          3=output CbCrY(GBR);
                                                                    //                          4=output CbYCr(GRB);
                                                                    //                          5=output CrCbY(BGR);
                                                                    //                          6,7=Rsrv.
                         (1                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
                         (1                                 <<12)   // [15:12] rd_rate. 0=A read every clk2; 1=A read every 2 clk2; ...; 15=A read every 16 clk2.
    );
    if((param->VIC == HDMI_480i60_16x9_rpt) || (param->VIC == HDMI_576i50_16x9_rpt)) {
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 3, 12, 4);
    }
    aml_set_reg32_bits(P_VPU_HDMI_SETTING, 1, 0, 1);  // [    0] src_sel_enci: Enable ENCI output to HDMI
}    

static void hdmi_tvenc_set(Hdmi_tx_video_para_t *param)
{
    unsigned long VFIFO2VD_TO_HDMI_LATENCY = 2; // Annie 01Sep2011: Change value from 3 to 2, due to video encoder path delay change.
    unsigned long TOTAL_PIXELS, PIXEL_REPEAT_HDMI, PIXEL_REPEAT_VENC, ACTIVE_PIXELS;
    unsigned FRONT_PORCH, HSYNC_PIXELS, ACTIVE_LINES, INTERLACE_MODE, TOTAL_LINES, SOF_LINES, VSYNC_LINES;
    unsigned LINES_F0, LINES_F1,BACK_PORCH, EOF_LINES, TOTAL_FRAMES;

    unsigned long total_pixels_venc ;
    unsigned long active_pixels_venc;
    unsigned long front_porch_venc  ;
    unsigned long hsync_pixels_venc ;

    unsigned long de_h_begin, de_h_end;
    unsigned long de_v_begin_even, de_v_end_even, de_v_begin_odd, de_v_end_odd;
    unsigned long hs_begin, hs_end;
    unsigned long vs_adjust;
    unsigned long vs_bline_evn, vs_eline_evn, vs_bline_odd, vs_eline_odd;
    unsigned long vso_begin_evn, vso_begin_odd;

    switch(param->VIC) {
    case HDMI_480p60:
    case HDMI_480p60_16x9:
    case HDMI_480p60_16x9_rpt:
        INTERLACE_MODE     = 0;
        PIXEL_REPEAT_VENC  = 1;
        PIXEL_REPEAT_HDMI  = 0;
        ACTIVE_PIXELS      = (720*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES       = (480/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0           = 525;
        LINES_F1           = 525;
        FRONT_PORCH        = 16;
        HSYNC_PIXELS       = 62;
        BACK_PORCH         = 60;
        EOF_LINES          = 9;
        VSYNC_LINES        = 6;
        SOF_LINES          = 30;
        TOTAL_FRAMES       = 4;
        break;
    case HDMI_576p50:
    case HDMI_576p50_16x9:
    case HDMI_576p50_16x9_rpt:
        INTERLACE_MODE     = 0;
        PIXEL_REPEAT_VENC  = 1;
        PIXEL_REPEAT_HDMI  = 0;
        ACTIVE_PIXELS      = (720*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES       = (576/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0           = 625;
        LINES_F1           = 625;
        FRONT_PORCH        = 12;
        HSYNC_PIXELS       = 64;
        BACK_PORCH         = 68;
        EOF_LINES          = 5;
        VSYNC_LINES        = 5;
        SOF_LINES          = 39;
        TOTAL_FRAMES       = 4;
        break;
    case HDMI_720p60:
        INTERLACE_MODE     = 0;
        PIXEL_REPEAT_VENC  = 1;
        PIXEL_REPEAT_HDMI  = 0;
        ACTIVE_PIXELS      = (1280*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES       = (720/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0           = 750;
        LINES_F1           = 750;
        FRONT_PORCH        = 110;
        HSYNC_PIXELS       = 40;
        BACK_PORCH         = 220;
        EOF_LINES          = 5;
        VSYNC_LINES        = 5;
        SOF_LINES          = 20;
        TOTAL_FRAMES       = 4;
        break;
    case HDMI_720p50:
        INTERLACE_MODE     = 0;
        PIXEL_REPEAT_VENC  = 1;
        PIXEL_REPEAT_HDMI  = 0;
        ACTIVE_PIXELS      = (1280*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES       = (720/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0           = 750;
        LINES_F1           = 750;
        FRONT_PORCH        = 440;
        HSYNC_PIXELS       = 40;
        BACK_PORCH         = 220;
        EOF_LINES          = 5;
        VSYNC_LINES        = 5;
        SOF_LINES          = 20;
        TOTAL_FRAMES       = 4;
        break;
    case HDMI_1080p50:
        INTERLACE_MODE      = 0;
        PIXEL_REPEAT_VENC   = 0;
        PIXEL_REPEAT_HDMI   = 0;
        ACTIVE_PIXELS       = (1920*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES        = (1080/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0            = 1125;
        LINES_F1            = 1125;
        FRONT_PORCH         = 528;
        HSYNC_PIXELS        = 44;
        BACK_PORCH          = 148;
        EOF_LINES           = 4;
        VSYNC_LINES         = 5;
        SOF_LINES           = 36;
        TOTAL_FRAMES        = 4;
        break;
    case HDMI_1080p24:
        INTERLACE_MODE      = 0;
        PIXEL_REPEAT_VENC   = 0;
        PIXEL_REPEAT_HDMI   = 0;
        ACTIVE_PIXELS       = (1920*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES        = (1080/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0            = 1125;
        LINES_F1            = 1125;
        FRONT_PORCH         = 638;
        HSYNC_PIXELS        = 44;
        BACK_PORCH          = 148;
        EOF_LINES           = 4;
        VSYNC_LINES         = 5;
        SOF_LINES           = 36;
        TOTAL_FRAMES        = 4;
        break;
    case HDMI_1080p60:
    case HDMI_1080p30:
    default:
        INTERLACE_MODE      = 0;
        PIXEL_REPEAT_VENC   = 0;
        PIXEL_REPEAT_HDMI   = 0;
        ACTIVE_PIXELS       = (1920*(1+PIXEL_REPEAT_HDMI)); // Number of active pixels per line.
        ACTIVE_LINES        = (1080/(1+INTERLACE_MODE));    // Number of active lines per field.
        LINES_F0            = 1125;
        LINES_F1            = 1125;
        FRONT_PORCH         = 88;
        HSYNC_PIXELS        = 44;
        BACK_PORCH          = 148;
        EOF_LINES           = 4;
        VSYNC_LINES         = 5;
        SOF_LINES           = 36;
        TOTAL_FRAMES        = 4;
        break;
    }

    TOTAL_PIXELS       = (FRONT_PORCH+HSYNC_PIXELS+BACK_PORCH+ACTIVE_PIXELS); // Number of total pixels per line.
    TOTAL_LINES        = (LINES_F0+(LINES_F1*INTERLACE_MODE));                // Number of total lines per frame.

    total_pixels_venc = (TOTAL_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 858 / 1 * 2 = 1716
    active_pixels_venc= (ACTIVE_PIXELS / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 720 / 1 * 2 = 1440
    front_porch_venc  = (FRONT_PORCH   / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 16   / 1 * 2 = 32
    hsync_pixels_venc = (HSYNC_PIXELS  / (1+PIXEL_REPEAT_HDMI)) * (1+PIXEL_REPEAT_VENC); // 62   / 1 * 2 = 124

    aml_write_reg32(P_ENCP_VIDEO_MODE,aml_read_reg32(P_ENCP_VIDEO_MODE)|(1<<14)); // cfg_de_v = 1
    // Program DE timing
    de_h_begin = modulo(aml_read_reg32(P_ENCP_VIDEO_HAVON_BEGIN) + VFIFO2VD_TO_HDMI_LATENCY,  total_pixels_venc); // (217 + 3) % 1716 = 220
    de_h_end   = modulo(de_h_begin + active_pixels_venc,                        total_pixels_venc); // (220 + 1440) % 1716 = 1660
    aml_write_reg32(P_ENCP_DE_H_BEGIN, de_h_begin);    // 220
    aml_write_reg32(P_ENCP_DE_H_END,   de_h_end);      // 1660
    // Program DE timing for even field
    de_v_begin_even = aml_read_reg32(P_ENCP_VIDEO_VAVON_BLINE);       // 42
    de_v_end_even   = de_v_begin_even + ACTIVE_LINES;   // 42 + 480 = 522
    aml_write_reg32(P_ENCP_DE_V_BEGIN_EVEN,de_v_begin_even);   // 42
    aml_write_reg32(P_ENCP_DE_V_END_EVEN,  de_v_end_even);     // 522
    // Program DE timing for odd field if needed
    if (INTERLACE_MODE) {
        // Calculate de_v_begin_odd according to enc480p_timing.v:
        //wire[10:0]    cfg_ofld_vavon_bline    = {{7{ofld_vavon_ofst1 [3]}},ofld_vavon_ofst1 [3:0]} + cfg_video_vavon_bline    + ofld_line;
        de_v_begin_odd  = to_signed((aml_read_reg32(P_ENCP_VIDEO_OFLD_VOAV_OFST) & 0xf0)>>4) + de_v_begin_even + (TOTAL_LINES-1)/2;
        de_v_end_odd    = de_v_begin_odd + ACTIVE_LINES;
        aml_write_reg32(P_ENCP_DE_V_BEGIN_ODD, de_v_begin_odd);
        aml_write_reg32(P_ENCP_DE_V_END_ODD,   de_v_end_odd);
    }

    // Program Hsync timing
    if (de_h_end + front_porch_venc >= total_pixels_venc) {
        hs_begin    = de_h_end + front_porch_venc - total_pixels_venc;
        vs_adjust   = 1;
    } else {
        hs_begin    = de_h_end + front_porch_venc; // 1660 + 32 = 1692
        vs_adjust   = 0;
    }
    hs_end  = modulo(hs_begin + hsync_pixels_venc,   total_pixels_venc); // (1692 + 124) % 1716 = 100
    aml_write_reg32(P_ENCP_DVI_HSO_BEGIN,  hs_begin);  // 1692
    aml_write_reg32(P_ENCP_DVI_HSO_END,    hs_end);    // 100
    
    // Program Vsync timing for even field
    if (de_v_begin_even >= SOF_LINES + VSYNC_LINES + (1-vs_adjust)) {
        vs_bline_evn = de_v_begin_even - SOF_LINES - VSYNC_LINES - (1-vs_adjust); // 42 - 30 - 6 - 1 = 5
    } else {
        vs_bline_evn = TOTAL_LINES + de_v_begin_even - SOF_LINES - VSYNC_LINES - (1-vs_adjust);
    }
    vs_eline_evn = modulo(vs_bline_evn + VSYNC_LINES, TOTAL_LINES); // (5 + 6) % 525 = 11
    aml_write_reg32(P_ENCP_DVI_VSO_BLINE_EVN, vs_bline_evn);   // 5
    aml_write_reg32(P_ENCP_DVI_VSO_ELINE_EVN, vs_eline_evn);   // 11
    vso_begin_evn = hs_begin; // 1692
    aml_write_reg32(P_ENCP_DVI_VSO_BEGIN_EVN, vso_begin_evn);  // 1692
    aml_write_reg32(P_ENCP_DVI_VSO_END_EVN,   vso_begin_evn);  // 1692
    // Program Vsync timing for odd field if needed
    if (INTERLACE_MODE) {
        vs_bline_odd = de_v_begin_odd-1 - SOF_LINES - VSYNC_LINES;
        vs_eline_odd = de_v_begin_odd-1 - SOF_LINES;
        vso_begin_odd   = modulo(hs_begin + (total_pixels_venc>>1), total_pixels_venc);
        aml_write_reg32(P_ENCP_DVI_VSO_BLINE_ODD, vs_bline_odd);
        aml_write_reg32(P_ENCP_DVI_VSO_ELINE_ODD, vs_eline_odd);
        aml_write_reg32(P_ENCP_DVI_VSO_BEGIN_ODD, vso_begin_odd);
        aml_write_reg32(P_ENCP_DVI_VSO_END_ODD,   vso_begin_odd);
    }
    // Annie 01Sep2011: Remove the following line as register VENC_DVI_SETTING_MORE is no long valid, use VPU_HDMI_SETTING instead.
    //Wr(VENC_DVI_SETTING_MORE, (TX_INPUT_COLOR_FORMAT==0)? 1 : 0); // [0] 0=Map data pins from Venc to Hdmi Tx as CrYCb mode;

    switch(param->VIC)
    {
        case HDMI_480p60:
        case HDMI_480p60_16x9:
        case HDMI_480p60_16x9_rpt:
        case HDMI_576p50:
        case HDMI_576p50_16x9:
        case HDMI_576p50_16x9_rpt:
//Note: Hsync & Vsync polarity should be negative.
//Refer to HDMI CTS 1.4A Page 169
            // Annie 01Sep2011: Register VENC_DVI_SETTING and VENC_DVI_SETTING_MORE are no long valid, use VPU_HDMI_SETTING instead.
            aml_write_reg32(P_VPU_HDMI_SETTING, (0                                 << 0) | // [    0] src_sel_enci
                                 (0                                 << 1) | // [    1] src_sel_encp
                                 (0                                 << 2) | // [    2] inv_hsync. 1=Invert Hsync polarity.
                                 (0                                 << 3) | // [    3] inv_vsync. 1=Invert Vsync polarity.
                                 (0                                 << 4) | // [    4] inv_dvi_clk. 1=Invert clock to external DVI, (clock invertion exists at internal HDMI).
                                 (((TX_INPUT_COLOR_FORMAT==0)?1:0)  << 5) | // [ 7: 5] data_comp_map. Input data is CrYCb(BRG), map the output data to desired format:
                                                                            //                          0=output CrYCb(BRG);
                                                                            //                          1=output YCbCr(RGB);
                                                                            //                          2=output YCrCb(RBG);
                                                                            //                          3=output CbCrY(GBR);
                                                                            //                          4=output CbYCr(GRB);
                                                                            //                          5=output CrCbY(BGR);
                                                                            //                          6,7=Rsrv.
                                 (1                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
                                 (0                                 <<12)   // [15:12] rd_rate. 0=A read every clk2; 1=A read every 2 clk2; ...; 15=A read every 16 clk2.
            );
            break;
        case HDMI_720p60:
        case HDMI_720p50:
            // Annie 01Sep2011: Register VENC_DVI_SETTING and VENC_DVI_SETTING_MORE are no long valid, use VPU_HDMI_SETTING instead.
            aml_write_reg32(P_VPU_HDMI_SETTING, (0                                 << 0) | // [    0] src_sel_enci
                                 (0                                 << 1) | // [    1] src_sel_encp
                                 (HSYNC_POLARITY                    << 2) | // [    2] inv_hsync. 1=Invert Hsync polarity.
                                 (VSYNC_POLARITY                    << 3) | // [    3] inv_vsync. 1=Invert Vsync polarity.
                                 (0                                 << 4) | // [    4] inv_dvi_clk. 1=Invert clock to external DVI, (clock invertion exists at internal HDMI).
                                 (((TX_INPUT_COLOR_FORMAT==0)?1:0)  << 5) | // [ 7: 5] data_comp_map. Input data is CrYCb(BRG), map the output data to desired format:
                                                                            //                          0=output CrYCb(BRG);
                                                                            //                          1=output YCbCr(RGB);
                                                                            //                          2=output YCrCb(RBG);
                                                                            //                          3=output CbCrY(GBR);
                                                                            //                          4=output CbYCr(GRB);
                                                                            //                          5=output CrCbY(BGR);
                                                                            //                          6,7=Rsrv.
#ifdef DOUBLE_CLK_720P_1080I
                                 (0                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
#else
                                 (1                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
#endif                             
                                 (0                                 <<12)   // [15:12] rd_rate. 0=A read every clk2; 1=A read every 2 clk2; ...; 15=A read every 16 clk2.
            );
            break;
        default:
            // Annie 01Sep2011: Register VENC_DVI_SETTING and VENC_DVI_SETTING_MORE are no long valid, use VPU_HDMI_SETTING instead.
            aml_write_reg32(P_VPU_HDMI_SETTING, (0                                 << 0) | // [    0] src_sel_enci
                                 (0                                 << 1) | // [    1] src_sel_encp
                                 (HSYNC_POLARITY                    << 2) | // [    2] inv_hsync. 1=Invert Hsync polarity.
                                 (VSYNC_POLARITY                    << 3) | // [    3] inv_vsync. 1=Invert Vsync polarity.
                                 (0                                 << 4) | // [    4] inv_dvi_clk. 1=Invert clock to external DVI, (clock invertion exists at internal HDMI).
                                 (((TX_INPUT_COLOR_FORMAT==0)?1:0)  << 5) | // [ 7: 5] data_comp_map. Input data is CrYCb(BRG), map the output data to desired format:
                                                                            //                          0=output CrYCb(BRG);
                                                                            //                          1=output YCbCr(RGB);
                                                                            //                          2=output YCrCb(RBG);
                                                                            //                          3=output CbCrY(GBR);
                                                                            //                          4=output CbYCr(GRB);
                                                                            //                          5=output CrCbY(BGR);
                                                                            //                          6,7=Rsrv.
                                 (0                                 << 8) | // [11: 8] wr_rate. 0=A write every clk1; 1=A write every 2 clk1; ...; 15=A write every 16 clk1.
                                 (0                                 <<12)   // [15:12] rd_rate. 0=A read every clk2; 1=A read every 2 clk2; ...; 15=A read every 16 clk2.
            );
    }

    if((param->VIC == HDMI_480p60_16x9_rpt) || (param->VIC == HDMI_576p50_16x9_rpt)) {
        aml_set_reg32_bits(P_VPU_HDMI_SETTING, 3, 12, 4);
    }
    // Annie 01Sep2011: Register VENC_DVI_SETTING and VENC_DVI_SETTING_MORE are no long valid, use VPU_HDMI_SETTING instead.
    aml_set_reg32_bits(P_VPU_HDMI_SETTING, 1, 1, 1);  // [    1] src_sel_encp: Enable ENCP output to HDMI
}

/*
hdmi on/off
*/
static int is_hpd_muxed(void)
{
    int ret;
    ret = !!(aml_read_reg32(P_PERIPHS_PIN_MUX_1)&(1<<26));
    return ret; 
}    

static void mux_hpd(void)
{
    aml_write_reg32(P_PERIPHS_PIN_MUX_1, aml_read_reg32(P_PERIPHS_PIN_MUX_1)|(1 << 26));
}

static void unmux_hpd(void)
{
    aml_write_reg32(P_PERIPHS_PIN_MUX_1, aml_read_reg32(P_PERIPHS_PIN_MUX_1)&~(1 << 26));
    aml_write_reg32(P_PREG_PAD_GPIO3_EN_N, aml_read_reg32(P_PREG_PAD_GPIO3_EN_N) | (1 << 19)); //GPIOH_0 as input
}

extern int read_hpd_gpio(void);
int read_hpd_gpio(void)
{
    int level;

    level = !!(aml_read_reg32(P_PREG_PAD_GPIO3_I)&(1<<19)); //read GPIOH_0
    return level;
}
EXPORT_SYMBOL(read_hpd_gpio);

#if 0
static unsigned long clk81_rate = 100000000;

static void clk81_set(void)
{
    struct clk *clk_tmp;

    clk_tmp = clk_get_sys("clk81", NULL);
    if (clk_tmp)
    {
        clk81_rate = clk_get_rate(clk_tmp);
        clk_set_rate(clk_tmp, 168000000);
        CLEAR_AOBUS_REG_MASK(AO_UART_CONTROL, (1 << 19) | 0xFFF);
        WRITE_AOBUS_REG_BITS(AO_UART_CONTROL, ((168000000 / (115200 * 4)) - 1) & 0xfff, 0, 12);
    }
    msleep(2);  //Waiting some time
    //printk("%s clk81_rate: %d\n", __FUNCTION__, clk81_rate);
}

static void clk81_resume(void)
{
    struct clk *clk_tmp;
    
    clk_tmp = clk_get_sys("clk81", NULL);
    if (clk_tmp)
    {
        clk_set_rate(clk_tmp, clk81_rate);
        CLEAR_AOBUS_REG_MASK(AO_UART_CONTROL, (1 << 19) | 0xFFF);
        WRITE_AOBUS_REG_BITS(AO_UART_CONTROL, ((clk81_rate / (115200 * 4)) - 1) & 0xfff, 0, 12);
    }
    msleep(2);  //Waiting some time
    //printk("%s clk81_rate: %d\n", __FUNCTION__, clk81_rate);
}
#endif

static void digital_clk_off(unsigned char flag)
{
//    clk81_resume();
    if(flag&1){
//#ifdef AML_A3
    /* off hdmi audio clock */
//        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_CTRL1,  hdmi_rd_reg(OTHER_BASE_ADDR + HDMI_OTHER_CTRL1)&(~(1<<13))); //
//        hdmi_wr_reg(TX_AUDIO_FORMAT, hdmi_rd_reg(TX_AUDIO_FORMAT)|(1<<7));
//        hdmi_wr_reg(TX_AUDIO_I2S, hdmi_rd_reg(TX_AUDIO_I2S)|(1<<1));
//#endif    
    }

    if(flag&2){
        /* off hdmi pixel clock */
//        Wr(HHI_GCLK_MPEG2, Rd(HHI_GCLK_MPEG2)&(~(1<<4))); //disable pixel clock, set cbus reg HHI_GCLK_MPEG2 bit [4] = 0
        aml_write_reg32(P_HHI_GCLK_OTHER, aml_read_reg32(P_HHI_GCLK_OTHER)&(~(1<<17))); //disable VCLK1_HDMI GATE, set cbus reg HHI_GCLK_OTHER bit [17] = 0
        aml_write_reg32(P_VENC_DVI_SETTING, (aml_read_reg32(P_VENC_DVI_SETTING)&(~(7<<4)))|(5<<4)); //set cbus reg VENC_DVI_SETTING bit[6:4] = 0x5
    // Second turn off gate.
        aml_write_reg32(P_HHI_GCLK_MPEG2, aml_read_reg32(P_HHI_GCLK_MPEG2) & (~(1<<4)));     //Disable HDMI PCLK
    }
    if(flag&4){
        /* off hdmi sys clock */
        aml_write_reg32(P_HHI_HDMI_CLK_CNTL, aml_read_reg32(P_HHI_HDMI_CLK_CNTL)&(~(1<<8))); // off hdmi sys clock gate
    }
}

static void digital_clk_on(unsigned char flag)
{
//    clk81_set();
    if(flag&4){
        /* on hdmi sys clock */
        // -----------------------------------------
        // HDMI (90Mhz)
        // -----------------------------------------
        //         .clk_div            ( hi_hdmi_clk_cntl[6:0] ),
        //         .clk_en             ( hi_hdmi_clk_cntl[8]   ),
        //         .clk_sel            ( hi_hdmi_clk_cntl[11:9]),
        aml_set_reg32_bits(P_HHI_HDMI_CLK_CNTL, 0, 0, 7);    // Divide the "other" PLL output by 1
        aml_set_reg32_bits(P_HHI_HDMI_CLK_CNTL, 0, 9, 3);    // select "XTAL" PLL
        aml_set_reg32_bits(P_HHI_HDMI_CLK_CNTL, 1, 8, 1);    // Enable gated clock
//        Wr( HHI_HDMI_CLK_CNTL,  ((2 << 9)  |   // select "misc" PLL
//                                 (1 << 8)  |   // Enable gated clock
//                                 (5 << 0)) );  // Divide the "other" PLL output by 6
    }
    if(flag&2){
        /* on hdmi pixel clock */
        aml_write_reg32(P_HHI_GCLK_MPEG2, aml_read_reg32(P_HHI_GCLK_MPEG2) | (1<<4));     //Enable HDMI PCLK
//        Wr(HHI_GCLK_MPEG2, Rd(HHI_GCLK_MPEG2)|(1<<4)); //enable pixel clock, set cbus reg HHI_GCLK_MPEG2 bit [4] = 1
        aml_write_reg32(P_HHI_GCLK_OTHER, aml_read_reg32(P_HHI_GCLK_OTHER)|(1<<17)); //enable VCLK1_HDMI GATE, set cbus reg HHI_GCLK_OTHER bit [17] = 1
    }
    if(flag&1){
    }  
}

static void phy_pll_off(void)
{
    hdmi_phy_suspend();
}

/**/
void hdmi_hw_set_powermode(hdmitx_dev_t* hdmitx_device)
{
    int vic = hdmitx_device->cur_VIC;

    switch(vic) {
    case HDMI_480i60:
    case HDMI_480i60_16x9:
    case HDMI_576p50:
    case HDMI_576p50_16x9:
    case HDMI_576i50:
    case HDMI_576i50_16x9:
    case HDMI_480p60:
    case HDMI_480p60_16x9:
    case HDMI_720p50:
    case HDMI_720p60:
    case HDMI_1080i50:
    case HDMI_1080i60:
    case HDMI_1080p24://1080p24 support
    case HDMI_1080p50:
    case HDMI_1080p60:
    default:
        //aml_write_reg32(P_HHI_HDMI_PHY_CNTL0, 0x08c38d0b);
        break;
    }
    //aml_write_reg32(P_HHI_HDMI_PHY_CNTL1, 2);
}

void hdmi_hw_init(hdmitx_dev_t* hdmitx_device)
{
    unsigned int tmp_add_data;
    HDMI_Video_Codes_t vic;
    
    digital_clk_on(7);

    aml_set_reg32_bits(P_HHI_MEM_PD_REG0, 0x00, 8, 8);    //disable HDMI memory PD  TODO: set in suspend/resume

    aml_write_reg32(P_HHI_HDMI_AFC_CNTL, aml_read_reg32(P_HHI_HDMI_AFC_CNTL) | 0x3);

    hdmi_wr_reg(TX_HDCP_MODE, 0x40);

    vic = hdmitx_device->HWOp.GetState(hdmitx_device, STAT_VIDEO_VIC, 0);
    if(vic != HDMI_Unkown) {
        hdmi_print(IMP, SYS "ALREADY init VIC = %d\n", vic);
        hdmitx_device->cur_VIC = vic;
        return;
    }
    hdmi_phy_suspend();
    //todo
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 0xf, 23, 4); //Enable reg1[23:24]:HDMI SDA(5v)/SCL(5V)
#ifdef CONFIG_PANEL_IT6681
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 0, 23, 3); //disable reg1[23:25]:HDMI CEC/SCL(5v)/SDA(5V)
#endif
    hdmi_print(IMP, SYS "hw init\n");

    hdmi_wr_reg(0x017, 0x1d);   //1d for power-up Band-gap and main-bias ,00 is power down 
    if(serial_reg_val<0x20){
        hdmi_wr_reg(0x018, 0x24);
    }
    else{
        hdmi_wr_reg(0x018, serial_reg_val);   //Serializer Internal clock setting ,please fix to vaue 24 ,other setting is only for debug  
    }
    hdmi_wr_reg(0x01a, 0xfb);   //bit[2:0]=011 ,CK channel output TMDS CLOCK ,bit[2:0]=101 ,ck channel output PHYCLCK 

    hdmi_hw_set_powermode(hdmitx_device);

    hdmi_wr_reg(0x0F7, 0x0F);   // Termination resistor calib value

    // --------------------------------------------------------
    // Program core_pin_mux to enable HDMI pins
    // --------------------------------------------------------
    //wire            pm_hdmi_cec_en              = pin_mux_reg0[2];
    //wire            pm_hdmi_hpd_5v_en           = pin_mux_reg0[1];
    //wire            pm_hdmi_i2c_5v_en           = pin_mux_reg0[0];

    // Enable these interrupts: [2] tx_edit_int_rise [1] tx_hpd_int_fall [0] tx_hpd_int_rise
    hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_MASKN, 0x0);
    // HPD glitch filter
    hdmi_wr_reg(TX_HDCP_HPD_FILTER_L, 0xa0);
    hdmi_wr_reg(TX_HDCP_HPD_FILTER_H, 0xa0);

    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x90); //bit5,6 is converted
    delay_us(10);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x60);
    delay_us(10);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0xff);
    delay_us(10);

    /**/

    // Enable software controlled DDC transaction
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7]   = 1'b0 ;  // forced_sys_trigger
    //tmp_add_data[6]   = 1'b0 ;  // sys_trigger_config
    //tmp_add_data[5]   = 1'b0 ;  // mem_acc_seq_mode
    //tmp_add_data[4]   = 1'b0 ;  // mem_acc_seq_start
    //tmp_add_data[3]   = 1'b1 ;  // forced_mem_copy_done
    //tmp_add_data[2]   = 1'b1 ;  // mem_copy_done_config
    //tmp_add_data[1]   = 1'b1 ;  // sys_trigger_config_semi_manu
    //tmp_add_data[0]   = 1'b0 ;  // Rsrv
    hdmi_wr_reg(TX_HDCP_EDID_CONFIG, 0x0c); //// for hdcp, can not use 0x0e
    hdmi_wr_reg(TX_CORE_EDID_CONFIG_MORE, (1 << 0));    // [1]: keep_edid_error
                                                        // [0]: sys_trigger_config_semi_manu
                                                        
    hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_1, 0);
    hdmi_wr_reg(TX_PACKET_CONTROL_2, 2);
    
    hdmi_wr_reg(TX_HDCP_CONFIG0,      1<<3);  //set TX rom_encrypt_off=1
    hdmi_wr_reg(TX_HDCP_MEM_CONFIG,   0<<3);  //set TX read_decrypt=0
    hdmi_wr_reg(TX_HDCP_ENCRYPT_BYTE, 0);     //set TX encrypt_byte=0x00

    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7] = 1'b0;       // Force packet timing
    //tmp_add_data[6] = 1'b0;       // PACKET ALLOC MODE
    //tmp_add_data[5:0] = 6'd47 ;   // PACKET_START_LATENCY
    //tmp_add_data = 47;
    tmp_add_data = 58;
    hdmi_wr_reg(TX_PACKET_CONTROL_1, tmp_add_data); //this register should be set to ensure the first hdcp succeed

    //tmp_add_data[7] = 1'b0;      // cp_desired
    //tmp_add_data[6] = 1'b0;      // ess_config
    //tmp_add_data[5] = 1'b0;      // set_avmute
    //tmp_add_data[4] = 1'b1;      // clear_avmute
    //tmp_add_data[3] = 1'b0;      // hdcp_1_1
    //tmp_add_data[2] = 1'b0;      // Vsync/Hsync forced_polarity_select
    //tmp_add_data[1] = 1'b0;      // forced_vsync_polarity
    //tmp_add_data[0] = 1'b0;      // forced_hsync_polarity
    //tmp_add_data = 0x10;
    tmp_add_data = 0x0; //rain
    hdmi_wr_reg(TX_HDCP_MODE, tmp_add_data);
    //config_hdmi(1);

    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7:0]   = 0xa ; // time_divider[7:0] for DDC I2C bus clock
    //tmp_add_data = 0xa; //800k
    //tmp_add_data = 0x3f; //190k
    tmp_add_data = 0x18 - 1; //50k     // hdmi system clock change to XTAL 24MHz
    hdmi_wr_reg(TX_HDCP_CONFIG3, tmp_add_data);

    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7]   = 8'b1 ;  //cp_desired 
    //tmp_add_data[6]   = 8'b1 ;  //ess_config 
    //tmp_add_data[5]   = 8'b0 ;  //set_avmute 
    //tmp_add_data[4]   = 8'b0 ;  //clear_avmute 
    //tmp_add_data[3]   = 8'b1 ;  //hdcp_1_1 
    //tmp_add_data[2]   = 8'b0 ;  //forced_polarity 
    //tmp_add_data[1]   = 8'b0 ;  //forced_vsync_polarity 
    //tmp_add_data[0]   = 8'b0 ;  //forced_hsync_polarity
    tmp_add_data = 0x40;
    hdmi_wr_reg(TX_HDCP_MODE, tmp_add_data);
    
    hdmi_wr_reg(TX_AUDIO_CONTROL_MORE, 1);

    hdmi_hw_set_powermode(hdmitx_device);

    // --------------------------------------------------------
    // Release TX out of reset
    // --------------------------------------------------------
    //new reset sequence, 2010Sep09, rain
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 1<<6); // Release resets all other TX digital clock domain, except tmds_clk
    delay_us(10);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x00); // Final release reset on tmds_clk domain
    delay_us(10);        

    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x68);        
    delay_us(10);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x60);        
    delay_us(10);
}    

// When have below format output, we shall manually configure
// bolow register to get stable Video Timing.
static void hdmi_reconfig_packet_setting(HDMI_Video_Codes_t vic)
{
    switch(vic) {
    case HDMI_1080p50:
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0x3a);         //0x7e
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_1, 0x01);    //0x78
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_2, 0x12);    //0x79
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_1, 0x10);       //0x7a
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_2, 0x12);       //0x7b
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_0, 0x01);       //0x81
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_1, 0x00);       //0x82
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_2, 0x0a);       //0x83
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_1, 0xb6);       //0x7c
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_2, 0x11);       //0x7d
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0xba);         //0x7e
        break;
    case HDMI_4k2k_30:
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0x3a);         //0x7e
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_1, 0x01);    //0x78
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_2, 0x0f);    //0x79
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_1, 0x3a);       //0x7a
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_2, 0x12);       //0x7b
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_0, 0x01);       //0x81
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_1, 0x00);       //0x82
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_2, 0x0a);       //0x83
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_1, 0x60);       //0x7c
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_2, 0x52);       //0x7d
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0xba);         //0x7e
        break;
    case HDMI_4k2k_25:
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0x3a);         //0x7e
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_1, 0x01);    //0x78
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_2, 0x12);    //0x79
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_1, 0x44);       //0x7a
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_2, 0x12);       //0x7b
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_0, 0x01);       //0x81
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_1, 0x00);       //0x82
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_2, 0x0a);       //0x83
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_1, 0xda);       //0x7c
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_2, 0x52);       //0x7d
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0xba);         //0x7e
        break;
    case HDMI_4k2k_24:
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0x3a);         //0x7e
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_1, 0x01);    //0x78
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_2, 0x12);    //0x79
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_1, 0x47);       //0x7a
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_2, 0x12);       //0x7b
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_0, 0x01);       //0x81
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_1, 0x00);       //0x82
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_2, 0x0a);       //0x83
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_1, 0xf8);       //0x7c
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_2, 0x52);       //0x7d
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0xba);         //0x7e
        break;
    case HDMI_4k2k_smpte_24:
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0x3a);         //0x7e
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_1, 0x01);    //0x78
        hdmi_wr_reg(TX_PACKET_ALLOC_ACTIVE_2, 0x12);    //0x79
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_1, 0x47);       //0x7a
        hdmi_wr_reg(TX_PACKET_ALLOC_EOF_2, 0x12);       //0x7b
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_0, 0x01);       //0x81
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_1, 0x00);       //0x82
        hdmi_wr_reg(TX_CORE_ALLOC_VSYNC_2, 0x0a);       //0x83
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_1, 0xf8);       //0x7c
        hdmi_wr_reg(TX_PACKET_ALLOC_SOF_2, 0x52);       //0x7d
        hdmi_wr_reg(TX_PACKET_CONTROL_1, 0xba);         //0x7e
        break;
    default:
        break;
    }
    hdmi_print(IMP, SYS "reconfig packet setting done\n");
}

static void hdmi_hw_reset(hdmitx_dev_t* hdmitx_device, Hdmi_tx_video_para_t *param)
{
    unsigned int tmp_add_data;
    unsigned long TX_OUTPUT_COLOR_FORMAT;

    hdmi_print(IMP, SYS "hw reset\n");
    
    digital_clk_on(7);

    if(param->color==COLOR_SPACE_YUV444){
        TX_OUTPUT_COLOR_FORMAT=1;
    }
    else if(param->color==COLOR_SPACE_YUV422){
        TX_OUTPUT_COLOR_FORMAT=3;
    }
    else{
        TX_OUTPUT_COLOR_FORMAT=0;
    }

    if(delay_flag&2)
        delay_us(1000*100);
    //printk("delay 100ms\n");

    aml_write_reg32(P_HHI_HDMI_AFC_CNTL, aml_read_reg32(P_HHI_HDMI_AFC_CNTL) | 0x3);

    hdmi_wr_reg(0x017, 0x1d);   //1d for power-up Band-gap and main-bias ,00 is power down 
    if(new_reset_sequence_flag==0){
        if(serial_reg_val==0){
            if((param->VIC==HDMI_1080p30)||(param->VIC==HDMI_720p60)||(param->VIC==HDMI_1080i60)
                ||(param->VIC==HDMI_1080p24)){
                hdmi_wr_reg(0x018, 0x22);   
            }
            else{
                hdmi_wr_reg(0x018, 0x24);   
            }
        }
        else if(serial_reg_val==1){
            if((param->VIC==HDMI_480p60)||(param->VIC==HDMI_480p60_16x9)
                ||(param->VIC==HDMI_576p50)||(param->VIC==HDMI_576p50_16x9)
                ||(param->VIC==HDMI_480i60)||(param->VIC==HDMI_480i60_16x9)
                ||(param->VIC==HDMI_576i50)||(param->VIC==HDMI_576i50_16x9)){
                hdmi_wr_reg(0x018, 0x24);   
            }
            else{
                hdmi_wr_reg(0x018, 0x22);   
            }
        }
        else{
            hdmi_wr_reg(0x018, serial_reg_val);
        }
        if((param->VIC==HDMI_1080p60)&&(param->color_depth==COLOR_30BIT)&&(hdmi_rd_reg(0x018)==0x22)){
            hdmi_wr_reg(0x018,0x12);
        }
    }
    hdmi_wr_reg(0x01a, 0xfb);   //bit[2:0]=011 ,CK channel output TMDS CLOCK ,bit[2:0]=101 ,ck channel output PHYCLCK 

    hdmi_hw_set_powermode(hdmitx_device);

    hdmi_wr_reg(0x0F7, 0x0F);   // Termination resistor calib value

    // delay 1000uS, then check HPLL_LOCK
    delay_us(1000);
    //while ( (Rd(HHI_VID_PLL_CNTL3) & (1<<31)) != (1<<31) );
 
//////////////////////////////reset    
    if(new_reset_sequence_flag){

        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x90);
        delay_us(10);
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x60);
        delay_us(10);
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0xff);
        delay_us(10);
    }
    else{
        // Keep TX (except register I/F) in reset, while programming the registers:
        tmp_add_data  = 0;
        tmp_add_data |= 1 << 7; // tx_pixel_rstn
        tmp_add_data |= 1 << 6; // tx_tmds_rstn
        tmp_add_data |= 1 << 5; // tx_audio_master_rstn
        tmp_add_data |= 1 << 4; // tx_audio_sample_rstn
        tmp_add_data |= 1 << 3; // tx_i2s_reset_rstn
        tmp_add_data |= 1 << 2; // tx_dig_reset_n_ch2
        tmp_add_data |= 1 << 1; // tx_dig_reset_n_ch1
        tmp_add_data |= 1 << 0; // tx_dig_reset_n_ch0
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, tmp_add_data);
    
        tmp_add_data  = 0;
        tmp_add_data |= 1 << 7; // HDMI_CH3_RST_IN
        tmp_add_data |= 1 << 6; // HDMI_CH2_RST_IN
        tmp_add_data |= 1 << 5; // HDMI_CH1_RST_IN
        tmp_add_data |= 1 << 4; // HDMI_CH0_RST_IN
        tmp_add_data |= 1 << 3; // HDMI_SR_RST
        tmp_add_data |= 1 << 0; // tx_dig_reset_n_ch3
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, tmp_add_data);
    }
    // Enable software controlled DDC transaction
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7]   = 1'b0 ;  // forced_sys_trigger
    //tmp_add_data[6]   = 1'b0 ;  // sys_trigger_config
    //tmp_add_data[5]   = 1'b0 ;  // mem_acc_seq_mode
    //tmp_add_data[4]   = 1'b0 ;  // mem_acc_seq_start
    //tmp_add_data[3]   = 1'b1 ;  // forced_mem_copy_done
    //tmp_add_data[2]   = 1'b1 ;  // mem_copy_done_config
    //tmp_add_data[1]   = 1'b1 ;  // sys_trigger_config_semi_manu
    //tmp_add_data[0]   = 1'b0 ;  // Rsrv

    tmp_add_data = 58;
    hdmi_wr_reg(TX_PACKET_CONTROL_1, tmp_add_data);

    tmp_add_data = 0x0c; // for hdcp, can not use 0x0e 
    hdmi_wr_reg(TX_HDCP_EDID_CONFIG, tmp_add_data);
    
    hdmi_wr_reg(TX_HDCP_CONFIG0,      0x3<<3);  //set TX rom_encrypt_off=1
    hdmi_wr_reg(TX_HDCP_MEM_CONFIG,   0<<3);  //set TX read_decrypt=0
    hdmi_wr_reg(TX_HDCP_ENCRYPT_BYTE, 0);     //set TX encrypt_byte=0x00

    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7] = 1'b0;      // Force DTV timing (Auto)
    //tmp_add_data[6] = 1'b0;      // Force Video Scan, only if [7]is set
    //tmp_add_data[5] = 1'b0 ;     // Force Video field, only if [7]is set
    //tmp_add_data[4:0] = 5'b00 ;  // Rsrv
    if(hdmitx_device->cur_VIC == 39)
        tmp_add_data = 0;
    else
        tmp_add_data = (1<<4);
    hdmi_wr_reg(TX_VIDEO_DTV_TIMING, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= 0                       << 7; // [7]   forced_default_phase
    tmp_add_data |= 0                       << 2; // [6:2] Rsrv
    tmp_add_data |= param->color_depth      << 0; // [1:0] Color_depth:0=24-bit pixel; 1=30-bit pixel; 2=36-bit pixel; 3=48-bit pixel
    hdmi_wr_reg(TX_VIDEO_DTV_MODE, tmp_add_data); // 0x00
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7] = 1'b0;       // Force packet timing
    //tmp_add_data[6] = 1'b0;       // PACKET ALLOC MODE
    //tmp_add_data[5:0] = 6'd47 ;   // PACKET_START_LATENCY
    //tmp_add_data = 47;
    tmp_add_data = 58;
    hdmi_wr_reg(TX_PACKET_CONTROL_1, tmp_add_data);

    // For debug: disable packets of audio_request, acr_request, deep_color_request, and avmute_request
    //hdmi_wr_reg(TX_PACKET_CONTROL_2, hdmi_rd_reg(TX_PACKET_CONTROL_2) | 0x0f);
    
    //HDMI CT 7-19 GCP PB1 through PB6 not equal to 0 | 720 3 0 37 72 16367911819.90 31822 General Control Packet (GCP) 
    //PACKET_CONTROL[~deep_color_request_enable]
    //0: horizontal GC packet transport enabled
    //1: horizontal GC packet masked
    hdmi_wr_reg(TX_PACKET_CONTROL_2, hdmi_rd_reg(TX_PACKET_CONTROL_2) | (0x1<<1));
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7:6] = 2'b0;     // audio_source_select[1:0]
    //tmp_add_data[5] = 1'b0;       // external_packet_enable
    //tmp_add_data[4] = 1'b1 ;      // internal_packet_enable
    //tmp_add_data[3:2] = 2'b0;     // afe_fifo_source_select_lane_1[1:0]
    //tmp_add_data[1:0] = 2'b0 ;    // afe_fifo_source_select_lane_0[1:0]
    tmp_add_data = 0x10;
    hdmi_wr_reg(TX_CORE_DATA_CAPTURE_2, tmp_add_data);
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7]   = 1'b0;     // monitor_lane_1
    //tmp_add_data[6:4] = 3'd0;     // monitor_select_lane_1[2:0]
    //tmp_add_data[3]   = 1'b1 ;    // monitor_lane_0
    //tmp_add_data[2:0] = 3'd7;     // monitor_select_lane_0[2:0]
    tmp_add_data = 0xf;
    hdmi_wr_reg(TX_CORE_DATA_MONITOR_1, tmp_add_data);
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7:3] = 5'b0;     // Rsrv
    //tmp_add_data[2:0] = 3'd2;     // monitor_select[2:0]
    tmp_add_data = 0x2;
    hdmi_wr_reg(TX_CORE_DATA_MONITOR_2, tmp_add_data);
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7] = 1'b1;     // forced_hdmi
    //tmp_add_data[6] = 1'b1;     // hdmi_config
    //tmp_add_data[5:4] = 2'b0;   // Rsrv
    //tmp_add_data[3] = 1'b0;     // bit_swap.
    //tmp_add_data[2:0] = 3'd0;   // channel_swap[2:0]
    tmp_add_data = 0xc0;
    hdmi_wr_reg(TX_TMDS_MODE, tmp_add_data);
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7] = 1'b0;  // Rsrv
    //tmp_add_data[6] = 1'b0;  // TX_CONNECT_SEL: 0=use lower channel data[29:0]; 1=use upper channel data[59:30]
    //tmp_add_data[5:0] = 'h0;  // Rsrv
    tmp_add_data = 0x0;
    hdmi_wr_reg(TX_SYS4_CONNECT_SEL_1, tmp_add_data);
    
    // Normally it makes sense to synch 3 channel output with clock channel's rising edge,
    // as HDMI's serializer is LSB out first, invert tmds_clk pattern from "1111100000" to
    // "0000011111" actually enable data synch with clock rising edge.
    //if((param->VIC==HDMI_1080p30)||(param->VIC==HDMI_720p60)||(param->VIC==HDMI_1080i60)){
    //    hdmi_wr_reg(TX_SYS4_CK_INV_VIDEO, 0xf0);
    //}
    //else{
        tmp_add_data = 1 << 4; // Set tmds_clk pattern to be "0000011111" before being sent to AFE clock channel
        hdmi_wr_reg(TX_SYS4_CK_INV_VIDEO, tmp_add_data);
    //}            
    
    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7] = 1'b0;  // Rsrv
    //tmp_add_data[6] = 1'b0;  // TX_AFE_FIFO channel 2 bypass=0
    //tmp_add_data[5] = 1'b0;  // TX_AFE_FIFO channel 1 bypass=0
    //tmp_add_data[4] = 1'b0;  // TX_AFE_FIFO channel 0 bypass=0
    //tmp_add_data[3] = 1'b1;  // output enable of clk channel (channel 3)
    //tmp_add_data[2] = 1'b1;  // TX_AFE_FIFO channel 2 enable
    //tmp_add_data[1] = 1'b1;  // TX_AFE_FIFO channel 1 enable
    //tmp_add_data[0] = 1'b1;  // TX_AFE_FIFO channel 0 enable
    tmp_add_data = 0x0f;
    hdmi_wr_reg(TX_SYS5_FIFO_CONFIG, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= TX_OUTPUT_COLOR_FORMAT  << 6; // [7:6] output_color_format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
    tmp_add_data |= TX_INPUT_COLOR_FORMAT   << 4; // [5:4] input_color_format:  0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
    tmp_add_data |= param->color_depth   << 2; // [3:2] output_color_depth:  0=24-b; 1=30-b; 2=36-b; 3=48-b.
    tmp_add_data |= TX_INPUT_COLOR_DEPTH    << 0; // [1:0] input_color_depth:   0=24-b; 1=30-b; 2=36-b; 3=48-b.
    hdmi_wr_reg(TX_VIDEO_DTV_OPTION_L, tmp_add_data); // 0x50

    if(hdmitx_device->cur_audio_param.channel_num > CC_2CH) {
        i2s_to_spdif_flag = 0;
    }else {
        i2s_to_spdif_flag = 1;
    }
    tmp_add_data  = 0;
    tmp_add_data |= 0                       << 4; // [7:4] Rsrv
    tmp_add_data |= TX_OUTPUT_COLOR_RANGE   << 2; // [3:2] output_color_range:  0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
    tmp_add_data |= TX_INPUT_COLOR_RANGE    << 0; // [1:0] input_color_range:   0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
    hdmi_wr_reg(TX_VIDEO_DTV_OPTION_H, tmp_add_data); // 0x00
    
    if(!hdmi_audio_off_flag){
#if 1
        hdmi_audio_init(i2s_to_spdif_flag);
#else
        hdmi_wr_reg(TX_AUDIO_PACK, 0x00); // disable audio sample packets
#endif
    }
    hdmi_wr_reg(TX_AUDIO_CONTROL_MORE, 1);
    //tmp_add_data[7] = 1'b0;      // cp_desired
    //tmp_add_data[6] = 1'b0;      // ess_config
    //tmp_add_data[5] = 1'b0;      // set_avmute
    //tmp_add_data[4] = 1'b1;      // clear_avmute
    //tmp_add_data[3] = 1'b0;      // hdcp_1_1
    //tmp_add_data[2] = 1'b0;      // Vsync/Hsync forced_polarity_select
    //tmp_add_data[1] = 1'b0;      // forced_vsync_polarity
    //tmp_add_data[0] = 1'b0;      // forced_hsync_polarity
    //tmp_add_data = 0x10;
    tmp_add_data = 0x0; //rain
//    hdmi_wr_reg(TX_HDCP_MODE, tmp_add_data);
    //config_hdmi(1);

    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7:0]   = 0xa ; // time_divider[7:0] for DDC I2C bus clock
    
    //tmp_add_data = 0xa; //800k
    //tmp_add_data = 0x3f; //190k
    tmp_add_data = 0x30 - 1; //50k     // hdmi system clock change to XTAL 24MHz
    hdmi_wr_reg(TX_HDCP_CONFIG3, tmp_add_data);

    //tmp_add_data[15:8] = 0;
    //tmp_add_data[7]   = 8'b1 ;  //cp_desired 
    //tmp_add_data[6]   = 8'b1 ;  //ess_config 
    //tmp_add_data[5]   = 8'b0 ;  //set_avmute 
    //tmp_add_data[4]   = 8'b0 ;  //clear_avmute 
    //tmp_add_data[3]   = 8'b1 ;  //hdcp_1_1 
    //tmp_add_data[2]   = 8'b0 ;  //forced_polarity 
    //tmp_add_data[1]   = 8'b0 ;  //forced_vsync_polarity 
    //tmp_add_data[0]   = 8'b0 ;  //forced_hsync_polarity
    tmp_add_data = 0x40;
    hdmi_wr_reg(TX_HDCP_MODE, tmp_add_data);

    if(param->cc == CC_ITU709){
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_B0, 0x7b);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_B1, 0x12);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_R0, 0x6c);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_R1, 0x36);

        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CB0, 0xf2);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CB1, 0x2f);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CR0, 0xd4);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CR1, 0x77);
    }
    else{
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_B0, 0x2f);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_B1, 0x1d);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_R0, 0x8b);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_R1, 0x4c);

        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CB0, 0x18);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CB1, 0x58);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CR0, 0xd0);
        hdmi_wr_reg(TX_VIDEO_CSC_COEFF_CR1, 0xb6);
    }    

    hdmi_hw_set_powermode(hdmitx_device);
    
    // --------------------------------------------------------
    // Release TX out of reset
    // --------------------------------------------------------
    if(new_reset_sequence_flag){
        //new reset sequence, 2010Sep09, rain
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 1<<6); // Release resets all other TX digital clock domain, except tmds_clk
        delay_us(10);
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x00); // Final release reset on tmds_clk domain
        delay_us(10);
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x68);        
        delay_us(10);
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x60);        
        delay_us(10);
        /* select serial*/
        if(serial_reg_val==0){
            if((param->VIC==HDMI_1080p30)||(param->VIC==HDMI_720p60)||(param->VIC==HDMI_1080i60)
                ||(param->VIC==HDMI_1080p24)){
                hdmi_wr_reg(0x018, 0x22);   
            }
            else{
                hdmi_wr_reg(0x018, 0x24);   
            }
        }
        else if(serial_reg_val==1){
            if((param->VIC==HDMI_480p60)||(param->VIC==HDMI_480p60_16x9)
                ||(param->VIC==HDMI_576p50)||(param->VIC==HDMI_576p50_16x9)
                ||(param->VIC==HDMI_480i60)||(param->VIC==HDMI_480i60_16x9)
                ||(param->VIC==HDMI_576i50)||(param->VIC==HDMI_576i50_16x9)){
                hdmi_wr_reg(0x018, 0x24);   
            }
            else{
                hdmi_wr_reg(0x018, 0x22);   
            }
        }
        else{
            hdmi_wr_reg(0x018, serial_reg_val);
        }
        if((param->VIC==HDMI_1080p60)&&(param->color_depth==COLOR_30BIT)&&(hdmi_rd_reg(0x018)==0x22)){
            hdmi_wr_reg(0x018,0x12);
        }
        
    }
    else{
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x01); // Release serializer resets
        delay_us(10);
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x00); // Release reset on TX digital clock channel
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 1<<6); // Release resets all other TX digital clock domain, except tmds_clk
        delay_us(10);
    
        hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x00); // Final release reset on tmds_clk domain
        
        tmp_add_data = hdmi_rd_reg(0x018);
        if((tmp_add_data==0x22)||(tmp_add_data==0x12)){
            hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x08);        
            delay_us(10);
            hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x00);        
        }
    }
    hdmi_reconfig_packet_setting(param->VIC);
}

static void hdmi_audio_init(unsigned char spdif_flag)
{
    unsigned tmp_add_data;

    /* If TX_AUDIO_FORMAT is set as 0, "Channel Status" will not be sent out correctly */
    /* TX_AUDIO_CONTROL[bit 0] should be 1, otherwise no sound??? */
    unsigned char tx_i2s_spdif;
    unsigned char tx_i2s_8_channel;
    
    hdmi_print(IMP, AUD "%s\n", spdif_flag ? "SPDIF" : "I2S");
    
    if(spdif_flag){
        tx_i2s_spdif=0;
    }
    else{
        tx_i2s_spdif=1;
    }
    tx_i2s_8_channel = ((i2s_to_spdif_flag == 1)? 0:1 );

    hdmi_wr_reg(TX_AUDIO_CONTROL_MORE, 1);

    tmp_add_data  = 0;
    tmp_add_data |= tx_i2s_spdif    << 7; // [7]    I2S or SPDIF
    tmp_add_data |= tx_i2s_8_channel<< 6; // [6]    8 or 2ch
    tmp_add_data |= 2               << 4; // [5:4]  Serial Format: I2S format
    tmp_add_data |= 3               << 2; // [3:2]  Bit Width: 24-bit
    tmp_add_data |= 0               << 1; // [1]    WS Polarity: 0=WS high is right
    tmp_add_data |= 1               << 0; // [0]    For I2S: 0=one-bit audio; 1=I2S;
                                          //        For SPDIF: 0= channel status from input data; 1=from register
    hdmi_wr_reg(TX_AUDIO_FORMAT, tmp_add_data); // 0x2f

    //tmp_add_data  = 0;
    //tmp_add_data |= 0x4 << 4; // [7:4]  FIFO Depth=512
    //tmp_add_data |= 0x2 << 2; // [3:2]  Critical threshold=Depth/16
    //tmp_add_data |= 0x1 << 0; // [1:0]  Normal threshold=Depth/8
    //hdmi_wr_reg(TX_AUDIO_FIFO, tmp_add_data); // 0x49
    hdmi_wr_reg(TX_AUDIO_FIFO, aud_para); // 0x49

    hdmi_wr_reg(TX_AUDIO_LIPSYNC, 0); // [7:0] Normalized lip-sync param: 0 means S(lipsync) = S(total)/2

    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7]    forced_audio_fifo_clear
    tmp_add_data |= 1   << 6; // [6]    auto_audio_fifo_clear
    tmp_add_data |= 0x0 << 4; // [5:4]  audio_packet_type: 0=audio sample packet; 1=one bit audio; 2=HBR audio packet; 3=DST audio packet.
    tmp_add_data |= 0   << 3; // [3]    Rsrv
    tmp_add_data |= 0   << 2; // [2]    Audio sample packet's valid bit: 0=valid bit is 0 for I2S, is input data for SPDIF; 1=valid bit from register
    tmp_add_data |= 0   << 1; // [1]    Audio sample packet's user bit: 0=user bit is 0 for I2S, is input data for SPDIF; 1=user bit from register
    tmp_add_data |= 0   << 0; // [0]    0=Audio sample packet's sample_flat bit is 1; 1=sample_flat is 0.
    hdmi_wr_reg(TX_AUDIO_CONTROL, tmp_add_data); // 0x40

    tmp_add_data  = 0;
    tmp_add_data |= tx_i2s_8_channel<< 7; // [7]    Audio sample packet's header layout bit: 0=layout0; 1=layout1
    tmp_add_data |= 0               << 6; // [6]    Set normal_double bit in DST packet header.
    tmp_add_data |= 0               << 0; // [5:0]  Rsrv
    hdmi_wr_reg(TX_AUDIO_HEADER, tmp_add_data); // 0x00

    tmp_add_data  = tx_i2s_8_channel ? 0xff : 0x03;
    hdmi_wr_reg(TX_AUDIO_SAMPLE, tmp_add_data); // Channel valid for up to 8 channels, 1 bit per channel.

    hdmi_wr_reg(TX_AUDIO_PACK, 0x00); // Enable audio sample packets

    // Set N = 4096 (N is not measured, N must be configured so as to be a reference to clock_meter)
    hdmi_wr_reg(TX_SYS1_ACR_N_0, 0x00); // N[7:0]
    hdmi_wr_reg(TX_SYS1_ACR_N_1, 0x30); // N[15:8]

    tmp_add_data  = 0;
    tmp_add_data |= 0x3 << 4;    // [7:4] Meas Tolerance
    tmp_add_data |= 0x0 << 0;    // [3:0] N[19:16]
    hdmi_wr_reg(TX_SYS1_ACR_N_2, tmp_add_data); // 0xa0

    hdmi_wr_reg(TX_AUDIO_CONTROL,   hdmi_rd_reg(TX_AUDIO_CONTROL)|0x1); 
}

static void enable_audio_spdif(void)
{    
    hdmi_print(INF, AUD "Enable audio spdif to HDMI\n");

    /* enable audio*/        
    hdmi_wr_reg(TX_AUDIO_I2S,   0x0 );  // Address  0x5A=0x0    TX_AUDIO_I2S

    hdmi_wr_reg(TX_AUDIO_SPDIF, 1); // TX AUDIO SPDIF Enable
}

static void enable_audio_i2s(void)
{
    hdmi_print(INF, AUD "Enable audio i2s to HDMI\n");
    hdmi_wr_reg(TX_AUDIO_I2S,   0x1 );  // Address  0x5A=0x0    TX_AUDIO_I2S
    hdmi_wr_reg(TX_AUDIO_SPDIF, 0); // TX AUDIO SPDIF Enable
}    

/************************************
*    hdmitx hardware level interface
*************************************/

static void hdmitx_dump_tvenc_reg(int cur_VIC, int printk_flag) 
{
    int i,j;
    for(i=0;hdmi_tvenc_configs[i].vic!=HDMI_Unkown;i++){
        if(cur_VIC==hdmi_tvenc_configs[i].vic){
            reg_t* reg_set=hdmi_tvenc_configs[i].reg_set;
            hdmi_print(printk_flag, "------dump tevenc reg for mode %d----\n", cur_VIC);
            for(j=0;reg_set[j].reg;j++){
                hdmi_print(printk_flag, "[%08x]=%08x\n",reg_set[j].reg,aml_read_reg32(CBUS_REG_ADDR(reg_set[j].reg)));
            }
            hdmi_print(printk_flag, "------------------\n");
            break;
        }
    }
}    

static void hdmitx_config_tvenc_reg(int vic, unsigned reg, unsigned val)
{
    int i,j;
    for(i=0;hdmi_tvenc_configs[i].vic!=HDMI_Unkown;i++){
        if(vic==hdmi_tvenc_configs[i].vic){
            reg_t* reg_set=hdmi_tvenc_configs[i].reg_set;
            for(j=0;reg_set[j].reg;j++){
                if(reg_set[j].reg==reg){
                    reg_set[j].val = val;    
                    hdmi_print(INF, SYS "set [%08x]=%08x\n",reg_set[j].reg, reg_set[j].val);
                    break;
                }
            }
            if(reg_set[j].reg == 0){
                hdmi_print(INF, SYS "no [%08x] in config\n", reg);
            }
            break;
        }
    }
}

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
//
// func: hdmitx_set_pll_fr_auto
// params: none
// return:
//		1: current vmode is special and clock setting handled
//		0: current vmode is not special and clock setting not handled
//
// desc:
//		special vmode has same hdmi vic with normal mode, such as 1080p59hz - 1080p60hz
//	so pll should not only be set according hdmi vic.
//
extern const vinfo_t *get_current_vinfo(void);
static int hdmitx_set_pll_fr_auto(void)
{
	int ret = 0;
	const vinfo_t *pvinfo = get_current_vinfo();
	
	if( strncmp(pvinfo->name, "480p59hz", strlen("480p59hz")) == 0 )
	{
		set_vmode_clk(VMODE_480P_59HZ);
		ret = 1;
	}
	if( strncmp(pvinfo->name, "720p59hz", strlen("720p59hz")) == 0 )
	{
		set_vmode_clk(VMODE_720P_59HZ);
		ret = 1;
	}
	else if( strncmp(pvinfo->name, "1080i59hz", strlen("1080i59hz")) == 0 )
	{
		set_vmode_clk(VMODE_1080I_59HZ);
		ret = 1;
	}
	else if( strncmp(pvinfo->name, "1080p59hz", strlen("1080p59hz")) == 0 )
	{
		set_vmode_clk(VMODE_1080P_59HZ);
		ret = 1;
	}
	else if( strncmp(pvinfo->name, "1080p23hz", strlen("1080p23hz")) == 0 )
	{
		set_vmode_clk(VMODE_1080P_23HZ);
		ret = 1;
	}
	else if( strncmp(pvinfo->name, "4k2k29hz", strlen("4k2k29hz")) == 0 )
	{
		set_vmode_clk(VMODE_4K2K_29HZ);
		ret = 1;
	}
	else if( strncmp(pvinfo->name, "4k2k23hz", strlen("4k2k23hz")) == 0 )
	{
		set_vmode_clk(VMODE_4K2K_23HZ);
		ret = 1;
	}

	return ret;		
}
#endif

static void hdmitx_set_pll(Hdmi_tx_video_para_t *param)
{
    hdmi_print(IMP, SYS "set pll\n");
    hdmi_print(IMP, SYS "param->VIC:%d\n", param->VIC);
    
    cur_vout_index = get_cur_vout_index();

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	if( hdmitx_set_pll_fr_auto() )
		return ;
#endif

    switch(param->VIC)
    {
        case HDMI_480p60:
        case HDMI_480p60_16x9:
            set_vmode_clk(VMODE_480P);
            break;
        case HDMI_576p50:
        case HDMI_576p50_16x9:
            set_vmode_clk(VMODE_576P);
            break;
        case HDMI_480i60_16x9_rpt:
            set_vmode_clk(VMODE_480I_RPT);
            break;
        case HDMI_480p60_16x9_rpt:
            set_vmode_clk(VMODE_480P_RPT);
            break;
        case HDMI_576i50_16x9_rpt:
            set_vmode_clk(VMODE_576I_RPT);
            break;
        case HDMI_576p50_16x9_rpt:
            set_vmode_clk(VMODE_576P_RPT);
            break;
        case HDMI_480i60:
        case HDMI_480i60_16x9:
            set_vmode_clk(VMODE_480I);
            break;
        case HDMI_576i50:
        case HDMI_576i50_16x9:
            set_vmode_clk(VMODE_576I);
            break;
        case HDMI_1080p24://1080p24 support
            set_vmode_clk(VMODE_1080P_24HZ);
            break;
        case HDMI_1080p30:
        case HDMI_720p60:
        case HDMI_720p50:
            set_vmode_clk(VMODE_720P);
            break;
        case HDMI_1080i60:
        case HDMI_1080i50:
            set_vmode_clk(VMODE_1080I);
            break;
        case HDMI_1080p60:
        case HDMI_1080p50:
            set_vmode_clk(VMODE_1080P);
            break;
        case HDMI_4k2k_30:
        case HDMI_4k2k_25:
        case HDMI_4k2k_24:
        case HDMI_4k2k_smpte_24:
            set_vmode_clk(VMODE_4K2K_24HZ);
        default:
            break;
    }
}

static void hdmitx_set_phy(hdmitx_dev_t* hdmitx_device)
{
    if(!hdmitx_device)
        return;

    switch(hdmitx_device->cur_VIC) {
        case HDMI_4k2k_24:
        case HDMI_4k2k_25:
        case HDMI_4k2k_30:
        case HDMI_4k2k_smpte_24:
            aml_write_reg32(P_HHI_HDMI_PHY_CNTL0, 0x08c34d0b);
            break;
        case HDMI_1080p60:
        default:
            aml_write_reg32(P_HHI_HDMI_PHY_CNTL0, 0x08c31e8b);
            break;
    }
// P_HHI_HDMI_PHY_CNTL1     bit[1]: enable clock    bit[0]: soft reset
#define RESET_HDMI_PHY()                        \
    aml_write_reg32(P_HHI_HDMI_PHY_CNTL1, 3);   \
    msleep(1);                                  \
    aml_write_reg32(P_HHI_HDMI_PHY_CNTL1, 2);   \
    msleep(1)

    aml_write_reg32(P_HHI_HDMI_PHY_CNTL1, 0);
    msleep(1);
    RESET_HDMI_PHY();
    RESET_HDMI_PHY();
    RESET_HDMI_PHY();
#undef RESET_HDMI_PHY
    hdmi_print(IMP, SYS "phy setting done\n");
}

static int hdmitx_set_dispmode(hdmitx_dev_t* hdmitx_device, Hdmi_tx_video_para_t *param)
{
    if(param == NULL){ //disable HDMI
        return 0;
    }
    else {
        if(!hdmitx_edid_VIC_support(param->VIC))
            return -1;
    }

    if(color_depth_f==24)
        param->color_depth = COLOR_24BIT;
    else if(color_depth_f==30)
        param->color_depth = COLOR_30BIT;
    else if(color_depth_f==36)
        param->color_depth = COLOR_36BIT;
    else if(color_depth_f==48)
        param->color_depth = COLOR_48BIT;
    hdmi_print(INF, SYS "set mode VIC %d (cd%d,cs%d,pm%d,vd%d,%x) \n",param->VIC, color_depth_f, color_space_f,power_mode,power_off_vdac_flag,serial_reg_val);
    if(color_space_f != 0){
        param->color = color_space_f;
    }

    hdmi_hw_reset(hdmitx_device, param);    
	// move hdmitx_set_pll() to the end of this function.
    // hdmitx_set_pll(param);

    if((param->VIC==HDMI_720p60)||(param->VIC==HDMI_720p50)||
        (param->VIC==HDMI_1080i60)||(param->VIC==HDMI_1080i50)){
        aml_write_reg32(P_ENCP_VIDEO_HAVON_BEGIN,  aml_read_reg32(P_ENCP_VIDEO_HAVON_BEGIN)-1);     
        aml_write_reg32(P_ENCP_VIDEO_HAVON_END,  aml_read_reg32(P_ENCP_VIDEO_HAVON_END)-1);     
    }

    switch(param->VIC){
        case HDMI_480i60:
        case HDMI_480i60_16x9:
        case HDMI_576i50:
        case HDMI_576i50_16x9:
        case HDMI_480i60_16x9_rpt:
        case HDMI_576i50_16x9_rpt:
            hdmi_tvenc480i_set(param);
            break;
        case HDMI_1080i60:
        case HDMI_1080i50:
            hdmi_tvenc1080i_set(param);
            break;
        case HDMI_4k2k_30:
        case HDMI_4k2k_25:
        case HDMI_4k2k_24:
        case HDMI_4k2k_smpte_24:
            hdmi_tvenc4k2k_set(param);
            break;
        default:
            hdmi_tvenc_set(param);
        }
    hdmitx_dump_tvenc_reg(param->VIC, 0);

//todo     hdmitx_special_handler_video(hdmitx_device);

    // reset TX_SYS5_TX_SOFT_RESET_1/2 twice
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0xff);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0xff);
    mdelay(5);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x00);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x00);
    mdelay(5);

    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0xff);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0xff);
    mdelay(5);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x00);
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_2, 0x00);
    mdelay(5);

	hdmitx_set_pll(param);
    switch(param->VIC) {
    case HDMI_480i60:
    case HDMI_480i60_16x9:
    case HDMI_576i50:
    case HDMI_576i50_16x9:
    case HDMI_480i60_16x9_rpt:
    case HDMI_576i50_16x9_rpt:
        enc_vpu_bridge_reset(0);
        break;
    default:
        enc_vpu_bridge_reset(1);
        break;
    }

    hdmitx_set_phy(hdmitx_device);

    return 0;
}

static void hdmitx_set_packet(int type, unsigned char* DB, unsigned char* HB)
{
    // AVI frame
    int i ;
    unsigned char ucData ;
    unsigned int pkt_reg_base=TX_PKT_REG_AVI_INFO_BASE_ADDR;
    int pkt_data_len=0;
    
    switch(type)
    {
        case HDMI_PACKET_AVI:
            pkt_reg_base=TX_PKT_REG_AVI_INFO_BASE_ADDR; 
            pkt_data_len=13;
            break;
        case HDMI_PACKET_VEND:
            pkt_reg_base=TX_PKT_REG_VEND_INFO_BASE_ADDR;
            pkt_data_len=6;
            break;
        case HDMI_AUDIO_INFO:
            pkt_reg_base=TX_PKT_REG_AUDIO_INFO_BASE_ADDR;
            pkt_data_len=9;
            break;
        case HDMI_SOURCE_DESCRIPTION:
            pkt_reg_base=TX_PKT_REG_SPD_INFO_BASE_ADDR;
            pkt_data_len=25;
        default:
            break;
    }
    
    if(DB){
        for(i=0;i<pkt_data_len;i++){
            hdmi_wr_reg(pkt_reg_base+i+1, DB[i]);  
        }
    
        for(i = 0,ucData = 0; i < pkt_data_len ; i++)
        {
            ucData -= DB[i] ;
        }
        for(i=0; i<3; i++){
            ucData -= HB[i];
        }
        hdmi_wr_reg(pkt_reg_base+0x00, ucData);  
    
        hdmi_wr_reg(pkt_reg_base+0x1C, HB[0]);        
        hdmi_wr_reg(pkt_reg_base+0x1D, HB[1]);        
        hdmi_wr_reg(pkt_reg_base+0x1E, HB[2]);        
        hdmi_wr_reg(pkt_reg_base+0x1F, 0x00ff);        // Enable packet generation
    }
    else{
        hdmi_wr_reg(pkt_reg_base+0x1F, 0x0);        // disable packet generation
    }
}


static void hdmitx_setaudioinfoframe(unsigned char* AUD_DB, unsigned char* CHAN_STAT_BUF)
{
    int i ;
    unsigned char AUD_HB[3]={0x84, 0x1, 0xa};
    hdmitx_set_packet(HDMI_AUDIO_INFO, AUD_DB, AUD_HB);    
    //channel status
    if(CHAN_STAT_BUF){
        for(i=0;i<24;i++){
            hdmi_wr_reg(TX_IEC60958_SUB1_OFFSET+i, CHAN_STAT_BUF[i]);        
            hdmi_wr_reg(TX_IEC60958_SUB2_OFFSET+i, CHAN_STAT_BUF[24+i]);
        }
    }
}


//------------------------------------------------------------------------------
// set_hdmi_audio_source(unsigned int src)
//
// Description:
// Select HDMI audio clock source, and I2S input data source.
//
// Parameters:
//  src -- 0=no audio clock to HDMI; 1=pcmout to HDMI; 2=Aiu I2S out to HDMI.
//------------------------------------------------------------------------------
static void set_hdmi_audio_source(unsigned int src)
{
    unsigned long data32;
    unsigned int i;

    // Disable HDMI audio clock input and its I2S input
    data32  = 0;
    data32 |= 0     << 4;   // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= 0     << 0;   // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
//    Wr(AIU_HDMI_CLK_DATA_CTRL, data32);
    aml_write_reg32(P_AIU_HDMI_CLK_DATA_CTRL, data32);

    switch(src)
    {
        case 0:
            hdmi_print(ERR, AUD "No audio clock to HDMI\n");
            break;
        case 1:
            hdmi_print(IMP, AUD "PCM out to HDMI\n");//SPDIF
            // Enable HDMI audio clock from the selected source
            data32  = 0;
            data32 |= 0      << 4;  // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
            data32 |= 2      << 0;  // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
            aml_write_reg32(P_AIU_HDMI_CLK_DATA_CTRL, data32);

            // Wait until clock change is settled
            i = 0;
            while ( (((aml_read_reg32(P_AIU_HDMI_CLK_DATA_CTRL))>>8)&0x3) != 0x2 ) {
            //    if (i > 255) {
            //        //stimulus_print("[TEST.C] Error: set_hdmi_audio_source timeout!\n");
            //        //stimulus_finish_fail(10);
            //    }
                i ++;
                if(i>100000)
                    break;
            }
            if(i>100000)
                hdmi_print(ERR, AUD "Time out: AIU_HDMI_CLK_DATA_CTRL\n");
           break;
        case 2:
            hdmi_print(INF, AUD "I2S out to HDMI\n");//I2S
            // Enable HDMI audio clock from the selected source
            data32  = 0;
            data32 |= 0      << 4;  // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
            data32 |= 2      << 0;  // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
            aml_write_reg32(P_AIU_HDMI_CLK_DATA_CTRL, data32);

            // Wait until clock change is settled
            i = 0;
            while ( (((aml_read_reg32(P_AIU_HDMI_CLK_DATA_CTRL))>>8)&0x3) != 0x2 ) {
            //    if (i > 255) {
            //        //stimulus_print("[TEST.C] Error: set_hdmi_audio_source timeout!\n");
            //        //stimulus_finish_fail(10);
            //    }
                i ++;
                if(i>100000)
                    break;
            }
            if(i>100000)
            hdmi_print(ERR, AUD "Time out: AIU_HDMI_CLK_DATA_CTRL !\n");
            // Enable HDMI I2S input from the selected source
            data32  = 0;
            data32 |= 2   << 4;   // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
            data32 |= 2   << 0;   // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
            aml_write_reg32(P_AIU_HDMI_CLK_DATA_CTRL, data32);

            // Wait until data change is settled
            i = 0;
            while ((((aml_read_reg32(P_AIU_HDMI_CLK_DATA_CTRL))>>12)&0x3) != 0x2 ) {
                i++;
                if(i>100000)
                    break;
            }
            if(i>100000)
                hdmi_print(ERR, AUD "Time out: AIU_HDMI_CLK_DATA_CTRL...\n");
            break;
        default:
            hdmi_print(ERR, AUD "Audio Src clock to HDMI Error\n");
            break;
    }
} /* set_hdmi_audio_source */

static void hdmitx_set_aud_pkt_type(audio_type_t type)
{
    // TX_AUDIO_CONTROL [5:4]
    //   0: Audio sample packet (HB0 = 0x02)
    //   1: One bit audio packet (HB0 = 0x07)
    //   2: HBR Audio packet (HB0 = 0x09)
    //   3: DST Audio packet (HB0 = 0x08)
    switch(type) {
    case CT_MAT:
        hdmi_set_reg_bits(TX_AUDIO_CONTROL, 0x2, 4, 2);
        break;
    case CT_ONE_BIT_AUDIO:
        hdmi_set_reg_bits(TX_AUDIO_CONTROL, 0x1, 4, 2);
        break;
    case CT_DST:
        hdmi_set_reg_bits(TX_AUDIO_CONTROL, 0x3, 4, 2);
        break;
    default:
        hdmi_set_reg_bits(TX_AUDIO_CONTROL, 0x0, 4, 2);
        break;
    }
}

static Cts_conf_tab cts_table_192k[] = {
    {24576,  27000,  27000},
    {24576,  54000,  54000},
    {24576, 108000, 108000},
    {24576,  74250,  74250},
    {24576, 148500, 148500},
    {24576, 297000, 297000},
};

static unsigned int get_cts(unsigned int clk)
{
    int i;

    for(i = 0; i < ARRAY_SIZE(cts_table_192k); i++) {
        if(clk == cts_table_192k[i].tmds_clk)
            return cts_table_192k[i].fixed_cts;
    }

    return 0;
}

static Vic_attr_map vic_attr_map_table[] = {
    {HDMI_640x480p60,       27000 },
    {HDMI_480p60,           27000 },
    {HDMI_480p60_16x9,      27000 },
    {HDMI_720p60,           74250 },
    {HDMI_1080i60,          74250 },
    {HDMI_480i60,           27000 },
    {HDMI_480i60_16x9,      27000 },
    {HDMI_480i60_16x9_rpt,  54000 },
    {HDMI_1440x480p60,      27000 },
    {HDMI_1440x480p60_16x9, 27000 },
    {HDMI_1080p60,          148500},
    {HDMI_576p50,           27000 },
    {HDMI_576p50_16x9,      27000 },
    {HDMI_720p50,           74250 },
    {HDMI_1080i50,          74250 },
    {HDMI_576i50,           27000 },
    {HDMI_576i50_16x9,      27000 },
    {HDMI_576i50_16x9_rpt,  54000 },
    {HDMI_1080p50,          148500},
    {HDMI_1080p24,          74250 },
    {HDMI_1080p25,          74250 },
    {HDMI_1080p30,          74250 },
    {HDMI_480p60_16x9_rpt,  108000},
    {HDMI_576p50_16x9_rpt,  108000},
    {HDMI_4k2k_24,          247500},
    {HDMI_4k2k_25,          247500},
    {HDMI_4k2k_30,          247500},
    {HDMI_4k2k_smpte_24,    247500},
};

static unsigned int vic_map_clk(HDMI_Video_Codes_t vic)
{
    int i;

    for(i = 0; i < ARRAY_SIZE(vic_attr_map_table); i++) {
        if(vic == vic_attr_map_table[i].VIC)
            return vic_attr_map_table[i].tmds_clk;
    }

    return 0;
}

static void hdmitx_set_aud_cts(audio_type_t type, Hdmi_tx_audio_cts_t cts_mode, HDMI_Video_Codes_t vic)
{
    unsigned int cts_val = 0;

    switch(type) {
    case CT_MAT:
        if(cts_mode == AUD_CTS_FIXED) {
            unsigned int clk = vic_map_clk(vic);
            if(clk) {
                cts_val = get_cts(clk);
                if(!cts_val)
                    hdmi_print(ERR, AUD "not find cts\n");
            }
            else {
                hdmi_print(ERR, AUD "not find tmds clk\n");
            }
        }
        if(cts_mode == AUD_CTS_CALC) {
            // TODO
        }
        break;
    default:
        hdmi_wr_reg(TX_SYS0_ACR_CTS_0, 0);      //audio_CTS & 0xff);
        hdmi_wr_reg(TX_SYS0_ACR_CTS_1, 0);      //(audio_CTS>>8) & 0xff);
        hdmi_wr_reg(TX_SYS0_ACR_CTS_2, 1 << 5);      // set bit[5] force_arc_stable to 1
        break;
    }

    if(cts_mode == AUD_CTS_FIXED) {
        hdmi_wr_reg(TX_SYS0_ACR_CTS_0, cts_val & 0xff);
        hdmi_wr_reg(TX_SYS0_ACR_CTS_1, (cts_val >> 8) & 0xff);
        hdmi_wr_reg(TX_SYS0_ACR_CTS_2, ((cts_val >> 16) & 0xff) | (1 << 4));
        hdmi_print(IMP, AUD "type: %d  CTS Mode: %d  VIC: %d  CTS: %d\n", type, cts_mode, vic, cts_val);
    }
}

static int hdmitx_set_audmode(struct hdmi_tx_dev_s* hdmitx_device, Hdmi_tx_audio_para_t* audio_param)
{
    unsigned int audio_N_para = 6272;
    unsigned int audio_N_tolerance = 3;
//    unsigned int audio_CTS = 30000;
    
    hdmi_print(INF, AUD "audio channel num is %d\n", hdmitx_device->cur_audio_param.channel_num);

    hdmi_wr_reg(TX_PACKET_CONTROL_2, hdmi_rd_reg(TX_PACKET_CONTROL_2) & (~(1<<3)));
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x30);     // reset audio master & sample
    hdmi_wr_reg(TX_SYS5_TX_SOFT_RESET_1, 0x00);

    if(hdmitx_device->cur_audio_param.channel_num > CC_2CH) {
        i2s_to_spdif_flag = 0;
    }else{
        i2s_to_spdif_flag = 1;
    }
    if(!hdmi_audio_off_flag){
        hdmi_audio_init(i2s_to_spdif_flag);
    }
    else {
        hdmi_wr_reg(TX_AUDIO_PACK, 0x00); // disable audio sample packets
    }

//Refer to HDMI SPEC V1.4 Page 137
    hdmi_print(INF, AUD "current VIC: %d\n", hdmitx_device->cur_VIC);
    hdmi_print(INF, AUD "audio sample rate: %d\n", audio_param->sample_rate);
    switch(audio_param->sample_rate) {
    case FS_48K:
        audio_N_para = 6144 * 2;
        if((hdmitx_device->cur_VIC == HDMI_4k2k_24) || (hdmitx_device->cur_VIC == HDMI_4k2k_25)
        || (hdmitx_device->cur_VIC == HDMI_4k2k_30) || (hdmitx_device->cur_VIC == HDMI_4k2k_smpte_24)) {
            audio_N_para = 5120;
        }
        break;
    case FS_32K:
        audio_N_para = 4096;
        break;
    case FS_44K1:
        audio_N_para = 6272 * 2;
        if((hdmitx_device->cur_VIC == HDMI_4k2k_24) || (hdmitx_device->cur_VIC == HDMI_4k2k_25)
        || (hdmitx_device->cur_VIC == HDMI_4k2k_30) || (hdmitx_device->cur_VIC == HDMI_4k2k_smpte_24)) {
            audio_N_para = 4704;
        }
        break;
    case FS_88K2:
        audio_N_para = 12544;
        if((hdmitx_device->cur_VIC == HDMI_4k2k_24) || (hdmitx_device->cur_VIC == HDMI_4k2k_25)
        || (hdmitx_device->cur_VIC == HDMI_4k2k_30) || (hdmitx_device->cur_VIC == HDMI_4k2k_smpte_24)) {
            audio_N_para = 9408;
        }
        break;
    case FS_176K4:
        audio_N_para = 25088;
        if((hdmitx_device->cur_VIC == HDMI_4k2k_24) || (hdmitx_device->cur_VIC == HDMI_4k2k_25)
        || (hdmitx_device->cur_VIC == HDMI_4k2k_30) || (hdmitx_device->cur_VIC == HDMI_4k2k_smpte_24)) {
            audio_N_para = 18816;
        }
        break;
    case FS_96K:
        audio_N_para = 12288;
        if((hdmitx_device->cur_VIC == HDMI_4k2k_24) || (hdmitx_device->cur_VIC == HDMI_4k2k_25)
        || (hdmitx_device->cur_VIC == HDMI_4k2k_30) || (hdmitx_device->cur_VIC == HDMI_4k2k_smpte_24)) {
            audio_N_para = 10240;
        }
        break;
    case FS_192K:
        audio_N_para = 24576;
        if((hdmitx_device->cur_VIC == HDMI_4k2k_24) || (hdmitx_device->cur_VIC == HDMI_4k2k_25)
        || (hdmitx_device->cur_VIC == HDMI_4k2k_30) || (hdmitx_device->cur_VIC == HDMI_4k2k_smpte_24)) {
            audio_N_para = 20480;
        }
        break;
    default:
        audio_N_para = 6144 * 2;
        break;
    }

    if((hdmitx_device->cur_VIC == HDMI_1080p24) && (audio_param->sample_rate == FS_48K)) {
        audio_N_para = 6144 * 3;
    }
    hdmi_print(INF, AUD "set audio N para\n");

    hdmitx_set_aud_pkt_type(audio_param->type);

    //TODO. Different audio type, maybe have special settings
    switch(audio_param->type){
        case CT_DOLBY_D:
            audio_N_para *= 4;
            break;
        case CT_PCM:
            break;
        case CT_AC_3:
            break;
        case CT_MPEG1:
            break;
        case CT_MP3:
            break;
        case CT_MPEG2:
            break;
        case CT_AAC:
            break;
        case CT_DTS:
            break;
        case CT_ATRAC:
            break;
        case CT_ONE_BIT_AUDIO:
            break;
        case CT_DTS_HD:
            audio_N_para *= 4;
            break;
        case CT_MAT:
            break;
        case CT_DST:
            break;
        case CT_WMA:
            break;
        default:
            break;
    }

    hdmi_wr_reg(TX_SYS1_ACR_N_0, (audio_N_para&0xff)); // N[7:0]
    hdmi_wr_reg(TX_SYS1_ACR_N_1, (audio_N_para>>8)&0xff); // N[15:8]
    hdmi_wr_reg(TX_SYS1_ACR_N_2, (audio_N_tolerance<<4)|((audio_N_para>>16)&0xf)); // N[19:16]
    hdmi_wr_reg(TX_AUDIO_CONTROL,   hdmi_rd_reg(TX_AUDIO_CONTROL)|0x1);

    set_hdmi_audio_source(i2s_to_spdif_flag ? 1 : 2);

    hdmi_print(INF, AUD "i2s_to_spdif_flag:%d \n", i2s_to_spdif_flag);
    if(i2s_to_spdif_flag)
        enable_audio_spdif();
    else
        enable_audio_i2s();

    if((i2s_to_spdif_flag == 1) && (hdmitx_device->cur_audio_param.type != CT_PCM)) {
        hdmi_wr_reg(TX_AUDIO_FORMAT, (hdmi_rd_reg(TX_AUDIO_FORMAT) & 0xfe));        // clear bit0, use channel status bit from input data
    }

    if(audio_param->type == CT_MAT)
        hdmitx_set_aud_cts(audio_param->type, AUD_CTS_FIXED, hdmitx_device->cur_VIC);
    else
        hdmitx_set_aud_cts(audio_param->type, AUD_CTS_AUTO, hdmitx_device->cur_VIC);

//todo    hdmitx_special_handler_audio(hdmitx_device);

    return 0;
}    
    
static void hdmitx_setupirq(hdmitx_dev_t* hdmitx_device)
{
   int r;
   r = request_irq(INT_HDMI_TX, &intr_handler,
                    IRQF_SHARED, "amhdmitx",
                    (void *)hdmitx_device);
}    


#if 1

//Expect 8*10-Bit shift pattern data:
//
//0x2e3   = 1011100011
//0x245   = 1001000101
//0x1cb   = 0111001011
//0x225   = 1000100101
//0x2da   = 1011011010
//0x3e0   = 1111100000
//0x367   = 1101100111
//0x000   = 0000000000

static void turn_on_shift_pattern (void)
{
    unsigned int tmp_add_data;
    hdmi_wr_reg(TX_SYS0_BIST_DATA_0, 0x00);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_1, 0x6c);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_2, 0xfe);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_3, 0x41);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_4, 0x5b);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_5, 0x91);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_6, 0x3a);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_7, 0x9d);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_8, 0x68);
    hdmi_wr_reg(TX_SYS0_BIST_DATA_9, 0xc7);

    //tmp_add_data[7:6] = 2'b0;     // audio_source_select[1:0]
    //tmp_add_data[5] = 1'b0;       // external_packet_enable
    //tmp_add_data[4] = 1'b1 ;      // internal_packet_enable
    //tmp_add_data[3:2] = 2'b0;     // afe_fifo_source_select_lane_1[1:0]
    //tmp_add_data[1:0] = 2'd3 ;    // afe_fifo_source_select_lane_0[1:0] : 0=data path; 1=injected on lane 0; 2=inject on lane 1; 3=BIST.
    tmp_add_data = 0x13;
    hdmi_wr_reg(TX_CORE_DATA_CAPTURE_2, tmp_add_data);

    hdmi_wr_reg(TX_SYS0_BIST_CONTROL, 0x00); // Reset BIST
    hdmi_wr_reg(TX_SYS0_BIST_CONTROL, 0xc0); // Enable shift pattern BIST
}

static void turn_off_shift_pattern (void)
{
    unsigned int tmp_add_data;
    hdmi_wr_reg(TX_SYS0_BIST_CONTROL, 0x00); // Reset BIST

    //tmp_add_data[7:6] = 2'b0;     // audio_source_select[1:0]
    //tmp_add_data[5] = 1'b0;       // external_packet_enable
    //tmp_add_data[4] = 1'b1 ;      // internal_packet_enable
    //tmp_add_data[3:2] = 2'b0;     // afe_fifo_source_select_lane_1[1:0]
    //tmp_add_data[1:0] = 2'd0 ;    // afe_fifo_source_select_lane_0[1:0] : 0=data path; 1=injected on lane 0; 2=inject on lane 1; 3=BIST.
    tmp_add_data = 0x10;
    hdmi_wr_reg(TX_CORE_DATA_CAPTURE_2, tmp_add_data);
}

static void turn_on_prbs_mode(int prbs_mode)
{
    unsigned int tmp_add_data;
   tmp_add_data     = 0;
    tmp_add_data    |= 0    << 6;   // [7:6] audio_source_select[1:0]
    tmp_add_data    |= 0    << 5;   //   [5] external_packet_enable
    tmp_add_data    |= 0    << 4;   //   [4] internal_packet_enable
    tmp_add_data    |= 0    << 2;   // [3:2] afe_fifo_source_select_lane_1[1:0]: 0=DATA_PATH; 1=TMDS_LANE_0; 2=TMDS_LANE_1; 3=BIST_PATTERN
    tmp_add_data    |= 3    << 0;   // [1:0] afe_fifo_source_select_lane_0[1:0]: 0=DATA_PATH; 1=TMDS_LANE_0; 2=TMDS_LANE_1; 3=BIST_PATTERN
    hdmi_wr_reg(TX_CORE_DATA_CAPTURE_2, tmp_add_data); // 0x03

    tmp_add_data     = 0;
    tmp_add_data    |= 0    << 7;   //   [7] monitor_lane_1
    tmp_add_data    |= 0    << 4;   // [6:4] monitor_select_lane_1[2:0]
    tmp_add_data    |= 1    << 3;   //   [3] monitor_lane_0
    tmp_add_data    |= 7    << 0;   // [2:0] monitor_select_lane_0[2:0]: 7=TMDS_ENCODE
    hdmi_wr_reg(TX_CORE_DATA_MONITOR_1, tmp_add_data); // 0x0f

    // Program PRBS_MODE
    hdmi_wr_reg(TX_SYS1_PRBS_DATA, prbs_mode); // 0=PRBS 11; 1=PRBS 15; 2=PRBS 7; 3=PRBS 31.
    // Program PRBS BIST

    tmp_add_data     = 0;
    tmp_add_data    |= 1    << 7;   //   [7] afe_bist_enable
    tmp_add_data    |= 0    << 6;   //   [6] tmds_shift_pattern_select
    tmp_add_data    |= 3    << 4;   // [5:4] tmds_prbs_pattern_select[1:0]:
                                    //       0=output all 0; 1=output 8-bit pattern;
                                    //       2=output 1-bit differential pattern; 3=output 10-bit pattern
    tmp_add_data    |= 0    << 3;   //   [3] Rsrv
    tmp_add_data    |= 0    << 0;   // [2:0] tmds_repeat_bist_pattern[2:0]
    hdmi_wr_reg(TX_SYS0_BIST_CONTROL, tmp_add_data); // 0xb0

    hdmi_print(INF, SYS "PRBS mode %d On\n", prbs_mode);
}
    
#endif

static void hdmitx_uninit(hdmitx_dev_t* hdmitx_device)
{
    //aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR);
    //aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK)&(~(1 << 25)));
    free_irq(INT_HDMI_TX, (void *)hdmitx_device);
    hdmi_print(1,"power off hdmi, unmux hpd\n");
    
    phy_pll_off();
    digital_clk_off(7); //off sys clk
    unmux_hpd();
}    

static char hdcp_log_buf[HDMITX_HDCP_MONITOR_BUF_SIZE] = { 0 };
const static hdcp_sub_t hdcp_monitor_array[] = {
    {"Aksv_shw", TX_HDCP_SHW_AKSV_0, 5},
    {"Bksv    ", TX_HDCP_SHW_BKSV_0, 5},
    {"Ainfo   ", TX_HDCP_SHW_AINFO , 1},
    {"An      ", TX_HDCP_SHW_AN_0 ,  8},
    {"Bcaps   ", TX_HDCP_SHW_BCAPS,  1},
    {"Bstatus ", TX_HDCP_SHW_BSTATUS_0, 2},
    {"Km      ", 0x148,              7},
    {"Ri      ", 0x150,              2},
    {"Ri'     ", TX_HDCP_SHW_RI1_0 , 2},
    {"Pj      ", 0x14f,              1},
    {"Pj'     ", TX_HDCP_SHW_PJ1   , 1},
    {"AuthSt  ", TX_HDCP_ST_AUTHENTICATION, 1},
    {"EncrySt0", TX_HDCP_ST_STATUS_0, 1},
    {"EncrySt1", TX_HDCP_ST_STATUS_1, 1},
    {"EncrySt2", TX_HDCP_ST_STATUS_2, 1},
    {"EncrySt3", TX_HDCP_ST_STATUS_3, 1},
    {"FramCnt ", TX_HDCP_ST_FRAME_COUNT, 1},
    {"EDIDStat", TX_HDCP_ST_EDID_STATUS, 1},
    {"MEMStat ", TX_HDCP_ST_MEM_STATUS, 1},
    {"EDID_Ext", 0x198, 1},
    {"EDDC_SEG", 0x199, 1},
    {"EDDC_ST ", 0x19a, 1},
    {"HDCP_RSV", 0x19b, 4},
    {"EDDC_SEG", 0x19c, 1},
    {"HDCPMODE", TX_HDCP_ST_ST_MODE, 1},
    {"TmdsMod ", TX_TMDS_MODE, 1},
};

static int hdmitx_cntl(hdmitx_dev_t* hdmitx_device, unsigned cmd, unsigned argv)
{
    if(cmd == HDMITX_AVMUTE_CNTL) {
        if(argv == AVMUTE_SET) {
            hdmi_wr_reg(TX_HDCP_MODE, hdmi_rd_reg(TX_HDCP_MODE)&(~(1<<4)));
            hdmi_wr_reg(TX_HDCP_MODE, hdmi_rd_reg(TX_HDCP_MODE)|(1<<5));
        }
        if(argv == AVMUTE_CLEAR) {
            hdmi_wr_reg(TX_HDCP_MODE, hdmi_rd_reg(TX_HDCP_MODE)&(~(1<<5)));
            hdmi_wr_reg(TX_HDCP_MODE, hdmi_rd_reg(TX_HDCP_MODE)|(1<<4));
        }
        if(argv == AVMUTE_OFF) {
            hdmi_wr_reg(TX_HDCP_MODE, hdmi_rd_reg(TX_HDCP_MODE)&(~(1<<4)));
            hdmi_wr_reg(TX_HDCP_MODE, hdmi_rd_reg(TX_HDCP_MODE)&(~(1<<5)));
        }
        return 0;
    }
    else if(cmd == HDMITX_SW_INTERNAL_HPD_TRIG){
        if(argv == 1)   // soft trig rising
            hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_INTR_STAT, 1);
        else            // soft trig falling
            hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_INTR_STAT, 2);
    }
    else if(cmd == HDMITX_EARLY_SUSPEND_RESUME_CNTL) {
        if(argv == HDMITX_EARLY_SUSPEND) {
            aml_set_reg32_bits(P_HHI_VID_PLL_CNTL, 0, 30, 1);
            hdmi_wr_reg(TX_AUDIO_CONTROL_MORE, 0);
            hdmi_phy_suspend();
        }
        if(argv == HDMITX_LATE_RESUME) {
            hdmi_wr_reg(TX_AUDIO_CONTROL_MORE, 1);
            aml_set_reg32_bits(P_HHI_VID_PLL_CNTL, 1, 30, 1);
            //hdmi_phy_wakeup();  	// no need
        }
        return 0;
    }
    else if(cmd == HDMITX_HDCP_MONITOR) {
        int i, len, st;
        int array = sizeof(hdcp_monitor_array) / sizeof(hdcp_sub_t);

        int pos;

        if(!(hdmitx_device->log & HDMI_LOG_HDCP))
            return 0;

        memset(hdcp_log_buf, 0, HDMITX_HDCP_MONITOR_BUF_SIZE);
        hdmi_print(INF, HDCP "\n\nMonitor HDCP start\n");
        pos = 0;
        for(i = 0; i < array; i ++){
            len = hdcp_monitor_array[i].hdcp_sub_len;
            st  = hdcp_monitor_array[i].hdcp_sub_addr_start;
            pos += sprintf(hdcp_log_buf + pos, "   %s: ", hdcp_monitor_array[i].hdcp_sub_name);
            do {
                pos += sprintf(hdcp_log_buf + pos, "%02x", (unsigned)hdmi_rd_reg(st+len-1));
            }while(--len);
            pos += sprintf(hdcp_log_buf + pos, "\n");
        }
        pos += sprintf(hdcp_log_buf + pos, "HDCP %s",
                                            ((hdmi_rd_reg(TX_HDCP_ST_STATUS_3)&0xa0)==0xa0)
                                            ? "OK\n" : "BAD\n"
                       );
        printk("%s", hdcp_log_buf);
        hdmi_print(INF, HDCP "Monitor HDCP end\n");
        return 0;
    }
    else if(cmd == HDMITX_IP_SW_RST){
        return 0;    //TODO
        aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)|(1<<16));
        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_CTRL0, hdmi_rd_reg(OTHER_BASE_ADDR + HDMI_OTHER_CTRL0) | (argv));
        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_CTRL0, hdmi_rd_reg(OTHER_BASE_ADDR + HDMI_OTHER_CTRL0) & (~(argv)));
        aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)&(~(1<<16)));
        hdmi_print(INF, SYS "reset IP: 0x%x\n", argv);
        return 0;
    }
    else if(cmd == HDMITX_CBUS_RST) {
        return 0;//todo
        aml_set_reg32_bits(P_RESET2_REGISTER, 1, 15, 1);
        msleep(50);
        hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_INTR_STAT_CLR, 0x7);
        return 0;
    }
    else if(cmd == HDMITX_INTR_MASKN_CNTL) {
        if(argv == INTR_MASKN_ENABLE) {
            hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_INTR_MASKN, 0);
        }
        if(argv == INTR_MASKN_DISABLE) {
            hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_INTR_MASKN, 0x7);
        }
        if(argv == INTR_CLEAR) {
            hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_STAT_CLR, 0x7);
        }
        return 0;
    }
    else if(cmd == HDMITX_IP_INTR_MASN_RST){
        hdmi_print(INF, SYS "reset intr mask\n");
        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_MASKN, 0);
        msleep(2);
        hdmi_wr_reg(OTHER_BASE_ADDR + HDMI_OTHER_INTR_MASKN, 0x7);
    }
    else if(cmd == HDMITX_HWCMD_MUX_HPD_IF_PIN_HIGH){
        /* turnon digital module if gpio is high */
        if(is_hpd_muxed() == 0){
            if(read_hpd_gpio()){
                hdmitx_device->internal_mode_change = 0;
                msleep(500);
                if(read_hpd_gpio()){
                hdmi_print(IMP, HPD "mux hpd\n");
                digital_clk_on(4);
                delay_us(1000*100);
                mux_hpd();
                }
            }
        }
    }
    else if(cmd == HDMITX_HWCMD_MUX_HPD){
         mux_hpd();
    } 
// For test only. 
    else if(cmd == HDMITX_HWCMD_TURNOFF_HDMIHW){
        int unmux_hpd_flag = argv;
//        WRITE_MPEG_REG(VENC_DVI_SETTING, READ_MPEG_REG(VENC_DVI_SETTING)&(~(1<<13))); //bit 13 is used by HDMI only
//        digital_clk_on(4); //enable sys clk so that hdmi registers can be accessed when calling phy_pll_off/digit_clk_off
        if(unmux_hpd_flag){
            hdmi_print(IMP, SYS "power off hdmi, unmux hpd\n");
            phy_pll_off();
            digital_clk_off(4); //off sys clk
            unmux_hpd();
        }
        else{
            hdmi_print(IMP, SYS "power off hdmi\n");
            digital_clk_on(6);
            phy_pll_off();      //should call digital_clk_on(), otherwise hdmi_rd/wr_reg will hungup
            digital_clk_off(3); //do not off sys clk
        }
#ifdef CONFIG_HDMI_TX_PHY
    digital_clk_off(7);
#endif
    }
    else if(cmd == HDMITX_HWCMD_TURN_ON_PRBS){
        turn_on_prbs_mode(argv);    
    }
    return 0;
}

static void hdmitx_print_info(hdmitx_dev_t* hdmitx_device, int printk_flag)
{
    hdmi_print(INF, "------------------\nHdmitx driver version: %s\nSerial %x\nColor Depth %d\n", HDMITX_VER, serial_reg_val, color_depth_f);
    hdmi_print(INF, "current vout index %d\n", cur_vout_index);
    hdmi_print(INF, "reset sequence %d\n", new_reset_sequence_flag);
    hdmi_print(INF, "power mode %d\n", power_mode);
    hdmi_print(INF, "%spowerdown when unplug\n",hdmitx_device->unplug_powerdown?"":"do not ");
    hdmi_print(INF, "use_tvenc_conf_flag=%d\n",use_tvenc_conf_flag); 
    hdmi_print(INF, "vdac %s\n", power_off_vdac_flag?"off":"on");
    hdmi_print(INF, "hdmi audio %s\n", hdmi_audio_off_flag?"off":"on");
    if(!hdmi_audio_off_flag){
        hdmi_print(INF, "audio out type %s\n", i2s_to_spdif_flag?"spdif":"i2s");
    }
    hdmi_print(INF, "delay flag %d\n", delay_flag);
    hdmi_print(INF, "------------------\n");
}

typedef struct {
    unsigned int val : 20;
    unsigned int stable: 1;
}aud_cts_log_t;

static inline unsigned int get_msr_cts(void)
{
    unsigned int ret;

    ret  = hdmi_rd_reg(TX_TMDS_ST_CLOCK_METER_1);
    ret += (hdmi_rd_reg(TX_TMDS_ST_CLOCK_METER_2) << 8);
    ret += ((hdmi_rd_reg(TX_TMDS_ST_CLOCK_METER_3) & 0xf) << 16);

    return ret;
}

static inline unsigned int get_msr_cts_st(void)
{
    return !!(hdmi_rd_reg(TX_TMDS_ST_CLOCK_METER_3) & 0x80);
}

#define AUD_CTS_LOG_NUM     1000
aud_cts_log_t cts_buf[AUD_CTS_LOG_NUM];
static void cts_test(hdmitx_dev_t* hdmitx_device)
{
    int i, j;
    unsigned int min = 0, max = 0, total = 0;

    printk("\nhdmitx: audio: cts test\n");
    memset(cts_buf, 0, sizeof(cts_buf));
    for(i = 0; i < AUD_CTS_LOG_NUM; i++) {
        cts_buf[i].val = get_msr_cts();
        cts_buf[i].stable = get_msr_cts_st();
        mdelay(1);
    }

    printk("cts unstable:\n");
    for(i = 0, j = 0; i < AUD_CTS_LOG_NUM; i++) {
        if(cts_buf[i].stable == 0) {
            printk("%d  ", i);
            j ++;
            if(((j+1) & 0xf) == 0)
                printk("\n");
        }
    }

    printk("\ncts change:\n");
    for(i = 1; i < AUD_CTS_LOG_NUM; i++) {
        if(cts_buf[i].val > cts_buf[i-1].val)
            printk("dis: +%d  [%d] %d  [%d] %d\n", cts_buf[i].val - cts_buf[i-1].val, i, cts_buf[i].val, i - 1, cts_buf[i - 1].val);
        if(cts_buf[i].val < cts_buf[i-1].val)
            printk("dis: %d  [%d] %d  [%d] %d\n", cts_buf[i].val - cts_buf[i-1].val, i, cts_buf[i].val, i - 1, cts_buf[i - 1].val);
    }

    min = max = cts_buf[0].val;
    for(i = 0; i < AUD_CTS_LOG_NUM; i++) {
        total += cts_buf[i].val;
        if(min > cts_buf[i].val)
            min = cts_buf[i].val;
        if(max < cts_buf[i].val)
            max = cts_buf[i].val;
    }
    printk("\nCTS Min: %d   Max: %d   Avg: %d/1000\n\n", min, max, total);
}

static void hdmitx_debug(hdmitx_dev_t* hdmitx_device, const char* buf)
{
    char tmpbuf[128];
    int i=0;
    unsigned int adr;
    unsigned int value=0;
    while((buf[i])&&(buf[i]!=',')&&(buf[i]!=' ')){
        tmpbuf[i]=buf[i];
        i++;    
    }
    tmpbuf[i]=0;
    if((strncmp(tmpbuf, "dumpreg", 7)==0) || (strncmp(tmpbuf, "dumptvencreg", 12)==0)){
        hdmitx_dump_tvenc_reg(hdmitx_device->cur_VIC, 1);
        return;
    }
    else if(strncmp(tmpbuf, "ctstest", 7) == 0) {
        cts_test(hdmitx_device);
        return;
    }
    else if(strncmp(tmpbuf, "ss", 2) == 0) {
        printk("hdmitx_device->output_blank_flag: 0x%x\n", hdmitx_device->output_blank_flag);
        printk("hdmitx_device->hpd_state: 0x%x\n", hdmitx_device->hpd_state);
        printk("hdmitx_device->cur_VIC: 0x%x\n", hdmitx_device->cur_VIC);
    }
    else if(strncmp(tmpbuf, "hpd_lock", 8) == 0) {
        if(tmpbuf[8] == '1') {
            hdmitx_device->hpd_lock = 1;
            hdmi_print(INF, HPD "hdmitx: lock hpd\n");
        }
        else {
            hdmitx_device->hpd_lock = 0;
            hdmi_print(INF, HPD "hdmitx: unlock hpd\n");
        }
        return ;
    }
    else if(strncmp(tmpbuf, "vic", 3)==0) {
        printk("hdmi vic count = %d\n", hdmitx_device->vic_count);
        if((tmpbuf[3] >= '0') && (tmpbuf[3] <= '9')){
            hdmitx_device->vic_count = tmpbuf[3] - '0';
            hdmi_print(INF, SYS "set hdmi vic count = %d\n", hdmitx_device->vic_count);
        }
    }
    else if(strncmp(tmpbuf, "cec", 3)==0) {
        extern void cec_test_(unsigned int cmd);
        cec_test_(tmpbuf[3] - '0');
    }
    else if(strncmp(tmpbuf, "dumphdmireg", 11)==0){
        unsigned char reg_val = 0;
        unsigned int reg_adr = 0;
        for (reg_adr = 0; reg_adr < 0x800; reg_adr ++){        //HDMI Regs address range: 0 ~ 0x7ff
            reg_val = hdmi_rd_reg(reg_adr);
            hdmi_print(INF, "HDMI[0x%x]: 0x%x\n", reg_adr, reg_val);
        }
        return ;
    }
    else if(strncmp(tmpbuf, "dumpcecreg",10) == 0){
        unsigned char cec_val = 0;
        unsigned int cec_adr =0;
        //HDMI CEC Regs address range:0xc000~0xc01c;0xc080~0xc094
        for(cec_adr = 0xc000; cec_adr < 0xc01d; cec_adr ++){
            cec_val = hdmi_rd_reg(cec_adr);
            hdmi_print(INF, "HDMI CEC Regs[0x%x]: 0x%x\n",cec_adr,cec_val);
        }
         for(cec_adr = 0xc080; cec_adr < 0xc095; cec_adr ++){
            cec_val = hdmi_rd_reg(cec_adr);
            hdmi_print(INF, "HDMI CEC Regs[0x%x]: 0x%x\n",cec_adr,cec_val);
        }
        return;      
    }
    else if(strncmp(tmpbuf, "dumpcbusreg", 11)==0){
        unsigned j;
        adr=simple_strtoul(tmpbuf+11, NULL, 16);  //CBUS Start addr
        value=simple_strtoul(buf+i+1, NULL, 16); //CBUS End addr
        for(j = 0 ; j < value-adr+1 ; j++){
//            printk("CBUS[0x%x]: 0x%x\n", adr+j, READ_MPEG_REG(adr+j));
            hdmi_print(INF, "CBUS[0x%x]: 0x%x\n", adr+j, aml_read_reg32(CBUS_REG_ADDR(adr+j)));
            
        }
    }
    else if(strncmp(tmpbuf, "log", 3)==0){
        if(strncmp(tmpbuf+3, "hdcp", 4)==0){
            static unsigned int i = 1;
            if(i & 1){
                hdmitx_device->log |= HDMI_LOG_HDCP;
            }
            else{
                hdmitx_device->log &= ~HDMI_LOG_HDCP;
            }
            i ++;
        }
        return ;
    }
    else if(strncmp(tmpbuf, "pllcalc", 7)==0){
        clk_measure(0xff);
        return;
    }
    else if(strncmp(tmpbuf, "hdmiaudio", 9)==0){
        value=simple_strtoul(tmpbuf+9, NULL, 16);
        if(value == 1){
            hdmi_audio_off_flag = 0;
            hdmi_audio_init(i2s_to_spdif_flag);
        }
        else if(value == 0){
            hdmi_wr_reg(TX_AUDIO_PACK, 0x00); // disable audio sample packets
        }
        return;
    }
    else if(strncmp(tmpbuf, "cfgreg", 6)==0){
        adr=simple_strtoul(tmpbuf+6, NULL, 16);
        value=simple_strtoul(buf+i+1, NULL, 16);
        hdmitx_config_tvenc_reg(hdmitx_device->cur_VIC, adr, value);
        return;
    }
    else if(strncmp(tmpbuf, "tvenc_flag", 10)==0){
        use_tvenc_conf_flag = tmpbuf[10]-'0';
        hdmi_print(INF, "set use_tvenc_conf_flag = %d\n", use_tvenc_conf_flag);
    }
    else if(strncmp(tmpbuf, "reset", 5)==0){
        if(tmpbuf[5]=='0')
            new_reset_sequence_flag=0;
        else 
            new_reset_sequence_flag=1;
        return;
    }
    else if(strncmp(tmpbuf, "delay_flag", 10)==0){
        delay_flag = tmpbuf[10]-'0';    
    }
    else if(tmpbuf[0]=='v'){
        hdmitx_print_info(hdmitx_device, 1);
        return;    
    }
    else if(tmpbuf[0]=='s'){
        serial_reg_val=simple_strtoul(tmpbuf+1,NULL,16);
        return;
    }
    else if(tmpbuf[0]=='c'){
        if(tmpbuf[1]=='d'){
            color_depth_f=simple_strtoul(tmpbuf+2,NULL,10);
            if((color_depth_f!=24)&&(color_depth_f!=30)&&(color_depth_f!=36)){
                printk("Color depth %d is not supported\n", color_depth_f);
                color_depth_f=0;
            }
            return;
        }
        else if(tmpbuf[1]=='s'){
            color_space_f=simple_strtoul(tmpbuf+2,NULL,10);
            if(color_space_f>2){
                printk("Color space %d is not supported\n", color_space_f);
                color_space_f=0;
            }
        }
    }
    else if(strncmp(tmpbuf,"i2s",2)==0){
        if(strncmp(tmpbuf+3,"off",3)==0)
            i2s_to_spdif_flag=1;
        else   
            i2s_to_spdif_flag=0;
    }
    else if(strncmp(tmpbuf, "pattern_on", 10)==0){
        turn_on_shift_pattern();
        hdmi_print(INF, "Shift Pattern On\n");
        return;        
    }
    else if(strncmp(tmpbuf, "pattern_off", 11)==0){
        turn_off_shift_pattern();
        hdmi_print(INF, "Shift Pattern Off\n");
        return;        
    }
    else if(strncmp(tmpbuf, "prbs", 4)==0){
        int prbs_mode =simple_strtoul(tmpbuf+4, NULL, 10);
        turn_on_prbs_mode(prbs_mode);
        return;
    }
    else if(tmpbuf[0]=='w'){
        unsigned read_back = 0;
        adr=simple_strtoul(tmpbuf+2, NULL, 16);
        value=simple_strtoul(buf+i+1, NULL, 16);
        if(buf[1]=='h'){
            hdmi_wr_reg(adr, value);
            read_back = hdmi_rd_reg(adr);
        }
        else if(buf[1]=='c'){
            aml_write_reg32(CBUS_REG_ADDR(adr), value);
            read_back = aml_read_reg32(CBUS_REG_ADDR(adr));
            
        }
        else if(buf[1]=='p'){
            aml_write_reg32(APB_REG_ADDR(adr), value);
            read_back = aml_read_reg32(APB_REG_ADDR(adr));
        }
        hdmi_print(INF, "write %x to %s reg[%x]\n",value,buf[1]=='p'?"APB":(buf[1]=='h'?"HDMI":"CBUS"), adr);
        //Add read back function in order to judge writting is OK or NG.
        hdmi_print(INF, "Read Back %s reg[%x]=%x\n",buf[1]=='p'?"APB":(buf[1]=='h'?"HDMI":"CBUS"), adr, read_back);
    }
    else if(tmpbuf[0]=='r'){
        adr=simple_strtoul(tmpbuf+2, NULL, 16);
        if(buf[1]=='h'){
            value = hdmi_rd_reg(adr);
            
        }
        else if(buf[1]=='c'){
            value = aml_read_reg32(CBUS_REG_ADDR(adr));
        }
        else if(buf[1]=='p'){
            value = aml_read_reg32(APB_REG_ADDR(adr));
        }
        hdmi_print(INF, "%s reg[%x]=%x\n",buf[1]=='p'?"APB":(buf[1]=='h'?"HDMI":"CBUS"), adr, value);
    }
}

static void hdmitx_getediddata(hdmitx_dev_t* hdmitx_device, unsigned int blk_idx)
{
    int ii, jj;

#ifdef CONFIG_PANEL_IT6681
    hdmitx_device->cur_edid_block = 0;
    it6681_read_edid(0, &hdmitx_device->EDID_buf[hdmitx_device->cur_edid_block*128], 256);
    hdmitx_device->cur_edid_block += 2;
    printk("%s: get edid from MHL buffer(cur_edid_block=%d)\n", __func__, hdmitx_device->cur_edid_block);
#else
    if((hdmitx_device->cur_edid_block+2)<=EDID_MAX_BLOCK){
        if(blk_idx == 0) {
            for(jj=0;jj<2;jj++){
                for(ii=0;ii<128;ii++){
                    hdmitx_device->EDID_buf[hdmitx_device->cur_edid_block*128+ii]
                        =hdmi_rd_reg(TX_RX_EDID_OFFSET+hdmitx_device->cur_phy_block_ptr*128+ii);
                }
                hdmitx_device->cur_edid_block++;
                hdmitx_device->cur_phy_block_ptr++;
                hdmitx_device->cur_phy_block_ptr=hdmitx_device->cur_phy_block_ptr&0x3;
            }
        }
        else {
            for(jj=0;jj<2;jj++){
                for(ii=0;ii<128;ii++){
                    hdmitx_device->EDID_buf1[hdmitx_device->cur_edid_block*128+ii]
                        =hdmi_rd_reg(TX_RX_EDID_OFFSET+hdmitx_device->cur_phy_block_ptr*128+ii);
                }
                hdmitx_device->cur_edid_block++;
                hdmitx_device->cur_phy_block_ptr++;
                hdmitx_device->cur_phy_block_ptr=hdmitx_device->cur_phy_block_ptr&0x3;
            }
        }
    }
#endif
}    

static int hdmitx_cntl_ddc(hdmitx_dev_t* hdmitx_device, unsigned cmd, unsigned argv)
{
    int i = 0;
    unsigned char *tmp_char = NULL;
    
    if(!(cmd & CMD_DDC_OFFSET))
        hdmi_print(ERR, "ddc: " "w: invalid cmd 0x%x\n", cmd);
    else
        hdmi_print(LOW, "ddc: " "cmd 0x%x\n", cmd);

    switch(cmd) {
    case DDC_RESET_EDID:
        hdmi_set_reg_bits(TX_HDCP_EDID_CONFIG, 0, 6, 1);
        hdmi_set_reg_bits(TX_SYS5_TX_SOFT_RESET_2, 1, 1, 1);
        hdmi_set_reg_bits(TX_SYS5_TX_SOFT_RESET_2, 0, 1, 1);
        break;
    case DDC_IS_EDID_DATA_READY:
        return !!(hdmi_rd_reg(TX_HDCP_ST_EDID_STATUS) & (1<<4));
        break;
    case DDC_EDID_READ_DATA:
        hdmi_set_reg_bits(TX_HDCP_EDID_CONFIG, 1, 6, 1);     // Assert sys_trigger_config
        break;
    case DDC_EDID_GET_DATA:
        hdmitx_getediddata(hdmitx_device, argv);
        break;
    case DDC_PIN_MUX_OP:
        if(argv == PIN_MUX) {
            aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 0x3, 24, 2);    // Mux HDMI SDA(5v)/SCL(5V)
        }
        if(argv == PIN_UNMUX) {
            aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 0x0, 24, 2);    // unMux HDMI SDA(5v)/SCL(5V)
        }
        break;
    case DDC_EDID_CLEAR_RAM:
        for(i = 0; i < EDID_RAM_ADDR_SIZE; i++) {
            hdmi_wr_reg(TX_RX_EDID_OFFSET + i, 0);
        }
        break;
    case DDC_RESET_HDCP:
        hdmi_set_reg_bits(TX_SYS5_TX_SOFT_RESET_2, 1, 2, 1);
        hdmi_set_reg_bits(TX_SYS5_TX_SOFT_RESET_2, 0, 2, 1);
        break;
    case DDC_HDCP_OP:
        if(argv == HDCP_ON) {
#ifdef CONFIG_AML_HDMI_TX_HDCP
            // check if bit7 is enable, if not, enable first
            if(hdmi_rd_reg(TX_PACKET_CONTROL_1) & (1 << 7)) {
                hdmi_print(IMP, SYS "already configure PACKET_CONTROL\n");// do nothing
            }
            else {
                hdmi_reconfig_packet_setting(hdmitx_device->cur_VIC);
            }
#endif
            hdmi_set_reg_bits(TX_HDCP_MODE, 1, 7, 1);
        }
        if(argv == HDCP_OFF) {
            hdmi_set_reg_bits(TX_HDCP_MODE, 0, 7, 1);
        }
        break;
    case DDC_IS_HDCP_ON:
        argv = !!((hdmi_rd_reg(TX_HDCP_MODE)) & (1 << 7));
        break;
    case DDC_HDCP_GET_AKSV:
        tmp_char = (unsigned char *) argv;
        for(i = 0; i < 5; i++) {
            tmp_char[i] = (unsigned char)hdmi_rd_reg(TX_HDCP_AKSV_SHADOW + 4 - i);
        }
        break;
    case DDC_HDCP_GET_BKSV:
        tmp_char = (unsigned char *) argv;
        for(i = 0; i < 5; i++) {
            tmp_char[i] = (unsigned char)hdmi_rd_reg(TX_HDCP_BKSV_SHADOW + 4 - i);
        }
        break;
    case DDC_HDCP_GET_AUTH:
        if((hdmi_rd_reg(TX_HDCP_ST_STATUS_3)&0xa0)==0xa0) {
            return 1;
        }
        else {
            return 0;
        }
        break;
    default:
        hdmi_print(INF, "ddc: " "unknown cmd: 0x%x\n", cmd);
    }
    return 1;
}

// clear hdmi packet configure registers
static void hdmitx_clr_sub_packet(unsigned int reg_base)
{
    int i = 0;
    for(i = 0; i < 0x20; i++) {
        hdmi_wr_reg(reg_base + i, 0x00);
    }
}

static int hdmitx_cntl_config(hdmitx_dev_t* hdmitx_device, unsigned cmd, unsigned argv)
{
    if(!(cmd & CMD_CONF_OFFSET))
        hdmi_print(ERR, "config: " "hdmitx: w: invalid cmd 0x%x\n", cmd);
    else
        hdmi_print(LOW, "config: " "hdmitx: conf cmd 0x%x\n", cmd);

    switch(cmd) {
    case CONF_HDMI_DVI_MODE:
        if(argv == HDMI_MODE) {
            hdmi_set_reg_bits(TX_TMDS_MODE, 0x3, 6, 2);
        }
        if(argv == DVI_MODE) {
            hdmi_set_reg_bits(TX_VIDEO_DTV_OPTION_L, 0x0, 6, 2);
            hdmi_set_reg_bits(TX_TMDS_MODE, 0x2, 6, 2);
        }
        break;
    case CONF_SYSTEM_ST:
        return (hdmi_rd_reg(TX_HDCP_ST_EDID_STATUS) >> 6);
        break;
    case CONF_AUDIO_MUTE_OP:
        if(argv == AUDIO_MUTE) {
            hdmi_wr_reg(TX_AUDIO_PACK, 0x00); // disable audio sample packets
            //hdmi_wr_reg(TX_PACKET_CONTROL_2, hdmi_rd_reg(TX_PACKET_CONTROL_2) | (1<<3));
        }
        if((argv == AUDIO_UNMUTE) && (hdmitx_device->tx_aud_cfg != 0)) {
            hdmi_wr_reg(TX_AUDIO_PACK, 0x01);
            //hdmitx_device->audio_param_update_flag = 1;
        }
        break;
    case CONF_VIDEO_BLANK_OP:
        if(argv == VIDEO_BLANK) {
            aml_write_reg32(P_VPU_HDMI_DATA_OVR, (0x200 << 20) | (0x0 << 10) | (0x200 << 0));   // set blank CrYCb as 0x200 0x0 0x200
            aml_set_reg32_bits(P_VPU_HDMI_SETTING, 0, 5, 3);        // Output data map: CrYCb
            aml_set_reg32_bits(P_VPU_HDMI_DATA_OVR, 1, 31, 1);      // Enable HDMI data override
        }
        if(argv == VIDEO_UNBLANK) {
            aml_write_reg32(P_VPU_HDMI_DATA_OVR, 0);    // Disable HDMI data override
        }
        break;
    case CONF_CLR_AVI_PACKET:
        hdmitx_clr_sub_packet(TX_PKT_REG_AVI_INFO_BASE_ADDR);
        break;
    case CONF_CLR_VSDB_PACKET:
        hdmitx_clr_sub_packet(TX_PKT_REG_VEND_INFO_BASE_ADDR);
        break;
    case CONF_CLR_AUDINFO_PACKET:
        hdmitx_clr_sub_packet(TX_PKT_REG_AUDIO_INFO_BASE_ADDR);
        break;
    default:
        hdmi_print(ERR, "config: ""hdmitx: unknown cmd: 0x%x\n", cmd);
    }
    return 1;
}

static int hdmitx_cntl_misc(hdmitx_dev_t* hdmitx_device, unsigned cmd, unsigned argv)
{
    if(!(cmd & CMD_MISC_OFFSET))
        hdmi_print(ERR, "misc: " "hdmitx: w: invalid cmd 0x%x\n", cmd);
    else
        hdmi_print(LOW, "misc: " "hdmitx: misc cmd 0x%x\n", cmd);

    switch(cmd) {
    case MISC_HPD_MUX_OP:
        if(argv == PIN_MUX) {
            aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 0x1, 26, 1);    // Mux HPD
        }
        if(argv == PIN_UNMUX) {
            aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 0x0, 26, 1);    // unMux HPD
        }
        break;
    case MISC_HPD_GPI_ST:
        return read_hpd_gpio();
        break;
    case MISC_HPLL_OP:
        if(argv == HPLL_ENABLE) {
            aml_set_reg32_bits(P_HHI_VID_PLL_CNTL, 1, 30, 1);   // disable hpll
        }
        if(argv == HPLL_DISABLE) {
            aml_set_reg32_bits(P_HHI_VID_PLL_CNTL, 0, 30, 1);   // disable hpll
        }
        break;
    case MISC_TMDS_PHY_OP:
        if(argv == TMDS_PHY_ENABLE) {
            hdmi_phy_wakeup(hdmitx_device);  // TODO
        }
        if(argv == TMDS_PHY_DISABLE) {
            hdmi_phy_suspend();
        }
        break;
    case MISC_VIID_IS_USING:
        return !!(aml_read_reg32(P_HHI_VID2_PLL_CNTL) & (1 << 30));     // bit30: enable
        break;
    default:
        hdmi_print(ERR, "misc: " "hdmitx: unknown cmd: 0x%x\n", cmd);
    }
    return 1;
}

static int hdmitx_get_state(hdmitx_dev_t* hdmitx_device, unsigned cmd, unsigned argv)
{
    if(!(cmd & CMD_STAT_OFFSET))
        hdmi_print(ERR, "stat: " "hdmitx: w: invalid cmd 0x%x\n", cmd);
    else
        hdmi_print(LOW, "stat: " "hdmitx: misc cmd 0x%x\n", cmd);

    switch(cmd) {
    case STAT_VIDEO_VIC:
        {
            /*
             * get current video vic directly from VIC packet or VSDB packet
             */
            HDMI_Video_Codes_t vic = HDMI_Unkown;
            vic = (HDMI_Video_Codes_t)hdmi_rd_reg(TX_PKT_REG_AVI_INFO_BASE_ADDR + 4);
            if(vic == HDMI_Unkown) {
                vic = (HDMI_Video_Codes_t)hdmi_rd_reg(TX_PKT_REG_VEND_INFO_BASE_ADDR + 5);
                if(vic == 1) {
                    vic = HDMI_4k2k_30;
                } else if(vic == 2) {
                    vic = HDMI_4k2k_25;
                } else if(vic == 3) {
                    vic = HDMI_4k2k_24;
                } else if(vic == 4) {
                    vic = HDMI_4k2k_smpte_24;
                } else {
                }
            }
            return (int)vic;
        }
        break;
    case STAT_VIDEO_CLK:
        break;
    default:
        break;
    }
    return 0;
}

void HDMITX_Meson_Init(hdmitx_dev_t* hdmitx_device)
{
    hdmitx_device->HWOp.SetPacket = hdmitx_set_packet;
    hdmitx_device->HWOp.SetAudioInfoFrame = hdmitx_setaudioinfoframe;
    hdmitx_device->HWOp.SetDispMode = hdmitx_set_dispmode;
    hdmitx_device->HWOp.SetAudMode = hdmitx_set_audmode;
    hdmitx_device->HWOp.SetupIRQ = hdmitx_setupirq;
    hdmitx_device->HWOp.DebugFun = hdmitx_debug;
    hdmitx_device->HWOp.UnInit = hdmitx_uninit;
    hdmitx_device->HWOp.Cntl = hdmitx_cntl;             // todo
    hdmitx_device->HWOp.CntlDDC = hdmitx_cntl_ddc;
    hdmitx_device->HWOp.GetState = hdmitx_get_state;
    hdmitx_device->HWOp.CntlPacket = hdmitx_cntl;
    hdmitx_device->HWOp.CntlConfig = hdmitx_cntl_config;
    hdmitx_device->HWOp.CntlMisc = hdmitx_cntl_misc;
                                                                  //     1=Map data pins from Venc to Hdmi Tx as RGB mode.
    // --------------------------------------------------------
    // Configure HDMI TX analog, and use HDMI PLL to generate TMDS clock
    // --------------------------------------------------------
    // Enable APB3 fail on error
//    WRITE_APB_REG(HDMI_CNTL_PORT, READ_APB_REG(HDMI_CNTL_PORT)|(1<<15)); //APB3 err_en
//\\ TODO
    aml_set_reg32_bits(P_PAD_PULL_UP_EN_REG1, 0, 16, 1);       // Disable GPIOH_0 internal pull-up register
    aml_write_reg32(P_HHI_HDMI_CLK_CNTL, aml_read_reg32(P_HHI_HDMI_CLK_CNTL)| (1 << 8));
    aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)|(1<<15)); //APB3 err_en
    hdmi_wr_reg(0x10, 0xff);

    /**/    
    hdmi_hw_init(hdmitx_device);
}    

void hdmi_set_audio_para(int para)
{
    aud_para = para;

}

// The following two functions should move to 
// static struct platform_driver amhdmitx_driver.suspend & .wakeup
// For tempelet use only.
// Later will change it.
typedef struct 
{
    unsigned long reg;
    unsigned long val_sleep;
    unsigned long val_save;
}hdmi_phy_t;

static unsigned int hdmi_phy_save = 0x08930e9b;     // Default setting

static void hdmi_phy_suspend(void)
{
    hdmi_phy_save = aml_read_reg32(P_HHI_HDMI_PHY_CNTL0);
    aml_write_reg32(P_HHI_HDMI_PHY_CNTL0, 0x08418d00);
    //hdmi_print(INF, SYS "phy suspend\n");
}

static void hdmi_phy_wakeup(hdmitx_dev_t* hdmitx_device)
{
    hdmitx_set_phy(hdmitx_device);
    //hdmi_print(INF, SYS "phy wakeup\n");
}
