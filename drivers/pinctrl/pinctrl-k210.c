// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include <dt-bindings/pinctrl/k210-fpioa.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

/*
 * The K210 only implements 8 drive levels, even though
 * there is register space for 16
 */
#define K210_PC_DRIVE_MASK	GENMASK(11, 8)
#define K210_PC_DRIVE_SHIFT	8
#define K210_PC_DRIVE_0		(0 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_1		(1 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_2		(2 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_3		(3 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_4		(4 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_5		(5 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_6		(6 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_7		(7 << K210_PC_DRIVE_SHIFT)
#define K210_PC_DRIVE_MAX	7
#define K210_PC_MODE_MASK	GENMASK(23, 12)

/*
 * output enabled == PC_OE & (PC_OE_INV ^ FUNCTION_OE)
 * where FUNCTION_OE is a physical signal from the function.
 */
#define K210_PC_OE		BIT(12) /* Output Enable */
#define K210_PC_OE_INV		BIT(13) /* INVert Output Enable */
#define K210_PC_DO_OE		BIT(14) /* set Data Out to Output Enable sig */
#define K210_PC_DO_INV		BIT(15) /* INVert final Data Output */
#define K210_PC_PU		BIT(16) /* Pull Up */
#define K210_PC_PD		BIT(17) /* Pull Down */
/* Strong pull up not implemented on K210 */
#define K210_PC_SL		BIT(19) /* reduce SLew rate */
/* Same semantics as OE above */
#define K210_PC_IE		BIT(20) /* Input Enable */
#define K210_PC_IE_INV		BIT(21) /* INVert Input Enable */
#define K210_PC_DI_INV		BIT(22) /* INVert Data Input */
#define K210_PC_ST		BIT(23) /* Schmitt Trigger */
#define K210_PC_DI		BIT(31) /* raw Data Input */

#define K210_PC_BIAS_MASK	(K210_PC_PU & K210_PC_PD)

#define K210_PC_MODE_IN		(K210_PC_IE | K210_PC_ST)
#define K210_PC_MODE_OUT	(K210_PC_DRIVE_7 | K210_PC_OE)
#define K210_PC_MODE_I2C	(K210_PC_MODE_IN | K210_PC_SL | \
				 K210_PC_OE | K210_PC_PU)
#define K210_PC_MODE_SCCB	(K210_PC_MODE_I2C | \
				 K210_PC_OE_INV | K210_PC_IE_INV)
#define K210_PC_MODE_SPI	(K210_PC_MODE_IN | K210_PC_IE_INV | \
				 K210_PC_MODE_OUT | K210_PC_OE_INV)
#define K210_PC_MODE_GPIO	(K210_PC_MODE_IN | K210_PC_MODE_OUT)

#define K210_PG_FUNC		GENMASK(7, 0)
#define K210_PG_DO		BIT(8)
#define K210_PG_PIN		GENMASK(22, 16)

/*
 * struct k210_fpioa: Kendryte K210 FPIOA memory mapped registers
 * @pins: 48 32-bits IO pin registers
 * @tie_en: 256 (one per function) input tie enable bits
 * @tie_val: 256 (one per function) input tie value bits
 */
struct k210_fpioa {
	u32 pins[48];
	u32 tie_en[8];
	u32 tie_val[8];
};

struct k210_fpioa_data {

	struct device *dev;
	struct pinctrl_dev *pctl;

	struct k210_fpioa __iomem *fpioa;
	struct regmap *sysctl_map;
	u32 power_offset;
	struct clk *clk;
	struct clk *pclk;
};

#define K210_PIN_NAME(i)	("IO_" #i)
#define K210_PIN(i)		[(i)] = PINCTRL_PIN((i), K210_PIN_NAME(i))

static const struct pinctrl_pin_desc k210_pins[] = {
	K210_PIN(0),  K210_PIN(1),  K210_PIN(2),
	K210_PIN(3),  K210_PIN(4),  K210_PIN(5),
	K210_PIN(6),  K210_PIN(7),  K210_PIN(8),
	K210_PIN(9),  K210_PIN(10), K210_PIN(11),
	K210_PIN(12), K210_PIN(13), K210_PIN(14),
	K210_PIN(15), K210_PIN(16), K210_PIN(17),
	K210_PIN(18), K210_PIN(19), K210_PIN(20),
	K210_PIN(21), K210_PIN(22), K210_PIN(23),
	K210_PIN(24), K210_PIN(25), K210_PIN(26),
	K210_PIN(27), K210_PIN(28), K210_PIN(29),
	K210_PIN(30), K210_PIN(31), K210_PIN(32),
	K210_PIN(33), K210_PIN(34), K210_PIN(35),
	K210_PIN(36), K210_PIN(37), K210_PIN(38),
	K210_PIN(39), K210_PIN(40), K210_PIN(41),
	K210_PIN(42), K210_PIN(43), K210_PIN(44),
	K210_PIN(45), K210_PIN(46), K210_PIN(47)
};

