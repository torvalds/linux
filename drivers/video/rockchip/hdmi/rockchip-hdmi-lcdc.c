#include "rockchip-hdmi.h"

static const struct hdmi_video_timing hdmi_mode[] = {
	{
		.mode = {
			.name = "720x480i@60Hz",
			.refresh = 60,
			.xres = 720,
			.yres = 480,
			.pixclock = 27000000,
			.left_margin = 57,
			.right_margin = 19,
			.upper_margin = 15,
			.lower_margin = 4,
			.hsync_len = 62,
			.vsync_len = 3,
			.sync = 0,
			.vmode = FB_VMODE_INTERLACED,
			.flag = 0,
		},
		.vic = HDMI_720X480I_60HZ_4_3,
		.vic_2nd = HDMI_720X480I_60HZ_16_9,
		.pixelrepeat = 2,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "720x576i@50Hz",
			.refresh = 50,
			.xres = 720,
			.yres = 576,
			.pixclock = 27000000,
			.left_margin = 69,
			.right_margin = 12,
			.upper_margin = 19,
			.lower_margin = 2,
			.hsync_len = 63,
			.vsync_len = 3,
			.sync = 0,
			.vmode = FB_VMODE_INTERLACED,
			.flag = 0,
		},
		.vic = HDMI_720X576I_50HZ_4_3,
		.vic_2nd = HDMI_720X576I_50HZ_16_9,
		.pixelrepeat = 2,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "720x480p@60Hz",
			.refresh = 60,
			.xres = 720,
			.yres = 480,
			.pixclock = 27000000,
			.left_margin = 60,
			.right_margin = 16,
			.upper_margin = 30,
			.lower_margin = 9,
			.hsync_len = 62,
			.vsync_len = 6,
			.sync = 0,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_720X480P_60HZ_4_3,
		.vic_2nd = HDMI_720X480P_60HZ_16_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "720x576p@50Hz",
			.refresh = 50,
			.xres = 720,
			.yres = 576,
			.pixclock = 27000000,
			.left_margin = 68,
			.right_margin = 12,
			.upper_margin = 39,
			.lower_margin = 5,
			.hsync_len = 64,
			.vsync_len = 5,
			.sync = 0,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_720X576P_50HZ_4_3,
		.vic_2nd = HDMI_720X576P_50HZ_16_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x720p@24Hz",
			.refresh = 24,
			.xres = 1280,
			.yres = 720,
			.pixclock = 59400000,
			.left_margin = 220,
			.right_margin = 1760,
			.upper_margin = 20,
			.lower_margin = 5,
			.hsync_len = 40,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1280X720P_24HZ,
		.vic_2nd = HDMI_1280X720P_24HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x720p@25Hz",
			.refresh = 25,
			.xres = 1280,
			.yres = 720,
			.pixclock = 74250000,
			.left_margin = 220,
			.right_margin = 2420,
			.upper_margin = 20,
			.lower_margin = 5,
			.hsync_len = 40,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1280X720P_25HZ,
		.vic_2nd = HDMI_1280X720P_25HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x720p@30Hz",
			.refresh = 30,
			.xres = 1280,
			.yres = 720,
			.pixclock = 74250000,
			.left_margin = 220,
			.right_margin = 1760,
			.upper_margin = 20,
			.lower_margin = 5,
			.hsync_len = 40,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1280X720P_30HZ,
		.vic_2nd = HDMI_1280X720P_30HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x720p@50Hz",
			.refresh = 50,
			.xres = 1280,
			.yres = 720,
			.pixclock = 74250000,
			.left_margin = 220,
			.right_margin = 440,
			.upper_margin = 20,
			.lower_margin = 5,
			.hsync_len = 40,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1280X720P_50HZ,
		.vic_2nd = HDMI_1280X720P_50HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x720p@60Hz",
			.refresh = 60,
			.xres = 1280,
			.yres = 720,
			.pixclock = 74250000,
			.left_margin = 220,
			.right_margin = 110,
			.upper_margin = 20,
			.lower_margin = 5,
			.hsync_len = 40,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1280X720P_60HZ,
		.vic_2nd = HDMI_1280X720P_60HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080i@50Hz",
			.refresh = 50,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 74250000,
			.left_margin = 148,
			.right_margin = 528,
			.upper_margin = 15,
			.lower_margin = 2,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = FB_VMODE_INTERLACED,
			.flag = 0,
		},
		.vic = HDMI_1920X1080I_50HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080i@60Hz",
			.refresh = 60,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 74250000,
			.left_margin = 148,
			.right_margin = 88,
			.upper_margin = 15,
			.lower_margin = 2,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = FB_VMODE_INTERLACED,
			.flag = 0,
		},
		.vic = HDMI_1920X1080I_60HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080p@24Hz",
			.refresh = 24,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 74250000,
			.left_margin = 148,
			.right_margin = 638,
			.upper_margin = 36,
			.lower_margin = 4,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1920X1080P_24HZ,
		.vic_2nd = HDMI_1920X1080P_24HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080p@25Hz",
			.refresh = 25,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 74250000,
			.left_margin = 148,
			.right_margin = 528,
			.upper_margin = 36,
			.lower_margin = 4,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1920X1080P_25HZ,
		.vic_2nd = HDMI_1920X1080P_25HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080p@30Hz",
			.refresh = 30,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 74250000,
			.left_margin = 148,
			.right_margin = 88,
			.upper_margin = 36,
			.lower_margin = 4,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1920X1080P_30HZ,
		.vic_2nd = HDMI_1920X1080P_30HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080p@50Hz",
			.refresh = 50,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 148500000,
			.left_margin = 148,
			.right_margin = 528,
			.upper_margin = 36,
			.lower_margin = 4,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1920X1080P_50HZ,
		.vic_2nd = HDMI_1920X1080P_50HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1920x1080p@60Hz",
			.refresh = 60,
			.xres = 1920,
			.yres = 1080,
			.pixclock = 148500000,
			.left_margin = 148,
			.right_margin = 88,
			.upper_margin = 36,
			.lower_margin = 4,
			.hsync_len = 44,
			.vsync_len = 5,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_1920X1080P_60HZ,
		.vic_2nd = HDMI_1920X1080P_60HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "3840x2160p@24Hz",
			.refresh = 24,
			.xres = 3840,
			.yres = 2160,
			.pixclock = 297000000,
			.left_margin = 296,
			.right_margin = 1276,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_3840X2160P_24HZ,
		.vic_2nd = HDMI_3840X2160P_24HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "3840x2160p@25Hz",
			.refresh = 25,
			.xres = 3840,
			.yres = 2160,
			.pixclock = 297000000,
			.left_margin = 296,
			.right_margin = 1056,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_3840X2160P_25HZ,
		.vic_2nd = HDMI_3840X2160P_25HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "3840x2160p@30Hz",
			.refresh = 30,
			.xres = 3840,
			.yres = 2160,
			.pixclock = 297000000,
			.left_margin = 296,
			.right_margin = 176,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_3840X2160P_30HZ,
		.vic_2nd = HDMI_3840X2160P_30HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "4096x2160p@24Hz",
			.refresh = 24,
			.xres = 4096,
			.yres = 2160,
			.pixclock = 297000000,
			.left_margin = 296,
			.right_margin = 1020,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_4096X2160P_24HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "4096x2160p@25Hz",
			.refresh = 25,
			.xres = 4096,
			.yres = 2160,
			.pixclock = 297000000,
			.left_margin = 128,
			.right_margin = 968,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_4096X2160P_25HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "4096x2160p@30Hz",
			.refresh = 30,
			.xres = 4096,
			.yres = 2160,
			.pixclock = 297000000,
			.left_margin = 128,
			.right_margin = 88,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_4096X2160P_30HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "3840x2160p@50Hz",
			.refresh = 50,
			.xres = 3840,
			.yres = 2160,
			.pixclock = 594000000,
			.left_margin = 296,
			.right_margin = 1056,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_3840X2160P_50HZ,
		.vic_2nd = HDMI_3840X2160P_50HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "3840x2160p@60Hz",
			.refresh = 60,
			.xres = 3840,
			.yres = 2160,
			.pixclock = 594000000,
			.left_margin = 296,
			.right_margin = 176,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_3840X2160P_60HZ,
		.vic_2nd = HDMI_3840X2160P_60HZ_21_9,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "4096x2160p@50Hz",
			.refresh = 50,
			.xres = 4096,
			.yres = 2160,
			.pixclock = 594000000,
			.left_margin = 128,
			.right_margin = 968,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_4096X2160P_50HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "4096x2160p@60Hz",
			.refresh = 60,
			.xres = 4096,
			.yres = 2160,
			.pixclock = 594000000,
			.left_margin = 128,
			.right_margin = 88,
			.upper_margin = 72,
			.lower_margin = 8,
			.hsync_len = 88,
			.vsync_len = 10,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_4096X2160P_60HZ,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "800x600p@60Hz",
			.refresh = 60,
			.xres = 800,
			.yres = 600,
			.pixclock = 40000000,
			.left_margin = 88,
			.right_margin = 40,
			.upper_margin = 23,
			.lower_margin = 1,
			.hsync_len = 128,
			.vsync_len = 4,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 1,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1024x768p@60Hz",
			.refresh = 60,
			.xres = 1024,
			.yres = 768,
			.pixclock = 65000000,
			.left_margin = 160,
			.right_margin = 24,
			.upper_margin = 29,
			.lower_margin = 3,
			.hsync_len = 136,
			.vsync_len = 6,
			.sync = 0,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 2,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x960p@60Hz",
			.refresh = 60,
			.xres = 1280,
			.yres = 960,
			.pixclock = 108000000,
			.left_margin = 312,
			.right_margin = 96,
			.upper_margin = 36,
			.lower_margin = 1,
			.hsync_len = 112,
			.vsync_len = 3,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 3,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1280x1024p@60Hz",
			.refresh = 60,
			.xres = 1280,
			.yres = 1024,
			.pixclock = 108000000,
			.left_margin = 248,
			.right_margin = 48,
			.upper_margin = 38,
			.lower_margin = 1,
			.hsync_len = 112,
			.vsync_len = 3,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 4,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1360x768p@60Hz",
			.refresh = 60,
			.xres = 1360,
			.yres = 768,
			.pixclock = 85500000,
			.left_margin = 256,
			.right_margin = 64,
			.upper_margin = 18,
			.lower_margin = 3,
			.hsync_len = 112,
			.vsync_len = 6,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 5,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1366x768p@60Hz",
			.refresh = 60,
			.xres = 1366,
			.yres = 768,
			.pixclock = 85500000,
			.left_margin = 213,
			.right_margin = 70,
			.upper_margin = 24,
			.lower_margin = 3,
			.hsync_len = 143,
			.vsync_len = 3,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 6,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1440x900p@60Hz",
			.refresh = 60,
			.xres = 1440,
			.yres = 768,
			.pixclock = 106500000,
			.left_margin = 232,
			.right_margin = 80,
			.upper_margin = 25,
			.lower_margin = 3,
			.hsync_len = 152,
			.vsync_len = 6,
			.sync = FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 7,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1600x900p@60Hz",
			.refresh = 60,
			.xres = 1600,
			.yres = 900,
			.pixclock = 108000000,
			.left_margin = 96,
			.right_margin = 24,
			.upper_margin = 96,
			.lower_margin = 1,
			.hsync_len = 80,
			.vsync_len = 3,
			.sync = FB_SYNC_VERT_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 8,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
	{
		.mode = {
			.name = "1680x1050@60Hz",
			.refresh = 60,
			.xres = 1680,
			.yres = 1050,
			.pixclock = 146250000,
			.left_margin = 280,
			.right_margin = 104,
			.upper_margin = 30,
			.lower_margin = 3,
			.hsync_len = 176,
			.vsync_len = 6,
			.sync = FB_SYNC_VERT_HIGH_ACT,
			.vmode = 0,
			.flag = 0,
		},
		.vic = HDMI_VIDEO_DMT | 9,
		.vic_2nd = 0,
		.pixelrepeat = 1,
		.interface = OUT_P888,
	},
};

