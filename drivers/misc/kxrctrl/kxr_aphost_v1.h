/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __APHOST_H__
#define __APHOST_H__

#include <linux/dma-buf.h>
#include <linux/module.h>

#define MAX_PACK_SIZE 100
#define MAX_DATA_SIZE 32

struct d_packet_t {
	uint64_t ts;
	uint32_t size;
	uint8_t data[MAX_DATA_SIZE];
};

struct cp_buffer_t {
	int8_t c_head;
	int8_t p_head;
	struct d_packet_t  data[MAX_PACK_SIZE];
};

struct js_spi_client {
	struct mutex js_sm_mutex; /* dma alloc and free mutex */

	void   *vaddr;
	size_t vsize;
	struct dma_buf *js_buf;
	spinlock_t smem_lock;
	int memfd;
};

#endif /* __APHOST_H__ */
