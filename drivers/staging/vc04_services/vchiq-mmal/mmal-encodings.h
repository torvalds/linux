/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BCM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders @ Collabora
 *          Dave Stevenson @ Broadcom
 *		(now dave.stevenson@raspberrypi.org)
 *          Simon Mellor @ Broadcom
 *          Luke Diamand @ Broadcom
 */
#ifndef MMAL_ENCODINGS_H
#define MMAL_ENCODINGS_H

#define MMAL_ENCODING_H264             MMAL_FOURCC('H', '2', '6', '4')
#define MMAL_ENCODING_H263             MMAL_FOURCC('H', '2', '6', '3')
#define MMAL_ENCODING_MP4V             MMAL_FOURCC('M', 'P', '4', 'V')
#define MMAL_ENCODING_MP2V             MMAL_FOURCC('M', 'P', '2', 'V')
#define MMAL_ENCODING_MP1V             MMAL_FOURCC('M', 'P', '1', 'V')
#define MMAL_ENCODING_WMV3             MMAL_FOURCC('W', 'M', 'V', '3')
#define MMAL_ENCODING_WMV2             MMAL_FOURCC('W', 'M', 'V', '2')
#define MMAL_ENCODING_WMV1             MMAL_FOURCC('W', 'M', 'V', '1')
#define MMAL_ENCODING_WVC1             MMAL_FOURCC('W', 'V', 'C', '1')
#define MMAL_ENCODING_VP8              MMAL_FOURCC('V', 'P', '8', ' ')
#define MMAL_ENCODING_VP7              MMAL_FOURCC('V', 'P', '7', ' ')
#define MMAL_ENCODING_VP6              MMAL_FOURCC('V', 'P', '6', ' ')
#define MMAL_ENCODING_THEORA           MMAL_FOURCC('T', 'H', 'E', 'O')
#define MMAL_ENCODING_SPARK            MMAL_FOURCC('S', 'P', 'R', 'K')
#define MMAL_ENCODING_MJPEG            MMAL_FOURCC('M', 'J', 'P', 'G')

#define MMAL_ENCODING_JPEG             MMAL_FOURCC('J', 'P', 'E', 'G')
#define MMAL_ENCODING_GIF              MMAL_FOURCC('G', 'I', 'F', ' ')
#define MMAL_ENCODING_PNG              MMAL_FOURCC('P', 'N', 'G', ' ')
#define MMAL_ENCODING_PPM              MMAL_FOURCC('P', 'P', 'M', ' ')
#define MMAL_ENCODING_TGA              MMAL_FOURCC('T', 'G', 'A', ' ')
#define MMAL_ENCODING_BMP              MMAL_FOURCC('B', 'M', 'P', ' ')

#define MMAL_ENCODING_I420             MMAL_FOURCC('I', '4', '2', '0')
#define MMAL_ENCODING_I420_SLICE       MMAL_FOURCC('S', '4', '2', '0')
#define MMAL_ENCODING_YV12             MMAL_FOURCC('Y', 'V', '1', '2')
#define MMAL_ENCODING_I422             MMAL_FOURCC('I', '4', '2', '2')
#define MMAL_ENCODING_I422_SLICE       MMAL_FOURCC('S', '4', '2', '2')
#define MMAL_ENCODING_YUYV             MMAL_FOURCC('Y', 'U', 'Y', 'V')
#define MMAL_ENCODING_YVYU             MMAL_FOURCC('Y', 'V', 'Y', 'U')
#define MMAL_ENCODING_UYVY             MMAL_FOURCC('U', 'Y', 'V', 'Y')
#define MMAL_ENCODING_VYUY             MMAL_FOURCC('V', 'Y', 'U', 'Y')
#define MMAL_ENCODING_NV12             MMAL_FOURCC('N', 'V', '1', '2')
#define MMAL_ENCODING_NV21             MMAL_FOURCC('N', 'V', '2', '1')
#define MMAL_ENCODING_ARGB             MMAL_FOURCC('A', 'R', 'G', 'B')
#define MMAL_ENCODING_RGBA             MMAL_FOURCC('R', 'G', 'B', 'A')
#define MMAL_ENCODING_ABGR             MMAL_FOURCC('A', 'B', 'G', 'R')
#define MMAL_ENCODING_BGRA             MMAL_FOURCC('B', 'G', 'R', 'A')
#define MMAL_ENCODING_RGB16            MMAL_FOURCC('R', 'G', 'B', '2')
#define MMAL_ENCODING_RGB24            MMAL_FOURCC('R', 'G', 'B', '3')
#define MMAL_ENCODING_RGB32            MMAL_FOURCC('R', 'G', 'B', '4')
#define MMAL_ENCODING_BGR16            MMAL_FOURCC('B', 'G', 'R', '2')
#define MMAL_ENCODING_BGR24            MMAL_FOURCC('B', 'G', 'R', '3')
#define MMAL_ENCODING_BGR32            MMAL_FOURCC('B', 'G', 'R', '4')

