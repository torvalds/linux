/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * user interface header for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _SSBSIP_MFC_API_H_
#define _SSBSIP_MFC_API_H_

/*--------------------------------------------------------------------------------*/
/* Definition                                                                     */
/*--------------------------------------------------------------------------------*/
#define MAX_DECODER_INPUT_BUFFER_SIZE  (1024 * 3072)
#define MAX_ENCODER_OUTPUT_BUFFER_SIZE (1024 * 3072)

#define SUPPORT_1080P        1

#if SUPPORT_1080P
#define MMAP_BUFFER_SIZE_MMAP          (70*1024*1024) /* only C110 use this value. in C210, memory size is decided in menuconfig*/
#else
#define MMAP_BUFFER_SIZE_MMAP          (62*1024*1024)
#endif

#define SAMSUNG_MFC_DEV_NAME           "/dev/s3c-mfc"


/*--------------------------------------------------------------------------------*/
/* Structure and Type                                                             */
/*--------------------------------------------------------------------------------*/
typedef enum {
    H264_DEC,
    VC1_DEC,     /* VC1 advaced Profile decoding  */
    MPEG4_DEC,
    XVID_DEC,
    MPEG1_DEC,
    MPEG2_DEC,
    H263_DEC,
    VC1RCV_DEC,  /* VC1 simple/main profile decoding  */
    FIMV1_DEC,
    FIMV2_DEC,
    FIMV3_DEC,
    FIMV4_DEC,
    H264_ENC,
    MPEG4_ENC,
    H263_ENC,
    UNKNOWN_TYPE
} SSBSIP_MFC_CODEC_TYPE;

typedef enum {
    DONT_CARE = 0,
    I_FRAME = 1,
    NOT_CODED = 2
} SSBSIP_MFC_FORCE_SET_FRAME_TYPE;

typedef enum {
    NV12_LINEAR = 0,
    NV12_TILE,
    NV21_LINEAR,
} SSBSIP_MFC_INSTRM_MODE_TYPE;

typedef enum {
    FRAME = 0,
    SLICE = 1,
} SSBSIP_MFC_OUTSTRM_MODE_TYPE;

typedef enum {
	NO_CACHE = 0,
	CACHE = 1
} SSBIP_MFC_BUFFER_TYPE;

typedef enum {
    MFC_DEC_SETCONF_POST_ENABLE = 1,
    MFC_DEC_SETCONF_EXTRA_BUFFER_NUM,
    MFC_DEC_SETCONF_DISPLAY_DELAY,
    MFC_DEC_SETCONF_IS_LAST_FRAME,
    MFC_DEC_SETCONF_SLICE_ENABLE,
    MFC_DEC_SETCONF_CRC_ENABLE,
    MFC_DEC_SETCONF_FIMV1_WIDTH_HEIGHT,
    MFC_DEC_SETCONF_FRAME_TAG,
    MFC_DEC_GETCONF_CRC_DATA,
    MFC_DEC_GETCONF_BUF_WIDTH_HEIGHT,
    MFC_DEC_GETCONF_CROP_INFO,
    MFC_DEC_GETCONF_FRAME_TAG,

    /* C210 specific feature */
    MFC_DEC_SETCONF_IMMEDIATELY_DISPLAY,
    MFC_DEC_SETCONF_DPB_FLUSH,
    MFC_DEC_SETCONF_PIXEL_CACHE,
    MFC_DEC_GETCONF_WIDTH_HEIGHT,

    MFC_DEC_SETCONF_SEI_PARSE,
    MFC_DEC_GETCONF_FRAME_PACKING,
} SSBSIP_MFC_DEC_CONF;

typedef enum {
    MFC_ENC_SETCONF_FRAME_TYPE = 100,
    MFC_ENC_SETCONF_CHANGE_FRAME_RATE,
    MFC_ENC_SETCONF_CHANGE_BIT_RATE,
    MFC_ENC_SETCONF_FRAME_TAG,
    MFC_ENC_SETCONF_ALLOW_FRAME_SKIP,
    MFC_ENC_GETCONF_FRAME_TAG,

    /* C210 specific feature */
    MFC_ENC_SETCONF_VUI_INFO,
    MFC_ENC_SETCONF_I_PERIOD,
    MFC_ENC_SETCONF_HIER_P,

    MFC_ENC_SETCONF_SEI_GEN,
    MFC_ENC_SETCONF_FRAME_PACKING,
} SSBSIP_MFC_ENC_CONF;

