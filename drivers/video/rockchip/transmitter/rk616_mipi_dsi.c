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
#define MIPI_DSI_REGISTER_IO	0
#define CONFIG_MIPI_DSI_LINUX   0
//#define CONFIG_MIPI_DSI_FT 	1
//#define CONFIG_MFD_RK616   	1
//#define CONFIG_ARCH_RK319X    1
#define CONFIG_ARCH_RK3288    1

#ifdef CONFIG_MIPI_DSI_LINUX
#if defined(CONFIG_MFD_RK616)
#define DWC_DSI_VERSION		0x3131302A
#define DWC_DSI_VERSION_0x3131302A 1
#elif defined(CONFIG_ARCH_RK319X)
#define DWC_DSI_VERSION		0x3132312A
#define DWC_DSI_VERSION_0x3132312A 1
#elif defined(CONFIG_ARCH_RK3288)
#define DWC_DSI_VERSION		0x3133302A
#define DWC_DSI_VERSION_0x3133302A 1
#else
#define DWC_DSI_VERSION -1
#endif  /* CONFIG_MFD_RK616 */
#else
#define DWC_DSI_VERSION		0x3131302A
#endif  /* end of CONFIG_MIPI_DSI_LINUX*/


#ifdef CONFIG_MIPI_DSI_LINUX
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rk616.h>
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

#else
#include "ft_lcd.h"
#endif
#include <linux/dma-mapping.h>
#include "mipi_dsi.h"
#include "rk616_mipi_dsi.h"
#include <linux/rockchip/iomap.h>



#if 1
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
*v1.4 : add register temp to reduce the time driver resume takes when 
		use I2C.
*v1.5 : change early suspend level (BLANK_SCREEN + 1)
*v1.6 : add dsi_rk616->resume to reduce the time driver resume takes
*v2.0 : add mipi dsi support for rk319x
*v2.1 : add inset and unplug the hdmi, mipi's lcd will be reset.
*v2.2 : fix bug of V1.4 register temp, dpicolom
*v3.0 : support kernel 3.10 and device tree 
*/
#define RK_MIPI_DSI_VERSION_AND_TIME  "rockchip mipi_dsi v3.0 2014-03-06"

static struct dsi *dsi0;
static struct dsi *dsi1;


#ifdef CONFIG_MFD_RK616
static struct mfd_rk616 *dsi_rk616;
static struct rk29fb_screen *g_rk29fd_screen = NULL;
#endif

#ifdef CONFIG_MIPI_DSI_FT
#define udelay 		DRVDelayUs
#define msleep 		DelayMs_nops
static u32 fre_to_period(u32 fre);
#endif
static int rk_mipi_dsi_is_active(void *arg);
static int rk_mipi_dsi_enable_hs_clk(void *arg, u32 enable);
static int rk_mipi_dsi_enable_video_mode(void *arg, u32 enable);
static int rk_mipi_dsi_enable_command_mode(void *arg, u32 enable);
static int rk_mipi_dsi_send_dcs_packet(void *arg, unsigned char regs[], u32 n);
static int rk_mipi_dsi_is_enable(void *arg, u32 enable);
int rk_mipi_screen_standby(u8 enable);

#ifdef CONFIG_MFD_RK616
static u32 *host_mem = NULL;
static u32 *phy_mem = NULL;
#endif

