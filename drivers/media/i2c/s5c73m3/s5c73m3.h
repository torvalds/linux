/*
 * Samsung LSI S5C73M3 8M pixel camera driver
 *
 * Copyright (C) 2012, Samsung Electronics, Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef S5C73M3_H_
#define S5C73M3_H_

#include <linux/kernel.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/s5c73m3.h>

#define DRIVER_NAME			"S5C73M3"

#define S5C73M3_ISP_FMT			V4L2_MBUS_FMT_VYUY8_2X8
#define S5C73M3_JPEG_FMT		V4L2_MBUS_FMT_S5C_UYVY_JPEG_1X8

/* Subdevs pad index definitions */
enum s5c73m3_pads {
	S5C73M3_ISP_PAD,
	S5C73M3_JPEG_PAD,
	S5C73M3_NUM_PADS
};

enum s5c73m3_oif_pads {
	OIF_ISP_PAD,
	OIF_JPEG_PAD,
	OIF_SOURCE_PAD,
	OIF_NUM_PADS
};

#define S5C73M3_SENSOR_FW_LEN		6
#define S5C73M3_SENSOR_TYPE_LEN		12

#define S5C73M3_REG(_addrh, _addrl) (((_addrh) << 16) | _addrl)

#define AHB_MSB_ADDR_PTR			0xfcfc
#define REG_CMDWR_ADDRH				0x0050
#define REG_CMDWR_ADDRL				0x0054
#define REG_CMDRD_ADDRH				0x0058
#define REG_CMDRD_ADDRL				0x005c
#define REG_CMDBUF_ADDR				0x0f14

#define REG_I2C_SEQ_STATUS			S5C73M3_REG(0x0009, 0x59A6)
#define  SEQ_END_PLL				(1<<0x0)
#define  SEQ_END_SENSOR				(1<<0x1)
#define  SEQ_END_GPIO				(1<<0x2)
#define  SEQ_END_FROM				(1<<0x3)
#define  SEQ_END_STABLE_AE_AWB			(1<<0x4)
#define  SEQ_END_READY_I2C_CMD			(1<<0x5)

#define REG_I2C_STATUS				S5C73M3_REG(0x0009, 0x599E)
#define  I2C_STATUS_CIS_I2C			(1<<0x0)
#define  I2C_STATUS_AF_INIT			(1<<0x1)
#define  I2C_STATUS_CAL_DATA			(1<<0x2)
#define  I2C_STATUS_FRAME_COUNT			(1<<0x3)
#define  I2C_STATUS_FROM_INIT			(1<<0x4)
#define  I2C_STATUS_I2C_CIS_STREAM_OFF		(1<<0x5)
#define  I2C_STATUS_I2C_N_CMD_OVER		(1<<0x6)
#define  I2C_STATUS_I2C_N_CMD_MISMATCH		(1<<0x7)
#define  I2C_STATUS_CHECK_BIN_CRC		(1<<0x8)
#define  I2C_STATUS_EXCEPTION			(1<<0x9)
#define  I2C_STATUS_INIF_INIT_STATE		(0x8)

#define REG_STATUS				S5C73M3_REG(0x0009, 0x5080)
#define  REG_STATUS_BOOT_SUB_MAIN_ENTER		0xff01
#define  REG_STATUS_BOOT_SRAM_TIMING_OK		0xff02
#define  REG_STATUS_BOOT_INTERRUPTS_EN		0xff03
#define  REG_STATUS_BOOT_R_PLL_DONE		0xff04
#define  REG_STATUS_BOOT_R_PLL_LOCKTIME_DONE	0xff05
#define  REG_STATUS_BOOT_DELAY_COUNT_DONE	0xff06
#define  REG_STATUS_BOOT_I_PLL_DONE		0xff07
#define  REG_STATUS_BOOT_I_PLL_LOCKTIME_DONE	0xff08
#define  REG_STATUS_BOOT_PLL_INIT_OK		0xff09
#define  REG_STATUS_BOOT_SENSOR_INIT_OK		0xff0a
#define  REG_STATUS_BOOT_GPIO_SETTING_OK	0xff0b
#define  REG_STATUS_BOOT_READ_CAL_DATA_OK	0xff0c
#define  REG_STATUS_BOOT_STABLE_AE_AWB_OK	0xff0d
#define  REG_STATUS_ISP_COMMAND_COMPLETED	0xffff
#define  REG_STATUS_EXCEPTION_OCCURED		0xdead

