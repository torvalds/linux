/*
 * arch/arm/mach-spear3xx/spear320.c
 *
 * SPEAr320 machine source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/ptrace.h>
#include <asm/irq.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* pad multiplexing support */
/* muxing registers */
#define PAD_MUX_CONFIG_REG	0x0C
#define MODE_CONFIG_REG		0x10

/* modes */
#define AUTO_NET_SMII_MODE	(1 << 0)
#define AUTO_NET_MII_MODE	(1 << 1)
#define AUTO_EXP_MODE		(1 << 2)
#define SMALL_PRINTERS_MODE	(1 << 3)
#define ALL_MODES		0xF

struct pmx_mode auto_net_smii_mode = {
	.id = AUTO_NET_SMII_MODE,
	.name = "Automation Networking SMII Mode",
	.mask = 0x00,
};

struct pmx_mode auto_net_mii_mode = {
	.id = AUTO_NET_MII_MODE,
	.name = "Automation Networking MII Mode",
	.mask = 0x01,
};

struct pmx_mode auto_exp_mode = {
	.id = AUTO_EXP_MODE,
	.name = "Automation Expanded Mode",
	.mask = 0x02,
};

struct pmx_mode small_printers_mode = {
	.id = SMALL_PRINTERS_MODE,
	.name = "Small Printers Mode",
	.mask = 0x03,
};

/* devices */
struct pmx_dev_mode pmx_clcd_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev pmx_clcd = {
	.name = "clcd",
	.modes = pmx_clcd_modes,
	.mode_count = ARRAY_SIZE(pmx_clcd_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_emi_modes[] = {
	{
		.ids = AUTO_EXP_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev pmx_emi = {
	.name = "emi",
	.modes = pmx_emi_modes,
	.mode_count = ARRAY_SIZE(pmx_emi_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_fsmc_modes[] = {
	{
		.ids = ALL_MODES,
		.mask = 0x0,
	},
};

struct pmx_dev pmx_fsmc = {
	.name = "fsmc",
	.modes = pmx_fsmc_modes,
	.mode_count = ARRAY_SIZE(pmx_fsmc_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_spp_modes[] = {
	{
		.ids = SMALL_PRINTERS_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev pmx_spp = {
	.name = "spp",
	.modes = pmx_spp_modes,
	.mode_count = ARRAY_SIZE(pmx_spp_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_sdio_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE |
			SMALL_PRINTERS_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev pmx_sdio = {
	.name = "sdio",
	.modes = pmx_sdio_modes,
	.mode_count = ARRAY_SIZE(pmx_sdio_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_i2s_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev pmx_i2s = {
	.name = "i2s",
	.modes = pmx_i2s_modes,
	.mode_count = ARRAY_SIZE(pmx_i2s_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_uart1_modes[] = {
	{
		.ids = ALL_MODES,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK,
	},
};

struct pmx_dev pmx_uart1 = {
	.name = "uart1",
	.modes = pmx_uart1_modes,
	.mode_count = ARRAY_SIZE(pmx_uart1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_uart1_modem_modes[] = {
	{
		.ids = AUTO_EXP_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK |
			PMX_SSP_CS_MASK,
	}, {
		.ids = SMALL_PRINTERS_MODE,
		.mask = PMX_GPIO_PIN3_MASK | PMX_GPIO_PIN4_MASK |
			PMX_GPIO_PIN5_MASK | PMX_SSP_CS_MASK,
	},
};

struct pmx_dev pmx_uart1_modem = {
	.name = "uart1_modem",
	.modes = pmx_uart1_modem_modes,
	.mode_count = ARRAY_SIZE(pmx_uart1_modem_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_uart2_modes[] = {
	{
		.ids = ALL_MODES,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev pmx_uart2 = {
	.name = "uart2",
	.modes = pmx_uart2_modes,
	.mode_count = ARRAY_SIZE(pmx_uart2_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_touchscreen_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev pmx_touchscreen = {
	.name = "touchscreen",
	.modes = pmx_touchscreen_modes,
	.mode_count = ARRAY_SIZE(pmx_touchscreen_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_can_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | AUTO_EXP_MODE,
		.mask = PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK |
			PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK,
	},
};

struct pmx_dev pmx_can = {
	.name = "can",
	.modes = pmx_can_modes,
	.mode_count = ARRAY_SIZE(pmx_can_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_sdio_led_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev pmx_sdio_led = {
	.name = "sdio_led",
	.modes = pmx_sdio_led_modes,
	.mode_count = ARRAY_SIZE(pmx_sdio_led_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_pwm0_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	}, {
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_pwm0 = {
	.name = "pwm0",
	.modes = pmx_pwm0_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm0_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_pwm1_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	}, {
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_pwm1 = {
	.name = "pwm1",
	.modes = pmx_pwm1_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_pwm2_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_SSP_CS_MASK,
	}, {
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_pwm2 = {
	.name = "pwm2",
	.modes = pmx_pwm2_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm2_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_pwm3_modes[] = {
	{
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE | AUTO_NET_SMII_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_pwm3 = {
	.name = "pwm3",
	.modes = pmx_pwm3_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm3_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_ssp1_modes[] = {
	{
		.ids = SMALL_PRINTERS_MODE | AUTO_NET_SMII_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_ssp1 = {
	.name = "ssp1",
	.modes = pmx_ssp1_modes,
	.mode_count = ARRAY_SIZE(pmx_ssp1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_ssp2_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_ssp2 = {
	.name = "ssp2",
	.modes = pmx_ssp2_modes,
	.mode_count = ARRAY_SIZE(pmx_ssp2_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_mii1_modes[] = {
	{
		.ids = AUTO_NET_MII_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev pmx_mii1 = {
	.name = "mii1",
	.modes = pmx_mii1_modes,
	.mode_count = ARRAY_SIZE(pmx_mii1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_smii0_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_smii0 = {
	.name = "smii0",
	.modes = pmx_smii0_modes,
	.mode_count = ARRAY_SIZE(pmx_smii0_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_smii1_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_smii1 = {
	.name = "smii1",
	.modes = pmx_smii1_modes,
	.mode_count = ARRAY_SIZE(pmx_smii1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_i2c1_modes[] = {
	{
		.ids = AUTO_EXP_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev pmx_i2c1 = {
	.name = "i2c1",
	.modes = pmx_i2c1_modes,
	.mode_count = ARRAY_SIZE(pmx_i2c1_modes),
	.enb_on_reset = 1,
};

/* pmx driver structure */
struct pmx_driver pmx_driver = {
	.mode_reg = {.offset = MODE_CONFIG_REG, .mask = 0x00000007},
	.mux_reg = {.offset = PAD_MUX_CONFIG_REG, .mask = 0x00007fff},
};

/* Add spear320 specific devices here */

/* spear320 routines */
void __init spear320_init(void)
{
	/* call spear3xx family common init function */
	spear3xx_init();
}

void spear320_pmx_init(void)
{
	spear_pmx_init(&pmx_driver, SPEAR320_SOC_CONFIG_BASE,
			SPEAR320_SOC_CONFIG_SIZE);
}
