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
#ifndef _arct_ip_offset_HEADER
#define _arct_ip_offset_HEADER

#define MAX_INSTANCE                                       8
#define MAX_SEGMENT                                         6


struct IP_BASE_INSTANCE
{
    unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE
{
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};


static const struct IP_BASE ATHUB_BASE            ={ { { { 0x00000C20, 0x00012460, 0x00408C00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE CLK_BASE            ={ { { { 0x000120C0, 0x00016C00, 0x00401800, 0, 0, 0 } },
                                        { { 0x000120E0, 0x00016E00, 0x00401C00, 0, 0, 0 } },
                                        { { 0x00012100, 0x00017000, 0x00402000, 0, 0, 0 } },
                                        { { 0x00012120, 0x00017200, 0x00402400, 0, 0, 0 } },
                                        { { 0x000136C0, 0x0001B000, 0x0042D800, 0, 0, 0 } },
                                        { { 0x00013720, 0x0001B200, 0x0042E400, 0, 0, 0 } },
                                        { { 0x000125E0, 0x00017E00, 0x0040BC00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DF_BASE            ={ { { { 0x00007000, 0x000125C0, 0x0040B800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE FUSE_BASE            ={ { { { 0x000120A0, 0x00017400, 0x00401400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE GC_BASE            ={ { { { 0x00002000, 0x0000A000, 0x00012160, 0x00402C00, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE HDP_BASE            ={ { { { 0x00000F20, 0x00012520, 0x0040A400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MMHUB_BASE            ={ { { { 0x00012440, 0x0001A000, 0x00408800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP0_BASE            ={ { { { 0x00016000, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE MP1_BASE            ={ { { { 0x00016000, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE NBIF0_BASE            ={ { { { 0x00000000, 0x00000014, 0x00000D20, 0x00010400, 0x00012D80, 0x0041B000 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE OSSSYS_BASE            ={ { { { 0x000010A0, 0x00012500, 0x0040A000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE PCIE0_BASE            ={ { { { 0x000128C0, 0x00411800, 0x04440000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA0_BASE            ={ { { { 0x00001260, 0x00012540, 0x0040A800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA1_BASE            ={ { { { 0x00001860, 0x00012560, 0x0040AC00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA2_BASE            ={ { { { 0x00013760, 0x0001E000, 0x0042EC00, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA3_BASE            ={ { { { 0x00013780, 0x0001E400, 0x0042F000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA4_BASE            ={ { { { 0x000137A0, 0x0001E800, 0x0042F400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA5_BASE            ={ { { { 0x000137C0, 0x0001EC00, 0x0042F800, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA6_BASE            ={ { { { 0x000137E0, 0x0001F000, 0x0042FC00, 0, 0, 0 } },
                                       { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SDMA7_BASE            ={ { { { 0x00013800, 0x0001F400, 0x00430000, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE SMUIO_BASE            ={ { { { 0x00016800, 0x00016A00, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE THM_BASE            ={ { { { 0x00016600, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE UMC_BASE            ={ { { { 0x000132C0, 0x00014000, 0x00425800, 0, 0, 0 } },
                                        { { 0x000132E0, 0x00054000, 0x00425C00, 0, 0, 0 } },
                                        { { 0x00013300, 0x00094000, 0x00426000, 0, 0, 0 } },
                                        { { 0x00013320, 0x000D4000, 0x00426400, 0, 0, 0 } },
                                        { { 0x00013340, 0x00114000, 0x00426800, 0, 0, 0 } },
                                        { { 0x00013360, 0x00154000, 0x00426C00, 0, 0, 0 } },
                                        { { 0x00013380, 0x00194000, 0x00427000, 0, 0, 0 } },
                                        { { 0x000133A0, 0x001D4000, 0x00427400, 0, 0, 0 } } } };
static const struct IP_BASE UVD_BASE            ={ { { { 0x00007800, 0x00007E00, 0x00012180, 0x00403000, 0, 0 } },
                                        { { 0x00007A00, 0x00009000, 0x000136E0, 0x0042DC00, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE DBGU_IO_BASE            ={ { { { 0x000001E0, 0x000125A0, 0x0040B400, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_BASE RSMU_BASE            ={ { { { 0x00012000, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };



#define ATHUB_BASE__INST0_SEG0                     0x00000C20
#define ATHUB_BASE__INST0_SEG1                     0x00012460
#define ATHUB_BASE__INST0_SEG2                     0x00408C00
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

#define ATHUB_BASE__INST7_SEG0                     0
#define ATHUB_BASE__INST7_SEG1                     0
#define ATHUB_BASE__INST7_SEG2                     0
#define ATHUB_BASE__INST7_SEG3                     0
#define ATHUB_BASE__INST7_SEG4                     0
#define ATHUB_BASE__INST7_SEG5                     0

#define CLK_BASE__INST0_SEG0                       0x000120C0
#define CLK_BASE__INST0_SEG1                       0x00016C00
#define CLK_BASE__INST0_SEG2                       0x00401800
#define CLK_BASE__INST0_SEG3                       0
#define CLK_BASE__INST0_SEG4                       0
#define CLK_BASE__INST0_SEG5                       0

#define CLK_BASE__INST1_SEG0                       0x000120E0
#define CLK_BASE__INST1_SEG1                       0x00016E00
#define CLK_BASE__INST1_SEG2                       0x00401C00
#define CLK_BASE__INST1_SEG3                       0
#define CLK_BASE__INST1_SEG4                       0
#define CLK_BASE__INST1_SEG5                       0

#define CLK_BASE__INST2_SEG0                       0x00012100
#define CLK_BASE__INST2_SEG1                       0x00017000
#define CLK_BASE__INST2_SEG2                       0x00402000
#define CLK_BASE__INST2_SEG3                       0
#define CLK_BASE__INST2_SEG4                       0
#define CLK_BASE__INST2_SEG5                       0

#define CLK_BASE__INST3_SEG0                       0x00012120
#define CLK_BASE__INST3_SEG1                       0x00017200
#define CLK_BASE__INST3_SEG2                       0x00402400
#define CLK_BASE__INST3_SEG3                       0
#define CLK_BASE__INST3_SEG4                       0
#define CLK_BASE__INST3_SEG5                       0

#define CLK_BASE__INST4_SEG0                       0x000136C0
#define CLK_BASE__INST4_SEG1                       0x0001B000
#define CLK_BASE__INST4_SEG2                       0x0042D800
#define CLK_BASE__INST4_SEG3                       0
#define CLK_BASE__INST4_SEG4                       0
#define CLK_BASE__INST4_SEG5                       0

#define CLK_BASE__INST5_SEG0                       0x00013720
#define CLK_BASE__INST5_SEG1                       0x0001B200
#define CLK_BASE__INST5_SEG2                       0x0042E400
#define CLK_BASE__INST5_SEG3                       0
#define CLK_BASE__INST5_SEG4                       0
#define CLK_BASE__INST5_SEG5                       0

#define CLK_BASE__INST6_SEG0                       0x000125E0
#define CLK_BASE__INST6_SEG1                       0x00017E00
#define CLK_BASE__INST6_SEG2                       0x0040BC00
#define CLK_BASE__INST6_SEG3                       0
#define CLK_BASE__INST6_SEG4                       0
#define CLK_BASE__INST6_SEG5                       0

#define CLK_BASE__INST7_SEG0                       0
#define CLK_BASE__INST7_SEG1                       0
#define CLK_BASE__INST7_SEG2                       0
#define CLK_BASE__INST7_SEG3                       0
#define CLK_BASE__INST7_SEG4                       0
#define CLK_BASE__INST7_SEG5                       0

#define DF_BASE__INST0_SEG0                        0x00007000
#define DF_BASE__INST0_SEG1                        0x000125C0
#define DF_BASE__INST0_SEG2                        0x0040B800
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

#define DF_BASE__INST7_SEG0                        0
#define DF_BASE__INST7_SEG1                        0
#define DF_BASE__INST7_SEG2                        0
#define DF_BASE__INST7_SEG3                        0
#define DF_BASE__INST7_SEG4                        0
#define DF_BASE__INST7_SEG5                        0

#define FUSE_BASE__INST0_SEG0                      0x000120A0
#define FUSE_BASE__INST0_SEG1                      0x00017400
#define FUSE_BASE__INST0_SEG2                      0x00401400
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

#define FUSE_BASE__INST7_SEG0                      0
#define FUSE_BASE__INST7_SEG1                      0
#define FUSE_BASE__INST7_SEG2                      0
#define FUSE_BASE__INST7_SEG3                      0
#define FUSE_BASE__INST7_SEG4                      0
#define FUSE_BASE__INST7_SEG5                      0

#define GC_BASE__INST0_SEG0                        0x00002000
#define GC_BASE__INST0_SEG1                        0x0000A000
#define GC_BASE__INST0_SEG2                        0x00012160
#define GC_BASE__INST0_SEG3                        0x00402C00
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

#define GC_BASE__INST7_SEG0                        0
#define GC_BASE__INST7_SEG1                        0
#define GC_BASE__INST7_SEG2                        0
#define GC_BASE__INST7_SEG3                        0
#define GC_BASE__INST7_SEG4                        0
#define GC_BASE__INST7_SEG5                        0

#define HDP_BASE__INST0_SEG0                       0x00000F20
#define HDP_BASE__INST0_SEG1                       0x00012520
#define HDP_BASE__INST0_SEG2                       0x0040A400
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

#define HDP_BASE__INST7_SEG0                       0
#define HDP_BASE__INST7_SEG1                       0
#define HDP_BASE__INST7_SEG2                       0
#define HDP_BASE__INST7_SEG3                       0
#define HDP_BASE__INST7_SEG4                       0
#define HDP_BASE__INST7_SEG5                       0

#define MMHUB_BASE__INST0_SEG0                     0x00012440
#define MMHUB_BASE__INST0_SEG1                     0x0001A000
#define MMHUB_BASE__INST0_SEG2                     0x00408800
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

#define MMHUB_BASE__INST7_SEG0                     0
#define MMHUB_BASE__INST7_SEG1                     0
#define MMHUB_BASE__INST7_SEG2                     0
#define MMHUB_BASE__INST7_SEG3                     0
#define MMHUB_BASE__INST7_SEG4                     0
#define MMHUB_BASE__INST7_SEG5                     0

#define MP0_BASE__INST0_SEG0                       0x00013FE0
#define MP0_BASE__INST0_SEG1                       0x00016000
#define MP0_BASE__INST0_SEG2                       0x0043FC00
#define MP0_BASE__INST0_SEG3                       0x00DC0000
#define MP0_BASE__INST0_SEG4                       0x00E00000
#define MP0_BASE__INST0_SEG5                       0x00E40000

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

#define MP0_BASE__INST7_SEG0                       0
#define MP0_BASE__INST7_SEG1                       0
#define MP0_BASE__INST7_SEG2                       0
#define MP0_BASE__INST7_SEG3                       0
#define MP0_BASE__INST7_SEG4                       0
#define MP0_BASE__INST7_SEG5                       0

#define MP1_BASE__INST0_SEG0                       0x00012020
#define MP1_BASE__INST0_SEG1                       0x00016200
#define MP1_BASE__INST0_SEG2                       0x00400400
#define MP1_BASE__INST0_SEG3                       0x00E80000
#define MP1_BASE__INST0_SEG4                       0x00EC0000
#define MP1_BASE__INST0_SEG5                       0x00F00000

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

#define MP1_BASE__INST7_SEG0                       0
#define MP1_BASE__INST7_SEG1                       0
#define MP1_BASE__INST7_SEG2                       0
#define MP1_BASE__INST7_SEG3                       0
#define MP1_BASE__INST7_SEG4                       0
#define MP1_BASE__INST7_SEG5                       0

#define NBIF0_BASE__INST0_SEG0                     0x00000000
#define NBIF0_BASE__INST0_SEG1                     0x00000014
#define NBIF0_BASE__INST0_SEG2                     0x00000D20
#define NBIF0_BASE__INST0_SEG3                     0x00010400
#define NBIF0_BASE__INST0_SEG4                     0x00012D80
#define NBIF0_BASE__INST0_SEG5                     0x0041B000

#define NBIF0_BASE__INST1_SEG0                     0
#define NBIF0_BASE__INST1_SEG1                     0
#define NBIF0_BASE__INST1_SEG2                     0
#define NBIF0_BASE__INST1_SEG3                     0
#define NBIF0_BASE__INST1_SEG4                     0
#define NBIF0_BASE__INST1_SEG5                     0

#define NBIF0_BASE__INST2_SEG0                     0
#define NBIF0_BASE__INST2_SEG1                     0
#define NBIF0_BASE__INST2_SEG2                     0
#define NBIF0_BASE__INST2_SEG3                     0
#define NBIF0_BASE__INST2_SEG4                     0
#define NBIF0_BASE__INST2_SEG5                     0

#define NBIF0_BASE__INST3_SEG0                     0
#define NBIF0_BASE__INST3_SEG1                     0
#define NBIF0_BASE__INST3_SEG2                     0
#define NBIF0_BASE__INST3_SEG3                     0
#define NBIF0_BASE__INST3_SEG4                     0
#define NBIF0_BASE__INST3_SEG5                     0

#define NBIF0_BASE__INST4_SEG0                     0
#define NBIF0_BASE__INST4_SEG1                     0
#define NBIF0_BASE__INST4_SEG2                     0
#define NBIF0_BASE__INST4_SEG3                     0
#define NBIF0_BASE__INST4_SEG4                     0
#define NBIF0_BASE__INST4_SEG5                     0

#define NBIF0_BASE__INST5_SEG0                     0
#define NBIF0_BASE__INST5_SEG1                     0
#define NBIF0_BASE__INST5_SEG2                     0
#define NBIF0_BASE__INST5_SEG3                     0
#define NBIF0_BASE__INST5_SEG4                     0
#define NBIF0_BASE__INST5_SEG5                     0

#define NBIF0_BASE__INST6_SEG0                     0
#define NBIF0_BASE__INST6_SEG1                     0
#define NBIF0_BASE__INST6_SEG2                     0
#define NBIF0_BASE__INST6_SEG3                     0
#define NBIF0_BASE__INST6_SEG4                     0
#define NBIF0_BASE__INST6_SEG5                     0

#define NBIF0_BASE__INST7_SEG0                     0
#define NBIF0_BASE__INST7_SEG1                     0
#define NBIF0_BASE__INST7_SEG2                     0
#define NBIF0_BASE__INST7_SEG3                     0
#define NBIF0_BASE__INST7_SEG4                     0
#define NBIF0_BASE__INST7_SEG5                     0

#define OSSSYS_BASE__INST0_SEG0                    0x000010A0
#define OSSSYS_BASE__INST0_SEG1                    0x00012500
#define OSSSYS_BASE__INST0_SEG2                    0x0040A000
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

#define OSSSYS_BASE__INST7_SEG0                    0
#define OSSSYS_BASE__INST7_SEG1                    0
#define OSSSYS_BASE__INST7_SEG2                    0
#define OSSSYS_BASE__INST7_SEG3                    0
#define OSSSYS_BASE__INST7_SEG4                    0
#define OSSSYS_BASE__INST7_SEG5                    0

#define PCIE0_BASE__INST0_SEG0                     0x000128C0
#define PCIE0_BASE__INST0_SEG1                     0x00411800
#define PCIE0_BASE__INST0_SEG2                     0x04440000
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

#define PCIE0_BASE__INST7_SEG0                     0
#define PCIE0_BASE__INST7_SEG1                     0
#define PCIE0_BASE__INST7_SEG2                     0
#define PCIE0_BASE__INST7_SEG3                     0
#define PCIE0_BASE__INST7_SEG4                     0
#define PCIE0_BASE__INST7_SEG5                     0

#define SDMA0_BASE__INST0_SEG0                     0x00001260
#define SDMA0_BASE__INST0_SEG1                     0x00012540
#define SDMA0_BASE__INST0_SEG2                     0x0040A800
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
#define SDMA1_BASE__INST0_SEG1                     0x00012560
#define SDMA1_BASE__INST0_SEG2                     0x0040AC00
#define SDMA1_BASE__INST0_SEG3                     0
#define SDMA1_BASE__INST0_SEG4                     0
#define SDMA1_BASE__INST0_SEG5                     0

#define SDMA1_BASE__INST1_SEG0                     0
#define SDMA1_BASE__INST1_SEG1                     0
#define SDMA1_BASE__INST1_SEG2                     0
#define SDMA1_BASE__INST1_SEG3                     0
#define SDMA1_BASE__INST1_SEG4                     0
#define SDMA1_BASE__INST1_SEG5                     0

#define SDMA1_BASE__INST2_SEG0                     0
#define SDMA1_BASE__INST2_SEG1                     0
#define SDMA1_BASE__INST2_SEG2                     0
#define SDMA1_BASE__INST2_SEG3                     0
#define SDMA1_BASE__INST2_SEG4                     0
#define SDMA1_BASE__INST2_SEG5                     0

#define SDMA1_BASE__INST3_SEG0                     0
#define SDMA1_BASE__INST3_SEG1                     0
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


#define SDMA2_BASE__INST0_SEG0                     0x00013760
#define SDMA2_BASE__INST0_SEG1                     0x0001E000
#define SDMA2_BASE__INST0_SEG2                     0x0042EC00
#define SDMA2_BASE__INST0_SEG3                     0
#define SDMA2_BASE__INST0_SEG4                     0
#define SDMA2_BASE__INST0_SEG5                     0


#define SDMA2_BASE__INST1_SEG0                     0
#define SDMA2_BASE__INST1_SEG1                     0
#define SDMA2_BASE__INST1_SEG2                     0
#define SDMA2_BASE__INST1_SEG3                     0
#define SDMA2_BASE__INST1_SEG4                     0
#define SDMA2_BASE__INST1_SEG5                     0

#define SDMA2_BASE__INST2_SEG0                     0
#define SDMA2_BASE__INST2_SEG1                     0
#define SDMA2_BASE__INST2_SEG2                     0
#define SDMA2_BASE__INST2_SEG3                     0
#define SDMA2_BASE__INST2_SEG4                     0
#define SDMA2_BASE__INST2_SEG5                     0

#define SDMA2_BASE__INST3_SEG0                     0
#define SDMA2_BASE__INST3_SEG1                     0
#define SDMA2_BASE__INST3_SEG2                     0
#define SDMA2_BASE__INST3_SEG3                     0
#define SDMA2_BASE__INST3_SEG4                     0
#define SDMA2_BASE__INST3_SEG5                     0

#define SDMA2_BASE__INST4_SEG0                     0
#define SDMA2_BASE__INST4_SEG1                     0
#define SDMA2_BASE__INST4_SEG2                     0
#define SDMA2_BASE__INST4_SEG3                     0
#define SDMA2_BASE__INST4_SEG4                     0
#define SDMA2_BASE__INST4_SEG5                     0

#define SDMA2_BASE__INST5_SEG0                     0
#define SDMA2_BASE__INST5_SEG1                     0
#define SDMA2_BASE__INST5_SEG2                     0
#define SDMA2_BASE__INST5_SEG3                     0
#define SDMA2_BASE__INST5_SEG4                     0
#define SDMA2_BASE__INST5_SEG5                     0

#define SDMA2_BASE__INST6_SEG0                     0
#define SDMA2_BASE__INST6_SEG1                     0
#define SDMA2_BASE__INST6_SEG2                     0
#define SDMA2_BASE__INST6_SEG3                     0
#define SDMA2_BASE__INST6_SEG4                     0
#define SDMA2_BASE__INST6_SEG5                     0

#define SDMA3_BASE__INST0_SEG0                     0x00013780
#define SDMA3_BASE__INST0_SEG1                     0x0001E400
#define SDMA3_BASE__INST0_SEG2                     0x0042F000
#define SDMA3_BASE__INST0_SEG3                     0
#define SDMA3_BASE__INST0_SEG4                     0
#define SDMA3_BASE__INST0_SEG5                     0

#define SDMA3_BASE__INST1_SEG0                     0
#define SDMA3_BASE__INST1_SEG1                     0
#define SDMA3_BASE__INST1_SEG2                     0
#define SDMA3_BASE__INST1_SEG3                     0
#define SDMA3_BASE__INST1_SEG4                     0
#define SDMA3_BASE__INST1_SEG5                     0

#define SDMA3_BASE__INST2_SEG0                     0
#define SDMA3_BASE__INST2_SEG1                     0
#define SDMA3_BASE__INST2_SEG2                     0
#define SDMA3_BASE__INST2_SEG3                     0
#define SDMA3_BASE__INST2_SEG4                     0
#define SDMA3_BASE__INST2_SEG5                     0

#define SDMA3_BASE__INST3_SEG0                     0
#define SDMA3_BASE__INST3_SEG1                     0
#define SDMA3_BASE__INST3_SEG2                     0
#define SDMA3_BASE__INST3_SEG3                     0
#define SDMA3_BASE__INST3_SEG4                     0
#define SDMA3_BASE__INST3_SEG5                     0

#define SDMA3_BASE__INST4_SEG0                     0
#define SDMA3_BASE__INST4_SEG1                     0
#define SDMA3_BASE__INST4_SEG2                     0
#define SDMA3_BASE__INST4_SEG3                     0
#define SDMA3_BASE__INST4_SEG4                     0
#define SDMA3_BASE__INST4_SEG5                     0

#define SDMA3_BASE__INST5_SEG0                     0
#define SDMA3_BASE__INST5_SEG1                     0
#define SDMA3_BASE__INST5_SEG2                     0
#define SDMA3_BASE__INST5_SEG3                     0
#define SDMA3_BASE__INST5_SEG4                     0
#define SDMA3_BASE__INST5_SEG5                     0

#define SDMA3_BASE__INST6_SEG0                     0
#define SDMA3_BASE__INST6_SEG1                     0
#define SDMA3_BASE__INST6_SEG2                     0
#define SDMA3_BASE__INST6_SEG3                     0
#define SDMA3_BASE__INST6_SEG4                     0
#define SDMA3_BASE__INST6_SEG5                     0

#define SDMA4_BASE__INST0_SEG0                     0x000137A0
#define SDMA4_BASE__INST0_SEG1                     0x0001E800
#define SDMA4_BASE__INST0_SEG2                     0x0042F400
#define SDMA4_BASE__INST0_SEG3                     0
#define SDMA4_BASE__INST0_SEG4                     0
#define SDMA4_BASE__INST0_SEG5                     0

#define SDMA4_BASE__INST1_SEG0                     0
#define SDMA4_BASE__INST1_SEG1                     0
#define SDMA4_BASE__INST1_SEG2                     0
#define SDMA4_BASE__INST1_SEG3                     0
#define SDMA4_BASE__INST1_SEG4                     0
#define SDMA4_BASE__INST1_SEG5                     0

#define SDMA4_BASE__INST2_SEG0                     0
#define SDMA4_BASE__INST2_SEG1                     0
#define SDMA4_BASE__INST2_SEG2                     0
#define SDMA4_BASE__INST2_SEG3                     0
#define SDMA4_BASE__INST2_SEG4                     0
#define SDMA4_BASE__INST2_SEG5                     0

#define SDMA4_BASE__INST3_SEG0                     0
#define SDMA4_BASE__INST3_SEG1                     0
#define SDMA4_BASE__INST3_SEG2                     0
#define SDMA4_BASE__INST3_SEG3                     0
#define SDMA4_BASE__INST3_SEG4                     0
#define SDMA4_BASE__INST3_SEG5                     0

#define SDMA4_BASE__INST4_SEG0                     0
#define SDMA4_BASE__INST4_SEG1                     0
#define SDMA4_BASE__INST4_SEG2                     0
#define SDMA4_BASE__INST4_SEG3                     0
#define SDMA4_BASE__INST4_SEG4                     0
#define SDMA4_BASE__INST4_SEG5                     0

#define SDMA4_BASE__INST5_SEG0                     0
#define SDMA4_BASE__INST5_SEG1                     0
#define SDMA4_BASE__INST5_SEG2                     0
#define SDMA4_BASE__INST5_SEG3                     0
#define SDMA4_BASE__INST5_SEG4                     0
#define SDMA4_BASE__INST5_SEG5                     0

#define SDMA4_BASE__INST6_SEG0                     0
#define SDMA4_BASE__INST6_SEG1                     0
#define SDMA4_BASE__INST6_SEG2                     0
#define SDMA4_BASE__INST6_SEG3                     0
#define SDMA4_BASE__INST6_SEG4                     0
#define SDMA4_BASE__INST6_SEG5                     0

#define SDMA5_BASE__INST0_SEG0                     0x000137C0
#define SDMA5_BASE__INST0_SEG1                     0x0001EC00
#define SDMA5_BASE__INST0_SEG2                     0x0042F800
#define SDMA5_BASE__INST0_SEG3                     0
#define SDMA5_BASE__INST0_SEG4                     0
#define SDMA5_BASE__INST0_SEG5                     0

#define SDMA5_BASE__INST1_SEG0                     0
#define SDMA5_BASE__INST1_SEG1                     0
#define SDMA5_BASE__INST1_SEG2                     0
#define SDMA5_BASE__INST1_SEG3                     0
#define SDMA5_BASE__INST1_SEG4                     0
#define SDMA5_BASE__INST1_SEG5                     0

#define SDMA5_BASE__INST2_SEG0                     0
#define SDMA5_BASE__INST2_SEG1                     0
#define SDMA5_BASE__INST2_SEG2                     0
#define SDMA5_BASE__INST2_SEG3                     0
#define SDMA5_BASE__INST2_SEG4                     0
#define SDMA5_BASE__INST2_SEG5                     0

#define SDMA5_BASE__INST3_SEG0                     0
#define SDMA5_BASE__INST3_SEG1                     0
#define SDMA5_BASE__INST3_SEG2                     0
#define SDMA5_BASE__INST3_SEG3                     0
#define SDMA5_BASE__INST3_SEG4                     0
#define SDMA5_BASE__INST3_SEG5                     0

#define SDMA5_BASE__INST4_SEG0                     0
#define SDMA5_BASE__INST4_SEG1                     0
#define SDMA5_BASE__INST4_SEG2                     0
#define SDMA5_BASE__INST4_SEG3                     0
#define SDMA5_BASE__INST4_SEG4                     0
#define SDMA5_BASE__INST4_SEG5                     0

#define SDMA5_BASE__INST5_SEG0                     0
#define SDMA5_BASE__INST5_SEG1                     0
#define SDMA5_BASE__INST5_SEG2                     0
#define SDMA5_BASE__INST5_SEG3                     0
#define SDMA5_BASE__INST5_SEG4                     0
#define SDMA5_BASE__INST5_SEG5                     0

#define SDMA5_BASE__INST6_SEG0                     0
#define SDMA5_BASE__INST6_SEG1                     0
#define SDMA5_BASE__INST6_SEG2                     0
#define SDMA5_BASE__INST6_SEG3                     0
#define SDMA5_BASE__INST6_SEG4                     0
#define SDMA5_BASE__INST6_SEG5                     0

#define SDMA6_BASE__INST0_SEG0                     0x000137E0
#define SDMA6_BASE__INST0_SEG1                     0x0001F000
#define SDMA6_BASE__INST0_SEG2                     0x0042FC00
#define SDMA6_BASE__INST0_SEG3                     0
#define SDMA6_BASE__INST0_SEG4                     0
#define SDMA6_BASE__INST0_SEG5                     0

#define SDMA6_BASE__INST1_SEG0                     0
#define SDMA6_BASE__INST1_SEG1                     0
#define SDMA6_BASE__INST1_SEG2                     0
#define SDMA6_BASE__INST1_SEG3                     0
#define SDMA6_BASE__INST1_SEG4                     0
#define SDMA6_BASE__INST1_SEG5                     0

#define SDMA6_BASE__INST2_SEG0                     0
#define SDMA6_BASE__INST2_SEG1                     0
#define SDMA6_BASE__INST2_SEG2                     0
#define SDMA6_BASE__INST2_SEG3                     0
#define SDMA6_BASE__INST2_SEG4                     0
#define SDMA6_BASE__INST2_SEG5                     0

#define SDMA6_BASE__INST3_SEG0                     0
#define SDMA6_BASE__INST3_SEG1                     0
#define SDMA6_BASE__INST3_SEG2                     0
#define SDMA6_BASE__INST3_SEG3                     0
#define SDMA6_BASE__INST3_SEG4                     0
#define SDMA6_BASE__INST3_SEG5                     0

#define SDMA6_BASE__INST4_SEG0                     0
#define SDMA6_BASE__INST4_SEG1                     0
#define SDMA6_BASE__INST4_SEG2                     0
#define SDMA6_BASE__INST4_SEG3                     0
#define SDMA6_BASE__INST4_SEG4                     0
#define SDMA6_BASE__INST4_SEG5                     0

#define SDMA6_BASE__INST5_SEG0                     0
#define SDMA6_BASE__INST5_SEG1                     0
#define SDMA6_BASE__INST5_SEG2                     0
#define SDMA6_BASE__INST5_SEG3                     0
#define SDMA6_BASE__INST5_SEG4                     0
#define SDMA6_BASE__INST5_SEG5                     0

#define SDMA6_BASE__INST6_SEG0                     0
#define SDMA6_BASE__INST6_SEG1                     0
#define SDMA6_BASE__INST6_SEG2                     0
#define SDMA6_BASE__INST6_SEG3                     0
#define SDMA6_BASE__INST6_SEG4                     0
#define SDMA6_BASE__INST6_SEG5                     0

#define SDMA7_BASE__INST0_SEG0                     0x00013800
#define SDMA7_BASE__INST0_SEG1                     0x0001F400
#define SDMA7_BASE__INST0_SEG2                     0x00430000
#define SDMA7_BASE__INST0_SEG3                     0
#define SDMA7_BASE__INST0_SEG4                     0
#define SDMA7_BASE__INST0_SEG5                     0

#define SDMA7_BASE__INST1_SEG0                     0
#define SDMA7_BASE__INST1_SEG1                     0
#define SDMA7_BASE__INST1_SEG2                     0
#define SDMA7_BASE__INST1_SEG3                     0
#define SDMA7_BASE__INST1_SEG4                     0
#define SDMA7_BASE__INST1_SEG5                     0

#define SDMA7_BASE__INST2_SEG0                     0
#define SDMA7_BASE__INST2_SEG1                     0
#define SDMA7_BASE__INST2_SEG2                     0
#define SDMA7_BASE__INST2_SEG3                     0
#define SDMA7_BASE__INST2_SEG4                     0
#define SDMA7_BASE__INST2_SEG5                     0

#define SDMA7_BASE__INST3_SEG0                     0
#define SDMA7_BASE__INST3_SEG1                     0
#define SDMA7_BASE__INST3_SEG2                     0
#define SDMA7_BASE__INST3_SEG3                     0
#define SDMA7_BASE__INST3_SEG4                     0
#define SDMA7_BASE__INST3_SEG5                     0

#define SDMA7_BASE__INST4_SEG0                     0
#define SDMA7_BASE__INST4_SEG1                     0
#define SDMA7_BASE__INST4_SEG2                     0
#define SDMA7_BASE__INST4_SEG3                     0
#define SDMA7_BASE__INST4_SEG4                     0
#define SDMA7_BASE__INST4_SEG5                     0

#define SDMA7_BASE__INST5_SEG0                     0
#define SDMA7_BASE__INST5_SEG1                     0
#define SDMA7_BASE__INST5_SEG2                     0
#define SDMA7_BASE__INST5_SEG3                     0
#define SDMA7_BASE__INST5_SEG4                     0
#define SDMA7_BASE__INST5_SEG5                     0

#define SDMA7_BASE__INST6_SEG0                     0
#define SDMA7_BASE__INST6_SEG1                     0
#define SDMA7_BASE__INST6_SEG2                     0
#define SDMA7_BASE__INST6_SEG3                     0
#define SDMA7_BASE__INST6_SEG4                     0
#define SDMA7_BASE__INST6_SEG5                     0

#define SMUIO_BASE__INST0_SEG0                     0x00012080
#define SMUIO_BASE__INST0_SEG1                     0x00016800
#define SMUIO_BASE__INST0_SEG2                     0x00016A00
#define SMUIO_BASE__INST0_SEG3                     0x00401000
#define SMUIO_BASE__INST0_SEG4                     0x00440000
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

#define SMUIO_BASE__INST7_SEG0                     0
#define SMUIO_BASE__INST7_SEG1                     0
#define SMUIO_BASE__INST7_SEG2                     0
#define SMUIO_BASE__INST7_SEG3                     0
#define SMUIO_BASE__INST7_SEG4                     0
#define SMUIO_BASE__INST7_SEG5                     0

#define THM_BASE__INST0_SEG0                       0x00012060
#define THM_BASE__INST0_SEG1                       0x00016600
#define THM_BASE__INST0_SEG2                       0x00400C00
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

#define THM_BASE__INST7_SEG0                       0
#define THM_BASE__INST7_SEG1                       0
#define THM_BASE__INST7_SEG2                       0
#define THM_BASE__INST7_SEG3                       0
#define THM_BASE__INST7_SEG4                       0
#define THM_BASE__INST7_SEG5                       0

#define UMC_BASE__INST0_SEG0                       0x000132C0
#define UMC_BASE__INST0_SEG1                       0x00014000
#define UMC_BASE__INST0_SEG2                       0x00425800
#define UMC_BASE__INST0_SEG3                       0
#define UMC_BASE__INST0_SEG4                       0
#define UMC_BASE__INST0_SEG5                       0

#define UMC_BASE__INST1_SEG0                       0x000132E0
#define UMC_BASE__INST1_SEG1                       0x00054000
#define UMC_BASE__INST1_SEG2                       0x00425C00
#define UMC_BASE__INST1_SEG3                       0
#define UMC_BASE__INST1_SEG4                       0
#define UMC_BASE__INST1_SEG5                       0

#define UMC_BASE__INST2_SEG0                       0x00013300
#define UMC_BASE__INST2_SEG1                       0x00094000
#define UMC_BASE__INST2_SEG2                       0x00426000
#define UMC_BASE__INST2_SEG3                       0
#define UMC_BASE__INST2_SEG4                       0
#define UMC_BASE__INST2_SEG5                       0

#define UMC_BASE__INST3_SEG0                       0x00013320
#define UMC_BASE__INST3_SEG1                       0x000D4000
#define UMC_BASE__INST3_SEG2                       0x00426400
#define UMC_BASE__INST3_SEG3                       0
#define UMC_BASE__INST3_SEG4                       0
#define UMC_BASE__INST3_SEG5                       0

#define UMC_BASE__INST4_SEG0                       0x00013340
#define UMC_BASE__INST4_SEG1                       0x00114000
#define UMC_BASE__INST4_SEG2                       0x00426800
#define UMC_BASE__INST4_SEG3                       0
#define UMC_BASE__INST4_SEG4                       0
#define UMC_BASE__INST4_SEG5                       0

#define UMC_BASE__INST5_SEG0                       0x00013360
#define UMC_BASE__INST5_SEG1                       0x00154000
#define UMC_BASE__INST5_SEG2                       0x00426C00
#define UMC_BASE__INST5_SEG3                       0
#define UMC_BASE__INST5_SEG4                       0
#define UMC_BASE__INST5_SEG5                       0

#define UMC_BASE__INST6_SEG0                       0x00013380
#define UMC_BASE__INST6_SEG1                       0x00194000
#define UMC_BASE__INST6_SEG2                       0x00427000
#define UMC_BASE__INST6_SEG3                       0
#define UMC_BASE__INST6_SEG4                       0
#define UMC_BASE__INST6_SEG5                       0

#define UMC_BASE__INST7_SEG0                       0x000133A0
#define UMC_BASE__INST7_SEG1                       0x001D4000
#define UMC_BASE__INST7_SEG2                       0x00427400
#define UMC_BASE__INST7_SEG3                       0
#define UMC_BASE__INST7_SEG4                       0
#define UMC_BASE__INST7_SEG5                       0

#define UVD_BASE__INST0_SEG0                       0x00007800
#define UVD_BASE__INST0_SEG1                       0x00007E00
#define UVD_BASE__INST0_SEG2                       0x00012180
#define UVD_BASE__INST0_SEG3                       0x00403000
#define UVD_BASE__INST0_SEG4                       0
#define UVD_BASE__INST0_SEG5                       0

#define UVD_BASE__INST1_SEG0                       0x00007A00
#define UVD_BASE__INST1_SEG1                       0x00009000
#define UVD_BASE__INST1_SEG2                       0x000136E0
#define UVD_BASE__INST1_SEG3                       0x0042DC00
#define UVD_BASE__INST1_SEG4                       0
#define UVD_BASE__INST1_SEG5                       0

#define UVD_BASE__INST2_SEG0                       0
#define UVD_BASE__INST2_SEG1                       0
#define UVD_BASE__INST2_SEG2                       0
#define UVD_BASE__INST2_SEG3                       0
#define UVD_BASE__INST2_SEG4                       0
#define UVD_BASE__INST2_SEG5                       0

#define UVD_BASE__INST3_SEG0                       0
#define UVD_BASE__INST3_SEG1                       0
#define UVD_BASE__INST3_SEG2                       0
#define UVD_BASE__INST3_SEG3                       0
#define UVD_BASE__INST3_SEG4                       0
#define UVD_BASE__INST3_SEG5                       0

#define UVD_BASE__INST4_SEG0                       0
#define UVD_BASE__INST4_SEG1                       0
#define UVD_BASE__INST4_SEG2                       0
#define UVD_BASE__INST4_SEG3                       0
#define UVD_BASE__INST4_SEG4                       0
#define UVD_BASE__INST4_SEG5                       0

#define UVD_BASE__INST5_SEG0                       0
#define UVD_BASE__INST5_SEG1                       0
#define UVD_BASE__INST5_SEG2                       0
#define UVD_BASE__INST5_SEG3                       0
#define UVD_BASE__INST5_SEG4                       0
#define UVD_BASE__INST5_SEG5                       0

#define UVD_BASE__INST6_SEG0                       0
#define UVD_BASE__INST6_SEG1                       0
#define UVD_BASE__INST6_SEG2                       0
#define UVD_BASE__INST6_SEG3                       0
#define UVD_BASE__INST6_SEG4                       0
#define UVD_BASE__INST6_SEG5                       0

#define UVD_BASE__INST7_SEG0                       0
#define UVD_BASE__INST7_SEG1                       0
#define UVD_BASE__INST7_SEG2                       0
#define UVD_BASE__INST7_SEG3                       0
#define UVD_BASE__INST7_SEG4                       0
#define UVD_BASE__INST7_SEG5                       0

#define DBGU_IO_BASE__INST0_SEG0                   0x000001E0
#define DBGU_IO_BASE__INST0_SEG1                   0x000125A0
#define DBGU_IO_BASE__INST0_SEG2                   0x0040B400
#define DBGU_IO_BASE__INST0_SEG3                   0
#define DBGU_IO_BASE__INST0_SEG4                   0
#define DBGU_IO_BASE__INST0_SEG5                   0

#define DBGU_IO_BASE__INST1_SEG0                   0
#define DBGU_IO_BASE__INST1_SEG1                   0
#define DBGU_IO_BASE__INST1_SEG2                   0
#define DBGU_IO_BASE__INST1_SEG3                   0
#define DBGU_IO_BASE__INST1_SEG4                   0
#define DBGU_IO_BASE__INST1_SEG5                   0

#define DBGU_IO_BASE__INST2_SEG0                   0
#define DBGU_IO_BASE__INST2_SEG1                   0
#define DBGU_IO_BASE__INST2_SEG2                   0
#define DBGU_IO_BASE__INST2_SEG3                   0
#define DBGU_IO_BASE__INST2_SEG4                   0
#define DBGU_IO_BASE__INST2_SEG5                   0

#define DBGU_IO_BASE__INST3_SEG0                   0
#define DBGU_IO_BASE__INST3_SEG1                   0
#define DBGU_IO_BASE__INST3_SEG2                   0
#define DBGU_IO_BASE__INST3_SEG3                   0
#define DBGU_IO_BASE__INST3_SEG4                   0
#define DBGU_IO_BASE__INST3_SEG5                   0

#define DBGU_IO_BASE__INST4_SEG0                   0
#define DBGU_IO_BASE__INST4_SEG1                   0
#define DBGU_IO_BASE__INST4_SEG2                   0
#define DBGU_IO_BASE__INST4_SEG3                   0
#define DBGU_IO_BASE__INST4_SEG4                   0
#define DBGU_IO_BASE__INST4_SEG5                   0

#define DBGU_IO_BASE__INST5_SEG0                   0
#define DBGU_IO_BASE__INST5_SEG1                   0
#define DBGU_IO_BASE__INST5_SEG2                   0
#define DBGU_IO_BASE__INST5_SEG3                   0
#define DBGU_IO_BASE__INST5_SEG4                   0
#define DBGU_IO_BASE__INST5_SEG5                   0

#define DBGU_IO_BASE__INST6_SEG0                   0
#define DBGU_IO_BASE__INST6_SEG1                   0
#define DBGU_IO_BASE__INST6_SEG2                   0
#define DBGU_IO_BASE__INST6_SEG3                   0
#define DBGU_IO_BASE__INST6_SEG4                   0
#define DBGU_IO_BASE__INST6_SEG5                   0

#define DBGU_IO_BASE__INST7_SEG0                   0
#define DBGU_IO_BASE__INST7_SEG1                   0
#define DBGU_IO_BASE__INST7_SEG2                   0
#define DBGU_IO_BASE__INST7_SEG3                   0
#define DBGU_IO_BASE__INST7_SEG4                   0
#define DBGU_IO_BASE__INST7_SEG5                   0

#define RSMU_BASE__INST0_SEG0                   0x00012000
#define RSMU_BASE__INST0_SEG1                   0
#define RSMU_BASE__INST0_SEG2                   0
#define RSMU_BASE__INST0_SEG3                   0
#define RSMU_BASE__INST0_SEG4                   0
#define RSMU_BASE__INST0_SEG5                   0

#define RSMU_BASE__INST1_SEG0                   0
#define RSMU_BASE__INST1_SEG1                   0
#define RSMU_BASE__INST1_SEG2                   0
#define RSMU_BASE__INST1_SEG3                   0
#define RSMU_BASE__INST1_SEG4                   0
#define RSMU_BASE__INST1_SEG5                   0

#define RSMU_BASE__INST2_SEG0                   0
#define RSMU_BASE__INST2_SEG1                   0
#define RSMU_BASE__INST2_SEG2                   0
#define RSMU_BASE__INST2_SEG3                   0
#define RSMU_BASE__INST2_SEG4                   0
#define RSMU_BASE__INST2_SEG5                   0

#define RSMU_BASE__INST3_SEG0                   0
#define RSMU_BASE__INST3_SEG1                   0
#define RSMU_BASE__INST3_SEG2                   0
#define RSMU_BASE__INST3_SEG3                   0
#define RSMU_BASE__INST3_SEG4                   0
#define RSMU_BASE__INST3_SEG5                   0

#define RSMU_BASE__INST4_SEG0                   0
#define RSMU_BASE__INST4_SEG1                   0
#define RSMU_BASE__INST4_SEG2                   0
#define RSMU_BASE__INST4_SEG3                   0
#define RSMU_BASE__INST4_SEG4                   0
#define RSMU_BASE__INST4_SEG5                   0

#define RSMU_BASE__INST5_SEG0                   0
#define RSMU_BASE__INST5_SEG1                   0
#define RSMU_BASE__INST5_SEG2                   0
#define RSMU_BASE__INST5_SEG3                   0
#define RSMU_BASE__INST5_SEG4                   0
#define RSMU_BASE__INST5_SEG5                   0

#define RSMU_BASE__INST6_SEG0                   0
#define RSMU_BASE__INST6_SEG1                   0
#define RSMU_BASE__INST6_SEG2                   0
#define RSMU_BASE__INST6_SEG3                   0
#define RSMU_BASE__INST6_SEG4                   0
#define RSMU_BASE__INST6_SEG5                   0

#define RSMU_BASE__INST7_SEG0                   0
#define RSMU_BASE__INST7_SEG1                   0
#define RSMU_BASE__INST7_SEG2                   0
#define RSMU_BASE__INST7_SEG3                   0
#define RSMU_BASE__INST7_SEG4                   0
#define RSMU_BASE__INST7_SEG5                   0


#endif
