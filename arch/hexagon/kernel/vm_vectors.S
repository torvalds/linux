/*
 * Event jump tables
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <asm/hexagon_vm.h>

.text

/*  This is registered early on to allow angel  */
.global _K_provisional_vec
_K_provisional_vec:
	jump 1f;
	jump 1f;
	jump 1f;
	jump 1f;
	jump 1f;
	trap1(#HVM_TRAP1_VMRTE)
	jump 1f;
	jump 1f;


.global _K_VM_event_vector
_K_VM_event_vector:
1:
	jump 1b;  /*  Reset  */
	jump _K_enter_machcheck;
	jump _K_enter_genex;
	jump 1b;  /*  3 Rsvd  */
	jump 1b;  /*  4 Rsvd  */
	jump _K_enter_trap0;
	jump 1b;  /*  6 Rsvd  */
	jump _K_enter_interrupt;
