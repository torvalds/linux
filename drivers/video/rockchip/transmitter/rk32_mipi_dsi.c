/*
 * Copyright (C) 2014 ROCKCHIP, Inc.
 * drivers/video/display/transmitter/rk32_mipi_dsi.c
 * author: libing@rock-chips.com
 * create date: 2014-04-10
 * debug /sys/kernel/debug/mipidsi* 
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
#ifndef CONFIG_RK32_MIPI_DSI
#include <common.h>
#endif

#ifdef CONFIG_RK32_MIPI_DSI
#define MIPI_DSI_REGISTER_IO	0
#define CONFIG_MIPI_DSI_LINUX	0
#endif
#define DWC_DSI_VERSION			0x3133302A
#define DWC_DSI_VERSION_RK312x		0x3132312A
#define DWC_DSI_VERSION_ERR		-1

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/rk_fb.h>
#include <linux/rk_screen.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <asm/div64.h>

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/regulator/machine.h>

#include <linux/dma-mapping.h>
#include "mipi_dsi.h"
#include "rk32_mipi_dsi.h"
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/cpu.h>

#define	MIPI_DBG(x...)	//printk(KERN_INFO x)

#ifdef CONFIG_MIPI_DSI_LINUX
#define	MIPI_TRACE(x...)	//printk(KERN_INFO x)
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
*v1.0 : this driver is rk32 mipi dsi driver of rockchip;
*v1.1 : add test eye pattern;
*
*/

#define RK_MIPI_DSI_VERSION_AND_TIME  "rockchip mipi_dsi v1.1 2014-06-17"

static struct dsi *dsi0;
static struct dsi *dsi1;

static int rk32_mipi_dsi_is_active(void *arg);
static int rk32_mipi_dsi_enable_hs_clk(void *arg, u32 enable);
static int rk32_mipi_dsi_enable_video_mode(void *arg, u32 enable);
static int rk32_mipi_dsi_enable_command_mode(void *arg, u32 enable);
static int rk32_mipi_dsi_is_enable(void *arg, u32 enable);
static int rk32_mipi_power_down_DDR(void);
static int rk32_mipi_power_up_DDR(void);
int rk_mipi_screen_standby(u8 enable);

