/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "vega10_hwmgr.h"
#include "vega10_smumgr.h"
#include "vega10_powertune.h"
#include "vega10_ppsmc.h"
#include "vega10_inc.h"
#include "pp_debug.h"
#include "soc15_common.h"

static const struct vega10_didt_config_reg SEDiDtTuningCtrlConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ */
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,        DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,        0x3853 },
	{   ixDIDT_SQ_TUNING_CTRL,             DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,        DIDT_SQ_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,        0x3153 },

	/* DIDT_TD */
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,        DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,        0x0dde },
	{   ixDIDT_TD_TUNING_CTRL,             DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,        DIDT_TD_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,        0x0dde },

	/* DIDT_TCP */
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,       DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,       0x3dde },
	{   ixDIDT_TCP_TUNING_CTRL,            DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,       DIDT_TCP_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,       0x3dde },

	/* DIDT_DB */
	{   ixDIDT_DB_TUNING_CTRL,             DIDT_DB_TUNING_CTRL__MAX_POWER_DELTA_HI_MASK,        DIDT_DB_TUNING_CTRL__MAX_POWER_DELTA_HI__SHIFT,        0x3dde },
	{   ixDIDT_DB_TUNING_CTRL,             DIDT_DB_TUNING_CTRL__MAX_POWER_DELTA_LO_MASK,        DIDT_DB_TUNING_CTRL__MAX_POWER_DELTA_LO__SHIFT,        0x3dde },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEDiDtCtrl3Config_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset               Mask                                                     Shift                                                            Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/*DIDT_SQ_CTRL3 */
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__GC_DIDT_ENABLE_MASK,       DIDT_SQ_CTRL3__GC_DIDT_ENABLE__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__GC_DIDT_CLK_EN_OVERRIDE_MASK,       DIDT_SQ_CTRL3__GC_DIDT_CLK_EN_OVERRIDE__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__THROTTLE_POLICY_MASK,       DIDT_SQ_CTRL3__THROTTLE_POLICY__SHIFT,             0x0003 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT_MASK,       DIDT_SQ_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__DIDT_POWER_LEVEL_LOWBIT_MASK,       DIDT_SQ_CTRL3__DIDT_POWER_LEVEL_LOWBIT__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS_MASK,       DIDT_SQ_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS__SHIFT,             0x0003 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__GC_DIDT_LEVEL_COMB_EN_MASK,       DIDT_SQ_CTRL3__GC_DIDT_LEVEL_COMB_EN__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__SE_DIDT_LEVEL_COMB_EN_MASK,       DIDT_SQ_CTRL3__SE_DIDT_LEVEL_COMB_EN__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__QUALIFY_STALL_EN_MASK,       DIDT_SQ_CTRL3__QUALIFY_STALL_EN__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__DIDT_STALL_SEL_MASK,       DIDT_SQ_CTRL3__DIDT_STALL_SEL__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__DIDT_FORCE_STALL_MASK,       DIDT_SQ_CTRL3__DIDT_FORCE_STALL__SHIFT,             0x0000 },
	{   ixDIDT_SQ_CTRL3,     DIDT_SQ_CTRL3__DIDT_STALL_DELAY_EN_MASK,       DIDT_SQ_CTRL3__DIDT_STALL_DELAY_EN__SHIFT,             0x0000 },

	/*DIDT_TCP_CTRL3 */
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__GC_DIDT_ENABLE_MASK,      DIDT_TCP_CTRL3__GC_DIDT_ENABLE__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__GC_DIDT_CLK_EN_OVERRIDE_MASK,      DIDT_TCP_CTRL3__GC_DIDT_CLK_EN_OVERRIDE__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__THROTTLE_POLICY_MASK,      DIDT_TCP_CTRL3__THROTTLE_POLICY__SHIFT,            0x0003 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT_MASK,      DIDT_TCP_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__DIDT_POWER_LEVEL_LOWBIT_MASK,      DIDT_TCP_CTRL3__DIDT_POWER_LEVEL_LOWBIT__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS_MASK,      DIDT_TCP_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS__SHIFT,            0x0003 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__GC_DIDT_LEVEL_COMB_EN_MASK,      DIDT_TCP_CTRL3__GC_DIDT_LEVEL_COMB_EN__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__SE_DIDT_LEVEL_COMB_EN_MASK,      DIDT_TCP_CTRL3__SE_DIDT_LEVEL_COMB_EN__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__QUALIFY_STALL_EN_MASK,      DIDT_TCP_CTRL3__QUALIFY_STALL_EN__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__DIDT_STALL_SEL_MASK,      DIDT_TCP_CTRL3__DIDT_STALL_SEL__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__DIDT_FORCE_STALL_MASK,      DIDT_TCP_CTRL3__DIDT_FORCE_STALL__SHIFT,            0x0000 },
	{   ixDIDT_TCP_CTRL3,    DIDT_TCP_CTRL3__DIDT_STALL_DELAY_EN_MASK,      DIDT_TCP_CTRL3__DIDT_STALL_DELAY_EN__SHIFT,            0x0000 },

	/*DIDT_TD_CTRL3 */
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__GC_DIDT_ENABLE_MASK,       DIDT_TD_CTRL3__GC_DIDT_ENABLE__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__GC_DIDT_CLK_EN_OVERRIDE_MASK,       DIDT_TD_CTRL3__GC_DIDT_CLK_EN_OVERRIDE__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__THROTTLE_POLICY_MASK,       DIDT_TD_CTRL3__THROTTLE_POLICY__SHIFT,             0x0003 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT_MASK,       DIDT_TD_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__DIDT_POWER_LEVEL_LOWBIT_MASK,       DIDT_TD_CTRL3__DIDT_POWER_LEVEL_LOWBIT__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS_MASK,       DIDT_TD_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS__SHIFT,             0x0003 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__GC_DIDT_LEVEL_COMB_EN_MASK,       DIDT_TD_CTRL3__GC_DIDT_LEVEL_COMB_EN__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__SE_DIDT_LEVEL_COMB_EN_MASK,       DIDT_TD_CTRL3__SE_DIDT_LEVEL_COMB_EN__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__QUALIFY_STALL_EN_MASK,       DIDT_TD_CTRL3__QUALIFY_STALL_EN__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__DIDT_STALL_SEL_MASK,       DIDT_TD_CTRL3__DIDT_STALL_SEL__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__DIDT_FORCE_STALL_MASK,       DIDT_TD_CTRL3__DIDT_FORCE_STALL__SHIFT,             0x0000 },
	{   ixDIDT_TD_CTRL3,     DIDT_TD_CTRL3__DIDT_STALL_DELAY_EN_MASK,       DIDT_TD_CTRL3__DIDT_STALL_DELAY_EN__SHIFT,             0x0000 },

	/*DIDT_DB_CTRL3 */
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__GC_DIDT_ENABLE_MASK,       DIDT_DB_CTRL3__GC_DIDT_ENABLE__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__GC_DIDT_CLK_EN_OVERRIDE_MASK,       DIDT_DB_CTRL3__GC_DIDT_CLK_EN_OVERRIDE__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__THROTTLE_POLICY_MASK,       DIDT_DB_CTRL3__THROTTLE_POLICY__SHIFT,             0x0003 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT_MASK,       DIDT_DB_CTRL3__DIDT_TRIGGER_THROTTLE_LOWBIT__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__DIDT_POWER_LEVEL_LOWBIT_MASK,       DIDT_DB_CTRL3__DIDT_POWER_LEVEL_LOWBIT__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS_MASK,       DIDT_DB_CTRL3__DIDT_STALL_PATTERN_BIT_NUMS__SHIFT,             0x0003 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__GC_DIDT_LEVEL_COMB_EN_MASK,       DIDT_DB_CTRL3__GC_DIDT_LEVEL_COMB_EN__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__SE_DIDT_LEVEL_COMB_EN_MASK,       DIDT_DB_CTRL3__SE_DIDT_LEVEL_COMB_EN__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__QUALIFY_STALL_EN_MASK,       DIDT_DB_CTRL3__QUALIFY_STALL_EN__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__DIDT_STALL_SEL_MASK,       DIDT_DB_CTRL3__DIDT_STALL_SEL__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__DIDT_FORCE_STALL_MASK,       DIDT_DB_CTRL3__DIDT_FORCE_STALL__SHIFT,             0x0000 },
	{   ixDIDT_DB_CTRL3,     DIDT_DB_CTRL3__DIDT_STALL_DELAY_EN_MASK,       DIDT_DB_CTRL3__DIDT_STALL_DELAY_EN__SHIFT,             0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEDiDtCtrl2Config_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                            Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ */
	{   ixDIDT_SQ_CTRL2,                  DIDT_SQ_CTRL2__MAX_POWER_DELTA_MASK,                 DIDT_SQ_CTRL2__MAX_POWER_DELTA__SHIFT,                 0x3853 },
	{   ixDIDT_SQ_CTRL2,                  DIDT_SQ_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,        DIDT_SQ_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,        0x00c0 },
	{   ixDIDT_SQ_CTRL2,                  DIDT_SQ_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,        DIDT_SQ_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,        0x0000 },

	/* DIDT_TD */
	{   ixDIDT_TD_CTRL2,                  DIDT_TD_CTRL2__MAX_POWER_DELTA_MASK,                 DIDT_TD_CTRL2__MAX_POWER_DELTA__SHIFT,                 0x3fff },
	{   ixDIDT_TD_CTRL2,                  DIDT_TD_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,        DIDT_TD_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,        0x00c0 },
	{   ixDIDT_TD_CTRL2,                  DIDT_TD_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,        DIDT_TD_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,        0x0001 },

	/* DIDT_TCP */
	{   ixDIDT_TCP_CTRL2,                 DIDT_TCP_CTRL2__MAX_POWER_DELTA_MASK,                DIDT_TCP_CTRL2__MAX_POWER_DELTA__SHIFT,                0x3dde },
	{   ixDIDT_TCP_CTRL2,                 DIDT_TCP_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,       DIDT_TCP_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,       0x00c0 },
	{   ixDIDT_TCP_CTRL2,                 DIDT_TCP_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,       DIDT_TCP_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,       0x0001 },

	/* DIDT_DB */
	{   ixDIDT_DB_CTRL2,                  DIDT_DB_CTRL2__MAX_POWER_DELTA_MASK,                 DIDT_DB_CTRL2__MAX_POWER_DELTA__SHIFT,                 0x3dde },
	{   ixDIDT_DB_CTRL2,                  DIDT_DB_CTRL2__SHORT_TERM_INTERVAL_SIZE_MASK,        DIDT_DB_CTRL2__SHORT_TERM_INTERVAL_SIZE__SHIFT,        0x00c0 },
	{   ixDIDT_DB_CTRL2,                  DIDT_DB_CTRL2__LONG_TERM_INTERVAL_RATIO_MASK,        DIDT_DB_CTRL2__LONG_TERM_INTERVAL_RATIO__SHIFT,        0x0001 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEDiDtCtrl1Config_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ */
	{   ixDIDT_SQ_CTRL1,                   DIDT_SQ_CTRL1__MIN_POWER_MASK,                       DIDT_SQ_CTRL1__MIN_POWER__SHIFT,                       0x0000 },
	{   ixDIDT_SQ_CTRL1,                   DIDT_SQ_CTRL1__MAX_POWER_MASK,                       DIDT_SQ_CTRL1__MAX_POWER__SHIFT,                       0xffff },
	/* DIDT_TD */
	{   ixDIDT_TD_CTRL1,                   DIDT_TD_CTRL1__MIN_POWER_MASK,                       DIDT_TD_CTRL1__MIN_POWER__SHIFT,                       0x0000 },
	{   ixDIDT_TD_CTRL1,                   DIDT_TD_CTRL1__MAX_POWER_MASK,                       DIDT_TD_CTRL1__MAX_POWER__SHIFT,                       0xffff },
	/* DIDT_TCP */
	{   ixDIDT_TCP_CTRL1,                  DIDT_TCP_CTRL1__MIN_POWER_MASK,                      DIDT_TCP_CTRL1__MIN_POWER__SHIFT,                      0x0000 },
	{   ixDIDT_TCP_CTRL1,                  DIDT_TCP_CTRL1__MAX_POWER_MASK,                      DIDT_TCP_CTRL1__MAX_POWER__SHIFT,                      0xffff },
	/* DIDT_DB */
	{   ixDIDT_DB_CTRL1,                   DIDT_DB_CTRL1__MIN_POWER_MASK,                       DIDT_DB_CTRL1__MIN_POWER__SHIFT,                       0x0000 },
	{   ixDIDT_DB_CTRL1,                   DIDT_DB_CTRL1__MAX_POWER_MASK,                       DIDT_DB_CTRL1__MAX_POWER__SHIFT,                       0xffff },

	{   0xFFFFFFFF  }  /* End of list */
};


static const struct vega10_didt_config_reg SEDiDtWeightConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                  Shift                                                 Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ */
	{   ixDIDT_SQ_WEIGHT0_3,               0xFFFFFFFF,                                           0,                                                    0x2B363B1A },
	{   ixDIDT_SQ_WEIGHT4_7,               0xFFFFFFFF,                                           0,                                                    0x270B2432 },
	{   ixDIDT_SQ_WEIGHT8_11,              0xFFFFFFFF,                                           0,                                                    0x00000018 },

	/* DIDT_TD */
	{   ixDIDT_TD_WEIGHT0_3,               0xFFFFFFFF,                                           0,                                                    0x2B1D220F },
	{   ixDIDT_TD_WEIGHT4_7,               0xFFFFFFFF,                                           0,                                                    0x00007558 },
	{   ixDIDT_TD_WEIGHT8_11,              0xFFFFFFFF,                                           0,                                                    0x00000000 },

	/* DIDT_TCP */
	{   ixDIDT_TCP_WEIGHT0_3,               0xFFFFFFFF,                                          0,                                                    0x5ACE160D },
	{   ixDIDT_TCP_WEIGHT4_7,               0xFFFFFFFF,                                          0,                                                    0x00000000 },
	{   ixDIDT_TCP_WEIGHT8_11,              0xFFFFFFFF,                                          0,                                                    0x00000000 },

	/* DIDT_DB */
	{   ixDIDT_DB_WEIGHT0_3,                0xFFFFFFFF,                                          0,                                                    0x0E152A0F },
	{   ixDIDT_DB_WEIGHT4_7,                0xFFFFFFFF,                                          0,                                                    0x09061813 },
	{   ixDIDT_DB_WEIGHT8_11,               0xFFFFFFFF,                                          0,                                                    0x00000013 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEDiDtCtrl0Config_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ */
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK,   DIDT_SQ_CTRL0__DIDT_CTRL_EN__SHIFT,  0x0000 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__PHASE_OFFSET_MASK,   DIDT_SQ_CTRL0__PHASE_OFFSET__SHIFT,  0x0000 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CTRL_RST_MASK,   DIDT_SQ_CTRL0__DIDT_CTRL_RST__SHIFT,  0x0000 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,   DIDT_SQ_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,  0x0000 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_STALL_CTRL_EN_MASK,   DIDT_SQ_CTRL0__DIDT_STALL_CTRL_EN__SHIFT,  0x0001 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_TUNING_CTRL_EN_MASK,   DIDT_SQ_CTRL0__DIDT_TUNING_CTRL_EN__SHIFT,  0x0001 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_STALL_AUTO_RELEASE_EN_MASK,   DIDT_SQ_CTRL0__DIDT_STALL_AUTO_RELEASE_EN__SHIFT,  0x0001 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_SQ_CTRL0__DIDT_HI_POWER_THRESHOLD__SHIFT,  0xffff },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_AUTO_MPD_EN_MASK,   DIDT_SQ_CTRL0__DIDT_AUTO_MPD_EN__SHIFT,  0x0000 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_STALL_EVENT_EN_MASK,   DIDT_SQ_CTRL0__DIDT_STALL_EVENT_EN__SHIFT,  0x0000 },
	{  ixDIDT_SQ_CTRL0,                   DIDT_SQ_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR_MASK,   DIDT_SQ_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR__SHIFT,  0x0000 },
	/* DIDT_TD */
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK,   DIDT_TD_CTRL0__DIDT_CTRL_EN__SHIFT,  0x0000 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__PHASE_OFFSET_MASK,   DIDT_TD_CTRL0__PHASE_OFFSET__SHIFT,  0x0000 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CTRL_RST_MASK,   DIDT_TD_CTRL0__DIDT_CTRL_RST__SHIFT,  0x0000 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,   DIDT_TD_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,  0x0000 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_STALL_CTRL_EN_MASK,   DIDT_TD_CTRL0__DIDT_STALL_CTRL_EN__SHIFT,  0x0001 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_TUNING_CTRL_EN_MASK,   DIDT_TD_CTRL0__DIDT_TUNING_CTRL_EN__SHIFT,  0x0001 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_STALL_AUTO_RELEASE_EN_MASK,   DIDT_TD_CTRL0__DIDT_STALL_AUTO_RELEASE_EN__SHIFT,  0x0001 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_TD_CTRL0__DIDT_HI_POWER_THRESHOLD__SHIFT,  0xffff },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_AUTO_MPD_EN_MASK,   DIDT_TD_CTRL0__DIDT_AUTO_MPD_EN__SHIFT,  0x0000 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_STALL_EVENT_EN_MASK,   DIDT_TD_CTRL0__DIDT_STALL_EVENT_EN__SHIFT,  0x0000 },
	{  ixDIDT_TD_CTRL0,                   DIDT_TD_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR_MASK,   DIDT_TD_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR__SHIFT,  0x0000 },
	/* DIDT_TCP */
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK,  DIDT_TCP_CTRL0__DIDT_CTRL_EN__SHIFT, 0x0000 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__PHASE_OFFSET_MASK,  DIDT_TCP_CTRL0__PHASE_OFFSET__SHIFT, 0x0000 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_CTRL_RST_MASK,  DIDT_TCP_CTRL0__DIDT_CTRL_RST__SHIFT, 0x0000 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,  DIDT_TCP_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT, 0x0000 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_STALL_CTRL_EN_MASK,  DIDT_TCP_CTRL0__DIDT_STALL_CTRL_EN__SHIFT, 0x0001 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_TUNING_CTRL_EN_MASK,  DIDT_TCP_CTRL0__DIDT_TUNING_CTRL_EN__SHIFT, 0x0001 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_STALL_AUTO_RELEASE_EN_MASK,  DIDT_TCP_CTRL0__DIDT_STALL_AUTO_RELEASE_EN__SHIFT, 0x0001 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_HI_POWER_THRESHOLD_MASK,  DIDT_TCP_CTRL0__DIDT_HI_POWER_THRESHOLD__SHIFT, 0xffff },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_AUTO_MPD_EN_MASK,  DIDT_TCP_CTRL0__DIDT_AUTO_MPD_EN__SHIFT, 0x0000 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_STALL_EVENT_EN_MASK,  DIDT_TCP_CTRL0__DIDT_STALL_EVENT_EN__SHIFT, 0x0000 },
	{  ixDIDT_TCP_CTRL0,                  DIDT_TCP_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR_MASK,  DIDT_TCP_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR__SHIFT, 0x0000 },
	/* DIDT_DB */
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_CTRL_EN_MASK,   DIDT_DB_CTRL0__DIDT_CTRL_EN__SHIFT,  0x0000 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__PHASE_OFFSET_MASK,   DIDT_DB_CTRL0__PHASE_OFFSET__SHIFT,  0x0000 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_CTRL_RST_MASK,   DIDT_DB_CTRL0__DIDT_CTRL_RST__SHIFT,  0x0000 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,   DIDT_DB_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,  0x0000 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_STALL_CTRL_EN_MASK,   DIDT_DB_CTRL0__DIDT_STALL_CTRL_EN__SHIFT,  0x0001 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_TUNING_CTRL_EN_MASK,   DIDT_DB_CTRL0__DIDT_TUNING_CTRL_EN__SHIFT,  0x0001 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_STALL_AUTO_RELEASE_EN_MASK,   DIDT_DB_CTRL0__DIDT_STALL_AUTO_RELEASE_EN__SHIFT,  0x0001 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_HI_POWER_THRESHOLD_MASK,   DIDT_DB_CTRL0__DIDT_HI_POWER_THRESHOLD__SHIFT,  0xffff },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_AUTO_MPD_EN_MASK,   DIDT_DB_CTRL0__DIDT_AUTO_MPD_EN__SHIFT,  0x0000 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_STALL_EVENT_EN_MASK,   DIDT_DB_CTRL0__DIDT_STALL_EVENT_EN__SHIFT,  0x0000 },
	{  ixDIDT_DB_CTRL0,                   DIDT_DB_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR_MASK,   DIDT_DB_CTRL0__DIDT_STALL_EVENT_COUNTER_CLEAR__SHIFT,  0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};


