	/*
 * arch/arm/mach-tegra/devices.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *	Erik Gilling <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/fsl_devices.h>
#include <asm/pmu.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dma.h>

static struct resource i2c_resource1[] = {
	[0] = {
		.start  = INT_I2C,
		.end    = INT_I2C,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_I2C_BASE,
		.end	= TEGRA_I2C_BASE + TEGRA_I2C_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource i2c_resource2[] = {
	[0] = {
		.start  = INT_I2C2,
		.end    = INT_I2C2,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_I2C2_BASE,
		.end	= TEGRA_I2C2_BASE + TEGRA_I2C2_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource i2c_resource3[] = {
	[0] = {
		.start  = INT_I2C3,
		.end    = INT_I2C3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_I2C3_BASE,
		.end	= TEGRA_I2C3_BASE + TEGRA_I2C3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource i2c_resource4[] = {
	[0] = {
		.start  = INT_DVC,
		.end    = INT_DVC,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_DVC_BASE,
		.end	= TEGRA_DVC_BASE + TEGRA_DVC_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device tegra_i2c_device1 = {
	.name		= "tegra-i2c",
	.id		= 0,
	.resource	= i2c_resource1,
	.num_resources	= ARRAY_SIZE(i2c_resource1),
	.dev = {
		.platform_data = 0,
	},
};

struct platform_device tegra_i2c_device2 = {
	.name		= "tegra-i2c",
	.id		= 1,
	.resource	= i2c_resource2,
	.num_resources	= ARRAY_SIZE(i2c_resource2),
	.dev = {
		.platform_data = 0,
	},
};

struct platform_device tegra_i2c_device3 = {
	.name		= "tegra-i2c",
	.id		= 2,
	.resource	= i2c_resource3,
	.num_resources	= ARRAY_SIZE(i2c_resource3),
	.dev = {
		.platform_data = 0,
	},
};

struct platform_device tegra_i2c_device4 = {
	.name		= "tegra-i2c",
	.id		= 3,
	.resource	= i2c_resource4,
	.num_resources	= ARRAY_SIZE(i2c_resource4),
	.dev = {
		.platform_data = 0,
	},
};

static struct resource spi_resource1[] = {
	[0] = {
		.start  = INT_S_LINK1,
		.end    = INT_S_LINK1,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SPI1_BASE,
		.end	= TEGRA_SPI1_BASE + TEGRA_SPI1_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource spi_resource2[] = {
	[0] = {
		.start  = INT_SPI_2,
		.end    = INT_SPI_2,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SPI2_BASE,
		.end	= TEGRA_SPI2_BASE + TEGRA_SPI2_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource spi_resource3[] = {
	[0] = {
		.start  = INT_SPI_3,
		.end    = INT_SPI_3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SPI3_BASE,
		.end	= TEGRA_SPI3_BASE + TEGRA_SPI3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource spi_resource4[] = {
	[0] = {
		.start  = INT_SPI_4,
		.end    = INT_SPI_4,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SPI4_BASE,
		.end	= TEGRA_SPI4_BASE + TEGRA_SPI4_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device tegra_spi_device1 = {
	.name           = "spi_tegra",
	.id             = 0,
	.resource       = spi_resource1,
	.num_resources  = ARRAY_SIZE(spi_resource1),
	.dev  = {
		.coherent_dma_mask      = 0xffffffff,
	},
};

struct platform_device tegra_spi_device2 = {
	.name           = "spi_tegra",
	.id             = 1,
	.resource       = spi_resource2,
	.num_resources  = ARRAY_SIZE(spi_resource2),
	.dev  = {
		.coherent_dma_mask      = 0xffffffff,
	},
};

struct platform_device tegra_spi_device3 = {
	.name           = "spi_tegra",
	.id             = 2,
	.resource       = spi_resource3,
	.num_resources  = ARRAY_SIZE(spi_resource3),
	.dev  = {
		.coherent_dma_mask      = 0xffffffff,
	},
};

struct platform_device tegra_spi_device4 = {
	.name           = "spi_tegra",
	.id             = 3,
	.resource       = spi_resource4,
	.num_resources  = ARRAY_SIZE(spi_resource4),
	.dev  = {
		.coherent_dma_mask      = 0xffffffff,
	},
};


static struct resource sdhci_resource1[] = {
	[0] = {
		.start  = INT_SDMMC1,
		.end    = INT_SDMMC1,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC1_BASE,
		.end	= TEGRA_SDMMC1_BASE + TEGRA_SDMMC1_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource2[] = {
	[0] = {
		.start  = INT_SDMMC2,
		.end    = INT_SDMMC2,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC2_BASE,
		.end	= TEGRA_SDMMC2_BASE + TEGRA_SDMMC2_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource3[] = {
	[0] = {
		.start  = INT_SDMMC3,
		.end    = INT_SDMMC3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC3_BASE,
		.end	= TEGRA_SDMMC3_BASE + TEGRA_SDMMC3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource4[] = {
	[0] = {
		.start  = INT_SDMMC4,
		.end    = INT_SDMMC4,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC4_BASE,
		.end	= TEGRA_SDMMC4_BASE + TEGRA_SDMMC4_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

/* board files should fill in platform_data register the devices themselvs.
 * See board-harmony.c for an example
 */
