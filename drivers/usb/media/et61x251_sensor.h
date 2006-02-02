/***************************************************************************
 * API for image sensors connected to ET61X[12]51 PC Camera Controllers    *
 *                                                                         *
 * Copyright (C) 2006 by Luca Risolia <luca.risolia@studio.unibo.it>       *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#ifndef _ET61X251_SENSOR_H_
#define _ET61X251_SENSOR_H_

#include <linux/usb.h>
#include <linux/videodev.h>
#include <linux/device.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <asm/types.h>

struct et61x251_device;
struct et61x251_sensor;

/*****************************************************************************/

extern int et61x251_probe_tas5130d1b(struct et61x251_device* cam);

#define ET61X251_SENSOR_TABLE                                                 \
/* Weak detections must go at the end of the list */                          \
static int (*et61x251_sensor_table[])(struct et61x251_device*) = {            \
	&et61x251_probe_tas5130d1b,                                           \
	NULL,                                                                 \
};

extern void
et61x251_attach_sensor(struct et61x251_device* cam,
                       struct et61x251_sensor* sensor);

/*****************************************************************************/

extern int et61x251_write_reg(struct et61x251_device*, u8 value, u16 index);
extern int et61x251_read_reg(struct et61x251_device*, u16 index);
extern int et61x251_i2c_write(struct et61x251_device*, u8 address, u8 value);
extern int et61x251_i2c_read(struct et61x251_device*, u8 address);
extern int et61x251_i2c_try_write(struct et61x251_device*,
                                  struct et61x251_sensor*, u8 address,
                                  u8 value);
extern int et61x251_i2c_try_read(struct et61x251_device*,
                                 struct et61x251_sensor*, u8 address);
extern int et61x251_i2c_raw_write(struct et61x251_device*, u8 n, u8 data1,
                                  u8 data2, u8 data3, u8 data4, u8 data5,
                                  u8 data6, u8 data7, u8 data8, u8 address);

/*****************************************************************************/

enum et61x251_i2c_sysfs_ops {
	ET61X251_I2C_READ = 0x01,
	ET61X251_I2C_WRITE = 0x02,
};

enum et61x251_i2c_interface {
	ET61X251_I2C_2WIRES,
	ET61X251_I2C_3WIRES,
};

/* Repeat start condition when RSTA is high */
enum et61x251_i2c_rsta {
	ET61X251_I2C_RSTA_STOP = 0x00, /* stop then start */
	ET61X251_I2C_RSTA_REPEAT = 0x01, /* repeat start */
};

#define ET61X251_MAX_CTRLS V4L2_CID_LASTP1-V4L2_CID_BASE+10

struct et61x251_sensor {
	char name[32];

	enum et61x251_i2c_sysfs_ops sysfs_ops;

	enum et61x251_i2c_interface interface;
	u8 i2c_slave_id;
	enum et61x251_i2c_rsta rsta;
	struct v4l2_rect active_pixel; /* left and top define FVSX and FVSY */

	struct v4l2_queryctrl qctrl[ET61X251_MAX_CTRLS];
	struct v4l2_cropcap cropcap;
	struct v4l2_pix_format pix_format;

	int (*init)(struct et61x251_device* cam);
	int (*get_ctrl)(struct et61x251_device* cam,
	                struct v4l2_control* ctrl);
	int (*set_ctrl)(struct et61x251_device* cam,
	                const struct v4l2_control* ctrl);
	int (*set_crop)(struct et61x251_device* cam,
	                const struct v4l2_rect* rect);
	int (*set_pix_format)(struct et61x251_device* cam,
	                      const struct v4l2_pix_format* pix);

	const struct usb_device* usbdev;

	/* Private */
	struct v4l2_queryctrl _qctrl[ET61X251_MAX_CTRLS];
	struct v4l2_rect _rect;
};

#endif /* _ET61X251_SENSOR_H_ */
