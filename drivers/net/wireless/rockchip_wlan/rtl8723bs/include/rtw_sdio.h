/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
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
#ifndef _RTW_SDIO_H_
#define _RTW_SDIO_H_

#include <drv_types.h>		/* struct dvobj_priv and etc. */

u8 rtw_sdio_read_cmd52(struct dvobj_priv *, u32 addr, void *buf, size_t len);
u8 rtw_sdio_read_cmd53(struct dvobj_priv *, u32 addr, void *buf, size_t len);
u8 rtw_sdio_write_cmd52(struct dvobj_priv *, u32 addr, void *buf, size_t len);
u8 rtw_sdio_write_cmd53(struct dvobj_priv *, u32 addr, void *buf, size_t len);
u8 rtw_sdio_f0_read(struct dvobj_priv *, u32 addr, void *buf, size_t len);

#endif /* _RTW_SDIO_H_ */
