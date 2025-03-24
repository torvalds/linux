/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 */
#ifndef ATOMISP_PLATFORM_H_
#define ATOMISP_PLATFORM_H_

#include <asm/cpu_device_id.h>
#include <asm/processor.h>

#include <linux/i2c.h>
#include <media/v4l2-subdev.h>
#include "atomisp.h"

#define MAX_SENSORS_PER_PORT 4
#define MAX_STREAMS_PER_CHANNEL 2

#define CAMERA_MODULE_ID_LEN 64

enum atomisp_bayer_order {
	atomisp_bayer_order_grbg,
	atomisp_bayer_order_rggb,
	atomisp_bayer_order_bggr,
	atomisp_bayer_order_gbrg
};

enum atomisp_input_stream_id {
	ATOMISP_INPUT_STREAM_GENERAL = 0,
	ATOMISP_INPUT_STREAM_CAPTURE = 0,
	ATOMISP_INPUT_STREAM_POSTVIEW,
	ATOMISP_INPUT_STREAM_PREVIEW,
	ATOMISP_INPUT_STREAM_VIDEO,
	ATOMISP_INPUT_STREAM_NUM
};

enum atomisp_input_format {
	ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY,/* 8 bits per subpixel (legacy) */
	ATOMISP_INPUT_FORMAT_YUV420_8, /* 8 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV420_10,/* 10 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV420_16,/* 16 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV422_8, /* UYVY..UVYV, 8 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV422_10,/* UYVY..UVYV, 10 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV422_16,/* UYVY..UVYV, 16 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_444,  /* BGR..BGR, 4 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_555,  /* BGR..BGR, 5 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_565,  /* BGR..BGR, 5 bits B and R, 6 bits G */
	ATOMISP_INPUT_FORMAT_RGB_666,  /* BGR..BGR, 6 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_888,  /* BGR..BGR, 8 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RAW_6,    /* RAW data, 6 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_7,    /* RAW data, 7 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_8,    /* RAW data, 8 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_10,   /* RAW data, 10 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_12,   /* RAW data, 12 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_14,   /* RAW data, 14 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_16,   /* RAW data, 16 bits per pixel */
	ATOMISP_INPUT_FORMAT_BINARY_8, /* Binary byte stream. */

	/* CSI2-MIPI specific format: Generic short packet data. It is used to
	 * keep the timing information for the opening/closing of shutters,
	 * triggering of flashes and etc.
	 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT1,  /* Generic Short Packet Code 1 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT2,  /* Generic Short Packet Code 2 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT3,  /* Generic Short Packet Code 3 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT4,  /* Generic Short Packet Code 4 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT5,  /* Generic Short Packet Code 5 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT6,  /* Generic Short Packet Code 6 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT7,  /* Generic Short Packet Code 7 */
	ATOMISP_INPUT_FORMAT_GENERIC_SHORT8,  /* Generic Short Packet Code 8 */

	/* CSI2-MIPI specific format: YUV data.
	 */
	ATOMISP_INPUT_FORMAT_YUV420_8_SHIFT,  /* YUV420 8-bit (Chroma Shifted
						 Pixel Sampling) */
	ATOMISP_INPUT_FORMAT_YUV420_10_SHIFT, /* YUV420 8-bit (Chroma Shifted
						 Pixel Sampling) */

	/* CSI2-MIPI specific format: Generic long packet data
	 */
	ATOMISP_INPUT_FORMAT_EMBEDDED, /* Embedded 8-bit non Image Data */

	/* CSI2-MIPI specific format: User defined byte-based data. For example,
	 * the data transmitter (e.g. the SoC sensor) can keep the JPEG data as
	 * the User Defined Data Type 4 and the MPEG data as the
	 * User Defined Data Type 7.
	 */
	ATOMISP_INPUT_FORMAT_USER_DEF1,  /* User defined 8-bit data type 1 */
	ATOMISP_INPUT_FORMAT_USER_DEF2,  /* User defined 8-bit data type 2 */
	ATOMISP_INPUT_FORMAT_USER_DEF3,  /* User defined 8-bit data type 3 */
	ATOMISP_INPUT_FORMAT_USER_DEF4,  /* User defined 8-bit data type 4 */
	ATOMISP_INPUT_FORMAT_USER_DEF5,  /* User defined 8-bit data type 5 */
	ATOMISP_INPUT_FORMAT_USER_DEF6,  /* User defined 8-bit data type 6 */
	ATOMISP_INPUT_FORMAT_USER_DEF7,  /* User defined 8-bit data type 7 */
	ATOMISP_INPUT_FORMAT_USER_DEF8,  /* User defined 8-bit data type 8 */
};

