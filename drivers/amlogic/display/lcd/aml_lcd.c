/*
 * Amlogic LCD Driver
 *
 * Author:
 *
 * Copyright (C) 2012 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/amlogic/vout/lcd.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/amlogic/vout/lcd_aml.h>

#include <linux/kernel.h>
#include <linux/interrupt.h>
//#include <linux/logo/logo.h>
#include <mach/am_regs.h>
#include <mach/mlvds_regs.h>
#include <mach/clock.h>
#include <asm/fiq.h>
#include <linux/delay.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <linux/gpio.h>
#include <linux/amlogic/panel.h>

#include <linux/of.h>

extern unsigned int clk_util_clk_msr(unsigned int clk_mux);

static struct class *aml_lcd_clsp;
#define CLOCK_ENABLE_DELAY  500
#define CLOCK_DISABLE_DELAY  50

#define PWM_ENABLE_DELAY     20
#define PWM_DISABLE_DELAY    20

#define PANEL_POWER_ON_DELAY   50
#define PANEL_POWER_OFF_DELAY   0

#define BACKLIGHT_POWER_ON_DELAY  0
#define BACKLIGHT_POWER_OFF_DELAY  200

#define H_ACTIVE        1920
#define V_ACTIVE        1080
#define H_PERIOD        2200
#define V_PERIOD        1125
#define VIDEO_ON_PIXEL      148
#define VIDEO_ON_LINE       41


#define LVDS_blank_data_reserved	30	// 31:30
#define LVDS_blank_data_r		20	// 29:20
#define LVDS_blank_data_g		10	// 19:10
#define LVDS_blank_data_b		0	//  9:0
#define LVDS_USE_TCON			7
#define LVDS_DUAL			6
#define PN_SWP				5
#define LSB_FIRST			4
#define LVDS_RESV			3
#define ODD_EVEN_SWP			2
#define LVDS_REPACK			0


void panel_power_on(void)
{
	//gpio_set_status(PAD_GPIOZ_5, gpio_status_out);
	//gpio_out(PAD_GPIOZ_5, 0);
	//gpio_direction_output(PAD_GPIOZ_5, 1);
	pr_info("%s\n", __func__);
	printk("\n\n panel_power_on %d",__LINE__);
}
void panel_power_off(void)
{
	//gpio_set_status(PAD_GPIOZ_5, gpio_status_out);
	//gpio_out(PAD_GPIOZ_5, 1);
	//gpio_direction_output(PAD_GPIOZ_5, 0);
	pr_info("%s\n", __func__);
}


//M6 PLL control value
#define M6_PLL_CNTL_CST2 (0x814d3928)
#define M6_PLL_CNTL_CST3 (0x6b425012)
#define M6_PLL_CNTL_CST4 (0x110)

//VID PLL
#define M6_VID_PLL_CNTL_2 (M6_PLL_CNTL_CST2)
#define M6_VID_PLL_CNTL_3 (M6_PLL_CNTL_CST3)
#define M6_VID_PLL_CNTL_4 (M6_PLL_CNTL_CST4)

#define FIQ_VSYNC

#define BL_MAX_LEVEL 0x100
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
#define NO_ENCT
#define NO_2ND_PLL
#endif

//#define PRINT_DEBUG_INFO
#ifdef PRINT_DEBUG_INFO
#define PRINT_INFO(...)        printk(__VA_ARGS__)
#else
#define PRINT_INFO(...)
#endif

typedef struct {
    Lcd_Config_t conf;
    vinfo_t lcd_info;
} lcd_dev_t;
//static unsigned long ddr_pll_clk = 0;

static lcd_dev_t *pDev = NULL;

static int bit_num = 1;
static int pn_swap = 0;
static int dual_port = 1;
static int lvds_repack = 1;
static int port_reverse = 1;
static int bit_num_flag = 1;
static int lvds_repack_flag = 1;
static int port_reverse_flag = 1;

int flaga = 0;
int flagb = 0;
int flagc = 0;
int flagd = 0;

static int cur_lvds_index = 0;

static void _lcd_init(Lcd_Config_t *pConf) ;

static void write_tcon_double(Mlvds_Tcon_Config_t *mlvds_tcon)
{
    unsigned int tmp;
    int channel_num = mlvds_tcon->channel_num;
    int hv_sel = mlvds_tcon->hv_sel;
    int hstart_1 = mlvds_tcon->tcon_1st_hs_addr;
    int hend_1 = mlvds_tcon->tcon_1st_he_addr;
    int vstart_1 = mlvds_tcon->tcon_1st_vs_addr;
    int vend_1 = mlvds_tcon->tcon_1st_ve_addr;
    int hstart_2 = mlvds_tcon->tcon_2nd_hs_addr;
    int hend_2 = mlvds_tcon->tcon_2nd_he_addr;
    int vstart_2 = mlvds_tcon->tcon_2nd_vs_addr;
    int vend_2 = mlvds_tcon->tcon_2nd_ve_addr;

    tmp = aml_read_reg32(P_L_TCON_MISC_SEL_ADDR);
    switch(channel_num)
    {
        case 0 :
            aml_write_reg32(P_MTCON0_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON0_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON0_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON0_1ST_VE_ADDR, vend_1);
            aml_write_reg32(P_MTCON0_2ND_HS_ADDR, hstart_2);
            aml_write_reg32(P_MTCON0_2ND_HE_ADDR, hend_2);
            aml_write_reg32(P_MTCON0_2ND_VS_ADDR, vstart_2);
            aml_write_reg32(P_MTCON0_2ND_VE_ADDR, vend_2);
            tmp &= (~(1 << LCD_STH1_SEL)) | (hv_sel << LCD_STH1_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 1 :
            aml_write_reg32(P_MTCON1_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON1_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON1_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON1_1ST_VE_ADDR, vend_1);
            aml_write_reg32(P_MTCON1_2ND_HS_ADDR, hstart_2);
            aml_write_reg32(P_MTCON1_2ND_HE_ADDR, hend_2);
            aml_write_reg32(P_MTCON1_2ND_VS_ADDR, vstart_2);
            aml_write_reg32(P_MTCON1_2ND_VE_ADDR, vend_2);
            tmp &= (~(1 << LCD_CPV1_SEL)) | (hv_sel << LCD_CPV1_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 2 :
            aml_write_reg32(P_MTCON2_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON2_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON2_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON2_1ST_VE_ADDR, vend_1);
            aml_write_reg32(P_MTCON2_2ND_HS_ADDR, hstart_2);
            aml_write_reg32(P_MTCON2_2ND_HE_ADDR, hend_2);
            aml_write_reg32(P_MTCON2_2ND_VS_ADDR, vstart_2);
            aml_write_reg32(P_MTCON2_2ND_VE_ADDR, vend_2);
            tmp &= (~(1 << LCD_STV1_SEL)) | (hv_sel << LCD_STV1_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 3 :
            aml_write_reg32(P_MTCON3_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON3_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON3_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON3_1ST_VE_ADDR, vend_1);
            aml_write_reg32(P_MTCON3_2ND_HS_ADDR, hstart_2);
            aml_write_reg32(P_MTCON3_2ND_HE_ADDR, hend_2);
            aml_write_reg32(P_MTCON3_2ND_VS_ADDR, vstart_2);
            aml_write_reg32(P_MTCON3_2ND_VE_ADDR, vend_2);
            tmp &= (~(1 << LCD_OEV1_SEL)) | (hv_sel << LCD_OEV1_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 4 :
            aml_write_reg32(P_MTCON4_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON4_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON4_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON4_1ST_VE_ADDR, vend_1);
            tmp &= (~(1 << LCD_STH2_SEL)) | (hv_sel << LCD_STH2_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 5 :
            aml_write_reg32(P_MTCON5_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON5_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON5_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON5_1ST_VE_ADDR, vend_1);
            tmp &= (~(1 << LCD_CPV2_SEL)) | (hv_sel << LCD_CPV2_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 6 :
            aml_write_reg32(P_MTCON6_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON6_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON6_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON6_1ST_VE_ADDR, vend_1);
            tmp &= (~(1 << LCD_OEH_SEL)) | (hv_sel << LCD_OEH_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        case 7 :
            aml_write_reg32(P_MTCON7_1ST_HS_ADDR, hstart_1);
            aml_write_reg32(P_MTCON7_1ST_HE_ADDR, hend_1);
            aml_write_reg32(P_MTCON7_1ST_VS_ADDR, vstart_1);
            aml_write_reg32(P_MTCON7_1ST_VE_ADDR, vend_1);
            tmp &= (~(1 << LCD_OEV3_SEL)) | (hv_sel << LCD_OEV3_SEL);
            aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, tmp);
            break;
        default:
            break;
    }
}

void vpp_set_matrix_ycbcr2rgb (int vd1_or_vd2_or_post, int mode)
{
   if (vd1_or_vd2_or_post == 0) //vd1
   {
      aml_set_reg32_bits (P_VPP_MATRIX_CTRL, 1, 5, 1);
      aml_set_reg32_bits (P_VPP_MATRIX_CTRL, 1, 8, 2);
   }
   else if (vd1_or_vd2_or_post == 1) //vd2
   {
      aml_set_reg32_bits (P_VPP_MATRIX_CTRL, 1, 4, 1);
      aml_set_reg32_bits (P_VPP_MATRIX_CTRL, 2, 8, 2);
   }
   else
   {
      aml_set_reg32_bits (P_VPP_MATRIX_CTRL, 1, 0, 1);
      aml_set_reg32_bits (P_VPP_MATRIX_CTRL, 0, 8, 2);
      if (mode == 0)
      {
        aml_set_reg32_bits(P_VPP_MATRIX_CTRL, 1, 1, 2);
      }
      else if (mode == 1)
      {
        aml_set_reg32_bits(P_VPP_MATRIX_CTRL, 0, 1, 2);
      }
   }

   if (mode == 0) //ycbcr not full range, 601 conversion
   {
        aml_write_reg32(P_VPP_MATRIX_PRE_OFFSET0_1, 0x7c00600);
        aml_write_reg32(P_VPP_MATRIX_PRE_OFFSET2, 0x0600);

        //1.164     0       1.596
        //1.164   -0.392    -0.813
        //1.164   2.017     0
        aml_write_reg32(P_VPP_MATRIX_COEF00_01, (0x4a8 << 16) |
                            0);
        aml_write_reg32(P_VPP_MATRIX_COEF02_10, (0x662 << 16) |
                            0x4a8);
        aml_write_reg32(P_VPP_MATRIX_COEF11_12, (0x1e6f << 16) |
                            0x1cbf);
        aml_write_reg32(P_VPP_MATRIX_COEF20_21, (0x4a8 << 16) |
                            0x811);
        aml_write_reg32(P_VPP_MATRIX_COEF22, 0x0);
        aml_write_reg32(P_VPP_MATRIX_OFFSET0_1, 0x0);
        aml_write_reg32(P_VPP_MATRIX_OFFSET2, 0x0);
   }
   else if (mode == 1) //ycbcr full range, 601 conversion
   {

        aml_write_reg32(P_VPP_MATRIX_PRE_OFFSET0_1, 0x0000600);
        aml_write_reg32(P_VPP_MATRIX_PRE_OFFSET2, 0x0600);

        //1     0       1.402
        //1   -0.34414  -0.71414
        //1   1.772     0
        aml_write_reg32(P_VPP_MATRIX_COEF00_01, (0x400 << 16) |
                            0);
        aml_write_reg32(P_VPP_MATRIX_COEF02_10, (0x59c << 16) |
                            0x400);
        aml_write_reg32(P_VPP_MATRIX_COEF11_12, (0x1ea0 << 16) |
                            0x1d25);
        aml_write_reg32(P_VPP_MATRIX_COEF20_21, (0x400 << 16) |
                            0x717);
        aml_write_reg32(P_VPP_MATRIX_COEF22, 0x0);
        aml_write_reg32(P_VPP_MATRIX_OFFSET0_1, 0x0);
        aml_write_reg32(P_VPP_MATRIX_OFFSET2, 0x0);
   }
}

static void set_tcon_ttl(Lcd_Config_t *pConf)
{
    Lcd_Timing_t *tcon_adr = &(pConf->lcd_timing);

    //set_lcd_gamma_table_ttl(pConf->lcd_effect.GammaTableR, LCD_H_SEL_R);
    //set_lcd_gamma_table_ttl(pConf->lcd_effect.GammaTableG, LCD_H_SEL_G);
    //set_lcd_gamma_table_ttl(pConf->lcd_effect.GammaTableB, LCD_H_SEL_B);

    //aml_write_reg32(P_GAMMA_CNTL_PORT, pConf->lcd_effect.gamma_cntl_port);
    //aml_write_reg32(P_GAMMA_VCOM_HSWITCH_ADDR, pConf->lcd_effect.gamma_vcom_hswitch_addr);

    //aml_write_reg32(P_RGB_BASE_ADDR,   pConf->lcd_effect.rgb_base_addr);
    //aml_write_reg32(P_RGB_COEFF_ADDR,  pConf->lcd_effect.rgb_coeff_addr);
    //aml_write_reg32(P_POL_CNTL_ADDR,   pConf->lcd_timing.pol_cntl_addr);
    if(pConf->lcd_basic.lcd_bits == 8)
		aml_write_reg32(P_DITH_CNTL_ADDR,  0x400);
	else
		aml_write_reg32(P_DITH_CNTL_ADDR,  0x600);

    aml_write_reg32(P_STH1_HS_ADDR,    tcon_adr->sth1_hs_addr);
    aml_write_reg32(P_STH1_HE_ADDR,    tcon_adr->sth1_he_addr);
    aml_write_reg32(P_STH1_VS_ADDR,    tcon_adr->sth1_vs_addr);
    aml_write_reg32(P_STH1_VE_ADDR,    tcon_adr->sth1_ve_addr);

    aml_write_reg32(P_OEH_HS_ADDR,     tcon_adr->oeh_hs_addr);
    aml_write_reg32(P_OEH_HE_ADDR,     tcon_adr->oeh_he_addr);
    aml_write_reg32(P_OEH_VS_ADDR,     tcon_adr->oeh_vs_addr);
    aml_write_reg32(P_OEH_VE_ADDR,     tcon_adr->oeh_ve_addr);

    aml_write_reg32(P_VCOM_HSWITCH_ADDR, tcon_adr->vcom_hswitch_addr);
    aml_write_reg32(P_VCOM_VS_ADDR,    tcon_adr->vcom_vs_addr);
    aml_write_reg32(P_VCOM_VE_ADDR,    tcon_adr->vcom_ve_addr);

    aml_write_reg32(P_CPV1_HS_ADDR,    tcon_adr->cpv1_hs_addr);
    aml_write_reg32(P_CPV1_HE_ADDR,    tcon_adr->cpv1_he_addr);
    aml_write_reg32(P_CPV1_VS_ADDR,    tcon_adr->cpv1_vs_addr);
    aml_write_reg32(P_CPV1_VE_ADDR,    tcon_adr->cpv1_ve_addr);

    aml_write_reg32(P_STV1_HS_ADDR,    tcon_adr->stv1_hs_addr);
    aml_write_reg32(P_STV1_HE_ADDR,    tcon_adr->stv1_he_addr);
    aml_write_reg32(P_STV1_VS_ADDR,    tcon_adr->stv1_vs_addr);
    aml_write_reg32(P_STV1_VE_ADDR,    tcon_adr->stv1_ve_addr);

    aml_write_reg32(P_OEV1_HS_ADDR,    tcon_adr->oev1_hs_addr);
    aml_write_reg32(P_OEV1_HE_ADDR,    tcon_adr->oev1_he_addr);
    aml_write_reg32(P_OEV1_VS_ADDR,    tcon_adr->oev1_vs_addr);
    aml_write_reg32(P_OEV1_VE_ADDR,    tcon_adr->oev1_ve_addr);

    aml_write_reg32(P_INV_CNT_ADDR,    tcon_adr->inv_cnt_addr);
    aml_write_reg32(P_TCON_MISC_SEL_ADDR,     tcon_adr->tcon_misc_sel_addr);
    aml_write_reg32(P_DUAL_PORT_CNTL_ADDR, tcon_adr->dual_port_cntl_addr);

    aml_write_reg32(P_VPP_MISC, aml_read_reg32(P_VPP_MISC) & ~(VPP_OUT_SATURATE));
}

static void set_tcon_lvds(Lcd_Config_t *pConf)
{
    //Lcd_Timing_t *tcon_adr = &(pConf->lcd_timing);

    vpp_set_matrix_ycbcr2rgb(2, 0);
    aml_write_reg32(P_ENCL_VIDEO_RGBIN_CTRL, 3);
    aml_write_reg32(P_L_RGB_BASE_ADDR, 0);
    aml_write_reg32(P_L_RGB_COEFF_ADDR, 0x400);
	if(pConf->lcd_basic.lcd_bits == 8)
		aml_write_reg32(P_L_DITH_CNTL_ADDR,  0x400);
	else if(pConf->lcd_basic.lcd_bits == 6)
		aml_write_reg32(P_L_DITH_CNTL_ADDR,  0x600);
	else
		aml_write_reg32(P_L_DITH_CNTL_ADDR,  0);
    //PRINT_INFO("final LVDS_FIFO_CLK = %d\n", clk_util_clk_msr(24));
	//PRINT_INFO("final cts_encl_clk = %d\n", clk_util_clk_msr(9));
    aml_write_reg32(P_VPP_MISC, aml_read_reg32(P_VPP_MISC) & ~(VPP_OUT_SATURATE));
}

// Set the mlvds TCON
// this function should support dual gate or singal gate TCON setting.
// singal gate TCON, Scan Function TO DO.
// scan_function   // 0 - Z1, 1 - Z2, 2- Gong
static void set_tcon_mlvds(Lcd_Config_t *pConf)
{
    Mlvds_Tcon_Config_t *mlvds_tconfig_l = pConf->lvds_mlvds_config.mlvds_tcon_config;
    int dual_gate = pConf->lvds_mlvds_config.mlvds_config->test_dual_gate;
    int bit_num = pConf->lcd_basic.lcd_bits;
    int pair_num = pConf->lvds_mlvds_config.mlvds_config->test_pair_num;

    unsigned int data32;

    int pclk_div;
    int ext_pixel = dual_gate ? pConf->lvds_mlvds_config.mlvds_config->total_line_clk : 0;
    int dual_wr_rd_start;
    int i = 0;

//    PRINT_INFO(" Notice: Setting VENC_DVI_SETTING[0x%4x] and GAMMA_CNTL_PORT[0x%4x].LCD_GAMMA_EN as 0 temporary\n", VENC_DVI_SETTING, GAMMA_CNTL_PORT);
//    PRINT_INFO(" Otherwise, the panel will display color abnormal.\n");
//    aml_write_reg32(P_VENC_DVI_SETTING, 0);

    //set_lcd_gamma_table_lvds(pConf->lcd_effect.GammaTableR, LCD_H_SEL_R);
    //set_lcd_gamma_table_lvds(pConf->lcd_effect.GammaTableG, LCD_H_SEL_G);
    //set_lcd_gamma_table_lvds(pConf->lcd_effect.GammaTableB, LCD_H_SEL_B);

    //aml_write_reg32(P_L_GAMMA_CNTL_PORT, pConf->lcd_effect.gamma_cntl_port);
    //aml_write_reg32(P_L_GAMMA_VCOM_HSWITCH_ADDR, pConf->lcd_effect.gamma_vcom_hswitch_addr);

    //aml_write_reg32(P_L_RGB_BASE_ADDR, pConf->lcd_effect.rgb_base_addr);
    //aml_write_reg32(P_L_RGB_COEFF_ADDR, pConf->lcd_effect.rgb_coeff_addr);
    //aml_write_reg32(P_L_POL_CNTL_ADDR, pConf->pol_cntl_addr);
	if(pConf->lcd_basic.lcd_bits == 8)
		aml_write_reg32(P_L_DITH_CNTL_ADDR,  0x400);
	else
		aml_write_reg32(P_L_DITH_CNTL_ADDR,  0x600);

//    aml_write_reg32(P_L_INV_CNT_ADDR, pConf->inv_cnt_addr);
//    aml_write_reg32(P_L_TCON_MISC_SEL_ADDR, pConf->tcon_misc_sel_addr);
//    aml_write_reg32(P_L_DUAL_PORT_CNTL_ADDR, pConf->dual_port_cntl_addr);
//
/*
    CLEAR_MPEG_REG_MASK(VPP_MISC, VPP_OUT_SATURATE);
*/
    data32 = (0x9867 << tcon_pattern_loop_data) |
             (1 << tcon_pattern_loop_start) |
             (4 << tcon_pattern_loop_end) |
             (1 << ((mlvds_tconfig_l[6].channel_num)+tcon_pattern_enable)); // POL_CHANNEL use pattern generate

    aml_write_reg32(P_L_TCON_PATTERN_HI,  (data32 >> 16));
    aml_write_reg32(P_L_TCON_PATTERN_LO, (data32 & 0xffff));

    pclk_div = (bit_num == 8) ? 3 : // phy_clk / 8
                                2 ; // phy_clk / 6
   data32 = (1 << ((mlvds_tconfig_l[7].channel_num)-2+tcon_pclk_enable)) |  // enable PCLK_CHANNEL
            (pclk_div << tcon_pclk_div) |
            (
              (pair_num == 6) ?
              (
              ((bit_num == 8) & dual_gate) ?
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              ) :
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              )
              ) :
              (
              ((bit_num == 8) & dual_gate) ?
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              ) :
              (bit_num == 8) ?
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              ) :
              (
                (0 << (tcon_delay + 0*3)) |
                (0 << (tcon_delay + 1*3)) |
                (0 << (tcon_delay + 2*3)) |
                (0 << (tcon_delay + 3*3)) |
                (0 << (tcon_delay + 4*3)) |
                (0 << (tcon_delay + 5*3)) |
                (0 << (tcon_delay + 6*3)) |
                (0 << (tcon_delay + 7*3))
              )
              )
            );

    aml_write_reg32(P_TCON_CONTROL_HI,  (data32 >> 16));
    aml_write_reg32(P_TCON_CONTROL_LO, (data32 & 0xffff));


    aml_write_reg32(P_L_TCON_DOUBLE_CTL,
                   (1<<(mlvds_tconfig_l[3].channel_num))   // invert CPV
                  );

	// for channel 4-7, set second setting same as first
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    aml_write_reg32(P_L_DE_HS_ADDR, (0x3 << 14) | ext_pixel);   // 0x3 -- enable double_tcon fir channel7:6
    aml_write_reg32(P_L_DE_HE_ADDR, (0x3 << 14) | ext_pixel);   // 0x3 -- enable double_tcon fir channel5:4
    aml_write_reg32(P_L_DE_VS_ADDR, (0x3 << 14) | 0);	// 0x3 -- enable double_tcon fir channel3:2
    aml_write_reg32(P_L_DE_VE_ADDR, (0x3 << 14) | 0);	// 0x3 -- enable double_tcon fir channel1:0
