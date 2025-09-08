/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_SYSCOM_H
#define IPU7_SYSCOM_H

#include <linux/types.h>

struct syscom_config_s;
struct syscom_queue_params_config;

struct syscom_queue_config {
	void *token_array_mem;
	u32 queue_size;
	u16 token_size_in_bytes;
	u16 max_capacity;
};

struct ipu7_syscom_context {
	u16 num_input_queues;
	u16 num_output_queues;
	struct syscom_queue_config *queue_configs;
	void __iomem *queue_indices;
	dma_addr_t queue_mem_dma_addr;
	void *queue_mem;
	u32 queue_mem_size;
};

void ipu7_syscom_put_token(struct ipu7_syscom_context *ctx, int q);
void *ipu7_syscom_get_token(struct ipu7_syscom_context *ctx, int q);
struct syscom_queue_params_config *
ipu7_syscom_get_queue_config(struct syscom_config_s *config);
#endif
