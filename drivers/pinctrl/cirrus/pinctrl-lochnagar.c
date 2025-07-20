// SPDX-License-Identifier: GPL-2.0
/*
 * Lochnagar pin and GPIO control
 *
 * Copyright (c) 2017-2018 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Charles Keepax <ckeepax@opensource.cirrus.com>
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/string_choices.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <linux/mfd/lochnagar.h>
#include <linux/mfd/lochnagar1_regs.h>
#include <linux/mfd/lochnagar2_regs.h>

#include <dt-bindings/pinctrl/lochnagar.h>

#include "../pinctrl-utils.h"

#define LN2_NUM_GPIO_CHANNELS	16

#define LN_CDC_AIF1_STR		"codec-aif1"
#define LN_CDC_AIF2_STR		"codec-aif2"
#define LN_CDC_AIF3_STR		"codec-aif3"
#define LN_DSP_AIF1_STR		"dsp-aif1"
#define LN_DSP_AIF2_STR		"dsp-aif2"
#define LN_PSIA1_STR		"psia1"
#define LN_PSIA2_STR		"psia2"
#define LN_GF_AIF1_STR		"gf-aif1"
#define LN_GF_AIF2_STR		"gf-aif2"
#define LN_GF_AIF3_STR		"gf-aif3"
#define LN_GF_AIF4_STR		"gf-aif4"
#define LN_SPDIF_AIF_STR	"spdif-aif"
#define LN_USB_AIF1_STR		"usb-aif1"
#define LN_USB_AIF2_STR		"usb-aif2"
#define LN_ADAT_AIF_STR		"adat-aif"
#define LN_SOUNDCARD_AIF_STR	"soundcard-aif"

#define LN_PIN_GPIO(REV, ID, NAME, REG, SHIFT, INVERT) \
static const struct lochnagar_pin lochnagar##REV##_##ID##_pin = { \
	.name = NAME, .type = LN_PTYPE_GPIO, .reg = LOCHNAGAR##REV##_##REG, \
	.shift = LOCHNAGAR##REV##_##SHIFT##_SHIFT, .invert = INVERT, \
}

#define LN_PIN_SAIF(REV, ID, NAME) \
static const struct lochnagar_pin lochnagar##REV##_##ID##_pin = \
	{ .name = NAME, .type = LN_PTYPE_AIF, }

#define LN_PIN_AIF(REV, ID) \
	LN_PIN_SAIF(REV, ID##_BCLK,  LN_##ID##_STR"-bclk"); \
	LN_PIN_SAIF(REV, ID##_LRCLK, LN_##ID##_STR"-lrclk"); \
	LN_PIN_SAIF(REV, ID##_RXDAT, LN_##ID##_STR"-rxdat"); \
	LN_PIN_SAIF(REV, ID##_TXDAT, LN_##ID##_STR"-txdat")

#define LN1_PIN_GPIO(ID, NAME, REG, SHIFT, INVERT) \
	LN_PIN_GPIO(1, ID, NAME, REG, SHIFT, INVERT)

#define LN1_PIN_MUX(ID, NAME) \
static const struct lochnagar_pin lochnagar1_##ID##_pin = \
	{ .name = NAME, .type = LN_PTYPE_MUX, .reg = LOCHNAGAR1_##ID, }

#define LN1_PIN_AIF(ID) LN_PIN_AIF(1, ID)

#define LN2_PIN_GPIO(ID, NAME, REG, SHIFT, INVERT) \
	LN_PIN_GPIO(2, ID, NAME, REG, SHIFT, INVERT)

#define LN2_PIN_MUX(ID, NAME) \
static const struct lochnagar_pin lochnagar2_##ID##_pin = \
	{ .name = NAME, .type = LN_PTYPE_MUX, .reg = LOCHNAGAR2_GPIO_##ID, }

#define LN2_PIN_AIF(ID) LN_PIN_AIF(2, ID)

#define LN2_PIN_GAI(ID) \
	LN2_PIN_MUX(ID##_BCLK,  LN_##ID##_STR"-bclk"); \
	LN2_PIN_MUX(ID##_LRCLK, LN_##ID##_STR"-lrclk"); \
	LN2_PIN_MUX(ID##_RXDAT, LN_##ID##_STR"-rxdat"); \
	LN2_PIN_MUX(ID##_TXDAT, LN_##ID##_STR"-txdat")

#define LN_PIN(REV, ID) [LOCHNAGAR##REV##_PIN_##ID] = { \
	.number = LOCHNAGAR##REV##_PIN_##ID, \
	.name = lochnagar##REV##_##ID##_pin.name, \
	.drv_data = (void *)&lochnagar##REV##_##ID##_pin, \
}

#define LN1_PIN(ID) LN_PIN(1, ID)
#define LN2_PIN(ID) LN_PIN(2, ID)

#define LN_PINS(REV, ID) \
	LN_PIN(REV, ID##_BCLK), LN_PIN(REV, ID##_LRCLK), \
	LN_PIN(REV, ID##_RXDAT), LN_PIN(REV, ID##_TXDAT)

#define LN1_PINS(ID) LN_PINS(1, ID)
#define LN2_PINS(ID) LN_PINS(2, ID)

enum {
	LOCHNAGAR1_PIN_GF_GPIO2 = LOCHNAGAR1_PIN_NUM_GPIOS,
	LOCHNAGAR1_PIN_GF_GPIO3,
	LOCHNAGAR1_PIN_GF_GPIO7,
	LOCHNAGAR1_PIN_LED1,
	LOCHNAGAR1_PIN_LED2,
	LOCHNAGAR1_PIN_CDC_AIF1_BCLK,
	LOCHNAGAR1_PIN_CDC_AIF1_LRCLK,
	LOCHNAGAR1_PIN_CDC_AIF1_RXDAT,
	LOCHNAGAR1_PIN_CDC_AIF1_TXDAT,
	LOCHNAGAR1_PIN_CDC_AIF2_BCLK,
	LOCHNAGAR1_PIN_CDC_AIF2_LRCLK,
	LOCHNAGAR1_PIN_CDC_AIF2_RXDAT,
	LOCHNAGAR1_PIN_CDC_AIF2_TXDAT,
	LOCHNAGAR1_PIN_CDC_AIF3_BCLK,
	LOCHNAGAR1_PIN_CDC_AIF3_LRCLK,
	LOCHNAGAR1_PIN_CDC_AIF3_RXDAT,
	LOCHNAGAR1_PIN_CDC_AIF3_TXDAT,
	LOCHNAGAR1_PIN_DSP_AIF1_BCLK,
	LOCHNAGAR1_PIN_DSP_AIF1_LRCLK,
	LOCHNAGAR1_PIN_DSP_AIF1_RXDAT,
	LOCHNAGAR1_PIN_DSP_AIF1_TXDAT,
	LOCHNAGAR1_PIN_DSP_AIF2_BCLK,
	LOCHNAGAR1_PIN_DSP_AIF2_LRCLK,
	LOCHNAGAR1_PIN_DSP_AIF2_RXDAT,
	LOCHNAGAR1_PIN_DSP_AIF2_TXDAT,
	LOCHNAGAR1_PIN_PSIA1_BCLK,
	LOCHNAGAR1_PIN_PSIA1_LRCLK,
	LOCHNAGAR1_PIN_PSIA1_RXDAT,
	LOCHNAGAR1_PIN_PSIA1_TXDAT,
	LOCHNAGAR1_PIN_PSIA2_BCLK,
	LOCHNAGAR1_PIN_PSIA2_LRCLK,
	LOCHNAGAR1_PIN_PSIA2_RXDAT,
	LOCHNAGAR1_PIN_PSIA2_TXDAT,
	LOCHNAGAR1_PIN_SPDIF_AIF_BCLK,
	LOCHNAGAR1_PIN_SPDIF_AIF_LRCLK,
	LOCHNAGAR1_PIN_SPDIF_AIF_RXDAT,
	LOCHNAGAR1_PIN_SPDIF_AIF_TXDAT,
	LOCHNAGAR1_PIN_GF_AIF3_BCLK,
	LOCHNAGAR1_PIN_GF_AIF3_RXDAT,
	LOCHNAGAR1_PIN_GF_AIF3_LRCLK,
	LOCHNAGAR1_PIN_GF_AIF3_TXDAT,
	LOCHNAGAR1_PIN_GF_AIF4_BCLK,
	LOCHNAGAR1_PIN_GF_AIF4_RXDAT,
	LOCHNAGAR1_PIN_GF_AIF4_LRCLK,
	LOCHNAGAR1_PIN_GF_AIF4_TXDAT,
	LOCHNAGAR1_PIN_GF_AIF1_BCLK,
	LOCHNAGAR1_PIN_GF_AIF1_RXDAT,
	LOCHNAGAR1_PIN_GF_AIF1_LRCLK,
	LOCHNAGAR1_PIN_GF_AIF1_TXDAT,
	LOCHNAGAR1_PIN_GF_AIF2_BCLK,
	LOCHNAGAR1_PIN_GF_AIF2_RXDAT,
	LOCHNAGAR1_PIN_GF_AIF2_LRCLK,
	LOCHNAGAR1_PIN_GF_AIF2_TXDAT,

	LOCHNAGAR2_PIN_SPDIF_AIF_BCLK = LOCHNAGAR2_PIN_NUM_GPIOS,
	LOCHNAGAR2_PIN_SPDIF_AIF_LRCLK,
	LOCHNAGAR2_PIN_SPDIF_AIF_RXDAT,
	LOCHNAGAR2_PIN_SPDIF_AIF_TXDAT,
	LOCHNAGAR2_PIN_USB_AIF1_BCLK,
	LOCHNAGAR2_PIN_USB_AIF1_LRCLK,
	LOCHNAGAR2_PIN_USB_AIF1_RXDAT,
	LOCHNAGAR2_PIN_USB_AIF1_TXDAT,
	LOCHNAGAR2_PIN_USB_AIF2_BCLK,
	LOCHNAGAR2_PIN_USB_AIF2_LRCLK,
	LOCHNAGAR2_PIN_USB_AIF2_RXDAT,
	LOCHNAGAR2_PIN_USB_AIF2_TXDAT,
	LOCHNAGAR2_PIN_ADAT_AIF_BCLK,
	LOCHNAGAR2_PIN_ADAT_AIF_LRCLK,
	LOCHNAGAR2_PIN_ADAT_AIF_RXDAT,
	LOCHNAGAR2_PIN_ADAT_AIF_TXDAT,
	LOCHNAGAR2_PIN_SOUNDCARD_AIF_BCLK,
	LOCHNAGAR2_PIN_SOUNDCARD_AIF_LRCLK,
	LOCHNAGAR2_PIN_SOUNDCARD_AIF_RXDAT,
	LOCHNAGAR2_PIN_SOUNDCARD_AIF_TXDAT,
};

enum lochnagar_pin_type {
	LN_PTYPE_GPIO,
	LN_PTYPE_MUX,
	LN_PTYPE_AIF,
	LN_PTYPE_COUNT,
};

struct lochnagar_pin {
	const char name[20];

	enum lochnagar_pin_type type;

	unsigned int reg;
	int shift;
	bool invert;
};

LN1_PIN_GPIO(CDC_RESET,    "codec-reset",    RST,      CDC_RESET,    1);
LN1_PIN_GPIO(DSP_RESET,    "dsp-reset",      RST,      DSP_RESET,    1);
LN1_PIN_GPIO(CDC_CIF1MODE, "codec-cif1mode", I2C_CTRL, CDC_CIF_MODE, 0);
LN1_PIN_MUX(GF_GPIO2,      "gf-gpio2");
LN1_PIN_MUX(GF_GPIO3,      "gf-gpio3");
LN1_PIN_MUX(GF_GPIO7,      "gf-gpio7");
LN1_PIN_MUX(LED1,          "led1");
LN1_PIN_MUX(LED2,          "led2");
LN1_PIN_AIF(CDC_AIF1);
LN1_PIN_AIF(CDC_AIF2);
LN1_PIN_AIF(CDC_AIF3);
LN1_PIN_AIF(DSP_AIF1);
LN1_PIN_AIF(DSP_AIF2);
LN1_PIN_AIF(PSIA1);
LN1_PIN_AIF(PSIA2);
LN1_PIN_AIF(SPDIF_AIF);
LN1_PIN_AIF(GF_AIF1);
LN1_PIN_AIF(GF_AIF2);
LN1_PIN_AIF(GF_AIF3);
LN1_PIN_AIF(GF_AIF4);

LN2_PIN_GPIO(CDC_RESET,    "codec-reset",    MINICARD_RESETS, CDC_RESET,     1);
LN2_PIN_GPIO(DSP_RESET,    "dsp-reset",      MINICARD_RESETS, DSP_RESET,     1);
LN2_PIN_GPIO(CDC_CIF1MODE, "codec-cif1mode", COMMS_CTRL4,     CDC_CIF1MODE,  0);
LN2_PIN_GPIO(CDC_LDOENA,   "codec-ldoena",   POWER_CTRL,      PWR_ENA,       0);
LN2_PIN_GPIO(SPDIF_HWMODE, "spdif-hwmode",   SPDIF_CTRL,      SPDIF_HWMODE,  0);
LN2_PIN_GPIO(SPDIF_RESET,  "spdif-reset",    SPDIF_CTRL,      SPDIF_RESET,   1);
LN2_PIN_MUX(FPGA_GPIO1,    "fpga-gpio1");
LN2_PIN_MUX(FPGA_GPIO2,    "fpga-gpio2");
LN2_PIN_MUX(FPGA_GPIO3,    "fpga-gpio3");
LN2_PIN_MUX(FPGA_GPIO4,    "fpga-gpio4");
LN2_PIN_MUX(FPGA_GPIO5,    "fpga-gpio5");
LN2_PIN_MUX(FPGA_GPIO6,    "fpga-gpio6");
LN2_PIN_MUX(CDC_GPIO1,     "codec-gpio1");
LN2_PIN_MUX(CDC_GPIO2,     "codec-gpio2");
LN2_PIN_MUX(CDC_GPIO3,     "codec-gpio3");
LN2_PIN_MUX(CDC_GPIO4,     "codec-gpio4");
LN2_PIN_MUX(CDC_GPIO5,     "codec-gpio5");
LN2_PIN_MUX(CDC_GPIO6,     "codec-gpio6");
LN2_PIN_MUX(CDC_GPIO7,     "codec-gpio7");
LN2_PIN_MUX(CDC_GPIO8,     "codec-gpio8");
LN2_PIN_MUX(DSP_GPIO1,     "dsp-gpio1");
LN2_PIN_MUX(DSP_GPIO2,     "dsp-gpio2");
LN2_PIN_MUX(DSP_GPIO3,     "dsp-gpio3");
LN2_PIN_MUX(DSP_GPIO4,     "dsp-gpio4");
LN2_PIN_MUX(DSP_GPIO5,     "dsp-gpio5");
LN2_PIN_MUX(DSP_GPIO6,     "dsp-gpio6");
LN2_PIN_MUX(GF_GPIO2,      "gf-gpio2");
LN2_PIN_MUX(GF_GPIO3,      "gf-gpio3");
LN2_PIN_MUX(GF_GPIO7,      "gf-gpio7");
LN2_PIN_MUX(DSP_UART1_RX,  "dsp-uart1-rx");
LN2_PIN_MUX(DSP_UART1_TX,  "dsp-uart1-tx");
LN2_PIN_MUX(DSP_UART2_RX,  "dsp-uart2-rx");
LN2_PIN_MUX(DSP_UART2_TX,  "dsp-uart2-tx");
LN2_PIN_MUX(GF_UART2_RX,   "gf-uart2-rx");
LN2_PIN_MUX(GF_UART2_TX,   "gf-uart2-tx");
LN2_PIN_MUX(USB_UART_RX,   "usb-uart-rx");
LN2_PIN_MUX(CDC_PDMCLK1,   "codec-pdmclk1");
LN2_PIN_MUX(CDC_PDMDAT1,   "codec-pdmdat1");
LN2_PIN_MUX(CDC_PDMCLK2,   "codec-pdmclk2");
LN2_PIN_MUX(CDC_PDMDAT2,   "codec-pdmdat2");
LN2_PIN_MUX(CDC_DMICCLK1,  "codec-dmicclk1");
LN2_PIN_MUX(CDC_DMICDAT1,  "codec-dmicdat1");
LN2_PIN_MUX(CDC_DMICCLK2,  "codec-dmicclk2");
LN2_PIN_MUX(CDC_DMICDAT2,  "codec-dmicdat2");
LN2_PIN_MUX(CDC_DMICCLK3,  "codec-dmicclk3");
LN2_PIN_MUX(CDC_DMICDAT3,  "codec-dmicdat3");
LN2_PIN_MUX(CDC_DMICCLK4,  "codec-dmicclk4");
LN2_PIN_MUX(CDC_DMICDAT4,  "codec-dmicdat4");
LN2_PIN_MUX(DSP_DMICCLK1,  "dsp-dmicclk1");
LN2_PIN_MUX(DSP_DMICDAT1,  "dsp-dmicdat1");
LN2_PIN_MUX(DSP_DMICCLK2,  "dsp-dmicclk2");
LN2_PIN_MUX(DSP_DMICDAT2,  "dsp-dmicdat2");
LN2_PIN_MUX(I2C2_SCL,      "i2c2-scl");
LN2_PIN_MUX(I2C2_SDA,      "i2c2-sda");
LN2_PIN_MUX(I2C3_SCL,      "i2c3-scl");
LN2_PIN_MUX(I2C3_SDA,      "i2c3-sda");
LN2_PIN_MUX(I2C4_SCL,      "i2c4-scl");
LN2_PIN_MUX(I2C4_SDA,      "i2c4-sda");
LN2_PIN_MUX(DSP_STANDBY,   "dsp-standby");
LN2_PIN_MUX(CDC_MCLK1,     "codec-mclk1");
LN2_PIN_MUX(CDC_MCLK2,     "codec-mclk2");
LN2_PIN_MUX(DSP_CLKIN,     "dsp-clkin");
LN2_PIN_MUX(PSIA1_MCLK,    "psia1-mclk");
LN2_PIN_MUX(PSIA2_MCLK,    "psia2-mclk");
LN2_PIN_MUX(GF_GPIO1,      "gf-gpio1");
LN2_PIN_MUX(GF_GPIO5,      "gf-gpio5");
LN2_PIN_MUX(DSP_GPIO20,    "dsp-gpio20");
LN2_PIN_GAI(CDC_AIF1);
LN2_PIN_GAI(CDC_AIF2);
LN2_PIN_GAI(CDC_AIF3);
LN2_PIN_GAI(DSP_AIF1);
LN2_PIN_GAI(DSP_AIF2);
LN2_PIN_GAI(PSIA1);
LN2_PIN_GAI(PSIA2);
LN2_PIN_GAI(GF_AIF1);
LN2_PIN_GAI(GF_AIF2);
LN2_PIN_GAI(GF_AIF3);
LN2_PIN_GAI(GF_AIF4);
LN2_PIN_AIF(SPDIF_AIF);
LN2_PIN_AIF(USB_AIF1);
LN2_PIN_AIF(USB_AIF2);
LN2_PIN_AIF(ADAT_AIF);
LN2_PIN_AIF(SOUNDCARD_AIF);

static const struct pinctrl_pin_desc lochnagar1_pins[] = {
	LN1_PIN(CDC_RESET),      LN1_PIN(DSP_RESET),    LN1_PIN(CDC_CIF1MODE),
	LN1_PIN(GF_GPIO2),       LN1_PIN(GF_GPIO3),     LN1_PIN(GF_GPIO7),
	LN1_PIN(LED1),           LN1_PIN(LED2),
	LN1_PINS(CDC_AIF1),      LN1_PINS(CDC_AIF2),    LN1_PINS(CDC_AIF3),
	LN1_PINS(DSP_AIF1),      LN1_PINS(DSP_AIF2),
	LN1_PINS(PSIA1),         LN1_PINS(PSIA2),
	LN1_PINS(SPDIF_AIF),
	LN1_PINS(GF_AIF1),       LN1_PINS(GF_AIF2),
	LN1_PINS(GF_AIF3),       LN1_PINS(GF_AIF4),
};

static const struct pinctrl_pin_desc lochnagar2_pins[] = {
	LN2_PIN(CDC_RESET),      LN2_PIN(DSP_RESET),    LN2_PIN(CDC_CIF1MODE),
	LN2_PIN(CDC_LDOENA),
	LN2_PIN(SPDIF_HWMODE),   LN2_PIN(SPDIF_RESET),
	LN2_PIN(FPGA_GPIO1),     LN2_PIN(FPGA_GPIO2),   LN2_PIN(FPGA_GPIO3),
	LN2_PIN(FPGA_GPIO4),     LN2_PIN(FPGA_GPIO5),   LN2_PIN(FPGA_GPIO6),
	LN2_PIN(CDC_GPIO1),      LN2_PIN(CDC_GPIO2),    LN2_PIN(CDC_GPIO3),
	LN2_PIN(CDC_GPIO4),      LN2_PIN(CDC_GPIO5),    LN2_PIN(CDC_GPIO6),
	LN2_PIN(CDC_GPIO7),      LN2_PIN(CDC_GPIO8),
	LN2_PIN(DSP_GPIO1),      LN2_PIN(DSP_GPIO2),    LN2_PIN(DSP_GPIO3),
	LN2_PIN(DSP_GPIO4),      LN2_PIN(DSP_GPIO5),    LN2_PIN(DSP_GPIO6),
	LN2_PIN(DSP_GPIO20),
	LN2_PIN(GF_GPIO1),       LN2_PIN(GF_GPIO2),     LN2_PIN(GF_GPIO3),
	LN2_PIN(GF_GPIO5),       LN2_PIN(GF_GPIO7),
	LN2_PINS(CDC_AIF1),      LN2_PINS(CDC_AIF2),    LN2_PINS(CDC_AIF3),
	LN2_PINS(DSP_AIF1),      LN2_PINS(DSP_AIF2),
	LN2_PINS(PSIA1),         LN2_PINS(PSIA2),
	LN2_PINS(GF_AIF1),       LN2_PINS(GF_AIF2),
	LN2_PINS(GF_AIF3),       LN2_PINS(GF_AIF4),
	LN2_PIN(DSP_UART1_RX),   LN2_PIN(DSP_UART1_TX),
	LN2_PIN(DSP_UART2_RX),   LN2_PIN(DSP_UART2_TX),
	LN2_PIN(GF_UART2_RX),    LN2_PIN(GF_UART2_TX),
	LN2_PIN(USB_UART_RX),
	LN2_PIN(CDC_PDMCLK1),    LN2_PIN(CDC_PDMDAT1),
	LN2_PIN(CDC_PDMCLK2),    LN2_PIN(CDC_PDMDAT2),
	LN2_PIN(CDC_DMICCLK1),   LN2_PIN(CDC_DMICDAT1),
	LN2_PIN(CDC_DMICCLK2),   LN2_PIN(CDC_DMICDAT2),
	LN2_PIN(CDC_DMICCLK3),   LN2_PIN(CDC_DMICDAT3),
	LN2_PIN(CDC_DMICCLK4),   LN2_PIN(CDC_DMICDAT4),
	LN2_PIN(DSP_DMICCLK1),   LN2_PIN(DSP_DMICDAT1),
	LN2_PIN(DSP_DMICCLK2),   LN2_PIN(DSP_DMICDAT2),
	LN2_PIN(I2C2_SCL),       LN2_PIN(I2C2_SDA),
	LN2_PIN(I2C3_SCL),       LN2_PIN(I2C3_SDA),
	LN2_PIN(I2C4_SCL),       LN2_PIN(I2C4_SDA),
	LN2_PIN(DSP_STANDBY),
	LN2_PIN(CDC_MCLK1),      LN2_PIN(CDC_MCLK2),
	LN2_PIN(DSP_CLKIN),
	LN2_PIN(PSIA1_MCLK),     LN2_PIN(PSIA2_MCLK),
	LN2_PINS(SPDIF_AIF),
	LN2_PINS(USB_AIF1),      LN2_PINS(USB_AIF2),
	LN2_PINS(ADAT_AIF),
	LN2_PINS(SOUNDCARD_AIF),
};

#define LN_AIF_PINS(REV, ID) \
	LOCHNAGAR##REV##_PIN_##ID##_BCLK, \
	LOCHNAGAR##REV##_PIN_##ID##_LRCLK, \
	LOCHNAGAR##REV##_PIN_##ID##_TXDAT, \
	LOCHNAGAR##REV##_PIN_##ID##_RXDAT,

#define LN1_AIF(ID, CTRL) \
static const struct lochnagar_aif lochnagar1_##ID##_aif = { \
	.name = LN_##ID##_STR, \
	.pins = { LN_AIF_PINS(1, ID) }, \
	.src_reg = LOCHNAGAR1_##ID##_SEL, \
	.src_mask = LOCHNAGAR1_SRC_MASK, \
	.ctrl_reg = LOCHNAGAR1_##CTRL, \
	.ena_mask = LOCHNAGAR1_##ID##_ENA_MASK, \
	.master_mask = LOCHNAGAR1_##ID##_LRCLK_DIR_MASK | \
		       LOCHNAGAR1_##ID##_BCLK_DIR_MASK, \
}

#define LN2_AIF(ID) \
static const struct lochnagar_aif lochnagar2_##ID##_aif = { \
	.name = LN_##ID##_STR, \
	.pins = { LN_AIF_PINS(2, ID) }, \
	.src_reg = LOCHNAGAR2_##ID##_CTRL,  \
	.src_mask = LOCHNAGAR2_AIF_SRC_MASK, \
	.ctrl_reg = LOCHNAGAR2_##ID##_CTRL, \
	.ena_mask = LOCHNAGAR2_AIF_ENA_MASK, \
	.master_mask = LOCHNAGAR2_AIF_LRCLK_DIR_MASK | \
		       LOCHNAGAR2_AIF_BCLK_DIR_MASK, \
}

struct lochnagar_aif {
	const char name[16];

	unsigned int pins[4];

	u16 src_reg;
	u16 src_mask;

	u16 ctrl_reg;
	u16 ena_mask;
	u16 master_mask;
};

LN1_AIF(CDC_AIF1,      CDC_AIF_CTRL1);
LN1_AIF(CDC_AIF2,      CDC_AIF_CTRL1);
LN1_AIF(CDC_AIF3,      CDC_AIF_CTRL2);
LN1_AIF(DSP_AIF1,      DSP_AIF);
LN1_AIF(DSP_AIF2,      DSP_AIF);
LN1_AIF(PSIA1,         PSIA_AIF);
LN1_AIF(PSIA2,         PSIA_AIF);
LN1_AIF(GF_AIF1,       GF_AIF1);
LN1_AIF(GF_AIF2,       GF_AIF2);
LN1_AIF(GF_AIF3,       GF_AIF1);
LN1_AIF(GF_AIF4,       GF_AIF2);
LN1_AIF(SPDIF_AIF,     EXT_AIF_CTRL);

LN2_AIF(CDC_AIF1);
LN2_AIF(CDC_AIF2);
LN2_AIF(CDC_AIF3);
LN2_AIF(DSP_AIF1);
LN2_AIF(DSP_AIF2);
LN2_AIF(PSIA1);
LN2_AIF(PSIA2);
LN2_AIF(GF_AIF1);
LN2_AIF(GF_AIF2);
LN2_AIF(GF_AIF3);
LN2_AIF(GF_AIF4);
LN2_AIF(SPDIF_AIF);
LN2_AIF(USB_AIF1);
LN2_AIF(USB_AIF2);
LN2_AIF(ADAT_AIF);
LN2_AIF(SOUNDCARD_AIF);

#define LN2_OP_AIF	0x00
#define LN2_OP_GPIO	0xFE

#define LN_FUNC(NAME, TYPE, OP) \
	{ .name = NAME, .type = LN_FTYPE_##TYPE, .op = OP }

#define LN_FUNC_PIN(REV, ID, OP) \
	LN_FUNC(lochnagar##REV##_##ID##_pin.name, PIN, OP)

#define LN1_FUNC_PIN(ID, OP) LN_FUNC_PIN(1, ID, OP)
#define LN2_FUNC_PIN(ID, OP) LN_FUNC_PIN(2, ID, OP)

#define LN_FUNC_AIF(REV, ID, OP) \
	LN_FUNC(lochnagar##REV##_##ID##_aif.name, AIF, OP)

#define LN1_FUNC_AIF(ID, OP) LN_FUNC_AIF(1, ID, OP)
#define LN2_FUNC_AIF(ID, OP) LN_FUNC_AIF(2, ID, OP)

#define LN2_FUNC_GAI(ID, OP, BOP, LROP, RXOP, TXOP) \
	LN2_FUNC_AIF(ID, OP), \
	LN_FUNC(lochnagar2_##ID##_BCLK_pin.name, PIN, BOP), \
	LN_FUNC(lochnagar2_##ID##_LRCLK_pin.name, PIN, LROP), \
	LN_FUNC(lochnagar2_##ID##_RXDAT_pin.name, PIN, RXOP), \
	LN_FUNC(lochnagar2_##ID##_TXDAT_pin.name, PIN, TXOP)

enum lochnagar_func_type {
	LN_FTYPE_PIN,
	LN_FTYPE_AIF,
	LN_FTYPE_COUNT,
};

struct lochnagar_func {
	const char * const name;

	enum lochnagar_func_type type;

	u8 op;
};

static const struct lochnagar_func lochnagar1_funcs[] = {
	LN_FUNC("dsp-gpio1",       PIN, 0x01),
	LN_FUNC("dsp-gpio2",       PIN, 0x02),
	LN_FUNC("dsp-gpio3",       PIN, 0x03),
	LN_FUNC("codec-gpio1",     PIN, 0x04),
	LN_FUNC("codec-gpio2",     PIN, 0x05),
	LN_FUNC("codec-gpio3",     PIN, 0x06),
	LN_FUNC("codec-gpio4",     PIN, 0x07),
	LN_FUNC("codec-gpio5",     PIN, 0x08),
	LN_FUNC("codec-gpio6",     PIN, 0x09),
	LN_FUNC("codec-gpio7",     PIN, 0x0A),
	LN_FUNC("codec-gpio8",     PIN, 0x0B),
	LN1_FUNC_PIN(GF_GPIO2,          0x0C),
	LN1_FUNC_PIN(GF_GPIO3,          0x0D),
	LN1_FUNC_PIN(GF_GPIO7,          0x0E),

	LN1_FUNC_AIF(SPDIF_AIF,         0x01),
	LN1_FUNC_AIF(PSIA1,             0x02),
	LN1_FUNC_AIF(PSIA2,             0x03),
	LN1_FUNC_AIF(CDC_AIF1,          0x04),
	LN1_FUNC_AIF(CDC_AIF2,          0x05),
	LN1_FUNC_AIF(CDC_AIF3,          0x06),
	LN1_FUNC_AIF(DSP_AIF1,          0x07),
	LN1_FUNC_AIF(DSP_AIF2,          0x08),
	LN1_FUNC_AIF(GF_AIF3,           0x09),
	LN1_FUNC_AIF(GF_AIF4,           0x0A),
	LN1_FUNC_AIF(GF_AIF1,           0x0B),
	LN1_FUNC_AIF(GF_AIF2,           0x0C),
};

static const struct lochnagar_func lochnagar2_funcs[] = {
	LN_FUNC("aif",             PIN, LN2_OP_AIF),
	LN2_FUNC_PIN(FPGA_GPIO1,        0x01),
	LN2_FUNC_PIN(FPGA_GPIO2,        0x02),
	LN2_FUNC_PIN(FPGA_GPIO3,        0x03),
	LN2_FUNC_PIN(FPGA_GPIO4,        0x04),
	LN2_FUNC_PIN(FPGA_GPIO5,        0x05),
	LN2_FUNC_PIN(FPGA_GPIO6,        0x06),
	LN2_FUNC_PIN(CDC_GPIO1,         0x07),
	LN2_FUNC_PIN(CDC_GPIO2,         0x08),
	LN2_FUNC_PIN(CDC_GPIO3,         0x09),
	LN2_FUNC_PIN(CDC_GPIO4,         0x0A),
	LN2_FUNC_PIN(CDC_GPIO5,         0x0B),
	LN2_FUNC_PIN(CDC_GPIO6,         0x0C),
	LN2_FUNC_PIN(CDC_GPIO7,         0x0D),
	LN2_FUNC_PIN(CDC_GPIO8,         0x0E),
	LN2_FUNC_PIN(DSP_GPIO1,         0x0F),
	LN2_FUNC_PIN(DSP_GPIO2,         0x10),
	LN2_FUNC_PIN(DSP_GPIO3,         0x11),
	LN2_FUNC_PIN(DSP_GPIO4,         0x12),
	LN2_FUNC_PIN(DSP_GPIO5,         0x13),
	LN2_FUNC_PIN(DSP_GPIO6,         0x14),
	LN2_FUNC_PIN(GF_GPIO2,          0x15),
	LN2_FUNC_PIN(GF_GPIO3,          0x16),
	LN2_FUNC_PIN(GF_GPIO7,          0x17),
	LN2_FUNC_PIN(GF_GPIO1,          0x18),
	LN2_FUNC_PIN(GF_GPIO5,          0x19),
	LN2_FUNC_PIN(DSP_GPIO20,        0x1A),
	LN_FUNC("codec-clkout",    PIN, 0x20),
	LN_FUNC("dsp-clkout",      PIN, 0x21),
	LN_FUNC("pmic-32k",        PIN, 0x22),
	LN_FUNC("spdif-clkout",    PIN, 0x23),
	LN_FUNC("clk-12m288",      PIN, 0x24),
	LN_FUNC("clk-11m2986",     PIN, 0x25),
	LN_FUNC("clk-24m576",      PIN, 0x26),
	LN_FUNC("clk-22m5792",     PIN, 0x27),
	LN_FUNC("xmos-mclk",       PIN, 0x29),
	LN_FUNC("gf-clkout1",      PIN, 0x2A),
	LN_FUNC("gf-mclk1",        PIN, 0x2B),
	LN_FUNC("gf-mclk3",        PIN, 0x2C),
	LN_FUNC("gf-mclk2",        PIN, 0x2D),
	LN_FUNC("gf-clkout2",      PIN, 0x2E),
	LN2_FUNC_PIN(CDC_MCLK1,         0x2F),
	LN2_FUNC_PIN(CDC_MCLK2,         0x30),
	LN2_FUNC_PIN(DSP_CLKIN,         0x31),
	LN2_FUNC_PIN(PSIA1_MCLK,        0x32),
	LN2_FUNC_PIN(PSIA2_MCLK,        0x33),
	LN_FUNC("spdif-mclk",      PIN, 0x34),
	LN_FUNC("codec-irq",       PIN, 0x42),
	LN2_FUNC_PIN(CDC_RESET,         0x43),
	LN2_FUNC_PIN(DSP_RESET,         0x44),
	LN_FUNC("dsp-irq",         PIN, 0x45),
	LN2_FUNC_PIN(DSP_STANDBY,       0x46),
	LN2_FUNC_PIN(CDC_PDMCLK1,       0x90),
	LN2_FUNC_PIN(CDC_PDMDAT1,       0x91),
	LN2_FUNC_PIN(CDC_PDMCLK2,       0x92),
	LN2_FUNC_PIN(CDC_PDMDAT2,       0x93),
	LN2_FUNC_PIN(CDC_DMICCLK1,      0xA0),
	LN2_FUNC_PIN(CDC_DMICDAT1,      0xA1),
	LN2_FUNC_PIN(CDC_DMICCLK2,      0xA2),
	LN2_FUNC_PIN(CDC_DMICDAT2,      0xA3),
	LN2_FUNC_PIN(CDC_DMICCLK3,      0xA4),
	LN2_FUNC_PIN(CDC_DMICDAT3,      0xA5),
	LN2_FUNC_PIN(CDC_DMICCLK4,      0xA6),
	LN2_FUNC_PIN(CDC_DMICDAT4,      0xA7),
	LN2_FUNC_PIN(DSP_DMICCLK1,      0xA8),
	LN2_FUNC_PIN(DSP_DMICDAT1,      0xA9),
	LN2_FUNC_PIN(DSP_DMICCLK2,      0xAA),
	LN2_FUNC_PIN(DSP_DMICDAT2,      0xAB),
	LN2_FUNC_PIN(DSP_UART1_RX,      0xC0),
	LN2_FUNC_PIN(DSP_UART1_TX,      0xC1),
	LN2_FUNC_PIN(DSP_UART2_RX,      0xC2),
	LN2_FUNC_PIN(DSP_UART2_TX,      0xC3),
	LN2_FUNC_PIN(GF_UART2_RX,       0xC4),
	LN2_FUNC_PIN(GF_UART2_TX,       0xC5),
	LN2_FUNC_PIN(USB_UART_RX,       0xC6),
	LN_FUNC("usb-uart-tx",     PIN, 0xC7),
	LN2_FUNC_PIN(I2C2_SCL,          0xE0),
	LN2_FUNC_PIN(I2C2_SDA,          0xE1),
	LN2_FUNC_PIN(I2C3_SCL,          0xE2),
	LN2_FUNC_PIN(I2C3_SDA,          0xE3),
	LN2_FUNC_PIN(I2C4_SCL,          0xE4),
	LN2_FUNC_PIN(I2C4_SDA,          0xE5),

	LN2_FUNC_AIF(SPDIF_AIF,         0x01),
	LN2_FUNC_GAI(PSIA1,             0x02, 0x50, 0x51, 0x52, 0x53),
	LN2_FUNC_GAI(PSIA2,             0x03, 0x54, 0x55, 0x56, 0x57),
	LN2_FUNC_GAI(CDC_AIF1,          0x04, 0x59, 0x5B, 0x5A, 0x58),
	LN2_FUNC_GAI(CDC_AIF2,          0x05, 0x5D, 0x5F, 0x5E, 0x5C),
	LN2_FUNC_GAI(CDC_AIF3,          0x06, 0x61, 0x62, 0x63, 0x60),
	LN2_FUNC_GAI(DSP_AIF1,          0x07, 0x65, 0x67, 0x66, 0x64),
	LN2_FUNC_GAI(DSP_AIF2,          0x08, 0x69, 0x6B, 0x6A, 0x68),
	LN2_FUNC_GAI(GF_AIF3,           0x09, 0x6D, 0x6F, 0x6C, 0x6E),
	LN2_FUNC_GAI(GF_AIF4,           0x0A, 0x71, 0x73, 0x70, 0x72),
	LN2_FUNC_GAI(GF_AIF1,           0x0B, 0x75, 0x77, 0x74, 0x76),
	LN2_FUNC_GAI(GF_AIF2,           0x0C, 0x79, 0x7B, 0x78, 0x7A),
	LN2_FUNC_AIF(USB_AIF1,          0x0D),
	LN2_FUNC_AIF(USB_AIF2,          0x0E),
	LN2_FUNC_AIF(ADAT_AIF,          0x0F),
	LN2_FUNC_AIF(SOUNDCARD_AIF,     0x10),
};

#define LN_GROUP_PIN(REV, ID) { \
	.name = lochnagar##REV##_##ID##_pin.name, \
	.type = LN_FTYPE_PIN, \
	.pins = &lochnagar##REV##_pins[LOCHNAGAR##REV##_PIN_##ID].number, \
	.npins = 1, \
	.priv = &lochnagar##REV##_pins[LOCHNAGAR##REV##_PIN_##ID], \
}

#define LN_GROUP_AIF(REV, ID) { \
	.name = lochnagar##REV##_##ID##_aif.name, \
	.type = LN_FTYPE_AIF, \
	.pins = lochnagar##REV##_##ID##_aif.pins, \
	.npins = ARRAY_SIZE(lochnagar##REV##_##ID##_aif.pins), \
	.priv = &lochnagar##REV##_##ID##_aif, \
}

#define LN1_GROUP_PIN(ID) LN_GROUP_PIN(1, ID)
#define LN2_GROUP_PIN(ID) LN_GROUP_PIN(2, ID)

#define LN1_GROUP_AIF(ID) LN_GROUP_AIF(1, ID)
#define LN2_GROUP_AIF(ID) LN_GROUP_AIF(2, ID)

#define LN2_GROUP_GAI(ID) \
	LN2_GROUP_AIF(ID), \
	LN2_GROUP_PIN(ID##_BCLK), LN2_GROUP_PIN(ID##_LRCLK), \
	LN2_GROUP_PIN(ID##_RXDAT), LN2_GROUP_PIN(ID##_TXDAT)

struct lochnagar_group {
	const char * const name;

	enum lochnagar_func_type type;

	const unsigned int *pins;
	unsigned int npins;

	const void *priv;
};

static const struct lochnagar_group lochnagar1_groups[] = {
	LN1_GROUP_PIN(GF_GPIO2),       LN1_GROUP_PIN(GF_GPIO3),
	LN1_GROUP_PIN(GF_GPIO7),
	LN1_GROUP_PIN(LED1),           LN1_GROUP_PIN(LED2),
	LN1_GROUP_AIF(CDC_AIF1),       LN1_GROUP_AIF(CDC_AIF2),
	LN1_GROUP_AIF(CDC_AIF3),
	LN1_GROUP_AIF(DSP_AIF1),       LN1_GROUP_AIF(DSP_AIF2),
	LN1_GROUP_AIF(PSIA1),          LN1_GROUP_AIF(PSIA2),
	LN1_GROUP_AIF(GF_AIF1),        LN1_GROUP_AIF(GF_AIF2),
	LN1_GROUP_AIF(GF_AIF3),        LN1_GROUP_AIF(GF_AIF4),
	LN1_GROUP_AIF(SPDIF_AIF),
};

static const struct lochnagar_group lochnagar2_groups[] = {
	LN2_GROUP_PIN(FPGA_GPIO1),     LN2_GROUP_PIN(FPGA_GPIO2),
	LN2_GROUP_PIN(FPGA_GPIO3),     LN2_GROUP_PIN(FPGA_GPIO4),
	LN2_GROUP_PIN(FPGA_GPIO5),     LN2_GROUP_PIN(FPGA_GPIO6),
	LN2_GROUP_PIN(CDC_GPIO1),      LN2_GROUP_PIN(CDC_GPIO2),
	LN2_GROUP_PIN(CDC_GPIO3),      LN2_GROUP_PIN(CDC_GPIO4),
	LN2_GROUP_PIN(CDC_GPIO5),      LN2_GROUP_PIN(CDC_GPIO6),
	LN2_GROUP_PIN(CDC_GPIO7),      LN2_GROUP_PIN(CDC_GPIO8),
	LN2_GROUP_PIN(DSP_GPIO1),      LN2_GROUP_PIN(DSP_GPIO2),
	LN2_GROUP_PIN(DSP_GPIO3),      LN2_GROUP_PIN(DSP_GPIO4),
	LN2_GROUP_PIN(DSP_GPIO5),      LN2_GROUP_PIN(DSP_GPIO6),
	LN2_GROUP_PIN(DSP_GPIO20),
	LN2_GROUP_PIN(GF_GPIO1),
	LN2_GROUP_PIN(GF_GPIO2),       LN2_GROUP_PIN(GF_GPIO5),
	LN2_GROUP_PIN(GF_GPIO3),       LN2_GROUP_PIN(GF_GPIO7),
	LN2_GROUP_PIN(DSP_UART1_RX),   LN2_GROUP_PIN(DSP_UART1_TX),
	LN2_GROUP_PIN(DSP_UART2_RX),   LN2_GROUP_PIN(DSP_UART2_TX),
	LN2_GROUP_PIN(GF_UART2_RX),    LN2_GROUP_PIN(GF_UART2_TX),
	LN2_GROUP_PIN(USB_UART_RX),
	LN2_GROUP_PIN(CDC_PDMCLK1),    LN2_GROUP_PIN(CDC_PDMDAT1),
	LN2_GROUP_PIN(CDC_PDMCLK2),    LN2_GROUP_PIN(CDC_PDMDAT2),
	LN2_GROUP_PIN(CDC_DMICCLK1),   LN2_GROUP_PIN(CDC_DMICDAT1),
	LN2_GROUP_PIN(CDC_DMICCLK2),   LN2_GROUP_PIN(CDC_DMICDAT2),
	LN2_GROUP_PIN(CDC_DMICCLK3),   LN2_GROUP_PIN(CDC_DMICDAT3),
	LN2_GROUP_PIN(CDC_DMICCLK4),   LN2_GROUP_PIN(CDC_DMICDAT4),
	LN2_GROUP_PIN(DSP_DMICCLK1),   LN2_GROUP_PIN(DSP_DMICDAT1),
	LN2_GROUP_PIN(DSP_DMICCLK2),   LN2_GROUP_PIN(DSP_DMICDAT2),
	LN2_GROUP_PIN(I2C2_SCL),       LN2_GROUP_PIN(I2C2_SDA),
	LN2_GROUP_PIN(I2C3_SCL),       LN2_GROUP_PIN(I2C3_SDA),
	LN2_GROUP_PIN(I2C4_SCL),       LN2_GROUP_PIN(I2C4_SDA),
	LN2_GROUP_PIN(DSP_STANDBY),
	LN2_GROUP_PIN(CDC_MCLK1),      LN2_GROUP_PIN(CDC_MCLK2),
	LN2_GROUP_PIN(DSP_CLKIN),
	LN2_GROUP_PIN(PSIA1_MCLK),     LN2_GROUP_PIN(PSIA2_MCLK),
	LN2_GROUP_GAI(CDC_AIF1),       LN2_GROUP_GAI(CDC_AIF2),
	LN2_GROUP_GAI(CDC_AIF3),
	LN2_GROUP_GAI(DSP_AIF1),       LN2_GROUP_GAI(DSP_AIF2),
	LN2_GROUP_GAI(PSIA1),          LN2_GROUP_GAI(PSIA2),
	LN2_GROUP_GAI(GF_AIF1),        LN2_GROUP_GAI(GF_AIF2),
	LN2_GROUP_GAI(GF_AIF3),        LN2_GROUP_GAI(GF_AIF4),
	LN2_GROUP_AIF(SPDIF_AIF),
	LN2_GROUP_AIF(USB_AIF1),       LN2_GROUP_AIF(USB_AIF2),
	LN2_GROUP_AIF(ADAT_AIF),
	LN2_GROUP_AIF(SOUNDCARD_AIF),
};

struct lochnagar_func_groups {
	const char **groups;
	unsigned int ngroups;
};

struct lochnagar_pin_priv {
	struct lochnagar *lochnagar;
	struct device *dev;

	const struct lochnagar_func *funcs;
	unsigned int nfuncs;

	const struct pinctrl_pin_desc *pins;
	unsigned int npins;

	const struct lochnagar_group *groups;
	unsigned int ngroups;

	struct lochnagar_func_groups func_groups[LN_FTYPE_COUNT];

	struct gpio_chip gpio_chip;
};

static int lochnagar_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->ngroups;
}

static const char *lochnagar_get_group_name(struct pinctrl_dev *pctldev,
					    unsigned int group_idx)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->groups[group_idx].name;
}

static int lochnagar_get_group_pins(struct pinctrl_dev *pctldev,
				    unsigned int group_idx,
				    const unsigned int **pins,
				    unsigned int *num_pins)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	*pins = priv->groups[group_idx].pins;
	*num_pins = priv->groups[group_idx].npins;

	return 0;
}

static const struct pinctrl_ops lochnagar_pin_group_ops = {
	.get_groups_count = lochnagar_get_groups_count,
	.get_group_name = lochnagar_get_group_name,
	.get_group_pins = lochnagar_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

static int lochnagar_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->nfuncs;
}

static const char *lochnagar_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned int func_idx)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->funcs[func_idx].name;
}

static int lochnagar_get_func_groups(struct pinctrl_dev *pctldev,
				     unsigned int func_idx,
				     const char * const **groups,
				     unsigned int * const num_groups)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	int func_type;

	func_type = priv->funcs[func_idx].type;

	*groups = priv->func_groups[func_type].groups;
	*num_groups = priv->func_groups[func_type].ngroups;

	return 0;
}

static int lochnagar2_get_gpio_chan(struct lochnagar_pin_priv *priv,
				    unsigned int op)
{
	struct regmap *regmap = priv->lochnagar->regmap;
	unsigned int val;
	int free = -1;
	int i, ret;

	for (i = 0; i < LN2_NUM_GPIO_CHANNELS; i++) {
		ret = regmap_read(regmap, LOCHNAGAR2_GPIO_CHANNEL1 + i, &val);
		if (ret)
			return ret;

		val &= LOCHNAGAR2_GPIO_CHANNEL_SRC_MASK;

		if (val == op)
			return i + 1;

		if (free < 0 && !val)
			free = i;
	}

	if (free >= 0) {
		ret = regmap_update_bits(regmap,
					 LOCHNAGAR2_GPIO_CHANNEL1 + free,
					 LOCHNAGAR2_GPIO_CHANNEL_SRC_MASK, op);
		if (ret)
			return ret;

		free++;

		dev_dbg(priv->dev, "Set channel %d to 0x%x\n", free, op);

		return free;
	}

	return -ENOSPC;
}

static int lochnagar_pin_set_mux(struct lochnagar_pin_priv *priv,
				 const struct lochnagar_pin *pin,
				 unsigned int op)
{
	int ret;

	switch (priv->lochnagar->type) {
	case LOCHNAGAR1:
		break;
	default:
		ret = lochnagar2_get_gpio_chan(priv, op);
		if (ret < 0) {
			dev_err(priv->dev, "Failed to get channel for %s: %d\n",
				pin->name, ret);
			return ret;
		}

		op = ret;
		break;
	}

	dev_dbg(priv->dev, "Set pin %s to 0x%x\n", pin->name, op);

	ret = regmap_write(priv->lochnagar->regmap, pin->reg, op);
	if (ret)
		dev_err(priv->dev, "Failed to set %s mux: %d\n",
			pin->name, ret);

	return 0;
}

static int lochnagar_aif_set_mux(struct lochnagar_pin_priv *priv,
				 const struct lochnagar_group *group,
				 unsigned int op)
{
	struct regmap *regmap = priv->lochnagar->regmap;
	const struct lochnagar_aif *aif = group->priv;
	const struct lochnagar_pin *pin;
	int i, ret;

	ret = regmap_update_bits(regmap, aif->src_reg, aif->src_mask, op);
	if (ret) {
		dev_err(priv->dev, "Failed to set %s source: %d\n",
			group->name, ret);
		return ret;
	}

	ret = regmap_update_bits(regmap, aif->ctrl_reg,
				 aif->ena_mask, aif->ena_mask);
	if (ret) {
		dev_err(priv->dev, "Failed to set %s enable: %d\n",
			group->name, ret);
		return ret;
	}

	for (i = 0; i < group->npins; i++) {
		pin = priv->pins[group->pins[i]].drv_data;

		if (pin->type != LN_PTYPE_MUX)
			continue;

		dev_dbg(priv->dev, "Set pin %s to AIF\n", pin->name);

		ret = regmap_update_bits(regmap, pin->reg,
					 LOCHNAGAR2_GPIO_SRC_MASK,
					 LN2_OP_AIF);
		if (ret) {
			dev_err(priv->dev, "Failed to set %s to AIF: %d\n",
				pin->name, ret);
			return ret;
		}
	}

	return 0;
}

static int lochnagar_set_mux(struct pinctrl_dev *pctldev,
			     unsigned int func_idx, unsigned int group_idx)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct lochnagar_func *func = &priv->funcs[func_idx];
	const struct lochnagar_group *group = &priv->groups[group_idx];
	const struct lochnagar_pin *pin;

	switch (func->type) {
	case LN_FTYPE_AIF:
		dev_dbg(priv->dev, "Set group %s to %s\n",
			group->name, func->name);

		return lochnagar_aif_set_mux(priv, group, func->op);
	case LN_FTYPE_PIN:
		pin = priv->pins[*group->pins].drv_data;

		dev_dbg(priv->dev, "Set pin %s to %s\n", pin->name, func->name);

		return lochnagar_pin_set_mux(priv, pin, func->op);
	default:
		return -EINVAL;
	}
}

static int lochnagar_gpio_request(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	struct lochnagar *lochnagar = priv->lochnagar;
	const struct lochnagar_pin *pin = priv->pins[offset].drv_data;
	int ret;

	dev_dbg(priv->dev, "Requesting GPIO %s\n", pin->name);

	if (lochnagar->type == LOCHNAGAR1 || pin->type != LN_PTYPE_MUX)
		return 0;

	ret = lochnagar2_get_gpio_chan(priv, LN2_OP_GPIO);
	if (ret < 0) {
		dev_err(priv->dev, "Failed to get low channel: %d\n", ret);
		return ret;
	}

	ret = lochnagar2_get_gpio_chan(priv, LN2_OP_GPIO | 0x1);
	if (ret < 0) {
		dev_err(priv->dev, "Failed to get high channel: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lochnagar_gpio_set_direction(struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned int offset,
					bool input)
{
	/* The GPIOs only support output */
	if (input)
		return -EINVAL;

	return 0;
}

