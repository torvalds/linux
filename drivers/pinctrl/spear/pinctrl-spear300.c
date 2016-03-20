/*
 * Driver for the ST Microelectronics SPEAr300 pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "pinctrl-spear3xx.h"

#define DRIVER_NAME "spear300-pinmux"

/* addresses */
#define PMX_CONFIG_REG			0x00
#define MODE_CONFIG_REG			0x04

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

static struct spear_pmx_mode pmx_mode_nand = {
	.name = "nand",
	.mode = NAND_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x00,
};

static struct spear_pmx_mode pmx_mode_nor = {
	.name = "nor",
	.mode = NOR_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x01,
};

static struct spear_pmx_mode pmx_mode_photo_frame = {
	.name = "photo frame mode",
	.mode = PHOTO_FRAME_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x02,
};

static struct spear_pmx_mode pmx_mode_lend_ip_phone = {
	.name = "lend ip phone mode",
	.mode = LEND_IP_PHONE_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x03,
};

static struct spear_pmx_mode pmx_mode_hend_ip_phone = {
	.name = "hend ip phone mode",
	.mode = HEND_IP_PHONE_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x04,
};

static struct spear_pmx_mode pmx_mode_lend_wifi_phone = {
	.name = "lend wifi phone mode",
	.mode = LEND_WIFI_PHONE_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x05,
};

static struct spear_pmx_mode pmx_mode_hend_wifi_phone = {
	.name = "hend wifi phone mode",
	.mode = HEND_WIFI_PHONE_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x06,
};

static struct spear_pmx_mode pmx_mode_ata_pabx_wi2s = {
	.name = "ata pabx wi2s mode",
	.mode = ATA_PABX_WI2S_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x07,
};

static struct spear_pmx_mode pmx_mode_ata_pabx_i2s = {
	.name = "ata pabx i2s mode",
	.mode = ATA_PABX_I2S_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x08,
};

static struct spear_pmx_mode pmx_mode_caml_lcdw = {
	.name = "caml lcdw mode",
	.mode = CAML_LCDW_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x0C,
};

static struct spear_pmx_mode pmx_mode_camu_lcd = {
	.name = "camu lcd mode",
	.mode = CAMU_LCD_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x0D,
};

static struct spear_pmx_mode pmx_mode_camu_wlcd = {
	.name = "camu wlcd mode",
	.mode = CAMU_WLCD_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0xE,
};

static struct spear_pmx_mode pmx_mode_caml_lcd = {
	.name = "caml lcd mode",
	.mode = CAML_LCD_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x0000000F,
	.val = 0x0F,
};

static struct spear_pmx_mode *spear300_pmx_modes[] = {
	&pmx_mode_nand,
	&pmx_mode_nor,
	&pmx_mode_photo_frame,
	&pmx_mode_lend_ip_phone,
	&pmx_mode_hend_ip_phone,
	&pmx_mode_lend_wifi_phone,
	&pmx_mode_hend_wifi_phone,
	&pmx_mode_ata_pabx_wi2s,
	&pmx_mode_ata_pabx_i2s,
	&pmx_mode_caml_lcdw,
	&pmx_mode_camu_lcd,
	&pmx_mode_camu_wlcd,
	&pmx_mode_caml_lcd,
};

/* fsmc_2chips_pins */
static const unsigned fsmc_2chips_pins[] = { 1, 97 };
static struct spear_muxreg fsmc_2chips_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_FIRDA_MASK,
		.val = 0,
	},
};

static struct spear_modemux fsmc_2chips_modemux[] = {
	{
		.modes = NAND_MODE | NOR_MODE | PHOTO_FRAME_MODE |
			ATA_PABX_WI2S_MODE | ATA_PABX_I2S_MODE,
		.muxregs = fsmc_2chips_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_2chips_muxreg),
	},
};

static struct spear_pingroup fsmc_2chips_pingroup = {
	.name = "fsmc_2chips_grp",
	.pins = fsmc_2chips_pins,
	.npins = ARRAY_SIZE(fsmc_2chips_pins),
	.modemuxs = fsmc_2chips_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_2chips_modemux),
};

