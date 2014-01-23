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
 * Interface to the hardware Packet Input Processing unit.
 *
 */

#ifndef __CVMX_PIP_H__
#define __CVMX_PIP_H__

#include <asm/octeon/cvmx-wqe.h>
#include <asm/octeon/cvmx-fpa.h>
#include <asm/octeon/cvmx-pip-defs.h>

#define CVMX_PIP_NUM_INPUT_PORTS		40
#define CVMX_PIP_NUM_WATCHERS			4

/*
 * Encodes the different error and exception codes
 */
typedef enum {
	CVMX_PIP_L4_NO_ERR = 0ull,
	/*
	 * 1 = TCP (UDP) packet not long enough to cover TCP (UDP)
	 * header
	 */
	CVMX_PIP_L4_MAL_ERR = 1ull,
	/* 2  = TCP/UDP checksum failure */
	CVMX_PIP_CHK_ERR = 2ull,
	/*
	 * 3 = TCP/UDP length check (TCP/UDP length does not match IP
	 * length).
	 */
	CVMX_PIP_L4_LENGTH_ERR = 3ull,
	/* 4  = illegal TCP/UDP port (either source or dest port is zero) */
	CVMX_PIP_BAD_PRT_ERR = 4ull,
	/* 8  = TCP flags = FIN only */
	CVMX_PIP_TCP_FLG8_ERR = 8ull,
	/* 9  = TCP flags = 0 */
	CVMX_PIP_TCP_FLG9_ERR = 9ull,
	/* 10 = TCP flags = FIN+RST+* */
	CVMX_PIP_TCP_FLG10_ERR = 10ull,
	/* 11 = TCP flags = SYN+URG+* */
	CVMX_PIP_TCP_FLG11_ERR = 11ull,
	/* 12 = TCP flags = SYN+RST+* */
	CVMX_PIP_TCP_FLG12_ERR = 12ull,
	/* 13 = TCP flags = SYN+FIN+* */
	CVMX_PIP_TCP_FLG13_ERR = 13ull
} cvmx_pip_l4_err_t;

typedef enum {

	CVMX_PIP_IP_NO_ERR = 0ull,
	/* 1 = not IPv4 or IPv6 */
	CVMX_PIP_NOT_IP = 1ull,
	/* 2 = IPv4 header checksum violation */
	CVMX_PIP_IPV4_HDR_CHK = 2ull,
	/* 3 = malformed (packet not long enough to cover IP hdr) */
	CVMX_PIP_IP_MAL_HDR = 3ull,
	/* 4 = malformed (packet not long enough to cover len in IP hdr) */
	CVMX_PIP_IP_MAL_PKT = 4ull,
	/* 5 = TTL / hop count equal zero */
	CVMX_PIP_TTL_HOP = 5ull,
	/* 6 = IPv4 options / IPv6 early extension headers */
	CVMX_PIP_OPTS = 6ull
} cvmx_pip_ip_exc_t;

/**
 * NOTES
 *	 late collision (data received before collision)
 *	      late collisions cannot be detected by the receiver
 *	      they would appear as JAM bits which would appear as bad FCS
 *	      or carrier extend error which is CVMX_PIP_EXTEND_ERR
 */
