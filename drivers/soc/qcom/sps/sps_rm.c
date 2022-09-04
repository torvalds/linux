// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2015, 2017-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* Resource management for the SPS device driver. */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/memory.h>
#include <linux/interrupt.h>

#include "spsi.h"
#include "sps_core.h"

/* Max BAM FIFO sizes */
#define SPSRM_MAX_DESC_FIFO_SIZE    0xffff
#define SPSRM_MAX_DATA_FIFO_SIZE    0xffff

/* Connection control struct pointer */
static struct sps_rm *sps_rm;

/**
 * Initialize resource manager module
 */
int sps_rm_init(struct sps_rm *rm, u32 options)
{
	/* Set the resource manager state struct pointer */
	sps_rm = rm;

	/* Initialize the state struct */
	INIT_LIST_HEAD(&sps_rm->connections_q);
	mutex_init(&sps_rm->lock);

	return 0;
}

/**
 * Initialize client state context
 *
 */
void sps_rm_config_init(struct sps_connect *connect)
{
	memset(connect, SPSRM_CLEAR, sizeof(*connect));
}

/**
 * Remove reference to connection mapping
 *
 * This function removes a reference from a connection mapping struct.
 *
 * @map - pointer to connection mapping struct
 *
 */
static void sps_rm_remove_ref(struct sps_connection *map)
{
	/* Free this connection */
	map->refs--;
	if (map->refs <= 0) {
		if (map->client_src != NULL || map->client_dest != NULL)
			SPS_ERR(sps,
				"sps:%s:Failed to allocate connection struct\n",
				__func__);

		list_del(&map->list);
		kfree(map);
	}
}

/**
 * Compare map to connect parameters
 *
 * This function compares client connect parameters to an allocated
 * connection mapping.
 *
 * @pipe - client context for SPS connection end point
 *
 * @return - true if match, false otherwise
 *
 */
static int sps_rm_map_match(const struct sps_connect *cfg,
			    const struct sps_connection *map)
{
	if (cfg->source != map->src.dev ||
	    cfg->destination != map->dest.dev)
		return false;

	if (cfg->src_pipe_index != SPSRM_CLEAR &&
	    cfg->src_pipe_index != map->src.pipe_index)
		return false;

	if (cfg->dest_pipe_index != SPSRM_CLEAR &&
	    cfg->dest_pipe_index != map->dest.pipe_index)
		return false;

	if (cfg->config != map->config)
		return false;

	if (cfg->desc.size != SPSRM_CLEAR) {
		if (cfg->desc.size != map->desc.size)
			return false;

		if (cfg->desc.phys_base != (SPSRM_CLEAR|SPSRM_ADDR_CLR) &&
		    cfg->desc.base != (void *)(SPSRM_CLEAR|SPSRM_ADDR_CLR) &&
		    (cfg->desc.phys_base != map->desc.phys_base ||
		     cfg->desc.base != map->desc.base)) {
			return false;
		}
	}

	if (cfg->data.size != SPSRM_CLEAR) {
		if (cfg->data.size != map->data.size)
			return false;

		if (cfg->data.phys_base != (SPSRM_CLEAR|SPSRM_ADDR_CLR) &&
		    cfg->data.base != (void *)(SPSRM_CLEAR|SPSRM_ADDR_CLR) &&
		    (cfg->data.phys_base != map->data.phys_base ||
		     cfg->data.base != map->data.base))
			return false;
	}

	return true;
}

/**
 * Find unconnected mapping
 *
 * This function finds an allocated a connection mapping.
 *
 * @pipe - client context for SPS connection end point
 *
 * @return - pointer to allocated connection mapping, or NULL if not found
 *
 */
static struct sps_connection *find_unconnected(struct sps_pipe *pipe)
{
	struct sps_connect *cfg = &pipe->connect;
	struct sps_connection *map;

	/* Has this connection already been allocated? */
	list_for_each_entry(map, &sps_rm->connections_q, list) {
		if (sps_rm_map_match(cfg, map))
			if ((cfg->mode == SPS_MODE_SRC
			     && map->client_src == NULL)
			    || (cfg->mode != SPS_MODE_SRC
				&& map->client_dest == NULL))
				return map;	/* Found */
	}

