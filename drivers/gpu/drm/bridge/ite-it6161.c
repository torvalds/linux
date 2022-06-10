// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#ifdef __linux__
//#include <linux/bits.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <drm/drm_mipi_dsi.h>

#include <crypto/hash.h>
#include <crypto/sha.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>

#include <sound/hdmi-codec.h>
#else

#include "platform.h"

#endif
#define IT6161_PRINT(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)

static int ite_debug = 0;

#define it6161_debug(fmt, ...) do {				\
		if (ite_debug)					\
			_DRM_PRINTK(, INFO, fmt, ##__VA_ARGS__); \
	} while (0)

/* Vendor option */
#define AUDIO_SELECT I2S
#define AUDIO_TYPE LPCM
#define AUDIO_SAMPLE_RATE SAMPLE_RATE_48K
#define AUDIO_CHANNEL_COUNT 2

/*
 * 0: Standard I2S
 * 1: 32bit I2S
 */
#define I2S_INPUT_FORMAT 1

/*
 * 0: Left-justified
 * 1: Right-justified
 */
#define I2S_JUSTIFIED 0

/*
 * 0: Data delay 1T correspond to WS
 * 1: No data delay correspond to WS
 */
#define I2S_DATA_DELAY 0

/*
 * 0: Left channel
 * 1: Right channel
 */
#define I2S_WS_CHANNEL 0

/*
 * 0: MSB shift first
 * 1: LSB shift first
 */
#define I2S_DATA_SEQUENCE 0

#define AUX_WAIT_TIMEOUT_MS 100
#define PIXEL_CLK_DELAY 1
#define PIXEL_CLK_INVERSE 0
#define ADJUST_PHASE_THRESHOLD 80000
#define MAX_PIXEL_CLK 95000
#define DEFAULT_DRV_HOLD 0
#define DEFAULT_PWR_ON 0

enum hdmi_tx_mode {
	HDMI_TX_NONE,
	HDMI_TX_BY_PASS,
	HDMI_TX_ENABLE_DE_ONLY,
	HDMI_TX_ENABLE_PATTERN_GENERATOR,
};

enum it6161_audio_select {
	I2S = 0,
	SPDIF,
    TDM,
};

enum it6161_audio_sample_rate {
	SAMPLE_RATE_24K = 0x6,
	SAMPLE_RATE_32K = 0x3,
	SAMPLE_RATE_48K = 0x2,
	SAMPLE_RATE_96K = 0xA,
	SAMPLE_RATE_192K = 0xE,
	SAMPLE_RATE_44_1K = 0x0,
	SAMPLE_RATE_88_2K = 0x8,
	SAMPLE_RATE_176_4K = 0xC,
};

enum it6161_audio_type {
	LPCM = 0,
	NLPCM,
	DSS,
};

enum it6161_audio_u32_length {
	u32_LENGTH_16BIT = 0,
	u32_LENGTH_18BIT,
	u32_LENGTH_20BIT,
	u32_LENGTH_24BIT,
};

/*
 * Audio Sample u32 Length
 * u32_LENGTH_16BIT
 * u32_LENGTH_18BIT
 * u32_LENGTH_20BIT
 * u32_LENGTH_24BIT
 */
#define AUDIO_u32_LENGTH u32_LENGTH_24BIT

enum it6161_active_level {
    LOW,
    HIGH,
};

enum dsi_data_id {
	RGB_24b = 0x3E,
	RGB_30b = 0x0D,
	RGB_36b = 0x1D,
	RGB_18b = 0x1E,
	RGB_18b_L = 0x2E,
	YCbCr_16b = 0x2C,
	YCbCr_20b = 0x0C,
	YCbCr_24b = 0x1C,
};

#include "ite_it6161_hdmi_tx.h"
#include "ite_it6161_mipi_rx.h"

// for sample clock
#define AUDFS_22p05KHz  4
#define AUDFS_44p1KHz 0
#define AUDFS_88p2KHz 8
#define AUDFS_176p4KHz    12

#define AUDFS_24KHz  6
#define AUDFS_48KHz  2
#define AUDFS_96KHz  10
#define AUDFS_192KHz 14

#define AUDFS_768KHz 9

#define AUDFS_32KHz  3
#define AUDFS_OTHER    1

