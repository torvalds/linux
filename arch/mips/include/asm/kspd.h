/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */

#ifndef _ASM_KSPD_H
#define _ASM_KSPD_H

struct kspd_notifications {
	void (*kspd_sp_exit)(int sp_id);

	struct list_head list;
};

#ifdef CONFIG_MIPS_APSP_KSPD
extern void kspd_notify(struct kspd_notifications *notify);
#else
static inline void kspd_notify(struct kspd_notifications *notify)
{
}
#endif

#endif
