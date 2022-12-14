/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _vcn_2_5_SH_MASK_HEADER
#define _vcn_2_5_SH_MASK_HEADER

// addressBlock: uvd0_mmsch_dec
//MMSCH_UCODE_ADDR
#define MMSCH_UCODE_ADDR__UCODE_ADDR__SHIFT                                                                   0x2
#define MMSCH_UCODE_ADDR__UCODE_LOCK__SHIFT                                                                   0x1f
#define MMSCH_UCODE_ADDR__UCODE_ADDR_MASK                                                                     0x00003FFCL
#define MMSCH_UCODE_ADDR__UCODE_LOCK_MASK                                                                     0x80000000L
//MMSCH_UCODE_DATA
#define MMSCH_UCODE_DATA__UCODE_DATA__SHIFT                                                                   0x0
#define MMSCH_UCODE_DATA__UCODE_DATA_MASK                                                                     0xFFFFFFFFL
//MMSCH_SRAM_ADDR
#define MMSCH_SRAM_ADDR__SRAM_ADDR__SHIFT                                                                     0x2
#define MMSCH_SRAM_ADDR__SRAM_LOCK__SHIFT                                                                     0x1f
#define MMSCH_SRAM_ADDR__SRAM_ADDR_MASK                                                                       0x00001FFCL
#define MMSCH_SRAM_ADDR__SRAM_LOCK_MASK                                                                       0x80000000L
//MMSCH_SRAM_DATA
#define MMSCH_SRAM_DATA__SRAM_DATA__SHIFT                                                                     0x0
#define MMSCH_SRAM_DATA__SRAM_DATA_MASK                                                                       0xFFFFFFFFL
//MMSCH_VF_SRAM_OFFSET
#define MMSCH_VF_SRAM_OFFSET__VF_SRAM_OFFSET__SHIFT                                                           0x2
#define MMSCH_VF_SRAM_OFFSET__VF_SRAM_NUM_DW_PER_VF__SHIFT                                                    0x10
#define MMSCH_VF_SRAM_OFFSET__VF_SRAM_OFFSET_MASK                                                             0x00001FFCL
#define MMSCH_VF_SRAM_OFFSET__VF_SRAM_NUM_DW_PER_VF_MASK                                                      0x00FF0000L
//MMSCH_DB_SRAM_OFFSET
#define MMSCH_DB_SRAM_OFFSET__DB_SRAM_OFFSET__SHIFT                                                           0x2
#define MMSCH_DB_SRAM_OFFSET__DB_SRAM_NUM_ENG__SHIFT                                                          0x10
#define MMSCH_DB_SRAM_OFFSET__DB_SRAM_NUM_RING_PER_ENG__SHIFT                                                 0x18
#define MMSCH_DB_SRAM_OFFSET__DB_SRAM_OFFSET_MASK                                                             0x00001FFCL
#define MMSCH_DB_SRAM_OFFSET__DB_SRAM_NUM_ENG_MASK                                                            0x00FF0000L
#define MMSCH_DB_SRAM_OFFSET__DB_SRAM_NUM_RING_PER_ENG_MASK                                                   0xFF000000L
//MMSCH_CTX_SRAM_OFFSET
#define MMSCH_CTX_SRAM_OFFSET__CTX_SRAM_OFFSET__SHIFT                                                         0x2
#define MMSCH_CTX_SRAM_OFFSET__CTX_SRAM_SIZE__SHIFT                                                           0x10
#define MMSCH_CTX_SRAM_OFFSET__CTX_SRAM_OFFSET_MASK                                                           0x00001FFCL
#define MMSCH_CTX_SRAM_OFFSET__CTX_SRAM_SIZE_MASK                                                             0xFFFF0000L
//MMSCH_CTL
#define MMSCH_CTL__P_RUNSTALL__SHIFT                                                                          0x0
#define MMSCH_CTL__P_RESET__SHIFT                                                                             0x1
#define MMSCH_CTL__VFID_FIFO_EN__SHIFT                                                                        0x4
#define MMSCH_CTL__P_LOCK__SHIFT                                                                              0x1f
#define MMSCH_CTL__P_RUNSTALL_MASK                                                                            0x00000001L
#define MMSCH_CTL__P_RESET_MASK                                                                               0x00000002L
#define MMSCH_CTL__VFID_FIFO_EN_MASK                                                                          0x00000010L
#define MMSCH_CTL__P_LOCK_MASK                                                                                0x80000000L
//MMSCH_INTR
#define MMSCH_INTR__INTR__SHIFT                                                                               0x0
#define MMSCH_INTR__INTR_MASK                                                                                 0x00001FFFL
//MMSCH_INTR_ACK
#define MMSCH_INTR_ACK__INTR__SHIFT                                                                           0x0
#define MMSCH_INTR_ACK__INTR_MASK                                                                             0x00001FFFL
//MMSCH_INTR_STATUS
#define MMSCH_INTR_STATUS__INTR__SHIFT                                                                        0x0
#define MMSCH_INTR_STATUS__INTR_MASK                                                                          0x00001FFFL
//MMSCH_VF_VMID
#define MMSCH_VF_VMID__VF_CTX_VMID__SHIFT                                                                     0x0
#define MMSCH_VF_VMID__VF_GPCOM_VMID__SHIFT                                                                   0x5
#define MMSCH_VF_VMID__VF_CTX_VMID_MASK                                                                       0x0000001FL
#define MMSCH_VF_VMID__VF_GPCOM_VMID_MASK                                                                     0x000003E0L
//MMSCH_VF_CTX_ADDR_LO
#define MMSCH_VF_CTX_ADDR_LO__VF_CTX_ADDR_LO__SHIFT                                                           0x6
#define MMSCH_VF_CTX_ADDR_LO__VF_CTX_ADDR_LO_MASK                                                             0xFFFFFFC0L
//MMSCH_VF_CTX_ADDR_HI
#define MMSCH_VF_CTX_ADDR_HI__VF_CTX_ADDR_HI__SHIFT                                                           0x0
#define MMSCH_VF_CTX_ADDR_HI__VF_CTX_ADDR_HI_MASK                                                             0xFFFFFFFFL
//MMSCH_VF_CTX_SIZE
#define MMSCH_VF_CTX_SIZE__VF_CTX_SIZE__SHIFT                                                                 0x0
#define MMSCH_VF_CTX_SIZE__VF_CTX_SIZE_MASK                                                                   0xFFFFFFFFL
//MMSCH_VF_GPCOM_ADDR_LO
#define MMSCH_VF_GPCOM_ADDR_LO__VF_GPCOM_ADDR_LO__SHIFT                                                       0x6
#define MMSCH_VF_GPCOM_ADDR_LO__VF_GPCOM_ADDR_LO_MASK                                                         0xFFFFFFC0L
//MMSCH_VF_GPCOM_ADDR_HI
#define MMSCH_VF_GPCOM_ADDR_HI__VF_GPCOM_ADDR_HI__SHIFT                                                       0x0
#define MMSCH_VF_GPCOM_ADDR_HI__VF_GPCOM_ADDR_HI_MASK                                                         0xFFFFFFFFL
//MMSCH_VF_GPCOM_SIZE
#define MMSCH_VF_GPCOM_SIZE__VF_GPCOM_SIZE__SHIFT                                                             0x0
#define MMSCH_VF_GPCOM_SIZE__VF_GPCOM_SIZE_MASK                                                               0xFFFFFFFFL
//MMSCH_VF_MAILBOX_HOST
#define MMSCH_VF_MAILBOX_HOST__DATA__SHIFT                                                                    0x0
#define MMSCH_VF_MAILBOX_HOST__DATA_MASK                                                                      0xFFFFFFFFL
//MMSCH_VF_MAILBOX_RESP
#define MMSCH_VF_MAILBOX_RESP__RESP__SHIFT                                                                    0x0
#define MMSCH_VF_MAILBOX_RESP__RESP_MASK                                                                      0xFFFFFFFFL
//MMSCH_VF_MAILBOX_0
#define MMSCH_VF_MAILBOX_0__DATA__SHIFT                                                                       0x0
#define MMSCH_VF_MAILBOX_0__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_VF_MAILBOX_0_RESP
#define MMSCH_VF_MAILBOX_0_RESP__RESP__SHIFT                                                                  0x0
#define MMSCH_VF_MAILBOX_0_RESP__RESP_MASK                                                                    0xFFFFFFFFL
//MMSCH_VF_MAILBOX_1
#define MMSCH_VF_MAILBOX_1__DATA__SHIFT                                                                       0x0
#define MMSCH_VF_MAILBOX_1__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_VF_MAILBOX_1_RESP
#define MMSCH_VF_MAILBOX_1_RESP__RESP__SHIFT                                                                  0x0
#define MMSCH_VF_MAILBOX_1_RESP__RESP_MASK                                                                    0xFFFFFFFFL
//MMSCH_CNTL
#define MMSCH_CNTL__CLK_EN__SHIFT                                                                             0x0
#define MMSCH_CNTL__ED_ENABLE__SHIFT                                                                          0x1
#define MMSCH_CNTL__MMSCH_IRQ_ERR__SHIFT                                                                      0x5
#define MMSCH_CNTL__MMSCH_NACK_INTR_EN__SHIFT                                                                 0x9
#define MMSCH_CNTL__MMSCH_DB_BUSY_INTR_EN__SHIFT                                                              0xa
#define MMSCH_CNTL__PRB_TIMEOUT_VAL__SHIFT                                                                    0x14
#define MMSCH_CNTL__TIMEOUT_DIS__SHIFT                                                                        0x1c
#define MMSCH_CNTL__CLK_EN_MASK                                                                               0x00000001L
#define MMSCH_CNTL__ED_ENABLE_MASK                                                                            0x00000002L
#define MMSCH_CNTL__MMSCH_IRQ_ERR_MASK                                                                        0x000001E0L
#define MMSCH_CNTL__MMSCH_NACK_INTR_EN_MASK                                                                   0x00000200L
#define MMSCH_CNTL__MMSCH_DB_BUSY_INTR_EN_MASK                                                                0x00000400L
#define MMSCH_CNTL__PRB_TIMEOUT_VAL_MASK                                                                      0x0FF00000L
#define MMSCH_CNTL__TIMEOUT_DIS_MASK                                                                          0x10000000L
//MMSCH_NONCACHE_OFFSET0
#define MMSCH_NONCACHE_OFFSET0__OFFSET__SHIFT                                                                 0x0
#define MMSCH_NONCACHE_OFFSET0__OFFSET_MASK                                                                   0x0FFFFFFFL
//MMSCH_NONCACHE_SIZE0
#define MMSCH_NONCACHE_SIZE0__SIZE__SHIFT                                                                     0x0
#define MMSCH_NONCACHE_SIZE0__SIZE_MASK                                                                       0x00FFFFFFL
//MMSCH_NONCACHE_OFFSET1
#define MMSCH_NONCACHE_OFFSET1__OFFSET__SHIFT                                                                 0x0
#define MMSCH_NONCACHE_OFFSET1__OFFSET_MASK                                                                   0x0FFFFFFFL
//MMSCH_NONCACHE_SIZE1
#define MMSCH_NONCACHE_SIZE1__SIZE__SHIFT                                                                     0x0
#define MMSCH_NONCACHE_SIZE1__SIZE_MASK                                                                       0x00FFFFFFL
//MMSCH_PROC_STATE1
#define MMSCH_PROC_STATE1__PC__SHIFT                                                                          0x0
#define MMSCH_PROC_STATE1__PC_MASK                                                                            0xFFFFFFFFL
//MMSCH_LAST_MC_ADDR
#define MMSCH_LAST_MC_ADDR__MC_ADDR__SHIFT                                                                    0x0
#define MMSCH_LAST_MC_ADDR__RW__SHIFT                                                                         0x1f
#define MMSCH_LAST_MC_ADDR__MC_ADDR_MASK                                                                      0x0FFFFFFFL
#define MMSCH_LAST_MC_ADDR__RW_MASK                                                                           0x80000000L
//MMSCH_LAST_MEM_ACCESS_HI
#define MMSCH_LAST_MEM_ACCESS_HI__PROC_CMD__SHIFT                                                             0x0
#define MMSCH_LAST_MEM_ACCESS_HI__FIFO_RPTR__SHIFT                                                            0x8
#define MMSCH_LAST_MEM_ACCESS_HI__FIFO_WPTR__SHIFT                                                            0xc
#define MMSCH_LAST_MEM_ACCESS_HI__PROC_CMD_MASK                                                               0x00000007L
#define MMSCH_LAST_MEM_ACCESS_HI__FIFO_RPTR_MASK                                                              0x00000700L
#define MMSCH_LAST_MEM_ACCESS_HI__FIFO_WPTR_MASK                                                              0x00007000L
//MMSCH_LAST_MEM_ACCESS_LO
#define MMSCH_LAST_MEM_ACCESS_LO__PROC_ADDR__SHIFT                                                            0x0
#define MMSCH_LAST_MEM_ACCESS_LO__PROC_ADDR_MASK                                                              0xFFFFFFFFL
//MMSCH_IOV_ACTIVE_FCN_ID
#define MMSCH_IOV_ACTIVE_FCN_ID__ACTIVE_VF_ID__SHIFT                                                          0x0
#define MMSCH_IOV_ACTIVE_FCN_ID__ACTIVE_PF_VF__SHIFT                                                          0x1f
#define MMSCH_IOV_ACTIVE_FCN_ID__ACTIVE_VF_ID_MASK                                                            0x0000001FL
#define MMSCH_IOV_ACTIVE_FCN_ID__ACTIVE_PF_VF_MASK                                                            0x80000000L
//MMSCH_SCRATCH_0
#define MMSCH_SCRATCH_0__SCRATCH_0__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_0__SCRATCH_0_MASK                                                                       0xFFFFFFFFL
//MMSCH_SCRATCH_1
#define MMSCH_SCRATCH_1__SCRATCH_1__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_1__SCRATCH_1_MASK                                                                       0xFFFFFFFFL
//MMSCH_GPUIOV_SCH_BLOCK_0
#define MMSCH_GPUIOV_SCH_BLOCK_0__ID__SHIFT                                                                   0x0
#define MMSCH_GPUIOV_SCH_BLOCK_0__VERSION__SHIFT                                                              0x4
#define MMSCH_GPUIOV_SCH_BLOCK_0__SIZE__SHIFT                                                                 0x8
#define MMSCH_GPUIOV_SCH_BLOCK_0__ID_MASK                                                                     0x0000000FL
#define MMSCH_GPUIOV_SCH_BLOCK_0__VERSION_MASK                                                                0x000000F0L
#define MMSCH_GPUIOV_SCH_BLOCK_0__SIZE_MASK                                                                   0x0000FF00L
//MMSCH_GPUIOV_CMD_CONTROL_0
#define MMSCH_GPUIOV_CMD_CONTROL_0__CMD_TYPE__SHIFT                                                           0x0
#define MMSCH_GPUIOV_CMD_CONTROL_0__CMD_EXECUTE__SHIFT                                                        0x4
#define MMSCH_GPUIOV_CMD_CONTROL_0__CMD_EXECUTE_INTR_EN__SHIFT                                                0x5
#define MMSCH_GPUIOV_CMD_CONTROL_0__VM_BUSY_INTR_EN__SHIFT                                                    0x6
#define MMSCH_GPUIOV_CMD_CONTROL_0__FUNCTINO_ID__SHIFT                                                        0x8
#define MMSCH_GPUIOV_CMD_CONTROL_0__NEXT_FUNCTINO_ID__SHIFT                                                   0x10
#define MMSCH_GPUIOV_CMD_CONTROL_0__CMD_TYPE_MASK                                                             0x0000000FL
#define MMSCH_GPUIOV_CMD_CONTROL_0__CMD_EXECUTE_MASK                                                          0x00000010L
#define MMSCH_GPUIOV_CMD_CONTROL_0__CMD_EXECUTE_INTR_EN_MASK                                                  0x00000020L
#define MMSCH_GPUIOV_CMD_CONTROL_0__VM_BUSY_INTR_EN_MASK                                                      0x00000040L
#define MMSCH_GPUIOV_CMD_CONTROL_0__FUNCTINO_ID_MASK                                                          0x0000FF00L
#define MMSCH_GPUIOV_CMD_CONTROL_0__NEXT_FUNCTINO_ID_MASK                                                     0x00FF0000L
//MMSCH_GPUIOV_CMD_STATUS_0
#define MMSCH_GPUIOV_CMD_STATUS_0__CMD_STATUS__SHIFT                                                          0x0
#define MMSCH_GPUIOV_CMD_STATUS_0__CMD_STATUS_MASK                                                            0x0000000FL
//MMSCH_GPUIOV_VM_BUSY_STATUS_0
#define MMSCH_GPUIOV_VM_BUSY_STATUS_0__BUSY__SHIFT                                                            0x0
#define MMSCH_GPUIOV_VM_BUSY_STATUS_0__BUSY_MASK                                                              0xFFFFFFFFL
//MMSCH_GPUIOV_ACTIVE_FCNS_0
#define MMSCH_GPUIOV_ACTIVE_FCNS_0__ACTIVE_FCNS__SHIFT                                                        0x0
#define MMSCH_GPUIOV_ACTIVE_FCNS_0__ACTIVE_FCNS_MASK                                                          0xFFFFFFFFL
//MMSCH_GPUIOV_ACTIVE_FCN_ID_0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_0__ID__SHIFT                                                               0x0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_0__ID_STATUS__SHIFT                                                        0x8
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_0__ID_MASK                                                                 0x000000FFL
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_0__ID_STATUS_MASK                                                          0x00000F00L
//MMSCH_GPUIOV_DW6_0
#define MMSCH_GPUIOV_DW6_0__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW6_0__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_DW7_0
#define MMSCH_GPUIOV_DW7_0__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW7_0__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_DW8_0
#define MMSCH_GPUIOV_DW8_0__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW8_0__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_SCH_BLOCK_1
#define MMSCH_GPUIOV_SCH_BLOCK_1__ID__SHIFT                                                                   0x0
#define MMSCH_GPUIOV_SCH_BLOCK_1__VERSION__SHIFT                                                              0x4
#define MMSCH_GPUIOV_SCH_BLOCK_1__SIZE__SHIFT                                                                 0x8
#define MMSCH_GPUIOV_SCH_BLOCK_1__ID_MASK                                                                     0x0000000FL
#define MMSCH_GPUIOV_SCH_BLOCK_1__VERSION_MASK                                                                0x000000F0L
#define MMSCH_GPUIOV_SCH_BLOCK_1__SIZE_MASK                                                                   0x0000FF00L
//MMSCH_GPUIOV_CMD_CONTROL_1
#define MMSCH_GPUIOV_CMD_CONTROL_1__CMD_TYPE__SHIFT                                                           0x0
#define MMSCH_GPUIOV_CMD_CONTROL_1__CMD_EXECUTE__SHIFT                                                        0x4
#define MMSCH_GPUIOV_CMD_CONTROL_1__CMD_EXECUTE_INTR_EN__SHIFT                                                0x5
#define MMSCH_GPUIOV_CMD_CONTROL_1__VM_BUSY_INTR_EN__SHIFT                                                    0x6
#define MMSCH_GPUIOV_CMD_CONTROL_1__FUNCTINO_ID__SHIFT                                                        0x8
#define MMSCH_GPUIOV_CMD_CONTROL_1__NEXT_FUNCTINO_ID__SHIFT                                                   0x10
#define MMSCH_GPUIOV_CMD_CONTROL_1__CMD_TYPE_MASK                                                             0x0000000FL
#define MMSCH_GPUIOV_CMD_CONTROL_1__CMD_EXECUTE_MASK                                                          0x00000010L
#define MMSCH_GPUIOV_CMD_CONTROL_1__CMD_EXECUTE_INTR_EN_MASK                                                  0x00000020L
#define MMSCH_GPUIOV_CMD_CONTROL_1__VM_BUSY_INTR_EN_MASK                                                      0x00000040L
#define MMSCH_GPUIOV_CMD_CONTROL_1__FUNCTINO_ID_MASK                                                          0x0000FF00L
#define MMSCH_GPUIOV_CMD_CONTROL_1__NEXT_FUNCTINO_ID_MASK                                                     0x00FF0000L
//MMSCH_GPUIOV_CMD_STATUS_1
#define MMSCH_GPUIOV_CMD_STATUS_1__CMD_STATUS__SHIFT                                                          0x0
#define MMSCH_GPUIOV_CMD_STATUS_1__CMD_STATUS_MASK                                                            0x0000000FL
//MMSCH_GPUIOV_VM_BUSY_STATUS_1
#define MMSCH_GPUIOV_VM_BUSY_STATUS_1__BUSY__SHIFT                                                            0x0
#define MMSCH_GPUIOV_VM_BUSY_STATUS_1__BUSY_MASK                                                              0xFFFFFFFFL
//MMSCH_GPUIOV_ACTIVE_FCNS_1
#define MMSCH_GPUIOV_ACTIVE_FCNS_1__ACTIVE_FCNS__SHIFT                                                        0x0
#define MMSCH_GPUIOV_ACTIVE_FCNS_1__ACTIVE_FCNS_MASK                                                          0xFFFFFFFFL
//MMSCH_GPUIOV_ACTIVE_FCN_ID_1
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_1__ID__SHIFT                                                               0x0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_1__ID_STATUS__SHIFT                                                        0x8
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_1__ID_MASK                                                                 0x000000FFL
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_1__ID_STATUS_MASK                                                          0x00000F00L
//MMSCH_GPUIOV_DW6_1
#define MMSCH_GPUIOV_DW6_1__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW6_1__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_DW7_1
#define MMSCH_GPUIOV_DW7_1__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW7_1__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_DW8_1
#define MMSCH_GPUIOV_DW8_1__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW8_1__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_CNTXT
#define MMSCH_GPUIOV_CNTXT__CNTXT_SIZE__SHIFT                                                                 0x0
#define MMSCH_GPUIOV_CNTXT__CNTXT_LOCATION__SHIFT                                                             0x7
#define MMSCH_GPUIOV_CNTXT__CNTXT_OFFSET__SHIFT                                                               0xa
#define MMSCH_GPUIOV_CNTXT__CNTXT_SIZE_MASK                                                                   0x0000007FL
#define MMSCH_GPUIOV_CNTXT__CNTXT_LOCATION_MASK                                                               0x00000080L
#define MMSCH_GPUIOV_CNTXT__CNTXT_OFFSET_MASK                                                                 0xFFFFFC00L
//MMSCH_SCRATCH_2
#define MMSCH_SCRATCH_2__SCRATCH_2__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_2__SCRATCH_2_MASK                                                                       0xFFFFFFFFL
//MMSCH_SCRATCH_3
#define MMSCH_SCRATCH_3__SCRATCH_3__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_3__SCRATCH_3_MASK                                                                       0xFFFFFFFFL
//MMSCH_SCRATCH_4
#define MMSCH_SCRATCH_4__SCRATCH_4__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_4__SCRATCH_4_MASK                                                                       0xFFFFFFFFL
//MMSCH_SCRATCH_5
#define MMSCH_SCRATCH_5__SCRATCH_5__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_5__SCRATCH_5_MASK                                                                       0xFFFFFFFFL
//MMSCH_SCRATCH_6
#define MMSCH_SCRATCH_6__SCRATCH_6__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_6__SCRATCH_6_MASK                                                                       0xFFFFFFFFL
//MMSCH_SCRATCH_7
#define MMSCH_SCRATCH_7__SCRATCH_7__SHIFT                                                                     0x0
#define MMSCH_SCRATCH_7__SCRATCH_7_MASK                                                                       0xFFFFFFFFL
//MMSCH_VFID_FIFO_HEAD_0
#define MMSCH_VFID_FIFO_HEAD_0__HEAD__SHIFT                                                                   0x0
#define MMSCH_VFID_FIFO_HEAD_0__HEAD_MASK                                                                     0x0000003FL
//MMSCH_VFID_FIFO_TAIL_0
#define MMSCH_VFID_FIFO_TAIL_0__TAIL__SHIFT                                                                   0x0
#define MMSCH_VFID_FIFO_TAIL_0__TAIL_MASK                                                                     0x0000003FL
//MMSCH_VFID_FIFO_HEAD_1
#define MMSCH_VFID_FIFO_HEAD_1__HEAD__SHIFT                                                                   0x0
#define MMSCH_VFID_FIFO_HEAD_1__HEAD_MASK                                                                     0x0000003FL
//MMSCH_VFID_FIFO_TAIL_1
#define MMSCH_VFID_FIFO_TAIL_1__TAIL__SHIFT                                                                   0x0
#define MMSCH_VFID_FIFO_TAIL_1__TAIL_MASK                                                                     0x0000003FL
//MMSCH_NACK_STATUS
#define MMSCH_NACK_STATUS__WR_NACK_STATUS__SHIFT                                                              0x0
#define MMSCH_NACK_STATUS__RD_NACK_STATUS__SHIFT                                                              0x2
#define MMSCH_NACK_STATUS__WR_NACK_STATUS_MASK                                                                0x00000003L
#define MMSCH_NACK_STATUS__RD_NACK_STATUS_MASK                                                                0x0000000CL
//MMSCH_VF_MAILBOX0_DATA
#define MMSCH_VF_MAILBOX0_DATA__DATA__SHIFT                                                                   0x0
#define MMSCH_VF_MAILBOX0_DATA__DATA_MASK                                                                     0xFFFFFFFFL
//MMSCH_VF_MAILBOX1_DATA
#define MMSCH_VF_MAILBOX1_DATA__DATA__SHIFT                                                                   0x0
#define MMSCH_VF_MAILBOX1_DATA__DATA_MASK                                                                     0xFFFFFFFFL
//MMSCH_GPUIOV_SCH_BLOCK_IP_0
#define MMSCH_GPUIOV_SCH_BLOCK_IP_0__ID__SHIFT                                                                0x0
#define MMSCH_GPUIOV_SCH_BLOCK_IP_0__VERSION__SHIFT                                                           0x4
#define MMSCH_GPUIOV_SCH_BLOCK_IP_0__SIZE__SHIFT                                                              0x8
#define MMSCH_GPUIOV_SCH_BLOCK_IP_0__ID_MASK                                                                  0x0000000FL
#define MMSCH_GPUIOV_SCH_BLOCK_IP_0__VERSION_MASK                                                             0x000000F0L
#define MMSCH_GPUIOV_SCH_BLOCK_IP_0__SIZE_MASK                                                                0x0000FF00L
//MMSCH_GPUIOV_CMD_STATUS_IP_0
#define MMSCH_GPUIOV_CMD_STATUS_IP_0__CMD_STATUS__SHIFT                                                       0x0
#define MMSCH_GPUIOV_CMD_STATUS_IP_0__CMD_STATUS_MASK                                                         0x0000000FL
//MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0__ID__SHIFT                                                            0x0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0__ID_STATUS__SHIFT                                                     0x8
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0__ID_MASK                                                              0x000000FFL
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0__ID_STATUS_MASK                                                       0x00000F00L
//MMSCH_GPUIOV_SCH_BLOCK_IP_1
#define MMSCH_GPUIOV_SCH_BLOCK_IP_1__ID__SHIFT                                                                0x0
#define MMSCH_GPUIOV_SCH_BLOCK_IP_1__VERSION__SHIFT                                                           0x4
#define MMSCH_GPUIOV_SCH_BLOCK_IP_1__SIZE__SHIFT                                                              0x8
#define MMSCH_GPUIOV_SCH_BLOCK_IP_1__ID_MASK                                                                  0x0000000FL
#define MMSCH_GPUIOV_SCH_BLOCK_IP_1__VERSION_MASK                                                             0x000000F0L
#define MMSCH_GPUIOV_SCH_BLOCK_IP_1__SIZE_MASK                                                                0x0000FF00L
//MMSCH_GPUIOV_CMD_STATUS_IP_1
#define MMSCH_GPUIOV_CMD_STATUS_IP_1__CMD_STATUS__SHIFT                                                       0x0
#define MMSCH_GPUIOV_CMD_STATUS_IP_1__CMD_STATUS_MASK                                                         0x0000000FL
//MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1__ID__SHIFT                                                            0x0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1__ID_STATUS__SHIFT                                                     0x8
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1__ID_MASK                                                              0x000000FFL
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1__ID_STATUS_MASK                                                       0x00000F00L
//MMSCH_GPUIOV_CNTXT_IP
#define MMSCH_GPUIOV_CNTXT_IP__CNTXT_SIZE__SHIFT                                                              0x0
#define MMSCH_GPUIOV_CNTXT_IP__CNTXT_LOCATION__SHIFT                                                          0x7
#define MMSCH_GPUIOV_CNTXT_IP__CNTXT_SIZE_MASK                                                                0x0000007FL
#define MMSCH_GPUIOV_CNTXT_IP__CNTXT_LOCATION_MASK                                                            0x00000080L
//MMSCH_GPUIOV_SCH_BLOCK_2
#define MMSCH_GPUIOV_SCH_BLOCK_2__ID__SHIFT                                                                   0x0
#define MMSCH_GPUIOV_SCH_BLOCK_2__VERSION__SHIFT                                                              0x4
#define MMSCH_GPUIOV_SCH_BLOCK_2__SIZE__SHIFT                                                                 0x8
#define MMSCH_GPUIOV_SCH_BLOCK_2__ID_MASK                                                                     0x0000000FL
#define MMSCH_GPUIOV_SCH_BLOCK_2__VERSION_MASK                                                                0x000000F0L
#define MMSCH_GPUIOV_SCH_BLOCK_2__SIZE_MASK                                                                   0x0000FF00L
//MMSCH_GPUIOV_CMD_CONTROL_2
#define MMSCH_GPUIOV_CMD_CONTROL_2__CMD_TYPE__SHIFT                                                           0x0
#define MMSCH_GPUIOV_CMD_CONTROL_2__CMD_EXECUTE__SHIFT                                                        0x4
#define MMSCH_GPUIOV_CMD_CONTROL_2__CMD_EXECUTE_INTR_EN__SHIFT                                                0x5
#define MMSCH_GPUIOV_CMD_CONTROL_2__VM_BUSY_INTR_EN__SHIFT                                                    0x6
#define MMSCH_GPUIOV_CMD_CONTROL_2__FUNCTINO_ID__SHIFT                                                        0x8
#define MMSCH_GPUIOV_CMD_CONTROL_2__NEXT_FUNCTINO_ID__SHIFT                                                   0x10
#define MMSCH_GPUIOV_CMD_CONTROL_2__CMD_TYPE_MASK                                                             0x0000000FL
#define MMSCH_GPUIOV_CMD_CONTROL_2__CMD_EXECUTE_MASK                                                          0x00000010L
#define MMSCH_GPUIOV_CMD_CONTROL_2__CMD_EXECUTE_INTR_EN_MASK                                                  0x00000020L
#define MMSCH_GPUIOV_CMD_CONTROL_2__VM_BUSY_INTR_EN_MASK                                                      0x00000040L
#define MMSCH_GPUIOV_CMD_CONTROL_2__FUNCTINO_ID_MASK                                                          0x0000FF00L
#define MMSCH_GPUIOV_CMD_CONTROL_2__NEXT_FUNCTINO_ID_MASK                                                     0x00FF0000L
//MMSCH_GPUIOV_CMD_STATUS_2
#define MMSCH_GPUIOV_CMD_STATUS_2__CMD_STATUS__SHIFT                                                          0x0
#define MMSCH_GPUIOV_CMD_STATUS_2__CMD_STATUS_MASK                                                            0x0000000FL
//MMSCH_GPUIOV_VM_BUSY_STATUS_2
#define MMSCH_GPUIOV_VM_BUSY_STATUS_2__BUSY__SHIFT                                                            0x0
#define MMSCH_GPUIOV_VM_BUSY_STATUS_2__BUSY_MASK                                                              0xFFFFFFFFL
//MMSCH_GPUIOV_ACTIVE_FCNS_2
#define MMSCH_GPUIOV_ACTIVE_FCNS_2__ACTIVE_FCNS__SHIFT                                                        0x0
#define MMSCH_GPUIOV_ACTIVE_FCNS_2__ACTIVE_FCNS_MASK                                                          0xFFFFFFFFL
//MMSCH_GPUIOV_ACTIVE_FCN_ID_2
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_2__ID__SHIFT                                                               0x0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_2__ID_STATUS__SHIFT                                                        0x8
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_2__ID_MASK                                                                 0x000000FFL
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_2__ID_STATUS_MASK                                                          0x00000F00L
//MMSCH_GPUIOV_DW6_2
#define MMSCH_GPUIOV_DW6_2__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW6_2__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_DW7_2
#define MMSCH_GPUIOV_DW7_2__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW7_2__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_DW8_2
#define MMSCH_GPUIOV_DW8_2__DATA__SHIFT                                                                       0x0
#define MMSCH_GPUIOV_DW8_2__DATA_MASK                                                                         0xFFFFFFFFL
//MMSCH_GPUIOV_SCH_BLOCK_IP_2
#define MMSCH_GPUIOV_SCH_BLOCK_IP_2__ID__SHIFT                                                                0x0
#define MMSCH_GPUIOV_SCH_BLOCK_IP_2__VERSION__SHIFT                                                           0x4
#define MMSCH_GPUIOV_SCH_BLOCK_IP_2__SIZE__SHIFT                                                              0x8
#define MMSCH_GPUIOV_SCH_BLOCK_IP_2__ID_MASK                                                                  0x0000000FL
#define MMSCH_GPUIOV_SCH_BLOCK_IP_2__VERSION_MASK                                                             0x000000F0L
#define MMSCH_GPUIOV_SCH_BLOCK_IP_2__SIZE_MASK                                                                0x0000FF00L
//MMSCH_GPUIOV_CMD_STATUS_IP_2
#define MMSCH_GPUIOV_CMD_STATUS_IP_2__CMD_STATUS__SHIFT                                                       0x0
#define MMSCH_GPUIOV_CMD_STATUS_IP_2__CMD_STATUS_MASK                                                         0x0000000FL
//MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2__ID__SHIFT                                                            0x0
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2__ID_STATUS__SHIFT                                                     0x8
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2__ID_MASK                                                              0x000000FFL
#define MMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2__ID_STATUS_MASK                                                       0x00000F00L
//MMSCH_VFID_FIFO_HEAD_2
#define MMSCH_VFID_FIFO_HEAD_2__HEAD__SHIFT                                                                   0x0
#define MMSCH_VFID_FIFO_HEAD_2__HEAD_MASK                                                                     0x0000003FL
//MMSCH_VFID_FIFO_TAIL_2
#define MMSCH_VFID_FIFO_TAIL_2__TAIL__SHIFT                                                                   0x0
#define MMSCH_VFID_FIFO_TAIL_2__TAIL_MASK                                                                     0x0000003FL
//MMSCH_VM_BUSY_STATUS_0
#define MMSCH_VM_BUSY_STATUS_0__BUSY__SHIFT                                                                   0x0
#define MMSCH_VM_BUSY_STATUS_0__BUSY_MASK                                                                     0xFFFFFFFFL
//MMSCH_VM_BUSY_STATUS_1
#define MMSCH_VM_BUSY_STATUS_1__BUSY__SHIFT                                                                   0x0
#define MMSCH_VM_BUSY_STATUS_1__BUSY_MASK                                                                     0xFFFFFFFFL
//MMSCH_VM_BUSY_STATUS_2
#define MMSCH_VM_BUSY_STATUS_2__BUSY__SHIFT                                                                   0x0
#define MMSCH_VM_BUSY_STATUS_2__BUSY_MASK                                                                     0xFFFFFFFFL


