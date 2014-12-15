/*
 * Meson Power Management Routines
 *
 * Copyright (C) 2010 Amlogic, Inc. http://www.amlogic.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/fs.h>

#include <asm/cacheflush.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include <mach/pm.h>
#include <mach/am_regs.h>
#include <plat/sram.h>
#include <mach/power_gate.h>
#include <mach/gpio.h>
#include <mach/pctl.h>
#include <mach/clock.h>
#include <plat/regops.h>
#include <plat/io.h>

#ifdef CONFIG_WAKELOCK
#include <linux/wakelock.h>
#endif

#include <mach/mod_gate.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
static int early_suspend_flag = 0;
#endif

#define ON  1
#define OFF 0
#define M6_TV_CLK_DEBUG
//#include <mach/sleep.h>
//#define EARLY_SUSPEND_USE_XTAL
//#define MESON_SUSPEND_DEBUG

#ifndef CONFIG_MESON_SUSPEND
static void (*meson_sram_suspend)(struct meson_pm_config *);
#endif

static struct meson_pm_config *pdata;
//static int mask_save_0[5];
//static int mask_save_1[5];

#ifndef CONFIG_MESON_SUSPEND
static void meson_sram_push(void *dest, void *src, unsigned int size)
{
	int res = 0;
	memcpy(dest, src, size);
	flush_icache_range((unsigned long)dest, (unsigned long)(dest + size));
	res = memcmp(dest, src, size);
	printk("compare code in sram addr = 0x%x, size = 0x%x, result = %d", (unsigned)dest, size, res);
}
#endif

#define M6TV_GATE_OFF(_MOD) do {power_gate_flag[GCLK_IDX_##_MOD] = IS_CLK_GATE_ON(_MOD);CLK_GATE_OFF(_MOD);} while(0)
#define M6TV_GATE_ON(_MOD) do {if (power_gate_flag[GCLK_IDX_##_MOD]) CLK_GATE_ON(_MOD);} while(0)
#define GATE_SWITCH(flag, _MOD) do {if (flag) M6TV_GATE_ON(_MOD); else M6TV_GATE_OFF(_MOD);} while(0)
static int power_gate_flag[GCLK_IDX_MAX];

void power_init_off(void)
{
#if 0
	aml_clr_reg32_mask(P_HHI_DEMOD_CLK_CNTL, (1 << 8));
	aml_clr_reg32_mask(P_HHI_SATA_CLK_CNTL, (1 << 8));
	aml_clr_reg32_mask(P_HHI_ETH_CLK_CNTL, (1 << 8));
	aml_clr_reg32_mask(P_HHI_WIFI_CLK_CNTL, (1 << 0));
	aml_set_reg32_mask(P_HHI_DEMOD_PLL_CNTL, (1 << 15));
#endif
}

void power_gate_switch(int flag)
{
	//GATE_SWITCH(flag, DDR);
	GATE_SWITCH(flag, DOS);
	//GATE_SWITCH(flag,MIPI_APB_CLK);
	//GATE_SWITCH(flag,MIPI_SYS_CLK);
	//GATE_SWITCH(flag, AHB_BRIDGE);
	//GATE_SWITCH(flag, ISA);
	//GATE_SWITCH(flag, APB_CBUS);
	//GATE_SWITCH(flag, _1200XXX);
	GATE_SWITCH(flag, SPICC);
	GATE_SWITCH(flag, I2C);
	GATE_SWITCH(flag, SAR_ADC);
	GATE_SWITCH(flag, SMART_CARD_MPEG_DOMAIN);
	//GATE_SWITCH(flag, RANDOM_NUM_GEN);
	GATE_SWITCH(flag, UART0);
	GATE_SWITCH(flag, SDHC);
	GATE_SWITCH(flag, STREAM);
	GATE_SWITCH(flag, ASYNC_FIFO);
	GATE_SWITCH(flag, SDIO);
	GATE_SWITCH(flag, AUD_BUF);
	GATE_SWITCH(flag, HDMI_RX);
	//GATE_SWITCH(flag, HIU_PARSER);
	//GATE_SWITCH(flag, AMRISC);
	GATE_SWITCH(flag, BT656_IN);
	//GATE_SWITCH(flag, ASSIST_MISC);
	//GATE_SWITCH(flag, VI_CORE);
	GATE_SWITCH(flag, SPI2);
	GATE_SWITCH(flag, ACODEC);
	//GATE_SWITCH(flag, MDEC_CLK_ASSIST);
	//GATE_SWITCH(flag, MDEC_CLK_PSC);
/*******************************************************/
	GATE_SWITCH(flag, PCLK_TVFE);
	GATE_SWITCH(flag, SPI1);
	//GATE_SWITCH(flag, AUD_IN);
	GATE_SWITCH(flag, ETHERNET);
	GATE_SWITCH(flag, AIU_AI_TOP_GLUE);
	GATE_SWITCH(flag, AIU_IEC958);
	GATE_SWITCH(flag, AIU_I2S_OUT);
	GATE_SWITCH(flag, AIU_AMCLK_MEASURE);
	GATE_SWITCH(flag, AIU_AIFIFO2);
	GATE_SWITCH(flag, AIU_AUD_MIXER);
	GATE_SWITCH(flag, AIU_MIXER_REG);
	GATE_SWITCH(flag, AIU_ADC);
	GATE_SWITCH(flag, BLK_MOV);
	//GATE_SWITCH(flag, UART1);
	//GATE_SWITCH(flag, VGHL_PWM);
	GATE_SWITCH(flag, GE2D);
	GATE_SWITCH(flag, USB0);
	GATE_SWITCH(flag, USB1);
	//GATE_SWITCH(flag, RESET);
	GATE_SWITCH(flag, NAND);
	GATE_SWITCH(flag, HIU_PARSER_TOP);
	GATE_SWITCH(flag, USB_GENERAL);
	//GATE_SWITCH(flag, MDEC_CLK_DBLK);
	//GATE_SWITCH(flag, MIPI_PHY);
	GATE_SWITCH(flag, VIDEO_IN);
	//GATE_SWITCH(flag, AHB_ARB0);
	GATE_SWITCH(flag, EFUSE);
	GATE_SWITCH(flag, ROM_CLK);
	//GATE_SWITCH(flag, AHB_DATA_BUS);
	//GATE_SWITCH(flag, AHB_CONTROL_BUS);
	GATE_SWITCH(flag, MISC_USB1_TO_DDR);
	GATE_SWITCH(flag, MISC_USB0_TO_DDR);
	GATE_SWITCH(flag, USB2_TO_DDR);
	GATE_SWITCH(flag, USB3_TO_DDR);
	GATE_SWITCH(flag, AIU_PCLK);
	//GATE_SWITCH(flag, MMC_PCLK);
	GATE_SWITCH(flag, UART2);
	GATE_SWITCH(flag, USB2_OTG_CON);
	GATE_SWITCH(flag, USB3_OTG_CON);
	GATE_SWITCH(flag, DEMOD_PCLK);
	/****/
	GATE_SWITCH(flag, DAC_CLK);
	GATE_SWITCH(flag, AIU_AOCLK);
	//GATE_SWITCH(flag, AIU_AMCLK);
	GATE_SWITCH(flag, AIU_ICE958_AMCLK);
	//GATE_SWITCH(flag, AIU_AUDIN_SCLK);
	GATE_SWITCH(flag, RAND_NUM_GEN);
}
EXPORT_SYMBOL(power_gate_switch);

