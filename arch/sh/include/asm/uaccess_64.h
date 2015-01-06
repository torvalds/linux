#ifndef __ASM_SH_UACCESS_64_H
#define __ASM_SH_UACCESS_64_H

/*
 * include/asm-sh/uaccess_64.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * User space memory access functions
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 *  Based on:
 *     MIPS implementation version 1.15 by
 *              Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 *     and i386 version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define __get_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	case 1:							\
		retval = __get_user_asm_b((void *)&x,		\
					  (long)ptr);		\
		break;						\
	case 2:							\
		retval = __get_user_asm_w((void *)&x,		\
					  (long)ptr);		\
		break;						\
	case 4:							\
		retval = __get_user_asm_l((void *)&x,		\
					  (long)ptr);		\
		break;						\
	case 8:							\
		retval = __get_user_asm_q((void *)&x,		\
					  (long)ptr);		\
		break;						\
	default:						\
		__get_user_unknown();				\
		break;						\
	}							\
} while (0)

extern long __get_user_asm_b(void *, long);
extern long __get_user_asm_w(void *, long);
extern long __get_user_asm_l(void *, long);
extern long __get_user_asm_q(void *, long);
extern void __get_user_unknown(void);

#define __put_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	case 1:							\
		retval = __put_user_asm_b((void *)&x,		\
					  (__force long)ptr);	\
		break;						\
	case 2:							\
		retval = __put_user_asm_w((void *)&x,		\
					  (__force long)ptr);	\
		break;						\
	case 4:							\
		retval = __put_user_asm_l((void *)&x,		\
					  (__force long)ptr);	\
		break;						\
	case 8:							\
		retval = __put_user_asm_q((void *)&x,		\
					  (__force long)ptr);	\
		break;						\
	default:						\
		__put_user_unknown();				\
	}							\
} while (0)

extern long __put_user_asm_b(void *, long);
extern long __put_user_asm_w(void *, long);
extern long __put_user_asm_l(void *, long);
extern long __put_user_asm_q(void *, long);
extern void __put_user_unknown(void);

#endif /* __ASM_SH_UACCESS_64_H */
