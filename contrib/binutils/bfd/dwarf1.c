/* DWARF 1 find nearest line (_bfd_dwarf1_find_nearest_line).
   Copyright 1998, 1999, 2000, 2001, 2002, 2004, 2005, 2007
   Free Software Foundation, Inc.

   Written by Gavin Romig-Koch of Cygnus Solutions (gavin@cygnus.com).

   This file is part of BFD.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf/dwarf.h"

/* dwarf1_debug is the starting point for all dwarf1 info.  */

struct dwarf1_debug
{
  /* The bfd we are working with.  */
  bfd* abfd;

  /* List of already parsed compilation units.  */
  struct dwarf1_unit* lastUnit;

  /* The buffer for the .debug section.
     Zero indicates that the .debug section failed to load.  */
  char* debug_section;

  /* Pointer to the end of the .debug_info section memory buffer.  */
  char* debug_section_end;

  /* The buffer for the .line section.  */
  char* line_section;

  /* End of that buffer.  */
  char* line_section_end;

  /* The current or next unread die within the .debug section.  */
  char* currentDie;
};

/* One dwarf1_unit for each parsed compilation unit die.  */

struct dwarf1_unit
{
  /* Linked starting from stash->lastUnit.  */
  struct dwarf1_unit* prev;

  /* Name of the compilation unit.  */
  char* name;

  /* The highest and lowest address used in the compilation unit.  */
  unsigned long low_pc;
  unsigned long high_pc;

  /* Does this unit have a statement list?  */
  int has_stmt_list;

  /* If any, the offset of the line number table in the .line section.  */
  unsigned long stmt_list_offset;

  /* If non-zero, a pointer to the first child of this unit.  */
  char* first_child;

  /* How many line entries?  */
  unsigned long line_count;

  /* The decoded line number table (line_count entries).  */
  struct linenumber* linenumber_table;

  /* The list of functions in this unit.  */
  struct dwarf1_func* func_list;
};

/* One dwarf1_func for each parsed function die.  */

struct dwarf1_func
{
  /* Linked starting from aUnit->func_list.  */
  struct dwarf1_func* prev;

  /* Name of function.  */
  char* name;

  /* The highest and lowest address used in the compilation unit.  */
  unsigned long low_pc;
  unsigned long high_pc;
};

/* Used to return info about a parsed die.  */
struct die_info
{
  unsigned long length;
  unsigned long sibling;
  unsigned long low_pc;
  unsigned long high_pc;
  unsigned long stmt_list_offset;

  char* name;

  int has_stmt_list;

  unsigned short tag;
};

/* Parsed line number information.  */
struct linenumber
{
  /* First address in the line.  */
  unsigned long addr;

  /* The line number.  */
  unsigned long linenumber;
};

/* Find the form of an attr, from the attr field.  */
#define FORM_FROM_ATTR(attr)	((attr) & 0xF)	/* Implicitly specified.  */

/* Return a newly allocated dwarf1_unit.  It should be cleared and
   then attached into the 'stash' at 'stash->lastUnit'.  */

static struct dwarf1_unit*
alloc_dwarf1_unit (struct dwarf1_debug* stash)
{
  bfd_size_type amt = sizeof (struct dwarf1_unit);

  struct dwarf1_unit* x = bfd_zalloc (stash->abfd, amt);
  x->prev = stash->lastUnit;
  stash->lastUnit = x;

  return x;
}

/* Return a newly allocated dwarf1_func.  It must be cleared and
   attached into 'aUnit' at 'aUnit->func_list'.  */

static struct dwarf1_func *
alloc_dwarf1_func (struct dwarf1_debug* stash, struct dwarf1_unit* aUnit)
{
  bfd_size_type amt = sizeof (struct dwarf1_func);

  struct dwarf1_func* x = bfd_zalloc (stash->abfd, amt);
  x->prev = aUnit->func_list;
  aUnit->func_list = x;

  return x;
}

/* parse_die - parse a Dwarf1 die.
   Parse the die starting at 'aDiePtr' into 'aDieInfo'.
   'abfd' must be the bfd from which the section that 'aDiePtr'
   points to was pulled from.

   Return FALSE if the die is invalidly formatted; TRUE otherwise.  */

