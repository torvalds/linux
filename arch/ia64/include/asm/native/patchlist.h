/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 * arch/ia64/include/asm/native/inst.h
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
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
