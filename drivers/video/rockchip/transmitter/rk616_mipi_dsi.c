/*
drivers/video/display/transmitter/rk616_mipi_dsi.c
debug sys/kernel/debug/rk616/mipi
*/


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

#include "mipi_dsi.h"
#include "rk616_mipi_dsi.h"

#if 0
#define	MIPI_DBG(x...)	printk(KERN_INFO x)
#else
#define	MIPI_DBG(x...)
#endif


static struct dsi gDsi;
static struct mfd_rk616 *dsi_rk616;
static struct mipi_dsi_ops rk_mipi_dsi_ops;
static struct rk29fb_screen *g_screen = NULL;
static unsigned char dcs_exit_sleep_mode[] = {0x11};
static unsigned char dcs_set_diaplay_on[] = {0x29};
static unsigned char dcs_enter_sleep_mode[] = {0x10};
static unsigned char dcs_set_diaplay_off[] = {0x28};
static unsigned char dcs_test[] = {0x3e, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
static int rk_mipi_dsi_send_dcs_packet(unsigned char regs[], u32 n);

static int dsi_read_reg(u16 reg, u32 *pval)
{
	return dsi_rk616->read_dev(dsi_rk616, reg, pval);
}


static int dsi_write_reg(u16 reg, u32 *pval)
{
	return dsi_rk616->write_dev(dsi_rk616, reg, pval);
}

static int dsi_get_bits(u32 reg)
{
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

static int dsi_set_bits(u32 data, u32 reg)
{
	u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	bits = (1 << bits) - 1;
	
	dsi_read_reg(reg_addr, &val);      //CAN optimise  speed and time cost   warnning
	val &= ~(bits << offset);
	val |= (data & bits) << offset;
	dsi_write_reg(reg_addr, &val);
	
	//if(dsi_get_bits(reg) != data)
	//	printk("write :%08x error\n", reg);
	return 0;
}

static int rk_mipi_dsi_phy_preset_gotp(void *array, int n) {

	//u32 val = 0;
	//struct rk29fb_screen *screen = array;

	return 0;
}

static int rk_mipi_dsi_phy_set_gotp(u32 offset, int n) {
	
	u32 val = 0, temp = 0, Tlpx = 0;
	u32 ddr_clk = gDsi.phy.ddr_clk;
	u32 Tddr_clk = gDsi.phy.Tddr_clk;
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
	dsi_write_reg(reg_ths_settle + offset, &val);
		
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
		
	MIPI_DBG("reg_hs_ths_prepare: %d, %d\n", val, val*Tddr_clk/1000);				
	dsi_write_reg(reg_hs_ths_prepare + offset, &val);
	
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
	
	MIPI_DBG("reg_hs_the_zero: %d, %d\n", val, (val + 5)*Ttxbyte_clk/1000);					
	dsi_write_reg(reg_hs_the_zero + offset, &val);
	
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
		val = 0x27;		
	dsi_write_reg(reg_hs_ths_trail + offset, &val);
	
	val = 120000 / Ttxbyte_clk + 1;
	MIPI_DBG("reg_hs_ths_exit: %d, %d\n", val, val*Ttxbyte_clk/1000);
	dsi_write_reg(reg_hs_ths_exit + offset, &val);	
	
	if(offset == DPHY_CLOCK_OFFSET) {
		val = (80000 + 52*gDsi.phy.UI) / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_post: %d, %d\n", val, val*Ttxbyte_clk/1000);
		dsi_write_reg(reg_hs_tclk_post + offset, &val);	
		val = 10*gDsi.phy.UI / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_pre: %d, %d\n", val, val*Ttxbyte_clk/1000);
		dsi_write_reg(reg_hs_tclk_pre + offset, &val);	
	}

	val = 1010000000 / Tsys_clk + 1;
	MIPI_DBG("reg_hs_twakup: %d, %d\n", val, val*Tsys_clk/1000);
	if(val > 0x3ff) {
		val = 0x2ff;
		MIPI_DBG("val is too large, 0x3ff is the largest\n");	
	}	
	temp = (val >> 8) & 0x03;
	val &= 0xff;
	dsi_write_reg(reg_hs_twakup_h + offset, &temp);		
	dsi_write_reg(reg_hs_twakup_l + offset, &val);	
	
	if(Ttxclkesc > 50000) {
		val = 2*Ttxclkesc;
		MIPI_DBG("Ttxclkesc:%d\n", Ttxclkesc);
	}
	val = val / Ttxbyte_clk;
	Tlpx = val*Ttxbyte_clk;
	MIPI_DBG("reg_hs_tlpx: %d, %d\n", val, Tlpx);
	val -= 2;
	dsi_write_reg(reg_hs_tlpx + offset, &val);
	
	Tlpx = 2*Ttxclkesc;
	val = 4*Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_go: %d, %d\n", val, val*Ttxclkesc);
	dsi_write_reg(reg_hs_tta_go + offset, &val);	

	val = 3 * Tlpx / 2 / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_sure: %d, %d\n", val, val*Ttxclkesc);
	dsi_write_reg(reg_hs_tta_sure + offset, &val);	

	val = 5 * Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_wait: %d, %d\n", val, val*Ttxclkesc);
	dsi_write_reg(reg_hs_tta_wait + offset, &val);	
#if 1
	val = 0x5b;
	dsi_write_reg(offset + 0x18, &val);
	val = 0x38;
	dsi_write_reg(offset + 0x20, &val);	
#endif
	return 0;
}


static int rk_mipi_dsi_phy_init(void *array, int n) {

	u32 val = 0;
	//DPHY init
	dsi_set_bits((gDsi.phy.fbdiv >> 8) & 0x01, reg_fbdiv_8);
	dsi_set_bits(gDsi.phy.prediv, reg_prediv);
	dsi_set_bits(gDsi.phy.fbdiv & 0xff, reg_fbdiv);
	
	val = 0xe4;       
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
#if 0	
	//new temp
	val = 0xff;
	dsi_write_reg(0x0c24, &val);
	
	val = 0x77;
	dsi_write_reg(0x0c18, &val);
	val = 0x77;
	dsi_write_reg(0x0c1c, &val);
	
	val = 0x4f;
	dsi_write_reg(0x0c20, &val);
	
	val = 0xc0;
	dsi_write_reg(0x0c28, &val);
#endif	

#if 0
	val = 0xff;
	dsi_write_reg(RK_ADDR(0x09), &val);
	val = 0x4e;
	dsi_write_reg(RK_ADDR(0x08), &val);
	val = 0x84;
	dsi_write_reg(RK_ADDR(0x0a), &val);
#endif
	
	val = 0x30;
	dsi_write_reg(RK_ADDR(0x05), &val);
			
	//if(800 <= gDsi.phy.ddr_clk && gDsi.phy.ddr_clk <= 1000)
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


static int rk_mipi_dsi_host_init(void *array, int n) {

	u32 val = 0, bytes_px = 0;
	struct rk29fb_screen *screen = array;
	u32 decimals = gDsi.phy.Ttxbyte_clk, temp = 0, i = 0;
	u32 m = 1, lane = gDsi.host.lane, Tpclk = gDsi.phy.Tpclk, Ttxbyte_clk = gDsi.phy.Ttxbyte_clk;
	
	val = 0x04000000;
	dsi_write_reg(CRU_CRU_CLKSEL1_CON, &val);
	
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
	
	gDsi.phy.txclkesc = 10 * MHz;
	val = gDsi.phy.txbyte_clk / gDsi.phy.txclkesc + 1;
	gDsi.phy.txclkesc = gDsi.phy.txbyte_clk / val;
	dsi_set_bits(val, TX_ESC_CLK_DIVISION);
	
	
	dsi_set_bits(10, TO_CLK_DIVISION);
	dsi_set_bits(1000, lprx_to_cnt);
	dsi_set_bits(1000, hstx_to_cnt);	
	dsi_set_bits(100, phy_stop_wait_time);

	dsi_set_bits(4, outvact_lpcmd_time);   //byte
	dsi_set_bits(4, invact_lpcmd_time);
		
	dsi_set_bits(20, phy_hs2lp_time);
	dsi_set_bits(16, phy_lp2hs_time);	
	
	dsi_set_bits(10, max_rd_time);

	dsi_set_bits(1, dpicolom);
	dsi_set_bits(1, dpishutdn);

	//interrupt            //need 
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
	dsi_set_bits(1, shutdownz);
	//dsi_set_bits(1, phy_enableclk);

	val = 10;
	while(!dsi_get_bits(phylock) && val--) {
		udelay(10);
	};
	if(val == 0)
		printk("%s:phylock fail\n", __func__);		
	val = 10;
	while(!dsi_get_bits(phystopstateclklane) && val--) {
		udelay(10);
	};
	
	dsi_set_bits(4, phy_tx_triggers);
	//dsi_set_bits(1, phy_txexitulpslan);
	//dsi_set_bits(1, phy_txexitulpsclk);
	dsi_set_bits(1, phy_txrequestclkhs);
	dsi_set_bits(0, en_video_mode);

	return 0;
}



/*
	mipi protocol layer definition
*/
static int rk_mipi_dsi_init(void *array, u32 n) {
	u32 decimals = 1000, i = 0, pre = 0;
	struct rk29fb_screen *screen = array;
	
	if(!g_screen && screen)
		g_screen = screen;
	
	if(g_screen->type != SCREEN_MIPI) {
		printk("only mipi dsi lcd is supported\n");
		return -1;
	}
	
	gDsi.phy.Tpclk = rk_fb_get_prmry_screen_pixclock();

	if(dsi_rk616->mclk)
		gDsi.phy.ref_clk = clk_get_rate(dsi_rk616->mclk);
	else
		gDsi.phy.ref_clk = 24 * MHz;
		
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
	
	gDsi.phy.txclkesc = 10 * MHz;        // < 10MHz
	gDsi.phy.txclkesc = gDsi.phy.txbyte_clk / (gDsi.phy.txbyte_clk / gDsi.phy.txclkesc + 1);
	
	gDsi.phy.pclk = div_u64(1000000000000llu, gDsi.phy.Tpclk);
	gDsi.phy.Ttxclkesc = div_u64(1000000000000llu, gDsi.phy.txclkesc);
	gDsi.phy.Tsys_clk = div_u64(1000000000000llu, gDsi.phy.sys_clk);
	gDsi.phy.Tddr_clk = div_u64(1000000000000llu, gDsi.phy.ddr_clk);
	gDsi.phy.Ttxbyte_clk = div_u64(1000000000000llu, gDsi.phy.txbyte_clk);
	
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
	
	dsi_set_bits(0, en_video_mode);
	dsi_set_bits(0, shutdownz);
	
	rk_mipi_dsi_phy_init(screen, n);
	rk_mipi_dsi_host_init(screen, n);
	
	if(!screen->init) { 
		rk_mipi_dsi_send_dcs_packet(dcs_exit_sleep_mode, sizeof(dcs_exit_sleep_mode));
		msleep(1);
		rk_mipi_dsi_send_dcs_packet(dcs_set_diaplay_on, sizeof(dcs_set_diaplay_on));
		msleep(10);
	}
	
	dsi_set_bits(1, en_video_mode);
	rk616_display_router_cfg(dsi_rk616, screen, 0);
	
	return 0;
}


static int rk_mipi_dsi_send_dcs_packet(unsigned char regs[], u32 n) {

	u32 data = 0, i = 0, j = 0;
	if(n <= 0)
		return -1;
		
	if(dsi_get_bits(gen_cmd_full) == 1) {
		printk("gen_cmd_full\n");
		return -1;
	}
	dsi_set_bits(0, lpcmden);        //send in high speed mode
	
	if(n <= 2) {
		data = (gDsi.vid << 6) | ((n-1) << 4) | 0x05;
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
					printk("gen_pld_w_full :%d\n", i);
					break;
				}
				dsi_write_reg(GEN_PLD_DATA, &data);
				MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
				data = 0;
			}
		}
		data = (gDsi.vid << 6) | 0x39;		
		data |= n << 8;
	}
	//MIPI_DBG("write GEN_HDR:%08x\n", data);
	dsi_write_reg(GEN_HDR, &data);
	i = 10;
	
	while(!dsi_get_bits(gen_cmd_empty) && i--) {
		MIPI_DBG(".");
		udelay(10);
	}
	//MIPI_DBG("send command");
	//MIPI_DBG("\n");
	return 0;
}


