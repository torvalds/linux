/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_WWAN_H
#define IOSM_IPC_WWAN_H

/**
 * ipc_wwan_init - Allocate, Init and register WWAN device
 * @ipc_imem:		Pointer to imem data-struct
 * @dev:		Pointer to device structure
 *
 * Returns: Pointer to instance on success else NULL
 */
struct iosm_wwan *ipc_wwan_init(struct iosm_imem *ipc_imem, struct device *dev);

/**
 * ipc_wwan_deinit - Unregister and free WWAN device, clear pointer
 * @ipc_wwan:	Pointer to wwan instance data
 */
void ipc_wwan_deinit(struct iosm_wwan *ipc_wwan);

/**
 * ipc_wwan_receive - Receive a downlink packet from CP.
 * @ipc_wwan:	Pointer to wwan instance
 * @skb_arg:	Pointer to struct sk_buff
 * @dss:	Set to true if interafce id is from 257 to 261,
 *		else false
 * @if_id:	Interface ID
 *
 * Return: 0 on success and failure value on error
 */
int ipc_wwan_receive(struct iosm_wwan *ipc_wwan, struct sk_buff *skb_arg,
		     bool dss, int if_id);

/**
 * ipc_wwan_tx_flowctrl - Enable/Disable TX flow control
 * @ipc_wwan:	Pointer to wwan instance
 * @id:		Ipc mux channel session id
 * @on:		if true then flow ctrl would be enabled else disable
 *
 */
void ipc_wwan_tx_flowctrl(struct iosm_wwan *ipc_wwan, int id, bool on);

/**
 * ipc_wwan_is_tx_stopped - Checks if Tx stopped for a Interface id.
 * @ipc_wwan:	Pointer to wwan instance
 * @id:		Ipc mux channel session id
 *
 * Return: true if stopped, false otherwise
 */
bool ipc_wwan_is_tx_stopped(struct iosm_wwan *ipc_wwan, int id);

#endif
