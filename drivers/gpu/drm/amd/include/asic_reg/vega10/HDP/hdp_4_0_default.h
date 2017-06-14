/*
 * Copyright (C) 2017  Advanced Micro Devices, Inc.
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
#ifndef _hdp_4_0_DEFAULT_HEADER
#define _hdp_4_0_DEFAULT_HEADER


// addressBlock: hdp_hdpdec
#define mmHDP_MMHUB_TLVL_DEFAULT                                                 0x00006666
#define mmHDP_MMHUB_UNITID_DEFAULT                                               0x00000000
#define mmHDP_NONSURFACE_BASE_DEFAULT                                            0x00000000
#define mmHDP_NONSURFACE_INFO_DEFAULT                                            0x00000000
#define mmHDP_NONSURFACE_BASE_HI_DEFAULT                                         0x00000000
#define mmHDP_NONSURF_FLAGS_DEFAULT                                              0x00000000
#define mmHDP_NONSURF_FLAGS_CLR_DEFAULT                                          0x00000000
#define mmHDP_HOST_PATH_CNTL_DEFAULT                                             0x00680000
#define mmHDP_SW_SEMAPHORE_DEFAULT                                               0x00000000
#define mmHDP_DEBUG0_DEFAULT                                                     0x00000000
#define mmHDP_LAST_SURFACE_HIT_DEFAULT                                           0x00000003
#define mmHDP_READ_CACHE_INVALIDATE_DEFAULT                                      0x00000000
#define mmHDP_OUTSTANDING_REQ_DEFAULT                                            0x00000000
#define mmHDP_MISC_CNTL_DEFAULT                                                  0x2d200861
#define mmHDP_MEM_POWER_LS_DEFAULT                                               0x00000901
#define mmHDP_MMHUB_CNTL_DEFAULT                                                 0x00000000
#define mmHDP_EDC_CNT_DEFAULT                                                    0x00000000
#define mmHDP_VERSION_DEFAULT                                                    0x00000400
#define mmHDP_CLK_CNTL_DEFAULT                                                   0x0000000f
#define mmHDP_MEMIO_CNTL_DEFAULT                                                 0x00000000
#define mmHDP_MEMIO_ADDR_DEFAULT                                                 0x00000000
#define mmHDP_MEMIO_STATUS_DEFAULT                                               0x00000000
#define mmHDP_MEMIO_WR_DATA_DEFAULT                                              0x00000000
#define mmHDP_MEMIO_RD_DATA_DEFAULT                                              0xdeadbeef
#define mmHDP_XDP_DIRECT2HDP_FIRST_DEFAULT                                       0x00000000
#define mmHDP_XDP_D2H_FLUSH_DEFAULT                                              0x00000000
#define mmHDP_XDP_D2H_BAR_UPDATE_DEFAULT                                         0x00000000
#define mmHDP_XDP_D2H_RSVD_3_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_4_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_5_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_6_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_7_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_8_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_9_DEFAULT                                             0x00000000
#define mmHDP_XDP_D2H_RSVD_10_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_11_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_12_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_13_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_14_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_15_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_16_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_17_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_18_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_19_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_20_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_21_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_22_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_23_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_24_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_25_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_26_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_27_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_28_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_29_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_30_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_31_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_32_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_33_DEFAULT                                            0x00000000
#define mmHDP_XDP_D2H_RSVD_34_DEFAULT                                            0x00000000
#define mmHDP_XDP_DIRECT2HDP_LAST_DEFAULT                                        0x00000000
#define mmHDP_XDP_P2P_BAR_CFG_DEFAULT                                            0x0000000f
#define mmHDP_XDP_P2P_MBX_OFFSET_DEFAULT                                         0x000011bc
#define mmHDP_XDP_P2P_MBX_ADDR0_DEFAULT                                          0x00000000
#define mmHDP_XDP_P2P_MBX_ADDR1_DEFAULT                                          0x00000000
#define mmHDP_XDP_P2P_MBX_ADDR2_DEFAULT                                          0x00000000
#define mmHDP_XDP_P2P_MBX_ADDR3_DEFAULT                                          0x00000000
#define mmHDP_XDP_P2P_MBX_ADDR4_DEFAULT                                          0x00000000
#define mmHDP_XDP_P2P_MBX_ADDR5_DEFAULT                                          0x00000000
#define mmHDP_XDP_P2P_MBX_ADDR6_DEFAULT                                          0x00000000
#define mmHDP_XDP_HDP_MBX_MC_CFG_DEFAULT                                         0x00000000
#define mmHDP_XDP_HDP_MC_CFG_DEFAULT                                             0x00020000
#define mmHDP_XDP_HST_CFG_DEFAULT                                                0x0000001b
#define mmHDP_XDP_HDP_IPH_CFG_DEFAULT                                            0x00000000
#define mmHDP_XDP_P2P_BAR0_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR1_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR2_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR3_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR4_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR5_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR6_DEFAULT                                               0x00000000
#define mmHDP_XDP_P2P_BAR7_DEFAULT                                               0x00000000
#define mmHDP_XDP_FLUSH_ARMED_STS_DEFAULT                                        0x00000000
#define mmHDP_XDP_FLUSH_CNTR0_STS_DEFAULT                                        0x00000000
#define mmHDP_XDP_BUSY_STS_DEFAULT                                               0x00000000
#define mmHDP_XDP_STICKY_DEFAULT                                                 0x00000000
#define mmHDP_XDP_CHKN_DEFAULT                                                   0x48584450
#define mmHDP_XDP_BARS_ADDR_39_36_DEFAULT                                        0x00000000
#define mmHDP_XDP_MC_VM_FB_LOCATION_BASE_DEFAULT                                 0x00000000
#define mmHDP_XDP_GPU_IOV_VIOLATION_LOG_DEFAULT                                  0x00000000
#define mmHDP_XDP_MMHUB_ERROR_DEFAULT                                            0x00000000

#endif