static int rk_mipi_dsi_send_packet(unsigned char type, unsigned char regs[], u32 n) {

	return 0;
}

static int rk_mipi_dsi_read_dcs_packet(unsigned char *data, u32 n) {
	//DCS READ 
	
	return 0;
}

static int rk_mipi_dsi_power_up(void) {

	return 0;
}

static int rk_mipi_dsi_power_down(void) {
	
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
	.dsi_send_dcs_packet = rk_mipi_dsi_send_dcs_packet,
	.dsi_read_dcs_packet = rk_mipi_dsi_read_dcs_packet,
	.power_up = rk_mipi_dsi_power_up,
	.power_down = rk_mipi_dsi_power_down,
	.dsi_init = rk_mipi_dsi_init,
};


#if MIPI_DSI_REGISTER_IO
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>

static struct proc_dir_entry *reg_proc_entry;

int reg_proc_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)
{
	int ret = -1, i = 0;
	u32 read_val = 0;
	char *buf = kmalloc(count, GFP_KERNEL);
	char *data = buf;
	char str[32];
	char command = 0;
	u64 regs_val = 0;
	struct regulator *ldo;
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
				if((regs_val & 0xffff00000000) == 0)
					goto reg_proc_write_exit;
				read_val = regs_val & 0xffffffff;
				dsi_write_reg(regs_val >> 32, &read_val);
				dsi_read_reg(regs_val >> 32, &read_val);
				regs_val &= 0xffffffff;
				if(read_val != regs_val)
					printk("%s fail:0x%08x\n", __func__, read_val);	
				
				data += 3;
				msleep(1);	
			}
		
			break;
		case 'r':
				data = strstr(data, "0x");
				if(data == NULL)
					goto reg_proc_write_exit;
				sscanf(data, "0x%x", &regs_val);
				dsi_read_reg((u16)regs_val, &read_val);
				printk("*%04x : %08x\n", (u16)regs_val, read_val);
				msleep(1);	
			break;	
	
		case 's':
				while(*(++data) == ' ');
				sscanf(data, "%d", &read_val);
				rk_mipi_dsi_init(g_screen, read_val * MHz);
			break;
		case 'p':
				while(*(++data) == ' ');
				sscanf(data, "%d", &read_val);
				while(*(++data) == ' ');
				sscanf(data, "%s", str);
				printk(" get %s\n", str);
				ldo = regulator_get(NULL, str);
				if(!ldo)
					break;
				if(read_val == 0) {
					while(regulator_is_enabled(ldo)>0)
						regulator_disable(ldo);	
				} else {
					regulator_enable(ldo);
				}	
				regulator_put(ldo);
				
			break;
	
		default:
			break;
	}

