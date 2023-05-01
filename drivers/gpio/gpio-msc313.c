// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Daniel Palmer<daniel@thingy.jp> */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <dt-bindings/gpio/msc313-gpio.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

#define DRIVER_NAME "gpio-msc313"

#define MSC313_GPIO_IN  BIT(0)
#define MSC313_GPIO_OUT BIT(4)
#define MSC313_GPIO_OEN BIT(5)

/*
 * These bits need to be saved to correctly restore the
 * gpio state when resuming from suspend to memory.
 */
#define MSC313_GPIO_BITSTOSAVE (MSC313_GPIO_OUT | MSC313_GPIO_OEN)

/* pad names for fuart, same for all SoCs so far */
#define MSC313_PINNAME_FUART_RX		"fuart_rx"
#define MSC313_PINNAME_FUART_TX		"fuart_tx"
#define MSC313_PINNAME_FUART_CTS	"fuart_cts"
#define MSC313_PINNAME_FUART_RTS	"fuart_rts"

/* pad names for sr, mercury5 is different */
#define MSC313_PINNAME_SR_IO2		"sr_io2"
#define MSC313_PINNAME_SR_IO3		"sr_io3"
#define MSC313_PINNAME_SR_IO4		"sr_io4"
#define MSC313_PINNAME_SR_IO5		"sr_io5"
#define MSC313_PINNAME_SR_IO6		"sr_io6"
#define MSC313_PINNAME_SR_IO7		"sr_io7"
#define MSC313_PINNAME_SR_IO8		"sr_io8"
#define MSC313_PINNAME_SR_IO9		"sr_io9"
#define MSC313_PINNAME_SR_IO10		"sr_io10"
#define MSC313_PINNAME_SR_IO11		"sr_io11"
#define MSC313_PINNAME_SR_IO12		"sr_io12"
#define MSC313_PINNAME_SR_IO13		"sr_io13"
#define MSC313_PINNAME_SR_IO14		"sr_io14"
#define MSC313_PINNAME_SR_IO15		"sr_io15"
#define MSC313_PINNAME_SR_IO16		"sr_io16"
#define MSC313_PINNAME_SR_IO17		"sr_io17"

/* pad names for sd, same for all SoCs so far */
#define MSC313_PINNAME_SD_CLK		"sd_clk"
#define MSC313_PINNAME_SD_CMD		"sd_cmd"
#define MSC313_PINNAME_SD_D0		"sd_d0"
#define MSC313_PINNAME_SD_D1		"sd_d1"
#define MSC313_PINNAME_SD_D2		"sd_d2"
#define MSC313_PINNAME_SD_D3		"sd_d3"

/* pad names for i2c1, same for all SoCs so for */
#define MSC313_PINNAME_I2C1_SCL		"i2c1_scl"
#define MSC313_PINNAME_I2C1_SCA		"i2c1_sda"

/* pad names for spi0, same for all SoCs so far */
#define MSC313_PINNAME_SPI0_CZ		"spi0_cz"
#define MSC313_PINNAME_SPI0_CK		"spi0_ck"
#define MSC313_PINNAME_SPI0_DI		"spi0_di"
#define MSC313_PINNAME_SPI0_DO		"spi0_do"

#define FUART_NAMES			\
	MSC313_PINNAME_FUART_RX,	\
	MSC313_PINNAME_FUART_TX,	\
	MSC313_PINNAME_FUART_CTS,	\
	MSC313_PINNAME_FUART_RTS

#define OFF_FUART_RX	0x50
#define OFF_FUART_TX	0x54
#define OFF_FUART_CTS	0x58
#define OFF_FUART_RTS	0x5c

#define FUART_OFFSETS	\
	OFF_FUART_RX,	\
	OFF_FUART_TX,	\
	OFF_FUART_CTS,	\
	OFF_FUART_RTS