////////////////////////////////////////////////////////////////////////////////
// HDMI VTable
////////////////////////////////////////////////////////////////////////////////
static HDMI_VTiming const s_VMTable[] = {
    { 1,0,640,480,800,525,25175000L,0x89,16,96,48,10,2,33,PROG,Vneg,Hneg},//640x480@60Hz
    { 2,0,720,480,858,525,27000000L,0x80,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@60Hz
    { 3,0,720,480,858,525,27000000L,0x80,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@60Hz
    { 4,0,1280,720,1650,750,74250000L,0x2E,110,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@60Hz
    { 5,0,1920,540,2200,562,74250000L,0x2E,88,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@60Hz
    { 6,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    { 7,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    { 8,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    { 9,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    {10,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    {11,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    {12,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    {13,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    {14,1,1440,480,1716,525,54000000L,0x40,32,124,120,9,6,30,PROG,Vneg,Hneg},//1440x480@60Hz
    {15,1,1440,480,1716,525,54000000L,0x40,32,124,120,9,6,30,PROG,Vneg,Hneg},//1440x480@60Hz
    {16,0,1920,1080,2200,1125,148500000L,0x17,88,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@60Hz
    {17,0,720,576,864,625,27000000L,0x80,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@50Hz
    {18,0,720,576,864,625,27000000L,0x80,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@50Hz
    {19,0,1280,720,1980,750,74250000L,0x2E,440,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@50Hz
    {20,0,1920,540,2640,562,74250000L,0x2E,528,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@50Hz
    {21,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {22,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {23,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {24,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {25,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {26,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {27,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {28,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {29,1,1440,576,1728,625,54000000L,0x40,24,128,136,5,5,39,PROG,Vpos,Hneg},//1440x576@50Hz
    {30,1,1440,576,1728,625,54000000L,0x40,24,128,136,5,5,39,PROG,Vpos,Hneg},//1440x576@50Hz
    {31,0,1920,1080,2640,1125,148500000L,0x17,528,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@50Hz
    {32,0,1920,1080,2750,1125,74250000L,0x2E,638,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@24Hz
    {33,0,1920,1080,2640,1125,74250000L,0x2E,528,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@25Hz
    {34,0,1920,1080,2200,1125,74250000L,0x2E,88,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@30Hz
    {35,2,2880,480,1716*2,525,108000000L,0x20,32*2,124*2,120*2,9,6,30,PROG,Vneg,Hneg},//2880x480@60Hz
    {36,2,2880,480,1716*2,525,108000000L,0x20,32*2,124*2,120*2,9,6,30,PROG,Vneg,Hneg},//2880x480@60Hz
    {37,1,2880,576,3456,625,108000000L,0x20,24*2,128*2,136*2,5,5,39,PROG,Vneg,Hneg},//2880x576@50Hz
    {38,2,2880,576,3456,625,108000000L,0x20,24*2,128*2,136*2,5,5,39,PROG,Vneg,Hneg},//2880x576@50Hz
    {39,0,1920,540,2304,625,72000000L,0x17,32,168,184,23,5,57,INTERLACE,Vneg,Hpos},//1920x1080@50Hz
    // 100Hz
    {40,0,1920,540,2640,562,148500000L,0x17,528,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@100Hz
    {41,0,1280,720,1980,750,148500000L,0x17,440,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@100Hz
    {42,0,720,576,864,625,   54000000L,0x40,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@100Hz
    {43,0,720,576,864,625,   54000000L,0x40,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@100Hz
    {44,1,720,288,864,312,   27000000L,0x80,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@100Hz
    {45,1,720,288,864,312,   27000000L,0x80,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@100Hz
    // 120Hz
    {46,0,1920,540,2200,562,148500000L,0x17,88,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@120Hz
    {47,0,1280,720,1650,750,148500000L,0x17,110,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@120Hz
    {48,0, 720,480, 858,525, 54000000L,0x40,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {49,0, 720,480, 858,525, 54000000L,0x40,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {50,1, 720,240, 858,262, 27000000L,0x80,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz
    {51,1, 720,240, 858,262, 27000000L,0x80,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz

    // 200Hz
    {52,0,720,576,864,625,108000000L,0x20,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@200Hz
    {53,0,720,576,864,625,108000000L,0x20,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@200Hz
    {54,1,720,288,864,312, 54000000L,0x40,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@200Hz
    {55,1,720,288,864,312, 54000000L,0x40,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@200Hz
    // 240Hz
    {56,0,720,480,858,525,108000000L,0x20,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {57,0,720,480,858,525,108000000L,0x20,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {58,1,720,240,858,262, 54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz
    {59,1,720,240,858,262, 54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz
    // 720p low resolution
    {60,0,1280, 720,3300, 750, 59400000L,0x3A,1760,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@24Hz
    {61,0,1280, 720,3960, 750, 74250000L,0x2E,2420,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@25Hz
    {62,0,1280, 720,3300, 750, 74250000L,0x2E,1760,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@30Hz
    // 1080p high refresh rate
    {63,0,1920,1080,2200,1125,297000000L,0x0B, 88,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@120Hz
    {64,0,1920,1080,2640,1125,297000000L,0x0B,528,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@100Hz
    // VESA mode
    {0,0,640,350,832,445,31500000L,0x6D,32,64,96,32,3,60,PROG,Vneg,Hpos},// 640x350@85
    {0,0,640,400,832,445,31500000L,0x6D,32,64,96,1,3,41,PROG,Vneg,Hneg},// 640x400@85
    {0,0,832,624,1152,667,57283000L,0x3C,32,64,224,1,3,39,PROG,Vneg,Hneg},// 832x624@75Hz
    {0,0,720,350,900,449,28322000L,0x7A,18,108,54,59,2,38,PROG,Vneg,Hneg},// 720x350@70Hz
    {0,0,720,400,900,449,28322000L,0x7A,18,108,54,13,2,34,PROG,Vpos,Hneg},// 720x400@70Hz
    {0,0,720,400,936,446,35500000L,0x61,36,72,108,1,3,42,PROG,Vpos,Hneg},// 720x400@85
    {0,0,640,480,800,525,25175000L,0x89,16,96,48,10,2,33,PROG,Vneg,Hneg},// 640x480@60
    {0,0,640,480,832,520,31500000L,0x6D,24,40,128,9,3,28,PROG,Vneg,Hneg},// 640x480@72
    {0,0,640,480,840,500,31500000L,0x6D,16,64,120,1,3,16,PROG,Vneg,Hneg},// 640x480@75
    {0,0,640,480,832,509,36000000L,0x60,56,56,80,1,3,25,PROG,Vneg,Hneg},// 640x480@85
    {0,0,800,600,1024,625,36000000L,0x60,24,72,128,1,2,22,PROG,Vpos,Hpos},// 800x600@56
    {0,0,800,600,1056,628,40000000L,0x56,40,128,88,1,4,23,PROG,Vpos,Hpos},// 800x600@60
    {0,0,800,600,1040,666,50000000L,0x45,56,120,64,37,6,23,PROG,Vpos,Hpos},// 800x600@72
    {0,0,800,600,1056,625,49500000L,0x45,16,80,160,1,3,21,PROG,Vpos,Hpos},// 800x600@75
    {0,0,800,600,1048,631,56250000L,0x3D,32,64,152,1,3,27,PROG,Vpos,Hpos},// 800X600@85
    {0,0,848,480,1088,517,33750000L,0x66,16,112,112,6,8,23,PROG,Vpos,Hpos},// 840X480@60
    {0,0,1024,384,1264,408,44900000L,0x4C,8,176,56,0,4,20,INTERLACE,Vpos,Hpos},//1024x768(I)@87Hz
    {0,0,1024,768,1344,806,65000000L,0x35,24,136,160,3,6,29,PROG,Vneg,Hneg},// 1024x768@60
    {0,0,1024,768,1328,806,75000000L,0x2E,24,136,144,3,6,29,PROG,Vneg,Hneg},// 1024x768@70
    {0,0,1024,768,1312,800,78750000L,0x2B,16,96,176,1,3,28,PROG,Vpos,Hpos},// 1024x768@75
    {0,0,1024,768,1376,808,94500000L,0x24,48,96,208,1,3,36,PROG,Vpos,Hpos},// 1024x768@85
    {0,0,1152,864,1600,900,108000000L,0x20,64,128,256,1,3,32,PROG,Vpos,Hpos},// 1152x864@75
    {0,0,1280,768,1440,790,68250000L,0x32,48,32,80,3,7,12,PROG,Vneg,Hpos},// 1280x768@60-R
    {0,0,1280,768,1664,798,79500000L,0x2B,64,128,192,3,7,20,PROG,Vpos,Hneg},// 1280x768@60
    {0,0,1280,768,1696,805,102250000L,0x21,80,128,208,3,7,27,PROG,Vpos,Hneg},// 1280x768@75
    {0,0,1280,768,1712,809,117500000L,0x1D,80,136,216,3,7,31,PROG,Vpos,Hneg},// 1280x768@85
    {0,0,1280,800,1440, 823, 71000000L,0x31, 48, 32, 80,3,6,14,PROG,Vpos,Hneg},// 1280x800@60Hz
    {0,0,1280,800,1680, 831, 83500000L,0x29, 72,128,200,3,6,22,PROG,Vpos,Hneg},// 1280x800@60Hz
    {0,0,1280,800,1696, 838,106500000L,0x20, 80,128,208,3,6,29,PROG,Vpos,Hneg},// 1280x800@75Hz
    {0,0,1280,800,1712, 843,122500000L,0x1C, 80,136,216,3,6,34,PROG,Vpos,Hneg},// 1280x800@85Hz
	{0,0,1280,960,1800,1000,108000000L,0x20,96,112,312,1,3,36,PROG,Vpos,Hpos},// 1280x960@60
    {0,0,1280,960,1728,1011,148500000L,0x17,64,160,224,1,3,47,PROG,Vpos,Hpos},// 1280x960@85
    {0,0,1280,1024,1688,1066,108000000L,0x20,48,112,248,1,3,38,PROG,Vpos,Hpos},// 1280x1024@60
    {0,0,1280,1024,1688,1066,135000000L,0x19,16,144,248,1,3,38,PROG,Vpos,Hpos},// 1280x1024@75
    {0,0,1280,1024,1728,1072,157500000L,0x15,64,160,224,1,3,44,PROG,Vpos,Hpos},// 1280X1024@85
    {0,0,1360,768,1792,795,85500000L,0x28,64,112,256,3,6,18,PROG,Vpos,Hpos},// 1360X768@60
    {0,0,1366,768,1792,798,85500000L,0x28, 70,143,213,3,3,24,PROG,Vpos,Hpos},// 1366X768@60
    {0,0,1366,768,1500,800,72000000L,0x30, 14, 56, 64,1,3,28,PROG,Vpos,Hpos},// 1360X768@60
    {0,0,1400,1050,1560,1080,101000000L,0x22,48,32,80,3,4,23,PROG,Vneg,Hpos},// 1400x768@60-R
    {0,0,1400,1050,1864,1089,121750000L,0x1C,88,144,232,3,4,32,PROG,Vpos,Hneg},// 1400x768@60
    {0,0,1400,1050,1896,1099,156000000L,0x16,104,144,248,3,4,42,PROG,Vpos,Hneg},// 1400x1050@75
    {0,0,1400,1050,1912,1105,179500000L,0x13,104,152,256,3,4,48,PROG,Vpos,Hneg},// 1400x1050@85
    {0,0,1440,900,1600,926,88750000L,0x26,48,32,80,3,6,17,PROG,Vneg,Hpos},// 1440x900@60-R
    {0,0,1440,900,1904,934,106500000L,0x20,80,152,232,3,6,25,PROG,Vpos,Hneg},// 1440x900@60
    {0,0,1440,900,1936,942,136750000L,0x19,96,152,248,3,6,33,PROG,Vpos,Hneg},// 1440x900@75
    {0,0,1440,900,1952,948,157000000L,0x16,104,152,256,3,6,39,PROG,Vpos,Hneg},// 1440x900@85
    {0,0,1600,1200,2160,1250,162000000L,0x15,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@60
    {0,0,1600,1200,2160,1250,175500000L,0x13,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@65
    {0,0,1600,1200,2160,1250,189000000L,0x12,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@70
    {0,0,1600,1200,2160,1250,202500000L,0x11,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@75
    {0,0,1600,1200,2160,1250,229500000L,0x0F,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@85
    {0,0,1680,1050,1840,1080,119000000L,0x1D,48,32,80,3,6,21,PROG,Vneg,Hpos},// 1680x1050@60-R
    {0,0,1680,1050,2240,1089,146250000L,0x17,104,176,280,3,6,30,PROG,Vpos,Hneg},// 1680x1050@60
    {0,0,1680,1050,2272,1099,187000000L,0x12,120,176,296,3,6,40,PROG,Vpos,Hneg},// 1680x1050@75
    {0,0,1680,1050,2288,1105,214750000L,0x10,128,176,304,3,6,46,PROG,Vpos,Hneg},// 1680x1050@85
    {0,0,1792,1344,2448,1394,204750000L,0x10,128,200,328,1,3,46,PROG,Vpos,Hneg},// 1792x1344@60
    {0,0,1792,1344,2456,1417,261000000L,0x0D,96,216,352,1,3,69,PROG,Vpos,Hneg},// 1792x1344@75
    {0,0,1856,1392,2528,1439,218250000L,0x0F,96,224,352,1,3,43,PROG,Vpos,Hneg},// 1856x1392@60
    {0,0,1856,1392,2560,1500,288000000L,0x0C,128,224,352,1,3,104,PROG,Vpos,Hneg},// 1856x1392@75
    {0,0,1920,1200,2080,1235,154000000L,0x16,48,32,80,3,6,26,PROG,Vneg,Hpos},// 1920x1200@60-R
    {0,0,1920,1200,2592,1245,193250000L,0x11,136,200,336,3,6,36,PROG,Vpos,Hneg},// 1920x1200@60
    {0,0,1920,1200,2608,1255,245250000L,0x0E,136,208,344,3,6,46,PROG,Vpos,Hneg},// 1920x1200@75
    {0,0,1920,1200,2624,1262,281250000L,0x0C,144,208,352,3,6,53,PROG,Vpos,Hneg},// 1920x1200@85
    {0,0,1920,1440,2600,1500,234000000L,0x0E,128,208,344,1,3,56,PROG,Vpos,Hneg},// 1920x1440@60
    {0,0,1920,1440,2640,1500,297000000L,0x0B,144,224,352,1,3,56,PROG,Vpos,Hneg},// 1920x1440@75
};

#define DIFF(a,b) (((a)>(b))?((a)-(b)):((b)-(a)))

static bool bChangeMode = false ;
static unsigned char CommunBuff[128] ;

static const u8 CA[] = { 0,0,0, 02, 0x3, 0x7, 0xB, 0xF, 0x1F } ;

static u32 VideoPixelClock ;
static u8 pixelrep ; // no pixelrepeating
// static HDMI_Aspec aspec ;
// static HDMI_Colorimetry Colorimetry ;

static u32 ulAudioSampleFS = INPUT_SAMPLE_FREQ_HZ;
// u8 bAudioSampleFreq = INPUT_SAMPLE_FREQ ;
static u8 bOutputAudioChannel = OUTPUT_CHANNEL;
static u8 bOutputAudioType=CNOFIG_INPUT_AUDIO_TYPE;

#define MSCOUNT 1000
#define LOADING_UPDATE_TIMEOUT (3000/32)    // 3sec

static HDMITXDEV hdmiTxDev[HDMITX_MAX_DEV_COUNT];

/* configuration */
//#define ENABLE_HDCP
#define ENABLE_MIPI_RX_EXTERNAL_CLOCK false
#define MPLaneSwap FALSE
#define MPPNSwap FALSE /* TRUE: MTK , FALSE: Solomon */
#define MIPI_RX_LANE_COUNT 4 /* 1~4 */

#define OUTPUT_COLOR_MODE F_MODE_RGB444 /* F_MODE_YUV444, F_MODE_YUV422, F_MODE_RGB444 */
#define HDMI_TX_MODE HDMI_TX_ENABLE_DE_ONLY /* HDMI_TX_NONE, HDMI_TX_BY_PASS, HDMI_TX_ENABLE_DE_ONLY, HDMI_TX_ENABLE_PATTERN_GENERATOR */
#define HDMI_TX_PATTERN_GENERATOR_FORMAT 16 /* support format 2, 4, 16*/
#define HDMI_TX_PATTERN_COLLOR_R 0x03
#define HDMI_TX_PATTERN_COLLOR_G 0x00
#define HDMI_TX_PATTERN_COLLOR_B 0x00

/* vendor option */
#define EOTPSel 0 /* LM option 0~15 */
#define EnDeSkew TRUE
#define PPIDbgSel 12/* 0~15 */
#define RegIgnrNull 1
#define RegIgnrBlk 1
#define RegEnDummyECC 0
#define LMDbgSel 0 /* 0~7 */
#define EnContCK TRUE
#define HSSetNum 3
#define EnMBPM FALSE /* enable MIPI Bypass Mode */

#if (EnMBPM == TRUE)
#define PREC_Update TRUE//int PREC_Update = FALSE;  // enable P-timing update
#define MREC_Update TRUE//int MREC_Update = FALSE;  // enable M-timing update
#define EnTBPM TRUE /* enable HDMITX Bypass Mode */
#else
#define PREC_Update FALSE//int PREC_Update = FALSE; // enable P-timing update
#define MREC_Update FALSE//int MREC_Update = FALSE; // enable M-timing update
#define EnTBPM FALSE /* enable HDMITX Bypass Mode */
#endif

#define REGSELDEF FALSE
#define MPForceStb FALSE
#define EnHReSync FALSE
#define EnVReSync FALSE
#define EnFReSync FALSE
#define EnVREnh FALSE
#define EnVREnhSel 1 /* 0:Div2, 1:Div4, 2:Div8, 3:Div16, 4:Div32 */
#define EnMAvg TRUE

#if (IC_VERSION == 0xC0)
#define SkipStg 4
#else
#define SkipStg 2
#endif

#if (IC_VERSION == 0xC0)
#define PDREFCLK FALSE
#else
#define PDREFCLK TRUE
#endif

#define PDREFCNT 0 /* when PDREFCLK=TRUE, 0:div2, 1:div4, 2:div8, 3:divg16 */
#define EnIntWakeU3 FALSE
#define EnIOIDDQ FALSE
#define EnStb2Rst FALSE
#define EnExtStdby FALSE
#define EnStandby FALSE

#if (IC_VERSION == 0xC0)
#define MShift 4//int MShift = 5; // default: 0 //fmt2 fmt4 :4
#define PPSFFRdStg 0x04//int PPSFFRdStg = 0x10; //PPSFFRdStg(2:0)
#define RegAutoSync TRUE//int RegAutoSync = TRUE;//add sync falling //pet:D0 20200211
#else
#define MShift 5//int MShift = 5; // default: 0 //fmt2 fmt4 :4
#define PPSFFRdStg 0x10//int PPSFFRdStg = 0x10; //PPSFFRdStg(2:0)
#endif //#if (IC_VERSION == 0xC0)

#define PShift 3
#define EnFFAutoRst TRUE

#define RegEnSyncErr FALSE//int RegEnSyncErr = FALSE;
#define EnTxCRC TRUE//int EnTxCRC = TRUE;
#define TxCRCnum (0x20) //D0 20200211//(0x00)//TxCRCnum(6:0)//int TxCRCnum = 0x00; //TxCRCnum(6:0)

#if (IC_VERSION == 0xC0)
#define InvMCLK TRUE //FALSE for solomon, if NonUFO, MCLK max = 140MHz with InvMCLK=TRUE
#else
#define InvMCLK FALSE //FALSE for solomon, if NonUFO, MCLK max = 140MHz with InvMCLK=TRUE
#endif

#define InvPCLK FALSE

#ifndef INV_INPUT_PCLK
#define PCLKINV 0
#else
#define PCLKINV B_TX_VDO_LATCH_EDGE
#endif

#ifndef INV_INPUT_ACLK
    #define InvAudCLK 0
#else
    #define InvAudCLK B_TX_AUDFMT_FALL_EDGE_SAMPLE_WS
#endif

// #define INIT_CLK_LOW

#define TxChSwap 0
#define TxPNSwap 0

#define NRTXRCLK 1//int NRTXRCLK = true;//it6161b0 option true:set TRCLK by self
#define RCLKFreqSel 1// int RCLKFreqSel = true; // false:  10MHz(div1),  true  : 20 MHz(OSSDIV2)
// #ifdef REDUCE_HDMITX_SRC_JITTER
	// #define ForceTxCLKStb true// int ForceTxCLKStb = false;  //true:define _hdmitx_jitter_
// #else
	#define ForceTxCLKStb true //20200220 C code set true-> false
// #endif //#ifdef REDUCE_HDMITX_SRC_JITTER
static const RegSetEntry HDMITX_Init_Table[] = {
    {0x0F, 0x40, 0x00},
	//PLL Reset
    {0x62, 0x08, 0x00}, // XP_RESETB
    {0x64, 0x04, 0x00}, // IP_RESETB
    {0x0F, 0x01, 0x00}, // bank 0 ;3
    #ifdef INIT_CLK_LOW
        {0x62, 0x90, 0x10},
        {0x64, 0x89, 0x09},
        {0x68, 0x10, 0x10},
    #endif



    // {0xD1, 0x0E, 0x0C},
    // {0x65, 0x03, 0x00},
    // #ifdef NON_SEQUENTIAL_YCBCR422 // for ITE HDMIRX
        // {0x71, 0xFC, 0x1C},
    // #else
        // {0x71, 0xFC, 0x18},
    // #endif

    {0x8D, 0xFF, CEC_I2C_SLAVE_ADDR},//EnCEC
    //{0x0F, 0x08, 0x08},
	{0xA9, 0x80, (EnTBPM<<7)},// hdmitxset(0xa9, 0xc0, (EnTBPM<<7) + (EnTxPatMux<<6))
	{0xBF, 0x80, (NRTXRCLK<<7)},//from c code hdmitxset(0xbf, 0x80, (NRTXRCLK<<7));

	// Initial Value
    {0xF8,0xFF,0xC3},
    {0xF8,0xFF,0xA5},
	//{0x05,0x1E,0x0C},//hdmitxset(0x05, 0x1E, (ForceRxOn<<4)+(RCLKPDSel<<2)+(RCLKPDEn<<1));ForceRxOn:F,RCLKPDSel=3,RCLKPDSel=false
	{0xF4,0x0C,0x00},//hdmitxset(0xF4, 0x0C, DDCSpeed<<2);//DDC75K
	{0xF3,0x02,0x00},//hdmitxset(0xF3, 0x02, ForceVOut<<1);//ForceVOut:false
    // {0x20, 0x80, 0x80},//TODO: check need or not?
    // {0x37, 0x01, 0x00},//TODO: check need or not?
    // {0x20, 0x80, 0x00},//TODO: check need or not?
    {0xF8,0xFF,0xFF},
	{0x5A,0x0C,0x0C},//hdmitxset(0x5A, 0x0C, 0x0C);
	{0xD1,0x0A,((ForceTxCLKStb)<<3)+0x02},//hdmitxset(0xD1, 0x0A, (ForceTxCLKStb<<3)+0x02);   // High Sensitivity , modified by junjie force "CLK_stable"
	{0x5D,0x04,((RCLKFreqSel)<<2)},//hdmitxset(0x5D, 0x04, (RCLKFreqSel<<2));//int RCLKFreqSel = true; // false:  10MHz(div1),  true  : 20 MHz(OSSDIV2)
	{0x65,0x03,0x00},//hdmitxset(0x65, 0x03, RINGOSC);
	{0x71,0xF9,((0<<6)+(0<<5)+(1<<4)+(1<<3)+0)},//hdmitxset(0x71, 0xF9, (XPStableTime<<6)+(EnXPLockChk<<5)+(EnPLLBufRst<<4)+(EnFFAutoRst<<3)+ EnFFManualRst);
	{0xCF,0xFF,(0<<7)+(0<<6)+(0<<4)+(0<<2)+0},//hdmitxset(0xCF, 0xFF, (EnPktLimitGB<<7)+(EnPktBlankGB<<6)+(KeepOutGBSel<<4)+(PktLimitGBSel<<2)+PktBlankGBSel);

	{0xd1,0x02,0x00},//hdmitxset(0xd1, 0x02, 0x00);//VidStbSen = false
    // 2014/01/07 HW Request for ROSC stable

    // {0x5D,0x03,0x01},
    //~2014/01/07
// #ifdef USE_IT66120
    // {0x5A, 0x02, 0x00},
    // {0xE2, 0xFF, 0xFF},
// #endif

    {0x59, 0xD0, (((2-1)<<6)+(0<<4))},//hdmitxset(0x59, 0xD0, ((ManuallPR-1)<<6)+(DisLockPR<<4));ManuallPR=2, DisLockPR = 0
	// {0x59, 0xD8, 0x40|PCLKINV},
	#if((TxChSwap == 1) || (TxPNSwap == 1))
	{0x6b,0xC0,((TxChSwap<<7)+ (TxPNSwap<<6))},// hdmitxset(0x6b, 0xC0,(TxChSwap<<7)+ (TxPNSwap<<6));
	{0x61,0x40,0x40},// hdmitxset(0x61, 0x40,0x40);
	#endif //#if((TxChSwap ==true) || (TxPNSwap ==true))


	{0xE1, 0x20, InvAudCLK},// Inverse Audio Latch Edge of IACLK
	{0xF5, 0x40, 0x00},//hdmitxset(0xF5,0x40,ForceTMDSStable<<6);

    {0x05, 0xC0, 0x40},// Setup INT Pin: Active Low & Open-Drain

    // {REG_TX_INT_MASK1, 0xFF, ~(B_TX_RXSEN_MASK|B_TX_HPD_MASK)},
    // {REG_TX_INT_MASK2, 0xFF, ~(B_TX_KSVLISTCHK_MASK|B_TX_AUTH_DONE_MASK|B_TX_AUTH_FAIL_MASK)},
    // {REG_TX_INT_MASK3, 0xFF, ~(B_TX_VIDSTABLE_MASK)},
    {0x0C, 0xFF, 0xFF},
    {0x0D, 0xFF, 0xFF},
    {0x0E, 0x03, 0x03},// Clear all Interrupt

    {0x0C, 0xFF, 0x00},
    {0x0D, 0xFF, 0x00},
    {0x0E, 0x02, 0x00},
    //{0x09, 0x03, 0x00}, // Enable HPD and RxSen Interrupt//remove for interrupt mode allen
    {0x20,0x01,0x00}
};

static const RegSetEntry HDMITX_DefaultVideo_Table[] = {

    ////////////////////////////////////////////////////
    // Config default output format.
    ////////////////////////////////////////////////////
    {0x72, 0xff, 0x00},
    {0x70, 0xff, 0x00},
#ifndef DEFAULT_INPUT_YCBCR
// GenCSC\RGB2YUV_ITU709_16_235.c
    {0x72, 0xFF, 0x02},
    {0x73, 0xFF, 0x00},
    {0x74, 0xFF, 0x80},
    {0x75, 0xFF, 0x00},
    {0x76, 0xFF, 0xB8},
    {0x77, 0xFF, 0x05},
    {0x78, 0xFF, 0xB4},
    {0x79, 0xFF, 0x01},
    {0x7A, 0xFF, 0x93},
    {0x7B, 0xFF, 0x00},
    {0x7C, 0xFF, 0x49},
    {0x7D, 0xFF, 0x3C},
    {0x7E, 0xFF, 0x18},
    {0x7F, 0xFF, 0x04},
    {0x80, 0xFF, 0x9F},
    {0x81, 0xFF, 0x3F},
    {0x82, 0xFF, 0xD9},
    {0x83, 0xFF, 0x3C},
    {0x84, 0xFF, 0x10},
    {0x85, 0xFF, 0x3F},
    {0x86, 0xFF, 0x18},
    {0x87, 0xFF, 0x04},
#else
// GenCSC\YUV2RGB_ITU709_16_235.c
    {0x0F, 0x01, 0x00},
    {0x72, 0xFF, 0x03},
    {0x73, 0xFF, 0x00},
    {0x74, 0xFF, 0x80},
    {0x75, 0xFF, 0x00},
    {0x76, 0xFF, 0x00},
    {0x77, 0xFF, 0x08},
    {0x78, 0xFF, 0x53},
    {0x79, 0xFF, 0x3C},
    {0x7A, 0xFF, 0x89},
    {0x7B, 0xFF, 0x3E},
    {0x7C, 0xFF, 0x00},
    {0x7D, 0xFF, 0x08},
    {0x7E, 0xFF, 0x51},
    {0x7F, 0xFF, 0x0C},
    {0x80, 0xFF, 0x00},
    {0x81, 0xFF, 0x00},
    {0x82, 0xFF, 0x00},
    {0x83, 0xFF, 0x08},
    {0x84, 0xFF, 0x00},
    {0x85, 0xFF, 0x00},
    {0x86, 0xFF, 0x87},
    {0x87, 0xFF, 0x0E},
#endif
    // 2012/12/20 added by Keming's suggestion test
    {0x88, 0xF0, 0x00},
};
static const RegSetEntry HDMITX_SetHDMI_Table[] = {

    ////////////////////////////////////////////////////
    // Config default HDMI Mode
    ////////////////////////////////////////////////////
    {0xC0, 0x01, 0x01},
    {0xC1, 0x03, 0x03},
    {0xC6, 0x03, 0x03}
};

static const RegSetEntry HDMITX_SetDVI_Table[] = {

    ////////////////////////////////////////////////////
    // Config default HDMI Mode
    ////////////////////////////////////////////////////
    {0x0F, 0x01, 0x01},
    {0x58, 0xFF, 0x00},
    {0x0F, 0x01, 0x00},
    {0xC0, 0x01, 0x00},
    {0xC1, 0x03, 0x02},
    {0xC6, 0x03, 0x00}
};

static const RegSetEntry HDMITX_DefaultAVIInfo_Table[] = {

    ////////////////////////////////////////////////////
    // Config default avi infoframe
    ////////////////////////////////////////////////////
    {0x0F, 0x01, 0x01},
    {0x58, 0xFF, 0x10},
    {0x59, 0xFF, 0x08},
    {0x5A, 0xFF, 0x00},
    {0x5B, 0xFF, 0x00},
    {0x5C, 0xFF, 0x00},
    {0x5D, 0xFF, 0x57},
    {0x5E, 0xFF, 0x00},
    {0x5F, 0xFF, 0x00},
    {0x60, 0xFF, 0x00},
    {0x61, 0xFF, 0x00},
    {0x62, 0xFF, 0x00},
    {0x63, 0xFF, 0x00},
    {0x64, 0xFF, 0x00},
    {0x65, 0xFF, 0x00},
    {0x0F, 0x01, 0x00},
    {0xCD, 0x03, 0x03}
};
static const RegSetEntry HDMITX_DeaultAudioInfo_Table[] = {

    ////////////////////////////////////////////////////
    // Config default audio infoframe
    ////////////////////////////////////////////////////
    {0x0F, 0x01, 0x01},
    {0x68, 0xFF, 0x00},
    {0x69, 0xFF, 0x00},
    {0x6A, 0xFF, 0x00},
    {0x6B, 0xFF, 0x00},
    {0x6C, 0xFF, 0x00},
    {0x6D, 0xFF, 0x71},
    {0x0F, 0x01, 0x00},
    {0xCE, 0x03, 0x03}
};

static const RegSetEntry HDMITX_Aud_CHStatus_LPCM_20bit_48Khz[] =
{
    {0x0F, 0x01, 0x01},
    {0x33, 0xFF, 0x00},
    {0x34, 0xFF, 0x18},
    {0x35, 0xFF, 0x00},
    {0x91, 0xFF, 0x00},
    {0x92, 0xFF, 0x00},
    {0x93, 0xFF, 0x01},
    {0x94, 0xFF, 0x00},
    {0x98, 0xFF, 0x02},
    {0x99, 0xFF, 0xDA},
    {0x0F, 0x01, 0x00}
} ;

static const RegSetEntry HDMITX_AUD_SPDIF_2ch_24bit[] =
{
    {0x0F, 0x11, 0x00},
    {0x04, 0x14, 0x04},
    {0xE0, 0xFF, 0xD1},
    {0xE1, 0xFF, 0x01},
    {0xE2, 0xFF, 0xE4},
    {0xE3, 0xFF, 0x10},
    {0xE4, 0xFF, 0x00},
    {0xE5, 0xFF, 0x00},
    {0x04, 0x14, 0x00}
} ;

static const RegSetEntry HDMITX_AUD_I2S_2ch_24bit[] =
{
    {0x0F, 0x11, 0x00},
    {0x04, 0x14, 0x04},
    {0xE0, 0xFF, 0xC1},
    {0xE1, 0xFF, 0x01},
#ifdef USE_IT66120
    {0x5A, 0x02, 0x00},
    {0xE2, 0xFF, 0xFF},
#else
    {0xE2, 0xFF, 0xE4},
#endif
    {0xE3, 0xFF, 0x00},
    {0xE4, 0xFF, 0x00},
    {0xE5, 0xFF, 0x00},
    {0x04, 0x14, 0x00}
} ;

static const RegSetEntry HDMITX_DefaultAudio_Table[] = {

    ////////////////////////////////////////////////////
    // Config default audio output format.
    ////////////////////////////////////////////////////
    {0x0F, 0x21, 0x00},
    {0x04, 0x14, 0x04},
    {0xE0, 0xFF, 0xC1},
    {0xE1, 0xFF, 0x01},
#ifdef USE_IT66120
    {0xE2, 0xFF, 0xFF},
#else
    {0xE2, 0xFF, 0xE4},
#endif
    {0xE3, 0xFF, 0x00},
    {0xE4, 0xFF, 0x00},
    {0xE5, 0xFF, 0x00},
    {0x0F, 0x01, 0x01},
    {0x33, 0xFF, 0x00},
    {0x34, 0xFF, 0x18},
    {0x35, 0xFF, 0x00},
    {0x91, 0xFF, 0x00},
    {0x92, 0xFF, 0x00},
    {0x93, 0xFF, 0x01},
    {0x94, 0xFF, 0x00},
    {0x98, 0xFF, 0x02},
    {0x99, 0xFF, 0xDB},
    {0x0F, 0x01, 0x00},
    {0x04, 0x14, 0x00}
} ;

static const RegSetEntry HDMITX_PwrDown_Table[] = {
	{0x05, 0x60, 0x60},
	 {0xf8, 0xc3},
     {0xf8, 0xa5},
     {0xe8, 0x60},
	 {0xE0, 0x0F, 0x00},
	 // Enable GRCLK
	 // #if (IC_VERSION == 0xC0)
     // {0x0F, 0x40, 0x00},
	 // #else
	 // {0x0F, 0x70, 0x70},// PwrDown RCLK , IACLK ,TXCLK
	 // #endif //#if (IC_VERSION == 0xC0)
     // PLL Reset
     {0x61, 0x10, 0x10},   // DRV_RST
     {0x62, 0x08, 0x00},   // XP_RESETB
     {0x64, 0x04, 0x00},   // IP_RESETB
     {0x01, 0x00, 0x00}, // idle(100);

	 {0x61, 0x60, 0x60},

	 {0x70, 0xFF, 0x00},//hdmitxwr(0x70, 0x00);      // Select TXCLK power-down path
     // PLL PwrDn
     // {0x61, 0x20, 0x20},   // PwrDn DRV
     // {0x62, 0x44, 0x44},   // PwrDn XPLL
     // {0x64, 0x40, 0x40},   // PwrDn IPLL

     // HDMITX PwrDn
     // {0x05, 0x01, 0x01},   // PwrDn PCLK
     // {0xE0, 0x0F, 0x00},// hdmitxset(0xE0, 0x0F, 0x00);   // PwrDn GIACLK, IACLK
     // {0x72, 0x03, 0x00},// hdmitxset(0x72, 0x03, 0x00);   // PwrDn GTxCLK (QCLK)
     // {0x0F, 0x78, 0x78},   // PwrDn GRCLK
	 {0x0F, 0x70, 0x70}//Gate RCLK IACLK TXCLK
};

static const RegSetEntry HDMITX_PwrOn_Table[] = {
    {0x0F, 0x70, 0x00},   // // PwrOn RCLK , IACLK ,TXCLK
	// {0x0F, 0x78, 0x38},   // PwrOn GRCLK
    // {0x05, 0x01, 0x00},   // PwrOn PCLK

    // PLL PwrOn
    {0x61, 0x20, 0x00},   // PwrOn DRV
    {0x62, 0x44, 0x00},   // PwrOn XPLL
    {0x64, 0x40, 0x00},   // PwrOn IPLL

    // PLL Reset OFF
    {0x61, 0x10, 0x00},   // DRV_RST
    {0x62, 0x08, 0x08},   // XP_RESETB
    {0x64, 0x04, 0x04}   // IP_RESETB
    // {0x0F, 0x78, 0x08},   // PwrOn IACLK
};

static const RegSetEntry hdmi_tx_pg_1080p60_table[] = {
// 1920x1080@60.00Hz VIC = 16
	{0x0F, 0xFF, 0x00},
	{0x90, 0xFF, 0x76},
	{0x91, 0xFF, 0x89},
	{0x92, 0xFF, 0xC0},
	{0x93, 0xFF, 0x40},
	{0x94, 0xFF, 0x80},
	{0x95, 0xFF, 0x00},
	{0x96, 0xFF, 0x2C},
	{0x97, 0xFF, 0x00},
	{0x98, 0xFF, 0x64},
	{0x99, 0xFF, 0x04},
	{0x9A, 0xFF, 0x28},
	{0x9B, 0xFF, 0x60},
	{0x9C, 0xFF, 0x40},
	{0x9D, 0xFF, 0xFF},
	{0x9E, 0xFF, 0xFF},
	{0x9F, 0xFF, 0xFF},
	{0xA0, 0xFF, 0x00},
	{0xA1, 0xFF, 0x50},
	{0xA2, 0xFF, 0xFF},
	{0xA3, 0xFF, 0xFF},
	{0xA4, 0xFF, 0x4C},
	{0xA5, 0xFF, 0x04},
	{0xA6, 0xFF, 0xF0},
	{0xB1, 0xFF, 0x00},
	{0xB2, 0xFF, 0x00},
	{0xA9, 0xFF, 0x70},
	{0xAA, 0xFF, 0x00},
	{0xAB, 0xFF, 0x00},
	{0xAC, 0xFF, 0x00},
	{0xAF, 0xFF, 0x00},
	{0xB0, 0xFF, 0x00}
};

static const RegSetEntry hdmi_tx_pg_720p60_table[] = {
// PatGen\fmt4_PatGen.c
// 1280x720@60.00Hz VIC = 4
    {0x0F, 0x01, 0x00},
    {0x90, 0xFF, 0x16},
    {0x91, 0xFF, 0x67},
    {0x92, 0xFF, 0x04},
    {0x93, 0xFF, 0x04},
    {0x94, 0xFF, 0x61},
    {0x95, 0xFF, 0x00},
    {0x96, 0xFF, 0x28},
    {0x97, 0xFF, 0x00},
    {0x98, 0xFF, 0xED},
    {0x99, 0xFF, 0x02},
    {0x9A, 0xFF, 0x18},
    {0x9B, 0xFF, 0xE8},
    {0x9C, 0xFF, 0x20},
    {0x9D, 0xFF, 0xFF},
    {0x9E, 0xFF, 0xFF},
    {0x9F, 0xFF, 0xFF},
    {0xA0, 0xFF, 0x00},
    {0xA1, 0xFF, 0x50},
    {0xA2, 0xFF, 0xFF},
    {0xA3, 0xFF, 0xFF},
    {0xA4, 0xFF, 0x39},
    {0xA5, 0xFF, 0x03},
    {0xA6, 0xFF, 0xF0},
    {0xB1, 0xFF, 0x00},
    {0xB2, 0xFF, 0x00},
	{0xA9, 0xFF, 0x70},
	{0xAA, 0xFF, 0x00},
	{0xAB, 0xFF, 0x00},
	{0xAC, 0xFF, 0x00},
	{0xAF, 0xFF, 0x00},
	{0xB0, 0xFF, 0x00}
};
static const RegSetEntry hdmi_tx_pg_480p60_table[] = {
// PatGen\fmt2_PatGen.c
// 720x480@59.94Hz VIC = 2
    {0x0F, 0x01, 0x00},
    {0x90, 0xFF, 0x90},
    {0x91, 0xFF, 0x35},
    {0x92, 0xFF, 0x7A},
    {0x93, 0xFF, 0x4A},
    {0x94, 0xFF, 0x30},
    {0x95, 0xFF, 0x00},
    {0x96, 0xFF, 0x3E},
    {0x97, 0xFF, 0x00},
    {0x98, 0xFF, 0x0C},
    {0x99, 0xFF, 0x02},
    {0x9A, 0xFF, 0x23},
    {0x9B, 0xFF, 0x03},
    {0x9C, 0xFF, 0x20},
    {0x9D, 0xFF, 0x30},
    {0x9E, 0xFF, 0x10},
    {0x9F, 0xFF, 0x42},
    {0xA0, 0xFF, 0x00},
    {0xA1, 0xFF, 0x60},
    {0xA2, 0xFF, 0x0D},
    {0xA3, 0xFF, 0x32},
    {0xA4, 0xFF, 0xAC},
    {0xA5, 0xFF, 0x01},
    {0xA6, 0xFF, 0x10},
    {0xB1, 0xFF, 0x00},
    {0xB2, 0xFF, 0x00},
    // {0xA9, 0xFF, 0x7F},
	{0xA9, 0xFF, 0x70},
	{0xAA, 0xFF, 0x00},
	{0xAB, 0xFF, 0x00},
	{0xAC, 0xFF, 0x00},
	{0xAF, 0xFF, 0x00},
	{0xB0, 0xFF, 0x00}
};

#ifdef DETECT_VSYNC_CHG_IN_SAV
static bool EnSavVSync = false ;
#endif

/* Period of hdcp checks (to ensure we're still authenticated) */
#define DRM_HDCP_CHECK_PERIOD_MS		(128 * 16)
#define DRM_HDCP2_CHECK_PERIOD_MS		500

/* Shared lengths/masks between HDMI/DVI/DisplayPort */
#define DRM_HDCP_AN_LEN				8
#define DRM_HDCP_BSTATUS_LEN			2
#define DRM_HDCP_KSV_LEN			5
#define DRM_HDCP_RI_LEN				2
#define DRM_HDCP_V_PRIME_PART_LEN		4
#define DRM_HDCP_V_PRIME_NUM_PARTS		5
#define DRM_HDCP_NUM_DOWNSTREAM(x)		(x & 0x7f)
#define DRM_HDCP_MAX_CASCADE_EXCEEDED(x)	(x & BIT(3))
#define DRM_HDCP_MAX_DEVICE_EXCEEDED(x)		(x & BIT(7))

/* Slave address for the HDCP registers in the receiver */
#define DRM_HDCP_DDC_ADDR			0x3A

/* Value to use at the end of the SHA-1 bytestream used for repeaters */
#define DRM_HDCP_SHA1_TERMINATOR		0x80

/* HDCP register offsets for HDMI/DVI devices */
#define DRM_HDCP_DDC_BKSV			0x00
#define DRM_HDCP_DDC_RI_PRIME			0x08
#define DRM_HDCP_DDC_AKSV			0x10
#define DRM_HDCP_DDC_AN				0x18
#define DRM_HDCP_DDC_V_PRIME(h)			(0x20 + h * 4)
#define DRM_HDCP_DDC_BCAPS			0x40
#define DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT	BIT(6)
#define DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY	BIT(5)
#define DRM_HDCP_DDC_BSTATUS			0x41
#define DRM_HDCP_DDC_KSV_FIFO			0x43

#define MAX_HDCP_DOWN_STREAM_COUNT 10
#define HDCP_SHA1_FIFO_LEN (MAX_HDCP_DOWN_STREAM_COUNT * 5 + 10)
#define HDMI_TX_HDCP_RETRY 3
#define HDMI_TX_PCLK_DIV2 false

#ifndef __linux__

extern const struct drm_display_mode edid_cea_modes[];

#endif

struct it6161 {
	struct device *dev;
	struct drm_bridge bridge;
	struct i2c_client *i2c_mipi_rx;
	struct i2c_client *i2c_hdmi_tx;
	struct i2c_client *i2c_cec;
	struct edid *edid;
	struct drm_connector connector;
	struct mutex mode_lock;
	struct regmap *regmap_mipi_rx;
	struct regmap *regmap_hdmi_tx;
	struct regmap *regmap_cec;
	u32 it6161_addr_hdmi_tx;
	u32 it6161_addr_cec;
	struct device_node *host_node;
	struct mipi_dsi_device *dsi;
	struct completion wait_hdcp_event;
	struct completion wait_edid_complete;
	struct delayed_work hdcp_work;
	struct work_struct wait_hdcp_ksv_list;
	u8 hdmi_tx_hdcp_retry;
	/* kHz */
	u32 hdmi_tx_rclk;
	/* kHz */
	u32 hdmi_tx_pclk;
	/* kHz */
	u32 mipi_rx_mclk;
	/* kHz */
	u32 mipi_rx_rclk;
	/* kHz */
	u32 mipi_rx_pclk;
	struct drm_display_mode mipi_rx_p_display_mode;
	struct drm_display_mode hdmi_tx_display_mode;
	struct drm_display_mode source_display_mode;
	struct hdmi_avi_infoframe source_avi_infoframe;
	u32 vic;
    u8 mipi_rx_lane_count;
	bool is_repeater;
	u8 hdcp_downstream_count;
	u8 bksv[DRM_HDCP_KSV_LEN];
	u8 sha1_transform_input[HDCP_SHA1_FIFO_LEN];
	u16 bstatus;
	bool enable_drv_hold;
	u8 hdmi_tx_output_color_space;
	u8 hdmi_tx_input_color_space;
	u8 hdmi_tx_mode;
    u8 support_audio;

    u8 bOutputAudioMode;
    u8 bAudioChannelSwap;
    u8 bAudioChannelEnable;
    u8 bAudFs ;
    u32 TMDSClock ;
    u32 RCLK ;
    #ifdef _SUPPORT_HDCP_REPEATER_
        HDMITX_HDCP_State TxHDCP_State ;
        u16 usHDCPTimeOut ;
        u16 Tx_BStatus ;
    #endif
    u8 bAuthenticated:1 ;
    bool hdmi_mode;
    u8 bAudInterface;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *test_gpio;

	struct timer_list timer;
	struct delayed_work	restart;
};

static struct it6161 *it6161;
//static struct it6161 *it6161_dump;
static struct drm_bridge *it6161_bridge;

static const struct regmap_range it6161_mipi_rx_bridge_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xFF },
};

static const struct regmap_access_table it6161_mipi_rx_bridge_volatile_table = {
	.yes_ranges = it6161_mipi_rx_bridge_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it6161_mipi_rx_bridge_volatile_ranges),
};

static const struct regmap_config it6161_mipi_rx_bridge_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it6161_mipi_rx_bridge_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_range it6161_hdmi_tx_bridge_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xFF },
};

static const struct regmap_access_table it6161_hdmi_tx_bridge_volatile_table = {
	.yes_ranges = it6161_hdmi_tx_bridge_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it6161_hdmi_tx_bridge_volatile_ranges),
};

static const struct regmap_config it6161_hdmi_tx_bridge_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it6161_hdmi_tx_bridge_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_range it6161_cec_bridge_volatile_ranges[] = {
    { .range_min = 0, .range_max = 0xFF },
};

static const struct regmap_access_table it6161_cec_bridge_volatile_table = {
    .yes_ranges = it6161_cec_bridge_volatile_ranges,
    .n_yes_ranges = ARRAY_SIZE(it6161_cec_bridge_volatile_ranges),
};

static const struct regmap_config it6161_cec_bridge_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .volatile_table = &it6161_cec_bridge_volatile_table,
    .cache_type = REGCACHE_NONE,
};

static int it6161_mipi_rx_read(struct it6161 *it6161, unsigned int reg_addr)
{
	unsigned int value;
	int err;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	err = regmap_read(it6161->regmap_mipi_rx, reg_addr, &value);
	if (err < 0) {
		DRM_DEV_ERROR(dev, "mipi rx read failed reg[0x%x] err: %d", reg_addr,
			      err);
		return err;
	}

	return value;
}

static int mipi_rx_read_word(struct it6161 *it6161, unsigned int reg)
{
	int val_0, val_1;

	val_0 = it6161_mipi_rx_read(it6161, reg);

	if (val_0 < 0)
		return val_0;

	val_1 = it6161_mipi_rx_read(it6161, reg + 1);

	if (val_1 < 0)
		return val_1;

	return (val_1 << 8) | val_0;
}

static int it6161_mipi_rx_write(struct it6161 *it6161, unsigned int reg_addr,
		      unsigned int reg_val)
{
	int err;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	err = regmap_write(it6161->regmap_mipi_rx, reg_addr, reg_val);

	if (err < 0) {
		DRM_DEV_ERROR(dev, "mipi rx write failed reg[0x%x] = 0x%x err = %d",
			      reg_addr, reg_val, err);
		return err;
	}

	return 0;
}

static int it6161_mipi_rx_set_bits(struct it6161 *it6161, unsigned int reg,
			 unsigned int mask, unsigned int value)
{
	int err;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	err = regmap_update_bits(it6161->regmap_mipi_rx, reg, mask, value);
	if (err < 0) {
		DRM_DEV_ERROR(
			dev, "mipi rx set reg[0x%x] = 0x%x mask = 0x%x failed err %d",
			reg, value, mask, err);
		return err;
	}

	return 0;
}

#if 0
static void it6161_mipi_rx_dump(struct it6161 *it6161)
{
	unsigned int i, j;
	u8 regs[16];
	//struct device *dev = &it6161->i2c_mipi_rx->dev;

	it6161_debug("mipi rx dump:");
	for (i = 0; i <= 0xff; i += 16) {
		for (j = 0; j < 16; j++)
			regs[j] = it6161_mipi_rx_read(it6161, i + j);

		it6161_debug("[0x%02x] = %16ph", i, regs);
	}
}
#endif

static int it6161_hdmi_tx_read(struct it6161 *it6161, unsigned int reg_addr)
{
	unsigned int value;
	int err;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	err = regmap_read(it6161->regmap_hdmi_tx, reg_addr, &value);
	if (err < 0) {
		DRM_DEV_ERROR(dev, "hdmi tx read failed reg[0x%x] err: %d", reg_addr,
			      err);
		return err;
	}

	return value;
}

static int hdmi_tx_read_word(struct it6161 *it6161, unsigned int reg)
{
	int val_0, val_1;

	val_0 = it6161_hdmi_tx_read(it6161, reg);

	if (val_0 < 0)
		return val_0;

	val_1 = it6161_hdmi_tx_read(it6161, reg + 1);

	if (val_1 < 0)
		return val_1;

	return (val_1 << 8) | val_0;
}

#ifdef HDCP
static int it6161_hdmi_tx_burst_read(struct it6161 *it6161, unsigned int reg_addr, void *buffer, size_t size)
{
	struct device *dev = &it6161->i2c_mipi_rx->dev;
	int ret;

	ret = regmap_bulk_read(it6161->regmap_hdmi_tx, reg_addr, buffer, size);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "hdmi tx burst read failed reg[0x%x] ret: %d",
			      reg_addr, ret);

	return ret;
}
#endif
static int it6161_hdmi_tx_write(struct it6161 *it6161, unsigned int reg_addr,
				unsigned int reg_val)
{
	struct device *dev = &it6161->i2c_mipi_rx->dev;
	int err;

	err = regmap_write(it6161->regmap_hdmi_tx, reg_addr, reg_val);

	if (err < 0) {
		DRM_DEV_ERROR(dev, "hdmi tx write failed reg[0x%x] = 0x%x err = %d",
			      reg_addr, reg_val, err);
		return err;
	}

	return 0;
}

#if 0
static int hdmi_tx_burst_write(struct it6161 *it6161, unsigned int reg_addr,
			       void *buffer, size_t size)
{
	struct device *dev = &it6161->i2c_hdmi_tx->dev;
	int ret;

	ret = regmap_bulk_write(it6161->regmap_hdmi_tx, reg_addr,
				(u8 *)buffer, size);

	if (ret < 0) {
		DRM_DEV_ERROR(dev, "hdmi tx burst write failed reg[0x%x] ret = %d",
			      reg_addr, ret);
		return ret;
	}

	return ret;
}
#endif

static int it6161_hdmi_tx_set_bits(struct it6161 *it6161, unsigned int reg,
			 unsigned int mask, unsigned int value)
{
	int err;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	err = regmap_update_bits(it6161->regmap_hdmi_tx, reg, mask, value);
	if (err < 0) {
		DRM_DEV_ERROR(
			dev, "hdmi tx set reg[0x%x] = 0x%x mask = 0x%x failed err %d",
			reg, value, mask, err);
		return err;
	}

	return 0;
}

static int inline it6161_hdmi_tx_change_bank(struct it6161 *it6161, int x)
{
	return it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x03, x & 0x03);
}

#if 0
static void it6161_hdmi_tx_dump(struct it6161 *it6161, unsigned int bank)
{
	unsigned int i, j;
	u8 regs[16];
	//struct device *dev = &it6161->i2c_mipi_rx->dev;

	it6161_debug("hdmi tx dump bank: %d", bank);
	it6161_hdmi_tx_change_bank(it6161, bank);

	for (i = 0; i <= 0xff; i += 16) {
		for (j = 0; j < 16; j++)
			regs[j] = it6161_hdmi_tx_read(it6161, i + j);

		it6161_debug("[0x%02x] = %16ph", i, regs);
	}
}
#endif

static int it6161_cec_read(struct it6161 *it6161, unsigned int reg_addr)
{
    unsigned int value;
    int err;
    struct device *dev = &it6161->i2c_mipi_rx->dev;

    err = regmap_read(it6161->regmap_cec, reg_addr, &value);
    if (err < 0) {
        DRM_DEV_ERROR(dev, "cec read failed reg[0x%x] err: %d", reg_addr,
                  err);
        return err;
    }

    return value;
}

static int it6161_cec_write(struct it6161 *it6161, unsigned int reg_addr,
              unsigned int reg_val)
{
    int err;
    struct device *dev = &it6161->i2c_mipi_rx->dev;

    err = regmap_write(it6161->regmap_cec, reg_addr, reg_val);

    if (err < 0) {
        DRM_DEV_ERROR(dev, "cec write failed reg[0x%x] = 0x%x err = %d",
                  reg_addr, reg_val, err);
        return err;
    }

    return 0;
}

#if 0
static int it6161_cec_set_bits(struct it6161 *it6161, unsigned int reg,
             unsigned int mask, unsigned int value)
{
    int err;
    struct device *dev = &it6161->i2c_mipi_rx->dev;

    err = regmap_update_bits(it6161->regmap_cec, reg, mask, value);
    if (err < 0) {
        DRM_DEV_ERROR(
            dev, "cec set reg[0x%x] = 0x%x mask = 0x%x failed err %d",
            reg, value, mask, err);
        return err;
    }

    return 0;
}
#endif

static inline struct it6161 *connector_to_it6161(struct drm_connector *c)
{
	return container_of(c, struct it6161, connector);
}

static inline struct it6161 *bridge_to_it6161(struct drm_bridge *bridge)
{
	return container_of(bridge, struct it6161, bridge);
}

static void mipi_rx_logic_reset(struct it6161 *it6161)
{
    it6161_mipi_rx_set_bits(it6161, 0x05, 0x08, 0x08);
}

static void mipi_rx_logic_reset_release(struct it6161 *it6161)
{
    it6161_mipi_rx_set_bits(it6161, 0x05, 0x08, 0x00);
}

static void hdmi_tx_logic_reset(struct it6161 *it6161)
{
    it6161_hdmi_tx_set_bits(it6161, 0x04, 0x20, 0x20);
}

static void it6161_mipi_rx_int_mask_disable(struct it6161 *it6161)
{
    it6161_mipi_rx_set_bits(it6161, 0x0F, 0x03, 0x00);
    it6161_mipi_rx_write(it6161, 0x09, 0x00);
    it6161_mipi_rx_write(it6161, 0x0A, 0x00);
    it6161_mipi_rx_write(it6161, 0x0B, 0x00);
}

static void it6161_mipi_rx_int_mask_enable(struct it6161 *it6161)
{
    it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x03, 0x00);
    it6161_mipi_rx_write(it6161, 0x09, EnMBPM ? 0x11 : 0xBF);
    it6161_mipi_rx_write(it6161, 0x0A, 0xFF);
    it6161_mipi_rx_write(it6161, 0x0B, 0x3F);
}

static void hdmi_tx_hdcp_int_mask_disable(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x03, 0x00);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_INT_MASK2, B_TX_AUTH_FAIL_MASK | B_TX_AUTH_DONE_MASK | B_TX_KSVLISTCHK_MASK, B_TX_AUTH_FAIL_MASK | B_TX_AUTH_DONE_MASK | B_TX_KSVLISTCHK_MASK);
}

#ifdef HDCP
static void hdmi_tx_hdcp_int_mask_enable(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x03, 0x00);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_INT_MASK2, B_TX_AUTH_FAIL_MASK | B_TX_AUTH_DONE_MASK | B_TX_KSVLISTCHK_MASK, ~(B_TX_AUTH_FAIL_MASK | B_TX_AUTH_DONE_MASK | B_TX_KSVLISTCHK_MASK));
}
#endif
/*
static void it6161_hdmi_tx_int_mask_disable(struct it6161 *it6161)
{
    it6161_mipi_rx_set_bits(it6161, 0x0F, 0x03, 0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_INT_MASK1, 0xFF);
    it6161_hdmi_tx_write(it6161, REG_TX_INT_MASK2, 0xFF);
    it6161_hdmi_tx_write(it6161, REG_TX_INT_MASK3, 0xFF);
}
*/
static void it6161_hdmi_tx_int_mask_enable(struct it6161 *it6161)
{
    it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x03, 0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_INT_MASK1, ~(B_TX_AUDIO_OVFLW_MASK | B_TX_DDC_FIFO_ERR_MASK | B_TX_DDC_BUS_HANG_MASK | B_TX_HPD_MASK | B_TX_RXSEN_MASK));//0x7C);
    it6161_hdmi_tx_write(it6161, REG_TX_INT_MASK2, ~(B_TX_AUTH_FAIL_MASK | B_TX_AUTH_DONE_MASK | B_TX_KSVLISTCHK_MASK | B_TX_PKT_VID_UNSTABLE_MASK));
    it6161_hdmi_tx_write(it6161, REG_TX_INT_MASK3, ~B_TX_VIDSTABLE_MASK);
}

static void it6161_hdmi_tx_write_table(struct it6161 *it6161, const RegSetEntry table[], int size)
{
	int i ;

	for (i = 0; i < size; i++) {
		if (table[i].mask == 0 && table[i].value == 0) {
			msleep(table[i].offset);
		} else if (table[i].mask == 0xFF) {
			it6161_hdmi_tx_write(it6161, table[i].offset,
								 table[i].value);
		} else {
			it6161_hdmi_tx_set_bits(it6161, table[i].offset, table[i].mask,
									table[i].value);
		}
	}
}

static inline void hdmi_tx_enable_pattern_generator(struct it6161 *it6161)
{
	it6161_debug("enable pattern generator");
	it6161_hdmi_tx_set_bits(it6161, 0xA8, 0x01, 0x01);
}

static inline void hdmi_tx_disable_pattern_generator(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, 0xA8, 0x01, 0x00);
}

static inline void hdmi_tx_pattern_generator_setup_color(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, 0xA9, 0x3F, (HDMI_TX_PATTERN_COLLOR_B << 4) | (HDMI_TX_PATTERN_COLLOR_G << 2) | HDMI_TX_PATTERN_COLLOR_R);
}

static void hdmi_tx_setup_pattern_generator(struct it6161 *it6161)
{
    switch (HDMI_TX_PATTERN_GENERATOR_FORMAT) {
		case 2:
		it6161_hdmi_tx_write_table(it6161, hdmi_tx_pg_480p60_table, sizeof(hdmi_tx_pg_480p60_table) / sizeof(RegSetEntry));
		DRM_INFO("use 480p60 pattern");
		break;

		case 4:
		it6161_hdmi_tx_write_table(it6161, hdmi_tx_pg_720p60_table, sizeof(hdmi_tx_pg_720p60_table) / sizeof(RegSetEntry));
		DRM_INFO("use 720p60 pattern");
		break;

		case 16:
		it6161_hdmi_tx_write_table(it6161, hdmi_tx_pg_1080p60_table, sizeof(hdmi_tx_pg_1080p60_table) / sizeof(RegSetEntry));
		DRM_INFO("use 1080p60 pattern");
		break;

		default:
		it6161_hdmi_tx_write_table(it6161, hdmi_tx_pg_1080p60_table, sizeof(hdmi_tx_pg_1080p60_table) / sizeof(RegSetEntry));
		DRM_INFO("other format, will use 1080p60 pattern");
    }

    hdmi_tx_pattern_generator_setup_color(it6161);
	hdmi_tx_enable_pattern_generator(it6161);
}

static void show_display_mode(struct it6161 *it6161, struct drm_display_mode *display_mode, u8 select)
{
	char *name[3] = { "source output", "it6161 hdmi tx receive", "mipi rx p receive" };

	DRM_INFO("%s timing:", name[select]);
	DRM_INFO("timing name:%s", display_mode->name);
	DRM_INFO("clock = %dkHz", display_mode->clock);
	DRM_INFO("htotal = %d", display_mode->htotal);
	DRM_INFO("hactive = %d", display_mode->hdisplay);
	DRM_INFO("hfront_porch = %d", display_mode->hsync_start - display_mode->hdisplay);
	DRM_INFO("hsyncw = %d", display_mode->hsync_end - display_mode->hsync_start);
	DRM_INFO("hback_porch = %d", display_mode->htotal - display_mode->hsync_end);

	DRM_INFO("vtotal = %d", display_mode->vtotal);
	DRM_INFO("vactive = %d", display_mode->vdisplay);
	DRM_INFO("vfront_porch = %d", display_mode->vsync_start - display_mode->vdisplay);
	DRM_INFO("vsyncw = %d", display_mode->vsync_end - display_mode->vsync_start);
	DRM_INFO("vback_porch = %d", display_mode->vtotal - display_mode->vsync_end);
	DRM_INFO("drm_display_mode flags = 0x%04x", display_mode->flags);

}

static void inline it6161_set_interrupts_active_level(enum it6161_active_level level)
{
	it6161_mipi_rx_set_bits(it6161, 0x0D, 0x02, level == HIGH ? 0x02 : 0x00);
	it6161_hdmi_tx_set_bits(it6161, 0x05, 0xC0, level == HIGH ? 0x80 : 0x40);
}

static void hdmi_tx_init(struct it6161 *it6161)
{
    it6161_debug("init hdmi tx");

    it6161_hdmi_tx_write_table(it6161, HDMITX_Init_Table, ARRAY_SIZE(HDMITX_Init_Table));
    it6161_hdmi_tx_write_table(it6161, HDMITX_PwrOn_Table, ARRAY_SIZE(HDMITX_PwrOn_Table));
    it6161_hdmi_tx_write_table(it6161, HDMITX_DefaultVideo_Table, ARRAY_SIZE(HDMITX_DefaultVideo_Table));
    it6161_hdmi_tx_write_table(it6161, HDMITX_SetHDMI_Table, ARRAY_SIZE(HDMITX_SetHDMI_Table));
    it6161_hdmi_tx_write_table(it6161, HDMITX_DefaultAVIInfo_Table, ARRAY_SIZE(HDMITX_DefaultAVIInfo_Table));
    it6161_hdmi_tx_write_table(it6161, HDMITX_DeaultAudioInfo_Table, ARRAY_SIZE(HDMITX_DeaultAudioInfo_Table));
    it6161_hdmi_tx_write_table(it6161, HDMITX_Aud_CHStatus_LPCM_20bit_48Khz, ARRAY_SIZE(HDMITX_Aud_CHStatus_LPCM_20bit_48Khz));
    it6161_hdmi_tx_write_table(it6161, HDMITX_AUD_SPDIF_2ch_24bit, ARRAY_SIZE(HDMITX_AUD_SPDIF_2ch_24bit));

#ifdef SUPPORT_CEC
    it6161_hdmi_tx_change_bank(it6161, 0);
    it6161_hdmi_tx_set_bits(it6161, 0x8D, 0x01, 0x01);//it6161_hdmi_tx_write(it6161,  0xf, 0 ); //pet

    Initial_Ext_Int1();
    HDMITX_CEC_Init();
#endif // SUPPORT_CEC
}

static bool mipi_rx_get_m_video_stable(struct it6161 *it6161)
{
	return !!(it6161_mipi_rx_read(it6161, 0x0D) & 0x10);
}

static bool mipi_rx_get_p_video_stable(struct it6161 *it6161)
{
	return !!(it6161_mipi_rx_read(it6161, 0x0D) & 0x20);
}

static void mipi_rx_setup_polarity(struct it6161 *it6161)
{
	struct drm_display_mode *display_mode = &it6161->source_display_mode;
	u8 polarity;

	polarity = ((display_mode->flags & DRM_MODE_FLAG_PHSYNC) == DRM_MODE_FLAG_PHSYNC) ? 0x01 : 0x00;
	polarity |= ((display_mode->flags & DRM_MODE_FLAG_PVSYNC) == DRM_MODE_FLAG_PVSYNC) ? 0x02 : 0x00;
	
	it6161_mipi_rx_set_bits(it6161, 0x4E, 0x03, polarity);
}

static void mipi_rx_afe_configuration(struct it6161 *it6161, u8 data_id)
{
    //struct device *dev = &it6161->i2c_mipi_rx->dev;
    u8 MPLaneNum = (it6161->mipi_rx_lane_count - 1);

    it6161_debug("afe configuration data_id:0x%02x", data_id);

    if (data_id == RGB_18b) {
        if( MPLaneNum==3 ) {
            // if( EnMPx1PCLK )
                it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x02); // MPPCLKSel = 1; // 4-lane : MCLK = 1/1 PCLK
            // else
            // {
                // it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x02); // MPPCLKSel = 1; // 4-lane : MCLK = 1/1 PCLK
                DRM_INFO("Solomon is impossible TXPLL= 9/2 PCLK !!! \n");
                DRM_INFO("MCLK=3/4PCLK Change to MCLK=PCLK ...\n");
            // }
        } else if( MPLaneNum==1 ) {
            // if( EnMPx1PCLK )
                it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x05); // MPPCLKSel = 6; // 2-lane : MCLK = 1/1 PCLK
            // else
            // {
                // it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x05); // MPPCLKSel = 6; // 2-lane : MCLK = 1/1 PCLK
                DRM_INFO("IT6121 is impossible RXPLL= 2/9 PCLK !!! \n");
                DRM_INFO("MCLK=3/4PCLK Change to MCLK=PCLK ...\n");
            // }
        } else if( MPLaneNum==0 ) {
            // if( EnMPx1PCLK )
                // it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x0b);// MPPCLKSel = 7; // 1-lane : MCLK = 1/1 PCLK
            // else
                it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x08); // MPPCLKSel = 8; // 1-lane : MCLK = 3/4 PCLK
        }
    } else {
        if( MPLaneNum==3 ) {
        // if( EnMPx1PCLK )
            // it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x03);// MPPCLKSel = 0; // 4-lane : MCLK = 1/1 PCLK
        // else
            it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x02);// MPPCLKSel = 1; // 4-lane : MCLK = 3/4 PCLK
        } else if( MPLaneNum==1 ) {
            // if( EnMPx1PCLK )
                // it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x07); // MPPCLKSel = 2; // 2-lane : MCLK = 1/1 PCLK
            // else
                it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x05); // MPPCLKSel = 3; // 2-lane : MCLK = 3/4 PCLK
        } else if( MPLaneNum==0 ) {
            // if( EnMPx1PCLK )
                // it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x0f);// MPPCLKSel = 4; // 1-lane : MCLK = 1/1 PCLK
            // else
                it6161_mipi_rx_set_bits(it6161, 0x80, 0x1F, 0x0b); // MPPCLKSel = 5; // 1-lane : MCLK = 3/4 PCLK
        }
    }
}

static void mipi_rx_configuration(struct it6161 *it6161)
{
    //struct device *dev = &it6161->i2c_mipi_rx->dev;
    u8 mipi_lane_config = (it6161->mipi_rx_lane_count - 1);

	//20200211 D0
	it6161_mipi_rx_set_bits(it6161, 0x10, 0x0F, 0x0F);
	msleep(1);//idle(100);
	it6161_mipi_rx_set_bits(it6161, 0x10, 0x0F, 0x00);

	// MPRX Software Reset
	// it6161_mipi_rx_set_bits(it6161, 0x05, 0x07, 0x07);
	// msleep(1);
	// it6161_mipi_rx_set_bits(it6161, 0x05, 0x08, 0x08);
	mipi_rx_logic_reset(it6161);
	msleep(1);
	mipi_rx_logic_reset_release(it6161);

	it6161_mipi_rx_int_mask_disable(it6161);

	/* setup INT pin: active low */
	it6161_mipi_rx_set_bits(it6161, 0x0d, 0x02, 0x00);

	it6161_mipi_rx_set_bits(it6161, 0x0C, 0x0F, (MPLaneSwap<<3) + (MPPNSwap<<2) + mipi_lane_config);

	it6161_mipi_rx_set_bits(it6161, 0x11, 0x3F, (EnIOIDDQ<<5)+(EnStb2Rst<<4)+(EnExtStdby<<3)+(EnStandby<<2)+(InvPCLK<<1)+InvMCLK);
	it6161_mipi_rx_set_bits(it6161, 0x12, 0x03, (PDREFCNT<<1)+PDREFCLK);

	it6161_mipi_rx_set_bits(it6161, 0x18, 0xf7, (RegEnSyncErr<<7)+(SkipStg<<4)+HSSetNum);
	it6161_mipi_rx_set_bits(it6161, 0x19, 0xf3, (PPIDbgSel<<4)+(EnContCK<<1)+EnDeSkew);
	it6161_mipi_rx_set_bits(it6161, 0x20, 0xf7, (EOTPSel<<4)+(RegEnDummyECC<<2)+(RegIgnrBlk<<1)+RegIgnrNull);
	it6161_mipi_rx_set_bits(it6161, 0x21, 0x07, LMDbgSel);

	// it6161_mipi_rx_set_bits(it6161, 0x44, 0x38, (MREC_Update<<5)+(PREC_Update<<4)+(REGSELDEF<<3));
	it6161_mipi_rx_set_bits(it6161, 0x44, 0x3a, (MREC_Update<<5)+(PREC_Update<<4)+(REGSELDEF<<3)+(RegAutoSync<<1));//D0 20200211
	it6161_mipi_rx_set_bits(it6161, 0x4B, 0x1f, (EnFReSync<<4)+(EnVREnh<<3)+EnVREnhSel);
	it6161_mipi_rx_write(it6161, 0x4C, PPSFFRdStg);
	it6161_mipi_rx_set_bits(it6161, 0x4D, 0x01, (PPSFFRdStg>>8)&0x01);
	it6161_mipi_rx_set_bits(it6161, 0x4E, 0x0C, (EnVReSync<<3)+(EnHReSync<<2));
	it6161_mipi_rx_set_bits(it6161, 0x4F, 0x03, EnFFAutoRst);

	//it6161_mipi_rx_write(it6161, 0x27, MPVidType);
	it6161_mipi_rx_set_bits(it6161, 0x70, 0x01, EnMAvg);
	it6161_mipi_rx_write(it6161, 0x72, MShift);
	it6161_mipi_rx_write(it6161, 0x73, PShift);
	it6161_mipi_rx_set_bits(it6161, 0x80, 0x20, ENABLE_MIPI_RX_EXTERNAL_CLOCK << 5);
	it6161_mipi_rx_write(it6161, 0x21, 0x00); //debug sel
	//	 it6161_mipi_rx_set_bits(it6161, 0x84, 0x70, 0x70);	// max swing
	// 	 it6161_mipi_rx_set_bits(it6161, 0x84, 0x70, 0x40);	// def swing
	it6161_mipi_rx_set_bits(it6161, 0x84, 0x70, 0x00);	// min swing

	it6161_mipi_rx_set_bits(it6161, 0xA0, 0x01, EnMBPM);

    /* enable auto detect format */
    it6161_mipi_rx_set_bits(it6161, 0x21, 0x08, 0x08);


	// if( REGSELDEF == true )
	// {
		// DRM_INFO("REGSELDEF MODE !!! ...\n");
		// PHFP = 0x10;
		// PHSW = 0x3e;
		// PHBP = 0x3c;
		// it6161_mipi_rx_write(it6161, 0x30, PHFP);					// HFP
		// it6161_mipi_rx_write(it6161, 0x31, 0x80+((PHFP&0x3F00)>>8));
		// it6161_mipi_rx_write(it6161, 0x32, PHSW);					// HBP
		// it6161_mipi_rx_write(it6161, 0x33, 0x80+((PHSW&0x3F00)>>8));
		// it6161_mipi_rx_write(it6161, 0x34, PHBP);					// HBP
		// it6161_mipi_rx_write(it6161, 0x35, 0x80+((PHFP&0x3F00)>>8));
	// }

	it6161_mipi_rx_set_bits(it6161, 0x70, 0x01, EnMAvg);

	it6161_mipi_rx_set_bits(it6161, 0x05, 0x02, 0x02);  // Video Clock Domain Reset

	if( EnMBPM ) {
		//HSW = pSetVTiming->HSyncWidth;
		//VSW = pSetVTiming->VSyncWidth;

		it6161_mipi_rx_write(it6161, 0xA1, 0x00);	// HRS offset
		it6161_mipi_rx_write(it6161, 0xA2, 0x00);	// VRS offset

		// it6161_mipi_rx_write(it6161, 0xA3, 44);//HSW);	// HSW
		// it6161_mipi_rx_write(it6161, 0xA5, 5);//VSW);	// VSW
			it6161_mipi_rx_write(it6161, 0xA3, 0x08);//0x10);	// HSW
			it6161_mipi_rx_write(it6161, 0xA5, 0x04);	// VSw

		// it6161_hdmi_tx_set_bits(it6161, 0xa9, 0xc0, (EnTBPM<<7)/* + (EnTxPatMux<<6)*/);
		// it6161_hdmi_tx_set_bits(it6161, 0xbf, 0x80, (NRTXRCLK<<7));
	}
	if( MPForceStb == true ){
		// mprxwr(0x30, HFP);					// HSP
		// mprxwr(0x31, 0x80+((HFP&0x3F00)>>8));
		// mprxwr(0x32, HSW);					// HSW
		// mprxwr(0x33, 0x80+((HSW&0x3F00)>>8));
		// mprxwr(0x34, HBP&0xFF);				// HBP
		// mprxwr(0x35, 0x80+((HBP&0x3F00)>>8));
		// mprxwr(0x36, HDEW&0xFF);				// HDEW
		// mprxwr(0x37, 0x80+((HDEW&0x3F00)>>8));
		// mprxwr(0x38, HVR2nd&0xFF);			// HVR2nd
		// mprxwr(0x39, 0x80+((HVR2nd&0x3F00)>>8));

		// mprxwr(0x3A, VFP);					// VSP
		// mprxwr(0x3B, 0x80+((VFP&0x3F00)>>8));
		// mprxwr(0x3C, VSW);					// VSW
		// mprxwr(0x3D, 0x80+((VSW&0x3F00)>>8));
		// mprxwr(0x3E, VBP&0xFF);				// VBP
		// mprxwr(0x3F, 0x80+((VBP&0x3F00)>>8));
		// mprxwr(0x40, VDEW&0xFF);				// VDEW
		// mprxwr(0x41, 0x80+((VDEW&0x3F00)>>8));
		// mprxwr(0x42, VFP2nd&0xFF);			// VFP2nd
		// mprxwr(0x43, 0x80+((VFP2nd&0x3F00)>>8));

		// mprxset(0x4e, 0x03, 0xC0+(VPol<<1)+HPol);
	}
	else
	{
		if( REGSELDEF == false ) {
			it6161_mipi_rx_set_bits(it6161, 0x31, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x33, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x35, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x37, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x39, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x3A, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x3C, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x3E, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x41, 0x80, 0x00);
			it6161_mipi_rx_set_bits(it6161, 0x43, 0x80, 0x00);
		}

		// mprxset(0x4e, 0x03, 0x00+(VPol<<1)+HPol);
	}
}

static void mipi_rx_init(struct it6161 *it6161)
{
    //struct device *dev = &it6161->i2c_mipi_rx->dev;

    it6161_debug("init mpip rx");
    mipi_rx_configuration(it6161);
    it6161_mipi_rx_set_bits(it6161, 0x05, 0x03, 0x00);  // Enable MPRX clock domain
}

static void hdmi_tx_video_reset(struct it6161 *it6161)
{
    //struct device *dev = &it6161->i2c_mipi_rx->dev;

    it6161_debug("%s reg04:0x%02x reg05:0x%02x reg6:0x%02x reg07:0x%02x reg08:0x%02x reg0e:0x%02x", __func__, it6161_hdmi_tx_read(it6161, 0x04), it6161_hdmi_tx_read(it6161, 0x05), it6161_hdmi_tx_read(it6161, 0x06), it6161_hdmi_tx_read(it6161, 0x07), it6161_hdmi_tx_read(it6161, 0x08), it6161_hdmi_tx_read(it6161, 0x0e));
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_VID_RST, B_HDMITX_VID_RST);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_VID_RST, 0x00);
    msleep(10);
}
/*
static void hdmi_tx_audio_fifo_reset(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_AUD_RST, B_HDMITX_AUD_RST);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_AUD_RST, 0x00);
}
*/
/* DDC master will set to be host */

static void it6161_hdmi_tx_clear_ddc_fifo(struct it6161 *it6161)
{
	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERDDC | B_TX_MASTERHOST);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD, CMD_FIFO_CLR);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERHOST, 0x00);
}

#ifdef HDCP
static void it6161_hdmi_tx_generate_ddc_sclk(struct it6161 *it6161)
{
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD,CMD_GEN_SCLCLK);
}
#endif
static void hdmi_tx_generate_blank_timing(struct it6161 *it6161)
{
	struct drm_display_mode *display_mode = &it6161->source_display_mode;
	bool force_hdmi_tx_clock_stable = true, force_hdmi_tx_video_stable = true, hdmi_tx_by_pass_mode = false, de_generation = false, enable_de_only = true;
	u8 polarity;
	u16 hsync_start, hsync_end, vsync_start, vsync_end, htotal, hde_start, vtotal;
	u16 vsync_start_2nd = 0, vsync_end_2nd = 0, vsync_rising_at_h_2nd;

	it6161_debug("start %s", __func__);
	polarity = ((display_mode->flags & DRM_MODE_FLAG_PHSYNC) == DRM_MODE_FLAG_PHSYNC) ? 0x02 : 0x00;
	polarity |= ((display_mode->flags & DRM_MODE_FLAG_PVSYNC) == DRM_MODE_FLAG_PVSYNC) ? 0x04 : 0x00;

	hsync_start = display_mode->hsync_start - display_mode->hdisplay - 1;
	hsync_end = hsync_start + display_mode->hsync_end - display_mode->hsync_start;
	vsync_rising_at_h_2nd = hsync_start + display_mode->htotal / 2;
	hde_start = display_mode->htotal - display_mode->hsync_start;

	it6161_hdmi_tx_set_bits(it6161, 0xD1, 0x0C, force_hdmi_tx_clock_stable << 3 | force_hdmi_tx_video_stable << 2);
	it6161_hdmi_tx_set_bits(it6161, 0xA9, 0x80, hdmi_tx_by_pass_mode << 7);
	it6161_hdmi_tx_set_bits(it6161, 0x90, 0x01, de_generation);
	it6161_hdmi_tx_write(it6161, 0x91, vsync_rising_at_h_2nd >> 4);
	it6161_hdmi_tx_set_bits(it6161, 0x90, 0xF0, (vsync_rising_at_h_2nd & 0x00F) << 4);
	it6161_hdmi_tx_set_bits(it6161, 0x90, 0x06, polarity);
	it6161_hdmi_tx_write(it6161, 0x95, (u8)hsync_start);
	it6161_hdmi_tx_write(it6161, 0x96, (u8)hsync_end);
	it6161_hdmi_tx_write(it6161, 0x97, (hsync_end & 0x0F00) >> 4 | hsync_start >> 8);

	vsync_start = display_mode->vsync_start - display_mode->vdisplay;
	vsync_end = display_mode->vsync_end - display_mode->vdisplay;

	if ((display_mode->flags & DRM_MODE_FLAG_INTERLACE) != DRM_MODE_FLAG_INTERLACE) {
		vsync_start_2nd = 0x0FFF;
		vsync_end_2nd = 0x3F;
		vtotal = display_mode->vtotal - 1;
		it6161_hdmi_tx_set_bits(it6161, 0xA5, 0x10, 0x00);
	} else {
		vtotal = display_mode->vtotal * 2;
		it6161_hdmi_tx_set_bits(it6161, 0xA5, 0x10, 0x10);
	}
	it6161_hdmi_tx_write(it6161, 0xA0, (u8)vsync_start);
	it6161_hdmi_tx_write(it6161, 0xA1, (vsync_end & 0x0F) << 4 | vsync_start >> 8);
	it6161_hdmi_tx_write(it6161, 0xA2, (u8)vsync_start_2nd);
	it6161_hdmi_tx_write(it6161, 0xA6, (vsync_end_2nd & 0xF0) | vsync_end >> 4);
	it6161_hdmi_tx_write(it6161, 0xA3, (vsync_end_2nd & 0x0F) << 4 | vsync_start_2nd >> 8);
	it6161_hdmi_tx_write(it6161, 0xA4, vsync_rising_at_h_2nd);
	it6161_hdmi_tx_set_bits(it6161, 0xB1, 0x51, (hsync_end & 0x1000) >> 6 | (hsync_start & 0x1000) >> 8 | hde_start >> 12);
	it6161_hdmi_tx_set_bits(it6161, 0xA5, 0x2F, enable_de_only << 5 | vsync_rising_at_h_2nd >> 8);
	it6161_hdmi_tx_set_bits(it6161, 0xB2, 0x05, (vsync_rising_at_h_2nd & 0x1000) >> 10 | (vsync_rising_at_h_2nd & 0x1000) >> 12);

	htotal = display_mode->htotal - 1;
	it6161_hdmi_tx_set_bits(it6161, 0x90, 0xF0, (htotal & 0x0F) << 4);
	it6161_hdmi_tx_write(it6161, 0x91, (htotal & 0x0FF0) >> 4);
	it6161_hdmi_tx_set_bits(it6161, 0xB2, 0x01, (htotal & 0x1000) >> 12);
	it6161_hdmi_tx_write(it6161, 0x98, vtotal & 0x0FF);
	it6161_hdmi_tx_write(it6161, 0x99, (vtotal & 0xF00) >> 8);
}

/* force abort DDC and reset DDC bus */

static void it6161_hdmi_tx_abort_ddc(struct it6161 *it6161)
{
	u8 cp_desire, sw_reset, ddc_master, retry = 2;
	u8 uc, timeout, i;

	it6161_debug("%s", __func__);
	/* save the sw reset, ddc master and cp desire setting */
	sw_reset = it6161_hdmi_tx_read(it6161, REG_TX_SW_RST);
	cp_desire = it6161_hdmi_tx_read(it6161, REG_TX_HDCP_DESIRE);
	ddc_master = it6161_hdmi_tx_read(it6161, REG_TX_DDC_MASTER_CTRL);

	// it6161_hdmi_tx_write(it6161, REG_TX_HDCP_DESIRE,CPDesire&(~B_TX_CPDESIRE)); // @emily change order
	it6161_hdmi_tx_write(it6161, REG_TX_SW_RST, sw_reset | B_TX_HDCP_RST_HDMITX);         // @emily change order
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERDDC | B_TX_MASTERHOST);

	/* do abort DDC */
	for (i = 0; i < retry; i++) {
		it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD, CMD_DDC_ABORT);
		it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD, CMD_GEN_SCLCLK);//hdmitxwr(0x15, 0x0A); //it6161A0   // Generate SCL Clock

		for (timeout = 0; timeout < 200; timeout++) {
			uc = it6161_hdmi_tx_read(it6161, REG_TX_DDC_STATUS);
			if (uc&B_TX_DDC_DONE)
				break ;

			if (uc & (B_TX_DDC_NOACK | B_TX_DDC_WAITBUS | B_TX_DDC_ARBILOSE)) {
				DRM_INFO("it6161_hdmi_tx_abort_ddc Fail by reg16=%02X\n",(int)uc);//pet
				break ;
			}
			/* delay 1 ms to stable */
			msleep(1);
		}
	}
}

static bool hdmi_tx_get_video_state(struct it6161 *it6161)
{
	return !!(B_TXVIDSTABLE & it6161_hdmi_tx_read(it6161, REG_TX_SYS_STATUS));
}

static bool inline hdmi_tx_get_sink_hpd(struct it6161 *it6161)
{
    return !!(it6161_hdmi_tx_read(it6161, REG_TX_SYS_STATUS) & B_TX_HPDETECT);
}

static bool it6161_ddc_op_finished(struct it6161 *it6161)
{
    int reg16 = it6161_hdmi_tx_read(it6161, REG_TX_DDC_STATUS);

    if (reg16 < 0)
        return false;

    return (reg16 & B_TX_DDC_DONE) == B_TX_DDC_DONE;
}

static int it6161_ddc_wait(struct it6161 *it6161)
{
    int status;
    unsigned long timeout;
    //struct device *dev = &it6161->client->dev;

    timeout = jiffies + msecs_to_jiffies(AUX_WAIT_TIMEOUT_MS) + 1;

    while (!it6161_ddc_op_finished(it6161)) {
        if (time_after(jiffies, timeout)) {
            DRM_INFO("Timed out waiting AUX to finish");
            return -ETIMEDOUT;
        }
        usleep_range(1000, 2000);
    }

    status = it6161_hdmi_tx_read(it6161, REG_TX_DDC_STATUS);
    if (status < 0) {
        DRM_INFO("Failed to read DDC channel: 0x%02x", status);
        return status;
    }

    if(status & B_TX_DDC_DONE) {
        return 0;
    } else {
        DRM_INFO("DDC error: 0x%02x", status);
        return -EIO;
    }
}

static void hdmi_tx_ddc_operation(struct it6161 *it6161, u8 addr, u8 offset, u8 size, u8 segment, u8 cmd)
{
	size = min(size, (u8)DDC_FIFO_MAXREQ);
	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERDDC | B_TX_MASTERHOST);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_HEADER, addr);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQOFF, offset);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQCOUNT, size);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_EDIDSEG, segment);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD, cmd);
}

//////////////////////////////////////////////////////////////////////
// Function: it6161_ddc_get_edid_operation
// Parameter: buffer - the pointer of buffer to receive EDID ucdata.
//            segment - the segment of EDID readback.
//            offset - the offset of EDID ucdata in the segment. in byte.
//            size - the read back bytes count,cannot exceed 32
// Return: ER_SUCCESS if successfully getting EDID. ER_FAIL otherwise.
// Remark: function for read EDID ucdata from reciever.
// Side-Effect: DDC master will set to be HOST. DDC FIFO will be used and dirty.
//////////////////////////////////////////////////////////////////////

static int it6161_ddc_get_edid_operation(struct it6161 *it6161, u8 *buffer, u8 segment, u8 offset, int size)
{
	u8 status, i;

	if(!buffer)
		return -ENOMEM;

	if(it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1) & B_TX_INT_DDC_BUS_HANG) {
		DRM_INFO("Called it6161_hdmi_tx_abort_ddc()");
		it6161_hdmi_tx_abort_ddc(it6161);
	}

	it6161_hdmi_tx_clear_ddc_fifo(it6161);    
	status = it6161_ddc_wait(it6161);

	if (status < 0)
		goto error;

	hdmi_tx_ddc_operation(it6161, DDC_EDID_ADDRESS, offset, size, segment, CMD_EDID_READ);
	status = it6161_ddc_wait(it6161);

	if (status < 0)
		goto error;

	for (i = 0; i < size; i++) {
		status = it6161_hdmi_tx_read(it6161, REG_TX_DDC_READFIFO);

		if (status < 0)
			goto error;

		buffer[i] = status;
	}

	return i;

error:
	return status;
}

static int it6161_get_edid_block(void *data, u8 *buf, unsigned int block_num,
                                                size_t len)
{
    struct it6161 *it6161 = data;
    u8 offset, ret, step = 8;

    step = min(step, (u8)DDC_FIFO_MAXREQ);
    DRM_INFO("[%s] edid block number:%d read step:%d", __func__, block_num, step);

    for (offset = 0; offset < len; offset += step) {
        ret = it6161_ddc_get_edid_operation(it6161, buf + offset, block_num / 2, (block_num % 2) * EDID_LENGTH + offset, step);

        if(ret < 0)
            return ret;
#ifdef __linux__
        DRM_INFO("[0x%02x]: %*ph", offset, step, buf + offset);
#else
        DRM_INFO("[0x%02x]:", offset);

        for (ret = 0; ret < step; ret++)
            printf(" 0x%02x", (buf + offset)[ret]);
        printf("\n\r");
#endif
    }
    return 0;
}

static void hdmi_tx_set_capability_from_edid_parse(struct it6161 *it6161)
{
	struct drm_display_info *info = &it6161->connector.display_info;

	it6161->hdmi_mode = drm_detect_hdmi_monitor(it6161->edid);
	it6161->support_audio = drm_detect_monitor_audio(it6161->edid);
	if (it6161->hdmi_tx_output_color_space == F_MODE_YUV444) {
		if ((info->color_formats & DRM_COLOR_FORMAT_YCRCB444) != DRM_COLOR_FORMAT_YCRCB444) {
			it6161->hdmi_tx_output_color_space &= ~F_MODE_CLRMOD_MASK;
			it6161->hdmi_tx_output_color_space |= F_MODE_RGB444;
		}
	}

	if (it6161->hdmi_tx_output_color_space == F_MODE_YUV422) {
		if ((info->color_formats & DRM_COLOR_FORMAT_YCRCB422) != DRM_COLOR_FORMAT_YCRCB422) {
			it6161->hdmi_tx_output_color_space &= ~F_MODE_CLRMOD_MASK;
			it6161->hdmi_tx_output_color_space |= F_MODE_RGB444;
		}
	}
	DRM_INFO("%s mode, monitor %ssupport audio, outputcolormode:%d color_formats:0x%08x color_depth:%d",
		it6161->hdmi_mode ? "HDMI" : "DVI", it6161->support_audio ? "" : "not ", it6161->hdmi_tx_output_color_space, info->color_formats, info->bpc);

	if ((info->color_formats & DRM_COLOR_FORMAT_RGB444) == DRM_COLOR_FORMAT_RGB444)
		DRM_INFO("support RGB444 output");
	if ((info->color_formats & DRM_COLOR_FORMAT_YCRCB444) == DRM_COLOR_FORMAT_YCRCB444)
		DRM_INFO("support YUV444 output");
	if ((info->color_formats & DRM_COLOR_FORMAT_YCRCB422) == DRM_COLOR_FORMAT_YCRCB422)
		DRM_INFO("support YUV422 output");
}

static void it6161_variable_config(struct it6161 *it6161)
{
    it6161->hdmi_tx_hdcp_retry = HDMI_TX_HDCP_RETRY;
    it6161->hdmi_tx_mode = HDMI_TX_MODE;
    it6161->mipi_rx_lane_count = MIPI_RX_LANE_COUNT;
}

#ifdef __linux__

static int it6161_get_modes(struct drm_connector *connector)
{
 	struct it6161 *it6161 = connector_to_it6161(connector);
 	int err, num_modes = 0, i, retry = 3;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	it6161_debug("%s start", __func__);

	if (it6161->edid)
		return drm_add_edid_modes(connector, it6161->edid);
	mutex_lock(&it6161->mode_lock);
	reinit_completion(&it6161->wait_edid_complete);

	for (i = 0; i < retry; i++) {
		it6161->edid =
		drm_do_get_edid(&it6161->connector, it6161_get_edid_block, it6161);

		if (it6161->edid)
			break;
	}
	if (!it6161->edid) {
		DRM_DEV_ERROR(dev, "Failed to read EDID");
		goto unlock;
	}

	err = drm_connector_update_edid_property(connector, it6161->edid);
	if (err) {
		DRM_DEV_ERROR(dev, "Failed to update EDID property: %d", err);
		goto unlock;
	}

	num_modes = drm_add_edid_modes(connector, it6161->edid);

unlock:
	complete(&it6161->wait_edid_complete);
	DRM_INFO("edid mode number:%d", num_modes);
	mutex_unlock(&it6161->mode_lock);

	return num_modes;
}

 static const struct drm_connector_helper_funcs it6161_connector_helper_funcs = {
 	.get_modes = it6161_get_modes,
 };

static enum drm_connector_status it6161_detect(struct drm_connector *connector,
					       bool force)
{
	struct it6161 *it6161 = connector_to_it6161(connector);
	enum drm_connector_status status = connector_status_disconnected;
	bool hpd = hdmi_tx_get_sink_hpd(it6161);

	DRM_INFO("hpd:%s", hpd ? "high" : "low");
	//mutex_lock(&it6161->mode_lock);

	if (hpd) {
		it6161_variable_config(it6161);
		status = connector_status_connected;
	}

	it6161_set_interrupts_active_level(HIGH);
	it6161_mipi_rx_int_mask_enable(it6161);
	it6161_hdmi_tx_int_mask_enable(it6161);

	//mutex_lock(&it6161->mode_lock);
	return status;
}

static const struct drm_connector_funcs it6161_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = it6161_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int it6161_attach_dsi(struct it6161 *it6161)
{
    struct mipi_dsi_host *host;
    struct mipi_dsi_device *dsi;
    int ret = 0;
    const struct mipi_dsi_device_info info = { .type = "it6161",
                         };

    host = of_find_mipi_dsi_host_by_node(it6161->host_node);
    if (!host) {
        DRM_INFO("it6161 failed to find dsi host\n");
        return -EPROBE_DEFER;
    }

    dsi = mipi_dsi_device_register_full(host, &info);
    if (IS_ERR(dsi)) {
        DRM_INFO("it6161 failed to create dsi device\n");
        ret = PTR_ERR(dsi);
        goto err_dsi_device;
    }

    it6161->dsi = dsi;

    dsi->lanes = 4;
    dsi->format = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
              MIPI_DSI_MODE_EOT_PACKET | MIPI_DSI_MODE_VIDEO_HSE;

    ret = mipi_dsi_attach(dsi);
    if (ret < 0) {
        DRM_INFO("it6161 failed to attach dsi to host\n");
        goto err_dsi_attach;
    }

    return 0;

err_dsi_attach:
    mipi_dsi_device_unregister(dsi);
err_dsi_device:
    return ret;
}

static int it6161_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct it6161 *it6161 = bridge_to_it6161(bridge);
	struct device *dev;
	int err;

	dev = &it6161->i2c_mipi_rx->dev;
	if (!bridge->encoder) {
		DRM_DEV_ERROR(dev, "Parent encoder object not found");
		return -ENODEV;
	}

	err = drm_connector_init(bridge->dev, &it6161->connector,
				 &it6161_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (err < 0) {
		DRM_DEV_ERROR(dev, "Failed to initialize connector: %d", err);
		return err;
	}

	drm_connector_helper_add(&it6161->connector,
				 &it6161_connector_helper_funcs);

	it6161->connector.polled = DRM_CONNECTOR_POLL_HPD;

	err = drm_connector_attach_encoder(&it6161->connector, bridge->encoder);
	if (err < 0) {
		DRM_DEV_ERROR(dev, "Failed to link up connector to encoder: %d",
			      err);
		goto cleanup_connector;
	}

    	DRM_INFO("%s, ret:%d", __func__, it6161_attach_dsi(it6161));

	err = drm_connector_register(&it6161->connector);
	if (err < 0) {
		DRM_DEV_ERROR(dev, "Failed to register connector: %d", err);
		goto cleanup_connector;
	}
	it6161_debug("%s finish", __func__);

	return 0;

// unregister_connector:
// 	drm_connector_unregister(&it6161->connector);
cleanup_connector:
	drm_connector_cleanup(&it6161->connector);
	return err;
}

static void it6161_detach_dsi(struct it6161 *it6161)
{
	mipi_dsi_detach(it6161->dsi);
	mipi_dsi_device_unregister(it6161->dsi);
}

static void it6161_bridge_detach(struct drm_bridge *bridge)
{
	struct it6161 *it6161 = bridge_to_it6161(bridge);

	drm_connector_unregister(&it6161->connector);
	drm_connector_cleanup(&it6161->connector);
	it6161_detach_dsi(it6161);
}

static enum drm_mode_status
it6161_bridge_mode_valid(struct drm_bridge *bridge, 
								const struct drm_display_info *info,
								const struct drm_display_mode *mode)
{
	//struct it6161 *it6161 = bridge_to_it6161(bridge);

	// if (mode->flags & DRM_MODE_FLAG_INTERLACE)
	// 	return MODE_NO_INTERLACE;
	it6161_debug("%s\n", __func__);
	// /* Max 1200p at 5.4 Ghz, one lane */
	// if (mode->clock > MAX_PIXEL_CLK)
	// 	return MODE_CLOCK_HIGH;

	if (mode->clock == 27027)
		return MODE_BAD;

	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;

	//DRM_INFO("%s end", __func__);

	return MODE_OK;
}

// static int it6161_send_video_infoframe(struct it6161 *it6161,
// 				       struct hdmi_avi_infoframe *frame)
// {
// 	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
// 	int err;
// 	struct device *dev = &it6161->i2c_mipi_rx->dev;

// 	err = hdmi_avi_infoframe_pack(frame, buffer, sizeof(buffer));
// 	if (err < 0) {
// 		DRM_DEV_ERROR(dev, "Failed to pack AVI infoframe: %d\n", err);
// 		return err;
// 	}

// 	err = dptx_set_bits(it6161, 0xE8, 0x01, 0x00);
// 	if (err)
// 		return err;

// 	err = regmap_bulk_write(it6161->regmap_mipi_rx, 0xE9,
// 				buffer + HDMI_INFOFRAME_HEADER_SIZE,
// 				frame->length);
// 	if (err)
// 		return err;

// 	err = dptx_set_bits(it6161, 0xE8, 0x01, 0x01);
// 	if (err)
// 		return err;

// 	return 0;
// }

static void it6161_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adjusted_mode)
{
	struct it6161 *it6161 = bridge_to_it6161(bridge);
	struct device *dev = &it6161->i2c_mipi_rx->dev;
	struct drm_display_mode *display_mode = &it6161->source_display_mode;
	int err;

	mutex_lock(&it6161->mode_lock);
	err = drm_hdmi_avi_infoframe_from_display_mode(&it6161->source_avi_infoframe, &it6161->connector,
						       mode);
	if (err) {
		DRM_DEV_ERROR(dev, "Failed to setup AVI infoframe: %d", err);
		goto unlock;
	}

	//it6161->hdmi_tx_display_mode.base.id = adjusted_mode->base.id;
	strlcpy(it6161->hdmi_tx_display_mode.name, adjusted_mode->name,
		DRM_DISPLAY_MODE_LEN);
	it6161->hdmi_tx_display_mode.type = adjusted_mode->type;
	it6161->hdmi_tx_display_mode.flags = adjusted_mode->flags;
	//err = it6161_send_video_infoframe(it6161, &frame);
	strlcpy(display_mode->name, adjusted_mode->name,
		DRM_DISPLAY_MODE_LEN);
	display_mode->clock = mode->clock;
	display_mode->hdisplay = mode->hdisplay;
	display_mode->hsync_start = mode->hsync_start;
	display_mode->hsync_end = mode->hsync_end;
	display_mode->htotal = mode->htotal;
	display_mode->vdisplay = mode->vdisplay;
	display_mode->vsync_start = mode->vsync_start;
	display_mode->vsync_end = mode->vsync_end;
	display_mode->vtotal = mode->vtotal;
	display_mode->flags = mode->flags;
	it6161->vic = it6161->source_avi_infoframe.video_code;
	DRM_INFO("config display mode clk: %d\n", display_mode->clock);
	DRM_INFO("config display mode hdisplay: %d\n", display_mode->hdisplay);
	DRM_INFO("config display mode hsync_start: %d\n", display_mode->hsync_start);
	DRM_INFO("config display mode hsync_end: %d\n", display_mode->hsync_end);
	DRM_INFO("config display mode htotal: %d\n", display_mode->htotal);
	DRM_INFO("config display mode vdisplay: %d\n", display_mode->vdisplay);
	DRM_INFO("config display mode vsync_start: %d\n", display_mode->vsync_start);
	DRM_INFO("config display mode vsync_end: %d\n", display_mode->vsync_end);
	DRM_INFO("config display mode vtotal: %d\n", display_mode->vtotal);

	if (err)
		DRM_DEV_ERROR(dev, "Failed to send AVI infoframe: %d", err);

unlock:
	mutex_unlock(&it6161->mode_lock);
}

static void it6161_bridge_enable(struct drm_bridge *bridge)
{
	struct it6161 *it6161 = bridge_to_it6161(bridge);
	it6161_bridge = bridge;

	it6161_debug("%s start", __func__);
	mipi_rx_init(it6161);//allen
	hdmi_tx_init(it6161);//allen
	it6161_set_interrupts_active_level(HIGH);
	it6161_mipi_rx_int_mask_enable(it6161);
	it6161_hdmi_tx_int_mask_enable(it6161);

	it6161_debug("%s start restart delayed work\n", __func__);
	schedule_delayed_work(&it6161->restart, msecs_to_jiffies(2000));

	//if (enable_de_only)
	//			hdmi_tx_generate_blank_timing(it6161);

	//hdmi_tx_video_reset(it6161);
	// if (!it6161->enable_drv_hold) {
	// 	it6161_int_mask_on(it6161);
	// 	dptx_sys_chg(it6161, SYS_HPD);
	// }
}

static void it6161_bridge_disable(struct drm_bridge *bridge)
{
	struct it6161 *it6161 = bridge_to_it6161(bridge);

	it6161_debug("%s start", __func__);//----TODO
	mipi_rx_logic_reset(it6161);
	hdmi_tx_logic_reset(it6161);
	it6161_set_interrupts_active_level(HIGH);
	it6161_mipi_rx_int_mask_enable(it6161);
	it6161_hdmi_tx_int_mask_enable(it6161);//---
	//kfree(it6161->edid);
	//it6161->edid = NULL;
}

static const struct drm_bridge_funcs it6161_bridge_funcs = {
	.attach = it6161_bridge_attach,
	.detach = it6161_bridge_detach,
	.mode_valid = it6161_bridge_mode_valid,
	.mode_set = it6161_bridge_mode_set,
	.enable = it6161_bridge_enable,// set NULL for old linux version
	.disable = it6161_bridge_disable,
};

#endif

#define InitCEC() it6161_hdmi_tx_write(it6161, 0x8D, (CEC_I2C_SLAVE_ADDR|0x01))//HDMITX_SetI2C_Byte(0x8D, 0x01, 0x01)//HDMITX_SetI2C_Byte(0x0F, 0x08, 0x00)
#define DisableCEC() it6161_hdmi_tx_set_bits(it6161, 0x8D, 0x01, 0x00)//HDMITX_SetI2C_Byte(0x0F, 0x08, 0x08)

static bool it6161_check_device_ready(struct it6161 *it6161)
{
    u8 Vendor_ID[2], Device_ID[2];

    Vendor_ID[0] = it6161_mipi_rx_read(it6161, 0x00);
    Vendor_ID[1] = it6161_mipi_rx_read(it6161, 0x01);
    Device_ID[0] = it6161_mipi_rx_read(it6161, 0x02);
    Device_ID[1] = it6161_mipi_rx_read(it6161, 0x03);
    // Version_ID = MIPIRX_ReadI2C_Byte(0x04);
    if (Vendor_ID[0] == 0x54 && Vendor_ID[1] == 0x49 && Device_ID[0] == 0x61 && Device_ID[1] == 0x61)
    {
        DRM_INFO("Find 6161 revision: 0x%2x", (u32)it6161_mipi_rx_read(it6161, 0x04));
        return true;
    }
    DRM_INFO("find it6161 Fail");
    return false;
}

static u32 hdmi_tx_calc_rclk(struct it6161 *it6161)//in c code: cal_txrclk
{
	// u8 uc ;
	int i ;
	long sum = 0, RCLKCNT, TimeLoMax, retry = 5;

	InitCEC();
	// it6161_hdmi_tx_write(it6161, 0x8D, (CEC_I2C_SLAVE_ADDR|0x01));// Enable CRCLK
	msleep(10);

	for (i = 0; i < retry; i++) {
		// uc = it6161_cec_read(it6161, 0x09) & 0xFE ;
		it6161_cec_write(it6161, 0x09, 1);
		msleep(100);
		it6161_cec_write(it6161, 0x09, 0);
		RCLKCNT = it6161_cec_read(it6161, 0x47);
		RCLKCNT <<= 8 ;
		RCLKCNT |= it6161_cec_read(it6161, 0x46);
		RCLKCNT <<= 8 ;
		RCLKCNT |= it6161_cec_read(it6161, 0x45);
		// DRM_INFO1(("RCLK = %d\n",RCLKCNT) );
		sum += RCLKCNT ;
	}
	sum /= retry;
	RCLKCNT = sum/1000;
	it6161_cec_write(it6161, 0x0C, (RCLKCNT & 0xFF));

	DisableCEC();

//	it6161->hdmi_tx_rclk = (sum << 4) / 108;//actually nxp platform msleep(100) is 108ms
	it6161->hdmi_tx_rclk = (sum << 4) / 104;//actually nxp platform msleep(100) is 108ms
	DRM_INFO("hdmi tx rclk = %d.%dMHz", it6161->hdmi_tx_rclk / 1000, it6161->hdmi_tx_rclk % 1000);

	TimeLoMax = (sum << 4)/10;//10*TxRCLK;

     // add HDCP TimeLomax over-flow protection
     if(TimeLoMax>0x3FFFF)
         TimeLoMax = 0x3FFFF;

     DRM_INFO("TimeLoMax=%08lx\n", TimeLoMax);
     it6161_hdmi_tx_write(it6161, 0x47, (TimeLoMax&0xFF));
     it6161_hdmi_tx_write(it6161, 0x48, ((TimeLoMax&0xFF00)>>8));
     it6161_hdmi_tx_set_bits(it6161, 0x49, 0x03, ((TimeLoMax&0x30000)>>16));

     return it6161->hdmi_tx_rclk;
}

static u32 hdmi_tx_calc_pclk(struct it6161 *it6161) //c code void cal_txclk( void )
{
	u8 uc, RCLKFreqSelRead;
	int div, i;
	u32 sum , count;

	it6161_hdmi_tx_change_bank(it6161, 0);
	// uc = it6161_hdmi_tx_read(it6161, 0x5F) & 0x80 ;

	// if( ! uc )
	// {
	    // return 0 ;
	// }
	RCLKFreqSelRead = (it6161_hdmi_tx_read(it6161, 0x5D) & 0x04)>>2;//hdmitxset(0x5D, 0x04, (RCLKFreqSel<<2));
	/* PCLK Count Pre-Test */
	it6161_hdmi_tx_set_bits(it6161, 0xD7, 0xF0, 0x80);
	msleep(1);
	it6161_hdmi_tx_set_bits(it6161, 0xD7, 0x80, 0x00);

	count = it6161_hdmi_tx_read(it6161, 0xD7) & 0xF ;
	count <<= 8 ;
	count |= it6161_hdmi_tx_read(it6161, 0xD8);

	if (RCLKFreqSelRead)
		count <<= 1;

	for ( div = 7 ; div > 0 ; div-- ) {
		if (count < (1<<(11-div)))
			break ;
	}

	if (div < 0)
	{
		div = 0;
	}

	it6161_hdmi_tx_set_bits(it6161, 0xD7, 0x70, div<<4);

	uc = it6161_hdmi_tx_read(it6161, 0xD7) & 0x7F ;
	for( i = 0 , sum = 0 ; i < 100 ; i ++ )
	{
		it6161_hdmi_tx_write(it6161, 0xD7, uc|0x80) ;
		msleep(1);
		it6161_hdmi_tx_write(it6161, 0xD7, uc) ;

		count = it6161_hdmi_tx_read(it6161, 0xD7) & 0xF ;
		count <<= 8 ;
		count |= it6161_hdmi_tx_read(it6161, 0xD8);
		if (RCLKFreqSelRead)
		{
			count <<= 1;
		}
		sum += count;
	}
	sum /= 100;
	count = sum;

	it6161->hdmi_tx_pclk = it6161->hdmi_tx_rclk * 128 / count * 16 ;//128*16=2048
	it6161->hdmi_tx_pclk *= (1<<div);

	// if( it6161_hdmi_tx_read(it6161, 0x70) & 0x10 )
	// {
		// it6161->hdmi_tx_pclk /= 2 ;
	// }

	DRM_INFO("hdmi tx pclk = %d.%dMHz", it6161->hdmi_tx_pclk / 1000, it6161->hdmi_tx_pclk % 1000);
	return it6161->hdmi_tx_pclk;
}

static void hdmi_tx_get_display_mode(struct it6161 *it6161)
{
	struct drm_display_mode *display_mode = &it6161->hdmi_tx_display_mode;
	u32 hsyncpol, vsyncpol, interlaced;
	u32 htotal, hdes, hdee, hsyncw, hactive, hfront_porch, H2ndVRRise;
	u32 vtotal, vdes, vdee, vsyncw, vactive, vfront_porch, vdes2nd, vdee2nd, vsyncw2nd, VRS2nd;
	u32 vdew2nd, vfph2nd, vbph2nd;
	u8 rega9;

	hdmi_tx_calc_rclk(it6161);
	hdmi_tx_calc_pclk(it6161);

	/* enable video timing read back */
	it6161_hdmi_tx_set_bits(it6161, 0xA8, 0x08, 0x08);

	rega9 = it6161_hdmi_tx_read(it6161, 0xa9);

	hsyncpol = rega9 & 0x01;
	vsyncpol = (rega9 & 0x02) >> 1;
	interlaced = (rega9 & 0x04) >> 2;

	htotal = hdmi_tx_read_word(it6161, 0x98) & 0x0FFF;
	hdes = hdmi_tx_read_word(it6161, 0x90) & 0x0FFF;
	hdee = hdmi_tx_read_word(it6161, 0x92) & 0x0FFF;
	hsyncw = hdmi_tx_read_word(it6161, 0x94) & 0x0FFF;
	hactive = hdee - hdes;
	hfront_porch = htotal - hdee;

	vtotal = hdmi_tx_read_word(it6161, 0xA6) & 0x0FFF;
	vdes = hdmi_tx_read_word(it6161, 0x9C) & 0x0FFF;
	vdee = hdmi_tx_read_word(it6161, 0x9E) & 0x0FFF;
	vsyncw = it6161_hdmi_tx_read(it6161, 0xA0);
	vactive = vdee - vdes;
	vfront_porch = (interlaced == 0x01) ? (vtotal / 2 - vdee) : (vtotal - vdee);

	display_mode->clock = it6161->hdmi_tx_pclk;
	display_mode->hdisplay = hactive;
	display_mode->hsync_start = hactive + hfront_porch;
	display_mode->hsync_end = hactive + hfront_porch + hsyncw;
	display_mode->htotal = htotal;
	display_mode->vdisplay = vactive;
	display_mode->vsync_start = vactive + vfront_porch;
	display_mode->vsync_end = vactive + vfront_porch + vsyncw;
	display_mode->vtotal = vtotal;
	display_mode->flags = ((hsyncpol == 0x01) ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC) |
			      ((vsyncpol == 0x01) ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC) |
			      ((interlaced == 0x01) ? DRM_MODE_FLAG_INTERLACE : 0x00);

	if (interlaced) {
		vdes2nd = hdmi_tx_read_word(it6161, 0xA2) & 0x0FFF;
		vdee2nd = hdmi_tx_read_word(it6161, 0xA4) & 0x0FFF;
		VRS2nd = hdmi_tx_read_word(it6161, 0xB1) & 0x0FFF;
		vsyncw2nd = it6161_hdmi_tx_read(it6161, 0xA1);
		H2ndVRRise = hdmi_tx_read_word(it6161, 0x96) & 0x0FFF;
		vdew2nd = vdee2nd-vdes2nd;
		vfph2nd = VRS2nd-vdee;
		vbph2nd = vdes2nd-VRS2nd-vsyncw2nd;
		DRM_INFO("vdew2nd    = %d\n", vdew2nd);
		DRM_INFO("vfph2nd    = %d\n", vfph2nd);
		DRM_INFO("VSyncW2nd  = %d\n", vsyncw2nd);
		DRM_INFO("vbph2nd    = %d\n", vbph2nd);
		DRM_INFO("H2ndVRRise = %d\n", H2ndVRRise);
	}

	/* disable video timing read back */
	it6161_hdmi_tx_set_bits(it6161, 0xA8, 0x08, 0x00);
}

static void it6161_hdmi_tx_set_av_mute(struct it6161 *it6161, u8 bEnable)
{
    it6161_hdmi_tx_change_bank(it6161, 0);
    it6161_hdmi_tx_set_bits(it6161, REG_TX_GCP,B_TX_SETAVMUTE, bEnable?B_TX_SETAVMUTE:0 );
    it6161_hdmi_tx_write(it6161, REG_TX_PKT_GENERAL_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT);
}

#ifdef HDCP
#ifdef SUPPORT_SHA
static u8 SHABuff[64] ;
static u8 V[20] ;
static u8 KSVList[32] ;
static u8 Vr[20] ;
static u8 M0[8] ;
#endif

static bool hdmi_tx_hdcp_auth_status(struct it6161 *it6161)
{
	return !!(it6161_hdmi_tx_read(it6161, REG_TX_AUTH_STAT) & B_TX_AUTH_DONE);
}

static bool hdmi_tx_hdcp_get_auth_done(struct it6161 *it6161)
{
	return hdmiTxDev[0].bAuthenticated;
}

static void hdmi_tx_hdcp_clear_auth_interrupt(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, REG_TX_INT_MASK2, B_TX_KSVLISTCHK_MASK | B_TX_AUTH_DONE_MASK | B_TX_AUTH_FAIL_MASK, 0x00);
	it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR0, B_TX_CLR_AUTH_FAIL | B_TX_CLR_AUTH_DONE | B_TX_CLR_KSVLISTCHK);
	it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR1, 0x00);
	it6161_hdmi_tx_write(it6161, REG_TX_SYS_STATUS, B_TX_INTACTDONE);
}

static void hdmi_tx_hdcp_reset_auth(struct it6161 *it6161)
{
	it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL, 0x00);
	it6161_hdmi_tx_write(it6161, REG_TX_HDCP_DESIRE, 0x00);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_TX_HDCP_RST_HDMITX, B_TX_HDCP_RST_HDMITX);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERDDC | B_TX_MASTERHOST);
	it6161_hdmi_tx_clear_ddc_fifo(it6161);
	it6161_hdmi_tx_abort_ddc(it6161);
}

/* write anything to reg21 to enable HDCP authentication by HW */

static void hdmi_tx_hdcp_auth_fire(struct it6161 *it6161)
{
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERDDC | B_TX_MASTERHDCP); // MASTERHDCP,no need command but fire.
	it6161_hdmi_tx_write(it6161, REG_TX_AUTHFIRE, 0x01);
}

/*
 * Start the Cipher to free run for random number. When stop
 * An is ready in Reg30
 */

static void hdmi_tx_hdcp_start_an_cipher(struct it6161 *it6161)
{
	it6161_hdmi_tx_write(it6161, REG_TX_AN_GENERATE, B_TX_START_CIPHER_GEN);
}

/* Stop the Cipher,and An is ready in Reg30 */

static void hdmi_tx_hdcp_stop_an_cipher(struct it6161 *it6161)
{
	it6161_hdmi_tx_write(it6161, REG_TX_AN_GENERATE, B_TX_STOP_CIPHER_GEN);
}

/*
 * start An ciper random run at first,then stop it. Software can get
 * An in reg30~reg38,the write to reg28~2F
 */

static void hdmi_tx_hdcp_generate_an(struct it6161 *it6161)
{
	u8 an[DRM_HDCP_AN_LEN], i;

	hdmi_tx_hdcp_start_an_cipher(it6161);
	usleep_range(2000, 3000);
	hdmi_tx_hdcp_stop_an_cipher(it6161);
	it6161_hdmi_tx_change_bank(it6161, 0);
	/* new An is ready in reg30 */
	it6161_hdmi_tx_burst_read(it6161, REG_TX_AN_GEN, an, DRM_HDCP_AN_LEN);

	for (i = 0; i < DRM_HDCP_AN_LEN; i++)
		it6161_hdmi_tx_write(it6161, REG_TX_AN + i, an[i]);
}

/*
 * Parameter: pBCaps - pointer of byte to get BCaps.
 *            pBStatus - pointer of two bytes to get BStatus
 * Return: ER_SUCCESS if successfully got BCaps and BStatus.
 * Remark: get B status and capability from HDCP reciever via DDC bus.
 */

static SYS_STATUS hdmi_tx_get_hdcp_bcaps_bstatus(struct it6161 *it6161, u8 *pBCaps, u16 *pBStatus)
{
	int ucdata;

	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_HEADER,DDC_HDCP_ADDRESS);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQOFF,0x40); // BCaps offset
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQCOUNT,3);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD,CMD_DDC_SEQ_BURSTREAD);

	ucdata = it6161_ddc_wait(it6161);

	if (ucdata < 0) {
		DRM_INFO("get bcaps/bstatus failed");
		return ER_FAIL;
	}

#if 1
	ucdata = it6161_hdmi_tx_read(it6161, REG_TX_BSTAT + 1);
	*pBStatus = (u16)ucdata;
	*pBStatus <<= 8;
	ucdata = it6161_hdmi_tx_read(it6161, REG_TX_BSTAT);
	*pBStatus |= ((u16)ucdata & 0xFF);
	*pBCaps = it6161_hdmi_tx_read(it6161, REG_TX_BCAP);
#else
    *pBCaps = it6161_hdmi_tx_read(it6161, 0x17);
    *pBStatus = it6161_hdmi_tx_read(it6161, 0x17) & 0xFF ;
    *pBStatus |= (int)(it6161_hdmi_tx_read(it6161, 0x17)&0xFF)<<8;
    DRM_INFO("hdmi_tx_get_hdcp_bcaps_bstatus(): ucdata = %02X\n",(int)it6161_hdmi_tx_read(it6161, 0x16));
#endif
#ifdef _SUPPORT_HDCP_REPEATER_
	TxBstatus = *pBStatus;
#endif
	return ER_SUCCESS;
}

/* Get bksv from HDCP sink */

static int hdmi_tx_hdcp_get_bksv(struct it6161 *it6161, u8 *bksv, size_t size)
{
	int ret;
#ifdef _SUPPORT_HDMI_REPEATER_
	int timeout;
#endif

	hdmi_tx_ddc_operation(it6161, DDC_HDCP_ADDRESS, DRM_HDCP_DDC_BKSV, size, 0x00, CMD_DDC_SEQ_BURSTREAD);

	ret = it6161_ddc_wait(it6161);

	if (ret < 0) {
		DRM_INFO("ddc get bksv failed");
		return ret;
	}

	ret = it6161_hdmi_tx_burst_read(it6161, REG_TX_BKSV, bksv, size);

	if (ret < 0)
		DRM_INFO("i2c get bksv failed");

	return ret;

#ifdef _SUPPORT_HDMI_REPEATER_
	for (timeout = 0; timeout < 5; timeout++)
		KSVList[timeout] = *(bksv+timeout);
#endif
}

static u8 countbit(u8 b)
{
	u8 i, count ;

	for (i = 0, count = 0; i < 8; i++) {
		if (b & (1 << i))
			count++;
	}

	return count;
}

static void hdmitx_hdcp_CancelRepeaterAuthenticate(struct it6161 *it6161)
{
	it6161_debug("hdmitx_hdcp_CancelRepeaterAuthenticate");
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERDDC | B_TX_MASTERHOST);
	it6161_hdmi_tx_abort_ddc(it6161);
	it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL, B_TX_LISTFAIL | B_TX_LISTDONE);
	it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL, 0x00);
	hdmi_tx_hdcp_clear_auth_interrupt(it6161);
}

static void hdmitx_hdcp_ResumeRepeaterAuthenticate(struct it6161 *it6161)
{
	it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL, B_TX_LISTDONE);
	it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL, 0x00);
	it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERHDCP);
}

#define WCOUNT 17
u32    VH[5];
u32    w[WCOUNT];

#define rol(x,y)(((x)<< (y))| (((u32)x)>> (32-y)))

static void SHATransform(u32 * h)
{
    int t;
    u32 tmp;

    h[0]=0x67452301;
    h[1]=0xefcdab89;
    h[2]=0x98badcfe;
    h[3]=0x10325476;
    h[4]=0xc3d2e1f0;

    for (t=0; t < 20; t++){
        if(t>=16)
        {
            tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
            w[(t)% WCOUNT]=rol(tmp,1);
        }
        DRM_INFO("w[%d]=%08X\n",t,w[(t)% WCOUNT]);

        tmp=rol(h[0],5)+ ((h[1] & h[2])| (h[3] & ~h[1]))+ h[4] + w[(t)% WCOUNT] + 0x5a827999;
        DRM_INFO("%08X %08X %08X %08X %08X\n",h[0],h[1],h[2],h[3],h[4]);

        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;

    }
    for (t=20; t < 40; t++){
        tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
        w[(t)% WCOUNT]=rol(tmp,1);
        DRM_INFO("w[%d]=%08X\n",t,w[(t)% WCOUNT]);
        tmp=rol(h[0],5)+ (h[1] ^ h[2] ^ h[3])+ h[4] + w[(t)% WCOUNT] + 0x6ed9eba1;
        DRM_INFO("%08X %08X %08X %08X %08X\n",h[0],h[1],h[2],h[3],h[4]);
        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;
    }
    for (t=40; t < 60; t++){
        tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
        w[(t)% WCOUNT]=rol(tmp,1);
        DRM_INFO("w[%d]=%08X\n",t,w[(t)% WCOUNT]);
        tmp=rol(h[0],5)+ ((h[1] & h[2])| (h[1] & h[3])| (h[2] & h[3]))+ h[4] + w[(t)% WCOUNT] + 0x8f1bbcdc;
        DRM_INFO("%08X %08X %08X %08X %08X\n",h[0],h[1],h[2],h[3],h[4]);
        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;
    }
    for (t=60; t < 80; t++)
    {
        tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
        w[(t)% WCOUNT]=rol(tmp,1);
        DRM_INFO("w[%d]=%08X\n",t,w[(t)% WCOUNT]);
        tmp=rol(h[0],5)+ (h[1] ^ h[2] ^ h[3])+ h[4] + w[(t)% WCOUNT] + 0xca62c1d6;
        DRM_INFO("%08X %08X %08X %08X %08X\n",h[0],h[1],h[2],h[3],h[4]);
        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;
    }
    DRM_INFO("%08X %08X %08X %08X %08X\n",h[0],h[1],h[2],h[3],h[4]);
    h[0] +=0x67452301;
    h[1] +=0xefcdab89;
    h[2] +=0x98badcfe;
    h[3] +=0x10325476;
    h[4] +=0xc3d2e1f0;

    DRM_INFO("%08X %08X %08X %08X %08X\n",h[0],h[1],h[2],h[3],h[4]);
}

static void SHA_Simple(void *p,u32 len,u8 *output)
{
    // SHA_State s;
    u32 i,t;
    u32 c;
    u8 *pBuff=p;

    for(i=0;i < len;i++)
    {
        t=i/4;
        if(i%4==0)
        {
            w[t]=0;
        }
        c=pBuff[i];
        c <<=(3-(i%4))*8;
        w[t] |=c;
        DRM_INFO("pBuff[%d]=%02X,c=%08X,w[%d]=%08X\n",(int)i,(int)pBuff[i],c,(int)t,w[t]);
    }
    t=i/4;
    if(i%4==0)
    {
        w[t]=0;
    }
    //c=0x80 << ((3-i%4)*24);
    c=0x80;
    c <<=((3-i%4)*8);
    w[t]|=c;t++;
    for(; t < 15;t++)
    {
        w[t]=0;
    }
    w[15]=len*8;

    for(i = 0; i < 16; i++)
    {
        DRM_INFO("w[%d] = %08X\n",i,w[i]);
    }
    SHATransform(VH);

    for(i=0;i < 5;i++)
    {
        output[i*4+3]=(u8)((VH[i]>>24)&0xFF);
        output[i*4+2]=(u8)((VH[i]>>16)&0xFF);
        output[i*4+1]=(u8)((VH[i]>>8)&0xFF);
        output[i*4+0]=(u8)(VH[i]&0xFF);
    }
}

#ifdef SUPPORT_SHA

static SYS_STATUS hdmitx_hdcp_CheckSHA(u8 pM0[],u16 BStatus,u8 pKSVList[],int cDownStream,u8 Vr[])
{
    int i,n ;

    for(i = 0 ; i < cDownStream*5 ; i++)
    {
        SHABuff[i] = pKSVList[i] ;
    }
    SHABuff[i++] = BStatus & 0xFF ;
    SHABuff[i++] = (BStatus>>8) & 0xFF ;
    for(n = 0 ; n < 8 ; n++,i++)
    {
        SHABuff[i] = pM0[n] ;
    }
    n = i ;
    // SHABuff[i++] = 0x80 ; // end mask
    for(; i < 64 ; i++)
    {
        SHABuff[i] = 0 ;
    }
    // n = cDownStream * 5 + 2 /* for BStatus */ + 8 /* for M0 */ ;
    // n *= 8 ;
    // SHABuff[62] = (n>>8) & 0xff ;
    // SHABuff[63] = (n>>8) & 0xff ;
/*
    for(i = 0 ; i < 64 ; i++)
    {
        if(i % 16 == 0)
        {
            DRM_INFO("SHA[]: ");
        }
        DRM_INFO(" %02X",SHABuff[i]);
        if((i%16)==15)
        {
            DRM_INFO("\n");
        }
    }
    */
    SHA_Simple(SHABuff,n,V);
    for(i = 0 ; i < 20 ; i++)
    {
        if(V[i] != Vr[i])
        {
            DRM_INFO("V[] =");
            for(i = 0 ; i < 20 ; i++)
            {
                DRM_INFO(" %02X",(int)V[i]);
            }
            DRM_INFO("\nVr[] =");
            for(i = 0 ; i < 20 ; i++)
            {
                DRM_INFO(" %02X",(int)Vr[i]);
            }
            return ER_FAIL ;
        }
    }
    return ER_SUCCESS ;
}

#endif // SUPPORT_SHA

static SYS_STATUS hdmi_tx_hdcp_get_ksv_list(struct it6161 *it6161, u8 *pKSVList,u8 cDownStream)
{
    u8 timeout = 100, ucdata;

    if( cDownStream == 0 )
    {
        return ER_SUCCESS ;
    }
    if( /* cDownStream == 0 || */ pKSVList == NULL)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL, B_TX_MASTERHOST);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_HEADER, 0x74);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQOFF, 0x43);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQCOUNT, cDownStream * 5);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD, CMD_DDC_SEQ_BURSTREAD);

    ucdata = it6161_ddc_wait(it6161);

    if (ucdata < 0)
    {
        return ER_FAIL ;
    }
    DRM_INFO("hdmi_tx_hdcp_get_ksv_list(): KSV");
    for(timeout = 0 ; timeout < cDownStream * 5 ; timeout++)
    {
        pKSVList[timeout] = it6161_hdmi_tx_read(it6161, REG_TX_DDC_READFIFO);
		#ifdef _SUPPORT_HDCP_REPEATER_
		KSVList[timeout] = pKSVList[timeout];
        DRM_INFO(" %x",(int)pKSVList[timeout]);
        DRM_INFO(" %02X",(int)pKSVList[timeout]);
		#endif
    }

    return ER_SUCCESS ;
}

