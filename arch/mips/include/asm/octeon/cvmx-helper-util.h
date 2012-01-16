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

/*
 *
 * Small helper utilities.
 *
 */

#ifndef __CVMX_HELPER_UTIL_H__
#define __CVMX_HELPER_UTIL_H__

/**
 * Convert a interface mode into a human readable string
 *
 * @mode:   Mode to convert
 *
 * Returns String
 */
extern const char
    *cvmx_helper_interface_mode_to_string(cvmx_helper_interface_mode_t mode);

/**
 * Debug routine to dump the packet structure to the console
 *
 * @work:   Work queue entry containing the packet to dump
 * Returns
 */
extern int cvmx_helper_dump_packet(cvmx_wqe_t *work);

/**
 * Setup Random Early Drop on a specific input queue
 *
 * @queue:  Input queue to setup RED on (0-7)
 * @pass_thresh:
 *               Packets will begin slowly dropping when there are less than
 *               this many packet buffers free in FPA 0.
 * @drop_thresh:
 *               All incomming packets will be dropped when there are less
 *               than this many free packet buffers in FPA 0.
 * Returns Zero on success. Negative on failure
 */
extern int cvmx_helper_setup_red_queue(int queue, int pass_thresh,
				       int drop_thresh);

/**
 * Setup Random Early Drop to automatically begin dropping packets.
 *
 * @pass_thresh:
 *               Packets will begin slowly dropping when there are less than
 *               this many packet buffers free in FPA 0.
 * @drop_thresh:
 *               All incomming packets will be dropped when there are less
 *               than this many free packet buffers in FPA 0.
 * Returns Zero on success. Negative on failure
 */
extern int cvmx_helper_setup_red(int pass_thresh, int drop_thresh);

/**
 * Get the version of the CVMX libraries.
 *
 * Returns Version string. Note this buffer is allocated statically
 *         and will be shared by all callers.
 */
extern const char *cvmx_helper_get_version(void);

/**
 * Setup the common GMX settings that determine the number of
 * ports. These setting apply to almost all configurations of all
 * chips.
 *
 * @interface: Interface to configure
 * @num_ports: Number of ports on the interface
 *
 * Returns Zero on success, negative on failure
 */
extern int __cvmx_helper_setup_gmx(int interface, int num_ports);

/**
 * Returns the IPD/PKO port number for a port on the given
 * interface.
 *
 * @interface: Interface to use
 * @port:      Port on the interface
 *
 * Returns IPD/PKO port number
 */
extern int cvmx_helper_get_ipd_port(int interface, int port);

/**
 * Returns the IPD/PKO port number for the first port on the given
 * interface.
 *
 * @interface: Interface to use
 *
 * Returns IPD/PKO port number
 */
static inline int cvmx_helper_get_first_ipd_port(int interface)
{
	return cvmx_helper_get_ipd_port(interface, 0);
}

/**
 * Returns the IPD/PKO port number for the last port on the given
 * interface.
 *
 * @interface: Interface to use
 *
 * Returns IPD/PKO port number
 */
static inline int cvmx_helper_get_last_ipd_port(int interface)
{
	extern int cvmx_helper_ports_on_interface(int interface);

	return cvmx_helper_get_first_ipd_port(interface) +
	       cvmx_helper_ports_on_interface(interface) - 1;
}

/**
 * Free the packet buffers contained in a work queue entry.
 * The work queue entry is not freed.
 *
 * @work:   Work queue entry with packet to free
 */
static inline void cvmx_helper_free_packet_data(cvmx_wqe_t *work)
{
	uint64_t number_buffers;
	union cvmx_buf_ptr buffer_ptr;
	union cvmx_buf_ptr next_buffer_ptr;
	uint64_t start_of_buffer;

	number_buffers = work->word2.s.bufs;
	if (number_buffers == 0)
		return;
	buffer_ptr = work->packet_ptr;

	/*
	 * Since the number of buffers is not zero, we know this is
	 * not a dynamic short packet. We need to check if it is a
	 * packet received with IPD_CTL_STATUS[NO_WPTR]. If this is
	 * true, we need to free all buffers except for the first
	 * one. The caller doesn't expect their WQE pointer to be
	 * freed
	 */
	start_of_buffer = ((buffer_ptr.s.addr >> 7) - buffer_ptr.s.back) << 7;
	if (cvmx_ptr_to_phys(work) == start_of_buffer) {
		next_buffer_ptr =
		    *(union cvmx_buf_ptr *) cvmx_phys_to_ptr(buffer_ptr.s.addr - 8);
		buffer_ptr = next_buffer_ptr;
		number_buffers--;
	}

	while (number_buffers--) {
		/*
		 * Remember the back pointer is in cache lines, not
		 * 64bit words
		 */
		start_of_buffer =
		    ((buffer_ptr.s.addr >> 7) - buffer_ptr.s.back) << 7;
		/*
		 * Read pointer to next buffer before we free the
		 * current buffer.
		 */
		next_buffer_ptr =
		    *(union cvmx_buf_ptr *) cvmx_phys_to_ptr(buffer_ptr.s.addr - 8);
		cvmx_fpa_free(cvmx_phys_to_ptr(start_of_buffer),
			      buffer_ptr.s.pool, 0);
		buffer_ptr = next_buffer_ptr;
	}
}

/**
 * Returns the interface number for an IPD/PKO port number.
 *
 * @ipd_port: IPD/PKO port number
 *
 * Returns Interface number
 */
extern int cvmx_helper_get_interface_num(int ipd_port);

/**
 * Returns the interface index number for an IPD/PKO port
 * number.
 *
 * @ipd_port: IPD/PKO port number
 *
 * Returns Interface index number
 */
extern int cvmx_helper_get_interface_index_num(int ipd_port);

#endif /* __CVMX_HELPER_H__ */
