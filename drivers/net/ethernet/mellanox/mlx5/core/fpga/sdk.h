/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef MLX5_FPGA_SDK_H
#define MLX5_FPGA_SDK_H

#include <linux/types.h>
#include <linux/dma-direction.h>

/**
 * DOC: Innova SDK
 * This header defines the in-kernel API for Innova FPGA client drivers.
 */

struct mlx5_fpga_conn;
struct mlx5_fpga_device;

/**
 * struct mlx5_fpga_dma_entry - A scatter-gather DMA entry
 */
struct mlx5_fpga_dma_entry {
	/** @data: Virtual address pointer to the data */
	void *data;
	/** @size: Size in bytes of the data */
	unsigned int size;
	/** @dma_addr: Private member. Physical DMA-mapped address of the data */
	dma_addr_t dma_addr;
};

/**
 * struct mlx5_fpga_dma_buf - A packet buffer
 * May contain up to 2 scatter-gather data entries
 */
struct mlx5_fpga_dma_buf {
	/** @dma_dir: DMA direction */
	enum dma_data_direction dma_dir;
	/** @sg: Scatter-gather entries pointing to the data in memory */
	struct mlx5_fpga_dma_entry sg[2];
	/** @list: Item in SQ backlog, for TX packets */
	struct list_head list;
	/**
	 * @complete: Completion routine, for TX packets
	 * @conn: FPGA Connection this packet was sent to
	 * @fdev: FPGA device this packet was sent to
	 * @buf: The packet buffer
	 * @status: 0 if successful, or an error code otherwise
	 */
	void (*complete)(struct mlx5_fpga_conn *conn,
			 struct mlx5_fpga_device *fdev,
			 struct mlx5_fpga_dma_buf *buf, u8 status);
};

/**
 * struct mlx5_fpga_conn_attr - FPGA connection attributes
 * Describes the attributes of a connection
 */
struct mlx5_fpga_conn_attr {
	/** @tx_size: Size of connection TX queue, in packets */
	unsigned int tx_size;
	/** @rx_size: Size of connection RX queue, in packets */
	unsigned int rx_size;
	/**
	 * @recv_cb: Callback function which is called for received packets
	 * @cb_arg: The value provided in mlx5_fpga_conn_attr.cb_arg
	 * @buf: A buffer containing a received packet
	 *
	 * buf is guaranteed to only contain a single scatter-gather entry.
	 * The size of the actual packet received is specified in buf.sg[0].size
	 * When this callback returns, the packet buffer may be re-used for
	 * subsequent receives.
	 */
	void (*recv_cb)(void *cb_arg, struct mlx5_fpga_dma_buf *buf);
	void *cb_arg;
};

#endif /* MLX5_FPGA_SDK_H */
