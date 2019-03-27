/* Stabs in sections linking support.
   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
   2006, 2007 Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support.

   This file is part of BFD, the Binary File Descriptor library.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* This file contains support for linking stabs in sections, as used
   on COFF and ELF.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "aout/stab_gnu.h"
#include "safe-ctype.h"

/* Stabs entries use a 12 byte format:
     4 byte string table index
     1 byte stab type
     1 byte stab other field
     2 byte stab desc field
     4 byte stab value
   FIXME: This will have to change for a 64 bit object format.

   The stabs symbols are divided into compilation units.  For the
   first entry in each unit, the type of 0, the value is the length of
   the string table for this unit, and the desc field is the number of
   stabs symbols for this unit.  */

#define STRDXOFF  0
#define TYPEOFF   4
#define OTHEROFF  5
#define DESCOFF   6
#define VALOFF    8
#define STABSIZE  12

/* A linked list of totals that we have found for a particular header
   file.  A total is a unique identifier for a particular BINCL...EINCL
   sequence of STABs that can be used to identify duplicate sequences.
   It consists of three fields, 'sum_chars' which is the sum of all the
   STABS characters; 'num_chars' which is the number of these charactes
   and 'symb' which is a buffer of all the symbols in the sequence.  This
   buffer is only checked as a last resort.  */

struct stab_link_includes_totals
{
  struct stab_link_includes_totals *next;
  bfd_vma sum_chars;  /* Accumulated sum of STABS characters.  */
  bfd_vma num_chars;  /* Number of STABS characters.  */
  const char* symb;   /* The STABS characters themselves.  */
};

/* An entry in the header file hash table.  */

struct stab_link_includes_entry
{
  struct bfd_hash_entry root;
  /* List of totals we have found for this file.  */
  struct stab_link_includes_totals *totals;
};

/* This structure is used to hold a list of N_BINCL symbols, some of
   which might be converted into N_EXCL symbols.  */

struct stab_excl_list
{
  /* The next symbol to convert.  */
  struct stab_excl_list *next;
  /* The offset to this symbol in the section contents.  */
  bfd_size_type offset;
  /* The value to use for the symbol.  */
  bfd_vma val;
  /* The type of this symbol (N_BINCL or N_EXCL).  */
  int type;
};

/* This structure is stored with each .stab section.  */

struct stab_section_info
{
  /* This is a linked list of N_BINCL symbols which should be
     converted into N_EXCL symbols.  */
  struct stab_excl_list *excls;

  /* This is used to map input stab offsets within their sections
     to output stab offsets, to take into account stabs that have
     been deleted.  If it is NULL, the output offsets are the same
     as the input offsets, because no stabs have been deleted from
     this section.  Otherwise the i'th entry is the number of
     bytes of stabs that have been deleted prior to the i'th
     stab.  */
  bfd_size_type *cumulative_skips;

  /* This is an array of string indices.  For each stab symbol, we
     store the string index here.  If a stab symbol should not be
     included in the final output, the string index is -1.  */
  bfd_size_type stridxs[1];
};


/* The function to create a new entry in the header file hash table.  */

