/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_ENGINE_H__
#define __NVRM_ENGINE_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define MC_ENGINE_IDX_NULL                          0 // This must be 0
#define MC_ENGINE_IDX_TMR                           1
#define MC_ENGINE_IDX_DISP                          2
#define MC_ENGINE_IDX_FB                            3
#define MC_ENGINE_IDX_FIFO                          4
#define MC_ENGINE_IDX_VIDEO                         5
#define MC_ENGINE_IDX_MD                            6
#define MC_ENGINE_IDX_BUS                           7
#define MC_ENGINE_IDX_PMGR                          8
#define MC_ENGINE_IDX_VP2                           9
#define MC_ENGINE_IDX_CIPHER                        10
#define MC_ENGINE_IDX_BIF                           11
#define MC_ENGINE_IDX_PPP                           12
#define MC_ENGINE_IDX_PRIVRING                      13
#define MC_ENGINE_IDX_PMU                           14
#define MC_ENGINE_IDX_CE0                           15
#define MC_ENGINE_IDX_CE1                           16
#define MC_ENGINE_IDX_CE2                           17
#define MC_ENGINE_IDX_CE3                           18
#define MC_ENGINE_IDX_CE4                           19
#define MC_ENGINE_IDX_CE5                           20
#define MC_ENGINE_IDX_CE6                           21
#define MC_ENGINE_IDX_CE7                           22
#define MC_ENGINE_IDX_CE8                           23
#define MC_ENGINE_IDX_CE9                           24
#define MC_ENGINE_IDX_CE_MAX                        MC_ENGINE_IDX_CE9
#define MC_ENGINE_IDX_VIC                           35
#define MC_ENGINE_IDX_ISOHUB                        36
#define MC_ENGINE_IDX_VGPU                          37
#define MC_ENGINE_IDX_MSENC                         38
#define MC_ENGINE_IDX_MSENC1                        39
#define MC_ENGINE_IDX_MSENC2                        40
#define MC_ENGINE_IDX_C2C                           41
#define MC_ENGINE_IDX_LTC                           42
#define MC_ENGINE_IDX_FBHUB                         43
#define MC_ENGINE_IDX_HDACODEC                      44
#define MC_ENGINE_IDX_GMMU                          45
#define MC_ENGINE_IDX_SEC2                          46
#define MC_ENGINE_IDX_FSP                           47
#define MC_ENGINE_IDX_NVLINK                        48
#define MC_ENGINE_IDX_GSP                           49
#define MC_ENGINE_IDX_NVJPG                         50
#define MC_ENGINE_IDX_NVJPEG                        MC_ENGINE_IDX_NVJPG
#define MC_ENGINE_IDX_NVJPEG0                       MC_ENGINE_IDX_NVJPEG
#define MC_ENGINE_IDX_NVJPEG1                       51
#define MC_ENGINE_IDX_NVJPEG2                       52
#define MC_ENGINE_IDX_NVJPEG3                       53
#define MC_ENGINE_IDX_NVJPEG4                       54
#define MC_ENGINE_IDX_NVJPEG5                       55
#define MC_ENGINE_IDX_NVJPEG6                       56
#define MC_ENGINE_IDX_NVJPEG7                       57
#define MC_ENGINE_IDX_REPLAYABLE_FAULT              58
#define MC_ENGINE_IDX_ACCESS_CNTR                   59
#define MC_ENGINE_IDX_NON_REPLAYABLE_FAULT          60
#define MC_ENGINE_IDX_REPLAYABLE_FAULT_ERROR        61
#define MC_ENGINE_IDX_NON_REPLAYABLE_FAULT_ERROR    62
#define MC_ENGINE_IDX_INFO_FAULT                    63
#define MC_ENGINE_IDX_BSP                           64
#define MC_ENGINE_IDX_NVDEC                         MC_ENGINE_IDX_BSP
#define MC_ENGINE_IDX_NVDEC0                        MC_ENGINE_IDX_NVDEC
#define MC_ENGINE_IDX_NVDEC1                        65
#define MC_ENGINE_IDX_NVDEC2                        66
#define MC_ENGINE_IDX_NVDEC3                        67
#define MC_ENGINE_IDX_NVDEC4                        68
#define MC_ENGINE_IDX_NVDEC5                        69
#define MC_ENGINE_IDX_NVDEC6                        70
#define MC_ENGINE_IDX_NVDEC7                        71
#define MC_ENGINE_IDX_CPU_DOORBELL                  72
#define MC_ENGINE_IDX_PRIV_DOORBELL                 73
#define MC_ENGINE_IDX_MMU_ECC_ERROR                 74
#define MC_ENGINE_IDX_BLG                           75
#define MC_ENGINE_IDX_PERFMON                       76
#define MC_ENGINE_IDX_BUF_RESET                     77
#define MC_ENGINE_IDX_XBAR                          78
#define MC_ENGINE_IDX_ZPW                           79
#define MC_ENGINE_IDX_OFA0                          80
#define MC_ENGINE_IDX_TEGRA                         81
#define MC_ENGINE_IDX_GR                            82
#define MC_ENGINE_IDX_GR0                           MC_ENGINE_IDX_GR
#define MC_ENGINE_IDX_GR1                           83
#define MC_ENGINE_IDX_GR2                           84
#define MC_ENGINE_IDX_GR3                           85
#define MC_ENGINE_IDX_GR4                           86
#define MC_ENGINE_IDX_GR5                           87
#define MC_ENGINE_IDX_GR6                           88
#define MC_ENGINE_IDX_GR7                           89
#define MC_ENGINE_IDX_ESCHED                        90
#define MC_ENGINE_IDX_ESCHED__SIZE                  64
#define MC_ENGINE_IDX_GR_FECS_LOG                   154
#define MC_ENGINE_IDX_GR0_FECS_LOG                  MC_ENGINE_IDX_GR_FECS_LOG
#define MC_ENGINE_IDX_GR1_FECS_LOG                  155
#define MC_ENGINE_IDX_GR2_FECS_LOG                  156
#define MC_ENGINE_IDX_GR3_FECS_LOG                  157
#define MC_ENGINE_IDX_GR4_FECS_LOG                  158
#define MC_ENGINE_IDX_GR5_FECS_LOG                  159
#define MC_ENGINE_IDX_GR6_FECS_LOG                  160
#define MC_ENGINE_IDX_GR7_FECS_LOG                  161
#define MC_ENGINE_IDX_TMR_SWRL                      162
#define MC_ENGINE_IDX_DISP_GSP                      163
#define MC_ENGINE_IDX_REPLAYABLE_FAULT_CPU          164
#define MC_ENGINE_IDX_NON_REPLAYABLE_FAULT_CPU      165
#define MC_ENGINE_IDX_PXUC                          166
#define MC_ENGINE_IDX_MAX                           167 // This must be kept as the max bit if
#define MC_ENGINE_IDX_INVALID                0xFFFFFFFF
#define MC_ENGINE_IDX_GRn(x)            (MC_ENGINE_IDX_GR0 + (x))
#define MC_ENGINE_IDX_GRn_FECS_LOG(x)   (MC_ENGINE_IDX_GR0_FECS_LOG + (x))
#define MC_ENGINE_IDX_CE(x)             (MC_ENGINE_IDX_CE0 + (x))
#define MC_ENGINE_IDX_MSENCn(x)         (MC_ENGINE_IDX_MSENC + (x))
#define MC_ENGINE_IDX_NVDECn(x)         (MC_ENGINE_IDX_NVDEC + (x))
#define MC_ENGINE_IDX_NVJPEGn(x)        (MC_ENGINE_IDX_NVJPEG + (x))
#define MC_ENGINE_IDX_ESCHEDn(x)        (MC_ENGINE_IDX_ESCHED + (x))