static int rk32_dsi_read_reg(struct dsi *dsi, u16 reg, u32 *pval)
{
	if (dsi->ops.id == DWC_DSI_VERSION)
		*pval = __raw_readl(dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
	else if (dsi->ops.id == DWC_DSI_VERSION_RK312x) {
		if(reg >= MIPI_DSI_HOST_OFFSET)
			*pval = __raw_readl(dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
		else if(reg >= MIPI_DSI_PHY_OFFSET)
			*pval = __raw_readl(dsi->phy.membase + (reg - MIPI_DSI_PHY_OFFSET));
	}
	return 0;
}

static int rk32_dsi_write_reg(struct dsi *dsi, u16 reg, u32 *pval)
{
	if (dsi->ops.id == DWC_DSI_VERSION)
		__raw_writel(*pval, dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
	else if (dsi->ops.id == DWC_DSI_VERSION_RK312x) {
		if(reg >= MIPI_DSI_HOST_OFFSET)
			__raw_writel(*pval, dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
		else if(reg >= MIPI_DSI_PHY_OFFSET)
			__raw_writel(*pval, dsi->phy.membase + (reg - MIPI_DSI_PHY_OFFSET));	
	}
	return 0;
}

static int rk32_dsi_get_bits(struct dsi *dsi, u32 reg)
{
	u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	
	if(bits < 32)
		bits = (1 << bits) - 1;
	else
		bits = 0xffffffff;
	
	rk32_dsi_read_reg(dsi, reg_addr, &val);
	val >>= offset;
	val &= bits;
	
	return val;
}

static int rk32_dsi_set_bits(struct dsi *dsi, u32 data, u32 reg) 
{
	static u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	
	if(bits < 32)
		bits = (1 << bits) - 1;
	else
		bits = 0xffffffff;

	if(bits != 0xffffffff)
		rk32_dsi_read_reg(dsi, reg_addr, &val);

	val &= ~(bits << offset);
	val |= (data & bits) << offset;
	rk32_dsi_write_reg(dsi, reg_addr, &val);

	if(data > bits) {
		MIPI_TRACE("%s error reg_addr:0x%04x, offset:%d, bits:0x%04x, value:0x%04x\n", 
				__func__, reg_addr, offset, bits, data);
	}
	
	return 0;
}
#if 0
static int rk32_dwc_phy_test_rd(struct dsi *dsi, unsigned char test_code)
{
	int val = 0;
	rk32_dsi_set_bits(dsi, 1, phy_testclk);
	rk32_dsi_set_bits(dsi, test_code, phy_testdin);
	rk32_dsi_set_bits(dsi, 1, phy_testen);
	rk32_dsi_set_bits(dsi, 0, phy_testclk);
	rk32_dsi_set_bits(dsi, 0, phy_testen);;

	rk32_dsi_set_bits(dsi, 0, phy_testen);
	val = rk32_dsi_get_bits(dsi,phy_testdout);
	rk32_dsi_set_bits(dsi, 1, phy_testclk);
	rk32_dsi_set_bits(dsi, 0, phy_testclk);

	return val;
}
#endif
static int rk32_dwc_phy_test_wr(struct dsi *dsi, unsigned char test_code, unsigned char *test_data, unsigned char size)
{
	int i = 0;
	
	MIPI_DBG("test_code=0x%x,test_data=0x%x\n", test_code,test_data[0]);
	rk32_dsi_set_bits(dsi, 0x10000 | test_code, PHY_TEST_CTRL1);
	rk32_dsi_set_bits(dsi, 0x2, PHY_TEST_CTRL0);
	rk32_dsi_set_bits(dsi, 0x0, PHY_TEST_CTRL0);

	for(i = 0; i < size; i++) {
		rk32_dsi_set_bits(dsi, test_data[i], PHY_TEST_CTRL1);
		rk32_dsi_set_bits(dsi, 0x2, PHY_TEST_CTRL0);
		rk32_dsi_set_bits(dsi, 0x0, PHY_TEST_CTRL0);
		MIPI_DBG("rk32_dwc_phy_test_wr:%08x\n", rk32_dsi_get_bits(dsi, PHY_TEST_CTRL1));
	}
	return 0;
}

static int rk32_phy_power_up(struct dsi *dsi)
{
    //enable ref clock
    clk_prepare_enable(dsi->phy.refclk); 
    clk_prepare_enable(dsi->dsi_pclk);
    clk_prepare_enable(dsi->dsi_pd);
    udelay(10);

	switch(dsi->host.lane) {
		case 4:
			rk32_dsi_set_bits(dsi, 3, n_lanes);
			break;
		case 3:
			rk32_dsi_set_bits(dsi, 2, n_lanes);
			break;
		case 2:
			rk32_dsi_set_bits(dsi, 1, n_lanes);
			break;
		case 1:
			rk32_dsi_set_bits(dsi, 0, n_lanes);
			break;
		default:
			break;	
	}
	rk32_dsi_set_bits(dsi, 1, phy_shutdownz);
	rk32_dsi_set_bits(dsi, 1, phy_rstz);  
	rk32_dsi_set_bits(dsi, 1, phy_enableclk);
	rk32_dsi_set_bits(dsi, 1, phy_forcepll);
	
	return 0;
}

static int rk312x_mipi_dsi_phy_set_gotp(struct dsi *dsi, u32 offset, int n) 
{
	u32 val = 0, temp = 0, Tlpx = 0;
	u32 ddr_clk = dsi->phy.ddr_clk;
	u32 Ttxbyte_clk = dsi->phy.Ttxbyte_clk;
	u32 Tsys_clk = dsi->phy.Tsys_clk;
	u32 Ttxclkesc = dsi->phy.Ttxclkesc;
	printk("%s : ddr_clk %d\n",__func__, ddr_clk);
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
	printk("%s reg_ths_settle = 0x%x\n",__func__, val);
	rk32_dsi_set_bits(dsi, val, reg_ths_settle + offset);
	
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
	printk("%s reg_hs_ths_prepare = 0x%x\n",__func__, val);
	rk32_dsi_set_bits(dsi, val, reg_hs_ths_prepare + offset);

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
	printk("%s reg_hs_the_zero = 0x%x\n",__func__, val);
	rk32_dsi_set_bits(dsi, val, reg_hs_the_zero + offset);
	
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
		val = 0x21;   //0x27
		
	printk("%s reg_hs_ths_trail = 0x%x\n",__func__, val);

	rk32_dsi_set_bits(dsi, val, reg_hs_ths_trail + offset);
	val = 120000 / Ttxbyte_clk + 1;
	MIPI_DBG("reg_hs_ths_exit: %d, %d\n", val, val*Ttxbyte_clk/1000);
	rk32_dsi_set_bits(dsi, val, reg_hs_ths_exit + offset);
	
	if(offset == DPHY_CLOCK_OFFSET) {
		val = (60000 + 52*dsi->phy.UI) / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_post: %d, %d\n", val, val*Ttxbyte_clk/1000);
		rk32_dsi_set_bits(dsi, val, reg_hs_tclk_post + offset);
		val = 10*dsi->phy.UI / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_pre: %d, %d\n", val, val*Ttxbyte_clk/1000);	
		rk32_dsi_set_bits(dsi, val, reg_hs_tclk_pre + offset);
	}

	val = 1010000000 / Tsys_clk + 1;
	MIPI_DBG("reg_hs_twakup: %d, %d\n", val, val*Tsys_clk/1000);
	if(val > 0x3ff) {
		val = 0x2ff;
		MIPI_DBG("val is too large, 0x3ff is the largest\n");	
	}
	temp = (val >> 8) & 0x03;
	val &= 0xff;	
	rk32_dsi_set_bits(dsi, temp, reg_hs_twakup_h + offset);	
	rk32_dsi_set_bits(dsi, val, reg_hs_twakup_l + offset);
	
	if(Ttxclkesc > 50000) {
		val = 2*Ttxclkesc;
		MIPI_DBG("Ttxclkesc:%d\n", Ttxclkesc);
	}
	val = val / Ttxbyte_clk;
	Tlpx = val*Ttxbyte_clk;
	MIPI_DBG("reg_hs_tlpx: %d, %d\n", val, Tlpx);
	val -= 2;
	rk32_dsi_set_bits(dsi, val, reg_hs_tlpx + offset);
	
	Tlpx = 2*Ttxclkesc;
	val = 4*Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_go: %d, %d\n", val, val*Ttxclkesc);
	rk32_dsi_set_bits(dsi, val, reg_hs_tta_go + offset);
	val = 3 * Tlpx / 2 / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_sure: %d, %d\n", val, val*Ttxclkesc);	
	rk32_dsi_set_bits(dsi, val, reg_hs_tta_sure + offset);
	val = 5 * Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_wait: %d, %d\n", val, val*Ttxclkesc);
	rk32_dsi_set_bits(dsi, val, reg_hs_tta_wait + offset);
	return 0;
}

static void rk312x_mipi_dsi_set_hs_clk(struct dsi *dsi) 
{
	rk32_dsi_set_bits(dsi, dsi->phy.prediv, reg_prediv);
	rk32_dsi_set_bits(dsi, dsi->phy.fbdiv & 0xff, reg_fbdiv);
	rk32_dsi_set_bits(dsi, (dsi->phy.fbdiv >> 8) & 0x01, reg_fbdiv_8);
}

static int rk312x_phy_power_up(struct dsi *dsi)
{
	//enable ref clock
	rk312x_mipi_dsi_set_hs_clk(dsi);
	clk_prepare_enable(dsi->phy.refclk); 
	clk_prepare_enable(dsi->dsi_pclk);
	clk_prepare_enable(dsi->dsi_host_pclk);
	clk_prepare_enable(dsi->h2p_hclk);
	clk_prepare_enable(dsi->dsi_pd);
	udelay(10);
    
	rk32_dsi_set_bits(dsi, 0xe4, DPHY_REGISTER1);
	switch(dsi->host.lane) {
		case 4:
			rk32_dsi_set_bits(dsi, 1, lane_en_3);
		case 3:
			rk32_dsi_set_bits(dsi, 1, lane_en_2);
		case 2:
			rk32_dsi_set_bits(dsi, 1, lane_en_1);
		case 1:
			rk32_dsi_set_bits(dsi, 1, lane_en_0);
			rk32_dsi_set_bits(dsi, 1, lane_en_ck);
			break;
		default:
			break;  
	}
    
	rk32_dsi_set_bits(dsi, 0xe0, DPHY_REGISTER1);
	udelay(10);
    
	rk32_dsi_set_bits(dsi, 0x1e, DPHY_REGISTER20);
	rk32_dsi_set_bits(dsi, 0x1f, DPHY_REGISTER20);

	rk32_dsi_set_bits(dsi, 1, phy_enableclk);
	
	return 0;
}

static int rk_phy_power_up(struct dsi *dsi) 
{
	if (dsi->ops.id == DWC_DSI_VERSION)
		rk32_phy_power_up(dsi);
	else if (dsi->ops.id == DWC_DSI_VERSION_RK312x)
		rk312x_phy_power_up(dsi);
	return 0;
}

static int rk32_phy_power_down(struct dsi *dsi)
{
    rk32_dsi_set_bits(dsi, 0, phy_shutdownz);
    clk_disable_unprepare(dsi->phy.refclk); 
    clk_disable_unprepare(dsi->dsi_pclk);
    clk_disable_unprepare(dsi->dsi_pd);
    return 0;
}

static int rk312x_phy_power_down(struct dsi *dsi)
{
    rk32_dsi_set_bits(dsi, 0x01, DPHY_REGISTER0);
    rk32_dsi_set_bits(dsi, 0xe3, DPHY_REGISTER1);
    
    clk_disable_unprepare(dsi->phy.refclk); 
    clk_disable_unprepare(dsi->dsi_pclk);
	clk_disable_unprepare(dsi->dsi_host_pclk);
	clk_disable_unprepare(dsi->h2p_hclk);
    clk_disable_unprepare(dsi->dsi_pd);
    return 0;
}

static int rk_phy_power_down(struct dsi *dsi) 
{
	if (dsi->ops.id == DWC_DSI_VERSION)
		rk32_phy_power_down(dsi);
	else if (dsi->ops.id == DWC_DSI_VERSION_RK312x)
		rk312x_phy_power_down(dsi);
	return 0;
}

static int rk32_phy_init(struct dsi *dsi)
{
	u32 val = 0 , ddr_clk = 0, fbdiv = 0, prediv = 0;
	unsigned char test_data[2] = {0};
	
	ddr_clk	= dsi->phy.ddr_clk;
	prediv	= dsi->phy.prediv;
	fbdiv	= dsi->phy.fbdiv;

	if(ddr_clk < 200 * MHz)
		val = 0x0;
	else if(ddr_clk < 300 * MHz)
		val = 0x1;
	else if(ddr_clk < 500 * MHz)
		val = 0x2;
	else if(ddr_clk < 700 * MHz)
		val = 0x3;
	else if(ddr_clk < 900 * MHz)
		val = 0x4;
	else if(ddr_clk < 1100 * MHz)
		val = 0x5; 
	else if(ddr_clk < 1300 * MHz)
		val = 0x6;
	else if(ddr_clk <= 1500 * MHz)
		val = 0x7;
	
	test_data[0] = 0x80 | val << 3 | 0x3;
	rk32_dwc_phy_test_wr(dsi, 0x10, test_data, 1);
	
	test_data[0] = 0x8;
	rk32_dwc_phy_test_wr(dsi, 0x11, test_data, 1); 
	
	test_data[0] = 0x80 | 0x40;
	rk32_dwc_phy_test_wr(dsi, 0x12, test_data, 1); 
	
	if(ddr_clk < 90 * MHz)
		val = 0x01;
	else if(ddr_clk < 100 * MHz)
		val = 0x10;
	else if(ddr_clk < 110 * MHz)
		val = 0x20;
	else if(ddr_clk < 130 * MHz)
		val = 0x01;
	else if(ddr_clk < 140 * MHz)
		val = 0x11;
	else if(ddr_clk < 150 * MHz)
		val = 0x21; 
	else if(ddr_clk < 170 * MHz)
		val = 0x02;
	else if(ddr_clk < 180 * MHz)
		val = 0x12;
	else if(ddr_clk < 200 * MHz)
		val = 0x22;
	else if(ddr_clk < 220 * MHz)
		val = 0x03;
	else if(ddr_clk < 240 * MHz)
		val = 0x13;
	else if(ddr_clk < 250 * MHz)
		val = 0x23;
	else if(ddr_clk < 270 * MHz)
		val = 0x04; 
	else if(ddr_clk < 300 * MHz)
		val = 0x14;
	else if(ddr_clk < 330 * MHz)
		val = 0x05;
	else if(ddr_clk < 360 * MHz)
		val = 0x15; 
	else if(ddr_clk < 400 * MHz)
		val = 0x25;
	else if(ddr_clk < 450 * MHz)
		val = 0x06; 
	else if(ddr_clk < 500 * MHz)
		val = 0x16;
	else if(ddr_clk < 550 * MHz)
		val = 0x07;
	else if(ddr_clk < 600 * MHz)
		val = 0x17;
	else if(ddr_clk < 650 * MHz)
		val = 0x08;
	else if(ddr_clk < 700 * MHz)
		val = 0x18;
	else if(ddr_clk < 750 * MHz)
		val = 0x09;
	else if(ddr_clk < 800 * MHz)
		val = 0x19;
	else if(ddr_clk < 850 * MHz)
		val = 0x29;
	else if(ddr_clk < 900 * MHz)
		val = 0x39;
	else if(ddr_clk < 950 * MHz)
		val = 0x0a;
	else if(ddr_clk < 1000 * MHz)
		val = 0x1a;
	else if(ddr_clk < 1050 * MHz)
		val = 0x2a;
	else if(ddr_clk < 1100* MHz)
		val = 0x3a;
	else if(ddr_clk < 1150* MHz)
		val = 0x0b;
	else if(ddr_clk < 1200 * MHz)
		val = 0x1b;
	else if(ddr_clk < 1250 * MHz)
		val = 0x2b;
	else if(ddr_clk < 1300 * MHz)
		val = 0x3b;
	else if(ddr_clk < 1350 * MHz)
		val = 0x0c;
	else if(ddr_clk < 1400* MHz)
		val = 0x1c;
	else if(ddr_clk < 1450* MHz)
		val = 0x2c;
	else if(ddr_clk <= 1500* MHz)
		val = 0x3c;

	test_data[0] = val << 1;
	rk32_dwc_phy_test_wr(dsi, code_hs_rx_lane0, test_data, 1);

	test_data[0] = prediv- 1;
	rk32_dwc_phy_test_wr(dsi, code_pll_input_div_rat, test_data, 1);
	mdelay(2);
	test_data[0] = (fbdiv - 1) & 0x1f; 
	rk32_dwc_phy_test_wr(dsi, code_pll_loop_div_rat, test_data, 1);
	mdelay(2);
	test_data[0] = (fbdiv - 1) >> 5 | 0x80; 
	rk32_dwc_phy_test_wr(dsi, code_pll_loop_div_rat, test_data, 1);
	mdelay(2);
	test_data[0] = 0x30;
	rk32_dwc_phy_test_wr(dsi, code_pll_input_loop_div_rat, test_data, 1);
	mdelay(2);

	test_data[0] = 0x4d;
	rk32_dwc_phy_test_wr(dsi, 0x20, test_data, 1);
	
	test_data[0] = 0x3d;
	rk32_dwc_phy_test_wr(dsi, 0x21, test_data, 1);
	
	test_data[0] = 0xdf;
	rk32_dwc_phy_test_wr(dsi, 0x21, test_data, 1); 	
	
	test_data[0] =  0x7;
	rk32_dwc_phy_test_wr(dsi, 0x22, test_data, 1);

	test_data[0] = 0x80 | 0x7;
	rk32_dwc_phy_test_wr(dsi, 0x22, test_data, 1);

	test_data[0] = 0x80 | 15;
	rk32_dwc_phy_test_wr(dsi, code_hstxdatalanerequsetstatetime, test_data, 1);

	test_data[0] = 0x80 | 85;
	rk32_dwc_phy_test_wr(dsi, code_hstxdatalanepreparestatetime, test_data, 1);

	test_data[0] = 0x40 | 10;
	rk32_dwc_phy_test_wr(dsi, code_hstxdatalanehszerostatetime, test_data, 1);

    return 0;
}

static int rk312x_phy_init(struct dsi *dsi, int n)
{
	//DPHY init
	rk32_dsi_set_bits(dsi, 0x11, DSI_DPHY_BITS(0x06<<2, 32, 0));
	rk32_dsi_set_bits(dsi, 0x11, DSI_DPHY_BITS(0x07<<2, 32, 0));
	rk32_dsi_set_bits(dsi, 0xcc, DSI_DPHY_BITS(0x09<<2, 32, 0));
#if 0
	dsi_set_bits(0x4e, DSI_DPHY_BITS(0x08<<2, 32, 0));
	dsi_set_bits(0x84, DSI_DPHY_BITS(0x0a<<2, 32, 0));
#endif

	/*reg1[4] 0: enable a function of "pll phase for serial data being captured 
				 inside analog part" 
		  1: disable it 
	  we disable it here because reg5[6:4] is not compatible with the HS speed.		
	*/

	if(dsi->phy.ddr_clk >= 800*MHz) {
		rk32_dsi_set_bits(dsi, 0x30, DSI_DPHY_BITS(0x05<<2, 32, 0));
	} else {
		rk32_dsi_set_bits(dsi, 1, reg_da_ppfc);
	}

	switch(dsi->host.lane) {
		case 4:
			rk312x_mipi_dsi_phy_set_gotp(dsi, DPHY_LANE3_OFFSET, n);
		case 3:
			rk312x_mipi_dsi_phy_set_gotp(dsi, DPHY_LANE2_OFFSET, n);
		case 2:
			rk312x_mipi_dsi_phy_set_gotp(dsi, DPHY_LANE1_OFFSET, n);
		case 1:
			rk312x_mipi_dsi_phy_set_gotp(dsi, DPHY_LANE0_OFFSET, n);
			rk312x_mipi_dsi_phy_set_gotp(dsi, DPHY_CLOCK_OFFSET, n);
			break;
		default:
			break;	
	}
/*
	rk32_dsi_set_bits(dsi, 0x00e4, reg1_phy);
	rk32_dsi_set_bits(dsi, 0x007d, reg0_phy);
	udelay(1000);
	rk32_dsi_set_bits(dsi, 0x00e0, reg1_phy);
	rk32_dsi_set_bits(dsi, 0x001e, reg20_phy);
	udelay(1000);
	rk32_dsi_set_bits(dsi, 0x001f, reg20_phy);

	rk32_dsi_set_bits(dsi, 0x0063, reg10_phy);
	*/
	
	rk32_dsi_set_bits(dsi, 0x06, reg5_phy);
	rk32_dsi_set_bits(dsi, 0x6, reg10_4_6_phy);
	rk32_dsi_set_bits(dsi, 0x9, regb_phy);
	return 0;

}

static int rk_phy_init(struct dsi *dsi)
{
	if (dsi->ops.id == DWC_DSI_VERSION)
		rk32_phy_init(dsi);
	else if (dsi->ops.id == DWC_DSI_VERSION_RK312x)
		rk312x_phy_init(dsi, 4);
	return 0;
}

static int rk32_mipi_dsi_host_power_up(struct dsi *dsi) 
{
	int ret = 0;
	u32 val;

	//disable all interrupt            
	rk32_dsi_set_bits(dsi, 0x1fffff, INT_MKS0);
	rk32_dsi_set_bits(dsi, 0x3ffff, INT_MKS1);

	rk32_mipi_dsi_is_enable(dsi, 1);
	
	val = 10;
	while(!rk32_dsi_get_bits(dsi, phylock) && val--) {
		udelay(10);
	};
	
	if(val == 0) {
		ret = -1;
		MIPI_TRACE("%s:phylock fail.\n", __func__);	
	}
	
	val = 10;
	while(!rk32_dsi_get_bits(dsi, phystopstateclklane) && val--) {
		udelay(10);
	};
	
	return ret;
}




static int rk32_mipi_dsi_host_power_down(struct dsi *dsi) 
{	
	rk32_mipi_dsi_enable_video_mode(dsi, 0);
	rk32_mipi_dsi_enable_hs_clk(dsi, 0);
	rk32_mipi_dsi_is_enable(dsi, 0);
	return 0;
}



static int rk32_mipi_dsi_host_init(struct dsi *dsi) 
{
	u32 val = 0, bytes_px = 0;
	struct mipi_dsi_screen *screen = &dsi->screen;
	u32 decimals = dsi->phy.Ttxbyte_clk, temp = 0, i = 0;
	u32 m = 1, lane = dsi->host.lane, Tpclk = dsi->phy.Tpclk, 
			Ttxbyte_clk = dsi->phy.Ttxbyte_clk;

	rk32_dsi_set_bits(dsi, dsi->host.lane - 1, n_lanes);
	rk32_dsi_set_bits(dsi, dsi->vid, dpi_vcid);
	
	switch(screen->face) {
		case OUT_P888:
			rk32_dsi_set_bits(dsi, 5, dpi_color_coding);
			bytes_px = 3;
			break;
		
		case OUT_D888_P666:
		
		case OUT_P666:
			rk32_dsi_set_bits(dsi, 3, dpi_color_coding);
			rk32_dsi_set_bits(dsi, 1, en18_loosely);
			bytes_px = 3;
			break;
		
		case OUT_P565:
			rk32_dsi_set_bits(dsi, 0, dpi_color_coding);
			bytes_px = 2;
		
		default:
			break;
	}
	if (dsi->ops.id == DWC_DSI_VERSION) {
		rk32_dsi_set_bits(dsi, 1, hsync_active_low);
		rk32_dsi_set_bits(dsi, 1, vsync_active_low);
		
		rk32_dsi_set_bits(dsi, 0, dataen_active_low);
		rk32_dsi_set_bits(dsi, 0, colorm_active_low);
		rk32_dsi_set_bits(dsi, 0, shutd_active_low);
	} else if (dsi->ops.id == DWC_DSI_VERSION_RK312x) {
		rk32_dsi_set_bits(dsi, !screen->pin_hsync, hsync_active_low);
		rk32_dsi_set_bits(dsi, !screen->pin_vsync, vsync_active_low);
		
		rk32_dsi_set_bits(dsi, screen->pin_den, dataen_active_low);
		rk32_dsi_set_bits(dsi, 1, colorm_active_low);
		rk32_dsi_set_bits(dsi, 1, shutd_active_low);

	}
	
	rk32_dsi_set_bits(dsi, dsi->host.video_mode, vid_mode_type);	  //burst mode
	
	switch(dsi->host.video_mode) {
	
		case VM_BM:
		    if(screen->type == SCREEN_DUAL_MIPI)
			    rk32_dsi_set_bits(dsi, screen->x_res / 2 + 4, vid_pkt_size);
			 else
			    rk32_dsi_set_bits(dsi, screen->x_res, vid_pkt_size);
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

			rk32_dsi_set_bits(dsi, screen->x_res / m + 1, num_chunks);
			rk32_dsi_set_bits(dsi, m, vid_pkt_size);
			temp = m * lane * Tpclk / Ttxbyte_clk - m * bytes_px;
			MIPI_DBG("%s:%d, %d\n", __func__, m, temp);
		
			if(temp >= 12) 
				rk32_dsi_set_bits(dsi, temp - 12, null_pkt_size);
				
			break;
			
		default:
		
			break;
	}	

	//rk32_dsi_set_bits(dsi, 0, CMD_MODE_CFG << 16);
	if(screen->type == SCREEN_MIPI){
		rk32_dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->x_res + screen->left_margin + 
					screen->hsync_len + screen->right_margin) \
						/ dsi->phy.Ttxbyte_clk, vid_hline_time);
	}
	else{
		rk32_dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->x_res + 8 + screen->left_margin + 
					screen->hsync_len + screen->right_margin) \
						/ dsi->phy.Ttxbyte_clk, vid_hline_time);	
	}
	MIPI_DBG("dsi->phy.Tpclk = %d\n", dsi->phy.Tpclk);
	MIPI_DBG("screen->left_margin = %d\n", screen->left_margin);
	MIPI_DBG("dsi->phy.Ttxbyte_clk = %d\n", dsi->phy.Ttxbyte_clk);
	MIPI_DBG("screen->hsync_len = %d\n", screen->hsync_len);
    rk32_dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->left_margin) / dsi->phy.Ttxbyte_clk, 
					vid_hbp_time);
	rk32_dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->hsync_len) / dsi->phy.Ttxbyte_clk, 
					vid_hsa_time);
    
	rk32_dsi_set_bits(dsi, screen->y_res , vid_active_lines);
	rk32_dsi_set_bits(dsi, screen->lower_margin, vid_vfp_lines);
	rk32_dsi_set_bits(dsi, screen->upper_margin, vid_vbp_lines);
	rk32_dsi_set_bits(dsi, screen->vsync_len, vid_vsa_lines);
	
	dsi->phy.txclkesc = 20 * MHz;
	val = dsi->phy.txbyte_clk / dsi->phy.txclkesc + 1;
	dsi->phy.txclkesc = dsi->phy.txbyte_clk / val;
	rk32_dsi_set_bits(dsi, val, TX_ESC_CLK_DIVISION);
	
	rk32_dsi_set_bits(dsi, 10, TO_CLK_DIVISION);
    rk32_dsi_set_bits(dsi, 1000, hstx_to_cnt); //no sure
	rk32_dsi_set_bits(dsi, 1000, lprx_to_cnt);	
	rk32_dsi_set_bits(dsi, 100, phy_stop_wait_time);

	//rk32_dsi_set_bits(dsi, 0, outvact_lpcmd_time);   //byte
	//rk32_dsi_set_bits(dsi, 0, invact_lpcmd_time);
		
	rk32_dsi_set_bits(dsi, 20, phy_hs2lp_time);
	rk32_dsi_set_bits(dsi, 16, phy_lp2hs_time);	

	// rk32_dsi_set_bits(dsi, 87, phy_hs2lp_time_clk_lane); //no sure
	//  rk32_dsi_set_bits(dsi, 25, phy_hs2hs_time_clk_lane); //no sure

	rk32_dsi_set_bits(dsi, 10000, max_rd_time);

	rk32_dsi_set_bits(dsi, 1, lp_hfp_en);
	//rk32_dsi_set_bits(dsi, 1, lp_hbp_en); //no sure
	rk32_dsi_set_bits(dsi, 1, lp_vact_en);
	rk32_dsi_set_bits(dsi, 1, lp_vfp_en);
	rk32_dsi_set_bits(dsi, 1, lp_vbp_en);
	rk32_dsi_set_bits(dsi, 1, lp_vsa_en);
	
	//rk32_dsi_set_bits(dsi, 1, frame_bta_ack_en);
	rk32_dsi_set_bits(dsi, 1, phy_enableclk);
	rk32_dsi_set_bits(dsi, 0, phy_tx_triggers);
	//rk32_dsi_set_bits(dsi, 1, phy_txexitulpslan);
	//rk32_dsi_set_bits(dsi, 1, phy_txexitulpsclk);
	return 0;
}

