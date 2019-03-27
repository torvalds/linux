/* Return a string describing the type of a file.

   Copyright (C) 1993, 1994, 2001, 2002, 2004 Free Software Foundation, Inc.

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

/* Written by Paul Eggert and Jim Meyering.  */

#ifndef FILE_TYPE_H
# define FILE_TYPE_H 1

# if ! defined S_ISREG && ! defined S_IFREG
you must include <sys/stat.h> before including this file
# endif

char const *file_type (struct stat const *);

# ifndef S_IFMT
#  define S_IFMT 0170000
# endif

# if STAT_MACROS_BROKEN
#  undef S_ISBLK
#  undef S_ISCHR
#  undef S_ISDIR
#  undef S_ISDOOR
#  undef S_ISFIFO
#  undef S_ISLNK
#  undef S_ISNAM
#  undef S_ISMPB
#  undef S_ISMPC
#  undef S_ISNWK
#  undef S_ISREG
#  undef S_ISSOCK
# endif


# ifndef S_ISBLK
#  ifdef S_IFBLK
#   define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#  else
#   define S_ISBLK(m) 0
#  endif
# endif

# ifndef S_ISCHR
#  ifdef S_IFCHR
#   define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#  else
#   define S_ISCHR(m) 0
#  endif
# endif

# ifndef S_ISDIR
#  ifdef S_IFDIR
#   define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#  else
#   define S_ISDIR(m) 0
#  endif
# endif

# ifndef S_ISDOOR /* Solaris 2.5 and up */
#  ifdef S_IFDOOR
#   define S_ISDOOR(m) (((m) & S_IFMT) == S_IFDOOR)
#  else
#   define S_ISDOOR(m) 0
#  endif
# endif

# ifndef S_ISFIFO
#  ifdef S_IFIFO
#   define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#  else
#   define S_ISFIFO(m) 0
#  endif
# endif

# ifndef S_ISLNK
#  ifdef S_IFLNK
#   define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#  else
#   define S_ISLNK(m) 0
#  endif
# endif

# ifndef S_ISMPB /* V7 */
#  ifdef S_IFMPB
#   define S_ISMPB(m) (((m) & S_IFMT) == S_IFMPB)
#   define S_ISMPC(m) (((m) & S_IFMT) == S_IFMPC)
#  else
#   define S_ISMPB(m) 0
#   define S_ISMPC(m) 0
#  endif
# endif

# ifndef S_ISNAM /* Xenix */
#  ifdef S_IFNAM
#   define S_ISNAM(m) (((m) & S_IFMT) == S_IFNAM)
#  else
#   define S_ISNAM(m) 0
#  endif
# endif

# ifndef S_ISNWK /* HP/UX */
#  ifdef S_IFNWK
#   define S_ISNWK(m) (((m) & S_IFMT) == S_IFNWK)
#  else
#   define S_ISNWK(m) 0
#  endif
# endif

# ifndef S_ISREG
#  ifdef S_IFREG
#   define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#  else
#   define S_ISREG(m) 0
#  endif
# endif

# ifndef S_ISSOCK
#  ifdef S_IFSOCK
#   define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#  else
#   define S_ISSOCK(m) 0
#  endif
# endif


# ifndef S_TYPEISMQ
#  define S_TYPEISMQ(p) 0
# endif

# ifndef S_TYPEISTMO
#  define S_TYPEISTMO(p) 0
# endif


# ifndef S_TYPEISSEM
#  ifdef S_INSEM
#   define S_TYPEISSEM(p) (S_ISNAM ((p)->st_mode) && (p)->st_rdev == S_INSEM)
#  else
#   define S_TYPEISSEM(p) 0
#  endif
# endif

# ifndef S_TYPEISSHM
#  ifdef S_INSHD
#   define S_TYPEISSHM(p) (S_ISNAM ((p)->st_mode) && (p)->st_rdev == S_INSHD)
#  else
#   define S_TYPEISSHM(p) 0
#  endif
# endif

#endif /* FILE_TYPE_H */