#define SR_NAMES		\
	MSC313_PINNAME_SR_IO2,	\
	MSC313_PINNAME_SR_IO3,	\
	MSC313_PINNAME_SR_IO4,	\
	MSC313_PINNAME_SR_IO5,	\
	MSC313_PINNAME_SR_IO6,	\
	MSC313_PINNAME_SR_IO7,	\
	MSC313_PINNAME_SR_IO8,	\
	MSC313_PINNAME_SR_IO9,	\
	MSC313_PINNAME_SR_IO10,	\
	MSC313_PINNAME_SR_IO11,	\
	MSC313_PINNAME_SR_IO12,	\
	MSC313_PINNAME_SR_IO13,	\
	MSC313_PINNAME_SR_IO14,	\
	MSC313_PINNAME_SR_IO15,	\
	MSC313_PINNAME_SR_IO16,	\
	MSC313_PINNAME_SR_IO17

#define OFF_SR_IO2	0x88
#define OFF_SR_IO3	0x8c
#define OFF_SR_IO4	0x90
#define OFF_SR_IO5	0x94
#define OFF_SR_IO6	0x98
#define OFF_SR_IO7	0x9c
#define OFF_SR_IO8	0xa0
#define OFF_SR_IO9	0xa4
#define OFF_SR_IO10	0xa8
#define OFF_SR_IO11	0xac
#define OFF_SR_IO12	0xb0
#define OFF_SR_IO13	0xb4
#define OFF_SR_IO14	0xb8
#define OFF_SR_IO15	0xbc
#define OFF_SR_IO16	0xc0
#define OFF_SR_IO17	0xc4

#define SR_OFFSETS	\
	OFF_SR_IO2,	\
	OFF_SR_IO3,	\
	OFF_SR_IO4,	\
	OFF_SR_IO5,	\
	OFF_SR_IO6,	\
	OFF_SR_IO7,	\
	OFF_SR_IO8,	\
	OFF_SR_IO9,	\
	OFF_SR_IO10,	\
	OFF_SR_IO11,	\
	OFF_SR_IO12,	\
	OFF_SR_IO13,	\
	OFF_SR_IO14,	\
	OFF_SR_IO15,	\
	OFF_SR_IO16,	\
	OFF_SR_IO17

#define SD_NAMES		\
	MSC313_PINNAME_SD_CLK,	\
	MSC313_PINNAME_SD_CMD,	\
	MSC313_PINNAME_SD_D0,	\
	MSC313_PINNAME_SD_D1,	\
	MSC313_PINNAME_SD_D2,	\
	MSC313_PINNAME_SD_D3

#define OFF_SD_CLK	0x140
#define OFF_SD_CMD	0x144
#define OFF_SD_D0	0x148
#define OFF_SD_D1	0x14c
#define OFF_SD_D2	0x150
#define OFF_SD_D3	0x154

#define SD_OFFSETS	\
	OFF_SD_CLK,	\
	OFF_SD_CMD,	\
	OFF_SD_D0,	\
	OFF_SD_D1,	\
	OFF_SD_D2,	\
	OFF_SD_D3

#define I2C1_NAMES			\
	MSC313_PINNAME_I2C1_SCL,	\
	MSC313_PINNAME_I2C1_SCA

#define OFF_I2C1_SCL	0x188
#define OFF_I2C1_SCA	0x18c

#define I2C1_OFFSETS	\
	OFF_I2C1_SCL,	\
	OFF_I2C1_SCA

#define SPI0_NAMES		\
	MSC313_PINNAME_SPI0_CZ,	\
	MSC313_PINNAME_SPI0_CK,	\
	MSC313_PINNAME_SPI0_DI,	\
	MSC313_PINNAME_SPI0_DO

#define OFF_SPI0_CZ	0x1c0
#define OFF_SPI0_CK	0x1c4
#define OFF_SPI0_DI	0x1c8
#define OFF_SPI0_DO	0x1cc

