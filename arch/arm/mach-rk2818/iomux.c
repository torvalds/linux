/*
 * arch/arm/mach-rk2818/iomux.c
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

#include <mach/rk2818_iomap.h>  
#include <mach/iomux.h>


#define RK2818_IOMUX_A_CON    (RK2818_REGFILE_BASE + IOMUX_A_CON)
#define RK2818_IOMUX_B_CON    (RK2818_REGFILE_BASE + IOMUX_B_CON)

static struct mux_config rk2818_muxs[] = {
/*
 *	 description				mux  mode   mux	  mux  
 *						reg  offset inter mode
 */
MUX_CFG(GPIOE_I2C0_SEL_NAME,		 	A,   30,    2,	  0,	DEFAULT) 		/* 0: i2c0_sda/scl gpioe_u1ir_i2c1 */
MUX_CFG(GPIOE_U1IR_I2C1_NAME,		 	A,   28,    2,	  0,	DEFAULT)		/* 00 : gpio_e6/e7 01 gpiof1_uart1_cpwm1_out_n 10 : i2c1_sda/scl */
MUX_CFG(GPIOF1_UART1_CPWM1_NAME,		A,   26,    2,	  1,	DEFAULT)		/* 00 : gpio_f1 01 : uart1_sout 10 : cx_timer1_pwm */
MUX_CFG(GPIOF0_UART1_CPWM0_NAME,		A,   24,    2,	  1,	DEFAULT)		/* 00 : gpio_f0 01 : uart1_sin 10 : cx_timer0_pwm */
MUX_CFG(GPIOG_MMC1_SEL_NAME,		 	A,   23,    1,	  0,	DEFAULT)		/* 0 : gpio_g2/g3/g7 1: sdmmc1_cmd/data0/clkout */
MUX_CFG(GPIOG_MMC1D_SEL_NAME,		 	A,   22,    1,	  0,	DEFAULT)		/* 0 : gpio_g4/g5/g6 1 : sdmmc1_data1/data2/data3 */
MUX_CFG(GPIOE_SPI1_FLASH_SEL_NAME,		A,   20,    2,	  0,	DEFAULT)		/* 00 : gpio_e1/e2/e3/f7 01 : spi1_clkin/spi1_ss_in_n/spi1_rxd/spi1_txd  10 :flash_cs6 / flash_cs7*/
MUX_CFG(GPIOE_SPI1_FLASH_SEL1_NAME,		A,   18,    2,	  0,	DEFAULT)        /*00 : gpio1_a1/a2 01 : spi1_clkin/spi1_ss_in_n 10 : flash_cs4 / flash_cs5 */
MUX_CFG(GPIOB0_SPI0CSN1_MMC1PCA_NAME,	A,   16,    2,	  0,	DEFAULT)		/* 00 : gpio_b0 01 : spi0_csn1 10 : sdmmc1_pwr_en */
MUX_CFG(GPIOG1_UART0_MMC1WPT_NAME,		A,   14,    2,	  0,	DEFAULT)		/* 00 : gpio_g1 01 : uart0_sout 10 : sdmmc1_write_prt */
MUX_CFG(GPIOG0_UART0_MMC1DET_NAME,		A,   12,    2,	  0,	DEFAULT)		/* 00 : gpio_g0 01 : uart0_sin 10 : sdmmc1_detect_n */
MUX_CFG(GPIOF4_APWM2_MMC0WPT_NAME,		A,   10,    2,	  0,	DEFAULT)		/* 00 : gpio_f4 01 : pwm2 10 : sdmmc0_write_prt */
MUX_CFG(GPIOF3_APWM1_MMC0DETN_NAME,		A,    8,    2,	  0,	DEFAULT)		/* 00 : gpio_f3 01 : pwm1 10 : sdmmc0_detect_n */
MUX_CFG(GPIOB1_SMCS1_MMC0PCA_NAME,		A,    6,    2,	  0,	DEFAULT)		/* 00 : gpio_b1 01 : sm_cs1_n 10 : sdmmc0_pwr_en */
MUX_CFG(GPIOH_MMC0D_SEL_NAME,		 	A,    5,    1,	  0,	DEFAULT)		/* 0 : gpio_h2/h3/h4 1 : sdmm0_data1/data2/data3 */
MUX_CFG(GPIOH_MMC0_SEL_NAME,		 	A,    4,    1,	  0,	DEFAULT)		/* 0 : gpio_h0/h1/h5 1 : sdmmc0_cmd/data0/clkout */
MUX_CFG(GPIOB_SPI0_MMC0_NAME,		 	A,    2,    2,	  0,	DEFAULT)		/* 00 : gpio_b5/b6/b7 01 : spi0_clkout/spi0_txd/spi0_rxd 10 : sdmmc0_data5/data6/data7 */
MUX_CFG(GPIOB4_SPI0CS0_MMC0D4_NAME,		A,    0,    2,	  0,	DEFAULT)		/* 00 : gpio_b4 01 : spi0_csn0 10 : sdmmc0_data4 */

