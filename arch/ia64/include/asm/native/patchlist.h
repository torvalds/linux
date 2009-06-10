/******************************************************************************
 * arch/ia64/include/asm/native/inst.h
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

#define __paravirt_start_gate_fsyscall_patchlist		\
	__ia64_native_start_gate_fsyscall_patchlist
#define __paravirt_end_gate_fsyscall_patchlist			\
	__ia64_native_end_gate_fsyscall_patchlist
#define __paravirt_start_gate_brl_fsys_bubble_down_patchlist	\
	__ia64_native_start_gate_brl_fsys_bubble_down_patchlist
#define __paravirt_end_gate_brl_fsys_bubble_down_patchlist	\
	__ia64_native_end_gate_brl_fsys_bubble_down_patchlist
#define __paravirt_start_gate_vtop_patchlist			\
	__ia64_native_start_gate_vtop_patchlist
#define __paravirt_end_gate_vtop_patchlist			\
	__ia64_native_end_gate_vtop_patchlist
#define __paravirt_start_gate_mckinley_e9_patchlist		\
	__ia64_native_start_gate_mckinley_e9_patchlist
#define __paravirt_end_gate_mckinley_e9_patchlist		\
	__ia64_native_end_gate_mckinley_e9_patchlist
