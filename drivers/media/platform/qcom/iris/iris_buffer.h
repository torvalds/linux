/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_BUFFER_H__
#define __IRIS_BUFFER_H__

#include <media/videobuf2-v4l2.h>

struct iris_inst;

#define to_iris_buffer(ptr)	container_of(ptr, struct iris_buffer, vb2)

/**
 * enum iris_buffer_type
 *
 * @BUF_INPUT: input buffer to the iris hardware
 * @BUF_OUTPUT: output buffer from the iris hardware
 * @BUF_BIN: buffer to store intermediate bin data
 * @BUF_ARP: buffer for auto register programming
 * @BUF_COMV: buffer to store colocated motion vectors
 * @BUF_NON_COMV: buffer to hold config data for HW
 * @BUF_LINE: buffer to store decoding/encoding context data for HW
 * @BUF_DPB: buffer to store display picture buffers for reference
 * @BUF_PERSIST: buffer to store session context data
 * @BUF_SCRATCH_1: buffer to store decoding/encoding context data for HW
 * @BUF_SCRATCH_2: buffer to store encoding context data for HW
 * @BUF_VPSS: buffer to store VPSS context data for HW
 * @BUF_TYPE_MAX: max buffer types
 */
enum iris_buffer_type {
	BUF_INPUT = 1,
	BUF_OUTPUT,
	BUF_BIN,
	BUF_ARP,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
	BUF_DPB,
	BUF_PERSIST,
	BUF_SCRATCH_1,
	BUF_SCRATCH_2,
	BUF_VPSS,
	BUF_TYPE_MAX,
};

/*
 * enum iris_buffer_attributes
 *
 * BUF_ATTR_DEFERRED: buffer queued by client but not submitted to firmware.
 * BUF_ATTR_PENDING_RELEASE: buffers requested to be released from firmware.
 * BUF_ATTR_QUEUED: buffers submitted to firmware.
 * BUF_ATTR_DEQUEUED: buffers received from firmware.
 * BUF_ATTR_BUFFER_DONE: buffers sent back to vb2.
 */
enum iris_buffer_attributes {
	BUF_ATTR_DEFERRED		= BIT(0),
	BUF_ATTR_PENDING_RELEASE	= BIT(1),
	BUF_ATTR_QUEUED			= BIT(2),
	BUF_ATTR_DEQUEUED		= BIT(3),
	BUF_ATTR_BUFFER_DONE		= BIT(4),
};

/**
 * struct iris_buffer
 *
 * @vb2: v4l2 vb2 buffer
 * @list: list head for the iris_buffers structure
 * @inst: iris instance structure
 * @type: enum for type of iris buffer
 * @index: identifier for the iris buffer
 * @fd: file descriptor of the buffer
 * @buffer_size: accessible buffer size in bytes starting from addr_offset
 * @data_offset: accessible buffer offset from base address
 * @data_size: data size in bytes
 * @device_addr: device address of the buffer
 * @kvaddr: kernel virtual address of the buffer
 * @dma_attrs: dma attributes
 * @flags: buffer flags. It is represented as bit masks.
 * @timestamp: timestamp of the buffer in nano seconds (ns)
 * @attr: enum for iris buffer attributes
 */
struct iris_buffer {
	struct vb2_v4l2_buffer		vb2;
	struct list_head		list;
	struct iris_inst		*inst;
	enum iris_buffer_type		type;
	u32				index;
	int				fd;
	size_t				buffer_size;
	u32				data_offset;
	size_t				data_size;
	dma_addr_t			device_addr;
	void				*kvaddr;
	unsigned long			dma_attrs;
	u32				flags; /* V4L2_BUF_FLAG_* */
	u64				timestamp;
	enum iris_buffer_attributes	attr;
};

struct iris_buffers {
	struct list_head	list;
	u32			min_count;
	u32			size;
};

int iris_get_buffer_size(struct iris_inst *inst, enum iris_buffer_type buffer_type);
void iris_get_internal_buffers(struct iris_inst *inst, u32 plane);
int iris_create_internal_buffers(struct iris_inst *inst, u32 plane);
int iris_queue_internal_buffers(struct iris_inst *inst, u32 plane);
int iris_queue_internal_deferred_buffers(struct iris_inst *inst, enum iris_buffer_type buffer_type);
int iris_destroy_internal_buffer(struct iris_inst *inst, struct iris_buffer *buffer);
int iris_destroy_all_internal_buffers(struct iris_inst *inst, u32 plane);
int iris_destroy_dequeued_internal_buffers(struct iris_inst *inst, u32 plane);
int iris_alloc_and_queue_persist_bufs(struct iris_inst *inst, enum iris_buffer_type buf_type);
int iris_alloc_and_queue_input_int_bufs(struct iris_inst *inst);
int iris_queue_buffer(struct iris_inst *inst, struct iris_buffer *buf);
int iris_queue_deferred_buffers(struct iris_inst *inst, enum iris_buffer_type buf_type);
int iris_vb2_buffer_done(struct iris_inst *inst, struct iris_buffer *buf);
void iris_vb2_queue_error(struct iris_inst *inst);

#endif