static const struct vega10_didt_config_reg SEDiDtStallCtrlConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                   Mask                                                     Shift                                                      Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ */
	{   ixDIDT_SQ_STALL_CTRL,    DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,    DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0004 },
	{   ixDIDT_SQ_STALL_CTRL,    DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,    DIDT_SQ_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0004 },
	{   ixDIDT_SQ_STALL_CTRL,    DIDT_SQ_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI_MASK,    DIDT_SQ_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,     0x000a },
	{   ixDIDT_SQ_STALL_CTRL,    DIDT_SQ_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO_MASK,    DIDT_SQ_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,     0x000a },

	/* DIDT_TD */
	{   ixDIDT_TD_STALL_CTRL,    DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,    DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0001 },
	{   ixDIDT_TD_STALL_CTRL,    DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,    DIDT_TD_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0001 },
	{   ixDIDT_TD_STALL_CTRL,    DIDT_TD_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI_MASK,    DIDT_TD_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,     0x000a },
	{   ixDIDT_TD_STALL_CTRL,    DIDT_TD_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO_MASK,    DIDT_TD_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,     0x000a },

	/* DIDT_TCP */
	{   ixDIDT_TCP_STALL_CTRL,   DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,   DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,    0x0001 },
	{   ixDIDT_TCP_STALL_CTRL,   DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,   DIDT_TCP_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,    0x0001 },
	{   ixDIDT_TCP_STALL_CTRL,   DIDT_TCP_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI_MASK,   DIDT_TCP_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,    0x000a },
	{   ixDIDT_TCP_STALL_CTRL,   DIDT_TCP_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO_MASK,   DIDT_TCP_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,    0x000a },

	/* DIDT_DB */
	{   ixDIDT_DB_STALL_CTRL,    DIDT_DB_STALL_CTRL__DIDT_STALL_DELAY_HI_MASK,    DIDT_DB_STALL_CTRL__DIDT_STALL_DELAY_HI__SHIFT,     0x0004 },
	{   ixDIDT_DB_STALL_CTRL,    DIDT_DB_STALL_CTRL__DIDT_STALL_DELAY_LO_MASK,    DIDT_DB_STALL_CTRL__DIDT_STALL_DELAY_LO__SHIFT,     0x0004 },
	{   ixDIDT_DB_STALL_CTRL,    DIDT_DB_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI_MASK,    DIDT_DB_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_HI__SHIFT,     0x000a },
	{   ixDIDT_DB_STALL_CTRL,    DIDT_DB_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO_MASK,    DIDT_DB_STALL_CTRL__DIDT_MAX_STALLS_ALLOWED_LO__SHIFT,     0x000a },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEDiDtStallPatternConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                        Mask                                                      Shift                                                    Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* DIDT_SQ_STALL_PATTERN_1_2 */
	{   ixDIDT_SQ_STALL_PATTERN_1_2,  DIDT_SQ_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1_MASK,    DIDT_SQ_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1__SHIFT,  0x0001 },
	{   ixDIDT_SQ_STALL_PATTERN_1_2,  DIDT_SQ_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2_MASK,    DIDT_SQ_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2__SHIFT,  0x0001 },

	/* DIDT_SQ_STALL_PATTERN_3_4 */
	{   ixDIDT_SQ_STALL_PATTERN_3_4,  DIDT_SQ_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3_MASK,    DIDT_SQ_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3__SHIFT,  0x0001 },
	{   ixDIDT_SQ_STALL_PATTERN_3_4,  DIDT_SQ_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4_MASK,    DIDT_SQ_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4__SHIFT,  0x0001 },

	/* DIDT_SQ_STALL_PATTERN_5_6 */
	{   ixDIDT_SQ_STALL_PATTERN_5_6,  DIDT_SQ_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5_MASK,    DIDT_SQ_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5__SHIFT,  0x0000 },
	{   ixDIDT_SQ_STALL_PATTERN_5_6,  DIDT_SQ_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6_MASK,    DIDT_SQ_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6__SHIFT,  0x0000 },

	/* DIDT_SQ_STALL_PATTERN_7 */
	{   ixDIDT_SQ_STALL_PATTERN_7,    DIDT_SQ_STALL_PATTERN_7__DIDT_STALL_PATTERN_7_MASK,      DIDT_SQ_STALL_PATTERN_7__DIDT_STALL_PATTERN_7__SHIFT,    0x0000 },

	/* DIDT_TCP_STALL_PATTERN_1_2 */
	{   ixDIDT_TCP_STALL_PATTERN_1_2, DIDT_TCP_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1_MASK,   DIDT_TCP_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1__SHIFT, 0x0001 },
	{   ixDIDT_TCP_STALL_PATTERN_1_2, DIDT_TCP_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2_MASK,   DIDT_TCP_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2__SHIFT, 0x0001 },

	/* DIDT_TCP_STALL_PATTERN_3_4 */
	{   ixDIDT_TCP_STALL_PATTERN_3_4, DIDT_TCP_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3_MASK,   DIDT_TCP_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3__SHIFT, 0x0001 },
	{   ixDIDT_TCP_STALL_PATTERN_3_4, DIDT_TCP_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4_MASK,   DIDT_TCP_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4__SHIFT, 0x0001 },

	/* DIDT_TCP_STALL_PATTERN_5_6 */
	{   ixDIDT_TCP_STALL_PATTERN_5_6, DIDT_TCP_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5_MASK,   DIDT_TCP_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5__SHIFT, 0x0000 },
	{   ixDIDT_TCP_STALL_PATTERN_5_6, DIDT_TCP_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6_MASK,   DIDT_TCP_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6__SHIFT, 0x0000 },

	/* DIDT_TCP_STALL_PATTERN_7 */
	{   ixDIDT_TCP_STALL_PATTERN_7,   DIDT_TCP_STALL_PATTERN_7__DIDT_STALL_PATTERN_7_MASK,     DIDT_TCP_STALL_PATTERN_7__DIDT_STALL_PATTERN_7__SHIFT,   0x0000 },

	/* DIDT_TD_STALL_PATTERN_1_2 */
	{   ixDIDT_TD_STALL_PATTERN_1_2,  DIDT_TD_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1_MASK,    DIDT_TD_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1__SHIFT,  0x0001 },
	{   ixDIDT_TD_STALL_PATTERN_1_2,  DIDT_TD_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2_MASK,    DIDT_TD_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2__SHIFT,  0x0001 },

	/* DIDT_TD_STALL_PATTERN_3_4 */
	{   ixDIDT_TD_STALL_PATTERN_3_4,  DIDT_TD_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3_MASK,    DIDT_TD_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3__SHIFT,  0x0001 },
	{   ixDIDT_TD_STALL_PATTERN_3_4,  DIDT_TD_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4_MASK,    DIDT_TD_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4__SHIFT,  0x0001 },

	/* DIDT_TD_STALL_PATTERN_5_6 */
	{   ixDIDT_TD_STALL_PATTERN_5_6,  DIDT_TD_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5_MASK,    DIDT_TD_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5__SHIFT,  0x0000 },
	{   ixDIDT_TD_STALL_PATTERN_5_6,  DIDT_TD_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6_MASK,    DIDT_TD_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6__SHIFT,  0x0000 },

	/* DIDT_TD_STALL_PATTERN_7 */
	{   ixDIDT_TD_STALL_PATTERN_7,    DIDT_TD_STALL_PATTERN_7__DIDT_STALL_PATTERN_7_MASK,      DIDT_TD_STALL_PATTERN_7__DIDT_STALL_PATTERN_7__SHIFT,    0x0000 },

	/* DIDT_DB_STALL_PATTERN_1_2 */
	{   ixDIDT_DB_STALL_PATTERN_1_2,  DIDT_DB_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1_MASK,    DIDT_DB_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_1__SHIFT,  0x0001 },
	{   ixDIDT_DB_STALL_PATTERN_1_2,  DIDT_DB_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2_MASK,    DIDT_DB_STALL_PATTERN_1_2__DIDT_STALL_PATTERN_2__SHIFT,  0x0001 },

	/* DIDT_DB_STALL_PATTERN_3_4 */
	{   ixDIDT_DB_STALL_PATTERN_3_4,  DIDT_DB_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3_MASK,    DIDT_DB_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_3__SHIFT,  0x0001 },
	{   ixDIDT_DB_STALL_PATTERN_3_4,  DIDT_DB_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4_MASK,    DIDT_DB_STALL_PATTERN_3_4__DIDT_STALL_PATTERN_4__SHIFT,  0x0001 },

	/* DIDT_DB_STALL_PATTERN_5_6 */
	{   ixDIDT_DB_STALL_PATTERN_5_6,  DIDT_DB_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5_MASK,    DIDT_DB_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_5__SHIFT,  0x0000 },
	{   ixDIDT_DB_STALL_PATTERN_5_6,  DIDT_DB_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6_MASK,    DIDT_DB_STALL_PATTERN_5_6__DIDT_STALL_PATTERN_6__SHIFT,  0x0000 },

	/* DIDT_DB_STALL_PATTERN_7 */
	{   ixDIDT_DB_STALL_PATTERN_7,    DIDT_DB_STALL_PATTERN_7__DIDT_STALL_PATTERN_7_MASK,      DIDT_DB_STALL_PATTERN_7__DIDT_STALL_PATTERN_7__SHIFT,    0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SELCacConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x00060021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x00860021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x01060021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x01860021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x02060021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x02860021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x03060021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x03860021 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x04060021 },
	/* TD */
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x000E0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x008E0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x010E0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x018E0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x020E0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x028E0020 },
	/* TCP */
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x001c0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x009c0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x011c0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x019c0020 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x021c0020 },
	/* DB */
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x00200008 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x00820008 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x01020008 },
	{   ixSE_CAC_CNTL,                     0xFFFFFFFF,                                          0,                                                     0x01820008 },

	{   0xFFFFFFFF  }  /* End of list */
};


