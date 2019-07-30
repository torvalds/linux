// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */

#include "global.h"

int viafb_ioctl_get_viafb_info(u_long arg)
{
	struct viafb_ioctl_info viainfo;

	memset(&viainfo, 0, sizeof(struct viafb_ioctl_info));

	viainfo.viafb_id = VIAID;
	viainfo.vendor_id = PCI_VIA_VENDOR_ID;

	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_CLE266:
		viainfo.device_id = UNICHROME_CLE266_DID;
		break;

	case UNICHROME_K400:
		viainfo.device_id = UNICHROME_K400_DID;
		break;

	case UNICHROME_K800:
		viainfo.device_id = UNICHROME_K800_DID;
		break;

	case UNICHROME_PM800:
		viainfo.device_id = UNICHROME_PM800_DID;
		break;

	case UNICHROME_CN700:
		viainfo.device_id = UNICHROME_CN700_DID;
		break;

	case UNICHROME_CX700:
		viainfo.device_id = UNICHROME_CX700_DID;
		break;

	case UNICHROME_K8M890:
		viainfo.device_id = UNICHROME_K8M890_DID;
		break;

	case UNICHROME_P4M890:
		viainfo.device_id = UNICHROME_P4M890_DID;
		break;

	case UNICHROME_P4M900:
		viainfo.device_id = UNICHROME_P4M900_DID;
		break;
	}

	viainfo.version = VERSION_MAJOR;
	viainfo.revision = VERSION_MINOR;

	if (copy_to_user((void __user *)arg, &viainfo, sizeof(viainfo)))
		return -EFAULT;

	return 0;
}

/* Hot-Plug Priority: DVI > CRT*/
int viafb_ioctl_hotplug(int hres, int vres, int bpp)
{
	int DVIsense, status = 0;
	DEBUG_MSG(KERN_INFO "viafb_ioctl_hotplug!!\n");

	if (viaparinfo->chip_info->tmds_chip_info.tmds_chip_name !=
		NON_TMDS_TRANSMITTER) {
		DVIsense = viafb_dvi_sense();

		if (DVIsense) {
			DEBUG_MSG(KERN_INFO "DVI Attached...\n");
			if (viafb_DeviceStatus != DVI_Device) {
				viafb_DVI_ON = 1;
				viafb_CRT_ON = 0;
				viafb_LCD_ON = 0;
				viafb_DeviceStatus = DVI_Device;
				viafb_set_iga_path();
				return viafb_DeviceStatus;
			}
			status = 1;
		} else
			DEBUG_MSG(KERN_INFO "DVI De-attached...\n");
	}

	if ((viafb_DeviceStatus != CRT_Device) && (status == 0)) {
		viafb_CRT_ON = 1;
		viafb_DVI_ON = 0;
		viafb_LCD_ON = 0;

		viafb_DeviceStatus = CRT_Device;
		viafb_set_iga_path();
		return viafb_DeviceStatus;
	}

	return 0;
}
