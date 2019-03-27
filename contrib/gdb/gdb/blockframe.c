/* Get info from stack frames; convert between frames, blocks,
   functions and pc values.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
#include "symtab.h"
#include "bfd.h"
#include "objfiles.h"
#include "frame.h"
#include "gdbcore.h"
#include "value.h"		/* for read_register */
#include "target.h"		/* for target_has_stack */
#include "inferior.h"		/* for read_pc */
#include "annotate.h"
#include "regcache.h"
#include "gdb_assert.h"
#include "dummy-frame.h"
#include "command.h"
#include "gdbcmd.h"
#include "block.h"

/* Prototypes for exported functions. */

void _initialize_blockframe (void);

/* Is ADDR inside the startup file?  Note that if your machine has a
   way to detect the bottom of the stack, there is no need to call
   this function from DEPRECATED_FRAME_CHAIN_VALID; the reason for
   doing so is that some machines have no way of detecting bottom of
   stack.

   A PC of zero is always considered to be the bottom of the stack. */

int
deprecated_inside_entry_file (CORE_ADDR addr)
{
  if (addr == 0)
    return 1;
  if (symfile_objfile == 0)
    return 0;
  if (CALL_DUMMY_LOCATION == AT_ENTRY_POINT
      || CALL_DUMMY_LOCATION == AT_SYMBOL)
    {
      /* Do not stop backtracing if the pc is in the call dummy
         at the entry point.  */
      /* FIXME: Won't always work with zeros for the last two arguments */
      if (DEPRECATED_PC_IN_CALL_DUMMY (addr, 0, 0))
	return 0;
    }
  return (addr >= symfile_objfile->ei.deprecated_entry_file_lowpc &&
	  addr < symfile_objfile->ei.deprecated_entry_file_highpc);
}

/* Test whether PC is in the range of addresses that corresponds to
   the "main" function.  */

int
inside_main_func (CORE_ADDR pc)
{
  struct minimal_symbol *msymbol;

  if (symfile_objfile == 0)
    return 0;

  msymbol = lookup_minimal_symbol (main_name (), NULL, symfile_objfile);

  /* If the address range hasn't been set up at symbol reading time,
     set it up now.  */

  if (msymbol != NULL
      && symfile_objfile->ei.main_func_lowpc == INVALID_ENTRY_LOWPC
      && symfile_objfile->ei.main_func_highpc == INVALID_ENTRY_HIGHPC)
    {
      /* brobecker/2003-10-10: We used to rely on lookup_symbol() to
	 search the symbol associated to the "main" function.
	 Unfortunately, lookup_symbol() uses the current-language
	 la_lookup_symbol_nonlocal function to do the global symbol
	 search.  Depending on the language, this can introduce
	 certain side-effects, because certain languages, for instance
	 Ada, may find more than one match.  Therefore we prefer to
	 search the "main" function symbol using its address rather
	 than its name.  */
      struct symbol *mainsym =
	find_pc_function (SYMBOL_VALUE_ADDRESS (msymbol));

      if (mainsym && SYMBOL_CLASS (mainsym) == LOC_BLOCK)
	{
	  symfile_objfile->ei.main_func_lowpc =
	    BLOCK_START (SYMBOL_BLOCK_VALUE (mainsym));
	  symfile_objfile->ei.main_func_highpc =
	    BLOCK_END (SYMBOL_BLOCK_VALUE (mainsym));
	}
    }

  /* Not in the normal symbol tables, see if "main" is in the partial
     symbol table.  If it's not, then give up.  */
  if (msymbol != NULL && MSYMBOL_TYPE (msymbol) == mst_text)
    {
      CORE_ADDR maddr = SYMBOL_VALUE_ADDRESS (msymbol);
      asection *msect = SYMBOL_BFD_SECTION (msymbol);
      struct obj_section *osect = find_pc_sect_section (maddr, msect);

      if (osect != NULL)
	{
	  int i;

	  /* Step over other symbols at this same address, and symbols
	     in other sections, to find the next symbol in this
	     section with a different address.  */
	  for (i = 1; SYMBOL_LINKAGE_NAME (msymbol + i) != NULL; i++)
	    {
	      if (SYMBOL_VALUE_ADDRESS (msymbol + i) != maddr
		  && SYMBOL_BFD_SECTION (msymbol + i) == msect)
		break;
	    }

	  symfile_objfile->ei.main_func_lowpc = maddr;

	  /* Use the lesser of the next minimal symbol in the same
	     section, or the end of the section, as the end of the
	     function.  */
	  if (SYMBOL_LINKAGE_NAME (msymbol + i) != NULL
	      && SYMBOL_VALUE_ADDRESS (msymbol + i) < osect->endaddr)
	    symfile_objfile->ei.main_func_highpc =
	      SYMBOL_VALUE_ADDRESS (msymbol + i);
	  else
	    /* We got the start address from the last msymbol in the
	       objfile.  So the end address is the end of the
	       section.  */
	    symfile_objfile->ei.main_func_highpc = osect->endaddr;
	}
    }

  return (symfile_objfile->ei.main_func_lowpc <= pc
	  && symfile_objfile->ei.main_func_highpc > pc);
}

