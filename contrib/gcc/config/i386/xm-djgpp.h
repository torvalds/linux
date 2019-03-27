/* Configuration for GCC for Intel 80386 running DJGPP.
   Copyright (C) 1988, 1996, 1998, 1999, 2000, 2001, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Use semicolons to separate elements of a path.  */
#define PATH_SEPARATOR ';'

#define HOST_EXECUTABLE_SUFFIX ".exe"

/* System dependent initialization for collect2
   to tell system() to act like Unix.  */
#define COLLECT2_HOST_INITIALIZATION \
  do { __system_flags |= (__system_allow_multiple_cmds			\
		          | __system_emulate_chdir); } while (0)

/* Define a version appropriate for DOS.  */
#undef XREF_FILE_NAME
#define XREF_FILE_NAME(xref_file, file) \
  do { \
    const char xref_ext[] = ".gxref"; \
    strcpy (xref_file, file); \
    s = basename (xref_file); \
    t = strchr (s, '.'); \
    if (t) \
      strcpy (t, xref_ext); \
    else \
      strcat (xref_file, xref_ext); \
  } while (0)

#undef GCC_DRIVER_HOST_INITIALIZATION
#define GCC_DRIVER_HOST_INITIALIZATION \
  do { \
    /* If the environment variable DJDIR is not defined, then DJGPP is not \
       installed correctly and GCC will quickly become confused with the \
       default prefix settings. Report the problem now so the user doesn't \
       receive deceptive "file not found" error messages later.  */ \
    char *djdir = getenv ("DJDIR"); \
    if (djdir == NULL) \
      { \
        /* DJDIR is automatically defined by the DJGPP environment config \
           file pointed to by the environment variable DJGPP. Examine DJGPP \
           to try and figure out what's wrong.  */ \
        char *djgpp = getenv ("DJGPP"); \
        if (djgpp == NULL) \
          fatal ("environment variable DJGPP not defined"); \
        else if (access (djgpp, R_OK) == 0) \
          fatal ("environment variable DJGPP points to missing file '%s'", \
                 djgpp); \
        else \
          fatal ("environment variable DJGPP points to corrupt file '%s'", \
                  djgpp); \
      } \
  } while (0)

/* Canonicalize paths containing '/dev/env/'; used in prefix.c.
   _fixpath is a djgpp-specific function to canonicalize a path.
   "/dev/env/DJDIR" evaluates to "c:/djgpp" if DJDIR is "c:/djgpp" for
   example.  It removes any trailing '/', so add it back.  */
/* We cannot free PATH below as it can point to string constant  */
#define UPDATE_PATH_HOST_CANONICALIZE(PATH) \
  if (memcmp ((PATH), "/dev/env/", sizeof("/dev/env/") - 1) == 0) \
    {						\
      static char fixed_path[FILENAME_MAX + 1];	\
						\
      _fixpath ((PATH), fixed_path);		\
      strcat (fixed_path, "/");			\
      (PATH) = xstrdup (fixed_path);		\
    } 
