/*
 * Copyright (c) 2013 Tomasz Figa <tomasz.figa at gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for all S3C64xx SoCs.
*/

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clock/samsung,s3c64xx-clock.h>

#include "clk.h"
#include "clk-pll.h"

/* S3C64xx clock controller register offsets. */
#define APLL_LOCK		0x000
#define MPLL_LOCK		0x004
#define EPLL_LOCK		0x008
#define APLL_CON		0x00c
#define MPLL_CON		0x010
#define EPLL_CON0		0x014
#define EPLL_CON1		0x018
#define CLK_SRC			0x01c
#define CLK_DIV0		0x020
#define CLK_DIV1		0x024
#define CLK_DIV2		0x028
#define HCLK_GATE		0x030
#define PCLK_GATE		0x034
#define SCLK_GATE		0x038
#define MEM0_GATE		0x03c
#define CLK_SRC2		0x10c
#define OTHERS			0x900

/* Helper macros to define clock arrays. */
#define FIXED_RATE_CLOCKS(name)	\
		static struct samsung_fixed_rate_clock name[]
#define MUX_CLOCKS(name)	\
		static struct samsung_mux_clock name[]
#define DIV_CLOCKS(name)	\
		static struct samsung_div_clock name[]
#define GATE_CLOCKS(name)	\
		static struct samsung_gate_clock name[]

/* Helper macros for gate types present on S3C64xx. */
#define GATE_BUS(_id, cname, pname, o, b) \
		GATE(_id, cname, pname, o, b, 0, 0)
#define GATE_SCLK(_id, cname, pname, o, b) \
		GATE(_id, cname, pname, o, b, CLK_SET_RATE_PARENT, 0)
#define GATE_ON(_id, cname, pname, o, b) \
		GATE(_id, cname, pname, o, b, CLK_IGNORE_UNUSED, 0)

/* list of PLLs to be registered */
enum s3c64xx_plls {
	apll, mpll, epll,
};

static void __iomem *reg_base;
static bool is_s3c6400;

#ifdef CONFIG_PM_SLEEP
static struct samsung_clk_reg_dump *s3c64xx_save_common;
static struct samsung_clk_reg_dump *s3c64xx_save_soc;

/*
 * List of controller registers to be saved and restored during
 * a suspend/resume cycle.
 */
static unsigned long s3c64xx_clk_regs[] __initdata = {
	APLL_LOCK,
	MPLL_LOCK,
	EPLL_LOCK,
	APLL_CON,
	MPLL_CON,
	EPLL_CON0,
	EPLL_CON1,
	CLK_SRC,
	CLK_DIV0,
	CLK_DIV1,
	CLK_DIV2,
	HCLK_GATE,
	PCLK_GATE,
	SCLK_GATE,
};

static unsigned long s3c6410_clk_regs[] __initdata = {
	CLK_SRC2,
	MEM0_GATE,
};

static int s3c64xx_clk_suspend(void)
{
	samsung_clk_save(reg_base, s3c64xx_save_common,
				ARRAY_SIZE(s3c64xx_clk_regs));

	if (!is_s3c6400)
		samsung_clk_save(reg_base, s3c64xx_save_soc,
					ARRAY_SIZE(s3c6410_clk_regs));

	return 0;
}

static void s3c64xx_clk_resume(void)
{
	samsung_clk_restore(reg_base, s3c64xx_save_common,
				ARRAY_SIZE(s3c64xx_clk_regs));

	if (!is_s3c6400)
		samsung_clk_restore(reg_base, s3c64xx_save_soc,
					ARRAY_SIZE(s3c6410_clk_regs));
}

static struct syscore_ops s3c64xx_clk_syscore_ops = {
	.suspend = s3c64xx_clk_suspend,
	.resume = s3c64xx_clk_resume,
};

static void s3c64xx_clk_sleep_init(void)
{
	s3c64xx_save_common = samsung_clk_alloc_reg_dump(s3c64xx_clk_regs,
						ARRAY_SIZE(s3c64xx_clk_regs));
	if (!s3c64xx_save_common)
		goto err_warn;

	if (!is_s3c6400) {
		s3c64xx_save_soc = samsung_clk_alloc_reg_dump(s3c6410_clk_regs,
						ARRAY_SIZE(s3c6410_clk_regs));
		if (!s3c64xx_save_soc)
			goto err_soc;
	}

	register_syscore_ops(&s3c64xx_clk_syscore_ops);
	return;

err_soc:
	kfree(s3c64xx_save_common);
err_warn:
	pr_warn("%s: failed to allocate sleep save data, no sleep support!\n",
		__func__);
}
#else
static void s3c64xx_clk_sleep_init(void) {}
#endif