/*
 *	mipi protocol layer definition
 */
static int rk_mipi_dsi_init(void *arg, u32 n)
{
	u32 decimals = 1000, i = 0, pre = 0;
	struct dsi *dsi = arg;
	struct mipi_dsi_screen *screen = &dsi->screen;
	
	if(!screen)
		return -1;
	
	if((screen->type != SCREEN_MIPI) && (screen->type != SCREEN_DUAL_MIPI) ) {
		MIPI_TRACE("only mipi dsi lcd is supported!\n");
		return -1;
	}

	if(((screen->type == SCREEN_DUAL_MIPI) && (rk_mipi_get_dsi_num() == 1)) ||  ((screen->type == SCREEN_MIPI) && (rk_mipi_get_dsi_num() == 2))){
		MIPI_TRACE("dsi number and mipi type not match!\n");
	    return -1;
    }

	dsi->phy.Tpclk = rk_fb_get_prmry_screen_pixclock();

	if(dsi->phy.refclk)
		dsi->phy.ref_clk = clk_get_rate(dsi->phy.refclk) ;
	if (dsi->ops.id == DWC_DSI_VERSION_RK312x)
		dsi->phy.ref_clk = dsi->phy.ref_clk / 2;// 1/2 of input refclk
	
	dsi->phy.sys_clk = dsi->phy.ref_clk;

	printk("dsi->phy.sys_clk =%d\n",dsi->phy.sys_clk );
	if (dsi->ops.id == DWC_DSI_VERSION) {
		if((screen->hs_tx_clk <= 90 * MHz) || (screen->hs_tx_clk >= 1500 * MHz))
			dsi->phy.ddr_clk = 1500 * MHz;    //default is 1.5HGz
		else
			dsi->phy.ddr_clk = screen->hs_tx_clk;   
	} else if (dsi->ops.id == DWC_DSI_VERSION_RK312x) {
		if((screen->hs_tx_clk <= 80 * MHz) || (screen->hs_tx_clk >= 1000 * MHz))
			dsi->phy.ddr_clk = 1000 * MHz;    //default is 1GHz
		else
			dsi->phy.ddr_clk = screen->hs_tx_clk;   

	}

	if(n != 0) 
		dsi->phy.ddr_clk = n;

	decimals = dsi->phy.ref_clk;
	for(i = 1; i < 6; i++) {
		pre = dsi->phy.ref_clk / i;
		if((decimals > (dsi->phy.ddr_clk % pre)) && (dsi->phy.ddr_clk / pre < 512)) {
			decimals = dsi->phy.ddr_clk % pre;
			dsi->phy.prediv = i;
			dsi->phy.fbdiv = dsi->phy.ddr_clk / pre;
		}	
		if(decimals == 0) 
			break;
	}

	

	MIPI_DBG("prediv:%d, fbdiv:%d,dsi->phy.ddr_clk:%d\n", dsi->phy.prediv, dsi->phy.fbdiv,dsi->phy.ref_clk / dsi->phy.prediv * dsi->phy.fbdiv);

	dsi->phy.ddr_clk = dsi->phy.ref_clk / dsi->phy.prediv * dsi->phy.fbdiv;	

	MIPI_DBG("dsi->phy.ddr_clk =%d\n",dsi->phy.ddr_clk);
	dsi->phy.txbyte_clk = dsi->phy.ddr_clk / 8;
	
	dsi->phy.txclkesc = 20 * MHz;        // < 20MHz
	dsi->phy.txclkesc = dsi->phy.txbyte_clk / (dsi->phy.txbyte_clk / dsi->phy.txclkesc + 1);

	dsi->phy.pclk = div_u64(1000000000000llu, dsi->phy.Tpclk);
	dsi->phy.Ttxclkesc = div_u64(1000000000000llu, dsi->phy.txclkesc);
	dsi->phy.Tsys_clk = div_u64(1000000000000llu, dsi->phy.sys_clk);
	dsi->phy.Tddr_clk = div_u64(1000000000000llu, dsi->phy.ddr_clk);
	dsi->phy.Ttxbyte_clk = div_u64(1000000000000llu, dsi->phy.txbyte_clk);	

	dsi->phy.UI = dsi->phy.Tddr_clk;
	dsi->vid = 0;
	
	if(screen->dsi_lane > 0 && screen->dsi_lane <= 4)
		dsi->host.lane = screen->dsi_lane;
	else
		dsi->host.lane = 4;
		
	dsi->host.video_mode = VM_BM;
	
	MIPI_DBG("UI:%d\n", dsi->phy.UI);	
	MIPI_DBG("ref_clk:%d\n", dsi->phy.ref_clk);
	MIPI_DBG("pclk:%d, Tpclk:%d\n", dsi->phy.pclk, dsi->phy.Tpclk);
	MIPI_DBG("sys_clk:%d, Tsys_clk:%d\n", dsi->phy.sys_clk, dsi->phy.Tsys_clk);
	MIPI_DBG("ddr_clk:%d, Tddr_clk:%d\n", dsi->phy.ddr_clk, dsi->phy.Tddr_clk);
	MIPI_DBG("txbyte_clk:%d, Ttxbyte_clk:%d\n", dsi->phy.txbyte_clk, 
				dsi->phy.Ttxbyte_clk);
	MIPI_DBG("txclkesc:%d, Ttxclkesc:%d\n", dsi->phy.txclkesc, dsi->phy.Ttxclkesc);

	mdelay(10);
	rk_phy_power_up(dsi);
	rk32_mipi_dsi_host_power_up(dsi);
	rk_phy_init(dsi);
	rk32_mipi_dsi_host_init(dsi);
	
	return 0;
}

