// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl driver for the T-Head TH1520 SoC
 *
 * Copyright (C) 2023 Emil Renner Berthing <emil.renner.berthing@canonical.com>
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/of_address.h>
#include <linux/of_device.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"
#include "pinconf.h"

#define TH1520_PADCFG_IE	BIT(9)
#define TH1520_PADCFG_SL	BIT(8)
#define TH1520_PADCFG_ST	BIT(7)
#define TH1520_PADCFG_SPU	BIT(6)
#define TH1520_PADCFG_PS	BIT(5)
#define TH1520_PADCFG_PE	BIT(4)
#define TH1520_PADCFG_BIAS	(TH1520_PADCFG_SPU | TH1520_PADCFG_PS | TH1520_PADCFG_PE)
#define TH1520_PADCFG_DS	GENMASK(3, 0)

#define TH1520_PULL_DOWN_OHM	44000 /* typ. 44kOhm */
#define TH1520_PULL_UP_OHM	48000 /* typ. 48kOhm */
#define TH1520_PULL_STRONG_OHM	 2100 /* typ. 2.1kOhm */

#define TH1520_PAD_NO_PADCFG	BIT(30)
#define TH1520_PAD_MUXDATA	GENMASK(29, 0)

struct th1520_pad_group {
	const char *name;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
};

struct th1520_pinctrl {
	struct pinctrl_desc desc;
	struct mutex mutex;	/* serialize adding functions */
	raw_spinlock_t lock;	/* serialize register access */
	void __iomem *base;
	struct pinctrl_dev *pctl;
};

static void __iomem *th1520_padcfg(struct th1520_pinctrl *thp,
				   unsigned int pin)
{
	return thp->base + 4 * (pin / 2);
}

static unsigned int th1520_padcfg_shift(unsigned int pin)
{
	return 16 * (pin & BIT(0));
}

static void __iomem *th1520_muxcfg(struct th1520_pinctrl *thp,
				   unsigned int pin)
{
	return thp->base + 0x400 + 4 * (pin / 8);
}

static unsigned int th1520_muxcfg_shift(unsigned int pin)
{
	return 4 * (pin & GENMASK(2, 0));
}

enum th1520_muxtype {
	TH1520_MUX_____,
	TH1520_MUX_GPIO,
	TH1520_MUX_PWM,
	TH1520_MUX_UART,
	TH1520_MUX_IR,
	TH1520_MUX_I2C,
	TH1520_MUX_SPI,
	TH1520_MUX_QSPI,
	TH1520_MUX_SDIO,
	TH1520_MUX_AUD,
	TH1520_MUX_I2S,
	TH1520_MUX_MAC0,
	TH1520_MUX_MAC1,
	TH1520_MUX_DPU0,
	TH1520_MUX_DPU1,
	TH1520_MUX_ISP,
	TH1520_MUX_HDMI,
	TH1520_MUX_BSEL,
	TH1520_MUX_DBG,
	TH1520_MUX_CLK,
	TH1520_MUX_JTAG,
	TH1520_MUX_ISO,
	TH1520_MUX_FUSE,
	TH1520_MUX_RST,
};

static const char *const th1520_muxtype_string[] = {
	[TH1520_MUX_GPIO] = "gpio",
	[TH1520_MUX_PWM]  = "pwm",
	[TH1520_MUX_UART] = "uart",
	[TH1520_MUX_IR]   = "ir",
	[TH1520_MUX_I2C]  = "i2c",
	[TH1520_MUX_SPI]  = "spi",
	[TH1520_MUX_QSPI] = "qspi",
	[TH1520_MUX_SDIO] = "sdio",
	[TH1520_MUX_AUD]  = "audio",
	[TH1520_MUX_I2S]  = "i2s",
	[TH1520_MUX_MAC0] = "gmac0",
	[TH1520_MUX_MAC1] = "gmac1",
	[TH1520_MUX_DPU0] = "dpu0",
	[TH1520_MUX_DPU1] = "dpu1",
	[TH1520_MUX_ISP]  = "isp",
	[TH1520_MUX_HDMI] = "hdmi",
	[TH1520_MUX_BSEL] = "bootsel",
	[TH1520_MUX_DBG]  = "debug",
	[TH1520_MUX_CLK]  = "clock",
	[TH1520_MUX_JTAG] = "jtag",
	[TH1520_MUX_ISO]  = "iso7816",
	[TH1520_MUX_FUSE] = "efuse",
	[TH1520_MUX_RST]  = "reset",
};

