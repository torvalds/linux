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
 * Intel MIC driver.
 *
 */
#ifndef __MIC_DEV_H__
#define __MIC_DEV_H__

/**
 * struct mic_mw - MIC memory window
 *
 * @pa: Base physical address.
 * @va: Base ioremap'd virtual address.
 * @len: Size of the memory window.
 */
struct mic_mw {
	phys_addr_t pa;
	void __iomem *va;
	resource_size_t len;
};

/*
 * Scratch pad register offsets used by the host to communicate
 * device page DMA address to the card.
 */
#define MIC_DPLO_SPAD 14
#define MIC_DPHI_SPAD 15

/*
 * These values are supposed to be in the config_change field of the
 * device page when the host sends a config change interrupt to the card.
 */
#define MIC_VIRTIO_PARAM_DEV_REMOVE 0x1
#define MIC_VIRTIO_PARAM_CONFIG_CHANGED 0x2

/* Maximum number of DMA channels */
#define MIC_MAX_DMA_CHAN 4

#endif
