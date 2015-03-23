/*
 * WPA Supplicant - Layer2 packet interface definition
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file defines an interface for layer 2 (link layer) packet sending and
 * receiving. l2_packet_linux.c is one implementation for such a layer 2
 * implementation using Linux packet sockets and l2_packet_pcap.c another one
 * using libpcap and libdnet. When porting %wpa_supplicant to other operating
 * systems, a new l2_packet implementation may need to be added.
 */

#ifndef L2_PACKET_H
#define L2_PACKET_H

/**
 * struct l2_packet_data - Internal l2_packet data structure
 *
 * This structure is used by the l2_packet implementation to store its private
 * data. Other files use a pointer to this data when calling the l2_packet
 * functions, but the contents of this structure should not be used directly
 * outside l2_packet implementation.
 */
struct l2_packet_data;

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct l2_ethhdr {
	u8 h_dest[ETH_ALEN];
	u8 h_source[ETH_ALEN];
	be16 h_proto;
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

/**
 * l2_packet_init - Initialize l2_packet interface
 * @ifname: Interface name
 * @own_addr: Optional own MAC address if available from driver interface or
 *	%NULL if not available
 * @protocol: Ethernet protocol number in host byte order
 * @rx_callback: Callback function that will be called for each received packet
 * @rx_callback_ctx: Callback data (ctx) for calls to rx_callback()
 * @l2_hdr: 1 = include layer 2 header, 0 = do not include header
 * Returns: Pointer to internal data or %NULL on failure
 *
 * rx_callback function will be called with src_addr pointing to the source
 * address (MAC address) of the the packet. If l2_hdr is set to 0, buf
 * points to len bytes of the payload after the layer 2 header and similarly,
 * TX buffers start with payload. This behavior can be changed by setting
 * l2_hdr=1 to include the layer 2 header in the data buffer.
 */
struct l2_packet_data * l2_packet_init(
	const char *ifname, const u8 *own_addr, unsigned short protocol,
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len),
	void *rx_callback_ctx, int l2_hdr);

/**
 * l2_packet_deinit - Deinitialize l2_packet interface
 * @l2: Pointer to internal l2_packet data from l2_packet_init()
 */
void l2_packet_deinit(struct l2_packet_data *l2);

/**
 * l2_packet_get_own_addr - Get own layer 2 address
 * @l2: Pointer to internal l2_packet data from l2_packet_init()
 * @addr: Buffer for the own address (6 bytes)
 * Returns: 0 on success, -1 on failure
 */
int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr);

/**
 * l2_packet_send - Send a packet
 * @l2: Pointer to internal l2_packet data from l2_packet_init()
 * @dst_addr: Destination address for the packet (only used if l2_hdr == 0)
 * @proto: Protocol/ethertype for the packet in host byte order (only used if
 * l2_hdr == 0)
 * @buf: Packet contents to be sent; including layer 2 header if l2_hdr was
 * set to 1 in l2_packet_init() call. Otherwise, only the payload of the packet
 * is included.
 * @len: Length of the buffer (including l2 header only if l2_hdr == 1)
 * Returns: >=0 on success, <0 on failure
 */
int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
		   const u8 *buf, size_t len);

/**
 * l2_packet_get_ip_addr - Get the current IP address from the interface
 * @l2: Pointer to internal l2_packet data from l2_packet_init()
 * @buf: Buffer for the IP address in text format
 * @len: Maximum buffer length
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to get the current IP address from the interface
 * bound to the l2_packet. This is mainly for status information and the IP
 * address will be stored as an ASCII string. This function is not essential
 * for %wpa_supplicant operation, so full implementation is not required.
 * l2_packet implementation will need to define the function, but it can return
 * -1 if the IP address information is not available.
 */
int l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf, size_t len);


/**
 * l2_packet_notify_auth_start - Notify l2_packet about start of authentication
 * @l2: Pointer to internal l2_packet data from l2_packet_init()
 *
 * This function is called when authentication is expected to start, e.g., when
 * association has been completed, in order to prepare l2_packet implementation
 * for EAPOL frames. This function is used mainly if the l2_packet code needs
 * to do polling in which case it can increasing polling frequency. This can
 * also be an empty function if the l2_packet implementation does not benefit
 * from knowing about the starting authentication.
 */
void l2_packet_notify_auth_start(struct l2_packet_data *l2);

#endif /* L2_PACKET_H */