static SYS_STATUS hdmitx_hdcp_GetVr(struct it6161 *it6161, u8 *pVr)
{
    u8 timeout, ucdata;

    if(pVr == NULL)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_MASTER_CTRL,B_TX_MASTERHOST);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_HEADER,0x74);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQOFF,0x20);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_REQCOUNT,20);
    it6161_hdmi_tx_write(it6161, REG_TX_DDC_CMD,CMD_DDC_SEQ_BURSTREAD);

    ucdata = it6161_ddc_wait(it6161);
    if (ucdata < 0)
    {
        DRM_INFO("hdmitx_hdcp_GetVr(): DDC fail by timeout.\n");
        return ER_FAIL ;
    }
    it6161_hdmi_tx_change_bank(it6161, 0);

    for(timeout = 0 ; timeout < 5 ; timeout++)
    {
        it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL ,timeout);
        pVr[timeout*4]  = (u32)it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE1);
        pVr[timeout*4+1] = (u32)it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE2);
        pVr[timeout*4+2] = (u32)it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE3);
        pVr[timeout*4+3] = (u32)it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE4);
//        DRM_INFO("V' = %02X %02X %02X %02X\n",(int)pVr[timeout*4],(int)pVr[timeout*4+1],(int)pVr[timeout*4+2],(int)pVr[timeout*4+3]);
    }
    return ER_SUCCESS ;
}

