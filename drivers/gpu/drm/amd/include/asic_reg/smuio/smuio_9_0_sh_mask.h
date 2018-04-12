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
#ifndef _smuio_9_0_SH_MASK_HEADER
#define _smuio_9_0_SH_MASK_HEADER


// addressBlock: smuio_smuio_SmuSmuioDec
//ROM_CNTL
#define ROM_CNTL__CLOCK_GATING_EN__SHIFT                                                                      0x0
#define ROM_CNTL__CLOCK_GATING_EN_MASK                                                                        0x00000001L
//ROM_STATUS
#define ROM_STATUS__ROM_BUSY__SHIFT                                                                           0x0
#define ROM_STATUS__ROM_BUSY_MASK                                                                             0x00000001L
//CGTT_ROM_CLK_CTRL0
#define CGTT_ROM_CLK_CTRL0__ON_DELAY__SHIFT                                                                   0x0
#define CGTT_ROM_CLK_CTRL0__OFF_HYSTERESIS__SHIFT                                                             0x4
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1__SHIFT                                                             0x1e
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0__SHIFT                                                             0x1f
#define CGTT_ROM_CLK_CTRL0__ON_DELAY_MASK                                                                     0x0000000FL
#define CGTT_ROM_CLK_CTRL0__OFF_HYSTERESIS_MASK                                                               0x00000FF0L
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK                                                               0x40000000L
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK                                                               0x80000000L
//ROM_INDEX
#define ROM_INDEX__ROM_INDEX__SHIFT                                                                           0x0
#define ROM_INDEX__ROM_INDEX_MASK                                                                             0x00FFFFFFL
//ROM_DATA
#define ROM_DATA__ROM_DATA__SHIFT                                                                             0x0
#define ROM_DATA__ROM_DATA_MASK                                                                               0xFFFFFFFFL
//ROM_START
#define ROM_START__ROM_START__SHIFT                                                                           0x0
#define ROM_START__ROM_START_MASK                                                                             0x00FFFFFFL
//ROM_SW_CNTL
#define ROM_SW_CNTL__DATA_SIZE__SHIFT                                                                         0x0
#define ROM_SW_CNTL__COMMAND_SIZE__SHIFT                                                                      0x10
#define ROM_SW_CNTL__ROM_SW_RETURN_DATA_ENABLE__SHIFT                                                         0x12
#define ROM_SW_CNTL__DATA_SIZE_MASK                                                                           0x0000FFFFL
#define ROM_SW_CNTL__COMMAND_SIZE_MASK                                                                        0x00030000L
#define ROM_SW_CNTL__ROM_SW_RETURN_DATA_ENABLE_MASK                                                           0x00040000L
//ROM_SW_STATUS
#define ROM_SW_STATUS__ROM_SW_DONE__SHIFT                                                                     0x0
#define ROM_SW_STATUS__ROM_SW_DONE_MASK                                                                       0x00000001L
//ROM_SW_COMMAND
#define ROM_SW_COMMAND__ROM_SW_INSTRUCTION__SHIFT                                                             0x0
#define ROM_SW_COMMAND__ROM_SW_ADDRESS__SHIFT                                                                 0x8
#define ROM_SW_COMMAND__ROM_SW_INSTRUCTION_MASK                                                               0x000000FFL
#define ROM_SW_COMMAND__ROM_SW_ADDRESS_MASK                                                                   0xFFFFFF00L
//ROM_SW_DATA_1
#define ROM_SW_DATA_1__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_1__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_2
#define ROM_SW_DATA_2__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_2__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_3
#define ROM_SW_DATA_3__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_3__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_4
#define ROM_SW_DATA_4__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_4__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_5
#define ROM_SW_DATA_5__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_5__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_6
#define ROM_SW_DATA_6__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_6__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_7
#define ROM_SW_DATA_7__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_7__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_8
#define ROM_SW_DATA_8__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_8__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_9
#define ROM_SW_DATA_9__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_9__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_10
#define ROM_SW_DATA_10__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_10__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_11
#define ROM_SW_DATA_11__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_11__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_12
#define ROM_SW_DATA_12__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_12__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_13
#define ROM_SW_DATA_13__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_13__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_14
#define ROM_SW_DATA_14__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_14__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_15
#define ROM_SW_DATA_15__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_15__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_16
#define ROM_SW_DATA_16__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_16__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_17
#define ROM_SW_DATA_17__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_17__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_18
#define ROM_SW_DATA_18__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_18__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_19
#define ROM_SW_DATA_19__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_19__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_20
#define ROM_SW_DATA_20__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_20__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_21
#define ROM_SW_DATA_21__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_21__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_22
#define ROM_SW_DATA_22__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_22__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_23
#define ROM_SW_DATA_23__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_23__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_24
#define ROM_SW_DATA_24__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_24__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_25
#define ROM_SW_DATA_25__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_25__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_26
#define ROM_SW_DATA_26__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_26__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_27
#define ROM_SW_DATA_27__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_27__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_28
#define ROM_SW_DATA_28__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_28__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_29
#define ROM_SW_DATA_29__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_29__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_30
#define ROM_SW_DATA_30__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_30__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_31
#define ROM_SW_DATA_31__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_31__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_32
#define ROM_SW_DATA_32__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_32__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_33
#define ROM_SW_DATA_33__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_33__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_34
#define ROM_SW_DATA_34__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_34__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_35
#define ROM_SW_DATA_35__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_35__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_36
#define ROM_SW_DATA_36__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_36__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_37
#define ROM_SW_DATA_37__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_37__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_38
#define ROM_SW_DATA_38__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_38__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_39
#define ROM_SW_DATA_39__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_39__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_40
#define ROM_SW_DATA_40__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_40__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_41
#define ROM_SW_DATA_41__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_41__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_42
#define ROM_SW_DATA_42__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_42__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_43
#define ROM_SW_DATA_43__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_43__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_44
#define ROM_SW_DATA_44__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_44__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_45
#define ROM_SW_DATA_45__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_45__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_46
#define ROM_SW_DATA_46__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_46__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_47
#define ROM_SW_DATA_47__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_47__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_48
#define ROM_SW_DATA_48__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_48__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_49
#define ROM_SW_DATA_49__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_49__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_50
#define ROM_SW_DATA_50__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_50__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_51
#define ROM_SW_DATA_51__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_51__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_52
#define ROM_SW_DATA_52__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_52__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_53
#define ROM_SW_DATA_53__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_53__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_54
#define ROM_SW_DATA_54__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_54__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_55
#define ROM_SW_DATA_55__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_55__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_56
#define ROM_SW_DATA_56__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_56__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_57
#define ROM_SW_DATA_57__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_57__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_58
#define ROM_SW_DATA_58__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_58__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_59
#define ROM_SW_DATA_59__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_59__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_60
#define ROM_SW_DATA_60__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_60__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_61
#define ROM_SW_DATA_61__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_61__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_62
#define ROM_SW_DATA_62__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_62__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_63
#define ROM_SW_DATA_63__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_63__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_64
#define ROM_SW_DATA_64__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_64__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
/* SMUSVI0_PLANE0_CURRENTVID */
#define SMUSVI0_PLANE0_CURRENTVID__CURRENT_SVI0_PLANE0_VID__SHIFT                                             0x18
#define SMUSVI0_PLANE0_CURRENTVID__CURRENT_SVI0_PLANE0_VID_MASK                                               0xFF000000L

#endif