/* List of parent clocks common for all S3C64xx SoCs. */
PNAME(spi_mmc_p)	= { "mout_epll", "dout_mpll", "fin_pll", "clk27m" };
PNAME(uart_p)		= { "mout_epll", "dout_mpll" };
PNAME(audio0_p)		= { "mout_epll", "dout_mpll", "fin_pll", "iiscdclk0",
				"pcmcdclk0", "none", "none", "none" };
PNAME(audio1_p)		= { "mout_epll", "dout_mpll", "fin_pll", "iiscdclk1",
				"pcmcdclk0", "none", "none", "none" };
PNAME(mfc_p)		= { "hclkx2", "mout_epll" };
PNAME(apll_p)		= { "fin_pll", "fout_apll" };
PNAME(mpll_p)		= { "fin_pll", "fout_mpll" };
PNAME(epll_p)		= { "fin_pll", "fout_epll" };
PNAME(hclkx2_p)		= { "mout_mpll", "mout_apll" };

/* S3C6400-specific parent clocks. */
PNAME(scaler_lcd_p6400)	= { "mout_epll", "dout_mpll", "none", "none" };
PNAME(irda_p6400)	= { "mout_epll", "dout_mpll", "none", "clk48m" };
PNAME(uhost_p6400)	= { "clk48m", "mout_epll", "dout_mpll", "none" };

/* S3C6410-specific parent clocks. */
PNAME(clk27_p6410)	= { "clk27m", "fin_pll" };
PNAME(scaler_lcd_p6410)	= { "mout_epll", "dout_mpll", "fin_pll", "none" };
PNAME(irda_p6410)	= { "mout_epll", "dout_mpll", "fin_pll", "clk48m" };
PNAME(uhost_p6410)	= { "clk48m", "mout_epll", "dout_mpll", "fin_pll" };
PNAME(audio2_p6410)	= { "mout_epll", "dout_mpll", "fin_pll", "iiscdclk2",
				"pcmcdclk1", "none", "none", "none" };

/* Fixed rate clocks generated outside the SoC. */
FIXED_RATE_CLOCKS(s3c64xx_fixed_rate_ext_clks) __initdata = {
	FRATE(0, "fin_pll", NULL, CLK_IS_ROOT, 0),
	FRATE(0, "xusbxti", NULL, CLK_IS_ROOT, 0),
};

/* Fixed rate clocks generated inside the SoC. */
FIXED_RATE_CLOCKS(s3c64xx_fixed_rate_clks) __initdata = {
	FRATE(CLK27M, "clk27m", NULL, CLK_IS_ROOT, 27000000),
	FRATE(CLK48M, "clk48m", NULL, CLK_IS_ROOT, 48000000),
};

/* List of clock muxes present on all S3C64xx SoCs. */
MUX_CLOCKS(s3c64xx_mux_clks) __initdata = {
	MUX_F(0, "mout_syncmux", hclkx2_p, OTHERS, 6, 1, 0, CLK_MUX_READ_ONLY),
	MUX(MOUT_APLL, "mout_apll", apll_p, CLK_SRC, 0, 1),
	MUX(MOUT_MPLL, "mout_mpll", mpll_p, CLK_SRC, 1, 1),
	MUX(MOUT_EPLL, "mout_epll", epll_p, CLK_SRC, 2, 1),
	MUX(MOUT_MFC, "mout_mfc", mfc_p, CLK_SRC, 4, 1),
	MUX(MOUT_AUDIO0, "mout_audio0", audio0_p, CLK_SRC, 7, 3),
	MUX(MOUT_AUDIO1, "mout_audio1", audio1_p, CLK_SRC, 10, 3),
	MUX(MOUT_UART, "mout_uart", uart_p, CLK_SRC, 13, 1),
	MUX(MOUT_SPI0, "mout_spi0", spi_mmc_p, CLK_SRC, 14, 2),
	MUX(MOUT_SPI1, "mout_spi1", spi_mmc_p, CLK_SRC, 16, 2),
	MUX(MOUT_MMC0, "mout_mmc0", spi_mmc_p, CLK_SRC, 18, 2),
	MUX(MOUT_MMC1, "mout_mmc1", spi_mmc_p, CLK_SRC, 20, 2),
	MUX(MOUT_MMC2, "mout_mmc2", spi_mmc_p, CLK_SRC, 22, 2),
};