/** SAND Video (YUVUV128) format, native format understood by VideoCore.
 * This format is *not* opaque - if requested you will receive full frames
 * of YUV_UV video.
 */
#define MMAL_ENCODING_YUVUV128         MMAL_FOURCC('S', 'A', 'N', 'D')

/** VideoCore opaque image format, image handles are returned to
 * the host but not the actual image data.
 */
#define MMAL_ENCODING_OPAQUE           MMAL_FOURCC('O', 'P', 'Q', 'V')

/** An EGL image handle
 */
#define MMAL_ENCODING_EGL_IMAGE        MMAL_FOURCC('E', 'G', 'L', 'I')

/* }@ */

/** \name Pre-defined audio encodings */
/* @{ */
#define MMAL_ENCODING_PCM_UNSIGNED_BE  MMAL_FOURCC('P', 'C', 'M', 'U')
#define MMAL_ENCODING_PCM_UNSIGNED_LE  MMAL_FOURCC('p', 'c', 'm', 'u')
#define MMAL_ENCODING_PCM_SIGNED_BE    MMAL_FOURCC('P', 'C', 'M', 'S')
#define MMAL_ENCODING_PCM_SIGNED_LE    MMAL_FOURCC('p', 'c', 'm', 's')
#define MMAL_ENCODING_PCM_FLOAT_BE     MMAL_FOURCC('P', 'C', 'M', 'F')
#define MMAL_ENCODING_PCM_FLOAT_LE     MMAL_FOURCC('p', 'c', 'm', 'f')

/* Pre-defined H264 encoding variants */

/** ISO 14496-10 Annex B byte stream format */
#define MMAL_ENCODING_VARIANT_H264_DEFAULT   0
/** ISO 14496-15 AVC stream format */
#define MMAL_ENCODING_VARIANT_H264_AVC1      MMAL_FOURCC('A', 'V', 'C', '1')
/** Implicitly delineated NAL units without emulation prevention */
#define MMAL_ENCODING_VARIANT_H264_RAW       MMAL_FOURCC('R', 'A', 'W', ' ')

/** \defgroup MmalColorSpace List of pre-defined video color spaces
 * This defines a list of common color spaces. This list isn't exhaustive and
 * is only provided as a convenience to avoid clients having to use FourCC
 * codes directly. However components are allowed to define and use their own
 * FourCC codes.
 */
/* @{ */

/** Unknown color space */
#define MMAL_COLOR_SPACE_UNKNOWN       0
/** ITU-R BT.601-5 [SDTV] */
#define MMAL_COLOR_SPACE_ITUR_BT601    MMAL_FOURCC('Y', '6', '0', '1')
/** ITU-R BT.709-3 [HDTV] */
#define MMAL_COLOR_SPACE_ITUR_BT709    MMAL_FOURCC('Y', '7', '0', '9')
/** JPEG JFIF */
#define MMAL_COLOR_SPACE_JPEG_JFIF     MMAL_FOURCC('Y', 'J', 'F', 'I')
/** Title 47 Code of Federal Regulations (2003) 73.682 (a) (20) */
#define MMAL_COLOR_SPACE_FCC           MMAL_FOURCC('Y', 'F', 'C', 'C')
/** Society of Motion Picture and Television Engineers 240M (1999) */
#define MMAL_COLOR_SPACE_SMPTE240M     MMAL_FOURCC('Y', '2', '4', '0')
/** ITU-R BT.470-2 System M */
#define MMAL_COLOR_SPACE_BT470_2_M     MMAL_FOURCC('Y', '_', '_', 'M')
/** ITU-R BT.470-2 System BG */
#define MMAL_COLOR_SPACE_BT470_2_BG    MMAL_FOURCC('Y', '_', 'B', 'G')
/** JPEG JFIF, but with 16..255 luma */
#define MMAL_COLOR_SPACE_JFIF_Y16_255  MMAL_FOURCC('Y', 'Y', '1', '6')
/* @} MmalColorSpace List */

#endif /* MMAL_ENCODINGS_H */
