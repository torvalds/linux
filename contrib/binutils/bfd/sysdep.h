/* sysdep.h -- handle host dependencies for the BFD library
   Copyright 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2007
   Free Software Foundation, Inc.
   Written by Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef BFD_SYSDEP_H
#define BFD_SYSDEP_H

#include "config.h"

#include "ansidecl.h"

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#if !(defined(errno) || defined(_MSC_VER) && defined(_INC_ERRNO))
extern int errno;
#endif

#ifdef STRING_WITH_STRINGS
#include <string.h>
#include <strings.h>
#else
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
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef USE_BINARY_FOPEN
#include "fopen-bin.h"
#else
#include "fopen-same.h"
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#else
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#endif

#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef O_RDWR
#define O_RDWR 2
#endif
#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#include "filenames.h"

#if !HAVE_DECL_FFS
extern int ffs (int);
#endif

#if !HAVE_DECL_FREE
extern void free ();
#endif

#if !HAVE_DECL_GETENV
extern char *getenv ();
#endif

#if !HAVE_DECL_MALLOC
extern PTR malloc ();
#endif

#if !HAVE_DECL_REALLOC
extern PTR realloc ();
#endif

#if !HAVE_DECL_STPCPY
extern char *stpcpy (char *__dest, const char *__src);
#endif

#if !HAVE_DECL_STRSTR
extern char *strstr ();
#endif

#ifdef HAVE_FTELLO
#if !HAVE_DECL_FTELLO
extern off_t ftello (FILE *stream);
#endif
#endif

#ifdef HAVE_FTELLO64
#if !HAVE_DECL_FTELLO64
extern off64_t ftello64 (FILE *stream);
#endif
#endif

#ifdef HAVE_FSEEKO
#if !HAVE_DECL_FSEEKO
extern int fseeko (FILE *stream, off_t offset, int whence);
#endif
#endif

#ifdef HAVE_FSEEKO64
#if !HAVE_DECL_FSEEKO64
extern int fseeko64 (FILE *stream, off64_t offset, int whence);
#endif
#endif

/* Define offsetof for those systems which lack it */

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifdef ENABLE_NLS
#include <libintl.h>
/* Note the use of dgetext() and PACKAGE here, rather than gettext().

   This is because the code in this directory is used to build a library which
   will be linked with code in other directories to form programs.  We want to
   maintain a seperate translation file for this directory however, rather
   than being forced to merge it with that of any program linked to libbfd.
   This is a library, so it cannot depend on the catalog currently loaded.

   In order to do this, we have to make sure that when we extract messages we
   use the OPCODES domain rather than the domain of the program that included
   the bfd library, (eg OBJDUMP).  Hence we use dgettext (PACKAGE, String)
   and define PACKAGE to be 'bfd'.  (See the code in configure).  */
#define _(String) dgettext (PACKAGE, String)
#ifdef gettext_noop
#define N_(String) gettext_noop (String)
#else
#define N_(String) (String)
#endif
#else
# define gettext(Msgid) (Msgid)
# define dgettext(Domainname, Msgid) (Msgid)
# define dcgettext(Domainname, Msgid, Category) (Msgid)
# define textdomain(Domainname) while (0) /* nothing */
# define bindtextdomain(Domainname, Dirname) while (0) /* nothing */
# define _(String) (String)
# define N_(String) (String)
#endif

#endif /* ! defined (BFD_SYSDEP_H) */
