/* Definitions for frame unwinder, for GDB, the GNU debugger.

   Copyright 2003 Free Software Foundation, Inc.

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
#include "frame.h"
#include "frame-unwind.h"
#include "gdb_assert.h"
#include "dummy-frame.h"

static struct gdbarch_data *frame_unwind_data;

frame_unwind_sniffer_ftype *kgdb_sniffer_kluge;

struct frame_unwind_table
{
  frame_unwind_sniffer_ftype **sniffer;
  int nr;
};

/* Append a predicate to the end of the table.  */
static void
append_predicate (struct frame_unwind_table *table,
		  frame_unwind_sniffer_ftype *sniffer)
{
  table->sniffer = xrealloc (table->sniffer, ((table->nr + 1)
					      * sizeof (frame_unwind_sniffer_ftype *)));
  table->sniffer[table->nr] = sniffer;
  table->nr++;
}

static void *
frame_unwind_init (struct gdbarch *gdbarch)
{
  struct frame_unwind_table *table = XCALLOC (1, struct frame_unwind_table);
  append_predicate (table, dummy_frame_sniffer);
  if (kgdb_sniffer_kluge != NULL)
    append_predicate (table, kgdb_sniffer_kluge);
  return table;
}

void
frame_unwind_append_sniffer (struct gdbarch *gdbarch,
			     frame_unwind_sniffer_ftype *sniffer)
{
  struct frame_unwind_table *table =
    gdbarch_data (gdbarch, frame_unwind_data);
  if (table == NULL)
    {
      /* ULGH, called during architecture initialization.  Patch
         things up.  */
      table = frame_unwind_init (gdbarch);
      set_gdbarch_data (gdbarch, frame_unwind_data, table);
    }
  append_predicate (table, sniffer);
}

const struct frame_unwind *
frame_unwind_find_by_frame (struct frame_info *next_frame)
{
  int i;
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct frame_unwind_table *table = gdbarch_data (gdbarch, frame_unwind_data);
  if (!DEPRECATED_USE_GENERIC_DUMMY_FRAMES && legacy_frame_p (gdbarch))
    /* Seriously old code.  Don't even try to use this new mechanism.
       (Note: The variable USE_GENERIC_DUMMY_FRAMES is deprecated, not
       the dummy frame mechanism.  All architectures should be using
       generic dummy frames).  */
    return legacy_saved_regs_unwind;
  for (i = 0; i < table->nr; i++)
    {
      const struct frame_unwind *desc;
      desc = table->sniffer[i] (next_frame);
      if (desc != NULL)
	return desc;
    }
  return legacy_saved_regs_unwind;
}

extern initialize_file_ftype _initialize_frame_unwind; /* -Wmissing-prototypes */

void
_initialize_frame_unwind (void)
{
  frame_unwind_data = register_gdbarch_data (frame_unwind_init);
}
