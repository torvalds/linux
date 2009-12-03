/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFA_FWIMG_PRIV_H__
#define __BFA_FWIMG_PRIV_H__

#define	BFI_FLASH_CHUNK_SZ		256	/*  Flash chunk size */
#define	BFI_FLASH_CHUNK_SZ_WORDS	(BFI_FLASH_CHUNK_SZ/sizeof(u32))

extern u32 *bfi_image_ct_get_chunk(u32 off);
extern u32 bfi_image_ct_size;
extern u32 *bfi_image_cb_get_chunk(u32 off);
extern u32 bfi_image_cb_size;
extern u32 *bfi_image_cb;
extern u32 *bfi_image_ct;

#endif /* __BFA_FWIMG_PRIV_H__ */