/* Test whether THIS_FRAME is inside the process entry point function.  */

int
inside_entry_func (struct frame_info *this_frame)
{
  return (get_frame_func (this_frame) == entry_point_address ());
}

/* Similar to inside_entry_func, but accomodating legacy frame code.  */

static int
legacy_inside_entry_func (CORE_ADDR pc)
{
  if (symfile_objfile == 0)
    return 0;

  if (CALL_DUMMY_LOCATION == AT_ENTRY_POINT)
    {
      /* Do not stop backtracing if the program counter is in the call
         dummy at the entry point.  */
      /* FIXME: This won't always work with zeros for the last two
         arguments.  */
      if (DEPRECATED_PC_IN_CALL_DUMMY (pc, 0, 0))
	return 0;
    }

  return (symfile_objfile->ei.entry_func_lowpc <= pc
	  && symfile_objfile->ei.entry_func_highpc > pc);
}

/* Return nonzero if the function for this frame lacks a prologue.
   Many machines can define DEPRECATED_FRAMELESS_FUNCTION_INVOCATION
   to just call this function.  */

int
legacy_frameless_look_for_prologue (struct frame_info *frame)
{
  CORE_ADDR func_start;

  func_start = get_frame_func (frame);
  if (func_start)
    {
      func_start += FUNCTION_START_OFFSET;
      /* NOTE: cagney/2004-02-09: Eliminated per-architecture
         PROLOGUE_FRAMELESS_P call as architectures with custom
         implementations had all been deleted.  Eventually even this
         function can go - GDB no longer tries to differentiate
         between framed, frameless and stackless functions.  They are
         all now considered equally evil :-^.  */
      /* If skipping the prologue ends up skips nothing, there must be
         no prologue and hence no code creating a frame.  There for
         the function is "frameless" :-/.  */
      return func_start == SKIP_PROLOGUE (func_start);
    }
  else if (get_frame_pc (frame) == 0)
    /* A frame with a zero PC is usually created by dereferencing a
       NULL function pointer, normally causing an immediate core dump
       of the inferior. Mark function as frameless, as the inferior
       has no chance of setting up a stack frame.  */
    return 1;
  else
    /* If we can't find the start of the function, we don't really
       know whether the function is frameless, but we should be able
       to get a reasonable (i.e. best we can do under the
       circumstances) backtrace by saying that it isn't.  */
    return 0;
}

/* Return the innermost lexical block in execution
   in a specified stack frame.  The frame address is assumed valid.

   If ADDR_IN_BLOCK is non-zero, set *ADDR_IN_BLOCK to the exact code
   address we used to choose the block.  We use this to find a source
   line, to decide which macro definitions are in scope.

   The value returned in *ADDR_IN_BLOCK isn't necessarily the frame's
   PC, and may not really be a valid PC at all.  For example, in the
   caller of a function declared to never return, the code at the
   return address will never be reached, so the call instruction may
   be the very last instruction in the block.  So the address we use
   to choose the block is actually one byte before the return address
   --- hopefully pointing us at the call instruction, or its delay
   slot instruction.  */

struct block *
get_frame_block (struct frame_info *frame, CORE_ADDR *addr_in_block)
{
  const CORE_ADDR pc = get_frame_address_in_block (frame);

  if (addr_in_block)
    *addr_in_block = pc;

  return block_for_pc (pc);
}

