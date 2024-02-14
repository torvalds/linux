/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SCLP "store data in absolute storage"
 *
 * Copyright IBM Corp. 2003, 2013
 */

#ifndef SCLP_SDIAS_H
#define SCLP_SDIAS_H

#include "sclp.h"

#define SDIAS_EQ_STORE_DATA		0x0
#define SDIAS_EQ_SIZE			0x1
#define SDIAS_DI_FCP_DUMP		0x0
#define SDIAS_ASA_SIZE_32		0x0
#define SDIAS_ASA_SIZE_64		0x1
#define SDIAS_EVSTATE_ALL_STORED	0x0
#define SDIAS_EVSTATE_NO_DATA		0x3
#define SDIAS_EVSTATE_PART_STORED	0x10

struct sdias_evbuf {
	struct	evbuf_header hdr;
	u8	event_qual;
	u8	data_id;
	u64	reserved2;
	u32	event_id;
	u16	reserved3;
	u8	asa_size;
	u8	event_status;
	u32	reserved4;
	u32	blk_cnt;
	u64	asa;
	u32	reserved5;
	u32	fbn;
	u32	reserved6;
	u32	lbn;
	u16	reserved7;
	u16	dbs;
} __packed;

struct sdias_sccb {
	struct sccb_header	hdr;
	struct sdias_evbuf	evbuf;
} __packed;

#endif /* SCLP_SDIAS_H */
