/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_CSF_IOCTL_H_
#define _KBASE_CSF_IOCTL_H_

#include <asm-generic/ioctl.h>
#include <linux/types.h>

/*
 * 1.0:
 * - CSF IOCTL header separated from JM
 */

#define BASE_UK_VERSION_MAJOR 1
#define BASE_UK_VERSION_MINOR 0

/**
 * struct kbase_ioctl_version_check - Check version compatibility between
 * kernel and userspace
 *
 * @major: Major version number
 * @minor: Minor version number
 */
struct kbase_ioctl_version_check {
	__u16 major;
	__u16 minor;
};

#define KBASE_IOCTL_VERSION_CHECK \
	_IOWR(KBASE_IOCTL_TYPE, 52, struct kbase_ioctl_version_check)

#define KBASE_IOCTL_VERSION_CHECK_RESERVED \
	_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)


/**
 * struct kbase_ioctl_cs_queue_register - Register a GPU command queue with the
 *                                        base back-end
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 * @buffer_size: Size of the buffer in bytes
 * @priority: Priority of the queue within a group when run within a process
 * @padding: Currently unused, must be zero
 */
struct kbase_ioctl_cs_queue_register {
	__u64 buffer_gpu_addr;
	__u32 buffer_size;
	__u8 priority;
	__u8 padding[3];
};

#define KBASE_IOCTL_CS_QUEUE_REGISTER \
	_IOW(KBASE_IOCTL_TYPE, 36, struct kbase_ioctl_cs_queue_register)

/**
 * struct kbase_ioctl_cs_queue_kick - Kick the GPU command queue group scheduler
 *                                    to notify that a queue has been updated
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 */
struct kbase_ioctl_cs_queue_kick {
	__u64 buffer_gpu_addr;
};

#define KBASE_IOCTL_CS_QUEUE_KICK \
	_IOW(KBASE_IOCTL_TYPE, 37, struct kbase_ioctl_cs_queue_kick)

/**
 * union kbase_ioctl_cs_queue_bind - Bind a GPU command queue to a group
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 * @group_handle: Handle of the group to which the queue should be bound
 * @csi_index: Index of the CSF interface the queue should be bound to
 * @padding: Currently unused, must be zero
 * @mmap_handle: Handle to be used for creating the mapping of command stream
 *               input/output pages
 *
 * @in: Input parameters
 * @out: Output parameters
 *
 */
union kbase_ioctl_cs_queue_bind {
	struct {
		__u64 buffer_gpu_addr;
		__u8 group_handle;
		__u8 csi_index;
		__u8 padding[6];
	} in;
	struct {
		__u64 mmap_handle;
	} out;
};

#define KBASE_IOCTL_CS_QUEUE_BIND \
	_IOWR(KBASE_IOCTL_TYPE, 39, union kbase_ioctl_cs_queue_bind)

/* ioctl 40 is free to use */

/**
 * struct kbase_ioctl_cs_queue_terminate - Terminate a GPU command queue
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 */
struct kbase_ioctl_cs_queue_terminate {
	__u64 buffer_gpu_addr;
};

#define KBASE_IOCTL_CS_QUEUE_TERMINATE \
	_IOW(KBASE_IOCTL_TYPE, 41, struct kbase_ioctl_cs_queue_terminate)

/**
 * union kbase_ioctl_cs_queue_group_create - Create a GPU command queue group
 *
 * @tiler_mask:		Mask of tiler endpoints the group is allowed to use.
 * @fragment_mask:	Mask of fragment endpoints the group is allowed to use.
 * @compute_mask:	Mask of compute endpoints the group is allowed to use.
 * @cs_min:		Minimum number of command streams required.
 * @priority:		Queue group's priority within a process.
 * @tiler_max:		Maximum number of tiler endpoints the group is allowed
 *			to use.
 * @fragment_max:	Maximum number of fragment endpoints the group is
 *			allowed to use.
 * @compute_max:	Maximum number of compute endpoints the group is allowed
 *			to use.
 * @padding:		Currently unused, must be zero
 * @group_handle:	Handle of a newly created queue group.
 *
 * @in: Input parameters
 * @out: Output parameters
 *
 */
