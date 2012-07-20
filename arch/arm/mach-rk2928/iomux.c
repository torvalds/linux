/*
 * arch/arm/mach-rk2928/iomux.c
 *
 *Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/spinlock.h>

#include <mach/io.h>  
#include <mach/iomux.h>

//#define IOMUX_DBG

static struct mux_config rk30_muxs[] = {
/*
 *	 description				mux  mode   mux	  mux  
 *						reg  offset inter mode
 */ 
//gpio0a
MUX_CFG(GPIO0A0_I2C0_SCL_NAME,                  GPIO0A,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO0A1_I2C0_SDA_NAME,                  GPIO0A,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO0A2_I2C1_SCL_NAME,                  GPIO0A,    4,    1,    0,    DEFAULT)
MUX_CFG(GPIO0A3_I2C1_SDA_NAME,                  GPIO0A,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO0A6_I2C3_SCL_HDMI_DDCSCL_NAME,      GPIO0A,    12,    2,    0,    DEFAULT)
MUX_CFG(GPIO0A7_I2C3_SDA_HDMI_DDCSDA_NAME,      GPIO0A,    14,    2,    0,    DEFAULT)

//gpio0b
MUX_CFG(GPIO0B0_MMC1_CMD_NAME,                  GPIO0B,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B1_MMC1_CLKOUT_NAME,               GPIO0B,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B2_MMC1_DETN_NAME,                 GPIO0B,    4,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B3_MMC1_D0_NAME,                   GPIO0B,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B4_MMC1_D1_NAME,                   GPIO0B,    8,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B5_MMC1_D2_NAME,                   GPIO0B,    10,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B6_MMC1_D3_NAME,                   GPIO0B,    12,    1,    0,    DEFAULT)
MUX_CFG(GPIO0B7_HDMI_HOTPLUGIN_NAME,            GPIO0B,    14,    1,    0,    DEFAULT)

//gpio0c
MUX_CFG(GPIO0C0_UART0_SOUT_NAME,                GPIO0C,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO0C1_UART0_SIN_NAME,                 GPIO0C,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO0C2_UART0_RTSN_NAME,                GPIO0C,    4,    1,    0,    DEFAULT)
MUX_CFG(GPIO0C3_UART0_CTSN_NAME,                GPIO0C,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO0C4_HDMI_CECSDA_NAME,               GPIO0C,    8,    1,    0,    DEFAULT)
MUX_CFG(GPIO0C7_NAND_CS1_NAME,                  GPIO0C,    14,    1,    0,    DEFAULT)

//gpio0d
MUX_CFG(GPIO0D0_UART2_RTSN_NAME,                GPIO0D,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D1_UART2_CTSN_NAME,                GPIO0D,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D2_PWM_0_NAME,                     GPIO0D,    4,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D3_PWM_1_NAME,                     GPIO0D,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D4_PWM_2_NAME,                     GPIO0D,    8,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D5_MMC1_WRPRT_NAME,                GPIO0D,    10,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D6_MMC1_PWREN_NAME,                GPIO0D,    12,    1,    0,    DEFAULT)
MUX_CFG(GPIO0D7_MMC1_BKEPWR_NAME,               GPIO0D,    14,    1,    0,    DEFAULT)

//gpio1a
MUX_CFG(GPIO1A0_I2S_MCLK_NAME,                  GPIO1A,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO1A1_I2S_SCLK_NAME,                  GPIO1A,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO1A2_I2S_LRCKRX_GPS_CLK_NAME,        GPIO1A,    4,    2,    0,    DEFAULT)
MUX_CFG(GPIO1A3_I2S_LRCKTX_NAME,                GPIO1A,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO1A4_I2S_SDO_GPS_MAG_NAME,           GPIO1A,    8,    2,    0,    DEFAULT)
MUX_CFG(GPIO1A5_I2S_SDI_GPS_SIGN_NAME,          GPIO1A,    10,    2,    0,    DEFAULT)
MUX_CFG(GPIO1A6_MMC1_INTN_NAME,                 GPIO1A,    12,    1,    0,    DEFAULT)
MUX_CFG(GPIO1A7_MMC0_WRPRT_NAME,                GPIO1A,    14,    1,    0,    DEFAULT)

