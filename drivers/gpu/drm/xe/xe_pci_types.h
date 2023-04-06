/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PCI_TYPES_H_
#define _XE_PCI_TYPES_H_

#include <linux/types.h>

struct xe_graphics_desc {
	u8 ver;
	u8 rel;
};

struct xe_media_desc {
	u8 ver;
	u8 rel;
};

#endif
