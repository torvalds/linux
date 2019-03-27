/* ehopt.c--optimize gcc exception frame information.
   Copyright 1998, 2000, 2001, 2003, 2005 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@cygnus.com>.

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

#include "as.h"
#include "subsegs.h"

/* We include this ELF file, even though we may not be assembling for
   ELF, since the exception frame information is always in a format
   derived from DWARF.  */

#include "elf/dwarf2.h"

/* Try to optimize gcc 2.8 exception frame information.

   Exception frame information is emitted for every function in the
   .eh_frame or .debug_frame sections.  Simple information for a function
   with no exceptions looks like this:

__FRAME_BEGIN__:
	.4byte	.LLCIE1	/ Length of Common Information Entry
.LSCIE1:
#if .eh_frame
	.4byte	0x0	/ CIE Identifier Tag
#elif .debug_frame
	.4byte	0xffffffff / CIE Identifier Tag
#endif
	.byte	0x1	/ CIE Version
	.byte	0x0	/ CIE Augmentation (none)
	.byte	0x1	/ ULEB128 0x1 (CIE Code Alignment Factor)
	.byte	0x7c	/ SLEB128 -4 (CIE Data Alignment Factor)
	.byte	0x8	/ CIE RA Column
	.byte	0xc	/ DW_CFA_def_cfa
	.byte	0x4	/ ULEB128 0x4
	.byte	0x4	/ ULEB128 0x4
	.byte	0x88	/ DW_CFA_offset, column 0x8
	.byte	0x1	/ ULEB128 0x1
	.align 4
.LECIE1:
	.set	.LLCIE1,.LECIE1-.LSCIE1	/ CIE Length Symbol
	.4byte	.LLFDE1	/ FDE Length
.LSFDE1:
	.4byte	.LSFDE1-__FRAME_BEGIN__	/ FDE CIE offset
	.4byte	.LFB1	/ FDE initial location
	.4byte	.LFE1-.LFB1	/ FDE address range
	.byte	0x4	/ DW_CFA_advance_loc4
	.4byte	.LCFI0-.LFB1
	.byte	0xe	/ DW_CFA_def_cfa_offset
	.byte	0x8	/ ULEB128 0x8
	.byte	0x85	/ DW_CFA_offset, column 0x5
	.byte	0x2	/ ULEB128 0x2
	.byte	0x4	/ DW_CFA_advance_loc4
	.4byte	.LCFI1-.LCFI0
	.byte	0xd	/ DW_CFA_def_cfa_register
	.byte	0x5	/ ULEB128 0x5
	.byte	0x4	/ DW_CFA_advance_loc4
	.4byte	.LCFI2-.LCFI1
	.byte	0x2e	/ DW_CFA_GNU_args_size
	.byte	0x4	/ ULEB128 0x4
	.byte	0x4	/ DW_CFA_advance_loc4
	.4byte	.LCFI3-.LCFI2
	.byte	0x2e	/ DW_CFA_GNU_args_size
	.byte	0x0	/ ULEB128 0x0
	.align 4
.LEFDE1:
	.set	.LLFDE1,.LEFDE1-.LSFDE1	/ FDE Length Symbol

   The immediate issue we can address in the assembler is the
   DW_CFA_advance_loc4 followed by a four byte value.  The value is
   the difference of two addresses in the function.  Since gcc does
   not know this value, it always uses four bytes.  We will know the
   value at the end of assembly, so we can do better.  */

struct cie_info
{
  unsigned code_alignment;
  int z_augmentation;
};

static int get_cie_info (struct cie_info *);

/* Extract information from the CIE.  */