struct platform_device tegra_sdhci_device1 = {
	.name		= "sdhci-tegra",
	.id		= 0,
	.resource	= sdhci_resource1,
	.num_resources	= ARRAY_SIZE(sdhci_resource1),
};

struct platform_device tegra_sdhci_device2 = {
	.name		= "sdhci-tegra",
	.id		= 1,
	.resource	= sdhci_resource2,
	.num_resources	= ARRAY_SIZE(sdhci_resource2),
};

struct platform_device tegra_sdhci_device3 = {
	.name		= "sdhci-tegra",
	.id		= 2,
	.resource	= sdhci_resource3,
	.num_resources	= ARRAY_SIZE(sdhci_resource3),
};

struct platform_device tegra_sdhci_device4 = {
	.name		= "sdhci-tegra",
	.id		= 3,
	.resource	= sdhci_resource4,
	.num_resources	= ARRAY_SIZE(sdhci_resource4),
};

static struct resource w1_resources[] = {
	[0] = {
		.start = INT_OWR,
		.end   = INT_OWR,
		.flags = IORESOURCE_IRQ
	},
	[1] = {
		.start = TEGRA_OWR_BASE,
		.end = TEGRA_OWR_BASE + TEGRA_OWR_SIZE - 1,
		.flags = IORESOURCE_MEM
	}
};

struct platform_device tegra_w1_device = {
	.name          = "tegra_w1",
	.id            = -1,
	.resource      = w1_resources,
	.num_resources = ARRAY_SIZE(w1_resources),
};

