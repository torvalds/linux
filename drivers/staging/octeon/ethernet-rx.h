/*********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
*********************************************************************/
#include "cvmx-fau.h"

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