static int dsi_read_reg(struct dsi *dsi, u16 reg, u32 *pval)
{
#ifdef CONFIG_MIPI_DSI_LINUX

#if defined(CONFIG_MFD_RK616)
	return dsi_rk616->read_dev(dsi_rk616, reg, pval);
#elif defined(CONFIG_ARCH_RK319X)
	if(reg >= MIPI_DSI_HOST_OFFSET)
		*pval = __raw_readl(dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
	else if(reg >= MIPI_DSI_PHY_OFFSET)
		*pval = __raw_readl(dsi->phy.membase + (reg - MIPI_DSI_PHY_OFFSET));
	return 0;
#elif defined(CONFIG_ARCH_RK3288)
	*pval = __raw_readl(dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
	return 0;
#endif  /* CONFIG_MFD_RK616 */

#else

#ifdef CONFIG_MIPI_DSI_FT
	return JETTA_ReadControlRegister(reg, pval);
#endif  /* CONFIG_MIPI_DSI_FT */

#endif  /* end of CONFIG_MIPI_DSI_LINUX */
}


static int dsi_write_reg(struct dsi *dsi, u16 reg, u32 *pval)
{
#ifdef CONFIG_MIPI_DSI_LINUX

#if defined(CONFIG_MFD_RK616)
	return dsi_rk616->write_dev(dsi_rk616, reg, pval);
#elif defined(CONFIG_ARCH_RK319X)
	if(reg >= MIPI_DSI_HOST_OFFSET)
		__raw_writel(*pval, dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
	else if(reg >= MIPI_DSI_PHY_OFFSET)
		__raw_writel(*pval, dsi->phy.membase + (reg - MIPI_DSI_PHY_OFFSET));	
	return 0;
#elif defined(CONFIG_ARCH_RK3288)
	__raw_writel(*pval, dsi->host.membase + (reg - MIPI_DSI_HOST_OFFSET));
	return 0;
#endif  /* CONFIG_MFD_RK616 */

#else

#ifdef CONFIG_MIPI_DSI_FT
	return JETTA_WriteControlRegister(reg, *pval);
#endif  /* CONFIG_MIPI_DSI_FT */

#endif  /* end of CONFIG_MIPI_DSI_LINUX */
}

#ifdef CONFIG_MFD_RK616
static int dsi_write_reg_bulk(u16 reg, u32 count, u32 *pval)
{
	return dsi_rk616->write_bulk(dsi_rk616, reg, count, pval);
}
#endif

static int dsi_get_bits(struct dsi *dsi, u32 reg)
{
	u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	if(bits < 32)
		bits = (1 << bits) - 1;
	else
		bits = 0xffffffff;
	dsi_read_reg(dsi, reg_addr, &val);
	val >>= offset;
	val &= bits;
	return val;
}

static int dsi_set_bits(struct dsi *dsi, u32 data, u32 reg) 
{
	u32 val = 0;
	u32 bits = (reg >> 8) & 0xff;
	u16 reg_addr = (reg >> 16) & 0xffff;
	u8 offset = reg & 0xff;
	if(bits < 32)
		bits = (1 << bits) - 1;
	else
		bits = 0xffffffff;

	if(bits != 0xffffffff) {
#ifdef CONFIG_MFD_RK616
		if(reg_addr >= MIPI_DSI_HOST_OFFSET) {
			val = host_mem[(reg_addr - MIPI_DSI_HOST_OFFSET)>>2];
		} else if(reg_addr >= MIPI_DSI_PHY_OFFSET) {
			val = phy_mem[(reg_addr - MIPI_DSI_PHY_OFFSET)>>2];
		} else
			dsi_read_reg(dsi, reg_addr, &val);
		if(val == 0xaaaaaaaa)
			dsi_read_reg(dsi, reg_addr, &val);
#else
		dsi_read_reg(dsi, reg_addr, &val);
#endif
	}

	val &= ~(bits << offset);
	val |= (data & bits) << offset;
	//printk("%s:%04x->%08x\n", __func__, reg_addr, val);
	dsi_write_reg(dsi, reg_addr, &val);
#ifdef CONFIG_MFD_RK616
	if(reg_addr >= MIPI_DSI_HOST_OFFSET) {
		host_mem[(reg_addr - MIPI_DSI_HOST_OFFSET)>>2] = val;
	} else if(reg_addr >= MIPI_DSI_PHY_OFFSET) {
		phy_mem[(reg_addr - MIPI_DSI_PHY_OFFSET)>>2] = val;
	}
#endif

	if(data > bits) {
		MIPI_TRACE("%s error reg_addr:0x%04x, offset:%d, bits:0x%04x, value:0x%04x\n", 
				__func__, reg_addr, offset, bits, data);
	}
	return 0;
}

static int dwc_phy_test_rd(struct dsi *dsi, unsigned char test_code)
{
    int val = 0;
    dsi_set_bits(dsi, 1, phy_testclk);
    dsi_set_bits(dsi, test_code, phy_testdin);
    dsi_set_bits(dsi, 1, phy_testen);
	dsi_set_bits(dsi, 0, phy_testclk);
	dsi_set_bits(dsi, 0, phy_testen);;

    dsi_set_bits(dsi, 0, phy_testen);
    val = dsi_get_bits(dsi,phy_testdout);
    dsi_set_bits(dsi, 1, phy_testclk);
    dsi_set_bits(dsi, 0, phy_testclk);

    return val;
}


static int dwc_phy_test_wr(struct dsi *dsi, unsigned char test_code, unsigned char *test_data, unsigned char size)
{
	int i = 0;
 
    dsi_set_bits(dsi, 0x10000 | test_code, PHY_TEST_CTRL1);
    dsi_set_bits(dsi, 0x2, PHY_TEST_CTRL0);
    dsi_set_bits(dsi, 0x0, PHY_TEST_CTRL0);

	for(i = 0; i < size; i++) {
    	dsi_set_bits(dsi, test_data[i], PHY_TEST_CTRL1);
        dsi_set_bits(dsi, 0x2, PHY_TEST_CTRL0);
        dsi_set_bits(dsi, 0x0, PHY_TEST_CTRL0);
        MIPI_DBG("dwc_phy_test_wr:%08x\n", dsi_get_bits(dsi, PHY_TEST_CTRL1));
	}
	return 0;
}

#ifdef CONFIG_MFD_RK616
static int rk_mipi_recover_reg(void) 
{
	u32 reg_addr = 0, count = 0, i = 0;
	
	for(i = 0x0c; i < MIPI_DSI_PHY_SIZE; i += 4) {
		if(phy_mem[i>>2] != 0xaaaaaaaa) {
			count++;
		}
			
		if((phy_mem[i>>2] == 0xaaaaaaaa) && (phy_mem[(i-4) >> 2] != 0xaaaaaaaa)) {
loop1:		reg_addr = i - (count<<2);
			dsi_write_reg_bulk(reg_addr + MIPI_DSI_PHY_OFFSET, count, 
								phy_mem+(reg_addr>>2));
			//printk("%4x:%08x\n", reg_addr, phy_mem[reg_addr>>2]);
			count = 0;
		}
		if((i == (MIPI_DSI_PHY_SIZE-4)) && (count != 0)) {
			i = MIPI_DSI_PHY_SIZE;		
			goto loop1;
		}
	}
	count = 0;
	for(i = 0x08; i < MIPI_DSI_HOST_SIZE; i += 4) {
		if(host_mem[i>>2] != 0xaaaaaaaa) {
			count++;
		}
			
		if((host_mem[i>>2] == 0xaaaaaaaa) && (host_mem[(i-4) >> 2] != 0xaaaaaaaa)) {
loop2:		reg_addr = i - (count<<2);
			dsi_write_reg_bulk(reg_addr + MIPI_DSI_HOST_OFFSET, count, 
								host_mem+(reg_addr>>2));
			//printk("%4x:%08x\n", reg_addr, host_mem[reg_addr>>2]);
			count = 0;
		}
		if((i == (MIPI_DSI_HOST_SIZE-4)) && (count != 0))		
			goto loop2;
	}		
	return 0;
}
#endif
#if defined(CONFIG_MFD_RK616) || defined(CONFIG_ARCH_RK319X)
static int inno_phy_set_gotp(struct dsi *dsi, u32 offset) 
{
	u32 val = 0, temp = 0, Tlpx = 0;
	u32 ddr_clk = dsi->phy.ddr_clk;
	u32 Ttxbyte_clk = dsi->phy.Ttxbyte_clk;
	u32 Tsys_clk = dsi->phy.Tsys_clk;
	u32 Ttxclkesc = dsi->phy.Ttxclkesc;
	
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
	dsi_set_bits(dsi, val, reg_ths_settle + offset);
	
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
	dsi_set_bits(dsi, val, reg_hs_ths_prepare + offset);

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
	dsi_set_bits(dsi, val, reg_hs_the_zero + offset);
	
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

	dsi_set_bits(dsi, val, reg_hs_ths_trail + offset);
	val = 120000 / Ttxbyte_clk + 1;
	MIPI_DBG("reg_hs_ths_exit: %d, %d\n", val, val*Ttxbyte_clk/1000);
	dsi_set_bits(dsi, val, reg_hs_ths_exit + offset);
	
	if(offset == DPHY_CLOCK_OFFSET) {
		val = (60000 + 52*dsi->phy.UI) / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_post: %d, %d\n", val, val*Ttxbyte_clk/1000);
		dsi_set_bits(dsi, val, reg_hs_tclk_post + offset);
		val = 10*dsi->phy.UI / Ttxbyte_clk + 1;
		MIPI_DBG("reg_hs_tclk_pre: %d, %d\n", val, val*Ttxbyte_clk/1000);	
		dsi_set_bits(dsi, val, reg_hs_tclk_pre + offset);
	}

	val = 1010000000 / Tsys_clk + 1;
	MIPI_DBG("reg_hs_twakup: %d, %d\n", val, val*Tsys_clk/1000);
	if(val > 0x3ff) {
		val = 0x2ff;
		MIPI_DBG("val is too large, 0x3ff is the largest\n");	
	}
	temp = (val >> 8) & 0x03;
	val &= 0xff;	
	dsi_set_bits(dsi, temp, reg_hs_twakup_h + offset);	
	dsi_set_bits(dsi, val, reg_hs_twakup_l + offset);
	
	if(Ttxclkesc > 50000) {
		val = 2*Ttxclkesc;
		MIPI_DBG("Ttxclkesc:%d\n", Ttxclkesc);
	}
	val = val / Ttxbyte_clk;
	Tlpx = val*Ttxbyte_clk;
	MIPI_DBG("reg_hs_tlpx: %d, %d\n", val, Tlpx);
	val -= 2;
	dsi_set_bits(dsi, val, reg_hs_tlpx + offset);
	
	Tlpx = 2*Ttxclkesc;
	val = 4*Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_go: %d, %d\n", val, val*Ttxclkesc);
	dsi_set_bits(dsi, val, reg_hs_tta_go + offset);
	val = 3 * Tlpx / 2 / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_sure: %d, %d\n", val, val*Ttxclkesc);	
	dsi_set_bits(dsi, val, reg_hs_tta_sure + offset);
	val = 5 * Tlpx / Ttxclkesc;
	MIPI_DBG("reg_hs_tta_wait: %d, %d\n", val, val*Ttxclkesc);
	dsi_set_bits(dsi, val, reg_hs_tta_wait + offset);
	return 0;
}

static int inno_set_hs_clk(struct dsi *dsi) 
{
	dsi_set_bits(dsi, dsi->phy.prediv, reg_prediv);
	dsi_set_bits(dsi, dsi->phy.fbdiv & 0xff, reg_fbdiv);
	dsi_set_bits(dsi, (dsi->phy.fbdiv >> 8) & 0x01, reg_fbdiv_8);
	return 0;
}

static int inno_phy_power_up(struct dsi *dsi)
{
	inno_set_hs_clk(dsi);
#if defined(CONFIG_ARCH_RK319X)
	//enable ref clock
	clk_enable(dsi->phy.refclk);
	udelay(10);
#endif
	dsi_set_bits(dsi, 0xe4, DPHY_REGISTER1);
	switch(dsi->host.lane) {
		case 4:
			dsi_set_bits(dsi, 1, lane_en_3);
		case 3:
			dsi_set_bits(dsi, 1, lane_en_2);
		case 2:
			dsi_set_bits(dsi, 1, lane_en_1);
		case 1:
			dsi_set_bits(dsi, 1, lane_en_0);
			dsi_set_bits(dsi, 1, lane_en_ck);
			break;
		default:
			break;	
	}

	dsi_set_bits(dsi, 0xe0, DPHY_REGISTER1);
	udelay(10);

	dsi_set_bits(dsi, 0x1e, DPHY_REGISTER20);
	dsi_set_bits(dsi, 0x1f, DPHY_REGISTER20);
	return 0;
}

static int inno_phy_power_down(struct dsi *dsi) 
{
	dsi_set_bits(dsi, 0x01, DPHY_REGISTER0);
	dsi_set_bits(dsi, 0xe3, DPHY_REGISTER1);
#if defined(CONFIG_ARCH_RK319X)
	//disable ref clock
	clk_disable(dsi->phy.refclk);
#endif
	return 0;
}

static int inno_phy_init(struct dsi *dsi) 
{
	//DPHY init
	dsi_set_bits(dsi, 0x11, DSI_DPHY_BITS(0x06<<2, 32, 0));
	dsi_set_bits(dsi, 0x11, DSI_DPHY_BITS(0x07<<2, 32, 0));
	dsi_set_bits(dsi, 0xcc, DSI_DPHY_BITS(0x09<<2, 32, 0));
#if 0
	dsi_set_bits(dsi, 0x4e, DSI_DPHY_BITS(0x08<<2, 32, 0));
	dsi_set_bits(dsi, 0x84, DSI_DPHY_BITS(0x0a<<2, 32, 0));
#endif

	/*reg1[4] 0: enable a function of "pll phase for serial data being captured 
				 inside analog part" 
	          1: disable it 
	  we disable it here because reg5[6:4] is not compatible with the HS speed. 		
	*/

	if(dsi->phy.ddr_clk >= 800*MHz) {
		dsi_set_bits(dsi, 0x30, DSI_DPHY_BITS(0x05<<2, 32, 0));
	} else {
		dsi_set_bits(dsi, 1, reg_da_ppfc);
	}

	switch(dsi->host.lane) {
		case 4:
			inno_phy_set_gotp(dsi, DPHY_LANE3_OFFSET);
		case 3:
			inno_phy_set_gotp(dsi, DPHY_LANE2_OFFSET);
		case 2:
			inno_phy_set_gotp(dsi, DPHY_LANE1_OFFSET);
		case 1:
			inno_phy_set_gotp(dsi, DPHY_LANE0_OFFSET);
			inno_phy_set_gotp(dsi, DPHY_CLOCK_OFFSET);
			break;
		default:
			break;	
	}	
	return 0;
}
#endif
static int rk32_phy_power_up(struct dsi *dsi)
{
    //enable ref clock
    clk_prepare_enable(dsi->phy.refclk); 
    clk_prepare_enable(dsi->dsi_pclk);
    udelay(10);

	switch(dsi->host.lane) {
		case 4:
			dsi_set_bits(dsi, 3, n_lanes);
		case 3:
			dsi_set_bits(dsi, 2, n_lanes);
		case 2:
			dsi_set_bits(dsi, 1, n_lanes);
		case 1:
			dsi_set_bits(dsi, 0, n_lanes);
			break;
		default:
			break;	
	}
    dsi_set_bits(dsi, 1, phy_shutdownz);
    dsi_set_bits(dsi, 1, phy_rstz);  
    dsi_set_bits(dsi, 1, phy_enableclk);
    dsi_set_bits(dsi, 1, phy_forcepll);
    return 0;
}

static int rk32_phy_power_down(struct dsi *dsi)
{
    dsi_set_bits(dsi, 0, phy_shutdownz);
    clk_disable_unprepare(dsi->phy.refclk); 
    clk_disable_unprepare(dsi->dsi_pclk);
    return 0;
}

static int rk32_phy_init(struct dsi *dsi)
{
    u32 val = 0;
    u32 ddr_clk = dsi->phy.ddr_clk;
    u16 prediv = dsi->phy.prediv;
    u16 fbdiv = dsi->phy.fbdiv;
    // u32 Ttxclkesc = dsi->phy.Ttxclkesc;
    unsigned char test_data[2] = {0};

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

    //N=2,M=84
    test_data[0] = val << 1;
    dwc_phy_test_wr(dsi, code_hs_rx_lane0, test_data, 1);

    test_data[0] = prediv- 1;
    dwc_phy_test_wr(dsi, code_pll_input_div_rat, test_data, 1);
    
    test_data[0] = (fbdiv - 1) & 0x1f; //0x14; 
    dwc_phy_test_wr(dsi, code_pll_loop_div_rat, test_data, 1);
    
    test_data[0] = (fbdiv - 1) >> 5 | 0x80;  //0x82
    dwc_phy_test_wr(dsi, code_pll_loop_div_rat, test_data, 1);
    
    test_data[0] = 0x30;
    dwc_phy_test_wr(dsi, code_pll_input_loop_div_rat, test_data, 1);
    mdelay(100);

    test_data[0] = 0x00;
    // dwc_phy_test_wr(dsi, 0x60, test_data, 1);

    test_data[0] = 0x81;
    // dwc_phy_test_wr(dsi, 0x61, test_data, 1);

    test_data[0] = 0x0;
    // dwc_phy_test_wr(dsi, 0x62, test_data, 1);

    test_data[0] = 0x80 | 15;
    dwc_phy_test_wr(dsi, code_hstxdatalanerequsetstatetime, test_data, 1);

    test_data[0] = 0x80 | 85;
    dwc_phy_test_wr(dsi, code_hstxdatalanepreparestatetime, test_data, 1);

    test_data[0] = 0x40 | 10;
    dwc_phy_test_wr(dsi, code_hstxdatalanehszerostatetime, test_data, 1);


    // test_data[0] = 0x80 | 127;
    // dwc_phy_test_wr(dsi, 0x71, test_data, 1);

    // test_data[0] = 0x3;
    // dwc_phy_test_wr(dsi, 0x57, test_data, 1);

    return 0;
}

static int rk_mipi_dsi_phy_power_up(struct dsi *dsi)
{
#if defined(CONFIG_MFD_RK616) || defined(CONFIG_ARCH_RK319X)
	return inno_phy_power_up(dsi);
#else
	return rk32_phy_power_up(dsi);
#endif
}


static int rk_mipi_dsi_phy_power_down(struct dsi *dsi) 
{
#if defined(CONFIG_MFD_RK616) || defined(CONFIG_ARCH_RK319X)
	return inno_phy_power_down(dsi);
#else
	return rk32_phy_power_down(dsi);
#endif
	return 0;
}

static int rk_mipi_dsi_phy_init(struct dsi *dsi) 
{
#if defined(CONFIG_MFD_RK616) || defined(CONFIG_ARCH_RK319X)
	return inno_phy_init(dsi);
#else
	return rk32_phy_init(dsi);
#endif
	return 0;
}

static int rk_mipi_dsi_host_power_up(struct dsi *dsi) 
{
	int ret = 0;
	u32 val = 0;
	
	//disable all interrupt            
#ifdef DWC_DSI_VERSION_0x3131302A
	dsi_set_bits(dsi, 0x1fffff, ERROR_MSK0);
	dsi_set_bits(dsi, 0x1ffff, ERROR_MSK1);
#else
	dsi_set_bits(dsi, 0x1fffff, INT_MKS0);
	dsi_set_bits(dsi, 0x1ffff, INT_MKS1);
#endif

	rk_mipi_dsi_is_enable(dsi, 1);
	
	val = 10;
	while(!dsi_get_bits(dsi, phylock) && val--) {
		udelay(10);
	};
	
	if(val == 0) {
		ret = -1;
		MIPI_TRACE("%s:phylock fail\n", __func__);	
	}
	
	val = 10;
	while(!dsi_get_bits(dsi, phystopstateclklane) && val--) {
		udelay(10);
	};
	
	return ret;
}

static int rk_mipi_dsi_host_power_down(struct dsi *dsi) 
{	
	rk_mipi_dsi_enable_video_mode(dsi, 0);
	rk_mipi_dsi_enable_hs_clk(dsi, 0);
	rk_mipi_dsi_is_enable(dsi, 0);
	return 0;
}

static int rk_mipi_dsi_host_init(struct dsi *dsi) 
{
	u32 val = 0, bytes_px = 0;
	struct mipi_dsi_screen *screen = &dsi->screen;
	u32 decimals = dsi->phy.Ttxbyte_clk, temp = 0, i = 0;
	u32 m = 1, lane = dsi->host.lane, Tpclk = dsi->phy.Tpclk, 
			Ttxbyte_clk = dsi->phy.Ttxbyte_clk;
#ifdef CONFIG_MFD_RK616
	val = 0x04000000;
	dsi_write_reg(dsi, CRU_CRU_CLKSEL1_CON, &val);
#endif	
	dsi_set_bits(dsi, dsi->host.lane - 1, n_lanes);
	dsi_set_bits(dsi, dsi->vid, dpi_vcid);
	
	switch(screen->face) {
		case OUT_P888:
			dsi_set_bits(dsi, 5, dpi_color_coding);
			bytes_px = 3;
			break;
		case OUT_D888_P666:
		case OUT_P666:
			dsi_set_bits(dsi, 3, dpi_color_coding);
			dsi_set_bits(dsi, 1, en18_loosely);
			bytes_px = 3;
			break;
		case OUT_P565:
			dsi_set_bits(dsi, 0, dpi_color_coding);
			bytes_px = 2;
		default:
			break;
	}
	
	dsi_set_bits(dsi, 1, hsync_active_low);
	dsi_set_bits(dsi, 1, vsync_active_low);
	
	dsi_set_bits(dsi, 0, dataen_active_low);
	dsi_set_bits(dsi, 0, colorm_active_low);
	dsi_set_bits(dsi, 0, shutd_active_low);
	
	dsi_set_bits(dsi, dsi->host.video_mode, vid_mode_type);	  //burst mode
	switch(dsi->host.video_mode) {
		case VM_BM:
		    if(screen->type == SCREEN_DUAL_MIPI)
			    dsi_set_bits(dsi, screen->x_res / 2 + 4, vid_pkt_size);
			 else
			    dsi_set_bits(dsi, screen->x_res, vid_pkt_size);
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
#ifdef CONFIG_MFD_RK616
			dsi_set_bits(dsi, 1, en_multi_pkt);
#endif
			dsi_set_bits(dsi, screen->x_res / m + 1, num_chunks);
			dsi_set_bits(dsi, m, vid_pkt_size);
			temp = m * lane * Tpclk / Ttxbyte_clk - m * bytes_px;
			MIPI_DBG("%s:%d, %d\n", __func__, m, temp);
			if(temp >= 12) {
#ifdef CONFIG_MFD_RK616
				dsi_set_bits(dsi, 1, en_null_pkt);
#endif
				dsi_set_bits(dsi, temp - 12, null_pkt_size);
			}
			break;
		default:
			break;
	}	

	//dsi_set_bits(dsi, 0, CMD_MODE_CFG << 16);
	if(rk_mipi_get_dsi_num() ==1){
		dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->x_res + screen->left_margin + 
					screen->hsync_len + screen->right_margin) \
						/ dsi->phy.Ttxbyte_clk, vid_hline_time);
	}
	else{
		dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->x_res + 8 + screen->left_margin + 
					screen->hsync_len + screen->right_margin) \
						/ dsi->phy.Ttxbyte_clk, vid_hline_time);	
	}
		dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->left_margin) / dsi->phy.Ttxbyte_clk, 
					vid_hbp_time);
	dsi_set_bits(dsi, dsi->phy.Tpclk * (screen->hsync_len) / dsi->phy.Ttxbyte_clk, 
					vid_hsa_time);
    
	dsi_set_bits(dsi, screen->y_res , vid_active_lines);
	dsi_set_bits(dsi, screen->lower_margin, vid_vfp_lines);
	dsi_set_bits(dsi, screen->upper_margin, vid_vbp_lines);
	dsi_set_bits(dsi, screen->vsync_len, vid_vsa_lines);
	
	dsi->phy.txclkesc = 20 * MHz;
	val = dsi->phy.txbyte_clk / dsi->phy.txclkesc + 1;
	dsi->phy.txclkesc = dsi->phy.txbyte_clk / val;
	dsi_set_bits(dsi, val, TX_ESC_CLK_DIVISION);
	
	dsi_set_bits(dsi, 10, TO_CLK_DIVISION);
    dsi_set_bits(dsi, 1000, hstx_to_cnt); //no sure
	dsi_set_bits(dsi, 1000, lprx_to_cnt);	
	dsi_set_bits(dsi, 100, phy_stop_wait_time);

	//dsi_set_bits(dsi, 0, outvact_lpcmd_time);   //byte
	//dsi_set_bits(dsi, 0, invact_lpcmd_time);
		
	dsi_set_bits(dsi, 20, phy_hs2lp_time);
	dsi_set_bits(dsi, 16, phy_lp2hs_time);	
    
