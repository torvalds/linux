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
#ifndef _HARDWARE_MANAGER_H_
#define _HARDWARE_MANAGER_H_



struct pp_hwmgr;
struct pp_hw_power_state;
struct pp_power_state;
enum amd_dpm_forced_level;
struct PP_TemperatureRange;


struct phm_fan_speed_info {
	uint32_t min_percent;
	uint32_t max_percent;
	uint32_t min_rpm;
	uint32_t max_rpm;
	bool supports_percent_read;
	bool supports_percent_write;
	bool supports_rpm_read;
	bool supports_rpm_write;
};

/* Automatic Power State Throttling */
enum PHM_AutoThrottleSource
{
    PHM_AutoThrottleSource_Thermal,
    PHM_AutoThrottleSource_External
};

typedef enum PHM_AutoThrottleSource PHM_AutoThrottleSource;

enum phm_platform_caps {
	PHM_PlatformCaps_AtomBiosPpV1 = 0,
	PHM_PlatformCaps_PowerPlaySupport,
	PHM_PlatformCaps_ACOverdriveSupport,
	PHM_PlatformCaps_BacklightSupport,
	PHM_PlatformCaps_ThermalController,
	PHM_PlatformCaps_BiosPowerSourceControl,
	PHM_PlatformCaps_DisableVoltageTransition,
	PHM_PlatformCaps_DisableEngineTransition,
	PHM_PlatformCaps_DisableMemoryTransition,
	PHM_PlatformCaps_DynamicPowerManagement,
	PHM_PlatformCaps_EnableASPML0s,
	PHM_PlatformCaps_EnableASPML1,
	PHM_PlatformCaps_OD5inACSupport,
	PHM_PlatformCaps_OD5inDCSupport,
	PHM_PlatformCaps_SoftStateOD5,
	PHM_PlatformCaps_NoOD5Support,
	PHM_PlatformCaps_ContinuousHardwarePerformanceRange,
	PHM_PlatformCaps_ActivityReporting,
	PHM_PlatformCaps_EnableBackbias,
	PHM_PlatformCaps_OverdriveDisabledByPowerBudget,
	PHM_PlatformCaps_ShowPowerBudgetWarning,
	PHM_PlatformCaps_PowerBudgetWaiverAvailable,
	PHM_PlatformCaps_GFXClockGatingSupport,
	PHM_PlatformCaps_MMClockGatingSupport,
	PHM_PlatformCaps_AutomaticDCTransition,
	PHM_PlatformCaps_GeminiPrimary,
	PHM_PlatformCaps_MemorySpreadSpectrumSupport,
	PHM_PlatformCaps_EngineSpreadSpectrumSupport,
	PHM_PlatformCaps_StepVddc,
	PHM_PlatformCaps_DynamicPCIEGen2Support,
	PHM_PlatformCaps_SMC,
	PHM_PlatformCaps_FaultyInternalThermalReading,          /* Internal thermal controller reports faulty temperature value when DAC2 is active */
	PHM_PlatformCaps_EnableVoltageControl,                  /* indicates voltage can be controlled */
	PHM_PlatformCaps_EnableSideportControl,                 /* indicates Sideport can be controlled */
	PHM_PlatformCaps_VideoPlaybackEEUNotification,          /* indicates EEU notification of video start/stop is required */
	PHM_PlatformCaps_TurnOffPll_ASPML1,                     /* PCIE Turn Off PLL in ASPM L1 */
	PHM_PlatformCaps_EnableHTLinkControl,                   /* indicates HT Link can be controlled by ACPI or CLMC overrided/automated mode. */
	PHM_PlatformCaps_PerformanceStateOnly,                  /* indicates only performance power state to be used on current system. */
	PHM_PlatformCaps_ExclusiveModeAlwaysHigh,               /* In Exclusive (3D) mode always stay in High state. */
	PHM_PlatformCaps_DisableMGClockGating,                  /* to disable Medium Grain Clock Gating or not */
	PHM_PlatformCaps_DisableMGCGTSSM,                       /* TO disable Medium Grain Clock Gating Shader Complex control */
	PHM_PlatformCaps_UVDAlwaysHigh,                         /* In UVD mode always stay in High state */
	PHM_PlatformCaps_DisablePowerGating,                    /* to disable power gating */
	PHM_PlatformCaps_CustomThermalPolicy,                   /* indicates only performance power state to be used on current system. */
	PHM_PlatformCaps_StayInBootState,                       /* Stay in Boot State, do not do clock/voltage or PCIe Lane and Gen switching (RV7xx and up). */
	PHM_PlatformCaps_SMCAllowSeparateSWThermalState,        /* SMC use separate SW thermal state, instead of the default SMC thermal policy. */
	PHM_PlatformCaps_MultiUVDStateSupport,                  /* Powerplay state table supports multi UVD states. */
	PHM_PlatformCaps_EnableSCLKDeepSleepForUVD,             /* With HW ECOs, we don't need to disable SCLK Deep Sleep for UVD state. */
	PHM_PlatformCaps_EnableMCUHTLinkControl,                /* Enable HT link control by MCU */
	PHM_PlatformCaps_ABM,                                   /* ABM support.*/
	PHM_PlatformCaps_KongThermalPolicy,                     /* A thermal policy specific for Kong */
	PHM_PlatformCaps_SwitchVDDNB,                           /* if the users want to switch VDDNB */
	PHM_PlatformCaps_ULPS,                                  /* support ULPS mode either through ACPI state or ULPS state */
	PHM_PlatformCaps_NativeULPS,                            /* hardware capable of ULPS state (other than through the ACPI state) */
	PHM_PlatformCaps_EnableMVDDControl,                     /* indicates that memory voltage can be controlled */
	PHM_PlatformCaps_ControlVDDCI,                          /* Control VDDCI separately from VDDC. */
	PHM_PlatformCaps_DisableDCODT,                          /* indicates if DC ODT apply or not */
	PHM_PlatformCaps_DynamicACTiming,                       /* if the SMC dynamically re-programs MC SEQ register values */
	PHM_PlatformCaps_EnableThermalIntByGPIO,                /* enable throttle control through GPIO */
	PHM_PlatformCaps_BootStateOnAlert,                      /* Go to boot state on alerts, e.g. on an AC->DC transition. */
	PHM_PlatformCaps_DontWaitForVBlankOnAlert,              /* Do NOT wait for VBLANK during an alert (e.g. AC->DC transition). */
	PHM_PlatformCaps_Force3DClockSupport,                   /* indicates if the platform supports force 3D clock. */
	PHM_PlatformCaps_MicrocodeFanControl,                   /* Fan is controlled by the SMC microcode. */
	PHM_PlatformCaps_AdjustUVDPriorityForSP,
	PHM_PlatformCaps_DisableLightSleep,                     /* Light sleep for evergreen family. */
	PHM_PlatformCaps_DisableMCLS,                           /* MC Light sleep */
	PHM_PlatformCaps_RegulatorHot,                          /* Enable throttling on 'regulator hot' events. */
	PHM_PlatformCaps_BACO,                                  /* Support Bus Alive Chip Off mode */
	PHM_PlatformCaps_DisableDPM,                            /* Disable DPM, supported from Llano */
	PHM_PlatformCaps_DynamicM3Arbiter,                      /* support dynamically change m3 arbitor parameters */
	PHM_PlatformCaps_SclkDeepSleep,                         /* support sclk deep sleep */
	PHM_PlatformCaps_DynamicPatchPowerState,                /* this ASIC supports to patch power state dynamically */
	PHM_PlatformCaps_ThermalAutoThrottling,                 /* enabling auto thermal throttling, */
	PHM_PlatformCaps_SumoThermalPolicy,                     /* A thermal policy specific for Sumo */
	PHM_PlatformCaps_PCIEPerformanceRequest,                /* support to change RC voltage */
	PHM_PlatformCaps_BLControlledByGPU,                     /* support varibright */
	PHM_PlatformCaps_PowerContainment,                      /* support DPM2 power containment (AKA TDP clamping) */
	PHM_PlatformCaps_SQRamping,                             /* support DPM2 SQ power throttle */
	PHM_PlatformCaps_CAC,                                   /* support Capacitance * Activity power estimation */
	PHM_PlatformCaps_NIChipsets,                            /* Northern Island and beyond chipsets */
	PHM_PlatformCaps_TrinityChipsets,                       /* Trinity chipset */
	PHM_PlatformCaps_EvergreenChipsets,                     /* Evergreen family chipset */
	PHM_PlatformCaps_PowerControl,                          /* Cayman and beyond chipsets */
	PHM_PlatformCaps_DisableLSClockGating,                  /* to disable Light Sleep control for HDP memories */
	PHM_PlatformCaps_BoostState,                            /* this ASIC supports boost state */
	PHM_PlatformCaps_UserMaxClockForMultiDisplays,          /* indicates if max memory clock is used for all status when multiple displays are connected */
	PHM_PlatformCaps_RegWriteDelay,                         /* indicates if back to back reg write delay is required */
	PHM_PlatformCaps_NonABMSupportInPPLib,                  /* ABM is not supported in PPLIB, (moved from PPLIB to DAL) */
	PHM_PlatformCaps_GFXDynamicMGPowerGating,               /* Enable Dynamic MG PowerGating on Trinity */
	PHM_PlatformCaps_DisableSMUUVDHandshake,                /* Disable SMU UVD Handshake */
	PHM_PlatformCaps_DTE,                                   /* Support Digital Temperature Estimation */
	PHM_PlatformCaps_W5100Specifc_SmuSkipMsgDTE,            /* This is for the feature requested by David B., and Tonny W.*/
	PHM_PlatformCaps_UVDPowerGating,                        /* enable UVD power gating, supported from Llano */
	PHM_PlatformCaps_UVDDynamicPowerGating,                 /* enable UVD Dynamic power gating, supported from UVD5 */
	PHM_PlatformCaps_VCEPowerGating,                        /* Enable VCE power gating, supported for TN and later ASICs */
	PHM_PlatformCaps_SamuPowerGating,                       /* Enable SAMU power gating, supported for KV and later ASICs */
	PHM_PlatformCaps_UVDDPM,                                /* UVD clock DPM */
	PHM_PlatformCaps_VCEDPM,                                /* VCE clock DPM */
	PHM_PlatformCaps_SamuDPM,                               /* SAMU clock DPM */
	PHM_PlatformCaps_AcpDPM,                                /* ACP clock DPM */
	PHM_PlatformCaps_SclkDeepSleepAboveLow,                 /* Enable SCLK Deep Sleep on all DPM states */
	PHM_PlatformCaps_DynamicUVDState,                       /* Dynamic UVD State */
	PHM_PlatformCaps_WantSAMClkWithDummyBackEnd,            /* Set SAM Clk With Dummy Back End */
	PHM_PlatformCaps_WantUVDClkWithDummyBackEnd,            /* Set UVD Clk With Dummy Back End */
	PHM_PlatformCaps_WantVCEClkWithDummyBackEnd,            /* Set VCE Clk With Dummy Back End */
	PHM_PlatformCaps_WantACPClkWithDummyBackEnd,            /* Set SAM Clk With Dummy Back End */
	PHM_PlatformCaps_OD6inACSupport,                        /* indicates that the ASIC/back end supports OD6 */
	PHM_PlatformCaps_OD6inDCSupport,                        /* indicates that the ASIC/back end supports OD6 in DC */
	PHM_PlatformCaps_EnablePlatformPowerManagement,         /* indicates that Platform Power Management feature is supported */
	PHM_PlatformCaps_SurpriseRemoval,                       /* indicates that surprise removal feature is requested */
	PHM_PlatformCaps_NewCACVoltage,                         /* indicates new CAC voltage table support */
	PHM_PlatformCaps_DBRamping,                             /* for dI/dT feature */
	PHM_PlatformCaps_TDRamping,                             /* for dI/dT feature */
	PHM_PlatformCaps_TCPRamping,                            /* for dI/dT feature */
	PHM_PlatformCaps_EnableSMU7ThermalManagement,           /* SMC will manage thermal events */
	PHM_PlatformCaps_FPS,                                   /* FPS support */
	PHM_PlatformCaps_ACP,                                   /* ACP support */
	PHM_PlatformCaps_SclkThrottleLowNotification,           /* SCLK Throttle Low Notification */
	PHM_PlatformCaps_XDMAEnabled,                           /* XDMA engine is enabled */
	PHM_PlatformCaps_UseDummyBackEnd,                       /* use dummy back end */
	PHM_PlatformCaps_EnableDFSBypass,                       /* Enable DFS bypass */
	PHM_PlatformCaps_VddNBDirectRequest,
	PHM_PlatformCaps_PauseMMSessions,
	PHM_PlatformCaps_UnTabledHardwareInterface,             /* Tableless/direct call hardware interface for CI and newer ASICs */
	PHM_PlatformCaps_SMU7,                                  /* indicates that vpuRecoveryBegin without SMU shutdown */
	PHM_PlatformCaps_RevertGPIO5Polarity,                   /* indicates revert GPIO5 plarity table support */
	PHM_PlatformCaps_Thermal2GPIO17,                        /* indicates thermal2GPIO17 table support */
	PHM_PlatformCaps_ThermalOutGPIO,                        /* indicates ThermalOutGPIO support, pin number is assigned by VBIOS */
	PHM_PlatformCaps_DisableMclkSwitchingForFrameLock,      /* Disable memory clock switch during Framelock */
	PHM_PlatformCaps_VRHotGPIOConfigurable,                 /* indicates VR_HOT GPIO configurable */
	PHM_PlatformCaps_TempInversion,                         /* enable Temp Inversion feature */
	PHM_PlatformCaps_IOIC3,
	PHM_PlatformCaps_ConnectedStandby,
	PHM_PlatformCaps_EVV,
	PHM_PlatformCaps_EnableLongIdleBACOSupport,
	PHM_PlatformCaps_CombinePCCWithThermalSignal,
	PHM_PlatformCaps_DisableUsingActualTemperatureForPowerCalc,
	PHM_PlatformCaps_StablePState,
	PHM_PlatformCaps_OD6PlusinACSupport,
	PHM_PlatformCaps_OD6PlusinDCSupport,
	PHM_PlatformCaps_ODThermalLimitUnlock,
	PHM_PlatformCaps_ReducePowerLimit,
	PHM_PlatformCaps_ODFuzzyFanControlSupport,
	PHM_PlatformCaps_GeminiRegulatorFanControlSupport,
	PHM_PlatformCaps_ControlVDDGFX,
	PHM_PlatformCaps_BBBSupported,
	PHM_PlatformCaps_DisableVoltageIsland,
	PHM_PlatformCaps_FanSpeedInTableIsRPM,
	PHM_PlatformCaps_GFXClockGatingManagedInCAIL,
	PHM_PlatformCaps_IcelandULPSSWWorkAround,
	PHM_PlatformCaps_FPSEnhancement,
	PHM_PlatformCaps_LoadPostProductionFirmware,
	PHM_PlatformCaps_VpuRecoveryInProgress,
	PHM_PlatformCaps_Falcon_QuickTransition,
	PHM_PlatformCaps_AVFS,
	PHM_PlatformCaps_ClockStretcher,
	PHM_PlatformCaps_TablelessHardwareInterface,
	PHM_PlatformCaps_EnableDriverEVV,
	PHM_PlatformCaps_SPLLShutdownSupport,
	PHM_PlatformCaps_Max
};

