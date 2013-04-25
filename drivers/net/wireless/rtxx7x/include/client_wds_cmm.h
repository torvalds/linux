/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __CLIENT_WDS_CMM_H__
#define __CLIENT_WDS_CMM_H__

#include "rtmp_def.h"

#ifdef CLIENT_WDS


#define CLI_WDS_ENTRY_AGEOUT 5000  /* seconds */

#define CLIWDS_POOL_SIZE 128
#define CLIWDS_HASH_TAB_SIZE 64  /* the legth of hash table must be power of 2. */
typedef struct _CLIWDS_PROXY_ENTRY {
	struct _CLIWDS_PROXY_ENTRY * pNext;
	ULONG LastRefTime;
	SHORT Aid;
	UCHAR Addr[MAC_ADDR_LEN];
} CLIWDS_PROXY_ENTRY, *PCLIWDS_PROXY_ENTRY;

#endif /* CLIENT_WDS */

#endif /* __CLIENT_WDS_CMM_H__ */

