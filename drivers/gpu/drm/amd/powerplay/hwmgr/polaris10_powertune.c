/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "hwmgr.h"
#include "smumgr.h"
#include "polaris10_hwmgr.h"
#include "polaris10_powertune.h"
#include "polaris10_smumgr.h"
#include "smu74_discrete.h"
#include "pp_debug.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "oss/oss_3_0_sh_mask.h"

#define VOLTAGE_SCALE  4
#define POWERTUNE_DEFAULT_SET_MAX    1

uint32_t DIDTBlock_Info = SQ_IR_MASK | TCP_IR_MASK | TD_PCC_MASK;

struct polaris10_pt_config_reg GCCACConfig_Polaris10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                Shift                                               Value       Type
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00060013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00860013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01060013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01860013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02060013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02860013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x03060013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x03860013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x04060013, POLARIS10_CONFIGREG_GC_CAC_IND },

	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x000E0013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x008E0013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x010E0013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x018E0013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x020E0013, POLARIS10_CONFIGREG_GC_CAC_IND },

	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00100013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00900013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01100013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01900013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02100013, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02900013, POLARIS10_CONFIGREG_GC_CAC_IND },

	{   0xFFFFFFFF  }
};

struct polaris10_pt_config_reg GCCACConfig_Polaris11[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                Shift                                               Value       Type
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00060011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00860011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01060011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01860011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02060011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02860011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x03060011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x03860011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x04060011, POLARIS10_CONFIGREG_GC_CAC_IND },

	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x000E0011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x008E0011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x010E0011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x018E0011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x020E0011, POLARIS10_CONFIGREG_GC_CAC_IND },

	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00100011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x00900011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01100011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x01900011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02100011, POLARIS10_CONFIGREG_GC_CAC_IND },
	{   ixGC_CAC_CNTL,                     0xFFFFFFFF,                                         0,                                                  0x02900011, POLARIS10_CONFIGREG_GC_CAC_IND },

	{   0xFFFFFFFF  }
};

