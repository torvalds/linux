/*
 * Copyright 2010, 2011 Mark Nelson and Tseng-Hui (Frank) Lin, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_IO_EVENT_IRQ_H
#define _ASM_POWERPC_IO_EVENT_IRQ_H

#include <linux/types.h>
#include <linux/notifier.h>

#define PSERIES_IOEI_RPC_MAX_LEN 216

#define PSERIES_IOEI_TYPE_ERR_DETECTED		0x01
#define PSERIES_IOEI_TYPE_ERR_RECOVERED		0x02
#define PSERIES_IOEI_TYPE_EVENT			0x03
#define PSERIES_IOEI_TYPE_RPC_PASS_THRU		0x04

#define PSERIES_IOEI_SUBTYPE_NOT_APP		0x00
#define PSERIES_IOEI_SUBTYPE_REBALANCE_REQ	0x01
#define PSERIES_IOEI_SUBTYPE_NODE_ONLINE	0x03
#define PSERIES_IOEI_SUBTYPE_NODE_OFFLINE	0x04
#define PSERIES_IOEI_SUBTYPE_DUMP_SIZE_CHANGE	0x05
#define PSERIES_IOEI_SUBTYPE_TORRENT_IRV_UPDATE	0x06
#define PSERIES_IOEI_SUBTYPE_TORRENT_HFI_CFGED	0x07

#define PSERIES_IOEI_SCOPE_NOT_APP		0x00
#define PSERIES_IOEI_SCOPE_RIO_HUB		0x36
#define PSERIES_IOEI_SCOPE_RIO_BRIDGE		0x37
#define PSERIES_IOEI_SCOPE_PHB			0x38
#define PSERIES_IOEI_SCOPE_EADS_GLOBAL		0x39
#define PSERIES_IOEI_SCOPE_EADS_SLOT		0x3A
#define PSERIES_IOEI_SCOPE_TORRENT_HUB		0x3B
#define PSERIES_IOEI_SCOPE_SERVICE_PROC		0x51

/* Platform Event Log Format, Version 6, data portition of IO event section */
struct pseries_io_event {
	uint8_t event_type;		/* 0x00 IO-Event Type		*/
	uint8_t rpc_data_len;		/* 0x01 RPC data length		*/
	uint8_t scope;			/* 0x02 Error/Event Scope	*/
	uint8_t event_subtype;		/* 0x03 I/O-Event Sub-Type	*/
	uint32_t drc_index;		/* 0x04 DRC Index		*/
	uint8_t rpc_data[PSERIES_IOEI_RPC_MAX_LEN];
					/* 0x08 RPC Data (0-216 bytes,	*/
					/* padded to 4 bytes alignment)	*/
};

extern struct atomic_notifier_head pseries_ioei_notifier_list;

#endif /* _ASM_POWERPC_IO_EVENT_IRQ_H */