#define K210_NPINS ARRAY_SIZE(k210_pins)

/*
 * Pin groups: each of the 48 programmable pins is a group.
 * To this are added 8 power domain groups, which for the purposes of
 * the pin subsystem, contain no pins. The power domain groups only exist
 * to set the power level. The id should never be used (since there are
 * no pins 48-55).
 */
static const char *const k210_group_names[] = {
	/* The first 48 groups are for pins, one each */
	K210_PIN_NAME(0),  K210_PIN_NAME(1),  K210_PIN_NAME(2),
	K210_PIN_NAME(3),  K210_PIN_NAME(4),  K210_PIN_NAME(5),
	K210_PIN_NAME(6),  K210_PIN_NAME(7),  K210_PIN_NAME(8),
	K210_PIN_NAME(9),  K210_PIN_NAME(10), K210_PIN_NAME(11),
	K210_PIN_NAME(12), K210_PIN_NAME(13), K210_PIN_NAME(14),
	K210_PIN_NAME(15), K210_PIN_NAME(16), K210_PIN_NAME(17),
	K210_PIN_NAME(18), K210_PIN_NAME(19), K210_PIN_NAME(20),
	K210_PIN_NAME(21), K210_PIN_NAME(22), K210_PIN_NAME(23),
	K210_PIN_NAME(24), K210_PIN_NAME(25), K210_PIN_NAME(26),
	K210_PIN_NAME(27), K210_PIN_NAME(28), K210_PIN_NAME(29),
	K210_PIN_NAME(30), K210_PIN_NAME(31), K210_PIN_NAME(32),
	K210_PIN_NAME(33), K210_PIN_NAME(34), K210_PIN_NAME(35),
	K210_PIN_NAME(36), K210_PIN_NAME(37), K210_PIN_NAME(38),
	K210_PIN_NAME(39), K210_PIN_NAME(40), K210_PIN_NAME(41),
	K210_PIN_NAME(42), K210_PIN_NAME(43), K210_PIN_NAME(44),
	K210_PIN_NAME(45), K210_PIN_NAME(46), K210_PIN_NAME(47),
	[48] = "A0", [49] = "A1", [50] = "A2",
	[51] = "B3", [52] = "B4", [53] = "B5",
	[54] = "C6", [55] = "C7"
};

#define K210_NGROUPS	ARRAY_SIZE(k210_group_names)

enum k210_pinctrl_mode_id {
	K210_PC_DEFAULT_DISABLED,
	K210_PC_DEFAULT_IN,
	K210_PC_DEFAULT_IN_TIE,
	K210_PC_DEFAULT_OUT,
	K210_PC_DEFAULT_I2C,
	K210_PC_DEFAULT_SCCB,
	K210_PC_DEFAULT_SPI,
	K210_PC_DEFAULT_GPIO,
	K210_PC_DEFAULT_INT13,
};