static SYS_STATUS hdmitx_hdcp_GetM0(struct it6161 *it6161, u8 *pM0)
{
    int i ;

    if(!pM0)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL,5); // read m0[31:0] from reg51~reg54
    pM0[0] = it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE1);
    pM0[1] = it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE2);
    pM0[2] = it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE3);
    pM0[3] = it6161_hdmi_tx_read(it6161, REG_TX_SHA_RD_BYTE4);
    it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL,0); // read m0[39:32] from reg55
    pM0[4] = it6161_hdmi_tx_read(it6161, REG_TX_AKSV_RD_BYTE5);
    it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL,1); // read m0[47:40] from reg55
    pM0[5] = it6161_hdmi_tx_read(it6161, REG_TX_AKSV_RD_BYTE5);
    it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL,2); // read m0[55:48] from reg55
    pM0[6] = it6161_hdmi_tx_read(it6161, REG_TX_AKSV_RD_BYTE5);
    it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL,3); // read m0[63:56] from reg55
    pM0[7] = it6161_hdmi_tx_read(it6161, REG_TX_AKSV_RD_BYTE5);

    DRM_INFO("M[] =");
    for(i = 0 ; i < 8 ; i++)
    {
        DRM_INFO("0x%02x,",(int)pM0[i]);
    }
    DRM_INFO("\n");
    return ER_SUCCESS ;
}

#ifdef _SUPPORT_HDCP_REPEATER_

static void TxHDCP_chg(HDMITX_HDCP_State state)
{
    if( state == hdmiTxDev[0].TxHDCP_State )
    {
        return ;
    }
    DRM_INFO("TxHDCP %d -> %d\n",hdmiTxDev[0].TxHDCP_State,state);
    hdmiTxDev[0].TxHDCP_State = state ;

    switch(state)
    {
    case TxHDCP_Off:
        hdmiTxDev[0].bAuthenticated=false;
        hdmiTxDev[0].usHDCPTimeOut = 0 ;
        hdmi_tx_hdcp_reset_auth(it6161);
        break;

    case TxHDCP_AuthRestart:
        hdmiTxDev[0].bAuthenticated = false ;
        hdmi_tx_hdcp_reset_auth(it6161);
        hdmiTxDev[0].usHDCPTimeOut = 5 ;
        break;

    case TxHDCP_AuthStart:
        hdmi_tx_hdcp_reset_auth(it6161);
        hdmiTxDev[0].bAuthenticated = false ;
        hdmiTxDev[0].usHDCPTimeOut = 80 ;
        break;

    case TxHDCP_Receiver:
        hdmiTxDev[0].usHDCPTimeOut = 250 ; // set the count as the 5000ms/interval
        break;

    case TxHDCP_Repeater:
        hdmiTxDev[0].usHDCPTimeOut = 250 ; // set the count as the 5000ms/interval
        break;

    case TxHDCP_CheckFIFORDY:

        hdmiTxDev[0].usHDCPTimeOut = 300 ; // set the count as the 6000ms/interval
        break;

    case TxHDCP_VerifyRevocationList:
        break;


    case TxHDCP_AuthFail:
        hdmi_tx_hdcp_reset_auth(it6161);
        hdmiTxDev[0].bAuthenticated = false ;
        break;

    case TxHDCP_RepeaterFail:
        hdmitx_hdcp_CancelRepeaterAuthenticate(it6161);
        hdmi_tx_hdcp_reset_auth(it6161);
        hdmiTxDev[0].bAuthenticated = false ;
        break;

    case TxHDCP_RepeaterSuccess:
        hdmitx_hdcp_ResumeRepeaterAuthenticate(it6161);
    case TxHDCP_Authenticated:
        it6161_hdmi_tx_set_av_mute(it6161, false) ;
        hdmiTxDev[0].bAuthenticated = true ;
        break;

    }
}