#define SPI0_OFFSETS	\
	OFF_SPI0_CZ,	\
	OFF_SPI0_CK,	\
	OFF_SPI0_DI,	\
	OFF_SPI0_DO

struct msc313_gpio_data {
	const char * const *names;
	const unsigned int *offsets;
	const unsigned int num;
};

#define MSC313_GPIO_CHIPDATA(_chip) \
static const struct msc313_gpio_data _chip##_data = { \
	.names = _chip##_names, \
	.offsets = _chip##_offsets, \
	.num = ARRAY_SIZE(_chip##_offsets), \
}

#ifdef CONFIG_MACH_INFINITY
static const char * const msc313_names[] = {
	FUART_NAMES,
	SR_NAMES,
	SD_NAMES,
	I2C1_NAMES,
	SPI0_NAMES,
};

static const unsigned int msc313_offsets[] = {
	FUART_OFFSETS,
	SR_OFFSETS,
	SD_OFFSETS,
	I2C1_OFFSETS,
	SPI0_OFFSETS,
};

MSC313_GPIO_CHIPDATA(msc313);

/*
 * Unlike the msc313(e) the ssd20xd have a bunch of pins
 * that are actually called gpio probably because they
 * have no dedicated function.
 */
#define SSD20XD_PINNAME_GPIO0		"gpio0"
#define SSD20XD_PINNAME_GPIO1		"gpio1"
#define SSD20XD_PINNAME_GPIO2		"gpio2"
#define SSD20XD_PINNAME_GPIO3		"gpio3"
#define SSD20XD_PINNAME_GPIO4		"gpio4"
#define SSD20XD_PINNAME_GPIO5		"gpio5"
#define SSD20XD_PINNAME_GPIO6		"gpio6"
#define SSD20XD_PINNAME_GPIO7		"gpio7"
#define SSD20XD_PINNAME_GPIO10		"gpio10"
#define SSD20XD_PINNAME_GPIO11		"gpio11"
#define SSD20XD_PINNAME_GPIO12		"gpio12"
#define SSD20XD_PINNAME_GPIO13		"gpio13"
#define SSD20XD_PINNAME_GPIO14		"gpio14"
#define SSD20XD_PINNAME_GPIO85		"gpio85"
#define SSD20XD_PINNAME_GPIO86		"gpio86"
#define SSD20XD_PINNAME_GPIO90		"gpio90"

#define SSD20XD_GPIO_NAMES SSD20XD_PINNAME_GPIO0,  \
			   SSD20XD_PINNAME_GPIO1,  \
			   SSD20XD_PINNAME_GPIO2,  \
			   SSD20XD_PINNAME_GPIO3,  \
			   SSD20XD_PINNAME_GPIO4,  \
			   SSD20XD_PINNAME_GPIO5,  \
			   SSD20XD_PINNAME_GPIO6,  \
			   SSD20XD_PINNAME_GPIO7,  \
			   SSD20XD_PINNAME_GPIO10, \
			   SSD20XD_PINNAME_GPIO11, \
			   SSD20XD_PINNAME_GPIO12, \
			   SSD20XD_PINNAME_GPIO13, \
			   SSD20XD_PINNAME_GPIO14, \
			   SSD20XD_PINNAME_GPIO85, \
			   SSD20XD_PINNAME_GPIO86, \
			   SSD20XD_PINNAME_GPIO90

#define SSD20XD_GPIO_OFF_GPIO0 0x0
#define SSD20XD_GPIO_OFF_GPIO1 0x4
#define SSD20XD_GPIO_OFF_GPIO2 0x8
#define SSD20XD_GPIO_OFF_GPIO3 0xc
#define SSD20XD_GPIO_OFF_GPIO4 0x10
#define SSD20XD_GPIO_OFF_GPIO5 0x14
#define SSD20XD_GPIO_OFF_GPIO6 0x18
#define SSD20XD_GPIO_OFF_GPIO7 0x1c
#define SSD20XD_GPIO_OFF_GPIO10 0x28
#define SSD20XD_GPIO_OFF_GPIO11 0x2c
#define SSD20XD_GPIO_OFF_GPIO12 0x30
#define SSD20XD_GPIO_OFF_GPIO13 0x34
#define SSD20XD_GPIO_OFF_GPIO14 0x38
#define SSD20XD_GPIO_OFF_GPIO85 0x100
#define SSD20XD_GPIO_OFF_GPIO86 0x104
#define SSD20XD_GPIO_OFF_GPIO90 0x114

