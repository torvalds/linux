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
 * @EFX_FILTER_RX_TCP_FULL: RX, matching TCP/IPv4 4-tuple
 * @EFX_FILTER_RX_TCP_WILD: RX, matching TCP/IPv4 destination (host, port)
 * @EFX_FILTER_RX_UDP_FULL: RX, matching UDP/IPv4 4-tuple
 * @EFX_FILTER_RX_UDP_WILD: RX, matching UDP/IPv4 destination (host, port)
 * @EFX_FILTER_RX_MAC_FULL: RX, matching Ethernet destination MAC address, VID
 * @EFX_FILTER_RX_MAC_WILD: RX, matching Ethernet destination MAC address
 *
 * Falcon NICs only support the RX TCP/IPv4 and UDP/IPv4 filter types.
 */
enum efx_filter_type {
	EFX_FILTER_RX_TCP_FULL = 0,
	EFX_FILTER_RX_TCP_WILD,
	EFX_FILTER_RX_UDP_FULL,
	EFX_FILTER_RX_UDP_WILD,
	EFX_FILTER_RX_MAC_FULL = 4,
	EFX_FILTER_RX_MAC_WILD,
	EFX_FILTER_TYPE_COUNT,
};

/**
 * enum efx_filter_priority - priority of a hardware filter specification
 * @EFX_FILTER_PRI_HINT: Performance hint
 * @EFX_FILTER_PRI_MANUAL: Manually configured filter
 * @EFX_FILTER_PRI_REQUIRED: Required for correct behaviour
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
 *
 * Currently, no flags are defined for TX filters.
 */
enum efx_filter_flags {
	EFX_FILTER_FLAG_RX_RSS = 0x01,
	EFX_FILTER_FLAG_RX_SCATTER = 0x02,
	EFX_FILTER_FLAG_RX_OVERRIDE_IP = 0x04,
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
 */
struct efx_filter_spec {
	u8	type:4;
	u8	priority:4;
	u8	flags;
	u16	dmaq_id;
	u32	data[3];
};

/**
 * efx_filter_set_rx_tcp_full - specify RX filter with TCP/IPv4 full match
 * @spec: Specification to initialise
 * @shost: Source host address (host byte order)
 * @sport: Source port (host byte order)
 * @dhost: Destination host address (host byte order)
 * @dport: Destination port (host byte order)
 */
static inline void
efx_filter_set_rx_tcp_full(struct efx_filter_spec *spec,
			   u32 shost, u16 sport, u32 dhost, u16 dport)
{
	spec->type = EFX_FILTER_RX_TCP_FULL;
	spec->data[0] = sport | shost << 16;
	spec->data[1] = dport << 16 | shost >> 16;
	spec->data[2] = dhost;
}

/**
 * efx_filter_set_rx_tcp_wild - specify RX filter with TCP/IPv4 wildcard match
 * @spec: Specification to initialise
 * @dhost: Destination host address (host byte order)
 * @dport: Destination port (host byte order)
 */
static inline void
efx_filter_set_rx_tcp_wild(struct efx_filter_spec *spec, u32 dhost, u16 dport)
{
	spec->type = EFX_FILTER_RX_TCP_WILD;
	spec->data[0] = 0;
	spec->data[1] = dport << 16;
	spec->data[2] = dhost;
}

/**
 * efx_filter_set_rx_udp_full - specify RX filter with UDP/IPv4 full match
 * @spec: Specification to initialise
 * @shost: Source host address (host byte order)
 * @sport: Source port (host byte order)
 * @dhost: Destination host address (host byte order)
 * @dport: Destination port (host byte order)
 */
static inline void
efx_filter_set_rx_udp_full(struct efx_filter_spec *spec,
			   u32 shost, u16 sport, u32 dhost, u16 dport)
{
	spec->type = EFX_FILTER_RX_UDP_FULL;
	spec->data[0] = sport | shost << 16;
	spec->data[1] = dport << 16 | shost >> 16;
	spec->data[2] = dhost;
}

/**
 * efx_filter_set_rx_udp_wild - specify RX filter with UDP/IPv4 wildcard match
 * @spec: Specification to initialise
 * @dhost: Destination host address (host byte order)
 * @dport: Destination port (host byte order)
 */
static inline void
efx_filter_set_rx_udp_wild(struct efx_filter_spec *spec, u32 dhost, u16 dport)
{
	spec->type = EFX_FILTER_RX_UDP_WILD;
	spec->data[0] = dport;
	spec->data[1] = 0;
	spec->data[2] = dhost;
}

/**
 * efx_filter_set_rx_mac_full - specify RX filter with MAC full match
 * @spec: Specification to initialise
 * @vid: VLAN ID
 * @addr: Destination MAC address
 */
static inline void efx_filter_set_rx_mac_full(struct efx_filter_spec *spec,
					      u16 vid, const u8 *addr)
{
	spec->type = EFX_FILTER_RX_MAC_FULL;
	spec->data[0] = vid;
	spec->data[1] = addr[2] << 24 | addr[3] << 16 | addr[4] << 8 | addr[5];
	spec->data[2] = addr[0] << 8 | addr[1];
}

/**
 * efx_filter_set_rx_mac_full - specify RX filter with MAC wildcard match
 * @spec: Specification to initialise
 * @addr: Destination MAC address
 */
static inline void efx_filter_set_rx_mac_wild(struct efx_filter_spec *spec,
					      const u8 *addr)
{
	spec->type = EFX_FILTER_RX_MAC_WILD;
	spec->data[0] = 0;
	spec->data[1] = addr[2] << 24 | addr[3] << 16 | addr[4] << 8 | addr[5];
	spec->data[2] = addr[0] << 8 | addr[1];
}

#endif /* EFX_FILTER_H */
