/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
#ifndef __R600_DPM_H__
#define __R600_DPM_H__

#define R600_ASI_DFLT                                10000
#define R600_BSP_DFLT                                0x41EB
#define R600_BSU_DFLT                                0x2
#define R600_AH_DFLT                                 5
#define R600_RLP_DFLT                                25
#define R600_RMP_DFLT                                65
#define R600_LHP_DFLT                                40
#define R600_LMP_DFLT                                15
#define R600_TD_DFLT                                 0
#define R600_UTC_DFLT_00                             0x24
#define R600_UTC_DFLT_01                             0x22
#define R600_UTC_DFLT_02                             0x22
#define R600_UTC_DFLT_03                             0x22
#define R600_UTC_DFLT_04                             0x22
#define R600_UTC_DFLT_05                             0x22
#define R600_UTC_DFLT_06                             0x22
#define R600_UTC_DFLT_07                             0x22
#define R600_UTC_DFLT_08                             0x22
#define R600_UTC_DFLT_09                             0x22
#define R600_UTC_DFLT_10                             0x22
#define R600_UTC_DFLT_11                             0x22
#define R600_UTC_DFLT_12                             0x22
#define R600_UTC_DFLT_13                             0x22
#define R600_UTC_DFLT_14                             0x22
#define R600_DTC_DFLT_00                             0x24
#define R600_DTC_DFLT_01                             0x22
#define R600_DTC_DFLT_02                             0x22
#define R600_DTC_DFLT_03                             0x22
#define R600_DTC_DFLT_04                             0x22
#define R600_DTC_DFLT_05                             0x22
#define R600_DTC_DFLT_06                             0x22
#define R600_DTC_DFLT_07                             0x22
#define R600_DTC_DFLT_08                             0x22
#define R600_DTC_DFLT_09                             0x22
#define R600_DTC_DFLT_10                             0x22
#define R600_DTC_DFLT_11                             0x22
#define R600_DTC_DFLT_12                             0x22
#define R600_DTC_DFLT_13                             0x22
#define R600_DTC_DFLT_14                             0x22
#define R600_VRC_DFLT                                0x0000C003
#define R600_VOLTAGERESPONSETIME_DFLT                1000
#define R600_BACKBIASRESPONSETIME_DFLT               1000
#define R600_VRU_DFLT                                0x3
#define R600_SPLLSTEPTIME_DFLT                       0x1000
#define R600_SPLLSTEPUNIT_DFLT                       0x3
#define R600_TPU_DFLT                                0
#define R600_TPC_DFLT                                0x200
#define R600_SSTU_DFLT                               0
#define R600_SST_DFLT                                0x00C8
#define R600_GICST_DFLT                              0x200
#define R600_FCT_DFLT                                0x0400
#define R600_FCTU_DFLT                               0
#define R600_CTXCGTT3DRPHC_DFLT                      0x20
#define R600_CTXCGTT3DRSDC_DFLT                      0x40
#define R600_VDDC3DOORPHC_DFLT                       0x100
#define R600_VDDC3DOORSDC_DFLT                       0x7
#define R600_VDDC3DOORSU_DFLT                        0
#define R600_MPLLLOCKTIME_DFLT                       100
#define R600_MPLLRESETTIME_DFLT                      150
#define R600_VCOSTEPPCT_DFLT                          20
#define R600_ENDINGVCOSTEPPCT_DFLT                    5
#define R600_REFERENCEDIVIDER_DFLT                    4

#define R600_PM_NUMBER_OF_TC 15
#define R600_PM_NUMBER_OF_SCLKS 20
#define R600_PM_NUMBER_OF_MCLKS 4
#define R600_PM_NUMBER_OF_VOLTAGE_LEVELS 4
#define R600_PM_NUMBER_OF_ACTIVITY_LEVELS 3

/* XXX are these ok? */
#define R600_TEMP_RANGE_MIN (90 * 1000)
#define R600_TEMP_RANGE_MAX (120 * 1000)

#define FDO_PWM_MODE_STATIC  1
#define FDO_PWM_MODE_STATIC_RPM 5

enum r600_power_level {
	R600_POWER_LEVEL_LOW = 0,
	R600_POWER_LEVEL_MEDIUM = 1,
	R600_POWER_LEVEL_HIGH = 2,
	R600_POWER_LEVEL_CTXSW = 3,
};

enum r600_td {
	R600_TD_AUTO,
	R600_TD_UP,
	R600_TD_DOWN,
};

enum r600_display_watermark {
	R600_DISPLAY_WATERMARK_LOW = 0,
	R600_DISPLAY_WATERMARK_HIGH = 1,
};

enum r600_display_gap
{
    R600_PM_DISPLAY_GAP_VBLANK_OR_WM = 0,
    R600_PM_DISPLAY_GAP_VBLANK       = 1,
    R600_PM_DISPLAY_GAP_WATERMARK    = 2,
    R600_PM_DISPLAY_GAP_IGNORE       = 3,
};
#endif
