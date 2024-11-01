/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_HPD_H_
#define _DP_HPD_H_

//#include <linux/usb/usbpd.h>

#include <linux/types.h>
#include <linux/device.h>

enum plug_orientation {
	ORIENTATION_NONE,
	ORIENTATION_CC1,
	ORIENTATION_CC2,
};

/**
 * struct dp_usbpd - DisplayPort status
 *
 * @orientation: plug orientation configuration
 * @low_pow_st: low power state
 * @adaptor_dp_en: adaptor functionality enabled
 * @multi_func: multi-function preferred
 * @usb_config_req: request to switch to usb
 * @exit_dp_mode: request exit from displayport mode
 * @hpd_irq: Change in the status since last message
 * @alt_mode_cfg_done: bool to specify alt mode status
 * @debug_en: bool to specify debug mode
 * @connect: simulate disconnect or connect for debug mode
 */
struct dp_usbpd {
	enum plug_orientation orientation;
	bool low_pow_st;
	bool adaptor_dp_en;
	bool multi_func;
	bool usb_config_req;
	bool exit_dp_mode;
	bool hpd_irq;
	bool alt_mode_cfg_done;
	bool debug_en;

	int (*connect)(struct dp_usbpd *dp_usbpd, bool hpd);
};

/**
 * struct dp_usbpd_cb - callback functions provided by the client
 *
 * @configure: called by usbpd module when PD communication has
 * been completed and the usb peripheral has been configured on
 * dp mode.
 * @disconnect: notify the cable disconnect issued by usb.
 * @attention: notify any attention message issued by usb.
 */
struct dp_usbpd_cb {
	int (*configure)(struct device *dev);
	int (*disconnect)(struct device *dev);
	int (*attention)(struct device *dev);
};

/**
 * dp_hpd_get() - setup hpd module
 *
 * @dev: device instance of the caller
 * @cb: struct containing callback function pointers.
 *
 * This function allows the client to initialize the usbpd
 * module. The module will communicate with HPD module.
 */
struct dp_usbpd *dp_hpd_get(struct device *dev, struct dp_usbpd_cb *cb);

int dp_hpd_register(struct dp_usbpd *dp_usbpd);
void dp_hpd_unregister(struct dp_usbpd *dp_usbpd);
int dp_hpd_connect(struct dp_usbpd *dp_usbpd, bool hpd);

#endif /* _DP_HPD_H_ */
