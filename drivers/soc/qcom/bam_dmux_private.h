/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2014, 2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _BAM_DMUX_PRIVATE_H
#define _BAM_DMUX_PRIVATE_H

#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <linux/msm-sps.h>
#include <linux/soc/qcom/smem_state.h>

#define BAM_MUX_HDR_MAGIC_NO			0x33fc
#define BAM_MUX_HDR_CMD_DATA			0
#define BAM_MUX_HDR_CMD_OPEN			1
#define BAM_MUX_HDR_CMD_CLOSE			2
#define BAM_MUX_HDR_CMD_STATUS			3 /* unused */
#define BAM_MUX_HDR_CMD_OPEN_NO_A2_PC		4
#define DEFAULT_BUFFER_SIZE			SZ_2K

#define DYNAMIC_MTU_MASK			0x2
#define MTU_SIZE_MASK				0xc0
#define MTU_SIZE_SHIFT				0x6
#define DL_POOL_SIZE_SHIFT			0x4

/**
 * struct bam_ops_if - collection of function pointers to allow swappable
 * runtime functionality
 * @smsm_change_state_ptr: pointer to smsm_change_state function
 * @smsm_get_state_ptr: pointer to smsm_get_state function
 * @smsm_state_cb_register_ptr: pointer to smsm_state_cb_register function
 * @smsm_state_cb_deregister_ptr: pointer to smsm_state_cb_deregister function
 * @sps_connect_ptr: pointer to sps_connect function
 * @sps_disconnect_ptr: pointer to sps_disconnect function
 * @sps_register_bam_device_ptr: pointer to sps_register_bam_device
 * @sps_deregister_bam_device_ptr: pointer to sps_deregister_bam_device
 * function
 * @sps_alloc_endpoint_ptr: pointer to sps_alloc_endpoint function
 * @sps_free_endpoint_ptr: pointer to sps_free_endpoint function
 * @sps_set_config_ptr: pointer to sps_set_config function
 * @sps_get_config_ptr: pointer to sps_get_config function
 * @sps_device_reset_ptr: pointer to sps_device_reset function
 * @sps_register_event_ptr: pointer to sps_register_event function
 * @sps_transfer_one_ptr: pointer to sps_transfer_one function
 * @sps_get_iovec_ptr: pointer to sps_get_iovec function
 * @sps_get_unused_desc_num_ptr: pointer to sps_get_unused_desc_num function
 * @dma_to: enum for the direction of dma operations to device
 * @dma_from: enum for the direction of dma operations from device
 *
 * This struct contains the interface from bam_dmux to smsm and sps. The
 * pointers can be swapped out at run time to provide different functionality.
 */
struct bam_ops_if {
	/* smsm */
	int (*smsm_change_state_ptr)(struct qcom_smem_state *state, u32 mask,
		u32 value);

	struct qcom_smem_state *(*smsm_get_state_ptr)(struct device *dev,
		const char *con_id, unsigned int *bit);

	void (*smsm_put_state_ptr)(struct qcom_smem_state *state);

	/* sps */
	int (*sps_connect_ptr)(struct sps_pipe *h, struct sps_connect *connect);

	int (*sps_disconnect_ptr)(struct sps_pipe *h);

	int (*sps_register_bam_device_ptr)(
		const struct sps_bam_props *bam_props,
		unsigned long *dev_handle);

	int (*sps_deregister_bam_device_ptr)(unsigned long dev_handle);

	struct sps_pipe *(*sps_alloc_endpoint_ptr)(void);

	int (*sps_free_endpoint_ptr)(struct sps_pipe *h);

	int (*sps_set_config_ptr)(struct sps_pipe *h,
		struct sps_connect *config);

	int (*sps_get_config_ptr)(struct sps_pipe *h,
		struct sps_connect *config);

	int (*sps_device_reset_ptr)(unsigned long dev);

	int (*sps_register_event_ptr)(struct sps_pipe *h,
		struct sps_register_event *reg);

	int (*sps_transfer_one_ptr)(struct sps_pipe *h,
		phys_addr_t addr, u32 size,
		void *user, u32 flags);

	int (*sps_get_iovec_ptr)(struct sps_pipe *h,
		struct sps_iovec *iovec);

	int (*sps_get_unused_desc_num_ptr)(struct sps_pipe *h,
		u32 *desc_num);

	enum dma_data_direction dma_to;

	enum dma_data_direction dma_from;

	u32 a2_pwr_state;

	void *pwr_state;

	void *pwr_ack_state;
};

/**
 * struct bam_mux_hdr - struct which contains bam dmux header info
 * @magic_num: magic number placed at start to ensure that it is actually a
 * valid bam dmux header
 * @signal: optional signaling bits with commmand type specific definitions
 * @cmd: the command
 * @pad_len: the length of padding
 * @ch_id: the id of the bam dmux channel that this is sent on
 * @pkt_len: the length of the packet that this is the header of
 */
struct bam_mux_hdr {
	uint16_t magic_num;
	uint8_t signal;
	uint8_t cmd;
	uint8_t pad_len;
	uint8_t ch_id;
	uint16_t pkt_len;
};

/**
 * struct rx_pkt_info - struct describing an rx packet
 * @skb: socket buffer containing the packet
 * @dma_address: dma mapped address of the packet
 * @work: work_struct for processing the packet
 * @list_node: list_head for placing this on a list
 * @sps_size: size of the sps_iovec for this packet
 * @len: total length of the buffer containing this packet
 */
struct rx_pkt_info {
	struct sk_buff *skb;
	dma_addr_t dma_address;
	struct work_struct work;
	struct list_head list_node;
	uint16_t sps_size;
	uint16_t len;
};

/**
 * struct tx_pkt_info - struct describing a tx packet
 * @skb: socket buffer containing the packet
 * @dma_address: dma mapped address of the packet
 * @is_cmd: signifies whether this is a command or data packet
 * @len: length og the packet
 * @work: work_struct for processing this packet
 * @list_node: list_head for placing this on a list
 * @ts_sec: seconds portion of the timestamp
 * @ts_nsec: nanoseconds portion of the timestamp
 *
 */
struct tx_pkt_info {
	struct sk_buff *skb;
	dma_addr_t dma_address;
	char is_cmd;
	uint32_t len;
	struct work_struct work;
	struct list_head list_node;
	unsigned int ts_sec;
	unsigned long ts_nsec;
};

void msm_bam_dmux_set_bam_ops(struct bam_ops_if *ops);

void msm_bam_dmux_deinit(void);

void msm_bam_dmux_reinit(void);

#endif /* _BAM_DMUX_PRIVATE_H */
