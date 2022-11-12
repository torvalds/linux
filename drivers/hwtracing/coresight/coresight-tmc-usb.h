/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_TMC_USB_H
#define _CORESIGHT_TMC_USB_H

#include <linux/amba/bus.h>
#include <linux/usb/usb_qdss.h>

#define TMC_USB_BAM_PIPE_INDEX	0
#define TMC_USB_BAM_NR_PIPES	2

enum tmc_etr_usb_mode {
	TMC_ETR_USB_NONE,
	TMC_ETR_USB_SW,
};

struct tmc_usb_data {
	enum tmc_etr_usb_mode	usb_mode;
	struct usb_qdss_ch	*usbch;
	struct tmc_drvdata	*tmcdrvdata;
	bool			data_overwritten;
	u64			drop_data_size;
};

extern int tmc_usb_enable(struct tmc_usb_data *usb_data);
extern void tmc_usb_disable(struct tmc_usb_data *usb_data);

#endif
