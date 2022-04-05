/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef SMU9_H
#define SMU9_H

#pragma pack(push, 1)

#define ENABLE_DEBUG_FEATURES

/* Feature Control Defines */
#define FEATURE_DPM_PREFETCHER_BIT      0
#define FEATURE_DPM_GFXCLK_BIT          1
#define FEATURE_DPM_UCLK_BIT            2
#define FEATURE_DPM_SOCCLK_BIT          3
#define FEATURE_DPM_UVD_BIT             4
#define FEATURE_DPM_VCE_BIT             5
#define FEATURE_ULV_BIT                 6
#define FEATURE_DPM_MP0CLK_BIT          7
#define FEATURE_DPM_LINK_BIT            8
#define FEATURE_DPM_DCEFCLK_BIT         9
#define FEATURE_AVFS_BIT                10
#define FEATURE_DS_GFXCLK_BIT           11
#define FEATURE_DS_SOCCLK_BIT           12
#define FEATURE_DS_LCLK_BIT             13
#define FEATURE_PPT_BIT                 14
#define FEATURE_TDC_BIT                 15
#define FEATURE_THERMAL_BIT             16
#define FEATURE_GFX_PER_CU_CG_BIT       17
#define FEATURE_RM_BIT                  18
#define FEATURE_DS_DCEFCLK_BIT          19
#define FEATURE_ACDC_BIT                20
#define FEATURE_VR0HOT_BIT              21
#define FEATURE_VR1HOT_BIT              22
#define FEATURE_FW_CTF_BIT              23
#define FEATURE_LED_DISPLAY_BIT         24
#define FEATURE_FAN_CONTROL_BIT         25
#define FEATURE_FAST_PPT_BIT            26
#define FEATURE_GFX_EDC_BIT             27
#define FEATURE_ACG_BIT                 28
#define FEATURE_PCC_LIMIT_CONTROL_BIT   29
#define FEATURE_SPARE_30_BIT            30
#define FEATURE_SPARE_31_BIT            31

#define NUM_FEATURES                    32

#define FFEATURE_DPM_PREFETCHER_MASK     (1 << FEATURE_DPM_PREFETCHER_BIT     )
#define FFEATURE_DPM_GFXCLK_MASK         (1 << FEATURE_DPM_GFXCLK_BIT         )
#define FFEATURE_DPM_UCLK_MASK           (1 << FEATURE_DPM_UCLK_BIT           )
#define FFEATURE_DPM_SOCCLK_MASK         (1 << FEATURE_DPM_SOCCLK_BIT         )
#define FFEATURE_DPM_UVD_MASK            (1 << FEATURE_DPM_UVD_BIT            )
#define FFEATURE_DPM_VCE_MASK            (1 << FEATURE_DPM_VCE_BIT            )
#define FFEATURE_ULV_MASK                (1 << FEATURE_ULV_BIT                )
#define FFEATURE_DPM_MP0CLK_MASK         (1 << FEATURE_DPM_MP0CLK_BIT         )
#define FFEATURE_DPM_LINK_MASK           (1 << FEATURE_DPM_LINK_BIT           )
#define FFEATURE_DPM_DCEFCLK_MASK        (1 << FEATURE_DPM_DCEFCLK_BIT        )
#define FFEATURE_AVFS_MASK               (1 << FEATURE_AVFS_BIT               )
#define FFEATURE_DS_GFXCLK_MASK          (1 << FEATURE_DS_GFXCLK_BIT          )
#define FFEATURE_DS_SOCCLK_MASK          (1 << FEATURE_DS_SOCCLK_BIT          )
#define FFEATURE_DS_LCLK_MASK            (1 << FEATURE_DS_LCLK_BIT            )
#define FFEATURE_PPT_MASK                (1 << FEATURE_PPT_BIT                )
#define FFEATURE_TDC_MASK                (1 << FEATURE_TDC_BIT                )
#define FFEATURE_THERMAL_MASK            (1 << FEATURE_THERMAL_BIT            )
#define FFEATURE_GFX_PER_CU_CG_MASK      (1 << FEATURE_GFX_PER_CU_CG_BIT      )
#define FFEATURE_RM_MASK                 (1 << FEATURE_RM_BIT                 )
#define FFEATURE_DS_DCEFCLK_MASK         (1 << FEATURE_DS_DCEFCLK_BIT         )
#define FFEATURE_ACDC_MASK               (1 << FEATURE_ACDC_BIT               )
#define FFEATURE_VR0HOT_MASK             (1 << FEATURE_VR0HOT_BIT             )
#define FFEATURE_VR1HOT_MASK             (1 << FEATURE_VR1HOT_BIT             )
#define FFEATURE_FW_CTF_MASK             (1 << FEATURE_FW_CTF_BIT             )
#define FFEATURE_LED_DISPLAY_MASK        (1 << FEATURE_LED_DISPLAY_BIT        )
#define FFEATURE_FAN_CONTROL_MASK        (1 << FEATURE_FAN_CONTROL_BIT        )