void early_power_gate_switch(int flag)
{
	//GATE_SWITCH(flag, AMRISC);
	//GATE_SWITCH(flag, AUD_IN);
	GATE_SWITCH(flag, BLK_MOV);
	GATE_SWITCH(flag, VENC_I_TOP);
	GATE_SWITCH(flag, VENC_P_TOP);
	GATE_SWITCH(flag, VENC_T_TOP);
	GATE_SWITCH(flag, VENC_DAC);
	GATE_SWITCH(flag, HDMI_INTR_SYNC);
	GATE_SWITCH(flag, HDMI_PCLK);
	GATE_SWITCH(flag, MISC_DVIN);
	GATE_SWITCH(flag, MISC_RDMA);
	GATE_SWITCH(flag, VENCI_INT);
	GATE_SWITCH(flag, VIU2);
	GATE_SWITCH(flag, VENCP_INT);
	GATE_SWITCH(flag, VENCT_INT);
	GATE_SWITCH(flag, VENCL_INT);
	GATE_SWITCH(flag, VENC_L_TOP);
	GATE_SWITCH(flag, VCLK2_VENCI);
	GATE_SWITCH(flag, VCLK2_VENCI1);
	GATE_SWITCH(flag, VCLK2_VENCP);
	GATE_SWITCH(flag, VCLK2_VENCP1);
	GATE_SWITCH(flag, VCLK2_VENCT);
	GATE_SWITCH(flag, VCLK2_VENCT1);
	GATE_SWITCH(flag, VCLK2_OTHER);
	GATE_SWITCH(flag, VCLK2_ENCI);
	GATE_SWITCH(flag, VCLK2_ENCP);
	//GATE_SWITCH(flag, VCLK1_HDMI);
	GATE_SWITCH(flag, ENC480P);
	GATE_SWITCH(flag, VCLK2_ENCT);
	GATE_SWITCH(flag, VCLK2_ENCL);
	GATE_SWITCH(flag, VCLK2_VENCL);
	GATE_SWITCH(flag, VCLK2_OTHER1);
	//GATE_SWITCH(flag, LED_PWM);
	//GATE_SWITCH(flag, GE2D);
	//GATE_SWITCH(flag, VIDEO_IN);
	GATE_SWITCH(flag, VI_CORE);
}
EXPORT_SYMBOL(early_power_gate_switch);

#define CLK_COUNT 8
static char clk_flag[CLK_COUNT];
static char clk_aud_23;
static char clk_vdec_24;
static unsigned clks[CLK_COUNT] = {
	P_HHI_ETH_CLK_CNTL,
	P_HHI_VID_CLK_CNTL,
	P_HHI_VIID_CLK_CNTL,
	P_HHI_AUD_CLK_CNTL,
	P_HHI_MALI_CLK_CNTL,
	P_HHI_HDMI_CLK_CNTL,
	P_HHI_MPEG_CLK_CNTL,
	P_HHI_VDEC_CLK_CNTL,
};

static char clks_name[CLK_COUNT][32] = {
	"HHI_ETH_CLK_CNTL",
	"HHI_VID_CLK_CNTL",
	"HHI_VIID_CLK_CNTL",
	"HHI_AUD_CLK_CNTL",
	"HHI_MALI_CLK_CNTL",
	"HHI_HDMI_CLK_CNTL",
	"HHI_MPEG_CLK_CNTL",
	"HHI_VDEC_CLK_CNTL",
};

#ifdef EARLY_SUSPEND_USE_XTAL
#define EARLY_CLK_COUNT 3
#else
#define EARLY_CLK_COUNT 2
#endif
static char early_clk_flag[EARLY_CLK_COUNT];
static unsigned early_clks[EARLY_CLK_COUNT] = {
	P_HHI_VID_CLK_CNTL,
	P_HHI_VIID_CLK_CNTL,
#ifdef EARLY_SUSPEND_USE_XTAL
	P_HHI_MPEG_CLK_CNTL,
#endif
};

static char early_clks_name[EARLY_CLK_COUNT][32] = {
	"HHI_VID_CLK_CNTL",
	"HHI_VIID_CLK_CNTL",
#ifdef EARLY_SUSPEND_USE_XTAL
	"HHI_MPEG_CLK_CNTL",
#endif
};
#if 1
static void wait_uart_empty(void)
{
	unsigned int count = 0;
	do {
		if ((aml_read_reg32(P_UART0_STATUS) & (1<<22)) == 0)
			udelay(4);
		else
			break;
		count++;
	} while (count < 2000000);

	count = 0;
	do{
		if((aml_read_reg32(P_UART1_STATUS) & (1<<22)) == 0)
			udelay(4);
		else
			break;
		count++;
	} while (count < 2000000);

	count = 0;
	do {
		if((aml_read_reg32(P_AO_UART_STATUS) & (1<<22)) == 0)
			udelay(4);
		else
			break;
		count++;
	} while (count < 2000000);
}
#else
static void wait_uart_empty()
{
	do {
		if ((aml_read_reg32(P_UART0_STATUS) & (1<<22)) == 0)
			udelay(4);
		else
			break;
	} while (1);

	do {
		if ((aml_read_reg32(P_UART1_STATUS) & (1<<22)) == 0)
			udelay(4);
		else
			break;
	} while (1);

	do {
		if ((aml_read_reg32(P_AO_UART_STATUS) & (1<<22)) == 0)
			udelay(4);
		else
			break;
	} while (1);
}
#endif

