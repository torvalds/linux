/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#ifndef __ATOMISP_CSI2_H__
#define __ATOMISP_CSI2_H__

#include <linux/gpio/consumer.h>
#include <linux/property.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#include "../../include/linux/atomisp.h"

#define CSI2_PAD_SINK		0
#define CSI2_PAD_SOURCE		1
#define CSI2_PADS_NUM		2

#define CSI2_MAX_LANES		4
#define CSI2_MAX_LINK_FREQS	3

#define CSI2_MAX_ACPI_GPIOS	2u

struct acpi_device;
struct v4l2_device;

struct atomisp_device;
struct atomisp_sub_device;

struct atomisp_csi2_acpi_gpio_map {
	struct acpi_gpio_params params[CSI2_MAX_ACPI_GPIOS];
	struct acpi_gpio_mapping mapping[CSI2_MAX_ACPI_GPIOS + 1];
};

struct atomisp_csi2_acpi_gpio_parsing_data {
	struct acpi_device *adev;
	struct atomisp_csi2_acpi_gpio_map *map;
	u32 settings[CSI2_MAX_ACPI_GPIOS];
	unsigned int settings_count;
	unsigned int res_count;
	unsigned int map_count;
};

enum atomisp_csi2_sensor_swnodes {
	SWNODE_SENSOR,
	SWNODE_SENSOR_PORT,
	SWNODE_SENSOR_ENDPOINT,
	SWNODE_CSI2_PORT,
	SWNODE_CSI2_ENDPOINT,
	SWNODE_COUNT
};

struct atomisp_csi2_property_names {
	char clock_frequency[16];
	char rotation[9];
	char bus_type[9];
	char data_lanes[11];
	char remote_endpoint[16];
	char link_frequencies[17];
};

struct atomisp_csi2_node_names {
	char port[7];
	char endpoint[11];
	char remote_port[7];
};

struct atomisp_csi2_sensor_config {
	const char *hid;
	int lanes;
	int nr_link_freqs;
	u64 link_freqs[CSI2_MAX_LINK_FREQS];
};

struct atomisp_csi2_sensor {
	/* Append port in "-%u" format as suffix of HID */
	char name[ACPI_ID_LEN + 4];
	struct acpi_device *adev;
	int port;
	int lanes;

	/* SWNODE_COUNT + 1 for terminating NULL */
	const struct software_node *group[SWNODE_COUNT + 1];
	struct software_node swnodes[SWNODE_COUNT];
	struct atomisp_csi2_node_names node_names;
	struct atomisp_csi2_property_names prop_names;
	/* "clock-frequency", "rotation" + terminating entry */
	struct property_entry dev_properties[3];
	/* "bus-type", "data-lanes", "remote-endpoint" + "link-freq" + terminating entry */
	struct property_entry ep_properties[5];
	/* "data-lanes", "remote-endpoint" + terminating entry */
	struct property_entry csi2_properties[3];
	struct software_node_ref_args local_ref[1];
	struct software_node_ref_args remote_ref[1];
	struct software_node_ref_args vcm_ref[1];
	/* GPIO mappings storage */
	struct atomisp_csi2_acpi_gpio_map gpio_map;
};

struct atomisp_csi2_bridge {
	struct software_node csi2_node;
	char csi2_node_name[14];
	u32 data_lanes[CSI2_MAX_LANES];
	unsigned int n_sensors;
	struct atomisp_csi2_sensor sensors[ATOMISP_CAMERA_NR_PORTS];
};

struct atomisp_mipi_csi2_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[CSI2_PADS_NUM];
	struct v4l2_mbus_framefmt formats[CSI2_PADS_NUM];

	struct v4l2_ctrl_handler ctrls;
	struct atomisp_device *isp;
};

int atomisp_csi2_set_ffmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  unsigned int which, uint16_t pad,
			  struct v4l2_mbus_framefmt *ffmt);
int atomisp_mipi_csi2_init(struct atomisp_device *isp);
void atomisp_mipi_csi2_cleanup(struct atomisp_device *isp);
void atomisp_mipi_csi2_unregister_entities(
    struct atomisp_mipi_csi2_device *csi2);
int atomisp_mipi_csi2_register_entities(struct atomisp_mipi_csi2_device *csi2,
					struct v4l2_device *vdev);
int atomisp_csi2_bridge_init(struct atomisp_device *isp);
int atomisp_csi2_bridge_parse_firmware(struct atomisp_device *isp);

void atomisp_csi2_configure(struct atomisp_sub_device *asd);

#endif /* __ATOMISP_CSI2_H__ */