/* fsmc_4chips_pins */
static const unsigned fsmc_4chips_pins[] = { 1, 2, 3, 97 };
static struct spear_muxreg fsmc_4chips_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_FIRDA_MASK | PMX_UART0_MASK,
		.val = 0,
	},
};

static struct spear_modemux fsmc_4chips_modemux[] = {
	{
		.modes = NAND_MODE | NOR_MODE | PHOTO_FRAME_MODE |
			ATA_PABX_WI2S_MODE | ATA_PABX_I2S_MODE,
		.muxregs = fsmc_4chips_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_4chips_muxreg),
	},
};

static struct spear_pingroup fsmc_4chips_pingroup = {
	.name = "fsmc_4chips_grp",
	.pins = fsmc_4chips_pins,
	.npins = ARRAY_SIZE(fsmc_4chips_pins),
	.modemuxs = fsmc_4chips_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_4chips_modemux),
};

static const char *const fsmc_grps[] = { "fsmc_2chips_grp", "fsmc_4chips_grp"
};
static struct spear_function fsmc_function = {
	.name = "fsmc",
	.groups = fsmc_grps,
	.ngroups = ARRAY_SIZE(fsmc_grps),
};

/* clcd_lcdmode_pins */
static const unsigned clcd_lcdmode_pins[] = { 49, 50 };
static struct spear_muxreg clcd_lcdmode_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_modemux clcd_lcdmode_modemux[] = {
	{
		.modes = HEND_IP_PHONE_MODE | HEND_WIFI_PHONE_MODE |
			CAMU_LCD_MODE | CAML_LCD_MODE,
		.muxregs = clcd_lcdmode_muxreg,
		.nmuxregs = ARRAY_SIZE(clcd_lcdmode_muxreg),
	},
};

static struct spear_pingroup clcd_lcdmode_pingroup = {
	.name = "clcd_lcdmode_grp",
	.pins = clcd_lcdmode_pins,
	.npins = ARRAY_SIZE(clcd_lcdmode_pins),
	.modemuxs = clcd_lcdmode_modemux,
	.nmodemuxs = ARRAY_SIZE(clcd_lcdmode_modemux),
};

/* clcd_pfmode_pins */
static const unsigned clcd_pfmode_pins[] = { 47, 48, 49, 50 };
static struct spear_muxreg clcd_pfmode_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_modemux clcd_pfmode_modemux[] = {
	{
		.modes = PHOTO_FRAME_MODE,
		.muxregs = clcd_pfmode_muxreg,
		.nmuxregs = ARRAY_SIZE(clcd_pfmode_muxreg),
	},
};

static struct spear_pingroup clcd_pfmode_pingroup = {
	.name = "clcd_pfmode_grp",
	.pins = clcd_pfmode_pins,
	.npins = ARRAY_SIZE(clcd_pfmode_pins),
	.modemuxs = clcd_pfmode_modemux,
	.nmodemuxs = ARRAY_SIZE(clcd_pfmode_modemux),
};

static const char *const clcd_grps[] = { "clcd_lcdmode_grp", "clcd_pfmode_grp"
};
static struct spear_function clcd_function = {
	.name = "clcd",
	.groups = clcd_grps,
	.ngroups = ARRAY_SIZE(clcd_grps),
};

/* tdm_pins */
static const unsigned tdm_pins[] = { 34, 35, 36, 37, 38 };
static struct spear_muxreg tdm_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK | PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_modemux tdm_modemux[] = {
	{
		.modes = PHOTO_FRAME_MODE | LEND_IP_PHONE_MODE |
			HEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE
			| HEND_WIFI_PHONE_MODE | ATA_PABX_WI2S_MODE
			| ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE
			| CAMU_WLCD_MODE | CAML_LCD_MODE,
		.muxregs = tdm_muxreg,
		.nmuxregs = ARRAY_SIZE(tdm_muxreg),
	},
};

static struct spear_pingroup tdm_pingroup = {
	.name = "tdm_grp",
	.pins = tdm_pins,
	.npins = ARRAY_SIZE(tdm_pins),
	.modemuxs = tdm_modemux,
	.nmodemuxs = ARRAY_SIZE(tdm_modemux),
};

