/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef _mmhub_4_2_0_SH_MASK_HEADER
#define _mmhub_4_2_0_SH_MASK_HEADER


// addressBlock: mmhub_dagb_dagbdec
//DAGB0_CNTL_MISC2
#define DAGB0_CNTL_MISC2__DAGB_BUSY_OVERRIDE__SHIFT                                                           0x0
#define DAGB0_CNTL_MISC2__SWAP_CTL__SHIFT                                                                     0x1
#define DAGB0_CNTL_MISC2__RDRET_FIFO_PERF__SHIFT                                                              0x2
#define DAGB0_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG__SHIFT                                                 0x3
#define DAGB0_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG__SHIFT                                                 0x4
#define DAGB0_CNTL_MISC2__DISABLE_MCA_INTR_REQ_FGCG__SHIFT                                                    0x5
#define DAGB0_CNTL_MISC2__SWAP_CTL_MASK                                                                       0x00000002L
#define DAGB0_CNTL_MISC2__RDRET_FIFO_PERF_MASK                                                                0x00000004L
#define DAGB0_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG_MASK                                                   0x00000008L
#define DAGB0_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG_MASK                                                   0x00000010L
//DAGB1_CNTL_MISC2
#define DAGB1_CNTL_MISC2__DAGB_BUSY_OVERRIDE__SHIFT                                                           0x0
#define DAGB1_CNTL_MISC2__SWAP_CTL__SHIFT                                                                     0x1
#define DAGB1_CNTL_MISC2__RDRET_FIFO_PERF__SHIFT                                                              0x2
#define DAGB1_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG__SHIFT                                                 0x3
#define DAGB1_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG__SHIFT                                                 0x4
#define DAGB1_CNTL_MISC2__DISABLE_MCA_INTR_REQ_FGCG__SHIFT                                                    0x5
#define DAGB1_CNTL_MISC2__SWAP_CTL_MASK                                                                       0x00000002L
#define DAGB1_CNTL_MISC2__RDRET_FIFO_PERF_MASK                                                                0x00000004L
#define DAGB1_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG_MASK                                                   0x00000008L
#define DAGB1_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG_MASK                                                   0x00000010L


// addressBlock: mmhub_mm_cane_mmcanedec
//MM_CANE_ICG_CTRL
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_IREQ0__SHIFT                                                          0x0
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_ATRET__SHIFT                                                          0x1
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_OREQ__SHIFT                                                           0x2
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_REGISTER__SHIFT                                                       0x3
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_SDPM_RETURN__SHIFT                                                    0x4
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_IREQ0_MASK                                                            0x00000001L
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_ATRET_MASK                                                            0x00000002L
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_OREQ_MASK                                                             0x00000004L
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_REGISTER_MASK                                                         0x00000008L
#define MM_CANE_ICG_CTRL__SOFT_OVERRIDE_SDPM_RETURN_MASK                                                      0x00000010L


// addressBlock: mmhub_mmutcl2_mmvmsharedpfdec
//MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB
#define MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB__PHYSICAL_PAGE_NUMBER_LSB__SHIFT                             0x0
#define MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB__PHYSICAL_PAGE_NUMBER_LSB_MASK                               0xFFFFFFFFL
//MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB
#define MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB__PHYSICAL_PAGE_NUMBER_MSB__SHIFT                             0x0
#define MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB__PHYSICAL_PAGE_NUMBER_MSB_MASK                               0x000000FFL
//MMUTCL2_CGTT_CLK_CTRL
#define MMUTCL2_CGTT_CLK_CTRL__ON_DELAY__SHIFT                                                                0x0
#define MMUTCL2_CGTT_CLK_CTRL__OFF_HYSTERESIS__SHIFT                                                          0x5
#define MMUTCL2_CGTT_CLK_CTRL__LS_ASSERT_HYSTERESIS__SHIFT                                                    0xd
#define MMUTCL2_CGTT_CLK_CTRL__MIN_MGLS__SHIFT                                                                0x1a
#define MMUTCL2_CGTT_CLK_CTRL__CGLS_DISABLE__SHIFT                                                            0x1d
#define MMUTCL2_CGTT_CLK_CTRL__LS_DISABLE__SHIFT                                                              0x1e
#define MMUTCL2_CGTT_CLK_CTRL__BUSY_OVERRIDE__SHIFT                                                           0x1f
#define MMUTCL2_CGTT_CLK_CTRL__ON_DELAY_MASK                                                                  0x0000001FL
#define MMUTCL2_CGTT_CLK_CTRL__OFF_HYSTERESIS_MASK                                                            0x00001FE0L
#define MMUTCL2_CGTT_CLK_CTRL__LS_ASSERT_HYSTERESIS_MASK                                                      0x03FFE000L
#define MMUTCL2_CGTT_CLK_CTRL__MIN_MGLS_MASK                                                                  0x1C000000L
#define MMUTCL2_CGTT_CLK_CTRL__CGLS_DISABLE_MASK                                                              0x20000000L
#define MMUTCL2_CGTT_CLK_CTRL__LS_DISABLE_MASK                                                                0x40000000L
#define MMUTCL2_CGTT_CLK_CTRL__BUSY_OVERRIDE_MASK                                                             0x80000000L
//MMMC_SHARED_ACTIVE_FCN_ID
#define MMMC_SHARED_ACTIVE_FCN_ID__VFID__SHIFT                                                                0x0
#define MMMC_SHARED_ACTIVE_FCN_ID__VF__SHIFT                                                                  0x1f
#define MMMC_SHARED_ACTIVE_FCN_ID__VFID_MASK                                                                  0x0000001FL
#define MMMC_SHARED_ACTIVE_FCN_ID__VF_MASK                                                                    0x80000000L
//MMUTCL2_CGTT_BUSY_CTRL
#define MMUTCL2_CGTT_BUSY_CTRL__READ_DELAY__SHIFT                                                             0x0
#define MMUTCL2_CGTT_BUSY_CTRL__ALWAYS_BUSY__SHIFT                                                            0x5
#define MMUTCL2_CGTT_BUSY_CTRL__READ_DELAY_MASK                                                               0x0000001FL
#define MMUTCL2_CGTT_BUSY_CTRL__ALWAYS_BUSY_MASK                                                              0x00000020L
//MMUTCL2_GROUP_RET_FAULT_STATUS
#define MMUTCL2_GROUP_RET_FAULT_STATUS__FAULT_GROUPS__SHIFT                                                   0x0
#define MMUTCL2_GROUP_RET_FAULT_STATUS__FAULT_GROUPS_MASK                                                     0xFFFFFFFFL