static int hdmi_set_info(struct rk_screen *screen, struct hdmi *hdmi)
{
	int i, vic;
	struct fb_videomode *mode;

	if (screen == NULL || hdmi == NULL)
		return HDMI_ERROR_FALSE;

	if (hdmi->vic == 0)
		hdmi->vic = hdmi->property->defaultmode;

	if (hdmi->vic & HDMI_VIDEO_DMT)
		vic = hdmi->vic;
	else
		vic = hdmi->vic & HDMI_VIC_MASK;
	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic == vic ||
		    hdmi_mode[i].vic_2nd == vic)
			break;
	}
	if (i == ARRAY_SIZE(hdmi_mode))
		return HDMI_ERROR_FALSE;

	memset(screen, 0, sizeof(struct rk_screen));

	/* screen type & face */
	screen->type = SCREEN_HDMI;
	if (hdmi->video.color_input == HDMI_COLOR_RGB_0_255)
		screen->color_mode = COLOR_RGB;
	else
		screen->color_mode = COLOR_YCBCR;
	if (hdmi->vic & HDMI_VIDEO_YUV420) {
		if (hdmi->video.color_output_depth == 10)
			screen->face = OUT_YUV_420_10BIT;
		else
			screen->face = OUT_YUV_420;
	} else {
		if (hdmi->video.color_output_depth == 10)
			screen->face = OUT_P101010;
		else
			screen->face = hdmi_mode[i].interface;
	}
	screen->pixelrepeat = hdmi_mode[i].pixelrepeat - 1;
	mode = (struct fb_videomode *)&(hdmi_mode[i].mode);

	screen->mode = *mode;
	if (hdmi->video.format_3d == HDMI_3D_FRAME_PACKING) {
		screen->mode.pixclock = 2 * mode->pixclock;
		if (mode->vmode == 0) {
			screen->mode.yres = 2 * mode->yres +
					mode->upper_margin +
					mode->lower_margin +
					mode->vsync_len;
		} else {
			screen->mode.yres = 2 * mode->yres +
					    3 * (mode->upper_margin +
						 mode->lower_margin +
						 mode->vsync_len) + 2;
			screen->mode.vmode = 0;
		}
	}
	/* Pin polarity */
	if (FB_SYNC_HOR_HIGH_ACT & mode->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if (FB_SYNC_VERT_HIGH_ACT & mode->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;

	screen->pin_den = 0;
	screen->pin_dclk = 1;

	/* Swap rule */
	if (hdmi->soctype > HDMI_SOC_RK3288 &&
	    screen->color_mode > COLOR_RGB &&
	    (screen->face == OUT_P888 ||
	     screen->face == OUT_P101010))
		screen->swap_rb = 1;
	else
		screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;

	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;

	screen->overscan.left = hdmi->xscale;
	screen->overscan.top = hdmi->yscale;
	screen->overscan.right = hdmi->xscale;
	screen->overscan.bottom = hdmi->yscale;
	return 0;
}

/**
 * hdmi_find_best_mode: find the video mode nearest to input vic
 * @hdmi:
 * @vic: input vic
 *
 * NOTES:
 * If vic is zero, return the high resolution video mode vic.
 */
int hdmi_find_best_mode(struct hdmi *hdmi, int vic)
{
	struct list_head *pos, *head = &hdmi->edid.modelist;
	struct display_modelist *modelist;
	int found = 0;

	if (vic) {
		list_for_each(pos, head) {
			modelist =
				list_entry(pos,
					   struct display_modelist, list);
			if (modelist->vic == vic) {
				found = 1;
				break;
			}
		}
	}
	if ((!vic || !found) && head->next != head) {
		/* If parse edid error, we select default mode; */
		if (hdmi->edid.specs &&
		    hdmi->edid.specs->modedb_len)
			modelist = list_entry(head->next,
					      struct display_modelist, list);
		else
			return hdmi->property->defaultmode;
	}

	if (modelist != NULL)
		return modelist->vic;
	else
		return 0;
}
/**
 * hdmi_set_lcdc: switch lcdc mode to required video mode
 * @hdmi:
 *
 * NOTES:
 *
 */
int hdmi_set_lcdc(struct hdmi *hdmi)
{
	int rc = 0;
	struct rk_screen screen;

	rc = hdmi_set_info(&screen, hdmi);
	if (!rc)
		rk_fb_switch_screen(&screen, 1, hdmi->lcdc->id);
	return rc;
}

/**
 * hdmi_videomode_compare - compare 2 videomodes
 * @mode1: first videomode
 * @mode2: second videomode
 *
 * RETURNS:
 * 1 if mode1 > mode2, 0 if mode1 = mode2, -1 mode1 < mode2
 */
static int hdmi_videomode_compare(const struct fb_videomode *mode1,
				  const struct fb_videomode *mode2)
{
	if (mode1->xres > mode2->xres)
		return 1;

	if (mode1->xres == mode2->xres) {
		if (mode1->yres > mode2->yres)
			return 1;
		if (mode1->yres == mode2->yres) {
			if (mode1->vmode < mode2->vmode)
				return 1;
			if (mode1->pixclock > mode2->pixclock)
				return 1;
			if (mode1->pixclock == mode2->pixclock) {
				if (mode1->refresh > mode2->refresh)
					return 1;
				if (mode1->refresh == mode2->refresh) {
					if (mode2->flag > mode1->flag)
						return 1;
					if (mode2->flag < mode1->flag)
						return -1;
					if (mode2->vmode > mode1->vmode)
						return 1;
					if (mode2->vmode == mode1->vmode)
						return 0;
				}
			}
		}
	}
	return -1;
}

/**
 * hdmi_add_vic - add entry to modelist according vic
 * @vic: vic to be added
 * @head: struct list_head of modelist
 *
 * NOTES:
 * Will only add unmatched mode entries
 */
int hdmi_add_vic(int vic, struct list_head *head)
{
	struct list_head *pos;
	struct display_modelist *modelist;
	int found = 0, v;

	/*pr_info("%s vic %d\n", __FUNCTION__, vic);*/
	if (vic == 0)
		return -1;

	list_for_each(pos, head) {
		modelist = list_entry(pos, struct display_modelist, list);
		v = modelist->vic;
		if (v == vic) {
			found = 1;
			break;
		}
	}
	if (!found) {
		modelist = kmalloc(sizeof(*modelist),
				   GFP_KERNEL);

		if (!modelist)
			return -ENOMEM;
		memset(modelist, 0, sizeof(struct display_modelist));
		modelist->vic = vic;
		list_add_tail(&modelist->list, head);
	}
	return 0;
}

/**
 * hdmi_add_videomode: adds videomode entry to modelist
 * @mode: videomode to be added
 * @head: struct list_head of modelist
 *
 * NOTES:
 * Will only add unmatched mode entries
 */
static int hdmi_add_videomode(const struct fb_videomode *mode,
			      struct list_head *head)
{
	struct list_head *pos;
	struct display_modelist *modelist, *modelist_new;
	struct fb_videomode *m;
	int i, found = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		m = (struct fb_videomode *)&(hdmi_mode[i].mode);
		if (fb_mode_is_equal(m, mode)) {
			found = 1;
			break;
		}
	}

	if (found) {
		list_for_each(pos, head) {
			modelist = list_entry(pos,
					      struct display_modelist, list);
			m = &modelist->mode;
			if (fb_mode_is_equal(m, mode))
				return 0;
			else if (hdmi_videomode_compare(m, mode) == -1)
				break;
		}

		modelist_new = kmalloc(sizeof(*modelist_new), GFP_KERNEL);
		if (!modelist_new)
			return -ENOMEM;
		memset(modelist_new, 0, sizeof(struct display_modelist));
		modelist_new->mode = hdmi_mode[i].mode;
		modelist_new->vic = hdmi_mode[i].vic;
		list_add_tail(&modelist_new->list, pos);
	}

	return 0;
}

