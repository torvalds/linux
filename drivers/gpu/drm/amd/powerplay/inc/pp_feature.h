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

#ifndef _PP_FEATURE_H_
#define _PP_FEATURE_H_

/**
 * PowerPlay feature ids.
 */
enum pp_feature {
	PP_Feature_PowerPlay = 0,
	PP_Feature_User2DPerformance,
	PP_Feature_User3DPerformance,
	PP_Feature_VariBright,
	PP_Feature_VariBrightOnPowerXpress,
	PP_Feature_ReducedRefreshRate,
	PP_Feature_GFXClockGating,
	PP_Feature_OverdriveTest,
	PP_Feature_OverDrive,
	PP_Feature_PowerBudgetWaiver,
	PP_Feature_PowerControl,
	PP_Feature_PowerControl_2,
	PP_Feature_MultiUVDState,
	PP_Feature_Force3DClock,
	PP_Feature_BACO,
	PP_Feature_PowerDown,
	PP_Feature_DynamicUVDState,
	PP_Feature_VCEDPM,
	PP_Feature_PPM,
	PP_Feature_ACP_POWERGATING,
	PP_Feature_FFC,
	PP_Feature_FPS,
	PP_Feature_ViPG,
	PP_Feature_Max
};

/**
 * Struct for PowerPlay feature info.
 */
struct pp_feature_info {
	bool supported;               /* feature supported by PowerPlay */
	bool enabled;                 /* feature enabled in PowerPlay */
	bool enabled_default;        /* default enable status of the feature */
	uint32_t version;             /* feature version */
};

#endif /* _PP_FEATURE_H_ */
