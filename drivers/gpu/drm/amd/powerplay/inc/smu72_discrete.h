/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SMU72_DISCRETE_H
#define SMU72_DISCRETE_H

#include "smu72.h"

#if !defined(SMC_MICROCODE)
#pragma pack(push, 1)
#endif

struct SMIO_Pattern {
	uint16_t Voltage;
	uint8_t  Smio;
	uint8_t  padding;
};

typedef struct SMIO_Pattern SMIO_Pattern;

struct SMIO_Table {
	SMIO_Pattern Pattern[SMU_MAX_SMIO_LEVELS];
};

typedef struct SMIO_Table SMIO_Table;

struct SMU72_Discrete_GraphicsLevel {
	SMU_VoltageLevel MinVoltage;

	uint32_t    SclkFrequency;

	uint8_t     pcieDpmLevel;
	uint8_t     DeepSleepDivId;
	uint16_t    ActivityLevel;

	uint32_t    CgSpllFuncCntl3;
	uint32_t    CgSpllFuncCntl4;
	uint32_t    SpllSpreadSpectrum;
	uint32_t    SpllSpreadSpectrum2;
	uint32_t    CcPwrDynRm;
	uint32_t    CcPwrDynRm1;
	uint8_t     SclkDid;
	uint8_t     DisplayWatermark;
	uint8_t     EnabledForActivity;
	uint8_t     EnabledForThrottle;
	uint8_t     UpHyst;
	uint8_t     DownHyst;
	uint8_t     VoltageDownHyst;
	uint8_t     PowerThrottle;
};

typedef struct SMU72_Discrete_GraphicsLevel SMU72_Discrete_GraphicsLevel;

struct SMU72_Discrete_ACPILevel {
	uint32_t    Flags;
	SMU_VoltageLevel MinVoltage;
	uint32_t    SclkFrequency;
	uint8_t     SclkDid;
	uint8_t     DisplayWatermark;
	uint8_t     DeepSleepDivId;
	uint8_t     padding;
	uint32_t    CgSpllFuncCntl;
	uint32_t    CgSpllFuncCntl2;
	uint32_t    CgSpllFuncCntl3;
	uint32_t    CgSpllFuncCntl4;
	uint32_t    SpllSpreadSpectrum;
	uint32_t    SpllSpreadSpectrum2;
	uint32_t    CcPwrDynRm;
	uint32_t    CcPwrDynRm1;
};

typedef struct SMU72_Discrete_ACPILevel SMU72_Discrete_ACPILevel;

struct SMU72_Discrete_Ulv {
	uint32_t    CcPwrDynRm;
	uint32_t    CcPwrDynRm1;
	uint16_t    VddcOffset;
	uint8_t     VddcOffsetVid;
	uint8_t     VddcPhase;
	uint32_t    Reserved;
};

typedef struct SMU72_Discrete_Ulv SMU72_Discrete_Ulv;

struct SMU72_Discrete_MemoryLevel {
	SMU_VoltageLevel MinVoltage;
	uint32_t    MinMvdd;

	uint32_t    MclkFrequency;

	uint8_t     EdcReadEnable;
	uint8_t     EdcWriteEnable;
	uint8_t     RttEnable;
	uint8_t     StutterEnable;

	uint8_t     StrobeEnable;
	uint8_t     StrobeRatio;
	uint8_t     EnabledForThrottle;
	uint8_t     EnabledForActivity;

	uint8_t     UpHyst;
	uint8_t     DownHyst;
	uint8_t     VoltageDownHyst;
	uint8_t     padding;

	uint16_t    ActivityLevel;
	uint8_t     DisplayWatermark;
	uint8_t     padding1;

	uint32_t    MpllFuncCntl;
	uint32_t    MpllFuncCntl_1;
	uint32_t    MpllFuncCntl_2;
	uint32_t    MpllAdFuncCntl;
	uint32_t    MpllDqFuncCntl;
	uint32_t    MclkPwrmgtCntl;
	uint32_t    DllCntl;
	uint32_t    MpllSs1;
	uint32_t    MpllSs2;
};

typedef struct SMU72_Discrete_MemoryLevel SMU72_Discrete_MemoryLevel;