static int rk32_mipi_dsi_is_enable(void *arg, u32 enable)
{
	struct dsi *dsi = arg;

	rk32_dsi_set_bits(dsi, enable, shutdownz);

	return 0;
}

static int rk32_mipi_dsi_enable_video_mode(void *arg, u32 enable)
{
	struct dsi *dsi = arg;

	rk32_dsi_set_bits(dsi, !enable, cmd_video_mode);

	return 0;
}

static int rk32_mipi_dsi_enable_command_mode(void *arg, u32 enable)
{
	struct dsi *dsi = arg;

	rk32_dsi_set_bits(dsi, enable, cmd_video_mode);

	return 0;
}

static int rk32_mipi_dsi_enable_hs_clk(void *arg, u32 enable)
{
	struct dsi *dsi = arg;
	
	rk32_dsi_set_bits(dsi, enable, phy_txrequestclkhs);
	return 0;
}

static int rk32_mipi_dsi_is_active(void *arg)
{
	struct dsi *dsi = arg;
	
	return rk32_dsi_get_bits(dsi, shutdownz);
}

static int rk32_mipi_dsi_send_packet(void *arg, unsigned char cmds[], u32 length)
{
	struct dsi *dsi = arg;
	unsigned char *regs;
	u32 type, liTmp = 0, i = 0, j = 0, data = 0;

	if(rk32_dsi_get_bits(dsi, gen_cmd_full) == 1) {
		MIPI_TRACE("gen_cmd_full\n");
		return -1;
	}
	regs = kmalloc(0x400, GFP_KERNEL);
	if(!regs) {
		printk("request regs fail!\n");
		return -ENOMEM;
	}
	memcpy(regs,cmds,length);
	
	liTmp	= length - 2;
	type	= regs[1];
	
	switch(type)
	{
		case DTYPE_DCS_SWRITE_0P:
			rk32_dsi_set_bits(dsi, regs[0], dcs_sw_0p_tx);
			data = regs[2] << 8 | type;
			break;
		
		case DTYPE_DCS_SWRITE_1P:
			rk32_dsi_set_bits(dsi, regs[0], dcs_sw_1p_tx);
			data = regs[2] << 8 | type;
			data |= regs[3] << 16;
			break;
		    
		case DTYPE_DCS_LWRITE: 
			rk32_dsi_set_bits(dsi, regs[0], dcs_lw_tx);
		
			for(i = 0; i < liTmp; i++){
			    regs[i] = regs[i+2];
		    }
		
			for(i = 0; i < liTmp; i++) {
				j = i % 4;
				
				data |= regs[i] << (j * 8);
				if(j == 3 || ((i + 1) == liTmp)) {
					if(rk32_dsi_get_bits(dsi, gen_pld_w_full) == 1) {
						MIPI_TRACE("gen_pld_w_full :%d\n", i);
						break;
					}
					
					rk32_dsi_set_bits(dsi, data, GEN_PLD_DATA);
					MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
					data = 0;
				}
			}
			
			data = type;	
			data |= (liTmp & 0xffff) << 8;
			
			break;
			
		case DTYPE_GEN_LWRITE:
			rk32_dsi_set_bits(dsi, regs[0], gen_lw_tx);
			
			for(i = 0; i < liTmp; i++){
				regs[i] = regs[i+2];
			}
			
			for(i = 0; i < liTmp; i++) {
				j = i % 4;
				
				data |= regs[i] << (j * 8);
				if(j == 3 || ((i + 1) == liTmp)) {
					if(rk32_dsi_get_bits(dsi, gen_pld_w_full) == 1) {
						MIPI_TRACE("gen_pld_w_full :%d\n", i);
						break;
					}
					
					rk32_dsi_set_bits(dsi, data, GEN_PLD_DATA);
					MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
					data = 0;
				}
			}
		
			data = (dsi->vid << 6) | type;		
			data |= (liTmp & 0xffff) << 8;
			break;
	
		case DTYPE_GEN_SWRITE_2P:
			
			rk32_dsi_set_bits(dsi, regs[0], gen_sw_2p_tx);
			for(i = 0; i < liTmp; i++){
			    regs[i] = regs[i+2];
			}
			
			for(i = 0; i < liTmp; i++) {
				j = i % 4;
				data |= regs[i] << (j * 8);
				if(j == 3 || ((i + 1) == liTmp)) {
					if(rk32_dsi_get_bits(dsi, gen_pld_w_full) == 1) {
						MIPI_TRACE("gen_pld_w_full :%d\n", i);
						break;
					}
					
					rk32_dsi_set_bits(dsi, data, GEN_PLD_DATA);
					MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
					data = 0;
				}
			}
			data = type;		
			data |= (liTmp & 0xffff) << 8;

			break;
			
		case DTYPE_GEN_SWRITE_1P:
			rk32_dsi_set_bits(dsi, regs[0], gen_sw_1p_tx);
			data = type;
			data |= regs[2] << 8;
			data |= regs[3] << 16;
			break;
			
		case DTYPE_GEN_SWRITE_0P:
			rk32_dsi_set_bits(dsi, regs[0], gen_sw_0p_tx);
			data =  type;
			data |= regs[2] << 8;
			break;

		default:
		   printk("0x%x:this type not suppport!\n",type);
			
	}

	MIPI_DBG("%d command sent in %s size:%d\n", __LINE__, regs[0] ? "LP mode" : "HS mode", liTmp);
	
	MIPI_DBG("write GEN_HDR:%08x\n", data);
	rk32_dsi_set_bits(dsi, data, GEN_HDR);
	
	i = 10;
	while(!rk32_dsi_get_bits(dsi, gen_cmd_empty) && i--) {
		MIPI_DBG(".");
		udelay(10);
	}
	udelay(10);
	kfree(regs);
	return 0;
}

