/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef SMU_15_0_8_PMFW_H
#define SMU_15_0_8_PMFW_H

#define NUM_VCLK_DPM_LEVELS 		4
#define NUM_DCLK_DPM_LEVELS 		4
#define NUM_SOCCLK_DPM_LEVELS 		4
#define NUM_LCLK_DPM_LEVELS 		4
#define NUM_UCLK_DPM_LEVELS 		4
#define NUM_FCLK_DPM_LEVELS 		4
#define NUM_XGMI_DPM_LEVELS 		2
#define NUM_PCIE_BITRATES 		4
#define NUM_XGMI_BITRATES 		4
#define NUM_XGMI_WIDTHS 		3
#define NUM_GFX_P2S_TABLES 		8
#define NUM_PSM_DIDT_THRESHOLDS 	3
#define NUM_XCD_XVMIN_VMIN_THRESHOLDS 3

#define PRODUCT_MODEL_NUMBER_LEN      20
#define PRODUCT_NAME_LEN              64
#define PRODUCT_SERIAL_LEN            20
#define PRODUCT_MANUFACTURER_NAME_LEN 32
#define PRODUCT_FRU_ID_LEN            32

//Feature ID list
#define FEATURE_ID_DATA_CALCULATION       1
#define FEATURE_ID_DPM_FCLK               2
#define FEATURE_ID_DPM_GFXCLK             3
#define FEATURE_ID_DPM_SPARE_4            4
#define FEATURE_ID_DPM_SPARE_5            5
#define FEATURE_ID_DPM_UCLK               6
#define FEATURE_ID_DPM_SPARE_7            7
#define FEATURE_ID_DPM_XGMI               8
#define FEATURE_ID_DS_FCLK                9
#define FEATURE_ID_DS_GFXCLK              10
#define FEATURE_ID_DS_LCLK                11
#define FEATURE_ID_DS_MP0CLK              12
#define FEATURE_ID_DS_MP1CLK              13
#define FEATURE_ID_DS_MPIOCLK             14
#define FEATURE_ID_DS_SOCCLK              15
#define FEATURE_ID_DS_VCN                 16
#define FEATURE_ID_PPT                    17
#define FEATURE_ID_TDC                    18
#define FEATURE_ID_THERMAL                19
#define FEATURE_ID_SOC_PCC                20
#define FEATURE_ID_PROCHOT                21
#define FEATURE_ID_XVMIN0_VMIN_AID        22
#define FEATURE_ID_XVMIN1_DD_AID          23
#define FEATURE_ID_XVMIN0_VMIN_XCD        24
#define FEATURE_ID_XVMIN1_DD_XCD          25
#define FEATURE_ID_FW_CTF                 26
#define FEATURE_ID_MGCG                   27
#define FEATURE_ID_PSI7                   28
#define FEATURE_ID_XGMI_PER_LINK_PWR_DOWN 29
#define FEATURE_ID_SOC_DC_RTC             30
#define FEATURE_ID_GFX_DC_RTC             31
#define FEATURE_ID_DVM_MIN_PSM            32
#define FEATURE_ID_PRC                    33
#define FEATURE_ID_PSM_DIDT               34
#define FEATURE_ID_PIT                    35
#define FEATURE_ID_DVO                    36
#define FEATURE_ID_XVMIN_CLKSTOP_DS       37
#define FEATURE_ID_HBM_THROTTLE_CTRL      38
#define FEATURE_ID_DPM_GL2CLK             39
#define FEATURE_ID_GC_CAC_EDC             40
#define FEATURE_ID_DS_DMABECLK            41
#define FEATURE_ID_DS_MPIFOECLK           42
#define FEATURE_ID_DS_MPRASCLK            43
#define FEATURE_ID_DS_MPNHTCLK            44
#define FEATURE_ID_DS_FIOCLK              45
#define FEATURE_ID_DS_DXIOCLK             46
#define FEATURE_ID_PCC                    47
#define FEATURE_ID_OCP                    48
#define FEATURE_ID_TRO                    49
#define FEATURE_ID_GL2_CAC_EDC            50
#define FEATURE_ID_SPARE_51               51
#define FEATURE_ID_GL2_CGCG               52
#define FEATURE_ID_XCAC                   53
#define FEATURE_ID_DS_GL2CLK              54
#define FEATURE_ID_FCS_VIN_PCC            55
#define FEATURE_ID_FCS_VDDX_OCP_WARN      56
#define FEATURE_ID_FCS_PWRBRK             57
#define FEATURE_ID_DF_CSTATE              58
#define FEATURE_ID_ARO                    59
#define FEATURE_ID_PS_PsPowerLimit        60
#define FEATURE_ID_PS_PsPowerFloor        61
#define FEATURE_ID_OCPWARNRC              62
#define FEATURE_ID_XGMI_FOLDING           63
#define FEATURE_ID_SMU_CG                 64
#define NUM_FEATURES                      65