#define COMM_RESULT_OFFSET			S5C73M3_REG(0x0009, 0x5000)

#define COMM_IMG_OUTPUT				0x0902
#define  COMM_IMG_OUTPUT_HDR			0x0008
#define  COMM_IMG_OUTPUT_YUV			0x0009
#define  COMM_IMG_OUTPUT_INTERLEAVED		0x000d

#define COMM_STILL_PRE_FLASH			0x0a00
#define  COMM_STILL_PRE_FLASH_FIRE		0x0000
#define  COMM_STILL_PRE_FLASH_NON_FIRED		0x0000
#define  COMM_STILL_PRE_FLASH_FIRED		0x0001

#define COMM_STILL_MAIN_FLASH			0x0a02
#define  COMM_STILL_MAIN_FLASH_CANCEL		0x0001
#define  COMM_STILL_MAIN_FLASH_FIRE		0x0002

#define COMM_ZOOM_STEP				0x0b00

#define COMM_IMAGE_EFFECT			0x0b0a
#define  COMM_IMAGE_EFFECT_NONE			0x0001
#define  COMM_IMAGE_EFFECT_NEGATIVE		0x0002
#define  COMM_IMAGE_EFFECT_AQUA			0x0003
#define  COMM_IMAGE_EFFECT_SEPIA		0x0004
#define  COMM_IMAGE_EFFECT_MONO			0x0005

#define COMM_IMAGE_QUALITY			0x0b0c
#define  COMM_IMAGE_QUALITY_SUPERFINE		0x0000
#define  COMM_IMAGE_QUALITY_FINE		0x0001
#define  COMM_IMAGE_QUALITY_NORMAL		0x0002

#define COMM_FLASH_MODE				0x0b0e
#define  COMM_FLASH_MODE_OFF			0x0000
#define  COMM_FLASH_MODE_ON			0x0001
#define  COMM_FLASH_MODE_AUTO			0x0002

#define COMM_FLASH_STATUS			0x0b80
#define  COMM_FLASH_STATUS_OFF			0x0001
#define  COMM_FLASH_STATUS_ON			0x0002
#define  COMM_FLASH_STATUS_AUTO			0x0003

#define COMM_FLASH_TORCH			0x0b12
#define  COMM_FLASH_TORCH_OFF			0x0000
#define  COMM_FLASH_TORCH_ON			0x0001

#define COMM_AE_NEEDS_FLASH			0x0cba
#define  COMM_AE_NEEDS_FLASH_OFF		0x0000
#define  COMM_AE_NEEDS_FLASH_ON			0x0001

#define COMM_CHG_MODE				0x0b10
#define  COMM_CHG_MODE_NEW			0x8000
#define  COMM_CHG_MODE_SUBSAMPLING_HALF		0x2000
#define  COMM_CHG_MODE_SUBSAMPLING_QUARTER	0x4000

#define  COMM_CHG_MODE_YUV_320_240		0x0001
#define  COMM_CHG_MODE_YUV_640_480		0x0002
#define  COMM_CHG_MODE_YUV_880_720		0x0003
#define  COMM_CHG_MODE_YUV_960_720		0x0004
#define  COMM_CHG_MODE_YUV_1184_666		0x0005
#define  COMM_CHG_MODE_YUV_1280_720		0x0006
#define  COMM_CHG_MODE_YUV_1536_864		0x0007
#define  COMM_CHG_MODE_YUV_1600_1200		0x0008
#define  COMM_CHG_MODE_YUV_1632_1224		0x0009
#define  COMM_CHG_MODE_YUV_1920_1080		0x000a
#define  COMM_CHG_MODE_YUV_1920_1440		0x000b
#define  COMM_CHG_MODE_YUV_2304_1296		0x000c
#define  COMM_CHG_MODE_YUV_3264_2448		0x000d
#define  COMM_CHG_MODE_YUV_352_288		0x000e
#define  COMM_CHG_MODE_YUV_1008_672		0x000f

