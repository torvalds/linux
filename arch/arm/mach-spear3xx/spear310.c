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
#include <plat/shirq.h>

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

/* spear3xx shared irq */
struct shirq_dev_config shirq_ras1_config[] = {
	{
		.virq = VIRQ_SMII0,
		.status_mask = SMII0_IRQ_MASK,
	}, {
		.virq = VIRQ_SMII1,
		.status_mask = SMII1_IRQ_MASK,
	}, {
		.virq = VIRQ_SMII2,
		.status_mask = SMII2_IRQ_MASK,
	}, {
		.virq = VIRQ_SMII3,
		.status_mask = SMII3_IRQ_MASK,
	}, {
		.virq = VIRQ_WAKEUP_SMII0,
		.status_mask = WAKEUP_SMII0_IRQ_MASK,
	}, {
		.virq = VIRQ_WAKEUP_SMII1,
		.status_mask = WAKEUP_SMII1_IRQ_MASK,
	}, {
		.virq = VIRQ_WAKEUP_SMII2,
		.status_mask = WAKEUP_SMII2_IRQ_MASK,
	}, {
		.virq = VIRQ_WAKEUP_SMII3,
		.status_mask = WAKEUP_SMII3_IRQ_MASK,
	},
};

struct spear_shirq shirq_ras1 = {
	.irq = IRQ_GEN_RAS_1,
	.dev_config = shirq_ras1_config,
	.dev_count = ARRAY_SIZE(shirq_ras1_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = INT_STS_MASK_REG,
		.status_reg_mask = SHIRQ_RAS1_MASK,
		.clear_reg = -1,
	},
};

struct shirq_dev_config shirq_ras2_config[] = {
	{
		.virq = VIRQ_UART1,
		.status_mask = UART1_IRQ_MASK,
	}, {
		.virq = VIRQ_UART2,
		.status_mask = UART2_IRQ_MASK,
	}, {
		.virq = VIRQ_UART3,
		.status_mask = UART3_IRQ_MASK,
	}, {
		.virq = VIRQ_UART4,
		.status_mask = UART4_IRQ_MASK,
	}, {
		.virq = VIRQ_UART5,
		.status_mask = UART5_IRQ_MASK,
	},
};

struct spear_shirq shirq_ras2 = {
	.irq = IRQ_GEN_RAS_2,
	.dev_config = shirq_ras2_config,
	.dev_count = ARRAY_SIZE(shirq_ras2_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = INT_STS_MASK_REG,
		.status_reg_mask = SHIRQ_RAS2_MASK,
		.clear_reg = -1,
	},
};

struct shirq_dev_config shirq_ras3_config[] = {
	{
		.virq = VIRQ_EMI,
		.status_mask = EMI_IRQ_MASK,
	},
};

struct spear_shirq shirq_ras3 = {
	.irq = IRQ_GEN_RAS_3,
	.dev_config = shirq_ras3_config,
	.dev_count = ARRAY_SIZE(shirq_ras3_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = INT_STS_MASK_REG,
		.status_reg_mask = SHIRQ_RAS3_MASK,
		.clear_reg = -1,
	},
};

struct shirq_dev_config shirq_intrcomm_ras_config[] = {
	{
		.virq = VIRQ_TDM_HDLC,
		.status_mask = TDM_HDLC_IRQ_MASK,
	}, {
		.virq = VIRQ_RS485_0,
		.status_mask = RS485_0_IRQ_MASK,
	}, {
		.virq = VIRQ_RS485_1,
		.status_mask = RS485_1_IRQ_MASK,
	},
};

struct spear_shirq shirq_intrcomm_ras = {
	.irq = IRQ_INTRCOMM_RAS_ARM,
	.dev_config = shirq_intrcomm_ras_config,
	.dev_count = ARRAY_SIZE(shirq_intrcomm_ras_config),
	.regs = {
		.enb_reg = -1,
		.status_reg = INT_STS_MASK_REG,
		.status_reg_mask = SHIRQ_INTRCOMM_RAS_MASK,
		.clear_reg = -1,
	},
};

/* spear310 routines */
void __init spear310_init(void)
{
	void __iomem *base;
	int ret = 0;

	/* call spear3xx family common init function */
	spear3xx_init();

	/* shared irq registeration */
	base = ioremap(SPEAR310_SOC_CONFIG_BASE, SPEAR310_SOC_CONFIG_SIZE);
	if (base) {
		/* shirq 1 */
		shirq_ras1.regs.base = base;
		ret = spear_shirq_register(&shirq_ras1);
		if (ret)
			printk(KERN_ERR "Error registering Shared IRQ 1\n");

		/* shirq 2 */
		shirq_ras2.regs.base = base;
		ret = spear_shirq_register(&shirq_ras2);
		if (ret)
			printk(KERN_ERR "Error registering Shared IRQ 2\n");

		/* shirq 3 */
		shirq_ras3.regs.base = base;
		ret = spear_shirq_register(&shirq_ras3);
		if (ret)
			printk(KERN_ERR "Error registering Shared IRQ 3\n");

		/* shirq 4 */
		shirq_intrcomm_ras.regs.base = base;
		ret = spear_shirq_register(&shirq_intrcomm_ras);
		if (ret)
			printk(KERN_ERR "Error registering Shared IRQ 4\n");
	}
}

void spear310_pmx_init(void)
{
	spear_pmx_init(&pmx_driver, SPEAR310_SOC_CONFIG_BASE,
			SPEAR310_SOC_CONFIG_SIZE);
}
