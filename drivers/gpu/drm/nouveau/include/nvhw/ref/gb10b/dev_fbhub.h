/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gb10b_dev_fb_h__
#define __gb10b_dev_fb_h__

#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_LO                                0x008a1d58 /* RW-4R */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_LO_ADR                                  31:0 /* RWIVF */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_LO_ADR_INIT                       0x00000000 /* RWI-V */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_LO_ADR_MASK                       0xffffff00 /* RW--V */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_HI                                0x008a1d5c /* RW-4R */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_HI_ADR                                  31:0 /* RWIVF */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_HI_ADR_INIT                       0x00000000 /* RWI-V */
#define NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_HI_ADR_MASK                       0x000fffff /* RW--V */

#endif // __gb10b_dev_fb_h__