#if defined(CONFIG_ARCH_RK3288)	
   // dsi_set_bits(dsi, 87, phy_hs2lp_time_clk_lane); //no sure
  //  dsi_set_bits(dsi, 25, phy_hs2hs_time_clk_lane); //no sure
#endif	

	dsi_set_bits(dsi, 10000, max_rd_time);
#ifdef DWC_DSI_VERSION_0x3131302A
	dsi_set_bits(dsi, 1, dpicolom);
	dsi_set_bits(dsi, 1, dpishutdn);
#endif
#if 1
	dsi_set_bits(dsi, 1, lp_hfp_en);
	//dsi_set_bits(dsi, 1, lp_hbp_en); //no sure
	dsi_set_bits(dsi, 1, lp_vact_en);
	dsi_set_bits(dsi, 1, lp_vfp_en);
	dsi_set_bits(dsi, 1, lp_vbp_en);
	dsi_set_bits(dsi, 1, lp_vsa_en);
#endif	
	//dsi_set_bits(dsi, 1, frame_bta_ack_en);
	dsi_set_bits(dsi, 1, phy_enableclk);
	dsi_set_bits(dsi, 0, phy_tx_triggers);
	//dsi_set_bits(dsi, 1, phy_txexitulpslan);
	//dsi_set_bits(dsi, 1, phy_txexitulpsclk);
	return 0;
}