struct polaris10_pt_config_reg DIDTConfig_Polaris10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                Shift                                               Value       Type
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT0_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT0__SHIFT,                  0x0073,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT1_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT1__SHIFT,                  0x00ab,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT2_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT2__SHIFT,                  0x0084,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT3_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT3__SHIFT,                  0x005a,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT4_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT4__SHIFT,                  0x0067,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT5_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT5__SHIFT,                  0x0084,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT6_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT6__SHIFT,                  0x0027,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT7_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT7__SHIFT,                  0x0046,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT8_MASK,                   DIDT_SQ_WEIGHT8_11__WEIGHT8__SHIFT,                 0x00aa,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT9_MASK,                   DIDT_SQ_WEIGHT8_11__WEIGHT9__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT10_MASK,                  DIDT_SQ_WEIGHT8_11__WEIGHT10__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT11_MASK,                  DIDT_SQ_WEIGHT8_11__WEIGHT11__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL1,                   DIDT_SQ_CTRL1__MIN_POWER_MASK,                      DIDT_SQ_CTRL1__MIN_POWER__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL1,                   DIDT_SQ_CTRL1__MAX_POWER_MASK,                      DIDT_SQ_CTRL1__MAX_POWER__SHIFT,                    0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL_OCP,                DIDT_SQ_CTRL_OCP__UNUSED_0_MASK,                    DIDT_SQ_CTRL_OCP__UNUSED_0__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL_OCP,                DIDT_SQ_CTRL_OCP__OCP_MAX_POWER_MASK,               DIDT_SQ_CTRL_OCP__OCP_MAX_POWER__SHIFT,             0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__MAX_POWER_DELTA_MASK,                DIDT_SQ_CTRL2__MAX_POWER_DELTA__SHIFT,              0x3853,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__UNUSED_0_MASK,                       DIDT_SQ_CTRL2__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,       DIDT_SQ_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,     0x005a,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__UNUSED_1_MASK,                       DIDT_SQ_CTRL2__UNUSED_1__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,       DIDT_SQ_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__UNUSED_2_MASK,                       DIDT_SQ_CTRL2__UNUSED_2__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_STALL_CTRL_ENABLE_MASK,    DIDT_SQ_STALL_CTRL__DIDT_STALL_CTRL_ENABLE__SHIFT,  0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,       DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,       DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_SQ_STALL_CTRL__DIDT_HI_POWER_THRESHOLD__SHIFT, 0x0ebb,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__UNUSED_0_MASK,                  DIDT_SQ_STALL_CTRL__UNUSED_0__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__DIDT_TUNING_ENABLE_MASK,       DIDT_SQ_TUNING_CTRL__DIDT_TUNING_ENABLE__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,       DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,     0x3853,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,       DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,     0x3153,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__UNUSED_0_MASK,                 DIDT_SQ_TUNING_CTRL__UNUSED_0__SHIFT,               0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK,                   DIDT_SQ_CTRL0__DIDT_CTRL_EN__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__USE_REF_CLOCK_MASK,                  DIDT_SQ_CTRL0__USE_REF_CLOCK__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__PHASE_OFFSET_MASK,                   DIDT_SQ_CTRL0__PHASE_OFFSET__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CTRL_RST_MASK,                  DIDT_SQ_CTRL0__DIDT_CTRL_RST__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,           DIDT_SQ_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,         0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI_MASK,     DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO_MASK,     DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__UNUSED_0_MASK,                       DIDT_SQ_CTRL0__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT0_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT0__SHIFT,                  0x000a,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT1_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT1__SHIFT,                  0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT2_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT2__SHIFT,                  0x0017,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT3_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT3__SHIFT,                  0x002f,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT4_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT4__SHIFT,                  0x0046,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT5_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT5__SHIFT,                  0x005d,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT6_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT6__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT7_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT7__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL1,                   DIDT_TD_CTRL1__MIN_POWER_MASK,                      DIDT_TD_CTRL1__MIN_POWER__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL1,                   DIDT_TD_CTRL1__MAX_POWER_MASK,                      DIDT_TD_CTRL1__MAX_POWER__SHIFT,                    0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL_OCP,                DIDT_TD_CTRL_OCP__UNUSED_0_MASK,                    DIDT_TD_CTRL_OCP__UNUSED_0__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL_OCP,                DIDT_TD_CTRL_OCP__OCP_MAX_POWER_MASK,               DIDT_TD_CTRL_OCP__OCP_MAX_POWER__SHIFT,             0x00ff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__MAX_POWER_DELTA_MASK,                DIDT_TD_CTRL2__MAX_POWER_DELTA__SHIFT,              0x3fff,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__UNUSED_0_MASK,                       DIDT_TD_CTRL2__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,       DIDT_TD_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,     0x000f,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__UNUSED_1_MASK,                       DIDT_TD_CTRL2__UNUSED_1__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,       DIDT_TD_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__UNUSED_2_MASK,                       DIDT_TD_CTRL2__UNUSED_2__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_STALL_CTRL_ENABLE_MASK,    DIDT_TD_STALL_CTRL__DIDT_STALL_CTRL_ENABLE__SHIFT,  0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,       DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,       DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_TD_STALL_CTRL__DIDT_HI_POWER_THRESHOLD__SHIFT, 0x01aa,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__UNUSED_0_MASK,                  DIDT_TD_STALL_CTRL__UNUSED_0__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__DIDT_TUNING_ENABLE_MASK,       DIDT_TD_TUNING_CTRL__DIDT_TUNING_ENABLE__SHIFT,     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,       DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,     0x0dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,       DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,     0x0dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__UNUSED_0_MASK,                 DIDT_TD_TUNING_CTRL__UNUSED_0__SHIFT,               0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK,                   DIDT_TD_CTRL0__DIDT_CTRL_EN__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__USE_REF_CLOCK_MASK,                  DIDT_TD_CTRL0__USE_REF_CLOCK__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__PHASE_OFFSET_MASK,                   DIDT_TD_CTRL0__PHASE_OFFSET__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CTRL_RST_MASK,                  DIDT_TD_CTRL0__DIDT_CTRL_RST__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,           DIDT_TD_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,         0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI_MASK,     DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,   0x0009,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO_MASK,     DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,   0x0009,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__UNUSED_0_MASK,                       DIDT_TD_CTRL0__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT0_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT0__SHIFT,                 0x0004,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT1_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT1__SHIFT,                 0x0037,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT2_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT2__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT3_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT3__SHIFT,                 0x00ff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT4_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT4__SHIFT,                 0x0054,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT5_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT5__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT6_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT6__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT7_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT7__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL1,                  DIDT_TCP_CTRL1__MIN_POWER_MASK,                     DIDT_TCP_CTRL1__MIN_POWER__SHIFT,                   0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL1,                  DIDT_TCP_CTRL1__MAX_POWER_MASK,                     DIDT_TCP_CTRL1__MAX_POWER__SHIFT,                   0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL_OCP,               DIDT_TCP_CTRL_OCP__UNUSED_0_MASK,                   DIDT_TCP_CTRL_OCP__UNUSED_0__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL_OCP,               DIDT_TCP_CTRL_OCP__OCP_MAX_POWER_MASK,              DIDT_TCP_CTRL_OCP__OCP_MAX_POWER__SHIFT,            0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__MAX_POWER_DELTA_MASK,               DIDT_TCP_CTRL2__MAX_POWER_DELTA__SHIFT,             0x3dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__UNUSED_0_MASK,                      DIDT_TCP_CTRL2__UNUSED_0__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,      DIDT_TCP_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,    0x0032,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__UNUSED_1_MASK,                      DIDT_TCP_CTRL2__UNUSED_1__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,      DIDT_TCP_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__UNUSED_2_MASK,                      DIDT_TCP_CTRL2__UNUSED_2__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_STALL_CTRL_ENABLE_MASK,   DIDT_TCP_STALL_CTRL__DIDT_STALL_CTRL_ENABLE__SHIFT, 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,      DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,    0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,      DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,    0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_HI_POWER_THRESHOLD_MASK,  DIDT_TCP_STALL_CTRL__DIDT_HI_POWER_THRESHOLD__SHIFT, 0x01aa,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__UNUSED_0_MASK,                 DIDT_TCP_STALL_CTRL__UNUSED_0__SHIFT,               0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__DIDT_TUNING_ENABLE_MASK,      DIDT_TCP_TUNING_CTRL__DIDT_TUNING_ENABLE__SHIFT,    0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,      DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,    0x3dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,      DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,    0x3dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__UNUSED_0_MASK,                DIDT_TCP_TUNING_CTRL__UNUSED_0__SHIFT,              0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK,                   DIDT_TCP_CTRL0__DIDT_CTRL_EN__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__USE_REF_CLOCK_MASK,                  DIDT_TCP_CTRL0__USE_REF_CLOCK__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__PHASE_OFFSET_MASK,                   DIDT_TCP_CTRL0__PHASE_OFFSET__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_CTRL_RST_MASK,                  DIDT_TCP_CTRL0__DIDT_CTRL_RST__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,           DIDT_TCP_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,         0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI_MASK,     DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO_MASK,     DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__UNUSED_0_MASK,                       DIDT_TCP_CTRL0__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   0xFFFFFFFF  }
};