static unsigned uart_rate_backup;
static unsigned xtal_uart_rate_backup;

void clk_switch(int flag)
{
	int i;

	if (flag) {
		for (i = CLK_COUNT - 1; i >= 0; i--) {
			if (clk_flag[i]) {
				if ((clks[i] == P_HHI_VID_CLK_CNTL)||(clks[i] == P_HHI_VIID_CLK_CNTL)) {
					aml_set_reg32_bits(clks[i],clk_flag[i],19,2);
				} else if (clks[i] == P_HHI_MPEG_CLK_CNTL) {
					if (uart_rate_backup == 0) {
						struct clk* sys_clk = clk_get_sys("clk81", NULL);
						sys_clk->rate = 0;
						uart_rate_backup = clk_get_rate(sys_clk);
					}
					wait_uart_empty();
					aml_set_reg32_mask(clks[i],(1<<7));//gate on pll
					udelay(10);
					aml_set_reg32_mask(clks[i],(1<<8));//switch to pll
					udelay(10);
					aml_clr_reg32_mask(P_UART0_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_UART0_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_UART0_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_UART1_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_UART1_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_UART1_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_AO_UART2_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_AO_UART2_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_AO_UART2_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_AO_UART_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_AO_UART_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_AO_UART_REG5, 1, 23, 1);
				} else {
					aml_set_reg32_mask(clks[i],(1<<8));
				}
				clk_flag[i] = 0;
				printk(KERN_INFO "clk %s(%x) on\n", clks_name[i], clks[i]);
			}
#ifdef M6_TV_CLK_DEBUG
			if (clk_aud_23)
				aml_set_reg32_mask(P_HHI_AUD_CLK_CNTL,(1<<23));
			if (clk_vdec_24)
				aml_set_reg32_mask(P_HHI_VDEC_CLK_CNTL,(1<<24));
#endif
		}
	} else {
		for (i = 0; i < CLK_COUNT; i++) {
			if ((clks[i] == P_HHI_VID_CLK_CNTL)||(clks[i] == P_HHI_VIID_CLK_CNTL)) {
				clk_flag[i] = aml_get_reg32_bits(clks[i], 19, 2);
				if (clk_flag[i]) {
					aml_clr_reg32_mask(clks[i], (1<<19)|(1<<20));
				}
			} else if (clks[i] == P_HHI_MPEG_CLK_CNTL) {
				if (aml_read_reg32(clks[i]) & (1 << 8)) {
					if (xtal_uart_rate_backup == 0) {//if no early suspend supported
						struct clk* sys_clk = clk_get_sys("xtal", NULL);
						xtal_uart_rate_backup = clk_get_rate(sys_clk);
					}
					wait_uart_empty();
					clk_flag[i] = 1;
					aml_clr_reg32_mask(clks[i], (1 << 8)); // 24M
					udelay(10);
					aml_clr_reg32_mask(clks[i], (1 << 7)); // 24M
					udelay(10);
					aml_clr_reg32_mask(P_UART0_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_UART0_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_UART0_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_UART1_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_UART1_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_UART1_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_AO_UART2_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_AO_UART2_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_AO_UART2_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_AO_UART_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_AO_UART_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_AO_UART_REG5, 1, 23, 1);
				}
			}
#ifdef M6_TV_CLK_DEBUG
			else if (clks[i] == P_HHI_AUD_CLK_CNTL) {
				clk_flag[i] = aml_get_reg32_bits(clks[i], 8, 1) ? 1 : 0;
				if (clk_flag[i]) {
					aml_clr_reg32_mask(clks[i], (1 << 8));
				}
				clk_aud_23 = aml_get_reg32_bits(clks[i], 23, 1) ? 1 : 0;
				if (clk_aud_23) {
					aml_clr_reg32_mask(clks[i], (1 << 23));
				}
			}
			else if (clks[i] == P_HHI_VDEC_CLK_CNTL) {
				clk_flag[i] = aml_get_reg32_bits(clks[i], 8, 1) ? 1 : 0;
				if (clk_flag[i]) {
					aml_clr_reg32_mask(clks[i], (1 << 8));
				}
				clk_vdec_24 = aml_get_reg32_bits(clks[i], 24, 1) ? 1 : 0;
				if (clk_vdec_24) {
					aml_clr_reg32_mask(clks[i], (1 << 24));
				}
			}
			else {
				clk_flag[i] = aml_get_reg32_bits(clks[i], 8, 1) ? 1 : 0;
				if (clk_flag[i]) {
					aml_clr_reg32_mask(clks[i], (1 << 8));
				}
			}
#else
			else {
				clk_flag[i] = aml_get_reg32_bits(clks[i], 8, 1) ? 1 : 0;
				if (clk_flag[i]) {
					aml_clr_reg32_mask(clks[i], (1 << 8));
				}
			}
#endif
			if (clk_flag[i]) {
				printk(KERN_INFO "clk %s(%x) off\n", clks_name[i], clks[i]);
				wait_uart_empty();
			}
		}
	}
}
EXPORT_SYMBOL(clk_switch);