/**
 * hdmi_sort_modelist: sort modelist of edid
 * @edid: edid to be sort
 */
static void hdmi_sort_modelist(struct hdmi_edid *edid, int feature)
{
	struct list_head *pos, *pos_new;
	struct list_head head_new, *head = &edid->modelist;
	struct display_modelist *modelist, *modelist_new, *modelist_n;
	struct fb_videomode *m, *m_new;
	int i, compare, vic;

	INIT_LIST_HEAD(&head_new);
	list_for_each(pos, head) {
		modelist = list_entry(pos, struct display_modelist, list);
		/*pr_info("%s vic %d\n", __function__, modelist->vic);*/
		for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
			if (modelist->vic & HDMI_VIDEO_DMT) {
				if (feature & SUPPORT_VESA_DMT)
					vic = modelist->vic;
				else
					continue;
			} else {
				vic = modelist->vic & HDMI_VIC_MASK;
			}
			if (vic == hdmi_mode[i].vic ||
			    vic == hdmi_mode[i].vic_2nd) {
				if ((feature & SUPPORT_4K) == 0 &&
				    hdmi_mode[i].mode.xres >= 3840)
					continue;
				if ((feature & SUPPORT_4K_4096) == 0 &&
				    hdmi_mode[i].mode.xres == 4096)
					continue;
				if ((feature & SUPPORT_TMDS_600M) == 0 &&
				    !(modelist->vic & HDMI_VIDEO_YUV420) &&
				    hdmi_mode[i].mode.pixclock > 340000000)
					continue;
				if ((modelist->vic & HDMI_VIDEO_YUV420) &&
				    (feature & SUPPORT_YUV420) == 0)
					continue;
				if ((feature & SUPPORT_1080I) == 0 &&
				    hdmi_mode[i].mode.xres == 1920 &&
				    (hdmi_mode[i].mode.vmode &
				     FB_VMODE_INTERLACED))
					continue;
				if ((feature & SUPPORT_480I_576I) == 0 &&
				    hdmi_mode[i].mode.xres == 720 &&
				    hdmi_mode[i].mode.vmode &
				     FB_VMODE_INTERLACED)
					continue;
				modelist->mode = hdmi_mode[i].mode;
				if (modelist->vic & HDMI_VIDEO_YUV420)
					modelist->mode.flag = 1;

				compare = 1;
				m = (struct fb_videomode *)&(modelist->mode);
				list_for_each(pos_new, &head_new) {
					modelist_new =
					list_entry(pos_new,
						   struct display_modelist,
						   list);
					m_new = &modelist_new->mode;
					compare =
					hdmi_videomode_compare(m, m_new);
					if (compare != -1)
						break;
				}
				if (compare != 0) {
					modelist_n =
						kmalloc(sizeof(*modelist_n),
							GFP_KERNEL);
					if (!modelist_n)
						return;
					*modelist_n = *modelist;
					list_add_tail(&modelist_n->list,
						      pos_new);
				}
				break;
			}
		}
	}
	fb_destroy_modelist(head);
	if (head_new.next == &head_new) {
		pr_info("There is no available video mode in EDID.\n");
		INIT_LIST_HEAD(&edid->modelist);
	} else {
		edid->modelist = head_new;
		edid->modelist.prev->next = &edid->modelist;
		edid->modelist.next->prev = &edid->modelist;
	}
}