static enum th1520_muxtype th1520_muxtype_get(const char *str)
{
	enum th1520_muxtype mt;

	for (mt = TH1520_MUX_GPIO; mt < ARRAY_SIZE(th1520_muxtype_string); mt++) {
		if (!strcmp(str, th1520_muxtype_string[mt]))
			return mt;
	}
	return TH1520_MUX_____;
}

#define TH1520_PAD(_nr, _name, m0, m1, m2, m3, m4, m5, _flags) \
	{ .number = _nr, .name = #_name, .drv_data = (void *)((_flags) | \
		(TH1520_MUX_##m0 <<  0) | (TH1520_MUX_##m1 <<  5) | (TH1520_MUX_##m2 << 10) | \
		(TH1520_MUX_##m3 << 15) | (TH1520_MUX_##m4 << 20) | (TH1520_MUX_##m5 << 25)) }

static unsigned long th1520_pad_muxdata(void *drv_data)
{
	return (uintptr_t)drv_data & TH1520_PAD_MUXDATA;
}

static bool th1520_pad_no_padcfg(void *drv_data)
{
	return (uintptr_t)drv_data & TH1520_PAD_NO_PADCFG;
}

static const struct pinctrl_pin_desc th1520_group1_pins[] = {
	TH1520_PAD(0,  OSC_CLK_IN,    ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(1,  OSC_CLK_OUT,   ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(2,  SYS_RST_N,     ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(3,  RTC_CLK_IN,    ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(4,  RTC_CLK_OUT,   ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	/* skip number 5 so we can calculate register offsets and shifts from the pin number */
	TH1520_PAD(6,  TEST_MODE,     ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(7,  DEBUG_MODE,    DBG,  ____, ____, GPIO, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(8,  POR_SEL,       ____, ____, ____, ____, ____, ____, TH1520_PAD_NO_PADCFG),
	TH1520_PAD(9,  I2C_AON_SCL,   I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(10, I2C_AON_SDA,   I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(11, CPU_JTG_TCLK,  JTAG, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(12, CPU_JTG_TMS,   JTAG, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(13, CPU_JTG_TDI,   JTAG, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(14, CPU_JTG_TDO,   JTAG, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(15, CPU_JTG_TRST,  JTAG, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(16, AOGPIO_7,      CLK,  AUD,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(17, AOGPIO_8,      UART, AUD,  IR,   GPIO, ____, ____, 0),
	TH1520_PAD(18, AOGPIO_9,      UART, AUD,  IR,   GPIO, ____, ____, 0),
	TH1520_PAD(19, AOGPIO_10,     CLK,  AUD,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(20, AOGPIO_11,     GPIO, AUD,  ____, ____, ____, ____, 0),
	TH1520_PAD(21, AOGPIO_12,     GPIO, AUD,  ____, ____, ____, ____, 0),
	TH1520_PAD(22, AOGPIO_13,     GPIO, AUD,  ____, ____, ____, ____, 0),
	TH1520_PAD(23, AOGPIO_14,     GPIO, AUD,  ____, ____, ____, ____, 0),
	TH1520_PAD(24, AOGPIO_15,     GPIO, AUD,  ____, ____, ____, ____, 0),
	TH1520_PAD(25, AUDIO_PA0,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(26, AUDIO_PA1,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(27, AUDIO_PA2,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(28, AUDIO_PA3,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(29, AUDIO_PA4,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(30, AUDIO_PA5,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(31, AUDIO_PA6,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(32, AUDIO_PA7,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(33, AUDIO_PA8,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(34, AUDIO_PA9,     AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(35, AUDIO_PA10,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(36, AUDIO_PA11,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(37, AUDIO_PA12,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(38, AUDIO_PA13,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(39, AUDIO_PA14,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(40, AUDIO_PA15,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(41, AUDIO_PA16,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(42, AUDIO_PA17,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(43, AUDIO_PA27,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(44, AUDIO_PA28,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(45, AUDIO_PA29,    AUD,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(46, AUDIO_PA30,    AUD,  RST,  ____, GPIO, ____, ____, 0),
};

static const struct pinctrl_pin_desc th1520_group2_pins[] = {
	TH1520_PAD(0,  QSPI1_SCLK,    QSPI, ISO,  ____, GPIO, FUSE, ____, 0),
	TH1520_PAD(1,  QSPI1_CSN0,    QSPI, ____, I2C,  GPIO, FUSE, ____, 0),
	TH1520_PAD(2,  QSPI1_D0_MOSI, QSPI, ISO,  I2C,  GPIO, FUSE, ____, 0),
	TH1520_PAD(3,  QSPI1_D1_MISO, QSPI, ISO,  ____, GPIO, FUSE, ____, 0),
	TH1520_PAD(4,  QSPI1_D2_WP,   QSPI, ISO,  UART, GPIO, FUSE, ____, 0),
	TH1520_PAD(5,  QSPI1_D3_HOLD, QSPI, ISO,  UART, GPIO, ____, ____, 0),
	TH1520_PAD(6,  I2C0_SCL,      I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(7,  I2C0_SDA,      I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(8,  I2C1_SCL,      I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(9,  I2C1_SDA,      I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(10, UART1_TXD,     UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(11, UART1_RXD,     UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(12, UART4_TXD,     UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(13, UART4_RXD,     UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(14, UART4_CTSN,    UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(15, UART4_RTSN,    UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(16, UART3_TXD,     DBG,  UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(17, UART3_RXD,     DBG,  UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(18, GPIO0_18,      GPIO, I2C,  ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(19, GPIO0_19,      GPIO, I2C,  ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(20, GPIO0_20,      GPIO, UART, IR,   ____, DPU0, DPU1, 0),
	TH1520_PAD(21, GPIO0_21,      GPIO, UART, IR,   ____, DPU0, DPU1, 0),
	TH1520_PAD(22, GPIO0_22,      GPIO, JTAG, I2C,  ____, DPU0, DPU1, 0),
	TH1520_PAD(23, GPIO0_23,      GPIO, JTAG, I2C,  ____, DPU0, DPU1, 0),
	TH1520_PAD(24, GPIO0_24,      GPIO, JTAG, QSPI, ____, DPU0, DPU1, 0),
	TH1520_PAD(25, GPIO0_25,      GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(26, GPIO0_26,      GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(27, GPIO0_27,      GPIO, ____, I2C,  ____, DPU0, DPU1, 0),
	TH1520_PAD(28, GPIO0_28,      GPIO, ____, I2C,  ____, DPU0, DPU1, 0),
	TH1520_PAD(29, GPIO0_29,      GPIO, ____, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(30, GPIO0_30,      GPIO, ____, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(31, GPIO0_31,      GPIO, ____, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(32, GPIO1_0,       GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(33, GPIO1_1,       GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(34, GPIO1_2,       GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(35, GPIO1_3,       GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(36, GPIO1_4,       GPIO, JTAG, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(37, GPIO1_5,       GPIO, ____, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(38, GPIO1_6,       GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(39, GPIO1_7,       GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(40, GPIO1_8,       GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(41, GPIO1_9,       GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(42, GPIO1_10,      GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(43, GPIO1_11,      GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(44, GPIO1_12,      GPIO, QSPI, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(45, GPIO1_13,      GPIO, UART, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(46, GPIO1_14,      GPIO, UART, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(47, GPIO1_15,      GPIO, UART, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(48, GPIO1_16,      GPIO, UART, ____, ____, DPU0, DPU1, 0),
	TH1520_PAD(49, CLK_OUT_0,     BSEL, CLK,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(50, CLK_OUT_1,     BSEL, CLK,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(51, CLK_OUT_2,     BSEL, CLK,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(52, CLK_OUT_3,     BSEL, CLK,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(53, GPIO1_21,      JTAG, ____, ISP,  GPIO, ____, ____, 0),
	TH1520_PAD(54, GPIO1_22,      JTAG, ____, ISP,  GPIO, ____, ____, 0),
	TH1520_PAD(55, GPIO1_23,      JTAG, ____, ISP,  GPIO, ____, ____, 0),
	TH1520_PAD(56, GPIO1_24,      JTAG, ____, ISP,  GPIO, ____, ____, 0),
	TH1520_PAD(57, GPIO1_25,      JTAG, ____, ISP,  GPIO, ____, ____, 0),
	TH1520_PAD(58, GPIO1_26,      GPIO, ____, ISP,  ____, ____, ____, 0),
	TH1520_PAD(59, GPIO1_27,      GPIO, ____, ISP,  ____, ____, ____, 0),
	TH1520_PAD(60, GPIO1_28,      GPIO, ____, ISP,  ____, ____, ____, 0),
	TH1520_PAD(61, GPIO1_29,      GPIO, ____, ISP,  ____, ____, ____, 0),
	TH1520_PAD(62, GPIO1_30,      GPIO, ____, ISP,  ____, ____, ____, 0),
};

static const struct pinctrl_pin_desc th1520_group3_pins[] = {
	TH1520_PAD(0,  UART0_TXD,     UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(1,  UART0_RXD,     UART, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(2,  QSPI0_SCLK,    QSPI, PWM,  I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(3,  QSPI0_CSN0,    QSPI, PWM,  I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(4,  QSPI0_CSN1,    QSPI, PWM,  I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(5,  QSPI0_D0_MOSI, QSPI, PWM,  I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(6,  QSPI0_D1_MISO, QSPI, PWM,  I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(7,  QSPI0_D2_WP,   QSPI, PWM,  I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(8,  QSPI1_D3_HOLD, QSPI, ____, I2S,  GPIO, ____, ____, 0),
	TH1520_PAD(9,  I2C2_SCL,      I2C,  UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(10, I2C2_SDA,      I2C,  UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(11, I2C3_SCL,      I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(12, I2C3_SDA,      I2C,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(13, GPIO2_13,      GPIO, SPI,  ____, ____, ____, ____, 0),
	TH1520_PAD(14, SPI_SCLK,      SPI,  UART, IR,   GPIO, ____, ____, 0),
	TH1520_PAD(15, SPI_CSN,       SPI,  UART, IR,   GPIO, ____, ____, 0),
	TH1520_PAD(16, SPI_MOSI,      SPI,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(17, SPI_MISO,      SPI,  ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(18, GPIO2_18,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(19, GPIO2_19,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(20, GPIO2_20,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(21, GPIO2_21,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(22, GPIO2_22,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(23, GPIO2_23,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(24, GPIO2_24,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(25, GPIO2_25,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(26, SDIO0_WPRTN,   SDIO, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(27, SDIO0_DETN,    SDIO, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(28, SDIO1_WPRTN,   SDIO, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(29, SDIO1_DETN,    SDIO, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(30, GPIO2_30,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(31, GPIO2_31,      GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(32, GPIO3_0,       GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(33, GPIO3_1,       GPIO, MAC1, ____, ____, ____, ____, 0),
	TH1520_PAD(34, GPIO3_2,       GPIO, PWM,  ____, ____, ____, ____, 0),
	TH1520_PAD(35, GPIO3_3,       GPIO, PWM,  ____, ____, ____, ____, 0),
	TH1520_PAD(36, HDMI_SCL,      HDMI, PWM,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(37, HDMI_SDA,      HDMI, PWM,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(38, HDMI_CEC,      HDMI, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(39, GMAC0_TX_CLK,  MAC0, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(40, GMAC0_RX_CLK,  MAC0, ____, ____, GPIO, ____, ____, 0),
	TH1520_PAD(41, GMAC0_TXEN,    MAC0, UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(42, GMAC0_TXD0,    MAC0, UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(43, GMAC0_TXD1,    MAC0, UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(44, GMAC0_TXD2,    MAC0, UART, ____, GPIO, ____, ____, 0),
	TH1520_PAD(45, GMAC0_TXD3,    MAC0, I2C,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(46, GMAC0_RXDV,    MAC0, I2C,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(47, GMAC0_RXD0,    MAC0, I2C,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(48, GMAC0_RXD1,    MAC0, I2C,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(49, GMAC0_RXD2,    MAC0, SPI,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(50, GMAC0_RXD3,    MAC0, SPI,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(51, GMAC0_MDC,     MAC0, SPI,  MAC1, GPIO, ____, ____, 0),
	TH1520_PAD(52, GMAC0_MDIO,    MAC0, SPI,  MAC1, GPIO, ____, ____, 0),
	TH1520_PAD(53, GMAC0_COL,     MAC0, PWM,  ____, GPIO, ____, ____, 0),
	TH1520_PAD(54, GMAC0_CRS,     MAC0, PWM,  ____, GPIO, ____, ____, 0),
};

static const struct th1520_pad_group th1520_group1 = {
	.name = "th1520-group1",
	.pins = th1520_group1_pins,
	.npins = ARRAY_SIZE(th1520_group1_pins),
};

static const struct th1520_pad_group th1520_group2 = {
	.name = "th1520-group2",
	.pins = th1520_group2_pins,
	.npins = ARRAY_SIZE(th1520_group2_pins),
};

static const struct th1520_pad_group th1520_group3 = {
	.name = "th1520-group3",
	.pins = th1520_group3_pins,
	.npins = ARRAY_SIZE(th1520_group3_pins),
};

static int th1520_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);

	return thp->desc.npins;
}

static const char *th1520_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int gsel)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);

	return thp->desc.pins[gsel].name;
}

static int th1520_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int gsel,
					 const unsigned int **pins,
					 unsigned int *npins)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);

	*pins = &thp->desc.pins[gsel].number;
	*npins = 1;
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void th1520_pin_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int pin)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *padcfg = th1520_padcfg(thp, pin);
	void __iomem *muxcfg = th1520_muxcfg(thp, pin);
	u32 pad;
	u32 mux;

	scoped_guard(raw_spinlock_irqsave, &thp->lock) {
		pad = readl_relaxed(padcfg);
		mux = readl_relaxed(muxcfg);
	}

	seq_printf(s, "[PADCFG_%03u:0x%x=0x%07x MUXCFG_%03u:0x%x=0x%08x]",
		   1 + pin / 2, 0x000 + 4 * (pin / 2), pad,
		   1 + pin / 8, 0x400 + 4 * (pin / 8), mux);
}
#else
#define th1520_pin_dbg_show NULL
#endif

static void th1520_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				       struct pinctrl_map *map, unsigned int nmaps)
{
	unsigned long *seen = NULL;
	unsigned int i;

	for (i = 0; i < nmaps; i++) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_PIN &&
		    map[i].data.configs.configs != seen) {
			seen = map[i].data.configs.configs;
			kfree(seen);
		}
	}

	kfree(map);
}

static int th1520_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					 struct device_node *np,
					 struct pinctrl_map **maps,
					 unsigned int *num_maps)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_map *map;
	unsigned long *configs;
	unsigned int nconfigs;
	unsigned int nmaps;
	int ret;

	nmaps = 0;
	for_each_available_child_of_node_scoped(np, child) {
		int npins = of_property_count_strings(child, "pins");

		if (npins <= 0) {
			dev_err(thp->pctl->dev, "no pins selected for %pOFn.%pOFn\n",
				np, child);
			return -EINVAL;
		}
		nmaps += npins;
		if (of_property_present(child, "function"))
			nmaps += npins;
	}

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	nmaps = 0;
	guard(mutex)(&thp->mutex);
	for_each_available_child_of_node_scoped(np, child) {
		unsigned int rollback = nmaps;
		enum th1520_muxtype muxtype;
		struct property *prop;
		const char *funcname;
		const char **pgnames;
		const char *pinname;
		int npins;

		ret = pinconf_generic_parse_dt_config(child, pctldev, &configs, &nconfigs);
		if (ret) {
			dev_err(thp->pctl->dev, "%pOFn.%pOFn: error parsing pin config\n",
				np, child);
			goto free_map;
		}

		if (!of_property_read_string(child, "function", &funcname)) {
			muxtype = th1520_muxtype_get(funcname);
			if (!muxtype) {
				dev_err(thp->pctl->dev, "%pOFn.%pOFn: unknown function '%s'\n",
					np, child, funcname);
				ret = -EINVAL;
				goto free_configs;
			}

			funcname = devm_kasprintf(thp->pctl->dev, GFP_KERNEL, "%pOFn.%pOFn",
						  np, child);
			if (!funcname) {
				ret = -ENOMEM;
				goto free_configs;
			}

			npins = of_property_count_strings(child, "pins");
			pgnames = devm_kcalloc(thp->pctl->dev, npins, sizeof(*pgnames), GFP_KERNEL);
			if (!pgnames) {
				ret = -ENOMEM;
				goto free_configs;
			}
		} else {
			funcname = NULL;
		}

		npins = 0;
		of_property_for_each_string(child, "pins", prop, pinname) {
			unsigned int i;

			for (i = 0; i < thp->desc.npins; i++) {
				if (!strcmp(pinname, thp->desc.pins[i].name))
					break;
			}
			if (i == thp->desc.npins) {
				nmaps = rollback;
				dev_err(thp->pctl->dev, "%pOFn.%pOFn: unknown pin '%s'\n",
					np, child, pinname);
				ret = -EINVAL;
				goto free_configs;
			}

			if (nconfigs) {
				map[nmaps].type = PIN_MAP_TYPE_CONFIGS_PIN;
				map[nmaps].data.configs.group_or_pin = thp->desc.pins[i].name;
				map[nmaps].data.configs.configs = configs;
				map[nmaps].data.configs.num_configs = nconfigs;
				nmaps += 1;
			}
			if (funcname) {
				pgnames[npins++] = thp->desc.pins[i].name;
				map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
				map[nmaps].data.mux.function = funcname;
				map[nmaps].data.mux.group = thp->desc.pins[i].name;
				nmaps += 1;
			}
		}

		if (funcname) {
			ret = pinmux_generic_add_function(pctldev, funcname, pgnames,
							  npins, (void *)muxtype);
			if (ret < 0) {
				dev_err(thp->pctl->dev, "error adding function %s\n", funcname);
				goto free_map;
			}
		}
	}

	*maps = map;
	*num_maps = nmaps;
	return 0;

free_configs:
	kfree(configs);
free_map:
	th1520_pinctrl_dt_free_map(pctldev, map, nmaps);
	return ret;
}

static const struct pinctrl_ops th1520_pinctrl_ops = {
	.get_groups_count = th1520_pinctrl_get_groups_count,
	.get_group_name = th1520_pinctrl_get_group_name,
	.get_group_pins = th1520_pinctrl_get_group_pins,
	.pin_dbg_show = th1520_pin_dbg_show,
	.dt_node_to_map = th1520_pinctrl_dt_node_to_map,
	.dt_free_map = th1520_pinctrl_dt_free_map,
};

static const u8 th1520_drive_strength_in_ma[16] = {
	1, 2, 3, 5, 7, 8, 10, 12, 13, 15, 16, 18, 20, 21, 23, 25,
};

static u16 th1520_drive_strength_from_ma(u32 arg)
{
	u16 ds;

	for (ds = 0; ds < TH1520_PADCFG_DS; ds++) {
		if (arg <= th1520_drive_strength_in_ma[ds])
			return ds;
	}
	return TH1520_PADCFG_DS;
}

static int th1520_padcfg_rmw(struct th1520_pinctrl *thp, unsigned int pin,
			     u32 mask, u32 value)
{
	void __iomem *padcfg = th1520_padcfg(thp, pin);
	unsigned int shift = th1520_padcfg_shift(pin);
	u32 tmp;

	mask <<= shift;
	value <<= shift;

	scoped_guard(raw_spinlock_irqsave, &thp->lock) {
		tmp = readl_relaxed(padcfg);
		tmp = (tmp & ~mask) | value;
		writel_relaxed(tmp, padcfg);
	}
	return 0;
}

static int th1520_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	bool enabled;
	int param;
	u32 value;
	u32 arg;

	if (th1520_pad_no_padcfg(desc->drv_data))
		return -ENOTSUPP;

	value = readl_relaxed(th1520_padcfg(thp, pin));
	value = (value >> th1520_padcfg_shift(pin)) & GENMASK(9, 0);

	param = pinconf_to_config_param(*config);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = !(value & (TH1520_PADCFG_SPU | TH1520_PADCFG_PE));
		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = (value & TH1520_PADCFG_BIAS) == TH1520_PADCFG_PE;
		arg = enabled ? TH1520_PULL_DOWN_OHM : 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (value & TH1520_PADCFG_SPU) {
			enabled = true;
			arg = TH1520_PULL_STRONG_OHM;
		} else if ((value & (TH1520_PADCFG_PE | TH1520_PADCFG_PS)) ==
				    (TH1520_PADCFG_PE | TH1520_PADCFG_PS)) {
			enabled = true;
			arg = TH1520_PULL_UP_OHM;
		} else {
			enabled = false;
			arg = 0;
		}
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		enabled = true;
		arg = th1520_drive_strength_in_ma[value & TH1520_PADCFG_DS];
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		enabled = value & TH1520_PADCFG_IE;
		arg = enabled ? 1 : 0;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		enabled = value & TH1520_PADCFG_ST;
		arg = enabled ? 1 : 0;
		break;
	case PIN_CONFIG_SLEW_RATE:
		enabled = value & TH1520_PADCFG_SL;
		arg = enabled ? 1 : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int th1520_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned int gsel, unsigned long *config)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int pin = thp->desc.pins[gsel].number;

	return th1520_pinconf_get(pctldev, pin, config);
}

static int th1520_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	unsigned int i;
	u16 mask, value;

	if (th1520_pad_no_padcfg(desc->drv_data))
		return -ENOTSUPP;

	mask = 0;
	value = 0;
	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			mask |= TH1520_PADCFG_BIAS;
			value &= ~TH1520_PADCFG_BIAS;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= TH1520_PADCFG_BIAS;
			value &= ~TH1520_PADCFG_BIAS;
			value |= TH1520_PADCFG_PE;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= TH1520_PADCFG_BIAS;
			value &= ~TH1520_PADCFG_BIAS;
			if (arg == TH1520_PULL_STRONG_OHM)
				value |= TH1520_PADCFG_SPU;
			else
				value |= TH1520_PADCFG_PE | TH1520_PADCFG_PS;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			mask |= TH1520_PADCFG_DS;
			value &= ~TH1520_PADCFG_DS;
			value |= th1520_drive_strength_from_ma(arg);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			mask |= TH1520_PADCFG_IE;
			if (arg)
				value |= TH1520_PADCFG_IE;
			else
				value &= ~TH1520_PADCFG_IE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			mask |= TH1520_PADCFG_ST;
			if (arg)
				value |= TH1520_PADCFG_ST;
			else
				value &= ~TH1520_PADCFG_ST;
			break;
		case PIN_CONFIG_SLEW_RATE:
			mask |= TH1520_PADCFG_SL;
			if (arg)
				value |= TH1520_PADCFG_SL;
			else
				value &= ~TH1520_PADCFG_SL;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return th1520_padcfg_rmw(thp, pin, mask, value);
}

static int th1520_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int gsel,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int pin = thp->desc.pins[gsel].number;

	return th1520_pinconf_set(pctldev, pin, configs, num_configs);
}

#ifdef CONFIG_DEBUG_FS
static void th1520_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *s, unsigned int pin)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	u32 value = readl_relaxed(th1520_padcfg(thp, pin));

	value = (value >> th1520_padcfg_shift(pin)) & GENMASK(9, 0);

	seq_printf(s, " [0x%03x]", value);
}
#else
#define th1520_pinconf_dbg_show NULL
#endif

static const struct pinconf_ops th1520_pinconf_ops = {
	.pin_config_get = th1520_pinconf_get,
	.pin_config_group_get = th1520_pinconf_group_get,
	.pin_config_set = th1520_pinconf_set,
	.pin_config_group_set = th1520_pinconf_group_set,
	.pin_config_dbg_show = th1520_pinconf_dbg_show,
	.is_generic = true,
};

static int th1520_pinmux_set(struct th1520_pinctrl *thp, unsigned int pin,
			     unsigned long muxdata, enum th1520_muxtype muxtype)
{
	void __iomem *muxcfg = th1520_muxcfg(thp, pin);
	unsigned int shift = th1520_muxcfg_shift(pin);
	u32 mask, value, tmp;

	for (value = 0; muxdata; muxdata >>= 5, value++) {
		if ((muxdata & GENMASK(4, 0)) == muxtype)
			break;
	}
	if (!muxdata) {
		dev_err(thp->pctl->dev, "invalid mux %s for pin %s\n",
			th1520_muxtype_string[muxtype], pin_get_name(thp->pctl, pin));
		return -EINVAL;
	}

	mask = GENMASK(3, 0) << shift;
	value = value << shift;

	scoped_guard(raw_spinlock_irqsave, &thp->lock) {
		tmp = readl_relaxed(muxcfg);
		tmp = (tmp & ~mask) | value;
		writel_relaxed(tmp, muxcfg);
	}
	return 0;
}

static int th1520_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int fsel, unsigned int gsel)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func = pinmux_generic_get_function(pctldev, fsel);
	enum th1520_muxtype muxtype;

	if (!func)
		return -EINVAL;

	muxtype = (uintptr_t)func->data;
	return th1520_pinmux_set(thp, thp->desc.pins[gsel].number,
				 th1520_pad_muxdata(thp->desc.pins[gsel].drv_data),
				 muxtype);
}

static int th1520_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, offset);

	return th1520_pinmux_set(thp, offset,
				 th1520_pad_muxdata(desc->drv_data),
				 TH1520_MUX_GPIO);
}

static int th1520_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int offset, bool input)
{
	struct th1520_pinctrl *thp = pinctrl_dev_get_drvdata(pctldev);

	return th1520_padcfg_rmw(thp, offset, TH1520_PADCFG_IE,
				 input ? TH1520_PADCFG_IE : 0);
}

static const struct pinmux_ops th1520_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = th1520_pinmux_set_mux,
	.gpio_request_enable = th1520_gpio_request_enable,
	.gpio_set_direction = th1520_gpio_set_direction,
	.strict = true,
};

static int th1520_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct th1520_pad_group *group;
	struct device_node *np = dev->of_node;
	struct th1520_pinctrl *thp;
	struct clk *clk;
	u32 pin_group;
	int ret;

	thp = devm_kzalloc(dev, sizeof(*thp), GFP_KERNEL);
	if (!thp)
		return -ENOMEM;

	thp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(thp->base))
		return PTR_ERR(thp->base);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "error getting clock\n");

	ret = of_property_read_u32(np, "thead,pad-group", &pin_group);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read the thead,pad-group property\n");

	if (pin_group == 1)
		group = &th1520_group1;
	else if (pin_group == 2)
		group = &th1520_group2;
	else if (pin_group == 3)
		group = &th1520_group3;
	else
		return dev_err_probe(dev, -EINVAL, "unit address did not match any pad group\n");

	thp->desc.name = group->name;
	thp->desc.pins = group->pins;
	thp->desc.npins = group->npins;
	thp->desc.pctlops = &th1520_pinctrl_ops;
	thp->desc.pmxops = &th1520_pinmux_ops;
	thp->desc.confops = &th1520_pinconf_ops;
	thp->desc.owner = THIS_MODULE;
	mutex_init(&thp->mutex);
	raw_spin_lock_init(&thp->lock);

	ret = devm_pinctrl_register_and_init(dev, &thp->desc, thp, &thp->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "could not register pinctrl driver\n");

	return pinctrl_enable(thp->pctl);
}

static const struct of_device_id th1520_pinctrl_of_match[] = {
	{ .compatible = "thead,th1520-pinctrl"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, th1520_pinctrl_of_match);

static struct platform_driver th1520_pinctrl_driver = {
	.probe = th1520_pinctrl_probe,
	.driver = {
		.name = "pinctrl-th1520",
		.of_match_table = th1520_pinctrl_of_match,
	},
};
module_platform_driver(th1520_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for the T-Head TH1520 SoC");
MODULE_AUTHOR("Emil Renner Berthing <emil.renner.berthing@canonical.com>");
MODULE_LICENSE("GPL");