void early_clk_switch(int flag)
{
	int i;
	struct clk *sys_clk;
	if (flag) {
		for (i = EARLY_CLK_COUNT - 1; i >= 0; i--) {
			if (early_clk_flag[i]) {
				if ((early_clks[i] == P_HHI_VID_CLK_CNTL)||(early_clks[i] == P_HHI_VIID_CLK_CNTL)) {
					aml_set_reg32_bits(early_clks[i], early_clk_flag[i], 19, 2);
				}
#ifdef EARLY_SUSPEND_USE_XTAL
				else if (early_clks[i] == P_HHI_MPEG_CLK_CNTL) {
					udelay(1000);
					aml_set_reg32_mask(early_clks[i], (1 << 8)); // clk81 back to normal
					aml_clr_reg32_mask(P_UART0_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_UART0_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_UART0_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_UART1_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_UART1_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_UART1_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_AO_UART2_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_AO_UART2_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_AO_UART2_REG5, 1, 23, 1);

					aml_clr_reg32_mask(P_AO_UART_REG5, 0x7FFFFF);
					aml_set_reg32_bits(P_AO_UART_REG5, ((uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
					aml_set_reg32_bits(P_AO_UART_REG5, 1, 23, 1);
					udelay(1000);
				}
#endif
			else {
				aml_set_reg32_mask(early_clks[i], (1 << 8));
			}
				printk(KERN_INFO "late clk %s(%x) on\n", early_clks_name[i], early_clks[i]);
				early_clk_flag[i] = 0;
			}
		}
	} else {
		sys_clk = clk_get_sys("clk81", NULL);
		sys_clk->rate = 0;
		uart_rate_backup = clk_get_rate(sys_clk);
		sys_clk = clk_get_sys("xtal", NULL);
		//        xtal_uart_rate_backup = sys_clk->rate;
		xtal_uart_rate_backup = clk_get_rate(sys_clk);

		for (i = 0; i < EARLY_CLK_COUNT; i++) {
			if ((early_clks[i] == P_HHI_VID_CLK_CNTL)||(early_clks[i] == P_HHI_VIID_CLK_CNTL)) {
				early_clk_flag[i] = aml_get_reg32_bits(early_clks[i], 19, 2);
				if (early_clk_flag[i]) {
					aml_clr_reg32_mask(early_clks[i], (1<<19)|(1<<20));
				}
			}
#ifdef EARLY_SUSPEND_USE_XTAL
			else if (early_clks[i] == P_HHI_MPEG_CLK_CNTL) {
				early_clk_flag[i] = 1;

				udelay(1000);
				aml_clr_reg32_mask(early_clks[i], (1 << 8)); // 24M

				aml_clr_reg32_mask(P_UART0_REG5, 0x7FFFFF);
				aml_set_reg32_bits(P_UART0_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
				aml_set_reg32_bits(P_UART0_REG5, 1, 23, 1);

				aml_clr_reg32_mask(P_UART1_REG5, 0x7FFFFF);
				aml_set_reg32_bits(P_UART1_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
				aml_set_reg32_bits(P_UART1_REG5, 1, 23, 1);

				aml_clr_reg32_mask(P_AO_UART2_REG5, 0x7FFFFF);
				aml_set_reg32_bits(P_AO_UART2_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
				aml_set_reg32_bits(P_AO_UART2_REG5, 1, 23, 1);

				aml_clr_reg32_mask(P_AO_UART_REG5, 0x7FFFFF);
				aml_set_reg32_bits(P_AO_UART_REG5, ((xtal_uart_rate_backup / (115200 * 4)) - 1) & 0x7fffff, 0, 23);
				aml_set_reg32_bits(P_AO_UART_REG5, 1, 23, 1);

				udelay(1000);
			}
#endif
			else {
				early_clk_flag[i] = aml_get_reg32_bits(early_clks[i], 8, 1) ? 1 : 0;
				if (early_clk_flag[i]) {
					aml_clr_reg32_mask(early_clks[i], (1 << 8));
				}
			}
			if (early_clk_flag[i]) {
				printk(KERN_INFO "early clk %s(%x) off\n", early_clks_name[i], early_clks[i]);
			}
		}
	}
}
EXPORT_SYMBOL(early_clk_switch);

#if 0
#define PLL_COUNT 3
static char pll_flag[PLL_COUNT];
static unsigned plls[PLL_COUNT] = {
	P_HHI_VID_PLL_CNTL,
	P_HHI_VIID_PLL_CNTL,
//	P_HHI_AUD_PLL_CNTL,
	P_HHI_MPLL_CNTL,
};

static char plls_name[PLL_COUNT][32] = {
	"HHI_VID_PLL_CNTL",
	"HHI_VIID_PLL_CNTL",
//	"HHI_AUD_PLL_CNTL",
	"HHI_MPLL_CNTL",
};
#endif

#if 0
#define EARLY_PLL_COUNT 2
static char early_pll_flag[EARLY_PLL_COUNT];
static unsigned early_pll_settings[EARLY_PLL_COUNT][4];
static unsigned early_plls[EARLY_PLL_COUNT] = {
	P_HHI_VID_PLL_CNTL,
	P_HHI_VIID_PLL_CNTL,
};

static char early_plls_name[EARLY_PLL_COUNT][32] = {
	"HHI_VID_PLL_CNTL",
	"HHI_VIID_PLL_CNTL",
};
#endif

/*
void pll_switch(int flag)
{
    int i;
    if (flag) {
         for (i = PLL_COUNT - 1; i >= 0; i--) {
            if (pll_flag[i]) {
 					   		if(default_console_loglevel >= 7){
	       	        printk(KERN_INFO "pll %s(%x) on\n", plls_name[i], plls[i]);
	       	        udelay(2000);
	       	        udelay(2000);
								}
                if ((plls[i]==P_HHI_VID_PLL_CNTL)||(plls[i]==P_HHI_VIID_PLL_CNTL)||(plls[i]==P_HHI_MPLL_CNTL)){
                    aml_clr_reg32_mask(plls[i],(1<<30));
                    pll_flag[i] = 0;
                }
                else{
                    aml_clr_reg32_mask(plls[i],(1<<15));//bit15 PD:power down
                    pll_flag[i] = 0;
                }
                udelay(10);
             }
        }
        udelay(1000);
	     } else {
        for (i = 0; i < PLL_COUNT; i++) {
        	  if ((plls[i]==P_HHI_VID_PLL_CNTL)||(plls[i]==P_HHI_VIID_PLL_CNTL)||(plls[i]==P_HHI_MPLL_CNTL))
        	  	pll_flag[i]=aml_get_reg32_bits(plls[i],30,1) ? 0:1;
        	  else
        	  	pll_flag[i]=aml_get_reg32_bits(plls[i],15,1) ? 0:1;
            if (pll_flag[i]) {
                printk(KERN_INFO "pll %s(%x) off\n", plls_name[i], plls[i]);
                if ((plls[i]==P_HHI_VID_PLL_CNTL)||(plls[i]==P_HHI_VIID_PLL_CNTL)){
                    aml_set_reg32_mask(plls[i],(1<<30));
                }
                else{
                    aml_set_reg32_mask(plls[i],(1<<15));
                }
            }
        }
    }
}
EXPORT_SYMBOL(pll_switch);
*/


/*
void early_pll_switch(int flag)//for MX only
{
    int i;
    if (flag) {
        for (i = EARLY_PLL_COUNT - 1; i >= 0; i--) {
            if (early_pll_flag[i]) {
				early_pll_flag[i] = 0;
                if (early_plls[i]==P_HHI_VID_PLL_CNTL)
                {
                    do{
	                    aml_write_reg32(P_HHI_VID_PLL_CNTL,1<<29);
				        aml_write_reg32(P_HHI_VID_PLL_CNTL2,0x814d3928);
				        aml_write_reg32(P_HHI_VID_PLL_CNTL3,0x6d425012);
				        aml_write_reg32(P_HHI_VID_PLL_CNTL4,0x110);

                        aml_write_reg32(P_HHI_VID_PLL_CNTL,(early_pll_settings[i][0] & ~(1<<30))|1<<29);
                        aml_write_reg32(P_HHI_VID_PLL_CNTL,early_pll_settings[i][0] & ~(3<<30));
                        udelay(1000);
                    }while((aml_read_reg32(P_HHI_VID_PLL_CNTL) & 1<<31) == 0);
                }
                else if(early_plls[i]==P_HHI_VIID_PLL_CNTL)
                {
                    do{
                        aml_write_reg32(P_HHI_VIID_PLL_CNTL,1<<29);
                        aml_write_reg32(P_HHI_VIID_PLL_CNTL2,0x814d3928);
                        aml_write_reg32(P_HHI_VIID_PLL_CNTL3,0x6d425012);
                        aml_write_reg32(P_HHI_VIID_PLL_CNTL4,0x110);

                        aml_write_reg32(P_HHI_VIID_PLL_CNTL,(early_pll_settings[i][0] & ~(1<<30))|1<<29);
                        aml_write_reg32(P_HHI_VIID_PLL_CNTL,early_pll_settings[i][0] & ~(3<<30));
                        udelay(1000);
                    }while((aml_read_reg32(P_HHI_VIID_PLL_CNTL) & 1<<31) == 0);
                }
                else{
                   printk("Error: not restore pll setting!!!\n");
                }
                printk(KERN_INFO "late pll %s(%x) on\n", early_plls_name[i], early_plls[i]);
            }
        }
        udelay(1000);
    } else {
        for (i = 0; i < EARLY_PLL_COUNT; i++) {
            if (early_plls[i]==P_HHI_VID_PLL_CNTL)
            {
            	early_pll_flag[i] = aml_get_reg32_bits(early_plls[i],30,1) ? 0 : 1;
				early_pll_settings[i][0]=aml_read_reg32(P_HHI_VID_PLL_CNTL);
				early_pll_settings[i][1]=aml_read_reg32(P_HHI_VID_PLL_CNTL2);
				early_pll_settings[i][2]=aml_read_reg32(P_HHI_VID_PLL_CNTL3);
				early_pll_settings[i][3]=aml_read_reg32(P_HHI_VID_PLL_CNTL4);
            }
            else if(early_plls[i]==P_HHI_VIID_PLL_CNTL)
            {
                early_pll_flag[i] = aml_get_reg32_bits(early_plls[i],30,1) ? 0 : 1;
				early_pll_settings[i][0]=aml_read_reg32(P_HHI_VIID_PLL_CNTL);
				early_pll_settings[i][1]=aml_read_reg32(P_HHI_VIID_PLL_CNTL2);
				early_pll_settings[i][2]=aml_read_reg32(P_HHI_VIID_PLL_CNTL3);
				early_pll_settings[i][3]=aml_read_reg32(P_HHI_VIID_PLL_CNTL4);
			}
            else
                printk("Error: not store pll setting!\n");
            if (early_pll_flag[i]) {
                printk(KERN_INFO "early pll %s(%x) off\n", early_plls_name[i], early_plls[i]);
                if ((early_plls[i]==P_HHI_VID_PLL_CNTL)||(early_plls[i]==P_HHI_VIID_PLL_CNTL))
                   aml_set_reg32_mask(early_plls[i], (1 << 30));
            }
        }
    }
}
EXPORT_SYMBOL(early_pll_switch);
*/

typedef struct {
	char name[32];
	unsigned reg_addr;
	unsigned set_bits;
	unsigned clear_bits;
	unsigned reg_value;
	unsigned enable; // 1:cbus 2:apb 3:ahb 0:disable
} analog_t;

//#define ANALOG_COUNT    2
#define ANALOG_COUNT    1
static analog_t analog_regs[ANALOG_COUNT] = {
    {"SAR_ADC",             P_SAR_ADC_REG3,       1 << 28, (1 << 30) | (1 << 21),    0,  1},
#if 0
#ifdef ADJUST_CORE_VOLTAGE
    {"LED_PWM_REG0",        P_LED_PWM_REG0,       1 << 13,          1 << 12,              0,  0}, // needed for core voltage adjustment, so not off
#else
    {"LED_PWM_REG0",        P_LED_PWM_REG0,       1 << 13,          1 << 12,              0,  1},
#endif
#endif
    //{"VGHL_PWM_REG0",       P_VGHL_PWM_REG0,      1 << 13,          1 << 12,              0,  1},
};


void analog_switch(int flag)
{
	int i;
	unsigned reg_value = 0;

	if (flag) {
		printk(KERN_INFO "analog on\n");
		aml_set_reg32_mask(P_AM_ANALOG_TOP_REG0, 1 << 1); // set 0x206e bit[1] 1 to power on top analog
		for (i = 0; i < ANALOG_COUNT; i++) {
			if (analog_regs[i].enable && (analog_regs[i].set_bits || analog_regs[i].clear_bits)) {
				if (analog_regs[i].enable == 1) {
					aml_write_reg32(analog_regs[i].reg_addr, analog_regs[i].reg_value);
				} else if (analog_regs[i].enable == 2) {
					aml_write_reg32(analog_regs[i].reg_addr, analog_regs[i].reg_value);
				} else if (analog_regs[i].enable == 3) {
					aml_write_reg32(analog_regs[i].reg_addr, analog_regs[i].reg_value);
				}
			}
		}
	} else {
		printk(KERN_INFO "analog off\n");
		for (i = 0; i < ANALOG_COUNT; i++) {
			if (analog_regs[i].enable && (analog_regs[i].set_bits || analog_regs[i].clear_bits)) {
				if (analog_regs[i].enable == 1) {
					analog_regs[i].reg_value = aml_read_reg32(analog_regs[i].reg_addr);
					printk("%s(0x%x):0x%x", analog_regs[i].name, analog_regs[i].reg_addr, analog_regs[i].reg_value);
					if (analog_regs[i].clear_bits) {
						aml_clr_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].clear_bits);
						printk(" & ~0x%x", analog_regs[i].clear_bits);
					}
					if (analog_regs[i].set_bits) {
						aml_set_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].set_bits);
						printk(" | 0x%x", analog_regs[i].set_bits);
					}
					reg_value = aml_read_reg32(analog_regs[i].reg_addr);
					printk(" = 0x%x\n", reg_value);
				} else if (analog_regs[i].enable == 2) {
					analog_regs[i].reg_value = aml_read_reg32(analog_regs[i].reg_addr);
					printk("%s(0x%x):0x%x", analog_regs[i].name, analog_regs[i].reg_addr, analog_regs[i].reg_value);
					if (analog_regs[i].clear_bits) {
						aml_clr_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].clear_bits);
						printk(" & ~0x%x", analog_regs[i].clear_bits);
					}
					if (analog_regs[i].set_bits) {
						aml_set_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].set_bits);
						printk(" | 0x%x", analog_regs[i].set_bits);
					}
					reg_value = aml_read_reg32(analog_regs[i].reg_addr);
					printk(" = 0x%x\n", reg_value);
				} else if (analog_regs[i].enable == 3) {
					analog_regs[i].reg_value = aml_read_reg32(analog_regs[i].reg_addr);
					printk("%s(0x%x):0x%x", analog_regs[i].name, analog_regs[i].reg_addr, analog_regs[i].reg_value);
					if (analog_regs[i].clear_bits) {
						aml_clr_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].clear_bits);
						printk(" & ~0x%x", analog_regs[i].clear_bits);
					}
					if (analog_regs[i].set_bits) {
						aml_set_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].set_bits);
						printk(" | 0x%x", analog_regs[i].set_bits);
					}
					reg_value = aml_read_reg32(analog_regs[i].reg_addr);
					printk(" = 0x%x\n", reg_value);
				}
			}
		}
		aml_clr_reg32_mask(P_AM_ANALOG_TOP_REG0, 1 << 1); // set 0x206e bit[1] 0 to shutdown top analog
	}
}

