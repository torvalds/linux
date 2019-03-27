/* Portable <string.h>
   Copyright 1995, 1998, 1999, 2000, 2001 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#if !defined(GDB_STRING_H)
#define GDB_STRING_H

#ifdef STDC_HEADERS
#include <string.h>
#else
#ifdef HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif

#ifndef strchr
extern char *strchr (const char *, int);	/* X3.159-1989  4.11.5.2 */
#endif

#ifndef strrchr
extern char *strrchr (const char *, int);	/* X3.159-1989  4.11.5.5 */
#endif

#ifndef strtok
extern char *strtok (char *, const char *);	/* X3.159-1989  4.11.5.8 */
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#else
extern void *memset ();
extern void *memcpy ();
extern void *memmove ();
extern int memcmp ();
#endif
#endif /* STDC_HEADERS */

#ifdef NEED_DECLARATION_STRERROR
#ifndef strerror
extern char *strerror (int);	/* X3.159-1989  4.11.6.2 */
#endif
#endif

#ifdef NEED_DECLARATION_STRSTR
#ifndef strstr
extern char *strstr (const char *, const char *);	/* X3.159-1989  4.11.5.7 */
#endif
#endif

#endif /* !defined(GDB_STRING_H) */
