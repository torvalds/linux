/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */


#ifndef MEI_VERSION_H
#define MEI_VERSION_H

#define MAJOR_VERSION		7
#define MINOR_VERSION		1
#define QUICK_FIX_NUMBER	20
#define VER_BUILD		1

#define MEI_DRV_VER1 __stringify(MAJOR_VERSION) "." __stringify(MINOR_VERSION)
#define MEI_DRV_VER2 __stringify(QUICK_FIX_NUMBER) "." __stringify(VER_BUILD)

#define MEI_DRIVER_VERSION	MEI_DRV_VER1 "." MEI_DRV_VER2

#endif