/*
void usb_switch(int is_on, int ctrl)
{
    int index, por;

    if (ctrl == 0) {
        index = USB_CTL_INDEX_A;
    } else {
        index = USB_CTL_INDEX_B;
    }

    if (is_on) {
        por = USB_CTL_POR_ON;
    } else {
        por = USB_CTL_POR_OFF;
    }

    set_usb_ctl_por(index, por);
}
*/

#ifdef CONFIG_HAS_EARLYSUSPEND
static void meson_system_early_suspend(struct early_suspend *h)
{
	if (!early_suspend_flag) {
		printk(KERN_INFO "sys_suspend\n");
		if (pdata->set_exgpio_early_suspend) {
			pdata->set_exgpio_early_suspend(OFF);
		}
		early_clk_switch(OFF);
		//early_pll_switch(OFF);
		early_power_gate_switch(OFF);
		early_suspend_flag = 1;
	}
}

static void meson_system_late_resume(struct early_suspend *h)
{
	if (early_suspend_flag) {
		early_power_gate_switch(ON);
		//early_pll_switch(ON);
		early_clk_switch(ON);
		early_suspend_flag = 0;
		if (pdata->set_exgpio_early_suspend) {
			pdata->set_exgpio_early_suspend(ON);
		}
		printk(KERN_INFO "sys_resume\n");
	}
#ifdef CONFIG_SUSPEND_WATCHDOG
	extern void reset_watchdog(void);
	reset_watchdog();
#endif
}
#endif

