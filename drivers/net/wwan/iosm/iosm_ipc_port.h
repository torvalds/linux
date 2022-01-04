/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_PORT_H
#define IOSM_IPC_PORT_H

#include <linux/wwan.h>

#include "iosm_ipc_imem_ops.h"

/**
 * struct iosm_cdev - State of the char driver layer.
 * @iosm_port:		Pointer of type wwan_port
 * @ipc_imem:		imem instance
 * @dev:		Pointer to device struct
 * @pcie:		PCIe component
 * @port_type:		WWAN port type
 * @channel:		Channel instance
 * @chl_id:		Channel Indentifier
 */
struct iosm_cdev {
	struct wwan_port *iosm_port;
	struct iosm_imem *ipc_imem;
	struct device *dev;
	struct iosm_pcie *pcie;
	enum wwan_port_type port_type;
	struct ipc_mem_channel *channel;
	enum ipc_channel_id chl_id;
};

/**
 * ipc_port_init - Allocate IPC port & register to wwan subsystem for AT/MBIM
 *		   communication.
 * @ipc_imem:		Pointer to iosm_imem structure
 * @ipc_port_cfg:	IPC Port Config
 *
 * Returns: 0 on success & NULL on failure
 */
struct iosm_cdev *ipc_port_init(struct iosm_imem *ipc_imem,
				struct ipc_chnl_cfg ipc_port_cfg);

/**
 * ipc_port_deinit - Free IPC port & unregister port with wwan subsystem.
 * @ipc_port:	Array of pointer to the ipc port data-struct
 */
void ipc_port_deinit(struct iosm_cdev *ipc_port[]);

#endif
