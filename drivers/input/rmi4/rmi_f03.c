/*
 * Copyright (C) 2015-2016 Red Hat
 * Copyright (C) 2015 Lyude Paul <thatslyude@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/serio.h>
#include <linux/notifier.h>
#include "rmi_driver.h"

#define RMI_F03_RX_DATA_OFB		0x01
#define RMI_F03_OB_SIZE			2

#define RMI_F03_OB_OFFSET		2
#define RMI_F03_OB_DATA_OFFSET		1
#define RMI_F03_OB_FLAG_TIMEOUT		BIT(6)
#define RMI_F03_OB_FLAG_PARITY		BIT(7)

#define RMI_F03_DEVICE_COUNT		0x07
#define RMI_F03_BYTES_PER_DEVICE	0x07
#define RMI_F03_BYTES_PER_DEVICE_SHIFT	4
#define RMI_F03_QUEUE_LENGTH		0x0F

struct f03_data {
	struct rmi_function *fn;

	struct serio *serio;

	u8 device_count;
	u8 rx_queue_length;
};

static int rmi_f03_pt_write(struct serio *id, unsigned char val)
{
	struct f03_data *f03 = id->port_data;
	int error;

	rmi_dbg(RMI_DEBUG_FN, &f03->fn->dev,
		"%s: Wrote %.2hhx to PS/2 passthrough address",
		__func__, val);

	error = rmi_write(f03->fn->rmi_dev, f03->fn->fd.data_base_addr, val);
	if (error) {
		dev_err(&f03->fn->dev,
			"%s: Failed to write to F03 TX register (%d).\n",
			__func__, error);
		return error;
	}

	return 0;
}

static int rmi_f03_initialize(struct f03_data *f03)
{
	struct rmi_function *fn = f03->fn;
	struct device *dev = &fn->dev;
	int error;
	u8 bytes_per_device;
	u8 query1;
	u8 query2[RMI_F03_DEVICE_COUNT * RMI_F03_BYTES_PER_DEVICE];
	size_t query2_len;

	error = rmi_read(fn->rmi_dev, fn->fd.query_base_addr, &query1);
	if (error) {
		dev_err(dev, "Failed to read query register (%d).\n", error);
		return error;
	}

	f03->device_count = query1 & RMI_F03_DEVICE_COUNT;
	bytes_per_device = (query1 >> RMI_F03_BYTES_PER_DEVICE_SHIFT) &
				RMI_F03_BYTES_PER_DEVICE;

	query2_len = f03->device_count * bytes_per_device;

	/*
	 * The first generation of image sensors don't have a second part to
	 * their f03 query, as such we have to set some of these values manually
	 */
	if (query2_len < 1) {
		f03->device_count = 1;
		f03->rx_queue_length = 7;
	} else {
		error = rmi_read_block(fn->rmi_dev, fn->fd.query_base_addr + 1,
				       query2, query2_len);
		if (error) {
			dev_err(dev,
				"Failed to read second set of query registers (%d).\n",
				error);
			return error;
		}

		f03->rx_queue_length = query2[0] & RMI_F03_QUEUE_LENGTH;
	}

	return 0;
}

static int rmi_f03_register_pt(struct f03_data *f03)
{
	struct serio *serio;

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio)
		return -ENOMEM;

	serio->id.type = SERIO_8042;
	serio->write = rmi_f03_pt_write;
	serio->port_data = f03;

	strlcpy(serio->name, "Synaptics RMI4 PS/2 pass-through",
		sizeof(serio->name));
	strlcpy(serio->phys, "synaptics-rmi4-pt/serio1",
		sizeof(serio->phys));
	serio->dev.parent = &f03->fn->dev;

	f03->serio = serio;

	serio_register_port(serio);

	return 0;
}

