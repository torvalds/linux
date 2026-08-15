/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2003-2022, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */
#ifndef _MEI_HW_MEI_REGS_H_
#define _MEI_HW_MEI_REGS_H_

#include <linux/bits.h>

/*
 * MEI device IDs
 */
#define PCI_DEVICE_ID_INTEL_MEI_82946GZ    0x2974  /* 82946GZ/GL */
#define PCI_DEVICE_ID_INTEL_MEI_82G35      0x2984  /* 82G35 Express */
#define PCI_DEVICE_ID_INTEL_MEI_82Q965     0x2994  /* 82Q963/Q965 */
#define PCI_DEVICE_ID_INTEL_MEI_82G965     0x29A4  /* 82P965/G965 */

#define PCI_DEVICE_ID_INTEL_MEI_82GM965    0x2A04  /* Mobile PM965/GM965 */
#define PCI_DEVICE_ID_INTEL_MEI_82GME965   0x2A14  /* Mobile GME965/GLE960 */

#define PCI_DEVICE_ID_INTEL_MEI_ICH9_82Q35 0x29B4  /* 82Q35 Express */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_82G33 0x29C4  /* 82G33/G31/P35/P31 Express */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_82Q33 0x29D4  /* 82Q33 Express */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_82X38 0x29E4  /* 82X38/X48 Express */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_3200  0x29F4  /* 3200/3210 Server */

#define PCI_DEVICE_ID_INTEL_MEI_ICH9_6     0x28B4  /* Bearlake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_7     0x28C4  /* Bearlake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_8     0x28D4  /* Bearlake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_9     0x28E4  /* Bearlake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9_10    0x28F4  /* Bearlake */

#define PCI_DEVICE_ID_INTEL_MEI_ICH9M_1    0x2A44  /* Cantiga */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9M_2    0x2A54  /* Cantiga */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9M_3    0x2A64  /* Cantiga */
#define PCI_DEVICE_ID_INTEL_MEI_ICH9M_4    0x2A74  /* Cantiga */

#define PCI_DEVICE_ID_INTEL_MEI_ICH10_1    0x2E04  /* Eaglelake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH10_2    0x2E14  /* Eaglelake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH10_3    0x2E24  /* Eaglelake */
#define PCI_DEVICE_ID_INTEL_MEI_ICH10_4    0x2E34  /* Eaglelake */

#define PCI_DEVICE_ID_INTEL_MEI_IBXPK_1    0x3B64  /* Calpella */
#define PCI_DEVICE_ID_INTEL_MEI_IBXPK_2    0x3B65  /* Calpella */

#define PCI_DEVICE_ID_INTEL_MEI_CPT_1      0x1C3A  /* Couger Point */
#define PCI_DEVICE_ID_INTEL_MEI_PBG_1      0x1D3A  /* C600/X79 Patsburg */

#define PCI_DEVICE_ID_INTEL_MEI_PPT_1      0x1E3A  /* Panther Point */
#define PCI_DEVICE_ID_INTEL_MEI_PPT_2      0x1CBA  /* Panther Point */
#define PCI_DEVICE_ID_INTEL_MEI_PPT_3      0x1DBA  /* Panther Point */

#define PCI_DEVICE_ID_INTEL_MEI_LPT_H      0x8C3A  /* Lynx Point H */
#define PCI_DEVICE_ID_INTEL_MEI_LPT_W      0x8D3A  /* Lynx Point - Wellsburg */
#define PCI_DEVICE_ID_INTEL_MEI_LPT_LP     0x9C3A  /* Lynx Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_LPT_HR     0x8CBA  /* Lynx Point H Refresh */

#define PCI_DEVICE_ID_INTEL_MEI_WPT_LP     0x9CBA  /* Wildcat Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_WPT_LP_2   0x9CBB  /* Wildcat Point LP 2 */

#define PCI_DEVICE_ID_INTEL_MEI_SPT        0x9D3A  /* Sunrise Point */
#define PCI_DEVICE_ID_INTEL_MEI_SPT_2      0x9D3B  /* Sunrise Point 2 */
#define PCI_DEVICE_ID_INTEL_MEI_SPT_3      0x9D3E  /* Sunrise Point 3 (iToutch) */
#define PCI_DEVICE_ID_INTEL_MEI_SPT_H      0xA13A  /* Sunrise Point H */
#define PCI_DEVICE_ID_INTEL_MEI_SPT_H_2    0xA13B  /* Sunrise Point H 2 */

#define PCI_DEVICE_ID_INTEL_MEI_LBG        0xA1BA  /* Lewisburg (SPT) */

#define PCI_DEVICE_ID_INTEL_MEI_BXT_M      0x1A9A  /* Broxton M */
#define PCI_DEVICE_ID_INTEL_MEI_APL_I      0x5A9A  /* Apollo Lake I */

#define PCI_DEVICE_ID_INTEL_MEI_DNV_IE     0x19E5  /* Denverton IE */

#define PCI_DEVICE_ID_INTEL_MEI_GLK        0x319A  /* Gemini Lake */

#define PCI_DEVICE_ID_INTEL_MEI_KBP        0xA2BA  /* Kaby Point */
#define PCI_DEVICE_ID_INTEL_MEI_KBP_2      0xA2BB  /* Kaby Point 2 */
#define PCI_DEVICE_ID_INTEL_MEI_KBP_3      0xA2BE  /* Kaby Point 3 (iTouch) */

#define PCI_DEVICE_ID_INTEL_MEI_CNP_LP     0x9DE0  /* Cannon Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_CNP_LP_3   0x9DE4  /* Cannon Point LP 3 (iTouch) */
#define PCI_DEVICE_ID_INTEL_MEI_CNP_H      0xA360  /* Cannon Point H */
#define PCI_DEVICE_ID_INTEL_MEI_CNP_H_3    0xA364  /* Cannon Point H 3 (iTouch) */

