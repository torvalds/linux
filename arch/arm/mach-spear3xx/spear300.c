/*
 * arch/arm/mach-spear3xx/spear300.c
 *
 * SPEAr300 machine source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/amba/pl061.h>
#include <linux/ptrace.h>
#include <asm/irq.h>
#include <plat/shirq.h>
#include <mach/generic.h>
#include <mach/hardware.h>

/* pad multiplexing support */
/* muxing registers */
#define PAD_MUX_CONFIG_REG	0x00
#define MODE_CONFIG_REG		0x04

/* modes */
#define NAND_MODE			(1 << 0)
#define NOR_MODE			(1 << 1)
#define PHOTO_FRAME_MODE		(1 << 2)
#define LEND_IP_PHONE_MODE		(1 << 3)
#define HEND_IP_PHONE_MODE		(1 << 4)
#define LEND_WIFI_PHONE_MODE		(1 << 5)
#define HEND_WIFI_PHONE_MODE		(1 << 6)
#define ATA_PABX_WI2S_MODE		(1 << 7)
#define ATA_PABX_I2S_MODE		(1 << 8)
#define CAML_LCDW_MODE			(1 << 9)
#define CAMU_LCD_MODE			(1 << 10)
#define CAMU_WLCD_MODE			(1 << 11)
#define CAML_LCD_MODE			(1 << 12)
#define ALL_MODES			0x1FFF

struct pmx_mode spear300_nand_mode = {
	.id = NAND_MODE,
	.name = "nand mode",
	.mask = 0x00,
};

struct pmx_mode spear300_nor_mode = {
	.id = NOR_MODE,
	.name = "nor mode",
	.mask = 0x01,
};

struct pmx_mode spear300_photo_frame_mode = {
	.id = PHOTO_FRAME_MODE,
	.name = "photo frame mode",
	.mask = 0x02,
};

struct pmx_mode spear300_lend_ip_phone_mode = {
	.id = LEND_IP_PHONE_MODE,
	.name = "lend ip phone mode",
	.mask = 0x03,
};

struct pmx_mode spear300_hend_ip_phone_mode = {
	.id = HEND_IP_PHONE_MODE,
	.name = "hend ip phone mode",
	.mask = 0x04,
};

struct pmx_mode spear300_lend_wifi_phone_mode = {
	.id = LEND_WIFI_PHONE_MODE,
	.name = "lend wifi phone mode",
	.mask = 0x05,
};

struct pmx_mode spear300_hend_wifi_phone_mode = {
	.id = HEND_WIFI_PHONE_MODE,
	.name = "hend wifi phone mode",
	.mask = 0x06,
};

struct pmx_mode spear300_ata_pabx_wi2s_mode = {
	.id = ATA_PABX_WI2S_MODE,
	.name = "ata pabx wi2s mode",
	.mask = 0x07,
};

struct pmx_mode spear300_ata_pabx_i2s_mode = {
	.id = ATA_PABX_I2S_MODE,
	.name = "ata pabx i2s mode",
	.mask = 0x08,
};

struct pmx_mode spear300_caml_lcdw_mode = {
	.id = CAML_LCDW_MODE,
	.name = "caml lcdw mode",
	.mask = 0x0C,
};

struct pmx_mode spear300_camu_lcd_mode = {
	.id = CAMU_LCD_MODE,
	.name = "camu lcd mode",
	.mask = 0x0D,
};

struct pmx_mode spear300_camu_wlcd_mode = {
	.id = CAMU_WLCD_MODE,
	.name = "camu wlcd mode",
	.mask = 0x0E,
};

struct pmx_mode spear300_caml_lcd_mode = {
	.id = CAML_LCD_MODE,
	.name = "caml lcd mode",
	.mask = 0x0F,
};

