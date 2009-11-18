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
 *  types.h Protocol defined base types
 */

#ifndef __TYPES_H__
#define __TYPES_H__

#include <bfa_os_inc.h>

#define wwn_t u64
#define lun_t u64

#define WWN_NULL	(0)
#define FC_SYMNAME_MAX	256	/*  max name server symbolic name size */
#define FC_ALPA_MAX	128

#pragma pack(1)

#define MAC_ADDRLEN	(6)
struct mac_s { u8 mac[MAC_ADDRLEN]; };
#define mac_t struct mac_s

#pragma pack()

#endif
