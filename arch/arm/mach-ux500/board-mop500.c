/*
 * Copyright (C) 2008-2012 ST-Ericsson
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
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-nomadik.h>
#include <linux/platform_data/db8500_thermal.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/amba/serial.h>
#include <linux/spi/spi.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/regulator/ab8500.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/mfd/tc3589x.h>
#include <linux/mfd/tps6105x.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/mfd/abx500/ab8500-codec.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/input.h>
#include <linux/smsc911x.h>
#include <linux/gpio_keys.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_data/pinctrl-nomadik.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "setup.h"
#include "devices.h"
#include "irqs.h"
#include <linux/platform_data/crypto-ux500.h>

#include "ste-dma40-db8500.h"
#include "db8500-regs.h"
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

static struct fixed_voltage_config snowball_gpio_en_3v3_data = {
       .supply_name            = "EN-3V3",
       .gpio                   = SNOWBALL_EN_3V3_ETH_GPIO,
       .microvolts             = 3300000,
       .enable_high            = 1,
       .init_data              = &gpio_en_3v3_regulator,
       .startup_delay          = 5000, /* 1200us */
};

static struct platform_device snowball_gpio_en_3v3_regulator_dev = {
       .name   = "reg-fixed-voltage",
       .id     = 1,
       .dev    = {
               .platform_data  = &snowball_gpio_en_3v3_data,
       },
};

/* Dynamically populated. */
static struct gpio sdi0_reg_gpios[] = {
	{ 0, GPIOF_OUT_INIT_LOW, "mmci_vsel" },
};

static struct gpio_regulator_state sdi0_reg_states[] = {
	{ .value = 2900000, .gpios = (0 << 0) },
	{ .value = 1800000, .gpios = (1 << 0) },
};

static struct gpio_regulator_config sdi0_reg_info = {
	.supply_name		= "ext-mmc-level-shifter",
	.gpios			= sdi0_reg_gpios,
	.nr_gpios		= ARRAY_SIZE(sdi0_reg_gpios),
	.states			= sdi0_reg_states,
	.nr_states		= ARRAY_SIZE(sdi0_reg_states),
	.type			= REGULATOR_VOLTAGE,
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &sdi0_reg_init_data,
	.startup_delay		= 100,
};

static struct platform_device sdi0_regulator = {
	.name = "gpio-regulator",
	.id   = -1,
	.dev  = {
		.platform_data = &sdi0_reg_info,
	},
};

static struct abx500_gpio_platform_data ab8500_gpio_pdata = {
	.gpio_base		= MOP500_AB8500_PIN_GPIO(1),
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

struct ab8500_platform_data ab8500_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
	.regulator	= &ab8500_regulator_plat_data,
	.gpio		= &ab8500_gpio_pdata,
	.codec		= &ab8500_codec_pdata,
};

static struct platform_device u8500_cpufreq_cooling_device = {
	.name           = "db8500-cpufreq-cooling",
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

static struct lp55xx_led_config lp5521_pri_led[] = {
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

static struct lp55xx_platform_data __initdata lp5521_pri_data = {
       .label = "lp5521_pri",
       .led_config     = &lp5521_pri_led[0],
       .num_channels   = 3,
       .clock_mode     = LP55XX_CLOCK_EXT,
};

static struct lp55xx_led_config lp5521_sec_led[] = {
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

static struct lp55xx_platform_data __initdata lp5521_sec_data = {
       .label = "lp5521_sec",
       .led_config     = &lp5521_sec_led[0],
       .num_channels   = 3,
       .clock_mode     = LP55XX_CLOCK_EXT,
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

