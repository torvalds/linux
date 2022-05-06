/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _IFS_H_
#define _IFS_H_

#include <linux/device.h>
#include <linux/miscdevice.h>

/**
 * struct ifs_data - attributes related to intel IFS driver
 * @integrity_cap_bit: MSR_INTEGRITY_CAPS bit enumerating this test
 */
struct ifs_data {
	int	integrity_cap_bit;
};

struct ifs_device {
	struct ifs_data data;
	struct miscdevice misc;
};

void ifs_load_firmware(struct device *dev);

#endif