// addressBlock: mmhub_mmutcl2_mmvml2pfdec
//MMVM_L2_CNTL
#define MMVM_L2_CNTL__ENABLE_L2_CACHE__SHIFT                                                                  0x0
#define MMVM_L2_CNTL__ENABLE_L2_FRAGMENT_PROCESSING__SHIFT                                                    0x1
#define MMVM_L2_CNTL__L2_CACHE_PTE_ENDIAN_SWAP_MODE__SHIFT                                                    0x2
#define MMVM_L2_CNTL__L2_CACHE_PDE_ENDIAN_SWAP_MODE__SHIFT                                                    0x4
#define MMVM_L2_CNTL__L2_PDE0_CACHE_TAG_GENERATION_MODE__SHIFT                                                0x8
#define MMVM_L2_CNTL__ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE__SHIFT                                          0x9
#define MMVM_L2_CNTL__ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE__SHIFT                                         0xa
#define MMVM_L2_CNTL__ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY__SHIFT                                         0xb
#define MMVM_L2_CNTL__L2_PDE0_CACHE_SPLIT_MODE__SHIFT                                                         0xc
#define MMVM_L2_CNTL__EFFECTIVE_L2_QUEUE_SIZE__SHIFT                                                          0xf
#define MMVM_L2_CNTL__PDE_FAULT_CLASSIFICATION__SHIFT                                                         0x12
#define MMVM_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE__SHIFT                                                    0x13
#define MMVM_L2_CNTL__IDENTITY_MODE_FRAGMENT_SIZE__SHIFT                                                      0x15
#define MMVM_L2_CNTL__L2_PTE_CACHE_ADDR_MODE__SHIFT                                                           0x1a
#define MMVM_L2_CNTL__ENABLE_L2_CACHE_MASK                                                                    0x00000001L
#define MMVM_L2_CNTL__ENABLE_L2_FRAGMENT_PROCESSING_MASK                                                      0x00000002L
#define MMVM_L2_CNTL__L2_CACHE_PTE_ENDIAN_SWAP_MODE_MASK                                                      0x0000000CL
#define MMVM_L2_CNTL__L2_CACHE_PDE_ENDIAN_SWAP_MODE_MASK                                                      0x00000030L
#define MMVM_L2_CNTL__L2_PDE0_CACHE_TAG_GENERATION_MODE_MASK                                                  0x00000100L
#define MMVM_L2_CNTL__ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE_MASK                                            0x00000200L
#define MMVM_L2_CNTL__ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE_MASK                                           0x00000400L
#define MMVM_L2_CNTL__ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY_MASK                                           0x00000800L
#define MMVM_L2_CNTL__L2_PDE0_CACHE_SPLIT_MODE_MASK                                                           0x00007000L
#define MMVM_L2_CNTL__EFFECTIVE_L2_QUEUE_SIZE_MASK                                                            0x00038000L
#define MMVM_L2_CNTL__PDE_FAULT_CLASSIFICATION_MASK                                                           0x00040000L
#define MMVM_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE_MASK                                                      0x00180000L
#define MMVM_L2_CNTL__IDENTITY_MODE_FRAGMENT_SIZE_MASK                                                        0x03E00000L
#define MMVM_L2_CNTL__L2_PTE_CACHE_ADDR_MODE_MASK                                                             0x0C000000L
//MMVM_L2_CNTL2
#define MMVM_L2_CNTL2__INVALIDATE_ALL_L1_TLBS__SHIFT                                                          0x0
#define MMVM_L2_CNTL2__INVALIDATE_L2_CACHE__SHIFT                                                             0x1
#define MMVM_L2_CNTL2__DISABLE_INVALIDATE_PER_DOMAIN__SHIFT                                                   0x15
#define MMVM_L2_CNTL2__DISABLE_BIGK_CACHE_OPTIMIZATION__SHIFT                                                 0x16
#define MMVM_L2_CNTL2__L2_PTE_CACHE_VMID_MODE__SHIFT                                                          0x17
#define MMVM_L2_CNTL2__INVALIDATE_CACHE_MODE__SHIFT                                                           0x1a
#define MMVM_L2_CNTL2__PDE_CACHE_EFFECTIVE_SIZE__SHIFT                                                        0x1c
#define MMVM_L2_CNTL2__INVALIDATE_ALL_L1_TLBS_MASK                                                            0x00000001L
#define MMVM_L2_CNTL2__INVALIDATE_L2_CACHE_MASK                                                               0x00000002L
#define MMVM_L2_CNTL2__DISABLE_INVALIDATE_PER_DOMAIN_MASK                                                     0x00200000L
#define MMVM_L2_CNTL2__DISABLE_BIGK_CACHE_OPTIMIZATION_MASK                                                   0x00400000L
#define MMVM_L2_CNTL2__L2_PTE_CACHE_VMID_MODE_MASK                                                            0x03800000L
#define MMVM_L2_CNTL2__INVALIDATE_CACHE_MODE_MASK                                                             0x0C000000L
#define MMVM_L2_CNTL2__PDE_CACHE_EFFECTIVE_SIZE_MASK                                                          0x70000000L
//MMVM_L2_CNTL3
#define MMVM_L2_CNTL3__BANK_SELECT__SHIFT                                                                     0x0
#define MMVM_L2_CNTL3__L2_CACHE_UPDATE_MODE__SHIFT                                                            0x6
#define MMVM_L2_CNTL3__L2_CACHE_UPDATE_WILDCARD_REFERENCE_VALUE__SHIFT                                        0x8
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                                                     0xf
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_ASSOCIATIVITY__SHIFT                                                     0x14
#define MMVM_L2_CNTL3__L2_CACHE_4K_EFFECTIVE_SIZE__SHIFT                                                      0x15
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_EFFECTIVE_SIZE__SHIFT                                                    0x18
#define MMVM_L2_CNTL3__L2_CACHE_4K_FORCE_MISS__SHIFT                                                          0x1c
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_FORCE_MISS__SHIFT                                                        0x1d
#define MMVM_L2_CNTL3__PDE_CACHE_FORCE_MISS__SHIFT                                                            0x1e
#define MMVM_L2_CNTL3__L2_CACHE_4K_ASSOCIATIVITY__SHIFT                                                       0x1f
#define MMVM_L2_CNTL3__BANK_SELECT_MASK                                                                       0x0000003FL
#define MMVM_L2_CNTL3__L2_CACHE_UPDATE_MODE_MASK                                                              0x000000C0L
#define MMVM_L2_CNTL3__L2_CACHE_UPDATE_WILDCARD_REFERENCE_VALUE_MASK                                          0x00001F00L
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                                                       0x000F8000L
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_ASSOCIATIVITY_MASK                                                       0x00100000L
#define MMVM_L2_CNTL3__L2_CACHE_4K_EFFECTIVE_SIZE_MASK                                                        0x00E00000L
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_EFFECTIVE_SIZE_MASK                                                      0x0F000000L
#define MMVM_L2_CNTL3__L2_CACHE_4K_FORCE_MISS_MASK                                                            0x10000000L
#define MMVM_L2_CNTL3__L2_CACHE_BIGK_FORCE_MISS_MASK                                                          0x20000000L
#define MMVM_L2_CNTL3__PDE_CACHE_FORCE_MISS_MASK                                                              0x40000000L
#define MMVM_L2_CNTL3__L2_CACHE_4K_ASSOCIATIVITY_MASK                                                         0x80000000L
//MMVM_L2_STATUS
#define MMVM_L2_STATUS__L2_BUSY__SHIFT                                                                        0x0
#define MMVM_L2_STATUS__CONTEXT_DOMAIN_BUSY__SHIFT                                                            0x1
#define MMVM_L2_STATUS__FOUND_4K_PTE_CACHE_PARITY_ERRORS__SHIFT                                               0x11
#define MMVM_L2_STATUS__FOUND_BIGK_PTE_CACHE_PARITY_ERRORS__SHIFT                                             0x12
#define MMVM_L2_STATUS__FOUND_PDE0_CACHE_PARITY_ERRORS__SHIFT                                                 0x13
#define MMVM_L2_STATUS__FOUND_PDE1_CACHE_PARITY_ERRORS__SHIFT                                                 0x14
#define MMVM_L2_STATUS__FOUND_PDE2_CACHE_PARITY_ERRORS__SHIFT                                                 0x15
#define MMVM_L2_STATUS__FOUND_PDE3_CACHE_PARITY_ERRORS__SHIFT                                                 0x16
#define MMVM_L2_STATUS__L2_BUSY_MASK                                                                          0x00000001L
#define MMVM_L2_STATUS__CONTEXT_DOMAIN_BUSY_MASK                                                              0x0001FFFEL
#define MMVM_L2_STATUS__FOUND_4K_PTE_CACHE_PARITY_ERRORS_MASK                                                 0x00020000L
#define MMVM_L2_STATUS__FOUND_BIGK_PTE_CACHE_PARITY_ERRORS_MASK                                               0x00040000L
#define MMVM_L2_STATUS__FOUND_PDE0_CACHE_PARITY_ERRORS_MASK                                                   0x00080000L
#define MMVM_L2_STATUS__FOUND_PDE1_CACHE_PARITY_ERRORS_MASK                                                   0x00100000L
#define MMVM_L2_STATUS__FOUND_PDE2_CACHE_PARITY_ERRORS_MASK                                                   0x00200000L
//MMVM_DUMMY_PAGE_FAULT_CNTL
#define MMVM_DUMMY_PAGE_FAULT_CNTL__DUMMY_PAGE_FAULT_ENABLE__SHIFT                                            0x0
#define MMVM_DUMMY_PAGE_FAULT_CNTL__DUMMY_PAGE_ADDRESS_LOGICAL__SHIFT                                         0x1
#define MMVM_DUMMY_PAGE_FAULT_CNTL__DUMMY_PAGE_COMPARE_MSBS__SHIFT                                            0x2
#define MMVM_DUMMY_PAGE_FAULT_CNTL__DUMMY_PAGE_FAULT_ENABLE_MASK                                              0x00000001L
#define MMVM_DUMMY_PAGE_FAULT_CNTL__DUMMY_PAGE_ADDRESS_LOGICAL_MASK                                           0x00000002L
#define MMVM_DUMMY_PAGE_FAULT_CNTL__DUMMY_PAGE_COMPARE_MSBS_MASK                                              0x000000FCL
//MMVM_DUMMY_PAGE_FAULT_ADDR_LO32
#define MMVM_DUMMY_PAGE_FAULT_ADDR_LO32__DUMMY_PAGE_ADDR_LO32__SHIFT                                          0x0
#define MMVM_DUMMY_PAGE_FAULT_ADDR_LO32__DUMMY_PAGE_ADDR_LO32_MASK                                            0xFFFFFFFFL
//MMVM_DUMMY_PAGE_FAULT_ADDR_HI32
#define MMVM_DUMMY_PAGE_FAULT_ADDR_HI32__DUMMY_PAGE_ADDR_HI13__SHIFT                                          0x0
#define MMVM_DUMMY_PAGE_FAULT_ADDR_HI32__DUMMY_PAGE_ADDR_HI13_MASK                                            0x00001FFFL
//MMVM_INVALIDATE_CNTL
#define MMVM_INVALIDATE_CNTL__PRI_REG_ALTERNATING__SHIFT                                                      0x0
#define MMVM_INVALIDATE_CNTL__MAX_REG_OUTSTANDING__SHIFT                                                      0x8
#define MMVM_INVALIDATE_CNTL__PRI_REG_ALTERNATING_MASK                                                        0x000000FFL
#define MMVM_INVALIDATE_CNTL__MAX_REG_OUTSTANDING_MASK                                                        0x0000FF00L
//MMVM_L2_PROTECTION_FAULT_CNTL_LO32
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                         0x0
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__ALLOW_SUBSEQUENT_PROTECTION_FAULT_STATUS_ADDR_UPDATES__SHIFT      0x1
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                      0x2
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                       0x3
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE1_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                       0x4
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE2_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                       0x5
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE3_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                       0x6
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT          0x7
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__NACK_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                       0x8
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                 0x9
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                      0xa
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                       0xb
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                      0xc
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                    0xd
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__CLIENT_ID_NO_RETRY_FAULT_INTERRUPT__SHIFT                         0xe
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__OTHER_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT__SHIFT                   0x1e
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__CRASH_ON_NO_RETRY_FAULT__SHIFT                                    0x1f
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                           0x00000001L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__ALLOW_SUBSEQUENT_PROTECTION_FAULT_STATUS_ADDR_UPDATES_MASK        0x00000002L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                        0x00000004L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                         0x00000008L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE1_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                         0x00000010L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE2_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                         0x00000020L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__PDE3_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                         0x00000040L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT_MASK            0x00000080L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__NACK_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                         0x00000100L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                   0x00000200L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                        0x00000400L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                         0x00000800L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                        0x00001000L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                      0x00002000L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__CLIENT_ID_NO_RETRY_FAULT_INTERRUPT_MASK                           0x3FFFC000L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__OTHER_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT_MASK                     0x40000000L
#define MMVM_L2_PROTECTION_FAULT_CNTL_LO32__CRASH_ON_NO_RETRY_FAULT_MASK                                      0x80000000L
//MMVM_L2_PROTECTION_FAULT_CNTL_HI32
#define MMVM_L2_PROTECTION_FAULT_CNTL_HI32__CRASH_ON_RETRY_FAULT__SHIFT                                       0x0
#define MMVM_L2_PROTECTION_FAULT_CNTL_HI32__CRASH_ON_RETRY_FAULT_MASK                                         0x00000001L
//MMVM_L2_PROTECTION_FAULT_CNTL2
#define MMVM_L2_PROTECTION_FAULT_CNTL2__CLIENT_ID_PRT_FAULT_INTERRUPT__SHIFT                                  0x0
#define MMVM_L2_PROTECTION_FAULT_CNTL2__OTHER_CLIENT_ID_PRT_FAULT_INTERRUPT__SHIFT                            0x10
#define MMVM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE__SHIFT                                      0x11
#define MMVM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY__SHIFT                           0x12
#define MMVM_L2_PROTECTION_FAULT_CNTL2__ENABLE_RETRY_FAULT_INTERRUPT__SHIFT                                   0x13
#define MMVM_L2_PROTECTION_FAULT_CNTL2__CLIENT_ID_PRT_FAULT_INTERRUPT_MASK                                    0x0000FFFFL
#define MMVM_L2_PROTECTION_FAULT_CNTL2__OTHER_CLIENT_ID_PRT_FAULT_INTERRUPT_MASK                              0x00010000L
#define MMVM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_MASK                                        0x00020000L
#define MMVM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY_MASK                             0x00040000L
#define MMVM_L2_PROTECTION_FAULT_CNTL2__ENABLE_RETRY_FAULT_INTERRUPT_MASK                                     0x00080000L
//MMVM_L2_PROTECTION_FAULT_MM_CNTL3
#define MMVM_L2_PROTECTION_FAULT_MM_CNTL3__VML1_READ_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT__SHIFT                0x0
#define MMVM_L2_PROTECTION_FAULT_MM_CNTL3__VML1_READ_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT_MASK                  0xFFFFFFFFL
//MMVM_L2_PROTECTION_FAULT_MM_CNTL4
#define MMVM_L2_PROTECTION_FAULT_MM_CNTL4__VML1_WRITE_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT__SHIFT               0x0
#define MMVM_L2_PROTECTION_FAULT_MM_CNTL4__VML1_WRITE_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT_MASK                 0xFFFFFFFFL
//MMVM_L2_PROTECTION_FAULT_STATUS_LO32
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__MORE_FAULTS__SHIFT                                              0x0
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__WALKER_ERROR__SHIFT                                             0x1
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__PERMISSION_FAULTS__SHIFT                                        0x5
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__MAPPING_ERROR__SHIFT                                            0xa
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__CID__SHIFT                                                      0xb
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__RW__SHIFT                                                       0x14
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__ATOMIC__SHIFT                                                   0x15
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__VMID__SHIFT                                                     0x16
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__VF__SHIFT                                                       0x1a
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__VFID__SHIFT                                                     0x1b
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__MORE_FAULTS_MASK                                                0x00000001L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__WALKER_ERROR_MASK                                               0x0000001EL
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__PERMISSION_FAULTS_MASK                                          0x000003E0L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__MAPPING_ERROR_MASK                                              0x00000400L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__CID_MASK                                                        0x000FF800L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__RW_MASK                                                         0x00100000L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__ATOMIC_MASK                                                     0x00200000L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__VMID_MASK                                                       0x03C00000L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__VF_MASK                                                         0x04000000L
#define MMVM_L2_PROTECTION_FAULT_STATUS_LO32__VFID_MASK                                                       0xF8000000L
//MMVM_L2_PROTECTION_FAULT_STATUS_HI32
#define MMVM_L2_PROTECTION_FAULT_STATUS_HI32__PRT__SHIFT                                                      0x0
#define MMVM_L2_PROTECTION_FAULT_STATUS_HI32__UCE__SHIFT                                                      0x1
#define MMVM_L2_PROTECTION_FAULT_STATUS_HI32__FED__SHIFT                                                      0x2
#define MMVM_L2_PROTECTION_FAULT_STATUS_HI32__PRT_MASK                                                        0x00000001L
#define MMVM_L2_PROTECTION_FAULT_STATUS_HI32__UCE_MASK                                                        0x00000002L
#define MMVM_L2_PROTECTION_FAULT_STATUS_HI32__FED_MASK                                                        0x00000004L
//MMVM_L2_PROTECTION_FAULT_ADDR_LO32
#define MMVM_L2_PROTECTION_FAULT_ADDR_LO32__LOGICAL_PAGE_ADDR_LO32__SHIFT                                     0x0
#define MMVM_L2_PROTECTION_FAULT_ADDR_LO32__LOGICAL_PAGE_ADDR_LO32_MASK                                       0xFFFFFFFFL
//MMVM_L2_PROTECTION_FAULT_ADDR_HI32
#define MMVM_L2_PROTECTION_FAULT_ADDR_HI32__LOGICAL_PAGE_ADDR_HI13__SHIFT                                     0x0
#define MMVM_L2_PROTECTION_FAULT_ADDR_HI32__LOGICAL_PAGE_ADDR_HI13_MASK                                       0x00001FFFL
//MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32
#define MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32__PHYSICAL_PAGE_ADDR_LO32__SHIFT                            0x0
#define MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32__PHYSICAL_PAGE_ADDR_LO32_MASK                              0xFFFFFFFFL
//MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32
#define MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32__PHYSICAL_PAGE_ADDR_HI8__SHIFT                             0x0
#define MMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32__PHYSICAL_PAGE_ADDR_HI8_MASK                               0x000000FFL
//MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                     0x0
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                       0xFFFFFFFFL
//MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                     0x0
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                       0x00001FFFL
//MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                    0x0
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                      0xFFFFFFFFL
//MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                    0x0
#define MMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                      0x00001FFFL
//MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32
#define MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32__PHYSICAL_PAGE_OFFSET_LO32__SHIFT                       0x0
#define MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32__PHYSICAL_PAGE_OFFSET_LO32_MASK                         0xFFFFFFFFL
//MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32
#define MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32__PHYSICAL_PAGE_OFFSET_HI8__SHIFT                        0x0
#define MMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32__PHYSICAL_PAGE_OFFSET_HI8_MASK                          0x000000FFL
//MMVM_L2_CNTL4
#define MMVM_L2_CNTL4__L2_CACHE_4K_PARTITION_COUNT__SHIFT                                                     0x0
#define MMVM_L2_CNTL4__VMC_TAP_PDE_REQUEST_PHYSICAL__SHIFT                                                    0x6
#define MMVM_L2_CNTL4__VMC_TAP_PTE_REQUEST_PHYSICAL__SHIFT                                                    0x7
#define MMVM_L2_CNTL4__MM_NONRT_IFIFO_ACTIVE_TRANSACTION_LIMIT__SHIFT                                         0x8
#define MMVM_L2_CNTL4__MM_SOFTRT_IFIFO_ACTIVE_TRANSACTION_LIMIT__SHIFT                                        0x12
#define MMVM_L2_CNTL4__BPM_CGCGLS_OVERRIDE__SHIFT                                                             0x1c
#define MMVM_L2_CNTL4__GC_CH_FGCG_OFF__SHIFT                                                                  0x1d
#define MMVM_L2_CNTL4__VFIFO_HEAD_OF_QUEUE__SHIFT                                                             0x1e
#define MMVM_L2_CNTL4__VFIFO_VISIBLE_BANK_SILOS__SHIFT                                                        0x1f
#define MMVM_L2_CNTL4__L2_CACHE_4K_PARTITION_COUNT_MASK                                                       0x0000003FL
#define MMVM_L2_CNTL4__VMC_TAP_PDE_REQUEST_PHYSICAL_MASK                                                      0x00000040L
#define MMVM_L2_CNTL4__VMC_TAP_PTE_REQUEST_PHYSICAL_MASK                                                      0x00000080L
#define MMVM_L2_CNTL4__MM_NONRT_IFIFO_ACTIVE_TRANSACTION_LIMIT_MASK                                           0x0003FF00L
#define MMVM_L2_CNTL4__MM_SOFTRT_IFIFO_ACTIVE_TRANSACTION_LIMIT_MASK                                          0x0FFC0000L
#define MMVM_L2_CNTL4__BPM_CGCGLS_OVERRIDE_MASK                                                               0x10000000L
#define MMVM_L2_CNTL4__GC_CH_FGCG_OFF_MASK                                                                    0x20000000L
#define MMVM_L2_CNTL4__VFIFO_HEAD_OF_QUEUE_MASK                                                               0x40000000L
#define MMVM_L2_CNTL4__VFIFO_VISIBLE_BANK_SILOS_MASK                                                          0x80000000L
//MMVM_L2_MM_GROUP_RT_CLASSES
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_0_RT_CLASS__SHIFT                                                  0x0
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_1_RT_CLASS__SHIFT                                                  0x1
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_2_RT_CLASS__SHIFT                                                  0x2
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_3_RT_CLASS__SHIFT                                                  0x3
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_4_RT_CLASS__SHIFT                                                  0x4
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_5_RT_CLASS__SHIFT                                                  0x5
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_6_RT_CLASS__SHIFT                                                  0x6
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_7_RT_CLASS__SHIFT                                                  0x7
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_8_RT_CLASS__SHIFT                                                  0x8
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_9_RT_CLASS__SHIFT                                                  0x9
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_10_RT_CLASS__SHIFT                                                 0xa
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_11_RT_CLASS__SHIFT                                                 0xb
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_12_RT_CLASS__SHIFT                                                 0xc
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_13_RT_CLASS__SHIFT                                                 0xd
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_14_RT_CLASS__SHIFT                                                 0xe
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_15_RT_CLASS__SHIFT                                                 0xf
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_16_RT_CLASS__SHIFT                                                 0x10
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_17_RT_CLASS__SHIFT                                                 0x11
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_18_RT_CLASS__SHIFT                                                 0x12
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_19_RT_CLASS__SHIFT                                                 0x13
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_20_RT_CLASS__SHIFT                                                 0x14
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_21_RT_CLASS__SHIFT                                                 0x15
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_22_RT_CLASS__SHIFT                                                 0x16
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_23_RT_CLASS__SHIFT                                                 0x17
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_24_RT_CLASS__SHIFT                                                 0x18
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_25_RT_CLASS__SHIFT                                                 0x19
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_26_RT_CLASS__SHIFT                                                 0x1a
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_27_RT_CLASS__SHIFT                                                 0x1b
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_28_RT_CLASS__SHIFT                                                 0x1c
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_29_RT_CLASS__SHIFT                                                 0x1d
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_30_RT_CLASS__SHIFT                                                 0x1e
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_31_RT_CLASS__SHIFT                                                 0x1f
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_0_RT_CLASS_MASK                                                    0x00000001L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_1_RT_CLASS_MASK                                                    0x00000002L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_2_RT_CLASS_MASK                                                    0x00000004L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_3_RT_CLASS_MASK                                                    0x00000008L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_4_RT_CLASS_MASK                                                    0x00000010L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_5_RT_CLASS_MASK                                                    0x00000020L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_6_RT_CLASS_MASK                                                    0x00000040L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_7_RT_CLASS_MASK                                                    0x00000080L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_8_RT_CLASS_MASK                                                    0x00000100L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_9_RT_CLASS_MASK                                                    0x00000200L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_10_RT_CLASS_MASK                                                   0x00000400L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_11_RT_CLASS_MASK                                                   0x00000800L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_12_RT_CLASS_MASK                                                   0x00001000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_13_RT_CLASS_MASK                                                   0x00002000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_14_RT_CLASS_MASK                                                   0x00004000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_15_RT_CLASS_MASK                                                   0x00008000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_16_RT_CLASS_MASK                                                   0x00010000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_17_RT_CLASS_MASK                                                   0x00020000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_18_RT_CLASS_MASK                                                   0x00040000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_19_RT_CLASS_MASK                                                   0x00080000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_20_RT_CLASS_MASK                                                   0x00100000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_21_RT_CLASS_MASK                                                   0x00200000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_22_RT_CLASS_MASK                                                   0x00400000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_23_RT_CLASS_MASK                                                   0x00800000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_24_RT_CLASS_MASK                                                   0x01000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_25_RT_CLASS_MASK                                                   0x02000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_26_RT_CLASS_MASK                                                   0x04000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_27_RT_CLASS_MASK                                                   0x08000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_28_RT_CLASS_MASK                                                   0x10000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_29_RT_CLASS_MASK                                                   0x20000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_30_RT_CLASS_MASK                                                   0x40000000L
#define MMVM_L2_MM_GROUP_RT_CLASSES__GROUP_31_RT_CLASS_MASK                                                   0x80000000L
//MMVM_L2_BANK_SELECT_RESERVED_CID
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_READ_CLIENT_ID__SHIFT                                      0x0
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_WRITE_CLIENT_ID__SHIFT                                     0xa
#define MMVM_L2_BANK_SELECT_RESERVED_CID__ENABLE__SHIFT                                                       0x14
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_CACHE_INVALIDATION_MODE__SHIFT                             0x18
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_CACHE_PRIVATE_INVALIDATION__SHIFT                          0x19
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_CACHE_FRAGMENT_SIZE__SHIFT                                 0x1a
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_READ_CLIENT_ID_MASK                                        0x000001FFL
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_WRITE_CLIENT_ID_MASK                                       0x0007FC00L
#define MMVM_L2_BANK_SELECT_RESERVED_CID__ENABLE_MASK                                                         0x00100000L
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_CACHE_INVALIDATION_MODE_MASK                               0x01000000L
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_CACHE_PRIVATE_INVALIDATION_MASK                            0x02000000L
#define MMVM_L2_BANK_SELECT_RESERVED_CID__RESERVED_CACHE_FRAGMENT_SIZE_MASK                                   0x7C000000L
//MMVM_L2_BANK_SELECT_RESERVED_CID2
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_READ_CLIENT_ID__SHIFT                                     0x0
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_WRITE_CLIENT_ID__SHIFT                                    0xa
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__ENABLE__SHIFT                                                      0x14
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_CACHE_INVALIDATION_MODE__SHIFT                            0x18
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_CACHE_PRIVATE_INVALIDATION__SHIFT                         0x19
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_CACHE_FRAGMENT_SIZE__SHIFT                                0x1a
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_READ_CLIENT_ID_MASK                                       0x000001FFL
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_WRITE_CLIENT_ID_MASK                                      0x0007FC00L
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__ENABLE_MASK                                                        0x00100000L
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_CACHE_INVALIDATION_MODE_MASK                              0x01000000L
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_CACHE_PRIVATE_INVALIDATION_MASK                           0x02000000L
#define MMVM_L2_BANK_SELECT_RESERVED_CID2__RESERVED_CACHE_FRAGMENT_SIZE_MASK                                  0x7C000000L
//MMVM_L2_CACHE_PARITY_CNTL
#define MMVM_L2_CACHE_PARITY_CNTL__ENABLE_PARITY_CHECKS_IN_4K_PTE_CACHES__SHIFT                               0x0
#define MMVM_L2_CACHE_PARITY_CNTL__ENABLE_PARITY_CHECKS_IN_BIGK_PTE_CACHES__SHIFT                             0x1
#define MMVM_L2_CACHE_PARITY_CNTL__ENABLE_PARITY_CHECKS_IN_PDE_CACHES__SHIFT                                  0x2
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_PARITY_MISMATCH_IN_4K_PTE_CACHE__SHIFT                               0x3
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_PARITY_MISMATCH_IN_BIGK_PTE_CACHE__SHIFT                             0x4
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_PARITY_MISMATCH_IN_PDE_CACHE__SHIFT                                  0x5
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_CACHE_BANK__SHIFT                                                    0x6
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_CACHE_NUMBER__SHIFT                                                  0x9
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_CACHE_ASSOC__SHIFT                                                   0xc
#define MMVM_L2_CACHE_PARITY_CNTL__ENABLE_PARITY_CHECKS_IN_4K_PTE_CACHES_MASK                                 0x00000001L
#define MMVM_L2_CACHE_PARITY_CNTL__ENABLE_PARITY_CHECKS_IN_BIGK_PTE_CACHES_MASK                               0x00000002L
#define MMVM_L2_CACHE_PARITY_CNTL__ENABLE_PARITY_CHECKS_IN_PDE_CACHES_MASK                                    0x00000004L
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_PARITY_MISMATCH_IN_4K_PTE_CACHE_MASK                                 0x00000008L
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_PARITY_MISMATCH_IN_BIGK_PTE_CACHE_MASK                               0x00000010L
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_PARITY_MISMATCH_IN_PDE_CACHE_MASK                                    0x00000020L
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_CACHE_BANK_MASK                                                      0x000001C0L
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_CACHE_NUMBER_MASK                                                    0x00000E00L
#define MMVM_L2_CACHE_PARITY_CNTL__FORCE_CACHE_ASSOC_MASK                                                     0x0000F000L
//MMVM_L2_CGTT_CLK_CTRL
#define MMVM_L2_CGTT_CLK_CTRL__ON_DELAY__SHIFT                                                                0x0
#define MMVM_L2_CGTT_CLK_CTRL__OFF_HYSTERESIS__SHIFT                                                          0x5
#define MMVM_L2_CGTT_CLK_CTRL__LS_ASSERT_HYSTERESIS__SHIFT                                                    0xd
#define MMVM_L2_CGTT_CLK_CTRL__MIN_MGLS__SHIFT                                                                0x1a
#define MMVM_L2_CGTT_CLK_CTRL__CGLS_DISABLE__SHIFT                                                            0x1d
#define MMVM_L2_CGTT_CLK_CTRL__LS_DISABLE__SHIFT                                                              0x1e
#define MMVM_L2_CGTT_CLK_CTRL__BUSY_OVERRIDE__SHIFT                                                           0x1f
#define MMVM_L2_CGTT_CLK_CTRL__ON_DELAY_MASK                                                                  0x0000001FL
#define MMVM_L2_CGTT_CLK_CTRL__OFF_HYSTERESIS_MASK                                                            0x00001FE0L
#define MMVM_L2_CGTT_CLK_CTRL__LS_ASSERT_HYSTERESIS_MASK                                                      0x03FFE000L
#define MMVM_L2_CGTT_CLK_CTRL__MIN_MGLS_MASK                                                                  0x1C000000L
#define MMVM_L2_CGTT_CLK_CTRL__CGLS_DISABLE_MASK                                                              0x20000000L
#define MMVM_L2_CGTT_CLK_CTRL__LS_DISABLE_MASK                                                                0x40000000L
#define MMVM_L2_CGTT_CLK_CTRL__BUSY_OVERRIDE_MASK                                                             0x80000000L
//MMVM_L2_CNTL5
#define MMVM_L2_CNTL5__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT                                                   0x0
#define MMVM_L2_CNTL5__WALKER_PRIORITY_CLIENT_ID__SHIFT                                                       0x5
#define MMVM_L2_CNTL5__WALKER_FETCH_PDE_NOALLOC_ENABLE__SHIFT                                                 0xe
#define MMVM_L2_CNTL5__WALKER_FETCH_PDE_MTYPE_ENABLE__SHIFT                                                   0xf
#define MMVM_L2_CNTL5__MM_CLIENT_RET_FGCG_OFF__SHIFT                                                          0x10
#define MMVM_L2_CNTL5__UTCL2_ATC_REQ_FGCG_OFF__SHIFT                                                          0x11
#define MMVM_L2_CNTL5__UTCL2_ATC_INVREQ_REPEATER_FGCG_OFF__SHIFT                                              0x12
#define MMVM_L2_CNTL5__UTCL2_ONE_OUTSTANDING_ATC_INVREQ__SHIFT                                                0x13
#define MMVM_L2_CNTL5__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                                                     0x0000001FL
#define MMVM_L2_CNTL5__WALKER_PRIORITY_CLIENT_ID_MASK                                                         0x00003FE0L
#define MMVM_L2_CNTL5__WALKER_FETCH_PDE_NOALLOC_ENABLE_MASK                                                   0x00004000L
#define MMVM_L2_CNTL5__WALKER_FETCH_PDE_MTYPE_ENABLE_MASK                                                     0x00008000L
#define MMVM_L2_CNTL5__MM_CLIENT_RET_FGCG_OFF_MASK                                                            0x00010000L
#define MMVM_L2_CNTL5__UTCL2_ATC_REQ_FGCG_OFF_MASK                                                            0x00020000L
#define MMVM_L2_CNTL5__UTCL2_ATC_INVREQ_REPEATER_FGCG_OFF_MASK                                                0x00040000L
#define MMVM_L2_CNTL5__UTCL2_ONE_OUTSTANDING_ATC_INVREQ_MASK                                                  0x00080000L
//MMVM_L2_GCR_CNTL
#define MMVM_L2_GCR_CNTL__GCR_ENABLE__SHIFT                                                                   0x0
#define MMVM_L2_GCR_CNTL__GCR_CLIENT_ID__SHIFT                                                                0x1
#define MMVM_L2_GCR_CNTL__GCR_ENABLE_MASK                                                                     0x00000001L
#define MMVM_L2_GCR_CNTL__GCR_CLIENT_ID_MASK                                                                  0x000003FEL
//MMVM_L2_CGTT_BUSY_CTRL
#define MMVM_L2_CGTT_BUSY_CTRL__READ_DELAY__SHIFT                                                             0x0
#define MMVM_L2_CGTT_BUSY_CTRL__ALWAYS_BUSY__SHIFT                                                            0x5
#define MMVM_L2_CGTT_BUSY_CTRL__READ_DELAY_MASK                                                               0x0000001FL
#define MMVM_L2_CGTT_BUSY_CTRL__ALWAYS_BUSY_MASK                                                              0x00000020L
//MMVM_L2_PTE_CACHE_DUMP_CNTL
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__ENABLE__SHIFT                                                            0x0
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__READY__SHIFT                                                             0x1
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__BANK__SHIFT                                                              0x4
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__CACHE__SHIFT                                                             0x8
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__ASSOC__SHIFT                                                             0xc
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__INDEX__SHIFT                                                             0x10
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__ENABLE_MASK                                                              0x00000001L
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__READY_MASK                                                               0x00000002L
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__BANK_MASK                                                                0x000000F0L
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__CACHE_MASK                                                               0x00000F00L
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__ASSOC_MASK                                                               0x0000F000L
#define MMVM_L2_PTE_CACHE_DUMP_CNTL__INDEX_MASK                                                               0xFFFF0000L
//MMVM_L2_PTE_CACHE_DUMP_READ
#define MMVM_L2_PTE_CACHE_DUMP_READ__DATA__SHIFT                                                              0x0
#define MMVM_L2_PTE_CACHE_DUMP_READ__DATA_MASK                                                                0xFFFFFFFFL
//MMVM_L2_BANK_SELECT_MASKS
#define MMVM_L2_BANK_SELECT_MASKS__MASK0__SHIFT                                                               0x0
#define MMVM_L2_BANK_SELECT_MASKS__MASK1__SHIFT                                                               0x4
#define MMVM_L2_BANK_SELECT_MASKS__MASK2__SHIFT                                                               0x8
#define MMVM_L2_BANK_SELECT_MASKS__MASK3__SHIFT                                                               0xc
#define MMVM_L2_BANK_SELECT_MASKS__MASK0_MASK                                                                 0x0000000FL
#define MMVM_L2_BANK_SELECT_MASKS__MASK1_MASK                                                                 0x000000F0L
#define MMVM_L2_BANK_SELECT_MASKS__MASK2_MASK                                                                 0x00000F00L
#define MMVM_L2_BANK_SELECT_MASKS__MASK3_MASK                                                                 0x0000F000L
//MMUTCL2_CREDIT_SAFETY_GROUP_RET_CDC
#define MMUTCL2_CREDIT_SAFETY_GROUP_RET_CDC__CREDITS__SHIFT                                                   0x0
#define MMUTCL2_CREDIT_SAFETY_GROUP_RET_CDC__UPDATE__SHIFT                                                    0xa
#define MMUTCL2_CREDIT_SAFETY_GROUP_RET_CDC__CREDITS_MASK                                                     0x000003FFL
#define MMUTCL2_CREDIT_SAFETY_GROUP_RET_CDC__UPDATE_MASK                                                      0x00000400L
//MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_CDC
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_CDC__CREDITS__SHIFT                                        0x0
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_CDC__UPDATE__SHIFT                                         0xa
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_CDC__CREDITS_MASK                                          0x000003FFL
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_CDC__UPDATE_MASK                                           0x00000400L
//MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_NOCDC
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_NOCDC__CREDITS__SHIFT                                      0x0
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_NOCDC__UPDATE__SHIFT                                       0xa
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_NOCDC__CREDITS_MASK                                        0x000003FFL
#define MMUTCL2_CREDIT_SAFETY_GROUP_CLIENTS_INVREQ_NOCDC__UPDATE_MASK                                         0x00000400L
//MMVML2_CREDIT_SAFETY_IH_FAULT_INTERRUPT
#define MMVML2_CREDIT_SAFETY_IH_FAULT_INTERRUPT__CREDITS__SHIFT                                               0x0
#define MMVML2_CREDIT_SAFETY_IH_FAULT_INTERRUPT__UPDATE__SHIFT                                                0xa
#define MMVML2_CREDIT_SAFETY_IH_FAULT_INTERRUPT__CREDITS_MASK                                                 0x000003FFL
#define MMVML2_CREDIT_SAFETY_IH_FAULT_INTERRUPT__UPDATE_MASK                                                  0x00000400L
//MMVML2_WALKER_CREDIT_SAFETY_FETCH_RDREQ
#define MMVML2_WALKER_CREDIT_SAFETY_FETCH_RDREQ__CREDITS__SHIFT                                               0x0
#define MMVML2_WALKER_CREDIT_SAFETY_FETCH_RDREQ__UPDATE__SHIFT                                                0xa
#define MMVML2_WALKER_CREDIT_SAFETY_FETCH_RDREQ__CREDITS_MASK                                                 0x000003FFL
#define MMVML2_WALKER_CREDIT_SAFETY_FETCH_RDREQ__UPDATE_MASK                                                  0x00000400L
//MMVML2_IH_FAULT_INTERRUPT_CNTL
#define MMVML2_IH_FAULT_INTERRUPT_CNTL__ENABLE_PF_POISON_INTERRUPT__SHIFT                                     0x0
#define MMVML2_IH_FAULT_INTERRUPT_CNTL__ENABLE_VF_POISON_INTERRUPT__SHIFT                                     0x1
#define MMVML2_IH_FAULT_INTERRUPT_CNTL__POISON_INTERRUPT_TO_PF__SHIFT                                         0x2
#define MMVML2_IH_FAULT_INTERRUPT_CNTL__ENABLE_PF_POISON_INTERRUPT_MASK                                       0x00000001L
#define MMVML2_IH_FAULT_INTERRUPT_CNTL__ENABLE_VF_POISON_INTERRUPT_MASK                                       0x00000002L
#define MMVML2_IH_FAULT_INTERRUPT_CNTL__POISON_INTERRUPT_TO_PF_MASK                                           0x00000004L


