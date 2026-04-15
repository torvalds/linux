/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
#ifndef _lsdma_7_1_0_SH_MASK_HEADER
#define _lsdma_7_1_0_SH_MASK_HEADER


// addressBlock: lsdma0_lsdma0dec
//LSDMA_PIO_STATUS
#define LSDMA_PIO_STATUS__CMD_IN_FIFO__SHIFT                                                                  0x0
#define LSDMA_PIO_STATUS__CMD_PROCESSING__SHIFT                                                               0x3
#define LSDMA_PIO_STATUS__ERROR_INVALID_ADDR__SHIFT                                                           0xb
#define LSDMA_PIO_STATUS__ERROR_ZERO_COUNT__SHIFT                                                             0xc
#define LSDMA_PIO_STATUS__ERROR_DRAM_ECC__SHIFT                                                               0xd
#define LSDMA_PIO_STATUS__ERROR_SRAM_ECC__SHIFT                                                               0xe
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_GEN_ERR__SHIFT                                                     0xf
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_GEN_ERR__SHIFT                                                     0x10
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_PRT__SHIFT                                                         0x11
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_PRT__SHIFT                                                         0x12
#define LSDMA_PIO_STATUS__ERROR_REQ_DROP__SHIFT                                                               0x13
#define LSDMA_PIO_STATUS__PIO_FIFO_EMPTY__SHIFT                                                               0x1c
#define LSDMA_PIO_STATUS__PIO_FIFO_FULL__SHIFT                                                                0x1d
#define LSDMA_PIO_STATUS__PIO_IDLE__SHIFT                                                                     0x1f
#define LSDMA_PIO_STATUS__CMD_IN_FIFO_MASK                                                                    0x00000007L
#define LSDMA_PIO_STATUS__CMD_PROCESSING_MASK                                                                 0x000003F8L
#define LSDMA_PIO_STATUS__ERROR_INVALID_ADDR_MASK                                                             0x00000800L
#define LSDMA_PIO_STATUS__ERROR_ZERO_COUNT_MASK                                                               0x00001000L
#define LSDMA_PIO_STATUS__ERROR_DRAM_ECC_MASK                                                                 0x00002000L
#define LSDMA_PIO_STATUS__ERROR_SRAM_ECC_MASK                                                                 0x00004000L
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_GEN_ERR_MASK                                                       0x00008000L
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_GEN_ERR_MASK                                                       0x00010000L
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_PRT_MASK                                                           0x00020000L
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_PRT_MASK                                                           0x00040000L
#define LSDMA_PIO_STATUS__ERROR_REQ_DROP_MASK                                                                 0x00080000L
#define LSDMA_PIO_STATUS__PIO_FIFO_EMPTY_MASK                                                                 0x10000000L
#define LSDMA_PIO_STATUS__PIO_FIFO_FULL_MASK                                                                  0x20000000L
#define LSDMA_PIO_STATUS__PIO_IDLE_MASK                                                                       0x80000000L
//LSDMA_PIO_SRC_ADDR_LO
#define LSDMA_PIO_SRC_ADDR_LO__SRC_ADDR_LO__SHIFT                                                             0x0
#define LSDMA_PIO_SRC_ADDR_LO__SRC_ADDR_LO_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_SRC_ADDR_HI
#define LSDMA_PIO_SRC_ADDR_HI__SRC_ADDR_HI__SHIFT                                                             0x0
#define LSDMA_PIO_SRC_ADDR_HI__SRC_ADDR_HI_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_DST_ADDR_LO
#define LSDMA_PIO_DST_ADDR_LO__DST_ADDR_LO__SHIFT                                                             0x0
#define LSDMA_PIO_DST_ADDR_LO__DST_ADDR_LO_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_DST_ADDR_HI
#define LSDMA_PIO_DST_ADDR_HI__DST_ADDR_HI__SHIFT                                                             0x0
#define LSDMA_PIO_DST_ADDR_HI__DST_ADDR_HI_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_CONTROL
#define LSDMA_PIO_CONTROL__VMID__SHIFT                                                                        0x0
#define LSDMA_PIO_CONTROL__DST_GPA__SHIFT                                                                     0x4
#define LSDMA_PIO_CONTROL__DST_SYS__SHIFT                                                                     0x5
#define LSDMA_PIO_CONTROL__DST_GCC__SHIFT                                                                     0x6
#define LSDMA_PIO_CONTROL__DST_SNOOP__SHIFT                                                                   0x7
#define LSDMA_PIO_CONTROL__DST_REUSE_HINT__SHIFT                                                              0x8
#define LSDMA_PIO_CONTROL__DST_COMP_EN__SHIFT                                                                 0xa
#define LSDMA_PIO_CONTROL__SRC_GPA__SHIFT                                                                     0x14
#define LSDMA_PIO_CONTROL__SRC_SYS__SHIFT                                                                     0x15
#define LSDMA_PIO_CONTROL__SRC_SNOOP__SHIFT                                                                   0x17
#define LSDMA_PIO_CONTROL__SRC_REUSE_HINT__SHIFT                                                              0x18
#define LSDMA_PIO_CONTROL__SRC_COMP_EN__SHIFT                                                                 0x1a
#define LSDMA_PIO_CONTROL__VMID_MASK                                                                          0x0000000FL
#define LSDMA_PIO_CONTROL__DST_GPA_MASK                                                                       0x00000010L
#define LSDMA_PIO_CONTROL__DST_SYS_MASK                                                                       0x00000020L
#define LSDMA_PIO_CONTROL__DST_GCC_MASK                                                                       0x00000040L
#define LSDMA_PIO_CONTROL__DST_SNOOP_MASK                                                                     0x00000080L
#define LSDMA_PIO_CONTROL__DST_REUSE_HINT_MASK                                                                0x00000300L
#define LSDMA_PIO_CONTROL__DST_COMP_EN_MASK                                                                   0x00000400L
#define LSDMA_PIO_CONTROL__SRC_GPA_MASK                                                                       0x00100000L
#define LSDMA_PIO_CONTROL__SRC_SYS_MASK                                                                       0x00200000L
#define LSDMA_PIO_CONTROL__SRC_SNOOP_MASK                                                                     0x00800000L
#define LSDMA_PIO_CONTROL__SRC_REUSE_HINT_MASK                                                                0x03000000L
#define LSDMA_PIO_CONTROL__SRC_COMP_EN_MASK                                                                   0x04000000L
//LSDMA_PIO_COMMAND
#define LSDMA_PIO_COMMAND__COUNT__SHIFT                                                                       0x0
#define LSDMA_PIO_COMMAND__RAW_WAIT__SHIFT                                                                    0x1e
#define LSDMA_PIO_COMMAND__CONSTANT_FILL__SHIFT                                                               0x1f
#define LSDMA_PIO_COMMAND__COUNT_MASK                                                                         0x03FFFFFFL
#define LSDMA_PIO_COMMAND__RAW_WAIT_MASK                                                                      0x40000000L
#define LSDMA_PIO_COMMAND__CONSTANT_FILL_MASK                                                                 0x80000000L
//LSDMA_PIO_CONSTFILL_DATA
#define LSDMA_PIO_CONSTFILL_DATA__DATA__SHIFT                                                                 0x0
#define LSDMA_PIO_CONSTFILL_DATA__DATA_MASK                                                                   0xFFFFFFFFL

#endif
