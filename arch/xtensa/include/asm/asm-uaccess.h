/*
 * include/asm-xtensa/uaccess.h
 *
 * User space memory access functions
 *
 * These routines provide basic accessing functions to the user memory
 * space for the kernel. This header file provides functions such as:
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_ASM_UACCESS_H
#define _XTENSA_ASM_UACCESS_H

#include <linux/errno.h>
#include <asm/types.h>

#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>

/*
 * These assembly macros mirror the C macros in asm/uaccess.h.  They
 * should always have identical functionality.  See
 * arch/xtensa/kernel/sys.S for usage.
 */

#define KERNEL_DS	0
#define USER_DS		1

#define get_ds		(KERNEL_DS)

/*
 * get_fs reads current->thread.current_ds into a register.
 * On Entry:
 * 	<ad>	anything
 * 	<sp>	stack
 * On Exit:
 * 	<ad>	contains current->thread.current_ds
 */
	.macro	get_fs	ad, sp
	GET_CURRENT(\ad,\sp)
#if THREAD_CURRENT_DS > 1020
	addi	\ad, \ad, TASK_THREAD
	l32i	\ad, \ad, THREAD_CURRENT_DS - TASK_THREAD
#else
	l32i	\ad, \ad, THREAD_CURRENT_DS
#endif
	.endm

/*
 * set_fs sets current->thread.current_ds to some value.
 * On Entry:
 *	<at>	anything (temp register)
 *	<av>	value to write
 *	<sp>	stack
 * On Exit:
 *	<at>	destroyed (actually, current)
 *	<av>	preserved, value to write
 */
	.macro	set_fs	at, av, sp
	GET_CURRENT(\at,\sp)
	s32i	\av, \at, THREAD_CURRENT_DS
	.endm

/*
 * kernel_ok determines whether we should bypass addr/size checking.
 * See the equivalent C-macro version below for clarity.
 * On success, kernel_ok branches to a label indicated by parameter
 * <success>.  This implies that the macro falls through to the next
 * insruction on an error.
 *
 * Note that while this macro can be used independently, we designed
 * in for optimal use in the access_ok macro below (i.e., we fall
 * through on error).
 *
 * On Entry:
 * 	<at>		anything (temp register)
 * 	<success>	label to branch to on success; implies
 * 			fall-through macro on error
 * 	<sp>		stack pointer
 * On Exit:
 * 	<at>		destroyed (actually, current->thread.current_ds)
 */

#if ((KERNEL_DS != 0) || (USER_DS == 0))
# error Assembly macro kernel_ok fails
#endif
	.macro	kernel_ok  at, sp, success
	get_fs	\at, \sp
	beqz	\at, \success
	.endm

/*
 * user_ok determines whether the access to user-space memory is allowed.
 * See the equivalent C-macro version below for clarity.
 *
 * On error, user_ok branches to a label indicated by parameter
 * <error>.  This implies that the macro falls through to the next
 * instruction on success.
 *
 * Note that while this macro can be used independently, we designed
 * in for optimal use in the access_ok macro below (i.e., we fall
 * through on success).
 *
 * On Entry:
 * 	<aa>	register containing memory address
 * 	<as>	register containing memory size
 * 	<at>	temp register
 * 	<error>	label to branch to on error; implies fall-through
 * 		macro on success
 * On Exit:
 * 	<aa>	preserved
 * 	<as>	preserved
 * 	<at>	destroyed (actually, (TASK_SIZE + 1 - size))
 */
	.macro	user_ok	aa, as, at, error
	movi	\at, __XTENSA_UL_CONST(TASK_SIZE)
	bgeu	\as, \at, \error
	sub	\at, \at, \as
	bgeu	\aa, \at, \error
	.endm

/*
 * access_ok determines whether a memory access is allowed.  See the
 * equivalent C-macro version below for clarity.
 *
 * On error, access_ok branches to a label indicated by parameter
 * <error>.  This implies that the macro falls through to the next
 * instruction on success.
 *
 * Note that we assume success is the common case, and we optimize the
 * branch fall-through case on success.
 *
 * On Entry:
 * 	<aa>	register containing memory address
 * 	<as>	register containing memory size
 * 	<at>	temp register
 * 	<sp>
 * 	<error>	label to branch to on error; implies fall-through
 * 		macro on success
 * On Exit:
 * 	<aa>	preserved
 * 	<as>	preserved
 * 	<at>	destroyed
 */
	.macro	access_ok  aa, as, at, sp, error
	kernel_ok  \at, \sp, .Laccess_ok_\@
	user_ok    \aa, \as, \at, \error
.Laccess_ok_\@:
	.endm

#endif	/* _XTENSA_ASM_UACCESS_H */