// addressBlock: mmhub_mmutcl2_mmvml2prdec
//MMMC_VM_L2_PERFCOUNTER_LO
#define MMMC_VM_L2_PERFCOUNTER_LO__COUNTER_LO__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER_LO__COUNTER_LO_MASK                                                            0xFFFFFFFFL
//MMMC_VM_L2_PERFCOUNTER_HI
#define MMMC_VM_L2_PERFCOUNTER_HI__COUNTER_HI__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER_HI__COMPARE_VALUE__SHIFT                                                       0x10
#define MMMC_VM_L2_PERFCOUNTER_HI__COUNTER_HI_MASK                                                            0x0000FFFFL
#define MMMC_VM_L2_PERFCOUNTER_HI__COMPARE_VALUE_MASK                                                         0xFFFF0000L
//MMUTCL2_PERFCOUNTER_LO
#define MMUTCL2_PERFCOUNTER_LO__COUNTER_LO__SHIFT                                                             0x0
#define MMUTCL2_PERFCOUNTER_LO__COUNTER_LO_MASK                                                               0xFFFFFFFFL
//MMUTCL2_PERFCOUNTER_HI
#define MMUTCL2_PERFCOUNTER_HI__COUNTER_HI__SHIFT                                                             0x0
#define MMUTCL2_PERFCOUNTER_HI__COMPARE_VALUE__SHIFT                                                          0x10
#define MMUTCL2_PERFCOUNTER_HI__COUNTER_HI_MASK                                                               0x0000FFFFL
#define MMUTCL2_PERFCOUNTER_HI__COMPARE_VALUE_MASK                                                            0xFFFF0000L


// addressBlock: mmhub_mmutcl2_mmatcl2prdec
//MM_ATC_L2_PERFCOUNTER_LO
#define MM_ATC_L2_PERFCOUNTER_LO__COUNTER_LO__SHIFT                                                           0x0
#define MM_ATC_L2_PERFCOUNTER_LO__COUNTER_LO_MASK                                                             0xFFFFFFFFL
//MM_ATC_L2_PERFCOUNTER_HI
#define MM_ATC_L2_PERFCOUNTER_HI__COUNTER_HI__SHIFT                                                           0x0
#define MM_ATC_L2_PERFCOUNTER_HI__COMPARE_VALUE__SHIFT                                                        0x10
#define MM_ATC_L2_PERFCOUNTER_HI__COUNTER_HI_MASK                                                             0x0000FFFFL
#define MM_ATC_L2_PERFCOUNTER_HI__COMPARE_VALUE_MASK                                                          0xFFFF0000L