// addressBlock: uvd0_jpegnpdec
//UVD_JPEG_CNTL
#define UVD_JPEG_CNTL__REQUEST_EN__SHIFT                                                                      0x1
#define UVD_JPEG_CNTL__ERR_RST_EN__SHIFT                                                                      0x2
#define UVD_JPEG_CNTL__HUFF_SPEED_EN__SHIFT                                                                   0x3
#define UVD_JPEG_CNTL__HUFF_SPEED_STATUS__SHIFT                                                               0x4
#define UVD_JPEG_CNTL__DBG_MUX_SEL__SHIFT                                                                     0x8
#define UVD_JPEG_CNTL__REQUEST_EN_MASK                                                                        0x00000002L
#define UVD_JPEG_CNTL__ERR_RST_EN_MASK                                                                        0x00000004L
#define UVD_JPEG_CNTL__HUFF_SPEED_EN_MASK                                                                     0x00000008L
#define UVD_JPEG_CNTL__HUFF_SPEED_STATUS_MASK                                                                 0x00000010L
#define UVD_JPEG_CNTL__DBG_MUX_SEL_MASK                                                                       0x00007F00L
//UVD_JPEG_RB_BASE
#define UVD_JPEG_RB_BASE__RB_BYTE_OFF__SHIFT                                                                  0x0
#define UVD_JPEG_RB_BASE__RB_BASE__SHIFT                                                                      0x6
#define UVD_JPEG_RB_BASE__RB_BYTE_OFF_MASK                                                                    0x0000003FL
#define UVD_JPEG_RB_BASE__RB_BASE_MASK                                                                        0xFFFFFFC0L
//UVD_JPEG_RB_WPTR
#define UVD_JPEG_RB_WPTR__RB_WPTR__SHIFT                                                                      0x4
#define UVD_JPEG_RB_WPTR__RB_WPTR_MASK                                                                        0x3FFFFFF0L
//UVD_JPEG_RB_RPTR
#define UVD_JPEG_RB_RPTR__RB_RPTR__SHIFT                                                                      0x4
#define UVD_JPEG_RB_RPTR__RB_RPTR_MASK                                                                        0x3FFFFFF0L
//UVD_JPEG_RB_SIZE
#define UVD_JPEG_RB_SIZE__RB_SIZE__SHIFT                                                                      0x4
#define UVD_JPEG_RB_SIZE__RB_SIZE_MASK                                                                        0x3FFFFFF0L
//UVD_JPEG_DEC_SCRATCH0
#define UVD_JPEG_DEC_SCRATCH0__SCRATCH0__SHIFT                                                                0x0
#define UVD_JPEG_DEC_SCRATCH0__SCRATCH0_MASK                                                                  0xFFFFFFFFL
//UVD_JPEG_INT_EN
#define UVD_JPEG_INT_EN__OUTBUF_WPTR_INC_EN__SHIFT                                                            0x0
#define UVD_JPEG_INT_EN__JOB_AVAIL_EN__SHIFT                                                                  0x1
#define UVD_JPEG_INT_EN__FENCE_VAL_EN__SHIFT                                                                  0x2
#define UVD_JPEG_INT_EN__FIFO_OVERFLOW_ERR_EN__SHIFT                                                          0x6
#define UVD_JPEG_INT_EN__BLK_CNT_OUT_OF_SYNC_ERR_EN__SHIFT                                                    0x7
#define UVD_JPEG_INT_EN__EOI_ERR_EN__SHIFT                                                                    0x8
#define UVD_JPEG_INT_EN__HFM_ERR_EN__SHIFT                                                                    0x9
#define UVD_JPEG_INT_EN__RST_ERR_EN__SHIFT                                                                    0xa
#define UVD_JPEG_INT_EN__ECS_MK_ERR_EN__SHIFT                                                                 0xb
#define UVD_JPEG_INT_EN__TIMEOUT_ERR_EN__SHIFT                                                                0xc
#define UVD_JPEG_INT_EN__MARKER_ERR_EN__SHIFT                                                                 0xd
#define UVD_JPEG_INT_EN__FMT_ERR_EN__SHIFT                                                                    0xe
#define UVD_JPEG_INT_EN__PROFILE_ERR_EN__SHIFT                                                                0xf
#define UVD_JPEG_INT_EN__OUTBUF_WPTR_INC_EN_MASK                                                              0x00000001L
#define UVD_JPEG_INT_EN__JOB_AVAIL_EN_MASK                                                                    0x00000002L
#define UVD_JPEG_INT_EN__FENCE_VAL_EN_MASK                                                                    0x00000004L
#define UVD_JPEG_INT_EN__FIFO_OVERFLOW_ERR_EN_MASK                                                            0x00000040L
#define UVD_JPEG_INT_EN__BLK_CNT_OUT_OF_SYNC_ERR_EN_MASK                                                      0x00000080L
#define UVD_JPEG_INT_EN__EOI_ERR_EN_MASK                                                                      0x00000100L
#define UVD_JPEG_INT_EN__HFM_ERR_EN_MASK                                                                      0x00000200L
#define UVD_JPEG_INT_EN__RST_ERR_EN_MASK                                                                      0x00000400L
#define UVD_JPEG_INT_EN__ECS_MK_ERR_EN_MASK                                                                   0x00000800L
#define UVD_JPEG_INT_EN__TIMEOUT_ERR_EN_MASK                                                                  0x00001000L
#define UVD_JPEG_INT_EN__MARKER_ERR_EN_MASK                                                                   0x00002000L
#define UVD_JPEG_INT_EN__FMT_ERR_EN_MASK                                                                      0x00004000L
#define UVD_JPEG_INT_EN__PROFILE_ERR_EN_MASK                                                                  0x00008000L
//UVD_JPEG_INT_STAT
#define UVD_JPEG_INT_STAT__OUTBUF_WPTR_INC_INT__SHIFT                                                         0x0
#define UVD_JPEG_INT_STAT__JOB_AVAIL_INT__SHIFT                                                               0x1
#define UVD_JPEG_INT_STAT__FENCE_VAL_INT__SHIFT                                                               0x2
#define UVD_JPEG_INT_STAT__FIFO_OVERFLOW_ERR_INT__SHIFT                                                       0x6
#define UVD_JPEG_INT_STAT__BLK_CNT_OUT_OF_SYNC_ERR_INT__SHIFT                                                 0x7
#define UVD_JPEG_INT_STAT__EOI_ERR_INT__SHIFT                                                                 0x8
#define UVD_JPEG_INT_STAT__HFM_ERR_INT__SHIFT                                                                 0x9
#define UVD_JPEG_INT_STAT__RST_ERR_INT__SHIFT                                                                 0xa
#define UVD_JPEG_INT_STAT__ECS_MK_ERR_INT__SHIFT                                                              0xb
#define UVD_JPEG_INT_STAT__TIMEOUT_ERR_INT__SHIFT                                                             0xc
#define UVD_JPEG_INT_STAT__MARKER_ERR_INT__SHIFT                                                              0xd
#define UVD_JPEG_INT_STAT__FMT_ERR_INT__SHIFT                                                                 0xe
#define UVD_JPEG_INT_STAT__PROFILE_ERR_INT__SHIFT                                                             0xf
#define UVD_JPEG_INT_STAT__OUTBUF_WPTR_INC_INT_MASK                                                           0x00000001L
#define UVD_JPEG_INT_STAT__JOB_AVAIL_INT_MASK                                                                 0x00000002L
#define UVD_JPEG_INT_STAT__FENCE_VAL_INT_MASK                                                                 0x00000004L
#define UVD_JPEG_INT_STAT__FIFO_OVERFLOW_ERR_INT_MASK                                                         0x00000040L
#define UVD_JPEG_INT_STAT__BLK_CNT_OUT_OF_SYNC_ERR_INT_MASK                                                   0x00000080L
#define UVD_JPEG_INT_STAT__EOI_ERR_INT_MASK                                                                   0x00000100L
#define UVD_JPEG_INT_STAT__HFM_ERR_INT_MASK                                                                   0x00000200L
#define UVD_JPEG_INT_STAT__RST_ERR_INT_MASK                                                                   0x00000400L
#define UVD_JPEG_INT_STAT__ECS_MK_ERR_INT_MASK                                                                0x00000800L
#define UVD_JPEG_INT_STAT__TIMEOUT_ERR_INT_MASK                                                               0x00001000L
#define UVD_JPEG_INT_STAT__MARKER_ERR_INT_MASK                                                                0x00002000L
#define UVD_JPEG_INT_STAT__FMT_ERR_INT_MASK                                                                   0x00004000L
#define UVD_JPEG_INT_STAT__PROFILE_ERR_INT_MASK                                                               0x00008000L
//UVD_JPEG_PITCH
#define UVD_JPEG_PITCH__PITCH__SHIFT                                                                          0x0
#define UVD_JPEG_PITCH__PITCH_MASK                                                                            0xFFFFFFFFL
//UVD_JPEG_UV_PITCH
#define UVD_JPEG_UV_PITCH__UV_PITCH__SHIFT                                                                    0x0
#define UVD_JPEG_UV_PITCH__UV_PITCH_MASK                                                                      0xFFFFFFFFL
//JPEG_DEC_Y_GFX8_TILING_SURFACE
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__BANK_WIDTH__SHIFT                                                     0x0
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__BANK_HEIGHT__SHIFT                                                    0x2
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__MACRO_TILE_ASPECT__SHIFT                                              0x4
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__NUM_BANKS__SHIFT                                                      0x6
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__PIPE_CONFIG__SHIFT                                                    0x8
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__TILE_SPLIT__SHIFT                                                     0xd
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__ARRAY_MODE__SHIFT                                                     0x10
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__BANK_WIDTH_MASK                                                       0x00000003L
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__BANK_HEIGHT_MASK                                                      0x0000000CL
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__MACRO_TILE_ASPECT_MASK                                                0x00000030L
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__NUM_BANKS_MASK                                                        0x000000C0L
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__PIPE_CONFIG_MASK                                                      0x00001F00L
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__TILE_SPLIT_MASK                                                       0x0000E000L
#define JPEG_DEC_Y_GFX8_TILING_SURFACE__ARRAY_MODE_MASK                                                       0x000F0000L
//JPEG_DEC_UV_GFX8_TILING_SURFACE
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__BANK_WIDTH__SHIFT                                                    0x0
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__BANK_HEIGHT__SHIFT                                                   0x2
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__MACRO_TILE_ASPECT__SHIFT                                             0x4
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__NUM_BANKS__SHIFT                                                     0x6
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__PIPE_CONFIG__SHIFT                                                   0x8
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__TILE_SPLIT__SHIFT                                                    0xd
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__ARRAY_MODE__SHIFT                                                    0x10
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__BANK_WIDTH_MASK                                                      0x00000003L
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__BANK_HEIGHT_MASK                                                     0x0000000CL
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__MACRO_TILE_ASPECT_MASK                                               0x00000030L
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__NUM_BANKS_MASK                                                       0x000000C0L
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__PIPE_CONFIG_MASK                                                     0x00001F00L
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__TILE_SPLIT_MASK                                                      0x0000E000L
#define JPEG_DEC_UV_GFX8_TILING_SURFACE__ARRAY_MODE_MASK                                                      0x000F0000L
//JPEG_DEC_GFX8_ADDR_CONFIG
#define JPEG_DEC_GFX8_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                0x4
#define JPEG_DEC_GFX8_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                  0x00000070L
//JPEG_DEC_Y_GFX10_TILING_SURFACE
#define JPEG_DEC_Y_GFX10_TILING_SURFACE__SWIZZLE_MODE__SHIFT                                                  0x0
#define JPEG_DEC_Y_GFX10_TILING_SURFACE__SWIZZLE_MODE_MASK                                                    0x0000001FL
//JPEG_DEC_UV_GFX10_TILING_SURFACE
#define JPEG_DEC_UV_GFX10_TILING_SURFACE__SWIZZLE_MODE__SHIFT                                                 0x0
#define JPEG_DEC_UV_GFX10_TILING_SURFACE__SWIZZLE_MODE_MASK                                                   0x0000001FL
//JPEG_DEC_GFX10_ADDR_CONFIG
#define JPEG_DEC_GFX10_ADDR_CONFIG__NUM_PIPES__SHIFT                                                          0x0
#define JPEG_DEC_GFX10_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                               0x3
#define JPEG_DEC_GFX10_ADDR_CONFIG__NUM_BANKS__SHIFT                                                          0xc
#define JPEG_DEC_GFX10_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                 0x13
#define JPEG_DEC_GFX10_ADDR_CONFIG__NUM_PIPES_MASK                                                            0x00000007L
#define JPEG_DEC_GFX10_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                 0x00000038L
#define JPEG_DEC_GFX10_ADDR_CONFIG__NUM_BANKS_MASK                                                            0x00007000L
#define JPEG_DEC_GFX10_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                   0x00180000L
//JPEG_DEC_ADDR_MODE
#define JPEG_DEC_ADDR_MODE__ADDR_MODE_Y__SHIFT                                                                0x0
#define JPEG_DEC_ADDR_MODE__ADDR_MODE_UV__SHIFT                                                               0x2
#define JPEG_DEC_ADDR_MODE__ADDR_LIB_SEL__SHIFT                                                               0xc
#define JPEG_DEC_ADDR_MODE__ADDR_MODE_Y_MASK                                                                  0x00000003L
#define JPEG_DEC_ADDR_MODE__ADDR_MODE_UV_MASK                                                                 0x0000000CL
#define JPEG_DEC_ADDR_MODE__ADDR_LIB_SEL_MASK                                                                 0x00007000L
//UVD_JPEG_OUTPUT_XY
//UVD_JPEG_GPCOM_CMD
#define UVD_JPEG_GPCOM_CMD__CMD__SHIFT                                                                        0x1
#define UVD_JPEG_GPCOM_CMD__CMD_MASK                                                                          0x0000000EL
//UVD_JPEG_GPCOM_DATA0
#define UVD_JPEG_GPCOM_DATA0__DATA0__SHIFT                                                                    0x0
#define UVD_JPEG_GPCOM_DATA0__DATA0_MASK                                                                      0xFFFFFFFFL
//UVD_JPEG_GPCOM_DATA1
#define UVD_JPEG_GPCOM_DATA1__DATA1__SHIFT                                                                    0x0
#define UVD_JPEG_GPCOM_DATA1__DATA1_MASK                                                                      0xFFFFFFFFL
//UVD_JPEG_SCRATCH1
#define UVD_JPEG_SCRATCH1__SCRATCH1__SHIFT                                                                    0x0
#define UVD_JPEG_SCRATCH1__SCRATCH1_MASK                                                                      0xFFFFFFFFL
//UVD_JPEG_DEC_SOFT_RST
#define UVD_JPEG_DEC_SOFT_RST__SOFT_RESET__SHIFT                                                              0x0
#define UVD_JPEG_DEC_SOFT_RST__RESET_STATUS__SHIFT                                                            0x10
#define UVD_JPEG_DEC_SOFT_RST__SOFT_RESET_MASK                                                                0x00000001L
#define UVD_JPEG_DEC_SOFT_RST__RESET_STATUS_MASK                                                              0x00010000L


// addressBlock: uvd0_uvd_jpeg_enc_dec
//UVD_JPEG_ENC_INT_EN
#define UVD_JPEG_ENC_INT_EN__HUFF_JOB_DONE_INT_EN__SHIFT                                                      0x0
#define UVD_JPEG_ENC_INT_EN__SCLR_JOB_DONE_INT_EN__SHIFT                                                      0x1
#define UVD_JPEG_ENC_INT_EN__HUFF_ERROR_INT_EN__SHIFT                                                         0x2
#define UVD_JPEG_ENC_INT_EN__SCLR_ERROR_INT_EN__SHIFT                                                         0x3
#define UVD_JPEG_ENC_INT_EN__QTBL_ERROR_INT_EN__SHIFT                                                         0x4
#define UVD_JPEG_ENC_INT_EN__PIC_SIZE_ERROR_INT_EN__SHIFT                                                     0x5
#define UVD_JPEG_ENC_INT_EN__FENCE_VAL_INT_EN__SHIFT                                                          0x6
#define UVD_JPEG_ENC_INT_EN__HUFF_JOB_DONE_INT_EN_MASK                                                        0x00000001L
#define UVD_JPEG_ENC_INT_EN__SCLR_JOB_DONE_INT_EN_MASK                                                        0x00000002L
#define UVD_JPEG_ENC_INT_EN__HUFF_ERROR_INT_EN_MASK                                                           0x00000004L
#define UVD_JPEG_ENC_INT_EN__SCLR_ERROR_INT_EN_MASK                                                           0x00000008L
#define UVD_JPEG_ENC_INT_EN__QTBL_ERROR_INT_EN_MASK                                                           0x00000010L
#define UVD_JPEG_ENC_INT_EN__PIC_SIZE_ERROR_INT_EN_MASK                                                       0x00000020L
#define UVD_JPEG_ENC_INT_EN__FENCE_VAL_INT_EN_MASK                                                            0x00000040L
//UVD_JPEG_ENC_INT_STATUS
#define UVD_JPEG_ENC_INT_STATUS__HUFF_JOB_DONE_STATUS__SHIFT                                                  0x0
#define UVD_JPEG_ENC_INT_STATUS__SCLR_JOB_DONE_STATUS__SHIFT                                                  0x1
#define UVD_JPEG_ENC_INT_STATUS__HUFF_ERROR_STATUS__SHIFT                                                     0x2
#define UVD_JPEG_ENC_INT_STATUS__SCLR_ERROR_STATUS__SHIFT                                                     0x3
#define UVD_JPEG_ENC_INT_STATUS__QTBL_ERROR_STATUS__SHIFT                                                     0x4
#define UVD_JPEG_ENC_INT_STATUS__PIC_SIZE_ERROR_STATUS__SHIFT                                                 0x5
#define UVD_JPEG_ENC_INT_STATUS__FENCE_VAL_STATUS__SHIFT                                                      0x6
#define UVD_JPEG_ENC_INT_STATUS__HUFF_JOB_DONE_STATUS_MASK                                                    0x00000001L
#define UVD_JPEG_ENC_INT_STATUS__SCLR_JOB_DONE_STATUS_MASK                                                    0x00000002L
#define UVD_JPEG_ENC_INT_STATUS__HUFF_ERROR_STATUS_MASK                                                       0x00000004L
#define UVD_JPEG_ENC_INT_STATUS__SCLR_ERROR_STATUS_MASK                                                       0x00000008L
#define UVD_JPEG_ENC_INT_STATUS__QTBL_ERROR_STATUS_MASK                                                       0x00000010L
#define UVD_JPEG_ENC_INT_STATUS__PIC_SIZE_ERROR_STATUS_MASK                                                   0x00000020L
#define UVD_JPEG_ENC_INT_STATUS__FENCE_VAL_STATUS_MASK                                                        0x00000040L
//UVD_JPEG_ENC_ENGINE_CNTL
#define UVD_JPEG_ENC_ENGINE_CNTL__HUFF_WR_COMB_DIS__SHIFT                                                     0x0
#define UVD_JPEG_ENC_ENGINE_CNTL__DISTINCT_CHROMA_QUANT_TABLES__SHIFT                                         0x1
#define UVD_JPEG_ENC_ENGINE_CNTL__SCALAR_EN__SHIFT                                                            0x2
#define UVD_JPEG_ENC_ENGINE_CNTL__ENCODE_EN__SHIFT                                                            0x3
#define UVD_JPEG_ENC_ENGINE_CNTL__CMP_NEEDED__SHIFT                                                           0x4
#define UVD_JPEG_ENC_ENGINE_CNTL__ECS_RESTRICT_32B_EN__SHIFT                                                  0x9
#define UVD_JPEG_ENC_ENGINE_CNTL__HUFF_WR_COMB_DIS_MASK                                                       0x00000001L
#define UVD_JPEG_ENC_ENGINE_CNTL__DISTINCT_CHROMA_QUANT_TABLES_MASK                                           0x00000002L
#define UVD_JPEG_ENC_ENGINE_CNTL__SCALAR_EN_MASK                                                              0x00000004L
#define UVD_JPEG_ENC_ENGINE_CNTL__ENCODE_EN_MASK                                                              0x00000008L
#define UVD_JPEG_ENC_ENGINE_CNTL__CMP_NEEDED_MASK                                                             0x00000010L
#define UVD_JPEG_ENC_ENGINE_CNTL__ECS_RESTRICT_32B_EN_MASK                                                    0x00000200L
//UVD_JPEG_ENC_SCRATCH1
#define UVD_JPEG_ENC_SCRATCH1__SCRATCH1__SHIFT                                                                0x0
#define UVD_JPEG_ENC_SCRATCH1__SCRATCH1_MASK                                                                  0xFFFFFFFFL


// addressBlock: uvd0_uvd_jpeg_enc_sclk_dec
//UVD_JPEG_ENC_STATUS
#define UVD_JPEG_ENC_STATUS__PEL_FETCH_IDLE__SHIFT                                                            0x0
#define UVD_JPEG_ENC_STATUS__HUFF_CORE_IDLE__SHIFT                                                            0x1
#define UVD_JPEG_ENC_STATUS__FDCT_IDLE__SHIFT                                                                 0x2
#define UVD_JPEG_ENC_STATUS__SCALAR_IDLE__SHIFT                                                               0x3
#define UVD_JPEG_ENC_STATUS__PEL_FETCH_IDLE_MASK                                                              0x00000001L
#define UVD_JPEG_ENC_STATUS__HUFF_CORE_IDLE_MASK                                                              0x00000002L
#define UVD_JPEG_ENC_STATUS__FDCT_IDLE_MASK                                                                   0x00000004L
#define UVD_JPEG_ENC_STATUS__SCALAR_IDLE_MASK                                                                 0x00000008L
//UVD_JPEG_ENC_PITCH
#define UVD_JPEG_ENC_PITCH__PITCH_Y__SHIFT                                                                    0x0
#define UVD_JPEG_ENC_PITCH__PITCH_UV__SHIFT                                                                   0x10
#define UVD_JPEG_ENC_PITCH__PITCH_Y_MASK                                                                      0x00000FFFL
#define UVD_JPEG_ENC_PITCH__PITCH_UV_MASK                                                                     0x0FFF0000L
//UVD_JPEG_ENC_LUMA_BASE
#define UVD_JPEG_ENC_LUMA_BASE__LUMA_BASE__SHIFT                                                              0x0
#define UVD_JPEG_ENC_LUMA_BASE__LUMA_BASE_MASK                                                                0xFFFFFFFFL
//UVD_JPEG_ENC_CHROMAU_BASE
#define UVD_JPEG_ENC_CHROMAU_BASE__CHROMAU_BASE__SHIFT                                                        0x0
#define UVD_JPEG_ENC_CHROMAU_BASE__CHROMAU_BASE_MASK                                                          0xFFFFFFFFL
//UVD_JPEG_ENC_CHROMAV_BASE
#define UVD_JPEG_ENC_CHROMAV_BASE__CHROMAV_BASE__SHIFT                                                        0x0
#define UVD_JPEG_ENC_CHROMAV_BASE__CHROMAV_BASE_MASK                                                          0xFFFFFFFFL
//JPEG_ENC_Y_GFX10_TILING_SURFACE
#define JPEG_ENC_Y_GFX10_TILING_SURFACE__SWIZZLE_MODE__SHIFT                                                  0x0
#define JPEG_ENC_Y_GFX10_TILING_SURFACE__SWIZZLE_MODE_MASK                                                    0x0000001FL
//JPEG_ENC_UV_GFX10_TILING_SURFACE
#define JPEG_ENC_UV_GFX10_TILING_SURFACE__SWIZZLE_MODE__SHIFT                                                 0x0
#define JPEG_ENC_UV_GFX10_TILING_SURFACE__SWIZZLE_MODE_MASK                                                   0x0000001FL
//JPEG_ENC_GFX10_ADDR_CONFIG
#define JPEG_ENC_GFX10_ADDR_CONFIG__NUM_PIPES__SHIFT                                                          0x0
#define JPEG_ENC_GFX10_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                               0x3
#define JPEG_ENC_GFX10_ADDR_CONFIG__NUM_BANKS__SHIFT                                                          0xc
#define JPEG_ENC_GFX10_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                 0x13
#define JPEG_ENC_GFX10_ADDR_CONFIG__NUM_PIPES_MASK                                                            0x00000007L
#define JPEG_ENC_GFX10_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                 0x00000038L
#define JPEG_ENC_GFX10_ADDR_CONFIG__NUM_BANKS_MASK                                                            0x00007000L
#define JPEG_ENC_GFX10_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                   0x00180000L
//JPEG_ENC_ADDR_MODE
#define JPEG_ENC_ADDR_MODE__ADDR_MODE_Y__SHIFT                                                                0x0
#define JPEG_ENC_ADDR_MODE__ADDR_MODE_UV__SHIFT                                                               0x2
#define JPEG_ENC_ADDR_MODE__ADDR_LIB_SEL__SHIFT                                                               0xc
#define JPEG_ENC_ADDR_MODE__ADDR_MODE_Y_MASK                                                                  0x00000003L
#define JPEG_ENC_ADDR_MODE__ADDR_MODE_UV_MASK                                                                 0x0000000CL
#define JPEG_ENC_ADDR_MODE__ADDR_LIB_SEL_MASK                                                                 0x00007000L
//UVD_JPEG_ENC_GPCOM_CMD
#define UVD_JPEG_ENC_GPCOM_CMD__CMD__SHIFT                                                                    0x1
#define UVD_JPEG_ENC_GPCOM_CMD__CMD_MASK                                                                      0x0000000EL
//UVD_JPEG_ENC_GPCOM_DATA0
#define UVD_JPEG_ENC_GPCOM_DATA0__DATA0__SHIFT                                                                0x0
#define UVD_JPEG_ENC_GPCOM_DATA0__DATA0_MASK                                                                  0xFFFFFFFFL
//UVD_JPEG_ENC_GPCOM_DATA1
#define UVD_JPEG_ENC_GPCOM_DATA1__DATA1__SHIFT                                                                0x0
#define UVD_JPEG_ENC_GPCOM_DATA1__DATA1_MASK                                                                  0xFFFFFFFFL
//UVD_JPEG_ENC_CGC_CNTL
#define UVD_JPEG_ENC_CGC_CNTL__CGC_EN__SHIFT                                                                  0x0
#define UVD_JPEG_ENC_CGC_CNTL__CGC_EN_MASK                                                                    0x00000001L
//UVD_JPEG_ENC_SCRATCH0
#define UVD_JPEG_ENC_SCRATCH0__SCRATCH0__SHIFT                                                                0x0
#define UVD_JPEG_ENC_SCRATCH0__SCRATCH0_MASK                                                                  0xFFFFFFFFL
//UVD_JPEG_ENC_SOFT_RST
#define UVD_JPEG_ENC_SOFT_RST__SOFT_RST__SHIFT                                                                0x0
#define UVD_JPEG_ENC_SOFT_RST__RESET_STATUS__SHIFT                                                            0x10
#define UVD_JPEG_ENC_SOFT_RST__SOFT_RST_MASK                                                                  0x00000001L
#define UVD_JPEG_ENC_SOFT_RST__RESET_STATUS_MASK                                                              0x00010000L


// addressBlock: uvd0_uvd_jrbc_dec
//UVD_JRBC_RB_WPTR
#define UVD_JRBC_RB_WPTR__RB_WPTR__SHIFT                                                                      0x4
#define UVD_JRBC_RB_WPTR__RB_WPTR_MASK                                                                        0x007FFFF0L
//UVD_JRBC_RB_CNTL
#define UVD_JRBC_RB_CNTL__RB_NO_FETCH__SHIFT                                                                  0x0
#define UVD_JRBC_RB_CNTL__RB_RPTR_WR_EN__SHIFT                                                                0x1
#define UVD_JRBC_RB_CNTL__RB_PRE_WRITE_TIMER__SHIFT                                                           0x4
#define UVD_JRBC_RB_CNTL__RB_NO_FETCH_MASK                                                                    0x00000001L
#define UVD_JRBC_RB_CNTL__RB_RPTR_WR_EN_MASK                                                                  0x00000002L
#define UVD_JRBC_RB_CNTL__RB_PRE_WRITE_TIMER_MASK                                                             0x0007FFF0L
//UVD_JRBC_IB_SIZE
#define UVD_JRBC_IB_SIZE__IB_SIZE__SHIFT                                                                      0x4
#define UVD_JRBC_IB_SIZE__IB_SIZE_MASK                                                                        0x007FFFF0L
//UVD_JRBC_URGENT_CNTL
#define UVD_JRBC_URGENT_CNTL__CMD_READ_REQ_PRIORITY_MARK__SHIFT                                               0x0
#define UVD_JRBC_URGENT_CNTL__CMD_READ_REQ_PRIORITY_MARK_MASK                                                 0x00000003L
//UVD_JRBC_RB_REF_DATA
#define UVD_JRBC_RB_REF_DATA__REF_DATA__SHIFT                                                                 0x0
#define UVD_JRBC_RB_REF_DATA__REF_DATA_MASK                                                                   0xFFFFFFFFL
//UVD_JRBC_RB_COND_RD_TIMER
#define UVD_JRBC_RB_COND_RD_TIMER__RETRY_TIMER_CNT__SHIFT                                                     0x0
#define UVD_JRBC_RB_COND_RD_TIMER__RETRY_INTERVAL_CNT__SHIFT                                                  0x10
#define UVD_JRBC_RB_COND_RD_TIMER__CONTINUOUS_POLL_EN__SHIFT                                                  0x18
#define UVD_JRBC_RB_COND_RD_TIMER__MEM_TIMEOUT_EN__SHIFT                                                      0x19
#define UVD_JRBC_RB_COND_RD_TIMER__RETRY_TIMER_CNT_MASK                                                       0x0000FFFFL
#define UVD_JRBC_RB_COND_RD_TIMER__RETRY_INTERVAL_CNT_MASK                                                    0x00FF0000L
#define UVD_JRBC_RB_COND_RD_TIMER__CONTINUOUS_POLL_EN_MASK                                                    0x01000000L
#define UVD_JRBC_RB_COND_RD_TIMER__MEM_TIMEOUT_EN_MASK                                                        0x02000000L
//UVD_JRBC_SOFT_RESET
#define UVD_JRBC_SOFT_RESET__RESET__SHIFT                                                                     0x0
#define UVD_JRBC_SOFT_RESET__SCLK_RESET_STATUS__SHIFT                                                         0x11
#define UVD_JRBC_SOFT_RESET__RESET_MASK                                                                       0x00000001L
#define UVD_JRBC_SOFT_RESET__SCLK_RESET_STATUS_MASK                                                           0x00020000L
//UVD_JRBC_STATUS
#define UVD_JRBC_STATUS__RB_JOB_DONE__SHIFT                                                                   0x0
#define UVD_JRBC_STATUS__IB_JOB_DONE__SHIFT                                                                   0x1
#define UVD_JRBC_STATUS__RB_ILLEGAL_CMD__SHIFT                                                                0x2
#define UVD_JRBC_STATUS__RB_COND_REG_RD_TIMEOUT__SHIFT                                                        0x3
#define UVD_JRBC_STATUS__RB_MEM_WR_TIMEOUT__SHIFT                                                             0x4
#define UVD_JRBC_STATUS__RB_MEM_RD_TIMEOUT__SHIFT                                                             0x5
#define UVD_JRBC_STATUS__IB_ILLEGAL_CMD__SHIFT                                                                0x6
#define UVD_JRBC_STATUS__IB_COND_REG_RD_TIMEOUT__SHIFT                                                        0x7
#define UVD_JRBC_STATUS__IB_MEM_WR_TIMEOUT__SHIFT                                                             0x8
#define UVD_JRBC_STATUS__IB_MEM_RD_TIMEOUT__SHIFT                                                             0x9
#define UVD_JRBC_STATUS__RB_TRAP_STATUS__SHIFT                                                                0xa
#define UVD_JRBC_STATUS__PREEMPT_STATUS__SHIFT                                                                0xb
#define UVD_JRBC_STATUS__IB_TRAP_STATUS__SHIFT                                                                0xc
#define UVD_JRBC_STATUS__INT_EN__SHIFT                                                                        0x10
#define UVD_JRBC_STATUS__INT_ACK__SHIFT                                                                       0x11
#define UVD_JRBC_STATUS__RB_JOB_DONE_MASK                                                                     0x00000001L
#define UVD_JRBC_STATUS__IB_JOB_DONE_MASK                                                                     0x00000002L
#define UVD_JRBC_STATUS__RB_ILLEGAL_CMD_MASK                                                                  0x00000004L
#define UVD_JRBC_STATUS__RB_COND_REG_RD_TIMEOUT_MASK                                                          0x00000008L
#define UVD_JRBC_STATUS__RB_MEM_WR_TIMEOUT_MASK                                                               0x00000010L
#define UVD_JRBC_STATUS__RB_MEM_RD_TIMEOUT_MASK                                                               0x00000020L
#define UVD_JRBC_STATUS__IB_ILLEGAL_CMD_MASK                                                                  0x00000040L
#define UVD_JRBC_STATUS__IB_COND_REG_RD_TIMEOUT_MASK                                                          0x00000080L
#define UVD_JRBC_STATUS__IB_MEM_WR_TIMEOUT_MASK                                                               0x00000100L
#define UVD_JRBC_STATUS__IB_MEM_RD_TIMEOUT_MASK                                                               0x00000200L
#define UVD_JRBC_STATUS__RB_TRAP_STATUS_MASK                                                                  0x00000400L
#define UVD_JRBC_STATUS__PREEMPT_STATUS_MASK                                                                  0x00000800L
#define UVD_JRBC_STATUS__IB_TRAP_STATUS_MASK                                                                  0x00001000L
#define UVD_JRBC_STATUS__INT_EN_MASK                                                                          0x00010000L
#define UVD_JRBC_STATUS__INT_ACK_MASK                                                                         0x00020000L
//UVD_JRBC_RB_RPTR
#define UVD_JRBC_RB_RPTR__RB_RPTR__SHIFT                                                                      0x4
#define UVD_JRBC_RB_RPTR__RB_RPTR_MASK                                                                        0x007FFFF0L
//UVD_JRBC_RB_BUF_STATUS
#define UVD_JRBC_RB_BUF_STATUS__RB_BUF_VALID__SHIFT                                                           0x0
#define UVD_JRBC_RB_BUF_STATUS__RB_BUF_RD_ADDR__SHIFT                                                         0x10
#define UVD_JRBC_RB_BUF_STATUS__RB_BUF_WR_ADDR__SHIFT                                                         0x18
#define UVD_JRBC_RB_BUF_STATUS__RB_BUF_VALID_MASK                                                             0x0000FFFFL
#define UVD_JRBC_RB_BUF_STATUS__RB_BUF_RD_ADDR_MASK                                                           0x000F0000L
#define UVD_JRBC_RB_BUF_STATUS__RB_BUF_WR_ADDR_MASK                                                           0x03000000L
//UVD_JRBC_IB_BUF_STATUS
#define UVD_JRBC_IB_BUF_STATUS__IB_BUF_VALID__SHIFT                                                           0x0
#define UVD_JRBC_IB_BUF_STATUS__IB_BUF_RD_ADDR__SHIFT                                                         0x10
#define UVD_JRBC_IB_BUF_STATUS__IB_BUF_WR_ADDR__SHIFT                                                         0x18
#define UVD_JRBC_IB_BUF_STATUS__IB_BUF_VALID_MASK                                                             0x0000FFFFL
#define UVD_JRBC_IB_BUF_STATUS__IB_BUF_RD_ADDR_MASK                                                           0x000F0000L
#define UVD_JRBC_IB_BUF_STATUS__IB_BUF_WR_ADDR_MASK                                                           0x03000000L
//UVD_JRBC_IB_SIZE_UPDATE
#define UVD_JRBC_IB_SIZE_UPDATE__REMAIN_IB_SIZE__SHIFT                                                        0x4
#define UVD_JRBC_IB_SIZE_UPDATE__REMAIN_IB_SIZE_MASK                                                          0x007FFFF0L
//UVD_JRBC_IB_COND_RD_TIMER
#define UVD_JRBC_IB_COND_RD_TIMER__RETRY_TIMER_CNT__SHIFT                                                     0x0
#define UVD_JRBC_IB_COND_RD_TIMER__RETRY_INTERVAL_CNT__SHIFT                                                  0x10
#define UVD_JRBC_IB_COND_RD_TIMER__CONTINUOUS_POLL_EN__SHIFT                                                  0x18
#define UVD_JRBC_IB_COND_RD_TIMER__MEM_TIMEOUT_EN__SHIFT                                                      0x19
#define UVD_JRBC_IB_COND_RD_TIMER__RETRY_TIMER_CNT_MASK                                                       0x0000FFFFL
#define UVD_JRBC_IB_COND_RD_TIMER__RETRY_INTERVAL_CNT_MASK                                                    0x00FF0000L
#define UVD_JRBC_IB_COND_RD_TIMER__CONTINUOUS_POLL_EN_MASK                                                    0x01000000L
#define UVD_JRBC_IB_COND_RD_TIMER__MEM_TIMEOUT_EN_MASK                                                        0x02000000L
//UVD_JRBC_IB_REF_DATA
#define UVD_JRBC_IB_REF_DATA__REF_DATA__SHIFT                                                                 0x0
#define UVD_JRBC_IB_REF_DATA__REF_DATA_MASK                                                                   0xFFFFFFFFL
//UVD_JPEG_PREEMPT_CMD
#define UVD_JPEG_PREEMPT_CMD__PREEMPT_EN__SHIFT                                                               0x0
#define UVD_JPEG_PREEMPT_CMD__WAIT_JPEG_JOB_DONE__SHIFT                                                       0x1
#define UVD_JPEG_PREEMPT_CMD__PREEMPT_FENCE_CMD__SHIFT                                                        0x2
#define UVD_JPEG_PREEMPT_CMD__PREEMPT_EN_MASK                                                                 0x00000001L
#define UVD_JPEG_PREEMPT_CMD__WAIT_JPEG_JOB_DONE_MASK                                                         0x00000002L
#define UVD_JPEG_PREEMPT_CMD__PREEMPT_FENCE_CMD_MASK                                                          0x00000004L
//UVD_JPEG_PREEMPT_FENCE_DATA0
#define UVD_JPEG_PREEMPT_FENCE_DATA0__PREEMPT_FENCE_DATA0__SHIFT                                              0x0
#define UVD_JPEG_PREEMPT_FENCE_DATA0__PREEMPT_FENCE_DATA0_MASK                                                0xFFFFFFFFL
//UVD_JPEG_PREEMPT_FENCE_DATA1
#define UVD_JPEG_PREEMPT_FENCE_DATA1__PREEMPT_FENCE_DATA1__SHIFT                                              0x0
#define UVD_JPEG_PREEMPT_FENCE_DATA1__PREEMPT_FENCE_DATA1_MASK                                                0xFFFFFFFFL
//UVD_JRBC_RB_SIZE
#define UVD_JRBC_RB_SIZE__RB_SIZE__SHIFT                                                                      0x4
#define UVD_JRBC_RB_SIZE__RB_SIZE_MASK                                                                        0x00FFFFF0L
//UVD_JRBC_SCRATCH0
#define UVD_JRBC_SCRATCH0__SCRATCH0__SHIFT                                                                    0x0
#define UVD_JRBC_SCRATCH0__SCRATCH0_MASK                                                                      0xFFFFFFFFL