static struct resource tegra_udc_resources[] = {
	[0] = {
		.start	= TEGRA_USB_BASE,
		.end	= TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_USB,
		.end	= INT_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource tegra_usb1_resources[] = {
	[0] = {
		.start	= TEGRA_USB_BASE,
		.end	= TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_USB,
		.end	= INT_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource tegra_usb2_resources[] = {
	[0] = {
		.start	= TEGRA_USB2_BASE,
		.end	= TEGRA_USB2_BASE + TEGRA_USB2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_USB2,
		.end	= INT_USB2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource tegra_usb3_resources[] = {
	[0] = {
		.start	= TEGRA_USB3_BASE,
		.end	= TEGRA_USB3_BASE + TEGRA_USB3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_USB3,
		.end	= INT_USB3,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 tegra_udc_dmamask = DMA_BIT_MASK(32);

static struct fsl_usb2_platform_data tegra_udc_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI,
};

struct platform_device tegra_udc_device = {
	.name	= "fsl-tegra-udc",
	.id	= -1,
	.dev	= {
		.dma_mask	= &tegra_udc_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data	= &tegra_udc_pdata,
	},
	.resource = tegra_udc_resources,
	.num_resources = ARRAY_SIZE(tegra_udc_resources),
};

static u64 tegra_ehci_dmamask = DMA_BIT_MASK(32);

struct platform_device tegra_ehci1_device = {
	.name	= "tegra-ehci",
	.id	= 0,
	.dev	= {
		.dma_mask	= &tegra_ehci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = tegra_usb1_resources,
	.num_resources = ARRAY_SIZE(tegra_usb1_resources),
};

struct platform_device tegra_ehci2_device = {
	.name	= "tegra-ehci",
	.id	= 1,
	.dev	= {
		.dma_mask	= &tegra_ehci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = tegra_usb2_resources,
	.num_resources = ARRAY_SIZE(tegra_usb2_resources),
};

struct platform_device tegra_ehci3_device = {
	.name	= "tegra-ehci",
	.id	= 2,
	.dev	= {
		.dma_mask	= &tegra_ehci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = tegra_usb3_resources,
	.num_resources = ARRAY_SIZE(tegra_usb3_resources),
};

static struct resource i2s_resource1[] = {
	[0] = {
		.start	= INT_I2S1,
		.end	= INT_I2S1,
		.flags	= IORESOURCE_IRQ
	},
	[1] = {
		.start	= TEGRA_DMA_REQ_SEL_I2S_1,
		.end	= TEGRA_DMA_REQ_SEL_I2S_1,
		.flags	= IORESOURCE_DMA
	},
	[2] = {
		.start	= TEGRA_I2S1_BASE,
		.end	= TEGRA_I2S1_BASE + TEGRA_I2S1_SIZE - 1,
		.flags	= IORESOURCE_MEM
	}
};

static struct resource i2s_resource2[] = {
	[0] = {
		.start	= INT_I2S2,
		.end	= INT_I2S2,
		.flags	= IORESOURCE_IRQ
	},
	[1] = {
		.start	= TEGRA_DMA_REQ_SEL_I2S_2,
		.end	= TEGRA_DMA_REQ_SEL_I2S_2,
		.flags	= IORESOURCE_DMA
	},
	[2] = {
		.start	= TEGRA_I2S2_BASE,
		.end	= TEGRA_I2S2_BASE + TEGRA_I2S2_SIZE - 1,
		.flags	= IORESOURCE_MEM
	}
};

struct platform_device tegra_i2s_device1 = {
	.name		= "i2s",
	.id		= 0,
	.resource	= i2s_resource1,
	.num_resources	= ARRAY_SIZE(i2s_resource1),
};

struct platform_device tegra_i2s_device2 = {
	.name		= "i2s",
	.id		= 1,
	.resource	= i2s_resource2,
	.num_resources	= ARRAY_SIZE(i2s_resource2),
};

static struct resource tegra_gart_resources[] = {
	[0] = {
		.name	= "mc",
		.flags	= IORESOURCE_MEM,
		.start	= TEGRA_MC_BASE,
		.end	= TEGRA_MC_BASE + TEGRA_MC_SIZE - 1,
	},
	[1] = {
		.name	= "gart",
		.flags	= IORESOURCE_MEM,
		.start	= TEGRA_GART_BASE,
		.end	= TEGRA_GART_BASE + TEGRA_GART_SIZE - 1,
	}
};

struct platform_device tegra_gart_device = {
	.name		= "tegra_gart",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(tegra_gart_resources),
	.resource	= tegra_gart_resources
};

static struct resource pmu_resources[] = {
	[0] = {
		.start	= INT_CPU0_PMU_INTR,
		.end	= INT_CPU0_PMU_INTR,
		.flags	= IORESOURCE_IRQ,
	},
	[1] = {
		.start	= INT_CPU1_PMU_INTR,
		.end	= INT_CPU1_PMU_INTR,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pmu_device = {
	.name		= "arm-pmu",
	.id		= ARM_PMU_DEVICE_CPU,
	.num_resources	= ARRAY_SIZE(pmu_resources),
	.resource	= pmu_resources,
};
