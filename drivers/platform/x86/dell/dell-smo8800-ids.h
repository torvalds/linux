/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ACPI SMO88XX lis3lv02d freefall / accelerometer device-ids.
 *
 *  Copyright (C) 2012 Sonal Santan <sonal.santan@gmail.com>
 *  Copyright (C) 2014 Pali Rohár <pali@kernel.org>
 */
#ifndef _DELL_SMO8800_IDS_H_
#define _DELL_SMO8800_IDS_H_

#include <linux/mod_devicetable.h>
#include <linux/module.h>

static const struct acpi_device_id smo8800_ids[] = {
	{ "SMO8800" },
	{ "SMO8801" },
	{ "SMO8810" },
	{ "SMO8811" },
	{ "SMO8820" },
	{ "SMO8821" },
	{ "SMO8830" },
	{ "SMO8831" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, smo8800_ids);

#endif