static int rmi_f03_probe(struct rmi_function *fn)
{
	struct device *dev = &fn->dev;
	struct f03_data *f03;
	int error;

	f03 = devm_kzalloc(dev, sizeof(struct f03_data), GFP_KERNEL);
	if (!f03)
		return -ENOMEM;

	f03->fn = fn;

	error = rmi_f03_initialize(f03);
	if (error < 0)
		return error;

	if (f03->device_count != 1)
		dev_warn(dev, "found %d devices on PS/2 passthrough",
			 f03->device_count);

	dev_set_drvdata(dev, f03);

	error = rmi_f03_register_pt(f03);
	if (error)
		return error;

	return 0;
}

static int rmi_f03_config(struct rmi_function *fn)
{
	fn->rmi_dev->driver->set_irq_bits(fn->rmi_dev, fn->irq_mask);

	return 0;
}

static int rmi_f03_attention(struct rmi_function *fn, unsigned long *irq_bits)
{
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct rmi_driver_data *drvdata = dev_get_drvdata(&rmi_dev->dev);
	struct f03_data *f03 = dev_get_drvdata(&fn->dev);
	u16 data_addr = fn->fd.data_base_addr;
	const u8 ob_len = f03->rx_queue_length * RMI_F03_OB_SIZE;
	u8 obs[RMI_F03_QUEUE_LENGTH * RMI_F03_OB_SIZE];
	u8 ob_status;
	u8 ob_data;
	unsigned int serio_flags;
	int i;
	int error;

	if (!rmi_dev)
		return -ENODEV;

	if (drvdata->attn_data.data) {
		/* First grab the data passed by the transport device */
		if (drvdata->attn_data.size < ob_len) {
			dev_warn(&fn->dev, "F03 interrupted, but data is missing!\n");
			return 0;
		}

		memcpy(obs, drvdata->attn_data.data, ob_len);

		drvdata->attn_data.data += ob_len;
		drvdata->attn_data.size -= ob_len;
	} else {
		/* Grab all of the data registers, and check them for data */
		error = rmi_read_block(fn->rmi_dev, data_addr + RMI_F03_OB_OFFSET,
				       &obs, ob_len);
		if (error) {
			dev_err(&fn->dev,
				"%s: Failed to read F03 output buffers: %d\n",
				__func__, error);
			serio_interrupt(f03->serio, 0, SERIO_TIMEOUT);
			return error;
		}
	}

	for (i = 0; i < ob_len; i += RMI_F03_OB_SIZE) {
		ob_status = obs[i];
		ob_data = obs[i + RMI_F03_OB_DATA_OFFSET];
		serio_flags = 0;

		if (!(ob_status & RMI_F03_RX_DATA_OFB))
			continue;

		if (ob_status & RMI_F03_OB_FLAG_TIMEOUT)
			serio_flags |= SERIO_TIMEOUT;
		if (ob_status & RMI_F03_OB_FLAG_PARITY)
			serio_flags |= SERIO_PARITY;

		rmi_dbg(RMI_DEBUG_FN, &fn->dev,
			"%s: Received %.2hhx from PS2 guest T: %c P: %c\n",
			__func__, ob_data,
			serio_flags & SERIO_TIMEOUT ?  'Y' : 'N',
			serio_flags & SERIO_PARITY ? 'Y' : 'N');

		serio_interrupt(f03->serio, ob_data, serio_flags);
	}

	return 0;
}

static void rmi_f03_remove(struct rmi_function *fn)
{
	struct f03_data *f03 = dev_get_drvdata(&fn->dev);

	serio_unregister_port(f03->serio);
}

struct rmi_function_handler rmi_f03_handler = {
	.driver = {
		.name = "rmi4_f03",
	},
	.func = 0x03,
	.probe = rmi_f03_probe,
	.config = rmi_f03_config,
	.attention = rmi_f03_attention,
	.remove = rmi_f03_remove,
};

MODULE_AUTHOR("Lyude Paul <thatslyude@gmail.com>");
MODULE_DESCRIPTION("RMI F03 module");
MODULE_LICENSE("GPL");
