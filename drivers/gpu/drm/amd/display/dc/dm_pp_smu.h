/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef DM_PP_SMU_IF__H
#define DM_PP_SMU_IF__H

/*
 * interface to PPLIB/SMU to setup clocks and pstate requirements on SoC
 */

typedef bool BOOLEAN;

enum pp_smu_ver {
	/*
	 * PP_SMU_INTERFACE_X should be interpreted as the interface defined
	 * starting from X, where X is some family of ASICs.  This is as
	 * opposed to interfaces used only for X.  There will be some degree
	 * of interface sharing between families of ASIcs.
	 */
	PP_SMU_UNSUPPORTED,
	PP_SMU_VER_RV,
#ifndef CONFIG_TRIM_DRM_AMD_DC_DCN2_0
	PP_SMU_VER_NV,
#endif

	PP_SMU_VER_MAX
};

struct pp_smu {
	enum pp_smu_ver ver;
	const void *pp;

	/*
	 * interim extra handle for backwards compatibility
	 * as some existing functionality not yet implemented
	 * by ppsmu
	 */
	const void *dm;
};

enum pp_smu_status {
	PP_SMU_RESULT_UNDEFINED = 0,
	PP_SMU_RESULT_OK = 1,
	PP_SMU_RESULT_FAIL,
	PP_SMU_RESULT_UNSUPPORTED
};

#define PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN 0x0
#define PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX 0xFFFF

enum wm_type {
	WM_TYPE_PSTATE_CHG = 0,
	WM_TYPE_RETRAINING = 1,
};

/* This structure is a copy of WatermarkRowGeneric_t defined by smuxx_driver_if.h*/
struct pp_smu_wm_set_range {
	uint16_t min_fill_clk_mhz;
	uint16_t max_fill_clk_mhz;
	uint16_t min_drain_clk_mhz;
	uint16_t max_drain_clk_mhz;

	uint8_t wm_inst;
	uint8_t wm_type;
};

#define MAX_WATERMARK_SETS 4

struct pp_smu_wm_range_sets {
	unsigned int num_reader_wm_sets;
	struct pp_smu_wm_set_range reader_wm_sets[MAX_WATERMARK_SETS];

	unsigned int num_writer_wm_sets;
	struct pp_smu_wm_set_range writer_wm_sets[MAX_WATERMARK_SETS];
};

struct pp_smu_funcs_rv {
	struct pp_smu pp_smu;

	/* PPSMC_MSG_SetDisplayCount
	 * 0 triggers S0i2 optimization
	 */

	void (*set_display_count)(struct pp_smu *pp, int count);

	/* reader and writer WM's are sent together as part of one table*/
	/*
	 * PPSMC_MSG_SetDriverDramAddrHigh
	 * PPSMC_MSG_SetDriverDramAddrLow
	 * PPSMC_MSG_TransferTableDram2Smu
	 *
	 * */
	void (*set_wm_ranges)(struct pp_smu *pp,
			struct pp_smu_wm_range_sets *ranges);

	/* PPSMC_MSG_SetHardMinDcfclkByFreq
	 * fixed clock at requested freq, either from FCH bypass or DFS
	 */
	void (*set_hard_min_dcfclk_by_freq)(struct pp_smu *pp, int mhz);

	/* PPSMC_MSG_SetMinDeepSleepDcfclk
	 * when DF is in cstate, dcf clock is further divided down
	 * to just above given frequency
	 */
	void (*set_min_deep_sleep_dcfclk)(struct pp_smu *pp, int mhz);

	/* PPSMC_MSG_SetHardMinFclkByFreq
	 * FCLK will vary with DPM, but never below requested hard min
	 */
	void (*set_hard_min_fclk_by_freq)(struct pp_smu *pp, int mhz);

	/* PPSMC_MSG_SetHardMinSocclkByFreq
	 * Needed for DWB support
	 */
	void (*set_hard_min_socclk_by_freq)(struct pp_smu *pp, int mhz);

	/* PME w/a */
	void (*set_pme_wa_enable)(struct pp_smu *pp);
};

#ifndef CONFIG_TRIM_DRM_AMD_DC_DCN2_0
/* Used by pp_smu_funcs_nv.set_voltage_by_freq
 *
 */
enum pp_smu_nv_clock_id {
	PP_SMU_NV_DISPCLK,
	PP_SMU_NV_PHYCLK,
	PP_SMU_NV_PIXELCLK
};

