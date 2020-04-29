/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for Intel extcon hardware
 *
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 */

#ifndef __EXTCON_INTEL_H__
#define __EXTCON_INTEL_H__

enum extcon_intel_usb_id {
	INTEL_USB_ID_OTG,
	INTEL_USB_ID_GND,
	INTEL_USB_ID_FLOAT,
	INTEL_USB_RID_A,
	INTEL_USB_RID_B,
	INTEL_USB_RID_C,
};

#endif	/* __EXTCON_INTEL_H__ */