/*
	mipi protocol layer definition
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
	    
#ifdef CONFIG_MIPI_DSI_FT
	dsi->phy.pclk = screen->pixclock;
	dsi->phy.ref_clk = MIPI_DSI_MCLK;
#else
	
	dsi->phy.Tpclk = rk_fb_get_prmry_screen_pixclock();

	printk("dsi->phy.Tpclk=%d\n",dsi->phy.Tpclk);

#if defined(CONFIG_MFD_RK616)
	if(dsi_rk616->mclk)
		dsi->phy.ref_clk = clk_get_rate(dsi_rk616->mclk);
#elif defined(CONFIG_ARCH_RK319X)
	if(dsi->phy.refclk)
		dsi->phy.ref_clk = clk_get_rate(dsi->phy.refclk) / 2;  // 1/2 of input refclk
#endif   /* CONFIG_MFD_RK616 */
	//dsi->phy.ref_clk = 24 * MHz;
#endif   /* CONFIG_MIPI_DSI_FT */

    if(dsi->phy.refclk)
		dsi->phy.ref_clk = clk_get_rate(dsi->phy.refclk) ;

	dsi->phy.sys_clk = dsi->phy.ref_clk;

	printk(

"dsi->phy.sys_clk =%d\n",dsi->phy.sys_clk );

#ifndef CONFIG_ARCH_RK3288	
	if((screen->hs_tx_clk <= 80 * MHz) || (screen->hs_tx_clk >= 1000 * MHz))
		dsi->phy.ddr_clk = 1000 * MHz;    //default is 1HGz
	else
		dsi->phy.ddr_clk = screen->hs_tx_clk;	
#else
    if((screen->hs_tx_clk <= 90 * MHz) || (screen->hs_tx_clk >= 1500 * MHz))
        dsi->phy.ddr_clk = 1500 * MHz;    //default is 1.5HGz
    else
        dsi->phy.ddr_clk = screen->hs_tx_clk;   
#endif	


/*	if(n != 0) {
		dsi->phy.ddr_clk = n;
	}
    */

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

#ifdef CONFIG_MIPI_DSI_FT	
	dsi->phy.Tpclk = fre_to_period(dsi->phy.pclk);
	dsi->phy.Ttxclkesc = fre_to_period(dsi->phy.txclkesc);
	dsi->phy.Tsys_clk = fre_to_period(dsi->phy.sys_clk);
	dsi->phy.Tddr_clk = fre_to_period(dsi->phy.ddr_clk);
	dsi->phy.Ttxbyte_clk = fre_to_period(dsi->phy.txbyte_clk);	
#else
	dsi->phy.pclk = div_u64(1000000000000llu, dsi->phy.Tpclk);
	dsi->phy.Ttxclkesc = div_u64(1000000000000llu, dsi->phy.txclkesc);
	dsi->phy.Tsys_clk = div_u64(1000000000000llu, dsi->phy.sys_clk);
	dsi->phy.Tddr_clk = div_u64(1000000000000llu, dsi->phy.ddr_clk);
	dsi->phy.Ttxbyte_clk = div_u64(1000000000000llu, dsi->phy.txbyte_clk);	
#endif
	
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
	
	rk_mipi_dsi_phy_power_up(dsi);
	rk_mipi_dsi_host_power_up(dsi);
	rk_mipi_dsi_phy_init(dsi);
	rk_mipi_dsi_host_init(dsi);
		
	/*
		After the core reset, DPI waits for the first VSYNC active transition to start signal sampling, including
		pixel data, and preventing image transmission in the middle of a frame.
	*/
#if 0	
	dsi_set_bits(dsi, 0, shutdownz);
	rk_mipi_dsi_enable_video_mode(dsi, 1);
#ifdef CONFIG_MFD_RK616
	rk616_display_router_cfg(dsi_rk616, g_rk29fd_screen, 0);
#endif
	dsi_set_bits(dsi, 1, shutdownz);
#endif
	return 0;
}