/* List of clock muxes present on S3C6400. */
MUX_CLOCKS(s3c6400_mux_clks) __initdata = {
	MUX(MOUT_UHOST, "mout_uhost", uhost_p6400, CLK_SRC, 5, 2),
	MUX(MOUT_IRDA, "mout_irda", irda_p6400, CLK_SRC, 24, 2),
	MUX(MOUT_LCD, "mout_lcd", scaler_lcd_p6400, CLK_SRC, 26, 2),
	MUX(MOUT_SCALER, "mout_scaler", scaler_lcd_p6400, CLK_SRC, 28, 2),
};

/* List of clock muxes present on S3C6410. */
MUX_CLOCKS(s3c6410_mux_clks) __initdata = {
	MUX(MOUT_UHOST, "mout_uhost", uhost_p6410, CLK_SRC, 5, 2),
	MUX(MOUT_IRDA, "mout_irda", irda_p6410, CLK_SRC, 24, 2),
	MUX(MOUT_LCD, "mout_lcd", scaler_lcd_p6410, CLK_SRC, 26, 2),
	MUX(MOUT_SCALER, "mout_scaler", scaler_lcd_p6410, CLK_SRC, 28, 2),
	MUX(MOUT_DAC27, "mout_dac27", clk27_p6410, CLK_SRC, 30, 1),
	MUX(MOUT_TV27, "mout_tv27", clk27_p6410, CLK_SRC, 31, 1),
	MUX(MOUT_AUDIO2, "mout_audio2", audio2_p6410, CLK_SRC2, 0, 3),
};

/* List of clock dividers present on all S3C64xx SoCs. */
DIV_CLOCKS(s3c64xx_div_clks) __initdata = {
	DIV(DOUT_MPLL, "dout_mpll", "mout_mpll", CLK_DIV0, 4, 1),
	DIV(HCLKX2, "hclkx2", "mout_syncmux", CLK_DIV0, 9, 3),
	DIV(HCLK, "hclk", "hclkx2", CLK_DIV0, 8, 1),
	DIV(PCLK, "pclk", "hclkx2", CLK_DIV0, 12, 4),
	DIV(DOUT_SECUR, "dout_secur", "hclkx2", CLK_DIV0, 18, 2),
	DIV(DOUT_CAM, "dout_cam", "hclkx2", CLK_DIV0, 20, 4),
	DIV(DOUT_JPEG, "dout_jpeg", "hclkx2", CLK_DIV0, 24, 4),
	DIV(DOUT_MFC, "dout_mfc", "mout_mfc", CLK_DIV0, 28, 4),
	DIV(DOUT_MMC0, "dout_mmc0", "mout_mmc0", CLK_DIV1, 0, 4),
	DIV(DOUT_MMC1, "dout_mmc1", "mout_mmc1", CLK_DIV1, 4, 4),
	DIV(DOUT_MMC2, "dout_mmc2", "mout_mmc2", CLK_DIV1, 8, 4),
	DIV(DOUT_LCD, "dout_lcd", "mout_lcd", CLK_DIV1, 12, 4),
	DIV(DOUT_SCALER, "dout_scaler", "mout_scaler", CLK_DIV1, 16, 4),
	DIV(DOUT_UHOST, "dout_uhost", "mout_uhost", CLK_DIV1, 20, 4),
	DIV(DOUT_SPI0, "dout_spi0", "mout_spi0", CLK_DIV2, 0, 4),
	DIV(DOUT_SPI1, "dout_spi1", "mout_spi1", CLK_DIV2, 4, 4),
	DIV(DOUT_AUDIO0, "dout_audio0", "mout_audio0", CLK_DIV2, 8, 4),
	DIV(DOUT_AUDIO1, "dout_audio1", "mout_audio1", CLK_DIV2, 12, 4),
	DIV(DOUT_UART, "dout_uart", "mout_uart", CLK_DIV2, 16, 4),
	DIV(DOUT_IRDA, "dout_irda", "mout_irda", CLK_DIV2, 20, 4),
};

/* List of clock dividers present on S3C6400. */
DIV_CLOCKS(s3c6400_div_clks) __initdata = {
	DIV(ARMCLK, "armclk", "mout_apll", CLK_DIV0, 0, 3),
};