// addressBlock: uvd0_uvd_jrbc_enc_dec
//UVD_JRBC_ENC_RB_WPTR
#define UVD_JRBC_ENC_RB_WPTR__RB_WPTR__SHIFT                                                                  0x4
#define UVD_JRBC_ENC_RB_WPTR__RB_WPTR_MASK                                                                    0x007FFFF0L
//UVD_JRBC_ENC_RB_CNTL
#define UVD_JRBC_ENC_RB_CNTL__RB_NO_FETCH__SHIFT                                                              0x0
#define UVD_JRBC_ENC_RB_CNTL__RB_RPTR_WR_EN__SHIFT                                                            0x1
#define UVD_JRBC_ENC_RB_CNTL__RB_PRE_WRITE_TIMER__SHIFT                                                       0x4
#define UVD_JRBC_ENC_RB_CNTL__RB_NO_FETCH_MASK                                                                0x00000001L
#define UVD_JRBC_ENC_RB_CNTL__RB_RPTR_WR_EN_MASK                                                              0x00000002L
#define UVD_JRBC_ENC_RB_CNTL__RB_PRE_WRITE_TIMER_MASK                                                         0x0007FFF0L
//UVD_JRBC_ENC_IB_SIZE
#define UVD_JRBC_ENC_IB_SIZE__IB_SIZE__SHIFT                                                                  0x4
#define UVD_JRBC_ENC_IB_SIZE__IB_SIZE_MASK                                                                    0x007FFFF0L
//UVD_JRBC_ENC_URGENT_CNTL
#define UVD_JRBC_ENC_URGENT_CNTL__CMD_READ_REQ_PRIORITY_MARK__SHIFT                                           0x0
#define UVD_JRBC_ENC_URGENT_CNTL__CMD_READ_REQ_PRIORITY_MARK_MASK                                             0x00000003L
//UVD_JRBC_ENC_RB_REF_DATA
#define UVD_JRBC_ENC_RB_REF_DATA__REF_DATA__SHIFT                                                             0x0
#define UVD_JRBC_ENC_RB_REF_DATA__REF_DATA_MASK                                                               0xFFFFFFFFL
//UVD_JRBC_ENC_RB_COND_RD_TIMER
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__RETRY_TIMER_CNT__SHIFT                                                 0x0
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__RETRY_INTERVAL_CNT__SHIFT                                              0x10
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__CONTINUOUS_POLL_EN__SHIFT                                              0x18
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__MEM_TIMEOUT_EN__SHIFT                                                  0x19
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__RETRY_TIMER_CNT_MASK                                                   0x0000FFFFL
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__RETRY_INTERVAL_CNT_MASK                                                0x00FF0000L
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__CONTINUOUS_POLL_EN_MASK                                                0x01000000L
#define UVD_JRBC_ENC_RB_COND_RD_TIMER__MEM_TIMEOUT_EN_MASK                                                    0x02000000L
//UVD_JRBC_ENC_SOFT_RESET
#define UVD_JRBC_ENC_SOFT_RESET__RESET__SHIFT                                                                 0x0
#define UVD_JRBC_ENC_SOFT_RESET__SCLK_RESET_STATUS__SHIFT                                                     0x11
#define UVD_JRBC_ENC_SOFT_RESET__RESET_MASK                                                                   0x00000001L
#define UVD_JRBC_ENC_SOFT_RESET__SCLK_RESET_STATUS_MASK                                                       0x00020000L
//UVD_JRBC_ENC_STATUS
#define UVD_JRBC_ENC_STATUS__RB_JOB_DONE__SHIFT                                                               0x0
#define UVD_JRBC_ENC_STATUS__IB_JOB_DONE__SHIFT                                                               0x1
#define UVD_JRBC_ENC_STATUS__RB_ILLEGAL_CMD__SHIFT                                                            0x2
#define UVD_JRBC_ENC_STATUS__RB_COND_REG_RD_TIMEOUT__SHIFT                                                    0x3
#define UVD_JRBC_ENC_STATUS__RB_MEM_WR_TIMEOUT__SHIFT                                                         0x4
#define UVD_JRBC_ENC_STATUS__RB_MEM_RD_TIMEOUT__SHIFT                                                         0x5
#define UVD_JRBC_ENC_STATUS__IB_ILLEGAL_CMD__SHIFT                                                            0x6
#define UVD_JRBC_ENC_STATUS__IB_COND_REG_RD_TIMEOUT__SHIFT                                                    0x7
#define UVD_JRBC_ENC_STATUS__IB_MEM_WR_TIMEOUT__SHIFT                                                         0x8
#define UVD_JRBC_ENC_STATUS__IB_MEM_RD_TIMEOUT__SHIFT                                                         0x9
#define UVD_JRBC_ENC_STATUS__RB_TRAP_STATUS__SHIFT                                                            0xa
#define UVD_JRBC_ENC_STATUS__PREEMPT_STATUS__SHIFT                                                            0xb
#define UVD_JRBC_ENC_STATUS__IB_TRAP_STATUS__SHIFT                                                            0xc
#define UVD_JRBC_ENC_STATUS__INT_EN__SHIFT                                                                    0x10
#define UVD_JRBC_ENC_STATUS__INT_ACK__SHIFT                                                                   0x11
#define UVD_JRBC_ENC_STATUS__RB_JOB_DONE_MASK                                                                 0x00000001L
#define UVD_JRBC_ENC_STATUS__IB_JOB_DONE_MASK                                                                 0x00000002L
#define UVD_JRBC_ENC_STATUS__RB_ILLEGAL_CMD_MASK                                                              0x00000004L
#define UVD_JRBC_ENC_STATUS__RB_COND_REG_RD_TIMEOUT_MASK                                                      0x00000008L
#define UVD_JRBC_ENC_STATUS__RB_MEM_WR_TIMEOUT_MASK                                                           0x00000010L
#define UVD_JRBC_ENC_STATUS__RB_MEM_RD_TIMEOUT_MASK                                                           0x00000020L
#define UVD_JRBC_ENC_STATUS__IB_ILLEGAL_CMD_MASK                                                              0x00000040L
#define UVD_JRBC_ENC_STATUS__IB_COND_REG_RD_TIMEOUT_MASK                                                      0x00000080L
#define UVD_JRBC_ENC_STATUS__IB_MEM_WR_TIMEOUT_MASK                                                           0x00000100L
#define UVD_JRBC_ENC_STATUS__IB_MEM_RD_TIMEOUT_MASK                                                           0x00000200L
#define UVD_JRBC_ENC_STATUS__RB_TRAP_STATUS_MASK                                                              0x00000400L
#define UVD_JRBC_ENC_STATUS__PREEMPT_STATUS_MASK                                                              0x00000800L
#define UVD_JRBC_ENC_STATUS__IB_TRAP_STATUS_MASK                                                              0x00001000L
#define UVD_JRBC_ENC_STATUS__INT_EN_MASK                                                                      0x00010000L
#define UVD_JRBC_ENC_STATUS__INT_ACK_MASK                                                                     0x00020000L
//UVD_JRBC_ENC_RB_RPTR
#define UVD_JRBC_ENC_RB_RPTR__RB_RPTR__SHIFT                                                                  0x4
#define UVD_JRBC_ENC_RB_RPTR__RB_RPTR_MASK                                                                    0x007FFFF0L
//UVD_JRBC_ENC_RB_BUF_STATUS
#define UVD_JRBC_ENC_RB_BUF_STATUS__RB_BUF_VALID__SHIFT                                                       0x0
#define UVD_JRBC_ENC_RB_BUF_STATUS__RB_BUF_RD_ADDR__SHIFT                                                     0x10
#define UVD_JRBC_ENC_RB_BUF_STATUS__RB_BUF_WR_ADDR__SHIFT                                                     0x18
#define UVD_JRBC_ENC_RB_BUF_STATUS__RB_BUF_VALID_MASK                                                         0x0000FFFFL
#define UVD_JRBC_ENC_RB_BUF_STATUS__RB_BUF_RD_ADDR_MASK                                                       0x000F0000L
#define UVD_JRBC_ENC_RB_BUF_STATUS__RB_BUF_WR_ADDR_MASK                                                       0x03000000L
//UVD_JRBC_ENC_IB_BUF_STATUS
#define UVD_JRBC_ENC_IB_BUF_STATUS__IB_BUF_VALID__SHIFT                                                       0x0
#define UVD_JRBC_ENC_IB_BUF_STATUS__IB_BUF_RD_ADDR__SHIFT                                                     0x10
#define UVD_JRBC_ENC_IB_BUF_STATUS__IB_BUF_WR_ADDR__SHIFT                                                     0x18
#define UVD_JRBC_ENC_IB_BUF_STATUS__IB_BUF_VALID_MASK                                                         0x0000FFFFL
#define UVD_JRBC_ENC_IB_BUF_STATUS__IB_BUF_RD_ADDR_MASK                                                       0x000F0000L
#define UVD_JRBC_ENC_IB_BUF_STATUS__IB_BUF_WR_ADDR_MASK                                                       0x03000000L
//UVD_JRBC_ENC_IB_SIZE_UPDATE
#define UVD_JRBC_ENC_IB_SIZE_UPDATE__REMAIN_IB_SIZE__SHIFT                                                    0x4
#define UVD_JRBC_ENC_IB_SIZE_UPDATE__REMAIN_IB_SIZE_MASK                                                      0x007FFFF0L
//UVD_JRBC_ENC_IB_COND_RD_TIMER
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__RETRY_TIMER_CNT__SHIFT                                                 0x0
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__RETRY_INTERVAL_CNT__SHIFT                                              0x10
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__CONTINUOUS_POLL_EN__SHIFT                                              0x18
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__MEM_TIMEOUT_EN__SHIFT                                                  0x19
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__RETRY_TIMER_CNT_MASK                                                   0x0000FFFFL
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__RETRY_INTERVAL_CNT_MASK                                                0x00FF0000L
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__CONTINUOUS_POLL_EN_MASK                                                0x01000000L
#define UVD_JRBC_ENC_IB_COND_RD_TIMER__MEM_TIMEOUT_EN_MASK                                                    0x02000000L
//UVD_JRBC_ENC_IB_REF_DATA
#define UVD_JRBC_ENC_IB_REF_DATA__REF_DATA__SHIFT                                                             0x0
#define UVD_JRBC_ENC_IB_REF_DATA__REF_DATA_MASK                                                               0xFFFFFFFFL
//UVD_JPEG_ENC_PREEMPT_CMD
#define UVD_JPEG_ENC_PREEMPT_CMD__PREEMPT_EN__SHIFT                                                           0x0
#define UVD_JPEG_ENC_PREEMPT_CMD__WAIT_JPEG_JOB_DONE__SHIFT                                                   0x1
#define UVD_JPEG_ENC_PREEMPT_CMD__PREEMPT_FENCE_CMD__SHIFT                                                    0x2
#define UVD_JPEG_ENC_PREEMPT_CMD__PREEMPT_EN_MASK                                                             0x00000001L
#define UVD_JPEG_ENC_PREEMPT_CMD__WAIT_JPEG_JOB_DONE_MASK                                                     0x00000002L
#define UVD_JPEG_ENC_PREEMPT_CMD__PREEMPT_FENCE_CMD_MASK                                                      0x00000004L
//UVD_JPEG_ENC_PREEMPT_FENCE_DATA0
#define UVD_JPEG_ENC_PREEMPT_FENCE_DATA0__PREEMPT_FENCE_DATA0__SHIFT                                          0x0
#define UVD_JPEG_ENC_PREEMPT_FENCE_DATA0__PREEMPT_FENCE_DATA0_MASK                                            0xFFFFFFFFL
//UVD_JPEG_ENC_PREEMPT_FENCE_DATA1
#define UVD_JPEG_ENC_PREEMPT_FENCE_DATA1__PREEMPT_FENCE_DATA1__SHIFT                                          0x0
#define UVD_JPEG_ENC_PREEMPT_FENCE_DATA1__PREEMPT_FENCE_DATA1_MASK                                            0xFFFFFFFFL
//UVD_JRBC_ENC_RB_SIZE
#define UVD_JRBC_ENC_RB_SIZE__RB_SIZE__SHIFT                                                                  0x4
#define UVD_JRBC_ENC_RB_SIZE__RB_SIZE_MASK                                                                    0x00FFFFF0L
//UVD_JRBC_ENC_SCRATCH0
#define UVD_JRBC_ENC_SCRATCH0__SCRATCH0__SHIFT                                                                0x0
#define UVD_JRBC_ENC_SCRATCH0__SCRATCH0_MASK                                                                  0xFFFFFFFFL


// addressBlock: uvd0_uvd_jmi_dec
//UVD_JMI_CTRL
#define UVD_JMI_CTRL__STALL_MC_ARB__SHIFT                                                                     0x0
#define UVD_JMI_CTRL__MASK_MC_URGENT__SHIFT                                                                   0x1
#define UVD_JMI_CTRL__ASSERT_MC_URGENT__SHIFT                                                                 0x2
#define UVD_JMI_CTRL__MC_RD_ARB_WAIT_TIMER__SHIFT                                                             0x8
#define UVD_JMI_CTRL__MC_WR_ARB_WAIT_TIMER__SHIFT                                                             0x10
#define UVD_JMI_CTRL__CRC_RESET__SHIFT                                                                        0x18
#define UVD_JMI_CTRL__CRC_SEL__SHIFT                                                                          0x19
#define UVD_JMI_CTRL__STALL_MC_ARB_MASK                                                                       0x00000001L
#define UVD_JMI_CTRL__MASK_MC_URGENT_MASK                                                                     0x00000002L
#define UVD_JMI_CTRL__ASSERT_MC_URGENT_MASK                                                                   0x00000004L
#define UVD_JMI_CTRL__MC_RD_ARB_WAIT_TIMER_MASK                                                               0x0000FF00L
#define UVD_JMI_CTRL__MC_WR_ARB_WAIT_TIMER_MASK                                                               0x00FF0000L
#define UVD_JMI_CTRL__CRC_RESET_MASK                                                                          0x01000000L
#define UVD_JMI_CTRL__CRC_SEL_MASK                                                                            0x1E000000L
//UVD_LMI_JRBC_CTRL
#define UVD_LMI_JRBC_CTRL__ARB_RD_WAIT_EN__SHIFT                                                              0x0
#define UVD_LMI_JRBC_CTRL__ARB_WR_WAIT_EN__SHIFT                                                              0x1
#define UVD_LMI_JRBC_CTRL__RD_MAX_BURST__SHIFT                                                                0x4
#define UVD_LMI_JRBC_CTRL__WR_MAX_BURST__SHIFT                                                                0x8
#define UVD_LMI_JRBC_CTRL__RD_SWAP__SHIFT                                                                     0x14
#define UVD_LMI_JRBC_CTRL__WR_SWAP__SHIFT                                                                     0x16
#define UVD_LMI_JRBC_CTRL__ARB_RD_WAIT_EN_MASK                                                                0x00000001L
#define UVD_LMI_JRBC_CTRL__ARB_WR_WAIT_EN_MASK                                                                0x00000002L
#define UVD_LMI_JRBC_CTRL__RD_MAX_BURST_MASK                                                                  0x000000F0L
#define UVD_LMI_JRBC_CTRL__WR_MAX_BURST_MASK                                                                  0x00000F00L
#define UVD_LMI_JRBC_CTRL__RD_SWAP_MASK                                                                       0x00300000L
#define UVD_LMI_JRBC_CTRL__WR_SWAP_MASK                                                                       0x00C00000L
//UVD_LMI_JPEG_CTRL
#define UVD_LMI_JPEG_CTRL__ARB_RD_WAIT_EN__SHIFT                                                              0x0
#define UVD_LMI_JPEG_CTRL__ARB_WR_WAIT_EN__SHIFT                                                              0x1
#define UVD_LMI_JPEG_CTRL__RD_MAX_BURST__SHIFT                                                                0x4
#define UVD_LMI_JPEG_CTRL__WR_MAX_BURST__SHIFT                                                                0x8
#define UVD_LMI_JPEG_CTRL__RD_SWAP__SHIFT                                                                     0x14
#define UVD_LMI_JPEG_CTRL__WR_SWAP__SHIFT                                                                     0x16
#define UVD_LMI_JPEG_CTRL__ARB_RD_WAIT_EN_MASK                                                                0x00000001L
#define UVD_LMI_JPEG_CTRL__ARB_WR_WAIT_EN_MASK                                                                0x00000002L
#define UVD_LMI_JPEG_CTRL__RD_MAX_BURST_MASK                                                                  0x000000F0L
#define UVD_LMI_JPEG_CTRL__WR_MAX_BURST_MASK                                                                  0x00000F00L
#define UVD_LMI_JPEG_CTRL__RD_SWAP_MASK                                                                       0x00300000L
#define UVD_LMI_JPEG_CTRL__WR_SWAP_MASK                                                                       0x00C00000L
//UVD_JMI_EJRBC_CTRL
#define UVD_JMI_EJRBC_CTRL__ARB_RD_WAIT_EN__SHIFT                                                             0x0
#define UVD_JMI_EJRBC_CTRL__ARB_WR_WAIT_EN__SHIFT                                                             0x1
#define UVD_JMI_EJRBC_CTRL__RD_MAX_BURST__SHIFT                                                               0x4
#define UVD_JMI_EJRBC_CTRL__WR_MAX_BURST__SHIFT                                                               0x8
#define UVD_JMI_EJRBC_CTRL__RD_SWAP__SHIFT                                                                    0x14
#define UVD_JMI_EJRBC_CTRL__WR_SWAP__SHIFT                                                                    0x16
#define UVD_JMI_EJRBC_CTRL__ARB_RD_WAIT_EN_MASK                                                               0x00000001L
#define UVD_JMI_EJRBC_CTRL__ARB_WR_WAIT_EN_MASK                                                               0x00000002L
#define UVD_JMI_EJRBC_CTRL__RD_MAX_BURST_MASK                                                                 0x000000F0L
#define UVD_JMI_EJRBC_CTRL__WR_MAX_BURST_MASK                                                                 0x00000F00L
#define UVD_JMI_EJRBC_CTRL__RD_SWAP_MASK                                                                      0x00300000L
#define UVD_JMI_EJRBC_CTRL__WR_SWAP_MASK                                                                      0x00C00000L
//UVD_LMI_EJPEG_CTRL
#define UVD_LMI_EJPEG_CTRL__ARB_RD_WAIT_EN__SHIFT                                                             0x0
#define UVD_LMI_EJPEG_CTRL__ARB_WR_WAIT_EN__SHIFT                                                             0x1
#define UVD_LMI_EJPEG_CTRL__RD_MAX_BURST__SHIFT                                                               0x4
#define UVD_LMI_EJPEG_CTRL__WR_MAX_BURST__SHIFT                                                               0x8
#define UVD_LMI_EJPEG_CTRL__RD_SWAP__SHIFT                                                                    0x14
#define UVD_LMI_EJPEG_CTRL__WR_SWAP__SHIFT                                                                    0x16
#define UVD_LMI_EJPEG_CTRL__ARB_RD_WAIT_EN_MASK                                                               0x00000001L
#define UVD_LMI_EJPEG_CTRL__ARB_WR_WAIT_EN_MASK                                                               0x00000002L
#define UVD_LMI_EJPEG_CTRL__RD_MAX_BURST_MASK                                                                 0x000000F0L
#define UVD_LMI_EJPEG_CTRL__WR_MAX_BURST_MASK                                                                 0x00000F00L
#define UVD_LMI_EJPEG_CTRL__RD_SWAP_MASK                                                                      0x00300000L
#define UVD_LMI_EJPEG_CTRL__WR_SWAP_MASK                                                                      0x00C00000L
//UVD_LMI_JRBC_IB_VMID
#define UVD_LMI_JRBC_IB_VMID__IB_WR_VMID__SHIFT                                                               0x0
#define UVD_LMI_JRBC_IB_VMID__IB_RD_VMID__SHIFT                                                               0x4
#define UVD_LMI_JRBC_IB_VMID__MEM_RD_VMID__SHIFT                                                              0x8
#define UVD_LMI_JRBC_IB_VMID__IB_WR_VMID_MASK                                                                 0x0000000FL
#define UVD_LMI_JRBC_IB_VMID__IB_RD_VMID_MASK                                                                 0x000000F0L
#define UVD_LMI_JRBC_IB_VMID__MEM_RD_VMID_MASK                                                                0x00000F00L
//UVD_LMI_JRBC_RB_VMID
#define UVD_LMI_JRBC_RB_VMID__RB_WR_VMID__SHIFT                                                               0x0
#define UVD_LMI_JRBC_RB_VMID__RB_RD_VMID__SHIFT                                                               0x4
#define UVD_LMI_JRBC_RB_VMID__MEM_RD_VMID__SHIFT                                                              0x8
#define UVD_LMI_JRBC_RB_VMID__RB_WR_VMID_MASK                                                                 0x0000000FL
#define UVD_LMI_JRBC_RB_VMID__RB_RD_VMID_MASK                                                                 0x000000F0L
#define UVD_LMI_JRBC_RB_VMID__MEM_RD_VMID_MASK                                                                0x00000F00L
//UVD_LMI_JPEG_VMID
#define UVD_LMI_JPEG_VMID__JPEG_RD_VMID__SHIFT                                                                0x0
#define UVD_LMI_JPEG_VMID__JPEG_WR_VMID__SHIFT                                                                0x4
#define UVD_LMI_JPEG_VMID__ATOMIC_USER0_WR_VMID__SHIFT                                                        0x8
#define UVD_LMI_JPEG_VMID__JPEG_RD_VMID_MASK                                                                  0x0000000FL
#define UVD_LMI_JPEG_VMID__JPEG_WR_VMID_MASK                                                                  0x000000F0L
#define UVD_LMI_JPEG_VMID__ATOMIC_USER0_WR_VMID_MASK                                                          0x00000F00L
//UVD_JMI_ENC_JRBC_IB_VMID
#define UVD_JMI_ENC_JRBC_IB_VMID__IB_WR_VMID__SHIFT                                                           0x0
#define UVD_JMI_ENC_JRBC_IB_VMID__IB_RD_VMID__SHIFT                                                           0x4
#define UVD_JMI_ENC_JRBC_IB_VMID__MEM_RD_VMID__SHIFT                                                          0x8
#define UVD_JMI_ENC_JRBC_IB_VMID__IB_WR_VMID_MASK                                                             0x0000000FL
#define UVD_JMI_ENC_JRBC_IB_VMID__IB_RD_VMID_MASK                                                             0x000000F0L
#define UVD_JMI_ENC_JRBC_IB_VMID__MEM_RD_VMID_MASK                                                            0x00000F00L
//UVD_JMI_ENC_JRBC_RB_VMID
#define UVD_JMI_ENC_JRBC_RB_VMID__RB_WR_VMID__SHIFT                                                           0x0
#define UVD_JMI_ENC_JRBC_RB_VMID__RB_RD_VMID__SHIFT                                                           0x4
#define UVD_JMI_ENC_JRBC_RB_VMID__MEM_RD_VMID__SHIFT                                                          0x8
#define UVD_JMI_ENC_JRBC_RB_VMID__RB_WR_VMID_MASK                                                             0x0000000FL
#define UVD_JMI_ENC_JRBC_RB_VMID__RB_RD_VMID_MASK                                                             0x000000F0L
#define UVD_JMI_ENC_JRBC_RB_VMID__MEM_RD_VMID_MASK                                                            0x00000F00L
//UVD_JMI_ENC_JPEG_VMID
#define UVD_JMI_ENC_JPEG_VMID__PEL_RD_VMID__SHIFT                                                             0x0
#define UVD_JMI_ENC_JPEG_VMID__BS_WR_VMID__SHIFT                                                              0x5
#define UVD_JMI_ENC_JPEG_VMID__SCALAR_RD_VMID__SHIFT                                                          0xa
#define UVD_JMI_ENC_JPEG_VMID__SCALAR_WR_VMID__SHIFT                                                          0xf
#define UVD_JMI_ENC_JPEG_VMID__HUFF_FENCE_VMID__SHIFT                                                         0x13
#define UVD_JMI_ENC_JPEG_VMID__ATOMIC_USER1_WR_VMID__SHIFT                                                    0x17
#define UVD_JMI_ENC_JPEG_VMID__PEL_RD_VMID_MASK                                                               0x0000000FL
#define UVD_JMI_ENC_JPEG_VMID__BS_WR_VMID_MASK                                                                0x000001E0L
#define UVD_JMI_ENC_JPEG_VMID__SCALAR_RD_VMID_MASK                                                            0x00003C00L
#define UVD_JMI_ENC_JPEG_VMID__SCALAR_WR_VMID_MASK                                                            0x00078000L
#define UVD_JMI_ENC_JPEG_VMID__HUFF_FENCE_VMID_MASK                                                           0x00780000L
#define UVD_JMI_ENC_JPEG_VMID__ATOMIC_USER1_WR_VMID_MASK                                                      0x07800000L
//UVD_JMI_PERFMON_CTRL
#define UVD_JMI_PERFMON_CTRL__PERFMON_STATE__SHIFT                                                            0x0
#define UVD_JMI_PERFMON_CTRL__PERFMON_SEL__SHIFT                                                              0x8
#define UVD_JMI_PERFMON_CTRL__PERFMON_STATE_MASK                                                              0x00000003L
#define UVD_JMI_PERFMON_CTRL__PERFMON_SEL_MASK                                                                0x00000F00L
//UVD_JMI_PERFMON_COUNT_LO
#define UVD_JMI_PERFMON_COUNT_LO__PERFMON_COUNT__SHIFT                                                        0x0
#define UVD_JMI_PERFMON_COUNT_LO__PERFMON_COUNT_MASK                                                          0xFFFFFFFFL
//UVD_JMI_PERFMON_COUNT_HI
#define UVD_JMI_PERFMON_COUNT_HI__PERFMON_COUNT__SHIFT                                                        0x0
#define UVD_JMI_PERFMON_COUNT_HI__PERFMON_COUNT_MASK                                                          0x0000FFFFL
//UVD_LMI_JPEG_READ_64BIT_BAR_LOW
#define UVD_LMI_JPEG_READ_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_JPEG_READ_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_JPEG_READ_64BIT_BAR_HIGH
#define UVD_LMI_JPEG_READ_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_JPEG_READ_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW
#define UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                    0x0
#define UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW__BITS_31_0_MASK                                                      0xFFFFFFFFL
//UVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH
#define UVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                  0x0
#define UVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                    0xFFFFFFFFL
//UVD_LMI_JPEG_PREEMPT_FENCE_64BIT_BAR_LOW
#define UVD_LMI_JPEG_PREEMPT_FENCE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                            0x0
#define UVD_LMI_JPEG_PREEMPT_FENCE_64BIT_BAR_LOW__BITS_31_0_MASK                                              0xFFFFFFFFL
//UVD_LMI_JPEG_PREEMPT_FENCE_64BIT_BAR_HIGH
#define UVD_LMI_JPEG_PREEMPT_FENCE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                          0x0
#define UVD_LMI_JPEG_PREEMPT_FENCE_64BIT_BAR_HIGH__BITS_63_32_MASK                                            0xFFFFFFFFL
//UVD_LMI_JRBC_RB_64BIT_BAR_LOW
#define UVD_LMI_JRBC_RB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                       0x0
#define UVD_LMI_JRBC_RB_64BIT_BAR_LOW__BITS_31_0_MASK                                                         0xFFFFFFFFL
//UVD_LMI_JRBC_RB_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_RB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                     0x0
#define UVD_LMI_JRBC_RB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                       0xFFFFFFFFL
//UVD_LMI_JRBC_IB_64BIT_BAR_LOW
#define UVD_LMI_JRBC_IB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                       0x0
#define UVD_LMI_JRBC_IB_64BIT_BAR_LOW__BITS_31_0_MASK                                                         0xFFFFFFFFL
//UVD_LMI_JRBC_IB_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_IB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                     0x0
#define UVD_LMI_JRBC_IB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                       0xFFFFFFFFL
//UVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW
#define UVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                0x0
#define UVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW__BITS_31_0_MASK                                                  0xFFFFFFFFL
//UVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                              0x0
#define UVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32_MASK                                                0xFFFFFFFFL
//UVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW
#define UVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                0x0
#define UVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW__BITS_31_0_MASK                                                  0xFFFFFFFFL
//UVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                              0x0
#define UVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32_MASK                                                0xFFFFFFFFL
//UVD_LMI_JRBC_IB_MEM_WR_64BIT_BAR_LOW
#define UVD_LMI_JRBC_IB_MEM_WR_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                0x0
#define UVD_LMI_JRBC_IB_MEM_WR_64BIT_BAR_LOW__BITS_31_0_MASK                                                  0xFFFFFFFFL
//UVD_LMI_JRBC_IB_MEM_WR_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_IB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                              0x0
#define UVD_LMI_JRBC_IB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32_MASK                                                0xFFFFFFFFL
//UVD_LMI_JRBC_IB_MEM_RD_64BIT_BAR_LOW
#define UVD_LMI_JRBC_IB_MEM_RD_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                0x0
#define UVD_LMI_JRBC_IB_MEM_RD_64BIT_BAR_LOW__BITS_31_0_MASK                                                  0xFFFFFFFFL
//UVD_LMI_JRBC_IB_MEM_RD_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_IB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                              0x0
#define UVD_LMI_JRBC_IB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32_MASK                                                0xFFFFFFFFL
//UVD_LMI_EJPEG_PREEMPT_FENCE_64BIT_BAR_LOW
#define UVD_LMI_EJPEG_PREEMPT_FENCE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                           0x0
#define UVD_LMI_EJPEG_PREEMPT_FENCE_64BIT_BAR_LOW__BITS_31_0_MASK                                             0xFFFFFFFFL
//UVD_LMI_EJPEG_PREEMPT_FENCE_64BIT_BAR_HIGH
#define UVD_LMI_EJPEG_PREEMPT_FENCE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                         0x0
#define UVD_LMI_EJPEG_PREEMPT_FENCE_64BIT_BAR_HIGH__BITS_63_32_MASK                                           0xFFFFFFFFL
//UVD_LMI_EJRBC_RB_64BIT_BAR_LOW
#define UVD_LMI_EJRBC_RB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                      0x0
#define UVD_LMI_EJRBC_RB_64BIT_BAR_LOW__BITS_31_0_MASK                                                        0xFFFFFFFFL
//UVD_LMI_EJRBC_RB_64BIT_BAR_HIGH
#define UVD_LMI_EJRBC_RB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                    0x0
#define UVD_LMI_EJRBC_RB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                      0xFFFFFFFFL
//UVD_LMI_EJRBC_IB_64BIT_BAR_LOW
#define UVD_LMI_EJRBC_IB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                      0x0
#define UVD_LMI_EJRBC_IB_64BIT_BAR_LOW__BITS_31_0_MASK                                                        0xFFFFFFFFL
//UVD_LMI_EJRBC_IB_64BIT_BAR_HIGH
#define UVD_LMI_EJRBC_IB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                    0x0
#define UVD_LMI_EJRBC_IB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                      0xFFFFFFFFL
//UVD_LMI_EJRBC_RB_MEM_WR_64BIT_BAR_LOW
#define UVD_LMI_EJRBC_RB_MEM_WR_64BIT_BAR_LOW__BITS_31_0__SHIFT                                               0x0
#define UVD_LMI_EJRBC_RB_MEM_WR_64BIT_BAR_LOW__BITS_31_0_MASK                                                 0xFFFFFFFFL
//UVD_LMI_EJRBC_RB_MEM_WR_64BIT_BAR_HIGH
#define UVD_LMI_EJRBC_RB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                             0x0
#define UVD_LMI_EJRBC_RB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32_MASK                                               0xFFFFFFFFL
//UVD_LMI_EJRBC_RB_MEM_RD_64BIT_BAR_LOW
#define UVD_LMI_EJRBC_RB_MEM_RD_64BIT_BAR_LOW__BITS_31_0__SHIFT                                               0x0
#define UVD_LMI_EJRBC_RB_MEM_RD_64BIT_BAR_LOW__BITS_31_0_MASK                                                 0xFFFFFFFFL
//UVD_LMI_EJRBC_RB_MEM_RD_64BIT_BAR_HIGH
#define UVD_LMI_EJRBC_RB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                             0x0
#define UVD_LMI_EJRBC_RB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32_MASK                                               0xFFFFFFFFL
//UVD_LMI_EJRBC_IB_MEM_WR_64BIT_BAR_LOW
#define UVD_LMI_EJRBC_IB_MEM_WR_64BIT_BAR_LOW__BITS_31_0__SHIFT                                               0x0
#define UVD_LMI_EJRBC_IB_MEM_WR_64BIT_BAR_LOW__BITS_31_0_MASK                                                 0xFFFFFFFFL
//UVD_LMI_EJRBC_IB_MEM_WR_64BIT_BAR_HIGH
#define UVD_LMI_EJRBC_IB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                             0x0
#define UVD_LMI_EJRBC_IB_MEM_WR_64BIT_BAR_HIGH__BITS_63_32_MASK                                               0xFFFFFFFFL
//UVD_LMI_EJRBC_IB_MEM_RD_64BIT_BAR_LOW
#define UVD_LMI_EJRBC_IB_MEM_RD_64BIT_BAR_LOW__BITS_31_0__SHIFT                                               0x0
#define UVD_LMI_EJRBC_IB_MEM_RD_64BIT_BAR_LOW__BITS_31_0_MASK                                                 0xFFFFFFFFL
//UVD_LMI_EJRBC_IB_MEM_RD_64BIT_BAR_HIGH
#define UVD_LMI_EJRBC_IB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                             0x0
#define UVD_LMI_EJRBC_IB_MEM_RD_64BIT_BAR_HIGH__BITS_63_32_MASK                                               0xFFFFFFFFL
//UVD_LMI_JPEG_PREEMPT_VMID
#define UVD_LMI_JPEG_PREEMPT_VMID__VMID__SHIFT                                                                0x0
#define UVD_LMI_JPEG_PREEMPT_VMID__VMID_MASK                                                                  0x0000000FL
//UVD_LMI_ENC_JPEG_PREEMPT_VMID
#define UVD_LMI_ENC_JPEG_PREEMPT_VMID__VMID__SHIFT                                                            0x0
#define UVD_LMI_ENC_JPEG_PREEMPT_VMID__VMID_MASK                                                              0x0000000FL
//UVD_LMI_JPEG2_VMID
#define UVD_LMI_JPEG2_VMID__JPEG2_RD_VMID__SHIFT                                                              0x0
#define UVD_LMI_JPEG2_VMID__JPEG2_WR_VMID__SHIFT                                                              0x4
#define UVD_LMI_JPEG2_VMID__JPEG2_RD_VMID_MASK                                                                0x0000000FL
#define UVD_LMI_JPEG2_VMID__JPEG2_WR_VMID_MASK                                                                0x000000F0L
//UVD_LMI_JPEG2_READ_64BIT_BAR_LOW
#define UVD_LMI_JPEG2_READ_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                    0x0
#define UVD_LMI_JPEG2_READ_64BIT_BAR_LOW__BITS_31_0_MASK                                                      0xFFFFFFFFL
//UVD_LMI_JPEG2_READ_64BIT_BAR_HIGH
#define UVD_LMI_JPEG2_READ_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                  0x0
#define UVD_LMI_JPEG2_READ_64BIT_BAR_HIGH__BITS_63_32_MASK                                                    0xFFFFFFFFL
//UVD_LMI_JPEG2_WRITE_64BIT_BAR_LOW
#define UVD_LMI_JPEG2_WRITE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_JPEG2_WRITE_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_JPEG2_WRITE_64BIT_BAR_HIGH
#define UVD_LMI_JPEG2_WRITE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_JPEG2_WRITE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_JPEG_CTRL2
#define UVD_LMI_JPEG_CTRL2__ARB_RD_WAIT_EN__SHIFT                                                             0x0
#define UVD_LMI_JPEG_CTRL2__ARB_WR_WAIT_EN__SHIFT                                                             0x1
#define UVD_LMI_JPEG_CTRL2__RD_MAX_BURST__SHIFT                                                               0x4
#define UVD_LMI_JPEG_CTRL2__WR_MAX_BURST__SHIFT                                                               0x8
#define UVD_LMI_JPEG_CTRL2__RD_SWAP__SHIFT                                                                    0x14
#define UVD_LMI_JPEG_CTRL2__WR_SWAP__SHIFT                                                                    0x16
#define UVD_LMI_JPEG_CTRL2__ARB_RD_WAIT_EN_MASK                                                               0x00000001L
#define UVD_LMI_JPEG_CTRL2__ARB_WR_WAIT_EN_MASK                                                               0x00000002L
#define UVD_LMI_JPEG_CTRL2__RD_MAX_BURST_MASK                                                                 0x000000F0L
#define UVD_LMI_JPEG_CTRL2__WR_MAX_BURST_MASK                                                                 0x00000F00L
#define UVD_LMI_JPEG_CTRL2__RD_SWAP_MASK                                                                      0x00300000L
#define UVD_LMI_JPEG_CTRL2__WR_SWAP_MASK                                                                      0x00C00000L
//UVD_JMI_DEC_SWAP_CNTL
#define UVD_JMI_DEC_SWAP_CNTL__RB_MC_SWAP__SHIFT                                                              0x0
#define UVD_JMI_DEC_SWAP_CNTL__IB_MC_SWAP__SHIFT                                                              0x2
#define UVD_JMI_DEC_SWAP_CNTL__RB_MEM_WR_MC_SWAP__SHIFT                                                       0x4
#define UVD_JMI_DEC_SWAP_CNTL__IB_MEM_WR_MC_SWAP__SHIFT                                                       0x6
#define UVD_JMI_DEC_SWAP_CNTL__RB_MEM_RD_MC_SWAP__SHIFT                                                       0x8
#define UVD_JMI_DEC_SWAP_CNTL__IB_MEM_RD_MC_SWAP__SHIFT                                                       0xa
#define UVD_JMI_DEC_SWAP_CNTL__PREEMPT_WR_MC_SWAP__SHIFT                                                      0xc
#define UVD_JMI_DEC_SWAP_CNTL__JPEG_RD_MC_SWAP__SHIFT                                                         0xe
#define UVD_JMI_DEC_SWAP_CNTL__JPEG_WR_MC_SWAP__SHIFT                                                         0x10
#define UVD_JMI_DEC_SWAP_CNTL__RB_MC_SWAP_MASK                                                                0x00000003L
#define UVD_JMI_DEC_SWAP_CNTL__IB_MC_SWAP_MASK                                                                0x0000000CL
#define UVD_JMI_DEC_SWAP_CNTL__RB_MEM_WR_MC_SWAP_MASK                                                         0x00000030L
#define UVD_JMI_DEC_SWAP_CNTL__IB_MEM_WR_MC_SWAP_MASK                                                         0x000000C0L
#define UVD_JMI_DEC_SWAP_CNTL__RB_MEM_RD_MC_SWAP_MASK                                                         0x00000300L
#define UVD_JMI_DEC_SWAP_CNTL__IB_MEM_RD_MC_SWAP_MASK                                                         0x00000C00L
#define UVD_JMI_DEC_SWAP_CNTL__PREEMPT_WR_MC_SWAP_MASK                                                        0x00003000L
#define UVD_JMI_DEC_SWAP_CNTL__JPEG_RD_MC_SWAP_MASK                                                           0x0000C000L
#define UVD_JMI_DEC_SWAP_CNTL__JPEG_WR_MC_SWAP_MASK                                                           0x00030000L
//UVD_JMI_ENC_SWAP_CNTL
#define UVD_JMI_ENC_SWAP_CNTL__RB_MC_SWAP__SHIFT                                                              0x0
#define UVD_JMI_ENC_SWAP_CNTL__IB_MC_SWAP__SHIFT                                                              0x2
#define UVD_JMI_ENC_SWAP_CNTL__RB_MEM_WR_MC_SWAP__SHIFT                                                       0x4
#define UVD_JMI_ENC_SWAP_CNTL__IB_MEM_WR_MC_SWAP__SHIFT                                                       0x6
#define UVD_JMI_ENC_SWAP_CNTL__RB_MEM_RD_MC_SWAP__SHIFT                                                       0x8
#define UVD_JMI_ENC_SWAP_CNTL__IB_MEM_RD_MC_SWAP__SHIFT                                                       0xa
#define UVD_JMI_ENC_SWAP_CNTL__PREEMPT_WR_MC_SWAP__SHIFT                                                      0xc
#define UVD_JMI_ENC_SWAP_CNTL__PEL_RD_MC_SWAP__SHIFT                                                          0xe
#define UVD_JMI_ENC_SWAP_CNTL__BS_WR_MC_SWAP__SHIFT                                                           0x10
#define UVD_JMI_ENC_SWAP_CNTL__SCALAR_RD_MC_SWAP__SHIFT                                                       0x12
#define UVD_JMI_ENC_SWAP_CNTL__SCALAR_WR_MC_SWAP__SHIFT                                                       0x14
#define UVD_JMI_ENC_SWAP_CNTL__HUFF_FENCE_MC_SWAP__SHIFT                                                      0x16
#define UVD_JMI_ENC_SWAP_CNTL__RB_MC_SWAP_MASK                                                                0x00000003L
#define UVD_JMI_ENC_SWAP_CNTL__IB_MC_SWAP_MASK                                                                0x0000000CL
#define UVD_JMI_ENC_SWAP_CNTL__RB_MEM_WR_MC_SWAP_MASK                                                         0x00000030L
#define UVD_JMI_ENC_SWAP_CNTL__IB_MEM_WR_MC_SWAP_MASK                                                         0x000000C0L
#define UVD_JMI_ENC_SWAP_CNTL__RB_MEM_RD_MC_SWAP_MASK                                                         0x00000300L
#define UVD_JMI_ENC_SWAP_CNTL__IB_MEM_RD_MC_SWAP_MASK                                                         0x00000C00L
#define UVD_JMI_ENC_SWAP_CNTL__PREEMPT_WR_MC_SWAP_MASK                                                        0x00003000L
#define UVD_JMI_ENC_SWAP_CNTL__PEL_RD_MC_SWAP_MASK                                                            0x0000C000L
#define UVD_JMI_ENC_SWAP_CNTL__BS_WR_MC_SWAP_MASK                                                             0x00030000L
#define UVD_JMI_ENC_SWAP_CNTL__SCALAR_RD_MC_SWAP_MASK                                                         0x000C0000L
#define UVD_JMI_ENC_SWAP_CNTL__SCALAR_WR_MC_SWAP_MASK                                                         0x00300000L
#define UVD_JMI_ENC_SWAP_CNTL__HUFF_FENCE_MC_SWAP_MASK                                                        0x00C00000L
//UVD_JMI_CNTL
#define UVD_JMI_CNTL__SOFT_RESET__SHIFT                                                                       0x0
#define UVD_JMI_CNTL__MC_RD_REQ_RET_MAX__SHIFT                                                                0x8
#define UVD_JMI_CNTL__SOFT_RESET_MASK                                                                         0x00000001L
#define UVD_JMI_CNTL__MC_RD_REQ_RET_MAX_MASK                                                                  0x0003FF00L
//UVD_JMI_HUFF_FENCE_64BIT_BAR_LOW
#define UVD_JMI_HUFF_FENCE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                    0x0
#define UVD_JMI_HUFF_FENCE_64BIT_BAR_LOW__BITS_31_0_MASK                                                      0xFFFFFFFFL
//UVD_JMI_HUFF_FENCE_64BIT_BAR_HIGH
#define UVD_JMI_HUFF_FENCE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                  0x0
#define UVD_JMI_HUFF_FENCE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                    0xFFFFFFFFL
//UVD_JMI_DEC_SWAP_CNTL2
#define UVD_JMI_DEC_SWAP_CNTL2__JPEG2_RD_MC_SWAP__SHIFT                                                       0x0
#define UVD_JMI_DEC_SWAP_CNTL2__JPEG2_WR_MC_SWAP__SHIFT                                                       0x2
#define UVD_JMI_DEC_SWAP_CNTL2__JPEG2_RD_MC_SWAP_MASK                                                         0x00000003L
#define UVD_JMI_DEC_SWAP_CNTL2__JPEG2_WR_MC_SWAP_MASK                                                         0x0000000CL


