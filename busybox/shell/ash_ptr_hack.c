/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2008 by Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

struct globals_misc;
struct globals_memstack;
struct globals_var;

#ifndef GCC_COMBINE

/* We cheat here. They are declared as const ptr in ash.c,
 * but here we make them live in R/W memory */
struct globals_misc     *ash_ptr_to_globals_misc;
struct globals_memstack *ash_ptr_to_globals_memstack;
struct globals_var      *ash_ptr_to_globals_var;

#else

/* gcc -combine will see through and complain */
/* Using alternative method which is more likely to break
 * on weird architectures, compilers, linkers and so on */
struct globals_misc     *const ash_ptr_to_globals_misc __attribute__ ((section (".data")));
struct globals_memstack *const ash_ptr_to_globals_memstack __attribute__ ((section (".data")));
struct globals_var      *const ash_ptr_to_globals_var __attribute__ ((section (".data")));

#endif
