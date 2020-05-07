/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef _RTL8821CS_IO_H_
#define _RTL8821CS_IO_H_

#include <drv_types.h>		/* struct dvobj_priv and etc. */

u32 rtl8821cs_write_port(struct dvobj_priv *, u32 cnt, u8 *mem);

#endif /* _RTL8821CS_IO_H_ */
