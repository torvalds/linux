/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2003-2019, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */
#ifndef _MEI_HW_MEI_REGS_H_
#define _MEI_HW_MEI_REGS_H_

/*
 * MEI device IDs
 */
#define MEI_DEV_ID_82946GZ    0x2974  /* 82946GZ/GL */
#define MEI_DEV_ID_82G35      0x2984  /* 82G35 Express */
#define MEI_DEV_ID_82Q965     0x2994  /* 82Q963/Q965 */
#define MEI_DEV_ID_82G965     0x29A4  /* 82P965/G965 */

#define MEI_DEV_ID_82GM965    0x2A04  /* Mobile PM965/GM965 */
#define MEI_DEV_ID_82GME965   0x2A14  /* Mobile GME965/GLE960 */

#define MEI_DEV_ID_ICH9_82Q35 0x29B4  /* 82Q35 Express */
#define MEI_DEV_ID_ICH9_82G33 0x29C4  /* 82G33/G31/P35/P31 Express */
#define MEI_DEV_ID_ICH9_82Q33 0x29D4  /* 82Q33 Express */
#define MEI_DEV_ID_ICH9_82X38 0x29E4  /* 82X38/X48 Express */
#define MEI_DEV_ID_ICH9_3200  0x29F4  /* 3200/3210 Server */

#define MEI_DEV_ID_ICH9_6     0x28B4  /* Bearlake */
#define MEI_DEV_ID_ICH9_7     0x28C4  /* Bearlake */
#define MEI_DEV_ID_ICH9_8     0x28D4  /* Bearlake */
#define MEI_DEV_ID_ICH9_9     0x28E4  /* Bearlake */
#define MEI_DEV_ID_ICH9_10    0x28F4  /* Bearlake */

#define MEI_DEV_ID_ICH9M_1    0x2A44  /* Cantiga */
#define MEI_DEV_ID_ICH9M_2    0x2A54  /* Cantiga */
#define MEI_DEV_ID_ICH9M_3    0x2A64  /* Cantiga */
#define MEI_DEV_ID_ICH9M_4    0x2A74  /* Cantiga */

#define MEI_DEV_ID_ICH10_1    0x2E04  /* Eaglelake */
#define MEI_DEV_ID_ICH10_2    0x2E14  /* Eaglelake */
#define MEI_DEV_ID_ICH10_3    0x2E24  /* Eaglelake */
#define MEI_DEV_ID_ICH10_4    0x2E34  /* Eaglelake */

#define MEI_DEV_ID_IBXPK_1    0x3B64  /* Calpella */
#define MEI_DEV_ID_IBXPK_2    0x3B65  /* Calpella */

#define MEI_DEV_ID_CPT_1      0x1C3A  /* Couger Point */
#define MEI_DEV_ID_PBG_1      0x1D3A  /* C600/X79 Patsburg */

#define MEI_DEV_ID_PPT_1      0x1E3A  /* Panther Point */
#define MEI_DEV_ID_PPT_2      0x1CBA  /* Panther Point */
#define MEI_DEV_ID_PPT_3      0x1DBA  /* Panther Point */

#define MEI_DEV_ID_LPT_H      0x8C3A  /* Lynx Point H */
#define MEI_DEV_ID_LPT_W      0x8D3A  /* Lynx Point - Wellsburg */
#define MEI_DEV_ID_LPT_LP     0x9C3A  /* Lynx Point LP */
#define MEI_DEV_ID_LPT_HR     0x8CBA  /* Lynx Point H Refresh */

#define MEI_DEV_ID_WPT_LP     0x9CBA  /* Wildcat Point LP */
#define MEI_DEV_ID_WPT_LP_2   0x9CBB  /* Wildcat Point LP 2 */

#define MEI_DEV_ID_SPT        0x9D3A  /* Sunrise Point */
#define MEI_DEV_ID_SPT_2      0x9D3B  /* Sunrise Point 2 */
#define MEI_DEV_ID_SPT_H      0xA13A  /* Sunrise Point H */
#define MEI_DEV_ID_SPT_H_2    0xA13B  /* Sunrise Point H 2 */

#define MEI_DEV_ID_LBG        0xA1BA  /* Lewisburg (SPT) */

#define MEI_DEV_ID_BXT_M      0x1A9A  /* Broxton M */
#define MEI_DEV_ID_APL_I      0x5A9A  /* Apollo Lake I */

#define MEI_DEV_ID_DNV_IE     0x19E5  /* Denverton IE */

#define MEI_DEV_ID_GLK        0x319A  /* Gemini Lake */

#define MEI_DEV_ID_KBP        0xA2BA  /* Kaby Point */
#define MEI_DEV_ID_KBP_2      0xA2BB  /* Kaby Point 2 */

#define MEI_DEV_ID_CNP_LP     0x9DE0  /* Cannon Point LP */
#define MEI_DEV_ID_CNP_LP_3   0x9DE4  /* Cannon Point LP 3 (iTouch) */
#define MEI_DEV_ID_CNP_H      0xA360  /* Cannon Point H */
#define MEI_DEV_ID_CNP_H_3    0xA364  /* Cannon Point H 3 (iTouch) */

