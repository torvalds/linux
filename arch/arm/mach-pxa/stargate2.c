/*
 *  linux/arch/arm/mach-pxa/stargate2.c
 *
 *  Author:	Ed C. Epp
 *  Created:	Nov 05, 2002
 *  Copyright:	Intel Corp.
 *
 *  Modified 2009:  Jonathan Cameron <jic23@cam.ac.uk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/partitions.h>

#include <linux/i2c/pxa-i2c.h>
#include <linux/i2c/pcf857x.h>
#include <linux/i2c/at24.h>
#include <linux/smc91x.h>
#include <linux/gpio.h>
#include <linux/leds.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/pxa27x-udc.h>
#include <mach/smemc.h>

#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/mfd/da903x.h>
#include <linux/sht15.h>

#include "devices.h"
#include "generic.h"

#define STARGATE_NR_IRQS	(IRQ_BOARD_START + 8)

/* Bluetooth */
#define SG2_BT_RESET		81

/* SD */
#define SG2_GPIO_nSD_DETECT	90
#define SG2_SD_POWER_ENABLE	89

static unsigned long sg2_im2_unified_pin_config[] __initdata = {
	/* Device Identification for wakeup*/
	GPIO102_GPIO,
	/* DA9030 */
	GPIO1_GPIO,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,

	/* 802.15.4 radio - driver out of mainline */
	GPIO22_GPIO,			/* CC_RSTN */
	GPIO114_GPIO,			/* CC_FIFO */
	GPIO116_GPIO,			/* CC_CCA */
	GPIO0_GPIO,			/* CC_FIFOP */
	GPIO16_GPIO,			/* CCSFD */
	GPIO115_GPIO,			/* Power enable */

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* SSP 3 - 802.15.4 radio */
	GPIO39_GPIO,			/* Chip Select */
	GPIO34_SSP3_SCLK,
	GPIO35_SSP3_TXD,
	GPIO41_SSP3_RXD,

	/* SSP 2 to daughter boards */
	GPIO11_SSP2_RXD,
	GPIO38_SSP2_TXD,
	GPIO36_SSP2_SCLK,
	GPIO37_GPIO, /* chip select */

	/* SSP 1 - to daughter boards */
	GPIO24_GPIO,			/* Chip Select */
	GPIO23_SSP1_SCLK,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* BTUART Basic Connector*/
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* STUART  - IM2 via debug board not sure on SG2*/
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* Basic sensor board */
	GPIO96_GPIO,	/* accelerometer interrupt */
	GPIO99_GPIO,	/* ADC interrupt */

	/* SHT15 */
	GPIO100_GPIO,
	GPIO98_GPIO,

	/* Basic sensor board */
	GPIO96_GPIO,	/* accelerometer interrupt */
	GPIO99_GPIO,	/* ADC interrupt */

	/* Connector pins specified as gpios */
	GPIO94_GPIO, /* large basic connector pin 14 */
	GPIO10_GPIO, /* large basic connector pin 23 */
};

static struct sht15_platform_data platform_data_sht15 = {
	.gpio_data =  100,
	.gpio_sck  =  98,
};

static struct platform_device sht15 = {
	.name = "sht15",
	.id = -1,
	.dev = {
		.platform_data = &platform_data_sht15,
	},
};

static struct regulator_consumer_supply stargate2_sensor_3_con[] = {
	{
		.dev_name = "sht15",
		.supply = "vcc",
	},
};

enum stargate2_ldos{
	vcc_vref,
	vcc_cc2420,
	/* a mote connector? */
	vcc_mica,
	/* the CSR bluecore chip */
	vcc_bt,
	/* The two voltages available to sensor boards */
	vcc_sensor_1_8,
	vcc_sensor_3,
	/* directly connected to the pxa27x */
	vcc_sram_ext,
	vcc_pxa_pll,
	vcc_pxa_usim, /* Reference voltage for certain gpios */
	vcc_pxa_mem,
	vcc_pxa_flash,
	vcc_pxa_core, /*Dc-Dc buck not yet supported */
	vcc_lcd,
	vcc_bb,
	vcc_bbio, /*not sure!*/
	vcc_io, /* cc2420 802.15.4 radio and pxa vcc_io ?*/
};

