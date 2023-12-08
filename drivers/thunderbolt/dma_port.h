/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt DMA configuration based mailbox support
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#ifndef DMA_PORT_H_
#define DMA_PORT_H_

#include "tb.h"

struct tb_switch;
struct tb_dma_port;

#define DMA_PORT_CSS_ADDRESS		0x3fffff
#define DMA_PORT_CSS_MAX_SIZE		SZ_128

struct tb_dma_port *dma_port_alloc(struct tb_switch *sw);
void dma_port_free(struct tb_dma_port *dma);
int dma_port_flash_read(struct tb_dma_port *dma, unsigned int address,
			void *buf, size_t size);
int dma_port_flash_update_auth(struct tb_dma_port *dma);
int dma_port_flash_update_auth_status(struct tb_dma_port *dma, u32 *status);
int dma_port_flash_write(struct tb_dma_port *dma, unsigned int address,
			 const void *buf, size_t size);
int dma_port_power_cycle(struct tb_dma_port *dma);

#endif
