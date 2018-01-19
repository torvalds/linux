/* SPDX-License-Identifier: GPL-2.0 */
/*!**************************************************************************
*!
*! FILE NAME  : eshlibld.h
*!
*! DESCRIPTION: Prototypes for exported shared library functions
*!
*! FUNCTIONS  : perform_cris_aout_relocations, shlibmod_fork, shlibmod_exit
*! (EXPORTED)
*!
*!---------------------------------------------------------------------------
*!
*! (C) Copyright 1998, 1999 Axis Communications AB, LUND, SWEDEN
*!
*!**************************************************************************/
/* $Id: eshlibld.h,v 1.2 2001/02/23 13:47:33 bjornw Exp $ */

#ifndef _cris_relocate_h
#define _cris_relocate_h

/* Please note that this file is also compiled into the xsim simulator.
   Try to avoid breaking its double use (only works on a little-endian
   32-bit machine such as the i386 anyway).

   Use __KERNEL__ when you're about to use kernel functions,
       (which you should not do here anyway, since this file is
       used by glibc).
   Use defined(__KERNEL__) || defined(__elinux__) when doing
       things that only makes sense on an elinux system.
   Use __CRIS__ when you're about to do (really) CRIS-specific code.
*/

/* We have dependencies all over the place for the host system
   for xsim being a linux system, so let's not pretend anything
   else with #ifdef:s here until fixed.  */
#include <linux/limits.h>

/* Maybe do sanity checking if file input. */
#undef SANITYCHECK_RELOC

/* Maybe output debug messages. */
#undef RELOC_DEBUG

/* Maybe we want to share core as well as disk space.
   Mainly depends on the config macro CONFIG_SHARE_SHLIB_CORE, but it is
   assumed that we want to share code when debugging (exposes more
   trouble). */
#ifndef SHARE_LIB_CORE
# if (defined(__KERNEL__) || !defined(RELOC_DEBUG))
#  define SHARE_LIB_CORE 0
# else
#  define SHARE_LIB_CORE 1
# endif /* __KERNEL__ etc */
#endif /* SHARE_LIB_CORE */


/* Main exported function; supposed to be called when the program a.out
   has been read in. */
extern int
perform_cris_aout_relocations(unsigned long text, unsigned long tlength,
			      unsigned long data, unsigned long dlength,
			      unsigned long baddr, unsigned long blength,

			      /* These may be zero when there's "perfect"
				 position-independent code. */
			      unsigned char *trel, unsigned long tsrel,
			      unsigned long dsrel,

			      /* These will be zero at a first try, to see
				 if code is statically linked.  Else a
				 second try, with the symbol table and
				 string table nonzero should be done. */
			      unsigned char *symbols, unsigned long symlength,
			      unsigned char *strings, unsigned long stringlength,

			      /* These will only be used when symbol table
			       information is present. */
			      char **env, int envc,
			      int euid, int is_suid);


#ifdef RELOC_DEBUG
/* Task-specific debug stuff. */
struct task_reloc_debug {
	struct memdebug *alloclast;
	unsigned long alloc_total;
	unsigned long export_total;
};
#endif /* RELOC_DEBUG */

#if SHARE_LIB_CORE

/* When code (and some very specific data) is shared and not just
   dynamically linked, we need to export hooks for exec beginning and
   end. */

struct shlibdep;

extern void
shlibmod_exit(struct shlibdep **deps);

/* Returns 0 if failure, nonzero for ok. */
extern int
shlibmod_fork(struct shlibdep **deps);

#else  /* ! SHARE_LIB_CORE */
# define shlibmod_exit(x)
# define shlibmod_fork(x) 1
#endif /* ! SHARE_LIB_CORE */

#endif _cris_relocate_h
/********************** END OF FILE eshlibld.h *****************************/

