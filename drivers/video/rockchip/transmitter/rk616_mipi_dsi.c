/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 * drivers/video/display/transmitter/rk616_mipi_dsi.c
 * author: hhb@rock-chips.com
 * create date: 2013-07-17
 * debug sys/kernel/debug/rk616/mipi
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

//config
#define MIPI_DSI_REGISTER_IO	1
#define CONFIG_MIPI_DSI_LINUX   1
//#define CONFIG_MIPI_DSI_FT 		1
//#define CONFIG_MFD_RK616   		1

#ifdef CONFIG_MIPI_DSI_LINUX
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rk616.h>
#include <linux/rk_fb.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <asm/div64.h>

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include<linux/earlysuspend.h>
#include <linux/regulator/machine.h>
#include <plat/clock.h>
#else
#include "ft_lcd.h"
#endif
#include "mipi_dsi.h"
#include "rk616_mipi_dsi.h"

#if 0
#define	MIPI_DBG(x...)	printk(KERN_INFO x)
#else
#ifdef CONFIG_MIPI_DSI_FT
#define	MIPI_DBG(...)    \
    do\
    {\
        printf(__VA_ARGS__);\
        printf("\n");\
    }while(0);
#else
#define	MIPI_DBG(x...)  
#endif    /* end of CONFIG_MIPI_DSI_FT */
#endif

#ifdef CONFIG_MIPI_DSI_LINUX
#define	MIPI_TRACE(x...)	printk(KERN_INFO x)
#else
#define	MIPI_TRACE(...)    \
    do\
    {\
        printf(__VA_ARGS__);\
        printf("\n");\
    }while(0);
    
#endif



/*
*			 Driver Version Note
*
*v1.0 : this driver is mipi dsi driver of rockchip;
*v1.1 : add FT code 
*v1.2 : add rk_mipi_dsi_init_lite() for mclk variation
*v1.3 : add clk_notifier function for mclk variation
*/
#define RK_MIPI_DSI_VERSION_AND_TIME  "rockchip mipi_dsi v1.3 2013-08-08"



#ifdef CONFIG_MFD_RK616
static struct mfd_rk616 *dsi_rk616;
static struct rk29fb_screen *g_rk29fd_screen = NULL;
#endif
static struct dsi gDsi;
static struct mipi_dsi_ops rk_mipi_dsi_ops;
static struct mipi_dsi_screen *g_screen = NULL;

#ifdef CONFIG_MIPI_DSI_FT
#define udelay 		DRVDelayUs
#define msleep 		DelayMs_nops
static u32 fre_to_period(u32 fre);
#endif
static int rk_mipi_dsi_is_active(void);
static int rk_mipi_dsi_enable_hs_clk(u32 enable);
static int rk_mipi_dsi_enable_video_mode(u32 enable);
static int rk_mipi_dsi_enable_command_mode(u32 enable);
static int rk_mipi_dsi_send_dcs_packet(unsigned char regs[], u32 n);

static int dsi_read_reg(u16 reg, u32 *pval)
{
#ifdef CONFIG_MIPI_DSI_FT
	return JETTA_ReadControlRegister(reg, pval);
#else
	return dsi_rk616->read_dev(dsi_rk616, reg, pval);
#endif
}


static int dsi_write_reg(u16 reg, u32 *pval)
{
#ifdef CONFIG_MIPI_DSI_FT
	return JETTA_WriteControlRegister(reg, *pval);
#else
	return dsi_rk616->write_dev(dsi_rk616, reg, pval);
#endif
}

static int dsi_get_bits(u32 reg) {

	u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	bits = (1 << bits) - 1;
	dsi_read_reg(reg_addr, &val);    
	val >>= offset;
	val &= bits;
	return val;
}

static int dsi_set_bits(u32 data, u32 reg) {

	u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	bits = (1 << bits) - 1;
	
	dsi_read_reg(reg_addr, &val);
	val &= ~(bits << offset);
	val |= (data & bits) << offset;
	dsi_write_reg(reg_addr, &val);
	if(data > bits) {
		MIPI_TRACE("%s error reg_addr:0x%04x, offset:%d, bits:0x%04x, value:0x%04x\n", 
				__func__, reg_addr, offset, bits, data);
	}
	return 0;
}

