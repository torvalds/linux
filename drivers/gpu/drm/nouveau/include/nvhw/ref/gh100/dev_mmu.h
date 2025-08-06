/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gh100_dev_mmu_h__
#define __gh100_dev_mmu_h__

#define NV_MMU_PTE                                                      /* ----G */
#define NV_MMU_PTE_APERTURE                           (1*32+2):(1*32+1) /* RWXVF */
#define NV_MMU_PTE_APERTURE_VIDEO_MEMORY                     0x00000000 /* RW--V */
#define NV_MMU_PTE_APERTURE_PEER_MEMORY                      0x00000001 /* RW--V */
#define NV_MMU_PTE_APERTURE_SYSTEM_COHERENT_MEMORY           0x00000002 /* RW--V */
#define NV_MMU_PTE_APERTURE_SYSTEM_NON_COHERENT_MEMORY       0x00000003 /* RW--V */
#define NV_MMU_PTE_KIND                              (1*32+7):(1*32+4) /* RWXVF */
#define NV_MMU_PTE_KIND_INVALID                       0x07 /* R---V */
#define NV_MMU_PTE_KIND_PITCH                         0x00 /* R---V */
#define NV_MMU_PTE_KIND_GENERIC_MEMORY                                                  0x6 /* R---V */
#define NV_MMU_PTE_KIND_Z16                                                             0x1 /* R---V */
#define NV_MMU_PTE_KIND_S8                                                              0x2 /* R---V */
#define NV_MMU_PTE_KIND_S8Z24                                                           0x3 /* R---V */
#define NV_MMU_PTE_KIND_ZF32_X24S8                                                      0x4 /* R---V */
#define NV_MMU_PTE_KIND_Z24S8                                                           0x5 /* R---V */
#define NV_MMU_PTE_KIND_GENERIC_MEMORY_COMPRESSIBLE                                     0x8 /* R---V */
#define NV_MMU_PTE_KIND_GENERIC_MEMORY_COMPRESSIBLE_DISABLE_PLC                         0x9 /* R---V */
#define NV_MMU_PTE_KIND_S8_COMPRESSIBLE_DISABLE_PLC                                     0xA /* R---V */
#define NV_MMU_PTE_KIND_Z16_COMPRESSIBLE_DISABLE_PLC                                    0xB /* R---V */
#define NV_MMU_PTE_KIND_S8Z24_COMPRESSIBLE_DISABLE_PLC                                  0xC /* R---V */
#define NV_MMU_PTE_KIND_ZF32_X24S8_COMPRESSIBLE_DISABLE_PLC                             0xD /* R---V */
#define NV_MMU_PTE_KIND_Z24S8_COMPRESSIBLE_DISABLE_PLC                                  0xE /* R---V */
#define NV_MMU_PTE_KIND_SMSKED_MESSAGE                                                  0xF /* R---V */

#define NV_MMU_VER3_PDE                                                      /* ----G */
#define NV_MMU_VER3_PDE_IS_PTE                                           0:0 /* RWXVF */
#define NV_MMU_VER3_PDE_IS_PTE_TRUE                                      0x1 /* RW--V */
#define NV_MMU_VER3_PDE_IS_PTE_FALSE                                     0x0 /* RW--V */
#define NV_MMU_VER3_PDE_VALID                                            0:0 /* RWXVF */
#define NV_MMU_VER3_PDE_VALID_TRUE                                       0x1 /* RW--V */
#define NV_MMU_VER3_PDE_VALID_FALSE                                      0x0 /* RW--V */
#define NV_MMU_VER3_PDE_APERTURE                                         2:1 /* RWXVF */
#define NV_MMU_VER3_PDE_APERTURE_INVALID                          0x00000000 /* RW--V */
#define NV_MMU_VER3_PDE_APERTURE_VIDEO_MEMORY                     0x00000001 /* RW--V */
#define NV_MMU_VER3_PDE_APERTURE_SYSTEM_COHERENT_MEMORY           0x00000002 /* RW--V */
#define NV_MMU_VER3_PDE_APERTURE_SYSTEM_NON_COHERENT_MEMORY       0x00000003 /* RW--V */
#define NV_MMU_VER3_PDE_PCF                                                                        5:3 /* RWXVF */
#define NV_MMU_VER3_PDE_PCF_VALID_CACHED_ATS_ALLOWED__OR__INVALID_ATS_ALLOWED               0x00000000 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_CACHED_ATS_ALLOWED                                        0x00000000 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_INVALID_ATS_ALLOWED                                             0x00000000 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_UNCACHED_ATS_ALLOWED__OR__SPARSE_ATS_ALLOWED              0x00000001 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_UNCACHED_ATS_ALLOWED                                      0x00000001 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_SPARSE_ATS_ALLOWED                                              0x00000001 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_CACHED_ATS_NOT_ALLOWED__OR__INVALID_ATS_NOT_ALLOWED       0x00000002 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_CACHED_ATS_NOT_ALLOWED                                    0x00000002 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_INVALID_ATS_NOT_ALLOWED                                         0x00000002 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_UNCACHED_ATS_NOT_ALLOWED__OR__SPARSE_ATS_NOT_ALLOWED      0x00000003 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_VALID_UNCACHED_ATS_NOT_ALLOWED                                  0x00000003 /* RW--V */
#define NV_MMU_VER3_PDE_PCF_SPARSE_ATS_NOT_ALLOWED                                          0x00000003 /* RW--V */
#define NV_MMU_VER3_PDE_ADDRESS                                             51:12 /* RWXVF */
#define NV_MMU_VER3_PDE_ADDRESS_SHIFT                                  0x0000000c /*       */
#define NV_MMU_VER3_PDE__SIZE                                              8