int rk_mipi_dsi_init_lite(struct dsi *dsi)
{
	u32 decimals = 1000, i = 0, pre = 0, ref_clk = 0;
	struct mipi_dsi_screen *screen = &dsi->screen;
	
	if(!screen)
		return -1;
	
	if(rk_mipi_dsi_is_active(dsi) == 0)
		return -1;
#if defined(CONFIG_MFD_RK616)
	ref_clk = clk_get_rate(dsi_rk616->mclk);
#elif defined(CONFIG_ARCH_RK319X)
	ref_clk = clk_get_rate(dsi->phy.refclk);
#endif
	if(dsi->phy.ref_clk == ref_clk)
		return -1;
		
	dsi->phy.ref_clk = ref_clk;
	dsi->phy.sys_clk = dsi->phy.ref_clk;
	
	if((screen->hs_tx_clk <= 80 * MHz) || (screen->hs_tx_clk >= 1000 * MHz))
		dsi->phy.ddr_clk = 1000 * MHz;    //default is 1HGz
	else
		dsi->phy.ddr_clk = screen->hs_tx_clk;
		
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

	MIPI_DBG("prediv:%d, fbdiv:%d\n", dsi->phy.prediv, dsi->phy.fbdiv);
	dsi->phy.ddr_clk = dsi->phy.ref_clk / dsi->phy.prediv * dsi->phy.fbdiv;	
	dsi->phy.txbyte_clk = dsi->phy.ddr_clk / 8;
	
	dsi->phy.txclkesc = 20 * MHz;        // < 20MHz
	dsi->phy.txclkesc = dsi->phy.txbyte_clk / (dsi->phy.txbyte_clk / dsi->phy.txclkesc + 1);
	
	dsi->phy.pclk = div_u64(1000000000000llu, dsi->phy.Tpclk);
	dsi->phy.Ttxclkesc = div_u64(1000000000000llu, dsi->phy.txclkesc);
	dsi->phy.Tsys_clk = div_u64(1000000000000llu, dsi->phy.sys_clk);
	dsi->phy.Tddr_clk = div_u64(1000000000000llu, dsi->phy.ddr_clk);
	dsi->phy.Ttxbyte_clk = div_u64(1000000000000llu, dsi->phy.txbyte_clk);
	dsi->phy.UI = dsi->phy.Tddr_clk;
		
	MIPI_DBG("UI:%d\n", dsi->phy.UI);	
	MIPI_DBG("ref_clk:%d\n", dsi->phy.ref_clk);
	MIPI_DBG("pclk:%d, Tpclk:%d\n", dsi->phy.pclk, dsi->phy.Tpclk);
	MIPI_DBG("sys_clk:%d, Tsys_clk:%d\n", dsi->phy.sys_clk, dsi->phy.Tsys_clk);
	MIPI_DBG("ddr_clk:%d, Tddr_clk:%d\n", dsi->phy.ddr_clk, dsi->phy.Tddr_clk);
	MIPI_DBG("txbyte_clk:%d, Ttxbyte_clk:%d\n", dsi->phy.txbyte_clk, dsi->phy.Ttxbyte_clk);
	MIPI_DBG("txclkesc:%d, Ttxclkesc:%d\n", dsi->phy.txclkesc, dsi->phy.Ttxclkesc);
		
	rk_mipi_dsi_host_power_down(dsi);
	rk_mipi_dsi_phy_power_down(dsi);
	rk_mipi_dsi_phy_power_up(dsi);
	rk_mipi_dsi_phy_init(dsi);
	//rk_mipi_dsi_host_power_up(dsi);
	//rk_mipi_dsi_host_init(dsi);
	//dsi_set_bits(dsi, 0, shutdownz);
	rk_mipi_dsi_enable_hs_clk(dsi, 1);
	rk_mipi_dsi_enable_video_mode(dsi, 1);
	rk_mipi_dsi_is_enable(dsi, 1);
	return 0;
}

static int rk_mipi_dsi_is_enable(void *arg, u32 enable)
{
	struct dsi *dsi = arg;

	dsi_set_bits(dsi, enable, shutdownz);

	return 0;
}

static int rk_mipi_dsi_enable_video_mode(void *arg, u32 enable)
{
	struct dsi *dsi = arg;
#ifdef DWC_DSI_VERSION_0x3131302A
	dsi_set_bits(dsi, enable, en_video_mode);
#else
	dsi_set_bits(dsi, !enable, cmd_video_mode);
#endif

	return 0;
}

static int rk_mipi_dsi_enable_command_mode(void *arg, u32 enable)
{
	struct dsi *dsi = arg;
#ifdef DWC_DSI_VERSION_0x3131302A
	dsi_set_bits(dsi, enable, en_cmd_mode);
#else
	dsi_set_bits(dsi, enable, cmd_video_mode);
#endif
	return 0;
}

static int rk_mipi_dsi_enable_hs_clk(void *arg, u32 enable)
{
	struct dsi *dsi = arg;
	dsi_set_bits(dsi, enable, phy_txrequestclkhs);
	return 0;
}

static int rk_mipi_dsi_is_active(void *arg)
{
	struct dsi *dsi = arg;
	return dsi_get_bits(dsi, shutdownz);
}

static int rk_mipi_dsi_send_packet(struct dsi *dsi, u32 type, unsigned char regs[], u32 n)
{
	u32 data = 0, i = 0, j = 0;
#ifdef DWC_DSI_VERSION_0x3131302A	
	u32 flag = 0;
#endif	
	if((n == 0) && (type != DTYPE_GEN_SWRITE_0P))
		return -1;
#ifndef CONFIG_MFD_RK616
	if(dsi_get_bits(dsi, gen_cmd_full) == 1) {
		MIPI_TRACE("gen_cmd_full\n");
		return -1;
	}
#endif	

#ifdef DWC_DSI_VERSION_0x3131302A
	if(dsi_get_bits(dsi, en_video_mode) == 1) {
		//rk_mipi_dsi_enable_video_mode(dsi, 0);
		flag = 1;
	}
#endif
	//rk_mipi_dsi_enable_command_mode(dsi, 1);
	udelay(10);

	 if(n <= 2) {
	    if(type ==  0x29)
	    {
            printk("type=0x%x\n", type);
            data = 0;
        	for(i = 0; i < n; i++) {
        		j = i % 4;
        		data |= regs[i] << (j * 8);
        		if(j == 3 || ((i + 1) == n)) {
        			#ifndef CONFIG_MFD_RK616
        			if(dsi_get_bits(dsi, gen_pld_w_full) == 1) {
        				MIPI_TRACE("gen_pld_w_full :%d\n", i);
        				break;
        			}
        			#endif
        			dsi_set_bits(dsi, data, GEN_PLD_DATA);
        			MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
        			data = 0;
        		}
        	}
        	data = (dsi->vid << 6) | type;		
        	data |= (n & 0xffff) << 8;   
	    }
		else 
		{
		    if(type == DTYPE_GEN_SWRITE_0P)
			    data = (dsi->vid << 6) | (n << 4) | type;
		    else 
			    data = (dsi->vid << 6) | ((n-1) << 4) | type;
			    
        	data |= regs[0] << 8;
        	if(n == 2)
        		data |= regs[1] << 16;
        }
	} else {
		data = 0;
		for(i = 0; i < n; i++) {
			j = i % 4;
			data |= regs[i] << (j * 8);
			if(j == 3 || ((i + 1) == n)) {
				#ifndef CONFIG_MFD_RK616
				if(dsi_get_bits(dsi, gen_pld_w_full) == 1) {
					MIPI_TRACE("gen_pld_w_full :%d\n", i);
					break;
				}
				#endif
				dsi_set_bits(dsi, data, GEN_PLD_DATA);
				MIPI_DBG("write GEN_PLD_DATA:%d, %08x\n", i, data);
				data = 0;
			}
		}
		data = (dsi->vid << 6) | type;		
		data |= (n & 0xffff) << 8;
	}
	
	MIPI_DBG("write GEN_HDR:%08x\n", data);
	dsi_set_bits(dsi, data, GEN_HDR);
#ifndef CONFIG_MFD_RK616
	i = 10;
	while(!dsi_get_bits(dsi, gen_cmd_empty) && i--) {
		MIPI_DBG(".");
		udelay(10);
	}
	udelay(10);
#endif

#ifdef DWC_DSI_VERSION_0x3131302A
	//rk_mipi_dsi_enable_command_mode(dsi, 0);
	if(flag == 1) {
	//	rk_mipi_dsi_enable_video_mode(dsi, 1);
	}
#endif
	return 0;
}

static int rk_mipi_dsi_send_dcs_packet(void *arg, unsigned char regs[], u32 n)
{
	struct dsi *dsi = arg;
	n -= 1;
	if((regs[1] ==0x2c) || (regs[1] ==0x3c))
	{
	    dsi_set_bits(dsi, regs[0], dcs_sw_0p_tx);
		rk_mipi_dsi_send_packet(dsi, DTYPE_DCS_LWRITE, regs + 1, n);
	}else
	if(n <= 2) {
		if(n == 1)
			dsi_set_bits(dsi, regs[0], dcs_sw_0p_tx);
		else
			dsi_set_bits(dsi, regs[0], dcs_sw_1p_tx);
		rk_mipi_dsi_send_packet(dsi, DTYPE_DCS_SWRITE_0P, regs + 1, n);
	} else {
		dsi_set_bits(dsi, regs[0], dcs_lw_tx);
		rk_mipi_dsi_send_packet(dsi, DTYPE_DCS_LWRITE, regs + 1, n);
	}
	MIPI_DBG("***%s:%d command sent in %s size:%d\n", __func__, __LINE__, regs[0] ? "LP mode" : "HS mode", n);
	return 0;
}

