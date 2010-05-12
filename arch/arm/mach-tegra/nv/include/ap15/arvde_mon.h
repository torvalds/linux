/*
 * Copyright (c) 2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef ___ARVDE_MON_H_INC_
#define ___ARVDE_MON_H_INC_
//----------------------------------------------------------
// PPB, IDLE, & debug-observability
// --------------------------------------------------
// PPB, IDLE, & debug-observability registers in VDE
// --------------------------------------------------
// This IDLE monitor is intended to count the number of cycles where
// all of the NV_VDE_<submodule>'s are all idle. This information is
// expected to be used by software to adjust the system clock and video
// clock to optimal values.

// Register ARVDE_PPB_IDLE_MON_0  
#define ARVDE_PPB_IDLE_MON_0                    _MK_ADDR_CONST(0x2800)
#define ARVDE_PPB_IDLE_MON_0_WORD_COUNT                         0x1
#define ARVDE_PPB_IDLE_MON_0_RESET_VAL                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_RESET_MASK                         _MK_MASK_CONST(0xbfffffff)
#define ARVDE_PPB_IDLE_MON_0_SW_DEFAULT_VAL                     _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_SW_DEFAULT_MASK                    _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_READ_MASK                  _MK_MASK_CONST(0xbfffffff)
#define ARVDE_PPB_IDLE_MON_0_WRITE_MASK                         _MK_MASK_CONST(0xbfffffff)
// read=1 means monitoring active. read=0 means monitoring inactive
// write1 means start monitoring. write0 means stop monitoring.
// monitor will also become inactive automatically if either
// 1. sample period ends, or
// 2. overflow is reached (in either indefinite sampling or sample-period mode).
#define ARVDE_PPB_IDLE_MON_0_ENB_SHIFT                  _MK_SHIFT_CONST(31)
#define ARVDE_PPB_IDLE_MON_0_ENB_FIELD                  (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_MON_0_ENB_SHIFT)
#define ARVDE_PPB_IDLE_MON_0_ENB_RANGE                  31:31
#define ARVDE_PPB_IDLE_MON_0_ENB_WOFFSET                        0x0
#define ARVDE_PPB_IDLE_MON_0_ENB_DEFAULT                        _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_ENB_DEFAULT_MASK                   _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_MON_0_ENB_SW_DEFAULT                     _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_ENB_SW_DEFAULT_MASK                        _MK_MASK_CONST(0x0)

// read=1 means monitoring transitioned to inactive automically
// because of cause 1 or 2 above.
// write1 means clear this interrupt bit. write0 is ignored
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_SHIFT                   _MK_SHIFT_CONST(29)
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_FIELD                   (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_MON_0_INT_STATUS_SHIFT)
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_RANGE                   29:29
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_WOFFSET                 0x0
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_DEFAULT                 _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_DEFAULT_MASK                    _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_SW_DEFAULT                      _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_INT_STATUS_SW_DEFAULT_MASK                 _MK_MASK_CONST(0x0)

// 1 means indefinite/continous sampling
// 0 means use SAMPLE_PERIOD for duration
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_SHIFT                  _MK_SHIFT_CONST(28)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_FIELD                  (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_SHIFT)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_RANGE                  28:28
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_WOFFSET                        0x0
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_DEFAULT                        _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_DEFAULT_MASK                   _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_SW_DEFAULT                     _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_MODE_SW_DEFAULT_MASK                        _MK_MASK_CONST(0x0)

// sample period in clock cycles. implemented as n+1, so that
// "0" means sample period is 1 clock cycle
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_SHIFT                        _MK_SHIFT_CONST(3)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_FIELD                        (_MK_MASK_CONST(0x1ffffff) << ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_SHIFT)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_RANGE                        27:3
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_WOFFSET                      0x0
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_DEFAULT                      _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_DEFAULT_MASK                 _MK_MASK_CONST(0x1ffffff)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_SW_DEFAULT                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_SAMPLE_PERIOD_SW_DEFAULT_MASK                      _MK_MASK_CONST(0x0)

// power-of-2 encoding for # of required continously active IDLE cycles
// before counting will start. 0 means don't use thresh, just count directly.
// 1 means start counting after 1 continuous idle cycle  has  been observed. (if idle active for 10 clocks, count would be 9)
// 2 means start counting after 2 continuous idle cycles have been observed. (if idle active for 10 clocks, count would be 8)
// 3 means start counting after 4 continuous idle cycles have been observed. (if idle active for 10 clocks, count would be 6)
#define ARVDE_PPB_IDLE_MON_0_THRESH_SHIFT                       _MK_SHIFT_CONST(0)
#define ARVDE_PPB_IDLE_MON_0_THRESH_FIELD                       (_MK_MASK_CONST(0x7) << ARVDE_PPB_IDLE_MON_0_THRESH_SHIFT)
#define ARVDE_PPB_IDLE_MON_0_THRESH_RANGE                       2:0
#define ARVDE_PPB_IDLE_MON_0_THRESH_WOFFSET                     0x0
#define ARVDE_PPB_IDLE_MON_0_THRESH_DEFAULT                     _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_THRESH_DEFAULT_MASK                        _MK_MASK_CONST(0x7)
#define ARVDE_PPB_IDLE_MON_0_THRESH_SW_DEFAULT                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_MON_0_THRESH_SW_DEFAULT_MASK                     _MK_MASK_CONST(0x0)


// Register ARVDE_PPB_IDLE_STATUS_0  
#define ARVDE_PPB_IDLE_STATUS_0                 _MK_ADDR_CONST(0x2804)
#define ARVDE_PPB_IDLE_STATUS_0_WORD_COUNT                      0x1
#define ARVDE_PPB_IDLE_STATUS_0_RESET_VAL                       _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_STATUS_0_RESET_MASK                      _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_STATUS_0_SW_DEFAULT_VAL                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_STATUS_0_SW_DEFAULT_MASK                         _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_STATUS_0_READ_MASK                       _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_STATUS_0_WRITE_MASK                      _MK_MASK_CONST(0x0)
// # of cycles of idle observed. value of 0xFFFF.FFFF indicates overflow
// condition. COUNT will not stay at 0xFFFF.FFFF once overflow has been
// detected. Value is cleared to 0 whenever VDE_IDLE_MON.ENB field is
// written to 1 (see above register)
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_SHIFT                     _MK_SHIFT_CONST(0)
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_FIELD                     (_MK_MASK_CONST(0xffffffff) << ARVDE_PPB_IDLE_STATUS_0_COUNT_SHIFT)
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_RANGE                     31:0
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_WOFFSET                   0x0
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_DEFAULT                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_DEFAULT_MASK                      _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_SW_DEFAULT                        _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_STATUS_0_COUNT_SW_DEFAULT_MASK                   _MK_MASK_CONST(0x0)


// Reserved address 10248 [0x2808] 

// Reserved address 10252 [0x280c] 
// This submodule IDLE monitor is intended measure the activity/idle status of a single selected VDE_<submodule>. 
// Software can use these registers to measure the effectiveness of hardware controlled dynamic clock-enable
// power-gating, or to profile submodule activity during a particular video stream or set of streams.

// Register ARVDE_PPB_IDLE_SUBMOD_MON_0  
#define ARVDE_PPB_IDLE_SUBMOD_MON_0                     _MK_ADDR_CONST(0x2810)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_WORD_COUNT                  0x1
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_RESET_VAL                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_RESET_MASK                  _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SW_DEFAULT_VAL                      _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SW_DEFAULT_MASK                     _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_READ_MASK                   _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_WRITE_MASK                  _MK_MASK_CONST(0xffffffff)
// read=1 means monitoring active. read=0 means monitoring inactive
// write1 means start monitoring. write0 means stop monitoring.
// monitor will also become inactive automatically if either
// 1. sample period ends, or
// 2. overflow is reached in indefinite sampling mode.
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_SHIFT                   _MK_SHIFT_CONST(31)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_FIELD                   (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_RANGE                   31:31
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_WOFFSET                 0x0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_DEFAULT                 _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_DEFAULT_MASK                    _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_SW_DEFAULT                      _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_ENB_SW_DEFAULT_MASK                 _MK_MASK_CONST(0x0)

// AND'd with INT_STATUS below for passing interrupt signal. 0=mask, 1=enable
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_SHIFT                       _MK_SHIFT_CONST(30)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_FIELD                       (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_RANGE                       30:30
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_WOFFSET                     0x0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_DEFAULT                     _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_DEFAULT_MASK                        _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_SW_DEFAULT                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_ENB_SW_DEFAULT_MASK                     _MK_MASK_CONST(0x0)

// read=1 means monitoring transitioned to inactive automically
// because of one of the two causes above.
// write1 means clear this interrupt bit. write0 is ignored
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_SHIFT                    _MK_SHIFT_CONST(29)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_FIELD                    (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_RANGE                    29:29
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_WOFFSET                  0x0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_DEFAULT                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_DEFAULT_MASK                     _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_SW_DEFAULT                       _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_INT_STATUS_SW_DEFAULT_MASK                  _MK_MASK_CONST(0x0)

// 1 means indefinite/continous sampling
// 0 means use SAMPLE_PERIOD for duration
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_SHIFT                   _MK_SHIFT_CONST(28)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_FIELD                   (_MK_MASK_CONST(0x1) << ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_RANGE                   28:28
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_WOFFSET                 0x0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_DEFAULT                 _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_DEFAULT_MASK                    _MK_MASK_CONST(0x1)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_SW_DEFAULT                      _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_MODE_SW_DEFAULT_MASK                 _MK_MASK_CONST(0x0)

// sample period in clock cycles. implemented as n+1, so that
// "0" means sample period is 1 clock cycle
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_SHIFT                 _MK_SHIFT_CONST(3)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_FIELD                 (_MK_MASK_CONST(0x1ffffff) << ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_RANGE                 27:3
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_WOFFSET                       0x0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_DEFAULT                       _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_DEFAULT_MASK                  _MK_MASK_CONST(0x1ffffff)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_SW_DEFAULT                    _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_SAMPLE_PERIOD_SW_DEFAULT_MASK                       _MK_MASK_CONST(0x0)

// power-of-2 encoding for # of required continously active IDLE cycles
// before counting will start.
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_SHIFT                        _MK_SHIFT_CONST(0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_FIELD                        (_MK_MASK_CONST(0x7) << ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_RANGE                        2:0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_WOFFSET                      0x0
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_DEFAULT                      _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_DEFAULT_MASK                 _MK_MASK_CONST(0x7)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_SW_DEFAULT                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_MON_0_THRESH_SW_DEFAULT_MASK                      _MK_MASK_CONST(0x0)


// Register ARVDE_PPB_IDLE_SUBMOD_STATUS_0  
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0                  _MK_ADDR_CONST(0x2814)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_WORD_COUNT                       0x1
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_RESET_VAL                        _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_RESET_MASK                       _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_SW_DEFAULT_VAL                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_SW_DEFAULT_MASK                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_READ_MASK                        _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_WRITE_MASK                       _MK_MASK_CONST(0x0)
// # of cycles of idle observed. value of 0xFFFF.FFFF indicates overflow
// condition. COUNT will not stay at 0xFFFF.FFFF once overflow has been
// detected. Value is cleared to 0 whenever VDE_IDLE_MON.ENB field is
// written to 1 (see above register)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_SHIFT                      _MK_SHIFT_CONST(0)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_FIELD                      (_MK_MASK_CONST(0xffffffff) << ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_RANGE                      31:0
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_WOFFSET                    0x0
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_DEFAULT                    _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_DEFAULT_MASK                       _MK_MASK_CONST(0xffffffff)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_SW_DEFAULT                 _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_STATUS_0_COUNT_SW_DEFAULT_MASK                    _MK_MASK_CONST(0x0)


// Register ARVDE_PPB_IDLE_SUBMOD_SELECT_0  
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0                  _MK_ADDR_CONST(0x2818)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_WORD_COUNT                       0x1
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_RESET_VAL                        _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_RESET_MASK                       _MK_MASK_CONST(0x7)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_SW_DEFAULT_VAL                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_SW_DEFAULT_MASK                  _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_READ_MASK                        _MK_MASK_CONST(0x7)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_WRITE_MASK                       _MK_MASK_CONST(0x0)
// 0=SXE, 1=BSEV, 2=TFE, 3=MBE, 4=MCE, 5=PPE, others=RESERVED
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_SHIFT                     _MK_SHIFT_CONST(0)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_FIELD                     (_MK_MASK_CONST(0x7) << ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_SHIFT)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_RANGE                     2:0
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_WOFFSET                   0x0
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_DEFAULT                   _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_DEFAULT_MASK                      _MK_MASK_CONST(0x7)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_SW_DEFAULT                        _MK_MASK_CONST(0x0)
#define ARVDE_PPB_IDLE_SUBMOD_SELECT_0_MODULE_SW_DEFAULT_MASK                   _MK_MASK_CONST(0x0)

#endif // ifndef ___ARVDE_MON_H_INC_

