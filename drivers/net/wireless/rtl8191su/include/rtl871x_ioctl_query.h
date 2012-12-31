/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#ifndef __IOCTL_QUERY_H
#define __IOCTL_QUERY_H

#include <drv_conf.h>
#include <drv_types.h>


#ifdef PLATFORM_WINDOWS

u8 query_802_11_capability(_adapter*	padapter,u8*	pucBuf,u32 *	pulOutLen);
u8 query_802_11_association_information (_adapter * padapter, PNDIS_802_11_ASSOCIATION_INFORMATION pAssocInfo);

#endif


#endif

