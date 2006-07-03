/*
 *
 *    Copyright 2000-2001 MontaVista Software Inc.
 *      Original author: Armin Kuster akuster@mvista.com
 *
 *    Module name: ibm405gp.c
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/param.h>
#include <linux/string.h>
#include <platforms/4xx/ibm405gp.h>
#include <asm/ibm4xx.h>
#include <asm/ocp.h>
#include <asm/ppc4xx_pic.h>

static struct ocp_func_emac_data ibm405gp_emac0_def = {
	.rgmii_idx	= -1,		/* No RGMII */
	.rgmii_mux	= -1,		/* No RGMII */
	.zmii_idx	= -1,		/* ZMII device index */
	.zmii_mux	= 0,		/* ZMII input of this EMAC */
	.mal_idx	= 0,		/* MAL device index */
	.mal_rx_chan	= 0,		/* MAL rx channel number */
	.mal_tx_chan	= 0,		/* MAL tx channel number */
	.wol_irq	= 9,		/* WOL interrupt number */
	.mdio_idx	= -1,		/* No shared MDIO */
	.tah_idx	= -1,		/* No TAH */
};
OCP_SYSFS_EMAC_DATA()

static struct ocp_func_mal_data ibm405gp_mal0_def = {
	.num_tx_chans	= 1,		/* Number of TX channels */
	.num_rx_chans	= 1,		/* Number of RX channels */
	.txeob_irq	= 11,		/* TX End Of Buffer IRQ  */
	.rxeob_irq	= 12,		/* RX End Of Buffer IRQ  */
	.txde_irq	= 13,		/* TX Descriptor Error IRQ */
	.rxde_irq	= 14,		/* RX Descriptor Error IRQ */
	.serr_irq	= 10,		/* MAL System Error IRQ    */
	.dcr_base	= DCRN_MAL_BASE /* MAL0_CFG DCR number */
};
OCP_SYSFS_MAL_DATA()

static struct ocp_func_iic_data ibm405gp_iic0_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};
OCP_SYSFS_IIC_DATA()

struct ocp_def core_ocp[] = {
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_OPB,
	  .index	= 0,
	  .paddr	= 0xEF600000,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 0,
	  .paddr	= UART0_IO_BASE,
	  .irq		= UART0_INT,
	  .pm		= IBM_CPM_UART0
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 1,
	  .paddr	= UART1_IO_BASE,
	  .irq		= UART1_INT,
	  .pm		= IBM_CPM_UART1
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .paddr	= 0xEF600500,
	  .irq		= 2,
	  .pm		= IBM_CPM_IIC0,
	  .additions	= &ibm405gp_iic0_def,
	  .show		= &ocp_show_iic_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_GPIO,
	  .paddr	= 0xEF600700,
	  .irq		= OCP_IRQ_NA,
	  .pm		= IBM_CPM_GPIO0
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_MAL,
	  .paddr	= OCP_PADDR_NA,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ibm405gp_mal0_def,
	  .show		= &ocp_show_mal_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_EMAC,
	  .index	= 0,
	  .paddr	= EMAC0_BASE,
	  .irq		= 15,
	  .pm		= IBM_CPM_EMAC0,
	  .additions	= &ibm405gp_emac0_def,
	  .show		= &ocp_show_emac_data,
	},
	{ .vendor	= OCP_VENDOR_INVALID
	}
};

/* Polarity and triggering settings for internal interrupt sources */
struct ppc4xx_uic_settings ppc4xx_core_uic_cfg[] __initdata = {
	{ .polarity 	= 0xffffff80,
	  .triggering	= 0x10000000,
	  .ext_irq_mask	= 0x0000007f,	/* IRQ0 - IRQ6 */
	}
};