/*
 * Used by pp_smu_funcs_nv.get_maximum_sustainable_clocks
 */
struct pp_smu_nv_clock_table {
	// voltage managed SMU, freq set by driver
	unsigned int    displayClockInKhz;
	unsigned int	dppClockInKhz;
	unsigned int    phyClockInKhz;
	unsigned int    pixelClockInKhz;
	unsigned int	dscClockInKhz;

	// freq/voltage managed by SMU
	unsigned int	fabricClockInKhz;
	unsigned int	socClockInKhz;
	unsigned int    dcfClockInKhz;
	unsigned int    uClockInKhz;
};

struct pp_smu_funcs_nv {
	struct pp_smu pp_smu;

	/* PPSMC_MSG_SetDisplayCount
	 * 0 triggers S0i2 optimization
	 */
	enum pp_smu_status (*set_display_count)(struct pp_smu *pp, int count);

	/* PPSMC_MSG_SetHardMinDcfclkByFreq
	 * fixed clock at requested freq, either from FCH bypass or DFS
	 */
	enum pp_smu_status (*set_hard_min_dcfclk_by_freq)(struct pp_smu *pp, int Mhz);

	/* PPSMC_MSG_SetMinDeepSleepDcfclk
	 * when DF is in cstate, dcf clock is further divided down
	 * to just above given frequency
	 */
	enum pp_smu_status (*set_min_deep_sleep_dcfclk)(struct pp_smu *pp, int Mhz);

	/* PPSMC_MSG_SetHardMinUclkByFreq
	 * UCLK will vary with DPM, but never below requested hard min
	 */
	enum pp_smu_status (*set_hard_min_uclk_by_freq)(struct pp_smu *pp, int Mhz);

	/* PPSMC_MSG_SetHardMinSocclkByFreq
	 * Needed for DWB support
	 */
	enum pp_smu_status (*set_hard_min_socclk_by_freq)(struct pp_smu *pp, int Mhz);

	/* PME w/a */
	enum pp_smu_status (*set_pme_wa_enable)(struct pp_smu *pp);

	/* PPSMC_MSG_SetHardMinByFreq
	 * Needed to set ASIC voltages for clocks programmed by DAL
	 */
	enum pp_smu_status (*set_voltage_by_freq)(struct pp_smu *pp,
			enum pp_smu_nv_clock_id clock_id, int Mhz);

	/* reader and writer WM's are sent together as part of one table*/
	/*
	 * PPSMC_MSG_SetDriverDramAddrHigh
	 * PPSMC_MSG_SetDriverDramAddrLow
	 * PPSMC_MSG_TransferTableDram2Smu
	 *
	 * on DCN20:
	 * 	reader fill clk = uclk
	 * 	reader drain clk = dcfclk
	 * 	writer fill clk = socclk
	 * 	writer drain clk = uclk
	 * */
	enum pp_smu_status (*set_wm_ranges)(struct pp_smu *pp,
			struct pp_smu_wm_range_sets *ranges);

	/* Not a single SMU message.  This call should return maximum sustainable limit for all
	 * clocks that DC depends on.  These will be used as basis for mode enumeration.
	 */
	enum pp_smu_status (*get_maximum_sustainable_clocks)(struct pp_smu *pp,
			struct pp_smu_nv_clock_table *max_clocks);

	/* This call should return the discrete uclk DPM states available
	 */
	enum pp_smu_status (*get_uclk_dpm_states)(struct pp_smu *pp,
			unsigned int *clock_values_in_khz, unsigned int *num_states);

	/* Not a single SMU message.  This call informs PPLIB that display will not be able
	 * to perform pstate handshaking in its current state.  Typically this handshake
	 * is used to perform uCLK switching, so disabling pstate disables uCLK switching.
	 *
	 * Note that when setting handshake to unsupported, the call is pre-emptive.  That means
	 * DC will make the call BEFORE setting up the display state which would cause pstate
	 * request to go un-acked.  Only when the call completes should such a state be applied to
	 * DC hardware
	 */
	enum pp_smu_status (*set_pstate_handshake_support)(struct pp_smu *pp,
			BOOLEAN pstate_handshake_supported);
};
#endif

struct pp_smu_funcs {
	struct pp_smu ctx;
	union {
		struct pp_smu_funcs_rv rv_funcs;
#ifndef CONFIG_TRIM_DRM_AMD_DC_DCN2_0
		struct pp_smu_funcs_nv nv_funcs;
#endif

	};
};

#endif /* DM_PP_SMU_IF__H */