reg_proc_write_exit:
	kfree(buf);
	msleep(10);	
 	return count;
}



int reg_proc_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	int i = 0;
	u32 val = 0;
	u8 buf[4] = "hhb";
	copy_to_user(buff, buf, 4);
	count = 4;
	for(i = VERSION; i <= LP_CMD_TIM; i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}
	
	printk("\n");
	for(i = DPHY_REGISTER0; i <= DPHY_REGISTER4; i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}
	printk("\n");
	i = DPHY_REGISTER20;
	dsi_read_reg(i, &val);
	printk("%04x: %08x\n", i, val);
	msleep(1);
#if 1
	printk("\n");
	for(i = DPHY_CLOCK_OFFSET; i <= (DPHY_CLOCK_OFFSET + reg_hs_tta_wait); i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}

	
	printk("\n");
	for(i = DPHY_LANE0_OFFSET; i <= (DPHY_LANE0_OFFSET + reg_hs_tta_wait); i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}

		printk("\n");
	for(i = DPHY_LANE1_OFFSET; i <= (DPHY_LANE1_OFFSET + reg_hs_tta_wait); i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}

	printk("\n");
	for(i = DPHY_LANE2_OFFSET; i <= (DPHY_LANE2_OFFSET + reg_hs_tta_wait); i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}
	
		printk("\n");
	for(i = DPHY_LANE3_OFFSET; i <= (DPHY_LANE3_OFFSET + reg_hs_tta_wait); i += 4) {
		dsi_read_reg(i, &val);
		printk("%04x: %08x\n", i, val);
		msleep(1);
	}