typedef enum {
	/* No error */
	CVMX_PIP_RX_NO_ERR = 0ull,
	/* RGM+SPI 1 = partially received packet (buffering/bandwidth
	 * not adequate) */
	CVMX_PIP_PARTIAL_ERR = 1ull,
	/* RGM+SPI 2 = receive packet too large and truncated */
	CVMX_PIP_JABBER_ERR = 2ull,
	/*
	 * RGM 3 = max frame error (pkt len > max frame len) (with FCS
	 * error)
	 */
	CVMX_PIP_OVER_FCS_ERR = 3ull,
	/* RGM+SPI 4 = max frame error (pkt len > max frame len) */
	CVMX_PIP_OVER_ERR = 4ull,
	/*
	 * RGM 5 = nibble error (data not byte multiple - 100M and 10M
	 * only)
	 */
	CVMX_PIP_ALIGN_ERR = 5ull,
	/*
	 * RGM 6 = min frame error (pkt len < min frame len) (with FCS
	 * error)
	 */
	CVMX_PIP_UNDER_FCS_ERR = 6ull,
	/* RGM	   7 = FCS error */
	CVMX_PIP_GMX_FCS_ERR = 7ull,
	/* RGM+SPI 8 = min frame error (pkt len < min frame len) */
	CVMX_PIP_UNDER_ERR = 8ull,
	/* RGM	   9 = Frame carrier extend error */
	CVMX_PIP_EXTEND_ERR = 9ull,
	/*
	 * RGM 10 = length mismatch (len did not match len in L2
	 * length/type)
	 */
	CVMX_PIP_LENGTH_ERR = 10ull,
	/* RGM 11 = Frame error (some or all data bits marked err) */
	CVMX_PIP_DAT_ERR = 11ull,
	/*     SPI 11 = DIP4 error */
	CVMX_PIP_DIP_ERR = 11ull,
	/*
	 * RGM 12 = packet was not large enough to pass the skipper -
	 * no inspection could occur.
	 */
	CVMX_PIP_SKIP_ERR = 12ull,
	/*
	 * RGM 13 = studder error (data not repeated - 100M and 10M
	 * only)
	 */
	CVMX_PIP_NIBBLE_ERR = 13ull,
	/* RGM+SPI 16 = FCS error */
	CVMX_PIP_PIP_FCS = 16L,
	/*
	 * RGM+SPI+PCI 17 = packet was not large enough to pass the
	 * skipper - no inspection could occur.
	 */
	CVMX_PIP_PIP_SKIP_ERR = 17L,
	/*
	 * RGM+SPI+PCI 18 = malformed l2 (packet not long enough to
	 * cover L2 hdr).
	 */
	CVMX_PIP_PIP_L2_MAL_HDR = 18L
	/*
	 * NOTES: xx = late collision (data received before collision)
	 *	 late collisions cannot be detected by the receiver
	 *	 they would appear as JAM bits which would appear as
	 *	 bad FCS or carrier extend error which is
	 *	 CVMX_PIP_EXTEND_ERR
	 */
} cvmx_pip_rcv_err_t;

/**
 * This defines the err_code field errors in the work Q entry
 */
typedef union {
	cvmx_pip_l4_err_t l4_err;
	cvmx_pip_ip_exc_t ip_exc;
	cvmx_pip_rcv_err_t rcv_err;
} cvmx_pip_err_t;

/**
 * Status statistics for a port
 */
typedef struct {
	/* Inbound octets marked to be dropped by the IPD */
	uint32_t dropped_octets;
	/* Inbound packets marked to be dropped by the IPD */
	uint32_t dropped_packets;
	/* RAW PCI Packets received by PIP per port */
	uint32_t pci_raw_packets;
	/* Number of octets processed by PIP */
	uint32_t octets;
	/* Number of packets processed by PIP */
	uint32_t packets;
	/*
	 * Number of identified L2 multicast packets.	Does not
	 * include broadcast packets.  Only includes packets whose
	 * parse mode is SKIP_TO_L2
	 */
	uint32_t multicast_packets;
	/*
	 * Number of identified L2 broadcast packets.	Does not
	 * include multicast packets.  Only includes packets whose
	 * parse mode is SKIP_TO_L2
	 */
	uint32_t broadcast_packets;
	/* Number of 64B packets */
	uint32_t len_64_packets;
	/* Number of 65-127B packets */
	uint32_t len_65_127_packets;
	/* Number of 128-255B packets */
	uint32_t len_128_255_packets;
	/* Number of 256-511B packets */
	uint32_t len_256_511_packets;
	/* Number of 512-1023B packets */
	uint32_t len_512_1023_packets;
	/* Number of 1024-1518B packets */
	uint32_t len_1024_1518_packets;
	/* Number of 1519-max packets */
	uint32_t len_1519_max_packets;
	/* Number of packets with FCS or Align opcode errors */
	uint32_t fcs_align_err_packets;
	/* Number of packets with length < min */
	uint32_t runt_packets;
	/* Number of packets with length < min and FCS error */
	uint32_t runt_crc_packets;
	/* Number of packets with length > max */
	uint32_t oversize_packets;
	/* Number of packets with length > max and FCS error */
	uint32_t oversize_crc_packets;
	/* Number of packets without GMX/SPX/PCI errors received by PIP */
	uint32_t inb_packets;
	/*
	 * Total number of octets from all packets received by PIP,
	 * including CRC
	 */
	uint64_t inb_octets;
	/* Number of packets with GMX/SPX/PCI errors received by PIP */
	uint16_t inb_errors;
} cvmx_pip_port_status_t;

