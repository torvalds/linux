/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#ifndef SMU_13_0_12_PMFW_H
#define SMU_13_0_12_PMFW_H

#define NUM_VCLK_DPM_LEVELS   4
#define NUM_DCLK_DPM_LEVELS   4
#define NUM_SOCCLK_DPM_LEVELS 4
#define NUM_LCLK_DPM_LEVELS   4
#define NUM_UCLK_DPM_LEVELS   4
#define NUM_FCLK_DPM_LEVELS   4
#define NUM_XGMI_DPM_LEVELS   2
#define NUM_CXL_BITRATES      4
#define NUM_PCIE_BITRATES     4
#define NUM_XGMI_BITRATES     4
#define NUM_XGMI_WIDTHS       3
#define NUM_TDP_GROUPS        4
#define NUM_SOC_P2S_TABLES    6
#define NUM_GFX_P2S_TABLES    8
#define NUM_PSM_DIDT_THRESHOLDS 3
#define NUM_XVMIN_VMIN_THRESHOLDS 3

#define PRODUCT_MODEL_NUMBER_LEN      20
#define PRODUCT_NAME_LEN              64
#define PRODUCT_SERIAL_LEN            20
#define PRODUCT_MANUFACTURER_NAME_LEN 32
#define PRODUCT_FRU_ID_LEN            32

typedef enum {
/*0*/   FEATURE_DATA_CALCULATION            = 0,
/*1*/   FEATURE_DPM_FCLK                    = 1,
/*2*/   FEATURE_DPM_GFXCLK                  = 2,
/*3*/   FEATURE_DPM_LCLK                    = 3,
/*4*/   FEATURE_DPM_SOCCLK                  = 4,
/*5*/   FEATURE_DPM_UCLK                    = 5,
/*6*/   FEATURE_DPM_VCN                     = 6,
/*7*/   FEATURE_DPM_XGMI                    = 7,
/*8*/   FEATURE_DS_FCLK                     = 8,
/*9*/   FEATURE_DS_GFXCLK                   = 9,
/*10*/  FEATURE_DS_LCLK                     = 10,
/*11*/  FEATURE_DS_MP0CLK                   = 11,
/*12*/  FEATURE_DS_MP1CLK                   = 12,
/*13*/  FEATURE_DS_MPIOCLK                  = 13,
/*14*/  FEATURE_DS_SOCCLK                   = 14,
/*15*/  FEATURE_DS_VCN                      = 15,
/*16*/  FEATURE_APCC_DFLL                   = 16,
/*17*/  FEATURE_APCC_PLUS                   = 17,
/*18*/  FEATURE_PPT                         = 18,
/*19*/  FEATURE_TDC                         = 19,
/*20*/  FEATURE_THERMAL                     = 20,
/*21*/  FEATURE_SOC_PCC                     = 21,
/*22*/  FEATURE_PROCHOT                     = 22,
/*23*/  FEATURE_FDD_AID_HBM                 = 23,
/*24*/  FEATURE_FDD_AID_SOC                 = 24,
/*25*/  FEATURE_FDD_XCD_EDC                 = 25,
/*26*/  FEATURE_FDD_XCD_XVMIN               = 26,
/*27*/  FEATURE_FW_CTF                      = 27,
/*28*/  FEATURE_SMU_CG                      = 28,
/*29*/  FEATURE_PSI7                        = 29,
/*30*/  FEATURE_XGMI_PER_LINK_PWR_DOWN      = 30,
/*31*/  FEATURE_SOC_DC_RTC                  = 31,
/*32*/  FEATURE_GFX_DC_RTC                  = 32,
/*33*/  FEATURE_DVM_MIN_PSM                 = 33,
/*34*/  FEATURE_PRC                         = 34,
/*35*/  FEATURE_PSM_SQ_THROTTLER            = 35,
/*36*/  FEATURE_PIT                         = 36,
/*37*/  FEATURE_DVO                         = 37,
/*38*/  FEATURE_XVMINORPSM_CLKSTOP_DS       = 38,

/*39*/  NUM_FEATURES                        = 39
} FEATURE_LIST_e;

