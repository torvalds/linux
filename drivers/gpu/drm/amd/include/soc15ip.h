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
#ifndef _soc15ip_new_HEADER
#define _soc15ip_new_HEADER

// HW ID
#define MP1_HWID                                           1
#define MP2_HWID                                           2
#define THM_HWID                                           3
#define SMUIO_HWID                                         4
#define FUSE_HWID                                          5
#define CLKA_HWID                                          6
#define PWR_HWID                                          10
#define GC_HWID                                           11
#define UVD_HWID                                          12
#define VCN_HWID                                          UVD_HWID
#define AUDIO_AZ_HWID                                     13
#define ACP_HWID                                          14
#define DCI_HWID                                          15
#define DMU_HWID                                         271
#define DCO_HWID                                          16
#define DIO_HWID                                         272
#define XDMA_HWID                                         17
#define DCEAZ_HWID                                        18
#define DAZ_HWID                                         274
#define SDPMUX_HWID                                       19
#define NTB_HWID                                          20
#define IOHC_HWID                                         24
#define L2IMU_HWID                                        28
#define VCE_HWID                                          32
#define MMHUB_HWID                                        34
#define ATHUB_HWID                                        35
#define DBGU_NBIO_HWID                                    36
#define DFX_HWID                                          37
#define DBGU0_HWID                                        38
#define DBGU1_HWID                                        39
#define OSSSYS_HWID                                       40
#define HDP_HWID                                          41
#define SDMA0_HWID                                        42
#define SDMA1_HWID                                        43
#define ISP_HWID                                          44
#define DBGU_IO_HWID                                      45
#define DF_HWID                                           46
#define CLKB_HWID                                         47
#define FCH_HWID                                          48
#define DFX_DAP_HWID                                      49
#define L1IMU_PCIE_HWID                                   50
#define L1IMU_NBIF_HWID                                   51
#define L1IMU_IOAGR_HWID                                  52
#define L1IMU3_HWID                                       53
#define L1IMU4_HWID                                       54
#define L1IMU5_HWID                                       55
#define L1IMU6_HWID                                       56
#define L1IMU7_HWID                                       57
#define L1IMU8_HWID                                       58
#define L1IMU9_HWID                                       59
#define L1IMU10_HWID                                      60
#define L1IMU11_HWID                                      61
#define L1IMU12_HWID                                      62
#define L1IMU13_HWID                                      63
#define L1IMU14_HWID                                      64
#define L1IMU15_HWID                                      65
#define WAFLC_HWID                                        66
#define FCH_USB_PD_HWID                                   67
#define PCIE_HWID                                         70
#define PCS_HWID                                          80
#define DDCL_HWID                                         89
#define SST_HWID                                          90
#define IOAGR_HWID                                       100
#define NBIF_HWID                                        108
#define IOAPIC_HWID                                      124
#define SYSTEMHUB_HWID                                   128
#define NTBCCP_HWID                                      144
#define UMC_HWID                                         150
#define SATA_HWID                                        168
#define USB_HWID                                         170
#define CCXSEC_HWID                                      176
#define XGBE_HWID                                        216
#define MP0_HWID                                         254

#define MAX_INSTANCE                                       5
#define MAX_SEGMENT                                        5


struct IP_BASE_INSTANCE 
{
    unsigned int segment[MAX_SEGMENT];
};
 
struct IP_BASE 
{
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};


