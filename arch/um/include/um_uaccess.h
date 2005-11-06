/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_UM_UACCESS_H
#define __ARCH_UM_UACCESS_H

#include "linux/config.h"
#include "choose-mode.h"

#ifdef CONFIG_MODE_TT
#include "uaccess-tt.h"
#endif

#ifdef CONFIG_MODE_SKAS
#include "uaccess-skas.h"
#endif

#define access_ok(type, addr, size) \
	CHOOSE_MODE_PROC(access_ok_tt, access_ok_skas, type, addr, size)

static inline int copy_from_user(void *to, const void __user *from, int n)
{
	return(CHOOSE_MODE_PROC(copy_from_user_tt, copy_from_user_skas, to,
				from, n));
}

static inline int copy_to_user(void __user *to, const void *from, int n)
{
	return(CHOOSE_MODE_PROC(copy_to_user_tt, copy_to_user_skas, to, 
				from, n));
}

/*
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */

static inline int strncpy_from_user(char *dst, const char __user *src, int count)
{
	return(CHOOSE_MODE_PROC(strncpy_from_user_tt, strncpy_from_user_skas,
				dst, src, count));
}

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
static inline int __clear_user(void *mem, int len)
{
	return(CHOOSE_MODE_PROC(__clear_user_tt, __clear_user_skas, mem, len));
}

/*
 * clear_user: - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
static inline int clear_user(void __user *mem, int len)
{
	return(CHOOSE_MODE_PROC(clear_user_tt, clear_user_skas, mem, len));
}

/*
 * strlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 * @n:   The maximum valid length
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
static inline int strnlen_user(const void __user *str, long len)
{
	return(CHOOSE_MODE_PROC(strnlen_user_tt, strnlen_user_skas, str, len));
}

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