#define  COMM_CHG_MODE_JPEG_640_480		0x0010
#define  COMM_CHG_MODE_JPEG_800_450		0x0020
#define  COMM_CHG_MODE_JPEG_800_600		0x0030
#define  COMM_CHG_MODE_JPEG_1280_720		0x0040
#define  COMM_CHG_MODE_JPEG_1280_960		0x0050
#define  COMM_CHG_MODE_JPEG_1600_900		0x0060
#define  COMM_CHG_MODE_JPEG_1600_1200		0x0070
#define  COMM_CHG_MODE_JPEG_2048_1152		0x0080
#define  COMM_CHG_MODE_JPEG_2048_1536		0x0090
#define  COMM_CHG_MODE_JPEG_2560_1440		0x00a0
#define  COMM_CHG_MODE_JPEG_2560_1920		0x00b0
#define  COMM_CHG_MODE_JPEG_3264_2176		0x00c0
#define  COMM_CHG_MODE_JPEG_1024_768		0x00d0
#define  COMM_CHG_MODE_JPEG_3264_1836		0x00e0
#define  COMM_CHG_MODE_JPEG_3264_2448		0x00f0

#define COMM_AF_CON				0x0e00
#define  COMM_AF_CON_STOP			0x0000
#define  COMM_AF_CON_SCAN			0x0001 /* Full Search */
#define  COMM_AF_CON_START			0x0002 /* Fast Search */

#define COMM_AF_CAL				0x0e06
#define COMM_AF_TOUCH_AF			0x0e0a

#define REG_AF_STATUS				S5C73M3_REG(0x0009, 0x5e80)
#define  REG_CAF_STATUS_FIND_SEARCH_DIR		0x0001
#define  REG_CAF_STATUS_FOCUSING		0x0002
#define  REG_CAF_STATUS_FOCUSED			0x0003
#define  REG_CAF_STATUS_UNFOCUSED		0x0004
#define  REG_AF_STATUS_INVALID			0x0010
#define  REG_AF_STATUS_FOCUSING			0x0020
#define  REG_AF_STATUS_FOCUSED			0x0030
#define  REG_AF_STATUS_UNFOCUSED		0x0040

#define REG_AF_TOUCH_POSITION			S5C73M3_REG(0x0009, 0x5e8e)
#define COMM_AF_FACE_ZOOM			0x0e10

#define COMM_AF_MODE				0x0e02
#define  COMM_AF_MODE_NORMAL			0x0000
#define  COMM_AF_MODE_MACRO			0x0001
#define  COMM_AF_MODE_MOVIE_CAF_START		0x0002
#define  COMM_AF_MODE_MOVIE_CAF_STOP		0x0003
#define  COMM_AF_MODE_PREVIEW_CAF_START		0x0004
#define  COMM_AF_MODE_PREVIEW_CAF_STOP		0x0005

#define COMM_AF_SOFTLANDING			0x0e16
#define  COMM_AF_SOFTLANDING_ON			0x0000
#define  COMM_AF_SOFTLANDING_RES_COMPLETE	0x0001

#define COMM_FACE_DET				0x0e0c
#define  COMM_FACE_DET_OFF			0x0000
#define  COMM_FACE_DET_ON			0x0001

#define COMM_FACE_DET_OSD			0x0e0e
#define  COMM_FACE_DET_OSD_OFF			0x0000
#define  COMM_FACE_DET_OSD_ON			0x0001

#define COMM_AE_CON				0x0c00
#define  COMM_AE_STOP				0x0000 /* lock */
#define  COMM_AE_START				0x0001 /* unlock */

#define COMM_ISO				0x0c02
#define  COMM_ISO_AUTO				0x0000
#define  COMM_ISO_100				0x0001
#define  COMM_ISO_200				0x0002
#define  COMM_ISO_400				0x0003
#define  COMM_ISO_800				0x0004
#define  COMM_ISO_SPORTS			0x0005
#define  COMM_ISO_NIGHT				0x0006
#define  COMM_ISO_INDOOR			0x0007

/* 0x00000 (-2.0 EV)...0x0008 (2.0 EV), 0.5EV step */
#define COMM_EV					0x0c04

#define COMM_METERING				0x0c06
#define  COMM_METERING_CENTER			0x0000
#define  COMM_METERING_SPOT			0x0001
#define  COMM_METERING_AVERAGE			0x0002
#define  COMM_METERING_SMART			0x0003

#define COMM_WDR				0x0c08
#define  COMM_WDR_OFF				0x0000
#define  COMM_WDR_ON				0x0001