#define K210_PC_DEFAULT(mode) \
	[K210_PC_DEFAULT_##mode] = K210_PC_MODE_##mode

static const u32 k210_pinconf_mode_id_to_mode[] = {
	[K210_PC_DEFAULT_DISABLED] = 0,
	K210_PC_DEFAULT(IN),
	[K210_PC_DEFAULT_IN_TIE] = K210_PC_MODE_IN,
	K210_PC_DEFAULT(OUT),
	K210_PC_DEFAULT(I2C),
	K210_PC_DEFAULT(SCCB),
	K210_PC_DEFAULT(SPI),
	K210_PC_DEFAULT(GPIO),
	[K210_PC_DEFAULT_INT13] = K210_PC_MODE_IN | K210_PC_PU,
};

#undef DEFAULT

/*
 * Pin functions configuration information.
 */
struct k210_pcf_info {
	char name[15];
	u8 mode_id;
};

#define K210_FUNC(id, mode)				\
	[K210_PCF_##id] = {				\
		.name = #id,				\
		.mode_id = K210_PC_DEFAULT_##mode	\
	}

static const struct k210_pcf_info k210_pcf_infos[] = {
	K210_FUNC(JTAG_TCLK,		IN),
	K210_FUNC(JTAG_TDI,		IN),
	K210_FUNC(JTAG_TMS,		IN),
	K210_FUNC(JTAG_TDO,		OUT),
	K210_FUNC(SPI0_D0,		SPI),
	K210_FUNC(SPI0_D1,		SPI),
	K210_FUNC(SPI0_D2,		SPI),
	K210_FUNC(SPI0_D3,		SPI),
	K210_FUNC(SPI0_D4,		SPI),
	K210_FUNC(SPI0_D5,		SPI),
	K210_FUNC(SPI0_D6,		SPI),
	K210_FUNC(SPI0_D7,		SPI),
	K210_FUNC(SPI0_SS0,		OUT),
	K210_FUNC(SPI0_SS1,		OUT),
	K210_FUNC(SPI0_SS2,		OUT),
	K210_FUNC(SPI0_SS3,		OUT),
	K210_FUNC(SPI0_ARB,		IN_TIE),
	K210_FUNC(SPI0_SCLK,		OUT),
	K210_FUNC(UARTHS_RX,		IN),
	K210_FUNC(UARTHS_TX,		OUT),
	K210_FUNC(RESV6,		IN),
	K210_FUNC(RESV7,		IN),
	K210_FUNC(CLK_SPI1,		OUT),
	K210_FUNC(CLK_I2C1,		OUT),
	K210_FUNC(GPIOHS0,		GPIO),
	K210_FUNC(GPIOHS1,		GPIO),
	K210_FUNC(GPIOHS2,		GPIO),
	K210_FUNC(GPIOHS3,		GPIO),
	K210_FUNC(GPIOHS4,		GPIO),
	K210_FUNC(GPIOHS5,		GPIO),
	K210_FUNC(GPIOHS6,		GPIO),
	K210_FUNC(GPIOHS7,		GPIO),
	K210_FUNC(GPIOHS8,		GPIO),
	K210_FUNC(GPIOHS9,		GPIO),
	K210_FUNC(GPIOHS10,		GPIO),
	K210_FUNC(GPIOHS11,		GPIO),
	K210_FUNC(GPIOHS12,		GPIO),
	K210_FUNC(GPIOHS13,		GPIO),
	K210_FUNC(GPIOHS14,		GPIO),
	K210_FUNC(GPIOHS15,		GPIO),
	K210_FUNC(GPIOHS16,		GPIO),
	K210_FUNC(GPIOHS17,		GPIO),
	K210_FUNC(GPIOHS18,		GPIO),
	K210_FUNC(GPIOHS19,		GPIO),
	K210_FUNC(GPIOHS20,		GPIO),
	K210_FUNC(GPIOHS21,		GPIO),
	K210_FUNC(GPIOHS22,		GPIO),
	K210_FUNC(GPIOHS23,		GPIO),
	K210_FUNC(GPIOHS24,		GPIO),
	K210_FUNC(GPIOHS25,		GPIO),
	K210_FUNC(GPIOHS26,		GPIO),
	K210_FUNC(GPIOHS27,		GPIO),
	K210_FUNC(GPIOHS28,		GPIO),
	K210_FUNC(GPIOHS29,		GPIO),
	K210_FUNC(GPIOHS30,		GPIO),
	K210_FUNC(GPIOHS31,		GPIO),
	K210_FUNC(GPIO0,		GPIO),
	K210_FUNC(GPIO1,		GPIO),
	K210_FUNC(GPIO2,		GPIO),
	K210_FUNC(GPIO3,		GPIO),
	K210_FUNC(GPIO4,		GPIO),
	K210_FUNC(GPIO5,		GPIO),
	K210_FUNC(GPIO6,		GPIO),
	K210_FUNC(GPIO7,		GPIO),
	K210_FUNC(UART1_RX,		IN),
	K210_FUNC(UART1_TX,		OUT),
	K210_FUNC(UART2_RX,		IN),
	K210_FUNC(UART2_TX,		OUT),
	K210_FUNC(UART3_RX,		IN),
	K210_FUNC(UART3_TX,		OUT),
	K210_FUNC(SPI1_D0,		SPI),
	K210_FUNC(SPI1_D1,		SPI),
	K210_FUNC(SPI1_D2,		SPI),
	K210_FUNC(SPI1_D3,		SPI),
	K210_FUNC(SPI1_D4,		SPI),
	K210_FUNC(SPI1_D5,		SPI),
	K210_FUNC(SPI1_D6,		SPI),
	K210_FUNC(SPI1_D7,		SPI),
	K210_FUNC(SPI1_SS0,		OUT),
	K210_FUNC(SPI1_SS1,		OUT),
	K210_FUNC(SPI1_SS2,		OUT),
	K210_FUNC(SPI1_SS3,		OUT),
	K210_FUNC(SPI1_ARB,		IN_TIE),
	K210_FUNC(SPI1_SCLK,		OUT),
	K210_FUNC(SPI2_D0,		SPI),
	K210_FUNC(SPI2_SS,		IN),
	K210_FUNC(SPI2_SCLK,		IN),
	K210_FUNC(I2S0_MCLK,		OUT),
	K210_FUNC(I2S0_SCLK,		OUT),
	K210_FUNC(I2S0_WS,		OUT),
	K210_FUNC(I2S0_IN_D0,		IN),
	K210_FUNC(I2S0_IN_D1,		IN),
	K210_FUNC(I2S0_IN_D2,		IN),
	K210_FUNC(I2S0_IN_D3,		IN),
	K210_FUNC(I2S0_OUT_D0,		OUT),
	K210_FUNC(I2S0_OUT_D1,		OUT),
	K210_FUNC(I2S0_OUT_D2,		OUT),
	K210_FUNC(I2S0_OUT_D3,		OUT),
	K210_FUNC(I2S1_MCLK,		OUT),
	K210_FUNC(I2S1_SCLK,		OUT),
	K210_FUNC(I2S1_WS,		OUT),
	K210_FUNC(I2S1_IN_D0,		IN),
	K210_FUNC(I2S1_IN_D1,		IN),
	K210_FUNC(I2S1_IN_D2,		IN),
	K210_FUNC(I2S1_IN_D3,		IN),
	K210_FUNC(I2S1_OUT_D0,		OUT),
	K210_FUNC(I2S1_OUT_D1,		OUT),
	K210_FUNC(I2S1_OUT_D2,		OUT),
	K210_FUNC(I2S1_OUT_D3,		OUT),
	K210_FUNC(I2S2_MCLK,		OUT),
	K210_FUNC(I2S2_SCLK,		OUT),
	K210_FUNC(I2S2_WS,		OUT),
	K210_FUNC(I2S2_IN_D0,		IN),
	K210_FUNC(I2S2_IN_D1,		IN),
	K210_FUNC(I2S2_IN_D2,		IN),
	K210_FUNC(I2S2_IN_D3,		IN),
	K210_FUNC(I2S2_OUT_D0,		OUT),
	K210_FUNC(I2S2_OUT_D1,		OUT),
	K210_FUNC(I2S2_OUT_D2,		OUT),
	K210_FUNC(I2S2_OUT_D3,		OUT),
	K210_FUNC(RESV0,		DISABLED),
	K210_FUNC(RESV1,		DISABLED),
	K210_FUNC(RESV2,		DISABLED),
	K210_FUNC(RESV3,		DISABLED),
	K210_FUNC(RESV4,		DISABLED),
	K210_FUNC(RESV5,		DISABLED),
	K210_FUNC(I2C0_SCLK,		I2C),
	K210_FUNC(I2C0_SDA,		I2C),
	K210_FUNC(I2C1_SCLK,		I2C),
	K210_FUNC(I2C1_SDA,		I2C),
	K210_FUNC(I2C2_SCLK,		I2C),
	K210_FUNC(I2C2_SDA,		I2C),
	K210_FUNC(DVP_XCLK,		OUT),
	K210_FUNC(DVP_RST,		OUT),
	K210_FUNC(DVP_PWDN,		OUT),
	K210_FUNC(DVP_VSYNC,		IN),
	K210_FUNC(DVP_HSYNC,		IN),
	K210_FUNC(DVP_PCLK,		IN),
	K210_FUNC(DVP_D0,		IN),
	K210_FUNC(DVP_D1,		IN),
	K210_FUNC(DVP_D2,		IN),
	K210_FUNC(DVP_D3,		IN),
	K210_FUNC(DVP_D4,		IN),
	K210_FUNC(DVP_D5,		IN),
	K210_FUNC(DVP_D6,		IN),
	K210_FUNC(DVP_D7,		IN),
	K210_FUNC(SCCB_SCLK,		SCCB),
	K210_FUNC(SCCB_SDA,		SCCB),
	K210_FUNC(UART1_CTS,		IN),
	K210_FUNC(UART1_DSR,		IN),
	K210_FUNC(UART1_DCD,		IN),
	K210_FUNC(UART1_RI,		IN),
	K210_FUNC(UART1_SIR_IN,		IN),
	K210_FUNC(UART1_DTR,		OUT),
	K210_FUNC(UART1_RTS,		OUT),
	K210_FUNC(UART1_OUT2,		OUT),
	K210_FUNC(UART1_OUT1,		OUT),
	K210_FUNC(UART1_SIR_OUT,	OUT),
	K210_FUNC(UART1_BAUD,		OUT),
	K210_FUNC(UART1_RE,		OUT),
	K210_FUNC(UART1_DE,		OUT),
	K210_FUNC(UART1_RS485_EN,	OUT),
	K210_FUNC(UART2_CTS,		IN),
	K210_FUNC(UART2_DSR,		IN),
	K210_FUNC(UART2_DCD,		IN),
	K210_FUNC(UART2_RI,		IN),
	K210_FUNC(UART2_SIR_IN,		IN),
	K210_FUNC(UART2_DTR,		OUT),
	K210_FUNC(UART2_RTS,		OUT),
	K210_FUNC(UART2_OUT2,		OUT),
	K210_FUNC(UART2_OUT1,		OUT),
	K210_FUNC(UART2_SIR_OUT,	OUT),
	K210_FUNC(UART2_BAUD,		OUT),
	K210_FUNC(UART2_RE,		OUT),
	K210_FUNC(UART2_DE,		OUT),
	K210_FUNC(UART2_RS485_EN,	OUT),
	K210_FUNC(UART3_CTS,		IN),
	K210_FUNC(UART3_DSR,		IN),
	K210_FUNC(UART3_DCD,		IN),
	K210_FUNC(UART3_RI,		IN),
	K210_FUNC(UART3_SIR_IN,		IN),
	K210_FUNC(UART3_DTR,		OUT),
	K210_FUNC(UART3_RTS,		OUT),
	K210_FUNC(UART3_OUT2,		OUT),
	K210_FUNC(UART3_OUT1,		OUT),
	K210_FUNC(UART3_SIR_OUT,	OUT),
	K210_FUNC(UART3_BAUD,		OUT),
	K210_FUNC(UART3_RE,		OUT),
	K210_FUNC(UART3_DE,		OUT),
	K210_FUNC(UART3_RS485_EN,	OUT),
	K210_FUNC(TIMER0_TOGGLE1,	OUT),
	K210_FUNC(TIMER0_TOGGLE2,	OUT),
	K210_FUNC(TIMER0_TOGGLE3,	OUT),
	K210_FUNC(TIMER0_TOGGLE4,	OUT),
	K210_FUNC(TIMER1_TOGGLE1,	OUT),
	K210_FUNC(TIMER1_TOGGLE2,	OUT),
	K210_FUNC(TIMER1_TOGGLE3,	OUT),
	K210_FUNC(TIMER1_TOGGLE4,	OUT),
	K210_FUNC(TIMER2_TOGGLE1,	OUT),
	K210_FUNC(TIMER2_TOGGLE2,	OUT),
	K210_FUNC(TIMER2_TOGGLE3,	OUT),
	K210_FUNC(TIMER2_TOGGLE4,	OUT),
	K210_FUNC(CLK_SPI2,		OUT),
	K210_FUNC(CLK_I2C2,		OUT),
	K210_FUNC(INTERNAL0,		OUT),
	K210_FUNC(INTERNAL1,		OUT),
	K210_FUNC(INTERNAL2,		OUT),
	K210_FUNC(INTERNAL3,		OUT),
	K210_FUNC(INTERNAL4,		OUT),
	K210_FUNC(INTERNAL5,		OUT),
	K210_FUNC(INTERNAL6,		OUT),
	K210_FUNC(INTERNAL7,		OUT),
	K210_FUNC(INTERNAL8,		OUT),
	K210_FUNC(INTERNAL9,		IN),
	K210_FUNC(INTERNAL10,		IN),
	K210_FUNC(INTERNAL11,		IN),
	K210_FUNC(INTERNAL12,		IN),
	K210_FUNC(INTERNAL13,		INT13),
	K210_FUNC(INTERNAL14,		I2C),
	K210_FUNC(INTERNAL15,		IN),
	K210_FUNC(INTERNAL16,		IN),
	K210_FUNC(INTERNAL17,		IN),
	K210_FUNC(CONSTANT,		DISABLED),
	K210_FUNC(INTERNAL18,		IN),
	K210_FUNC(DEBUG0,		OUT),
	K210_FUNC(DEBUG1,		OUT),
	K210_FUNC(DEBUG2,		OUT),
	K210_FUNC(DEBUG3,		OUT),
	K210_FUNC(DEBUG4,		OUT),
	K210_FUNC(DEBUG5,		OUT),
	K210_FUNC(DEBUG6,		OUT),
	K210_FUNC(DEBUG7,		OUT),
	K210_FUNC(DEBUG8,		OUT),
	K210_FUNC(DEBUG9,		OUT),
	K210_FUNC(DEBUG10,		OUT),
	K210_FUNC(DEBUG11,		OUT),
	K210_FUNC(DEBUG12,		OUT),
	K210_FUNC(DEBUG13,		OUT),
	K210_FUNC(DEBUG14,		OUT),
	K210_FUNC(DEBUG15,		OUT),
	K210_FUNC(DEBUG16,		OUT),
	K210_FUNC(DEBUG17,		OUT),
	K210_FUNC(DEBUG18,		OUT),
	K210_FUNC(DEBUG19,		OUT),
	K210_FUNC(DEBUG20,		OUT),
	K210_FUNC(DEBUG21,		OUT),
	K210_FUNC(DEBUG22,		OUT),
	K210_FUNC(DEBUG23,		OUT),
	K210_FUNC(DEBUG24,		OUT),
	K210_FUNC(DEBUG25,		OUT),
	K210_FUNC(DEBUG26,		OUT),
	K210_FUNC(DEBUG27,		OUT),
	K210_FUNC(DEBUG28,		OUT),
	K210_FUNC(DEBUG29,		OUT),
	K210_FUNC(DEBUG30,		OUT),
	K210_FUNC(DEBUG31,		OUT),
};

#define PIN_CONFIG_OUTPUT_INVERT	(PIN_CONFIG_END + 1)
#define PIN_CONFIG_INPUT_INVERT		(PIN_CONFIG_END + 2)

static const struct pinconf_generic_params k210_pinconf_custom_params[] = {
	{ "output-polarity-invert", PIN_CONFIG_OUTPUT_INVERT, 1 },
	{ "input-polarity-invert",  PIN_CONFIG_INPUT_INVERT, 1 },
};

/*
 * Max drive strength in uA.
 */
static const int k210_pinconf_drive_strength[] = {
	[0] = 11200,
	[1] = 16800,
	[2] = 22300,
	[3] = 27800,
	[4] = 33300,
	[5] = 38700,
	[6] = 44100,
	[7] = 49500,
};

static int k210_pinconf_get_drive(unsigned int max_strength_ua)
{
	int i;

	for (i = K210_PC_DRIVE_MAX; i; i--) {
		if (k210_pinconf_drive_strength[i] <= max_strength_ua)
			return i;
	}

	return -EINVAL;
}

static void k210_pinmux_set_pin_function(struct pinctrl_dev *pctldev,
					 u32 pin, u32 func)
{
	struct k210_fpioa_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	const struct k210_pcf_info *info = &k210_pcf_infos[func];
	u32 mode = k210_pinconf_mode_id_to_mode[info->mode_id];
	u32 val = func | mode;

	dev_dbg(pdata->dev, "set pin %u function %s (%u) -> 0x%08x\n",
		pin, info->name, func, val);

	writel(val, &pdata->fpioa->pins[pin]);
}

static int k210_pinconf_set_param(struct pinctrl_dev *pctldev,
				  unsigned int pin,
				  unsigned int param, unsigned int arg)
{
	struct k210_fpioa_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	u32 val = readl(&pdata->fpioa->pins[pin]);
	int drive;

	dev_dbg(pdata->dev, "set pin %u param %u, arg 0x%x\n",
		pin, param, arg);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		val &= ~K210_PC_BIAS_MASK;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!arg)
			return -EINVAL;
		val |= K210_PC_PD;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!arg)
			return -EINVAL;
		val |= K210_PC_PD;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg *= 1000;
		fallthrough;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		drive = k210_pinconf_get_drive(arg);
		if (drive < 0)
			return drive;
		val &= ~K210_PC_DRIVE_MASK;
		val |= FIELD_PREP(K210_PC_DRIVE_MASK, drive);
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		if (arg)
			val |= K210_PC_IE;
		else
			val &= ~K210_PC_IE;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (arg)
			val |= K210_PC_ST;
		else
			val &= ~K210_PC_ST;
		break;
	case PIN_CONFIG_OUTPUT:
		k210_pinmux_set_pin_function(pctldev, pin, K210_PCF_CONSTANT);
		val = readl(&pdata->fpioa->pins[pin]);
		val |= K210_PC_MODE_OUT;
		if (!arg)
			val |= K210_PC_DO_INV;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		if (arg)
			val |= K210_PC_OE;
		else
			val &= ~K210_PC_OE;
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (arg)
			val |= K210_PC_SL;
		else
			val &= ~K210_PC_SL;
		break;
	case PIN_CONFIG_OUTPUT_INVERT:
		if (arg)
			val |= K210_PC_DO_INV;
		else
			val &= ~K210_PC_DO_INV;
		break;
	case PIN_CONFIG_INPUT_INVERT:
		if (arg)
			val |= K210_PC_DI_INV;
		else
			val &= ~K210_PC_DI_INV;
		break;
	default:
		return -EINVAL;
	}

	writel(val, &pdata->fpioa->pins[pin]);

	return 0;
}

