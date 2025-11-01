/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 - 2025 Intel Corporation
 */

#ifndef IPU7_FW_SYSCOM_ABI_H
#define IPU7_FW_SYSCOM_ABI_H

#include <linux/types.h>

#include "ipu7_fw_common_abi.h"

#pragma pack(push, 1)
#define SYSCOM_QUEUE_MIN_CAPACITY	2U

struct syscom_queue_params_config {
	ia_gofo_addr_t token_array_mem;
	u16 token_size_in_bytes;
	u16 max_capacity;
};

struct syscom_config_s {
	u16 max_output_queues;
	u16 max_input_queues;
};

#pragma pack(pop)

static inline struct syscom_queue_params_config *
syscom_config_get_queue_configs(struct syscom_config_s *config)
{
	return (struct syscom_queue_params_config *)(&config[1]);
}

static inline const struct syscom_queue_params_config *
syscom_config_get_queue_configs_const(const struct syscom_config_s *config)
{
	return (const struct syscom_queue_params_config *)(&config[1]);
}

#pragma pack(push, 1)
struct syscom_queue_indices_s {
	u32 read_index;
	u32 write_index;
};

#pragma pack(pop)

#endif
