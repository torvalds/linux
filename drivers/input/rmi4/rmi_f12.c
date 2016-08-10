/*
 * Copyright (c) 2012-2016 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/rmi.h>
#include "rmi_driver.h"
#include "rmi_2d_sensor.h"

enum rmi_f12_object_type {
	RMI_F12_OBJECT_NONE			= 0x00,
	RMI_F12_OBJECT_FINGER			= 0x01,
	RMI_F12_OBJECT_STYLUS			= 0x02,
	RMI_F12_OBJECT_PALM			= 0x03,
	RMI_F12_OBJECT_UNCLASSIFIED		= 0x04,
	RMI_F12_OBJECT_GLOVED_FINGER		= 0x06,
	RMI_F12_OBJECT_NARROW_OBJECT		= 0x07,
	RMI_F12_OBJECT_HAND_EDGE		= 0x08,
	RMI_F12_OBJECT_COVER			= 0x0A,
	RMI_F12_OBJECT_STYLUS_2			= 0x0B,
	RMI_F12_OBJECT_ERASER			= 0x0C,
	RMI_F12_OBJECT_SMALL_OBJECT		= 0x0D,
};

struct f12_data {
	struct rmi_2d_sensor sensor;
	struct rmi_2d_sensor_platform_data sensor_pdata;

	u16 data_addr;

	struct rmi_register_descriptor query_reg_desc;
	struct rmi_register_descriptor control_reg_desc;
	struct rmi_register_descriptor data_reg_desc;

	/* F12 Data1 describes sensed objects */
	const struct rmi_register_desc_item *data1;
	u16 data1_offset;

	/* F12 Data5 describes finger ACM */
	const struct rmi_register_desc_item *data5;
	u16 data5_offset;

	/* F12 Data5 describes Pen */
	const struct rmi_register_desc_item *data6;
	u16 data6_offset;


	/* F12 Data9 reports relative data */
	const struct rmi_register_desc_item *data9;
	u16 data9_offset;

	const struct rmi_register_desc_item *data15;
	u16 data15_offset;
};

static int rmi_f12_read_sensor_tuning(struct f12_data *f12)
{
	const struct rmi_register_desc_item *item;
	struct rmi_2d_sensor *sensor = &f12->sensor;
	struct rmi_function *fn = sensor->fn;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	int ret;
	int offset;
	u8 buf[15];
	int pitch_x = 0;
	int pitch_y = 0;
	int clip_x_low = 0;
	int clip_x_high = 0;
	int clip_y_low = 0;
	int clip_y_high = 0;
	int rx_receivers = 0;
	int tx_receivers = 0;
	int sensor_flags = 0;

	item = rmi_get_register_desc_item(&f12->control_reg_desc, 8);
	if (!item) {
		dev_err(&fn->dev,
			"F12 does not have the sensor tuning control register\n");
		return -ENODEV;
	}

	offset = rmi_register_desc_calc_reg_offset(&f12->control_reg_desc, 8);

	if (item->reg_size > sizeof(buf)) {
		dev_err(&fn->dev,
			"F12 control8 should be no bigger than %zd bytes, not: %ld\n",
			sizeof(buf), item->reg_size);
		return -ENODEV;
	}

	ret = rmi_read_block(rmi_dev, fn->fd.control_base_addr + offset, buf,
				item->reg_size);
	if (ret)
		return ret;

	offset = 0;
	if (rmi_register_desc_has_subpacket(item, 0)) {
		sensor->max_x = (buf[offset + 1] << 8) | buf[offset];
		sensor->max_y = (buf[offset + 3] << 8) | buf[offset + 2];
		offset += 4;
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: max_x: %d max_y: %d\n", __func__,
		sensor->max_x, sensor->max_y);

	if (rmi_register_desc_has_subpacket(item, 1)) {
		pitch_x = (buf[offset + 1] << 8) | buf[offset];
		pitch_y	= (buf[offset + 3] << 8) | buf[offset + 2];
		offset += 4;
	}

	if (rmi_register_desc_has_subpacket(item, 2)) {
		sensor->axis_align.clip_x_low = buf[offset];
		sensor->axis_align.clip_x_high = sensor->max_x
							- buf[offset + 1];
		sensor->axis_align.clip_y_low = buf[offset + 2];
		sensor->axis_align.clip_y_high = sensor->max_y
							- buf[offset + 3];
		offset += 4;
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: x low: %d x high: %d y low: %d y high: %d\n",
		__func__, clip_x_low, clip_x_high, clip_y_low, clip_y_high);

	if (rmi_register_desc_has_subpacket(item, 3)) {
		rx_receivers = buf[offset];
		tx_receivers = buf[offset + 1];
		offset += 2;
	}

	if (rmi_register_desc_has_subpacket(item, 4)) {
		sensor_flags = buf[offset];
		offset += 1;
	}

	sensor->x_mm = (pitch_x * rx_receivers) >> 12;
	sensor->y_mm = (pitch_y * tx_receivers) >> 12;

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: x_mm: %d y_mm: %d\n", __func__,
		sensor->x_mm, sensor->y_mm);

	return 0;
}

