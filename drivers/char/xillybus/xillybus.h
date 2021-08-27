/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/drivers/misc/xillybus.h
 *
 * Copyright 2011 Xillybus Ltd, http://xillybus.com
 *
 * Header file for the Xillybus FPGA/host framework.
 */

#ifndef __XILLYBUS_H
#define __XILLYBUS_H

#include <linux/list.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct xilly_endpoint_hardware;

struct xilly_buffer {
	void *addr;
	dma_addr_t dma_addr;
	int end_offset; /* Counting elements, not bytes */
};

struct xilly_idt_handle {
	unsigned char *chandesc;
	unsigned char *names;
	int names_len;
	int entries;
};

/*
 * Read-write confusion: wr_* and rd_* notation sticks to FPGA view, so
 * wr_* buffers are those consumed by read(), since the FPGA writes to them
 * and vice versa.
 */

struct xilly_channel {
	struct xilly_endpoint *endpoint;
	int chan_num;
	int log2_element_size;
	int seekable;

	struct xilly_buffer **wr_buffers; /* FPGA writes, driver reads! */
	int num_wr_buffers;
	unsigned int wr_buf_size; /* In bytes */
	int wr_fpga_buf_idx;
	int wr_host_buf_idx;
	int wr_host_buf_pos;
	int wr_empty;
	int wr_ready; /* Significant only when wr_empty == 1 */
	int wr_sleepy;
	int wr_eof;
	int wr_hangup;
	spinlock_t wr_spinlock;
	struct mutex wr_mutex;
	wait_queue_head_t wr_wait;
	wait_queue_head_t wr_ready_wait;
	int wr_ref_count;
	int wr_synchronous;
	int wr_allow_partial;
	int wr_exclusive_open;
	int wr_supports_nonempty;

	struct xilly_buffer **rd_buffers; /* FPGA reads, driver writes! */
	int num_rd_buffers;
	unsigned int rd_buf_size; /* In bytes */
	int rd_fpga_buf_idx;
	int rd_host_buf_pos;
	int rd_host_buf_idx;
	int rd_full;
	spinlock_t rd_spinlock;
	struct mutex rd_mutex;
	wait_queue_head_t rd_wait;
	int rd_ref_count;
	int rd_allow_partial;
	int rd_synchronous;
	int rd_exclusive_open;
	struct delayed_work rd_workitem;
	unsigned char rd_leftovers[4];
};

struct xilly_endpoint {
	struct device *dev;
	struct xilly_endpoint_hardware *ephw;

	int dma_using_dac; /* =1 if 64-bit DMA is used, =0 otherwise. */
	__iomem void *registers;
	int fatal_error;

	struct mutex register_mutex;
	wait_queue_head_t ep_wait;

	int num_channels; /* EXCLUDING message buffer */
	struct xilly_channel **channels;
	int msg_counter;
	int failed_messages;
	int idtlen;

	u32 *msgbuf_addr;
	dma_addr_t msgbuf_dma_addr;
	unsigned int msg_buf_size;
};

struct xilly_endpoint_hardware {
	struct module *owner;
	void (*hw_sync_sgl_for_cpu)(struct xilly_endpoint *,
				    dma_addr_t,
				    size_t,
				    int);
	void (*hw_sync_sgl_for_device)(struct xilly_endpoint *,
				       dma_addr_t,
				       size_t,
				       int);
	int (*map_single)(struct xilly_endpoint *,
			  void *,
			  size_t,
			  int,
			  dma_addr_t *);
};

struct xilly_mapping {
	struct device *device;
	dma_addr_t dma_addr;
	size_t size;
	int direction;
};

irqreturn_t xillybus_isr(int irq, void *data);

struct xilly_endpoint *xillybus_init_endpoint(struct device *dev,
					      struct xilly_endpoint_hardware
					      *ephw);

int xillybus_endpoint_discovery(struct xilly_endpoint *endpoint);

void xillybus_endpoint_remove(struct xilly_endpoint *endpoint);

#endif /* __XILLYBUS_H */
