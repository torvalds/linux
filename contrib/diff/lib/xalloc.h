/* xalloc.h -- malloc with out-of-memory checking

   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifndef XALLOC_H_
# define XALLOC_H_

# include <stddef.h>

# ifndef __attribute__
#  if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
#   define __attribute__(x)
#  endif
# endif

# ifndef ATTRIBUTE_NORETURN
#  define ATTRIBUTE_NORETURN __attribute__ ((__noreturn__))
# endif

/* If this pointer is non-zero, run the specified function upon each
   allocation failure.  It is initialized to zero. */
extern void (*xalloc_fail_func) (void);

/* If XALLOC_FAIL_FUNC is undefined or a function that returns, this
   message is output.  It is translated via gettext.
   Its value is "memory exhausted".  */
extern char const xalloc_msg_memory_exhausted[];

/* This function is always triggered when memory is exhausted.  It is
   in charge of honoring the two previous items.  It exits with status
   exit_failure (defined in exitfail.h).  This is the
   function to call when one wants the program to die because of a
   memory allocation failure.  */
extern void xalloc_die (void) ATTRIBUTE_NORETURN;

void *xmalloc (size_t s);
void *xnmalloc (size_t n, size_t s);
void *xzalloc (size_t s);
void *xcalloc (size_t n, size_t s);
void *xrealloc (void *p, size_t s);
void *xnrealloc (void *p, size_t n, size_t s);
void *x2realloc (void *p, size_t *pn);
void *x2nrealloc (void *p, size_t *pn, size_t s);
void *xclone (void const *p, size_t s);
char *xstrdup (const char *str);

/* Return 1 if an array of N objects, each of size S, cannot exist due
   to size arithmetic overflow.  S must be positive and N must be
   nonnegative.  This is a macro, not an inline function, so that it
   works correctly even when SIZE_MAX < N.

   By gnulib convention, SIZE_MAX represents overflow in size
   calculations, so the conservative dividend to use here is
   SIZE_MAX - 1, since SIZE_MAX might represent an overflowed value.
   However, malloc (SIZE_MAX) fails on all known hosts where
   sizeof (ptrdiff_t) <= sizeof (size_t), so do not bother to test for
   exactly-SIZE_MAX allocations on such hosts; this avoids a test and
   branch when S is known to be 1.  */
# define xalloc_oversized(n, s) \
    ((size_t) (sizeof (ptrdiff_t) <= sizeof (size_t) ? -1 : -2) / (s) < (n))

/* These macros are deprecated; they will go away soon, and are retained
   temporarily only to ease conversion to the functions described above.  */
# define CCLONE(p, n) xclone (p, (n) * sizeof *(p))
# define CLONE(p) xclone (p, sizeof *(p))
# define NEW(type, var) type *var = xmalloc (sizeof (type))
# define XCALLOC(type, n) xcalloc (n, sizeof (type))
# define XMALLOC(type, n) xnmalloc (n, sizeof (type))
# define XREALLOC(p, type, n) xnrealloc (p, n, sizeof (type))
# define XFREE(p) free (p)

#endif /* !XALLOC_H_ */