//MGCG Feature ID List
#define WAFL_CG                 0
#define SMU_FUSE_CG_DEEPSLEEP   1
#define SMUIO_CG                2
#define RSMU_MGCG               3
#define SMU_CLK_MGCG            4
#define MP5_CG                  5
#define UMC_CG                  6
#define WAFL0_CLK               7
#define WAFL1_CLK               8
#define VCN_MGCG                9
#define GL2_MGCG                10
#define MGCG_NUM_FEATURES       11

/* enum for MPIO PCIe gen speed msgs */
typedef enum {
	PCIE_LINK_SPEED_INDEX_TABLE_GEN1,
	PCIE_LINK_SPEED_INDEX_TABLE_GEN2,
	PCIE_LINK_SPEED_INDEX_TABLE_GEN3,
	PCIE_LINK_SPEED_INDEX_TABLE_GEN4,
	PCIE_LINK_SPEED_INDEX_TABLE_GEN5,
	PCIE_LINK_SPEED_INDEX_TABLE_GEN6,
	PCIE_LINK_SPEED_INDEX_TABLE_GEN6_ESM,
	PCIE_LINK_SPEED_INDEX_TABLE_COUNT
} PCIE_LINK_SPEED_INDEX_TABLE_e;

typedef enum {
	GFX_GUARDBAND_OFFSET_0,
	GFX_GUARDBAND_OFFSET_1,
	GFX_GUARDBAND_OFFSET_2,
	GFX_GUARDBAND_OFFSET_3,
	GFX_GUARDBAND_OFFSET_4,
	GFX_GUARDBAND_OFFSET_5,
	GFX_GUARDBAND_OFFSET_6,
	GFX_GUARDBAND_OFFSET_7,
	GFX_GUARDBAND_OFFSET_COUNT
} GFX_GUARDBAND_OFFSET_e;

typedef enum {
	GFX_DVM_MARGINHI_0,
	GFX_DVM_MARGINHI_1,
	GFX_DVM_MARGINHI_2,
	GFX_DVM_MARGINHI_3,
	GFX_DVM_MARGINHI_4,
	GFX_DVM_MARGINHI_5,
	GFX_DVM_MARGINHI_6,
	GFX_DVM_MARGINHI_7,
	GFX_DVM_MARGINLO_0,
	GFX_DVM_MARGINLO_1,
	GFX_DVM_MARGINLO_2,
	GFX_DVM_MARGINLO_3,
	GFX_DVM_MARGINLO_4,
	GFX_DVM_MARGINLO_5,
	GFX_DVM_MARGINLO_6,
	GFX_DVM_MARGINLO_7,
	GFX_DVM_MARGIN_COUNT
} GFX_DVM_MARGIN_e;

typedef enum{
  SYSTEM_TEMP_UBB_FPGA,
  SYSTEM_TEMP_UBB_FRONT,
  SYSTEM_TEMP_UBB_BACK,
  SYSTEM_TEMP_UBB_OAM7,
  SYSTEM_TEMP_UBB_IBC,
  SYSTEM_TEMP_UBB_UFPGA,
  SYSTEM_TEMP_UBB_OAM1,
  SYSTEM_TEMP_OAM_0_1_HSC,
  SYSTEM_TEMP_OAM_2_3_HSC,
  SYSTEM_TEMP_OAM_4_5_HSC,
  SYSTEM_TEMP_OAM_6_7_HSC,
  SYSTEM_TEMP_UBB_FPGA_0V72_VR,
  SYSTEM_TEMP_UBB_FPGA_3V3_VR,
  SYSTEM_TEMP_RETIMER_0_1_2_3_1V2_VR,
  SYSTEM_TEMP_RETIMER_4_5_6_7_1V2_VR,
  SYSTEM_TEMP_RETIMER_0_1_0V9_VR,
  SYSTEM_TEMP_RETIMER_4_5_0V9_VR,
  SYSTEM_TEMP_RETIMER_2_3_0V9_VR,
  SYSTEM_TEMP_RETIMER_6_7_0V9_VR,
  SYSTEM_TEMP_OAM_0_1_2_3_3V3_VR,
  SYSTEM_TEMP_OAM_4_5_6_7_3V3_VR,
  SYSTEM_TEMP_IBC_HSC,
  SYSTEM_TEMP_IBC,
  SYSTEM_TEMP_MAX_ENTRIES   = 32
} SYSTEM_TEMP_e;

