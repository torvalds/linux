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
#include <linux/amba/serial.h>
#include <linux/spi/spi.h>
#include <linux/mfd/ab8500.h>
#include <linux/regulator/ab8500.h>
#include <linux/mfd/tc3589x.h>
#include <linux/mfd/tps6105x.h>
#include <linux/mfd/ab8500/gpio.h>
#include <linux/leds-lp5521.h>
#include <linux/input.h>
#include <linux/smsc911x.h>
#include <linux/gpio_keys.h>
#include <linux/delay.h>

#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/i2c.h>
#include <plat/ste_dma40.h>
#include <plat/pincfg.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/irqs.h>

#include "pins-db8500.h"
#include "ste-dma40-db8500.h"
#include "devices-db8500.h"
#include "board-mop500.h"
#include "board-mop500-regulators.h"

static struct gpio_led snowball_led_array[] = {
	{
		.name = "user_led",
		.default_trigger = "none",
		.gpio = 142,
	},
};

static struct gpio_led_platform_data snowball_led_data = {
	.leds = snowball_led_array,
	.num_leds = ARRAY_SIZE(snowball_led_array),
};

static struct platform_device snowball_led_dev = {
	.name = "leds-gpio",
	.dev = {
		.platform_data = &snowball_led_data,
	},
};

static struct ab8500_gpio_platform_data ab8500_gpio_pdata = {
	.gpio_base		= MOP500_AB8500_GPIO(0),
	.irq_base		= MOP500_AB8500_VIR_GPIO_IRQ_BASE,
	/* config_reg is the initial configuration of ab8500 pins.
	 * The pins can be configured as GPIO or alt functions based
	 * on value present in GpioSel1 to GpioSel6 and AlternatFunction
	 * register. This is the array of 7 configuration settings.
	 * One has to compile time decide these settings. Below is the
	 * explanation of these setting
	 * GpioSel1 = 0x00 => Pins GPIO1 to GPIO8 are not used as GPIO
	 * GpioSel2 = 0x1E => Pins GPIO10 to GPIO13 are configured as GPIO
	 * GpioSel3 = 0x80 => Pin GPIO24 is configured as GPIO
	 * GpioSel4 = 0x01 => Pin GPIo25 is configured as GPIO
	 * GpioSel5 = 0x7A => Pins GPIO34, GPIO36 to GPIO39 are conf as GPIO
	 * GpioSel6 = 0x00 => Pins GPIO41 & GPIo42 are not configured as GPIO
	 * AlternaFunction = 0x00 => If Pins GPIO10 to 13 are not configured
	 * as GPIO then this register selectes the alternate fucntions
	 */
	.config_reg		= {0x00, 0x1E, 0x80, 0x01,
					0x7A, 0x00, 0x00},
};

static struct gpio_keys_button snowball_key_array[] = {
	{
		.gpio           = 32,
		.type           = EV_KEY,
		.code           = KEY_1,
		.desc           = "userpb",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup         = 1,
	},
	{
		.gpio           = 151,
		.type           = EV_KEY,
		.code           = KEY_2,
		.desc           = "extkb1",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup         = 1,
	},
	{
		.gpio           = 152,
		.type           = EV_KEY,
		.code           = KEY_3,
		.desc           = "extkb2",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup         = 1,
	},
	{
		.gpio           = 161,
		.type           = EV_KEY,
		.code           = KEY_4,
		.desc           = "extkb3",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup         = 1,
	},
	{
		.gpio           = 162,
		.type           = EV_KEY,
		.code           = KEY_5,
		.desc           = "extkb4",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup         = 1,
	},
};

static struct gpio_keys_platform_data snowball_key_data = {
	.buttons        = snowball_key_array,
	.nbuttons       = ARRAY_SIZE(snowball_key_array),
};

static struct platform_device snowball_key_dev = {
	.name           = "gpio-keys",
	.id             = -1,
	.dev            = {
		.platform_data  = &snowball_key_data,
	}
};

static struct smsc911x_platform_config snowball_sbnet_cfg = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
	.irq_type = SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags = SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.shift = 1,
};

