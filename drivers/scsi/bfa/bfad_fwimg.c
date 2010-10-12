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

/**
 *  bfad_fwimg.c Linux driver PCI interface module.
 */
#include <bfa_os_inc.h>
#include <bfad_drv.h>
#include <bfad_im_compat.h>
#include <defs/bfa_defs_version.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <asm/fcntl.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <bfa_fwimg_priv.h>
#include <bfa.h>

u32 bfi_image_ct_fc_size;
u32 bfi_image_ct_cna_size;
u32 bfi_image_cb_fc_size;
u32 *bfi_image_ct_fc;
u32 *bfi_image_ct_cna;
u32 *bfi_image_cb_fc;


#define	BFAD_FW_FILE_CT_FC	"ctfw_fc.bin"
#define	BFAD_FW_FILE_CT_CNA	"ctfw_cna.bin"
#define	BFAD_FW_FILE_CB_FC	"cbfw_fc.bin"
MODULE_FIRMWARE(BFAD_FW_FILE_CT_FC);
MODULE_FIRMWARE(BFAD_FW_FILE_CT_CNA);
MODULE_FIRMWARE(BFAD_FW_FILE_CB_FC);

u32 *
bfad_read_firmware(struct pci_dev *pdev, u32 **bfi_image,
			u32 *bfi_image_size, char *fw_name)
{
	const struct firmware *fw;

	if (request_firmware(&fw, fw_name, &pdev->dev)) {
		printk(KERN_ALERT "Can't locate firmware %s\n", fw_name);
		goto error;
	}

	*bfi_image = vmalloc(fw->size);
	if (NULL == *bfi_image) {
		printk(KERN_ALERT "Fail to allocate buffer for fw image "
			"size=%x!\n", (u32) fw->size);
		goto error;
	}

	memcpy(*bfi_image, fw->data, fw->size);
	*bfi_image_size = fw->size/sizeof(u32);

	return *bfi_image;

error:
	return NULL;
}

u32 *
bfad_get_firmware_buf(struct pci_dev *pdev)
{
	if (pdev->device == BFA_PCI_DEVICE_ID_CT_FC) {
		if (bfi_image_ct_fc_size == 0)
			bfad_read_firmware(pdev, &bfi_image_ct_fc,
				&bfi_image_ct_fc_size, BFAD_FW_FILE_CT_FC);
		return bfi_image_ct_fc;
	} else if (pdev->device == BFA_PCI_DEVICE_ID_CT) {
		if (bfi_image_ct_cna_size == 0)
			bfad_read_firmware(pdev, &bfi_image_ct_cna,
				&bfi_image_ct_cna_size, BFAD_FW_FILE_CT_CNA);
		return bfi_image_ct_cna;
	} else {
		if (bfi_image_cb_fc_size == 0)
			bfad_read_firmware(pdev, &bfi_image_cb_fc,
				&bfi_image_cb_fc_size, BFAD_FW_FILE_CB_FC);
		return bfi_image_cb_fc;
	}
}

u32 *
bfi_image_ct_fc_get_chunk(u32 off)
{ return (u32 *)(bfi_image_ct_fc + off); }

u32 *
bfi_image_ct_cna_get_chunk(u32 off)
{ return (u32 *)(bfi_image_ct_cna + off); }

u32 *
bfi_image_cb_fc_get_chunk(u32 off)
{ return (u32 *)(bfi_image_cb_fc + off); }

uint32_t *
bfi_image_get_chunk(int type, uint32_t off)
{
	switch (type) {
	case BFI_IMAGE_CT_FC: return bfi_image_ct_fc_get_chunk(off); break;
	case BFI_IMAGE_CT_CNA: return bfi_image_ct_cna_get_chunk(off); break;
	case BFI_IMAGE_CB_FC: return bfi_image_cb_fc_get_chunk(off); break;
	default: return 0; break;
	}
}

uint32_t
bfi_image_get_size(int type)
{
	switch (type) {
	case BFI_IMAGE_CT_FC: return bfi_image_ct_fc_size; break;
	case BFI_IMAGE_CT_CNA: return bfi_image_ct_cna_size; break;
	case BFI_IMAGE_CB_FC: return bfi_image_cb_fc_size; break;
	default: return 0; break;
	}
}
