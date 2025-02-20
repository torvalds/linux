/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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

#ifndef __JPEG_V5_0_1_H__
#define __JPEG_V5_0_1_H__

extern const struct amdgpu_ip_block_version jpeg_v5_0_1_ip_block;

#define regUVD_JRBC0_UVD_JRBC_RB_WPTR                                                         0x0640
#define regUVD_JRBC0_UVD_JRBC_RB_WPTR_BASE_IDX                                                1
#define regUVD_JRBC0_UVD_JRBC_STATUS                                                          0x0649
#define regUVD_JRBC0_UVD_JRBC_STATUS_BASE_IDX                                                 1
#define regUVD_JRBC0_UVD_JRBC_RB_RPTR                                                         0x064a
#define regUVD_JRBC0_UVD_JRBC_RB_RPTR_BASE_IDX                                                1
#define regUVD_JRBC1_UVD_JRBC_RB_WPTR                                                         0x0000
#define regUVD_JRBC1_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC1_UVD_JRBC_STATUS                                                          0x0009
#define regUVD_JRBC1_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC1_UVD_JRBC_RB_RPTR                                                         0x000a
#define regUVD_JRBC1_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC2_UVD_JRBC_RB_WPTR                                                         0x0040
#define regUVD_JRBC2_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC2_UVD_JRBC_STATUS                                                          0x0049
#define regUVD_JRBC2_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC2_UVD_JRBC_RB_RPTR                                                         0x004a
#define regUVD_JRBC2_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC3_UVD_JRBC_RB_WPTR                                                         0x0080
#define regUVD_JRBC3_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC3_UVD_JRBC_STATUS                                                          0x0089
#define regUVD_JRBC3_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC3_UVD_JRBC_RB_RPTR                                                         0x008a
#define regUVD_JRBC3_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC4_UVD_JRBC_RB_WPTR                                                         0x00c0
#define regUVD_JRBC4_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC4_UVD_JRBC_STATUS                                                          0x00c9
#define regUVD_JRBC4_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC4_UVD_JRBC_RB_RPTR                                                         0x00ca
#define regUVD_JRBC4_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC5_UVD_JRBC_RB_WPTR                                                         0x0100
#define regUVD_JRBC5_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC5_UVD_JRBC_STATUS                                                          0x0109
#define regUVD_JRBC5_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC5_UVD_JRBC_RB_RPTR                                                         0x010a
#define regUVD_JRBC5_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC6_UVD_JRBC_RB_WPTR                                                         0x0140
#define regUVD_JRBC6_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC6_UVD_JRBC_STATUS                                                          0x0149
#define regUVD_JRBC6_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC6_UVD_JRBC_RB_RPTR                                                         0x014a
#define regUVD_JRBC6_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC7_UVD_JRBC_RB_WPTR                                                         0x0180
#define regUVD_JRBC7_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC7_UVD_JRBC_STATUS                                                          0x0189
#define regUVD_JRBC7_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC7_UVD_JRBC_RB_RPTR                                                         0x018a
#define regUVD_JRBC7_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC8_UVD_JRBC_RB_WPTR                                                         0x01c0
#define regUVD_JRBC8_UVD_JRBC_RB_WPTR_BASE_IDX                                                0
#define regUVD_JRBC8_UVD_JRBC_STATUS                                                          0x01c9
#define regUVD_JRBC8_UVD_JRBC_STATUS_BASE_IDX                                                 0
#define regUVD_JRBC8_UVD_JRBC_RB_RPTR                                                         0x01ca
#define regUVD_JRBC8_UVD_JRBC_RB_RPTR_BASE_IDX                                                0
#define regUVD_JRBC9_UVD_JRBC_RB_WPTR                                                         0x0440
#define regUVD_JRBC9_UVD_JRBC_RB_WPTR_BASE_IDX                                                1
#define regUVD_JRBC9_UVD_JRBC_STATUS                                                          0x0449
#define regUVD_JRBC9_UVD_JRBC_STATUS_BASE_IDX                                                 1
#define regUVD_JRBC9_UVD_JRBC_RB_RPTR                                                         0x044a
#define regUVD_JRBC9_UVD_JRBC_RB_RPTR_BASE_IDX                                                1
#define regUVD_JMI0_JPEG_LMI_DROP                                                             0x0663
#define regUVD_JMI0_JPEG_LMI_DROP_BASE_IDX                                                    1
#define regUVD_JMI0_UVD_JMI_CLIENT_STALL                                                      0x067a
#define regUVD_JMI0_UVD_JMI_CLIENT_STALL_BASE_IDX                                             1
#define regUVD_JMI0_UVD_JMI_CLIENT_CLEAN_STATUS                                               0x067b
#define regUVD_JMI0_UVD_JMI_CLIENT_CLEAN_STATUS_BASE_IDX                                      1
#define regJPEG_CORE_RST_CTRL                                                                 0x072e
#define regJPEG_CORE_RST_CTRL_BASE_IDX                                                        1

#define regVCN_RRMT_CNTL                          0x0940
#define regVCN_RRMT_CNTL_BASE_IDX                 1

#endif /* __JPEG_V5_0_1_H__ */