struct SMU72_Discrete_LinkLevel {
	uint8_t     PcieGenSpeed;           /*< 0:PciE-gen1 1:PciE-gen2 2:PciE-gen3 */
	uint8_t     PcieLaneCount;          /*< 1=x1, 2=x2, 3=x4, 4=x8, 5=x12, 6=x16 */
	uint8_t     EnabledForActivity;
	uint8_t     SPC;
	uint32_t    DownThreshold;
	uint32_t    UpThreshold;
	uint32_t    Reserved;
};

typedef struct SMU72_Discrete_LinkLevel SMU72_Discrete_LinkLevel;

/* MC ARB DRAM Timing registers. */
struct SMU72_Discrete_MCArbDramTimingTableEntry {
	uint32_t McArbDramTiming;
	uint32_t McArbDramTiming2;
	uint8_t  McArbBurstTime;
	uint8_t  padding[3];
};

typedef struct SMU72_Discrete_MCArbDramTimingTableEntry SMU72_Discrete_MCArbDramTimingTableEntry;

struct SMU72_Discrete_MCArbDramTimingTable {
	SMU72_Discrete_MCArbDramTimingTableEntry entries[SMU__NUM_SCLK_DPM_STATE][SMU__NUM_MCLK_DPM_LEVELS];
};

typedef struct SMU72_Discrete_MCArbDramTimingTable SMU72_Discrete_MCArbDramTimingTable;

/* UVD VCLK/DCLK state (level) definition. */
struct SMU72_Discrete_UvdLevel {
	uint32_t VclkFrequency;
	uint32_t DclkFrequency;
	SMU_VoltageLevel MinVoltage;
	uint8_t  VclkDivider;
	uint8_t  DclkDivider;
	uint8_t  padding[2];
};

typedef struct SMU72_Discrete_UvdLevel SMU72_Discrete_UvdLevel;

/* Clocks for other external blocks (VCE, ACP, SAMU). */
struct SMU72_Discrete_ExtClkLevel {
	uint32_t Frequency;
	SMU_VoltageLevel MinVoltage;
	uint8_t  Divider;
	uint8_t  padding[3];
};

typedef struct SMU72_Discrete_ExtClkLevel SMU72_Discrete_ExtClkLevel;

struct SMU72_Discrete_StateInfo {
	uint32_t SclkFrequency;
	uint32_t MclkFrequency;
	uint32_t VclkFrequency;
	uint32_t DclkFrequency;
	uint32_t SamclkFrequency;
	uint32_t AclkFrequency;
	uint32_t EclkFrequency;
	uint16_t MvddVoltage;
	uint16_t padding16;
	uint8_t  DisplayWatermark;
	uint8_t  McArbIndex;
	uint8_t  McRegIndex;
	uint8_t  SeqIndex;
	uint8_t  SclkDid;
	int8_t   SclkIndex;
	int8_t   MclkIndex;
	uint8_t  PCIeGen;

};

typedef struct SMU72_Discrete_StateInfo SMU72_Discrete_StateInfo;

struct SMU72_Discrete_DpmTable {
	/* Multi-DPM controller settings */
	SMU72_PIDController                  GraphicsPIDController;
	SMU72_PIDController                  MemoryPIDController;
	SMU72_PIDController                  LinkPIDController;

	uint32_t                            SystemFlags;

	/* SMIO masks for voltage and phase controls */
	uint32_t                            VRConfig;
	uint32_t                            SmioMask1;
	uint32_t                            SmioMask2;
	SMIO_Table                          SmioTable1;
	SMIO_Table                          SmioTable2;

	uint32_t                            VddcLevelCount;
	uint32_t                            VddciLevelCount;
	uint32_t                            VddGfxLevelCount;
	uint32_t                            MvddLevelCount;

	uint16_t                            VddcTable[SMU72_MAX_LEVELS_VDDC];
	uint16_t                            VddGfxTable[SMU72_MAX_LEVELS_VDDGFX];
	uint16_t                            VddciTable[SMU72_MAX_LEVELS_VDDCI];

	uint8_t                             BapmVddGfxVidHiSidd[SMU72_MAX_LEVELS_VDDGFX];
	uint8_t                             BapmVddGfxVidLoSidd[SMU72_MAX_LEVELS_VDDGFX];
	uint8_t                             BapmVddGfxVidHiSidd2[SMU72_MAX_LEVELS_VDDGFX];

