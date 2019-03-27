/*  This file contains the vector save and restore routines.
 *
 *   Copyright (C) 2004 Free Software Foundation, Inc.
 * 
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *  As a special exception, if you link this library with files
 *  compiled with GCC to produce an executable, this does not cause the
 *  resulting executable to be covered by the GNU General Public License.
 *  This exception does not however invalidate any other reasons why the
 *  executable file might be covered by the GNU General Public License.
 */ 

/* Vector save/restore routines for Darwin.  Note that each vector
   save/restore requires 2 instructions (8 bytes.)

   THE SAVE AND RESTORE ROUTINES CAN HAVE ONLY ONE GLOBALLY VISIBLE
   ENTRY POINT - callers have to jump to "saveFP+60" to save f29..f31,
   for example.  For FP reg saves/restores, it takes one instruction
   (4 bytes) to do the operation; for Vector regs, 2 instructions are
   required (8 bytes.).   */

	.machine ppc7400
.text
	.align 2

.private_extern saveVEC
saveVEC:
	li r11,-192
	stvx v20,r11,r0
	li r11,-176
	stvx v21,r11,r0
	li r11,-160
	stvx v22,r11,r0
	li r11,-144
	stvx v23,r11,r0
	li r11,-128
	stvx v24,r11,r0
	li r11,-112
	stvx v25,r11,r0
	li r11,-96
	stvx v26,r11,r0
	li r11,-80
	stvx v27,r11,r0
	li r11,-64
	stvx v28,r11,r0
	li r11,-48
	stvx v29,r11,r0
	li r11,-32
	stvx v30,r11,r0
	li r11,-16
	stvx v31,r11,r0
	blr

.private_extern restVEC
restVEC:
	li r11,-192
	lvx v20,r11,r0
	li r11,-176
	lvx v21,r11,r0
	li r11,-160
	lvx v22,r11,r0
	li r11,-144
	lvx v23,r11,r0
	li r11,-128
	lvx v24,r11,r0
	li r11,-112
	lvx v25,r11,r0
	li r11,-96
	lvx v26,r11,r0
	li r11,-80
	lvx v27,r11,r0
	li r11,-64
	lvx v28,r11,r0
	li r11,-48
	lvx v29,r11,r0
	li r11,-32
	lvx v30,r11,r0
	li r11,-16
	lvx v31,r11,r0
	blr

/* saveVEC_vr11 -- as saveVEC but VRsave is returned in R11.  */

.private_extern saveVEC_vr11
saveVEC_vr11:
	li r11,-192
	stvx v20,r11,r0
	li r11,-176
	stvx v21,r11,r0
	li r11,-160
	stvx v22,r11,r0
	li r11,-144
	stvx v23,r11,r0
	li r11,-128
	stvx v24,r11,r0
	li r11,-112
	stvx v25,r11,r0
	li r11,-96
	stvx v26,r11,r0
	li r11,-80
	stvx v27,r11,r0
	li r11,-64
	stvx v28,r11,r0
	li r11,-48
	stvx v29,r11,r0
	li r11,-32
	stvx v30,r11,r0
	li r11,-16
	stvx v31,r11,r0
	mfspr r11,VRsave
	blr

/* As restVec, but the original VRsave value passed in R10.  */

.private_extern restVEC_vr10
restVEC_vr10:
	li r11,-192
	lvx v20,r11,r0
	li r11,-176
	lvx v21,r11,r0
	li r11,-160
	lvx v22,r11,r0
	li r11,-144
	lvx v23,r11,r0
	li r11,-128
	lvx v24,r11,r0
	li r11,-112
	lvx v25,r11,r0
	li r11,-96
	lvx v26,r11,r0
	li r11,-80
	lvx v27,r11,r0
	li r11,-64
	lvx v28,r11,r0
	li r11,-48
	lvx v29,r11,r0
	li r11,-32
	lvx v30,r11,r0
	li r11,-16
	lvx v31,r11,r0
				/* restore VRsave from R10.  */
	mtspr VRsave,r10
	blr