#define COMM_FLICKER_MODE			0x0c12
#define  COMM_FLICKER_NONE			0x0000
#define  COMM_FLICKER_MANUAL_50HZ		0x0001
#define  COMM_FLICKER_MANUAL_60HZ		0x0002
#define  COMM_FLICKER_AUTO			0x0003
#define  COMM_FLICKER_AUTO_50HZ			0x0004
#define  COMM_FLICKER_AUTO_60HZ			0x0005

#define COMM_FRAME_RATE				0x0c1e
#define  COMM_FRAME_RATE_AUTO_SET		0x0000
#define  COMM_FRAME_RATE_FIXED_30FPS		0x0002
#define  COMM_FRAME_RATE_FIXED_20FPS		0x0003
#define  COMM_FRAME_RATE_FIXED_15FPS		0x0004
#define  COMM_FRAME_RATE_FIXED_60FPS		0x0007
#define  COMM_FRAME_RATE_FIXED_120FPS		0x0008
#define  COMM_FRAME_RATE_FIXED_7FPS		0x0009
#define  COMM_FRAME_RATE_FIXED_10FPS		0x000a
#define  COMM_FRAME_RATE_FIXED_90FPS		0x000b
#define  COMM_FRAME_RATE_ANTI_SHAKE		0x0013

/* 0x0000...0x0004 -> sharpness: 0, 1, 2, -1, -2 */
#define COMM_SHARPNESS				0x0c14

/* 0x0000...0x0004 -> saturation: 0, 1, 2, -1, -2 */
#define COMM_SATURATION				0x0c16

/* 0x0000...0x0004 -> contrast: 0, 1, 2, -1, -2 */
#define COMM_CONTRAST				0x0c18

#define COMM_SCENE_MODE				0x0c1a
#define  COMM_SCENE_MODE_NONE			0x0000
#define  COMM_SCENE_MODE_PORTRAIT		0x0001
#define  COMM_SCENE_MODE_LANDSCAPE		0x0002
#define  COMM_SCENE_MODE_SPORTS			0x0003
#define  COMM_SCENE_MODE_INDOOR			0x0004
#define  COMM_SCENE_MODE_BEACH			0x0005
#define  COMM_SCENE_MODE_SUNSET			0x0006
#define  COMM_SCENE_MODE_DAWN			0x0007
#define  COMM_SCENE_MODE_FALL			0x0008
#define  COMM_SCENE_MODE_NIGHT			0x0009
#define  COMM_SCENE_MODE_AGAINST_LIGHT		0x000a
#define  COMM_SCENE_MODE_FIRE			0x000b
#define  COMM_SCENE_MODE_TEXT			0x000c
#define  COMM_SCENE_MODE_CANDLE			0x000d

#define COMM_AE_AUTO_BRACKET			0x0b14
#define  COMM_AE_AUTO_BRAKET_EV05		0x0080
#define  COMM_AE_AUTO_BRAKET_EV10		0x0100
#define  COMM_AE_AUTO_BRAKET_EV15		0x0180
#define  COMM_AE_AUTO_BRAKET_EV20		0x0200

#define COMM_SENSOR_STREAMING			0x090a
#define  COMM_SENSOR_STREAMING_OFF		0x0000
#define  COMM_SENSOR_STREAMING_ON		0x0001

#define COMM_AWB_MODE				0x0d02
#define  COMM_AWB_MODE_INCANDESCENT		0x0000
#define  COMM_AWB_MODE_FLUORESCENT1		0x0001
#define  COMM_AWB_MODE_FLUORESCENT2		0x0002
#define  COMM_AWB_MODE_DAYLIGHT			0x0003
#define  COMM_AWB_MODE_CLOUDY			0x0004
#define  COMM_AWB_MODE_AUTO			0x0005

#define COMM_AWB_CON				0x0d00
#define  COMM_AWB_STOP				0x0000 /* lock */
#define  COMM_AWB_START				0x0001 /* unlock */

#define COMM_FW_UPDATE				0x0906
#define  COMM_FW_UPDATE_NOT_READY		0x0000
#define  COMM_FW_UPDATE_SUCCESS			0x0005
#define  COMM_FW_UPDATE_FAIL			0x0007
#define  COMM_FW_UPDATE_BUSY			0xffff


#define S5C73M3_MAX_SUPPLIES			6