CORE_ADDR
get_pc_function_start (CORE_ADDR pc)
{
  struct block *bl;
  struct minimal_symbol *msymbol;

  bl = block_for_pc (pc);
  if (bl)
    {
      struct symbol *symbol = block_function (bl);

      if (symbol)
	{
	  bl = SYMBOL_BLOCK_VALUE (symbol);
	  return BLOCK_START (bl);
	}
    }

  msymbol = lookup_minimal_symbol_by_pc (pc);
  if (msymbol)
    {
      CORE_ADDR fstart = SYMBOL_VALUE_ADDRESS (msymbol);

      if (find_pc_section (fstart))
	return fstart;
    }

  return 0;
}

/* Return the symbol for the function executing in frame FRAME.  */

struct symbol *
get_frame_function (struct frame_info *frame)
{
  struct block *bl = get_frame_block (frame, 0);
  if (bl == 0)
    return 0;
  return block_function (bl);
}


/* Return the function containing pc value PC in section SECTION.
   Returns 0 if function is not known.  */

struct symbol *
find_pc_sect_function (CORE_ADDR pc, struct bfd_section *section)
{
  struct block *b = block_for_pc_sect (pc, section);
  if (b == 0)
    return 0;
  return block_function (b);
}

/* Return the function containing pc value PC.
   Returns 0 if function is not known.  Backward compatibility, no section */

struct symbol *
find_pc_function (CORE_ADDR pc)
{
  return find_pc_sect_function (pc, find_pc_mapped_section (pc));
}

/* These variables are used to cache the most recent result
 * of find_pc_partial_function. */

static CORE_ADDR cache_pc_function_low = 0;
static CORE_ADDR cache_pc_function_high = 0;
static char *cache_pc_function_name = 0;
static struct bfd_section *cache_pc_function_section = NULL;

/* Clear cache, e.g. when symbol table is discarded. */

void
clear_pc_function_cache (void)
{
  cache_pc_function_low = 0;
  cache_pc_function_high = 0;
  cache_pc_function_name = (char *) 0;
  cache_pc_function_section = NULL;
}

/* Finds the "function" (text symbol) that is smaller than PC but
   greatest of all of the potential text symbols in SECTION.  Sets
   *NAME and/or *ADDRESS conditionally if that pointer is non-null.
   If ENDADDR is non-null, then set *ENDADDR to be the end of the
   function (exclusive), but passing ENDADDR as non-null means that
   the function might cause symbols to be read.  This function either
   succeeds or fails (not halfway succeeds).  If it succeeds, it sets
   *NAME, *ADDRESS, and *ENDADDR to real information and returns 1.
   If it fails, it sets *NAME, *ADDRESS, and *ENDADDR to zero and
   returns 0.  */

