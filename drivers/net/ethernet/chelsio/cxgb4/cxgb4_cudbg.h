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

enum CXGB4_ETHTOOL_DUMP_FLAGS {
	CXGB4_ETH_DUMP_NONE = ETH_FW_DUMP_DISABLE,
};

u32 cxgb4_get_dump_length(struct adapter *adap, u32 flag);
int cxgb4_cudbg_collect(struct adapter *adap, void *buf, u32 *buf_size,
			u32 flag);
void cxgb4_init_ethtool_dump(struct adapter *adapter);
#endif /* __CXGB4_CUDBG_H__ */
