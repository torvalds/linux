/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gh100_dev_fb_h_
#define __gh100_dev_fb_h_

#define NV_PFB_NISO_FLUSH_SYSMEM_ADDR_SHIFT                       8 /*       */
#define NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO           0x00100A34 /* RW-4R */
#define NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO_ADR             31:0 /* RWIVF */
#define NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI           0x00100A38 /* RW-4R */
#define NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI_ADR             31:0 /* RWIVF */
#define NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI_ADR_MASK  0x000FFFFF /* ----V */

#endif // __gh100_dev_fb_h_
