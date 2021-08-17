// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/wwan.h>

#include "iosm_ipc_chnl_cfg.h"

/* Max. sizes of a downlink buffers */
#define IPC_MEM_MAX_DL_FLASH_BUF_SIZE (16 * 1024)
#define IPC_MEM_MAX_DL_LOOPBACK_SIZE (1 * 1024 * 1024)
#define IPC_MEM_MAX_DL_AT_BUF_SIZE 2048
#define IPC_MEM_MAX_DL_RPC_BUF_SIZE (32 * 1024)
#define IPC_MEM_MAX_DL_MBIM_BUF_SIZE IPC_MEM_MAX_DL_RPC_BUF_SIZE

/* Max. transfer descriptors for a pipe. */
#define IPC_MEM_MAX_TDS_FLASH_DL 3
#define IPC_MEM_MAX_TDS_FLASH_UL 6
#define IPC_MEM_MAX_TDS_AT 4
#define IPC_MEM_MAX_TDS_RPC 4
#define IPC_MEM_MAX_TDS_MBIM IPC_MEM_MAX_TDS_RPC
#define IPC_MEM_MAX_TDS_LOOPBACK 11

/* Accumulation backoff usec */
#define IRQ_ACC_BACKOFF_OFF 0

/* MUX acc backoff 1ms */
#define IRQ_ACC_BACKOFF_MUX 1000

/* Modem channel configuration table
 * Always reserve element zero for flash channel.
 */
static struct ipc_chnl_cfg modem_cfg[] = {
	/* IP Mux */
	{ IPC_MEM_IP_CHL_ID_0, IPC_MEM_PIPE_0, IPC_MEM_PIPE_1,
	  IPC_MEM_MAX_TDS_MUX_LITE_UL, IPC_MEM_MAX_TDS_MUX_LITE_DL,
	  IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE, WWAN_PORT_UNKNOWN },
	/* RPC - 0 */
	{ IPC_MEM_CTRL_CHL_ID_1, IPC_MEM_PIPE_2, IPC_MEM_PIPE_3,
	  IPC_MEM_MAX_TDS_RPC, IPC_MEM_MAX_TDS_RPC,
	  IPC_MEM_MAX_DL_RPC_BUF_SIZE, WWAN_PORT_UNKNOWN },
	/* IAT0 */
	{ IPC_MEM_CTRL_CHL_ID_2, IPC_MEM_PIPE_4, IPC_MEM_PIPE_5,
	  IPC_MEM_MAX_TDS_AT, IPC_MEM_MAX_TDS_AT, IPC_MEM_MAX_DL_AT_BUF_SIZE,
	  WWAN_PORT_AT },
	/* Trace */
	{ IPC_MEM_CTRL_CHL_ID_3, IPC_MEM_PIPE_6, IPC_MEM_PIPE_7,
	  IPC_MEM_TDS_TRC, IPC_MEM_TDS_TRC, IPC_MEM_MAX_DL_TRC_BUF_SIZE,
	  WWAN_PORT_UNKNOWN },
	/* IAT1 */
	{ IPC_MEM_CTRL_CHL_ID_4, IPC_MEM_PIPE_8, IPC_MEM_PIPE_9,
	  IPC_MEM_MAX_TDS_AT, IPC_MEM_MAX_TDS_AT, IPC_MEM_MAX_DL_AT_BUF_SIZE,
	  WWAN_PORT_AT },
	/* Loopback */
	{ IPC_MEM_CTRL_CHL_ID_5, IPC_MEM_PIPE_10, IPC_MEM_PIPE_11,
	  IPC_MEM_MAX_TDS_LOOPBACK, IPC_MEM_MAX_TDS_LOOPBACK,
	  IPC_MEM_MAX_DL_LOOPBACK_SIZE, WWAN_PORT_UNKNOWN },
	/* MBIM Channel */
	{ IPC_MEM_CTRL_CHL_ID_6, IPC_MEM_PIPE_12, IPC_MEM_PIPE_13,
	  IPC_MEM_MAX_TDS_MBIM, IPC_MEM_MAX_TDS_MBIM,
	  IPC_MEM_MAX_DL_MBIM_BUF_SIZE, WWAN_PORT_MBIM },
};

int ipc_chnl_cfg_get(struct ipc_chnl_cfg *chnl_cfg, int index)
{
	int array_size = ARRAY_SIZE(modem_cfg);

	if (index >= array_size) {
		pr_err("index: %d and array_size %d", index, array_size);
		return -ECHRNG;
	}

	if (index == IPC_MEM_MUX_IP_CH_IF_ID)
		chnl_cfg->accumulation_backoff = IRQ_ACC_BACKOFF_MUX;
	else
		chnl_cfg->accumulation_backoff = IRQ_ACC_BACKOFF_OFF;

	chnl_cfg->ul_nr_of_entries = modem_cfg[index].ul_nr_of_entries;
	chnl_cfg->dl_nr_of_entries = modem_cfg[index].dl_nr_of_entries;
	chnl_cfg->dl_buf_size = modem_cfg[index].dl_buf_size;
	chnl_cfg->id = modem_cfg[index].id;
	chnl_cfg->ul_pipe = modem_cfg[index].ul_pipe;
	chnl_cfg->dl_pipe = modem_cfg[index].dl_pipe;
	chnl_cfg->wwan_port_type = modem_cfg[index].wwan_port_type;

	return 0;
}
