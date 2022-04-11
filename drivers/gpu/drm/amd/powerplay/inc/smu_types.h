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
       __SMU_DUMMY_MAP(NotifyPowerSource),            \
       __SMU_DUMMY_MAP(SetUclkFastSwitch),            \
       __SMU_DUMMY_MAP(SetUclkDownHyst),              \
       __SMU_DUMMY_MAP(GfxDeviceDriverReset),         \
       __SMU_DUMMY_MAP(GetCurrentRpm),                \
       __SMU_DUMMY_MAP(SetVideoFps),                  \
       __SMU_DUMMY_MAP(SetTjMax),                     \
       __SMU_DUMMY_MAP(SetFanTemperatureTarget),      \
       __SMU_DUMMY_MAP(PrepareMp1ForUnload),          \
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
       __SMU_DUMMY_MAP(BacoAudioD3PME),               \
       __SMU_DUMMY_MAP(ArmD3),                        \
       __SMU_DUMMY_MAP(RunGfxDcBtc),                  \
       __SMU_DUMMY_MAP(RunSocDcBtc),                  \
       __SMU_DUMMY_MAP(SetMemoryChannelEnable),       \
       __SMU_DUMMY_MAP(SetDfSwitchType),              \
       __SMU_DUMMY_MAP(GetVoltageByDpm),              \
       __SMU_DUMMY_MAP(GetVoltageByDpmOverdrive),     \
       __SMU_DUMMY_MAP(PowerUpVcn0),                  \
       __SMU_DUMMY_MAP(PowerDownVcn0),               \
       __SMU_DUMMY_MAP(PowerUpVcn1),                  \
       __SMU_DUMMY_MAP(PowerDownVcn1),                \
       __SMU_DUMMY_MAP(PowerUpGfx),                   \
       __SMU_DUMMY_MAP(PowerDownIspByTile),           \
       __SMU_DUMMY_MAP(PowerUpIspByTile),             \
       __SMU_DUMMY_MAP(PowerDownSdma),                \
	__SMU_DUMMY_MAP(PowerUpSdma),                 \
	__SMU_DUMMY_MAP(SetHardMinIspclkByFreq),      \
	__SMU_DUMMY_MAP(SetHardMinVcn),               \
	__SMU_DUMMY_MAP(Spare1),                      \
	__SMU_DUMMY_MAP(Spare2),           	      \
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
	__SMU_DUMMY_MAP(PowerGateMmHub),              \
	__SMU_DUMMY_MAP(UpdatePmeRestore),            \
	__SMU_DUMMY_MAP(GpuChangeState),              \
	__SMU_DUMMY_MAP(SetPowerLimitPercentage),     \
	__SMU_DUMMY_MAP(ForceGfxContentSave),         \
	__SMU_DUMMY_MAP(EnableTmdp48MHzRefclkPwrDown), \
	__SMU_DUMMY_MAP(PowerGateAtHub),              \
	__SMU_DUMMY_MAP(SetSoftMinJpeg),              \
	__SMU_DUMMY_MAP(SetHardMinFclkByFreq),        \

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
	SMU_OD_SCLK,
	SMU_OD_MCLK,
	SMU_OD_VDDC_CURVE,
	SMU_OD_RANGE,
	SMU_CLK_COUNT,
};

#define SMU_FEATURE_MASKS				\
       __SMU_DUMMY_MAP(DPM_PREFETCHER),			\
       __SMU_DUMMY_MAP(DPM_GFXCLK),                    	\
       __SMU_DUMMY_MAP(DPM_UCLK),                      	\
       __SMU_DUMMY_MAP(DPM_SOCCLK),                    	\
       __SMU_DUMMY_MAP(DPM_UVD),                       	\
       __SMU_DUMMY_MAP(DPM_VCE),                       	\
       __SMU_DUMMY_MAP(ULV),                           	\
       __SMU_DUMMY_MAP(DPM_MP0CLK),                    	\
       __SMU_DUMMY_MAP(DPM_LINK),                      	\
       __SMU_DUMMY_MAP(DPM_DCEFCLK),                   	\
       __SMU_DUMMY_MAP(DS_GFXCLK),                     	\
       __SMU_DUMMY_MAP(DS_SOCCLK),                     	\
       __SMU_DUMMY_MAP(DS_LCLK),                       	\
       __SMU_DUMMY_MAP(PPT),                           	\
       __SMU_DUMMY_MAP(TDC),                           	\
       __SMU_DUMMY_MAP(THERMAL),                       	\
       __SMU_DUMMY_MAP(GFX_PER_CU_CG),                 	\
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
       __SMU_DUMMY_MAP(XGMI),                          	\
       __SMU_DUMMY_MAP(DPM_GFX_PACE),                  	\
       __SMU_DUMMY_MAP(MEM_VDDCI_SCALING),             	\
       __SMU_DUMMY_MAP(MEM_MVDD_SCALING),              	\
       __SMU_DUMMY_MAP(DS_UCLK),                       	\
       __SMU_DUMMY_MAP(GFX_ULV),                       	\
       __SMU_DUMMY_MAP(FW_DSTATE),                     	\
       __SMU_DUMMY_MAP(BACO),                          	\
       __SMU_DUMMY_MAP(VCN_PG),                        	\
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
       __SMU_DUMMY_MAP(WAFL_CG),

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(feature)	SMU_FEATURE_##feature##_BIT
enum smu_feature_mask {
	SMU_FEATURE_MASKS
	SMU_FEATURE_COUNT,
};

#endif