typedef enum{
  NODE_TEMP_RETIMER,
  NODE_TEMP_IBC_TEMP,
  NODE_TEMP_IBC_2_TEMP,
  NODE_TEMP_VDD18_VR_TEMP,
  NODE_TEMP_04_HBM_B_VR_TEMP,
  NODE_TEMP_04_HBM_D_VR_TEMP,
  NODE_TEMP_MAX_TEMP_ENTRIES    = 12
} NODE_TEMP_e;

typedef enum {
  SVI_PLANE_VDDCR_X0_TEMP,
  SVI_PLANE_VDDCR_X1_TEMP,

  SVI_PLANE_VDDIO_HBM_B_TEMP,
  SVI_PLANE_VDDIO_HBM_D_TEMP,
  SVI_PLANE_VDDIO_04_HBM_B_TEMP,
  SVI_PLANE_VDDIO_04_HBM_D_TEMP,
  SVI_PLANE_VDDCR_HBM_B_TEMP,
  SVI_PLANE_VDDCR_HBM_D_TEMP,
  SVI_PLANE_VDDCR_075_HBM_B_TEMP,
  SVI_PLANE_VDDCR_075_HBM_D_TEMP,

  SVI_PLANE_VDDIO_11_GTA_A_TEMP,
  SVI_PLANE_VDDIO_11_GTA_C_TEMP,
  SVI_PLANE_VDDAN_075_GTA_A_TEMP,
  SVI_PLANE_VDDAN_075_GTA_C_TEMP,

  SVI_PLANE_VDDCR_075_UCIE_TEMP,
  SVI_PLANE_VDDIO_065_UCIEAA_TEMP,
  SVI_PLANE_VDDIO_065_UCIEAM_A_TEMP,
  SVI_PLANE_VDDIO_065_UCIEAM_C_TEMP,

  SVI_PLANE_VDDCR_SOCIO_A_TEMP,
  SVI_PLANE_VDDCR_SOCIO_C_TEMP,

  SVI_PLANE_VDDAN_075_TEMP,
  SVI_MAX_TEMP_ENTRIES,   //22
} SVI_TEMP_e;

typedef enum{
  SYSTEM_POWER_UBB_POWER,
  SYSTEM_POWER_UBB_POWER_THRESHOLD,
  SYSTEM_POWER_MAX_ENTRIES_WO_RESERVED,
  SYSTEM_POWER_MAX_ENTRIES  = 4
} SYSTEM_POWER_e;

#define SMU_METRICS_TABLE_VERSION 0xF

typedef struct __attribute__((packed, aligned(4))) {
  uint64_t AccumulationCounter;

  //TEMPERATURE
  uint32_t MaxSocketTemperature;
  uint32_t MaxVrTemperature;
  uint32_t HbmTemperature[12];
  uint64_t MaxSocketTemperatureAcc;
  uint64_t MaxVrTemperatureAcc;
  uint64_t HbmTemperatureAcc[12];
  uint32_t MidTemperature[2];
  uint32_t AidTemperature[2];
  uint32_t XcdTemperature[8];

  //POWER
  uint32_t SocketPowerLimit;
  uint32_t SocketPower;

  //ENERGY
  uint64_t Timestamp;
  uint64_t SocketEnergyAcc;
  uint64_t HbmEnergyAcc;

  //FREQUENCY
  uint32_t GfxclkFrequencyLimit;
  uint32_t FclkFrequency[2];
  uint32_t UclkFrequency[2];
  uint64_t GfxclkFrequencyAcc[8];
  uint32_t GfxclkFrequency[8];
  uint32_t SocclkFrequency[2];
  uint32_t VclkFrequency[4];
  uint32_t DclkFrequency[4];
  uint32_t LclkFrequency[2];

  //XGMI:
  uint32_t XgmiWidth;
  uint32_t XgmiBitrate;
  uint64_t XgmiReadBandwidthAcc;
  uint64_t XgmiWriteBandwidthAcc;

  //ACTIVITY:
  uint32_t SocketGfxBusy;
  uint32_t DramBandwidthUtilization;
  uint64_t SocketGfxBusyAcc;
  uint64_t DramBandwidthAcc;
  uint32_t MaxDramBandwidth;
  uint64_t DramBandwidthUtilizationAcc;
  uint64_t PcieBandwidthAcc[2];

  //THROTTLERS
  uint64_t ProchotResidencyAcc;
  uint64_t PptResidencyAcc;
  uint64_t SocketThmResidencyAcc;
  uint64_t VrThmResidencyAcc;
  uint64_t HbmThmResidencyAcc;

  //PCIE BW Data and error count
  uint32_t PcieBandwidth[2];
  uint64_t PCIeL0ToRecoveryCountAcc;
  uint64_t PCIenReplayAAcc;
  uint64_t PCIenReplayARolloverCountAcc;
  uint64_t PCIeNAKSentCountAcc;
  uint64_t PCIeNAKReceivedCountAcc;
  uint64_t PCIeOtherEndRecoveryAcc;       // The Pcie counter itself is accumulated

  // VCN/JPEG ACTIVITY
  uint32_t VcnBusy[4];
  uint32_t JpegBusy[40];

  // PCIE LINK Speed and width
  uint32_t PCIeLinkSpeed;
  uint32_t PCIeLinkWidth;

  // PER XCD ACTIVITY
  uint32_t GfxBusy[8];
  uint64_t GfxBusyAcc[8];

  //NVML-Parity: Total App Clock Counter
  uint64_t GfxclkBelowHostLimitPptAcc[8];
  uint64_t GfxclkBelowHostLimitThmAcc[8];
  uint64_t GfxclkBelowHostLimitTotalAcc[8];
  uint64_t GfxclkLowUtilizationAcc[8];
} MetricsTable_t;