#define FEATURE_FAST_PPT_MASK            (1 << FAST_PPT_BIT                   )
#define FEATURE_GFX_EDC_MASK             (1 << FEATURE_GFX_EDC_BIT            )
#define FEATURE_ACG_MASK                 (1 << FEATURE_ACG_BIT                )
#define FEATURE_PCC_LIMIT_CONTROL_MASK   (1 << FEATURE_PCC_LIMIT_CONTROL_BIT  )
#define FFEATURE_SPARE_30_MASK           (1 << FEATURE_SPARE_30_BIT           )
#define FFEATURE_SPARE_31_MASK           (1 << FEATURE_SPARE_31_BIT           )
/* Workload types */
#define WORKLOAD_VR_BIT                 0
#define WORKLOAD_FRTC_BIT               1
#define WORKLOAD_VIDEO_BIT              2
#define WORKLOAD_COMPUTE_BIT            3
#define NUM_WORKLOADS                   4

/* ULV Client Masks */
#define ULV_CLIENT_RLC_MASK         0x00000001
#define ULV_CLIENT_UVD_MASK         0x00000002
#define ULV_CLIENT_VCE_MASK         0x00000004
#define ULV_CLIENT_SDMA0_MASK       0x00000008
#define ULV_CLIENT_SDMA1_MASK       0x00000010
#define ULV_CLIENT_JPEG_MASK        0x00000020
#define ULV_CLIENT_GFXCLK_DPM_MASK  0x00000040
#define ULV_CLIENT_UVD_DPM_MASK     0x00000080
#define ULV_CLIENT_VCE_DPM_MASK     0x00000100
#define ULV_CLIENT_MP0CLK_DPM_MASK  0x00000200
#define ULV_CLIENT_UCLK_DPM_MASK    0x00000400
#define ULV_CLIENT_SOCCLK_DPM_MASK  0x00000800
#define ULV_CLIENT_DCEFCLK_DPM_MASK 0x00001000

typedef struct {
	/* MP1_EXT_SCRATCH0 */
	uint32_t CurrLevel_GFXCLK  : 4;
	uint32_t CurrLevel_UVD     : 4;
	uint32_t CurrLevel_VCE     : 4;
	uint32_t CurrLevel_LCLK    : 4;
	uint32_t CurrLevel_MP0CLK  : 4;
	uint32_t CurrLevel_UCLK    : 4;
	uint32_t CurrLevel_SOCCLK  : 4;
	uint32_t CurrLevel_DCEFCLK : 4;
	/* MP1_EXT_SCRATCH1 */
	uint32_t TargLevel_GFXCLK  : 4;
	uint32_t TargLevel_UVD     : 4;
	uint32_t TargLevel_VCE     : 4;
	uint32_t TargLevel_LCLK    : 4;
	uint32_t TargLevel_MP0CLK  : 4;
	uint32_t TargLevel_UCLK    : 4;
	uint32_t TargLevel_SOCCLK  : 4;
	uint32_t TargLevel_DCEFCLK : 4;
	/* MP1_EXT_SCRATCH2-7 */
	uint32_t Reserved[6];
} FwStatus_t;

#pragma pack(pop)

#endif