/**
 * hdmi_ouputmode_select - select hdmi transmitter output mode: hdmi or dvi?
 * @hdmi: handle of hdmi
 * @edid_ok: get EDID data success or not, HDMI_ERROR_SUCESS means success.
 */
int hdmi_ouputmode_select(struct hdmi *hdmi, int edid_ok)
{
	struct list_head *head = &hdmi->edid.modelist;
	struct fb_monspecs *specs = hdmi->edid.specs;
	struct fb_videomode *modedb = NULL, *mode = NULL;
	int i, pixclock, feature = hdmi->property->feature;

	if (edid_ok != HDMI_ERROR_SUCESS) {
		dev_err(hdmi->dev, "warning: EDID error, assume sink as HDMI !!!!");
		hdmi->edid.status = -1;
		hdmi->edid.sink_hdmi = 1;
		hdmi->edid.baseaudio_support = 1;
		hdmi->edid.ycbcr444 = 0;
		hdmi->edid.ycbcr422 = 0;
	}

	if (head->next == head) {
		dev_info(hdmi->dev,
			 "warning: no CEA video mode parsed from EDID !!!!\n");
		/* If EDID get error, list all system supported mode.
		 * If output mode is set to DVI and EDID is ok, check
		 * the output timing.
		 */
		if (hdmi->edid.sink_hdmi == 0 && specs && specs->modedb_len) {
			/* Get max resolution timing */
			modedb = &specs->modedb[0];
			for (i = 0; i < specs->modedb_len; i++) {
				if (specs->modedb[i].xres > modedb->xres)
					modedb = &specs->modedb[i];
				else if (specs->modedb[i].xres ==
					 modedb->xres &&
					 specs->modedb[i].yres > modedb->yres)
					modedb = &specs->modedb[i];
			}
			/* For some monitor, the max pixclock read from EDID
			 * is smaller than the clock of max resolution mode
			 * supported. We fix it.
			 */
			pixclock = PICOS2KHZ(modedb->pixclock);
			pixclock /= 250;
			pixclock *= 250;
			pixclock *= 1000;
			if (pixclock == 148250000)
				pixclock = 148500000;
			if (pixclock > specs->dclkmax)
				specs->dclkmax = pixclock;
		}

		for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
			mode = (struct fb_videomode *)&(hdmi_mode[i].mode);
			if (modedb) {
				if ((mode->pixclock < specs->dclkmin) ||
				    (mode->pixclock > specs->dclkmax) ||
				    (mode->refresh < specs->vfmin) ||
				    (mode->refresh > specs->vfmax) ||
				    (mode->xres > modedb->xres) ||
				    (mode->yres > modedb->yres))
					continue;
			} else {
				/* If there is no valid information in EDID,
				 * just list common hdmi foramt.
				 */
				if (mode->xres > 3840 ||
				    mode->refresh < 50 ||
				    (mode->vmode & FB_VMODE_INTERLACED) ||
				    hdmi_mode[i].vic & HDMI_VIDEO_DMT)
					continue;
			}
			if ((feature & SUPPORT_TMDS_600M) == 0 &&
			    mode->pixclock > 340000000)
				continue;
			if ((feature & SUPPORT_4K) == 0 &&
			    mode->xres >= 3840)
				continue;
			if ((feature & SUPPORT_4K_4096) == 0 &&
			    mode->xres == 4096)
				continue;
			if ((feature & SUPPORT_1080I) == 0 &&
			    mode->xres == 1920 &&
			    (mode->vmode & FB_VMODE_INTERLACED))
				continue;
			if ((feature & SUPPORT_480I_576I) == 0 &&
			    mode->xres == 720 &&
			    (mode->vmode & FB_VMODE_INTERLACED))
				continue;
			hdmi_add_videomode(mode, head);
		}
	} else {
		/* There are some video mode is not defined in EDID extend
		 * block, so we need to check first block data.
		 */
		if (specs && specs->modedb_len) {
			for (i = 0; i < specs->modedb_len; i++) {
				modedb = &specs->modedb[i];
				pixclock = hdmi_videomode_to_vic(modedb);
				if (pixclock)
					hdmi_add_vic(pixclock, head);
			}
		}
		hdmi_sort_modelist(&hdmi->edid, hdmi->property->feature);
	}

	return HDMI_ERROR_SUCESS;
}

