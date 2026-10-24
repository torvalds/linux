/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PAGEFAULT_TYPES_H_
#define _XE_PAGEFAULT_TYPES_H_

#include <linux/workqueue.h>

struct xe_device;
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

/**
 * enum xe_pagefault_error - Xe page fault servicing error
 *
 * Uniquely identifies the high-level point at which servicing of a page
 * fault failed. Encoded into the reserved low bits of
 * &xe_pagefault.consumer.page_addr (which is always page aligned) so the
 * failure reason can be threaded back up to xe_pagefault_print() without
 * growing the size of struct xe_pagefault. See xe_pagefault_set_error() and
 * xe_pagefault_error_to_str().
 */
enum xe_pagefault_error {
	/** @XE_PAGEFAULT_ERROR_NONE: No error recorded */
	XE_PAGEFAULT_ERROR_NONE = 0,
	/**
	 * @XE_PAGEFAULT_ERROR_VM_NOT_FOUND: VM lookup by ASID failed, e.g.
	 * the VM's file descriptor was already closed and the ASID has been
	 * torn down
	 */
	XE_PAGEFAULT_ERROR_VM_NOT_FOUND,
	/**
	 * @XE_PAGEFAULT_ERROR_VM_NOT_IN_FAULT_MODE: VM found by ASID lookup
	 * but is not in fault mode
	 */
	XE_PAGEFAULT_ERROR_VM_NOT_IN_FAULT_MODE,
	/** @XE_PAGEFAULT_ERROR_VM_CLOSED: VM found but already closed */
	XE_PAGEFAULT_ERROR_VM_CLOSED,
	/** @XE_PAGEFAULT_ERROR_VMA_NOT_FOUND: No VMA covers the faulted address */
	XE_PAGEFAULT_ERROR_VMA_NOT_FOUND,
	/**
	 * @XE_PAGEFAULT_ERROR_READ_ONLY_VIOLATION: Write/atomic fault on a
	 * read-only VMA
	 */
	XE_PAGEFAULT_ERROR_READ_ONLY_VIOLATION,
	/**
	 * @XE_PAGEFAULT_ERROR_VMA_NEEDS_VRAM_CHECK: Failed determining if VMA
	 * requires VRAM for an atomic access
	 */
	XE_PAGEFAULT_ERROR_VMA_NEEDS_VRAM_CHECK,
	/**
	 * @XE_PAGEFAULT_ERROR_VMA_ATOMIC_USERPTR: Atomic access requires VRAM
	 * but VMA is a userptr, which is unsupported
	 */
	XE_PAGEFAULT_ERROR_VMA_ATOMIC_USERPTR,
	/** @XE_PAGEFAULT_ERROR_VMA_USERPTR_PIN: Userptr page pin/repin failed */
	XE_PAGEFAULT_ERROR_VMA_USERPTR_PIN,
	/**
	 * @XE_PAGEFAULT_ERROR_VMA_VALIDATE: Failed to lock/validate VMA's BO
	 * or migrate it to VRAM
	 */
	XE_PAGEFAULT_ERROR_VMA_VALIDATE,
	/** @XE_PAGEFAULT_ERROR_VMA_REBIND: Failed to rebind VMA into page tables */
	XE_PAGEFAULT_ERROR_VMA_REBIND,
	/**
	 * @XE_PAGEFAULT_ERROR_SVM_GARBAGE_COLLECTOR: Failed processing
	 * pending SVM garbage collection (unmaps) prior to servicing the
	 * fault
	 */
	XE_PAGEFAULT_ERROR_SVM_GARBAGE_COLLECTOR,
	/**
	 * @XE_PAGEFAULT_ERROR_SVM_RANGE_NOT_FOUND: Failed to find or insert
	 * an SVM range covering the faulted address
	 */
	XE_PAGEFAULT_ERROR_SVM_RANGE_NOT_FOUND,
	/** @XE_PAGEFAULT_ERROR_SVM_REBIND: Failed to rebind an SVM range into page tables */
	XE_PAGEFAULT_ERROR_SVM_REBIND,
	/**
	 * @XE_PAGEFAULT_ERROR_SVM_NEEDS_VRAM_CHECK: Failed determining if SVM
	 * VMA requires VRAM for an atomic access
	 */
	XE_PAGEFAULT_ERROR_SVM_NEEDS_VRAM_CHECK,
	/**
	 * @XE_PAGEFAULT_ERROR_SVM_VMA_NOT_FOUND: SVM VMA re-lookup after a
	 * range split failed to find a covering VMA
	 */
	XE_PAGEFAULT_ERROR_SVM_VMA_NOT_FOUND,
	/**
	 * @XE_PAGEFAULT_ERROR_SVM_SERVICE_FAILED: SVM range population,
	 * migration, or bind failed
	 */
	XE_PAGEFAULT_ERROR_SVM_SERVICE_FAILED,
};