/**
 * Definition of the PIP custom header that can be prepended
 * to a packet by external hardware.
 */
typedef union {
	uint64_t u64;
	struct {
		/*
		 * Documented as R - Set if the Packet is RAWFULL. If
		 * set, this header must be the full 8 bytes.
		 */
		uint64_t rawfull:1;
		/* Must be zero */
		uint64_t reserved0:5;
		/* PIP parse mode for this packet */
		uint64_t parse_mode:2;
		/* Must be zero */
		uint64_t reserved1:1;
		/*
		 * Skip amount, including this header, to the
		 * beginning of the packet
		 */
		uint64_t skip_len:7;
		/* Must be zero */
		uint64_t reserved2:6;
		/* POW input queue for this packet */
		uint64_t qos:3;
		/* POW input group for this packet */
		uint64_t grp:4;
		/*
		 * Flag to store this packet in the work queue entry,
		 * if possible
		 */
		uint64_t rs:1;
		/* POW input tag type */
		uint64_t tag_type:2;
		/* POW input tag */
		uint64_t tag:32;
	} s;
} cvmx_pip_pkt_inst_hdr_t;

/* CSR typedefs have been moved to cvmx-csr-*.h */

/**
 * Configure an ethernet input port
 *
 * @port_num: Port number to configure
 * @port_cfg: Port hardware configuration
 * @port_tag_cfg:
 *		   Port POW tagging configuration
 */
static inline void cvmx_pip_config_port(uint64_t port_num,
					union cvmx_pip_prt_cfgx port_cfg,
					union cvmx_pip_prt_tagx port_tag_cfg)
{
	cvmx_write_csr(CVMX_PIP_PRT_CFGX(port_num), port_cfg.u64);
	cvmx_write_csr(CVMX_PIP_PRT_TAGX(port_num), port_tag_cfg.u64);
}
#if 0
/**
 * @deprecated	    This function is a thin wrapper around the Pass1 version
 *		    of the CVMX_PIP_QOS_WATCHX CSR; Pass2 has added a field for
 *		    setting the group that is incompatible with this function,
 *		    the preferred upgrade path is to use the CSR directly.
 *
 * Configure the global QoS packet watchers. Each watcher is
 * capable of matching a field in a packet to determine the
 * QoS queue for scheduling.
 *
 * @watcher:	Watcher number to configure (0 - 3).
 * @match_type: Watcher match type
 * @match_value:
 *		     Value the watcher will match against
 * @qos:	QoS queue for packets matching this watcher
 */
static inline void cvmx_pip_config_watcher(uint64_t watcher,
					   cvmx_pip_qos_watch_types match_type,
					   uint64_t match_value, uint64_t qos)
{
	cvmx_pip_port_watcher_cfg_t watcher_config;

	watcher_config.u64 = 0;
	watcher_config.s.match_type = match_type;
	watcher_config.s.match_value = match_value;
	watcher_config.s.qos = qos;

	cvmx_write_csr(CVMX_PIP_QOS_WATCHX(watcher), watcher_config.u64);
}
#endif
/**
 * Configure the VLAN priority to QoS queue mapping.
 *
 * @vlan_priority:
 *		 VLAN priority (0-7)
 * @qos:    QoS queue for packets matching this watcher
 */
static inline void cvmx_pip_config_vlan_qos(uint64_t vlan_priority,
					    uint64_t qos)
{
	union cvmx_pip_qos_vlanx pip_qos_vlanx;
	pip_qos_vlanx.u64 = 0;
	pip_qos_vlanx.s.qos = qos;
	cvmx_write_csr(CVMX_PIP_QOS_VLANX(vlan_priority), pip_qos_vlanx.u64);
}

/**
 * Configure the Diffserv to QoS queue mapping.
 *
 * @diffserv: Diffserv field value (0-63)
 * @qos:      QoS queue for packets matching this watcher
 */
