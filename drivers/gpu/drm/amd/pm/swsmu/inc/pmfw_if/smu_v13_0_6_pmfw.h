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
#ifndef SMU_13_0_6_PMFW_H
#define SMU_13_0_6_PMFW_H

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

typedef enum {
/*0*/   FEATURE_DATA_CALCULATION            = 0,
/*1*/   FEATURE_DPM_CCLK                    = 1,
/*2*/   FEATURE_DPM_FCLK                    = 2,
/*3*/   FEATURE_DPM_GFXCLK                  = 3,
/*4*/   FEATURE_DPM_LCLK                    = 4,
/*5*/   FEATURE_DPM_SOCCLK                  = 5,
/*6*/   FEATURE_DPM_UCLK                    = 6,
/*7*/   FEATURE_DPM_VCN                     = 7,
/*8*/   FEATURE_DPM_XGMI                    = 8,
/*9*/   FEATURE_DS_FCLK                     = 9,
/*10*/  FEATURE_DS_GFXCLK                   = 10,
/*11*/  FEATURE_DS_LCLK                     = 11,
/*12*/  FEATURE_DS_MP0CLK                   = 12,
/*13*/  FEATURE_DS_MP1CLK                   = 13,
/*14*/  FEATURE_DS_MPIOCLK                  = 14,
/*15*/  FEATURE_DS_SOCCLK                   = 15,
/*16*/  FEATURE_DS_VCN                      = 16,
/*17*/  FEATURE_APCC_DFLL                   = 17,
/*18*/  FEATURE_APCC_PLUS                   = 18,
/*19*/  FEATURE_DF_CSTATE                   = 19,
/*20*/  FEATURE_CC6                         = 20,
/*21*/  FEATURE_PC6                         = 21,
/*22*/  FEATURE_CPPC                        = 22,
/*23*/  FEATURE_PPT                         = 23,
/*24*/  FEATURE_TDC                         = 24,
/*25*/  FEATURE_THERMAL                     = 25,
/*26*/  FEATURE_SOC_PCC                     = 26,
/*27*/  FEATURE_CCD_PCC                     = 27,
/*28*/  FEATURE_CCD_EDC                     = 28,
/*29*/  FEATURE_PROCHOT                     = 29,
/*30*/  FEATURE_DVO_CCLK                    = 30,
/*31*/  FEATURE_FDD_AID_HBM                 = 31,
/*32*/  FEATURE_FDD_AID_SOC                 = 32,
/*33*/  FEATURE_FDD_XCD_EDC                 = 33,
/*34*/  FEATURE_FDD_XCD_XVMIN               = 34,
/*35*/  FEATURE_FW_CTF                      = 35,
/*36*/  FEATURE_GFXOFF                      = 36,
/*37*/  FEATURE_SMU_CG                      = 37,
/*38*/  FEATURE_PSI7                        = 38,
/*39*/  FEATURE_CSTATE_BOOST                = 39,
/*40*/  FEATURE_XGMI_PER_LINK_PWR_DOWN      = 40,
/*41*/  FEATURE_CXL_QOS                     = 41,
/*42*/  FEATURE_SOC_DC_RTC                  = 42,
/*43*/  FEATURE_GFX_DC_RTC                  = 43,

/*44*/  NUM_FEATURES                        = 44
} FEATURE_LIST_e;

//enum for MPIO PCIe gen speed msgs
typedef enum {
  PCIE_LINK_SPEED_INDEX_TABLE_GEN1,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN2,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN3,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN4,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN4_ESM,
  PCIE_LINK_SPEED_INDEX_TABLE_GEN5,
  PCIE_LINK_SPEED_INDEX_TABLE_COUNT
} PCIE_LINK_SPEED_INDEX_TABLE_e;

typedef enum {
  VOLTAGE_COLD_0,
  VOLTAGE_COLD_1,
  VOLTAGE_COLD_2,
  VOLTAGE_COLD_3,
  VOLTAGE_COLD_4,
  VOLTAGE_COLD_5,
  VOLTAGE_COLD_6,
  VOLTAGE_COLD_7,
  VOLTAGE_MID_0,
  VOLTAGE_MID_1,
  VOLTAGE_MID_2,
  VOLTAGE_MID_3,
  VOLTAGE_MID_4,
  VOLTAGE_MID_5,
  VOLTAGE_MID_6,
  VOLTAGE_MID_7,
  VOLTAGE_HOT_0,
  VOLTAGE_HOT_1,
  VOLTAGE_HOT_2,
  VOLTAGE_HOT_3,
  VOLTAGE_HOT_4,
  VOLTAGE_HOT_5,
  VOLTAGE_HOT_6,
  VOLTAGE_HOT_7,
  VOLTAGE_GUARDBAND_COUNT
} GFX_GUARDBAND_e;

#define SMU_METRICS_TABLE_VERSION 0xE

