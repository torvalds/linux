/*
 * arch/arm/mach-spear3xx/spear320.c
 *
 * SPEAr320 machine source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr320: " fmt

#include <linux/amba/pl022.h>
#include <linux/amba/pl08x.h>
#include <linux/amba/serial.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <plat/shirq.h>
#include <mach/generic.h>
#include <mach/spear.h>

#define SPEAR320_UART1_BASE		UL(0xA3000000)
#define SPEAR320_UART2_BASE		UL(0xA4000000)
#define SPEAR320_SSP0_BASE		UL(0xA5000000)
#define SPEAR320_SSP1_BASE		UL(0xA6000000)
#define SPEAR320_SOC_CONFIG_BASE	UL(0xB3000000)

/* Interrupt registers offsets and masks */
#define SPEAR320_INT_STS_MASK_REG		0x04
#define SPEAR320_INT_CLR_MASK_REG		0x04
#define SPEAR320_INT_ENB_MASK_REG		0x08
#define SPEAR320_GPIO_IRQ_MASK			(1 << 0)
#define SPEAR320_I2S_PLAY_IRQ_MASK		(1 << 1)
#define SPEAR320_I2S_REC_IRQ_MASK		(1 << 2)
#define SPEAR320_EMI_IRQ_MASK			(1 << 7)
#define SPEAR320_CLCD_IRQ_MASK			(1 << 8)
#define SPEAR320_SPP_IRQ_MASK			(1 << 9)
#define SPEAR320_SDHCI_IRQ_MASK			(1 << 10)
#define SPEAR320_CAN_U_IRQ_MASK			(1 << 11)
#define SPEAR320_CAN_L_IRQ_MASK			(1 << 12)
#define SPEAR320_UART1_IRQ_MASK			(1 << 13)
#define SPEAR320_UART2_IRQ_MASK			(1 << 14)
#define SPEAR320_SSP1_IRQ_MASK			(1 << 15)
#define SPEAR320_SSP2_IRQ_MASK			(1 << 16)
#define SPEAR320_SMII0_IRQ_MASK			(1 << 17)
#define SPEAR320_MII1_SMII1_IRQ_MASK		(1 << 18)
#define SPEAR320_WAKEUP_SMII0_IRQ_MASK		(1 << 19)
#define SPEAR320_WAKEUP_MII1_SMII1_IRQ_MASK	(1 << 20)
#define SPEAR320_I2C1_IRQ_MASK			(1 << 21)

#define SPEAR320_SHIRQ_RAS1_MASK		0x000380
#define SPEAR320_SHIRQ_RAS3_MASK		0x000007
#define SPEAR320_SHIRQ_INTRCOMM_RAS_MASK	0x3FF800

/* SPEAr320 Virtual irq definitions */
/* IRQs sharing IRQ_GEN_RAS_1 */
#define SPEAR320_VIRQ_EMI			(SPEAR3XX_VIRQ_START + 0)
#define SPEAR320_VIRQ_CLCD			(SPEAR3XX_VIRQ_START + 1)
#define SPEAR320_VIRQ_SPP			(SPEAR3XX_VIRQ_START + 2)

/* IRQs sharing IRQ_GEN_RAS_2 */
#define SPEAR320_IRQ_SDHCI			SPEAR3XX_IRQ_GEN_RAS_2

/* IRQs sharing IRQ_GEN_RAS_3 */
#define SPEAR320_VIRQ_PLGPIO			(SPEAR3XX_VIRQ_START + 3)
#define SPEAR320_VIRQ_I2S_PLAY			(SPEAR3XX_VIRQ_START + 4)
#define SPEAR320_VIRQ_I2S_REC			(SPEAR3XX_VIRQ_START + 5)

/* IRQs sharing IRQ_INTRCOMM_RAS_ARM */
#define SPEAR320_VIRQ_CANU			(SPEAR3XX_VIRQ_START + 6)
#define SPEAR320_VIRQ_CANL			(SPEAR3XX_VIRQ_START + 7)
#define SPEAR320_VIRQ_UART1			(SPEAR3XX_VIRQ_START + 8)
#define SPEAR320_VIRQ_UART2			(SPEAR3XX_VIRQ_START + 9)
#define SPEAR320_VIRQ_SSP1			(SPEAR3XX_VIRQ_START + 10)
#define SPEAR320_VIRQ_SSP2			(SPEAR3XX_VIRQ_START + 11)
#define SPEAR320_VIRQ_SMII0			(SPEAR3XX_VIRQ_START + 12)
#define SPEAR320_VIRQ_MII1_SMII1		(SPEAR3XX_VIRQ_START + 13)
#define SPEAR320_VIRQ_WAKEUP_SMII0		(SPEAR3XX_VIRQ_START + 14)
#define SPEAR320_VIRQ_WAKEUP_MII1_SMII1		(SPEAR3XX_VIRQ_START + 15)
#define SPEAR320_VIRQ_I2C1			(SPEAR3XX_VIRQ_START + 16)