#endif
	return -1;
}

int reg_proc_open(struct inode *inode, struct file *file)
{
	//printk("%s\n", __func__);
	//msleep(10);
	return 0;
}

int reg_proc_close(struct inode *inode, struct file *file)
{
	//printk("%s\n", __func__);
	//msleep(10);
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
	debugfs_create_file("mipi", S_IRUSR, dsi_rk616->debugfs_dir, dsi_rk616, &reg_proc_fops);
#else
  	reg_proc_entry = create_proc_entry(name, 0666, NULL);
	if(reg_proc_entry == NULL) {
		printk("Couldn't create proc entry : %s!\n", name);
		ret = -ENOMEM;
		return ret ;
	}
	else {
		printk("Create proc entry:%s success!\n", name);
		reg_proc_entry->proc_fops = &reg_proc_fops;
	}
#endif	
	return 0;
}

static int __init rk_mipi_dsi_reg(void)
{
	return reg_proc_init("mipi_dsi");
}
module_init(rk_mipi_dsi_reg);

#endif



#if	defined(CONFIG_HAS_EARLYSUSPEND)
static void rk616_mipi_dsi_early_suspend(struct early_suspend *h)
{
	u32 val = 0X01;   
	dsi_set_bits(0, en_video_mode);
	dsi_write_reg(DPHY_REGISTER0, &val);
	
	val = 0xe3;    
	dsi_write_reg(DPHY_REGISTER1, &val);
	dsi_set_bits(0, shutdownz);
}

