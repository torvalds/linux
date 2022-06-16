/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2013 Solarflare Communications Inc.
 */

#ifndef EFX_FILTER_H
#define EFX_FILTER_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <asm/byteorder.h>

/**
 * enum efx_filter_match_flags - Flags for hardware filter match type
 * @EFX_FILTER_MATCH_REM_HOST: Match by remote IP host address
 * @EFX_FILTER_MATCH_LOC_HOST: Match by local IP host address
 * @EFX_FILTER_MATCH_REM_MAC: Match by remote MAC address
 * @EFX_FILTER_MATCH_REM_PORT: Match by remote TCP/UDP port
 * @EFX_FILTER_MATCH_LOC_MAC: Match by local MAC address
 * @EFX_FILTER_MATCH_LOC_PORT: Match by local TCP/UDP port
 * @EFX_FILTER_MATCH_ETHER_TYPE: Match by Ether-type
 * @EFX_FILTER_MATCH_INNER_VID: Match by inner VLAN ID
 * @EFX_FILTER_MATCH_OUTER_VID: Match by outer VLAN ID
 * @EFX_FILTER_MATCH_IP_PROTO: Match by IP transport protocol
 * @EFX_FILTER_MATCH_LOC_MAC_IG: Match by local MAC address I/G bit.
 * @EFX_FILTER_MATCH_ENCAP_TYPE: Match by encapsulation type.
 *	Used for RX default unicast and multicast/broadcast filters.
 *
 * Only some combinations are supported, depending on NIC type:
 *
 * - Falcon supports RX filters matching by {TCP,UDP}/IPv4 4-tuple or
 *   local 2-tuple (only implemented for Falcon B0)
 *
 * - Siena supports RX and TX filters matching by {TCP,UDP}/IPv4 4-tuple
 *   or local 2-tuple, or local MAC with or without outer VID, and RX
 *   default filters
 *
 * - Huntington supports filter matching controlled by firmware, potentially
 *   using {TCP,UDP}/IPv{4,6} 4-tuple or local 2-tuple, local MAC or I/G bit,
 *   with or without outer and inner VID
 */
enum efx_filter_match_flags {
	EFX_FILTER_MATCH_REM_HOST =	0x0001,
	EFX_FILTER_MATCH_LOC_HOST =	0x0002,
	EFX_FILTER_MATCH_REM_MAC =	0x0004,
	EFX_FILTER_MATCH_REM_PORT =	0x0008,
	EFX_FILTER_MATCH_LOC_MAC =	0x0010,
	EFX_FILTER_MATCH_LOC_PORT =	0x0020,
	EFX_FILTER_MATCH_ETHER_TYPE =	0x0040,
	EFX_FILTER_MATCH_INNER_VID =	0x0080,
	EFX_FILTER_MATCH_OUTER_VID =	0x0100,
	EFX_FILTER_MATCH_IP_PROTO =	0x0200,
	EFX_FILTER_MATCH_LOC_MAC_IG =	0x0400,
	EFX_FILTER_MATCH_ENCAP_TYPE =	0x0800,
};

/**
 * enum efx_filter_priority - priority of a hardware filter specification
 * @EFX_FILTER_PRI_HINT: Performance hint
 * @EFX_FILTER_PRI_AUTO: Automatic filter based on device address list
 *	or hardware requirements.  This may only be used by the filter
 *	implementation for each NIC type.
 * @EFX_FILTER_PRI_MANUAL: Manually configured filter
 * @EFX_FILTER_PRI_REQUIRED: Required for correct behaviour (user-level
 *	networking and SR-IOV)
 */
enum efx_filter_priority {
	EFX_FILTER_PRI_HINT = 0,
	EFX_FILTER_PRI_AUTO,
	EFX_FILTER_PRI_MANUAL,
	EFX_FILTER_PRI_REQUIRED,
};

/**
 * enum efx_filter_flags - flags for hardware filter specifications
 * @EFX_FILTER_FLAG_RX_RSS: Use RSS to spread across multiple queues.
 *	By default, matching packets will be delivered only to the
 *	specified queue. If this flag is set, they will be delivered
 *	to a range of queues offset from the specified queue number
 *	according to the indirection table.
 * @EFX_FILTER_FLAG_RX_SCATTER: Enable DMA scatter on the receiving
 *	queue.
 * @EFX_FILTER_FLAG_RX_OVER_AUTO: Indicates a filter that is
 *	overriding an automatic filter (priority
 *	%EFX_FILTER_PRI_AUTO).  This may only be set by the filter
 *	implementation for each type.  A removal request will restore
 *	the automatic filter in its place.
 * @EFX_FILTER_FLAG_RX: Filter is for RX
 * @EFX_FILTER_FLAG_TX: Filter is for TX
 */
