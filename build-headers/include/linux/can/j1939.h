/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * j1939.h
 *
 * Copyright (c) 2010-2011 EIA Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _CAN_J1939_H_
#define _CAN_J1939_H_

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/can.h>

#define J1939_MAX_UNICAST_ADDR 0xfd
#define J1939_IDLE_ADDR 0xfe
#define J1939_NO_ADDR 0xff		/* == broadcast or no addr */
#define J1939_NO_NAME 0
#define J1939_PGN_REQUEST 0x0ea00		/* Request PG */
#define J1939_PGN_ADDRESS_CLAIMED 0x0ee00	/* Address Claimed */
#define J1939_PGN_ADDRESS_COMMANDED 0x0fed8	/* Commanded Address */
#define J1939_PGN_PDU1_MAX 0x3ff00
#define J1939_PGN_MAX 0x3ffff
#define J1939_NO_PGN 0x40000

/* J1939 Parameter Group Number
 *
 * bit 0-7	: PDU Specific (PS)
 * bit 8-15	: PDU Format (PF)
 * bit 16	: Data Page (DP)
 * bit 17	: Reserved (R)
 * bit 19-31	: set to zero
 */
typedef __u32 pgn_t;

/* J1939 Priority
 *
 * bit 0-2	: Priority (P)
 * bit 3-7	: set to zero
 */
typedef __u8 priority_t;

/* J1939 NAME
 *
 * bit 0-20	: Identity Number
 * bit 21-31	: Manufacturer Code
 * bit 32-34	: ECU Instance
 * bit 35-39	: Function Instance
 * bit 40-47	: Function
 * bit 48	: Reserved
 * bit 49-55	: Vehicle System
 * bit 56-59	: Vehicle System Instance
 * bit 60-62	: Industry Group
 * bit 63	: Arbitrary Address Capable
 */
typedef __u64 name_t;

/* J1939 socket options */
#define SOL_CAN_J1939 (SOL_CAN_BASE + CAN_J1939)
enum {
	SO_J1939_FILTER = 1,	/* set filters */
	SO_J1939_PROMISC = 2,	/* set/clr promiscuous mode */
	SO_J1939_SEND_PRIO = 3,
	SO_J1939_ERRQUEUE = 4,
};

enum {
	SCM_J1939_DEST_ADDR = 1,
	SCM_J1939_DEST_NAME = 2,
	SCM_J1939_PRIO = 3,
	SCM_J1939_ERRQUEUE = 4,
};

enum {
	J1939_NLA_PAD,
	J1939_NLA_BYTES_ACKED,
	J1939_NLA_TOTAL_SIZE,
	J1939_NLA_PGN,
	J1939_NLA_SRC_NAME,
	J1939_NLA_DEST_NAME,
	J1939_NLA_SRC_ADDR,
	J1939_NLA_DEST_ADDR,
};

enum {
	J1939_EE_INFO_NONE,
	J1939_EE_INFO_TX_ABORT,
	J1939_EE_INFO_RX_RTS,
	J1939_EE_INFO_RX_DPO,
	J1939_EE_INFO_RX_ABORT,
};

struct j1939_filter {
	name_t name;
	name_t name_mask;
	pgn_t pgn;
	pgn_t pgn_mask;
	__u8 addr;
	__u8 addr_mask;
};

#define J1939_FILTER_MAX 512 /* maximum number of j1939_filter set via setsockopt() */

#endif /* !_UAPI_CAN_J1939_H_ */