/* The values of the various regulator constraints are obviously dependent
 * on exactly what is wired to each ldo.  Unfortunately this information is
 * not generally available.  More information has been requested from Xbow.
 */
static struct regulator_init_data stargate2_ldo_init_data[] = {
	[vcc_bbio] = {
		.constraints = { /* board default 1.8V */
			.name = "vcc_bbio",
			.min_uV = 1800000,
			.max_uV = 1800000,
		},
	},
	[vcc_bb] = {
		.constraints = { /* board default 2.8V */
			.name = "vcc_bb",
			.min_uV = 2700000,
			.max_uV = 3000000,
		},
	},
	[vcc_pxa_flash] = {
		.constraints = {/* default is 1.8V */
			.name = "vcc_pxa_flash",
			.min_uV = 1800000,
			.max_uV = 1800000,
		},
	},
	[vcc_cc2420] = { /* also vcc_io */
		.constraints = {
			/* board default is 2.8V */
			.name = "vcc_cc2420",
			.min_uV = 2700000,
			.max_uV = 3300000,
		},
	},
	[vcc_vref] = { /* Reference for what? */
		.constraints = { /* default 1.8V */
			.name = "vcc_vref",
			.min_uV = 1800000,
			.max_uV = 1800000,
		},
	},
	[vcc_sram_ext] = {
		.constraints = { /* default 2.8V */
			.name = "vcc_sram_ext",
			.min_uV = 2800000,
			.max_uV = 2800000,
		},
	},
	[vcc_mica] = {
		.constraints = { /* default 2.8V */
			.name = "vcc_mica",
			.min_uV = 2800000,
			.max_uV = 2800000,
		},
	},
	[vcc_bt] = {
		.constraints = { /* default 2.8V */
			.name = "vcc_bt",
			.min_uV = 2800000,
			.max_uV = 2800000,
		},
	},
	[vcc_lcd] = {
		.constraints = { /* default 2.8V */
			.name = "vcc_lcd",
			.min_uV = 2700000,
			.max_uV = 3300000,
		},
	},
	[vcc_io] = { /* Same or higher than everything
			  * bar vccbat and vccusb */
		.constraints = { /* default 2.8V */
			.name = "vcc_io",
			.min_uV = 2692000,
			.max_uV = 3300000,
		},
	},
	[vcc_sensor_1_8] = {
		.constraints = { /* default 1.8V */
			.name = "vcc_sensor_1_8",
			.min_uV = 1800000,
			.max_uV = 1800000,
		},
	},
	[vcc_sensor_3] = { /* curiously default 2.8V */
		.constraints = {
			.name = "vcc_sensor_3",
			.min_uV = 2800000,
			.max_uV = 3000000,
		},
		.num_consumer_supplies = ARRAY_SIZE(stargate2_sensor_3_con),
		.consumer_supplies = stargate2_sensor_3_con,
	},
	[vcc_pxa_pll] = { /* 1.17V - 1.43V, default 1.3V*/
		.constraints = {
			.name = "vcc_pxa_pll",
			.min_uV = 1170000,
			.max_uV = 1430000,
		},
	},
	[vcc_pxa_usim] = {
		.constraints = { /* default 1.8V */
			.name = "vcc_pxa_usim",
			.min_uV = 1710000,
			.max_uV = 2160000,
		},
	},
	[vcc_pxa_mem] = {
		.constraints = { /* default 1.8V */
			.name = "vcc_pxa_mem",
			.min_uV = 1800000,
			.max_uV = 1800000,
		},
	},
};

static struct mtd_partition stargate2flash_partitions[] = {
	{
		.name = "Bootloader",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = 0,
	}, {
		.name = "Kernel",
		.size = 0x00200000,
		.offset = 0x00040000,
		.mask_flags = 0
	}, {
		.name = "Filesystem",
		.size = 0x01DC0000,
		.offset = 0x00240000,
		.mask_flags = 0
	},
};