static const struct vega10_didt_config_reg SEEDCStallPatternConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixDIDT_SQ_EDC_STALL_PATTERN_1_2,   0xFFFFFFFF,                                          0,                                                     0x00030001 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_3_4,   0xFFFFFFFF,                                          0,                                                     0x000F0007 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_5_6,   0xFFFFFFFF,                                          0,                                                     0x003F001F },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_7,     0xFFFFFFFF,                                          0,                                                     0x0000007F },
	/* TD */
	{   ixDIDT_TD_EDC_STALL_PATTERN_1_2,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_PATTERN_3_4,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_PATTERN_5_6,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_PATTERN_7,     0xFFFFFFFF,                                          0,                                                     0x00000000 },
	/* TCP */
	{   ixDIDT_TCP_EDC_STALL_PATTERN_1_2,   0xFFFFFFFF,                                         0,                                                     0x00000000 },
	{   ixDIDT_TCP_EDC_STALL_PATTERN_3_4,   0xFFFFFFFF,                                         0,                                                     0x00000000 },
	{   ixDIDT_TCP_EDC_STALL_PATTERN_5_6,   0xFFFFFFFF,                                         0,                                                     0x00000000 },
	{   ixDIDT_TCP_EDC_STALL_PATTERN_7,     0xFFFFFFFF,                                         0,                                                     0x00000000 },
	/* DB */
	{   ixDIDT_DB_EDC_STALL_PATTERN_1_2,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_DB_EDC_STALL_PATTERN_3_4,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_DB_EDC_STALL_PATTERN_5_6,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_DB_EDC_STALL_PATTERN_7,     0xFFFFFFFF,                                          0,                                                     0x00000000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEEDCForceStallPatternConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixDIDT_SQ_EDC_STALL_PATTERN_1_2,   0xFFFFFFFF,                                          0,                                                     0x00000015 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_3_4,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_5_6,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_7,     0xFFFFFFFF,                                          0,                                                     0x00000000 },
	/* TD */
	{   ixDIDT_TD_EDC_STALL_PATTERN_1_2,   0xFFFFFFFF,                                          0,                                                     0x00000015 },
	{   ixDIDT_TD_EDC_STALL_PATTERN_3_4,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_PATTERN_5_6,   0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_PATTERN_7,     0xFFFFFFFF,                                          0,                                                     0x00000000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEEDCStallDelayConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixDIDT_SQ_EDC_STALL_DELAY_1,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_2,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_3,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_4,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	/* TD */
	{   ixDIDT_TD_EDC_STALL_DELAY_1,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_DELAY_2,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_DELAY_3,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TD_EDC_STALL_DELAY_4,       0xFFFFFFFF,                                          0,                                                     0x00000000 },
	/* TCP */
	{   ixDIDT_TCP_EDC_STALL_DELAY_1,      0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TCP_EDC_STALL_DELAY_2,      0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TCP_EDC_STALL_DELAY_3,      0xFFFFFFFF,                                          0,                                                     0x00000000 },
	{   ixDIDT_TCP_EDC_STALL_DELAY_4,      0xFFFFFFFF,                                          0,                                                     0x00000000 },
	/* DB */
	{   ixDIDT_DB_EDC_STALL_DELAY_1,       0xFFFFFFFF,                                          0,                                                     0x00000000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEEDCThresholdConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   ixDIDT_SQ_EDC_THRESHOLD,           0xFFFFFFFF,                                          0,                                                     0x0000010E },
	{   ixDIDT_TD_EDC_THRESHOLD,           0xFFFFFFFF,                                          0,                                                     0xFFFFFFFF },
	{   ixDIDT_TCP_EDC_THRESHOLD,          0xFFFFFFFF,                                          0,                                                     0xFFFFFFFF },
	{   ixDIDT_DB_EDC_THRESHOLD,           0xFFFFFFFF,                                          0,                                                     0xFFFFFFFF },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEEDCCtrlResetConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_EN_MASK,                       DIDT_SQ_EDC_CTRL__EDC_EN__SHIFT,                        0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_SW_RST_MASK,                   DIDT_SQ_EDC_CTRL__EDC_SW_RST__SHIFT,                    0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,          DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL_MASK,              DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL__SHIFT,               0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,  DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,   0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS_MASK,   DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS__SHIFT,    0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,     DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,      0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_EN_MASK,                    DIDT_SQ_EDC_CTRL__GC_EDC_EN__SHIFT,                     0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY_MASK,          DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEEDCCtrlConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_EN_MASK,                       DIDT_SQ_EDC_CTRL__EDC_EN__SHIFT,                        0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_SW_RST_MASK,                   DIDT_SQ_EDC_CTRL__EDC_SW_RST__SHIFT,                    0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,          DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL_MASK,              DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL__SHIFT,               0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,  DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,   0x0004 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS_MASK,   DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS__SHIFT,    0x0006 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,     DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,      0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_EN_MASK,                    DIDT_SQ_EDC_CTRL__GC_EDC_EN__SHIFT,                     0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY_MASK,          DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN__SHIFT,          0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg SEEDCCtrlForceStallConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ */
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_EN_MASK,                       DIDT_SQ_EDC_CTRL__EDC_EN__SHIFT,                        0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_SW_RST_MASK,                   DIDT_SQ_EDC_CTRL__EDC_SW_RST__SHIFT,                    0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,          DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL_MASK,              DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL__SHIFT,               0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,  DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,   0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS_MASK,   DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS__SHIFT,    0x000C },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,     DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,      0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_EN_MASK,                    DIDT_SQ_EDC_CTRL__GC_EDC_EN__SHIFT,                     0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY_MASK,          DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN__SHIFT,          0x0001 },

	/* TD */
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_EN_MASK,                       DIDT_TD_EDC_CTRL__EDC_EN__SHIFT,                        0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_SW_RST_MASK,                   DIDT_TD_EDC_CTRL__EDC_SW_RST__SHIFT,                    0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,          DIDT_TD_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,           0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_FORCE_STALL_MASK,              DIDT_TD_EDC_CTRL__EDC_FORCE_STALL__SHIFT,               0x0001 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,  DIDT_TD_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,   0x0001 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS_MASK,   DIDT_TD_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS__SHIFT,    0x000E },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,     DIDT_TD_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,      0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__GC_EDC_EN_MASK,                    DIDT_TD_EDC_CTRL__GC_EDC_EN__SHIFT,                     0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__GC_EDC_STALL_POLICY_MASK,          DIDT_TD_EDC_CTRL__GC_EDC_STALL_POLICY__SHIFT,           0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__GC_EDC_LEVEL_COMB_EN_MASK,         DIDT_TD_EDC_CTRL__GC_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },
	{   ixDIDT_TD_EDC_CTRL,                DIDT_TD_EDC_CTRL__SE_EDC_LEVEL_COMB_EN_MASK,         DIDT_TD_EDC_CTRL__SE_EDC_LEVEL_COMB_EN__SHIFT,          0x0001 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg    GCDiDtDroopCtrlConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   mmGC_DIDT_DROOP_CTRL,             GC_DIDT_DROOP_CTRL__DIDT_DROOP_LEVEL_EN_MASK,   GC_DIDT_DROOP_CTRL__DIDT_DROOP_LEVEL_EN__SHIFT,  0x0000 },
	{   mmGC_DIDT_DROOP_CTRL,             GC_DIDT_DROOP_CTRL__DIDT_DROOP_THRESHOLD_MASK,   GC_DIDT_DROOP_CTRL__DIDT_DROOP_THRESHOLD__SHIFT,  0x0000 },
	{   mmGC_DIDT_DROOP_CTRL,             GC_DIDT_DROOP_CTRL__DIDT_DROOP_LEVEL_INDEX_MASK,   GC_DIDT_DROOP_CTRL__DIDT_DROOP_LEVEL_INDEX__SHIFT,  0x0000 },
	{   mmGC_DIDT_DROOP_CTRL,             GC_DIDT_DROOP_CTRL__DIDT_LEVEL_SEL_MASK,   GC_DIDT_DROOP_CTRL__DIDT_LEVEL_SEL__SHIFT,  0x0000 },
	{   mmGC_DIDT_DROOP_CTRL,             GC_DIDT_DROOP_CTRL__DIDT_DROOP_LEVEL_OVERFLOW_MASK,   GC_DIDT_DROOP_CTRL__DIDT_DROOP_LEVEL_OVERFLOW__SHIFT,  0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg    GCDiDtCtrl0Config_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   mmGC_DIDT_CTRL0,                  GC_DIDT_CTRL0__DIDT_CTRL_EN_MASK,   GC_DIDT_CTRL0__DIDT_CTRL_EN__SHIFT,  0x0000 },
	{   mmGC_DIDT_CTRL0,                  GC_DIDT_CTRL0__PHASE_OFFSET_MASK,   GC_DIDT_CTRL0__PHASE_OFFSET__SHIFT,  0x0000 },
	{   mmGC_DIDT_CTRL0,                  GC_DIDT_CTRL0__DIDT_SW_RST_MASK,   GC_DIDT_CTRL0__DIDT_SW_RST__SHIFT,  0x0000 },
	{   mmGC_DIDT_CTRL0,                  GC_DIDT_CTRL0__DIDT_CLK_EN_OVERRIDE_MASK,   GC_DIDT_CTRL0__DIDT_CLK_EN_OVERRIDE__SHIFT,  0x0000 },
	{   mmGC_DIDT_CTRL0,                  GC_DIDT_CTRL0__DIDT_TRIGGER_THROTTLE_LOWBIT_MASK,   GC_DIDT_CTRL0__DIDT_TRIGGER_THROTTLE_LOWBIT__SHIFT,  0x0000 },
	{   0xFFFFFFFF  }  /* End of list */
};


