/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This header is the first thing included in any of the bsdtar
 * source files.  As far as possible, platform-specific issues should
 * be dealt with here and not within individual source files.
 */

#ifndef BSDTAR_PLATFORM_H_INCLUDED
#define	BSDTAR_PLATFORM_H_INCLUDED

#if defined(PLATFORM_CONFIG_H)
/* Use hand-built config.h in environments that need it. */
#include PLATFORM_CONFIG_H
#else
/* Not having a config.h of some sort is a serious problem. */
#include "config.h"
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
#include "bsdtar_windows.h"
#endif

/* Get a real definition for __FBSDID if we can */
#if HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

/* If not, define it so as to avoid dangling semicolons. */
#ifndef __FBSDID
#define	__FBSDID(a)     struct _undefined_hack
#endif

#ifdef HAVE_LIBARCHIVE
/* If we're using the platform libarchive, include system headers. */
#include <archive.h>
#include <archive_entry.h>
#else
/* Otherwise, include user headers. */
#include "archive.h"
#include "archive_entry.h"
#endif

#ifdef HAVE_LIBACL
#include <acl/libacl.h>
#endif

/*
 * Include "dirent.h" (or its equivalent on several different platforms).
 *
 * This is slightly modified from the GNU autoconf recipe.
 * In particular, FreeBSD includes d_namlen in its dirent structure,
 * so my configure script includes an explicit test for the d_namlen
 * field.
 */
#if HAVE_DIRENT_H
# include <dirent.h>
# if HAVE_DIRENT_D_NAMLEN
#  define DIRENT_NAMLEN(dirent) (dirent)->d_namlen
# else
#  define DIRENT_NAMLEN(dirent) strlen((dirent)->d_name)
# endif
#else
# define dirent direct
# define DIRENT_NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#if HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define	ARCHIVE_STAT_CTIME_NANOS(st)	(st)->st_ctimespec.tv_nsec
#define	ARCHIVE_STAT_MTIME_NANOS(st)	(st)->st_mtimespec.tv_nsec
#elif HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define	ARCHIVE_STAT_CTIME_NANOS(st)	(st)->st_ctim.tv_nsec
#define	ARCHIVE_STAT_MTIME_NANOS(st)	(st)->st_mtim.tv_nsec
#elif HAVE_STRUCT_STAT_ST_MTIME_N
#define	ARCHIVE_STAT_CTIME_NANOS(st)	(st)->st_ctime_n
#define	ARCHIVE_STAT_MTIME_NANOS(st)	(st)->st_mtime_n
#elif HAVE_STRUCT_STAT_ST_UMTIME
#define	ARCHIVE_STAT_CTIME_NANOS(st)	(st)->st_uctime * 1000
#define	ARCHIVE_STAT_MTIME_NANOS(st)	(st)->st_umtime * 1000
#elif HAVE_STRUCT_STAT_ST_MTIME_USEC
#define	ARCHIVE_STAT_CTIME_NANOS(st)	(st)->st_ctime_usec * 1000
#define	ARCHIVE_STAT_MTIME_NANOS(st)	(st)->st_mtime_usec * 1000
#else
#define	ARCHIVE_STAT_CTIME_NANOS(st)	(0)
#define	ARCHIVE_STAT_MTIME_NANOS(st)	(0)
#endif

/* How to mark functions that don't return. */
/* This facilitates use of some newer static code analysis tools. */
#undef __LA_DEAD
#if defined(__GNUC__) && (__GNUC__ > 2 || \
			  (__GNUC__ == 2 && __GNUC_MINOR__ >= 5))
#define	__LA_DEAD	__attribute__((__noreturn__))
#else
#define	__LA_DEAD
#endif

#endif /* !BSDTAR_PLATFORM_H_INCLUDED */