static void TxHDCP_fsm()
{
    u8 ucdata ;
    int i ;

    static u8 BCaps ;
    static u16 BStatus ;
    static u8 cDownStream ;// this value will be use in the function....
    static u8 bksv[5] ;


    switch(hdmiTxDev[0].TxHDCP_State)
    {
    case TxHDCP_Off:
        break;

    case TxHDCP_AuthRestart:
        if(hdmiTxDev[0].usHDCPTimeOut>0)
        {
            hdmiTxDev[0].usHDCPTimeOut -- ;
        }
        if( hdmiTxDev[0].usHDCPTimeOut == 0 )
        {
            TxHDCP_chg(TxHDCP_AuthStart) ;
        }
        break;

    case TxHDCP_AuthStart:
        cDownStream = 0 ;
        it6161_hdmi_tx_change_bank(it6161, 0);
        if(hdmiTxDev[0].usHDCPTimeOut>0)
        {
            hdmiTxDev[0].usHDCPTimeOut -- ;
            ucdata = it6161_hdmi_tx_read(it6161, REG_TX_SYS_STATUS)& (B_TX_HPDETECT|B_TX_RXSENDETECT);

        	if(ucdata != (B_TX_HPDETECT|B_TX_RXSENDETECT))
        	{
        	    // if no Rx sense, do not start authentication.
        	    // Eventhough start it, cannot work.
        	    return ;
        	}

            if(hdmi_tx_get_hdcp_bcaps_bstatus(it6161, &BCaps,&BStatus) != ER_SUCCESS)
            {
                DRM_INFO("hdmi_tx_get_hdcp_bcaps_bstatus fail.\n");
                return;
            }
            // wait for HDMI State

            if(B_TX_HDMI_MODE == (it6161_hdmi_tx_read(it6161, REG_TX_HDMI_MODE) & B_TX_HDMI_MODE ))
            {
                if((BStatus & B_TX_CAP_HDMI_MODE)!=B_TX_CAP_HDMI_MODE)
                {
                    return;
                }
            }
            else
            {
                if((BStatus & B_TX_CAP_HDMI_MODE)==B_TX_CAP_HDMI_MODE)
                {
                    return ;
                }
            }

        }
        else
        {
            TxHDCP_chg(TxHDCP_AuthRestart) ;
        }

    	DRM_INFO("BCAPS = %x BSTATUS = %X\n", (int)BCaps, BStatus);
        hdmi_tx_hdcp_get_bksv(it6161, bksv, ARRAY_SIZE(bksv));
        DRM_INFO("bksv %X %X %X %X %X\n",(int)bksv[0],(int)bksv[1],(int)bksv[2],(int)bksv[3],(int)bksv[4]);

        for(i = 0, ucdata = 0 ; i < 5 ; i ++)
        {
            ucdata += countbit(bksv[i]);
        }
        if( ucdata != 20 )
        {
            DRM_INFO("countbit error\n");
            TxHDCP_chg(TxHDCP_AuthFail) ;
            return ;

        }

        it6161_hdmi_tx_change_bank(it6161, 0); // switch bank action should start on direct register writting of each function.

        it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_TX_HDCP_RST_HDMITX, 0x00);

        it6161_hdmi_tx_write(it6161, REG_TX_HDCP_DESIRE,8|B_TX_CPDESIRE);
        hdmi_tx_hdcp_clear_auth_interrupt(it6161);

        hdmi_tx_hdcp_generate_an(it6161);
        it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL,0);
        hdmiTxDev[0].bAuthenticated = false ;

        it6161_hdmi_tx_clear_ddc_fifo(it6161);
        hdmi_tx_hdcp_auth_fire(it6161);
        hdmiTxDev[0].Tx_BStatus = BStatus ;
        if(BCaps & B_TX_CAP_HDMI_REPEATER)
        {
            TxHDCP_chg(TxHDCP_Repeater) ;
        }
        else
        {

            for(i = 0; i < 5 ; i ++)
            {
                KSVList[i] = bksv[i] ;
            }
            TxHDCP_chg(TxHDCP_Receiver) ;
        }
        break;

    case TxHDCP_Receiver:

        if(hdmiTxDev[0].usHDCPTimeOut >0)
        {
            hdmiTxDev[0].usHDCPTimeOut -- ;
        }

        if(hdmiTxDev[0].usHDCPTimeOut==0)
        {
            TxHDCP_chg(TxHDCP_AuthFail) ;
            return ;
        }
        else
        {
            DRM_INFO("[Fsm] Receiver: usHDCPTimeOut = %d\n",hdmiTxDev[0].usHDCPTimeOut);
            ucdata = it6161_hdmi_tx_read(it6161, REG_TX_AUTH_STAT);
            DRM_INFO("[Fsm] Receiver: ucdata = %X, BStatus = %X\n",ucdata,BStatus);

            if(ucdata & B_TX_AUTH_DONE)
            {
                //BStatus += 0x101 ;
            	IT680X_DownStream_AuthDoneCallback(bksv, BStatus);
                TxHDCP_chg(TxHDCP_Authenticated) ;
                break ;
            }
        }
        break;

    case TxHDCP_Repeater:
        if(hdmiTxDev[0].usHDCPTimeOut >0)
        {
            hdmiTxDev[0].usHDCPTimeOut -- ;
        }

        if(hdmiTxDev[0].usHDCPTimeOut==0)
        {
            TxHDCP_chg(TxHDCP_AuthFail) ;
            return ;
        }
        break;

    case TxHDCP_CheckFIFORDY:
        if(hdmiTxDev[0].usHDCPTimeOut >0)
        {
            hdmiTxDev[0].usHDCPTimeOut -- ;
        }

        if(hdmiTxDev[0].usHDCPTimeOut==0)
        {
            TxHDCP_chg(TxHDCP_RepeaterFail) ;
            return ;
        }

        if(hdmi_tx_get_hdcp_bcaps_bstatus(it6161, &BCaps,&BStatus) == ER_FAIL)
        {
            DRM_INFO("Get BCaps fail\n");
            break ;  // get fail, again.
        }


        if(BCaps & B_TX_CAP_KSV_FIFO_RDY)
        {
            DRM_INFO("FIFO Ready\n");


            it6161_hdmi_tx_clear_ddc_fifo(it6161);
            it6161_hdmi_tx_generate_ddc_sclk(it6161);
            hdmiTxDev[0].Tx_BStatus = BStatus ;
            cDownStream=(BStatus & M_TX_DOWNSTREAM_COUNT);
            //+++++++++++++++++++++++++++++++++++++
            DRM_INFO("Downstream=%X \n",cDownStream);

            if( cDownStream > (MAX_REPEATER_DOWNSTREAM_COUNT-1))
            {
                hdmiTxDev[0].Tx_BStatus |= B_TX_DOWNSTREAM_OVER ;
            }
            if( cDownStream > (MAX_REPEATER_DOWNSTREAM_COUNT-1) ||
                BStatus & (B_TX_MAX_CASCADE_EXCEEDED|B_TX_DOWNSTREAM_OVER))
            {
                DRM_INFO("Invalid Down stream count,fail\n");

                // RxAuthSetBStatus(B_DOWNSTREAM_OVER|B_MAX_CASCADE_EXCEEDED);    //for ALLION HDCP 3C-2-06
                // ForceKSVFIFOReady(B_DOWNSTREAM_OVER|B_MAX_CASCADE_EXCEEDED) ;
                IT680X_DownStream_AuthDoneCallback(KSVList, 0xFFF);
                TxHDCP_chg(TxHDCP_RepeaterFail);
                break;
            }

             TxHDCP_chg(TxHDCP_VerifyRevocationList) ;
             break ;
        }

        break;

    case TxHDCP_VerifyRevocationList:

    #ifdef SUPPORT_SHA
        DRM_INFO("TxHDCP_VerifyRevocationList: cDownStream = %d",cDownStream);
        if(hdmi_tx_hdcp_get_ksv_list(it6161, KSVList,cDownStream) == ER_FAIL)
        {
        	DRM_INFO("hdmitx_hdcp_Repeater_Fail 2\n");
            TxHDCP_chg(TxHDCP_RepeaterFail);
            break;
        }

        if(hdmitx_hdcp_GetVr(it6161, Vr) == ER_FAIL)
        {
        	DRM_INFO("hdmitx_hdcp_Repeater_Fail 3\n");
            TxHDCP_chg(TxHDCP_RepeaterFail);
            break;
        }
        if(hdmitx_hdcp_GetM0(it6161, M0) == ER_FAIL)
        {
        	DRM_INFO("hdmitx_hdcp_Repeater_Fail 4\n");
            TxHDCP_chg(TxHDCP_RepeaterFail);
            break;
        }
        // do check SHA
        if(hdmitx_hdcp_CheckSHA(M0,BStatus,KSVList,cDownStream,Vr) == ER_FAIL)
        {
        	DRM_INFO("hdmitx_hdcp_Repeater_Fail 5\n");
            TxHDCP_chg(TxHDCP_RepeaterFail);
            break;
        }
    #endif // SUPPORT_SHA
        // checkSHA success, append the bksv to KSV List
        for(i = 0; i < 5 ; i ++)
        {
            KSVList[cDownStream*5+i] = bksv[i] ;
        }

        for( i = 0 ; i < ((cDownStream+1)*5) ; i ++ )
        {
            DRM_INFO("KSVLIST[%d] = %X\n",i,KSVList[i]);
        }

        //BStatus += 0x101 ;
    	IT680X_DownStream_AuthDoneCallback(KSVList, BStatus);

        TxHDCP_chg(TxHDCP_RepeaterSuccess) ;
        break;


    case TxHDCP_Authenticated:
        break;

    case TxHDCP_AuthFail:
        // force revoke the KSVList
        // IT680X_DownStream_AuthDoneCallback(KSVList, 0xFFF);
        TxHDCP_chg(TxHDCP_AuthRestart);
        break;

    case TxHDCP_RepeaterFail:
        TxHDCP_chg(TxHDCP_AuthFail);
        break;

    case TxHDCP_RepeaterSuccess:
        TxHDCP_chg(TxHDCP_Authenticated);
        break;

    }
}

#else

//////////////////////////////////////////////////////////////////////
// Function: hdmi_tx_hdcp_auth_process_Repeater
// Parameter: BCaps and BStatus
// Return: ER_SUCCESS if success,if AUTH_FAIL interrupt status,return fail.
// Remark:
// Side-Effect: as Authentication
//////////////////////////////////////////////////////////////////////
#ifdef HDCP
static SYS_STATUS hdmi_tx_hdcp_auth_process_Repeater(struct it6161 *it6161)
{
    u8 uc ,ii;
    // u8 revoked ;
    // int i ;
    u8 cDownStream ;

    u8 BCaps;
    u16 BStatus ;
    u16 timeout ;

    DRM_INFO("Authentication for repeater\n");
    // emily add for test,abort HDCP
    // 2007/10/01 marked by jj_tseng@chipadvanced.com
    // it6161_hdmi_tx_write(it6161, 0x20,0x00);
    // it6161_hdmi_tx_write(it6161, 0x04,0x01);
    // it6161_hdmi_tx_write(it6161, 0x10,0x01);
    // it6161_hdmi_tx_write(it6161, 0x15,0x0F);
    // msleep(100);
    // it6161_hdmi_tx_write(it6161, 0x04,0x00);
    // it6161_hdmi_tx_write(it6161, 0x10,0x00);
    // it6161_hdmi_tx_write(it6161, 0x20,0x01);
    // msleep(100);
    // test07 = it6161_hdmi_tx_read(it6161, 0x7);
    // test06 = it6161_hdmi_tx_read(it6161, 0x6);
    // test08 = it6161_hdmi_tx_read(it6161, 0x8);
    //~jj_tseng@chipadvanced.com
    // end emily add for test
    //////////////////////////////////////
    // Authenticate Fired
    //////////////////////////////////////

    hdmi_tx_get_hdcp_bcaps_bstatus(it6161, &BCaps,&BStatus);
    msleep(2);
    if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1))
    {
        DRM_INFO("HPD Before Fire Auth\n");
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    hdmi_tx_hdcp_auth_fire(it6161);
    //msleep(550); // emily add for test
    for(ii=0;ii<55;ii++)    //msleep(550); // emily add for test
    {
        if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1))
        {
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        msleep(10);
    }
    for(timeout = /*250*6*/10 ; timeout > 0 ; timeout --)
    {
        DRM_INFO("timeout = %d wait part 1\n",timeout);
        if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1))
        {
            DRM_INFO("HPD at wait part 1\n");
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        uc = it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1);
        if(uc & B_TX_INT_DDC_BUS_HANG)
        {
            DRM_INFO("DDC Bus hang\n");
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        uc = it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT2);

        if(uc & B_TX_INT_AUTH_FAIL)
        {
            /*
            it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR0,B_TX_CLR_AUTH_FAIL);
            it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR1,0);
            it6161_hdmi_tx_write(it6161, REG_TX_SYS_STATUS,B_TX_INTACTDONE);
            it6161_hdmi_tx_write(it6161, REG_TX_SYS_STATUS,0);
            */
            DRM_INFO("hdmi_tx_hdcp_auth_process_Repeater(): B_TX_INT_AUTH_FAIL.\n");
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        // emily add for test
        // test =(it6161_hdmi_tx_read(it6161, 0x7)&0x4)>>2 ;
        if(uc & B_TX_INT_KSVLIST_CHK)
        {
            it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR0,B_TX_CLR_KSVLISTCHK);
            it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR1,0);
            it6161_hdmi_tx_write(it6161, REG_TX_SYS_STATUS,B_TX_INTACTDONE);
            it6161_hdmi_tx_write(it6161, REG_TX_SYS_STATUS,0);
            DRM_INFO("B_TX_INT_KSVLIST_CHK\n");
            break ;
        }
        msleep(5);
    }
    if(timeout == 0)
    {
        DRM_INFO("Time out for wait KSV List checking interrupt\n");
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    ///////////////////////////////////////
    // clear KSVList check interrupt.
    ///////////////////////////////////////

    for(timeout = 500 ; timeout > 0 ; timeout --)
    {
        DRM_INFO("timeout=%d at wait FIFO ready\n",timeout);
        if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1))
        {
            DRM_INFO("HPD at wait FIFO ready\n");
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        if(hdmi_tx_get_hdcp_bcaps_bstatus(it6161, &BCaps,&BStatus) == ER_FAIL)
        {
            DRM_INFO("Get BCaps fail\n");
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        if(BCaps & B_TX_CAP_KSV_FIFO_RDY)
        {
             DRM_INFO("FIFO Ready\n");
             break ;
        }
        msleep(5);

    }
    if(timeout == 0)
    {
        DRM_INFO("Get KSV FIFO ready timeout\n");
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    DRM_INFO("Wait timeout = %d\n",timeout);

    it6161_hdmi_tx_clear_ddc_fifo(it6161);
    it6161_hdmi_tx_generate_ddc_sclk(it6161);
    cDownStream =  (BStatus & M_TX_DOWNSTREAM_COUNT);

    if(/*cDownStream == 0 ||*/ cDownStream > 6 || BStatus & (B_TX_MAX_CASCADE_EXCEEDED|B_TX_DOWNSTREAM_OVER))
    {
        DRM_INFO("Invalid Down stream count,fail\n");
        goto hdmitx_hdcp_Repeater_Fail ;
    }
#ifdef SUPPORT_SHA
    if(hdmi_tx_hdcp_get_ksv_list(it6161, KSVList,cDownStream) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
#if 0
    for(i = 0 ; i < cDownStream ; i++)
    {
        revoked=false ; uc = 0 ;
        for( timeout = 0 ; timeout < 5 ; timeout++ )
        {
            // check bit count
            uc += countbit(KSVList[i*5+timeout]);
        }
        if( uc != 20 ) revoked = true ;

        if(revoked)
        {
            DRM_INFO("KSVFIFO[%d] = %02X %02X %02X %02X %02X is revoked\n",i,(int)KSVList[i*5],(int)KSVList[i*5+1],(int)KSVList[i*5+2],(int)KSVList[i*5+3],(int)KSVList[i*5+4]);
             goto hdmitx_hdcp_Repeater_Fail ;
        }
    }
#endif

    if(hdmitx_hdcp_GetVr(it6161, Vr) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    if(hdmitx_hdcp_GetM0(it6161, M0) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    // do check SHA
    if(hdmitx_hdcp_CheckSHA(M0,BStatus,KSVList,cDownStream,Vr) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&it6161_hdmi_tx_read(it6161, REG_TX_INT_STAT1))
    {
        DRM_INFO("HPD at Final\n");
        goto hdmitx_hdcp_Repeater_Fail ;
    }
#endif // SUPPORT_SHA

    hdmitx_hdcp_ResumeRepeaterAuthenticate(it6161);
    hdmiTxDev[0].bAuthenticated = true ;
    return ER_SUCCESS ;

hdmitx_hdcp_Repeater_Fail:
    hdmitx_hdcp_CancelRepeaterAuthenticate(it6161);
    return ER_FAIL ;
}
#endif
#if 0
static int hdmi_tx_hdcp_get_m0(struct it6161 *it6161, u8 *m0)
{
    int ret, i;

	if(!m0)
		return -ENOMEM;

	/* read m0[31:0] from reg51~reg54 */
	it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL, 5);
	ret = it6161_hdmi_tx_burst_read(it6161, REG_TX_SHA_RD_BYTE1, m0, 4);

	if (ret < 0) {
		DRM_INFO("i2c read m0 failed");
		return ret;
	}

	/* read m0[39 + i * 8 : 32 + i * 8] from reg55 */
	for (i = 0; i < 4; i++) {
		it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL, i);
		m0[4 + i] = it6161_hdmi_tx_read(it6161, REG_TX_AKSV_RD_BYTE5);
	}

	return ret;
}
#endif

static int hdmi_tx_hdcp_get_bcaps(struct it6161 *it6161)
{
	int ret;

	hdmi_tx_ddc_operation(it6161, DDC_HDCP_ADDRESS, DRM_HDCP_DDC_BCAPS, 0x01, 0x00, CMD_DDC_SEQ_BURSTREAD);
	ret = it6161_ddc_wait(it6161);

	if (ret < 0) {
		DRM_INFO("ddc get bcaps failed");
		return ret;
	}

	ret = it6161_hdmi_tx_read(it6161, REG_TX_BCAP);

	if (ret < 0) {
		DRM_INFO("i2c get bcaps failed");
		return ret;
	}

	return ret;
}

static int hdmi_tx_hdcp_get_bstatus(struct it6161 *it6161)
{
	int ret, bstatus;

	hdmi_tx_ddc_operation(it6161, DDC_HDCP_ADDRESS, DRM_HDCP_DDC_BSTATUS, 0x02, 0x00, CMD_DDC_SEQ_BURSTREAD);
	ret = it6161_ddc_wait(it6161);

	if (ret < 0) {
		DRM_INFO("ddc get bstatus failed");
		return ret;
	}

	ret = it6161_hdmi_tx_read(it6161, REG_TX_BSTAT);

	if (ret < 0) {
		DRM_INFO("i2c get bstatus failed");
		return ret;
	}

	bstatus = ret;

	ret = it6161_hdmi_tx_read(it6161, REG_TX_BSTAT + 1);

	if (ret < 0) {
		DRM_INFO("i2c get bstatus failed");
		return ret;
	}

	bstatus |= ret << 8;

	return bstatus;
}

#if 0
static void hdmi_tx_hdcp_show_ksv_list(struct it6161 *it6161, u8 *ksvlist)
{
	u8 i;

	for (i = 0; i < it6161->hdcp_downstream_count; i++, ksvlist += DRM_HDCP_KSV_LEN)
		DRM_INFO("device%d ksv:0x %*ph", i, DRM_HDCP_KSV_LEN, ksvlist);
}

static int hdmi_tx_hdcp_get_ksv_list1(struct it6161 *it6161, u8 *ksvlist, size_t size)
{
	int i, ret;

	hdmi_tx_ddc_operation(it6161, DDC_HDCP_ADDRESS, DRM_HDCP_DDC_KSV_FIFO, size, 0x00, CMD_DDC_SEQ_BURSTREAD);
	ret = it6161_ddc_wait(it6161);

	if (ret < 0) {
		DRM_INFO("ddc get ksv list failed");
		return ret;
	}

	for (i = 0; i < size; i++) {
		ret = it6161_hdmi_tx_read(it6161, REG_TX_DDC_READFIFO);

		if (ret < 0) {
			DRM_INFO("i2c get ksv list failed");
			return ret;
		}

		ksvlist[i] = ret;
	}

	return 0;
}

static int hdmi_tx_hdcp_get_v_prime(struct it6161 *it6161, u8 *v_prime, size_t size)
{
	int i, ret;

	hdmi_tx_ddc_operation(it6161, DDC_HDCP_ADDRESS, DRM_HDCP_DDC_V_PRIME(0), size, 0x00, CMD_DDC_SEQ_BURSTREAD);
	ret = it6161_ddc_wait(it6161);

	if (ret < 0) {
		DRM_INFO("ddc get v prime failed");
		return ret;
	}

	for(i = 0 ; i < DRM_HDCP_V_PRIME_NUM_PARTS ; i++) {
		it6161_hdmi_tx_write(it6161, REG_TX_SHA_SEL ,i);
		ret = it6161_hdmi_tx_burst_read(it6161, REG_TX_SHA_RD_BYTE1, v_prime, DRM_HDCP_V_PRIME_PART_LEN);
		if (ret < 0) {
			DRM_INFO("i2c get v prime failed");
			return ret;
		}

		v_prime += DRM_HDCP_V_PRIME_PART_LEN;
	}

	return 0;
}

static int hdmi_tx_setup_sha1_input(struct it6161 *it6161, u8 *ksvlist, u8 *sha1_input)
{
	u8 i, m0[8], msg_count = 0;

	hdmi_tx_hdcp_get_m0(it6161, m0);

	for (i = 0; i < it6161->hdcp_downstream_count * DRM_HDCP_KSV_LEN; i++)
		sha1_input[i] = ksvlist[i];

	msg_count += i;
	sha1_input[msg_count++] = (u8)it6161->bstatus;
	sha1_input[msg_count++] = (u8)(it6161->bstatus >> 8);
	i = 0;

	while (i < ARRAY_SIZE(m0))
		sha1_input[msg_count++] = m0[i++];

	return msg_count;
}

static int it6161_sha1_digest(struct it6161 *it6161, u8 *sha1_input,
			      unsigned int size, u8 *output_av)
{
	struct shash_desc *desc;
	struct crypto_shash *tfm;
	int err;
	struct device *dev = &it6161->i2c_mipi_rx->dev;

	tfm = crypto_alloc_shash("sha1", 0, 0);
	if (IS_ERR(tfm)) {
		DRM_DEV_ERROR(dev, "crypto_alloc_shash sha1 failed");
		return PTR_ERR(tfm);
	}
	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	err = crypto_shash_digest(desc, sha1_input, size, output_av);
	if (err)
		DRM_DEV_ERROR(dev, "crypto_shash_digest sha1 failed");

	crypto_free_shash(tfm);
	kfree(desc);
	return err;
}

static bool hdmi_tX_hdcp_compare_sha1_v_prime_v(struct it6161 *it6161, u8 *v_array, u8 *v_prime_array)
{
	int i, j;

	u8 (*v)[DRM_HDCP_V_PRIME_PART_LEN] = (u8 (*)[DRM_HDCP_V_PRIME_PART_LEN])v_array;
	u8 (*v_prime)[DRM_HDCP_V_PRIME_PART_LEN] = (u8 (*)[DRM_HDCP_V_PRIME_PART_LEN])v_prime_array;

	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++) {
		for (j = 0; j < DRM_HDCP_V_PRIME_PART_LEN; j++) {
			if (v[i][j] !=
			    v_prime[i][DRM_HDCP_V_PRIME_PART_LEN - 1- j])
				return false;
		}
	}

	return true;
}

static void hdmi_tx_hdcp_auth_part2_process(struct work_struct *work)
{
	struct it6161 *it6161 = container_of(work, struct it6161,
					     wait_hdcp_ksv_list);
	int timeout = 5000, ret, i;
	//u8 bcaps, ksvlist[DRM_HDCP_KSV_LEN * it6161->hdcp_downstream_count], *tmp;
	u8 bcaps, ksvlist[DRM_HDCP_KSV_LEN], *tmp;
	u8 v[DRM_HDCP_V_PRIME_NUM_PARTS][DRM_HDCP_V_PRIME_PART_LEN], v_prime[DRM_HDCP_V_PRIME_NUM_PARTS][DRM_HDCP_V_PRIME_PART_LEN];

	while (timeout > 0) {
		if (!hdmi_tx_get_sink_hpd(it6161))
			return;

		ret = hdmi_tx_hdcp_get_bcaps(it6161);

		if (ret < 0)
			return;

		bcaps = ret;
		if (!!(bcaps & DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY)) {
			DRM_INFO("ksv list fifo ready");
			break;
		}
		usleep_range(1000, 2000);
	}

	if (timeout <= 0) {
		DRM_INFO("wait ksv list ready timeout");
		return;
	}

	ret = hdmi_tx_hdcp_get_ksv_list1(it6161, ksvlist, ARRAY_SIZE(ksvlist));

	if (ret != 0)
		return;

	hdmi_tx_hdcp_show_ksv_list(it6161, ksvlist);
	ret = hdmi_tx_setup_sha1_input(it6161, ksvlist, it6161->sha1_transform_input);
	DRM_INFO("sha1 msg_count :%d \nsha1_input:0x %*ph", ret, ret, it6161->sha1_transform_input);
	ret = it6161_sha1_digest(it6161, it6161->sha1_transform_input, ret, (u8 *)v);
	if (ret != 0)
		return;

	tmp = (u8 *)v;
	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++, tmp += DRM_HDCP_V_PRIME_PART_LEN)
		DRM_INFO("v:0x %*ph", DRM_HDCP_V_PRIME_PART_LEN, tmp);

	ret = hdmi_tx_hdcp_get_v_prime(it6161, (u8 *)v_prime, sizeof(v_prime));

	if (ret != 0)
		return;

	tmp = (u8 *)v_prime;
	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++, tmp += DRM_HDCP_V_PRIME_PART_LEN)
		DRM_INFO("v_prime:0x %*ph", DRM_HDCP_V_PRIME_PART_LEN, tmp);

	ret = hdmi_tX_hdcp_compare_sha1_v_prime_v(it6161, (u8 *)v, (u8 *)v_prime);
	DRM_INFO("sha1 check result: %s", ret ? "pass" : "failed");

	if (ret) {
		it6161_hdmi_tx_set_bits(it6161, REG_TX_LISTCTRL, B_TX_LISTDONE , B_TX_LISTDONE);
	} else {
		it6161_hdmi_tx_set_bits(it6161, REG_TX_LISTCTRL, B_TX_LISTDONE | B_TX_LISTFAIL, B_TX_LISTDONE | B_TX_LISTFAIL);
	}
	it6161_hdmi_tx_set_bits(it6161, REG_TX_LISTCTRL, B_TX_LISTDONE | B_TX_LISTFAIL, 0x00);
}
#endif

static bool hdmi_tx_hdcp_enable_auth_part1(struct it6161 *it6161)
{
	int ret;
	u8 i, count = 0;

	ret = hdmi_tx_hdcp_get_bksv(it6161, it6161->bksv, (int)ARRAY_SIZE(it6161->bksv));
	if (ret < 0)
		return hdmiTxDev[0].bAuthenticated;
	DRM_INFO("bksv: 0x %*ph", (int)ARRAY_SIZE(it6161->bksv), it6161->bksv);

	for (i = 0; i < ARRAY_SIZE(it6161->bksv); i++)
		count += countbit(it6161->bksv[i]);

	if (count != 20) {
		DRM_INFO("not a valid bksv");
		return hdmiTxDev[0].bAuthenticated;
	}

	if ((it6161->bksv[4] == 0x93) &&
		(it6161->bksv[3] == 0x43) &&
		(it6161->bksv[2] == 0x5C) &&
		(it6161->bksv[1] == 0xDE) &&
		(it6161->bksv[0] == 0x23)) {
		DRM_INFO("Revoked bksv");
		return hdmiTxDev[0].bAuthenticated ;
	}

	it6161_hdmi_tx_write(it6161, REG_TX_HDCP_DESIRE, 0x08 | B_TX_CPDESIRE);
	hdmi_tx_hdcp_generate_an(it6161);
	hdmi_tx_hdcp_auth_fire(it6161);

	return true;
}

static bool hdmi_tx_hdcp_auth_process(struct it6161 *it6161)
{
	u8 bcaps;
	u16 i, retry = 3;
	int ret;

	/* Authenticate should be called after AFE setup up */

	DRM_INFO("start");
	hdmiTxDev[0].bAuthenticated = false ;
	it6161->hdmi_tx_hdcp_retry--;
	hdmi_tx_hdcp_reset_auth(it6161);
	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_TX_HDCP_RST_HDMITX, 0x00);
	it6161_hdmi_tx_write(it6161, REG_TX_LISTCTRL, 0x00);
	it6161_hdmi_tx_clear_ddc_fifo(it6161);
	hdmi_tx_hdcp_int_mask_enable(it6161);

	for (i = 0; i < retry; i++) {
		ret = hdmi_tx_hdcp_get_bstatus(it6161);
		if(ret < 0)
			continue;
		it6161->bstatus = (u16)ret;

		ret = hdmi_tx_hdcp_get_bcaps(it6161);
		if (ret < 0)
			continue;
		bcaps = ret;
		break;
	}

	if (i == retry)
		return hdmiTxDev[0].bAuthenticated;

	DRM_INFO("bcaps: 0x%02x, bstatus: 0x%04x, hdmi_mode: %d", bcaps, it6161->bstatus,
		 it6161->bstatus & B_TX_CAP_HDMI_MODE);
	it6161->is_repeater = !!(bcaps & DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT);
	DRM_INFO("downstream is hdcp %s", it6161->is_repeater ? "repeater" : "receiver");
	if (it6161->is_repeater) {
		it6161->hdcp_downstream_count = (u8)DRM_HDCP_NUM_DOWNSTREAM(it6161->bstatus);
		DRM_INFO("down stream Count %d", it6161->hdcp_downstream_count);
		if (it6161->hdcp_downstream_count > 6) {
			DRM_INFO("over maximum supported number 6");
			return hdmiTxDev[0].bAuthenticated;
		}
	}

	ret = hdmi_tx_hdcp_enable_auth_part1(it6161);

	if (!ret)
		return ret;

	if (!it6161->is_repeater) {
		ret = wait_for_completion_timeout(&it6161->wait_hdcp_event, msecs_to_jiffies(1000));
		DRM_INFO("completion:%d", ret);
		if (ret == 0)
			DRM_INFO("hdcp-receiver: timeout");

		return hdmiTxDev[0].bAuthenticated;
	}
	return hdmiTxDev[0].bAuthenticated;
}
#endif

static void hdmitx_hdcp_ResumeAuthentication(struct it6161 *it6161)
{
    it6161_hdmi_tx_set_av_mute(it6161, true);
    if(hdmi_tx_hdcp_auth_process(it6161) == ER_SUCCESS)
    {
    }
    it6161_hdmi_tx_set_av_mute(it6161, false);
}

static void hdmi_tx_hdcp_work(struct work_struct *work)
{
	struct it6161 *it6161 = container_of(work, struct it6161,
					     hdcp_work.work);
	//struct device *dev = &it6161->i2c_hdmi_tx->dev;
	bool ret, sink_hpd = hdmi_tx_get_sink_hpd(it6161), video_state = hdmi_tx_get_video_state(it6161);

	if (it6161->hdmi_tx_hdcp_retry <= 0 || !sink_hpd || !video_state) {
		DRM_INFO("hdcp_retry:%d sink_hpd:%d video_stable_state:%d", it6161->hdmi_tx_hdcp_retry, sink_hpd, video_state);
		return;
	}

	ret = hdmi_tx_hdcp_auth_process(it6161);
	if (it6161->is_repeater) {
		DRM_INFO("is repeater and wait for ksv list interrupt");
		return;
	}

	DRM_INFO("hdcp_auth_process %s", ret ? "pass" : "failed");
}

static void hdmi_tx_enable_hdcp(struct it6161 *it6161)
{
	struct device *dev = &it6161->i2c_hdmi_tx->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "start");
	queue_delayed_work(system_wq, &it6161->hdcp_work,
			   msecs_to_jiffies(500));
}
#endif

static bool getHDMITX_LinkStatus()
{
it6161_debug("%s reg0E:0x%02x reg0x61:0x%02x", __func__, it6161_hdmi_tx_read(it6161, REG_TX_SYS_STATUS), it6161_hdmi_tx_read(it6161, REG_TX_AFE_DRV_CTRL));//allen
    if(B_TX_RXSENDETECT & it6161_hdmi_tx_read(it6161, REG_TX_SYS_STATUS)) {
        if(0==it6161_hdmi_tx_read(it6161, REG_TX_AFE_DRV_CTRL))
        {
            //DRM_INFO("getHDMITX_LinkStatus()!!\n");
            return true;
        }
    }
    DRM_INFO("GetTMDS not Ready()!!\n");

    return false;
}

// static void HDMITX_PowerDown()
// {
//     it6161_hdmi_tx_write_table(it6161, HDMITX_PwrDown_Table, ARRAY_SIZE(HDMITX_PwrDown_Table));
// }

static void hdmi_tx_setup_pclk_div2(struct it6161 *it6161)
{
	if (HDMI_TX_PCLK_DIV2) {
		DRM_INFO("PCLK Divided by 2 mode");
		it6161_hdmi_tx_set_bits(it6161, REG_TX_INPUT_MODE, B_TX_PCLKDIV2, B_TX_PCLKDIV2);
    }
}

//////////////////////////////////////////////////////////////////////
// Function: hdmi_tx_setup_csc
// Parameter: input_mode -
//             D[1:0] - Color Mode
//             D[4] - Colorimetry 0: ITU_BT601 1: ITU_BT709
//             D[5] - Quantization 0: 0_255 1: 16_235
//             D[6] - Up/Dn Filter 'Required'
//                    0: no up/down filter
//                    1: enable up/down filter when csc need.
//             D[7] - Dither Filter 'Required'
//                    0: no dither enabled.
//                    1: enable dither and dither free go "when required".
//            output_mode -
//             D[1:0] - Color mode.
// Return: N/A
// Remark: reg72~reg8D will be programmed depended the input with table.
// Side-Effect:
//////////////////////////////////////////////////////////////////////

