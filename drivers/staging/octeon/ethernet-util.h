/**********************************************************************
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
	else
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
	else
		return ipd_port & 3;
}