static const struct pinmux_ops lochnagar_pin_mux_ops = {
	.get_functions_count = lochnagar_get_funcs_count,
	.get_function_name = lochnagar_get_func_name,
	.get_function_groups = lochnagar_get_func_groups,
	.set_mux = lochnagar_set_mux,

	.gpio_request_enable = lochnagar_gpio_request,
	.gpio_set_direction = lochnagar_gpio_set_direction,

	.strict = true,
};

static int lochnagar_aif_set_master(struct lochnagar_pin_priv *priv,
				    unsigned int group_idx, bool master)
{
	struct regmap *regmap = priv->lochnagar->regmap;
	const struct lochnagar_group *group = &priv->groups[group_idx];
	const struct lochnagar_aif *aif = group->priv;
	unsigned int val = 0;
	int ret;

	if (group->type != LN_FTYPE_AIF)
		return -EINVAL;

	if (!master)
		val = aif->master_mask;

	dev_dbg(priv->dev, "Set AIF %s to %s\n",
		group->name, master ? "master" : "slave");

	ret = regmap_update_bits(regmap, aif->ctrl_reg, aif->master_mask, val);
	if (ret) {
		dev_err(priv->dev, "Failed to set %s mode: %d\n",
			group->name, ret);
		return ret;
	}

	return 0;
}

