/* Obstack wrapper for GDB.

   Copyright 2002 Free Software Foundation, Inc.

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

#if !defined (GDB_OBSTACK_H)
#define GDB_OBSTACK_H 1

#include "obstack.h"

/* Unless explicitly specified, GDB obstacks always use xmalloc() and
   xfree().  */
/* Note: ezannoni 2004-02-09: One could also specify the allocation
   functions using a special init function for each obstack,
   obstack_specify_allocation.  However we just use obstack_init and
   let these defines here do the job.  While one could argue the
   superiority of one approach over the other, we just chose one
   throughout.  */

#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free xfree

#endif
