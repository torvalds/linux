/* SPDX-License-Identifier: GPL-2.0 */
/*
 * zfcp device driver
 *
 * Definitions for handling diagnostics in the the zfcp device driver.
 *
 * Copyright IBM Corp. 2018
 */

#ifndef ZFCP_DIAG_H
#define ZFCP_DIAG_H

#include <linux/spinlock.h>

#include "zfcp_fsf.h"
#include "zfcp_def.h"

/**
 * struct zfcp_diag_header - general part of a diagnostic buffer.
 * @access_lock: lock protecting all the data in this buffer.
 * @updating: flag showing that an update for this buffer is currently running.
 * @incomplete: flag showing that the data in @buffer is incomplete.
 * @timestamp: time in jiffies when the data of this buffer was last captured.
 * @buffer: implementation-depending data of this buffer
 * @buffer_size: size of @buffer
 */
struct zfcp_diag_header {
	spinlock_t	access_lock;

	/* Flags */
	u64		updating	:1;
	u64		incomplete	:1;

	unsigned long	timestamp;

	void		*buffer;
	size_t		buffer_size;
};

/**
 * struct zfcp_diag_adapter - central storage for all diagnostics concerning an
 *			      adapter.
 * @port_data: data retrieved using exchange port data.
 * @port_data.header: header with metadata for the cache in @port_data.data.
 * @port_data.data: cached QTCB Bottom of command exchange port data.
 * @config_data: data retrieved using exchange config data.
 * @config_data.header: header with metadata for the cache in @config_data.data.
 * @config_data.data: cached QTCB Bottom of command exchange config data.
 */
struct zfcp_diag_adapter {
	struct {
		struct zfcp_diag_header		header;
		struct fsf_qtcb_bottom_port	data;
	} port_data;
	struct {
		struct zfcp_diag_header		header;
		struct fsf_qtcb_bottom_config	data;
	} config_data;
};

int zfcp_diag_adapter_setup(struct zfcp_adapter *const adapter);
void zfcp_diag_adapter_free(struct zfcp_adapter *const adapter);

void zfcp_diag_update_xdata(struct zfcp_diag_header *const hdr,
			    const void *const data, const bool incomplete);

#endif /* ZFCP_DIAG_H */