#define SSD20XD_GPIO_OFFSETS SSD20XD_GPIO_OFF_GPIO0,  \
			     SSD20XD_GPIO_OFF_GPIO1,  \
			     SSD20XD_GPIO_OFF_GPIO2,  \
			     SSD20XD_GPIO_OFF_GPIO3,  \
			     SSD20XD_GPIO_OFF_GPIO4,  \
			     SSD20XD_GPIO_OFF_GPIO5,  \
			     SSD20XD_GPIO_OFF_GPIO6,  \
			     SSD20XD_GPIO_OFF_GPIO7,  \
			     SSD20XD_GPIO_OFF_GPIO10, \
			     SSD20XD_GPIO_OFF_GPIO11, \
			     SSD20XD_GPIO_OFF_GPIO12, \
			     SSD20XD_GPIO_OFF_GPIO13, \
			     SSD20XD_GPIO_OFF_GPIO14, \
			     SSD20XD_GPIO_OFF_GPIO85, \
			     SSD20XD_GPIO_OFF_GPIO86, \
			     SSD20XD_GPIO_OFF_GPIO90

/* "ttl" pins lcd interface pins */
#define SSD20XD_PINNAME_TTL0	"ttl0"
#define SSD20XD_PINNAME_TTL1	"ttl1"
#define SSD20XD_PINNAME_TTL2	"ttl2"
#define SSD20XD_PINNAME_TTL3	"ttl3"
#define SSD20XD_PINNAME_TTL4	"ttl4"
#define SSD20XD_PINNAME_TTL5	"ttl5"
#define SSD20XD_PINNAME_TTL6	"ttl6"
#define SSD20XD_PINNAME_TTL7	"ttl7"
#define SSD20XD_PINNAME_TTL8	"ttl8"
#define SSD20XD_PINNAME_TTL9	"ttl9"
#define SSD20XD_PINNAME_TTL10	"ttl10"
#define SSD20XD_PINNAME_TTL11	"ttl11"
#define SSD20XD_PINNAME_TTL12	"ttl12"
#define SSD20XD_PINNAME_TTL13	"ttl13"
#define SSD20XD_PINNAME_TTL14	"ttl14"
#define SSD20XD_PINNAME_TTL15	"ttl15"
#define SSD20XD_PINNAME_TTL16	"ttl16"
#define SSD20XD_PINNAME_TTL17	"ttl17"
#define SSD20XD_PINNAME_TTL18	"ttl18"
#define SSD20XD_PINNAME_TTL19	"ttl19"
#define SSD20XD_PINNAME_TTL20	"ttl20"
#define SSD20XD_PINNAME_TTL21	"ttl21"
#define SSD20XD_PINNAME_TTL22	"ttl22"
#define SSD20XD_PINNAME_TTL23	"ttl23"
#define SSD20XD_PINNAME_TTL24	"ttl24"
#define SSD20XD_PINNAME_TTL25	"ttl25"
#define SSD20XD_PINNAME_TTL26	"ttl26"
#define SSD20XD_PINNAME_TTL27	"ttl27"

