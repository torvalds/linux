/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 - 2025 Intel Corporation
 */

#ifndef IPU7_BOOT_H
#define IPU7_BOOT_H

#include <linux/types.h>

struct ipu7_bus_device;
struct syscom_queue_config;

#define FW_QUEUE_CONFIG_SIZE(num_queues)	\
	(sizeof(struct syscom_queue_config) * (num_queues))

int ipu7_boot_init_boot_config(struct ipu7_bus_device *adev,
			       struct syscom_queue_config *qconfigs,
			       int num_queues, u32 uc_freq,
			       dma_addr_t subsys_config, u8 major);
void ipu7_boot_release_boot_config(struct ipu7_bus_device *adev);
int ipu7_boot_start_fw(const struct ipu7_bus_device *adev);
int ipu7_boot_stop_fw(const struct ipu7_bus_device *adev);
u32 ipu7_boot_get_boot_state(const struct ipu7_bus_device *adev);
#endif
