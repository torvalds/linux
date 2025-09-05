/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _clc36f_h_
#define _clc36f_h_

#define NVC36F_NON_STALL_INTERRUPT                                 (0x00000020)
#define NVC36F_NON_STALL_INTERRUPT_HANDLE                                 31:0
// NOTE - MEM_OP_A and MEM_OP_B have been replaced in gp100 with methods for
// specifying the page address for a targeted TLB invalidate and the uTLB for
// a targeted REPLAY_CANCEL for UVM.
// The previous MEM_OP_A/B functionality is in MEM_OP_C/D, with slightly
// rearranged fields.
#define NVC36F_MEM_OP_A                                            (0x00000028)
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_CANCEL_TARGET_CLIENT_UNIT_ID        5:0  // only relevant for REPLAY_CANCEL_TARGETED
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_INVALIDATION_SIZE                   5:0  // Used to specify size of invalidate, used for invalidates which are not of the REPLAY_CANCEL_TARGETED type
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_CANCEL_TARGET_GPC_ID               10:6  // only relevant for REPLAY_CANCEL_TARGETED
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_CANCEL_MMU_ENGINE_ID                6:0  // only relevant for REPLAY_CANCEL_VA_GLOBAL
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_SYSMEMBAR                         11:11
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_SYSMEMBAR_EN                 0x00000001
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_SYSMEMBAR_DIS                0x00000000
#define NVC36F_MEM_OP_A_TLB_INVALIDATE_TARGET_ADDR_LO                    31:12
#define NVC36F_MEM_OP_B                                            (0x0000002c)
#define NVC36F_MEM_OP_B_TLB_INVALIDATE_TARGET_ADDR_HI                     31:0
#define NVC36F_MEM_OP_C                                            (0x00000030)
#define NVC36F_MEM_OP_C_MEMBAR_TYPE                                        2:0
#define NVC36F_MEM_OP_C_MEMBAR_TYPE_SYS_MEMBAR                      0x00000000
#define NVC36F_MEM_OP_C_MEMBAR_TYPE_MEMBAR                          0x00000001
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB                                 0:0
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_ONE                      0x00000000
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_ALL                      0x00000001  // Probably nonsensical for MMU_TLB_INVALIDATE_TARGETED
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_GPC                                 1:1
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_GPC_ENABLE                   0x00000000
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_GPC_DISABLE                  0x00000001
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY                              4:2  // only relevant if GPC ENABLE
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY_NONE                  0x00000000
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY_START                 0x00000001
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY_START_ACK_ALL         0x00000002
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY_CANCEL_TARGETED       0x00000003
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY_CANCEL_GLOBAL         0x00000004
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_REPLAY_CANCEL_VA_GLOBAL      0x00000005
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACK_TYPE                            6:5  // only relevant if GPC ENABLE
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACK_TYPE_NONE                0x00000000
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACK_TYPE_GLOBALLY            0x00000001
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACK_TYPE_INTRANODE           0x00000002
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE                         9:7 //only relevant for REPLAY_CANCEL_VA_GLOBAL
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_READ                 0
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_WRITE                1
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_ATOMIC_STRONG        2
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_RSVRVD               3
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_ATOMIC_WEAK          4
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_ATOMIC_ALL           5
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_WRITE_AND_ATOMIC     6
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_ACCESS_TYPE_VIRT_ALL                  7
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL                    9:7  // Invalidate affects this level and all below
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_ALL         0x00000000  // Invalidate tlb caches at all levels of the page table
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_PTE_ONLY    0x00000001
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_UP_TO_PDE0  0x00000002
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_UP_TO_PDE1  0x00000003
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_UP_TO_PDE2  0x00000004
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_UP_TO_PDE3  0x00000005
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_UP_TO_PDE4  0x00000006
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PAGE_TABLE_LEVEL_UP_TO_PDE5  0x00000007
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_APERTURE                          11:10  // only relevant if PDB_ONE
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_APERTURE_VID_MEM             0x00000000
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_APERTURE_SYS_MEM_COHERENT    0x00000002
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_APERTURE_SYS_MEM_NONCOHERENT 0x00000003
#define NVC36F_MEM_OP_C_TLB_INVALIDATE_PDB_ADDR_LO                       31:12  // only relevant if PDB_ONE
#define NVC36F_MEM_OP_C_ACCESS_COUNTER_CLR_TARGETED_NOTIFY_TAG            19:0
// MEM_OP_D MUST be preceded by MEM_OPs A-C.
#define NVC36F_MEM_OP_D                                            (0x00000034)
#define NVC36F_MEM_OP_D_TLB_INVALIDATE_PDB_ADDR_HI                        26:0  // only relevant if PDB_ONE
#define NVC36F_MEM_OP_D_OPERATION                                        31:27
#define NVC36F_MEM_OP_D_OPERATION_MEMBAR                            0x00000005
#define NVC36F_MEM_OP_D_OPERATION_MMU_TLB_INVALIDATE                0x00000009
#define NVC36F_MEM_OP_D_OPERATION_MMU_TLB_INVALIDATE_TARGETED       0x0000000a
#define NVC36F_MEM_OP_D_OPERATION_L2_PEERMEM_INVALIDATE             0x0000000d
#define NVC36F_MEM_OP_D_OPERATION_L2_SYSMEM_INVALIDATE              0x0000000e
// CLEAN_LINES is an alias for Tegra/GPU IP usage
#define NVC36F_MEM_OP_B_OPERATION_L2_INVALIDATE_CLEAN_LINES         0x0000000e
#define NVC36F_MEM_OP_D_OPERATION_L2_CLEAN_COMPTAGS                 0x0000000f
#define NVC36F_MEM_OP_D_OPERATION_L2_FLUSH_DIRTY                    0x00000010
#define NVC36F_MEM_OP_D_OPERATION_L2_WAIT_FOR_SYS_PENDING_READS     0x00000015
#define NVC36F_MEM_OP_D_OPERATION_ACCESS_COUNTER_CLR                0x00000016
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TYPE                            1:0
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TYPE_MIMC                0x00000000
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TYPE_MOMC                0x00000001
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TYPE_ALL                 0x00000002
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TYPE_TARGETED            0x00000003
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TARGETED_TYPE                   2:2
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TARGETED_TYPE_MIMC       0x00000000
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TARGETED_TYPE_MOMC       0x00000001
#define NVC36F_MEM_OP_D_ACCESS_COUNTER_CLR_TARGETED_BANK                   6:3
#define NVC36F_SEM_ADDR_LO                                         (0x0000005c)
#define NVC36F_SEM_ADDR_LO_OFFSET                                         31:2
#define NVC36F_SEM_ADDR_HI                                         (0x00000060)
#define NVC36F_SEM_ADDR_HI_OFFSET                                          7:0
#define NVC36F_SEM_PAYLOAD_LO                                      (0x00000064)
#define NVC36F_SEM_PAYLOAD_LO_PAYLOAD                                     31:0
#define NVC36F_SEM_PAYLOAD_HI                                      (0x00000068)
#define NVC36F_SEM_PAYLOAD_HI_PAYLOAD                                     31:0
#define NVC36F_SEM_EXECUTE                                         (0x0000006c)
#define NVC36F_SEM_EXECUTE_OPERATION                                       2:0
#define NVC36F_SEM_EXECUTE_OPERATION_ACQUIRE                        0x00000000
#define NVC36F_SEM_EXECUTE_OPERATION_RELEASE                        0x00000001
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_STRICT_GEQ                 0x00000002
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_CIRC_GEQ                   0x00000003
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_AND                        0x00000004
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_NOR                        0x00000005
#define NVC36F_SEM_EXECUTE_OPERATION_REDUCTION                      0x00000006
#define NVC36F_SEM_EXECUTE_ACQUIRE_SWITCH_TSG                            12:12
#define NVC36F_SEM_EXECUTE_ACQUIRE_SWITCH_TSG_DIS                   0x00000000
#define NVC36F_SEM_EXECUTE_ACQUIRE_SWITCH_TSG_EN                    0x00000001
#define NVC36F_SEM_EXECUTE_RELEASE_WFI                                   20:20
#define NVC36F_SEM_EXECUTE_RELEASE_WFI_DIS                          0x00000000
#define NVC36F_SEM_EXECUTE_RELEASE_WFI_EN                           0x00000001
#define NVC36F_SEM_EXECUTE_PAYLOAD_SIZE                                  24:24
#define NVC36F_SEM_EXECUTE_PAYLOAD_SIZE_32BIT                       0x00000000
#define NVC36F_SEM_EXECUTE_PAYLOAD_SIZE_64BIT                       0x00000001
#define NVC36F_SEM_EXECUTE_RELEASE_TIMESTAMP                             25:25
#define NVC36F_SEM_EXECUTE_RELEASE_TIMESTAMP_DIS                    0x00000000
#define NVC36F_SEM_EXECUTE_RELEASE_TIMESTAMP_EN                     0x00000001
#define NVC36F_SEM_EXECUTE_REDUCTION                                     30:27
#define NVC36F_SEM_EXECUTE_REDUCTION_IMIN                           0x00000000
#define NVC36F_SEM_EXECUTE_REDUCTION_IMAX                           0x00000001
#define NVC36F_SEM_EXECUTE_REDUCTION_IXOR                           0x00000002
#define NVC36F_SEM_EXECUTE_REDUCTION_IAND                           0x00000003
#define NVC36F_SEM_EXECUTE_REDUCTION_IOR                            0x00000004
#define NVC36F_SEM_EXECUTE_REDUCTION_IADD                           0x00000005
#define NVC36F_SEM_EXECUTE_REDUCTION_INC                            0x00000006
#define NVC36F_SEM_EXECUTE_REDUCTION_DEC                            0x00000007
#define NVC36F_SEM_EXECUTE_REDUCTION_FORMAT                              31:31
#define NVC36F_SEM_EXECUTE_REDUCTION_FORMAT_SIGNED                  0x00000000
#define NVC36F_SEM_EXECUTE_REDUCTION_FORMAT_UNSIGNED                0x00000001

#endif