//gpio1b
MUX_CFG(GPIO1B0_SPI_CLK_UART1_CTSN_NAME,        GPIO1B,    0,    2,    0,    DEFAULT)
MUX_CFG(GPIO1B1_SPI_TXD_UART1_SOUT_NAME,        GPIO1B,    2,    2,    0,    DEFAULT)
MUX_CFG(GPIO1B2_SPI_RXD_UART1_SIN_NAME,         GPIO1B,    4,    2,    0,    DEFAULT)
MUX_CFG(GPIO1B3_SPI_CSN0_UART1_RTSN_NAME,       GPIO1B,    6,    2,    0,    DEFAULT)
MUX_CFG(GPIO1B4_SPI_CSN1_NAME,                  GPIO1B,    8,    1,    0,    DEFAULT)
MUX_CFG(GPIO1B5_MMC0_RSTNOUT_NAME,              GPIO1B,    10,    1,    0,    DEFAULT)
MUX_CFG(GPIO1B6_MMC0_PWREN_NAME,                GPIO1B,    12,    1,    0,    DEFAULT)
MUX_CFG(GPIO1B7_MMC0_CMD_NAME,                  GPIO1B,    14,    1,    0,    DEFAULT)

//gpio1c
MUX_CFG(GPIO1C0_MMC0_CLKOUT_NAME,               GPIO1C,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO1C1_MMC0_DETN_NAME,                 GPIO1C,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO1C2_MMC0_D0_NAME,                   GPIO1C,    4,    1,    0,    DEFAULT)
MUX_CFG(GPIO1C3_MMC0_D1_NAME,                   GPIO1C,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO1C4_MMC0_D2_NAME,                   GPIO1C,    8,    1,    0,    DEFAULT)
MUX_CFG(GPIO1C5_MMC0_D3_NAME,                   GPIO1C,    10,    1,    0,    DEFAULT)
MUX_CFG(GPIO1C6_NAND_CS2_EMMC_CMD_NAME,         GPIO1C,    12,    2,    0,    DEFAULT)
MUX_CFG(GPIO1C7_NAND_CS3_EMMC_RSTNOUT_NAME,     GPIO1C,    14,    2,    0,    DEFAULT)

//gpio1d
MUX_CFG(GPIO1D0_NAND_D0_EMMC_D0_NAME,           GPIO1D,    0,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D1_NAND_D1_EMMC_D1_NAME,           GPIO1D,    2,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D2_NAND_D2_EMMC_D2_NAME,           GPIO1D,    4,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D3_NAND_D3_EMMC_D3_NAME,           GPIO1D,    6,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D4_NAND_D4_EMMC_D4_NAME,           GPIO1D,    8,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D5_NAND_D5_EMMC_D5_NAME,           GPIO1D,    10,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D6_NAND_D6_EMMC_D6_NAME,           GPIO1D,    12,    2,    0,    DEFAULT)
MUX_CFG(GPIO1D7_NAND_D7_EMMC_D7_NAME,           GPIO1D,    14,    2,    0,    DEFAULT)

//gpio2a
MUX_CFG(GPIO2A0_NAND_ALE_NAME,                  GPIO2A,    0,    1,    0,    DEFAULT)
MUX_CFG(GPIO2A1_NAND_CLE_NAME,                  GPIO2A,    2,    1,    0,    DEFAULT)
MUX_CFG(GPIO2A2_NAND_WRN_NAME,                  GPIO2A,    4,    1,    0,    DEFAULT)
MUX_CFG(GPIO2A3_NAND_RDN_NAME,                  GPIO2A,    6,    1,    0,    DEFAULT)
MUX_CFG(GPIO2A4_NAND_RDY_NAME,                  GPIO2A,    8,    1,    0,    DEFAULT)
MUX_CFG(GPIO2A5_NAND_WP_EMMC_PWREN_NAME,        GPIO2A,    10,    2,    0,    DEFAULT)
MUX_CFG(GPIO2A6_NAND_CS0_NAME,                  GPIO2A,    12,    1,    0,    DEFAULT)
MUX_CFG(GPIO2A7_NAND_DPS_EMMC_CLKOUT_NAME,      GPIO2A,    14,    2,    0,    DEFAULT)

//gpio2b
MUX_CFG(GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME,     GPIO2B,    0,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME,   GPIO2B,    2,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME,   GPIO2B,    4,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME,       GPIO2B,    6,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B4_LCDC0_D10_LCDC1_D10_NAME,       GPIO2B,    8,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B5_LCDC0_D11_LCDC1_D11_NAME,       GPIO2B,    10,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B6_LCDC0_D12_LCDC1_D12_NAME,       GPIO2B,    12,    2,    0,    DEFAULT)
MUX_CFG(GPIO2B7_LCDC0_D13_LCDC1_D13_NAME,       GPIO2B,    14,    2,    0,    DEFAULT)

