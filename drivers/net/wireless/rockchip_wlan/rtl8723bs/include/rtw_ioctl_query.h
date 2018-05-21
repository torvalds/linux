/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef _RTW_IOCTL_QUERY_H_
#define _RTW_IOCTL_QUERY_H_


#ifdef PLATFORM_WINDOWS
u8 query_802_11_capability(_adapter	*padapter, u8 *pucBuf, u32 *pulOutLen);
u8 query_802_11_association_information(_adapter *padapter, PNDIS_802_11_ASSOCIATION_INFORMATION pAssocInfo);
#endif


#endif