#define PHM_MAX_NUM_CAPS_BITS_PER_FIELD (sizeof(uint32_t)*8)

/* Number of uint32_t entries used by CAPS table */
#define PHM_MAX_NUM_CAPS_ULONG_ENTRIES \
	((PHM_PlatformCaps_Max + ((PHM_MAX_NUM_CAPS_BITS_PER_FIELD) - 1)) / (PHM_MAX_NUM_CAPS_BITS_PER_FIELD))

struct pp_hw_descriptor {
	uint32_t hw_caps[PHM_MAX_NUM_CAPS_ULONG_ENTRIES];
};

enum PHM_PerformanceLevelDesignation {
	PHM_PerformanceLevelDesignation_Activity,
	PHM_PerformanceLevelDesignation_PowerContainment
};

typedef enum PHM_PerformanceLevelDesignation PHM_PerformanceLevelDesignation;

struct PHM_PerformanceLevel {
    uint32_t    coreClock;
    uint32_t    memory_clock;
    uint32_t  vddc;
    uint32_t  vddci;
    uint32_t    nonLocalMemoryFreq;
    uint32_t nonLocalMemoryWidth;
};

typedef struct PHM_PerformanceLevel PHM_PerformanceLevel;

/* Function for setting a platform cap */
static inline void phm_cap_set(uint32_t *caps,
			enum phm_platform_caps c)
{
	caps[c / PHM_MAX_NUM_CAPS_BITS_PER_FIELD] |= (1UL <<
			     (c & (PHM_MAX_NUM_CAPS_BITS_PER_FIELD - 1)));
}