static const struct vega10_didt_config_reg   PSMSEEDCStallPatternConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ EDC STALL PATTERNs */
	{   ixDIDT_SQ_EDC_STALL_PATTERN_1_2,  DIDT_SQ_EDC_STALL_PATTERN_1_2__EDC_STALL_PATTERN_1_MASK,   DIDT_SQ_EDC_STALL_PATTERN_1_2__EDC_STALL_PATTERN_1__SHIFT,   0x0101 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_1_2,  DIDT_SQ_EDC_STALL_PATTERN_1_2__EDC_STALL_PATTERN_2_MASK,   DIDT_SQ_EDC_STALL_PATTERN_1_2__EDC_STALL_PATTERN_2__SHIFT,   0x0101 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_3_4,  DIDT_SQ_EDC_STALL_PATTERN_3_4__EDC_STALL_PATTERN_3_MASK,   DIDT_SQ_EDC_STALL_PATTERN_3_4__EDC_STALL_PATTERN_3__SHIFT,   0x1111 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_3_4,  DIDT_SQ_EDC_STALL_PATTERN_3_4__EDC_STALL_PATTERN_4_MASK,   DIDT_SQ_EDC_STALL_PATTERN_3_4__EDC_STALL_PATTERN_4__SHIFT,   0x1111 },

	{   ixDIDT_SQ_EDC_STALL_PATTERN_5_6,  DIDT_SQ_EDC_STALL_PATTERN_5_6__EDC_STALL_PATTERN_5_MASK,   DIDT_SQ_EDC_STALL_PATTERN_5_6__EDC_STALL_PATTERN_5__SHIFT,   0x1515 },
	{   ixDIDT_SQ_EDC_STALL_PATTERN_5_6,  DIDT_SQ_EDC_STALL_PATTERN_5_6__EDC_STALL_PATTERN_6_MASK,   DIDT_SQ_EDC_STALL_PATTERN_5_6__EDC_STALL_PATTERN_6__SHIFT,   0x1515 },

	{   ixDIDT_SQ_EDC_STALL_PATTERN_7,  DIDT_SQ_EDC_STALL_PATTERN_7__EDC_STALL_PATTERN_7_MASK,   DIDT_SQ_EDC_STALL_PATTERN_7__EDC_STALL_PATTERN_7__SHIFT,     0x5555 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg   PSMSEEDCStallDelayConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ EDC STALL DELAYs */
	{   ixDIDT_SQ_EDC_STALL_DELAY_1,      DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ0_MASK,  DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ0__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_1,      DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ1_MASK,  DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ1__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_1,      DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ2_MASK,  DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ2__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_1,      DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ3_MASK,  DIDT_SQ_EDC_STALL_DELAY_1__EDC_STALL_DELAY_SQ3__SHIFT,  0x0000 },

	{   ixDIDT_SQ_EDC_STALL_DELAY_2,      DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ4_MASK,  DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ4__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_2,      DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ5_MASK,  DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ5__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_2,      DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ6_MASK,  DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ6__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_2,      DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ7_MASK,  DIDT_SQ_EDC_STALL_DELAY_2__EDC_STALL_DELAY_SQ7__SHIFT,  0x0000 },

	{   ixDIDT_SQ_EDC_STALL_DELAY_3,      DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ8_MASK,  DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ8__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_3,      DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ9_MASK,  DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ9__SHIFT,  0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_3,      DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ10_MASK, DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ10__SHIFT, 0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_3,      DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ11_MASK, DIDT_SQ_EDC_STALL_DELAY_3__EDC_STALL_DELAY_SQ11__SHIFT, 0x0000 },

	{   ixDIDT_SQ_EDC_STALL_DELAY_4,      DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ12_MASK, DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ12__SHIFT, 0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_4,      DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ12_MASK, DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ13__SHIFT, 0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_4,      DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ14_MASK, DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ14__SHIFT, 0x0000 },
	{   ixDIDT_SQ_EDC_STALL_DELAY_4,      DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ15_MASK, DIDT_SQ_EDC_STALL_DELAY_4__EDC_STALL_DELAY_SQ15__SHIFT, 0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg   PSMSEEDCCtrlResetConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ EDC CTRL */
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_EN_MASK,                       DIDT_SQ_EDC_CTRL__EDC_EN__SHIFT,                        0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_SW_RST_MASK,                   DIDT_SQ_EDC_CTRL__EDC_SW_RST__SHIFT,                    0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,          DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL_MASK,              DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL__SHIFT,               0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,  DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,   0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS_MASK,   DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS__SHIFT,    0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,     DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,      0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_EN_MASK,                    DIDT_SQ_EDC_CTRL__GC_EDC_EN__SHIFT,                     0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY_MASK,          DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg   PSMSEEDCCtrlConfig_Vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	/* SQ EDC CTRL */
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_EN_MASK,                       DIDT_SQ_EDC_CTRL__EDC_EN__SHIFT,                        0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_SW_RST_MASK,                   DIDT_SQ_EDC_CTRL__EDC_SW_RST__SHIFT,                    0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,          DIDT_SQ_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,           0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL_MASK,              DIDT_SQ_EDC_CTRL__EDC_FORCE_STALL__SHIFT,               0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,  DIDT_SQ_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,   0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS_MASK,   DIDT_SQ_EDC_CTRL__EDC_STALL_PATTERN_BIT_NUMS__SHIFT,    0x000E },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,     DIDT_SQ_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,      0x0000 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_EN_MASK,                    DIDT_SQ_EDC_CTRL__GC_EDC_EN__SHIFT,                     0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY_MASK,          DIDT_SQ_EDC_CTRL__GC_EDC_STALL_POLICY__SHIFT,           0x0003 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__GC_EDC_LEVEL_COMB_EN__SHIFT,          0x0001 },
	{   ixDIDT_SQ_EDC_CTRL,                DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN_MASK,         DIDT_SQ_EDC_CTRL__SE_EDC_LEVEL_COMB_EN__SHIFT,          0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg   PSMGCEDCDroopCtrlConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   mmGC_EDC_DROOP_CTRL,               GC_EDC_DROOP_CTRL__EDC_DROOP_LEVEL_EN_MASK,          GC_EDC_DROOP_CTRL__EDC_DROOP_LEVEL_EN__SHIFT,           0x0001 },
	{   mmGC_EDC_DROOP_CTRL,               GC_EDC_DROOP_CTRL__EDC_DROOP_THRESHOLD_MASK,         GC_EDC_DROOP_CTRL__EDC_DROOP_THRESHOLD__SHIFT,          0x0384 },
	{   mmGC_EDC_DROOP_CTRL,               GC_EDC_DROOP_CTRL__EDC_DROOP_LEVEL_INDEX_MASK,       GC_EDC_DROOP_CTRL__EDC_DROOP_LEVEL_INDEX__SHIFT,        0x0001 },
	{   mmGC_EDC_DROOP_CTRL,               GC_EDC_DROOP_CTRL__AVG_PSM_SEL_MASK,                 GC_EDC_DROOP_CTRL__AVG_PSM_SEL__SHIFT,                  0x0001 },
	{   mmGC_EDC_DROOP_CTRL,               GC_EDC_DROOP_CTRL__EDC_LEVEL_SEL_MASK,               GC_EDC_DROOP_CTRL__EDC_LEVEL_SEL__SHIFT,                0x0001 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg   PSMGCEDCCtrlResetConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_EN_MASK,                            GC_EDC_CTRL__EDC_EN__SHIFT,                             0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_SW_RST_MASK,                        GC_EDC_CTRL__EDC_SW_RST__SHIFT,                         0x0001 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,               GC_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,                0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_FORCE_STALL_MASK,                   GC_EDC_CTRL__EDC_FORCE_STALL__SHIFT,                    0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,       GC_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,        0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,          GC_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,           0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg   PSMGCEDCCtrlConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_EN_MASK,                            GC_EDC_CTRL__EDC_EN__SHIFT,                             0x0001 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_SW_RST_MASK,                        GC_EDC_CTRL__EDC_SW_RST__SHIFT,                         0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_CLK_EN_OVERRIDE_MASK,               GC_EDC_CTRL__EDC_CLK_EN_OVERRIDE__SHIFT,                0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_FORCE_STALL_MASK,                   GC_EDC_CTRL__EDC_FORCE_STALL__SHIFT,                    0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT_MASK,       GC_EDC_CTRL__EDC_TRIGGER_THROTTLE_LOWBIT__SHIFT,        0x0000 },
	{   mmGC_EDC_CTRL,                     GC_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA_MASK,          GC_EDC_CTRL__EDC_ALLOW_WRITE_PWRDELTA__SHIFT,           0x0000 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg    AvfsPSMResetConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   0x16A02,                         0xFFFFFFFF,                                            0x0,                                                    0x0000005F },
	{   0x16A05,                         0xFFFFFFFF,                                            0x0,                                                    0x00000001 },
	{   0x16A06,                         0x00000001,                                            0x0,                                                    0x02000000 },
	{   0x16A01,                         0xFFFFFFFF,                                            0x0,                                                    0x00003027 },

	{   0xFFFFFFFF  }  /* End of list */
};

