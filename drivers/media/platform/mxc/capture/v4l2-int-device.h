/*
 * include/media/v4l2-int-device.h
 *
 * V4L2 internal ioctl interface.
 *
 * Copyright 2005-2014 Freescale Semiconductor, Inc.
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef V4L2_INT_DEVICE_H
#define V4L2_INT_DEVICE_H

#include <media/v4l2-common.h>

#define V4L2NAMESIZE 32

/*
 *
 * The internal V4L2 device interface core.
 *
 */

enum v4l2_int_type {
	v4l2_int_type_master = 1,
	v4l2_int_type_slave
};

struct module;

struct v4l2_int_device;

struct v4l2_int_master {
	int (*attach)(struct v4l2_int_device *slave);
	void (*detach)(struct v4l2_int_device *slave);
};

typedef int (v4l2_int_ioctl_func)(struct v4l2_int_device *);
typedef int (v4l2_int_ioctl_func_0)(struct v4l2_int_device *);
typedef int (v4l2_int_ioctl_func_1)(struct v4l2_int_device *, void *);

struct v4l2_int_ioctl_desc {
	int num;
	v4l2_int_ioctl_func *func;
};

struct v4l2_int_slave {
	/* Don't touch master. */
	struct v4l2_int_device *master;

	char attach_to[V4L2NAMESIZE];

	int num_ioctls;
	struct v4l2_int_ioctl_desc *ioctls;
};

struct v4l2_int_device {
	/* Don't touch head. */
	struct list_head head;

	struct module *module;

	char name[V4L2NAMESIZE];

	enum v4l2_int_type type;
	union {
		struct v4l2_int_master *master;
		struct v4l2_int_slave *slave;
	} u;

	void *priv;
};

void v4l2_int_device_try_attach_all(void);

int v4l2_int_device_register(struct v4l2_int_device *d);
void v4l2_int_device_unregister(struct v4l2_int_device *d);

int v4l2_int_ioctl_0(struct v4l2_int_device *d, int cmd);
int v4l2_int_ioctl_1(struct v4l2_int_device *d, int cmd, void *arg);

/*
 *
 * Types and definitions for IOCTL commands.
 *
 */

enum v4l2_power {
	V4L2_POWER_OFF = 0,
	V4L2_POWER_ON,
	V4L2_POWER_STANDBY,
};

/* Slave interface type. */
enum v4l2_if_type {
	/*
	 * Parallel 8-, 10- or 12-bit interface, used by for example
	 * on certain image sensors.
	 */
	V4L2_IF_TYPE_BT656,
};

enum v4l2_if_type_bt656_mode {
	/*
	 * Modes without Bt synchronisation codes. Separate
	 * synchronisation signal lines are used.
	 */
	V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT,
	V4L2_IF_TYPE_BT656_MODE_NOBT_10BIT,
	V4L2_IF_TYPE_BT656_MODE_NOBT_12BIT,
	/*
	 * Use Bt synchronisation codes. The vertical and horizontal
	 * synchronisation is done based on synchronisation codes.
	 */
	V4L2_IF_TYPE_BT656_MODE_BT_8BIT,
	V4L2_IF_TYPE_BT656_MODE_BT_10BIT,
};

struct v4l2_if_type_bt656 {
	/*
	 * 0: Frame begins when vsync is high.
	 * 1: Frame begins when vsync changes from low to high.
	 */
	unsigned frame_start_on_rising_vs:1;
	/* Use Bt synchronisation codes for sync correction. */
	unsigned bt_sync_correct:1;
	/* Swap every two adjacent image data elements. */
	unsigned swap:1;
	/* Inverted latch clock polarity from slave. */
	unsigned latch_clk_inv:1;
	/* Hs polarity. 0 is active high, 1 active low. */
	unsigned nobt_hs_inv:1;
	/* Vs polarity. 0 is active high, 1 active low. */
	unsigned nobt_vs_inv:1;
	enum v4l2_if_type_bt656_mode mode;
	/* Minimum accepted bus clock for slave (in Hz). */
	u32 clock_min;
	/* Maximum accepted bus clock for slave. */
	u32 clock_max;
	/*
	 * Current wish of the slave. May only change in response to
	 * ioctls that affect image capture.
	 */
	u32 clock_curr;
};

struct v4l2_ifparm {
	enum v4l2_if_type if_type;
	union {
		struct v4l2_if_type_bt656 bt656;
	} u;
};

/* IOCTL command numbers. */
enum v4l2_int_ioctl_num {
	/*
	 *
	 * "Proper" V4L ioctls, as in struct video_device.
	 *
	 */
	vidioc_int_enum_fmt_cap_num = 1,
	vidioc_int_g_fmt_cap_num,
	vidioc_int_s_fmt_cap_num,
	vidioc_int_try_fmt_cap_num,
	vidioc_int_queryctrl_num,
	vidioc_int_g_ctrl_num,
	vidioc_int_s_ctrl_num,
	vidioc_int_cropcap_num,
	vidioc_int_g_crop_num,
	vidioc_int_s_crop_num,
	vidioc_int_g_parm_num,
	vidioc_int_s_parm_num,
	vidioc_int_querystd_num,
	vidioc_int_s_std_num,
	vidioc_int_s_video_routing_num,

