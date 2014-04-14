/*
 * DA8XX/OMAP L1XX platform device data
 *
 * Copyright (c) 2007-2009, MontaVista Software, Inc. <source@mvista.com>
 * Derived from code that was:
 *	Copyright (C) 2006 Komal Shah <komal_shah802003@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-contiguous.h>
#include <linux/serial_8250.h>
#include <linux/ahci_platform.h>
#include <linux/clk.h>
#include <linux/reboot.h>

#include <mach/cputype.h>
#include <mach/common.h>
#include <mach/time.h>
#include <mach/da8xx.h>
#include <mach/cpuidle.h>
#include <mach/sram.h>

#include "clock.h"
#include "asp.h"

#define DA8XX_TPCC_BASE			0x01c00000
#define DA8XX_TPTC0_BASE		0x01c08000
#define DA8XX_TPTC1_BASE		0x01c08400
#define DA8XX_WDOG_BASE			0x01c21000 /* DA8XX_TIMER64P1_BASE */
#define DA8XX_I2C0_BASE			0x01c22000
#define DA8XX_RTC_BASE			0x01c23000
#define DA8XX_PRUSS_MEM_BASE		0x01c30000
#define DA8XX_MMCSD0_BASE		0x01c40000
#define DA8XX_SPI0_BASE			0x01c41000
#define DA830_SPI1_BASE			0x01e12000
#define DA8XX_LCD_CNTRL_BASE		0x01e13000
#define DA850_SATA_BASE			0x01e18000
#define DA850_MMCSD1_BASE		0x01e1b000
#define DA8XX_EMAC_CPPI_PORT_BASE	0x01e20000
#define DA8XX_EMAC_CPGMACSS_BASE	0x01e22000
#define DA8XX_EMAC_CPGMAC_BASE		0x01e23000
#define DA8XX_EMAC_MDIO_BASE		0x01e24000
#define DA8XX_I2C1_BASE			0x01e28000
#define DA850_TPCC1_BASE		0x01e30000
#define DA850_TPTC2_BASE		0x01e38000
#define DA850_SPI1_BASE			0x01f0e000
#define DA8XX_DDR2_CTL_BASE		0xb0000000

#define DA8XX_EMAC_CTRL_REG_OFFSET	0x3000
#define DA8XX_EMAC_MOD_REG_OFFSET	0x2000
#define DA8XX_EMAC_RAM_OFFSET		0x0000
#define DA8XX_EMAC_CTRL_RAM_SIZE	SZ_8K

#define DA8XX_DMA_SPI0_RX	EDMA_CTLR_CHAN(0, 14)
#define DA8XX_DMA_SPI0_TX	EDMA_CTLR_CHAN(0, 15)
#define DA8XX_DMA_MMCSD0_RX	EDMA_CTLR_CHAN(0, 16)
#define DA8XX_DMA_MMCSD0_TX	EDMA_CTLR_CHAN(0, 17)
#define DA8XX_DMA_SPI1_RX	EDMA_CTLR_CHAN(0, 18)
#define DA8XX_DMA_SPI1_TX	EDMA_CTLR_CHAN(0, 19)
#define DA850_DMA_MMCSD1_RX	EDMA_CTLR_CHAN(1, 28)
#define DA850_DMA_MMCSD1_TX	EDMA_CTLR_CHAN(1, 29)

void __iomem *da8xx_syscfg0_base;
void __iomem *da8xx_syscfg1_base;