#define MEI_DEV_ID_CMP_LP     0x02e0  /* Comet Point LP */
#define MEI_DEV_ID_CMP_LP_3   0x02e4  /* Comet Point LP 3 (iTouch) */

#define MEI_DEV_ID_CMP_V      0xA3BA  /* Comet Point Lake V */

#define MEI_DEV_ID_CMP_H      0x06e0  /* Comet Lake H */
#define MEI_DEV_ID_CMP_H_3    0x06e4  /* Comet Lake H 3 (iTouch) */

#define MEI_DEV_ID_CDF        0x18D3  /* Cedar Fork */

#define MEI_DEV_ID_ICP_LP     0x34E0  /* Ice Lake Point LP */

#define MEI_DEV_ID_JSP_N      0x4DE0  /* Jasper Lake Point N */

#define MEI_DEV_ID_TGP_LP     0xA0E0  /* Tiger Lake Point LP */

#define MEI_DEV_ID_MCC        0x4B70  /* Mule Creek Canyon (EHL) */
#define MEI_DEV_ID_MCC_4      0x4B75  /* Mule Creek Canyon 4 (EHL) */

/*
 * MEI HW Section
 */

/* Host Firmware Status Registers in PCI Config Space */
#define PCI_CFG_HFS_1         0x40
#  define PCI_CFG_HFS_1_D0I3_MSK     0x80000000
#define PCI_CFG_HFS_2         0x48
#define PCI_CFG_HFS_3         0x60
#define PCI_CFG_HFS_4         0x64
#define PCI_CFG_HFS_5         0x68
#define PCI_CFG_HFS_6         0x6C

/* MEI registers */
/* H_CB_WW - Host Circular Buffer (CB) Write Window register */
#define H_CB_WW    0
/* H_CSR - Host Control Status register */
#define H_CSR      4
/* ME_CB_RW - ME Circular Buffer Read Window register (read only) */
#define ME_CB_RW   8
/* ME_CSR_HA - ME Control Status Host Access register (read only) */
#define ME_CSR_HA  0xC
/* H_HGC_CSR - PGI register */
#define H_HPG_CSR  0x10
/* H_D0I3C - D0I3 Control  */
#define H_D0I3C    0x800

/* register bits of H_CSR (Host Control Status register) */
/* Host Circular Buffer Depth - maximum number of 32-bit entries in CB */
#define H_CBD             0xFF000000
/* Host Circular Buffer Write Pointer */
#define H_CBWP            0x00FF0000
/* Host Circular Buffer Read Pointer */
#define H_CBRP            0x0000FF00
/* Host Reset */
#define H_RST             0x00000010
/* Host Ready */
#define H_RDY             0x00000008
/* Host Interrupt Generate */
#define H_IG              0x00000004
/* Host Interrupt Status */
#define H_IS              0x00000002
/* Host Interrupt Enable */
#define H_IE              0x00000001
/* Host D0I3 Interrupt Enable */
#define H_D0I3C_IE        0x00000020
/* Host D0I3 Interrupt Status */
#define H_D0I3C_IS        0x00000040

/* H_CSR masks */
#define H_CSR_IE_MASK     (H_IE | H_D0I3C_IE)
#define H_CSR_IS_MASK     (H_IS | H_D0I3C_IS)

/* register bits of ME_CSR_HA (ME Control Status Host Access register) */
/* ME CB (Circular Buffer) Depth HRA (Host Read Access) - host read only
access to ME_CBD */
#define ME_CBD_HRA        0xFF000000
/* ME CB Write Pointer HRA - host read only access to ME_CBWP */
#define ME_CBWP_HRA       0x00FF0000
/* ME CB Read Pointer HRA - host read only access to ME_CBRP */
#define ME_CBRP_HRA       0x0000FF00
/* ME Power Gate Isolation Capability HRA  - host ready only access */
#define ME_PGIC_HRA       0x00000040
/* ME Reset HRA - host read only access to ME_RST */
#define ME_RST_HRA        0x00000010
/* ME Ready HRA - host read only access to ME_RDY */
#define ME_RDY_HRA        0x00000008
/* ME Interrupt Generate HRA - host read only access to ME_IG */
#define ME_IG_HRA         0x00000004
/* ME Interrupt Status HRA - host read only access to ME_IS */
#define ME_IS_HRA         0x00000002
/* ME Interrupt Enable HRA - host read only access to ME_IE */
#define ME_IE_HRA         0x00000001
/* TRC control shadow register */
#define ME_TRC            0x00000030

/* H_HPG_CSR register bits */
#define H_HPG_CSR_PGIHEXR 0x00000001
#define H_HPG_CSR_PGI     0x00000002

/* H_D0I3C register bits */
#define H_D0I3C_CIP      0x00000001
#define H_D0I3C_IR       0x00000002
#define H_D0I3C_I3       0x00000004
#define H_D0I3C_RR       0x00000008

#endif /* _MEI_HW_MEI_REGS_H_ */