static void rmi_f12_process_objects(struct f12_data *f12, u8 *data1)
{
	int i;
	struct rmi_2d_sensor *sensor = &f12->sensor;

	for (i = 0; i < f12->data1->num_subpackets; i++) {
		struct rmi_2d_sensor_abs_object *obj = &sensor->objs[i];

		obj->type = RMI_2D_OBJECT_NONE;
		obj->mt_tool = MT_TOOL_FINGER;

		switch (data1[0]) {
		case RMI_F12_OBJECT_FINGER:
			obj->type = RMI_2D_OBJECT_FINGER;
			break;
		case RMI_F12_OBJECT_STYLUS:
			obj->type = RMI_2D_OBJECT_STYLUS;
			obj->mt_tool = MT_TOOL_PEN;
			break;
		case RMI_F12_OBJECT_PALM:
			obj->type = RMI_2D_OBJECT_PALM;
			obj->mt_tool = MT_TOOL_PALM;
			break;
		case RMI_F12_OBJECT_UNCLASSIFIED:
			obj->type = RMI_2D_OBJECT_UNCLASSIFIED;
			break;
		}

		obj->x = (data1[2] << 8) | data1[1];
		obj->y = (data1[4] << 8) | data1[3];
		obj->z = data1[5];
		obj->wx = data1[6];
		obj->wy = data1[7];

		rmi_2d_sensor_abs_process(sensor, obj, i);

		data1 += 8;
	}

	if (sensor->kernel_tracking)
		input_mt_assign_slots(sensor->input,
				      sensor->tracking_slots,
				      sensor->tracking_pos,
				      sensor->nbr_fingers,
				      sensor->dmax);

	for (i = 0; i < sensor->nbr_fingers; i++)
		rmi_2d_sensor_abs_report(sensor, &sensor->objs[i], i);
}

static int rmi_f12_attention(struct rmi_function *fn,
			     unsigned long *irq_nr_regs)
{
	int retval;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct f12_data *f12 = dev_get_drvdata(&fn->dev);
	struct rmi_2d_sensor *sensor = &f12->sensor;

	if (rmi_dev->xport->attn_data) {
		memcpy(sensor->data_pkt, rmi_dev->xport->attn_data,
			sensor->attn_size);
		rmi_dev->xport->attn_data += sensor->attn_size;
		rmi_dev->xport->attn_size -= sensor->attn_size;
	} else {
		retval = rmi_read_block(rmi_dev, f12->data_addr,
					sensor->data_pkt, sensor->pkt_size);
		if (retval < 0) {
			dev_err(&fn->dev, "Failed to read object data. Code: %d.\n",
				retval);
			return retval;
		}
	}

	if (f12->data1)
		rmi_f12_process_objects(f12,
			&sensor->data_pkt[f12->data1_offset]);

	input_mt_sync_frame(sensor->input);

	return 0;
}

static int rmi_f12_config(struct rmi_function *fn)
{
	struct rmi_driver *drv = fn->rmi_dev->driver;

	drv->set_irq_bits(fn->rmi_dev, fn->irq_mask);

	return 0;
}

