/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/module.h"

extern void __bb_init_func(void *);
EXPORT_SYMBOL(__bb_init_func);

/* This is defined (and referred to in profiling stub code) only by some GCC
 * versions in libgcov.
 *
 * Since SuSE backported the fix, we cannot handle it depending on GCC version.
 * So, unconditinally export it. But also give it a weak declaration, which will
 * be overriden by any other one.
 */

extern void __gcov_init(void *) __attribute__((weak));
EXPORT_SYMBOL(__gcov_init);

extern void __gcov_merge_add(void *) __attribute__((weak));
EXPORT_SYMBOL(__gcov_merge_add);

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