static const struct vega10_didt_config_reg    AvfsPSMInitConfig_vega10[] = {
/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 *      Offset                             Mask                                                 Shift                                                  Value
 * ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 */
	{   0x16A05,                         0xFFFFFFFF,                                            0x18,                                                    0x00000001 },
	{   0x16A05,                         0xFFFFFFFF,                                            0x8,                                                     0x00000003 },
	{   0x16A05,                         0xFFFFFFFF,                                            0xa,                                                     0x00000006 },
	{   0x16A05,                         0xFFFFFFFF,                                            0x7,                                                     0x00000000 },
	{   0x16A06,                         0xFFFFFFFF,                                            0x18,                                                    0x00000001 },
	{   0x16A06,                         0xFFFFFFFF,                                            0x19,                                                    0x00000001 },
	{   0x16A01,                         0xFFFFFFFF,                                            0x0,                                                     0x00003027 },

	{   0xFFFFFFFF  }  /* End of list */
};

static int vega10_program_didt_config_registers(struct pp_hwmgr *hwmgr, const struct vega10_didt_config_reg *config_regs, enum vega10_didt_config_reg_type reg_type)
{
	uint32_t data;

	PP_ASSERT_WITH_CODE((config_regs != NULL), "[vega10_program_didt_config_registers] Invalid config register table!", return -EINVAL);

	while (config_regs->offset != 0xFFFFFFFF) {
		switch (reg_type) {
		case VEGA10_CONFIGREG_DIDT:
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, config_regs->offset);
			data &= ~config_regs->mask;
			data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, config_regs->offset, data);
			break;
		case VEGA10_CONFIGREG_GCCAC:
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG_GC_CAC, config_regs->offset);
			data &= ~config_regs->mask;
			data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG_GC_CAC, config_regs->offset, data);
			break;
		case VEGA10_CONFIGREG_SECAC:
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG_SE_CAC, config_regs->offset);
			data &= ~config_regs->mask;
			data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG_SE_CAC, config_regs->offset, data);
			break;
		default:
			return -EINVAL;
		}

		config_regs++;
	}

	return 0;
}