static inline void phm_cap_unset(uint32_t *caps,
			enum phm_platform_caps c)
{
	caps[c / PHM_MAX_NUM_CAPS_BITS_PER_FIELD] &= ~(1UL << (c & (PHM_MAX_NUM_CAPS_BITS_PER_FIELD - 1)));
}

static inline bool phm_cap_enabled(const uint32_t *caps, enum phm_platform_caps c)
{
	return (0 != (caps[c / PHM_MAX_NUM_CAPS_BITS_PER_FIELD] &
		  (1UL << (c & (PHM_MAX_NUM_CAPS_BITS_PER_FIELD - 1)))));
}

#define PP_PCIEGenInvalid  0xffff
enum PP_PCIEGen {
    PP_PCIEGen1 = 0,                /* PCIE 1.0 - Transfer rate of 2.5 GT/s */
    PP_PCIEGen2,                    /*PCIE 2.0 - Transfer rate of 5.0 GT/s */
    PP_PCIEGen3                     /*PCIE 3.0 - Transfer rate of 8.0 GT/s */
};

typedef enum PP_PCIEGen PP_PCIEGen;

#define PP_Min_PCIEGen     PP_PCIEGen1
#define PP_Max_PCIEGen     PP_PCIEGen3
#define PP_Min_PCIELane    1
#define PP_Max_PCIELane    32

