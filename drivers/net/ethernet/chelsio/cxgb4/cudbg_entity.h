/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#ifndef __CUDBG_ENTITY_H__
#define __CUDBG_ENTITY_H__

#define EDC0_FLAG 3
#define EDC1_FLAG 4

struct card_mem {
	u16 size_edc0;
	u16 size_edc1;
	u16 mem_flag;
};
#endif /* __CUDBG_ENTITY_H__ */
