/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Google, Inc.
 *
 */

#ifndef _UAPI_LINUX_VSOC_SHM_H
#define _UAPI_LINUX_VSOC_SHM_H

#include <linux/types.h>

/**
 * A permission is a token that permits a receiver to read and/or write an area
 * of memory within a Vsoc region.
 *
 * An fd_scoped permission grants both read and write access, and can be
 * attached to a file description (see open(2)).
 * Ownership of the area can then be shared by passing a file descriptor
 * among processes.
 *
 * begin_offset and end_offset define the area of memory that is controlled by
 * the permission. owner_offset points to a word, also in shared memory, that
 * controls ownership of the area.
 *
 * ownership of the region expires when the associated file description is
 * released.
 *
 * At most one permission can be attached to each file description.
 *
 * This is useful when implementing HALs like gralloc that scope and pass
 * ownership of shared resources via file descriptors.
 *
 * The caller is responsibe for doing any fencing.
 *
 * The calling process will normally identify a currently free area of
 * memory. It will construct a proposed fd_scoped_permission_arg structure:
 *
 *   begin_offset and end_offset describe the area being claimed
 *
 *   owner_offset points to the location in shared memory that indicates the
 *   owner of the area.
 *
 *   owned_value is the value that will be stored in owner_offset iff the
 *   permission can be granted. It must be different than VSOC_REGION_FREE.
 *
 * Two fd_scoped_permission structures are compatible if they vary only by
 * their owned_value fields.
 *
 * The driver ensures that, for any group of simultaneous callers proposing
 * compatible fd_scoped_permissions, it will accept exactly one of the
 * propopsals. The other callers will get a failure with errno of EAGAIN.
 *
 * A process receiving a file descriptor can identify the region being
 * granted using the VSOC_GET_FD_SCOPED_PERMISSION ioctl.
 */
struct fd_scoped_permission {
	__u32 begin_offset;
	__u32 end_offset;
	__u32 owner_offset;
	__u32 owned_value;
};

/*
 * This value represents a free area of memory. The driver expects to see this
 * value at owner_offset when creating a permission otherwise it will not do it,
 * and will write this value back once the permission is no longer needed.
 */
#define VSOC_REGION_FREE ((__u32)0)

/**
 * ioctl argument for VSOC_CREATE_FD_SCOPE_PERMISSION
 */
struct fd_scoped_permission_arg {
	struct fd_scoped_permission perm;
	__s32 managed_region_fd;
};

#define VSOC_NODE_FREE ((__u32)0)

/*
 * Describes a signal table in shared memory. Each non-zero entry in the
 * table indicates that the receiver should signal the futex at the given
 * offset. Offsets are relative to the region, not the shared memory window.
 *
 * interrupt_signalled_offset is used to reliably signal interrupts across the
 * vmm boundary. There are two roles: transmitter and receiver. For example,
 * in the host_to_guest_signal_table the host is the transmitter and the
 * guest is the receiver. The protocol is as follows:
 *
 * 1. The transmitter should convert the offset of the futex to an offset
 *    in the signal table [0, (1 << num_nodes_lg2))
 *    The transmitter can choose any appropriate hashing algorithm, including
 *    hash = futex_offset & ((1 << num_nodes_lg2) - 1)
 *
 * 3. The transmitter should atomically compare and swap futex_offset with 0
 *    at hash. There are 3 possible outcomes
 *      a. The swap fails because the futex_offset is already in the table.
 *         The transmitter should stop.
 *      b. Some other offset is in the table. This is a hash collision. The
 *         transmitter should move to another table slot and try again. One
 *         possible algorithm:
 *         hash = (hash + 1) & ((1 << num_nodes_lg2) - 1)
 *      c. The swap worked. Continue below.
 *
 * 3. The transmitter atomically swaps 1 with the value at the
 *    interrupt_signalled_offset. There are two outcomes:
 *      a. The prior value was 1. In this case an interrupt has already been
 *         posted. The transmitter is done.
 *      b. The prior value was 0, indicating that the receiver may be sleeping.
 *         The transmitter will issue an interrupt.
 *
 * 4. On waking the receiver immediately exchanges a 0 with the
 *    interrupt_signalled_offset. If it receives a 0 then this a spurious
 *    interrupt. That may occasionally happen in the current protocol, but
 *    should be rare.
 *
 * 5. The receiver scans the signal table by atomicaly exchanging 0 at each
 *    location. If a non-zero offset is returned from the exchange the
 *    receiver wakes all sleepers at the given offset:
 *      futex((int*)(region_base + old_value), FUTEX_WAKE, MAX_INT);
 *
 * 6. The receiver thread then does a conditional wait, waking immediately
 *    if the value at interrupt_signalled_offset is non-zero. This catches cases
 *    here additional  signals were posted while the table was being scanned.
 *    On the guest the wait is handled via the VSOC_WAIT_FOR_INCOMING_INTERRUPT
 *    ioctl.
 */
struct vsoc_signal_table_layout {
	/* log_2(Number of signal table entries) */
	__u32 num_nodes_lg2;
	/*
	 * Offset to the first signal table entry relative to the start of the
	 * region
	 */
	__u32 futex_uaddr_table_offset;
	/*
	 * Offset to an atomic_t / atomic uint32_t. A non-zero value indicates
	 * that one or more offsets are currently posted in the table.
	 * semi-unique access to an entry in the table
	 */
	__u32 interrupt_signalled_offset;
};

#define VSOC_REGION_WHOLE ((__s32)0)
#define VSOC_DEVICE_NAME_SZ 16

