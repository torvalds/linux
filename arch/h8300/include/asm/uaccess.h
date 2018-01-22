/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/string.h>

static inline __must_check unsigned long
raw_copy_from_user(void *to, const void __user * from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		switch(n) {
		case 1:
			*(u8 *)to = *(u8 __force *)from;
			return 0;
		case 2:
			*(u16 *)to = *(u16 __force *)from;
			return 0;
		case 4:
			*(u32 *)to = *(u32 __force *)from;
			return 0;
		}
	}

	memcpy(to, (const void __force *)from, n);
	return 0;
}

static inline __must_check unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		switch(n) {
		case 1:
			*(u8 __force *)to = *(u8 *)from;
			return 0;
		case 2:
			*(u16 __force *)to = *(u16 *)from;
			return 0;
		case 4:
			*(u32 __force *)to = *(u32 *)from;
			return 0;
		default:
			break;
		}
	}

	memcpy((void __force *)to, from, n);
	return 0;
}
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

#include <asm-generic/uaccess.h>

#endif
