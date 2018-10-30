/*
 * Copyright (C) 2014-2017 Broadcom
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _USB_BRCM_COMMON_INIT_H
#define _USB_BRCM_COMMON_INIT_H

#define USB_CTLR_MODE_HOST 0
#define USB_CTLR_MODE_DEVICE 1
#define USB_CTLR_MODE_DRD 2
#define USB_CTLR_MODE_TYPEC_PD 3

struct  brcm_usb_init_params;

struct  brcm_usb_init_params {
	void __iomem *ctrl_regs;
	void __iomem *xhci_ec_regs;
	int ioc;
	int ipp;
	int mode;
	u32 family_id;
	u32 product_id;
	int selected_family;
	const char *family_name;
	const u32 *usb_reg_bits_map;
};

void brcm_usb_set_family_map(struct brcm_usb_init_params *params);
int brcm_usb_init_get_dual_select(struct brcm_usb_init_params *params);
void brcm_usb_init_set_dual_select(struct brcm_usb_init_params *params,
				   int mode);

void brcm_usb_init_ipp(struct brcm_usb_init_params *ini);
void brcm_usb_init_common(struct brcm_usb_init_params *ini);
void brcm_usb_init_eohci(struct brcm_usb_init_params *ini);
void brcm_usb_init_xhci(struct brcm_usb_init_params *ini);
void brcm_usb_uninit_common(struct brcm_usb_init_params *ini);
void brcm_usb_uninit_eohci(struct brcm_usb_init_params *ini);
void brcm_usb_uninit_xhci(struct brcm_usb_init_params *ini);

#endif /* _USB_BRCM_COMMON_INIT_H */
