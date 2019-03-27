/* Very simple "bfd" target, for GDB, the GNU debugger.

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
#include "target.h"
#include "bfd-target.h"
#include "gdb_assert.h"
#include "gdb_string.h"

/* Locate all mappable sections of a BFD file, filling in a target
   section for each.  */

struct section_closure
{
  struct section_table *end;
};

static void
add_to_section_table (struct bfd *abfd, struct bfd_section *asect,
		      void *closure)
{
  struct section_closure *pp = closure;
  flagword aflag;

  /* NOTE: cagney/2003-10-22: Is this pruning useful?  */
  aflag = bfd_get_section_flags (abfd, asect);
  if (!(aflag & SEC_ALLOC))
    return;
  if (bfd_section_size (abfd, asect) == 0)
    return;
  pp->end->bfd = abfd;
  pp->end->the_bfd_section = asect;
  pp->end->addr = bfd_section_vma (abfd, asect);
  pp->end->endaddr = pp->end->addr + bfd_section_size (abfd, asect);
  pp->end++;
}

void
build_target_sections_from_bfd (struct target_ops *targ, struct bfd *abfd)
{
  unsigned count;
  struct section_table *start;
  struct section_closure cl;

  count = bfd_count_sections (abfd);
  target_resize_to_sections (targ, count);
  start = targ->to_sections;
  cl.end = targ->to_sections;
  bfd_map_over_sections (abfd, add_to_section_table, &cl);
  gdb_assert (cl.end - start <= count);
}

LONGEST
target_bfd_xfer_partial (struct target_ops *ops,
			 enum target_object object,
			 const char *annex, void *readbuf,
			 const void *writebuf, ULONGEST offset, LONGEST len)
{
  switch (object)
    {
    case TARGET_OBJECT_MEMORY:
      {
	struct section_table *s = target_section_by_addr (ops, offset);
	if (s == NULL)
	  return -1;
	/* If the length extends beyond the section, truncate it.  Be
           careful to not suffer from overflow (wish S contained a
           length).  */
	if ((offset - s->addr + len) > (s->endaddr - s->addr))
	  len = (s->endaddr - s->addr) - (offset - s->addr);
	if (readbuf != NULL
	    && !bfd_get_section_contents (s->bfd, s->the_bfd_section,
					  readbuf, offset - s->addr, len))
	  return -1;
#if 1
	if (writebuf != NULL)
	  return -1;
#else
	/* FIXME: cagney/2003-10-31: The BFD interface doesn't yet
           take a const buffer.  */
	if (writebuf != NULL
	    && !bfd_set_section_contents (s->bfd, s->the_bfd_section,
					  writebuf, offset - s->addr, len))
	  return -1;
#endif
	return len;
      }
    default:
      return -1;
    }
}

void
target_bfd_xclose (struct target_ops *t, int quitting)
{
  bfd_close (t->to_data);
  xfree (t->to_sections);
  xfree (t);
}

struct target_ops *
target_bfd_reopen (struct bfd *bfd)
{
  struct target_ops *t = XZALLOC (struct target_ops);
  t->to_shortname = "bfd";
  t->to_longname = "BFD backed target";
  t->to_doc = "You should never see this";
  t->to_xfer_partial = target_bfd_xfer_partial;
  t->to_xclose = target_bfd_xclose;
  t->to_data = bfd;
  build_target_sections_from_bfd (t, bfd);
  return t;
}