#define SMU_SYSTEM_METRICS_TABLE_VERSION 0x1

#pragma pack(push, 4)
typedef struct {
  uint64_t AccumulationCounter;                             // Last update timestamp
  uint16_t LabelVersion;                                    //Defaults to 0.
  uint16_t NodeIdentifier;
  int16_t  SystemTemperatures[SYSTEM_TEMP_MAX_ENTRIES];     // Signed integer temperature value in Celsius, unused fields are set to 0xFFFF
  int16_t  NodeTemperatures[NODE_TEMP_MAX_TEMP_ENTRIES];    // Signed integer temperature value in Celsius, unused fields are set to 0xFFFF
  int16_t  VrTemperatures[SVI_MAX_TEMP_ENTRIES];            // Signed integer temperature value in Celsius, 13 entries,
  int16_t  spare[7];

  //NPM: NODE POWER MANAGEMENT
  uint32_t NodePowerLimit;
  uint32_t NodePower;
  uint32_t GlobalPPTResidencyAcc;

  uint16_t SystemPower[SYSTEM_POWER_MAX_ENTRIES];           // UBB Current Power and Power Threshold
} SystemMetricsTable_t;
#pragma pack(pop)

#define SMU_VF_METRICS_TABLE_VERSION 0x5

typedef struct __attribute__((packed, aligned(4))) {
	uint32_t AccumulationCounter;
	uint32_t InstGfxclk_TargFreq;
	uint64_t AccGfxclk_TargFreq;
	uint64_t AccGfxRsmuDpm_Busy;
	uint64_t AccGfxclkBelowHostLimit;
} VfMetricsTable_t;

/* FRU product information */
typedef struct __attribute__((aligned(4))) {
  uint8_t  ModelNumber[PRODUCT_MODEL_NUMBER_LEN];
  uint8_t  Name[PRODUCT_NAME_LEN];
  uint8_t  Serial[PRODUCT_SERIAL_LEN];
  uint8_t  ManufacturerName[PRODUCT_MANUFACTURER_NAME_LEN];
  uint8_t  FruId[PRODUCT_FRU_ID_LEN];
} FRUProductInfo_t;

#define SMU_STATIC_METRICS_TABLE_VERSION 0x1

#pragma pack(push, 4)
typedef struct {
  //FRU PRODUCT INFO
  FRUProductInfo_t  ProductInfo; //from i2c

  //POWER
  uint32_t MaxSocketPowerLimit;

  //FREQUENCY RANGE
  uint32_t MaxGfxclkFrequency;
  uint32_t MinGfxclkFrequency;
  uint32_t MaxFclkFrequency;
  uint32_t MinFclkFrequency;
  uint32_t MaxGl2clkFrequency;
  uint32_t MinGl2clkFrequency;
  uint32_t UclkFrequencyTable[4];
  uint32_t SocclkFrequency;
  uint32_t LclkFrequency;
  uint32_t VclkFrequency;
  uint32_t DclkFrequency;

  //CTF limits
  uint32_t CTFLimit_MID;
  uint32_t CTFLimit_AID;
  uint32_t CTFLimit_XCD;
  uint32_t CTFLimit_HBM;

  //Thermal Throttling limits
  uint32_t ThermalLimit_MID;
  uint32_t ThermalLimit_AID;
  uint32_t ThermalLimit_XCD;
  uint32_t ThermalLimit_HBM;

  //PSNs
  uint64_t PublicSerialNumber_MID[2];
  uint64_t PublicSerialNumber_AID[2];
  uint64_t PublicSerialNumber_XCD[8];

  //XGMI
  uint32_t MaxXgmiWidth;
  uint32_t MaxXgmiBitrate;

  // Telemetry
  uint32_t InputTelemetryVoltageInmV;

  // General info
  uint32_t pldmVersion[2];

  uint32_t PPT1Max;
  uint32_t PPT1Min;
  uint32_t PPT1Default;
} StaticMetricsTable_t;
#pragma pack(pop)

#endif