static struct plat_serial8250_port da8xx_serial0_pdata[] = {
	{
		.mapbase	= DA8XX_UART0_BASE,
		.irq		= IRQ_DA8XX_UARTINT0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
					UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};
static struct plat_serial8250_port da8xx_serial1_pdata[] = {
	{
		.mapbase	= DA8XX_UART1_BASE,
		.irq		= IRQ_DA8XX_UARTINT1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
					UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};
static struct plat_serial8250_port da8xx_serial2_pdata[] = {
	{
		.mapbase	= DA8XX_UART2_BASE,
		.irq		= IRQ_DA8XX_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
					UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};

struct platform_device da8xx_serial_device[] = {
	{
		.name	= "serial8250",
		.id	= PLAT8250_DEV_PLATFORM,
		.dev	= {
			.platform_data	= da8xx_serial0_pdata,
		}
	},
	{
		.name	= "serial8250",
		.id	= PLAT8250_DEV_PLATFORM1,
		.dev	= {
			.platform_data	= da8xx_serial1_pdata,
		}
	},
	{
		.name	= "serial8250",
		.id	= PLAT8250_DEV_PLATFORM2,
		.dev	= {
			.platform_data	= da8xx_serial2_pdata,
		}
	},
	{
	}
};

static s8 da8xx_queue_tc_mapping[][2] = {
	/* {event queue no, TC no} */
	{0, 0},
	{1, 1},
	{-1, -1}
};

static s8 da8xx_queue_priority_mapping[][2] = {
	/* {event queue no, Priority} */
	{0, 3},
	{1, 7},
	{-1, -1}
};

static s8 da850_queue_tc_mapping[][2] = {
	/* {event queue no, TC no} */
	{0, 0},
	{-1, -1}
};

static s8 da850_queue_priority_mapping[][2] = {
	/* {event queue no, Priority} */
	{0, 3},
	{-1, -1}
};

static struct edma_soc_info da830_edma_cc0_info = {
	.n_channel		= 32,
	.n_region		= 4,
	.n_slot			= 128,
	.n_tc			= 2,
	.n_cc			= 1,
	.queue_tc_mapping	= da8xx_queue_tc_mapping,
	.queue_priority_mapping	= da8xx_queue_priority_mapping,
	.default_queue		= EVENTQ_1,
};

static struct edma_soc_info *da830_edma_info[EDMA_MAX_CC] = {
	&da830_edma_cc0_info,
};

static struct edma_soc_info da850_edma_cc_info[] = {
	{
		.n_channel		= 32,
		.n_region		= 4,
		.n_slot			= 128,
		.n_tc			= 2,
		.n_cc			= 1,
		.queue_tc_mapping	= da8xx_queue_tc_mapping,
		.queue_priority_mapping	= da8xx_queue_priority_mapping,
		.default_queue		= EVENTQ_1,
	},
	{
		.n_channel		= 32,
		.n_region		= 4,
		.n_slot			= 128,
		.n_tc			= 1,
		.n_cc			= 1,
		.queue_tc_mapping	= da850_queue_tc_mapping,
		.queue_priority_mapping	= da850_queue_priority_mapping,
		.default_queue		= EVENTQ_0,
	},
};

static struct edma_soc_info *da850_edma_info[EDMA_MAX_CC] = {
	&da850_edma_cc_info[0],
	&da850_edma_cc_info[1],
};

static struct resource da830_edma_resources[] = {
	{
		.name	= "edma_cc0",
		.start	= DA8XX_TPCC_BASE,
		.end	= DA8XX_TPCC_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc0",
		.start	= DA8XX_TPTC0_BASE,
		.end	= DA8XX_TPTC0_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc1",
		.start	= DA8XX_TPTC1_BASE,
		.end	= DA8XX_TPTC1_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma0",
		.start	= IRQ_DA8XX_CCINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma0_err",
		.start	= IRQ_DA8XX_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource da850_edma_resources[] = {
	{
		.name	= "edma_cc0",
		.start	= DA8XX_TPCC_BASE,
		.end	= DA8XX_TPCC_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc0",
		.start	= DA8XX_TPTC0_BASE,
		.end	= DA8XX_TPTC0_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc1",
		.start	= DA8XX_TPTC1_BASE,
		.end	= DA8XX_TPTC1_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_cc1",
		.start	= DA850_TPCC1_BASE,
		.end	= DA850_TPCC1_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc2",
		.start	= DA850_TPTC2_BASE,
		.end	= DA850_TPTC2_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma0",
		.start	= IRQ_DA8XX_CCINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma0_err",
		.start	= IRQ_DA8XX_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma1",
		.start	= IRQ_DA850_CCINT1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma1_err",
		.start	= IRQ_DA850_CCERRINT1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da830_edma_device = {
	.name		= "edma",
	.id		= -1,
	.dev = {
		.platform_data = da830_edma_info,
	},
	.num_resources	= ARRAY_SIZE(da830_edma_resources),
	.resource	= da830_edma_resources,
};

static struct platform_device da850_edma_device = {
	.name		= "edma",
	.id		= -1,
	.dev = {
		.platform_data = da850_edma_info,
	},
	.num_resources	= ARRAY_SIZE(da850_edma_resources),
	.resource	= da850_edma_resources,
};

int __init da830_register_edma(struct edma_rsv_info *rsv)
{
	da830_edma_cc0_info.rsv = rsv;

	return platform_device_register(&da830_edma_device);
}

int __init da850_register_edma(struct edma_rsv_info *rsv[2])
{
	if (rsv) {
		da850_edma_cc_info[0].rsv = rsv[0];
		da850_edma_cc_info[1].rsv = rsv[1];
	}

	return platform_device_register(&da850_edma_device);
}

static struct resource da8xx_i2c_resources0[] = {
	{
		.start	= DA8XX_I2C0_BASE,
		.end	= DA8XX_I2C0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA8XX_I2CINT0,
		.end	= IRQ_DA8XX_I2CINT0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da8xx_i2c_device0 = {
	.name		= "i2c_davinci",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(da8xx_i2c_resources0),
	.resource	= da8xx_i2c_resources0,
};

static struct resource da8xx_i2c_resources1[] = {
	{
		.start	= DA8XX_I2C1_BASE,
		.end	= DA8XX_I2C1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA8XX_I2CINT1,
		.end	= IRQ_DA8XX_I2CINT1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da8xx_i2c_device1 = {
	.name		= "i2c_davinci",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(da8xx_i2c_resources1),
	.resource	= da8xx_i2c_resources1,
};

int __init da8xx_register_i2c(int instance,
		struct davinci_i2c_platform_data *pdata)
{
	struct platform_device *pdev;

	if (instance == 0)
		pdev = &da8xx_i2c_device0;
	else if (instance == 1)
		pdev = &da8xx_i2c_device1;
	else
		return -EINVAL;

	pdev->dev.platform_data = pdata;
	return platform_device_register(pdev);
}

static struct resource da8xx_watchdog_resources[] = {
	{
		.start	= DA8XX_WDOG_BASE,
		.end	= DA8XX_WDOG_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device da8xx_wdt_device = {
	.name		= "davinci-wdt",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(da8xx_watchdog_resources),
	.resource	= da8xx_watchdog_resources,
};

void da8xx_restart(enum reboot_mode mode, const char *cmd)
{
	struct device *dev;

	dev = bus_find_device_by_name(&platform_bus_type, NULL, "davinci-wdt");
	if (!dev) {
		pr_err("%s: failed to find watchdog device\n", __func__);
		return;
	}

	davinci_watchdog_reset(to_platform_device(dev));
}

int __init da8xx_register_watchdog(void)
{
	return platform_device_register(&da8xx_wdt_device);
}

static struct resource da8xx_emac_resources[] = {
	{
		.start	= DA8XX_EMAC_CPPI_PORT_BASE,
		.end	= DA8XX_EMAC_CPPI_PORT_BASE + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA8XX_C0_RX_THRESH_PULSE,
		.end	= IRQ_DA8XX_C0_RX_THRESH_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_C0_RX_PULSE,
		.end	= IRQ_DA8XX_C0_RX_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_C0_TX_PULSE,
		.end	= IRQ_DA8XX_C0_TX_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_C0_MISC_PULSE,
		.end	= IRQ_DA8XX_C0_MISC_PULSE,
		.flags	= IORESOURCE_IRQ,
	},
};

struct emac_platform_data da8xx_emac_pdata = {
	.ctrl_reg_offset	= DA8XX_EMAC_CTRL_REG_OFFSET,
	.ctrl_mod_reg_offset	= DA8XX_EMAC_MOD_REG_OFFSET,
	.ctrl_ram_offset	= DA8XX_EMAC_RAM_OFFSET,
	.ctrl_ram_size		= DA8XX_EMAC_CTRL_RAM_SIZE,
	.version		= EMAC_VERSION_2,
};

static struct platform_device da8xx_emac_device = {
	.name		= "davinci_emac",
	.id		= 1,
	.dev = {
		.platform_data	= &da8xx_emac_pdata,
	},
	.num_resources	= ARRAY_SIZE(da8xx_emac_resources),
	.resource	= da8xx_emac_resources,
};

static struct resource da8xx_mdio_resources[] = {
	{
		.start	= DA8XX_EMAC_MDIO_BASE,
		.end	= DA8XX_EMAC_MDIO_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device da8xx_mdio_device = {
	.name		= "davinci_mdio",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(da8xx_mdio_resources),
	.resource	= da8xx_mdio_resources,
};

int __init da8xx_register_emac(void)
{
	int ret;

	ret = platform_device_register(&da8xx_mdio_device);
	if (ret < 0)
		return ret;

	return platform_device_register(&da8xx_emac_device);
}

static struct resource da830_mcasp1_resources[] = {
	{
		.name	= "mpu",
		.start	= DAVINCI_DA830_MCASP1_REG_BASE,
		.end	= DAVINCI_DA830_MCASP1_REG_BASE + (SZ_1K * 12) - 1,
		.flags	= IORESOURCE_MEM,
	},
	/* TX event */
	{
		.start	= DAVINCI_DA830_DMA_MCASP1_AXEVT,
		.end	= DAVINCI_DA830_DMA_MCASP1_AXEVT,
		.flags	= IORESOURCE_DMA,
	},
	/* RX event */
	{
		.start	= DAVINCI_DA830_DMA_MCASP1_AREVT,
		.end	= DAVINCI_DA830_DMA_MCASP1_AREVT,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device da830_mcasp1_device = {
	.name		= "davinci-mcasp",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(da830_mcasp1_resources),
	.resource	= da830_mcasp1_resources,
};

static struct resource da850_mcasp_resources[] = {
	{
		.name	= "mpu",
		.start	= DAVINCI_DA8XX_MCASP0_REG_BASE,
		.end	= DAVINCI_DA8XX_MCASP0_REG_BASE + (SZ_1K * 12) - 1,
		.flags	= IORESOURCE_MEM,
	},
	/* TX event */
	{
		.start	= DAVINCI_DA8XX_DMA_MCASP0_AXEVT,
		.end	= DAVINCI_DA8XX_DMA_MCASP0_AXEVT,
		.flags	= IORESOURCE_DMA,
	},
	/* RX event */
	{
		.start	= DAVINCI_DA8XX_DMA_MCASP0_AREVT,
		.end	= DAVINCI_DA8XX_DMA_MCASP0_AREVT,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device da850_mcasp_device = {
	.name		= "davinci-mcasp",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(da850_mcasp_resources),
	.resource	= da850_mcasp_resources,
};

void __init da8xx_register_mcasp(int id, struct snd_platform_data *pdata)
{
	/* DA830/OMAP-L137 has 3 instances of McASP */
	if (cpu_is_davinci_da830() && id == 1) {
		da830_mcasp1_device.dev.platform_data = pdata;
		platform_device_register(&da830_mcasp1_device);
	} else if (cpu_is_davinci_da850()) {
		da850_mcasp_device.dev.platform_data = pdata;
		platform_device_register(&da850_mcasp_device);
	}
}

static struct resource da8xx_pruss_resources[] = {
	{
		.start	= DA8XX_PRUSS_MEM_BASE,
		.end	= DA8XX_PRUSS_MEM_BASE + 0xFFFF,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT0,
		.end	= IRQ_DA8XX_EVTOUT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT1,
		.end	= IRQ_DA8XX_EVTOUT1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT2,
		.end	= IRQ_DA8XX_EVTOUT2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT3,
		.end	= IRQ_DA8XX_EVTOUT3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT4,
		.end	= IRQ_DA8XX_EVTOUT4,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT5,
		.end	= IRQ_DA8XX_EVTOUT5,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT6,
		.end	= IRQ_DA8XX_EVTOUT6,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DA8XX_EVTOUT7,
		.end	= IRQ_DA8XX_EVTOUT7,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct uio_pruss_pdata da8xx_uio_pruss_pdata = {
	.pintc_base	= 0x4000,
};

static struct platform_device da8xx_uio_pruss_dev = {
	.name		= "pruss_uio",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(da8xx_pruss_resources),
	.resource	= da8xx_pruss_resources,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &da8xx_uio_pruss_pdata,
	}
};

int __init da8xx_register_uio_pruss(void)
{
	da8xx_uio_pruss_pdata.sram_pool = sram_get_gen_pool();
	return platform_device_register(&da8xx_uio_pruss_dev);
}

static struct lcd_ctrl_config lcd_cfg = {
	.panel_shade		= COLOR_ACTIVE,
	.bpp			= 16,
};

struct da8xx_lcdc_platform_data sharp_lcd035q3dg01_pdata = {
	.manu_name		= "sharp",
	.controller_data	= &lcd_cfg,
	.type			= "Sharp_LCD035Q3DG01",
};

struct da8xx_lcdc_platform_data sharp_lk043t1dg01_pdata = {
	.manu_name		= "sharp",
	.controller_data	= &lcd_cfg,
	.type			= "Sharp_LK043T1DG01",
};

static struct resource da8xx_lcdc_resources[] = {
	[0] = { /* registers */
		.start  = DA8XX_LCD_CNTRL_BASE,
		.end    = DA8XX_LCD_CNTRL_BASE + SZ_4K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = { /* interrupt */
		.start  = IRQ_DA8XX_LCDINT,
		.end    = IRQ_DA8XX_LCDINT,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device da8xx_lcdc_device = {
	.name		= "da8xx_lcdc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(da8xx_lcdc_resources),
	.resource	= da8xx_lcdc_resources,
};

int __init da8xx_register_lcdc(struct da8xx_lcdc_platform_data *pdata)
{
	da8xx_lcdc_device.dev.platform_data = pdata;
	return platform_device_register(&da8xx_lcdc_device);
}

static struct resource da8xx_gpio_resources[] = {
	{ /* registers */
		.start	= DA8XX_GPIO_BASE,
		.end	= DA8XX_GPIO_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{ /* interrupt */
		.start	= IRQ_DA8XX_GPIO0,
		.end	= IRQ_DA8XX_GPIO8,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device da8xx_gpio_device = {
	.name		= "davinci_gpio",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(da8xx_gpio_resources),
	.resource	= da8xx_gpio_resources,
};

int __init da8xx_register_gpio(void *pdata)
{
	da8xx_gpio_device.dev.platform_data = pdata;
	return platform_device_register(&da8xx_gpio_device);
}

static struct resource da8xx_mmcsd0_resources[] = {
	{		/* registers */
		.start	= DA8XX_MMCSD0_BASE,
		.end	= DA8XX_MMCSD0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{		/* interrupt */
		.start	= IRQ_DA8XX_MMCSDINT0,
		.end	= IRQ_DA8XX_MMCSDINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{		/* DMA RX */
		.start	= DA8XX_DMA_MMCSD0_RX,
		.end	= DA8XX_DMA_MMCSD0_RX,
		.flags	= IORESOURCE_DMA,
	},
	{		/* DMA TX */
		.start	= DA8XX_DMA_MMCSD0_TX,
		.end	= DA8XX_DMA_MMCSD0_TX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device da8xx_mmcsd0_device = {
	.name		= "da830-mmc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(da8xx_mmcsd0_resources),
	.resource	= da8xx_mmcsd0_resources,
};

int __init da8xx_register_mmcsd0(struct davinci_mmc_config *config)
{
	da8xx_mmcsd0_device.dev.platform_data = config;
	return platform_device_register(&da8xx_mmcsd0_device);
}

#ifdef CONFIG_ARCH_DAVINCI_DA850
static struct resource da850_mmcsd1_resources[] = {
	{		/* registers */
		.start	= DA850_MMCSD1_BASE,
		.end	= DA850_MMCSD1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{		/* interrupt */
		.start	= IRQ_DA850_MMCSDINT0_1,
		.end	= IRQ_DA850_MMCSDINT0_1,
		.flags	= IORESOURCE_IRQ,
	},
	{		/* DMA RX */
		.start	= DA850_DMA_MMCSD1_RX,
		.end	= DA850_DMA_MMCSD1_RX,
		.flags	= IORESOURCE_DMA,
	},
	{		/* DMA TX */
		.start	= DA850_DMA_MMCSD1_TX,
		.end	= DA850_DMA_MMCSD1_TX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device da850_mmcsd1_device = {
	.name		= "da830-mmc",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(da850_mmcsd1_resources),
	.resource	= da850_mmcsd1_resources,
};

int __init da850_register_mmcsd1(struct davinci_mmc_config *config)
{
	da850_mmcsd1_device.dev.platform_data = config;
	return platform_device_register(&da850_mmcsd1_device);
}
#endif

static struct resource da8xx_rproc_resources[] = {
	{ /* DSP boot address */
		.start		= DA8XX_SYSCFG0_BASE + DA8XX_HOST1CFG_REG,
		.end		= DA8XX_SYSCFG0_BASE + DA8XX_HOST1CFG_REG + 3,
		.flags		= IORESOURCE_MEM,
	},
	{ /* DSP interrupt registers */
		.start		= DA8XX_SYSCFG0_BASE + DA8XX_CHIPSIG_REG,
		.end		= DA8XX_SYSCFG0_BASE + DA8XX_CHIPSIG_REG + 7,
		.flags		= IORESOURCE_MEM,
	},
	{ /* dsp irq */
		.start		= IRQ_DA8XX_CHIPINT0,
		.end		= IRQ_DA8XX_CHIPINT0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device da8xx_dsp = {
	.name	= "davinci-rproc",
	.dev	= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(da8xx_rproc_resources),
	.resource	= da8xx_rproc_resources,
};

#if IS_ENABLED(CONFIG_DA8XX_REMOTEPROC)

static phys_addr_t rproc_base __initdata;
static unsigned long rproc_size __initdata;

static int __init early_rproc_mem(char *p)
{
	char *endp;

	if (p == NULL)
		return 0;

	rproc_size = memparse(p, &endp);
	if (*endp == '@')
		rproc_base = memparse(endp + 1, NULL);

	return 0;
}
early_param("rproc_mem", early_rproc_mem);

void __init da8xx_rproc_reserve_cma(void)
{
	int ret;

	if (!rproc_base || !rproc_size) {
		pr_err("%s: 'rproc_mem=nn@address' badly specified\n"
		       "    'nn' and 'address' must both be non-zero\n",
		       __func__);

		return;
	}

	pr_info("%s: reserving 0x%lx @ 0x%lx...\n",
		__func__, rproc_size, (unsigned long)rproc_base);

	ret = dma_declare_contiguous(&da8xx_dsp.dev, rproc_size, rproc_base, 0);
	if (ret)
		pr_err("%s: dma_declare_contiguous failed %d\n", __func__, ret);
}

#else

void __init da8xx_rproc_reserve_cma(void)
{
}

#endif

int __init da8xx_register_rproc(void)
{
	int ret;

	ret = platform_device_register(&da8xx_dsp);
	if (ret)
		pr_err("%s: can't register DSP device: %d\n", __func__, ret);

	return ret;
};

static struct resource da8xx_rtc_resources[] = {
	{
		.start		= DA8XX_RTC_BASE,
		.end		= DA8XX_RTC_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
	{ /* timer irq */
		.start		= IRQ_DA8XX_RTC,
		.end		= IRQ_DA8XX_RTC,
		.flags		= IORESOURCE_IRQ,
	},
	{ /* alarm irq */
		.start		= IRQ_DA8XX_RTC,
		.end		= IRQ_DA8XX_RTC,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device da8xx_rtc_device = {
	.name           = "da830-rtc",
	.id             = -1,
	.num_resources	= ARRAY_SIZE(da8xx_rtc_resources),
	.resource	= da8xx_rtc_resources,
};

int da8xx_register_rtc(void)
{
	return platform_device_register(&da8xx_rtc_device);
}

static void __iomem *da8xx_ddr2_ctlr_base;
void __iomem * __init da8xx_get_mem_ctlr(void)
{
	if (da8xx_ddr2_ctlr_base)
		return da8xx_ddr2_ctlr_base;

	da8xx_ddr2_ctlr_base = ioremap(DA8XX_DDR2_CTL_BASE, SZ_32K);
	if (!da8xx_ddr2_ctlr_base)
		pr_warn("%s: Unable to map DDR2 controller", __func__);

	return da8xx_ddr2_ctlr_base;
}

static struct resource da8xx_cpuidle_resources[] = {
	{
		.start		= DA8XX_DDR2_CTL_BASE,
		.end		= DA8XX_DDR2_CTL_BASE + SZ_32K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

/* DA8XX devices support DDR2 power down */
static struct davinci_cpuidle_config da8xx_cpuidle_pdata = {
	.ddr2_pdown	= 1,
};


static struct platform_device da8xx_cpuidle_device = {
	.name			= "cpuidle-davinci",
	.num_resources		= ARRAY_SIZE(da8xx_cpuidle_resources),
	.resource		= da8xx_cpuidle_resources,
	.dev = {
		.platform_data	= &da8xx_cpuidle_pdata,
	},
};

int __init da8xx_register_cpuidle(void)
{
	da8xx_cpuidle_pdata.ddr2_ctlr_base = da8xx_get_mem_ctlr();

	return platform_device_register(&da8xx_cpuidle_device);
}

static struct resource da8xx_spi0_resources[] = {
	[0] = {
		.start	= DA8XX_SPI0_BASE,
		.end	= DA8XX_SPI0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DA8XX_SPINT0,
		.end	= IRQ_DA8XX_SPINT0,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= DA8XX_DMA_SPI0_RX,
		.end	= DA8XX_DMA_SPI0_RX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= DA8XX_DMA_SPI0_TX,
		.end	= DA8XX_DMA_SPI0_TX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource da8xx_spi1_resources[] = {
	[0] = {
		.start	= DA830_SPI1_BASE,
		.end	= DA830_SPI1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DA8XX_SPINT1,
		.end	= IRQ_DA8XX_SPINT1,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= DA8XX_DMA_SPI1_RX,
		.end	= DA8XX_DMA_SPI1_RX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= DA8XX_DMA_SPI1_TX,
		.end	= DA8XX_DMA_SPI1_TX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct davinci_spi_platform_data da8xx_spi_pdata[] = {
	[0] = {
		.version	= SPI_VERSION_2,
		.intr_line	= 1,
		.dma_event_q	= EVENTQ_0,
	},
	[1] = {
		.version	= SPI_VERSION_2,
		.intr_line	= 1,
		.dma_event_q	= EVENTQ_0,
	},
};

static struct platform_device da8xx_spi_device[] = {
	[0] = {
		.name		= "spi_davinci",
		.id		= 0,
		.num_resources	= ARRAY_SIZE(da8xx_spi0_resources),
		.resource	= da8xx_spi0_resources,
		.dev		= {
			.platform_data = &da8xx_spi_pdata[0],
		},
	},
	[1] = {
		.name		= "spi_davinci",
		.id		= 1,
		.num_resources	= ARRAY_SIZE(da8xx_spi1_resources),
		.resource	= da8xx_spi1_resources,
		.dev		= {
			.platform_data = &da8xx_spi_pdata[1],
		},
	},
};

int __init da8xx_register_spi_bus(int instance, unsigned num_chipselect)
{
	if (instance < 0 || instance > 1)
		return -EINVAL;

	da8xx_spi_pdata[instance].num_chipselect = num_chipselect;

	if (instance == 1 && cpu_is_davinci_da850()) {
		da8xx_spi1_resources[0].start = DA850_SPI1_BASE;
		da8xx_spi1_resources[0].end = DA850_SPI1_BASE + SZ_4K - 1;
	}

	return platform_device_register(&da8xx_spi_device[instance]);
}

#ifdef CONFIG_ARCH_DAVINCI_DA850

static struct resource da850_sata_resources[] = {
	{
		.start	= DA850_SATA_BASE,
		.end	= DA850_SATA_BASE + 0x1fff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DA850_SATAINT,
		.flags	= IORESOURCE_IRQ,
	},
};

/* SATA PHY Control Register offset from AHCI base */
#define SATA_P0PHYCR_REG	0x178

#define SATA_PHY_MPY(x)		((x) << 0)
#define SATA_PHY_LOS(x)		((x) << 6)
#define SATA_PHY_RXCDR(x)	((x) << 10)
#define SATA_PHY_RXEQ(x)	((x) << 13)
#define SATA_PHY_TXSWING(x)	((x) << 19)
#define SATA_PHY_ENPLL(x)	((x) << 31)

static struct clk *da850_sata_clk;
static unsigned long da850_sata_refclkpn;

/* Supported DA850 SATA crystal frequencies */
#define KHZ_TO_HZ(freq) ((freq) * 1000)
static unsigned long da850_sata_xtal[] = {
	KHZ_TO_HZ(300000),
	KHZ_TO_HZ(250000),
	0,			/* Reserved */
	KHZ_TO_HZ(187500),
	KHZ_TO_HZ(150000),
	KHZ_TO_HZ(125000),
	KHZ_TO_HZ(120000),
	KHZ_TO_HZ(100000),
	KHZ_TO_HZ(75000),
	KHZ_TO_HZ(60000),
};

static int da850_sata_init(struct device *dev, void __iomem *addr)
{
	int i, ret;
	unsigned int val;

	da850_sata_clk = clk_get(dev, NULL);
	if (IS_ERR(da850_sata_clk))
		return PTR_ERR(da850_sata_clk);

	ret = clk_prepare_enable(da850_sata_clk);
	if (ret)
		goto err0;

	/* Enable SATA clock receiver */
	val = __raw_readl(DA8XX_SYSCFG1_VIRT(DA8XX_PWRDN_REG));
	val &= ~BIT(0);
	__raw_writel(val, DA8XX_SYSCFG1_VIRT(DA8XX_PWRDN_REG));

	/* Get the multiplier needed for 1.5GHz PLL output */
	for (i = 0; i < ARRAY_SIZE(da850_sata_xtal); i++)
		if (da850_sata_xtal[i] == da850_sata_refclkpn)
			break;

	if (i == ARRAY_SIZE(da850_sata_xtal)) {
		ret = -EINVAL;
		goto err1;
	}

	val = SATA_PHY_MPY(i + 1) |
		SATA_PHY_LOS(1) |
		SATA_PHY_RXCDR(4) |
		SATA_PHY_RXEQ(1) |
		SATA_PHY_TXSWING(3) |
		SATA_PHY_ENPLL(1);

	__raw_writel(val, addr + SATA_P0PHYCR_REG);

	return 0;

err1:
	clk_disable_unprepare(da850_sata_clk);
err0:
	clk_put(da850_sata_clk);
	return ret;
}

static void da850_sata_exit(struct device *dev)
{
	clk_disable_unprepare(da850_sata_clk);
	clk_put(da850_sata_clk);
}

static struct ahci_platform_data da850_sata_pdata = {
	.init	= da850_sata_init,
	.exit	= da850_sata_exit,
};

static u64 da850_sata_dmamask = DMA_BIT_MASK(32);

static struct platform_device da850_sata_device = {
	.name	= "ahci",
	.id	= -1,
	.dev	= {
		.platform_data		= &da850_sata_pdata,
		.dma_mask		= &da850_sata_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(da850_sata_resources),
	.resource	= da850_sata_resources,
};

int __init da850_register_sata(unsigned long refclkpn)
{
	da850_sata_refclkpn = refclkpn;
	if (!da850_sata_refclkpn)
		return -EINVAL;

	return platform_device_register(&da850_sata_device);
}
#endif
