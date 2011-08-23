/* arch/arm/mach-rk29/include/mach/vpu_service.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * author: chenhengming chm@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK29_VPU_SERVICE_H
#define __ARCH_ARM_MACH_RK29_VPU_SERVICE_H

#include <linux/ioctl.h>    /* needed for the _IOW etc stuff used later */

/*
 * Ioctl definitions
 */

/* Use 'k' as magic number */
#define VPU_IOC_MAGIC                       'l'

#define VPU_IOC_SET_CLIENT_TYPE             _IOW(VPU_IOC_MAGIC, 1, unsigned long)
#define VPU_IOC_GET_HW_FUSE_STATUS          _IOW(VPU_IOC_MAGIC, 2, unsigned long)

#define VPU_IOC_SET_REG                     _IOW(VPU_IOC_MAGIC, 3, unsigned long)
#define VPU_IOC_GET_REG                     _IOW(VPU_IOC_MAGIC, 4, unsigned long)


// client type: decoder only£º60 registers, size 240B
#define VPU_REG_NUM_DEC                     (60)
// client type: post-process only£º41 registers, size 164B
#define VPU_REG_NUM_PP                      (41)
// client type: decoder plus post-process£º101 registers, size 404B
#define VPU_REG_NUM_DEC_PP                  (VPU_REG_NUM_DEC+VPU_REG_NUM_PP)
// client type: encoder only£º96 registers, size 384B
#define VPU_REG_NUM_ENC                     (96)

typedef enum VPU_CLIENT_TYPE {
    VPU_ENC                 = 0x0,
    VPU_DEC                 = 0x1,
    VPU_PP                  = 0x2,
    VPU_DEC_PP              = 0x3,
    VPU_TYPE_BUTT           ,

} VPU_CLIENT_TYPE;

/* Hardware decoder configuration description */

typedef struct VPUHwDecConfig {
    unsigned long   maxDecPicWidth;         /* Maximum video decoding width supported  */
    unsigned long   maxPpOutPicWidth;       /* Maximum output width of Post-Processor */
    unsigned long   h264Support;            /* HW supports h.264 */
    unsigned long   jpegSupport;            /* HW supports JPEG */
    unsigned long   mpeg4Support;           /* HW supports MPEG-4 */
    unsigned long   customMpeg4Support;     /* HW supports custom MPEG-4 features */
    unsigned long   vc1Support;             /* HW supports VC-1 Simple */
    unsigned long   mpeg2Support;           /* HW supports MPEG-2 */
    unsigned long   ppSupport;              /* HW supports post-processor */
    unsigned long   ppConfig;               /* HW post-processor functions bitmask */
    unsigned long   sorensonSparkSupport;   /* HW supports Sorenson Spark */
    unsigned long   refBufSupport;          /* HW supports reference picture buffering */
    unsigned long   vp6Support;             /* HW supports VP6 */
    unsigned long   vp7Support;             /* HW supports VP7 */
    unsigned long   vp8Support;             /* HW supports VP8 */
    unsigned long   avsSupport;             /* HW supports AVS */
    unsigned long   jpegESupport;           /* HW supports JPEG extensions */
    unsigned long   rvSupport;              /* HW supports REAL */
    unsigned long   mvcSupport;             /* HW supports H264 MVC extension */
} VPUHwDecConfig_t;

/* Hardware encoder configuration description */

typedef struct VPUHwEndConfig
{
    unsigned long   maxEncodedWidth;        /* Maximum supported width for video encoding (not JPEG) */
    unsigned long   h264Enabled;            /* HW supports H.264 */
    unsigned long   jpegEnabled;            /* HW supports JPEG */
    unsigned long   mpeg4Enabled;           /* HW supports MPEG-4 */
    unsigned long   vsEnabled;              /* HW supports video stabilization */
    unsigned long   rgbEnabled;             /* HW supports RGB input */
    unsigned long   busType;                /* HW bus type in use */
    unsigned long   busWidth;
    unsigned long   synthesisLanguage;
} VPUHwEncConfig_t;

typedef struct VPUHwCfgReq
{
    unsigned long  *cfg;
    unsigned long   size;
} VPUHwCfgReq_t;

#define DWL_MPEG2_E         31  /* 1 bit */
#define DWL_VC1_E           29  /* 2 bits */
#define DWL_JPEG_E          28  /* 1 bit */
#define DWL_MPEG4_E         26  /* 2 bits */
#define DWL_H264_E          24  /* 2 bits */
#define DWL_VP6_E           23  /* 1 bit */
#define DWL_PJPEG_E         22  /* 1 bit */
#define DWL_REF_BUFF_E      20  /* 1 bit */

#define DWL_JPEG_EXT_E          31  /* 1 bit */
#define DWL_REF_BUFF_ILACE_E    30  /* 1 bit */
#define DWL_MPEG4_CUSTOM_E      29  /* 1 bit */
#define DWL_REF_BUFF_DOUBLE_E   28  /* 1 bit */
#define DWL_RV_E            26  /* 2 bits */
#define DWL_VP7_E           24  /* 1 bit */
#define DWL_VP8_E           23  /* 1 bit */
#define DWL_AVS_E           22  /* 1 bit */
#define DWL_MVC_E           20  /* 2 bits */

