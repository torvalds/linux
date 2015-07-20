/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

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
	if (ipd_port < 32)	/* Interface 0 or 1 for RGMII,GMII,SPI, etc */
		return ipd_port >> 4;
	else if (ipd_port < 36)	/* Interface 2 for NPI */
		return 2;
	else if (ipd_port < 40)	/* Interface 3 for loopback */
		return 3;
	else if (ipd_port == 40)	/* Non existent interface for POW0 */
		return 4;
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
	if (ipd_port < 32)
		return ipd_port & 15;
	return ipd_port & 3;
}