int
find_pc_sect_partial_function (CORE_ADDR pc, asection *section, char **name,
			       CORE_ADDR *address, CORE_ADDR *endaddr)
{
  struct partial_symtab *pst;
  struct symbol *f;
  struct minimal_symbol *msymbol;
  struct partial_symbol *psb;
  struct obj_section *osect;
  int i;
  CORE_ADDR mapped_pc;

  mapped_pc = overlay_mapped_address (pc, section);

  if (mapped_pc >= cache_pc_function_low
      && mapped_pc < cache_pc_function_high
      && section == cache_pc_function_section)
    goto return_cached_value;

  /* If sigtramp is in the u area, it counts as a function (especially
     important for step_1).  */
  if (SIGTRAMP_START_P () && PC_IN_SIGTRAMP (mapped_pc, (char *) NULL))
    {
      cache_pc_function_low = SIGTRAMP_START (mapped_pc);
      cache_pc_function_high = SIGTRAMP_END (mapped_pc);
      cache_pc_function_name = "<sigtramp>";
      cache_pc_function_section = section;
      goto return_cached_value;
    }

  msymbol = lookup_minimal_symbol_by_pc_section (mapped_pc, section);
  pst = find_pc_sect_psymtab (mapped_pc, section);
  if (pst)
    {
      /* Need to read the symbols to get a good value for the end address.  */
      if (endaddr != NULL && !pst->readin)
	{
	  /* Need to get the terminal in case symbol-reading produces
	     output.  */
	  target_terminal_ours_for_output ();
	  PSYMTAB_TO_SYMTAB (pst);
	}

      if (pst->readin)
	{
	  /* Checking whether the msymbol has a larger value is for the
	     "pathological" case mentioned in print_frame_info.  */
	  f = find_pc_sect_function (mapped_pc, section);
	  if (f != NULL
	      && (msymbol == NULL
		  || (BLOCK_START (SYMBOL_BLOCK_VALUE (f))
		      >= SYMBOL_VALUE_ADDRESS (msymbol))))
	    {
	      cache_pc_function_low = BLOCK_START (SYMBOL_BLOCK_VALUE (f));
	      cache_pc_function_high = BLOCK_END (SYMBOL_BLOCK_VALUE (f));
	      cache_pc_function_name = DEPRECATED_SYMBOL_NAME (f);
	      cache_pc_function_section = section;
	      goto return_cached_value;
	    }
	}
      else
	{
	  /* Now that static symbols go in the minimal symbol table, perhaps
	     we could just ignore the partial symbols.  But at least for now
	     we use the partial or minimal symbol, whichever is larger.  */
	  psb = find_pc_sect_psymbol (pst, mapped_pc, section);

	  if (psb
	      && (msymbol == NULL ||
		  (SYMBOL_VALUE_ADDRESS (psb)
		   >= SYMBOL_VALUE_ADDRESS (msymbol))))
	    {
	      /* This case isn't being cached currently. */
	      if (address)
		*address = SYMBOL_VALUE_ADDRESS (psb);
	      if (name)
		*name = DEPRECATED_SYMBOL_NAME (psb);
	      /* endaddr non-NULL can't happen here.  */
	      return 1;
	    }
	}
    }

  /* Not in the normal symbol tables, see if the pc is in a known section.
     If it's not, then give up.  This ensures that anything beyond the end
     of the text seg doesn't appear to be part of the last function in the
     text segment.  */

  osect = find_pc_sect_section (mapped_pc, section);

  if (!osect)
    msymbol = NULL;

  /* Must be in the minimal symbol table.  */
  if (msymbol == NULL)
    {
      /* No available symbol.  */
      if (name != NULL)
	*name = 0;
      if (address != NULL)
	*address = 0;
      if (endaddr != NULL)
	*endaddr = 0;
      return 0;
    }

  cache_pc_function_low = SYMBOL_VALUE_ADDRESS (msymbol);
  cache_pc_function_name = DEPRECATED_SYMBOL_NAME (msymbol);
  cache_pc_function_section = section;

  /* Use the lesser of the next minimal symbol in the same section, or
     the end of the section, as the end of the function.  */

  /* Step over other symbols at this same address, and symbols in
     other sections, to find the next symbol in this section with
     a different address.  */

  for (i = 1; DEPRECATED_SYMBOL_NAME (msymbol + i) != NULL; i++)
    {
      if (SYMBOL_VALUE_ADDRESS (msymbol + i) != SYMBOL_VALUE_ADDRESS (msymbol)
	  && SYMBOL_BFD_SECTION (msymbol + i) == SYMBOL_BFD_SECTION (msymbol))
	break;
    }

  if (DEPRECATED_SYMBOL_NAME (msymbol + i) != NULL
      && SYMBOL_VALUE_ADDRESS (msymbol + i) < osect->endaddr)
    cache_pc_function_high = SYMBOL_VALUE_ADDRESS (msymbol + i);
  else
    /* We got the start address from the last msymbol in the objfile.
       So the end address is the end of the section.  */
    cache_pc_function_high = osect->endaddr;

 return_cached_value:

  if (address)
    {
      if (pc_in_unmapped_range (pc, section))
	*address = overlay_unmapped_address (cache_pc_function_low, section);
      else
	*address = cache_pc_function_low;
    }

  if (name)
    *name = cache_pc_function_name;

  if (endaddr)
    {
      if (pc_in_unmapped_range (pc, section))
	{
	  /* Because the high address is actually beyond the end of
	     the function (and therefore possibly beyond the end of
	     the overlay), we must actually convert (high - 1) and
	     then add one to that. */

	  *endaddr = 1 + overlay_unmapped_address (cache_pc_function_high - 1,
						   section);
	}
      else
	*endaddr = cache_pc_function_high;
    }

  return 1;
}

/* Backward compatibility, no section argument.  */

int
find_pc_partial_function (CORE_ADDR pc, char **name, CORE_ADDR *address,
			  CORE_ADDR *endaddr)
{
  struct bfd_section *bfd_section;

  /* To ensure that the symbol returned belongs to the correct setion
     (and that the last [random] symbol from the previous section
     isn't returned) try to find the section containing PC.  First try
     the overlay code (which by default returns NULL); and second try
     the normal section code (which almost always succeeds).  */
  bfd_section = find_pc_overlay (pc);
  if (bfd_section == NULL)
    {
      struct obj_section *obj_section = find_pc_section (pc);
      if (obj_section == NULL)
	bfd_section = NULL;
      else
	bfd_section = obj_section->the_bfd_section;
    }
  return find_pc_sect_partial_function (pc, bfd_section, name, address,
					endaddr);
}

