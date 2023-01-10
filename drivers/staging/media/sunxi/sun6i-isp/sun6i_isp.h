/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_ISP_H_
#define _SUN6I_ISP_H_

#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_isp_capture.h"
#include "sun6i_isp_params.h"
#include "sun6i_isp_proc.h"

#define SUN6I_ISP_NAME			"sun6i-isp"
#define SUN6I_ISP_DESCRIPTION		"Allwinner A31 ISP Device"

enum sun6i_isp_port {
	SUN6I_ISP_PORT_CSI0	= 0,
	SUN6I_ISP_PORT_CSI1	= 1,
};

struct sun6i_isp_buffer {
	struct vb2_v4l2_buffer	v4l2_buffer;
	struct list_head	list;
};

struct sun6i_isp_v4l2 {
	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;
};

struct sun6i_isp_table {
	void		*data;
	dma_addr_t	address;
	unsigned int	size;
};

struct sun6i_isp_tables {
	struct sun6i_isp_table	load;
	struct sun6i_isp_table	save;

	struct sun6i_isp_table	lut;
	struct sun6i_isp_table	drc;
	struct sun6i_isp_table	stats;
};

struct sun6i_isp_device {
	struct device			*dev;

	struct sun6i_isp_tables		tables;

	struct sun6i_isp_v4l2		v4l2;
	struct sun6i_isp_proc		proc;
	struct sun6i_isp_capture	capture;
	struct sun6i_isp_params		params;

	struct regmap			*regmap;
	struct clk			*clock_mod;
	struct clk			*clock_ram;
	struct reset_control		*reset;

	spinlock_t			state_lock; /* State helpers lock. */
};

struct sun6i_isp_variant {
	unsigned int	table_load_save_size;
	unsigned int	table_lut_size;
	unsigned int	table_drc_size;
	unsigned int	table_stats_size;
};

/* Helpers */

u32 sun6i_isp_load_read(struct sun6i_isp_device *isp_dev, u32 offset);
void sun6i_isp_load_write(struct sun6i_isp_device *isp_dev, u32 offset,
			  u32 value);
u32 sun6i_isp_address_value(dma_addr_t address);

/* State */

void sun6i_isp_state_update(struct sun6i_isp_device *isp_dev, bool ready_hold);

/* Tables */

void sun6i_isp_tables_configure(struct sun6i_isp_device *isp_dev);

#endif
