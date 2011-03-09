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
#include <plat/shirq.h>

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

/* spear3xx shared irq */
struct shirq_dev_config shirq_ras1_config[] = {
	{
		.virq = VIRQ_EMI,
		.status_mask = EMI_IRQ_MASK,
		.clear_mask = EMI_IRQ_MASK,
	}, {
		.virq = VIRQ_CLCD,
		.status_mask = CLCD_IRQ_MASK,
		.clear_mask = CLCD_IRQ_MASK,
	}, {
		.virq = VIRQ_SPP,
		.status_mask = SPP_IRQ_MASK,
		.clear_mask = SPP_IRQ_MASK,
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
		.clear_reg = INT_CLR_MASK_REG,
		.reset_to_clear = 1,
	},
};

struct shirq_dev_config shirq_ras3_config[] = {
	{
		.virq = VIRQ_PLGPIO,
		.enb_mask = GPIO_IRQ_MASK,
		.status_mask = GPIO_IRQ_MASK,
		.clear_mask = GPIO_IRQ_MASK,
	}, {
		.virq = VIRQ_I2S_PLAY,
		.enb_mask = I2S_PLAY_IRQ_MASK,
		.status_mask = I2S_PLAY_IRQ_MASK,
		.clear_mask = I2S_PLAY_IRQ_MASK,
	}, {
		.virq = VIRQ_I2S_REC,
		.enb_mask = I2S_REC_IRQ_MASK,
		.status_mask = I2S_REC_IRQ_MASK,
		.clear_mask = I2S_REC_IRQ_MASK,
	},
};

struct spear_shirq shirq_ras3 = {
	.irq = IRQ_GEN_RAS_3,
	.dev_config = shirq_ras3_config,
	.dev_count = ARRAY_SIZE(shirq_ras3_config),
	.regs = {
		.enb_reg = INT_ENB_MASK_REG,
		.reset_to_enb = 1,
		.status_reg = INT_STS_MASK_REG,
		.status_reg_mask = SHIRQ_RAS3_MASK,
		.clear_reg = INT_CLR_MASK_REG,
		.reset_to_clear = 1,
	},
};

struct shirq_dev_config shirq_intrcomm_ras_config[] = {
	{
		.virq = VIRQ_CANU,
		.status_mask = CAN_U_IRQ_MASK,
		.clear_mask = CAN_U_IRQ_MASK,
	}, {
		.virq = VIRQ_CANL,
		.status_mask = CAN_L_IRQ_MASK,
		.clear_mask = CAN_L_IRQ_MASK,
	}, {
		.virq = VIRQ_UART1,
		.status_mask = UART1_IRQ_MASK,
		.clear_mask = UART1_IRQ_MASK,
	}, {
		.virq = VIRQ_UART2,
		.status_mask = UART2_IRQ_MASK,
		.clear_mask = UART2_IRQ_MASK,
	}, {
		.virq = VIRQ_SSP1,
		.status_mask = SSP1_IRQ_MASK,
		.clear_mask = SSP1_IRQ_MASK,
	}, {
		.virq = VIRQ_SSP2,
		.status_mask = SSP2_IRQ_MASK,
		.clear_mask = SSP2_IRQ_MASK,
	}, {
		.virq = VIRQ_SMII0,
		.status_mask = SMII0_IRQ_MASK,
		.clear_mask = SMII0_IRQ_MASK,
	}, {
		.virq = VIRQ_MII1_SMII1,
		.status_mask = MII1_SMII1_IRQ_MASK,
		.clear_mask = MII1_SMII1_IRQ_MASK,
	}, {
		.virq = VIRQ_WAKEUP_SMII0,
		.status_mask = WAKEUP_SMII0_IRQ_MASK,
		.clear_mask = WAKEUP_SMII0_IRQ_MASK,
	}, {
		.virq = VIRQ_WAKEUP_MII1_SMII1,
		.status_mask = WAKEUP_MII1_SMII1_IRQ_MASK,
		.clear_mask = WAKEUP_MII1_SMII1_IRQ_MASK,
	}, {
		.virq = VIRQ_I2C,
		.status_mask = I2C1_IRQ_MASK,
		.clear_mask = I2C1_IRQ_MASK,
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
		.clear_reg = INT_CLR_MASK_REG,
		.reset_to_clear = 1,
	},
};

/* spear320 routines */
void __init spear320_init(void)
{
	void __iomem *base;
	int ret = 0;

	/* call spear3xx family common init function */
	spear3xx_init();

	/* shared irq registration */
	base = ioremap(SPEAR320_SOC_CONFIG_BASE, SPEAR320_SOC_CONFIG_SIZE);
	if (base) {
		/* shirq 1 */
		shirq_ras1.regs.base = base;
		ret = spear_shirq_register(&shirq_ras1);
		if (ret)
			printk(KERN_ERR "Error registering Shared IRQ 1\n");

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

void spear320_pmx_init(void)
{
	spear_pmx_init(&pmx_driver, SPEAR320_SOC_CONFIG_BASE,
			SPEAR320_SOC_CONFIG_SIZE);
}