static int rk_mipi_dsi_phy_set_gotp(u32 offset, int n) {
	
	u32 val = 0, temp = 0, Tlpx = 0;
	u32 ddr_clk = gDsi.phy.ddr_clk;
	u32 Ttxbyte_clk = gDsi.phy.Ttxbyte_clk;
	u32 Tsys_clk = gDsi.phy.Tsys_clk;
	u32 Ttxclkesc = gDsi.phy.Ttxclkesc;
	
	switch(offset) {
		case DPHY_CLOCK_OFFSET:
			MIPI_DBG("******set DPHY_CLOCK_OFFSET gotp******\n");
			break;
		case DPHY_LANE0_OFFSET:
			MIPI_DBG("******set DPHY_LANE0_OFFSET gotp******\n");
			break;
		case DPHY_LANE1_OFFSET:
			MIPI_DBG("******set DPHY_LANE1_OFFSET gotp******\n");
			break;
		case DPHY_LANE2_OFFSET:
			MIPI_DBG("******set DPHY_LANE2_OFFSET gotp******\n");
			break;
		case DPHY_LANE3_OFFSET:
			MIPI_DBG("******set DPHY_LANE3_OFFSET gotp******\n");
			break;
		default:
			break;					
	}
	
	if(ddr_clk < 110 * MHz)
		val = 0;
	else if(ddr_clk < 150 * MHz)
		val = 1;
	else if(ddr_clk < 200 * MHz)
		val = 2;
	else if(ddr_clk < 250 * MHz)
		val = 3;
	else if(ddr_clk < 300 * MHz)
		val = 4;
	else if(ddr_clk < 400 * MHz)
		val = 5;		
	else if(ddr_clk < 500 * MHz)
		val = 6;		
	else if(ddr_clk < 600 * MHz)
		val = 7;		
	else if(ddr_clk < 700 * MHz)
		val = 8;
	else if(ddr_clk < 800 * MHz)
		val = 9;		
	else if(ddr_clk <= 1000 * MHz)
		val = 10;	
	dsi_set_bits(val, reg_ths_settle + offset);
	
	if(ddr_clk < 110 * MHz)
		val = 0x20;
	else if(ddr_clk < 150 * MHz)
		val = 0x06;
	else if(ddr_clk < 200 * MHz)
		val = 0x18;
	else if(ddr_clk < 250 * MHz)
		val = 0x05;
	else if(ddr_clk < 300 * MHz)
		val = 0x51;
	else if(ddr_clk < 400 * MHz)
		val = 0x64;		
	else if(ddr_clk < 500 * MHz)
		val = 0x59;		
	else if(ddr_clk < 600 * MHz)
		val = 0x6a;		
	else if(ddr_clk < 700 * MHz)
		val = 0x3e;
	else if(ddr_clk < 800 * MHz)
		val = 0x21;		
	else if(ddr_clk <= 1000 * MHz)
		val = 0x09;								
	dsi_set_bits(val, reg_hs_ths_prepare + offset);
	
	if(offset != DPHY_CLOCK_OFFSET) {
	
		if(ddr_clk < 110 * MHz)
			val = 2;
		else if(ddr_clk < 150 * MHz)
			val = 3;
		else if(ddr_clk < 200 * MHz)
			val = 4;
		else if(ddr_clk < 250 * MHz)
			val = 5;
		else if(ddr_clk < 300 * MHz)
			val = 6;
		else if(ddr_clk < 400 * MHz)
			val = 7;		
		else if(ddr_clk < 500 * MHz)
			val = 7;		
		else if(ddr_clk < 600 * MHz)
			val = 8;		
		else if(ddr_clk < 700 * MHz)
			val = 8;
		else if(ddr_clk < 800 * MHz)
			val = 9;		
		else if(ddr_clk <= 1000 * MHz)
			val = 9;	
	} else {
	
		if(ddr_clk < 110 * MHz)
			val = 0x16;
		else if(ddr_clk < 150 * MHz)
			val = 0x16;
		else if(ddr_clk < 200 * MHz)
			val = 0x17;
		else if(ddr_clk < 250 * MHz)
			val = 0x17;
		else if(ddr_clk < 300 * MHz)
			val = 0x18;
		else if(ddr_clk < 400 * MHz)
			val = 0x19;		
		else if(ddr_clk < 500 * MHz)
			val = 0x1b;		
		else if(ddr_clk < 600 * MHz)
			val = 0x1d;		
		else if(ddr_clk < 700 * MHz)
			val = 0x1e;
		else if(ddr_clk < 800 * MHz)
			val = 0x1f;		
		else if(ddr_clk <= 1000 * MHz)
			val = 0x20;	
	}				
	dsi_set_bits(val, reg_hs_the_zero + offset);
	
	if(ddr_clk < 110 * MHz)
		val = 0x22;
	else if(ddr_clk < 150 * MHz)
		val = 0x45;
	else if(ddr_clk < 200 * MHz)
		val = 0x0b;
	else if(ddr_clk < 250 * MHz)
		val = 0x16;
	else if(ddr_clk < 300 * MHz)
		val = 0x2c;
	else if(ddr_clk < 400 * MHz)
		val = 0x33;		
	else if(ddr_clk < 500 * MHz)
		val = 0x4e;		
	else if(ddr_clk < 600 * MHz)
		val = 0x3a;		
	else if(ddr_clk < 700 * MHz)
		val = 0x6a;
	else if(ddr_clk < 800 * MHz)
		val = 0x29;		
	else if(ddr_clk <= 1000 * MHz)
		val = 0x21;
	dsi_set_bits(val, reg_hs_ths_trail + offset);
	val = 120000 / Ttxbyte_clk + 1;
	MIPI_DBG("reg_hs_ths_exit: %d, %d\n", val, val*Ttxbyte_clk/1000);
	dsi_set_bits(val, reg_hs_ths_exit + offset);
	
	if(offset == DPHY_CLOCK_OFFSET) {
		val = (60000 + 52*gDsi.phy.UI) / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_post: %d, %d\n", val, val*Ttxbyte_clk/1000);
		dsi_set_bits(val, reg_hs_tclk_post + offset);
		val = 10*gDsi.phy.UI / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_pre: %d, %d\n", val, val*Ttxbyte_clk/1000);	
		dsi_set_bits(val, reg_hs_tclk_pre + offset);
	}

	val = 1010000000 / Tsys_clk + 1;
	MIPI_DBG("reg_hs_twakup: %d, %d\n", val, val*Tsys_clk/1000);
	if(val > 0x3ff) {
		val = 0x2ff;
		MIPI_DBG("val is too large, 0x3ff is the largest\n");	
	}	
	temp = (val >> 8) & 0x03;
	val &= 0xff;	
	dsi_set_bits(temp, reg_hs_twakup_h + offset);	
	dsi_set_bits(val, reg_hs_twakup_l + offset);
	
	if(Ttxclkesc > 50000) {
		val = 2*Ttxclkesc;
		MIPI_DBG("Ttxclkesc:%d\n", Ttxclkesc);
	}
	val = val / Ttxbyte_clk;
	Tlpx = val*Ttxbyte_clk;
	MIPI_DBG("reg_hs_tlpx: %d, %d\n", val, Tlpx);
	val -= 2;
	dsi_set_bits(val, reg_hs_tlpx + offset);
	
	Tlpx = 2*Ttxclkesc;
	val = 4*Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_go: %d, %d\n", val, val*Ttxclkesc);
	dsi_set_bits(val, reg_hs_tta_go + offset);
	val = 3 * Tlpx / 2 / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_sure: %d, %d\n", val, val*Ttxclkesc);	
	dsi_set_bits(val, reg_hs_tta_sure + offset);
	val = 5 * Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_wait: %d, %d\n", val, val*Ttxclkesc);
	dsi_set_bits(val, reg_hs_tta_wait + offset);
	return 0;
}


static int rk_mipi_dsi_phy_power_up(void) {

	u32 val = 0xe4;       
	dsi_write_reg(DPHY_REGISTER1, &val);
	
	switch(gDsi.host.lane) {
		case 4:
			dsi_set_bits(1, lane_en_3);
		case 3:
			dsi_set_bits(1, lane_en_2);
		case 2:
			dsi_set_bits(1, lane_en_1);
		case 1:
			dsi_set_bits(1, lane_en_0);
			dsi_set_bits(1, lane_en_ck);
			break;
		default:
			break;	
	}

	val = 0xe0;
	dsi_write_reg(DPHY_REGISTER1, &val);
	udelay(10);

	val = 0x1e;
	dsi_write_reg(DPHY_REGISTER20, &val);
	val = 0x1f;
	dsi_write_reg(DPHY_REGISTER20, &val);
	return 0;
}

