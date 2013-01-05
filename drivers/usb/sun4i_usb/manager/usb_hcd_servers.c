/*
 * drivers/usb/sun4i_usb/manager/usb_hcd_servers.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include  "../include/sw_usb_config.h"
#include  "usb_hcd_servers.h"

int sw_usb_disable_ehci(__u32 usbc_no);
int sw_usb_enable_ehci(__u32 usbc_no);
int sw_usb_disable_ohci(__u32 usbc_no);
int sw_usb_enable_ohci(__u32 usbc_no);

/*
*******************************************************************************
*                     sw_usb_disable_hcd
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
int sw_usb_disable_hcd(__u32 usbc_no)
{
	if(usbc_no == 0){
#if defined(CONFIG_USB_SW_SUN4I_USB0_OTG) || defined(USB_SW_SUN4I_USB0_HOST_ONLY)
		sw_usb_disable_hcd0();
#endif
	} else if (usbc_no == 1 || usbc_no == 2) {
#if defined(CONFIG_USB_SUNXI_EHCI)
		sw_usb_disable_ehci(usbc_no);
#endif
#if defined(CONFIG_USB_SUNXI_OHCI)
		sw_usb_disable_ohci(usbc_no);
#endif
	}else{
		DMSG_PANIC("ERR: unkown usbc_no(%d)\n", usbc_no);
		return -1;
	}

    return 0;
}
EXPORT_SYMBOL(sw_usb_disable_hcd);

/*
*******************************************************************************
*                     sw_usb_enable_hcd
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
int sw_usb_enable_hcd(__u32 usbc_no)
{
	if(usbc_no == 0){
#if defined(CONFIG_USB_SW_SUN4I_USB0_OTG) || defined(USB_SW_SUN4I_USB0_HOST_ONLY)
		sw_usb_enable_hcd0();
#endif
	} else if (usbc_no == 1 || usbc_no == 2) {
#if defined(CONFIG_USB_SUNXI_EHCI)
		sw_usb_enable_ehci(usbc_no);
#endif
#if defined(CONFIG_USB_SUNXI_OHCI)
		sw_usb_enable_ohci(usbc_no);
#endif
	}else{
		DMSG_PANIC("ERR: unkown usbc_no(%d)\n", usbc_no);
		return -1;
	}

    return 0;
}
EXPORT_SYMBOL(sw_usb_enable_hcd);