struct polaris10_pt_config_reg DIDTConfig_Polaris11[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                Shift                                               Value       Type
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT0_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT0__SHIFT,                  0x0073,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT1_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT1__SHIFT,                  0x00ab,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT2_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT2__SHIFT,                  0x0084,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT0_3,               DIDT_SQ_WEIGHT0_3__WEIGHT3_MASK,                    DIDT_SQ_WEIGHT0_3__WEIGHT3__SHIFT,                  0x005a,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT4_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT4__SHIFT,                  0x0067,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT5_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT5__SHIFT,                  0x0084,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT6_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT6__SHIFT,                  0x0027,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT4_7,               DIDT_SQ_WEIGHT4_7__WEIGHT7_MASK,                    DIDT_SQ_WEIGHT4_7__WEIGHT7__SHIFT,                  0x0046,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT8_MASK,                   DIDT_SQ_WEIGHT8_11__WEIGHT8__SHIFT,                 0x00aa,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT9_MASK,                   DIDT_SQ_WEIGHT8_11__WEIGHT9__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT10_MASK,                  DIDT_SQ_WEIGHT8_11__WEIGHT10__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_WEIGHT8_11,              DIDT_SQ_WEIGHT8_11__WEIGHT11_MASK,                  DIDT_SQ_WEIGHT8_11__WEIGHT11__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL1,                   DIDT_SQ_CTRL1__MIN_POWER_MASK,                      DIDT_SQ_CTRL1__MIN_POWER__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL1,                   DIDT_SQ_CTRL1__MAX_POWER_MASK,                      DIDT_SQ_CTRL1__MAX_POWER__SHIFT,                    0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL_OCP,                DIDT_SQ_CTRL_OCP__UNUSED_0_MASK,                    DIDT_SQ_CTRL_OCP__UNUSED_0__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL_OCP,                DIDT_SQ_CTRL_OCP__OCP_MAX_POWER_MASK,               DIDT_SQ_CTRL_OCP__OCP_MAX_POWER__SHIFT,             0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__MAX_POWER_DELTA_MASK,                DIDT_SQ_CTRL2__MAX_POWER_DELTA__SHIFT,              0x3853,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__UNUSED_0_MASK,                       DIDT_SQ_CTRL2__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,       DIDT_SQ_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,     0x005a,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__UNUSED_1_MASK,                       DIDT_SQ_CTRL2__UNUSED_1__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,       DIDT_SQ_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL2,                   DIDT_SQ_CTRL2__UNUSED_2_MASK,                       DIDT_SQ_CTRL2__UNUSED_2__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_STALL_CTRL_ENABLE_MASK,    DIDT_SQ_STALL_CTRL__DIDT_STALL_CTRL_ENABLE__SHIFT,  0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,       DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,       DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_SQ_STALL_CTRL__DIDT_HI_POWER_THRESHOLD__SHIFT, 0x0ebb,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_STALL_CTRL,              DIDT_SQ_STALL_CTRL__UNUSED_0_MASK,                  DIDT_SQ_STALL_CTRL__UNUSED_0__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__DIDT_TUNING_ENABLE_MASK,       DIDT_SQ_TUNING_CTRL__DIDT_TUNING_ENABLE__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,       DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,     0x3853,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,       DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,     0x3153,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__UNUSED_0_MASK,                 DIDT_SQ_TUNING_CTRL__UNUSED_0__SHIFT,               0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK,                   DIDT_SQ_CTRL0__DIDT_CTRL_EN__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__USE_REF_CLOCK_MASK,                  DIDT_SQ_CTRL0__USE_REF_CLOCK__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__PHASE_OFFSET_MASK,                   DIDT_SQ_CTRL0__PHASE_OFFSET__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CTRL_RST_MASK,                  DIDT_SQ_CTRL0__DIDT_CTRL_RST__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,           DIDT_SQ_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,         0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI_MASK,     DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO_MASK,     DIDT_SQ_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__UNUSED_0_MASK,                       DIDT_SQ_CTRL0__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT0_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT0__SHIFT,                  0x000a,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT1_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT1__SHIFT,                  0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT2_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT2__SHIFT,                  0x0017,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT0_3,               DIDT_TD_WEIGHT0_3__WEIGHT3_MASK,                    DIDT_TD_WEIGHT0_3__WEIGHT3__SHIFT,                  0x002f,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT4_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT4__SHIFT,                  0x0046,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT5_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT5__SHIFT,                  0x005d,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT6_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT6__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_WEIGHT4_7,               DIDT_TD_WEIGHT4_7__WEIGHT7_MASK,                    DIDT_TD_WEIGHT4_7__WEIGHT7__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL1,                   DIDT_TD_CTRL1__MIN_POWER_MASK,                      DIDT_TD_CTRL1__MIN_POWER__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL1,                   DIDT_TD_CTRL1__MAX_POWER_MASK,                      DIDT_TD_CTRL1__MAX_POWER__SHIFT,                    0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL_OCP,                DIDT_TD_CTRL_OCP__UNUSED_0_MASK,                    DIDT_TD_CTRL_OCP__UNUSED_0__SHIFT,                  0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL_OCP,                DIDT_TD_CTRL_OCP__OCP_MAX_POWER_MASK,               DIDT_TD_CTRL_OCP__OCP_MAX_POWER__SHIFT,             0x00ff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__MAX_POWER_DELTA_MASK,                DIDT_TD_CTRL2__MAX_POWER_DELTA__SHIFT,              0x3fff,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__UNUSED_0_MASK,                       DIDT_TD_CTRL2__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,       DIDT_TD_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,     0x000f,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__UNUSED_1_MASK,                       DIDT_TD_CTRL2__UNUSED_1__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,       DIDT_TD_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL2,                   DIDT_TD_CTRL2__UNUSED_2_MASK,                       DIDT_TD_CTRL2__UNUSED_2__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_STALL_CTRL_ENABLE_MASK,    DIDT_TD_STALL_CTRL__DIDT_STALL_CTRL_ENABLE__SHIFT,  0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,       DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,       DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_TD_STALL_CTRL__DIDT_HI_POWER_THRESHOLD__SHIFT, 0x01aa,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_STALL_CTRL,              DIDT_TD_STALL_CTRL__UNUSED_0_MASK,                  DIDT_TD_STALL_CTRL__UNUSED_0__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__DIDT_TUNING_ENABLE_MASK,       DIDT_TD_TUNING_CTRL__DIDT_TUNING_ENABLE__SHIFT,     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,       DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,     0x0dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,       DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,     0x0dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__UNUSED_0_MASK,                 DIDT_TD_TUNING_CTRL__UNUSED_0__SHIFT,               0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK,                   DIDT_TD_CTRL0__DIDT_CTRL_EN__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__USE_REF_CLOCK_MASK,                  DIDT_TD_CTRL0__USE_REF_CLOCK__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__PHASE_OFFSET_MASK,                   DIDT_TD_CTRL0__PHASE_OFFSET__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CTRL_RST_MASK,                  DIDT_TD_CTRL0__DIDT_CTRL_RST__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,           DIDT_TD_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,         0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI_MASK,     DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,   0x0008,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO_MASK,     DIDT_TD_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,   0x0008,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__UNUSED_0_MASK,                       DIDT_TD_CTRL0__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT0_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT0__SHIFT,                 0x0004,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT1_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT1__SHIFT,                 0x0037,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT2_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT2__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT0_3,              DIDT_TCP_WEIGHT0_3__WEIGHT3_MASK,                   DIDT_TCP_WEIGHT0_3__WEIGHT3__SHIFT,                 0x00ff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT4_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT4__SHIFT,                 0x0054,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT5_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT5__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT6_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT6__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_WEIGHT4_7,              DIDT_TCP_WEIGHT4_7__WEIGHT7_MASK,                   DIDT_TCP_WEIGHT4_7__WEIGHT7__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL1,                  DIDT_TCP_CTRL1__MIN_POWER_MASK,                     DIDT_TCP_CTRL1__MIN_POWER__SHIFT,                   0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL1,                  DIDT_TCP_CTRL1__MAX_POWER_MASK,                     DIDT_TCP_CTRL1__MAX_POWER__SHIFT,                   0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL_OCP,               DIDT_TCP_CTRL_OCP__UNUSED_0_MASK,                   DIDT_TCP_CTRL_OCP__UNUSED_0__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL_OCP,               DIDT_TCP_CTRL_OCP__OCP_MAX_POWER_MASK,              DIDT_TCP_CTRL_OCP__OCP_MAX_POWER__SHIFT,            0xffff,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__MAX_POWER_DELTA_MASK,               DIDT_TCP_CTRL2__MAX_POWER_DELTA__SHIFT,             0x3dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__UNUSED_0_MASK,                      DIDT_TCP_CTRL2__UNUSED_0__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,      DIDT_TCP_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,    0x0032,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__UNUSED_1_MASK,                      DIDT_TCP_CTRL2__UNUSED_1__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,      DIDT_TCP_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL2,                  DIDT_TCP_CTRL2__UNUSED_2_MASK,                      DIDT_TCP_CTRL2__UNUSED_2__SHIFT,                    0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_STALL_CTRL_ENABLE_MASK,   DIDT_TCP_STALL_CTRL__DIDT_STALL_CTRL_ENABLE__SHIFT, 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,      DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,    0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,      DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,    0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__DIDT_HI_POWER_THRESHOLD_MASK,  DIDT_TCP_STALL_CTRL__DIDT_HI_POWER_THRESHOLD__SHIFT, 0x01aa,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_STALL_CTRL,             DIDT_TCP_STALL_CTRL__UNUSED_0_MASK,                 DIDT_TCP_STALL_CTRL__UNUSED_0__SHIFT,               0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__DIDT_TUNING_ENABLE_MASK,      DIDT_TCP_TUNING_CTRL__DIDT_TUNING_ENABLE__SHIFT,    0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,      DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,    0x3dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,      DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,    0x3dde,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__UNUSED_0_MASK,                DIDT_TCP_TUNING_CTRL__UNUSED_0__SHIFT,              0x0000,     POLARIS10_CONFIGREG_DIDT_IND },

	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK,                   DIDT_TCP_CTRL0__DIDT_CTRL_EN__SHIFT,                 0x0001,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__USE_REF_CLOCK_MASK,                  DIDT_TCP_CTRL0__USE_REF_CLOCK__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__PHASE_OFFSET_MASK,                   DIDT_TCP_CTRL0__PHASE_OFFSET__SHIFT,                 0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_CTRL_RST_MASK,                  DIDT_TCP_CTRL0__DIDT_CTRL_RST__SHIFT,                0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,           DIDT_TCP_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,         0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI_MASK,     DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO_MASK,     DIDT_TCP_CTRL0__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,   0x0010,     POLARIS10_CONFIGREG_DIDT_IND },
	{   ixDIDT_TCP_CTRL0,                   DIDT_TCP_CTRL0__UNUSED_0_MASK,                       DIDT_TCP_CTRL0__UNUSED_0__SHIFT,                     0x0000,     POLARIS10_CONFIGREG_DIDT_IND },
	{   0xFFFFFFFF  }
};