static struct resource flash_resources = {
	.start = PXA_CS0_PHYS,
	.end = PXA_CS0_PHYS + SZ_32M - 1,
	.flags = IORESOURCE_MEM,
};

static struct flash_platform_data stargate2_flash_data = {
	.map_name = "cfi_probe",
	.parts = stargate2flash_partitions,
	.nr_parts = ARRAY_SIZE(stargate2flash_partitions),
	.name = "PXA27xOnChipROM",
	.width = 2,
};

static struct platform_device stargate2_flash_device = {
	.name = "pxa2xx-flash",
	.id = 0,
	.dev = {
		.platform_data = &stargate2_flash_data,
	},
	.resource = &flash_resources,
	.num_resources = 1,
};

static struct pxa2xx_spi_master pxa_ssp_master_0_info = {
	.num_chipselect = 1,
};

static struct pxa2xx_spi_master pxa_ssp_master_1_info = {
	.num_chipselect = 1,
};

static struct pxa2xx_spi_master pxa_ssp_master_2_info = {
	.num_chipselect = 1,
};

/* An upcoming kernel change will scrap SFRM usage so these
 * drivers have been moved to use gpio's via cs_control */
static struct pxa2xx_spi_chip staccel_chip_info = {
	.tx_threshold = 8,
	.rx_threshold = 8,
	.dma_burst_size = 8,
	.timeout = 235,
	.gpio_cs = 24,
};

static struct pxa2xx_spi_chip cc2420_info = {
	.tx_threshold = 8,
	.rx_threshold = 8,
	.dma_burst_size = 8,
	.timeout = 235,
	.gpio_cs = 39,
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias = "lis3l02dq",
		.max_speed_hz = 8000000,/* 8MHz max spi frequency at 3V */
		.bus_num = 1,
		.chip_select = 0,
		.controller_data = &staccel_chip_info,
		.irq = PXA_GPIO_TO_IRQ(96),
	}, {
		.modalias = "cc2420",
		.max_speed_hz = 6500000,
		.bus_num = 3,
		.chip_select = 0,
		.controller_data = &cc2420_info,
	},
};

static void sg2_udc_command(int cmd)
{
	switch (cmd) {
	case PXA2XX_UDC_CMD_CONNECT:
		UP2OCR |=  UP2OCR_HXOE  | UP2OCR_DPPUE | UP2OCR_DPPUBE;
		break;
	case PXA2XX_UDC_CMD_DISCONNECT:
		UP2OCR &= ~(UP2OCR_HXOE  | UP2OCR_DPPUE | UP2OCR_DPPUBE);
		break;
	}
}

static struct i2c_pxa_platform_data i2c_pwr_pdata = {
	.fast_mode = 1,
};

static struct i2c_pxa_platform_data i2c_pdata = {
	.fast_mode = 1,
};

static void __init imote2_stargate2_init(void)
{

	pxa2xx_mfp_config(ARRAY_AND_SIZE(sg2_im2_unified_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	pxa2xx_set_spi_info(1, &pxa_ssp_master_0_info);
	pxa2xx_set_spi_info(2, &pxa_ssp_master_1_info);
	pxa2xx_set_spi_info(3, &pxa_ssp_master_2_info);
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));


	pxa27x_set_i2c_power_info(&i2c_pwr_pdata);
	pxa_set_i2c_info(&i2c_pdata);
}

#ifdef CONFIG_MACH_INTELMOTE2
/* As the the imote2 doesn't currently have a conventional SD slot
 * there is no option to hotplug cards, making all this rather simple
 */
static int imote2_mci_get_ro(struct device *dev)
{
	return 0;
}

/* Rather simple case as hotplugging not possible */
static struct pxamci_platform_data imote2_mci_platform_data = {
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34, /* default anyway */
	.get_ro = imote2_mci_get_ro,
	.gpio_card_detect = -1,
	.gpio_card_ro	= -1,
	.gpio_power = -1,
};