enum efx_filter_flags {
	EFX_FILTER_FLAG_RX_RSS = 0x01,
	EFX_FILTER_FLAG_RX_SCATTER = 0x02,
	EFX_FILTER_FLAG_RX_OVER_AUTO = 0x04,
	EFX_FILTER_FLAG_RX = 0x08,
	EFX_FILTER_FLAG_TX = 0x10,
};

/** enum efx_encap_type - types of encapsulation
 * @EFX_ENCAP_TYPE_NONE: no encapsulation
 * @EFX_ENCAP_TYPE_VXLAN: VXLAN encapsulation
 * @EFX_ENCAP_TYPE_NVGRE: NVGRE encapsulation
 * @EFX_ENCAP_TYPE_GENEVE: GENEVE encapsulation
 * @EFX_ENCAP_FLAG_IPV6: indicates IPv6 outer frame
 *
 * Contains both enumerated types and flags.
 * To get just the type, OR with @EFX_ENCAP_TYPES_MASK.
 */
enum efx_encap_type {
	EFX_ENCAP_TYPE_NONE = 0,
	EFX_ENCAP_TYPE_VXLAN = 1,
	EFX_ENCAP_TYPE_NVGRE = 2,
	EFX_ENCAP_TYPE_GENEVE = 3,

	EFX_ENCAP_TYPES_MASK = 7,
	EFX_ENCAP_FLAG_IPV6 = 8,
};

/**
 * struct efx_filter_spec - specification for a hardware filter
 * @match_flags: Match type flags, from &enum efx_filter_match_flags
 * @priority: Priority of the filter, from &enum efx_filter_priority
 * @flags: Miscellaneous flags, from &enum efx_filter_flags
 * @rss_context: RSS context to use, if %EFX_FILTER_FLAG_RX_RSS is set.  This
 *	is a user_id (with 0 meaning the driver/default RSS context), not an
 *	MCFW context_id.
 * @dmaq_id: Source/target queue index, or %EFX_FILTER_RX_DMAQ_ID_DROP for
 *	an RX drop filter
 * @outer_vid: Outer VLAN ID to match, if %EFX_FILTER_MATCH_OUTER_VID is set
 * @inner_vid: Inner VLAN ID to match, if %EFX_FILTER_MATCH_INNER_VID is set
 * @loc_mac: Local MAC address to match, if %EFX_FILTER_MATCH_LOC_MAC or
 *	%EFX_FILTER_MATCH_LOC_MAC_IG is set
 * @rem_mac: Remote MAC address to match, if %EFX_FILTER_MATCH_REM_MAC is set
 * @ether_type: Ether-type to match, if %EFX_FILTER_MATCH_ETHER_TYPE is set
 * @ip_proto: IP transport protocol to match, if %EFX_FILTER_MATCH_IP_PROTO
 *	is set
 * @loc_host: Local IP host to match, if %EFX_FILTER_MATCH_LOC_HOST is set
 * @rem_host: Remote IP host to match, if %EFX_FILTER_MATCH_REM_HOST is set
 * @loc_port: Local TCP/UDP port to match, if %EFX_FILTER_MATCH_LOC_PORT is set
 * @rem_port: Remote TCP/UDP port to match, if %EFX_FILTER_MATCH_REM_PORT is set
 * @encap_type: Encapsulation type to match (from &enum efx_encap_type), if
 *	%EFX_FILTER_MATCH_ENCAP_TYPE is set
 *
 * The efx_filter_init_rx() or efx_filter_init_tx() function *must* be
 * used to initialise the structure.  The efx_filter_set_*() functions
 * may then be used to set @rss_context, @match_flags and related
 * fields.
 *
 * The @priority field is used by software to determine whether a new
 * filter may replace an old one.  The hardware priority of a filter
 * depends on which fields are matched.
 */
struct efx_filter_spec {
	u32	match_flags:12;
	u32	priority:2;
	u32	flags:6;
	u32	dmaq_id:12;
	u32	rss_context;
	__be16	outer_vid __aligned(4); /* allow jhash2() of match values */
	__be16	inner_vid;
	u8	loc_mac[ETH_ALEN];
	u8	rem_mac[ETH_ALEN];
	__be16	ether_type;
	u8	ip_proto;
	__be32	loc_host[4];
	__be32	rem_host[4];
	__be16	loc_port;
	__be16	rem_port;
	u32     encap_type:4;
	/* total 65 bytes */
};

enum {
	EFX_FILTER_RX_DMAQ_ID_DROP = 0xfff
};