static const struct polaris10_pt_defaults polaris10_power_tune_data_set_array[POWERTUNE_DEFAULT_SET_MAX] = {
	/* sviLoadLIneEn, SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt,
	 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac, BAPM_TEMP_GRADIENT */
	{ 1, 0xF, 0xFD, 0x19, 5, 45, 0, 0xB0000,
	{ 0x79, 0x253, 0x25D, 0xAE, 0x72, 0x80, 0x83, 0x86, 0x6F, 0xC8, 0xC9, 0xC9, 0x2F, 0x4D, 0x61},
	{ 0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203, 0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4 } },
};

void polaris10_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *polaris10_hwmgr = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct  phm_ppt_v1_information *table_info =
			(struct  phm_ppt_v1_information *)(hwmgr->pptable);

	if (table_info &&
			table_info->cac_dtp_table->usPowerTuneDataSetID <= POWERTUNE_DEFAULT_SET_MAX &&
			table_info->cac_dtp_table->usPowerTuneDataSetID)
		polaris10_hwmgr->power_tune_defaults =
				&polaris10_power_tune_data_set_array
				[table_info->cac_dtp_table->usPowerTuneDataSetID - 1];
	else
		polaris10_hwmgr->power_tune_defaults = &polaris10_power_tune_data_set_array[0];

}