union kbase_ioctl_cs_queue_group_create {
	struct {
		__u64 tiler_mask;
		__u64 fragment_mask;
		__u64 compute_mask;
		__u8 cs_min;
		__u8 priority;
		__u8 tiler_max;
		__u8 fragment_max;
		__u8 compute_max;
		__u8 padding[3];

	} in;
	struct {
		__u8 group_handle;
		__u8 padding[7];
	} out;
};

#define KBASE_IOCTL_CS_QUEUE_GROUP_CREATE \
	_IOWR(KBASE_IOCTL_TYPE, 42, union kbase_ioctl_cs_queue_group_create)

/**
 * struct kbase_ioctl_cs_queue_group_term - Terminate a GPU command queue group
 *
 * @group_handle: Handle of the queue group to be terminated
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_cs_queue_group_term {
	__u8 group_handle;
	__u8 padding[7];
};

#define KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE \
	_IOW(KBASE_IOCTL_TYPE, 43, struct kbase_ioctl_cs_queue_group_term)

#define KBASE_IOCTL_CS_EVENT_SIGNAL \
	_IO(KBASE_IOCTL_TYPE, 44)

typedef __u8 base_kcpu_queue_id; /* We support up to 256 active KCPU queues */

/**
 * struct kbase_ioctl_kcpu_queue_new - Create a KCPU command queue
 *
 * @id: ID of the new command queue returned by the kernel
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_kcpu_queue_new {
	base_kcpu_queue_id id;
	__u8 padding[7];
};

#define KBASE_IOCTL_KCPU_QUEUE_CREATE \
	_IOR(KBASE_IOCTL_TYPE, 45, struct kbase_ioctl_kcpu_queue_new)

/**
 * struct kbase_ioctl_kcpu_queue_delete - Destroy a KCPU command queue
 *
 * @id: ID of the command queue to be destroyed
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_kcpu_queue_delete {
	base_kcpu_queue_id id;
	__u8 padding[7];
};

#define KBASE_IOCTL_KCPU_QUEUE_DELETE \
	_IOW(KBASE_IOCTL_TYPE, 46, struct kbase_ioctl_kcpu_queue_delete)

/**
 * struct kbase_ioctl_kcpu_queue_enqueue - Enqueue commands into the KCPU queue
 *
 * @addr: Memory address of an array of struct base_kcpu_queue_command
 * @nr_commands: Number of commands in the array
 * @id: kcpu queue identifier, returned by KBASE_IOCTL_KCPU_QUEUE_CREATE ioctl
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_kcpu_queue_enqueue {
	__u64 addr;
	__u32 nr_commands;
	base_kcpu_queue_id id;
	__u8 padding[3];
};

#define KBASE_IOCTL_KCPU_QUEUE_ENQUEUE \
	_IOW(KBASE_IOCTL_TYPE, 47, struct kbase_ioctl_kcpu_queue_enqueue)

/**
 * union kbase_ioctl_cs_tiler_heap_init - Initialize chunked tiler memory heap
 *
 * @chunk_size: Size of each chunk.
 * @initial_chunks: Initial number of chunks that heap will be created with.
 * @max_chunks: Maximum number of chunks that the heap is allowed to use.
 * @target_in_flight: Number of render-passes that the driver should attempt to
 *                    keep in flight for which allocation of new chunks is
 *                    allowed.
 * @group_id: Group ID to be used for physical allocations.
 * @gpu_heap_va: GPU VA (virtual address) of Heap context that was set up for
 *               the heap.
 * @first_chunk_va: GPU VA of the first chunk allocated for the heap, actually
 *                  points to the header of heap chunk and not to the low
 *                  address of free memory in the chunk.
 *
 * @in: Input parameters
 * @out: Output parameters
 *
 */
