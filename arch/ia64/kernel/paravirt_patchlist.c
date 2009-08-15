/******************************************************************************
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

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/paravirt.h>

#define DECLARE(name)						\
	extern unsigned long					\
		__ia64_native_start_gate_##name##_patchlist[];	\
	extern unsigned long					\
		__ia64_native_end_gate_##name##_patchlist[]

DECLARE(fsyscall);
DECLARE(brl_fsys_bubble_down);
DECLARE(vtop);
DECLARE(mckinley_e9);

extern unsigned long __start_gate_section[];

#define ASSIGN(name)							    \
	.start_##name##_patchlist =					    \
		(unsigned long)__ia64_native_start_gate_##name##_patchlist, \
	.end_##name##_patchlist =					    \
		(unsigned long)__ia64_native_end_gate_##name##_patchlist

struct pv_patchdata pv_patchdata __initdata = {
	ASSIGN(fsyscall),
	ASSIGN(brl_fsys_bubble_down),
	ASSIGN(vtop),
	ASSIGN(mckinley_e9),

	.gate_section = (void*)__start_gate_section,
};


unsigned long __init
paravirt_get_gate_patchlist(enum pv_gate_patchlist type)
{

#define CASE(NAME, name)					\
	case PV_GATE_START_##NAME:				\
		return pv_patchdata.start_##name##_patchlist;	\
	case PV_GATE_END_##NAME:				\
		return pv_patchdata.end_##name##_patchlist;	\

	switch (type) {
		CASE(FSYSCALL, fsyscall);
		CASE(BRL_FSYS_BUBBLE_DOWN, brl_fsys_bubble_down);
		CASE(VTOP, vtop);
		CASE(MCKINLEY_E9, mckinley_e9);
	default:
		BUG();
		break;
	}
	return 0;
}

void * __init
paravirt_get_gate_section(void)
{
	return pv_patchdata.gate_section;
}
