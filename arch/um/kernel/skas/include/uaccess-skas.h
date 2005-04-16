/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_UACCESS_H
#define __SKAS_UACCESS_H

#include "asm/errno.h"
#include "asm/fixmap.h"

#define access_ok_skas(type, addr, size) \
	((segment_eq(get_fs(), KERNEL_DS)) || \
	 (((unsigned long) (addr) < TASK_SIZE) && \
	  ((unsigned long) (addr) + (size) <= TASK_SIZE)) || \
	 ((type == VERIFY_READ ) && \
	  ((unsigned long) (addr) >= FIXADDR_USER_START) && \
	  ((unsigned long) (addr) + (size) <= FIXADDR_USER_END) && \
	  ((unsigned long) (addr) + (size) >= (unsigned long)(addr))))

static inline int verify_area_skas(int type, const void * addr,
				   unsigned long size)
{
	return(access_ok_skas(type, addr, size) ? 0 : -EFAULT);
}

extern int copy_from_user_skas(void *to, const void *from, int n);
extern int copy_to_user_skas(void *to, const void *from, int n);
extern int strncpy_from_user_skas(char *dst, const char *src, int count);
extern int __clear_user_skas(void *mem, int len);
extern int clear_user_skas(void *mem, int len);
extern int strnlen_user_skas(const void *str, int len);

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
