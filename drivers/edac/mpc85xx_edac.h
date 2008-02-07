/*
 * Freescale MPC85xx Memory Controller kenel module
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */
#ifndef _MPC85XX_EDAC_H_
#define _MPC85XX_EDAC_H_

#define MPC85XX_REVISION " Ver: 2.0.0 " __DATE__
#define EDAC_MOD_STR	"MPC85xx_edac"

#define mpc85xx_printk(level, fmt, arg...) \
	edac_printk(level, "MPC85xx", fmt, ##arg)

#define mpc85xx_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "MPC85xx", fmt, ##arg)

/*
 * DRAM error defines
 */

/* DDR_SDRAM_CFG */
#define MPC85XX_MC_DDR_SDRAM_CFG	0x0110
#define MPC85XX_MC_CS_BNDS_0		0x0000
#define MPC85XX_MC_CS_BNDS_1		0x0008
#define MPC85XX_MC_CS_BNDS_2		0x0010
#define MPC85XX_MC_CS_BNDS_3		0x0018
#define MPC85XX_MC_CS_BNDS_OFS		0x0008

#define MPC85XX_MC_DATA_ERR_INJECT_HI	0x0e00
#define MPC85XX_MC_DATA_ERR_INJECT_LO	0x0e04
#define MPC85XX_MC_ECC_ERR_INJECT	0x0e08
#define MPC85XX_MC_CAPTURE_DATA_HI	0x0e20
#define MPC85XX_MC_CAPTURE_DATA_LO	0x0e24
#define MPC85XX_MC_CAPTURE_ECC		0x0e28
#define MPC85XX_MC_ERR_DETECT		0x0e40
#define MPC85XX_MC_ERR_DISABLE		0x0e44
#define MPC85XX_MC_ERR_INT_EN		0x0e48
#define MPC85XX_MC_CAPTURE_ATRIBUTES	0x0e4c
#define MPC85XX_MC_CAPTURE_ADDRESS	0x0e50
#define MPC85XX_MC_ERR_SBE		0x0e58

#define DSC_MEM_EN	0x80000000
#define DSC_ECC_EN	0x20000000
#define DSC_RD_EN	0x10000000

#define DSC_SDTYPE_MASK		0x07000000

#define DSC_SDTYPE_DDR		0x02000000
#define DSC_SDTYPE_DDR2		0x03000000
#define DSC_X32_EN	0x00000020

/* Err_Int_En */
#define DDR_EIE_MSEE	0x1	/* memory select */
#define DDR_EIE_SBEE	0x4	/* single-bit ECC error */
#define DDR_EIE_MBEE	0x8	/* multi-bit ECC error */

/* Err_Detect */
#define DDR_EDE_MSE		0x1	/* memory select */
#define DDR_EDE_SBE		0x4	/* single-bit ECC error */
#define DDR_EDE_MBE		0x8	/* multi-bit ECC error */
#define DDR_EDE_MME		0x80000000	/* multiple memory errors */

/* Err_Disable */
#define DDR_EDI_MSED	0x1	/* memory select disable */
#define	DDR_EDI_SBED	0x4	/* single-bit ECC error disable */
#define	DDR_EDI_MBED	0x8	/* multi-bit ECC error disable */

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
#define MPC85XX_PCI_ERR_ATTRIB		0x000c
#define MPC85XX_PCI_ERR_ADDR		0x0010
#define MPC85XX_PCI_ERR_EXT_ADDR	0x0014
#define MPC85XX_PCI_ERR_DL		0x0018
#define MPC85XX_PCI_ERR_DH		0x001c
#define MPC85XX_PCI_GAS_TIMR		0x0020
#define MPC85XX_PCI_PCIX_TIMR		0x0024

struct mpc85xx_mc_pdata {
	char *name;
	int edac_idx;
	void __iomem *mc_vbase;
	int irq;
};

struct mpc85xx_l2_pdata {
	char *name;
	int edac_idx;
	void __iomem *l2_vbase;
	int irq;
};

struct mpc85xx_pci_pdata {
	char *name;
	int edac_idx;
	void __iomem *pci_vbase;
	int irq;
};

#endif
