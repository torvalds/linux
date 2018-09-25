/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Private data and functions for adjunct processor VFIO matrix driver.
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *
 * Copyright IBM Corp. 2018
 */

#ifndef _VFIO_AP_PRIVATE_H_
#define _VFIO_AP_PRIVATE_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mdev.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include "ap_bus.h"

#define VFIO_AP_MODULE_NAME "vfio_ap"
#define VFIO_AP_DRV_NAME "vfio_ap"

/**
 * ap_matrix_dev - the AP matrix device structure
 * @device:	generic device structure associated with the AP matrix device
 */
struct ap_matrix_dev {
	struct device device;
};

extern struct ap_matrix_dev *matrix_dev;

#endif /* _VFIO_AP_PRIVATE_H_ */