static int rk_mipi_dsi_phy_power_down(void) {

	u32 val = 0X01;   
	dsi_write_reg(DPHY_REGISTER0, &val);
	
	val = 0xe3;
	dsi_write_reg(DPHY_REGISTER1, &val);
	return 0;
}

static void rk_mipi_dsi_set_hs_clk(void) {
	dsi_set_bits(gDsi.phy.prediv, reg_prediv);
	dsi_set_bits(gDsi.phy.fbdiv & 0xff, reg_fbdiv);
	dsi_set_bits((gDsi.phy.fbdiv >> 8) & 0x01, reg_fbdiv_8);
}



static int rk_mipi_dsi_phy_init(void *array, int n) {

	u32 val = 0;
	//DPHY init
	rk_mipi_dsi_set_hs_clk();
	
	val = 0x11;
	dsi_write_reg(RK_ADDR(0x06), &val);
	val = 0x11;
	dsi_write_reg(RK_ADDR(0x07), &val);
	val = 0xcc;
	dsi_write_reg(RK_ADDR(0x09), &val);	
	
#if 0
	val = 0x4e;
	dsi_write_reg(RK_ADDR(0x08), &val);
	val = 0x84;
	dsi_write_reg(RK_ADDR(0x0a), &val);
#endif

	/*reg1[4] 0: enable a function of "pll phase for serial data being captured inside analog part" 
	          1: disable it 
	          we disable it here because reg5[6:4] is not compatible with the HS speed. 		
	*/
#if 1
	if(gDsi.phy.ddr_clk >= 800*MHz) {
		val = 0x30;
		dsi_write_reg(RK_ADDR(0x05), &val);
	} else {
		dsi_set_bits(1, reg_da_ppfc);
	}
#endif
			
	switch(gDsi.host.lane) {
		case 4:
			rk_mipi_dsi_phy_set_gotp(DPHY_LANE3_OFFSET, n);
		case 3:
			rk_mipi_dsi_phy_set_gotp(DPHY_LANE2_OFFSET, n);
		case 2:
			rk_mipi_dsi_phy_set_gotp(DPHY_LANE1_OFFSET, n);
		case 1:
			rk_mipi_dsi_phy_set_gotp(DPHY_LANE0_OFFSET, n);
			rk_mipi_dsi_phy_set_gotp(DPHY_CLOCK_OFFSET, n);
			break;
		default:
			break;	
	}	
	return 0;
}

static int rk_mipi_dsi_host_power_up(void) {
	int ret = 0;
	u32 val = 0;
	
	dsi_set_bits(1, shutdownz);
	
	val = 10;
	while(!dsi_get_bits(phylock) && val--) {
		udelay(10);
	};
	if(val == 0) {
		ret = -1;
		MIPI_TRACE("%s:phylock fail\n", __func__);	
	}		
	val = 10;
	while(!dsi_get_bits(phystopstateclklane) && val--) {
		udelay(10);
	};
	return ret;
}

static int rk_mipi_dsi_host_power_down(void) {	
	rk_mipi_dsi_enable_video_mode(0);
	rk_mipi_dsi_enable_hs_clk(0);
	dsi_set_bits(0, shutdownz);
	return 0;
}


static int rk_mipi_dsi_host_init(void *array, int n) {

	u32 val = 0, bytes_px = 0;
	struct mipi_dsi_screen *screen = array;
	u32 decimals = gDsi.phy.Ttxbyte_clk, temp = 0, i = 0;
	u32 m = 1, lane = gDsi.host.lane, Tpclk = gDsi.phy.Tpclk, Ttxbyte_clk = gDsi.phy.Ttxbyte_clk;
#ifdef CONFIG_MFD_RK616	
	val = 0x04000000;
	dsi_write_reg(CRU_CRU_CLKSEL1_CON, &val);
#endif	
	dsi_set_bits(gDsi.host.lane - 1, n_lanes);
	dsi_set_bits(gDsi.vid, dpi_vid);
	
	switch(screen->face) {
		case OUT_P888:
			dsi_set_bits(7, dpi_color_coding);
			bytes_px = 3;
			break;
		case OUT_D888_P666:
		case OUT_P666:
			dsi_set_bits(3, dpi_color_coding);
			dsi_set_bits(1, en18_loosely);
			bytes_px = 3;
			break;
		case OUT_P565:
			dsi_set_bits(0, dpi_color_coding);
			bytes_px = 2;
		default:
			break;
	}
	
	dsi_set_bits(!screen->pin_hsync, hsync_active_low);
	dsi_set_bits(!screen->pin_vsync, vsync_active_low);
	dsi_set_bits(screen->pin_den, dataen_active_low);
	dsi_set_bits(1, colorm_active_low);
	dsi_set_bits(1, shutd_active_low);
	
	dsi_set_bits(gDsi.host.video_mode, vid_mode_type);	  //burst mode  //need to expand
	switch(gDsi.host.video_mode) {
		case VM_BM:
			dsi_set_bits(screen->x_res, vid_pkt_size);
			break;
		case VM_NBMWSE:
		case VM_NBMWSP:
			for(i = 8; i < 32; i++){
				temp = i * lane * Tpclk % Ttxbyte_clk;
				if(decimals > temp) {
					decimals = temp;
					m = i;
				}
				if(decimals == 0)
					break;
			}
			dsi_set_bits(1, en_multi_pkt);
			dsi_set_bits(screen->x_res / m + 1, num_chunks);
			dsi_set_bits(m, vid_pkt_size);
			temp = m * lane * Tpclk / Ttxbyte_clk - m * bytes_px;
			MIPI_DBG("%s:%d, %d\n", __func__, m, temp);
			if(temp >= 12) {
				dsi_set_bits(1, en_null_pkt);
				dsi_set_bits(temp - 12, null_pkt_size);
			}
			break;
		default:
			break;
	}	
	
	val = 0x0;
	dsi_write_reg(CMD_MODE_CFG, &val);
	
	dsi_set_bits(gDsi.phy.Tpclk * (screen->x_res + screen->left_margin + screen->hsync_len + screen->right_margin) \
									/ gDsi.phy.Ttxbyte_clk, hline_time);
	dsi_set_bits(gDsi.phy.Tpclk * screen->left_margin / gDsi.phy.Ttxbyte_clk, hbp_time);
	dsi_set_bits(gDsi.phy.Tpclk * screen->hsync_len / gDsi.phy.Ttxbyte_clk, hsa_time);
	
	dsi_set_bits(screen->y_res, v_active_lines);
	dsi_set_bits(screen->lower_margin, vfp_lines);
	dsi_set_bits(screen->upper_margin, vbp_lines);
	dsi_set_bits(screen->vsync_len, vsa_lines);
	
	gDsi.phy.txclkesc = 20 * MHz;
	val = gDsi.phy.txbyte_clk / gDsi.phy.txclkesc + 1;
	gDsi.phy.txclkesc = gDsi.phy.txbyte_clk / val;
	dsi_set_bits(val, TX_ESC_CLK_DIVISION);
	
	dsi_set_bits(10, TO_CLK_DIVISION);
	dsi_set_bits(1000, lprx_to_cnt);
	dsi_set_bits(1000, hstx_to_cnt);	
	dsi_set_bits(100, phy_stop_wait_time);

	//dsi_set_bits(0, outvact_lpcmd_time);   //byte
	//dsi_set_bits(0, invact_lpcmd_time);
		
	dsi_set_bits(20, phy_hs2lp_time);
	dsi_set_bits(16, phy_lp2hs_time);	
	
	dsi_set_bits(10000, max_rd_time);
	dsi_set_bits(1, dpicolom);
	dsi_set_bits(1, dpishutdn);

	//disable all interrupt            
	val = 0x1fffff;
	dsi_write_reg(ERROR_MSK0, &val);
	val = 0x1ffff;
	dsi_write_reg(ERROR_MSK1, &val);

	dsi_set_bits(1, en_lp_hfp);
	//dsi_set_bits(1, en_lp_hbp);
	dsi_set_bits(1, en_lp_vact);
	dsi_set_bits(1, en_lp_vfp);
	dsi_set_bits(1, en_lp_vbp);
	dsi_set_bits(1, en_lp_vsa);
	
	//dsi_set_bits(1, frame_BTA_ack);
	//dsi_set_bits(1, phy_enableclk);
	//dsi_set_bits(0, phy_tx_triggers);
	//dsi_set_bits(1, phy_txexitulpslan);
	//dsi_set_bits(1, phy_txexitulpsclk);
	return 0;
}