// addressBlock: mmhub_mmutcl2_mmvml2pldec
//MMMC_VM_L2_PERFCOUNTER0_CFG
#define MMMC_VM_L2_PERFCOUNTER0_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER0_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER0_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER0_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER0_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER0_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER0_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER0_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER0_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER0_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER1_CFG
#define MMMC_VM_L2_PERFCOUNTER1_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER1_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER1_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER1_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER1_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER1_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER1_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER1_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER1_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER1_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER2_CFG
#define MMMC_VM_L2_PERFCOUNTER2_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER2_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER2_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER2_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER2_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER2_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER2_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER2_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER2_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER2_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER3_CFG
#define MMMC_VM_L2_PERFCOUNTER3_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER3_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER3_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER3_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER3_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER3_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER3_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER3_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER3_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER3_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER4_CFG
#define MMMC_VM_L2_PERFCOUNTER4_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER4_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER4_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER4_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER4_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER4_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER4_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER4_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER4_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER4_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER5_CFG
#define MMMC_VM_L2_PERFCOUNTER5_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER5_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER5_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER5_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER5_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER5_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER5_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER5_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER5_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER5_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER6_CFG
#define MMMC_VM_L2_PERFCOUNTER6_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER6_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER6_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER6_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER6_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER6_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER6_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER6_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER6_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER6_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER7_CFG
#define MMMC_VM_L2_PERFCOUNTER7_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER7_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER7_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER7_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER7_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER7_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER7_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER7_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER7_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER7_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER8_CFG
#define MMMC_VM_L2_PERFCOUNTER8_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER8_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER8_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER8_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER8_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER8_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER8_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER8_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER8_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER8_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER9_CFG
#define MMMC_VM_L2_PERFCOUNTER9_CFG__PERF_SEL__SHIFT                                                          0x0
#define MMMC_VM_L2_PERFCOUNTER9_CFG__PERF_SEL_END__SHIFT                                                      0x8
#define MMMC_VM_L2_PERFCOUNTER9_CFG__PERF_MODE__SHIFT                                                         0x18
#define MMMC_VM_L2_PERFCOUNTER9_CFG__ENABLE__SHIFT                                                            0x1c
#define MMMC_VM_L2_PERFCOUNTER9_CFG__CLEAR__SHIFT                                                             0x1d
#define MMMC_VM_L2_PERFCOUNTER9_CFG__PERF_SEL_MASK                                                            0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER9_CFG__PERF_SEL_END_MASK                                                        0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER9_CFG__PERF_MODE_MASK                                                           0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER9_CFG__ENABLE_MASK                                                              0x10000000L
#define MMMC_VM_L2_PERFCOUNTER9_CFG__CLEAR_MASK                                                               0x20000000L
//MMMC_VM_L2_PERFCOUNTER10_CFG
#define MMMC_VM_L2_PERFCOUNTER10_CFG__PERF_SEL__SHIFT                                                         0x0
#define MMMC_VM_L2_PERFCOUNTER10_CFG__PERF_SEL_END__SHIFT                                                     0x8
#define MMMC_VM_L2_PERFCOUNTER10_CFG__PERF_MODE__SHIFT                                                        0x18
#define MMMC_VM_L2_PERFCOUNTER10_CFG__ENABLE__SHIFT                                                           0x1c
#define MMMC_VM_L2_PERFCOUNTER10_CFG__CLEAR__SHIFT                                                            0x1d
#define MMMC_VM_L2_PERFCOUNTER10_CFG__PERF_SEL_MASK                                                           0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER10_CFG__PERF_SEL_END_MASK                                                       0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER10_CFG__PERF_MODE_MASK                                                          0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER10_CFG__ENABLE_MASK                                                             0x10000000L
#define MMMC_VM_L2_PERFCOUNTER10_CFG__CLEAR_MASK                                                              0x20000000L
//MMMC_VM_L2_PERFCOUNTER11_CFG
#define MMMC_VM_L2_PERFCOUNTER11_CFG__PERF_SEL__SHIFT                                                         0x0
#define MMMC_VM_L2_PERFCOUNTER11_CFG__PERF_SEL_END__SHIFT                                                     0x8
#define MMMC_VM_L2_PERFCOUNTER11_CFG__PERF_MODE__SHIFT                                                        0x18
#define MMMC_VM_L2_PERFCOUNTER11_CFG__ENABLE__SHIFT                                                           0x1c
#define MMMC_VM_L2_PERFCOUNTER11_CFG__CLEAR__SHIFT                                                            0x1d
#define MMMC_VM_L2_PERFCOUNTER11_CFG__PERF_SEL_MASK                                                           0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER11_CFG__PERF_SEL_END_MASK                                                       0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER11_CFG__PERF_MODE_MASK                                                          0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER11_CFG__ENABLE_MASK                                                             0x10000000L
#define MMMC_VM_L2_PERFCOUNTER11_CFG__CLEAR_MASK                                                              0x20000000L
//MMMC_VM_L2_PERFCOUNTER12_CFG
#define MMMC_VM_L2_PERFCOUNTER12_CFG__PERF_SEL__SHIFT                                                         0x0
#define MMMC_VM_L2_PERFCOUNTER12_CFG__PERF_SEL_END__SHIFT                                                     0x8
#define MMMC_VM_L2_PERFCOUNTER12_CFG__PERF_MODE__SHIFT                                                        0x18
#define MMMC_VM_L2_PERFCOUNTER12_CFG__ENABLE__SHIFT                                                           0x1c
#define MMMC_VM_L2_PERFCOUNTER12_CFG__CLEAR__SHIFT                                                            0x1d
#define MMMC_VM_L2_PERFCOUNTER12_CFG__PERF_SEL_MASK                                                           0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER12_CFG__PERF_SEL_END_MASK                                                       0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER12_CFG__PERF_MODE_MASK                                                          0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER12_CFG__ENABLE_MASK                                                             0x10000000L
#define MMMC_VM_L2_PERFCOUNTER12_CFG__CLEAR_MASK                                                              0x20000000L
//MMMC_VM_L2_PERFCOUNTER13_CFG
#define MMMC_VM_L2_PERFCOUNTER13_CFG__PERF_SEL__SHIFT                                                         0x0
#define MMMC_VM_L2_PERFCOUNTER13_CFG__PERF_SEL_END__SHIFT                                                     0x8
#define MMMC_VM_L2_PERFCOUNTER13_CFG__PERF_MODE__SHIFT                                                        0x18
#define MMMC_VM_L2_PERFCOUNTER13_CFG__ENABLE__SHIFT                                                           0x1c
#define MMMC_VM_L2_PERFCOUNTER13_CFG__CLEAR__SHIFT                                                            0x1d
#define MMMC_VM_L2_PERFCOUNTER13_CFG__PERF_SEL_MASK                                                           0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER13_CFG__PERF_SEL_END_MASK                                                       0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER13_CFG__PERF_MODE_MASK                                                          0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER13_CFG__ENABLE_MASK                                                             0x10000000L
#define MMMC_VM_L2_PERFCOUNTER13_CFG__CLEAR_MASK                                                              0x20000000L
//MMMC_VM_L2_PERFCOUNTER14_CFG
#define MMMC_VM_L2_PERFCOUNTER14_CFG__PERF_SEL__SHIFT                                                         0x0
#define MMMC_VM_L2_PERFCOUNTER14_CFG__PERF_SEL_END__SHIFT                                                     0x8
#define MMMC_VM_L2_PERFCOUNTER14_CFG__PERF_MODE__SHIFT                                                        0x18
#define MMMC_VM_L2_PERFCOUNTER14_CFG__ENABLE__SHIFT                                                           0x1c
#define MMMC_VM_L2_PERFCOUNTER14_CFG__CLEAR__SHIFT                                                            0x1d
#define MMMC_VM_L2_PERFCOUNTER14_CFG__PERF_SEL_MASK                                                           0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER14_CFG__PERF_SEL_END_MASK                                                       0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER14_CFG__PERF_MODE_MASK                                                          0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER14_CFG__ENABLE_MASK                                                             0x10000000L
#define MMMC_VM_L2_PERFCOUNTER14_CFG__CLEAR_MASK                                                              0x20000000L
//MMMC_VM_L2_PERFCOUNTER15_CFG
#define MMMC_VM_L2_PERFCOUNTER15_CFG__PERF_SEL__SHIFT                                                         0x0
#define MMMC_VM_L2_PERFCOUNTER15_CFG__PERF_SEL_END__SHIFT                                                     0x8
#define MMMC_VM_L2_PERFCOUNTER15_CFG__PERF_MODE__SHIFT                                                        0x18
#define MMMC_VM_L2_PERFCOUNTER15_CFG__ENABLE__SHIFT                                                           0x1c
#define MMMC_VM_L2_PERFCOUNTER15_CFG__CLEAR__SHIFT                                                            0x1d
#define MMMC_VM_L2_PERFCOUNTER15_CFG__PERF_SEL_MASK                                                           0x000000FFL
#define MMMC_VM_L2_PERFCOUNTER15_CFG__PERF_SEL_END_MASK                                                       0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER15_CFG__PERF_MODE_MASK                                                          0x0F000000L
#define MMMC_VM_L2_PERFCOUNTER15_CFG__ENABLE_MASK                                                             0x10000000L
#define MMMC_VM_L2_PERFCOUNTER15_CFG__CLEAR_MASK                                                              0x20000000L
//MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT__SHIFT                                          0x0
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__START_TRIGGER__SHIFT                                                0x8
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER__SHIFT                                                 0x10
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY__SHIFT                                                   0x18
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL__SHIFT                                                    0x19
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE__SHIFT                                         0x1a
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT_MASK                                            0x0000000FL
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__START_TRIGGER_MASK                                                  0x0000FF00L
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER_MASK                                                   0x00FF0000L
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY_MASK                                                     0x01000000L
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL_MASK                                                      0x02000000L
#define MMMC_VM_L2_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE_MASK                                           0x04000000L
//MMUTCL2_PERFCOUNTER0_CFG
#define MMUTCL2_PERFCOUNTER0_CFG__PERF_SEL__SHIFT                                                             0x0
#define MMUTCL2_PERFCOUNTER0_CFG__PERF_SEL_END__SHIFT                                                         0x8
#define MMUTCL2_PERFCOUNTER0_CFG__PERF_MODE__SHIFT                                                            0x18
#define MMUTCL2_PERFCOUNTER0_CFG__ENABLE__SHIFT                                                               0x1c
#define MMUTCL2_PERFCOUNTER0_CFG__CLEAR__SHIFT                                                                0x1d
#define MMUTCL2_PERFCOUNTER0_CFG__PERF_SEL_MASK                                                               0x000000FFL
#define MMUTCL2_PERFCOUNTER0_CFG__PERF_SEL_END_MASK                                                           0x0000FF00L
#define MMUTCL2_PERFCOUNTER0_CFG__PERF_MODE_MASK                                                              0x0F000000L
#define MMUTCL2_PERFCOUNTER0_CFG__ENABLE_MASK                                                                 0x10000000L
#define MMUTCL2_PERFCOUNTER0_CFG__CLEAR_MASK                                                                  0x20000000L
//MMUTCL2_PERFCOUNTER1_CFG
#define MMUTCL2_PERFCOUNTER1_CFG__PERF_SEL__SHIFT                                                             0x0
#define MMUTCL2_PERFCOUNTER1_CFG__PERF_SEL_END__SHIFT                                                         0x8
#define MMUTCL2_PERFCOUNTER1_CFG__PERF_MODE__SHIFT                                                            0x18
#define MMUTCL2_PERFCOUNTER1_CFG__ENABLE__SHIFT                                                               0x1c
#define MMUTCL2_PERFCOUNTER1_CFG__CLEAR__SHIFT                                                                0x1d
#define MMUTCL2_PERFCOUNTER1_CFG__PERF_SEL_MASK                                                               0x000000FFL
#define MMUTCL2_PERFCOUNTER1_CFG__PERF_SEL_END_MASK                                                           0x0000FF00L
#define MMUTCL2_PERFCOUNTER1_CFG__PERF_MODE_MASK                                                              0x0F000000L
#define MMUTCL2_PERFCOUNTER1_CFG__ENABLE_MASK                                                                 0x10000000L
#define MMUTCL2_PERFCOUNTER1_CFG__CLEAR_MASK                                                                  0x20000000L
//MMUTCL2_PERFCOUNTER2_CFG
#define MMUTCL2_PERFCOUNTER2_CFG__PERF_SEL__SHIFT                                                             0x0
#define MMUTCL2_PERFCOUNTER2_CFG__PERF_SEL_END__SHIFT                                                         0x8
#define MMUTCL2_PERFCOUNTER2_CFG__PERF_MODE__SHIFT                                                            0x18
#define MMUTCL2_PERFCOUNTER2_CFG__ENABLE__SHIFT                                                               0x1c
#define MMUTCL2_PERFCOUNTER2_CFG__CLEAR__SHIFT                                                                0x1d
#define MMUTCL2_PERFCOUNTER2_CFG__PERF_SEL_MASK                                                               0x000000FFL
#define MMUTCL2_PERFCOUNTER2_CFG__PERF_SEL_END_MASK                                                           0x0000FF00L
#define MMUTCL2_PERFCOUNTER2_CFG__PERF_MODE_MASK                                                              0x0F000000L
#define MMUTCL2_PERFCOUNTER2_CFG__ENABLE_MASK                                                                 0x10000000L
#define MMUTCL2_PERFCOUNTER2_CFG__CLEAR_MASK                                                                  0x20000000L
//MMUTCL2_PERFCOUNTER3_CFG
#define MMUTCL2_PERFCOUNTER3_CFG__PERF_SEL__SHIFT                                                             0x0
#define MMUTCL2_PERFCOUNTER3_CFG__PERF_SEL_END__SHIFT                                                         0x8
#define MMUTCL2_PERFCOUNTER3_CFG__PERF_MODE__SHIFT                                                            0x18
#define MMUTCL2_PERFCOUNTER3_CFG__ENABLE__SHIFT                                                               0x1c
#define MMUTCL2_PERFCOUNTER3_CFG__CLEAR__SHIFT                                                                0x1d
#define MMUTCL2_PERFCOUNTER3_CFG__PERF_SEL_MASK                                                               0x000000FFL
#define MMUTCL2_PERFCOUNTER3_CFG__PERF_SEL_END_MASK                                                           0x0000FF00L
#define MMUTCL2_PERFCOUNTER3_CFG__PERF_MODE_MASK                                                              0x0F000000L
#define MMUTCL2_PERFCOUNTER3_CFG__ENABLE_MASK                                                                 0x10000000L
#define MMUTCL2_PERFCOUNTER3_CFG__CLEAR_MASK                                                                  0x20000000L
//MMUTCL2_PERFCOUNTER_RSLT_CNTL
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT__SHIFT                                             0x0
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__START_TRIGGER__SHIFT                                                   0x8
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER__SHIFT                                                    0x10
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY__SHIFT                                                      0x18
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL__SHIFT                                                       0x19
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE__SHIFT                                            0x1a
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT_MASK                                               0x0000000FL
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__START_TRIGGER_MASK                                                     0x0000FF00L
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER_MASK                                                      0x00FF0000L
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY_MASK                                                        0x01000000L
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL_MASK                                                         0x02000000L
#define MMUTCL2_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE_MASK                                              0x04000000L


// addressBlock: mmhub_mmutcl2_mmatcl2pldec
//MM_ATC_L2_PERFCOUNTER0_CFG
#define MM_ATC_L2_PERFCOUNTER0_CFG__PERF_SEL__SHIFT                                                           0x0
#define MM_ATC_L2_PERFCOUNTER0_CFG__PERF_SEL_END__SHIFT                                                       0x8
#define MM_ATC_L2_PERFCOUNTER0_CFG__PERF_MODE__SHIFT                                                          0x18
#define MM_ATC_L2_PERFCOUNTER0_CFG__ENABLE__SHIFT                                                             0x1c
#define MM_ATC_L2_PERFCOUNTER0_CFG__CLEAR__SHIFT                                                              0x1d
#define MM_ATC_L2_PERFCOUNTER0_CFG__PERF_SEL_MASK                                                             0x000000FFL
#define MM_ATC_L2_PERFCOUNTER0_CFG__PERF_SEL_END_MASK                                                         0x0000FF00L
#define MM_ATC_L2_PERFCOUNTER0_CFG__PERF_MODE_MASK                                                            0x0F000000L
#define MM_ATC_L2_PERFCOUNTER0_CFG__ENABLE_MASK                                                               0x10000000L
#define MM_ATC_L2_PERFCOUNTER0_CFG__CLEAR_MASK                                                                0x20000000L
//MM_ATC_L2_PERFCOUNTER1_CFG
#define MM_ATC_L2_PERFCOUNTER1_CFG__PERF_SEL__SHIFT                                                           0x0
#define MM_ATC_L2_PERFCOUNTER1_CFG__PERF_SEL_END__SHIFT                                                       0x8
#define MM_ATC_L2_PERFCOUNTER1_CFG__PERF_MODE__SHIFT                                                          0x18
#define MM_ATC_L2_PERFCOUNTER1_CFG__ENABLE__SHIFT                                                             0x1c
#define MM_ATC_L2_PERFCOUNTER1_CFG__CLEAR__SHIFT                                                              0x1d
#define MM_ATC_L2_PERFCOUNTER1_CFG__PERF_SEL_MASK                                                             0x000000FFL
#define MM_ATC_L2_PERFCOUNTER1_CFG__PERF_SEL_END_MASK                                                         0x0000FF00L
#define MM_ATC_L2_PERFCOUNTER1_CFG__PERF_MODE_MASK                                                            0x0F000000L
#define MM_ATC_L2_PERFCOUNTER1_CFG__ENABLE_MASK                                                               0x10000000L
#define MM_ATC_L2_PERFCOUNTER1_CFG__CLEAR_MASK                                                                0x20000000L
//MM_ATC_L2_PERFCOUNTER_RSLT_CNTL
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT__SHIFT                                           0x0
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__START_TRIGGER__SHIFT                                                 0x8
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER__SHIFT                                                  0x10
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY__SHIFT                                                    0x18
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL__SHIFT                                                     0x19
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE__SHIFT                                          0x1a
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT_MASK                                             0x0000000FL
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__START_TRIGGER_MASK                                                   0x0000FF00L
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER_MASK                                                    0x00FF0000L
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY_MASK                                                      0x01000000L
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL_MASK                                                       0x02000000L
#define MM_ATC_L2_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE_MASK                                            0x04000000L


// addressBlock: mmhub_mmutcl2_mmvmsharedvcdec
//MMMC_VM_FB_LOCATION_BASE_LO32
#define MMMC_VM_FB_LOCATION_BASE_LO32__FB_BASE_LO32__SHIFT                                                    0x0
#define MMMC_VM_FB_LOCATION_BASE_LO32__FB_BASE_LO32_MASK                                                      0xFFFFFFFFL
//MMMC_VM_FB_LOCATION_BASE_HI32
#define MMMC_VM_FB_LOCATION_BASE_HI32__FB_BASE_HI1__SHIFT                                                     0x0
#define MMMC_VM_FB_LOCATION_BASE_HI32__FB_BASE_HI1_MASK                                                       0x00000001L
//MMMC_VM_FB_LOCATION_TOP_LO32
#define MMMC_VM_FB_LOCATION_TOP_LO32__FB_TOP_LO32__SHIFT                                                      0x0
#define MMMC_VM_FB_LOCATION_TOP_LO32__FB_TOP_LO32_MASK                                                        0xFFFFFFFFL
//MMMC_VM_FB_LOCATION_TOP_HI32
#define MMMC_VM_FB_LOCATION_TOP_HI32__FB_TOP_HI1__SHIFT                                                       0x0
#define MMMC_VM_FB_LOCATION_TOP_HI32__FB_TOP_HI1_MASK                                                         0x00000001L
//MMMC_VM_AGP_TOP_LO32
#define MMMC_VM_AGP_TOP_LO32__AGP_TOP_LO32__SHIFT                                                             0x0
#define MMMC_VM_AGP_TOP_LO32__AGP_TOP_LO32_MASK                                                               0xFFFFFFFFL
//MMMC_VM_AGP_TOP_HI32
#define MMMC_VM_AGP_TOP_HI32__AGP_TOP_HI1__SHIFT                                                              0x0
#define MMMC_VM_AGP_TOP_HI32__AGP_TOP_HI1_MASK                                                                0x00000001L
//MMMC_VM_AGP_BOT_LO32
#define MMMC_VM_AGP_BOT_LO32__AGP_BOT_LO32__SHIFT                                                             0x0
#define MMMC_VM_AGP_BOT_LO32__AGP_BOT_LO32_MASK                                                               0xFFFFFFFFL
//MMMC_VM_AGP_BOT_HI32
#define MMMC_VM_AGP_BOT_HI32__AGP_BOT_HI1__SHIFT                                                              0x0
#define MMMC_VM_AGP_BOT_HI32__AGP_BOT_HI1_MASK                                                                0x00000001L
//MMMC_VM_AGP_BASE_LO32
#define MMMC_VM_AGP_BASE_LO32__AGP_BASE_LO32__SHIFT                                                           0x0
#define MMMC_VM_AGP_BASE_LO32__AGP_BASE_LO32_MASK                                                             0xFFFFFFFFL
//MMMC_VM_AGP_BASE_HI32
#define MMMC_VM_AGP_BASE_HI32__AGP_BASE_HI1__SHIFT                                                            0x0
#define MMMC_VM_AGP_BASE_HI32__AGP_BASE_HI1_MASK                                                              0x00000001L
//MMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32
#define MMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32__LOGICAL_ADDR_LO32__SHIFT                                       0x0
#define MMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32__LOGICAL_ADDR_LO32_MASK                                         0xFFFFFFFFL
//MMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32
#define MMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32__LOGICAL_ADDR_HI7__SHIFT                                        0x0
#define MMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32__LOGICAL_ADDR_HI7_MASK                                          0x0000007FL
//MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32
#define MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32__LOGICAL_ADDR_LO32__SHIFT                                      0x0
#define MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32__LOGICAL_ADDR_LO32_MASK                                        0xFFFFFFFFL
//MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32
#define MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32__LOGICAL_ADDR_HI7__SHIFT                                       0x0
#define MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32__LOGICAL_ADDR_HI7_MASK                                         0x0000007FL
//MMMC_VM_MX_L1_TLB_CNTL
#define MMMC_VM_MX_L1_TLB_CNTL__ENABLE_L1_TLB__SHIFT                                                          0x0
#define MMMC_VM_MX_L1_TLB_CNTL__SYSTEM_ACCESS_MODE__SHIFT                                                     0x3
#define MMMC_VM_MX_L1_TLB_CNTL__SYSTEM_APERTURE_UNMAPPED_ACCESS__SHIFT                                        0x5
#define MMMC_VM_MX_L1_TLB_CNTL__ENABLE_ADVANCED_DRIVER_MODEL__SHIFT                                           0x6
#define MMMC_VM_MX_L1_TLB_CNTL__ECO_BITS__SHIFT                                                               0x7
#define MMMC_VM_MX_L1_TLB_CNTL__MTYPE__SHIFT                                                                  0xb
#define MMMC_VM_MX_L1_TLB_CNTL__ENABLE_L1_TLB_MASK                                                            0x00000001L
#define MMMC_VM_MX_L1_TLB_CNTL__SYSTEM_ACCESS_MODE_MASK                                                       0x00000018L
#define MMMC_VM_MX_L1_TLB_CNTL__SYSTEM_APERTURE_UNMAPPED_ACCESS_MASK                                          0x00000020L
#define MMMC_VM_MX_L1_TLB_CNTL__ENABLE_ADVANCED_DRIVER_MODEL_MASK                                             0x00000040L
#define MMMC_VM_MX_L1_TLB_CNTL__ECO_BITS_MASK                                                                 0x00000780L
#define MMMC_VM_MX_L1_TLB_CNTL__MTYPE_MASK                                                                    0x00001800L