typedef enum {
    MFC_GETOUTBUF_STATUS_NULL = 0,
    MFC_GETOUTBUF_DECODING_ONLY = 1,
    MFC_GETOUTBUF_DISPLAY_DECODING,
    MFC_GETOUTBUF_DISPLAY_ONLY,
    MFC_GETOUTBUF_DISPLAY_END,
    MFC_GETOUTBUF_CHANGE_RESOL
} SSBSIP_MFC_DEC_OUTBUF_STATUS;

typedef enum {
    MFC_FRAME_TYPE_NOT_CODED,
    MFC_FRAME_TYPE_I_FRAME,
    MFC_FRAME_TYPE_P_FRAME,
    MFC_FRAME_TYPE_B_FRAME,
    MFC_FRAME_TYPE_OTHERS
} SSBSIP_MFC_FRAME_TYPE;

typedef enum {
    MFC_RET_OK                      = 1,
    MFC_RET_FAIL                    = -1000,
    MFC_RET_OPEN_FAIL               = -1001,
    MFC_RET_CLOSE_FAIL              = -1002,

    MFC_RET_DEC_INIT_FAIL           = -2000,
    MFC_RET_DEC_EXE_TIME_OUT        = -2001,
    MFC_RET_DEC_EXE_ERR             = -2002,
    MFC_RET_DEC_GET_INBUF_FAIL      = -2003,
    MFC_RET_DEC_SET_INBUF_FAIL      = -2004,
    MFC_RET_DEC_GET_OUTBUF_FAIL     = -2005,
    MFC_RET_DEC_GET_CONF_FAIL       = -2006,
    MFC_RET_DEC_SET_CONF_FAIL       = -2007,

    MFC_RET_ENC_INIT_FAIL           = -3000,
    MFC_RET_ENC_EXE_TIME_OUT        = -3001,
    MFC_RET_ENC_EXE_ERR             = -3002,
    MFC_RET_ENC_GET_INBUF_FAIL      = -3003,
    MFC_RET_ENC_SET_INBUF_FAIL      = -3004,
    MFC_RET_ENC_GET_OUTBUF_FAIL     = -3005,
    MFC_RET_ENC_SET_OUTBUF_FAIL     = -3006,
    MFC_RET_ENC_GET_CONF_FAIL       = -3007,
    MFC_RET_ENC_SET_CONF_FAIL       = -3008,

    MFC_RET_INVALID_PARAM           = -4000
} SSBSIP_MFC_ERROR_CODE;

typedef struct {
    void *YPhyAddr;                     /* [OUT] physical address of Y */
    void *CPhyAddr;                     /* [OUT] physical address of CbCr */
    void *YVirAddr;                     /* [OUT] virtual address of Y */
    void *CVirAddr;                     /* [OUT] virtual address of CbCr */

    int img_width;                      /* [OUT] width of real image */
    int img_height;                     /* [OUT] height of real image */
    int buf_width;                      /* [OUT] width aligned to 16 */
    int buf_height;                     /* [OUT] height alighed to 16 */

    int timestamp_top;                  /* [OUT] timestamp of top filed(This is used for interlaced stream) */
    int timestamp_bottom;               /* [OUT] timestamp of bottom filed(This is used for interlaced stream) */
    int consumedByte;                   /* [OUT] the number of byte consumed during decoding */
    int res_change;                     /* [OUT] whether resolution is changed or not. 0: not change, 1: increased, 2: decreased */
    int crop_top_offset;                /* [OUT] crop information, top_offset */
    int crop_bottom_offset;             /* [OUT] crop information, bottom_offset */
    int crop_left_offset;               /* [OUT] crop information, left_offset */
    int crop_right_offset;              /* [OUT] crop information, right_offset */
    int disp_pic_frame_type;            /* [OUT] display picture frame type information */

    /* C210 UMP feature */
    unsigned int y_cookie;              /* [OUT] cookie for Y address */
    unsigned int c_cookie;              /* [OUT] cookie for CbCr address, If it is 0, Y and CbCr is in continous memory */
} SSBSIP_MFC_DEC_OUTPUT_INFO;

typedef struct {
    void *YPhyAddr;                     /* [IN/OUT] physical address of Y */
    void *CPhyAddr;                     /* [IN/OUT] physical address of CbCr */
    void *YVirAddr;                     /* [IN/OUT] virtual address of Y */
    void *CVirAddr;                     /* [IN/OUT] virtual address of CbCr */
    int YSize;                          /* [IN/OUT] input size of Y data */
    int CSize;                          /* [IN/OUT] input size of CbCr data */

    /* C210 UMP feature */
    unsigned int y_cookie;              /* [OUT] cookie for Y address */
    unsigned int c_cookie;              /* [OUT] cookie for CbCr address, If it is 0, Y and CbCr is in continous memory */
} SSBSIP_MFC_ENC_INPUT_INFO;

