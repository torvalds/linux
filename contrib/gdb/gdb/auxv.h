/* Auxiliary vector support for GDB, the GNU debugger.

   Copyright 2004 Free Software Foundation, Inc.

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

#ifndef AUXV_H
#define AUXV_H

/* See "include/elf/common.h" for the definition of valid AT_* values.  */


/* Avoid miscellaneous includes in this file, so that it can be
   included by nm-*.h for the procfs_xfer_auxv decl if that is
   used in NATIVE_XFER_AUXV.  */
struct target_ops;		/* Forward declaration.  */


/* Read all the auxv data into a contiguous xmalloc'd buffer,
   stored in *DATA.  Return the size in bytes of this data.
   If zero, there is no data and *DATA is null.
   if < 0, there was an error and *DATA is null.  */
extern LONGEST target_auxv_read (struct target_ops *ops, char **data);

/* Read one auxv entry from *READPTR, not reading locations >= ENDPTR.
   Return 0 if *READPTR is already at the end of the buffer.
   Return -1 if there is insufficient buffer for a whole entry.
   Return 1 if an entry was read into *TYPEP and *VALP.  */
extern int target_auxv_parse (struct target_ops *ops,
			      char **readptr, char *endptr,
			      CORE_ADDR *typep, CORE_ADDR *valp);

/* Extract the auxiliary vector entry with a_type matching MATCH.
   Return zero if no such entry was found, or -1 if there was
   an error getting the information.  On success, return 1 after
   storing the entry's value field in *VALP.  */
extern int target_auxv_search (struct target_ops *ops,
			       CORE_ADDR match, CORE_ADDR *valp);

/* Print the contents of the target's AUXV on the specified file. */
extern int fprint_target_auxv (struct ui_file *file, struct target_ops *ops);


/* This function is called like a to_xfer_partial hook,
   but must be called with TARGET_OBJECT_AUXV.
   It handles access via /proc/PID/auxv, which is the common method.
   This function is appropriate for doing:
	   #define NATIVE_XFER_AUXV	procfs_xfer_auxv
   for a native target that uses inftarg.c's child_xfer_partial hook.  */

extern LONGEST procfs_xfer_auxv (struct target_ops *ops,
				 int /* enum target_object */ object,
				 const char *annex,
				 void *readbuf,
				 const void *writebuf,
				 ULONGEST offset,
				 LONGEST len);


#endif