#else
    aml_write_reg32(P_L_DE_HS_ADDR, (0xf << 12) | ext_pixel);   // 0xf -- enable double_tcon fir channel7:4
    aml_write_reg32(P_L_DE_HE_ADDR, (0xf << 12) | ext_pixel);   // 0xf -- enable double_tcon fir channel3:0
    aml_write_reg32(P_L_DE_VS_ADDR, 0);
    aml_write_reg32(P_L_DE_VE_ADDR, 0);
#endif

    dual_wr_rd_start = 0x5d;
    aml_write_reg32(P_MLVDS_DUAL_GATE_WR_START, dual_wr_rd_start);
    aml_write_reg32(P_MLVDS_DUAL_GATE_WR_END, dual_wr_rd_start + 1280);
    aml_write_reg32(P_MLVDS_DUAL_GATE_RD_START, dual_wr_rd_start + ext_pixel - 2);
    aml_write_reg32(P_MLVDS_DUAL_GATE_RD_END, dual_wr_rd_start + 1280 + ext_pixel - 2);

    aml_write_reg32(P_MLVDS_SECOND_RESET_CTL, (pConf->lvds_mlvds_config.mlvds_config->mlvds_insert_start + ext_pixel));

    data32 = (0 << ((mlvds_tconfig_l[5].channel_num)+mlvds_tcon_field_en)) |  // enable EVEN_F on TCON channel 6
             ( (0x0 << mlvds_scan_mode_odd) | (0x0 << mlvds_scan_mode_even)
             ) | (0 << mlvds_scan_mode_start_line);

    aml_write_reg32(P_MLVDS_DUAL_GATE_CTL_HI,  (data32 >> 16));
    aml_write_reg32(P_MLVDS_DUAL_GATE_CTL_LO, (data32 & 0xffff));

    PRINT_INFO("write minilvds tcon 0~7.\n");
    for(i = 0; i < 8; i++)
    {
		write_tcon_double(&mlvds_tconfig_l[i]);
    }
}