static uint16_t scale_fan_gain_settings(uint16_t raw_setting)
{
	uint32_t tmp;
	tmp = raw_setting * 4096 / 100;
	return (uint16_t)tmp;
}

int polaris10_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_pt_defaults *defaults = data->power_tune_defaults;
	SMU74_Discrete_DpmTable  *dpm_table = &(data->smc_state_table);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_dtp_table = table_info->cac_dtp_table;
	struct pp_advance_fan_control_parameters *fan_table=
			&hwmgr->thermal_controller.advanceFanControlParameters;
	int i, j, k;
	const uint16_t *pdef1;
	const uint16_t *pdef2;

	dpm_table->DefaultTdp = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usTDP * 128));
	dpm_table->TargetTdp  = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usTDP * 128));

	PP_ASSERT_WITH_CODE(cac_dtp_table->usTargetOperatingTemp <= 255,
				"Target Operating Temp is out of Range!",
				);

	dpm_table->TemperatureLimitEdge = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTargetOperatingTemp * 256);
	dpm_table->TemperatureLimitHotspot = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitHotspot * 256);
	dpm_table->FanGainEdge = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainEdge));
	dpm_table->FanGainHotspot = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainHotspot));

	pdef1 = defaults->BAPMTI_R;
	pdef2 = defaults->BAPMTI_RC;

	for (i = 0; i < SMU74_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU74_DTE_SOURCES; j++) {
			for (k = 0; k < SMU74_DTE_SINKS; k++) {
				dpm_table->BAPMTI_R[i][j][k] = PP_HOST_TO_SMC_US(*pdef1);
				dpm_table->BAPMTI_RC[i][j][k] = PP_HOST_TO_SMC_US(*pdef2);
				pdef1++;
				pdef2++;
			}
		}
	}

	return 0;
}

