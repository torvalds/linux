/* Code dealing with register stack frames, for GDB, the GNU debugger.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002 Free Software
   Foundation, Inc.

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


#include "defs.h"
#include "regcache.h"
#include "sentinel-frame.h"
#include "inferior.h"
#include "frame-unwind.h"

struct frame_unwind_cache
{
  struct regcache *regcache;
};

void *
sentinel_frame_cache (struct regcache *regcache)
{
  struct frame_unwind_cache *cache = 
    FRAME_OBSTACK_ZALLOC (struct frame_unwind_cache);
  cache->regcache = regcache;
  return cache;
}

/* Here the register value is taken direct from the register cache.  */

static void
sentinel_frame_prev_register (struct frame_info *next_frame,
			      void **this_prologue_cache,
			      int regnum, int *optimized,
			      enum lval_type *lvalp, CORE_ADDR *addrp,
			      int *realnum, void *bufferp)
{
  struct frame_unwind_cache *cache = *this_prologue_cache;
  /* Describe the register's location.  A reg-frame maps all registers
     onto the corresponding hardware register.  */
  *optimized = 0;
  *lvalp = lval_register;
  *addrp = register_offset_hack (current_gdbarch, regnum);
  *realnum = regnum;

  /* If needed, find and return the value of the register.  */
  if (bufferp != NULL)
    {
      /* Return the actual value.  */
      /* Use the regcache_cooked_read() method so that it, on the fly,
         constructs either a raw or pseudo register from the raw
         register cache.  */
      regcache_cooked_read (cache->regcache, regnum, bufferp);
    }
}

static void
sentinel_frame_this_id (struct frame_info *next_frame,
			void **this_prologue_cache,
			struct frame_id *this_id)
{
  /* The sentinel frame is used as a starting point for creating the
     previous (inner most) frame.  That frame's THIS_ID method will be
     called to determine the inner most frame's ID.  Not this one.  */
  internal_error (__FILE__, __LINE__, "sentinel_frame_this_id called");
}

const struct frame_unwind sentinel_frame_unwinder =
{
  /* Should the sentinel frame be given a special type?  */
  NORMAL_FRAME,
  sentinel_frame_this_id,
  sentinel_frame_prev_register
};

const struct frame_unwind *const sentinel_frame_unwind = &sentinel_frame_unwinder;