static void set_video_spread_spectrum(int video_pll_sel, int video_ss_level)
{
    if (video_pll_sel){
		switch (video_ss_level)
		{
			case 0:  // disable ss
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x814d3928 );
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x6b425012 );
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x110 );
				break;
			case 1:  //about 1%
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x4d625012);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x130);
				break;
			case 2:  //about 2%
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x2d425012);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x130);
				break;
			case 3:  //about 3%
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x1d425012);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x130);
				break;
			case 4:  //about 4%
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x0d125012);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x130);
				break;
			case 5:  //about 5%
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x0e425012);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x130);
				break;
			default:  //disable ss
				aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x814d3928);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x6b425012);
				aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x110);
		}
	}
	else{
		switch (video_ss_level)
		{
			case 0:  // disable ss
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x814d3928 );
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x6b425012 );
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x110 );
				break;
			case 1:  //about 1%
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x4d625012);
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x130);
				break;
			case 2:  //about 2%
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x2d425012);
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x130);
				break;
			case 3:  //about 3%
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x1d425012);
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x130);
				break;
			case 4:  //about 4%
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x0d125012);
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x130);
				break;
			case 5:  //about 5%
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x16110696);
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x0e425012);
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x130);
				break;
			default:  //disable ss
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x814d3928);
				aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x6b425012);
				aml_write_reg32(P_HHI_VID_PLL_CNTL4, 0x110);
		}
	}
	//PRINT_INFO("set video spread spectrum %d%%.\n", video_ss_level);
}

static void vclk_set_lcd(int lcd_type, int pll_sel, int pll_div_sel, int vclk_sel, unsigned long pll_reg, unsigned long vid_div_reg, unsigned int xd)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
#ifdef NO_2ND_PLL
	pll_sel = 0;
#endif
#endif
    PRINT_INFO("setup lcd clk.\n");
    vid_div_reg |= (1 << 16) ; // turn clock gate on
    vid_div_reg |= (pll_sel << 15); // vid_div_clk_sel

    if(vclk_sel) {
      aml_write_reg32(P_HHI_VIID_CLK_CNTL, aml_read_reg32(P_HHI_VIID_CLK_CNTL) & ~(1 << 19) );     //disable clk_div0
    }
    else {
      aml_write_reg32(P_HHI_VID_CLK_CNTL, aml_read_reg32(P_HHI_VID_CLK_CNTL) & ~(1 << 19) );     //disable clk_div0
      aml_write_reg32(P_HHI_VID_CLK_CNTL, aml_read_reg32(P_HHI_VID_CLK_CNTL) & ~(1 << 20) );     //disable clk_div1
    }

    // delay 2uS to allow the sync mux to switch over
    //aml_write_reg32(P_ISA_TIMERE, 0); while( aml_read_reg32(P_ISA_TIMERE) < 2 ) {}
    udelay(2);

    if(pll_sel){
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
		aml_write_reg32(P_HHI_VIID_PLL_CNTL, pll_reg );
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
		M6_PLL_RESET(P_HHI_VIID_PLL_CNTL);
		aml_write_reg32(P_HHI_VIID_PLL_CNTL, pll_reg|(1<<29) );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x814d3928 );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x6b425012 );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL4, 0x110 );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL, pll_reg );
		M6_PLL_WAIT_FOR_LOCK(P_HHI_VIID_PLL_CNTL);
#else
		aml_write_reg32(P_HHI_VIID_PLL_CNTL, pll_reg|(1<<30) );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL2, 0x65e31ff );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL3, 0x9649a941 );
		aml_write_reg32(P_HHI_VIID_PLL_CNTL, pll_reg );
#endif
    }
    else{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
		aml_write_reg32(P_HHI_VID_PLL_CNTL, pll_reg );
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
		M6_PLL_RESET(P_HHI_VID_PLL_CNTL);
		aml_write_reg32(P_HHI_VID_PLL_CNTL, pll_reg|(1<<29) );
		aml_write_reg32(P_HHI_VID_PLL_CNTL2, M6_VID_PLL_CNTL_2 );
		aml_write_reg32(P_HHI_VID_PLL_CNTL3, M6_VID_PLL_CNTL_3 );
		aml_write_reg32(P_HHI_VID_PLL_CNTL4, M6_VID_PLL_CNTL_4 );
		aml_write_reg32(P_HHI_VID_PLL_CNTL, pll_reg );
		M6_PLL_WAIT_FOR_LOCK(P_HHI_VID_PLL_CNTL);
#else
		aml_write_reg32(P_HHI_VID_PLL_CNTL, pll_reg|(1<<30) );
		aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x65e31ff );
		aml_write_reg32(P_HHI_VID_PLL_CNTL3, 0x9649a941 );
		aml_write_reg32(P_HHI_VID_PLL_CNTL, pll_reg );
#endif
    }

    if(pll_div_sel ){
        aml_write_reg32(P_HHI_VIID_DIVIDER_CNTL,   vid_div_reg);
    }
    else{
        aml_write_reg32(P_HHI_VID_DIVIDER_CNTL,   vid_div_reg);
    }

    if(vclk_sel)
        aml_write_reg32(P_HHI_VIID_CLK_DIV, (aml_read_reg32(P_HHI_VIID_CLK_DIV) & ~(0xFF << 0)) | (xd-1) );   // setup the XD divider value
    else
        aml_write_reg32(P_HHI_VID_CLK_DIV, (aml_read_reg32(P_HHI_VID_CLK_DIV) & ~(0xFF << 0)) | (xd-1) );   // setup the XD divider value

    // delay 5uS
    //aml_write_reg32(P_ISA_TIMERE, 0); while( aml_read_reg32(P_ISA_TIMERE) < 5 ) {}
    udelay(5);

	if(vclk_sel) {
		if(pll_div_sel) aml_set_reg32_bits (P_HHI_VIID_CLK_CNTL, 4, 16, 3);  // Bit[18:16] - v2_cntl_clk_in_sel
		else aml_set_reg32_bits (P_HHI_VIID_CLK_CNTL, 0, 16, 3);  // Bit[18:16] - cntl_clk_in_sel
		//aml_write_reg32(P_HHI_VIID_CLK_CNTL, aml_read_reg32(P_HHI_VIID_CLK_CNTL) |  (1 << 19) );     //enable clk_div0
		aml_set_reg32_mask(P_HHI_VIID_CLK_CNTL, (1 << 19) );     //enable clk_div0
	}
	else {
		if(pll_div_sel) aml_set_reg32_bits (P_HHI_VID_CLK_CNTL, 4, 16, 3);  // Bit[18:16] - v2_cntl_clk_in_sel
		else aml_set_reg32_bits (P_HHI_VID_CLK_CNTL, 0, 16, 3);  // Bit[18:16] - cntl_clk_in_sel
		//aml_write_reg32(P_HHI_VID_CLK_CNTL, aml_read_reg32(P_HHI_VID_CLK_CNTL) |  (1 << 19) );     //enable clk_div0
		aml_set_reg32_mask(P_HHI_VID_CLK_CNTL, (1 << 19) );     //enable clk_div0
		//aml_write_reg32(P_HHI_VID_CLK_CNTL, aml_read_reg32(P_HHI_VID_CLK_CNTL) |  (1 << 20) );     //enable clk_div1
		aml_set_reg32_mask(P_HHI_VID_CLK_CNTL, (1 << 20) );     //enable clk_div1
	}

    // delay 2uS
    //aml_write_reg32(P_ISA_TIMERE, 0); while( aml_read_reg32(P_ISA_TIMERE) < 2 ) {}
    udelay(2);

    // set tcon_clko setting
    aml_set_reg32_bits (P_HHI_VID_CLK_CNTL,
                    (
                    (0 << 11) |     //clk_div1_sel
                    (1 << 10) |     //clk_inv
                    (0 << 9)  |     //neg_edge_sel
                    (0 << 5)  |     //tcon high_thresh
                    (0 << 1)  |     //tcon low_thresh
                    (1 << 0)        //cntl_clk_en1
                    ),
                    20, 12);

    if(lcd_type == LCD_DIGITAL_TTL){
		if(vclk_sel)
		{
			aml_set_reg32_bits (P_HHI_VID_CLK_DIV, 8, 20, 4); // [23:20] enct_clk_sel, select v2_clk_div1
		}
		else
		{
			aml_set_reg32_bits (P_HHI_VID_CLK_DIV, 0, 20, 4); // [23:20] enct_clk_sel, select v1_clk_div1
		}

	}
	else {
#ifdef NO_ENCT
			aml_set_reg32_bits (P_HHI_VIID_CLK_DIV,
						 0, 	 // select clk_div1
						 12, 4); // [23:20] encl_clk_sel
#else
			aml_set_reg32_bits (P_HHI_VID_CLK_DIV,
						 0, 	 // select clk_div1
						 20, 4); // [23:20] enct_clk_sel
#endif
	}

    if(vclk_sel) {
      aml_set_reg32_bits (P_HHI_VIID_CLK_CNTL,
                   (1<<0),  // Enable cntl_div1_en
                   0, 1    // cntl_div1_en
                   );
      aml_set_reg32_bits (P_HHI_VIID_CLK_CNTL, 1, 15, 1);  //soft reset
      aml_set_reg32_bits (P_HHI_VIID_CLK_CNTL, 0, 15, 1);  //release soft reset
    }
    else {
      aml_set_reg32_bits (P_HHI_VID_CLK_CNTL,
                   (1<<0),  // Enable cntl_div1_en
                   0, 1    // cntl_div1_en
                   );
      aml_set_reg32_bits (P_HHI_VID_CLK_CNTL, 1, 15, 1);  //soft reset
      aml_set_reg32_bits (P_HHI_VID_CLK_CNTL, 0, 15, 1);  //release soft reset
    }

    //PRINT_INFO("video pl1 clk = %d\n", clk_util_clk_msr(6));
	//PRINT_INFO("video2 pl1 clk = %d\n", clk_util_clk_msr(12));
	//PRINT_INFO("video pl12 clk = %d\n", clk_util_clk_msr(62));
	//PRINT_INFO("DDR_PLL_CLK = %d\n", clk_util_clk_msr(3));
	//PRINT_INFO("CLK81 = %d\n", clk_util_clk_msr(7));
	//PRINT_INFO("cts_encl_clk = %d\n", clk_util_clk_msr(9));
	//PRINT_INFO("LVDS_FIFO_CLK = %d\n", clk_util_clk_msr(24));
    //PRINT_INFO("video pll2 clk = %d\n", clk_util_clk_msr(VID2_PLL_CLK));
    //PRINT_INFO("cts_enct clk = %d\n", clk_util_clk_msr(CTS_ENCT_CLK));
	//PRINT_INFO("cts_encl clk = %d\n", clk_util_clk_msr(CTS_ENCL_CLK));
}

