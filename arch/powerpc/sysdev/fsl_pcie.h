/*
 * MPC85xx/86xx PCI Express structure define
 *
 * Copyright 2007 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __POWERPC_FSL_PCIE_H
#define __POWERPC_FSL_PCIE_H

/* PCIE Express IO block registers in 85xx/86xx */

struct ccsr_pex {
	__be32 __iomem    pex_config_addr;	/* 0x.000 - PCI Express Configuration Address Register */
	__be32 __iomem    pex_config_data;	/* 0x.004 - PCI Express Configuration Data Register */
	u8 __iomem    res1[4];
	__be32 __iomem    pex_otb_cpl_tor;	/* 0x.00c - PCI Express Outbound completion timeout register */
	__be32 __iomem    pex_conf_tor;		/* 0x.010 - PCI Express configuration timeout register */
	u8 __iomem    res2[12];
	__be32 __iomem    pex_pme_mes_dr;	/* 0x.020 - PCI Express PME and message detect register */
	__be32 __iomem    pex_pme_mes_disr;	/* 0x.024 - PCI Express PME and message disable register */
	__be32 __iomem    pex_pme_mes_ier;	/* 0x.028 - PCI Express PME and message interrupt enable register */
	__be32 __iomem    pex_pmcr;		/* 0x.02c - PCI Express power management command register */
	u8 __iomem    res3[3024];
	__be32 __iomem    pexotar0;		/* 0x.c00 - PCI Express outbound translation address register 0 */
	__be32 __iomem    pexotear0;		/* 0x.c04 - PCI Express outbound translation extended address register 0*/
	u8 __iomem    res4[8];
	__be32 __iomem    pexowar0;		/* 0x.c10 - PCI Express outbound window attributes register 0*/
	u8 __iomem    res5[12];
	__be32 __iomem    pexotar1;		/* 0x.c20 - PCI Express outbound translation address register 1 */
	__be32 __iomem    pexotear1;		/* 0x.c24 - PCI Express outbound translation extended address register 1*/
	__be32 __iomem    pexowbar1;		/* 0x.c28 - PCI Express outbound window base address register 1*/
	u8 __iomem    res6[4];
	__be32 __iomem    pexowar1;		/* 0x.c30 - PCI Express outbound window attributes register 1*/
	u8 __iomem    res7[12];
	__be32 __iomem    pexotar2;		/* 0x.c40 - PCI Express outbound translation address register 2 */
	__be32 __iomem    pexotear2;		/* 0x.c44 - PCI Express outbound translation extended address register 2*/
	__be32 __iomem    pexowbar2;		/* 0x.c48 - PCI Express outbound window base address register 2*/
	u8 __iomem    res8[4];
	__be32 __iomem    pexowar2;		/* 0x.c50 - PCI Express outbound window attributes register 2*/
	u8 __iomem    res9[12];
	__be32 __iomem    pexotar3;		/* 0x.c60 - PCI Express outbound translation address register 3 */
	__be32 __iomem    pexotear3;		/* 0x.c64 - PCI Express outbound translation extended address register 3*/
	__be32 __iomem    pexowbar3;		/* 0x.c68 - PCI Express outbound window base address register 3*/
	u8 __iomem    res10[4];
	__be32 __iomem    pexowar3;		/* 0x.c70 - PCI Express outbound window attributes register 3*/
	u8 __iomem    res11[12];
	__be32 __iomem    pexotar4;		/* 0x.c80 - PCI Express outbound translation address register 4 */
	__be32 __iomem    pexotear4;		/* 0x.c84 - PCI Express outbound translation extended address register 4*/
	__be32 __iomem    pexowbar4;		/* 0x.c88 - PCI Express outbound window base address register 4*/
	u8 __iomem    res12[4];
	__be32 __iomem    pexowar4;		/* 0x.c90 - PCI Express outbound window attributes register 4*/
	u8 __iomem    res13[12];
	u8 __iomem    res14[256];
	__be32 __iomem    pexitar3;		/* 0x.da0 - PCI Express inbound translation address register 3 */
	u8 __iomem    res15[4];
	__be32 __iomem    pexiwbar3;		/* 0x.da8 - PCI Express inbound window base address register 3 */
	__be32 __iomem    pexiwbear3;		/* 0x.dac - PCI Express inbound window base extended address register 3 */
	__be32 __iomem    pexiwar3;		/* 0x.db0 - PCI Express inbound window attributes register 3 */
	u8 __iomem    res16[12];
	__be32 __iomem    pexitar2;		/* 0x.dc0 - PCI Express inbound translation address register 2 */
	u8 __iomem    res17[4];
	__be32 __iomem    pexiwbar2;		/* 0x.dc8 - PCI Express inbound window base address register 2 */
	__be32 __iomem    pexiwbear2;		/* 0x.dcc - PCI Express inbound window base extended address register 2 */
	__be32 __iomem    pexiwar2;		/* 0x.dd0 - PCI Express inbound window attributes register 2 */
	u8 __iomem    res18[12];
	__be32 __iomem    pexitar1;		/* 0x.de0 - PCI Express inbound translation address register 2 */
	u8 __iomem    res19[4];
	__be32 __iomem    pexiwbar1;		/* 0x.de8 - PCI Express inbound window base address register 2 */
	__be32 __iomem    pexiwbear1;		/* 0x.dec - PCI Express inbound window base extended address register 2 */
	__be32 __iomem    pexiwar1;		/* 0x.df0 - PCI Express inbound window attributes register 2 */
	u8 __iomem    res20[12];
	__be32 __iomem    pex_err_dr;		/* 0x.e00 - PCI Express error detect register */
	u8 __iomem    res21[4];
	__be32 __iomem    pex_err_en;		/* 0x.e08 - PCI Express error interrupt enable register */
	u8 __iomem    res22[4];
	__be32 __iomem    pex_err_disr;		/* 0x.e10 - PCI Express error disable register */
	u8 __iomem    res23[12];
	__be32 __iomem    pex_err_cap_stat;	/* 0x.e20 - PCI Express error capture status register */
	u8 __iomem    res24[4];
	__be32 __iomem    pex_err_cap_r0;	/* 0x.e28 - PCI Express error capture register 0 */
	__be32 __iomem    pex_err_cap_r1;	/* 0x.e2c - PCI Express error capture register 0 */
	__be32 __iomem    pex_err_cap_r2;	/* 0x.e30 - PCI Express error capture register 0 */
	__be32 __iomem    pex_err_cap_r3;	/* 0x.e34 - PCI Express error capture register 0 */
};

#endif /* __POWERPC_FSL_PCIE_H */
#endif /* __KERNEL__ */