// addressBlock: uvd0_uvd_jpeg_common_dec
//JPEG_SOFT_RESET_STATUS
#define JPEG_SOFT_RESET_STATUS__JPEG_DEC_RESET_STATUS__SHIFT                                                  0x0
#define JPEG_SOFT_RESET_STATUS__JPEG2_DEC_RESET_STATUS__SHIFT                                                 0x1
#define JPEG_SOFT_RESET_STATUS__DJRBC_RESET_STATUS__SHIFT                                                     0x2
#define JPEG_SOFT_RESET_STATUS__JPEG_ENC_RESET_STATUS__SHIFT                                                  0x3
#define JPEG_SOFT_RESET_STATUS__EJRBC_RESET_STATUS__SHIFT                                                     0x4
#define JPEG_SOFT_RESET_STATUS__JMCIF_RESET_STATUS__SHIFT                                                     0x5
#define JPEG_SOFT_RESET_STATUS__JPEG_DEC_RESET_STATUS_MASK                                                    0x00000001L
#define JPEG_SOFT_RESET_STATUS__JPEG2_DEC_RESET_STATUS_MASK                                                   0x00000002L
#define JPEG_SOFT_RESET_STATUS__DJRBC_RESET_STATUS_MASK                                                       0x00000004L
#define JPEG_SOFT_RESET_STATUS__JPEG_ENC_RESET_STATUS_MASK                                                    0x00000008L
#define JPEG_SOFT_RESET_STATUS__EJRBC_RESET_STATUS_MASK                                                       0x00000010L
#define JPEG_SOFT_RESET_STATUS__JMCIF_RESET_STATUS_MASK                                                       0x00000020L
//JPEG_SYS_INT_EN
#define JPEG_SYS_INT_EN__DJPEG_CORE__SHIFT                                                                    0x0
#define JPEG_SYS_INT_EN__DJRBC__SHIFT                                                                         0x1
#define JPEG_SYS_INT_EN__DJPEG_PF_RPT__SHIFT                                                                  0x2
#define JPEG_SYS_INT_EN__EJPEG_PF_RPT__SHIFT                                                                  0x3
#define JPEG_SYS_INT_EN__EJPEG_CORE__SHIFT                                                                    0x4
#define JPEG_SYS_INT_EN__EJRBC__SHIFT                                                                         0x5
#define JPEG_SYS_INT_EN__DJPEG_CORE2__SHIFT                                                                   0x6
#define JPEG_SYS_INT_EN__DJPEG_CORE_MASK                                                                      0x00000001L
#define JPEG_SYS_INT_EN__DJRBC_MASK                                                                           0x00000002L
#define JPEG_SYS_INT_EN__DJPEG_PF_RPT_MASK                                                                    0x00000004L
#define JPEG_SYS_INT_EN__EJPEG_PF_RPT_MASK                                                                    0x00000008L
#define JPEG_SYS_INT_EN__EJPEG_CORE_MASK                                                                      0x00000010L
#define JPEG_SYS_INT_EN__EJRBC_MASK                                                                           0x00000020L
#define JPEG_SYS_INT_EN__DJPEG_CORE2_MASK                                                                     0x00000040L
//JPEG_SYS_INT_STATUS
#define JPEG_SYS_INT_STATUS__DJPEG_CORE__SHIFT                                                                0x0
#define JPEG_SYS_INT_STATUS__DJRBC__SHIFT                                                                     0x1
#define JPEG_SYS_INT_STATUS__DJPEG_PF_RPT__SHIFT                                                              0x2
#define JPEG_SYS_INT_STATUS__EJPEG_PF_RPT__SHIFT                                                              0x3
#define JPEG_SYS_INT_STATUS__EJPEG_CORE__SHIFT                                                                0x4
#define JPEG_SYS_INT_STATUS__EJRBC__SHIFT                                                                     0x5
#define JPEG_SYS_INT_STATUS__DJPEG_CORE2__SHIFT                                                               0x6
#define JPEG_SYS_INT_STATUS__DJPEG_CORE_MASK                                                                  0x00000001L
#define JPEG_SYS_INT_STATUS__DJRBC_MASK                                                                       0x00000002L
#define JPEG_SYS_INT_STATUS__DJPEG_PF_RPT_MASK                                                                0x00000004L
#define JPEG_SYS_INT_STATUS__EJPEG_PF_RPT_MASK                                                                0x00000008L
#define JPEG_SYS_INT_STATUS__EJPEG_CORE_MASK                                                                  0x00000010L
#define JPEG_SYS_INT_STATUS__EJRBC_MASK                                                                       0x00000020L
#define JPEG_SYS_INT_STATUS__DJPEG_CORE2_MASK                                                                 0x00000040L
//JPEG_SYS_INT_ACK
#define JPEG_SYS_INT_ACK__DJPEG_CORE__SHIFT                                                                   0x0
#define JPEG_SYS_INT_ACK__DJRBC__SHIFT                                                                        0x1
#define JPEG_SYS_INT_ACK__DJPEG_PF_RPT__SHIFT                                                                 0x2
#define JPEG_SYS_INT_ACK__EJPEG_PF_RPT__SHIFT                                                                 0x3
#define JPEG_SYS_INT_ACK__EJPEG_CORE__SHIFT                                                                   0x4
#define JPEG_SYS_INT_ACK__EJRBC__SHIFT                                                                        0x5
#define JPEG_SYS_INT_ACK__DJPEG_CORE2__SHIFT                                                                  0x6
#define JPEG_SYS_INT_ACK__DJPEG_CORE_MASK                                                                     0x00000001L
#define JPEG_SYS_INT_ACK__DJRBC_MASK                                                                          0x00000002L
#define JPEG_SYS_INT_ACK__DJPEG_PF_RPT_MASK                                                                   0x00000004L
#define JPEG_SYS_INT_ACK__EJPEG_PF_RPT_MASK                                                                   0x00000008L
#define JPEG_SYS_INT_ACK__EJPEG_CORE_MASK                                                                     0x00000010L
#define JPEG_SYS_INT_ACK__EJRBC_MASK                                                                          0x00000020L
#define JPEG_SYS_INT_ACK__DJPEG_CORE2_MASK                                                                    0x00000040L
//JPEG_MASTINT_EN
#define JPEG_MASTINT_EN__OVERRUN_RST__SHIFT                                                                   0x0
#define JPEG_MASTINT_EN__INT_OVERRUN__SHIFT                                                                   0x4
#define JPEG_MASTINT_EN__OVERRUN_RST_MASK                                                                     0x00000001L
#define JPEG_MASTINT_EN__INT_OVERRUN_MASK                                                                     0x007FFFF0L
//JPEG_IH_CTRL
#define JPEG_IH_CTRL__IH_SOFT_RESET__SHIFT                                                                    0x0
#define JPEG_IH_CTRL__IH_STALL_EN__SHIFT                                                                      0x1
#define JPEG_IH_CTRL__IH_STATUS_CLEAN__SHIFT                                                                  0x2
#define JPEG_IH_CTRL__IH_VMID__SHIFT                                                                          0x3
#define JPEG_IH_CTRL__IH_USER_DATA__SHIFT                                                                     0x7
#define JPEG_IH_CTRL__IH_RINGID__SHIFT                                                                        0x13
#define JPEG_IH_CTRL__IH_SOFT_RESET_MASK                                                                      0x00000001L
#define JPEG_IH_CTRL__IH_STALL_EN_MASK                                                                        0x00000002L
#define JPEG_IH_CTRL__IH_STATUS_CLEAN_MASK                                                                    0x00000004L
#define JPEG_IH_CTRL__IH_VMID_MASK                                                                            0x00000078L
#define JPEG_IH_CTRL__IH_USER_DATA_MASK                                                                       0x0007FF80L
#define JPEG_IH_CTRL__IH_RINGID_MASK                                                                          0x07F80000L
//JRBBM_ARB_CTRL
#define JRBBM_ARB_CTRL__DJRBC_DROP__SHIFT                                                                     0x0
#define JRBBM_ARB_CTRL__EJRBC_DROP__SHIFT                                                                     0x1
#define JRBBM_ARB_CTRL__SRBM_DROP__SHIFT                                                                      0x2
#define JRBBM_ARB_CTRL__DJRBC_DROP_MASK                                                                       0x00000001L
#define JRBBM_ARB_CTRL__EJRBC_DROP_MASK                                                                       0x00000002L
#define JRBBM_ARB_CTRL__SRBM_DROP_MASK                                                                        0x00000004L


// addressBlock: uvd0_uvd_jpeg_common_sclk_dec
//JPEG_CGC_GATE
#define JPEG_CGC_GATE__JPEG_DEC__SHIFT                                                                        0x0
#define JPEG_CGC_GATE__JPEG2_DEC__SHIFT                                                                       0x1
#define JPEG_CGC_GATE__JPEG_ENC__SHIFT                                                                        0x2
#define JPEG_CGC_GATE__JMCIF__SHIFT                                                                           0x3
#define JPEG_CGC_GATE__JRBBM__SHIFT                                                                           0x4
#define JPEG_CGC_GATE__JPEG_DEC_MASK                                                                          0x00000001L
#define JPEG_CGC_GATE__JPEG2_DEC_MASK                                                                         0x00000002L
#define JPEG_CGC_GATE__JPEG_ENC_MASK                                                                          0x00000004L
#define JPEG_CGC_GATE__JMCIF_MASK                                                                             0x00000008L
#define JPEG_CGC_GATE__JRBBM_MASK                                                                             0x00000010L
//JPEG_CGC_CTRL
#define JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT                                                                  0x0
#define JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT                                                              0x1
#define JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT                                                                   0x5
#define JPEG_CGC_CTRL__DYN_OCLK_RAMP_EN__SHIFT                                                                0xa
#define JPEG_CGC_CTRL__DYN_RCLK_RAMP_EN__SHIFT                                                                0xb
#define JPEG_CGC_CTRL__GATER_DIV_ID__SHIFT                                                                    0xc
#define JPEG_CGC_CTRL__JPEG_DEC_MODE__SHIFT                                                                   0x10
#define JPEG_CGC_CTRL__JPEG2_DEC_MODE__SHIFT                                                                  0x11
#define JPEG_CGC_CTRL__JPEG_ENC_MODE__SHIFT                                                                   0x12
#define JPEG_CGC_CTRL__JMCIF_MODE__SHIFT                                                                      0x13
#define JPEG_CGC_CTRL__JRBBM_MODE__SHIFT                                                                      0x14
#define JPEG_CGC_CTRL__DYN_CLOCK_MODE_MASK                                                                    0x00000001L
#define JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK                                                                0x0000001EL
#define JPEG_CGC_CTRL__CLK_OFF_DELAY_MASK                                                                     0x000003E0L
#define JPEG_CGC_CTRL__DYN_OCLK_RAMP_EN_MASK                                                                  0x00000400L
#define JPEG_CGC_CTRL__DYN_RCLK_RAMP_EN_MASK                                                                  0x00000800L
#define JPEG_CGC_CTRL__GATER_DIV_ID_MASK                                                                      0x00007000L
#define JPEG_CGC_CTRL__JPEG_DEC_MODE_MASK                                                                     0x00010000L
#define JPEG_CGC_CTRL__JPEG2_DEC_MODE_MASK                                                                    0x00020000L
#define JPEG_CGC_CTRL__JPEG_ENC_MODE_MASK                                                                     0x00040000L
#define JPEG_CGC_CTRL__JMCIF_MODE_MASK                                                                        0x00080000L
#define JPEG_CGC_CTRL__JRBBM_MODE_MASK                                                                        0x00100000L
//JPEG_CGC_STATUS
#define JPEG_CGC_STATUS__JPEG_DEC_VCLK_ACTIVE__SHIFT                                                          0x0
#define JPEG_CGC_STATUS__JPEG_DEC_SCLK_ACTIVE__SHIFT                                                          0x1
#define JPEG_CGC_STATUS__JPEG2_DEC_VCLK_ACTIVE__SHIFT                                                         0x2
#define JPEG_CGC_STATUS__JPEG2_DEC_SCLK_ACTIVE__SHIFT                                                         0x3
#define JPEG_CGC_STATUS__JPEG_ENC_VCLK_ACTIVE__SHIFT                                                          0x4
#define JPEG_CGC_STATUS__JPEG_ENC_SCLK_ACTIVE__SHIFT                                                          0x5
#define JPEG_CGC_STATUS__JMCIF_SCLK_ACTIVE__SHIFT                                                             0x6
#define JPEG_CGC_STATUS__JRBBM_VCLK_ACTIVE__SHIFT                                                             0x7
#define JPEG_CGC_STATUS__JRBBM_SCLK_ACTIVE__SHIFT                                                             0x8
#define JPEG_CGC_STATUS__JPEG_DEC_VCLK_ACTIVE_MASK                                                            0x00000001L
#define JPEG_CGC_STATUS__JPEG_DEC_SCLK_ACTIVE_MASK                                                            0x00000002L
#define JPEG_CGC_STATUS__JPEG2_DEC_VCLK_ACTIVE_MASK                                                           0x00000004L
#define JPEG_CGC_STATUS__JPEG2_DEC_SCLK_ACTIVE_MASK                                                           0x00000008L
#define JPEG_CGC_STATUS__JPEG_ENC_VCLK_ACTIVE_MASK                                                            0x00000010L
#define JPEG_CGC_STATUS__JPEG_ENC_SCLK_ACTIVE_MASK                                                            0x00000020L
#define JPEG_CGC_STATUS__JMCIF_SCLK_ACTIVE_MASK                                                               0x00000040L
#define JPEG_CGC_STATUS__JRBBM_VCLK_ACTIVE_MASK                                                               0x00000080L
#define JPEG_CGC_STATUS__JRBBM_SCLK_ACTIVE_MASK                                                               0x00000100L
//JPEG_COMN_CGC_MEM_CTRL
#define JPEG_COMN_CGC_MEM_CTRL__JMCIF_LS_EN__SHIFT                                                            0x0
#define JPEG_COMN_CGC_MEM_CTRL__JMCIF_DS_EN__SHIFT                                                            0x1
#define JPEG_COMN_CGC_MEM_CTRL__JMCIF_SD_EN__SHIFT                                                            0x2
#define JPEG_COMN_CGC_MEM_CTRL__LS_SET_DELAY__SHIFT                                                           0x10
#define JPEG_COMN_CGC_MEM_CTRL__LS_CLEAR_DELAY__SHIFT                                                         0x14
#define JPEG_COMN_CGC_MEM_CTRL__JMCIF_LS_EN_MASK                                                              0x00000001L
#define JPEG_COMN_CGC_MEM_CTRL__JMCIF_DS_EN_MASK                                                              0x00000002L
#define JPEG_COMN_CGC_MEM_CTRL__JMCIF_SD_EN_MASK                                                              0x00000004L
#define JPEG_COMN_CGC_MEM_CTRL__LS_SET_DELAY_MASK                                                             0x000F0000L
#define JPEG_COMN_CGC_MEM_CTRL__LS_CLEAR_DELAY_MASK                                                           0x00F00000L
//JPEG_DEC_CGC_MEM_CTRL
#define JPEG_DEC_CGC_MEM_CTRL__JPEG_DEC_LS_EN__SHIFT                                                          0x0
#define JPEG_DEC_CGC_MEM_CTRL__JPEG_DEC_DS_EN__SHIFT                                                          0x1
#define JPEG_DEC_CGC_MEM_CTRL__JPEG_DEC_SD_EN__SHIFT                                                          0x2
#define JPEG_DEC_CGC_MEM_CTRL__JPEG_DEC_LS_EN_MASK                                                            0x00000001L
#define JPEG_DEC_CGC_MEM_CTRL__JPEG_DEC_DS_EN_MASK                                                            0x00000002L
#define JPEG_DEC_CGC_MEM_CTRL__JPEG_DEC_SD_EN_MASK                                                            0x00000004L
//JPEG2_DEC_CGC_MEM_CTRL
#define JPEG2_DEC_CGC_MEM_CTRL__JPEG2_DEC_LS_EN__SHIFT                                                        0x0
#define JPEG2_DEC_CGC_MEM_CTRL__JPEG2_DEC_DS_EN__SHIFT                                                        0x1
#define JPEG2_DEC_CGC_MEM_CTRL__JPEG2_DEC_SD_EN__SHIFT                                                        0x2
#define JPEG2_DEC_CGC_MEM_CTRL__JPEG2_DEC_LS_EN_MASK                                                          0x00000001L
#define JPEG2_DEC_CGC_MEM_CTRL__JPEG2_DEC_DS_EN_MASK                                                          0x00000002L
#define JPEG2_DEC_CGC_MEM_CTRL__JPEG2_DEC_SD_EN_MASK                                                          0x00000004L
//JPEG_ENC_CGC_MEM_CTRL
#define JPEG_ENC_CGC_MEM_CTRL__JPEG_ENC_LS_EN__SHIFT                                                          0x0
#define JPEG_ENC_CGC_MEM_CTRL__JPEG_ENC_DS_EN__SHIFT                                                          0x1
#define JPEG_ENC_CGC_MEM_CTRL__JPEG_ENC_SD_EN__SHIFT                                                          0x2
#define JPEG_ENC_CGC_MEM_CTRL__JPEG_ENC_LS_EN_MASK                                                            0x00000001L
#define JPEG_ENC_CGC_MEM_CTRL__JPEG_ENC_DS_EN_MASK                                                            0x00000002L
#define JPEG_ENC_CGC_MEM_CTRL__JPEG_ENC_SD_EN_MASK                                                            0x00000004L
//JPEG_SOFT_RESET2
#define JPEG_SOFT_RESET2__ATOMIC_SOFT_RESET__SHIFT                                                            0x0
#define JPEG_SOFT_RESET2__ATOMIC_SOFT_RESET_MASK                                                              0x00000001L
//JPEG_PERF_BANK_CONF
#define JPEG_PERF_BANK_CONF__RESET__SHIFT                                                                     0x0
#define JPEG_PERF_BANK_CONF__PEEK__SHIFT                                                                      0x8
#define JPEG_PERF_BANK_CONF__CONCATENATE__SHIFT                                                               0x10
#define JPEG_PERF_BANK_CONF__RESET_MASK                                                                       0x0000000FL
#define JPEG_PERF_BANK_CONF__PEEK_MASK                                                                        0x00000F00L
#define JPEG_PERF_BANK_CONF__CONCATENATE_MASK                                                                 0x00030000L
//JPEG_PERF_BANK_EVENT_SEL
#define JPEG_PERF_BANK_EVENT_SEL__SEL0__SHIFT                                                                 0x0
#define JPEG_PERF_BANK_EVENT_SEL__SEL1__SHIFT                                                                 0x8
#define JPEG_PERF_BANK_EVENT_SEL__SEL2__SHIFT                                                                 0x10
#define JPEG_PERF_BANK_EVENT_SEL__SEL3__SHIFT                                                                 0x18
#define JPEG_PERF_BANK_EVENT_SEL__SEL0_MASK                                                                   0x000000FFL
#define JPEG_PERF_BANK_EVENT_SEL__SEL1_MASK                                                                   0x0000FF00L
#define JPEG_PERF_BANK_EVENT_SEL__SEL2_MASK                                                                   0x00FF0000L
#define JPEG_PERF_BANK_EVENT_SEL__SEL3_MASK                                                                   0xFF000000L
//JPEG_PERF_BANK_COUNT0
#define JPEG_PERF_BANK_COUNT0__COUNT__SHIFT                                                                   0x0
#define JPEG_PERF_BANK_COUNT0__COUNT_MASK                                                                     0xFFFFFFFFL
//JPEG_PERF_BANK_COUNT1
#define JPEG_PERF_BANK_COUNT1__COUNT__SHIFT                                                                   0x0
#define JPEG_PERF_BANK_COUNT1__COUNT_MASK                                                                     0xFFFFFFFFL
//JPEG_PERF_BANK_COUNT2
#define JPEG_PERF_BANK_COUNT2__COUNT__SHIFT                                                                   0x0
#define JPEG_PERF_BANK_COUNT2__COUNT_MASK                                                                     0xFFFFFFFFL
//JPEG_PERF_BANK_COUNT3
#define JPEG_PERF_BANK_COUNT3__COUNT__SHIFT                                                                   0x0
#define JPEG_PERF_BANK_COUNT3__COUNT_MASK                                                                     0xFFFFFFFFL


// addressBlock: uvd0_uvd_pg_dec
//UVD_PGFSM_CONFIG
#define UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT                                                              0x0
#define UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT                                                              0x2
#define UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT                                                              0x4
#define UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT                                                              0x6
#define UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT                                                              0x8
#define UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG__SHIFT                                                             0xa
#define UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG__SHIFT                                                             0xc
#define UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT                                                             0xe
#define UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT                                                             0x10
#define UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT                                                              0x12
#define UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG__SHIFT                                                              0x14
#define UVD_PGFSM_CONFIG__UVDJ_PWR_CONFIG__SHIFT                                                              0x16
#define UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG_MASK                                                                0x00000003L
#define UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG_MASK                                                                0x0000000CL
#define UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG_MASK                                                                0x00000030L
#define UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG_MASK                                                                0x000000C0L
#define UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG_MASK                                                                0x00000300L
#define UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG_MASK                                                               0x00000C00L
#define UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG_MASK                                                               0x00003000L
#define UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG_MASK                                                               0x0000C000L
#define UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG_MASK                                                               0x00030000L
#define UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG_MASK                                                                0x000C0000L
#define UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG_MASK                                                                0x00300000L
#define UVD_PGFSM_CONFIG__UVDJ_PWR_CONFIG_MASK                                                                0x00C00000L
//UVD_PGFSM_STATUS
#define UVD_PGFSM_STATUS__UVDM_PWR_STATUS__SHIFT                                                              0x0
#define UVD_PGFSM_STATUS__UVDU_PWR_STATUS__SHIFT                                                              0x2
#define UVD_PGFSM_STATUS__UVDF_PWR_STATUS__SHIFT                                                              0x4
#define UVD_PGFSM_STATUS__UVDC_PWR_STATUS__SHIFT                                                              0x6
#define UVD_PGFSM_STATUS__UVDB_PWR_STATUS__SHIFT                                                              0x8
#define UVD_PGFSM_STATUS__UVDIL_PWR_STATUS__SHIFT                                                             0xa
#define UVD_PGFSM_STATUS__UVDIR_PWR_STATUS__SHIFT                                                             0xc
#define UVD_PGFSM_STATUS__UVDTD_PWR_STATUS__SHIFT                                                             0xe
#define UVD_PGFSM_STATUS__UVDTE_PWR_STATUS__SHIFT                                                             0x10
#define UVD_PGFSM_STATUS__UVDE_PWR_STATUS__SHIFT                                                              0x12
#define UVD_PGFSM_STATUS__UVDW_PWR_STATUS__SHIFT                                                              0x14
#define UVD_PGFSM_STATUS__UVDJ_PWR_STATUS__SHIFT                                                              0x16
#define UVD_PGFSM_STATUS__UVDM_PWR_STATUS_MASK                                                                0x00000003L
#define UVD_PGFSM_STATUS__UVDU_PWR_STATUS_MASK                                                                0x0000000CL
#define UVD_PGFSM_STATUS__UVDF_PWR_STATUS_MASK                                                                0x00000030L
#define UVD_PGFSM_STATUS__UVDC_PWR_STATUS_MASK                                                                0x000000C0L
#define UVD_PGFSM_STATUS__UVDB_PWR_STATUS_MASK                                                                0x00000300L
#define UVD_PGFSM_STATUS__UVDIL_PWR_STATUS_MASK                                                               0x00000C00L
#define UVD_PGFSM_STATUS__UVDIR_PWR_STATUS_MASK                                                               0x00003000L
#define UVD_PGFSM_STATUS__UVDTD_PWR_STATUS_MASK                                                               0x0000C000L
#define UVD_PGFSM_STATUS__UVDTE_PWR_STATUS_MASK                                                               0x00030000L
#define UVD_PGFSM_STATUS__UVDE_PWR_STATUS_MASK                                                                0x000C0000L
#define UVD_PGFSM_STATUS__UVDW_PWR_STATUS_MASK                                                                0x00300000L
#define UVD_PGFSM_STATUS__UVDJ_PWR_STATUS_MASK                                                                0x00C00000L
//UVD_POWER_STATUS
#define UVD_POWER_STATUS__UVD_POWER_STATUS__SHIFT                                                             0x0
#define UVD_POWER_STATUS__UVD_PG_MODE__SHIFT                                                                  0x2
#define UVD_POWER_STATUS__UVD_CG_MODE__SHIFT                                                                  0x4
#define UVD_POWER_STATUS__UVD_PG_EN__SHIFT                                                                    0x8
#define UVD_POWER_STATUS__RBC_SNOOP_DIS__SHIFT                                                                0x9
#define UVD_POWER_STATUS__SW_RB_SNOOP_DIS__SHIFT                                                              0xb
#define UVD_POWER_STATUS__STALL_DPG_POWER_UP__SHIFT                                                           0x1f
#define UVD_POWER_STATUS__UVD_POWER_STATUS_MASK                                                               0x00000003L
#define UVD_POWER_STATUS__UVD_PG_MODE_MASK                                                                    0x00000004L
#define UVD_POWER_STATUS__UVD_CG_MODE_MASK                                                                    0x00000030L
#define UVD_POWER_STATUS__UVD_PG_EN_MASK                                                                      0x00000100L
#define UVD_POWER_STATUS__RBC_SNOOP_DIS_MASK                                                                  0x00000200L
#define UVD_POWER_STATUS__SW_RB_SNOOP_DIS_MASK                                                                0x00000800L
#define UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK                                                             0x80000000L
//UVD_PG_IND_INDEX
#define UVD_PG_IND_INDEX__INDEX__SHIFT                                                                        0x0
#define UVD_PG_IND_INDEX__INDEX_MASK                                                                          0x0000003FL
//UVD_PG_IND_DATA
#define UVD_PG_IND_DATA__DATA__SHIFT                                                                          0x0
#define UVD_PG_IND_DATA__DATA_MASK                                                                            0xFFFFFFFFL
//CC_UVD_HARVESTING
#define CC_UVD_HARVESTING__MMSCH_DISABLE__SHIFT                                                               0x0
#define CC_UVD_HARVESTING__UVD_DISABLE__SHIFT                                                                 0x1
#define CC_UVD_HARVESTING__MMSCH_DISABLE_MASK                                                                 0x00000001L
#define CC_UVD_HARVESTING__UVD_DISABLE_MASK                                                                   0x00000002L
//UVD_JPEG_POWER_STATUS
#define UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS__SHIFT                                                       0x0
#define UVD_JPEG_POWER_STATUS__JPEG_PG_MODE__SHIFT                                                            0x4
#define UVD_JPEG_POWER_STATUS__JRBC_DEC_SNOOP_DIS__SHIFT                                                      0x8
#define UVD_JPEG_POWER_STATUS__JRBC_ENC_SNOOP_DIS__SHIFT                                                      0x9
#define UVD_JPEG_POWER_STATUS__STALL_JDPG_POWER_UP__SHIFT                                                     0x1f
#define UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK                                                         0x00000001L
#define UVD_JPEG_POWER_STATUS__JPEG_PG_MODE_MASK                                                              0x00000010L
#define UVD_JPEG_POWER_STATUS__JRBC_DEC_SNOOP_DIS_MASK                                                        0x00000100L
#define UVD_JPEG_POWER_STATUS__JRBC_ENC_SNOOP_DIS_MASK                                                        0x00000200L
#define UVD_JPEG_POWER_STATUS__STALL_JDPG_POWER_UP_MASK                                                       0x80000000L
//UVD_DPG_LMA_CTL
#define UVD_DPG_LMA_CTL__READ_WRITE__SHIFT                                                                    0x0
#define UVD_DPG_LMA_CTL__MASK_EN__SHIFT                                                                       0x1
#define UVD_DPG_LMA_CTL__ADDR_AUTO_INCREMENT__SHIFT                                                           0x2
#define UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT                                                                      0x4
#define UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT                                                               0x10
#define UVD_DPG_LMA_CTL__READ_WRITE_MASK                                                                      0x00000001L
#define UVD_DPG_LMA_CTL__MASK_EN_MASK                                                                         0x00000002L
#define UVD_DPG_LMA_CTL__ADDR_AUTO_INCREMENT_MASK                                                             0x00000004L
#define UVD_DPG_LMA_CTL__SRAM_SEL_MASK                                                                        0x00000010L
#define UVD_DPG_LMA_CTL__READ_WRITE_ADDR_MASK                                                                 0xFFFF0000L
//UVD_DPG_LMA_DATA
#define UVD_DPG_LMA_DATA__LMA_DATA__SHIFT                                                                     0x0
#define UVD_DPG_LMA_DATA__LMA_DATA_MASK                                                                       0xFFFFFFFFL
//UVD_DPG_LMA_MASK
#define UVD_DPG_LMA_MASK__LMA_MASK__SHIFT                                                                     0x0
#define UVD_DPG_LMA_MASK__LMA_MASK_MASK                                                                       0xFFFFFFFFL
//UVD_DPG_PAUSE
#define UVD_DPG_PAUSE__JPEG_PAUSE_DPG_REQ__SHIFT                                                              0x0
#define UVD_DPG_PAUSE__JPEG_PAUSE_DPG_ACK__SHIFT                                                              0x1
#define UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ__SHIFT                                                                0x2
#define UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK__SHIFT                                                                0x3
#define UVD_DPG_PAUSE__JPEG_PAUSE_DPG_REQ_MASK                                                                0x00000001L
#define UVD_DPG_PAUSE__JPEG_PAUSE_DPG_ACK_MASK                                                                0x00000002L
#define UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK                                                                  0x00000004L
#define UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK                                                                  0x00000008L
//UVD_SCRATCH1
#define UVD_SCRATCH1__SCRATCH1_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH1__SCRATCH1_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH2
#define UVD_SCRATCH2__SCRATCH2_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH2__SCRATCH2_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH3
#define UVD_SCRATCH3__SCRATCH3_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH3__SCRATCH3_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH4
#define UVD_SCRATCH4__SCRATCH4_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH4__SCRATCH4_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH5
#define UVD_SCRATCH5__SCRATCH5_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH5__SCRATCH5_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH6
#define UVD_SCRATCH6__SCRATCH6_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH6__SCRATCH6_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH7
#define UVD_SCRATCH7__SCRATCH7_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH7__SCRATCH7_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH8
#define UVD_SCRATCH8__SCRATCH8_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH8__SCRATCH8_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH9
#define UVD_SCRATCH9__SCRATCH9_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH9__SCRATCH9_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH10
#define UVD_SCRATCH10__SCRATCH10_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH10__SCRATCH10_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH11
#define UVD_SCRATCH11__SCRATCH11_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH11__SCRATCH11_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH12
#define UVD_SCRATCH12__SCRATCH12_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH12__SCRATCH12_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH13
#define UVD_SCRATCH13__SCRATCH13_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH13__SCRATCH13_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH14
#define UVD_SCRATCH14__SCRATCH14_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH14__SCRATCH14_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_FREE_COUNTER_REG
#define UVD_FREE_COUNTER_REG__FREE_COUNTER__SHIFT                                                             0x0
#define UVD_FREE_COUNTER_REG__FREE_COUNTER_MASK                                                               0xFFFFFFFFL
//UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                0x0
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0_MASK                                                  0xFFFFFFFFL
//UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                              0x0
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                0xFFFFFFFFL
//UVD_DPG_VCPU_CACHE_OFFSET0
#define UVD_DPG_VCPU_CACHE_OFFSET0__CACHE_OFFSET0__SHIFT                                                      0x0
#define UVD_DPG_VCPU_CACHE_OFFSET0__CACHE_OFFSET0_MASK                                                        0x01FFFFFFL
//UVD_DPG_LMI_VCPU_CACHE_VMID
#define UVD_DPG_LMI_VCPU_CACHE_VMID__VCPU_CACHE_VMID__SHIFT                                                   0x0
#define UVD_DPG_LMI_VCPU_CACHE_VMID__VCPU_CACHE_VMID_MASK                                                     0x0000000FL
//UVD_PF_STATUS
#define UVD_PF_STATUS__JPEG_PF_OCCURED__SHIFT                                                                 0x0
#define UVD_PF_STATUS__NJ_PF_OCCURED__SHIFT                                                                   0x1
#define UVD_PF_STATUS__ENCODER0_PF_OCCURED__SHIFT                                                             0x2
#define UVD_PF_STATUS__ENCODER1_PF_OCCURED__SHIFT                                                             0x3
#define UVD_PF_STATUS__ENCODER2_PF_OCCURED__SHIFT                                                             0x4
#define UVD_PF_STATUS__ENCODER3_PF_OCCURED__SHIFT                                                             0x5
#define UVD_PF_STATUS__ENCODER4_PF_OCCURED__SHIFT                                                             0x6
#define UVD_PF_STATUS__EJPEG_PF_OCCURED__SHIFT                                                                0x7
#define UVD_PF_STATUS__JPEG_PF_CLEAR__SHIFT                                                                   0x8
#define UVD_PF_STATUS__NJ_PF_CLEAR__SHIFT                                                                     0x9
#define UVD_PF_STATUS__ENCODER0_PF_CLEAR__SHIFT                                                               0xa
#define UVD_PF_STATUS__ENCODER1_PF_CLEAR__SHIFT                                                               0xb
#define UVD_PF_STATUS__ENCODER2_PF_CLEAR__SHIFT                                                               0xc
#define UVD_PF_STATUS__ENCODER3_PF_CLEAR__SHIFT                                                               0xd
#define UVD_PF_STATUS__ENCODER4_PF_CLEAR__SHIFT                                                               0xe
#define UVD_PF_STATUS__EJPEG_PF_CLEAR__SHIFT                                                                  0xf
#define UVD_PF_STATUS__NJ_ATM_PF_OCCURED__SHIFT                                                               0x10
#define UVD_PF_STATUS__DJ_ATM_PF_OCCURED__SHIFT                                                               0x11
#define UVD_PF_STATUS__EJ_ATM_PF_OCCURED__SHIFT                                                               0x12
#define UVD_PF_STATUS__JPEG_PF_OCCURED_MASK                                                                   0x00000001L
#define UVD_PF_STATUS__NJ_PF_OCCURED_MASK                                                                     0x00000002L
#define UVD_PF_STATUS__ENCODER0_PF_OCCURED_MASK                                                               0x00000004L
#define UVD_PF_STATUS__ENCODER1_PF_OCCURED_MASK                                                               0x00000008L
#define UVD_PF_STATUS__ENCODER2_PF_OCCURED_MASK                                                               0x00000010L
#define UVD_PF_STATUS__ENCODER3_PF_OCCURED_MASK                                                               0x00000020L
#define UVD_PF_STATUS__ENCODER4_PF_OCCURED_MASK                                                               0x00000040L
#define UVD_PF_STATUS__EJPEG_PF_OCCURED_MASK                                                                  0x00000080L
#define UVD_PF_STATUS__JPEG_PF_CLEAR_MASK                                                                     0x00000100L
#define UVD_PF_STATUS__NJ_PF_CLEAR_MASK                                                                       0x00000200L
#define UVD_PF_STATUS__ENCODER0_PF_CLEAR_MASK                                                                 0x00000400L
#define UVD_PF_STATUS__ENCODER1_PF_CLEAR_MASK                                                                 0x00000800L
#define UVD_PF_STATUS__ENCODER2_PF_CLEAR_MASK                                                                 0x00001000L
#define UVD_PF_STATUS__ENCODER3_PF_CLEAR_MASK                                                                 0x00002000L
#define UVD_PF_STATUS__ENCODER4_PF_CLEAR_MASK                                                                 0x00004000L
#define UVD_PF_STATUS__EJPEG_PF_CLEAR_MASK                                                                    0x00008000L
#define UVD_PF_STATUS__NJ_ATM_PF_OCCURED_MASK                                                                 0x00010000L
#define UVD_PF_STATUS__DJ_ATM_PF_OCCURED_MASK                                                                 0x00020000L
#define UVD_PF_STATUS__EJ_ATM_PF_OCCURED_MASK                                                                 0x00040000L
//UVD_DPG_CLK_EN_VCPU_REPORT
#define UVD_DPG_CLK_EN_VCPU_REPORT__CLK_EN__SHIFT                                                             0x0
#define UVD_DPG_CLK_EN_VCPU_REPORT__VCPU_REPORT__SHIFT                                                        0x1
#define UVD_DPG_CLK_EN_VCPU_REPORT__CLK_EN_MASK                                                               0x00000001L
#define UVD_DPG_CLK_EN_VCPU_REPORT__VCPU_REPORT_MASK                                                          0x000000FEL
//UVD_GFX8_ADDR_CONFIG
#define UVD_GFX8_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                     0x4
#define UVD_GFX8_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                       0x00000070L
//UVD_GFX10_ADDR_CONFIG
#define UVD_GFX10_ADDR_CONFIG__NUM_PIPES__SHIFT                                                               0x0
#define UVD_GFX10_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                    0x3
#define UVD_GFX10_ADDR_CONFIG__NUM_BANKS__SHIFT                                                               0xc
#define UVD_GFX10_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                      0x13
#define UVD_GFX10_ADDR_CONFIG__NUM_PIPES_MASK                                                                 0x00000007L
#define UVD_GFX10_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                      0x00000038L
#define UVD_GFX10_ADDR_CONFIG__NUM_BANKS_MASK                                                                 0x00007000L
#define UVD_GFX10_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                        0x00180000L
//UVD_GPCNT2_CNTL
#define UVD_GPCNT2_CNTL__CLR__SHIFT                                                                           0x0
#define UVD_GPCNT2_CNTL__START__SHIFT                                                                         0x1
#define UVD_GPCNT2_CNTL__COUNTUP__SHIFT                                                                       0x2
#define UVD_GPCNT2_CNTL__CLR_MASK                                                                             0x00000001L
#define UVD_GPCNT2_CNTL__START_MASK                                                                           0x00000002L
#define UVD_GPCNT2_CNTL__COUNTUP_MASK                                                                         0x00000004L
//UVD_GPCNT2_TARGET_LOWER
#define UVD_GPCNT2_TARGET_LOWER__TARGET__SHIFT                                                                0x0
#define UVD_GPCNT2_TARGET_LOWER__TARGET_MASK                                                                  0xFFFFFFFFL
//UVD_GPCNT2_STATUS_LOWER
#define UVD_GPCNT2_STATUS_LOWER__COUNT__SHIFT                                                                 0x0
#define UVD_GPCNT2_STATUS_LOWER__COUNT_MASK                                                                   0xFFFFFFFFL
//UVD_GPCNT2_TARGET_UPPER
#define UVD_GPCNT2_TARGET_UPPER__TARGET__SHIFT                                                                0x0
#define UVD_GPCNT2_TARGET_UPPER__TARGET_MASK                                                                  0x0000FFFFL
//UVD_GPCNT2_STATUS_UPPER
#define UVD_GPCNT2_STATUS_UPPER__COUNT__SHIFT                                                                 0x0
#define UVD_GPCNT2_STATUS_UPPER__COUNT_MASK                                                                   0x0000FFFFL
//UVD_GPCNT3_CNTL
#define UVD_GPCNT3_CNTL__CLR__SHIFT                                                                           0x0
#define UVD_GPCNT3_CNTL__START__SHIFT                                                                         0x1
#define UVD_GPCNT3_CNTL__COUNTUP__SHIFT                                                                       0x2
#define UVD_GPCNT3_CNTL__FREQ__SHIFT                                                                          0x3
#define UVD_GPCNT3_CNTL__DIV__SHIFT                                                                           0xa
#define UVD_GPCNT3_CNTL__CLR_MASK                                                                             0x00000001L
#define UVD_GPCNT3_CNTL__START_MASK                                                                           0x00000002L
#define UVD_GPCNT3_CNTL__COUNTUP_MASK                                                                         0x00000004L
#define UVD_GPCNT3_CNTL__FREQ_MASK                                                                            0x000003F8L
#define UVD_GPCNT3_CNTL__DIV_MASK                                                                             0x0001FC00L
//UVD_GPCNT3_TARGET_LOWER
#define UVD_GPCNT3_TARGET_LOWER__TARGET__SHIFT                                                                0x0
#define UVD_GPCNT3_TARGET_LOWER__TARGET_MASK                                                                  0xFFFFFFFFL
//UVD_GPCNT3_STATUS_LOWER
#define UVD_GPCNT3_STATUS_LOWER__COUNT__SHIFT                                                                 0x0
#define UVD_GPCNT3_STATUS_LOWER__COUNT_MASK                                                                   0xFFFFFFFFL
//UVD_GPCNT3_TARGET_UPPER
#define UVD_GPCNT3_TARGET_UPPER__TARGET__SHIFT                                                                0x0
#define UVD_GPCNT3_TARGET_UPPER__TARGET_MASK                                                                  0x0000FFFFL
//UVD_GPCNT3_STATUS_UPPER
#define UVD_GPCNT3_STATUS_UPPER__COUNT__SHIFT                                                                 0x0
#define UVD_GPCNT3_STATUS_UPPER__COUNT_MASK                                                                   0x0000FFFFL


