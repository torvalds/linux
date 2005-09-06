/*
 * arch/ppc/platforms/4xx/ibmstb4.c
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/init.h>
#include <asm/ocp.h>
#include <asm/ppc4xx_pic.h>
#include <platforms/4xx/ibmstb4.h>

static struct ocp_func_iic_data ibmstb4_iic0_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};

static struct ocp_func_iic_data ibmstb4_iic1_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};
OCP_SYSFS_IIC_DATA()

struct ocp_def core_ocp[] __initdata = {
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 0,
	  .paddr	= UART0_IO_BASE,
	  .irq		= UART0_INT,
	  .pm		= IBM_CPM_UART0,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 1,
	  .paddr	= UART1_IO_BASE,
	  .irq		= UART1_INT,
	  .pm		= IBM_CPM_UART1,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 2,
	  .paddr	= UART2_IO_BASE,
	  .irq		= UART2_INT,
	  .pm		= IBM_CPM_UART2,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .paddr	= IIC0_BASE,
	  .irq		= IIC0_IRQ,
	  .pm		= IBM_CPM_IIC0,
	  .additions	= &ibmstb4_iic0_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .paddr	= IIC1_BASE,
	  .irq		= IIC1_IRQ,
	  .pm		= IBM_CPM_IIC1,
	  .additions	= &ibmstb4_iic1_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_GPIO,
	  .paddr	= GPIO0_BASE,
	  .irq		= OCP_IRQ_NA,
	  .pm		= IBM_CPM_GPIO0,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IDE,
	  .paddr	= IDE0_BASE,
	  .irq		= IDE0_IRQ,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_INVALID,
	}
};

/* Polarity and triggering settings for internal interrupt sources */
struct ppc4xx_uic_settings ppc4xx_core_uic_cfg[] __initdata = {
	{ .polarity 	= 0x7fffff01,
	  .triggering	= 0x00000000,
	  .ext_irq_mask	= 0x0000007e,	/* IRQ0 - IRQ5 */
	}
};

static struct resource ohci_usb_resources[] = {
	[0] = {
		.start	= USB0_BASE,
		.end	= USB0_BASE + USB0_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= USB0_IRQ,
		.end	= USB0_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 dma_mask = 0xffffffffULL;

static struct platform_device ohci_usb_device = {
	.name		= "ppc-soc-ohci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ohci_usb_resources),
	.resource	= ohci_usb_resources,
	.dev		= {
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xffffffffULL,
	}
};

static struct platform_device *ibmstb4_devs[] __initdata = {
	&ohci_usb_device,
};

static int __init
ibmstb4_platform_add_devices(void)
{
	return platform_add_devices(ibmstb4_devs, ARRAY_SIZE(ibmstb4_devs));
}
arch_initcall(ibmstb4_platform_add_devices);