typedef enum
{
    RM_ENGINE_TYPE_NULL                 =       (0x00000000),
    RM_ENGINE_TYPE_GR0                  =       (0x00000001),
    RM_ENGINE_TYPE_GR1                  =       (0x00000002),
    RM_ENGINE_TYPE_GR2                  =       (0x00000003),
    RM_ENGINE_TYPE_GR3                  =       (0x00000004),
    RM_ENGINE_TYPE_GR4                  =       (0x00000005),
    RM_ENGINE_TYPE_GR5                  =       (0x00000006),
    RM_ENGINE_TYPE_GR6                  =       (0x00000007),
    RM_ENGINE_TYPE_GR7                  =       (0x00000008),
    RM_ENGINE_TYPE_COPY0                =       (0x00000009),
    RM_ENGINE_TYPE_COPY1                =       (0x0000000a),
    RM_ENGINE_TYPE_COPY2                =       (0x0000000b),
    RM_ENGINE_TYPE_COPY3                =       (0x0000000c),
    RM_ENGINE_TYPE_COPY4                =       (0x0000000d),
    RM_ENGINE_TYPE_COPY5                =       (0x0000000e),
    RM_ENGINE_TYPE_COPY6                =       (0x0000000f),
    RM_ENGINE_TYPE_COPY7                =       (0x00000010),
    RM_ENGINE_TYPE_COPY8                =       (0x00000011),
    RM_ENGINE_TYPE_COPY9                =       (0x00000012),
    RM_ENGINE_TYPE_NVDEC0               =       (0x0000001d),
    RM_ENGINE_TYPE_NVDEC1               =       (0x0000001e),
    RM_ENGINE_TYPE_NVDEC2               =       (0x0000001f),
    RM_ENGINE_TYPE_NVDEC3               =       (0x00000020),
    RM_ENGINE_TYPE_NVDEC4               =       (0x00000021),
    RM_ENGINE_TYPE_NVDEC5               =       (0x00000022),
    RM_ENGINE_TYPE_NVDEC6               =       (0x00000023),
    RM_ENGINE_TYPE_NVDEC7               =       (0x00000024),
    RM_ENGINE_TYPE_NVENC0               =       (0x00000025),
    RM_ENGINE_TYPE_NVENC1               =       (0x00000026),
    RM_ENGINE_TYPE_NVENC2               =       (0x00000027),
    RM_ENGINE_TYPE_VP                   =       (0x00000028),
    RM_ENGINE_TYPE_ME                   =       (0x00000029),
    RM_ENGINE_TYPE_PPP                  =       (0x0000002a),
    RM_ENGINE_TYPE_MPEG                 =       (0x0000002b),
    RM_ENGINE_TYPE_SW                   =       (0x0000002c),
    RM_ENGINE_TYPE_TSEC                 =       (0x0000002d),
    RM_ENGINE_TYPE_VIC                  =       (0x0000002e),
    RM_ENGINE_TYPE_MP                   =       (0x0000002f),
    RM_ENGINE_TYPE_SEC2                 =       (0x00000030),
    RM_ENGINE_TYPE_HOST                 =       (0x00000031),
    RM_ENGINE_TYPE_DPU                  =       (0x00000032),
    RM_ENGINE_TYPE_PMU                  =       (0x00000033),
    RM_ENGINE_TYPE_FBFLCN               =       (0x00000034),
    RM_ENGINE_TYPE_NVJPEG0              =       (0x00000035),
    RM_ENGINE_TYPE_NVJPEG1              =       (0x00000036),
    RM_ENGINE_TYPE_NVJPEG2              =       (0x00000037),
    RM_ENGINE_TYPE_NVJPEG3              =       (0x00000038),
    RM_ENGINE_TYPE_NVJPEG4              =       (0x00000039),
    RM_ENGINE_TYPE_NVJPEG5              =       (0x0000003a),
    RM_ENGINE_TYPE_NVJPEG6              =       (0x0000003b),
    RM_ENGINE_TYPE_NVJPEG7              =       (0x0000003c),
    RM_ENGINE_TYPE_OFA                  =       (0x0000003d),
    RM_ENGINE_TYPE_LAST                 =       (0x0000003e),
} RM_ENGINE_TYPE;