static void set_pll_ttl(Lcd_Config_t *pConf)
{
    unsigned pll_reg, div_reg, xd;
    int pll_sel, pll_div_sel, vclk_sel;
	int lcd_type, ss_level;

    pll_reg = pConf->lcd_timing.pll_ctrl;
    div_reg = pConf->lcd_timing.div_ctrl | 0x3;
	ss_level = ((pConf->lcd_timing.clk_ctrl) >>16) & 0xf;
    pll_sel = ((pConf->lcd_timing.clk_ctrl) >>12) & 0x1;
    pll_div_sel = ((pConf->lcd_timing.clk_ctrl) >>8) & 0x1;
    vclk_sel = ((pConf->lcd_timing.clk_ctrl) >>4) & 0x1;
	xd = pConf->lcd_timing.clk_ctrl & 0xf;

	lcd_type = pConf->lcd_basic.lcd_type;

    printk("ss_level=%d, pll_sel=%d, pll_div_sel=%d, vclk_sel=%d, pll_reg=0x%x, div_reg=0x%x, xd=%d.\n", ss_level, pll_sel, pll_div_sel, vclk_sel, pll_reg, div_reg, xd);
    vclk_set_lcd(lcd_type, pll_sel, pll_div_sel, vclk_sel, pll_reg, div_reg, xd);
	set_video_spread_spectrum(pll_sel, ss_level);
}

static void clk_util_lvds_set_clk_div(  unsigned long   divn_sel,
                                    unsigned long   divn_tcnt,
                                    unsigned long   div2_en  )
{
    // assign          lvds_div_phy_clk_en     = tst_lvds_tmode ? 1'b1         : phy_clk_cntl[10];
    // assign          lvds_div_div2_sel       = tst_lvds_tmode ? atest_i[5]   : phy_clk_cntl[9];
    // assign          lvds_div_sel            = tst_lvds_tmode ? atest_i[7:6] : phy_clk_cntl[8:7];
    // assign          lvds_div_tcnt           = tst_lvds_tmode ? 3'd6         : phy_clk_cntl[6:4];
    // If dividing by 1, just select the divide by 1 path
    if( divn_tcnt == 1 ) {
        divn_sel = 0;
    }
    aml_write_reg32(P_LVDS_PHY_CLK_CNTL, ((aml_read_reg32(P_LVDS_PHY_CLK_CNTL) & ~((0x3 << 7) | (1 << 9) | (0x7 << 4))) | ((1 << 10) | (divn_sel << 7) | (div2_en << 9) | (((divn_tcnt-1)&0x7) << 4))) );
}

static void set_pll_lvds(Lcd_Config_t *pConf)
{

    int pll_div_post;
    int phy_clk_div2;

    unsigned pll_reg, div_reg, xd;
    int pll_sel, pll_div_sel, vclk_sel;
	int lcd_type;

    PRINT_INFO("%s\n", __FUNCTION__);
#ifdef NO_2ND_PLL
	pll_sel = 0;
#endif
	pll_reg = pConf->lcd_timing.pll_ctrl;//pll_sel ? 0x001514d0 : 0x001514d0;
	pll_div_sel = 1;
    vclk_sel = 0;//((pConf->lcd_timing.clk_ctrl) >>4) & 0x1;
	//xd = pConf->lcd_timing.clk_ctrl & 0xf;
	xd = 1;

	lcd_type = pConf->lcd_basic.lcd_type;

    pll_div_post = 7;

    phy_clk_div2 = 0;

	div_reg = pConf->lcd_timing.div_ctrl | 0x3; //0x00010803;//(div_reg | (1 << 8) | (1 << 11) | ((pll_div_post-1) << 12) | (phy_clk_div2 << 10));
	//printk("pll_sel=%d, pll_div_sel=%d, vclk_sel=%d, pll_reg=0x%x, div_reg=0x%x, xd=%d.\n", pll_sel, pll_div_sel, vclk_sel, pll_reg, div_reg, xd);
    vclk_set_lcd(lcd_type, pll_sel, pll_div_sel, vclk_sel, pll_reg, div_reg, xd);
	aml_write_reg32( P_HHI_VIID_DIVIDER_CNTL, ((aml_read_reg32(P_HHI_VIID_DIVIDER_CNTL) & ~(0x7 << 8)) | (2 << 8) | (0<<10)) );
	//set_video_spread_spectrum(pll_sel, ss_level);

	//clk_util_lvds_set_clk_div(2, pll_div_post, phy_clk_div2);//test code //
    //    lvds_gen_cntl       <= {10'h0,      // [15:4] unused
    //                            2'h1,       // [5:4] divide by 7 in the PHY
    //                            1'b0,       // [3] fifo_en
    //                            1'b0,       // [2] wr_bist_gate
    //                            2'b00};     // [1:0] fifo_wr mode
    //rd_data = aml_read_reg32(P_LVDS_GEN_CNTL);
    //rd_data = rd_data | (1 << 3) | (3<< 0);
    aml_write_reg32(P_LVDS_GEN_CNTL, (aml_read_reg32(P_LVDS_GEN_CNTL)| (1 << 3) | (3<< 0)));
}

static void set_pll_mlvds(Lcd_Config_t *pConf)
{

    int test_bit_num = pConf->lcd_basic.lcd_bits;
    int test_dual_gate = pConf->lvds_mlvds_config.mlvds_config->test_dual_gate;
    int test_pair_num= pConf->lvds_mlvds_config.mlvds_config->test_pair_num;
    int pll_div_post;
    int phy_clk_div2;
    int FIFO_CLK_SEL;
    int MPCLK_DELAY;
    int MCLK_half;
    int MCLK_half_delay;
    unsigned int data32;
    unsigned long mclk_pattern_dual_6_6;
    int test_high_phase = (test_bit_num != 8) | test_dual_gate;
    unsigned long rd_data;

    unsigned pll_reg, div_reg, xd;
    int pll_sel, pll_div_sel, vclk_sel;
	int lcd_type, ss_level;

    PRINT_INFO("%s\n", __FUNCTION__);
    pll_reg = pConf->lcd_timing.pll_ctrl;
    div_reg = pConf->lcd_timing.div_ctrl | 0x3;
	ss_level = ((pConf->lcd_timing.clk_ctrl) >>16) & 0xf;
    pll_sel = ((pConf->lcd_timing.clk_ctrl) >>12) & 0x1;
    //pll_div_sel = ((pConf->lcd_timing.clk_ctrl) >>8) & 0x1;
	pll_div_sel = 1;
    vclk_sel = ((pConf->lcd_timing.clk_ctrl) >>4) & 0x1;
	//xd = pConf->lcd_timing.clk_ctrl & 0xf;
	xd = 1;

	lcd_type = pConf->lcd_basic.lcd_type;

    switch(pConf->lvds_mlvds_config.mlvds_config->TL080_phase)
    {
      case 0 :
        mclk_pattern_dual_6_6 = 0xc3c3c3;
        MCLK_half = 1;
        break;
      case 1 :
        mclk_pattern_dual_6_6 = 0xc3c3c3;
        MCLK_half = 0;
        break;
      case 2 :
        mclk_pattern_dual_6_6 = 0x878787;
        MCLK_half = 1;
        break;
      case 3 :
        mclk_pattern_dual_6_6 = 0x878787;
        MCLK_half = 0;
        break;
      case 4 :
        mclk_pattern_dual_6_6 = 0x3c3c3c;
        MCLK_half = 1;
        break;
      case 5 :
        mclk_pattern_dual_6_6 = 0x3c3c3c;
        MCLK_half = 0;
        break;
      case 6 :
        mclk_pattern_dual_6_6 = 0x787878;
        MCLK_half = 1;
        break;
      default : // case 7
        mclk_pattern_dual_6_6 = 0x787878;
        MCLK_half = 0;
        break;
    }

    pll_div_post = (test_bit_num == 8) ?
                      (
                        test_dual_gate ? 4 :
                                         8
                      ) :
                      (
                        test_dual_gate ? 3 :
                                         6
                      ) ;

    phy_clk_div2 = (test_pair_num != 3);

	div_reg = (div_reg | (1 << 8) | (1 << 11) | ((pll_div_post-1) << 12) | (phy_clk_div2 << 10));
	printk("ss_level=%d, pll_sel=%d, pll_div_sel=%d, vclk_sel=%d, pll_reg=0x%x, div_reg=0x%x, xd=%d.\n", ss_level, pll_sel, pll_div_sel, vclk_sel, pll_reg, div_reg, xd);
    vclk_set_lcd(lcd_type, pll_sel, pll_div_sel, vclk_sel, pll_reg, div_reg, xd);
	set_video_spread_spectrum(pll_sel, ss_level);

	clk_util_lvds_set_clk_div(1, pll_div_post, phy_clk_div2);

	//enable v2_clk div
    // aml_write_reg32(P_ HHI_VIID_CLK_CNTL, aml_read_reg32(P_HHI_VIID_CLK_CNTL) | (0xF << 0) );
    // aml_write_reg32(P_ HHI_VID_CLK_CNTL, aml_read_reg32(P_HHI_VID_CLK_CNTL) | (0xF << 0) );

    aml_write_reg32(P_LVDS_PHY_CNTL0, 0xffff );

    //    lvds_gen_cntl       <= {10'h0,      // [15:4] unused
    //                            2'h1,       // [5:4] divide by 7 in the PHY
    //                            1'b0,       // [3] fifo_en
    //                            1'b0,       // [2] wr_bist_gate
    //                            2'b00};     // [1:0] fifo_wr mode

    FIFO_CLK_SEL = (test_bit_num == 8) ? 2 : // div8
                                    0 ; // div6
    rd_data = aml_read_reg32(P_LVDS_GEN_CNTL);
    rd_data = (rd_data & 0xffcf) | (FIFO_CLK_SEL<< 4);
    aml_write_reg32(P_LVDS_GEN_CNTL, rd_data);

    MPCLK_DELAY = (test_pair_num == 6) ?
                  ((test_bit_num == 8) ? (test_dual_gate ? 5 : 3) : 2) :
                  ((test_bit_num == 8) ? 3 : 3) ;

    MCLK_half_delay = pConf->lvds_mlvds_config.mlvds_config->phase_select ? MCLK_half :
                      (
                      test_dual_gate &
                      (test_bit_num == 8) &
                      (test_pair_num != 6)
                      );

    if(test_high_phase)
    {
        if(test_dual_gate)
        data32 = (MPCLK_DELAY << mpclk_dly) |
                 (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                 (1 << use_mpclk) |
                 (MCLK_half_delay << mlvds_clk_half_delay) |
                 (((test_bit_num == 8) ? (
                                           (test_pair_num == 6) ? 0x999999 : // DIV4
                                                                  0x555555   // DIV2
                                         ) :
                                         (
                                           (test_pair_num == 6) ? mclk_pattern_dual_6_6 : // DIV8
                                                                  0x999999   // DIV4
                                         )
                                         ) << mlvds_clk_pattern);      // DIV 8
        else if(test_bit_num == 8)
            data32 = (MPCLK_DELAY << mpclk_dly) |
                     (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                     (1 << use_mpclk) |
                     (0 << mlvds_clk_half_delay) |
                     (0xc3c3c3 << mlvds_clk_pattern);      // DIV 8
        else
            data32 = (MPCLK_DELAY << mpclk_dly) |
                     (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                     (1 << use_mpclk) |
                     (0 << mlvds_clk_half_delay) |
                     (
                       (
                         (test_pair_num == 6) ? 0xc3c3c3 : // DIV8
                                                0x999999   // DIV4
                       ) << mlvds_clk_pattern
                     );
    }
    else
    {
        if(test_pair_num == 6)
        {
            data32 = (MPCLK_DELAY << mpclk_dly) |
                     (((test_bit_num == 8) ? 3 : 2) << mpclk_div) |
                     (1 << use_mpclk) |
                     (0 << mlvds_clk_half_delay) |
                     (
                       (
                         (test_pair_num == 6) ? 0x999999 : // DIV4
                                                0x555555   // DIV2
                       ) << mlvds_clk_pattern
                     );
        }
        else
        {
            data32 = (1 << mlvds_clk_half_delay) |
                   (0x555555 << mlvds_clk_pattern);      // DIV 2
        }
    }

    //aml_write_reg32(P_MLVDS_CLK_CTL_HI,  (data32 >> 16));
    //aml_write_reg32(P_MLVDS_CLK_CTL_LO, (data32 & 0xffff));

	//pll_div_sel
    if(1){
		// Set Soft Reset vid_pll_div_pre
		aml_write_reg32(P_HHI_VIID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VIID_DIVIDER_CNTL) | 0x00008);
		// Set Hard Reset vid_pll_div_post
		aml_write_reg32(P_HHI_VIID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VIID_DIVIDER_CNTL) & 0x1fffd);
		// Set Hard Reset lvds_phy_ser_top
		aml_write_reg32(P_LVDS_PHY_CLK_CNTL, aml_read_reg32(P_LVDS_PHY_CLK_CNTL) & 0x7fff);
		// Release Hard Reset lvds_phy_ser_top
		aml_write_reg32(P_LVDS_PHY_CLK_CNTL, aml_read_reg32(P_LVDS_PHY_CLK_CNTL) | 0x8000);
		// Release Hard Reset vid_pll_div_post
		aml_write_reg32(P_HHI_VIID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VIID_DIVIDER_CNTL) | 0x00002);
		// Release Soft Reset vid_pll_div_pre
		aml_write_reg32(P_HHI_VIID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VIID_DIVIDER_CNTL) & 0x1fff7);
	}
	else{
		// Set Soft Reset vid_pll_div_pre
		aml_write_reg32(P_HHI_VID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VID_DIVIDER_CNTL) | 0x00008);
		// Set Hard Reset vid_pll_div_post
		aml_write_reg32(P_HHI_VID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VID_DIVIDER_CNTL) & 0x1fffd);
		// Set Hard Reset lvds_phy_ser_top
		aml_write_reg32(P_LVDS_PHY_CLK_CNTL, aml_read_reg32(P_LVDS_PHY_CLK_CNTL) & 0x7fff);
		// Release Hard Reset lvds_phy_ser_top
		aml_write_reg32(P_LVDS_PHY_CLK_CNTL, aml_read_reg32(P_LVDS_PHY_CLK_CNTL) | 0x8000);
		// Release Hard Reset vid_pll_div_post
		aml_write_reg32(P_HHI_VID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VID_DIVIDER_CNTL) | 0x00002);
		// Release Soft Reset vid_pll_div_pre
		aml_write_reg32(P_HHI_VID_DIVIDER_CNTL, aml_read_reg32(P_HHI_VID_DIVIDER_CNTL) & 0x1fff7);
    }
}