static int
get_cie_info (struct cie_info *info)
{
  fragS *f;
  fixS *fix;
  int offset;
  char CIE_id;
  char augmentation[10];
  int iaug;
  int code_alignment = 0;

  /* We should find the CIE at the start of the section.  */

  f = seg_info (now_seg)->frchainP->frch_root;
  fix = seg_info (now_seg)->frchainP->fix_root;

  /* Look through the frags of the section to find the code alignment.  */

  /* First make sure that the CIE Identifier Tag is 0/-1.  */

  if (strcmp (segment_name (now_seg), ".debug_frame") == 0)
    CIE_id = (char)0xff;
  else
    CIE_id = 0;

  offset = 4;
  while (f != NULL && offset >= f->fr_fix)
    {
      offset -= f->fr_fix;
      f = f->fr_next;
    }
  if (f == NULL
      || f->fr_fix - offset < 4
      || f->fr_literal[offset] != CIE_id
      || f->fr_literal[offset + 1] != CIE_id
      || f->fr_literal[offset + 2] != CIE_id
      || f->fr_literal[offset + 3] != CIE_id)
    return 0;

  /* Next make sure the CIE version number is 1.  */

  offset += 4;
  while (f != NULL && offset >= f->fr_fix)
    {
      offset -= f->fr_fix;
      f = f->fr_next;
    }
  if (f == NULL
      || f->fr_fix - offset < 1
      || f->fr_literal[offset] != 1)
    return 0;

  /* Skip the augmentation (a null terminated string).  */

  iaug = 0;
  ++offset;
  while (1)
    {
      while (f != NULL && offset >= f->fr_fix)
	{
	  offset -= f->fr_fix;
	  f = f->fr_next;
	}
      if (f == NULL)
	return 0;

      while (offset < f->fr_fix && f->fr_literal[offset] != '\0')
	{
	  if ((size_t) iaug < (sizeof augmentation) - 1)
	    {
	      augmentation[iaug] = f->fr_literal[offset];
	      ++iaug;
	    }
	  ++offset;
	}
      if (offset < f->fr_fix)
	break;
    }
  ++offset;
  while (f != NULL && offset >= f->fr_fix)
    {
      offset -= f->fr_fix;
      f = f->fr_next;
    }
  if (f == NULL)
    return 0;

  augmentation[iaug] = '\0';
  if (augmentation[0] == '\0')
    {
      /* No augmentation.  */
    }
  else if (strcmp (augmentation, "eh") == 0)
    {
      /* We have to skip a pointer.  Unfortunately, we don't know how
	 large it is.  We find out by looking for a matching fixup.  */
      while (fix != NULL
	     && (fix->fx_frag != f || fix->fx_where != offset))
	fix = fix->fx_next;
      if (fix == NULL)
	offset += 4;
      else
	offset += fix->fx_size;
      while (f != NULL && offset >= f->fr_fix)
	{
	  offset -= f->fr_fix;
	  f = f->fr_next;
	}
      if (f == NULL)
	return 0;
    }
  else if (augmentation[0] != 'z')
    return 0;

  /* We're now at the code alignment factor, which is a ULEB128.  If
     it isn't a single byte, forget it.  */

  code_alignment = f->fr_literal[offset] & 0xff;
  if ((code_alignment & 0x80) != 0)
    code_alignment = 0;

  info->code_alignment = code_alignment;
  info->z_augmentation = (augmentation[0] == 'z');

  return 1;
}

/* This function is called from emit_expr.  It looks for cases which
   we can optimize.

   Rather than try to parse all this information as we read it, we
   look for a single byte DW_CFA_advance_loc4 followed by a 4 byte
   difference.  We turn that into a rs_cfa_advance frag, and handle
   those frags at the end of the assembly.  If the gcc output changes
   somewhat, this optimization may stop working.

   This function returns non-zero if it handled the expression and
   emit_expr should not do anything, or zero otherwise.  It can also
   change *EXP and *PNBYTES.  */