#define NV_MMU_VER3_DUAL_PDE                                                      /* ----G */
#define NV_MMU_VER3_DUAL_PDE_IS_PTE                                           0:0 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_IS_PTE_TRUE                                      0x1 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_IS_PTE_FALSE                                     0x0 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_VALID                                            0:0 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_VALID_TRUE                                       0x1 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_VALID_FALSE                                      0x0 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_BIG                                     2:1 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_BIG_INVALID                      0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_BIG_VIDEO_MEMORY                 0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_BIG_SYSTEM_COHERENT_MEMORY       0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_BIG_SYSTEM_NON_COHERENT_MEMORY   0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG                                                                        5:3 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_CACHED_ATS_ALLOWED__OR__INVALID_ATS_ALLOWED               0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_CACHED_ATS_ALLOWED                                        0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_INVALID_ATS_ALLOWED                                             0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_UNCACHED_ATS_ALLOWED__OR__SPARSE_ATS_ALLOWED              0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_UNCACHED_ATS_ALLOWED                                      0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_SPARSE_ATS_ALLOWED                                              0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_CACHED_ATS_NOT_ALLOWED__OR__INVALID_ATS_NOT_ALLOWED       0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_CACHED_ATS_NOT_ALLOWED                                    0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_INVALID_ATS_NOT_ALLOWED                                         0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_UNCACHED_ATS_NOT_ALLOWED__OR__SPARSE_ATS_NOT_ALLOWED      0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_VALID_UNCACHED_ATS_NOT_ALLOWED                                  0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_BIG_SPARSE_ATS_NOT_ALLOWED                                          0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_ADDRESS_BIG                                     51:8 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_SMALL                                 66:65 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_SMALL_INVALID                    0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_SMALL_VIDEO_MEMORY               0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_SMALL_SYSTEM_COHERENT_MEMORY     0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_APERTURE_SMALL_SYSTEM_NON_COHERENT_MEMORY 0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL                                                                      69:67 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_CACHED_ATS_ALLOWED__OR__INVALID_ATS_ALLOWED               0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_CACHED_ATS_ALLOWED                                        0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_INVALID_ATS_ALLOWED                                             0x00000000 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_UNCACHED_ATS_ALLOWED__OR__SPARSE_ATS_ALLOWED              0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_UNCACHED_ATS_ALLOWED                                      0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_SPARSE_ATS_ALLOWED                                              0x00000001 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_CACHED_ATS_NOT_ALLOWED__OR__INVALID_ATS_NOT_ALLOWED       0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_CACHED_ATS_NOT_ALLOWED                                    0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_INVALID_ATS_NOT_ALLOWED                                         0x00000002 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_UNCACHED_ATS_NOT_ALLOWED__OR__SPARSE_ATS_NOT_ALLOWED      0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_VALID_UNCACHED_ATS_NOT_ALLOWED                                  0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_PCF_SMALL_SPARSE_ATS_NOT_ALLOWED                                          0x00000003 /* RW--V */
#define NV_MMU_VER3_DUAL_PDE_ADDRESS_SMALL                                 115:76 /* RWXVF */
#define NV_MMU_VER3_DUAL_PDE_ADDRESS_SHIFT                             0x0000000c /*       */
#define NV_MMU_VER3_DUAL_PDE_ADDRESS_BIG_SHIFT 8 /*       */
#define NV_MMU_VER3_DUAL_PDE__SIZE                                             16