static int k210_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int num_configs)
{
	unsigned int param, arg;
	int i, ret;

	if (WARN_ON(pin >= K210_NPINS))
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);
		ret = k210_pinconf_set_param(pctldev, pin, param, arg);
		if (ret)
			return ret;
	}

	return 0;
}

static void k210_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s, unsigned int pin)
{
	struct k210_fpioa_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	seq_printf(s, "%#x", readl(&pdata->fpioa->pins[pin]));
}

static int k210_pinconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int selector, unsigned long *configs,
				  unsigned int num_configs)
{
	struct k210_fpioa_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param, arg;
	u32 bit;
	int i;

	/* Pins should be configured with pinmux, not groups*/
	if (selector < K210_NPINS)
		return -EINVAL;

	/* Otherwise it's a power domain */
	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		if (param != PIN_CONFIG_POWER_SOURCE)
			return -EINVAL;

		arg = pinconf_to_config_argument(configs[i]);
		bit = BIT(selector - K210_NPINS);
		regmap_update_bits(pdata->sysctl_map,
				   pdata->power_offset,
				   bit, arg ? bit : 0);
	}

	return 0;
}

static void k210_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned int selector)
{
	struct k210_fpioa_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	int ret;
	u32 val;

	if (selector < K210_NPINS)
		return k210_pinconf_dbg_show(pctldev, s, selector);

	ret = regmap_read(pdata->sysctl_map, pdata->power_offset, &val);
	if (ret) {
		dev_err(pdata->dev, "Failed to read power reg\n");
		return;
	}

	seq_printf(s, "%s: %s V", k210_group_names[selector],
		   val & BIT(selector - K210_NPINS) ? "1.8" : "3.3");
}

