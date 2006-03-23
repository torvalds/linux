/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/init.h>
#include <asm/ocp.h>
#include <platforms/4xx/ibmstbx25.h>
#include <asm/ppc4xx_pic.h>

static struct ocp_func_iic_data ibmstbx25_iic0_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};
OCP_SYSFS_IIC_DATA()

struct ocp_def core_ocp[] __initdata = {
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index        = 0,
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
	  .additions	= &ibmstbx25_iic0_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_GPIO,
	  .paddr	= GPIO0_BASE,
	  .irq		= OCP_IRQ_NA,
	  .pm		= IBM_CPM_GPIO0,
	},
	{ .vendor	= OCP_VENDOR_INVALID
	}
};

/* Polarity and triggering settings for internal interrupt sources */
struct ppc4xx_uic_settings ppc4xx_core_uic_cfg[] __initdata = {
	{ .polarity 	= 0xffff8f80,
	  .triggering	= 0x00000000,
	  .ext_irq_mask	= 0x0000707f,	/* IRQ7 - IRQ9, IRQ0 - IRQ6 */
	}
};