// addressBlock: uvd0_uvddec
//UVD_STATUS
#define UVD_STATUS__RBC_BUSY__SHIFT                                                                           0x0
#define UVD_STATUS__VCPU_REPORT__SHIFT                                                                        0x1
#define UVD_STATUS__RBC_ACCESS_GPCOM__SHIFT                                                                   0x10
#define UVD_STATUS__SYS_GPCOM_REQ__SHIFT                                                                      0x1f
#define UVD_STATUS__RBC_BUSY_MASK                                                                             0x00000001L
#define UVD_STATUS__VCPU_REPORT_MASK                                                                          0x000000FEL
#define UVD_STATUS__RBC_ACCESS_GPCOM_MASK                                                                     0x00010000L
#define UVD_STATUS__SYS_GPCOM_REQ_MASK                                                                        0x80000000L
//UVD_ENC_PIPE_BUSY
#define UVD_ENC_PIPE_BUSY__IME_BUSY__SHIFT                                                                    0x0
#define UVD_ENC_PIPE_BUSY__SMP_BUSY__SHIFT                                                                    0x1
#define UVD_ENC_PIPE_BUSY__SIT_BUSY__SHIFT                                                                    0x2
#define UVD_ENC_PIPE_BUSY__SDB_BUSY__SHIFT                                                                    0x3
#define UVD_ENC_PIPE_BUSY__ENT_BUSY__SHIFT                                                                    0x4
#define UVD_ENC_PIPE_BUSY__ENT_HEADER_BUSY__SHIFT                                                             0x5
#define UVD_ENC_PIPE_BUSY__LCM_BUSY__SHIFT                                                                    0x6
#define UVD_ENC_PIPE_BUSY__MDM_RD_CUR_BUSY__SHIFT                                                             0x7
#define UVD_ENC_PIPE_BUSY__MDM_RD_REF_BUSY__SHIFT                                                             0x8
#define UVD_ENC_PIPE_BUSY__MDM_RD_GEN_BUSY__SHIFT                                                             0x9
#define UVD_ENC_PIPE_BUSY__MDM_WR_RECON_BUSY__SHIFT                                                           0xa
#define UVD_ENC_PIPE_BUSY__MDM_WR_GEN_BUSY__SHIFT                                                             0xb
#define UVD_ENC_PIPE_BUSY__MIF_RD_CUR_BUSY__SHIFT                                                             0x10
#define UVD_ENC_PIPE_BUSY__MIF_RD_REF0_BUSY__SHIFT                                                            0x11
#define UVD_ENC_PIPE_BUSY__MIF_WR_GEN0_BUSY__SHIFT                                                            0x12
#define UVD_ENC_PIPE_BUSY__MIF_RD_GEN0_BUSY__SHIFT                                                            0x13
#define UVD_ENC_PIPE_BUSY__MIF_WR_GEN1_BUSY__SHIFT                                                            0x14
#define UVD_ENC_PIPE_BUSY__MIF_RD_GEN1_BUSY__SHIFT                                                            0x15
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP0_BUSY__SHIFT                                                            0x16
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP1_BUSY__SHIFT                                                            0x17
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD0_BUSY__SHIFT                                                            0x18
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD1_BUSY__SHIFT                                                            0x19
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD2_BUSY__SHIFT                                                            0x1a
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD3_BUSY__SHIFT                                                            0x1b
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD4_BUSY__SHIFT                                                            0x1c
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP2_BUSY__SHIFT                                                            0x1d
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP3_BUSY__SHIFT                                                            0x1e
#define UVD_ENC_PIPE_BUSY__IME_BUSY_MASK                                                                      0x00000001L
#define UVD_ENC_PIPE_BUSY__SMP_BUSY_MASK                                                                      0x00000002L
#define UVD_ENC_PIPE_BUSY__SIT_BUSY_MASK                                                                      0x00000004L
#define UVD_ENC_PIPE_BUSY__SDB_BUSY_MASK                                                                      0x00000008L
#define UVD_ENC_PIPE_BUSY__ENT_BUSY_MASK                                                                      0x00000010L
#define UVD_ENC_PIPE_BUSY__ENT_HEADER_BUSY_MASK                                                               0x00000020L
#define UVD_ENC_PIPE_BUSY__LCM_BUSY_MASK                                                                      0x00000040L
#define UVD_ENC_PIPE_BUSY__MDM_RD_CUR_BUSY_MASK                                                               0x00000080L
#define UVD_ENC_PIPE_BUSY__MDM_RD_REF_BUSY_MASK                                                               0x00000100L
#define UVD_ENC_PIPE_BUSY__MDM_RD_GEN_BUSY_MASK                                                               0x00000200L
#define UVD_ENC_PIPE_BUSY__MDM_WR_RECON_BUSY_MASK                                                             0x00000400L
#define UVD_ENC_PIPE_BUSY__MDM_WR_GEN_BUSY_MASK                                                               0x00000800L
#define UVD_ENC_PIPE_BUSY__MIF_RD_CUR_BUSY_MASK                                                               0x00010000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_REF0_BUSY_MASK                                                              0x00020000L
#define UVD_ENC_PIPE_BUSY__MIF_WR_GEN0_BUSY_MASK                                                              0x00040000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_GEN0_BUSY_MASK                                                              0x00080000L
#define UVD_ENC_PIPE_BUSY__MIF_WR_GEN1_BUSY_MASK                                                              0x00100000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_GEN1_BUSY_MASK                                                              0x00200000L
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP0_BUSY_MASK                                                              0x00400000L
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP1_BUSY_MASK                                                              0x00800000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD0_BUSY_MASK                                                              0x01000000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD1_BUSY_MASK                                                              0x02000000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD2_BUSY_MASK                                                              0x04000000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD3_BUSY_MASK                                                              0x08000000L
#define UVD_ENC_PIPE_BUSY__MIF_RD_BSD4_BUSY_MASK                                                              0x10000000L
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP2_BUSY_MASK                                                              0x20000000L
#define UVD_ENC_PIPE_BUSY__MIF_WR_BSP3_BUSY_MASK                                                              0x40000000L
//UVD_SOFT_RESET
#define UVD_SOFT_RESET__RBC_SOFT_RESET__SHIFT                                                                 0x0
#define UVD_SOFT_RESET__LBSI_SOFT_RESET__SHIFT                                                                0x1
#define UVD_SOFT_RESET__LMI_SOFT_RESET__SHIFT                                                                 0x2
#define UVD_SOFT_RESET__VCPU_SOFT_RESET__SHIFT                                                                0x3
#define UVD_SOFT_RESET__UDEC_SOFT_RESET__SHIFT                                                                0x4
#define UVD_SOFT_RESET__CXW_SOFT_RESET__SHIFT                                                                 0x6
#define UVD_SOFT_RESET__TAP_SOFT_RESET__SHIFT                                                                 0x7
#define UVD_SOFT_RESET__MPC_SOFT_RESET__SHIFT                                                                 0x8
#define UVD_SOFT_RESET__EFC_SOFT_RESET__SHIFT                                                                 0x9
#define UVD_SOFT_RESET__IH_SOFT_RESET__SHIFT                                                                  0xa
#define UVD_SOFT_RESET__MPRD_SOFT_RESET__SHIFT                                                                0xb
#define UVD_SOFT_RESET__IDCT_SOFT_RESET__SHIFT                                                                0xc
#define UVD_SOFT_RESET__LMI_UMC_SOFT_RESET__SHIFT                                                             0xd
#define UVD_SOFT_RESET__SPH_SOFT_RESET__SHIFT                                                                 0xe
#define UVD_SOFT_RESET__MIF_SOFT_RESET__SHIFT                                                                 0xf
#define UVD_SOFT_RESET__LCM_SOFT_RESET__SHIFT                                                                 0x10
#define UVD_SOFT_RESET__SUVD_SOFT_RESET__SHIFT                                                                0x11
#define UVD_SOFT_RESET__LBSI_VCLK_RESET_STATUS__SHIFT                                                         0x12
#define UVD_SOFT_RESET__VCPU_VCLK_RESET_STATUS__SHIFT                                                         0x13
#define UVD_SOFT_RESET__UDEC_VCLK_RESET_STATUS__SHIFT                                                         0x14
#define UVD_SOFT_RESET__UDEC_DCLK_RESET_STATUS__SHIFT                                                         0x15
#define UVD_SOFT_RESET__MPC_DCLK_RESET_STATUS__SHIFT                                                          0x16
#define UVD_SOFT_RESET__MPRD_VCLK_RESET_STATUS__SHIFT                                                         0x17
#define UVD_SOFT_RESET__MPRD_DCLK_RESET_STATUS__SHIFT                                                         0x18
#define UVD_SOFT_RESET__IDCT_VCLK_RESET_STATUS__SHIFT                                                         0x19
#define UVD_SOFT_RESET__MIF_DCLK_RESET_STATUS__SHIFT                                                          0x1a
#define UVD_SOFT_RESET__LCM_DCLK_RESET_STATUS__SHIFT                                                          0x1b
#define UVD_SOFT_RESET__SUVD_VCLK_RESET_STATUS__SHIFT                                                         0x1c
#define UVD_SOFT_RESET__SUVD_DCLK_RESET_STATUS__SHIFT                                                         0x1d
#define UVD_SOFT_RESET__RE_DCLK_RESET_STATUS__SHIFT                                                           0x1e
#define UVD_SOFT_RESET__SRE_DCLK_RESET_STATUS__SHIFT                                                          0x1f
#define UVD_SOFT_RESET__RBC_SOFT_RESET_MASK                                                                   0x00000001L
#define UVD_SOFT_RESET__LBSI_SOFT_RESET_MASK                                                                  0x00000002L
#define UVD_SOFT_RESET__LMI_SOFT_RESET_MASK                                                                   0x00000004L
#define UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK                                                                  0x00000008L
#define UVD_SOFT_RESET__UDEC_SOFT_RESET_MASK                                                                  0x00000010L
#define UVD_SOFT_RESET__CXW_SOFT_RESET_MASK                                                                   0x00000040L
#define UVD_SOFT_RESET__TAP_SOFT_RESET_MASK                                                                   0x00000080L
#define UVD_SOFT_RESET__MPC_SOFT_RESET_MASK                                                                   0x00000100L
#define UVD_SOFT_RESET__EFC_SOFT_RESET_MASK                                                                   0x00000200L
#define UVD_SOFT_RESET__IH_SOFT_RESET_MASK                                                                    0x00000400L
#define UVD_SOFT_RESET__MPRD_SOFT_RESET_MASK                                                                  0x00000800L
#define UVD_SOFT_RESET__IDCT_SOFT_RESET_MASK                                                                  0x00001000L
#define UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK                                                               0x00002000L
#define UVD_SOFT_RESET__SPH_SOFT_RESET_MASK                                                                   0x00004000L
#define UVD_SOFT_RESET__MIF_SOFT_RESET_MASK                                                                   0x00008000L
#define UVD_SOFT_RESET__LCM_SOFT_RESET_MASK                                                                   0x00010000L
#define UVD_SOFT_RESET__SUVD_SOFT_RESET_MASK                                                                  0x00020000L
#define UVD_SOFT_RESET__LBSI_VCLK_RESET_STATUS_MASK                                                           0x00040000L
#define UVD_SOFT_RESET__VCPU_VCLK_RESET_STATUS_MASK                                                           0x00080000L
#define UVD_SOFT_RESET__UDEC_VCLK_RESET_STATUS_MASK                                                           0x00100000L
#define UVD_SOFT_RESET__UDEC_DCLK_RESET_STATUS_MASK                                                           0x00200000L
#define UVD_SOFT_RESET__MPC_DCLK_RESET_STATUS_MASK                                                            0x00400000L
#define UVD_SOFT_RESET__MPRD_VCLK_RESET_STATUS_MASK                                                           0x00800000L
#define UVD_SOFT_RESET__MPRD_DCLK_RESET_STATUS_MASK                                                           0x01000000L
#define UVD_SOFT_RESET__IDCT_VCLK_RESET_STATUS_MASK                                                           0x02000000L
#define UVD_SOFT_RESET__MIF_DCLK_RESET_STATUS_MASK                                                            0x04000000L
#define UVD_SOFT_RESET__LCM_DCLK_RESET_STATUS_MASK                                                            0x08000000L
#define UVD_SOFT_RESET__SUVD_VCLK_RESET_STATUS_MASK                                                           0x10000000L
#define UVD_SOFT_RESET__SUVD_DCLK_RESET_STATUS_MASK                                                           0x20000000L
#define UVD_SOFT_RESET__RE_DCLK_RESET_STATUS_MASK                                                             0x40000000L
#define UVD_SOFT_RESET__SRE_DCLK_RESET_STATUS_MASK                                                            0x80000000L
//UVD_SOFT_RESET2
#define UVD_SOFT_RESET2__ATOMIC_SOFT_RESET__SHIFT                                                             0x0
#define UVD_SOFT_RESET2__MMSCH_VCLK_RESET_STATUS__SHIFT                                                       0x10
#define UVD_SOFT_RESET2__MMSCH_SCLK_RESET_STATUS__SHIFT                                                       0x11
#define UVD_SOFT_RESET2__ATOMIC_SOFT_RESET_MASK                                                               0x00000001L
#define UVD_SOFT_RESET2__MMSCH_VCLK_RESET_STATUS_MASK                                                         0x00010000L
#define UVD_SOFT_RESET2__MMSCH_SCLK_RESET_STATUS_MASK                                                         0x00020000L
//UVD_MMSCH_SOFT_RESET
#define UVD_MMSCH_SOFT_RESET__MMSCH_RESET__SHIFT                                                              0x0
#define UVD_MMSCH_SOFT_RESET__TAP_SOFT_RESET__SHIFT                                                           0x1
#define UVD_MMSCH_SOFT_RESET__MMSCH_LOCK__SHIFT                                                               0x1f
#define UVD_MMSCH_SOFT_RESET__MMSCH_RESET_MASK                                                                0x00000001L
#define UVD_MMSCH_SOFT_RESET__TAP_SOFT_RESET_MASK                                                             0x00000002L
#define UVD_MMSCH_SOFT_RESET__MMSCH_LOCK_MASK                                                                 0x80000000L
//UVD_CGC_GATE
#define UVD_CGC_GATE__SYS__SHIFT                                                                              0x0
#define UVD_CGC_GATE__UDEC__SHIFT                                                                             0x1
#define UVD_CGC_GATE__MPEG2__SHIFT                                                                            0x2
#define UVD_CGC_GATE__REGS__SHIFT                                                                             0x3
#define UVD_CGC_GATE__RBC__SHIFT                                                                              0x4
#define UVD_CGC_GATE__LMI_MC__SHIFT                                                                           0x5
#define UVD_CGC_GATE__LMI_UMC__SHIFT                                                                          0x6
#define UVD_CGC_GATE__IDCT__SHIFT                                                                             0x7
#define UVD_CGC_GATE__MPRD__SHIFT                                                                             0x8
#define UVD_CGC_GATE__MPC__SHIFT                                                                              0x9
#define UVD_CGC_GATE__LBSI__SHIFT                                                                             0xa
#define UVD_CGC_GATE__LRBBM__SHIFT                                                                            0xb
#define UVD_CGC_GATE__UDEC_RE__SHIFT                                                                          0xc
#define UVD_CGC_GATE__UDEC_CM__SHIFT                                                                          0xd
#define UVD_CGC_GATE__UDEC_IT__SHIFT                                                                          0xe
#define UVD_CGC_GATE__UDEC_DB__SHIFT                                                                          0xf
#define UVD_CGC_GATE__UDEC_MP__SHIFT                                                                          0x10
#define UVD_CGC_GATE__WCB__SHIFT                                                                              0x11
#define UVD_CGC_GATE__VCPU__SHIFT                                                                             0x12
#define UVD_CGC_GATE__MMSCH__SHIFT                                                                            0x14
#define UVD_CGC_GATE__SYS_MASK                                                                                0x00000001L
#define UVD_CGC_GATE__UDEC_MASK                                                                               0x00000002L
#define UVD_CGC_GATE__MPEG2_MASK                                                                              0x00000004L
#define UVD_CGC_GATE__REGS_MASK                                                                               0x00000008L
#define UVD_CGC_GATE__RBC_MASK                                                                                0x00000010L
#define UVD_CGC_GATE__LMI_MC_MASK                                                                             0x00000020L
#define UVD_CGC_GATE__LMI_UMC_MASK                                                                            0x00000040L
#define UVD_CGC_GATE__IDCT_MASK                                                                               0x00000080L
#define UVD_CGC_GATE__MPRD_MASK                                                                               0x00000100L
#define UVD_CGC_GATE__MPC_MASK                                                                                0x00000200L
#define UVD_CGC_GATE__LBSI_MASK                                                                               0x00000400L
#define UVD_CGC_GATE__LRBBM_MASK                                                                              0x00000800L
#define UVD_CGC_GATE__UDEC_RE_MASK                                                                            0x00001000L
#define UVD_CGC_GATE__UDEC_CM_MASK                                                                            0x00002000L
#define UVD_CGC_GATE__UDEC_IT_MASK                                                                            0x00004000L
#define UVD_CGC_GATE__UDEC_DB_MASK                                                                            0x00008000L
#define UVD_CGC_GATE__UDEC_MP_MASK                                                                            0x00010000L
#define UVD_CGC_GATE__WCB_MASK                                                                                0x00020000L
#define UVD_CGC_GATE__VCPU_MASK                                                                               0x00040000L
#define UVD_CGC_GATE__MMSCH_MASK                                                                              0x00100000L
//UVD_CGC_STATUS
#define UVD_CGC_STATUS__SYS_SCLK__SHIFT                                                                       0x0
#define UVD_CGC_STATUS__SYS_DCLK__SHIFT                                                                       0x1
#define UVD_CGC_STATUS__SYS_VCLK__SHIFT                                                                       0x2
#define UVD_CGC_STATUS__UDEC_SCLK__SHIFT                                                                      0x3
#define UVD_CGC_STATUS__UDEC_DCLK__SHIFT                                                                      0x4
#define UVD_CGC_STATUS__UDEC_VCLK__SHIFT                                                                      0x5
#define UVD_CGC_STATUS__MPEG2_SCLK__SHIFT                                                                     0x6
#define UVD_CGC_STATUS__MPEG2_DCLK__SHIFT                                                                     0x7
#define UVD_CGC_STATUS__MPEG2_VCLK__SHIFT                                                                     0x8
#define UVD_CGC_STATUS__REGS_SCLK__SHIFT                                                                      0x9
#define UVD_CGC_STATUS__REGS_VCLK__SHIFT                                                                      0xa
#define UVD_CGC_STATUS__RBC_SCLK__SHIFT                                                                       0xb
#define UVD_CGC_STATUS__LMI_MC_SCLK__SHIFT                                                                    0xc
#define UVD_CGC_STATUS__LMI_UMC_SCLK__SHIFT                                                                   0xd
#define UVD_CGC_STATUS__IDCT_SCLK__SHIFT                                                                      0xe
#define UVD_CGC_STATUS__IDCT_VCLK__SHIFT                                                                      0xf
#define UVD_CGC_STATUS__MPRD_SCLK__SHIFT                                                                      0x10
#define UVD_CGC_STATUS__MPRD_DCLK__SHIFT                                                                      0x11
#define UVD_CGC_STATUS__MPRD_VCLK__SHIFT                                                                      0x12
#define UVD_CGC_STATUS__MPC_SCLK__SHIFT                                                                       0x13
#define UVD_CGC_STATUS__MPC_DCLK__SHIFT                                                                       0x14
#define UVD_CGC_STATUS__LBSI_SCLK__SHIFT                                                                      0x15
#define UVD_CGC_STATUS__LBSI_VCLK__SHIFT                                                                      0x16
#define UVD_CGC_STATUS__LRBBM_SCLK__SHIFT                                                                     0x17
#define UVD_CGC_STATUS__WCB_SCLK__SHIFT                                                                       0x18
#define UVD_CGC_STATUS__VCPU_SCLK__SHIFT                                                                      0x19
#define UVD_CGC_STATUS__VCPU_VCLK__SHIFT                                                                      0x1a
#define UVD_CGC_STATUS__MMSCH_SCLK__SHIFT                                                                     0x1b
#define UVD_CGC_STATUS__MMSCH_VCLK__SHIFT                                                                     0x1c
#define UVD_CGC_STATUS__ALL_ENC_ACTIVE__SHIFT                                                                 0x1d
#define UVD_CGC_STATUS__ALL_DEC_ACTIVE__SHIFT                                                                 0x1f
#define UVD_CGC_STATUS__SYS_SCLK_MASK                                                                         0x00000001L
#define UVD_CGC_STATUS__SYS_DCLK_MASK                                                                         0x00000002L
#define UVD_CGC_STATUS__SYS_VCLK_MASK                                                                         0x00000004L
#define UVD_CGC_STATUS__UDEC_SCLK_MASK                                                                        0x00000008L
#define UVD_CGC_STATUS__UDEC_DCLK_MASK                                                                        0x00000010L
#define UVD_CGC_STATUS__UDEC_VCLK_MASK                                                                        0x00000020L
#define UVD_CGC_STATUS__MPEG2_SCLK_MASK                                                                       0x00000040L
#define UVD_CGC_STATUS__MPEG2_DCLK_MASK                                                                       0x00000080L
#define UVD_CGC_STATUS__MPEG2_VCLK_MASK                                                                       0x00000100L
#define UVD_CGC_STATUS__REGS_SCLK_MASK                                                                        0x00000200L
#define UVD_CGC_STATUS__REGS_VCLK_MASK                                                                        0x00000400L
#define UVD_CGC_STATUS__RBC_SCLK_MASK                                                                         0x00000800L
#define UVD_CGC_STATUS__LMI_MC_SCLK_MASK                                                                      0x00001000L
#define UVD_CGC_STATUS__LMI_UMC_SCLK_MASK                                                                     0x00002000L
#define UVD_CGC_STATUS__IDCT_SCLK_MASK                                                                        0x00004000L
#define UVD_CGC_STATUS__IDCT_VCLK_MASK                                                                        0x00008000L
#define UVD_CGC_STATUS__MPRD_SCLK_MASK                                                                        0x00010000L
#define UVD_CGC_STATUS__MPRD_DCLK_MASK                                                                        0x00020000L
#define UVD_CGC_STATUS__MPRD_VCLK_MASK                                                                        0x00040000L
#define UVD_CGC_STATUS__MPC_SCLK_MASK                                                                         0x00080000L
#define UVD_CGC_STATUS__MPC_DCLK_MASK                                                                         0x00100000L
#define UVD_CGC_STATUS__LBSI_SCLK_MASK                                                                        0x00200000L
#define UVD_CGC_STATUS__LBSI_VCLK_MASK                                                                        0x00400000L
#define UVD_CGC_STATUS__LRBBM_SCLK_MASK                                                                       0x00800000L
#define UVD_CGC_STATUS__WCB_SCLK_MASK                                                                         0x01000000L
#define UVD_CGC_STATUS__VCPU_SCLK_MASK                                                                        0x02000000L
#define UVD_CGC_STATUS__VCPU_VCLK_MASK                                                                        0x04000000L
#define UVD_CGC_STATUS__MMSCH_SCLK_MASK                                                                       0x08000000L
#define UVD_CGC_STATUS__MMSCH_VCLK_MASK                                                                       0x10000000L
#define UVD_CGC_STATUS__ALL_ENC_ACTIVE_MASK                                                                   0x20000000L
#define UVD_CGC_STATUS__ALL_DEC_ACTIVE_MASK                                                                   0x80000000L
//UVD_CGC_CTRL
#define UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT                                                                   0x0
#define UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT                                                               0x2
#define UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT                                                                    0x6
#define UVD_CGC_CTRL__UDEC_RE_MODE__SHIFT                                                                     0xb
#define UVD_CGC_CTRL__UDEC_CM_MODE__SHIFT                                                                     0xc
#define UVD_CGC_CTRL__UDEC_IT_MODE__SHIFT                                                                     0xd
#define UVD_CGC_CTRL__UDEC_DB_MODE__SHIFT                                                                     0xe
#define UVD_CGC_CTRL__UDEC_MP_MODE__SHIFT                                                                     0xf
#define UVD_CGC_CTRL__SYS_MODE__SHIFT                                                                         0x10
#define UVD_CGC_CTRL__UDEC_MODE__SHIFT                                                                        0x11
#define UVD_CGC_CTRL__MPEG2_MODE__SHIFT                                                                       0x12
#define UVD_CGC_CTRL__REGS_MODE__SHIFT                                                                        0x13
#define UVD_CGC_CTRL__RBC_MODE__SHIFT                                                                         0x14
#define UVD_CGC_CTRL__LMI_MC_MODE__SHIFT                                                                      0x15
#define UVD_CGC_CTRL__LMI_UMC_MODE__SHIFT                                                                     0x16
#define UVD_CGC_CTRL__IDCT_MODE__SHIFT                                                                        0x17
#define UVD_CGC_CTRL__MPRD_MODE__SHIFT                                                                        0x18
#define UVD_CGC_CTRL__MPC_MODE__SHIFT                                                                         0x19
#define UVD_CGC_CTRL__LBSI_MODE__SHIFT                                                                        0x1a
#define UVD_CGC_CTRL__LRBBM_MODE__SHIFT                                                                       0x1b
#define UVD_CGC_CTRL__WCB_MODE__SHIFT                                                                         0x1c
#define UVD_CGC_CTRL__VCPU_MODE__SHIFT                                                                        0x1d
#define UVD_CGC_CTRL__MMSCH_MODE__SHIFT                                                                       0x1f
#define UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK                                                                     0x00000001L
#define UVD_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK                                                                 0x0000003CL
#define UVD_CGC_CTRL__CLK_OFF_DELAY_MASK                                                                      0x000007C0L
#define UVD_CGC_CTRL__UDEC_RE_MODE_MASK                                                                       0x00000800L
#define UVD_CGC_CTRL__UDEC_CM_MODE_MASK                                                                       0x00001000L
#define UVD_CGC_CTRL__UDEC_IT_MODE_MASK                                                                       0x00002000L
#define UVD_CGC_CTRL__UDEC_DB_MODE_MASK                                                                       0x00004000L
#define UVD_CGC_CTRL__UDEC_MP_MODE_MASK                                                                       0x00008000L
#define UVD_CGC_CTRL__SYS_MODE_MASK                                                                           0x00010000L
#define UVD_CGC_CTRL__UDEC_MODE_MASK                                                                          0x00020000L
#define UVD_CGC_CTRL__MPEG2_MODE_MASK                                                                         0x00040000L
#define UVD_CGC_CTRL__REGS_MODE_MASK                                                                          0x00080000L
#define UVD_CGC_CTRL__RBC_MODE_MASK                                                                           0x00100000L
#define UVD_CGC_CTRL__LMI_MC_MODE_MASK                                                                        0x00200000L
#define UVD_CGC_CTRL__LMI_UMC_MODE_MASK                                                                       0x00400000L
#define UVD_CGC_CTRL__IDCT_MODE_MASK                                                                          0x00800000L
#define UVD_CGC_CTRL__MPRD_MODE_MASK                                                                          0x01000000L
#define UVD_CGC_CTRL__MPC_MODE_MASK                                                                           0x02000000L
#define UVD_CGC_CTRL__LBSI_MODE_MASK                                                                          0x04000000L
#define UVD_CGC_CTRL__LRBBM_MODE_MASK                                                                         0x08000000L
#define UVD_CGC_CTRL__WCB_MODE_MASK                                                                           0x10000000L
#define UVD_CGC_CTRL__VCPU_MODE_MASK                                                                          0x20000000L
#define UVD_CGC_CTRL__MMSCH_MODE_MASK                                                                         0x80000000L
//UVD_CGC_UDEC_STATUS
#define UVD_CGC_UDEC_STATUS__RE_SCLK__SHIFT                                                                   0x0
#define UVD_CGC_UDEC_STATUS__RE_DCLK__SHIFT                                                                   0x1
#define UVD_CGC_UDEC_STATUS__RE_VCLK__SHIFT                                                                   0x2
#define UVD_CGC_UDEC_STATUS__CM_SCLK__SHIFT                                                                   0x3
#define UVD_CGC_UDEC_STATUS__CM_DCLK__SHIFT                                                                   0x4
#define UVD_CGC_UDEC_STATUS__CM_VCLK__SHIFT                                                                   0x5
#define UVD_CGC_UDEC_STATUS__IT_SCLK__SHIFT                                                                   0x6
#define UVD_CGC_UDEC_STATUS__IT_DCLK__SHIFT                                                                   0x7
#define UVD_CGC_UDEC_STATUS__IT_VCLK__SHIFT                                                                   0x8
#define UVD_CGC_UDEC_STATUS__DB_SCLK__SHIFT                                                                   0x9
#define UVD_CGC_UDEC_STATUS__DB_DCLK__SHIFT                                                                   0xa
#define UVD_CGC_UDEC_STATUS__DB_VCLK__SHIFT                                                                   0xb
#define UVD_CGC_UDEC_STATUS__MP_SCLK__SHIFT                                                                   0xc
#define UVD_CGC_UDEC_STATUS__MP_DCLK__SHIFT                                                                   0xd
#define UVD_CGC_UDEC_STATUS__MP_VCLK__SHIFT                                                                   0xe
#define UVD_CGC_UDEC_STATUS__RE_SCLK_MASK                                                                     0x00000001L
#define UVD_CGC_UDEC_STATUS__RE_DCLK_MASK                                                                     0x00000002L
#define UVD_CGC_UDEC_STATUS__RE_VCLK_MASK                                                                     0x00000004L
#define UVD_CGC_UDEC_STATUS__CM_SCLK_MASK                                                                     0x00000008L
#define UVD_CGC_UDEC_STATUS__CM_DCLK_MASK                                                                     0x00000010L
#define UVD_CGC_UDEC_STATUS__CM_VCLK_MASK                                                                     0x00000020L
#define UVD_CGC_UDEC_STATUS__IT_SCLK_MASK                                                                     0x00000040L
#define UVD_CGC_UDEC_STATUS__IT_DCLK_MASK                                                                     0x00000080L
#define UVD_CGC_UDEC_STATUS__IT_VCLK_MASK                                                                     0x00000100L
#define UVD_CGC_UDEC_STATUS__DB_SCLK_MASK                                                                     0x00000200L
#define UVD_CGC_UDEC_STATUS__DB_DCLK_MASK                                                                     0x00000400L
#define UVD_CGC_UDEC_STATUS__DB_VCLK_MASK                                                                     0x00000800L
#define UVD_CGC_UDEC_STATUS__MP_SCLK_MASK                                                                     0x00001000L
#define UVD_CGC_UDEC_STATUS__MP_DCLK_MASK                                                                     0x00002000L
#define UVD_CGC_UDEC_STATUS__MP_VCLK_MASK                                                                     0x00004000L
//UVD_SUVD_CGC_GATE
#define UVD_SUVD_CGC_GATE__SRE__SHIFT                                                                         0x0
#define UVD_SUVD_CGC_GATE__SIT__SHIFT                                                                         0x1
#define UVD_SUVD_CGC_GATE__SMP__SHIFT                                                                         0x2
#define UVD_SUVD_CGC_GATE__SCM__SHIFT                                                                         0x3
#define UVD_SUVD_CGC_GATE__SDB__SHIFT                                                                         0x4
#define UVD_SUVD_CGC_GATE__SRE_H264__SHIFT                                                                    0x5
#define UVD_SUVD_CGC_GATE__SRE_HEVC__SHIFT                                                                    0x6
#define UVD_SUVD_CGC_GATE__SIT_H264__SHIFT                                                                    0x7
#define UVD_SUVD_CGC_GATE__SIT_HEVC__SHIFT                                                                    0x8
#define UVD_SUVD_CGC_GATE__SCM_H264__SHIFT                                                                    0x9
#define UVD_SUVD_CGC_GATE__SCM_HEVC__SHIFT                                                                    0xa
#define UVD_SUVD_CGC_GATE__SDB_H264__SHIFT                                                                    0xb
#define UVD_SUVD_CGC_GATE__SDB_HEVC__SHIFT                                                                    0xc
#define UVD_SUVD_CGC_GATE__SCLR__SHIFT                                                                        0xd
#define UVD_SUVD_CGC_GATE__UVD_SC__SHIFT                                                                      0xe
#define UVD_SUVD_CGC_GATE__ENT__SHIFT                                                                         0xf
#define UVD_SUVD_CGC_GATE__IME__SHIFT                                                                         0x10
#define UVD_SUVD_CGC_GATE__SIT_HEVC_DEC__SHIFT                                                                0x11
#define UVD_SUVD_CGC_GATE__SIT_HEVC_ENC__SHIFT                                                                0x12
#define UVD_SUVD_CGC_GATE__SITE__SHIFT                                                                        0x13
#define UVD_SUVD_CGC_GATE__SRE_VP9__SHIFT                                                                     0x14
#define UVD_SUVD_CGC_GATE__SCM_VP9__SHIFT                                                                     0x15
#define UVD_SUVD_CGC_GATE__SIT_VP9_DEC__SHIFT                                                                 0x16
#define UVD_SUVD_CGC_GATE__SDB_VP9__SHIFT                                                                     0x17
#define UVD_SUVD_CGC_GATE__IME_HEVC__SHIFT                                                                    0x18
#define UVD_SUVD_CGC_GATE__EFC__SHIFT                                                                         0x19
#define UVD_SUVD_CGC_GATE__SRE_MASK                                                                           0x00000001L
#define UVD_SUVD_CGC_GATE__SIT_MASK                                                                           0x00000002L
#define UVD_SUVD_CGC_GATE__SMP_MASK                                                                           0x00000004L
#define UVD_SUVD_CGC_GATE__SCM_MASK                                                                           0x00000008L
#define UVD_SUVD_CGC_GATE__SDB_MASK                                                                           0x00000010L
#define UVD_SUVD_CGC_GATE__SRE_H264_MASK                                                                      0x00000020L
#define UVD_SUVD_CGC_GATE__SRE_HEVC_MASK                                                                      0x00000040L
#define UVD_SUVD_CGC_GATE__SIT_H264_MASK                                                                      0x00000080L
#define UVD_SUVD_CGC_GATE__SIT_HEVC_MASK                                                                      0x00000100L
#define UVD_SUVD_CGC_GATE__SCM_H264_MASK                                                                      0x00000200L
#define UVD_SUVD_CGC_GATE__SCM_HEVC_MASK                                                                      0x00000400L
#define UVD_SUVD_CGC_GATE__SDB_H264_MASK                                                                      0x00000800L
#define UVD_SUVD_CGC_GATE__SDB_HEVC_MASK                                                                      0x00001000L
#define UVD_SUVD_CGC_GATE__SCLR_MASK                                                                          0x00002000L
#define UVD_SUVD_CGC_GATE__UVD_SC_MASK                                                                        0x00004000L
#define UVD_SUVD_CGC_GATE__ENT_MASK                                                                           0x00008000L
#define UVD_SUVD_CGC_GATE__IME_MASK                                                                           0x00010000L
#define UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK                                                                  0x00020000L
#define UVD_SUVD_CGC_GATE__SIT_HEVC_ENC_MASK                                                                  0x00040000L
#define UVD_SUVD_CGC_GATE__SITE_MASK                                                                          0x00080000L
#define UVD_SUVD_CGC_GATE__SRE_VP9_MASK                                                                       0x00100000L
#define UVD_SUVD_CGC_GATE__SCM_VP9_MASK                                                                       0x00200000L
#define UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK                                                                   0x00400000L
#define UVD_SUVD_CGC_GATE__SDB_VP9_MASK                                                                       0x00800000L
#define UVD_SUVD_CGC_GATE__IME_HEVC_MASK                                                                      0x01000000L
#define UVD_SUVD_CGC_GATE__EFC_MASK                                                                           0x02000000L
//UVD_SUVD_CGC_STATUS
#define UVD_SUVD_CGC_STATUS__SRE_VCLK__SHIFT                                                                  0x0
#define UVD_SUVD_CGC_STATUS__SRE_DCLK__SHIFT                                                                  0x1
#define UVD_SUVD_CGC_STATUS__SIT_DCLK__SHIFT                                                                  0x2
#define UVD_SUVD_CGC_STATUS__SMP_DCLK__SHIFT                                                                  0x3
#define UVD_SUVD_CGC_STATUS__SCM_DCLK__SHIFT                                                                  0x4
#define UVD_SUVD_CGC_STATUS__SDB_DCLK__SHIFT                                                                  0x5
#define UVD_SUVD_CGC_STATUS__SRE_H264_VCLK__SHIFT                                                             0x6
#define UVD_SUVD_CGC_STATUS__SRE_HEVC_VCLK__SHIFT                                                             0x7
#define UVD_SUVD_CGC_STATUS__SIT_H264_DCLK__SHIFT                                                             0x8
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DCLK__SHIFT                                                             0x9
#define UVD_SUVD_CGC_STATUS__SCM_H264_DCLK__SHIFT                                                             0xa
#define UVD_SUVD_CGC_STATUS__SCM_HEVC_DCLK__SHIFT                                                             0xb
#define UVD_SUVD_CGC_STATUS__SDB_H264_DCLK__SHIFT                                                             0xc
#define UVD_SUVD_CGC_STATUS__SDB_HEVC_DCLK__SHIFT                                                             0xd
#define UVD_SUVD_CGC_STATUS__SCLR_DCLK__SHIFT                                                                 0xe
#define UVD_SUVD_CGC_STATUS__UVD_SC__SHIFT                                                                    0xf
#define UVD_SUVD_CGC_STATUS__ENT_DCLK__SHIFT                                                                  0x10
#define UVD_SUVD_CGC_STATUS__IME_DCLK__SHIFT                                                                  0x11
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DEC_DCLK__SHIFT                                                         0x12
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_ENC_DCLK__SHIFT                                                         0x13
#define UVD_SUVD_CGC_STATUS__SITE_DCLK__SHIFT                                                                 0x14
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_DCLK__SHIFT                                                            0x15
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_ENC_DCLK__SHIFT                                                        0x16
#define UVD_SUVD_CGC_STATUS__SRE_VP9_VCLK__SHIFT                                                              0x17
#define UVD_SUVD_CGC_STATUS__SCM_VP9_VCLK__SHIFT                                                              0x18
#define UVD_SUVD_CGC_STATUS__SIT_VP9_DEC_DCLK__SHIFT                                                          0x19
#define UVD_SUVD_CGC_STATUS__SDB_VP9_DCLK__SHIFT                                                              0x1a
#define UVD_SUVD_CGC_STATUS__IME_HEVC_DCLK__SHIFT                                                             0x1b
#define UVD_SUVD_CGC_STATUS__EFC_DCLK__SHIFT                                                                  0x1c
#define UVD_SUVD_CGC_STATUS__SRE_VCLK_MASK                                                                    0x00000001L
#define UVD_SUVD_CGC_STATUS__SRE_DCLK_MASK                                                                    0x00000002L
#define UVD_SUVD_CGC_STATUS__SIT_DCLK_MASK                                                                    0x00000004L
#define UVD_SUVD_CGC_STATUS__SMP_DCLK_MASK                                                                    0x00000008L
#define UVD_SUVD_CGC_STATUS__SCM_DCLK_MASK                                                                    0x00000010L
#define UVD_SUVD_CGC_STATUS__SDB_DCLK_MASK                                                                    0x00000020L
#define UVD_SUVD_CGC_STATUS__SRE_H264_VCLK_MASK                                                               0x00000040L
#define UVD_SUVD_CGC_STATUS__SRE_HEVC_VCLK_MASK                                                               0x00000080L
#define UVD_SUVD_CGC_STATUS__SIT_H264_DCLK_MASK                                                               0x00000100L
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DCLK_MASK                                                               0x00000200L
#define UVD_SUVD_CGC_STATUS__SCM_H264_DCLK_MASK                                                               0x00000400L
#define UVD_SUVD_CGC_STATUS__SCM_HEVC_DCLK_MASK                                                               0x00000800L
#define UVD_SUVD_CGC_STATUS__SDB_H264_DCLK_MASK                                                               0x00001000L
#define UVD_SUVD_CGC_STATUS__SDB_HEVC_DCLK_MASK                                                               0x00002000L
#define UVD_SUVD_CGC_STATUS__SCLR_DCLK_MASK                                                                   0x00004000L
#define UVD_SUVD_CGC_STATUS__UVD_SC_MASK                                                                      0x00008000L
#define UVD_SUVD_CGC_STATUS__ENT_DCLK_MASK                                                                    0x00010000L
#define UVD_SUVD_CGC_STATUS__IME_DCLK_MASK                                                                    0x00020000L
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DEC_DCLK_MASK                                                           0x00040000L
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_ENC_DCLK_MASK                                                           0x00080000L
#define UVD_SUVD_CGC_STATUS__SITE_DCLK_MASK                                                                   0x00100000L
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_DCLK_MASK                                                              0x00200000L
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_ENC_DCLK_MASK                                                          0x00400000L
#define UVD_SUVD_CGC_STATUS__SRE_VP9_VCLK_MASK                                                                0x00800000L
#define UVD_SUVD_CGC_STATUS__SCM_VP9_VCLK_MASK                                                                0x01000000L
#define UVD_SUVD_CGC_STATUS__SIT_VP9_DEC_DCLK_MASK                                                            0x02000000L
#define UVD_SUVD_CGC_STATUS__SDB_VP9_DCLK_MASK                                                                0x04000000L
#define UVD_SUVD_CGC_STATUS__IME_HEVC_DCLK_MASK                                                               0x08000000L
#define UVD_SUVD_CGC_STATUS__EFC_DCLK_MASK                                                                    0x10000000L
//UVD_SUVD_CGC_CTRL
#define UVD_SUVD_CGC_CTRL__SRE_MODE__SHIFT                                                                    0x0
#define UVD_SUVD_CGC_CTRL__SIT_MODE__SHIFT                                                                    0x1
#define UVD_SUVD_CGC_CTRL__SMP_MODE__SHIFT                                                                    0x2
#define UVD_SUVD_CGC_CTRL__SCM_MODE__SHIFT                                                                    0x3
#define UVD_SUVD_CGC_CTRL__SDB_MODE__SHIFT                                                                    0x4
#define UVD_SUVD_CGC_CTRL__SCLR_MODE__SHIFT                                                                   0x5
#define UVD_SUVD_CGC_CTRL__UVD_SC_MODE__SHIFT                                                                 0x6
#define UVD_SUVD_CGC_CTRL__ENT_MODE__SHIFT                                                                    0x7
#define UVD_SUVD_CGC_CTRL__IME_MODE__SHIFT                                                                    0x8
#define UVD_SUVD_CGC_CTRL__SITE_MODE__SHIFT                                                                   0x9
#define UVD_SUVD_CGC_CTRL__EFC_MODE__SHIFT                                                                    0xa
#define UVD_SUVD_CGC_CTRL__SRE_MODE_MASK                                                                      0x00000001L
#define UVD_SUVD_CGC_CTRL__SIT_MODE_MASK                                                                      0x00000002L
#define UVD_SUVD_CGC_CTRL__SMP_MODE_MASK                                                                      0x00000004L
#define UVD_SUVD_CGC_CTRL__SCM_MODE_MASK                                                                      0x00000008L
#define UVD_SUVD_CGC_CTRL__SDB_MODE_MASK                                                                      0x00000010L
#define UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK                                                                     0x00000020L
#define UVD_SUVD_CGC_CTRL__UVD_SC_MODE_MASK                                                                   0x00000040L
#define UVD_SUVD_CGC_CTRL__ENT_MODE_MASK                                                                      0x00000080L
#define UVD_SUVD_CGC_CTRL__IME_MODE_MASK                                                                      0x00000100L
#define UVD_SUVD_CGC_CTRL__SITE_MODE_MASK                                                                     0x00000200L
#define UVD_SUVD_CGC_CTRL__EFC_MODE_MASK                                                                      0x00000400L
//UVD_GPCOM_VCPU_CMD
#define UVD_GPCOM_VCPU_CMD__CMD_SEND__SHIFT                                                                   0x0
#define UVD_GPCOM_VCPU_CMD__CMD__SHIFT                                                                        0x1
#define UVD_GPCOM_VCPU_CMD__CMD_SOURCE__SHIFT                                                                 0x1f
#define UVD_GPCOM_VCPU_CMD__CMD_SEND_MASK                                                                     0x00000001L
#define UVD_GPCOM_VCPU_CMD__CMD_MASK                                                                          0x7FFFFFFEL
#define UVD_GPCOM_VCPU_CMD__CMD_SOURCE_MASK                                                                   0x80000000L
//UVD_GPCOM_VCPU_DATA0
#define UVD_GPCOM_VCPU_DATA0__DATA0__SHIFT                                                                    0x0
#define UVD_GPCOM_VCPU_DATA0__DATA0_MASK                                                                      0xFFFFFFFFL
//UVD_GPCOM_VCPU_DATA1
#define UVD_GPCOM_VCPU_DATA1__DATA1__SHIFT                                                                    0x0
#define UVD_GPCOM_VCPU_DATA1__DATA1_MASK                                                                      0xFFFFFFFFL
//UVD_GPCOM_SYS_CMD
#define UVD_GPCOM_SYS_CMD__CMD_SEND__SHIFT                                                                    0x0
#define UVD_GPCOM_SYS_CMD__CMD__SHIFT                                                                         0x1
#define UVD_GPCOM_SYS_CMD__CMD_SOURCE__SHIFT                                                                  0x1f
#define UVD_GPCOM_SYS_CMD__CMD_SEND_MASK                                                                      0x00000001L
#define UVD_GPCOM_SYS_CMD__CMD_MASK                                                                           0x7FFFFFFEL
#define UVD_GPCOM_SYS_CMD__CMD_SOURCE_MASK                                                                    0x80000000L
//UVD_GPCOM_SYS_DATA0
#define UVD_GPCOM_SYS_DATA0__DATA0__SHIFT                                                                     0x0
#define UVD_GPCOM_SYS_DATA0__DATA0_MASK                                                                       0xFFFFFFFFL
//UVD_GPCOM_SYS_DATA1
#define UVD_GPCOM_SYS_DATA1__DATA1__SHIFT                                                                     0x0
#define UVD_GPCOM_SYS_DATA1__DATA1_MASK                                                                       0xFFFFFFFFL
//UVD_VCPU_INT_EN
#define UVD_VCPU_INT_EN__PIF_ADDR_ERR_EN__SHIFT                                                               0x0
#define UVD_VCPU_INT_EN__SEMA_WAIT_FAULT_TIMEOUT_EN__SHIFT                                                    0x1
#define UVD_VCPU_INT_EN__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_EN__SHIFT                                             0x2
#define UVD_VCPU_INT_EN__NJ_PF_RPT_EN__SHIFT                                                                  0x3
#define UVD_VCPU_INT_EN__SW_RB1_INT_EN__SHIFT                                                                 0x4
#define UVD_VCPU_INT_EN__SW_RB2_INT_EN__SHIFT                                                                 0x5
#define UVD_VCPU_INT_EN__RBC_REG_PRIV_FAULT_EN__SHIFT                                                         0x6
#define UVD_VCPU_INT_EN__SW_RB3_INT_EN__SHIFT                                                                 0x7
#define UVD_VCPU_INT_EN__SW_RB4_INT_EN__SHIFT                                                                 0x9
#define UVD_VCPU_INT_EN__SW_RB5_INT_EN__SHIFT                                                                 0xa
#define UVD_VCPU_INT_EN__LBSI_EN__SHIFT                                                                       0xb
#define UVD_VCPU_INT_EN__UDEC_EN__SHIFT                                                                       0xc
#define UVD_VCPU_INT_EN__RPTR_WR_EN__SHIFT                                                                    0x10
#define UVD_VCPU_INT_EN__JOB_START_EN__SHIFT                                                                  0x11
#define UVD_VCPU_INT_EN__NJ_PF_EN__SHIFT                                                                      0x12
#define UVD_VCPU_INT_EN__SEMA_WAIT_FAIL_SIG_EN__SHIFT                                                         0x17
#define UVD_VCPU_INT_EN__IDCT_EN__SHIFT                                                                       0x18
#define UVD_VCPU_INT_EN__MPRD_EN__SHIFT                                                                       0x19
#define UVD_VCPU_INT_EN__AVM_INT_EN__SHIFT                                                                    0x1a
#define UVD_VCPU_INT_EN__CLK_SWT_EN__SHIFT                                                                    0x1b
#define UVD_VCPU_INT_EN__MIF_HWINT_EN__SHIFT                                                                  0x1c
#define UVD_VCPU_INT_EN__MPRD_ERR_EN__SHIFT                                                                   0x1d
#define UVD_VCPU_INT_EN__DRV_FW_REQ_EN__SHIFT                                                                 0x1e
#define UVD_VCPU_INT_EN__DRV_FW_ACK_EN__SHIFT                                                                 0x1f
#define UVD_VCPU_INT_EN__PIF_ADDR_ERR_EN_MASK                                                                 0x00000001L
#define UVD_VCPU_INT_EN__SEMA_WAIT_FAULT_TIMEOUT_EN_MASK                                                      0x00000002L
#define UVD_VCPU_INT_EN__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_EN_MASK                                               0x00000004L
#define UVD_VCPU_INT_EN__NJ_PF_RPT_EN_MASK                                                                    0x00000008L
#define UVD_VCPU_INT_EN__SW_RB1_INT_EN_MASK                                                                   0x00000010L
#define UVD_VCPU_INT_EN__SW_RB2_INT_EN_MASK                                                                   0x00000020L
#define UVD_VCPU_INT_EN__RBC_REG_PRIV_FAULT_EN_MASK                                                           0x00000040L
#define UVD_VCPU_INT_EN__SW_RB3_INT_EN_MASK                                                                   0x00000080L
#define UVD_VCPU_INT_EN__SW_RB4_INT_EN_MASK                                                                   0x00000200L
#define UVD_VCPU_INT_EN__SW_RB5_INT_EN_MASK                                                                   0x00000400L
#define UVD_VCPU_INT_EN__LBSI_EN_MASK                                                                         0x00000800L
#define UVD_VCPU_INT_EN__UDEC_EN_MASK                                                                         0x00001000L
#define UVD_VCPU_INT_EN__RPTR_WR_EN_MASK                                                                      0x00010000L
#define UVD_VCPU_INT_EN__JOB_START_EN_MASK                                                                    0x00020000L
#define UVD_VCPU_INT_EN__NJ_PF_EN_MASK                                                                        0x00040000L
#define UVD_VCPU_INT_EN__SEMA_WAIT_FAIL_SIG_EN_MASK                                                           0x00800000L
#define UVD_VCPU_INT_EN__IDCT_EN_MASK                                                                         0x01000000L
#define UVD_VCPU_INT_EN__MPRD_EN_MASK                                                                         0x02000000L
#define UVD_VCPU_INT_EN__AVM_INT_EN_MASK                                                                      0x04000000L
#define UVD_VCPU_INT_EN__CLK_SWT_EN_MASK                                                                      0x08000000L
#define UVD_VCPU_INT_EN__MIF_HWINT_EN_MASK                                                                    0x10000000L
#define UVD_VCPU_INT_EN__MPRD_ERR_EN_MASK                                                                     0x20000000L
#define UVD_VCPU_INT_EN__DRV_FW_REQ_EN_MASK                                                                   0x40000000L
#define UVD_VCPU_INT_EN__DRV_FW_ACK_EN_MASK                                                                   0x80000000L
//UVD_VCPU_INT_ACK
#define UVD_VCPU_INT_ACK__PIF_ADDR_ERR_ACK__SHIFT                                                             0x0
#define UVD_VCPU_INT_ACK__SEMA_WAIT_FAULT_TIMEOUT_ACK__SHIFT                                                  0x1
#define UVD_VCPU_INT_ACK__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_ACK__SHIFT                                           0x2
#define UVD_VCPU_INT_ACK__NJ_PF_RPT_ACK__SHIFT                                                                0x3
#define UVD_VCPU_INT_ACK__SW_RB1_INT_ACK__SHIFT                                                               0x4
#define UVD_VCPU_INT_ACK__SW_RB2_INT_ACK__SHIFT                                                               0x5
#define UVD_VCPU_INT_ACK__RBC_REG_PRIV_FAULT_ACK__SHIFT                                                       0x6
#define UVD_VCPU_INT_ACK__SW_RB3_INT_ACK__SHIFT                                                               0x7
#define UVD_VCPU_INT_ACK__SW_RB4_INT_ACK__SHIFT                                                               0x9
#define UVD_VCPU_INT_ACK__SW_RB5_INT_ACK__SHIFT                                                               0xa
#define UVD_VCPU_INT_ACK__LBSI_ACK__SHIFT                                                                     0xb
#define UVD_VCPU_INT_ACK__UDEC_ACK__SHIFT                                                                     0xc
#define UVD_VCPU_INT_ACK__RPTR_WR_ACK__SHIFT                                                                  0x10
#define UVD_VCPU_INT_ACK__JOB_START_ACK__SHIFT                                                                0x11
#define UVD_VCPU_INT_ACK__NJ_PF_ACK__SHIFT                                                                    0x12
#define UVD_VCPU_INT_ACK__SEMA_WAIT_FAIL_SIG_ACK__SHIFT                                                       0x17
#define UVD_VCPU_INT_ACK__IDCT_ACK__SHIFT                                                                     0x18
#define UVD_VCPU_INT_ACK__MPRD_ACK__SHIFT                                                                     0x19
#define UVD_VCPU_INT_ACK__AVM_INT_ACK__SHIFT                                                                  0x1a
#define UVD_VCPU_INT_ACK__CLK_SWT_ACK__SHIFT                                                                  0x1b
#define UVD_VCPU_INT_ACK__MIF_HWINT_ACK__SHIFT                                                                0x1c
#define UVD_VCPU_INT_ACK__MPRD_ERR_ACK__SHIFT                                                                 0x1d
#define UVD_VCPU_INT_ACK__DRV_FW_REQ_ACK__SHIFT                                                               0x1e
#define UVD_VCPU_INT_ACK__DRV_FW_ACK_ACK__SHIFT                                                               0x1f
#define UVD_VCPU_INT_ACK__PIF_ADDR_ERR_ACK_MASK                                                               0x00000001L
#define UVD_VCPU_INT_ACK__SEMA_WAIT_FAULT_TIMEOUT_ACK_MASK                                                    0x00000002L
#define UVD_VCPU_INT_ACK__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_ACK_MASK                                             0x00000004L
#define UVD_VCPU_INT_ACK__NJ_PF_RPT_ACK_MASK                                                                  0x00000008L
#define UVD_VCPU_INT_ACK__SW_RB1_INT_ACK_MASK                                                                 0x00000010L
#define UVD_VCPU_INT_ACK__SW_RB2_INT_ACK_MASK                                                                 0x00000020L
#define UVD_VCPU_INT_ACK__RBC_REG_PRIV_FAULT_ACK_MASK                                                         0x00000040L
#define UVD_VCPU_INT_ACK__SW_RB3_INT_ACK_MASK                                                                 0x00000080L
#define UVD_VCPU_INT_ACK__SW_RB4_INT_ACK_MASK                                                                 0x00000200L
#define UVD_VCPU_INT_ACK__SW_RB5_INT_ACK_MASK                                                                 0x00000400L
#define UVD_VCPU_INT_ACK__LBSI_ACK_MASK                                                                       0x00000800L
#define UVD_VCPU_INT_ACK__UDEC_ACK_MASK                                                                       0x00001000L
#define UVD_VCPU_INT_ACK__RPTR_WR_ACK_MASK                                                                    0x00010000L
#define UVD_VCPU_INT_ACK__JOB_START_ACK_MASK                                                                  0x00020000L
#define UVD_VCPU_INT_ACK__NJ_PF_ACK_MASK                                                                      0x00040000L
#define UVD_VCPU_INT_ACK__SEMA_WAIT_FAIL_SIG_ACK_MASK                                                         0x00800000L
#define UVD_VCPU_INT_ACK__IDCT_ACK_MASK                                                                       0x01000000L
#define UVD_VCPU_INT_ACK__MPRD_ACK_MASK                                                                       0x02000000L
#define UVD_VCPU_INT_ACK__AVM_INT_ACK_MASK                                                                    0x04000000L
#define UVD_VCPU_INT_ACK__CLK_SWT_ACK_MASK                                                                    0x08000000L
#define UVD_VCPU_INT_ACK__MIF_HWINT_ACK_MASK                                                                  0x10000000L
#define UVD_VCPU_INT_ACK__MPRD_ERR_ACK_MASK                                                                   0x20000000L
#define UVD_VCPU_INT_ACK__DRV_FW_REQ_ACK_MASK                                                                 0x40000000L
#define UVD_VCPU_INT_ACK__DRV_FW_ACK_ACK_MASK                                                                 0x80000000L
//UVD_VCPU_INT_ROUTE
#define UVD_VCPU_INT_ROUTE__DRV_FW_MSG__SHIFT                                                                 0x0
#define UVD_VCPU_INT_ROUTE__FW_DRV_MSG_ACK__SHIFT                                                             0x1
#define UVD_VCPU_INT_ROUTE__VCPU_GPCOM__SHIFT                                                                 0x2
#define UVD_VCPU_INT_ROUTE__DRV_FW_MSG_MASK                                                                   0x00000001L
#define UVD_VCPU_INT_ROUTE__FW_DRV_MSG_ACK_MASK                                                               0x00000002L
#define UVD_VCPU_INT_ROUTE__VCPU_GPCOM_MASK                                                                   0x00000004L
//UVD_ENC_VCPU_INT_EN
#define UVD_ENC_VCPU_INT_EN__DCE_UVD_SCAN_IN_BUFMGR_EN__SHIFT                                                 0x0
#define UVD_ENC_VCPU_INT_EN__DCE_UVD_SCAN_IN_BUFMGR2_EN__SHIFT                                                0x1
#define UVD_ENC_VCPU_INT_EN__DCE_UVD_SCAN_IN_BUFMGR3_EN__SHIFT                                                0x2
#define UVD_ENC_VCPU_INT_EN__DCE_UVD_SCAN_IN_BUFMGR_EN_MASK                                                   0x00000001L
#define UVD_ENC_VCPU_INT_EN__DCE_UVD_SCAN_IN_BUFMGR2_EN_MASK                                                  0x00000002L
#define UVD_ENC_VCPU_INT_EN__DCE_UVD_SCAN_IN_BUFMGR3_EN_MASK                                                  0x00000004L
//UVD_ENC_VCPU_INT_ACK
#define UVD_ENC_VCPU_INT_ACK__DCE_UVD_SCAN_IN_BUFMGR_ACK__SHIFT                                               0x0
#define UVD_ENC_VCPU_INT_ACK__DCE_UVD_SCAN_IN_BUFMGR2_ACK__SHIFT                                              0x1
#define UVD_ENC_VCPU_INT_ACK__DCE_UVD_SCAN_IN_BUFMGR3_ACK__SHIFT                                              0x2
#define UVD_ENC_VCPU_INT_ACK__DCE_UVD_SCAN_IN_BUFMGR_ACK_MASK                                                 0x00000001L
#define UVD_ENC_VCPU_INT_ACK__DCE_UVD_SCAN_IN_BUFMGR2_ACK_MASK                                                0x00000002L
#define UVD_ENC_VCPU_INT_ACK__DCE_UVD_SCAN_IN_BUFMGR3_ACK_MASK                                                0x00000004L
//UVD_MASTINT_EN
#define UVD_MASTINT_EN__OVERRUN_RST__SHIFT                                                                    0x0
#define UVD_MASTINT_EN__VCPU_EN__SHIFT                                                                        0x1
#define UVD_MASTINT_EN__SYS_EN__SHIFT                                                                         0x2
#define UVD_MASTINT_EN__INT_OVERRUN__SHIFT                                                                    0x4
#define UVD_MASTINT_EN__OVERRUN_RST_MASK                                                                      0x00000001L
#define UVD_MASTINT_EN__VCPU_EN_MASK                                                                          0x00000002L
#define UVD_MASTINT_EN__SYS_EN_MASK                                                                           0x00000004L
#define UVD_MASTINT_EN__INT_OVERRUN_MASK                                                                      0x007FFFF0L
//UVD_SYS_INT_EN
#define UVD_SYS_INT_EN__PIF_ADDR_ERR_EN__SHIFT                                                                0x0
#define UVD_SYS_INT_EN__SEMA_WAIT_FAULT_TIMEOUT_EN__SHIFT                                                     0x1
#define UVD_SYS_INT_EN__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_EN__SHIFT                                              0x2
#define UVD_SYS_INT_EN__CXW_WR_EN__SHIFT                                                                      0x3
#define UVD_SYS_INT_EN__RBC_REG_PRIV_FAULT_EN__SHIFT                                                          0x6
#define UVD_SYS_INT_EN__LBSI_EN__SHIFT                                                                        0xb
#define UVD_SYS_INT_EN__UDEC_EN__SHIFT                                                                        0xc
#define UVD_SYS_INT_EN__JOB_DONE_EN__SHIFT                                                                    0x10
#define UVD_SYS_INT_EN__SEMA_WAIT_FAIL_SIG_EN__SHIFT                                                          0x17
#define UVD_SYS_INT_EN__IDCT_EN__SHIFT                                                                        0x18
#define UVD_SYS_INT_EN__MPRD_EN__SHIFT                                                                        0x19
#define UVD_SYS_INT_EN__CLK_SWT_EN__SHIFT                                                                     0x1b
#define UVD_SYS_INT_EN__MIF_HWINT_EN__SHIFT                                                                   0x1c
#define UVD_SYS_INT_EN__MPRD_ERR_EN__SHIFT                                                                    0x1d
#define UVD_SYS_INT_EN__AVM_INT_EN__SHIFT                                                                     0x1f
#define UVD_SYS_INT_EN__PIF_ADDR_ERR_EN_MASK                                                                  0x00000001L
#define UVD_SYS_INT_EN__SEMA_WAIT_FAULT_TIMEOUT_EN_MASK                                                       0x00000002L
#define UVD_SYS_INT_EN__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_EN_MASK                                                0x00000004L
#define UVD_SYS_INT_EN__CXW_WR_EN_MASK                                                                        0x00000008L
#define UVD_SYS_INT_EN__RBC_REG_PRIV_FAULT_EN_MASK                                                            0x00000040L
#define UVD_SYS_INT_EN__LBSI_EN_MASK                                                                          0x00000800L
#define UVD_SYS_INT_EN__UDEC_EN_MASK                                                                          0x00001000L
#define UVD_SYS_INT_EN__JOB_DONE_EN_MASK                                                                      0x00010000L
#define UVD_SYS_INT_EN__SEMA_WAIT_FAIL_SIG_EN_MASK                                                            0x00800000L
#define UVD_SYS_INT_EN__IDCT_EN_MASK                                                                          0x01000000L
#define UVD_SYS_INT_EN__MPRD_EN_MASK                                                                          0x02000000L
#define UVD_SYS_INT_EN__CLK_SWT_EN_MASK                                                                       0x08000000L
#define UVD_SYS_INT_EN__MIF_HWINT_EN_MASK                                                                     0x10000000L
#define UVD_SYS_INT_EN__MPRD_ERR_EN_MASK                                                                      0x20000000L
#define UVD_SYS_INT_EN__AVM_INT_EN_MASK                                                                       0x80000000L
//UVD_SYS_INT_STATUS
#define UVD_SYS_INT_STATUS__PIF_ADDR_ERR_INT__SHIFT                                                           0x0
#define UVD_SYS_INT_STATUS__SEMA_WAIT_FAULT_TIMEOUT_INT__SHIFT                                                0x1
#define UVD_SYS_INT_STATUS__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_INT__SHIFT                                         0x2
#define UVD_SYS_INT_STATUS__CXW_WR_INT__SHIFT                                                                 0x3
#define UVD_SYS_INT_STATUS__RBC_REG_PRIV_FAULT_INT__SHIFT                                                     0x6
#define UVD_SYS_INT_STATUS__LBSI_INT__SHIFT                                                                   0xb
#define UVD_SYS_INT_STATUS__UDEC_INT__SHIFT                                                                   0xc
#define UVD_SYS_INT_STATUS__JOB_DONE_INT__SHIFT                                                               0x10
#define UVD_SYS_INT_STATUS__GPCOM_INT__SHIFT                                                                  0x12
#define UVD_SYS_INT_STATUS__SEMA_WAIT_FAIL_SIG_INT__SHIFT                                                     0x17
#define UVD_SYS_INT_STATUS__IDCT_INT__SHIFT                                                                   0x18
#define UVD_SYS_INT_STATUS__MPRD_INT__SHIFT                                                                   0x19
#define UVD_SYS_INT_STATUS__CLK_SWT_INT__SHIFT                                                                0x1b
#define UVD_SYS_INT_STATUS__MIF_HWINT__SHIFT                                                                  0x1c
#define UVD_SYS_INT_STATUS__MPRD_ERR_INT__SHIFT                                                               0x1d
#define UVD_SYS_INT_STATUS__AVM_INT__SHIFT                                                                    0x1f
#define UVD_SYS_INT_STATUS__PIF_ADDR_ERR_INT_MASK                                                             0x00000001L
#define UVD_SYS_INT_STATUS__SEMA_WAIT_FAULT_TIMEOUT_INT_MASK                                                  0x00000002L
#define UVD_SYS_INT_STATUS__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_INT_MASK                                           0x00000004L
#define UVD_SYS_INT_STATUS__CXW_WR_INT_MASK                                                                   0x00000008L
#define UVD_SYS_INT_STATUS__RBC_REG_PRIV_FAULT_INT_MASK                                                       0x00000040L
#define UVD_SYS_INT_STATUS__LBSI_INT_MASK                                                                     0x00000800L
#define UVD_SYS_INT_STATUS__UDEC_INT_MASK                                                                     0x00001000L
#define UVD_SYS_INT_STATUS__JOB_DONE_INT_MASK                                                                 0x00010000L
#define UVD_SYS_INT_STATUS__GPCOM_INT_MASK                                                                    0x00040000L
#define UVD_SYS_INT_STATUS__SEMA_WAIT_FAIL_SIG_INT_MASK                                                       0x00800000L
#define UVD_SYS_INT_STATUS__IDCT_INT_MASK                                                                     0x01000000L
#define UVD_SYS_INT_STATUS__MPRD_INT_MASK                                                                     0x02000000L
#define UVD_SYS_INT_STATUS__CLK_SWT_INT_MASK                                                                  0x08000000L
#define UVD_SYS_INT_STATUS__MIF_HWINT_MASK                                                                    0x10000000L
#define UVD_SYS_INT_STATUS__MPRD_ERR_INT_MASK                                                                 0x20000000L
#define UVD_SYS_INT_STATUS__AVM_INT_MASK                                                                      0x80000000L
//UVD_SYS_INT_ACK
#define UVD_SYS_INT_ACK__PIF_ADDR_ERR_ACK__SHIFT                                                              0x0
#define UVD_SYS_INT_ACK__SEMA_WAIT_FAULT_TIMEOUT_ACK__SHIFT                                                   0x1
#define UVD_SYS_INT_ACK__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_ACK__SHIFT                                            0x2
#define UVD_SYS_INT_ACK__CXW_WR_ACK__SHIFT                                                                    0x3
#define UVD_SYS_INT_ACK__RBC_REG_PRIV_FAULT_ACK__SHIFT                                                        0x6
#define UVD_SYS_INT_ACK__LBSI_ACK__SHIFT                                                                      0xb
#define UVD_SYS_INT_ACK__UDEC_ACK__SHIFT                                                                      0xc
#define UVD_SYS_INT_ACK__JOB_DONE_ACK__SHIFT                                                                  0x10
#define UVD_SYS_INT_ACK__SEMA_WAIT_FAIL_SIG_ACK__SHIFT                                                        0x17
#define UVD_SYS_INT_ACK__IDCT_ACK__SHIFT                                                                      0x18
#define UVD_SYS_INT_ACK__MPRD_ACK__SHIFT                                                                      0x19
#define UVD_SYS_INT_ACK__CLK_SWT_ACK__SHIFT                                                                   0x1b
#define UVD_SYS_INT_ACK__MIF_HWINT_ACK__SHIFT                                                                 0x1c
#define UVD_SYS_INT_ACK__MPRD_ERR_ACK__SHIFT                                                                  0x1d
#define UVD_SYS_INT_ACK__AVM_INT_ACK__SHIFT                                                                   0x1f
#define UVD_SYS_INT_ACK__PIF_ADDR_ERR_ACK_MASK                                                                0x00000001L
#define UVD_SYS_INT_ACK__SEMA_WAIT_FAULT_TIMEOUT_ACK_MASK                                                     0x00000002L
#define UVD_SYS_INT_ACK__SEMA_SIGNAL_INCOMPLETE_TIMEOUT_ACK_MASK                                              0x00000004L
#define UVD_SYS_INT_ACK__CXW_WR_ACK_MASK                                                                      0x00000008L
#define UVD_SYS_INT_ACK__RBC_REG_PRIV_FAULT_ACK_MASK                                                          0x00000040L
#define UVD_SYS_INT_ACK__LBSI_ACK_MASK                                                                        0x00000800L
#define UVD_SYS_INT_ACK__UDEC_ACK_MASK                                                                        0x00001000L
#define UVD_SYS_INT_ACK__JOB_DONE_ACK_MASK                                                                    0x00010000L
#define UVD_SYS_INT_ACK__SEMA_WAIT_FAIL_SIG_ACK_MASK                                                          0x00800000L
#define UVD_SYS_INT_ACK__IDCT_ACK_MASK                                                                        0x01000000L
#define UVD_SYS_INT_ACK__MPRD_ACK_MASK                                                                        0x02000000L
#define UVD_SYS_INT_ACK__CLK_SWT_ACK_MASK                                                                     0x08000000L
#define UVD_SYS_INT_ACK__MIF_HWINT_ACK_MASK                                                                   0x10000000L
#define UVD_SYS_INT_ACK__MPRD_ERR_ACK_MASK                                                                    0x20000000L
#define UVD_SYS_INT_ACK__AVM_INT_ACK_MASK                                                                     0x80000000L
//UVD_JOB_DONE
#define UVD_JOB_DONE__JOB_DONE__SHIFT                                                                         0x0
#define UVD_JOB_DONE__JOB_DONE_MASK                                                                           0x00000003L
//UVD_CBUF_ID
#define UVD_CBUF_ID__CBUF_ID__SHIFT                                                                           0x0
#define UVD_CBUF_ID__CBUF_ID_MASK                                                                             0xFFFFFFFFL
//UVD_CONTEXT_ID
#define UVD_CONTEXT_ID__CONTEXT_ID__SHIFT                                                                     0x0
#define UVD_CONTEXT_ID__CONTEXT_ID_MASK                                                                       0xFFFFFFFFL
//UVD_CONTEXT_ID2
#define UVD_CONTEXT_ID2__CONTEXT_ID2__SHIFT                                                                   0x0
#define UVD_CONTEXT_ID2__CONTEXT_ID2_MASK                                                                     0xFFFFFFFFL
//UVD_NO_OP
#define UVD_NO_OP__NO_OP__SHIFT                                                                               0x0
#define UVD_NO_OP__NO_OP_MASK                                                                                 0xFFFFFFFFL
//UVD_RB_BASE_LO
#define UVD_RB_BASE_LO__RB_BASE_LO__SHIFT                                                                     0x6
#define UVD_RB_BASE_LO__RB_BASE_LO_MASK                                                                       0xFFFFFFC0L
//UVD_RB_BASE_HI
#define UVD_RB_BASE_HI__RB_BASE_HI__SHIFT                                                                     0x0
#define UVD_RB_BASE_HI__RB_BASE_HI_MASK                                                                       0xFFFFFFFFL
//UVD_RB_SIZE
#define UVD_RB_SIZE__RB_SIZE__SHIFT                                                                           0x4
#define UVD_RB_SIZE__RB_SIZE_MASK                                                                             0x007FFFF0L
//UVD_RB_RPTR
#define UVD_RB_RPTR__RB_RPTR__SHIFT                                                                           0x4
#define UVD_RB_RPTR__RB_RPTR_MASK                                                                             0x007FFFF0L
//UVD_RB_WPTR
#define UVD_RB_WPTR__RB_WPTR__SHIFT                                                                           0x4
#define UVD_RB_WPTR__RB_WPTR_MASK                                                                             0x007FFFF0L
//UVD_RB_BASE_LO2
#define UVD_RB_BASE_LO2__RB_BASE_LO__SHIFT                                                                    0x6
#define UVD_RB_BASE_LO2__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//UVD_RB_BASE_HI2
#define UVD_RB_BASE_HI2__RB_BASE_HI__SHIFT                                                                    0x0
#define UVD_RB_BASE_HI2__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//UVD_RB_SIZE2
#define UVD_RB_SIZE2__RB_SIZE__SHIFT                                                                          0x4
#define UVD_RB_SIZE2__RB_SIZE_MASK                                                                            0x007FFFF0L
//UVD_RB_RPTR2
#define UVD_RB_RPTR2__RB_RPTR__SHIFT                                                                          0x4
#define UVD_RB_RPTR2__RB_RPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_WPTR2
#define UVD_RB_WPTR2__RB_WPTR__SHIFT                                                                          0x4
#define UVD_RB_WPTR2__RB_WPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_BASE_LO3
#define UVD_RB_BASE_LO3__RB_BASE_LO__SHIFT                                                                    0x6
#define UVD_RB_BASE_LO3__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//UVD_RB_BASE_HI3
#define UVD_RB_BASE_HI3__RB_BASE_HI__SHIFT                                                                    0x0
#define UVD_RB_BASE_HI3__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//UVD_RB_SIZE3
#define UVD_RB_SIZE3__RB_SIZE__SHIFT                                                                          0x4
#define UVD_RB_SIZE3__RB_SIZE_MASK                                                                            0x007FFFF0L
//UVD_RB_RPTR3
#define UVD_RB_RPTR3__RB_RPTR__SHIFT                                                                          0x4
#define UVD_RB_RPTR3__RB_RPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_WPTR3
#define UVD_RB_WPTR3__RB_WPTR__SHIFT                                                                          0x4
#define UVD_RB_WPTR3__RB_WPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_BASE_LO4
#define UVD_RB_BASE_LO4__RB_BASE_LO__SHIFT                                                                    0x6
#define UVD_RB_BASE_LO4__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//UVD_RB_BASE_HI4
#define UVD_RB_BASE_HI4__RB_BASE_HI__SHIFT                                                                    0x0
#define UVD_RB_BASE_HI4__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//UVD_RB_SIZE4
#define UVD_RB_SIZE4__RB_SIZE__SHIFT                                                                          0x4
#define UVD_RB_SIZE4__RB_SIZE_MASK                                                                            0x007FFFF0L
//UVD_RB_RPTR4
#define UVD_RB_RPTR4__RB_RPTR__SHIFT                                                                          0x4
#define UVD_RB_RPTR4__RB_RPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_WPTR4
#define UVD_RB_WPTR4__RB_WPTR__SHIFT                                                                          0x4
#define UVD_RB_WPTR4__RB_WPTR_MASK                                                                            0x007FFFF0L
//UVD_OUT_RB_BASE_LO
#define UVD_OUT_RB_BASE_LO__RB_BASE_LO__SHIFT                                                                 0x6
#define UVD_OUT_RB_BASE_LO__RB_BASE_LO_MASK                                                                   0xFFFFFFC0L
//UVD_OUT_RB_BASE_HI
#define UVD_OUT_RB_BASE_HI__RB_BASE_HI__SHIFT                                                                 0x0
#define UVD_OUT_RB_BASE_HI__RB_BASE_HI_MASK                                                                   0xFFFFFFFFL
//UVD_OUT_RB_SIZE
#define UVD_OUT_RB_SIZE__RB_SIZE__SHIFT                                                                       0x4
#define UVD_OUT_RB_SIZE__RB_SIZE_MASK                                                                         0x007FFFF0L
//UVD_OUT_RB_RPTR
#define UVD_OUT_RB_RPTR__RB_RPTR__SHIFT                                                                       0x4
#define UVD_OUT_RB_RPTR__RB_RPTR_MASK                                                                         0x007FFFF0L
//UVD_OUT_RB_WPTR
#define UVD_OUT_RB_WPTR__RB_WPTR__SHIFT                                                                       0x4
#define UVD_OUT_RB_WPTR__RB_WPTR_MASK                                                                         0x007FFFF0L
//UVD_RB_ARB_CTRL
#define UVD_RB_ARB_CTRL__SRBM_DROP__SHIFT                                                                     0x0
#define UVD_RB_ARB_CTRL__SRBM_DIS__SHIFT                                                                      0x1
#define UVD_RB_ARB_CTRL__VCPU_DROP__SHIFT                                                                     0x2
#define UVD_RB_ARB_CTRL__VCPU_DIS__SHIFT                                                                      0x3
#define UVD_RB_ARB_CTRL__RBC_DROP__SHIFT                                                                      0x4
#define UVD_RB_ARB_CTRL__RBC_DIS__SHIFT                                                                       0x5
#define UVD_RB_ARB_CTRL__FWOFLD_DROP__SHIFT                                                                   0x6
#define UVD_RB_ARB_CTRL__FWOFLD_DIS__SHIFT                                                                    0x7
#define UVD_RB_ARB_CTRL__FAST_PATH_EN__SHIFT                                                                  0x8
#define UVD_RB_ARB_CTRL__SRBM_DROP_MASK                                                                       0x00000001L
#define UVD_RB_ARB_CTRL__SRBM_DIS_MASK                                                                        0x00000002L
#define UVD_RB_ARB_CTRL__VCPU_DROP_MASK                                                                       0x00000004L
#define UVD_RB_ARB_CTRL__VCPU_DIS_MASK                                                                        0x00000008L
#define UVD_RB_ARB_CTRL__RBC_DROP_MASK                                                                        0x00000010L
#define UVD_RB_ARB_CTRL__RBC_DIS_MASK                                                                         0x00000020L
#define UVD_RB_ARB_CTRL__FWOFLD_DROP_MASK                                                                     0x00000040L
#define UVD_RB_ARB_CTRL__FWOFLD_DIS_MASK                                                                      0x00000080L
#define UVD_RB_ARB_CTRL__FAST_PATH_EN_MASK                                                                    0x00000100L
//UVD_CTX_INDEX
#define UVD_CTX_INDEX__INDEX__SHIFT                                                                           0x0
#define UVD_CTX_INDEX__INDEX_MASK                                                                             0x000001FFL
//UVD_CTX_DATA
#define UVD_CTX_DATA__DATA__SHIFT                                                                             0x0
#define UVD_CTX_DATA__DATA_MASK                                                                               0xFFFFFFFFL
//UVD_CXW_WR
#define UVD_CXW_WR__DAT__SHIFT                                                                                0x0
#define UVD_CXW_WR__STAT__SHIFT                                                                               0x1f
#define UVD_CXW_WR__DAT_MASK                                                                                  0x0FFFFFFFL
#define UVD_CXW_WR__STAT_MASK                                                                                 0x80000000L
//UVD_CXW_WR_INT_ID
#define UVD_CXW_WR_INT_ID__ID__SHIFT                                                                          0x0
#define UVD_CXW_WR_INT_ID__ID_MASK                                                                            0x000000FFL
//UVD_CXW_WR_INT_CTX_ID
#define UVD_CXW_WR_INT_CTX_ID__ID__SHIFT                                                                      0x0
#define UVD_CXW_WR_INT_CTX_ID__ID_MASK                                                                        0x0FFFFFFFL
//UVD_CXW_INT_ID
#define UVD_CXW_INT_ID__ID__SHIFT                                                                             0x0
#define UVD_CXW_INT_ID__ID_MASK                                                                               0x000000FFL
//UVD_TOP_CTRL
#define UVD_TOP_CTRL__STANDARD__SHIFT                                                                         0x0
#define UVD_TOP_CTRL__STD_VERSION__SHIFT                                                                      0x4
#define UVD_TOP_CTRL__STANDARD_MASK                                                                           0x0000000FL
#define UVD_TOP_CTRL__STD_VERSION_MASK                                                                        0x000000F0L
//UVD_YBASE
#define UVD_YBASE__DUM__SHIFT                                                                                 0x0
#define UVD_YBASE__DUM_MASK                                                                                   0xFFFFFFFFL
//UVD_UVBASE
#define UVD_UVBASE__DUM__SHIFT                                                                                0x0
#define UVD_UVBASE__DUM_MASK                                                                                  0xFFFFFFFFL
//UVD_PITCH
#define UVD_PITCH__DUM__SHIFT                                                                                 0x0
#define UVD_PITCH__DUM_MASK                                                                                   0xFFFFFFFFL
//UVD_WIDTH
#define UVD_WIDTH__DUM__SHIFT                                                                                 0x0
#define UVD_WIDTH__DUM_MASK                                                                                   0xFFFFFFFFL
//UVD_HEIGHT
#define UVD_HEIGHT__DUM__SHIFT                                                                                0x0
#define UVD_HEIGHT__DUM_MASK                                                                                  0xFFFFFFFFL
//UVD_PICCOUNT
#define UVD_PICCOUNT__DUM__SHIFT                                                                              0x0
#define UVD_PICCOUNT__DUM_MASK                                                                                0xFFFFFFFFL
//UVD_SCRATCH_NP
#define UVD_SCRATCH_NP__DATA__SHIFT                                                                           0x0
#define UVD_SCRATCH_NP__DATA_MASK                                                                             0xFFFFFFFFL
//UVD_VERSION
#define UVD_VERSION__MINOR_VERSION__SHIFT                                                                     0x0
#define UVD_VERSION__MAJOR_VERSION__SHIFT                                                                     0x10
#define UVD_VERSION__MINOR_VERSION_MASK                                                                       0x0000FFFFL
#define UVD_VERSION__MAJOR_VERSION_MASK                                                                       0x0FFF0000L
//UVD_GP_SCRATCH0
#define UVD_GP_SCRATCH0__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH0__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH1
#define UVD_GP_SCRATCH1__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH1__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH2
#define UVD_GP_SCRATCH2__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH2__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH3
#define UVD_GP_SCRATCH3__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH3__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH4
#define UVD_GP_SCRATCH4__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH4__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH5
#define UVD_GP_SCRATCH5__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH5__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH6
#define UVD_GP_SCRATCH6__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH6__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH7
#define UVD_GP_SCRATCH7__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH7__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH8
#define UVD_GP_SCRATCH8__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH8__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH9
#define UVD_GP_SCRATCH9__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH9__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH10
#define UVD_GP_SCRATCH10__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH10__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH11
#define UVD_GP_SCRATCH11__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH11__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH12
#define UVD_GP_SCRATCH12__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH12__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH13
#define UVD_GP_SCRATCH13__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH13__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH14
#define UVD_GP_SCRATCH14__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH14__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH15
#define UVD_GP_SCRATCH15__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH15__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH16
#define UVD_GP_SCRATCH16__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH16__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH17
#define UVD_GP_SCRATCH17__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH17__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH18
#define UVD_GP_SCRATCH18__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH18__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH19
#define UVD_GP_SCRATCH19__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH19__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH20
#define UVD_GP_SCRATCH20__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH20__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH21
#define UVD_GP_SCRATCH21__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH21__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH22
#define UVD_GP_SCRATCH22__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH22__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH23
#define UVD_GP_SCRATCH23__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH23__DATA_MASK                                                                           0xFFFFFFFFL


