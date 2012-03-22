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

/* pad multiplexing support */
/* muxing registers */
#define PAD_MUX_CONFIG_REG	0x08

/* devices */
static struct pmx_dev_mode pmx_emi_cs_0_1_4_5_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear310_pmx_emi_cs_0_1_4_5 = {
	.name = "emi_cs_0_1_4_5",
	.modes = pmx_emi_cs_0_1_4_5_modes,
	.mode_count = ARRAY_SIZE(pmx_emi_cs_0_1_4_5_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_emi_cs_2_3_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev spear310_pmx_emi_cs_2_3 = {
	.name = "emi_cs_2_3",
	.modes = pmx_emi_cs_2_3_modes,
	.mode_count = ARRAY_SIZE(pmx_emi_cs_2_3_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_uart1_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev spear310_pmx_uart1 = {
	.name = "uart1",
	.modes = pmx_uart1_modes,
	.mode_count = ARRAY_SIZE(pmx_uart1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_uart2_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev spear310_pmx_uart2 = {
	.name = "uart2",
	.modes = pmx_uart2_modes,
	.mode_count = ARRAY_SIZE(pmx_uart2_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_uart3_4_5_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev spear310_pmx_uart3_4_5 = {
	.name = "uart3_4_5",
	.modes = pmx_uart3_4_5_modes,
	.mode_count = ARRAY_SIZE(pmx_uart3_4_5_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_fsmc_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev spear310_pmx_fsmc = {
	.name = "fsmc",
	.modes = pmx_fsmc_modes,
	.mode_count = ARRAY_SIZE(pmx_fsmc_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_rs485_0_1_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear310_pmx_rs485_0_1 = {
	.name = "rs485_0_1",
	.modes = pmx_rs485_0_1_modes,
	.mode_count = ARRAY_SIZE(pmx_rs485_0_1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_tdm0_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear310_pmx_tdm0 = {
	.name = "tdm0",
	.modes = pmx_tdm0_modes,
	.mode_count = ARRAY_SIZE(pmx_tdm0_modes),
	.enb_on_reset = 1,
};

/* pmx driver structure */
static struct pmx_driver pmx_driver = {
	.mux_reg = {.offset = PAD_MUX_CONFIG_REG, .mask = 0x00007fff},
};

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

/* padmux devices to enable */
static struct pmx_dev *spear310_evb_pmx_devs[] = {
	/* spear3xx specific devices */
	&spear3xx_pmx_i2c,
	&spear3xx_pmx_ssp,
	&spear3xx_pmx_gpio_pin0,
	&spear3xx_pmx_gpio_pin1,
	&spear3xx_pmx_gpio_pin2,
	&spear3xx_pmx_gpio_pin3,
	&spear3xx_pmx_gpio_pin4,
	&spear3xx_pmx_gpio_pin5,
	&spear3xx_pmx_uart0,

	/* spear310 specific devices */
	&spear310_pmx_emi_cs_0_1_4_5,
	&spear310_pmx_emi_cs_2_3,
	&spear310_pmx_uart1,
	&spear310_pmx_uart2,
	&spear310_pmx_uart3_4_5,
	&spear310_pmx_fsmc,
	&spear310_pmx_rs485_0_1,
	&spear310_pmx_tdm0,
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
	int ret = 0;

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

	if (of_machine_is_compatible("st,spear310-evb")) {
		/* pmx initialization */
		pmx_driver.base = base;
		pmx_driver.mode = NULL;
		pmx_driver.devs = spear310_evb_pmx_devs;
		pmx_driver.devs_count = ARRAY_SIZE(spear310_evb_pmx_devs);

		ret = pmx_register(&pmx_driver);
		if (ret)
			pr_err("padmux: registration failed. err no: %d\n",
					ret);
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
