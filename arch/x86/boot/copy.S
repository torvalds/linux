/* SPDX-License-Identifier: GPL-2.0-only */
/* ----------------------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

#include <linux/linkage.h>

/*
 * Memory copy routines
 */

	.code16
	.text

SYM_FUNC_START_NOALIGN(memcpy)
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
	retl
SYM_FUNC_END(memcpy)

SYM_FUNC_START_NOALIGN(memset)
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
	retl
SYM_FUNC_END(memset)

SYM_FUNC_START_NOALIGN(copy_from_fs)
	pushw	%ds
	pushw	%fs
	popw	%ds
	calll	memcpy
	popw	%ds
	retl
SYM_FUNC_END(copy_from_fs)

SYM_FUNC_START_NOALIGN(copy_to_fs)
	pushw	%es
	pushw	%fs
	popw	%es
	calll	memcpy
	popw	%es
	retl
SYM_FUNC_END(copy_to_fs)