	uint8_t                             BapmVddcVidHiSidd[SMU72_MAX_LEVELS_VDDC];
	uint8_t                             BapmVddcVidLoSidd[SMU72_MAX_LEVELS_VDDC];
	uint8_t                             BapmVddcVidHiSidd2[SMU72_MAX_LEVELS_VDDC];

	uint8_t                             GraphicsDpmLevelCount;
	uint8_t                             MemoryDpmLevelCount;
	uint8_t                             LinkLevelCount;
	uint8_t                             MasterDeepSleepControl;

	uint8_t                             UvdLevelCount;
	uint8_t                             VceLevelCount;
	uint8_t                             AcpLevelCount;
	uint8_t                             SamuLevelCount;

	uint8_t                             ThermOutGpio;
	uint8_t                             ThermOutPolarity;
	uint8_t                             ThermOutMode;
	uint8_t                             DPMFreezeAndForced;
	uint32_t                            Reserved[4];

	/* State table entries for each DPM state */
	SMU72_Discrete_GraphicsLevel        GraphicsLevel[SMU72_MAX_LEVELS_GRAPHICS];
	SMU72_Discrete_MemoryLevel          MemoryACPILevel;
	SMU72_Discrete_MemoryLevel          MemoryLevel[SMU72_MAX_LEVELS_MEMORY];
	SMU72_Discrete_LinkLevel            LinkLevel[SMU72_MAX_LEVELS_LINK];
	SMU72_Discrete_ACPILevel            ACPILevel;
	SMU72_Discrete_UvdLevel             UvdLevel[SMU72_MAX_LEVELS_UVD];
	SMU72_Discrete_ExtClkLevel          VceLevel[SMU72_MAX_LEVELS_VCE];
	SMU72_Discrete_ExtClkLevel          AcpLevel[SMU72_MAX_LEVELS_ACP];
	SMU72_Discrete_ExtClkLevel          SamuLevel[SMU72_MAX_LEVELS_SAMU];
	SMU72_Discrete_Ulv                  Ulv;

	uint32_t                            SclkStepSize;
	uint32_t                            Smio[SMU72_MAX_ENTRIES_SMIO];

	uint8_t                             UvdBootLevel;
	uint8_t                             VceBootLevel;
	uint8_t                             AcpBootLevel;
	uint8_t                             SamuBootLevel;

	uint8_t                             GraphicsBootLevel;
	uint8_t                             GraphicsVoltageChangeEnable;
	uint8_t                             GraphicsThermThrottleEnable;
	uint8_t                             GraphicsInterval;

	uint8_t                             VoltageInterval;
	uint8_t                             ThermalInterval;
	uint16_t                            TemperatureLimitHigh;

	uint16_t                            TemperatureLimitLow;
	uint8_t                             MemoryBootLevel;
	uint8_t                             MemoryVoltageChangeEnable;

	uint16_t                            BootMVdd;
	uint8_t                             MemoryInterval;
	uint8_t                             MemoryThermThrottleEnable;

	uint16_t                            VoltageResponseTime;
	uint16_t                            PhaseResponseTime;

	uint8_t                             PCIeBootLinkLevel;
	uint8_t                             PCIeGenInterval;
	uint8_t                             DTEInterval;
	uint8_t                             DTEMode;

	uint8_t                             SVI2Enable;
	uint8_t                             VRHotGpio;
	uint8_t                             AcDcGpio;
	uint8_t                             ThermGpio;

	uint16_t                            PPM_PkgPwrLimit;
	uint16_t                            PPM_TemperatureLimit;

	uint16_t                            DefaultTdp;
	uint16_t                            TargetTdp;

	uint16_t                            FpsHighThreshold;
	uint16_t                            FpsLowThreshold;

	uint16_t                            BAPMTI_R[SMU72_DTE_ITERATIONS][SMU72_DTE_SOURCES][SMU72_DTE_SINKS];
	uint16_t                            BAPMTI_RC[SMU72_DTE_ITERATIONS][SMU72_DTE_SOURCES][SMU72_DTE_SINKS];

	uint8_t                             DTEAmbientTempBase;
	uint8_t                             DTETjOffset;
	uint8_t                             GpuTjMax;
	uint8_t                             GpuTjHyst;