static bfd_boolean
parse_die (bfd *             abfd,
	   struct die_info * aDieInfo,
	   char *            aDiePtr,
	   char *            aDiePtrEnd)
{
  char* this_die = aDiePtr;
  char* xptr = this_die;

  memset (aDieInfo, 0, sizeof (* aDieInfo));

  /* First comes the length.  */
  aDieInfo->length = bfd_get_32 (abfd, (bfd_byte *) xptr);
  xptr += 4;
  if (aDieInfo->length == 0
      || (this_die + aDieInfo->length) >= aDiePtrEnd)
    return FALSE;
  if (aDieInfo->length < 6)
    {
      /* Just padding bytes.  */
      aDieInfo->tag = TAG_padding;
      return TRUE;
    }

  /* Then the tag.  */
  aDieInfo->tag = bfd_get_16 (abfd, (bfd_byte *) xptr);
  xptr += 2;

  /* Then the attributes.  */
  while (xptr < (this_die + aDieInfo->length))
    {
      unsigned short attr;

      /* Parse the attribute based on its form.  This section
         must handle all dwarf1 forms, but need only handle the
	 actual attributes that we care about.  */
      attr = bfd_get_16 (abfd, (bfd_byte *) xptr);
      xptr += 2;

      switch (FORM_FROM_ATTR (attr))
	{
	case FORM_DATA2:
	  xptr += 2;
	  break;
	case FORM_DATA4:
	case FORM_REF:
	  if (attr == AT_sibling)
	    aDieInfo->sibling = bfd_get_32 (abfd, (bfd_byte *) xptr);
	  else if (attr == AT_stmt_list)
	    {
	      aDieInfo->stmt_list_offset = bfd_get_32 (abfd, (bfd_byte *) xptr);
	      aDieInfo->has_stmt_list = 1;
	    }
	  xptr += 4;
	  break;
	case FORM_DATA8:
	  xptr += 8;
	  break;
	case FORM_ADDR:
	  if (attr == AT_low_pc)
	    aDieInfo->low_pc = bfd_get_32 (abfd, (bfd_byte *) xptr);
	  else if (attr == AT_high_pc)
	    aDieInfo->high_pc = bfd_get_32 (abfd, (bfd_byte *) xptr);
	  xptr += 4;
	  break;
	case FORM_BLOCK2:
	  xptr += 2 + bfd_get_16 (abfd, (bfd_byte *) xptr);
	  break;
	case FORM_BLOCK4:
	  xptr += 4 + bfd_get_32 (abfd, (bfd_byte *) xptr);
	  break;
	case FORM_STRING:
	  if (attr == AT_name)
	    aDieInfo->name = xptr;
	  xptr += strlen (xptr) + 1;
	  break;
	}
    }

  return TRUE;
}

/* Parse a dwarf1 line number table for 'aUnit->stmt_list_offset'
   into 'aUnit->linenumber_table'.  Return FALSE if an error
   occurs; TRUE otherwise.  */

static bfd_boolean
parse_line_table (struct dwarf1_debug* stash, struct dwarf1_unit* aUnit)
{
  char* xptr;

  /* Load the ".line" section from the bfd if we haven't already.  */
  if (stash->line_section == 0)
    {
      asection *msec;
      bfd_size_type size;

      msec = bfd_get_section_by_name (stash->abfd, ".line");
      if (! msec)
	return FALSE;

      size = msec->rawsize ? msec->rawsize : msec->size;
      stash->line_section = bfd_alloc (stash->abfd, size);

      if (! stash->line_section)
	return FALSE;

      if (! bfd_get_section_contents (stash->abfd, msec, stash->line_section,
				      0, size))
	{
	  stash->line_section = 0;
	  return FALSE;
	}

      stash->line_section_end = stash->line_section + size;
    }

  xptr = stash->line_section + aUnit->stmt_list_offset;
  if (xptr < stash->line_section_end)
    {
      unsigned long eachLine;
      char *tblend;
      unsigned long base;
      bfd_size_type amt;

      /* First comes the length.  */
      tblend = bfd_get_32 (stash->abfd, (bfd_byte *) xptr) + xptr;
      xptr += 4;

      /* Then the base address for each address in the table.  */
      base = bfd_get_32 (stash->abfd, (bfd_byte *) xptr);
      xptr += 4;

      /* How many line entrys?
	 10 = 4 (line number) + 2 (pos in line) + 4 (address in line).  */
      aUnit->line_count = (tblend - xptr) / 10;

      /* Allocate an array for the entries.  */
      amt = sizeof (struct linenumber) * aUnit->line_count;
      aUnit->linenumber_table = bfd_alloc (stash->abfd, amt);

      for (eachLine = 0; eachLine < aUnit->line_count; eachLine++)
	{
	  /* A line number.  */
	  aUnit->linenumber_table[eachLine].linenumber
	    = bfd_get_32 (stash->abfd, (bfd_byte *) xptr);
	  xptr += 4;

	  /* Skip the position within the line.  */
	  xptr += 2;

	  /* And finally the address.  */
	  aUnit->linenumber_table[eachLine].addr
	    = base + bfd_get_32 (stash->abfd, (bfd_byte *) xptr);
	  xptr += 4;
	}
    }

  return TRUE;
}