	return regulator_enable(prox_regulator);
}

static void mop500_prox_deactivate(struct device *dev)
{
	regulator_disable(prox_regulator);
	regulator_put(prox_regulator);
}

void mop500_snowball_ethernet_clock_enable(void)
{
	struct clk *clk;

	clk = clk_get_sys("fsmc", NULL);
	if (!IS_ERR(clk))
		clk_prepare_enable(clk);
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
	&sdi0_regulator,
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

struct pl022_ssp_controller ssp0_plat = {
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

struct amba_pl011_data uart0_plat = {
#ifdef CONFIG_STE_DMA40
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart0_dma_cfg_rx,
	.dma_tx_param = &uart0_dma_cfg_tx,
#endif
};

struct amba_pl011_data uart1_plat = {
#ifdef CONFIG_STE_DMA40
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart1_dma_cfg_rx,
	.dma_tx_param = &uart1_dma_cfg_tx,
#endif
};

struct amba_pl011_data uart2_plat = {
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
	&snowball_gpio_en_3v3_regulator_dev,
	&u8500_cpufreq_cooling_device,
	&sdi0_regulator,
};

static void __init mop500_init_machine(void)
{
	struct device *parent = NULL;
	int i2c0_devs;
	int i;

	platform_device_register(&db8500_prcmu_device);
	mop500_gpio_keys[0].gpio = GPIO_PROX_SENSOR;

	sdi0_reg_info.enable_gpio = GPIO_SDMMC_EN;
	sdi0_reg_info.gpios[0].gpio = GPIO_SDMMC_1V8_3V_SEL;

	mop500_pinmaps_init();
	parent = u8500_init_devices(&ab8500_platdata);

	for (i = 0; i < ARRAY_SIZE(mop500_platform_devs); i++)
		mop500_platform_devs[i]->dev.parent = parent;

	platform_add_devices(mop500_platform_devs,
			ARRAY_SIZE(mop500_platform_devs));

	mop500_i2c_init(parent);
	mop500_sdi_init(parent);
	mop500_spi_init(parent);
	mop500_audio_init(parent);
	mop500_uart_init(parent);

	u8500_cryp1_hash1_init(parent);

	i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);

	i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
	i2c_register_board_info(2, mop500_i2c2_devices,
				ARRAY_SIZE(mop500_i2c2_devices));

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

static void __init snowball_init_machine(void)
{
	struct device *parent = NULL;
	int i;

	platform_device_register(&db8500_prcmu_device);

	sdi0_reg_info.enable_gpio = SNOWBALL_SDMMC_EN_GPIO;
	sdi0_reg_info.gpios[0].gpio = SNOWBALL_SDMMC_1V8_3V_GPIO;

	snowball_pinmaps_init();
	parent = u8500_init_devices(&ab8500_platdata);

	for (i = 0; i < ARRAY_SIZE(snowball_platform_devs); i++)
		snowball_platform_devs[i]->dev.parent = parent;

	platform_add_devices(snowball_platform_devs,
			ARRAY_SIZE(snowball_platform_devs));

	mop500_i2c_init(parent);
	snowball_sdi_init(parent);
	mop500_spi_init(parent);
	mop500_audio_init(parent);
	mop500_uart_init(parent);

	mop500_snowball_ethernet_clock_enable();

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

static void __init hrefv60_init_machine(void)
{
	struct device *parent = NULL;
	int i2c0_devs;
	int i;

	platform_device_register(&db8500_prcmu_device);
	/*
	 * The HREFv60 board removed a GPIO expander and routed
	 * all these GPIO pins to the internal GPIO controller
	 * instead.
	 */
	mop500_gpio_keys[0].gpio = HREFV60_PROX_SENSE_GPIO;

	sdi0_reg_info.enable_gpio = HREFV60_SDMMC_EN_GPIO;
	sdi0_reg_info.gpios[0].gpio = HREFV60_SDMMC_1V8_3V_GPIO;

	hrefv60_pinmaps_init();
	parent = u8500_init_devices(&ab8500_platdata);

	for (i = 0; i < ARRAY_SIZE(mop500_platform_devs); i++)
		mop500_platform_devs[i]->dev.parent = parent;

	platform_add_devices(mop500_platform_devs,
			ARRAY_SIZE(mop500_platform_devs));

	mop500_i2c_init(parent);
	hrefv60_sdi_init(parent);
	mop500_spi_init(parent);
	mop500_audio_init(parent);
	mop500_uart_init(parent);

	i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);

	i2c0_devs -= NUM_PRE_V60_I2C0_DEVICES;

	i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
	i2c_register_board_info(2, mop500_i2c2_devices,
				ARRAY_SIZE(mop500_i2c2_devices));

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.atag_offset	= 0x100,
	.smp		= smp_ops(ux500_smp_ops),
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.init_time	= ux500_timer_init,
	.init_machine	= mop500_init_machine,
	.init_late	= ux500_init_late,
MACHINE_END

MACHINE_START(U8520, "ST-Ericsson U8520 Platform HREFP520")
	.atag_offset	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.init_time	= ux500_timer_init,
	.init_machine	= mop500_init_machine,
	.init_late	= ux500_init_late,
MACHINE_END

MACHINE_START(HREFV60, "ST-Ericsson U8500 Platform HREFv60+")
	.atag_offset	= 0x100,
	.smp		= smp_ops(ux500_smp_ops),
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.init_time	= ux500_timer_init,
	.init_machine	= hrefv60_init_machine,
	.init_late	= ux500_init_late,
MACHINE_END

MACHINE_START(SNOWBALL, "Calao Systems Snowball platform")
	.atag_offset	= 0x100,
	.smp		= smp_ops(ux500_smp_ops),
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.init_time	= ux500_timer_init,
	.init_machine	= snowball_init_machine,
	.init_late	= NULL,
MACHINE_END