int
check_eh_frame (expressionS *exp, unsigned int *pnbytes)
{
  struct frame_data
  {
    enum frame_state
    {
      state_idle,
      state_saw_size,
      state_saw_cie_offset,
      state_saw_pc_begin,
      state_seeing_aug_size,
      state_skipping_aug,
      state_wait_loc4,
      state_saw_loc4,
      state_error,
    } state;

    int cie_info_ok;
    struct cie_info cie_info;

    symbolS *size_end_sym;
    fragS *loc4_frag;
    int loc4_fix;

    int aug_size;
    int aug_shift;
  };

  static struct frame_data eh_frame_data;
  static struct frame_data debug_frame_data;
  struct frame_data *d;

  /* Don't optimize.  */
  if (flag_traditional_format)
    return 0;

  /* Select the proper section data.  */
  if (strcmp (segment_name (now_seg), ".eh_frame") == 0)
    d = &eh_frame_data;
  else if (strcmp (segment_name (now_seg), ".debug_frame") == 0)
    d = &debug_frame_data;
  else
    return 0;

  if (d->state >= state_saw_size && S_IS_DEFINED (d->size_end_sym))
    {
      /* We have come to the end of the CIE or FDE.  See below where
         we set saw_size.  We must check this first because we may now
         be looking at the next size.  */
      d->state = state_idle;
    }

  switch (d->state)
    {
    case state_idle:
      if (*pnbytes == 4)
	{
	  /* This might be the size of the CIE or FDE.  We want to know
	     the size so that we don't accidentally optimize across an FDE
	     boundary.  We recognize the size in one of two forms: a
	     symbol which will later be defined as a difference, or a
	     subtraction of two symbols.  Either way, we can tell when we
	     are at the end of the FDE because the symbol becomes defined
	     (in the case of a subtraction, the end symbol, from which the
	     start symbol is being subtracted).  Other ways of describing
	     the size will not be optimized.  */
	  if ((exp->X_op == O_symbol || exp->X_op == O_subtract)
	      && ! S_IS_DEFINED (exp->X_add_symbol))
	    {
	      d->state = state_saw_size;
	      d->size_end_sym = exp->X_add_symbol;
	    }
	}
      break;

    case state_saw_size:
    case state_saw_cie_offset:
      /* Assume whatever form it appears in, it appears atomically.  */
      d->state += 1;
      break;

    case state_saw_pc_begin:
      /* Decide whether we should see an augmentation.  */
      if (! d->cie_info_ok
	  && ! (d->cie_info_ok = get_cie_info (&d->cie_info)))
	d->state = state_error;
      else if (d->cie_info.z_augmentation)
	{
	  d->state = state_seeing_aug_size;
	  d->aug_size = 0;
	  d->aug_shift = 0;
	}
      else
	d->state = state_wait_loc4;
      break;

    case state_seeing_aug_size:
      /* Bytes == -1 means this comes from an leb128 directive.  */
      if ((int)*pnbytes == -1 && exp->X_op == O_constant)
	{
	  d->aug_size = exp->X_add_number;
	  d->state = state_skipping_aug;
	}
      else if (*pnbytes == 1 && exp->X_op == O_constant)
	{
	  unsigned char byte = exp->X_add_number;
	  d->aug_size |= (byte & 0x7f) << d->aug_shift;
	  d->aug_shift += 7;
	  if ((byte & 0x80) == 0)
	    d->state = state_skipping_aug;
	}
      else
	d->state = state_error;
      if (d->state == state_skipping_aug && d->aug_size == 0)
	d->state = state_wait_loc4;
      break;

    case state_skipping_aug:
      if ((int)*pnbytes < 0)
	d->state = state_error;
      else
	{
	  int left = (d->aug_size -= *pnbytes);
	  if (left == 0)
	    d->state = state_wait_loc4;
	  else if (left < 0)
	    d->state = state_error;
	}
      break;

    case state_wait_loc4:
      if (*pnbytes == 1
	  && exp->X_op == O_constant
	  && exp->X_add_number == DW_CFA_advance_loc4)
	{
	  /* This might be a DW_CFA_advance_loc4.  Record the frag and the
	     position within the frag, so that we can change it later.  */
	  frag_grow (1);
	  d->state = state_saw_loc4;
	  d->loc4_frag = frag_now;
	  d->loc4_fix = frag_now_fix ();
	}
      break;

    case state_saw_loc4:
      d->state = state_wait_loc4;
      if (*pnbytes != 4)
	break;
      if (exp->X_op == O_constant)
	{
	  /* This is a case which we can optimize.  The two symbols being
	     subtracted were in the same frag and the expression was
	     reduced to a constant.  We can do the optimization entirely
	     in this function.  */
	  if (d->cie_info.code_alignment > 0
	      && exp->X_add_number % d->cie_info.code_alignment == 0
	      && exp->X_add_number / d->cie_info.code_alignment < 0x40)
	    {
	      d->loc4_frag->fr_literal[d->loc4_fix]
		= DW_CFA_advance_loc
		  | (exp->X_add_number / d->cie_info.code_alignment);
	      /* No more bytes needed.  */
	      return 1;
	    }
	  else if (exp->X_add_number < 0x100)
	    {
	      d->loc4_frag->fr_literal[d->loc4_fix] = DW_CFA_advance_loc1;
	      *pnbytes = 1;
	    }
	  else if (exp->X_add_number < 0x10000)
	    {
	      d->loc4_frag->fr_literal[d->loc4_fix] = DW_CFA_advance_loc2;
	      *pnbytes = 2;
	    }
	}
      else if (exp->X_op == O_subtract)
	{
	  /* This is a case we can optimize.  The expression was not
	     reduced, so we can not finish the optimization until the end
	     of the assembly.  We set up a variant frag which we handle
	     later.  */
	  int fr_subtype;

	  if (d->cie_info.code_alignment > 0)
	    fr_subtype = d->cie_info.code_alignment << 3;
	  else
	    fr_subtype = 0;

	  frag_var (rs_cfa, 4, 0, fr_subtype, make_expr_symbol (exp),
		    d->loc4_fix, (char *) d->loc4_frag);
	  return 1;
	}
      break;

    case state_error:
      /* Just skipping everything.  */
      break;
    }

  return 0;
}

