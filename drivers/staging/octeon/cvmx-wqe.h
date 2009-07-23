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
 * This header file defines the work queue entry (wqe) data structure.
 * Since this is a commonly used structure that depends on structures
 * from several hardware blocks, those definitions have been placed
 * in this file to create a single point of definition of the wqe
 * format.
 * Data structures are still named according to the block that they
 * relate to.
 *
 */

#ifndef __CVMX_WQE_H__
#define __CVMX_WQE_H__

#include "cvmx-packet.h"


#define OCT_TAG_TYPE_STRING(x)						\
	(((x) == CVMX_POW_TAG_TYPE_ORDERED) ?  "ORDERED" :		\
		(((x) == CVMX_POW_TAG_TYPE_ATOMIC) ?  "ATOMIC" :	\
			(((x) == CVMX_POW_TAG_TYPE_NULL) ?  "NULL" :	\
				"NULL_NULL")))

/**
 * HW decode / err_code in work queue entry
 */
typedef union {
	uint64_t u64;

	/* Use this struct if the hardware determines that the packet is IP */
	struct {
		/* HW sets this to the number of buffers used by this packet */
		uint64_t bufs:8;
		/* HW sets to the number of L2 bytes prior to the IP */
		uint64_t ip_offset:8;
		/* set to 1 if we found DSA/VLAN in the L2 */
		uint64_t vlan_valid:1;
		/* Set to 1 if the DSA/VLAN tag is stacked */
		uint64_t vlan_stacked:1;
		uint64_t unassigned:1;
		/* HW sets to the DSA/VLAN CFI flag (valid when vlan_valid) */
		uint64_t vlan_cfi:1;
		/* HW sets to the DSA/VLAN_ID field (valid when vlan_valid) */
		uint64_t vlan_id:12;
		/* Ring Identifier (if PCIe). Requires PIP_GBL_CTL[RING_EN]=1 */
		uint64_t pr:4;
		uint64_t unassigned2:8;
		/* the packet needs to be decompressed */
		uint64_t dec_ipcomp:1;
		/* the packet is either TCP or UDP */
		uint64_t tcp_or_udp:1;
		/* the packet needs to be decrypted (ESP or AH) */
		uint64_t dec_ipsec:1;
		/* the packet is IPv6 */
		uint64_t is_v6:1;

		/*
		 * (rcv_error, not_IP, IP_exc, is_frag, L4_error,
		 * software, etc.).
		 */

		/*
		 * reserved for software use, hardware will clear on
		 * packet creation.
		 */
		uint64_t software:1;
		/* exceptional conditions below */
		/* the receive interface hardware detected an L4 error
		 * (only applies if !is_frag) (only applies if
		 * !rcv_error && !not_IP && !IP_exc && !is_frag)
		 * failure indicated in err_code below, decode:
		 *
		 * - 1 = Malformed L4
		 * - 2 = L4 Checksum Error: the L4 checksum value is
		 * - 3 = UDP Length Error: The UDP length field would
		 *       make the UDP data longer than what remains in
		 *       the IP packet (as defined by the IP header
		 *       length field).
		 * - 4 = Bad L4 Port: either the source or destination
		 *       TCP/UDP port is 0.
		 * - 8 = TCP FIN Only: the packet is TCP and only the
		 *       FIN flag set.
		 * - 9 = TCP No Flags: the packet is TCP and no flags
		 *       are set.
		 * - 10 = TCP FIN RST: the packet is TCP and both FIN
		 *        and RST are set.
		 * - 11 = TCP SYN URG: the packet is TCP and both SYN
		 *        and URG are set.
		 * - 12 = TCP SYN RST: the packet is TCP and both SYN
		 *        and RST are set.
		 * - 13 = TCP SYN FIN: the packet is TCP and both SYN
		 *        and FIN are set.
		 */
		uint64_t L4_error:1;
		/* set if the packet is a fragment */
		uint64_t is_frag:1;
		/* the receive interface hardware detected an IP error
		 * / exception (only applies if !rcv_error && !not_IP)
		 * failure indicated in err_code below, decode:
		 *
		 * - 1 = Not IP: the IP version field is neither 4 nor
		 *       6.
		 * - 2 = IPv4 Header Checksum Error: the IPv4 header
		 *       has a checksum violation.
		 * - 3 = IP Malformed Header: the packet is not long
		 *       enough to contain the IP header.
		 * - 4 = IP Malformed: the packet is not long enough
		 *	 to contain the bytes indicated by the IP
		 *	 header. Pad is allowed.
		 * - 5 = IP TTL Hop: the IPv4 TTL field or the IPv6
		 *       Hop Count field are zero.
		 * - 6 = IP Options
		 */
		uint64_t IP_exc:1;
		/*
		 * Set if the hardware determined that the packet is a
		 * broadcast.
		 */
		uint64_t is_bcast:1;
		/*
		 * St if the hardware determined that the packet is a
		 * multi-cast.
		 */
		uint64_t is_mcast:1;
		/*
		 * Set if the packet may not be IP (must be zero in
		 * this case).
		 */
		uint64_t not_IP:1;
		/*
		 * The receive interface hardware detected a receive
		 * error (must be zero in this case).
		 */
		uint64_t rcv_error:1;
		/* lower err_code = first-level descriptor of the
		 * work */
		/* zero for packet submitted by hardware that isn't on
		 * the slow path */
		/* type is cvmx_pip_err_t */
		uint64_t err_code:8;
	} s;

	/* use this to get at the 16 vlan bits */
	struct {
		uint64_t unused1:16;
		uint64_t vlan:16;
		uint64_t unused2:32;
	} svlan;

	/*
	 * use this struct if the hardware could not determine that
	 * the packet is ip.
	 */
	struct {
		/*
		 * HW sets this to the number of buffers used by this
		 * packet.
		 */
		uint64_t bufs:8;
		uint64_t unused:8;
		/* set to 1 if we found DSA/VLAN in the L2 */
		uint64_t vlan_valid:1;
		/* Set to 1 if the DSA/VLAN tag is stacked */
		uint64_t vlan_stacked:1;
		uint64_t unassigned:1;
		/*
		 * HW sets to the DSA/VLAN CFI flag (valid when
		 * vlan_valid)
		 */
		uint64_t vlan_cfi:1;
		/*
		 * HW sets to the DSA/VLAN_ID field (valid when
		 * vlan_valid).
		 */
		uint64_t vlan_id:12;
		/*
		 * Ring Identifier (if PCIe). Requires
		 * PIP_GBL_CTL[RING_EN]=1
		 */
		uint64_t pr:4;
		uint64_t unassigned2:12;
		/*
		 * reserved for software use, hardware will clear on
		 * packet creation.
		 */
		uint64_t software:1;
		uint64_t unassigned3:1;
		/*
		 * set if the hardware determined that the packet is
		 * rarp.
		 */
		uint64_t is_rarp:1;
		/*
		 * set if the hardware determined that the packet is
		 * arp
		 */
		uint64_t is_arp:1;
		/*
		 * set if the hardware determined that the packet is a
		 * broadcast.
		 */
		uint64_t is_bcast:1;
		/*
		 * set if the hardware determined that the packet is a
		 * multi-cast
		 */
		uint64_t is_mcast:1;
		/*
		 * set if the packet may not be IP (must be one in
		 * this case)
		 */
		uint64_t not_IP:1;
		/* The receive interface hardware detected a receive
		 * error.  Failure indicated in err_code below,
		 * decode:
		 *
		 * - 1 = partial error: a packet was partially
		 *       received, but internal buffering / bandwidth
		 *       was not adequate to receive the entire
		 *       packet.
		 * - 2 = jabber error: the RGMII packet was too large
		 *       and is truncated.
		 * - 3 = overrun error: the RGMII packet is longer
		 *       than allowed and had an FCS error.
		 * - 4 = oversize error: the RGMII packet is longer
		 *       than allowed.
		 * - 5 = alignment error: the RGMII packet is not an
		 *       integer number of bytes
		 *       and had an FCS error (100M and 10M only).
		 * - 6 = fragment error: the RGMII packet is shorter
		 *       than allowed and had an FCS error.
		 * - 7 = GMX FCS error: the RGMII packet had an FCS
		 *       error.
		 * - 8 = undersize error: the RGMII packet is shorter
		 *       than allowed.
		 * - 9 = extend error: the RGMII packet had an extend
		 *       error.
		 * - 10 = length mismatch error: the RGMII packet had
		 *        a length that did not match the length field
		 *        in the L2 HDR.
		 * - 11 = RGMII RX error/SPI4 DIP4 Error: the RGMII
		 * 	  packet had one or more data reception errors
		 * 	  (RXERR) or the SPI4 packet had one or more
		 * 	  DIP4 errors.
		 * - 12 = RGMII skip error/SPI4 Abort Error: the RGMII
		 *        packet was not large enough to cover the
		 *        skipped bytes or the SPI4 packet was
		 *        terminated with an About EOPS.
		 * - 13 = RGMII nibble error/SPI4 Port NXA Error: the
		 *        RGMII packet had a studder error (data not
		 *        repeated - 10/100M only) or the SPI4 packet
		 *        was sent to an NXA.
		 * - 16 = FCS error: a SPI4.2 packet had an FCS error.
		 * - 17 = Skip error: a packet was not large enough to
		 *        cover the skipped bytes.
		 * - 18 = L2 header malformed: the packet is not long
		 *        enough to contain the L2.
		 */

		uint64_t rcv_error:1;
		/*
		 * lower err_code = first-level descriptor of the
		 * work
		 */
		/*
		 * zero for packet submitted by hardware that isn't on
		 * the slow path
		 */
		/* type is cvmx_pip_err_t (union, so can't use directly */
		uint64_t err_code:8;
	} snoip;

} cvmx_pip_wqe_word2;

