/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
/*! \file octeon_main.h
 *  \brief Host Driver: This file is included by all host driver source files
 *  to include common definitions.
 */

#ifndef _OCTEON_MAIN_H_
#define  _OCTEON_MAIN_H_

#include <linux/sched/signal.h>

#if BITS_PER_LONG == 32
#define CVM_CAST64(v) ((long long)(v))
#elif BITS_PER_LONG == 64
#define CVM_CAST64(v) ((long long)(long)(v))
#else
#error "Unknown system architecture"
#endif

#define DRV_NAME "LiquidIO"

/** This structure is used by NIC driver to store information required
 * to free the sk_buff when the packet has been fetched by Octeon.
 * Bytes offset below assume worst-case of a 64-bit system.
 */
struct octnet_buf_free_info {
	/** Bytes 1-8.  Pointer to network device private structure. */
	struct lio *lio;

	/** Bytes 9-16.  Pointer to sk_buff. */
	struct sk_buff *skb;

	/** Bytes 17-24.  Pointer to gather list. */
	struct octnic_gather *g;

	/** Bytes 25-32. Physical address of skb->data or gather list. */
	u64 dptr;

	/** Bytes 33-47. Piggybacked soft command, if any */
	struct octeon_soft_command *sc;
};

/* BQL-related functions */
void octeon_report_sent_bytes_to_bql(void *buf, int reqtype);
void octeon_update_tx_completion_counters(void *buf, int reqtype,
					  unsigned int *pkts_compl,
					  unsigned int *bytes_compl);
void octeon_report_tx_completion_to_bql(void *txq, unsigned int pkts_compl,
					unsigned int bytes_compl);
void octeon_pf_changed_vf_macaddr(struct octeon_device *oct, u8 *mac);
/** Swap 8B blocks */
static inline void octeon_swap_8B_data(u64 *data, u32 blocks)
{
	while (blocks) {
		cpu_to_be64s(data);
		blocks--;
		data++;
	}
}

/**
 * \brief unmaps a PCI BAR
 * @param oct Pointer to Octeon device
 * @param baridx bar index
 */
static inline void octeon_unmap_pci_barx(struct octeon_device *oct, int baridx)
{
	dev_dbg(&oct->pci_dev->dev, "Freeing PCI mapped regions for Bar%d\n",
		baridx);

	if (oct->mmio[baridx].done)
		iounmap(oct->mmio[baridx].hw_addr);

	if (oct->mmio[baridx].start)
		pci_release_region(oct->pci_dev, baridx * 2);
}

/**
 * \brief maps a PCI BAR
 * @param oct Pointer to Octeon device
 * @param baridx bar index
 * @param max_map_len maximum length of mapped memory
 */
static inline int octeon_map_pci_barx(struct octeon_device *oct,
				      int baridx, int max_map_len)
{
	u32 mapped_len = 0;

	if (pci_request_region(oct->pci_dev, baridx * 2, DRV_NAME)) {
		dev_err(&oct->pci_dev->dev, "pci_request_region failed for bar %d\n",
			baridx);
		return 1;
	}

	oct->mmio[baridx].start = pci_resource_start(oct->pci_dev, baridx * 2);
	oct->mmio[baridx].len = pci_resource_len(oct->pci_dev, baridx * 2);

	mapped_len = oct->mmio[baridx].len;
	if (!mapped_len)
		goto err_release_region;

	if (max_map_len && (mapped_len > max_map_len))
		mapped_len = max_map_len;

	oct->mmio[baridx].hw_addr =
		ioremap(oct->mmio[baridx].start, mapped_len);
	oct->mmio[baridx].mapped_len = mapped_len;

	dev_dbg(&oct->pci_dev->dev, "BAR%d start: 0x%llx mapped %u of %u bytes\n",
		baridx, oct->mmio[baridx].start, mapped_len,
		oct->mmio[baridx].len);

	if (!oct->mmio[baridx].hw_addr) {
		dev_err(&oct->pci_dev->dev, "error ioremap for bar %d\n",
			baridx);
		goto err_release_region;
	}
	oct->mmio[baridx].done = 1;

	return 0;

err_release_region:
	pci_release_region(oct->pci_dev, baridx * 2);
	return 1;
}

static inline int
sleep_cond(wait_queue_head_t *wait_queue, int *condition)
{
	int errno = 0;
	wait_queue_entry_t we;

	init_waitqueue_entry(&we, current);
	add_wait_queue(wait_queue, &we);
	while (!(READ_ONCE(*condition))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			errno = -EINTR;
			goto out;
		}
		schedule();
	}
out:
	set_current_state(TASK_RUNNING);
	remove_wait_queue(wait_queue, &we);
	return errno;
}

/* Gives up the CPU for a timeout period.
 * Check that the condition is not true before we go to sleep for a
 * timeout period.
 */
static inline void
sleep_timeout_cond(wait_queue_head_t *wait_queue,
		   int *condition,
		   int timeout)
{
	wait_queue_entry_t we;

	init_waitqueue_entry(&we, current);
	add_wait_queue(wait_queue, &we);
	set_current_state(TASK_INTERRUPTIBLE);
	if (!(*condition))
		schedule_timeout(timeout);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(wait_queue, &we);
}

#ifndef ROUNDUP4
#define ROUNDUP4(val) (((val) + 3) & 0xfffffffc)
#endif

#ifndef ROUNDUP8
#define ROUNDUP8(val) (((val) + 7) & 0xfffffff8)
#endif

#ifndef ROUNDUP16
#define ROUNDUP16(val) (((val) + 15) & 0xfffffff0)
#endif

#ifndef ROUNDUP128
#define ROUNDUP128(val) (((val) + 127) & 0xffffff80)
#endif

#endif /* _OCTEON_MAIN_H_ */