// addressBlock: uvd0_ecpudec
//UVD_VCPU_CACHE_OFFSET0
#define UVD_VCPU_CACHE_OFFSET0__CACHE_OFFSET0__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET0__CACHE_OFFSET0_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE0
#define UVD_VCPU_CACHE_SIZE0__CACHE_SIZE0__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE0__CACHE_SIZE0_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET1
#define UVD_VCPU_CACHE_OFFSET1__CACHE_OFFSET1__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET1__CACHE_OFFSET1_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE1
#define UVD_VCPU_CACHE_SIZE1__CACHE_SIZE1__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE1__CACHE_SIZE1_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET2
#define UVD_VCPU_CACHE_OFFSET2__CACHE_OFFSET2__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET2__CACHE_OFFSET2_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE2
#define UVD_VCPU_CACHE_SIZE2__CACHE_SIZE2__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE2__CACHE_SIZE2_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET3
#define UVD_VCPU_CACHE_OFFSET3__CACHE_OFFSET3__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET3__CACHE_OFFSET3_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE3
#define UVD_VCPU_CACHE_SIZE3__CACHE_SIZE3__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE3__CACHE_SIZE3_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET4
#define UVD_VCPU_CACHE_OFFSET4__CACHE_OFFSET4__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET4__CACHE_OFFSET4_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE4
#define UVD_VCPU_CACHE_SIZE4__CACHE_SIZE4__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE4__CACHE_SIZE4_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET5
#define UVD_VCPU_CACHE_OFFSET5__CACHE_OFFSET5__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET5__CACHE_OFFSET5_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE5
#define UVD_VCPU_CACHE_SIZE5__CACHE_SIZE5__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE5__CACHE_SIZE5_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET6
#define UVD_VCPU_CACHE_OFFSET6__CACHE_OFFSET6__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET6__CACHE_OFFSET6_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE6
#define UVD_VCPU_CACHE_SIZE6__CACHE_SIZE6__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE6__CACHE_SIZE6_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET7
#define UVD_VCPU_CACHE_OFFSET7__CACHE_OFFSET7__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET7__CACHE_OFFSET7_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE7
#define UVD_VCPU_CACHE_SIZE7__CACHE_SIZE7__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE7__CACHE_SIZE7_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET8
#define UVD_VCPU_CACHE_OFFSET8__CACHE_OFFSET8__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET8__CACHE_OFFSET8_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE8
#define UVD_VCPU_CACHE_SIZE8__CACHE_SIZE8__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE8__CACHE_SIZE8_MASK                                                                0x001FFFFFL
//UVD_VCPU_NONCACHE_OFFSET0
#define UVD_VCPU_NONCACHE_OFFSET0__NONCACHE_OFFSET0__SHIFT                                                    0x0
#define UVD_VCPU_NONCACHE_OFFSET0__NONCACHE_OFFSET0_MASK                                                      0x01FFFFFFL
//UVD_VCPU_NONCACHE_SIZE0
#define UVD_VCPU_NONCACHE_SIZE0__NONCACHE_SIZE0__SHIFT                                                        0x0
#define UVD_VCPU_NONCACHE_SIZE0__NONCACHE_SIZE0_MASK                                                          0x001FFFFFL
//UVD_VCPU_NONCACHE_OFFSET1
#define UVD_VCPU_NONCACHE_OFFSET1__NONCACHE_OFFSET1__SHIFT                                                    0x0
#define UVD_VCPU_NONCACHE_OFFSET1__NONCACHE_OFFSET1_MASK                                                      0x01FFFFFFL
//UVD_VCPU_NONCACHE_SIZE1
#define UVD_VCPU_NONCACHE_SIZE1__NONCACHE_SIZE1__SHIFT                                                        0x0
#define UVD_VCPU_NONCACHE_SIZE1__NONCACHE_SIZE1_MASK                                                          0x001FFFFFL
//UVD_VCPU_CNTL
#define UVD_VCPU_CNTL__IRQ_ERR__SHIFT                                                                         0x0
#define UVD_VCPU_CNTL__PMB_ED_ENABLE__SHIFT                                                                   0x5
#define UVD_VCPU_CNTL__PMB_SOFT_RESET__SHIFT                                                                  0x6
#define UVD_VCPU_CNTL__RBBM_SOFT_RESET__SHIFT                                                                 0x7
#define UVD_VCPU_CNTL__ABORT_REQ__SHIFT                                                                       0x8
#define UVD_VCPU_CNTL__CLK_EN__SHIFT                                                                          0x9
#define UVD_VCPU_CNTL__TRCE_EN__SHIFT                                                                         0xa
#define UVD_VCPU_CNTL__TRCE_MUX__SHIFT                                                                        0xb
#define UVD_VCPU_CNTL__JTAG_EN__SHIFT                                                                         0x10
#define UVD_VCPU_CNTL__TIMEOUT_DIS__SHIFT                                                                     0x12
#define UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT                                                                 0x14
#define UVD_VCPU_CNTL__BLK_RST__SHIFT                                                                         0x1c
#define UVD_VCPU_CNTL__IRQ_ERR_MASK                                                                           0x0000000FL
#define UVD_VCPU_CNTL__PMB_ED_ENABLE_MASK                                                                     0x00000020L
#define UVD_VCPU_CNTL__PMB_SOFT_RESET_MASK                                                                    0x00000040L
#define UVD_VCPU_CNTL__RBBM_SOFT_RESET_MASK                                                                   0x00000080L
#define UVD_VCPU_CNTL__ABORT_REQ_MASK                                                                         0x00000100L
#define UVD_VCPU_CNTL__CLK_EN_MASK                                                                            0x00000200L
#define UVD_VCPU_CNTL__TRCE_EN_MASK                                                                           0x00000400L
#define UVD_VCPU_CNTL__TRCE_MUX_MASK                                                                          0x00001800L
#define UVD_VCPU_CNTL__JTAG_EN_MASK                                                                           0x00010000L
#define UVD_VCPU_CNTL__TIMEOUT_DIS_MASK                                                                       0x00040000L
#define UVD_VCPU_CNTL__PRB_TIMEOUT_VAL_MASK                                                                   0x0FF00000L
#define UVD_VCPU_CNTL__BLK_RST_MASK                                                                           0x10000000L
//UVD_VCPU_PRID
#define UVD_VCPU_PRID__PRID__SHIFT                                                                            0x0
#define UVD_VCPU_PRID__PRID_MASK                                                                              0x0000FFFFL
//UVD_VCPU_TRCE
#define UVD_VCPU_TRCE__PC__SHIFT                                                                              0x0
#define UVD_VCPU_TRCE__PC_MASK                                                                                0x0FFFFFFFL
//UVD_VCPU_TRCE_RD
#define UVD_VCPU_TRCE_RD__DATA__SHIFT                                                                         0x0
#define UVD_VCPU_TRCE_RD__DATA_MASK                                                                           0xFFFFFFFFL


