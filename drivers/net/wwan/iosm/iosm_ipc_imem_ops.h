/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_IMEM_OPS_H
#define IOSM_IPC_IMEM_OPS_H

#include "iosm_ipc_mux_codec.h"

/* Maximum wait time for blocking read */
#define IPC_READ_TIMEOUT 500

/* The delay in ms for defering the unregister */
#define SIO_UNREGISTER_DEFER_DELAY_MS 1

/* Default delay till CP PSI image is running and modem updates the
 * execution stage.
 * unit : milliseconds
 */
#define PSI_START_DEFAULT_TIMEOUT 3000

/* Default time out when closing SIO, till the modem is in
 * running state.
 * unit : milliseconds
 */
#define BOOT_CHECK_DEFAULT_TIMEOUT 400

/* IP MUX channel range */
#define IP_MUX_SESSION_START 0
#define IP_MUX_SESSION_END 7

/* Default IP MUX channel */
#define IP_MUX_SESSION_DEFAULT	0

/**
 * ipc_imem_sys_port_open - Open a port link to CP.
 * @ipc_imem:	Imem instance.
 * @chl_id:	Channel Indentifier.
 * @hp_id:	HP Indentifier.
 *
 * Return: channel instance on success, NULL for failure
 */
struct ipc_mem_channel *ipc_imem_sys_port_open(struct iosm_imem *ipc_imem,
					       int chl_id, int hp_id);

/**
 * ipc_imem_sys_cdev_close - Release a sio link to CP.
 * @ipc_cdev:		iosm sio instance.
 */
void ipc_imem_sys_cdev_close(struct iosm_cdev *ipc_cdev);

/**
 * ipc_imem_sys_cdev_write - Route the uplink buffer to CP.
 * @ipc_cdev:		iosm_cdev instance.
 * @skb:		Pointer to skb.
 *
 * Return: 0 on success and failure value on error
 */
int ipc_imem_sys_cdev_write(struct iosm_cdev *ipc_cdev, struct sk_buff *skb);

/**
 * ipc_imem_sys_wwan_open - Open packet data online channel between network
 *			layer and CP.
 * @ipc_imem:		Imem instance.
 * @if_id:		ip link tag of the net device.
 *
 * Return: Channel ID on success and failure value on error
 */
int ipc_imem_sys_wwan_open(struct iosm_imem *ipc_imem, int if_id);

/**
 * ipc_imem_sys_wwan_close - Close packet data online channel between network
 *			 layer and CP.
 * @ipc_imem:		Imem instance.
 * @if_id:		IP link id net device.
 * @channel_id:		Channel ID to be closed.
 */
void ipc_imem_sys_wwan_close(struct iosm_imem *ipc_imem, int if_id,
			     int channel_id);

/**
 * ipc_imem_sys_wwan_transmit - Function for transfer UL data
 * @ipc_imem:		Imem instance.
 * @if_id:		link ID of the device.
 * @channel_id:		Channel ID used
 * @skb:		Pointer to sk buffer
 *
 * Return: 0 on success and failure value on error
 */
int ipc_imem_sys_wwan_transmit(struct iosm_imem *ipc_imem, int if_id,
			       int channel_id, struct sk_buff *skb);
/**
 * ipc_imem_wwan_channel_init - Initializes WWAN channels and the channel for
 *				MUX.
 * @ipc_imem:		Pointer to iosm_imem struct.
 * @mux_type:		Type of mux protocol.
 */
void ipc_imem_wwan_channel_init(struct iosm_imem *ipc_imem,
				enum ipc_mux_protocol mux_type);
#endif
