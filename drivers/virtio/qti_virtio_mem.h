/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef QTI_VIRTIO_MEM_PRIVATE_H
#define QTI_VIRTIO_MEM_PRIVATE_H

int virtio_mem_update_config_size(s64 size, bool sync);
void qvm_update_plugged_size(uint64_t size);
int virtio_mem_get_device_block_size(uint64_t *device_block_size);
int virtio_mem_get_max_plugin_threshold(uint64_t *max_plugin_threshold);

#endif