static int polaris10_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_pt_defaults *defaults = data->power_tune_defaults;

	data->power_tune_table.SviLoadLineEn = defaults->SviLoadLineEn;
	data->power_tune_table.SviLoadLineVddC = defaults->SviLoadLineVddC;
	data->power_tune_table.SviLoadLineTrimVddC = 3;
	data->power_tune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int polaris10_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	const struct polaris10_pt_defaults *defaults = data->power_tune_defaults;

	tdc_limit = (uint16_t)(table_info->cac_dtp_table->usTDC * 128);
	data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->TDC_VDDC_ThrottleReleaseLimitPerc;
	data->power_tune_table.TDC_MAWt = defaults->TDC_MAWt;

	return 0;
}

static int polaris10_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_pt_defaults *defaults = data->power_tune_defaults;
	uint32_t temp;

	if (polaris10_read_smc_sram_dword(hwmgr->smumgr,
			fuse_table_offset +
			offsetof(SMU74_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, data->sram_end))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 (SviLoadLineEn) from SMC Failed!",
				return -EINVAL);
	else {
		data->power_tune_table.TdcWaterfallCtl = defaults->TdcWaterfallCtl;
		data->power_tune_table.LPMLTemperatureMin =
				(uint8_t)((temp >> 16) & 0xff);
		data->power_tune_table.LPMLTemperatureMax =
				(uint8_t)((temp >> 8) & 0xff);
		data->power_tune_table.Reserved = (uint8_t)(temp & 0xff);
	}
	return 0;
}

static int polaris10_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	int i;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		data->power_tune_table.LPMLTemperatureScaler[i] = 0;

	return 0;
}

static int polaris10_populate_fuzzy_fan(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if ((hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity & (1 << 15))
		|| 0 == hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity)
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity =
			hwmgr->thermal_controller.advanceFanControlParameters.usDefaultFanOutputSensitivity;

	data->power_tune_table.FuzzyFan_PwmSetDelta = PP_HOST_TO_SMC_US(
				hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity);
	return 0;
}

static int polaris10_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int polaris10_min_max_vgnb_lpml_id_from_bapm_vddc(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int polaris10_enable_didt(struct pp_hwmgr *hwmgr, const bool enable)
{

	uint32_t en = enable ? 1 : 0;
	int32_t result = 0;
	uint32_t data;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SQRamping)) {
		data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_SQ_CTRL0);
		data &= ~DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK;
		data |= ((en << DIDT_SQ_CTRL0__DIDT_CTRL_EN__SHIFT) & DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_SQ_CTRL0, data);
		DIDTBlock_Info &= ~SQ_Enable_MASK;
		DIDTBlock_Info |= en << SQ_Enable_SHIFT;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DBRamping)) {
		data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_DB_CTRL0);
		data &= ~DIDT_DB_CTRL0__DIDT_CTRL_EN_MASK;
		data |= ((en << DIDT_DB_CTRL0__DIDT_CTRL_EN__SHIFT) & DIDT_DB_CTRL0__DIDT_CTRL_EN_MASK);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_DB_CTRL0, data);
		DIDTBlock_Info &= ~DB_Enable_MASK;
		DIDTBlock_Info |= en << DB_Enable_SHIFT;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TDRamping)) {
		data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TD_CTRL0);
		data &= ~DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK;
		data |= ((en << DIDT_TD_CTRL0__DIDT_CTRL_EN__SHIFT) & DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TD_CTRL0, data);
		DIDTBlock_Info &= ~TD_Enable_MASK;
		DIDTBlock_Info |= en << TD_Enable_SHIFT;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TCPRamping)) {
		data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TCP_CTRL0);
		data &= ~DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK;
		data |= ((en << DIDT_TCP_CTRL0__DIDT_CTRL_EN__SHIFT) & DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TCP_CTRL0, data);
		DIDTBlock_Info &= ~TCP_Enable_MASK;
		DIDTBlock_Info |= en << TCP_Enable_SHIFT;
	}

	if (enable)
		result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_Didt_Block_Function, DIDTBlock_Info);

	return result;
}

