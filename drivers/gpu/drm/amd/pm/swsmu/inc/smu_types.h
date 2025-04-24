/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */

#ifndef __SMU_TYPES_H__
#define __SMU_TYPES_H__

#define SMU_MESSAGE_TYPES			      \
       __SMU_DUMMY_MAP(TestMessage),		      \
       __SMU_DUMMY_MAP(GetSmuVersion),                \
       __SMU_DUMMY_MAP(GetDriverIfVersion),           \
       __SMU_DUMMY_MAP(SetAllowedFeaturesMaskLow),    \
       __SMU_DUMMY_MAP(SetAllowedFeaturesMaskHigh),   \
       __SMU_DUMMY_MAP(EnableAllSmuFeatures),         \
       __SMU_DUMMY_MAP(DisableAllSmuFeatures),        \
       __SMU_DUMMY_MAP(EnableSmuFeaturesLow),         \
       __SMU_DUMMY_MAP(EnableSmuFeaturesHigh),        \
       __SMU_DUMMY_MAP(DisableSmuFeaturesLow),        \
       __SMU_DUMMY_MAP(DisableSmuFeaturesHigh),       \
       __SMU_DUMMY_MAP(GetEnabledSmuFeatures),	      \
       __SMU_DUMMY_MAP(GetEnabledSmuFeaturesLow),     \
       __SMU_DUMMY_MAP(GetEnabledSmuFeaturesHigh),    \
       __SMU_DUMMY_MAP(SetWorkloadMask),              \
       __SMU_DUMMY_MAP(SetPptLimit),                  \
       __SMU_DUMMY_MAP(SetDriverDramAddrHigh),        \
       __SMU_DUMMY_MAP(SetDriverDramAddrLow),         \
       __SMU_DUMMY_MAP(SetToolsDramAddrHigh),         \
       __SMU_DUMMY_MAP(SetToolsDramAddrLow),          \
       __SMU_DUMMY_MAP(TransferTableSmu2Dram),        \
       __SMU_DUMMY_MAP(TransferTableDram2Smu),        \
       __SMU_DUMMY_MAP(UseDefaultPPTable),            \
       __SMU_DUMMY_MAP(UseBackupPPTable),             \
       __SMU_DUMMY_MAP(RunBtc),                       \
       __SMU_DUMMY_MAP(RequestI2CBus),                \
       __SMU_DUMMY_MAP(ReleaseI2CBus),                \
       __SMU_DUMMY_MAP(SetFloorSocVoltage),           \
       __SMU_DUMMY_MAP(SoftReset),                    \
       __SMU_DUMMY_MAP(StartBacoMonitor),             \
       __SMU_DUMMY_MAP(CancelBacoMonitor),            \
       __SMU_DUMMY_MAP(EnterBaco),                    \
       __SMU_DUMMY_MAP(SetSoftMinByFreq),             \
       __SMU_DUMMY_MAP(SetSoftMaxByFreq),             \
       __SMU_DUMMY_MAP(SetHardMinByFreq),             \
       __SMU_DUMMY_MAP(SetHardMaxByFreq),             \
       __SMU_DUMMY_MAP(GetMinDpmFreq),                \
       __SMU_DUMMY_MAP(GetMaxDpmFreq),                \
       __SMU_DUMMY_MAP(GetDpmFreqByIndex),            \
       __SMU_DUMMY_MAP(GetDpmClockFreq),              \
       __SMU_DUMMY_MAP(GetSsVoltageByDpm),            \
       __SMU_DUMMY_MAP(SetMemoryChannelConfig),       \
       __SMU_DUMMY_MAP(SetGeminiMode),                \
       __SMU_DUMMY_MAP(SetGeminiApertureHigh),        \
       __SMU_DUMMY_MAP(SetGeminiApertureLow),         \
       __SMU_DUMMY_MAP(SetMinLinkDpmByIndex),         \
       __SMU_DUMMY_MAP(OverridePcieParameters),       \
       __SMU_DUMMY_MAP(OverDriveSetPercentage),       \
       __SMU_DUMMY_MAP(SetMinDeepSleepDcefclk),       \
       __SMU_DUMMY_MAP(ReenableAcDcInterrupt),        \
       __SMU_DUMMY_MAP(AllowIHHostInterrupt),        \
       __SMU_DUMMY_MAP(NotifyPowerSource),            \
       __SMU_DUMMY_MAP(SetUclkFastSwitch),            \
       __SMU_DUMMY_MAP(SetUclkDownHyst),              \
       __SMU_DUMMY_MAP(GfxDeviceDriverReset),         \
       __SMU_DUMMY_MAP(GetCurrentRpm),                \
       __SMU_DUMMY_MAP(SetVideoFps),                  \
       __SMU_DUMMY_MAP(SetTjMax),                     \
       __SMU_DUMMY_MAP(SetFanTemperatureTarget),      \
       __SMU_DUMMY_MAP(PrepareMp1ForUnload),          \
       __SMU_DUMMY_MAP(GetCTFLimit),                  \
       __SMU_DUMMY_MAP(DramLogSetDramAddrHigh),       \
       __SMU_DUMMY_MAP(DramLogSetDramAddrLow),        \
       __SMU_DUMMY_MAP(DramLogSetDramSize),           \
       __SMU_DUMMY_MAP(SetFanMaxRpm),                 \
       __SMU_DUMMY_MAP(SetFanMinPwm),                 \
       __SMU_DUMMY_MAP(ConfigureGfxDidt),             \
       __SMU_DUMMY_MAP(NumOfDisplays),                \
       __SMU_DUMMY_MAP(RemoveMargins),                \
       __SMU_DUMMY_MAP(ReadSerialNumTop32),           \
       __SMU_DUMMY_MAP(ReadSerialNumBottom32),        \
       __SMU_DUMMY_MAP(SetSystemVirtualDramAddrHigh), \
       __SMU_DUMMY_MAP(SetSystemVirtualDramAddrLow),  \
       __SMU_DUMMY_MAP(WaflTest),                     \
       __SMU_DUMMY_MAP(SetFclkGfxClkRatio),           \
       __SMU_DUMMY_MAP(AllowGfxOff),                  \
       __SMU_DUMMY_MAP(DisallowGfxOff),               \
       __SMU_DUMMY_MAP(GetPptLimit),                  \
       __SMU_DUMMY_MAP(GetDcModeMaxDpmFreq),          \
       __SMU_DUMMY_MAP(GetDebugData),                 \
       __SMU_DUMMY_MAP(SetXgmiMode),                  \
       __SMU_DUMMY_MAP(RunAfllBtc),                   \
       __SMU_DUMMY_MAP(ExitBaco),                     \
       __SMU_DUMMY_MAP(PrepareMp1ForReset),           \
       __SMU_DUMMY_MAP(PrepareMp1ForShutdown),        \
       __SMU_DUMMY_MAP(SetMGpuFanBoostLimitRpm),      \
       __SMU_DUMMY_MAP(GetAVFSVoltageByDpm),          \
       __SMU_DUMMY_MAP(PowerUpVcn),                   \
       __SMU_DUMMY_MAP(PowerDownVcn),                 \
       __SMU_DUMMY_MAP(PowerUpJpeg),                  \
       __SMU_DUMMY_MAP(PowerDownJpeg),                \
       __SMU_DUMMY_MAP(PowerUpJpeg0),                 \
       __SMU_DUMMY_MAP(PowerDownJpeg0),               \
       __SMU_DUMMY_MAP(PowerUpJpeg1),                 \
       __SMU_DUMMY_MAP(PowerDownJpeg1),               \
       __SMU_DUMMY_MAP(BacoAudioD3PME),               \
       __SMU_DUMMY_MAP(ArmD3),                        \
       __SMU_DUMMY_MAP(RunDcBtc),                     \
       __SMU_DUMMY_MAP(RunGfxDcBtc),                  \
       __SMU_DUMMY_MAP(RunSocDcBtc),                  \
       __SMU_DUMMY_MAP(SetMemoryChannelEnable),       \
       __SMU_DUMMY_MAP(SetDfSwitchType),              \
       __SMU_DUMMY_MAP(GetVoltageByDpm),              \
       __SMU_DUMMY_MAP(GetVoltageByDpmOverdrive),     \
       __SMU_DUMMY_MAP(PowerUpVcn0),                  \
       __SMU_DUMMY_MAP(PowerDownVcn0),                \
       __SMU_DUMMY_MAP(PowerUpVcn1),                  \
       __SMU_DUMMY_MAP(PowerDownVcn1),                \
       __SMU_DUMMY_MAP(PowerUpGfx),                   \
       __SMU_DUMMY_MAP(PowerDownIspByTile),           \
       __SMU_DUMMY_MAP(PowerUpIspByTile),             \
       __SMU_DUMMY_MAP(PowerDownSdma),                \
	__SMU_DUMMY_MAP(PowerUpSdma),                 \
	__SMU_DUMMY_MAP(SetHardMinIspclkByFreq),      \
	__SMU_DUMMY_MAP(SetHardMinVcn),               \
       __SMU_DUMMY_MAP(SetHardMinVcn0),               \
       __SMU_DUMMY_MAP(SetHardMinVcn1),               \
	__SMU_DUMMY_MAP(SetAllowFclkSwitch),          \
	__SMU_DUMMY_MAP(SetMinVideoGfxclkFreq),       \
	__SMU_DUMMY_MAP(ActiveProcessNotify),         \
	__SMU_DUMMY_MAP(SetCustomPolicy),             \
	__SMU_DUMMY_MAP(QueryPowerLimit),             \
	__SMU_DUMMY_MAP(SetGfxclkOverdriveByFreqVid), \
	__SMU_DUMMY_MAP(SetHardMinDcfclkByFreq),      \
	__SMU_DUMMY_MAP(SetHardMinSocclkByFreq),      \
	__SMU_DUMMY_MAP(ControlIgpuATS),              \
	__SMU_DUMMY_MAP(SetMinVideoFclkFreq),         \
	__SMU_DUMMY_MAP(SetMinDeepSleepDcfclk),       \
	__SMU_DUMMY_MAP(ForcePowerDownGfx),           \
	__SMU_DUMMY_MAP(SetPhyclkVoltageByFreq),      \
	__SMU_DUMMY_MAP(SetDppclkVoltageByFreq),      \
	__SMU_DUMMY_MAP(SetSoftMinVcn),               \
       __SMU_DUMMY_MAP(SetSoftMinVcn0),              \
       __SMU_DUMMY_MAP(SetSoftMinVcn1),              \
	__SMU_DUMMY_MAP(EnablePostCode),              \
	__SMU_DUMMY_MAP(GetGfxclkFrequency),          \
	__SMU_DUMMY_MAP(GetFclkFrequency),            \
	__SMU_DUMMY_MAP(GetMinGfxclkFrequency),       \
	__SMU_DUMMY_MAP(GetMaxGfxclkFrequency),       \
	__SMU_DUMMY_MAP(SetGfxCGPG),                  \
	__SMU_DUMMY_MAP(SetSoftMaxGfxClk),            \
	__SMU_DUMMY_MAP(SetHardMinGfxClk),            \
	__SMU_DUMMY_MAP(SetSoftMaxSocclkByFreq),      \
	__SMU_DUMMY_MAP(SetSoftMaxFclkByFreq),        \
	__SMU_DUMMY_MAP(SetSoftMaxVcn),               \
       __SMU_DUMMY_MAP(SetSoftMaxVcn0),              \
       __SMU_DUMMY_MAP(SetSoftMaxVcn1),              \
	__SMU_DUMMY_MAP(PowerGateMmHub),              \
	__SMU_DUMMY_MAP(UpdatePmeRestore),            \
	__SMU_DUMMY_MAP(GpuChangeState),              \
	__SMU_DUMMY_MAP(SetPowerLimitPercentage),     \
	__SMU_DUMMY_MAP(ForceGfxContentSave),         \
	__SMU_DUMMY_MAP(EnableTmdp48MHzRefclkPwrDown),\
	__SMU_DUMMY_MAP(PowerGateAtHub),              \
	__SMU_DUMMY_MAP(SetSoftMinJpeg),              \
	__SMU_DUMMY_MAP(SetHardMinFclkByFreq),        \
	__SMU_DUMMY_MAP(DFCstateControl), \
	__SMU_DUMMY_MAP(GmiPwrDnControl), \
	__SMU_DUMMY_MAP(spare), \
	__SMU_DUMMY_MAP(SetNumBadHbmPagesRetired), \
	__SMU_DUMMY_MAP(GetGmiPwrDnHyst), \
	__SMU_DUMMY_MAP(SetGmiPwrDnHyst), \
	__SMU_DUMMY_MAP(EnterGfxoff), \
	__SMU_DUMMY_MAP(ExitGfxoff), \
	__SMU_DUMMY_MAP(SetExecuteDMATest), \
	__SMU_DUMMY_MAP(DAL_DISABLE_DUMMY_PSTATE_CHANGE), \
	__SMU_DUMMY_MAP(DAL_ENABLE_DUMMY_PSTATE_CHANGE), \
	__SMU_DUMMY_MAP(SET_DRIVER_DUMMY_TABLE_DRAM_ADDR_HIGH), \
	__SMU_DUMMY_MAP(SET_DRIVER_DUMMY_TABLE_DRAM_ADDR_LOW), \
	__SMU_DUMMY_MAP(GET_UMC_FW_WA), \
	__SMU_DUMMY_MAP(Mode1Reset), \
	__SMU_DUMMY_MAP(RlcPowerNotify),                 \
	__SMU_DUMMY_MAP(SetHardMinIspiclkByFreq),        \
	__SMU_DUMMY_MAP(SetHardMinIspxclkByFreq),        \
	__SMU_DUMMY_MAP(SetSoftMinSocclkByFreq),         \
	__SMU_DUMMY_MAP(PowerUpCvip),                    \
	__SMU_DUMMY_MAP(PowerDownCvip),                  \
       __SMU_DUMMY_MAP(EnableGfxOff),                   \
       __SMU_DUMMY_MAP(DisableGfxOff),                   \
       __SMU_DUMMY_MAP(SetSoftMinGfxclk),               \
       __SMU_DUMMY_MAP(SetSoftMinFclk),                 \
       __SMU_DUMMY_MAP(GetThermalLimit),                \
       __SMU_DUMMY_MAP(GetCurrentTemperature),          \
       __SMU_DUMMY_MAP(GetCurrentPower),                \
       __SMU_DUMMY_MAP(GetCurrentVoltage),              \
       __SMU_DUMMY_MAP(GetCurrentCurrent),              \
       __SMU_DUMMY_MAP(GetAverageCpuActivity),          \
       __SMU_DUMMY_MAP(GetAverageGfxActivity),          \
       __SMU_DUMMY_MAP(GetAveragePower),                \
       __SMU_DUMMY_MAP(GetAverageTemperature),          \
       __SMU_DUMMY_MAP(SetAveragePowerTimeConstant),        \
       __SMU_DUMMY_MAP(SetAverageActivityTimeConstant),     \
       __SMU_DUMMY_MAP(SetAverageTemperatureTimeConstant),  \
       __SMU_DUMMY_MAP(SetMitigationEndHysteresis),         \
       __SMU_DUMMY_MAP(GetCurrentFreq),                     \
       __SMU_DUMMY_MAP(SetReducedPptLimit),                 \
       __SMU_DUMMY_MAP(SetReducedThermalLimit),             \
       __SMU_DUMMY_MAP(DramLogSetDramAddr),                 \
       __SMU_DUMMY_MAP(StartDramLogging),                   \
       __SMU_DUMMY_MAP(StopDramLogging),                    \
       __SMU_DUMMY_MAP(SetSoftMinCclk),                     \
       __SMU_DUMMY_MAP(SetSoftMaxCclk),                     \
	__SMU_DUMMY_MAP(SetGpoFeaturePMask),             \
	__SMU_DUMMY_MAP(DisallowGpo),                    \
	__SMU_DUMMY_MAP(Enable2ndUSB20Port),             \
	__SMU_DUMMY_MAP(RequestActiveWgp),               \
       __SMU_DUMMY_MAP(SetFastPPTLimit),                \
       __SMU_DUMMY_MAP(SetSlowPPTLimit),                \
       __SMU_DUMMY_MAP(GetFastPPTLimit),                \
       __SMU_DUMMY_MAP(GetSlowPPTLimit),                \
	__SMU_DUMMY_MAP(EnableDeterminism),		\
	__SMU_DUMMY_MAP(DisableDeterminism),		\
	__SMU_DUMMY_MAP(SetUclkDpmMode),		\
	__SMU_DUMMY_MAP(LightSBR),			\
	__SMU_DUMMY_MAP(GfxDriverResetRecovery),	\
	__SMU_DUMMY_MAP(BoardPowerCalibration),   \
	__SMU_DUMMY_MAP(RequestGfxclk),           \
	__SMU_DUMMY_MAP(ForceGfxVid),             \
	__SMU_DUMMY_MAP(Spare0),                  \
	__SMU_DUMMY_MAP(UnforceGfxVid),           \
	__SMU_DUMMY_MAP(HeavySBR),			\
	__SMU_DUMMY_MAP(SetBadHBMPagesRetiredFlagsPerChannel), \
	__SMU_DUMMY_MAP(EnableGfxImu), \
	__SMU_DUMMY_MAP(DriverMode2Reset), \
	__SMU_DUMMY_MAP(GetGfxOffStatus),		 \
	__SMU_DUMMY_MAP(GetGfxOffEntryCount),		 \
	__SMU_DUMMY_MAP(LogGfxOffResidency),			\
	__SMU_DUMMY_MAP(SetNumBadMemoryPagesRetired),		\
	__SMU_DUMMY_MAP(SetBadMemoryPagesRetiredFlagsPerChannel), \
	__SMU_DUMMY_MAP(AllowGpo),	\
	__SMU_DUMMY_MAP(Mode2Reset),	\
	__SMU_DUMMY_MAP(RequestI2cTransaction), \
	__SMU_DUMMY_MAP(GetMetricsTable), \
	__SMU_DUMMY_MAP(DALNotPresent), \
	__SMU_DUMMY_MAP(ClearMcaOnRead),	\
	__SMU_DUMMY_MAP(QueryValidMcaCount),	\
	__SMU_DUMMY_MAP(QueryValidMcaCeCount),	\
	__SMU_DUMMY_MAP(McaBankDumpDW),		\
	__SMU_DUMMY_MAP(McaBankCeDumpDW),	\
	__SMU_DUMMY_MAP(SelectPLPDMode),	\
	__SMU_DUMMY_MAP(PowerUpVpe),	\
	__SMU_DUMMY_MAP(PowerDownVpe), \
	__SMU_DUMMY_MAP(PowerUpUmsch),	\
	__SMU_DUMMY_MAP(PowerDownUmsch),	\
	__SMU_DUMMY_MAP(SetSoftMaxVpe),	\
	__SMU_DUMMY_MAP(SetSoftMinVpe), \
	__SMU_DUMMY_MAP(GetMetricsVersion), \
	__SMU_DUMMY_MAP(EnableUCLKShadow), \
	__SMU_DUMMY_MAP(RmaDueToBadPageThreshold), \
	__SMU_DUMMY_MAP(SetThrottlingPolicy), \
	__SMU_DUMMY_MAP(MALLPowerController), \
	__SMU_DUMMY_MAP(MALLPowerState), \
	__SMU_DUMMY_MAP(ResetSDMA), \
	__SMU_DUMMY_MAP(GetStaticMetricsTable),

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(type)	SMU_MSG_##type
enum smu_message_type {
	SMU_MESSAGE_TYPES
	SMU_MSG_MAX_COUNT,
};

