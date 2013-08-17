/*
 * Samsung EXYNOS4412 FIMC-ISP driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * All rights reserved.
 */

#ifndef FIMC_IS_SENSOR_H_
#define FIMC_IS_SENSOR_H_

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <media/exynos_fimc_is.h>

#define FIMC_IS_SENSOR_OPEN_TIMEOUT	2000 /* ms */

#define FIMC_IS_SENSOR_DEF_PIX_WIDTH	816
#define FIMC_IS_SENSOR_DEF_PIX_HEIGHT	492

#define SENSOR_NUM_SUPPLIES		2


/* MESS begin */
enum sensor_list {
	SENSOR_S5K3H2_CSI_A	= 1,
	SENSOR_S5K6A3_CSI_A	= 2,
	SENSOR_S5K4E5_CSI_A	= 3,
	SENSOR_S5K3H7_CSI_A	= 4,
	SENSOR_S5K3H2_CSI_B	= 101,
	SENSOR_S5K6A3_CSI_B	= 102,
	SENSOR_S5K4E5_CSI_B	= 103,
	SENSOR_S5K3H7_CSI_B	= 104,
	/* Custom mode */
	SENSOR_S5K6A3_CSI_B_CUSTOM	= 200,
};

enum sensor_name {
	SENSOR_NAME_S5K3H2	= 1,
	SENSOR_NAME_S5K6A3	= 2,
	SENSOR_NAME_S5K4E5	= 3,
	SENSOR_NAME_S5K3H7	= 4,
	SENSOR_NAME_CUSTOM	= 5,
	SENSOR_NAME_END
};

enum sensor_channel {
	SENSOR_CONTROL_I2C0	= 0,
	SENSOR_CONTROL_I2C1	= 1
};
/* MESS end */

/*
 * struct sensor_pix_format - FIMC-IS sensor pixel format description
 * @code: corresponding media bus code
 */
struct sensor_pix_format {
	enum v4l2_mbus_pixelcode code;
};

struct fimc_is_sensor_board_info {
	int gpio_reset;
};

/*
 * struct fimc_is_sensor - the driver's internal state data structure
 * @lock: mutex serializing the subdev and power management operations,
 *        protecting @format and @flags members
 * @pad: media pad
 * @sd: sensor subdev
 * @flags: the state variable for power and streaming control
 * @sensor_fmt: current pixel format
 * @format: common media bus format for the source and sink pad
 */
struct fimc_is_sensor {
	struct regulator_bulk_data supplies[SENSOR_NUM_SUPPLIES];
	struct fimc_is_sensor_board_info board_info;
	struct mutex lock;
	struct media_pad pad;
	struct v4l2_subdev subdev;
	const struct sensor_pix_format *sensor_fmt;
	struct v4l2_mbus_framefmt format;
	struct fimc_is *is;

	u32 offset_x;
	u32 offset_y;
	u32 frame_count;
	int id;
	int i2c_ch;
	bool use_af;
	bool test_pattern_flg;
};

int fimc_is_sensor_subdev_create(struct fimc_is_sensor *sensor,
				 struct fimc_is_sensor_info *inf);
void fimc_is_sensor_subdev_destroy(struct fimc_is_sensor *sensor);

#ifdef CONFIG_VIDEO_EXYNOS4_FIMC_IS
const char * const fimc_is_get_sensor_name(struct fimc_is_sensor_info *info);
#else
static inline const char * const
fimc_is_get_sensor_name(struct fimc_is_sensor_info *info)
{
	return NULL;
}
#endif
#endif /* FIMC_IS_SENSOR_H_ */