/** struct xe_pagefault_ops - Xe pagefault ops (producer) */
struct xe_pagefault_ops {
	/**
	 * @ack_fault_begin: Ack fault begin
	 * @private: producer private data
	 *
	 * Page fault producer begins acknowledgment from the consumer.
	 */
	void (*ack_fault_begin)(void *private);
	/**
	 * @ack_fault: Ack fault
	 * @pf: Page fault
	 * @err: Error state of fault
	 *
	 * Page fault producer receives acknowledgment from the consumer and
	 * sends the result to the HW/FW interface.
	 */
	void (*ack_fault)(struct xe_pagefault *pf, int err);
	/**
	 * @ack_fault_end: Ack fault end
	 * @private: producer private data
	 *
	 * Page fault producer ends acknowledgment from the consumer.
	 */
	void (*ack_fault_end)(void *private);
	/**
	 * @print: Print pagefault info
	 * @pf: Page fault
	 * @err_str: Consumer servicing error string
	 *
	 * Print pagefault identification fields decoded from producer,
	 * along with consumer-specific @err_str prints.
	 * Must be implemented by all producers.
	 */
	void (*print)(struct xe_pagefault *pf, const char *err_str);
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
	 * information back to the producer upon fault acknowledgment. After
	 * fault acknowledgment, the producer should only access consumer fields
	 * via well defined helpers.
	 */
	struct {
		/**
		 * @consumer.page_addr: address of page fault, populated by
		 * consumer after fault completion
		 */
		u64 page_addr;
		union {
			struct {
				/**
				 * @consumer.alloc_state: page fault allocation
				 * state
				 */
				u8 alloc_state;
				/**
				 * @consumer.access_type: access type, u8 rather
				 * than enum to keep size compact
				 */
				u8 access_type;
#define XE_PAGEFAULT_ACCESS_TYPE_MASK	GENMASK(1, 0)
#define XE_PAGEFAULT_ACCESS_PREFETCH	BIT(7)
				/**
				 * @consumer.fault_type_level: fault type and
				 * level, u8 rather than enum to keep size
				 * compact
				 */
				u8 fault_type_level;
#define XE_PAGEFAULT_TYPE_LEVEL_NACK		0xff	/* Producer indicates nack fault */
#define XE_PAGEFAULT_LEVEL_MASK			GENMASK(2, 0)
#define XE_PAGEFAULT_TYPE_MASK			GENMASK(6, 3)
#define XE_PAGEFAULT_REQUEUE_MASK		BIT(7)
				/** @consumer.engine_class_instance: engine class and instance */
				u8 engine_class_instance;
#define XE_PAGEFAULT_ENGINE_CLASS_MASK		GENMASK(3, 0)
#define XE_PAGEFAULT_ENGINE_INSTANCE_MASK	GENMASK(7, 4)
				/**
				 * @consumer.id: address space ID and SRCID, folded into one
				 * to keep size compact
				 */
				u32 id;
#define XE_PAGEFAULT_ASID_MASK	GENMASK(23, 0)
#define XE_PAGEFAULT_SRCID_MASK	GENMASK(31, 24)
			};
			/**
			 * @consumer.end_addr: end address of page fault,
			 * populated by consumer after fault completion
			 */
			u64 end_addr;
		};
		/**
		 * @consumer.next: next pagefault chained to this fault,
		 * protected by pf_queue lock
		 */
		struct xe_pagefault *next;
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
 * struct xe_pagefault_queue - Xe pagefault queue (consumer)
 *
 * Used to capture all device page faults for deferred processing. Size this
 * queue to absorb the device’s worst-case number of outstanding faults.
 */
struct xe_pagefault_queue {
	/** @xe: Back-pointer to the Xe device */
	struct xe_device *xe;
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
	/** @lock: protects page fault queue, workers caches */
	spinlock_t lock;
};

/**
 * struct xe_pagefault_work - Xe page fault work item (consumer)
 *
 * Represents a worker that pops a &struct xe_pagefault from the page fault
 * queue and processes it.
 */
struct xe_pagefault_work {
	/** @xe: Back-pointer to the Xe device */
	struct xe_device *xe;
	/** @id: Identifier for this work item */
	int id;
	/**
	 * @cache: Page fault cache for the currently processed fault
	 *
	 * Protected by the page fault queue lock.
	 */
	struct {
		/** @cache.start: Start address of the current page fault */
		u64 start;
		/** @cache.end: End address of the current page fault */
		u64 end;
		/** @cache.asid: Address space ID of the current page fault */
		u32 asid;
		/** @cache.pf: Pointer to the current page fault */
		struct xe_pagefault *pf;
	} cache;
	/** @work: Work item used to process the page fault */
	struct work_struct work;
};

#endif