#define NV2080_ENGINE_TYPE_NULL                       (0x00000000)
#define NV2080_ENGINE_TYPE_GRAPHICS                   (0x00000001)
#define NV2080_ENGINE_TYPE_GR0                        NV2080_ENGINE_TYPE_GRAPHICS
#define NV2080_ENGINE_TYPE_GR1                        (0x00000002)
#define NV2080_ENGINE_TYPE_GR2                        (0x00000003)
#define NV2080_ENGINE_TYPE_GR3                        (0x00000004)
#define NV2080_ENGINE_TYPE_GR4                        (0x00000005)
#define NV2080_ENGINE_TYPE_GR5                        (0x00000006)
#define NV2080_ENGINE_TYPE_GR6                        (0x00000007)
#define NV2080_ENGINE_TYPE_GR7                        (0x00000008)
#define NV2080_ENGINE_TYPE_COPY0                      (0x00000009)
#define NV2080_ENGINE_TYPE_COPY1                      (0x0000000a)
#define NV2080_ENGINE_TYPE_COPY2                      (0x0000000b)
#define NV2080_ENGINE_TYPE_COPY3                      (0x0000000c)
#define NV2080_ENGINE_TYPE_COPY4                      (0x0000000d)
#define NV2080_ENGINE_TYPE_COPY5                      (0x0000000e)
#define NV2080_ENGINE_TYPE_COPY6                      (0x0000000f)
#define NV2080_ENGINE_TYPE_COPY7                      (0x00000010)
#define NV2080_ENGINE_TYPE_COPY8                      (0x00000011)
#define NV2080_ENGINE_TYPE_COPY9                      (0x00000012)
#define NV2080_ENGINE_TYPE_BSP                        (0x00000013)
#define NV2080_ENGINE_TYPE_NVDEC0                     NV2080_ENGINE_TYPE_BSP
#define NV2080_ENGINE_TYPE_NVDEC1                     (0x00000014)
#define NV2080_ENGINE_TYPE_NVDEC2                     (0x00000015)
#define NV2080_ENGINE_TYPE_NVDEC3                     (0x00000016)
#define NV2080_ENGINE_TYPE_NVDEC4                     (0x00000017)
#define NV2080_ENGINE_TYPE_NVDEC5                     (0x00000018)
#define NV2080_ENGINE_TYPE_NVDEC6                     (0x00000019)
#define NV2080_ENGINE_TYPE_NVDEC7                     (0x0000001a)
#define NV2080_ENGINE_TYPE_MSENC                      (0x0000001b)
#define NV2080_ENGINE_TYPE_NVENC0                      NV2080_ENGINE_TYPE_MSENC  /* Mutually exclusive alias */
#define NV2080_ENGINE_TYPE_NVENC1                     (0x0000001c)
#define NV2080_ENGINE_TYPE_NVENC2                     (0x0000001d)
#define NV2080_ENGINE_TYPE_VP                         (0x0000001e)
#define NV2080_ENGINE_TYPE_ME                         (0x0000001f)
#define NV2080_ENGINE_TYPE_PPP                        (0x00000020)
#define NV2080_ENGINE_TYPE_MPEG                       (0x00000021)
#define NV2080_ENGINE_TYPE_SW                         (0x00000022)
#define NV2080_ENGINE_TYPE_CIPHER                     (0x00000023)
#define NV2080_ENGINE_TYPE_TSEC                       NV2080_ENGINE_TYPE_CIPHER
#define NV2080_ENGINE_TYPE_VIC                        (0x00000024)
#define NV2080_ENGINE_TYPE_MP                         (0x00000025)
#define NV2080_ENGINE_TYPE_SEC2                       (0x00000026)
#define NV2080_ENGINE_TYPE_HOST                       (0x00000027)
#define NV2080_ENGINE_TYPE_DPU                        (0x00000028)
#define NV2080_ENGINE_TYPE_PMU                        (0x00000029)
#define NV2080_ENGINE_TYPE_FBFLCN                     (0x0000002a)
#define NV2080_ENGINE_TYPE_NVJPG                      (0x0000002b)
#define NV2080_ENGINE_TYPE_NVJPEG0                     NV2080_ENGINE_TYPE_NVJPG
#define NV2080_ENGINE_TYPE_NVJPEG1                    (0x0000002c)
#define NV2080_ENGINE_TYPE_NVJPEG2                    (0x0000002d)
#define NV2080_ENGINE_TYPE_NVJPEG3                    (0x0000002e)
#define NV2080_ENGINE_TYPE_NVJPEG4                    (0x0000002f)
#define NV2080_ENGINE_TYPE_NVJPEG5                    (0x00000030)
#define NV2080_ENGINE_TYPE_NVJPEG6                    (0x00000031)
#define NV2080_ENGINE_TYPE_NVJPEG7                    (0x00000032)
#define NV2080_ENGINE_TYPE_OFA                        (0x00000033)
#define NV2080_ENGINE_TYPE_LAST                       (0x0000003e)
#define NV2080_ENGINE_TYPE_ALLENGINES                 (0xffffffff)
#define NV2080_ENGINE_TYPE_COPY_SIZE 10
#define NV2080_ENGINE_TYPE_NVENC_SIZE 3
#define NV2080_ENGINE_TYPE_NVJPEG_SIZE 8
#define NV2080_ENGINE_TYPE_NVDEC_SIZE 8
#define NV2080_ENGINE_TYPE_GR_SIZE 8
#define NV2080_ENGINE_TYPE_COPY(i)     (NV2080_ENGINE_TYPE_COPY0+(i))
#define NV2080_ENGINE_TYPE_IS_COPY(i)  (((i) >= NV2080_ENGINE_TYPE_COPY0) && ((i) <= NV2080_ENGINE_TYPE_COPY9))
#define NV2080_ENGINE_TYPE_COPY_IDX(i) ((i) - NV2080_ENGINE_TYPE_COPY0)
#define NV2080_ENGINE_TYPE_NVENC(i)    (NV2080_ENGINE_TYPE_NVENC0+(i))
#define NV2080_ENGINE_TYPE_IS_NVENC(i)  (((i) >= NV2080_ENGINE_TYPE_NVENC0) && ((i) < NV2080_ENGINE_TYPE_NVENC(NV2080_ENGINE_TYPE_NVENC_SIZE)))
#define NV2080_ENGINE_TYPE_NVENC_IDX(i) ((i) - NV2080_ENGINE_TYPE_NVENC0)
#define NV2080_ENGINE_TYPE_NVDEC(i)    (NV2080_ENGINE_TYPE_NVDEC0+(i))
#define NV2080_ENGINE_TYPE_IS_NVDEC(i)  (((i) >= NV2080_ENGINE_TYPE_NVDEC0) && ((i) < NV2080_ENGINE_TYPE_NVDEC(NV2080_ENGINE_TYPE_NVDEC_SIZE)))
#define NV2080_ENGINE_TYPE_NVDEC_IDX(i) ((i) - NV2080_ENGINE_TYPE_NVDEC0)
#define NV2080_ENGINE_TYPE_NVJPEG(i)    (NV2080_ENGINE_TYPE_NVJPEG0+(i))
#define NV2080_ENGINE_TYPE_IS_NVJPEG(i)  (((i) >= NV2080_ENGINE_TYPE_NVJPEG0) && ((i) < NV2080_ENGINE_TYPE_NVJPEG(NV2080_ENGINE_TYPE_NVJPEG_SIZE)))
#define NV2080_ENGINE_TYPE_NVJPEG_IDX(i) ((i) - NV2080_ENGINE_TYPE_NVJPEG0)
#define NV2080_ENGINE_TYPE_GR(i)       (NV2080_ENGINE_TYPE_GR0 + (i))
#define NV2080_ENGINE_TYPE_IS_GR(i)    (((i) >= NV2080_ENGINE_TYPE_GR0) && ((i) < NV2080_ENGINE_TYPE_GR(NV2080_ENGINE_TYPE_GR_SIZE)))
#define NV2080_ENGINE_TYPE_GR_IDX(i)   ((i) - NV2080_ENGINE_TYPE_GR0)
#define NV2080_ENGINE_TYPE_IS_VALID(i) (((i) > (NV2080_ENGINE_TYPE_NULL)) && ((i) < (NV2080_ENGINE_TYPE_LAST)))
#endif