static int rk32_mipi_dsi_read_dcs_packet(void *arg, unsigned char *data1, u32 n)
{
	 struct dsi *dsi = arg;
	 //DCS READ
	 //unsigned char *regs = data;
	 unsigned char regs[2];
	 u32 data = 0;
	 int type = 0x06;
	 regs[0] = LPDT;
	 regs[1] = 0x0a;
	 n = n - 1;

	   
	 rk32_dsi_set_bits(dsi, regs[0], dcs_sr_0p_tx);
	 
	 /* if(type == DTYPE_GEN_SWRITE_0P)
	  data = (dsi->vid << 6) | (n << 4) | type;
	 else
	  data = (dsi->vid << 6) | ((n-1) << 4) | type;*/

	 data |= regs[1] << 8 | type;
	 // if(n == 2)
	 // data |= regs[1] << 16;

	 printk("write GEN_HDR:%08x\n", data);
	 
	 rk32_dsi_set_bits(dsi, 0xFFFF, bta_to_cnt);
	 rk32_dsi_set_bits(dsi, 1, bta_en);
	 rk32_dsi_set_bits(dsi, data, GEN_HDR);
	 //msleep(1);
	 udelay(20);
	   
	 // rk32_dsi_set_bits(dsi, regs[0], gen_sr_0p_tx);

	 printk("rk32_mipi_dsi_read_dcs_packet==0x%x\n",rk32_dsi_get_bits(dsi, GEN_PLD_DATA));
	 rk32_dsi_set_bits(dsi, 0, bta_en);

	 //msleep(100);

	 // rk32_dsi_set_bits(dsi, regs[0], max_rd_pkt_size);
	 
	 //msleep(100);
	 // printk("_____rk32_mipi_dsi_read_dcs_packet==0x%x\n",rk32_dsi_get_bits(dsi, GEN_PLD_DATA));
	 
	 //msleep(100);
 return 0;
}