/* The function estimates the size of a rs_cfa variant frag based on
   the current values of the symbols.  It is called before the
   relaxation loop.  We set fr_subtype{0:2} to the expected length.  */

int
eh_frame_estimate_size_before_relax (fragS *frag)
{
  offsetT diff;
  int ca = frag->fr_subtype >> 3;
  int ret;

  diff = resolve_symbol_value (frag->fr_symbol);

  if (ca > 0 && diff % ca == 0 && diff / ca < 0x40)
    ret = 0;
  else if (diff < 0x100)
    ret = 1;
  else if (diff < 0x10000)
    ret = 2;
  else
    ret = 4;

  frag->fr_subtype = (frag->fr_subtype & ~7) | ret;

  return ret;
}

/* This function relaxes a rs_cfa variant frag based on the current
   values of the symbols.  fr_subtype{0:2} is the current length of
   the frag.  This returns the change in frag length.  */

int
eh_frame_relax_frag (fragS *frag)
{
  int oldsize, newsize;

  oldsize = frag->fr_subtype & 7;
  newsize = eh_frame_estimate_size_before_relax (frag);
  return newsize - oldsize;
}

/* This function converts a rs_cfa variant frag into a normal fill
   frag.  This is called after all relaxation has been done.
   fr_subtype{0:2} will be the desired length of the frag.  */

void
eh_frame_convert_frag (fragS *frag)
{
  offsetT diff;
  fragS *loc4_frag;
  int loc4_fix;

  loc4_frag = (fragS *) frag->fr_opcode;
  loc4_fix = (int) frag->fr_offset;

  diff = resolve_symbol_value (frag->fr_symbol);

  switch (frag->fr_subtype & 7)
    {
    case 0:
      {
	int ca = frag->fr_subtype >> 3;
	assert (ca > 0 && diff % ca == 0 && diff / ca < 0x40);
	loc4_frag->fr_literal[loc4_fix] = DW_CFA_advance_loc | (diff / ca);
      }
      break;

    case 1:
      assert (diff < 0x100);
      loc4_frag->fr_literal[loc4_fix] = DW_CFA_advance_loc1;
      frag->fr_literal[frag->fr_fix] = diff;
      break;

    case 2:
      assert (diff < 0x10000);
      loc4_frag->fr_literal[loc4_fix] = DW_CFA_advance_loc2;
      md_number_to_chars (frag->fr_literal + frag->fr_fix, diff, 2);
      break;

    default:
      md_number_to_chars (frag->fr_literal + frag->fr_fix, diff, 4);
      break;
    }

  frag->fr_fix += frag->fr_subtype & 7;
  frag->fr_type = rs_fill;
  frag->fr_subtype = 0;
  frag->fr_offset = 0;
}