static struct gpio_led imote2_led_pins[] = {
	{
		.name       =  "imote2:red",
		.gpio       = 103,
		.active_low = 1,
	}, {
		.name       = "imote2:green",
		.gpio       = 104,
		.active_low = 1,
	}, {
		.name       = "imote2:blue",
		.gpio       = 105,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data imote2_led_data = {
	.num_leds = ARRAY_SIZE(imote2_led_pins),
	.leds     = imote2_led_pins,
};

static struct platform_device imote2_leds = {
	.name = "leds-gpio",
	.id   = -1,
	.dev = {
		.platform_data = &imote2_led_data,
	},
};

static struct da903x_subdev_info imote2_da9030_subdevs[] = {
	{
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO2,
		.platform_data = &stargate2_ldo_init_data[vcc_bbio],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO3,
		.platform_data = &stargate2_ldo_init_data[vcc_bb],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO4,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_flash],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO5,
		.platform_data = &stargate2_ldo_init_data[vcc_cc2420],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO6,
		.platform_data = &stargate2_ldo_init_data[vcc_vref],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO7,
		.platform_data = &stargate2_ldo_init_data[vcc_sram_ext],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO8,
		.platform_data = &stargate2_ldo_init_data[vcc_mica],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO9,
		.platform_data = &stargate2_ldo_init_data[vcc_bt],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO10,
		.platform_data = &stargate2_ldo_init_data[vcc_sensor_1_8],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO11,
		.platform_data = &stargate2_ldo_init_data[vcc_sensor_3],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO12,
		.platform_data = &stargate2_ldo_init_data[vcc_lcd],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO15,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_pll],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO17,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_usim],
	}, {
		.name = "da903x-regulator", /*pxa vcc i/o and cc2420 vcc i/o */
		.id = DA9030_ID_LDO18,
		.platform_data = &stargate2_ldo_init_data[vcc_io],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO19,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_mem],
	},
};

static struct da903x_platform_data imote2_da9030_pdata = {
	.num_subdevs = ARRAY_SIZE(imote2_da9030_subdevs),
	.subdevs = imote2_da9030_subdevs,
};

static struct i2c_board_info __initdata imote2_pwr_i2c_board_info[] = {
	{
		.type = "da9030",
		.addr = 0x49,
		.platform_data = &imote2_da9030_pdata,
		.irq = PXA_GPIO_TO_IRQ(1),
	},
};

static struct i2c_board_info __initdata imote2_i2c_board_info[] = {
	{ /* UCAM sensor board */
		.type = "max1239",
		.addr = 0x35,
	}, { /* ITS400 Sensor board only */
		.type = "max1363",
		.addr = 0x34,
		/* Through a nand gate - Also beware, on V2 sensor board the
		 * pull up resistors are missing.
		 */
		.irq = PXA_GPIO_TO_IRQ(99),
	}, { /* ITS400 Sensor board only */
		.type = "tsl2561",
		.addr = 0x49,
		/* Through a nand gate - Also beware, on V2 sensor board the
		 * pull up resistors are missing.
		 */
		.irq = PXA_GPIO_TO_IRQ(99),
	}, { /* ITS400 Sensor board only */
		.type = "tmp175",
		.addr = 0x4A,
		.irq = PXA_GPIO_TO_IRQ(96),
	}, { /* IMB400 Multimedia board */
		.type = "wm8940",
		.addr = 0x1A,
	},
};

static unsigned long imote2_pin_config[] __initdata = {

	/* Button */
	GPIO91_GPIO,

	/* LEDS */
	GPIO103_GPIO, /* red led */
	GPIO104_GPIO, /* green led */
	GPIO105_GPIO, /* blue led */
};

static struct pxa2xx_udc_mach_info imote2_udc_info __initdata = {
	.udc_command		= sg2_udc_command,
};

static struct platform_device imote2_audio_device = {
	.name = "imote2-audio",
	.id   = -1,
};

static struct platform_device *imote2_devices[] = {
	&stargate2_flash_device,
	&imote2_leds,
	&sht15,
	&imote2_audio_device,
};

