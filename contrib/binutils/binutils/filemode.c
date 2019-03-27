/* filemode.c -- make a string describing file modes
   Copyright 1985, 1990, 1991, 1994, 1995, 1997, 1999, 2002, 2003, 2005,
   2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"

static char ftypelet (unsigned long);
static void setst (unsigned long, char *);

/* filemodestring - fill in string STR with an ls-style ASCII
   representation of the st_mode field of file stats block STATP.
   10 characters are stored in STR; no terminating null is added.
   The characters stored in STR are:

   0	File type.  'd' for directory, 'c' for character
	special, 'b' for block special, 'm' for multiplex,
	'l' for symbolic link, 's' for socket, 'p' for fifo,
	'-' for any other file type

   1	'r' if the owner may read, '-' otherwise.

   2	'w' if the owner may write, '-' otherwise.

   3	'x' if the owner may execute, 's' if the file is
	set-user-id, '-' otherwise.
	'S' if the file is set-user-id, but the execute
	bit isn't set.

   4	'r' if group members may read, '-' otherwise.

   5	'w' if group members may write, '-' otherwise.

   6	'x' if group members may execute, 's' if the file is
	set-group-id, '-' otherwise.
	'S' if it is set-group-id but not executable.

   7	'r' if any user may read, '-' otherwise.

   8	'w' if any user may write, '-' otherwise.

   9	'x' if any user may execute, 't' if the file is "sticky"
	(will be retained in swap space after execution), '-'
	otherwise.
	'T' if the file is sticky but not executable.  */

/* Get definitions for the file permission bits.  */

#ifndef S_IRWXU
#define S_IRWXU 0700
#endif
#ifndef S_IRUSR
#define S_IRUSR 0400
#endif
#ifndef S_IWUSR
#define S_IWUSR 0200
#endif
#ifndef S_IXUSR
#define S_IXUSR 0100
#endif

#ifndef S_IRWXG
#define S_IRWXG 0070
#endif
#ifndef S_IRGRP
#define S_IRGRP 0040
#endif
#ifndef S_IWGRP
#define S_IWGRP 0020
#endif
#ifndef S_IXGRP
#define S_IXGRP 0010
#endif

#ifndef S_IRWXO
#define S_IRWXO 0007
#endif
#ifndef S_IROTH
#define S_IROTH 0004
#endif
#ifndef S_IWOTH
#define S_IWOTH 0002
#endif
#ifndef S_IXOTH
#define S_IXOTH 0001
#endif

/* Like filemodestring, but only the relevant part of the `struct stat'
   is given as an argument.  */

void
mode_string (unsigned long mode, char *str)
{
  str[0] = ftypelet ((unsigned long) mode);
  str[1] = (mode & S_IRUSR) != 0 ? 'r' : '-';
  str[2] = (mode & S_IWUSR) != 0 ? 'w' : '-';
  str[3] = (mode & S_IXUSR) != 0 ? 'x' : '-';
  str[4] = (mode & S_IRGRP) != 0 ? 'r' : '-';
  str[5] = (mode & S_IWGRP) != 0 ? 'w' : '-';
  str[6] = (mode & S_IXGRP) != 0 ? 'x' : '-';
  str[7] = (mode & S_IROTH) != 0 ? 'r' : '-';
  str[8] = (mode & S_IWOTH) != 0 ? 'w' : '-';
  str[9] = (mode & S_IXOTH) != 0 ? 'x' : '-';
  setst ((unsigned long) mode, str);
}

/* Return a character indicating the type of file described by
   file mode BITS:
   'd' for directories
   'b' for block special files
   'c' for character special files
   'm' for multiplexer files
   'l' for symbolic links
   's' for sockets
   'p' for fifos
   '-' for any other file type.  */

#ifndef S_ISDIR
#ifdef S_IFDIR
#define S_ISDIR(i) (((i) & S_IFMT) == S_IFDIR)
#else /* ! defined (S_IFDIR) */
#define S_ISDIR(i) (((i) & 0170000) == 040000)
#endif /* ! defined (S_IFDIR) */
#endif /* ! defined (S_ISDIR) */

#ifndef S_ISBLK
#ifdef S_IFBLK
#define S_ISBLK(i) (((i) & S_IFMT) == S_IFBLK)
#else /* ! defined (S_IFBLK) */
#define S_ISBLK(i) 0
#endif /* ! defined (S_IFBLK) */
#endif /* ! defined (S_ISBLK) */

#ifndef S_ISCHR
#ifdef S_IFCHR
#define S_ISCHR(i) (((i) & S_IFMT) == S_IFCHR)
#else /* ! defined (S_IFCHR) */
#define S_ISCHR(i) 0
#endif /* ! defined (S_IFCHR) */
#endif /* ! defined (S_ISCHR) */

#ifndef S_ISFIFO
#ifdef S_IFIFO
#define S_ISFIFO(i) (((i) & S_IFMT) == S_IFIFO)
#else /* ! defined (S_IFIFO) */
#define S_ISFIFO(i) 0
#endif /* ! defined (S_IFIFO) */
#endif /* ! defined (S_ISFIFO) */

#ifndef S_ISSOCK
#ifdef S_IFSOCK
#define S_ISSOCK(i) (((i) & S_IFMT) == S_IFSOCK)
#else /* ! defined (S_IFSOCK) */
#define S_ISSOCK(i) 0
#endif /* ! defined (S_IFSOCK) */
#endif /* ! defined (S_ISSOCK) */

#ifndef S_ISLNK
#ifdef S_IFLNK
#define S_ISLNK(i) (((i) & S_IFMT) == S_IFLNK)
#else /* ! defined (S_IFLNK) */
#define S_ISLNK(i) 0
#endif /* ! defined (S_IFLNK) */
#endif /* ! defined (S_ISLNK) */

static char
ftypelet (unsigned long bits)
{
  if (S_ISDIR (bits))
    return 'd';
  if (S_ISLNK (bits))
    return 'l';
  if (S_ISBLK (bits))
    return 'b';
  if (S_ISCHR (bits))
    return 'c';
  if (S_ISSOCK (bits))
    return 's';
  if (S_ISFIFO (bits))
    return 'p';

#ifdef S_IFMT
#ifdef S_IFMPC
  if ((bits & S_IFMT) == S_IFMPC
      || (bits & S_IFMT) == S_IFMPB)
    return 'm';
#endif
#ifdef S_IFNWK
  if ((bits & S_IFMT) == S_IFNWK)
    return 'n';
#endif
#endif

  return '-';
}

/* Set the 's' and 't' flags in file attributes string CHARS,
   according to the file mode BITS.  */

static void
setst (unsigned long bits ATTRIBUTE_UNUSED, char *chars ATTRIBUTE_UNUSED)
{
#ifdef S_ISUID
  if (bits & S_ISUID)
    {
      if (chars[3] != 'x')
	/* Set-uid, but not executable by owner.  */
	chars[3] = 'S';
      else
	chars[3] = 's';
    }
#endif
#ifdef S_ISGID
  if (bits & S_ISGID)
    {
      if (chars[6] != 'x')
	/* Set-gid, but not executable by group.  */
	chars[6] = 'S';
      else
	chars[6] = 's';
    }
#endif
#ifdef S_ISVTX
  if (bits & S_ISVTX)
    {
      if (chars[9] != 'x')
	/* Sticky, but not executable by others.  */
	chars[9] = 'T';
      else
	chars[9] = 't';
    }
#endif
}