static void hdmi_tx_setup_csc(struct it6161 *it6161)
{
    u8 ucData, csc = 0, i, filter = 0; // filter is for Video CTRL DN_FREE_GO,EN_DITHER,and ENUDFILT
    u8 input_mode = it6161->hdmi_tx_input_color_space;
    u8 output_mode = it6161->hdmi_tx_output_color_space;

    // (1) YUV422 in,RGB/YUV444 output (Output is 8-bit,input is 12-bit)
    // (2) YUV444/422  in,RGB output (CSC enable,and output is not YUV422)
    // (3) RGB in,YUV444 output   (CSC enable,and output is not YUV422)
    //
    // YUV444/RGB24 <-> YUV422 need set up/down filter.
    DRM_INFO("hdmi_tx_setup_csc(u8 input_mode = %x,u8 output_mode = %x)\n", (int)input_mode, (int)output_mode);
    switch(input_mode&F_MODE_CLRMOD_MASK)
    {
    #ifdef SUPPORT_INPUTYUV444
    case F_MODE_YUV444:
        DRM_INFO("Input mode is YUV444 ");
        switch(output_mode&F_MODE_CLRMOD_MASK)
        {
        case F_MODE_YUV444:
            DRM_INFO("Output mode is YUV444\n");
            csc = B_HDMITX_CSC_BYPASS ;
            break ;

        case F_MODE_YUV422:
            DRM_INFO("Output mode is YUV422\n");
            if(input_mode & F_VIDMODE_EN_UDFILT) // YUV444 to YUV422 need up/down filter for processing.
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            csc = B_HDMITX_CSC_BYPASS ;
            break ;
        case F_MODE_RGB444:
            DRM_INFO("Output mode is RGB24\n");
            csc = B_HDMITX_CSC_YUV2RGB ;
            if(input_mode & F_VIDMODE_EN_DITHER) // YUV444 to RGB24 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;
        }
        break ;
    #endif

    #ifdef SUPPORT_INPUTYUV422
    case F_MODE_YUV422:
        DRM_INFO("Input mode is YUV422\n");
        switch(output_mode&F_MODE_CLRMOD_MASK)
        {
        case F_MODE_YUV444:
            DRM_INFO("Output mode is YUV444\n");
            csc = B_HDMITX_CSC_BYPASS ;
            if(input_mode & F_VIDMODE_EN_UDFILT) // YUV422 to YUV444 need up filter
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            if(input_mode & F_VIDMODE_EN_DITHER) // YUV422 to YUV444 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;
        case F_MODE_YUV422:
            DRM_INFO("Output mode is YUV422\n");
            csc = B_HDMITX_CSC_BYPASS ;

            break ;

        case F_MODE_RGB444:
            DRM_INFO("Output mode is RGB24\n");
            csc = B_HDMITX_CSC_YUV2RGB ;
            if(input_mode & F_VIDMODE_EN_UDFILT) // YUV422 to RGB24 need up/dn filter.
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            if(input_mode & F_VIDMODE_EN_DITHER) // YUV422 to RGB24 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;
        }
        break ;
    #endif

    #ifdef SUPPORT_INPUTRGB
    case F_MODE_RGB444:
        DRM_INFO("Input mode is RGB24\n");
        switch(output_mode&F_MODE_CLRMOD_MASK)
        {
        case F_MODE_YUV444:
            DRM_INFO("Output mode is YUV444\n");
            csc = B_HDMITX_CSC_RGB2YUV ;

            if(input_mode & F_VIDMODE_EN_DITHER) // RGB24 to YUV444 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;

        case F_MODE_YUV422:
            DRM_INFO("Output mode is YUV422\n");
            if(input_mode & F_VIDMODE_EN_UDFILT) // RGB24 to YUV422 need down filter.
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            if(input_mode & F_VIDMODE_EN_DITHER) // RGB24 to YUV422 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            csc = B_HDMITX_CSC_RGB2YUV ;
            break ;

        case F_MODE_RGB444:
            DRM_INFO("Output mode is RGB24\n");
            csc = B_HDMITX_CSC_BYPASS ;
            break ;
        }
        break ;
    #endif
    }
#ifndef DISABLE_HDMITX_CSC

    #ifdef SUPPORT_INPUTRGB
    // set the CSC metrix registers by colorimetry and quantization
    if(csc == B_HDMITX_CSC_RGB2YUV)
    {
        DRM_INFO("CSC = RGB2YUV %x ",csc);
        switch(input_mode&(F_VIDMODE_ITU709|F_VIDMODE_16_235))
        {
        case F_VIDMODE_ITU709|F_VIDMODE_16_235:
            DRM_INFO("ITU709 16-235 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU709_16_235[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU709_16_235[i]);}
            break ;
        case F_VIDMODE_ITU709|F_VIDMODE_0_255:
            DRM_INFO("ITU709 0-255 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU709_0_255[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU709_0_255[i]);}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_16_235:
            DRM_INFO("ITU601 16-235 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU601_16_235[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU601_16_235[i]);}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_0_255:
        default:
            DRM_INFO("ITU601 0-255 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU601_0_255[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU601_0_255[i]);}
            break ;
        }
    }
    #endif

    #ifdef SUPPORT_INPUTYUV
    if (csc == B_HDMITX_CSC_YUV2RGB)
    {
        DRM_INFO("CSC = YUV2RGB %x ",csc);

        switch(input_mode&(F_VIDMODE_ITU709|F_VIDMODE_16_235))
        {
        case F_VIDMODE_ITU709|F_VIDMODE_16_235:
            DRM_INFO("ITU709 16-235 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU709_16_235[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU709_16_235[i]);}
            break ;
        case F_VIDMODE_ITU709|F_VIDMODE_0_255:
            DRM_INFO("ITU709 0-255 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU709_0_255[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU709_0_255[i]);}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_16_235:
            DRM_INFO("ITU601 16-235 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU601_16_235[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU601_16_235[i]);}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_0_255:
        default:
            DRM_INFO("ITU601 0-255 ");
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ it6161_hdmi_tx_write(it6161, REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU601_0_255[i]) ; DRM_INFO("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU601_0_255[i]);}
            break ;
        }
    }
    #endif
#else// DISABLE_HDMITX_CSC
    csc = B_HDMITX_CSC_BYPASS ;
#endif// DISABLE_HDMITX_CSC

    if( csc == B_HDMITX_CSC_BYPASS )
    {
        it6161_hdmi_tx_set_bits(it6161, 0xF, 0x10, 0x10);
    }
    else
    {
        it6161_hdmi_tx_set_bits(it6161, 0xF, 0x10, 0x00);
    }
    ucData = it6161_hdmi_tx_read(it6161, REG_TX_CSC_CTRL) & ~(M_TX_CSC_SEL|B_TX_DNFREE_GO|B_TX_EN_DITHER|B_TX_EN_UDFILTER);
    ucData |= filter|csc ;

    it6161_hdmi_tx_write(it6161, REG_TX_CSC_CTRL,ucData);
}

// Parameter: VIDEOPCLKLEVEL level
//            PCLK_LOW - for 13.5MHz (for mode less than 1080p)
//            PCLK MEDIUM - for 25MHz~74MHz
//            PCLK HIGH - PCLK > 80Hz (for 1080p mode or above)
// Remark: set reg62~reg65 depended on HighFreqMode
//         reg61 have to be programmed at last and after video stable input.

static void hdmi_tx_setup_afe(struct it6161 *it6161, VIDEOPCLKLEVEL level)
{

    it6161_hdmi_tx_write(it6161, REG_TX_AFE_DRV_CTRL, B_TX_AFE_DRV_RST);
    switch(level)
    {
        case PCLK_HIGH:
            it6161_hdmi_tx_set_bits(it6161, 0x62, 0x90, 0x80);
            it6161_hdmi_tx_set_bits(it6161, 0x64, 0x89, 0x80);
            it6161_hdmi_tx_set_bits(it6161, 0x68, 0x10, 0x00);
            it6161_hdmi_tx_set_bits(it6161, 0x66, 0x80, 0x80);//hdmitxset(0x66, 0x80, 0x80);// mark fix 6017
            break ;
        default:
            it6161_hdmi_tx_set_bits(it6161, 0x62, 0x90, 0x10);
            it6161_hdmi_tx_set_bits(it6161, 0x64, 0x89, 0x09);
            it6161_hdmi_tx_set_bits(it6161, 0x68, 0x10, 0x10);
            break ;
    }
	DRM_INFO("setup afe: %s", level ? "high" : "low");
#ifdef REDUCE_HDMITX_SRC_JITTER
    //it6161_hdmi_tx_set_bits(it6161, 0x64, 0x01, 0x00); //pet: TODO need check
    // it6161_hdmi_tx_set_bits(it6161, 0x6A, 0xFF, 0xFF);

    // 2019/02/15 modified by jjtseng, 
    // Dr. Liu: 
    // it6161 �b�� Solomon mipi TX (�Ψ�Ljitter �ܤj��source��), �Х[�H�U�]�w
    // reg[6A]=0x5D 
    // Note: Register 6A  is REG_XP_TEST[7:0], its eye and jitter CTS report please see attached file 
    // 
    // it6161 �b�� ite mipi TX (�Ψ�Ljitter OK��source��), keep
    // [6A]=0x00 (default setting)
    
    it6161_hdmi_tx_set_bits(it6161, 0x6A, 0xFF, 0x5D);
#endif
}

// Remark: write reg61 with 0x04
//         When program reg61 with 0x04,then audio and video circuit work.

static void hdmi_tx_fire_afe(struct it6161 *it6161)
{
    it6161_hdmi_tx_change_bank(it6161, 0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_AFE_DRV_CTRL, 0x00);
}

//////////////////////////////////////////////////////////////////////
// utility function for main..
//////////////////////////////////////////////////////////////////////

// #ifndef DISABLE_HDMITX_CSC
//     #if (defined (SUPPORT_OUTPUTYUV)) && (defined (SUPPORT_INPUTRGB))
//         extern const u8 bCSCMtx_RGB2YUV_ITU601_16_235[] ;
//         extern const u8 bCSCMtx_RGB2YUV_ITU601_0_255[] ;
//         extern const u8 bCSCMtx_RGB2YUV_ITU709_16_235[] ;
//         extern const u8 bCSCMtx_RGB2YUV_ITU709_0_255[] ;
//     #endif

//     #if (defined (SUPPORT_OUTPUTRGB)) && (defined (SUPPORT_INPUTYUV))
//         extern const u8 bCSCMtx_YUV2RGB_ITU601_16_235[] ;
//         extern const u8 bCSCMtx_YUV2RGB_ITU601_0_255[] ;
//         extern const u8 bCSCMtx_YUV2RGB_ITU709_16_235[] ;
//         extern const u8 bCSCMtx_YUV2RGB_ITU709_0_255[] ;

//     #endif
// #endif// DISABLE_HDMITX_CSC


static void hdmi_tx_disable_video_output(struct it6161 *it6161)
{
	it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_VID_RST, B_HDMITX_VID_RST);
	it6161_hdmi_tx_write(it6161, REG_TX_AFE_DRV_CTRL,B_TX_AFE_DRV_RST|B_TX_AFE_DRV_PWD);
	it6161_hdmi_tx_set_bits(it6161, 0x62, 0x90, 0x00);
	it6161_hdmi_tx_set_bits(it6161, 0x64, 0x89, 0x00);
}

static void hdmi_tx_enable_video_output(struct it6161 *it6161, VIDEOPCLKLEVEL level)
{
	it6161_hdmi_tx_write(it6161, REG_TX_SW_RST, B_HDMITX_AUD_RST | B_TX_AREF_RST | B_TX_HDCP_RST_HDMITX);
	it6161_hdmi_tx_change_bank(it6161, 1);
	it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB1, 0x00);
	it6161_hdmi_tx_change_bank(it6161, 0);

	if(it6161->hdmi_mode)
		it6161_hdmi_tx_set_av_mute(it6161, true);

	hdmi_tx_setup_pclk_div2(it6161);
	hdmi_tx_setup_csc(it6161);
	it6161_hdmi_tx_write(it6161, REG_TX_HDMI_MODE, it6161->hdmi_mode ? B_TX_HDMI_MODE : B_TX_DVI_MODE);
	hdmi_tx_setup_afe(it6161, level);
	hdmi_tx_fire_afe(it6161);
}

static u8 AudioDelayCnt=0;
static u8 LastRefaudfreqnum=0;
static bool bForceCTS = false;

static void setHDMITX_ChStat(struct it6161 *it6161, u8 ucIEC60958ChStat[])
{
    u8 uc ;

    it6161_hdmi_tx_change_bank(it6161, 1);
    uc = (ucIEC60958ChStat[0] <<1)& 0x7C ;
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_MODE,uc);
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_CAT,ucIEC60958ChStat[1]); // 192, audio CATEGORY
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_SRCNUM,ucIEC60958ChStat[2]&0xF);
    it6161_hdmi_tx_write(it6161, REG_TX_AUD0CHST_CHTNUM,(ucIEC60958ChStat[2]>>4)&0xF);
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_CA_FS,ucIEC60958ChStat[3]); // choose clock
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_OFS_WL,ucIEC60958ChStat[4]);
    it6161_hdmi_tx_change_bank(it6161, 0);
}
/*
void setHDMITX_UpdateChStatFs(u32 Fs)
{
    u8 uc ;

    /////////////////////////////////////
    // Fs should be the following value.
    // #define AUDFS_22p05KHz  4
    // #define AUDFS_44p1KHz 0
    // #define AUDFS_88p2KHz 8
    // #define AUDFS_176p4KHz    12
    //
    // #define AUDFS_24KHz  6
    // #define AUDFS_48KHz  2
    // #define AUDFS_96KHz  10
    // #define AUDFS_192KHz 14
    //
    // #define AUDFS_768KHz 9
    //
    // #define AUDFS_32KHz  3
    // #define AUDFS_OTHER    1
    /////////////////////////////////////

    it6161_hdmi_tx_change_bank(it6161, 1);
    uc = it6161_hdmi_tx_read(it6161, REG_TX_AUDCHST_CA_FS); // choose clock
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_CA_FS,uc); // choose clock
    uc &= 0xF0 ;
    uc |= (Fs&0xF);

    uc = it6161_hdmi_tx_read(it6161, REG_TX_AUDCHST_OFS_WL);
    uc &= 0xF ;
    uc |= ((~Fs) << 4)&0xF0 ;
    it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_OFS_WL,uc);

    it6161_hdmi_tx_change_bank(it6161, 0);
}
*/
static void setHDMITX_LPCMAudio(u8 AudioSrcNum, u8 AudSWL, u8 bAudInterface /*I2S/SPDIF/TDM*/)
{

    u8 AudioEnable, AudioFormat ;
    u8 bTDMSetting ;
    AudioEnable = 0 ;
    AudioFormat = hdmiTxDev[0].bOutputAudioMode ;

    switch(AudSWL)
    {
    case 16:
        AudioEnable |= M_TX_AUD_16BIT ;
        break ;
    case 18:
        AudioEnable |= M_TX_AUD_18BIT ;
        break ;
    case 20:
        AudioEnable |= M_TX_AUD_20BIT ;
        break ;
    case 24:
    default:
        AudioEnable |= M_TX_AUD_24BIT ;
        break ;
    }
    if( bAudInterface == SPDIF )
    {
        AudioFormat &= ~0x40 ;
        AudioEnable |= B_TX_AUD_SPDIF|B_TX_AUD_EN_I2S0 ;
    }
    else
    {
        AudioFormat |= 0x40 ;
        switch(AudioSrcNum)
        {
        case 4:
            AudioEnable |= B_TX_AUD_EN_I2S3|B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0 ;
            break ;

        case 3:
            AudioEnable |= B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0 ;
            break ;

        case 2:
            AudioEnable |= B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0 ;
            break ;

        case 1:
        default:
            AudioFormat &= ~0x40 ;
            AudioEnable |= B_TX_AUD_EN_I2S0 ;
            break ;

        }
    }
    AudioFormat|=0x01;//mingchih add
    hdmiTxDev[0].bAudioChannelEnable=AudioEnable;

    it6161_hdmi_tx_change_bank(it6161, 0);
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0,AudioEnable&0xF0);

    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL1,AudioFormat); // regE1 bOutputAudioMode should be loaded from ROM image.
#ifdef USE_IT66120
    it6161_hdmi_tx_set_bits(it6161, 0x5A,0x02, 0x00);
    if( bAudInterface == SPDIF )
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
    }
    else
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xFF); // default mapping.
    }
#else
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
#endif

#ifdef USE_SPDIF_CHSTAT
    if( bAudInterface == SPDIF )
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,B_TX_CHSTSEL);
    }
    else
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,0);
    }
#else // not USE_SPDIF_CHSTAT
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,0);
#endif // USE_SPDIF_CHSTAT

    it6161_hdmi_tx_write(it6161, REG_TX_AUD_SRCVALID_FLAT,0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_AUD_HDAUDIO,0x00); // regE5 = 0 ;

    if( bAudInterface == SPDIF )
    {
        u8 i ;
        it6161_hdmi_tx_set_bits(it6161, 0x5c,(1<<6), (1<<6));
        for( i = 0 ; i < 100 ; i++ )
        {
            if(it6161_hdmi_tx_read(it6161, REG_TX_CLK_STATUS2) & B_TX_OSF_LOCK)
            {
                break ; // stable clock.
            }
        }
    }
    else
    {
        bTDMSetting = it6161_hdmi_tx_read(it6161, REG_TX_AUD_HDAUDIO) ;
        if( bAudInterface == TDM )
        {
            bTDMSetting |= B_TX_TDM ;
            bTDMSetting &= 0x9F ;
            bTDMSetting |= (AudioSrcNum-1)<< 5;
        }
        else
        {
            bTDMSetting &= ~B_TX_TDM ;
        }
        it6161_hdmi_tx_write(it6161, REG_TX_AUD_HDAUDIO, bTDMSetting) ; // 2 channel NLPCM, no TDM mode.
    }
}

static void setHDMITX_NLPCMAudio(u8 bAudInterface /*I2S/SPDIF/TDM*/) // no Source Num, no I2S.
{
    u8 AudioEnable, AudioFormat ;
    u8 i ;

    AudioFormat = 0x01 ; // NLPCM must use standard I2S mode.
    if( bAudInterface == SPDIF )
    {
        AudioEnable = M_TX_AUD_24BIT|B_TX_AUD_SPDIF;
    }
    else
    {
        AudioEnable = M_TX_AUD_24BIT;
    }

    it6161_hdmi_tx_change_bank(it6161, 0);
    // it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF);
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, AudioEnable);
    //it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_AUD_RST|B_TX_AREF_RST, 0x00);

    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL1,0x01); // regE1 bOutputAudioMode should be loaded from ROM image.
#ifdef USE_IT66120
    if( bAudInterface == SPDIF )
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
    }
    else
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xFF); // default mapping.
    }
#else
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
#endif

#ifdef USE_SPDIF_CHSTAT
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,B_TX_CHSTSEL);
#else // not USE_SPDIF_CHSTAT
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,0);
#endif // USE_SPDIF_CHSTAT

    it6161_hdmi_tx_write(it6161, REG_TX_AUD_SRCVALID_FLAT,0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_AUD_HDAUDIO,0x00); // regE5 = 0 ;

    if( bAudInterface == SPDIF )
    {
        for( i = 0 ; i < 100 ; i++ )
        {
            if(it6161_hdmi_tx_read(it6161, REG_TX_CLK_STATUS2) & B_TX_OSF_LOCK)
            {
                break ; // stable clock.
            }
        }
    }
    else
    {
        i = it6161_hdmi_tx_read(it6161, REG_TX_AUD_HDAUDIO) ;
        i &= ~B_TX_TDM ;
        it6161_hdmi_tx_write(it6161, REG_TX_AUD_HDAUDIO, i) ; // 2 channel NLPCM, no TDM mode.
    }
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, AudioEnable|B_TX_AUD_EN_I2S0);
}

static void setHDMITX_HBRAudio(u8 bAudInterface /*I2S/SPDIF/TDM*/)
{
    // u8 rst;
    it6161_hdmi_tx_change_bank(it6161, 0);

    // rst = it6161_hdmi_tx_read(it6161, REG_TX_SW_RST);
	// rst &= ~(B_HDMITX_AUD_RST|B_TX_AREF_RST);

    // it6161_hdmi_tx_write(it6161, REG_TX_SW_RST, rst | B_HDMITX_AUD_RST );

    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL1,0x47); // regE1 bOutputAudioMode should be loaded from ROM image.
#ifdef USE_IT66120
    if( bAudInterface == SPDIF )
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
    }
    else
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xFF); // default mapping.
    }
#else
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
#endif

    if( bAudInterface == SPDIF )
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF);
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,B_TX_CHSTSEL);
    }
    else
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT);
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,0);
    }
    it6161_hdmi_tx_write(it6161, REG_TX_AUD_SRCVALID_FLAT,0x08);
    it6161_hdmi_tx_write(it6161, REG_TX_AUD_HDAUDIO,B_TX_HBR); // regE5 = 0 ;

    //uc = it6161_hdmi_tx_read(it6161, REG_TX_CLK_CTRL1);
    //uc &= ~M_TX_AUD_DIV ;
    //it6161_hdmi_tx_write(it6161, REG_TX_CLK_CTRL1, uc);

    if( bAudInterface == SPDIF )
    {
        u8 i ;
        for( i = 0 ; i < 100 ; i++ )
        {
            if(it6161_hdmi_tx_read(it6161, REG_TX_CLK_STATUS2) & B_TX_OSF_LOCK)
            {
                break ; // stable clock.
            }
        }
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF|B_TX_AUD_EN_SPDIF);
    }
    else
    {
        it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_EN_I2S3|B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0);
    }
    it6161_hdmi_tx_set_bits(it6161, 0x5c, BIT(6), 0x00);
    hdmiTxDev[0].bAudioChannelEnable=it6161_hdmi_tx_read(it6161, REG_TX_AUDIO_CTRL0);
    // it6161_hdmi_tx_write(it6161, REG_TX_SW_RST, rst  );
}

static void setHDMITX_DSDAudio()
{
    // to be continue
    // u8 rst;
    // rst = it6161_hdmi_tx_read(it6161, REG_TX_SW_RST);

    //it6161_hdmi_tx_write(it6161, REG_TX_SW_RST, rst | (B_HDMITX_AUD_RST|B_TX_AREF_RST) );

    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL1,0x41); // regE1 bOutputAudioMode should be loaded from ROM image.
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.

    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT);
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL3,0);

    it6161_hdmi_tx_write(it6161, REG_TX_AUD_SRCVALID_FLAT,0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_AUD_HDAUDIO,B_TX_DSD); // regE5 = 0 ;
    //it6161_hdmi_tx_write(it6161, REG_TX_SW_RST, rst & ~(B_HDMITX_AUD_RST|B_TX_AREF_RST) );

    //uc = it6161_hdmi_tx_read(it6161, REG_TX_CLK_CTRL1);
    //uc &= ~M_TX_AUD_DIV ;
    //it6161_hdmi_tx_write(it6161, REG_TX_CLK_CTRL1, uc);

    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_EN_I2S3|B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0);
}

static void HDMITX_DisableAudioOutput(struct it6161 *it6161)
{
    //u8 uc = (it6161_hdmi_tx_read(it6161, REG_TX_SW_RST) | (B_HDMITX_AUD_RST | B_TX_AREF_RST));
    //it6161_hdmi_tx_write(it6161, REG_TX_SW_RST,uc);
    AudioDelayCnt=AudioOutDelayCnt;
    LastRefaudfreqnum=0;
    it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, (B_HDMITX_AUD_RST | B_TX_AREF_RST), (B_HDMITX_AUD_RST | B_TX_AREF_RST) );
    it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x10, 0x10 );
}

static void HDMITX_EnableAudioOutput(struct it6161 *it6161, u8 AudioType, u8 bAudInterface /*I2S/SPDIF/TDM*/,  u32 SampleFreq,  u8 ChNum, u8 *pIEC60958ChStat, u32 TMDSClock)
{
    static u8 ucIEC60958ChStat[5] ;

    u8 Fs ;
    AudioDelayCnt=36;
    LastRefaudfreqnum=0;
    hdmiTxDev[0].TMDSClock=TMDSClock;
    hdmiTxDev[0].bAudioChannelEnable=0;
    hdmiTxDev[0].bAudInterface=bAudInterface;

    //DRM_INFO1(("HDMITX_EnableAudioOutput(%02X, %s, %d, %d, %p, %d);\n",
    //    AudioType, bSPDIF?"SPDIF":"I2S",SampleFreq, ChNum, pIEC60958ChStat, TMDSClock
    //    ));

    it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST,(B_HDMITX_AUD_RST | B_TX_AREF_RST), (B_HDMITX_AUD_RST | B_TX_AREF_RST));
    it6161_hdmi_tx_write(it6161, REG_TX_CLK_CTRL0,B_TX_AUTO_OVER_SAMPLING_CLOCK|B_TX_EXT_256FS|0x01);

    it6161_hdmi_tx_set_bits(it6161, 0x0F, 0x10, 0x00 ); // power on the ACLK

    if(bAudInterface == SPDIF)
    {
        if(AudioType==T_AUDIO_HBR)
        {
            it6161_hdmi_tx_write(it6161, REG_TX_CLK_CTRL0,0x81);
        }
        it6161_hdmi_tx_set_bits(it6161, REG_TX_AUDIO_CTRL0,B_TX_AUD_SPDIF, B_TX_AUD_SPDIF);
    }
    else
    {
        it6161_hdmi_tx_set_bits(it6161, REG_TX_AUDIO_CTRL0, B_TX_AUD_SPDIF, 0x00);
    }
    if( AudioType != T_AUDIO_DSD)
    {
        // one bit audio have no channel status.
        switch(SampleFreq)
        {
        case  44100L: Fs =  AUDFS_44p1KHz ; break ;
        case  88200L: Fs =  AUDFS_88p2KHz ; break ;
        case 176400L: Fs = AUDFS_176p4KHz ; break ;
        case  32000L: Fs =    AUDFS_32KHz ; break ;
        case  48000L: Fs =    AUDFS_48KHz ; break ;
        case  96000L: Fs =    AUDFS_96KHz ; break ;
        case 192000L: Fs =   AUDFS_192KHz ; break ;
        case 768000L: Fs =   AUDFS_768KHz ; break ;
        default:
            SampleFreq = 48000L ;
            Fs =    AUDFS_48KHz ;
            break ; // default, set Fs = 48KHz.
        }
    #ifdef SUPPORT_AUDIO_MONITOR
        hdmiTxDev[0].bAudFs=Fs;// AUDFS_OTHER;
    #else
        hdmiTxDev[0].bAudFs=Fs;
    #endif
        setHDMITX_NCTS(hdmiTxDev[0].bAudFs);
        if( pIEC60958ChStat == NULL )
        {
            ucIEC60958ChStat[0] = 0 ;
            ucIEC60958ChStat[1] = 0 ;
            ucIEC60958ChStat[2] = (ChNum+1)/2 ;

            if(ucIEC60958ChStat[2]<1)
            {
                ucIEC60958ChStat[2] = 1 ;
            }
            else if( ucIEC60958ChStat[2] >4 )
            {
                ucIEC60958ChStat[2] = 4 ;
            }
            ucIEC60958ChStat[3] = Fs ;
            ucIEC60958ChStat[4] = (((~Fs)<<4) & 0xF0) | CHTSTS_SWCODE ; // Fs | 24bit u32 length
            pIEC60958ChStat = ucIEC60958ChStat ;
        }
    }
    it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST),B_TX_AREF_RST);

    switch(AudioType)
    {
    case T_AUDIO_HBR:
        DRM_INFO("T_AUDIO_HBR\n");
        pIEC60958ChStat[0] |= 1<<1 ;
        pIEC60958ChStat[2] = 0;
        pIEC60958ChStat[3] &= 0xF0 ;
        pIEC60958ChStat[3] |= AUDFS_768KHz ;
        pIEC60958ChStat[4] |= (((~AUDFS_768KHz)<<4) & 0xF0)| 0xB ;
        setHDMITX_ChStat(it6161, pIEC60958ChStat);
        setHDMITX_HBRAudio(bAudInterface);

        break ;
    case T_AUDIO_DSD:
        DRM_INFO("T_AUDIO_DSD\n");
        setHDMITX_DSDAudio();
        break ;
    case T_AUDIO_NLPCM:
        DRM_INFO("T_AUDIO_NLPCM\n");
        pIEC60958ChStat[0] |= 1<<1 ;
        setHDMITX_ChStat(it6161, pIEC60958ChStat);
        setHDMITX_NLPCMAudio(bAudInterface);
        break ;
    case T_AUDIO_LPCM:
        DRM_INFO("T_AUDIO_LPCM\n");
        pIEC60958ChStat[0] &= ~(1<<1);

        setHDMITX_ChStat(it6161, pIEC60958ChStat);
        setHDMITX_LPCMAudio((ChNum+1)/2, SUPPORT_AUDI_AudSWL, bAudInterface);
        // can add auto adjust
        break ;
    }
    it6161_hdmi_tx_set_bits(it6161, REG_TX_INT_MASK1, B_TX_AUDIO_OVFLW_MASK, 0x00);
    it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, hdmiTxDev[0].bAudioChannelEnable);

    it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST),0);
}
#ifdef SUPPORT_AUDIO_MONITOR
void hdmitx_AutoAdjustAudio()
{
    u32 SampleFreq,cTMDSClock ;
    u32 N ;
    u32 aCTS=0;
    u8 fs, uc,LoopCnt=10;
    if(bForceCTS)
    {
        it6161_hdmi_tx_change_bank(it6161, 0);
        it6161_hdmi_tx_write(it6161, 0xF8, 0xC3);
        it6161_hdmi_tx_write(it6161, 0xF8, 0xA5);
        it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL, B_TX_SW_CTS, 0x00); // D[1] = 0, HW auto count CTS
        it6161_hdmi_tx_write(it6161, 0xF8, 0xFF);
    }
    //msleep(50);
    it6161_hdmi_tx_change_bank(it6161, 1);
    N = ((u32)it6161_hdmi_tx_read(it6161, REGPktAudN2)&0xF) << 16 ;
    N |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudN1)) <<8 ;
    N |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudN0));

    while(LoopCnt--)
    {   u32 TempCTS=0;
        aCTS = ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt2)) << 12 ;
        aCTS |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt1)) <<4 ;
        aCTS |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt0)&0xf0)>>4  ;
        if(aCTS==TempCTS)
        {break;}
        TempCTS=aCTS;
    }
    it6161_hdmi_tx_change_bank(it6161, 0);
    if( aCTS == 0)
    {
        DRM_INFO("aCTS== 0");
        return;
    }
    uc = it6161_hdmi_tx_read(it6161, REG_TX_GCP);

    cTMDSClock = hdmiTxDev[0].TMDSClock ;
    //TMDSClock=GetInputPclk();
    DRM_INFO("PCLK = %u0,000\n",(u32)(cTMDSClock/10000));
    switch(uc & 0x70)
    {
    case 0x50:
        cTMDSClock *= 5 ;
        cTMDSClock /= 4 ;
        break ;
    case 0x60:
        cTMDSClock *= 3 ;
        cTMDSClock /= 2 ;
    }
    SampleFreq = cTMDSClock/aCTS ;
    SampleFreq *= N ;
    SampleFreq /= 128 ;
    //SampleFreq=48000;

    DRM_INFO("SampleFreq = %u0\n",(u32)(SampleFreq/10));
    if( SampleFreq>31000L && SampleFreq<=38050L ){fs = AUDFS_32KHz ;}
    else if (SampleFreq < 46550L )  {fs = AUDFS_44p1KHz ;}//46050
    else if (SampleFreq < 68100L )  {fs = AUDFS_48KHz ;}
    else if (SampleFreq < 92100L )  {fs = AUDFS_88p2KHz ;}
    else if (SampleFreq < 136200L ) {fs = AUDFS_96KHz ;}
    else if (SampleFreq < 184200L ) {fs = AUDFS_176p4KHz ;}
    else if (SampleFreq < 240200L ) {fs = AUDFS_192KHz ;}
    else if (SampleFreq < 800000L ) {fs = AUDFS_768KHz ;}
    else
    {
        fs = AUDFS_OTHER;
        DRM_INFO("fs = AUDFS_OTHER\n");
    }
    if(hdmiTxDev[0].bAudFs != fs)
    {
        hdmiTxDev[0].bAudFs=fs;
        setHDMITX_NCTS(hdmiTxDev[0].bAudFs); // set N, CTS by new generated clock.
        //CurrCTS=0;
        return;
    }
    return;
}

bool hdmitx_IsAudioChang()
{
    //u32 pCTS=0;
    u8 FreDiff=0,Refaudfreqnum;

    //it6161_hdmi_tx_change_bank(it6161, 1);
    //pCTS = ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt2)) << 12 ;
    //pCTS |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt1)) <<4 ;
    //pCTS |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt0)&0xf0)>>4  ;
    //it6161_hdmi_tx_change_bank(it6161, 0);
    it6161_hdmi_tx_change_bank(it6161, 0);
    Refaudfreqnum=it6161_hdmi_tx_read(it6161, 0x60);
    //"Refaudfreqnum=%X    pCTS= %u",(u32)Refaudfreqnum,(u32)(pCTS/10000)));
    //if((pCTS%10000)<1000)DRM_INFO("0");
    //if((pCTS%10000)<100)DRM_INFO("0");
    //if((pCTS%10000)<10)DRM_INFO("0");
    //DRM_INFO("%u\n",(u32)(pCTS%10000));
    if((1<<4)&it6161_hdmi_tx_read(it6161, 0x5f))
    {
        //printf("=======XXXXXXXXXXX=========\n");
        return false;
    }
    if(LastRefaudfreqnum>Refaudfreqnum)
        {FreDiff=LastRefaudfreqnum-Refaudfreqnum;}
    else
        {FreDiff=Refaudfreqnum-LastRefaudfreqnum;}
    LastRefaudfreqnum=Refaudfreqnum;
    if(3<FreDiff)
    {
        DRM_INFO("Aduio FreDiff=%d\n",(int)FreDiff);
        it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL,(1<<5), (1<<5));
        it6161_hdmi_tx_set_bits(it6161, REG_TX_AUDIO_CTRL0, 0x0F, 0x00);
        return true;
    }
    else
    {
        return false;
    }
}
/*
void setHDMITX_AudioChannelEnable(bool EnableAudio_b)
{
    static bool AudioOutStatus=false;
    if(EnableAudio_b)
    {
        if(AudioDelayCnt==0)
        {
            //if(hdmiTxDev[0].bAuthenticated==false)
            //{
        #ifdef SUPPORT_AUDIO_MONITOR
            if(hdmitx_IsAudioChang())
            {
                hdmitx_AutoAdjustAudio();
        #else
            if(AudioOutStatus==false)
            {
                setHDMITX_NCTS(hdmiTxDev[0].bAudFs);
        #endif
                it6161_hdmi_tx_write(it6161, REG_TX_AUD_SRCVALID_FLAT,0);
                it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL,(1<<5), (1<<5));
                it6161_hdmi_tx_write(it6161, REG_TX_AUDIO_CTRL0, hdmiTxDev[0].bAudioChannelEnable);
                //it6161_hdmi_tx_set_bits(it6161, 0x59,(1<<2), (1<<2));  //for test
                it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL, 0x3C, 0x00);
                it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL, 1<<5, 0x00);
                printf("Audio Out Enable\n");
        #ifndef SUPPORT_AUDIO_MONITOR
                AudioOutStatus=true;
        #endif
            }
        }
        else
        {
            AudioOutStatus=false;
            if(0==(it6161_hdmi_tx_read(it6161, REG_TX_CLK_STATUS2)&0x10))
            {
                AudioDelayCnt--;
            }
            else
            {
                AudioDelayCnt=AudioOutDelayCnt;
            }
        }
    }
    else
    {
       // CurrCTS=0;
    }
}*/
#endif //#ifdef SUPPORT_AUDIO_MONITOR
//////////////////////////////////////////////////////////////////////
// Function: setHDMITX_NCTS
// Parameter: PCLK - video clock in Hz.
//            Fs - Encoded audio sample rate
//                          AUDFS_22p05KHz  4
//                          AUDFS_44p1KHz 0
//                          AUDFS_88p2KHz 8
//                          AUDFS_176p4KHz    12
//
//                          AUDFS_24KHz  6
//                          AUDFS_48KHz  2
//                          AUDFS_96KHz  10
//                          AUDFS_192KHz 14
//
//                          AUDFS_768KHz 9
//
//                          AUDFS_32KHz  3
//                          AUDFS_OTHER    1
// Return: ER_SUCCESS if success
// Remark: set N value,the CTS will be auto generated by HW.
// Side-Effect: register bank will reset to bank 0.
//////////////////////////////////////////////////////////////////////

static void setHDMITX_NCTS(u8 Fs)
{
    u32 n;
    u8 LoopCnt=255,CTSStableCnt=0;
    u32 diff;
    u32 CTS=0,LastCTS=0;
    bool HBR_mode;
    // u8 aVIC;

    if(B_TX_HBR & it6161_hdmi_tx_read(it6161, REG_TX_AUD_HDAUDIO))
    {
        HBR_mode=true;
    }
    else
    {
        HBR_mode=false;
    }
    switch(Fs)
    {
    case AUDFS_32KHz: n = 4096; break;
    case AUDFS_44p1KHz: n = 6272; break;
    case AUDFS_48KHz: n = 6144; break;
    case AUDFS_88p2KHz: n = 12544; break;
    case AUDFS_96KHz: n = 12288; break;
    case AUDFS_176p4KHz: n = 25088; break;
    case AUDFS_192KHz: n = 24576; break;
    case AUDFS_768KHz: n = 24576; break ;
    default: n = 6144;
    }
    // tr_printf((" n = %d\n",n));
    it6161_hdmi_tx_change_bank(it6161, 1);
    it6161_hdmi_tx_write(it6161, REGPktAudN0,(u8)((n)&0xFF));
    it6161_hdmi_tx_write(it6161, REGPktAudN1,(u8)((n>>8)&0xFF));
    it6161_hdmi_tx_write(it6161, REGPktAudN2,(u8)((n>>16)&0xF));

    if(bForceCTS)
    {
        u32 SumCTS=0;
        while(LoopCnt--)
        {
            msleep(30);
            CTS = ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt2)) << 12 ;
            CTS |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt1)) <<4 ;
            CTS |= ((u32)it6161_hdmi_tx_read(it6161, REGPktAudCTSCnt0)&0xf0)>>4  ;
            if( CTS == 0)
            {
                continue;
            }
            else
            {
                if(LastCTS>CTS )
                    {diff=LastCTS-CTS;}
                else
                    {diff=CTS-LastCTS;}
                //DRM_INFO("LastCTS= %u%u",(u32)(LastCTS/10000),(u32)(LastCTS%10000));
                //DRM_INFO("       CTS= %u%u\n",(u32)(CTS/10000),(u32)(CTS%10000));
                LastCTS=CTS;
                if(5>diff)
                {
                    CTSStableCnt++;
                    SumCTS+=CTS;
                }
                else
                {
                    CTSStableCnt=0;
                    SumCTS=0;
                    continue;
                }
                if(CTSStableCnt>=32)
                {
                    LastCTS=(SumCTS>>5);
                    break;
                }
            }
        }
    }
    it6161_hdmi_tx_write(it6161, REGPktAudCTS0,(u8)((LastCTS)&0xFF));
    it6161_hdmi_tx_write(it6161, REGPktAudCTS1,(u8)((LastCTS>>8)&0xFF));
    it6161_hdmi_tx_write(it6161, REGPktAudCTS2,(u8)((LastCTS>>16)&0xF));
    it6161_hdmi_tx_change_bank(it6161, 0);
#ifdef Force_CTS
    bForceCTS = true;
#endif
    it6161_hdmi_tx_write(it6161, 0xF8, 0xC3);
    it6161_hdmi_tx_write(it6161, 0xF8, 0xA5);
    if(bForceCTS)
    {
        it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL,B_TX_SW_CTS, B_TX_SW_CTS); // D[1] = 0, HW auto count CTS
    }
    else
    {
        it6161_hdmi_tx_set_bits(it6161, REG_TX_PKT_SINGLE_CTRL, B_TX_SW_CTS, 0x00); // D[1] = 0, HW auto count CTS
    }
    it6161_hdmi_tx_write(it6161, 0xF8, 0xFF);

    if(false==HBR_mode) //LPCM
    {
        u8 uData;
        it6161_hdmi_tx_change_bank(it6161, 1);
        Fs = AUDFS_768KHz ;
        it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_CA_FS,0x00|Fs);
        Fs = ~Fs ; // OFS is the one's complement of FS
        uData = (0x0f&it6161_hdmi_tx_read(it6161, REG_TX_AUDCHST_OFS_WL));
        it6161_hdmi_tx_write(it6161, REG_TX_AUDCHST_OFS_WL,(Fs<<4)|uData);
        it6161_hdmi_tx_change_bank(it6161, 0);
    }
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetAudioInfoFrame()
// Parameter: pAudioInfoFrame - the pointer to HDMI Audio Infoframe ucData
// Return: N/A
// Remark: Fill the Audio InfoFrame ucData,and count checksum,then fill into
//         Audio InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