/* List of clock dividers present on S3C6410. */
DIV_CLOCKS(s3c6410_div_clks) __initdata = {
	DIV(ARMCLK, "armclk", "mout_apll", CLK_DIV0, 0, 4),
	DIV(DOUT_FIMC, "dout_fimc", "hclk", CLK_DIV1, 24, 4),
	DIV(DOUT_AUDIO2, "dout_audio2", "mout_audio2", CLK_DIV2, 24, 4),
};

/* List of clock gates present on all S3C64xx SoCs. */
GATE_CLOCKS(s3c64xx_gate_clks) __initdata = {
	GATE_BUS(HCLK_UHOST, "hclk_uhost", "hclk", HCLK_GATE, 29),
	GATE_BUS(HCLK_SECUR, "hclk_secur", "hclk", HCLK_GATE, 28),
	GATE_BUS(HCLK_SDMA1, "hclk_sdma1", "hclk", HCLK_GATE, 27),
	GATE_BUS(HCLK_SDMA0, "hclk_sdma0", "hclk", HCLK_GATE, 26),
	GATE_ON(HCLK_DDR1, "hclk_ddr1", "hclk", HCLK_GATE, 24),
	GATE_BUS(HCLK_USB, "hclk_usb", "hclk", HCLK_GATE, 20),
	GATE_BUS(HCLK_HSMMC2, "hclk_hsmmc2", "hclk", HCLK_GATE, 19),
	GATE_BUS(HCLK_HSMMC1, "hclk_hsmmc1", "hclk", HCLK_GATE, 18),
	GATE_BUS(HCLK_HSMMC0, "hclk_hsmmc0", "hclk", HCLK_GATE, 17),
	GATE_BUS(HCLK_MDP, "hclk_mdp", "hclk", HCLK_GATE, 16),
	GATE_BUS(HCLK_DHOST, "hclk_dhost", "hclk", HCLK_GATE, 15),
	GATE_BUS(HCLK_IHOST, "hclk_ihost", "hclk", HCLK_GATE, 14),
	GATE_BUS(HCLK_DMA1, "hclk_dma1", "hclk", HCLK_GATE, 13),
	GATE_BUS(HCLK_DMA0, "hclk_dma0", "hclk", HCLK_GATE, 12),
	GATE_BUS(HCLK_JPEG, "hclk_jpeg", "hclk", HCLK_GATE, 11),
	GATE_BUS(HCLK_CAMIF, "hclk_camif", "hclk", HCLK_GATE, 10),
	GATE_BUS(HCLK_SCALER, "hclk_scaler", "hclk", HCLK_GATE, 9),
	GATE_BUS(HCLK_2D, "hclk_2d", "hclk", HCLK_GATE, 8),
	GATE_BUS(HCLK_TV, "hclk_tv", "hclk", HCLK_GATE, 7),
	GATE_BUS(HCLK_POST0, "hclk_post0", "hclk", HCLK_GATE, 5),
	GATE_BUS(HCLK_ROT, "hclk_rot", "hclk", HCLK_GATE, 4),
	GATE_BUS(HCLK_LCD, "hclk_lcd", "hclk", HCLK_GATE, 3),
	GATE_BUS(HCLK_TZIC, "hclk_tzic", "hclk", HCLK_GATE, 2),
	GATE_ON(HCLK_INTC, "hclk_intc", "hclk", HCLK_GATE, 1),
	GATE_ON(PCLK_SKEY, "pclk_skey", "pclk", PCLK_GATE, 24),
	GATE_ON(PCLK_CHIPID, "pclk_chipid", "pclk", PCLK_GATE, 23),
	GATE_BUS(PCLK_SPI1, "pclk_spi1", "pclk", PCLK_GATE, 22),
	GATE_BUS(PCLK_SPI0, "pclk_spi0", "pclk", PCLK_GATE, 21),
	GATE_BUS(PCLK_HSIRX, "pclk_hsirx", "pclk", PCLK_GATE, 20),
	GATE_BUS(PCLK_HSITX, "pclk_hsitx", "pclk", PCLK_GATE, 19),
	GATE_ON(PCLK_GPIO, "pclk_gpio", "pclk", PCLK_GATE, 18),
	GATE_BUS(PCLK_IIC0, "pclk_iic0", "pclk", PCLK_GATE, 17),
	GATE_BUS(PCLK_IIS1, "pclk_iis1", "pclk", PCLK_GATE, 16),
	GATE_BUS(PCLK_IIS0, "pclk_iis0", "pclk", PCLK_GATE, 15),
	GATE_BUS(PCLK_AC97, "pclk_ac97", "pclk", PCLK_GATE, 14),
	GATE_BUS(PCLK_TZPC, "pclk_tzpc", "pclk", PCLK_GATE, 13),
	GATE_BUS(PCLK_TSADC, "pclk_tsadc", "pclk", PCLK_GATE, 12),
	GATE_BUS(PCLK_KEYPAD, "pclk_keypad", "pclk", PCLK_GATE, 11),
	GATE_BUS(PCLK_IRDA, "pclk_irda", "pclk", PCLK_GATE, 10),
	GATE_BUS(PCLK_PCM1, "pclk_pcm1", "pclk", PCLK_GATE, 9),
	GATE_BUS(PCLK_PCM0, "pclk_pcm0", "pclk", PCLK_GATE, 8),
	GATE_BUS(PCLK_PWM, "pclk_pwm", "pclk", PCLK_GATE, 7),
	GATE_BUS(PCLK_RTC, "pclk_rtc", "pclk", PCLK_GATE, 6),
	GATE_BUS(PCLK_WDT, "pclk_wdt", "pclk", PCLK_GATE, 5),
	GATE_BUS(PCLK_UART3, "pclk_uart3", "pclk", PCLK_GATE, 4),
	GATE_BUS(PCLK_UART2, "pclk_uart2", "pclk", PCLK_GATE, 3),
	GATE_BUS(PCLK_UART1, "pclk_uart1", "pclk", PCLK_GATE, 2),
	GATE_BUS(PCLK_UART0, "pclk_uart0", "pclk", PCLK_GATE, 1),
	GATE_BUS(PCLK_MFC, "pclk_mfc", "pclk", PCLK_GATE, 0),
	GATE_SCLK(SCLK_UHOST, "sclk_uhost", "dout_uhost", SCLK_GATE, 30),
	GATE_SCLK(SCLK_MMC2_48, "sclk_mmc2_48", "clk48m", SCLK_GATE, 29),
	GATE_SCLK(SCLK_MMC1_48, "sclk_mmc1_48", "clk48m", SCLK_GATE, 28),
	GATE_SCLK(SCLK_MMC0_48, "sclk_mmc0_48", "clk48m", SCLK_GATE, 27),
	GATE_SCLK(SCLK_MMC2, "sclk_mmc2", "dout_mmc2", SCLK_GATE, 26),
	GATE_SCLK(SCLK_MMC1, "sclk_mmc1", "dout_mmc1", SCLK_GATE, 25),
	GATE_SCLK(SCLK_MMC0, "sclk_mmc0", "dout_mmc0", SCLK_GATE, 24),
	GATE_SCLK(SCLK_SPI1_48, "sclk_spi1_48", "clk48m", SCLK_GATE, 23),
	GATE_SCLK(SCLK_SPI0_48, "sclk_spi0_48", "clk48m", SCLK_GATE, 22),
	GATE_SCLK(SCLK_SPI1, "sclk_spi1", "dout_spi1", SCLK_GATE, 21),
	GATE_SCLK(SCLK_SPI0, "sclk_spi0", "dout_spi0", SCLK_GATE, 20),
	GATE_SCLK(SCLK_DAC27, "sclk_dac27", "mout_dac27", SCLK_GATE, 19),
	GATE_SCLK(SCLK_TV27, "sclk_tv27", "mout_tv27", SCLK_GATE, 18),
	GATE_SCLK(SCLK_SCALER27, "sclk_scaler27", "clk27m", SCLK_GATE, 17),
	GATE_SCLK(SCLK_SCALER, "sclk_scaler", "dout_scaler", SCLK_GATE, 16),
	GATE_SCLK(SCLK_LCD27, "sclk_lcd27", "clk27m", SCLK_GATE, 15),
	GATE_SCLK(SCLK_LCD, "sclk_lcd", "dout_lcd", SCLK_GATE, 14),
	GATE_SCLK(SCLK_POST0_27, "sclk_post0_27", "clk27m", SCLK_GATE, 12),
	GATE_SCLK(SCLK_POST0, "sclk_post0", "dout_lcd", SCLK_GATE, 10),
	GATE_SCLK(SCLK_AUDIO1, "sclk_audio1", "dout_audio1", SCLK_GATE, 9),
	GATE_SCLK(SCLK_AUDIO0, "sclk_audio0", "dout_audio0", SCLK_GATE, 8),
	GATE_SCLK(SCLK_SECUR, "sclk_secur", "dout_secur", SCLK_GATE, 7),
	GATE_SCLK(SCLK_IRDA, "sclk_irda", "dout_irda", SCLK_GATE, 6),
	GATE_SCLK(SCLK_UART, "sclk_uart", "dout_uart", SCLK_GATE, 5),
	GATE_SCLK(SCLK_MFC, "sclk_mfc", "dout_mfc", SCLK_GATE, 3),
	GATE_SCLK(SCLK_CAM, "sclk_cam", "dout_cam", SCLK_GATE, 2),
	GATE_SCLK(SCLK_JPEG, "sclk_jpeg", "dout_jpeg", SCLK_GATE, 1),
};

