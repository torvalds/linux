/* Dependency generator for Makefile fragments.
   Copyright (C) 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Zack Weinberg, Mar 2000

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#ifndef LIBCPP_MKDEPS_H
#define LIBCPP_MKDEPS_H

/* This is the data structure used by all the functions in mkdeps.c.
   It's quite straightforward, but should be treated as opaque.  */

struct deps;

/* Create a deps buffer.  */
extern struct deps *deps_init (void);

/* Destroy a deps buffer.  */
extern void deps_free (struct deps *);

/* Add a set of "vpath" directories. The second argument is a colon-
   separated list of pathnames, like you would set Make's VPATH
   variable to.  If a dependency or target name begins with any of
   these pathnames (and the next path element is not "..") that
   pathname is stripped off.  */
extern void deps_add_vpath (struct deps *, const char *);

/* Add a target (appears on left side of the colon) to the deps list.  Takes
   a boolean indicating whether to quote the target for MAKE.  */
extern void deps_add_target (struct deps *, const char *, int);

/* Sets the default target if none has been given already.  An empty
   string as the default target is interpreted as stdin.  */
extern void deps_add_default_target (struct deps *, const char *);

/* Add a dependency (appears on the right side of the colon) to the
   deps list.  Dependencies will be printed in the order that they
   were entered with this function.  By convention, the first
   dependency entered should be the primary source file.  */
extern void deps_add_dep (struct deps *, const char *);

/* Write out a deps buffer to a specified file.  The third argument
   is the number of columns to word-wrap at (0 means don't wrap).  */
extern void deps_write (const struct deps *, FILE *, unsigned int);

/* Write out a deps buffer to a file, in a form that can be read back
   with deps_restore.  Returns nonzero on error, in which case the
   error number will be in errno.  */
extern int deps_save (struct deps *, FILE *);

/* Read back dependency information written with deps_save into
   the deps buffer.  The third argument may be NULL, in which case
   the dependency information is just skipped, or it may be a filename,
   in which case that filename is skipped.  */
extern int deps_restore (struct deps *, FILE *, const char *);

/* For each dependency *except the first*, emit a dummy rule for that
   file, causing it to depend on nothing.  This is used to work around
   the intermediate-file deletion misfeature in Make, in some
   automatic dependency schemes.  */
extern void deps_phony_targets (const struct deps *, FILE *);

#endif /* ! LIBCPP_MKDEPS_H */
