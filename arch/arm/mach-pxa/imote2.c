/*
 * linux/arch/arm/mach-pxa/imote2.c
 *
 * Author:	Ed C. Epp
 * Created:	Nov 05, 2002
 * Copyright:	Intel Corp.
 *
 * Modified 2008:  Jonathan Cameron
 *
 * The Imote2 is a wireless sensor node platform sold
 * by Crossbow (www.xbow.com).
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/mfd/da903x.h>
#include <linux/sht15.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <plat/i2c.h>
#include <mach/udc.h>
#include <mach/mmc.h>
#include <mach/pxa2xx_spi.h>
#include <mach/pxa27x-udc.h>

#include "devices.h"
#include "generic.h"

static unsigned long imote2_pin_config[] __initdata = {

	/* Device Identification for wakeup*/
	GPIO102_GPIO,

	/* Button */
	GPIO91_GPIO,

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
	GPIO39_GPIO,			/* CSn */
	GPIO115_GPIO,			/* Power enable */

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* SSP 3 - 802.15.4 radio */
	GPIO39_GPIO, 			/* Chip Select */
	GPIO34_SSP3_SCLK,
	GPIO35_SSP3_TXD,
	GPIO41_SSP3_RXD,

	/* SSP 2 - to daughter boards */
	GPIO37_GPIO,			/* Chip Select */
	GPIO36_SSP2_SCLK,
	GPIO38_SSP2_TXD,
	GPIO11_SSP2_RXD,

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

	/* STUART Serial console via debug board*/
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* Basic sensor board */
	GPIO96_GPIO,	/* accelerometer interrupt */
	GPIO99_GPIO,	/* ADC interrupt */

	/* SHT15 */
	GPIO100_GPIO,
	GPIO98_GPIO,

	/* Connector pins specified as gpios */
	GPIO94_GPIO, /* large basic connector pin 14 */
	GPIO10_GPIO, /* large basic connector pin 23 */

	/* LEDS */
	GPIO103_GPIO, /* red led */
	GPIO104_GPIO, /* green led */
	GPIO105_GPIO, /* blue led */
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

static struct regulator_consumer_supply imote2_sensor_3_con[] = {
	{
		.dev = &sht15.dev,
		.supply = "vcc",
	},
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

/* Reverse engineered partly from Platformx drivers */
enum imote2_ldos{
	vcc_vref,
	vcc_cc2420,
	vcc_mica,
	vcc_bt,
	/* The two voltages available to sensor boards */
	vcc_sensor_1_8,
	vcc_sensor_3,

	vcc_sram_ext, /* directly connected to the pxa271 */
	vcc_pxa_pll,
	vcc_pxa_usim, /* Reference voltage for certain gpios */
	vcc_pxa_mem,
	vcc_pxa_flash,
	vcc_pxa_core, /*Dc-Dc buck not yet supported */
	vcc_lcd,
	vcc_bb,
	vcc_bbio,
	vcc_io, /* cc2420 802.15.4 radio and pxa vcc_io ?*/
};

/* The values of the various regulator constraints are obviously dependent
 * on exactly what is wired to each ldo.  Unfortunately this information is
 * not generally available.  More information has been requested from Xbow
 * but as of yet they haven't been forthcoming.
 *
 * Some of these are clearly Stargate 2 related (no way of plugging
 * in an lcd on the IM2 for example!).
 */
static struct regulator_init_data imote2_ldo_init_data[] = {
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
		.num_consumer_supplies = ARRAY_SIZE(imote2_sensor_3_con),
		.consumer_supplies = imote2_sensor_3_con,
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

static struct da903x_subdev_info imote2_da9030_subdevs[] = {
	{
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO2,
		.platform_data = &imote2_ldo_init_data[vcc_bbio],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO3,
		.platform_data = &imote2_ldo_init_data[vcc_bb],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO4,
		.platform_data = &imote2_ldo_init_data[vcc_pxa_flash],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO5,
		.platform_data = &imote2_ldo_init_data[vcc_cc2420],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO6,
		.platform_data = &imote2_ldo_init_data[vcc_vref],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO7,
		.platform_data = &imote2_ldo_init_data[vcc_sram_ext],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO8,
		.platform_data = &imote2_ldo_init_data[vcc_mica],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO9,
		.platform_data = &imote2_ldo_init_data[vcc_bt],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO10,
		.platform_data = &imote2_ldo_init_data[vcc_sensor_1_8],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO11,
		.platform_data = &imote2_ldo_init_data[vcc_sensor_3],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO12,
		.platform_data = &imote2_ldo_init_data[vcc_lcd],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO15,
		.platform_data = &imote2_ldo_init_data[vcc_pxa_pll],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO17,
		.platform_data = &imote2_ldo_init_data[vcc_pxa_usim],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO18,
		.platform_data = &imote2_ldo_init_data[vcc_io],
	}, {
		.name = "da903x-regulator",
		.id = DA9030_ID_LDO19,
		.platform_data = &imote2_ldo_init_data[vcc_pxa_mem],
	},
};

static struct da903x_platform_data imote2_da9030_pdata = {
	.num_subdevs = ARRAY_SIZE(imote2_da9030_subdevs),
	.subdevs = imote2_da9030_subdevs,
};

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

static struct mtd_partition imote2flash_partitions[] = {
	{
		.name = "Bootloader",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE,
	}, {
		.name = "Kernel",
		.size = 0x00200000,
		.offset = 0x00040000,
		.mask_flags = 0,
	}, {
		.name = "Filesystem",
		.size = 0x01DC0000,
		.offset = 0x00240000,
		.mask_flags = 0,
	},
};

static struct resource flash_resources = {
	.start = PXA_CS0_PHYS,
	.end = PXA_CS0_PHYS + SZ_32M - 1,
	.flags = IORESOURCE_MEM,
};

static struct flash_platform_data imote2_flash_data = {
	.map_name = "cfi_probe",
	.parts = imote2flash_partitions,
	.nr_parts = ARRAY_SIZE(imote2flash_partitions),
	.name = "PXA27xOnChipROM",
	.width = 2,
};

static struct platform_device imote2_flash_device = {
	.name = "pxa2xx-flash",
	.id = 0,
	.dev = {
		.platform_data = &imote2_flash_data,
	},
	.resource = &flash_resources,
	.num_resources = 1,
};

/* Some of the drivers here are out of kernel at the moment (parts of IIO)
 * and it may be a while before they are in the mainline.
 */
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
		.irq = IRQ_GPIO(99),
	}, { /* ITS400 Sensor board only */
		.type = "tsl2561",
		.addr = 0x49,
		/* Through a nand gate - Also beware, on V2 sensor board the
		 * pull up resistors are missing.
		 */
		.irq = IRQ_GPIO(99),
	}, { /* ITS400 Sensor board only */
		.type = "tmp175",
		.addr = 0x4A,
		.irq = IRQ_GPIO(96),
	}, { /* IMB400 Multimedia board */
		.type = "wm8940",
		.addr = 0x1A,
	},
};