static const char *const tdm_grps[] = { "tdm_grp" };
static struct spear_function tdm_function = {
	.name = "tdm",
	.groups = tdm_grps,
	.ngroups = ARRAY_SIZE(tdm_grps),
};

/* i2c_clk_pins */
static const unsigned i2c_clk_pins[] = { 45, 46, 47, 48 };
static struct spear_muxreg i2c_clk_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c_clk_modemux[] = {
	{
		.modes = LEND_IP_PHONE_MODE | HEND_IP_PHONE_MODE |
			LEND_WIFI_PHONE_MODE | HEND_WIFI_PHONE_MODE |
			ATA_PABX_WI2S_MODE | ATA_PABX_I2S_MODE | CAML_LCDW_MODE
			| CAML_LCD_MODE,
		.muxregs = i2c_clk_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c_clk_muxreg),
	},
};

static struct spear_pingroup i2c_clk_pingroup = {
	.name = "i2c_clk_grp_grp",
	.pins = i2c_clk_pins,
	.npins = ARRAY_SIZE(i2c_clk_pins),
	.modemuxs = i2c_clk_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c_clk_modemux),
};

static const char *const i2c_grps[] = { "i2c_clk_grp" };
static struct spear_function i2c_function = {
	.name = "i2c1",
	.groups = i2c_grps,
	.ngroups = ARRAY_SIZE(i2c_grps),
};

/* caml_pins */
static const unsigned caml_pins[] = { 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 };
static struct spear_muxreg caml_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_modemux caml_modemux[] = {
	{
		.modes = CAML_LCDW_MODE | CAML_LCD_MODE,
		.muxregs = caml_muxreg,
		.nmuxregs = ARRAY_SIZE(caml_muxreg),
	},
};

static struct spear_pingroup caml_pingroup = {
	.name = "caml_grp",
	.pins = caml_pins,
	.npins = ARRAY_SIZE(caml_pins),
	.modemuxs = caml_modemux,
	.nmodemuxs = ARRAY_SIZE(caml_modemux),
};

/* camu_pins */
static const unsigned camu_pins[] = { 16, 17, 18, 19, 20, 21, 45, 46, 47, 48 };
static struct spear_muxreg camu_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK | PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_modemux camu_modemux[] = {
	{
		.modes = CAMU_LCD_MODE | CAMU_WLCD_MODE,
		.muxregs = camu_muxreg,
		.nmuxregs = ARRAY_SIZE(camu_muxreg),
	},
};

static struct spear_pingroup camu_pingroup = {
	.name = "camu_grp",
	.pins = camu_pins,
	.npins = ARRAY_SIZE(camu_pins),
	.modemuxs = camu_modemux,
	.nmodemuxs = ARRAY_SIZE(camu_modemux),
};

static const char *const cam_grps[] = { "caml_grp", "camu_grp" };
static struct spear_function cam_function = {
	.name = "cam",
	.groups = cam_grps,
	.ngroups = ARRAY_SIZE(cam_grps),
};

/* dac_pins */
static const unsigned dac_pins[] = { 43, 44 };
static struct spear_muxreg dac_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK,
		.val = 0,
	},
};

static struct spear_modemux dac_modemux[] = {
	{
		.modes = ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE
			| CAMU_WLCD_MODE | CAML_LCD_MODE,
		.muxregs = dac_muxreg,
		.nmuxregs = ARRAY_SIZE(dac_muxreg),
	},
};

static struct spear_pingroup dac_pingroup = {
	.name = "dac_grp",
	.pins = dac_pins,
	.npins = ARRAY_SIZE(dac_pins),
	.modemuxs = dac_modemux,
	.nmodemuxs = ARRAY_SIZE(dac_modemux),
};

static const char *const dac_grps[] = { "dac_grp" };
static struct spear_function dac_function = {
	.name = "dac",
	.groups = dac_grps,
	.ngroups = ARRAY_SIZE(dac_grps),
};

/* i2s_pins */
static const unsigned i2s_pins[] = { 39, 40, 41, 42 };
static struct spear_muxreg i2s_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2s_modemux[] = {
	{
		.modes = LEND_IP_PHONE_MODE | HEND_IP_PHONE_MODE
			| LEND_WIFI_PHONE_MODE | HEND_WIFI_PHONE_MODE |
			ATA_PABX_I2S_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE
			| CAMU_WLCD_MODE | CAML_LCD_MODE,
		.muxregs = i2s_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s_muxreg),
	},
};

