// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SFH tablet-mode switch driver
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include <linux/amd-pmf-io.h>
#include <linux/auxiliary_bus.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/pci_ids.h>

#define POLL_INTERVAL_MS	200

static void sfh_tm_poll(struct input_dev *input)
{
	struct amd_sfh_info info = {};

	if (amd_get_sfh_info(&info, MT_OP_MODE))
		return;

	input_report_switch(input, SW_TABLET_MODE,
			    info.op_mode == SFH_MODE_TABLET);
	input_sync(input);
}

static int sfh_tm_probe(struct auxiliary_device *auxdev,
			const struct auxiliary_device_id *id)
{
	struct device *dev = &auxdev->dev;
	struct amd_sfh_info info = {};
	struct input_dev *input;
	int error;

	error = amd_get_sfh_info(&info, MT_OP_MODE);
	if (error)
		return error == -EINVAL ? -ENODEV : error;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name		= "AMD SFH tablet mode switch";
	input->phys		= "amd-sfh/tabletmode";
	input->id.bustype	= BUS_HOST;
	input->id.vendor	= PCI_VENDOR_ID_AMD;
	input_set_capability(input, EV_SW, SW_TABLET_MODE);

	error = input_setup_polling(input, sfh_tm_poll);
	if (error)
		return error;
	input_set_poll_interval(input, POLL_INTERVAL_MS);

	sfh_tm_poll(input);

	error = input_register_device(input);
	if (error)
		return error;

	return 0;
}

static const struct auxiliary_device_id sfh_tm_id_table[] = {
	{ .name = "amd_sfh.tabletmode" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, sfh_tm_id_table);

static struct auxiliary_driver sfh_tm_driver = {
	.name		= "tabletmode",
	.id_table	= sfh_tm_id_table,
	.probe		= sfh_tm_probe,
};
module_auxiliary_driver(sfh_tm_driver);

MODULE_DESCRIPTION("AMD SFH tablet mode switch");
MODULE_LICENSE("GPL");