static struct bfd_hash_entry *
stab_link_includes_newfunc (struct bfd_hash_entry *entry,
			    struct bfd_hash_table *table,
			    const char *string)
{
  struct stab_link_includes_entry *ret =
    (struct stab_link_includes_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == NULL)
    ret = bfd_hash_allocate (table,
			     sizeof (struct stab_link_includes_entry));
  if (ret == NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  ret = ((struct stab_link_includes_entry *)
	 bfd_hash_newfunc ((struct bfd_hash_entry *) ret, table, string));
  if (ret)
    /* Set local fields.  */
    ret->totals = NULL;

  return (struct bfd_hash_entry *) ret;
}

/* This function is called for each input file from the add_symbols
   pass of the linker.  */

bfd_boolean
_bfd_link_section_stabs (bfd *abfd,
			 struct stab_info *sinfo,
			 asection *stabsec,
			 asection *stabstrsec,
			 void * *psecinfo,
			 bfd_size_type *pstring_offset)
{
  bfd_boolean first;
  bfd_size_type count, amt;
  struct stab_section_info *secinfo;
  bfd_byte *stabbuf = NULL;
  bfd_byte *stabstrbuf = NULL;
  bfd_byte *sym, *symend;
  bfd_size_type stroff, next_stroff, skip;
  bfd_size_type *pstridx;

  if (stabsec->size == 0
      || stabstrsec->size == 0)
    /* This file does not contain stabs debugging information.  */
    return TRUE;

  if (stabsec->size % STABSIZE != 0)
    /* Something is wrong with the format of these stab symbols.
       Don't try to optimize them.  */
    return TRUE;

  if ((stabstrsec->flags & SEC_RELOC) != 0)
    /* We shouldn't see relocations in the strings, and we aren't
       prepared to handle them.  */
    return TRUE;

  if (bfd_is_abs_section (stabsec->output_section)
      || bfd_is_abs_section (stabstrsec->output_section))
    /* At least one of the sections is being discarded from the
       link, so we should just ignore them.  */
    return TRUE;

  first = FALSE;

  if (sinfo->stabstr == NULL)
    {
      flagword flags;

      /* Initialize the stabs information we need to keep track of.  */
      first = TRUE;
      sinfo->strings = _bfd_stringtab_init ();
      if (sinfo->strings == NULL)
	goto error_return;
      /* Make sure the first byte is zero.  */
      (void) _bfd_stringtab_add (sinfo->strings, "", TRUE, TRUE);
      if (! bfd_hash_table_init (&sinfo->includes,
				 stab_link_includes_newfunc,
				 sizeof (struct stab_link_includes_entry)))
	goto error_return;
      flags = (SEC_HAS_CONTENTS | SEC_READONLY | SEC_DEBUGGING
	       | SEC_LINKER_CREATED);
      sinfo->stabstr = bfd_make_section_anyway_with_flags (abfd, ".stabstr",
							   flags);
      if (sinfo->stabstr == NULL)
	goto error_return;
    }

  /* Initialize the information we are going to store for this .stab
     section.  */
  count = stabsec->size / STABSIZE;

  amt = sizeof (struct stab_section_info);
  amt += (count - 1) * sizeof (bfd_size_type);
  *psecinfo = bfd_alloc (abfd, amt);
  if (*psecinfo == NULL)
    goto error_return;

  secinfo = (struct stab_section_info *) *psecinfo;
  secinfo->excls = NULL;
  stabsec->rawsize = stabsec->size;
  secinfo->cumulative_skips = NULL;
  memset (secinfo->stridxs, 0, (size_t) count * sizeof (bfd_size_type));

  /* Read the stabs information from abfd.  */
  if (!bfd_malloc_and_get_section (abfd, stabsec, &stabbuf)
      || !bfd_malloc_and_get_section (abfd, stabstrsec, &stabstrbuf))
    goto error_return;

  /* Look through the stabs symbols, work out the new string indices,
     and identify N_BINCL symbols which can be eliminated.  */
  stroff = 0;
  /* The stabs sections can be split when
     -split-by-reloc/-split-by-file is used.  We must keep track of
     each stab section's place in the single concatenated string
     table.  */
  next_stroff = pstring_offset ? *pstring_offset : 0;
  skip = 0;

  symend = stabbuf + stabsec->size;
  for (sym = stabbuf, pstridx = secinfo->stridxs;
       sym < symend;
       sym += STABSIZE, ++pstridx)
    {
      bfd_size_type symstroff;
      int type;
      const char *string;

      if (*pstridx != 0)
	/* This symbol has already been handled by an N_BINCL pass.  */
	continue;

      type = sym[TYPEOFF];

      if (type == 0)
	{
	  /* Special type 0 stabs indicate the offset to the next
	     string table.  We only copy the very first one.  */
	  stroff = next_stroff;
	  next_stroff += bfd_get_32 (abfd, sym + 8);
	  if (pstring_offset)
	    *pstring_offset = next_stroff;
	  if (! first)
	    {
	      *pstridx = (bfd_size_type) -1;
	      ++skip;
	      continue;
	    }
	  first = FALSE;
	}

      /* Store the string in the hash table, and record the index.  */
      symstroff = stroff + bfd_get_32 (abfd, sym + STRDXOFF);
      if (symstroff >= stabstrsec->size)
	{
	  (*_bfd_error_handler)
	    (_("%B(%A+0x%lx): Stabs entry has invalid string index."),
	     abfd, stabsec, (long) (sym - stabbuf));
	  bfd_set_error (bfd_error_bad_value);
	  goto error_return;
	}
      string = (char *) stabstrbuf + symstroff;
      *pstridx = _bfd_stringtab_add (sinfo->strings, string, TRUE, TRUE);

      /* An N_BINCL symbol indicates the start of the stabs entries
	 for a header file.  We need to scan ahead to the next N_EINCL
	 symbol, ignoring nesting, adding up all the characters in the
	 symbol names, not including the file numbers in types (the
	 first number after an open parenthesis).  */
      if (type == (int) N_BINCL)
	{
	  bfd_vma sum_chars;
	  bfd_vma num_chars;
	  bfd_vma buf_len = 0;
	  char * symb;
	  char * symb_rover;
	  int nest;
	  bfd_byte * incl_sym;
	  struct stab_link_includes_entry * incl_entry;
	  struct stab_link_includes_totals * t;
	  struct stab_excl_list * ne;

	  symb = symb_rover = NULL;
	  sum_chars = num_chars = 0;
	  nest = 0;

	  for (incl_sym = sym + STABSIZE;
	       incl_sym < symend;
	       incl_sym += STABSIZE)
	    {
	      int incl_type;

	      incl_type = incl_sym[TYPEOFF];
	      if (incl_type == 0)
		break;
	      else if (incl_type == (int) N_EXCL)
		continue;
	      else if (incl_type == (int) N_EINCL)
		{
		  if (nest == 0)
		    break;
		  --nest;
		}
	      else if (incl_type == (int) N_BINCL)
		++nest;
	      else if (nest == 0)
		{
		  const char *str;

		  str = ((char *) stabstrbuf
			 + stroff
			 + bfd_get_32 (abfd, incl_sym + STRDXOFF));
		  for (; *str != '\0'; str++)
		    {
		      if (num_chars >= buf_len)
			{
			  buf_len += 32 * 1024;
			  symb = bfd_realloc (symb, buf_len);
			  if (symb == NULL)
			    goto error_return;
			  symb_rover = symb + num_chars;
			}
		      * symb_rover ++ = * str;
		      sum_chars += *str;
		      num_chars ++;
		      if (*str == '(')
			{
			  /* Skip the file number.  */
			  ++str;
			  while (ISDIGIT (*str))
			    ++str;
			  --str;
			}
		    }
		}
	    }

	  BFD_ASSERT (num_chars == (bfd_vma) (symb_rover - symb));

	  /* If we have already included a header file with the same
	     value, then replaced this one with an N_EXCL symbol.  */
	  incl_entry = (struct stab_link_includes_entry * )
	    bfd_hash_lookup (&sinfo->includes, string, TRUE, TRUE);
	  if (incl_entry == NULL)
	    goto error_return;

	  for (t = incl_entry->totals; t != NULL; t = t->next)
	    if (t->sum_chars == sum_chars
		&& t->num_chars == num_chars
		&& memcmp (t->symb, symb, num_chars) == 0)
	      break;

	  /* Record this symbol, so that we can set the value
	     correctly.  */
	  amt = sizeof *ne;
	  ne = bfd_alloc (abfd, amt);
	  if (ne == NULL)
	    goto error_return;
	  ne->offset = sym - stabbuf;
	  ne->val = sum_chars;
	  ne->type = (int) N_BINCL;
	  ne->next = secinfo->excls;
	  secinfo->excls = ne;

	  if (t == NULL)
	    {
	      /* This is the first time we have seen this header file
		 with this set of stabs strings.  */
	      t = bfd_hash_allocate (&sinfo->includes, sizeof *t);
	      if (t == NULL)
		goto error_return;
	      t->sum_chars = sum_chars;
	      t->num_chars = num_chars;
	      t->symb = bfd_realloc (symb, num_chars); /* Trim data down.  */
	      t->next = incl_entry->totals;
	      incl_entry->totals = t;
	    }
	  else
	    {
	      bfd_size_type *incl_pstridx;

	      /* We have seen this header file before.  Tell the final
		 pass to change the type to N_EXCL.  */
	      ne->type = (int) N_EXCL;

	      /* Free off superfluous symbols.  */
	      free (symb);

	      /* Mark the skipped symbols.  */

	      nest = 0;
	      for (incl_sym = sym + STABSIZE, incl_pstridx = pstridx + 1;
		   incl_sym < symend;
		   incl_sym += STABSIZE, ++incl_pstridx)
		{
		  int incl_type;

		  incl_type = incl_sym[TYPEOFF];

		  if (incl_type == (int) N_EINCL)
		    {
		      if (nest == 0)
			{
			  *incl_pstridx = (bfd_size_type) -1;
			  ++skip;
			  break;
			}
		      --nest;
		    }
		  else if (incl_type == (int) N_BINCL)
		    ++nest;
		  else if (incl_type == (int) N_EXCL)
		    /* Keep existing exclusion marks.  */
		    continue;
		  else if (nest == 0)
		    {
		      *incl_pstridx = (bfd_size_type) -1;
		      ++skip;
		    }
		}
	    }
	}
    }

  free (stabbuf);
  stabbuf = NULL;
  free (stabstrbuf);
  stabstrbuf = NULL;

  /* We need to set the section sizes such that the linker will
     compute the output section sizes correctly.  We set the .stab
     size to not include the entries we don't want.  We set
     SEC_EXCLUDE for the .stabstr section, so that it will be dropped
     from the link.  We record the size of the strtab in the first
     .stabstr section we saw, and make sure we don't set SEC_EXCLUDE
     for that section.  */
  stabsec->size = (count - skip) * STABSIZE;
  if (stabsec->size == 0)
    stabsec->flags |= SEC_EXCLUDE | SEC_KEEP;
  stabstrsec->flags |= SEC_EXCLUDE | SEC_KEEP;
  sinfo->stabstr->size = _bfd_stringtab_size (sinfo->strings);

  /* Calculate the `cumulative_skips' array now that stabs have been
     deleted for this section.  */

  if (skip != 0)
    {
      bfd_size_type i, offset;
      bfd_size_type *pskips;

      amt = count * sizeof (bfd_size_type);
      secinfo->cumulative_skips = bfd_alloc (abfd, amt);
      if (secinfo->cumulative_skips == NULL)
	goto error_return;

      pskips = secinfo->cumulative_skips;
      pstridx = secinfo->stridxs;
      offset = 0;

      for (i = 0; i < count; i++, pskips++, pstridx++)
	{
	  *pskips = offset;
	  if (*pstridx == (bfd_size_type) -1)
	    offset += STABSIZE;
	}

      BFD_ASSERT (offset != 0);
    }

  return TRUE;

 error_return:
  if (stabbuf != NULL)
    free (stabbuf);
  if (stabstrbuf != NULL)
    free (stabstrbuf);
  return FALSE;
}

/* This function is called for each input file before the stab
   section is relocated.  It discards stab entries for discarded
   functions and variables.  The function returns TRUE iff
   any entries have been deleted.
*/

bfd_boolean
_bfd_discard_section_stabs (bfd *abfd,
			    asection *stabsec,
			    void * psecinfo,
			    bfd_boolean (*reloc_symbol_deleted_p) (bfd_vma, void *),
			    void * cookie)
{
  bfd_size_type count, amt;
  struct stab_section_info *secinfo;
  bfd_byte *stabbuf = NULL;
  bfd_byte *sym, *symend;
  bfd_size_type skip;
  bfd_size_type *pstridx;
  int deleting;

  if (stabsec->size == 0)
    /* This file does not contain stabs debugging information.  */
    return FALSE;

  if (stabsec->size % STABSIZE != 0)
    /* Something is wrong with the format of these stab symbols.
       Don't try to optimize them.  */
    return FALSE;

  if ((stabsec->output_section != NULL
       && bfd_is_abs_section (stabsec->output_section)))
    /* At least one of the sections is being discarded from the
       link, so we should just ignore them.  */
    return FALSE;

  /* We should have initialized our data in _bfd_link_stab_sections.
     If there was some bizarre error reading the string sections, though,
     we might not have.  Bail rather than asserting.  */
  if (psecinfo == NULL)
    return FALSE;

  count = stabsec->rawsize / STABSIZE;
  secinfo = (struct stab_section_info *) psecinfo;

  /* Read the stabs information from abfd.  */
  if (!bfd_malloc_and_get_section (abfd, stabsec, &stabbuf))
    goto error_return;

  /* Look through the stabs symbols and discard any information for
     discarded functions.  */
  skip = 0;
  deleting = -1;

  symend = stabbuf + stabsec->rawsize;
  for (sym = stabbuf, pstridx = secinfo->stridxs;
       sym < symend;
       sym += STABSIZE, ++pstridx)
    {
      int type;

      if (*pstridx == (bfd_size_type) -1)
	/* This stab was deleted in a previous pass.  */
	continue;

      type = sym[TYPEOFF];

      if (type == (int) N_FUN)
	{
	  int strx = bfd_get_32 (abfd, sym + STRDXOFF);

	  if (strx == 0)
	    {
	      if (deleting)
		{
		  skip++;
		  *pstridx = -1;
		}
	      deleting = -1;
	      continue;
	    }
	  deleting = 0;
	  if ((*reloc_symbol_deleted_p) (sym + VALOFF - stabbuf, cookie))
	    deleting = 1;
	}

      if (deleting == 1)
	{
	  *pstridx = -1;
	  skip++;
	}
      else if (deleting == -1)
	{
	  /* Outside of a function.  Check for deleted variables.  */
	  if (type == (int) N_STSYM || type == (int) N_LCSYM)
	    if ((*reloc_symbol_deleted_p) (sym + VALOFF - stabbuf, cookie))
	      {
		*pstridx = -1;
		skip ++;
	      }
	  /* We should also check for N_GSYM entries which reference a
	     deleted global, but those are less harmful to debuggers
	     and would require parsing the stab strings.  */
	}
    }

  free (stabbuf);
  stabbuf = NULL;

  /* Shrink the stabsec as needed.  */
  stabsec->size -= skip * STABSIZE;
  if (stabsec->size == 0)
    stabsec->flags |= SEC_EXCLUDE | SEC_KEEP;

  /* Recalculate the `cumulative_skips' array now that stabs have been
     deleted for this section.  */

  if (skip != 0)
    {
      bfd_size_type i, offset;
      bfd_size_type *pskips;

      if (secinfo->cumulative_skips == NULL)
	{
	  amt = count * sizeof (bfd_size_type);
	  secinfo->cumulative_skips = bfd_alloc (abfd, amt);
	  if (secinfo->cumulative_skips == NULL)
	    goto error_return;
	}

      pskips = secinfo->cumulative_skips;
      pstridx = secinfo->stridxs;
      offset = 0;

      for (i = 0; i < count; i++, pskips++, pstridx++)
	{
	  *pskips = offset;
	  if (*pstridx == (bfd_size_type) -1)
	    offset += STABSIZE;
	}

      BFD_ASSERT (offset != 0);
    }

  return skip > 0;

 error_return:
  if (stabbuf != NULL)
    free (stabbuf);
  return FALSE;
}

/* Write out the stab section.  This is called with the relocated
   contents.  */

bfd_boolean
_bfd_write_section_stabs (bfd *output_bfd,
			  struct stab_info *sinfo,
			  asection *stabsec,
			  void * *psecinfo,
			  bfd_byte *contents)
{
  struct stab_section_info *secinfo;
  struct stab_excl_list *e;
  bfd_byte *sym, *tosym, *symend;
  bfd_size_type *pstridx;

  secinfo = (struct stab_section_info *) *psecinfo;

  if (secinfo == NULL)
    return bfd_set_section_contents (output_bfd, stabsec->output_section,
				     contents, stabsec->output_offset,
				     stabsec->size);

  /* Handle each N_BINCL entry.  */
  for (e = secinfo->excls; e != NULL; e = e->next)
    {
      bfd_byte *excl_sym;

      BFD_ASSERT (e->offset < stabsec->rawsize);
      excl_sym = contents + e->offset;
      bfd_put_32 (output_bfd, e->val, excl_sym + VALOFF);
      excl_sym[TYPEOFF] = e->type;
    }

  /* Copy over all the stabs symbols, omitting the ones we don't want,
     and correcting the string indices for those we do want.  */
  tosym = contents;
  symend = contents + stabsec->rawsize;
  for (sym = contents, pstridx = secinfo->stridxs;
       sym < symend;
       sym += STABSIZE, ++pstridx)
    {
      if (*pstridx != (bfd_size_type) -1)
	{
	  if (tosym != sym)
	    memcpy (tosym, sym, STABSIZE);
	  bfd_put_32 (output_bfd, *pstridx, tosym + STRDXOFF);

	  if (sym[TYPEOFF] == 0)
	    {
	      /* This is the header symbol for the stabs section.  We
		 don't really need one, since we have merged all the
		 input stabs sections into one, but we generate one
		 for the benefit of readers which expect to see one.  */
	      BFD_ASSERT (sym == contents);
	      bfd_put_32 (output_bfd, _bfd_stringtab_size (sinfo->strings),
			  tosym + VALOFF);
	      bfd_put_16 (output_bfd,
			  stabsec->output_section->size / STABSIZE - 1,
			  tosym + DESCOFF);
	    }

	  tosym += STABSIZE;
	}
    }

  BFD_ASSERT ((bfd_size_type) (tosym - contents) == stabsec->size);

  return bfd_set_section_contents (output_bfd, stabsec->output_section,
				   contents, (file_ptr) stabsec->output_offset,
				   stabsec->size);
}

/* Write out the .stabstr section.  */

bfd_boolean
_bfd_write_stab_strings (bfd *output_bfd, struct stab_info *sinfo)
{
  if (bfd_is_abs_section (sinfo->stabstr->output_section))
    /* The section was discarded from the link.  */
    return TRUE;

  BFD_ASSERT ((sinfo->stabstr->output_offset
	       + _bfd_stringtab_size (sinfo->strings))
	      <= sinfo->stabstr->output_section->size);

  if (bfd_seek (output_bfd,
		(file_ptr) (sinfo->stabstr->output_section->filepos
			    + sinfo->stabstr->output_offset),
		SEEK_SET) != 0)
    return FALSE;

  if (! _bfd_stringtab_emit (output_bfd, sinfo->strings))
    return FALSE;

  /* We no longer need the stabs information.  */
  _bfd_stringtab_free (sinfo->strings);
  bfd_hash_table_free (&sinfo->includes);

  return TRUE;
}

/* Adjust an address in the .stab section.  Given OFFSET within
   STABSEC, this returns the new offset in the adjusted stab section,
   or -1 if the address refers to a stab which has been removed.  */

bfd_vma
_bfd_stab_section_offset (asection *stabsec,
			  void * psecinfo,
			  bfd_vma offset)
{
  struct stab_section_info *secinfo;

  secinfo = (struct stab_section_info *) psecinfo;

  if (secinfo == NULL)
    return offset;

  if (offset >= stabsec->rawsize)
    return offset - stabsec->rawsize + stabsec->size;

  if (secinfo->cumulative_skips)
    {
      bfd_vma i;

      i = offset / STABSIZE;

      if (secinfo->stridxs [i] == (bfd_size_type) -1)
	return (bfd_vma) -1;

      return offset - secinfo->cumulative_skips [i];
    }

  return offset;
}
