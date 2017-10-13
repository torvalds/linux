/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#include "cxgb4.h"
#include "cxgb4_cudbg.h"

u32 cxgb4_get_dump_length(struct adapter *adap, u32 flag)
{
	return 0;
}

int cxgb4_cudbg_collect(struct adapter *adap, void *buf, u32 *buf_size,
			u32 flag)
{
	struct cudbg_init cudbg_init = { 0 };
	struct cudbg_buffer dbg_buff = { 0 };
	u32 size, min_size, total_size = 0;
	struct cudbg_hdr *cudbg_hdr;

	size = *buf_size;

	cudbg_init.adap = adap;
	cudbg_init.outbuf = buf;
	cudbg_init.outbuf_size = size;

	dbg_buff.data = buf;
	dbg_buff.size = size;
	dbg_buff.offset = 0;

	cudbg_hdr = (struct cudbg_hdr *)buf;
	cudbg_hdr->signature = CUDBG_SIGNATURE;
	cudbg_hdr->hdr_len = sizeof(struct cudbg_hdr);
	cudbg_hdr->major_ver = CUDBG_MAJOR_VERSION;
	cudbg_hdr->minor_ver = CUDBG_MINOR_VERSION;
	cudbg_hdr->max_entities = CUDBG_MAX_ENTITY;
	cudbg_hdr->chip_ver = adap->params.chip;
	cudbg_hdr->dump_type = CUDBG_DUMP_TYPE_MINI;
	cudbg_hdr->compress_type = CUDBG_COMPRESSION_NONE;

	min_size = sizeof(struct cudbg_hdr) +
		   sizeof(struct cudbg_entity_hdr) *
		   cudbg_hdr->max_entities;
	if (size < min_size)
		return -ENOMEM;

	dbg_buff.offset += min_size;
	total_size = dbg_buff.offset;

	cudbg_hdr->data_len = total_size;
	*buf_size = total_size;
	return 0;
}

void cxgb4_init_ethtool_dump(struct adapter *adapter)
{
	adapter->eth_dump.flag = CXGB4_ETH_DUMP_NONE;
	adapter->eth_dump.version = adapter->params.fw_vers;
	adapter->eth_dump.len = 0;
}
