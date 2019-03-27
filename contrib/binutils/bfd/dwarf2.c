/* DWARF 2 support.
   Copyright 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007 Free Software Foundation, Inc.

   Adapted from gdb/dwarf2read.c by Gavin Koch of Cygnus Solutions
   (gavin@cygnus.com).

   From the dwarf2read.c header:
   Adapted by Gary Funck (gary@intrepid.com), Intrepid Technology,
   Inc.  with support from Florida State University (under contract
   with the Ada Joint Program Office), and Silicon Graphics, Inc.
   Initial contribution by Brent Benson, Harris Computer Systems, Inc.,
   based on Fred Fish's (Cygnus Support) implementation of DWARF 1
   support in dwarfread.c

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
#include "elf/dwarf2.h"

/* The data in the .debug_line statement prologue looks like this.  */

struct line_head
{
  bfd_vma total_length;
  unsigned short version;
  bfd_vma prologue_length;
  unsigned char minimum_instruction_length;
  unsigned char default_is_stmt;
  int line_base;
  unsigned char line_range;
  unsigned char opcode_base;
  unsigned char *standard_opcode_lengths;
};

/* Attributes have a name and a value.  */

struct attribute
{
  enum dwarf_attribute name;
  enum dwarf_form form;
  union
  {
    char *str;
    struct dwarf_block *blk;
    bfd_uint64_t val;
    bfd_int64_t sval;
  }
  u;
};

/* Blocks are a bunch of untyped bytes.  */
struct dwarf_block
{
  unsigned int size;
  bfd_byte *data;
};

struct loadable_section
{
  asection *section;
  bfd_vma adj_vma;
};

struct dwarf2_debug
{
  /* A list of all previously read comp_units.  */
  struct comp_unit *all_comp_units;

  /* The next unread compilation unit within the .debug_info section.
     Zero indicates that the .debug_info section has not been loaded
     into a buffer yet.  */
  bfd_byte *info_ptr;

  /* Pointer to the end of the .debug_info section memory buffer.  */
  bfd_byte *info_ptr_end;

  /* Pointer to the bfd, section and address of the beginning of the
     section.  The bfd might be different than expected because of
     gnu_debuglink sections.  */
  bfd * bfd;
  asection *sec;
  bfd_byte *sec_info_ptr;

  /* Pointer to the symbol table.  */
  asymbol **syms;

  /* Pointer to the .debug_abbrev section loaded into memory.  */
  bfd_byte *dwarf_abbrev_buffer;

  /* Length of the loaded .debug_abbrev section.  */
  unsigned long dwarf_abbrev_size;

  /* Buffer for decode_line_info.  */
  bfd_byte *dwarf_line_buffer;

  /* Length of the loaded .debug_line section.  */
  unsigned long dwarf_line_size;

  /* Pointer to the .debug_str section loaded into memory.  */
  bfd_byte *dwarf_str_buffer;

  /* Length of the loaded .debug_str section.  */
  unsigned long dwarf_str_size;

  /* Pointer to the .debug_ranges section loaded into memory. */
  bfd_byte *dwarf_ranges_buffer;

  /* Length of the loaded .debug_ranges section. */
  unsigned long dwarf_ranges_size;

  /* If the most recent call to bfd_find_nearest_line was given an
     address in an inlined function, preserve a pointer into the
     calling chain for subsequent calls to bfd_find_inliner_info to
     use. */
  struct funcinfo *inliner_chain;

  /* Number of loadable sections.  */
  unsigned int loadable_section_count;

  /* Array of loadable sections.  */
  struct loadable_section *loadable_sections;
};

struct arange
{
  struct arange *next;
  bfd_vma low;
  bfd_vma high;
};

/* A minimal decoding of DWARF2 compilation units.  We only decode
   what's needed to get to the line number information.  */

struct comp_unit
{
  /* Chain the previously read compilation units.  */
  struct comp_unit *next_unit;

  /* Keep the bfd convenient (for memory allocation).  */
  bfd *abfd;

  /* The lowest and highest addresses contained in this compilation
     unit as specified in the compilation unit header.  */
  struct arange arange;

  /* The DW_AT_name attribute (for error messages).  */
  char *name;

  /* The abbrev hash table.  */
  struct abbrev_info **abbrevs;

  /* Note that an error was found by comp_unit_find_nearest_line.  */
  int error;

  /* The DW_AT_comp_dir attribute.  */
  char *comp_dir;

  /* TRUE if there is a line number table associated with this comp. unit.  */
  int stmtlist;

  /* Pointer to the current comp_unit so that we can find a given entry
     by its reference.  */
  bfd_byte *info_ptr_unit;

  /* The offset into .debug_line of the line number table.  */
  unsigned long line_offset;

  /* Pointer to the first child die for the comp unit.  */
  bfd_byte *first_child_die_ptr;

  /* The end of the comp unit.  */
  bfd_byte *end_ptr;

  /* The decoded line number, NULL if not yet decoded.  */
  struct line_info_table *line_table;

  /* A list of the functions found in this comp. unit.  */
  struct funcinfo *function_table;

  /* A list of the variables found in this comp. unit.  */
  struct varinfo *variable_table;

  /* Pointer to dwarf2_debug structure.  */
  struct dwarf2_debug *stash;

  /* Address size for this unit - from unit header.  */
  unsigned char addr_size;

  /* Offset size for this unit - from unit header.  */
  unsigned char offset_size;

  /* Base address for this unit - from DW_AT_low_pc attribute of
     DW_TAG_compile_unit DIE */
  bfd_vma base_address;
};

/* This data structure holds the information of an abbrev.  */
struct abbrev_info
{
  unsigned int number;		/* Number identifying abbrev.  */
  enum dwarf_tag tag;		/* DWARF tag.  */
  int has_children;		/* Boolean.  */
  unsigned int num_attrs;	/* Number of attributes.  */
  struct attr_abbrev *attrs;	/* An array of attribute descriptions.  */
  struct abbrev_info *next;	/* Next in chain.  */
};

struct attr_abbrev
{
  enum dwarf_attribute name;
  enum dwarf_form form;
};

#ifndef ABBREV_HASH_SIZE
#define ABBREV_HASH_SIZE 121
#endif
#ifndef ATTR_ALLOC_CHUNK
#define ATTR_ALLOC_CHUNK 4
#endif

/* VERBATIM
   The following function up to the END VERBATIM mark are
   copied directly from dwarf2read.c.  */

/* Read dwarf information from a buffer.  */

static unsigned int
read_1_byte (bfd *abfd ATTRIBUTE_UNUSED, bfd_byte *buf)
{
  return bfd_get_8 (abfd, buf);
}

static int
read_1_signed_byte (bfd *abfd ATTRIBUTE_UNUSED, bfd_byte *buf)
{
  return bfd_get_signed_8 (abfd, buf);
}

static unsigned int
read_2_bytes (bfd *abfd, bfd_byte *buf)
{
  return bfd_get_16 (abfd, buf);
}

static unsigned int
read_4_bytes (bfd *abfd, bfd_byte *buf)
{
  return bfd_get_32 (abfd, buf);
}

static bfd_uint64_t
read_8_bytes (bfd *abfd, bfd_byte *buf)
{
  return bfd_get_64 (abfd, buf);
}

static bfd_byte *
read_n_bytes (bfd *abfd ATTRIBUTE_UNUSED,
	      bfd_byte *buf,
	      unsigned int size ATTRIBUTE_UNUSED)
{
  /* If the size of a host char is 8 bits, we can return a pointer
     to the buffer, otherwise we have to copy the data to a buffer
     allocated on the temporary obstack.  */
  return buf;
}

static char *
read_string (bfd *abfd ATTRIBUTE_UNUSED,
	     bfd_byte *buf,
	     unsigned int *bytes_read_ptr)
{
  /* Return a pointer to the embedded string.  */
  char *str = (char *) buf;
  if (*str == '\0')
    {
      *bytes_read_ptr = 1;
      return NULL;
    }

  *bytes_read_ptr = strlen (str) + 1;
  return str;
}

static char *
read_indirect_string (struct comp_unit* unit,
		      bfd_byte *buf,
		      unsigned int *bytes_read_ptr)
{
  bfd_uint64_t offset;
  struct dwarf2_debug *stash = unit->stash;
  char *str;

  if (unit->offset_size == 4)
    offset = read_4_bytes (unit->abfd, buf);
  else
    offset = read_8_bytes (unit->abfd, buf);
  *bytes_read_ptr = unit->offset_size;

  if (! stash->dwarf_str_buffer)
    {
      asection *msec;
      bfd *abfd = unit->abfd;
      bfd_size_type sz;

      msec = bfd_get_section_by_name (abfd, ".debug_str");
      if (! msec)
	{
	  (*_bfd_error_handler)
	    (_("Dwarf Error: Can't find .debug_str section."));
	  bfd_set_error (bfd_error_bad_value);
	  return NULL;
	}

      sz = msec->rawsize ? msec->rawsize : msec->size;
      stash->dwarf_str_size = sz;
      stash->dwarf_str_buffer = bfd_alloc (abfd, sz);
      if (! stash->dwarf_str_buffer)
	return NULL;

      if (! bfd_get_section_contents (abfd, msec, stash->dwarf_str_buffer,
				      0, sz))
	return NULL;
    }

  if (offset >= stash->dwarf_str_size)
    {
      (*_bfd_error_handler) (_("Dwarf Error: DW_FORM_strp offset (%lu) greater than or equal to .debug_str size (%lu)."),
			     (unsigned long) offset, stash->dwarf_str_size);
      bfd_set_error (bfd_error_bad_value);
      return NULL;
    }

  str = (char *) stash->dwarf_str_buffer + offset;
  if (*str == '\0')
    return NULL;
  return str;
}

