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

u32 bfi_image_ct_size;
u32 bfi_image_cb_size;
u32 *bfi_image_ct;
u32 *bfi_image_cb;


#define	BFAD_FW_FILE_CT	"ctfw.bin"
#define	BFAD_FW_FILE_CB	"cbfw.bin"

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

	return(*bfi_image);

error:
	return(NULL);
}

u32 *
bfad_get_firmware_buf(struct pci_dev *pdev)
{
	if (pdev->device == BFA_PCI_DEVICE_ID_CT) {
		if (bfi_image_ct_size == 0)
			bfad_read_firmware(pdev, &bfi_image_ct,
				&bfi_image_ct_size, BFAD_FW_FILE_CT);
		return(bfi_image_ct);
	} else {
		if (bfi_image_cb_size == 0)
			bfad_read_firmware(pdev, &bfi_image_cb,
				&bfi_image_cb_size, BFAD_FW_FILE_CB);
		return(bfi_image_cb);
	}
}

u32 *
bfi_image_ct_get_chunk(u32 off)
{ return (u32 *)(bfi_image_ct + off); }

u32 *
bfi_image_cb_get_chunk(u32 off)
{ return (u32 *)(bfi_image_cb + off); }

