/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <asm/octeon/cvmx-pip.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-util.h>

/**
 * cvm_oct_get_buffer_ptr - convert packet data address to pointer
 * @packet_ptr: Packet data hardware address
 *
 * Returns Packet buffer pointer
 */
static inline void *cvm_oct_get_buffer_ptr(union cvmx_buf_ptr packet_ptr)
{
	return cvmx_phys_to_ptr(((packet_ptr.s.addr >> 7) - packet_ptr.s.back)
				<< 7);
}

/**
 * INTERFACE - convert IPD port to logical interface
 * @ipd_port: Port to check
 *
 * Returns Logical interface
 */
static inline int INTERFACE(int ipd_port)
{
	int interface;

	if (ipd_port == CVMX_PIP_NUM_INPUT_PORTS)
		return 10;
	interface = cvmx_helper_get_interface_num(ipd_port);
	if (interface >= 0)
		return interface;
	panic("Illegal ipd_port %d passed to INTERFACE\n", ipd_port);
}

/**
 * INDEX - convert IPD/PKO port number to the port's interface index
 * @ipd_port: Port to check
 *
 * Returns Index into interface port list
 */
static inline int INDEX(int ipd_port)
{
	return cvmx_helper_get_interface_index_num(ipd_port);
}
