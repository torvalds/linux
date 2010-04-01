/*
 * arch/arm/mach-spear3xx/spear310.c
 *
 * SPEAr310 machine source file
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
#define PAD_MUX_CONFIG_REG	0x08

/* devices */
struct pmx_dev_mode pmx_emi_cs_0_1_4_5_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev pmx_emi_cs_0_1_4_5 = {
	.name = "emi_cs_0_1_4_5",
	.modes = pmx_emi_cs_0_1_4_5_modes,
	.mode_count = ARRAY_SIZE(pmx_emi_cs_0_1_4_5_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_emi_cs_2_3_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev pmx_emi_cs_2_3 = {
	.name = "emi_cs_2_3",
	.modes = pmx_emi_cs_2_3_modes,
	.mode_count = ARRAY_SIZE(pmx_emi_cs_2_3_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_uart1_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev pmx_uart1 = {
	.name = "uart1",
	.modes = pmx_uart1_modes,
	.mode_count = ARRAY_SIZE(pmx_uart1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_uart2_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev pmx_uart2 = {
	.name = "uart2",
	.modes = pmx_uart2_modes,
	.mode_count = ARRAY_SIZE(pmx_uart2_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_uart3_4_5_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev pmx_uart3_4_5 = {
	.name = "uart3_4_5",
	.modes = pmx_uart3_4_5_modes,
	.mode_count = ARRAY_SIZE(pmx_uart3_4_5_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_fsmc_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev pmx_fsmc = {
	.name = "fsmc",
	.modes = pmx_fsmc_modes,
	.mode_count = ARRAY_SIZE(pmx_fsmc_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_rs485_0_1_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_rs485_0_1 = {
	.name = "rs485_0_1",
	.modes = pmx_rs485_0_1_modes,
	.mode_count = ARRAY_SIZE(pmx_rs485_0_1_modes),
	.enb_on_reset = 1,
};

struct pmx_dev_mode pmx_tdm0_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev pmx_tdm0 = {
	.name = "tdm0",
	.modes = pmx_tdm0_modes,
	.mode_count = ARRAY_SIZE(pmx_tdm0_modes),
	.enb_on_reset = 1,
};

/* pmx driver structure */
struct pmx_driver pmx_driver = {
	.mux_reg = {.offset = PAD_MUX_CONFIG_REG, .mask = 0x00007fff},
};

/* Add spear310 specific devices here */

/* spear310 routines */
void __init spear310_init(void)
{
	/* call spear3xx family common init function */
	spear3xx_init();
}

void spear310_pmx_init(void)
{
	spear_pmx_init(&pmx_driver, SPEAR310_SOC_CONFIG_BASE,
			SPEAR310_SOC_CONFIG_SIZE);
}
