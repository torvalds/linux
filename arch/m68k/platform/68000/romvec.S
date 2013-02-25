/*
 * romvec.S - Vector table for 68000 cpus
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright 1996 Roman Zippel
 * Copyright 1999 D. Jeff Dionne <jeff@rt-control.com>
 * Copyright 2006 Greg Ungerer <gerg@snapgear.com>
 */

.global _start
.global _buserr
.global trap
.global system_call

.section .romvec

e_vectors:
.long CONFIG_RAMBASE+CONFIG_RAMSIZE-4, _start, buserr, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
/* TRAP #0-15 */
.long system_call, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long trap, trap, trap, trap
.long 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

