/******************************************************************************
 * arch/ia64/include/asm/xen/grant_table.h
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ASM_IA64_XEN_GRANT_TABLE_H
#define _ASM_IA64_XEN_GRANT_TABLE_H

struct vm_struct *xen_alloc_vm_area(unsigned long size);
void xen_free_vm_area(struct vm_struct *area);

#endif /* _ASM_IA64_XEN_GRANT_TABLE_H */