#define SSD20XD_TTL_PINNAMES SSD20XD_PINNAME_TTL0,  \
			     SSD20XD_PINNAME_TTL1,  \
			     SSD20XD_PINNAME_TTL2,  \
			     SSD20XD_PINNAME_TTL3,  \
			     SSD20XD_PINNAME_TTL4,  \
			     SSD20XD_PINNAME_TTL5,  \
			     SSD20XD_PINNAME_TTL6,  \
			     SSD20XD_PINNAME_TTL7,  \
			     SSD20XD_PINNAME_TTL8,  \
			     SSD20XD_PINNAME_TTL9,  \
			     SSD20XD_PINNAME_TTL10, \
			     SSD20XD_PINNAME_TTL11, \
			     SSD20XD_PINNAME_TTL12, \
			     SSD20XD_PINNAME_TTL13, \
			     SSD20XD_PINNAME_TTL14, \
			     SSD20XD_PINNAME_TTL15, \
			     SSD20XD_PINNAME_TTL16, \
			     SSD20XD_PINNAME_TTL17, \
			     SSD20XD_PINNAME_TTL18, \
			     SSD20XD_PINNAME_TTL19, \
			     SSD20XD_PINNAME_TTL20, \
			     SSD20XD_PINNAME_TTL21, \
			     SSD20XD_PINNAME_TTL22, \
			     SSD20XD_PINNAME_TTL23, \
			     SSD20XD_PINNAME_TTL24, \
			     SSD20XD_PINNAME_TTL25, \
			     SSD20XD_PINNAME_TTL26, \
			     SSD20XD_PINNAME_TTL27

#define SSD20XD_TTL_OFFSET_TTL0		0x80
#define SSD20XD_TTL_OFFSET_TTL1		0x84
#define SSD20XD_TTL_OFFSET_TTL2		0x88
#define SSD20XD_TTL_OFFSET_TTL3		0x8c
#define SSD20XD_TTL_OFFSET_TTL4		0x90
#define SSD20XD_TTL_OFFSET_TTL5		0x94
#define SSD20XD_TTL_OFFSET_TTL6		0x98
#define SSD20XD_TTL_OFFSET_TTL7		0x9c
#define SSD20XD_TTL_OFFSET_TTL8		0xa0
#define SSD20XD_TTL_OFFSET_TTL9		0xa4
#define SSD20XD_TTL_OFFSET_TTL10	0xa8
#define SSD20XD_TTL_OFFSET_TTL11	0xac
#define SSD20XD_TTL_OFFSET_TTL12	0xb0
#define SSD20XD_TTL_OFFSET_TTL13	0xb4
#define SSD20XD_TTL_OFFSET_TTL14	0xb8
#define SSD20XD_TTL_OFFSET_TTL15	0xbc
#define SSD20XD_TTL_OFFSET_TTL16	0xc0
#define SSD20XD_TTL_OFFSET_TTL17	0xc4
#define SSD20XD_TTL_OFFSET_TTL18	0xc8
#define SSD20XD_TTL_OFFSET_TTL19	0xcc
#define SSD20XD_TTL_OFFSET_TTL20	0xd0
#define SSD20XD_TTL_OFFSET_TTL21	0xd4
#define SSD20XD_TTL_OFFSET_TTL22	0xd8
#define SSD20XD_TTL_OFFSET_TTL23	0xdc
#define SSD20XD_TTL_OFFSET_TTL24	0xe0
#define SSD20XD_TTL_OFFSET_TTL25	0xe4
#define SSD20XD_TTL_OFFSET_TTL26	0xe8
#define SSD20XD_TTL_OFFSET_TTL27	0xec

