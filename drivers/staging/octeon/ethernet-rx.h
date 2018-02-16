/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 */

#include <asm/octeon/cvmx-fau.h>

void cvm_oct_poll_controller(struct net_device *dev);
void cvm_oct_rx_initialize(void);
void cvm_oct_rx_shutdown(void);

static inline void cvm_oct_rx_refill_pool(int fill_threshold)
{
	int number_to_free;
	int num_freed;
	/* Refill the packet buffer pool */
	number_to_free =
		cvmx_fau_fetch_and_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);

	if (number_to_free > fill_threshold) {
		cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
				      -number_to_free);
		num_freed = cvm_oct_mem_fill_fpa(CVMX_FPA_PACKET_POOL,
						 CVMX_FPA_PACKET_POOL_SIZE,
						 number_to_free);
		if (num_freed != number_to_free) {
			cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
					      number_to_free - num_freed);
		}
	}
}