	/*
	 *
	 * Strictly internal ioctls.
	 *
	 */
	/* Initialise the device when slave attaches to the master. */
	vidioc_int_dev_init_num = 1000,
	/* Delinitialise the device at slave detach. */
	vidioc_int_dev_exit_num,
	/* Set device power state. */
	vidioc_int_s_power_num,
	/*
	* Get slave private data, e.g. platform-specific slave
	* configuration used by the master.
	*/
	vidioc_int_g_priv_num,
	/* Get slave interface parameters. */
	vidioc_int_g_ifparm_num,
	/* Does the slave need to be reset after VIDIOC_DQBUF? */
	vidioc_int_g_needs_reset_num,
	vidioc_int_enum_framesizes_num,
	vidioc_int_enum_frameintervals_num,

	/*
	 *
	 * VIDIOC_INT_* ioctls.
	 *
	 */
	/* VIDIOC_INT_RESET */
	vidioc_int_reset_num,
	/* VIDIOC_INT_INIT */
	vidioc_int_init_num,
	/* VIDIOC_DBG_G_CHIP_IDENT */
	vidioc_int_g_chip_ident_num,

	/*
	 *
	 * Start of private ioctls.
	 *
	 */
	vidioc_int_priv_start_num = 2000,
};

/*
 *
 * IOCTL wrapper functions for better type checking.
 *
 */

#define V4L2_INT_WRAPPER_0(name)					\
	static inline int vidioc_int_##name(struct v4l2_int_device *d)	\
	{								\
		return v4l2_int_ioctl_0(d, vidioc_int_##name##_num);	\
	}								\
									\
	static inline struct v4l2_int_ioctl_desc			\
	vidioc_int_##name##_cb(int (*func)				\
			       (struct v4l2_int_device *))		\
	{								\
		struct v4l2_int_ioctl_desc desc;			\
									\
		desc.num = vidioc_int_##name##_num;			\
		desc.func = (v4l2_int_ioctl_func *)func;		\
									\
		return desc;						\
	}

#define V4L2_INT_WRAPPER_1(name, arg_type, asterisk)			\
	static inline int vidioc_int_##name(struct v4l2_int_device *d,	\
					    arg_type asterisk arg)	\
	{								\
		return v4l2_int_ioctl_1(d, vidioc_int_##name##_num,	\
					(void *)(unsigned long)arg);	\
	}								\
									\
	static inline struct v4l2_int_ioctl_desc			\
	vidioc_int_##name##_cb(int (*func)				\
			       (struct v4l2_int_device *,		\
				arg_type asterisk))			\
	{								\
		struct v4l2_int_ioctl_desc desc;			\
									\
		desc.num = vidioc_int_##name##_num;			\
		desc.func = (v4l2_int_ioctl_func *)func;		\
									\
		return desc;						\
	}

V4L2_INT_WRAPPER_1(enum_fmt_cap, struct v4l2_fmtdesc, *);
V4L2_INT_WRAPPER_1(g_fmt_cap, struct v4l2_format, *);
V4L2_INT_WRAPPER_1(s_fmt_cap, struct v4l2_format, *);
V4L2_INT_WRAPPER_1(try_fmt_cap, struct v4l2_format, *);
V4L2_INT_WRAPPER_1(queryctrl, struct v4l2_queryctrl, *);
V4L2_INT_WRAPPER_1(g_ctrl, struct v4l2_control, *);
V4L2_INT_WRAPPER_1(s_ctrl, struct v4l2_control, *);
V4L2_INT_WRAPPER_1(cropcap, struct v4l2_cropcap, *);
V4L2_INT_WRAPPER_1(g_crop, struct v4l2_crop, *);
V4L2_INT_WRAPPER_1(s_crop, struct v4l2_crop, *);
V4L2_INT_WRAPPER_1(g_parm, struct v4l2_streamparm, *);
V4L2_INT_WRAPPER_1(s_parm, struct v4l2_streamparm, *);
V4L2_INT_WRAPPER_1(querystd, v4l2_std_id, *);
V4L2_INT_WRAPPER_1(s_std, v4l2_std_id, *);
V4L2_INT_WRAPPER_1(s_video_routing, struct v4l2_routing, *);

V4L2_INT_WRAPPER_0(dev_init);
V4L2_INT_WRAPPER_0(dev_exit);
V4L2_INT_WRAPPER_1(s_power, enum v4l2_power, /*dummy arg*/);
V4L2_INT_WRAPPER_1(g_priv, void, *);
V4L2_INT_WRAPPER_1(g_ifparm, struct v4l2_ifparm, *);
V4L2_INT_WRAPPER_1(g_needs_reset, void, *);
V4L2_INT_WRAPPER_1(enum_framesizes, struct v4l2_frmsizeenum, *);
V4L2_INT_WRAPPER_1(enum_frameintervals, struct v4l2_frmivalenum, *);

V4L2_INT_WRAPPER_0(reset);
V4L2_INT_WRAPPER_0(init);
V4L2_INT_WRAPPER_1(g_chip_ident, int, *);

#endif