static inline void cvmx_pip_config_diffserv_qos(uint64_t diffserv, uint64_t qos)
{
	union cvmx_pip_qos_diffx pip_qos_diffx;
	pip_qos_diffx.u64 = 0;
	pip_qos_diffx.s.qos = qos;
	cvmx_write_csr(CVMX_PIP_QOS_DIFFX(diffserv), pip_qos_diffx.u64);
}

/**
 * Get the status counters for a port.
 *
 * @port_num: Port number to get statistics for.
 * @clear:    Set to 1 to clear the counters after they are read
 * @status:   Where to put the results.
 */
static inline void cvmx_pip_get_port_status(uint64_t port_num, uint64_t clear,
					    cvmx_pip_port_status_t *status)
{
	union cvmx_pip_stat_ctl pip_stat_ctl;
	union cvmx_pip_stat0_prtx stat0;
	union cvmx_pip_stat1_prtx stat1;
	union cvmx_pip_stat2_prtx stat2;
	union cvmx_pip_stat3_prtx stat3;
	union cvmx_pip_stat4_prtx stat4;
	union cvmx_pip_stat5_prtx stat5;
	union cvmx_pip_stat6_prtx stat6;
	union cvmx_pip_stat7_prtx stat7;
	union cvmx_pip_stat8_prtx stat8;
	union cvmx_pip_stat9_prtx stat9;
	union cvmx_pip_stat_inb_pktsx pip_stat_inb_pktsx;
	union cvmx_pip_stat_inb_octsx pip_stat_inb_octsx;
	union cvmx_pip_stat_inb_errsx pip_stat_inb_errsx;

	pip_stat_ctl.u64 = 0;
	pip_stat_ctl.s.rdclr = clear;
	cvmx_write_csr(CVMX_PIP_STAT_CTL, pip_stat_ctl.u64);

	stat0.u64 = cvmx_read_csr(CVMX_PIP_STAT0_PRTX(port_num));
	stat1.u64 = cvmx_read_csr(CVMX_PIP_STAT1_PRTX(port_num));
	stat2.u64 = cvmx_read_csr(CVMX_PIP_STAT2_PRTX(port_num));
	stat3.u64 = cvmx_read_csr(CVMX_PIP_STAT3_PRTX(port_num));
	stat4.u64 = cvmx_read_csr(CVMX_PIP_STAT4_PRTX(port_num));
	stat5.u64 = cvmx_read_csr(CVMX_PIP_STAT5_PRTX(port_num));
	stat6.u64 = cvmx_read_csr(CVMX_PIP_STAT6_PRTX(port_num));
	stat7.u64 = cvmx_read_csr(CVMX_PIP_STAT7_PRTX(port_num));
	stat8.u64 = cvmx_read_csr(CVMX_PIP_STAT8_PRTX(port_num));
	stat9.u64 = cvmx_read_csr(CVMX_PIP_STAT9_PRTX(port_num));
	pip_stat_inb_pktsx.u64 =
	    cvmx_read_csr(CVMX_PIP_STAT_INB_PKTSX(port_num));
	pip_stat_inb_octsx.u64 =
	    cvmx_read_csr(CVMX_PIP_STAT_INB_OCTSX(port_num));
	pip_stat_inb_errsx.u64 =
	    cvmx_read_csr(CVMX_PIP_STAT_INB_ERRSX(port_num));

	status->dropped_octets = stat0.s.drp_octs;
	status->dropped_packets = stat0.s.drp_pkts;
	status->octets = stat1.s.octs;
	status->pci_raw_packets = stat2.s.raw;
	status->packets = stat2.s.pkts;
	status->multicast_packets = stat3.s.mcst;
	status->broadcast_packets = stat3.s.bcst;
	status->len_64_packets = stat4.s.h64;
	status->len_65_127_packets = stat4.s.h65to127;
	status->len_128_255_packets = stat5.s.h128to255;
	status->len_256_511_packets = stat5.s.h256to511;
	status->len_512_1023_packets = stat6.s.h512to1023;
	status->len_1024_1518_packets = stat6.s.h1024to1518;
	status->len_1519_max_packets = stat7.s.h1519;
	status->fcs_align_err_packets = stat7.s.fcs;
	status->runt_packets = stat8.s.undersz;
	status->runt_crc_packets = stat8.s.frag;
	status->oversize_packets = stat9.s.oversz;
	status->oversize_crc_packets = stat9.s.jabber;
	status->inb_packets = pip_stat_inb_pktsx.s.pkts;
	status->inb_octets = pip_stat_inb_octsx.s.octs;
	status->inb_errors = pip_stat_inb_errsx.s.errs;

	if (cvmx_octeon_is_pass1()) {
		/*
		 * Kludge to fix Octeon Pass 1 errata - Drop counts
		 * don't work.
		 */
		if (status->inb_packets > status->packets)
			status->dropped_packets =
			    status->inb_packets - status->packets;
		else
			status->dropped_packets = 0;
		if (status->inb_octets - status->inb_packets * 4 >
		    status->octets)
			status->dropped_octets =
			    status->inb_octets - status->inb_packets * 4 -
			    status->octets;
		else
			status->dropped_octets = 0;
	}
}