static int vega10_program_gc_didt_config_registers(struct pp_hwmgr *hwmgr, const struct vega10_didt_config_reg *config_regs)
{
	uint32_t data;

	while (config_regs->offset != 0xFFFFFFFF) {
		data = cgs_read_register(hwmgr->device, config_regs->offset);
		data &= ~config_regs->mask;
		data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
		cgs_write_register(hwmgr->device, config_regs->offset, data);
		config_regs++;
	}

	return 0;
}

static void vega10_didt_set_mask(struct pp_hwmgr *hwmgr, const bool enable)
{
	uint32_t data;
	uint32_t en = (enable ? 1 : 0);
	uint32_t didt_block_info = SQ_IR_MASK | TCP_IR_MASK | TD_PCC_MASK;

	if (PP_CAP(PHM_PlatformCaps_SQRamping)) {
		CGS_WREG32_FIELD_IND(hwmgr->device, CGS_IND_REG__DIDT,
				     DIDT_SQ_CTRL0, DIDT_CTRL_EN, en);
		didt_block_info &= ~SQ_Enable_MASK;
		didt_block_info |= en << SQ_Enable_SHIFT;
	}

	if (PP_CAP(PHM_PlatformCaps_DBRamping)) {
		CGS_WREG32_FIELD_IND(hwmgr->device, CGS_IND_REG__DIDT,
				     DIDT_DB_CTRL0, DIDT_CTRL_EN, en);
		didt_block_info &= ~DB_Enable_MASK;
		didt_block_info |= en << DB_Enable_SHIFT;
	}

	if (PP_CAP(PHM_PlatformCaps_TDRamping)) {
		CGS_WREG32_FIELD_IND(hwmgr->device, CGS_IND_REG__DIDT,
				     DIDT_TD_CTRL0, DIDT_CTRL_EN, en);
		didt_block_info &= ~TD_Enable_MASK;
		didt_block_info |= en << TD_Enable_SHIFT;
	}

	if (PP_CAP(PHM_PlatformCaps_TCPRamping)) {
		CGS_WREG32_FIELD_IND(hwmgr->device, CGS_IND_REG__DIDT,
				     DIDT_TCP_CTRL0, DIDT_CTRL_EN, en);
		didt_block_info &= ~TCP_Enable_MASK;
		didt_block_info |= en << TCP_Enable_SHIFT;
	}

	if (PP_CAP(PHM_PlatformCaps_DBRRamping)) {
		CGS_WREG32_FIELD_IND(hwmgr->device, CGS_IND_REG__DIDT,
				     DIDT_DBR_CTRL0, DIDT_CTRL_EN, en);
	}

	if (PP_CAP(PHM_PlatformCaps_DiDtEDCEnable)) {
		if (PP_CAP(PHM_PlatformCaps_SQRamping)) {
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_SQ_EDC_CTRL);
			data = REG_SET_FIELD(data, DIDT_SQ_EDC_CTRL, EDC_EN, en);
			data = REG_SET_FIELD(data, DIDT_SQ_EDC_CTRL, EDC_SW_RST, ~en);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_SQ_EDC_CTRL, data);
		}

		if (PP_CAP(PHM_PlatformCaps_DBRamping)) {
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_DB_EDC_CTRL);
			data = REG_SET_FIELD(data, DIDT_DB_EDC_CTRL, EDC_EN, en);
			data = REG_SET_FIELD(data, DIDT_DB_EDC_CTRL, EDC_SW_RST, ~en);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_DB_EDC_CTRL, data);
		}

		if (PP_CAP(PHM_PlatformCaps_TDRamping)) {
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TD_EDC_CTRL);
			data = REG_SET_FIELD(data, DIDT_TD_EDC_CTRL, EDC_EN, en);
			data = REG_SET_FIELD(data, DIDT_TD_EDC_CTRL, EDC_SW_RST, ~en);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TD_EDC_CTRL, data);
		}

		if (PP_CAP(PHM_PlatformCaps_TCPRamping)) {
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TCP_EDC_CTRL);
			data = REG_SET_FIELD(data, DIDT_TCP_EDC_CTRL, EDC_EN, en);
			data = REG_SET_FIELD(data, DIDT_TCP_EDC_CTRL, EDC_SW_RST, ~en);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_TCP_EDC_CTRL, data);
		}

		if (PP_CAP(PHM_PlatformCaps_DBRRamping)) {
			data = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_DBR_EDC_CTRL);
			data = REG_SET_FIELD(data, DIDT_DBR_EDC_CTRL, EDC_EN, en);
			data = REG_SET_FIELD(data, DIDT_DBR_EDC_CTRL, EDC_SW_RST, ~en);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__DIDT, ixDIDT_DBR_EDC_CTRL, data);
		}
	}

	/* For Vega10, SMC does not support any mask yet. */
	if (enable)
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_ConfigureGfxDidt, didt_block_info,
						NULL);

}

