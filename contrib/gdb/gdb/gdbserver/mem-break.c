/* Memory breakpoint operations for the remote server for GDB.
   Copyright 2002
   Free Software Foundation, Inc.

   Contributed by MontaVista Software.

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

#include "server.h"

const char *breakpoint_data;
int breakpoint_len;

#define MAX_BREAKPOINT_LEN 8

struct breakpoint
{
  struct breakpoint *next;
  CORE_ADDR pc;
  unsigned char old_data[MAX_BREAKPOINT_LEN];

  /* Non-zero iff we are stepping over this breakpoint.  */
  int reinserting;

  /* Non-NULL iff this breakpoint was inserted to step over
     another one.  Points to the other breakpoint (which is also
     in the *next chain somewhere).  */
  struct breakpoint *breakpoint_to_reinsert;

  /* Function to call when we hit this breakpoint.  */
  void (*handler) (CORE_ADDR);
};

struct breakpoint *breakpoints;

void
set_breakpoint_at (CORE_ADDR where, void (*handler) (CORE_ADDR))
{
  struct breakpoint *bp;

  if (breakpoint_data == NULL)
    error ("Target does not support breakpoints.");

  bp = malloc (sizeof (struct breakpoint));
  memset (bp, 0, sizeof (struct breakpoint));

  (*the_target->read_memory) (where, bp->old_data,
			      breakpoint_len);
  (*the_target->write_memory) (where, breakpoint_data,
			       breakpoint_len);

  bp->pc = where;
  bp->handler = handler;

  bp->next = breakpoints;
  breakpoints = bp;
}

static void
delete_breakpoint (struct breakpoint *bp)
{
  struct breakpoint *cur;

  if (breakpoints == bp)
    {
      breakpoints = bp->next;
      (*the_target->write_memory) (bp->pc, bp->old_data,
				   breakpoint_len);
      free (bp);
      return;
    }
  cur = breakpoints;
  while (cur->next)
    {
      if (cur->next == bp)
	{
	  cur->next = bp->next;
	  (*the_target->write_memory) (bp->pc, bp->old_data,
				       breakpoint_len);
	  free (bp);
	  return;
	}
    }
  warning ("Could not find breakpoint in list.");
}

static struct breakpoint *
find_breakpoint_at (CORE_ADDR where)
{
  struct breakpoint *bp = breakpoints;

  while (bp != NULL)
    {
      if (bp->pc == where)
	return bp;
      bp = bp->next;
    }

  return NULL;
}

static void
reinsert_breakpoint_handler (CORE_ADDR stop_pc)
{
  struct breakpoint *stop_bp, *orig_bp;

  stop_bp = find_breakpoint_at (stop_pc);
  if (stop_bp == NULL)
    error ("lost the stopping breakpoint.");

  orig_bp = stop_bp->breakpoint_to_reinsert;
  if (orig_bp == NULL)
    error ("no breakpoint to reinsert");

  (*the_target->write_memory) (orig_bp->pc, breakpoint_data,
			       breakpoint_len);
  orig_bp->reinserting = 0;
  delete_breakpoint (stop_bp);
}

void
reinsert_breakpoint_by_bp (CORE_ADDR stop_pc, CORE_ADDR stop_at)
{
  struct breakpoint *bp, *orig_bp;

  set_breakpoint_at (stop_at, reinsert_breakpoint_handler);

  orig_bp = find_breakpoint_at (stop_pc);
  if (orig_bp == NULL)
    error ("Could not find original breakpoint in list.");

  bp = find_breakpoint_at (stop_at);
  if (bp == NULL)
    error ("Could not find breakpoint in list (reinserting by breakpoint).");
  bp->breakpoint_to_reinsert = orig_bp;

  (*the_target->write_memory) (orig_bp->pc, orig_bp->old_data,
			       breakpoint_len);
  orig_bp->reinserting = 1;
}

void
uninsert_breakpoint (CORE_ADDR stopped_at)
{
  struct breakpoint *bp;

  bp = find_breakpoint_at (stopped_at);
  if (bp == NULL)
    error ("Could not find breakpoint in list (uninserting).");

  (*the_target->write_memory) (bp->pc, bp->old_data,
			       breakpoint_len);
  bp->reinserting = 1;
}

void
reinsert_breakpoint (CORE_ADDR stopped_at)
{
  struct breakpoint *bp;

  bp = find_breakpoint_at (stopped_at);
  if (bp == NULL)
    error ("Could not find breakpoint in list (uninserting).");
  if (! bp->reinserting)
    error ("Breakpoint already inserted at reinsert time.");

  (*the_target->write_memory) (bp->pc, breakpoint_data,
			       breakpoint_len);
  bp->reinserting = 0;
}

int
check_breakpoints (CORE_ADDR stop_pc)
{
  struct breakpoint *bp;

  bp = find_breakpoint_at (stop_pc);
  if (bp == NULL)
    return 0;
  if (bp->reinserting)
    {
      warning ("Hit a removed breakpoint?");
      return 0;
    }

  (*bp->handler) (bp->pc);
  return 1;
}

void
set_breakpoint_data (const char *bp_data, int bp_len)
{
  breakpoint_data = bp_data;
  breakpoint_len = bp_len;
}

void
check_mem_read (CORE_ADDR mem_addr, char *buf, int mem_len)
{
  struct breakpoint *bp = breakpoints;
  CORE_ADDR mem_end = mem_addr + mem_len;

  for (; bp != NULL; bp = bp->next)
    {
      CORE_ADDR bp_end = bp->pc + breakpoint_len;
      CORE_ADDR start, end;
      int copy_offset, copy_len, buf_offset;

      if (mem_addr >= bp_end)
	continue;
      if (bp->pc >= mem_end)
	continue;

      start = bp->pc;
      if (mem_addr > start)
	start = mem_addr;

      end = bp_end;
      if (end > mem_end)
	end = mem_end;

      copy_len = end - start;
      copy_offset = start - bp->pc;
      buf_offset = start - mem_addr;

      memcpy (buf + buf_offset, bp->old_data + copy_offset, copy_len);
    }
}

void
check_mem_write (CORE_ADDR mem_addr, char *buf, int mem_len)
{
  struct breakpoint *bp = breakpoints;
  CORE_ADDR mem_end = mem_addr + mem_len;

  for (; bp != NULL; bp = bp->next)
    {
      CORE_ADDR bp_end = bp->pc + breakpoint_len;
      CORE_ADDR start, end;
      int copy_offset, copy_len, buf_offset;

      if (mem_addr >= bp_end)
	continue;
      if (bp->pc >= mem_end)
	continue;

      start = bp->pc;
      if (mem_addr > start)
	start = mem_addr;

      end = bp_end;
      if (end > mem_end)
	end = mem_end;

      copy_len = end - start;
      copy_offset = start - bp->pc;
      buf_offset = start - mem_addr;

      memcpy (bp->old_data + copy_offset, buf + buf_offset, copy_len);
      if (bp->reinserting == 0)
	memcpy (buf + buf_offset, breakpoint_data + copy_offset, copy_len);
    }
}