enum smu_clk_type {
	SMU_GFXCLK,
	SMU_VCLK,
	SMU_DCLK,
	SMU_VCLK1,
	SMU_DCLK1,
	SMU_ECLK,
	SMU_SOCCLK,
	SMU_UCLK,
	SMU_DCEFCLK,
	SMU_DISPCLK,
	SMU_PIXCLK,
	SMU_PHYCLK,
	SMU_FCLK,
	SMU_SCLK,
	SMU_MCLK,
	SMU_PCIE,
	SMU_LCLK,
	SMU_OD_CCLK,
	SMU_OD_SCLK,
	SMU_OD_MCLK,
	SMU_OD_VDDC_CURVE,
	SMU_OD_RANGE,
	SMU_OD_VDDGFX_OFFSET,
	SMU_OD_FAN_CURVE,
	SMU_OD_ACOUSTIC_LIMIT,
	SMU_OD_ACOUSTIC_TARGET,
	SMU_OD_FAN_TARGET_TEMPERATURE,
	SMU_OD_FAN_MINIMUM_PWM,
	SMU_OD_FAN_ZERO_RPM_ENABLE,
	SMU_OD_FAN_ZERO_RPM_STOP_TEMP,
	SMU_CLK_COUNT,
};

#define SMU_FEATURE_MASKS				\
       __SMU_DUMMY_MAP(DPM_PREFETCHER),			\
       __SMU_DUMMY_MAP(DPM_GFXCLK),                    	\
       __SMU_DUMMY_MAP(DPM_UCLK),                      	\
       __SMU_DUMMY_MAP(DPM_SOCCLK),                    	\
       __SMU_DUMMY_MAP(DPM_UVD),                       	\
       __SMU_DUMMY_MAP(DPM_VCE),                       	\
       __SMU_DUMMY_MAP(DPM_LCLK),                       \
       __SMU_DUMMY_MAP(ULV),                           	\
       __SMU_DUMMY_MAP(DPM_MP0CLK),                    	\
       __SMU_DUMMY_MAP(DPM_LINK),                      	\
       __SMU_DUMMY_MAP(DPM_DCEFCLK),                   	\
       __SMU_DUMMY_MAP(DPM_XGMI),			\
       __SMU_DUMMY_MAP(DS_GFXCLK),                     	\
       __SMU_DUMMY_MAP(DS_SOCCLK),                     	\
       __SMU_DUMMY_MAP(DS_LCLK),                       	\
       __SMU_DUMMY_MAP(PPT),                           	\
       __SMU_DUMMY_MAP(TDC),                           	\
       __SMU_DUMMY_MAP(THERMAL),                       	\
       __SMU_DUMMY_MAP(GFX_PER_CU_CG),                 	\
       __SMU_DUMMY_MAP(DATA_CALCULATIONS),                 	\
       __SMU_DUMMY_MAP(RM),                            	\
       __SMU_DUMMY_MAP(DS_DCEFCLK),                    	\
       __SMU_DUMMY_MAP(ACDC),                          	\
       __SMU_DUMMY_MAP(VR0HOT),                        	\
       __SMU_DUMMY_MAP(VR1HOT),                        	\
       __SMU_DUMMY_MAP(FW_CTF),                        	\
       __SMU_DUMMY_MAP(LED_DISPLAY),                   	\
       __SMU_DUMMY_MAP(FAN_CONTROL),                   	\
       __SMU_DUMMY_MAP(GFX_EDC),                       	\
       __SMU_DUMMY_MAP(GFXOFF),                        	\
       __SMU_DUMMY_MAP(CG),                            	\
       __SMU_DUMMY_MAP(DPM_FCLK),                      	\
       __SMU_DUMMY_MAP(DS_FCLK),                       	\
       __SMU_DUMMY_MAP(DS_MP1CLK),                     	\
       __SMU_DUMMY_MAP(DS_MP0CLK),                     	\
       __SMU_DUMMY_MAP(DS_MPIOCLK),                     \
       __SMU_DUMMY_MAP(XGMI_PER_LINK_PWR_DWN),          \
       __SMU_DUMMY_MAP(DPM_GFX_PACE),                  	\
       __SMU_DUMMY_MAP(MEM_VDDCI_SCALING),             	\
       __SMU_DUMMY_MAP(MEM_MVDD_SCALING),              	\
       __SMU_DUMMY_MAP(DS_UCLK),                       	\
       __SMU_DUMMY_MAP(GFX_ULV),                       	\
       __SMU_DUMMY_MAP(FW_DSTATE),                     	\
       __SMU_DUMMY_MAP(BACO),                          	\
       __SMU_DUMMY_MAP(VCN_PG),                        	\
       __SMU_DUMMY_MAP(MM_DPM_PG),                     	\
       __SMU_DUMMY_MAP(JPEG_PG),                       	\
       __SMU_DUMMY_MAP(USB_PG),                        	\
       __SMU_DUMMY_MAP(RSMU_SMN_CG),                   	\
       __SMU_DUMMY_MAP(APCC_PLUS),                     	\
       __SMU_DUMMY_MAP(GTHR),                          	\
       __SMU_DUMMY_MAP(GFX_DCS),                       	\
       __SMU_DUMMY_MAP(GFX_SS),                        	\
       __SMU_DUMMY_MAP(OUT_OF_BAND_MONITOR),           	\
       __SMU_DUMMY_MAP(TEMP_DEPENDENT_VMIN),           	\
       __SMU_DUMMY_MAP(MMHUB_PG),                      	\
       __SMU_DUMMY_MAP(ATHUB_PG),                      	\
       __SMU_DUMMY_MAP(APCC_DFLL),                     	\
       __SMU_DUMMY_MAP(DF_CSTATE),                     	\
       __SMU_DUMMY_MAP(DPM_GFX_GPO),                    \
       __SMU_DUMMY_MAP(WAFL_CG),                        \
       __SMU_DUMMY_MAP(CCLK_DPM),                     	\
       __SMU_DUMMY_MAP(FAN_CONTROLLER),                 \
       __SMU_DUMMY_MAP(VCN_DPM),                     	\
       __SMU_DUMMY_MAP(LCLK_DPM),                     	\
       __SMU_DUMMY_MAP(SHUBCLK_DPM),                    \
       __SMU_DUMMY_MAP(DCFCLK_DPM),                     \
       __SMU_DUMMY_MAP(DS_DCFCLK),                     	\
       __SMU_DUMMY_MAP(S0I2),                     	\
       __SMU_DUMMY_MAP(SMU_LOW_POWER),                  \
       __SMU_DUMMY_MAP(GFX_DEM),                        \
       __SMU_DUMMY_MAP(PSI),                     	\
       __SMU_DUMMY_MAP(PROCHOT),                        \
       __SMU_DUMMY_MAP(CPUOFF),                     	\
       __SMU_DUMMY_MAP(STAPM),                          \
       __SMU_DUMMY_MAP(S0I3),                     	\
       __SMU_DUMMY_MAP(DF_CSTATES),                     \
       __SMU_DUMMY_MAP(PERF_LIMIT),                     \
       __SMU_DUMMY_MAP(CORE_DLDO),                     	\
       __SMU_DUMMY_MAP(RSMU_LOW_POWER),                 \
       __SMU_DUMMY_MAP(SMN_LOW_POWER),                  \
       __SMU_DUMMY_MAP(THM_LOW_POWER),                  \
       __SMU_DUMMY_MAP(SMUIO_LOW_POWER),                \
       __SMU_DUMMY_MAP(MP1_LOW_POWER),                  \
       __SMU_DUMMY_MAP(DS_VCN),                         \
       __SMU_DUMMY_MAP(CPPC),                           \
       __SMU_DUMMY_MAP(OS_CSTATES),                     \
       __SMU_DUMMY_MAP(ISP_DPM),                        \
       __SMU_DUMMY_MAP(A55_DPM),                        \
       __SMU_DUMMY_MAP(CVIP_DSP_DPM),                   \
       __SMU_DUMMY_MAP(MSMU_LOW_POWER),			\
       __SMU_DUMMY_MAP(FUSE_CG),			\
       __SMU_DUMMY_MAP(MP1_CG),				\
       __SMU_DUMMY_MAP(SMUIO_CG),			\
       __SMU_DUMMY_MAP(THM_CG),				\
       __SMU_DUMMY_MAP(CLK_CG),				\
       __SMU_DUMMY_MAP(DATA_CALCULATION),				\
       __SMU_DUMMY_MAP(DPM_VCLK),			\
       __SMU_DUMMY_MAP(DPM_DCLK),			\
       __SMU_DUMMY_MAP(FW_DATA_READ),			\
       __SMU_DUMMY_MAP(DPM_GFX_POWER_OPTIMIZER),	\
       __SMU_DUMMY_MAP(DPM_DCN),			\
       __SMU_DUMMY_MAP(VMEMP_SCALING),			\
       __SMU_DUMMY_MAP(VDDIO_MEM_SCALING),		\
       __SMU_DUMMY_MAP(MM_DPM),				\
       __SMU_DUMMY_MAP(SOC_MPCLK_DS),			\
       __SMU_DUMMY_MAP(BACO_MPCLK_DS),			\
       __SMU_DUMMY_MAP(THROTTLERS),			\
       __SMU_DUMMY_MAP(SMARTSHIFT),			\
       __SMU_DUMMY_MAP(GFX_READ_MARGIN),		\
       __SMU_DUMMY_MAP(GFX_IMU),			\
       __SMU_DUMMY_MAP(GFX_PCC_DFLL),			\
       __SMU_DUMMY_MAP(BOOT_TIME_CAL),			\
       __SMU_DUMMY_MAP(BOOT_POWER_OPT),			\
       __SMU_DUMMY_MAP(GFXCLK_SPREAD_SPECTRUM),		\
       __SMU_DUMMY_MAP(SOC_PCC),			\
       __SMU_DUMMY_MAP(OPTIMIZED_VMIN),			\
       __SMU_DUMMY_MAP(CLOCK_POWER_DOWN_BYPASS),	\
       __SMU_DUMMY_MAP(MEM_TEMP_READ),			\
       __SMU_DUMMY_MAP(ATHUB_MMHUB_PG),			\
       __SMU_DUMMY_MAP(BACO_CG),			\
       __SMU_DUMMY_MAP(SOC_CG),    \
       __SMU_DUMMY_MAP(LOW_POWER_DCNCLKS),       \
       __SMU_DUMMY_MAP(WHISPER_MODE),			\
       __SMU_DUMMY_MAP(EDC_PWRBRK),				\
       __SMU_DUMMY_MAP(SOC_EDC_XVMIN),				\
       __SMU_DUMMY_MAP(GFX_PSM_DIDT),				\
       __SMU_DUMMY_MAP(APT_ALL_ENABLE),				\
       __SMU_DUMMY_MAP(APT_SQ_THROTTLE),				\
       __SMU_DUMMY_MAP(APT_PF_DCS),				\
       __SMU_DUMMY_MAP(GFX_EDC_XVMIN),				\
       __SMU_DUMMY_MAP(GFX_DIDT_XVMIN),				\
       __SMU_DUMMY_MAP(FAN_ABNORMAL),				\
       __SMU_DUMMY_MAP(PIT),

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(feature)	SMU_FEATURE_##feature##_BIT
enum smu_feature_mask {
	SMU_FEATURE_MASKS
	SMU_FEATURE_COUNT,
};

/* Message category flags */
#define SMU_MSG_VF_FLAG			(1U << 0)
#define SMU_MSG_RAS_PRI			(1U << 1)

/* Firmware capability flags */
#define SMU_FW_CAP_RAS_PRI		(1U << 0)

#endif