static void rk616_mipi_dsi_late_resume(struct early_suspend *h)
{
	rk_mipi_dsi_phy_init(g_screen, 0);
	//dsi_set_bits(1, shutdownz);
	rk_mipi_dsi_host_init(g_screen, 0);
	
	if(!g_screen->init) {
		rk_mipi_dsi_send_dcs_packet(dcs_exit_sleep_mode, sizeof(dcs_exit_sleep_mode));
		msleep(1);
		rk_mipi_dsi_send_dcs_packet(dcs_set_diaplay_on, sizeof(dcs_set_diaplay_on));
		msleep(10);
	}
	dsi_set_bits(1, en_video_mode);
	rk616_display_router_cfg(dsi_rk616, g_screen, 0);
}

#endif



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
	
	register_dsi_ops(&rk_mipi_dsi_ops);
	
	ret = dsi_probe_current_chip();
	if(ret) {
		dev_err(&pdev->dev,"mipi dsi probe fail\n");
		return -ENODEV;
	}
	
	screen = rk_fb_get_prmry_screen();
	if(!screen)
	{
		dev_err(&pdev->dev,"the fb prmry screen is null!\n");
		return -ENODEV;
	}
	
	rk_mipi_dsi_init(screen, 0);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	gDsi.early_suspend.suspend = rk616_mipi_dsi_early_suspend;
	gDsi.early_suspend.resume = rk616_mipi_dsi_late_resume;
	gDsi.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&gDsi.early_suspend);
#endif
	
	dev_info(&pdev->dev,"rk616 mipi_dsi probe success!\n");
	
	return 0;
	
}

static int rk616_mipi_dsi_remove(struct platform_device *pdev)
{
	
	return 0;
}

static void rk616_mipi_dsi_shutdown(struct platform_device *pdev)
{
	
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
