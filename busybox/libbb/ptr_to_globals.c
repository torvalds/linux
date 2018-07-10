/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2008 by Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <errno.h>

struct globals;

#ifndef GCC_COMBINE

/* We cheat here. It is declared as const ptr in libbb.h,
 * but here we make it live in R/W memory */
struct globals *ptr_to_globals;

#ifdef __GLIBC__
int *bb_errno;
#endif


#else


/* gcc -combine will see through and complain */
/* Using alternative method which is more likely to break
 * on weird architectures, compilers, linkers and so on */
struct globals *const ptr_to_globals __attribute__ ((section (".data")));

#ifdef __GLIBC__
int *const bb_errno __attribute__ ((section (".data")));
#endif

#endif
