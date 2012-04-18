/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
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
 ***********************license end**************************************/

/**
 * @file
 *
 * Helper functions for FPA setup.
 *
 */
#include "executive-config.h"
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-bootmem.h"
#include "cvmx-fpa.h"
#include "cvmx-helper-fpa.h"

/**
 * Allocate memory for and initialize a single FPA pool.
 *
 * @pool:    Pool to initialize
 * @buffer_size:  Size of buffers to allocate in bytes
 * @buffers: Number of buffers to put in the pool. Zero is allowed
 * @name:    String name of the pool for debugging purposes
 * Returns Zero on success, non-zero on failure
 */
static int __cvmx_helper_initialize_fpa_pool(int pool, uint64_t buffer_size,
					     uint64_t buffers, const char *name)
{
	uint64_t current_num;
	void *memory;
	uint64_t align = CVMX_CACHE_LINE_SIZE;

	/*
	 * Align the allocation so that power of 2 size buffers are
	 * naturally aligned.
	 */
	while (align < buffer_size)
		align = align << 1;

	if (buffers == 0)
		return 0;

	current_num = cvmx_read_csr(CVMX_FPA_QUEX_AVAILABLE(pool));
	if (current_num) {
		cvmx_dprintf("Fpa pool %d(%s) already has %llu buffers. "
			     "Skipping setup.\n",
		     pool, name, (unsigned long long)current_num);
		return 0;
	}

	memory = cvmx_bootmem_alloc(buffer_size * buffers, align);
	if (memory == NULL) {
		cvmx_dprintf("Out of memory initializing fpa pool %d(%s).\n",
			     pool, name);
		return -1;
	}
	cvmx_fpa_setup_pool(pool, name, memory, buffer_size, buffers);
	return 0;
}

/**
 * Allocate memory and initialize the FPA pools using memory
 * from cvmx-bootmem. Specifying zero for the number of
 * buffers will cause that FPA pool to not be setup. This is
 * useful if you aren't using some of the hardware and want
 * to save memory. Use cvmx_helper_initialize_fpa instead of
 * this function directly.
 *
 * @pip_pool: Should always be CVMX_FPA_PACKET_POOL
 * @pip_size: Should always be CVMX_FPA_PACKET_POOL_SIZE
 * @pip_buffers:
 *                 Number of packet buffers.
 * @wqe_pool: Should always be CVMX_FPA_WQE_POOL
 * @wqe_size: Should always be CVMX_FPA_WQE_POOL_SIZE
 * @wqe_entries:
 *                 Number of work queue entries
 * @pko_pool: Should always be CVMX_FPA_OUTPUT_BUFFER_POOL
 * @pko_size: Should always be CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE
 * @pko_buffers:
 *                 PKO Command buffers. You should at minimum have two per
 *                 each PKO queue.
 * @tim_pool: Should always be CVMX_FPA_TIMER_POOL
 * @tim_size: Should always be CVMX_FPA_TIMER_POOL_SIZE
 * @tim_buffers:
 *                 TIM ring buffer command queues. At least two per timer bucket
 *                 is recommened.
 * @dfa_pool: Should always be CVMX_FPA_DFA_POOL
 * @dfa_size: Should always be CVMX_FPA_DFA_POOL_SIZE
 * @dfa_buffers:
 *                 DFA command buffer. A relatively small (32 for example)
 *                 number should work.
 * Returns Zero on success, non-zero if out of memory
 */