// addressBlock: uvd0_uvd_mpcdec
//UVD_MP_SWAP_CNTL
#define UVD_MP_SWAP_CNTL__MP_REF0_MC_SWAP__SHIFT                                                              0x0
#define UVD_MP_SWAP_CNTL__MP_REF1_MC_SWAP__SHIFT                                                              0x2
#define UVD_MP_SWAP_CNTL__MP_REF2_MC_SWAP__SHIFT                                                              0x4
#define UVD_MP_SWAP_CNTL__MP_REF3_MC_SWAP__SHIFT                                                              0x6
#define UVD_MP_SWAP_CNTL__MP_REF4_MC_SWAP__SHIFT                                                              0x8
#define UVD_MP_SWAP_CNTL__MP_REF5_MC_SWAP__SHIFT                                                              0xa
#define UVD_MP_SWAP_CNTL__MP_REF6_MC_SWAP__SHIFT                                                              0xc
#define UVD_MP_SWAP_CNTL__MP_REF7_MC_SWAP__SHIFT                                                              0xe
#define UVD_MP_SWAP_CNTL__MP_REF8_MC_SWAP__SHIFT                                                              0x10
#define UVD_MP_SWAP_CNTL__MP_REF9_MC_SWAP__SHIFT                                                              0x12
#define UVD_MP_SWAP_CNTL__MP_REF10_MC_SWAP__SHIFT                                                             0x14
#define UVD_MP_SWAP_CNTL__MP_REF11_MC_SWAP__SHIFT                                                             0x16
#define UVD_MP_SWAP_CNTL__MP_REF12_MC_SWAP__SHIFT                                                             0x18
#define UVD_MP_SWAP_CNTL__MP_REF13_MC_SWAP__SHIFT                                                             0x1a
#define UVD_MP_SWAP_CNTL__MP_REF14_MC_SWAP__SHIFT                                                             0x1c
#define UVD_MP_SWAP_CNTL__MP_REF15_MC_SWAP__SHIFT                                                             0x1e
#define UVD_MP_SWAP_CNTL__MP_REF0_MC_SWAP_MASK                                                                0x00000003L
#define UVD_MP_SWAP_CNTL__MP_REF1_MC_SWAP_MASK                                                                0x0000000CL
#define UVD_MP_SWAP_CNTL__MP_REF2_MC_SWAP_MASK                                                                0x00000030L
#define UVD_MP_SWAP_CNTL__MP_REF3_MC_SWAP_MASK                                                                0x000000C0L
#define UVD_MP_SWAP_CNTL__MP_REF4_MC_SWAP_MASK                                                                0x00000300L
#define UVD_MP_SWAP_CNTL__MP_REF5_MC_SWAP_MASK                                                                0x00000C00L
#define UVD_MP_SWAP_CNTL__MP_REF6_MC_SWAP_MASK                                                                0x00003000L
#define UVD_MP_SWAP_CNTL__MP_REF7_MC_SWAP_MASK                                                                0x0000C000L
#define UVD_MP_SWAP_CNTL__MP_REF8_MC_SWAP_MASK                                                                0x00030000L
#define UVD_MP_SWAP_CNTL__MP_REF9_MC_SWAP_MASK                                                                0x000C0000L
#define UVD_MP_SWAP_CNTL__MP_REF10_MC_SWAP_MASK                                                               0x00300000L
#define UVD_MP_SWAP_CNTL__MP_REF11_MC_SWAP_MASK                                                               0x00C00000L
#define UVD_MP_SWAP_CNTL__MP_REF12_MC_SWAP_MASK                                                               0x03000000L
#define UVD_MP_SWAP_CNTL__MP_REF13_MC_SWAP_MASK                                                               0x0C000000L
#define UVD_MP_SWAP_CNTL__MP_REF14_MC_SWAP_MASK                                                               0x30000000L
#define UVD_MP_SWAP_CNTL__MP_REF15_MC_SWAP_MASK                                                               0xC0000000L
//UVD_MPC_LUMA_SRCH
#define UVD_MPC_LUMA_SRCH__CNTR__SHIFT                                                                        0x0
#define UVD_MPC_LUMA_SRCH__CNTR_MASK                                                                          0xFFFFFFFFL
//UVD_MPC_LUMA_HIT
#define UVD_MPC_LUMA_HIT__CNTR__SHIFT                                                                         0x0
#define UVD_MPC_LUMA_HIT__CNTR_MASK                                                                           0xFFFFFFFFL
//UVD_MPC_LUMA_HITPEND
#define UVD_MPC_LUMA_HITPEND__CNTR__SHIFT                                                                     0x0
#define UVD_MPC_LUMA_HITPEND__CNTR_MASK                                                                       0xFFFFFFFFL
//UVD_MPC_CHROMA_SRCH
#define UVD_MPC_CHROMA_SRCH__CNTR__SHIFT                                                                      0x0
#define UVD_MPC_CHROMA_SRCH__CNTR_MASK                                                                        0xFFFFFFFFL
//UVD_MPC_CHROMA_HIT
#define UVD_MPC_CHROMA_HIT__CNTR__SHIFT                                                                       0x0
#define UVD_MPC_CHROMA_HIT__CNTR_MASK                                                                         0xFFFFFFFFL
//UVD_MPC_CHROMA_HITPEND
#define UVD_MPC_CHROMA_HITPEND__CNTR__SHIFT                                                                   0x0
#define UVD_MPC_CHROMA_HITPEND__CNTR_MASK                                                                     0xFFFFFFFFL
//UVD_MPC_CNTL
#define UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT                                                                 0x3
#define UVD_MPC_CNTL__PERF_RST__SHIFT                                                                         0x6
#define UVD_MPC_CNTL__AVE_WEIGHT__SHIFT                                                                       0x10
#define UVD_MPC_CNTL__URGENT_EN__SHIFT                                                                        0x12
#define UVD_MPC_CNTL__SMPAT_REQ_SPEED_UP__SHIFT                                                               0x13
#define UVD_MPC_CNTL__TEST_MODE_EN__SHIFT                                                                     0x14
#define UVD_MPC_CNTL__REPLACEMENT_MODE_MASK                                                                   0x00000038L
#define UVD_MPC_CNTL__PERF_RST_MASK                                                                           0x00000040L
#define UVD_MPC_CNTL__AVE_WEIGHT_MASK                                                                         0x00030000L
#define UVD_MPC_CNTL__URGENT_EN_MASK                                                                          0x00040000L
#define UVD_MPC_CNTL__SMPAT_REQ_SPEED_UP_MASK                                                                 0x00080000L
#define UVD_MPC_CNTL__TEST_MODE_EN_MASK                                                                       0x00100000L
//UVD_MPC_PITCH
#define UVD_MPC_PITCH__LUMA_PITCH__SHIFT                                                                      0x0
#define UVD_MPC_PITCH__LUMA_PITCH_MASK                                                                        0x000007FFL
//UVD_MPC_SET_MUXA0
#define UVD_MPC_SET_MUXA0__VARA_0__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXA0__VARA_1__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXA0__VARA_2__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXA0__VARA_3__SHIFT                                                                      0x12
#define UVD_MPC_SET_MUXA0__VARA_4__SHIFT                                                                      0x18
#define UVD_MPC_SET_MUXA0__VARA_0_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXA0__VARA_1_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXA0__VARA_2_MASK                                                                        0x0003F000L
#define UVD_MPC_SET_MUXA0__VARA_3_MASK                                                                        0x00FC0000L
#define UVD_MPC_SET_MUXA0__VARA_4_MASK                                                                        0x3F000000L
//UVD_MPC_SET_MUXA1
#define UVD_MPC_SET_MUXA1__VARA_5__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXA1__VARA_6__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXA1__VARA_7__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXA1__VARA_5_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXA1__VARA_6_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXA1__VARA_7_MASK                                                                        0x0003F000L
//UVD_MPC_SET_MUXB0
#define UVD_MPC_SET_MUXB0__VARB_0__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXB0__VARB_1__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXB0__VARB_2__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXB0__VARB_3__SHIFT                                                                      0x12
#define UVD_MPC_SET_MUXB0__VARB_4__SHIFT                                                                      0x18
#define UVD_MPC_SET_MUXB0__VARB_0_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXB0__VARB_1_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXB0__VARB_2_MASK                                                                        0x0003F000L
#define UVD_MPC_SET_MUXB0__VARB_3_MASK                                                                        0x00FC0000L
#define UVD_MPC_SET_MUXB0__VARB_4_MASK                                                                        0x3F000000L
//UVD_MPC_SET_MUXB1
#define UVD_MPC_SET_MUXB1__VARB_5__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXB1__VARB_6__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXB1__VARB_7__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXB1__VARB_5_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXB1__VARB_6_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXB1__VARB_7_MASK                                                                        0x0003F000L
//UVD_MPC_SET_MUX
#define UVD_MPC_SET_MUX__SET_0__SHIFT                                                                         0x0
#define UVD_MPC_SET_MUX__SET_1__SHIFT                                                                         0x3
#define UVD_MPC_SET_MUX__SET_2__SHIFT                                                                         0x6
#define UVD_MPC_SET_MUX__SET_0_MASK                                                                           0x00000007L
#define UVD_MPC_SET_MUX__SET_1_MASK                                                                           0x00000038L
#define UVD_MPC_SET_MUX__SET_2_MASK                                                                           0x000001C0L
//UVD_MPC_SET_ALU
#define UVD_MPC_SET_ALU__FUNCT__SHIFT                                                                         0x0
#define UVD_MPC_SET_ALU__OPERAND__SHIFT                                                                       0x4
#define UVD_MPC_SET_ALU__FUNCT_MASK                                                                           0x00000007L
#define UVD_MPC_SET_ALU__OPERAND_MASK                                                                         0x00000FF0L
//UVD_MPC_PERF0
#define UVD_MPC_PERF0__MAX_LAT__SHIFT                                                                         0x0
#define UVD_MPC_PERF0__MAX_LAT_MASK                                                                           0x000003FFL
//UVD_MPC_PERF1
#define UVD_MPC_PERF1__AVE_LAT__SHIFT                                                                         0x0
#define UVD_MPC_PERF1__AVE_LAT_MASK                                                                           0x000003FFL


// addressBlock: uvd0_uvd_rbcdec
//UVD_RBC_IB_SIZE
#define UVD_RBC_IB_SIZE__IB_SIZE__SHIFT                                                                       0x4
#define UVD_RBC_IB_SIZE__IB_SIZE_MASK                                                                         0x007FFFF0L
//UVD_RBC_IB_SIZE_UPDATE
#define UVD_RBC_IB_SIZE_UPDATE__REMAIN_IB_SIZE__SHIFT                                                         0x4
#define UVD_RBC_IB_SIZE_UPDATE__REMAIN_IB_SIZE_MASK                                                           0x007FFFF0L
//UVD_RBC_RB_CNTL
#define UVD_RBC_RB_CNTL__RB_BUFSZ__SHIFT                                                                      0x0
#define UVD_RBC_RB_CNTL__RB_BLKSZ__SHIFT                                                                      0x8
#define UVD_RBC_RB_CNTL__RB_NO_FETCH__SHIFT                                                                   0x10
#define UVD_RBC_RB_CNTL__RB_WPTR_POLL_EN__SHIFT                                                               0x14
#define UVD_RBC_RB_CNTL__RB_NO_UPDATE__SHIFT                                                                  0x18
#define UVD_RBC_RB_CNTL__RB_RPTR_WR_EN__SHIFT                                                                 0x1c
#define UVD_RBC_RB_CNTL__RB_BUFSZ_MASK                                                                        0x0000001FL
#define UVD_RBC_RB_CNTL__RB_BLKSZ_MASK                                                                        0x00001F00L
#define UVD_RBC_RB_CNTL__RB_NO_FETCH_MASK                                                                     0x00010000L
#define UVD_RBC_RB_CNTL__RB_WPTR_POLL_EN_MASK                                                                 0x00100000L
#define UVD_RBC_RB_CNTL__RB_NO_UPDATE_MASK                                                                    0x01000000L
#define UVD_RBC_RB_CNTL__RB_RPTR_WR_EN_MASK                                                                   0x10000000L
//UVD_RBC_RB_RPTR_ADDR
#define UVD_RBC_RB_RPTR_ADDR__RB_RPTR_ADDR__SHIFT                                                             0x0
#define UVD_RBC_RB_RPTR_ADDR__RB_RPTR_ADDR_MASK                                                               0xFFFFFFFFL
//UVD_RBC_RB_RPTR
#define UVD_RBC_RB_RPTR__RB_RPTR__SHIFT                                                                       0x4
#define UVD_RBC_RB_RPTR__RB_RPTR_MASK                                                                         0x007FFFF0L
//UVD_RBC_RB_WPTR
#define UVD_RBC_RB_WPTR__RB_WPTR__SHIFT                                                                       0x4
#define UVD_RBC_RB_WPTR__RB_WPTR_MASK                                                                         0x007FFFF0L
//UVD_RBC_VCPU_ACCESS
#define UVD_RBC_VCPU_ACCESS__ENABLE_RBC__SHIFT                                                                0x0
#define UVD_RBC_VCPU_ACCESS__ENABLE_RBC_MASK                                                                  0x00000001L
//UVD_RBC_READ_REQ_URGENT_CNTL
#define UVD_RBC_READ_REQ_URGENT_CNTL__CMD_READ_REQ_PRIORITY_MARK__SHIFT                                       0x0
#define UVD_RBC_READ_REQ_URGENT_CNTL__CMD_READ_REQ_PRIORITY_MARK_MASK                                         0x00000003L
//UVD_RBC_RB_WPTR_CNTL
#define UVD_RBC_RB_WPTR_CNTL__RB_PRE_WRITE_TIMER__SHIFT                                                       0x0
#define UVD_RBC_RB_WPTR_CNTL__RB_PRE_WRITE_TIMER_MASK                                                         0x00007FFFL
//UVD_RBC_WPTR_STATUS
#define UVD_RBC_WPTR_STATUS__RB_WPTR_IN_USE__SHIFT                                                            0x4
#define UVD_RBC_WPTR_STATUS__RB_WPTR_IN_USE_MASK                                                              0x007FFFF0L
//UVD_RBC_WPTR_POLL_CNTL
#define UVD_RBC_WPTR_POLL_CNTL__POLL_FREQ__SHIFT                                                              0x0
#define UVD_RBC_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT                                                        0x10
#define UVD_RBC_WPTR_POLL_CNTL__POLL_FREQ_MASK                                                                0x0000FFFFL
#define UVD_RBC_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK                                                          0xFFFF0000L
//UVD_RBC_WPTR_POLL_ADDR
#define UVD_RBC_WPTR_POLL_ADDR__POLL_ADDR__SHIFT                                                              0x2
#define UVD_RBC_WPTR_POLL_ADDR__POLL_ADDR_MASK                                                                0xFFFFFFFCL
//UVD_SEMA_CMD
#define UVD_SEMA_CMD__REQ_CMD__SHIFT                                                                          0x0
#define UVD_SEMA_CMD__WR_PHASE__SHIFT                                                                         0x4
#define UVD_SEMA_CMD__MODE__SHIFT                                                                             0x6
#define UVD_SEMA_CMD__VMID_EN__SHIFT                                                                          0x7
#define UVD_SEMA_CMD__VMID__SHIFT                                                                             0x8
#define UVD_SEMA_CMD__REQ_CMD_MASK                                                                            0x0000000FL
#define UVD_SEMA_CMD__WR_PHASE_MASK                                                                           0x00000030L
#define UVD_SEMA_CMD__MODE_MASK                                                                               0x00000040L
#define UVD_SEMA_CMD__VMID_EN_MASK                                                                            0x00000080L
#define UVD_SEMA_CMD__VMID_MASK                                                                               0x00000F00L
//UVD_SEMA_ADDR_LOW
#define UVD_SEMA_ADDR_LOW__ADDR_26_3__SHIFT                                                                   0x0
#define UVD_SEMA_ADDR_LOW__ADDR_26_3_MASK                                                                     0x00FFFFFFL
//UVD_SEMA_ADDR_HIGH
#define UVD_SEMA_ADDR_HIGH__ADDR_47_27__SHIFT                                                                 0x0
#define UVD_SEMA_ADDR_HIGH__ADDR_47_27_MASK                                                                   0x001FFFFFL
//UVD_ENGINE_CNTL
#define UVD_ENGINE_CNTL__ENGINE_START__SHIFT                                                                  0x0
#define UVD_ENGINE_CNTL__ENGINE_START_MODE__SHIFT                                                             0x1
#define UVD_ENGINE_CNTL__NJ_PF_HANDLE_DISABLE__SHIFT                                                          0x2
#define UVD_ENGINE_CNTL__ENGINE_START_MASK                                                                    0x00000001L
#define UVD_ENGINE_CNTL__ENGINE_START_MODE_MASK                                                               0x00000002L
#define UVD_ENGINE_CNTL__NJ_PF_HANDLE_DISABLE_MASK                                                            0x00000004L
//UVD_SEMA_TIMEOUT_STATUS
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_INCOMPLETE_TIMEOUT_STAT__SHIFT                                0x0
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_FAULT_TIMEOUT_STAT__SHIFT                                     0x1
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_SIGNAL_INCOMPLETE_TIMEOUT_STAT__SHIFT                              0x2
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_TIMEOUT_CLEAR__SHIFT                                               0x3
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_INCOMPLETE_TIMEOUT_STAT_MASK                                  0x00000001L
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_FAULT_TIMEOUT_STAT_MASK                                       0x00000002L
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_SIGNAL_INCOMPLETE_TIMEOUT_STAT_MASK                                0x00000004L
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_TIMEOUT_CLEAR_MASK                                                 0x00000008L
//UVD_SEMA_CNTL
#define UVD_SEMA_CNTL__SEMAPHORE_EN__SHIFT                                                                    0x0
#define UVD_SEMA_CNTL__ADVANCED_MODE_DIS__SHIFT                                                               0x1
#define UVD_SEMA_CNTL__SEMAPHORE_EN_MASK                                                                      0x00000001L
#define UVD_SEMA_CNTL__ADVANCED_MODE_DIS_MASK                                                                 0x00000002L
//UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_EN__SHIFT                                  0x0
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_COUNT__SHIFT                               0x1
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER__SHIFT                                          0x18
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_EN_MASK                                    0x00000001L
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_COUNT_MASK                                 0x001FFFFEL
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER_MASK                                            0x07000000L
//UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_EN__SHIFT                                                0x0
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_COUNT__SHIFT                                             0x1
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__RESEND_TIMER__SHIFT                                                 0x18
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_EN_MASK                                                  0x00000001L
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_COUNT_MASK                                               0x001FFFFEL
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__RESEND_TIMER_MASK                                                   0x07000000L
//UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_EN__SHIFT                                      0x0
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_COUNT__SHIFT                                   0x1
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER__SHIFT                                            0x18
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_EN_MASK                                        0x00000001L
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_COUNT_MASK                                     0x001FFFFEL
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER_MASK                                              0x07000000L
//UVD_JOB_START
#define UVD_JOB_START__JOB_START__SHIFT                                                                       0x0
#define UVD_JOB_START__JOB_START_MASK                                                                         0x00000001L
//UVD_RBC_BUF_STATUS
#define UVD_RBC_BUF_STATUS__RB_BUF_VALID__SHIFT                                                               0x0
#define UVD_RBC_BUF_STATUS__IB_BUF_VALID__SHIFT                                                               0x8
#define UVD_RBC_BUF_STATUS__RB_BUF_RD_ADDR__SHIFT                                                             0x10
#define UVD_RBC_BUF_STATUS__IB_BUF_RD_ADDR__SHIFT                                                             0x13
#define UVD_RBC_BUF_STATUS__RB_BUF_WR_ADDR__SHIFT                                                             0x16
#define UVD_RBC_BUF_STATUS__IB_BUF_WR_ADDR__SHIFT                                                             0x19
#define UVD_RBC_BUF_STATUS__RB_BUF_VALID_MASK                                                                 0x000000FFL
#define UVD_RBC_BUF_STATUS__IB_BUF_VALID_MASK                                                                 0x0000FF00L
#define UVD_RBC_BUF_STATUS__RB_BUF_RD_ADDR_MASK                                                               0x00070000L
#define UVD_RBC_BUF_STATUS__IB_BUF_RD_ADDR_MASK                                                               0x00380000L
#define UVD_RBC_BUF_STATUS__RB_BUF_WR_ADDR_MASK                                                               0x01C00000L
#define UVD_RBC_BUF_STATUS__IB_BUF_WR_ADDR_MASK                                                               0x0E000000L


// addressBlock: uvd0_uvdgendec
//UVD_LCM_CGC_CNTRL
#define UVD_LCM_CGC_CNTRL__FORCE_OFF__SHIFT                                                                   0x12
#define UVD_LCM_CGC_CNTRL__FORCE_ON__SHIFT                                                                    0x13
#define UVD_LCM_CGC_CNTRL__OFF_DELAY__SHIFT                                                                   0x14
#define UVD_LCM_CGC_CNTRL__ON_DELAY__SHIFT                                                                    0x1c
#define UVD_LCM_CGC_CNTRL__FORCE_OFF_MASK                                                                     0x00040000L
#define UVD_LCM_CGC_CNTRL__FORCE_ON_MASK                                                                      0x00080000L
#define UVD_LCM_CGC_CNTRL__OFF_DELAY_MASK                                                                     0x0FF00000L
#define UVD_LCM_CGC_CNTRL__ON_DELAY_MASK                                                                      0xF0000000L