/* List of clock gates present on S3C6400. */
GATE_CLOCKS(s3c6400_gate_clks) __initdata = {
	GATE_ON(HCLK_DDR0, "hclk_ddr0", "hclk", HCLK_GATE, 23),
	GATE_SCLK(SCLK_ONENAND, "sclk_onenand", "parent", SCLK_GATE, 4),
};

/* List of clock gates present on S3C6410. */
GATE_CLOCKS(s3c6410_gate_clks) __initdata = {
	GATE_BUS(HCLK_3DSE, "hclk_3dse", "hclk", HCLK_GATE, 31),
	GATE_ON(HCLK_IROM, "hclk_irom", "hclk", HCLK_GATE, 25),
	GATE_ON(HCLK_MEM1, "hclk_mem1", "hclk", HCLK_GATE, 22),
	GATE_ON(HCLK_MEM0, "hclk_mem0", "hclk", HCLK_GATE, 21),
	GATE_BUS(HCLK_MFC, "hclk_mfc", "hclk", HCLK_GATE, 0),
	GATE_BUS(PCLK_IIC1, "pclk_iic1", "pclk", PCLK_GATE, 27),
	GATE_BUS(PCLK_IIS2, "pclk_iis2", "pclk", PCLK_GATE, 26),
	GATE_SCLK(SCLK_FIMC, "sclk_fimc", "dout_fimc", SCLK_GATE, 13),
	GATE_SCLK(SCLK_AUDIO2, "sclk_audio2", "dout_audio2", SCLK_GATE, 11),
	GATE_BUS(MEM0_CFCON, "mem0_cfcon", "hclk_mem0", MEM0_GATE, 5),
	GATE_BUS(MEM0_ONENAND1, "mem0_onenand1", "hclk_mem0", MEM0_GATE, 4),
	GATE_BUS(MEM0_ONENAND0, "mem0_onenand0", "hclk_mem0", MEM0_GATE, 3),
	GATE_BUS(MEM0_NFCON, "mem0_nfcon", "hclk_mem0", MEM0_GATE, 2),
	GATE_ON(MEM0_SROM, "mem0_srom", "hclk_mem0", MEM0_GATE, 1),
};