// addressBlock: mmhub_mmutcl2_mmvml2vcdec
//MMVM_CONTEXT0_CNTL
#define MMVM_CONTEXT0_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT0_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT0_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT0_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT0_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT0_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT0_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT0_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT0_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT0_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT0_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT0_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT0_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT0_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT0_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT0_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT0_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT0_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT0_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT0_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT0_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT0_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT0_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT0_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT0_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT0_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT0_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT0_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT0_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT0_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT0_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT0_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT0_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT0_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT1_CNTL
#define MMVM_CONTEXT1_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT1_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT1_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT1_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT1_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT1_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT1_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT1_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT1_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT1_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT1_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT1_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT1_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT1_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT2_CNTL
#define MMVM_CONTEXT2_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT2_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT2_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT2_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT2_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT2_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT2_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT2_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT2_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT2_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT2_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT2_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT2_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT2_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT2_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT2_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT2_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT2_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT2_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT2_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT2_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT2_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT2_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT2_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT2_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT2_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT2_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT2_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT2_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT2_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT2_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT2_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT2_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT2_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT2_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT2_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT2_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT2_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT2_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT2_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT2_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT2_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT3_CNTL
#define MMVM_CONTEXT3_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT3_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT3_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT3_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT3_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT3_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT3_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT3_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT3_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT3_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT3_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT3_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT3_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT3_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT3_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT3_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT3_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT3_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT3_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT3_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT3_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT3_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT3_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT3_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT3_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT3_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT3_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT3_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT3_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT3_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT3_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT3_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT3_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT3_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT3_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT3_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT3_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT3_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT3_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT3_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT3_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT3_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT4_CNTL
#define MMVM_CONTEXT4_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT4_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT4_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT4_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT4_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT4_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT4_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT4_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT4_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT4_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT4_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT4_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT4_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT4_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT4_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT4_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT4_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT4_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT4_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT4_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT4_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT4_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT4_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT4_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT4_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT4_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT4_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT4_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT4_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT4_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT4_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT4_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT4_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT4_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT4_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT4_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT4_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT4_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT4_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT4_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT4_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT4_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT5_CNTL
#define MMVM_CONTEXT5_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT5_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT5_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT5_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT5_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT5_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT5_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT5_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT5_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT5_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT5_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT5_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT5_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT5_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT5_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT5_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT5_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT5_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT5_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT5_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT5_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT5_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT5_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT5_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT5_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT5_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT5_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT5_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT5_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT5_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT5_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT5_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT5_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT5_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT5_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT5_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT5_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT5_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT5_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT5_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT5_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT5_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT6_CNTL
#define MMVM_CONTEXT6_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT6_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT6_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT6_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT6_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT6_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT6_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT6_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT6_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT6_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT6_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT6_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT6_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT6_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT6_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT6_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT6_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT6_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT6_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT6_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT6_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT6_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT6_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT6_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT6_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT6_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT6_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT6_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT6_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT6_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT6_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT6_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT6_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT6_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT6_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT6_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT6_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT6_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT6_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT6_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT6_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT6_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT7_CNTL
#define MMVM_CONTEXT7_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT7_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT7_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT7_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT7_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT7_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT7_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT7_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT7_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT7_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT7_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT7_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT7_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT7_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT7_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT7_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT7_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT7_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT7_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT7_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT7_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT7_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT7_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT7_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT7_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT7_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT7_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT7_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT7_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT7_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT7_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT7_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT7_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT7_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT7_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT7_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT7_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT7_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT7_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT7_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT7_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT7_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT8_CNTL
#define MMVM_CONTEXT8_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT8_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT8_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT8_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT8_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT8_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT8_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT8_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT8_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT8_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT8_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT8_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT8_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT8_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT8_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT8_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT8_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT8_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT8_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT8_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT8_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT8_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT8_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT8_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT8_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT8_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT8_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT8_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT8_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT8_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT8_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT8_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT8_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT8_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT8_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT8_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT8_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT8_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT8_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT8_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT8_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT8_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT9_CNTL
#define MMVM_CONTEXT9_CNTL__ENABLE_CONTEXT__SHIFT                                                             0x0
#define MMVM_CONTEXT9_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                           0x1
#define MMVM_CONTEXT9_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                      0x4
#define MMVM_CONTEXT9_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                     0x8
#define MMVM_CONTEXT9_CNTL__RETRY_OTHER_FAULT__SHIFT                                                          0x9
#define MMVM_CONTEXT9_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xa
#define MMVM_CONTEXT9_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xb
#define MMVM_CONTEXT9_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                               0xc
#define MMVM_CONTEXT9_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                 0xd
#define MMVM_CONTEXT9_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0xe
#define MMVM_CONTEXT9_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0xf
#define MMVM_CONTEXT9_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x10
#define MMVM_CONTEXT9_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x11
#define MMVM_CONTEXT9_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                     0x12
#define MMVM_CONTEXT9_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                       0x13
#define MMVM_CONTEXT9_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x14
#define MMVM_CONTEXT9_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x15
#define MMVM_CONTEXT9_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x16
#define MMVM_CONTEXT9_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x17
#define MMVM_CONTEXT9_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x18
#define MMVM_CONTEXT9_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x19
#define MMVM_CONTEXT9_CNTL__ENABLE_CONTEXT_MASK                                                               0x00000001L
#define MMVM_CONTEXT9_CNTL__PAGE_TABLE_DEPTH_MASK                                                             0x0000000EL
#define MMVM_CONTEXT9_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                        0x000000F0L
#define MMVM_CONTEXT9_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                       0x00000100L
#define MMVM_CONTEXT9_CNTL__RETRY_OTHER_FAULT_MASK                                                            0x00000200L
#define MMVM_CONTEXT9_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00000400L
#define MMVM_CONTEXT9_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00000800L
#define MMVM_CONTEXT9_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                 0x00001000L
#define MMVM_CONTEXT9_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                   0x00002000L
#define MMVM_CONTEXT9_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00004000L
#define MMVM_CONTEXT9_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00008000L
#define MMVM_CONTEXT9_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00010000L
#define MMVM_CONTEXT9_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00020000L
#define MMVM_CONTEXT9_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                       0x00040000L
#define MMVM_CONTEXT9_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                         0x00080000L
#define MMVM_CONTEXT9_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00100000L
#define MMVM_CONTEXT9_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00200000L
#define MMVM_CONTEXT9_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x00400000L
#define MMVM_CONTEXT9_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x00800000L
#define MMVM_CONTEXT9_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x01000000L
#define MMVM_CONTEXT9_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x02000000L
//MMVM_CONTEXT10_CNTL
#define MMVM_CONTEXT10_CNTL__ENABLE_CONTEXT__SHIFT                                                            0x0
#define MMVM_CONTEXT10_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                          0x1
#define MMVM_CONTEXT10_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                     0x4
#define MMVM_CONTEXT10_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                    0x8
#define MMVM_CONTEXT10_CNTL__RETRY_OTHER_FAULT__SHIFT                                                         0x9
#define MMVM_CONTEXT10_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0xa
#define MMVM_CONTEXT10_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0xb
#define MMVM_CONTEXT10_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                              0xc
#define MMVM_CONTEXT10_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                0xd
#define MMVM_CONTEXT10_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xe
#define MMVM_CONTEXT10_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xf
#define MMVM_CONTEXT10_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x10
#define MMVM_CONTEXT10_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x11
#define MMVM_CONTEXT10_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x12
#define MMVM_CONTEXT10_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x13
#define MMVM_CONTEXT10_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x14
#define MMVM_CONTEXT10_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x15
#define MMVM_CONTEXT10_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                 0x16
#define MMVM_CONTEXT10_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                   0x17
#define MMVM_CONTEXT10_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x18
#define MMVM_CONTEXT10_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x19
#define MMVM_CONTEXT10_CNTL__ENABLE_CONTEXT_MASK                                                              0x00000001L
#define MMVM_CONTEXT10_CNTL__PAGE_TABLE_DEPTH_MASK                                                            0x0000000EL
#define MMVM_CONTEXT10_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                       0x000000F0L
#define MMVM_CONTEXT10_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                      0x00000100L
#define MMVM_CONTEXT10_CNTL__RETRY_OTHER_FAULT_MASK                                                           0x00000200L
#define MMVM_CONTEXT10_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00000400L
#define MMVM_CONTEXT10_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00000800L
#define MMVM_CONTEXT10_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                0x00001000L
#define MMVM_CONTEXT10_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                  0x00002000L
#define MMVM_CONTEXT10_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00004000L
#define MMVM_CONTEXT10_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00008000L
#define MMVM_CONTEXT10_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00010000L
#define MMVM_CONTEXT10_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00020000L
#define MMVM_CONTEXT10_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00040000L
#define MMVM_CONTEXT10_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00080000L
#define MMVM_CONTEXT10_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00100000L
#define MMVM_CONTEXT10_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00200000L
#define MMVM_CONTEXT10_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                   0x00400000L
#define MMVM_CONTEXT10_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                     0x00800000L
#define MMVM_CONTEXT10_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x01000000L
#define MMVM_CONTEXT10_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x02000000L
//MMVM_CONTEXT11_CNTL
#define MMVM_CONTEXT11_CNTL__ENABLE_CONTEXT__SHIFT                                                            0x0
#define MMVM_CONTEXT11_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                          0x1
#define MMVM_CONTEXT11_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                     0x4
#define MMVM_CONTEXT11_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                    0x8
#define MMVM_CONTEXT11_CNTL__RETRY_OTHER_FAULT__SHIFT                                                         0x9
#define MMVM_CONTEXT11_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0xa
#define MMVM_CONTEXT11_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0xb
#define MMVM_CONTEXT11_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                              0xc
#define MMVM_CONTEXT11_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                0xd
#define MMVM_CONTEXT11_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xe
#define MMVM_CONTEXT11_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xf
#define MMVM_CONTEXT11_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x10
#define MMVM_CONTEXT11_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x11
#define MMVM_CONTEXT11_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x12
#define MMVM_CONTEXT11_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x13
#define MMVM_CONTEXT11_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x14
#define MMVM_CONTEXT11_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x15
#define MMVM_CONTEXT11_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                 0x16
#define MMVM_CONTEXT11_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                   0x17
#define MMVM_CONTEXT11_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x18
#define MMVM_CONTEXT11_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x19
#define MMVM_CONTEXT11_CNTL__ENABLE_CONTEXT_MASK                                                              0x00000001L
#define MMVM_CONTEXT11_CNTL__PAGE_TABLE_DEPTH_MASK                                                            0x0000000EL
#define MMVM_CONTEXT11_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                       0x000000F0L
#define MMVM_CONTEXT11_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                      0x00000100L
#define MMVM_CONTEXT11_CNTL__RETRY_OTHER_FAULT_MASK                                                           0x00000200L
#define MMVM_CONTEXT11_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00000400L
#define MMVM_CONTEXT11_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00000800L
#define MMVM_CONTEXT11_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                0x00001000L
#define MMVM_CONTEXT11_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                  0x00002000L
#define MMVM_CONTEXT11_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00004000L
#define MMVM_CONTEXT11_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00008000L
#define MMVM_CONTEXT11_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00010000L
#define MMVM_CONTEXT11_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00020000L
#define MMVM_CONTEXT11_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00040000L
#define MMVM_CONTEXT11_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00080000L
#define MMVM_CONTEXT11_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00100000L
#define MMVM_CONTEXT11_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00200000L
#define MMVM_CONTEXT11_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                   0x00400000L
#define MMVM_CONTEXT11_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                     0x00800000L
#define MMVM_CONTEXT11_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x01000000L
#define MMVM_CONTEXT11_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x02000000L
//MMVM_CONTEXT12_CNTL
#define MMVM_CONTEXT12_CNTL__ENABLE_CONTEXT__SHIFT                                                            0x0
#define MMVM_CONTEXT12_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                          0x1
#define MMVM_CONTEXT12_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                     0x4
#define MMVM_CONTEXT12_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                    0x8
#define MMVM_CONTEXT12_CNTL__RETRY_OTHER_FAULT__SHIFT                                                         0x9
#define MMVM_CONTEXT12_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0xa
#define MMVM_CONTEXT12_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0xb
#define MMVM_CONTEXT12_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                              0xc
#define MMVM_CONTEXT12_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                0xd
#define MMVM_CONTEXT12_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xe
#define MMVM_CONTEXT12_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xf
#define MMVM_CONTEXT12_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x10
#define MMVM_CONTEXT12_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x11
#define MMVM_CONTEXT12_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x12
#define MMVM_CONTEXT12_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x13
#define MMVM_CONTEXT12_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x14
#define MMVM_CONTEXT12_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x15
#define MMVM_CONTEXT12_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                 0x16
#define MMVM_CONTEXT12_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                   0x17
#define MMVM_CONTEXT12_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x18
#define MMVM_CONTEXT12_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x19
#define MMVM_CONTEXT12_CNTL__ENABLE_CONTEXT_MASK                                                              0x00000001L
#define MMVM_CONTEXT12_CNTL__PAGE_TABLE_DEPTH_MASK                                                            0x0000000EL
#define MMVM_CONTEXT12_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                       0x000000F0L
#define MMVM_CONTEXT12_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                      0x00000100L
#define MMVM_CONTEXT12_CNTL__RETRY_OTHER_FAULT_MASK                                                           0x00000200L
#define MMVM_CONTEXT12_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00000400L
#define MMVM_CONTEXT12_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00000800L
#define MMVM_CONTEXT12_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                0x00001000L
#define MMVM_CONTEXT12_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                  0x00002000L
#define MMVM_CONTEXT12_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00004000L
#define MMVM_CONTEXT12_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00008000L
#define MMVM_CONTEXT12_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00010000L
#define MMVM_CONTEXT12_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00020000L
#define MMVM_CONTEXT12_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00040000L
#define MMVM_CONTEXT12_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00080000L
#define MMVM_CONTEXT12_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00100000L
#define MMVM_CONTEXT12_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00200000L
#define MMVM_CONTEXT12_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                   0x00400000L
#define MMVM_CONTEXT12_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                     0x00800000L
#define MMVM_CONTEXT12_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x01000000L
#define MMVM_CONTEXT12_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x02000000L
//MMVM_CONTEXT13_CNTL
#define MMVM_CONTEXT13_CNTL__ENABLE_CONTEXT__SHIFT                                                            0x0
#define MMVM_CONTEXT13_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                          0x1
#define MMVM_CONTEXT13_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                     0x4
#define MMVM_CONTEXT13_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                    0x8
#define MMVM_CONTEXT13_CNTL__RETRY_OTHER_FAULT__SHIFT                                                         0x9
#define MMVM_CONTEXT13_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0xa
#define MMVM_CONTEXT13_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0xb
#define MMVM_CONTEXT13_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                              0xc
#define MMVM_CONTEXT13_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                0xd
#define MMVM_CONTEXT13_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xe
#define MMVM_CONTEXT13_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xf
#define MMVM_CONTEXT13_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x10
#define MMVM_CONTEXT13_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x11
#define MMVM_CONTEXT13_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x12
#define MMVM_CONTEXT13_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x13
#define MMVM_CONTEXT13_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x14
#define MMVM_CONTEXT13_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x15
#define MMVM_CONTEXT13_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                 0x16
#define MMVM_CONTEXT13_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                   0x17
#define MMVM_CONTEXT13_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x18
#define MMVM_CONTEXT13_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x19
#define MMVM_CONTEXT13_CNTL__ENABLE_CONTEXT_MASK                                                              0x00000001L
#define MMVM_CONTEXT13_CNTL__PAGE_TABLE_DEPTH_MASK                                                            0x0000000EL
#define MMVM_CONTEXT13_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                       0x000000F0L
#define MMVM_CONTEXT13_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                      0x00000100L
#define MMVM_CONTEXT13_CNTL__RETRY_OTHER_FAULT_MASK                                                           0x00000200L
#define MMVM_CONTEXT13_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00000400L
#define MMVM_CONTEXT13_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00000800L
#define MMVM_CONTEXT13_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                0x00001000L
#define MMVM_CONTEXT13_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                  0x00002000L
#define MMVM_CONTEXT13_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00004000L
#define MMVM_CONTEXT13_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00008000L
#define MMVM_CONTEXT13_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00010000L
#define MMVM_CONTEXT13_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00020000L
#define MMVM_CONTEXT13_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00040000L
#define MMVM_CONTEXT13_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00080000L
#define MMVM_CONTEXT13_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00100000L
#define MMVM_CONTEXT13_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00200000L
#define MMVM_CONTEXT13_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                   0x00400000L
#define MMVM_CONTEXT13_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                     0x00800000L
#define MMVM_CONTEXT13_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x01000000L
#define MMVM_CONTEXT13_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x02000000L
//MMVM_CONTEXT14_CNTL
#define MMVM_CONTEXT14_CNTL__ENABLE_CONTEXT__SHIFT                                                            0x0
#define MMVM_CONTEXT14_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                          0x1
#define MMVM_CONTEXT14_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                     0x4
#define MMVM_CONTEXT14_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                    0x8
#define MMVM_CONTEXT14_CNTL__RETRY_OTHER_FAULT__SHIFT                                                         0x9
#define MMVM_CONTEXT14_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0xa
#define MMVM_CONTEXT14_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0xb
#define MMVM_CONTEXT14_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                              0xc
#define MMVM_CONTEXT14_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                0xd
#define MMVM_CONTEXT14_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xe
#define MMVM_CONTEXT14_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xf
#define MMVM_CONTEXT14_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x10
#define MMVM_CONTEXT14_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x11
#define MMVM_CONTEXT14_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x12
#define MMVM_CONTEXT14_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x13
#define MMVM_CONTEXT14_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x14
#define MMVM_CONTEXT14_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x15
#define MMVM_CONTEXT14_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                 0x16
#define MMVM_CONTEXT14_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                   0x17
#define MMVM_CONTEXT14_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x18
#define MMVM_CONTEXT14_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x19
#define MMVM_CONTEXT14_CNTL__ENABLE_CONTEXT_MASK                                                              0x00000001L
#define MMVM_CONTEXT14_CNTL__PAGE_TABLE_DEPTH_MASK                                                            0x0000000EL
#define MMVM_CONTEXT14_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                       0x000000F0L
#define MMVM_CONTEXT14_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                      0x00000100L
#define MMVM_CONTEXT14_CNTL__RETRY_OTHER_FAULT_MASK                                                           0x00000200L
#define MMVM_CONTEXT14_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00000400L
#define MMVM_CONTEXT14_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00000800L
#define MMVM_CONTEXT14_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                0x00001000L
#define MMVM_CONTEXT14_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                  0x00002000L
#define MMVM_CONTEXT14_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00004000L
#define MMVM_CONTEXT14_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00008000L
#define MMVM_CONTEXT14_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00010000L
#define MMVM_CONTEXT14_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00020000L
#define MMVM_CONTEXT14_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00040000L
#define MMVM_CONTEXT14_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00080000L
#define MMVM_CONTEXT14_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00100000L
#define MMVM_CONTEXT14_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00200000L
#define MMVM_CONTEXT14_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                   0x00400000L
#define MMVM_CONTEXT14_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                     0x00800000L
#define MMVM_CONTEXT14_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x01000000L
#define MMVM_CONTEXT14_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x02000000L
//MMVM_CONTEXT15_CNTL
#define MMVM_CONTEXT15_CNTL__ENABLE_CONTEXT__SHIFT                                                            0x0
#define MMVM_CONTEXT15_CNTL__PAGE_TABLE_DEPTH__SHIFT                                                          0x1
#define MMVM_CONTEXT15_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT                                                     0x4
#define MMVM_CONTEXT15_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT                                    0x8
#define MMVM_CONTEXT15_CNTL__RETRY_OTHER_FAULT__SHIFT                                                         0x9
#define MMVM_CONTEXT15_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0xa
#define MMVM_CONTEXT15_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0xb
#define MMVM_CONTEXT15_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                              0xc
#define MMVM_CONTEXT15_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                0xd
#define MMVM_CONTEXT15_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0xe
#define MMVM_CONTEXT15_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0xf
#define MMVM_CONTEXT15_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x10
#define MMVM_CONTEXT15_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x11
#define MMVM_CONTEXT15_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                    0x12
#define MMVM_CONTEXT15_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                      0x13
#define MMVM_CONTEXT15_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                   0x14
#define MMVM_CONTEXT15_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                     0x15
#define MMVM_CONTEXT15_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                 0x16
#define MMVM_CONTEXT15_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                   0x17
#define MMVM_CONTEXT15_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT__SHIFT                                  0x18
#define MMVM_CONTEXT15_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT                                    0x19
#define MMVM_CONTEXT15_CNTL__ENABLE_CONTEXT_MASK                                                              0x00000001L
#define MMVM_CONTEXT15_CNTL__PAGE_TABLE_DEPTH_MASK                                                            0x0000000EL
#define MMVM_CONTEXT15_CNTL__PAGE_TABLE_BLOCK_SIZE_MASK                                                       0x000000F0L
#define MMVM_CONTEXT15_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT_MASK                                      0x00000100L
#define MMVM_CONTEXT15_CNTL__RETRY_OTHER_FAULT_MASK                                                           0x00000200L
#define MMVM_CONTEXT15_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00000400L
#define MMVM_CONTEXT15_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00000800L
#define MMVM_CONTEXT15_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                0x00001000L
#define MMVM_CONTEXT15_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                  0x00002000L
#define MMVM_CONTEXT15_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00004000L
#define MMVM_CONTEXT15_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00008000L
#define MMVM_CONTEXT15_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00010000L
#define MMVM_CONTEXT15_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00020000L
#define MMVM_CONTEXT15_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                      0x00040000L
#define MMVM_CONTEXT15_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                        0x00080000L
#define MMVM_CONTEXT15_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                     0x00100000L
#define MMVM_CONTEXT15_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                       0x00200000L
#define MMVM_CONTEXT15_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                   0x00400000L
#define MMVM_CONTEXT15_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                     0x00800000L
#define MMVM_CONTEXT15_CNTL__SECURE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK                                    0x01000000L
#define MMVM_CONTEXT15_CNTL__SECURE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK                                      0x02000000L
//MMVM_CONTEXTS_DISABLE
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_0__SHIFT                                                       0x0
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_1__SHIFT                                                       0x1
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_2__SHIFT                                                       0x2
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_3__SHIFT                                                       0x3
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_4__SHIFT                                                       0x4
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_5__SHIFT                                                       0x5
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_6__SHIFT                                                       0x6
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_7__SHIFT                                                       0x7
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_8__SHIFT                                                       0x8
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_9__SHIFT                                                       0x9
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_10__SHIFT                                                      0xa
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_11__SHIFT                                                      0xb
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_12__SHIFT                                                      0xc
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_13__SHIFT                                                      0xd
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_14__SHIFT                                                      0xe
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_15__SHIFT                                                      0xf
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_0_MASK                                                         0x00000001L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_1_MASK                                                         0x00000002L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_2_MASK                                                         0x00000004L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_3_MASK                                                         0x00000008L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_4_MASK                                                         0x00000010L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_5_MASK                                                         0x00000020L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_6_MASK                                                         0x00000040L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_7_MASK                                                         0x00000080L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_8_MASK                                                         0x00000100L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_9_MASK                                                         0x00000200L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_10_MASK                                                        0x00000400L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_11_MASK                                                        0x00000800L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_12_MASK                                                        0x00001000L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_13_MASK                                                        0x00002000L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_14_MASK                                                        0x00004000L
#define MMVM_CONTEXTS_DISABLE__DISABLE_CONTEXT_15_MASK                                                        0x00008000L
//MMVM_INVALIDATE_ENG0_SEM
#define MMVM_INVALIDATE_ENG0_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG0_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG1_SEM
#define MMVM_INVALIDATE_ENG1_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG1_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG2_SEM
#define MMVM_INVALIDATE_ENG2_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG2_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG3_SEM
#define MMVM_INVALIDATE_ENG3_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG3_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG4_SEM
#define MMVM_INVALIDATE_ENG4_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG4_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG5_SEM
#define MMVM_INVALIDATE_ENG5_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG5_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG6_SEM
#define MMVM_INVALIDATE_ENG6_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG6_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG7_SEM
#define MMVM_INVALIDATE_ENG7_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG7_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG8_SEM
#define MMVM_INVALIDATE_ENG8_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG8_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG9_SEM
#define MMVM_INVALIDATE_ENG9_SEM__SEMAPHORE__SHIFT                                                            0x0
#define MMVM_INVALIDATE_ENG9_SEM__SEMAPHORE_MASK                                                              0x00000001L
//MMVM_INVALIDATE_ENG10_SEM
#define MMVM_INVALIDATE_ENG10_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG10_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG11_SEM
#define MMVM_INVALIDATE_ENG11_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG11_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG12_SEM
#define MMVM_INVALIDATE_ENG12_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG12_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG13_SEM
#define MMVM_INVALIDATE_ENG13_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG13_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG14_SEM
#define MMVM_INVALIDATE_ENG14_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG14_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG15_SEM
#define MMVM_INVALIDATE_ENG15_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG15_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG16_SEM
#define MMVM_INVALIDATE_ENG16_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG16_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG17_SEM
#define MMVM_INVALIDATE_ENG17_SEM__SEMAPHORE__SHIFT                                                           0x0
#define MMVM_INVALIDATE_ENG17_SEM__SEMAPHORE_MASK                                                             0x00000001L
//MMVM_INVALIDATE_ENG0_REQ
#define MMVM_INVALIDATE_ENG0_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG0_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG0_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG0_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG0_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG0_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG0_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG0_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG0_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG1_REQ
#define MMVM_INVALIDATE_ENG1_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG1_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG1_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG1_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG1_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG1_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG1_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG1_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG1_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG2_REQ
#define MMVM_INVALIDATE_ENG2_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG2_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG2_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG2_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG2_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG2_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG2_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG2_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG2_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG3_REQ
#define MMVM_INVALIDATE_ENG3_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG3_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG3_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG3_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG3_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG3_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG3_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG3_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG3_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG4_REQ
#define MMVM_INVALIDATE_ENG4_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG4_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG4_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG4_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG4_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG4_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG4_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG4_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG4_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG5_REQ
#define MMVM_INVALIDATE_ENG5_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG5_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG5_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG5_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG5_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG5_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG5_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG5_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG5_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG6_REQ
#define MMVM_INVALIDATE_ENG6_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG6_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG6_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG6_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG6_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG6_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG6_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG6_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG6_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG7_REQ
#define MMVM_INVALIDATE_ENG7_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG7_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG7_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG7_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG7_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG7_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG7_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG7_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG7_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG8_REQ
#define MMVM_INVALIDATE_ENG8_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG8_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG8_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG8_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG8_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG8_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG8_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG8_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG8_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG9_REQ
#define MMVM_INVALIDATE_ENG9_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG9_REQ__FLUSH_TYPE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PTES__SHIFT                                                   0x13
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE0__SHIFT                                                   0x14
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE1__SHIFT                                                   0x15
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE2__SHIFT                                                   0x16
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L1_PTES__SHIFT                                                   0x17
#define MMVM_INVALIDATE_ENG9_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                   0x18
#define MMVM_INVALIDATE_ENG9_REQ__LOG_REQUEST__SHIFT                                                          0x19
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                             0x1a
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE3__SHIFT                                                   0x1b
#define MMVM_INVALIDATE_ENG9_REQ__PER_VMID_INVALIDATE_REQ_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG9_REQ__FLUSH_TYPE_MASK                                                             0x00070000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PTES_MASK                                                     0x00080000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE0_MASK                                                     0x00100000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE1_MASK                                                     0x00200000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE2_MASK                                                     0x00400000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L1_PTES_MASK                                                     0x00800000L
#define MMVM_INVALIDATE_ENG9_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                     0x01000000L
#define MMVM_INVALIDATE_ENG9_REQ__LOG_REQUEST_MASK                                                            0x02000000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                               0x04000000L
#define MMVM_INVALIDATE_ENG9_REQ__INVALIDATE_L2_PDE3_MASK                                                     0x08000000L
//MMVM_INVALIDATE_ENG10_REQ
#define MMVM_INVALIDATE_ENG10_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG10_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG10_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG10_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG10_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG10_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG10_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG10_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG10_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG11_REQ
#define MMVM_INVALIDATE_ENG11_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG11_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG11_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG11_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG11_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG11_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG11_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG11_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG11_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG12_REQ
#define MMVM_INVALIDATE_ENG12_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG12_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG12_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG12_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG12_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG12_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG12_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG12_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG12_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG13_REQ
#define MMVM_INVALIDATE_ENG13_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG13_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG13_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG13_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG13_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG13_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG13_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG13_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG13_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG14_REQ
#define MMVM_INVALIDATE_ENG14_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG14_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG14_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG14_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG14_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG14_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG14_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG14_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG14_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG15_REQ
#define MMVM_INVALIDATE_ENG15_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG15_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG15_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG15_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG15_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG15_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG15_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG15_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG15_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG16_REQ
#define MMVM_INVALIDATE_ENG16_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG16_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG16_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG16_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG16_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG16_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG16_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG16_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG17_REQ
#define MMVM_INVALIDATE_ENG17_REQ__PER_VMID_INVALIDATE_REQ__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG17_REQ__FLUSH_TYPE__SHIFT                                                          0x10
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PTES__SHIFT                                                  0x13
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE0__SHIFT                                                  0x14
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE1__SHIFT                                                  0x15
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE2__SHIFT                                                  0x16
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L1_PTES__SHIFT                                                  0x17
#define MMVM_INVALIDATE_ENG17_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR__SHIFT                                  0x18
#define MMVM_INVALIDATE_ENG17_REQ__LOG_REQUEST__SHIFT                                                         0x19
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_4K_PAGES_ONLY__SHIFT                                            0x1a
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE3__SHIFT                                                  0x1b
#define MMVM_INVALIDATE_ENG17_REQ__PER_VMID_INVALIDATE_REQ_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG17_REQ__FLUSH_TYPE_MASK                                                            0x00070000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PTES_MASK                                                    0x00080000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE0_MASK                                                    0x00100000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE1_MASK                                                    0x00200000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE2_MASK                                                    0x00400000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L1_PTES_MASK                                                    0x00800000L
#define MMVM_INVALIDATE_ENG17_REQ__CLEAR_PROTECTION_FAULT_STATUS_ADDR_MASK                                    0x01000000L
#define MMVM_INVALIDATE_ENG17_REQ__LOG_REQUEST_MASK                                                           0x02000000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_4K_PAGES_ONLY_MASK                                              0x04000000L
#define MMVM_INVALIDATE_ENG17_REQ__INVALIDATE_L2_PDE3_MASK                                                    0x08000000L
//MMVM_INVALIDATE_ENG0_ACK
#define MMVM_INVALIDATE_ENG0_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG0_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG0_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG0_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG1_ACK
#define MMVM_INVALIDATE_ENG1_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG1_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG1_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG1_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG2_ACK
#define MMVM_INVALIDATE_ENG2_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG2_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG2_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG2_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG3_ACK
#define MMVM_INVALIDATE_ENG3_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG3_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG3_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG3_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG4_ACK
#define MMVM_INVALIDATE_ENG4_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG4_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG4_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG4_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG5_ACK
#define MMVM_INVALIDATE_ENG5_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG5_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG5_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG5_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG6_ACK
#define MMVM_INVALIDATE_ENG6_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG6_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG6_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG6_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG7_ACK
#define MMVM_INVALIDATE_ENG7_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG7_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG7_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG7_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG8_ACK
#define MMVM_INVALIDATE_ENG8_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG8_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG8_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG8_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG9_ACK
#define MMVM_INVALIDATE_ENG9_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                              0x0
#define MMVM_INVALIDATE_ENG9_ACK__SEMAPHORE__SHIFT                                                            0x10
#define MMVM_INVALIDATE_ENG9_ACK__PER_VMID_INVALIDATE_ACK_MASK                                                0x0000FFFFL
#define MMVM_INVALIDATE_ENG9_ACK__SEMAPHORE_MASK                                                              0x00010000L
//MMVM_INVALIDATE_ENG10_ACK
#define MMVM_INVALIDATE_ENG10_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG10_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG10_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG10_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG11_ACK
#define MMVM_INVALIDATE_ENG11_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG11_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG11_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG11_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG12_ACK
#define MMVM_INVALIDATE_ENG12_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG12_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG12_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG12_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG13_ACK
#define MMVM_INVALIDATE_ENG13_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG13_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG13_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG13_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG14_ACK
#define MMVM_INVALIDATE_ENG14_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG14_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG14_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG14_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG15_ACK
#define MMVM_INVALIDATE_ENG15_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG15_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG15_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG15_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG16_ACK
#define MMVM_INVALIDATE_ENG16_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG16_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG16_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG16_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG17_ACK
#define MMVM_INVALIDATE_ENG17_ACK__PER_VMID_INVALIDATE_ACK__SHIFT                                             0x0
#define MMVM_INVALIDATE_ENG17_ACK__SEMAPHORE__SHIFT                                                           0x10
#define MMVM_INVALIDATE_ENG17_ACK__PER_VMID_INVALIDATE_ACK_MASK                                               0x0000FFFFL
#define MMVM_INVALIDATE_ENG17_ACK__SEMAPHORE_MASK                                                             0x00010000L
//MMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG1_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG1_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG1_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG2_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG2_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG2_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG2_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG2_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG2_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG2_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG2_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG3_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG3_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG3_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG3_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG3_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG3_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG3_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG3_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG4_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG4_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG4_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG4_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG4_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG4_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG4_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG4_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG5_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG5_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG5_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG5_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG5_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG5_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG5_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG5_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG6_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG6_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG6_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG6_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG6_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG6_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG6_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG6_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG7_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG7_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG7_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG7_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG7_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG7_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG7_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG7_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG8_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG8_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG8_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG8_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG8_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG8_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG8_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG8_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG9_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG9_ADDR_RANGE_LO32__S_BIT__SHIFT                                                    0x0
#define MMVM_INVALIDATE_ENG9_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                                0x1
#define MMVM_INVALIDATE_ENG9_ADDR_RANGE_LO32__S_BIT_MASK                                                      0x00000001L
#define MMVM_INVALIDATE_ENG9_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                  0xFFFFFFFEL
//MMVM_INVALIDATE_ENG9_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG9_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                                0x0
#define MMVM_INVALIDATE_ENG9_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                  0x00003FFFL
//MMVM_INVALIDATE_ENG10_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG10_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG10_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG10_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG10_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG10_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG10_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG10_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG11_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG11_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG11_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG11_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG11_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG11_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG11_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG11_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG12_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG12_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG12_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG12_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG12_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG12_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG12_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG12_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG13_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG13_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG13_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG13_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG13_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG13_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG13_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG13_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG14_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG14_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG14_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG14_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG14_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG14_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG14_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG14_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG15_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG15_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG15_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG15_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG15_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG15_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG15_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG15_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG16_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG16_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG16_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG16_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG16_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG16_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG16_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG16_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_INVALIDATE_ENG17_ADDR_RANGE_LO32
#define MMVM_INVALIDATE_ENG17_ADDR_RANGE_LO32__S_BIT__SHIFT                                                   0x0
#define MMVM_INVALIDATE_ENG17_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31__SHIFT                               0x1
#define MMVM_INVALIDATE_ENG17_ADDR_RANGE_LO32__S_BIT_MASK                                                     0x00000001L
#define MMVM_INVALIDATE_ENG17_ADDR_RANGE_LO32__LOGI_PAGE_ADDR_RANGE_LO31_MASK                                 0xFFFFFFFEL
//MMVM_INVALIDATE_ENG17_ADDR_RANGE_HI32
#define MMVM_INVALIDATE_ENG17_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14__SHIFT                               0x0
#define MMVM_INVALIDATE_ENG17_ADDR_RANGE_HI32__LOGI_PAGE_ADDR_RANGE_HI14_MASK                                 0x00003FFFL
//MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT2_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT2_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT2_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT2_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT2_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT2_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT3_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT3_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT3_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT3_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT3_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT3_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT4_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT4_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT4_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT4_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT4_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT4_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT5_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT5_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT5_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT5_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT5_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT5_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT6_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT6_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT6_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT6_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT6_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT6_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT7_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT7_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT7_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT7_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT7_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT7_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT8_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT8_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT8_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT8_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT8_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT8_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT9_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT9_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                             0x0
#define MMVM_CONTEXT9_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT9_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT9_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                             0x0
#define MMVM_CONTEXT9_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT10_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT10_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                            0x0
#define MMVM_CONTEXT10_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT10_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT10_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                            0x0
#define MMVM_CONTEXT10_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT11_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT11_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                            0x0
#define MMVM_CONTEXT11_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT11_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT11_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                            0x0
#define MMVM_CONTEXT11_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT12_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT12_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                            0x0
#define MMVM_CONTEXT12_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT12_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT12_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                            0x0
#define MMVM_CONTEXT12_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT13_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT13_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                            0x0
#define MMVM_CONTEXT13_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT13_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT13_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                            0x0
#define MMVM_CONTEXT13_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT14_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT14_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                            0x0
#define MMVM_CONTEXT14_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT14_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT14_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                            0x0
#define MMVM_CONTEXT14_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT15_PAGE_TABLE_BASE_ADDR_LO32
#define MMVM_CONTEXT15_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32__SHIFT                            0x0
#define MMVM_CONTEXT15_PAGE_TABLE_BASE_ADDR_LO32__PAGE_DIRECTORY_ENTRY_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT15_PAGE_TABLE_BASE_ADDR_HI32
#define MMVM_CONTEXT15_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32__SHIFT                            0x0
#define MMVM_CONTEXT15_PAGE_TABLE_BASE_ADDR_HI32__PAGE_DIRECTORY_ENTRY_HI32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT2_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT2_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT2_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT2_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT2_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT2_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT3_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT3_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT3_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT3_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT3_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT3_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT4_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT4_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT4_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT4_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT4_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT4_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT5_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT5_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT5_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT5_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT5_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT5_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT6_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT6_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT6_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT6_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT6_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT6_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT7_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT7_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT7_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT7_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT7_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT7_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT8_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT8_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT8_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT8_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT8_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT8_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT9_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT9_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                             0x0
#define MMVM_CONTEXT9_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                               0xFFFFFFFFL
//MMVM_CONTEXT9_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT9_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                             0x0
#define MMVM_CONTEXT9_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                               0x00001FFFL
//MMVM_CONTEXT10_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT10_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                            0x0
#define MMVM_CONTEXT10_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT10_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT10_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                            0x0
#define MMVM_CONTEXT10_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                              0x00001FFFL
//MMVM_CONTEXT11_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT11_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                            0x0
#define MMVM_CONTEXT11_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT11_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT11_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                            0x0
#define MMVM_CONTEXT11_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                              0x00001FFFL
//MMVM_CONTEXT12_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT12_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                            0x0
#define MMVM_CONTEXT12_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT12_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT12_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                            0x0
#define MMVM_CONTEXT12_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                              0x00001FFFL
//MMVM_CONTEXT13_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT13_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                            0x0
#define MMVM_CONTEXT13_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT13_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT13_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                            0x0
#define MMVM_CONTEXT13_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                              0x00001FFFL
//MMVM_CONTEXT14_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT14_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                            0x0
#define MMVM_CONTEXT14_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT14_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT14_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                            0x0
#define MMVM_CONTEXT14_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                              0x00001FFFL
//MMVM_CONTEXT15_PAGE_TABLE_START_ADDR_LO32
#define MMVM_CONTEXT15_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                            0x0
#define MMVM_CONTEXT15_PAGE_TABLE_START_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                              0xFFFFFFFFL
//MMVM_CONTEXT15_PAGE_TABLE_START_ADDR_HI32
#define MMVM_CONTEXT15_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                            0x0
#define MMVM_CONTEXT15_PAGE_TABLE_START_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                              0x00001FFFL
//MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT2_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT2_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT2_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT2_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT2_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT2_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT3_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT3_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT3_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT3_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT3_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT3_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT4_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT4_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT4_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT4_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT4_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT4_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT5_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT5_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT5_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT5_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT5_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT5_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT6_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT6_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT6_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT6_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT6_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT6_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT7_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT7_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT7_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT7_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT7_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT7_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT8_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT8_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT8_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT8_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT8_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT8_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT9_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT9_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                               0x0
#define MMVM_CONTEXT9_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                 0xFFFFFFFFL
//MMVM_CONTEXT9_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT9_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                               0x0
#define MMVM_CONTEXT9_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                 0x00001FFFL
//MMVM_CONTEXT10_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT10_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                              0x0
#define MMVM_CONTEXT10_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                0xFFFFFFFFL
//MMVM_CONTEXT10_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT10_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                              0x0
#define MMVM_CONTEXT10_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                0x00001FFFL
//MMVM_CONTEXT11_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT11_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                              0x0
#define MMVM_CONTEXT11_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                0xFFFFFFFFL
//MMVM_CONTEXT11_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT11_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                              0x0
#define MMVM_CONTEXT11_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                0x00001FFFL
//MMVM_CONTEXT12_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT12_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                              0x0
#define MMVM_CONTEXT12_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                0xFFFFFFFFL
//MMVM_CONTEXT12_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT12_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                              0x0
#define MMVM_CONTEXT12_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                0x00001FFFL
//MMVM_CONTEXT13_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT13_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                              0x0
#define MMVM_CONTEXT13_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                0xFFFFFFFFL
//MMVM_CONTEXT13_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT13_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                              0x0
#define MMVM_CONTEXT13_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                0x00001FFFL
//MMVM_CONTEXT14_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT14_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                              0x0
#define MMVM_CONTEXT14_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                0xFFFFFFFFL
//MMVM_CONTEXT14_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT14_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                              0x0
#define MMVM_CONTEXT14_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                0x00001FFFL
//MMVM_CONTEXT15_PAGE_TABLE_END_ADDR_LO32
#define MMVM_CONTEXT15_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32__SHIFT                              0x0
#define MMVM_CONTEXT15_PAGE_TABLE_END_ADDR_LO32__LOGICAL_PAGE_NUMBER_LO32_MASK                                0xFFFFFFFFL
//MMVM_CONTEXT15_PAGE_TABLE_END_ADDR_HI32
#define MMVM_CONTEXT15_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13__SHIFT                              0x0
#define MMVM_CONTEXT15_PAGE_TABLE_END_ADDR_HI32__LOGICAL_PAGE_NUMBER_HI13_MASK                                0x00001FFFL
//MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT                       0x0
#define MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                         0x5
#define MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                         0xa
#define MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                         0x0000001FL
#define MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                           0x000003E0L
#define MMVM_L2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                           0x0000FC00L
//MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT2_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT3_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT4_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT5_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT6_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT7_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT8_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT              0x0
#define MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT                0x5
#define MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                                0xa
#define MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK                0x0000001FL
#define MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                  0x000003E0L
#define MMVM_L2_CONTEXT9_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                  0x0000FC00L
//MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT             0x0
#define MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT               0x5
#define MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                               0xa
#define MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK               0x0000001FL
#define MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                 0x000003E0L
#define MMVM_L2_CONTEXT10_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                 0x0000FC00L
//MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT             0x0
#define MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT               0x5
#define MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                               0xa
#define MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK               0x0000001FL
#define MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                 0x000003E0L
#define MMVM_L2_CONTEXT11_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                 0x0000FC00L
//MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT             0x0
#define MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT               0x5
#define MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                               0xa
#define MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK               0x0000001FL
#define MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                 0x000003E0L
#define MMVM_L2_CONTEXT12_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                 0x0000FC00L
//MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT             0x0
#define MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT               0x5
#define MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                               0xa
#define MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK               0x0000001FL
#define MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                 0x000003E0L
#define MMVM_L2_CONTEXT13_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                 0x0000FC00L
//MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT             0x0
#define MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT               0x5
#define MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                               0xa
#define MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK               0x0000001FL
#define MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                 0x000003E0L
#define MMVM_L2_CONTEXT14_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                 0x0000FC00L
//MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES
#define MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE__SHIFT             0x0
#define MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT               0x5
#define MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT__SHIFT                               0xa
#define MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_SMALLK_FRAGMENT_SIZE_MASK               0x0000001FL
#define MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__L2_CACHE_BIGK_FRAGMENT_SIZE_MASK                 0x000003E0L
#define MMVM_L2_CONTEXT15_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES__BANK_SELECT_MASK                                 0x0000FC00L