#define SSD20XD_TTL_OFFSETS SSD20XD_TTL_OFFSET_TTL0,  \
			    SSD20XD_TTL_OFFSET_TTL1,  \
			    SSD20XD_TTL_OFFSET_TTL2,  \
			    SSD20XD_TTL_OFFSET_TTL3,  \
			    SSD20XD_TTL_OFFSET_TTL4,  \
			    SSD20XD_TTL_OFFSET_TTL5,  \
			    SSD20XD_TTL_OFFSET_TTL6,  \
			    SSD20XD_TTL_OFFSET_TTL7,  \
			    SSD20XD_TTL_OFFSET_TTL8,  \
			    SSD20XD_TTL_OFFSET_TTL9,  \
			    SSD20XD_TTL_OFFSET_TTL10, \
			    SSD20XD_TTL_OFFSET_TTL11, \
			    SSD20XD_TTL_OFFSET_TTL12, \
			    SSD20XD_TTL_OFFSET_TTL13, \
			    SSD20XD_TTL_OFFSET_TTL14, \
			    SSD20XD_TTL_OFFSET_TTL15, \
			    SSD20XD_TTL_OFFSET_TTL16, \
			    SSD20XD_TTL_OFFSET_TTL17, \
			    SSD20XD_TTL_OFFSET_TTL18, \
			    SSD20XD_TTL_OFFSET_TTL19, \
			    SSD20XD_TTL_OFFSET_TTL20, \
			    SSD20XD_TTL_OFFSET_TTL21, \
			    SSD20XD_TTL_OFFSET_TTL22, \
			    SSD20XD_TTL_OFFSET_TTL23, \
			    SSD20XD_TTL_OFFSET_TTL24, \
			    SSD20XD_TTL_OFFSET_TTL25, \
			    SSD20XD_TTL_OFFSET_TTL26, \
			    SSD20XD_TTL_OFFSET_TTL27

/* On the ssd20xd the two normal uarts have dedicated pins */
#define SSD20XD_PINNAME_UART0_RX	"uart0_rx"
#define SSD20XD_PINNAME_UART0_TX	"uart0_tx"

#define SSD20XD_UART0_NAMES	  \
	SSD20XD_PINNAME_UART0_RX, \
	SSD20XD_PINNAME_UART0_TX

#define SSD20XD_PINNAME_UART1_RX	"uart1_rx"
#define SSD20XD_PINNAME_UART1_TX	"uart1_tx"

#define SSD20XD_UART1_NAMES	  \
	SSD20XD_PINNAME_UART1_RX, \
	SSD20XD_PINNAME_UART1_TX

#define SSD20XD_OFF_UART0_RX	0x60
#define SSD20XD_OFF_UART0_TX	0x64

#define SSD20XD_UART0_OFFSETS \
	SSD20XD_OFF_UART0_RX, \
	SSD20XD_OFF_UART0_TX

#define SSD20XD_OFF_UART1_RX	0x68
#define SSD20XD_OFF_UART1_TX	0x6c

#define SSD20XD_UART1_OFFSETS \
	SSD20XD_OFF_UART1_RX, \
	SSD20XD_OFF_UART1_TX

/*
 * ssd20x has the same pin names but different ordering
 * of the registers that control the gpio.
 */
#define SSD20XD_OFF_SD_D0	0x140
#define SSD20XD_OFF_SD_D1	0x144
#define SSD20XD_OFF_SD_D2	0x148
#define SSD20XD_OFF_SD_D3	0x14c
#define SSD20XD_OFF_SD_CMD	0x150
#define SSD20XD_OFF_SD_CLK	0x154

#define SSD20XD_SD_OFFSETS	SSD20XD_OFF_SD_CLK, \
				SSD20XD_OFF_SD_CMD, \
				SSD20XD_OFF_SD_D0,  \
				SSD20XD_OFF_SD_D1,  \
				SSD20XD_OFF_SD_D2,  \
				SSD20XD_OFF_SD_D3

static const char * const ssd20xd_names[] = {
	FUART_NAMES,
	SD_NAMES,
	SSD20XD_UART0_NAMES,
	SSD20XD_UART1_NAMES,
	SSD20XD_TTL_PINNAMES,
	SSD20XD_GPIO_NAMES,
};

