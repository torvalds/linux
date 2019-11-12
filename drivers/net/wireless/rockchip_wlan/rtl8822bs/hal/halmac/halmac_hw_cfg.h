/******************************************************************************
 *
 * Copyright(c) 2016 - 2018 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/

#ifndef __HALMAC__HW_CFG_H__
#define __HALMAC__HW_CFG_H__

#include <drv_conf.h>	/* CONFIG_[IC], CONFIG_[INTF]_HCI */

#ifdef CONFIG_RTL8723A
#define HALMAC_8723A_SUPPORT	1
#else
#define HALMAC_8723A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8188E
#define HALMAC_8188E_SUPPORT	1
#else
#define HALMAC_8188E_SUPPORT	0
#endif

#ifdef CONFIG_RTL8821A
#define HALMAC_8821A_SUPPORT	1
#else
#define HALMAC_8821A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8723B
#define HALMAC_8723B_SUPPORT	1
#else
#define HALMAC_8723B_SUPPORT	0
#endif

#ifdef CONFIG_RTL8812A
#define HALMAC_8812A_SUPPORT	1
#else
#define HALMAC_8812A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8192E
#define HALMAC_8192E_SUPPORT	1
#else
#define HALMAC_8192E_SUPPORT	0
#endif

#ifdef CONFIG_RTL8881A
#define HALMAC_8881A_SUPPORT	1
#else
#define HALMAC_8881A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8821B
#define HALMAC_8821B_SUPPORT	1
#else
#define HALMAC_8821B_SUPPORT	0
#endif

#ifdef CONFIG_RTL8814A
#define HALMAC_8814A_SUPPORT	1
#else
#define HALMAC_8814A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8881A
#define HALMAC_8881A_SUPPORT	1
#else
#define HALMAC_8881A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8703B
#define HALMAC_8703B_SUPPORT	1
#else
#define HALMAC_8703B_SUPPORT	0
#endif

#ifdef CONFIG_RTL8723D
#define HALMAC_8723D_SUPPORT	1
#else
#define HALMAC_8723D_SUPPORT	0
#endif

#ifdef CONFIG_RTL8188F
#define HALMAC_8188F_SUPPORT	1
#else
#define HALMAC_8188F_SUPPORT	0
#endif

#ifdef CONFIG_RTL8821BMP
#define HALMAC_8821BMP_SUPPORT	1
#else
#define HALMAC_8821BMP_SUPPORT	0
#endif

#ifdef CONFIG_RTL8814AMP
#define HALMAC_8814AMP_SUPPORT	1
#else
#define HALMAC_8814AMP_SUPPORT	0
#endif

#ifdef CONFIG_RTL8195A
#define HALMAC_8195A_SUPPORT	1
#else
#define HALMAC_8195A_SUPPORT	0
#endif

#ifdef CONFIG_RTL8821B
#define HALMAC_8821B_SUPPORT	1
#else
#define HALMAC_8821B_SUPPORT	0
#endif

#ifdef CONFIG_RTL8196F
#define HALMAC_8196F_SUPPORT	1
#else
#define HALMAC_8196F_SUPPORT	0
#endif

#ifdef CONFIG_RTL8197F
#define HALMAC_8197F_SUPPORT	1
#else
#define HALMAC_8197F_SUPPORT	0
#endif

#ifdef CONFIG_RTL8198F
#define HALMAC_8198F_SUPPORT	1
#else
#define HALMAC_8198F_SUPPORT	0
#endif

#ifdef CONFIG_RTL8192F
#define HALMAC_8192F_SUPPORT	1
#else
#define HALMAC_8192F_SUPPORT	0
#endif

#ifdef CONFIG_RTL8197G
#define HALMAC_8197G_SUPPORT	1
#else
#define HALMAC_8197G_SUPPORT	0
#endif



/* Halmac support IC version */

#ifdef CONFIG_RTL8814B
#define HALMAC_8814B_SUPPORT	1
#else
#define HALMAC_8814B_SUPPORT	0
#endif

#ifdef CONFIG_RTL8821C
#define HALMAC_8821C_SUPPORT	1
#else
#define HALMAC_8821C_SUPPORT	0
#endif

#ifdef CONFIG_RTL8822B
#define HALMAC_8822B_SUPPORT	1
#else
#define HALMAC_8822B_SUPPORT	0
#endif

#ifdef CONFIG_RTL8822C
#define HALMAC_8822C_SUPPORT	1
#else
#define HALMAC_8822C_SUPPORT	0
#endif

#ifdef CONFIG_RTL8812F
#define HALMAC_8812F_SUPPORT	1
#else
#define HALMAC_8812F_SUPPORT	0
#endif


/* Interface support */
#ifdef CONFIG_SDIO_HCI
#define HALMAC_SDIO_SUPPORT	1
#else
#define HALMAC_SDIO_SUPPORT	0
#endif
#ifdef CONFIG_USB_HCI
#define HALMAC_USB_SUPPORT	1
#else
#define HALMAC_USB_SUPPORT	0
#endif
#ifdef CONFIG_PCI_HCI
#define HALMAC_PCIE_SUPPORT	1
#else
#define HALMAC_PCIE_SUPPORT	0
#endif

#endif /* __HALMAC__HW_CFG_H__ */