/*
	mipi protocol layer definition
*/
static int rk_mipi_dsi_init(void *array, u32 n) {

	u8 dcs[4] = {0};
	u32 decimals = 1000, i = 0, pre = 0;
	struct mipi_dsi_screen *screen = array;
	
	if(!screen)
		return -1;
	
	if(screen->type != SCREEN_MIPI) {
		MIPI_TRACE("only mipi dsi lcd is supported\n");
		return -1;
	}
	
#ifdef CONFIG_MIPI_DSI_FT
	gDsi.phy.pclk = screen->pixclock;
	gDsi.phy.ref_clk = MIPI_DSI_MCLK;
#else
	gDsi.phy.Tpclk = rk_fb_get_prmry_screen_pixclock();
	if(dsi_rk616->mclk)
		gDsi.phy.ref_clk = clk_get_rate(dsi_rk616->mclk);
	else
		gDsi.phy.ref_clk = 24 * MHz;	
#endif

	gDsi.phy.sys_clk = gDsi.phy.ref_clk;
	
	if((screen->hs_tx_clk <= 80 * MHz) || (screen->hs_tx_clk >= 1000 * MHz))
		gDsi.phy.ddr_clk = 1000 * MHz;    //default is 1HGz
	else
		gDsi.phy.ddr_clk = screen->hs_tx_clk;	
	
	if(n != 0) {
		gDsi.phy.ddr_clk = n;
	} 

	decimals = gDsi.phy.ref_clk;
	for(i = 1; i < 6; i++) {
		pre = gDsi.phy.ref_clk / i;
		if((decimals > (gDsi.phy.ddr_clk % pre)) && (gDsi.phy.ddr_clk / pre < 512)) {
			decimals = gDsi.phy.ddr_clk % pre;
			gDsi.phy.prediv = i;
			gDsi.phy.fbdiv = gDsi.phy.ddr_clk / pre;
		}	
		if(decimals == 0) 
			break;		
	}

	MIPI_DBG("prediv:%d, fbdiv:%d\n", gDsi.phy.prediv, gDsi.phy.fbdiv);
	gDsi.phy.ddr_clk = gDsi.phy.ref_clk / gDsi.phy.prediv * gDsi.phy.fbdiv;	
	gDsi.phy.txbyte_clk = gDsi.phy.ddr_clk / 8;
	
	gDsi.phy.txclkesc = 20 * MHz;        // < 20MHz
	gDsi.phy.txclkesc = gDsi.phy.txbyte_clk / (gDsi.phy.txbyte_clk / gDsi.phy.txclkesc + 1);
#ifdef CONFIG_MIPI_DSI_FT	
	gDsi.phy.Tpclk = fre_to_period(gDsi.phy.pclk);
	gDsi.phy.Ttxclkesc = fre_to_period(gDsi.phy.txclkesc);
	gDsi.phy.Tsys_clk = fre_to_period(gDsi.phy.sys_clk);
	gDsi.phy.Tddr_clk = fre_to_period(gDsi.phy.ddr_clk);
	gDsi.phy.Ttxbyte_clk = fre_to_period(gDsi.phy.txbyte_clk);	
#else
	gDsi.phy.pclk = div_u64(1000000000000llu, gDsi.phy.Tpclk);
	gDsi.phy.Ttxclkesc = div_u64(1000000000000llu, gDsi.phy.txclkesc);
	gDsi.phy.Tsys_clk = div_u64(1000000000000llu, gDsi.phy.sys_clk);
	gDsi.phy.Tddr_clk = div_u64(1000000000000llu, gDsi.phy.ddr_clk);
	gDsi.phy.Ttxbyte_clk = div_u64(1000000000000llu, gDsi.phy.txbyte_clk);	
#endif
	
	gDsi.phy.UI = gDsi.phy.Tddr_clk;
	gDsi.vid = 0;
	
	if(screen->dsi_lane > 0 && screen->dsi_lane <= 4)
		gDsi.host.lane = screen->dsi_lane;
	else
		gDsi.host.lane = 4;
		
	gDsi.host.video_mode = VM_BM;
	
	MIPI_DBG("UI:%d\n", gDsi.phy.UI);	
	MIPI_DBG("ref_clk:%d\n", gDsi.phy.ref_clk);
	MIPI_DBG("pclk:%d, Tpclk:%d\n", gDsi.phy.pclk, gDsi.phy.Tpclk);
	MIPI_DBG("sys_clk:%d, Tsys_clk:%d\n", gDsi.phy.sys_clk, gDsi.phy.Tsys_clk);
	MIPI_DBG("ddr_clk:%d, Tddr_clk:%d\n", gDsi.phy.ddr_clk, gDsi.phy.Tddr_clk);
	MIPI_DBG("txbyte_clk:%d, Ttxbyte_clk:%d\n", gDsi.phy.txbyte_clk, gDsi.phy.Ttxbyte_clk);
	MIPI_DBG("txclkesc:%d, Ttxclkesc:%d\n", gDsi.phy.txclkesc, gDsi.phy.Ttxclkesc);
	
	rk_mipi_dsi_phy_power_up();
	rk_mipi_dsi_host_power_up();
	rk_mipi_dsi_phy_init(screen, n);
	rk_mipi_dsi_host_init(screen, n);

	if(!screen->init) {
		rk_mipi_dsi_enable_hs_clk(1);
#ifndef CONFIG_MIPI_DSI_FT		
		dcs[0] = HSDT;
		dcs[1] = dcs_exit_sleep_mode; 
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_on;
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(10);
#endif	
	} else {
		screen->init();
	}
	
	/*
		After the core reset, DPI waits for the first VSYNC active transition to start signal sampling, including
		pixel data, and preventing image transmission in the middle of a frame.
	*/
	dsi_set_bits(0, shutdownz);
	rk_mipi_dsi_enable_video_mode(1);
#ifdef CONFIG_MFD_RK616
	rk616_display_router_cfg(dsi_rk616, g_rk29fd_screen, 0);
#endif
	dsi_set_bits(1, shutdownz);
	return 0;
}


