/*
 * Copyright 2007 Robert Schwebel <r.schwebel@pengutronix.de>, Pengutronix
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/io.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/mc13783.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/board-pcm038.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/i2c.h>
#include <mach/iomux-mx27.h>
#include <mach/imx-uart.h>
#include <mach/mxc_nand.h>
#include <mach/spi.h>
#include <mach/mxc_ehci.h>
#include <mach/ulpi.h>

#include "devices.h"

static int pcm038_pins[] = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* UART2 */
	PE3_PF_UART2_CTS,
	PE4_PF_UART2_RTS,
	PE6_PF_UART2_TXD,
	PE7_PF_UART2_RXD,
	/* UART3 */
	PE8_PF_UART3_TXD,
	PE9_PF_UART3_RXD,
	PE10_PF_UART3_CTS,
	PE11_PF_UART3_RTS,
	/* FEC */
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_RX_CLK,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN,
	/* I2C2 */
	PC5_PF_I2C2_SDA,
	PC6_PF_I2C2_SCL,
	/* SPI1 */
	PD25_PF_CSPI1_RDY,
	PD29_PF_CSPI1_SCLK,
	PD30_PF_CSPI1_MISO,
	PD31_PF_CSPI1_MOSI,
	/* SSI1 */
	PC20_PF_SSI1_FS,
	PC21_PF_SSI1_RXD,
	PC22_PF_SSI1_TXD,
	PC23_PF_SSI1_CLK,
	/* SSI4 */
	PC16_PF_SSI4_FS,
	PC17_PF_SSI4_RXD,
	PC18_PF_SSI4_TXD,
	PC19_PF_SSI4_CLK,
	/* USB host */
	PA0_PF_USBH2_CLK,
	PA1_PF_USBH2_DIR,
	PA2_PF_USBH2_DATA7,
	PA3_PF_USBH2_NXT,
	PA4_PF_USBH2_STP,
	PD19_AF_USBH2_DATA4,
	PD20_AF_USBH2_DATA3,
	PD21_AF_USBH2_DATA6,
	PD22_AF_USBH2_DATA0,
	PD23_AF_USBH2_DATA2,
	PD24_AF_USBH2_DATA1,
	PD26_AF_USBH2_DATA5,
};

/*
 * Phytec's PCM038 comes with 2MiB battery buffered SRAM,
 * 16 bit width
 */

static struct platdata_mtd_ram pcm038_sram_data = {
	.bankwidth = 2,
};

static struct resource pcm038_sram_resource = {
	.start = MX27_CS1_BASE_ADDR,
	.end   = MX27_CS1_BASE_ADDR + 512 * 1024 - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device pcm038_sram_mtd_device = {
	.name = "mtd-ram",
	.id = 0,
	.dev = {
		.platform_data = &pcm038_sram_data,
	},
	.num_resources = 1,
	.resource = &pcm038_sram_resource,
};

/*
 * Phytec's phyCORE-i.MX27 comes with 32MiB flash,
 * 16 bit width
 */
static struct physmap_flash_data pcm038_flash_data = {
	.width = 2,
};

static struct resource pcm038_flash_resource = {
	.start = 0xc0000000,
	.end   = 0xc1ffffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device pcm038_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &pcm038_flash_data,
	},
	.num_resources = 1,
	.resource = &pcm038_flash_resource,
};

static struct imxuart_platform_data uart_pdata[] = {
	{
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.flags = IMXUART_HAVE_RTSCTS,
	},
};

static struct mxc_nand_platform_data pcm038_nand_board_info = {
	.width = 1,
	.hw_ecc = 1,
};

static struct platform_device *platform_devices[] __initdata = {
	&pcm038_nor_mtd_device,
	&mxc_w1_master_device,
	&mxc_fec_device,
	&pcm038_sram_mtd_device,
	&mxc_wdt,
};

/* On pcm038 there's a sram attached to CS1, we enable the chipselect here and
 * setup other stuffs to access the sram. */
static void __init pcm038_init_sram(void)
{
	mx27_setup_weimcs(1, 0x0000d843, 0x22252521, 0x22220a00);
}

static struct imxi2c_platform_data pcm038_i2c_1_data = {
	.bitrate = 100000,
};