union kbase_ioctl_cs_tiler_heap_init {
	struct {
		__u32 chunk_size;
		__u32 initial_chunks;
		__u32 max_chunks;
		__u16 target_in_flight;
		__u8 group_id;
		__u8 padding;
	} in;
	struct {
		__u64 gpu_heap_va;
		__u64 first_chunk_va;
	} out;
};

#define KBASE_IOCTL_CS_TILER_HEAP_INIT \
	_IOWR(KBASE_IOCTL_TYPE, 48, union kbase_ioctl_cs_tiler_heap_init)

/**
 * struct kbase_ioctl_cs_tiler_heap_term - Terminate a chunked tiler heap
 *                                         instance
 *
 * @gpu_heap_va: GPU VA of Heap context that was set up for the heap.
 */
struct kbase_ioctl_cs_tiler_heap_term {
	__u64 gpu_heap_va;
};

#define KBASE_IOCTL_CS_TILER_HEAP_TERM \
	_IOW(KBASE_IOCTL_TYPE, 49, struct kbase_ioctl_cs_tiler_heap_term)

/**
 * union kbase_ioctl_cs_get_glb_iface - Request the global control block
 *                                        of CSF interface capabilities
 *
 * @max_group_num:        The maximum number of groups to be read. Can be 0, in
 *                        which case groups_ptr is unused.
 * @max_total_stream_num: The maximum number of streams to be read. Can be 0, in
 *                        which case streams_ptr is unused.
 * @groups_ptr:       Pointer where to store all the group data (sequentially).
 * @streams_ptr:      Pointer where to store all the stream data (sequentially).
 * @glb_version:      Global interface version. Bits 31:16 hold the major
 *                    version number and 15:0 hold the minor version number.
 *                    A higher minor version is backwards-compatible with a
 *                    lower minor version for the same major version.
 * @features:         Bit mask of features (e.g. whether certain types of job
 *                    can be suspended).
 * @group_num:        Number of command stream groups supported.
 * @prfcnt_size:      Size of CSF performance counters, in bytes. Bits 31:16
 *                    hold the size of firmware performance counter data
 *                    and 15:0 hold the size of hardware performance counter
 *                    data.
 * @total_stream_num: Total number of command streams, summed across all groups.
 * @padding:          Will be zeroed.
 *
 * @in: Input parameters
 * @out: Output parameters
 *
 */
union kbase_ioctl_cs_get_glb_iface {
	struct {
		__u32 max_group_num;
		__u32 max_total_stream_num;
		__u64 groups_ptr;
		__u64 streams_ptr;
	} in;
	struct {
		__u32 glb_version;
		__u32 features;
		__u32 group_num;
		__u32 prfcnt_size;
		__u32 total_stream_num;
		__u32 padding;
	} out;
};

#define KBASE_IOCTL_CS_GET_GLB_IFACE \
	_IOWR(KBASE_IOCTL_TYPE, 51, union kbase_ioctl_cs_get_glb_iface)

/***************
 * test ioctls *
 ***************/
#if MALI_UNIT_TEST
/* These ioctls are purely for test purposes and are not used in the production
 * driver, they therefore may change without notice
 */

/**
 * struct kbase_ioctl_cs_event_memory_write - Write an event memory address
 * @cpu_addr: Memory address to write
 * @value: Value to write
 * @padding: Currently unused, must be zero
 */
struct kbase_ioctl_cs_event_memory_write {
	__u64 cpu_addr;
	__u8 value;
	__u8 padding[7];
};

/**
 * union kbase_ioctl_cs_event_memory_read - Read an event memory address
 * @cpu_addr: Memory address to read
 * @value: Value read
 * @padding: Currently unused, must be zero
 *
 * @in: Input parameters
 * @out: Output parameters
 */
union kbase_ioctl_cs_event_memory_read {
	struct {
		__u64 cpu_addr;
	} in;
	struct {
		__u8 value;
		__u8 padding[7];
	} out;
};

#endif /* MALI_UNIT_TEST */

#endif /* _KBASE_CSF_IOCTL_H_ */