/* Parse each function die in a compilation unit 'aUnit'.
   The first child die of 'aUnit' should be in 'aUnit->first_child',
   the result is placed in 'aUnit->func_list'.
   Return FALSE if error; TRUE otherwise.  */

static bfd_boolean
parse_functions_in_unit (struct dwarf1_debug* stash, struct dwarf1_unit* aUnit)
{
  char* eachDie;

  if (aUnit->first_child)
    for (eachDie = aUnit->first_child;
 	 eachDie < stash->debug_section_end;
	 )
      {
	struct die_info eachDieInfo;

	if (! parse_die (stash->abfd, &eachDieInfo, eachDie,
			 stash->debug_section_end))
	  return FALSE;

	if (eachDieInfo.tag == TAG_global_subroutine
	    || eachDieInfo.tag == TAG_subroutine
	    || eachDieInfo.tag == TAG_inlined_subroutine
	    || eachDieInfo.tag == TAG_entry_point)
	  {
	    struct dwarf1_func* aFunc = alloc_dwarf1_func (stash,aUnit);

	    aFunc->name = eachDieInfo.name;
	    aFunc->low_pc = eachDieInfo.low_pc;
	    aFunc->high_pc = eachDieInfo.high_pc;
	  }

	/* Move to next sibling, if none, end loop */
	if (eachDieInfo.sibling)
	  eachDie = stash->debug_section + eachDieInfo.sibling;
	else
	  break;
      }

  return TRUE;
}

/* Find the nearest line to 'addr' in 'aUnit'.
   Return whether we found the line (or a function) without error.  */

static bfd_boolean
dwarf1_unit_find_nearest_line (struct dwarf1_debug* stash,
			       struct dwarf1_unit* aUnit,
			       unsigned long addr,
			       const char **filename_ptr,
			       const char **functionname_ptr,
			       unsigned int *linenumber_ptr)
{
  int line_p = FALSE;
  int func_p = FALSE;

  if (aUnit->low_pc <= addr && addr < aUnit->high_pc)
    {
      if (aUnit->has_stmt_list)
	{
	  unsigned long i;
	  struct dwarf1_func* eachFunc;

	  if (! aUnit->linenumber_table)
	    {
	      if (! parse_line_table (stash, aUnit))
		return FALSE;
	    }

	  if (! aUnit->func_list)
	    {
	      if (! parse_functions_in_unit (stash, aUnit))
		return FALSE;
	    }

	  for (i = 0; i < aUnit->line_count; i++)
	    {
	      if (aUnit->linenumber_table[i].addr <= addr
		  && addr < aUnit->linenumber_table[i+1].addr)
		{
		  *filename_ptr = aUnit->name;
		  *linenumber_ptr = aUnit->linenumber_table[i].linenumber;
		  line_p = TRUE;
		  break;
		}
	    }

	  for (eachFunc = aUnit->func_list;
	       eachFunc;
	       eachFunc = eachFunc->prev)
	    {
	      if (eachFunc->low_pc <= addr
		  && addr < eachFunc->high_pc)
		{
		  *functionname_ptr = eachFunc->name;
		  func_p = TRUE;
		  break;
		}
	    }
	}
    }

  return line_p || func_p;
}