	return NULL;		/* Not Found */
}

/**
 * Assign connection to client
 *
 * This function assigns a connection to a client.
 *
 * @pipe - client context for SPS connection end point
 *
 * @map - connection mapping
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_rm_assign(struct sps_pipe *pipe,
			 struct sps_connection *map)
{
	struct sps_connect *cfg = &pipe->connect;
	unsigned long desc_iova = 0;
	unsigned long data_iova = 0;

	/* Check ownership and BAM */
	if ((cfg->mode == SPS_MODE_SRC && map->client_src != NULL) ||
	    (cfg->mode != SPS_MODE_SRC && map->client_dest != NULL)) {
		SPS_ERR(sps,
			"sps:%s:The end point is already connected\n",
			__func__);
		return SPS_ERROR;
	}

	/* Check whether this end point is a BAM (not memory) */
	if ((cfg->mode == SPS_MODE_SRC && map->src.bam == NULL) ||
	    (cfg->mode != SPS_MODE_SRC && map->dest.bam == NULL)) {
		SPS_ERR(sps, "sps:%s:The end point is empty\n", __func__);
		return SPS_ERROR;
	}

	/* Record the connection assignment */
	if (cfg->mode == SPS_MODE_SRC) {
		map->client_src = pipe;
		pipe->bam = map->src.bam;
		pipe->pipe_index = map->src.pipe_index;
		if (pipe->connect.event_thresh != SPSRM_CLEAR)
			map->src.event_threshold = pipe->connect.event_thresh;
		if (pipe->connect.lock_group != SPSRM_CLEAR)
			map->src.lock_group = pipe->connect.lock_group;
	} else {
		map->client_dest = pipe;
		pipe->bam = map->dest.bam;
		pipe->pipe_index = map->dest.pipe_index;
		if (pipe->connect.event_thresh != SPSRM_CLEAR)
			map->dest.event_threshold =
			pipe->connect.event_thresh;
		if (pipe->connect.lock_group != SPSRM_CLEAR)
			map->dest.lock_group = pipe->connect.lock_group;
	}
	pipe->map = map;

	SPS_DBG(pipe->bam, "sps:%s.bam %pa.pipe_index=%d\n",
			__func__, BAM_ID(pipe->bam), pipe->pipe_index);

	/* Copy parameters to client connect state */
	pipe->connect.src_pipe_index = map->src.pipe_index;
	pipe->connect.dest_pipe_index = map->dest.pipe_index;

	/*
	 * The below assignment to connect.desc and connect.data will
	 * overwrite the previous values given by the first client
	 * in a BAM-to-BAM connection. Prevent that since the IOVAs
	 * may be different for the same physical buffers if the
	 * BAMs use different SMMUs.
	 */
	if (pipe->bam->props.options & SPS_BAM_SMMU_EN) {
		desc_iova = pipe->connect.desc.iova;
		data_iova = pipe->connect.data.iova;
	}
	pipe->connect.desc = map->desc;
	pipe->connect.data = map->data;
	if (pipe->bam->props.options & SPS_BAM_SMMU_EN) {
		pipe->connect.desc.iova = desc_iova;
		pipe->connect.data.iova = data_iova;
	}

	pipe->client_state = SPS_STATE_ALLOCATE;

	return 0;
}

/**
 * Free connection mapping resources
 *
 * This function frees a connection mapping resources.
 *
 * @pipe - client context for SPS connection end point
 *
 */
