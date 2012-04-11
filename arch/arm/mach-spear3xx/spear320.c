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

struct pmx_mode spear320_auto_net_smii_mode = {
	.id = AUTO_NET_SMII_MODE,
	.name = "Automation Networking SMII Mode",
	.mask = 0x00,
};

struct pmx_mode spear320_auto_net_mii_mode = {
	.id = AUTO_NET_MII_MODE,
	.name = "Automation Networking MII Mode",
	.mask = 0x01,
};

struct pmx_mode spear320_auto_exp_mode = {
	.id = AUTO_EXP_MODE,
	.name = "Automation Expanded Mode",
	.mask = 0x02,
};

struct pmx_mode spear320_small_printers_mode = {
	.id = SMALL_PRINTERS_MODE,
	.name = "Small Printers Mode",
	.mask = 0x03,
};

/* devices */
static struct pmx_dev_mode pmx_clcd_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev spear320_pmx_clcd = {
	.name = "clcd",
	.modes = pmx_clcd_modes,
	.mode_count = ARRAY_SIZE(pmx_clcd_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_emi_modes[] = {
	{
		.ids = AUTO_EXP_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear320_pmx_emi = {
	.name = "emi",
	.modes = pmx_emi_modes,
	.mode_count = ARRAY_SIZE(pmx_emi_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_fsmc_modes[] = {
	{
		.ids = ALL_MODES,
		.mask = 0x0,
	},
};

struct pmx_dev spear320_pmx_fsmc = {
	.name = "fsmc",
	.modes = pmx_fsmc_modes,
	.mode_count = ARRAY_SIZE(pmx_fsmc_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_spp_modes[] = {
	{
		.ids = SMALL_PRINTERS_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev spear320_pmx_spp = {
	.name = "spp",
	.modes = pmx_spp_modes,
	.mode_count = ARRAY_SIZE(pmx_spp_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_sdhci_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE |
			SMALL_PRINTERS_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear320_pmx_sdhci = {
	.name = "sdhci",
	.modes = pmx_sdhci_modes,
	.mode_count = ARRAY_SIZE(pmx_sdhci_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_i2s_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev spear320_pmx_i2s = {
	.name = "i2s",
	.modes = pmx_i2s_modes,
	.mode_count = ARRAY_SIZE(pmx_i2s_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_uart1_modes[] = {
	{
		.ids = ALL_MODES,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK,
	},
};

struct pmx_dev spear320_pmx_uart1 = {
	.name = "uart1",
	.modes = pmx_uart1_modes,
	.mode_count = ARRAY_SIZE(pmx_uart1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_uart1_modem_modes[] = {
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

struct pmx_dev spear320_pmx_uart1_modem = {
	.name = "uart1_modem",
	.modes = pmx_uart1_modem_modes,
	.mode_count = ARRAY_SIZE(pmx_uart1_modem_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_uart2_modes[] = {
	{
		.ids = ALL_MODES,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev spear320_pmx_uart2 = {
	.name = "uart2",
	.modes = pmx_uart2_modes,
	.mode_count = ARRAY_SIZE(pmx_uart2_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_touchscreen_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev spear320_pmx_touchscreen = {
	.name = "touchscreen",
	.modes = pmx_touchscreen_modes,
	.mode_count = ARRAY_SIZE(pmx_touchscreen_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_can_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | AUTO_EXP_MODE,
		.mask = PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK |
			PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK,
	},
};

struct pmx_dev spear320_pmx_can = {
	.name = "can",
	.modes = pmx_can_modes,
	.mode_count = ARRAY_SIZE(pmx_can_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_sdhci_led_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev spear320_pmx_sdhci_led = {
	.name = "sdhci_led",
	.modes = pmx_sdhci_led_modes,
	.mode_count = ARRAY_SIZE(pmx_sdhci_led_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_pwm0_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	}, {
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_pwm0 = {
	.name = "pwm0",
	.modes = pmx_pwm0_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm0_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_pwm1_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	}, {
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_pwm1 = {
	.name = "pwm1",
	.modes = pmx_pwm1_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_pwm2_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE,
		.mask = PMX_SSP_CS_MASK,
	}, {
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_pwm2 = {
	.name = "pwm2",
	.modes = pmx_pwm2_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm2_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_pwm3_modes[] = {
	{
		.ids = AUTO_EXP_MODE | SMALL_PRINTERS_MODE | AUTO_NET_SMII_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_pwm3 = {
	.name = "pwm3",
	.modes = pmx_pwm3_modes,
	.mode_count = ARRAY_SIZE(pmx_pwm3_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_ssp1_modes[] = {
	{
		.ids = SMALL_PRINTERS_MODE | AUTO_NET_SMII_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_ssp1 = {
	.name = "ssp1",
	.modes = pmx_ssp1_modes,
	.mode_count = ARRAY_SIZE(pmx_ssp1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_ssp2_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_ssp2 = {
	.name = "ssp2",
	.modes = pmx_ssp2_modes,
	.mode_count = ARRAY_SIZE(pmx_ssp2_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_mii1_modes[] = {
	{
		.ids = AUTO_NET_MII_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev spear320_pmx_mii1 = {
	.name = "mii1",
	.modes = pmx_mii1_modes,
	.mode_count = ARRAY_SIZE(pmx_mii1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_smii0_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | AUTO_EXP_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_smii0 = {
	.name = "smii0",
	.modes = pmx_smii0_modes,
	.mode_count = ARRAY_SIZE(pmx_smii0_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_smii1_modes[] = {
	{
		.ids = AUTO_NET_SMII_MODE | SMALL_PRINTERS_MODE,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear320_pmx_smii1 = {
	.name = "smii1",
	.modes = pmx_smii1_modes,
	.mode_count = ARRAY_SIZE(pmx_smii1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_i2c1_modes[] = {
	{
		.ids = AUTO_EXP_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev spear320_pmx_i2c1 = {
	.name = "i2c1",
	.modes = pmx_i2c1_modes,
	.mode_count = ARRAY_SIZE(pmx_i2c1_modes),
	.enb_on_reset = 1,
};

/* pmx driver structure */
static struct pmx_driver pmx_driver = {
	.mode_reg = {.offset = MODE_CONFIG_REG, .mask = 0x00000007},
	.mux_reg = {.offset = PAD_MUX_CONFIG_REG, .mask = 0x00007fff},
};

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

/* padmux devices to enable */
static struct pmx_dev *spear320_evb_pmx_devs[] = {
	/* spear3xx specific devices */
	&spear3xx_pmx_i2c,
	&spear3xx_pmx_ssp,
	&spear3xx_pmx_mii,
	&spear3xx_pmx_uart0,

	/* spear320 specific devices */
	&spear320_pmx_fsmc,
	&spear320_pmx_sdhci,
	&spear320_pmx_i2s,
	&spear320_pmx_uart1,
	&spear320_pmx_uart2,
	&spear320_pmx_can,
	&spear320_pmx_pwm0,
	&spear320_pmx_pwm1,
	&spear320_pmx_pwm2,
	&spear320_pmx_mii1,
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
	int ret = 0;

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

	if (of_machine_is_compatible("st,spear320-evb")) {
		/* pmx initialization */
		pmx_driver.base = base;
		pmx_driver.mode = &spear320_auto_net_mii_mode;
		pmx_driver.devs = spear320_evb_pmx_devs;
		pmx_driver.devs_count = ARRAY_SIZE(spear320_evb_pmx_devs);

		ret = pmx_register(&pmx_driver);
		if (ret)
			pr_err("padmux: registration failed. err no: %d\n",
					ret);
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