/**
 * hdmi_videomode_to_vic: transverse video mode to vic
 * @vmode: videomode to transverse
 *
 */
int hdmi_videomode_to_vic(struct fb_videomode *vmode)
{
	struct fb_videomode *mode;
	unsigned int vic = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		mode = (struct fb_videomode *)&(hdmi_mode[i].mode);
		if (vmode->vmode == mode->vmode &&
		    vmode->refresh == mode->refresh &&
		    vmode->xres == mode->xres &&
		    vmode->yres == mode->yres &&
		    vmode->left_margin == mode->left_margin &&
		    vmode->right_margin == mode->right_margin &&
		    vmode->upper_margin == mode->upper_margin &&
		    vmode->lower_margin == mode->lower_margin &&
		    vmode->hsync_len == mode->hsync_len &&
		    vmode->vsync_len == mode->vsync_len) {
			vic = hdmi_mode[i].vic;
			break;
		}
	}
	return vic;
}

/**
 * hdmi_vic2timing: transverse vic mode to video timing
 * @vmode: vic to transverse
 *
 */
const struct hdmi_video_timing *hdmi_vic2timing(int vic)
{
	int i;

	if (vic == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic == vic || hdmi_mode[i].vic_2nd == vic)
			return &(hdmi_mode[i]);
	}
	return NULL;
}