static void sps_rm_free_map_rsrc(struct sps_connection *map)
{
	struct sps_bam *bam;

	if (map->client_src != NULL || map->client_dest != NULL)
		return;

	if (map->alloc_src_pipe != SPS_BAM_PIPE_INVALID) {
		bam = map->src.bam;
		sps_bam_pipe_free(bam, map->src.pipe_index);

		/* Is this a BAM-DMA pipe? */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		if ((bam->props.options & SPS_BAM_OPT_BAMDMA))
			/* Deallocate and free the BAM-DMA channel */
			sps_dma_pipe_free(bam, map->src.pipe_index);
#endif
		map->alloc_src_pipe = SPS_BAM_PIPE_INVALID;
		map->src.pipe_index = SPS_BAM_PIPE_INVALID;
	}
	if (map->alloc_dest_pipe != SPS_BAM_PIPE_INVALID) {
		bam = map->dest.bam;
		sps_bam_pipe_free(bam, map->dest.pipe_index);

		/* Is this a BAM-DMA pipe? */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		if ((bam->props.options & SPS_BAM_OPT_BAMDMA)) {
			/* Deallocate the BAM-DMA channel */
			sps_dma_pipe_free(bam, map->dest.pipe_index);
		}
#endif
		map->alloc_dest_pipe = SPS_BAM_PIPE_INVALID;
		map->dest.pipe_index = SPS_BAM_PIPE_INVALID;
	}
	if (map->alloc_desc_base != SPS_ADDR_INVALID) {
		sps_mem_free_io(map->alloc_desc_base, map->desc.size);

		map->alloc_desc_base = SPS_ADDR_INVALID;
		map->desc.phys_base = SPS_ADDR_INVALID;
	}
	if (map->alloc_data_base != SPS_ADDR_INVALID) {
		sps_mem_free_io(map->alloc_data_base, map->data.size);

		map->alloc_data_base = SPS_ADDR_INVALID;
		map->data.phys_base = SPS_ADDR_INVALID;
	}
}

/**
 * Init connection mapping from client connect
 *
 * This function initializes a connection mapping from the client's
 * connect parameters.
 *
 * @map - connection mapping struct
 *
 * @cfg - client connect parameters
 *
 * @return - pointer to allocated connection mapping, or NULL on error
 *
 */
static void sps_rm_init_map(struct sps_connection *map,
			    const struct sps_connect *cfg)
{
	/* Clear the connection mapping struct */
	memset(map, 0, sizeof(*map));
	map->desc.phys_base = SPS_ADDR_INVALID;
	map->data.phys_base = SPS_ADDR_INVALID;
	map->alloc_desc_base = SPS_ADDR_INVALID;
	map->alloc_data_base = SPS_ADDR_INVALID;
	map->alloc_src_pipe = SPS_BAM_PIPE_INVALID;
	map->alloc_dest_pipe = SPS_BAM_PIPE_INVALID;

	/* Copy client required parameters */
	map->src.dev = cfg->source;
	map->dest.dev = cfg->destination;
	map->desc.size = cfg->desc.size;
	map->data.size = cfg->data.size;
	map->config = cfg->config;

	/* Did client specify descriptor FIFO? */
	if (map->desc.size != SPSRM_CLEAR &&
	    cfg->desc.phys_base != (SPSRM_CLEAR|SPSRM_ADDR_CLR) &&
	    cfg->desc.base != (void *)(SPSRM_CLEAR|SPSRM_ADDR_CLR))
		map->desc = cfg->desc;

	/* Did client specify data FIFO? */
	if (map->data.size != SPSRM_CLEAR &&
	    cfg->data.phys_base != (SPSRM_CLEAR|SPSRM_ADDR_CLR) &&
	    cfg->data.base != (void *)(SPSRM_CLEAR|SPSRM_ADDR_CLR))
		map->data = cfg->data;

	/* Did client specify source pipe? */
	if (cfg->src_pipe_index != SPSRM_CLEAR)
		map->src.pipe_index = cfg->src_pipe_index;
	else
		map->src.pipe_index = SPS_BAM_PIPE_INVALID;


	/* Did client specify destination pipe? */
	if (cfg->dest_pipe_index != SPSRM_CLEAR)
		map->dest.pipe_index = cfg->dest_pipe_index;
	else
		map->dest.pipe_index = SPS_BAM_PIPE_INVALID;
}

/**
 * Create a new connection mapping
 *
 * This function creates a new connection mapping.
 *
 * @pipe - client context for SPS connection end point
 *
 * @return - pointer to allocated connection mapping, or NULL on error
 *
 */
static struct sps_connection *sps_rm_create(struct sps_pipe *pipe)
{
	struct sps_connection *map;
	struct sps_bam *bam;
	u32 desc_size;
	u32 data_size;
	enum sps_mode dir;
	int success = false;

