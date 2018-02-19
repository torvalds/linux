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
#include "cudbg_if.h"
#include "cudbg_lib_common.h"

int cudbg_get_buff(struct cudbg_buffer *pdbg_buff, u32 size,
		   struct cudbg_buffer *pin_buff)
{
	u32 offset;

	offset = pdbg_buff->offset;
	if (offset + size > pdbg_buff->size)
		return CUDBG_STATUS_NO_MEM;

	pin_buff->data = (char *)pdbg_buff->data + offset;
	pin_buff->offset = offset;
	pin_buff->size = size;
	pdbg_buff->size -= size;
	return 0;
}

void cudbg_put_buff(struct cudbg_buffer *pin_buff,
		    struct cudbg_buffer *pdbg_buff)
{
	pdbg_buff->size += pin_buff->size;
	pin_buff->data = NULL;
	pin_buff->offset = 0;
	pin_buff->size = 0;
}

void cudbg_update_buff(struct cudbg_buffer *pin_buff,
		       struct cudbg_buffer *pout_buff)
{
	/* We already write to buffer provided by ethool, so just
	 * increment offset to next free space.
	 */
	pout_buff->offset += pin_buff->size;
}
