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

#ifndef __CXGB4_CUDBG_H__
#define __CXGB4_CUDBG_H__

#include "cudbg_if.h"
#include "cudbg_lib_common.h"
#include "cudbg_entity.h"
#include "cudbg_lib.h"

#define CUDBG_DUMP_BUFF_SIZE (32 * 1024 * 1024) /* 32 MB */
#define CUDBG_COMPRESS_BUFF_SIZE (4 * 1024 * 1024) /* 4 MB */

typedef int (*cudbg_collect_callback_t)(struct cudbg_init *pdbg_init,
					struct cudbg_buffer *dbg_buff,
					struct cudbg_error *cudbg_err);

struct cxgb4_collect_entity {
	enum cudbg_dbg_entity_type entity;
	cudbg_collect_callback_t collect_cb;
};

enum CXGB4_ETHTOOL_DUMP_FLAGS {
	CXGB4_ETH_DUMP_NONE = ETH_FW_DUMP_DISABLE,
	CXGB4_ETH_DUMP_MEM = (1 << 0), /* On-Chip Memory Dumps */
	CXGB4_ETH_DUMP_HW = (1 << 1), /* various FW and HW dumps */
};

u32 cxgb4_get_dump_length(struct adapter *adap, u32 flag);
int cxgb4_cudbg_collect(struct adapter *adap, void *buf, u32 *buf_size,
			u32 flag);
void cxgb4_init_ethtool_dump(struct adapter *adapter);
#endif /* __CXGB4_CUDBG_H__ */
