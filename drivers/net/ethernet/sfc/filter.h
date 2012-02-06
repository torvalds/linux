/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_FILTER_H
#define EFX_FILTER_H

#include <linux/types.h>

/**
 * enum efx_filter_type - type of hardware filter
 * @EFX_FILTER_TCP_FULL: Matching TCP/IPv4 4-tuple
 * @EFX_FILTER_TCP_WILD: Matching TCP/IPv4 destination (host, port)
 * @EFX_FILTER_UDP_FULL: Matching UDP/IPv4 4-tuple
 * @EFX_FILTER_UDP_WILD: Matching UDP/IPv4 destination (host, port)
 * @EFX_FILTER_MAC_FULL: Matching Ethernet destination MAC address, VID
 * @EFX_FILTER_MAC_WILD: Matching Ethernet destination MAC address
 * @EFX_FILTER_UC_DEF: Matching all otherwise unmatched unicast
 * @EFX_FILTER_MC_DEF: Matching all otherwise unmatched multicast
 * @EFX_FILTER_UNSPEC: Match type is unspecified
 *
 * Falcon NICs only support the TCP/IPv4 and UDP/IPv4 filter types.
 */
enum efx_filter_type {
	EFX_FILTER_TCP_FULL = 0,
	EFX_FILTER_TCP_WILD,
	EFX_FILTER_UDP_FULL,
	EFX_FILTER_UDP_WILD,
	EFX_FILTER_MAC_FULL = 4,
	EFX_FILTER_MAC_WILD,
	EFX_FILTER_UC_DEF = 8,
	EFX_FILTER_MC_DEF,
	EFX_FILTER_TYPE_COUNT,		/* number of specific types */
	EFX_FILTER_UNSPEC = 0xf,
};

/**
 * enum efx_filter_priority - priority of a hardware filter specification
 * @EFX_FILTER_PRI_HINT: Performance hint
 * @EFX_FILTER_PRI_MANUAL: Manually configured filter
 * @EFX_FILTER_PRI_REQUIRED: Required for correct behaviour (user-level
 *	networking and SR-IOV)
 */
enum efx_filter_priority {
	EFX_FILTER_PRI_HINT = 0,
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
 * @EFX_FILTER_FLAG_RX_OVERRIDE_IP: Enables a MAC filter to override
 *	any IP filter that matches the same packet.  By default, IP
 *	filters take precedence.
 * @EFX_FILTER_FLAG_RX: Filter is for RX
 * @EFX_FILTER_FLAG_TX: Filter is for TX
 */
enum efx_filter_flags {
	EFX_FILTER_FLAG_RX_RSS = 0x01,
	EFX_FILTER_FLAG_RX_SCATTER = 0x02,
	EFX_FILTER_FLAG_RX_OVERRIDE_IP = 0x04,
	EFX_FILTER_FLAG_RX = 0x08,
	EFX_FILTER_FLAG_TX = 0x10,
};

/**
 * struct efx_filter_spec - specification for a hardware filter
 * @type: Type of match to be performed, from &enum efx_filter_type
 * @priority: Priority of the filter, from &enum efx_filter_priority
 * @flags: Miscellaneous flags, from &enum efx_filter_flags
 * @dmaq_id: Source/target queue index
 * @data: Match data (type-dependent)
 *
 * Use the efx_filter_set_*() functions to initialise the @type and
 * @data fields.
 *
 * The @priority field is used by software to determine whether a new
 * filter may replace an old one.  The hardware priority of a filter
 * depends on the filter type and %EFX_FILTER_FLAG_RX_OVERRIDE_IP
 * flag.
 */
struct efx_filter_spec {
	u8	type:4;
	u8	priority:4;
	u8	flags;
	u16	dmaq_id;
	u32	data[3];
};

static inline void efx_filter_init_rx(struct efx_filter_spec *spec,
				      enum efx_filter_priority priority,
				      enum efx_filter_flags flags,
				      unsigned rxq_id)
{
	spec->type = EFX_FILTER_UNSPEC;
	spec->priority = priority;
	spec->flags = EFX_FILTER_FLAG_RX | flags;
	spec->dmaq_id = rxq_id;
}

static inline void efx_filter_init_tx(struct efx_filter_spec *spec,
				      unsigned txq_id)
{
	spec->type = EFX_FILTER_UNSPEC;
	spec->priority = EFX_FILTER_PRI_REQUIRED;
	spec->flags = EFX_FILTER_FLAG_TX;
	spec->dmaq_id = txq_id;
}

extern int efx_filter_set_ipv4_local(struct efx_filter_spec *spec, u8 proto,
				     __be32 host, __be16 port);
extern int efx_filter_get_ipv4_local(const struct efx_filter_spec *spec,
				     u8 *proto, __be32 *host, __be16 *port);
extern int efx_filter_set_ipv4_full(struct efx_filter_spec *spec, u8 proto,
				    __be32 host, __be16 port,
				    __be32 rhost, __be16 rport);
extern int efx_filter_get_ipv4_full(const struct efx_filter_spec *spec,
				    u8 *proto, __be32 *host, __be16 *port,
				    __be32 *rhost, __be16 *rport);
extern int efx_filter_set_eth_local(struct efx_filter_spec *spec,
				    u16 vid, const u8 *addr);
extern int efx_filter_get_eth_local(const struct efx_filter_spec *spec,
				    u16 *vid, u8 *addr);
extern int efx_filter_set_uc_def(struct efx_filter_spec *spec);
extern int efx_filter_set_mc_def(struct efx_filter_spec *spec);
enum {
	EFX_FILTER_VID_UNSPEC = 0xffff,
};

#endif /* EFX_FILTER_H */
