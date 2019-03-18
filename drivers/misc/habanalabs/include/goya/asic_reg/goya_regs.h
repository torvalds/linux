/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef ASIC_REG_GOYA_REGS_H_
#define ASIC_REG_GOYA_REGS_H_

#include "goya_blocks.h"
#include "stlb_regs.h"
#include "mmu_regs.h"
#include "pcie_aux_regs.h"
#include "psoc_global_conf_regs.h"
#include "psoc_spi_regs.h"
#include "psoc_mme_pll_regs.h"
#include "psoc_pci_pll_regs.h"
#include "psoc_emmc_pll_regs.h"
#include "cpu_if_regs.h"
#include "cpu_ca53_cfg_regs.h"
#include "cpu_pll_regs.h"
#include "ic_pll_regs.h"
#include "mc_pll_regs.h"
#include "tpc_pll_regs.h"
#include "dma_qm_0_regs.h"
#include "dma_qm_1_regs.h"
#include "dma_qm_2_regs.h"
#include "dma_qm_3_regs.h"
#include "dma_qm_4_regs.h"
#include "dma_ch_0_regs.h"
#include "dma_ch_1_regs.h"
#include "dma_ch_2_regs.h"
#include "dma_ch_3_regs.h"
#include "dma_ch_4_regs.h"
#include "dma_macro_regs.h"
#include "dma_nrtr_regs.h"
#include "pci_nrtr_regs.h"
#include "sram_y0_x0_rtr_regs.h"
#include "sram_y0_x1_rtr_regs.h"
#include "sram_y0_x2_rtr_regs.h"
#include "sram_y0_x3_rtr_regs.h"
#include "sram_y0_x4_rtr_regs.h"
#include "mme_regs.h"
#include "mme_qm_regs.h"
#include "mme_cmdq_regs.h"
#include "mme1_rtr_regs.h"
#include "mme2_rtr_regs.h"
#include "mme3_rtr_regs.h"
#include "mme4_rtr_regs.h"
#include "mme5_rtr_regs.h"
#include "mme6_rtr_regs.h"
#include "tpc0_cfg_regs.h"
#include "tpc1_cfg_regs.h"
#include "tpc2_cfg_regs.h"
#include "tpc3_cfg_regs.h"
#include "tpc4_cfg_regs.h"
#include "tpc5_cfg_regs.h"
#include "tpc6_cfg_regs.h"
#include "tpc7_cfg_regs.h"
#include "tpc0_qm_regs.h"
#include "tpc1_qm_regs.h"
#include "tpc2_qm_regs.h"
#include "tpc3_qm_regs.h"
#include "tpc4_qm_regs.h"
#include "tpc5_qm_regs.h"
#include "tpc6_qm_regs.h"
#include "tpc7_qm_regs.h"
#include "tpc0_cmdq_regs.h"
#include "tpc1_cmdq_regs.h"
#include "tpc2_cmdq_regs.h"
#include "tpc3_cmdq_regs.h"
#include "tpc4_cmdq_regs.h"
#include "tpc5_cmdq_regs.h"
#include "tpc6_cmdq_regs.h"
#include "tpc7_cmdq_regs.h"
#include "tpc0_nrtr_regs.h"
#include "tpc1_rtr_regs.h"
#include "tpc2_rtr_regs.h"
#include "tpc3_rtr_regs.h"
#include "tpc4_rtr_regs.h"
#include "tpc5_rtr_regs.h"
#include "tpc6_rtr_regs.h"
#include "tpc7_nrtr_regs.h"
#include "tpc0_eml_cfg_regs.h"

#include "psoc_global_conf_masks.h"
#include "dma_macro_masks.h"
#include "dma_qm_0_masks.h"
#include "tpc0_qm_masks.h"
#include "tpc0_cmdq_masks.h"
#include "mme_qm_masks.h"
#include "mme_cmdq_masks.h"
#include "tpc0_cfg_masks.h"
#include "tpc0_eml_cfg_masks.h"
#include "mme1_rtr_masks.h"
#include "tpc0_nrtr_masks.h"
#include "dma_nrtr_masks.h"
#include "pci_nrtr_masks.h"
#include "stlb_masks.h"
#include "cpu_ca53_cfg_masks.h"
#include "mmu_masks.h"
#include "mme_masks.h"

#define mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG                           0xC02000
#define mmPCIE_DBI_MSIX_DOORBELL_OFF                                 0xC02948

#define mmSYNC_MNGR_MON_PAY_ADDRL_0                                  0x113000
#define mmSYNC_MNGR_SOB_OBJ_0                                        0x112000
#define mmSYNC_MNGR_SOB_OBJ_1000                                     0x112FA0
#define mmSYNC_MNGR_SOB_OBJ_1007                                     0x112FBC
#define mmSYNC_MNGR_SOB_OBJ_1023                                     0x112FFC
#define mmSYNC_MNGR_MON_STATUS_0                                     0x114000
#define mmSYNC_MNGR_MON_STATUS_255                                   0x1143FC

#define mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR                         0x800040

#endif /* ASIC_REG_GOYA_REGS_H_ */
