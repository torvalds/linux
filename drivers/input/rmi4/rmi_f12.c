// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/rmi.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>
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

#define F12_DATA1_BYTES_PER_OBJ			8
#define RMI_F12_QUERY_RESOLUTION		29

struct f12_data {
	struct rmi_2d_sensor sensor;
	struct rmi_2d_sensor_platform_data sensor_pdata;
	bool has_dribble;

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

	unsigned long irq_mask[];
};

static int rmi_f12_read_register_descs(struct rmi_function *fn,
				       struct f12_data *f12, u16 query_addr)
{
	struct {
		struct rmi_register_descriptor *desc;
		const char *name;
	} descriptors[] = {
		{ &f12->query_reg_desc, "Query" },
		{ &f12->control_reg_desc, "Control" },
		{ &f12->data_reg_desc, "Data" },
	};
	struct rmi_device *rmi_dev = fn->rmi_dev;
	int error;
	int i;

	for (i = 0; i < ARRAY_SIZE(descriptors); i++) {
		error = rmi_read_register_desc(rmi_dev, query_addr,
					       descriptors[i].desc);
		if (error) {
			dev_err(&fn->dev,
				"Failed to read the %s Register Descriptor: %d\n",
				descriptors[i].name, error);
			return error;
		}
		query_addr += 3;
	}

	return 0;
}

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
	int rx_receivers = 0;
	int tx_receivers = 0;
	u16 query_dpm_addr = 0;
	int dpm_resolution = 0;

	item = rmi_get_register_desc_item(&f12->control_reg_desc, 8);
	if (!item) {
		dev_err(&fn->dev,
			"F12 does not have the sensor tuning control register\n");
		return -ENODEV;
	}

	offset = rmi_register_desc_calc_reg_offset(&f12->control_reg_desc, 8);

	if (item->reg_size > sizeof(buf)) {
		dev_err(&fn->dev,
			"F12 control8 should be no bigger than %zd bytes, not: %u\n",
			sizeof(buf), item->reg_size);
		return -ENODEV;
	}

	ret = rmi_read_block(rmi_dev, fn->fd.control_base_addr + offset,
			     buf, item->reg_size);
	if (ret)
		return ret;

	offset = 0;
	if (rmi_register_desc_has_subpacket(item, 0)) {
		sensor->max_x = get_unaligned_le16(&buf[offset]);
		sensor->max_y = get_unaligned_le16(&buf[offset + 2]);
		offset += 4;
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: max_x: %d max_y: %d\n", __func__,
		sensor->max_x, sensor->max_y);

	if (rmi_register_desc_has_subpacket(item, 1)) {
		pitch_x = get_unaligned_le16(&buf[offset]);
		pitch_y = get_unaligned_le16(&buf[offset + 2]);
		offset += 4;
	}

	if (rmi_register_desc_has_subpacket(item, 2)) {
		/* Units 1/128 sensor pitch */
		rmi_dbg(RMI_DEBUG_FN, &fn->dev,
			"%s: Inactive Border xlo:%d xhi:%d ylo:%d yhi:%d\n",
			__func__,
			buf[offset], buf[offset + 1],
			buf[offset + 2], buf[offset + 3]);

		offset += 4;
	}

	/*
	 * Use the Query DPM feature when the resolution query register
	 * exists.
	 */
	if (rmi_get_register_desc_item(&f12->query_reg_desc,
				       RMI_F12_QUERY_RESOLUTION)) {
		offset = rmi_register_desc_calc_reg_offset(&f12->query_reg_desc,
							   RMI_F12_QUERY_RESOLUTION);
		query_dpm_addr = fn->fd.query_base_addr	+ offset;
		ret = rmi_read(fn->rmi_dev, query_dpm_addr, buf);
		if (ret) {
			dev_err(&fn->dev, "Failed to read DPM value: %d\n", ret);
			return ret;
		}
		dpm_resolution = buf[0];

		sensor->x_mm = sensor->max_x / dpm_resolution;
		sensor->y_mm = sensor->max_y / dpm_resolution;
	} else {
		if (rmi_register_desc_has_subpacket(item, 3)) {
			rx_receivers = buf[offset];
			tx_receivers = buf[offset + 1];
			offset += 2;
		}

		/* Skip over sensor flags */
		if (rmi_register_desc_has_subpacket(item, 4))
			offset += 1;

		sensor->x_mm = (pitch_x * rx_receivers) >> 12;
		sensor->y_mm = (pitch_y * tx_receivers) >> 12;
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: x_mm: %d y_mm: %d\n", __func__,
		sensor->x_mm, sensor->y_mm);

	return 0;
}

