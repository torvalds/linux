/*
 * arch/arm/mach-spear3xx/spear310.c
 *
 * SPEAr310 machine source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr310: " fmt

#include <linux/amba/pl08x.h>
#include <linux/amba/serial.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <plat/shirq.h>
#include <mach/generic.h>
#include <mach/hardware.h>

/* spear3xx shared irq */
static struct shirq_dev_config shirq_ras1_config[] = {
	{
		.virq = SPEAR310_VIRQ_SMII0,
		.status_mask = SPEAR310_SMII0_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_SMII1,
		.status_mask = SPEAR310_SMII1_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_SMII2,
		.status_mask = SPEAR310_SMII2_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_SMII3,
		.status_mask = SPEAR310_SMII3_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_WAKEUP_SMII0,
		.status_mask = SPEAR310_WAKEUP_SMII0_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_WAKEUP_SMII1,
		.status_mask = SPEAR310_WAKEUP_SMII1_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_WAKEUP_SMII2,
		.status_mask = SPEAR310_WAKEUP_SMII2_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_WAKEUP_SMII3,
		.status_mask = SPEAR310_WAKEUP_SMII3_IRQ_MASK,
	},
};

static struct spear_shirq shirq_ras1 = {
	.irq = SPEAR3XX_IRQ_GEN_RAS_1,
	.dev_config = shirq_ras1_config,
	.dev_count = ARRAY_SIZE(shirq_ras1_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = SPEAR310_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR310_SHIRQ_RAS1_MASK,
		.clear_reg = -1,
	},
};

static struct shirq_dev_config shirq_ras2_config[] = {
	{
		.virq = SPEAR310_VIRQ_UART1,
		.status_mask = SPEAR310_UART1_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_UART2,
		.status_mask = SPEAR310_UART2_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_UART3,
		.status_mask = SPEAR310_UART3_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_UART4,
		.status_mask = SPEAR310_UART4_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_UART5,
		.status_mask = SPEAR310_UART5_IRQ_MASK,
	},
};

static struct spear_shirq shirq_ras2 = {
	.irq = SPEAR3XX_IRQ_GEN_RAS_2,
	.dev_config = shirq_ras2_config,
	.dev_count = ARRAY_SIZE(shirq_ras2_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = SPEAR310_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR310_SHIRQ_RAS2_MASK,
		.clear_reg = -1,
	},
};

static struct shirq_dev_config shirq_ras3_config[] = {
	{
		.virq = SPEAR310_VIRQ_EMI,
		.status_mask = SPEAR310_EMI_IRQ_MASK,
	},
};

static struct spear_shirq shirq_ras3 = {
	.irq = SPEAR3XX_IRQ_GEN_RAS_3,
	.dev_config = shirq_ras3_config,
	.dev_count = ARRAY_SIZE(shirq_ras3_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = SPEAR310_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR310_SHIRQ_RAS3_MASK,
		.clear_reg = -1,
	},
};

static struct shirq_dev_config shirq_intrcomm_ras_config[] = {
	{
		.virq = SPEAR310_VIRQ_TDM_HDLC,
		.status_mask = SPEAR310_TDM_HDLC_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_RS485_0,
		.status_mask = SPEAR310_RS485_0_IRQ_MASK,
	}, {
		.virq = SPEAR310_VIRQ_RS485_1,
		.status_mask = SPEAR310_RS485_1_IRQ_MASK,
	},
};

static struct spear_shirq shirq_intrcomm_ras = {
	.irq = SPEAR3XX_IRQ_INTRCOMM_RAS_ARM,
	.dev_config = shirq_intrcomm_ras_config,
	.dev_count = ARRAY_SIZE(shirq_intrcomm_ras_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = SPEAR310_INT_STS_MASK_REG,
		.status_reg_mask = SPEAR310_SHIRQ_INTRCOMM_RAS_MASK,
		.clear_reg = -1,
	},
};

/* DMAC platform data's slave info */
struct pl08x_channel_data spear310_dma_info[] = {
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
		.bus_id = "uart1_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart1_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart2_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart2_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart3_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart3_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart4_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart4_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart5_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 1,
		.cctl = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart5_tx",
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

/* uart devices plat data */
static struct amba_pl011_data spear310_uart_data[] = {
	{
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart1_tx",
		.dma_rx_param = "uart1_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart2_tx",
		.dma_rx_param = "uart2_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart3_tx",
		.dma_rx_param = "uart3_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart4_tx",
		.dma_rx_param = "uart4_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart5_tx",
		.dma_rx_param = "uart5_rx",
	},
};

/* Add SPEAr310 auxdata to pass platform data */
static struct of_dev_auxdata spear310_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl022", SPEAR3XX_ICM1_SSP_BASE, NULL,
			&pl022_plat_data),
	OF_DEV_AUXDATA("arm,pl080", SPEAR3XX_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART1_BASE, NULL,
			&spear310_uart_data[0]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART2_BASE, NULL,
			&spear310_uart_data[1]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART3_BASE, NULL,
			&spear310_uart_data[2]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART4_BASE, NULL,
			&spear310_uart_data[3]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART5_BASE, NULL,
			&spear310_uart_data[4]),
	{}
};

static void __init spear310_dt_init(void)
{
	void __iomem *base;
	int ret;

	pl080_plat_data.slave_channels = spear310_dma_info;
	pl080_plat_data.num_slave_channels = ARRAY_SIZE(spear310_dma_info);

	of_platform_populate(NULL, of_default_bus_match_table,
			spear310_auxdata_lookup, NULL);

	/* shared irq registration */
	base = ioremap(SPEAR310_SOC_CONFIG_BASE, SZ_4K);
	if (base) {
		/* shirq 1 */
		shirq_ras1.regs.base = base;
		ret = spear_shirq_register(&shirq_ras1);
		if (ret)
			pr_err("Error registering Shared IRQ 1\n");

		/* shirq 2 */
		shirq_ras2.regs.base = base;
		ret = spear_shirq_register(&shirq_ras2);
		if (ret)
			pr_err("Error registering Shared IRQ 2\n");

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

static const char * const spear310_dt_board_compat[] = {
	"st,spear310",
	"st,spear310-evb",
	NULL,
};

static void __init spear310_map_io(void)
{
	spear3xx_map_io();
	spear310_clk_init();
}

DT_MACHINE_START(SPEAR310_DT, "ST SPEAr310 SoC with Flattened Device Tree")
	.map_io		=	spear310_map_io,
	.init_irq	=	spear3xx_dt_init_irq,
	.handle_irq	=	vic_handle_irq,
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear310_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear310_dt_board_compat,
MACHINE_END