static void venc_set_ttl(Lcd_Config_t *pConf)
{
    PRINT_INFO("%s\n", __FUNCTION__);
	aml_write_reg32(P_ENCT_VIDEO_EN,           0);
    aml_write_reg32(P_VPU_VIU_VENC_MUX_CTRL,
       (3<<0) |    // viu1 select enct
       (3<<2)      // viu2 select enct
    );
    aml_write_reg32(P_ENCT_VIDEO_MODE,        0);
    aml_write_reg32(P_ENCT_VIDEO_MODE_ADV,    0x0418);

	// bypass filter
    aml_write_reg32(P_ENCT_VIDEO_FILT_CTRL,    0x1000);
    aml_write_reg32(P_VENC_DVI_SETTING,        0x11);
    aml_write_reg32(P_VENC_VIDEO_PROG_MODE,    0x100);

    aml_write_reg32(P_ENCT_VIDEO_MAX_PXCNT,    pConf->lcd_basic.h_period - 1);
    aml_write_reg32(P_ENCT_VIDEO_MAX_LNCNT,    pConf->lcd_basic.v_period - 1);

    aml_write_reg32(P_ENCT_VIDEO_HAVON_BEGIN,  pConf->lcd_timing.video_on_pixel);
    aml_write_reg32(P_ENCT_VIDEO_HAVON_END,    pConf->lcd_basic.h_active - 1 + pConf->lcd_timing.video_on_pixel);
    aml_write_reg32(P_ENCT_VIDEO_VAVON_BLINE,  pConf->lcd_timing.video_on_line);
    aml_write_reg32(P_ENCT_VIDEO_VAVON_ELINE,  pConf->lcd_basic.v_active + 3  + pConf->lcd_timing.video_on_line);

    aml_write_reg32(P_ENCT_VIDEO_HSO_BEGIN,    15);
    aml_write_reg32(P_ENCT_VIDEO_HSO_END,      31);
    aml_write_reg32(P_ENCT_VIDEO_VSO_BEGIN,    15);
    aml_write_reg32(P_ENCT_VIDEO_VSO_END,      31);
    aml_write_reg32(P_ENCT_VIDEO_VSO_BLINE,    0);
    aml_write_reg32(P_ENCT_VIDEO_VSO_ELINE,    2);

    // enable enct
    aml_write_reg32(P_ENCT_VIDEO_EN,           1);
}

static void venc_set_lvds(Lcd_Config_t *pConf)
{
    PRINT_INFO("%s\n", __FUNCTION__);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
    aml_write_reg32(P_VPU_VIU_VENC_MUX_CTRL,
    (0<<0) |    // viu1 select encl
    (0<<2)      // viu2 select encl
    );
#endif
	aml_write_reg32(P_ENCL_VIDEO_EN,           0);
	//int havon_begin = 80;
    aml_write_reg32(P_VPU_VIU_VENC_MUX_CTRL,
       (0<<0) |    // viu1 select encl
       (0<<2)      // viu2 select encl
       );
	aml_write_reg32(P_ENCL_VIDEO_MODE,         0); // Enable Hsync and equalization pulse switch in center; bit[14] cfg_de_v = 1
	aml_write_reg32(P_ENCL_VIDEO_MODE_ADV,     0x0418); // Sampling rate: 1

	// bypass filter
 	aml_write_reg32(P_ENCL_VIDEO_FILT_CTRL	,0x1000);

	aml_write_reg32(P_ENCL_VIDEO_MAX_PXCNT,	pConf->lcd_basic.h_period - 1);
if(cur_lvds_index)
	aml_write_reg32(P_ENCL_VIDEO_MAX_LNCNT,	1350 - 1);
else
	aml_write_reg32(P_ENCL_VIDEO_MAX_LNCNT,	pConf->lcd_basic.v_period - 1);

	aml_write_reg32(P_ENCL_VIDEO_HAVON_BEGIN,	pConf->lcd_timing.video_on_pixel);
	aml_write_reg32(P_ENCL_VIDEO_HAVON_END,		pConf->lcd_basic.h_active - 1 + pConf->lcd_timing.video_on_pixel);
	aml_write_reg32(P_ENCL_VIDEO_VAVON_BLINE,	pConf->lcd_timing.video_on_line);
	aml_write_reg32(P_ENCL_VIDEO_VAVON_ELINE,	pConf->lcd_basic.v_active - 1  + pConf->lcd_timing.video_on_line);

	aml_write_reg32(P_ENCL_VIDEO_HSO_BEGIN,	pConf->lcd_timing.sth1_hs_addr);//10);
	aml_write_reg32(P_ENCL_VIDEO_HSO_END,	pConf->lcd_timing.sth1_he_addr);//20);
	aml_write_reg32(P_ENCL_VIDEO_VSO_BEGIN,	pConf->lcd_timing.stv1_hs_addr);//10);
	aml_write_reg32(P_ENCL_VIDEO_VSO_END,	pConf->lcd_timing.stv1_he_addr);//20);
	aml_write_reg32(P_ENCL_VIDEO_VSO_BLINE,	pConf->lcd_timing.stv1_vs_addr);//2);
	aml_write_reg32(P_ENCL_VIDEO_VSO_ELINE,	pConf->lcd_timing.stv1_ve_addr);//4);

	aml_write_reg32(P_ENCL_VIDEO_RGBIN_CTRL, 	0);

	// enable encl
    aml_write_reg32(P_ENCL_VIDEO_EN,           1);
}

static void venc_set_mlvds(Lcd_Config_t *pConf)
{
	int ext_pixel,active_h_start,active_v_start,width,height,max_height;
    PRINT_INFO("%s\n", __FUNCTION__);

    aml_write_reg32(P_ENCL_VIDEO_EN,           0);

    aml_write_reg32(P_VPU_VIU_VENC_MUX_CTRL,
       (0<<0) |    // viu1 select encl
       (0<<2)      // viu2 select encl
       );
	ext_pixel = pConf->lvds_mlvds_config.mlvds_config->test_dual_gate ? pConf->lvds_mlvds_config.mlvds_config->total_line_clk : 0;
	active_h_start = pConf->lcd_timing.video_on_pixel;
	active_v_start = pConf->lcd_timing.video_on_line;
	width = pConf->lcd_basic.h_active;
	height = pConf->lcd_basic.v_active;
	max_height = pConf->lcd_basic.v_period;

	aml_write_reg32(P_ENCL_VIDEO_MODE,             0x0040 | (1<<14)); // Enable Hsync and equalization pulse switch in center; bit[14] cfg_de_v = 1
	aml_write_reg32(P_ENCL_VIDEO_MODE_ADV,         0x0008); // Sampling rate: 1

	// bypass filter
 	aml_write_reg32(P_ENCL_VIDEO_FILT_CTRL,	0x1000);

	aml_write_reg32(P_ENCL_VIDEO_YFP1_HTIME,       active_h_start);
	aml_write_reg32(P_ENCL_VIDEO_YFP2_HTIME,       active_h_start + width);

	aml_write_reg32(P_ENCL_VIDEO_MAX_PXCNT,        pConf->lvds_mlvds_config.mlvds_config->total_line_clk - 1 + ext_pixel);
	aml_write_reg32(P_ENCL_VIDEO_MAX_LNCNT,        max_height - 1);

	aml_write_reg32(P_ENCL_VIDEO_HAVON_BEGIN,      active_h_start);
	aml_write_reg32(P_ENCL_VIDEO_HAVON_END,        active_h_start + width - 1);  // for dual_gate mode still read 1408 pixel at first half of line
	aml_write_reg32(P_ENCL_VIDEO_VAVON_BLINE,      active_v_start);
	aml_write_reg32(P_ENCL_VIDEO_VAVON_ELINE,      active_v_start + height -1);  //15+768-1);

	aml_write_reg32(P_ENCL_VIDEO_HSO_BEGIN,        24);
	aml_write_reg32(P_ENCL_VIDEO_HSO_END,          1420 + ext_pixel);
	aml_write_reg32(P_ENCL_VIDEO_VSO_BEGIN,        1400 + ext_pixel);
	aml_write_reg32(P_ENCL_VIDEO_VSO_END,          1410 + ext_pixel);
	aml_write_reg32(P_ENCL_VIDEO_VSO_BLINE,        1);
	aml_write_reg32(P_ENCL_VIDEO_VSO_ELINE,        3);

	aml_write_reg32(P_ENCL_VIDEO_RGBIN_CTRL, 	0);

	// enable encl
    aml_write_reg32(P_ENCL_VIDEO_EN,           1);
}

