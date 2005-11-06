/*
 * arch/ppc/platforms/4xx/ibm440gx.c
 *
 * PPC440GX I/O descriptions
 *
 * Matt Porter <mporter@mvista.com>
 * Copyright 2002-2004 MontaVista Software Inc.
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
#include <platforms/4xx/ibm440gx.h>
#include <asm/ocp.h>
#include <asm/ppc4xx_pic.h>

static struct ocp_func_emac_data ibm440gx_emac0_def = {
	.rgmii_idx	= -1,		/* No RGMII */
	.rgmii_mux	= -1,		/* No RGMII */
	.zmii_idx       = 0,            /* ZMII device index */
	.zmii_mux       = 0,            /* ZMII input of this EMAC */
	.mal_idx        = 0,            /* MAL device index */
	.mal_rx_chan    = 0,            /* MAL rx channel number */
	.mal_tx_chan    = 0,            /* MAL tx channel number */
	.wol_irq        = 61,   	/* WOL interrupt number */
	.mdio_idx       = -1,           /* No shared MDIO */
	.tah_idx	= -1,		/* No TAH */
};

static struct ocp_func_emac_data ibm440gx_emac1_def = {
	.rgmii_idx	= -1,		/* No RGMII */
	.rgmii_mux	= -1,		/* No RGMII */
	.zmii_idx       = 0,            /* ZMII device index */
	.zmii_mux       = 1,            /* ZMII input of this EMAC */
	.mal_idx        = 0,            /* MAL device index */
	.mal_rx_chan    = 1,            /* MAL rx channel number */
	.mal_tx_chan    = 1,            /* MAL tx channel number */
	.wol_irq        = 63,  		/* WOL interrupt number */
	.mdio_idx       = -1,           /* No shared MDIO */
	.tah_idx	= -1,		/* No TAH */
};

static struct ocp_func_emac_data ibm440gx_emac2_def = {
	.rgmii_idx	= 0,		/* RGMII device index */
	.rgmii_mux	= 0,		/* RGMII input of this EMAC */
	.zmii_idx       = 0,            /* ZMII device index */
	.zmii_mux       = 2,            /* ZMII input of this EMAC */
	.mal_idx        = 0,            /* MAL device index */
	.mal_rx_chan    = 2,            /* MAL rx channel number */
	.mal_tx_chan    = 2,            /* MAL tx channel number */
	.wol_irq        = 65,  		/* WOL interrupt number */
	.mdio_idx       = -1,           /* No shared MDIO */
	.tah_idx	= 0,		/* TAH device index */
	.jumbo		= 1,		/* Jumbo frames supported */
};

static struct ocp_func_emac_data ibm440gx_emac3_def = {
	.rgmii_idx	= 0,		/* RGMII device index */
	.rgmii_mux	= 1,		/* RGMII input of this EMAC */
	.zmii_idx       = 0,            /* ZMII device index */
	.zmii_mux       = 3,            /* ZMII input of this EMAC */
	.mal_idx        = 0,            /* MAL device index */
	.mal_rx_chan    = 3,            /* MAL rx channel number */
	.mal_tx_chan    = 3,            /* MAL tx channel number */
	.wol_irq        = 67,  		/* WOL interrupt number */
	.mdio_idx       = -1,           /* No shared MDIO */
	.tah_idx	= 1,		/* TAH device index */
	.jumbo		= 1,		/* Jumbo frames supported */
};
OCP_SYSFS_EMAC_DATA()

static struct ocp_func_mal_data ibm440gx_mal0_def = {
	.num_tx_chans   = 4,    	/* Number of TX channels */
	.num_rx_chans   = 4,    	/* Number of RX channels */
	.txeob_irq	= 10,		/* TX End Of Buffer IRQ  */
	.rxeob_irq	= 11,		/* RX End Of Buffer IRQ  */
	.txde_irq	= 33,		/* TX Descriptor Error IRQ */
	.rxde_irq	= 34,		/* RX Descriptor Error IRQ */
	.serr_irq	= 32,		/* MAL System Error IRQ    */
	.dcr_base	= DCRN_MAL_BASE /* MAL0_CFG DCR number */
};
OCP_SYSFS_MAL_DATA()

