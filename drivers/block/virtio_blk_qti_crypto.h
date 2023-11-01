/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _VIRTIO_BLK_QTI_CRYPTO_H
#define _VIRTIO_BLK_QTI_CRYPTO_H

#include <linux/device.h>
#include <linux/blkdev.h>

/**
 * This function intializes the supported crypto capabilities
 * and create crypto profile to manage keyslots for virtual
 * disks.
 *
 * Return: zero on success, else a -errno value
 */
int virtblk_init_crypto_qti_spec(struct device *dev);

/**
 * set up a crypto profile in the virtual disks request_queue
 *
 * @request_queue: virtual disk request queue
 */
void virtblk_crypto_qti_crypto_register(struct request_queue *q);

#endif /* _VIRTIO_BLK_QTI_CRYPTO_H */

