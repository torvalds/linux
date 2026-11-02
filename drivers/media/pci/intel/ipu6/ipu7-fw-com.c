// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Intel Corporation
 */

#include <linux/io.h>

#include "ipu7-fw-com.h"

static void __iomem *ipu7_fw_com_get_indices(struct ipu7_fw_com_context *ctx,
					     u32 q)
{
	return ctx->queue_indices + (q * sizeof(struct ipu7_fw_com_queue_indices));
}

void *ipu7_fw_com_get_token(struct ipu7_fw_com_context *ctx, int q)
{
	struct ipu7_fw_com_queue_config *queue_params = &ctx->queue_configs[q];
	void __iomem *queue_indices = ipu7_fw_com_get_indices(ctx, q);
	u32 write_index = readl(queue_indices +
				offsetof(struct ipu7_fw_com_queue_indices,
					 write_index));
	u32 read_index = readl(queue_indices +
			       offsetof(struct ipu7_fw_com_queue_indices,
					read_index));
	void *token = NULL;

	if (q < ctx->num_output_queues) {
		/* Output queue */
		bool empty = (write_index == read_index);

		if (!empty)
			token = queue_params->token_array_mem +
				read_index *
				queue_params->token_size_in_bytes;
	} else {
		/* Input queue */
		bool full = (read_index == ((write_index + 1U) %
					    (u32)queue_params->max_capacity));

		if (!full)
			token = queue_params->token_array_mem +
				write_index * queue_params->token_size_in_bytes;
	}
	return token;
}
EXPORT_SYMBOL_NS_GPL(ipu7_fw_com_get_token, "INTEL_IPU6");

void ipu7_fw_com_put_token(struct ipu7_fw_com_context *ctx, int q)
{
	struct ipu7_fw_com_queue_config *queue_params = &ctx->queue_configs[q];
	void __iomem *queue_indices = ipu7_fw_com_get_indices(ctx, q);
	u32 offset, index;

	if (q < ctx->num_output_queues)
		/* Output queue */
		offset = offsetof(struct ipu7_fw_com_queue_indices, read_index);

	else
		/* Input queue */
		offset = offsetof(struct ipu7_fw_com_queue_indices, write_index);

	index = readl(queue_indices + offset);
	writel((index + 1U) % queue_params->max_capacity,
	       queue_indices + offset);
}
EXPORT_SYMBOL_NS_GPL(ipu7_fw_com_put_token, "INTEL_IPU6");

struct ipu7_fw_com_queue_params_config *
ipu7_fw_com_get_queue_config(struct ipu7_fw_com_config *config)
{
	return (struct ipu7_fw_com_queue_params_config *)(&config[1]);
}
EXPORT_SYMBOL_NS_GPL(ipu7_fw_com_get_queue_config, "INTEL_IPU6");
