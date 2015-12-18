/*
 * Greybus "AP" USB driver for "ES2" controller chips
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __ES2_H
#define __ES2_H

#include <linux/types.h>

struct gb_host_device;

struct es2_ap_csi_config {
	u8 csi_id;
	u8 clock_mode;
	u8 num_lanes;
	u32 bus_freq;
};

int es2_ap_csi_setup(struct gb_host_device *hd, bool start,
		     struct es2_ap_csi_config *cfg);

#endif /* __ES2_H */