MUX_CFG(GPIOF34_UART3_SEL_NAME,		    B,   31,    1,	  0,	DEFAULT)        /*0 : gpio1_b3 / gpio1_b4 function is decided by gpio1b3_apwm1_mmc0detn(IOMUX_A_CON[9:8]) and gpio1b4_apwm2_mmc0wpt(IOMUX_A_CON[11:10] separately  1 : uart3_sin / uart3_sout*/
MUX_CFG(GPIOF01_UART2_SEL_NAME,		    B,   30,    1,	  0,	DEFAULT)        /*0 : gpio0_b0 / gpio0_b1 function is decided by gpio0b0_spi0csn1_mmc1pca(IOMUX_A_CON [17:16]) and gpio0b1_smcs1_mmc0pca(IOMUX_A_CON [7:6] separately 1 : uart2_sin / uart2_sout */
MUX_CFG(GPIOA23_UART2_SEL_NAME,		    B,   29,    1,	  0,	DEFAULT)        /*0 : gpio0_a2/gpio0_a3 1 : uart2_cts_n / uart2_rts_n */
MUX_CFG(CXGPIO_HSADC_NORFLASH_SEL_NAME,	B,   27,    2,	  0,	DEFAULT)        /*00 : gpio2_22 ~ gpio2_23 01 : hsadc_data_q[9:8] 10 : sm_we_n/sm_oe_n */
MUX_CFG(CXGPIO_HSADC_HIF_SEL_NAME,	    B,   25,    2,	  0,	DEFAULT)        /*00 : gpio2_14 ~ gpio2_21 01 : hsadc_data_q[7:0] 10 : host_data[15:8]*/
MUX_CFG(GPIOA1_HOSTDATA17_SEL_NAME,	    B,   24,    1,	  0,	DEFAULT)        /*0 : gpio0_a1 1: host data 17 */
MUX_CFG(GPIOA0_HOSTDATA16_SEL_NAME,		B,   23,    1,	  0,	DEFAULT)        /*0 : gpio0_a0 1: host data 16 */
MUX_CFG(GPIOE0_VIPDATA0_SEL_NAME,		B,   22,    1,	  0,	DEFAULT)        /*0: gpio1_a0 1: vip_data0 */
MUX_CFG(CXGPIO_GPSCLK_HSADCCLKOUT_NAME,	B,   20,    2,	  0,	DEFAULT)		/* 00 : gpio2_24 01 : gps clk 10 : hsadc_clkout */
MUX_CFG(HSADCDATA_TSCON_SEL_NAME,		B,   19,    1,	  0,	DEFAULT)		/* 0: hsadc_data_i[9:8] 1: ts_fail / ts_valid */
MUX_CFG(GPIOA7_FLASHCS3_SEL_NAME,		B,   18,    1,	  0,	DEFAULT)		/* 0 : gpio_a7 1 : flash_cs3 */
MUX_CFG(GPIOA6_FLASHCS2_SEL_NAME,		B,   17,    1,	  0,	DEFAULT)		/* 0 : gpio_a6 1 : flash_cs2 */
MUX_CFG(GPIOA5_FLASHCS1_SEL_NAME,		B,   16,    1,	  0,	DEFAULT)		/* 0 : gpio_a5 1 : flash_cs1 */
MUX_CFG(GPIOF5_APWM3_DPWM3_NAME,		B,   14,    2,	  0,	DEFAULT)		/* 00 : gpio_f5 01 : pwm3 10 : demod pwm out */
MUX_CFG(GPIOB3_U0RTSN_SEL_NAME,			B,   13,    1,	  0,	DEFAULT)		/* 0 : gpio_b3 1 : uart0_rts_n */
MUX_CFG(GPIOB2_U0CTSN_SEL_NAME,			B,   12,    1,	  0,	DEFAULT)		/* 0 : gpio_b2 1 : uart0_cts_n */
MUX_CFG(GPIOF2_APWM0_SEL_NAME,			B,   11,    1,	  0,	INITIAL)		/* 0 : gpio_f2 1 : pwm0 */
MUX_CFG(GPIOC_LCDC16BIT_SEL_NAME,		B,   10,    1,	  1,	INITIAL)		/* 0 : gpio_d0 ~ gpio_d7 1 : lcdc_data8 ~ lcdc_data15 */
MUX_CFG(GPIOC_LCDC24BIT_SEL_NAME,		B,    9,    1,	  0,	INITIAL)		/* 0 : gpio_c2 ~ gpio_c7 1 : lcdc_data18 ~ lcdc_data23 */
MUX_CFG(GPIOC_LCDC18BIT_SEL_NAME,		B,    8,    1,	  1,	INITIAL)		/* 0 : gpio_c0/c1 1 : lcdc_data16 ~ lcdc_data17 */
MUX_CFG(CXGPIO_LCDDEN_SEL_NAME,			B,    7,    1,	  1,	INITIAL)		/* 0 : gpio2_26 1 : lcdc_denable */
MUX_CFG(CXGPIO_LCDVSYNC_SEL_NAME,		B,    6,    1,	  1,	INITIAL)		/* 0 : gpio2_25 1 : lcdc_vsync */
MUX_CFG(CXGPIO_HSADC_SEL_NAME,			B,    5,    1,	  0,	DEFAULT)		/* 0 : gpio2_14 ~ gpio2_23 1 : hsadc_data_q[9:0] */
MUX_CFG(CXGPIO_HOST_SEL_NAME,			B,    4,    1,	  0,	DEFAULT)		/* 0 : gpio2_0 ~ gpio2_13 1 : host interface */
MUX_CFG(GPIOH7_HSADCCLK_SEL_NAME,		B,    3,    1,	  0,	DEFAULT)		/* 0 : gpio_h7 1 : hsadc_clkin */
MUX_CFG(GPIOH6_IQ_SEL_NAME,			    B,    2,    1,	  0,	DEFAULT)		/* 0 : gpio_h6 1 : ext_iq_index */
MUX_CFG(CXGPIO_I2S_SEL_NAME,			B,    1,    1,	  0,	DEFAULT)		/* 0 : i2s interface 1 : gpio2_27 ~ gpio2_31 */
MUX_CFG(GPIOF6_VIPCLK_SEL_NAME,			B,    0,    1,	  0,	DEFAULT)		/* 0 : gpio_f6 1 : vip clkout */
};