int rk_mipi_dsi_init_lite(void) {

	u32 decimals = 1000, i = 0, pre = 0, ref_clk = 0;
	struct mipi_dsi_screen *screen = g_screen;
	
	if(!screen)
		return -1;
	
	if(rk_mipi_dsi_is_active() == 0)
		return -1;
	
	ref_clk = clk_get_rate(dsi_rk616->mclk);
	if(gDsi.phy.ref_clk == ref_clk)
		return -1;
		
	gDsi.phy.ref_clk = ref_clk;
	gDsi.phy.sys_clk = gDsi.phy.ref_clk;
	
	if((screen->hs_tx_clk <= 80 * MHz) || (screen->hs_tx_clk >= 1000 * MHz))
		gDsi.phy.ddr_clk = 1000 * MHz;    //default is 1HGz
	else
		gDsi.phy.ddr_clk = screen->hs_tx_clk;
		
	decimals = gDsi.phy.ref_clk;
	for(i = 1; i < 6; i++) {
		pre = gDsi.phy.ref_clk / i;
		if((decimals > (gDsi.phy.ddr_clk % pre)) && (gDsi.phy.ddr_clk / pre < 512)) {
			decimals = gDsi.phy.ddr_clk % pre;
			gDsi.phy.prediv = i;
			gDsi.phy.fbdiv = gDsi.phy.ddr_clk / pre;
		}	
		if(decimals == 0) 
			break;		
	}

	MIPI_DBG("prediv:%d, fbdiv:%d\n", gDsi.phy.prediv, gDsi.phy.fbdiv);
	gDsi.phy.ddr_clk = gDsi.phy.ref_clk / gDsi.phy.prediv * gDsi.phy.fbdiv;	
	gDsi.phy.txbyte_clk = gDsi.phy.ddr_clk / 8;
	
	gDsi.phy.txclkesc = 20 * MHz;        // < 20MHz
	gDsi.phy.txclkesc = gDsi.phy.txbyte_clk / (gDsi.phy.txbyte_clk / gDsi.phy.txclkesc + 1);
	
	gDsi.phy.pclk = div_u64(1000000000000llu, gDsi.phy.Tpclk);
	gDsi.phy.Ttxclkesc = div_u64(1000000000000llu, gDsi.phy.txclkesc);
	gDsi.phy.Tsys_clk = div_u64(1000000000000llu, gDsi.phy.sys_clk);
	gDsi.phy.Tddr_clk = div_u64(1000000000000llu, gDsi.phy.ddr_clk);
	gDsi.phy.Ttxbyte_clk = div_u64(1000000000000llu, gDsi.phy.txbyte_clk);
	gDsi.phy.UI = gDsi.phy.Tddr_clk;
		
	MIPI_DBG("UI:%d\n", gDsi.phy.UI);	
	MIPI_DBG("ref_clk:%d\n", gDsi.phy.ref_clk);
	MIPI_DBG("pclk:%d, Tpclk:%d\n", gDsi.phy.pclk, gDsi.phy.Tpclk);
	MIPI_DBG("sys_clk:%d, Tsys_clk:%d\n", gDsi.phy.sys_clk, gDsi.phy.Tsys_clk);
	MIPI_DBG("ddr_clk:%d, Tddr_clk:%d\n", gDsi.phy.ddr_clk, gDsi.phy.Tddr_clk);
	MIPI_DBG("txbyte_clk:%d, Ttxbyte_clk:%d\n", gDsi.phy.txbyte_clk, gDsi.phy.Ttxbyte_clk);
	MIPI_DBG("txclkesc:%d, Ttxclkesc:%d\n", gDsi.phy.txclkesc, gDsi.phy.Ttxclkesc);
		
	rk_mipi_dsi_host_power_down();
	rk_mipi_dsi_phy_power_down();
	rk_mipi_dsi_phy_power_up();
	rk_mipi_dsi_phy_init(screen, 0);
	//rk_mipi_dsi_host_power_up();
	//rk_mipi_dsi_host_init(screen, 0);
	//dsi_set_bits(0, shutdownz);
	rk_mipi_dsi_enable_hs_clk(1);
	rk_mipi_dsi_enable_video_mode(1);
	dsi_set_bits(1, shutdownz);
	return 0;
}


static int rk_mipi_dsi_enable_video_mode(u32 enable) {

	dsi_set_bits(enable, en_video_mode);
	return 0;
}


static int rk_mipi_dsi_enable_command_mode(u32 enable) {

	dsi_set_bits(enable, en_cmd_mode);
	return 0;
}

static int rk_mipi_dsi_enable_hs_clk(u32 enable) {

	dsi_set_bits(enable, phy_txrequestclkhs);
	return 0;
}

static int rk_mipi_dsi_is_active(void) {

	return dsi_get_bits(shutdownz);
}