enum phm_clock_Type {
	PHM_DispClock = 1,
	PHM_SClock,
	PHM_MemClock
};

#define MAX_NUM_CLOCKS 16

struct PP_Clocks {
	uint32_t engineClock;
	uint32_t memoryClock;
	uint32_t BusBandwidth;
	uint32_t engineClockInSR;
};

struct pp_clock_info {
	uint32_t min_mem_clk;
	uint32_t max_mem_clk;
	uint32_t min_eng_clk;
	uint32_t max_eng_clk;
	uint32_t min_bus_bandwidth;
	uint32_t max_bus_bandwidth;
};

struct phm_platform_descriptor {
	uint32_t platformCaps[PHM_MAX_NUM_CAPS_ULONG_ENTRIES];
	uint32_t vbiosInterruptId;
	struct PP_Clocks overdriveLimit;
	struct PP_Clocks clockStep;
	uint32_t hardwareActivityPerformanceLevels;
	uint32_t minimumClocksReductionPercentage;
	uint32_t minOverdriveVDDC;
	uint32_t maxOverdriveVDDC;
	uint32_t overdriveVDDCStep;
	uint32_t hardwarePerformanceLevels;
	uint16_t powerBudget;
	uint32_t TDPLimit;
	uint32_t nearTDPLimit;
	uint32_t nearTDPLimitAdjusted;
	uint32_t SQRampingThreshold;
	uint32_t CACLeakage;
	uint16_t TDPODLimit;
	uint32_t TDPAdjustment;
	bool TDPAdjustmentPolarity;
	uint16_t LoadLineSlope;
	uint32_t  VidMinLimit;
	uint32_t  VidMaxLimit;
	uint32_t  VidStep;
	uint32_t  VidAdjustment;
	bool VidAdjustmentPolarity;
};