static int vega10_enable_cac_driving_se_didt_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result;
	uint32_t num_se = 0, count, data;

	num_se = adev->gfx.config.max_shader_engines;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	mutex_lock(&adev->grbm_idx_mutex);
	for (count = 0; count < num_se; count++) {
		data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK | GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK | (count << GRBM_GFX_INDEX__SE_INDEX__SHIFT);
		WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, data);

		result =  vega10_program_didt_config_registers(hwmgr, SEDiDtStallCtrlConfig_vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtStallPatternConfig_vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtWeightConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtCtrl1Config_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtCtrl2Config_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtCtrl3Config_vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtTuningCtrlConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SELCacConfig_Vega10, VEGA10_CONFIGREG_SECAC);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtCtrl0Config_Vega10, VEGA10_CONFIGREG_DIDT);

		if (0 != result)
			break;
	}
	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xE0000000);
	mutex_unlock(&adev->grbm_idx_mutex);

	vega10_didt_set_mask(hwmgr, true);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	return 0;
}

static int vega10_disable_cac_driving_se_didt_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	vega10_didt_set_mask(hwmgr, false);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	return 0;
}

static int vega10_enable_psm_gc_didt_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result;
	uint32_t num_se = 0, count, data;

	num_se = adev->gfx.config.max_shader_engines;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	mutex_lock(&adev->grbm_idx_mutex);
	for (count = 0; count < num_se; count++) {
		data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK | GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK | (count << GRBM_GFX_INDEX__SE_INDEX__SHIFT);
		WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, data);

		result = vega10_program_didt_config_registers(hwmgr, SEDiDtStallCtrlConfig_vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtStallPatternConfig_vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtCtrl3Config_vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEDiDtCtrl0Config_Vega10, VEGA10_CONFIGREG_DIDT);
		if (0 != result)
			break;
	}
	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xE0000000);
	mutex_unlock(&adev->grbm_idx_mutex);

	vega10_didt_set_mask(hwmgr, true);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	vega10_program_gc_didt_config_registers(hwmgr, GCDiDtDroopCtrlConfig_vega10);
	if (PP_CAP(PHM_PlatformCaps_GCEDC))
		vega10_program_gc_didt_config_registers(hwmgr, GCDiDtCtrl0Config_vega10);

	if (PP_CAP(PHM_PlatformCaps_PSM))
		vega10_program_gc_didt_config_registers(hwmgr,  AvfsPSMInitConfig_vega10);

	return 0;
}

static int vega10_disable_psm_gc_didt_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t data;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	vega10_didt_set_mask(hwmgr, false);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	if (PP_CAP(PHM_PlatformCaps_GCEDC)) {
		data = 0x00000000;
		cgs_write_register(hwmgr->device, mmGC_DIDT_CTRL0, data);
	}

	if (PP_CAP(PHM_PlatformCaps_PSM))
		vega10_program_gc_didt_config_registers(hwmgr,  AvfsPSMResetConfig_vega10);

	return 0;
}

static int vega10_enable_se_edc_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result;
	uint32_t num_se = 0, count, data;

	num_se = adev->gfx.config.max_shader_engines;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	mutex_lock(&adev->grbm_idx_mutex);
	for (count = 0; count < num_se; count++) {
		data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK | GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK | (count << GRBM_GFX_INDEX__SE_INDEX__SHIFT);
		WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, data);
		result = vega10_program_didt_config_registers(hwmgr, SEDiDtWeightConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEEDCStallPatternConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEEDCStallDelayConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEEDCThresholdConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEEDCCtrlResetConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, SEEDCCtrlConfig_Vega10, VEGA10_CONFIGREG_DIDT);

		if (0 != result)
			break;
	}
	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xE0000000);
	mutex_unlock(&adev->grbm_idx_mutex);

	vega10_didt_set_mask(hwmgr, true);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	return 0;
}

static int vega10_disable_se_edc_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	vega10_didt_set_mask(hwmgr, false);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	return 0;
}

static int vega10_enable_psm_gc_edc_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result = 0;
	uint32_t num_se = 0;
	uint32_t count, data;

	num_se = adev->gfx.config.max_shader_engines;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	vega10_program_gc_didt_config_registers(hwmgr, AvfsPSMResetConfig_vega10);

	mutex_lock(&adev->grbm_idx_mutex);
	for (count = 0; count < num_se; count++) {
		data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK | GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK | (count << GRBM_GFX_INDEX__SE_INDEX__SHIFT);
		WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, data);
		result = vega10_program_didt_config_registers(hwmgr, PSMSEEDCStallPatternConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, PSMSEEDCStallDelayConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, PSMSEEDCCtrlResetConfig_Vega10, VEGA10_CONFIGREG_DIDT);
		result |= vega10_program_didt_config_registers(hwmgr, PSMSEEDCCtrlConfig_Vega10, VEGA10_CONFIGREG_DIDT);

		if (0 != result)
			break;
	}
	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xE0000000);
	mutex_unlock(&adev->grbm_idx_mutex);

	vega10_didt_set_mask(hwmgr, true);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	vega10_program_gc_didt_config_registers(hwmgr, PSMGCEDCDroopCtrlConfig_vega10);

	if (PP_CAP(PHM_PlatformCaps_GCEDC)) {
		vega10_program_gc_didt_config_registers(hwmgr, PSMGCEDCCtrlResetConfig_vega10);
		vega10_program_gc_didt_config_registers(hwmgr, PSMGCEDCCtrlConfig_vega10);
	}

	if (PP_CAP(PHM_PlatformCaps_PSM))
		vega10_program_gc_didt_config_registers(hwmgr,  AvfsPSMInitConfig_vega10);

	return 0;
}