typedef struct {
    unsigned int dataSize;              /* [OUT] encoded data size(without header) */
    unsigned int headerSize;            /* [OUT] encoded header size */
    unsigned int frameType;             /* [OUT] frame type of encoded stream */
    void *StrmPhyAddr;                  /* [OUT] physical address of Y */
    void *StrmVirAddr;                  /* [OUT] virtual address of Y */
    void *encodedYPhyAddr;              /* [OUT] physical address of Y which is flushed */
    void *encodedCPhyAddr;              /* [OUT] physical address of C which is flushed */

    /* C210 UMP feature */
	unsigned int strm_cookie;			/* [OUT] cooke for stream buffer */
    unsigned int y_encoded_cookie;      /* [OUT] cookie for Y address */
    unsigned int c_encoded_cookie;      /* [OUT] cookie for CbCr address, If it is 0, Y and CbCr is in continous memory */
} SSBSIP_MFC_ENC_OUTPUT_INFO;

typedef struct {
    /* common parameters */
    SSBSIP_MFC_CODEC_TYPE codecType;    /* [IN] codec type */
    int SourceWidth;                    /* [IN] width of video to be encoded */
    int SourceHeight;                   /* [IN] height of video to be encoded */
    int IDRPeriod;                      /* [IN] GOP number(interval of I-frame) */
    int SliceMode;                      /* [IN] Multi slice mode */
    int RandomIntraMBRefresh;           /* [IN] cyclic intra refresh */
    int EnableFRMRateControl;           /* [IN] frame based rate control enable */
    int Bitrate;                        /* [IN] rate control parameter(bit rate) */
    int FrameQp;                        /* [IN] The quantization parameter of the frame */
    int FrameQp_P;                      /* [IN] The quantization parameter of the P frame */
    int QSCodeMax;                      /* [IN] Maximum Quantization value */
    int QSCodeMin;                      /* [IN] Minimum Quantization value */
    int CBRPeriodRf;                    /* [IN] Reaction coefficient parameter for rate control */
    int PadControlOn;                   /* [IN] Enable padding control */
    int LumaPadVal;                     /* [IN] Luma pel value used to fill padding area */
    int CbPadVal;                       /* [IN] CB pel value used to fill padding area */
    int CrPadVal;                       /* [IN] CR pel value used to fill padding area */
    int FrameMap;                       /* [IN] Encoding input mode(tile mode or linear mode) */
    int OutputMode;                     /* [IN] Output mode: Frame/Slice */

    /* H.264 specific parameters */
    int ProfileIDC;                     /* [IN] profile */
    int LevelIDC;                       /* [IN] level */
    int FrameQp_B;                      /* [IN] The quantization parameter of the B frame */
    int FrameRate;                      /* [IN] rate control parameter(frame rate) */
    int SliceArgument;                  /* [IN] MB number or byte number */
    int NumberBFrames;                  /* [IN] The number of consecutive B frame inserted */
    int NumberReferenceFrames;          /* [IN] The number of reference pictures used */
    int NumberRefForPframes;            /* [IN] The number of reference pictures used for encoding P pictures */
    int LoopFilterDisable;              /* [IN] disable the loop filter */
    int LoopFilterAlphaC0Offset;        /* [IN] Alpha & C0 offset for H.264 loop filter */
    int LoopFilterBetaOffset;           /* [IN] Beta offset for H.264 loop filter */
    int SymbolMode;                     /* [IN] The mode of entropy coding(CABAC, CAVLC) */
    int PictureInterlace;               /* [IN] Enables the interlace mode */
    int Transform8x8Mode;               /* [IN] Allow 8x8 transform(This is allowed only for high profile) */
    int EnableMBRateControl;            /* [IN] Enable macroblock-level rate control */
    int DarkDisable;                    /* [IN] Disable adaptive rate control on dark region */
    int SmoothDisable;                  /* [IN] Disable adaptive rate control on smooth region */
    int StaticDisable;                  /* [IN] Disable adaptive rate control on static region */
    int ActivityDisable;                /* [IN] Disable adaptive rate control on high activity region */
} SSBSIP_MFC_ENC_H264_PARAM;

