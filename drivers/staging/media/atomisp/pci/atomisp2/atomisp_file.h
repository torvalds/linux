/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __ATOMISP_FILE_H__
#define __ATOMISP_FILE_H__

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

struct atomisp_device;

struct atomisp_file_device {
	struct v4l2_subdev sd;
	struct atomisp_device *isp;
	struct media_pad pads[1];

	struct workqueue_struct *work_queue;
	struct work_struct work;
};

void atomisp_file_input_cleanup(struct atomisp_device *isp);
int atomisp_file_input_init(struct atomisp_device *isp);
void atomisp_file_input_unregister_entities(
				struct atomisp_file_device *file_dev);
int atomisp_file_input_register_entities(struct atomisp_file_device *file_dev,
			struct v4l2_device *vdev);
#endif /* __ATOMISP_FILE_H__ */
