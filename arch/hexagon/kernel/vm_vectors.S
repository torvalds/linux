/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Event jump tables
 *
 * Copyright (c) 2010-2012,2013, The Linux Foundation. All rights reserved.
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
	jump _K_enter_debug;
	jump 1b;  /*  4 Rsvd  */
	jump _K_enter_trap0;
	jump 1b;  /*  6 Rsvd  */
	jump _K_enter_interrupt;