static struct resource sbnet_res[] = {
	{
		.name = "smsc911x-memory",
		.start = (0x5000 << 16),
		.end  =  (0x5000 << 16) + 0xffff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(140),
		.end = NOMADIK_GPIO_TO_IRQ(140),
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device snowball_sbnet_dev = {
	.name           = "smsc911x",
	.num_resources  = ARRAY_SIZE(sbnet_res),
	.resource       = sbnet_res,
	.dev            = {
		.platform_data = &snowball_sbnet_cfg,
	},
};

static struct ab8500_platform_data ab8500_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
	.regulator_reg_init = ab8500_regulator_reg_init,
	.num_regulator_reg_init	= ARRAY_SIZE(ab8500_regulator_reg_init),
	.regulator	= ab8500_regulators,
	.num_regulator	= ARRAY_SIZE(ab8500_regulators),
	.gpio		= &ab8500_gpio_pdata,
};

static struct resource ab8500_resources[] = {
	[0] = {
		.start	= IRQ_DB8500_AB8500,
		.end	= IRQ_DB8500_AB8500,
		.flags	= IORESOURCE_IRQ
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

/*
 * TPS61052
 */

static struct tps6105x_platform_data mop500_tps61052_data = {
	.mode = TPS6105X_MODE_VOLTAGE,
	.regulator_data = &tps61052_regulator,
};

/*
 * TC35892
 */

static void mop500_tc35892_init(struct tc3589x *tc3589x, unsigned int base)
{
	mop500_sdi_tc35892_init();
}

static struct tc3589x_gpio_platform_data mop500_tc35892_gpio_data = {
	.gpio_base	= MOP500_EGPIO(0),
	.setup		= mop500_tc35892_init,
};

static struct tc3589x_platform_data mop500_tc35892_data = {
	.block		= TC3589x_BLOCK_GPIO,
	.gpio		= &mop500_tc35892_gpio_data,
	.irq_base	= MOP500_EGPIO_IRQ_BASE,
};

static struct lp5521_led_config lp5521_pri_led[] = {
       [0] = {
	       .chan_nr = 0,
	       .led_current = 0x2f,
	       .max_current = 0x5f,
       },
       [1] = {
	       .chan_nr = 1,
	       .led_current = 0x2f,
	       .max_current = 0x5f,
       },
       [2] = {
	       .chan_nr = 2,
	       .led_current = 0x2f,
	       .max_current = 0x5f,
       },
};

static struct lp5521_platform_data __initdata lp5521_pri_data = {
       .label = "lp5521_pri",
       .led_config     = &lp5521_pri_led[0],
       .num_channels   = 3,
       .clock_mode     = LP5521_CLOCK_EXT,
};

static struct lp5521_led_config lp5521_sec_led[] = {
       [0] = {
	       .chan_nr = 0,
	       .led_current = 0x2f,
	       .max_current = 0x5f,
       },
       [1] = {
	       .chan_nr = 1,
	       .led_current = 0x2f,
	       .max_current = 0x5f,
       },
       [2] = {
	       .chan_nr = 2,
	       .led_current = 0x2f,
	       .max_current = 0x5f,
       },
};

static struct lp5521_platform_data __initdata lp5521_sec_data = {
       .label = "lp5521_sec",
       .led_config     = &lp5521_sec_led[0],
       .num_channels   = 3,
       .clock_mode     = LP5521_CLOCK_EXT,
};

static struct i2c_board_info __initdata mop500_i2c0_devices[] = {
	{
		I2C_BOARD_INFO("tc3589x", 0x42),
		.irq		= NOMADIK_GPIO_TO_IRQ(217),
		.platform_data  = &mop500_tc35892_data,
	},
	/* I2C0 devices only available prior to HREFv60 */
	{
		I2C_BOARD_INFO("tps61052", 0x33),
		.platform_data  = &mop500_tps61052_data,
	},
};

#define NUM_PRE_V60_I2C0_DEVICES 1

static struct i2c_board_info __initdata mop500_i2c2_devices[] = {
	{
		/* lp5521 LED driver, 1st device */
		I2C_BOARD_INFO("lp5521", 0x33),
		.platform_data = &lp5521_pri_data,
	},
	{
		/* lp5521 LED driver, 2st device */
		I2C_BOARD_INFO("lp5521", 0x34),
		.platform_data = &lp5521_sec_data,
	},
	{
		/* Light sensor Rohm BH1780GLI */
		I2C_BOARD_INFO("bh1780", 0x29),
	},
};

#define U8500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, t_out, _sm)	\
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
	/* Slave response timeout(ms) */\
	.timeout	= t_out,	\
	.sm		= _sm,		\
}

/*
 * The board uses 4 i2c controllers, initialize all of
 * them with slave data setup time of 250 ns,
 * Tx & Rx FIFO threshold values as 8 and standard
 * mode of operation
 */
U8500_I2C_CONTROLLER(0, 0xe, 1, 8, 100000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(1, 0xe, 1, 8, 100000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(2,	0xe, 1, 8, 100000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(3,	0xe, 1, 8, 100000, 200, I2C_FREQ_MODE_FAST);

static void __init mop500_i2c_init(void)
{
	db8500_add_i2c0(&u8500_i2c0_data);
	db8500_add_i2c1(&u8500_i2c1_data);
	db8500_add_i2c2(&u8500_i2c2_data);
	db8500_add_i2c3(&u8500_i2c3_data);
}

static struct gpio_keys_button mop500_gpio_keys[] = {
	{
		.desc			= "SFH7741 Proximity Sensor",
		.type			= EV_SW,
		.code			= SW_FRONT_PROXIMITY,
		.active_low		= 0,
		.can_disable		= 1,
	}
};

static struct regulator *prox_regulator;
static int mop500_prox_activate(struct device *dev);
static void mop500_prox_deactivate(struct device *dev);

static struct gpio_keys_platform_data mop500_gpio_keys_data = {
	.buttons	= mop500_gpio_keys,
	.nbuttons	= ARRAY_SIZE(mop500_gpio_keys),
	.enable		= mop500_prox_activate,
	.disable	= mop500_prox_deactivate,
};

static struct platform_device mop500_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &mop500_gpio_keys_data,
	},
};

static int mop500_prox_activate(struct device *dev)
{
	prox_regulator = regulator_get(&mop500_gpio_keys_device.dev,
						"vcc");
	if (IS_ERR(prox_regulator)) {
		dev_err(&mop500_gpio_keys_device.dev,
			"no regulator\n");
		return PTR_ERR(prox_regulator);
	}
	regulator_enable(prox_regulator);
	return 0;
}

static void mop500_prox_deactivate(struct device *dev)
{
	regulator_disable(prox_regulator);
	regulator_put(prox_regulator);
}

/* add any platform devices here - TODO */
static struct platform_device *mop500_platform_devs[] __initdata = {
	&mop500_gpio_keys_device,
	&ab8500_device,
};

#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg ssp0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV8_SSP0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg ssp0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV8_SSP0_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};
#endif

static struct pl022_ssp_controller ssp0_platform_data = {
	.bus_id = 0,
#ifdef CONFIG_STE_DMA40
	.enable_dma = 1,
	.dma_filter = stedma40_filter,
	.dma_rx_param = &ssp0_dma_cfg_rx,
	.dma_tx_param = &ssp0_dma_cfg_tx,
#else
	.enable_dma = 0,
#endif
	/* on this platform, gpio 31,142,144,214 &
	 * 224 are connected as chip selects
	 */
	.num_chipselect = 5,
};

static void __init mop500_spi_init(void)
{
	db8500_add_ssp0(&ssp0_platform_data);
}

#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg uart0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV13_UART0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV13_UART0_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart1_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV12_UART1_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart1_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV12_UART1_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart2_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV11_UART2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart2_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV11_UART2_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};
#endif


static pin_cfg_t mop500_pins_uart0[] = {
	GPIO0_U0_CTSn   | PIN_INPUT_PULLUP,
	GPIO1_U0_RTSn   | PIN_OUTPUT_HIGH,
	GPIO2_U0_RXD    | PIN_INPUT_PULLUP,
	GPIO3_U0_TXD    | PIN_OUTPUT_HIGH,
};

#define PRCC_K_SOFTRST_SET      0x18
#define PRCC_K_SOFTRST_CLEAR    0x1C
static void ux500_uart0_reset(void)
{
	void __iomem *prcc_rst_set, *prcc_rst_clr;

	prcc_rst_set = (void __iomem *)IO_ADDRESS(U8500_CLKRST1_BASE +
			PRCC_K_SOFTRST_SET);
	prcc_rst_clr = (void __iomem *)IO_ADDRESS(U8500_CLKRST1_BASE +
			PRCC_K_SOFTRST_CLEAR);

	/* Activate soft reset PRCC_K_SOFTRST_CLEAR */
	writel((readl(prcc_rst_clr) | 0x1), prcc_rst_clr);
	udelay(1);

	/* Release soft reset PRCC_K_SOFTRST_SET */
	writel((readl(prcc_rst_set) | 0x1), prcc_rst_set);
	udelay(1);
}

static void ux500_uart0_init(void)
{
	int ret;

	ret = nmk_config_pins(mop500_pins_uart0,
			ARRAY_SIZE(mop500_pins_uart0));
	if (ret < 0)
		pr_err("pl011: uart pins_enable failed\n");
}

static void ux500_uart0_exit(void)
{
	int ret;

	ret = nmk_config_pins_sleep(mop500_pins_uart0,
			ARRAY_SIZE(mop500_pins_uart0));
	if (ret < 0)
		pr_err("pl011: uart pins_disable failed\n");
}

static struct amba_pl011_data uart0_plat = {
#ifdef CONFIG_STE_DMA40
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart0_dma_cfg_rx,
	.dma_tx_param = &uart0_dma_cfg_tx,
#endif
	.init = ux500_uart0_init,
	.exit = ux500_uart0_exit,
	.reset = ux500_uart0_reset,
};

static struct amba_pl011_data uart1_plat = {
#ifdef CONFIG_STE_DMA40
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart1_dma_cfg_rx,
	.dma_tx_param = &uart1_dma_cfg_tx,
#endif
};

static struct amba_pl011_data uart2_plat = {
#ifdef CONFIG_STE_DMA40
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart2_dma_cfg_rx,
	.dma_tx_param = &uart2_dma_cfg_tx,
#endif
};

static void __init mop500_uart_init(void)
{
	db8500_add_uart0(&uart0_plat);
	db8500_add_uart1(&uart1_plat);
	db8500_add_uart2(&uart2_plat);
}

static struct platform_device *snowball_platform_devs[] __initdata = {
	&snowball_led_dev,
	&snowball_key_dev,
	&snowball_sbnet_dev,
	&ab8500_device,
};

static void __init mop500_init_machine(void)
{
	int i2c0_devs;

	/*
	 * The HREFv60 board removed a GPIO expander and routed
	 * all these GPIO pins to the internal GPIO controller
	 * instead.
	 */
	if (!machine_is_snowball()) {
		if (machine_is_hrefv60())
			mop500_gpio_keys[0].gpio = HREFV60_PROX_SENSE_GPIO;
		else
			mop500_gpio_keys[0].gpio = GPIO_PROX_SENSOR;
	}

	u8500_init_devices();

	mop500_pins_init();

	if (machine_is_snowball())
		platform_add_devices(snowball_platform_devs,
					ARRAY_SIZE(snowball_platform_devs));
	else
		platform_add_devices(mop500_platform_devs,
					ARRAY_SIZE(mop500_platform_devs));

	mop500_i2c_init();
	mop500_sdi_init();
	mop500_spi_init();
	mop500_uart_init();

	i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);
	if (machine_is_hrefv60())
		i2c0_devs -= NUM_PRE_V60_I2C0_DEVICES;

	i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
	i2c_register_board_info(2, mop500_i2c2_devices,
				ARRAY_SIZE(mop500_i2c2_devices));

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
MACHINE_END

MACHINE_START(HREFV60, "ST-Ericsson U8500 Platform HREFv60+")
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
MACHINE_END

MACHINE_START(SNOWBALL, "Calao Systems Snowball platform")
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
MACHINE_END