typedef struct {
    /* common parameters */
    SSBSIP_MFC_CODEC_TYPE codecType;    /* [IN] codec type */
    int SourceWidth;                    /* [IN] width of video to be encoded */
    int SourceHeight;                   /* [IN] height of video to be encoded */
    int IDRPeriod;                      /* [IN] GOP number(interval of I-frame) */
    int SliceMode;                      /* [IN] Multi slice mode */
    int RandomIntraMBRefresh;           /* [IN] cyclic intra refresh */
    int EnableFRMRateControl;           /* [IN] frame based rate control enable */
    int Bitrate;                        /* [IN] rate control parameter(bit rate) */
    int FrameQp;                        /* [IN] The quantization parameter of the frame */
    int FrameQp_P;                      /* [IN] The quantization parameter of the P frame */
    int QSCodeMax;                      /* [IN] Maximum Quantization value */
    int QSCodeMin;                      /* [IN] Minimum Quantization value */
    int CBRPeriodRf;                    /* [IN] Reaction coefficient parameter for rate control */
    int PadControlOn;                   /* [IN] Enable padding control */
    int LumaPadVal;                     /* [IN] Luma pel value used to fill padding area */
    int CbPadVal;                       /* [IN] CB pel value used to fill padding area */
    int CrPadVal;                       /* [IN] CR pel value used to fill padding area */
    int FrameMap;                       /* [IN] Encoding input mode(tile mode or linear mode) */
    int OutputMode;                     /* [IN] Output mode: Frame/Slice */

    /* MPEG4 specific parameters */
    int ProfileIDC;                     /* [IN] profile */
    int LevelIDC;                       /* [IN] level */
    int FrameQp_B;                      /* [IN] The quantization parameter of the B frame */
    int TimeIncreamentRes;              /* [IN] frame rate */
    int VopTimeIncreament;              /* [IN] frame rate */
    int SliceArgument;                  /* [IN] MB number or byte number */
    int NumberBFrames;                  /* [IN] The number of consecutive B frame inserted */
    int DisableQpelME;                  /* [IN] disable quarter-pixel motion estimation */
} SSBSIP_MFC_ENC_MPEG4_PARAM;

typedef struct {
    /* common parameters */
    SSBSIP_MFC_CODEC_TYPE codecType;    /* [IN] codec type */
    int SourceWidth;                    /* [IN] width of video to be encoded */
    int SourceHeight;                   /* [IN] height of video to be encoded */
    int IDRPeriod;                      /* [IN] GOP number(interval of I-frame) */
    int SliceMode;                      /* [IN] Multi slice mode */
    int RandomIntraMBRefresh;           /* [IN] cyclic intra refresh */
    int EnableFRMRateControl;           /* [IN] frame based rate control enable */
    int Bitrate;                        /* [IN] rate control parameter(bit rate) */
    int FrameQp;                        /* [IN] The quantization parameter of the frame */
    int FrameQp_P;                      /* [IN] The quantization parameter of the P frame */
    int QSCodeMax;                      /* [IN] Maximum Quantization value */
    int QSCodeMin;                      /* [IN] Minimum Quantization value */
    int CBRPeriodRf;                    /* [IN] Reaction coefficient parameter for rate control */
    int PadControlOn;                   /* [IN] Enable padding control */
    int LumaPadVal;                     /* [IN] Luma pel value used to fill padding area */
    int CbPadVal;                       /* [IN] CB pel value used to fill padding area */
    int CrPadVal;                       /* [IN] CR pel value used to fill padding area */
    int FrameMap;                       /* [IN] Encoding input mode(tile mode or linear mode) */
    int OutputMode;                     /* [IN] Output mode: Frame/Slice */

    /* H.263 specific parameters */
    int FrameRate;                      /* [IN] rate control parameter(frame rate) */
} SSBSIP_MFC_ENC_H263_PARAM;

typedef struct {
    int width;
    int height;
    int buf_width;
    int buf_height;
} SSBSIP_MFC_IMG_RESOLUTION;

typedef struct {
    int crop_top_offset;
    int crop_bottom_offset;
    int crop_left_offset;
    int crop_right_offset;
} SSBSIP_MFC_CROP_INFORMATION;

typedef struct {
    int		  available;
    unsigned int  arrangement_id;
    int		  arrangement_cancel_flag;
    unsigned char arrangement_type;
    int		  quincunx_sampling_flag;
    unsigned char content_interpretation_type;
    int		  spatial_flipping_flag;
    int		  frame0_flipped_flag;
    int		  field_views_flag;
    int		  current_frame_is_frame0_flag;
    unsigned char frame0_grid_pos_x;
    unsigned char frame0_grid_pos_y;
    unsigned char frame1_grid_pos_x;
    unsigned char frame1_grid_pos_y;
} SSBSIP_MFC_FRAME_PACKING;

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------------*/
/* Format Conversion API                                                          */
/*--------------------------------------------------------------------------------*/
/* Format Conversion API */
void Y_tile_to_linear_4x2(unsigned char *p_linear_addr, unsigned char *p_tiled_addr, unsigned int x_size, unsigned int y_size);
void CbCr_tile_to_linear_4x2(unsigned char *p_linear_addr, unsigned char *p_tiled_addr, unsigned int x_size, unsigned int y_size);