static int __cvmx_helper_initialize_fpa(int pip_pool, int pip_size,
					int pip_buffers, int wqe_pool,
					int wqe_size, int wqe_entries,
					int pko_pool, int pko_size,
					int pko_buffers, int tim_pool,
					int tim_size, int tim_buffers,
					int dfa_pool, int dfa_size,
					int dfa_buffers)
{
	int status;

	cvmx_fpa_enable();

	if ((pip_buffers > 0) && (pip_buffers <= 64))
		cvmx_dprintf
		    ("Warning: %d packet buffers may not be enough for hardware"
		     " prefetch. 65 or more is recommended.\n", pip_buffers);

	if (pip_pool >= 0) {
		status =
		    __cvmx_helper_initialize_fpa_pool(pip_pool, pip_size,
						      pip_buffers,
						      "Packet Buffers");
		if (status)
			return status;
	}

	if (wqe_pool >= 0) {
		status =
		    __cvmx_helper_initialize_fpa_pool(wqe_pool, wqe_size,
						      wqe_entries,
						      "Work Queue Entries");
		if (status)
			return status;
	}

	if (pko_pool >= 0) {
		status =
		    __cvmx_helper_initialize_fpa_pool(pko_pool, pko_size,
						      pko_buffers,
						      "PKO Command Buffers");
		if (status)
			return status;
	}

	if (tim_pool >= 0) {
		status =
		    __cvmx_helper_initialize_fpa_pool(tim_pool, tim_size,
						      tim_buffers,
						      "TIM Command Buffers");
		if (status)
			return status;
	}

	if (dfa_pool >= 0) {
		status =
		    __cvmx_helper_initialize_fpa_pool(dfa_pool, dfa_size,
						      dfa_buffers,
						      "DFA Command Buffers");
		if (status)
			return status;
	}

	return 0;
}

/**
 * Allocate memory and initialize the FPA pools using memory
 * from cvmx-bootmem. Sizes of each element in the pools is
 * controlled by the cvmx-config.h header file. Specifying
 * zero for any parameter will cause that FPA pool to not be
 * setup. This is useful if you aren't using some of the
 * hardware and want to save memory.
 *
 * @packet_buffers:
 *               Number of packet buffers to allocate
 * @work_queue_entries:
 *               Number of work queue entries
 * @pko_buffers:
 *               PKO Command buffers. You should at minimum have two per
 *               each PKO queue.
 * @tim_buffers:
 *               TIM ring buffer command queues. At least two per timer bucket
 *               is recommened.
 * @dfa_buffers:
 *               DFA command buffer. A relatively small (32 for example)
 *               number should work.
 * Returns Zero on success, non-zero if out of memory
 */
int cvmx_helper_initialize_fpa(int packet_buffers, int work_queue_entries,
			       int pko_buffers, int tim_buffers,
			       int dfa_buffers)
{
#ifndef CVMX_FPA_PACKET_POOL
#define CVMX_FPA_PACKET_POOL -1
#define CVMX_FPA_PACKET_POOL_SIZE 0
#endif
#ifndef CVMX_FPA_WQE_POOL
#define CVMX_FPA_WQE_POOL -1
#define CVMX_FPA_WQE_POOL_SIZE 0
#endif
#ifndef CVMX_FPA_OUTPUT_BUFFER_POOL
#define CVMX_FPA_OUTPUT_BUFFER_POOL -1
#define CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE 0
#endif
#ifndef CVMX_FPA_TIMER_POOL
#define CVMX_FPA_TIMER_POOL -1
#define CVMX_FPA_TIMER_POOL_SIZE 0
#endif
#ifndef CVMX_FPA_DFA_POOL
#define CVMX_FPA_DFA_POOL -1
#define CVMX_FPA_DFA_POOL_SIZE 0
#endif
	return __cvmx_helper_initialize_fpa(CVMX_FPA_PACKET_POOL,
					    CVMX_FPA_PACKET_POOL_SIZE,
					    packet_buffers, CVMX_FPA_WQE_POOL,
					    CVMX_FPA_WQE_POOL_SIZE,
					    work_queue_entries,
					    CVMX_FPA_OUTPUT_BUFFER_POOL,
					    CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE,
					    pko_buffers, CVMX_FPA_TIMER_POOL,
					    CVMX_FPA_TIMER_POOL_SIZE,
					    tim_buffers, CVMX_FPA_DFA_POOL,
					    CVMX_FPA_DFA_POOL_SIZE,
					    dfa_buffers);
}