static const struct IP_BASE NBIF_BASE			= { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE NBIO_BASE			= { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DCE_BASE			= { { { { 0x00000012, 0x000000C0, 0x000034C0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DCN_BASE			= { { { { 0x00000012, 0x000000C0, 0x000034C0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP0_BASE			= { { { { 0x00016000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP1_BASE			= { { { { 0x00016000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP2_BASE			= { { { { 0x00016000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DF_BASE			= { { { { 0x00007000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE UVD_BASE			= { { { { 0x00007800, 0x00007E00, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };  //note: GLN does not use the first segment
static const struct IP_BASE VCN_BASE			= { { { { 0x00007800, 0x00007E00, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };  //note: GLN does not use the first segment
static const struct IP_BASE DBGU_BASE			= { { { { 0x00000180, 0x000001A0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE DBGU_NBIO_BASE		= { { { { 0x000001C0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE DBGU_IO_BASE		= { { { { 0x000001E0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE DFX_DAP_BASE		= { { { { 0x000005A0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE DFX_BASE			= { { { { 0x00000580, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // this file does not contain registers
static const struct IP_BASE ISP_BASE			= { { { { 0x00018000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE SYSTEMHUB_BASE		= { { { { 0x00000EA0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE L2IMU_BASE			= { { { { 0x00007DC0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE IOHC_BASE			= { { { { 0x00010000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE ATHUB_BASE			= { { { { 0x00000C20, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE VCE_BASE			= { { { { 0x00007E00, 0x00048800, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE GC_BASE			= { { { { 0x00002000, 0x0000A000, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MMHUB_BASE			= { { { { 0x0001A000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE RSMU_BASE			= { { { { 0x00012000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE HDP_BASE			= { { { { 0x00000F20, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE OSSSYS_BASE		= { { { { 0x000010A0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA0_BASE			= { { { { 0x00001260, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA1_BASE			= { { { { 0x00001460, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE XDMA_BASE			= { { { { 0x00003400, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE UMC_BASE			= { { { { 0x00014000, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE THM_BASE			= { { { { 0x00016600, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SMUIO_BASE			= { { { { 0x00016800, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE PWR_BASE			= { { { { 0x00016A00, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE CLK_BASE			= { { { { 0x00016C00, 0, 0, 0, 0 } },
									    { { 0x00016E00, 0, 0, 0, 0 } }, 
										{ { 0x00017000, 0, 0, 0, 0 } }, 
	                                    { { 0x00017200, 0, 0, 0, 0 } }, 
						                { { 0x00017E00, 0, 0, 0, 0 } } } };  
static const struct IP_BASE FUSE_BASE			= { { { { 0x00017400, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } }, 
										{ { 0, 0, 0, 0, 0 } } } };


#define NBIF_BASE__INST0_SEG0                     0x00000000
#define NBIF_BASE__INST0_SEG1                     0x00000014
#define NBIF_BASE__INST0_SEG2                     0x00000D20
#define NBIF_BASE__INST0_SEG3                     0x00010400
#define NBIF_BASE__INST0_SEG4                     0

#define NBIF_BASE__INST1_SEG0                     0
#define NBIF_BASE__INST1_SEG1                     0
#define NBIF_BASE__INST1_SEG2                     0
#define NBIF_BASE__INST1_SEG3                     0
#define NBIF_BASE__INST1_SEG4                     0

#define NBIF_BASE__INST2_SEG0                     0
#define NBIF_BASE__INST2_SEG1                     0
#define NBIF_BASE__INST2_SEG2                     0
#define NBIF_BASE__INST2_SEG3                     0
#define NBIF_BASE__INST2_SEG4                     0

#define NBIF_BASE__INST3_SEG0                     0
#define NBIF_BASE__INST3_SEG1                     0
#define NBIF_BASE__INST3_SEG2                     0
#define NBIF_BASE__INST3_SEG3                     0
#define NBIF_BASE__INST3_SEG4                     0

#define NBIF_BASE__INST4_SEG0                     0
#define NBIF_BASE__INST4_SEG1                     0
#define NBIF_BASE__INST4_SEG2                     0
#define NBIF_BASE__INST4_SEG3                     0
#define NBIF_BASE__INST4_SEG4                     0

#define NBIO_BASE__INST0_SEG0                     0x00000000
#define NBIO_BASE__INST0_SEG1                     0x00000014
#define NBIO_BASE__INST0_SEG2                     0x00000D20
#define NBIO_BASE__INST0_SEG3                     0x00010400
#define NBIO_BASE__INST0_SEG4                     0

#define NBIO_BASE__INST1_SEG0                     0
#define NBIO_BASE__INST1_SEG1                     0
#define NBIO_BASE__INST1_SEG2                     0
#define NBIO_BASE__INST1_SEG3                     0
#define NBIO_BASE__INST1_SEG4                     0

#define NBIO_BASE__INST2_SEG0                     0
#define NBIO_BASE__INST2_SEG1                     0
#define NBIO_BASE__INST2_SEG2                     0
#define NBIO_BASE__INST2_SEG3                     0
#define NBIO_BASE__INST2_SEG4                     0

#define NBIO_BASE__INST3_SEG0                     0
#define NBIO_BASE__INST3_SEG1                     0
#define NBIO_BASE__INST3_SEG2                     0
#define NBIO_BASE__INST3_SEG3                     0
#define NBIO_BASE__INST3_SEG4                     0

#define NBIO_BASE__INST4_SEG0                     0
#define NBIO_BASE__INST4_SEG1                     0
#define NBIO_BASE__INST4_SEG2                     0
#define NBIO_BASE__INST4_SEG3                     0
#define NBIO_BASE__INST4_SEG4                     0

#define DCE_BASE__INST0_SEG0                      0x00000012
#define DCE_BASE__INST0_SEG1                      0x000000C0
#define DCE_BASE__INST0_SEG2                      0x000034C0
#define DCE_BASE__INST0_SEG3                      0
#define DCE_BASE__INST0_SEG4                      0

#define DCE_BASE__INST1_SEG0                      0
#define DCE_BASE__INST1_SEG1                      0
#define DCE_BASE__INST1_SEG2                      0
#define DCE_BASE__INST1_SEG3                      0
#define DCE_BASE__INST1_SEG4                      0

#define DCE_BASE__INST2_SEG0                      0
#define DCE_BASE__INST2_SEG1                      0
#define DCE_BASE__INST2_SEG2                      0
#define DCE_BASE__INST2_SEG3                      0
#define DCE_BASE__INST2_SEG4                      0

#define DCE_BASE__INST3_SEG0                      0
#define DCE_BASE__INST3_SEG1                      0
#define DCE_BASE__INST3_SEG2                      0
#define DCE_BASE__INST3_SEG3                      0
#define DCE_BASE__INST3_SEG4                      0

#define DCE_BASE__INST4_SEG0                      0
#define DCE_BASE__INST4_SEG1                      0
#define DCE_BASE__INST4_SEG2                      0
#define DCE_BASE__INST4_SEG3                      0
#define DCE_BASE__INST4_SEG4                      0

#define DCN_BASE__INST0_SEG0                      0x00000012
#define DCN_BASE__INST0_SEG1                      0x000000C0
#define DCN_BASE__INST0_SEG2                      0x000034C0
#define DCN_BASE__INST0_SEG3                      0
#define DCN_BASE__INST0_SEG4                      0

#define DCN_BASE__INST1_SEG0                      0
#define DCN_BASE__INST1_SEG1                      0
#define DCN_BASE__INST1_SEG2                      0
#define DCN_BASE__INST1_SEG3                      0
#define DCN_BASE__INST1_SEG4                      0

#define DCN_BASE__INST2_SEG0                      0
#define DCN_BASE__INST2_SEG1                      0
#define DCN_BASE__INST2_SEG2                      0
#define DCN_BASE__INST2_SEG3                      0
#define DCN_BASE__INST2_SEG4                      0

#define DCN_BASE__INST3_SEG0                      0
#define DCN_BASE__INST3_SEG1                      0
#define DCN_BASE__INST3_SEG2                      0
#define DCN_BASE__INST3_SEG3                      0
#define DCN_BASE__INST3_SEG4                      0

#define DCN_BASE__INST4_SEG0                      0
#define DCN_BASE__INST4_SEG1                      0
#define DCN_BASE__INST4_SEG2                      0
#define DCN_BASE__INST4_SEG3                      0
#define DCN_BASE__INST4_SEG4                      0

#define MP0_BASE__INST0_SEG0                      0x00016000
#define MP0_BASE__INST0_SEG1                      0
#define MP0_BASE__INST0_SEG2                      0
#define MP0_BASE__INST0_SEG3                      0
#define MP0_BASE__INST0_SEG4                      0

#define MP0_BASE__INST1_SEG0                      0
#define MP0_BASE__INST1_SEG1                      0
#define MP0_BASE__INST1_SEG2                      0
#define MP0_BASE__INST1_SEG3                      0
#define MP0_BASE__INST1_SEG4                      0

#define MP0_BASE__INST2_SEG0                      0
#define MP0_BASE__INST2_SEG1                      0
#define MP0_BASE__INST2_SEG2                      0
#define MP0_BASE__INST2_SEG3                      0
#define MP0_BASE__INST2_SEG4                      0

#define MP0_BASE__INST3_SEG0                      0
#define MP0_BASE__INST3_SEG1                      0
#define MP0_BASE__INST3_SEG2                      0
#define MP0_BASE__INST3_SEG3                      0
#define MP0_BASE__INST3_SEG4                      0

#define MP0_BASE__INST4_SEG0                      0
#define MP0_BASE__INST4_SEG1                      0
#define MP0_BASE__INST4_SEG2                      0
#define MP0_BASE__INST4_SEG3                      0
#define MP0_BASE__INST4_SEG4                      0

#define MP1_BASE__INST0_SEG0                      0x00016200
#define MP1_BASE__INST0_SEG1                      0
#define MP1_BASE__INST0_SEG2                      0
#define MP1_BASE__INST0_SEG3                      0
#define MP1_BASE__INST0_SEG4                      0

#define MP1_BASE__INST1_SEG0                      0
#define MP1_BASE__INST1_SEG1                      0
#define MP1_BASE__INST1_SEG2                      0
#define MP1_BASE__INST1_SEG3                      0
#define MP1_BASE__INST1_SEG4                      0

#define MP1_BASE__INST2_SEG0                      0
#define MP1_BASE__INST2_SEG1                      0
#define MP1_BASE__INST2_SEG2                      0
#define MP1_BASE__INST2_SEG3                      0
#define MP1_BASE__INST2_SEG4                      0

#define MP1_BASE__INST3_SEG0                      0
#define MP1_BASE__INST3_SEG1                      0
#define MP1_BASE__INST3_SEG2                      0
#define MP1_BASE__INST3_SEG3                      0
#define MP1_BASE__INST3_SEG4                      0

#define MP1_BASE__INST4_SEG0                      0
#define MP1_BASE__INST4_SEG1                      0
#define MP1_BASE__INST4_SEG2                      0
#define MP1_BASE__INST4_SEG3                      0
#define MP1_BASE__INST4_SEG4                      0

#define MP2_BASE__INST0_SEG0                      0x00016400
#define MP2_BASE__INST0_SEG1                      0
#define MP2_BASE__INST0_SEG2                      0
#define MP2_BASE__INST0_SEG3                      0
#define MP2_BASE__INST0_SEG4                      0

#define MP2_BASE__INST1_SEG0                      0
#define MP2_BASE__INST1_SEG1                      0
#define MP2_BASE__INST1_SEG2                      0
#define MP2_BASE__INST1_SEG3                      0
#define MP2_BASE__INST1_SEG4                      0

#define MP2_BASE__INST2_SEG0                      0
#define MP2_BASE__INST2_SEG1                      0
#define MP2_BASE__INST2_SEG2                      0
#define MP2_BASE__INST2_SEG3                      0
#define MP2_BASE__INST2_SEG4                      0

#define MP2_BASE__INST3_SEG0                      0
#define MP2_BASE__INST3_SEG1                      0
#define MP2_BASE__INST3_SEG2                      0
#define MP2_BASE__INST3_SEG3                      0
#define MP2_BASE__INST3_SEG4                      0

#define MP2_BASE__INST4_SEG0                      0
#define MP2_BASE__INST4_SEG1                      0
#define MP2_BASE__INST4_SEG2                      0
#define MP2_BASE__INST4_SEG3                      0
#define MP2_BASE__INST4_SEG4                      0

#define DF_BASE__INST0_SEG0                       0x00007000
#define DF_BASE__INST0_SEG1                       0
#define DF_BASE__INST0_SEG2                       0
#define DF_BASE__INST0_SEG3                       0
#define DF_BASE__INST0_SEG4                       0

#define DF_BASE__INST1_SEG0                       0
#define DF_BASE__INST1_SEG1                       0
#define DF_BASE__INST1_SEG2                       0
#define DF_BASE__INST1_SEG3                       0
#define DF_BASE__INST1_SEG4                       0

#define DF_BASE__INST2_SEG0                       0
#define DF_BASE__INST2_SEG1                       0
#define DF_BASE__INST2_SEG2                       0
#define DF_BASE__INST2_SEG3                       0
#define DF_BASE__INST2_SEG4                       0

#define DF_BASE__INST3_SEG0                       0
#define DF_BASE__INST3_SEG1                       0
#define DF_BASE__INST3_SEG2                       0
#define DF_BASE__INST3_SEG3                       0
#define DF_BASE__INST3_SEG4                       0

#define DF_BASE__INST4_SEG0                       0
#define DF_BASE__INST4_SEG1                       0
#define DF_BASE__INST4_SEG2                       0
#define DF_BASE__INST4_SEG3                       0
#define DF_BASE__INST4_SEG4                       0

#define UVD_BASE__INST0_SEG0                      0x00007800
#define UVD_BASE__INST0_SEG1                      0x00007E00
#define UVD_BASE__INST0_SEG2                      0
#define UVD_BASE__INST0_SEG3                      0
#define UVD_BASE__INST0_SEG4                      0

#define UVD_BASE__INST1_SEG0                      0
#define UVD_BASE__INST1_SEG1                      0
#define UVD_BASE__INST1_SEG2                      0
#define UVD_BASE__INST1_SEG3                      0
#define UVD_BASE__INST1_SEG4                      0

#define UVD_BASE__INST2_SEG0                      0
#define UVD_BASE__INST2_SEG1                      0
#define UVD_BASE__INST2_SEG2                      0
#define UVD_BASE__INST2_SEG3                      0
#define UVD_BASE__INST2_SEG4                      0

#define UVD_BASE__INST3_SEG0                      0
#define UVD_BASE__INST3_SEG1                      0
#define UVD_BASE__INST3_SEG2                      0
#define UVD_BASE__INST3_SEG3                      0
#define UVD_BASE__INST3_SEG4                      0

#define UVD_BASE__INST4_SEG0                      0
#define UVD_BASE__INST4_SEG1                      0
#define UVD_BASE__INST4_SEG2                      0
#define UVD_BASE__INST4_SEG3                      0
#define UVD_BASE__INST4_SEG4                      0

#define VCN_BASE__INST0_SEG0                      0x00007800
#define VCN_BASE__INST0_SEG1                      0x00007E00
#define VCN_BASE__INST0_SEG2                      0
#define VCN_BASE__INST0_SEG3                      0
#define VCN_BASE__INST0_SEG4                      0

#define VCN_BASE__INST1_SEG0                      0
#define VCN_BASE__INST1_SEG1                      0
#define VCN_BASE__INST1_SEG2                      0
#define VCN_BASE__INST1_SEG3                      0
#define VCN_BASE__INST1_SEG4                      0

#define VCN_BASE__INST2_SEG0                      0
#define VCN_BASE__INST2_SEG1                      0
#define VCN_BASE__INST2_SEG2                      0
#define VCN_BASE__INST2_SEG3                      0
#define VCN_BASE__INST2_SEG4                      0

#define VCN_BASE__INST3_SEG0                      0
#define VCN_BASE__INST3_SEG1                      0
#define VCN_BASE__INST3_SEG2                      0
#define VCN_BASE__INST3_SEG3                      0
#define VCN_BASE__INST3_SEG4                      0

#define VCN_BASE__INST4_SEG0                      0
#define VCN_BASE__INST4_SEG1                      0
#define VCN_BASE__INST4_SEG2                      0
#define VCN_BASE__INST4_SEG3                      0
#define VCN_BASE__INST4_SEG4                      0

#define DBGU_BASE__INST0_SEG0                     0x00000180
#define DBGU_BASE__INST0_SEG1                     0x000001A0
#define DBGU_BASE__INST0_SEG2                     0
#define DBGU_BASE__INST0_SEG3                     0
#define DBGU_BASE__INST0_SEG4                     0

#define DBGU_BASE__INST1_SEG0                     0
#define DBGU_BASE__INST1_SEG1                     0
#define DBGU_BASE__INST1_SEG2                     0
#define DBGU_BASE__INST1_SEG3                     0
#define DBGU_BASE__INST1_SEG4                     0

#define DBGU_BASE__INST2_SEG0                     0
#define DBGU_BASE__INST2_SEG1                     0
#define DBGU_BASE__INST2_SEG2                     0
#define DBGU_BASE__INST2_SEG3                     0
#define DBGU_BASE__INST2_SEG4                     0

#define DBGU_BASE__INST3_SEG0                     0
#define DBGU_BASE__INST3_SEG1                     0
#define DBGU_BASE__INST3_SEG2                     0
#define DBGU_BASE__INST3_SEG3                     0
#define DBGU_BASE__INST3_SEG4                     0

#define DBGU_BASE__INST4_SEG0                     0
#define DBGU_BASE__INST4_SEG1                     0
#define DBGU_BASE__INST4_SEG2                     0
#define DBGU_BASE__INST4_SEG3                     0
#define DBGU_BASE__INST4_SEG4                     0

#define DBGU_NBIO_BASE__INST0_SEG0                0x000001C0
#define DBGU_NBIO_BASE__INST0_SEG1                0
#define DBGU_NBIO_BASE__INST0_SEG2                0
#define DBGU_NBIO_BASE__INST0_SEG3                0
#define DBGU_NBIO_BASE__INST0_SEG4                0

#define DBGU_NBIO_BASE__INST1_SEG0                0
#define DBGU_NBIO_BASE__INST1_SEG1                0
#define DBGU_NBIO_BASE__INST1_SEG2                0
#define DBGU_NBIO_BASE__INST1_SEG3                0
#define DBGU_NBIO_BASE__INST1_SEG4                0

#define DBGU_NBIO_BASE__INST2_SEG0                0
#define DBGU_NBIO_BASE__INST2_SEG1                0
#define DBGU_NBIO_BASE__INST2_SEG2                0
#define DBGU_NBIO_BASE__INST2_SEG3                0
#define DBGU_NBIO_BASE__INST2_SEG4                0

#define DBGU_NBIO_BASE__INST3_SEG0                0
#define DBGU_NBIO_BASE__INST3_SEG1                0
#define DBGU_NBIO_BASE__INST3_SEG2                0
#define DBGU_NBIO_BASE__INST3_SEG3                0
#define DBGU_NBIO_BASE__INST3_SEG4                0

#define DBGU_NBIO_BASE__INST4_SEG0                0
#define DBGU_NBIO_BASE__INST4_SEG1                0
#define DBGU_NBIO_BASE__INST4_SEG2                0
#define DBGU_NBIO_BASE__INST4_SEG3                0
#define DBGU_NBIO_BASE__INST4_SEG4                0

#define DBGU_IO_BASE__INST0_SEG0                  0x000001E0
#define DBGU_IO_BASE__INST0_SEG1                  0
#define DBGU_IO_BASE__INST0_SEG2                  0
#define DBGU_IO_BASE__INST0_SEG3                  0
#define DBGU_IO_BASE__INST0_SEG4                  0

#define DBGU_IO_BASE__INST1_SEG0                  0
#define DBGU_IO_BASE__INST1_SEG1                  0
#define DBGU_IO_BASE__INST1_SEG2                  0
#define DBGU_IO_BASE__INST1_SEG3                  0
#define DBGU_IO_BASE__INST1_SEG4                  0

#define DBGU_IO_BASE__INST2_SEG0                  0
#define DBGU_IO_BASE__INST2_SEG1                  0
#define DBGU_IO_BASE__INST2_SEG2                  0
#define DBGU_IO_BASE__INST2_SEG3                  0
#define DBGU_IO_BASE__INST2_SEG4                  0

#define DBGU_IO_BASE__INST3_SEG0                  0
#define DBGU_IO_BASE__INST3_SEG1                  0
#define DBGU_IO_BASE__INST3_SEG2                  0
#define DBGU_IO_BASE__INST3_SEG3                  0
#define DBGU_IO_BASE__INST3_SEG4                  0

#define DBGU_IO_BASE__INST4_SEG0                  0
#define DBGU_IO_BASE__INST4_SEG1                  0
#define DBGU_IO_BASE__INST4_SEG2                  0
#define DBGU_IO_BASE__INST4_SEG3                  0
#define DBGU_IO_BASE__INST4_SEG4                  0

#define DFX_DAP_BASE__INST0_SEG0                  0x000005A0
#define DFX_DAP_BASE__INST0_SEG1                  0
#define DFX_DAP_BASE__INST0_SEG2                  0
#define DFX_DAP_BASE__INST0_SEG3                  0
#define DFX_DAP_BASE__INST0_SEG4                  0

#define DFX_DAP_BASE__INST1_SEG0                  0
#define DFX_DAP_BASE__INST1_SEG1                  0
#define DFX_DAP_BASE__INST1_SEG2                  0
#define DFX_DAP_BASE__INST1_SEG3                  0
#define DFX_DAP_BASE__INST1_SEG4                  0

#define DFX_DAP_BASE__INST2_SEG0                  0
#define DFX_DAP_BASE__INST2_SEG1                  0
#define DFX_DAP_BASE__INST2_SEG2                  0
#define DFX_DAP_BASE__INST2_SEG3                  0
#define DFX_DAP_BASE__INST2_SEG4                  0

#define DFX_DAP_BASE__INST3_SEG0                  0
#define DFX_DAP_BASE__INST3_SEG1                  0
#define DFX_DAP_BASE__INST3_SEG2                  0
#define DFX_DAP_BASE__INST3_SEG3                  0
#define DFX_DAP_BASE__INST3_SEG4                  0

#define DFX_DAP_BASE__INST4_SEG0                  0
#define DFX_DAP_BASE__INST4_SEG1                  0
#define DFX_DAP_BASE__INST4_SEG2                  0
#define DFX_DAP_BASE__INST4_SEG3                  0
#define DFX_DAP_BASE__INST4_SEG4                  0

#define DFX_BASE__INST0_SEG0                      0x00000580
#define DFX_BASE__INST0_SEG1                      0
#define DFX_BASE__INST0_SEG2                      0
#define DFX_BASE__INST0_SEG3                      0
#define DFX_BASE__INST0_SEG4                      0

#define DFX_BASE__INST1_SEG0                      0
#define DFX_BASE__INST1_SEG1                      0
#define DFX_BASE__INST1_SEG2                      0
#define DFX_BASE__INST1_SEG3                      0
#define DFX_BASE__INST1_SEG4                      0

#define DFX_BASE__INST2_SEG0                      0
#define DFX_BASE__INST2_SEG1                      0
#define DFX_BASE__INST2_SEG2                      0
#define DFX_BASE__INST2_SEG3                      0
#define DFX_BASE__INST2_SEG4                      0

#define DFX_BASE__INST3_SEG0                      0
#define DFX_BASE__INST3_SEG1                      0
#define DFX_BASE__INST3_SEG2                      0
#define DFX_BASE__INST3_SEG3                      0
#define DFX_BASE__INST3_SEG4                      0

#define DFX_BASE__INST4_SEG0                      0
#define DFX_BASE__INST4_SEG1                      0
#define DFX_BASE__INST4_SEG2                      0
#define DFX_BASE__INST4_SEG3                      0
#define DFX_BASE__INST4_SEG4                      0

#define ISP_BASE__INST0_SEG0                      0x00018000
#define ISP_BASE__INST0_SEG1                      0
#define ISP_BASE__INST0_SEG2                      0
#define ISP_BASE__INST0_SEG3                      0
#define ISP_BASE__INST0_SEG4                      0

#define ISP_BASE__INST1_SEG0                      0
#define ISP_BASE__INST1_SEG1                      0
#define ISP_BASE__INST1_SEG2                      0
#define ISP_BASE__INST1_SEG3                      0
#define ISP_BASE__INST1_SEG4                      0

#define ISP_BASE__INST2_SEG0                      0
#define ISP_BASE__INST2_SEG1                      0
#define ISP_BASE__INST2_SEG2                      0
#define ISP_BASE__INST2_SEG3                      0
#define ISP_BASE__INST2_SEG4                      0

#define ISP_BASE__INST3_SEG0                      0
#define ISP_BASE__INST3_SEG1                      0
#define ISP_BASE__INST3_SEG2                      0
#define ISP_BASE__INST3_SEG3                      0
#define ISP_BASE__INST3_SEG4                      0

#define ISP_BASE__INST4_SEG0                      0
#define ISP_BASE__INST4_SEG1                      0
#define ISP_BASE__INST4_SEG2                      0
#define ISP_BASE__INST4_SEG3                      0
#define ISP_BASE__INST4_SEG4                      0

#define SYSTEMHUB_BASE__INST0_SEG0                0x00000EA0
#define SYSTEMHUB_BASE__INST0_SEG1                0
#define SYSTEMHUB_BASE__INST0_SEG2                0
#define SYSTEMHUB_BASE__INST0_SEG3                0
#define SYSTEMHUB_BASE__INST0_SEG4                0

#define SYSTEMHUB_BASE__INST1_SEG0                0
#define SYSTEMHUB_BASE__INST1_SEG1                0
#define SYSTEMHUB_BASE__INST1_SEG2                0
#define SYSTEMHUB_BASE__INST1_SEG3                0
#define SYSTEMHUB_BASE__INST1_SEG4                0

#define SYSTEMHUB_BASE__INST2_SEG0                0
#define SYSTEMHUB_BASE__INST2_SEG1                0
#define SYSTEMHUB_BASE__INST2_SEG2                0
#define SYSTEMHUB_BASE__INST2_SEG3                0
#define SYSTEMHUB_BASE__INST2_SEG4                0

#define SYSTEMHUB_BASE__INST3_SEG0                0
#define SYSTEMHUB_BASE__INST3_SEG1                0
#define SYSTEMHUB_BASE__INST3_SEG2                0
#define SYSTEMHUB_BASE__INST3_SEG3                0
#define SYSTEMHUB_BASE__INST3_SEG4                0

#define SYSTEMHUB_BASE__INST4_SEG0                0
#define SYSTEMHUB_BASE__INST4_SEG1                0
#define SYSTEMHUB_BASE__INST4_SEG2                0
#define SYSTEMHUB_BASE__INST4_SEG3                0
#define SYSTEMHUB_BASE__INST4_SEG4                0

#define L2IMU_BASE__INST0_SEG0                    0x00007DC0
#define L2IMU_BASE__INST0_SEG1                    0
#define L2IMU_BASE__INST0_SEG2                    0
#define L2IMU_BASE__INST0_SEG3                    0
#define L2IMU_BASE__INST0_SEG4                    0

#define L2IMU_BASE__INST1_SEG0                    0
#define L2IMU_BASE__INST1_SEG1                    0
#define L2IMU_BASE__INST1_SEG2                    0
#define L2IMU_BASE__INST1_SEG3                    0
#define L2IMU_BASE__INST1_SEG4                    0

#define L2IMU_BASE__INST2_SEG0                    0
#define L2IMU_BASE__INST2_SEG1                    0
#define L2IMU_BASE__INST2_SEG2                    0
#define L2IMU_BASE__INST2_SEG3                    0
#define L2IMU_BASE__INST2_SEG4                    0

#define L2IMU_BASE__INST3_SEG0                    0
#define L2IMU_BASE__INST3_SEG1                    0
#define L2IMU_BASE__INST3_SEG2                    0
#define L2IMU_BASE__INST3_SEG3                    0
#define L2IMU_BASE__INST3_SEG4                    0

#define L2IMU_BASE__INST4_SEG0                    0
#define L2IMU_BASE__INST4_SEG1                    0
#define L2IMU_BASE__INST4_SEG2                    0
#define L2IMU_BASE__INST4_SEG3                    0
#define L2IMU_BASE__INST4_SEG4                    0

#define IOHC_BASE__INST0_SEG0                     0x00010000
#define IOHC_BASE__INST0_SEG1                     0
#define IOHC_BASE__INST0_SEG2                     0
#define IOHC_BASE__INST0_SEG3                     0
#define IOHC_BASE__INST0_SEG4                     0

#define IOHC_BASE__INST1_SEG0                     0
#define IOHC_BASE__INST1_SEG1                     0
#define IOHC_BASE__INST1_SEG2                     0
#define IOHC_BASE__INST1_SEG3                     0
#define IOHC_BASE__INST1_SEG4                     0

#define IOHC_BASE__INST2_SEG0                     0
#define IOHC_BASE__INST2_SEG1                     0
#define IOHC_BASE__INST2_SEG2                     0
#define IOHC_BASE__INST2_SEG3                     0
#define IOHC_BASE__INST2_SEG4                     0

#define IOHC_BASE__INST3_SEG0                     0
#define IOHC_BASE__INST3_SEG1                     0
#define IOHC_BASE__INST3_SEG2                     0
#define IOHC_BASE__INST3_SEG3                     0
#define IOHC_BASE__INST3_SEG4                     0

#define IOHC_BASE__INST4_SEG0                     0
#define IOHC_BASE__INST4_SEG1                     0
#define IOHC_BASE__INST4_SEG2                     0
#define IOHC_BASE__INST4_SEG3                     0
#define IOHC_BASE__INST4_SEG4                     0

#define ATHUB_BASE__INST0_SEG0                    0x00000C20
#define ATHUB_BASE__INST0_SEG1                    0
#define ATHUB_BASE__INST0_SEG2                    0
#define ATHUB_BASE__INST0_SEG3                    0
#define ATHUB_BASE__INST0_SEG4                    0

#define ATHUB_BASE__INST1_SEG0                    0
#define ATHUB_BASE__INST1_SEG1                    0
#define ATHUB_BASE__INST1_SEG2                    0
#define ATHUB_BASE__INST1_SEG3                    0
#define ATHUB_BASE__INST1_SEG4                    0

#define ATHUB_BASE__INST2_SEG0                    0
#define ATHUB_BASE__INST2_SEG1                    0
#define ATHUB_BASE__INST2_SEG2                    0
#define ATHUB_BASE__INST2_SEG3                    0
#define ATHUB_BASE__INST2_SEG4                    0

#define ATHUB_BASE__INST3_SEG0                    0
#define ATHUB_BASE__INST3_SEG1                    0
#define ATHUB_BASE__INST3_SEG2                    0
#define ATHUB_BASE__INST3_SEG3                    0
#define ATHUB_BASE__INST3_SEG4                    0

#define ATHUB_BASE__INST4_SEG0                    0
#define ATHUB_BASE__INST4_SEG1                    0
#define ATHUB_BASE__INST4_SEG2                    0
#define ATHUB_BASE__INST4_SEG3                    0
#define ATHUB_BASE__INST4_SEG4                    0

#define VCE_BASE__INST0_SEG0                      0x00007E00
#define VCE_BASE__INST0_SEG1                      0x00048800
#define VCE_BASE__INST0_SEG2                      0
#define VCE_BASE__INST0_SEG3                      0
#define VCE_BASE__INST0_SEG4                      0

#define VCE_BASE__INST1_SEG0                      0
#define VCE_BASE__INST1_SEG1                      0
#define VCE_BASE__INST1_SEG2                      0
#define VCE_BASE__INST1_SEG3                      0
#define VCE_BASE__INST1_SEG4                      0

#define VCE_BASE__INST2_SEG0                      0
#define VCE_BASE__INST2_SEG1                      0
#define VCE_BASE__INST2_SEG2                      0
#define VCE_BASE__INST2_SEG3                      0
#define VCE_BASE__INST2_SEG4                      0

#define VCE_BASE__INST3_SEG0                      0
#define VCE_BASE__INST3_SEG1                      0
#define VCE_BASE__INST3_SEG2                      0
#define VCE_BASE__INST3_SEG3                      0
#define VCE_BASE__INST3_SEG4                      0

#define VCE_BASE__INST4_SEG0                      0
#define VCE_BASE__INST4_SEG1                      0
#define VCE_BASE__INST4_SEG2                      0
#define VCE_BASE__INST4_SEG3                      0
#define VCE_BASE__INST4_SEG4                      0

#define GC_BASE__INST0_SEG0                       0x00002000
#define GC_BASE__INST0_SEG1                       0x0000A000
#define GC_BASE__INST0_SEG2                       0
#define GC_BASE__INST0_SEG3                       0
#define GC_BASE__INST0_SEG4                       0

#define GC_BASE__INST1_SEG0                       0
#define GC_BASE__INST1_SEG1                       0
#define GC_BASE__INST1_SEG2                       0
#define GC_BASE__INST1_SEG3                       0
#define GC_BASE__INST1_SEG4                       0

#define GC_BASE__INST2_SEG0                       0
#define GC_BASE__INST2_SEG1                       0
#define GC_BASE__INST2_SEG2                       0
#define GC_BASE__INST2_SEG3                       0
#define GC_BASE__INST2_SEG4                       0

#define GC_BASE__INST3_SEG0                       0
#define GC_BASE__INST3_SEG1                       0
#define GC_BASE__INST3_SEG2                       0
#define GC_BASE__INST3_SEG3                       0
#define GC_BASE__INST3_SEG4                       0

#define GC_BASE__INST4_SEG0                       0
#define GC_BASE__INST4_SEG1                       0
#define GC_BASE__INST4_SEG2                       0
#define GC_BASE__INST4_SEG3                       0
#define GC_BASE__INST4_SEG4                       0

#define MMHUB_BASE__INST0_SEG0                    0x0001A000
#define MMHUB_BASE__INST0_SEG1                    0
#define MMHUB_BASE__INST0_SEG2                    0
#define MMHUB_BASE__INST0_SEG3                    0
#define MMHUB_BASE__INST0_SEG4                    0

#define MMHUB_BASE__INST1_SEG0                    0
#define MMHUB_BASE__INST1_SEG1                    0
#define MMHUB_BASE__INST1_SEG2                    0
#define MMHUB_BASE__INST1_SEG3                    0
#define MMHUB_BASE__INST1_SEG4                    0

#define MMHUB_BASE__INST2_SEG0                    0
#define MMHUB_BASE__INST2_SEG1                    0
#define MMHUB_BASE__INST2_SEG2                    0
#define MMHUB_BASE__INST2_SEG3                    0
#define MMHUB_BASE__INST2_SEG4                    0

#define MMHUB_BASE__INST3_SEG0                    0
#define MMHUB_BASE__INST3_SEG1                    0
#define MMHUB_BASE__INST3_SEG2                    0
#define MMHUB_BASE__INST3_SEG3                    0
#define MMHUB_BASE__INST3_SEG4                    0

#define MMHUB_BASE__INST4_SEG0                    0
#define MMHUB_BASE__INST4_SEG1                    0
#define MMHUB_BASE__INST4_SEG2                    0
#define MMHUB_BASE__INST4_SEG3                    0
#define MMHUB_BASE__INST4_SEG4                    0

#define RSMU_BASE__INST0_SEG0                     0x00012000
#define RSMU_BASE__INST0_SEG1                     0
#define RSMU_BASE__INST0_SEG2                     0
#define RSMU_BASE__INST0_SEG3                     0
#define RSMU_BASE__INST0_SEG4                     0

#define RSMU_BASE__INST1_SEG0                     0
#define RSMU_BASE__INST1_SEG1                     0
#define RSMU_BASE__INST1_SEG2                     0
#define RSMU_BASE__INST1_SEG3                     0
#define RSMU_BASE__INST1_SEG4                     0

#define RSMU_BASE__INST2_SEG0                     0
#define RSMU_BASE__INST2_SEG1                     0
#define RSMU_BASE__INST2_SEG2                     0
#define RSMU_BASE__INST2_SEG3                     0
#define RSMU_BASE__INST2_SEG4                     0

#define RSMU_BASE__INST3_SEG0                     0
#define RSMU_BASE__INST3_SEG1                     0
#define RSMU_BASE__INST3_SEG2                     0
#define RSMU_BASE__INST3_SEG3                     0
#define RSMU_BASE__INST3_SEG4                     0

#define RSMU_BASE__INST4_SEG0                     0
#define RSMU_BASE__INST4_SEG1                     0
#define RSMU_BASE__INST4_SEG2                     0
#define RSMU_BASE__INST4_SEG3                     0
#define RSMU_BASE__INST4_SEG4                     0

#define HDP_BASE__INST0_SEG0                      0x00000F20
#define HDP_BASE__INST0_SEG1                      0
#define HDP_BASE__INST0_SEG2                      0
#define HDP_BASE__INST0_SEG3                      0
#define HDP_BASE__INST0_SEG4                      0

#define HDP_BASE__INST1_SEG0                      0
#define HDP_BASE__INST1_SEG1                      0
#define HDP_BASE__INST1_SEG2                      0
#define HDP_BASE__INST1_SEG3                      0
#define HDP_BASE__INST1_SEG4                      0

#define HDP_BASE__INST2_SEG0                      0
#define HDP_BASE__INST2_SEG1                      0
#define HDP_BASE__INST2_SEG2                      0
#define HDP_BASE__INST2_SEG3                      0
#define HDP_BASE__INST2_SEG4                      0

#define HDP_BASE__INST3_SEG0                      0
#define HDP_BASE__INST3_SEG1                      0
#define HDP_BASE__INST3_SEG2                      0
#define HDP_BASE__INST3_SEG3                      0
#define HDP_BASE__INST3_SEG4                      0

#define HDP_BASE__INST4_SEG0                      0
#define HDP_BASE__INST4_SEG1                      0
#define HDP_BASE__INST4_SEG2                      0
#define HDP_BASE__INST4_SEG3                      0
#define HDP_BASE__INST4_SEG4                      0

#define OSSSYS_BASE__INST0_SEG0                   0x000010A0
#define OSSSYS_BASE__INST0_SEG1                   0
#define OSSSYS_BASE__INST0_SEG2                   0
#define OSSSYS_BASE__INST0_SEG3                   0
#define OSSSYS_BASE__INST0_SEG4                   0

#define OSSSYS_BASE__INST1_SEG0                   0
#define OSSSYS_BASE__INST1_SEG1                   0
#define OSSSYS_BASE__INST1_SEG2                   0
#define OSSSYS_BASE__INST1_SEG3                   0
#define OSSSYS_BASE__INST1_SEG4                   0

#define OSSSYS_BASE__INST2_SEG0                   0
#define OSSSYS_BASE__INST2_SEG1                   0
#define OSSSYS_BASE__INST2_SEG2                   0
#define OSSSYS_BASE__INST2_SEG3                   0
#define OSSSYS_BASE__INST2_SEG4                   0

#define OSSSYS_BASE__INST3_SEG0                   0
#define OSSSYS_BASE__INST3_SEG1                   0
#define OSSSYS_BASE__INST3_SEG2                   0
#define OSSSYS_BASE__INST3_SEG3                   0
#define OSSSYS_BASE__INST3_SEG4                   0

#define OSSSYS_BASE__INST4_SEG0                   0
#define OSSSYS_BASE__INST4_SEG1                   0
#define OSSSYS_BASE__INST4_SEG2                   0
#define OSSSYS_BASE__INST4_SEG3                   0
#define OSSSYS_BASE__INST4_SEG4                   0

#define SDMA0_BASE__INST0_SEG0                    0x00001260
#define SDMA0_BASE__INST0_SEG1                    0
#define SDMA0_BASE__INST0_SEG2                    0
#define SDMA0_BASE__INST0_SEG3                    0
#define SDMA0_BASE__INST0_SEG4                    0

#define SDMA0_BASE__INST1_SEG0                    0
#define SDMA0_BASE__INST1_SEG1                    0
#define SDMA0_BASE__INST1_SEG2                    0
#define SDMA0_BASE__INST1_SEG3                    0
#define SDMA0_BASE__INST1_SEG4                    0

#define SDMA0_BASE__INST2_SEG0                    0
#define SDMA0_BASE__INST2_SEG1                    0
#define SDMA0_BASE__INST2_SEG2                    0
#define SDMA0_BASE__INST2_SEG3                    0
#define SDMA0_BASE__INST2_SEG4                    0

#define SDMA0_BASE__INST3_SEG0                    0
#define SDMA0_BASE__INST3_SEG1                    0
#define SDMA0_BASE__INST3_SEG2                    0
#define SDMA0_BASE__INST3_SEG3                    0
#define SDMA0_BASE__INST3_SEG4                    0

#define SDMA0_BASE__INST4_SEG0                    0
#define SDMA0_BASE__INST4_SEG1                    0
#define SDMA0_BASE__INST4_SEG2                    0
#define SDMA0_BASE__INST4_SEG3                    0
#define SDMA0_BASE__INST4_SEG4                    0

#define SDMA1_BASE__INST0_SEG0                    0x00001460
#define SDMA1_BASE__INST0_SEG1                    0
#define SDMA1_BASE__INST0_SEG2                    0
#define SDMA1_BASE__INST0_SEG3                    0
#define SDMA1_BASE__INST0_SEG4                    0

#define SDMA1_BASE__INST1_SEG0                    0
#define SDMA1_BASE__INST1_SEG1                    0
#define SDMA1_BASE__INST1_SEG2                    0
#define SDMA1_BASE__INST1_SEG3                    0
#define SDMA1_BASE__INST1_SEG4                    0

#define SDMA1_BASE__INST2_SEG0                    0
#define SDMA1_BASE__INST2_SEG1                    0
#define SDMA1_BASE__INST2_SEG2                    0
#define SDMA1_BASE__INST2_SEG3                    0
#define SDMA1_BASE__INST2_SEG4                    0

#define SDMA1_BASE__INST3_SEG0                    0
#define SDMA1_BASE__INST3_SEG1                    0
#define SDMA1_BASE__INST3_SEG2                    0
#define SDMA1_BASE__INST3_SEG3                    0
#define SDMA1_BASE__INST3_SEG4                    0

#define SDMA1_BASE__INST4_SEG0                    0
#define SDMA1_BASE__INST4_SEG1                    0
#define SDMA1_BASE__INST4_SEG2                    0
#define SDMA1_BASE__INST4_SEG3                    0
#define SDMA1_BASE__INST4_SEG4                    0

#define XDMA_BASE__INST0_SEG0                     0x00003400
#define XDMA_BASE__INST0_SEG1                     0
#define XDMA_BASE__INST0_SEG2                     0
#define XDMA_BASE__INST0_SEG3                     0
#define XDMA_BASE__INST0_SEG4                     0

#define XDMA_BASE__INST1_SEG0                     0
#define XDMA_BASE__INST1_SEG1                     0
#define XDMA_BASE__INST1_SEG2                     0
#define XDMA_BASE__INST1_SEG3                     0
#define XDMA_BASE__INST1_SEG4                     0

#define XDMA_BASE__INST2_SEG0                     0
#define XDMA_BASE__INST2_SEG1                     0
#define XDMA_BASE__INST2_SEG2                     0
#define XDMA_BASE__INST2_SEG3                     0
#define XDMA_BASE__INST2_SEG4                     0

#define XDMA_BASE__INST3_SEG0                     0
#define XDMA_BASE__INST3_SEG1                     0
#define XDMA_BASE__INST3_SEG2                     0
#define XDMA_BASE__INST3_SEG3                     0
#define XDMA_BASE__INST3_SEG4                     0

#define XDMA_BASE__INST4_SEG0                     0
#define XDMA_BASE__INST4_SEG1                     0
#define XDMA_BASE__INST4_SEG2                     0
#define XDMA_BASE__INST4_SEG3                     0
#define XDMA_BASE__INST4_SEG4                     0

#define UMC_BASE__INST0_SEG0                      0x00014000
#define UMC_BASE__INST0_SEG1                      0
#define UMC_BASE__INST0_SEG2                      0
#define UMC_BASE__INST0_SEG3                      0
#define UMC_BASE__INST0_SEG4                      0

#define UMC_BASE__INST1_SEG0                      0
#define UMC_BASE__INST1_SEG1                      0
#define UMC_BASE__INST1_SEG2                      0
#define UMC_BASE__INST1_SEG3                      0
#define UMC_BASE__INST1_SEG4                      0

#define UMC_BASE__INST2_SEG0                      0
#define UMC_BASE__INST2_SEG1                      0
#define UMC_BASE__INST2_SEG2                      0
#define UMC_BASE__INST2_SEG3                      0
#define UMC_BASE__INST2_SEG4                      0

#define UMC_BASE__INST3_SEG0                      0
#define UMC_BASE__INST3_SEG1                      0
#define UMC_BASE__INST3_SEG2                      0
#define UMC_BASE__INST3_SEG3                      0
#define UMC_BASE__INST3_SEG4                      0

#define UMC_BASE__INST4_SEG0                      0
#define UMC_BASE__INST4_SEG1                      0
#define UMC_BASE__INST4_SEG2                      0
#define UMC_BASE__INST4_SEG3                      0
#define UMC_BASE__INST4_SEG4                      0

#define THM_BASE__INST0_SEG0                      0x00016600
#define THM_BASE__INST0_SEG1                      0
#define THM_BASE__INST0_SEG2                      0
#define THM_BASE__INST0_SEG3                      0
#define THM_BASE__INST0_SEG4                      0

#define THM_BASE__INST1_SEG0                      0
#define THM_BASE__INST1_SEG1                      0
#define THM_BASE__INST1_SEG2                      0
#define THM_BASE__INST1_SEG3                      0
#define THM_BASE__INST1_SEG4                      0

#define THM_BASE__INST2_SEG0                      0
#define THM_BASE__INST2_SEG1                      0
#define THM_BASE__INST2_SEG2                      0
#define THM_BASE__INST2_SEG3                      0
#define THM_BASE__INST2_SEG4                      0

#define THM_BASE__INST3_SEG0                      0
#define THM_BASE__INST3_SEG1                      0
#define THM_BASE__INST3_SEG2                      0
#define THM_BASE__INST3_SEG3                      0
#define THM_BASE__INST3_SEG4                      0

#define THM_BASE__INST4_SEG0                      0
#define THM_BASE__INST4_SEG1                      0
#define THM_BASE__INST4_SEG2                      0
#define THM_BASE__INST4_SEG3                      0
#define THM_BASE__INST4_SEG4                      0

#define SMUIO_BASE__INST0_SEG0                    0x00016800
#define SMUIO_BASE__INST0_SEG1                    0
#define SMUIO_BASE__INST0_SEG2                    0
#define SMUIO_BASE__INST0_SEG3                    0
#define SMUIO_BASE__INST0_SEG4                    0

#define SMUIO_BASE__INST1_SEG0                    0
#define SMUIO_BASE__INST1_SEG1                    0
#define SMUIO_BASE__INST1_SEG2                    0
#define SMUIO_BASE__INST1_SEG3                    0
#define SMUIO_BASE__INST1_SEG4                    0

#define SMUIO_BASE__INST2_SEG0                    0
#define SMUIO_BASE__INST2_SEG1                    0
#define SMUIO_BASE__INST2_SEG2                    0
#define SMUIO_BASE__INST2_SEG3                    0
#define SMUIO_BASE__INST2_SEG4                    0

#define SMUIO_BASE__INST3_SEG0                    0
#define SMUIO_BASE__INST3_SEG1                    0
#define SMUIO_BASE__INST3_SEG2                    0
#define SMUIO_BASE__INST3_SEG3                    0
#define SMUIO_BASE__INST3_SEG4                    0

#define SMUIO_BASE__INST4_SEG0                    0
#define SMUIO_BASE__INST4_SEG1                    0
#define SMUIO_BASE__INST4_SEG2                    0
#define SMUIO_BASE__INST4_SEG3                    0
#define SMUIO_BASE__INST4_SEG4                    0

#define PWR_BASE__INST0_SEG0                      0x00016A00
#define PWR_BASE__INST0_SEG1                      0
#define PWR_BASE__INST0_SEG2                      0
#define PWR_BASE__INST0_SEG3                      0
#define PWR_BASE__INST0_SEG4                      0

#define PWR_BASE__INST1_SEG0                      0
#define PWR_BASE__INST1_SEG1                      0
#define PWR_BASE__INST1_SEG2                      0
#define PWR_BASE__INST1_SEG3                      0
#define PWR_BASE__INST1_SEG4                      0

#define PWR_BASE__INST2_SEG0                      0
#define PWR_BASE__INST2_SEG1                      0
#define PWR_BASE__INST2_SEG2                      0
#define PWR_BASE__INST2_SEG3                      0
#define PWR_BASE__INST2_SEG4                      0

#define PWR_BASE__INST3_SEG0                      0
#define PWR_BASE__INST3_SEG1                      0
#define PWR_BASE__INST3_SEG2                      0
#define PWR_BASE__INST3_SEG3                      0
#define PWR_BASE__INST3_SEG4                      0

#define PWR_BASE__INST4_SEG0                      0
#define PWR_BASE__INST4_SEG1                      0
#define PWR_BASE__INST4_SEG2                      0
#define PWR_BASE__INST4_SEG3                      0
#define PWR_BASE__INST4_SEG4                      0

#define CLK_BASE__INST0_SEG0                      0x00016C00
#define CLK_BASE__INST0_SEG1                      0
#define CLK_BASE__INST0_SEG2                      0
#define CLK_BASE__INST0_SEG3                      0
#define CLK_BASE__INST0_SEG4                      0

#define CLK_BASE__INST1_SEG0                      0x00016E00
#define CLK_BASE__INST1_SEG1                      0
#define CLK_BASE__INST1_SEG2                      0
#define CLK_BASE__INST1_SEG3                      0
#define CLK_BASE__INST1_SEG4                      0

#define CLK_BASE__INST2_SEG0                      0x00017000
#define CLK_BASE__INST2_SEG1                      0
#define CLK_BASE__INST2_SEG2                      0
#define CLK_BASE__INST2_SEG3                      0
#define CLK_BASE__INST2_SEG4                      0

#define CLK_BASE__INST3_SEG0                      0x00017200
#define CLK_BASE__INST3_SEG1                      0
#define CLK_BASE__INST3_SEG2                      0
#define CLK_BASE__INST3_SEG3                      0
#define CLK_BASE__INST3_SEG4                      0

#define CLK_BASE__INST4_SEG0                      0x00017E00
#define CLK_BASE__INST4_SEG1                      0
#define CLK_BASE__INST4_SEG2                      0
#define CLK_BASE__INST4_SEG3                      0
#define CLK_BASE__INST4_SEG4                      0

#define FUSE_BASE__INST0_SEG0                     0x00017400
#define FUSE_BASE__INST0_SEG1                     0
#define FUSE_BASE__INST0_SEG2                     0
#define FUSE_BASE__INST0_SEG3                     0
#define FUSE_BASE__INST0_SEG4                     0

#define FUSE_BASE__INST1_SEG0                     0
#define FUSE_BASE__INST1_SEG1                     0
#define FUSE_BASE__INST1_SEG2                     0
#define FUSE_BASE__INST1_SEG3                     0
#define FUSE_BASE__INST1_SEG4                     0

#define FUSE_BASE__INST2_SEG0                     0
#define FUSE_BASE__INST2_SEG1                     0
#define FUSE_BASE__INST2_SEG2                     0
#define FUSE_BASE__INST2_SEG3                     0
#define FUSE_BASE__INST2_SEG4                     0

#define FUSE_BASE__INST3_SEG0                     0
#define FUSE_BASE__INST3_SEG1                     0
#define FUSE_BASE__INST3_SEG2                     0
#define FUSE_BASE__INST3_SEG3                     0
#define FUSE_BASE__INST3_SEG4                     0

#define FUSE_BASE__INST4_SEG0                     0
#define FUSE_BASE__INST4_SEG1                     0
#define FUSE_BASE__INST4_SEG2                     0
#define FUSE_BASE__INST4_SEG3                     0
#define FUSE_BASE__INST4_SEG4                     0


#endif

