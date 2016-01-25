/*
 * File:         arch/blackfin/mach-bf561/acvilon.c
 * Based on:     arch/blackfin/mach-bf561/ezkit.c
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *               Copyright 2009 CJSC "NII STT"
 *
 * Bugs:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *
 * For more information about Acvilon BF561 SoM please
 * go to http://www.niistt.ru/
 *
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/plat-ram.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/i2c-pca-platform.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>
#include <asm/cacheflush.h>
#include <linux/i2c.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "Acvilon board";

#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
#include <linux/usb/isp1760.h>
static struct resource bfin_isp1760_resources[] = {
	[0] = {
	       .start = 0x20000000,
	       .end = 0x20000000 + 0x000fffff,
	       .flags = IORESOURCE_MEM,
	       },
	[1] = {
	       .start = IRQ_PF15,
	       .end = IRQ_PF15,
	       .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	       },
};

static struct isp1760_platform_data isp1760_priv = {
	.is_isp1761 = 0,
	.port1_disable = 0,
	.bus_width_16 = 1,
	.port1_otg = 0,
	.analog_oc = 0,
	.dack_polarity_high = 0,
	.dreq_polarity_high = 0,
};

static struct platform_device bfin_isp1760_device = {
	.name = "isp1760-hcd",
	.id = 0,
	.dev = {
		.platform_data = &isp1760_priv,
		},
	.num_resources = ARRAY_SIZE(bfin_isp1760_resources),
	.resource = bfin_isp1760_resources,
};
#endif

static struct resource bfin_i2c_pca_resources[] = {
	{
	 .name = "pca9564-regs",
	 .start = 0x2C000000,
	 .end = 0x2C000000 + 16,
	 .flags = IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	 }, {

	     .start = IRQ_PF8,
	     .end = IRQ_PF8,
	     .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	     },
};

struct i2c_pca9564_pf_platform_data pca9564_platform_data = {
	.gpio = -1,
	.i2c_clock_speed = 330000,
	.timeout = HZ,
};

/* PCA9564 I2C Bus driver */
static struct platform_device bfin_i2c_pca_device = {
	.name = "i2c-pca-platform",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_i2c_pca_resources),
	.resource = bfin_i2c_pca_resources,
	.dev = {
		.platform_data = &pca9564_platform_data,
		}
};

/* I2C devices fitted. */
static struct i2c_board_info acvilon_i2c_devs[] __initdata = {
	{
	 I2C_BOARD_INFO("ds1339", 0x68),
	 },
	{
	 I2C_BOARD_INFO("tcn75", 0x49),
	 },
};

#if IS_ENABLED(CONFIG_MTD_PLATRAM)
static struct platdata_mtd_ram mtd_ram_data = {
	.mapname = "rootfs(RAM)",
	.bankwidth = 4,
};

static struct resource mtd_ram_resource = {
	.start = 0x4000000,
	.end = 0x5ffffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device mtd_ram_device = {
	.name = "mtd-ram",
	.id = 0,
	.dev = {
		.platform_data = &mtd_ram_data,
		},
	.num_resources = 1,
	.resource = &mtd_ram_resource,
};
#endif

#if IS_ENABLED(CONFIG_SMSC911X)
#include <linux/smsc911x.h>
static struct resource smsc911x_resources[] = {
	{
	 .name = "smsc911x-memory",
	 .start = 0x28000000,
	 .end = 0x28000000 + 0xFF,
	 .flags = IORESOURCE_MEM,
	 },
	{
	 .start = IRQ_PF7,
	 .end = IRQ_PF7,
	 .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	 },
};

static struct smsc911x_platform_config smsc911x_config = {
	.flags = SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type = SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.phy_interface = PHY_INTERFACE_MODE_MII,
};

static struct platform_device smsc911x_device = {
	.name = "smsc911x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smsc911x_resources),
	.resource = smsc911x_resources,
	.dev = {
		.platform_data = &smsc911x_config,
		},
};
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
	 .start = BFIN_UART_THR,
	 .end = BFIN_UART_GCTL + 2,
	 .flags = IORESOURCE_MEM,
	 },
	{
	 .start = IRQ_UART_TX,
	 .end = IRQ_UART_TX,
	 .flags = IORESOURCE_IRQ,
	 },
	{
	 .start = IRQ_UART_RX,
	 .end = IRQ_UART_RX,
	 .flags = IORESOURCE_IRQ,
	 },
	{
	 .start = IRQ_UART_ERROR,
	 .end = IRQ_UART_ERROR,
	 .flags = IORESOURCE_IRQ,
	 },
	{
	 .start = CH_UART_TX,
	 .end = CH_UART_TX,
	 .flags = IORESOURCE_DMA,
	 },
	{
	 .start = CH_UART_RX,
	 .end = CH_UART_RX,
	 .flags = IORESOURCE_DMA,
	 },
};