// addressBlock: mmhub_mmutcl2_mmvmsharedvfdec
//MMMC_VM_PCIE_ATOMIC_SUPPORTED
#define MMMC_VM_PCIE_ATOMIC_SUPPORTED__PCIE_ATOMIC_SUPPORTED__SHIFT                                           0x0
#define MMMC_VM_PCIE_ATOMIC_SUPPORTED__PCIE_ATOMIC_SUPPORTED_MASK                                             0x00000001L


// addressBlock: mmhub_mmutcl2_mmvmsharedhvdec
//MMVM_PCIE_ATS_CNTL
#define MMVM_PCIE_ATS_CNTL__STU__SHIFT                                                                        0x10
#define MMVM_PCIE_ATS_CNTL__ATC_ENABLE__SHIFT                                                                 0x1f
#define MMVM_PCIE_ATS_CNTL__STU_MASK                                                                          0x001F0000L
#define MMVM_PCIE_ATS_CNTL__ATC_ENABLE_MASK                                                                   0x80000000L
//MMMC_VM_NB_MMIOBASE
#define MMMC_VM_NB_MMIOBASE__MMIOBASE__SHIFT                                                                  0x0
#define MMMC_VM_NB_MMIOBASE__MMIOBASE_MASK                                                                    0xFFFFFFFFL
//MMMC_VM_NB_MMIOLIMIT
#define MMMC_VM_NB_MMIOLIMIT__MMIOLIMIT__SHIFT                                                                0x0
#define MMMC_VM_NB_MMIOLIMIT__MMIOLIMIT_MASK                                                                  0xFFFFFFFFL
//MMMC_VM_NB_PCI_CTRL
#define MMMC_VM_NB_PCI_CTRL__MMIOENABLE__SHIFT                                                                0x17
#define MMMC_VM_NB_PCI_CTRL__MMIOENABLE_MASK                                                                  0x00800000L
//MMMC_VM_NB_PCI_ARB
#define MMMC_VM_NB_PCI_ARB__VGA_HOLE__SHIFT                                                                   0x3
#define MMMC_VM_NB_PCI_ARB__VGA_HOLE_MASK                                                                     0x00000008L
//MMMC_VM_NB_TOP_OF_DRAM_SLOT1
#define MMMC_VM_NB_TOP_OF_DRAM_SLOT1__TOP_OF_DRAM__SHIFT                                                      0x17
#define MMMC_VM_NB_TOP_OF_DRAM_SLOT1__TOP_OF_DRAM_MASK                                                        0xFF800000L
//MMMC_VM_NB_LOWER_TOP_OF_DRAM2
#define MMMC_VM_NB_LOWER_TOP_OF_DRAM2__ENABLE__SHIFT                                                          0x0
#define MMMC_VM_NB_LOWER_TOP_OF_DRAM2__LOWER_TOM2__SHIFT                                                      0x17
#define MMMC_VM_NB_LOWER_TOP_OF_DRAM2__ENABLE_MASK                                                            0x00000001L
#define MMMC_VM_NB_LOWER_TOP_OF_DRAM2__LOWER_TOM2_MASK                                                        0xFF800000L
//MMMC_VM_NB_UPPER_TOP_OF_DRAM2
#define MMMC_VM_NB_UPPER_TOP_OF_DRAM2__UPPER_TOM2__SHIFT                                                      0x0
#define MMMC_VM_NB_UPPER_TOP_OF_DRAM2__UPPER_TOM2_MASK                                                        0x000FFFFFL
//MMMC_VM_STEERING
#define MMMC_VM_STEERING__DEFAULT_STEERING__SHIFT                                                             0x0
#define MMMC_VM_STEERING__DEFAULT_STEERING_MASK                                                               0x00000003L
//MMMC_SHARED_VIRT_RESET_REQ
#define MMMC_SHARED_VIRT_RESET_REQ__VF__SHIFT                                                                 0x0
#define MMMC_SHARED_VIRT_RESET_REQ__PF__SHIFT                                                                 0x8
#define MMMC_SHARED_VIRT_RESET_REQ__VF_MASK                                                                   0x000000FFL
#define MMMC_SHARED_VIRT_RESET_REQ__PF_MASK                                                                   0x00000100L
//MMMC_VM_XGMI_LFB_CNTL
#define MMMC_VM_XGMI_LFB_CNTL__PF_LFB_REGION__SHIFT                                                           0x0
#define MMMC_VM_XGMI_LFB_CNTL__PF_MAX_REGION__SHIFT                                                           0x4
#define MMMC_VM_XGMI_LFB_CNTL__PF_LFB_REGION_MASK                                                             0x0000000FL
#define MMMC_VM_XGMI_LFB_CNTL__PF_MAX_REGION_MASK                                                             0x000000F0L
//MMMC_VM_XGMI_LFB_SIZE
#define MMMC_VM_XGMI_LFB_SIZE__PF_LFB_SIZE__SHIFT                                                             0x0
#define MMMC_VM_XGMI_LFB_SIZE__PF_LFB_SIZE_MASK                                                               0x0001FFFFL
//MMMC_VM_HOST_MAPPING
#define MMMC_VM_HOST_MAPPING__MODE__SHIFT                                                                     0x0
#define MMMC_VM_HOST_MAPPING__MODE_MASK                                                                       0x00000001L
//MMMC_VM_XGMI_GPUIOV_ENABLE
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF0__SHIFT                                                         0x0
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF1__SHIFT                                                         0x1
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF2__SHIFT                                                         0x2
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF3__SHIFT                                                         0x3
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF4__SHIFT                                                         0x4
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF5__SHIFT                                                         0x5
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF6__SHIFT                                                         0x6
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF7__SHIFT                                                         0x7
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_PF__SHIFT                                                          0x1f
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF0_MASK                                                           0x00000001L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF1_MASK                                                           0x00000002L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF2_MASK                                                           0x00000004L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF3_MASK                                                           0x00000008L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF4_MASK                                                           0x00000010L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF5_MASK                                                           0x00000020L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF6_MASK                                                           0x00000040L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_VF7_MASK                                                           0x00000080L
#define MMMC_VM_XGMI_GPUIOV_ENABLE__ENABLE_PF_MASK                                                            0x80000000L
//MMMC_VM_CACHEABLE_DRAM_ADDRESS_START
#define MMMC_VM_CACHEABLE_DRAM_ADDRESS_START__ADDRESS__SHIFT                                                  0x0
#define MMMC_VM_CACHEABLE_DRAM_ADDRESS_START__ADDRESS_MASK                                                    0x0FFFFFFFL
//MMMC_VM_CACHEABLE_DRAM_ADDRESS_END
#define MMMC_VM_CACHEABLE_DRAM_ADDRESS_END__ADDRESS__SHIFT                                                    0x0
#define MMMC_VM_CACHEABLE_DRAM_ADDRESS_END__ADDRESS_MASK                                                      0x0FFFFFFFL
//MMMC_VM_LOCAL_SYSMEM_ADDRESS_START
#define MMMC_VM_LOCAL_SYSMEM_ADDRESS_START__ADDRESS__SHIFT                                                    0x0
#define MMMC_VM_LOCAL_SYSMEM_ADDRESS_START__ADDRESS_MASK                                                      0x0FFFFFFFL
//MMMC_VM_LOCAL_SYSMEM_ADDRESS_END
#define MMMC_VM_LOCAL_SYSMEM_ADDRESS_END__ADDRESS__SHIFT                                                      0x0
#define MMMC_VM_LOCAL_SYSMEM_ADDRESS_END__ADDRESS_MASK                                                        0x0FFFFFFFL
//MMMC_VM_LPDDR_ADDRESS_START
#define MMMC_VM_LPDDR_ADDRESS_START__ADDRESS__SHIFT                                                           0x0
#define MMMC_VM_LPDDR_ADDRESS_START__ADDRESS_MASK                                                             0x0FFFFFFFL
//MMMC_VM_LPDDR_ADDRESS_END
#define MMMC_VM_LPDDR_ADDRESS_END__ADDRESS__SHIFT                                                             0x0
#define MMMC_VM_LPDDR_ADDRESS_END__ADDRESS_MASK                                                               0x0FFFFFFFL
//MMMC_VM_APT_CNTL
#define MMMC_VM_APT_CNTL__FORCE_MTYPE_UC__SHIFT                                                               0x0
#define MMMC_VM_APT_CNTL__DIRECT_SYSTEM_EN__SHIFT                                                             0x1
#define MMMC_VM_APT_CNTL__FRAG_APT_INTXN_MODE__SHIFT                                                          0x2
#define MMMC_VM_APT_CNTL__CHECK_IS_LOCAL__SHIFT                                                               0x4
#define MMMC_VM_APT_CNTL__CAP_FRAG_SIZE_2M__SHIFT                                                             0x5
#define MMMC_VM_APT_CNTL__LOCAL_SYSMEM_APERTURE_CNTL__SHIFT                                                   0x6
#define MMMC_VM_APT_CNTL__LPDDR_APERTURE_CNTL__SHIFT                                                          0x8
#define MMMC_VM_APT_CNTL__FORCE_MTYPE_UC_MASK                                                                 0x00000001L
#define MMMC_VM_APT_CNTL__DIRECT_SYSTEM_EN_MASK                                                               0x00000002L
#define MMMC_VM_APT_CNTL__FRAG_APT_INTXN_MODE_MASK                                                            0x0000000CL
#define MMMC_VM_APT_CNTL__CHECK_IS_LOCAL_MASK                                                                 0x00000010L
#define MMMC_VM_APT_CNTL__CAP_FRAG_SIZE_2M_MASK                                                               0x00000020L
#define MMMC_VM_APT_CNTL__LOCAL_SYSMEM_APERTURE_CNTL_MASK                                                     0x000000C0L
#define MMMC_VM_APT_CNTL__LPDDR_APERTURE_CNTL_MASK                                                            0x00000300L
//MMUTCL2_HARVEST_BYPASS_GROUPS
#define MMUTCL2_HARVEST_BYPASS_GROUPS__BYPASS_GROUPS__SHIFT                                                   0x0
#define MMUTCL2_HARVEST_BYPASS_GROUPS__BYPASS_GROUPS_MASK                                                     0xFFFFFFFFL