/* devices */
static struct pmx_dev_mode pmx_fsmc_2_chips_modes[] = {
	{
		.ids = NAND_MODE | NOR_MODE | PHOTO_FRAME_MODE |
			ATA_PABX_WI2S_MODE | ATA_PABX_I2S_MODE,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev spear300_pmx_fsmc_2_chips = {
	.name = "fsmc_2_chips",
	.modes = pmx_fsmc_2_chips_modes,
	.mode_count = ARRAY_SIZE(pmx_fsmc_2_chips_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_fsmc_4_chips_modes[] = {
	{
		.ids = NAND_MODE | NOR_MODE | PHOTO_FRAME_MODE |
			ATA_PABX_WI2S_MODE | ATA_PABX_I2S_MODE,
		.mask = PMX_FIRDA_MASK | PMX_UART0_MASK,
	},
};

struct pmx_dev spear300_pmx_fsmc_4_chips = {
	.name = "fsmc_4_chips",
	.modes = pmx_fsmc_4_chips_modes,
	.mode_count = ARRAY_SIZE(pmx_fsmc_4_chips_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_keyboard_modes[] = {
	{
		.ids = LEND_IP_PHONE_MODE | HEND_IP_PHONE_MODE |
			LEND_WIFI_PHONE_MODE | HEND_WIFI_PHONE_MODE |
			CAML_LCDW_MODE | CAMU_LCD_MODE | CAMU_WLCD_MODE |
			CAML_LCD_MODE,
		.mask = 0x0,
	},
};

struct pmx_dev spear300_pmx_keyboard = {
	.name = "keyboard",
	.modes = pmx_keyboard_modes,
	.mode_count = ARRAY_SIZE(pmx_keyboard_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_clcd_modes[] = {
	{
		.ids = PHOTO_FRAME_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK ,
	}, {
		.ids = HEND_IP_PHONE_MODE | HEND_WIFI_PHONE_MODE |
			CAMU_LCD_MODE | CAML_LCD_MODE,
		.mask = PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear300_pmx_clcd = {
	.name = "clcd",
	.modes = pmx_clcd_modes,
	.mode_count = ARRAY_SIZE(pmx_clcd_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_gpio_modes[] = {
	{
		.ids = PHOTO_FRAME_MODE | CAMU_LCD_MODE | CAML_LCD_MODE,
		.mask = PMX_MII_MASK,
	}, {
		.ids = LEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE,
		.mask = PMX_MII_MASK | PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK,
	}, {
		.ids = ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_WLCD_MODE,
		.mask = PMX_MII_MASK | PMX_TIMER_3_4_MASK,
	}, {
		.ids = HEND_IP_PHONE_MODE | HEND_WIFI_PHONE_MODE,
		.mask = PMX_MII_MASK | PMX_TIMER_1_2_MASK,
	}, {
		.ids = ATA_PABX_WI2S_MODE,
		.mask = PMX_MII_MASK | PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK
			| PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_gpio = {
	.name = "telecom_gpio",
	.modes = pmx_telecom_gpio_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_gpio_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_tdm_modes[] = {
	{
		.ids = PHOTO_FRAME_MODE | LEND_IP_PHONE_MODE |
			HEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE
			| HEND_WIFI_PHONE_MODE | ATA_PABX_WI2S_MODE
			| ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE
			| CAMU_WLCD_MODE | CAML_LCD_MODE,
		.mask = PMX_UART0_MODEM_MASK | PMX_SSP_CS_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_tdm = {
	.name = "telecom_tdm",
	.modes = pmx_telecom_tdm_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_tdm_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_spi_cs_i2c_clk_modes[] = {
	{
		.ids = LEND_IP_PHONE_MODE | HEND_IP_PHONE_MODE |
			LEND_WIFI_PHONE_MODE | HEND_WIFI_PHONE_MODE
			| ATA_PABX_WI2S_MODE | ATA_PABX_I2S_MODE |
			CAML_LCDW_MODE | CAML_LCD_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_spi_cs_i2c_clk = {
	.name = "telecom_spi_cs_i2c_clk",
	.modes = pmx_telecom_spi_cs_i2c_clk_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_spi_cs_i2c_clk_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_camera_modes[] = {
	{
		.ids = CAML_LCDW_MODE | CAML_LCD_MODE,
		.mask = PMX_MII_MASK,
	}, {
		.ids = CAMU_LCD_MODE | CAMU_WLCD_MODE,
		.mask = PMX_TIMER_1_2_MASK | PMX_TIMER_3_4_MASK | PMX_MII_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_camera = {
	.name = "telecom_camera",
	.modes = pmx_telecom_camera_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_camera_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_dac_modes[] = {
	{
		.ids = ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE
			| CAMU_WLCD_MODE | CAML_LCD_MODE,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_dac = {
	.name = "telecom_dac",
	.modes = pmx_telecom_dac_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_dac_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_i2s_modes[] = {
	{
		.ids = LEND_IP_PHONE_MODE | HEND_IP_PHONE_MODE
			| LEND_WIFI_PHONE_MODE | HEND_WIFI_PHONE_MODE |
			ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE
			| CAMU_WLCD_MODE | CAML_LCD_MODE,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_i2s = {
	.name = "telecom_i2s",
	.modes = pmx_telecom_i2s_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_i2s_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_boot_pins_modes[] = {
	{
		.ids = NAND_MODE | NOR_MODE,
		.mask = PMX_UART0_MODEM_MASK | PMX_TIMER_1_2_MASK |
			PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_boot_pins = {
	.name = "telecom_boot_pins",
	.modes = pmx_telecom_boot_pins_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_boot_pins_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_sdhci_4bit_modes[] = {
	{
		.ids = PHOTO_FRAME_MODE | LEND_IP_PHONE_MODE |
			HEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE |
			HEND_WIFI_PHONE_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE |
			CAMU_WLCD_MODE | CAML_LCD_MODE | ATA_PABX_WI2S_MODE |
			ATA_PABX_I2S_MODE,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK |
			PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK |
			PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_sdhci_4bit = {
	.name = "telecom_sdhci_4bit",
	.modes = pmx_telecom_sdhci_4bit_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_sdhci_4bit_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_telecom_sdhci_8bit_modes[] = {
	{
		.ids = PHOTO_FRAME_MODE | LEND_IP_PHONE_MODE |
			HEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE |
			HEND_WIFI_PHONE_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE |
			CAMU_WLCD_MODE | CAML_LCD_MODE,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK |
			PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK |
			PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK | PMX_MII_MASK,
	},
};

struct pmx_dev spear300_pmx_telecom_sdhci_8bit = {
	.name = "telecom_sdhci_8bit",
	.modes = pmx_telecom_sdhci_8bit_modes,
	.mode_count = ARRAY_SIZE(pmx_telecom_sdhci_8bit_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_gpio1_modes[] = {
	{
		.ids = PHOTO_FRAME_MODE,
		.mask = PMX_UART0_MODEM_MASK | PMX_TIMER_1_2_MASK |
			PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear300_pmx_gpio1 = {
	.name = "arm gpio1",
	.modes = pmx_gpio1_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio1_modes),
	.enb_on_reset = 1,
};

/* pmx driver structure */
static struct pmx_driver pmx_driver = {
	.mode_reg = {.offset = MODE_CONFIG_REG, .mask = 0x0000000f},
	.mux_reg = {.offset = PAD_MUX_CONFIG_REG, .mask = 0x00007fff},
};

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

/* Add spear300 specific devices here */
/* arm gpio1 device registration */
static struct pl061_platform_data gpio1_plat_data = {
	.gpio_base	= 8,
	.irq_base	= SPEAR300_GPIO1_INT_BASE,
};

struct amba_device spear300_gpio1_device = {
	.dev = {
		.init_name = "gpio1",
		.platform_data = &gpio1_plat_data,
	},
	.res = {
		.start = SPEAR300_GPIO_BASE,
		.end = SPEAR300_GPIO_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {SPEAR300_VIRQ_GPIO1, NO_IRQ},
};

/* spear300 routines */
void __init spear300_init(struct pmx_mode *pmx_mode, struct pmx_dev **pmx_devs,
		u8 pmx_dev_count)
{
	int ret = 0;

	/* call spear3xx family common init function */
	spear3xx_init();

	/* shared irq registration */
	shirq_ras1.regs.base = ioremap(SPEAR300_TELECOM_BASE, SZ_4K);
	if (shirq_ras1.regs.base) {
		ret = spear_shirq_register(&shirq_ras1);
		if (ret)
			printk(KERN_ERR "Error registering Shared IRQ\n");
	}

	/* pmx initialization */
	pmx_driver.mode = pmx_mode;
	pmx_driver.devs = pmx_devs;
	pmx_driver.devs_count = pmx_dev_count;

	pmx_driver.base = ioremap(SPEAR300_SOC_CONFIG_BASE, SZ_4K);
	if (pmx_driver.base) {
		ret = pmx_register(&pmx_driver);
		if (ret)
			printk(KERN_ERR "padmux: registeration failed. err no"
					": %d\n", ret);
		/* Free Mapping, device selection already done */
		iounmap(pmx_driver.base);
	}
}