#ifdef CONFIG_SCREEN_ON_EARLY
void vout_pll_resume_early(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	if (early_suspend_flag){
		early_power_gate_switch(ON);
		early_clk_switch(ON);
		early_suspend_flag = 0;
		if(pdata->set_exgpio_early_suspend){
			pdata->set_exgpio_early_suspend(ON);
		}
		printk(KERN_INFO "sys_resume\n");
	}
#endif
	return;
}
EXPORT_SYMBOL(vout_pll_resume_early);
#endif

#define         MODE_DELAYED_WAKE       0
#define         MODE_IRQ_DELAYED_WAKE   1
#define         MODE_IRQ_ONLY_WAKE      2

#ifndef CONFIG_MESON_SUSPEND
static void auto_clk_gating_setup(
    unsigned long sleep_dly_tb, unsigned long mode, unsigned long clear_fiq, unsigned long clear_irq,
    unsigned long   start_delay, unsigned long   clock_gate_dly, unsigned long   sleep_time, unsigned long   enable_delay)
{
}
#endif

//#ifdef CONFIG_MESON_SUSPEND
//extern int meson_power_suspend(void);
//#endif

static void meson_pm_suspend(void)
{
	unsigned ddr_clk_N = 0;


#ifdef ADJUST_CORE_VOLTAGE
	unsigned vcck_backup = aml_get_reg32_bits(P_LED_PWM_REG0, 0, 4);
	printk(KERN_INFO "current vcck is 0x%x!\n", vcck_backup);
#endif

	printk(KERN_INFO "enter meson_pm_suspend!\n");
#ifdef CONFIG_SUSPEND_WATCHDOG
	extern void enable_watchdog(void);
	enable_watchdog();
#endif

	// Disable MMC_LP_CTRL. Will be re-enabled at resume by kreboot.S
	//pr_debug("MMC_LP_CTRL1 before=%#x\n", aml_read_reg32(P_MMC_LP_CTRL1));
	//aml_write_reg32(P_MMC_LP_CTRL1, 0x60a80000);
	//pr_debug("MMC_LP_CTRL1 after=%#x\n", aml_read_reg32(P_MMC_LP_CTRL1));
	//done in bsp, early suspend

	pdata->ddr_clk = aml_read_reg32(P_HHI_DDR_PLL_CNTL);

	ddr_clk_N = (pdata->ddr_clk >> 9) & 0x1f;
	ddr_clk_N = ddr_clk_N * 4; // N*4
	if (ddr_clk_N > 0x1f) {
		ddr_clk_N = 0x1f;
	}
	pdata->ddr_clk &= ~(0x1f << 9);
	pdata->ddr_clk |= ddr_clk_N << 9;

	printk(KERN_INFO "target ddr clock 0x%x!\n", pdata->ddr_clk);

	analog_switch(OFF);

	//usb_switch(OFF, 0);
	//usb_switch(OFF, 1);

	if (pdata->set_vccx2) {
		pdata->set_vccx2(OFF);
	}
	if (pdata->set_pinmux) {
		pdata->set_pinmux(OFF);
		printk("set gpio_p6 output mode low\n");
		WRITE_CBUS_REG_BITS(PREG_PAD_GPIO1_O,0,30,1);
		WRITE_CBUS_REG_BITS(PREG_PAD_GPIO1_EN_N,0,30,1);
	}

	clk_switch(OFF);

	//   pll_switch(OFF);

	power_gate_switch(OFF);

	switch_mod_gate_by_type(MOD_MEDIA_CPU, 1);

#ifndef CONFIG_MESON_SUSPEND
	printk("meson_sram_suspend params 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
	(unsigned)pdata->pctl_reg_base, (unsigned)pdata->mmc_reg_base, (unsigned)pdata->hiu_reg_base,
	(unsigned)pdata->power_key, (unsigned)pdata->ddr_clk, (unsigned)pdata->ddr_reg_backup);

	meson_sram_push(meson_sram_suspend, meson_cpu_suspend,
	meson_cpu_suspend_sz);
#endif

	printk(KERN_INFO "sleep ...\n");

#ifndef CONFIG_MESON_SUSPEND
	auto_clk_gating_setup(	2,			// select 100uS timebase
				MODE_IRQ_ONLY_WAKE,	// Set interrupt wakeup only
				0,			// don't clear the FIQ global mask
				0,			// don't clear the IRQ global mask
				1,			// 1us start delay
				1,			// 1uS gate delay
				1,			// Set the delay wakeup time (1mS)
				1);			// 1uS enable delay
#endif
	//switch A9 clock to xtal 24MHz
	aml_clr_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL, 1 << 7);
	aml_set_reg32_mask(P_HHI_SYS_PLL_CNTL, 1 << 30);//power down sys pll

