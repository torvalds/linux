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
/*
 * bfi_boot.h
 */

#ifndef __BFI_BOOT_H__
#define __BFI_BOOT_H__

#define BFI_BOOT_TYPE_OFF		8
#define BFI_BOOT_PARAM_OFF		12

#define BFI_BOOT_TYPE_NORMAL 		0	/* param is device id */
#define	BFI_BOOT_TYPE_FLASH		1
#define	BFI_BOOT_TYPE_MEMTEST		2

#define BFI_BOOT_MEMTEST_RES_ADDR   0x900
#define BFI_BOOT_MEMTEST_RES_SIG    0xA0A1A2A3

#endif