static int lochnagar_conf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int group_idx,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	struct lochnagar_pin_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		unsigned int param = pinconf_to_config_param(*configs);

		switch (param) {
		case PIN_CONFIG_OUTPUT_ENABLE:
			ret = lochnagar_aif_set_master(priv, group_idx, true);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			ret = lochnagar_aif_set_master(priv, group_idx, false);
			if (ret)
				return ret;
			break;
		default:
			return -ENOTSUPP;
		}

		configs++;
	}

	return 0;
}

static const struct pinconf_ops lochnagar_pin_conf_ops = {
	.pin_config_group_set = lochnagar_conf_group_set,
};

static const struct pinctrl_desc lochnagar_pin_desc = {
	.name = "lochnagar-pinctrl",
	.owner = THIS_MODULE,

	.pctlops = &lochnagar_pin_group_ops,
	.pmxops = &lochnagar_pin_mux_ops,
	.confops = &lochnagar_pin_conf_ops,
};

static void lochnagar_gpio_set(struct gpio_chip *chip,
			       unsigned int offset, int value)
{
	struct lochnagar_pin_priv *priv = gpiochip_get_data(chip);
	struct lochnagar *lochnagar = priv->lochnagar;
	const struct lochnagar_pin *pin = priv->pins[offset].drv_data;
	int ret;

	value = !!value;

	dev_dbg(priv->dev, "Set GPIO %s to %s\n",
		pin->name, str_high_low(value));

	switch (pin->type) {
	case LN_PTYPE_MUX:
		value |= LN2_OP_GPIO;

		ret = lochnagar_pin_set_mux(priv, pin, value);
		break;
	case LN_PTYPE_GPIO:
		if (pin->invert)
			value = !value;

		ret = regmap_update_bits(lochnagar->regmap, pin->reg,
					 BIT(pin->shift), value << pin->shift);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0)
		dev_err(chip->parent, "Failed to set %s value: %d\n",
			pin->name, ret);
}

