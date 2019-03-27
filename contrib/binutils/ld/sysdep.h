/* sysdep.h -- handle host dependencies for the GNU linker
   Copyright 1995, 1996, 1997, 1999, 2002, 2003
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef LD_SYSDEP_H
#define LD_SYSDEP_H

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#else
extern char *strchr ();
extern char *strrchr ();
#endif
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* for PATH_MAX */
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
/* for MAXPATHLEN */
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#ifndef __PAST_END
# define __PAST_END(array, offset) (((typeof(*(array)) *)(array))[offset])
#endif
#endif
#ifdef PATH_MAX
# define LD_PATHMAX PATH_MAX
#else
# ifdef MAXPATHLEN
#  define LD_PATHMAX MAXPATHLEN
# else
#  define LD_PATHMAX 1024
# endif
#endif

#ifdef HAVE_REALPATH
# define REALPATH(a,b) realpath (a, b)
#else
# define REALPATH(a,b) NULL
#endif

#ifdef USE_BINARY_FOPEN
#include "fopen-bin.h"
#else
#include "fopen-same.h"
#endif

#if !HAVE_DECL_STRSTR
extern char *strstr ();
#endif

#if !HAVE_DECL_FREE
extern void free ();
#endif

#if !HAVE_DECL_GETENV
extern char *getenv ();
#endif

#if !HAVE_DECL_ENVIRON
extern char **environ;
#endif

#endif /* ! defined (LD_SYSDEP_H) */