static void set_control_lvds(Lcd_Config_t *pConf)
{

	//int lvds_repack, port_reverse, pn_swap, bit_num, dual_port;
    PRINT_INFO("%s\n", __FUNCTION__);
#if 0//MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
    data32 = (0x00 << LVDS_blank_data_r) |
             (0x00 << LVDS_blank_data_g) |
             (0x00 << LVDS_blank_data_b) ;
    aml_write_reg32(P_LVDS_BLANK_DATA_HI,  (data32 >> 16));
    aml_write_reg32(P_LVDS_BLANK_DATA_LO, (data32 & 0xffff));

	aml_write_reg32(P_LVDS_PHY_CNTL0, 0xffff );
	aml_write_reg32(P_LVDS_PHY_CNTL1, 0xff00 );
#endif

	//aml_write_reg32(P_ENCL_VIDEO_EN,           1);
	if(lvds_repack_flag)
	lvds_repack = (pConf->lvds_mlvds_config.lvds_config->lvds_repack) & 0x1;
	pn_swap = (pConf->lvds_mlvds_config.lvds_config->pn_swap) & 0x1;
	dual_port = (pConf->lvds_mlvds_config.lvds_config->dual_port) & 0x1;
	if(port_reverse_flag)
	port_reverse = (pConf->lvds_mlvds_config.lvds_config->port_reverse) & 0x1;
    if(bit_num_flag)
    	{
		switch(pConf->lcd_basic.lcd_bits)
			{
			case 10:
				bit_num=0;
				break;
			case 8:
				bit_num=1;
				break;
			case 6:
				bit_num=2;
				break;
			case 4:
				bit_num=3;
				break;
			default:
				bit_num=1;
				break;
			}
    	}
	aml_write_reg32(P_MLVDS_CONTROL,  (aml_read_reg32(P_MLVDS_CONTROL) & ~(1 << 0)));  //disable mlvds

	aml_write_reg32(P_LVDS_PACK_CNTL_ADDR,
					( lvds_repack<<0 ) | // repack
					( port_reverse?(0<<2):(1<<2)) | // odd_even
					( 0<<3 ) | // reserve
					( 0<<4 ) | // lsb first
					( pn_swap<<5 ) | // pn swap
					( dual_port<<6 ) | // dual port
					( 0<<7 ) | // use tcon control
					( bit_num<<8 ) | // 0:10bits, 1:8bits, 2:6bits, 3:4bits.
					( 0<<10 ) | //r_select  //0:R, 1:G, 2:B, 3:0
					( 1<<12 ) | //g_select  //0:R, 1:G, 2:B, 3:0
					( 2<<14 ));  //b_select  //0:R, 1:G, 2:B, 3:0;
    //aml_write_reg32(P_LVDS_GEN_CNTL, (aml_read_reg32(P_LVDS_GEN_CNTL) | (1 << 3))); // enable fifo

	//PRINT_INFO("lvds fifo clk = %d.\n", clk_util_clk_msr(LVDS_FIFO_CLK));
}

static void set_control_mlvds(Lcd_Config_t *pConf)
{

	int test_bit_num = pConf->lcd_basic.lcd_bits;
    int test_pair_num = pConf->lvds_mlvds_config.mlvds_config->test_pair_num;
    int test_dual_gate = pConf->lvds_mlvds_config.mlvds_config->test_dual_gate;
    int scan_function = pConf->lvds_mlvds_config.mlvds_config->scan_function;     //0:U->D,L->R  //1:D->U,R->L
    int mlvds_insert_start;
    unsigned int reset_offset;
    unsigned int reset_length;

    unsigned long data32;

    PRINT_INFO("%s\n", __FUNCTION__);
    mlvds_insert_start = test_dual_gate ?
                           ((test_bit_num == 8) ? ((test_pair_num == 6) ? 0x9f : 0xa9) :
                                                  ((test_pair_num == 6) ? pConf->lvds_mlvds_config.mlvds_config->mlvds_insert_start : 0xa7)
                           ) :
                           (
                             (test_pair_num == 6) ? ((test_bit_num == 8) ? 0xa9 : 0xa7) :
                                                    ((test_bit_num == 8) ? 0xae : 0xad)
                           );

    // Enable the LVDS PHY (power down bits)
    aml_write_reg32(P_LVDS_PHY_CNTL1,aml_read_reg32(P_LVDS_PHY_CNTL1) | (0x7F << 8) );

    data32 = (0x00 << LVDS_blank_data_r) |
             (0x00 << LVDS_blank_data_g) |
             (0x00 << LVDS_blank_data_b) ;
    aml_write_reg32(P_LVDS_BLANK_DATA_HI,  (data32 >> 16));
    aml_write_reg32(P_LVDS_BLANK_DATA_LO, (data32 & 0xffff));

    data32 = 0x7fffffff; //  '0'x1 + '1'x32 + '0'x2
    aml_write_reg32(P_MLVDS_RESET_PATTERN_HI,  (data32 >> 16));
    aml_write_reg32(P_MLVDS_RESET_PATTERN_LO, (data32 & 0xffff));
    data32 = 0x8000; // '0'x1 + '1'x32 + '0'x2
    aml_write_reg32(P_MLVDS_RESET_PATTERN_EXT,  (data32 & 0xffff));

    reset_length = 1+32+2;
    reset_offset = test_bit_num - (reset_length%test_bit_num);

    data32 = (reset_offset << mLVDS_reset_offset) |
             (reset_length << mLVDS_reset_length) |
             ((test_pair_num == 6) << mLVDS_data_write_toggle) |
             ((test_pair_num != 6) << mLVDS_data_write_ini) |
             ((test_pair_num == 6) << mLVDS_data_latch_1_toggle) |
             (0 << mLVDS_data_latch_1_ini) |
             ((test_pair_num == 6) << mLVDS_data_latch_0_toggle) |
             (1 << mLVDS_data_latch_0_ini) |
             ((test_pair_num == 6) << mLVDS_reset_1_select) |
             (mlvds_insert_start << mLVDS_reset_start);
    aml_write_reg32(P_MLVDS_CONFIG_HI,  (data32 >> 16));
    aml_write_reg32(P_MLVDS_CONFIG_LO, (data32 & 0xffff));

    data32 = (1 << mLVDS_double_pattern) |  //POL double pattern
			 (0x3f << mLVDS_ins_reset) |
             (test_dual_gate << mLVDS_dual_gate) |
             ((test_bit_num == 8) << mLVDS_bit_num) |
             ((test_pair_num == 6) << mLVDS_pair_num) |
             (0 << mLVDS_msb_first) |
             (0 << mLVDS_PORT_SWAP) |
             ((scan_function==1 ? 1:0) << mLVDS_MLSB_SWAP) |
             (0 << mLVDS_PN_SWAP) |
             (1 << mLVDS_en);
    aml_write_reg32(P_MLVDS_CONTROL,  (data32 & 0xffff));

    aml_write_reg32(P_LVDS_PACK_CNTL_ADDR,
                   ( 0 ) | // repack
                   ( 0<<2 ) | // odd_even
                   ( 0<<3 ) | // reserve
                   ( 0<<4 ) | // lsb first
                   ( 0<<5 ) | // pn swap
                   ( 0<<6 ) | // dual port
                   ( 0<<7 ) | // use tcon control
                   ( 1<<8 ) | // 0:10bits, 1:8bits, 2:6bits, 3:4bits.
                   ( (scan_function==1 ? 2:0)<<10 ) |  //r_select // 0:R, 1:G, 2:B, 3:0
                   ( 1<<12 ) |                        //g_select
                   ( (scan_function==1 ? 0:2)<<14 ));  //b_select

    aml_write_reg32(P_L_POL_CNTL_ADDR,  (1 << LCD_DCLK_SEL) |
       //(0x1 << LCD_HS_POL) |
       (0x1 << LCD_VS_POL)
    );

    //aml_write_reg32(P_LVDS_GEN_CNTL, (aml_read_reg32(P_LVDS_GEN_CNTL) | (1 << 3))); // enable fifo
}

static void init_lvds_phy(Lcd_Config_t *pConf)
{

    PRINT_INFO("%s\n", __FUNCTION__);
	aml_write_reg32( P_LVDS_SER_EN, 0xfff );
    aml_write_reg32( P_LVDS_PHY_CNTL0, 0x0002 );//0xffff
    aml_write_reg32( P_LVDS_PHY_CNTL1, 0xff00 );
	aml_write_reg32( P_LVDS_PHY_CNTL3, 0x0ee1 );
    aml_write_reg32( P_LVDS_PHY_CNTL4, 0x3fff );
	aml_write_reg32( P_LVDS_PHY_CNTL5, 0xac24 );//ac24
    //aml_write_reg32(P_LVDS_PHY_CNTL4, aml_read_reg32(P_LVDS_PHY_CNTL4) | (0x7f<<0));  //enable LVDS phy port..
}

#include <mach/mod_gate.h>

static void switch_lcd_gates(Lcd_Type_t lcd_type)
{
    switch(lcd_type){
        case LCD_DIGITAL_TTL:
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
            //switch_mod_gate_by_name("tcon", 1);
            //switch_mod_gate_by_name("lvds", 0);
#endif
            break;
        case LCD_DIGITAL_LVDS:
        case LCD_DIGITAL_MINILVDS:
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
            //switch_mod_gate_by_name("lvds", 1);
            //switch_mod_gate_by_name("tcon", 0);
#endif
            break;
        default:
            break;
    }
}

static inline void _init_display_driver(Lcd_Config_t *pConf)
{
    int lcd_type;
	const char* lcd_type_table[]={
		"NULL",
		"TTL",
		"LVDS",
		"miniLVDS",
		"invalid",
	};

	lcd_type = pDev->conf.lcd_basic.lcd_type;
	printk("\nInit LCD type: %s.\n", lcd_type_table[lcd_type]);
	//printk("lcd frame rate=%d/%d.\n", pDev->conf.lcd_timing.sync_duration_num, pDev->conf.lcd_timing.sync_duration_den);

    switch_lcd_gates(lcd_type);

	switch(lcd_type){
        case LCD_DIGITAL_TTL:
            set_pll_ttl(pConf);
            venc_set_ttl(pConf);
			set_tcon_ttl(pConf);
            break;
        case LCD_DIGITAL_LVDS:
        	set_pll_lvds(pConf);
            venc_set_lvds(pConf);
        	set_control_lvds(pConf);
        	init_lvds_phy(pConf);
			set_tcon_lvds(pConf);
            break;
        case LCD_DIGITAL_MINILVDS:
			set_pll_mlvds(pConf);
			venc_set_mlvds(pConf);
			set_control_mlvds(pConf);
			init_lvds_phy(pConf);
			set_tcon_mlvds(pConf);
            break;
        default:
            printk("Invalid LCD type.\n");
			break;
    }
}