static int lochnagar_gpio_direction_out(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	lochnagar_gpio_set(chip, offset, value);

	return pinctrl_gpio_direction_output(chip, offset);
}

static int lochnagar_fill_func_groups(struct lochnagar_pin_priv *priv)
{
	struct lochnagar_func_groups *funcs;
	int i;

	for (i = 0; i < priv->ngroups; i++)
		priv->func_groups[priv->groups[i].type].ngroups++;

	for (i = 0; i < LN_FTYPE_COUNT; i++) {
		funcs = &priv->func_groups[i];

		if (!funcs->ngroups)
			continue;

		funcs->groups = devm_kcalloc(priv->dev, funcs->ngroups,
					     sizeof(*funcs->groups),
					     GFP_KERNEL);
		if (!funcs->groups)
			return -ENOMEM;

		funcs->ngroups = 0;
	}

	for (i = 0; i < priv->ngroups; i++) {
		funcs = &priv->func_groups[priv->groups[i].type];

		funcs->groups[funcs->ngroups++] = priv->groups[i].name;
	}

	return 0;
}

static int lochnagar_pin_probe(struct platform_device *pdev)
{
	struct lochnagar *lochnagar = dev_get_drvdata(pdev->dev.parent);
	struct lochnagar_pin_priv *priv;
	struct pinctrl_desc *desc;
	struct pinctrl_dev *pctl;
	struct device *dev = &pdev->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->lochnagar = lochnagar;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	*desc = lochnagar_pin_desc;

	priv->gpio_chip.label = dev_name(dev);
	priv->gpio_chip.request = gpiochip_generic_request;
	priv->gpio_chip.free = gpiochip_generic_free;
	priv->gpio_chip.direction_output = lochnagar_gpio_direction_out;
	priv->gpio_chip.set = lochnagar_gpio_set;
	priv->gpio_chip.can_sleep = true;
	priv->gpio_chip.parent = dev;
	priv->gpio_chip.base = -1;

	switch (lochnagar->type) {
	case LOCHNAGAR1:
		priv->funcs = lochnagar1_funcs;
		priv->nfuncs = ARRAY_SIZE(lochnagar1_funcs);
		priv->pins = lochnagar1_pins;
		priv->npins = ARRAY_SIZE(lochnagar1_pins);
		priv->groups = lochnagar1_groups;
		priv->ngroups = ARRAY_SIZE(lochnagar1_groups);

		priv->gpio_chip.ngpio = LOCHNAGAR1_PIN_NUM_GPIOS;
		break;
	case LOCHNAGAR2:
		priv->funcs = lochnagar2_funcs;
		priv->nfuncs = ARRAY_SIZE(lochnagar2_funcs);
		priv->pins = lochnagar2_pins;
		priv->npins = ARRAY_SIZE(lochnagar2_pins);
		priv->groups = lochnagar2_groups;
		priv->ngroups = ARRAY_SIZE(lochnagar2_groups);

		priv->gpio_chip.ngpio = LOCHNAGAR2_PIN_NUM_GPIOS;
		break;
	default:
		dev_err(dev, "Unknown Lochnagar type: %d\n", lochnagar->type);
		return -EINVAL;
	}

	ret = lochnagar_fill_func_groups(priv);
	if (ret < 0)
		return ret;

	desc->pins = priv->pins;
	desc->npins = priv->npins;

	pctl = devm_pinctrl_register(dev, desc, priv);
	if (IS_ERR(pctl)) {
		ret = PTR_ERR(pctl);
		dev_err(priv->dev, "Failed to register pinctrl: %d\n", ret);
		return ret;
	}

	ret = devm_gpiochip_add_data(dev, &priv->gpio_chip, priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register gpiochip: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id lochnagar_of_match[] = {
	{ .compatible = "cirrus,lochnagar-pinctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, lochnagar_of_match);

static struct platform_driver lochnagar_pin_driver = {
	.driver = {
		.name = "lochnagar-pinctrl",
		.of_match_table = of_match_ptr(lochnagar_of_match),
	},

	.probe = lochnagar_pin_probe,
};
module_platform_driver(lochnagar_pin_driver);

MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_DESCRIPTION("Pinctrl driver for Cirrus Logic Lochnagar Board");
MODULE_LICENSE("GPL v2");