/**
 * Work queue entry format
 *
 * must be 8-byte aligned
 */
typedef struct {

    /*****************************************************************
     * WORD 0
     *  HW WRITE: the following 64 bits are filled by HW when a packet arrives
     */

    /**
     * raw chksum result generated by the HW
     */
	uint16_t hw_chksum;
    /**
     * Field unused by hardware - available for software
     */
	uint8_t unused;
    /**
     * Next pointer used by hardware for list maintenance.
     * May be written/read by HW before the work queue
     *           entry is scheduled to a PP
     * (Only 36 bits used in Octeon 1)
     */
	uint64_t next_ptr:40;

    /*****************************************************************
     * WORD 1
     *  HW WRITE: the following 64 bits are filled by HW when a packet arrives
     */

    /**
     * HW sets to the total number of bytes in the packet
     */
	uint64_t len:16;
    /**
     * HW sets this to input physical port
     */
	uint64_t ipprt:6;

    /**
     * HW sets this to what it thought the priority of the input packet was
     */
	uint64_t qos:3;

    /**
     * the group that the work queue entry will be scheduled to
     */
	uint64_t grp:4;
    /**
     * the type of the tag (ORDERED, ATOMIC, NULL)
     */
	uint64_t tag_type:3;
    /**
     * the synchronization/ordering tag
     */
	uint64_t tag:32;

    /**
     * WORD 2 HW WRITE: the following 64-bits are filled in by
     *   hardware when a packet arrives This indicates a variety of
     *   status and error conditions.
     */
	cvmx_pip_wqe_word2 word2;

    /**
     * Pointer to the first segment of the packet.
     */
	union cvmx_buf_ptr packet_ptr;

    /**
     *   HW WRITE: octeon will fill in a programmable amount from the
     *             packet, up to (at most, but perhaps less) the amount
     *             needed to fill the work queue entry to 128 bytes
     *
     *   If the packet is recognized to be IP, the hardware starts
     *   (except that the IPv4 header is padded for appropriate
     *   alignment) writing here where the IP header starts.  If the
     *   packet is not recognized to be IP, the hardware starts
     *   writing the beginning of the packet here.
     */
	uint8_t packet_data[96];

    /**
     * If desired, SW can make the work Q entry any length. For the
     * purposes of discussion here, Assume 128B always, as this is all that
     * the hardware deals with.
     *
     */

} CVMX_CACHE_LINE_ALIGNED cvmx_wqe_t;

#endif /* __CVMX_WQE_H__ */