/**
 * hdmi_vic_to_videomode: transverse vic mode to video mode
 * @vmode: vic to transverse
 *
 */
const struct fb_videomode *hdmi_vic_to_videomode(int vic)
{
	int i, vid;

	if (vic == 0)
		return NULL;
	else if (vic & HDMI_VIDEO_DMT)
		vid = vic;
	else
		vid = vic & HDMI_VIC_MASK;
	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic == vid ||
		    hdmi_mode[i].vic_2nd == vid)
			return &hdmi_mode[i].mode;
	}
	return NULL;
}

/**
 * hdmi_init_modelist: initial hdmi mode list
 * @hdmi:
 *
 * NOTES:
 *
 */
void hdmi_init_modelist(struct hdmi *hdmi)
{
	int i, feature;
	struct list_head *head = &hdmi->edid.modelist;

	feature = hdmi->property->feature;
	INIT_LIST_HEAD(&hdmi->edid.modelist);
	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic & HDMI_VIDEO_DMT)
			continue;
		if ((feature & SUPPORT_TMDS_600M) == 0 &&
		    hdmi_mode[i].mode.pixclock > 340000000)
			continue;
		if ((feature & SUPPORT_4K) == 0 &&
		    hdmi_mode[i].mode.xres >= 3840)
			continue;
		if ((feature & SUPPORT_4K_4096) == 0 &&
		    hdmi_mode[i].mode.xres == 4096)
			continue;
		if ((feature & SUPPORT_1080I) == 0 &&
		    hdmi_mode[i].mode.xres == 1920 &&
		    (hdmi_mode[i].mode.vmode & FB_VMODE_INTERLACED))
			continue;
		if ((feature & SUPPORT_480I_576I) == 0 &&
		    hdmi_mode[i].mode.xres == 720 &&
		    (hdmi_mode[i].mode.vmode & FB_VMODE_INTERLACED))
			continue;
		hdmi_add_videomode(&(hdmi_mode[i].mode), head);
	}
}