// addressBlock: mmhub_mmutcl2_mmatcl2dec
//MM_ATC_L2_CNTL
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_READ_REQUESTS__SHIFT                                            0x0
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_WRITE_REQUESTS__SHIFT                                           0x2
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_READS_DEPENDS_ON_ADDR_MOD__SHIFT                                0x4
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_WRITES_DEPENDS_ON_ADDR_MOD__SHIFT                               0x5
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_READ_REQUESTS__SHIFT                                       0x6
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_WRITE_REQUESTS__SHIFT                                      0x8
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_READS_DEPENDS_ON_ADDR_MOD__SHIFT                           0xa
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_WRITES_DEPENDS_ON_ADDR_MOD__SHIFT                          0xb
#define MM_ATC_L2_CNTL__CACHE_INVALIDATE_MODE__SHIFT                                                          0xc
#define MM_ATC_L2_CNTL__FRAG_APT_INTXN_MODE__SHIFT                                                            0xf
#define MM_ATC_L2_CNTL__CLI_GPA_REQ_FRAG_SIZE__SHIFT                                                          0x11
#define MM_ATC_L2_CNTL__CLI_GVA_REQ_FRAG_SIZE__SHIFT                                                          0x17
#define MM_ATC_L2_CNTL__FORCE_8T_CAP_ON_AT_RETURN__SHIFT                                                      0x1d
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_READ_REQUESTS_MASK                                              0x00000003L
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_WRITE_REQUESTS_MASK                                             0x0000000CL
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_READS_DEPENDS_ON_ADDR_MOD_MASK                                  0x00000010L
#define MM_ATC_L2_CNTL__NUMBER_OF_TRANSLATION_WRITES_DEPENDS_ON_ADDR_MOD_MASK                                 0x00000020L
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_READ_REQUESTS_MASK                                         0x000000C0L
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_WRITE_REQUESTS_MASK                                        0x00000300L
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_READS_DEPENDS_ON_ADDR_MOD_MASK                             0x00000400L
#define MM_ATC_L2_CNTL__NUMBER_OF_HOST_TRANSLATION_WRITES_DEPENDS_ON_ADDR_MOD_MASK                            0x00000800L
#define MM_ATC_L2_CNTL__CACHE_INVALIDATE_MODE_MASK                                                            0x00007000L
#define MM_ATC_L2_CNTL__FRAG_APT_INTXN_MODE_MASK                                                              0x00018000L
#define MM_ATC_L2_CNTL__CLI_GPA_REQ_FRAG_SIZE_MASK                                                            0x007E0000L
//MM_ATC_L2_CNTL2
#define MM_ATC_L2_CNTL2__BANK_SELECT__SHIFT                                                                   0x0
#define MM_ATC_L2_CNTL2__NUM_BANKS_LOG2__SHIFT                                                                0x6
#define MM_ATC_L2_CNTL2__L2_CACHE_UPDATE_MODE__SHIFT                                                          0x9
#define MM_ATC_L2_CNTL2__ENABLE_L2_CACHE_LRU_UPDATE_BY_WRITE__SHIFT                                           0xb
#define MM_ATC_L2_CNTL2__L2_CACHE_SWAP_TAG_INDEX_LSBS__SHIFT                                                  0xc
#define MM_ATC_L2_CNTL2__L2_CACHE_VMID_MODE__SHIFT                                                            0xf
#define MM_ATC_L2_CNTL2__L2_CACHE_UPDATE_WILDCARD_REFERENCE_VALUE__SHIFT                                      0x12
#define MM_ATC_L2_CNTL2__BANK_SELECT_MASK                                                                     0x0000003FL
#define MM_ATC_L2_CNTL2__NUM_BANKS_LOG2_MASK                                                                  0x000001C0L
#define MM_ATC_L2_CNTL2__L2_CACHE_UPDATE_MODE_MASK                                                            0x00000600L
#define MM_ATC_L2_CNTL2__ENABLE_L2_CACHE_LRU_UPDATE_BY_WRITE_MASK                                             0x00000800L
#define MM_ATC_L2_CNTL2__L2_CACHE_SWAP_TAG_INDEX_LSBS_MASK                                                    0x00007000L
#define MM_ATC_L2_CNTL2__L2_CACHE_VMID_MODE_MASK                                                              0x00038000L
#define MM_ATC_L2_CNTL2__L2_CACHE_UPDATE_WILDCARD_REFERENCE_VALUE_MASK                                        0x00FC0000L
//MM_ATC_L2_CACHE_DATA0
#define MM_ATC_L2_CACHE_DATA0__DATA_REGISTER_VALID__SHIFT                                                     0x0
#define MM_ATC_L2_CACHE_DATA0__CACHE_ENTRY_VALID__SHIFT                                                       0x1
#define MM_ATC_L2_CACHE_DATA0__CACHED_ATTRIBUTES__SHIFT                                                       0x2
#define MM_ATC_L2_CACHE_DATA0__DATA_REGISTER_VALID_MASK                                                       0x00000001L
#define MM_ATC_L2_CACHE_DATA0__CACHE_ENTRY_VALID_MASK                                                         0x00000002L
#define MM_ATC_L2_CACHE_DATA0__CACHED_ATTRIBUTES_MASK                                                         0x00FFFFFCL
//MM_ATC_L2_CACHE_DATA1
#define MM_ATC_L2_CACHE_DATA1__VIRTUAL_PAGE_ADDRESS_LOW__SHIFT                                                0x0
#define MM_ATC_L2_CACHE_DATA1__VIRTUAL_PAGE_ADDRESS_LOW_MASK                                                  0xFFFFFFFFL
//MM_ATC_L2_CACHE_DATA2
#define MM_ATC_L2_CACHE_DATA2__PHYSICAL_PAGE_ADDRESS__SHIFT                                                   0x0
#define MM_ATC_L2_CACHE_DATA2__PHYSICAL_PAGE_ADDRESS_MASK                                                     0xFFFFFFFFL
//MM_ATC_L2_CNTL3
#define MM_ATC_L2_CNTL3__L2_SMALLK_CACHE_FRAGMENT_SIZE__SHIFT                                                 0x0
#define MM_ATC_L2_CNTL3__L2_BIGK_CACHE_FRAGMENT_SIZE__SHIFT                                                   0x6
#define MM_ATC_L2_CNTL3__DELAY_SEND_INVALIDATION_REQUEST__SHIFT                                               0xc
#define MM_ATC_L2_CNTL3__ATS_REQUEST_CREDIT_MINUS1__SHIFT                                                     0xf
#define MM_ATC_L2_CNTL3__COMPCLKREQ_OFF_HYSTERESIS__SHIFT                                                     0x15
#define MM_ATC_L2_CNTL3__REPEATER_FGCG_OFF__SHIFT                                                             0x18
#define MM_ATC_L2_CNTL3__L2_SMALLK_CACHE_FRAGMENT_SIZE_MASK                                                   0x0000003FL
#define MM_ATC_L2_CNTL3__L2_BIGK_CACHE_FRAGMENT_SIZE_MASK                                                     0x00000FC0L
#define MM_ATC_L2_CNTL3__DELAY_SEND_INVALIDATION_REQUEST_MASK                                                 0x00007000L
#define MM_ATC_L2_CNTL3__ATS_REQUEST_CREDIT_MINUS1_MASK                                                       0x001F8000L
#define MM_ATC_L2_CNTL3__COMPCLKREQ_OFF_HYSTERESIS_MASK                                                       0x00E00000L
#define MM_ATC_L2_CNTL3__REPEATER_FGCG_OFF_MASK                                                               0x01000000L
//MM_ATC_L2_CNTL4
#define MM_ATC_L2_CNTL4__CANE_CLKEN_OVERRIDE__SHIFT                                                           0x0
#define MM_ATC_L2_CNTL4__L2_RT_SMALLK_CACHE_FRAGMENT_SIZE__SHIFT                                              0x1
#define MM_ATC_L2_CNTL4__L2_RT_BIGK_CACHE_FRAGMENT_SIZE__SHIFT                                                0x7
#define MM_ATC_L2_CNTL4__L2_RT_SMALLK_CACHE_FRAGMENT_SIZE_MASK                                                0x0000007EL
#define MM_ATC_L2_CNTL4__L2_RT_BIGK_CACHE_FRAGMENT_SIZE_MASK                                                  0x00001F80L
//MM_ATC_L2_CNTL5
#define MM_ATC_L2_CNTL5__MM_NONRT_IFIFO_ACTIVE_TRANSACTION_LIMIT__SHIFT                                       0x0
#define MM_ATC_L2_CNTL5__MM_SOFTRT_IFIFO_ACTIVE_TRANSACTION_LIMIT__SHIFT                                      0xa
#define MM_ATC_L2_CNTL5__MM_NONRT_IFIFO_ACTIVE_TRANSACTION_LIMIT_MASK                                         0x000003FFL
#define MM_ATC_L2_CNTL5__MM_SOFTRT_IFIFO_ACTIVE_TRANSACTION_LIMIT_MASK                                        0x000FFC00L
//MM_ATC_L2_MM_GROUP_RT_CLASSES
#define MM_ATC_L2_MM_GROUP_RT_CLASSES__GROUP_RT_CLASS__SHIFT                                                  0x0
#define MM_ATC_L2_MM_GROUP_RT_CLASSES__GROUP_RT_CLASS_MASK                                                    0xFFFFFFFFL
//MM_ATC_L2_STATUS
#define MM_ATC_L2_STATUS__BUSY__SHIFT                                                                         0x0
#define MM_ATC_L2_STATUS__NO_OUTSTANDING_AT_REQUESTS__SHIFT                                                   0x1
#define MM_ATC_L2_STATUS__BUSY_MASK                                                                           0x00000001L
#define MM_ATC_L2_STATUS__NO_OUTSTANDING_AT_REQUESTS_MASK                                                     0x00000002L
//MM_ATC_L2_MISC_CG
#define MM_ATC_L2_MISC_CG__OFFDLY__SHIFT                                                                      0x6
#define MM_ATC_L2_MISC_CG__ENABLE__SHIFT                                                                      0x12
#define MM_ATC_L2_MISC_CG__MEM_LS_ENABLE__SHIFT                                                               0x13
#define MM_ATC_L2_MISC_CG__OFFDLY_MASK                                                                        0x00000FC0L
#define MM_ATC_L2_MISC_CG__ENABLE_MASK                                                                        0x00040000L
#define MM_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK                                                                 0x00080000L
//MM_ATC_L2_CGTT_CLK_CTRL
#define MM_ATC_L2_CGTT_CLK_CTRL__ON_DELAY__SHIFT                                                              0x0
#define MM_ATC_L2_CGTT_CLK_CTRL__OFF_HYSTERESIS__SHIFT                                                        0x5
#define MM_ATC_L2_CGTT_CLK_CTRL__LS_ASSERT_HYSTERESIS__SHIFT                                                  0xd
#define MM_ATC_L2_CGTT_CLK_CTRL__MIN_MGLS__SHIFT                                                              0x1a
#define MM_ATC_L2_CGTT_CLK_CTRL__CGLS_DISABLE__SHIFT                                                          0x1d
#define MM_ATC_L2_CGTT_CLK_CTRL__LS_DISABLE__SHIFT                                                            0x1e
#define MM_ATC_L2_CGTT_CLK_CTRL__BUSY_OVERRIDE__SHIFT                                                         0x1f
#define MM_ATC_L2_CGTT_CLK_CTRL__ON_DELAY_MASK                                                                0x0000001FL
#define MM_ATC_L2_CGTT_CLK_CTRL__OFF_HYSTERESIS_MASK                                                          0x00001FE0L
#define MM_ATC_L2_CGTT_CLK_CTRL__LS_ASSERT_HYSTERESIS_MASK                                                    0x03FFE000L
#define MM_ATC_L2_CGTT_CLK_CTRL__MIN_MGLS_MASK                                                                0x1C000000L
#define MM_ATC_L2_CGTT_CLK_CTRL__CGLS_DISABLE_MASK                                                            0x20000000L
#define MM_ATC_L2_CGTT_CLK_CTRL__LS_DISABLE_MASK                                                              0x40000000L
#define MM_ATC_L2_CGTT_CLK_CTRL__BUSY_OVERRIDE_MASK                                                           0x80000000L
//MM_ATC_L2_SDPPORT_CTRL
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPCKEN__SHIFT                                                      0x0
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPCKENRCV__SHIFT                                                   0x1
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPDATACKEN__SHIFT                                                  0x2
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPDATACKENRCV__SHIFT                                               0x3
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_WRRSPCKEN__SHIFT                                                      0x4
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_WRRSPCKENRCV__SHIFT                                                   0x5
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_REQCKEN__SHIFT                                                        0x6
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_REQCKENRCV__SHIFT                                                     0x7
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_ORIGDATACKEN__SHIFT                                                   0x8
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_ORIGDATACKENRCV__SHIFT                                                0x9
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPCKEN_MASK                                                        0x00000001L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPCKENRCV_MASK                                                     0x00000002L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPDATACKEN_MASK                                                    0x00000004L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_RDRSPDATACKENRCV_MASK                                                 0x00000008L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_WRRSPCKEN_MASK                                                        0x00000010L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_WRRSPCKENRCV_MASK                                                     0x00000020L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_REQCKEN_MASK                                                          0x00000040L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_REQCKENRCV_MASK                                                       0x00000080L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_ORIGDATACKEN_MASK                                                     0x00000100L
#define MM_ATC_L2_SDPPORT_CTRL__SDPVDCI_ORIGDATACKENRCV_MASK                                                  0x00000200L


// addressBlock: mmhub_mmutcl2_mmvml2pspdec
//MMVM_IOMMU_CONTROL_REGISTER
#define MMVM_IOMMU_CONTROL_REGISTER__IOMMUEN__SHIFT                                                           0x0
#define MMVM_IOMMU_CONTROL_REGISTER__IOMMUEN_MASK                                                             0x00000001L
//MMVM_IOMMU_PERFORMANCE_OPTIMIZATION_CONTROL_REGISTER
#define MMVM_IOMMU_PERFORMANCE_OPTIMIZATION_CONTROL_REGISTER__PERFOPTEN__SHIFT                                0xd
#define MMVM_IOMMU_PERFORMANCE_OPTIMIZATION_CONTROL_REGISTER__PERFOPTEN_MASK                                  0x00002000L


// addressBlock: mmhub_mmutcl2_mmvmsharedpspdec
//MMMC_VM_FB_OFFSET
#define MMMC_VM_FB_OFFSET__FB_OFFSET__SHIFT                                                                   0x0
#define MMMC_VM_FB_OFFSET__FB_OFFSET_MASK                                                                     0x0FFFFFFFL


#endif