static struct at24_platform_data board_eeprom = {
	.byte_len = 4096,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static struct i2c_board_info pcm038_i2c_devices[] = {
	{
		I2C_BOARD_INFO("at24", 0x52), /* E0=0, E1=1, E2=0 */
		.platform_data = &board_eeprom,
	}, {
		I2C_BOARD_INFO("pcf8563", 0x51),
	}, {
		I2C_BOARD_INFO("lm75", 0x4a),
	}
};

static int pcm038_spi_cs[] = {GPIO_PORTD + 28};

static struct spi_imx_master pcm038_spi_0_data = {
	.chipselect = pcm038_spi_cs,
	.num_chipselect = ARRAY_SIZE(pcm038_spi_cs),
};

static struct regulator_consumer_supply sdhc1_consumers[] = {
	{
		.dev	= &mxc_sdhc_device1.dev,
		.supply	= "sdhc_vcc",
	},
};

static struct regulator_init_data sdhc1_data = {
	.constraints = {
		.min_uV = 3000000,
		.max_uV = 3400000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
			REGULATOR_MODE_FAST,
		.always_on = 0,
		.boot_on = 0,
	},
	.num_consumer_supplies = ARRAY_SIZE(sdhc1_consumers),
	.consumer_supplies = sdhc1_consumers,
};

static struct regulator_consumer_supply cam_consumers[] = {
	{
		.dev	= NULL,
		.supply	= "imx_cam_vcc",
	},
};

static struct regulator_init_data cam_data = {
	.constraints = {
		.min_uV = 3000000,
		.max_uV = 3400000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
			REGULATOR_MODE_FAST,
		.always_on = 0,
		.boot_on = 0,
	},
	.num_consumer_supplies = ARRAY_SIZE(cam_consumers),
	.consumer_supplies = cam_consumers,
};

struct mc13783_regulator_init_data pcm038_regulators[] = {
	{
		.id = MC13783_REGU_VCAM,
		.init_data = &cam_data,
	}, {
		.id = MC13783_REGU_VMMC1,
		.init_data = &sdhc1_data,
	},
};

static struct mc13783_platform_data pcm038_pmic = {
	.regulators = pcm038_regulators,
	.num_regulators = ARRAY_SIZE(pcm038_regulators),
	.flags = MC13783_USE_ADC | MC13783_USE_REGULATOR |
		 MC13783_USE_TOUCHSCREEN,
};

static struct spi_board_info pcm038_spi_board_info[] __initdata = {
	{
		.modalias = "mc13783",
		.irq = IRQ_GPIOB(23),
		.max_speed_hz = 300000,
		.bus_num = 0,
		.chip_select = 0,
		.platform_data = &pcm038_pmic,
		.mode = SPI_CS_HIGH,
	}
};

static struct mxc_usbh_platform_data usbh2_pdata = {
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= MXC_EHCI_POWER_PINS_ENABLED | MXC_EHCI_INTERFACE_DIFF_UNI,
};

static void __init pcm038_init(void)
{
	mxc_gpio_setup_multiple_pins(pcm038_pins, ARRAY_SIZE(pcm038_pins),
			"PCM038");

	pcm038_init_sram();

	mxc_register_device(&mxc_uart_device0, &uart_pdata[0]);
	mxc_register_device(&mxc_uart_device1, &uart_pdata[1]);
	mxc_register_device(&mxc_uart_device2, &uart_pdata[2]);

	mxc_gpio_mode(PE16_AF_OWIRE);
	mxc_register_device(&imx27_nand_device, &pcm038_nand_board_info);

	/* only the i2c master 1 is used on this CPU card */
	i2c_register_board_info(1, pcm038_i2c_devices,
				ARRAY_SIZE(pcm038_i2c_devices));

	mxc_register_device(&mxc_i2c_device1, &pcm038_i2c_1_data);

	/* PE18 for user-LED D40 */
	mxc_gpio_mode(GPIO_PORTE | 18 | GPIO_GPIO | GPIO_OUT);

	mxc_gpio_mode(GPIO_PORTD | 28 | GPIO_GPIO | GPIO_OUT);

	/* MC13783 IRQ */
	mxc_gpio_mode(GPIO_PORTB | 23 | GPIO_GPIO | GPIO_IN);

	mxc_register_device(&mxc_spi_device0, &pcm038_spi_0_data);
	spi_register_board_info(pcm038_spi_board_info,
				ARRAY_SIZE(pcm038_spi_board_info));

	mxc_register_device(&mxc_usbh2, &usbh2_pdata);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

#ifdef CONFIG_MACH_PCM970_BASEBOARD
	pcm970_baseboard_init();
#endif
}

static void __init pcm038_timer_init(void)
{
	mx27_clocks_init(26000000);
}

static struct sys_timer pcm038_timer = {
	.init = pcm038_timer_init,
};

MACHINE_START(PCM038, "phyCORE-i.MX27")
	.phys_io        = MX27_AIPI_BASE_ADDR,
	.io_pg_offst    = ((MX27_AIPI_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = MX27_PHYS_OFFSET + 0x100,
	.map_io         = mx27_map_io,
	.init_irq       = mx27_init_irq,
	.init_machine   = pcm038_init,
	.timer          = &pcm038_timer,
MACHINE_END