static void __init imote2_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(imote2_pin_config));

	imote2_stargate2_init();

	platform_add_devices(imote2_devices, ARRAY_SIZE(imote2_devices));

	i2c_register_board_info(0, imote2_i2c_board_info,
				ARRAY_SIZE(imote2_i2c_board_info));
	i2c_register_board_info(1, imote2_pwr_i2c_board_info,
				ARRAY_SIZE(imote2_pwr_i2c_board_info));

	pxa_set_mci_info(&imote2_mci_platform_data);
	pxa_set_udc_info(&imote2_udc_info);
}
#endif

#ifdef CONFIG_MACH_STARGATE2

static unsigned long stargate2_pin_config[] __initdata = {

	GPIO15_nCS_1, /* SRAM */
	/* SMC91x */
	GPIO80_nCS_4,
	GPIO40_GPIO, /*cable detect?*/

	/* Button */
	GPIO91_GPIO | WAKEUP_ON_LEVEL_HIGH,

	/* Compact Flash */
	GPIO79_PSKTSEL,
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO120_GPIO, /* Buff ctrl */
	GPIO108_GPIO, /* Power ctrl */
	GPIO82_GPIO, /* Reset */
	GPIO53_GPIO, /* SG2_S0_GPIO_DETECT */

	/* MMC not shared with imote2 */
	GPIO90_GPIO, /* nSD detect */
	GPIO89_GPIO, /* SD_POWER_ENABLE */

	/* Bluetooth */
	GPIO81_GPIO, /* reset */
};