static int rk32_mipi_dsi_power_up(void *arg)
{
	struct dsi *dsi = arg;
	
	rk32_phy_power_up(dsi);
	rk32_mipi_dsi_host_power_up(dsi);
	return 0;
}

static int rk32_mipi_dsi_power_down(void *arg)
{
	struct dsi *dsi = arg;
	struct mipi_dsi_screen *screen = &dsi->screen;

	if(!screen)
		return -1;

	rk32_mipi_dsi_host_power_down(dsi);
	rk_phy_power_down(dsi);

	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
	return 0;
}

static int rk32_mipi_dsi_get_id(void *arg)
{
	u32 id = 0;
	struct dsi *dsi = arg;
	
	id	= rk32_dsi_get_bits(dsi, VERSION);
	
	return id;
}

/* the most top level of mipi dsi init */
static int rk_mipi_dsi_probe(struct dsi *dsi)
{
	int ret = 0;
	
	register_dsi_ops(dsi->dsi_id, &dsi->ops);
	
	ret = dsi_probe_current_chip(dsi->dsi_id);
	if(ret) {
		MIPI_TRACE("mipi dsi probe fail\n");
		return -ENODEV;
	}

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
				printk("regs_val=0x%llx\n",regs_val);
				rk32_dsi_write_reg(dsi0, regs_val >> 32, &read_val);
				rk32_dsi_read_reg(dsi0, regs_val >> 32, &read_val);
				regs_val &= 0xffffffff;
				if(read_val != regs_val)
					MIPI_TRACE("%s fail:0x%08x\n", __func__, read_val);					
				data += 3;
				msleep(1);	
			}
		
			break;
		case 'r':
				data = strstr(data, "0x");
				if(data == NULL){
					goto reg_proc_write_exit;
				}
				sscanf(data, "0x%llx", &regs_val);
				rk32_dsi_read_reg(dsi0, (u16)regs_val, &read_val);
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

				//rk32_mipi_dsi_init_lite(dsi);
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
					sscanf(data, "%x,", (unsigned int *)(str + i));   //-c 1,29,02,03,05,06,> pro
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
						rk32_mipi_dsi_send_packet(dsi0, str, read_val);
					else
						rk32_mipi_dsi_send_packet(dsi0, str, read_val);
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

int reg_proc_read(struct file *file, char __user *buff, size_t count, 
					loff_t *offp)
{
	int i = 0;
	u32 val = 0;

    for(i = VERSION; i < (VERSION + (0xdc<<16)); i += 4<<16) {
		val = rk32_dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}

	MIPI_TRACE("\n");

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
	.owner  = THIS_MODULE,
	.open   = reg_proc_open,
	.release= reg_proc_close,
	.write  = reg_proc_write,
	.read   = reg_proc_read,
};


int reg_proc_write1(struct file *file, const char __user *buff, size_t count, loff_t *offp)
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
				rk32_dsi_write_reg(dsi1, regs_val >> 32, &read_val);
				rk32_dsi_read_reg(dsi1, regs_val >> 32, &read_val);
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
				rk32_dsi_read_reg(dsi1, (u16)regs_val, &read_val);
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

				//rk32_mipi_dsi_init_lite(dsi);
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
					sscanf(data, "%x,", (unsigned int *)(str + i));   //-c 1,29,02,03,05,06,> pro
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
						rk32_mipi_dsi_send_packet(dsi1, str, read_val);
					else
						rk32_mipi_dsi_send_packet(dsi1, str, read_val);
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