static int rk_mipi_dsi_send_packet(u32 type, unsigned char regs[], u32 n) {

	u32 data = 0, i = 0, j = 0, flag = 0;
	
	if((n == 0) && (type != DTYPE_GEN_SWRITE_0P))
		return -1;
	
	if(dsi_get_bits(gen_cmd_full) == 1) {
		MIPI_TRACE("gen_cmd_full\n");
		return -1;
	}
	
	if(dsi_get_bits(en_video_mode) == 1) {
		rk_mipi_dsi_enable_video_mode(0);
		flag = 1;
	}
	rk_mipi_dsi_enable_command_mode(1);
	udelay(10);
	
	if(n <= 2) {
		if(type == DTYPE_GEN_SWRITE_0P)
			data = (gDsi.vid << 6) | (n << 4) | type;
		else 
			data = (gDsi.vid << 6) | ((n-1) << 4) | type;
		data |= regs[0] << 8;
		if(n == 2)
			data |= regs[1] << 16;
	} else {
		data = 0;
		for(i = 0; i < n; i++) {
			j = i % 4;
			data |= regs[i] << (j * 8);
			if(j == 3 || ((i + 1) == n)) {
				if(dsi_get_bits(gen_pld_w_full) == 1) {
					MIPI_TRACE("gen_pld_w_full :%d\n", i);
					break;
				}
				dsi_write_reg(GEN_PLD_DATA, &data);
				MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
				data = 0;
			}
		}
		data = (gDsi.vid << 6) | type;		
		data |= (n & 0xffff) << 8;
	}
	
	MIPI_DBG("write GEN_HDR:%08x\n", data);
	dsi_write_reg(GEN_HDR, &data);
	
	i = 10;
	while(!dsi_get_bits(gen_cmd_empty) && i--) {
		MIPI_DBG(".");
		udelay(10);
	}
	udelay(10);
	rk_mipi_dsi_enable_command_mode(0);
	if(flag == 1) {
		rk_mipi_dsi_enable_video_mode(1);
	}
	return 0;
}

static int rk_mipi_dsi_send_dcs_packet(unsigned char regs[], u32 n) {
	
	n -= 1;
	if(n <= 2) {
		if(n == 1)
			dsi_set_bits(regs[0], dcs_sw_0p_tx);
		else
			dsi_set_bits(regs[0], dcs_sw_1p_tx);
		rk_mipi_dsi_send_packet(DTYPE_DCS_SWRITE_0P, regs + 1, n);
	} else {
		dsi_set_bits(regs[0], dcs_lw_tx);
		rk_mipi_dsi_send_packet(DTYPE_DCS_LWRITE, regs + 1, n);
	}
	MIPI_DBG("***%s:%d command sent in %s size:%d\n", __func__, __LINE__, regs[0] ? "LP mode" : "HS mode", n);
	return 0;
}

static int rk_mipi_dsi_send_gen_packet(void *data, u32 n) {

	unsigned char *regs = data;
	n -= 1;
	if(n <= 2) {
		if(n == 2)
			dsi_set_bits(regs[0], gen_sw_2p_tx);
		else if(n == 1)
			dsi_set_bits(regs[0], gen_sw_1p_tx);
		else 
			dsi_set_bits(regs[0], gen_sw_0p_tx);	
		rk_mipi_dsi_send_packet(DTYPE_GEN_SWRITE_0P, regs + 1, n);
	} else {
		dsi_set_bits(regs[0], gen_lw_tx);
		rk_mipi_dsi_send_packet(DTYPE_GEN_LWRITE, regs + 1, n);
	}
	MIPI_DBG("***%s:%d command sent in %s size:%d\n", __func__, __LINE__, regs[0] ? "LP mode" : "HS mode", n);
	return 0;
}

static int rk_mipi_dsi_read_dcs_packet(unsigned char *data, u32 n) {
	//DCS READ 
	
	return 0;
}

static int rk_mipi_dsi_power_up(void) {

	rk_mipi_dsi_phy_power_up();
	rk_mipi_dsi_host_power_up();
	return 0;
}

static int rk_mipi_dsi_power_down(void) {

	rk_mipi_dsi_phy_power_down();
	rk_mipi_dsi_host_power_down();	
	return 0;
}

static int rk_mipi_dsi_get_id(void) {
	
	u32 id = 0;
	dsi_read_reg(VERSION, &id);
	return id;
}

static struct mipi_dsi_ops rk_mipi_dsi_ops = {
	.id = DWC_DSI_VERSION,
	.name = "rk_mipi_dsi",
	.get_id = rk_mipi_dsi_get_id,
	.dsi_send_packet = rk_mipi_dsi_send_gen_packet,
	.dsi_send_dcs_packet = rk_mipi_dsi_send_dcs_packet,
	.dsi_read_dcs_packet = rk_mipi_dsi_read_dcs_packet,
	.dsi_enable_video_mode = rk_mipi_dsi_enable_video_mode,
	.dsi_enable_command_mode = rk_mipi_dsi_enable_command_mode,
	.dsi_enable_hs_clk = rk_mipi_dsi_enable_hs_clk,
	.dsi_is_active = rk_mipi_dsi_is_active,
	.power_up = rk_mipi_dsi_power_up,
	.power_down = rk_mipi_dsi_power_down,
	.dsi_init = rk_mipi_dsi_init,
};

/* the most top level of mipi dsi init */
static int rk_mipi_dsi_probe(void *array, int n) {
	
	int ret = 0;
	struct mipi_dsi_screen *screen = array;
	register_dsi_ops(&rk_mipi_dsi_ops);
	ret = dsi_probe_current_chip();
	if(ret) {
		MIPI_TRACE("mipi dsi probe fail\n");
		return -ENODEV;
	}	
	rk_mipi_dsi_init(screen, 0);	

	return 0;
}


#ifdef MIPI_DSI_REGISTER_IO
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>