static struct resource smc91x_resources[] = {
	[0] = {
		.name = "smc91x-regs",
		.start = (PXA_CS4_PHYS + 0x300),
		.end = (PXA_CS4_PHYS + 0xfffff),
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PXA_GPIO_TO_IRQ(40),
		.end = PXA_GPIO_TO_IRQ(40),
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct smc91x_platdata stargate2_smc91x_info = {
	.flags = SMC91X_USE_8BIT | SMC91X_USE_16BIT | SMC91X_USE_32BIT
	| SMC91X_NOWAIT | SMC91X_USE_DMA,
};

static struct platform_device smc91x_device = {
	.name = "smc91x",
	.id = -1,
	.num_resources = ARRAY_SIZE(smc91x_resources),
	.resource = smc91x_resources,
	.dev = {
		.platform_data = &stargate2_smc91x_info,
	},
};


/*
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert / eject.
 */
static int stargate2_mci_init(struct device *dev,
			      irq_handler_t stargate2_detect_int,
			      void *data)
{
	int err;

	err = gpio_request(SG2_SD_POWER_ENABLE, "SG2_sd_power_enable");
	if (err) {
		printk(KERN_ERR "Can't get the gpio for SD power control");
		goto return_err;
	}
	gpio_direction_output(SG2_SD_POWER_ENABLE, 0);

	err = gpio_request(SG2_GPIO_nSD_DETECT, "SG2_sd_detect");
	if (err) {
		printk(KERN_ERR "Can't get the sd detect gpio");
		goto free_power_en;
	}
	gpio_direction_input(SG2_GPIO_nSD_DETECT);

	err = request_irq(PXA_GPIO_TO_IRQ(SG2_GPIO_nSD_DETECT),
			  stargate2_detect_int,
			  IRQ_TYPE_EDGE_BOTH,
			  "MMC card detect",
			  data);
	if (err) {
		printk(KERN_ERR "can't request MMC card detect IRQ\n");
		goto free_nsd_detect;
	}
	return 0;

 free_nsd_detect:
	gpio_free(SG2_GPIO_nSD_DETECT);
 free_power_en:
	gpio_free(SG2_SD_POWER_ENABLE);
 return_err:
	return err;
}

/**
 * stargate2_mci_setpower() - set state of mmc power supply
 *
 * Very simple control. Either it is on or off and is controlled by
 * a gpio pin */
static void stargate2_mci_setpower(struct device *dev, unsigned int vdd)
{
	gpio_set_value(SG2_SD_POWER_ENABLE, !!vdd);
}

static void stargate2_mci_exit(struct device *dev, void *data)
{
	free_irq(PXA_GPIO_TO_IRQ(SG2_GPIO_nSD_DETECT), data);
	gpio_free(SG2_SD_POWER_ENABLE);
	gpio_free(SG2_GPIO_nSD_DETECT);
}

static struct pxamci_platform_data stargate2_mci_platform_data = {
	.detect_delay_ms = 250,
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34,
	.init = stargate2_mci_init,
	.setpower = stargate2_mci_setpower,
	.exit = stargate2_mci_exit,
};


/*
 * SRAM - The Stargate 2 has 32MB of SRAM.
 *
 * Here it is made available as an MTD. This will then
 * typically have a cifs filesystem created on it to provide
 * fast temporary storage.
 */
static struct resource sram_resources = {
	.start = PXA_CS1_PHYS,
	.end = PXA_CS1_PHYS + SZ_32M-1,
	.flags = IORESOURCE_MEM,
};

static struct platdata_mtd_ram stargate2_sram_pdata = {
	.mapname = "Stargate2 SRAM",
	.bankwidth = 2,
};

static struct platform_device stargate2_sram = {
	.name = "mtd-ram",
	.id = 0,
	.resource = &sram_resources,
	.num_resources = 1,
	.dev = {
		.platform_data = &stargate2_sram_pdata,
	},
};

static struct pcf857x_platform_data platform_data_pcf857x = {
	.gpio_base = 128,
	.n_latch = 0,
	.setup = NULL,
	.teardown = NULL,
	.context = NULL,
};

static struct at24_platform_data pca9500_eeprom_pdata = {
	.byte_len = 256,
	.page_size = 4,
};

/**
 * stargate2_reset_bluetooth() reset the bluecore to ensure consistent state
 **/
static int stargate2_reset_bluetooth(void)
{
	int err;
	err = gpio_request(SG2_BT_RESET, "SG2_BT_RESET");
	if (err) {
		printk(KERN_ERR "Could not get gpio for bluetooth reset\n");
		return err;
	}
	gpio_direction_output(SG2_BT_RESET, 1);
	mdelay(5);
	/* now reset it - 5 msec minimum */
	gpio_set_value(SG2_BT_RESET, 0);
	mdelay(10);
	gpio_set_value(SG2_BT_RESET, 1);
	gpio_free(SG2_BT_RESET);
	return 0;
}

static struct led_info stargate2_leds[] = {
	{
		.name = "sg2:red",
		.flags = DA9030_LED_RATE_ON,
	}, {
		.name = "sg2:blue",
		.flags = DA9030_LED_RATE_ON,
	}, {
		.name = "sg2:green",
		.flags = DA9030_LED_RATE_ON,
	},
};

static struct da903x_subdev_info stargate2_da9030_subdevs[] = {
	{
		.name = "da903x-led",
		.id = DA9030_ID_LED_2,
		.platform_data = &stargate2_leds[0],
	}, {
		.name = "da903x-led",
		.id = DA9030_ID_LED_3,
		.platform_data = &stargate2_leds[2],
	}, {
		.name = "da903x-led",
		.id = DA9030_ID_LED_4,
		.platform_data = &stargate2_leds[1],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO2,
		.platform_data = &stargate2_ldo_init_data[vcc_bbio],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO3,
		.platform_data = &stargate2_ldo_init_data[vcc_bb],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO4,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_flash],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO5,
		.platform_data = &stargate2_ldo_init_data[vcc_cc2420],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO6,
		.platform_data = &stargate2_ldo_init_data[vcc_vref],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO7,
		.platform_data = &stargate2_ldo_init_data[vcc_sram_ext],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO8,
		.platform_data = &stargate2_ldo_init_data[vcc_mica],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO9,
		.platform_data = &stargate2_ldo_init_data[vcc_bt],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO10,
		.platform_data = &stargate2_ldo_init_data[vcc_sensor_1_8],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO11,
		.platform_data = &stargate2_ldo_init_data[vcc_sensor_3],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO12,
		.platform_data = &stargate2_ldo_init_data[vcc_lcd],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO15,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_pll],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO17,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_usim],
	}, {
		.name = "da903x-regulator", /*pxa vcc i/o and cc2420 vcc i/o */
		.id = DA9030_ID_LDO18,
		.platform_data = &stargate2_ldo_init_data[vcc_io],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO19,
		.platform_data = &stargate2_ldo_init_data[vcc_pxa_mem],
	},
};

