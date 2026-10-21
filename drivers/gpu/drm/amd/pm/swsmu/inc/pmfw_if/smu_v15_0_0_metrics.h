/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __SMU_V15_0_0_METRICS_H__
#define __SMU_V15_0_0_METRICS_H__

#define METRICS_TABLE_VERSION 0x04
#define NUM_MAX_CORES         12
#define NUM_CLK_DPM_LEVELS    8
#define METRIC_CORE_TYPE_MAX  4
#define METRIC_CCX_MAX        4

/* Variable Type | Description */
#pragma pack(push, 4)
typedef struct {
uint32_t AccumulationCounter;

/*SET_VOLTAGES */
uint64_t VDDCR_SetVoltage;                                              /* Acc Float (V) */
uint64_t VDDCR_SOC_SetVoltage;                                          /* Acc Float (V) */
uint64_t VDDCR_NPU_SetVoltage;                                          /* Acc Float (V) */
uint64_t VDDCR_LP_SetVoltage;                                           /* Acc Float (V) */
uint64_t VDDCR_GFX_SetVoltage;                                          /* Acc Float (V) */
uint64_t VDD_MISC_SetVoltage;                                           /* Acc Float (V) */

/*TELEMETRY_VOLTAGES */
uint64_t VDDCR_TelemetryVoltage;                                        /* Acc Float (V) */
uint64_t VDDCR_SOC_TelemetryVoltage;                                    /* Acc Float (V) */
uint64_t VDDCR_NPU_TelemetryVoltage;                                    /* Acc Float (V) */
uint64_t VDDCR_LP_TelemetryVoltage;                                     /* Acc Float (V) */
uint64_t VDDCR_GFX_TelemetryVoltage;                                    /* Acc Float (V) */
uint64_t VDD_MISC_TelemetryVoltage;                                     /* Acc Float (V) */

/*TELEMETRY_POWERS */
uint64_t VDDCR_TelemetryPower;                                          /* Acc Float (W) */
uint64_t VDDCR_SOC_TelemetryPower;                                      /* Acc Float (W) */
uint64_t VDDCR_NPU_TelemetryPower;                                      /* Acc Float (W) */
uint64_t VDDCR_LP_TelemetryPower;                                       /* Acc Float (W) */
uint64_t VDDCR_GFX_TelemetryPower;                                      /* Acc Float (W) */
uint64_t VDD_MISC_TelemetryPower;                                       /* Acc Float (W) */

/*THROTTLERS */
uint32_t fPPT_FusedLimit;                                               /* Inst Float (W) */
uint32_t fPPT_MaxIrmLimit;                                              /* Inst Float (W) */
uint32_t fPPT_MaxPboLimit;                                              /* Inst Float (W) */
uint32_t fPPT_Limit;                                                    /* Inst Float (W) */
uint64_t fPPT_ValueAcc;                                                 /* Acc Int (W) */
uint32_t fPPT_ResidencyAcc;                                             /* Acc Float */

uint32_t sPPT_FusedLimit;                                               /* Inst Float (W) */
uint32_t sPPT_MaxIrmLimit;                                              /* Inst Float (W) */
uint32_t sPPT_MaxPboLimit;                                              /* Inst Float (W) */
uint32_t sPPT_Limit;                                                    /* Inst Float (W) */
uint64_t sPPT_ValueAcc;                                                 /* Acc Int (W) */
uint32_t sPPT_ResidencyAcc;                                             /* Acc Float */

uint32_t SPL_FusedLimit;                                                /* Inst Float (W) */
uint32_t SPL_MaxIrmLimit;                                               /* Inst Float (W) */
uint32_t SPL_MaxPboLimit;                                               /* Inst Float (W) */
uint32_t SPL_Limit;                                                     /* Inst Float (W) */
uint64_t SPL_ValueAcc;                                                  /* Acc Int (W) */
uint32_t SPL_ResidencyAcc;                                              /* Acc Float */

uint32_t TDC_VDDCR_FusedLimit;                                          /* Inst Float (A) */
uint32_t TDC_VDDCR_MaxIrmLimit;                                         /* Inst Float (A) */
uint32_t TDC_VDDCR_MaxPboLimit;                                         /* Inst Float (A) */
uint32_t TDC_VDDCR_Limit;                                               /* Inst Float (A) */
uint64_t TDC_VDDCR_ValueAcc;                                            /* Acc Int    (A) */
uint32_t TDC_VDDCR_ResidencyAcc;                                        /* Acc Float */

uint32_t TDC_VDDCR_SOC_FusedLimit;                                      /* Inst Float (A) */
uint32_t TDC_VDDCR_SOC_MaxIrmLimit;                                     /* Inst Float (A) */
uint32_t TDC_VDDCR_SOC_MaxPboLimit;                                     /* Inst Float (A) */
uint32_t TDC_VDDCR_SOC_Limit;                                           /* Inst Float (A) */
uint64_t TDC_VDDCR_SOC_ValueAcc;                                        /* Acc Int    (A) */
uint32_t TDC_VDDCR_SOC_ResidencyAcc;                                    /* Acc Float */

uint32_t TDC_VDDCR_NPU_FusedLimit;                                      /* Inst Float (A) */
uint32_t TDC_VDDCR_NPU_MaxIrmLimit;                                     /* Inst Float (A) */
uint32_t TDC_VDDCR_NPU_MaxPboLimit;                                     /* Inst Float (A) */
uint32_t TDC_VDDCR_NPU_Limit;                                           /* Inst Float (A) */
uint64_t TDC_VDDCR_NPU_ValueAcc;                                        /* Acc Int    (A) */
uint32_t TDC_VDDCR_NPU_ResidencyAcc;                                    /* Acc Float */

uint32_t TDC_VDDCR_LP_FusedLimit;                                       /* Inst Float (A) */
uint32_t TDC_VDDCR_LP_MaxIrmLimit;                                      /* Inst Float (A) */
uint32_t TDC_VDDCR_LP_MaxPboLimit;                                      /* Inst Float (A) */
uint32_t TDC_VDDCR_LP_Limit;                                            /* Inst Float (A) */
uint64_t TDC_VDDCR_LP_ValueAcc;                                         /* Acc Int    (A) */
uint32_t TDC_VDDCR_LP_ResidencyAcc;                                     /* Acc Float */

uint32_t TDC_VDDCR_GFX_FusedLimit;                                      /* Inst Float (A) */
uint32_t TDC_VDDCR_GFX_MaxIrmLimit;                                     /* Inst Float (A) */
uint32_t TDC_VDDCR_GFX_MaxPboLimit;                                     /* Inst Float (A) */
uint32_t TDC_VDDCR_GFX_Limit;                                           /* Inst Float (A) */
uint64_t TDC_VDDCR_GFX_ValueAcc;                                        /* Acc Int    (A) */
uint32_t TDC_VDDCR_GFX_ResidencyAcc;                                    /* Acc Float */

uint32_t EDC_VDDCR_FusedLimit;                                          /* Inst Float (A) */
uint32_t EDC_VDDCR_MaxIrmLimit;                                         /* Inst Float (A) */
uint32_t EDC_VDDCR_MaxPboLimit;                                         /* Inst Float (A) */
uint32_t EDC_VDDCR_Limit;                                               /* Inst Float (A) */

uint32_t THM_FusedLimit;                                                /* Inst Float (C) */
uint32_t THM_Limit;                                                     /* Inst Float (C) */
uint64_t THM_ValueAcc;                                                  /* Acc Float  (C) */
uint32_t THM_ResidencyAcc;                                              /* Acc Float */
uint32_t PROCHOT_ResidencyAcc;                                          /* Acc Float */
uint64_t GFX_TempAcc;                                                   /* Acc Float  (C) */
uint64_t SOC_TempAcc;                                                   /* Acc Float  (C) */
uint32_t P3T_FusedLimit;                                                /* Inst Float (W) */
uint64_t P3T_ValueAcc;                                                  /* Acc Float  (W) */

/*POWER */
uint64_t SystemPowerAcc;                                                /* Acc Float (W) */
uint64_t ApuPowerAcc;                                                   /* Acc Float (W) */
uint64_t dGpuPowerAcc;                                                  /* Acc Float (W) */
uint64_t NpuPowerAcc;                                                   /* Acc Float (W) */

/*FREQUENCIES */
uint64_t FclkFreqEffAcc;                                                /* Acc Float (MHz) */
uint64_t MemclkFreqEffAcc;                                              /* Acc Float (MHz) */
uint64_t LclkFreqEffAcc;                                                /* Acc Float (MHz) */
uint64_t GfxclkFreqEffAcc;                                              /* Acc Float (MHz) */
uint64_t SocclkFreqEffAcc;                                              /* Acc Float (MHz) */
uint64_t VclkFreqEffAcc;                                                /* Acc Float (MHz) */
uint64_t VpeclkFreqEffAcc;                                              /* Acc Float (MHz) */
uint64_t AieclkFreqEffAcc;                                              /* Acc Float (MHz) */
uint64_t NpuhclkFreqEffAcc;                                             /* Acc Float (MHz) */
/*BANDWIDTH */
uint64_t DramReadBandwidth;                                             /* Acc Float (GB/sec) */
uint64_t DramWriteBandwidth;                                            /* Acc Float (GB/sec) */

/*ACTIVITY MONITORS */
uint64_t GfxBusyAcc;                                                    /* Acc Float (%) */
uint64_t VcnBusyAcc;                                                    /* Acc Float (%) */
uint64_t NpuBusyAcc[3];                                                 /* Acc Float (%) */

/*STT */
uint32_t STT_MinLimit;                                                  /* Inst Float (W) */
uint64_t STT_APU_HotSpotTempAcc;                                        /* Acc Float (C) */
uint64_t STT_HS2_HotSpotTempAcc;                                        /* Acc Float (C) */
uint32_t STT_APU_Temp_Limit;                                            /* Inst Float (C) */
uint64_t STT_APU_SkinTempAcc;                                           /* Acc Float (C) */

/*RESIDENCIES */
uint64_t CpuOffResidency_CCX0;                                          /* Acc Float (%) */
uint64_t CpuOffResidency_CCX1;                                          /* Acc Float (%) */
uint64_t CpuOffResidency_CCX2;                                          /* Acc Float (%) */
uint64_t CpuOffResidency_CCX3;                                          /* Acc Float (%) */

/*DFPSTATES */
uint32_t FclkFreqTable[NUM_CLK_DPM_LEVELS];                             /* Inst Int (MHz) */
uint32_t UclkFreqTable[NUM_CLK_DPM_LEVELS];                             /* Inst Int (MHz) */
uint32_t DdrRateTable[NUM_CLK_DPM_LEVELS];                              /* Inst Int (MT/s) */
uint8_t  DfPstate_Source[NUM_CLK_DPM_LEVELS];                           /* Inst Int */

/*SYSTEM */
uint8_t  GfxDisabled;                                                   /* Inst Int */
uint8_t  spare2[3];
uint32_t GfxClk_Fmax;                                                   /* Inst Float (GHz) */
uint8_t  CClk_CoreFuseEnable[METRIC_CORE_TYPE_MAX][NUM_MAX_CORES];      /* Inst Int */
uint8_t  CClk_CoreEnabled[METRIC_CORE_TYPE_MAX][NUM_MAX_CORES];         /* Inst Int */
uint32_t CClk_Fmax[METRIC_CORE_TYPE_MAX][NUM_MAX_CORES];                /* Inst Float (GHz) */

/*OVERCLOCK CAPABLE */
uint8_t CpuPreciseAndDirectOverClockingCapable;                         /* Inst Int */
uint8_t GfxPreciseAndDirectOverClockingCapable;                         /* Inst Int */
uint8_t PboBasicOverClockingCapable;                                    /* Inst Int */
uint8_t PboAdvancedOverClockingCapable;                                 /* Inst Int */
uint8_t PboNitroOverClockingCapable;                                    /* Inst Int */
uint8_t MemoryAndFabricOverClockingCapable;                             /* Inst Int */
uint8_t MiscOverClockingCapable;                                        /* Inst Int */
uint8_t ExtremeColdOverclockingCapable;                                 /* Inst Int */
uint8_t DownConfigControlCapable;                                       /* Inst Int */
uint8_t spare0[3];

/*OVERCLOCK STATUS */
uint32_t FIT_LimitScalar;                                               /* Inst Float */
uint8_t  LN2Enabled;                                                    /* Inst Int */
uint8_t  CpuPreciseAndDirectOcEnabled;                                  /* Inst Int */
uint8_t  GfxPreciseAndDirectOcEnabled;                                  /* Inst Int */
uint8_t  spare1[2];
int8_t   PsmGuardband[5][5][3];                                         /* Inst Int */
int32_t  CorePowerLimitOffset;                                          /* Inst Float (W) */
uint32_t MaxFreqOffset[5];                                              /* Inst Float (GHz) */

uint64_t NpuTempAcc;                                                    /* Acc Float (C) */
uint64_t DfPStateResidencyAcc[NUM_CLK_DPM_LEVELS];                      /* Acc Int (%) */
uint32_t CClk_Fboost;                                                   /* Inst Float (GHz) */
uint32_t spare3[5];
} MetricsTable_IOD_t;

typedef struct {
uint64_t Core_C0[NUM_MAX_CORES];                                        /* Acc Float (%) */
uint64_t Core_CC6[NUM_MAX_CORES];                                       /* Acc Float (%) */
uint64_t Core_FREQ[NUM_MAX_CORES];                                      /* Acc Float (GHz) */
uint64_t Core_FREQEFF[NUM_MAX_CORES];                                   /* Acc Float (GHz) */
uint64_t Core_TEMP[NUM_MAX_CORES];                                      /* Acc Float (C) */
uint64_t Core_POWER[NUM_MAX_CORES];                                     /* Acc Float (W) */
} MetricsTable_CCX_t;

typedef struct {
MetricsTable_IOD_t IOD;
MetricsTable_CCX_t CCX[METRIC_CCX_MAX];
} MetricsTable_t;

#pragma pack(pop)

#endif