static unsigned short bfin_uart0_peripherals[] = {
	P_UART0_TX, P_UART0_RX, 0
};

static struct platform_device bfin_uart0_device = {
	.name = "bfin-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_uart0_resources),
	.resource = bfin_uart0_resources,
	.dev = {
		/* Passed to driver */
		.platform_data = &bfin_uart0_peripherals,
		},
};
#endif
#endif

#if IS_ENABLED(CONFIG_MTD_NAND_PLATFORM)

static struct mtd_partition bfin_plat_nand_partitions[] = {
	{
	 .name = "params(nand)",
	 .size = 32 * 1024 * 1024,
	 .offset = 0,
	 }, {
	     .name = "userfs(nand)",
	     .size = MTDPART_SIZ_FULL,
	     .offset = MTDPART_OFS_APPEND,
	     },
};

#define BFIN_NAND_PLAT_CLE 2
#define BFIN_NAND_PLAT_ALE 3

static void bfin_plat_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
				    unsigned int ctrl)
{
	struct nand_chip *this = mtd_to_nand(mtd);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, this->IO_ADDR_W + (1 << BFIN_NAND_PLAT_CLE));
	else
		writeb(cmd, this->IO_ADDR_W + (1 << BFIN_NAND_PLAT_ALE));
}

#define BFIN_NAND_PLAT_READY GPIO_PF10
static int bfin_plat_nand_dev_ready(struct mtd_info *mtd)
{
	return gpio_get_value(BFIN_NAND_PLAT_READY);
}

static struct platform_nand_data bfin_plat_nand_data = {
	.chip = {
		 .nr_chips = 1,
		 .chip_delay = 30,
		 .partitions = bfin_plat_nand_partitions,
		 .nr_partitions = ARRAY_SIZE(bfin_plat_nand_partitions),
		 },
	.ctrl = {
		 .cmd_ctrl = bfin_plat_nand_cmd_ctrl,
		 .dev_ready = bfin_plat_nand_dev_ready,
		 },
};

#define MAX(x, y) (x > y ? x : y)
static struct resource bfin_plat_nand_resources = {
	.start = 0x24000000,
	.end = 0x24000000 + (1 << MAX(BFIN_NAND_PLAT_CLE, BFIN_NAND_PLAT_ALE)),
	.flags = IORESOURCE_MEM,
};

static struct platform_device bfin_async_nand_device = {
	.name = "gen_nand",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_plat_nand_resources,
	.dev = {
		.platform_data = &bfin_plat_nand_data,
		},
};

static void bfin_plat_nand_init(void)
{
	gpio_request(BFIN_NAND_PLAT_READY, "bfin_nand_plat");
}
#else
static void bfin_plat_nand_init(void)
{
}
#endif

#if IS_ENABLED(CONFIG_MTD_DATAFLASH)
static struct mtd_partition bfin_spi_dataflash_partitions[] = {
	{
	 .name = "bootloader",
	 .size = 0x4200,
	 .offset = 0,
	 .mask_flags = MTD_CAP_ROM},
	{
	 .name = "u-boot",
	 .size = 0x42000,
	 .offset = MTDPART_OFS_APPEND,
	 },
	{
	 .name = "u-boot(params)",
	 .size = 0x4200,
	 .offset = MTDPART_OFS_APPEND,
	 },
	{
	 .name = "kernel",
	 .size = 0x294000,
	 .offset = MTDPART_OFS_APPEND,
	 },
	{
	 .name = "params",
	 .size = 0x42000,
	 .offset = MTDPART_OFS_APPEND,
	 },
	{
	 .name = "rootfs",
	 .size = MTDPART_SIZ_FULL,
	 .offset = MTDPART_OFS_APPEND,
	 }
};

static struct flash_platform_data bfin_spi_dataflash_data = {
	.name = "SPI Dataflash",
	.parts = bfin_spi_dataflash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_dataflash_partitions),
};