static struct i2c_board_info __initdata imote2_pwr_i2c_board_info[] = {
	{
		.type = "da9030",
		.addr = 0x49,
		.platform_data = &imote2_da9030_pdata,
		.irq = gpio_to_irq(1),
	},
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
	{ /* Driver in IIO */
		.modalias = "lis3l02dq",
		.max_speed_hz = 8000000,/* 8MHz max spi frequency at 3V */
		.bus_num = 1,
		.chip_select = 0,
		.controller_data = &staccel_chip_info,
		.irq = IRQ_GPIO(96),
	}, { /* Driver out of kernel as it needs considerable rewriting */
		.modalias = "cc2420",
		.max_speed_hz = 6500000,
		.bus_num = 3,
		.chip_select = 0,
		.controller_data = &cc2420_info,
	},
};

static void im2_udc_command(int cmd)
{
	switch (cmd) {
	case PXA2XX_UDC_CMD_CONNECT:
		UP2OCR |=  UP2OCR_HXOE | UP2OCR_DPPUE | UP2OCR_DPPUBE;
		break;
	case PXA2XX_UDC_CMD_DISCONNECT:
		UP2OCR &= ~(UP2OCR_HXOE | UP2OCR_DPPUE | UP2OCR_DPPUBE);
		break;
	}
}

static struct pxa2xx_udc_mach_info imote2_udc_info __initdata = {
	.udc_command		= im2_udc_command,
};

static struct platform_device *imote2_devices[] = {
	&imote2_flash_device,
	&imote2_leds,
	&sht15,
};

static struct i2c_pxa_platform_data i2c_pwr_pdata = {
	.fast_mode = 1,
};

static struct i2c_pxa_platform_data i2c_pdata = {
	.fast_mode = 1,
};

static void __init imote2_init(void)
{

	pxa2xx_mfp_config(ARRAY_AND_SIZE(imote2_pin_config));
	/* SPI chip select directions - all other directions should
	 * be handled by drivers.*/
	gpio_direction_output(37, 0);

	platform_add_devices(imote2_devices, ARRAY_SIZE(imote2_devices));

	pxa2xx_set_spi_info(1, &pxa_ssp_master_0_info);
	pxa2xx_set_spi_info(2, &pxa_ssp_master_1_info);
	pxa2xx_set_spi_info(3, &pxa_ssp_master_2_info);

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	i2c_register_board_info(0, imote2_i2c_board_info,
				ARRAY_SIZE(imote2_i2c_board_info));
	i2c_register_board_info(1, imote2_pwr_i2c_board_info,
				ARRAY_SIZE(imote2_pwr_i2c_board_info));

	pxa27x_set_i2c_power_info(&i2c_pwr_pdata);
	pxa_set_i2c_info(&i2c_pdata);

	pxa_set_mci_info(&imote2_mci_platform_data);
	pxa_set_udc_info(&imote2_udc_info);
}

MACHINE_START(INTELMOTE2, "IMOTE 2")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= imote2_init,
	.boot_params	= 0xA0000100,
MACHINE_END