#define DWL_CFG_E           24  /* 4 bits */
#define DWL_PP_E            16  /* 1 bit */

#define DWL_SORENSONSPARK_E 11  /* 1 bit */

#define DWL_H264_FUSE_E          31 /* 1 bit */
#define DWL_MPEG4_FUSE_E         30 /* 1 bit */
#define DWL_MPEG2_FUSE_E         29 /* 1 bit */
#define DWL_SORENSONSPARK_FUSE_E 28 /* 1 bit */
#define DWL_JPEG_FUSE_E          27 /* 1 bit */
#define DWL_VP6_FUSE_E           26 /* 1 bit */
#define DWL_VC1_FUSE_E           25 /* 1 bit */
#define DWL_PJPEG_FUSE_E         24 /* 1 bit */
#define DWL_CUSTOM_MPEG4_FUSE_E  23 /* 1 bit */
#define DWL_RV_FUSE_E            22 /* 1 bit */
#define DWL_VP7_FUSE_E           21 /* 1 bit */
#define DWL_VP8_FUSE_E           20 /* 1 bit */
#define DWL_AVS_FUSE_E           19 /* 1 bit */
#define DWL_MVC_FUSE_E           18 /* 1 bit */

#define DWL_DEC_MAX_1920_FUSE_E  15 /* 1 bit */
#define DWL_DEC_MAX_1280_FUSE_E  14 /* 1 bit */
#define DWL_DEC_MAX_720_FUSE_E   13 /* 1 bit */
#define DWL_DEC_MAX_352_FUSE_E   12 /* 1 bit */
#define DWL_REF_BUFF_FUSE_E       7 /* 1 bit */


#define DWL_PP_FUSE_E    31  /* 1 bit */
#define DWL_PP_DEINTERLACE_FUSE_E   30  /* 1 bit */
#define DWL_PP_ALPHA_BLEND_FUSE_E   29  /* 1 bit */
#define DWL_PP_MAX_1920_FUSE_E  15  /* 1 bit */
#define DWL_PP_MAX_1280_FUSE_E  14  /* 1 bit */
#define DWL_PP_MAX_720_FUSE_E  13  /* 1 bit */
#define DWL_PP_MAX_352_FUSE_E  12  /* 1 bit */


#define MPEG4_NOT_SUPPORTED             (u32)(0x00)
#define MPEG4_SIMPLE_PROFILE            (u32)(0x01)
#define MPEG4_ADVANCED_SIMPLE_PROFILE   (u32)(0x02)
#define MPEG4_CUSTOM_NOT_SUPPORTED      (u32)(0x00)
#define MPEG4_CUSTOM_FEATURE_1          (u32)(0x01)
#define H264_NOT_SUPPORTED              (u32)(0x00)
#define H264_BASELINE_PROFILE           (u32)(0x01)
#define H264_MAIN_PROFILE               (u32)(0x02)
#define H264_HIGH_PROFILE               (u32)(0x03)
#define VC1_NOT_SUPPORTED               (u32)(0x00)
#define VC1_SIMPLE_PROFILE              (u32)(0x01)
#define VC1_MAIN_PROFILE                (u32)(0x02)
#define VC1_ADVANCED_PROFILE            (u32)(0x03)
#define MPEG2_NOT_SUPPORTED             (u32)(0x00)
#define MPEG2_MAIN_PROFILE              (u32)(0x01)
#define JPEG_NOT_SUPPORTED              (u32)(0x00)
#define JPEG_BASELINE                   (u32)(0x01)
#define JPEG_PROGRESSIVE                (u32)(0x02)
#define PP_NOT_SUPPORTED                (u32)(0x00)
#define PP_SUPPORTED                    (u32)(0x01)
#define PP_DITHERING                    (u32)(0x10000000)
#define PP_SCALING                      (u32)(0x0C000000)
#define PP_DEINTERLACING                (u32)(0x02000000)
#define PP_ALPHA_BLENDING               (u32)(0x01000000)
#define SORENSON_SPARK_NOT_SUPPORTED    (u32)(0x00)
#define SORENSON_SPARK_SUPPORTED        (u32)(0x01)
#define VP6_NOT_SUPPORTED               (u32)(0x00)
#define VP6_SUPPORTED                   (u32)(0x01)
#define VP7_NOT_SUPPORTED               (u32)(0x00)
#define VP7_SUPPORTED                   (u32)(0x01)
#define VP8_NOT_SUPPORTED               (u32)(0x00)
#define VP8_SUPPORTED                   (u32)(0x01)
#define REF_BUF_NOT_SUPPORTED           (u32)(0x00)
#define REF_BUF_SUPPORTED               (u32)(0x01)
#define REF_BUF_INTERLACED              (u32)(0x02)
#define REF_BUF_DOUBLE                  (u32)(0x04)
#define AVS_NOT_SUPPORTED               (u32)(0x00)
#define AVS_SUPPORTED                   (u32)(0x01)
#define JPEG_EXT_NOT_SUPPORTED          (u32)(0x00)
#define JPEG_EXT_SUPPORTED              (u32)(0x01)
#define RV_NOT_SUPPORTED                (u32)(0x00)
#define RV_SUPPORTED                    (u32)(0x01)
#define MVC_NOT_SUPPORTED               (u32)(0x00)
#define MVC_SUPPORTED                   (u32)(0x01)