/* DataFlash chip */
static struct bfin5xx_spi_chip data_flash_chip_info = {
	.enable_dma = 0,	/* use dma transfer with this chip */
};
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	[0] = {
	       .start = SPI0_REGBASE,
	       .end = SPI0_REGBASE + 0xFF,
	       .flags = IORESOURCE_MEM,
	       },
	[1] = {
	       .start = CH_SPI,
	       .end = CH_SPI,
	       .flags = IORESOURCE_DMA,
	       },
	[2] = {
	       .start = IRQ_SPI,
	       .end = IRQ_SPI,
	       .flags = IORESOURCE_IRQ,
	       },
};

/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 8,
	.enable_dma = 1,	/* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct platform_device bfin_spi0_device = {
	.name = "bfin-spi",
	.id = 0,		/* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bfin_spi0_info,	/* Passed to driver */
		},
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if IS_ENABLED(CONFIG_SPI_SPIDEV)
	{
	 .modalias = "spidev",
	 .max_speed_hz = 3125000,	/* max spi clock (SCK) speed in HZ */
	 .bus_num = 0,
	 .chip_select = 3,
	 },
#endif
#if IS_ENABLED(CONFIG_MTD_DATAFLASH)
	{			/* DataFlash chip */
	 .modalias = "mtd_dataflash",
	 .max_speed_hz = 33250000,	/* max spi clock (SCK) speed in HZ */
	 .bus_num = 0,		/* Framework bus number */
	 .chip_select = 2,	/* Framework chip select */
	 .platform_data = &bfin_spi_dataflash_data,
	 .controller_data = &data_flash_chip_info,
	 .mode = SPI_MODE_3,
	 },
#endif
};

static struct resource bfin_gpios_resources = {
	.start = 31,
/*      .end   = MAX_BLACKFIN_GPIOS - 1, */
	.end = 32,
	.flags = IORESOURCE_IRQ,
};

static struct platform_device bfin_gpios_device = {
	.name = "simple-gpio",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_gpios_resources,
};

static const unsigned int cclk_vlev_datasheet[] = {
	VRPAIR(VLEV_085, 250000000),
	VRPAIR(VLEV_090, 300000000),
	VRPAIR(VLEV_095, 313000000),
	VRPAIR(VLEV_100, 350000000),
	VRPAIR(VLEV_105, 400000000),
	VRPAIR(VLEV_110, 444000000),
	VRPAIR(VLEV_115, 450000000),
	VRPAIR(VLEV_120, 475000000),
	VRPAIR(VLEV_125, 500000000),
	VRPAIR(VLEV_130, 600000000),
};

static struct bfin_dpmc_platform_data bfin_dmpc_vreg_data = {
	.tuple_tab = cclk_vlev_datasheet,
	.tabsize = ARRAY_SIZE(cclk_vlev_datasheet),
	.vr_settling_time = 25 /* us */ ,
};

static struct platform_device bfin_dpmc = {
	.name = "bfin dpmc",
	.dev = {
		.platform_data = &bfin_dmpc_vreg_data,
		},
};

static struct platform_device *acvilon_devices[] __initdata = {
	&bfin_dpmc,

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
	&bfin_spi0_device,
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif

	&bfin_gpios_device,

#if IS_ENABLED(CONFIG_SMSC911X)
	&smsc911x_device,
#endif

	&bfin_i2c_pca_device,

#if IS_ENABLED(CONFIG_MTD_NAND_PLATFORM)
	&bfin_async_nand_device,
#endif

#if IS_ENABLED(CONFIG_MTD_PLATRAM)
	&mtd_ram_device,
#endif

};

static int __init acvilon_init(void)
{
	int ret;

	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	bfin_plat_nand_init();
	ret =
	    platform_add_devices(acvilon_devices, ARRAY_SIZE(acvilon_devices));
	if (ret < 0)
		return ret;

	i2c_register_board_info(0, acvilon_i2c_devs,
				ARRAY_SIZE(acvilon_i2c_devs));

	bfin_write_FIO0_FLAG_C(1 << 14);
	msleep(5);
	bfin_write_FIO0_FLAG_S(1 << 14);

	spi_register_board_info(bfin_spi_board_info,
				ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(acvilon_init);

static struct platform_device *acvilon_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(acvilon_early_devices,
				   ARRAY_SIZE(acvilon_early_devices));
}