	/* Allocate new connection */
	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL) {
		SPS_ERR(sps,
			"sps:%s:Failed to allocate connection struct\n",
			__func__);
		return NULL;
	}

	/* Initialize connection struct */
	sps_rm_init_map(map, &pipe->connect);
	dir = pipe->connect.mode;

	/* Use a do/while() loop to avoid a "goto" */
	success = false;
	/* Get BAMs */
	map->src.bam = sps_h2bam(map->src.dev);
	if (map->src.bam == NULL) {
		if (map->src.dev != SPS_DEV_HANDLE_MEM) {
			SPS_ERR(sps, "sps:Invalid BAM handle: %pK\n",
					(void *)(&map->src.dev));
			goto exit_err;
		}
		map->src.pipe_index = SPS_BAM_PIPE_INVALID;
	}

	if (!(pipe->connect.options & SPS_O_DUMMY_PEER)) {
		map->dest.bam = sps_h2bam(map->dest.dev);
		if (map->dest.bam == NULL) {
			if (map->dest.dev != SPS_DEV_HANDLE_MEM) {
				SPS_ERR(sps,
				"sps:Invalid BAM handle: %pK",
				(void *)(&map->dest.dev));
				goto exit_err;
			}
			map->dest.pipe_index = SPS_BAM_PIPE_INVALID;
		}
	}

	/* Check the BAM device for the pipe */
	if ((dir == SPS_MODE_SRC && map->src.bam == NULL) ||
	    (dir != SPS_MODE_SRC && map->dest.bam == NULL)) {
		SPS_ERR(sps, "sps:Invalid BAM endpt: dir %d src %pK dest %pK\n",
			dir, (void *)(&map->src.dev), (void *)(&map->dest.dev));
		goto exit_err;
	}

	/* Allocate pipes and copy BAM parameters */
	if (map->src.bam != NULL) {
		/* Allocate the pipe */
		bam = map->src.bam;
		map->alloc_src_pipe = sps_bam_pipe_alloc(bam,
							map->src.pipe_index);
		if (map->alloc_src_pipe == SPS_BAM_PIPE_INVALID)
			goto exit_err;
		map->src.pipe_index = map->alloc_src_pipe;

		/* Is this a BAM-DMA pipe? */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		if ((bam->props.options & SPS_BAM_OPT_BAMDMA)) {
			int rc;
			/* Allocate the BAM-DMA channel */
			rc = sps_dma_pipe_alloc(bam, map->src.pipe_index,
						 SPS_MODE_SRC);
			if (rc) {
				SPS_ERR(bam,
					"sps:Failed to alloc BAM-DMA pipe: %d\n",
					map->src.pipe_index);
				goto exit_err;
			}
		}
#endif
		map->src.bam_phys = bam->props.phys_addr;
		map->src.event_threshold = bam->props.event_threshold;
	}
	if (map->dest.bam != NULL) {
		/* Allocate the pipe */
		bam = map->dest.bam;
		map->alloc_dest_pipe = sps_bam_pipe_alloc(bam,
							 map->dest.pipe_index);
		if (map->alloc_dest_pipe == SPS_BAM_PIPE_INVALID)
			goto exit_err;

		map->dest.pipe_index = map->alloc_dest_pipe;

		/* Is this a BAM-DMA pipe? */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		if ((bam->props.options & SPS_BAM_OPT_BAMDMA)) {
			int rc;
			/* Allocate the BAM-DMA channel */
			rc = sps_dma_pipe_alloc(bam, map->dest.pipe_index,
					       SPS_MODE_DEST);
			if (rc) {
				SPS_ERR(bam,
					"sps:Failed to alloc BAM-DMA pipe: %d\n",
					map->dest.pipe_index);
				goto exit_err;
			}
		}
#endif
		map->dest.bam_phys = bam->props.phys_addr;
		map->dest.event_threshold =
		bam->props.event_threshold;
	}

	/* Get default FIFO sizes */
	desc_size = 0;
	data_size = 0;
	if (map->src.bam != NULL) {
		bam = map->src.bam;
		desc_size = bam->props.desc_size;
		data_size = bam->props.data_size;
	}
	if (map->dest.bam != NULL) {
		bam = map->dest.bam;
		if (bam->props.desc_size > desc_size)
			desc_size = bam->props.desc_size;
		if (bam->props.data_size > data_size)
			data_size = bam->props.data_size;
	}

	/* Set FIFO sizes */
	if (map->desc.size == SPSRM_CLEAR)
		map->desc.size = desc_size;
	if (map->src.bam != NULL && map->dest.bam != NULL) {
		/* BAM-to-BAM requires data FIFO */
		if (map->data.size == SPSRM_CLEAR)
			map->data.size = data_size;
	} else {
		if (!(pipe->connect.options & SPS_O_DUMMY_PEER))
			map->data.size = 0;
	}
	if (map->desc.size > SPSRM_MAX_DESC_FIFO_SIZE) {
		SPS_ERR(sps, "sps:Invalid desc FIFO size: 0x%x\n",
						map->desc.size);
		goto exit_err;
	}
	if (map->src.bam != NULL && map->dest.bam != NULL &&
	    map->data.size > SPSRM_MAX_DATA_FIFO_SIZE) {
		SPS_ERR(sps, "sps:Invalid data FIFO size: 0x%x\n",
						map->data.size);
		goto exit_err;
	}

	/* Allocate descriptor FIFO if necessary */
	if (map->desc.size && map->desc.phys_base == SPS_ADDR_INVALID) {
		map->alloc_desc_base = sps_mem_alloc_io(map->desc.size);
		if (map->alloc_desc_base == SPS_ADDR_INVALID) {
			SPS_ERR(sps, "sps:I/O memory allocation failure:0x%x\n",
				map->desc.size);
			goto exit_err;
		}
		map->desc.phys_base = map->alloc_desc_base;
		map->desc.base = spsi_get_mem_ptr(map->desc.phys_base);
		if (map->desc.base == NULL) {
			SPS_ERR(sps,
				"sps:Cannot get virt addr for I/O buffer:%pa\n",
				&map->desc.phys_base);
			goto exit_err;
		}
	}

	/* Allocate data FIFO if necessary */
	if (map->data.size && map->data.phys_base == SPS_ADDR_INVALID) {
		map->alloc_data_base = sps_mem_alloc_io(map->data.size);
		if (map->alloc_data_base == SPS_ADDR_INVALID) {
			SPS_ERR(sps, "sps:I/O memory allocation failure:0x%x\n",
				map->data.size);
			goto exit_err;
		}
		map->data.phys_base = map->alloc_data_base;
		map->data.base = spsi_get_mem_ptr(map->data.phys_base);
		if (map->data.base == NULL) {
			SPS_ERR(sps,
				"sps:Cannot get virt addr for I/O buffer:%pa\n",
				&map->data.phys_base);
			goto exit_err;
		}
	}

	/* Attempt to assign this connection to the client */
	if (sps_rm_assign(pipe, map)) {
		SPS_ERR(sps,
		"sps:%s:failed to assign a connection to the client\n",
			__func__);
		goto exit_err;
	}

	/* Initialization was successful */
	success = true;