static inline void _disable_display_driver(void)
{
    int pll_sel, vclk_sel;

    pll_sel = 0;//((pConf->lcd_timing.clk_ctrl) >>12) & 0x1;
    vclk_sel = 0;//((pConf->lcd_timing.clk_ctrl) >>4) & 0x1;

	aml_set_reg32_bits(P_HHI_VIID_DIVIDER_CNTL, 0, 11, 1);	//close lvds phy clk gate: 0x104c[11]

	aml_write_reg32(P_ENCT_VIDEO_EN, 0);	//disable enct
	aml_write_reg32(P_ENCL_VIDEO_EN, 0);	//disable encl

	if (vclk_sel)
		aml_set_reg32_bits(P_HHI_VIID_CLK_CNTL, 0, 0, 5);		//close vclk2 gate: 0x104b[4:0]
	else
		aml_set_reg32_bits(P_HHI_VID_CLK_CNTL, 0, 0, 5);		//close vclk1 gate: 0x105f[4:0]

	if (pll_sel){
		aml_set_reg32_bits(P_HHI_VIID_DIVIDER_CNTL, 0, 16, 1);	//close vid2_pll gate: 0x104c[16]
		aml_set_reg32_bits(P_HHI_VIID_PLL_CNTL, 1, 30, 1);		//power down vid2_pll: 0x1047[30]
	}
	else{
		aml_set_reg32_bits(P_HHI_VID_DIVIDER_CNTL, 0, 16, 1);	//close vid1_pll gate: 0x1066[16]
		aml_set_reg32_bits(P_HHI_VID_PLL_CNTL, 0, 30, 1);		//power down vid1_pll: 0x105c[30]
	}
	printk("disable lcd display driver.\n");
}

static inline void _enable_vsync_interrupt(void)
{
    if ((aml_read_reg32(P_ENCT_VIDEO_EN) & 1) || (aml_read_reg32(P_ENCL_VIDEO_EN) & 1)) {
        aml_write_reg32(P_VENC_INTCTRL, 0x200);
#if 0
        while ((aml_read_reg32(P_VENC_INTFLAG) & 0x200) == 0) {
            u32 line1, line2;

            line1 = line2 = aml_read_reg32(P_VENC_ENCP_LINE);

            while (line1 >= line2) {
                line2 = line1;
                line1 = aml_read_reg32(P_VENC_ENCP_LINE);
            }

            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            if (aml_read_reg32(P_VENC_INTFLAG) & 0x200) {
                break;
            }

            aml_write_reg32(P_ENCP_VIDEO_EN, 0);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);

            aml_write_reg32(P_ENCP_VIDEO_EN, 1);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
            aml_read_reg32(P_VENC_INTFLAG);
        }
#endif
    }
    else{
        aml_write_reg32(P_VENC_INTCTRL, 0x2);
    }
}
static void _lcd_module_enable(void)
{
    BUG_ON(pDev==NULL);

	_init_display_driver(&pDev->conf);
	//pDev->conf.lcd_power_ctrl.power_ctrl?pDev->conf.lcd_power_ctrl.power_ctrl(ON):0;

    _enable_vsync_interrupt();
}

static const vinfo_t *lcd_get_current_info(void)
{
    if(cur_lvds_index)
    	{
    	pDev->lcd_info.name = "lvds1080p50hz";
		pDev->lcd_info.mode = VMODE_LVDS_1080P_50HZ;
		pDev->lcd_info.sync_duration_num = 50;
		pDev->lcd_info.sync_duration_den = 1;
    	}
	else
		{
    	pDev->lcd_info.name = "lvds1080p";
		pDev->lcd_info.mode = VMODE_LVDS_1080P;
		pDev->lcd_info.sync_duration_num = 60;
		pDev->lcd_info.sync_duration_den = 1;
		}
    return &pDev->lcd_info;
}

static int lcd_set_current_vmode(vmode_t mode)
{
    if ((mode != VMODE_LCD)&&(mode != VMODE_LVDS_1080P)&&(mode != VMODE_LVDS_1080P_50HZ))
        return -EINVAL;
	if(VMODE_LVDS_1080P_50HZ==(mode&VMODE_MODE_BIT_MASK))
		cur_lvds_index = 1;
	else
		cur_lvds_index = 0;

    aml_write_reg32(P_VPP_POSTBLEND_H_SIZE, pDev->lcd_info.width);
    _lcd_module_enable();
    if (VMODE_INIT_NULL == pDev->lcd_info.mode)
        pDev->lcd_info.mode = VMODE_LCD;
    //_enable_backlight(BL_MAX_LEVEL);
    return 0;
}
static vmode_t lcd_validate_vmode(char *mode)
{
    if ((strncmp(mode, "lvds1080p50hz", strlen("lvds1080p50hz"))) == 0)
        return VMODE_LVDS_1080P_50HZ;
    else if ((strncmp(mode, "lvds1080p", strlen("lvds1080p"))) == 0)
        return VMODE_LVDS_1080P;
    return VMODE_MAX;
}
static int lcd_vmode_is_supported(vmode_t mode)
{
    mode&=VMODE_MODE_BIT_MASK;
    if((mode == VMODE_LCD ) || (mode == VMODE_LVDS_1080P) || (mode == VMODE_LVDS_1080P_50HZ))
    return true;
    return false;
}
static int lcd_module_disable(vmode_t cur_vmod)
{
/*
    BUG_ON(pDev==NULL);
    _disable_backlight();
    pDev->conf.lcd_power_ctrl.power_ctrl?pDev->conf.lcd_power_ctrl.power_ctrl(OFF):0;
    _disable_display_driver(&pDev->conf);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("tcon", 0);
    switch_mod_gate_by_name("lvds", 0);
#endif
*/
    return 0;
}
#ifdef  CONFIG_PM
static int lcd_suspend(void)
{
    BUG_ON(pDev==NULL);
    PRINT_INFO("lcd_suspend \n");
    //_disable_backlight();

    //backlight_power_off();
    //mdelay(backlight_power_off_delay);

    // pDev->conf.lcd_power_ctrl.power_ctrl?pDev->conf.lcd_power_ctrl.power_ctrl(OFF):0;
    _disable_display_driver();
   // mdelay(clock_disable_delay);
   // pwm_disable();
   // mdelay(pwm_disable_delay);
   // panel_power_off();
    return 0;
}
static int lcd_resume(void)
{
    PRINT_INFO("lcd_resume\n");

    //panel_power_on();
   // mdelay(panel_power_on_delay);
    //pwm_enable();
   // mdelay(pwm_enable_delay);
    _lcd_module_enable();
    //_enable_backlight(BL_MAX_LEVEL);

   // mdelay(clock_enable_delay);

   // backlight_power_on();
    return 0;
}
#endif
static vout_server_t lcd_vout_server={
    .name = "lcd_vout_server",
    .op = {
        .get_vinfo = lcd_get_current_info,
        .set_vmode = lcd_set_current_vmode,
        .validate_vmode = lcd_validate_vmode,
        .vmode_is_supported=lcd_vmode_is_supported,
        .disable=lcd_module_disable,
#ifdef  CONFIG_PM
        .vout_suspend=lcd_suspend,
        .vout_resume=lcd_resume,
#endif
    },
};
static void _init_vout(lcd_dev_t *pDev)
{
    pDev->lcd_info.name = "lvds1080p";
    pDev->lcd_info.mode = VMODE_LVDS_1080P;
    pDev->lcd_info.width = pDev->conf.lcd_basic.h_active;
    pDev->lcd_info.height = pDev->conf.lcd_basic.v_active;
    pDev->lcd_info.field_height = pDev->conf.lcd_basic.v_active;
    pDev->lcd_info.aspect_ratio_num = pDev->conf.lcd_basic.screen_ratio_width;
    pDev->lcd_info.aspect_ratio_den = pDev->conf.lcd_basic.screen_ratio_height;
    pDev->lcd_info.screen_real_width= pDev->conf.lcd_basic.screen_actual_width;
    pDev->lcd_info.screen_real_height= pDev->conf.lcd_basic.screen_actual_height;
    pDev->lcd_info.sync_duration_num = pDev->conf.lcd_timing.sync_duration_num;
    pDev->lcd_info.sync_duration_den = pDev->conf.lcd_timing.sync_duration_den;

    //add lcd actual active area size
    //printk("lcd actual active area size: %d %d (mm).\n", pDev->conf.lcd_basic.screen_actual_width, pDev->conf.lcd_basic.screen_actual_height);
    vout_register_server(&lcd_vout_server);
}

static void _lcd_init(Lcd_Config_t *pConf)
{
    //logo_object_t  *init_logo_obj=NULL;

    _init_vout(pDev);
    //init_logo_obj = get_current_logo_obj();
    //if(NULL==init_logo_obj ||!init_logo_obj->para.loaded)
        //_lcd_module_enable();
}

static int lcd_reboot_notifier(struct notifier_block *nb, unsigned long state, void *cmd)
 {
    if (state == SYS_RESTART)
	{
		printk("shut down lcd...\n");
		//_disable_backlight();
		//pDev->conf.lcd_power_ctrl.power_ctrl?pDev->conf.lcd_power_ctrl.power_ctrl(OFF):0;
	}
    return NOTIFY_DONE;
}

static struct notifier_block lcd_reboot_nb;


static ssize_t power_show(struct class *cls,
			struct class_attribute *attr, char *buf)
{
	int ret = 0;
	bool status = false;
	int value;

	status = true;//gpio_get_status(PAD_GPIOZ_5);
	ret = sprintf(buf, "PAD_GPIOZ_5 gpio_get_status %s\n", (status?"true":"false"));
	ret = sprintf(buf, "\n");

	value = 0;//gpio_in_get(PAD_GPIOZ_5);
	ret = sprintf(buf, "PAD_GPIOZ_5 gpio_in_get %d\n", value);
	ret = sprintf(buf, "\n");

	return ret;
}

static ssize_t power_store(struct class *cls, struct class_attribute *attr,
			 const char *buf, size_t count)
{
	int ret = 0;
	int status = 0;
	status = simple_strtol(buf, NULL, 0);
	printk("input status %d\n", status);

	if (status != 0) {
		panel_power_off();
		printk("lvds_power_off\n");
	}
	else {
		panel_power_on();
		printk("lvds_power_on\n");
	}
	return ret;
}
// Define LVDS physical PREM SWING VCM REF
static Lvds_Phy_Control_t lcd_lvds_phy_control = {
    .lvds_prem_ctl  = 0x4,
    .lvds_swing_ctl = 0x2,
    .lvds_vcm_ctl   = 0x4,
    .lvds_ref_ctl   = 0x15,
    .lvds_phy_ctl0  = 0x0002,
    .lvds_fifo_wr_mode = 0x3,
};

//Define LVDS data mapping, pn swap.
static Lvds_Config_t lcd_lvds_config = {
    .lvds_repack    = 1,   //data mapping  //0:JEDIA mode, 1:VESA mode
    .pn_swap    = 0,       //0:normal, 1:swap
    .dual_port  = 1,
    .port_reverse   = 1,
};