//gpio2c
MUX_CFG(GPIO2C0_LCDC0_D14_LCDC1_D14_NAME,       GPIO2C,    0,    2,    0,    DEFAULT)
MUX_CFG(GPIO2C1_LCDC0_D15_LCDC1_D15_NAME,       GPIO2C,    2,    2,    0,    DEFAULT)
MUX_CFG(GPIO2C2_LCDC0_D16_LCDC1_D16_NAME,       GPIO2C,    4,    2,    0,    DEFAULT)
MUX_CFG(GPIO2C3_LCDC0_D17_LCDC1_D17_NAME,       GPIO2C,    6,    2,    0,    DEFAULT)
MUX_CFG(GPIO2C4_LCDC0_D18_LCDC1_D18_I2C2_SDA_NAME,GPIO2C,    8,    3,    0,    DEFAULT)
MUX_CFG(GPIO2C5_LCDC0_D19_LCDC1_D19_I2C2_SCL_NAME,GPIO2C,    10,    3,    0,    DEFAULT)
MUX_CFG(GPIO2C6_LCDC0_D20_LCDC1_D20_UART2_SIN_NAME,GPIO2C,    12,    3,    0,    DEFAULT)
MUX_CFG(GPIO2C7_LCDC0_D21_LCDC1_D21_UART2_SOUT_NAME,GPIO2C,    14,    3,    0,    DEFAULT)

//gpio2d
MUX_CFG(GPIO2D0_LCDC0_D22_LCDC1_D22_NAME,       GPIO2D,    0,    2,    0,    DEFAULT)
MUX_CFG(GPIO2D1_LCDC0_D23_LCDC1_D23_NAME,       GPIO2D,    2,    2,    0,    DEFAULT)

//gpio3c
MUX_CFG(GPIO3C1_OTG_DRVVBUS_NAME,               GPIO3C,    2,    1,    0,    DEFAULT)

//gpio3d
MUX_CFG(GPIO3D7_TESTCLK_OUT_NAME,               GPIO3D,    14,    1,    0,    DEFAULT)
};     


void rk30_mux_set(struct mux_config *cfg)
{
	int regValue = 0;
	int mask;
	
	mask = (((1<<(cfg->interleave))-1)<<cfg->offset) << 16;
	regValue |= mask;
	regValue |=(cfg->mode<<cfg->offset);
#ifdef IOMUX_DBG
	printk("%s::reg=0x%p,Value=0x%x,mask=0x%x\n",__FUNCTION__,cfg->mux_reg,regValue,mask);
#endif
	writel_relaxed(regValue,cfg->mux_reg);
	dsb();
	
	return;
}