/**
 * Configure the hardware CRC engine
 *
 * @interface: Interface to configure (0 or 1)
 * @invert_result:
 *		   Invert the result of the CRC
 * @reflect:  Reflect
 * @initialization_vector:
 *		   CRC initialization vector
 */
static inline void cvmx_pip_config_crc(uint64_t interface,
				       uint64_t invert_result, uint64_t reflect,
				       uint32_t initialization_vector)
{
	if (OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)) {
		union cvmx_pip_crc_ctlx config;
		union cvmx_pip_crc_ivx pip_crc_ivx;

		config.u64 = 0;
		config.s.invres = invert_result;
		config.s.reflect = reflect;
		cvmx_write_csr(CVMX_PIP_CRC_CTLX(interface), config.u64);

		pip_crc_ivx.u64 = 0;
		pip_crc_ivx.s.iv = initialization_vector;
		cvmx_write_csr(CVMX_PIP_CRC_IVX(interface), pip_crc_ivx.u64);
	}
}

/**
 * Clear all bits in a tag mask. This should be called on
 * startup before any calls to cvmx_pip_tag_mask_set. Each bit
 * set in the final mask represent a byte used in the packet for
 * tag generation.
 *
 * @mask_index: Which tag mask to clear (0..3)
 */
static inline void cvmx_pip_tag_mask_clear(uint64_t mask_index)
{
	uint64_t index;
	union cvmx_pip_tag_incx pip_tag_incx;
	pip_tag_incx.u64 = 0;
	pip_tag_incx.s.en = 0;
	for (index = mask_index * 16; index < (mask_index + 1) * 16; index++)
		cvmx_write_csr(CVMX_PIP_TAG_INCX(index), pip_tag_incx.u64);
}

/**
 * Sets a range of bits in the tag mask. The tag mask is used
 * when the cvmx_pip_port_tag_cfg_t tag_mode is non zero.
 * There are four separate masks that can be configured.
 *
 * @mask_index: Which tag mask to modify (0..3)
 * @offset: Offset into the bitmask to set bits at. Use the GCC macro
 *	    offsetof() to determine the offsets into packet headers.
 *	    For example, offsetof(ethhdr, protocol) returns the offset
 *	    of the ethernet protocol field.  The bitmask selects which
 *	    bytes to include the the tag, with bit offset X selecting
 *	    byte at offset X from the beginning of the packet data.
 * @len:    Number of bytes to include. Usually this is the sizeof()
 *	    the field.
 */
static inline void cvmx_pip_tag_mask_set(uint64_t mask_index, uint64_t offset,
					 uint64_t len)
{
	while (len--) {
		union cvmx_pip_tag_incx pip_tag_incx;
		uint64_t index = mask_index * 16 + offset / 8;
		pip_tag_incx.u64 = cvmx_read_csr(CVMX_PIP_TAG_INCX(index));
		pip_tag_incx.s.en |= 0x80 >> (offset & 0x7);
		cvmx_write_csr(CVMX_PIP_TAG_INCX(index), pip_tag_incx.u64);
		offset++;
	}
}

#endif /*  __CVMX_PIP_H__ */