int reg_proc_read1(struct file *file, char __user *buff, size_t count, 
					loff_t *offp)
{
	int i = 0;
	u32 val = 0;
	
	for(i = VERSION; i < (VERSION + (0xdc<<16)); i += 4<<16) {
		val = rk32_dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	return -1;
}

int reg_proc_open1(struct inode *inode, struct file *file)
{
	return 0;
}

int reg_proc_close1(struct inode *inode, struct file *file)
{
	return 0;
}

struct file_operations reg_proc_fops1 = {
	.owner  = THIS_MODULE,
	.open   = reg_proc_open1,
	.release= reg_proc_close1,
	.write  = reg_proc_write1,
	.read   = reg_proc_read1,
};
#endif
#if 0//def CONFIG_MIPI_DSI_LINUX
static irqreturn_t rk32_mipi_dsi_irq_handler(int irq, void *data)
{
	printk("-------rk32_mipi_dsi_irq_handler-------\n");
	return IRQ_HANDLED;
}
#endif

#if 0
static int dwc_phy_test_rd(struct dsi *dsi, unsigned char test_code)
{
    int val = 0;

    rk32_dsi_set_bits(dsi, 0x10000 | test_code, PHY_TEST_CTRL1);
    rk32_dsi_set_bits(dsi, 0x2, PHY_TEST_CTRL0);
    rk32_dsi_set_bits(dsi, 0x0, PHY_TEST_CTRL0);

	val = rk32_dsi_get_bits(dsi, PHY_TEST_CTRL1);


    return val;
}
#endif
static int rk32_dsi_enable(void)
{
	MIPI_DBG("rk32_dsi_enable-------\n");
	rk_fb_get_prmry_screen(dsi0->screen.screen);
	dsi0->screen.lcdc_id = dsi0->screen.screen->lcdc_id;
	rk32_init_phy_mode(dsi0->screen.lcdc_id);
	
	dsi_init(0, 0);
	if (rk_mipi_get_dsi_num() ==2)
		dsi_init(1, 0);

	rk_mipi_screen_standby(0);

	/*
		After the core reset, DPI waits for the first VSYNC active transition to start signal sampling, including
		pixel data, and preventing image transmission in the middle of a frame.
	*/
	dsi_is_enable(0, 0);
	if (rk_mipi_get_dsi_num() ==2)
		dsi_is_enable(1, 0);  

	dsi_enable_video_mode(0, 1);
	if (rk_mipi_get_dsi_num() ==2)
		dsi_enable_video_mode(1, 1);

	dsi_is_enable(0, 1);
	if (rk_mipi_get_dsi_num() ==2)
		dsi_is_enable(1, 1);

	return 0;
}

#ifdef CONFIG_MIPI_DSI_LINUX
static int rk32_dsi_disable(void)
{
	MIPI_DBG("rk32_dsi_disable-------\n");
	rk_mipi_screen_standby(1);
	dsi_power_off(0);
	if (rk_mipi_get_dsi_num() == 2)
		dsi_power_off(1);
	return 0;
}

static struct rk_fb_trsm_ops trsm_dsi_ops = 
{
	.enable = rk32_dsi_enable,
	.disable = rk32_dsi_disable,
	.dsp_pwr_on = rk32_mipi_power_up_DDR,
	.dsp_pwr_off = rk32_mipi_power_down_DDR,
};
#endif
static void rk32_init_phy_mode(int lcdc_id)
{ 
	int val0 = 0, val1 = 0;

	MIPI_DBG("rk32_init_phy_mode----------lcdc_id=%d\n",lcdc_id);
	
	if (dsi0->ops.id == DWC_DSI_VERSION_RK312x)
		return;
	
	//D-PHY mode select
	if( rk_mipi_get_dsi_num() ==1 ){
	
		if(lcdc_id == 1)
			//val0 =0x1 << 25 | 0x1 << 9;
			val0 = 0x1 << 22 | 0x1 << 6;  //1'b1: VOP LIT output to DSI host0;1'b0: VOP BIG output to DSI host0
		else
			val0 = 0x1 << 22 | 0x0 << 6; 

		writel_relaxed(val0, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);
	}
	else{
		if(lcdc_id == 1){
			val0 = 0x1 << 25 | 0x1 <<  9 | 0x1 << 22 | 0x1 <<  6; 
			val1 = 0x1 << 31 | 0x1 << 30 | 0x0 << 15 | 0x1 << 14; 
		}
		else{
			val0 = 0x1 << 25 | 0x0 <<  9 | 0x1 << 22 | 0x0 << 14; 
			val1 = 0x1 << 31 | 0x1 << 30 | 0x0 << 15 | 0x1 << 14; 
		}

		writel_relaxed(val0, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);
		writel_relaxed(val1, RK_GRF_VIRT + RK3288_GRF_SOC_CON14);
	}
}

#ifdef CONFIG_MIPI_DSI_LINUX
static int rk32_mipi_power_down_DDR(void)
{	
	dsi_is_enable(0, 0);	
	if (rk_mipi_get_dsi_num() ==2)	    
		dsi_is_enable(1, 0);  		
	return 0;   
}

static int rk32_mipi_power_up_DDR(void)
{	
	dsi_is_enable(0, 0);	
	if (rk_mipi_get_dsi_num() ==2)	    
		dsi_is_enable(1, 0);     		
	dsi_enable_video_mode(0, 1);	
	dsi_enable_video_mode(1, 1);		
	dsi_is_enable(0, 1);	
	if (rk_mipi_get_dsi_num() ==2)	    
		dsi_is_enable(1, 1);	
	return 0;
}

struct dsi_type {
	char * label;
	u32 dsi_id;
};

static struct dsi_type dsi_rk312x = {
	.label = "rk312-dsi",
	.dsi_id = DWC_DSI_VERSION_RK312x,
};

static struct dsi_type dsi_rk32 = {
	.label = "rk32-dsi",
	.dsi_id = DWC_DSI_VERSION,
};

static const struct of_device_id of_rk_mipi_dsi_match[] = {
	{ .compatible = "rockchip,rk32-dsi", .data = &dsi_rk32},
	{ .compatible = "rockchip,rk312x-dsi", .data = &dsi_rk312x},
	{ /* Sentinel */ } 
}; 

static int rk32_mipi_dsi_probe(struct platform_device *pdev)
{
	int ret = 0; 
	static int id = 0;
	struct dsi *dsi;
	struct mipi_dsi_ops *ops;
	struct rk_screen *screen;
	struct mipi_dsi_screen *dsi_screen;
	struct resource *res_host, *res_phy;
	const struct dsi_type *data;
 	const struct of_device_id *of_id =
		of_match_device(of_rk_mipi_dsi_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "failed to match device\n");
		return -ENODEV;
	}
	data = of_id->data;

	dsi = devm_kzalloc(&pdev->dev, sizeof(struct dsi), GFP_KERNEL);
	if(!dsi) {
		dev_err(&pdev->dev,"request struct dsi fail!\n");
		return -ENOMEM;
	}
	dsi->ops.id = data->dsi_id;
	printk(KERN_INFO "%s\n", data->label);

	if (dsi->ops.id == DWC_DSI_VERSION) {
		res_host = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		dsi->host.membase = devm_request_and_ioremap(&pdev->dev, res_host);
		if (!dsi->host.membase){
			dev_err(&pdev->dev, "get resource mipi host membase fail!\n");
			return -ENOMEM;
		}
	}else if (dsi->ops.id == DWC_DSI_VERSION_RK312x) {
		res_host = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mipi_dsi_host");
		dsi->host.membase = devm_request_and_ioremap(&pdev->dev, res_host);
		if (!dsi->host.membase){
			dev_err(&pdev->dev, "get resource mipi host membase fail!\n");
			return -ENOMEM;
		}
		res_phy = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mipi_dsi_phy");
		dsi->phy.membase = devm_request_and_ioremap(&pdev->dev, res_phy);
		if (!dsi->phy.membase){
			dev_err(&pdev->dev, "get resource mipi phy membase fail!\n");
			return -ENOMEM;
		}
	}
	
	dsi->phy.refclk  = devm_clk_get(&pdev->dev, "clk_mipi_24m"); 
	if (unlikely(IS_ERR(dsi->phy.refclk))) {
		dev_err(&pdev->dev, "get clk_mipi_24m clock fail\n");
		return PTR_ERR(dsi->phy.refclk);
	}

	/* Get the mipi phy pclk */
	dsi->dsi_pclk = devm_clk_get(&pdev->dev, "pclk_mipi_dsi");
	if (unlikely(IS_ERR(dsi->dsi_pclk))) {
		dev_err(&pdev->dev, "get pclk_mipi_dsi clock fail\n");
		return PTR_ERR(dsi->dsi_pclk);
	}
	
	if(dsi->ops.id == DWC_DSI_VERSION_RK312x) {
		/* Get the mipi host pclk */
		dsi->dsi_host_pclk = devm_clk_get(&pdev->dev, "pclk_mipi_dsi_host");
		if (unlikely(IS_ERR(dsi->dsi_host_pclk))) {
			dev_err(&pdev->dev, "get pclk_mipi_dsi_host clock fail\n");
			return PTR_ERR(dsi->dsi_host_pclk);
		}
		/* Get the pd_vio AHB h2p bridge clock */
		dsi->h2p_hclk = devm_clk_get(&pdev->dev, "hclk_vio_h2p");
		if (unlikely(IS_ERR(dsi->h2p_hclk))) {
			dev_err(&pdev->dev, "get hclk_vio_h2p clock fail\n");
			return PTR_ERR(dsi->h2p_hclk);
		}		
	}
	dsi->dsi_pd = devm_clk_get(&pdev->dev, "pd_mipi_dsi");
	if (unlikely(IS_ERR(dsi->dsi_pd))) {
		dev_err(&pdev->dev, "get pd_mipi_dsi clock fail\n");
		return PTR_ERR(dsi->dsi_pd);
	}
	dsi->host.irq = platform_get_irq(pdev, 0);
	if (dsi->host.irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return dsi->host.irq;
	}
	
	//ret = request_irq(dsi->host.irq, rk32_mipi_dsi_irq_handler, 0,dev_name(&pdev->dev), dsi);
	//if(ret) {
	//	dev_err(&pdev->dev, "request mipi_dsi irq fail\n");
	//	return -EINVAL;
	//}
	printk("dsi->host.irq =%d\n",dsi->host.irq); 

	disable_irq(dsi->host.irq);

	screen = devm_kzalloc(&pdev->dev, sizeof(struct rk_screen), GFP_KERNEL);
	if(!screen) {
		dev_err(&pdev->dev,"request struct rk_screen fail!\n");
		return -1;
	}
	rk_fb_get_prmry_screen(screen);

	dsi->pdev = pdev;
	ops = &dsi->ops;
	ops->dsi = dsi;

	ops->get_id = rk32_mipi_dsi_get_id,
	ops->dsi_send_packet = rk32_mipi_dsi_send_packet;
	ops->dsi_read_dcs_packet = rk32_mipi_dsi_read_dcs_packet,
	ops->dsi_enable_video_mode = rk32_mipi_dsi_enable_video_mode,
	ops->dsi_enable_command_mode = rk32_mipi_dsi_enable_command_mode,
	ops->dsi_enable_hs_clk = rk32_mipi_dsi_enable_hs_clk,
	ops->dsi_is_active = rk32_mipi_dsi_is_active,
	ops->dsi_is_enable= rk32_mipi_dsi_is_enable,
	ops->power_up = rk32_mipi_dsi_power_up,
	ops->power_down = rk32_mipi_dsi_power_down,
	ops->dsi_init = rk_mipi_dsi_init,

	dsi_screen = &dsi->screen;
	dsi_screen->screen = screen;
	dsi_screen->type = screen->type;
	dsi_screen->face = screen->face;
	dsi_screen->lcdc_id = screen->lcdc_id;
	dsi_screen->screen_id = screen->screen_id;
	dsi_screen->pixclock = screen->mode.pixclock;
	dsi_screen->left_margin = screen->mode.left_margin;
	dsi_screen->right_margin = screen->mode.right_margin;
	dsi_screen->hsync_len = screen->mode.hsync_len;
	dsi_screen->upper_margin = screen->mode.upper_margin;
	dsi_screen->lower_margin = screen->mode.lower_margin;
	dsi_screen->vsync_len = screen->mode.vsync_len;
	dsi_screen->x_res = screen->mode.xres;
	dsi_screen->y_res = screen->mode.yres;
	dsi_screen->pin_hsync = screen->pin_hsync;
	dsi_screen->pin_vsync = screen->pin_vsync;
	dsi_screen->pin_den = screen->pin_den;
	dsi_screen->pin_dclk = screen->pin_dclk;
	dsi_screen->dsi_lane = rk_mipi_get_dsi_lane();
//  dsi_screen->dsi_video_mode = screen->dsi_video_mode; //no sure
	dsi_screen->dsi_lane = rk_mipi_get_dsi_lane();
	dsi_screen->hs_tx_clk = rk_mipi_get_dsi_clk();  
	//dsi_screen->lcdc_id = 1;


	dsi->dsi_id = id++;

	sprintf(ops->name, "rk_mipi_dsi.%d", dsi->dsi_id);
	platform_set_drvdata(pdev, dsi);

	ret = rk_mipi_dsi_probe(dsi);
	if(ret) {
		dev_err(&pdev->dev,"rk mipi_dsi probe fail!\n");
		dev_err(&pdev->dev,"%s\n", RK_MIPI_DSI_VERSION_AND_TIME);
		return -1;
	}	

	if(id == 1){

		//if(!support_uboot_display())
		//	rk32_init_phy_mode(dsi_screen->lcdc_id);
		rk_fb_trsm_ops_register(&trsm_dsi_ops, SCREEN_MIPI);
	
#ifdef MIPI_DSI_REGISTER_IO        
		debugfs_create_file("mipidsi0", S_IFREG | S_IRUGO, dsi->debugfs_dir, dsi, 
							&reg_proc_fops);
#endif
		dsi0 = dsi;
	}else{   
		dsi1 = dsi;

#ifdef MIPI_DSI_REGISTER_IO  
        debugfs_create_file("mipidsi1", S_IFREG | S_IRUGO, dsi->debugfs_dir, dsi, 
							&reg_proc_fops1);
#endif

	}

    if(support_uboot_display()){
	    clk_prepare_enable(dsi->phy.refclk); 
	    clk_prepare_enable(dsi->dsi_pclk);
		if (dsi->ops.id == DWC_DSI_VERSION_RK312x) {
			clk_prepare_enable(dsi->dsi_host_pclk);
			clk_prepare_enable(dsi->h2p_hclk);
		}
	clk_prepare_enable(dsi->dsi_pd);
	    udelay(10);
    }
	dev_info(&pdev->dev,"rk mipi_dsi probe success!\n");
	dev_info(&pdev->dev,"%s\n", RK_MIPI_DSI_VERSION_AND_TIME);
	
	return ret;
}

static struct platform_driver rk32_mipi_dsi_driver = {
	.probe		= rk32_mipi_dsi_probe,
	.driver		= {
		.name	= "rk32-mipi",
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table	= of_rk_mipi_dsi_match,
#endif		
	},
};

static int __init rk32_mipi_dsi_init(void)
{
	return platform_driver_register(&rk32_mipi_dsi_driver);
}
fs_initcall(rk32_mipi_dsi_init);

static void __exit rk32_mipi_dsi_exit(void)
{
	platform_driver_unregister(&rk32_mipi_dsi_driver);
}
module_exit(rk32_mipi_dsi_exit);
#endif
