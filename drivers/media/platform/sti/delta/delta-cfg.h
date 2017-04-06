/*
 * Copyright (C) STMicroelectronics SA 2015
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef DELTA_CFG_H
#define DELTA_CFG_H

#define DELTA_FW_VERSION "21.1-3"

#define DELTA_MIN_WIDTH  32
#define DELTA_MAX_WIDTH  4096
#define DELTA_MIN_HEIGHT 32
#define DELTA_MAX_HEIGHT 2400

/* DELTA requires a 32x32 pixels alignment for frames */
#define DELTA_WIDTH_ALIGNMENT    32
#define DELTA_HEIGHT_ALIGNMENT   32

#define DELTA_DEFAULT_WIDTH  DELTA_MIN_WIDTH
#define DELTA_DEFAULT_HEIGHT DELTA_MIN_HEIGHT
#define DELTA_DEFAULT_FRAMEFORMAT  V4L2_PIX_FMT_NV12
#define DELTA_DEFAULT_STREAMFORMAT V4L2_PIX_FMT_MJPEG

#define DELTA_MAX_RESO (DELTA_MAX_WIDTH * DELTA_MAX_HEIGHT)

/* guard value for number of access units */
#define DELTA_MAX_AUS 10

/* IP perf dependent, can be tuned */
#define DELTA_PEAK_FRAME_SMOOTHING 2

/*
 * guard output frame count:
 * - at least 1 frame needed for display
 * - at worst 21
 *   ( max h264 dpb (16) +
 *     decoding peak smoothing (2) +
 *     user display pipeline (3) )
 */
#define DELTA_MIN_FRAME_USER    1
#define DELTA_MAX_DPB           16
#define DELTA_MAX_FRAME_USER    3 /* platform/use-case dependent */
#define DELTA_MAX_FRAMES (DELTA_MAX_DPB + DELTA_PEAK_FRAME_SMOOTHING +\
			  DELTA_MAX_FRAME_USER)

#if DELTA_MAX_FRAMES > VIDEO_MAX_FRAME
#undef DELTA_MAX_FRAMES
#define DELTA_MAX_FRAMES (VIDEO_MAX_FRAME)
#endif

/* extra space to be allocated to store codec specific data per frame */
#define DELTA_MAX_FRAME_PRIV_SIZE 100

/* PM runtime auto power-off after 5ms of inactivity */
#define DELTA_HW_AUTOSUSPEND_DELAY_MS 5

#define DELTA_MAX_DECODERS 10
#ifdef CONFIG_VIDEO_STI_DELTA_MJPEG
extern const struct delta_dec mjpegdec;
#endif

#endif /* DELTA_CFG_H */
