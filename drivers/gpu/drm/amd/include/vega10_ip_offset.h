/*
 * Copyright (C) 2018  Advanced Micro Devices, Inc.
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
#ifndef _vega10_ip_offset_HEADER
#define _vega10_ip_offset_HEADER

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


static const struct IP_BASE __maybe_unused NBIF_BASE	= { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused NBIO_BASE	= { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused DCE_BASE	= { { { { 0x00000012, 0x000000C0, 0x000034C0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused DCN_BASE	= { { { { 0x00000012, 0x000000C0, 0x000034C0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused MP0_BASE	= { { { { 0x00016000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused MP1_BASE	= { { { { 0x00016000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused MP2_BASE	= { { { { 0x00016000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused DF_BASE	= { { { { 0x00007000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused UVD_BASE	= { { { { 0x00007800, 0x00007E00, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };  //note: GLN does not use the first segment
static const struct IP_BASE __maybe_unused VCN_BASE	= { { { { 0x00007800, 0x00007E00, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };  //note: GLN does not use the first segment
static const struct IP_BASE __maybe_unused DBGU_BASE	= { { { { 0x00000180, 0x000001A0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE __maybe_unused DBGU_NBIO_BASE	= { { { { 0x000001C0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE __maybe_unused DBGU_IO_BASE	= { { { { 0x000001E0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE __maybe_unused DFX_DAP_BASE	= { { { { 0x000005A0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE __maybe_unused DFX_BASE	= { { { { 0x00000580, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // this file does not contain registers
static const struct IP_BASE __maybe_unused ISP_BASE	= { { { { 0x00018000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE __maybe_unused SYSTEMHUB_BASE	= { { { { 0x00000EA0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } }; // not exist
static const struct IP_BASE __maybe_unused L2IMU_BASE	= { { { { 0x00007DC0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused IOHC_BASE	= { { { { 0x00010000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused ATHUB_BASE	= { { { { 0x00000C20, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused VCE_BASE	= { { { { 0x00007E00, 0x00048800, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused GC_BASE	= { { { { 0x00002000, 0x0000A000, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused MMHUB_BASE	= { { { { 0x0001A000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused RSMU_BASE	= { { { { 0x00012000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused HDP_BASE	= { { { { 0x00000F20, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused OSSSYS_BASE	 = { { { { 0x000010A0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused SDMA0_BASE	= { { { { 0x00001260, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused SDMA1_BASE	= { { { { 0x00001460, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused XDMA_BASE	= { { { { 0x00003400, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused UMC_BASE	= { { { { 0x00014000, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused THM_BASE	= { { { { 0x00016600, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused SMUIO_BASE	= { { { { 0x00016800, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused PWR_BASE	= { { { { 0x00016A00, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } },
										{ { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused CLK_BASE	= { { { { 0x00016C00, 0, 0, 0, 0 } },
										{ { 0x00016E00, 0, 0, 0, 0 } },
										{ { 0x00017000, 0, 0, 0, 0 } },
										{ { 0x00017200, 0, 0, 0, 0 } },
										{ { 0x00017E00, 0, 0, 0, 0 } } } };
static const struct IP_BASE __maybe_unused FUSE_BASE	= { { { { 0x00017400, 0, 0, 0, 0 } },
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