void rk2818_mux_set(struct mux_config *cfg)
{
	int regValue;
	int mask;
	
	mask = ((1<<(cfg->interleave))-1)<<cfg->offset;
	regValue = readl(cfg->mux_reg);
	regValue &=~mask;
	regValue |=(cfg->mode<<cfg->offset);
	#ifdef DEBUG_LHH
	printk("%s::regValue is %x,mask is %x\n",__FUNCTION__,regValue,mask);
	#endif
	writel(regValue,cfg->mux_reg);
	
	return;
}

int rk2818_iomux_init(void)
{
	int i;
	for(i=0;i<ARRAY_SIZE(rk2818_muxs);i++)
	{
		if(rk2818_muxs[i].flags != DEFAULT)
			rk2818_mux_set(&rk2818_muxs[i]);	
	}
	return 0;
}
EXPORT_SYMBOL(rk2818_iomux_init);
/*
 *config iomux : input iomux name and iomux flags
 */ 
void rk2818_mux_api_set(char *name, unsigned int mode)
{
  int i;
        if (!name) {
                return;
        } 
	for(i=0;i<ARRAY_SIZE(rk2818_muxs);i++)
	{
		if (!strcmp(rk2818_muxs[i].name, name))
		{
		    rk2818_muxs[i].premode = rk2818_muxs[i].mode;
			rk2818_muxs[i].mode = mode;
			rk2818_mux_set(&rk2818_muxs[i]);	
			break;			
		}
	}
}
EXPORT_SYMBOL(rk2818_mux_api_set);

void rk2818_mux_api_mode_resume(char *name)
{
    int i; 
        if (!name) {
                return;
        }
	for(i=0;i<ARRAY_SIZE(rk2818_muxs);i++)
	{
		if (!strcmp(rk2818_muxs[i].name, name))
		{
		    if(rk2818_muxs[i].mode != rk2818_muxs[i].premode)
		    {
    			rk2818_muxs[i].mode = rk2818_muxs[i].premode;
    			rk2818_mux_set(&rk2818_muxs[i]);
			}
			break;
		}
	}
}
EXPORT_SYMBOL(rk2818_mux_api_mode_resume);

