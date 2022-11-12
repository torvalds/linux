// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2013, 2015, 2017-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/**
 * Connection mapping table management for SPS device driver.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/memory.h>

#include "spsi.h"

/* Module state */
struct sps_map_state {
	const struct sps_map *maps;
	u32 num_maps;
	u32 options;
};

static struct sps_map_state sps_maps;

/**
 * Initialize connection mapping module
 *
 */
int sps_map_init(const struct sps_map *map_props, u32 options)
{
	const struct sps_map *maps;

	/* Are there any connection mappings? */
	memset(&sps_maps, 0, sizeof(sps_maps));
	if (map_props == NULL)
		return 0;

	/* Init the module state */
	sps_maps.maps = map_props;
	sps_maps.options = options;
	for (maps = sps_maps.maps;; maps++, sps_maps.num_maps++)
		if (maps->src.periph_class == SPS_CLASS_INVALID &&
		    maps->src.periph_phy_addr == SPS_ADDR_INVALID)
			break;

	SPS_DBG(sps, "sps: %d mappings\n", sps_maps.num_maps);

	return 0;
}

/**
 * De-initialize connection mapping module
 *
 */
void sps_map_de_init(void)
{
	memset(&sps_maps, 0, sizeof(sps_maps));
}

/**
 * Find matching connection mapping
 *
 */
int sps_map_find(struct sps_connect *connect)
{
	const struct sps_map *map;
	u32 i;
	void *desc;
	void *data;

	/* Are there any connection mappings? */
	if (sps_maps.num_maps == 0)
		return SPS_ERROR;

	/* Search the mapping table for a match to the specified connection */
	for (i = sps_maps.num_maps, map = sps_maps.maps;
	    i > 0; i--, map++)
		if (map->src.periph_class == (u32) connect->source &&
		    map->dest.periph_class == (u32) connect->destination
		    && map->config == (u32) connect->config)
			break;

	if (i == 0)
		return SPS_ERROR;

	/*
	 * Before modifying client parameter struct, perform all
	 * operations that might fail
	 */
	desc = spsi_get_mem_ptr(map->desc_base);
	if (desc == NULL) {
		SPS_ERR(sps,
			"sps:Cannot get virt addr for I/O buffer: %pa\n",
			&map->desc_base);
		return SPS_ERROR;
	}

	if (map->data_size > 0 && map->data_base != SPS_ADDR_INVALID) {
		data = spsi_get_mem_ptr(map->data_base);
		if (data == NULL) {
			SPS_ERR(sps,
				"sps:Can't get virt addr for I/O buffer: %pa\n",
				&map->data_base);
			return SPS_ERROR;
		}
	} else {
		data = NULL;
	}

	/* Copy mapping values to client parameter struct */
	if (connect->source != SPS_DEV_HANDLE_MEM)
		connect->src_pipe_index = map->src.pipe_index;

	if (connect->destination != SPS_DEV_HANDLE_MEM)
		connect->dest_pipe_index = map->dest.pipe_index;

	if (connect->mode == SPS_MODE_SRC)
		connect->event_thresh = map->src.event_thresh;
	else
		connect->event_thresh = map->dest.event_thresh;

	connect->desc.size = map->desc_size;
	connect->desc.phys_base = map->desc_base;
	connect->desc.base = desc;
	if (map->data_size > 0 && map->data_base != SPS_ADDR_INVALID) {
		connect->data.size = map->data_size;
		connect->data.phys_base = map->data_base;
		connect->data.base = data;
	}

	return 0;
}
