/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PAGEFAULT_TYPES_H_
#define _XE_PAGEFAULT_TYPES_H_

#include <linux/workqueue.h>

struct xe_gt;
struct xe_pagefault;

/** enum xe_pagefault_access_type - Xe page fault access type */
enum xe_pagefault_access_type {
	/** @XE_PAGEFAULT_ACCESS_TYPE_READ: Read access type */
	XE_PAGEFAULT_ACCESS_TYPE_READ	= 0,
	/** @XE_PAGEFAULT_ACCESS_TYPE_WRITE: Write access type */
	XE_PAGEFAULT_ACCESS_TYPE_WRITE	= 1,
	/** @XE_PAGEFAULT_ACCESS_TYPE_ATOMIC: Atomic access type */
	XE_PAGEFAULT_ACCESS_TYPE_ATOMIC	= 2,
};

/** enum xe_pagefault_type - Xe page fault type */
enum xe_pagefault_type {
	/** @XE_PAGEFAULT_TYPE_NOT_PRESENT: Not present */
	XE_PAGEFAULT_TYPE_NOT_PRESENT			= 0,
	/** @XE_PAGEFAULT_TYPE_WRITE_ACCESS_VIOLATION: Write access violation */
	XE_PAGEFAULT_TYPE_WRITE_ACCESS_VIOLATION	= 1,
	/** @XE_PAGEFAULT_TYPE_ATOMIC_ACCESS_VIOLATION: Atomic access violation */
	XE_PAGEFAULT_TYPE_ATOMIC_ACCESS_VIOLATION	= 2,
};

/** struct xe_pagefault_ops - Xe pagefault ops (producer) */
struct xe_pagefault_ops {
	/**
	 * @ack_fault: Ack fault
	 * @pf: Page fault
	 * @err: Error state of fault
	 *
	 * Page fault producer receives acknowledgment from the consumer and
	 * sends the result to the HW/FW interface.
	 */
	void (*ack_fault)(struct xe_pagefault *pf, int err);
};

/**
 * struct xe_pagefault - Xe page fault
 *
 * Generic page fault structure for communication between producer and consumer.
 * Carefully sized to be 64 bytes. Upon a device page fault, the producer
 * populates this structure, and the consumer copies it into the page-fault
 * queue for deferred handling.
 */
struct xe_pagefault {
	/**
	 * @gt: GT of fault
	 */
	struct xe_gt *gt;
	/**
	 * @consumer: State for the software handling the fault. Populated by
	 * the producer and may be modified by the consumer to communicate
	 * information back to the producer upon fault acknowledgment.
	 */
	struct {
		/** @consumer.page_addr: address of page fault */
		u64 page_addr;
		/** @consumer.asid: address space ID */
		u32 asid;
		/**
		 * @consumer.access_type: access type, u8 rather than enum to
		 * keep size compact
		 */
		u8 access_type;
		/**
		 * @consumer.fault_type: fault type, u8 rather than enum to
		 * keep size compact
		 */
		u8 fault_type;
#define XE_PAGEFAULT_LEVEL_NACK		0xff	/* Producer indicates nack fault */
		/** @consumer.fault_level: fault level */
		u8 fault_level;
		/** @consumer.engine_class: engine class */
		u8 engine_class;
		/** @consumer.engine_instance: engine instance */
		u8 engine_instance;
		/** consumer.reserved: reserved bits for future expansion */
		u8 reserved[7];
	} consumer;
	/**
	 * @producer: State for the producer (i.e., HW/FW interface). Populated
	 * by the producer and should not be modified—or even inspected—by the
	 * consumer, except for calling operations.
	 */
	struct {
		/** @producer.private: private pointer */
		void *private;
		/** @producer.ops: operations */
		const struct xe_pagefault_ops *ops;
#define XE_PAGEFAULT_PRODUCER_MSG_LEN_DW	4
		/**
		 * @producer.msg: page fault message, used by producer in fault
		 * acknowledgment to formulate response to HW/FW interface.
		 * Included in the page-fault message because the producer
		 * typically receives the fault in a context where memory cannot
		 * be allocated (e.g., atomic context or the reclaim path).
		 */
		u32 msg[XE_PAGEFAULT_PRODUCER_MSG_LEN_DW];
	} producer;
};

/**
 * struct xe_pagefault_queue: Xe pagefault queue (consumer)
 *
 * Used to capture all device page faults for deferred processing. Size this
 * queue to absorb the device’s worst-case number of outstanding faults.
 */
struct xe_pagefault_queue {
	/**
	 * @data: Data in queue containing struct xe_pagefault, protected by
	 * @lock
	 */
	void *data;
	/** @size: Size of queue in bytes */
	u32 size;
	/** @head: Head pointer in bytes, moved by producer, protected by @lock */
	u32 head;
	/** @tail: Tail pointer in bytes, moved by consumer, protected by @lock */
	u32 tail;
	/** @lock: protects page fault queue */
	spinlock_t lock;
	/** @worker: to process page faults */
	struct work_struct worker;
};

#endif