static struct da903x_platform_data stargate2_da9030_pdata = {
	.num_subdevs = ARRAY_SIZE(stargate2_da9030_subdevs),
	.subdevs = stargate2_da9030_subdevs,
};

static struct i2c_board_info __initdata stargate2_pwr_i2c_board_info[] = {
	{
		.type = "da9030",
		.addr = 0x49,
		.platform_data = &stargate2_da9030_pdata,
		.irq = PXA_GPIO_TO_IRQ(1),
	},
};

static struct i2c_board_info __initdata stargate2_i2c_board_info[] = {
	/* Techically this a pca9500 - but it's compatible with the 8574
	 * for gpio expansion and the 24c02 for eeprom access.
	 */
	{
		.type = "pcf8574",
		.addr =  0x27,
		.platform_data = &platform_data_pcf857x,
	}, {
		.type = "24c02",
		.addr = 0x57,
		.platform_data = &pca9500_eeprom_pdata,
	}, {
		.type = "max1238",
		.addr = 0x35,
	}, { /* ITS400 Sensor board only */
		.type = "max1363",
		.addr = 0x34,
		/* Through a nand gate - Also beware, on V2 sensor board the
		 * pull up resistors are missing.
		 */
		.irq = PXA_GPIO_TO_IRQ(99),
	}, { /* ITS400 Sensor board only */
		.type = "tsl2561",
		.addr = 0x49,
		/* Through a nand gate - Also beware, on V2 sensor board the
		 * pull up resistors are missing.
		 */
		.irq = PXA_GPIO_TO_IRQ(99),
	}, { /* ITS400 Sensor board only */
		.type = "tmp175",
		.addr = 0x4A,
		.irq = PXA_GPIO_TO_IRQ(96),
	},
};

/* Board doesn't support cable detection - so always lie and say
 * something is there.
 */
static int sg2_udc_detect(void)
{
	return 1;
}

static struct pxa2xx_udc_mach_info stargate2_udc_info __initdata = {
	.udc_is_connected	= sg2_udc_detect,
	.udc_command		= sg2_udc_command,
};

static struct platform_device *stargate2_devices[] = {
	&stargate2_flash_device,
	&stargate2_sram,
	&smc91x_device,
	&sht15,
};

static void __init stargate2_init(void)
{
	/* This is probably a board specific hack as this must be set
	   prior to connecting the MFP stuff up. */
	__raw_writel(__raw_readl(MECR) & ~MECR_NOS, MECR);

	pxa2xx_mfp_config(ARRAY_AND_SIZE(stargate2_pin_config));

	imote2_stargate2_init();

	platform_add_devices(ARRAY_AND_SIZE(stargate2_devices));

	i2c_register_board_info(0, ARRAY_AND_SIZE(stargate2_i2c_board_info));
	i2c_register_board_info(1, stargate2_pwr_i2c_board_info,
				ARRAY_SIZE(stargate2_pwr_i2c_board_info));

	pxa_set_mci_info(&stargate2_mci_platform_data);

	pxa_set_udc_info(&stargate2_udc_info);

	stargate2_reset_bluetooth();
}
#endif

#ifdef CONFIG_MACH_INTELMOTE2
MACHINE_START(INTELMOTE2, "IMOTE 2")
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.timer		= &pxa_timer,
	.init_machine	= imote2_init,
	.atag_offset	= 0x100,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_STARGATE2
MACHINE_START(STARGATE2, "Stargate 2")
	.map_io = pxa27x_map_io,
	.nr_irqs = STARGATE_NR_IRQS,
	.init_irq = pxa27x_init_irq,
	.handle_irq = pxa27x_handle_irq,
	.timer = &pxa_timer,
	.init_machine = stargate2_init,
	.atag_offset = 0x100,
	.restart	= pxa_restart,
MACHINE_END
#endif
