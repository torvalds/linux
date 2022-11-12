/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011, 2013, 2016-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* SPS driver mapping table data declarations. */


#ifndef _SPS_MAP_H_
#define _SPS_MAP_H_

#include <linux/types.h>

/* End point parameters */
struct sps_map_end_point {
	u32 periph_class;	/* Peripheral device enumeration class */
	phys_addr_t periph_phy_addr;	/* Peripheral base address */
	u32 pipe_index;		/* Pipe index */
	u32 event_thresh;	/* Pipe event threshold */
};

/* Mapping connection descriptor */
struct sps_map {
	/* Source end point parameters */
	struct sps_map_end_point src;

	/* Destination end point parameters */
	struct sps_map_end_point dest;

	/* Resource parameters */
	u32 config;	 /* Configuration (stream) identifier */
	phys_addr_t desc_base;	 /* Physical address of descriptor FIFO */
	u32 desc_size;	 /* Size (bytes) of descriptor FIFO */
	phys_addr_t data_base;	 /* Physical address of data FIFO */
	u32 data_size;	 /* Size (bytes) of data FIFO */

};

#endif /* _SPS_MAP_H_ */