static struct ocp_func_iic_data ibm440gx_iic0_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};

static struct ocp_func_iic_data ibm440gx_iic1_def = {
	.fast_mode	= 0,		/* Use standad mode (100Khz) */
};
OCP_SYSFS_IIC_DATA()

struct ocp_def core_ocp[] = {
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_OPB,
	  .index	= 0,
	  .paddr	= 0x0000000140000000ULL,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 0,
	  .paddr	= PPC440GX_UART0_ADDR,
	  .irq		= UART0_INT,
	  .pm		= IBM_CPM_UART0,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_16550,
	  .index	= 1,
	  .paddr	= PPC440GX_UART1_ADDR,
	  .irq		= UART1_INT,
	  .pm		= IBM_CPM_UART1,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .index	= 0,
	  .paddr	= 0x0000000140000400ULL,
	  .irq		= 2,
	  .pm		= IBM_CPM_IIC0,
	  .additions	= &ibm440gx_iic0_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_IIC,
	  .index	= 1,
	  .paddr	= 0x0000000140000500ULL,
	  .irq		= 3,
	  .pm		= IBM_CPM_IIC1,
	  .additions	= &ibm440gx_iic1_def,
	  .show		= &ocp_show_iic_data
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_GPIO,
	  .index	= 0,
	  .paddr	= 0x0000000140000700ULL,
	  .irq		= OCP_IRQ_NA,
	  .pm		= IBM_CPM_GPIO0,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_MAL,
	  .paddr	= OCP_PADDR_NA,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ibm440gx_mal0_def,
	  .show		= &ocp_show_mal_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_EMAC,
	  .index	= 0,
	  .paddr	= 0x0000000140000800ULL,
	  .irq		= 60,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ibm440gx_emac0_def,
	  .show		= &ocp_show_emac_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_EMAC,
	  .index	= 1,
	  .paddr	= 0x0000000140000900ULL,
	  .irq		= 62,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ibm440gx_emac1_def,
	  .show		= &ocp_show_emac_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_EMAC,
	  .index	= 2,
	  .paddr	= 0x0000000140000C00ULL,
	  .irq		= 64,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ibm440gx_emac2_def,
	  .show		= &ocp_show_emac_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_EMAC,
	  .index	= 3,
	  .paddr	= 0x0000000140000E00ULL,
	  .irq		= 66,
	  .pm		= OCP_CPM_NA,
	  .additions	= &ibm440gx_emac3_def,
	  .show		= &ocp_show_emac_data,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_RGMII,
	  .paddr	= 0x0000000140000790ULL,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_ZMII,
	  .paddr	= 0x0000000140000780ULL,
	  .irq		= OCP_IRQ_NA,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_TAH,
	  .index	= 0,
	  .paddr	= 0x0000000140000b50ULL,
	  .irq		= 68,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_IBM,
	  .function	= OCP_FUNC_TAH,
	  .index	= 1,
	  .paddr	= 0x0000000140000d50ULL,
	  .irq		= 69,
	  .pm		= OCP_CPM_NA,
	},
	{ .vendor	= OCP_VENDOR_INVALID
	}
};

/* Polarity and triggering settings for internal interrupt sources */
struct ppc4xx_uic_settings ppc4xx_core_uic_cfg[] __initdata = {
	{ .polarity 	= 0xfffffe03,
	  .triggering	= 0x01c00000,
	  .ext_irq_mask	= 0x000001fc,	/* IRQ0 - IRQ6 */
	},
	{ .polarity 	= 0xffffc0ff,
	  .triggering	= 0x00ff8000,
	  .ext_irq_mask	= 0x00003f00,	/* IRQ7 - IRQ12 */
	},
	{ .polarity 	= 0xffff83ff,
	  .triggering	= 0x000f83c0,
	  .ext_irq_mask	= 0x00007c00,	/* IRQ13 - IRQ17 */
	},
};