int __init rk2928_iomux_init(void)
{
	int i;
	printk("%s\n",__func__);
	for(i=0;i<ARRAY_SIZE(rk30_muxs);i++)
	{
		if(rk30_muxs[i].flags != DEFAULT)
			rk30_mux_set(&rk30_muxs[i]);	
	}

#if defined(CONFIG_UART0_RK29) || (CONFIG_RK_DEBUG_UART == 0)
        rk30_mux_api_set(GPIO0C0_UART0_SOUT_NAME, GPIO0C_UART0_SOUT);
        rk30_mux_api_set(GPIO0C1_UART0_SIN_NAME, GPIO0C_UART0_SIN);
#ifdef CONFIG_UART0_CTS_RTS_RK29
	rk30_mux_api_set(GPIO0C2_UART0_RTSN_NAME, GPIO0C_UART0_RTSN);
	rk30_mux_api_set(GPIO0C3_UART0_CTSN_NAME, GPIO0C_UART0_CTSN);
#endif
#endif

#if defined(CONFIG_UART1_RK29) || (CONFIG_RK_DEBUG_UART == 1)
	//UART1 OR SPIM0
	rk30_mux_api_set(GPIO1B2_SPI_RXD_UART1_SIN_NAME, GPIO1B_UART1_SIN);
	rk30_mux_api_set(GPIO1B1_SPI_TXD_UART1_SOUT_NAME, GPIO1B_UART1_SOUT);
#ifdef CONFIG_UART1_CTS_RTS_RK29
	rk30_mux_api_set(GPIO1B0_SPI_CLK_UART1_CTSN_NAME, GPIO1B_UART1_CTSN);
	rk30_mux_api_set(GPIO1B3_SPI_CSN0_UART1_RTSN_NAME, GPIO1B_UART1_RTSN);
#endif
#endif

#if defined(CONFIG_UART2_RK29) || (CONFIG_RK_DEBUG_UART == 2)
        rk30_mux_api_set(GPIO0D0_UART2_RTSN_NAME, GPIO0D_UART2_RTSN);
        rk30_mux_api_set(GPIO0D1_UART2_CTSN_NAME, GPIO0D_UART2_CTSN);
#ifdef CONFIG_UART2_CTS_RTS_RK29
        rk30_mux_api_set(GPIO2C6_LCDC0_D20_LCDC1_D20_UART2_SIN_NAME, GPIO2C_UART2_SIN);
        rk30_mux_api_set(GPIO2C7_LCDC0_D21_LCDC1_D21_UART2_SOUT_NAME, GPIO2C_UART2_SOUT);
#endif
#endif

#ifdef CONFIG_SPIM0_RK29
	//UART1 OR SPIM0
        rk30_mux_api_set(GPIO1B0_SPI_CLK_UART1_CTSN_NAME, GPIO1B_SPI_CLK);
        rk30_mux_api_set(GPIO1B1_SPI_TXD_UART1_SOUT_NAME, GPIO1B_SPI_TXD);
        rk30_mux_api_set(GPIO1B2_SPI_RXD_UART1_SIN_NAME, GPIO1B_SPI_RXD);
        rk30_mux_api_set(GPIO1B3_SPI_CSN0_UART1_RTSN_NAME, GPIO1B_SPI_CSN0);
#endif

#ifdef CONFIG_I2C0_RK30
        rk30_mux_api_set(GPIO0A0_I2C0_SCL_NAME, GPIO0A_I2C0_SCL);
        rk30_mux_api_set(GPIO0A1_I2C0_SDA_NAME, GPIO0A_I2C0_SDA);
#endif

#ifdef CONFIG_I2C1_RK30
        rk30_mux_api_set(GPIO0A2_I2C1_SCL_NAME, GPIO0A_I2C1_SCL);
        rk30_mux_api_set(GPIO0A3_I2C1_SDA_NAME, GPIO0A_I2C1_SDA);
#endif

#ifdef CONFIG_I2C2_RK30
        rk30_mux_api_set(GPIO2C4_LCDC0_D18_LCDC1_D18_I2C2_SDA_NAME, GPIO2C_I2C2_SDA);
        rk30_mux_api_set(GPIO2C5_LCDC0_D19_LCDC1_D19_I2C2_SCL_NAME, GPIO2C_I2C2_SCL);
#endif

#ifdef CONFIG_I2C3_RK30
        rk30_mux_api_set(GPIO0A6_I2C3_SCL_HDMI_DDCSCL_NAME, GPIO0A_I2C3_SCL);
        rk30_mux_api_set(GPIO0A7_I2C3_SDA_HDMI_DDCSDA_NAME, GPIO0A_I2C3_SDA);
#endif

	return 0;
}

/*
 *config iomux : input iomux name and iomux flags
 */ 
void rk30_mux_api_set(char *name, unsigned int mode)
{
  int i;
        if (!name) {
                return;
        } 
	for(i=0;i<ARRAY_SIZE(rk30_muxs);i++)
	{
		if (!strcmp(rk30_muxs[i].name, name))
		{
		    rk30_muxs[i].premode = rk30_muxs[i].mode;
			rk30_muxs[i].mode = mode;
			rk30_mux_set(&rk30_muxs[i]);	
			break;			
		}
	}
}
EXPORT_SYMBOL(rk30_mux_api_set);


int rk30_mux_api_get(char *name)
{
	int i,ret=0;
	if (!name) {
		return -1;
	}
	for(i=0;i<ARRAY_SIZE(rk30_muxs);i++)
	{
		if (!strcmp(rk30_muxs[i].name, name))
		{
			ret = readl(rk30_muxs[i].mux_reg);
			ret = (ret >> rk30_muxs[i].offset) &((1<<(rk30_muxs[i].interleave))-1);
			return ret;
		}
	}

	return -1;
}
EXPORT_SYMBOL(rk30_mux_api_get);