/* List of PLL clocks. */
static struct samsung_pll_clock s3c64xx_pll_clks[] __initdata = {
	[apll] = PLL(pll_6552, FOUT_APLL, "fout_apll", "fin_pll",
						APLL_LOCK, APLL_CON, NULL),
	[mpll] = PLL(pll_6552, FOUT_MPLL, "fout_mpll", "fin_pll",
						MPLL_LOCK, MPLL_CON, NULL),
	[epll] = PLL(pll_6553, FOUT_EPLL, "fout_epll", "fin_pll",
						EPLL_LOCK, EPLL_CON0, NULL),
};

/* Aliases for common s3c64xx clocks. */
static struct samsung_clock_alias s3c64xx_clock_aliases[] = {
	ALIAS(FOUT_APLL, NULL, "fout_apll"),
	ALIAS(FOUT_MPLL, NULL, "fout_mpll"),
	ALIAS(FOUT_EPLL, NULL, "fout_epll"),
	ALIAS(MOUT_EPLL, NULL, "mout_epll"),
	ALIAS(DOUT_MPLL, NULL, "dout_mpll"),
	ALIAS(HCLKX2, NULL, "hclk2"),
	ALIAS(HCLK, NULL, "hclk"),
	ALIAS(PCLK, NULL, "pclk"),
	ALIAS(PCLK, NULL, "clk_uart_baud2"),
	ALIAS(ARMCLK, NULL, "armclk"),
	ALIAS(HCLK_UHOST, "s3c2410-ohci", "usb-host"),
	ALIAS(HCLK_USB, "s3c-hsotg", "otg"),
	ALIAS(HCLK_HSMMC2, "s3c-sdhci.2", "hsmmc"),
	ALIAS(HCLK_HSMMC2, "s3c-sdhci.2", "mmc_busclk.0"),
	ALIAS(HCLK_HSMMC1, "s3c-sdhci.1", "hsmmc"),
	ALIAS(HCLK_HSMMC1, "s3c-sdhci.1", "mmc_busclk.0"),
	ALIAS(HCLK_HSMMC0, "s3c-sdhci.0", "hsmmc"),
	ALIAS(HCLK_HSMMC0, "s3c-sdhci.0", "mmc_busclk.0"),
	ALIAS(HCLK_DMA1, "dma-pl080s.1", "apb_pclk"),
	ALIAS(HCLK_DMA0, "dma-pl080s.0", "apb_pclk"),
	ALIAS(HCLK_CAMIF, "s3c-camif", "camif"),
	ALIAS(HCLK_LCD, "s3c-fb", "lcd"),
	ALIAS(PCLK_SPI1, "s3c6410-spi.1", "spi"),
	ALIAS(PCLK_SPI0, "s3c6410-spi.0", "spi"),
	ALIAS(PCLK_IIC0, "s3c2440-i2c.0", "i2c"),
	ALIAS(PCLK_IIS1, "samsung-i2s.1", "iis"),
	ALIAS(PCLK_IIS0, "samsung-i2s.0", "iis"),
	ALIAS(PCLK_AC97, "samsung-ac97", "ac97"),
	ALIAS(PCLK_TSADC, "s3c64xx-adc", "adc"),
	ALIAS(PCLK_KEYPAD, "samsung-keypad", "keypad"),
	ALIAS(PCLK_PCM1, "samsung-pcm.1", "pcm"),
	ALIAS(PCLK_PCM0, "samsung-pcm.0", "pcm"),
	ALIAS(PCLK_PWM, NULL, "timers"),
	ALIAS(PCLK_RTC, "s3c64xx-rtc", "rtc"),
	ALIAS(PCLK_WDT, NULL, "watchdog"),
	ALIAS(PCLK_UART3, "s3c6400-uart.3", "uart"),
	ALIAS(PCLK_UART2, "s3c6400-uart.2", "uart"),
	ALIAS(PCLK_UART1, "s3c6400-uart.1", "uart"),
	ALIAS(PCLK_UART0, "s3c6400-uart.0", "uart"),
	ALIAS(SCLK_UHOST, "s3c2410-ohci", "usb-bus-host"),
	ALIAS(SCLK_MMC2, "s3c-sdhci.2", "mmc_busclk.2"),
	ALIAS(SCLK_MMC1, "s3c-sdhci.1", "mmc_busclk.2"),
	ALIAS(SCLK_MMC0, "s3c-sdhci.0", "mmc_busclk.2"),
	ALIAS(PCLK_SPI1, "s3c6410-spi.1", "spi_busclk0"),
	ALIAS(SCLK_SPI1, "s3c6410-spi.1", "spi_busclk2"),
	ALIAS(PCLK_SPI0, "s3c6410-spi.0", "spi_busclk0"),
	ALIAS(SCLK_SPI0, "s3c6410-spi.0", "spi_busclk2"),
	ALIAS(SCLK_AUDIO1, "samsung-pcm.1", "audio-bus"),
	ALIAS(SCLK_AUDIO1, "samsung-i2s.1", "audio-bus"),
	ALIAS(SCLK_AUDIO0, "samsung-pcm.0", "audio-bus"),
	ALIAS(SCLK_AUDIO0, "samsung-i2s.0", "audio-bus"),
	ALIAS(SCLK_UART, NULL, "clk_uart_baud3"),
	ALIAS(SCLK_CAM, "s3c-camif", "camera"),
};

