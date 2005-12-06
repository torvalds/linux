/*
 * arch/ppc/platforms/4xx/ppc440spe.c
 *
 * PPC440SPe I/O descriptions
 *
 * Roland Dreier <rolandd@cisco.com>
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 *
 * Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <platforms/4xx/ppc440spe.h>
#include <asm/ocp.h>
#include <asm/ppc4xx_pic.h>

static struct ocp_func_emac_data ppc440spe_emac0_def = {
	.rgmii_idx	= -1,		/* No RGMII */
	.rgmii_mux	= -1,		/* No RGMII */
	.zmii_idx       = -1,           /* No ZMII */
	.zmii_mux       = -1,           /* No ZMII */
	.mal_idx        = 0,            /* MAL device index */
	.mal_rx_chan    = 0,            /* MAL rx channel number */
	.mal_tx_chan    = 0,            /* MAL tx channel number */
	.wol_irq        = 61,  		/* WOL interrupt number */
	.mdio_idx       = -1,           /* No shared MDIO */
	.tah_idx	= -1,		/* No TAH */
};
OCP_SYSFS_EMAC_DATA()

static struct ocp_func_mal_data ppc440spe_mal0_def = {
	.num_tx_chans   = 1,    	/* Number of TX channels */
	.num_rx_chans   = 1,    	/* Number of RX channels */
	.txeob_irq	= 38,		/* TX End Of Buffer IRQ  */
	.rxeob_irq	= 39,		/* RX End Of Buffer IRQ  */
	.txde_irq	= 34,		/* TX Descriptor Error IRQ */
	.rxde_irq	= 35,		/* RX Descriptor Error IRQ */
	.serr_irq	= 33,		/* MAL System Error IRQ    */
	.dcr_base	= DCRN_MAL_BASE /* MAL0_CFG DCR number */
};
OCP_SYSFS_MAL_DATA()

static struct ocp_func_iic_data ppc440spe_iic0_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};

static struct ocp_func_iic_data ppc440spe_iic1_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};
OCP_SYSFS_IIC_DATA()

struct ocp_def core_ocp[] = {
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 0,
	  .paddr	= PPC440SPE_UART0_ADDR,
	  .irq		= UART0_INT,
	  .pm		= IBM_CPM_UART0,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 1,
	  .paddr	= PPC440SPE_UART1_ADDR,
	  .irq		= UART1_INT,
	  .pm		= IBM_CPM_UART1,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 2,
	  .paddr	= PPC440SPE_UART2_ADDR,
	  .irq		= UART2_INT,
	  .pm		= IBM_CPM_UART2,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .index	= 0,
	  .paddr	= 0x00000004f0000400ULL,
	  .irq		= 2,
	  .pm		= IBM_CPM_IIC0,
	  .additions	= &ppc440spe_iic0_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .index	= 1,
	  .paddr	= 0x00000004f0000500ULL,
	  .irq		= 3,
	  .pm		= IBM_CPM_IIC1,
	  .additions	= &ppc440spe_iic1_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_GPIO,
	  .index	= 0,
	  .paddr	= 0x00000004f0000700ULL,
	  .irq		= OCP_IRQ_NA,
	  .pm		= IBM_CPM_GPIO0,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_MAL,
	  .paddr	= OCP_PADDR_NA,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ppc440spe_mal0_def,
	  .show		= &ocp_show_mal_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_EMAC,
	  .index	= 0,
	  .paddr	= 0x00000004f0000800ULL,
	  .irq		= 60,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ppc440spe_emac0_def,
	  .show		= &ocp_show_emac_data,
	},
	{ .vendor	= OCP_VENDOR_INVALID
	}
};

/* Polarity and triggering settings for internal interrupt sources */
struct ppc4xx_uic_settings ppc4xx_core_uic_cfg[] __initdata = {
	{ .polarity     = 0xffffffff,
	  .triggering   = 0x010f0004,
	  .ext_irq_mask = 0x00000000,
	},
	{ .polarity     = 0xffffffff,
	  .triggering   = 0x001f8040,
	  .ext_irq_mask = 0x00007c30,   /* IRQ6 - IRQ7, IRQ8 - IRQ12 */
	},
	{ .polarity     = 0xffffffff,
	  .triggering   = 0x00000000,
	  .ext_irq_mask = 0x000000fc,   /* IRQ0 - IRQ5 */
	},
	{ .polarity     = 0xffffffff,
	  .triggering   = 0x00000000,
	  .ext_irq_mask = 0x00000000,
	},
};
