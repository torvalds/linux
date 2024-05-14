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
#ifndef PP_POWERSTATE_H
#define PP_POWERSTATE_H

struct pp_hw_power_state {
	unsigned int magic;
};

struct pp_power_state;


#define PP_INVALID_POWER_STATE_ID (0)


/*
 * An item of a list containing Power States.
 */

struct PP_StateLinkedList {
	struct pp_power_state *next;
	struct pp_power_state *prev;
};


enum PP_StateUILabel {
	PP_StateUILabel_None,
	PP_StateUILabel_Battery,
	PP_StateUILabel_MiddleLow,
	PP_StateUILabel_Balanced,
	PP_StateUILabel_MiddleHigh,
	PP_StateUILabel_Performance,
	PP_StateUILabel_BACO
};

enum PP_StateClassificationFlag {
	PP_StateClassificationFlag_Boot                = 0x0001,
	PP_StateClassificationFlag_Thermal             = 0x0002,
	PP_StateClassificationFlag_LimitedPowerSource  = 0x0004,
	PP_StateClassificationFlag_Rest                = 0x0008,
	PP_StateClassificationFlag_Forced              = 0x0010,
	PP_StateClassificationFlag_User3DPerformance   = 0x0020,
	PP_StateClassificationFlag_User2DPerformance   = 0x0040,
	PP_StateClassificationFlag_3DPerformance       = 0x0080,
	PP_StateClassificationFlag_ACOverdriveTemplate   = 0x0100,
	PP_StateClassificationFlag_Uvd                 = 0x0200,
	PP_StateClassificationFlag_3DPerformanceLow    = 0x0400,
	PP_StateClassificationFlag_ACPI                = 0x0800,
	PP_StateClassificationFlag_HD2                 = 0x1000,
	PP_StateClassificationFlag_UvdHD               = 0x2000,
	PP_StateClassificationFlag_UvdSD               = 0x4000,
	PP_StateClassificationFlag_UserDCPerformance    = 0x8000,
	PP_StateClassificationFlag_DCOverdriveTemplate   = 0x10000,
	PP_StateClassificationFlag_BACO                  = 0x20000,
	PP_StateClassificationFlag_LimitedPowerSource_2  = 0x40000,
	PP_StateClassificationFlag_ULV                   = 0x80000,
	PP_StateClassificationFlag_UvdMVC               = 0x100000,
};

typedef unsigned int PP_StateClassificationFlags;

struct PP_StateClassificationBlock {
	enum PP_StateUILabel         ui_label;
	enum PP_StateClassificationFlag  flags;
	int                          bios_index;
	bool                      temporary_state;
	bool                      to_be_deleted;
};

struct PP_StatePcieBlock {
	unsigned int lanes;
};

enum PP_RefreshrateSource {
	PP_RefreshrateSource_EDID,
	PP_RefreshrateSource_Explicit
};

struct PP_StateDisplayBlock {
	bool              disableFrameModulation;
	bool              limitRefreshrate;
	enum PP_RefreshrateSource refreshrateSource;
	int                  explicitRefreshrate;
	int                  edidRefreshrateIndex;
	bool              enableVariBright;
};

struct PP_StateMemroyBlock {
	bool              dllOff;
	uint8_t                 m3arb;
	uint8_t                 unused[3];
};

struct PP_StateSoftwareAlgorithmBlock {
	bool disableLoadBalancing;
	bool enableSleepForTimestamps;
};

#define PP_TEMPERATURE_UNITS_PER_CENTIGRADES 1000

/**
 * Type to hold a temperature range.
 */
struct PP_TemperatureRange {
	int min;
	int max;
	int edge_emergency_max;
	int hotspot_min;
	int hotspot_crit_max;
	int hotspot_emergency_max;
	int mem_min;
	int mem_crit_max;
	int mem_emergency_max;
	int sw_ctf_threshold;
};

struct PP_StateValidationBlock {
	bool singleDisplayOnly;
	bool disallowOnDC;
	uint8_t supportedPowerLevels;
};

struct PP_UVD_CLOCKS {
	uint32_t VCLK;
	uint32_t DCLK;
};

/**
* Structure to hold a PowerPlay Power State.
*/
struct pp_power_state {
	uint32_t                            id;
	struct PP_StateLinkedList                  orderedList;
	struct PP_StateLinkedList                  allStatesList;

	struct PP_StateClassificationBlock         classification;
	struct PP_StateValidationBlock             validation;
	struct PP_StatePcieBlock                   pcie;
	struct PP_StateDisplayBlock                display;
	struct PP_StateMemroyBlock                 memory;
	struct PP_TemperatureRange                 temperatures;
	struct PP_StateSoftwareAlgorithmBlock      software;
	struct PP_UVD_CLOCKS                       uvd_clocks;
	struct pp_hw_power_state  hardware;
};

enum PP_MMProfilingState {
	PP_MMProfilingState_NA = 0,
	PP_MMProfilingState_Started,
	PP_MMProfilingState_Stopped
};

struct pp_clock_engine_request {
	unsigned long client_type;
	unsigned long ctx_id;
	uint64_t  context_handle;
	unsigned long sclk;
	unsigned long sclk_hard_min;
	unsigned long mclk;
	unsigned long iclk;
	unsigned long evclk;
	unsigned long ecclk;
	unsigned long ecclk_hard_min;
	unsigned long vclk;
	unsigned long dclk;
	unsigned long sclk_over_drive;
	unsigned long mclk_over_drive;
	unsigned long sclk_threshold;
	unsigned long flag;
	unsigned long vclk_ceiling;
	unsigned long dclk_ceiling;
	unsigned long num_cus;
	unsigned long pm_flag;
	enum PP_MMProfilingState mm_profiling_state;
};

#endif