#define N_ATOMISP_INPUT_FORMAT (ATOMISP_INPUT_FORMAT_USER_DEF8 + 1)

struct intel_v4l2_subdev_table {
	enum atomisp_camera_port port;
	unsigned int lanes;
	struct v4l2_subdev *subdev;
};

/*
 *  Sensor of external ISP can send multiple streams with different mipi data
 * type in the same virtual channel. This information needs to come from the
 * sensor or external ISP
 */
struct atomisp_isys_config_info {
	u8 input_format;
	u16 width;
	u16 height;
};

struct atomisp_input_stream_info {
	enum atomisp_input_stream_id stream;
	u8 enable;
	/* Sensor driver fills ch_id with the id
	   of the virtual channel. */
	u8 ch_id;
	/* Tells how many streams in this virtual channel. If 0 ignore rest
	 * and the input format will be from mipi_info */
	u8 isys_configs;
	/*
	 * if more isys_configs is more than 0, sensor needs to configure the
	 * input format differently. width and height can be 0. If width and
	 * height is not zero, then the corresponding data needs to be set
	 */
	struct atomisp_isys_config_info isys_info[MAX_STREAMS_PER_CHANNEL];
};

struct camera_sensor_platform_data {
	int (*flisclk_ctrl)(struct v4l2_subdev *subdev, int flag);
	int (*csi_cfg)(struct v4l2_subdev *subdev, int flag);

	/*
	 * New G-Min power and GPIO interface to control individual
	 * lines as implemented on all known camera modules.
	 */
	int (*gpio0_ctrl)(struct v4l2_subdev *subdev, int on);
	int (*gpio1_ctrl)(struct v4l2_subdev *subdev, int on);
	int (*v1p8_ctrl)(struct v4l2_subdev *subdev, int on);
	int (*v2p8_ctrl)(struct v4l2_subdev *subdev, int on);
	int (*v1p2_ctrl)(struct v4l2_subdev *subdev, int on);
};

struct camera_mipi_info {
	enum atomisp_camera_port        port;
	unsigned int                    num_lanes;
	enum atomisp_input_format       input_format;
	enum atomisp_bayer_order        raw_bayer_order;
	enum atomisp_input_format       metadata_format;
	u32                             metadata_width;
	u32                             metadata_height;
	const u32                       *metadata_effective_width;
};

const struct intel_v4l2_subdev_table *atomisp_platform_get_subdevs(void);
int atomisp_register_sensor_no_gmin(struct v4l2_subdev *subdev, u32 lanes,
				    enum atomisp_input_format format,
				    enum atomisp_bayer_order bayer_order);
void atomisp_unregister_subdev(struct v4l2_subdev *subdev);

/* API from old platform_camera.h, new CPUID implementation */
#define __IS_SOC(x) (boot_cpu_data.x86_vfm == x)
#define __IS_SOCS(x, y) (boot_cpu_data.x86_vfm == x || boot_cpu_data.x86_vfm == y)

#define IS_MFLD	__IS_SOC(INTEL_ATOM_SALTWELL_MID)
#define IS_BYT	__IS_SOC(INTEL_ATOM_SILVERMONT)
#define IS_CHT	__IS_SOC(INTEL_ATOM_AIRMONT)
#define IS_MRFD	__IS_SOC(INTEL_ATOM_SILVERMONT_MID)
#define IS_MOFD	__IS_SOC(INTEL_ATOM_AIRMONT_MID)

/* Both CHT and MOFD come with ISP2401 */
#define IS_ISP2401 __IS_SOCS(INTEL_ATOM_AIRMONT, \
			     INTEL_ATOM_AIRMONT_MID)

#endif /* ATOMISP_PLATFORM_H_ */