	SMU_VoltageLevel                    BootVoltage;

	uint32_t                            BAPM_TEMP_GRADIENT;

	uint32_t                            LowSclkInterruptThreshold;
	uint32_t                            VddGfxReChkWait;

	uint8_t                             ClockStretcherAmount;

	uint8_t                             Sclk_CKS_masterEn0_7;
	uint8_t                             Sclk_CKS_masterEn8_15;
	uint8_t                             padding[1];

	uint8_t                             Sclk_voltageOffset[8];

	SMU_ClockStretcherDataTable         ClockStretcherDataTable;
	SMU_CKS_LOOKUPTable                 CKS_LOOKUPTable;
};

typedef struct SMU72_Discrete_DpmTable SMU72_Discrete_DpmTable;

/* --------------------------------------------------- AC Timing Parameters ------------------------------------------------ */
#define SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE 16
#define SMU72_DISCRETE_MC_REGISTER_ARRAY_SET_COUNT SMU72_MAX_LEVELS_MEMORY /* DPM */

struct SMU72_Discrete_MCRegisterAddress {
	uint16_t s0;
	uint16_t s1;
};

typedef struct SMU72_Discrete_MCRegisterAddress SMU72_Discrete_MCRegisterAddress;

struct SMU72_Discrete_MCRegisterSet {
	uint32_t value[SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMU72_Discrete_MCRegisterSet SMU72_Discrete_MCRegisterSet;

struct SMU72_Discrete_MCRegisters {
	uint8_t                             last;
	uint8_t                             reserved[3];
	SMU72_Discrete_MCRegisterAddress     address[SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE];
	SMU72_Discrete_MCRegisterSet         data[SMU72_DISCRETE_MC_REGISTER_ARRAY_SET_COUNT];
};

typedef struct SMU72_Discrete_MCRegisters SMU72_Discrete_MCRegisters;


/* --------------------------------------------------- Fan Table ----------------------------------------------------------- */

struct SMU72_Discrete_FanTable {
	uint16_t FdoMode;
	int16_t  TempMin;
	int16_t  TempMed;
	int16_t  TempMax;
	int16_t  Slope1;
	int16_t  Slope2;
	int16_t  FdoMin;
	int16_t  HystUp;
	int16_t  HystDown;
	int16_t  HystSlope;
	int16_t  TempRespLim;
	int16_t  TempCurr;
	int16_t  SlopeCurr;
	int16_t  PwmCurr;
	uint32_t RefreshPeriod;
	int16_t  FdoMax;
	uint8_t  TempSrc;
	int8_t   FanControl_GL_Flag;
};

typedef struct SMU72_Discrete_FanTable SMU72_Discrete_FanTable;

#define SMU7_DISCRETE_GPIO_SCLK_DEBUG             4
#define SMU7_DISCRETE_GPIO_SCLK_DEBUG_BIT         (0x1 << SMU7_DISCRETE_GPIO_SCLK_DEBUG)

struct SMU7_MclkDpmScoreboard {

	uint32_t PercentageBusy;

	int32_t  PIDError;
	int32_t  PIDIntegral;
	int32_t  PIDOutput;

	uint32_t SigmaDeltaAccum;
	uint32_t SigmaDeltaOutput;
	uint32_t SigmaDeltaLevel;

	uint32_t UtilizationSetpoint;

	uint8_t  TdpClampMode;
	uint8_t  TdcClampMode;
	uint8_t  ThermClampMode;
	uint8_t  VoltageBusy;

	int8_t   CurrLevel;
	int8_t   TargLevel;
	uint8_t  LevelChangeInProgress;
	uint8_t  UpHyst;

	uint8_t  DownHyst;
	uint8_t  VoltageDownHyst;
	uint8_t  DpmEnable;
	uint8_t  DpmRunning;

	uint8_t  DpmForce;
	uint8_t  DpmForceLevel;
	uint8_t  DisplayWatermark;
	uint8_t  McArbIndex;

	uint32_t MinimumPerfMclk;

	uint8_t  AcpiReq;
	uint8_t  AcpiAck;
	uint8_t  MclkSwitchInProgress;
	uint8_t  MclkSwitchCritical;

	uint8_t  IgnoreVBlank;
	uint8_t  TargetMclkIndex;
	uint8_t  TargetMvddIndex;
	uint8_t  MclkSwitchResult;

	uint16_t VbiFailureCount;
	uint8_t  VbiWaitCounter;
	uint8_t  EnabledLevelsChange;

	uint16_t LevelResidencyCountersN[SMU72_MAX_LEVELS_MEMORY];
	uint16_t LevelSwitchCounters[SMU72_MAX_LEVELS_MEMORY];

	void     (*TargetStateCalculator)(uint8_t);
	void     (*SavedTargetStateCalculator)(uint8_t);

	uint16_t AutoDpmInterval;
	uint16_t AutoDpmRange;

	uint16_t VbiTimeoutCount;
	uint16_t MclkSwitchingTime;

	uint8_t  fastSwitch;
	uint8_t  Save_PIC_VDDGFX_EXIT;
	uint8_t  Save_PIC_VDDGFX_ENTER;
	uint8_t  padding;

};

typedef struct SMU7_MclkDpmScoreboard SMU7_MclkDpmScoreboard;

struct SMU7_UlvScoreboard {
	uint8_t     EnterUlv;
	uint8_t     ExitUlv;
	uint8_t     UlvActive;
	uint8_t     WaitingForUlv;
	uint8_t     UlvEnable;
	uint8_t     UlvRunning;
	uint8_t     UlvMasterEnable;
	uint8_t     padding;
	uint32_t    UlvAbortedCount;
	uint32_t    UlvTimeStamp;
};

typedef struct SMU7_UlvScoreboard SMU7_UlvScoreboard;

struct VddgfxSavedRegisters {
	uint32_t GPU_DBG[3];
	uint32_t MEC_BaseAddress_Hi;
	uint32_t MEC_BaseAddress_Lo;
	uint32_t THM_TMON0_CTRL2__RDIR_PRESENT;
	uint32_t THM_TMON1_CTRL2__RDIR_PRESENT;
	uint32_t CP_INT_CNTL;
};

typedef struct VddgfxSavedRegisters VddgfxSavedRegisters;

struct SMU7_VddGfxScoreboard {
	uint8_t     VddGfxEnable;
	uint8_t     VddGfxActive;
	uint8_t     VPUResetOccured;
	uint8_t     padding;

	uint32_t    VddGfxEnteredCount;
	uint32_t    VddGfxAbortedCount;

	uint32_t    VddGfxVid;

	VddgfxSavedRegisters SavedRegisters;
};

typedef struct SMU7_VddGfxScoreboard SMU7_VddGfxScoreboard;

struct SMU7_TdcLimitScoreboard {
	uint8_t  Enable;
	uint8_t  Running;
	uint16_t Alpha;
	uint32_t FilteredIddc;
	uint32_t IddcLimit;
	uint32_t IddcHyst;
	SMU7_HystController_Data HystControllerData;
};

typedef struct SMU7_TdcLimitScoreboard SMU7_TdcLimitScoreboard;

struct SMU7_PkgPwrLimitScoreboard {
	uint8_t  Enable;
	uint8_t  Running;
	uint16_t Alpha;
	uint32_t FilteredPkgPwr;
	uint32_t Limit;
	uint32_t Hyst;
	uint32_t LimitFromDriver;
	SMU7_HystController_Data HystControllerData;
};

typedef struct SMU7_PkgPwrLimitScoreboard SMU7_PkgPwrLimitScoreboard;

struct SMU7_BapmScoreboard {
	uint32_t source_powers[SMU72_DTE_SOURCES];
	uint32_t source_powers_last[SMU72_DTE_SOURCES];
	int32_t entity_temperatures[SMU72_NUM_GPU_TES];
	int32_t initial_entity_temperatures[SMU72_NUM_GPU_TES];
	int32_t Limit;
	int32_t Hyst;
	int32_t therm_influence_coeff_table[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS * 2];
	int32_t therm_node_table[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
	uint16_t ConfigTDPPowerScalar;
	uint16_t FanSpeedPowerScalar;
	uint16_t OverDrivePowerScalar;
	uint16_t OverDriveLimitScalar;
	uint16_t FinalPowerScalar;
	uint8_t VariantID;
	uint8_t spare997;

	SMU7_HystController_Data HystControllerData;

	int32_t temperature_gradient_slope;
	int32_t temperature_gradient;
	uint32_t measured_temperature;
};


typedef struct SMU7_BapmScoreboard SMU7_BapmScoreboard;

struct SMU7_AcpiScoreboard {
	uint32_t SavedInterruptMask[2];
	uint8_t LastACPIRequest;
	uint8_t CgBifResp;
	uint8_t RequestType;
	uint8_t Padding;
	SMU72_Discrete_ACPILevel D0Level;
};

typedef struct SMU7_AcpiScoreboard SMU7_AcpiScoreboard;

struct SMU72_Discrete_PmFuses {
	/* dw1  */
	uint8_t SviLoadLineEn;
	uint8_t SviLoadLineVddC;
	uint8_t SviLoadLineTrimVddC;
	uint8_t SviLoadLineOffsetVddC;

	/* dw2 */
	uint16_t TDC_VDDC_PkgLimit;
	uint8_t TDC_VDDC_ThrottleReleaseLimitPerc;
	uint8_t TDC_MAWt;

	/* dw3 */
	uint8_t TdcWaterfallCtl;
	uint8_t LPMLTemperatureMin;
	uint8_t LPMLTemperatureMax;
	uint8_t Reserved;

	/* dw4-dw7  */
	uint8_t LPMLTemperatureScaler[16];

	/* dw8-dw9  */
	int16_t FuzzyFan_ErrorSetDelta;
	int16_t FuzzyFan_ErrorRateSetDelta;
	int16_t FuzzyFan_PwmSetDelta;
	uint16_t Reserved6;

	/* dw10-dw14  */
	uint8_t GnbLPML[16];

	/* dw15 */
	uint8_t GnbLPMLMaxVid;
	uint8_t GnbLPMLMinVid;
	uint8_t Reserved1[2];

	/* dw16 */
	uint16_t BapmVddCBaseLeakageHiSidd;
	uint16_t BapmVddCBaseLeakageLoSidd;
};

typedef struct SMU72_Discrete_PmFuses SMU72_Discrete_PmFuses;

struct SMU7_Discrete_Log_Header_Table {
	uint32_t    version;
	uint32_t    asic_id;
	uint16_t    flags;
	uint16_t    entry_size;
	uint32_t    total_size;
	uint32_t    num_of_entries;
	uint8_t     type;
	uint8_t     mode;
	uint8_t     filler_0[2];
	uint32_t    filler_1[2];
};

typedef struct SMU7_Discrete_Log_Header_Table SMU7_Discrete_Log_Header_Table;

struct SMU7_Discrete_Log_Cntl {
	uint8_t             Enabled;
	uint8_t             Type;
	uint8_t             padding[2];
	uint32_t            BufferSize;
	uint32_t            SamplesLogged;
	uint32_t            SampleSize;
	uint32_t            AddrL;
	uint32_t            AddrH;
};

typedef struct SMU7_Discrete_Log_Cntl SMU7_Discrete_Log_Cntl;

#define CAC_ACC_NW_NUM_OF_SIGNALS 87

struct SMU7_Discrete_Cac_Collection_Table {
	uint32_t temperature;
	uint32_t cac_acc_nw[CAC_ACC_NW_NUM_OF_SIGNALS];
};

typedef struct SMU7_Discrete_Cac_Collection_Table SMU7_Discrete_Cac_Collection_Table;

struct SMU7_Discrete_Cac_Verification_Table {
	uint32_t VddcTotalPower;
	uint32_t VddcLeakagePower;
	uint32_t VddcConstantPower;
	uint32_t VddcGfxDynamicPower;
	uint32_t VddcUvdDynamicPower;
	uint32_t VddcVceDynamicPower;
	uint32_t VddcAcpDynamicPower;
	uint32_t VddcPcieDynamicPower;
	uint32_t VddcDceDynamicPower;
	uint32_t VddcCurrent;
	uint32_t VddcVoltage;
	uint32_t VddciTotalPower;
	uint32_t VddciLeakagePower;
	uint32_t VddciConstantPower;
	uint32_t VddciDynamicPower;
	uint32_t Vddr1TotalPower;
	uint32_t Vddr1LeakagePower;
	uint32_t Vddr1ConstantPower;
	uint32_t Vddr1DynamicPower;
	uint32_t spare[4];
	uint32_t temperature;
};

typedef struct SMU7_Discrete_Cac_Verification_Table SMU7_Discrete_Cac_Verification_Table;

struct SMU7_Discrete_Pm_Status_Table {
	/* Thermal entities */
	int32_t T_meas_max;
	int32_t T_meas_acc;
	int32_t T_calc_max;
	int32_t T_calc_acc;
	uint32_t P_scalar_acc;
	uint32_t P_calc_max;
	uint32_t P_calc_acc;

	/*Voltage domains */
	uint32_t I_calc_max;
	uint32_t I_calc_acc;
	uint32_t I_calc_acc_vddci;
	uint32_t V_calc_noload_acc;
	uint32_t V_calc_load_acc;
	uint32_t V_calc_noload_acc_vddci;
	uint32_t P_meas_acc;
	uint32_t V_meas_noload_acc;
	uint32_t V_meas_load_acc;
	uint32_t I_meas_acc;
	uint32_t P_meas_acc_vddci;
	uint32_t V_meas_noload_acc_vddci;
	uint32_t V_meas_load_acc_vddci;
	uint32_t I_meas_acc_vddci;

	/*Frequency */
	uint16_t Sclk_dpm_residency[8];
	uint16_t Uvd_dpm_residency[8];
	uint16_t Vce_dpm_residency[8];
	uint16_t Mclk_dpm_residency[4];

	/*Chip */
	uint32_t P_vddci_acc;
	uint32_t P_vddr1_acc;
	uint32_t P_nte1_acc;
	uint32_t PkgPwr_max;
	uint32_t PkgPwr_acc;
	uint32_t MclkSwitchingTime_max;
	uint32_t MclkSwitchingTime_acc;
	uint32_t FanPwm_acc;
	uint32_t FanRpm_acc;

	uint32_t AccCnt;
};

typedef struct SMU7_Discrete_Pm_Status_Table SMU7_Discrete_Pm_Status_Table;

/*FIXME THESE NEED TO BE UPDATED */
#define SMU7_SCLK_CAC 0x561
#define SMU7_MCLK_CAC 0xF9
#define SMU7_VCLK_CAC 0x2DE
#define SMU7_DCLK_CAC 0x2DE
#define SMU7_ECLK_CAC 0x25E
#define SMU7_ACLK_CAC 0x25E
#define SMU7_SAMCLK_CAC 0x25E
#define SMU7_DISPCLK_CAC 0x100
#define SMU7_CAC_CONSTANT 0x2EE3430
#define SMU7_CAC_CONSTANT_SHIFT 18

#define SMU7_VDDCI_MCLK_CONST        1765
#define SMU7_VDDCI_MCLK_CONST_SHIFT  16
#define SMU7_VDDCI_VDDCI_CONST       50958
#define SMU7_VDDCI_VDDCI_CONST_SHIFT 14
#define SMU7_VDDCI_CONST             11781

#define SMU7_12C_VDDCI_MCLK_CONST        1623
#define SMU7_12C_VDDCI_MCLK_CONST_SHIFT  15
#define SMU7_12C_VDDCI_VDDCI_CONST       40088
#define SMU7_12C_VDDCI_VDDCI_CONST_SHIFT 13
#define SMU7_12C_VDDCI_CONST             20856

#define SMU7_VDDCI_STROBE_PWR        1331

#define SMU7_VDDR1_CONST            693
#define SMU7_VDDR1_CAC_WEIGHT       20
#define SMU7_VDDR1_CAC_WEIGHT_SHIFT 19
#define SMU7_VDDR1_STROBE_PWR       512

#define SMU7_AREA_COEFF_UVD 0xA78
#define SMU7_AREA_COEFF_VCE 0x190A
#define SMU7_AREA_COEFF_ACP 0x22D1
#define SMU7_AREA_COEFF_SAMU 0x534

/*ThermOutMode values */
#define SMU7_THERM_OUT_MODE_DISABLE       0x0
#define SMU7_THERM_OUT_MODE_THERM_ONLY    0x1
#define SMU7_THERM_OUT_MODE_THERM_VRHOT   0x2

#if !defined(SMC_MICROCODE)
#pragma pack(pop)
#endif


#endif