static const struct pinconf_ops k210_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = k210_pinconf_set,
	.pin_config_group_set = k210_pinconf_group_set,
	.pin_config_dbg_show = k210_pinconf_dbg_show,
	.pin_config_group_dbg_show = k210_pinconf_group_dbg_show,
};

static int k210_pinmux_get_function_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(k210_pcf_infos);
}

static const char *k210_pinmux_get_function_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	return k210_pcf_infos[selector].name;
}

static int k210_pinmux_get_function_groups(struct pinctrl_dev *pctldev,
					   unsigned int selector,
					   const char * const **groups,
					   unsigned int * const num_groups)
{
	/* Any function can be mapped to any pin */
	*groups = k210_group_names;
	*num_groups = K210_NPINS;

	return 0;
}

static int k210_pinmux_set_mux(struct pinctrl_dev *pctldev,
			       unsigned int function,
			       unsigned int group)
{
	/* Can't mux power domains */
	if (group >= K210_NPINS)
		return -EINVAL;

	k210_pinmux_set_pin_function(pctldev, group, function);

	return 0;
}

static const struct pinmux_ops k210_pinmux_ops = {
	.get_functions_count = k210_pinmux_get_function_count,
	.get_function_name = k210_pinmux_get_function_name,
	.get_function_groups = k210_pinmux_get_function_groups,
	.set_mux = k210_pinmux_set_mux,
	.strict = true,
};