static int rmi_f12_probe(struct rmi_function *fn)
{
	struct f12_data *f12;
	int ret;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	char buf;
	u16 query_addr = fn->fd.query_base_addr;
	const struct rmi_register_desc_item *item;
	struct rmi_2d_sensor *sensor;
	struct rmi_device_platform_data *pdata = rmi_get_platform_data(rmi_dev);
	struct rmi_transport_dev *xport = rmi_dev->xport;
	u16 data_offset = 0;

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s\n", __func__);

	ret = rmi_read(fn->rmi_dev, query_addr, &buf);
	if (ret < 0) {
		dev_err(&fn->dev, "Failed to read general info register: %d\n",
			ret);
		return -ENODEV;
	}
	++query_addr;

	if (!(buf & 0x1)) {
		dev_err(&fn->dev,
			"Behavior of F12 without register descriptors is undefined.\n");
		return -ENODEV;
	}

	f12 = devm_kzalloc(&fn->dev, sizeof(struct f12_data), GFP_KERNEL);
	if (!f12)
		return -ENOMEM;

	if (fn->dev.of_node) {
		ret = rmi_2d_sensor_of_probe(&fn->dev, &f12->sensor_pdata);
		if (ret)
			return ret;
	} else if (pdata->sensor_pdata) {
		f12->sensor_pdata = *pdata->sensor_pdata;
	}

	ret = rmi_read_register_desc(rmi_dev, query_addr,
					&f12->query_reg_desc);
	if (ret) {
		dev_err(&fn->dev,
			"Failed to read the Query Register Descriptor: %d\n",
			ret);
		return ret;
	}
	query_addr += 3;

	ret = rmi_read_register_desc(rmi_dev, query_addr,
						&f12->control_reg_desc);
	if (ret) {
		dev_err(&fn->dev,
			"Failed to read the Control Register Descriptor: %d\n",
			ret);
		return ret;
	}
	query_addr += 3;

	ret = rmi_read_register_desc(rmi_dev, query_addr,
						&f12->data_reg_desc);
	if (ret) {
		dev_err(&fn->dev,
			"Failed to read the Data Register Descriptor: %d\n",
			ret);
		return ret;
	}
	query_addr += 3;

	sensor = &f12->sensor;
	sensor->fn = fn;
	f12->data_addr = fn->fd.data_base_addr;
	sensor->pkt_size = rmi_register_desc_calc_size(&f12->data_reg_desc);

	sensor->axis_align =
		f12->sensor_pdata.axis_align;

	sensor->x_mm = f12->sensor_pdata.x_mm;
	sensor->y_mm = f12->sensor_pdata.y_mm;

	if (sensor->sensor_type == rmi_sensor_default)
		sensor->sensor_type =
			f12->sensor_pdata.sensor_type;

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: data packet size: %d\n", __func__,
		sensor->pkt_size);
	sensor->data_pkt = devm_kzalloc(&fn->dev, sensor->pkt_size, GFP_KERNEL);
	if (!sensor->data_pkt)
		return -ENOMEM;

	dev_set_drvdata(&fn->dev, f12);

	ret = rmi_f12_read_sensor_tuning(f12);
	if (ret)
		return ret;

	/*
	 * Figure out what data is contained in the data registers. HID devices
	 * may have registers defined, but their data is not reported in the
	 * HID attention report. Registers which are not reported in the HID
	 * attention report check to see if the device is receiving data from
	 * HID attention reports.
	 */
	item = rmi_get_register_desc_item(&f12->data_reg_desc, 0);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 1);
	if (item) {
		f12->data1 = item;
		f12->data1_offset = data_offset;
		data_offset += item->reg_size;
		sensor->nbr_fingers = item->num_subpackets;
		sensor->report_abs = 1;
		sensor->attn_size += item->reg_size;
	}

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 2);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 3);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 4);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 5);
	if (item) {
		f12->data5 = item;
		f12->data5_offset = data_offset;
		data_offset += item->reg_size;
		sensor->attn_size += item->reg_size;
	}

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 6);
	if (item && !xport->attn_data) {
		f12->data6 = item;
		f12->data6_offset = data_offset;
		data_offset += item->reg_size;
	}

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 7);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 8);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 9);
	if (item && !xport->attn_data) {
		f12->data9 = item;
		f12->data9_offset = data_offset;
		data_offset += item->reg_size;
		if (!sensor->report_abs)
			sensor->report_rel = 1;
	}

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 10);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 11);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 12);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 13);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 14);
	if (item && !xport->attn_data)
		data_offset += item->reg_size;

	item = rmi_get_register_desc_item(&f12->data_reg_desc, 15);
	if (item && !xport->attn_data) {
		f12->data15 = item;
		f12->data15_offset = data_offset;
		data_offset += item->reg_size;
	}

	/* allocate the in-kernel tracking buffers */
	sensor->tracking_pos = devm_kzalloc(&fn->dev,
			sizeof(struct input_mt_pos) * sensor->nbr_fingers,
			GFP_KERNEL);
	sensor->tracking_slots = devm_kzalloc(&fn->dev,
			sizeof(int) * sensor->nbr_fingers, GFP_KERNEL);
	sensor->objs = devm_kzalloc(&fn->dev,
			sizeof(struct rmi_2d_sensor_abs_object)
			* sensor->nbr_fingers, GFP_KERNEL);
	if (!sensor->tracking_pos || !sensor->tracking_slots || !sensor->objs)
		return -ENOMEM;

	ret = rmi_2d_sensor_configure_input(fn, sensor);
	if (ret)
		return ret;

	return 0;
}

struct rmi_function_handler rmi_f12_handler = {
	.driver = {
		.name = "rmi4_f12",
	},
	.func = 0x12,
	.probe = rmi_f12_probe,
	.config = rmi_f12_config,
	.attention = rmi_f12_attention,
};
