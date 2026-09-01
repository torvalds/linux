/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023-2026 Intel Corporation */
#ifndef _ISSEI_DMA_H_
#define _ISSEI_DMA_H_

#include <linux/types.h>

struct issei_device;

/**
 * struct issei_dma_length - sizes of DMA memory portions
 * @h2f: host to firmware buffer size
 * @f2h: firmware to host buffer size
 * @ctl: control buffer size
 */
struct issei_dma_length {
	size_t h2f;
	size_t f2h;
	size_t ctl;
};

/**
 * struct issei_dma - DMA memory structure
 * @vaddr: virtual address
 * @daddr: physical address
 * @length: memory sizes structure
 */
struct issei_dma {
	void *vaddr;
	dma_addr_t daddr;
	struct issei_dma_length length;
};

/* Operation statuses */
#define HAMS_SUCCESS                        0x00
#define HAMS_PROTOCOL_NOT_SUPPORTED         0x01
#define HAMS_DEPRECATED_BUS_MSG             0x02
#define HAMS_CLIENT_NOT_EXISTS              0x03
#define HAMS_MSG_TOO_BIG                    0x04
#define HAMS_MSG_NOT_CONSUMED               0x05
#define HAMS_CORRUPTED_BUS_MSG              0x06
#define HAMS_CORRUPTED_HEADER               0x07
#define HAMS_INVALID_LENGTH                 0x08
#define HAMS_SHARED_MEMORY_SIZE_UNSUPPORTED 0x09
#define HAMS_GENERAL_FATAL_ERROR            0xff

/**
 * struct issei_dma_data - data passed through channel
 * @fw_id: firmware client id
 * @host_id: host client id
 * @flags: flags bitmap
 * @status: operation status
 * @length: data length
 * @buf: pointer to data buffer
 */
struct issei_dma_data {
	u16 fw_id;
	u16 host_id;
	u32 flags;
	u32 status;
	u32 length;
	void *buf;
};

int issei_dmam_setup(struct issei_device *idev);
int issei_dma_write(struct issei_device *idev, const struct issei_dma_data *data);
int issei_dma_read(struct issei_device *idev, struct issei_dma_data *data);

#endif /*_ISSEI_DMA_H_*/
