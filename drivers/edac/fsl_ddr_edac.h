/*
 * Freescale Memory Controller kernel module
 *
 * Support  Power-based SoCs including MPC85xx, MPC86xx, MPC83xx and
 * ARM-based Layerscape SoCs including LS2xxx and LS1021A. Originally
 * split out from mpc85xx_edac EDAC driver.
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */
#ifndef _FSL_DDR_EDAC_H_
#define _FSL_DDR_EDAC_H_

#define fsl_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "FSL_DDR", fmt, ##arg)

/*
 * DRAM error defines
 */

/* DDR_SDRAM_CFG */
#define FSL_MC_DDR_SDRAM_CFG	0x0110
#define FSL_MC_CS_BNDS_0		0x0000
#define FSL_MC_CS_BNDS_OFS		0x0008

#define FSL_MC_DATA_ERR_INJECT_HI	0x0e00
#define FSL_MC_DATA_ERR_INJECT_LO	0x0e04
#define FSL_MC_ECC_ERR_INJECT	0x0e08
#define FSL_MC_CAPTURE_DATA_HI	0x0e20
#define FSL_MC_CAPTURE_DATA_LO	0x0e24
#define FSL_MC_CAPTURE_ECC		0x0e28
#define FSL_MC_ERR_DETECT		0x0e40
#define FSL_MC_ERR_DISABLE		0x0e44
#define FSL_MC_ERR_INT_EN		0x0e48
#define FSL_MC_CAPTURE_ATRIBUTES	0x0e4c
#define FSL_MC_CAPTURE_ADDRESS	0x0e50
#define FSL_MC_CAPTURE_EXT_ADDRESS	0x0e54
#define FSL_MC_ERR_SBE		0x0e58

#define DSC_MEM_EN	0x80000000
#define DSC_ECC_EN	0x20000000
#define DSC_RD_EN	0x10000000
#define DSC_DBW_MASK	0x00180000
#define DSC_DBW_32	0x00080000
#define DSC_DBW_64	0x00000000

#define DSC_SDTYPE_MASK		0x07000000
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

struct fsl_mc_pdata {
	char *name;
	int edac_idx;
	void __iomem *mc_vbase;
	int irq;
};
int fsl_mc_err_probe(struct platform_device *op);
int fsl_mc_err_remove(struct platform_device *op);
#endif
