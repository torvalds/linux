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
#ifndef __BFA_DEFS_TYPES_H__
#define __BFA_DEFS_TYPES_H__

#include <bfa_os_inc.h>

enum bfa_boolean {
	BFA_FALSE = 0,
	BFA_TRUE  = 1
};
#define bfa_boolean_t enum bfa_boolean

#define BFA_STRING_32	32

#endif /* __BFA_DEFS_TYPES_H__ */
