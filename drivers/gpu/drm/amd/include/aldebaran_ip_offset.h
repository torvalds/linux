/*
 * Copyright (C) 2020  Advanced Micro Devices, Inc.
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
#ifndef _aldebaran_ip_offset_HEADER
#define _aldebaran_ip_offset_HEADER

#define MAX_INSTANCE                                        7
#define MAX_SEGMENT                                         6

struct IP_BASE_INSTANCE {
    unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE {
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};

static const struct IP_BASE ATHUB_BASE = { { { { 0x00000C20, 0x02408C00, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE CLK_BASE = { { { { 0x00016C00, 0x02401800, 0, 0, 0, 0 } },
                                        { { 0x00016E00, 0x02401C00, 0, 0, 0, 0 } },
                                        { { 0x00017000, 0x02402000, 0, 0, 0, 0 } },
                                        { { 0x00017200, 0x02402400, 0, 0, 0, 0 } },
                                        { { 0x0001B000, 0x0242D800, 0, 0, 0, 0 } },
                                        { { 0x0001B200, 0x0242DC00, 0, 0, 0, 0 } },
                                        { { 0x00017E00, 0x0240BC00, 0, 0, 0, 0 } } } };
static const struct IP_BASE DBGU_IO0_BASE = { { { { 0x000001E0, 0x0240B400, 0, 0, 0, 0 } },
                                        { { 0x00000260, 0x02413C00, 0, 0, 0, 0 } },
                                        { { 0x00000280, 0x02416000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DF_BASE = { { { { 0x00007000, 0x0240B800, 0x07C00000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE FUSE_BASE = { { { { 0x00017400, 0x02401400, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE GC_BASE = { { { { 0x00002000, 0x0000A000, 0x02402C00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE HDP_BASE = { { { { 0x00000F20, 0x0240A400, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE IOAGR0_BASE = { { { { 0x02419000, 0x056C0000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE IOAPIC0_BASE = { { { { 0x00A00000, 0x0241F000, 0x050C0000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE IOHC0_BASE = { { { { 0x00010000, 0x02406000, 0x04EC0000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE L1IMUIOAGR0_BASE = { { { { 0x0240CC00, 0x05200000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE L1IMUPCIE0_BASE = { { { { 0x0240C800, 0x051C0000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE L2IMU0_BASE = { { { { 0x00007DC0, 0x00900000, 0x02407000, 0x04FC0000, 0x055C0000, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MMHUB_BASE = { { { { 0x0001A000, 0x02408800, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP0_BASE = { { { { 0x00016000, 0x00DC0000, 0x00E00000, 0x00E40000, 0x0243FC00, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP1_BASE = { { { { 0x00016000, 0x00DC0000, 0x00E00000, 0x00E40000, 0x0243FC00, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE NBIO_BASE = { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0x0241B000, 0x04040000 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE OSSSYS_BASE = { { { { 0x000010A0, 0x0240A000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE PCIE0_BASE = { { { { 0x02411800, 0x04440000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA0_BASE = { { { { 0x00001260, 0x00012540, 0x0040A800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA1_BASE = { { { { 0x00001860, 0x00012560, 0x0040AC00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA2_BASE = { { { { 0x00013760, 0x0001E000, 0x0042EC00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA3_BASE = { { { { 0x00013780, 0x0001E400, 0x0042F000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA4_BASE = { { { { 0x000137A0, 0x0001E800, 0x0042F400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SMUIO_BASE = { { { { 0x00016800, 0x00016A00, 0x02401000, 0x03440000, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE THM_BASE = { { { { 0x00016600, 0x02400C00, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE UMC_BASE = { { { { 0x00014000, 0x00054000, 0x02425800, 0, 0, 0 } },
                                        { { 0x00094000, 0x000D4000, 0x02425C00, 0, 0, 0 } },
                                        { { 0x00114000, 0x00154000, 0x02426000, 0, 0, 0 } },
                                        { { 0x00194000, 0x001D4000, 0x02426400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE VCN_BASE = { { { { 0x00007800, 0x00007E00, 0x02403000, 0, 0, 0 } },
                                        { { 0x00007A00, 0x00009000, 0x02445000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE WAFL0_BASE = { { { { 0x02438000, 0x04880000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE WAFL1_BASE = { { { { 0, 0x01300000, 0x02410800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE XGMI0_BASE = { { { { 0x02438C00, 0x04680000, 0x04940000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE XGMI1_BASE = { { { { 0x02439000, 0x046C0000, 0x04980000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE XGMI2_BASE = { { { { 0x04700000, 0x049C0000, 0, 0, 0, 0 } },
                                        { { 0x04740000, 0x04A00000, 0, 0, 0, 0 } },
                                        { { 0x04780000, 0x04A40000, 0, 0, 0, 0 } },
                                        { { 0x047C0000, 0x04A80000, 0, 0, 0, 0 } },
                                        { { 0x04800000, 0x04AC0000, 0, 0, 0, 0 } },
                                        { { 0x04840000, 0x04B00000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };


#define ATHUB_BASE__INST0_SEG0                     0x00000C20
#define ATHUB_BASE__INST0_SEG1                     0x02408C00
#define ATHUB_BASE__INST0_SEG2                     0
#define ATHUB_BASE__INST0_SEG3                     0
#define ATHUB_BASE__INST0_SEG4                     0
#define ATHUB_BASE__INST0_SEG5                     0

#define ATHUB_BASE__INST1_SEG0                     0
#define ATHUB_BASE__INST1_SEG1                     0
#define ATHUB_BASE__INST1_SEG2                     0
#define ATHUB_BASE__INST1_SEG3                     0
#define ATHUB_BASE__INST1_SEG4                     0
#define ATHUB_BASE__INST1_SEG5                     0

#define ATHUB_BASE__INST2_SEG0                     0
#define ATHUB_BASE__INST2_SEG1                     0
#define ATHUB_BASE__INST2_SEG2                     0
#define ATHUB_BASE__INST2_SEG3                     0
#define ATHUB_BASE__INST2_SEG4                     0
#define ATHUB_BASE__INST2_SEG5                     0

#define ATHUB_BASE__INST3_SEG0                     0
#define ATHUB_BASE__INST3_SEG1                     0
#define ATHUB_BASE__INST3_SEG2                     0
#define ATHUB_BASE__INST3_SEG3                     0
#define ATHUB_BASE__INST3_SEG4                     0
#define ATHUB_BASE__INST3_SEG5                     0

#define ATHUB_BASE__INST4_SEG0                     0
#define ATHUB_BASE__INST4_SEG1                     0
#define ATHUB_BASE__INST4_SEG2                     0
#define ATHUB_BASE__INST4_SEG3                     0
#define ATHUB_BASE__INST4_SEG4                     0
#define ATHUB_BASE__INST4_SEG5                     0

#define ATHUB_BASE__INST5_SEG0                     0
#define ATHUB_BASE__INST5_SEG1                     0
#define ATHUB_BASE__INST5_SEG2                     0
#define ATHUB_BASE__INST5_SEG3                     0
#define ATHUB_BASE__INST5_SEG4                     0
#define ATHUB_BASE__INST5_SEG5                     0

#define ATHUB_BASE__INST6_SEG0                     0
#define ATHUB_BASE__INST6_SEG1                     0
#define ATHUB_BASE__INST6_SEG2                     0
#define ATHUB_BASE__INST6_SEG3                     0
#define ATHUB_BASE__INST6_SEG4                     0
#define ATHUB_BASE__INST6_SEG5                     0

#define CLK_BASE__INST0_SEG0                       0x00016C00
#define CLK_BASE__INST0_SEG1                       0x02401800
#define CLK_BASE__INST0_SEG2                       0
#define CLK_BASE__INST0_SEG3                       0
#define CLK_BASE__INST0_SEG4                       0
#define CLK_BASE__INST0_SEG5                       0

#define CLK_BASE__INST1_SEG0                       0x00016E00
#define CLK_BASE__INST1_SEG1                       0x02401C00
#define CLK_BASE__INST1_SEG2                       0
#define CLK_BASE__INST1_SEG3                       0
#define CLK_BASE__INST1_SEG4                       0
#define CLK_BASE__INST1_SEG5                       0

#define CLK_BASE__INST2_SEG0                       0x00017000
#define CLK_BASE__INST2_SEG1                       0x02402000
#define CLK_BASE__INST2_SEG2                       0
#define CLK_BASE__INST2_SEG3                       0
#define CLK_BASE__INST2_SEG4                       0
#define CLK_BASE__INST2_SEG5                       0

#define CLK_BASE__INST3_SEG0                       0x00017200
#define CLK_BASE__INST3_SEG1                       0x02402400
#define CLK_BASE__INST3_SEG2                       0
#define CLK_BASE__INST3_SEG3                       0
#define CLK_BASE__INST3_SEG4                       0
#define CLK_BASE__INST3_SEG5                       0

#define CLK_BASE__INST4_SEG0                       0x0001B000
#define CLK_BASE__INST4_SEG1                       0x0242D800
#define CLK_BASE__INST4_SEG2                       0
#define CLK_BASE__INST4_SEG3                       0
#define CLK_BASE__INST4_SEG4                       0
#define CLK_BASE__INST4_SEG5                       0

#define CLK_BASE__INST5_SEG0                       0x0001B200
#define CLK_BASE__INST5_SEG1                       0x0242DC00
#define CLK_BASE__INST5_SEG2                       0
#define CLK_BASE__INST5_SEG3                       0
#define CLK_BASE__INST5_SEG4                       0
#define CLK_BASE__INST5_SEG5                       0

#define CLK_BASE__INST6_SEG0                       0x00017E00
#define CLK_BASE__INST6_SEG1                       0x0240BC00
#define CLK_BASE__INST6_SEG2                       0
#define CLK_BASE__INST6_SEG3                       0
#define CLK_BASE__INST6_SEG4                       0
#define CLK_BASE__INST6_SEG5                       0

#define DBGU_IO0_BASE__INST0_SEG0                  0x000001E0
#define DBGU_IO0_BASE__INST0_SEG1                  0x0240B400
#define DBGU_IO0_BASE__INST0_SEG2                  0
#define DBGU_IO0_BASE__INST0_SEG3                  0
#define DBGU_IO0_BASE__INST0_SEG4                  0
#define DBGU_IO0_BASE__INST0_SEG5                  0

#define DBGU_IO0_BASE__INST1_SEG0                  0x00000260
#define DBGU_IO0_BASE__INST1_SEG1                  0x02413C00
#define DBGU_IO0_BASE__INST1_SEG2                  0
#define DBGU_IO0_BASE__INST1_SEG3                  0
#define DBGU_IO0_BASE__INST1_SEG4                  0
#define DBGU_IO0_BASE__INST1_SEG5                  0

#define DBGU_IO0_BASE__INST2_SEG0                  0x00000280
#define DBGU_IO0_BASE__INST2_SEG1                  0x02416000
#define DBGU_IO0_BASE__INST2_SEG2                  0
#define DBGU_IO0_BASE__INST2_SEG3                  0
#define DBGU_IO0_BASE__INST2_SEG4                  0
#define DBGU_IO0_BASE__INST2_SEG5                  0

#define DBGU_IO0_BASE__INST3_SEG0                  0
#define DBGU_IO0_BASE__INST3_SEG1                  0
#define DBGU_IO0_BASE__INST3_SEG2                  0
#define DBGU_IO0_BASE__INST3_SEG3                  0
#define DBGU_IO0_BASE__INST3_SEG4                  0
#define DBGU_IO0_BASE__INST3_SEG5                  0

#define DBGU_IO0_BASE__INST4_SEG0                  0
#define DBGU_IO0_BASE__INST4_SEG1                  0
#define DBGU_IO0_BASE__INST4_SEG2                  0
#define DBGU_IO0_BASE__INST4_SEG3                  0
#define DBGU_IO0_BASE__INST4_SEG4                  0
#define DBGU_IO0_BASE__INST4_SEG5                  0

#define DBGU_IO0_BASE__INST5_SEG0                  0
#define DBGU_IO0_BASE__INST5_SEG1                  0
#define DBGU_IO0_BASE__INST5_SEG2                  0
#define DBGU_IO0_BASE__INST5_SEG3                  0
#define DBGU_IO0_BASE__INST5_SEG4                  0
#define DBGU_IO0_BASE__INST5_SEG5                  0

#define DBGU_IO0_BASE__INST6_SEG0                  0
#define DBGU_IO0_BASE__INST6_SEG1                  0
#define DBGU_IO0_BASE__INST6_SEG2                  0
#define DBGU_IO0_BASE__INST6_SEG3                  0
#define DBGU_IO0_BASE__INST6_SEG4                  0
#define DBGU_IO0_BASE__INST6_SEG5                  0

#define DF_BASE__INST0_SEG0                        0x00007000
#define DF_BASE__INST0_SEG1                        0x0240B800
#define DF_BASE__INST0_SEG2                        0x07C00000
#define DF_BASE__INST0_SEG3                        0
#define DF_BASE__INST0_SEG4                        0
#define DF_BASE__INST0_SEG5                        0

#define DF_BASE__INST1_SEG0                        0
#define DF_BASE__INST1_SEG1                        0
#define DF_BASE__INST1_SEG2                        0
#define DF_BASE__INST1_SEG3                        0
#define DF_BASE__INST1_SEG4                        0
#define DF_BASE__INST1_SEG5                        0

#define DF_BASE__INST2_SEG0                        0
#define DF_BASE__INST2_SEG1                        0
#define DF_BASE__INST2_SEG2                        0
#define DF_BASE__INST2_SEG3                        0
#define DF_BASE__INST2_SEG4                        0
#define DF_BASE__INST2_SEG5                        0

#define DF_BASE__INST3_SEG0                        0
#define DF_BASE__INST3_SEG1                        0
#define DF_BASE__INST3_SEG2                        0
#define DF_BASE__INST3_SEG3                        0
#define DF_BASE__INST3_SEG4                        0
#define DF_BASE__INST3_SEG5                        0

#define DF_BASE__INST4_SEG0                        0
#define DF_BASE__INST4_SEG1                        0
#define DF_BASE__INST4_SEG2                        0
#define DF_BASE__INST4_SEG3                        0
#define DF_BASE__INST4_SEG4                        0
#define DF_BASE__INST4_SEG5                        0

#define DF_BASE__INST5_SEG0                        0
#define DF_BASE__INST5_SEG1                        0
#define DF_BASE__INST5_SEG2                        0
#define DF_BASE__INST5_SEG3                        0
#define DF_BASE__INST5_SEG4                        0
#define DF_BASE__INST5_SEG5                        0

#define DF_BASE__INST6_SEG0                        0
#define DF_BASE__INST6_SEG1                        0
#define DF_BASE__INST6_SEG2                        0
#define DF_BASE__INST6_SEG3                        0
#define DF_BASE__INST6_SEG4                        0
#define DF_BASE__INST6_SEG5                        0

#define FUSE_BASE__INST0_SEG0                      0x00017400
#define FUSE_BASE__INST0_SEG1                      0x02401400
#define FUSE_BASE__INST0_SEG2                      0
#define FUSE_BASE__INST0_SEG3                      0
#define FUSE_BASE__INST0_SEG4                      0
#define FUSE_BASE__INST0_SEG5                      0

#define FUSE_BASE__INST1_SEG0                      0
#define FUSE_BASE__INST1_SEG1                      0
#define FUSE_BASE__INST1_SEG2                      0
#define FUSE_BASE__INST1_SEG3                      0
#define FUSE_BASE__INST1_SEG4                      0
#define FUSE_BASE__INST1_SEG5                      0

#define FUSE_BASE__INST2_SEG0                      0
#define FUSE_BASE__INST2_SEG1                      0
#define FUSE_BASE__INST2_SEG2                      0
#define FUSE_BASE__INST2_SEG3                      0
#define FUSE_BASE__INST2_SEG4                      0
#define FUSE_BASE__INST2_SEG5                      0

#define FUSE_BASE__INST3_SEG0                      0
#define FUSE_BASE__INST3_SEG1                      0
#define FUSE_BASE__INST3_SEG2                      0
#define FUSE_BASE__INST3_SEG3                      0
#define FUSE_BASE__INST3_SEG4                      0
#define FUSE_BASE__INST3_SEG5                      0

#define FUSE_BASE__INST4_SEG0                      0
#define FUSE_BASE__INST4_SEG1                      0
#define FUSE_BASE__INST4_SEG2                      0
#define FUSE_BASE__INST4_SEG3                      0
#define FUSE_BASE__INST4_SEG4                      0
#define FUSE_BASE__INST4_SEG5                      0

#define FUSE_BASE__INST5_SEG0                      0
#define FUSE_BASE__INST5_SEG1                      0
#define FUSE_BASE__INST5_SEG2                      0
#define FUSE_BASE__INST5_SEG3                      0
#define FUSE_BASE__INST5_SEG4                      0
#define FUSE_BASE__INST5_SEG5                      0

#define FUSE_BASE__INST6_SEG0                      0
#define FUSE_BASE__INST6_SEG1                      0
#define FUSE_BASE__INST6_SEG2                      0
#define FUSE_BASE__INST6_SEG3                      0
#define FUSE_BASE__INST6_SEG4                      0
#define FUSE_BASE__INST6_SEG5                      0

#define GC_BASE__INST0_SEG0                        0x00002000
#define GC_BASE__INST0_SEG1                        0x0000A000
#define GC_BASE__INST0_SEG2                        0x02402C00
#define GC_BASE__INST0_SEG3                        0
#define GC_BASE__INST0_SEG4                        0
#define GC_BASE__INST0_SEG5                        0

#define GC_BASE__INST1_SEG0                        0
#define GC_BASE__INST1_SEG1                        0
#define GC_BASE__INST1_SEG2                        0
#define GC_BASE__INST1_SEG3                        0
#define GC_BASE__INST1_SEG4                        0
#define GC_BASE__INST1_SEG5                        0

#define GC_BASE__INST2_SEG0                        0
#define GC_BASE__INST2_SEG1                        0
#define GC_BASE__INST2_SEG2                        0
#define GC_BASE__INST2_SEG3                        0
#define GC_BASE__INST2_SEG4                        0
#define GC_BASE__INST2_SEG5                        0

#define GC_BASE__INST3_SEG0                        0
#define GC_BASE__INST3_SEG1                        0
#define GC_BASE__INST3_SEG2                        0
#define GC_BASE__INST3_SEG3                        0
#define GC_BASE__INST3_SEG4                        0
#define GC_BASE__INST3_SEG5                        0

#define GC_BASE__INST4_SEG0                        0
#define GC_BASE__INST4_SEG1                        0
#define GC_BASE__INST4_SEG2                        0
#define GC_BASE__INST4_SEG3                        0
#define GC_BASE__INST4_SEG4                        0
#define GC_BASE__INST4_SEG5                        0

#define GC_BASE__INST5_SEG0                        0
#define GC_BASE__INST5_SEG1                        0
#define GC_BASE__INST5_SEG2                        0
#define GC_BASE__INST5_SEG3                        0
#define GC_BASE__INST5_SEG4                        0
#define GC_BASE__INST5_SEG5                        0

#define GC_BASE__INST6_SEG0                        0
#define GC_BASE__INST6_SEG1                        0
#define GC_BASE__INST6_SEG2                        0
#define GC_BASE__INST6_SEG3                        0
#define GC_BASE__INST6_SEG4                        0
#define GC_BASE__INST6_SEG5                        0

#define HDP_BASE__INST0_SEG0                       0x00000F20
#define HDP_BASE__INST0_SEG1                       0x0240A400
#define HDP_BASE__INST0_SEG2                       0
#define HDP_BASE__INST0_SEG3                       0
#define HDP_BASE__INST0_SEG4                       0
#define HDP_BASE__INST0_SEG5                       0

#define HDP_BASE__INST1_SEG0                       0
#define HDP_BASE__INST1_SEG1                       0
#define HDP_BASE__INST1_SEG2                       0
#define HDP_BASE__INST1_SEG3                       0
#define HDP_BASE__INST1_SEG4                       0
#define HDP_BASE__INST1_SEG5                       0

#define HDP_BASE__INST2_SEG0                       0
#define HDP_BASE__INST2_SEG1                       0
#define HDP_BASE__INST2_SEG2                       0
#define HDP_BASE__INST2_SEG3                       0
#define HDP_BASE__INST2_SEG4                       0
#define HDP_BASE__INST2_SEG5                       0

#define HDP_BASE__INST3_SEG0                       0
#define HDP_BASE__INST3_SEG1                       0
#define HDP_BASE__INST3_SEG2                       0
#define HDP_BASE__INST3_SEG3                       0
#define HDP_BASE__INST3_SEG4                       0
#define HDP_BASE__INST3_SEG5                       0

#define HDP_BASE__INST4_SEG0                       0
#define HDP_BASE__INST4_SEG1                       0
#define HDP_BASE__INST4_SEG2                       0
#define HDP_BASE__INST4_SEG3                       0
#define HDP_BASE__INST4_SEG4                       0
#define HDP_BASE__INST4_SEG5                       0

#define HDP_BASE__INST5_SEG0                       0
#define HDP_BASE__INST5_SEG1                       0
#define HDP_BASE__INST5_SEG2                       0
#define HDP_BASE__INST5_SEG3                       0
#define HDP_BASE__INST5_SEG4                       0
#define HDP_BASE__INST5_SEG5                       0

#define HDP_BASE__INST6_SEG0                       0
#define HDP_BASE__INST6_SEG1                       0
#define HDP_BASE__INST6_SEG2                       0
#define HDP_BASE__INST6_SEG3                       0
#define HDP_BASE__INST6_SEG4                       0
#define HDP_BASE__INST6_SEG5                       0

#define IOAGR0_BASE__INST0_SEG0                    0x02419000
#define IOAGR0_BASE__INST0_SEG1                    0x056C0000
#define IOAGR0_BASE__INST0_SEG2                    0
#define IOAGR0_BASE__INST0_SEG3                    0
#define IOAGR0_BASE__INST0_SEG4                    0
#define IOAGR0_BASE__INST0_SEG5                    0

#define IOAGR0_BASE__INST1_SEG0                    0
#define IOAGR0_BASE__INST1_SEG1                    0
#define IOAGR0_BASE__INST1_SEG2                    0
#define IOAGR0_BASE__INST1_SEG3                    0
#define IOAGR0_BASE__INST1_SEG4                    0
#define IOAGR0_BASE__INST1_SEG5                    0

#define IOAGR0_BASE__INST2_SEG0                    0
#define IOAGR0_BASE__INST2_SEG1                    0
#define IOAGR0_BASE__INST2_SEG2                    0
#define IOAGR0_BASE__INST2_SEG3                    0
#define IOAGR0_BASE__INST2_SEG4                    0
#define IOAGR0_BASE__INST2_SEG5                    0

#define IOAGR0_BASE__INST3_SEG0                    0
#define IOAGR0_BASE__INST3_SEG1                    0
#define IOAGR0_BASE__INST3_SEG2                    0
#define IOAGR0_BASE__INST3_SEG3                    0
#define IOAGR0_BASE__INST3_SEG4                    0
#define IOAGR0_BASE__INST3_SEG5                    0

#define IOAGR0_BASE__INST4_SEG0                    0
#define IOAGR0_BASE__INST4_SEG1                    0
#define IOAGR0_BASE__INST4_SEG2                    0
#define IOAGR0_BASE__INST4_SEG3                    0
#define IOAGR0_BASE__INST4_SEG4                    0
#define IOAGR0_BASE__INST4_SEG5                    0

#define IOAGR0_BASE__INST5_SEG0                    0
#define IOAGR0_BASE__INST5_SEG1                    0
#define IOAGR0_BASE__INST5_SEG2                    0
#define IOAGR0_BASE__INST5_SEG3                    0
#define IOAGR0_BASE__INST5_SEG4                    0
#define IOAGR0_BASE__INST5_SEG5                    0

#define IOAGR0_BASE__INST6_SEG0                    0
#define IOAGR0_BASE__INST6_SEG1                    0
#define IOAGR0_BASE__INST6_SEG2                    0
#define IOAGR0_BASE__INST6_SEG3                    0
#define IOAGR0_BASE__INST6_SEG4                    0
#define IOAGR0_BASE__INST6_SEG5                    0

#define IOAPIC0_BASE__INST0_SEG0                   0x00A00000
#define IOAPIC0_BASE__INST0_SEG1                   0x0241F000
#define IOAPIC0_BASE__INST0_SEG2                   0x050C0000
#define IOAPIC0_BASE__INST0_SEG3                   0
#define IOAPIC0_BASE__INST0_SEG4                   0
#define IOAPIC0_BASE__INST0_SEG5                   0

#define IOAPIC0_BASE__INST1_SEG0                   0
#define IOAPIC0_BASE__INST1_SEG1                   0
#define IOAPIC0_BASE__INST1_SEG2                   0
#define IOAPIC0_BASE__INST1_SEG3                   0
#define IOAPIC0_BASE__INST1_SEG4                   0
#define IOAPIC0_BASE__INST1_SEG5                   0

#define IOAPIC0_BASE__INST2_SEG0                   0
#define IOAPIC0_BASE__INST2_SEG1                   0
#define IOAPIC0_BASE__INST2_SEG2                   0
#define IOAPIC0_BASE__INST2_SEG3                   0
#define IOAPIC0_BASE__INST2_SEG4                   0
#define IOAPIC0_BASE__INST2_SEG5                   0

#define IOAPIC0_BASE__INST3_SEG0                   0
#define IOAPIC0_BASE__INST3_SEG1                   0
#define IOAPIC0_BASE__INST3_SEG2                   0
#define IOAPIC0_BASE__INST3_SEG3                   0
#define IOAPIC0_BASE__INST3_SEG4                   0
#define IOAPIC0_BASE__INST3_SEG5                   0

#define IOAPIC0_BASE__INST4_SEG0                   0
#define IOAPIC0_BASE__INST4_SEG1                   0
#define IOAPIC0_BASE__INST4_SEG2                   0
#define IOAPIC0_BASE__INST4_SEG3                   0
#define IOAPIC0_BASE__INST4_SEG4                   0
#define IOAPIC0_BASE__INST4_SEG5                   0

#define IOAPIC0_BASE__INST5_SEG0                   0
#define IOAPIC0_BASE__INST5_SEG1                   0
#define IOAPIC0_BASE__INST5_SEG2                   0
#define IOAPIC0_BASE__INST5_SEG3                   0
#define IOAPIC0_BASE__INST5_SEG4                   0
#define IOAPIC0_BASE__INST5_SEG5                   0

#define IOAPIC0_BASE__INST6_SEG0                   0
#define IOAPIC0_BASE__INST6_SEG1                   0
#define IOAPIC0_BASE__INST6_SEG2                   0
#define IOAPIC0_BASE__INST6_SEG3                   0
#define IOAPIC0_BASE__INST6_SEG4                   0
#define IOAPIC0_BASE__INST6_SEG5                   0

#define IOHC0_BASE__INST0_SEG0                     0x00010000
#define IOHC0_BASE__INST0_SEG1                     0x02406000
#define IOHC0_BASE__INST0_SEG2                     0x04EC0000
#define IOHC0_BASE__INST0_SEG3                     0
#define IOHC0_BASE__INST0_SEG4                     0
#define IOHC0_BASE__INST0_SEG5                     0

#define IOHC0_BASE__INST1_SEG0                     0
#define IOHC0_BASE__INST1_SEG1                     0
#define IOHC0_BASE__INST1_SEG2                     0
#define IOHC0_BASE__INST1_SEG3                     0
#define IOHC0_BASE__INST1_SEG4                     0
#define IOHC0_BASE__INST1_SEG5                     0

#define IOHC0_BASE__INST2_SEG0                     0
#define IOHC0_BASE__INST2_SEG1                     0
#define IOHC0_BASE__INST2_SEG2                     0
#define IOHC0_BASE__INST2_SEG3                     0
#define IOHC0_BASE__INST2_SEG4                     0
#define IOHC0_BASE__INST2_SEG5                     0

#define IOHC0_BASE__INST3_SEG0                     0
#define IOHC0_BASE__INST3_SEG1                     0
#define IOHC0_BASE__INST3_SEG2                     0
#define IOHC0_BASE__INST3_SEG3                     0
#define IOHC0_BASE__INST3_SEG4                     0
#define IOHC0_BASE__INST3_SEG5                     0

#define IOHC0_BASE__INST4_SEG0                     0
#define IOHC0_BASE__INST4_SEG1                     0
#define IOHC0_BASE__INST4_SEG2                     0
#define IOHC0_BASE__INST4_SEG3                     0
#define IOHC0_BASE__INST4_SEG4                     0
#define IOHC0_BASE__INST4_SEG5                     0

#define IOHC0_BASE__INST5_SEG0                     0
#define IOHC0_BASE__INST5_SEG1                     0
#define IOHC0_BASE__INST5_SEG2                     0
#define IOHC0_BASE__INST5_SEG3                     0
#define IOHC0_BASE__INST5_SEG4                     0
#define IOHC0_BASE__INST5_SEG5                     0

#define IOHC0_BASE__INST6_SEG0                     0
#define IOHC0_BASE__INST6_SEG1                     0
#define IOHC0_BASE__INST6_SEG2                     0
#define IOHC0_BASE__INST6_SEG3                     0
#define IOHC0_BASE__INST6_SEG4                     0
#define IOHC0_BASE__INST6_SEG5                     0

#define L1IMUIOAGR0_BASE__INST0_SEG0               0x0240CC00
#define L1IMUIOAGR0_BASE__INST0_SEG1               0x05200000
#define L1IMUIOAGR0_BASE__INST0_SEG2               0
#define L1IMUIOAGR0_BASE__INST0_SEG3               0
#define L1IMUIOAGR0_BASE__INST0_SEG4               0
#define L1IMUIOAGR0_BASE__INST0_SEG5               0

#define L1IMUIOAGR0_BASE__INST1_SEG0               0
#define L1IMUIOAGR0_BASE__INST1_SEG1               0
#define L1IMUIOAGR0_BASE__INST1_SEG2               0
#define L1IMUIOAGR0_BASE__INST1_SEG3               0
#define L1IMUIOAGR0_BASE__INST1_SEG4               0
#define L1IMUIOAGR0_BASE__INST1_SEG5               0

#define L1IMUIOAGR0_BASE__INST2_SEG0               0
#define L1IMUIOAGR0_BASE__INST2_SEG1               0
#define L1IMUIOAGR0_BASE__INST2_SEG2               0
#define L1IMUIOAGR0_BASE__INST2_SEG3               0
#define L1IMUIOAGR0_BASE__INST2_SEG4               0
#define L1IMUIOAGR0_BASE__INST2_SEG5               0

#define L1IMUIOAGR0_BASE__INST3_SEG0               0
#define L1IMUIOAGR0_BASE__INST3_SEG1               0
#define L1IMUIOAGR0_BASE__INST3_SEG2               0
#define L1IMUIOAGR0_BASE__INST3_SEG3               0
#define L1IMUIOAGR0_BASE__INST3_SEG4               0
#define L1IMUIOAGR0_BASE__INST3_SEG5               0

#define L1IMUIOAGR0_BASE__INST4_SEG0               0
#define L1IMUIOAGR0_BASE__INST4_SEG1               0
#define L1IMUIOAGR0_BASE__INST4_SEG2               0
#define L1IMUIOAGR0_BASE__INST4_SEG3               0
#define L1IMUIOAGR0_BASE__INST4_SEG4               0
#define L1IMUIOAGR0_BASE__INST4_SEG5               0

#define L1IMUIOAGR0_BASE__INST5_SEG0               0
#define L1IMUIOAGR0_BASE__INST5_SEG1               0
#define L1IMUIOAGR0_BASE__INST5_SEG2               0
#define L1IMUIOAGR0_BASE__INST5_SEG3               0
#define L1IMUIOAGR0_BASE__INST5_SEG4               0
#define L1IMUIOAGR0_BASE__INST5_SEG5               0

#define L1IMUIOAGR0_BASE__INST6_SEG0               0
#define L1IMUIOAGR0_BASE__INST6_SEG1               0
#define L1IMUIOAGR0_BASE__INST6_SEG2               0
#define L1IMUIOAGR0_BASE__INST6_SEG3               0
#define L1IMUIOAGR0_BASE__INST6_SEG4               0
#define L1IMUIOAGR0_BASE__INST6_SEG5               0

#define L1IMUPCIE0_BASE__INST0_SEG0                0x0240C800
#define L1IMUPCIE0_BASE__INST0_SEG1                0x051C0000
#define L1IMUPCIE0_BASE__INST0_SEG2                0
#define L1IMUPCIE0_BASE__INST0_SEG3                0
#define L1IMUPCIE0_BASE__INST0_SEG4                0
#define L1IMUPCIE0_BASE__INST0_SEG5                0

#define L1IMUPCIE0_BASE__INST1_SEG0                0
#define L1IMUPCIE0_BASE__INST1_SEG1                0
#define L1IMUPCIE0_BASE__INST1_SEG2                0
#define L1IMUPCIE0_BASE__INST1_SEG3                0
#define L1IMUPCIE0_BASE__INST1_SEG4                0
#define L1IMUPCIE0_BASE__INST1_SEG5                0

#define L1IMUPCIE0_BASE__INST2_SEG0                0
#define L1IMUPCIE0_BASE__INST2_SEG1                0
#define L1IMUPCIE0_BASE__INST2_SEG2                0
#define L1IMUPCIE0_BASE__INST2_SEG3                0
#define L1IMUPCIE0_BASE__INST2_SEG4                0
#define L1IMUPCIE0_BASE__INST2_SEG5                0

#define L1IMUPCIE0_BASE__INST3_SEG0                0
#define L1IMUPCIE0_BASE__INST3_SEG1                0
#define L1IMUPCIE0_BASE__INST3_SEG2                0
#define L1IMUPCIE0_BASE__INST3_SEG3                0
#define L1IMUPCIE0_BASE__INST3_SEG4                0
#define L1IMUPCIE0_BASE__INST3_SEG5                0

#define L1IMUPCIE0_BASE__INST4_SEG0                0
#define L1IMUPCIE0_BASE__INST4_SEG1                0
#define L1IMUPCIE0_BASE__INST4_SEG2                0
#define L1IMUPCIE0_BASE__INST4_SEG3                0
#define L1IMUPCIE0_BASE__INST4_SEG4                0
#define L1IMUPCIE0_BASE__INST4_SEG5                0

#define L1IMUPCIE0_BASE__INST5_SEG0                0
#define L1IMUPCIE0_BASE__INST5_SEG1                0
#define L1IMUPCIE0_BASE__INST5_SEG2                0
#define L1IMUPCIE0_BASE__INST5_SEG3                0
#define L1IMUPCIE0_BASE__INST5_SEG4                0
#define L1IMUPCIE0_BASE__INST5_SEG5                0

#define L1IMUPCIE0_BASE__INST6_SEG0                0
#define L1IMUPCIE0_BASE__INST6_SEG1                0
#define L1IMUPCIE0_BASE__INST6_SEG2                0
#define L1IMUPCIE0_BASE__INST6_SEG3                0
#define L1IMUPCIE0_BASE__INST6_SEG4                0
#define L1IMUPCIE0_BASE__INST6_SEG5                0

#define L2IMU0_BASE__INST0_SEG0                    0x00007DC0
#define L2IMU0_BASE__INST0_SEG1                    0x00900000
#define L2IMU0_BASE__INST0_SEG2                    0x02407000
#define L2IMU0_BASE__INST0_SEG3                    0x04FC0000
#define L2IMU0_BASE__INST0_SEG4                    0x055C0000
#define L2IMU0_BASE__INST0_SEG5                    0

#define L2IMU0_BASE__INST1_SEG0                    0
#define L2IMU0_BASE__INST1_SEG1                    0
#define L2IMU0_BASE__INST1_SEG2                    0
#define L2IMU0_BASE__INST1_SEG3                    0
#define L2IMU0_BASE__INST1_SEG4                    0
#define L2IMU0_BASE__INST1_SEG5                    0

#define L2IMU0_BASE__INST2_SEG0                    0
#define L2IMU0_BASE__INST2_SEG1                    0
#define L2IMU0_BASE__INST2_SEG2                    0
#define L2IMU0_BASE__INST2_SEG3                    0
#define L2IMU0_BASE__INST2_SEG4                    0
#define L2IMU0_BASE__INST2_SEG5                    0

#define L2IMU0_BASE__INST3_SEG0                    0
#define L2IMU0_BASE__INST3_SEG1                    0
#define L2IMU0_BASE__INST3_SEG2                    0
#define L2IMU0_BASE__INST3_SEG3                    0
#define L2IMU0_BASE__INST3_SEG4                    0
#define L2IMU0_BASE__INST3_SEG5                    0

#define L2IMU0_BASE__INST4_SEG0                    0
#define L2IMU0_BASE__INST4_SEG1                    0
#define L2IMU0_BASE__INST4_SEG2                    0
#define L2IMU0_BASE__INST4_SEG3                    0
#define L2IMU0_BASE__INST4_SEG4                    0
#define L2IMU0_BASE__INST4_SEG5                    0

#define L2IMU0_BASE__INST5_SEG0                    0
#define L2IMU0_BASE__INST5_SEG1                    0
#define L2IMU0_BASE__INST5_SEG2                    0
#define L2IMU0_BASE__INST5_SEG3                    0
#define L2IMU0_BASE__INST5_SEG4                    0
#define L2IMU0_BASE__INST5_SEG5                    0

#define L2IMU0_BASE__INST6_SEG0                    0
#define L2IMU0_BASE__INST6_SEG1                    0
#define L2IMU0_BASE__INST6_SEG2                    0
#define L2IMU0_BASE__INST6_SEG3                    0
#define L2IMU0_BASE__INST6_SEG4                    0
#define L2IMU0_BASE__INST6_SEG5                    0

#define MMHUB_BASE__INST0_SEG0                     0x0001A000
#define MMHUB_BASE__INST0_SEG1                     0x02408800
#define MMHUB_BASE__INST0_SEG2                     0
#define MMHUB_BASE__INST0_SEG3                     0
#define MMHUB_BASE__INST0_SEG4                     0
#define MMHUB_BASE__INST0_SEG5                     0

#define MMHUB_BASE__INST1_SEG0                     0
#define MMHUB_BASE__INST1_SEG1                     0
#define MMHUB_BASE__INST1_SEG2                     0
#define MMHUB_BASE__INST1_SEG3                     0
#define MMHUB_BASE__INST1_SEG4                     0
#define MMHUB_BASE__INST1_SEG5                     0

#define MMHUB_BASE__INST2_SEG0                     0
#define MMHUB_BASE__INST2_SEG1                     0
#define MMHUB_BASE__INST2_SEG2                     0
#define MMHUB_BASE__INST2_SEG3                     0
#define MMHUB_BASE__INST2_SEG4                     0
#define MMHUB_BASE__INST2_SEG5                     0

#define MMHUB_BASE__INST3_SEG0                     0
#define MMHUB_BASE__INST3_SEG1                     0
#define MMHUB_BASE__INST3_SEG2                     0
#define MMHUB_BASE__INST3_SEG3                     0
#define MMHUB_BASE__INST3_SEG4                     0
#define MMHUB_BASE__INST3_SEG5                     0

#define MMHUB_BASE__INST4_SEG0                     0
#define MMHUB_BASE__INST4_SEG1                     0
#define MMHUB_BASE__INST4_SEG2                     0
#define MMHUB_BASE__INST4_SEG3                     0
#define MMHUB_BASE__INST4_SEG4                     0
#define MMHUB_BASE__INST4_SEG5                     0

#define MMHUB_BASE__INST5_SEG0                     0
#define MMHUB_BASE__INST5_SEG1                     0
#define MMHUB_BASE__INST5_SEG2                     0
#define MMHUB_BASE__INST5_SEG3                     0
#define MMHUB_BASE__INST5_SEG4                     0
#define MMHUB_BASE__INST5_SEG5                     0

#define MMHUB_BASE__INST6_SEG0                     0
#define MMHUB_BASE__INST6_SEG1                     0
#define MMHUB_BASE__INST6_SEG2                     0
#define MMHUB_BASE__INST6_SEG3                     0
#define MMHUB_BASE__INST6_SEG4                     0
#define MMHUB_BASE__INST6_SEG5                     0

#define MP0_BASE__INST0_SEG0                       0x00016000
#define MP0_BASE__INST0_SEG1                       0x00DC0000
#define MP0_BASE__INST0_SEG2                       0x00E00000
#define MP0_BASE__INST0_SEG3                       0x00E40000
#define MP0_BASE__INST0_SEG4                       0x0243FC00
#define MP0_BASE__INST0_SEG5                       0

#define MP0_BASE__INST1_SEG0                       0
#define MP0_BASE__INST1_SEG1                       0
#define MP0_BASE__INST1_SEG2                       0
#define MP0_BASE__INST1_SEG3                       0
#define MP0_BASE__INST1_SEG4                       0
#define MP0_BASE__INST1_SEG5                       0

#define MP0_BASE__INST2_SEG0                       0
#define MP0_BASE__INST2_SEG1                       0
#define MP0_BASE__INST2_SEG2                       0
#define MP0_BASE__INST2_SEG3                       0
#define MP0_BASE__INST2_SEG4                       0
#define MP0_BASE__INST2_SEG5                       0

#define MP0_BASE__INST3_SEG0                       0
#define MP0_BASE__INST3_SEG1                       0
#define MP0_BASE__INST3_SEG2                       0
#define MP0_BASE__INST3_SEG3                       0
#define MP0_BASE__INST3_SEG4                       0
#define MP0_BASE__INST3_SEG5                       0

#define MP0_BASE__INST4_SEG0                       0
#define MP0_BASE__INST4_SEG1                       0
#define MP0_BASE__INST4_SEG2                       0
#define MP0_BASE__INST4_SEG3                       0
#define MP0_BASE__INST4_SEG4                       0
#define MP0_BASE__INST4_SEG5                       0

#define MP0_BASE__INST5_SEG0                       0
#define MP0_BASE__INST5_SEG1                       0
#define MP0_BASE__INST5_SEG2                       0
#define MP0_BASE__INST5_SEG3                       0
#define MP0_BASE__INST5_SEG4                       0
#define MP0_BASE__INST5_SEG5                       0

#define MP0_BASE__INST6_SEG0                       0
#define MP0_BASE__INST6_SEG1                       0
#define MP0_BASE__INST6_SEG2                       0
#define MP0_BASE__INST6_SEG3                       0
#define MP0_BASE__INST6_SEG4                       0
#define MP0_BASE__INST6_SEG5                       0

#define MP1_BASE__INST0_SEG0                       0x00016000
#define MP1_BASE__INST0_SEG1                       0x00DC0000
#define MP1_BASE__INST0_SEG2                       0x00E00000
#define MP1_BASE__INST0_SEG3                       0x00E40000
#define MP1_BASE__INST0_SEG4                       0x0243FC00
#define MP1_BASE__INST0_SEG5                       0

#define MP1_BASE__INST1_SEG0                       0
#define MP1_BASE__INST1_SEG1                       0
#define MP1_BASE__INST1_SEG2                       0
#define MP1_BASE__INST1_SEG3                       0
#define MP1_BASE__INST1_SEG4                       0
#define MP1_BASE__INST1_SEG5                       0

#define MP1_BASE__INST2_SEG0                       0
#define MP1_BASE__INST2_SEG1                       0
#define MP1_BASE__INST2_SEG2                       0
#define MP1_BASE__INST2_SEG3                       0
#define MP1_BASE__INST2_SEG4                       0
#define MP1_BASE__INST2_SEG5                       0

#define MP1_BASE__INST3_SEG0                       0
#define MP1_BASE__INST3_SEG1                       0
#define MP1_BASE__INST3_SEG2                       0
#define MP1_BASE__INST3_SEG3                       0
#define MP1_BASE__INST3_SEG4                       0
#define MP1_BASE__INST3_SEG5                       0

#define MP1_BASE__INST4_SEG0                       0
#define MP1_BASE__INST4_SEG1                       0
#define MP1_BASE__INST4_SEG2                       0
#define MP1_BASE__INST4_SEG3                       0
#define MP1_BASE__INST4_SEG4                       0
#define MP1_BASE__INST4_SEG5                       0

#define MP1_BASE__INST5_SEG0                       0
#define MP1_BASE__INST5_SEG1                       0
#define MP1_BASE__INST5_SEG2                       0
#define MP1_BASE__INST5_SEG3                       0
#define MP1_BASE__INST5_SEG4                       0
#define MP1_BASE__INST5_SEG5                       0

#define MP1_BASE__INST6_SEG0                       0
#define MP1_BASE__INST6_SEG1                       0
#define MP1_BASE__INST6_SEG2                       0
#define MP1_BASE__INST6_SEG3                       0
#define MP1_BASE__INST6_SEG4                       0
#define MP1_BASE__INST6_SEG5                       0

#define NBIO_BASE__INST0_SEG0                      0x00000000
#define NBIO_BASE__INST0_SEG1                      0x00000014
#define NBIO_BASE__INST0_SEG2                      0x00000D20
#define NBIO_BASE__INST0_SEG3                      0x00010400
#define NBIO_BASE__INST0_SEG4                      0x0241B000
#define NBIO_BASE__INST0_SEG5                      0x04040000

#define NBIO_BASE__INST1_SEG0                      0
#define NBIO_BASE__INST1_SEG1                      0
#define NBIO_BASE__INST1_SEG2                      0
#define NBIO_BASE__INST1_SEG3                      0
#define NBIO_BASE__INST1_SEG4                      0
#define NBIO_BASE__INST1_SEG5                      0

#define NBIO_BASE__INST2_SEG0                      0
#define NBIO_BASE__INST2_SEG1                      0
#define NBIO_BASE__INST2_SEG2                      0
#define NBIO_BASE__INST2_SEG3                      0
#define NBIO_BASE__INST2_SEG4                      0
#define NBIO_BASE__INST2_SEG5                      0

#define NBIO_BASE__INST3_SEG0                      0
#define NBIO_BASE__INST3_SEG1                      0
#define NBIO_BASE__INST3_SEG2                      0
#define NBIO_BASE__INST3_SEG3                      0
#define NBIO_BASE__INST3_SEG4                      0
#define NBIO_BASE__INST3_SEG5                      0

#define NBIO_BASE__INST4_SEG0                      0
#define NBIO_BASE__INST4_SEG1                      0
#define NBIO_BASE__INST4_SEG2                      0
#define NBIO_BASE__INST4_SEG3                      0
#define NBIO_BASE__INST4_SEG4                      0
#define NBIO_BASE__INST4_SEG5                      0

#define NBIO_BASE__INST5_SEG0                      0
#define NBIO_BASE__INST5_SEG1                      0
#define NBIO_BASE__INST5_SEG2                      0
#define NBIO_BASE__INST5_SEG3                      0
#define NBIO_BASE__INST5_SEG4                      0
#define NBIO_BASE__INST5_SEG5                      0

#define NBIO_BASE__INST6_SEG0                      0
#define NBIO_BASE__INST6_SEG1                      0
#define NBIO_BASE__INST6_SEG2                      0
#define NBIO_BASE__INST6_SEG3                      0
#define NBIO_BASE__INST6_SEG4                      0
#define NBIO_BASE__INST6_SEG5                      0

#define OSSSYS_BASE__INST0_SEG0                    0x000010A0
#define OSSSYS_BASE__INST0_SEG1                    0x0240A000
#define OSSSYS_BASE__INST0_SEG2                    0
#define OSSSYS_BASE__INST0_SEG3                    0
#define OSSSYS_BASE__INST0_SEG4                    0
#define OSSSYS_BASE__INST0_SEG5                    0

#define OSSSYS_BASE__INST1_SEG0                    0
#define OSSSYS_BASE__INST1_SEG1                    0
#define OSSSYS_BASE__INST1_SEG2                    0
#define OSSSYS_BASE__INST1_SEG3                    0
#define OSSSYS_BASE__INST1_SEG4                    0
#define OSSSYS_BASE__INST1_SEG5                    0

#define OSSSYS_BASE__INST2_SEG0                    0
#define OSSSYS_BASE__INST2_SEG1                    0
#define OSSSYS_BASE__INST2_SEG2                    0
#define OSSSYS_BASE__INST2_SEG3                    0
#define OSSSYS_BASE__INST2_SEG4                    0
#define OSSSYS_BASE__INST2_SEG5                    0

#define OSSSYS_BASE__INST3_SEG0                    0
#define OSSSYS_BASE__INST3_SEG1                    0
#define OSSSYS_BASE__INST3_SEG2                    0
#define OSSSYS_BASE__INST3_SEG3                    0
#define OSSSYS_BASE__INST3_SEG4                    0
#define OSSSYS_BASE__INST3_SEG5                    0

#define OSSSYS_BASE__INST4_SEG0                    0
#define OSSSYS_BASE__INST4_SEG1                    0
#define OSSSYS_BASE__INST4_SEG2                    0
#define OSSSYS_BASE__INST4_SEG3                    0
#define OSSSYS_BASE__INST4_SEG4                    0
#define OSSSYS_BASE__INST4_SEG5                    0

#define OSSSYS_BASE__INST5_SEG0                    0
#define OSSSYS_BASE__INST5_SEG1                    0
#define OSSSYS_BASE__INST5_SEG2                    0
#define OSSSYS_BASE__INST5_SEG3                    0
#define OSSSYS_BASE__INST5_SEG4                    0
#define OSSSYS_BASE__INST5_SEG5                    0

#define OSSSYS_BASE__INST6_SEG0                    0
#define OSSSYS_BASE__INST6_SEG1                    0
#define OSSSYS_BASE__INST6_SEG2                    0
#define OSSSYS_BASE__INST6_SEG3                    0
#define OSSSYS_BASE__INST6_SEG4                    0
#define OSSSYS_BASE__INST6_SEG5                    0

#define PCIE0_BASE__INST0_SEG0                     0x02411800
#define PCIE0_BASE__INST0_SEG1                     0x04440000
#define PCIE0_BASE__INST0_SEG2                     0
#define PCIE0_BASE__INST0_SEG3                     0
#define PCIE0_BASE__INST0_SEG4                     0
#define PCIE0_BASE__INST0_SEG5                     0

#define PCIE0_BASE__INST1_SEG0                     0
#define PCIE0_BASE__INST1_SEG1                     0
#define PCIE0_BASE__INST1_SEG2                     0
#define PCIE0_BASE__INST1_SEG3                     0
#define PCIE0_BASE__INST1_SEG4                     0
#define PCIE0_BASE__INST1_SEG5                     0

#define PCIE0_BASE__INST2_SEG0                     0
#define PCIE0_BASE__INST2_SEG1                     0
#define PCIE0_BASE__INST2_SEG2                     0
#define PCIE0_BASE__INST2_SEG3                     0
#define PCIE0_BASE__INST2_SEG4                     0
#define PCIE0_BASE__INST2_SEG5                     0

#define PCIE0_BASE__INST3_SEG0                     0
#define PCIE0_BASE__INST3_SEG1                     0
#define PCIE0_BASE__INST3_SEG2                     0
#define PCIE0_BASE__INST3_SEG3                     0
#define PCIE0_BASE__INST3_SEG4                     0
#define PCIE0_BASE__INST3_SEG5                     0

#define PCIE0_BASE__INST4_SEG0                     0
#define PCIE0_BASE__INST4_SEG1                     0
#define PCIE0_BASE__INST4_SEG2                     0
#define PCIE0_BASE__INST4_SEG3                     0
#define PCIE0_BASE__INST4_SEG4                     0
#define PCIE0_BASE__INST4_SEG5                     0

#define PCIE0_BASE__INST5_SEG0                     0
#define PCIE0_BASE__INST5_SEG1                     0
#define PCIE0_BASE__INST5_SEG2                     0
#define PCIE0_BASE__INST5_SEG3                     0
#define PCIE0_BASE__INST5_SEG4                     0
#define PCIE0_BASE__INST5_SEG5                     0

#define PCIE0_BASE__INST6_SEG0                     0
#define PCIE0_BASE__INST6_SEG1                     0
#define PCIE0_BASE__INST6_SEG2                     0
#define PCIE0_BASE__INST6_SEG3                     0
#define PCIE0_BASE__INST6_SEG4                     0
#define PCIE0_BASE__INST6_SEG5                     0

#define SDMA0_BASE__INST0_SEG0                     0x00001260
#define SDMA0_BASE__INST0_SEG1                     0x02445400
#define SDMA0_BASE__INST0_SEG2                     0
#define SDMA0_BASE__INST0_SEG3                     0
#define SDMA0_BASE__INST0_SEG4                     0
#define SDMA0_BASE__INST0_SEG5                     0

#define SDMA0_BASE__INST1_SEG0                     0
#define SDMA0_BASE__INST1_SEG1                     0
#define SDMA0_BASE__INST1_SEG2                     0
#define SDMA0_BASE__INST1_SEG3                     0
#define SDMA0_BASE__INST1_SEG4                     0
#define SDMA0_BASE__INST1_SEG5                     0

#define SDMA0_BASE__INST2_SEG0                     0
#define SDMA0_BASE__INST2_SEG1                     0
#define SDMA0_BASE__INST2_SEG2                     0
#define SDMA0_BASE__INST2_SEG3                     0
#define SDMA0_BASE__INST2_SEG4                     0
#define SDMA0_BASE__INST2_SEG5                     0

#define SDMA0_BASE__INST3_SEG0                     0
#define SDMA0_BASE__INST3_SEG1                     0
#define SDMA0_BASE__INST3_SEG2                     0
#define SDMA0_BASE__INST3_SEG3                     0
#define SDMA0_BASE__INST3_SEG4                     0
#define SDMA0_BASE__INST3_SEG5                     0

#define SDMA0_BASE__INST4_SEG0                     0
#define SDMA0_BASE__INST4_SEG1                     0
#define SDMA0_BASE__INST4_SEG2                     0
#define SDMA0_BASE__INST4_SEG3                     0
#define SDMA0_BASE__INST4_SEG4                     0
#define SDMA0_BASE__INST4_SEG5                     0

#define SDMA0_BASE__INST5_SEG0                     0
#define SDMA0_BASE__INST5_SEG1                     0
#define SDMA0_BASE__INST5_SEG2                     0
#define SDMA0_BASE__INST5_SEG3                     0
#define SDMA0_BASE__INST5_SEG4                     0
#define SDMA0_BASE__INST5_SEG5                     0

#define SDMA0_BASE__INST6_SEG0                     0
#define SDMA0_BASE__INST6_SEG1                     0
#define SDMA0_BASE__INST6_SEG2                     0
#define SDMA0_BASE__INST6_SEG3                     0
#define SDMA0_BASE__INST6_SEG4                     0
#define SDMA0_BASE__INST6_SEG5                     0

#define SDMA1_BASE__INST0_SEG0                     0x00001860
#define SDMA1_BASE__INST0_SEG1                     0x02445800
#define SDMA1_BASE__INST0_SEG2                     0
#define SDMA1_BASE__INST0_SEG3                     0
#define SDMA1_BASE__INST0_SEG4                     0
#define SDMA1_BASE__INST0_SEG5                     0

#define SDMA1_BASE__INST1_SEG0                     0x0001E000
#define SDMA1_BASE__INST1_SEG1                     0x02446400
#define SDMA1_BASE__INST1_SEG2                     0
#define SDMA1_BASE__INST1_SEG3                     0
#define SDMA1_BASE__INST1_SEG4                     0
#define SDMA1_BASE__INST1_SEG5                     0

#define SDMA1_BASE__INST2_SEG0                     0x0001E400
#define SDMA1_BASE__INST2_SEG1                     0x02446800
#define SDMA1_BASE__INST2_SEG2                     0
#define SDMA1_BASE__INST2_SEG3                     0
#define SDMA1_BASE__INST2_SEG4                     0
#define SDMA1_BASE__INST2_SEG5                     0

#define SDMA1_BASE__INST3_SEG0                     0x0001E800
#define SDMA1_BASE__INST3_SEG1                     0x02446C00
#define SDMA1_BASE__INST3_SEG2                     0
#define SDMA1_BASE__INST3_SEG3                     0
#define SDMA1_BASE__INST3_SEG4                     0
#define SDMA1_BASE__INST3_SEG5                     0

#define SDMA1_BASE__INST4_SEG0                     0
#define SDMA1_BASE__INST4_SEG1                     0
#define SDMA1_BASE__INST4_SEG2                     0
#define SDMA1_BASE__INST4_SEG3                     0
#define SDMA1_BASE__INST4_SEG4                     0
#define SDMA1_BASE__INST4_SEG5                     0

#define SDMA1_BASE__INST5_SEG0                     0
#define SDMA1_BASE__INST5_SEG1                     0
#define SDMA1_BASE__INST5_SEG2                     0
#define SDMA1_BASE__INST5_SEG3                     0
#define SDMA1_BASE__INST5_SEG4                     0
#define SDMA1_BASE__INST5_SEG5                     0

#define SDMA1_BASE__INST6_SEG0                     0
#define SDMA1_BASE__INST6_SEG1                     0
#define SDMA1_BASE__INST6_SEG2                     0
#define SDMA1_BASE__INST6_SEG3                     0
#define SDMA1_BASE__INST6_SEG4                     0
#define SDMA1_BASE__INST6_SEG5                     0

#define SMUIO_BASE__INST0_SEG0                     0x00016800
#define SMUIO_BASE__INST0_SEG1                     0x00016A00
#define SMUIO_BASE__INST0_SEG2                     0x02401000
#define SMUIO_BASE__INST0_SEG3                     0x03440000
#define SMUIO_BASE__INST0_SEG4                     0
#define SMUIO_BASE__INST0_SEG5                     0

#define SMUIO_BASE__INST1_SEG0                     0
#define SMUIO_BASE__INST1_SEG1                     0
#define SMUIO_BASE__INST1_SEG2                     0
#define SMUIO_BASE__INST1_SEG3                     0
#define SMUIO_BASE__INST1_SEG4                     0
#define SMUIO_BASE__INST1_SEG5                     0

#define SMUIO_BASE__INST2_SEG0                     0
#define SMUIO_BASE__INST2_SEG1                     0
#define SMUIO_BASE__INST2_SEG2                     0
#define SMUIO_BASE__INST2_SEG3                     0
#define SMUIO_BASE__INST2_SEG4                     0
#define SMUIO_BASE__INST2_SEG5                     0

#define SMUIO_BASE__INST3_SEG0                     0
#define SMUIO_BASE__INST3_SEG1                     0
#define SMUIO_BASE__INST3_SEG2                     0
#define SMUIO_BASE__INST3_SEG3                     0
#define SMUIO_BASE__INST3_SEG4                     0
#define SMUIO_BASE__INST3_SEG5                     0

#define SMUIO_BASE__INST4_SEG0                     0
#define SMUIO_BASE__INST4_SEG1                     0
#define SMUIO_BASE__INST4_SEG2                     0
#define SMUIO_BASE__INST4_SEG3                     0
#define SMUIO_BASE__INST4_SEG4                     0
#define SMUIO_BASE__INST4_SEG5                     0

#define SMUIO_BASE__INST5_SEG0                     0
#define SMUIO_BASE__INST5_SEG1                     0
#define SMUIO_BASE__INST5_SEG2                     0
#define SMUIO_BASE__INST5_SEG3                     0
#define SMUIO_BASE__INST5_SEG4                     0
#define SMUIO_BASE__INST5_SEG5                     0

#define SMUIO_BASE__INST6_SEG0                     0
#define SMUIO_BASE__INST6_SEG1                     0
#define SMUIO_BASE__INST6_SEG2                     0
#define SMUIO_BASE__INST6_SEG3                     0
#define SMUIO_BASE__INST6_SEG4                     0
#define SMUIO_BASE__INST6_SEG5                     0

#define THM_BASE__INST0_SEG0                       0x00016600
#define THM_BASE__INST0_SEG1                       0x02400C00
#define THM_BASE__INST0_SEG2                       0
#define THM_BASE__INST0_SEG3                       0
#define THM_BASE__INST0_SEG4                       0
#define THM_BASE__INST0_SEG5                       0

#define THM_BASE__INST1_SEG0                       0
#define THM_BASE__INST1_SEG1                       0
#define THM_BASE__INST1_SEG2                       0
#define THM_BASE__INST1_SEG3                       0
#define THM_BASE__INST1_SEG4                       0
#define THM_BASE__INST1_SEG5                       0

#define THM_BASE__INST2_SEG0                       0
#define THM_BASE__INST2_SEG1                       0
#define THM_BASE__INST2_SEG2                       0
#define THM_BASE__INST2_SEG3                       0
#define THM_BASE__INST2_SEG4                       0
#define THM_BASE__INST2_SEG5                       0

#define THM_BASE__INST3_SEG0                       0
#define THM_BASE__INST3_SEG1                       0
#define THM_BASE__INST3_SEG2                       0
#define THM_BASE__INST3_SEG3                       0
#define THM_BASE__INST3_SEG4                       0
#define THM_BASE__INST3_SEG5                       0

#define THM_BASE__INST4_SEG0                       0
#define THM_BASE__INST4_SEG1                       0
#define THM_BASE__INST4_SEG2                       0
#define THM_BASE__INST4_SEG3                       0
#define THM_BASE__INST4_SEG4                       0
#define THM_BASE__INST4_SEG5                       0

#define THM_BASE__INST5_SEG0                       0
#define THM_BASE__INST5_SEG1                       0
#define THM_BASE__INST5_SEG2                       0
#define THM_BASE__INST5_SEG3                       0
#define THM_BASE__INST5_SEG4                       0
#define THM_BASE__INST5_SEG5                       0

#define THM_BASE__INST6_SEG0                       0
#define THM_BASE__INST6_SEG1                       0
#define THM_BASE__INST6_SEG2                       0
#define THM_BASE__INST6_SEG3                       0
#define THM_BASE__INST6_SEG4                       0
#define THM_BASE__INST6_SEG5                       0

#define UMC_BASE__INST0_SEG0                       0x00014000
#define UMC_BASE__INST0_SEG1                       0x00054000
#define UMC_BASE__INST0_SEG2                       0x02425800
#define UMC_BASE__INST0_SEG3                       0
#define UMC_BASE__INST0_SEG4                       0
#define UMC_BASE__INST0_SEG5                       0

#define UMC_BASE__INST1_SEG0                       0x00094000
#define UMC_BASE__INST1_SEG1                       0x000D4000
#define UMC_BASE__INST1_SEG2                       0x02425C00
#define UMC_BASE__INST1_SEG3                       0
#define UMC_BASE__INST1_SEG4                       0
#define UMC_BASE__INST1_SEG5                       0

#define UMC_BASE__INST2_SEG0                       0x00114000
#define UMC_BASE__INST2_SEG1                       0x00154000
#define UMC_BASE__INST2_SEG2                       0x02426000
#define UMC_BASE__INST2_SEG3                       0
#define UMC_BASE__INST2_SEG4                       0
#define UMC_BASE__INST2_SEG5                       0

#define UMC_BASE__INST3_SEG0                       0x00194000
#define UMC_BASE__INST3_SEG1                       0x001D4000
#define UMC_BASE__INST3_SEG2                       0x02426400
#define UMC_BASE__INST3_SEG3                       0
#define UMC_BASE__INST3_SEG4                       0
#define UMC_BASE__INST3_SEG5                       0

#define UMC_BASE__INST4_SEG0                       0
#define UMC_BASE__INST4_SEG1                       0
#define UMC_BASE__INST4_SEG2                       0
#define UMC_BASE__INST4_SEG3                       0
#define UMC_BASE__INST4_SEG4                       0
#define UMC_BASE__INST4_SEG5                       0

#define UMC_BASE__INST5_SEG0                       0
#define UMC_BASE__INST5_SEG1                       0
#define UMC_BASE__INST5_SEG2                       0
#define UMC_BASE__INST5_SEG3                       0
#define UMC_BASE__INST5_SEG4                       0
#define UMC_BASE__INST5_SEG5                       0

#define UMC_BASE__INST6_SEG0                       0
#define UMC_BASE__INST6_SEG1                       0
#define UMC_BASE__INST6_SEG2                       0
#define UMC_BASE__INST6_SEG3                       0
#define UMC_BASE__INST6_SEG4                       0
#define UMC_BASE__INST6_SEG5                       0

#define VCN_BASE__INST0_SEG0                       0x00007800
#define VCN_BASE__INST0_SEG1                       0x00007E00
#define VCN_BASE__INST0_SEG2                       0x02403000
#define VCN_BASE__INST0_SEG3                       0
#define VCN_BASE__INST0_SEG4                       0
#define VCN_BASE__INST0_SEG5                       0

#define VCN_BASE__INST1_SEG0                       0x00007A00
#define VCN_BASE__INST1_SEG1                       0x00009000
#define VCN_BASE__INST1_SEG2                       0x02445000
#define VCN_BASE__INST1_SEG3                       0
#define VCN_BASE__INST1_SEG4                       0
#define VCN_BASE__INST1_SEG5                       0

#define VCN_BASE__INST2_SEG0                       0
#define VCN_BASE__INST2_SEG1                       0
#define VCN_BASE__INST2_SEG2                       0
#define VCN_BASE__INST2_SEG3                       0
#define VCN_BASE__INST2_SEG4                       0
#define VCN_BASE__INST2_SEG5                       0

#define VCN_BASE__INST3_SEG0                       0
#define VCN_BASE__INST3_SEG1                       0
#define VCN_BASE__INST3_SEG2                       0
#define VCN_BASE__INST3_SEG3                       0
#define VCN_BASE__INST3_SEG4                       0
#define VCN_BASE__INST3_SEG5                       0

#define VCN_BASE__INST4_SEG0                       0
#define VCN_BASE__INST4_SEG1                       0
#define VCN_BASE__INST4_SEG2                       0
#define VCN_BASE__INST4_SEG3                       0
#define VCN_BASE__INST4_SEG4                       0
#define VCN_BASE__INST4_SEG5                       0

#define VCN_BASE__INST5_SEG0                       0
#define VCN_BASE__INST5_SEG1                       0
#define VCN_BASE__INST5_SEG2                       0
#define VCN_BASE__INST5_SEG3                       0
#define VCN_BASE__INST5_SEG4                       0
#define VCN_BASE__INST5_SEG5                       0

#define VCN_BASE__INST6_SEG0                       0
#define VCN_BASE__INST6_SEG1                       0
#define VCN_BASE__INST6_SEG2                       0
#define VCN_BASE__INST6_SEG3                       0
#define VCN_BASE__INST6_SEG4                       0
#define VCN_BASE__INST6_SEG5                       0

#define WAFL0_BASE__INST0_SEG0                     0x02438000
#define WAFL0_BASE__INST0_SEG1                     0x04880000
#define WAFL0_BASE__INST0_SEG2                     0
#define WAFL0_BASE__INST0_SEG3                     0
#define WAFL0_BASE__INST0_SEG4                     0
#define WAFL0_BASE__INST0_SEG5                     0

#define WAFL0_BASE__INST1_SEG0                     0
#define WAFL0_BASE__INST1_SEG1                     0
#define WAFL0_BASE__INST1_SEG2                     0
#define WAFL0_BASE__INST1_SEG3                     0
#define WAFL0_BASE__INST1_SEG4                     0
#define WAFL0_BASE__INST1_SEG5                     0

#define WAFL0_BASE__INST2_SEG0                     0
#define WAFL0_BASE__INST2_SEG1                     0
#define WAFL0_BASE__INST2_SEG2                     0
#define WAFL0_BASE__INST2_SEG3                     0
#define WAFL0_BASE__INST2_SEG4                     0
#define WAFL0_BASE__INST2_SEG5                     0

#define WAFL0_BASE__INST3_SEG0                     0
#define WAFL0_BASE__INST3_SEG1                     0
#define WAFL0_BASE__INST3_SEG2                     0
#define WAFL0_BASE__INST3_SEG3                     0
#define WAFL0_BASE__INST3_SEG4                     0
#define WAFL0_BASE__INST3_SEG5                     0

#define WAFL0_BASE__INST4_SEG0                     0
#define WAFL0_BASE__INST4_SEG1                     0
#define WAFL0_BASE__INST4_SEG2                     0
#define WAFL0_BASE__INST4_SEG3                     0
#define WAFL0_BASE__INST4_SEG4                     0
#define WAFL0_BASE__INST4_SEG5                     0

#define WAFL0_BASE__INST5_SEG0                     0
#define WAFL0_BASE__INST5_SEG1                     0
#define WAFL0_BASE__INST5_SEG2                     0
#define WAFL0_BASE__INST5_SEG3                     0
#define WAFL0_BASE__INST5_SEG4                     0
#define WAFL0_BASE__INST5_SEG5                     0

#define WAFL0_BASE__INST6_SEG0                     0
#define WAFL0_BASE__INST6_SEG1                     0
#define WAFL0_BASE__INST6_SEG2                     0
#define WAFL0_BASE__INST6_SEG3                     0
#define WAFL0_BASE__INST6_SEG4                     0
#define WAFL0_BASE__INST6_SEG5                     0

#define WAFL1_BASE__INST0_SEG0                     0
#define WAFL1_BASE__INST0_SEG1                     0x01300000
#define WAFL1_BASE__INST0_SEG2                     0x02410800
#define WAFL1_BASE__INST0_SEG3                     0
#define WAFL1_BASE__INST0_SEG4                     0
#define WAFL1_BASE__INST0_SEG5                     0

#define WAFL1_BASE__INST1_SEG0                     0
#define WAFL1_BASE__INST1_SEG1                     0
#define WAFL1_BASE__INST1_SEG2                     0
#define WAFL1_BASE__INST1_SEG3                     0
#define WAFL1_BASE__INST1_SEG4                     0
#define WAFL1_BASE__INST1_SEG5                     0

#define WAFL1_BASE__INST2_SEG0                     0
#define WAFL1_BASE__INST2_SEG1                     0
#define WAFL1_BASE__INST2_SEG2                     0
#define WAFL1_BASE__INST2_SEG3                     0
#define WAFL1_BASE__INST2_SEG4                     0
#define WAFL1_BASE__INST2_SEG5                     0

#define WAFL1_BASE__INST3_SEG0                     0
#define WAFL1_BASE__INST3_SEG1                     0
#define WAFL1_BASE__INST3_SEG2                     0
#define WAFL1_BASE__INST3_SEG3                     0
#define WAFL1_BASE__INST3_SEG4                     0
#define WAFL1_BASE__INST3_SEG5                     0

#define WAFL1_BASE__INST4_SEG0                     0
#define WAFL1_BASE__INST4_SEG1                     0
#define WAFL1_BASE__INST4_SEG2                     0
#define WAFL1_BASE__INST4_SEG3                     0
#define WAFL1_BASE__INST4_SEG4                     0
#define WAFL1_BASE__INST4_SEG5                     0

#define WAFL1_BASE__INST5_SEG0                     0
#define WAFL1_BASE__INST5_SEG1                     0
#define WAFL1_BASE__INST5_SEG2                     0
#define WAFL1_BASE__INST5_SEG3                     0
#define WAFL1_BASE__INST5_SEG4                     0
#define WAFL1_BASE__INST5_SEG5                     0

#define WAFL1_BASE__INST6_SEG0                     0
#define WAFL1_BASE__INST6_SEG1                     0
#define WAFL1_BASE__INST6_SEG2                     0
#define WAFL1_BASE__INST6_SEG3                     0
#define WAFL1_BASE__INST6_SEG4                     0
#define WAFL1_BASE__INST6_SEG5                     0

#define XGMI0_BASE__INST0_SEG0                     0x02438C00
#define XGMI0_BASE__INST0_SEG1                     0x04680000
#define XGMI0_BASE__INST0_SEG2                     0x04940000
#define XGMI0_BASE__INST0_SEG3                     0
#define XGMI0_BASE__INST0_SEG4                     0
#define XGMI0_BASE__INST0_SEG5                     0

#define XGMI0_BASE__INST1_SEG0                     0
#define XGMI0_BASE__INST1_SEG1                     0
#define XGMI0_BASE__INST1_SEG2                     0
#define XGMI0_BASE__INST1_SEG3                     0
#define XGMI0_BASE__INST1_SEG4                     0
#define XGMI0_BASE__INST1_SEG5                     0

#define XGMI0_BASE__INST2_SEG0                     0
#define XGMI0_BASE__INST2_SEG1                     0
#define XGMI0_BASE__INST2_SEG2                     0
#define XGMI0_BASE__INST2_SEG3                     0
#define XGMI0_BASE__INST2_SEG4                     0
#define XGMI0_BASE__INST2_SEG5                     0

#define XGMI0_BASE__INST3_SEG0                     0
#define XGMI0_BASE__INST3_SEG1                     0
#define XGMI0_BASE__INST3_SEG2                     0
#define XGMI0_BASE__INST3_SEG3                     0
#define XGMI0_BASE__INST3_SEG4                     0
#define XGMI0_BASE__INST3_SEG5                     0

#define XGMI0_BASE__INST4_SEG0                     0
#define XGMI0_BASE__INST4_SEG1                     0
#define XGMI0_BASE__INST4_SEG2                     0
#define XGMI0_BASE__INST4_SEG3                     0
#define XGMI0_BASE__INST4_SEG4                     0
#define XGMI0_BASE__INST4_SEG5                     0

#define XGMI0_BASE__INST5_SEG0                     0
#define XGMI0_BASE__INST5_SEG1                     0
#define XGMI0_BASE__INST5_SEG2                     0
#define XGMI0_BASE__INST5_SEG3                     0
#define XGMI0_BASE__INST5_SEG4                     0
#define XGMI0_BASE__INST5_SEG5                     0

#define XGMI0_BASE__INST6_SEG0                     0
#define XGMI0_BASE__INST6_SEG1                     0
#define XGMI0_BASE__INST6_SEG2                     0
#define XGMI0_BASE__INST6_SEG3                     0
#define XGMI0_BASE__INST6_SEG4                     0
#define XGMI0_BASE__INST6_SEG5                     0

#define XGMI1_BASE__INST0_SEG0                     0x02439000
#define XGMI1_BASE__INST0_SEG1                     0x046C0000
#define XGMI1_BASE__INST0_SEG2                     0x04980000
#define XGMI1_BASE__INST0_SEG3                     0
#define XGMI1_BASE__INST0_SEG4                     0
#define XGMI1_BASE__INST0_SEG5                     0

#define XGMI1_BASE__INST1_SEG0                     0
#define XGMI1_BASE__INST1_SEG1                     0
#define XGMI1_BASE__INST1_SEG2                     0
#define XGMI1_BASE__INST1_SEG3                     0
#define XGMI1_BASE__INST1_SEG4                     0
#define XGMI1_BASE__INST1_SEG5                     0

#define XGMI1_BASE__INST2_SEG0                     0
#define XGMI1_BASE__INST2_SEG1                     0
#define XGMI1_BASE__INST2_SEG2                     0
#define XGMI1_BASE__INST2_SEG3                     0
#define XGMI1_BASE__INST2_SEG4                     0
#define XGMI1_BASE__INST2_SEG5                     0

#define XGMI1_BASE__INST3_SEG0                     0
#define XGMI1_BASE__INST3_SEG1                     0
#define XGMI1_BASE__INST3_SEG2                     0
#define XGMI1_BASE__INST3_SEG3                     0
#define XGMI1_BASE__INST3_SEG4                     0
#define XGMI1_BASE__INST3_SEG5                     0

#define XGMI1_BASE__INST4_SEG0                     0
#define XGMI1_BASE__INST4_SEG1                     0
#define XGMI1_BASE__INST4_SEG2                     0
#define XGMI1_BASE__INST4_SEG3                     0
#define XGMI1_BASE__INST4_SEG4                     0
#define XGMI1_BASE__INST4_SEG5                     0

#define XGMI1_BASE__INST5_SEG0                     0
#define XGMI1_BASE__INST5_SEG1                     0
#define XGMI1_BASE__INST5_SEG2                     0
#define XGMI1_BASE__INST5_SEG3                     0
#define XGMI1_BASE__INST5_SEG4                     0
#define XGMI1_BASE__INST5_SEG5                     0

#define XGMI1_BASE__INST6_SEG0                     0
#define XGMI1_BASE__INST6_SEG1                     0
#define XGMI1_BASE__INST6_SEG2                     0
#define XGMI1_BASE__INST6_SEG3                     0
#define XGMI1_BASE__INST6_SEG4                     0
#define XGMI1_BASE__INST6_SEG5                     0

#define XGMI2_BASE__INST0_SEG0                     0x04700000
#define XGMI2_BASE__INST0_SEG1                     0x049C0000
#define XGMI2_BASE__INST0_SEG2                     0
#define XGMI2_BASE__INST0_SEG3                     0
#define XGMI2_BASE__INST0_SEG4                     0
#define XGMI2_BASE__INST0_SEG5                     0

#define XGMI2_BASE__INST1_SEG0                     0x04740000
#define XGMI2_BASE__INST1_SEG1                     0x04A00000
#define XGMI2_BASE__INST1_SEG2                     0
#define XGMI2_BASE__INST1_SEG3                     0
#define XGMI2_BASE__INST1_SEG4                     0
#define XGMI2_BASE__INST1_SEG5                     0

#define XGMI2_BASE__INST2_SEG0                     0x04780000
#define XGMI2_BASE__INST2_SEG1                     0x04A40000
#define XGMI2_BASE__INST2_SEG2                     0
#define XGMI2_BASE__INST2_SEG3                     0
#define XGMI2_BASE__INST2_SEG4                     0
#define XGMI2_BASE__INST2_SEG5                     0

#define XGMI2_BASE__INST3_SEG0                     0x047C0000
#define XGMI2_BASE__INST3_SEG1                     0x04A80000
#define XGMI2_BASE__INST3_SEG2                     0
#define XGMI2_BASE__INST3_SEG3                     0
#define XGMI2_BASE__INST3_SEG4                     0
#define XGMI2_BASE__INST3_SEG5                     0

#define XGMI2_BASE__INST4_SEG0                     0x04800000
#define XGMI2_BASE__INST4_SEG1                     0x04AC0000
#define XGMI2_BASE__INST4_SEG2                     0
#define XGMI2_BASE__INST4_SEG3                     0
#define XGMI2_BASE__INST4_SEG4                     0
#define XGMI2_BASE__INST4_SEG5                     0

#define XGMI2_BASE__INST5_SEG0                     0x04840000
#define XGMI2_BASE__INST5_SEG1                     0x04B00000
#define XGMI2_BASE__INST5_SEG2                     0
#define XGMI2_BASE__INST5_SEG3                     0
#define XGMI2_BASE__INST5_SEG4                     0
#define XGMI2_BASE__INST5_SEG5                     0

#define XGMI2_BASE__INST6_SEG0                     0
#define XGMI2_BASE__INST6_SEG1                     0
#define XGMI2_BASE__INST6_SEG2                     0
#define XGMI2_BASE__INST6_SEG3                     0
#define XGMI2_BASE__INST6_SEG4                     0
#define XGMI2_BASE__INST6_SEG5                     0

#endif