static const unsigned int ssd20xd_offsets[] = {
	FUART_OFFSETS,
	SSD20XD_SD_OFFSETS,
	SSD20XD_UART0_OFFSETS,
	SSD20XD_UART1_OFFSETS,
	SSD20XD_TTL_OFFSETS,
	SSD20XD_GPIO_OFFSETS,
};

MSC313_GPIO_CHIPDATA(ssd20xd);
#endif

struct msc313_gpio {
	void __iomem *base;
	const struct msc313_gpio_data *gpio_data;
	u8 *saved;
};

static void msc313_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct msc313_gpio *gpio = gpiochip_get_data(chip);
	u8 gpioreg = readb_relaxed(gpio->base + gpio->gpio_data->offsets[offset]);

	if (value)
		gpioreg |= MSC313_GPIO_OUT;
	else
		gpioreg &= ~MSC313_GPIO_OUT;

	writeb_relaxed(gpioreg, gpio->base + gpio->gpio_data->offsets[offset]);
}

static int msc313_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct msc313_gpio *gpio = gpiochip_get_data(chip);

	return readb_relaxed(gpio->base + gpio->gpio_data->offsets[offset]) & MSC313_GPIO_IN;
}

static int msc313_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct msc313_gpio *gpio = gpiochip_get_data(chip);
	u8 gpioreg = readb_relaxed(gpio->base + gpio->gpio_data->offsets[offset]);

	gpioreg |= MSC313_GPIO_OEN;
	writeb_relaxed(gpioreg, gpio->base + gpio->gpio_data->offsets[offset]);

	return 0;
}

static int msc313_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct msc313_gpio *gpio = gpiochip_get_data(chip);
	u8 gpioreg = readb_relaxed(gpio->base + gpio->gpio_data->offsets[offset]);

	gpioreg &= ~MSC313_GPIO_OEN;
	if (value)
		gpioreg |= MSC313_GPIO_OUT;
	else
		gpioreg &= ~MSC313_GPIO_OUT;
	writeb_relaxed(gpioreg, gpio->base + gpio->gpio_data->offsets[offset]);

	return 0;
}

/*
 * The interrupt handling happens in the parent interrupt controller,
 * we don't do anything here.
 */
static struct irq_chip msc313_gpio_irqchip = {
	.name = "GPIO",
	.irq_eoi = irq_chip_eoi_parent,
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_set_type = irq_chip_set_type_parent,
	.irq_set_affinity = irq_chip_set_affinity_parent,
};

/*
 * The parent interrupt controller needs the GIC interrupt type set to GIC_SPI
 * so we need to provide the fwspec. Essentially gpiochip_populate_parent_fwspec_twocell
 * that puts GIC_SPI into the first cell.
 */
static int msc313_gpio_populate_parent_fwspec(struct gpio_chip *gc,
					      union gpio_irq_fwspec *gfwspec,
					      unsigned int parent_hwirq,
					      unsigned int parent_type)
{
	struct irq_fwspec *fwspec = &gfwspec->fwspec;

	fwspec->fwnode = gc->irq.parent_domain->fwnode;
	fwspec->param_count = 3;
	fwspec->param[0] = GIC_SPI;
	fwspec->param[1] = parent_hwirq;
	fwspec->param[2] = parent_type;

	return 0;
}

static int msc313e_gpio_child_to_parent_hwirq(struct gpio_chip *chip,
					     unsigned int child,
					     unsigned int child_type,
					     unsigned int *parent,
					     unsigned int *parent_type)
{
	struct msc313_gpio *priv = gpiochip_get_data(chip);
	unsigned int offset = priv->gpio_data->offsets[child];

	/*
	 * only the spi0 pins have interrupts on the parent
	 * on all of the known chips and so far they are all
	 * mapped to the same place
	 */
	if (offset >= OFF_SPI0_CZ && offset <= OFF_SPI0_DO) {
		*parent_type = child_type;
		*parent = ((offset - OFF_SPI0_CZ) >> 2) + 28;
		return 0;
	}

	return -EINVAL;
}

