/*
 * arch/arm/mach-spear3xx/spear300.c
 *
 * SPEAr300 machine source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr300: " fmt

#include <linux/amba/pl08x.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <plat/shirq.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* Base address of various IPs */
#define SPEAR300_TELECOM_BASE		UL(0x50000000)

/* Interrupt registers offsets and masks */
#define SPEAR300_INT_ENB_MASK_REG	0x54
#define SPEAR300_INT_STS_MASK_REG	0x58
#define SPEAR300_IT_PERS_S_IRQ_MASK	(1 << 0)
#define SPEAR300_IT_CHANGE_S_IRQ_MASK	(1 << 1)
#define SPEAR300_I2S_IRQ_MASK		(1 << 2)
#define SPEAR300_TDM_IRQ_MASK		(1 << 3)
#define SPEAR300_CAMERA_L_IRQ_MASK	(1 << 4)
#define SPEAR300_CAMERA_F_IRQ_MASK	(1 << 5)
#define SPEAR300_CAMERA_V_IRQ_MASK	(1 << 6)
#define SPEAR300_KEYBOARD_IRQ_MASK	(1 << 7)
#define SPEAR300_GPIO1_IRQ_MASK		(1 << 8)

#define SPEAR300_SHIRQ_RAS1_MASK	0x1FF

#define SPEAR300_SOC_CONFIG_BASE	UL(0x99000000)


/* SPEAr300 Virtual irq definitions */
/* IRQs sharing IRQ_GEN_RAS_1 */
#define SPEAR300_VIRQ_IT_PERS_S			(SPEAR3XX_VIRQ_START + 0)
#define SPEAR300_VIRQ_IT_CHANGE_S		(SPEAR3XX_VIRQ_START + 1)
#define SPEAR300_VIRQ_I2S			(SPEAR3XX_VIRQ_START + 2)
#define SPEAR300_VIRQ_TDM			(SPEAR3XX_VIRQ_START + 3)
#define SPEAR300_VIRQ_CAMERA_L			(SPEAR3XX_VIRQ_START + 4)
#define SPEAR300_VIRQ_CAMERA_F			(SPEAR3XX_VIRQ_START + 5)
#define SPEAR300_VIRQ_CAMERA_V			(SPEAR3XX_VIRQ_START + 6)
#define SPEAR300_VIRQ_KEYBOARD			(SPEAR3XX_VIRQ_START + 7)
#define SPEAR300_VIRQ_GPIO1			(SPEAR3XX_VIRQ_START + 8)

/* IRQs sharing IRQ_GEN_RAS_3 */
#define SPEAR300_IRQ_CLCD			SPEAR3XX_IRQ_GEN_RAS_3

/* IRQs sharing IRQ_INTRCOMM_RAS_ARM */
#define SPEAR300_IRQ_SDHCI			SPEAR3XX_IRQ_INTRCOMM_RAS_ARM

/* spear3xx shared irq */
static struct shirq_dev_config shirq_ras1_config[] = {
	{
		.virq = SPEAR300_VIRQ_IT_PERS_S,
		.enb_mask = SPEAR300_IT_PERS_S_IRQ_MASK,
		.status_mask = SPEAR300_IT_PERS_S_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_IT_CHANGE_S,
		.enb_mask = SPEAR300_IT_CHANGE_S_IRQ_MASK,
		.status_mask = SPEAR300_IT_CHANGE_S_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_I2S,
		.enb_mask = SPEAR300_I2S_IRQ_MASK,
		.status_mask = SPEAR300_I2S_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_TDM,
		.enb_mask = SPEAR300_TDM_IRQ_MASK,
		.status_mask = SPEAR300_TDM_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_CAMERA_L,
		.enb_mask = SPEAR300_CAMERA_L_IRQ_MASK,
		.status_mask = SPEAR300_CAMERA_L_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_CAMERA_F,
		.enb_mask = SPEAR300_CAMERA_F_IRQ_MASK,
		.status_mask = SPEAR300_CAMERA_F_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_CAMERA_V,
		.enb_mask = SPEAR300_CAMERA_V_IRQ_MASK,
		.status_mask = SPEAR300_CAMERA_V_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_KEYBOARD,
		.enb_mask = SPEAR300_KEYBOARD_IRQ_MASK,
		.status_mask = SPEAR300_KEYBOARD_IRQ_MASK,
	}, {
		.virq = SPEAR300_VIRQ_GPIO1,
		.enb_mask = SPEAR300_GPIO1_IRQ_MASK,
		.status_mask = SPEAR300_GPIO1_IRQ_MASK,
	},
};

static struct spear_shirq shirq_ras1 = {
	.irq = SPEAR3XX_IRQ_GEN_RAS_1,
	.dev_config = shirq_ras1_config,
	.dev_count = ARRAY_SIZE(shirq_ras1_config),
	.regs = {
		.enb_reg = SPEAR300_INT_ENB_MASK_REG,
		.status_reg = SPEAR300_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR300_SHIRQ_RAS1_MASK,
		.clear_reg = -1,
	},
};

/* DMAC platform data's slave info */
struct pl08x_channel_data spear300_dma_info[] = {
	{
		.bus_id = "uart0_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart0_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp0_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp0_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "i2c_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "i2c_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "irda",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "adc",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "to_jpeg",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "from_jpeg",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras0_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras0_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras1_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras1_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras2_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras2_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras3_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras3_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras4_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras4_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras5_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras5_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras6_rx",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras6_tx",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras7_rx",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras7_tx",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	},
};

/* Add SPEAr300 auxdata to pass platform data */
static struct of_dev_auxdata spear300_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl022", SPEAR3XX_ICM1_SSP_BASE, NULL,
			&pl022_plat_data),
	OF_DEV_AUXDATA("arm,pl080", SPEAR3XX_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	{}
};

static void __init spear300_dt_init(void)
{
	int ret;

	pl080_plat_data.slave_channels = spear300_dma_info;
	pl080_plat_data.num_slave_channels = ARRAY_SIZE(spear300_dma_info);

	of_platform_populate(NULL, of_default_bus_match_table,
			spear300_auxdata_lookup, NULL);

	/* shared irq registration */
	shirq_ras1.regs.base = ioremap(SPEAR300_TELECOM_BASE, SZ_4K);
	if (shirq_ras1.regs.base) {
		ret = spear_shirq_register(&shirq_ras1);
		if (ret)
			pr_err("Error registering Shared IRQ\n");
	}
}

static const char * const spear300_dt_board_compat[] = {
	"st,spear300",
	"st,spear300-evb",
	NULL,
};

static void __init spear300_map_io(void)
{
	spear3xx_map_io();
}

DT_MACHINE_START(SPEAR300_DT, "ST SPEAr300 SoC with Flattened Device Tree")
	.map_io		=	spear300_map_io,
	.init_irq	=	spear3xx_dt_init_irq,
	.handle_irq	=	vic_handle_irq,
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear300_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear300_dt_board_compat,
MACHINE_END
