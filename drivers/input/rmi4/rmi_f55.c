// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2015 Synaptics Incorporated
 * Copyright (C) 2016 Zodiac Inflight Innovations
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/slab.h>
#include "rmi_driver.h"

#define F55_NAME		"rmi4_f55"

/* F55 data offsets */
#define F55_NUM_RX_OFFSET	0
#define F55_NUM_TX_OFFSET	1
#define F55_PHYS_CHAR_OFFSET	2

/* Only read required query registers */
#define F55_QUERY_LEN		3

/* F55 capabilities */
#define F55_CAP_SENSOR_ASSIGN	BIT(0)

struct f55_data {
	struct rmi_function *fn;

	u8 qry[F55_QUERY_LEN];
	u8 num_rx_electrodes;
	u8 cfg_num_rx_electrodes;
	u8 num_tx_electrodes;
	u8 cfg_num_tx_electrodes;
};

static int rmi_f55_detect(struct rmi_function *fn)
{
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct rmi_driver_data *drv_data = dev_get_drvdata(&rmi_dev->dev);
	struct f55_data *f55;
	int error;

	f55 = dev_get_drvdata(&fn->dev);

	error = rmi_read_block(fn->rmi_dev, fn->fd.query_base_addr,
			       &f55->qry, sizeof(f55->qry));
	if (error) {
		dev_err(&fn->dev, "%s: Failed to query F55 properties\n",
			__func__);
		return error;
	}

	f55->num_rx_electrodes = f55->qry[F55_NUM_RX_OFFSET];
	f55->num_tx_electrodes = f55->qry[F55_NUM_TX_OFFSET];

	f55->cfg_num_rx_electrodes = f55->num_rx_electrodes;
	f55->cfg_num_tx_electrodes = f55->num_rx_electrodes;

	drv_data->num_rx_electrodes = f55->cfg_num_rx_electrodes;
	drv_data->num_tx_electrodes = f55->cfg_num_rx_electrodes;

	if (f55->qry[F55_PHYS_CHAR_OFFSET] & F55_CAP_SENSOR_ASSIGN) {
		int i, total;
		u8 buf[256];

		/*
		 * Calculate the number of enabled receive and transmit
		 * electrodes by reading F55:Ctrl1 (sensor receiver assignment)
		 * and F55:Ctrl2 (sensor transmitter assignment). The number of
		 * enabled electrodes is the sum of all field entries with a
		 * value other than 0xff.
		 */
		error = rmi_read_block(fn->rmi_dev,
				       fn->fd.control_base_addr + 1,
				       buf, f55->num_rx_electrodes);
		if (!error) {
			total = 0;
			for (i = 0; i < f55->num_rx_electrodes; i++) {
				if (buf[i] != 0xff)
					total++;
			}
			f55->cfg_num_rx_electrodes = total;
			drv_data->num_rx_electrodes = total;
		}

		error = rmi_read_block(fn->rmi_dev,
				       fn->fd.control_base_addr + 2,
				       buf, f55->num_tx_electrodes);
		if (!error) {
			total = 0;
			for (i = 0; i < f55->num_tx_electrodes; i++) {
				if (buf[i] != 0xff)
					total++;
			}
			f55->cfg_num_tx_electrodes = total;
			drv_data->num_tx_electrodes = total;
		}
	}

	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F55 num_rx_electrodes: %d (raw %d)\n",
		f55->cfg_num_rx_electrodes, f55->num_rx_electrodes);
	rmi_dbg(RMI_DEBUG_FN, &fn->dev, "F55 num_tx_electrodes: %d (raw %d)\n",
		f55->cfg_num_tx_electrodes, f55->num_tx_electrodes);

	return 0;
}

static int rmi_f55_probe(struct rmi_function *fn)
{
	struct f55_data *f55;

	f55 = devm_kzalloc(&fn->dev, sizeof(struct f55_data), GFP_KERNEL);
	if (!f55)
		return -ENOMEM;

	f55->fn = fn;
	dev_set_drvdata(&fn->dev, f55);

	return rmi_f55_detect(fn);
}

struct rmi_function_handler rmi_f55_handler = {
	.driver = {
		.name = F55_NAME,
	},
	.func = 0x55,
	.probe = rmi_f55_probe,
};