struct phm_clocks {
	uint32_t num_of_entries;
	uint32_t clock[MAX_NUM_CLOCKS];
};

extern int phm_enable_clock_power_gatings(struct pp_hwmgr *hwmgr);
extern int phm_powergate_uvd(struct pp_hwmgr *hwmgr, bool gate);
extern int phm_powergate_vce(struct pp_hwmgr *hwmgr, bool gate);
extern int phm_powerdown_uvd(struct pp_hwmgr *hwmgr);
extern int phm_setup_asic(struct pp_hwmgr *hwmgr);
extern int phm_enable_dynamic_state_management(struct pp_hwmgr *hwmgr);
extern int phm_disable_dynamic_state_management(struct pp_hwmgr *hwmgr);
extern void phm_init_dynamic_caps(struct pp_hwmgr *hwmgr);
extern bool phm_is_hw_access_blocked(struct pp_hwmgr *hwmgr);
extern int phm_block_hw_access(struct pp_hwmgr *hwmgr, bool block);
extern int phm_set_power_state(struct pp_hwmgr *hwmgr,
		    const struct pp_hw_power_state *pcurrent_state,
		 const struct pp_hw_power_state *pnew_power_state);

extern int phm_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				   struct pp_power_state *adjusted_ps,
			     const struct pp_power_state *current_ps);

