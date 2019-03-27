/* subsegs.c - subsegments -
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Segments & sub-segments.  */

#include "as.h"

#include "subsegs.h"
#include "obstack.h"

frchainS *frchain_now;

static struct obstack frchains;

static fragS dummy_frag;


void
subsegs_begin (void)
{
  obstack_begin (&frchains, chunksize);
#if __GNUC__ >= 2
  obstack_alignment_mask (&frchains) = __alignof__ (frchainS) - 1;
#endif

  frchain_now = NULL;		/* Warn new_subseg() that we are booting.  */
  frag_now = &dummy_frag;
}

/*
 *			subseg_change()
 *
 * Change the subsegment we are in, BUT DO NOT MAKE A NEW FRAG for the
 * subsegment. If we are already in the correct subsegment, change nothing.
 * This is used eg as a worker for subseg_set [which does make a new frag_now]
 * and for changing segments after we have read the source. We construct eg
 * fixSs even after the source file is read, so we do have to keep the
 * segment context correct.
 */
void
subseg_change (register segT seg, register int subseg)
{
  segment_info_type *seginfo = seg_info (seg);
  now_seg = seg;
  now_subseg = subseg;

  if (! seginfo)
    {
      seginfo = xcalloc (1, sizeof (*seginfo));
      seginfo->bfd_section = seg;
      (void) bfd_set_section_userdata (stdoutput, seg, seginfo);
    }
}

static void
subseg_set_rest (segT seg, subsegT subseg)
{
  frchainS *frcP;		/* crawl frchain chain */
  frchainS **lastPP;		/* address of last pointer */
  frchainS *newP;		/* address of new frchain */
  segment_info_type *seginfo;

  mri_common_symbol = NULL;

  if (frag_now && frchain_now)
    frchain_now->frch_frag_now = frag_now;

  assert (frchain_now == 0
	  || frchain_now->frch_last == frag_now);

  subseg_change (seg, (int) subseg);

  seginfo = seg_info (seg);

  /* Attempt to find or make a frchain for that subsection.
     We keep the list sorted by subsection number.  */
  for (frcP = *(lastPP = &seginfo->frchainP);
       frcP != NULL;
       frcP = *(lastPP = &frcP->frch_next))
    if (frcP->frch_subseg >= subseg)
      break;

  if (frcP == NULL || frcP->frch_subseg != subseg)
    {
      /* This should be the only code that creates a frchainS.  */

      newP = obstack_alloc (&frchains, sizeof (frchainS));
      newP->frch_subseg = subseg;
      newP->fix_root = NULL;
      newP->fix_tail = NULL;
      obstack_begin (&newP->frch_obstack, chunksize);
#if __GNUC__ >= 2
      obstack_alignment_mask (&newP->frch_obstack) = __alignof__ (fragS) - 1;
#endif
      newP->frch_frag_now = frag_alloc (&newP->frch_obstack);
      newP->frch_frag_now->fr_type = rs_fill;
      newP->frch_cfi_data = NULL;

      newP->frch_root = newP->frch_last = newP->frch_frag_now;

      *lastPP = newP;
      newP->frch_next = frcP;
      frcP = newP;
    }

  frchain_now = frcP;
  frag_now = frcP->frch_frag_now;

  assert (frchain_now->frch_last == frag_now);
}

/*
 *			subseg_set(segT, subsegT)
 *
 * If you attempt to change to the current subsegment, nothing happens.
 *
 * In:	segT, subsegT code for new subsegment.
 *	frag_now -> incomplete frag for current subsegment.
 *	If frag_now==NULL, then there is no old, incomplete frag, so
 *	the old frag is not closed off.
 *
 * Out:	now_subseg, now_seg updated.
 *	Frchain_now points to the (possibly new) struct frchain for this
 *	sub-segment.
 */

segT
subseg_get (const char *segname, int force_new)
{
  segT secptr;
  segment_info_type *seginfo;
  const char *now_seg_name = (now_seg
			      ? bfd_get_section_name (stdoutput, now_seg)
			      : 0);

  if (!force_new
      && now_seg_name
      && (now_seg_name == segname
	  || !strcmp (now_seg_name, segname)))
    return now_seg;

  if (!force_new)
    secptr = bfd_make_section_old_way (stdoutput, segname);
  else
    secptr = bfd_make_section_anyway (stdoutput, segname);

  seginfo = seg_info (secptr);
  if (! seginfo)
    {
      secptr->output_section = secptr;
      seginfo = xcalloc (1, sizeof (*seginfo));
      seginfo->bfd_section = secptr;
      (void) bfd_set_section_userdata (stdoutput, secptr, seginfo);
    }
  return secptr;
}