/* Aliases for s3c6400-specific clocks. */
static struct samsung_clock_alias s3c6400_clock_aliases[] = {
	/* Nothing to place here yet. */
};

/* Aliases for s3c6410-specific clocks. */
static struct samsung_clock_alias s3c6410_clock_aliases[] = {
	ALIAS(PCLK_IIC1, "s3c2440-i2c.1", "i2c"),
	ALIAS(PCLK_IIS2, "samsung-i2s.2", "iis"),
	ALIAS(SCLK_FIMC, "s3c-camif", "fimc"),
	ALIAS(SCLK_AUDIO2, "samsung-i2s.2", "audio-bus"),
	ALIAS(MEM0_SROM, NULL, "srom"),
};

static void __init s3c64xx_clk_register_fixed_ext(
				struct samsung_clk_provider *ctx,
				unsigned long fin_pll_f,
				unsigned long xusbxti_f)
{
	s3c64xx_fixed_rate_ext_clks[0].fixed_rate = fin_pll_f;
	s3c64xx_fixed_rate_ext_clks[1].fixed_rate = xusbxti_f;
	samsung_clk_register_fixed_rate(ctx, s3c64xx_fixed_rate_ext_clks,
				ARRAY_SIZE(s3c64xx_fixed_rate_ext_clks));
}

