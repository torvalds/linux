/* ----------------------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Memory copy routines
 */

	.code16gcc
	.text

	.globl	memcpy
	.type	memcpy, @function
memcpy:
	pushw	%si
	pushw	%di
	movw	%ax, %di
	movw	%dx, %si
	pushw	%cx
	shrw	$2, %cx
	rep; movsl
	popw	%cx
	andw	$3, %cx
	rep; movsb
	popw	%di
	popw	%si
	ret
	.size	memcpy, .-memcpy

	.globl	memset
	.type	memset, @function
memset:
	pushw	%di
	movw	%ax, %di
	movzbl	%dl, %eax
	imull	$0x01010101,%eax
	pushw	%cx
	shrw	$2, %cx
	rep; stosl
	popw	%cx
	andw	$3, %cx
	rep; stosb
	popw	%di
	ret
	.size	memset, .-memset

	.globl	copy_from_fs
	.type	copy_from_fs, @function
copy_from_fs:
	pushw	%ds
	pushw	%fs
	popw	%ds
	call	memcpy
	popw	%ds
	ret
	.size	copy_from_fs, .-copy_from_fs

	.globl	copy_to_fs
	.type	copy_to_fs, @function
copy_to_fs:
	pushw	%es
	pushw	%fs
	popw	%es
	call	memcpy
	popw	%es
	ret
	.size	copy_to_fs, .-copy_to_fs

#if 0 /* Not currently used, but can be enabled as needed */

	.globl	copy_from_gs
	.type	copy_from_gs, @function
copy_from_gs:
	pushw	%ds
	pushw	%gs
	popw	%ds
	call	memcpy
	popw	%ds
	ret
	.size	copy_from_gs, .-copy_from_gs
	.globl	copy_to_gs

	.type	copy_to_gs, @function
copy_to_gs:
	pushw	%es
	pushw	%gs
	popw	%es
	call	memcpy
	popw	%es
	ret
	.size	copy_to_gs, .-copy_to_gs

#endif