/* Return the innermost stack frame executing inside of BLOCK,
   or NULL if there is no such frame.  If BLOCK is NULL, just return NULL.  */

struct frame_info *
block_innermost_frame (struct block *block)
{
  struct frame_info *frame;
  CORE_ADDR start;
  CORE_ADDR end;
  CORE_ADDR calling_pc;

  if (block == NULL)
    return NULL;

  start = BLOCK_START (block);
  end = BLOCK_END (block);

  frame = NULL;
  while (1)
    {
      frame = get_prev_frame (frame);
      if (frame == NULL)
	return NULL;
      calling_pc = get_frame_address_in_block (frame);
      if (calling_pc >= start && calling_pc < end)
	return frame;
    }
}

/* Are we in a call dummy?  The code below which allows DECR_PC_AFTER_BREAK
   below is for infrun.c, which may give the macro a pc without that
   subtracted out.  */

/* Is the PC in a call dummy?  SP and FRAME_ADDRESS are the bottom and
   top of the stack frame which we are checking, where "bottom" and
   "top" refer to some section of memory which contains the code for
   the call dummy.  Calls to this macro assume that the contents of
   SP_REGNUM and DEPRECATED_FP_REGNUM (or the saved values thereof),
   respectively, are the things to pass.

   This won't work on the 29k, where SP_REGNUM and
   DEPRECATED_FP_REGNUM don't have that meaning, but the 29k doesn't
   use ON_STACK.  This could be fixed by generalizing this scheme,
   perhaps by passing in a frame and adding a few fields, at least on
   machines which need them for DEPRECATED_PC_IN_CALL_DUMMY.

   Something simpler, like checking for the stack segment, doesn't work,
   since various programs (threads implementations, gcc nested function
   stubs, etc) may either allocate stack frames in another segment, or
   allocate other kinds of code on the stack.  */

int
deprecated_pc_in_call_dummy_on_stack (CORE_ADDR pc, CORE_ADDR sp,
				      CORE_ADDR frame_address)
{
  return (INNER_THAN ((sp), (pc))
	  && (frame_address != 0)
	  && INNER_THAN ((pc), (frame_address)));
}

int
deprecated_pc_in_call_dummy_at_entry_point (CORE_ADDR pc, CORE_ADDR sp,
					    CORE_ADDR frame_address)
{
  CORE_ADDR addr = entry_point_address ();
  return ((pc) >= addr && (pc) <= (addr + DECR_PC_AFTER_BREAK));
}

/* Returns true for a user frame or a call_function_by_hand dummy
   frame, and false for the CRT0 start-up frame.  Purpose is to
   terminate backtrace.  */

int
legacy_frame_chain_valid (CORE_ADDR fp, struct frame_info *fi)
{
  /* Don't prune CALL_DUMMY frames.  */
  if (DEPRECATED_USE_GENERIC_DUMMY_FRAMES
      && DEPRECATED_PC_IN_CALL_DUMMY (get_frame_pc (fi), 0, 0))
    return 1;

  /* If the new frame pointer is zero, then it isn't valid.  */
  if (fp == 0)
    return 0;
  
  /* If the new frame would be inside (younger than) the previous frame,
     then it isn't valid.  */
  if (INNER_THAN (fp, get_frame_base (fi)))
    return 0;
  
  /* If the architecture has a custom DEPRECATED_FRAME_CHAIN_VALID,
     call it now.  */
  if (DEPRECATED_FRAME_CHAIN_VALID_P ())
    return DEPRECATED_FRAME_CHAIN_VALID (fp, fi);

  /* If we're already inside the entry function for the main objfile, then it
     isn't valid.  */
  if (legacy_inside_entry_func (get_frame_pc (fi)))
    return 0;

  /* If we're inside the entry file, it isn't valid.  */
  /* NOTE/drow 2002-12-25: should there be a way to disable this check?  It
     assumes a single small entry file, and the way some debug readers (e.g.
     dbxread) figure out which object is the entry file is somewhat hokey.  */
  if (deprecated_inside_entry_file (frame_pc_unwind (fi)))
      return 0;

  return 1;
}
