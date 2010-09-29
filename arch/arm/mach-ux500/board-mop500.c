/*
 * Copyright (C) 2008-2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/spi/spi.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/tc35892.h>
#include <linux/input/matrix_keypad.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/pincfg.h>
#include <plat/i2c.h>
#include <plat/ske.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/irqs.h>

#include "devices-db8500.h"
#include "pins-db8500.h"
#include "board-mop500.h"

static pin_cfg_t mop500_pins[] = {
	/* SSP0 */
	GPIO143_SSP0_CLK,
	GPIO144_SSP0_FRM,
	GPIO145_SSP0_RXD,
	GPIO146_SSP0_TXD,

	/* I2C */
	GPIO147_I2C0_SCL,
	GPIO148_I2C0_SDA,
	GPIO16_I2C1_SCL,
	GPIO17_I2C1_SDA,
	GPIO10_I2C2_SDA,
	GPIO11_I2C2_SCL,
	GPIO229_I2C3_SDA,
	GPIO230_I2C3_SCL,

	/* SKE keypad */
	GPIO153_KP_I7,
	GPIO154_KP_I6,
	GPIO155_KP_I5,
	GPIO156_KP_I4,
	GPIO157_KP_O7,
	GPIO158_KP_O6,
	GPIO159_KP_O5,
	GPIO160_KP_O4,
	GPIO161_KP_I3,
	GPIO162_KP_I2,
	GPIO163_KP_I1,
	GPIO164_KP_I0,
	GPIO165_KP_O3,
	GPIO166_KP_O2,
	GPIO167_KP_O1,
	GPIO168_KP_O0,

	GPIO217_GPIO,		/* GPIO_EXP_INT */
};

static void ab4500_spi_cs_control(u32 command)
{
	/* set the FRM signal, which is CS  - TODO */
}

struct pl022_config_chip ab4500_chip_info = {
	.com_mode = INTERRUPT_TRANSFER,
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	/* we can act as master only */
	.hierarchy = SSP_MASTER,
	.slave_tx_disable = 0,
	.rx_lev_trig = SSP_RX_1_OR_MORE_ELEM,
	.tx_lev_trig = SSP_TX_1_OR_MORE_EMPTY_LOC,
	.cs_control = ab4500_spi_cs_control,
};

static struct ab8500_platform_data ab8500_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
};

static struct resource ab8500_resources[] = {
	[0] = {
		.start = IRQ_AB8500,
		.end = IRQ_AB8500,
		.flags = IORESOURCE_IRQ
	}
};

struct platform_device ab8500_device = {
	.name = "ab8500-i2c",
	.id = 0,
	.dev = {
		.platform_data = &ab8500_platdata,
	},
	.num_resources = 1,
	.resource = ab8500_resources,
};

static struct spi_board_info ab8500_spi_devices[] = {
	{
		.modalias = "ab8500-spi",
		.controller_data = &ab4500_chip_info,
		.platform_data = &ab8500_platdata,
		.max_speed_hz = 12000000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_3,
		.irq = IRQ_DB8500_AB8500,
	},
};

static struct pl022_ssp_controller ssp0_platform_data = {
	.bus_id = 0,
	/* pl022 not yet supports dma */
	.enable_dma = 0,
	/* on this platform, gpio 31,142,144,214 &
	 * 224 are connected as chip selects
	 */
	.num_chipselect = 5,
};

/*
 * TC35892
 */

static void mop500_tc35892_init(struct tc35892 *tc35892, unsigned int base)
{
	mop500_sdi_tc35892_init();
}

static struct tc35892_gpio_platform_data mop500_tc35892_gpio_data = {
	.gpio_base	= MOP500_EGPIO(0),
	.setup		= mop500_tc35892_init,
};

static struct tc35892_platform_data mop500_tc35892_data = {
	.gpio		= &mop500_tc35892_gpio_data,
	.irq_base	= MOP500_EGPIO_IRQ_BASE,
};

static struct i2c_board_info mop500_i2c0_devices[] = {
	{
		I2C_BOARD_INFO("tc35892", 0x42),
		.irq            = NOMADIK_GPIO_TO_IRQ(217),
		.platform_data  = &mop500_tc35892_data,
	},
};

#define U8500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, _sm) \
static struct nmk_i2c_controller u8500_i2c##id##_data = { \
	/*				\
	 * slave data setup time, which is	\
	 * 250 ns,100ns,10ns which is 14,6,2	\
	 * respectively for a 48 Mhz	\
	 * i2c clock			\
	 */				\
	.slsu		= _slsu,	\
	/* Tx FIFO threshold */		\
	.tft		= _tft,		\
	/* Rx FIFO threshold */		\
	.rft		= _rft,		\
	/* std. mode operation */	\
	.clk_freq	= clk,		\
	.sm		= _sm,		\
}

/*
 * The board uses 4 i2c controllers, initialize all of
 * them with slave data setup time of 250 ns,
 * Tx & Rx FIFO threshold values as 1 and standard
 * mode of operation
 */
U8500_I2C_CONTROLLER(0, 0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);
U8500_I2C_CONTROLLER(1, 0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);
U8500_I2C_CONTROLLER(2,	0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);
U8500_I2C_CONTROLLER(3,	0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);