static int rk_mipi_dsi_send_gen_packet(void *arg, void *data, u32 n)
{
	struct dsi *dsi = arg;
	unsigned char *regs = data;
	n -= 1;
	if(regs[1] == 0xb3)
	{
	    dsi_set_bits(dsi, regs[0], gen_sw_1p_tx);
		rk_mipi_dsi_send_packet(dsi, DTYPE_GEN_LWRITE, regs + 1, n);
	}
	else{ 
	if(n <= 2) {
		if(n == 2)
			dsi_set_bits(dsi, regs[0], gen_sw_2p_tx);
		else if(n == 1)
			dsi_set_bits(dsi, regs[0], gen_sw_1p_tx);
		else 
			dsi_set_bits(dsi, regs[0], gen_sw_0p_tx);	
		rk_mipi_dsi_send_packet(dsi, DTYPE_GEN_SWRITE_0P, regs + 1, n);
	} else {
		dsi_set_bits(dsi, regs[0], gen_lw_tx);
		rk_mipi_dsi_send_packet(dsi, DTYPE_GEN_LWRITE, regs + 1, n);
	}
	}
	MIPI_DBG("***%s:%d command sent in %s size:%d\n", __func__, __LINE__, regs[0] ? "LP mode" : "HS mode", n);
	return 0;
}

static int rk_mipi_dsi_read_dcs_packet(void *arg, unsigned char *data1, u32 n)
{
    struct dsi *dsi = arg;
	//DCS READ 
	//unsigned char *regs = data;
	unsigned char regs[2];
	regs[0] = LPDT;
	regs[1] = 0x0a;
	 n = n - 1;
	u32 data = 0;
	
	dsi_set_bits(dsi, regs[0], dcs_sr_0p_tx);
	int type = 0x06;

   /* if(type == DTYPE_GEN_SWRITE_0P)
        data = (dsi->vid << 6) | (n << 4) | type;
    else 
        data = (dsi->vid << 6) | ((n-1) << 4) | type;*/
        
    data |= regs[1] << 8 | type;
   // if(n == 2)
    //    data |= regs[1] << 16;

    MIPI_DBG("write GEN_HDR:%08x\n", data);
	dsi_set_bits(dsi, data, GEN_HDR);
    msleep(100);
    
   // dsi_set_bits(dsi, regs[0], gen_sr_0p_tx);

    printk("rk_mipi_dsi_read_dcs_packet==0x%x\n",dsi_get_bits(dsi, GEN_PLD_DATA));
    msleep(100);

  //  dsi_set_bits(dsi, regs[0], max_rd_pkt_size);
    
    msleep(100);
    // printk("_____rk_mipi_dsi_read_dcs_packet==0x%x\n",dsi_get_bits(dsi, GEN_PLD_DATA));
	
    msleep(100);
	return 0;
}

static int rk_mipi_dsi_power_up(void *arg)
{
	struct dsi *dsi = arg;
	rk_mipi_dsi_phy_power_up(dsi);
	rk_mipi_dsi_host_power_up(dsi);
	return 0;
}

static int rk_mipi_dsi_power_down(void *arg)
{
    u8 dcs[4] = {0};
	struct dsi *dsi = arg;
	struct mipi_dsi_screen *screen = &dsi->screen;
	
	if(!screen)
		return -1;
	
	if(!screen->standby) {
		rk_mipi_dsi_enable_video_mode(dsi, 0);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_off; 
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_enter_sleep_mode; 
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
	} else {
		screen->standby(1);
	}	
		
	rk_mipi_dsi_host_power_down(dsi);
	rk_mipi_dsi_phy_power_down(dsi);
#if defined(CONFIG_ARCH_RK319X)
	clk_disable(dsi->dsi_pd);
	clk_disable(dsi->dsi_pclk);
#endif
	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
	return 0;
}

static int rk_mipi_dsi_get_id(void *arg)
{
	u32 id = 0;
	struct dsi *dsi = arg;
	id = dsi_get_bits(dsi, VERSION);
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
				dsi_write_reg(dsi0, regs_val >> 32, &read_val);
				dsi_read_reg(dsi0, regs_val >> 32, &read_val);
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
				dsi_read_reg(dsi0, (u16)regs_val, &read_val);
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
#ifdef CONFIG_MFD_RK616
				clk_set_rate(dsi_rk616->mclk, read_val);	
#endif
				//rk_mipi_dsi_init_lite(dsi);
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
					sscanf(data, "%x,", str + i);   //-c 1,29,02,03,05,06,> pro
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
						rk_mipi_dsi_send_dcs_packet(dsi0, str, read_val);
					else
						rk_mipi_dsi_send_gen_packet(dsi0, str, read_val);
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
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	/*for(i = DPHY_REGISTER0; i <= DPHY_REGISTER4; i += 4<<16) {
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	MIPI_TRACE("\n");
	i = DPHY_REGISTER20;
	val = dsi_get_bits(dsi0, i);
	MIPI_TRACE("%04x: %08x\n", i>>16, val);
	msleep(1);

	MIPI_TRACE("\n");
	for(i = (DPHY_CLOCK_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_CLOCK_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = (DPHY_LANE0_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE0_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	for(i = (DPHY_LANE1_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE1_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	for(i = (DPHY_LANE2_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE2_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = (DPHY_LANE3_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE3_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi0, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}*/
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
				dsi_write_reg(dsi1, regs_val >> 32, &read_val);
				dsi_read_reg(dsi1, regs_val >> 32, &read_val);
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
				dsi_read_reg(dsi1, (u16)regs_val, &read_val);
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
#ifdef CONFIG_MFD_RK616
				clk_set_rate(dsi_rk616->mclk, read_val);	
#endif
				//rk_mipi_dsi_init_lite(dsi);
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
					sscanf(data, "%x,", str + i);   //-c 1,29,02,03,05,06,> pro
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
						rk_mipi_dsi_send_dcs_packet(dsi1, str, read_val);
					else
						rk_mipi_dsi_send_gen_packet(dsi1, str, read_val);
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
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
/*	for(i = DPHY_REGISTER0; i <= DPHY_REGISTER4; i += 4<<16) {
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	MIPI_TRACE("\n");
	i = DPHY_REGISTER20;
	val = dsi_get_bits(dsi1, i);
	MIPI_TRACE("%04x: %08x\n", i>>16, val);
	msleep(1);

	MIPI_TRACE("\n");
	for(i = (DPHY_CLOCK_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_CLOCK_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = (DPHY_LANE0_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE0_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	for(i = (DPHY_LANE1_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE1_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}

	MIPI_TRACE("\n");
	for(i = (DPHY_LANE2_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE2_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}
	
	MIPI_TRACE("\n");
	for(i = (DPHY_LANE3_OFFSET + DSI_DPHY_BITS(0x0000, 32, 0)); i <= ((DPHY_LANE3_OFFSET + DSI_DPHY_BITS(0x0048, 32, 0))); i += 4<<16) {
		val = dsi_get_bits(dsi1, i);
		MIPI_TRACE("%04x: %08x\n", i>>16, val);
		msleep(1);
	}*/
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

#if 0
static int reg_proc_init(char *name)
{
	int ret = 0;
#if 0	
#ifdef CONFIG_MFD_RK616
	//debugfs_create_file("mipi", S_IRUSR, dsi_rk616->debugfs_dir, dsi_rk616, 
							&reg_proc_fops);
#endif	
#else
	static struct proc_dir_entry *reg_proc_entry;
  	reg_proc_entry = create_proc_entry(name, 0666, NULL);
	/*if(reg_proc_entry == NULL) {
		//MIPI_TRACE("Couldn't create proc entry : %s!\n", name);
		ret = -ENOMEM;
		return ret;
	}
	else {
		MIPI_TRACE("Create proc entry:%s success!\n", name);
		reg_proc_entry->proc_fops = &reg_proc_fops;
	}*/
#endif	
	return ret;
}

static int __init rk_mipi_dsi_reg(void)
{
	return 0;//reg_proc_init("mipi_dsi");
}
module_init(rk_mipi_dsi_reg);
#endif
#endif


#ifdef CONFIG_MIPI_DSI_FT
static struct mipi_dsi_screen ft_screen;

