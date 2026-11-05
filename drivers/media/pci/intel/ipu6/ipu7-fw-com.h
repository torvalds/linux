/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Intel Corporation */

#ifndef IPU7_FW_COM_H
#define IPU7_FW_COM_H

#include <linux/types.h>

struct ipu7_fw_com_queue_config {
	void *token_array_mem;
	u32 queue_size;
	u16 token_size_in_bytes;
	u16 max_capacity;
};

struct ipu7_fw_com_context {
	u16 num_input_queues;
	u16 num_output_queues;
	struct ipu7_fw_com_queue_config *queue_configs;
	void __iomem *queue_indices;
	dma_addr_t queue_mem_dma_addr;
	void *queue_mem;
	u32 queue_mem_size;
	struct ipu7_boot_abi_cfg *boot_config;
	dma_addr_t boot_config_dma_addr;
	u32 boot_config_size;
	u32 fw_entry;
	void *fw_config;
	dma_addr_t fw_config_dma_addr;
};

struct ipu7_fw_com_queue_params_config {
	u32 token_array_mem;
	u16 token_size_in_bytes;
	u16 max_capacity;
};

struct ipu7_fw_com_config {
	u16 max_output_queues;
	u16 max_input_queues;
};

struct ipu7_fw_com_queue_indices {
	u32 read_index;
	u32 write_index;
};

void ipu7_fw_com_put_token(struct ipu7_fw_com_context *ctx, int q);
void *ipu7_fw_com_get_token(struct ipu7_fw_com_context *ctx, int q);
struct ipu7_fw_com_queue_params_config *
ipu7_fw_com_get_queue_config(struct ipu7_fw_com_config *config);

#endif /* IPU7_FW_COM_H */
