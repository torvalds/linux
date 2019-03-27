/* crtn.s for eabi
   Copyright (C) 1996, 2000 Free Software Foundation, Inc.
   Written By Michael Meissner

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

   As a special exception, if you link this library with files
   compiled with GCC to produce an executable, this does not cause
   the resulting executable to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.

  */

/* This file just supplies labeled ending points for the .got* and other
   special sections.  It is linked in last after other modules.  */
 
	.file	"crtn.s"
	.ident	"GNU C crtn.s"

#ifndef __powerpc64__
	.section ".got","aw"
	.globl	__GOT_END__
	.type	__GOT_END__,@object
__GOT_END__:

	.section ".got1","aw"
	.globl	__GOT1_END__
	.type	__GOT1_END__,@object
__GOT1_END__:

	.section ".got2","aw"
	.globl	__GOT2_END__
	.type	__GOT2_END__,@object
__GOT2_END__:

	.section ".fixup","aw"
	.globl	__FIXUP_END__
	.type	__FIXUP_END__,@object
__FIXUP_END__:

	.section ".ctors","aw"
	.globl	__CTOR_END__
	.type	__CTOR_END__,@object
__CTOR_END__:

	.section ".dtors","aw"
	.globl	__DTOR_END__
	.type	__DTOR_END__,@object
__DTOR_END__:

	.section ".sdata","aw"
	.globl	__SDATA_END__
	.type	__SDATA_END__,@object
__SDATA_END__:

	.section ".sbss","aw",@nobits
	.globl	__SBSS_END__
	.type	__SBSS_END__,@object
__SBSS_END__:

	.section ".sdata2","a"
	.globl	__SDATA2_END__
	.type	__SDATA2_END__,@object
__SDATA2_END__:

	.section ".sbss2","a"
	.globl	__SBSS2_END__
	.type	__SBSS2_END__,@object
__SBSS2_END__:

	.section ".gcc_except_table","aw"
	.globl	__EXCEPT_END__
	.type	__EXCEPT_END__,@object
__EXCEPT_END__:

	.section ".eh_frame","aw"
	.globl	__EH_FRAME_END__
	.type	__EH_FRAME_END__,@object
__EH_FRAME_END__:
        .long   0

/* Tail of __init function used for static constructors.  */
	.section ".init","ax"
	lwz 0,20(1)
	mtlr 0
	addi 1,1,16
	blr

/* Tail of __fini function used for static destructors.  */
	.section ".fini","ax"
	lwz 0,20(1)
	mtlr 0
	addi 1,1,16
	blr
#endif