static struct spear_pingroup i2s_pingroup = {
	.name = "i2s_grp",
	.pins = i2s_pins,
	.npins = ARRAY_SIZE(i2s_pins),
	.modemuxs = i2s_modemux,
	.nmodemuxs = ARRAY_SIZE(i2s_modemux),
};

static const char *const i2s_grps[] = { "i2s_grp" };
static struct spear_function i2s_function = {
	.name = "i2s",
	.groups = i2s_grps,
	.ngroups = ARRAY_SIZE(i2s_grps),
};

/* sdhci_4bit_pins */
static const unsigned sdhci_4bit_pins[] = { 28, 29, 30, 31, 32, 33 };
static struct spear_muxreg sdhci_4bit_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK |
			PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK |
			PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK,
		.val = 0,
	},
};

static struct spear_modemux sdhci_4bit_modemux[] = {
	{
		.modes = PHOTO_FRAME_MODE | LEND_IP_PHONE_MODE |
			HEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE |
			HEND_WIFI_PHONE_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE |
			CAMU_WLCD_MODE | CAML_LCD_MODE | ATA_PABX_WI2S_MODE,
		.muxregs = sdhci_4bit_muxreg,
		.nmuxregs = ARRAY_SIZE(sdhci_4bit_muxreg),
	},
};

static struct spear_pingroup sdhci_4bit_pingroup = {
	.name = "sdhci_4bit_grp",
	.pins = sdhci_4bit_pins,
	.npins = ARRAY_SIZE(sdhci_4bit_pins),
	.modemuxs = sdhci_4bit_modemux,
	.nmodemuxs = ARRAY_SIZE(sdhci_4bit_modemux),
};

/* sdhci_8bit_pins */
static const unsigned sdhci_8bit_pins[] = { 24, 25, 26, 27, 28, 29, 30, 31, 32,
	33 };
static struct spear_muxreg sdhci_8bit_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK |
			PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK |
			PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK | PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_modemux sdhci_8bit_modemux[] = {
	{
		.modes = PHOTO_FRAME_MODE | LEND_IP_PHONE_MODE |
			HEND_IP_PHONE_MODE | LEND_WIFI_PHONE_MODE |
			HEND_WIFI_PHONE_MODE | CAML_LCDW_MODE | CAMU_LCD_MODE |
			CAMU_WLCD_MODE | CAML_LCD_MODE,
		.muxregs = sdhci_8bit_muxreg,
		.nmuxregs = ARRAY_SIZE(sdhci_8bit_muxreg),
	},
};

static struct spear_pingroup sdhci_8bit_pingroup = {
	.name = "sdhci_8bit_grp",
	.pins = sdhci_8bit_pins,
	.npins = ARRAY_SIZE(sdhci_8bit_pins),
	.modemuxs = sdhci_8bit_modemux,
	.nmodemuxs = ARRAY_SIZE(sdhci_8bit_modemux),
};

static const char *const sdhci_grps[] = { "sdhci_4bit_grp", "sdhci_8bit_grp" };
static struct spear_function sdhci_function = {
	.name = "sdhci",
	.groups = sdhci_grps,
	.ngroups = ARRAY_SIZE(sdhci_grps),
};

/* gpio1_0_to_3_pins */
static const unsigned gpio1_0_to_3_pins[] = { 39, 40, 41, 42 };
static struct spear_muxreg gpio1_0_to_3_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_modemux gpio1_0_to_3_modemux[] = {
	{
		.modes = PHOTO_FRAME_MODE,
		.muxregs = gpio1_0_to_3_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio1_0_to_3_muxreg),
	},
};

static struct spear_pingroup gpio1_0_to_3_pingroup = {
	.name = "gpio1_0_to_3_grp",
	.pins = gpio1_0_to_3_pins,
	.npins = ARRAY_SIZE(gpio1_0_to_3_pins),
	.modemuxs = gpio1_0_to_3_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio1_0_to_3_modemux),
};