static int polaris10_program_pt_config_registers(struct pp_hwmgr *hwmgr,
				struct polaris10_pt_config_reg *cac_config_regs)
{
	struct polaris10_pt_config_reg *config_regs = cac_config_regs;
	uint32_t cache = 0;
	uint32_t data = 0;

	PP_ASSERT_WITH_CODE((config_regs != NULL), "Invalid config register table.", return -EINVAL);

	while (config_regs->offset != 0xFFFFFFFF) {
		if (config_regs->type == POLARIS10_CONFIGREG_CACHE)
			cache |= ((config_regs->value << config_regs->shift) & config_regs->mask);
		else {
			switch (config_regs->type) {
			case POLARIS10_CONFIGREG_SMC_IND:
				data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, config_regs->offset);
				break;

			case POLARIS10_CONFIGREG_DIDT_IND:
				data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, config_regs->offset);
				break;

			case POLARIS10_CONFIGREG_GC_CAC_IND:
				data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG_GC_CAC, config_regs->offset);
				break;

			default:
				data = cgs_read_register(hwmgr->device, config_regs->offset);
				break;
			}

			data &= ~config_regs->mask;
			data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
			data |= cache;

			switch (config_regs->type) {
			case POLARIS10_CONFIGREG_SMC_IND:
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, config_regs->offset, data);
				break;

			case POLARIS10_CONFIGREG_DIDT_IND:
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, config_regs->offset, data);
				break;

			case POLARIS10_CONFIGREG_GC_CAC_IND:
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG_GC_CAC, config_regs->offset, data);
				break;

			default:
				cgs_write_register(hwmgr->device, config_regs->offset, data);
				break;
			}
			cache = 0;
		}

		config_regs++;
	}

	return 0;
}

int polaris10_enable_didt_config(struct pp_hwmgr *hwmgr)
{
	int result;
	uint32_t num_se = 0;
	uint32_t count, value, value2;
	struct cgs_system_info sys_info = {0};

	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_GFX_SE_INFO;
	result = cgs_query_system_info(hwmgr->device, &sys_info);


	if (result == 0)
		num_se = sys_info.value;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SQRamping) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DBRamping) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TDRamping) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TCPRamping)) {

		/* TO DO Pre DIDT disable clock gating */
		value = 0;
		value2 = cgs_read_register(hwmgr->device, mmGRBM_GFX_INDEX);
		for (count = 0; count < num_se; count++) {
			value = SYS_GRBM_GFX_INDEX_DATA__INSTANCE_BROADCAST_WRITES_MASK
				| SYS_GRBM_GFX_INDEX_DATA__SH_BROADCAST_WRITES_MASK
				| (count << SYS_GRBM_GFX_INDEX_DATA__SE_INDEX__SHIFT);
			cgs_write_register(hwmgr->device, mmGRBM_GFX_INDEX, value);

			if (hwmgr->chip_id == CHIP_POLARIS10) {
				result = polaris10_program_pt_config_registers(hwmgr, GCCACConfig_Polaris10);
				PP_ASSERT_WITH_CODE((result == 0), "DIDT Config failed.", return result);
				result = polaris10_program_pt_config_registers(hwmgr, DIDTConfig_Polaris10);
				PP_ASSERT_WITH_CODE((result == 0), "DIDT Config failed.", return result);
			} else if (hwmgr->chip_id == CHIP_POLARIS11) {
				result = polaris10_program_pt_config_registers(hwmgr, GCCACConfig_Polaris11);
				PP_ASSERT_WITH_CODE((result == 0), "DIDT Config failed.", return result);
				result = polaris10_program_pt_config_registers(hwmgr, DIDTConfig_Polaris11);
				PP_ASSERT_WITH_CODE((result == 0), "DIDT Config failed.", return result);
			}
		}
		cgs_write_register(hwmgr->device, mmGRBM_GFX_INDEX, value2);

		result = polaris10_enable_didt(hwmgr, true);
		PP_ASSERT_WITH_CODE((result == 0), "EnableDiDt failed.", return result);

		/* TO DO Post DIDT enable clock gating */
	}

	return 0;
}

int polaris10_disable_didt_config(struct pp_hwmgr *hwmgr)
{
	int result;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SQRamping) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DBRamping) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TDRamping) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TCPRamping)) {
		/* TO DO Pre DIDT disable clock gating */

		result = polaris10_enable_didt(hwmgr, false);
		PP_ASSERT_WITH_CODE((result == 0), "Post DIDT enable clock gating failed.", return result);
		/* TO DO Post DIDT enable clock gating */
	}

	return 0;
}


static int polaris10_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint16_t hi_sidd = data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t lo_sidd = data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;

	hi_sidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	lo_sidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(hi_sidd);
	data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(lo_sidd);

	return 0;
}