#ifdef ADJUST_CORE_VOLTAGE
	aml_set_reg32_bits(P_LED_PWM_REG0,0,0,4);
#endif

#if 0
	//while ((READ_AOBUS_REG(AO_RTC_ADDR1) >> 2) & 1){
	while ((aml_read_reg32(AO_RTC_ADDR1) >> 2) & 1){
	udelay(10);
	}
#else
#ifdef CONFIG_MESON_SUSPEND
	meson_power_suspend();
#else
	/**
	* @todo you should not enable irq with a directly register operation
	*       Please replace it with setup_irq .
	*/
	aml_set_reg32_mask(P_SYS_CPU_0_IRQ_IN2_INTR_MASK, pdata->power_key); //enable rtc interrupt only
	meson_sram_suspend(pdata);
#endif
#endif

#ifdef ADJUST_CORE_VOLTAGE
	aml_set_reg32_bits(P_LED_PWM_REG0,vcck_backup, 0, 4);
	udelay(100);
#endif
	aml_clr_reg32_mask(P_HHI_SYS_PLL_CNTL, (1 << 30)); //turn on sys pll

	printk(KERN_INFO "... wake up\n");

	if ((*(volatile unsigned *)(P_AO_RTC_ADDR1)) & (1<<12)) {
		// Woke from alarm, not power button. Set flag to inform key_input driver.
		WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, 0x12345678);
	}
	// clear RTC interrupt
	*(volatile unsigned *)(P_AO_RTC_ADDR1)=(*(volatile unsigned *)(P_AO_RTC_ADDR1))|(0xf000);
	printk(KERN_INFO "RTCADD3=0x%x\n",*(volatile unsigned *)(P_AO_RTC_ADDR3));
	if((*(volatile unsigned *)(P_AO_RTC_ADDR3))|(1<<29))
	{
		*(volatile unsigned *)(P_AO_RTC_ADDR3)=(*(volatile unsigned *)(P_AO_RTC_ADDR3))&(~(1<<29));
		udelay(1000);
	}
	printk(KERN_INFO "RTCADD3=0x%x\n",*(volatile unsigned *)P_AO_RTC_ADDR3);

	if (pdata->set_vccx2) {
		pdata->set_vccx2(ON);
	}
	if (pdata->set_pinmux) {
		pdata->set_pinmux(ON);
	}
	wait_uart_empty();
	aml_set_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL , (1 << 7)); //a9 use pll

	switch_mod_gate_by_type(MOD_MEDIA_CPU, 0);

	power_gate_switch(ON);

	//    pll_switch(ON);

	clk_switch(ON);

	//usb_switch(ON, 0);
	//usb_switch(ON, 1);

	analog_switch(ON);
}

static int meson_pm_prepare(void)
{
	printk(KERN_INFO "enter meson_pm_prepare!\n");
#if 0
	mask_save_0[0] = aml_read_reg32(P_SYS_CPU_0_IRQ_IN0_INTR_MASK);
	mask_save_0[1] = aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK);
	mask_save_0[2] = aml_read_reg32(P_SYS_CPU_0_IRQ_IN2_INTR_MASK);
	mask_save_0[3] = aml_read_reg32(P_SYS_CPU_0_IRQ_IN3_INTR_MASK);
	mask_save_0[4] = aml_read_reg32(P_SYS_CPU_0_IRQ_IN4_INTR_MASK);

	mask_save_1[0] = aml_read_reg32(P_SYS_CPU_1_IRQ_IN0_INTR_MASK);
	mask_save_1[1] = aml_read_reg32(P_SYS_CPU_1_IRQ_IN1_INTR_MASK);
	mask_save_1[2] = aml_read_reg32(P_SYS_CPU_1_IRQ_IN2_INTR_MASK);
	mask_save_1[3] = aml_read_reg32(P_SYS_CPU_1_IRQ_IN3_INTR_MASK);
	mask_save_1[4] = aml_read_reg32(P_SYS_CPU_1_IRQ_IN4_INTR_MASK);

	aml_write_reg32(P_SYS_CPU_0_IRQ_IN0_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN2_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN3_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN4_INTR_MASK, 0x0);

	aml_write_reg32(P_SYS_CPU_1_IRQ_IN0_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN1_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN2_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN3_INTR_MASK, 0x0);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN4_INTR_MASK, 0x0);
