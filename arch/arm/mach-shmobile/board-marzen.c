/*
 * marzen board support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/spi/spi.h>
#include <linux/spi/sh_hspi.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/usb/otg.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/pm_runtime.h>
#include <mach/hardware.h>
#include <mach/r8a7779.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/traps.h>

/* Fixed 3.3V regulator to be used by SDHI0 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* SMSC LAN89218 */
static struct resource smsc911x_resources[] = {
	[0] = {
		.start		= 0x18000000, /* ExCS0 */
		.end		= 0x180000ff, /* A1->A7 */
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= gic_spi(28), /* IRQ 1 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_platdata = {
	.flags		= SMSC911X_USE_32BIT, /* 32-bit SW on 16-bit HW bus */
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= -1,
	.dev  = {
		.platform_data = &smsc911x_platdata,
	},
	.resource	= smsc911x_resources,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "sdhi0",
		.start	= 0xffe4c000,
		.end	= 0xffe4c0ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(104),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mobile_sdhi_info sdhi0_platform_data = {
	.tmio_flags = TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_HAS_IDLE_WAIT,
	.tmio_caps = MMC_CAP_SD_HIGHSPEED,
};

static struct platform_device sdhi0_device = {
	.name = "sh_mobile_sdhi",
	.num_resources = ARRAY_SIZE(sdhi0_resources),
	.resource = sdhi0_resources,
	.id = 0,
	.dev = {
		.platform_data = &sdhi0_platform_data,
	}
};

/* Thermal */
static struct resource thermal_resources[] = {
	[0] = {
		.start		= 0xFFC48000,
		.end		= 0xFFC48038 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device thermal_device = {
	.name		= "rcar_thermal",
	.resource	= thermal_resources,
	.num_resources	= ARRAY_SIZE(thermal_resources),
};

/* HSPI */
static struct resource hspi_resources[] = {
	[0] = {
		.start		= 0xFFFC7000,
		.end		= 0xFFFC7018 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device hspi_device = {
	.name	= "sh-hspi",
	.id	= 0,
	.resource	= hspi_resources,
	.num_resources	= ARRAY_SIZE(hspi_resources),
};

/* USB PHY */
static struct resource usb_phy_resources[] = {
	[0] = {
		.start		= 0xffe70000,
		.end		= 0xffe70900 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= 0xfff70000,
		.end		= 0xfff70900 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device usb_phy_device = {
	.name		= "rcar_usb_phy",
	.resource	= usb_phy_resources,
	.num_resources	= ARRAY_SIZE(usb_phy_resources),
};

static struct platform_device *marzen_devices[] __initdata = {
	&eth_device,
	&sdhi0_device,
	&thermal_device,
	&hspi_device,
	&usb_phy_device,
};

/* USB */
static struct usb_phy *phy;
static int usb_power_on(struct platform_device *pdev)
{
	if (!phy)
		return -EIO;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	usb_phy_init(phy);

	return 0;
}

static void usb_power_off(struct platform_device *pdev)
{
	if (!phy)
		return;

	usb_phy_shutdown(phy);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static struct usb_ehci_pdata ehcix_pdata = {
	.power_on	= usb_power_on,
	.power_off	= usb_power_off,
	.power_suspend	= usb_power_off,
};

static struct resource ehci0_resources[] = {
	[0] = {
		.start	= 0xffe70000,
		.end	= 0xffe70400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(44),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ehci0_device = {
	.name	= "ehci-platform",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ehci0_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ehcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ehci0_resources),
	.resource	= ehci0_resources,
};

static struct resource ehci1_resources[] = {
	[0] = {
		.start	= 0xfff70000,
		.end	= 0xfff70400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(45),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ehci1_device = {
	.name	= "ehci-platform",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ehci1_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ehcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ehci1_resources),
	.resource	= ehci1_resources,
};

static struct usb_ohci_pdata ohcix_pdata = {
	.power_on	= usb_power_on,
	.power_off	= usb_power_off,
	.power_suspend	= usb_power_off,
};

static struct resource ohci0_resources[] = {
	[0] = {
		.start	= 0xffe70400,
		.end	= 0xffe70800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(44),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ohci0_device = {
	.name	= "ohci-platform",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ohci0_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ohcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ohci0_resources),
	.resource	= ohci0_resources,
};

static struct resource ohci1_resources[] = {
	[0] = {
		.start	= 0xfff70400,
		.end	= 0xfff70800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(45),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ohci1_device = {
	.name	= "ohci-platform",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ohci1_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ohcix_pdata,
	},
	.num_resources	= ARRAY_SIZE(ohci1_resources),
	.resource	= ohci1_resources,
};

static struct platform_device *marzen_late_devices[] __initdata = {
	&ehci0_device,
	&ehci1_device,
	&ohci0_device,
	&ohci1_device,
};

void __init marzen_init_late(void)
{
	/* get usb phy */
	phy = usb_get_phy(USB_PHY_TYPE_USB2);

	shmobile_init_late();
	platform_add_devices(marzen_late_devices,
			     ARRAY_SIZE(marzen_late_devices));
}

static void __init marzen_init(void)
{
	regulator_register_always_on(0, "fixed-3.3V", fixed3v3_power_consumers,
				ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	regulator_register_fixed(1, dummy_supplies,
				ARRAY_SIZE(dummy_supplies));

	r8a7779_pinmux_init();

	/* SCIF2 (CN18: DEBUG0) */
	gpio_request(GPIO_FN_TX2_C, NULL);
	gpio_request(GPIO_FN_RX2_C, NULL);

	/* SCIF4 (CN19: DEBUG1) */
	gpio_request(GPIO_FN_TX4, NULL);
	gpio_request(GPIO_FN_RX4, NULL);

	/* LAN89218 */
	gpio_request(GPIO_FN_EX_CS0, NULL); /* nCS */
	gpio_request(GPIO_FN_IRQ1_B, NULL); /* IRQ + PME */

	/* SD0 (CN20) */
	gpio_request(GPIO_FN_SD0_CLK, NULL);
	gpio_request(GPIO_FN_SD0_CMD, NULL);
	gpio_request(GPIO_FN_SD0_DAT0, NULL);
	gpio_request(GPIO_FN_SD0_DAT1, NULL);
	gpio_request(GPIO_FN_SD0_DAT2, NULL);
	gpio_request(GPIO_FN_SD0_DAT3, NULL);
	gpio_request(GPIO_FN_SD0_CD, NULL);
	gpio_request(GPIO_FN_SD0_WP, NULL);

	/* HSPI 0 */
	gpio_request(GPIO_FN_HSPI_CLK0,	NULL);
	gpio_request(GPIO_FN_HSPI_CS0,	NULL);
	gpio_request(GPIO_FN_HSPI_TX0,	NULL);
	gpio_request(GPIO_FN_HSPI_RX0,	NULL);

	/* USB (CN21) */
	gpio_request(GPIO_FN_USB_OVC0, NULL);
	gpio_request(GPIO_FN_USB_OVC1, NULL);
	gpio_request(GPIO_FN_USB_OVC2, NULL);

	/* USB (CN22) */
	gpio_request(GPIO_FN_USB_PENC2, NULL);

	r8a7779_add_standard_devices();
	platform_add_devices(marzen_devices, ARRAY_SIZE(marzen_devices));
}

MACHINE_START(MARZEN, "marzen")
	.smp		= smp_ops(r8a7779_smp_ops),
	.map_io		= r8a7779_map_io,
	.init_early	= r8a7779_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= r8a7779_init_irq,
	.init_machine	= marzen_init,
	.init_late	= marzen_init_late,
	.init_time	= r8a7779_earlytimer_init,
MACHINE_END