static SYS_STATUS hdmitx_SetAudioInfoFrame(struct it6161 *it6161, Audio_InfoFrame *pAudioInfoFrame)
{
    u8 checksum ;

    if(!pAudioInfoFrame)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_change_bank(it6161, 1);
    checksum = 0x100-(AUDIO_INFOFRAME_VER+AUDIO_INFOFRAME_TYPE+AUDIO_INFOFRAME_LEN );
    it6161_hdmi_tx_write(it6161, REG_TX_PKT_AUDINFO_CC,pAudioInfoFrame->pktbyte.AUD_DB[0]);
    checksum -= it6161_hdmi_tx_read(it6161, REG_TX_PKT_AUDINFO_CC); checksum &= 0xFF ;
    it6161_hdmi_tx_write(it6161, REG_TX_PKT_AUDINFO_SF,pAudioInfoFrame->pktbyte.AUD_DB[1]);
    checksum -= it6161_hdmi_tx_read(it6161, REG_TX_PKT_AUDINFO_SF); checksum &= 0xFF ;
    it6161_hdmi_tx_write(it6161, REG_TX_PKT_AUDINFO_CA,pAudioInfoFrame->pktbyte.AUD_DB[3]);
    checksum -= it6161_hdmi_tx_read(it6161, REG_TX_PKT_AUDINFO_CA); checksum &= 0xFF ;
    it6161_hdmi_tx_write(it6161, REG_TX_PKT_AUDINFO_DM_LSV,pAudioInfoFrame->pktbyte.AUD_DB[4]);
    checksum -= it6161_hdmi_tx_read(it6161, REG_TX_PKT_AUDINFO_DM_LSV); checksum &= 0xFF ;

    it6161_hdmi_tx_write(it6161, REG_TX_PKT_AUDINFO_SUM,checksum);

    it6161_hdmi_tx_change_bank(it6161, 0);
    hdmitx_ENABLE_AUD_INFOFRM_PKT();
    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetAVIInfoFrame()
// Parameter: pAVIInfoFrame - the pointer to HDMI AVI Infoframe ucData
// Return: N/A
// Remark: Fill the AVI InfoFrame ucData,and count checksum,then fill into
//         AVI InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////
#if 0
static SYS_STATUS hdmitx_SetAVIInfoFrame(struct it6161 *it6161, AVI_InfoFrame *pAVIInfoFrame)
{
    int i ;
    u8 checksum ;

    if(!pAVIInfoFrame)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_change_bank(it6161, 1);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB1,pAVIInfoFrame->pktbyte.AVI_DB[0]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB2,pAVIInfoFrame->pktbyte.AVI_DB[1]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB3,pAVIInfoFrame->pktbyte.AVI_DB[2]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB4,pAVIInfoFrame->pktbyte.AVI_DB[3]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB5,pAVIInfoFrame->pktbyte.AVI_DB[4]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB6,pAVIInfoFrame->pktbyte.AVI_DB[5]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB7,pAVIInfoFrame->pktbyte.AVI_DB[6]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB8,pAVIInfoFrame->pktbyte.AVI_DB[7]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB9,pAVIInfoFrame->pktbyte.AVI_DB[8]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB10,pAVIInfoFrame->pktbyte.AVI_DB[9]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB11,pAVIInfoFrame->pktbyte.AVI_DB[10]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB12,pAVIInfoFrame->pktbyte.AVI_DB[11]);
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB13,pAVIInfoFrame->pktbyte.AVI_DB[12]);
    for(i = 0,checksum = 0; i < 13 ; i++)
    {
        checksum -= pAVIInfoFrame->pktbyte.AVI_DB[i] ;
    }
    /*
    DRM_INFO("SetAVIInfo(): ");
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB1));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB2));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB3));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB4));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB5));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB6));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB7));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB8));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB9));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB10));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB11));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB12));
    DRM_INFO("%02X ",(int)it6161_hdmi_tx_read(it6161, REG_TX_AVIINFO_DB13));
    DRM_INFO("\n");
    */
    checksum -= AVI_INFOFRAME_VER+AVI_INFOFRAME_TYPE+AVI_INFOFRAME_LEN ;
    it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_SUM,checksum);

    it6161_hdmi_tx_change_bank(it6161, 0);
    hdmitx_ENABLE_AVI_INFOFRM_PKT();
    return ER_SUCCESS ;
}
#endif
static SYS_STATUS hdmitx_SetVSIInfoFrame(struct it6161 *it6161, VendorSpecific_InfoFrame *pVSIInfoFrame)
{
    u8 ucData=0 ;

    if(!pVSIInfoFrame)
    {
        return ER_FAIL ;
    }

    it6161_hdmi_tx_change_bank(it6161, 1);
    it6161_hdmi_tx_write(it6161, 0x80,pVSIInfoFrame->pktbyte.VS_DB[3]);
    it6161_hdmi_tx_write(it6161, 0x81,pVSIInfoFrame->pktbyte.VS_DB[4]);

    ucData -= pVSIInfoFrame->pktbyte.VS_DB[3] ;
    ucData -= pVSIInfoFrame->pktbyte.VS_DB[4] ;

    if(  pVSIInfoFrame->pktbyte.VS_DB[4] & (1<<7 ))
    {
        ucData -= pVSIInfoFrame->pktbyte.VS_DB[5] ;
        it6161_hdmi_tx_write(it6161, 0x82,pVSIInfoFrame->pktbyte.VS_DB[5]);
        ucData -= VENDORSPEC_INFOFRAME_TYPE + VENDORSPEC_INFOFRAME_VER + 6 + 0x0C + 0x03 ;
    }
    else
    {
        ucData -= VENDORSPEC_INFOFRAME_TYPE + VENDORSPEC_INFOFRAME_VER + 5 + 0x0C + 0x03 ;
    }

    pVSIInfoFrame->pktbyte.CheckSum=ucData;

    it6161_hdmi_tx_write(it6161, 0x83,pVSIInfoFrame->pktbyte.CheckSum);
    it6161_hdmi_tx_change_bank(it6161, 0);
    it6161_hdmi_tx_write(it6161, REG_TX_3D_INFO_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT);
    return ER_SUCCESS ;
}

static bool HDMITX_EnableVSInfoFrame(struct it6161 *it6161, u8 bEnable,u8 *pVSInfoFrame)
{
    if(!bEnable)
    {
        hdmitx_DISABLE_VSDB_PKT();
        return true ;
    }
    if(hdmitx_SetVSIInfoFrame(it6161, (VendorSpecific_InfoFrame *)pVSInfoFrame) == ER_SUCCESS)
    {
        return true ;
    }
    return false ;
}
/*
static bool HDMITX_EnableAVIInfoFrame(struct it6161 *it6161, u8 bEnable,u8 *pAVIInfoFrame)
{
    if(!bEnable)
    {
        hdmitx_DISABLE_AVI_INFOFRM_PKT();
        return true ;
    }
    if(hdmitx_SetAVIInfoFrame(it6161, (AVI_InfoFrame *)pAVIInfoFrame) == ER_SUCCESS)
    {
        return true ;
    }
    return false ;
}
*/
static bool HDMITX_EnableAudioInfoFrame(struct it6161 *it6161, u8 bEnable,u8 *pAudioInfoFrame)
{
    if(!bEnable)
    {
        hdmitx_DISABLE_AVI_INFOFRM_PKT();
        return true ;
    }
    if(hdmitx_SetAudioInfoFrame(it6161, (Audio_InfoFrame *)pAudioInfoFrame) == ER_SUCCESS)
    {
        return true ;
    }
    return false ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetSPDInfoFrame()
// Parameter: pSPDInfoFrame - the pointer to HDMI SPD Infoframe ucData
// Return: N/A
// Remark: Fill the SPD InfoFrame ucData,and count checksum,then fill into
//         SPD InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////
/*
SYS_STATUS hdmitx_SetSPDInfoFrame(SPD_InfoFrame *pSPDInfoFrame)
{
    int i ;
    u8 ucData ;

    if(!pSPDInfoFrame)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_change_bank(it6161, 1);
    for(i = 0,ucData = 0 ; i < 25 ; i++)
    {
        ucData -= pSPDInfoFrame->pktbyte.SPD_DB[i] ;
        it6161_hdmi_tx_write(it6161, REG_TX_PKT_SPDINFO_PB1+i,pSPDInfoFrame->pktbyte.SPD_DB[i]);
    }
    ucData -= SPD_INFOFRAME_VER+SPD_INFOFRAME_TYPE+SPD_INFOFRAME_LEN ;
    it6161_hdmi_tx_write(it6161, REG_TX_PKT_SPDINFO_SUM,ucData); // checksum
    it6161_hdmi_tx_change_bank(it6161, 0);
    hdmitx_ENABLE_SPD_INFOFRM_PKT();
    return ER_SUCCESS ;
}
*/
//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetMPEGInfoFrame()
// Parameter: pMPEGInfoFrame - the pointer to HDMI MPEG Infoframe ucData
// Return: N/A
// Remark: Fill the MPEG InfoFrame ucData,and count checksum,then fill into
//         MPEG InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////
/*
SYS_STATUS hdmitx_SetMPEGInfoFrame(MPEG_InfoFrame *pMPGInfoFrame)
{
    int i ;
    u8 ucData ;

    if(!pMPGInfoFrame)
    {
        return ER_FAIL ;
    }
    it6161_hdmi_tx_change_bank(it6161, 1);

    it6161_hdmi_tx_write(it6161, REG_TX_PKT_MPGINFO_FMT,pMPGInfoFrame->info.FieldRepeat|(pMPGInfoFrame->info.MpegFrame<<1));
    it6161_hdmi_tx_write(it6161, REG_TX_PKG_MPGINFO_DB0,pMPGInfoFrame->pktbyte.MPG_DB[0]);
    it6161_hdmi_tx_write(it6161, REG_TX_PKG_MPGINFO_DB1,pMPGInfoFrame->pktbyte.MPG_DB[1]);
    it6161_hdmi_tx_write(it6161, REG_TX_PKG_MPGINFO_DB2,pMPGInfoFrame->pktbyte.MPG_DB[2]);
    it6161_hdmi_tx_write(it6161, REG_TX_PKG_MPGINFO_DB3,pMPGInfoFrame->pktbyte.MPG_DB[3]);

    for(ucData = 0,i = 0 ; i < 5 ; i++)
    {
        ucData -= pMPGInfoFrame->pktbyte.MPG_DB[i] ;
    }
    ucData -= MPEG_INFOFRAME_VER+MPEG_INFOFRAME_TYPE+MPEG_INFOFRAME_LEN ;

    it6161_hdmi_tx_write(it6161, REG_TX_PKG_MPGINFO_SUM,ucData);

    it6161_hdmi_tx_change_bank(it6161, 0);
    hdmitx_ENABLE_SPD_INFOFRM_PKT();

    return ER_SUCCESS ;
}
*/
// 2009/12/04 added by Ming-chih.lung@ite.com.tw

/*
SYS_STATUS hdmitx_Set_GeneralPurpose_PKT(u8 *pData)
{
    int i ;

    if( pData == NULL )
    {
        return ER_FAIL ;

    }
    it6161_hdmi_tx_change_bank(it6161, 1);
    for( i = 0x38 ; i <= 0x56 ; i++)
    {
        it6161_hdmi_tx_write(it6161, i, pData[i-0x38] );
    }
    it6161_hdmi_tx_change_bank(it6161, 0);
    hdmitx_ENABLE_GeneralPurpose_PKT();
    //hdmitx_ENABLE_NULL_PKT();
    return ER_SUCCESS ;
}
*/
/*
static void ConfigAVIInfoFrame(u8 VIC, u8 pixelrep)
{
    AVI_InfoFrame *AviInfo;
    AviInfo = (AVI_InfoFrame *)CommunBuff ;

    AviInfo->pktbyte.AVI_HB[0] = AVI_INFOFRAME_TYPE|0x80 ;
    AviInfo->pktbyte.AVI_HB[1] = AVI_INFOFRAME_VER ;
    AviInfo->pktbyte.AVI_HB[2] = AVI_INFOFRAME_LEN ;

    switch(it6161->hdmi_tx_output_color_space)
    {
    case F_MODE_YUV444:
        // AviInfo->info.ColorMode = 2 ;
        AviInfo->pktbyte.AVI_DB[0] = (2<<5)|(1<<4);
        break ;
    case F_MODE_YUV422:
        // AviInfo->info.ColorMode = 1 ;
        AviInfo->pktbyte.AVI_DB[0] = (1<<5)|(1<<4);
        break ;
    case F_MODE_RGB444:
    default:
        // AviInfo->info.ColorMode = 0 ;
        AviInfo->pktbyte.AVI_DB[0] = (0<<5)|(1<<4);
        break ;
    }
    AviInfo->pktbyte.AVI_DB[1] = 8 ;
    AviInfo->pktbyte.AVI_DB[1] |= (aspec != HDMI_16x9)?(1<<4):(2<<4); // 4:3 or 16:9
    AviInfo->pktbyte.AVI_DB[1] |= (Colorimetry != HDMI_ITU709)?(1<<6):(2<<6); // 4:3 or 16:9
    AviInfo->pktbyte.AVI_DB[2] = 0 ;
    AviInfo->pktbyte.AVI_DB[3] = VIC ;
    AviInfo->pktbyte.AVI_DB[4] =  pixelrep & 3 ;
    AviInfo->pktbyte.AVI_DB[5] = 0 ;
    AviInfo->pktbyte.AVI_DB[6] = 0 ;
    AviInfo->pktbyte.AVI_DB[7] = 0 ;
    AviInfo->pktbyte.AVI_DB[8] = 0 ;
    AviInfo->pktbyte.AVI_DB[9] = 0 ;
    AviInfo->pktbyte.AVI_DB[10] = 0 ;
    AviInfo->pktbyte.AVI_DB[11] = 0 ;
    AviInfo->pktbyte.AVI_DB[12] = 0 ;

    HDMITX_EnableAVIInfoFrame(it6161, true, (unsigned char *)AviInfo);
}
*/
static void ConfigAudioInfoFrm(struct it6161 *it6161, u8 channel_count)
{
    int i;
    Audio_InfoFrame *AudioInfo ;
    AudioInfo = (Audio_InfoFrame *)CommunBuff ;

    DRM_INFO("ConfigAudioInfoFrm channel count: %d", channel_count);

    AudioInfo->pktbyte.AUD_HB[0] = AUDIO_INFOFRAME_TYPE ;
    AudioInfo->pktbyte.AUD_HB[1] = 1 ;
    AudioInfo->pktbyte.AUD_HB[2] = AUDIO_INFOFRAME_LEN ;
    AudioInfo->pktbyte.AUD_DB[0] = channel_count - 1;

    for (i = 1 ;i < AUDIO_INFOFRAME_LEN ; i++) {
        AudioInfo->pktbyte.AUD_DB[i] = 0 ;
    }

    /* audio_infoframe_ca */
	switch (channel_count) {
		case 0 :
			AudioInfo->pktbyte.AUD_DB[3] = 0xFF;
			break;  // no audio
		case 2 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x00;
			break;
		case 3 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x01;
			break;  // 0x01,0x02,0x04
		case 4 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x03;
			break;  // 0x03,0x05,0x06,0x08,0x14
		case 5 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x07;
			break;  // 0x07,0x09,0x0A,0x0C,0x15,0x16,0x18
		case 6 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x0B;
			break;  // 0x0B,0x0D,0x0E,0x10,0x17,0x19,0x1A,0x1C
		case 7 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x0F;
			break;  // 0x0F,0x11,0x12,0x1B,0x1D,0x1E
		case 8 :
			AudioInfo->pktbyte.AUD_DB[3] = 0x1F;
			break;  // 0x13,0x1F
		default :
			DRM_INFO("Error: Audio Channel Number Error!");
	}

    HDMITX_EnableAudioInfoFrame(it6161, TRUE, (unsigned char *)AudioInfo);
}

#ifdef OUTPUT_3D_MODE
void ConfigfHdmiVendorSpecificInfoFrame(struct it6161 *it6161, u8 _3D_Stru)
{
    VendorSpecific_InfoFrame *VS_Info;

    VS_Info=(VendorSpecific_InfoFrame *)CommunBuff ;

    VS_Info->pktbyte.VS_HB[0] = VENDORSPEC_INFOFRAME_TYPE|0x80;
    VS_Info->pktbyte.VS_HB[1] = VENDORSPEC_INFOFRAME_VER;
    VS_Info->pktbyte.VS_HB[2] = (_3D_Stru == Side_by_Side)?6:5;
    VS_Info->pktbyte.VS_DB[0] = 0x03;
    VS_Info->pktbyte.VS_DB[1] = 0x0C;
    VS_Info->pktbyte.VS_DB[2] = 0x00;
    VS_Info->pktbyte.VS_DB[3] = 0x40;
    switch(_3D_Stru)
    {
    case Side_by_Side:
    case Frame_Pcaking:
    case Top_and_Botton:
        VS_Info->pktbyte.VS_DB[4] = (_3D_Stru<<4);
        break;
    default:
        VS_Info->pktbyte.VS_DB[4] = (Frame_Pcaking<<4);
        break ;
    }
    VS_Info->pktbyte.VS_DB[5] = 0x00;
    HDMITX_EnableVSInfoFrame(it6161, true,(u8 *)VS_Info);
}
#endif //#ifdef OUTPUT_3D_MODE

static void hdmi_tx_audio_process(struct it6161 *it6161)
{
    if (it6161->support_audio) {
        ConfigAudioInfoFrm(it6161, bOutputAudioChannel);
        // HDMITX_EnableAudioOutput(T_AUDIO_LPCM, false, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,TMDSClock);
        HDMITX_EnableAudioOutput(it6161,
            //CNOFIG_INPUT_AUDIO_TYPE,
            bOutputAudioType,
            CONFIG_INPUT_AUDIO_INTERFACE,
            ulAudioSampleFS,
            bOutputAudioChannel,
            NULL, // pointer to cahnnel status.
            VideoPixelClock*(pixelrep+1));
        // if you have channel status , set here.
        // setHDMITX_ChStat(it6161, u8 ucIEC60958ChStat[]);
    }
}

#if 0
static u8 hdmi_infoframe_checksum(u8 *ptr, size_t size)
{
	u8 csum = 0;
	size_t i;

	/* compute checksum */
	for (i = 0; i < size; i++)
		csum += ptr[i];

	return 256 - csum;
}

static void hdmi_infoframe_set_checksum(void *buffer, size_t size)
{
	u8 *ptr = buffer;

	ptr[3] = hdmi_infoframe_checksum(buffer, size);
}
#endif

static int hdmi_tx_get_avi_infoframe_from_source(struct it6161 *it6161, u8 *buffer, size_t size)
{
	struct device *dev = &it6161->i2c_mipi_rx->dev;
	int err;

	err = hdmi_avi_infoframe_pack(&it6161->source_avi_infoframe, buffer, size);
	if (err < 0) {
		DRM_DEV_ERROR(dev, "Failed to pack AVI infoframe: %d", err);
		return err;
	}

	return 0;
}

static int hdmi_tx_get_avi_infoframe_from_user_define(struct it6161 *it6161, u8 *buffer, size_t size)
{
	struct device *dev = &it6161->i2c_hdmi_tx->dev;
	struct hdmi_avi_infoframe *frame = &it6161->source_avi_infoframe;
	struct drm_display_mode *display_mode = &it6161->source_display_mode;
	int ret;

	DRM_INFO( "user define to setup AVI infoframe");

	ret = drm_hdmi_avi_infoframe_from_display_mode(frame, &it6161->connector, display_mode);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to setup AVI infoframe: %d", ret);
		return ret;
	}

	if ((it6161->hdmi_tx_output_color_space & F_MODE_CLRMOD_MASK) == F_MODE_RGB444)
		frame->colorspace =  HDMI_COLORSPACE_RGB;

	if ((it6161->hdmi_tx_output_color_space & F_MODE_CLRMOD_MASK) == F_MODE_YUV444)
		frame->colorspace =  HDMI_COLORSPACE_YUV444;

	if ((it6161->hdmi_tx_output_color_space & F_MODE_CLRMOD_MASK) == F_MODE_YUV422)
		frame->colorspace =  HDMI_COLORSPACE_YUV422;

	ret = hdmi_tx_get_avi_infoframe_from_source(it6161, buffer, size);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to pack AVI infoframe: %d", ret);
		return ret;
	}

	return 0;
}

static int (*hdmi_tx_get_avi_infoframe)(struct it6161*, u8*, size_t) = hdmi_tx_get_avi_infoframe_from_user_define;

static void hdmi_tx_setup_avi_infoframe(struct it6161 *it6161, u8 *buffer, size_t size)
{
	u8 i, *ptr = buffer + HDMI_INFOFRAME_HEADER_SIZE;

	it6161_hdmi_tx_change_bank(it6161, 1);

	for (i = 0; i < it6161->source_avi_infoframe.length; i++)
		it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_DB1 + i, ptr[i]);

	it6161_hdmi_tx_write(it6161, REG_TX_AVIINFO_SUM, buffer[3]);
}

static inline void hdmi_tx_disable_avi_infoframe(struct it6161 *it6161)
{
	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_write(it6161, REG_TX_AVI_INFOFRM_CTRL, 0x00);
}

static inline void hdmi_tx_enable_avi_infoframe(struct it6161 *it6161)
{
	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_write(it6161, REG_TX_AVI_INFOFRM_CTRL, B_TX_ENABLE_PKT | B_TX_REPEAT_PKT);
}

static int hdmi_tx_avi_infoframe_process(struct it6161 *it6161)
{
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	int err;

	hdmi_tx_disable_avi_infoframe(it6161);
	err = hdmi_tx_get_avi_infoframe(it6161, buffer, sizeof(buffer));

	if (err)
		return err;

	hdmi_tx_setup_avi_infoframe(it6161, buffer, sizeof(buffer));
	hdmi_tx_enable_avi_infoframe(it6161);

	DRM_INFO("avi_infoframe:0x%*ph", (int)ARRAY_SIZE(buffer), buffer);

	return 0;
}

static void hdmi_tx_set_output_process(struct it6161 *it6161)
{
	VIDEOPCLKLEVEL level;
	u32 TMDSClock;

	TMDSClock = it6161->hdmi_tx_pclk * 1000 * (it6161->source_avi_infoframe.pixel_repeat + 1);

	HDMITX_DisableAudioOutput(it6161);
	//hdmi_tx_hdcp_reset_auth(it6161);
    hdmi_tx_disable_avi_infoframe(it6161);
    HDMITX_EnableVSInfoFrame(it6161, false,NULL);

	if (TMDSClock > 80000000L) {
		level = PCLK_HIGH ;
	} else if(TMDSClock > 20000000L) {
		level = PCLK_MEDIUM ;
	} else {
		level = PCLK_LOW ;
	}

	hdmi_tx_enable_video_output(it6161, level);

	if (it6161->hdmi_mode) {
#ifdef OUTPUT_3D_MODE
		ConfigfHdmiVendorSpecificInfoFrame(it6161, OUTPUT_3D_MODE);
#endif

		hdmi_tx_avi_infoframe_process(it6161);
		hdmi_tx_audio_process(it6161);

        // if( it6161->support_audio )
        // {
        //     ConfigAudioInfoFrm(it6161);
        // #ifdef SUPPORT_HBR_AUDIO
        //     HDMITX_EnableAudioOutput(it6161, T_AUDIO_HBR, CONFIG_INPUT_AUDIO_INTERFACE, 768000L,8,NULL,TMDSClock);
        // #else
        //     // HDMITX_EnableAudioOutput(it6161, T_AUDIO_LPCM, false, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,TMDSClock);
        //     HDMITX_EnableAudioOutput(it6161, CNOFIG_INPUT_AUDIO_TYPE, CONFIG_INPUT_AUDIO_INTERFACE, ulAudioSampleFS,bOutputAudioChannel,NULL,TMDSClock);
        // #endif
        // }

	}

#ifdef SUPPORT_CEC
	it6161_hdmi_tx_change_bank(it6161, 0);
	it6161_hdmi_tx_write(it6161,  0xf, 0 );
	Initial_Ext_Int1();
	HDMITX_CEC_Init();
#endif // SUPPORT_CEC
	it6161_hdmi_tx_set_av_mute(it6161, false);
	bChangeMode = false ;
}

/*void HDMITX_ChangeAudioOption(u8 Option, u8 channelNum, u8 AudioFs)
{

    switch(Option )
    {
    case T_AUDIO_HBR :
        bOutputAudioType = T_AUDIO_HBR ;
        ulAudioSampleFS = 768000L ;
        bOutputAudioChannel = 8 ;
        return ;
    case T_AUDIO_NLPCM :
        bOutputAudioType = T_AUDIO_NLPCM ;
        bOutputAudioChannel = 2 ;
        break ;
    default:
        bOutputAudioType = T_AUDIO_LPCM ;
        if( channelNum < 1 )
        {
            bOutputAudioChannel = 1 ;
        }
        else if( channelNum > 8 )
        {
            bOutputAudioChannel = 8 ;
        }
        else
        {
            bOutputAudioChannel = channelNum ;
        }
        break ;
    }

    switch(AudioFs)
    {
    case AUDFS_44p1KHz:
        ulAudioSampleFS =  44100L ;
        break ;
    case AUDFS_88p2KHz:
        ulAudioSampleFS =  88200L ;
        break ;
    case AUDFS_176p4KHz:
        ulAudioSampleFS = 176400L ;
        break ;

    case AUDFS_48KHz:
        ulAudioSampleFS =  48000L ;
        break ;
    case AUDFS_96KHz:
        ulAudioSampleFS =  96000L ;
        break ;
    case AUDFS_192KHz:
        ulAudioSampleFS = 192000L ;
        break ;

    case AUDFS_768KHz:
        ulAudioSampleFS = 768000L ;
        break ;

    case AUDFS_32KHz:
        ulAudioSampleFS =  32000L ;
        break ;
    default:
        ulAudioSampleFS =  48000L ;
        break ;
    }
    DRM_INFO("HDMITX_ChangeAudioOption():bOutputAudioType = %02X, ulAudioSampleFS = %8ld, bOutputAudioChannel = %d\n",(int)bOutputAudioType,ulAudioSampleFS,(int)bOutputAudioChannel);
}
*/

#ifdef HDMITX_AUTO_MONITOR_INPUT

void HDMITX_MonitorInputAudioChange()
{
    static u32 prevAudioSampleFS = 0 ;
    u32 AudioFS ;

    if( !it6161->support_audio )
    {
        prevAudioSampleFS = 0 ;
    }
    else
    {
        AudioFS = CalcAudFS() ;
        DRM_INFO1(("Audio Chagne, Audio clock = %dHz\n",AudioFS)) ;
        if( AudioFS > 188000L ) // 192KHz
        {
            ulAudioSampleFS = 192000L ;
        }
        else if( AudioFS > 144000L ) // 176.4KHz
        {
            ulAudioSampleFS = 176400L ;
        }
        else if( AudioFS >  93000L ) // 96KHz
        {
            ulAudioSampleFS = 96000L ;
        }
        else if( AudioFS >  80000L ) // 88.2KHz
        {
            ulAudioSampleFS = 88200L ;
        }
        else if( AudioFS >  45000L ) // 48 KHz
        {
            ulAudioSampleFS = 48000L ;
        }
        else if( AudioFS >  36000L ) // 44.1KHz
        {
            ulAudioSampleFS = 44100L ;
        }
        else                         // 32KHz
        {
            ulAudioSampleFS = 32000L ;
        }

        if(!bChangeMode)
        {
            if( ulAudioSampleFS != prevAudioSampleFS )
            {
                DRM_INFO("ulAudioSampleFS = %dHz -> %dHz\n",ulAudioSampleFS,ulAudioSampleFS);
                ConfigAudioInfoFrm(it6161, 2);
                HDMITX_EnableAudioOutput(it6161, CNOFIG_INPUT_AUDIO_TYPE, CONFIG_INPUT_AUDIO_INTERFACE, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,0);
                // HDMITX_EnableAudioOutput(it6161, T_AUDIO_LPCM, false, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,0);

            }
        }

        prevAudioSampleFS = ulAudioSampleFS ;

    }
}

#endif // HDMITX_AUTO_MONITOR_INPUT

static void mipi_rx_calc_rclk(struct it6161 *it6161)
{
	u32 sum = 0, i, retry = 5;
	int t10usint;

	//it6161_hdmi_tx_write(it6161, 0x8D, (CEC_I2C_SLAVE_ADDR|0x01));// Enable CRCLK
	for (i = 0; i < retry; i++) {
		it6161_mipi_rx_set_bits(it6161, 0x94, 0x80, 0x80); // Enable RCLK 100ms count
		msleep(100);
		it6161_mipi_rx_set_bits(it6161, 0x94, 0x80, 0x00); // Disable RCLK 100ms count

		it6161->mipi_rx_rclk = it6161_mipi_rx_read(it6161, 0x97);
		it6161->mipi_rx_rclk <<= 8;
		it6161->mipi_rx_rclk += it6161_mipi_rx_read(it6161, 0x96);
		it6161->mipi_rx_rclk <<=8;
		it6161->mipi_rx_rclk += it6161_mipi_rx_read(it6161, 0x95);
		sum += it6161->mipi_rx_rclk;
	}

	sum /= retry;
	DRM_INFO("rclk: %d\n", sum);
	//it6161->mipi_rx_rclk = sum / 100;
	it6161->mipi_rx_rclk = sum / 104;
	t10usint = it6161->mipi_rx_rclk / 108;//actually nxp platform msleep(100) is 108ms
	DRM_INFO("it6161->mipi_rx_rclk = %d,%03d,%03d\n",(sum*10)/1000000,((sum*10)%1000000)/1000,((sum*10)%100));
	DRM_INFO("T10usInt=0x%03X\n", (int)t10usint);
	it6161_mipi_rx_write(it6161, 0x91, t10usint&0xFF);
}

static void mipi_rx_calc_mclk(struct it6161 *it6161)
{
	u32 i, rddata, sum = 0, calc_time = 3;

	for (i = 0; i < calc_time; i++) {
		it6161_mipi_rx_set_bits(it6161, 0x9B, 0x80, 0x80);
		msleep(5);
		it6161_mipi_rx_set_bits(it6161, 0x9B, 0x80, 0x00);

		rddata = it6161_mipi_rx_read(it6161, 0x9B) & 0x0F;
		rddata <<= 8;
		rddata += it6161_mipi_rx_read(it6161, 0x9A);

		sum += rddata;
	}

	sum /= calc_time;
	it6161->mipi_rx_mclk = it6161->mipi_rx_rclk * 2048 / sum;
	DRM_INFO("MCLK = %d.%03dMHz", it6161->mipi_rx_mclk / 1000, it6161->mipi_rx_mclk % 1000);
}

static void mipi_rx_calc_pclk(struct it6161 *it6161)
{
	u32 rddata, sum = 0, retry = 3;
	u8 i;

	it6161_mipi_rx_set_bits(it6161, 0x99, 0x80, 0x00);

	for (i = 0; i < retry; i++) {
		it6161_mipi_rx_set_bits(it6161, 0x99, 0x80, 0x80);
		msleep(5);
		it6161_mipi_rx_set_bits(it6161, 0x99, 0x80, 0x00);
		msleep(1);

		rddata = it6161_mipi_rx_read(it6161, 0x99) & 0x0F;
		rddata <<= 8;
		rddata += it6161_mipi_rx_read(it6161, 0x98);
		sum += rddata;
	}

	sum /= retry;
	DRM_INFO("pclk: %d\n", sum);
	it6161->mipi_rx_pclk = it6161->mipi_rx_rclk * 2048 / sum;
	//it6161->mipi_rx_pclk = it6161->mipi_rx_rclk * 1960 / sum;
	DRM_INFO("it6161->mipi_rx_pclk = %d.%03dMHz", it6161->mipi_rx_pclk / 1000, it6161->mipi_rx_pclk % 1000);
}

static void mipi_rx_show_mrec(struct it6161 *it6161)
{
	int m_hfront_porch, m_hsyncw, m_hback_porch, m_hactive, MHVR2nd, MHBlank;
	int m_vfront_porch, m_vsyncw, m_vback_porch, m_vactive, MVFP2nd, MVTotal;


	m_hfront_porch  = mipi_rx_read_word(it6161, 0x50) & 0x3FFF;
	m_hsyncw  = mipi_rx_read_word(it6161, 0x52) & 0x3FFF;
	m_hback_porch  = mipi_rx_read_word(it6161, 0x54) & 0x3FFF;
	m_hactive  = mipi_rx_read_word(it6161, 0x56) & 0x3FFF;
	MHVR2nd  = mipi_rx_read_word(it6161, 0x58) & 0x3FFF;

	MHBlank = m_hfront_porch + m_hsyncw + m_hback_porch;

	m_vfront_porch  = mipi_rx_read_word(it6161, 0x5A) & 0x3FFF;
	m_vsyncw  = mipi_rx_read_word(it6161, 0x5C) & 0x3FFF;
	m_vback_porch  = mipi_rx_read_word(it6161, 0x5E) & 0x3FFF;
	m_vactive  = mipi_rx_read_word(it6161, 0x60) & 0x3FFF;
	MVFP2nd  = mipi_rx_read_word(it6161, 0x62) & 0x3FFF;

	MVTotal = m_vfront_porch + m_vsyncw + m_vback_porch + m_vactive ;

	DRM_INFO("m_hfront_porch    = %d\n", m_hfront_porch);
	DRM_INFO("m_hsyncw    = %d\n", m_hsyncw);
	DRM_INFO("m_hback_porch    = %d\n", m_hback_porch);
	DRM_INFO("m_hactive   = %d\n", m_hactive);
	DRM_INFO("MHVR2nd = %d\n", MHVR2nd);
	DRM_INFO("MHBlank  = %d\n", MHBlank);

	DRM_INFO("m_vfront_porch    = %d\n", m_vfront_porch);
	DRM_INFO("m_vsyncw    = %d\n", m_vsyncw);
	DRM_INFO("m_vback_porch   = %d\n", m_vback_porch);
	DRM_INFO("m_vactive   = %d\n", m_vactive);
	DRM_INFO("MVFP2nd   = %d\n", MVFP2nd);
	DRM_INFO("MVTotal = %d\n", MVTotal);
}

static void mipi_rx_prec_get_display_mode(struct it6161 *it6161)
{
	struct drm_display_mode *display_mode = &it6161->mipi_rx_p_display_mode;
	struct device *dev = &it6161->i2c_hdmi_tx->dev;
	int p_hfront_porch, p_hsyncw, p_hback_porch, p_hactive, p_htotal;
	int p_vfront_porch, p_vsyncw, p_vback_porch, p_vactive, p_vtotal;

	p_hfront_porch  = mipi_rx_read_word(it6161, 0x30) & 0x3FFF;
	p_hsyncw  = mipi_rx_read_word(it6161, 0x32) & 0x3FFF;
	p_hback_porch  = mipi_rx_read_word(it6161, 0x34) & 0x3FFF;
	p_hactive  = mipi_rx_read_word(it6161, 0x36) & 0x3FFF;
	p_htotal  = mipi_rx_read_word(it6161, 0x38) & 0x3FFF;

	//p_htotal = p_hfront_porch + p_hsyncw + p_hback_porch + p_hactive ;

	p_vfront_porch  = mipi_rx_read_word(it6161, 0x3A) & 0x3FFF;
	p_vsyncw  = mipi_rx_read_word(it6161, 0x3C) & 0x3FFF;
	p_vback_porch  = mipi_rx_read_word(it6161, 0x3E) & 0x3FFF;
	p_vactive  = mipi_rx_read_word(it6161, 0x40) & 0x3FFF;
	p_vtotal  = mipi_rx_read_word(it6161, 0x42) & 0x3FFF;

	//p_vtotal = p_vfront_porch + p_vsyncw + p_vback_porch + p_vactive ;

    display_mode->clock = it6161->mipi_rx_pclk;
	display_mode->hdisplay = p_hactive;
	display_mode->hsync_start = p_hactive + p_hfront_porch;
	display_mode->hsync_end = p_hactive + p_hfront_porch + p_hsyncw;
	display_mode->htotal = p_htotal;
	display_mode->vdisplay = p_vactive;
	display_mode->vsync_start = p_vactive + p_vfront_porch;
	display_mode->vsync_end = p_vactive + p_vfront_porch + p_vsyncw;
	display_mode->vtotal = p_vtotal;

    DRM_DEV_DEBUG_DRIVER(dev, "mipi pixel clock: %d KHz", display_mode->clock);
	DRM_INFO("p_hfront_porch    = %d\r\n", p_hfront_porch);
	DRM_INFO("p_hsyncw    = %d\r\n", p_hsyncw);
	DRM_INFO("p_hback_porch   = %d\r\n", p_hback_porch);
	DRM_INFO("p_hactive   = %d\r\n", p_hactive);
	DRM_INFO("p_htotal = %d\r\n", p_htotal);

	DRM_INFO("p_vfront_porch    = %d\r\n", p_vfront_porch);
	DRM_INFO("p_vsyncw    = %d\r\n", p_vsyncw);
	DRM_INFO("p_vback_porch   = %d\r\n", p_vback_porch);
	DRM_INFO("p_vactive   = %d\r\n", p_vactive);
	DRM_INFO("p_vtotal = %d\r\n", p_vtotal);
}

