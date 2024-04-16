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
#ifndef _sienna_cichlid_ip_offset_HEADER
#define _sienna_cichlid_ip_offset_HEADER

#define MAX_INSTANCE                                        7
#define MAX_SEGMENT                                         5


struct IP_BASE_INSTANCE
{
    unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE
{
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
} __maybe_unused;


static const struct IP_BASE ATHUB_BASE = { { { { 0x00000C00, 0x02408C00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE CLK_BASE = { { { { 0x00016C00, 0x02401800, 0, 0, 0 } },
                                        { { 0x00016E00, 0x02401C00, 0, 0, 0 } },
                                        { { 0x00017000, 0x02402000, 0, 0, 0 } },
                                        { { 0x00017200, 0x02402400, 0, 0, 0 } },
                                        { { 0x0001B000, 0x0242D800, 0, 0, 0 } },
                                        { { 0x0001B200, 0x0242DC00, 0, 0, 0 } },
                                        { { 0x0001B400, 0x0242E000, 0, 0, 0 } } } };
static const struct IP_BASE DF_BASE = { { { { 0x00007000, 0x0240B800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DIO_BASE = { { { { 0x02404000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DCN_BASE = { { { { 0x00000012, 0x000000C0, 0x000034C0, 0x00009000, 0x02403C00 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DPCS_BASE = { { { { 0x00000012, 0x000000C0, 0x000034C0, 0x00009000, 0x02403C00 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE FUSE_BASE = { { { { 0x00017400, 0x02401400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE GC_BASE = { { { { 0x00001260, 0x0000A000, 0x0001C000, 0x02402C00, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE HDA_BASE = { { { { 0x004C0000, 0x02404800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE HDP_BASE = { { { { 0x00000F20, 0x0240A400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MMHUB_BASE = { { { { 0x0001A000, 0x02408800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP0_BASE = { { { { 0x00016000, 0x00DC0000, 0x00E00000, 0x00E40000, 0x0243FC00 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP1_BASE = { { { { 0x00016000, 0x00DC0000, 0x00E00000, 0x00E40000, 0x0243FC00 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE NBIO_BASE = { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0x0241B000 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE OSSSYS_BASE = { { { { 0x000010A0, 0x0240A000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE PCIE0_BASE = { { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0x0241B000 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA0_BASE = { { { { 0x00001260, 0x0000A000, 0x0001C000, 0x02402C00, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA1_BASE = { { { { 0x00001260, 0x0000A000, 0x0001C000, 0x02402C00, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SMUIO_BASE = { { { { 0x00016800, 0x00016A00, 0x00440000, 0x02401000, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE THM_BASE = { { { { 0x00016600, 0x02400C00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE UMC_BASE = { { { { 0x00014000, 0x02425800, 0, 0, 0 } },
                                        { { 0x00054000, 0x02425C00, 0, 0, 0 } },
                                        { { 0x00094000, 0x02426000, 0, 0, 0 } },
                                        { { 0x000D4000, 0x02426400, 0, 0, 0 } },
                                        { { 0x00114000, 0x02426800, 0, 0, 0 } },
                                        { { 0x00154000, 0x02426C00, 0, 0, 0 } },
                                        { { 0x00194000, 0x02427000, 0, 0, 0 } } } };
static const struct IP_BASE USB0_BASE = { { { { 0x0242A800, 0x05B00000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE VCN_BASE = { { { { 0x00007800, 0x00007E00, 0x02403000, 0, 0 } },
                                        { { 0x00007B00, 0x00012000, 0x02445000, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0 } } } };


#define ATHUB_BASE__INST0_SEG0                     0x00000C00
#define ATHUB_BASE__INST0_SEG1                     0x02408C00
#define ATHUB_BASE__INST0_SEG2                     0
#define ATHUB_BASE__INST0_SEG3                     0
#define ATHUB_BASE__INST0_SEG4                     0

#define ATHUB_BASE__INST1_SEG0                     0
#define ATHUB_BASE__INST1_SEG1                     0
#define ATHUB_BASE__INST1_SEG2                     0
#define ATHUB_BASE__INST1_SEG3                     0
#define ATHUB_BASE__INST1_SEG4                     0

#define ATHUB_BASE__INST2_SEG0                     0
#define ATHUB_BASE__INST2_SEG1                     0
#define ATHUB_BASE__INST2_SEG2                     0
#define ATHUB_BASE__INST2_SEG3                     0
#define ATHUB_BASE__INST2_SEG4                     0

#define ATHUB_BASE__INST3_SEG0                     0
#define ATHUB_BASE__INST3_SEG1                     0
#define ATHUB_BASE__INST3_SEG2                     0
#define ATHUB_BASE__INST3_SEG3                     0
#define ATHUB_BASE__INST3_SEG4                     0

#define ATHUB_BASE__INST4_SEG0                     0
#define ATHUB_BASE__INST4_SEG1                     0
#define ATHUB_BASE__INST4_SEG2                     0
#define ATHUB_BASE__INST4_SEG3                     0
#define ATHUB_BASE__INST4_SEG4                     0

#define ATHUB_BASE__INST5_SEG0                     0
#define ATHUB_BASE__INST5_SEG1                     0
#define ATHUB_BASE__INST5_SEG2                     0
#define ATHUB_BASE__INST5_SEG3                     0
#define ATHUB_BASE__INST5_SEG4                     0

#define ATHUB_BASE__INST6_SEG0                     0
#define ATHUB_BASE__INST6_SEG1                     0
#define ATHUB_BASE__INST6_SEG2                     0
#define ATHUB_BASE__INST6_SEG3                     0
#define ATHUB_BASE__INST6_SEG4                     0

#define CLK_BASE__INST0_SEG0                       0x00016C00
#define CLK_BASE__INST0_SEG1                       0x02401800
#define CLK_BASE__INST0_SEG2                       0
#define CLK_BASE__INST0_SEG3                       0
#define CLK_BASE__INST0_SEG4                       0

#define CLK_BASE__INST1_SEG0                       0x00016E00
#define CLK_BASE__INST1_SEG1                       0x02401C00
#define CLK_BASE__INST1_SEG2                       0
#define CLK_BASE__INST1_SEG3                       0
#define CLK_BASE__INST1_SEG4                       0

#define CLK_BASE__INST2_SEG0                       0x00017000
#define CLK_BASE__INST2_SEG1                       0x02402000
#define CLK_BASE__INST2_SEG2                       0
#define CLK_BASE__INST2_SEG3                       0
#define CLK_BASE__INST2_SEG4                       0

#define CLK_BASE__INST3_SEG0                       0x00017200
#define CLK_BASE__INST3_SEG1                       0x02402400
#define CLK_BASE__INST3_SEG2                       0
#define CLK_BASE__INST3_SEG3                       0
#define CLK_BASE__INST3_SEG4                       0

#define CLK_BASE__INST4_SEG0                       0x0001B000
#define CLK_BASE__INST4_SEG1                       0x0242D800
#define CLK_BASE__INST4_SEG2                       0
#define CLK_BASE__INST4_SEG3                       0
#define CLK_BASE__INST4_SEG4                       0

#define CLK_BASE__INST5_SEG0                       0x0001B200
#define CLK_BASE__INST5_SEG1                       0x0242DC00
#define CLK_BASE__INST5_SEG2                       0
#define CLK_BASE__INST5_SEG3                       0
#define CLK_BASE__INST5_SEG4                       0

#define CLK_BASE__INST6_SEG0                       0x0001B400
#define CLK_BASE__INST6_SEG1                       0x0242E000
#define CLK_BASE__INST6_SEG2                       0
#define CLK_BASE__INST6_SEG3                       0
#define CLK_BASE__INST6_SEG4                       0

#define DF_BASE__INST0_SEG0                        0x00007000
#define DF_BASE__INST0_SEG1                        0x0240B800
#define DF_BASE__INST0_SEG2                        0
#define DF_BASE__INST0_SEG3                        0
#define DF_BASE__INST0_SEG4                        0

#define DF_BASE__INST1_SEG0                        0
#define DF_BASE__INST1_SEG1                        0
#define DF_BASE__INST1_SEG2                        0
#define DF_BASE__INST1_SEG3                        0
#define DF_BASE__INST1_SEG4                        0

#define DF_BASE__INST2_SEG0                        0
#define DF_BASE__INST2_SEG1                        0
#define DF_BASE__INST2_SEG2                        0
#define DF_BASE__INST2_SEG3                        0
#define DF_BASE__INST2_SEG4                        0

#define DF_BASE__INST3_SEG0                        0
#define DF_BASE__INST3_SEG1                        0
#define DF_BASE__INST3_SEG2                        0
#define DF_BASE__INST3_SEG3                        0
#define DF_BASE__INST3_SEG4                        0

#define DF_BASE__INST4_SEG0                        0
#define DF_BASE__INST4_SEG1                        0
#define DF_BASE__INST4_SEG2                        0
#define DF_BASE__INST4_SEG3                        0
#define DF_BASE__INST4_SEG4                        0

#define DF_BASE__INST5_SEG0                        0
#define DF_BASE__INST5_SEG1                        0
#define DF_BASE__INST5_SEG2                        0
#define DF_BASE__INST5_SEG3                        0
#define DF_BASE__INST5_SEG4                        0

#define DF_BASE__INST6_SEG0                        0
#define DF_BASE__INST6_SEG1                        0
#define DF_BASE__INST6_SEG2                        0
#define DF_BASE__INST6_SEG3                        0
#define DF_BASE__INST6_SEG4                        0

#define DIO_BASE__INST0_SEG0                       0x02404000
#define DIO_BASE__INST0_SEG1                       0
#define DIO_BASE__INST0_SEG2                       0
#define DIO_BASE__INST0_SEG3                       0
#define DIO_BASE__INST0_SEG4                       0

#define DIO_BASE__INST1_SEG0                       0
#define DIO_BASE__INST1_SEG1                       0
#define DIO_BASE__INST1_SEG2                       0
#define DIO_BASE__INST1_SEG3                       0
#define DIO_BASE__INST1_SEG4                       0

#define DIO_BASE__INST2_SEG0                       0
#define DIO_BASE__INST2_SEG1                       0
#define DIO_BASE__INST2_SEG2                       0
#define DIO_BASE__INST2_SEG3                       0
#define DIO_BASE__INST2_SEG4                       0

#define DIO_BASE__INST3_SEG0                       0
#define DIO_BASE__INST3_SEG1                       0
#define DIO_BASE__INST3_SEG2                       0
#define DIO_BASE__INST3_SEG3                       0
#define DIO_BASE__INST3_SEG4                       0

#define DIO_BASE__INST4_SEG0                       0
#define DIO_BASE__INST4_SEG1                       0
#define DIO_BASE__INST4_SEG2                       0
#define DIO_BASE__INST4_SEG3                       0
#define DIO_BASE__INST4_SEG4                       0

#define DIO_BASE__INST5_SEG0                       0
#define DIO_BASE__INST5_SEG1                       0
#define DIO_BASE__INST5_SEG2                       0
#define DIO_BASE__INST5_SEG3                       0
#define DIO_BASE__INST5_SEG4                       0

#define DIO_BASE__INST6_SEG0                       0
#define DIO_BASE__INST6_SEG1                       0
#define DIO_BASE__INST6_SEG2                       0
#define DIO_BASE__INST6_SEG3                       0
#define DIO_BASE__INST6_SEG4                       0

#define DCN_BASE__INST0_SEG0                       0x00000012
#define DCN_BASE__INST0_SEG1                       0x000000C0
#define DCN_BASE__INST0_SEG2                       0x000034C0
#define DCN_BASE__INST0_SEG3                       0x00009000
#define DCN_BASE__INST0_SEG4                       0x02403C00

#define DCN_BASE__INST1_SEG0                       0
#define DCN_BASE__INST1_SEG1                       0
#define DCN_BASE__INST1_SEG2                       0
#define DCN_BASE__INST1_SEG3                       0
#define DCN_BASE__INST1_SEG4                       0

#define DCN_BASE__INST2_SEG0                       0
#define DCN_BASE__INST2_SEG1                       0
#define DCN_BASE__INST2_SEG2                       0
#define DCN_BASE__INST2_SEG3                       0
#define DCN_BASE__INST2_SEG4                       0

#define DCN_BASE__INST3_SEG0                       0
#define DCN_BASE__INST3_SEG1                       0
#define DCN_BASE__INST3_SEG2                       0
#define DCN_BASE__INST3_SEG3                       0
#define DCN_BASE__INST3_SEG4                       0

#define DCN_BASE__INST4_SEG0                       0
#define DCN_BASE__INST4_SEG1                       0
#define DCN_BASE__INST4_SEG2                       0
#define DCN_BASE__INST4_SEG3                       0
#define DCN_BASE__INST4_SEG4                       0

#define DCN_BASE__INST5_SEG0                       0
#define DCN_BASE__INST5_SEG1                       0
#define DCN_BASE__INST5_SEG2                       0
#define DCN_BASE__INST5_SEG3                       0
#define DCN_BASE__INST5_SEG4                       0

#define DCN_BASE__INST6_SEG0                       0
#define DCN_BASE__INST6_SEG1                       0
#define DCN_BASE__INST6_SEG2                       0
#define DCN_BASE__INST6_SEG3                       0
#define DCN_BASE__INST6_SEG4                       0

#define DPCS_BASE__INST0_SEG0                      0x00000012
#define DPCS_BASE__INST0_SEG1                      0x000000C0
#define DPCS_BASE__INST0_SEG2                      0x000034C0
#define DPCS_BASE__INST0_SEG3                      0x00009000
#define DPCS_BASE__INST0_SEG4                      0x02403C00

#define DPCS_BASE__INST1_SEG0                      0
#define DPCS_BASE__INST1_SEG1                      0
#define DPCS_BASE__INST1_SEG2                      0
#define DPCS_BASE__INST1_SEG3                      0
#define DPCS_BASE__INST1_SEG4                      0

#define DPCS_BASE__INST2_SEG0                      0
#define DPCS_BASE__INST2_SEG1                      0
#define DPCS_BASE__INST2_SEG2                      0
#define DPCS_BASE__INST2_SEG3                      0
#define DPCS_BASE__INST2_SEG4                      0

#define DPCS_BASE__INST3_SEG0                      0
#define DPCS_BASE__INST3_SEG1                      0
#define DPCS_BASE__INST3_SEG2                      0
#define DPCS_BASE__INST3_SEG3                      0
#define DPCS_BASE__INST3_SEG4                      0

#define DPCS_BASE__INST4_SEG0                      0
#define DPCS_BASE__INST4_SEG1                      0
#define DPCS_BASE__INST4_SEG2                      0
#define DPCS_BASE__INST4_SEG3                      0
#define DPCS_BASE__INST4_SEG4                      0

#define DPCS_BASE__INST5_SEG0                      0
#define DPCS_BASE__INST5_SEG1                      0
#define DPCS_BASE__INST5_SEG2                      0
#define DPCS_BASE__INST5_SEG3                      0
#define DPCS_BASE__INST5_SEG4                      0

#define DPCS_BASE__INST6_SEG0                      0
#define DPCS_BASE__INST6_SEG1                      0
#define DPCS_BASE__INST6_SEG2                      0
#define DPCS_BASE__INST6_SEG3                      0
#define DPCS_BASE__INST6_SEG4                      0

#define FUSE_BASE__INST0_SEG0                      0x00017400
#define FUSE_BASE__INST0_SEG1                      0x02401400
#define FUSE_BASE__INST0_SEG2                      0
#define FUSE_BASE__INST0_SEG3                      0
#define FUSE_BASE__INST0_SEG4                      0

#define FUSE_BASE__INST1_SEG0                      0
#define FUSE_BASE__INST1_SEG1                      0
#define FUSE_BASE__INST1_SEG2                      0
#define FUSE_BASE__INST1_SEG3                      0
#define FUSE_BASE__INST1_SEG4                      0

#define FUSE_BASE__INST2_SEG0                      0
#define FUSE_BASE__INST2_SEG1                      0
#define FUSE_BASE__INST2_SEG2                      0
#define FUSE_BASE__INST2_SEG3                      0
#define FUSE_BASE__INST2_SEG4                      0

#define FUSE_BASE__INST3_SEG0                      0
#define FUSE_BASE__INST3_SEG1                      0
#define FUSE_BASE__INST3_SEG2                      0
#define FUSE_BASE__INST3_SEG3                      0
#define FUSE_BASE__INST3_SEG4                      0

#define FUSE_BASE__INST4_SEG0                      0
#define FUSE_BASE__INST4_SEG1                      0
#define FUSE_BASE__INST4_SEG2                      0
#define FUSE_BASE__INST4_SEG3                      0
#define FUSE_BASE__INST4_SEG4                      0

#define FUSE_BASE__INST5_SEG0                      0
#define FUSE_BASE__INST5_SEG1                      0
#define FUSE_BASE__INST5_SEG2                      0
#define FUSE_BASE__INST5_SEG3                      0
#define FUSE_BASE__INST5_SEG4                      0

#define FUSE_BASE__INST6_SEG0                      0
#define FUSE_BASE__INST6_SEG1                      0
#define FUSE_BASE__INST6_SEG2                      0
#define FUSE_BASE__INST6_SEG3                      0
#define FUSE_BASE__INST6_SEG4                      0

#define GC_BASE__INST0_SEG0                        0x00001260
#define GC_BASE__INST0_SEG1                        0x0000A000
#define GC_BASE__INST0_SEG2                        0x0001C000
#define GC_BASE__INST0_SEG3                        0x02402C00
#define GC_BASE__INST0_SEG4                        0

#define GC_BASE__INST1_SEG0                        0
#define GC_BASE__INST1_SEG1                        0
#define GC_BASE__INST1_SEG2                        0
#define GC_BASE__INST1_SEG3                        0
#define GC_BASE__INST1_SEG4                        0

#define GC_BASE__INST2_SEG0                        0
#define GC_BASE__INST2_SEG1                        0
#define GC_BASE__INST2_SEG2                        0
#define GC_BASE__INST2_SEG3                        0
#define GC_BASE__INST2_SEG4                        0

#define GC_BASE__INST3_SEG0                        0
#define GC_BASE__INST3_SEG1                        0
#define GC_BASE__INST3_SEG2                        0
#define GC_BASE__INST3_SEG3                        0
#define GC_BASE__INST3_SEG4                        0

#define GC_BASE__INST4_SEG0                        0
#define GC_BASE__INST4_SEG1                        0
#define GC_BASE__INST4_SEG2                        0
#define GC_BASE__INST4_SEG3                        0
#define GC_BASE__INST4_SEG4                        0

#define GC_BASE__INST5_SEG0                        0
#define GC_BASE__INST5_SEG1                        0
#define GC_BASE__INST5_SEG2                        0
#define GC_BASE__INST5_SEG3                        0
#define GC_BASE__INST5_SEG4                        0

#define GC_BASE__INST6_SEG0                        0
#define GC_BASE__INST6_SEG1                        0
#define GC_BASE__INST6_SEG2                        0
#define GC_BASE__INST6_SEG3                        0
#define GC_BASE__INST6_SEG4                        0

#define HDA_BASE__INST0_SEG0                       0x004C0000
#define HDA_BASE__INST0_SEG1                       0x02404800
#define HDA_BASE__INST0_SEG2                       0
#define HDA_BASE__INST0_SEG3                       0
#define HDA_BASE__INST0_SEG4                       0

#define HDA_BASE__INST1_SEG0                       0
#define HDA_BASE__INST1_SEG1                       0
#define HDA_BASE__INST1_SEG2                       0
#define HDA_BASE__INST1_SEG3                       0
#define HDA_BASE__INST1_SEG4                       0

#define HDA_BASE__INST2_SEG0                       0
#define HDA_BASE__INST2_SEG1                       0
#define HDA_BASE__INST2_SEG2                       0
#define HDA_BASE__INST2_SEG3                       0
#define HDA_BASE__INST2_SEG4                       0

#define HDA_BASE__INST3_SEG0                       0
#define HDA_BASE__INST3_SEG1                       0
#define HDA_BASE__INST3_SEG2                       0
#define HDA_BASE__INST3_SEG3                       0
#define HDA_BASE__INST3_SEG4                       0

#define HDA_BASE__INST4_SEG0                       0
#define HDA_BASE__INST4_SEG1                       0
#define HDA_BASE__INST4_SEG2                       0
#define HDA_BASE__INST4_SEG3                       0
#define HDA_BASE__INST4_SEG4                       0

#define HDA_BASE__INST5_SEG0                       0
#define HDA_BASE__INST5_SEG1                       0
#define HDA_BASE__INST5_SEG2                       0
#define HDA_BASE__INST5_SEG3                       0
#define HDA_BASE__INST5_SEG4                       0

#define HDA_BASE__INST6_SEG0                       0
#define HDA_BASE__INST6_SEG1                       0
#define HDA_BASE__INST6_SEG2                       0
#define HDA_BASE__INST6_SEG3                       0
#define HDA_BASE__INST6_SEG4                       0

#define HDP_BASE__INST0_SEG0                       0x00000F20
#define HDP_BASE__INST0_SEG1                       0x0240A400
#define HDP_BASE__INST0_SEG2                       0
#define HDP_BASE__INST0_SEG3                       0
#define HDP_BASE__INST0_SEG4                       0

#define HDP_BASE__INST1_SEG0                       0
#define HDP_BASE__INST1_SEG1                       0
#define HDP_BASE__INST1_SEG2                       0
#define HDP_BASE__INST1_SEG3                       0
#define HDP_BASE__INST1_SEG4                       0

#define HDP_BASE__INST2_SEG0                       0
#define HDP_BASE__INST2_SEG1                       0
#define HDP_BASE__INST2_SEG2                       0
#define HDP_BASE__INST2_SEG3                       0
#define HDP_BASE__INST2_SEG4                       0

#define HDP_BASE__INST3_SEG0                       0
#define HDP_BASE__INST3_SEG1                       0
#define HDP_BASE__INST3_SEG2                       0
#define HDP_BASE__INST3_SEG3                       0
#define HDP_BASE__INST3_SEG4                       0

#define HDP_BASE__INST4_SEG0                       0
#define HDP_BASE__INST4_SEG1                       0
#define HDP_BASE__INST4_SEG2                       0
#define HDP_BASE__INST4_SEG3                       0
#define HDP_BASE__INST4_SEG4                       0

#define HDP_BASE__INST5_SEG0                       0
#define HDP_BASE__INST5_SEG1                       0
#define HDP_BASE__INST5_SEG2                       0
#define HDP_BASE__INST5_SEG3                       0
#define HDP_BASE__INST5_SEG4                       0

#define HDP_BASE__INST6_SEG0                       0
#define HDP_BASE__INST6_SEG1                       0
#define HDP_BASE__INST6_SEG2                       0
#define HDP_BASE__INST6_SEG3                       0
#define HDP_BASE__INST6_SEG4                       0

#define MMHUB_BASE__INST0_SEG0                     0x0001A000
#define MMHUB_BASE__INST0_SEG1                     0x02408800
#define MMHUB_BASE__INST0_SEG2                     0
#define MMHUB_BASE__INST0_SEG3                     0
#define MMHUB_BASE__INST0_SEG4                     0

#define MMHUB_BASE__INST1_SEG0                     0
#define MMHUB_BASE__INST1_SEG1                     0
#define MMHUB_BASE__INST1_SEG2                     0
#define MMHUB_BASE__INST1_SEG3                     0
#define MMHUB_BASE__INST1_SEG4                     0

#define MMHUB_BASE__INST2_SEG0                     0
#define MMHUB_BASE__INST2_SEG1                     0
#define MMHUB_BASE__INST2_SEG2                     0
#define MMHUB_BASE__INST2_SEG3                     0
#define MMHUB_BASE__INST2_SEG4                     0

#define MMHUB_BASE__INST3_SEG0                     0
#define MMHUB_BASE__INST3_SEG1                     0
#define MMHUB_BASE__INST3_SEG2                     0
#define MMHUB_BASE__INST3_SEG3                     0
#define MMHUB_BASE__INST3_SEG4                     0

#define MMHUB_BASE__INST4_SEG0                     0
#define MMHUB_BASE__INST4_SEG1                     0
#define MMHUB_BASE__INST4_SEG2                     0
#define MMHUB_BASE__INST4_SEG3                     0
#define MMHUB_BASE__INST4_SEG4                     0

#define MMHUB_BASE__INST5_SEG0                     0
#define MMHUB_BASE__INST5_SEG1                     0
#define MMHUB_BASE__INST5_SEG2                     0
#define MMHUB_BASE__INST5_SEG3                     0
#define MMHUB_BASE__INST5_SEG4                     0

#define MMHUB_BASE__INST6_SEG0                     0
#define MMHUB_BASE__INST6_SEG1                     0
#define MMHUB_BASE__INST6_SEG2                     0
#define MMHUB_BASE__INST6_SEG3                     0
#define MMHUB_BASE__INST6_SEG4                     0

#define MP0_BASE__INST0_SEG0                       0x00016000
#define MP0_BASE__INST0_SEG1                       0x00DC0000
#define MP0_BASE__INST0_SEG2                       0x00E00000
#define MP0_BASE__INST0_SEG3                       0x00E40000
#define MP0_BASE__INST0_SEG4                       0x0243FC00

#define MP0_BASE__INST1_SEG0                       0
#define MP0_BASE__INST1_SEG1                       0
#define MP0_BASE__INST1_SEG2                       0
#define MP0_BASE__INST1_SEG3                       0
#define MP0_BASE__INST1_SEG4                       0

#define MP0_BASE__INST2_SEG0                       0
#define MP0_BASE__INST2_SEG1                       0
#define MP0_BASE__INST2_SEG2                       0
#define MP0_BASE__INST2_SEG3                       0
#define MP0_BASE__INST2_SEG4                       0

#define MP0_BASE__INST3_SEG0                       0
#define MP0_BASE__INST3_SEG1                       0
#define MP0_BASE__INST3_SEG2                       0
#define MP0_BASE__INST3_SEG3                       0
#define MP0_BASE__INST3_SEG4                       0

#define MP0_BASE__INST4_SEG0                       0
#define MP0_BASE__INST4_SEG1                       0
#define MP0_BASE__INST4_SEG2                       0
#define MP0_BASE__INST4_SEG3                       0
#define MP0_BASE__INST4_SEG4                       0

#define MP0_BASE__INST5_SEG0                       0
#define MP0_BASE__INST5_SEG1                       0
#define MP0_BASE__INST5_SEG2                       0
#define MP0_BASE__INST5_SEG3                       0
#define MP0_BASE__INST5_SEG4                       0

#define MP0_BASE__INST6_SEG0                       0
#define MP0_BASE__INST6_SEG1                       0
#define MP0_BASE__INST6_SEG2                       0
#define MP0_BASE__INST6_SEG3                       0
#define MP0_BASE__INST6_SEG4                       0

#define MP1_BASE__INST0_SEG0                       0x00016000
#define MP1_BASE__INST0_SEG1                       0x00DC0000
#define MP1_BASE__INST0_SEG2                       0x00E00000
#define MP1_BASE__INST0_SEG3                       0x00E40000
#define MP1_BASE__INST0_SEG4                       0x0243FC00

#define MP1_BASE__INST1_SEG0                       0
#define MP1_BASE__INST1_SEG1                       0
#define MP1_BASE__INST1_SEG2                       0
#define MP1_BASE__INST1_SEG3                       0
#define MP1_BASE__INST1_SEG4                       0

#define MP1_BASE__INST2_SEG0                       0
#define MP1_BASE__INST2_SEG1                       0
#define MP1_BASE__INST2_SEG2                       0
#define MP1_BASE__INST2_SEG3                       0
#define MP1_BASE__INST2_SEG4                       0

#define MP1_BASE__INST3_SEG0                       0
#define MP1_BASE__INST3_SEG1                       0
#define MP1_BASE__INST3_SEG2                       0
#define MP1_BASE__INST3_SEG3                       0
#define MP1_BASE__INST3_SEG4                       0

#define MP1_BASE__INST4_SEG0                       0
#define MP1_BASE__INST4_SEG1                       0
#define MP1_BASE__INST4_SEG2                       0
#define MP1_BASE__INST4_SEG3                       0
#define MP1_BASE__INST4_SEG4                       0

#define MP1_BASE__INST5_SEG0                       0
#define MP1_BASE__INST5_SEG1                       0
#define MP1_BASE__INST5_SEG2                       0
#define MP1_BASE__INST5_SEG3                       0
#define MP1_BASE__INST5_SEG4                       0

#define MP1_BASE__INST6_SEG0                       0
#define MP1_BASE__INST6_SEG1                       0
#define MP1_BASE__INST6_SEG2                       0
#define MP1_BASE__INST6_SEG3                       0
#define MP1_BASE__INST6_SEG4                       0

#define NBIO_BASE__INST0_SEG0                      0x00000000
#define NBIO_BASE__INST0_SEG1                      0x00000014
#define NBIO_BASE__INST0_SEG2                      0x00000D20
#define NBIO_BASE__INST0_SEG3                      0x00010400
#define NBIO_BASE__INST0_SEG4                      0x0241B000

#define NBIO_BASE__INST1_SEG0                      0
#define NBIO_BASE__INST1_SEG1                      0
#define NBIO_BASE__INST1_SEG2                      0
#define NBIO_BASE__INST1_SEG3                      0
#define NBIO_BASE__INST1_SEG4                      0

#define NBIO_BASE__INST2_SEG0                      0
#define NBIO_BASE__INST2_SEG1                      0
#define NBIO_BASE__INST2_SEG2                      0
#define NBIO_BASE__INST2_SEG3                      0
#define NBIO_BASE__INST2_SEG4                      0

#define NBIO_BASE__INST3_SEG0                      0
#define NBIO_BASE__INST3_SEG1                      0
#define NBIO_BASE__INST3_SEG2                      0
#define NBIO_BASE__INST3_SEG3                      0
#define NBIO_BASE__INST3_SEG4                      0

#define NBIO_BASE__INST4_SEG0                      0
#define NBIO_BASE__INST4_SEG1                      0
#define NBIO_BASE__INST4_SEG2                      0
#define NBIO_BASE__INST4_SEG3                      0
#define NBIO_BASE__INST4_SEG4                      0

#define NBIO_BASE__INST5_SEG0                      0
#define NBIO_BASE__INST5_SEG1                      0
#define NBIO_BASE__INST5_SEG2                      0
#define NBIO_BASE__INST5_SEG3                      0
#define NBIO_BASE__INST5_SEG4                      0

#define NBIO_BASE__INST6_SEG0                      0
#define NBIO_BASE__INST6_SEG1                      0
#define NBIO_BASE__INST6_SEG2                      0
#define NBIO_BASE__INST6_SEG3                      0
#define NBIO_BASE__INST6_SEG4                      0

#define OSSSYS_BASE__INST0_SEG0                    0x000010A0
#define OSSSYS_BASE__INST0_SEG1                    0x0240A000
#define OSSSYS_BASE__INST0_SEG2                    0
#define OSSSYS_BASE__INST0_SEG3                    0
#define OSSSYS_BASE__INST0_SEG4                    0

#define OSSSYS_BASE__INST1_SEG0                    0
#define OSSSYS_BASE__INST1_SEG1                    0
#define OSSSYS_BASE__INST1_SEG2                    0
#define OSSSYS_BASE__INST1_SEG3                    0
#define OSSSYS_BASE__INST1_SEG4                    0

#define OSSSYS_BASE__INST2_SEG0                    0
#define OSSSYS_BASE__INST2_SEG1                    0
#define OSSSYS_BASE__INST2_SEG2                    0
#define OSSSYS_BASE__INST2_SEG3                    0
#define OSSSYS_BASE__INST2_SEG4                    0

#define OSSSYS_BASE__INST3_SEG0                    0
#define OSSSYS_BASE__INST3_SEG1                    0
#define OSSSYS_BASE__INST3_SEG2                    0
#define OSSSYS_BASE__INST3_SEG3                    0
#define OSSSYS_BASE__INST3_SEG4                    0

#define OSSSYS_BASE__INST4_SEG0                    0
#define OSSSYS_BASE__INST4_SEG1                    0
#define OSSSYS_BASE__INST4_SEG2                    0
#define OSSSYS_BASE__INST4_SEG3                    0
#define OSSSYS_BASE__INST4_SEG4                    0

#define OSSSYS_BASE__INST5_SEG0                    0
#define OSSSYS_BASE__INST5_SEG1                    0
#define OSSSYS_BASE__INST5_SEG2                    0
#define OSSSYS_BASE__INST5_SEG3                    0
#define OSSSYS_BASE__INST5_SEG4                    0

#define OSSSYS_BASE__INST6_SEG0                    0
#define OSSSYS_BASE__INST6_SEG1                    0
#define OSSSYS_BASE__INST6_SEG2                    0
#define OSSSYS_BASE__INST6_SEG3                    0
#define OSSSYS_BASE__INST6_SEG4                    0

#define PCIE0_BASE__INST0_SEG0                     0x00000000
#define PCIE0_BASE__INST0_SEG1                     0x00000014
#define PCIE0_BASE__INST0_SEG2                     0x00000D20
#define PCIE0_BASE__INST0_SEG3                     0x00010400
#define PCIE0_BASE__INST0_SEG4                     0x0241B000

#define PCIE0_BASE__INST1_SEG0                     0
#define PCIE0_BASE__INST1_SEG1                     0
#define PCIE0_BASE__INST1_SEG2                     0
#define PCIE0_BASE__INST1_SEG3                     0
#define PCIE0_BASE__INST1_SEG4                     0

#define PCIE0_BASE__INST2_SEG0                     0
#define PCIE0_BASE__INST2_SEG1                     0
#define PCIE0_BASE__INST2_SEG2                     0
#define PCIE0_BASE__INST2_SEG3                     0
#define PCIE0_BASE__INST2_SEG4                     0

#define PCIE0_BASE__INST3_SEG0                     0
#define PCIE0_BASE__INST3_SEG1                     0
#define PCIE0_BASE__INST3_SEG2                     0
#define PCIE0_BASE__INST3_SEG3                     0
#define PCIE0_BASE__INST3_SEG4                     0

#define PCIE0_BASE__INST4_SEG0                     0
#define PCIE0_BASE__INST4_SEG1                     0
#define PCIE0_BASE__INST4_SEG2                     0
#define PCIE0_BASE__INST4_SEG3                     0
#define PCIE0_BASE__INST4_SEG4                     0

#define PCIE0_BASE__INST5_SEG0                     0
#define PCIE0_BASE__INST5_SEG1                     0
#define PCIE0_BASE__INST5_SEG2                     0
#define PCIE0_BASE__INST5_SEG3                     0
#define PCIE0_BASE__INST5_SEG4                     0

#define PCIE0_BASE__INST6_SEG0                     0
#define PCIE0_BASE__INST6_SEG1                     0
#define PCIE0_BASE__INST6_SEG2                     0
#define PCIE0_BASE__INST6_SEG3                     0
#define PCIE0_BASE__INST6_SEG4                     0

#define SDMA0_BASE__INST0_SEG0                     0x00001260
#define SDMA0_BASE__INST0_SEG1                     0x0000A000
#define SDMA0_BASE__INST0_SEG2                     0x0001C000
#define SDMA0_BASE__INST0_SEG3                     0x02402C00
#define SDMA0_BASE__INST0_SEG4                     0

#define SDMA0_BASE__INST1_SEG0                     0
#define SDMA0_BASE__INST1_SEG1                     0
#define SDMA0_BASE__INST1_SEG2                     0
#define SDMA0_BASE__INST1_SEG3                     0
#define SDMA0_BASE__INST1_SEG4                     0

#define SDMA0_BASE__INST2_SEG0                     0
#define SDMA0_BASE__INST2_SEG1                     0
#define SDMA0_BASE__INST2_SEG2                     0
#define SDMA0_BASE__INST2_SEG3                     0
#define SDMA0_BASE__INST2_SEG4                     0

#define SDMA0_BASE__INST3_SEG0                     0
#define SDMA0_BASE__INST3_SEG1                     0
#define SDMA0_BASE__INST3_SEG2                     0
#define SDMA0_BASE__INST3_SEG3                     0
#define SDMA0_BASE__INST3_SEG4                     0

#define SDMA0_BASE__INST4_SEG0                     0
#define SDMA0_BASE__INST4_SEG1                     0
#define SDMA0_BASE__INST4_SEG2                     0
#define SDMA0_BASE__INST4_SEG3                     0
#define SDMA0_BASE__INST4_SEG4                     0

#define SDMA0_BASE__INST5_SEG0                     0
#define SDMA0_BASE__INST5_SEG1                     0
#define SDMA0_BASE__INST5_SEG2                     0
#define SDMA0_BASE__INST5_SEG3                     0
#define SDMA0_BASE__INST5_SEG4                     0

#define SDMA0_BASE__INST6_SEG0                     0
#define SDMA0_BASE__INST6_SEG1                     0
#define SDMA0_BASE__INST6_SEG2                     0
#define SDMA0_BASE__INST6_SEG3                     0
#define SDMA0_BASE__INST6_SEG4                     0

#define SDMA1_BASE__INST0_SEG0                     0x00001260
#define SDMA1_BASE__INST0_SEG1                     0x0000A000
#define SDMA1_BASE__INST0_SEG2                     0x0001C000
#define SDMA1_BASE__INST0_SEG3                     0x02402C00
#define SDMA1_BASE__INST0_SEG4                     0

#define SDMA1_BASE__INST1_SEG0                     0
#define SDMA1_BASE__INST1_SEG1                     0
#define SDMA1_BASE__INST1_SEG2                     0
#define SDMA1_BASE__INST1_SEG3                     0
#define SDMA1_BASE__INST1_SEG4                     0

#define SDMA1_BASE__INST2_SEG0                     0
#define SDMA1_BASE__INST2_SEG1                     0
#define SDMA1_BASE__INST2_SEG2                     0
#define SDMA1_BASE__INST2_SEG3                     0
#define SDMA1_BASE__INST2_SEG4                     0

#define SDMA1_BASE__INST3_SEG0                     0
#define SDMA1_BASE__INST3_SEG1                     0
#define SDMA1_BASE__INST3_SEG2                     0
#define SDMA1_BASE__INST3_SEG3                     0
#define SDMA1_BASE__INST3_SEG4                     0

#define SDMA1_BASE__INST4_SEG0                     0
#define SDMA1_BASE__INST4_SEG1                     0
#define SDMA1_BASE__INST4_SEG2                     0
#define SDMA1_BASE__INST4_SEG3                     0
#define SDMA1_BASE__INST4_SEG4                     0

#define SDMA1_BASE__INST5_SEG0                     0
#define SDMA1_BASE__INST5_SEG1                     0
#define SDMA1_BASE__INST5_SEG2                     0
#define SDMA1_BASE__INST5_SEG3                     0
#define SDMA1_BASE__INST5_SEG4                     0

#define SDMA1_BASE__INST6_SEG0                     0
#define SDMA1_BASE__INST6_SEG1                     0
#define SDMA1_BASE__INST6_SEG2                     0
#define SDMA1_BASE__INST6_SEG3                     0
#define SDMA1_BASE__INST6_SEG4                     0

#define SMUIO_BASE__INST0_SEG0                     0x00016800
#define SMUIO_BASE__INST0_SEG1                     0x00016A00
#define SMUIO_BASE__INST0_SEG2                     0x00440000
#define SMUIO_BASE__INST0_SEG3                     0x02401000
#define SMUIO_BASE__INST0_SEG4                     0

#define SMUIO_BASE__INST1_SEG0                     0
#define SMUIO_BASE__INST1_SEG1                     0
#define SMUIO_BASE__INST1_SEG2                     0
#define SMUIO_BASE__INST1_SEG3                     0
#define SMUIO_BASE__INST1_SEG4                     0

#define SMUIO_BASE__INST2_SEG0                     0
#define SMUIO_BASE__INST2_SEG1                     0
#define SMUIO_BASE__INST2_SEG2                     0
#define SMUIO_BASE__INST2_SEG3                     0
#define SMUIO_BASE__INST2_SEG4                     0

#define SMUIO_BASE__INST3_SEG0                     0
#define SMUIO_BASE__INST3_SEG1                     0
#define SMUIO_BASE__INST3_SEG2                     0
#define SMUIO_BASE__INST3_SEG3                     0
#define SMUIO_BASE__INST3_SEG4                     0

#define SMUIO_BASE__INST4_SEG0                     0
#define SMUIO_BASE__INST4_SEG1                     0
#define SMUIO_BASE__INST4_SEG2                     0
#define SMUIO_BASE__INST4_SEG3                     0
#define SMUIO_BASE__INST4_SEG4                     0

#define SMUIO_BASE__INST5_SEG0                     0
#define SMUIO_BASE__INST5_SEG1                     0
#define SMUIO_BASE__INST5_SEG2                     0
#define SMUIO_BASE__INST5_SEG3                     0
#define SMUIO_BASE__INST5_SEG4                     0

#define SMUIO_BASE__INST6_SEG0                     0
#define SMUIO_BASE__INST6_SEG1                     0
#define SMUIO_BASE__INST6_SEG2                     0
#define SMUIO_BASE__INST6_SEG3                     0
#define SMUIO_BASE__INST6_SEG4                     0

#define THM_BASE__INST0_SEG0                       0x00016600
#define THM_BASE__INST0_SEG1                       0x02400C00
#define THM_BASE__INST0_SEG2                       0
#define THM_BASE__INST0_SEG3                       0
#define THM_BASE__INST0_SEG4                       0

#define THM_BASE__INST1_SEG0                       0
#define THM_BASE__INST1_SEG1                       0
#define THM_BASE__INST1_SEG2                       0
#define THM_BASE__INST1_SEG3                       0
#define THM_BASE__INST1_SEG4                       0

#define THM_BASE__INST2_SEG0                       0
#define THM_BASE__INST2_SEG1                       0
#define THM_BASE__INST2_SEG2                       0
#define THM_BASE__INST2_SEG3                       0
#define THM_BASE__INST2_SEG4                       0

#define THM_BASE__INST3_SEG0                       0
#define THM_BASE__INST3_SEG1                       0
#define THM_BASE__INST3_SEG2                       0
#define THM_BASE__INST3_SEG3                       0
#define THM_BASE__INST3_SEG4                       0

#define THM_BASE__INST4_SEG0                       0
#define THM_BASE__INST4_SEG1                       0
#define THM_BASE__INST4_SEG2                       0
#define THM_BASE__INST4_SEG3                       0
#define THM_BASE__INST4_SEG4                       0

#define THM_BASE__INST5_SEG0                       0
#define THM_BASE__INST5_SEG1                       0
#define THM_BASE__INST5_SEG2                       0
#define THM_BASE__INST5_SEG3                       0
#define THM_BASE__INST5_SEG4                       0

#define THM_BASE__INST6_SEG0                       0
#define THM_BASE__INST6_SEG1                       0
#define THM_BASE__INST6_SEG2                       0
#define THM_BASE__INST6_SEG3                       0
#define THM_BASE__INST6_SEG4                       0

#define UMC_BASE__INST0_SEG0                       0x00014000
#define UMC_BASE__INST0_SEG1                       0x02425800
#define UMC_BASE__INST0_SEG2                       0
#define UMC_BASE__INST0_SEG3                       0
#define UMC_BASE__INST0_SEG4                       0

#define UMC_BASE__INST1_SEG0                       0x00054000
#define UMC_BASE__INST1_SEG1                       0x02425C00
#define UMC_BASE__INST1_SEG2                       0
#define UMC_BASE__INST1_SEG3                       0
#define UMC_BASE__INST1_SEG4                       0

#define UMC_BASE__INST2_SEG0                       0x00094000
#define UMC_BASE__INST2_SEG1                       0x02426000
#define UMC_BASE__INST2_SEG2                       0
#define UMC_BASE__INST2_SEG3                       0
#define UMC_BASE__INST2_SEG4                       0

#define UMC_BASE__INST3_SEG0                       0x000D4000
#define UMC_BASE__INST3_SEG1                       0x02426400
#define UMC_BASE__INST3_SEG2                       0
#define UMC_BASE__INST3_SEG3                       0
#define UMC_BASE__INST3_SEG4                       0

#define UMC_BASE__INST4_SEG0                       0x00114000
#define UMC_BASE__INST4_SEG1                       0x02426800
#define UMC_BASE__INST4_SEG2                       0
#define UMC_BASE__INST4_SEG3                       0
#define UMC_BASE__INST4_SEG4                       0

#define UMC_BASE__INST5_SEG0                       0x00154000
#define UMC_BASE__INST5_SEG1                       0x02426C00
#define UMC_BASE__INST5_SEG2                       0
#define UMC_BASE__INST5_SEG3                       0
#define UMC_BASE__INST5_SEG4                       0

#define UMC_BASE__INST6_SEG0                       0x00194000
#define UMC_BASE__INST6_SEG1                       0x02427000
#define UMC_BASE__INST6_SEG2                       0
#define UMC_BASE__INST6_SEG3                       0
#define UMC_BASE__INST6_SEG4                       0

#define USB0_BASE__INST0_SEG0                      0x0242A800
#define USB0_BASE__INST0_SEG1                      0x05B00000
#define USB0_BASE__INST0_SEG2                      0
#define USB0_BASE__INST0_SEG3                      0
#define USB0_BASE__INST0_SEG4                      0

#define USB0_BASE__INST1_SEG0                      0
#define USB0_BASE__INST1_SEG1                      0
#define USB0_BASE__INST1_SEG2                      0
#define USB0_BASE__INST1_SEG3                      0
#define USB0_BASE__INST1_SEG4                      0

#define USB0_BASE__INST2_SEG0                      0
#define USB0_BASE__INST2_SEG1                      0
#define USB0_BASE__INST2_SEG2                      0
#define USB0_BASE__INST2_SEG3                      0
#define USB0_BASE__INST2_SEG4                      0

#define USB0_BASE__INST3_SEG0                      0
#define USB0_BASE__INST3_SEG1                      0
#define USB0_BASE__INST3_SEG2                      0
#define USB0_BASE__INST3_SEG3                      0
#define USB0_BASE__INST3_SEG4                      0

#define USB0_BASE__INST4_SEG0                      0
#define USB0_BASE__INST4_SEG1                      0
#define USB0_BASE__INST4_SEG2                      0
#define USB0_BASE__INST4_SEG3                      0
#define USB0_BASE__INST4_SEG4                      0

#define USB0_BASE__INST5_SEG0                      0
#define USB0_BASE__INST5_SEG1                      0
#define USB0_BASE__INST5_SEG2                      0
#define USB0_BASE__INST5_SEG3                      0
#define USB0_BASE__INST5_SEG4                      0

#define USB0_BASE__INST6_SEG0                      0
#define USB0_BASE__INST6_SEG1                      0
#define USB0_BASE__INST6_SEG2                      0
#define USB0_BASE__INST6_SEG3                      0
#define USB0_BASE__INST6_SEG4                      0

#define VCN_BASE__INST0_SEG0                       0x00007800
#define VCN_BASE__INST0_SEG1                       0x00007E00
#define VCN_BASE__INST0_SEG2                       0x02403000
#define VCN_BASE__INST0_SEG3                       0
#define VCN_BASE__INST0_SEG4                       0

#define VCN_BASE__INST1_SEG0                       0x00007B00
#define VCN_BASE__INST1_SEG1                       0x00012000
#define VCN_BASE__INST1_SEG2                       0x02445000
#define VCN_BASE__INST1_SEG3                       0
#define VCN_BASE__INST1_SEG4                       0

#define VCN_BASE__INST2_SEG0                       0
#define VCN_BASE__INST2_SEG1                       0
#define VCN_BASE__INST2_SEG2                       0
#define VCN_BASE__INST2_SEG3                       0
#define VCN_BASE__INST2_SEG4                       0

#define VCN_BASE__INST3_SEG0                       0
#define VCN_BASE__INST3_SEG1                       0
#define VCN_BASE__INST3_SEG2                       0
#define VCN_BASE__INST3_SEG3                       0
#define VCN_BASE__INST3_SEG4                       0

#define VCN_BASE__INST4_SEG0                       0
#define VCN_BASE__INST4_SEG1                       0
#define VCN_BASE__INST4_SEG2                       0
#define VCN_BASE__INST4_SEG3                       0
#define VCN_BASE__INST4_SEG4                       0

#define VCN_BASE__INST5_SEG0                       0
#define VCN_BASE__INST5_SEG1                       0
#define VCN_BASE__INST5_SEG2                       0
#define VCN_BASE__INST5_SEG3                       0
#define VCN_BASE__INST5_SEG4                       0

#define VCN_BASE__INST6_SEG0                       0
#define VCN_BASE__INST6_SEG1                       0
#define VCN_BASE__INST6_SEG2                       0
#define VCN_BASE__INST6_SEG3                       0
#define VCN_BASE__INST6_SEG4                       0

#endif