#define H264_NOT_SUPPORTED_FUSE             (u32)(0x00)
#define H264_FUSE_ENABLED                   (u32)(0x01)
#define MPEG4_NOT_SUPPORTED_FUSE            (u32)(0x00)
#define MPEG4_FUSE_ENABLED                  (u32)(0x01)
#define MPEG2_NOT_SUPPORTED_FUSE            (u32)(0x00)
#define MPEG2_FUSE_ENABLED                  (u32)(0x01)
#define SORENSON_SPARK_NOT_SUPPORTED_FUSE   (u32)(0x00)
#define SORENSON_SPARK_ENABLED              (u32)(0x01)
#define JPEG_NOT_SUPPORTED_FUSE             (u32)(0x00)
#define JPEG_FUSE_ENABLED                   (u32)(0x01)
#define VP6_NOT_SUPPORTED_FUSE              (u32)(0x00)
#define VP6_FUSE_ENABLED                    (u32)(0x01)
#define VP7_NOT_SUPPORTED_FUSE              (u32)(0x00)
#define VP7_FUSE_ENABLED                    (u32)(0x01)
#define VP8_NOT_SUPPORTED_FUSE              (u32)(0x00)
#define VP8_FUSE_ENABLED                    (u32)(0x01)
#define VC1_NOT_SUPPORTED_FUSE              (u32)(0x00)
#define VC1_FUSE_ENABLED                    (u32)(0x01)
#define JPEG_PROGRESSIVE_NOT_SUPPORTED_FUSE (u32)(0x00)
#define JPEG_PROGRESSIVE_FUSE_ENABLED       (u32)(0x01)
#define REF_BUF_NOT_SUPPORTED_FUSE          (u32)(0x00)
#define REF_BUF_FUSE_ENABLED                (u32)(0x01)
#define AVS_NOT_SUPPORTED_FUSE              (u32)(0x00)
#define AVS_FUSE_ENABLED                    (u32)(0x01)
#define RV_NOT_SUPPORTED_FUSE               (u32)(0x00)
#define RV_FUSE_ENABLED                     (u32)(0x01)
#define MVC_NOT_SUPPORTED_FUSE              (u32)(0x00)
#define MVC_FUSE_ENABLED                    (u32)(0x01)

#define PP_NOT_SUPPORTED_FUSE               (u32)(0x00)
#define PP_FUSE_ENABLED                     (u32)(0x01)
#define PP_FUSE_DEINTERLACING_ENABLED       (u32)(0x40000000)
#define PP_FUSE_ALPHA_BLENDING_ENABLED      (u32)(0x20000000)
#define MAX_PP_OUT_WIDHT_1920_FUSE_ENABLED  (u32)(0x00008000)
#define MAX_PP_OUT_WIDHT_1280_FUSE_ENABLED  (u32)(0x00004000)
#define MAX_PP_OUT_WIDHT_720_FUSE_ENABLED   (u32)(0x00002000)
#define MAX_PP_OUT_WIDHT_352_FUSE_ENABLED   (u32)(0x00001000)

#define VPU_DEC_HWCFG0              50
#define VPU_DEC_HWCFG1              51
#define VPU_DEC_HW_FUSE_CFG         57
#define VPU_PP_HW_SYNTH_CFG         40
#define VPU_PP_HW_FUSE_CFG          41

typedef struct VPUHwFuseStatus
{
    u32 h264SupportFuse;            /* HW supports h.264 */
    u32 mpeg4SupportFuse;           /* HW supports MPEG-4 */
    u32 mpeg2SupportFuse;           /* HW supports MPEG-2 */
    u32 sorensonSparkSupportFuse;   /* HW supports Sorenson Spark */
    u32 jpegSupportFuse;            /* HW supports JPEG */
    u32 vp6SupportFuse;             /* HW supports VP6 */
    u32 vp7SupportFuse;             /* HW supports VP6 */
    u32 vp8SupportFuse;             /* HW supports VP6 */
    u32 vc1SupportFuse;             /* HW supports VC-1 Simple */
    u32 jpegProgSupportFuse;        /* HW supports Progressive JPEG */
    u32 ppSupportFuse;              /* HW supports post-processor */
    u32 ppConfigFuse;               /* HW post-processor functions bitmask */
    u32 maxDecPicWidthFuse;         /* Maximum video decoding width supported  */
    u32 maxPpOutPicWidthFuse;       /* Maximum output width of Post-Processor */
    u32 refBufSupportFuse;          /* HW supports reference picture buffering */
    u32 avsSupportFuse;             /* one of the AVS values defined above */
    u32 rvSupportFuse;              /* one of the REAL values defined above */
    u32 mvcSupportFuse;
    u32 customMpeg4SupportFuse;     /* Fuse for custom MPEG-4 */

} VPUHwFuseStatus_t;


#endif

