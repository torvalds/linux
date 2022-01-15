/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation
 */

#ifndef IOSM_IPC_CHNL_CFG_H
#define IOSM_IPC_CHNL_CFG_H

#include "iosm_ipc_mux.h"

/* Number of TDs on the trace channel */
#define IPC_MEM_TDS_TRC 32

/* Trace channel TD buffer size. */
#define IPC_MEM_MAX_DL_TRC_BUF_SIZE 8192

/* Channel ID */
enum ipc_channel_id {
	IPC_MEM_IP_CHL_ID_0 = 0,
	IPC_MEM_CTRL_CHL_ID_1,
	IPC_MEM_CTRL_CHL_ID_2,
	IPC_MEM_CTRL_CHL_ID_3,
	IPC_MEM_CTRL_CHL_ID_4,
	IPC_MEM_CTRL_CHL_ID_5,
	IPC_MEM_CTRL_CHL_ID_6,
};

/**
 * struct ipc_chnl_cfg - IPC channel configuration structure
 * @id:				Interface ID
 * @ul_pipe:			Uplink datastream
 * @dl_pipe:			Downlink datastream
 * @ul_nr_of_entries:		Number of Transfer descriptor uplink pipe
 * @dl_nr_of_entries:		Number of Transfer descriptor downlink pipe
 * @dl_buf_size:		Downlink buffer size
 * @wwan_port_type:		Wwan subsystem port type
 * @accumulation_backoff:	Time in usec for data accumalation
 */
struct ipc_chnl_cfg {
	u32 id;
	u32 ul_pipe;
	u32 dl_pipe;
	u32 ul_nr_of_entries;
	u32 dl_nr_of_entries;
	u32 dl_buf_size;
	u32 wwan_port_type;
	u32 accumulation_backoff;
};

/**
 * ipc_chnl_cfg_get - Get pipe configuration.
 * @chnl_cfg:		Array of ipc_chnl_cfg struct
 * @index:		Channel index (upto MAX_CHANNELS)
 *
 * Return: 0 on success and failure value on error
 */
int ipc_chnl_cfg_get(struct ipc_chnl_cfg *chnl_cfg, int index);

#endif
