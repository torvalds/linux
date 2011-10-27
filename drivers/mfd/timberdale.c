/*
 * timberdale.c timberdale FPGA MFD driver
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Timberdale FPGA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>

#include <linux/timb_gpio.h>

#include <linux/i2c.h>
#include <linux/i2c-ocores.h>
#include <linux/i2c-xiic.h>
#include <linux/i2c/tsc2007.h>

#include <linux/spi/spi.h>
#include <linux/spi/xilinx_spi.h>
#include <linux/spi/max7301.h>
#include <linux/spi/mc33880.h>

#include <media/timb_radio.h>
#include <media/timb_video.h>

#include <linux/timb_dma.h>

#include <linux/ks8842.h>

#include "timberdale.h"

#define DRIVER_NAME "timberdale"

struct timberdale_device {
	resource_size_t		ctl_mapbase;
	unsigned char __iomem   *ctl_membase;
	struct {
		u32 major;
		u32 minor;
		u32 config;
	} fw;
};

/*--------------------------------------------------------------------------*/

static struct tsc2007_platform_data timberdale_tsc2007_platform_data = {
	.model = 2003,
	.x_plate_ohms = 100
};

static struct i2c_board_info timberdale_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("tsc2007", 0x48),
		.platform_data = &timberdale_tsc2007_platform_data,
		.irq = IRQ_TIMBERDALE_TSC_INT
	},
};

static __devinitdata struct xiic_i2c_platform_data
timberdale_xiic_platform_data = {
	.devices = timberdale_i2c_board_info,
	.num_devices = ARRAY_SIZE(timberdale_i2c_board_info)
};

static __devinitdata struct ocores_i2c_platform_data
timberdale_ocores_platform_data = {
	.regstep = 4,
	.clock_khz = 62500,
	.devices = timberdale_i2c_board_info,
	.num_devices = ARRAY_SIZE(timberdale_i2c_board_info)
};

