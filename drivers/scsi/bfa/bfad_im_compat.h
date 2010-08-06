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

#ifndef __BFAD_IM_COMPAT_H__
#define __BFAD_IM_COMPAT_H__

extern struct device_attribute *bfad_im_host_attrs[];
extern struct device_attribute *bfad_im_vport_attrs[];

u32 *bfad_get_firmware_buf(struct pci_dev *pdev);
u32 *bfad_read_firmware(struct pci_dev *pdev, u32 **bfi_image,
			u32 *bfi_image_size, char *fw_name);

static inline u32 *
bfad_load_fwimg(struct pci_dev *pdev)
{
	return bfad_get_firmware_buf(pdev);
}

static inline void
bfad_free_fwimg(void)
{
	if (bfi_image_ct_fc_size && bfi_image_ct_fc)
		vfree(bfi_image_ct_fc);
	if (bfi_image_ct_cna_size && bfi_image_ct_cna)
		vfree(bfi_image_ct_cna);
	if (bfi_image_cb_fc_size && bfi_image_cb_fc)
		vfree(bfi_image_cb_fc);
}

#endif