int polaris10_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (polaris10_read_smc_sram_dword(hwmgr->smumgr,
				SMU7_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU74_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to get pm_fuse_table_offset Failed!",
					return -EINVAL);

		if (polaris10_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate SviLoadLine Failed!",
					return -EINVAL);

		if (polaris10_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed!", return -EINVAL);

		if (polaris10_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TdcWaterfallCtl, "
					"LPMLTemperature Min and Max Failed!",
					return -EINVAL);

		if (0 != polaris10_populate_temperature_scaler(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate LPMLTemperatureScaler Failed!",
					return -EINVAL);

		if (polaris10_populate_fuzzy_fan(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate Fuzzy Fan Control parameters Failed!",
					return -EINVAL);

		if (polaris10_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Failed!",
					return -EINVAL);

		if (polaris10_min_max_vgnb_lpml_id_from_bapm_vddc(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Min and Max Vid Failed!",
					return -EINVAL);

		if (polaris10_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate BapmVddCBaseLeakage Hi and Lo "
					"Sidd Failed!", return -EINVAL);

		if (polaris10_copy_bytes_to_smc(hwmgr->smumgr, pm_fuse_table_offset,
				(uint8_t *)&data->power_tune_table,
				(sizeof(struct SMU74_Discrete_PmFuses) - 92), data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed!",
					return -EINVAL);
	}
	return 0;
}

int polaris10_enable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC)) {
		int smc_result;
		smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
				(uint16_t)(PPSMC_MSG_EnableCac));
		PP_ASSERT_WITH_CODE((0 == smc_result),
				"Failed to enable CAC in SMC.", result = -1);

		data->cac_enabled = (0 == smc_result) ? true : false;
	}
	return result;
}

int polaris10_disable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC) && data->cac_enabled) {
		int smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
				(uint16_t)(PPSMC_MSG_DisableCac));
		PP_ASSERT_WITH_CODE((smc_result == 0),
				"Failed to disable CAC in SMC.", result = -1);

		data->cac_enabled = false;
	}
	return result;
}

int polaris10_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (data->power_containment_features &
			POWERCONTAINMENT_FEATURE_PkgPwrLimit)
		return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_PkgPwrSetLimit, n);
	return 0;
}

static int polaris10_set_overdriver_target_tdp(struct pp_hwmgr *pHwMgr, uint32_t target_tdp)
{
	return smum_send_msg_to_smc_with_parameter(pHwMgr->smumgr,
			PPSMC_MSG_OverDriveSetTargetTdp, target_tdp);
}

int polaris10_enable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int smc_result;
	int result = 0;

	data->power_containment_features = 0;
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {

		if (data->enable_tdc_limit_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_TDCLimitEnable));
			PP_ASSERT_WITH_CODE((0 == smc_result),
					"Failed to enable TDCLimit in SMC.", result = -1;);
			if (0 == smc_result)
				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_TDCLimit;
		}

		if (data->enable_pkg_pwr_tracking_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_PkgPwrLimitEnable));
			PP_ASSERT_WITH_CODE((0 == smc_result),
					"Failed to enable PkgPwrTracking in SMC.", result = -1;);
			if (0 == smc_result) {
				struct phm_cac_tdp_table *cac_table =
						table_info->cac_dtp_table;
				uint32_t default_limit =
					(uint32_t)(cac_table->usMaximumPowerDeliveryLimit * 256);

				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_PkgPwrLimit;

				if (polaris10_set_power_limit(hwmgr, default_limit))
					printk(KERN_ERR "Failed to set Default Power Limit in SMC!");
			}
		}
	}
	return result;
}

int polaris10_disable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment) &&
			data->power_containment_features) {
		int smc_result;

		if (data->power_containment_features &
				POWERCONTAINMENT_FEATURE_TDCLimit) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_TDCLimitDisable));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to disable TDCLimit in SMC.",
					result = smc_result);
		}

		if (data->power_containment_features &
				POWERCONTAINMENT_FEATURE_DTE) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_DisableDTE));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to disable DTE in SMC.",
					result = smc_result);
		}

		if (data->power_containment_features &
				POWERCONTAINMENT_FEATURE_PkgPwrLimit) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_PkgPwrLimitDisable));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to disable PkgPwrTracking in SMC.",
					result = smc_result);
		}
		data->power_containment_features = 0;
	}

	return result;
}

int polaris10_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;
	int adjust_percent, target_tdp;
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		/* adjustment percentage has already been validated */
		adjust_percent = hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		/* SMC requested that target_tdp to be 7 bit fraction in DPM table
		 * but message to be 8 bit fraction for messages
		 */
		target_tdp = ((100 + adjust_percent) * (int)(cac_table->usTDP * 256)) / 100;
		result = polaris10_set_overdriver_target_tdp(hwmgr, (uint32_t)target_tdp);
	}

	return result;
}