static void mipi_rx_reset_p_domain(struct it6161 *it6161)
{
	it6161_mipi_rx_set_bits(it6161, 0x05, 0x04, 0x04);  // Video Clock Domain Reset
	it6161_mipi_rx_set_bits(it6161, 0x05, 0x04, 0x00);  // Release Video Clock Domain Reset
}

static void it6161_mipi_rx_interrupt_clear(struct it6161 *it6161, u8 reg06, u8 reg07, u8 reg08)
{
	it6161_mipi_rx_write(it6161, 0x06, reg06);
	it6161_mipi_rx_write(it6161, 0x07, reg07);
	it6161_mipi_rx_write(it6161, 0x08, reg08);
	it6161_debug("mipi rx i2c read reg06:0x%02x reg07:0x%02x reg08:0x%02x",
		 it6161_mipi_rx_read(it6161, 0x06), it6161_mipi_rx_read(it6161, 0x07), it6161_mipi_rx_read(it6161, 0x08));
}

static void it6161_mipi_rx_interrupt_reg06_process(struct it6161 *it6161, u8 reg06)
{
	bool m_video_stable, p_video_stable;
#ifndef __linux__
    struct drm_display_mode *dmt_display_mode;
#endif
    u8 data_id;

    if (reg06 == 0x00)
        return;

    if (reg06 & 0x01) {
    	m_video_stable = mipi_rx_get_m_video_stable(it6161);
        DRM_INFO("PPS M video stable Change Interrupt, %sstable", m_video_stable ? "" : "un");

        if (m_video_stable) {
            data_id = it6161_mipi_rx_read(it6161, 0x28);
            DRM_INFO("mipi receive video format: 0x%02x", data_id);
            mipi_rx_calc_rclk(it6161);
            mipi_rx_calc_mclk(it6161);
            mipi_rx_show_mrec(it6161);
            mipi_rx_afe_configuration(it6161, data_id);
            mipi_rx_reset_p_domain(it6161);            
        }
    }

    if(reg06 & 0x02)
    {
        DRM_INFO("PPS MHSync error interrupt");
    }

    if(reg06 & 0x04)
    {
        DRM_INFO("PPS MHDE Error Interrupt");
    }

    if(reg06 & 0x08)
    {
        DRM_INFO("PPS MVSync Error Interrupt");
    }

    if (reg06 & 0x10) {
    	p_video_stable = mipi_rx_get_p_video_stable(it6161);
        DRM_INFO("PPS P video stable Change Interrupt, %sstable",  p_video_stable ? "" : "un");
		it6161_debug("cancel restart work\n");
		cancel_delayed_work(&it6161->restart);

        if (p_video_stable) {
            DRM_INFO("PVidStb Change to HIGH");
            mipi_rx_calc_rclk(it6161);
            mipi_rx_calc_pclk(it6161);
            mipi_rx_prec_get_display_mode(it6161);
            it6161->vic = drm_match_cea_mode(&it6161->mipi_rx_p_display_mode);

#ifndef __linux__
            if (it6161->vic == 0) {
                dmt_display_mode = drm_match_dmt_mode(&it6161->mipi_rx_p_display_mode);

                if (dmt_display_mode)
                    it6161->source_display_mode = *dmt_display_mode;

                DRM_INFO("%sfind dmt timing", dmt_display_mode ? "" : "not ");
            } else {
                it6161->source_display_mode = edid_cea_modes[it6161->vic];
            }
#endif

            DRM_INFO("source output vic: %d, %s cea timing", it6161->vic, it6161->vic ? " standard" : " not");
            show_display_mode(it6161, &it6161->source_display_mode, 0);

            show_display_mode(it6161, &it6161->mipi_rx_p_display_mode, 2);
            mipi_rx_setup_polarity(it6161);
            it6161_mipi_rx_write(it6161, 0xC0,(EnTxCRC<<7) +TxCRCnum);
            // setup 1 sec timer interrupt
            it6161_mipi_rx_set_bits(it6161, 0x0b,0x40, 0x40);

			switch (it6161->hdmi_tx_mode) {
			case HDMI_TX_BY_PASS:
				it6161_hdmi_tx_set_bits(it6161, 0xA9, 0x80, 0x80);
				break;

			case HDMI_TX_ENABLE_DE_ONLY:
				hdmi_tx_generate_blank_timing(it6161);
				break;

			case HDMI_TX_ENABLE_PATTERN_GENERATOR:
				hdmi_tx_setup_pattern_generator(it6161);
				break;

			default:
				DRM_INFO("use hdmi tx normal mode");
				break;
			}

			hdmi_tx_video_reset(it6161);
        }
    }

    if(reg06 & 0x20)
    {
        if(DisPHSyncErr == false)
        {
            DRM_INFO("PPS PHSync Error Interrupt");
        }
    }

    if(reg06 & 0x40)
    {
        DRM_INFO("PPS PHDE Error Interrupt");
    }

    if(reg06 & 0x80)
    {
        DRM_INFO("PPS MVDE Error Interrupt");
    }
}

static void it6161_mipi_rx_interrupt_reg07_process(struct it6161 *it6161, u8 reg07)
{
    if (reg07 == 0x00)
        return;

    if(reg07 & 0x01)
    {
        DRM_INFO("PatGen PPGVidStb change interrupt !!!\n");
    }

    if(reg07 & 0x02)
    {
        DRM_INFO("PPS Data Byte Error Interrupt !!!\n");
    }

    if(reg07 & 0x04)
    {
        DRM_INFO("PPS CMOff Interrupt !!!\n");
    }

    if(reg07 & 0x08)
    {
        DRM_INFO("PPS CMOn Interrupt !!!\n");
    }

    if(reg07 & 0x10)
    {
        DRM_INFO("PPS ShutDone cmd Interrupt !!! \n");
    }

    if(reg07 & 0x20)
    {
        DRM_INFO("PPS TurnOn Interrupt !!!\n");
    }

    if((reg07 & 0x40) || (reg07 & 0x80))
    {
         if( reg07&0x40 ) {
             DRM_INFO("PPS FIFO over read Interrupt !!! tx video stable:%d", hdmi_tx_get_video_state(it6161));
             it6161_mipi_rx_set_bits(it6161, 0x07, 0x40, 0x40);
         }
         if( reg07&0x80 ) {
             DRM_INFO("PPS FIFO over write Interrupt !!!\n");
             it6161_mipi_rx_set_bits(it6161, 0x07, 0x80, 0x80);
         }
    }
}

static void it6161_mipi_rx_interrupt_reg08_process(struct it6161 *it6161, u8 reg08)
{
    int crc;

    if (reg08 == 0x00)
        return;

    if(reg08 & 0x01)
    {
        if(DisECCErr == false)
        {
            DRM_INFO("ECC 1-bit Error Interrupt !!!\n");
        }
    }

    if(reg08 & 0x02)
    {
        if(DisECCErr == false)
        {
            DRM_INFO("ECC 2-bit Error Interrupt !!!\n");
        }
    }

    if(reg08 & 0x04)
    {
        DRM_INFO("LM FIFO Error Interrupt !!!\n");
    }

    if(reg08 & 0x08)
    {
        DRM_INFO("CRC Error Interrupt !!!\n");
    }

    if(reg08 & 0x10)
    {
        DRM_INFO("MCLK Off Interrupt !!!\n");
        //DRM_INFO("setP1_6 High Mipi not Stable\n");
        //P1_6=1;
    }

    if(reg08 & 0x20)
    {
        DRM_INFO("PPI FIFO OverWrite Interrupt !!!\n");
    }

    if(reg08 & 0x40)
    {
        DRM_INFO("FW Timer Interrupt !!!\n");
        it6161_mipi_rx_set_bits(it6161, 0x0b, 0x40, 0x00);

        if((it6161_mipi_rx_read(it6161, 0xC1)&0x03) == 0x03)
        {
            DRM_INFO("CRC Fail !!!\n");
        }
        if((it6161_mipi_rx_read(it6161, 0xC1)&0x05) == 0x05)
        {
            DRM_INFO("CRC Pass !!!\n");
            crc = it6161_mipi_rx_read(it6161, 0xC2) + (it6161_mipi_rx_read(it6161, 0xC3) <<8);
            DRM_INFO("CRCR = 0x%x !!!\n" , crc);
            crc = it6161_mipi_rx_read(it6161, 0xC4) + (it6161_mipi_rx_read(it6161, 0xC5) <<8);
            DRM_INFO("CRCG = 0x%x !!!\n" , crc);
            crc = it6161_mipi_rx_read(it6161, 0xC6) + (it6161_mipi_rx_read(it6161, 0xC7) <<8);
            DRM_INFO("CRCB = 0x%x !!!\n" , crc);
        }
    }
}

static void it6161_hdmi_tx_interrupt_clear(struct it6161 *it6161, u8 reg06, u8 reg07, u8 reg08, u8 regee)
{
	u8 int_clear;

	if(reg06 & B_TX_INT_AUD_OVERFLOW) {
		DRM_INFO("B_TX_INT_AUD_OVERFLOW");
		it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST), (B_HDMITX_AUD_RST|B_TX_AREF_RST));
		it6161_hdmi_tx_set_bits(it6161, REG_TX_SW_RST, B_HDMITX_AUD_RST|B_TX_AREF_RST, 0x00);
		//AudioDelayCnt=AudioOutDelayCnt;
		//LastRefaudfreqnum=0;
	}

	if(reg06 & B_TX_INT_DDCFIFO_ERR) {
		DRM_INFO("DDC FIFO Error");
		it6161_hdmi_tx_clear_ddc_fifo(it6161);
		hdmiTxDev[0].bAuthenticated= false ;
	}

	if(reg06 & B_TX_INT_DDC_BUS_HANG) {
		DRM_INFO("DDC BUS HANG");
		it6161_hdmi_tx_abort_ddc(it6161);

        	if (hdmiTxDev[0].bAuthenticated) {
			DRM_INFO("when DDC hang,and aborted DDC,the HDCP authentication need to restart");
#ifndef _SUPPORT_HDCP_REPEATER_
#ifdef ENABLE_HDCP
			hdmitx_hdcp_ResumeAuthentication(it6161);
#endif
#else
			TxHDCP_chg(TxHDCP_AuthFail);
#endif
		}
	}

	/* clear ext interrupt */
	it6161_hdmi_tx_write(it6161, 0xEE, regee);
	it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR0, 0xFF);
	it6161_hdmi_tx_write(it6161, REG_TX_INT_CLR1, 0xFF);
	/* write B_TX_INTACTDONE '1' to trigger clear interrupt */
	int_clear = (it6161_hdmi_tx_read(it6161, REG_TX_SYS_STATUS)) | B_TX_CLR_AUD_CTS | B_TX_INTACTDONE ;
	it6161_hdmi_tx_write(it6161, REG_TX_SYS_STATUS, int_clear);

	it6161_debug("hdmi tx i2c read reg06:0x%02x reg07:0x%02x reg08:0x%02x regee:0x%02x", it6161_hdmi_tx_read(it6161, 0x06), it6161_hdmi_tx_read(it6161, 0x07), it6161_hdmi_tx_read(it6161, 0x08), it6161_hdmi_tx_read(it6161, 0xEE));
}

static void it6161_hdmi_tx_interrupt_reg06_process(struct it6161 *it6161, u8 reg06)
{
    //struct device *dev = &it6161->i2c_mipi_rx->dev;
	u8 ret;//, reg0e = it6161_hdmi_tx_read(it6161, 0x0e);

	if(reg06 & B_TX_INT_HPD_PLUG) {
		drm_helper_hpd_irq_event(it6161->connector.dev);
		if(hdmi_tx_get_sink_hpd(it6161)) {
			DRM_INFO("hpd on");
			ret = wait_for_completion_timeout(&it6161->wait_edid_complete, msecs_to_jiffies(2000));

			if (ret == 0)
				DRM_INFO("wait edid timeout");

			it6161->hdmi_tx_output_color_space = OUTPUT_COLOR_MODE;
			it6161->hdmi_tx_input_color_space = INPUT_COLOR_MODE;
			hdmi_tx_set_capability_from_edid_parse(it6161);
			reinit_completion(&it6161->wait_hdcp_event);
			hdmi_tx_video_reset(it6161);

			bChangeMode=true;
			// 1. not only HDMI but DVI need the set the upstream HPD
			// 2. Before set upstream HPD , the EDID must be ready.
		} else {
			DRM_INFO("hpd off");
			hdmi_tx_disable_video_output(it6161);
			kfree(it6161->edid);
			it6161->edid = NULL;

			if (it6161->hdmi_tx_mode == HDMI_TX_ENABLE_PATTERN_GENERATOR)
				hdmi_tx_disable_pattern_generator(it6161);
        }
    }

    if (reg06 & B_TX_INT_RX_SENSE) {
        DRM_INFO("rx sense interrupt");
        hdmiTxDev[0].bAuthenticated = false;
    }
}

static void it6161_hdmi_tx_interrupt_reg07_process(struct it6161 *it6161, u8 reg07)
{
	bool video_state = hdmi_tx_get_video_state(it6161);

	if(reg07 & B_TX_INT_AUTH_DONE) {
		DRM_INFO("hdmi tx authenticate done interrupt");
		hdmi_tx_hdcp_int_mask_disable(it6161);
		hdmiTxDev[0].bAuthenticated = true ;
		it6161_hdmi_tx_set_av_mute(it6161, false);
		complete(&it6161->wait_hdcp_event);
	}
	if(reg07 & B_TX_INT_AUTH_FAIL) {
		hdmiTxDev[0].bAuthenticated = false;
		DRM_INFO("hdmi tx interrupt authenticate fail reg46:0x%02x, start HDCP again", it6161_hdmi_tx_read(it6161, 0x46));
		complete(&it6161->wait_hdcp_event);
#ifdef ENABLE_HDCP
		hdmi_tx_enable_hdcp(it6161);
#ifdef _SUPPORT_HDCP_REPEATER_
		TxHDCP_chg(TxHDCP_AuthFail) ;
#endif
#endif
	}

	if(reg07 & B_TX_INT_KSVLIST_CHK ) {
		DRM_INFO("ksv list event interrupt");
		schedule_work(&it6161->wait_hdcp_ksv_list);
	}

	if (reg07 & B_TX_INT_VID_UNSTABLE) {
		if (!video_state)
			DRM_INFO("hdmi tx interrupt video unstable!");
	}
}

static void it6161_hdmi_tx_interrupt_reg08_process(struct it6161 *it6161, u8 reg08)
{
	if (reg08 & B_TX_INT_VIDSTABLE) {
		it6161_hdmi_tx_write(it6161, REG_TX_INT_STAT3, reg08);
			if (hdmi_tx_get_video_state(it6161)) {
				DRM_INFO("hdmi tx interrupt video stable link status:%d, rx reg0d:0x%02x start HDCP", getHDMITX_LinkStatus(), it6161_mipi_rx_read(it6161, 0x0D));
				hdmi_tx_get_display_mode(it6161);
                show_display_mode(it6161, &it6161->source_display_mode, 0);
                show_display_mode(it6161, &it6161->hdmi_tx_display_mode, 1);
                hdmi_tx_set_output_process(it6161);
#ifdef ENABLE_HDCP
				hdmi_tx_enable_hdcp(it6161);
#endif
			}
	}
}

static void it6161_hdmi_tx_interrupt_regee_process(struct it6161 *it6161, u8 regee)
{
	if (regee != 0x00) {
		DRM_INFO("%s%s%s%s%s%s%s",
			(regee & 0x40) ? "video parameter change ":"",
			(regee & 0x20) ? "HDCP Pj check done ":"",
			(regee & 0x10) ? "HDCP Ri check done ":"",
			(regee & 0x8) ? "DDC bus hang ":"",
			(regee & 0x4) ? "Video input FIFO auto reset ":"",
			(regee & 0x2) ? "No audio input interrupt  ":"",
			(regee & 0x1) ? "Audio decode error interrupt ":"");
	}
}

static irqreturn_t it6161_intp_threaded_handler(int unused, void *data)
{
	struct it6161 *it6161 = data;
	//struct device *dev = &it6161->i2c_mipi_rx->dev;
	u8 mipi_rx_reg06, mipi_rx_reg07, mipi_rx_reg08, mipi_rx_reg0d;
	u8 hdmi_tx_reg06, hdmi_tx_reg07, hdmi_tx_reg08, hdmi_tx_regee, hdmi_tx_reg0e;
	//it6161_dump = data;

	if (it6161->enable_drv_hold)
		goto unlock;

	mipi_rx_reg06 = it6161_mipi_rx_read(it6161, 0x06);
	mipi_rx_reg07 = it6161_mipi_rx_read(it6161, 0x07);
	mipi_rx_reg08 = it6161_mipi_rx_read(it6161, 0x08);
	mipi_rx_reg0d = it6161_mipi_rx_read(it6161, 0x0D);

	hdmi_tx_reg06 = it6161_hdmi_tx_read(it6161, 0x06);
	hdmi_tx_reg07 = it6161_hdmi_tx_read(it6161, 0x07);
	hdmi_tx_reg08 = it6161_hdmi_tx_read(it6161, 0x08);
	hdmi_tx_reg0e = it6161_hdmi_tx_read(it6161, 0x0E);
	hdmi_tx_regee = it6161_hdmi_tx_read(it6161, 0xEE);

    if ((mipi_rx_reg06 != 0) || (mipi_rx_reg07 != 0) || (mipi_rx_reg08 != 0)) {
        it6161_debug("rx reg06: 0x%02x reg07:0x%02x reg08:0x%02x reg0d:0x%02x", mipi_rx_reg06, mipi_rx_reg07, mipi_rx_reg08, mipi_rx_reg0d);
        it6161_mipi_rx_interrupt_clear(it6161, mipi_rx_reg06, mipi_rx_reg07, mipi_rx_reg08);
    }
	
    if ((hdmi_tx_reg06 != 0) || (hdmi_tx_reg07 != 0) || (hdmi_tx_reg08 != 0)) {
        it6161_debug("tx reg06: 0x%02x reg07: 0x%02x reg08: 0x%02x reg0e: 0x%02x regee: 0x%02x", hdmi_tx_reg06, hdmi_tx_reg07, hdmi_tx_reg08, hdmi_tx_reg0e, hdmi_tx_regee);
        it6161_hdmi_tx_interrupt_clear(it6161, hdmi_tx_reg06, hdmi_tx_reg07, hdmi_tx_reg08, hdmi_tx_regee);
    }

    it6161_mipi_rx_interrupt_reg08_process(it6161, mipi_rx_reg08);
	it6161_mipi_rx_interrupt_reg06_process(it6161, mipi_rx_reg06);
	it6161_mipi_rx_interrupt_reg07_process(it6161, mipi_rx_reg07);
	it6161_hdmi_tx_interrupt_reg06_process(it6161, hdmi_tx_reg06);
	it6161_hdmi_tx_interrupt_reg07_process(it6161, hdmi_tx_reg07);
	it6161_hdmi_tx_interrupt_reg08_process(it6161, hdmi_tx_reg08);
	it6161_hdmi_tx_interrupt_regee_process(it6161, hdmi_tx_regee);
	it6161_debug("end %s", __func__);

unlock:
	return IRQ_HANDLED;
}

static void mipirx_restart(struct work_struct *work)
{
	it6161_debug("****it6161: %s\n", __func__);
	it6161_bridge_enable(it6161_bridge);
}

#if 0
static ssize_t enable_drv_hold_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "drv_hold: %d\n", it6161->enable_drv_hold);
}

static ssize_t enable_drv_hold_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);
	unsigned int drv_hold;

	if (kstrtoint(buf, 10, &drv_hold) < 0)
		return -EINVAL;

	it6161->enable_drv_hold = !!drv_hold;

	if (it6161->enable_drv_hold) {
		it6161_mipi_rx_int_mask_disable(it6161);
		it6161_hdmi_tx_int_mask_disable(it6161);
	} else {
		it6161_mipi_rx_interrupt_clear(it6161, 0xFF, 0xFF, 0xFF);
		it6161_hdmi_tx_interrupt_clear(it6161, 0xFF, 0xFF, 0xFF, 0xFF);
		it6161_mipi_rx_int_mask_enable(it6161);
		it6161_hdmi_tx_int_mask_enable(it6161);
	}
	return count;
}

static ssize_t hdmi_output_color_space_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);

	DRM_INFO("config color space: %s", buf);
	it6161->hdmi_tx_output_color_space &= ~F_MODE_CLRMOD_MASK;

	if (strncmp(buf, "ycbcr444", strlen(buf) - 1) == 0 || strncmp(buf, "yuv444", strlen(buf) - 1) == 0) {
		it6161->hdmi_tx_output_color_space |= F_MODE_YUV444;
		goto end;
	}

	if (strncmp(buf, "ycbcr422", strlen(buf) - 1) == 0 || strncmp(buf, "yuv422", strlen(buf) - 1) == 0) {
		it6161->hdmi_tx_output_color_space |= F_MODE_YUV422;
		goto end;
	}

	if (strncmp(buf, "rgb444", strlen(buf) - 1) == 0) {
		it6161->hdmi_tx_output_color_space |= F_MODE_RGB444;
		goto end;
	}

	DRM_INFO("not support this color space, only support ycbcr444/yuv444, ycbcr422/yuv422, rgb444");
	return count;

end:
	DRM_INFO("config color space: %s value:0x%02x", buf, it6161->hdmi_tx_output_color_space);
	return count;
}

static ssize_t hdmi_output_color_space_show(struct device *dev,
					    struct device_attribute *attr, char *buf)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);
	char *str = buf, *end = buf + PAGE_SIZE;

	str += scnprintf(str, end - str, "it6161->hdmi_tx_output_color_space:%d\n", it6161->hdmi_tx_output_color_space);

	return str - buf;
}
#endif

#if 0
static ssize_t print_timing_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);
	struct drm_display_mode *vid = &it6161->source_display_mode;
	char *str = buf, *end = buf + PAGE_SIZE;

	str += scnprintf(str, end - str, "---video timing---\n");
	str += scnprintf(str, end - str, "PCLK:%d.%03dMHz\n", vid->clock / 1000,
			 vid->clock % 1000);
	str += scnprintf(str, end - str, "HTotal:%d\n", vid->htotal);
	str += scnprintf(str, end - str, "HActive:%d\n", vid->hdisplay);
	str += scnprintf(str, end - str, "HFrontPorch:%d\n",
			 vid->hsync_start - vid->hdisplay);
	str += scnprintf(str, end - str, "HSyncWidth:%d\n",
			 vid->hsync_end - vid->hsync_start);
	str += scnprintf(str, end - str, "HBackPorch:%d\n",
			 vid->htotal - vid->hsync_end);
	str += scnprintf(str, end - str, "VTotal:%d\n", vid->vtotal);
	str += scnprintf(str, end - str, "VActive:%d\n", vid->vdisplay);
	str += scnprintf(str, end - str, "VFrontPorch:%d\n",
			 vid->vsync_start - vid->vdisplay);
	str += scnprintf(str, end - str, "VSyncWidth:%d\n",
			 vid->vsync_end - vid->vsync_start);
	str += scnprintf(str, end - str, "VBackPorch:%d\n",
			 vid->vtotal - vid->vsync_end);

	return str - buf;
}

static ssize_t sha_debug_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int i = 0;
	char *str = buf, *end = buf + PAGE_SIZE;
	struct it6161 *it6161 = dev_get_drvdata(dev);

	str += scnprintf(str, end - str, "sha input:\n");
	for (i = 0; i < ARRAY_SIZE(it6161->sha1_input); i += 16)
		str += scnprintf(str, end - str, "%16ph\n",
				 it6161->sha1_input + i);

	str += scnprintf(str, end - str, "av:\n");
	for (i = 0; i < ARRAY_SIZE(it6161->av); i++)
		str += scnprintf(str, end - str, "%4ph\n", it6161->av[i]);

	str += scnprintf(str, end - str, "bv:\n");
	for (i = 0; i < ARRAY_SIZE(it6161->bv); i++)
		str += scnprintf(str, end - str, "%4ph\n", it6161->bv[i]);

	return end - str;
}

static ssize_t enable_hdcp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", it6161->enable_hdcp);
}

static ssize_t enable_hdcp_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);
	unsigned int reg3f, hdcp;

	if (kstrtoint(buf, 10, &hdcp) < 0)
		return -EINVAL;

	if (!it6161->powered || it6161->state == SYS_UNPLUG) {
		DRM_DEV_DEBUG_DRIVER(dev,
				     "power down or unplug, can not fire HDCP");
		return -EINVAL;
	}
	it6161->enable_hdcp = hdcp ? true : false;

	if (it6161->enable_hdcp) {
		if (it6161->cp_capable) {
			dptx_sys_chg(it6161, SYS_HDCP);
			dptx_sys_fsm(it6161);
		} else {
			DRM_DEV_ERROR(dev, "sink not support HDCP");
		}
	} else {
		dptx_set_bits(it6161, 0x05, 0x10, 0x10);
		dptx_set_bits(it6161, 0x05, 0x10, 0x00);
		reg3f = dptx_read(it6161, 0x3F);
		hdcp = (reg3f & BIT(7)) >> 7;
		DRM_DEV_DEBUG_DRIVER(dev, "%s to disable hdcp",
				     hdcp ? "failed" : "succeeded");
	}
	return count;
}

static ssize_t force_pwronoff_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);
	int pwr;

	if (kstrtoint(buf, 10, &pwr) < 0)
		return -EINVAL;
	if (pwr)
		it6161_poweron(it6161);
	else
		it6161_poweroff(it6161);
	return count;
}

static ssize_t pwr_state_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct it6161 *it6161 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", it6161->powered);
}

static DEVICE_ATTR_RO(print_timing);
static DEVICE_ATTR_RO(pwr_state);
static DEVICE_ATTR_RO(sha_debug);
static DEVICE_ATTR_WO(force_pwronoff);
static DEVICE_ATTR_RW(enable_hdcp);

static const struct attribute *it6161_attrs[] = {
	&dev_attr_enable_drv_hold.attr,
	&dev_attr_print_timing.attr,
	&dev_attr_sha_debug.attr,
	&dev_attr_enable_hdcp.attr,
	&dev_attr_force_pwronoff.attr,
	&dev_attr_pwr_state.attr,
	NULL,
};

static void it6161_shutdown(struct i2c_client *i2c_mipi_rx)
{
	struct it6161 *it6161 = dev_get_drvdata(&i2c_mipi_rx->dev);

	dptx_sys_chg(it6161, SYS_UNPLUG);
}

#endif

#if 0
static DEVICE_ATTR_RW(enable_drv_hold);
static DEVICE_ATTR_RW(hdmi_output_color_space);

static const struct attribute *it6161_attrs[] = {
	&dev_attr_enable_drv_hold.attr,
	&dev_attr_hdmi_output_color_space.attr,
	NULL,
};
#endif
static int it6161_parse_dt(struct it6161 *it6161, struct device_node *np)
{
    //struct device *dev = &adv->i2c_mipi_rx->dev;

    it6161->host_node = of_graph_get_remote_node(np, 0, 0);
    if (!it6161->host_node) {
        DRM_INFO("no host node");
        return -ENODEV;
    }
	DRM_INFO("%s", __func__);
    of_node_put(it6161->host_node);

    return 0;
}

static int it6161_gpio_init(struct it6161 *it6161)
{
	struct device *dev = it6161->dev;

	it6161->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(it6161->enable_gpio)) {
		dev_err(dev, "failed to acquire enable gpio\n");
		return PTR_ERR(it6161->enable_gpio);
	}

	it6161->test_gpio = devm_gpiod_get_optional(dev, "test",
						      GPIOD_OUT_LOW);
	if (IS_ERR(it6161->test_gpio)) {
		dev_info(dev, "failed to acquire test gpio\n");
	}

	msleep(20);
	gpiod_set_value_cansleep(it6161->enable_gpio, 1);
	msleep(20);

	return 0;
}

static int it6161_i2c_probe(struct i2c_client *i2c_mipi_rx,
			    const struct i2c_device_id *id)
{
	//struct it6161 *it6161;
	struct device *dev = &i2c_mipi_rx->dev;
	int err, intp_irq;

	it6161 = devm_kzalloc(dev, sizeof(*it6161), GFP_KERNEL);
	if (!it6161)
		return -ENOMEM;

	it6161_bridge = devm_kzalloc(dev, sizeof(*it6161), GFP_KERNEL);
	if (!it6161_bridge)
		return -ENOMEM;

	it6161->i2c_mipi_rx = i2c_mipi_rx;
	it6161->dev = &i2c_mipi_rx->dev;
	// mutex_init(&it6161->lock);
	mutex_init(&it6161->mode_lock);
	//init_completion(&it6161->wait_hdcp_event);
	init_completion(&it6161->wait_edid_complete);

	//INIT_DELAYED_WORK(&it6161->hdcp_work, hdmi_tx_hdcp_work);
	//INIT_WORK(&it6161->wait_hdcp_ksv_list, hdmi_tx_hdcp_auth_part2_process);
	// init_waitqueue_head(&it6161->edid_wait);

	/* set up mipirx restart work*/
	INIT_DELAYED_WORK(&it6161->restart, mipirx_restart);

	it6161->bridge.of_node = i2c_mipi_rx->dev.of_node;

	it6161_parse_dt(it6161, dev->of_node);
	it6161_gpio_init(it6161);

	it6161->regmap_mipi_rx =
		devm_regmap_init_i2c(i2c_mipi_rx, &it6161_mipi_rx_bridge_regmap_config);
	if (IS_ERR(it6161->regmap_mipi_rx)) {
		DRM_DEV_ERROR(dev, "regmap_mipi_rx i2c init failed");
		return PTR_ERR(it6161->regmap_mipi_rx);
	}

	if (device_property_read_u32(dev, "it6161-addr-hdmi-tx", &it6161->it6161_addr_hdmi_tx) < 0)
		it6161->it6161_addr_hdmi_tx = 0x4c;

	it6161->i2c_hdmi_tx = i2c_new_dummy_device(i2c_mipi_rx->adapter, it6161->it6161_addr_hdmi_tx);
	if (IS_ERR(it6161->i2c_hdmi_tx)) {
		DRM_DEV_ERROR(dev, "Failed to register it6161 I2C device\n");
		return PTR_ERR(it6161->i2c_hdmi_tx);
	}

	it6161->regmap_hdmi_tx =
		devm_regmap_init_i2c(it6161->i2c_hdmi_tx, &it6161_hdmi_tx_bridge_regmap_config);

	if (IS_ERR(it6161->regmap_hdmi_tx)) {
		DRM_DEV_ERROR(dev, "regmap_hdmi_tx i2c init failed");
		err = PTR_ERR(it6161->regmap_hdmi_tx);
		goto err_hdmitx;
	}

	if (device_property_read_u32(dev, "it6161-addr-cec", &it6161->it6161_addr_cec) < 0)
		it6161->it6161_addr_cec = 0x4E;

	it6161->i2c_cec = i2c_new_dummy_device(i2c_mipi_rx->adapter, it6161->it6161_addr_cec);
	if (IS_ERR(it6161->i2c_cec)) {
		DRM_DEV_ERROR(dev, "i2c_cec init failed");
		err = PTR_ERR(it6161->i2c_cec);
		goto  err_hdmitx;
	}

	it6161->regmap_cec =
		devm_regmap_init_i2c(it6161->i2c_cec, &it6161_cec_bridge_regmap_config);
	
	if (IS_ERR(it6161->regmap_cec)) {
		DRM_DEV_ERROR(dev, "regmap_cec i2c init failed");
		err = PTR_ERR(it6161->regmap_cec);
		goto err_cec;
	}

	if (!it6161_check_device_ready(it6161)) {
		err =  -ENODEV;
		goto err_cec;
	}

	it6161->enable_drv_hold = DEFAULT_DRV_HOLD;
	it6161_set_interrupts_active_level(HIGH);

	intp_irq = i2c_mipi_rx->irq;

	if (!intp_irq) {
		DRM_DEV_ERROR(dev, "it6112 failed to get INTP IRQ");
		err = -ENODEV;
		goto err_cec;
	}

	err = devm_request_threaded_irq(&i2c_mipi_rx->dev, intp_irq, NULL,
					/*it6161_intp_threaded_handler,*/it6161_intp_threaded_handler,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"it6161-intp", it6161);
	if (err) {
		DRM_DEV_ERROR(dev, "it6112 failed to request INTP threaded IRQ: %d",
			      err);
		goto err_cec;
	}

	i2c_set_clientdata(i2c_mipi_rx, it6161);
	it6161->bridge.funcs = &it6161_bridge_funcs;
	drm_bridge_add(&it6161->bridge);

	return 0;

err_cec:
	i2c_unregister_device(it6161->i2c_cec);
err_hdmitx:
	i2c_unregister_device(it6161->i2c_hdmi_tx);

	return err;
}

#if 0
static int it6161_remove(struct i2c_client *i2c_mipi_rx)
{
	struct it6161 *it6161 = i2c_get_clientdata(i2c_mipi_rx);

	drm_connector_unregister(&it6161->connector);
	drm_connector_cleanup(&it6161->connector);
	drm_bridge_remove(&it6161->bridge);
	// sysfs_remove_files(&i2c_mipi_rx->dev.kobj, it6161_attrs);
	// drm_dp_aux_unregister(&it6161->aux);
	// it6161_poweroff(it6161);
	return 0;
}
#endif
static const struct i2c_device_id it6161_id[] = {
	{ "it6161", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, it6161_id);

static const struct of_device_id it6161_of_match[] = {
	{ .compatible = "ite,it6161" },
	{ }
};

static struct i2c_driver it6161_i2c_driver = {
	.driver = {
		.name = "it6161_mipirx_hdmitx",
		.of_match_table = it6161_of_match,
		//.pm = &it6161_bridge_pm_ops,
	},
	.probe = it6161_i2c_probe,
	//.remove = it6161_remove,
	//.shutdown = it6161_shutdown,
	.id_table = it6161_id,
};

module_i2c_driver(it6161_i2c_driver);

MODULE_AUTHOR("allen chen <allen.chen@ite.com.tw>");
MODULE_DESCRIPTION("it6161 HDMI Transmitter driver");
MODULE_LICENSE("GPL v2");