static int k210_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return K210_NGROUPS;
}

static const char *k210_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
					       unsigned int group)
{
	return k210_group_names[group];
}

static int k210_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
				       unsigned int group,
				       const unsigned int **pins,
				       unsigned int *npins)
{
	if (group >= K210_NPINS) {
		*pins = NULL;
		*npins = 0;
		return 0;
	}

	*pins = &k210_pins[group].number;
	*npins = 1;

	return 0;
}

static void k210_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
				      struct seq_file *s, unsigned int offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int k210_pinctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
					  struct device_node *np,
					  struct pinctrl_map **map,
					  unsigned int *reserved_maps,
					  unsigned int *num_maps)
{
	struct property *prop;
	const __be32 *p;
	int ret, pinmux_groups;
	u32 pinmux_group;
	unsigned long *configs = NULL;
	unsigned int num_configs = 0;
	unsigned int reserve = 0;

	ret = of_property_count_strings(np, "groups");
	if (!ret)
		return pinconf_generic_dt_subnode_to_map(pctldev, np, map,
						reserved_maps, num_maps,
						PIN_MAP_TYPE_CONFIGS_GROUP);

	pinmux_groups = of_property_count_u32_elems(np, "pinmux");
	if (pinmux_groups <= 0) {
		/* Ignore this node */
		return 0;
	}

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs,
					      &num_configs);
	if (ret < 0) {
		dev_err(pctldev->dev, "%pOF: could not parse node property\n",
			np);
		return ret;
	}

	reserve = pinmux_groups * (1 + num_configs);
	ret = pinctrl_utils_reserve_map(pctldev, map, reserved_maps, num_maps,
					reserve);
	if (ret < 0)
		goto exit;

	of_property_for_each_u32(np, "pinmux", prop, p, pinmux_group) {
		const char *group_name, *func_name;
		u32 pin = FIELD_GET(K210_PG_PIN, pinmux_group);
		u32 func = FIELD_GET(K210_PG_FUNC, pinmux_group);

		if (pin >= K210_NPINS) {
			ret = -EINVAL;
			goto exit;
		}

		group_name = k210_group_names[pin];
		func_name = k210_pcf_infos[func].name;

		dev_dbg(pctldev->dev, "Pinmux %s: pin %u func %s\n",
			np->name, pin, func_name);

		ret = pinctrl_utils_add_map_mux(pctldev, map, reserved_maps,
						num_maps, group_name,
						func_name);
		if (ret < 0) {
			dev_err(pctldev->dev, "%pOF add mux map failed %d\n",
				np, ret);
			goto exit;
		}

		if (num_configs) {
			ret = pinctrl_utils_add_map_configs(pctldev, map,
					reserved_maps, num_maps, group_name,
					configs, num_configs,
					PIN_MAP_TYPE_CONFIGS_PIN);
			if (ret < 0) {
				dev_err(pctldev->dev,
					"%pOF add configs map failed %d\n",
					np, ret);
				goto exit;
			}
		}
	}

	ret = 0;