/**
 * Each HAL would (usually) talk to a single device region
 * Mulitple entities care about these regions:
 * - The ivshmem_server will populate the regions in shared memory
 * - The guest kernel will read the region, create minor device nodes, and
 *   allow interested parties to register for FUTEX_WAKE events in the region
 * - HALs will access via the minor device nodes published by the guest kernel
 * - Host side processes will access the region via the ivshmem_server:
 *   1. Pass name to ivshmem_server at a UNIX socket
 *   2. ivshmemserver will reply with 2 fds:
 *     - host->guest doorbell fd
 *     - guest->host doorbell fd
 *     - fd for the shared memory region
 *     - region offset
 *   3. Start a futex receiver thread on the doorbell fd pointed at the
 *      signal_nodes
 */
struct vsoc_device_region {
	__u16 current_version;
	__u16 min_compatible_version;
	__u32 region_begin_offset;
	__u32 region_end_offset;
	__u32 offset_of_region_data;
	struct vsoc_signal_table_layout guest_to_host_signal_table;
	struct vsoc_signal_table_layout host_to_guest_signal_table;
	/* Name of the device. Must always be terminated with a '\0', so
	 * the longest supported device name is 15 characters.
	 */
	char device_name[VSOC_DEVICE_NAME_SZ];
	/* There are two ways that permissions to access regions are handled:
	 *   - When subdivided_by is VSOC_REGION_WHOLE, any process that can
	 *     open the device node for the region gains complete access to it.
	 *   - When subdivided is set processes that open the region cannot
	 *     access it. Access to a sub-region must be established by invoking
	 *     the VSOC_CREATE_FD_SCOPE_PERMISSION ioctl on the region
	 *     referenced in subdivided_by, providing a fileinstance
	 *     (represented by a fd) opened on this region.
	 */
	__u32 managed_by;
};

/*
 * The vsoc layout descriptor.
 * The first 4K should be reserved for the shm header and region descriptors.
 * The regions should be page aligned.
 */

struct vsoc_shm_layout_descriptor {
	__u16 major_version;
	__u16 minor_version;

	/* size of the shm. This may be redundant but nice to have */
	__u32 size;

	/* number of shared memory regions */
	__u32 region_count;

	/* The offset to the start of region descriptors */
	__u32 vsoc_region_desc_offset;
};

/*
 * This specifies the current version that should be stored in
 * vsoc_shm_layout_descriptor.major_version and
 * vsoc_shm_layout_descriptor.minor_version.
 * It should be updated only if the vsoc_device_region and
 * vsoc_shm_layout_descriptor structures have changed.
 * Versioning within each region is transferred
 * via the min_compatible_version and current_version fields in
 * vsoc_device_region. The driver does not consult these fields: they are left
 * for the HALs and host processes and will change independently of the layout
 * version.
 */
#define CURRENT_VSOC_LAYOUT_MAJOR_VERSION 2
#define CURRENT_VSOC_LAYOUT_MINOR_VERSION 0

#define VSOC_CREATE_FD_SCOPED_PERMISSION \
	_IOW(0xF5, 0, struct fd_scoped_permission)
#define VSOC_GET_FD_SCOPED_PERMISSION _IOR(0xF5, 1, struct fd_scoped_permission)

/*
 * This is used to signal the host to scan the guest_to_host_signal_table
 * for new futexes to wake. This sends an interrupt if one is not already
 * in flight.
 */
#define VSOC_MAYBE_SEND_INTERRUPT_TO_HOST _IO(0xF5, 2)

/*
 * When this returns the guest will scan host_to_guest_signal_table to
 * check for new futexes to wake.
 */
/* TODO(ghartman): Consider moving this to the bottom half */
#define VSOC_WAIT_FOR_INCOMING_INTERRUPT _IO(0xF5, 3)

/*
 * Guest HALs will use this to retrieve the region description after
 * opening their device node.
 */
#define VSOC_DESCRIBE_REGION _IOR(0xF5, 4, struct vsoc_device_region)

/*
 * Wake any threads that may be waiting for a host interrupt on this region.
 * This is mostly used during shutdown.
 */
#define VSOC_SELF_INTERRUPT _IO(0xF5, 5)

/*
 * This is used to signal the host to scan the guest_to_host_signal_table
 * for new futexes to wake. This sends an interrupt unconditionally.
 */
#define VSOC_SEND_INTERRUPT_TO_HOST _IO(0xF5, 6)

enum wait_types {
	VSOC_WAIT_UNDEFINED = 0,
	VSOC_WAIT_IF_EQUAL = 1,
	VSOC_WAIT_IF_EQUAL_TIMEOUT = 2
};

/*
 * Wait for a condition to be true
 *
 * Note, this is sized and aligned so the 32 bit and 64 bit layouts are
 * identical.
 */
struct vsoc_cond_wait {
	/* Input: Offset of the 32 bit word to check */
	__u32 offset;
	/* Input: Value that will be compared with the offset */
	__u32 value;
	/* Monotonic time to wake at in seconds */
	__u64 wake_time_sec;
	/* Input: Monotonic time to wait in nanoseconds */
	__u32 wake_time_nsec;
	/* Input: Type of wait */
	__u32 wait_type;
	/* Output: Number of times the thread woke before returning. */
	__u32 wakes;
	/* Ensure that we're 8-byte aligned and 8 byte length for 32/64 bit
	 * compatibility.
	 */
	__u32 reserved_1;
};

#define VSOC_COND_WAIT _IOWR(0xF5, 7, struct vsoc_cond_wait)

/* Wake any local threads waiting at the offset given in arg */
#define VSOC_COND_WAKE _IO(0xF5, 8)

#endif /* _UAPI_LINUX_VSOC_SHM_H */
