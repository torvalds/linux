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

/**
 * BFI FW image type
 */
enum {
	BFI_IMAGE_CB_FC,
	BFI_IMAGE_CT_FC,
	BFI_IMAGE_CT_CNA,
	BFI_IMAGE_MAX,
};

extern u32 *bfi_image_get_chunk(int type, uint32_t off);
extern u32 bfi_image_get_size(int type);
extern u32 bfi_image_ct_fc_size;
extern u32 bfi_image_ct_cna_size;
extern u32 bfi_image_cb_fc_size;
extern u32 *bfi_image_ct_fc;
extern u32 *bfi_image_ct_cna;
extern u32 *bfi_image_cb_fc;


#endif /* __BFA_FWIMG_PRIV_H__ */