exit_err:

	/* If initialization failed, free resources */
	if (!success) {
		sps_rm_free_map_rsrc(map);
		kfree(map);
		return NULL;
	}

	return map;
}

/**
 * Free connection mapping
 *
 * This function frees a connection mapping.
 *
 * @pipe - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_rm_free(struct sps_pipe *pipe)
{
	struct sps_connection *map = (void *)pipe->map;
	struct sps_connect *cfg = &pipe->connect;

	mutex_lock(&sps_rm->lock);

	/* Free this connection */
	if (cfg->mode == SPS_MODE_SRC)
		map->client_src = NULL;
	else
		map->client_dest = NULL;

	pipe->map = NULL;
	pipe->client_state = SPS_STATE_DISCONNECT;
	sps_rm_free_map_rsrc(map);

	sps_rm_remove_ref(map);

	mutex_unlock(&sps_rm->lock);

	return 0;
}

/**
 * Allocate an SPS connection end point
 *
 * This function allocates resources and initializes a BAM connection.
 *
 * @pipe - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_rm_alloc(struct sps_pipe *pipe)
{
	struct sps_connection *map;
	int result = SPS_ERROR;

	if (pipe->connect.sps_reserved != SPSRM_CLEAR) {
		/*
		 * Client did not call sps_get_config()	to init
		 * struct sps_connect, so only use legacy members.
		 */
		unsigned long source = pipe->connect.source;
		unsigned long destination = pipe->connect.destination;
		enum sps_mode mode = pipe->connect.mode;
		u32 config = pipe->connect.config;

		memset(&pipe->connect, SPSRM_CLEAR,
			      sizeof(pipe->connect));
		pipe->connect.source = source;
		pipe->connect.destination = destination;
		pipe->connect.mode = mode;
		pipe->connect.config = config;
	}
	if (pipe->connect.config == SPSRM_CLEAR)
		pipe->connect.config = SPS_CONFIG_DEFAULT;

	/*
	 *  If configuration is not default, then client is specifying a
	 * connection mapping.  Find a matching mapping, or fail.
	 * If a match is found, the client's Connect struct will be updated
	 * with all the mapping's values.
	 */
	if (pipe->connect.config != SPS_CONFIG_DEFAULT) {
		if (sps_map_find(&pipe->connect)) {
			SPS_ERR(sps,
				"sps:%s:Failed to find connection mapping\n",
								__func__);
			return SPS_ERROR;
		}
	}

	mutex_lock(&sps_rm->lock);
	/* Check client state */
	if (IS_SPS_STATE_OK(pipe)) {
		SPS_ERR(sps,
			"sps:%s:Client connection already allocated\n",
							__func__);
		goto exit_err;
	}

	/* Are the connection resources already allocated? */
	map = find_unconnected(pipe);
	if (map != NULL) {
		/* Attempt to assign this connection to the client */
		if (sps_rm_assign(pipe, map))
			/* Assignment failed, so must allocate new */
			map = NULL;
	}

	/* Allocate a new connection if necessary */
	if (map == NULL) {
		map = sps_rm_create(pipe);
		if (map == NULL) {
			SPS_ERR(sps,
				"sps:%s:Failed to allocate connection\n",
							__func__);
			goto exit_err;
		}
		list_add_tail(&map->list, &sps_rm->connections_q);
	}

	/* Add the connection to the allocated queue */
	map->refs++;

	/* Initialization was successful */
	result = 0;
