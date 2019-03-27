# crti.s for sysv4

#   Copyright (C) 1996 Free Software Foundation, Inc.
#   Written By Michael Meissner
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

# This file just supplies labeled starting points for the .got* and other
# special sections.  It is linked in first before other modules.
 
	.file	"scrti.s"
	.ident	"GNU C scrti.s"

#ifndef __powerpc64__
# Start of .text
	.section ".text"
	.globl	_ex_text0
_ex_text0:

# Exception range
	.section ".exception_ranges","aw"
	.globl	_ex_range0
_ex_range0:

# List of C++ constructors
	.section ".ctors","aw"
	.globl	__CTOR_LIST__
	.type	__CTOR_LIST__,@object
__CTOR_LIST__:

# List of C++ destructors
	.section ".dtors","aw"
	.globl	__DTOR_LIST__
	.type	__DTOR_LIST__,@object
__DTOR_LIST__:

# Head of _init function used for static constructors
	.section ".init","ax"
	.align 2
	.globl _init
	.type _init,@function
_init:	stwu	%r1,-16(%r1)
	mflr	%r0
	stw	%r31,12(%r1)
	stw	%r0,16(%r1)

	bl	_GLOBAL_OFFSET_TABLE_-4	# get the GOT address
	mflr	%r31

#	lwz	%r3,_ex_shared0@got(%r31)
#	lwz	%r4,-8(%r3)		# _ex_register or 0
#	cmpi	%cr0,%r4,0
#	beq	.Lno_reg
#	mtlr	%r4
#	blrl
#.Lno_reg:

# Head of _fini function used for static destructors
	.section ".fini","ax"
	.align 2
	.globl _fini
	.type _fini,@function
_fini:	stwu	%r1,-16(%r1)
	mflr	%r0
	stw	%r31,12(%r1)
	stw	%r0,16(%r1)

	bl	_GLOBAL_OFFSET_TABLE_-4	# get the GOT address
	mflr	%r31

# _environ and its evil twin environ, pointing to the environment
	.section ".sdata","aw"
	.align 2
	.globl _environ
	.space 4
	.weak	environ
	.set	environ,_environ
#endif