/* gpio1_4_to_7_pins */
static const unsigned gpio1_4_to_7_pins[] = { 43, 44, 45, 46 };

static struct spear_muxreg gpio1_4_to_7_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_modemux gpio1_4_to_7_modemux[] = {
	{
		.modes = PHOTO_FRAME_MODE,
		.muxregs = gpio1_4_to_7_muxreg,
		.nmuxregs = ARRAY_SIZE(gpio1_4_to_7_muxreg),
	},
};

static struct spear_pingroup gpio1_4_to_7_pingroup = {
	.name = "gpio1_4_to_7_grp",
	.pins = gpio1_4_to_7_pins,
	.npins = ARRAY_SIZE(gpio1_4_to_7_pins),
	.modemuxs = gpio1_4_to_7_modemux,
	.nmodemuxs = ARRAY_SIZE(gpio1_4_to_7_modemux),
};

static const char *const gpio1_grps[] = { "gpio1_0_to_3_grp", "gpio1_4_to_7_grp"
};
static struct spear_function gpio1_function = {
	.name = "gpio1",
	.groups = gpio1_grps,
	.ngroups = ARRAY_SIZE(gpio1_grps),
};

/* pingroups */
static struct spear_pingroup *spear300_pingroups[] = {
	SPEAR3XX_COMMON_PINGROUPS,
	&fsmc_2chips_pingroup,
	&fsmc_4chips_pingroup,
	&clcd_lcdmode_pingroup,
	&clcd_pfmode_pingroup,
	&tdm_pingroup,
	&i2c_clk_pingroup,
	&caml_pingroup,
	&camu_pingroup,
	&dac_pingroup,
	&i2s_pingroup,
	&sdhci_4bit_pingroup,
	&sdhci_8bit_pingroup,
	&gpio1_0_to_3_pingroup,
	&gpio1_4_to_7_pingroup,
};

/* functions */
static struct spear_function *spear300_functions[] = {
	SPEAR3XX_COMMON_FUNCTIONS,
	&fsmc_function,
	&clcd_function,
	&tdm_function,
	&i2c_function,
	&cam_function,
	&dac_function,
	&i2s_function,
	&sdhci_function,
	&gpio1_function,
};

static const struct of_device_id spear300_pinctrl_of_match[] = {
	{
		.compatible = "st,spear300-pinmux",
	},
	{},
};

static int spear300_pinctrl_probe(struct platform_device *pdev)
{
	int ret;

	spear3xx_machdata.groups = spear300_pingroups;
	spear3xx_machdata.ngroups = ARRAY_SIZE(spear300_pingroups);
	spear3xx_machdata.functions = spear300_functions;
	spear3xx_machdata.nfunctions = ARRAY_SIZE(spear300_functions);
	spear3xx_machdata.gpio_pingroups = NULL;
	spear3xx_machdata.ngpio_pingroups = 0;

	spear3xx_machdata.modes_supported = true;
	spear3xx_machdata.pmx_modes = spear300_pmx_modes;
	spear3xx_machdata.npmx_modes = ARRAY_SIZE(spear300_pmx_modes);

	pmx_init_addr(&spear3xx_machdata, PMX_CONFIG_REG);

	ret = spear_pinctrl_probe(pdev, &spear3xx_machdata);
	if (ret)
		return ret;

	return 0;
}

static int spear300_pinctrl_remove(struct platform_device *pdev)
{
	return spear_pinctrl_remove(pdev);
}

static struct platform_driver spear300_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = spear300_pinctrl_of_match,
	},
	.probe = spear300_pinctrl_probe,
	.remove = spear300_pinctrl_remove,
};

static int __init spear300_pinctrl_init(void)
{
	return platform_driver_register(&spear300_pinctrl_driver);
}
arch_initcall(spear300_pinctrl_init);

static void __exit spear300_pinctrl_exit(void)
{
	platform_driver_unregister(&spear300_pinctrl_driver);
}
module_exit(spear300_pinctrl_exit);

MODULE_AUTHOR("Viresh Kumar <vireshk@kernel.org>");
MODULE_DESCRIPTION("ST Microelectronics SPEAr300 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, spear300_pinctrl_of_match);