struct s5c73m3_ctrls {
	struct v4l2_ctrl_handler handler;
	struct {
		/* exposure/exposure bias cluster */
		struct v4l2_ctrl *auto_exposure;
		struct v4l2_ctrl *exposure_bias;
		struct v4l2_ctrl *exposure_metering;
	};
	struct {
		/* iso/auto iso cluster */
		struct v4l2_ctrl *auto_iso;
		struct v4l2_ctrl *iso;
	};
	struct v4l2_ctrl *auto_wb;
	struct {
		/* continuous auto focus/auto focus cluster */
		struct v4l2_ctrl *focus_auto;
		struct v4l2_ctrl *af_start;
		struct v4l2_ctrl *af_stop;
		struct v4l2_ctrl *af_status;
		struct v4l2_ctrl *af_distance;
	};

	struct v4l2_ctrl *aaa_lock;
	struct v4l2_ctrl *colorfx;
	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *sharpness;
	struct v4l2_ctrl *zoom;
	struct v4l2_ctrl *wdr;
	struct v4l2_ctrl *stabilization;
	struct v4l2_ctrl *jpeg_quality;
	struct v4l2_ctrl *scene_mode;
};

enum s5c73m3_gpio_id {
	STBY,
	RST,
	GPIO_NUM,
};

enum s5c73m3_resolution_types {
	RES_ISP,
	RES_JPEG,
};

struct s5c73m3_interval {
	u16 fps_reg;
	struct v4l2_fract interval;
	/* Maximum rectangle for the interval */
	struct v4l2_frmsize_discrete size;
};

struct s5c73m3 {
	struct v4l2_subdev sensor_sd;
	struct media_pad sensor_pads[S5C73M3_NUM_PADS];

	struct v4l2_subdev oif_sd;
	struct media_pad oif_pads[OIF_NUM_PADS];

	struct spi_driver spidrv;
	struct spi_device *spi_dev;
	struct i2c_client *i2c_client;
	u32 i2c_write_address;
	u32 i2c_read_address;

	struct regulator_bulk_data supplies[S5C73M3_MAX_SUPPLIES];
	struct s5c73m3_gpio gpio[GPIO_NUM];

	/* External master clock frequency */
	u32 mclk_frequency;
	/* Video bus type - MIPI-CSI2/paralell */
	enum v4l2_mbus_type bus_type;

	const struct s5c73m3_frame_size *sensor_pix_size[2];
	const struct s5c73m3_frame_size *oif_pix_size[2];
	enum v4l2_mbus_pixelcode mbus_code;

	const struct s5c73m3_interval *fiv;

	struct v4l2_mbus_frame_desc frame_desc;
	/* protects the struct members below */
	struct mutex lock;

	struct s5c73m3_ctrls ctrls;

	u8 streaming:1;
	u8 apply_fmt:1;
	u8 apply_fiv:1;
	u8 isp_ready:1;

	short power;

	char sensor_fw[S5C73M3_SENSOR_FW_LEN + 2];
	char sensor_type[S5C73M3_SENSOR_TYPE_LEN + 2];
	char fw_file_version[2];
	unsigned int fw_size;
};

struct s5c73m3_frame_size {
	u32 width;
	u32 height;
	u8 reg_val;
};

extern int s5c73m3_dbg;

int s5c73m3_register_spi_driver(struct s5c73m3 *state);
void s5c73m3_unregister_spi_driver(struct s5c73m3 *state);
int s5c73m3_spi_write(struct s5c73m3 *state, const void *addr,
		      const unsigned int len, const unsigned int tx_size);
int s5c73m3_spi_read(struct s5c73m3 *state, void *addr,
		      const unsigned int len, const unsigned int tx_size);

int s5c73m3_read(struct s5c73m3 *state, u32 addr, u16 *data);
int s5c73m3_write(struct s5c73m3 *state, u32 addr, u16 data);
int s5c73m3_isp_command(struct s5c73m3 *state, u16 command, u16 data);
int s5c73m3_init_controls(struct s5c73m3 *state);

static inline struct v4l2_subdev *ctrl_to_sensor_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct s5c73m3,
			     ctrls.handler)->sensor_sd;
}

static inline struct s5c73m3 *sensor_sd_to_s5c73m3(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5c73m3, sensor_sd);
}

static inline struct s5c73m3 *oif_sd_to_s5c73m3(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5c73m3, oif_sd);
}
#endif	/* S5C73M3_H_ */