#define NV_MMU_VER3_PTE                                                      /* ----G */
#define NV_MMU_VER3_PTE_VALID                                            0:0 /* RWXVF */
#define NV_MMU_VER3_PTE_VALID_TRUE                                       0x1 /* RW--V */
#define NV_MMU_VER3_PTE_VALID_FALSE                                      0x0 /* RW--V */
#define NV_MMU_VER3_PTE_APERTURE                                         2:1 /* RWXVF */
#define NV_MMU_VER3_PTE_APERTURE_VIDEO_MEMORY                     0x00000000 /* RW--V */
#define NV_MMU_VER3_PTE_APERTURE_PEER_MEMORY                      0x00000001 /* RW--V */
#define NV_MMU_VER3_PTE_APERTURE_SYSTEM_COHERENT_MEMORY           0x00000002 /* RW--V */
#define NV_MMU_VER3_PTE_APERTURE_SYSTEM_NON_COHERENT_MEMORY       0x00000003 /* RW--V */
#define NV_MMU_VER3_PTE_PCF                                                                        7:3 /* RWXVF */
#define NV_MMU_VER3_PTE_PCF_INVALID                                                         0x00000000 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_SPARSE                                                          0x00000001 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_MAPPING_NOWHERE                                                 0x00000002 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_NO_VALID_4KB_PAGE                                               0x00000003 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_ATOMIC_CACHED_ACE                                    0x00000000 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_ATOMIC_UNCACHED_ACE                                  0x00000001 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_ATOMIC_CACHED_ACE                                  0x00000002 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_ATOMIC_UNCACHED_ACE                                0x00000003 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_ATOMIC_CACHED_ACE                                    0x00000004 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_ATOMIC_UNCACHED_ACE                                   0x00000005 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_ATOMIC_CACHED_ACE                                  0x00000006 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_ATOMIC_UNCACHED_ACE                                0x00000007 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_NO_ATOMIC_CACHED_ACE                                 0x00000008 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_NO_ATOMIC_UNCACHED_ACE                               0x00000009 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_NO_ATOMIC_CACHED_ACE                               0x0000000A /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_NO_ATOMIC_UNCACHED_ACE                             0x0000000B /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_NO_ATOMIC_CACHED_ACE                                 0x0000000C /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_NO_ATOMIC_UNCACHED_ACE                               0x0000000D /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_NO_ATOMIC_CACHED_ACE                               0x0000000E /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_NO_ATOMIC_UNCACHED_ACE                             0x0000000F /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_ATOMIC_CACHED_ACD                                    0x00000010 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_ATOMIC_UNCACHED_ACD                                  0x00000011 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_ATOMIC_CACHED_ACD                                  0x00000012 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_ATOMIC_UNCACHED_ACD                                0x00000013 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_ATOMIC_CACHED_ACD                                    0x00000014 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_ATOMIC_UNCACHED_ACD                                  0x00000015 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_ATOMIC_CACHED_ACD                                  0x00000016 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_ATOMIC_UNCACHED_ACD                                0x00000017 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_NO_ATOMIC_CACHED_ACD                                 0x00000018 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RW_NO_ATOMIC_UNCACHED_ACD                               0x00000019 /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_NO_ATOMIC_CACHED_ACD                               0x0000001A /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_NO_ATOMIC_UNCACHED_ACD                             0x0000001B /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_NO_ATOMIC_CACHED_ACD                                 0x0000001C /* RW--V */
#define NV_MMU_VER3_PTE_PCF_REGULAR_RO_NO_ATOMIC_UNCACHED_ACD                               0x0000001D /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_NO_ATOMIC_CACHED_ACD                               0x0000001E /* RW--V */
#define NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_NO_ATOMIC_UNCACHED_ACD                             0x0000001F /* RW--V */
#define NV_MMU_VER3_PTE_KIND                                           11:8 /* RWXVF */
#define NV_MMU_VER3_PTE_ADDRESS                                         51:12 /* RWXVF */
#define NV_MMU_VER3_PTE_ADDRESS_SYS                                     51:12 /* RWXVF */
#define NV_MMU_VER3_PTE_ADDRESS_PEER                                    51:12 /* RWXVF */
#define NV_MMU_VER3_PTE_ADDRESS_VID                                     39:12 /* RWXVF */
#define NV_MMU_VER3_PTE_PEER_ID                63:(64-3) /* RWXVF */
#define NV_MMU_VER3_PTE_PEER_ID_0                                 0x00000000 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_1                                 0x00000001 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_2                                 0x00000002 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_3                                 0x00000003 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_4                                 0x00000004 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_5                                 0x00000005 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_6                                 0x00000006 /* RW--V */
#define NV_MMU_VER3_PTE_PEER_ID_7                                 0x00000007 /* RW--V */
#define NV_MMU_VER3_PTE_ADDRESS_SHIFT                             0x0000000c /*       */
#define NV_MMU_VER3_PTE__SIZE                                              8

#endif // __gh100_dev_mmu_h__