static void __init mop500_i2c_init(void)
{
	db8500_add_i2c0(&u8500_i2c0_data);
	db8500_add_i2c1(&u8500_i2c1_data);
	db8500_add_i2c2(&u8500_i2c2_data);
	db8500_add_i2c3(&u8500_i2c3_data);
}

static const unsigned int ux500_keymap[] = {
	KEY(2, 5, KEY_END),
	KEY(4, 1, KEY_POWER),
	KEY(3, 5, KEY_VOLUMEDOWN),
	KEY(1, 3, KEY_3),
	KEY(5, 2, KEY_RIGHT),
	KEY(5, 0, KEY_9),

	KEY(0, 5, KEY_MENU),
	KEY(7, 6, KEY_ENTER),
	KEY(4, 5, KEY_0),
	KEY(6, 7, KEY_2),
	KEY(3, 4, KEY_UP),
	KEY(3, 3, KEY_DOWN),

	KEY(6, 4, KEY_SEND),
	KEY(6, 2, KEY_BACK),
	KEY(4, 2, KEY_VOLUMEUP),
	KEY(5, 5, KEY_1),
	KEY(4, 3, KEY_LEFT),
	KEY(3, 2, KEY_7),
};

static const struct matrix_keymap_data ux500_keymap_data = {
	.keymap         = ux500_keymap,
	.keymap_size    = ARRAY_SIZE(ux500_keymap),
};

/*
 * Nomadik SKE keypad
 */
#define ROW_PIN_I0      164
#define ROW_PIN_I1      163
#define ROW_PIN_I2      162
#define ROW_PIN_I3      161
#define ROW_PIN_I4      156
#define ROW_PIN_I5      155
#define ROW_PIN_I6      154
#define ROW_PIN_I7      153
#define COL_PIN_O0      168
#define COL_PIN_O1      167
#define COL_PIN_O2      166
#define COL_PIN_O3      165
#define COL_PIN_O4      160
#define COL_PIN_O5      159
#define COL_PIN_O6      158
#define COL_PIN_O7      157

#define SKE_KPD_MAX_ROWS        8
#define SKE_KPD_MAX_COLS        8

static int ske_kp_rows[] = {
	ROW_PIN_I0, ROW_PIN_I1, ROW_PIN_I2, ROW_PIN_I3,
	ROW_PIN_I4, ROW_PIN_I5, ROW_PIN_I6, ROW_PIN_I7,
};

/*
 * ske_set_gpio_row: request and set gpio rows
 */
static int ske_set_gpio_row(int gpio)
{
	int ret;

	ret = gpio_request(gpio, "ske-kp");
	if (ret < 0) {
		pr_err("ske_set_gpio_row: gpio request failed\n");
		return ret;
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret < 0) {
		pr_err("ske_set_gpio_row: gpio direction failed\n");
		gpio_free(gpio);
	}

	return ret;
}

/*
 * ske_kp_init - enable the gpio configuration
 */
static int ske_kp_init(void)
{
	int ret, i;

	for (i = 0; i < SKE_KPD_MAX_ROWS; i++) {
		ret = ske_set_gpio_row(ske_kp_rows[i]);
		if (ret < 0) {
			pr_err("ske_kp_init: failed init\n");
			return ret;
		}
	}

	return 0;
}

static struct ske_keypad_platform_data ske_keypad_board = {
	.init           = ske_kp_init,
	.keymap_data    = &ux500_keymap_data,
	.no_autorepeat  = true,
	.krow           = SKE_KPD_MAX_ROWS,     /* 8x8 matrix */
	.kcol           = SKE_KPD_MAX_COLS,
	.debounce_ms    = 40,                   /* in millsecs */
};



/* add any platform devices here - TODO */
static struct platform_device *platform_devs[] __initdata = {
	&ux500_ske_keypad_device,
};

static void __init mop500_spi_init(void)
{
	db8500_add_ssp0(&ssp0_platform_data);
}

static void __init mop500_uart_init(void)
{
	db8500_add_uart0();
	db8500_add_uart1();
	db8500_add_uart2();
}

static void __init u8500_init_machine(void)
{
	u8500_init_devices();

	nmk_config_pins(mop500_pins, ARRAY_SIZE(mop500_pins));

	ux500_ske_keypad_device.dev.platform_data = &ske_keypad_board;
	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	mop500_i2c_init();
	mop500_sdi_init();
	mop500_spi_init();
	mop500_uart_init();

	/* If HW is early drop (ED) or V1.0 then use SPI to access AB8500 */
	if (cpu_is_u8500ed() || cpu_is_u8500v10())
		spi_register_board_info(ab8500_spi_devices,
			ARRAY_SIZE(ab8500_spi_devices));
	else /* If HW is v.1.1 or later use I2C to access AB8500 */
		platform_device_register(&ab8500_device);

	i2c_register_board_info(0, mop500_i2c0_devices,
				ARRAY_SIZE(mop500_i2c0_devices));
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.init_machine	= u8500_init_machine,
MACHINE_END
