/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#ifndef _MIC_INTR_H_
#define _MIC_INTR_H_

#include <linux/bitops.h>
#include <linux/interrupt.h>
/*
 * The minimum number of msix vectors required for normal operation.
 * 3 for virtio network, console and block devices.
 * 1 for card shutdown notifications.
 * 4 for host owned DMA channels.
 * 1 for SCIF
 */
#define MIC_MIN_MSIX 9
#define MIC_NUM_OFFSETS 32

/**
 * mic_intr_source - The type of source that will generate
 * the interrupt.The number of types needs to be in sync with
 * MIC_NUM_INTR_TYPES
 *
 * MIC_INTR_DB: The source is a doorbell
 * MIC_INTR_DMA: The source is a DMA channel
 * MIC_INTR_ERR: The source is an error interrupt e.g. SBOX ERR
 * MIC_NUM_INTR_TYPES: Total number of interrupt sources.
 */
enum mic_intr_type {
	MIC_INTR_DB = 0,
	MIC_INTR_DMA,
	MIC_INTR_ERR,
	MIC_NUM_INTR_TYPES
};

/**
 * struct mic_intr_info - Contains h/w specific interrupt sources
 * information.
 *
 * @intr_start_idx: Contains the starting indexes of the
 * interrupt types.
 * @intr_len: Contains the length of the interrupt types.
 */
struct mic_intr_info {
	u16 intr_start_idx[MIC_NUM_INTR_TYPES];
	u16 intr_len[MIC_NUM_INTR_TYPES];
};

/**
 * struct mic_irq_info - OS specific irq information
 *
 * @next_avail_src: next available doorbell that can be assigned.
 * @msix_entries: msix entries allocated while setting up MSI-x
 * @mic_msi_map: The MSI/MSI-x mapping information.
 * @num_vectors: The number of MSI/MSI-x vectors that have been allocated.
 * @cb_ida: callback ID allocator to track the callbacks registered.
 * @mic_intr_lock: spinlock to protect the interrupt callback list.
 * @mic_thread_lock: spinlock to protect the thread callback list.
 *		   This lock is used to protect against thread_fn while
 *		   mic_intr_lock is used to protect against interrupt handler.
 * @cb_list: Array of callback lists one for each source.
 * @mask: Mask used by the main thread fn to call the underlying thread fns.
 */
struct mic_irq_info {
	int next_avail_src;
	struct msix_entry *msix_entries;
	u32 *mic_msi_map;
	u16 num_vectors;
	struct ida cb_ida;
	spinlock_t mic_intr_lock;
	spinlock_t mic_thread_lock;
	struct list_head *cb_list;
	unsigned long mask;
};

/**
 * struct mic_intr_cb - Interrupt callback structure.
 *
 * @handler: The callback function
 * @thread_fn: The thread_fn.
 * @data: Private data of the requester.
 * @cb_id: The callback id. Identifies this callback.
 * @list: list head pointing to the next callback structure.
 */
struct mic_intr_cb {
	irq_handler_t handler;
	irq_handler_t thread_fn;
	void *data;
	int cb_id;
	struct list_head list;
};

/**
 * struct mic_irq - opaque pointer used as cookie
 */
struct mic_irq;

/* Forward declaration */
struct mic_device;

/**
 * struct mic_hw_intr_ops: MIC HW specific interrupt operations
 * @intr_init: Initialize H/W specific interrupt information.
 * @enable_interrupts: Enable interrupts from the hardware.
 * @disable_interrupts: Disable interrupts from the hardware.
 * @program_msi_to_src_map: Update MSI mapping registers with
 * irq information.
 * @read_msi_to_src_map: Read MSI mapping registers containing
 * irq information.
 */
struct mic_hw_intr_ops {
	void (*intr_init)(struct mic_device *mdev);
	void (*enable_interrupts)(struct mic_device *mdev);
	void (*disable_interrupts)(struct mic_device *mdev);
	void (*program_msi_to_src_map) (struct mic_device *mdev,
			int idx, int intr_src, bool set);
	u32 (*read_msi_to_src_map) (struct mic_device *mdev,
			int idx);
};

int mic_next_db(struct mic_device *mdev);
struct mic_irq *
mic_request_threaded_irq(struct mic_device *mdev,
			 irq_handler_t handler, irq_handler_t thread_fn,
			 const char *name, void *data, int intr_src,
			 enum mic_intr_type type);
void mic_free_irq(struct mic_device *mdev,
		struct mic_irq *cookie, void *data);
int mic_setup_interrupts(struct mic_device *mdev, struct pci_dev *pdev);
void mic_free_interrupts(struct mic_device *mdev, struct pci_dev *pdev);
void mic_intr_restore(struct mic_device *mdev);
#endif