/* spear3xx shared irq */
static struct shirq_dev_config shirq_ras1_config[] = {
	{
		.virq = SPEAR320_VIRQ_EMI,
		.status_mask = SPEAR320_EMI_IRQ_MASK,
		.clear_mask = SPEAR320_EMI_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_CLCD,
		.status_mask = SPEAR320_CLCD_IRQ_MASK,
		.clear_mask = SPEAR320_CLCD_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_SPP,
		.status_mask = SPEAR320_SPP_IRQ_MASK,
		.clear_mask = SPEAR320_SPP_IRQ_MASK,
	},
};

static struct spear_shirq shirq_ras1 = {
	.irq = SPEAR3XX_IRQ_GEN_RAS_1,
	.dev_config = shirq_ras1_config,
	.dev_count = ARRAY_SIZE(shirq_ras1_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = SPEAR320_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR320_SHIRQ_RAS1_MASK,
		.clear_reg = SPEAR320_INT_CLR_MASK_REG,
		.reset_to_clear = 1,
	},
};

static struct shirq_dev_config shirq_ras3_config[] = {
	{
		.virq = SPEAR320_VIRQ_PLGPIO,
		.enb_mask = SPEAR320_GPIO_IRQ_MASK,
		.status_mask = SPEAR320_GPIO_IRQ_MASK,
		.clear_mask = SPEAR320_GPIO_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_I2S_PLAY,
		.enb_mask = SPEAR320_I2S_PLAY_IRQ_MASK,
		.status_mask = SPEAR320_I2S_PLAY_IRQ_MASK,
		.clear_mask = SPEAR320_I2S_PLAY_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_I2S_REC,
		.enb_mask = SPEAR320_I2S_REC_IRQ_MASK,
		.status_mask = SPEAR320_I2S_REC_IRQ_MASK,
		.clear_mask = SPEAR320_I2S_REC_IRQ_MASK,
	},
};

static struct spear_shirq shirq_ras3 = {
	.irq = SPEAR3XX_IRQ_GEN_RAS_3,
	.dev_config = shirq_ras3_config,
	.dev_count = ARRAY_SIZE(shirq_ras3_config),
	.regs = {
		.enb_reg = SPEAR320_INT_ENB_MASK_REG,
		.reset_to_enb = 1,
		.status_reg = SPEAR320_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR320_SHIRQ_RAS3_MASK,
		.clear_reg = SPEAR320_INT_CLR_MASK_REG,
		.reset_to_clear = 1,
	},
};

static struct shirq_dev_config shirq_intrcomm_ras_config[] = {
	{
		.virq = SPEAR320_VIRQ_CANU,
		.status_mask = SPEAR320_CAN_U_IRQ_MASK,
		.clear_mask = SPEAR320_CAN_U_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_CANL,
		.status_mask = SPEAR320_CAN_L_IRQ_MASK,
		.clear_mask = SPEAR320_CAN_L_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_UART1,
		.status_mask = SPEAR320_UART1_IRQ_MASK,
		.clear_mask = SPEAR320_UART1_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_UART2,
		.status_mask = SPEAR320_UART2_IRQ_MASK,
		.clear_mask = SPEAR320_UART2_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_SSP1,
		.status_mask = SPEAR320_SSP1_IRQ_MASK,
		.clear_mask = SPEAR320_SSP1_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_SSP2,
		.status_mask = SPEAR320_SSP2_IRQ_MASK,
		.clear_mask = SPEAR320_SSP2_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_SMII0,
		.status_mask = SPEAR320_SMII0_IRQ_MASK,
		.clear_mask = SPEAR320_SMII0_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_MII1_SMII1,
		.status_mask = SPEAR320_MII1_SMII1_IRQ_MASK,
		.clear_mask = SPEAR320_MII1_SMII1_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_WAKEUP_SMII0,
		.status_mask = SPEAR320_WAKEUP_SMII0_IRQ_MASK,
		.clear_mask = SPEAR320_WAKEUP_SMII0_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_WAKEUP_MII1_SMII1,
		.status_mask = SPEAR320_WAKEUP_MII1_SMII1_IRQ_MASK,
		.clear_mask = SPEAR320_WAKEUP_MII1_SMII1_IRQ_MASK,
	}, {
		.virq = SPEAR320_VIRQ_I2C1,
		.status_mask = SPEAR320_I2C1_IRQ_MASK,
		.clear_mask = SPEAR320_I2C1_IRQ_MASK,
	},
};

