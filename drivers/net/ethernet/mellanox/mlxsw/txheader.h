/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_TXHEADER_H
#define _MLXSW_TXHEADER_H

/* tx_hdr_version
 * Tx header version.
 * Must be set to 1.
 */
MLXSW_ITEM32(tx, hdr, version, 0x00, 28, 4);

/* tx_hdr_ctl
 * Packet control type.
 * 0 - Ethernet control (e.g. EMADs, LACP)
 * 1 - Ethernet data
 */
MLXSW_ITEM32(tx, hdr, ctl, 0x00, 26, 2);

/* tx_hdr_proto
 * Packet protocol type. Must be set to 1 (Ethernet).
 */
MLXSW_ITEM32(tx, hdr, proto, 0x00, 21, 3);

/* tx_hdr_rx_is_router
 * Packet is sent from the router. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, rx_is_router, 0x00, 19, 1);

/* tx_hdr_fid_valid
 * Indicates if the 'fid' field is valid and should be used for
 * forwarding lookup. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid_valid, 0x00, 16, 1);

/* tx_hdr_swid
 * Switch partition ID. Must be set to 0.
 */
MLXSW_ITEM32(tx, hdr, swid, 0x00, 12, 3);

/* tx_hdr_control_tclass
 * Indicates if the packet should use the control TClass and not one
 * of the data TClasses.
 */
MLXSW_ITEM32(tx, hdr, control_tclass, 0x00, 6, 1);

/* tx_hdr_port_mid
 * Destination local port for unicast packets.
 * Destination multicast ID for multicast packets.
 *
 * Control packets are directed to a specific egress port, while data
 * packets are transmitted through the CPU port (0) into the switch partition,
 * where forwarding rules are applied.
 */
MLXSW_ITEM32(tx, hdr, port_mid, 0x04, 16, 16);

/* tx_hdr_fid
 * Forwarding ID used for L2 forwarding lookup. Valid only if 'fid_valid' is
 * set, otherwise calculated based on the packet's VID using VID to FID mapping.
 * Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid, 0x08, 16, 16);

/* tx_hdr_type
 * 0 - Data packets
 * 6 - Control packets
 */
MLXSW_ITEM32(tx, hdr, type, 0x0C, 0, 4);

#define MLXSW_TXHDR_LEN 0x10
#define MLXSW_TXHDR_VERSION_0 0
#define MLXSW_TXHDR_VERSION_1 1

enum {
	MLXSW_TXHDR_ETH_CTL,
	MLXSW_TXHDR_ETH_DATA,
};

#define MLXSW_TXHDR_PROTO_ETH 1

enum {
	MLXSW_TXHDR_ETCLASS_0,
	MLXSW_TXHDR_ETCLASS_1,
	MLXSW_TXHDR_ETCLASS_2,
	MLXSW_TXHDR_ETCLASS_3,
	MLXSW_TXHDR_ETCLASS_4,
	MLXSW_TXHDR_ETCLASS_5,
	MLXSW_TXHDR_ETCLASS_6,
	MLXSW_TXHDR_ETCLASS_7,
};

enum {
	MLXSW_TXHDR_RDQ_OTHER,
	MLXSW_TXHDR_RDQ_EMAD = 0x1f,
};

#define MLXSW_TXHDR_CTCLASS3 0
#define MLXSW_TXHDR_CPU_SIG 0
#define MLXSW_TXHDR_SIG 0xE0E0
#define MLXSW_TXHDR_STCLASS_NONE 0

enum {
	MLXSW_TXHDR_NOT_EMAD,
	MLXSW_TXHDR_EMAD,
};

enum {
	MLXSW_TXHDR_TYPE_DATA,
	MLXSW_TXHDR_TYPE_CONTROL = 6,
};

#endif