typedef struct __attribute__((packed, aligned(4))) {
  uint32_t AccumulationCounter;

  //TEMPERATURE
  uint32_t MaxSocketTemperature;
  uint32_t MaxVrTemperature;
  uint32_t MaxHbmTemperature;
  uint64_t MaxSocketTemperatureAcc;
  uint64_t MaxVrTemperatureAcc;
  uint64_t MaxHbmTemperatureAcc;

  //POWER
  uint32_t SocketPowerLimit;
  uint32_t MaxSocketPowerLimit;
  uint32_t SocketPower;

  //ENERGY
  uint64_t Timestamp;
  uint64_t SocketEnergyAcc;
  uint64_t CcdEnergyAcc;
  uint64_t XcdEnergyAcc;
  uint64_t AidEnergyAcc;
  uint64_t HbmEnergyAcc;

  //FREQUENCY
  uint32_t CclkFrequencyLimit;
  uint32_t GfxclkFrequencyLimit;
  uint32_t FclkFrequency;
  uint32_t UclkFrequency;
  uint32_t SocclkFrequency[4];
  uint32_t VclkFrequency[4];
  uint32_t DclkFrequency[4];
  uint32_t LclkFrequency[4];
  uint64_t GfxclkFrequencyAcc[8];
  uint64_t CclkFrequencyAcc[96];

  //FREQUENCY RANGE
  uint32_t MaxCclkFrequency;
  uint32_t MinCclkFrequency;
  uint32_t MaxGfxclkFrequency;
  uint32_t MinGfxclkFrequency;
  uint32_t FclkFrequencyTable[4];
  uint32_t UclkFrequencyTable[4];
  uint32_t SocclkFrequencyTable[4];
  uint32_t VclkFrequencyTable[4];
  uint32_t DclkFrequencyTable[4];
  uint32_t LclkFrequencyTable[4];
  uint32_t MaxLclkDpmRange;
  uint32_t MinLclkDpmRange;

  //XGMI
  uint32_t XgmiWidth;
  uint32_t XgmiBitrate;
  uint64_t XgmiReadBandwidthAcc[8];
  uint64_t XgmiWriteBandwidthAcc[8];

  //ACTIVITY
  uint32_t SocketC0Residency;
  uint32_t SocketGfxBusy;
  uint32_t DramBandwidthUtilization;
  uint64_t SocketC0ResidencyAcc;
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

  //PSNs
  uint64_t PublicSerialNumber_AID[4];
  uint64_t PublicSerialNumber_XCD[8];
  uint64_t PublicSerialNumber_CCD[12];

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
  uint32_t JpegBusy[32];

  // PCIE LINK Speed and width
  uint32_t PCIeLinkSpeed;
  uint32_t PCIeLinkWidth;

  // PER XCD ACTIVITY
  uint32_t GfxBusy[8];
  uint64_t GfxBusyAcc[8];

  //PCIE BW Data and error count
  uint32_t PCIeOtherEndRecoveryAcc;       // The Pcie counter itself is accumulated
} MetricsTableX_t;

typedef struct __attribute__((packed, aligned(4))) {
  uint32_t AccumulationCounter;

  //TEMPERATURE
  uint32_t MaxSocketTemperature;
  uint32_t MaxVrTemperature;
  uint32_t MaxHbmTemperature;
  uint64_t MaxSocketTemperatureAcc;
  uint64_t MaxVrTemperatureAcc;
  uint64_t MaxHbmTemperatureAcc;

  //POWER
  uint32_t SocketPowerLimit;
  uint32_t MaxSocketPowerLimit;
  uint32_t SocketPower;

  //ENERGY
  uint64_t Timestamp;
  uint64_t SocketEnergyAcc;
  uint64_t CcdEnergyAcc;
  uint64_t XcdEnergyAcc;
  uint64_t AidEnergyAcc;
  uint64_t HbmEnergyAcc;

  //FREQUENCY
  uint32_t CclkFrequencyLimit;
  uint32_t GfxclkFrequencyLimit;
  uint32_t FclkFrequency;
  uint32_t UclkFrequency;
  uint32_t SocclkFrequency[4];
  uint32_t VclkFrequency[4];
  uint32_t DclkFrequency[4];
  uint32_t LclkFrequency[4];
  uint64_t GfxclkFrequencyAcc[8];
  uint64_t CclkFrequencyAcc[96];

  //FREQUENCY RANGE
  uint32_t MaxCclkFrequency;
  uint32_t MinCclkFrequency;
  uint32_t MaxGfxclkFrequency;
  uint32_t MinGfxclkFrequency;
  uint32_t FclkFrequencyTable[4];
  uint32_t UclkFrequencyTable[4];
  uint32_t SocclkFrequencyTable[4];
  uint32_t VclkFrequencyTable[4];
  uint32_t DclkFrequencyTable[4];
  uint32_t LclkFrequencyTable[4];
  uint32_t MaxLclkDpmRange;
  uint32_t MinLclkDpmRange;

  //XGMI
  uint32_t XgmiWidth;
  uint32_t XgmiBitrate;
  uint64_t XgmiReadBandwidthAcc[8];
  uint64_t XgmiWriteBandwidthAcc[8];

  //ACTIVITY
  uint32_t SocketC0Residency;
  uint32_t SocketGfxBusy;
  uint32_t DramBandwidthUtilization;
  uint64_t SocketC0ResidencyAcc;
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

  //PSNs
  uint64_t PublicSerialNumber_AID[4];
  uint64_t PublicSerialNumber_XCD[8];
  uint64_t PublicSerialNumber_CCD[12];

  //XGMI Data tranfser size
  uint64_t XgmiReadDataSizeAcc[8];//in KByte
  uint64_t XgmiWriteDataSizeAcc[8];//in KByte

  // VCN/JPEG ACTIVITY
  uint32_t VcnBusy[4];
  uint32_t JpegBusy[32];
} MetricsTableA_t;

#define SMU_VF_METRICS_TABLE_VERSION 0x3

typedef struct __attribute__((packed, aligned(4))) {
  uint32_t AccumulationCounter;
  uint32_t InstGfxclk_TargFreq;
  uint64_t AccGfxclk_TargFreq;
  uint64_t AccGfxRsmuDpm_Busy;
} VfMetricsTable_t;

#endif
