/*
 * ISHTP DMA I/F functions
 *
 * Copyright (c) 2003-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include "ishtp-dev.h"
#include "client.h"

/**
 * ishtp_cl_alloc_dma_buf() - Allocate DMA RX and TX buffer
 * @dev: ishtp device
 *
 * Allocate RX and TX DMA buffer once during bus setup.
 * It allocates 1MB, RX and TX DMA buffer, which are divided
 * into slots.
 */
void	ishtp_cl_alloc_dma_buf(struct ishtp_device *dev)
{
	dma_addr_t	h;

	if (dev->ishtp_host_dma_tx_buf)
		return;

	dev->ishtp_host_dma_tx_buf_size = 1024*1024;
	dev->ishtp_host_dma_rx_buf_size = 1024*1024;

	/* Allocate Tx buffer and init usage bitmap */
	dev->ishtp_host_dma_tx_buf = dma_alloc_coherent(dev->devc,
					dev->ishtp_host_dma_tx_buf_size,
					&h, GFP_KERNEL);
	if (dev->ishtp_host_dma_tx_buf)
		dev->ishtp_host_dma_tx_buf_phys = h;

	dev->ishtp_dma_num_slots = dev->ishtp_host_dma_tx_buf_size /
						DMA_SLOT_SIZE;

	dev->ishtp_dma_tx_map = kcalloc(dev->ishtp_dma_num_slots,
					sizeof(uint8_t),
					GFP_KERNEL);
	spin_lock_init(&dev->ishtp_dma_tx_lock);

	/* Allocate Rx buffer */
	dev->ishtp_host_dma_rx_buf = dma_alloc_coherent(dev->devc,
					dev->ishtp_host_dma_rx_buf_size,
					 &h, GFP_KERNEL);

	if (dev->ishtp_host_dma_rx_buf)
		dev->ishtp_host_dma_rx_buf_phys = h;
}

/**
 * ishtp_cl_free_dma_buf() - Free DMA RX and TX buffer
 * @dev: ishtp device
 *
 * Free DMA buffer when all clients are released. This is
 * only happens during error path in ISH built in driver
 * model
 */
void	ishtp_cl_free_dma_buf(struct ishtp_device *dev)
{
	dma_addr_t	h;

	if (dev->ishtp_host_dma_tx_buf) {
		h = dev->ishtp_host_dma_tx_buf_phys;
		dma_free_coherent(dev->devc, dev->ishtp_host_dma_tx_buf_size,
				  dev->ishtp_host_dma_tx_buf, h);
	}

	if (dev->ishtp_host_dma_rx_buf) {
		h = dev->ishtp_host_dma_rx_buf_phys;
		dma_free_coherent(dev->devc, dev->ishtp_host_dma_rx_buf_size,
				  dev->ishtp_host_dma_rx_buf, h);
	}

	kfree(dev->ishtp_dma_tx_map);
	dev->ishtp_host_dma_tx_buf = NULL;
	dev->ishtp_host_dma_rx_buf = NULL;
	dev->ishtp_dma_tx_map = NULL;
}

/*
 * ishtp_cl_get_dma_send_buf() - Get a DMA memory slot
 * @dev:	ishtp device
 * @size:	Size of memory to get
 *
 * Find and return free address of "size" bytes in dma tx buffer.
 * the function will mark this address as "in-used" memory.
 *
 * Return: NULL when no free buffer else a buffer to copy
 */
void *ishtp_cl_get_dma_send_buf(struct ishtp_device *dev,
				uint32_t size)
{
	unsigned long	flags;
	int i, j, free;
	/* additional slot is needed if there is rem */
	int required_slots = (size / DMA_SLOT_SIZE)
		+ 1 * (size % DMA_SLOT_SIZE != 0);

	spin_lock_irqsave(&dev->ishtp_dma_tx_lock, flags);
	for (i = 0; i <= (dev->ishtp_dma_num_slots - required_slots); i++) {
		free = 1;
		for (j = 0; j < required_slots; j++)
			if (dev->ishtp_dma_tx_map[i+j]) {
				free = 0;
				i += j;
				break;
			}
		if (free) {
			/* mark memory as "caught" */
			for (j = 0; j < required_slots; j++)
				dev->ishtp_dma_tx_map[i+j] = 1;
			spin_unlock_irqrestore(&dev->ishtp_dma_tx_lock, flags);
			return (i * DMA_SLOT_SIZE) +
				(unsigned char *)dev->ishtp_host_dma_tx_buf;
		}
	}
	spin_unlock_irqrestore(&dev->ishtp_dma_tx_lock, flags);
	dev_err(dev->devc, "No free DMA buffer to send msg\n");
	return NULL;
}

/*
 * ishtp_cl_release_dma_acked_mem() - Release DMA memory slot
 * @dev:	ishtp device
 * @msg_addr:	message address of slot
 * @size:	Size of memory to get
 *
 * Release_dma_acked_mem - returnes the acked memory to free list.
 * (from msg_addr, size bytes long)
 */
void ishtp_cl_release_dma_acked_mem(struct ishtp_device *dev,
				    void *msg_addr,
				    uint8_t size)
{
	unsigned long	flags;
	int acked_slots = (size / DMA_SLOT_SIZE)
		+ 1 * (size % DMA_SLOT_SIZE != 0);
	int i, j;

	if ((msg_addr - dev->ishtp_host_dma_tx_buf) % DMA_SLOT_SIZE) {
		dev_err(dev->devc, "Bad DMA Tx ack address\n");
		return;
	}

	i = (msg_addr - dev->ishtp_host_dma_tx_buf) / DMA_SLOT_SIZE;
	spin_lock_irqsave(&dev->ishtp_dma_tx_lock, flags);
	for (j = 0; j < acked_slots; j++) {
		if ((i + j) >= dev->ishtp_dma_num_slots ||
					!dev->ishtp_dma_tx_map[i+j]) {
			/* no such slot, or memory is already free */
			spin_unlock_irqrestore(&dev->ishtp_dma_tx_lock, flags);
			dev_err(dev->devc, "Bad DMA Tx ack address\n");
			return;
		}
		dev->ishtp_dma_tx_map[i+j] = 0;
	}
	spin_unlock_irqrestore(&dev->ishtp_dma_tx_lock, flags);
}