static const __devinitconst struct resource timberdale_xiic_resources[] = {
	{
		.start	= XIICOFFSET,
		.end	= XIICEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_I2C,
		.end	= IRQ_TIMBERDALE_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static const __devinitconst struct resource timberdale_ocores_resources[] = {
	{
		.start	= OCORESOFFSET,
		.end	= OCORESEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start 	= IRQ_TIMBERDALE_I2C,
		.end	= IRQ_TIMBERDALE_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

const struct max7301_platform_data timberdale_max7301_platform_data = {
	.base = 200
};

const struct mc33880_platform_data timberdale_mc33880_platform_data = {
	.base = 100
};

static struct spi_board_info timberdale_spi_16bit_board_info[] = {
	{
		.modalias = "max7301",
		.max_speed_hz = 26000,
		.chip_select = 2,
		.mode = SPI_MODE_0,
		.platform_data = &timberdale_max7301_platform_data
	},
};

static struct spi_board_info timberdale_spi_8bit_board_info[] = {
	{
		.modalias = "mc33880",
		.max_speed_hz = 4000,
		.chip_select = 1,
		.mode = SPI_MODE_1,
		.platform_data = &timberdale_mc33880_platform_data
	},
};

static __devinitdata struct xspi_platform_data timberdale_xspi_platform_data = {
	.num_chipselect = 3,
	.little_endian = true,
	/* bits per word and devices will be filled in runtime depending
	 * on the HW config
	 */
};

static const __devinitconst struct resource timberdale_spi_resources[] = {
	{
		.start 	= SPIOFFSET,
		.end	= SPIEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_SPI,
		.end	= IRQ_TIMBERDALE_SPI,
		.flags	= IORESOURCE_IRQ,
	},
};

static __devinitdata struct ks8842_platform_data
	timberdale_ks8842_platform_data = {
	.rx_dma_channel = DMA_ETH_RX,
	.tx_dma_channel = DMA_ETH_TX
};

static const __devinitconst struct resource timberdale_eth_resources[] = {
	{
		.start	= ETHOFFSET,
		.end	= ETHEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_ETHSW_IF,
		.end	= IRQ_TIMBERDALE_ETHSW_IF,
		.flags	= IORESOURCE_IRQ,
	},
};

static __devinitdata struct timbgpio_platform_data
	timberdale_gpio_platform_data = {
	.gpio_base = 0,
	.nr_pins = GPIO_NR_PINS,
	.irq_base = 200,
};

static const __devinitconst struct resource timberdale_gpio_resources[] = {
	{
		.start	= GPIOOFFSET,
		.end	= GPIOEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_GPIO,
		.end	= IRQ_TIMBERDALE_GPIO,
		.flags	= IORESOURCE_IRQ,
	},
};

static const __devinitconst struct resource timberdale_mlogicore_resources[] = {
	{
		.start	= MLCOREOFFSET,
		.end	= MLCOREEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_MLCORE,
		.end	= IRQ_TIMBERDALE_MLCORE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_TIMBERDALE_MLCORE_BUF,
		.end	= IRQ_TIMBERDALE_MLCORE_BUF,
		.flags	= IORESOURCE_IRQ,
	},
};

static const __devinitconst struct resource timberdale_uart_resources[] = {
	{
		.start	= UARTOFFSET,
		.end	= UARTEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_UART,
		.end	= IRQ_TIMBERDALE_UART,
		.flags	= IORESOURCE_IRQ,
	},
};

static const __devinitconst struct resource timberdale_uartlite_resources[] = {
	{
		.start	= UARTLITEOFFSET,
		.end	= UARTLITEEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_UARTLITE,
		.end	= IRQ_TIMBERDALE_UARTLITE,
		.flags	= IORESOURCE_IRQ,
	},
};

static __devinitdata struct i2c_board_info timberdale_adv7180_i2c_board_info = {
	/* Requires jumper JP9 to be off */
	I2C_BOARD_INFO("adv7180", 0x42 >> 1),
	.irq = IRQ_TIMBERDALE_ADV7180
};

static __devinitdata struct timb_video_platform_data
	timberdale_video_platform_data = {
	.dma_channel = DMA_VIDEO_RX,
	.i2c_adapter = 0,
	.encoder = {
		.info = &timberdale_adv7180_i2c_board_info
	}
};

static const __devinitconst struct resource
timberdale_radio_resources[] = {
	{
		.start	= RDSOFFSET,
		.end	= RDSEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_RDS,
		.end	= IRQ_TIMBERDALE_RDS,
		.flags	= IORESOURCE_IRQ,
	},
};

static __devinitdata struct i2c_board_info timberdale_tef6868_i2c_board_info = {
	I2C_BOARD_INFO("tef6862", 0x60)
};

static __devinitdata struct i2c_board_info timberdale_saa7706_i2c_board_info = {
	I2C_BOARD_INFO("saa7706h", 0x1C)
};

static __devinitdata struct timb_radio_platform_data
	timberdale_radio_platform_data = {
	.i2c_adapter = 0,
	.tuner = &timberdale_tef6868_i2c_board_info,
	.dsp = &timberdale_saa7706_i2c_board_info
};

static const __devinitconst struct resource timberdale_video_resources[] = {
	{
		.start	= LOGIWOFFSET,
		.end	= LOGIWEND,
		.flags	= IORESOURCE_MEM,
	},
	/*
	note that the "frame buffer" is located in DMA area
	starting at 0x1200000
	*/
};

static __devinitdata struct timb_dma_platform_data timb_dma_platform_data = {
	.nr_channels = 10,
	.channels = {
		{
			/* UART RX */
			.rx = true,
			.descriptors = 2,
			.descriptor_elements = 1
		},
		{
			/* UART TX */
			.rx = false,
			.descriptors = 2,
			.descriptor_elements = 1
		},
		{
			/* MLB RX */
			.rx = true,
			.descriptors = 2,
			.descriptor_elements = 1
		},
		{
			/* MLB TX */
			.rx = false,
			.descriptors = 2,
			.descriptor_elements = 1
		},
		{
			/* Video RX */
			.rx = true,
			.bytes_per_line = 1440,
			.descriptors = 2,
			.descriptor_elements = 16
		},
		{
			/* Video framedrop */
		},
		{
			/* SDHCI RX */
			.rx = true,
		},
		{
			/* SDHCI TX */
		},
		{
			/* ETH RX */
			.rx = true,
			.descriptors = 2,
			.descriptor_elements = 1
		},
		{
			/* ETH TX */
			.rx = false,
			.descriptors = 2,
			.descriptor_elements = 1
		},
	}
};

static const __devinitconst struct resource timberdale_dma_resources[] = {
	{
		.start	= DMAOFFSET,
		.end	= DMAEND,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_DMA,
		.end	= IRQ_TIMBERDALE_DMA,
		.flags	= IORESOURCE_IRQ,
	},
};

static __devinitdata struct mfd_cell timberdale_cells_bar0_cfg0[] = {
	{
		.name = "timb-dma",
		.num_resources = ARRAY_SIZE(timberdale_dma_resources),
		.resources = timberdale_dma_resources,
		.platform_data = &timb_dma_platform_data,
		.pdata_size = sizeof(timb_dma_platform_data),
	},
	{
		.name = "timb-uart",
		.num_resources = ARRAY_SIZE(timberdale_uart_resources),
		.resources = timberdale_uart_resources,
	},
	{
		.name = "xiic-i2c",
		.num_resources = ARRAY_SIZE(timberdale_xiic_resources),
		.resources = timberdale_xiic_resources,
		.platform_data = &timberdale_xiic_platform_data,
		.pdata_size = sizeof(timberdale_xiic_platform_data),
	},
	{
		.name = "timb-gpio",
		.num_resources = ARRAY_SIZE(timberdale_gpio_resources),
		.resources = timberdale_gpio_resources,
		.platform_data = &timberdale_gpio_platform_data,
		.pdata_size = sizeof(timberdale_gpio_platform_data),
	},
	{
		.name = "timb-video",
		.num_resources = ARRAY_SIZE(timberdale_video_resources),
		.resources = timberdale_video_resources,
		.platform_data = &timberdale_video_platform_data,
		.pdata_size = sizeof(timberdale_video_platform_data),
	},
	{
		.name = "timb-radio",
		.num_resources = ARRAY_SIZE(timberdale_radio_resources),
		.resources = timberdale_radio_resources,
		.platform_data = &timberdale_radio_platform_data,
		.pdata_size = sizeof(timberdale_radio_platform_data),
	},
	{
		.name = "xilinx_spi",
		.num_resources = ARRAY_SIZE(timberdale_spi_resources),
		.resources = timberdale_spi_resources,
		.platform_data = &timberdale_xspi_platform_data,
		.pdata_size = sizeof(timberdale_xspi_platform_data),
	},
	{
		.name = "ks8842",
		.num_resources = ARRAY_SIZE(timberdale_eth_resources),
		.resources = timberdale_eth_resources,
		.platform_data = &timberdale_ks8842_platform_data,
		.pdata_size = sizeof(timberdale_ks8842_platform_data),
	},
};

static __devinitdata struct mfd_cell timberdale_cells_bar0_cfg1[] = {
	{
		.name = "timb-dma",
		.num_resources = ARRAY_SIZE(timberdale_dma_resources),
		.resources = timberdale_dma_resources,
		.platform_data = &timb_dma_platform_data,
		.pdata_size = sizeof(timb_dma_platform_data),
	},
	{
		.name = "timb-uart",
		.num_resources = ARRAY_SIZE(timberdale_uart_resources),
		.resources = timberdale_uart_resources,
	},
	{
		.name = "uartlite",
		.num_resources = ARRAY_SIZE(timberdale_uartlite_resources),
		.resources = timberdale_uartlite_resources,
	},
	{
		.name = "xiic-i2c",
		.num_resources = ARRAY_SIZE(timberdale_xiic_resources),
		.resources = timberdale_xiic_resources,
		.platform_data = &timberdale_xiic_platform_data,
		.pdata_size = sizeof(timberdale_xiic_platform_data),
	},
	{
		.name = "timb-gpio",
		.num_resources = ARRAY_SIZE(timberdale_gpio_resources),
		.resources = timberdale_gpio_resources,
		.platform_data = &timberdale_gpio_platform_data,
		.pdata_size = sizeof(timberdale_gpio_platform_data),
	},
	{
		.name = "timb-mlogicore",
		.num_resources = ARRAY_SIZE(timberdale_mlogicore_resources),
		.resources = timberdale_mlogicore_resources,
	},
	{
		.name = "timb-video",
		.num_resources = ARRAY_SIZE(timberdale_video_resources),
		.resources = timberdale_video_resources,
		.platform_data = &timberdale_video_platform_data,
		.pdata_size = sizeof(timberdale_video_platform_data),
	},
	{
		.name = "timb-radio",
		.num_resources = ARRAY_SIZE(timberdale_radio_resources),
		.resources = timberdale_radio_resources,
		.platform_data = &timberdale_radio_platform_data,
		.pdata_size = sizeof(timberdale_radio_platform_data),
	},
	{
		.name = "xilinx_spi",
		.num_resources = ARRAY_SIZE(timberdale_spi_resources),
		.resources = timberdale_spi_resources,
		.platform_data = &timberdale_xspi_platform_data,
		.pdata_size = sizeof(timberdale_xspi_platform_data),
	},
	{
		.name = "ks8842",
		.num_resources = ARRAY_SIZE(timberdale_eth_resources),
		.resources = timberdale_eth_resources,
		.platform_data = &timberdale_ks8842_platform_data,
		.pdata_size = sizeof(timberdale_ks8842_platform_data),
	},
};

static __devinitdata struct mfd_cell timberdale_cells_bar0_cfg2[] = {
	{
		.name = "timb-dma",
		.num_resources = ARRAY_SIZE(timberdale_dma_resources),
		.resources = timberdale_dma_resources,
		.platform_data = &timb_dma_platform_data,
		.pdata_size = sizeof(timb_dma_platform_data),
	},
	{
		.name = "timb-uart",
		.num_resources = ARRAY_SIZE(timberdale_uart_resources),
		.resources = timberdale_uart_resources,
	},
	{
		.name = "xiic-i2c",
		.num_resources = ARRAY_SIZE(timberdale_xiic_resources),
		.resources = timberdale_xiic_resources,
		.platform_data = &timberdale_xiic_platform_data,
		.pdata_size = sizeof(timberdale_xiic_platform_data),
	},
	{
		.name = "timb-gpio",
		.num_resources = ARRAY_SIZE(timberdale_gpio_resources),
		.resources = timberdale_gpio_resources,
		.platform_data = &timberdale_gpio_platform_data,
		.pdata_size = sizeof(timberdale_gpio_platform_data),
	},
	{
		.name = "timb-video",
		.num_resources = ARRAY_SIZE(timberdale_video_resources),
		.resources = timberdale_video_resources,
		.platform_data = &timberdale_video_platform_data,
		.pdata_size = sizeof(timberdale_video_platform_data),
	},
	{
		.name = "timb-radio",
		.num_resources = ARRAY_SIZE(timberdale_radio_resources),
		.resources = timberdale_radio_resources,
		.platform_data = &timberdale_radio_platform_data,
		.pdata_size = sizeof(timberdale_radio_platform_data),
	},
	{
		.name = "xilinx_spi",
		.num_resources = ARRAY_SIZE(timberdale_spi_resources),
		.resources = timberdale_spi_resources,
		.platform_data = &timberdale_xspi_platform_data,
		.pdata_size = sizeof(timberdale_xspi_platform_data),
	},
};

static __devinitdata struct mfd_cell timberdale_cells_bar0_cfg3[] = {
	{
		.name = "timb-dma",
		.num_resources = ARRAY_SIZE(timberdale_dma_resources),
		.resources = timberdale_dma_resources,
		.platform_data = &timb_dma_platform_data,
		.pdata_size = sizeof(timb_dma_platform_data),
	},
	{
		.name = "timb-uart",
		.num_resources = ARRAY_SIZE(timberdale_uart_resources),
		.resources = timberdale_uart_resources,
	},
	{
		.name = "ocores-i2c",
		.num_resources = ARRAY_SIZE(timberdale_ocores_resources),
		.resources = timberdale_ocores_resources,
		.platform_data = &timberdale_ocores_platform_data,
		.pdata_size = sizeof(timberdale_ocores_platform_data),
	},
	{
		.name = "timb-gpio",
		.num_resources = ARRAY_SIZE(timberdale_gpio_resources),
		.resources = timberdale_gpio_resources,
		.platform_data = &timberdale_gpio_platform_data,
		.pdata_size = sizeof(timberdale_gpio_platform_data),
	},
	{
		.name = "timb-video",
		.num_resources = ARRAY_SIZE(timberdale_video_resources),
		.resources = timberdale_video_resources,
		.platform_data = &timberdale_video_platform_data,
		.pdata_size = sizeof(timberdale_video_platform_data),
	},
	{
		.name = "timb-radio",
		.num_resources = ARRAY_SIZE(timberdale_radio_resources),
		.resources = timberdale_radio_resources,
		.platform_data = &timberdale_radio_platform_data,
		.pdata_size = sizeof(timberdale_radio_platform_data),
	},
	{
		.name = "xilinx_spi",
		.num_resources = ARRAY_SIZE(timberdale_spi_resources),
		.resources = timberdale_spi_resources,
		.platform_data = &timberdale_xspi_platform_data,
		.pdata_size = sizeof(timberdale_xspi_platform_data),
	},
	{
		.name = "ks8842",
		.num_resources = ARRAY_SIZE(timberdale_eth_resources),
		.resources = timberdale_eth_resources,
		.platform_data = &timberdale_ks8842_platform_data,
		.pdata_size = sizeof(timberdale_ks8842_platform_data),
	},
};

static const __devinitconst struct resource timberdale_sdhc_resources[] = {
	/* located in bar 1 and bar 2 */
	{
		.start	= SDHC0OFFSET,
		.end	= SDHC0END,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_TIMBERDALE_SDHC,
		.end	= IRQ_TIMBERDALE_SDHC,
		.flags	= IORESOURCE_IRQ,
	},
};

static __devinitdata struct mfd_cell timberdale_cells_bar1[] = {
	{
		.name = "sdhci",
		.num_resources = ARRAY_SIZE(timberdale_sdhc_resources),
		.resources = timberdale_sdhc_resources,
	},
};

static __devinitdata struct mfd_cell timberdale_cells_bar2[] = {
	{
		.name = "sdhci",
		.num_resources = ARRAY_SIZE(timberdale_sdhc_resources),
		.resources = timberdale_sdhc_resources,
	},
};

static ssize_t show_fw_ver(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct timberdale_device *priv = pci_get_drvdata(pdev);

	return sprintf(buf, "%d.%d.%d\n", priv->fw.major, priv->fw.minor,
		priv->fw.config);
}

static DEVICE_ATTR(fw_ver, S_IRUGO, show_fw_ver, NULL);

/*--------------------------------------------------------------------------*/

static int __devinit timb_probe(struct pci_dev *dev,
	const struct pci_device_id *id)
{
	struct timberdale_device *priv;
	int err, i;
	resource_size_t mapbase;
	struct msix_entry *msix_entries = NULL;
	u8 ip_setup;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pci_set_drvdata(dev, priv);

	err = pci_enable_device(dev);
	if (err)
		goto err_enable;

	mapbase = pci_resource_start(dev, 0);
	if (!mapbase) {
		dev_err(&dev->dev, "No resource\n");
		goto err_start;
	}

	/* create a resource for the PCI master register */
	priv->ctl_mapbase = mapbase + CHIPCTLOFFSET;
	if (!request_mem_region(priv->ctl_mapbase, CHIPCTLSIZE, "timb-ctl")) {
		dev_err(&dev->dev, "Failed to request ctl mem\n");
		goto err_request;
	}

	priv->ctl_membase = ioremap(priv->ctl_mapbase, CHIPCTLSIZE);
	if (!priv->ctl_membase) {
		dev_err(&dev->dev, "ioremap failed for ctl mem\n");
		goto err_ioremap;
	}

	/* read the HW config */
	priv->fw.major = ioread32(priv->ctl_membase + TIMB_REV_MAJOR);
	priv->fw.minor = ioread32(priv->ctl_membase + TIMB_REV_MINOR);
	priv->fw.config = ioread32(priv->ctl_membase + TIMB_HW_CONFIG);

	if (priv->fw.major > TIMB_SUPPORTED_MAJOR) {
		dev_err(&dev->dev, "The driver supports an older "
			"version of the FPGA, please update the driver to "
			"support %d.%d\n", priv->fw.major, priv->fw.minor);
		goto err_ioremap;
	}
	if (priv->fw.major < TIMB_SUPPORTED_MAJOR ||
		priv->fw.minor < TIMB_REQUIRED_MINOR) {
		dev_err(&dev->dev, "The FPGA image is too old (%d.%d), "
			"please upgrade the FPGA to at least: %d.%d\n",
			priv->fw.major, priv->fw.minor,
			TIMB_SUPPORTED_MAJOR, TIMB_REQUIRED_MINOR);
		goto err_ioremap;
	}

	msix_entries = kzalloc(TIMBERDALE_NR_IRQS * sizeof(*msix_entries),
		GFP_KERNEL);
	if (!msix_entries)
		goto err_ioremap;

	for (i = 0; i < TIMBERDALE_NR_IRQS; i++)
		msix_entries[i].entry = i;

	err = pci_enable_msix(dev, msix_entries, TIMBERDALE_NR_IRQS);
	if (err) {
		dev_err(&dev->dev,
			"MSI-X init failed: %d, expected entries: %d\n",
			err, TIMBERDALE_NR_IRQS);
		goto err_msix;
	}

	err = device_create_file(&dev->dev, &dev_attr_fw_ver);
	if (err)
		goto err_create_file;

	/* Reset all FPGA PLB peripherals */
	iowrite32(0x1, priv->ctl_membase + TIMB_SW_RST);

	/* update IRQ offsets in I2C board info */
	for (i = 0; i < ARRAY_SIZE(timberdale_i2c_board_info); i++)
		timberdale_i2c_board_info[i].irq =
			msix_entries[timberdale_i2c_board_info[i].irq].vector;

	/* Update the SPI configuration depending on the HW (8 or 16 bit) */
	if (priv->fw.config & TIMB_HW_CONFIG_SPI_8BIT) {
		timberdale_xspi_platform_data.bits_per_word = 8;
		timberdale_xspi_platform_data.devices =
			timberdale_spi_8bit_board_info;
		timberdale_xspi_platform_data.num_devices =
			ARRAY_SIZE(timberdale_spi_8bit_board_info);
	} else {
		timberdale_xspi_platform_data.bits_per_word = 16;
		timberdale_xspi_platform_data.devices =
			timberdale_spi_16bit_board_info;
		timberdale_xspi_platform_data.num_devices =
			ARRAY_SIZE(timberdale_spi_16bit_board_info);
	}

	ip_setup = priv->fw.config & TIMB_HW_VER_MASK;
	switch (ip_setup) {
	case TIMB_HW_VER0:
		err = mfd_add_devices(&dev->dev, -1,
			timberdale_cells_bar0_cfg0,
			ARRAY_SIZE(timberdale_cells_bar0_cfg0),
			&dev->resource[0], msix_entries[0].vector);
		break;
	case TIMB_HW_VER1:
		err = mfd_add_devices(&dev->dev, -1,
			timberdale_cells_bar0_cfg1,
			ARRAY_SIZE(timberdale_cells_bar0_cfg1),
			&dev->resource[0], msix_entries[0].vector);
		break;
	case TIMB_HW_VER2:
		err = mfd_add_devices(&dev->dev, -1,
			timberdale_cells_bar0_cfg2,
			ARRAY_SIZE(timberdale_cells_bar0_cfg2),
			&dev->resource[0], msix_entries[0].vector);
		break;
	case TIMB_HW_VER3:
		err = mfd_add_devices(&dev->dev, -1,
			timberdale_cells_bar0_cfg3,
			ARRAY_SIZE(timberdale_cells_bar0_cfg3),
			&dev->resource[0], msix_entries[0].vector);
		break;
	default:
		dev_err(&dev->dev, "Uknown IP setup: %d.%d.%d\n",
			priv->fw.major, priv->fw.minor, ip_setup);
		err = -ENODEV;
		goto err_mfd;
		break;
	}

	if (err) {
		dev_err(&dev->dev, "mfd_add_devices failed: %d\n", err);
		goto err_mfd;
	}

	err = mfd_add_devices(&dev->dev, 0,
		timberdale_cells_bar1, ARRAY_SIZE(timberdale_cells_bar1),
		&dev->resource[1], msix_entries[0].vector);
	if (err) {
		dev_err(&dev->dev, "mfd_add_devices failed: %d\n", err);
		goto err_mfd2;
	}

	/* only version 0 and 3 have the iNand routed to SDHCI */
	if (((priv->fw.config & TIMB_HW_VER_MASK) == TIMB_HW_VER0) ||
		((priv->fw.config & TIMB_HW_VER_MASK) == TIMB_HW_VER3)) {
		err = mfd_add_devices(&dev->dev, 1, timberdale_cells_bar2,
			ARRAY_SIZE(timberdale_cells_bar2),
			&dev->resource[2], msix_entries[0].vector);
		if (err) {
			dev_err(&dev->dev, "mfd_add_devices failed: %d\n", err);
			goto err_mfd2;
		}
	}

	kfree(msix_entries);

	dev_info(&dev->dev,
		"Found Timberdale Card. Rev: %d.%d, HW config: 0x%02x\n",
		priv->fw.major, priv->fw.minor, priv->fw.config);

	return 0;

err_mfd2:
	mfd_remove_devices(&dev->dev);
err_mfd:
	device_remove_file(&dev->dev, &dev_attr_fw_ver);
err_create_file:
	pci_disable_msix(dev);
err_msix:
	iounmap(priv->ctl_membase);
err_ioremap:
	release_mem_region(priv->ctl_mapbase, CHIPCTLSIZE);
err_request:
	pci_set_drvdata(dev, NULL);
err_start:
	pci_disable_device(dev);
err_enable:
	kfree(msix_entries);
	kfree(priv);
	pci_set_drvdata(dev, NULL);
	return -ENODEV;
}

static void __devexit timb_remove(struct pci_dev *dev)
{
	struct timberdale_device *priv = pci_get_drvdata(dev);

	mfd_remove_devices(&dev->dev);

	device_remove_file(&dev->dev, &dev_attr_fw_ver);

	iounmap(priv->ctl_membase);
	release_mem_region(priv->ctl_mapbase, CHIPCTLSIZE);

	pci_disable_msix(dev);
	pci_disable_device(dev);
	pci_set_drvdata(dev, NULL);
	kfree(priv);
}

static struct pci_device_id timberdale_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TIMB, PCI_DEVICE_ID_TIMB) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, timberdale_pci_tbl);

static struct pci_driver timberdale_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = timberdale_pci_tbl,
	.probe = timb_probe,
	.remove = __devexit_p(timb_remove),
};

static int __init timberdale_init(void)
{
	int err;

	err = pci_register_driver(&timberdale_pci_driver);
	if (err < 0) {
		printk(KERN_ERR
			"Failed to register PCI driver for %s device.\n",
			timberdale_pci_driver.name);
		return -ENODEV;
	}

	printk(KERN_INFO "Driver for %s has been successfully registered.\n",
		timberdale_pci_driver.name);

	return 0;
}

static void __exit timberdale_exit(void)
{
	pci_unregister_driver(&timberdale_pci_driver);

	printk(KERN_INFO "Driver for %s has been successfully unregistered.\n",
		timberdale_pci_driver.name);
}

module_init(timberdale_init);
module_exit(timberdale_exit);

MODULE_AUTHOR("Mocean Laboratories <info@mocean-labs.com>");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL v2");