/* C210 specific feature */
void tile_to_linear_64x32_4x2_neon(unsigned char *p_linear_addr, unsigned char *p_tiled_addr, unsigned int x_size, unsigned int y_size);
void tile_to_linear_64x32_4x2_uv_neon(unsigned char *p_linear_addr, unsigned char *p_tiled_addr, unsigned int x_size, unsigned int y_size);
void Convert_NV12_to_I420_NEON(unsigned char *YUV420p, unsigned char *YVU420sp,	unsigned int YSize, unsigned int UVPlaneSize);

/*--------------------------------------------------------------------------------*/
/* Decoding APIs                                                                  */
/*--------------------------------------------------------------------------------*/
void *SsbSipMfcDecOpen(void);
void *SsbSipMfcDecOpenExt(void *value);
SSBSIP_MFC_ERROR_CODE SsbSipMfcDecInit(void *openHandle, SSBSIP_MFC_CODEC_TYPE codec_type, int Frameleng);
SSBSIP_MFC_ERROR_CODE SsbSipMfcDecExe(void *openHandle, int lengthBufFill);
SSBSIP_MFC_ERROR_CODE SsbSipMfcDecClose(void *openHandle);
void  *SsbSipMfcDecGetInBuf(void *openHandle, void **phyInBuf, int inputBufferSize);


#if (defined(CONFIG_VIDEO_MFC_VCM_UMP) || defined(USE_UMP))
SSBSIP_MFC_ERROR_CODE SsbSipMfcDecSetInBuf(void *openHandle, unsigned int secure_id, int size);
#else
SSBSIP_MFC_ERROR_CODE SsbSipMfcDecSetInBuf(void *openHandle, void *phyInBuf, void *virInBuf, int size);
#endif

SSBSIP_MFC_DEC_OUTBUF_STATUS SsbSipMfcDecGetOutBuf(void *openHandle, SSBSIP_MFC_DEC_OUTPUT_INFO *output_info);

SSBSIP_MFC_ERROR_CODE SsbSipMfcDecSetConfig(void *openHandle, SSBSIP_MFC_DEC_CONF conf_type, void *value);
SSBSIP_MFC_ERROR_CODE SsbSipMfcDecGetConfig(void *openHandle, SSBSIP_MFC_DEC_CONF conf_type, void *value);

/*--------------------------------------------------------------------------------*/
/* Encoding APIs                                                                  */
/*--------------------------------------------------------------------------------*/
void *SsbSipMfcEncOpen(void);
void *SsbSipMfcEncOpenExt(void *value);
SSBSIP_MFC_ERROR_CODE SsbSipMfcEncInit(void *openHandle, void *param);
SSBSIP_MFC_ERROR_CODE SsbSipMfcEncExe(void *openHandle);
SSBSIP_MFC_ERROR_CODE SsbSipMfcEncClose(void *openHandle);

SSBSIP_MFC_ERROR_CODE SsbSipMfcEncGetInBuf(void *openHandle, SSBSIP_MFC_ENC_INPUT_INFO *input_info);
SSBSIP_MFC_ERROR_CODE SsbSipMfcEncSetInBuf(void *openHandle, SSBSIP_MFC_ENC_INPUT_INFO *input_info);

SSBSIP_MFC_ERROR_CODE SsbSipMfcEncGetOutBuf(void *openHandle, SSBSIP_MFC_ENC_OUTPUT_INFO *output_info);
SSBSIP_MFC_ERROR_CODE SsbSipMfcEncSetOutBuf (void *openHandle, void *phyOutbuf, void *virOutbuf, int outputBufferSize);

SSBSIP_MFC_ERROR_CODE SsbSipMfcEncSetConfig(void *openHandle, SSBSIP_MFC_ENC_CONF conf_type, void *value);
SSBSIP_MFC_ERROR_CODE SsbSipMfcEncGetConfig(void *openHandle, SSBSIP_MFC_ENC_CONF conf_type, void *value);

#ifdef __cplusplus
}
#endif

#endif /* _SSBSIP_MFC_API_H_ */
