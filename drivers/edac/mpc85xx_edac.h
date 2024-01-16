/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Freescale MPC85xx Memory Controller kernel module
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2007 (c) MontaVista Software, Inc.
 */
#ifndef _MPC85XX_EDAC_H_
#define _MPC85XX_EDAC_H_

#define MPC85XX_REVISION " Ver: 2.0.0"
#define EDAC_MOD_STR	"MPC85xx_edac"

#define mpc85xx_printk(level, fmt, arg...) \
	edac_printk(level, "MPC85xx", fmt, ##arg)

/*
 * L2 Err defines
 */
#define MPC85XX_L2_ERRINJHI	0x0000
#define MPC85XX_L2_ERRINJLO	0x0004
#define MPC85XX_L2_ERRINJCTL	0x0008
#define MPC85XX_L2_CAPTDATAHI	0x0020
#define MPC85XX_L2_CAPTDATALO	0x0024
#define MPC85XX_L2_CAPTECC	0x0028
#define MPC85XX_L2_ERRDET	0x0040
#define MPC85XX_L2_ERRDIS	0x0044
#define MPC85XX_L2_ERRINTEN	0x0048
#define MPC85XX_L2_ERRATTR	0x004c
#define MPC85XX_L2_ERRADDR	0x0050
#define MPC85XX_L2_ERRCTL	0x0058

/* Error Interrupt Enable */
#define L2_EIE_L2CFGINTEN	0x1
#define L2_EIE_SBECCINTEN	0x4
#define L2_EIE_MBECCINTEN	0x8
#define L2_EIE_TPARINTEN	0x10
#define L2_EIE_MASK	(L2_EIE_L2CFGINTEN | L2_EIE_SBECCINTEN | \
			L2_EIE_MBECCINTEN | L2_EIE_TPARINTEN)

/* Error Detect */
#define L2_EDE_L2CFGERR		0x1
#define L2_EDE_SBECCERR		0x4
#define L2_EDE_MBECCERR		0x8
#define L2_EDE_TPARERR		0x10
#define L2_EDE_MULL2ERR		0x80000000

#define L2_EDE_CE_MASK	L2_EDE_SBECCERR
#define L2_EDE_UE_MASK	(L2_EDE_L2CFGERR | L2_EDE_MBECCERR | \
			L2_EDE_TPARERR)
#define L2_EDE_MASK	(L2_EDE_L2CFGERR | L2_EDE_SBECCERR | \
			L2_EDE_MBECCERR | L2_EDE_TPARERR | L2_EDE_MULL2ERR)

/*
 * PCI Err defines
 */
#define PCI_EDE_TOE			0x00000001
#define PCI_EDE_SCM			0x00000002
#define PCI_EDE_IRMSV			0x00000004
#define PCI_EDE_ORMSV			0x00000008
#define PCI_EDE_OWMSV			0x00000010
#define PCI_EDE_TGT_ABRT		0x00000020
#define PCI_EDE_MST_ABRT		0x00000040
#define PCI_EDE_TGT_PERR		0x00000080
#define PCI_EDE_MST_PERR		0x00000100
#define PCI_EDE_RCVD_SERR		0x00000200
#define PCI_EDE_ADDR_PERR		0x00000400
#define PCI_EDE_MULTI_ERR		0x80000000

#define PCI_EDE_PERR_MASK	(PCI_EDE_TGT_PERR | PCI_EDE_MST_PERR | \
				PCI_EDE_ADDR_PERR)

#define MPC85XX_PCI_ERR_DR		0x0000
#define MPC85XX_PCI_ERR_CAP_DR		0x0004
#define MPC85XX_PCI_ERR_EN		0x0008
#define   PEX_ERR_ICCAIE_EN_BIT		0x00020000
#define MPC85XX_PCI_ERR_ATTRIB		0x000c
#define MPC85XX_PCI_ERR_ADDR		0x0010
#define   PEX_ERR_ICCAD_DISR_BIT	0x00020000
#define MPC85XX_PCI_ERR_EXT_ADDR	0x0014
#define MPC85XX_PCI_ERR_DL		0x0018
#define MPC85XX_PCI_ERR_DH		0x001c
#define MPC85XX_PCI_GAS_TIMR		0x0020
#define MPC85XX_PCI_PCIX_TIMR		0x0024
#define MPC85XX_PCIE_ERR_CAP_R0		0x0028
#define MPC85XX_PCIE_ERR_CAP_R1		0x002c
#define MPC85XX_PCIE_ERR_CAP_R2		0x0030
#define MPC85XX_PCIE_ERR_CAP_R3		0x0034

struct mpc85xx_l2_pdata {
	char *name;
	int edac_idx;
	void __iomem *l2_vbase;
	int irq;
};

struct mpc85xx_pci_pdata {
	char *name;
	bool is_pcie;
	int edac_idx;
	void __iomem *pci_vbase;
	int irq;
};

#endif