static int vega10_disable_psm_gc_edc_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t data;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	vega10_didt_set_mask(hwmgr, false);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	if (PP_CAP(PHM_PlatformCaps_GCEDC)) {
		data = 0x00000000;
		cgs_write_register(hwmgr->device, mmGC_EDC_CTRL, data);
	}

	if (PP_CAP(PHM_PlatformCaps_PSM))
		vega10_program_gc_didt_config_registers(hwmgr,  AvfsPSMResetConfig_vega10);

	return 0;
}

static int vega10_enable_se_edc_force_stall_config(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	mutex_lock(&adev->grbm_idx_mutex);
	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xE0000000);
	mutex_unlock(&adev->grbm_idx_mutex);

	result = vega10_program_didt_config_registers(hwmgr, SEEDCForceStallPatternConfig_Vega10, VEGA10_CONFIGREG_DIDT);
	result |= vega10_program_didt_config_registers(hwmgr, SEEDCCtrlForceStallConfig_Vega10, VEGA10_CONFIGREG_DIDT);
	if (0 != result)
		goto exit_safe_mode;

	vega10_didt_set_mask(hwmgr, false);

exit_safe_mode:
	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	return result;
}

static int vega10_disable_se_edc_force_stall_config(struct pp_hwmgr *hwmgr)
{
	int result;

	result = vega10_disable_se_edc_config(hwmgr);
	PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDtConfig] Pre DIDT disable clock gating failed!", return result);

	return 0;
}

int vega10_enable_didt_config(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_DIDT].supported) {
		if (data->smu_features[GNLD_DIDT].enabled)
			PP_DBG_LOG("[EnableDiDtConfig] Feature DiDt Already enabled!\n");

		switch (data->registry_data.didt_mode) {
		case 0:
			result = vega10_enable_cac_driving_se_didt_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[EnableDiDt] Attempt to enable DiDt Mode 0 Failed!", return result);
			break;
		case 2:
			result = vega10_enable_psm_gc_didt_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[EnableDiDt] Attempt to enable DiDt Mode 2 Failed!", return result);
			break;
		case 3:
			result = vega10_enable_se_edc_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[EnableDiDt] Attempt to enable DiDt Mode 3 Failed!", return result);
			break;
		case 1:
		case 4:
		case 5:
			result = vega10_enable_psm_gc_edc_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[EnableDiDt] Attempt to enable DiDt Mode 5 Failed!", return result);
			break;
		case 6:
			result = vega10_enable_se_edc_force_stall_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[EnableDiDt] Attempt to enable DiDt Mode 6 Failed!", return result);
			break;
		default:
			result = -EINVAL;
			break;
		}

		if (0 == result) {
			result = vega10_enable_smc_features(hwmgr, true, data->smu_features[GNLD_DIDT].smu_feature_bitmap);
			PP_ASSERT_WITH_CODE((0 == result), "[EnableDiDtConfig] Attempt to Enable DiDt feature Failed!", return result);
			data->smu_features[GNLD_DIDT].enabled = true;
		}
	}

	return result;
}

int vega10_disable_didt_config(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_DIDT].supported) {
		if (!data->smu_features[GNLD_DIDT].enabled)
			PP_DBG_LOG("[DisableDiDtConfig] Feature DiDt Already Disabled!\n");

		switch (data->registry_data.didt_mode) {
		case 0:
			result = vega10_disable_cac_driving_se_didt_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDt] Attempt to disable DiDt Mode 0 Failed!", return result);
			break;
		case 2:
			result = vega10_disable_psm_gc_didt_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDt] Attempt to disable DiDt Mode 2 Failed!", return result);
			break;
		case 3:
			result = vega10_disable_se_edc_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDt] Attempt to disable DiDt Mode 3 Failed!", return result);
			break;
		case 1:
		case 4:
		case 5:
			result = vega10_disable_psm_gc_edc_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDt] Attempt to disable DiDt Mode 5 Failed!", return result);
			break;
		case 6:
			result = vega10_disable_se_edc_force_stall_config(hwmgr);
			PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDt] Attempt to disable DiDt Mode 6 Failed!", return result);
			break;
		default:
			result = -EINVAL;
			break;
		}

		if (0 == result) {
			result = vega10_enable_smc_features(hwmgr, false, data->smu_features[GNLD_DIDT].smu_feature_bitmap);
			PP_ASSERT_WITH_CODE((0 == result), "[DisableDiDtConfig] Attempt to Disable DiDt feature Failed!", return result);
			data->smu_features[GNLD_DIDT].enabled = false;
		}
	}

	return result;
}

void vega10_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_tdp_table *tdp_table = table_info->tdp_table;
	PPTable_t *table = &(data->smc_state_table.pp_table);

	table->SocketPowerLimit = cpu_to_le16(
			tdp_table->usMaximumPowerDeliveryLimit);
	table->TdcLimit = cpu_to_le16(tdp_table->usTDC);
	table->EdcLimit = cpu_to_le16(tdp_table->usEDCLimit);
	table->TedgeLimit = cpu_to_le16(tdp_table->usTemperatureLimitTedge);
	table->ThotspotLimit = cpu_to_le16(tdp_table->usTemperatureLimitHotspot);
	table->ThbmLimit = cpu_to_le16(tdp_table->usTemperatureLimitHBM);
	table->Tvr_socLimit = cpu_to_le16(tdp_table->usTemperatureLimitVrVddc);
	table->Tvr_memLimit = cpu_to_le16(tdp_table->usTemperatureLimitVrMvdd);
	table->Tliquid1Limit = cpu_to_le16(tdp_table->usTemperatureLimitLiquid1);
	table->Tliquid2Limit = cpu_to_le16(tdp_table->usTemperatureLimitLiquid2);
	table->TplxLimit = cpu_to_le16(tdp_table->usTemperatureLimitPlx);
	table->LoadLineResistance =
			hwmgr->platform_descriptor.LoadLineSlope * 256;
	table->FitLimit = 0; /* Not used for Vega10 */

	table->Liquid1_I2C_address = tdp_table->ucLiquid1_I2C_address;
	table->Liquid2_I2C_address = tdp_table->ucLiquid2_I2C_address;
	table->Vr_I2C_address = tdp_table->ucVr_I2C_address;
	table->Plx_I2C_address = tdp_table->ucPlx_I2C_address;

	table->Liquid_I2C_LineSCL = tdp_table->ucLiquid_I2C_Line;
	table->Liquid_I2C_LineSDA = tdp_table->ucLiquid_I2C_LineSDA;

	table->Vr_I2C_LineSCL = tdp_table->ucVr_I2C_Line;
	table->Vr_I2C_LineSDA = tdp_table->ucVr_I2C_LineSDA;

	table->Plx_I2C_LineSCL = tdp_table->ucPlx_I2C_Line;
	table->Plx_I2C_LineSDA = tdp_table->ucPlx_I2C_LineSDA;
}

int vega10_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->registry_data.enable_pkg_pwr_tracking_feature)
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetPptLimit, n,
				NULL);

	return 0;
}

int vega10_enable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_tdp_table *tdp_table = table_info->tdp_table;
	int result = 0;

	hwmgr->default_power_limit = hwmgr->power_limit =
			(uint32_t)(tdp_table->usMaximumPowerDeliveryLimit);

	if (!hwmgr->not_vf)
		return 0;

	if (PP_CAP(PHM_PlatformCaps_PowerContainment)) {
		if (data->smu_features[GNLD_PPT].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					true, data->smu_features[GNLD_PPT].smu_feature_bitmap),
					"Attempt to enable PPT feature Failed!",
					data->smu_features[GNLD_PPT].supported = false);

		if (data->smu_features[GNLD_TDC].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					true, data->smu_features[GNLD_TDC].smu_feature_bitmap),
					"Attempt to enable PPT feature Failed!",
					data->smu_features[GNLD_TDC].supported = false);

		result = vega10_set_power_limit(hwmgr, hwmgr->power_limit);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to set Default Power Limit in SMC!",
				return result);
	}

	return result;
}

int vega10_disable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (PP_CAP(PHM_PlatformCaps_PowerContainment)) {
		if (data->smu_features[GNLD_PPT].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					false, data->smu_features[GNLD_PPT].smu_feature_bitmap),
					"Attempt to disable PPT feature Failed!",
					data->smu_features[GNLD_PPT].supported = false);

		if (data->smu_features[GNLD_TDC].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					false, data->smu_features[GNLD_TDC].smu_feature_bitmap),
					"Attempt to disable PPT feature Failed!",
					data->smu_features[GNLD_TDC].supported = false);
	}

	return 0;
}

static void vega10_set_overdrive_target_percentage(struct pp_hwmgr *hwmgr,
		uint32_t adjust_percent)
{
	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_OverDriveSetPercentage, adjust_percent,
			NULL);
}

int vega10_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	int adjust_percent;

	if (PP_CAP(PHM_PlatformCaps_PowerContainment)) {
		adjust_percent =
				hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		vega10_set_overdrive_target_percentage(hwmgr,
				(uint32_t)adjust_percent);
	}
	return 0;
}