static void rmi_f12_process_objects(struct f12_data *f12, u8 *data1, u32 size)
{
	struct rmi_2d_sensor *sensor = &f12->sensor;
	u32 objects = min(f12->data1->num_subpackets, size / F12_DATA1_BYTES_PER_OBJ);
	int i;

	for (i = 0; i < objects; i++) {
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

		obj->x = get_unaligned_le16(&data1[1]);
		obj->y = get_unaligned_le16(&data1[3]);
		obj->z = data1[5];
		obj->wx = data1[6];
		obj->wy = data1[7];

		rmi_2d_sensor_abs_process(sensor, obj, i);

		data1 += F12_DATA1_BYTES_PER_OBJ;
	}

	if (sensor->kernel_tracking)
		input_mt_assign_slots(sensor->input,
				      sensor->tracking_slots,
				      sensor->tracking_pos,
				      sensor->nbr_fingers,
				      sensor->dmax);

	for (i = 0; i < objects; i++)
		rmi_2d_sensor_abs_report(sensor, &sensor->objs[i], i);
}

static irqreturn_t rmi_f12_attention(int irq, void *ctx)
{
	struct rmi_function *fn = ctx;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct rmi_driver_data *drvdata = dev_get_drvdata(&rmi_dev->dev);
	struct f12_data *f12 = dev_get_drvdata(&fn->dev);
	struct rmi_2d_sensor *sensor = &f12->sensor;
	u32 valid_bytes = sensor->pkt_size;
	int retval;

	if (drvdata->attn_data.data) {
		valid_bytes = min_t(u32, sensor->attn_size, drvdata->attn_data.size);
		memcpy(sensor->data_pkt, drvdata->attn_data.data, valid_bytes);
		drvdata->attn_data.data += valid_bytes;
		drvdata->attn_data.size -= valid_bytes;
	} else {
		retval = rmi_read_block(rmi_dev, f12->data_addr,
					sensor->data_pkt, sensor->pkt_size);
		if (retval < 0) {
			dev_err(&fn->dev, "Failed to read object data. Code: %d.\n",
				retval);
			return IRQ_RETVAL(retval);
		}
	}

	if (f12->data1)
		rmi_f12_process_objects(f12, &sensor->data_pkt[f12->data1_offset],
					valid_bytes);

	input_mt_sync_frame(sensor->input);

	return IRQ_HANDLED;
}

static int rmi_f12_update_dribble(struct rmi_function *fn, struct f12_data *f12)
{
	const struct rmi_register_desc_item *item;
	struct rmi_device *rmi_dev = fn->rmi_dev;
	u8 subpacket_offset = 0;
	u16 control_offset;
	u32 control_size;
	int error;
	u8 buf[3];

	item = rmi_get_register_desc_item(&f12->control_reg_desc, 20);
	if (!item)
		return 0;

	control_offset = rmi_register_desc_calc_reg_offset(&f12->control_reg_desc, 20);

	/*
	 * The byte containing the EnableDribble bit will be
	 * in either byte 0 or byte 2 of control 20. Depending
	 * on the existence of subpacket 0. If control 20 is
	 * larger then 3 bytes, just read the first 3.
	 */
	control_size = min(item->reg_size, 3U);

	error = rmi_read_block(rmi_dev, fn->fd.control_base_addr + control_offset,
			       buf, control_size);
	if (error)
		return error;

	if (rmi_register_desc_has_subpacket(item, 0))
		subpacket_offset += 1;

	switch (f12->sensor.dribble) {
	case RMI_REG_STATE_OFF:
		buf[subpacket_offset] &= ~BIT(2);
		break;
	case RMI_REG_STATE_ON:
		buf[subpacket_offset] |= BIT(2);
		break;
	case RMI_REG_STATE_DEFAULT:
	default:
		break;
	}

	error = rmi_write_block(rmi_dev, fn->fd.control_base_addr + control_offset,
				buf, control_size);
	if (error)
		return error;

	return 0;
}

static int rmi_f12_write_control_regs(struct rmi_function *fn)
{
	struct f12_data *f12 = dev_get_drvdata(&fn->dev);

	if (f12->has_dribble && f12->sensor.dribble != RMI_REG_STATE_DEFAULT)
		return rmi_f12_update_dribble(fn, f12);

	return 0;
}