#define PCI_DEVICE_ID_INTEL_MEI_CMP_LP     0x02e0  /* Comet Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_CMP_LP_3   0x02e4  /* Comet Point LP 3 (iTouch) */

#define PCI_DEVICE_ID_INTEL_MEI_CMP_V      0xA3BA  /* Comet Point Lake V */

#define PCI_DEVICE_ID_INTEL_MEI_CMP_H      0x06e0  /* Comet Lake H */
#define PCI_DEVICE_ID_INTEL_MEI_CMP_H_3    0x06e4  /* Comet Lake H 3 (iTouch) */

#define PCI_DEVICE_ID_INTEL_MEI_CDF        0x18D3  /* Cedar Fork */

#define PCI_DEVICE_ID_INTEL_MEI_ICP_LP     0x34E0  /* Ice Lake Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_ICP_N      0x38E0  /* Ice Lake Point N */

#define PCI_DEVICE_ID_INTEL_MEI_JSP_N      0x4DE0  /* Jasper Lake Point N */

#define PCI_DEVICE_ID_INTEL_MEI_TGP_LP     0xA0E0  /* Tiger Lake Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_TGP_H      0x43E0  /* Tiger Lake Point H */

#define PCI_DEVICE_ID_INTEL_MEI_MCC        0x4B70  /* Mule Creek Canyon (EHL) */
#define PCI_DEVICE_ID_INTEL_MEI_MCC_4      0x4B75  /* Mule Creek Canyon 4 (EHL) */

#define PCI_DEVICE_ID_INTEL_MEI_EBG        0x1BE0  /* Emmitsburg WS */

#define PCI_DEVICE_ID_INTEL_MEI_ADP_S      0x7AE8  /* Alder Lake Point S */
#define PCI_DEVICE_ID_INTEL_MEI_ADP_LP     0x7A60  /* Alder Lake Point LP */
#define PCI_DEVICE_ID_INTEL_MEI_ADP_P      0x51E0  /* Alder Lake Point P */
#define PCI_DEVICE_ID_INTEL_MEI_ADP_N      0x54E0  /* Alder Lake Point N */

#define PCI_DEVICE_ID_INTEL_MEI_RPL_S      0x7A68  /* Raptor Lake Point S */

#define PCI_DEVICE_ID_INTEL_MEI_MTL_M      0x7E70  /* Meteor Lake Point M */
#define PCI_DEVICE_ID_INTEL_MEI_ARL_S      0x7F68  /* Arrow Lake Point S */
#define PCI_DEVICE_ID_INTEL_MEI_ARL_H      0x7770  /* Arrow Lake Point H */

#define PCI_DEVICE_ID_INTEL_MEI_LNL_M      0xA870  /* Lunar Lake Point M */

#define PCI_DEVICE_ID_INTEL_MEI_PTL_H      0xE370  /* Panther Lake H */
#define PCI_DEVICE_ID_INTEL_MEI_PTL_P      0xE470  /* Panther Lake P */

#define PCI_DEVICE_ID_INTEL_MEI_WCL_P      0x4D70  /* Wildcat Lake P */

#define PCI_DEVICE_ID_INTEL_MEI_NVL_S      0x6E68  /* Nova Lake Point S */
#define PCI_DEVICE_ID_INTEL_MEI_NVL_H      0xD370  /* Nova Lake Point H */

#define PCI_DEVICE_ID_INTEL_MEI_CRI        0x6766  /* Crescent Island */

/*
 * MEI HW Section
 */

/* Host Firmware Status Registers in PCI Config Space */
#define PCI_CFG_HFS_1         0x40
#  define PCI_CFG_HFS_1_D0I3_MSK     0x80000000
#  define PCI_CFG_HFS_1_OPMODE_MSK 0xf0000 /* OP MODE Mask: SPS <= 4.0 */
#  define PCI_CFG_HFS_1_OPMODE_SPS 0xf0000 /* SPS SKU : SPS <= 4.0 */
#define PCI_CFG_HFS_2         0x48
#  define PCI_CFG_HFS_2_D3_BLOCK              BIT(7)
#  define PCI_CFG_HFS_2_PM_CMOFF_TO_CMX_ERROR 0x1000000 /* CMoff->CMx wake after an error */
#  define PCI_CFG_HFS_2_PM_CM_RESET_ERROR     0x5000000 /* CME reset due to exception */
#  define PCI_CFG_HFS_2_PM_EVENT_MASK         0xf000000
#define PCI_CFG_HFS_3         0x60
#  define PCI_CFG_HFS_3_EXT_SKU_MSK  GENMASK(3, 0) /* IOE detection bits */
#  define PCI_CFG_HFS_3_EXT_SKU_IOE  0x00000001
#  define PCI_CFG_HFS_3_FW_SKU_MSK   0x00000070
#  define PCI_CFG_HFS_3_FW_SKU_IGN   0x00000000
#  define PCI_CFG_HFS_3_FW_SKU_SPS   0x00000060
#define PCI_CFG_HFS_4         0x64
#define PCI_CFG_HFS_5         0x68
#  define GSC_CFG_HFS_5_BOOT_TYPE_MSK      0x00000003
#  define GSC_CFG_HFS_5_BOOT_TYPE_PXP               3
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

#define H_GSC_EXT_OP_MEM_BASE_ADDR_LO_REG 0x100
#define H_GSC_EXT_OP_MEM_BASE_ADDR_HI_REG 0x104
#define H_GSC_EXT_OP_MEM_LIMIT_REG        0x108
#define GSC_EXT_OP_MEM_VALID              BIT(31)

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