static inline int _get_lcd_default_config(struct platform_device *pdev)
{
	//Lcd_Config_t lcd_cfg_t;
	int ret = 0;
	unsigned int lvds_para[10];
	if (!pdev->dev.of_node){
		printk("\n can't get dev node---error----%s----%d",__FUNCTION__,__LINE__);
	}


	ret = of_property_read_u32_array(pdev->dev.of_node,"basic_setting",&lvds_para[0], 10);
	if(ret){
		printk("don't find to match basic_setting, use default setting.\n");
	}else{
		printk("get basic_setting ok.\n");
		pDev->conf.lcd_basic.h_active = lvds_para[0];
		pDev->conf.lcd_basic.v_active = lvds_para[1];
		pDev->conf.lcd_basic.h_period= lvds_para[2];
		pDev->conf.lcd_basic.v_period= lvds_para[3];
		pDev->conf.lcd_basic.screen_ratio_width = lvds_para[4];
		pDev->conf.lcd_basic.screen_ratio_height = lvds_para[5];
		pDev->conf.lcd_basic.screen_actual_width = lvds_para[6];
		pDev->conf.lcd_basic.screen_actual_height = lvds_para[7];
		pDev->conf.lcd_basic.lcd_type = lvds_para[8];
		pDev->conf.lcd_basic.lcd_bits = lvds_para[9];
	}
	ret = of_property_read_u32_array(pdev->dev.of_node,"delay_setting",&lvds_para[0], 8);
	if(ret){
		printk("don't find to match delay_setting, use default setting.\n");
	}else{
		pDev->conf.lcd_sequence.clock_enable_delay = lvds_para[0];
		pDev->conf.lcd_sequence.clock_disable_delay = lvds_para[1];
		pDev->conf.lcd_sequence.pwm_enable_delay= lvds_para[2];
		pDev->conf.lcd_sequence.pwm_disable_delay= lvds_para[3];
		pDev->conf.lcd_sequence.panel_power_on_delay = lvds_para[4];
		pDev->conf.lcd_sequence.panel_power_off_delay = lvds_para[5];
		pDev->conf.lcd_sequence.backlight_power_on_delay = lvds_para[6];
		pDev->conf.lcd_sequence.backlight_power_off_delay = lvds_para[7];
	}
	return ret;
}

static Lcd_Config_t m6tv_lvds_config = {
    // Refer to LCD Spec
    .lcd_basic = {
        .h_active = H_ACTIVE,
        .v_active = V_ACTIVE,
        .h_period = H_PERIOD,
        .v_period = V_PERIOD,
        .screen_ratio_width   = 16,
        .screen_ratio_height  = 9,
        .screen_actual_width  = 127, //this is value for 160 dpi please set real value according to spec.
        .screen_actual_height = 203, //
        .lcd_type = LCD_DIGITAL_LVDS,   //LCD_DIGITAL_TTL  //LCD_DIGITAL_LVDS  //LCD_DIGITAL_MINILVDS
        .lcd_bits = 8,  //8  //6
    },

    .lcd_timing = {
        .pll_ctrl = 0x40050c82,//0x400514d0, //
        .div_ctrl = 0x00010803,
        .clk_ctrl = 0x1111,  //[19:16]ss_ctrl, [12]pll_sel, [8]div_sel, [4]vclk_sel, [3:0]xd
        //.sync_duration_num = 501,
        //.sync_duration_den = 10,

        .video_on_pixel = VIDEO_ON_PIXEL,
        .video_on_line  = VIDEO_ON_LINE,

        .sth1_hs_addr = 44,
        .sth1_he_addr = 2156,
        .sth1_vs_addr = 0,
        .sth1_ve_addr = V_PERIOD - 1,
        .stv1_hs_addr = 2100,
        .stv1_he_addr = 2164,
        .stv1_vs_addr = 3,
        .stv1_ve_addr = 5,

        .pol_cntl_addr = (0x0 << LCD_CPH1_POL) |(0x0 << LCD_HS_POL) | (0x1 << LCD_VS_POL),
        .inv_cnt_addr = (0<<LCD_INV_EN) | (0<<LCD_INV_CNT),
        .tcon_misc_sel_addr = (1<<LCD_STV1_SEL) | (1<<LCD_STV2_SEL),
        .dual_port_cntl_addr = (1<<LCD_TTL_SEL) | (1<<LCD_ANALOG_SEL_CPH3) | (1<<LCD_ANALOG_3PHI_CLK_SEL) | (0<<LCD_RGB_SWP) | (0<<LCD_BIT_SWP),
    },

    .lcd_sequence = {
        .clock_enable_delay        = CLOCK_ENABLE_DELAY,
		.clock_disable_delay       = CLOCK_DISABLE_DELAY,
		.pwm_enable_delay          = PWM_ENABLE_DELAY,
		.pwm_disable_delay         = PWM_DISABLE_DELAY,
		.panel_power_on_delay      = PANEL_POWER_ON_DELAY,
		.panel_power_off_delay     = PANEL_POWER_OFF_DELAY,
		.backlight_power_on_delay  = BACKLIGHT_POWER_ON_DELAY,
		.backlight_power_off_delay = BACKLIGHT_POWER_OFF_DELAY,
    },

    .lvds_mlvds_config = {
        .lvds_config = &lcd_lvds_config,
        .lvds_phy_control = &lcd_lvds_phy_control,
    },
};


#ifdef CONFIG_USE_OF
static struct aml_lcd_platform m6tv_lvds_device = {
    .lcd_conf = &m6tv_lvds_config,
};

#define AMLOGIC_LVDS_DRV_DATA ((kernel_ulong_t)&m6tv_lvds_device)

static const struct of_device_id lvds_dt_match[]={
	{	.compatible = "amlogic,lvds",
		.data		= (void *)AMLOGIC_LVDS_DRV_DATA
	},
	{},
};
#else
#define lvds_dt_match NULL
#endif
#ifdef CONFIG_USE_OF
static inline struct aml_lcd_platform *lvds_get_driver_data(struct platform_device *pdev)
{
	const struct of_device_id *match;

	if(pdev->dev.of_node) {
		//DBG_PRINT("***of_device: get lcd driver data.***\n");
		match = of_match_node(lvds_dt_match, pdev->dev.of_node);
		return (struct aml_lcd_platform *)match->data;
	}else
		printk("\n ERROR get data %d",__LINE__);
	return NULL;
}






#endif


static CLASS_ATTR(power, S_IWUSR | S_IRUGO, power_show, power_store);

static int lcd_probe(struct platform_device *pdev)
{
	struct aml_lcd_platform *pdata;
	int err;
#ifdef 	CONFIG_USE_OF
	pdata = lvds_get_driver_data(pdev);
#else
	pdata = pdev->dev.platform_data;
#endif
	pDev = (lcd_dev_t *)kmalloc(sizeof(lcd_dev_t), GFP_KERNEL);

	if (!pDev) {
	PRINT_INFO("[tcon]: Not enough memory.\n");
	return -ENOMEM;
	}

	//    extern Lcd_Config_t m6skt_lcd_config;
	//pDev->conf = m6tv_lvds_config;

	pDev->conf = *(Lcd_Config_t *)(pdata->lcd_conf);        //*(Lcd_Config_t *)(s->start);

#ifdef CONFIG_USE_OF
	_get_lcd_default_config(pdev);
#endif

	printk("LCD probe ok\n");

	_lcd_init(&pDev->conf);

	lcd_reboot_nb.notifier_call = lcd_reboot_notifier;
	err = register_reboot_notifier(&lcd_reboot_nb);
	if (err) {
		printk("notifier register lcd_reboot_notifier fail!\n");
	}

	return 0;
}

static int lcd_remove(struct platform_device *pdev)
{
    unregister_reboot_notifier(&lcd_reboot_nb);
    kfree(pDev);

    return 0;
}



#ifdef CONFIG_PM

static int lcd_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
    pr_info("%s\n", __func__);
    return 0;
}

static int lcd_drv_resume(struct platform_device *pdev)
{
    pr_info("%s\n", __func__);
    return 0;
}

#endif /* CONFIG_PM */

static struct platform_driver lcd_driver = {
    .probe      = lcd_probe,
    .remove     = lcd_remove,
    .driver     = {
        .name   = "mesonlcd",    // removed "tcon-dev"
        .of_match_table = lvds_dt_match,
    },
#ifdef CONFIG_PM
    .suspend    = lcd_drv_suspend,
    .resume     = lcd_drv_resume,
#endif

};

static int __init lcd_init(void)
{
	int ret;
    printk("TV LCD driver init\n");
    if (platform_driver_register(&lcd_driver)) {
        PRINT_INFO("failed to register tcon driver module\n");
        return -ENODEV;
    }

    aml_lcd_clsp = class_create(THIS_MODULE, "aml_lcd");
    ret = class_create_file(aml_lcd_clsp, &class_attr_power);

    return 0;
}

static void __exit lcd_exit(void)
{
    class_remove_file(aml_lcd_clsp, &class_attr_power);
    class_destroy(aml_lcd_clsp);
    platform_driver_unregister(&lcd_driver);
}

static  int __init lvds_boot_para_setup(char *s)
{
    unsigned char* ptr;
    unsigned char flag_buf[16];
    int i;
    printk("LVDS boot args: %s\n", s);
    if(strstr(s, "10bit")){
		bit_num_flag = 0;
        bit_num = 0;
    }
    if(strstr(s, "8bit")){
		bit_num_flag = 0;
        bit_num = 1;
    }
    if(strstr(s, "6bit")){
		bit_num_flag = 0;
        bit_num = 2;
    }
    if(strstr(s, "4bit")){
		bit_num_flag = 0;
        bit_num = 3;
    }
    if(strstr(s, "jeida")){
		lvds_repack_flag = 0;
        lvds_repack = 0;
    }
    if(strstr(s, "port_reverse")){
		port_reverse_flag = 0;
        port_reverse = 0;
    }
    if(strstr(s, "flaga")){
        i=0;
        ptr=strstr(s,"flaga")+5;
        while((*ptr) && ((*ptr)!=',') && (i<10)){
            flag_buf[i]=*ptr;
            ptr++; i++;
        }
        flag_buf[i]=0;
        flaga = simple_strtoul(flag_buf, NULL, 10);
    }
    if(strstr(s, "flagb")){
        i=0;
        ptr=strstr(s,"flagb")+5;
        while((*ptr) && ((*ptr)!=',') && (i<10)){
            flag_buf[i]=*ptr;
            ptr++; i++;
        }
        flag_buf[i]=0;
        flagb = simple_strtoul(flag_buf, NULL, 10);
    }
    if(strstr(s, "flagc")){
        i=0;
        ptr=strstr(s,"flagc")+5;
        while((*ptr) && ((*ptr)!=',') && (i<10)){
            flag_buf[i]=*ptr;
            ptr++; i++;
        }
        flag_buf[i]=0;
        flagc = simple_strtoul(flag_buf, NULL, 10);
    }
    if(strstr(s, "flagd")){
        i=0;
        ptr=strstr(s,"flagd")+5;
        while((*ptr) && ((*ptr)!=',') && (i<10)){
            flag_buf[i]=*ptr;
            ptr++; i++;
        }
        flag_buf[i]=0;
        flagd = simple_strtoul(flag_buf, NULL, 10);
    }
    return 0;
}
__setup("lvds=",lvds_boot_para_setup);

MODULE_PARM_DESC(cur_lvds_index, "\n cur_lvds_index \n");
module_param(cur_lvds_index, int, 0664);

MODULE_PARM_DESC(pn_swap, "\n pn_swap \n");
module_param(pn_swap, int, 0664);

MODULE_PARM_DESC(dual_port, "\n dual_port \n");
module_param(dual_port, int, 0664);

MODULE_PARM_DESC(bit_num, "\n bit_num \n");
module_param(bit_num, int, 0664);

MODULE_PARM_DESC(lvds_repack, "\n lvds_repack \n");
module_param(lvds_repack, int, 0664);

MODULE_PARM_DESC(port_reverse, "\n port_reverse \n");
module_param(port_reverse, int, 0664);

MODULE_PARM_DESC(flaga, "\n flaga \n");
module_param(flaga, int, 0664);

MODULE_PARM_DESC(flagb, "\n flagb \n");
module_param(flagb, int, 0664);

MODULE_PARM_DESC(flagc, "\n flagc \n");
module_param(flagc, int, 0664);

MODULE_PARM_DESC(flagd, "\n flagd \n");
module_param(flagd, int, 0664);

EXPORT_SYMBOL(flaga);
EXPORT_SYMBOL(flagb);
EXPORT_SYMBOL(flagc);
EXPORT_SYMBOL(flagd);

subsys_initcall(lcd_init);
module_exit(lcd_exit);

MODULE_DESCRIPTION("Meson LCD Panel Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Amlogic, Inc.");