exit_err:
	mutex_unlock(&sps_rm->lock);

	if (result)
		return SPS_ERROR;

	return 0;
}

/**
 * Disconnect an SPS connection end point
 *
 * This function frees resources and de-initializes a BAM connection.
 *
 * @pipe - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_rm_disconnect(struct sps_pipe *pipe)
{
	sps_rm_free(pipe);
	return 0;
}

/**
 * Process connection state change
 *
 * This function processes a connection state change.
 *
 * @pipe - pointer to client context
 *
 * @state - new state for connection
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_rm_state_change(struct sps_pipe *pipe, u32 state)
{
	int auto_enable = false;
	int result;

	/* Allocate the pipe */
	if (pipe->client_state == SPS_STATE_DISCONNECT &&
	    state == SPS_STATE_ALLOCATE) {
		if (sps_rm_alloc(pipe)) {
			SPS_ERR(pipe->bam,
				"sps:Fail to allocate resource for BAM 0x%pK pipe %d\n",
					pipe->bam, pipe->pipe_index);
			return SPS_ERROR;
		}
	}

	/* Configure the pipe */
	if (pipe->client_state == SPS_STATE_ALLOCATE &&
	    state == SPS_STATE_CONNECT) {
		/* Connect the BAM pipe */
		struct sps_bam_connect_param params;

		memset(&params, 0, sizeof(params));
		params.mode = pipe->connect.mode;
		if (pipe->connect.options != SPSRM_CLEAR) {
			params.options = pipe->connect.options;
			params.irq_gen_addr = pipe->connect.irq_gen_addr;
			params.irq_gen_data = pipe->connect.irq_gen_data;
		}
		result = sps_bam_pipe_connect(pipe, &params);
		if (result) {
			SPS_ERR(pipe->bam,
				"sps:Failed to connect BAM 0x%pK pipe %d\n",
					pipe->bam, pipe->pipe_index);
			return SPS_ERROR;
		}
		pipe->client_state = SPS_STATE_CONNECT;

		/* Set auto-enable for system-mode connections */
		if (pipe->connect.source == SPS_DEV_HANDLE_MEM ||
		    pipe->connect.destination == SPS_DEV_HANDLE_MEM) {
			if (pipe->map->desc.size != 0 &&
			    pipe->map->desc.phys_base != SPS_ADDR_INVALID)
				auto_enable = true;
		}
	}

	/* Enable the pipe data flow */
	if (pipe->client_state == SPS_STATE_CONNECT &&
	    !(state == SPS_STATE_DISABLE
	      || state == SPS_STATE_DISCONNECT)
	    && (state == SPS_STATE_ENABLE || auto_enable
		|| (pipe->connect.options & SPS_O_AUTO_ENABLE))) {
		result = sps_bam_pipe_enable(pipe->bam, pipe->pipe_index);
		if (result) {
			SPS_ERR(pipe->bam,
				"sps:Failed to set BAM %pa pipe %d flow on\n",
				&pipe->bam->props.phys_addr,
				pipe->pipe_index);
			return SPS_ERROR;
		}

		/* Is this a BAM-DMA pipe? */
#ifdef CONFIG_SPS_SUPPORT_BAMDMA
		if ((pipe->bam->props.options & SPS_BAM_OPT_BAMDMA)) {
			/* Activate the BAM-DMA channel */
			result = sps_dma_pipe_enable(pipe->bam,
						     pipe->pipe_index);
			if (result) {
				SPS_ERR(pipe->bam,
					"sps:Failed to activate BAM-DMA pipe: %d\n",
					pipe->pipe_index);
				return SPS_ERROR;
			}
		}
#endif
		pipe->client_state = SPS_STATE_ENABLE;
	}

	/* Disable the pipe data flow */
	if (pipe->client_state == SPS_STATE_ENABLE &&
	    (state == SPS_STATE_DISABLE	|| state == SPS_STATE_DISCONNECT)) {
		result = sps_bam_pipe_disable(pipe->bam, pipe->pipe_index);
		if (result) {
			SPS_ERR(pipe->bam,
				"sps:Failed to set BAM %pa pipe %d flow off\n",
				&pipe->bam->props.phys_addr,
				pipe->pipe_index);
			return SPS_ERROR;
		}
		pipe->client_state = SPS_STATE_CONNECT;
	}

	/* Disconnect the BAM pipe */
	if (pipe->client_state == SPS_STATE_CONNECT &&
	    state == SPS_STATE_DISCONNECT) {
		struct sps_connection *map;
		struct sps_bam *bam = pipe->bam;
		unsigned long flags;
		u32 pipe_index;

		if (pipe->connect.mode == SPS_MODE_SRC)
			pipe_index = pipe->map->src.pipe_index;
		else
			pipe_index = pipe->map->dest.pipe_index;

		if (bam->props.irq > 0)
			synchronize_irq(bam->props.irq);

		spin_lock_irqsave(&bam->isr_lock, flags);
		pipe->disconnecting = true;
		spin_unlock_irqrestore(&bam->isr_lock, flags);
		result = sps_bam_pipe_disconnect(pipe->bam, pipe_index);
		if (result) {
			SPS_ERR(pipe->bam,
				"sps:Failed to disconnect BAM %pa pipe %d\n",
				&pipe->bam->props.phys_addr,
				pipe->pipe_index);
			return SPS_ERROR;
		}

		/* Clear map state */
		map = (void *)pipe->map;
		if (pipe->connect.mode == SPS_MODE_SRC)
			map->client_src = NULL;
		else if (pipe->connect.mode == SPS_MODE_DEST)
			map->client_dest = NULL;

		sps_rm_disconnect(pipe);

		/* Clear the client state */
		pipe->map = NULL;
		pipe->bam = NULL;
		pipe->client_state = SPS_STATE_DISCONNECT;
	}

	return 0;
}