int reg_proc_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)
{
	int ret = -1, i = 0;
	u32 read_val = 0;
	char *buf = kmalloc(count, GFP_KERNEL);
	char *data = buf;
	char str[32];
	char command = 0;
	u64 regs_val = 0;
	memset(buf, 0, count);
	ret = copy_from_user((void*)buf, buff, count);
	
	data = strstr(data, "-");
	if(data == NULL)
		goto reg_proc_write_exit;
	command = *(++data);
	
	switch(command) {
		case 'w':
			while(1) {
		
				data = strstr(data, "0x");
				if(data == NULL)
					goto reg_proc_write_exit;
				sscanf(data, "0x%llx", &regs_val);
				if((regs_val & 0xffff00000000ULL) == 0)
					goto reg_proc_write_exit;
				read_val = regs_val & 0xffffffff;
				dsi_write_reg(regs_val >> 32, &read_val);
				dsi_read_reg(regs_val >> 32, &read_val);
				regs_val &= 0xffffffff;
				if(read_val != regs_val)
					MIPI_TRACE("%s fail:0x%08x\n", __func__, read_val);	
				
				data += 3;
				msleep(1);	
			}
		
			break;
		case 'r':
				data = strstr(data, "0x");
				if(data == NULL)
					goto reg_proc_write_exit;
				sscanf(data, "0x%llx", &regs_val);
				dsi_read_reg((u16)regs_val, &read_val);
				MIPI_TRACE("*%04x : %08x\n", (u16)regs_val, read_val);
				msleep(1);	
			break;	
	
		case 's':
				while(*(++data) == ' ');
				sscanf(data, "%d", &read_val);
				if(read_val == 11)
					read_val = 11289600;
				else	
					read_val *= MHz;
				clk_set_rate(dsi_rk616->mclk, read_val);	
				//rk_mipi_dsi_init_lite();
			break;
		case 'd':
		case 'g':
		case 'c':
				while(*(++data) == ' ');
				i = 0;
				MIPI_TRACE("****%d:%d\n", data-buf, count);
				
				do {
					if(i > 31) {
						MIPI_TRACE("payload entry is larger than 32\n");
						break;
					}	
					sscanf(data, "%x,", str + i);        //-c 1,29,02,03,05,06,   > pro
					data = strstr(data, ",");
					if(data == NULL)
						break;
					data++;	
					i++;
				} while(1);
				read_val = i;
				
				i = 2;
				while(i--) {
					msleep(10);
					if(command == 'd')
						rk_mipi_dsi_send_dcs_packet(str, read_val);
					else
						rk_mipi_dsi_send_gen_packet(str, read_val);
				}	
				i = 1;
				while(i--) {
					msleep(1000);
				}
			break;
	
		default:
			break;
	}

reg_proc_write_exit:
	kfree(buf);
	msleep(20);	
 	return count;
}



int reg_proc_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	int i = 0;
	u32 val = 0;
	
	for(i = VERSION; i <= LP_CMD_TIM; i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = DPHY_REGISTER0; i <= DPHY_REGISTER4; i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}
	MIPI_TRACE("\n");
	i = DPHY_REGISTER20;
	dsi_read_reg(i, &val);
	MIPI_TRACE("%04x: %08x\n", i, val);
	msleep(1);