extern int phm_force_dpm_levels(struct pp_hwmgr *hwmgr, enum amd_dpm_forced_level level);
extern int phm_display_configuration_changed(struct pp_hwmgr *hwmgr);
extern int phm_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr);
extern int phm_register_thermal_interrupt(struct pp_hwmgr *hwmgr, const void *info);
extern int phm_start_thermal_controller(struct pp_hwmgr *hwmgr, struct PP_TemperatureRange *temperature_range);
extern int phm_stop_thermal_controller(struct pp_hwmgr *hwmgr);
extern bool phm_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr);

extern int phm_check_states_equal(struct pp_hwmgr *hwmgr,
				 const struct pp_hw_power_state *pstate1,
				 const struct pp_hw_power_state *pstate2,
				 bool *equal);

extern int phm_store_dal_configuration_data(struct pp_hwmgr *hwmgr,
		const struct amd_pp_display_configuration *display_config);

extern int phm_get_dal_power_level(struct pp_hwmgr *hwmgr,
		struct amd_pp_simple_clock_info *info);

extern int phm_set_cpu_power_state(struct pp_hwmgr *hwmgr);

extern int phm_power_down_asic(struct pp_hwmgr *hwmgr);

extern int phm_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level);

extern int phm_get_clock_info(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
			struct pp_clock_info *pclock_info,
			PHM_PerformanceLevelDesignation designation);

extern int phm_get_current_shallow_sleep_clocks(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state, struct pp_clock_info *clock_info);

extern int phm_get_clock_by_type(struct pp_hwmgr *hwmgr, enum amd_pp_clock_type type, struct amd_pp_clocks *clocks);

extern int phm_get_max_high_clocks(struct pp_hwmgr *hwmgr, struct amd_pp_simple_clock_info *clocks);

#endif /* _HARDWARE_MANAGER_H_ */

