
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
#include <linux/platform_data/i2c-nomadik.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/amba/serial.h>
#include <linux/spi/spi.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/regulator/ab8500.h>
#include <linux/mfd/tc3589x.h>
#include <linux/mfd/tps6105x.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/mfd/abx500/ab8500-codec.h>
#include <linux/leds-lp5521.h>
#include <linux/input.h>
#include <linux/smsc911x.h>
#include <linux/gpio_keys.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

#include <plat/ste_dma40.h>
#include <plat/gpio-nomadik.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/irqs.h>
#include <mach/crypto-ux500.h>

#include "ste-dma40-db8500.h"
#include "devices-db8500.h"
#include "board-mop500.h"
#include "board-mop500-regulators.h"

static struct gpio_led snowball_led_array[] = {
	{
		.name = "user_led",
		.default_trigger = "heartbeat",
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
	.gpio_base		= MOP500_AB8500_PIN_GPIO(1),
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

/* ab8500-codec */
static struct ab8500_codec_platform_data ab8500_codec_pdata = {
	.amics =  {
		.mic1_type = AMIC_TYPE_DIFFERENTIAL,
		.mic2_type = AMIC_TYPE_DIFFERENTIAL,
		.mic1a_micbias = AMIC_MICBIAS_VAMIC1,
		.mic1b_micbias = AMIC_MICBIAS_VAMIC1,
		.mic2_micbias = AMIC_MICBIAS_VAMIC2
	},
	.ear_cmv = EAR_CMV_0_95V
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
	.codec		= &ab8500_codec_pdata,
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
	struct device *parent = NULL;
#if 0
	/* FIXME: Is the sdi actually part of tc3589x? */
	parent = tc3589x->dev;
#endif
	mop500_sdi_tc35892_init(parent);
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

static void __init mop500_i2c_init(struct device *parent)
{
	db8500_add_i2c0(parent, NULL);
	db8500_add_i2c1(parent, NULL);
	db8500_add_i2c2(parent, NULL);
	db8500_add_i2c3(parent, NULL);
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

static struct cryp_platform_data u8500_cryp1_platform_data = {
		.mem_to_engine = {
				.dir = STEDMA40_MEM_TO_PERIPH,
				.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
				.dst_dev_type = DB8500_DMA_DEV48_CAC1_TX,
				.src_info.data_width = STEDMA40_WORD_WIDTH,
				.dst_info.data_width = STEDMA40_WORD_WIDTH,
				.mode = STEDMA40_MODE_LOGICAL,
				.src_info.psize = STEDMA40_PSIZE_LOG_4,
				.dst_info.psize = STEDMA40_PSIZE_LOG_4,
		},
		.engine_to_mem = {
				.dir = STEDMA40_PERIPH_TO_MEM,
				.src_dev_type = DB8500_DMA_DEV48_CAC1_RX,
				.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
				.src_info.data_width = STEDMA40_WORD_WIDTH,
				.dst_info.data_width = STEDMA40_WORD_WIDTH,
				.mode = STEDMA40_MODE_LOGICAL,
				.src_info.psize = STEDMA40_PSIZE_LOG_4,
				.dst_info.psize = STEDMA40_PSIZE_LOG_4,
		}
};

static struct stedma40_chan_cfg u8500_hash_dma_cfg_tx = {
		.dir = STEDMA40_MEM_TO_PERIPH,
		.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
		.dst_dev_type = DB8500_DMA_DEV50_HAC1_TX,
		.src_info.data_width = STEDMA40_WORD_WIDTH,
		.dst_info.data_width = STEDMA40_WORD_WIDTH,
		.mode = STEDMA40_MODE_LOGICAL,
		.src_info.psize = STEDMA40_PSIZE_LOG_16,
		.dst_info.psize = STEDMA40_PSIZE_LOG_16,
};

static struct hash_platform_data u8500_hash1_platform_data = {
		.mem_to_engine = &u8500_hash_dma_cfg_tx,
		.dma_filter = stedma40_filter,
};

/* add any platform devices here - TODO */
static struct platform_device *mop500_platform_devs[] __initdata = {
	&mop500_gpio_keys_device,
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

static struct pl022_ssp_controller ssp0_plat = {
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

static void __init mop500_spi_init(struct device *parent)
{
	db8500_add_ssp0(parent, &ssp0_plat);
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

static struct amba_pl011_data uart0_plat = {
#ifdef CONFIG_STE_DMA40
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart0_dma_cfg_rx,
	.dma_tx_param = &uart0_dma_cfg_tx,
#endif
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

static void __init mop500_uart_init(struct device *parent)
{
	db8500_add_uart0(parent, &uart0_plat);
	db8500_add_uart1(parent, &uart1_plat);
	db8500_add_uart2(parent, &uart2_plat);
}

static void __init u8500_cryp1_hash1_init(struct device *parent)
{
	db8500_add_cryp1(parent, &u8500_cryp1_platform_data);
	db8500_add_hash1(parent, &u8500_hash1_platform_data);
}

static struct platform_device *snowball_platform_devs[] __initdata = {
	&snowball_led_dev,
	&snowball_key_dev,
	&snowball_sbnet_dev,
};

static void __init mop500_init_machine(void)
{
	struct device *parent = NULL;
	int i2c0_devs;
	int i;

	mop500_gpio_keys[0].gpio = GPIO_PROX_SENSOR;

	mop500_pinmaps_init();
	parent = u8500_init_devices(&ab8500_platdata);

	for (i = 0; i < ARRAY_SIZE(mop500_platform_devs); i++)
		mop500_platform_devs[i]->dev.parent = parent;

	platform_add_devices(mop500_platform_devs,
			ARRAY_SIZE(mop500_platform_devs));

	mop500_i2c_init(parent);
	mop500_sdi_init(parent);
	mop500_spi_init(parent);
	mop500_msp_init(parent);
	mop500_uart_init(parent);

	u8500_cryp1_hash1_init(parent);

	i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);

	i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
	i2c_register_board_info(2, mop500_i2c2_devices,
				ARRAY_SIZE(mop500_i2c2_devices));

	/* This board has full regulator constraints */
	regulator_has_full_constraints();

	mop500_uib_init();
}

static void __init snowball_init_machine(void)
{
	struct device *parent = NULL;
	int i;

	snowball_pinmaps_init();
	parent = u8500_init_devices(&ab8500_platdata);

	for (i = 0; i < ARRAY_SIZE(snowball_platform_devs); i++)
		snowball_platform_devs[i]->dev.parent = parent;

	platform_add_devices(snowball_platform_devs,
			ARRAY_SIZE(snowball_platform_devs));

	mop500_i2c_init(parent);
	snowball_sdi_init(parent);
	mop500_spi_init(parent);
	mop500_msp_init(parent);
	mop500_uart_init(parent);

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

static void __init hrefv60_init_machine(void)
{
	struct device *parent = NULL;
	int i2c0_devs;
	int i;

	/*
	 * The HREFv60 board removed a GPIO expander and routed
	 * all these GPIO pins to the internal GPIO controller
	 * instead.
	 */
	mop500_gpio_keys[0].gpio = HREFV60_PROX_SENSE_GPIO;

	hrefv60_pinmaps_init();
	parent = u8500_init_devices(&ab8500_platdata);

	for (i = 0; i < ARRAY_SIZE(mop500_platform_devs); i++)
		mop500_platform_devs[i]->dev.parent = parent;

	platform_add_devices(mop500_platform_devs,
			ARRAY_SIZE(mop500_platform_devs));

	mop500_i2c_init(parent);
	hrefv60_sdi_init(parent);
	mop500_spi_init(parent);
	mop500_msp_init(parent);
	mop500_uart_init(parent);

	i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);

	i2c0_devs -= NUM_PRE_V60_I2C0_DEVICES;

	i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
	i2c_register_board_info(2, mop500_i2c2_devices,
				ARRAY_SIZE(mop500_i2c2_devices));

	/* This board has full regulator constraints */
	regulator_has_full_constraints();

	mop500_uib_init();
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.atag_offset	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= mop500_init_machine,
	.init_late	= ux500_init_late,
MACHINE_END

MACHINE_START(HREFV60, "ST-Ericsson U8500 Platform HREFv60+")
	.atag_offset	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= hrefv60_init_machine,
	.init_late	= ux500_init_late,
MACHINE_END

MACHINE_START(SNOWBALL, "Calao Systems Snowball platform")
	.atag_offset	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= snowball_init_machine,
	.init_late	= ux500_init_late,
MACHINE_END

#ifdef CONFIG_MACH_UX500_DT

struct of_dev_auxdata u8500_auxdata_lookup[] __initdata = {
	/* Requires call-back bindings. */
	OF_DEV_AUXDATA("arm,cortex-a9-pmu", 0, "arm-pmu", &db8500_pmu_platdata),
	/* Requires DMA and call-back bindings. */
	OF_DEV_AUXDATA("arm,pl011", 0x80120000, "uart0", &uart0_plat),
	OF_DEV_AUXDATA("arm,pl011", 0x80121000, "uart1", &uart1_plat),
	OF_DEV_AUXDATA("arm,pl011", 0x80007000, "uart2", &uart2_plat),
	/* Requires DMA bindings. */
	OF_DEV_AUXDATA("arm,pl022", 0x80002000, "ssp0",  &ssp0_plat),
	OF_DEV_AUXDATA("arm,pl18x", 0x80126000, "sdi0",  &mop500_sdi0_data),
	OF_DEV_AUXDATA("arm,pl18x", 0x80118000, "sdi1",  &mop500_sdi1_data),
	OF_DEV_AUXDATA("arm,pl18x", 0x80005000, "sdi2",  &mop500_sdi2_data),
	OF_DEV_AUXDATA("arm,pl18x", 0x80114000, "sdi4",  &mop500_sdi4_data),
	/* Requires clock name bindings. */
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8012e000, "gpio.0", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8012e080, "gpio.1", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8000e000, "gpio.2", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8000e080, "gpio.3", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8000e100, "gpio.4", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8000e180, "gpio.5", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8011e000, "gpio.6", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0x8011e080, "gpio.7", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", 0xa03fe000, "gpio.8", NULL),
	OF_DEV_AUXDATA("st,nomadik-i2c", 0x80004000, "nmk-i2c.0", NULL),
	OF_DEV_AUXDATA("st,nomadik-i2c", 0x80122000, "nmk-i2c.1", NULL),
	OF_DEV_AUXDATA("st,nomadik-i2c", 0x80128000, "nmk-i2c.2", NULL),
	OF_DEV_AUXDATA("st,nomadik-i2c", 0x80110000, "nmk-i2c.3", NULL),
	OF_DEV_AUXDATA("st,nomadik-i2c", 0x8012a000, "nmk-i2c.4", NULL),
	/* Requires device name bindings. */
	OF_DEV_AUXDATA("stericsson,nmk_pinctrl", 0, "pinctrl-db8500", NULL),
	/* Requires clock name and DMA bindings. */
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80123000,
		"ux500-msp-i2s.0", &msp0_platform_data),
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80124000,
		"ux500-msp-i2s.1", &msp1_platform_data),
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80117000,
		"ux500-msp-i2s.2", &msp2_platform_data),
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80125000,
		"ux500-msp-i2s.3", &msp3_platform_data),
	{},
};

static const struct of_device_id u8500_local_bus_nodes[] = {
	/* only create devices below soc node */
	{ .compatible = "stericsson,db8500", },
	{ .compatible = "stericsson,db8500-prcmu", },
	{ .compatible = "simple-bus"},
	{ },
};

static void __init u8500_init_machine(void)
{
	struct device *parent = NULL;
	int i2c0_devs;
	int i;

	/* Pinmaps must be in place before devices register */
	if (of_machine_is_compatible("st-ericsson,mop500"))
		mop500_pinmaps_init();
	else if (of_machine_is_compatible("calaosystems,snowball-a9500"))
		snowball_pinmaps_init();
	else if (of_machine_is_compatible("st-ericsson,hrefv60+"))
		hrefv60_pinmaps_init();

	parent = u8500_of_init_devices();

	for (i = 0; i < ARRAY_SIZE(mop500_platform_devs); i++)
		mop500_platform_devs[i]->dev.parent = parent;

	/* automatically probe child nodes of db8500 device */
	of_platform_populate(NULL, u8500_local_bus_nodes, u8500_auxdata_lookup, parent);

	if (of_machine_is_compatible("st-ericsson,mop500")) {
		mop500_gpio_keys[0].gpio = GPIO_PROX_SENSOR;

		platform_add_devices(mop500_platform_devs,
				ARRAY_SIZE(mop500_platform_devs));

		mop500_sdi_init(parent);
		mop500_msp_init(parent);
		i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);
		i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
		i2c_register_board_info(2, mop500_i2c2_devices,
					ARRAY_SIZE(mop500_i2c2_devices));

		mop500_uib_init();

	} else if (of_machine_is_compatible("calaosystems,snowball-a9500")) {
		mop500_of_msp_init(parent);
	} else if (of_machine_is_compatible("st-ericsson,hrefv60+")) {
		/*
		 * The HREFv60 board removed a GPIO expander and routed
		 * all these GPIO pins to the internal GPIO controller
		 * instead.
		 */
		mop500_gpio_keys[0].gpio = HREFV60_PROX_SENSE_GPIO;
		platform_add_devices(mop500_platform_devs,
				ARRAY_SIZE(mop500_platform_devs));

		hrefv60_sdi_init(parent);
		mop500_msp_init(parent);

		i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);
		i2c0_devs -= NUM_PRE_V60_I2C0_DEVICES;

		i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
		i2c_register_board_info(2, mop500_i2c2_devices,
					ARRAY_SIZE(mop500_i2c2_devices));

		mop500_uib_init();
	}

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

static const char * u8500_dt_board_compat[] = {
	"calaosystems,snowball-a9500",
	"st-ericsson,hrefv60+",
	"st-ericsson,u8500",
	"st-ericsson,mop500",
	NULL,
};


DT_MACHINE_START(U8500_DT, "ST-Ericsson U8500 platform (Device Tree Support)")
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= u8500_init_machine,
	.init_late	= ux500_init_late,
	.dt_compat      = u8500_dt_board_compat,
MACHINE_END
#endif
