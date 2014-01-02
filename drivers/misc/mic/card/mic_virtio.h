/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
 *
 */
#ifndef __MIC_CARD_VIRTIO_H
#define __MIC_CARD_VIRTIO_H

#include <linux/mic_common.h>
#include "mic_device.h"

/*
 * 64 bit I/O access
 */
#ifndef ioread64
#define ioread64 readq
#endif
#ifndef iowrite64
#define iowrite64 writeq
#endif

static inline unsigned mic_desc_size(struct mic_device_desc __iomem *desc)
{
	return mic_aligned_size(*desc)
		+ ioread8(&desc->num_vq) * mic_aligned_size(struct mic_vqconfig)
		+ ioread8(&desc->feature_len) * 2
		+ ioread8(&desc->config_len);
}

static inline struct mic_vqconfig __iomem *
mic_vq_config(struct mic_device_desc __iomem *desc)
{
	return (struct mic_vqconfig __iomem *)(desc + 1);
}

static inline __u8 __iomem *
mic_vq_features(struct mic_device_desc __iomem *desc)
{
	return (__u8 __iomem *)(mic_vq_config(desc) + ioread8(&desc->num_vq));
}

static inline __u8 __iomem *
mic_vq_configspace(struct mic_device_desc __iomem *desc)
{
	return mic_vq_features(desc) + ioread8(&desc->feature_len) * 2;
}
static inline unsigned mic_total_desc_size(struct mic_device_desc __iomem *desc)
{
	return mic_aligned_desc_size(desc) +
		mic_aligned_size(struct mic_device_ctrl);
}

int mic_devices_init(struct mic_driver *mdrv);
void mic_devices_uninit(struct mic_driver *mdrv);

#endif