//enum for MPIO PCIe gen speed msgs
typedef enum {
  PCIE_LINK_SPEED_INDEX_TABLE_RESERVED,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN1,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN2,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN3,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN4,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN5,
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

#define SMU_METRICS_TABLE_VERSION 0x12

typedef struct __attribute__((packed, aligned(4))) {
  uint64_t AccumulationCounter;

  //TEMPERATURE
  uint32_t MaxSocketTemperature;
  uint32_t MaxVrTemperature;
  uint32_t MaxHbmTemperature;
  uint64_t MaxSocketTemperatureAcc;
  uint64_t MaxVrTemperatureAcc;
  uint64_t MaxHbmTemperatureAcc;

  //POWER
  uint32_t SocketPowerLimit;
  uint32_t SocketPower;

  //ENERGY
  uint64_t Timestamp;
  uint64_t SocketEnergyAcc;
  uint64_t XcdEnergyAcc;
  uint64_t AidEnergyAcc;
  uint64_t HbmEnergyAcc;

  //FREQUENCY
  uint32_t GfxclkFrequencyLimit;
  uint32_t FclkFrequency;
  uint32_t UclkFrequency;
  uint32_t SocclkFrequency[4];
  uint32_t VclkFrequency[4];
  uint32_t DclkFrequency[4];
  uint32_t LclkFrequency[4];
  uint64_t GfxclkFrequencyAcc[8];

  //FREQUENCY RANGE
  uint32_t MaxLclkDpmRange;
  uint32_t MinLclkDpmRange;

  //XGMI
  uint32_t XgmiWidth;
  uint32_t XgmiBitrate;
  uint64_t XgmiReadBandwidthAcc[8];
  uint64_t XgmiWriteBandwidthAcc[8];

  //ACTIVITY
  uint32_t SocketGfxBusy;
  uint32_t DramBandwidthUtilization;
  uint64_t SocketGfxBusyAcc;
  uint64_t DramBandwidthAcc;
  uint32_t MaxDramBandwidth;
  uint64_t DramBandwidthUtilizationAcc;
  uint64_t PcieBandwidthAcc[4];

  //THROTTLERS
  uint32_t ProchotResidencyAcc;
  uint32_t PptResidencyAcc;
  uint32_t SocketThmResidencyAcc;
  uint32_t VrThmResidencyAcc;
  uint32_t HbmThmResidencyAcc;
  uint32_t GfxLockXCDMak;

  // New Items at end to maintain driver compatibility
  uint32_t GfxclkFrequency[8];

  //XGMI Data tranfser size
  uint64_t XgmiReadDataSizeAcc[8];//in KByte
  uint64_t XgmiWriteDataSizeAcc[8];//in KByte

  //PCIE BW Data and error count
  uint32_t PcieBandwidth[4];
  uint32_t PCIeL0ToRecoveryCountAcc;      // The Pcie counter itself is accumulated
  uint32_t PCIenReplayAAcc;               // The Pcie counter itself is accumulated
  uint32_t PCIenReplayARolloverCountAcc;  // The Pcie counter itself is accumulated
  uint32_t PCIeNAKSentCountAcc;           // The Pcie counter itself is accumulated
  uint32_t PCIeNAKReceivedCountAcc;       // The Pcie counter itself is accumulated

  // VCN/JPEG ACTIVITY
  uint32_t VcnBusy[4];
  uint32_t JpegBusy[40];

  // PCIE LINK Speed and width
  uint32_t PCIeLinkSpeed;
  uint32_t PCIeLinkWidth;

  // PER XCD ACTIVITY
  uint32_t GfxBusy[8];
  uint64_t GfxBusyAcc[8];

  //PCIE BW Data and error count
  uint32_t PCIeOtherEndRecoveryAcc;       // The Pcie counter itself is accumulated

  //Total App Clock Counter
  uint64_t GfxclkBelowHostLimitPptAcc[8];
  uint64_t GfxclkBelowHostLimitThmAcc[8];
  uint64_t GfxclkBelowHostLimitTotalAcc[8];
  uint64_t GfxclkLowUtilizationAcc[8];
} MetricsTable_t;

#define SMU_VF_METRICS_TABLE_MASK (1 << 31)
#define SMU_VF_METRICS_TABLE_VERSION (0x6 | SMU_VF_METRICS_TABLE_MASK)

typedef struct __attribute__((packed, aligned(4))) {
  uint32_t AccumulationCounter;
  uint32_t InstGfxclk_TargFreq;
  uint64_t AccGfxclk_TargFreq;
  uint64_t AccGfxRsmuDpm_Busy;
  uint64_t AccGfxclkBelowHostLimitPpt;
  uint64_t AccGfxclkBelowHostLimitThm;
  uint64_t AccGfxclkBelowHostLimitTotal;
  uint64_t AccGfxclkLowUtilization;
} VfMetricsTable_t;

/* FRU product information */
typedef struct __attribute__((packed, aligned(4))) {
  uint8_t  ModelNumber[PRODUCT_MODEL_NUMBER_LEN];
  uint8_t  Name[PRODUCT_NAME_LEN];
  uint8_t  Serial[PRODUCT_SERIAL_LEN];
  uint8_t  ManufacturerName[PRODUCT_MANUFACTURER_NAME_LEN];
  uint8_t  FruId[PRODUCT_FRU_ID_LEN];
} FRUProductInfo_t;

#pragma pack(push, 4)
typedef struct {
  //FRU PRODUCT INFO
  FRUProductInfo_t  ProductInfo;

  //POWER
  uint32_t MaxSocketPowerLimit;

  //FREQUENCY RANGE
  uint32_t MaxGfxclkFrequency;
  uint32_t MinGfxclkFrequency;
  uint32_t FclkFrequencyTable[4];
  uint32_t UclkFrequencyTable[4];
  uint32_t SocclkFrequencyTable[4];
  uint32_t VclkFrequencyTable[4];
  uint32_t DclkFrequencyTable[4];
  uint32_t LclkFrequencyTable[4];

  //PSNs
  uint64_t PublicSerialNumber_AID[4];
  uint64_t PublicSerialNumber_XCD[8];
} StaticMetricsTable_t;
#pragma pack(pop)

#endif