#if 1
	MIPI_TRACE("\n");
	for(i = DPHY_CLOCK_OFFSET >> 16; i <= ((DPHY_CLOCK_OFFSET + reg_hs_tta_wait) >> 16); i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = DPHY_LANE0_OFFSET >> 16; i <= ((DPHY_LANE0_OFFSET + reg_hs_tta_wait) >> 16); i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	for(i = DPHY_LANE1_OFFSET >> 16; i <= ((DPHY_LANE1_OFFSET + reg_hs_tta_wait) >> 16); i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	for(i = DPHY_LANE2_OFFSET >> 16; i <= ((DPHY_LANE2_OFFSET + reg_hs_tta_wait) >> 16); i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = DPHY_LANE3_OFFSET >> 16; i <= ((DPHY_LANE3_OFFSET + reg_hs_tta_wait) >> 16); i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}
	MIPI_TRACE("****************rk616 core:\n");
	for(i = 0; i <= 0x009c; i += 4) {
		dsi_read_reg(i, &val);
		MIPI_TRACE("%04x: %08x\n", i, val);
		msleep(1);
	}
	
#endif
	return -1;
}

int reg_proc_open(struct inode *inode, struct file *file)
{
	return 0;
}

int reg_proc_close(struct inode *inode, struct file *file)
{
	return 0;   
}

struct file_operations reg_proc_fops = {
	.owner = THIS_MODULE,
	.open = reg_proc_open,
	.release = reg_proc_close,
	.write = reg_proc_write,
	.read = reg_proc_read,
};

static int reg_proc_init(char *name)
{
	int ret = 0;
#if 1	
#ifdef CONFIG_MFD_RK616
	debugfs_create_file("mipi", S_IRUSR, dsi_rk616->debugfs_dir, dsi_rk616, &reg_proc_fops);
#endif	
#else
	static struct proc_dir_entry *reg_proc_entry;
  	reg_proc_entry = create_proc_entry(name, 0666, NULL);
	if(reg_proc_entry == NULL) {
		MIPI_TRACE("Couldn't create proc entry : %s!\n", name);
		ret = -ENOMEM;
		return ret ;
	}
	else {
		MIPI_TRACE("Create proc entry:%s success!\n", name);
		reg_proc_entry->proc_fops = &reg_proc_fops;
	}
#endif	
	return ret;
}

static int __init rk_mipi_dsi_reg(void)
{
	return reg_proc_init("mipi_dsi");
}
module_init(rk_mipi_dsi_reg);

#endif


#ifdef CONFIG_MIPI_DSI_FT
static struct mipi_dsi_screen ft_screen;

static u32 fre_to_period(u32 fre) {
	u32 interger = 0;
	u32 decimals = 0;
	interger = 1000000000UL / fre;
	decimals = 1000000000UL % fre;
	if(decimals <= 40000000)
		decimals = (decimals * 100) / (fre/10);
	else if(decimals <= 400000000)
		decimals = (decimals * 10) / (fre/100);
	else
		decimals = decimals / (fre/1000);
	interger = interger * 1000 + decimals;
	
	return interger;
}

static int rk616_mipi_dsi_set_screen_info(void) {
	
	g_screen = &ft_screen;
	g_screen->type = SCREEN_MIPI;
	g_screen->face = MIPI_DSI_OUT_FACE;
	g_screen->pixclock = MIPI_DSI_DCLK;
	g_screen->left_margin = MIPI_DSI_H_BP;
	g_screen->right_margin = MIPI_DSI_H_FP;
	g_screen->hsync_len = MIPI_DSI_H_PW;
	g_screen->upper_margin = MIPI_DSI_V_BP;
	g_screen->lower_margin = MIPI_DSI_V_FP;
	g_screen->vsync_len = MIPI_DSI_V_PW;
	g_screen->x_res = MIPI_DSI_H_VD;
	g_screen->y_res = MIPI_DSI_V_VD;
	g_screen->pin_hsync = MIPI_DSI_HSYNC_POL;
	g_screen->pin_vsync = MIPI_DSI_VSYNC_POL;
	g_screen->pin_den = MIPI_DSI_DEN_POL;
	g_screen->pin_dclk = MIPI_DSI_DCLK_POL;
	g_screen->dsi_lane = MIPI_DSI_LANE;
	g_screen->hs_tx_clk = MIPI_DSI_HS_CLK;
	g_screen->init = NULL;
	g_screen->standby = NULL;
	return 0;
}

int rk616_mipi_dsi_ft_init(void) {
	rk616_mipi_dsi_set_screen_info();
	rk_mipi_dsi_init(g_screen, 0);
	return 0;
}
#endif  /* end of CONFIG_MIPI_DSI_FT */



#ifdef CONFIG_MIPI_DSI_LINUX

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk616_mipi_dsi_early_suspend(struct early_suspend *h)
{
	u8 dcs[4] = {0};
	
	if(!g_screen->standby) {
		rk_mipi_dsi_enable_video_mode(0);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_off; 
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_enter_sleep_mode; 
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(1);
	} else {
		g_screen->standby(1);
	}	
		
	rk_mipi_dsi_phy_power_down();
	rk_mipi_dsi_host_power_down();
	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
}

static void rk616_mipi_dsi_late_resume(struct early_suspend *h)
{
	u8 dcs[4] = {0};
	rk_mipi_dsi_phy_power_up();
	rk_mipi_dsi_host_power_up();
	rk_mipi_dsi_phy_init(g_screen, 0);
	rk_mipi_dsi_host_init(g_screen, 0);

	if(!g_screen->standby) {
		rk_mipi_dsi_enable_hs_clk(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_exit_sleep_mode;
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_on;
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(10);
	} else {
		g_screen->standby(0);
	}
	
	dsi_set_bits(0, shutdownz);
	rk_mipi_dsi_enable_video_mode(1);
#ifdef CONFIG_MFD_RK616	
	rk616_display_router_cfg(dsi_rk616, g_rk29fd_screen, 0);
#endif	
	dsi_set_bits(1, shutdownz);
	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
}

#endif  /* end of CONFIG_HAS_EARLYSUSPEND */

static int rk616_mipi_dsi_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr) {
	rk_mipi_dsi_init_lite();
	return 0;
}		

struct notifier_block mipi_dsi_nb= {
	.notifier_call = rk616_mipi_dsi_notifier_event,
};

static int rk616_mipi_dsi_probe(struct platform_device *pdev)
{
	int ret = 0;
	rk_screen *screen; 
	struct mfd_rk616 *rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616)
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}
	else
		dsi_rk616 = rk616;
	
	clk_notifier_register(rk616->mclk, &mipi_dsi_nb);
	
	screen = rk_fb_get_prmry_screen();
	if(!screen) {
		dev_err(&pdev->dev,"the fb prmry screen is null!\n");
		return -ENODEV;
	}
	g_rk29fd_screen = screen;
	
	g_screen = kzalloc(sizeof(struct mipi_dsi_screen), GFP_KERNEL);
	if (g_screen == NULL) {
		ret = -ENOMEM;
		goto do_release_region;
	}
	g_screen->type = screen->type;
	g_screen->face = screen->face;
	g_screen->lcdc_id = screen->lcdc_id;
	g_screen->screen_id = screen->screen_id;
	g_screen->pixclock = screen->pixclock;
	g_screen->left_margin = screen->left_margin;
	g_screen->right_margin = screen->right_margin;
	g_screen->hsync_len = screen->hsync_len;
	g_screen->upper_margin = screen->upper_margin;
	g_screen->lower_margin = screen->lower_margin;
	g_screen->vsync_len = screen->vsync_len;
	g_screen->x_res = screen->x_res;
	g_screen->y_res = screen->y_res;
	g_screen->pin_hsync = screen->pin_hsync;
	g_screen->pin_vsync = screen->pin_vsync;
	g_screen->pin_den = screen->pin_den;
	g_screen->pin_dclk = screen->pin_dclk;
	g_screen->dsi_lane = screen->dsi_lane;
	g_screen->dsi_video_mode = screen->dsi_video_mode;
	g_screen->hs_tx_clk = screen->hs_tx_clk;
	g_screen->init = screen->init;
	g_screen->standby = screen->standby;	
	
	ret = rk_mipi_dsi_probe(g_screen, 0);
	if(ret) {
		dev_info(&pdev->dev,"rk mipi_dsi probe fail!\n");
		dev_info(&pdev->dev,"%s\n", RK_MIPI_DSI_VERSION_AND_TIME);
		goto do_release_region;
	}	
#ifdef CONFIG_HAS_EARLYSUSPEND
	gDsi.early_suspend.suspend = rk616_mipi_dsi_early_suspend;
	gDsi.early_suspend.resume = rk616_mipi_dsi_late_resume;
	gDsi.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&gDsi.early_suspend);
#endif
	
	dev_info(&pdev->dev,"rk mipi_dsi probe success!\n");
	dev_info(&pdev->dev,"%s\n", RK_MIPI_DSI_VERSION_AND_TIME);
	return 0;
do_release_region:	
	kfree(g_screen);
	return ret;
	
}

static int rk616_mipi_dsi_remove(struct platform_device *pdev)
{
	clk_notifier_unregister(dsi_rk616->mclk, &mipi_dsi_nb);
	return 0;
}

static void rk616_mipi_dsi_shutdown(struct platform_device *pdev)
{
	u8 dcs[4] = {0};
	
	if(!g_screen->standby) {
		rk_mipi_dsi_enable_video_mode(0);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_off; 
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_enter_sleep_mode; 
		rk_mipi_dsi_send_dcs_packet(dcs, 2);
		msleep(1);
	} else {
		g_screen->standby(1);
	}
		
	rk_mipi_dsi_phy_power_down();
	rk_mipi_dsi_host_power_down();
	
	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
	return;
}

static struct platform_driver rk616_mipi_dsi_driver = {
	.driver		= {
		.name	= "rk616-mipi",
		.owner	= THIS_MODULE,
	},
	.probe		= rk616_mipi_dsi_probe,
	.remove		= rk616_mipi_dsi_remove,
	.shutdown	= rk616_mipi_dsi_shutdown,
};

static int __init rk616_mipi_dsi_init(void)
{
	return platform_driver_register(&rk616_mipi_dsi_driver);
}
fs_initcall(rk616_mipi_dsi_init);

static void __exit rk616_mipi_dsi_exit(void)
{
	platform_driver_unregister(&rk616_mipi_dsi_driver);
}
module_exit(rk616_mipi_dsi_exit);
#endif  /* end of CONFIG_MIPI_DSI_LINUX */