#endif


#ifndef CONFIG_MESON_SUSPEND
	meson_sram_push(meson_sram_suspend, meson_cpu_suspend,
	meson_cpu_suspend_sz);
#endif
	return 0;
}

static int meson_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
		case PM_SUSPEND_STANDBY:
		case PM_SUSPEND_MEM:
			meson_pm_suspend();
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static void meson_pm_finish(void)
{
	printk(KERN_INFO "enter meson_pm_finish!\n");
#if 0
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN0_INTR_MASK, mask_save_0[0]);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, mask_save_0[1]);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN2_INTR_MASK, mask_save_0[2]);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN3_INTR_MASK, mask_save_0[3]);
	aml_write_reg32(P_SYS_CPU_0_IRQ_IN4_INTR_MASK, mask_save_0[4]);

	aml_write_reg32(P_SYS_CPU_1_IRQ_IN0_INTR_MASK, mask_save_1[0]);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN1_INTR_MASK, mask_save_1[1]);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN2_INTR_MASK, mask_save_1[2]);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN3_INTR_MASK, mask_save_1[3]);
	aml_write_reg32(P_SYS_CPU_1_IRQ_IN4_INTR_MASK, mask_save_1[4]);
#endif

#ifdef CONFIG_MESON_SUSPEND
#ifdef MESON_SUSPEND_DEBUG // only use this when debug without android rootfs
#ifdef CONFIG_EARLYSUSPEND
	extern void request_suspend_state(suspend_state_t new_state);
	request_suspend_state(0);
#else
	extern int enter_state(suspend_state_t state);
	enter_state(0);
#endif
#endif
#endif
}

static struct platform_suspend_ops meson_pm_ops = {
	.enter        = meson_pm_enter,
	.prepare      = meson_pm_prepare,
	.finish       = meson_pm_finish,
	.valid        = suspend_valid_only_mem,
};

static void power_off_unused_pll(void)
{
	aml_write_reg32(P_HHI_MPLL_CNTL7, 0x01082000); //turn off mp0
	aml_write_reg32(P_HHI_MPLL_CNTL8, 0x01082000); //turn off mp1
}

static void m6tv_set_vccx2(int power_on)
{
	if (power_on) {
		printk(KERN_INFO "%s() Power ON\n", __FUNCTION__);
		aml_clr_reg32_mask(P_PREG_PAD_GPIO0_EN_N,(1<<26));
		aml_clr_reg32_mask(P_PREG_PAD_GPIO0_O,(1<<26));
	} else {
		printk(KERN_INFO "%s() Power OFF\n", __FUNCTION__);
		aml_clr_reg32_mask(P_PREG_PAD_GPIO0_EN_N,(1<<26));
		aml_set_reg32_mask(P_PREG_PAD_GPIO0_O,(1<<26));
	}
}

static struct meson_pm_config m6tv_pm_pdata = {
	.pctl_reg_base = (void *)IO_APB_BUS_BASE,
	.mmc_reg_base = (void *)APB_REG_ADDR(0x1000),
	.hiu_reg_base = (void *)CBUS_REG_ADDR(0x1000),
	.power_key = (1<<8),
	.ddr_clk = 0x00110820,
	.sleepcount = 128,
	.set_vccx2 = m6tv_set_vccx2,
	.core_voltage_adjust = 7,  //5,8
};

static int __init meson_pm_probe(struct platform_device *pdev)
{
	printk(KERN_INFO "enter meson_pm_probe!\n");

	power_init_off();
	power_off_unused_pll();

#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	early_suspend.suspend = meson_system_early_suspend;
	early_suspend.resume = meson_system_late_resume;
	//early_suspend.param = pdev;
	register_early_suspend(&early_suspend);
#endif
	pdev->dev.platform_data = &m6tv_pm_pdata;
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "cannot get platform data\n");
		return -ENOENT;
	}

#ifndef CONFIG_MESON_SUSPEND
	pdata->ddr_reg_backup = sram_alloc(32 * 4);
	if (!pdata->ddr_reg_backup) {
		dev_err(&pdev->dev, "cannot allocate SRAM memory\n");
		return -ENOMEM;
	}

	meson_sram_suspend = sram_alloc(meson_cpu_suspend_sz);
	if (!meson_sram_suspend) {
		dev_err(&pdev->dev, "cannot allocate SRAM memory\n");
		return -ENOMEM;
	}
	meson_sram_push(meson_sram_suspend, meson_cpu_suspend,
		meson_cpu_suspend_sz);
#endif
	suspend_set_ops(&meson_pm_ops);

#ifndef CONFIG_MESON_SUSPEND
	printk(KERN_INFO "meson_pm_probe done 0x%x %d!\n", (unsigned)meson_sram_suspend, meson_cpu_suspend_sz);
#else
	printk(KERN_INFO "meson_pm_probe done !\n");
#endif
	return 0;
}

static int __exit meson_pm_remove(struct platform_device *pdev)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&early_suspend);
#endif
	return 0;
}

static const struct of_device_id m6tv_pm_dt_match[] = {
	{	.compatible = "amlogic,pm",
	},
};

static struct platform_driver meson_pm_driver = {
	.driver = {
		.name = "pm-meson",
		.owner = THIS_MODULE,
		.of_match_table = m6tv_pm_dt_match,
	},
	.remove = __exit_p(meson_pm_remove),
};

static int __init meson_pm_init(void)
{
	return platform_driver_probe(&meson_pm_driver, meson_pm_probe);
}
late_initcall(meson_pm_init);