static inline void efx_filter_init_rx(struct efx_filter_spec *spec,
				      enum efx_filter_priority priority,
				      enum efx_filter_flags flags,
				      unsigned rxq_id)
{
	memset(spec, 0, sizeof(*spec));
	spec->priority = priority;
	spec->flags = EFX_FILTER_FLAG_RX | flags;
	spec->rss_context = 0;
	spec->dmaq_id = rxq_id;
}

static inline void efx_filter_init_tx(struct efx_filter_spec *spec,
				      unsigned txq_id)
{
	memset(spec, 0, sizeof(*spec));
	spec->priority = EFX_FILTER_PRI_REQUIRED;
	spec->flags = EFX_FILTER_FLAG_TX;
	spec->dmaq_id = txq_id;
}

/**
 * efx_filter_set_ipv4_local - specify IPv4 host, transport protocol and port
 * @spec: Specification to initialise
 * @proto: Transport layer protocol number
 * @host: Local host address (network byte order)
 * @port: Local port (network byte order)
 */
static inline int
efx_filter_set_ipv4_local(struct efx_filter_spec *spec, u8 proto,
			  __be32 host, __be16 port)
{
	spec->match_flags |=
		EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT;
	spec->ether_type = htons(ETH_P_IP);
	spec->ip_proto = proto;
	spec->loc_host[0] = host;
	spec->loc_port = port;
	return 0;
}

/**
 * efx_filter_set_ipv4_full - specify IPv4 hosts, transport protocol and ports
 * @spec: Specification to initialise
 * @proto: Transport layer protocol number
 * @lhost: Local host address (network byte order)
 * @lport: Local port (network byte order)
 * @rhost: Remote host address (network byte order)
 * @rport: Remote port (network byte order)
 */
static inline int
efx_filter_set_ipv4_full(struct efx_filter_spec *spec, u8 proto,
			 __be32 lhost, __be16 lport,
			 __be32 rhost, __be16 rport)
{
	spec->match_flags |=
		EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT |
		EFX_FILTER_MATCH_REM_HOST | EFX_FILTER_MATCH_REM_PORT;
	spec->ether_type = htons(ETH_P_IP);
	spec->ip_proto = proto;
	spec->loc_host[0] = lhost;
	spec->loc_port = lport;
	spec->rem_host[0] = rhost;
	spec->rem_port = rport;
	return 0;
}

enum {
	EFX_FILTER_VID_UNSPEC = 0xffff,
};

/**
 * efx_filter_set_eth_local - specify local Ethernet address and/or VID
 * @spec: Specification to initialise
 * @vid: Outer VLAN ID to match, or %EFX_FILTER_VID_UNSPEC
 * @addr: Local Ethernet MAC address, or %NULL
 */
static inline int efx_filter_set_eth_local(struct efx_filter_spec *spec,
					   u16 vid, const u8 *addr)
{
	if (vid == EFX_FILTER_VID_UNSPEC && addr == NULL)
		return -EINVAL;

	if (vid != EFX_FILTER_VID_UNSPEC) {
		spec->match_flags |= EFX_FILTER_MATCH_OUTER_VID;
		spec->outer_vid = htons(vid);
	}
	if (addr != NULL) {
		spec->match_flags |= EFX_FILTER_MATCH_LOC_MAC;
		ether_addr_copy(spec->loc_mac, addr);
	}
	return 0;
}

/**
 * efx_filter_set_uc_def - specify matching otherwise-unmatched unicast
 * @spec: Specification to initialise
 */
static inline int efx_filter_set_uc_def(struct efx_filter_spec *spec)
{
	spec->match_flags |= EFX_FILTER_MATCH_LOC_MAC_IG;
	return 0;
}

/**
 * efx_filter_set_mc_def - specify matching otherwise-unmatched multicast
 * @spec: Specification to initialise
 */
static inline int efx_filter_set_mc_def(struct efx_filter_spec *spec)
{
	spec->match_flags |= EFX_FILTER_MATCH_LOC_MAC_IG;
	spec->loc_mac[0] = 1;
	return 0;
}

static inline void efx_filter_set_encap_type(struct efx_filter_spec *spec,
					     enum efx_encap_type encap_type)
{
	spec->match_flags |= EFX_FILTER_MATCH_ENCAP_TYPE;
	spec->encap_type = encap_type;
}

static inline enum efx_encap_type efx_filter_get_encap_type(
		const struct efx_filter_spec *spec)
{
	if (spec->match_flags & EFX_FILTER_MATCH_ENCAP_TYPE)
		return spec->encap_type;
	return EFX_ENCAP_TYPE_NONE;
}
#endif /* EFX_FILTER_H */