exit:
	kfree(configs);
	return ret;
}

static int k210_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				       struct device_node *np_config,
				       struct pinctrl_map **map,
				       unsigned int *num_maps)
{
	unsigned int reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	ret = k210_pinctrl_dt_subnode_to_map(pctldev, np_config, map,
					     &reserved_maps, num_maps);
	if (ret < 0)
		goto err;

	for_each_available_child_of_node(np_config, np) {
		ret = k210_pinctrl_dt_subnode_to_map(pctldev, np, map,
						     &reserved_maps, num_maps);
		if (ret < 0)
			goto err;
	}
	return 0;

err:
	pinctrl_utils_free_map(pctldev, *map, *num_maps);
	return ret;
}


static const struct pinctrl_ops k210_pinctrl_ops = {
	.get_groups_count = k210_pinctrl_get_groups_count,
	.get_group_name = k210_pinctrl_get_group_name,
	.get_group_pins = k210_pinctrl_get_group_pins,
	.pin_dbg_show = k210_pinctrl_pin_dbg_show,
	.dt_node_to_map = k210_pinctrl_dt_node_to_map,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static struct pinctrl_desc k210_pinctrl_desc = {
	.name = "k210-pinctrl",
	.pins = k210_pins,
	.npins = K210_NPINS,
	.pctlops = &k210_pinctrl_ops,
	.pmxops = &k210_pinmux_ops,
	.confops = &k210_pinconf_ops,
	.custom_params = k210_pinconf_custom_params,
	.num_custom_params = ARRAY_SIZE(k210_pinconf_custom_params),
};

static void k210_fpioa_init_ties(struct k210_fpioa_data *pdata)
{
	struct k210_fpioa __iomem *fpioa = pdata->fpioa;
	u32 val;
	int i, j;

	dev_dbg(pdata->dev, "Init pin ties\n");

	/* Init pin functions input ties */
	for (i = 0; i < ARRAY_SIZE(fpioa->tie_en); i++) {
		val = 0;
		for (j = 0; j < 32; j++) {
			if (k210_pcf_infos[i * 32 + j].mode_id ==
			    K210_PC_DEFAULT_IN_TIE) {
				dev_dbg(pdata->dev,
					"tie_en function %d (%s)\n",
					i * 32 + j,
					k210_pcf_infos[i * 32 + j].name);
				val |= BIT(j);
			}
		}

		/* Set value before enable */
		writel(val, &fpioa->tie_val[i]);
		writel(val, &fpioa->tie_en[i]);
	}
}

static int k210_fpioa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct k210_fpioa_data *pdata;
	int ret;

	dev_info(dev, "K210 FPIOA pin controller\n");

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = dev;
	platform_set_drvdata(pdev, pdata);

	pdata->fpioa = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pdata->fpioa))
		return PTR_ERR(pdata->fpioa);

	pdata->clk = devm_clk_get(dev, "ref");
	if (IS_ERR(pdata->clk))
		return PTR_ERR(pdata->clk);

	ret = clk_prepare_enable(pdata->clk);
	if (ret)
		return ret;

	pdata->pclk = devm_clk_get_optional(dev, "pclk");
	if (!IS_ERR(pdata->pclk)) {
		ret = clk_prepare_enable(pdata->pclk);
		if (ret)
			goto disable_clk;
	}

	pdata->sysctl_map =
		syscon_regmap_lookup_by_phandle_args(np,
						"canaan,k210-sysctl-power",
						1, &pdata->power_offset);
	if (IS_ERR(pdata->sysctl_map)) {
		ret = PTR_ERR(pdata->sysctl_map);
		goto disable_pclk;
	}

	k210_fpioa_init_ties(pdata);

	pdata->pctl = pinctrl_register(&k210_pinctrl_desc, dev, (void *)pdata);
	if (IS_ERR(pdata->pctl)) {
		ret = PTR_ERR(pdata->pctl);
		goto disable_pclk;
	}

	return 0;

disable_pclk:
	clk_disable_unprepare(pdata->pclk);
disable_clk:
	clk_disable_unprepare(pdata->clk);

	return ret;
}

static const struct of_device_id k210_fpioa_dt_ids[] = {
	{ .compatible = "canaan,k210-fpioa" },
	{ /* sentinel */ },
};

static struct platform_driver k210_fpioa_driver = {
	.probe	= k210_fpioa_probe,
	.driver = {
		.name		= "k210-fpioa",
		.of_match_table	= k210_fpioa_dt_ids,
	},
};
builtin_platform_driver(k210_fpioa_driver);