/* Register s3c64xx clocks. */
void __init s3c64xx_clk_init(struct device_node *np, unsigned long xtal_f,
			     unsigned long xusbxti_f, bool s3c6400,
			     void __iomem *base)
{
	struct samsung_clk_provider *ctx;

	reg_base = base;
	is_s3c6400 = s3c6400;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	}

	ctx = samsung_clk_init(np, reg_base, NR_CLKS);
	if (!ctx)
		panic("%s: unable to allocate context.\n", __func__);

	/* Register external clocks. */
	if (!np)
		s3c64xx_clk_register_fixed_ext(ctx, xtal_f, xusbxti_f);

	/* Register PLLs. */
	samsung_clk_register_pll(ctx, s3c64xx_pll_clks,
				ARRAY_SIZE(s3c64xx_pll_clks), reg_base);

	/* Register common internal clocks. */
	samsung_clk_register_fixed_rate(ctx, s3c64xx_fixed_rate_clks,
					ARRAY_SIZE(s3c64xx_fixed_rate_clks));
	samsung_clk_register_mux(ctx, s3c64xx_mux_clks,
					ARRAY_SIZE(s3c64xx_mux_clks));
	samsung_clk_register_div(ctx, s3c64xx_div_clks,
					ARRAY_SIZE(s3c64xx_div_clks));
	samsung_clk_register_gate(ctx, s3c64xx_gate_clks,
					ARRAY_SIZE(s3c64xx_gate_clks));

	/* Register SoC-specific clocks. */
	if (is_s3c6400) {
		samsung_clk_register_mux(ctx, s3c6400_mux_clks,
					ARRAY_SIZE(s3c6400_mux_clks));
		samsung_clk_register_div(ctx, s3c6400_div_clks,
					ARRAY_SIZE(s3c6400_div_clks));
		samsung_clk_register_gate(ctx, s3c6400_gate_clks,
					ARRAY_SIZE(s3c6400_gate_clks));
		samsung_clk_register_alias(ctx, s3c6400_clock_aliases,
					ARRAY_SIZE(s3c6400_clock_aliases));
	} else {
		samsung_clk_register_mux(ctx, s3c6410_mux_clks,
					ARRAY_SIZE(s3c6410_mux_clks));
		samsung_clk_register_div(ctx, s3c6410_div_clks,
					ARRAY_SIZE(s3c6410_div_clks));
		samsung_clk_register_gate(ctx, s3c6410_gate_clks,
					ARRAY_SIZE(s3c6410_gate_clks));
		samsung_clk_register_alias(ctx, s3c6410_clock_aliases,
					ARRAY_SIZE(s3c6410_clock_aliases));
	}

	samsung_clk_register_alias(ctx, s3c64xx_clock_aliases,
					ARRAY_SIZE(s3c64xx_clock_aliases));
	s3c64xx_clk_sleep_init();

	samsung_clk_of_add_provider(np, ctx);

	pr_info("%s clocks: apll = %lu, mpll = %lu\n"
		"\tepll = %lu, arm_clk = %lu\n",
		is_s3c6400 ? "S3C6400" : "S3C6410",
		_get_rate("fout_apll"),	_get_rate("fout_mpll"),
		_get_rate("fout_epll"), _get_rate("armclk"));
}

static void __init s3c6400_clk_init(struct device_node *np)
{
	s3c64xx_clk_init(np, 0, 0, true, NULL);
}
CLK_OF_DECLARE(s3c6400_clk, "samsung,s3c6400-clock", s3c6400_clk_init);

static void __init s3c6410_clk_init(struct device_node *np)
{
	s3c64xx_clk_init(np, 0, 0, false, NULL);
}
CLK_OF_DECLARE(s3c6410_clk, "samsung,s3c6410-clock", s3c6410_clk_init);