/* The DWARF 1 version of find_nearest line.
   Return TRUE if the line is found without error.  */

bfd_boolean
_bfd_dwarf1_find_nearest_line (bfd *abfd,
			       asection *section,
			       asymbol **symbols ATTRIBUTE_UNUSED,
			       bfd_vma offset,
			       const char **filename_ptr,
			       const char **functionname_ptr,
			       unsigned int *linenumber_ptr)
{
  struct dwarf1_debug *stash = elf_tdata (abfd)->dwarf1_find_line_info;

  struct dwarf1_unit* eachUnit;

  /* What address are we looking for? */
  unsigned long addr = (unsigned long)(offset + section->vma);

  *filename_ptr = NULL;
  *functionname_ptr = NULL;
  *linenumber_ptr = 0;

  if (! stash)
    {
      asection *msec;
      bfd_size_type size = sizeof (struct dwarf1_debug);

      stash = elf_tdata (abfd)->dwarf1_find_line_info
	= bfd_zalloc (abfd, size);

      if (! stash)
	return FALSE;

      msec = bfd_get_section_by_name (abfd, ".debug");
      if (! msec)
	/* No dwarf1 info.  Note that at this point the stash
	   has been allocated, but contains zeros, this lets
	   future calls to this function fail quicker.  */
	return FALSE;

      size = msec->rawsize ? msec->rawsize : msec->size;
      stash->debug_section = bfd_alloc (abfd, size);

      if (! stash->debug_section)
	return FALSE;

      if (! bfd_get_section_contents (abfd, msec, stash->debug_section,
				      0, size))
	{
	  stash->debug_section = 0;
	  return FALSE;
	}

      stash->debug_section_end = stash->debug_section + size;
      stash->currentDie = stash->debug_section;
      stash->abfd = abfd;
    }

  /* A null debug_section indicates that there was no dwarf1 info
     or that an error occured while setting up the stash.  */

  if (! stash->debug_section)
    return FALSE;

  /* Look at the previously parsed units to see if any contain
     the addr.  */
  for (eachUnit = stash->lastUnit; eachUnit; eachUnit = eachUnit->prev)
    if (eachUnit->low_pc <= addr && addr < eachUnit->high_pc)
      return dwarf1_unit_find_nearest_line (stash, eachUnit, addr,
					    filename_ptr,
					    functionname_ptr,
					    linenumber_ptr);

  while (stash->currentDie < stash->debug_section_end)
    {
      struct die_info aDieInfo;

      if (! parse_die (stash->abfd, &aDieInfo, stash->currentDie,
		       stash->debug_section_end))
	return FALSE;

      if (aDieInfo.tag == TAG_compile_unit)
	{
	  struct dwarf1_unit* aUnit
	    = alloc_dwarf1_unit (stash);

	  aUnit->name = aDieInfo.name;
	  aUnit->low_pc = aDieInfo.low_pc;
	  aUnit->high_pc = aDieInfo.high_pc;
	  aUnit->has_stmt_list = aDieInfo.has_stmt_list;
	  aUnit->stmt_list_offset = aDieInfo.stmt_list_offset;

	  /* A die has a child if it's followed by a die that is
	     not it's sibling.  */
	  if (aDieInfo.sibling
	      && stash->currentDie + aDieInfo.length
                    < stash->debug_section_end
	      && stash->currentDie + aDieInfo.length
	            != stash->debug_section + aDieInfo.sibling)
	    aUnit->first_child = stash->currentDie + aDieInfo.length;
	  else
	    aUnit->first_child = 0;

	  if (aUnit->low_pc <= addr && addr < aUnit->high_pc)
	    return dwarf1_unit_find_nearest_line (stash, aUnit, addr,
						  filename_ptr,
						  functionname_ptr,
						  linenumber_ptr);
	}

      if (aDieInfo.sibling != 0)
	stash->currentDie = stash->debug_section + aDieInfo.sibling;
      else
	stash->currentDie += aDieInfo.length;
    }

  return FALSE;
}