// addressBlock: uvd0_lmi_adpdec
//UVD_LMI_RBC_RB_64BIT_BAR_LOW
#define UVD_LMI_RBC_RB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                        0x0
#define UVD_LMI_RBC_RB_64BIT_BAR_LOW__BITS_31_0_MASK                                                          0xFFFFFFFFL
//UVD_LMI_RBC_RB_64BIT_BAR_HIGH
#define UVD_LMI_RBC_RB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                      0x0
#define UVD_LMI_RBC_RB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                        0xFFFFFFFFL
//UVD_LMI_RBC_IB_64BIT_BAR_LOW
#define UVD_LMI_RBC_IB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                        0x0
#define UVD_LMI_RBC_IB_64BIT_BAR_LOW__BITS_31_0_MASK                                                          0xFFFFFFFFL
//UVD_LMI_RBC_IB_64BIT_BAR_HIGH
#define UVD_LMI_RBC_IB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                      0x0
#define UVD_LMI_RBC_IB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                        0xFFFFFFFFL
//UVD_LMI_LBSI_64BIT_BAR_LOW
#define UVD_LMI_LBSI_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                          0x0
#define UVD_LMI_LBSI_64BIT_BAR_LOW__BITS_31_0_MASK                                                            0xFFFFFFFFL
//UVD_LMI_LBSI_64BIT_BAR_HIGH
#define UVD_LMI_LBSI_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                        0x0
#define UVD_LMI_LBSI_64BIT_BAR_HIGH__BITS_63_32_MASK                                                          0xFFFFFFFFL
//UVD_LMI_VCPU_NC0_64BIT_BAR_LOW
#define UVD_LMI_VCPU_NC0_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                      0x0
#define UVD_LMI_VCPU_NC0_64BIT_BAR_LOW__BITS_31_0_MASK                                                        0xFFFFFFFFL
//UVD_LMI_VCPU_NC0_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_NC0_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                    0x0
#define UVD_LMI_VCPU_NC0_64BIT_BAR_HIGH__BITS_63_32_MASK                                                      0xFFFFFFFFL
//UVD_LMI_VCPU_NC1_64BIT_BAR_LOW
#define UVD_LMI_VCPU_NC1_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                      0x0
#define UVD_LMI_VCPU_NC1_64BIT_BAR_LOW__BITS_31_0_MASK                                                        0xFFFFFFFFL
//UVD_LMI_VCPU_NC1_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_NC1_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                    0x0
#define UVD_LMI_VCPU_NC1_64BIT_BAR_HIGH__BITS_63_32_MASK                                                      0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                    0x0
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0_MASK                                                      0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                  0x0
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                    0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE8_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE8_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE8_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE8_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE8_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE8_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE3_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE3_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE3_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE3_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE3_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE3_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE4_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE4_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE4_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE4_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE4_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE4_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE5_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE5_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE5_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE5_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE5_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE5_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE6_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE6_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE6_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE6_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE6_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE6_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE7_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE7_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE7_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE7_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE7_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE7_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_MMSCH_NC0_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC0_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC0_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC0_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC0_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC0_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC1_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC1_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC1_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC1_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC1_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC1_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC2_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC2_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC2_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC2_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC2_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC2_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC3_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC3_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC3_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC3_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC3_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC3_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC4_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC4_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC4_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC4_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC4_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC4_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC5_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC5_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC5_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC5_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC5_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC5_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC6_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC6_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC6_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC6_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC6_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC6_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC7_64BIT_BAR_LOW
#define UVD_LMI_MMSCH_NC7_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                     0x0
#define UVD_LMI_MMSCH_NC7_64BIT_BAR_LOW__BITS_31_0_MASK                                                       0xFFFFFFFFL
//UVD_LMI_MMSCH_NC7_64BIT_BAR_HIGH
#define UVD_LMI_MMSCH_NC7_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                   0x0
#define UVD_LMI_MMSCH_NC7_64BIT_BAR_HIGH__BITS_63_32_MASK                                                     0xFFFFFFFFL
//UVD_LMI_MMSCH_NC_VMID
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC0_VMID__SHIFT                                                          0x0
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC1_VMID__SHIFT                                                          0x4
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC2_VMID__SHIFT                                                          0x8
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC3_VMID__SHIFT                                                          0xc
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC4_VMID__SHIFT                                                          0x10
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC5_VMID__SHIFT                                                          0x14
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC6_VMID__SHIFT                                                          0x18
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC7_VMID__SHIFT                                                          0x1c
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC0_VMID_MASK                                                            0x0000000FL
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC1_VMID_MASK                                                            0x000000F0L
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC2_VMID_MASK                                                            0x00000F00L
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC3_VMID_MASK                                                            0x0000F000L
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC4_VMID_MASK                                                            0x000F0000L
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC5_VMID_MASK                                                            0x00F00000L
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC6_VMID_MASK                                                            0x0F000000L
#define UVD_LMI_MMSCH_NC_VMID__MMSCH_NC7_VMID_MASK                                                            0xF0000000L
//UVD_LMI_MMSCH_CTRL
#define UVD_LMI_MMSCH_CTRL__MMSCH_DATA_COHERENCY_EN__SHIFT                                                    0x0
#define UVD_LMI_MMSCH_CTRL__MMSCH_VM__SHIFT                                                                   0x1
#define UVD_LMI_MMSCH_CTRL__MMSCH_R_MC_SWAP__SHIFT                                                            0x3
#define UVD_LMI_MMSCH_CTRL__MMSCH_W_MC_SWAP__SHIFT                                                            0x5
#define UVD_LMI_MMSCH_CTRL__MMSCH_RD__SHIFT                                                                   0x7
#define UVD_LMI_MMSCH_CTRL__MMSCH_WR__SHIFT                                                                   0x9
#define UVD_LMI_MMSCH_CTRL__MMSCH_RD_DROP__SHIFT                                                              0xb
#define UVD_LMI_MMSCH_CTRL__MMSCH_WR_DROP__SHIFT                                                              0xc
#define UVD_LMI_MMSCH_CTRL__MMSCH_DATA_COHERENCY_EN_MASK                                                      0x00000001L
#define UVD_LMI_MMSCH_CTRL__MMSCH_VM_MASK                                                                     0x00000002L
#define UVD_LMI_MMSCH_CTRL__MMSCH_R_MC_SWAP_MASK                                                              0x00000018L
#define UVD_LMI_MMSCH_CTRL__MMSCH_W_MC_SWAP_MASK                                                              0x00000060L
#define UVD_LMI_MMSCH_CTRL__MMSCH_RD_MASK                                                                     0x00000180L
#define UVD_LMI_MMSCH_CTRL__MMSCH_WR_MASK                                                                     0x00000600L
#define UVD_LMI_MMSCH_CTRL__MMSCH_RD_DROP_MASK                                                                0x00000800L
#define UVD_LMI_MMSCH_CTRL__MMSCH_WR_DROP_MASK                                                                0x00001000L
//UVD_LMI_ARB_CTRL2
#define UVD_LMI_ARB_CTRL2__CENC_RD_WAIT_EN__SHIFT                                                             0x0
#define UVD_LMI_ARB_CTRL2__ATOMIC_WR_WAIT_EN__SHIFT                                                           0x1
#define UVD_LMI_ARB_CTRL2__CENC_RD_MAX_BURST__SHIFT                                                           0x2
#define UVD_LMI_ARB_CTRL2__ATOMIC_WR_MAX_BURST__SHIFT                                                         0x6
#define UVD_LMI_ARB_CTRL2__MIF_RD_REQ_RET_MAX__SHIFT                                                          0xa
#define UVD_LMI_ARB_CTRL2__MIF_WR_REQ_RET_MAX__SHIFT                                                          0x14
#define UVD_LMI_ARB_CTRL2__CENC_RD_WAIT_EN_MASK                                                               0x00000001L
#define UVD_LMI_ARB_CTRL2__ATOMIC_WR_WAIT_EN_MASK                                                             0x00000002L
#define UVD_LMI_ARB_CTRL2__CENC_RD_MAX_BURST_MASK                                                             0x0000003CL
#define UVD_LMI_ARB_CTRL2__ATOMIC_WR_MAX_BURST_MASK                                                           0x000003C0L
#define UVD_LMI_ARB_CTRL2__MIF_RD_REQ_RET_MAX_MASK                                                            0x000FFC00L
#define UVD_LMI_ARB_CTRL2__MIF_WR_REQ_RET_MAX_MASK                                                            0xFFF00000L
//UVD_LMI_VCPU_CACHE_VMIDS_MULTI
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE1_VMID__SHIFT                                               0x0
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE2_VMID__SHIFT                                               0x4
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE3_VMID__SHIFT                                               0x8
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE4_VMID__SHIFT                                               0xc
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE5_VMID__SHIFT                                               0x10
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE6_VMID__SHIFT                                               0x14
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE7_VMID__SHIFT                                               0x18
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE8_VMID__SHIFT                                               0x1c
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE1_VMID_MASK                                                 0x0000000FL
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE2_VMID_MASK                                                 0x000000F0L
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE3_VMID_MASK                                                 0x00000F00L
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE4_VMID_MASK                                                 0x0000F000L
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE5_VMID_MASK                                                 0x000F0000L
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE6_VMID_MASK                                                 0x00F00000L
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE7_VMID_MASK                                                 0x0F000000L
#define UVD_LMI_VCPU_CACHE_VMIDS_MULTI__VCPU_CACHE8_VMID_MASK                                                 0xF0000000L
//UVD_LMI_VCPU_NC_VMIDS_MULTI
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC2_VMID__SHIFT                                                     0x4
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC3_VMID__SHIFT                                                     0x8
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC4_VMID__SHIFT                                                     0xc
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC5_VMID__SHIFT                                                     0x10
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC6_VMID__SHIFT                                                     0x14
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC7_VMID__SHIFT                                                     0x18
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC2_VMID_MASK                                                       0x000000F0L
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC3_VMID_MASK                                                       0x00000F00L
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC4_VMID_MASK                                                       0x0000F000L
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC5_VMID_MASK                                                       0x000F0000L
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC6_VMID_MASK                                                       0x00F00000L
#define UVD_LMI_VCPU_NC_VMIDS_MULTI__VCPU_NC7_VMID_MASK                                                       0x0F000000L
//UVD_LMI_LAT_CTRL
#define UVD_LMI_LAT_CTRL__SCALE__SHIFT                                                                        0x0
#define UVD_LMI_LAT_CTRL__MAX_START__SHIFT                                                                    0x8
#define UVD_LMI_LAT_CTRL__MIN_START__SHIFT                                                                    0x9
#define UVD_LMI_LAT_CTRL__AVG_START__SHIFT                                                                    0xa
#define UVD_LMI_LAT_CTRL__PERFMON_SYNC__SHIFT                                                                 0xb
#define UVD_LMI_LAT_CTRL__SKIP__SHIFT                                                                         0x10
#define UVD_LMI_LAT_CTRL__SCALE_MASK                                                                          0x000000FFL
#define UVD_LMI_LAT_CTRL__MAX_START_MASK                                                                      0x00000100L
#define UVD_LMI_LAT_CTRL__MIN_START_MASK                                                                      0x00000200L
#define UVD_LMI_LAT_CTRL__AVG_START_MASK                                                                      0x00000400L
#define UVD_LMI_LAT_CTRL__PERFMON_SYNC_MASK                                                                   0x00000800L
#define UVD_LMI_LAT_CTRL__SKIP_MASK                                                                           0x000F0000L
//UVD_LMI_LAT_CNTR
#define UVD_LMI_LAT_CNTR__MAX_LAT__SHIFT                                                                      0x0
#define UVD_LMI_LAT_CNTR__MIN_LAT__SHIFT                                                                      0x8
#define UVD_LMI_LAT_CNTR__MAX_LAT_MASK                                                                        0x000000FFL
#define UVD_LMI_LAT_CNTR__MIN_LAT_MASK                                                                        0x0000FF00L
//UVD_LMI_AVG_LAT_CNTR
#define UVD_LMI_AVG_LAT_CNTR__ENV_LOW__SHIFT                                                                  0x0
#define UVD_LMI_AVG_LAT_CNTR__ENV_HIGH__SHIFT                                                                 0x8
#define UVD_LMI_AVG_LAT_CNTR__ENV_HIT__SHIFT                                                                  0x10
#define UVD_LMI_AVG_LAT_CNTR__ENV_LOW_MASK                                                                    0x000000FFL
#define UVD_LMI_AVG_LAT_CNTR__ENV_HIGH_MASK                                                                   0x0000FF00L
#define UVD_LMI_AVG_LAT_CNTR__ENV_HIT_MASK                                                                    0xFFFF0000L
//UVD_LMI_SPH
#define UVD_LMI_SPH__ADDR__SHIFT                                                                              0x0
#define UVD_LMI_SPH__STS__SHIFT                                                                               0x1c
#define UVD_LMI_SPH__STS_VALID__SHIFT                                                                         0x1e
#define UVD_LMI_SPH__STS_OVERFLOW__SHIFT                                                                      0x1f
#define UVD_LMI_SPH__ADDR_MASK                                                                                0x0FFFFFFFL
#define UVD_LMI_SPH__STS_MASK                                                                                 0x30000000L
#define UVD_LMI_SPH__STS_VALID_MASK                                                                           0x40000000L
#define UVD_LMI_SPH__STS_OVERFLOW_MASK                                                                        0x80000000L
//UVD_LMI_VCPU_CACHE_VMID
#define UVD_LMI_VCPU_CACHE_VMID__VCPU_CACHE_VMID__SHIFT                                                       0x0
#define UVD_LMI_VCPU_CACHE_VMID__VCPU_CACHE_VMID_MASK                                                         0x0000000FL
//UVD_LMI_CTRL2
#define UVD_LMI_CTRL2__SPH_DIS__SHIFT                                                                         0x0
#define UVD_LMI_CTRL2__STALL_ARB__SHIFT                                                                       0x1
#define UVD_LMI_CTRL2__ASSERT_UMC_URGENT__SHIFT                                                               0x2
#define UVD_LMI_CTRL2__MASK_UMC_URGENT__SHIFT                                                                 0x3
#define UVD_LMI_CTRL2__CRC1_RESET__SHIFT                                                                      0x4
#define UVD_LMI_CTRL2__DRCITF_BUBBLE_FIX_DIS__SHIFT                                                           0x7
#define UVD_LMI_CTRL2__STALL_ARB_UMC__SHIFT                                                                   0x8
#define UVD_LMI_CTRL2__MC_READ_ID_SEL__SHIFT                                                                  0x9
#define UVD_LMI_CTRL2__MC_WRITE_ID_SEL__SHIFT                                                                 0xb
#define UVD_LMI_CTRL2__VCPU_NC0_EXT_EN__SHIFT                                                                 0xd
#define UVD_LMI_CTRL2__VCPU_NC1_EXT_EN__SHIFT                                                                 0xe
#define UVD_LMI_CTRL2__SPU_EXTRA_CID_EN__SHIFT                                                                0xf
#define UVD_LMI_CTRL2__RE_OFFLOAD_EN__SHIFT                                                                   0x10
#define UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT                                                          0x11
#define UVD_LMI_CTRL2__CLEAR_NJ_PF_BP__SHIFT                                                                  0x19
#define UVD_LMI_CTRL2__NJ_MIF_GATING__SHIFT                                                                   0x1a
#define UVD_LMI_CTRL2__CRC1_SEL__SHIFT                                                                        0x1b
#define UVD_LMI_CTRL2__SPH_DIS_MASK                                                                           0x00000001L
#define UVD_LMI_CTRL2__STALL_ARB_MASK                                                                         0x00000002L
#define UVD_LMI_CTRL2__ASSERT_UMC_URGENT_MASK                                                                 0x00000004L
#define UVD_LMI_CTRL2__MASK_UMC_URGENT_MASK                                                                   0x00000008L
#define UVD_LMI_CTRL2__CRC1_RESET_MASK                                                                        0x00000010L
#define UVD_LMI_CTRL2__DRCITF_BUBBLE_FIX_DIS_MASK                                                             0x00000080L
#define UVD_LMI_CTRL2__STALL_ARB_UMC_MASK                                                                     0x00000100L
#define UVD_LMI_CTRL2__MC_READ_ID_SEL_MASK                                                                    0x00000600L
#define UVD_LMI_CTRL2__MC_WRITE_ID_SEL_MASK                                                                   0x00001800L
#define UVD_LMI_CTRL2__VCPU_NC0_EXT_EN_MASK                                                                   0x00002000L
#define UVD_LMI_CTRL2__VCPU_NC1_EXT_EN_MASK                                                                   0x00004000L
#define UVD_LMI_CTRL2__SPU_EXTRA_CID_EN_MASK                                                                  0x00008000L
#define UVD_LMI_CTRL2__RE_OFFLOAD_EN_MASK                                                                     0x00010000L
#define UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM_MASK                                                            0x01FE0000L
#define UVD_LMI_CTRL2__CLEAR_NJ_PF_BP_MASK                                                                    0x02000000L
#define UVD_LMI_CTRL2__NJ_MIF_GATING_MASK                                                                     0x04000000L
#define UVD_LMI_CTRL2__CRC1_SEL_MASK                                                                          0xF8000000L
//UVD_LMI_URGENT_CTRL
#define UVD_LMI_URGENT_CTRL__ENABLE_MC_RD_URGENT_STALL__SHIFT                                                 0x0
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_RD_STALL__SHIFT                                                        0x1
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_RD_URGENT__SHIFT                                                       0x2
#define UVD_LMI_URGENT_CTRL__ENABLE_MC_WR_URGENT_STALL__SHIFT                                                 0x8
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_WR_STALL__SHIFT                                                        0x9
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_WR_URGENT__SHIFT                                                       0xa
#define UVD_LMI_URGENT_CTRL__ENABLE_UMC_RD_URGENT_STALL__SHIFT                                                0x10
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_RD_STALL__SHIFT                                                       0x11
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_RD_URGENT__SHIFT                                                      0x12
#define UVD_LMI_URGENT_CTRL__ENABLE_UMC_WR_URGENT_STALL__SHIFT                                                0x18
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_WR_STALL__SHIFT                                                       0x19
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_WR_URGENT__SHIFT                                                      0x1a
#define UVD_LMI_URGENT_CTRL__ENABLE_MC_RD_URGENT_STALL_MASK                                                   0x00000001L
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_RD_STALL_MASK                                                          0x00000002L
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_RD_URGENT_MASK                                                         0x0000003CL
#define UVD_LMI_URGENT_CTRL__ENABLE_MC_WR_URGENT_STALL_MASK                                                   0x00000100L
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_WR_STALL_MASK                                                          0x00000200L
#define UVD_LMI_URGENT_CTRL__ASSERT_MC_WR_URGENT_MASK                                                         0x00003C00L
#define UVD_LMI_URGENT_CTRL__ENABLE_UMC_RD_URGENT_STALL_MASK                                                  0x00010000L
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_RD_STALL_MASK                                                         0x00020000L
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_RD_URGENT_MASK                                                        0x003C0000L
#define UVD_LMI_URGENT_CTRL__ENABLE_UMC_WR_URGENT_STALL_MASK                                                  0x01000000L
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_WR_STALL_MASK                                                         0x02000000L
#define UVD_LMI_URGENT_CTRL__ASSERT_UMC_WR_URGENT_MASK                                                        0x3C000000L
//UVD_LMI_CTRL
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT                                                                0x0
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN__SHIFT                                                             0x8
#define UVD_LMI_CTRL__REQ_MODE__SHIFT                                                                         0x9
#define UVD_LMI_CTRL__ASSERT_MC_URGENT__SHIFT                                                                 0xb
#define UVD_LMI_CTRL__MASK_MC_URGENT__SHIFT                                                                   0xc
#define UVD_LMI_CTRL__DATA_COHERENCY_EN__SHIFT                                                                0xd
#define UVD_LMI_CTRL__CRC_RESET__SHIFT                                                                        0xe
#define UVD_LMI_CTRL__CRC_SEL__SHIFT                                                                          0xf
#define UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN__SHIFT                                                           0x15
#define UVD_LMI_CTRL__CM_DATA_COHERENCY_EN__SHIFT                                                             0x16
#define UVD_LMI_CTRL__DB_DB_DATA_COHERENCY_EN__SHIFT                                                          0x17
#define UVD_LMI_CTRL__DB_IT_DATA_COHERENCY_EN__SHIFT                                                          0x18
#define UVD_LMI_CTRL__IT_IT_DATA_COHERENCY_EN__SHIFT                                                          0x19
#define UVD_LMI_CTRL__MIF_MIF_DATA_COHERENCY_EN__SHIFT                                                        0x1a
#define UVD_LMI_CTRL__MIF_LESS_OUTSTANDING_RD_REQ__SHIFT                                                      0x1b
#define UVD_LMI_CTRL__RFU__SHIFT                                                                              0x1e
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER_MASK                                                                  0x000000FFL
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK                                                               0x00000100L
#define UVD_LMI_CTRL__REQ_MODE_MASK                                                                           0x00000200L
#define UVD_LMI_CTRL__ASSERT_MC_URGENT_MASK                                                                   0x00000800L
#define UVD_LMI_CTRL__MASK_MC_URGENT_MASK                                                                     0x00001000L
#define UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK                                                                  0x00002000L
#define UVD_LMI_CTRL__CRC_RESET_MASK                                                                          0x00004000L
#define UVD_LMI_CTRL__CRC_SEL_MASK                                                                            0x000F8000L
#define UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK                                                             0x00200000L
#define UVD_LMI_CTRL__CM_DATA_COHERENCY_EN_MASK                                                               0x00400000L
#define UVD_LMI_CTRL__DB_DB_DATA_COHERENCY_EN_MASK                                                            0x00800000L
#define UVD_LMI_CTRL__DB_IT_DATA_COHERENCY_EN_MASK                                                            0x01000000L
#define UVD_LMI_CTRL__IT_IT_DATA_COHERENCY_EN_MASK                                                            0x02000000L
#define UVD_LMI_CTRL__MIF_MIF_DATA_COHERENCY_EN_MASK                                                          0x04000000L
#define UVD_LMI_CTRL__MIF_LESS_OUTSTANDING_RD_REQ_MASK                                                        0x08000000L
#define UVD_LMI_CTRL__RFU_MASK                                                                                0xC0000000L
//UVD_LMI_STATUS
#define UVD_LMI_STATUS__READ_CLEAN__SHIFT                                                                     0x0
#define UVD_LMI_STATUS__WRITE_CLEAN__SHIFT                                                                    0x1
#define UVD_LMI_STATUS__WRITE_CLEAN_RAW__SHIFT                                                                0x2
#define UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN__SHIFT                                                           0x3
#define UVD_LMI_STATUS__UMC_READ_CLEAN__SHIFT                                                                 0x4
#define UVD_LMI_STATUS__UMC_WRITE_CLEAN__SHIFT                                                                0x5
#define UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW__SHIFT                                                            0x6
#define UVD_LMI_STATUS__PENDING_UVD_MC_WRITE__SHIFT                                                           0x7
#define UVD_LMI_STATUS__READ_CLEAN_RAW__SHIFT                                                                 0x8
#define UVD_LMI_STATUS__UMC_READ_CLEAN_RAW__SHIFT                                                             0x9
#define UVD_LMI_STATUS__UMC_UVD_IDLE__SHIFT                                                                   0xa
#define UVD_LMI_STATUS__UMC_AVP_IDLE__SHIFT                                                                   0xb
#define UVD_LMI_STATUS__ADP_MC_READ_CLEAN__SHIFT                                                              0xc
#define UVD_LMI_STATUS__ADP_UMC_READ_CLEAN__SHIFT                                                             0xd
#define UVD_LMI_STATUS__BSP0_WRITE_CLEAN__SHIFT                                                               0x12
#define UVD_LMI_STATUS__BSP1_WRITE_CLEAN__SHIFT                                                               0x13
#define UVD_LMI_STATUS__BSP2_WRITE_CLEAN__SHIFT                                                               0x14
#define UVD_LMI_STATUS__BSP3_WRITE_CLEAN__SHIFT                                                               0x15
#define UVD_LMI_STATUS__CENC_READ_CLEAN__SHIFT                                                                0x16
#define UVD_LMI_STATUS__READ_CLEAN_MASK                                                                       0x00000001L
#define UVD_LMI_STATUS__WRITE_CLEAN_MASK                                                                      0x00000002L
#define UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK                                                                  0x00000004L
#define UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK                                                             0x00000008L
#define UVD_LMI_STATUS__UMC_READ_CLEAN_MASK                                                                   0x00000010L
#define UVD_LMI_STATUS__UMC_WRITE_CLEAN_MASK                                                                  0x00000020L
#define UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK                                                              0x00000040L
#define UVD_LMI_STATUS__PENDING_UVD_MC_WRITE_MASK                                                             0x00000080L
#define UVD_LMI_STATUS__READ_CLEAN_RAW_MASK                                                                   0x00000100L
#define UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK                                                               0x00000200L
#define UVD_LMI_STATUS__UMC_UVD_IDLE_MASK                                                                     0x00000400L
#define UVD_LMI_STATUS__UMC_AVP_IDLE_MASK                                                                     0x00000800L
#define UVD_LMI_STATUS__ADP_MC_READ_CLEAN_MASK                                                                0x00001000L
#define UVD_LMI_STATUS__ADP_UMC_READ_CLEAN_MASK                                                               0x00002000L
#define UVD_LMI_STATUS__BSP0_WRITE_CLEAN_MASK                                                                 0x00040000L
#define UVD_LMI_STATUS__BSP1_WRITE_CLEAN_MASK                                                                 0x00080000L
#define UVD_LMI_STATUS__BSP2_WRITE_CLEAN_MASK                                                                 0x00100000L
#define UVD_LMI_STATUS__BSP3_WRITE_CLEAN_MASK                                                                 0x00200000L
#define UVD_LMI_STATUS__CENC_READ_CLEAN_MASK                                                                  0x00400000L
//UVD_LMI_PERFMON_CTRL
#define UVD_LMI_PERFMON_CTRL__PERFMON_STATE__SHIFT                                                            0x0
#define UVD_LMI_PERFMON_CTRL__PERFMON_SEL__SHIFT                                                              0x8
#define UVD_LMI_PERFMON_CTRL__PERFMON_STATE_MASK                                                              0x00000003L
#define UVD_LMI_PERFMON_CTRL__PERFMON_SEL_MASK                                                                0x00001F00L
//UVD_LMI_PERFMON_COUNT_LO
#define UVD_LMI_PERFMON_COUNT_LO__PERFMON_COUNT__SHIFT                                                        0x0
#define UVD_LMI_PERFMON_COUNT_LO__PERFMON_COUNT_MASK                                                          0xFFFFFFFFL
//UVD_LMI_PERFMON_COUNT_HI
#define UVD_LMI_PERFMON_COUNT_HI__PERFMON_COUNT__SHIFT                                                        0x0
#define UVD_LMI_PERFMON_COUNT_HI__PERFMON_COUNT_MASK                                                          0x0000FFFFL
//UVD_LMI_RBC_RB_VMID
#define UVD_LMI_RBC_RB_VMID__RB_VMID__SHIFT                                                                   0x0
#define UVD_LMI_RBC_RB_VMID__RB_VMID_MASK                                                                     0x0000000FL
//UVD_LMI_RBC_IB_VMID
#define UVD_LMI_RBC_IB_VMID__IB_VMID__SHIFT                                                                   0x0
#define UVD_LMI_RBC_IB_VMID__IB_VMID_MASK                                                                     0x0000000FL
//UVD_LMI_MC_CREDITS
#define UVD_LMI_MC_CREDITS__UVD_RD_CREDITS__SHIFT                                                             0x0
#define UVD_LMI_MC_CREDITS__UVD_WR_CREDITS__SHIFT                                                             0x8
#define UVD_LMI_MC_CREDITS__UMC_RD_CREDITS__SHIFT                                                             0x10
#define UVD_LMI_MC_CREDITS__UMC_WR_CREDITS__SHIFT                                                             0x18
#define UVD_LMI_MC_CREDITS__UVD_RD_CREDITS_MASK                                                               0x0000003FL
#define UVD_LMI_MC_CREDITS__UVD_WR_CREDITS_MASK                                                               0x00003F00L
#define UVD_LMI_MC_CREDITS__UMC_RD_CREDITS_MASK                                                               0x003F0000L
#define UVD_LMI_MC_CREDITS__UMC_WR_CREDITS_MASK                                                               0x3F000000L


// addressBlock: uvd0_uvdnpdec
//MDM_DMA_CMD
#define MDM_DMA_CMD__MDM_DMA_CMD__SHIFT                                                                       0x0
#define MDM_DMA_CMD__MDM_DMA_CMD_MASK                                                                         0xFFFFFFFFL
//MDM_DMA_STATUS
#define MDM_DMA_STATUS__SDB_DMA_WR_BUSY__SHIFT                                                                0x0
#define MDM_DMA_STATUS__SCM_DMA_WR_BUSY__SHIFT                                                                0x1
#define MDM_DMA_STATUS__SCM_DMA_RD_BUSY__SHIFT                                                                0x2
#define MDM_DMA_STATUS__RB_DMA_WR_BUSY__SHIFT                                                                 0x3
#define MDM_DMA_STATUS__RB_DMA_RD_BUSY__SHIFT                                                                 0x4
#define MDM_DMA_STATUS__SDB_DMA_RD_BUSY__SHIFT                                                                0x5
#define MDM_DMA_STATUS__SCLR_DMA_WR_BUSY__SHIFT                                                               0x6
#define MDM_DMA_STATUS__SDB_DMA_WR_BUSY_MASK                                                                  0x00000001L
#define MDM_DMA_STATUS__SCM_DMA_WR_BUSY_MASK                                                                  0x00000002L
#define MDM_DMA_STATUS__SCM_DMA_RD_BUSY_MASK                                                                  0x00000004L
#define MDM_DMA_STATUS__RB_DMA_WR_BUSY_MASK                                                                   0x00000008L
#define MDM_DMA_STATUS__RB_DMA_RD_BUSY_MASK                                                                   0x00000010L
#define MDM_DMA_STATUS__SDB_DMA_RD_BUSY_MASK                                                                  0x00000020L
#define MDM_DMA_STATUS__SCLR_DMA_WR_BUSY_MASK                                                                 0x00000040L
//MDM_DMA_CTL
#define MDM_DMA_CTL__MDM_BYPASS__SHIFT                                                                        0x0
#define MDM_DMA_CTL__FOUR_CMD__SHIFT                                                                          0x1
#define MDM_DMA_CTL__ENCODE_MODE__SHIFT                                                                       0x2
#define MDM_DMA_CTL__VP9_DEC_MODE__SHIFT                                                                      0x3
#define MDM_DMA_CTL__SW_DRST__SHIFT                                                                           0x1f
#define MDM_DMA_CTL__MDM_BYPASS_MASK                                                                          0x00000001L
#define MDM_DMA_CTL__FOUR_CMD_MASK                                                                            0x00000002L
#define MDM_DMA_CTL__ENCODE_MODE_MASK                                                                         0x00000004L
#define MDM_DMA_CTL__VP9_DEC_MODE_MASK                                                                        0x00000008L
#define MDM_DMA_CTL__SW_DRST_MASK                                                                             0x80000000L
//MDM_ENC_PIPE_BUSY
#define MDM_ENC_PIPE_BUSY__IME_BUSY__SHIFT                                                                    0x0
#define MDM_ENC_PIPE_BUSY__SMP_BUSY__SHIFT                                                                    0x1
#define MDM_ENC_PIPE_BUSY__SIT_BUSY__SHIFT                                                                    0x2
#define MDM_ENC_PIPE_BUSY__SDB_BUSY__SHIFT                                                                    0x3
#define MDM_ENC_PIPE_BUSY__ENT_BUSY__SHIFT                                                                    0x4
#define MDM_ENC_PIPE_BUSY__ENT_HEADER_BUSY__SHIFT                                                             0x5
#define MDM_ENC_PIPE_BUSY__LCM_BUSY__SHIFT                                                                    0x6
#define MDM_ENC_PIPE_BUSY__MDM_RD_CUR_BUSY__SHIFT                                                             0x7
#define MDM_ENC_PIPE_BUSY__MDM_RD_REF_BUSY__SHIFT                                                             0x8
#define MDM_ENC_PIPE_BUSY__MDM_RD_GEN_BUSY__SHIFT                                                             0x9
#define MDM_ENC_PIPE_BUSY__MDM_WR_RECON_BUSY__SHIFT                                                           0xa
#define MDM_ENC_PIPE_BUSY__MDM_WR_GEN_BUSY__SHIFT                                                             0xb
#define MDM_ENC_PIPE_BUSY__MDM_EFC_BUSY__SHIFT                                                                0xc
#define MDM_ENC_PIPE_BUSY__MDM_EFC_PROGRAM_BUSY__SHIFT                                                        0xd
#define MDM_ENC_PIPE_BUSY__MIF_RD_CUR_BUSY__SHIFT                                                             0x10
#define MDM_ENC_PIPE_BUSY__MIF_RD_REF0_BUSY__SHIFT                                                            0x11
#define MDM_ENC_PIPE_BUSY__MIF_WR_GEN0_BUSY__SHIFT                                                            0x12
#define MDM_ENC_PIPE_BUSY__MIF_RD_GEN0_BUSY__SHIFT                                                            0x13
#define MDM_ENC_PIPE_BUSY__MIF_WR_GEN1_BUSY__SHIFT                                                            0x14
#define MDM_ENC_PIPE_BUSY__MIF_RD_GEN1_BUSY__SHIFT                                                            0x15
#define MDM_ENC_PIPE_BUSY__MIF_WR_BSP0_BUSY__SHIFT                                                            0x16
#define MDM_ENC_PIPE_BUSY__MIF_WR_BSP1_BUSY__SHIFT                                                            0x17
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD0_BUSY__SHIFT                                                            0x18
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD1_BUSY__SHIFT                                                            0x19
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD2_BUSY__SHIFT                                                            0x1a
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD3_BUSY__SHIFT                                                            0x1b
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD4_BUSY__SHIFT                                                            0x1c
#define MDM_ENC_PIPE_BUSY__IME_BUSY_MASK                                                                      0x00000001L
#define MDM_ENC_PIPE_BUSY__SMP_BUSY_MASK                                                                      0x00000002L
#define MDM_ENC_PIPE_BUSY__SIT_BUSY_MASK                                                                      0x00000004L
#define MDM_ENC_PIPE_BUSY__SDB_BUSY_MASK                                                                      0x00000008L
#define MDM_ENC_PIPE_BUSY__ENT_BUSY_MASK                                                                      0x00000010L
#define MDM_ENC_PIPE_BUSY__ENT_HEADER_BUSY_MASK                                                               0x00000020L
#define MDM_ENC_PIPE_BUSY__LCM_BUSY_MASK                                                                      0x00000040L
#define MDM_ENC_PIPE_BUSY__MDM_RD_CUR_BUSY_MASK                                                               0x00000080L
#define MDM_ENC_PIPE_BUSY__MDM_RD_REF_BUSY_MASK                                                               0x00000100L
#define MDM_ENC_PIPE_BUSY__MDM_RD_GEN_BUSY_MASK                                                               0x00000200L
#define MDM_ENC_PIPE_BUSY__MDM_WR_RECON_BUSY_MASK                                                             0x00000400L
#define MDM_ENC_PIPE_BUSY__MDM_WR_GEN_BUSY_MASK                                                               0x00000800L
#define MDM_ENC_PIPE_BUSY__MDM_EFC_BUSY_MASK                                                                  0x00001000L
#define MDM_ENC_PIPE_BUSY__MDM_EFC_PROGRAM_BUSY_MASK                                                          0x00002000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_CUR_BUSY_MASK                                                               0x00010000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_REF0_BUSY_MASK                                                              0x00020000L
#define MDM_ENC_PIPE_BUSY__MIF_WR_GEN0_BUSY_MASK                                                              0x00040000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_GEN0_BUSY_MASK                                                              0x00080000L
#define MDM_ENC_PIPE_BUSY__MIF_WR_GEN1_BUSY_MASK                                                              0x00100000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_GEN1_BUSY_MASK                                                              0x00200000L
#define MDM_ENC_PIPE_BUSY__MIF_WR_BSP0_BUSY_MASK                                                              0x00400000L
#define MDM_ENC_PIPE_BUSY__MIF_WR_BSP1_BUSY_MASK                                                              0x00800000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD0_BUSY_MASK                                                              0x01000000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD1_BUSY_MASK                                                              0x02000000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD2_BUSY_MASK                                                              0x04000000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD3_BUSY_MASK                                                              0x08000000L
#define MDM_ENC_PIPE_BUSY__MIF_RD_BSD4_BUSY_MASK                                                              0x10000000L
//MDM_WIG_PIPE_BUSY
#define MDM_WIG_PIPE_BUSY__WIG_TBE_BUSY__SHIFT                                                                0x0
#define MDM_WIG_PIPE_BUSY__WIG_ENT_BUSY__SHIFT                                                                0x1
#define MDM_WIG_PIPE_BUSY__WIG_ENT_HEADER_BUSY__SHIFT                                                         0x2
#define MDM_WIG_PIPE_BUSY__WIG_ENT_HEADER_FIFO_FULL__SHIFT                                                    0x3
#define MDM_WIG_PIPE_BUSY__LCM_BUSY__SHIFT                                                                    0x4
#define MDM_WIG_PIPE_BUSY__MDM_RD_CUR_BUSY__SHIFT                                                             0x5
#define MDM_WIG_PIPE_BUSY__MDM_RD_REF_BUSY__SHIFT                                                             0x6
#define MDM_WIG_PIPE_BUSY__MDM_RD_GEN_BUSY__SHIFT                                                             0x7
#define MDM_WIG_PIPE_BUSY__MDM_WR_RECON_BUSY__SHIFT                                                           0x8
#define MDM_WIG_PIPE_BUSY__MDM_WR_GEN_BUSY__SHIFT                                                             0x9
#define MDM_WIG_PIPE_BUSY__MIF_RD_CUR_BUSY__SHIFT                                                             0xa
#define MDM_WIG_PIPE_BUSY__MIF_RD_REF0_BUSY__SHIFT                                                            0xb
#define MDM_WIG_PIPE_BUSY__MIF_WR_GEN0_BUSY__SHIFT                                                            0xc
#define MDM_WIG_PIPE_BUSY__MIF_RD_GEN0_BUSY__SHIFT                                                            0xd
#define MDM_WIG_PIPE_BUSY__MIF_WR_GEN1_BUSY__SHIFT                                                            0xe
#define MDM_WIG_PIPE_BUSY__MIF_RD_GEN1_BUSY__SHIFT                                                            0xf
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP0_BUSY__SHIFT                                                            0x10
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP1_BUSY__SHIFT                                                            0x11
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD0_BUSY__SHIFT                                                            0x12
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD1_BUSY__SHIFT                                                            0x13
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD2_BUSY__SHIFT                                                            0x14
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD3_BUSY__SHIFT                                                            0x15
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD4_BUSY__SHIFT                                                            0x16
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD5_BUSY__SHIFT                                                            0x17
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP2_BUSY__SHIFT                                                            0x18
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP3_BUSY__SHIFT                                                            0x19
#define MDM_WIG_PIPE_BUSY__LCM_BSP0_NOT_EMPTY__SHIFT                                                          0x1a
#define MDM_WIG_PIPE_BUSY__LCM_BSP1_NOT_EMPTY__SHIFT                                                          0x1b
#define MDM_WIG_PIPE_BUSY__LCM_BSP2_NOT_EMPTY__SHIFT                                                          0x1c
#define MDM_WIG_PIPE_BUSY__LCM_BSP3_NOT_EMPTY__SHIFT                                                          0x1d
#define MDM_WIG_PIPE_BUSY__WIG_TBE_BUSY_MASK                                                                  0x00000001L
#define MDM_WIG_PIPE_BUSY__WIG_ENT_BUSY_MASK                                                                  0x00000002L
#define MDM_WIG_PIPE_BUSY__WIG_ENT_HEADER_BUSY_MASK                                                           0x00000004L
#define MDM_WIG_PIPE_BUSY__WIG_ENT_HEADER_FIFO_FULL_MASK                                                      0x00000008L
#define MDM_WIG_PIPE_BUSY__LCM_BUSY_MASK                                                                      0x00000010L
#define MDM_WIG_PIPE_BUSY__MDM_RD_CUR_BUSY_MASK                                                               0x00000020L
#define MDM_WIG_PIPE_BUSY__MDM_RD_REF_BUSY_MASK                                                               0x00000040L
#define MDM_WIG_PIPE_BUSY__MDM_RD_GEN_BUSY_MASK                                                               0x00000080L
#define MDM_WIG_PIPE_BUSY__MDM_WR_RECON_BUSY_MASK                                                             0x00000100L
#define MDM_WIG_PIPE_BUSY__MDM_WR_GEN_BUSY_MASK                                                               0x00000200L
#define MDM_WIG_PIPE_BUSY__MIF_RD_CUR_BUSY_MASK                                                               0x00000400L
#define MDM_WIG_PIPE_BUSY__MIF_RD_REF0_BUSY_MASK                                                              0x00000800L
#define MDM_WIG_PIPE_BUSY__MIF_WR_GEN0_BUSY_MASK                                                              0x00001000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_GEN0_BUSY_MASK                                                              0x00002000L
#define MDM_WIG_PIPE_BUSY__MIF_WR_GEN1_BUSY_MASK                                                              0x00004000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_GEN1_BUSY_MASK                                                              0x00008000L
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP0_BUSY_MASK                                                              0x00010000L
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP1_BUSY_MASK                                                              0x00020000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD0_BUSY_MASK                                                              0x00040000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD1_BUSY_MASK                                                              0x00080000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD2_BUSY_MASK                                                              0x00100000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD3_BUSY_MASK                                                              0x00200000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD4_BUSY_MASK                                                              0x00400000L
#define MDM_WIG_PIPE_BUSY__MIF_RD_BSD5_BUSY_MASK                                                              0x00800000L
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP2_BUSY_MASK                                                              0x01000000L
#define MDM_WIG_PIPE_BUSY__MIF_WR_BSP3_BUSY_MASK                                                              0x02000000L
#define MDM_WIG_PIPE_BUSY__LCM_BSP0_NOT_EMPTY_MASK                                                            0x04000000L
#define MDM_WIG_PIPE_BUSY__LCM_BSP1_NOT_EMPTY_MASK                                                            0x08000000L
#define MDM_WIG_PIPE_BUSY__LCM_BSP2_NOT_EMPTY_MASK                                                            0x10000000L
#define MDM_WIG_PIPE_BUSY__LCM_BSP3_NOT_EMPTY_MASK                                                            0x20000000L


// addressBlock: lmi_adp_indirect
//UVD_LMI_CRC0
#define UVD_LMI_CRC0__CRC32__SHIFT                                                                            0x0
#define UVD_LMI_CRC0__CRC32_MASK                                                                              0xFFFFFFFFL
//UVD_LMI_CRC1
#define UVD_LMI_CRC1__CRC32__SHIFT                                                                            0x0
#define UVD_LMI_CRC1__CRC32_MASK                                                                              0xFFFFFFFFL
//UVD_LMI_CRC2
#define UVD_LMI_CRC2__CRC32__SHIFT                                                                            0x0
#define UVD_LMI_CRC2__CRC32_MASK                                                                              0xFFFFFFFFL
//UVD_LMI_CRC3
#define UVD_LMI_CRC3__CRC32__SHIFT                                                                            0x0
#define UVD_LMI_CRC3__CRC32_MASK                                                                              0xFFFFFFFFL


/* VCN 2_6_0 UVD_RAS_VCPU_VCODEC_STATUS */
#define UVD_RAS_VCPU_VCODEC_STATUS__POISONED_VF__SHIFT          0x0
#define UVD_RAS_VCPU_VCODEC_STATUS__POISONED_PF__SHIFT          0x1f
#define UVD_RAS_VCPU_VCODEC_STATUS__POISONED_VF_MASK            0x7FFFFFFFL
#define UVD_RAS_VCPU_VCODEC_STATUS__POISONED_PF_MASK            0x80000000L

/* VCN 2_6_0 UVD_RAS_MMSCH_FATAL_ERROR */
#define UVD_RAS_MMSCH_FATAL_ERROR__POISONED_VF__SHIFT           0x0
#define UVD_RAS_MMSCH_FATAL_ERROR__POISONED_PF__SHIFT           0x1f
#define UVD_RAS_MMSCH_FATAL_ERROR__POISONED_VF_MASK             0x7FFFFFFFL
#define UVD_RAS_MMSCH_FATAL_ERROR__POISONED_PF_MASK             0x80000000L

/* JPEG 2_6_0 UVD_RAS_JPEG0_STATUS */
#define UVD_RAS_JPEG0_STATUS__POISONED_VF__SHIFT                0x0
#define UVD_RAS_JPEG0_STATUS__POISONED_PF__SHIFT                0x1f
#define UVD_RAS_JPEG0_STATUS__POISONED_VF_MASK                  0x7FFFFFFFL
#define UVD_RAS_JPEG0_STATUS__POISONED_PF_MASK                  0x80000000L

/* JPEG 2_6_0 UVD_RAS_JPEG1_STATUS */
#define UVD_RAS_JPEG1_STATUS__POISONED_VF__SHIFT                0x0
#define UVD_RAS_JPEG1_STATUS__POISONED_PF__SHIFT                0x1f
#define UVD_RAS_JPEG1_STATUS__POISONED_VF_MASK                  0x7FFFFFFFL
#define UVD_RAS_JPEG1_STATUS__POISONED_PF_MASK                  0x80000000L

#endif
