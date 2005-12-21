/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_UACCESS_H
#define __SKAS_UACCESS_H

#include "asm/errno.h"

/* No SKAS-specific checking. */
#define access_ok_skas(type, addr, size) 0

extern int copy_from_user_skas(void *to, const void __user *from, int n);
extern int copy_to_user_skas(void __user *to, const void *from, int n);
extern int strncpy_from_user_skas(char *dst, const char __user *src, int count);
extern int __clear_user_skas(void __user *mem, int len);
extern int clear_user_skas(void __user *mem, int len);
extern int strnlen_user_skas(const void __user *str, int len);

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