segT
subseg_new (const char *segname, subsegT subseg)
{
  segT secptr;

  secptr = subseg_get (segname, 0);
  subseg_set_rest (secptr, subseg);
  return secptr;
}

/* Like subseg_new, except a new section is always created, even if
   a section with that name already exists.  */
segT
subseg_force_new (const char *segname, subsegT subseg)
{
  segT secptr;

  secptr = subseg_get (segname, 1);
  subseg_set_rest (secptr, subseg);
  return secptr;
}

void
subseg_set (segT secptr, subsegT subseg)
{
  if (! (secptr == now_seg && subseg == now_subseg))
    subseg_set_rest (secptr, subseg);
  mri_common_symbol = NULL;
}

#ifndef obj_sec_sym_ok_for_reloc
#define obj_sec_sym_ok_for_reloc(SEC)	0
#endif

symbolS *
section_symbol (segT sec)
{
  segment_info_type *seginfo = seg_info (sec);
  symbolS *s;

  if (seginfo == 0)
    abort ();
  if (seginfo->sym)
    return seginfo->sym;

#ifndef EMIT_SECTION_SYMBOLS
#define EMIT_SECTION_SYMBOLS 1
#endif

  if (! EMIT_SECTION_SYMBOLS || symbol_table_frozen)
    {
      /* Here we know it won't be going into the symbol table.  */
      s = symbol_create (sec->symbol->name, sec, 0, &zero_address_frag);
    }
  else
    {
      segT seg;
      s = symbol_find (sec->symbol->name);
      /* We have to make sure it is the right symbol when we
	 have multiple sections with the same section name.  */
      if (s == NULL
	  || ((seg = S_GET_SEGMENT (s)) != sec
	      && seg != undefined_section))
	s = symbol_new (sec->symbol->name, sec, 0, &zero_address_frag);
      else if (seg == undefined_section)
	{
	  S_SET_SEGMENT (s, sec);
	  symbol_set_frag (s, &zero_address_frag);
	}
    }

  S_CLEAR_EXTERNAL (s);

  /* Use the BFD section symbol, if possible.  */
  if (obj_sec_sym_ok_for_reloc (sec))
    symbol_set_bfdsym (s, sec->symbol);
  else
    symbol_get_bfdsym (s)->flags |= BSF_SECTION_SYM;

  seginfo->sym = s;
  return s;
}

/* Return whether the specified segment is thought to hold text.  */

int
subseg_text_p (segT sec)
{
  return (bfd_get_section_flags (stdoutput, sec) & SEC_CODE) != 0;
}

/* Return non zero if SEC has at least one byte of data.  It is
   possible that we'll return zero even on a non-empty section because
   we don't know all the fragment types, and it is possible that an
   fr_fix == 0 one still contributes data.  Think of this as
   seg_definitely_not_empty_p.  */

int
seg_not_empty_p (segT sec ATTRIBUTE_UNUSED)
{
  segment_info_type *seginfo = seg_info (sec);
  frchainS *chain;
  fragS *frag;

  if (!seginfo)
    return 0;
  
  for (chain = seginfo->frchainP; chain; chain = chain->frch_next)
    {
      for (frag = chain->frch_root; frag; frag = frag->fr_next)
	if (frag->fr_fix)
	  return 1;
      if (obstack_next_free (&chain->frch_obstack)
	  != chain->frch_last->fr_literal)
	return 1;
    }
  return 0;
}

void
subsegs_print_statistics (FILE *file)
{
  frchainS *frchp;
  asection *s;

  fprintf (file, "frag chains:\n");
  for (s = stdoutput->sections; s; s = s->next)
    {
      segment_info_type *seginfo;

      /* Skip gas-internal sections.  */
      if (segment_name (s)[0] == '*')
	continue;

      seginfo = seg_info (s);
      if (!seginfo)
	continue;

      for (frchp = seginfo->frchainP; frchp; frchp = frchp->frch_next)
	{
	  int count = 0;
	  fragS *fragp;

	  for (fragp = frchp->frch_root; fragp; fragp = fragp->fr_next)
	    count++;

	  fprintf (file, "\n");
	  fprintf (file, "\t%p %-10s\t%10d frags\n", (void *) frchp,
		   segment_name (s), count);
	}
    }
}

/* end of subsegs.c */