static u32 fre_to_period(u32 fre)
{
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

static int rk616_mipi_dsi_set_screen_info(void)
{
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

int rk616_mipi_dsi_ft_init(void) 
{
	rk616_mipi_dsi_set_screen_info();
	rk_mipi_dsi_init(g_screen, 0);
	return 0;
}
#endif  /* end of CONFIG_MIPI_DSI_FT */

#ifdef CONFIG_MIPI_DSI_LINUX

#ifdef CONFIG_HAS_EARLYSUSPEND
void  rk616_mipi_dsi_suspend(void)
{
	u8 dcs[4] = {0};
	
	if(!g_screen->standby) {
		rk_mipi_dsi_enable_video_mode(dsi, 0);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_off; 
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_enter_sleep_mode; 
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
	} else {
		g_screen->standby(1);
	}	
		
	rk_mipi_dsi_host_power_down(dsi);
	rk_mipi_dsi_phy_power_down(dsi);
#if defined(CONFIG_ARCH_RK319X)
	clk_disable(dsi->dsi_pd);
	clk_disable(dsi->dsi_pclk);
#endif
	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
}

void rk616_mipi_dsi_resume(void)
{
	u8 dcs[4] = {0};
#if defined(CONFIG_ARCH_RK319X)
	clk_enable(dsi->dsi_pd);
	clk_enable(dsi->dsi_pclk);
#endif
	rk_mipi_dsi_phy_power_up(dsi);
	rk_mipi_dsi_host_power_up(dsi);

#ifdef CONFIG_MFD_RK616
	rk_mipi_recover_reg();
#else
	rk_mipi_dsi_phy_init(dsi);
	rk_mipi_dsi_host_init(dsi);
#endif

/*	if(!g_screen->standby) {
		rk_mipi_dsi_enable_hs_clk(dsi, 1);
		dcs[0] = HSDT;
		dcs[1] = dcs_exit_sleep_mode;
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_on;
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		//msleep(10);
	} else {
		g_screen->standby(0);
	}*/
	
	rk_mipi_dsi_is_enable(dsi, 0);
	rk_mipi_dsi_enable_video_mode(dsi, 1);
	
#ifdef CONFIG_MFD_RK616	
	dsi_rk616->resume = 1;
	rk616_display_router_cfg(dsi_rk616, g_rk29fd_screen, 0);
	dsi_rk616->resume = 0;
#endif	
	rk_mipi_dsi_is_enable(dsi, 1, shutdownz);
	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk616_mipi_dsi_early_suspend(struct early_suspend *h)
{
    rk616_mipi_dsi_suspend();
}

static void rk616_mipi_dsi_late_resume(struct early_suspend *h)
{
    rk616_mipi_dsi_resume();
}
#endif  /* end of CONFIG_HAS_EARLYSUSPEND */
#endif


#ifdef CONFIG_MFD_RK616
static int rk616_mipi_dsi_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr) {
   
#ifdef CONFIG_RK616_MIPI_DSI_RST
   if(event == 1)
    {
        g_screen->standby(0);
        mdelay(5);
        rk616_mipi_dsi_suspend();
        mdelay(10);
    }
    else if(event == 2)
    {
        rk_mipi_dsi_init_lite(dsi);
        mdelay(5);
        g_screen->standby(1);
        mdelay(5);
        rk616_mipi_dsi_resume();
    }
#else
      	rk_mipi_dsi_init_lite(dsi);
#endif
	return 0;
}		

struct notifier_block mipi_dsi_nb= {
	.notifier_call = rk616_mipi_dsi_notifier_event,
};
#endif

#ifndef CONFIG_MFD_RK616
static irqreturn_t rk616_mipi_dsi_irq_handler(int irq, void *data)
{
	return IRQ_HANDLED;
	//return IRQ_NONE;
}
#endif

static int rk32_dsi_enable(void)
{   
    MIPI_DBG("rk32_dsi_enable-------\n");
    
    dsi_init(0, NULL, 0);
    if (rk_mipi_get_dsi_num() ==2)
        dsi_init(1, NULL, 0);
		
    rk_mipi_screen_standby(0);    

	/*
		After the core reset, DPI waits for the first VSYNC active transition to start signal sampling, including
		pixel data, and preventing image transmission in the middle of a frame.
	*/
    dsi_is_enable(0, 0);
    if (rk_mipi_get_dsi_num() ==2)
        dsi_is_enable(1, 0);     

    dsi_enable_video_mode(0, 1);
    dsi_enable_video_mode(1, 1);

#ifdef CONFIG_MFD_RK616
        rk616_display_router_cfg(dsi_rk616, g_rk29fd_screen, 0);
#endif

    dsi_is_enable(0, 1);
    if (rk_mipi_get_dsi_num() ==2)
        dsi_is_enable(1, 1);

    return 0;
}

static int rk32_dsi_disable(void)
{
    MIPI_DBG("rk32_dsi_disable-------\n");
    
	rk_mipi_screen_standby(1); 
    dsi_power_off(0);
    if (rk_mipi_get_dsi_num() ==2)
        dsi_power_off(1);
    
    return 0;
}


static struct rk_fb_trsm_ops trsm_dsi_ops = 
{
    .enable = rk32_dsi_enable,
    .disable = rk32_dsi_disable,
};

static void rk32_init_phy_mode(int lcdc_id)
{ 
    int val0 = 0, val1 = 0;

    MIPI_DBG("rk32_init_phy_mode----------lcdc_id=%d\n",lcdc_id);
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

static int rk616_mipi_dsi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct dsi *dsi;
	struct mipi_dsi_ops *ops;
	struct rk_screen *screen;
	struct mipi_dsi_screen *dsi_screen;
	static int id = 0;
	
#if defined(CONFIG_ARCH_RK319X) || defined(CONFIG_ARCH_RK3288)
	struct resource *res_host, *res_phy, *res_irq;
#endif
#if defined(CONFIG_MFD_RK616)
	struct mfd_rk616 *rk616;
#endif
	dsi = devm_kzalloc(&pdev->dev, sizeof(struct dsi), GFP_KERNEL);
	if(!dsi) {
		dev_err(&pdev->dev,"request struct dsi fail!\n");
		return -ENOMEM;
	}

#if defined(CONFIG_MFD_RK616)
	rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616) {
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		ret = -ENODEV;
		goto probe_err1;
	} else {
		dsi_rk616 = rk616;
	}
	clk_notifier_register(rk616->mclk, &mipi_dsi_nb);
#elif defined(CONFIG_ARCH_RK319X)
	res_host = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mipi_dsi_host");
	if (!res_host) {
		dev_err(&pdev->dev, "get resource mipi_dsi_host fail\n");
		ret = -EINVAL;
		goto probe_err1;
	}
	if (!request_mem_region(res_host->start, resource_size(res_host), pdev->name)) {
		dev_err(&pdev->dev, "host memory region already claimed\n");
		ret = -EBUSY;
		goto probe_err1;
	}
	dsi->host.iobase = res_host->start;
	dsi->host.membase = ioremap_nocache(res_host->start, resource_size(res_host));
	if (!dsi->host.membase) {
		dev_err(&pdev->dev, "ioremap mipi_dsi_host fail\n");
		ret = -ENXIO;
		goto probe_err2;
	}

	res_phy = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mipi_dsi_phy");
	if (!res_phy) {
		dev_err(&pdev->dev, "get resource mipi_dsi_phy fail\n");
		ret = -EINVAL;
		goto probe_err3;
	}
	if (!request_mem_region(res_phy->start, resource_size(res_phy), pdev->name)) {
		dev_err(&pdev->dev, "phy memory region already claimed\n");
		ret = -EBUSY;
		goto probe_err3;
	}
	dsi->phy.iobase = res_phy->start;
	dsi->phy.membase = ioremap_nocache(res_phy->start, resource_size(res_phy));
	if (!dsi->phy.membase) {
		dev_err(&pdev->dev, "ioremap mipi_dsi_phy fail\n");
		ret = -ENXIO;
		goto probe_err4;
	}

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq) {
		dev_err(&pdev->dev, "get resource mipi_dsi irq fail\n");
		ret = -EINVAL;
		goto probe_err5;
	}
	dsi->host.irq = res_irq->start;
	ret = request_irq(dsi->host.irq, rk616_mipi_dsi_irq_handler, 0,
					dev_name(&pdev->dev), dsi);
	if(ret) {
		dev_err(&pdev->dev, "request mipi_dsi irq fail\n");
		ret = -EINVAL;
		goto probe_err5;
	}
	disable_irq(dsi->host.irq);
	
	dsi->phy.refclk = clk_get(NULL, "mipi_ref");
	if (unlikely(IS_ERR(dsi->phy.refclk))) {
		dev_err(&pdev->dev, "get mipi_ref clock fail\n");
		ret = PTR_ERR(dsi->phy.refclk);
		goto probe_err6;
	}
	dsi->dsi_pclk = clk_get(NULL, "pclk_mipi_dsi");
	if (unlikely(IS_ERR(dsi->dsi_pclk))) {
		dev_err(&pdev->dev, "get pclk_mipi_dsi clock fail\n");
		ret = PTR_ERR(dsi->dsi_pclk);
		goto probe_err7;
	}
	dsi->dsi_pd = clk_get(NULL, "pd_mipi_dsi");
	if (unlikely(IS_ERR(dsi->dsi_pd))) {
		dev_err(&pdev->dev, "get pd_mipi_dsi clock fail\n");
		ret = PTR_ERR(dsi->dsi_pd);
		goto probe_err8;
	}

	clk_enable(dsi->dsi_pd);
	clk_enable(dsi->dsi_pclk);
	clk_enable(clk_get(NULL, "pclk_mipiphy_dsi"));

#elif defined(CONFIG_ARCH_RK3288)

	res_host = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->host.membase = devm_request_and_ioremap(&pdev->dev, res_host);
	if (!dsi->host.membase)
		return -ENOMEM;

    dsi->phy.refclk  = devm_clk_get(&pdev->dev, "clk_mipi_24m"); 
	if (unlikely(IS_ERR(dsi->phy.refclk))) {
		dev_err(&pdev->dev, "get mipi_ref clock fail\n");
		ret = PTR_ERR(dsi->phy.refclk);
		//goto probe_err6;
	}

   dsi->dsi_pclk = devm_clk_get(&pdev->dev, "pclk_mipi_dsi");
   if (unlikely(IS_ERR(dsi->dsi_pclk))) {
       dev_err(&pdev->dev, "get pclk_mipi_dsi clock fail\n");
       ret = PTR_ERR(dsi->dsi_pclk);
       //goto probe_err7;
   }

    

   // printk("dsi->phy.refclk =%x\n",dsi->phy.refclk);
    
    //clk_prepare_enable(dsi->phy.refclk);

    //clk_disable_unprepare(dsi->phy.refclk);

		
	dsi->host.irq = platform_get_irq(pdev, 0);
	if (dsi->host.irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return dsi->host.irq;
	}
	
	ret = request_irq(dsi->host.irq, rk616_mipi_dsi_irq_handler, 0,dev_name(&pdev->dev), dsi);
	if(ret) {
		dev_err(&pdev->dev, "request mipi_dsi irq fail\n");
		ret = -EINVAL;
		goto probe_err1;
	}
    printk("dsi->host.irq =%d\n",dsi->host.irq); 

    disable_irq(dsi->host.irq);

#endif  /* CONFIG_MFD_RK616 */

	screen = devm_kzalloc(&pdev->dev, sizeof(struct rk_screen), GFP_KERNEL);
	if(!screen) {
		dev_err(&pdev->dev,"request struct rk_screen fail!\n");
		goto probe_err9;
	}
	rk_fb_get_prmry_screen(screen);

#ifdef CONFIG_MFD_RK616
	g_rk29fd_screen = screen;
#endif

	dsi->pdev = pdev;
	ops = &dsi->ops;
	ops->dsi = dsi;
	ops->id = DWC_DSI_VERSION,
	ops->get_id = rk_mipi_dsi_get_id,
	ops->dsi_send_packet = rk_mipi_dsi_send_gen_packet,
	ops->dsi_send_dcs_packet = rk_mipi_dsi_send_dcs_packet,
	ops->dsi_read_dcs_packet = rk_mipi_dsi_read_dcs_packet,
	ops->dsi_enable_video_mode = rk_mipi_dsi_enable_video_mode,
	ops->dsi_enable_command_mode = rk_mipi_dsi_enable_command_mode,
	ops->dsi_enable_hs_clk = rk_mipi_dsi_enable_hs_clk,
	ops->dsi_is_active = rk_mipi_dsi_is_active,
	ops->dsi_is_enable= rk_mipi_dsi_is_enable,
	ops->power_up = rk_mipi_dsi_power_up,
	ops->power_down = rk_mipi_dsi_power_down,
	ops->dsi_init = rk_mipi_dsi_init,

	dsi_screen = &dsi->screen;
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
	dsi_screen->lcdc_id = 1;
    
	dsi->dsi_id = id++;//of_alias_get_id(pdev->dev.of_node, "dsi");

	sprintf(ops->name, "rk_mipi_dsi.%d", dsi->dsi_id);
	platform_set_drvdata(pdev, dsi);

#ifdef CONFIG_MFD_RK616
	host_mem = kzalloc(MIPI_DSI_HOST_SIZE, GFP_KERNEL);
	if(!host_mem) {
		dev_err(&pdev->dev,"request host_mem fail!\n");
		ret = -ENOMEM;
		goto probe_err10;
	}
	phy_mem = kzalloc(MIPI_DSI_PHY_SIZE, GFP_KERNEL);
	if(!phy_mem) {
		kfree(host_mem);
		dev_err(&pdev->dev,"request phy_mem fail!\n");
		ret = -ENOMEM;
		goto probe_err10;
	}
	
	memset(host_mem, 0xaa, MIPI_DSI_HOST_SIZE);	
	memset(phy_mem, 0xaa, MIPI_DSI_PHY_SIZE);
#endif

	ret = rk_mipi_dsi_probe(dsi);
	if(ret) {
		dev_err(&pdev->dev,"rk mipi_dsi probe fail!\n");
		dev_err(&pdev->dev,"%s\n", RK_MIPI_DSI_VERSION_AND_TIME);
		goto probe_err11;
	}	
#ifdef CONFIG_HAS_EARLYSUSPEND
	dsi->early_suspend.suspend = rk616_mipi_dsi_early_suspend;
	dsi->early_suspend.resume = rk616_mipi_dsi_late_resume;
	dsi->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	register_early_suspend(&dsi->early_suspend);
#endif
    
    if(id == 1){
        rk32_init_phy_mode(dsi_screen->lcdc_id);
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
    
	dev_info(&pdev->dev,"rk mipi_dsi probe success!\n");
	dev_info(&pdev->dev,"%s\n", RK_MIPI_DSI_VERSION_AND_TIME);
	return 0;

probe_err11:
#ifdef CONFIG_MFD_RK616
	kfree(host_mem);
	kfree(phy_mem);
probe_err10:
#endif

probe_err9:
#if defined(CONFIG_ARCH_RK319X)
	clk_put(dsi->dsi_pd);
probe_err8:
	clk_put(dsi->dsi_pclk);
probe_err7:
	clk_put(dsi->phy.refclk);
probe_err6:
	free_irq(dsi->host.irq, dsi);
probe_err5:
	iounmap(dsi->phy.membase);
probe_err4:
	release_mem_region(res_phy->start, resource_size(res_phy));
probe_err3:
	iounmap(dsi->host.membase);
probe_err2:
	release_mem_region(res_host->start, resource_size(res_host));
#endif

probe_err1:

	return ret;
	
}

static int rk616_mipi_dsi_remove(struct platform_device *pdev)
{
	//struct dsi *dsi = platform_get_drvdata(pdev);
#ifdef CONFIG_MFD_RK616
	clk_notifier_unregister(dsi_rk616->mclk, &mipi_dsi_nb);
#endif
	return 0;
}

static void rk616_mipi_dsi_shutdown(struct platform_device *pdev)
{
	u8 dcs[4] = {0};
	struct dsi *dsi = platform_get_drvdata(pdev);	

	if(!dsi->screen.standby) {
		rk_mipi_dsi_enable_video_mode(dsi, 0);
		dcs[0] = HSDT;
		dcs[1] = dcs_set_display_off; 
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
		dcs[0] = HSDT;
		dcs[1] = dcs_enter_sleep_mode; 
		rk_mipi_dsi_send_dcs_packet(dsi, dcs, 2);
		msleep(1);
	} else {
		dsi->screen.standby(1);
	}

	rk_mipi_dsi_host_power_down(dsi);
	rk_mipi_dsi_phy_power_down(dsi);

	MIPI_TRACE("%s:%d\n", __func__, __LINE__);
	return;
}

#ifdef CONFIG_OF
static const struct of_device_id of_rk_mipi_dsi_match[] = {
	{ .compatible = "rockchip,rk32-dsi" }, 
	{ /* Sentinel */ } 
}; 
#endif

static struct platform_driver rk616_mipi_dsi_driver = {
	.driver		= {
		.name	= "rk616-mipi",
#ifdef CONFIG_OF
		.of_match_table	= of_rk_mipi_dsi_match,
#endif
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