/* END VERBATIM */

static bfd_uint64_t
read_address (struct comp_unit *unit, bfd_byte *buf)
{
  int signed_vma = get_elf_backend_data (unit->abfd)->sign_extend_vma;

  if (signed_vma)
    {
      switch (unit->addr_size)
	{
	case 8:
	  return bfd_get_signed_64 (unit->abfd, buf);
	case 4:
	  return bfd_get_signed_32 (unit->abfd, buf);
	case 2:
	  return bfd_get_signed_16 (unit->abfd, buf);
	default:
	  abort ();
	}
    }
  else
    {
      switch (unit->addr_size)
	{
	case 8:
	  return bfd_get_64 (unit->abfd, buf);
	case 4:
	  return bfd_get_32 (unit->abfd, buf);
	case 2:
	  return bfd_get_16 (unit->abfd, buf);
	default:
	  abort ();
	}
    }
}

/* Lookup an abbrev_info structure in the abbrev hash table.  */

static struct abbrev_info *
lookup_abbrev (unsigned int number, struct abbrev_info **abbrevs)
{
  unsigned int hash_number;
  struct abbrev_info *abbrev;

  hash_number = number % ABBREV_HASH_SIZE;
  abbrev = abbrevs[hash_number];

  while (abbrev)
    {
      if (abbrev->number == number)
	return abbrev;
      else
	abbrev = abbrev->next;
    }

  return NULL;
}

/* In DWARF version 2, the description of the debugging information is
   stored in a separate .debug_abbrev section.  Before we read any
   dies from a section we read in all abbreviations and install them
   in a hash table.  */

