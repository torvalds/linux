#   Copyright (C) 2001, 2004 Free Software Foundation, Inc.
#   Written By Nick Clifton
# 
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
# 
# In addition to the permissions in the GNU General Public License, the
# Free Software Foundation gives you unlimited permission to link the
# compiled version of this file with other programs, and to distribute
# those programs without any restriction coming from the use of this
# file.  (The General Public License restrictions do apply in other
# respects; for example, they cover modification of the file, and
# distribution when not linked into another program.)
# 
# This file is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 
#    As a special exception, if you link this library with files
#    compiled with GCC to produce an executable, this does not cause
#    the resulting executable to be covered by the GNU General Public License.
#    This exception does not however invalidate any other reasons why
#    the executable file might be covered by the GNU General Public License.
# 

# This file just makes sure that the .fini and .init sections do in
# fact return.  Users may put any desired instructions in those sections.
# This file is the last thing linked into any executable.

	# Note - this macro is complemented by the FUNC_START macro
	# in crti.asm.  If you change this macro you must also change
	# that macro match.
	#
	# Note - we do not try any fancy optimizations of the return
	# sequences here, it is just not worth it.  Instead keep things
	# simple.  Restore all the save resgisters, including the link
	# register and then perform the correct function return instruction.
	# We also save/restore r3 to ensure stack alignment.
.macro FUNC_END
#ifdef __thumb__
	.thumb
	
	pop	{r3, r4, r5, r6, r7}
	pop	{r3}
	mov	lr, r3
#else
	.arm
	
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, sl, fp, sp, lr}
#endif
	
#if defined __THUMB_INTERWORK__ || defined __thumb__
	bx	lr
#else
	mov	pc, lr
#endif
.endm
		
	
	.file		"crtn.asm"

	.section .note.GNU-stack,"",%progbits

	.section	".init"
	;;
	FUNC_END
	
	.section	".fini"
	;;
	FUNC_END
	
# end of crtn.asm