static int rmi_f12_config(struct rmi_function *fn)
{
	struct rmi_driver *drv = fn->rmi_dev->driver;
	struct f12_data *f12 = dev_get_drvdata(&fn->dev);
	struct rmi_driver_data *drvdata = dev_get_drvdata(&fn->rmi_dev->dev);
	int irq_mask_size = BITS_TO_LONGS(drvdata->irq_count);
	unsigned long *abs_mask = f12->irq_mask;
	unsigned long *rel_mask = f12->irq_mask + irq_mask_size;
	struct rmi_2d_sensor *sensor;
	int ret;

	sensor = &f12->sensor;

	if (!sensor->report_abs)
		drv->clear_irq_bits(fn->rmi_dev, abs_mask);
	else
		drv->set_irq_bits(fn->rmi_dev, abs_mask);

	drv->clear_irq_bits(fn->rmi_dev, rel_mask);

	ret = rmi_f12_write_control_regs(fn);
	if (ret)
		dev_warn(&fn->dev,
			 "Failed to write F12 control registers: %d\n", ret);

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
	struct rmi_driver_data *drvdata = dev_get_drvdata(&rmi_dev->dev);
	size_t data_offset = 0;
	size_t pkt_size;
	int irq_mask_size;
	int i;

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s\n", __func__);

	irq_mask_size = BITS_TO_LONGS(drvdata->irq_count);

	ret = rmi_read(fn->rmi_dev, query_addr, &buf);
	if (ret < 0) {
		dev_err(&fn->dev, "Failed to read general info register: %d\n",
			ret);
		return -ENODEV;
	}
	++query_addr;

	if (!(buf & BIT(0))) {
		dev_err(&fn->dev,
			"Behavior of F12 without register descriptors is undefined.\n");
		return -ENODEV;
	}

	f12 = devm_kzalloc(&fn->dev, struct_size(f12, irq_mask, irq_mask_size * 2),
			   GFP_KERNEL);
	if (!f12)
		return -ENOMEM;

	set_bit(fn->irq_pos, f12->irq_mask);
	set_bit(fn->irq_pos + 1, f12->irq_mask + irq_mask_size);

	f12->has_dribble = !!(buf & BIT(3));

	if (fn->dev.of_node) {
		ret = rmi_2d_sensor_of_probe(&fn->dev, &f12->sensor_pdata);
		if (ret)
			return ret;
	} else {
		f12->sensor_pdata = pdata->sensor_pdata;
	}

	ret = rmi_f12_read_register_descs(fn, f12, query_addr);
	if (ret)
		return ret;

	sensor = &f12->sensor;
	sensor->fn = fn;
	f12->data_addr = fn->fd.data_base_addr;
	pkt_size = rmi_register_desc_calc_size(&f12->data_reg_desc);
	if (pkt_size > SZ_1M) {
		dev_err(&fn->dev, "Invalid data packet size: %zu\n", pkt_size);
		return -EINVAL;
	}
	sensor->pkt_size = pkt_size;

	sensor->axis_align = f12->sensor_pdata.axis_align;

	sensor->x_mm = f12->sensor_pdata.x_mm;
	sensor->y_mm = f12->sensor_pdata.y_mm;
	sensor->dribble = f12->sensor_pdata.dribble;

	if (sensor->sensor_type == rmi_sensor_default)
		sensor->sensor_type = f12->sensor_pdata.sensor_type;

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "%s: data packet size: %u\n", __func__,
		sensor->pkt_size);
	sensor->data_pkt = devm_kmalloc(&fn->dev, sensor->pkt_size, GFP_KERNEL);
	if (!sensor->data_pkt)
		return -ENOMEM;

	dev_set_drvdata(&fn->dev, f12);

	ret = rmi_f12_read_sensor_tuning(f12);
	if (ret)
		return ret;

	/*
	 * Identify available data registers and calculate their offsets within
	 * the attention report. For HID devices, only Data1 and Data5 are
	 * included in the report; other registers may be described but are
	 * not transmitted in the attention packet and thus skipped here.
	 */
	for (i = 0; i < 16; i++) {
		item = rmi_get_register_desc_item(&f12->data_reg_desc, i);
		if (!item)
			continue;

		/* HID attention reports only contain Data1 and Data5 */
		if (drvdata->attn_data.data && i != 1 && i != 5)
			continue;

		if (data_offset > U16_MAX) {
			dev_err(&fn->dev, "Invalid offset for data%d: %zu\n",
				i, data_offset);
			return -EINVAL;
		}

		switch (i) {
		case 1:
			f12->data1 = item;
			f12->data1_offset = data_offset;

			if (item->num_subpackets > 255) {
				dev_err(&fn->dev,
					"Too many fingers declared: %d\n",
					item->num_subpackets);
				return -EINVAL;
			}

			sensor->nbr_fingers = item->num_subpackets;
			sensor->report_abs = 1;
			sensor->attn_size += item->reg_size;
			break;

		case 5:
			f12->data5 = item;
			f12->data5_offset = data_offset;
			sensor->attn_size += item->reg_size;
			break;

		case 6:
			f12->data6 = item;
			f12->data6_offset = data_offset;
			break;

		case 9:
			f12->data9 = item;
			f12->data9_offset = data_offset;
			if (!sensor->report_abs)
				sensor->report_rel = 1;
			break;

		case 15:
			f12->data15 = item;
			f12->data15_offset = data_offset;
			break;
		}

		data_offset += item->reg_size;
	}

	/* allocate the in-kernel tracking buffers */
	sensor->tracking_pos = devm_kcalloc(&fn->dev, sensor->nbr_fingers,
					    sizeof(*sensor->tracking_pos),
					    GFP_KERNEL);
	if (!sensor->tracking_pos)
		return -ENOMEM;

	sensor->tracking_slots = devm_kcalloc(&fn->dev, sensor->nbr_fingers,
					      sizeof(*sensor->tracking_slots),
					      GFP_KERNEL);
	if (!sensor->tracking_slots)
		return -ENOMEM;

	sensor->objs = devm_kcalloc(&fn->dev, sensor->nbr_fingers,
				    sizeof(*sensor->objs), GFP_KERNEL);
	if (!sensor->objs)
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
