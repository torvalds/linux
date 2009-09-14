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
 *
 * Packet buffer defines.
 */

#ifndef __CVMX_PACKET_H__
#define __CVMX_PACKET_H__

/**
 * This structure defines a buffer pointer on Octeon
 */
union cvmx_buf_ptr {
	void *ptr;
	uint64_t u64;
	struct {
		/*
		 * if set, invert the "free" pick of the overall
		 * packet. HW always sets this bit to 0 on inbound
		 * packet
		 */
		uint64_t i:1;
		/*
		 * Indicates the amount to back up to get to the
		 * buffer start in cache lines. In most cases this is
		 * less than one complete cache line, so the value is
		 * zero.
		 */
		uint64_t back:4;
		/* The pool that the buffer came from / goes to */
		uint64_t pool:3;
		/* The size of the segment pointed to by addr (in bytes) */
		uint64_t size:16;
		/* Pointer to the first byte of the data, NOT buffer */
		uint64_t addr:40;
	} s;
};

#endif /*  __CVMX_PACKET_H__ */