static struct spear_shirq shirq_intrcomm_ras = {
	.irq = SPEAR3XX_IRQ_INTRCOMM_RAS_ARM,
	.dev_config = shirq_intrcomm_ras_config,
	.dev_count = ARRAY_SIZE(shirq_intrcomm_ras_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = SPEAR320_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR320_SHIRQ_INTRCOMM_RAS_MASK,
		.clear_reg = SPEAR320_INT_CLR_MASK_REG,
		.reset_to_clear = 1,
	},
};

/* DMAC platform data's slave info */
struct pl08x_channel_data spear320_dma_info[] = {
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
		.bus_id = "i2c0_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 0,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "i2c0_tx",
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
		.bus_id = "ssp1_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp1_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp2_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp2_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart1_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart1_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart2_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart2_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c1_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c1_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c2_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c2_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s_rx",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s_tx",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "rs485_rx",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "rs485_tx",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB2,
	},
};

static struct pl022_ssp_controller spear320_ssp_data[] = {
	{
		.bus_id = 1,
		.enable_dma = 1,
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "ssp1_tx",
		.dma_rx_param = "ssp1_rx",
		.num_chipselect = 2,
	}, {
		.bus_id = 2,
		.enable_dma = 1,
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "ssp2_tx",
		.dma_rx_param = "ssp2_rx",
		.num_chipselect = 2,
	}
};

static struct amba_pl011_data spear320_uart_data[] = {
	{
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart1_tx",
		.dma_rx_param = "uart1_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart2_tx",
		.dma_rx_param = "uart2_rx",
	},
};

/* Add SPEAr310 auxdata to pass platform data */
static struct of_dev_auxdata spear320_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl022", SPEAR3XX_ICM1_SSP_BASE, NULL,
			&pl022_plat_data),
	OF_DEV_AUXDATA("arm,pl080", SPEAR3XX_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	OF_DEV_AUXDATA("arm,pl022", SPEAR320_SSP0_BASE, NULL,
			&spear320_ssp_data[0]),
	OF_DEV_AUXDATA("arm,pl022", SPEAR320_SSP1_BASE, NULL,
			&spear320_ssp_data[1]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR320_UART1_BASE, NULL,
			&spear320_uart_data[0]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR320_UART2_BASE, NULL,
			&spear320_uart_data[1]),
	{}
};

static void __init spear320_dt_init(void)
{
	void __iomem *base;
	int ret;

	pl080_plat_data.slave_channels = spear320_dma_info;
	pl080_plat_data.num_slave_channels = ARRAY_SIZE(spear320_dma_info);

	of_platform_populate(NULL, of_default_bus_match_table,
			spear320_auxdata_lookup, NULL);

	/* shared irq registration */
	base = ioremap(SPEAR320_SOC_CONFIG_BASE, SZ_4K);
	if (base) {
		/* shirq 1 */
		shirq_ras1.regs.base = base;
		ret = spear_shirq_register(&shirq_ras1);
		if (ret)
			pr_err("Error registering Shared IRQ 1\n");

		/* shirq 3 */
		shirq_ras3.regs.base = base;
		ret = spear_shirq_register(&shirq_ras3);
		if (ret)
			pr_err("Error registering Shared IRQ 3\n");

		/* shirq 4 */
		shirq_intrcomm_ras.regs.base = base;
		ret = spear_shirq_register(&shirq_intrcomm_ras);
		if (ret)
			pr_err("Error registering Shared IRQ 4\n");
	}
}

static const char * const spear320_dt_board_compat[] = {
	"st,spear320",
	"st,spear320-evb",
	NULL,
};

static void __init spear320_map_io(void)
{
	spear3xx_map_io();
	spear320_clk_init();
}

DT_MACHINE_START(SPEAR320_DT, "ST SPEAr320 SoC with Flattened Device Tree")
	.map_io		=	spear320_map_io,
	.init_irq	=	spear3xx_dt_init_irq,
	.handle_irq	=	vic_handle_irq,
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear320_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear320_dt_board_compat,
MACHINE_END