static int msc313_gpio_probe(struct platform_device *pdev)
{
	const struct msc313_gpio_data *match_data;
	struct msc313_gpio *gpio;
	struct gpio_chip *gpiochip;
	struct gpio_irq_chip *gpioirqchip;
	struct irq_domain *parent_domain;
	struct device_node *parent_node;
	struct device *dev = &pdev->dev;

	match_data = of_device_get_match_data(dev);
	if (!match_data)
		return -EINVAL;

	parent_node = of_irq_find_parent(dev->of_node);
	if (!parent_node)
		return -ENODEV;

	parent_domain = irq_find_host(parent_node);
	if (!parent_domain)
		return -ENODEV;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->gpio_data = match_data;

	gpio->saved = devm_kcalloc(dev, gpio->gpio_data->num, sizeof(*gpio->saved), GFP_KERNEL);
	if (!gpio->saved)
		return -ENOMEM;

	gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	platform_set_drvdata(pdev, gpio);

	gpiochip = devm_kzalloc(dev, sizeof(*gpiochip), GFP_KERNEL);
	if (!gpiochip)
		return -ENOMEM;

	gpiochip->label = DRIVER_NAME;
	gpiochip->parent = dev;
	gpiochip->request = gpiochip_generic_request;
	gpiochip->free = gpiochip_generic_free;
	gpiochip->direction_input = msc313_gpio_direction_input;
	gpiochip->direction_output = msc313_gpio_direction_output;
	gpiochip->get = msc313_gpio_get;
	gpiochip->set = msc313_gpio_set;
	gpiochip->base = -1;
	gpiochip->ngpio = gpio->gpio_data->num;
	gpiochip->names = gpio->gpio_data->names;

	gpioirqchip = &gpiochip->irq;
	gpioirqchip->chip = &msc313_gpio_irqchip;
	gpioirqchip->fwnode = of_node_to_fwnode(dev->of_node);
	gpioirqchip->parent_domain = parent_domain;
	gpioirqchip->child_to_parent_hwirq = msc313e_gpio_child_to_parent_hwirq;
	gpioirqchip->populate_parent_alloc_arg = msc313_gpio_populate_parent_fwspec;
	gpioirqchip->handler = handle_bad_irq;
	gpioirqchip->default_type = IRQ_TYPE_NONE;

	return devm_gpiochip_add_data(dev, gpiochip, gpio);
}

static const struct of_device_id msc313_gpio_of_match[] = {
#ifdef CONFIG_MACH_INFINITY
	{
		.compatible = "mstar,msc313-gpio",
		.data = &msc313_data,
	},
	{
		.compatible = "sstar,ssd20xd-gpio",
		.data = &ssd20xd_data,
	},
#endif
	{ }
};

/*
 * The GPIO controller loses the state of the registers when the
 * SoC goes into suspend to memory mode so we need to save some
 * of the register bits before suspending and put it back when resuming
 */
static int __maybe_unused msc313_gpio_suspend(struct device *dev)
{
	struct msc313_gpio *gpio = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < gpio->gpio_data->num; i++)
		gpio->saved[i] = readb_relaxed(gpio->base + gpio->gpio_data->offsets[i]) & MSC313_GPIO_BITSTOSAVE;

	return 0;
}

static int __maybe_unused msc313_gpio_resume(struct device *dev)
{
	struct msc313_gpio *gpio = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < gpio->gpio_data->num; i++)
		writeb_relaxed(gpio->saved[i], gpio->base + gpio->gpio_data->offsets[i]);

	return 0;
}

static SIMPLE_DEV_PM_OPS(msc313_gpio_ops, msc313_gpio_suspend, msc313_gpio_resume);

static struct platform_driver msc313_gpio_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = msc313_gpio_of_match,
		.pm = &msc313_gpio_ops,
	},
	.probe = msc313_gpio_probe,
};
builtin_platform_driver(msc313_gpio_driver);