static struct abbrev_info**
read_abbrevs (bfd *abfd, bfd_uint64_t offset, struct dwarf2_debug *stash)
{
  struct abbrev_info **abbrevs;
  bfd_byte *abbrev_ptr;
  struct abbrev_info *cur_abbrev;
  unsigned int abbrev_number, bytes_read, abbrev_name;
  unsigned int abbrev_form, hash_number;
  bfd_size_type amt;

  if (! stash->dwarf_abbrev_buffer)
    {
      asection *msec;

      msec = bfd_get_section_by_name (abfd, ".debug_abbrev");
      if (! msec)
	{
	  (*_bfd_error_handler) (_("Dwarf Error: Can't find .debug_abbrev section."));
	  bfd_set_error (bfd_error_bad_value);
	  return 0;
	}

      stash->dwarf_abbrev_size = msec->size;
      stash->dwarf_abbrev_buffer
	= bfd_simple_get_relocated_section_contents (abfd, msec, NULL,
						     stash->syms);
      if (! stash->dwarf_abbrev_buffer)
	  return 0;
    }

  if (offset >= stash->dwarf_abbrev_size)
    {
      (*_bfd_error_handler) (_("Dwarf Error: Abbrev offset (%lu) greater than or equal to .debug_abbrev size (%lu)."),
			     (unsigned long) offset, stash->dwarf_abbrev_size);
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  amt = sizeof (struct abbrev_info*) * ABBREV_HASH_SIZE;
  abbrevs = bfd_zalloc (abfd, amt);

  abbrev_ptr = stash->dwarf_abbrev_buffer + offset;
  abbrev_number = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
  abbrev_ptr += bytes_read;

  /* Loop until we reach an abbrev number of 0.  */
  while (abbrev_number)
    {
      amt = sizeof (struct abbrev_info);
      cur_abbrev = bfd_zalloc (abfd, amt);

      /* Read in abbrev header.  */
      cur_abbrev->number = abbrev_number;
      cur_abbrev->tag = (enum dwarf_tag)
	read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      cur_abbrev->has_children = read_1_byte (abfd, abbrev_ptr);
      abbrev_ptr += 1;

      /* Now read in declarations.  */
      abbrev_name = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      abbrev_form = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;

      while (abbrev_name)
	{
	  if ((cur_abbrev->num_attrs % ATTR_ALLOC_CHUNK) == 0)
	    {
	      struct attr_abbrev *tmp;

	      amt = cur_abbrev->num_attrs + ATTR_ALLOC_CHUNK;
	      amt *= sizeof (struct attr_abbrev);
	      tmp = bfd_realloc (cur_abbrev->attrs, amt);
	      if (tmp == NULL)
		{
		  size_t i;

		  for (i = 0; i < ABBREV_HASH_SIZE; i++)
		    {
		      struct abbrev_info *abbrev = abbrevs[i];

		      while (abbrev)
			{
			  free (abbrev->attrs);
			  abbrev = abbrev->next;
			}
		    }
		  return NULL;
		}
	      cur_abbrev->attrs = tmp;
	    }

	  cur_abbrev->attrs[cur_abbrev->num_attrs].name
	    = (enum dwarf_attribute) abbrev_name;
	  cur_abbrev->attrs[cur_abbrev->num_attrs++].form
	    = (enum dwarf_form) abbrev_form;
	  abbrev_name = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
	  abbrev_ptr += bytes_read;
	  abbrev_form = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
	  abbrev_ptr += bytes_read;
	}

      hash_number = abbrev_number % ABBREV_HASH_SIZE;
      cur_abbrev->next = abbrevs[hash_number];
      abbrevs[hash_number] = cur_abbrev;

      /* Get next abbreviation.
	 Under Irix6 the abbreviations for a compilation unit are not
	 always properly terminated with an abbrev number of 0.
	 Exit loop if we encounter an abbreviation which we have
	 already read (which means we are about to read the abbreviations
	 for the next compile unit) or if the end of the abbreviation
	 table is reached.  */
      if ((unsigned int) (abbrev_ptr - stash->dwarf_abbrev_buffer)
	  >= stash->dwarf_abbrev_size)
	break;
      abbrev_number = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      if (lookup_abbrev (abbrev_number,abbrevs) != NULL)
	break;
    }

  return abbrevs;
}

/* Read an attribute value described by an attribute form.  */

static bfd_byte *
read_attribute_value (struct attribute *attr,
		      unsigned form,
		      struct comp_unit *unit,
		      bfd_byte *info_ptr)
{
  bfd *abfd = unit->abfd;
  unsigned int bytes_read;
  struct dwarf_block *blk;
  bfd_size_type amt;

  attr->form = (enum dwarf_form) form;

  switch (form)
    {
    case DW_FORM_addr:
      /* FIXME: DWARF3 draft says DW_FORM_ref_addr is offset_size.  */
    case DW_FORM_ref_addr:
      attr->u.val = read_address (unit, info_ptr);
      info_ptr += unit->addr_size;
      break;
    case DW_FORM_block2:
      amt = sizeof (struct dwarf_block);
      blk = bfd_alloc (abfd, amt);
      blk->size = read_2_bytes (abfd, info_ptr);
      info_ptr += 2;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      attr->u.blk = blk;
      break;
    case DW_FORM_block4:
      amt = sizeof (struct dwarf_block);
      blk = bfd_alloc (abfd, amt);
      blk->size = read_4_bytes (abfd, info_ptr);
      info_ptr += 4;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      attr->u.blk = blk;
      break;
    case DW_FORM_data2:
      attr->u.val = read_2_bytes (abfd, info_ptr);
      info_ptr += 2;
      break;
    case DW_FORM_data4:
      attr->u.val = read_4_bytes (abfd, info_ptr);
      info_ptr += 4;
      break;
    case DW_FORM_data8:
      attr->u.val = read_8_bytes (abfd, info_ptr);
      info_ptr += 8;
      break;
    case DW_FORM_string:
      attr->u.str = read_string (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_strp:
      attr->u.str = read_indirect_string (unit, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_block:
      amt = sizeof (struct dwarf_block);
      blk = bfd_alloc (abfd, amt);
      blk->size = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      attr->u.blk = blk;
      break;
    case DW_FORM_block1:
      amt = sizeof (struct dwarf_block);
      blk = bfd_alloc (abfd, amt);
      blk->size = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      attr->u.blk = blk;
      break;
    case DW_FORM_data1:
      attr->u.val = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      break;
    case DW_FORM_flag:
      attr->u.val = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      break;
    case DW_FORM_flag_present:
      attr->u.val = 1;
      break;
    case DW_FORM_sdata:
      attr->u.sval = read_signed_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_udata:
      attr->u.val = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_ref1:
      attr->u.val = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      break;
    case DW_FORM_ref2:
      attr->u.val = read_2_bytes (abfd, info_ptr);
      info_ptr += 2;
      break;
    case DW_FORM_ref4:
      attr->u.val = read_4_bytes (abfd, info_ptr);
      info_ptr += 4;
      break;
    case DW_FORM_ref8:
      attr->u.val = read_8_bytes (abfd, info_ptr);
      info_ptr += 8;
      break;
    case DW_FORM_ref_udata:
      attr->u.val = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_indirect:
      form = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      info_ptr = read_attribute_value (attr, form, unit, info_ptr);
      break;
    default:
      (*_bfd_error_handler) (_("Dwarf Error: Invalid or unhandled FORM value: %u."),
			     form);
      bfd_set_error (bfd_error_bad_value);
    }
  return info_ptr;
}

/* Read an attribute described by an abbreviated attribute.  */

static bfd_byte *
read_attribute (struct attribute *attr,
		struct attr_abbrev *abbrev,
		struct comp_unit *unit,
		bfd_byte *info_ptr)
{
  attr->name = abbrev->name;
  info_ptr = read_attribute_value (attr, abbrev->form, unit, info_ptr);
  return info_ptr;
}

/* Source line information table routines.  */

#define FILE_ALLOC_CHUNK 5
#define DIR_ALLOC_CHUNK 5

struct line_info
{
  struct line_info* prev_line;
  bfd_vma address;
  char *filename;
  unsigned int line;
  unsigned int column;
  int end_sequence;		/* End of (sequential) code sequence.  */
};

struct fileinfo
{
  char *name;
  unsigned int dir;
  unsigned int time;
  unsigned int size;
};

struct line_info_table
{
  bfd* abfd;
  unsigned int num_files;
  unsigned int num_dirs;
  char *comp_dir;
  char **dirs;
  struct fileinfo* files;
  struct line_info* last_line;  /* largest VMA */
  struct line_info* lcl_head;   /* local head; used in 'add_line_info' */
};

/* Remember some information about each function.  If the function is
   inlined (DW_TAG_inlined_subroutine) it may have two additional
   attributes, DW_AT_call_file and DW_AT_call_line, which specify the
   source code location where this function was inlined. */

struct funcinfo
{
  struct funcinfo *prev_func;		/* Pointer to previous function in list of all functions */
  struct funcinfo *caller_func;		/* Pointer to function one scope higher */
  char *caller_file;			/* Source location file name where caller_func inlines this func */
  int caller_line;			/* Source location line number where caller_func inlines this func */
  char *file;				/* Source location file name */
  int line;				/* Source location line number */
  int tag;
  char *name;
  struct arange arange;
  asection *sec;			/* Where the symbol is defined */
};

struct varinfo
{
  /* Pointer to previous variable in list of all variables */
  struct varinfo *prev_var;
  /* Source location file name */
  char *file;
  /* Source location line number */
  int line;
  int tag;
  char *name;
  bfd_vma addr;
  /* Where the symbol is defined */
  asection *sec;
  /* Is this a stack variable? */
  unsigned int stack: 1;
};

/* Return TRUE if NEW_LINE should sort after LINE.  */

static inline bfd_boolean
new_line_sorts_after (struct line_info *new_line, struct line_info *line)
{
  return (new_line->address > line->address
	  || (new_line->address == line->address
	      && new_line->end_sequence < line->end_sequence));
}


/* Adds a new entry to the line_info list in the line_info_table, ensuring
   that the list is sorted.  Note that the line_info list is sorted from
   highest to lowest VMA (with possible duplicates); that is,
   line_info->prev_line always accesses an equal or smaller VMA.  */

static void
add_line_info (struct line_info_table *table,
	       bfd_vma address,
	       char *filename,
	       unsigned int line,
	       unsigned int column,
	       int end_sequence)
{
  bfd_size_type amt = sizeof (struct line_info);
  struct line_info* info = bfd_alloc (table->abfd, amt);

  /* Set member data of 'info'.  */
  info->address = address;
  info->line = line;
  info->column = column;
  info->end_sequence = end_sequence;

  if (filename && filename[0])
    {
      info->filename = bfd_alloc (table->abfd, strlen (filename) + 1);
      if (info->filename)
	strcpy (info->filename, filename);
    }
  else
    info->filename = NULL;

  /* Find the correct location for 'info'.  Normally we will receive
     new line_info data 1) in order and 2) with increasing VMAs.
     However some compilers break the rules (cf. decode_line_info) and
     so we include some heuristics for quickly finding the correct
     location for 'info'. In particular, these heuristics optimize for
     the common case in which the VMA sequence that we receive is a
     list of locally sorted VMAs such as
       p...z a...j  (where a < j < p < z)

     Note: table->lcl_head is used to head an *actual* or *possible*
     sequence within the list (such as a...j) that is not directly
     headed by table->last_line

     Note: we may receive duplicate entries from 'decode_line_info'.  */

  if (!table->last_line
      || new_line_sorts_after (info, table->last_line))
    {
      /* Normal case: add 'info' to the beginning of the list */
      info->prev_line = table->last_line;
      table->last_line = info;

      /* lcl_head: initialize to head a *possible* sequence at the end.  */
      if (!table->lcl_head)
	table->lcl_head = info;
    }
  else if (!new_line_sorts_after (info, table->lcl_head)
	   && (!table->lcl_head->prev_line
	       || new_line_sorts_after (info, table->lcl_head->prev_line)))
    {
      /* Abnormal but easy: lcl_head is the head of 'info'.  */
      info->prev_line = table->lcl_head->prev_line;
      table->lcl_head->prev_line = info;
    }
  else
    {
      /* Abnormal and hard: Neither 'last_line' nor 'lcl_head' are valid
	 heads for 'info'.  Reset 'lcl_head'.  */
      struct line_info* li2 = table->last_line; /* always non-NULL */
      struct line_info* li1 = li2->prev_line;

      while (li1)
	{
	  if (!new_line_sorts_after (info, li2)
	      && new_line_sorts_after (info, li1))
	    break;

	  li2 = li1; /* always non-NULL */
	  li1 = li1->prev_line;
	}
      table->lcl_head = li2;
      info->prev_line = table->lcl_head->prev_line;
      table->lcl_head->prev_line = info;
    }
}

/* Extract a fully qualified filename from a line info table.
   The returned string has been malloc'ed and it is the caller's
   responsibility to free it.  */

static char *
concat_filename (struct line_info_table *table, unsigned int file)
{
  char *filename;

  if (file - 1 >= table->num_files)
    {
      /* FILE == 0 means unknown.  */
      if (file)
	(*_bfd_error_handler)
	  (_("Dwarf Error: mangled line number section (bad file number)."));
      return strdup ("<unknown>");
    }

  filename = table->files[file - 1].name;

  if (!IS_ABSOLUTE_PATH (filename))
    {
      char *dirname = NULL;
      char *subdirname = NULL;
      char *name;
      size_t len;

      if (table->files[file - 1].dir)
	subdirname = table->dirs[table->files[file - 1].dir - 1];

      if (!subdirname || !IS_ABSOLUTE_PATH (subdirname))
	dirname = table->comp_dir;

      if (!dirname)
	{
	  dirname = subdirname;
	  subdirname = NULL;
	}

      if (!dirname)
	return strdup (filename);

      len = strlen (dirname) + strlen (filename) + 2;

      if (subdirname)
	{
	  len += strlen (subdirname) + 1;
	  name = bfd_malloc (len);
	  if (name)
	    sprintf (name, "%s/%s/%s", dirname, subdirname, filename);
	}
      else
	{
	  name = bfd_malloc (len);
	  if (name)
	    sprintf (name, "%s/%s", dirname, filename);
	}

      return name;
    }

  return strdup (filename);
}

static void
arange_add (bfd *abfd, struct arange *first_arange, bfd_vma low_pc, bfd_vma high_pc)
{
  struct arange *arange;

  /* If the first arange is empty, use it. */
  if (first_arange->high == 0)
    {
      first_arange->low = low_pc;
      first_arange->high = high_pc;
      return;
    }

  /* Next see if we can cheaply extend an existing range.  */
  arange = first_arange;
  do
    {
      if (low_pc == arange->high)
	{
	  arange->high = high_pc;
	  return;
	}
      if (high_pc == arange->low)
	{
	  arange->low = low_pc;
	  return;
	}
      arange = arange->next;
    }
  while (arange);

  /* Need to allocate a new arange and insert it into the arange list.
     Order isn't significant, so just insert after the first arange. */
  arange = bfd_zalloc (abfd, sizeof (*arange));
  arange->low = low_pc;
  arange->high = high_pc;
  arange->next = first_arange->next;
  first_arange->next = arange;
}

/* Decode the line number information for UNIT.  */

static struct line_info_table*
decode_line_info (struct comp_unit *unit, struct dwarf2_debug *stash)
{
  bfd *abfd = unit->abfd;
  struct line_info_table* table;
  bfd_byte *line_ptr;
  bfd_byte *line_end;
  struct line_head lh;
  unsigned int i, bytes_read, offset_size;
  char *cur_file, *cur_dir;
  unsigned char op_code, extended_op, adj_opcode;
  bfd_size_type amt;

  if (! stash->dwarf_line_buffer)
    {
      asection *msec;

      msec = bfd_get_section_by_name (abfd, ".debug_line");
      if (! msec)
	{
	  (*_bfd_error_handler) (_("Dwarf Error: Can't find .debug_line section."));
	  bfd_set_error (bfd_error_bad_value);
	  return 0;
	}

      stash->dwarf_line_size = msec->size;
      stash->dwarf_line_buffer
	= bfd_simple_get_relocated_section_contents (abfd, msec, NULL,
						     stash->syms);
      if (! stash->dwarf_line_buffer)
	return 0;
    }

  /* It is possible to get a bad value for the line_offset.  Validate
     it here so that we won't get a segfault below.  */
  if (unit->line_offset >= stash->dwarf_line_size)
    {
      (*_bfd_error_handler) (_("Dwarf Error: Line offset (%lu) greater than or equal to .debug_line size (%lu)."),
			     unit->line_offset, stash->dwarf_line_size);
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  amt = sizeof (struct line_info_table);
  table = bfd_alloc (abfd, amt);
  table->abfd = abfd;
  table->comp_dir = unit->comp_dir;

  table->num_files = 0;
  table->files = NULL;

  table->num_dirs = 0;
  table->dirs = NULL;

  table->files = NULL;
  table->last_line = NULL;
  table->lcl_head = NULL;

  line_ptr = stash->dwarf_line_buffer + unit->line_offset;

  /* Read in the prologue.  */
  lh.total_length = read_4_bytes (abfd, line_ptr);
  line_ptr += 4;
  offset_size = 4;
  if (lh.total_length == 0xffffffff)
    {
      lh.total_length = read_8_bytes (abfd, line_ptr);
      line_ptr += 8;
      offset_size = 8;
    }
  else if (lh.total_length == 0 && unit->addr_size == 8)
    {
      /* Handle (non-standard) 64-bit DWARF2 formats.  */
      lh.total_length = read_4_bytes (abfd, line_ptr);
      line_ptr += 4;
      offset_size = 8;
    }
  line_end = line_ptr + lh.total_length;
  lh.version = read_2_bytes (abfd, line_ptr);
  line_ptr += 2;
  if (offset_size == 4)
    lh.prologue_length = read_4_bytes (abfd, line_ptr);
  else
    lh.prologue_length = read_8_bytes (abfd, line_ptr);
  line_ptr += offset_size;
  lh.minimum_instruction_length = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh.default_is_stmt = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh.line_base = read_1_signed_byte (abfd, line_ptr);
  line_ptr += 1;
  lh.line_range = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh.opcode_base = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  amt = lh.opcode_base * sizeof (unsigned char);
  lh.standard_opcode_lengths = bfd_alloc (abfd, amt);

  lh.standard_opcode_lengths[0] = 1;

  for (i = 1; i < lh.opcode_base; ++i)
    {
      lh.standard_opcode_lengths[i] = read_1_byte (abfd, line_ptr);
      line_ptr += 1;
    }

  /* Read directory table.  */
  while ((cur_dir = read_string (abfd, line_ptr, &bytes_read)) != NULL)
    {
      line_ptr += bytes_read;

      if ((table->num_dirs % DIR_ALLOC_CHUNK) == 0)
	{
	  char **tmp;

	  amt = table->num_dirs + DIR_ALLOC_CHUNK;
	  amt *= sizeof (char *);

	  tmp = bfd_realloc (table->dirs, amt);
	  if (tmp == NULL)
	    {
	      free (table->dirs);
	      return NULL;
	    }
	  table->dirs = tmp;
	}

      table->dirs[table->num_dirs++] = cur_dir;
    }

  line_ptr += bytes_read;

  /* Read file name table.  */
  while ((cur_file = read_string (abfd, line_ptr, &bytes_read)) != NULL)
    {
      line_ptr += bytes_read;

      if ((table->num_files % FILE_ALLOC_CHUNK) == 0)
	{
	  struct fileinfo *tmp;

	  amt = table->num_files + FILE_ALLOC_CHUNK;
	  amt *= sizeof (struct fileinfo);

	  tmp = bfd_realloc (table->files, amt);
	  if (tmp == NULL)
	    {
	      free (table->files);
	      free (table->dirs);
	      return NULL;
	    }
	  table->files = tmp;
	}

      table->files[table->num_files].name = cur_file;
      table->files[table->num_files].dir =
	read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
      line_ptr += bytes_read;
      table->files[table->num_files].time =
	read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
      line_ptr += bytes_read;
      table->files[table->num_files].size =
	read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
      line_ptr += bytes_read;
      table->num_files++;
    }

  line_ptr += bytes_read;

  /* Read the statement sequences until there's nothing left.  */
  while (line_ptr < line_end)
    {
      /* State machine registers.  */
      bfd_vma address = 0;
      char * filename = table->num_files ? concat_filename (table, 1) : NULL;
      unsigned int line = 1;
      unsigned int column = 0;
      int is_stmt = lh.default_is_stmt;
      int end_sequence = 0;
      /* eraxxon@alumni.rice.edu: Against the DWARF2 specs, some
	 compilers generate address sequences that are wildly out of
	 order using DW_LNE_set_address (e.g. Intel C++ 6.0 compiler
	 for ia64-Linux).  Thus, to determine the low and high
	 address, we must compare on every DW_LNS_copy, etc.  */
      bfd_vma low_pc  = (bfd_vma) -1;
      bfd_vma high_pc = 0;

      /* Decode the table.  */
      while (! end_sequence)
	{
	  op_code = read_1_byte (abfd, line_ptr);
	  line_ptr += 1;

	  if (op_code >= lh.opcode_base)
	    {
	      /* Special operand.  */
	      adj_opcode = op_code - lh.opcode_base;
	      address += (adj_opcode / lh.line_range)
		* lh.minimum_instruction_length;
	      line += lh.line_base + (adj_opcode % lh.line_range);
	      /* Append row to matrix using current values.  */
	      add_line_info (table, address, filename, line, column, 0);
	      if (address < low_pc)
		low_pc = address;
	      if (address > high_pc)
		high_pc = address;
	    }
	  else switch (op_code)
	    {
	    case DW_LNS_extended_op:
	      /* Ignore length.  */
	      line_ptr += 1;
	      extended_op = read_1_byte (abfd, line_ptr);
	      line_ptr += 1;

	      switch (extended_op)
		{
		case DW_LNE_end_sequence:
		  end_sequence = 1;
		  add_line_info (table, address, filename, line, column,
				 end_sequence);
		  if (address < low_pc)
		    low_pc = address;
		  if (address > high_pc)
		    high_pc = address;
		  arange_add (unit->abfd, &unit->arange, low_pc, high_pc);
		  break;
		case DW_LNE_set_address:
		  address = read_address (unit, line_ptr);
		  line_ptr += unit->addr_size;
		  break;
		case DW_LNE_define_file:
		  cur_file = read_string (abfd, line_ptr, &bytes_read);
		  line_ptr += bytes_read;
		  if ((table->num_files % FILE_ALLOC_CHUNK) == 0)
		    {
		      struct fileinfo *tmp;

		      amt = table->num_files + FILE_ALLOC_CHUNK;
		      amt *= sizeof (struct fileinfo);
		      tmp = bfd_realloc (table->files, amt);
		      if (tmp == NULL)
			{
			  free (table->files);
			  free (table->dirs);
			  free (filename);
			  return NULL;
			}
		      table->files = tmp;
		    }
		  table->files[table->num_files].name = cur_file;
		  table->files[table->num_files].dir =
		    read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
		  line_ptr += bytes_read;
		  table->files[table->num_files].time =
		    read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
		  line_ptr += bytes_read;
		  table->files[table->num_files].size =
		    read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
		  line_ptr += bytes_read;
		  table->num_files++;
		  break;
		default:
		  (*_bfd_error_handler) (_("Dwarf Error: mangled line number section."));
		  bfd_set_error (bfd_error_bad_value);
		  free (filename);
		  free (table->files);
		  free (table->dirs);
		  return NULL;
		}
	      break;
	    case DW_LNS_copy:
	      add_line_info (table, address, filename, line, column, 0);
	      if (address < low_pc)
		low_pc = address;
	      if (address > high_pc)
		high_pc = address;
	      break;
	    case DW_LNS_advance_pc:
	      address += lh.minimum_instruction_length
		* read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
	      line_ptr += bytes_read;
	      break;
	    case DW_LNS_advance_line:
	      line += read_signed_leb128 (abfd, line_ptr, &bytes_read);
	      line_ptr += bytes_read;
	      break;
	    case DW_LNS_set_file:
	      {
		unsigned int file;

		/* The file and directory tables are 0
		   based, the references are 1 based.  */
		file = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
		line_ptr += bytes_read;
		if (filename)
		  free (filename);
		filename = concat_filename (table, file);
		break;
	      }
	    case DW_LNS_set_column:
	      column = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
	      line_ptr += bytes_read;
	      break;
	    case DW_LNS_negate_stmt:
	      is_stmt = (!is_stmt);
	      break;
	    case DW_LNS_set_basic_block:
	      break;
	    case DW_LNS_const_add_pc:
	      address += lh.minimum_instruction_length
		      * ((255 - lh.opcode_base) / lh.line_range);
	      break;
	    case DW_LNS_fixed_advance_pc:
	      address += read_2_bytes (abfd, line_ptr);
	      line_ptr += 2;
	      break;
	    default:
	      {
		int i;

		/* Unknown standard opcode, ignore it.  */
		for (i = 0; i < lh.standard_opcode_lengths[op_code]; i++)
		  {
		    (void) read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
		    line_ptr += bytes_read;
		  }
	      }
	    }
	}

      if (filename)
	free (filename);
    }

  return table;
}

/* If ADDR is within TABLE set the output parameters and return TRUE,
   otherwise return FALSE.  The output parameters, FILENAME_PTR and
   LINENUMBER_PTR, are pointers to the objects to be filled in.  */

static bfd_boolean
lookup_address_in_line_info_table (struct line_info_table *table,
				   bfd_vma addr,
				   struct funcinfo *function,
				   const char **filename_ptr,
				   unsigned int *linenumber_ptr)
{
  /* Note: table->last_line should be a descendingly sorted list. */
  struct line_info* next_line = table->last_line;
  struct line_info* each_line = NULL;
  *filename_ptr = NULL;

  if (!next_line)
    return FALSE;

  each_line = next_line->prev_line;

  /* Check for large addresses */
  if (addr > next_line->address)
    each_line = NULL; /* ensure we skip over the normal case */

  /* Normal case: search the list; save  */
  while (each_line && next_line)
    {
      /* If we have an address match, save this info.  This allows us
	 to return as good as results as possible for strange debugging
	 info.  */
      bfd_boolean addr_match = FALSE;
      if (each_line->address <= addr && addr < next_line->address)
	{
	  addr_match = TRUE;

	  /* If this line appears to span functions, and addr is in the
	     later function, return the first line of that function instead
	     of the last line of the earlier one.  This check is for GCC
	     2.95, which emits the first line number for a function late.  */

	  if (function != NULL)
	    {
	      bfd_vma lowest_pc;
	      struct arange *arange;

	      /* Find the lowest address in the function's range list */
	      lowest_pc = function->arange.low;
	      for (arange = &function->arange;
		   arange;
		   arange = arange->next)
		{
		  if (function->arange.low < lowest_pc)
		    lowest_pc = function->arange.low;
		}
	      /* Check for spanning function and set outgoing line info */
	      if (addr >= lowest_pc
		  && each_line->address < lowest_pc
		  && next_line->address > lowest_pc)
		{
		  *filename_ptr = next_line->filename;
		  *linenumber_ptr = next_line->line;
		}
	      else
		{
		  *filename_ptr = each_line->filename;
		  *linenumber_ptr = each_line->line;
		}
	    }
	  else
	    {
	      *filename_ptr = each_line->filename;
	      *linenumber_ptr = each_line->line;
	    }
	}

      if (addr_match && !each_line->end_sequence)
	return TRUE; /* we have definitely found what we want */

      next_line = each_line;
      each_line = each_line->prev_line;
    }

  /* At this point each_line is NULL but next_line is not.  If we found
     a candidate end-of-sequence point in the loop above, we can return
     that (compatibility with a bug in the Intel compiler); otherwise,
     assuming that we found the containing function for this address in
     this compilation unit, return the first line we have a number for
     (compatibility with GCC 2.95).  */
  if (*filename_ptr == NULL && function != NULL)
    {
      *filename_ptr = next_line->filename;
      *linenumber_ptr = next_line->line;
      return TRUE;
    }

  return FALSE;
}

/* Read in the .debug_ranges section for future reference */

static bfd_boolean
read_debug_ranges (struct comp_unit *unit)
{
  struct dwarf2_debug *stash = unit->stash;
  if (! stash->dwarf_ranges_buffer)
    {
      bfd *abfd = unit->abfd;
      asection *msec;

      msec = bfd_get_section_by_name (abfd, ".debug_ranges");
      if (! msec)
	{
	  (*_bfd_error_handler) (_("Dwarf Error: Can't find .debug_ranges section."));
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}

      stash->dwarf_ranges_size = msec->size;
      stash->dwarf_ranges_buffer
	= bfd_simple_get_relocated_section_contents (abfd, msec, NULL,
						     stash->syms);
      if (! stash->dwarf_ranges_buffer)
	return FALSE;
    }
  return TRUE;
}

/* Function table functions.  */

/* If ADDR is within TABLE, set FUNCTIONNAME_PTR, and return TRUE.
   Note that we need to find the function that has the smallest
   range that contains ADDR, to handle inlined functions without
   depending upon them being ordered in TABLE by increasing range. */

static bfd_boolean
lookup_address_in_function_table (struct comp_unit *unit,
				  bfd_vma addr,
				  struct funcinfo **function_ptr,
				  const char **functionname_ptr)
{
  struct funcinfo* each_func;
  struct funcinfo* best_fit = NULL;
  struct arange *arange;

  for (each_func = unit->function_table;
       each_func;
       each_func = each_func->prev_func)
    {
      for (arange = &each_func->arange;
	   arange;
	   arange = arange->next)
	{
	  if (addr >= arange->low && addr < arange->high)
	    {
	      if (!best_fit ||
		  ((arange->high - arange->low) < (best_fit->arange.high - best_fit->arange.low)))
		best_fit = each_func;
	    }
	}
    }

  if (best_fit)
    {
      *functionname_ptr = best_fit->name;
      *function_ptr = best_fit;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

/* If SYM at ADDR is within function table of UNIT, set FILENAME_PTR
   and LINENUMBER_PTR, and return TRUE.  */

static bfd_boolean
lookup_symbol_in_function_table (struct comp_unit *unit,
				 asymbol *sym,
				 bfd_vma addr,
				 const char **filename_ptr,
				 unsigned int *linenumber_ptr)
{
  struct funcinfo* each_func;
  struct funcinfo* best_fit = NULL;
  struct arange *arange;
  const char *name = bfd_asymbol_name (sym);
  asection *sec = bfd_get_section (sym);

  for (each_func = unit->function_table;
       each_func;
       each_func = each_func->prev_func)
    {
      for (arange = &each_func->arange;
	   arange;
	   arange = arange->next)
	{
	  if ((!each_func->sec || each_func->sec == sec)
	      && addr >= arange->low
	      && addr < arange->high
	      && each_func->name
	      && strcmp (name, each_func->name) == 0
	      && (!best_fit
		  || ((arange->high - arange->low)
		      < (best_fit->arange.high - best_fit->arange.low))))
	    best_fit = each_func;
	}
    }

  if (best_fit)
    {
      best_fit->sec = sec;
      *filename_ptr = best_fit->file;
      *linenumber_ptr = best_fit->line;
      return TRUE;
    }
  else
    return FALSE;
}

/* Variable table functions.  */

/* If SYM is within variable table of UNIT, set FILENAME_PTR and
   LINENUMBER_PTR, and return TRUE.  */

static bfd_boolean
lookup_symbol_in_variable_table (struct comp_unit *unit,
				 asymbol *sym,
				 bfd_vma addr,
				 const char **filename_ptr,
				 unsigned int *linenumber_ptr)
{
  const char *name = bfd_asymbol_name (sym);
  asection *sec = bfd_get_section (sym);
  struct varinfo* each;

  for (each = unit->variable_table; each; each = each->prev_var)
    if (each->stack == 0
	&& each->file != NULL
	&& each->name != NULL
	&& each->addr == addr
	&& (!each->sec || each->sec == sec)
	&& strcmp (name, each->name) == 0)
      break;

  if (each)
    {
      each->sec = sec;
      *filename_ptr = each->file;
      *linenumber_ptr = each->line;
      return TRUE;
    }
  else
    return FALSE;
}

static char *
find_abstract_instance_name (struct comp_unit *unit, bfd_uint64_t die_ref)
{
  bfd *abfd = unit->abfd;
  bfd_byte *info_ptr;
  unsigned int abbrev_number, bytes_read, i;
  struct abbrev_info *abbrev;
  struct attribute attr;
  char *name = 0;

  info_ptr = unit->info_ptr_unit + die_ref;
  abbrev_number = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
  info_ptr += bytes_read;

  if (abbrev_number)
    {
      abbrev = lookup_abbrev (abbrev_number, unit->abbrevs);
      if (! abbrev)
	{
	  (*_bfd_error_handler) (_("Dwarf Error: Could not find abbrev number %u."),
				 abbrev_number);
	  bfd_set_error (bfd_error_bad_value);
	}
      else
	{
	  for (i = 0; i < abbrev->num_attrs; ++i)
	    {
	      info_ptr = read_attribute (&attr, &abbrev->attrs[i], unit, info_ptr);
	      switch (attr.name)
		{
		case DW_AT_name:
		  /* Prefer DW_AT_MIPS_linkage_name over DW_AT_name.  */
		  if (name == NULL)
		    name = attr.u.str;
		  break;
		case DW_AT_specification:
		  name = find_abstract_instance_name (unit, attr.u.val);
		  break;
		case DW_AT_MIPS_linkage_name:
		  name = attr.u.str;
		  break;
		default:
		  break;
		}
	    }
	}
    }
  return (name);
}

static void
read_rangelist (struct comp_unit *unit, struct arange *arange, bfd_uint64_t offset)
{
  bfd_byte *ranges_ptr;
  bfd_vma base_address = unit->base_address;

  if (! unit->stash->dwarf_ranges_buffer)
    {
      if (! read_debug_ranges (unit))
	return;
    }
  ranges_ptr = unit->stash->dwarf_ranges_buffer + offset;

  for (;;)
    {
      bfd_vma low_pc;
      bfd_vma high_pc;

      if (unit->addr_size == 4)
	{
	  low_pc = read_4_bytes (unit->abfd, ranges_ptr);
	  ranges_ptr += 4;
	  high_pc = read_4_bytes (unit->abfd, ranges_ptr);
	  ranges_ptr += 4;
	}
      else
	{
	  low_pc = read_8_bytes (unit->abfd, ranges_ptr);
	  ranges_ptr += 8;
	  high_pc = read_8_bytes (unit->abfd, ranges_ptr);
	  ranges_ptr += 8;
	}
      if (low_pc == 0 && high_pc == 0)
	break;
      if (low_pc == -1UL && high_pc != -1UL)
	base_address = high_pc;
      else
	arange_add (unit->abfd, arange, base_address + low_pc, base_address + high_pc);
    }
}

/* DWARF2 Compilation unit functions.  */

/* Scan over each die in a comp. unit looking for functions to add
   to the function table and variables to the variable table.  */

static bfd_boolean
scan_unit_for_symbols (struct comp_unit *unit)
{
  bfd *abfd = unit->abfd;
  bfd_byte *info_ptr = unit->first_child_die_ptr;
  int nesting_level = 1;
  struct funcinfo **nested_funcs;
  int nested_funcs_size;

  /* Maintain a stack of in-scope functions and inlined functions, which we
     can use to set the caller_func field.  */
  nested_funcs_size = 32;
  nested_funcs = bfd_malloc (nested_funcs_size * sizeof (struct funcinfo *));
  if (nested_funcs == NULL)
    return FALSE;
  nested_funcs[nesting_level] = 0;

  while (nesting_level)
    {
      unsigned int abbrev_number, bytes_read, i;
      struct abbrev_info *abbrev;
      struct attribute attr;
      struct funcinfo *func;
      struct varinfo *var;
      bfd_vma low_pc = 0;
      bfd_vma high_pc = 0;

      abbrev_number = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;

      if (! abbrev_number)
	{
	  nesting_level--;
	  continue;
	}

      abbrev = lookup_abbrev (abbrev_number,unit->abbrevs);
      if (! abbrev)
	{
	  (*_bfd_error_handler) (_("Dwarf Error: Could not find abbrev number %u."),
			     abbrev_number);
	  bfd_set_error (bfd_error_bad_value);
	  free (nested_funcs);
	  return FALSE;
	}

      var = NULL;
      if (abbrev->tag == DW_TAG_subprogram
	  || abbrev->tag == DW_TAG_entry_point
	  || abbrev->tag == DW_TAG_inlined_subroutine)
	{
	  bfd_size_type amt = sizeof (struct funcinfo);
	  func = bfd_zalloc (abfd, amt);
	  func->tag = abbrev->tag;
	  func->prev_func = unit->function_table;
	  unit->function_table = func;

	  if (func->tag == DW_TAG_inlined_subroutine)
	    for (i = nesting_level - 1; i >= 1; i--)
	      if (nested_funcs[i])
		{
		  func->caller_func = nested_funcs[i];
		  break;
		}
	  nested_funcs[nesting_level] = func;
	}
      else
	{
	  func = NULL;
	  if (abbrev->tag == DW_TAG_variable)
	    {
	      bfd_size_type amt = sizeof (struct varinfo);
	      var = bfd_zalloc (abfd, amt);
	      var->tag = abbrev->tag;
	      var->stack = 1;
	      var->prev_var = unit->variable_table;
	      unit->variable_table = var;
	    }

	  /* No inline function in scope at this nesting level.  */
	  nested_funcs[nesting_level] = 0;
	}

      for (i = 0; i < abbrev->num_attrs; ++i)
	{
	  info_ptr = read_attribute (&attr, &abbrev->attrs[i], unit, info_ptr);

	  if (func)
	    {
	      switch (attr.name)
		{
		case DW_AT_call_file:
		  func->caller_file = concat_filename (unit->line_table, attr.u.val);
		  break;

		case DW_AT_call_line:
		  func->caller_line = attr.u.val;
		  break;

		case DW_AT_abstract_origin:
		  func->name = find_abstract_instance_name (unit, attr.u.val);
		  break;

		case DW_AT_name:
		  /* Prefer DW_AT_MIPS_linkage_name over DW_AT_name.  */
		  if (func->name == NULL)
		    func->name = attr.u.str;
		  break;

		case DW_AT_MIPS_linkage_name:
		  func->name = attr.u.str;
		  break;

		case DW_AT_low_pc:
		  low_pc = attr.u.val;
		  break;

		case DW_AT_high_pc:
		  high_pc = attr.u.val;
		  break;

		case DW_AT_ranges:
		  read_rangelist (unit, &func->arange, attr.u.val);
		  break;

		case DW_AT_decl_file:
		  func->file = concat_filename (unit->line_table,
						attr.u.val);
		  break;

		case DW_AT_decl_line:
		  func->line = attr.u.val;
		  break;

		default:
		  break;
		}
	    }
	  else if (var)
	    {
	      switch (attr.name)
		{
		case DW_AT_name:
		  var->name = attr.u.str;
		  break;

		case DW_AT_decl_file:
		  var->file = concat_filename (unit->line_table,
					       attr.u.val);
		  break;

		case DW_AT_decl_line:
		  var->line = attr.u.val;
		  break;

		case DW_AT_external:
		  if (attr.u.val != 0)
		    var->stack = 0;
		  break;

		case DW_AT_location:
		  switch (attr.form)
		    {
		    case DW_FORM_block:
		    case DW_FORM_block1:
		    case DW_FORM_block2:
		    case DW_FORM_block4:
		      if (*attr.u.blk->data == DW_OP_addr)
			{
			  var->stack = 0;

			  /* Verify that DW_OP_addr is the only opcode in the
			     location, in which case the block size will be 1
			     plus the address size.  */
			  /* ??? For TLS variables, gcc can emit
			     DW_OP_addr <addr> DW_OP_GNU_push_tls_address
			     which we don't handle here yet.  */
			  if (attr.u.blk->size == unit->addr_size + 1U)
			    var->addr = bfd_get (unit->addr_size * 8,
						 unit->abfd,
						 attr.u.blk->data + 1);
			}
		      break;

		    default:
		      break;
		    }
		  break;

		default:
		  break;
		}
	    }
	}

      if (func && high_pc != 0)
	{
	  arange_add (unit->abfd, &func->arange, low_pc, high_pc);
	}

      if (abbrev->has_children)
	{
	  nesting_level++;

	  if (nesting_level >= nested_funcs_size)
	    {
	      struct funcinfo **tmp;

	      nested_funcs_size *= 2;
	      tmp = bfd_realloc (nested_funcs,
				 (nested_funcs_size
				  * sizeof (struct funcinfo *)));
	      if (tmp == NULL)
		{
		  free (nested_funcs);
		  return FALSE;
		}
	      nested_funcs = tmp;
	    }
	  nested_funcs[nesting_level] = 0;
	}
    }

  free (nested_funcs);
  return TRUE;
}

/* Parse a DWARF2 compilation unit starting at INFO_PTR.  This
   includes the compilation unit header that proceeds the DIE's, but
   does not include the length field that precedes each compilation
   unit header.  END_PTR points one past the end of this comp unit.
   OFFSET_SIZE is the size of DWARF2 offsets (either 4 or 8 bytes).

   This routine does not read the whole compilation unit; only enough
   to get to the line number information for the compilation unit.  */

static struct comp_unit *
parse_comp_unit (struct dwarf2_debug *stash,
		 bfd_vma unit_length,
		 bfd_byte *info_ptr_unit,
		 unsigned int offset_size)
{
  struct comp_unit* unit;
  unsigned int version;
  bfd_uint64_t abbrev_offset = 0;
  unsigned int addr_size;
  struct abbrev_info** abbrevs;
  unsigned int abbrev_number, bytes_read, i;
  struct abbrev_info *abbrev;
  struct attribute attr;
  bfd_byte *info_ptr = stash->info_ptr;
  bfd_byte *end_ptr = info_ptr + unit_length;
  bfd_size_type amt;
  bfd_vma low_pc = 0;
  bfd_vma high_pc = 0;
  bfd *abfd = stash->bfd;

  version = read_2_bytes (abfd, info_ptr);
  info_ptr += 2;
  BFD_ASSERT (offset_size == 4 || offset_size == 8);
  if (offset_size == 4)
    abbrev_offset = read_4_bytes (abfd, info_ptr);
  else
    abbrev_offset = read_8_bytes (abfd, info_ptr);
  info_ptr += offset_size;
  addr_size = read_1_byte (abfd, info_ptr);
  info_ptr += 1;

  if (version != 2)
    {
      (*_bfd_error_handler) (_("Dwarf Error: found dwarf version '%u', this reader only handles version 2 information."), version);
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  if (addr_size > sizeof (bfd_vma))
    {
      (*_bfd_error_handler) (_("Dwarf Error: found address size '%u', this reader can not handle sizes greater than '%u'."),
			 addr_size,
			 (unsigned int) sizeof (bfd_vma));
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  if (addr_size != 2 && addr_size != 4 && addr_size != 8)
    {
      (*_bfd_error_handler) ("Dwarf Error: found address size '%u', this reader can only handle address sizes '2', '4' and '8'.", addr_size);
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  /* Read the abbrevs for this compilation unit into a table.  */
  abbrevs = read_abbrevs (abfd, abbrev_offset, stash);
  if (! abbrevs)
      return 0;

  abbrev_number = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
  info_ptr += bytes_read;
  if (! abbrev_number)
    {
      (*_bfd_error_handler) (_("Dwarf Error: Bad abbrev number: %u."),
			 abbrev_number);
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  abbrev = lookup_abbrev (abbrev_number, abbrevs);
  if (! abbrev)
    {
      (*_bfd_error_handler) (_("Dwarf Error: Could not find abbrev number %u."),
			 abbrev_number);
      bfd_set_error (bfd_error_bad_value);
      return 0;
    }

  amt = sizeof (struct comp_unit);
  unit = bfd_zalloc (abfd, amt);
  unit->abfd = abfd;
  unit->addr_size = addr_size;
  unit->offset_size = offset_size;
  unit->abbrevs = abbrevs;
  unit->end_ptr = end_ptr;
  unit->stash = stash;
  unit->info_ptr_unit = info_ptr_unit;

  for (i = 0; i < abbrev->num_attrs; ++i)
    {
      info_ptr = read_attribute (&attr, &abbrev->attrs[i], unit, info_ptr);

      /* Store the data if it is of an attribute we want to keep in a
	 partial symbol table.  */
      switch (attr.name)
	{
	case DW_AT_stmt_list:
	  unit->stmtlist = 1;
	  unit->line_offset = attr.u.val;
	  break;

	case DW_AT_name:
	  unit->name = attr.u.str;
	  break;

	case DW_AT_low_pc:
	  low_pc = attr.u.val;
	  /* If the compilation unit DIE has a DW_AT_low_pc attribute,
	     this is the base address to use when reading location
	     lists or range lists. */
	  unit->base_address = low_pc;
	  break;

	case DW_AT_high_pc:
	  high_pc = attr.u.val;
	  break;

	case DW_AT_ranges:
	  read_rangelist (unit, &unit->arange, attr.u.val);
	  break;

	case DW_AT_comp_dir:
	  {
	    char *comp_dir = attr.u.str;
	    if (comp_dir)
	      {
		/* Irix 6.2 native cc prepends <machine>.: to the compilation
		   directory, get rid of it.  */
		char *cp = strchr (comp_dir, ':');

		if (cp && cp != comp_dir && cp[-1] == '.' && cp[1] == '/')
		  comp_dir = cp + 1;
	      }
	    unit->comp_dir = comp_dir;
	    break;
	  }

	default:
	  break;
	}
    }
  if (high_pc != 0)
    {
      arange_add (unit->abfd, &unit->arange, low_pc, high_pc);
    }

  unit->first_child_die_ptr = info_ptr;
  return unit;
}

/* Return TRUE if UNIT may contain the address given by ADDR.  When
   there are functions written entirely with inline asm statements, the
   range info in the compilation unit header may not be correct.  We
   need to consult the line info table to see if a compilation unit
   really contains the given address.  */

static bfd_boolean
comp_unit_contains_address (struct comp_unit *unit, bfd_vma addr)
{
  struct arange *arange;

  if (unit->error)
    return FALSE;

  arange = &unit->arange;
  do
    {
      if (addr >= arange->low && addr < arange->high)
	return TRUE;
      arange = arange->next;
    }
  while (arange);

  return FALSE;
}

/* If UNIT contains ADDR, set the output parameters to the values for
   the line containing ADDR.  The output parameters, FILENAME_PTR,
   FUNCTIONNAME_PTR, and LINENUMBER_PTR, are pointers to the objects
   to be filled in.

   Return TRUE if UNIT contains ADDR, and no errors were encountered;
   FALSE otherwise.  */

static bfd_boolean
comp_unit_find_nearest_line (struct comp_unit *unit,
			     bfd_vma addr,
			     const char **filename_ptr,
			     const char **functionname_ptr,
			     unsigned int *linenumber_ptr,
			     struct dwarf2_debug *stash)
{
  bfd_boolean line_p;
  bfd_boolean func_p;
  struct funcinfo *function;

  if (unit->error)
    return FALSE;

  if (! unit->line_table)
    {
      if (! unit->stmtlist)
	{
	  unit->error = 1;
	  return FALSE;
	}

      unit->line_table = decode_line_info (unit, stash);

      if (! unit->line_table)
	{
	  unit->error = 1;
	  return FALSE;
	}

      if (unit->first_child_die_ptr < unit->end_ptr
	  && ! scan_unit_for_symbols (unit))
	{
	  unit->error = 1;
	  return FALSE;
	}
    }

  function = NULL;
  func_p = lookup_address_in_function_table (unit, addr,
					     &function, functionname_ptr);
  if (func_p && (function->tag == DW_TAG_inlined_subroutine))
    stash->inliner_chain = function;
  line_p = lookup_address_in_line_info_table (unit->line_table, addr,
					      function, filename_ptr,
					      linenumber_ptr);
  return line_p || func_p;
}

/* If UNIT contains SYM at ADDR, set the output parameters to the
   values for the line containing SYM.  The output parameters,
   FILENAME_PTR, and LINENUMBER_PTR, are pointers to the objects to be
   filled in.

   Return TRUE if UNIT contains SYM, and no errors were encountered;
   FALSE otherwise.  */

static bfd_boolean
comp_unit_find_line (struct comp_unit *unit,
		     asymbol *sym,
		     bfd_vma addr,
		     const char **filename_ptr,
		     unsigned int *linenumber_ptr,
		     struct dwarf2_debug *stash)
{
  if (unit->error)
    return FALSE;

  if (! unit->line_table)
    {
      if (! unit->stmtlist)
	{
	  unit->error = 1;
	  return FALSE;
	}

      unit->line_table = decode_line_info (unit, stash);

      if (! unit->line_table)
	{
	  unit->error = 1;
	  return FALSE;
	}

      if (unit->first_child_die_ptr < unit->end_ptr
	  && ! scan_unit_for_symbols (unit))
	{
	  unit->error = 1;
	  return FALSE;
	}
    }

  if (sym->flags & BSF_FUNCTION)
    return lookup_symbol_in_function_table (unit, sym, addr,
					    filename_ptr,
					    linenumber_ptr);
  else
    return lookup_symbol_in_variable_table (unit, sym, addr,
					    filename_ptr,
					    linenumber_ptr);
}

/* Locate a section in a BFD containing debugging info.  The search starts
   from the section after AFTER_SEC, or from the first section in the BFD if
   AFTER_SEC is NULL.  The search works by examining the names of the
   sections.  There are two permissiable names.  The first is .debug_info.
   This is the standard DWARF2 name.  The second is a prefix .gnu.linkonce.wi.
   This is a variation on the .debug_info section which has a checksum
   describing the contents appended onto the name.  This allows the linker to
   identify and discard duplicate debugging sections for different
   compilation units.  */
#define DWARF2_DEBUG_INFO ".debug_info"
#define GNU_LINKONCE_INFO ".gnu.linkonce.wi."

static asection *
find_debug_info (bfd *abfd, asection *after_sec)
{
  asection * msec;

  msec = after_sec != NULL ? after_sec->next : abfd->sections;

  while (msec)
    {
      if (strcmp (msec->name, DWARF2_DEBUG_INFO) == 0)
	return msec;

      if (CONST_STRNEQ (msec->name, GNU_LINKONCE_INFO))
	return msec;

      msec = msec->next;
    }

  return NULL;
}

/* Unset vmas for loadable sections in STASH.  */

static void
unset_sections (struct dwarf2_debug *stash)
{
  unsigned int i;
  struct loadable_section *p;

  i = stash->loadable_section_count;
  p = stash->loadable_sections;
  for (; i > 0; i--, p++)
    p->section->vma = 0;
}

/* Set unique vmas for loadable sections in ABFD and save vmas in
   STASH for unset_sections.  */

static bfd_boolean
place_sections (bfd *abfd, struct dwarf2_debug *stash)
{
  struct loadable_section *p;
  unsigned int i;

  if (stash->loadable_section_count != 0)
    {
      i = stash->loadable_section_count;
      p = stash->loadable_sections;
      for (; i > 0; i--, p++)
	p->section->vma = p->adj_vma;
    }
  else
    {
      asection *sect;
      bfd_vma last_vma = 0;
      bfd_size_type amt;
      struct loadable_section *p;

      i = 0;
      for (sect = abfd->sections; sect != NULL; sect = sect->next)
	{
	  bfd_size_type sz;

	  if (sect->vma != 0 || (sect->flags & SEC_LOAD) == 0)
	    continue;

	  sz = sect->rawsize ? sect->rawsize : sect->size;
	  if (sz == 0)
	    continue;

	  i++;
	}

      amt = i * sizeof (struct loadable_section);
      p = (struct loadable_section *) bfd_zalloc (abfd, amt);
      if (! p)
	return FALSE;

      stash->loadable_sections = p;
      stash->loadable_section_count = i;

      for (sect = abfd->sections; sect != NULL; sect = sect->next)
	{
	  bfd_size_type sz;

	  if (sect->vma != 0 || (sect->flags & SEC_LOAD) == 0)
	    continue;

	  sz = sect->rawsize ? sect->rawsize : sect->size;
	  if (sz == 0)
	    continue;

	  p->section = sect;
	  if (last_vma != 0)
	    {
	      /* Align the new address to the current section
		 alignment.  */
	      last_vma = ((last_vma
			   + ~((bfd_vma) -1 << sect->alignment_power))
			  & ((bfd_vma) -1 << sect->alignment_power));
	      sect->vma = last_vma;
	    }
	  p->adj_vma = sect->vma;
	  last_vma += sect->vma + sz;

	  p++;
	}
    }

  return TRUE;
}

/* Find the source code location of SYMBOL.  If SYMBOL is NULL
   then find the nearest source code location corresponding to
   the address SECTION + OFFSET.
   Returns TRUE if the line is found without error and fills in
   FILENAME_PTR and LINENUMBER_PTR.  In the case where SYMBOL was
   NULL the FUNCTIONNAME_PTR is also filled in.
   SYMBOLS contains the symbol table for ABFD.
   ADDR_SIZE is the number of bytes in the initial .debug_info length
   field and in the abbreviation offset, or zero to indicate that the
   default value should be used.  */

static bfd_boolean
find_line (bfd *abfd,
	   asection *section,
	   bfd_vma offset,
	   asymbol *symbol,
	   asymbol **symbols,
	   const char **filename_ptr,
	   const char **functionname_ptr,
	   unsigned int *linenumber_ptr,
	   unsigned int addr_size,
	   void **pinfo)
{
  /* Read each compilation unit from the section .debug_info, and check
     to see if it contains the address we are searching for.  If yes,
     lookup the address, and return the line number info.  If no, go
     on to the next compilation unit.

     We keep a list of all the previously read compilation units, and
     a pointer to the next un-read compilation unit.  Check the
     previously read units before reading more.  */
  struct dwarf2_debug *stash;
  /* What address are we looking for?  */
  bfd_vma addr;
  struct comp_unit* each;
  bfd_vma found = FALSE;
  bfd_boolean do_line;

  stash = *pinfo;

  if (! stash)
    {
      bfd_size_type amt = sizeof (struct dwarf2_debug);

      stash = bfd_zalloc (abfd, amt);
      if (! stash)
	return FALSE;
    }

  /* In a relocatable file, 2 functions may have the same address.
     We change the section vma so that they won't overlap.  */
  if ((abfd->flags & (EXEC_P | DYNAMIC)) == 0)
    {
      if (! place_sections (abfd, stash))
	return FALSE;
    }

  do_line = (section == NULL
	     && offset == 0
	     && functionname_ptr == NULL
	     && symbol != NULL);
  if (do_line)
    {
      addr = symbol->value;
      section = bfd_get_section (symbol);
    }
  else if (section != NULL
	   && functionname_ptr != NULL
	   && symbol == NULL)
    addr = offset;
  else
    abort ();

  if (section->output_section)
    addr += section->output_section->vma + section->output_offset;
  else
    addr += section->vma;
  *filename_ptr = NULL;
  if (!do_line)
    *functionname_ptr = NULL;
  *linenumber_ptr = 0;

  if (! *pinfo)
    {
      bfd *debug_bfd;
      bfd_size_type total_size;
      asection *msec;

      *pinfo = stash;

      msec = find_debug_info (abfd, NULL);
      if (msec == NULL)
	{
	  char * debug_filename = bfd_follow_gnu_debuglink (abfd, DEBUGDIR);

	  if (debug_filename == NULL)
	    /* No dwarf2 info, and no gnu_debuglink to follow.
	       Note that at this point the stash has been allocated, but
	       contains zeros.  This lets future calls to this function
	       fail more quickly.  */
	    goto done;

	  if ((debug_bfd = bfd_openr (debug_filename, NULL)) == NULL
	      || ! bfd_check_format (debug_bfd, bfd_object)
	      || (msec = find_debug_info (debug_bfd, NULL)) == NULL)
	    {
	      if (debug_bfd)
		bfd_close (debug_bfd);
	      /* FIXME: Should we report our failure to follow the debuglink ?  */
	      free (debug_filename);
	      goto done;
	    }
	}
      else
	debug_bfd = abfd;

      /* There can be more than one DWARF2 info section in a BFD these days.
	 Read them all in and produce one large stash.  We do this in two
	 passes - in the first pass we just accumulate the section sizes.
	 In the second pass we read in the section's contents.  The allows
	 us to avoid reallocing the data as we add sections to the stash.  */
      for (total_size = 0; msec; msec = find_debug_info (debug_bfd, msec))
	total_size += msec->size;

      stash->info_ptr = bfd_alloc (debug_bfd, total_size);
      if (stash->info_ptr == NULL)
	goto done;

      stash->info_ptr_end = stash->info_ptr;

      for (msec = find_debug_info (debug_bfd, NULL);
	   msec;
	   msec = find_debug_info (debug_bfd, msec))
	{
	  bfd_size_type size;
	  bfd_size_type start;

	  size = msec->size;
	  if (size == 0)
	    continue;

	  start = stash->info_ptr_end - stash->info_ptr;

	  if ((bfd_simple_get_relocated_section_contents
	       (debug_bfd, msec, stash->info_ptr + start, symbols)) == NULL)
	    continue;

	  stash->info_ptr_end = stash->info_ptr + start + size;
	}

      BFD_ASSERT (stash->info_ptr_end == stash->info_ptr + total_size);

      stash->sec = find_debug_info (debug_bfd, NULL);
      stash->sec_info_ptr = stash->info_ptr;
      stash->syms = symbols;
      stash->bfd = debug_bfd;
    }

  /* A null info_ptr indicates that there is no dwarf2 info
     (or that an error occured while setting up the stash).  */
  if (! stash->info_ptr)
    goto done;

  stash->inliner_chain = NULL;

  /* Check the previously read comp. units first.  */
  for (each = stash->all_comp_units; each; each = each->next_unit)
    {
      if (do_line)
	found = (((symbol->flags & BSF_FUNCTION) == 0
		  || comp_unit_contains_address (each, addr))
		 && comp_unit_find_line (each, symbol, addr,
					 filename_ptr, linenumber_ptr,
					 stash));
      else
	found = (comp_unit_contains_address (each, addr)
		 && comp_unit_find_nearest_line (each, addr,
						 filename_ptr,
						 functionname_ptr,
						 linenumber_ptr,
						 stash));
      if (found)
	goto done;
    }

  /* The DWARF2 spec says that the initial length field, and the
     offset of the abbreviation table, should both be 4-byte values.
     However, some compilers do things differently.  */
  if (addr_size == 0)
    addr_size = 4;
  BFD_ASSERT (addr_size == 4 || addr_size == 8);

  /* Read each remaining comp. units checking each as they are read.  */
  while (stash->info_ptr < stash->info_ptr_end)
    {
      bfd_vma length;
      unsigned int offset_size = addr_size;
      bfd_byte *info_ptr_unit = stash->info_ptr;

      length = read_4_bytes (stash->bfd, stash->info_ptr);
      /* A 0xffffff length is the DWARF3 way of indicating
	 we use 64-bit offsets, instead of 32-bit offsets.  */
      if (length == 0xffffffff)
	{
	  offset_size = 8;
	  length = read_8_bytes (stash->bfd, stash->info_ptr + 4);
	  stash->info_ptr += 12;
	}
      /* A zero length is the IRIX way of indicating 64-bit offsets,
	 mostly because the 64-bit length will generally fit in 32
	 bits, and the endianness helps.  */
      else if (length == 0)
	{
	  offset_size = 8;
	  length = read_4_bytes (stash->bfd, stash->info_ptr + 4);
	  stash->info_ptr += 8;
	}
      /* In the absence of the hints above, we assume 32-bit DWARF2
	 offsets even for targets with 64-bit addresses, because:
	   a) most of the time these targets will not have generated
	      more than 2Gb of debug info and so will not need 64-bit
	      offsets,
	 and
	   b) if they do use 64-bit offsets but they are not using
	      the size hints that are tested for above then they are
	      not conforming to the DWARF3 standard anyway.  */
      else if (addr_size == 8)
	{
	  offset_size = 4;
          stash->info_ptr += 4;
	}
      else
	stash->info_ptr += 4;

      if (length > 0)
	{
	  each = parse_comp_unit (stash, length, info_ptr_unit,
				  offset_size);
	  stash->info_ptr += length;

	  if ((bfd_vma) (stash->info_ptr - stash->sec_info_ptr)
	      == stash->sec->size)
	    {
	      stash->sec = find_debug_info (stash->bfd, stash->sec);
	      stash->sec_info_ptr = stash->info_ptr;
	    }

	  if (each)
	    {
	      each->next_unit = stash->all_comp_units;
	      stash->all_comp_units = each;

	      /* DW_AT_low_pc and DW_AT_high_pc are optional for
		 compilation units.  If we don't have them (i.e.,
		 unit->high == 0), we need to consult the line info
		 table to see if a compilation unit contains the given
		 address.  */
	      if (do_line)
		found = (((symbol->flags & BSF_FUNCTION) == 0
			  || each->arange.high == 0
			  || comp_unit_contains_address (each, addr))
			 && comp_unit_find_line (each, symbol, addr,
						 filename_ptr,
						 linenumber_ptr,
						 stash));
	      else
		found = ((each->arange.high == 0
			  || comp_unit_contains_address (each, addr))
			 && comp_unit_find_nearest_line (each, addr,
							 filename_ptr,
							 functionname_ptr,
							 linenumber_ptr,
							 stash));
	      if (found)
		goto done;
	    }
	}
    }

done:
  if ((abfd->flags & (EXEC_P | DYNAMIC)) == 0)
    unset_sections (stash);

  return found;
}

/* The DWARF2 version of find_nearest_line.
   Return TRUE if the line is found without error.  */

bfd_boolean
_bfd_dwarf2_find_nearest_line (bfd *abfd,
			       asection *section,
			       asymbol **symbols,
			       bfd_vma offset,
			       const char **filename_ptr,
			       const char **functionname_ptr,
			       unsigned int *linenumber_ptr,
			       unsigned int addr_size,
			       void **pinfo)
{
  return find_line (abfd, section, offset, NULL, symbols, filename_ptr,
		    functionname_ptr, linenumber_ptr, addr_size,
		    pinfo);
}

/* The DWARF2 version of find_line.
   Return TRUE if the line is found without error.  */

bfd_boolean
_bfd_dwarf2_find_line (bfd *abfd,
		       asymbol **symbols,
		       asymbol *symbol,
		       const char **filename_ptr,
		       unsigned int *linenumber_ptr,
		       unsigned int addr_size,
		       void **pinfo)
{
  return find_line (abfd, NULL, 0, symbol, symbols, filename_ptr,
		    NULL, linenumber_ptr, addr_size,
		    pinfo);
}

bfd_boolean
_bfd_dwarf2_find_inliner_info (bfd *abfd ATTRIBUTE_UNUSED,
			       const char **filename_ptr,
			       const char **functionname_ptr,
			       unsigned int *linenumber_ptr,
			       void **pinfo)
{
  struct dwarf2_debug *stash;

  stash = *pinfo;
  if (stash)
    {
      struct funcinfo *func = stash->inliner_chain;

      if (func && func->caller_func)
	{
	  *filename_ptr = func->caller_file;
	  *functionname_ptr = func->caller_func->name;
	  *linenumber_ptr = func->caller_line;
	  stash->inliner_chain = func->caller_func;
	  return TRUE;
	}
    }

  return FALSE;
}

void
_bfd_dwarf2_cleanup_debug_info (bfd *abfd)
{
  struct comp_unit *each;
  struct dwarf2_debug *stash;

  if (abfd == NULL || elf_tdata (abfd) == NULL)
    return;

  stash = elf_tdata (abfd)->dwarf2_find_line_info;

  if (stash == NULL)
    return;

  for (each = stash->all_comp_units; each; each = each->next_unit)
    {
      struct abbrev_info **abbrevs = each->abbrevs;
      size_t i;

      for (i = 0; i < ABBREV_HASH_SIZE; i++)
	{
	  struct abbrev_info *abbrev = abbrevs[i];

	  while (abbrev)
	    {
	      free (abbrev->attrs);
	      abbrev = abbrev->next;
	    }
	}

      if (each->line_table)
	{
	  free (each->line_table->dirs);
	  free (each->line_table->files);
	}
    }

  free (stash->dwarf_abbrev_buffer);
  free (stash->dwarf_line_buffer);
  free (stash->dwarf_ranges_buffer);
}
