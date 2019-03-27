/* C-compiler utilities for types and variables storage layout
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1996, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "output.h"
#include "toplev.h"
#include "ggc.h"
#include "target.h"
#include "langhooks.h"
#include "regs.h"
#include "params.h"

/* Data type for the expressions representing sizes of data types.
   It is the first integer type laid out.  */
tree sizetype_tab[(int) TYPE_KIND_LAST];

/* If nonzero, this is an upper limit on alignment of structure fields.
   The value is measured in bits.  */
unsigned int maximum_field_alignment = TARGET_DEFAULT_PACK_STRUCT * BITS_PER_UNIT;
/* ... and its original value in bytes, specified via -fpack-struct=<value>.  */
unsigned int initial_max_fld_align = TARGET_DEFAULT_PACK_STRUCT;

/* Nonzero if all REFERENCE_TYPEs are internal and hence should be
   allocated in Pmode, not ptr_mode.   Set only by internal_reference_types
   called only by a front end.  */
static int reference_types_internal = 0;

static void finalize_record_size (record_layout_info);
static void finalize_type_size (tree);
static void place_union_field (record_layout_info, tree);
#if defined (PCC_BITFIELD_TYPE_MATTERS) || defined (BITFIELD_NBYTES_LIMITED)
static int excess_unit_span (HOST_WIDE_INT, HOST_WIDE_INT, HOST_WIDE_INT,
			     HOST_WIDE_INT, tree);
#endif
extern void debug_rli (record_layout_info);

/* SAVE_EXPRs for sizes of types and decls, waiting to be expanded.  */

static GTY(()) tree pending_sizes;

/* Show that REFERENCE_TYPES are internal and should be Pmode.  Called only
   by front end.  */

void
internal_reference_types (void)
{
  reference_types_internal = 1;
}

/* Get a list of all the objects put on the pending sizes list.  */

tree
get_pending_sizes (void)
{
  tree chain = pending_sizes;

  pending_sizes = 0;
  return chain;
}

/* Add EXPR to the pending sizes list.  */

void
put_pending_size (tree expr)
{
  /* Strip any simple arithmetic from EXPR to see if it has an underlying
     SAVE_EXPR.  */
  expr = skip_simple_arithmetic (expr);

  if (TREE_CODE (expr) == SAVE_EXPR)
    pending_sizes = tree_cons (NULL_TREE, expr, pending_sizes);
}

/* Put a chain of objects into the pending sizes list, which must be
   empty.  */

void
put_pending_sizes (tree chain)
{
  gcc_assert (!pending_sizes);
  pending_sizes = chain;
}

/* Given a size SIZE that may not be a constant, return a SAVE_EXPR
   to serve as the actual size-expression for a type or decl.  */

tree
variable_size (tree size)
{
  tree save;

  /* If the language-processor is to take responsibility for variable-sized
     items (e.g., languages which have elaboration procedures like Ada),
     just return SIZE unchanged.  Likewise for self-referential sizes and
     constant sizes.  */
  if (TREE_CONSTANT (size)
      || lang_hooks.decls.global_bindings_p () < 0
      || CONTAINS_PLACEHOLDER_P (size))
    return size;

  size = save_expr (size);

  /* If an array with a variable number of elements is declared, and
     the elements require destruction, we will emit a cleanup for the
     array.  That cleanup is run both on normal exit from the block
     and in the exception-handler for the block.  Normally, when code
     is used in both ordinary code and in an exception handler it is
     `unsaved', i.e., all SAVE_EXPRs are recalculated.  However, we do
     not wish to do that here; the array-size is the same in both
     places.  */
  save = skip_simple_arithmetic (size);

  if (cfun && cfun->x_dont_save_pending_sizes_p)
    /* The front-end doesn't want us to keep a list of the expressions
       that determine sizes for variable size objects.  Trust it.  */
    return size;

  if (lang_hooks.decls.global_bindings_p ())
    {
      if (TREE_CONSTANT (size))
	error ("type size can%'t be explicitly evaluated");
      else
	error ("variable-size type declared outside of any function");

      return size_one_node;
    }

  put_pending_size (save);

  return size;
}

#ifndef MAX_FIXED_MODE_SIZE
#define MAX_FIXED_MODE_SIZE GET_MODE_BITSIZE (DImode)
#endif

/* Return the machine mode to use for a nonscalar of SIZE bits.  The
   mode must be in class CLASS, and have exactly that many value bits;
   it may have padding as well.  If LIMIT is nonzero, modes of wider
   than MAX_FIXED_MODE_SIZE will not be used.  */

enum machine_mode
mode_for_size (unsigned int size, enum mode_class class, int limit)
{
  enum machine_mode mode;

  if (limit && size > MAX_FIXED_MODE_SIZE)
    return BLKmode;

  /* Get the first mode which has this size, in the specified class.  */
  for (mode = GET_CLASS_NARROWEST_MODE (class); mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if (GET_MODE_PRECISION (mode) == size)
      return mode;

  return BLKmode;
}

/* Similar, except passed a tree node.  */

enum machine_mode
mode_for_size_tree (tree size, enum mode_class class, int limit)
{
  unsigned HOST_WIDE_INT uhwi;
  unsigned int ui;

  if (!host_integerp (size, 1))
    return BLKmode;
  uhwi = tree_low_cst (size, 1);
  ui = uhwi;
  if (uhwi != ui)
    return BLKmode;
  return mode_for_size (ui, class, limit);
}

/* Similar, but never return BLKmode; return the narrowest mode that
   contains at least the requested number of value bits.  */

enum machine_mode
smallest_mode_for_size (unsigned int size, enum mode_class class)
{
  enum machine_mode mode;

  /* Get the first mode which has at least this size, in the
     specified class.  */
  for (mode = GET_CLASS_NARROWEST_MODE (class); mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if (GET_MODE_PRECISION (mode) >= size)
      return mode;

  gcc_unreachable ();
}

/* Find an integer mode of the exact same size, or BLKmode on failure.  */

enum machine_mode
int_mode_for_mode (enum machine_mode mode)
{
  switch (GET_MODE_CLASS (mode))
    {
    case MODE_INT:
    case MODE_PARTIAL_INT:
      break;

    case MODE_COMPLEX_INT:
    case MODE_COMPLEX_FLOAT:
    case MODE_FLOAT:
    case MODE_DECIMAL_FLOAT:
    case MODE_VECTOR_INT:
    case MODE_VECTOR_FLOAT:
      mode = mode_for_size (GET_MODE_BITSIZE (mode), MODE_INT, 0);
      break;

    case MODE_RANDOM:
      if (mode == BLKmode)
	break;

      /* ... fall through ...  */

    case MODE_CC:
    default:
      gcc_unreachable ();
    }

  return mode;
}

/* Return the alignment of MODE. This will be bounded by 1 and
   BIGGEST_ALIGNMENT.  */

unsigned int
get_mode_alignment (enum machine_mode mode)
{
  return MIN (BIGGEST_ALIGNMENT, MAX (1, mode_base_align[mode]*BITS_PER_UNIT));
}


/* Subroutine of layout_decl: Force alignment required for the data type.
   But if the decl itself wants greater alignment, don't override that.  */

static inline void
do_type_align (tree type, tree decl)
{
  if (TYPE_ALIGN (type) > DECL_ALIGN (decl))
    {
      DECL_ALIGN (decl) = TYPE_ALIGN (type);
      if (TREE_CODE (decl) == FIELD_DECL)
	DECL_USER_ALIGN (decl) = TYPE_USER_ALIGN (type);
    }
}

/* Set the size, mode and alignment of a ..._DECL node.
   TYPE_DECL does need this for C++.
   Note that LABEL_DECL and CONST_DECL nodes do not need this,
   and FUNCTION_DECL nodes have them set up in a special (and simple) way.
   Don't call layout_decl for them.

   KNOWN_ALIGN is the amount of alignment we can assume this
   decl has with no special effort.  It is relevant only for FIELD_DECLs
   and depends on the previous fields.
   All that matters about KNOWN_ALIGN is which powers of 2 divide it.
   If KNOWN_ALIGN is 0, it means, "as much alignment as you like":
   the record will be aligned to suit.  */

void
layout_decl (tree decl, unsigned int known_align)
{
  tree type = TREE_TYPE (decl);
  enum tree_code code = TREE_CODE (decl);
  rtx rtl = NULL_RTX;

  if (code == CONST_DECL)
    return;

  gcc_assert (code == VAR_DECL || code == PARM_DECL || code == RESULT_DECL
	      || code == TYPE_DECL ||code == FIELD_DECL);

  rtl = DECL_RTL_IF_SET (decl);

  if (type == error_mark_node)
    type = void_type_node;

  /* Usually the size and mode come from the data type without change,
     however, the front-end may set the explicit width of the field, so its
     size may not be the same as the size of its type.  This happens with
     bitfields, of course (an `int' bitfield may be only 2 bits, say), but it
     also happens with other fields.  For example, the C++ front-end creates
     zero-sized fields corresponding to empty base classes, and depends on
     layout_type setting DECL_FIELD_BITPOS correctly for the field.  Set the
     size in bytes from the size in bits.  If we have already set the mode,
     don't set it again since we can be called twice for FIELD_DECLs.  */

  DECL_UNSIGNED (decl) = TYPE_UNSIGNED (type);
  if (DECL_MODE (decl) == VOIDmode)
    DECL_MODE (decl) = TYPE_MODE (type);

  if (DECL_SIZE (decl) == 0)
    {
      DECL_SIZE (decl) = TYPE_SIZE (type);
      DECL_SIZE_UNIT (decl) = TYPE_SIZE_UNIT (type);
    }
  else if (DECL_SIZE_UNIT (decl) == 0)
    DECL_SIZE_UNIT (decl)
      = fold_convert (sizetype, size_binop (CEIL_DIV_EXPR, DECL_SIZE (decl),
					    bitsize_unit_node));

  if (code != FIELD_DECL)
    /* For non-fields, update the alignment from the type.  */
    do_type_align (type, decl);
  else
    /* For fields, it's a bit more complicated...  */
    {
      bool old_user_align = DECL_USER_ALIGN (decl);
      bool zero_bitfield = false;
      bool packed_p = DECL_PACKED (decl);
      unsigned int mfa;

      if (DECL_BIT_FIELD (decl))
	{
	  DECL_BIT_FIELD_TYPE (decl) = type;

	  /* A zero-length bit-field affects the alignment of the next
	     field.  In essence such bit-fields are not influenced by
	     any packing due to #pragma pack or attribute packed.  */
	  if (integer_zerop (DECL_SIZE (decl))
	      && ! targetm.ms_bitfield_layout_p (DECL_FIELD_CONTEXT (decl)))
	    {
	      zero_bitfield = true;
	      packed_p = false;
#ifdef PCC_BITFIELD_TYPE_MATTERS
	      if (PCC_BITFIELD_TYPE_MATTERS)
		do_type_align (type, decl);
	      else
#endif
		{
#ifdef EMPTY_FIELD_BOUNDARY
		  if (EMPTY_FIELD_BOUNDARY > DECL_ALIGN (decl))
		    {
		      DECL_ALIGN (decl) = EMPTY_FIELD_BOUNDARY;
		      DECL_USER_ALIGN (decl) = 0;
		    }
#endif
		}
	    }

	  /* See if we can use an ordinary integer mode for a bit-field.
	     Conditions are: a fixed size that is correct for another mode
	     and occupying a complete byte or bytes on proper boundary.  */
	  if (TYPE_SIZE (type) != 0
	      && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
	      && GET_MODE_CLASS (TYPE_MODE (type)) == MODE_INT)
	    {
	      enum machine_mode xmode
		= mode_for_size_tree (DECL_SIZE (decl), MODE_INT, 1);

	      if (xmode != BLKmode
		  && (known_align == 0
		      || known_align >= GET_MODE_ALIGNMENT (xmode)))
		{
		  DECL_ALIGN (decl) = MAX (GET_MODE_ALIGNMENT (xmode),
					   DECL_ALIGN (decl));
		  DECL_MODE (decl) = xmode;
		  DECL_BIT_FIELD (decl) = 0;
		}
	    }

	  /* Turn off DECL_BIT_FIELD if we won't need it set.  */
	  if (TYPE_MODE (type) == BLKmode && DECL_MODE (decl) == BLKmode
	      && known_align >= TYPE_ALIGN (type)
	      && DECL_ALIGN (decl) >= TYPE_ALIGN (type))
	    DECL_BIT_FIELD (decl) = 0;
	}
      else if (packed_p && DECL_USER_ALIGN (decl))
	/* Don't touch DECL_ALIGN.  For other packed fields, go ahead and
	   round up; we'll reduce it again below.  We want packing to
	   supersede USER_ALIGN inherited from the type, but defer to
	   alignment explicitly specified on the field decl.  */;
      else
	do_type_align (type, decl);

      /* If the field is of variable size, we can't misalign it since we
	 have no way to make a temporary to align the result.  But this
	 isn't an issue if the decl is not addressable.  Likewise if it
	 is of unknown size.

	 Note that do_type_align may set DECL_USER_ALIGN, so we need to
	 check old_user_align instead.  */
      if (packed_p
	  && !old_user_align
	  && (DECL_NONADDRESSABLE_P (decl)
	      || DECL_SIZE_UNIT (decl) == 0
	      || TREE_CODE (DECL_SIZE_UNIT (decl)) == INTEGER_CST))
	DECL_ALIGN (decl) = MIN (DECL_ALIGN (decl), BITS_PER_UNIT);

      if (! packed_p && ! DECL_USER_ALIGN (decl))
	{
	  /* Some targets (i.e. i386, VMS) limit struct field alignment
	     to a lower boundary than alignment of variables unless
	     it was overridden by attribute aligned.  */
#ifdef BIGGEST_FIELD_ALIGNMENT
	  DECL_ALIGN (decl)
	    = MIN (DECL_ALIGN (decl), (unsigned) BIGGEST_FIELD_ALIGNMENT);
#endif
#ifdef ADJUST_FIELD_ALIGN
	  DECL_ALIGN (decl) = ADJUST_FIELD_ALIGN (decl, DECL_ALIGN (decl));
#endif
	}

      if (zero_bitfield)
        mfa = initial_max_fld_align * BITS_PER_UNIT;
      else
	mfa = maximum_field_alignment;
      /* Should this be controlled by DECL_USER_ALIGN, too?  */
      if (mfa != 0)
	DECL_ALIGN (decl) = MIN (DECL_ALIGN (decl), mfa);
    }

  /* Evaluate nonconstant size only once, either now or as soon as safe.  */
  if (DECL_SIZE (decl) != 0 && TREE_CODE (DECL_SIZE (decl)) != INTEGER_CST)
    DECL_SIZE (decl) = variable_size (DECL_SIZE (decl));
  if (DECL_SIZE_UNIT (decl) != 0
      && TREE_CODE (DECL_SIZE_UNIT (decl)) != INTEGER_CST)
    DECL_SIZE_UNIT (decl) = variable_size (DECL_SIZE_UNIT (decl));

  /* If requested, warn about definitions of large data objects.  */
  if (warn_larger_than
      && (code == VAR_DECL || code == PARM_DECL)
      && ! DECL_EXTERNAL (decl))
    {
      tree size = DECL_SIZE_UNIT (decl);

      if (size != 0 && TREE_CODE (size) == INTEGER_CST
	  && compare_tree_int (size, larger_than_size) > 0)
	{
	  int size_as_int = TREE_INT_CST_LOW (size);

	  if (compare_tree_int (size, size_as_int) == 0)
	    warning (0, "size of %q+D is %d bytes", decl, size_as_int);
	  else
	    warning (0, "size of %q+D is larger than %wd bytes",
                     decl, larger_than_size);
	}
    }

  /* If the RTL was already set, update its mode and mem attributes.  */
  if (rtl)
    {
      PUT_MODE (rtl, DECL_MODE (decl));
      SET_DECL_RTL (decl, 0);
      set_mem_attributes (rtl, decl, 1);
      SET_DECL_RTL (decl, rtl);
    }
}

/* Given a VAR_DECL, PARM_DECL or RESULT_DECL, clears the results of
   a previous call to layout_decl and calls it again.  */

void
relayout_decl (tree decl)
{
  DECL_SIZE (decl) = DECL_SIZE_UNIT (decl) = 0;
  DECL_MODE (decl) = VOIDmode;
  if (!DECL_USER_ALIGN (decl))
    DECL_ALIGN (decl) = 0;
  SET_DECL_RTL (decl, 0);

  layout_decl (decl, 0);
}

/* Hook for a front-end function that can modify the record layout as needed
   immediately before it is finalized.  */

static void (*lang_adjust_rli) (record_layout_info) = 0;

void
set_lang_adjust_rli (void (*f) (record_layout_info))
{
  lang_adjust_rli = f;
}

/* Begin laying out type T, which may be a RECORD_TYPE, UNION_TYPE, or
   QUAL_UNION_TYPE.  Return a pointer to a struct record_layout_info which
   is to be passed to all other layout functions for this record.  It is the
   responsibility of the caller to call `free' for the storage returned.
   Note that garbage collection is not permitted until we finish laying
   out the record.  */

record_layout_info
start_record_layout (tree t)
{
  record_layout_info rli = xmalloc (sizeof (struct record_layout_info_s));

  rli->t = t;

  /* If the type has a minimum specified alignment (via an attribute
     declaration, for example) use it -- otherwise, start with a
     one-byte alignment.  */
  rli->record_align = MAX (BITS_PER_UNIT, TYPE_ALIGN (t));
  rli->unpacked_align = rli->record_align;
  rli->offset_align = MAX (rli->record_align, BIGGEST_ALIGNMENT);

#ifdef STRUCTURE_SIZE_BOUNDARY
  /* Packed structures don't need to have minimum size.  */
  if (! TYPE_PACKED (t))
    rli->record_align = MAX (rli->record_align, (unsigned) STRUCTURE_SIZE_BOUNDARY);
#endif

  rli->offset = size_zero_node;
  rli->bitpos = bitsize_zero_node;
  rli->prev_field = 0;
  rli->pending_statics = 0;
  rli->packed_maybe_necessary = 0;
  rli->remaining_in_alignment = 0;

  return rli;
}

/* These four routines perform computations that convert between
   the offset/bitpos forms and byte and bit offsets.  */

tree
bit_from_pos (tree offset, tree bitpos)
{
  return size_binop (PLUS_EXPR, bitpos,
		     size_binop (MULT_EXPR,
				 fold_convert (bitsizetype, offset),
				 bitsize_unit_node));
}

tree
byte_from_pos (tree offset, tree bitpos)
{
  return size_binop (PLUS_EXPR, offset,
		     fold_convert (sizetype,
				   size_binop (TRUNC_DIV_EXPR, bitpos,
					       bitsize_unit_node)));
}

void
pos_from_bit (tree *poffset, tree *pbitpos, unsigned int off_align,
	      tree pos)
{
  *poffset = size_binop (MULT_EXPR,
			 fold_convert (sizetype,
				       size_binop (FLOOR_DIV_EXPR, pos,
						   bitsize_int (off_align))),
			 size_int (off_align / BITS_PER_UNIT));
  *pbitpos = size_binop (FLOOR_MOD_EXPR, pos, bitsize_int (off_align));
}

/* Given a pointer to bit and byte offsets and an offset alignment,
   normalize the offsets so they are within the alignment.  */

void
normalize_offset (tree *poffset, tree *pbitpos, unsigned int off_align)
{
  /* If the bit position is now larger than it should be, adjust it
     downwards.  */
  if (compare_tree_int (*pbitpos, off_align) >= 0)
    {
      tree extra_aligns = size_binop (FLOOR_DIV_EXPR, *pbitpos,
				      bitsize_int (off_align));

      *poffset
	= size_binop (PLUS_EXPR, *poffset,
		      size_binop (MULT_EXPR,
				  fold_convert (sizetype, extra_aligns),
				  size_int (off_align / BITS_PER_UNIT)));

      *pbitpos
	= size_binop (FLOOR_MOD_EXPR, *pbitpos, bitsize_int (off_align));
    }
}

/* Print debugging information about the information in RLI.  */

void
debug_rli (record_layout_info rli)
{
  print_node_brief (stderr, "type", rli->t, 0);
  print_node_brief (stderr, "\noffset", rli->offset, 0);
  print_node_brief (stderr, " bitpos", rli->bitpos, 0);

  fprintf (stderr, "\naligns: rec = %u, unpack = %u, off = %u\n",
	   rli->record_align, rli->unpacked_align,
	   rli->offset_align);

  /* The ms_struct code is the only that uses this.  */
  if (targetm.ms_bitfield_layout_p (rli->t))
    fprintf (stderr, "remaining in alignment = %u\n", rli->remaining_in_alignment);

  if (rli->packed_maybe_necessary)
    fprintf (stderr, "packed may be necessary\n");

  if (rli->pending_statics)
    {
      fprintf (stderr, "pending statics:\n");
      debug_tree (rli->pending_statics);
    }
}

/* Given an RLI with a possibly-incremented BITPOS, adjust OFFSET and
   BITPOS if necessary to keep BITPOS below OFFSET_ALIGN.  */

void
normalize_rli (record_layout_info rli)
{
  normalize_offset (&rli->offset, &rli->bitpos, rli->offset_align);
}

/* Returns the size in bytes allocated so far.  */

tree
rli_size_unit_so_far (record_layout_info rli)
{
  return byte_from_pos (rli->offset, rli->bitpos);
}

/* Returns the size in bits allocated so far.  */

tree
rli_size_so_far (record_layout_info rli)
{
  return bit_from_pos (rli->offset, rli->bitpos);
}

/* FIELD is about to be added to RLI->T.  The alignment (in bits) of
   the next available location within the record is given by KNOWN_ALIGN.
   Update the variable alignment fields in RLI, and return the alignment
   to give the FIELD.  */

unsigned int
update_alignment_for_field (record_layout_info rli, tree field,
			    unsigned int known_align)
{
  /* The alignment required for FIELD.  */
  unsigned int desired_align;
  /* The type of this field.  */
  tree type = TREE_TYPE (field);
  /* True if the field was explicitly aligned by the user.  */
  bool user_align;
  bool is_bitfield;

  /* Do not attempt to align an ERROR_MARK node */
  if (TREE_CODE (type) == ERROR_MARK)
    return 0;

  /* Lay out the field so we know what alignment it needs.  */
  layout_decl (field, known_align);
  desired_align = DECL_ALIGN (field);
  user_align = DECL_USER_ALIGN (field);

  is_bitfield = (type != error_mark_node
		 && DECL_BIT_FIELD_TYPE (field)
		 && ! integer_zerop (TYPE_SIZE (type)));

  /* Record must have at least as much alignment as any field.
     Otherwise, the alignment of the field within the record is
     meaningless.  */
  if (targetm.ms_bitfield_layout_p (rli->t))
    {
      /* Here, the alignment of the underlying type of a bitfield can
	 affect the alignment of a record; even a zero-sized field
	 can do this.  The alignment should be to the alignment of
	 the type, except that for zero-size bitfields this only
	 applies if there was an immediately prior, nonzero-size
	 bitfield.  (That's the way it is, experimentally.) */
      if ((!is_bitfield && !DECL_PACKED (field))
	  || (!integer_zerop (DECL_SIZE (field))
	      ? !DECL_PACKED (field)
	      : (rli->prev_field
		 && DECL_BIT_FIELD_TYPE (rli->prev_field)
		 && ! integer_zerop (DECL_SIZE (rli->prev_field)))))
	{
	  unsigned int type_align = TYPE_ALIGN (type);
	  type_align = MAX (type_align, desired_align);
	  if (maximum_field_alignment != 0)
	    type_align = MIN (type_align, maximum_field_alignment);
	  rli->record_align = MAX (rli->record_align, type_align);
	  rli->unpacked_align = MAX (rli->unpacked_align, TYPE_ALIGN (type));
	}
    }
#ifdef PCC_BITFIELD_TYPE_MATTERS
  else if (is_bitfield && PCC_BITFIELD_TYPE_MATTERS)
    {
      /* Named bit-fields cause the entire structure to have the
	 alignment implied by their type.  Some targets also apply the same
	 rules to unnamed bitfields.  */
      if (DECL_NAME (field) != 0
	  || targetm.align_anon_bitfield ())
	{
	  unsigned int type_align = TYPE_ALIGN (type);

#ifdef ADJUST_FIELD_ALIGN
	  if (! TYPE_USER_ALIGN (type))
	    type_align = ADJUST_FIELD_ALIGN (field, type_align);
#endif

	  /* Targets might chose to handle unnamed and hence possibly
	     zero-width bitfield.  Those are not influenced by #pragmas
	     or packed attributes.  */
	  if (integer_zerop (DECL_SIZE (field)))
	    {
	      if (initial_max_fld_align)
	        type_align = MIN (type_align,
				  initial_max_fld_align * BITS_PER_UNIT);
	    }
	  else if (maximum_field_alignment != 0)
	    type_align = MIN (type_align, maximum_field_alignment);
	  else if (DECL_PACKED (field))
	    type_align = MIN (type_align, BITS_PER_UNIT);

	  /* The alignment of the record is increased to the maximum
	     of the current alignment, the alignment indicated on the
	     field (i.e., the alignment specified by an __aligned__
	     attribute), and the alignment indicated by the type of
	     the field.  */
	  rli->record_align = MAX (rli->record_align, desired_align);
	  rli->record_align = MAX (rli->record_align, type_align);

	  if (warn_packed)
	    rli->unpacked_align = MAX (rli->unpacked_align, TYPE_ALIGN (type));
	  user_align |= TYPE_USER_ALIGN (type);
	}
    }
#endif
  else
    {
      rli->record_align = MAX (rli->record_align, desired_align);
      rli->unpacked_align = MAX (rli->unpacked_align, TYPE_ALIGN (type));
    }

  TYPE_USER_ALIGN (rli->t) |= user_align;

  return desired_align;
}

/* Called from place_field to handle unions.  */

static void
place_union_field (record_layout_info rli, tree field)
{
  update_alignment_for_field (rli, field, /*known_align=*/0);

  DECL_FIELD_OFFSET (field) = size_zero_node;
  DECL_FIELD_BIT_OFFSET (field) = bitsize_zero_node;
  SET_DECL_OFFSET_ALIGN (field, BIGGEST_ALIGNMENT);

  /* If this is an ERROR_MARK return *after* having set the
     field at the start of the union. This helps when parsing
     invalid fields. */
  if (TREE_CODE (TREE_TYPE (field)) == ERROR_MARK)
    return;

  /* We assume the union's size will be a multiple of a byte so we don't
     bother with BITPOS.  */
  if (TREE_CODE (rli->t) == UNION_TYPE)
    rli->offset = size_binop (MAX_EXPR, rli->offset, DECL_SIZE_UNIT (field));
  else if (TREE_CODE (rli->t) == QUAL_UNION_TYPE)
    rli->offset = fold_build3 (COND_EXPR, sizetype,
			       DECL_QUALIFIER (field),
			       DECL_SIZE_UNIT (field), rli->offset);
}

#if defined (PCC_BITFIELD_TYPE_MATTERS) || defined (BITFIELD_NBYTES_LIMITED)
/* A bitfield of SIZE with a required access alignment of ALIGN is allocated
   at BYTE_OFFSET / BIT_OFFSET.  Return nonzero if the field would span more
   units of alignment than the underlying TYPE.  */
static int
excess_unit_span (HOST_WIDE_INT byte_offset, HOST_WIDE_INT bit_offset,
		  HOST_WIDE_INT size, HOST_WIDE_INT align, tree type)
{
  /* Note that the calculation of OFFSET might overflow; we calculate it so
     that we still get the right result as long as ALIGN is a power of two.  */
  unsigned HOST_WIDE_INT offset = byte_offset * BITS_PER_UNIT + bit_offset;

  offset = offset % align;
  return ((offset + size + align - 1) / align
	  > ((unsigned HOST_WIDE_INT) tree_low_cst (TYPE_SIZE (type), 1)
	     / align));
}
#endif

/* RLI contains information about the layout of a RECORD_TYPE.  FIELD
   is a FIELD_DECL to be added after those fields already present in
   T.  (FIELD is not actually added to the TYPE_FIELDS list here;
   callers that desire that behavior must manually perform that step.)  */

void
place_field (record_layout_info rli, tree field)
{
  /* The alignment required for FIELD.  */
  unsigned int desired_align;
  /* The alignment FIELD would have if we just dropped it into the
     record as it presently stands.  */
  unsigned int known_align;
  unsigned int actual_align;
  /* The type of this field.  */
  tree type = TREE_TYPE (field);

  gcc_assert (TREE_CODE (field) != ERROR_MARK);

  /* If FIELD is static, then treat it like a separate variable, not
     really like a structure field.  If it is a FUNCTION_DECL, it's a
     method.  In both cases, all we do is lay out the decl, and we do
     it *after* the record is laid out.  */
  if (TREE_CODE (field) == VAR_DECL)
    {
      rli->pending_statics = tree_cons (NULL_TREE, field,
					rli->pending_statics);
      return;
    }

  /* Enumerators and enum types which are local to this class need not
     be laid out.  Likewise for initialized constant fields.  */
  else if (TREE_CODE (field) != FIELD_DECL)
    return;

  /* Unions are laid out very differently than records, so split
     that code off to another function.  */
  else if (TREE_CODE (rli->t) != RECORD_TYPE)
    {
      place_union_field (rli, field);
      return;
    }

  else if (TREE_CODE (type) == ERROR_MARK)
    {
      /* Place this field at the current allocation position, so we
	 maintain monotonicity.  */
      DECL_FIELD_OFFSET (field) = rli->offset;
      DECL_FIELD_BIT_OFFSET (field) = rli->bitpos;
      SET_DECL_OFFSET_ALIGN (field, rli->offset_align);
      return;
    }

  /* Work out the known alignment so far.  Note that A & (-A) is the
     value of the least-significant bit in A that is one.  */
  if (! integer_zerop (rli->bitpos))
    known_align = (tree_low_cst (rli->bitpos, 1)
		   & - tree_low_cst (rli->bitpos, 1));
  else if (integer_zerop (rli->offset))
    known_align = 0;
  else if (host_integerp (rli->offset, 1))
    known_align = (BITS_PER_UNIT
		   * (tree_low_cst (rli->offset, 1)
		      & - tree_low_cst (rli->offset, 1)));
  else
    known_align = rli->offset_align;

  desired_align = update_alignment_for_field (rli, field, known_align);
  if (known_align == 0)
    known_align = MAX (BIGGEST_ALIGNMENT, rli->record_align);

  if (warn_packed && DECL_PACKED (field))
    {
      if (known_align >= TYPE_ALIGN (type))
	{
	  if (TYPE_ALIGN (type) > desired_align)
	    {
	      if (STRICT_ALIGNMENT)
		warning (OPT_Wattributes, "packed attribute causes "
                         "inefficient alignment for %q+D", field);
	      else
		warning (OPT_Wattributes, "packed attribute is "
			 "unnecessary for %q+D", field);
	    }
	}
      else
	rli->packed_maybe_necessary = 1;
    }

  /* Does this field automatically have alignment it needs by virtue
     of the fields that precede it and the record's own alignment?
     We already align ms_struct fields, so don't re-align them.  */
  if (known_align < desired_align
      && !targetm.ms_bitfield_layout_p (rli->t))
    {
      /* No, we need to skip space before this field.
	 Bump the cumulative size to multiple of field alignment.  */

      warning (OPT_Wpadded, "padding struct to align %q+D", field);

      /* If the alignment is still within offset_align, just align
	 the bit position.  */
      if (desired_align < rli->offset_align)
	rli->bitpos = round_up (rli->bitpos, desired_align);
      else
	{
	  /* First adjust OFFSET by the partial bits, then align.  */
	  rli->offset
	    = size_binop (PLUS_EXPR, rli->offset,
			  fold_convert (sizetype,
					size_binop (CEIL_DIV_EXPR, rli->bitpos,
						    bitsize_unit_node)));
	  rli->bitpos = bitsize_zero_node;

	  rli->offset = round_up (rli->offset, desired_align / BITS_PER_UNIT);
	}

      if (! TREE_CONSTANT (rli->offset))
	rli->offset_align = desired_align;

    }

  /* Handle compatibility with PCC.  Note that if the record has any
     variable-sized fields, we need not worry about compatibility.  */
#ifdef PCC_BITFIELD_TYPE_MATTERS
  if (PCC_BITFIELD_TYPE_MATTERS
      && ! targetm.ms_bitfield_layout_p (rli->t)
      && TREE_CODE (field) == FIELD_DECL
      && type != error_mark_node
      && DECL_BIT_FIELD (field)
      && ! DECL_PACKED (field)
      && maximum_field_alignment == 0
      && ! integer_zerop (DECL_SIZE (field))
      && host_integerp (DECL_SIZE (field), 1)
      && host_integerp (rli->offset, 1)
      && host_integerp (TYPE_SIZE (type), 1))
    {
      unsigned int type_align = TYPE_ALIGN (type);
      tree dsize = DECL_SIZE (field);
      HOST_WIDE_INT field_size = tree_low_cst (dsize, 1);
      HOST_WIDE_INT offset = tree_low_cst (rli->offset, 0);
      HOST_WIDE_INT bit_offset = tree_low_cst (rli->bitpos, 0);

#ifdef ADJUST_FIELD_ALIGN
      if (! TYPE_USER_ALIGN (type))
	type_align = ADJUST_FIELD_ALIGN (field, type_align);
#endif

      /* A bit field may not span more units of alignment of its type
	 than its type itself.  Advance to next boundary if necessary.  */
      if (excess_unit_span (offset, bit_offset, field_size, type_align, type))
	rli->bitpos = round_up (rli->bitpos, type_align);

      TYPE_USER_ALIGN (rli->t) |= TYPE_USER_ALIGN (type);
    }
#endif

#ifdef BITFIELD_NBYTES_LIMITED
  if (BITFIELD_NBYTES_LIMITED
      && ! targetm.ms_bitfield_layout_p (rli->t)
      && TREE_CODE (field) == FIELD_DECL
      && type != error_mark_node
      && DECL_BIT_FIELD_TYPE (field)
      && ! DECL_PACKED (field)
      && ! integer_zerop (DECL_SIZE (field))
      && host_integerp (DECL_SIZE (field), 1)
      && host_integerp (rli->offset, 1)
      && host_integerp (TYPE_SIZE (type), 1))
    {
      unsigned int type_align = TYPE_ALIGN (type);
      tree dsize = DECL_SIZE (field);
      HOST_WIDE_INT field_size = tree_low_cst (dsize, 1);
      HOST_WIDE_INT offset = tree_low_cst (rli->offset, 0);
      HOST_WIDE_INT bit_offset = tree_low_cst (rli->bitpos, 0);

#ifdef ADJUST_FIELD_ALIGN
      if (! TYPE_USER_ALIGN (type))
	type_align = ADJUST_FIELD_ALIGN (field, type_align);
#endif

      if (maximum_field_alignment != 0)
	type_align = MIN (type_align, maximum_field_alignment);
      /* ??? This test is opposite the test in the containing if
	 statement, so this code is unreachable currently.  */
      else if (DECL_PACKED (field))
	type_align = MIN (type_align, BITS_PER_UNIT);

      /* A bit field may not span the unit of alignment of its type.
	 Advance to next boundary if necessary.  */
      if (excess_unit_span (offset, bit_offset, field_size, type_align, type))
	rli->bitpos = round_up (rli->bitpos, type_align);

      TYPE_USER_ALIGN (rli->t) |= TYPE_USER_ALIGN (type);
    }
#endif

  /* See the docs for TARGET_MS_BITFIELD_LAYOUT_P for details.
     A subtlety:
	When a bit field is inserted into a packed record, the whole
	size of the underlying type is used by one or more same-size
	adjacent bitfields.  (That is, if its long:3, 32 bits is
	used in the record, and any additional adjacent long bitfields are
	packed into the same chunk of 32 bits. However, if the size
	changes, a new field of that size is allocated.)  In an unpacked
	record, this is the same as using alignment, but not equivalent
	when packing.

     Note: for compatibility, we use the type size, not the type alignment
     to determine alignment, since that matches the documentation */

  if (targetm.ms_bitfield_layout_p (rli->t))
    {
      tree prev_saved = rli->prev_field;
      tree prev_type = prev_saved ? DECL_BIT_FIELD_TYPE (prev_saved) : NULL;

      /* This is a bitfield if it exists.  */
      if (rli->prev_field)
	{
	  /* If both are bitfields, nonzero, and the same size, this is
	     the middle of a run.  Zero declared size fields are special
	     and handled as "end of run". (Note: it's nonzero declared
	     size, but equal type sizes!) (Since we know that both
	     the current and previous fields are bitfields by the
	     time we check it, DECL_SIZE must be present for both.) */
	  if (DECL_BIT_FIELD_TYPE (field)
	      && !integer_zerop (DECL_SIZE (field))
	      && !integer_zerop (DECL_SIZE (rli->prev_field))
	      && host_integerp (DECL_SIZE (rli->prev_field), 0)
	      && host_integerp (TYPE_SIZE (type), 0)
	      && simple_cst_equal (TYPE_SIZE (type), TYPE_SIZE (prev_type)))
	    {
	      /* We're in the middle of a run of equal type size fields; make
		 sure we realign if we run out of bits.  (Not decl size,
		 type size!) */
	      HOST_WIDE_INT bitsize = tree_low_cst (DECL_SIZE (field), 1);

	      if (rli->remaining_in_alignment < bitsize)
		{
		  HOST_WIDE_INT typesize = tree_low_cst (TYPE_SIZE (type), 1);

		  /* out of bits; bump up to next 'word'.  */
		  rli->bitpos
		    = size_binop (PLUS_EXPR, rli->bitpos,
				  bitsize_int (rli->remaining_in_alignment));
		  rli->prev_field = field;
		  if (typesize < bitsize)
		    rli->remaining_in_alignment = 0;
		  else
		    rli->remaining_in_alignment = typesize - bitsize;
		}
	      else
		rli->remaining_in_alignment -= bitsize;
	    }
	  else
	    {
	      /* End of a run: if leaving a run of bitfields of the same type
		 size, we have to "use up" the rest of the bits of the type
		 size.

		 Compute the new position as the sum of the size for the prior
		 type and where we first started working on that type.
		 Note: since the beginning of the field was aligned then
		 of course the end will be too.  No round needed.  */

	      if (!integer_zerop (DECL_SIZE (rli->prev_field)))
		{
		  rli->bitpos
		    = size_binop (PLUS_EXPR, rli->bitpos,
				  bitsize_int (rli->remaining_in_alignment));
		}
	      else
		/* We "use up" size zero fields; the code below should behave
		   as if the prior field was not a bitfield.  */
		prev_saved = NULL;

	      /* Cause a new bitfield to be captured, either this time (if
		 currently a bitfield) or next time we see one.  */
	      if (!DECL_BIT_FIELD_TYPE(field)
		  || integer_zerop (DECL_SIZE (field)))
		rli->prev_field = NULL;
	    }

	  normalize_rli (rli);
        }

      /* If we're starting a new run of same size type bitfields
	 (or a run of non-bitfields), set up the "first of the run"
	 fields.

	 That is, if the current field is not a bitfield, or if there
	 was a prior bitfield the type sizes differ, or if there wasn't
	 a prior bitfield the size of the current field is nonzero.

	 Note: we must be sure to test ONLY the type size if there was
	 a prior bitfield and ONLY for the current field being zero if
	 there wasn't.  */

      if (!DECL_BIT_FIELD_TYPE (field)
	  || (prev_saved != NULL
	      ? !simple_cst_equal (TYPE_SIZE (type), TYPE_SIZE (prev_type))
	      : !integer_zerop (DECL_SIZE (field)) ))
	{
	  /* Never smaller than a byte for compatibility.  */
	  unsigned int type_align = BITS_PER_UNIT;

	  /* (When not a bitfield), we could be seeing a flex array (with
	     no DECL_SIZE).  Since we won't be using remaining_in_alignment
	     until we see a bitfield (and come by here again) we just skip
	     calculating it.  */
	  if (DECL_SIZE (field) != NULL
	      && host_integerp (TYPE_SIZE (TREE_TYPE (field)), 0)
	      && host_integerp (DECL_SIZE (field), 0))
	    {
	      HOST_WIDE_INT bitsize = tree_low_cst (DECL_SIZE (field), 1);
	      HOST_WIDE_INT typesize
		= tree_low_cst (TYPE_SIZE (TREE_TYPE (field)), 1);

	      if (typesize < bitsize)
		rli->remaining_in_alignment = 0;
	      else
		rli->remaining_in_alignment = typesize - bitsize;
	    }

	  /* Now align (conventionally) for the new type.  */
	  type_align = TYPE_ALIGN (TREE_TYPE (field));

	  if (maximum_field_alignment != 0)
	    type_align = MIN (type_align, maximum_field_alignment);

	  rli->bitpos = round_up (rli->bitpos, type_align);

          /* If we really aligned, don't allow subsequent bitfields
	     to undo that.  */
	  rli->prev_field = NULL;
	}
    }

  /* Offset so far becomes the position of this field after normalizing.  */
  normalize_rli (rli);
  DECL_FIELD_OFFSET (field) = rli->offset;
  DECL_FIELD_BIT_OFFSET (field) = rli->bitpos;
  SET_DECL_OFFSET_ALIGN (field, rli->offset_align);

  /* If this field ended up more aligned than we thought it would be (we
     approximate this by seeing if its position changed), lay out the field
     again; perhaps we can use an integral mode for it now.  */
  if (! integer_zerop (DECL_FIELD_BIT_OFFSET (field)))
    actual_align = (tree_low_cst (DECL_FIELD_BIT_OFFSET (field), 1)
		    & - tree_low_cst (DECL_FIELD_BIT_OFFSET (field), 1));
  else if (integer_zerop (DECL_FIELD_OFFSET (field)))
    actual_align = MAX (BIGGEST_ALIGNMENT, rli->record_align);
  else if (host_integerp (DECL_FIELD_OFFSET (field), 1))
    actual_align = (BITS_PER_UNIT
		   * (tree_low_cst (DECL_FIELD_OFFSET (field), 1)
		      & - tree_low_cst (DECL_FIELD_OFFSET (field), 1)));
  else
    actual_align = DECL_OFFSET_ALIGN (field);
  /* ACTUAL_ALIGN is still the actual alignment *within the record* .
     store / extract bit field operations will check the alignment of the
     record against the mode of bit fields.  */

  if (known_align != actual_align)
    layout_decl (field, actual_align);

  if (rli->prev_field == NULL && DECL_BIT_FIELD_TYPE (field))
    rli->prev_field = field;

  /* Now add size of this field to the size of the record.  If the size is
     not constant, treat the field as being a multiple of bytes and just
     adjust the offset, resetting the bit position.  Otherwise, apportion the
     size amongst the bit position and offset.  First handle the case of an
     unspecified size, which can happen when we have an invalid nested struct
     definition, such as struct j { struct j { int i; } }.  The error message
     is printed in finish_struct.  */
  if (DECL_SIZE (field) == 0)
    /* Do nothing.  */;
  else if (TREE_CODE (DECL_SIZE (field)) != INTEGER_CST
	   || TREE_CONSTANT_OVERFLOW (DECL_SIZE (field)))
    {
      rli->offset
	= size_binop (PLUS_EXPR, rli->offset,
		      fold_convert (sizetype,
				    size_binop (CEIL_DIV_EXPR, rli->bitpos,
						bitsize_unit_node)));
      rli->offset
	= size_binop (PLUS_EXPR, rli->offset, DECL_SIZE_UNIT (field));
      rli->bitpos = bitsize_zero_node;
      rli->offset_align = MIN (rli->offset_align, desired_align);
    }
  else if (targetm.ms_bitfield_layout_p (rli->t))
    {
      rli->bitpos = size_binop (PLUS_EXPR, rli->bitpos, DECL_SIZE (field));

      /* If we ended a bitfield before the full length of the type then
	 pad the struct out to the full length of the last type.  */
      if ((TREE_CHAIN (field) == NULL
	   || TREE_CODE (TREE_CHAIN (field)) != FIELD_DECL)
	  && DECL_BIT_FIELD_TYPE (field)
	  && !integer_zerop (DECL_SIZE (field)))
	rli->bitpos = size_binop (PLUS_EXPR, rli->bitpos,
				  bitsize_int (rli->remaining_in_alignment));

      normalize_rli (rli);
    }
  else
    {
      rli->bitpos = size_binop (PLUS_EXPR, rli->bitpos, DECL_SIZE (field));
      normalize_rli (rli);
    }
}

/* Assuming that all the fields have been laid out, this function uses
   RLI to compute the final TYPE_SIZE, TYPE_ALIGN, etc. for the type
   indicated by RLI.  */

static void
finalize_record_size (record_layout_info rli)
{
  tree unpadded_size, unpadded_size_unit;

  /* Now we want just byte and bit offsets, so set the offset alignment
     to be a byte and then normalize.  */
  rli->offset_align = BITS_PER_UNIT;
  normalize_rli (rli);

  /* Determine the desired alignment.  */
#ifdef ROUND_TYPE_ALIGN
  TYPE_ALIGN (rli->t) = ROUND_TYPE_ALIGN (rli->t, TYPE_ALIGN (rli->t),
					  rli->record_align);
#else
  TYPE_ALIGN (rli->t) = MAX (TYPE_ALIGN (rli->t), rli->record_align);
#endif

  /* Compute the size so far.  Be sure to allow for extra bits in the
     size in bytes.  We have guaranteed above that it will be no more
     than a single byte.  */
  unpadded_size = rli_size_so_far (rli);
  unpadded_size_unit = rli_size_unit_so_far (rli);
  if (! integer_zerop (rli->bitpos))
    unpadded_size_unit
      = size_binop (PLUS_EXPR, unpadded_size_unit, size_one_node);

  /* Round the size up to be a multiple of the required alignment.  */
  TYPE_SIZE (rli->t) = round_up (unpadded_size, TYPE_ALIGN (rli->t));
  TYPE_SIZE_UNIT (rli->t)
    = round_up (unpadded_size_unit, TYPE_ALIGN_UNIT (rli->t));

  if (TREE_CONSTANT (unpadded_size)
      && simple_cst_equal (unpadded_size, TYPE_SIZE (rli->t)) == 0)
    warning (OPT_Wpadded, "padding struct size to alignment boundary");

  if (warn_packed && TREE_CODE (rli->t) == RECORD_TYPE
      && TYPE_PACKED (rli->t) && ! rli->packed_maybe_necessary
      && TREE_CONSTANT (unpadded_size))
    {
      tree unpacked_size;

#ifdef ROUND_TYPE_ALIGN
      rli->unpacked_align
	= ROUND_TYPE_ALIGN (rli->t, TYPE_ALIGN (rli->t), rli->unpacked_align);
#else
      rli->unpacked_align = MAX (TYPE_ALIGN (rli->t), rli->unpacked_align);
#endif

      unpacked_size = round_up (TYPE_SIZE (rli->t), rli->unpacked_align);
      if (simple_cst_equal (unpacked_size, TYPE_SIZE (rli->t)))
	{
	  TYPE_PACKED (rli->t) = 0;

	  if (TYPE_NAME (rli->t))
	    {
	      const char *name;

	      if (TREE_CODE (TYPE_NAME (rli->t)) == IDENTIFIER_NODE)
		name = IDENTIFIER_POINTER (TYPE_NAME (rli->t));
	      else
		name = IDENTIFIER_POINTER (DECL_NAME (TYPE_NAME (rli->t)));

	      if (STRICT_ALIGNMENT)
		warning (OPT_Wpacked, "packed attribute causes inefficient "
			 "alignment for %qs", name);
	      else
		warning (OPT_Wpacked,
			 "packed attribute is unnecessary for %qs", name);
	    }
	  else
	    {
	      if (STRICT_ALIGNMENT)
		warning (OPT_Wpacked,
			 "packed attribute causes inefficient alignment");
	      else
		warning (OPT_Wpacked, "packed attribute is unnecessary");
	    }
	}
    }
}

/* Compute the TYPE_MODE for the TYPE (which is a RECORD_TYPE).  */

void
compute_record_mode (tree type)
{
  tree field;
  enum machine_mode mode = VOIDmode;

  /* Most RECORD_TYPEs have BLKmode, so we start off assuming that.
     However, if possible, we use a mode that fits in a register
     instead, in order to allow for better optimization down the
     line.  */
  TYPE_MODE (type) = BLKmode;

  if (! host_integerp (TYPE_SIZE (type), 1))
    return;

  /* A record which has any BLKmode members must itself be
     BLKmode; it can't go in a register.  Unless the member is
     BLKmode only because it isn't aligned.  */
  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    {
      if (TREE_CODE (field) != FIELD_DECL)
	continue;

      if (TREE_CODE (TREE_TYPE (field)) == ERROR_MARK
	  || (TYPE_MODE (TREE_TYPE (field)) == BLKmode
	      && ! TYPE_NO_FORCE_BLK (TREE_TYPE (field))
	      && !(TYPE_SIZE (TREE_TYPE (field)) != 0
		   && integer_zerop (TYPE_SIZE (TREE_TYPE (field)))))
	  || ! host_integerp (bit_position (field), 1)
	  || DECL_SIZE (field) == 0
	  || ! host_integerp (DECL_SIZE (field), 1))
	return;

      /* If this field is the whole struct, remember its mode so
	 that, say, we can put a double in a class into a DF
	 register instead of forcing it to live in the stack.  */
      if (simple_cst_equal (TYPE_SIZE (type), DECL_SIZE (field)))
	mode = DECL_MODE (field);

#ifdef MEMBER_TYPE_FORCES_BLK
      /* With some targets, eg. c4x, it is sub-optimal
	 to access an aligned BLKmode structure as a scalar.  */

      if (MEMBER_TYPE_FORCES_BLK (field, mode))
	return;
#endif /* MEMBER_TYPE_FORCES_BLK  */
    }

  /* If we only have one real field; use its mode if that mode's size
     matches the type's size.  This only applies to RECORD_TYPE.  This
     does not apply to unions.  */
  if (TREE_CODE (type) == RECORD_TYPE && mode != VOIDmode
      && host_integerp (TYPE_SIZE (type), 1)
      && GET_MODE_BITSIZE (mode) == TREE_INT_CST_LOW (TYPE_SIZE (type)))
    TYPE_MODE (type) = mode;
  else
    TYPE_MODE (type) = mode_for_size_tree (TYPE_SIZE (type), MODE_INT, 1);

  /* If structure's known alignment is less than what the scalar
     mode would need, and it matters, then stick with BLKmode.  */
  if (TYPE_MODE (type) != BLKmode
      && STRICT_ALIGNMENT
      && ! (TYPE_ALIGN (type) >= BIGGEST_ALIGNMENT
	    || TYPE_ALIGN (type) >= GET_MODE_ALIGNMENT (TYPE_MODE (type))))
    {
      /* If this is the only reason this type is BLKmode, then
	 don't force containing types to be BLKmode.  */
      TYPE_NO_FORCE_BLK (type) = 1;
      TYPE_MODE (type) = BLKmode;
    }
}

/* Compute TYPE_SIZE and TYPE_ALIGN for TYPE, once it has been laid
   out.  */

static void
finalize_type_size (tree type)
{
  /* Normally, use the alignment corresponding to the mode chosen.
     However, where strict alignment is not required, avoid
     over-aligning structures, since most compilers do not do this
     alignment.  */

  if (TYPE_MODE (type) != BLKmode && TYPE_MODE (type) != VOIDmode
      && (STRICT_ALIGNMENT
	  || (TREE_CODE (type) != RECORD_TYPE && TREE_CODE (type) != UNION_TYPE
	      && TREE_CODE (type) != QUAL_UNION_TYPE
	      && TREE_CODE (type) != ARRAY_TYPE)))
    {
      unsigned mode_align = GET_MODE_ALIGNMENT (TYPE_MODE (type));

      /* Don't override a larger alignment requirement coming from a user
	 alignment of one of the fields.  */
      if (mode_align >= TYPE_ALIGN (type))
	{
	  TYPE_ALIGN (type) = mode_align;
	  TYPE_USER_ALIGN (type) = 0;
	}
    }

  /* Do machine-dependent extra alignment.  */
#ifdef ROUND_TYPE_ALIGN
  TYPE_ALIGN (type)
    = ROUND_TYPE_ALIGN (type, TYPE_ALIGN (type), BITS_PER_UNIT);
#endif

  /* If we failed to find a simple way to calculate the unit size
     of the type, find it by division.  */
  if (TYPE_SIZE_UNIT (type) == 0 && TYPE_SIZE (type) != 0)
    /* TYPE_SIZE (type) is computed in bitsizetype.  After the division, the
       result will fit in sizetype.  We will get more efficient code using
       sizetype, so we force a conversion.  */
    TYPE_SIZE_UNIT (type)
      = fold_convert (sizetype,
		      size_binop (FLOOR_DIV_EXPR, TYPE_SIZE (type),
				  bitsize_unit_node));

  if (TYPE_SIZE (type) != 0)
    {
      TYPE_SIZE (type) = round_up (TYPE_SIZE (type), TYPE_ALIGN (type));
      TYPE_SIZE_UNIT (type) = round_up (TYPE_SIZE_UNIT (type),
					TYPE_ALIGN_UNIT (type));
    }

  /* Evaluate nonconstant sizes only once, either now or as soon as safe.  */
  if (TYPE_SIZE (type) != 0 && TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST)
    TYPE_SIZE (type) = variable_size (TYPE_SIZE (type));
  if (TYPE_SIZE_UNIT (type) != 0
      && TREE_CODE (TYPE_SIZE_UNIT (type)) != INTEGER_CST)
    TYPE_SIZE_UNIT (type) = variable_size (TYPE_SIZE_UNIT (type));

  /* Also layout any other variants of the type.  */
  if (TYPE_NEXT_VARIANT (type)
      || type != TYPE_MAIN_VARIANT (type))
    {
      tree variant;
      /* Record layout info of this variant.  */
      tree size = TYPE_SIZE (type);
      tree size_unit = TYPE_SIZE_UNIT (type);
      unsigned int align = TYPE_ALIGN (type);
      unsigned int user_align = TYPE_USER_ALIGN (type);
      enum machine_mode mode = TYPE_MODE (type);

      /* Copy it into all variants.  */
      for (variant = TYPE_MAIN_VARIANT (type);
	   variant != 0;
	   variant = TYPE_NEXT_VARIANT (variant))
	{
	  TYPE_SIZE (variant) = size;
	  TYPE_SIZE_UNIT (variant) = size_unit;
	  TYPE_ALIGN (variant) = align;
	  TYPE_USER_ALIGN (variant) = user_align;
	  TYPE_MODE (variant) = mode;
	}
    }
}

/* Do all of the work required to layout the type indicated by RLI,
   once the fields have been laid out.  This function will call `free'
   for RLI, unless FREE_P is false.  Passing a value other than false
   for FREE_P is bad practice; this option only exists to support the
   G++ 3.2 ABI.  */

void
finish_record_layout (record_layout_info rli, int free_p)
{
  tree variant;

  /* Compute the final size.  */
  finalize_record_size (rli);

  /* Compute the TYPE_MODE for the record.  */
  compute_record_mode (rli->t);

  /* Perform any last tweaks to the TYPE_SIZE, etc.  */
  finalize_type_size (rli->t);

  /* Propagate TYPE_PACKED to variants.  With C++ templates,
     handle_packed_attribute is too early to do this.  */
  for (variant = TYPE_NEXT_VARIANT (rli->t); variant;
       variant = TYPE_NEXT_VARIANT (variant))
    TYPE_PACKED (variant) = TYPE_PACKED (rli->t);

  /* Lay out any static members.  This is done now because their type
     may use the record's type.  */
  while (rli->pending_statics)
    {
      layout_decl (TREE_VALUE (rli->pending_statics), 0);
      rli->pending_statics = TREE_CHAIN (rli->pending_statics);
    }

  /* Clean up.  */
  if (free_p)
    free (rli);
}


/* Finish processing a builtin RECORD_TYPE type TYPE.  It's name is
   NAME, its fields are chained in reverse on FIELDS.

   If ALIGN_TYPE is non-null, it is given the same alignment as
   ALIGN_TYPE.  */

void
finish_builtin_struct (tree type, const char *name, tree fields,
		       tree align_type)
{
  tree tail, next;

  for (tail = NULL_TREE; fields; tail = fields, fields = next)
    {
      DECL_FIELD_CONTEXT (fields) = type;
      next = TREE_CHAIN (fields);
      TREE_CHAIN (fields) = tail;
    }
  TYPE_FIELDS (type) = tail;

  if (align_type)
    {
      TYPE_ALIGN (type) = TYPE_ALIGN (align_type);
      TYPE_USER_ALIGN (type) = TYPE_USER_ALIGN (align_type);
    }

  layout_type (type);
#if 0 /* not yet, should get fixed properly later */
  TYPE_NAME (type) = make_type_decl (get_identifier (name), type);
#else
  TYPE_NAME (type) = build_decl (TYPE_DECL, get_identifier (name), type);
#endif
  TYPE_STUB_DECL (type) = TYPE_NAME (type);
  layout_decl (TYPE_NAME (type), 0);
}

/* Calculate the mode, size, and alignment for TYPE.
   For an array type, calculate the element separation as well.
   Record TYPE on the chain of permanent or temporary types
   so that dbxout will find out about it.

   TYPE_SIZE of a type is nonzero if the type has been laid out already.
   layout_type does nothing on such a type.

   If the type is incomplete, its TYPE_SIZE remains zero.  */

void
layout_type (tree type)
{
  gcc_assert (type);

  if (type == error_mark_node)
    return;

  /* Do nothing if type has been laid out before.  */
  if (TYPE_SIZE (type))
    return;

  switch (TREE_CODE (type))
    {
    case LANG_TYPE:
      /* This kind of type is the responsibility
	 of the language-specific code.  */
      gcc_unreachable ();

    case BOOLEAN_TYPE:  /* Used for Java, Pascal, and Chill.  */
      if (TYPE_PRECISION (type) == 0)
	TYPE_PRECISION (type) = 1; /* default to one byte/boolean.  */

      /* ... fall through ...  */

    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
      if (TREE_CODE (TYPE_MIN_VALUE (type)) == INTEGER_CST
	  && tree_int_cst_sgn (TYPE_MIN_VALUE (type)) >= 0)
	TYPE_UNSIGNED (type) = 1;

      TYPE_MODE (type) = smallest_mode_for_size (TYPE_PRECISION (type),
						 MODE_INT);
      TYPE_SIZE (type) = bitsize_int (GET_MODE_BITSIZE (TYPE_MODE (type)));
      TYPE_SIZE_UNIT (type) = size_int (GET_MODE_SIZE (TYPE_MODE (type)));
      break;

    case REAL_TYPE:
      TYPE_MODE (type) = mode_for_size (TYPE_PRECISION (type), MODE_FLOAT, 0);
      TYPE_SIZE (type) = bitsize_int (GET_MODE_BITSIZE (TYPE_MODE (type)));
      TYPE_SIZE_UNIT (type) = size_int (GET_MODE_SIZE (TYPE_MODE (type)));
      break;

    case COMPLEX_TYPE:
      TYPE_UNSIGNED (type) = TYPE_UNSIGNED (TREE_TYPE (type));
      TYPE_MODE (type)
	= mode_for_size (2 * TYPE_PRECISION (TREE_TYPE (type)),
			 (TREE_CODE (TREE_TYPE (type)) == REAL_TYPE
			  ? MODE_COMPLEX_FLOAT : MODE_COMPLEX_INT),
			 0);
      TYPE_SIZE (type) = bitsize_int (GET_MODE_BITSIZE (TYPE_MODE (type)));
      TYPE_SIZE_UNIT (type) = size_int (GET_MODE_SIZE (TYPE_MODE (type)));
      break;

    case VECTOR_TYPE:
      {
	int nunits = TYPE_VECTOR_SUBPARTS (type);
	tree nunits_tree = build_int_cst (NULL_TREE, nunits);
	tree innertype = TREE_TYPE (type);

	gcc_assert (!(nunits & (nunits - 1)));

	/* Find an appropriate mode for the vector type.  */
	if (TYPE_MODE (type) == VOIDmode)
	  {
	    enum machine_mode innermode = TYPE_MODE (innertype);
	    enum machine_mode mode;

	    /* First, look for a supported vector type.  */
	    if (SCALAR_FLOAT_MODE_P (innermode))
	      mode = MIN_MODE_VECTOR_FLOAT;
	    else
	      mode = MIN_MODE_VECTOR_INT;

	    for (; mode != VOIDmode ; mode = GET_MODE_WIDER_MODE (mode))
	      if (GET_MODE_NUNITS (mode) == nunits
	  	  && GET_MODE_INNER (mode) == innermode
	  	  && targetm.vector_mode_supported_p (mode))
	        break;

	    /* For integers, try mapping it to a same-sized scalar mode.  */
	    if (mode == VOIDmode
	        && GET_MODE_CLASS (innermode) == MODE_INT)
	      mode = mode_for_size (nunits * GET_MODE_BITSIZE (innermode),
				    MODE_INT, 0);

	    if (mode == VOIDmode || !have_regs_of_mode[mode])
	      TYPE_MODE (type) = BLKmode;
	    else
	      TYPE_MODE (type) = mode;
	  }

        TYPE_UNSIGNED (type) = TYPE_UNSIGNED (TREE_TYPE (type));
	TYPE_SIZE_UNIT (type) = int_const_binop (MULT_EXPR,
					         TYPE_SIZE_UNIT (innertype),
					         nunits_tree, 0);
	TYPE_SIZE (type) = int_const_binop (MULT_EXPR, TYPE_SIZE (innertype),
					    nunits_tree, 0);

	/* Always naturally align vectors.  This prevents ABI changes
	   depending on whether or not native vector modes are supported.  */
	TYPE_ALIGN (type) = tree_low_cst (TYPE_SIZE (type), 0);
        break;
      }

    case VOID_TYPE:
      /* This is an incomplete type and so doesn't have a size.  */
      TYPE_ALIGN (type) = 1;
      TYPE_USER_ALIGN (type) = 0;
      TYPE_MODE (type) = VOIDmode;
      break;

    case OFFSET_TYPE:
      TYPE_SIZE (type) = bitsize_int (POINTER_SIZE);
      TYPE_SIZE_UNIT (type) = size_int (POINTER_SIZE / BITS_PER_UNIT);
      /* A pointer might be MODE_PARTIAL_INT,
	 but ptrdiff_t must be integral.  */
      TYPE_MODE (type) = mode_for_size (POINTER_SIZE, MODE_INT, 0);
      break;

    case FUNCTION_TYPE:
    case METHOD_TYPE:
      /* It's hard to see what the mode and size of a function ought to
	 be, but we do know the alignment is FUNCTION_BOUNDARY, so
	 make it consistent with that.  */
      TYPE_MODE (type) = mode_for_size (FUNCTION_BOUNDARY, MODE_INT, 0);
      TYPE_SIZE (type) = bitsize_int (FUNCTION_BOUNDARY);
      TYPE_SIZE_UNIT (type) = size_int (FUNCTION_BOUNDARY / BITS_PER_UNIT);
      break;

    case POINTER_TYPE:
    case REFERENCE_TYPE:
	/* APPLE LOCAL blocks */
    case BLOCK_POINTER_TYPE:
      {

	enum machine_mode mode = ((TREE_CODE (type) == REFERENCE_TYPE
				   && reference_types_internal)
				  ? Pmode : TYPE_MODE (type));

	int nbits = GET_MODE_BITSIZE (mode);

	TYPE_SIZE (type) = bitsize_int (nbits);
	TYPE_SIZE_UNIT (type) = size_int (GET_MODE_SIZE (mode));
	TYPE_UNSIGNED (type) = 1;
	TYPE_PRECISION (type) = nbits;
      }
      break;

    case ARRAY_TYPE:
      {
	tree index = TYPE_DOMAIN (type);
	tree element = TREE_TYPE (type);

	build_pointer_type (element);

	/* We need to know both bounds in order to compute the size.  */
	if (index && TYPE_MAX_VALUE (index) && TYPE_MIN_VALUE (index)
	    && TYPE_SIZE (element))
	  {
	    tree ub = TYPE_MAX_VALUE (index);
	    tree lb = TYPE_MIN_VALUE (index);
	    tree length;
	    tree element_size;

	    /* The initial subtraction should happen in the original type so
	       that (possible) negative values are handled appropriately.  */
	    length = size_binop (PLUS_EXPR, size_one_node,
				 fold_convert (sizetype,
					       fold_build2 (MINUS_EXPR,
							    TREE_TYPE (lb),
							    ub, lb)));

	    /* Special handling for arrays of bits (for Chill).  */
	    element_size = TYPE_SIZE (element);
	    if (TYPE_PACKED (type) && INTEGRAL_TYPE_P (element)
		&& (integer_zerop (TYPE_MAX_VALUE (element))
		    || integer_onep (TYPE_MAX_VALUE (element)))
		&& host_integerp (TYPE_MIN_VALUE (element), 1))
	      {
		HOST_WIDE_INT maxvalue
		  = tree_low_cst (TYPE_MAX_VALUE (element), 1);
		HOST_WIDE_INT minvalue
		  = tree_low_cst (TYPE_MIN_VALUE (element), 1);

		if (maxvalue - minvalue == 1
		    && (maxvalue == 1 || maxvalue == 0))
		  element_size = integer_one_node;
	      }

	    /* If neither bound is a constant and sizetype is signed, make
	       sure the size is never negative.  We should really do this
	       if *either* bound is non-constant, but this is the best
	       compromise between C and Ada.  */
	    if (!TYPE_UNSIGNED (sizetype)
		&& TREE_CODE (TYPE_MIN_VALUE (index)) != INTEGER_CST
		&& TREE_CODE (TYPE_MAX_VALUE (index)) != INTEGER_CST)
	      length = size_binop (MAX_EXPR, length, size_zero_node);

	    TYPE_SIZE (type) = size_binop (MULT_EXPR, element_size,
					   fold_convert (bitsizetype,
							 length));

	    /* If we know the size of the element, calculate the total
	       size directly, rather than do some division thing below.
	       This optimization helps Fortran assumed-size arrays
	       (where the size of the array is determined at runtime)
	       substantially.
	       Note that we can't do this in the case where the size of
	       the elements is one bit since TYPE_SIZE_UNIT cannot be
	       set correctly in that case.  */
	    if (TYPE_SIZE_UNIT (element) != 0 && ! integer_onep (element_size))
	      TYPE_SIZE_UNIT (type)
		= size_binop (MULT_EXPR, TYPE_SIZE_UNIT (element), length);
	  }

	/* Now round the alignment and size,
	   using machine-dependent criteria if any.  */

#ifdef ROUND_TYPE_ALIGN
	TYPE_ALIGN (type)
	  = ROUND_TYPE_ALIGN (type, TYPE_ALIGN (element), BITS_PER_UNIT);
#else
	TYPE_ALIGN (type) = MAX (TYPE_ALIGN (element), BITS_PER_UNIT);
#endif
	TYPE_USER_ALIGN (type) = TYPE_USER_ALIGN (element);
	TYPE_MODE (type) = BLKmode;
	if (TYPE_SIZE (type) != 0
#ifdef MEMBER_TYPE_FORCES_BLK
	    && ! MEMBER_TYPE_FORCES_BLK (type, VOIDmode)
#endif
	    /* BLKmode elements force BLKmode aggregate;
	       else extract/store fields may lose.  */
	    && (TYPE_MODE (TREE_TYPE (type)) != BLKmode
		|| TYPE_NO_FORCE_BLK (TREE_TYPE (type))))
	  {
	    /* One-element arrays get the component type's mode.  */
	    if (simple_cst_equal (TYPE_SIZE (type),
				  TYPE_SIZE (TREE_TYPE (type))))
	      TYPE_MODE (type) = TYPE_MODE (TREE_TYPE (type));
	    else
	      TYPE_MODE (type)
		= mode_for_size_tree (TYPE_SIZE (type), MODE_INT, 1);

	    if (TYPE_MODE (type) != BLKmode
		&& STRICT_ALIGNMENT && TYPE_ALIGN (type) < BIGGEST_ALIGNMENT
		&& TYPE_ALIGN (type) < GET_MODE_ALIGNMENT (TYPE_MODE (type))
		&& TYPE_MODE (type) != BLKmode)
	      {
		TYPE_NO_FORCE_BLK (type) = 1;
		TYPE_MODE (type) = BLKmode;
	      }
	  }
	/* When the element size is constant, check that it is at least as
	   large as the element alignment.  */
	if (TYPE_SIZE_UNIT (element)
	    && TREE_CODE (TYPE_SIZE_UNIT (element)) == INTEGER_CST
	    /* If TYPE_SIZE_UNIT overflowed, then it is certainly larger than
	       TYPE_ALIGN_UNIT.  */
	    && !TREE_CONSTANT_OVERFLOW (TYPE_SIZE_UNIT (element))
	    && !integer_zerop (TYPE_SIZE_UNIT (element))
	    && compare_tree_int (TYPE_SIZE_UNIT (element),
			  	 TYPE_ALIGN_UNIT (element)) < 0)
	  error ("alignment of array elements is greater than element size");
	break;
      }

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      {
	tree field;
	record_layout_info rli;

	/* Initialize the layout information.  */
	rli = start_record_layout (type);

	/* If this is a QUAL_UNION_TYPE, we want to process the fields
	   in the reverse order in building the COND_EXPR that denotes
	   its size.  We reverse them again later.  */
	if (TREE_CODE (type) == QUAL_UNION_TYPE)
	  TYPE_FIELDS (type) = nreverse (TYPE_FIELDS (type));

	/* Place all the fields.  */
	for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	  place_field (rli, field);

	if (TREE_CODE (type) == QUAL_UNION_TYPE)
	  TYPE_FIELDS (type) = nreverse (TYPE_FIELDS (type));

	if (lang_adjust_rli)
	  (*lang_adjust_rli) (rli);

	/* Finish laying out the record.  */
	finish_record_layout (rli, /*free_p=*/true);
      }
      break;

    default:
      gcc_unreachable ();
    }

  /* Compute the final TYPE_SIZE, TYPE_ALIGN, etc. for TYPE.  For
     records and unions, finish_record_layout already called this
     function.  */
  if (TREE_CODE (type) != RECORD_TYPE
      && TREE_CODE (type) != UNION_TYPE
      && TREE_CODE (type) != QUAL_UNION_TYPE)
    finalize_type_size (type);

  /* If an alias set has been set for this aggregate when it was incomplete,
     force it into alias set 0.
     This is too conservative, but we cannot call record_component_aliases
     here because some frontends still change the aggregates after
     layout_type.  */
  if (AGGREGATE_TYPE_P (type) && TYPE_ALIAS_SET_KNOWN_P (type))
    TYPE_ALIAS_SET (type) = 0;
}

/* Create and return a type for signed integers of PRECISION bits.  */

tree
make_signed_type (int precision)
{
  tree type = make_node (INTEGER_TYPE);

  TYPE_PRECISION (type) = precision;

  fixup_signed_type (type);
  return type;
}

/* Create and return a type for unsigned integers of PRECISION bits.  */

tree
make_unsigned_type (int precision)
{
  tree type = make_node (INTEGER_TYPE);

  TYPE_PRECISION (type) = precision;

  fixup_unsigned_type (type);
  return type;
}

/* Initialize sizetype and bitsizetype to a reasonable and temporary
   value to enable integer types to be created.  */

void
initialize_sizetypes (bool signed_p)
{
  tree t = make_node (INTEGER_TYPE);
  int precision = GET_MODE_BITSIZE (SImode);

  TYPE_MODE (t) = SImode;
  TYPE_ALIGN (t) = GET_MODE_ALIGNMENT (SImode);
  TYPE_USER_ALIGN (t) = 0;
  TYPE_IS_SIZETYPE (t) = 1;
  TYPE_UNSIGNED (t) = !signed_p;
  TYPE_SIZE (t) = build_int_cst (t, precision);
  TYPE_SIZE_UNIT (t) = build_int_cst (t, GET_MODE_SIZE (SImode));
  TYPE_PRECISION (t) = precision;

  /* Set TYPE_MIN_VALUE and TYPE_MAX_VALUE.  */
  set_min_and_max_values_for_integral_type (t, precision, !signed_p);

  sizetype = t;
  bitsizetype = build_distinct_type_copy (t);
}

/* Make sizetype a version of TYPE, and initialize *sizetype
   accordingly.  We do this by overwriting the stub sizetype and
   bitsizetype nodes created by initialize_sizetypes.  This makes sure
   that (a) anything stubby about them no longer exists, (b) any
   INTEGER_CSTs created with such a type, remain valid.  */

void
set_sizetype (tree type)
{
  int oprecision = TYPE_PRECISION (type);
  /* The *bitsizetype types use a precision that avoids overflows when
     calculating signed sizes / offsets in bits.  However, when
     cross-compiling from a 32 bit to a 64 bit host, we are limited to 64 bit
     precision.  */
  int precision = MIN (MIN (oprecision + BITS_PER_UNIT_LOG + 1,
			    MAX_FIXED_MODE_SIZE),
		       2 * HOST_BITS_PER_WIDE_INT);
  tree t;

  gcc_assert (TYPE_UNSIGNED (type) == TYPE_UNSIGNED (sizetype));

  t = build_distinct_type_copy (type);
  /* We do want to use sizetype's cache, as we will be replacing that
     type.  */
  TYPE_CACHED_VALUES (t) = TYPE_CACHED_VALUES (sizetype);
  TYPE_CACHED_VALUES_P (t) = TYPE_CACHED_VALUES_P (sizetype);
  TREE_TYPE (TYPE_CACHED_VALUES (t)) = type;
  TYPE_UID (t) = TYPE_UID (sizetype);
  TYPE_IS_SIZETYPE (t) = 1;

  /* Replace our original stub sizetype.  */
  memcpy (sizetype, t, tree_size (sizetype));
  TYPE_MAIN_VARIANT (sizetype) = sizetype;

  t = make_node (INTEGER_TYPE);
  TYPE_NAME (t) = get_identifier ("bit_size_type");
  /* We do want to use bitsizetype's cache, as we will be replacing that
     type.  */
  TYPE_CACHED_VALUES (t) = TYPE_CACHED_VALUES (bitsizetype);
  TYPE_CACHED_VALUES_P (t) = TYPE_CACHED_VALUES_P (bitsizetype);
  TYPE_PRECISION (t) = precision;
  TYPE_UID (t) = TYPE_UID (bitsizetype);
  TYPE_IS_SIZETYPE (t) = 1;

  /* Replace our original stub bitsizetype.  */
  memcpy (bitsizetype, t, tree_size (bitsizetype));
  TYPE_MAIN_VARIANT (bitsizetype) = bitsizetype;

  if (TYPE_UNSIGNED (type))
    {
      fixup_unsigned_type (bitsizetype);
      ssizetype = build_distinct_type_copy (make_signed_type (oprecision));
      TYPE_IS_SIZETYPE (ssizetype) = 1;
      sbitsizetype = build_distinct_type_copy (make_signed_type (precision));
      TYPE_IS_SIZETYPE (sbitsizetype) = 1;
    }
  else
    {
      fixup_signed_type (bitsizetype);
      ssizetype = sizetype;
      sbitsizetype = bitsizetype;
    }

  /* If SIZETYPE is unsigned, we need to fix TYPE_MAX_VALUE so that
     it is sign extended in a way consistent with force_fit_type.  */
  if (TYPE_UNSIGNED (type))
    {
      tree orig_max, new_max;

      orig_max = TYPE_MAX_VALUE (sizetype);

      /* Build a new node with the same values, but a different type.  */
      new_max = build_int_cst_wide (sizetype,
				    TREE_INT_CST_LOW (orig_max),
				    TREE_INT_CST_HIGH (orig_max));

      /* Now sign extend it using force_fit_type to ensure
	 consistency.  */
      new_max = force_fit_type (new_max, 0, 0, 0);
      TYPE_MAX_VALUE (sizetype) = new_max;
    }
}

/* TYPE is an integral type, i.e., an INTEGRAL_TYPE, ENUMERAL_TYPE
   or BOOLEAN_TYPE.  Set TYPE_MIN_VALUE and TYPE_MAX_VALUE
   for TYPE, based on the PRECISION and whether or not the TYPE
   IS_UNSIGNED.  PRECISION need not correspond to a width supported
   natively by the hardware; for example, on a machine with 8-bit,
   16-bit, and 32-bit register modes, PRECISION might be 7, 23, or
   61.  */

void
set_min_and_max_values_for_integral_type (tree type,
					  int precision,
					  bool is_unsigned)
{
  tree min_value;
  tree max_value;

  if (is_unsigned)
    {
      min_value = build_int_cst (type, 0);
      max_value
	= build_int_cst_wide (type, precision - HOST_BITS_PER_WIDE_INT >= 0
			      ? -1
			      : ((HOST_WIDE_INT) 1 << precision) - 1,
			      precision - HOST_BITS_PER_WIDE_INT > 0
			      ? ((unsigned HOST_WIDE_INT) ~0
				 >> (HOST_BITS_PER_WIDE_INT
				     - (precision - HOST_BITS_PER_WIDE_INT)))
			      : 0);
    }
  else
    {
      min_value
	= build_int_cst_wide (type,
			      (precision - HOST_BITS_PER_WIDE_INT > 0
			       ? 0
			       : (HOST_WIDE_INT) (-1) << (precision - 1)),
			      (((HOST_WIDE_INT) (-1)
				<< (precision - HOST_BITS_PER_WIDE_INT - 1 > 0
				    ? precision - HOST_BITS_PER_WIDE_INT - 1
				    : 0))));
      max_value
	= build_int_cst_wide (type,
			      (precision - HOST_BITS_PER_WIDE_INT > 0
			       ? -1
			       : ((HOST_WIDE_INT) 1 << (precision - 1)) - 1),
			      (precision - HOST_BITS_PER_WIDE_INT - 1 > 0
			       ? (((HOST_WIDE_INT) 1
				   << (precision - HOST_BITS_PER_WIDE_INT - 1))) - 1
			       : 0));
    }

  TYPE_MIN_VALUE (type) = min_value;
  TYPE_MAX_VALUE (type) = max_value;
}

/* Set the extreme values of TYPE based on its precision in bits,
   then lay it out.  Used when make_signed_type won't do
   because the tree code is not INTEGER_TYPE.
   E.g. for Pascal, when the -fsigned-char option is given.  */

void
fixup_signed_type (tree type)
{
  int precision = TYPE_PRECISION (type);

  /* We can not represent properly constants greater then
     2 * HOST_BITS_PER_WIDE_INT, still we need the types
     as they are used by i386 vector extensions and friends.  */
  if (precision > HOST_BITS_PER_WIDE_INT * 2)
    precision = HOST_BITS_PER_WIDE_INT * 2;

  set_min_and_max_values_for_integral_type (type, precision,
					    /*is_unsigned=*/false);

  /* Lay out the type: set its alignment, size, etc.  */
  layout_type (type);
}

/* Set the extreme values of TYPE based on its precision in bits,
   then lay it out.  This is used both in `make_unsigned_type'
   and for enumeral types.  */

void
fixup_unsigned_type (tree type)
{
  int precision = TYPE_PRECISION (type);

  /* We can not represent properly constants greater then
     2 * HOST_BITS_PER_WIDE_INT, still we need the types
     as they are used by i386 vector extensions and friends.  */
  if (precision > HOST_BITS_PER_WIDE_INT * 2)
    precision = HOST_BITS_PER_WIDE_INT * 2;

  TYPE_UNSIGNED (type) = 1;

  set_min_and_max_values_for_integral_type (type, precision,
					    /*is_unsigned=*/true);

  /* Lay out the type: set its alignment, size, etc.  */
  layout_type (type);
}

/* Find the best machine mode to use when referencing a bit field of length
   BITSIZE bits starting at BITPOS.

   The underlying object is known to be aligned to a boundary of ALIGN bits.
   If LARGEST_MODE is not VOIDmode, it means that we should not use a mode
   larger than LARGEST_MODE (usually SImode).

   If no mode meets all these conditions, we return VOIDmode.

   If VOLATILEP is false and SLOW_BYTE_ACCESS is false, we return the
   smallest mode meeting these conditions.

   If VOLATILEP is false and SLOW_BYTE_ACCESS is true, we return the
   largest mode (but a mode no wider than UNITS_PER_WORD) that meets
   all the conditions.

   If VOLATILEP is true the narrow_volatile_bitfields target hook is used to
   decide which of the above modes should be used.  */

enum machine_mode
get_best_mode (int bitsize, int bitpos, unsigned int align,
	       enum machine_mode largest_mode, int volatilep)
{
  enum machine_mode mode;
  unsigned int unit = 0;

  /* Find the narrowest integer mode that contains the bit field.  */
  for (mode = GET_CLASS_NARROWEST_MODE (MODE_INT); mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    {
      unit = GET_MODE_BITSIZE (mode);
      if ((bitpos % unit) + bitsize <= unit)
	break;
    }

  if (mode == VOIDmode
      /* It is tempting to omit the following line
	 if STRICT_ALIGNMENT is true.
	 But that is incorrect, since if the bitfield uses part of 3 bytes
	 and we use a 4-byte mode, we could get a spurious segv
	 if the extra 4th byte is past the end of memory.
	 (Though at least one Unix compiler ignores this problem:
	 that on the Sequent 386 machine.  */
      || MIN (unit, BIGGEST_ALIGNMENT) > align
      || (largest_mode != VOIDmode && unit > GET_MODE_BITSIZE (largest_mode)))
    return VOIDmode;

  if ((SLOW_BYTE_ACCESS && ! volatilep)
      || (volatilep && !targetm.narrow_volatile_bitfield()))
    {
      enum machine_mode wide_mode = VOIDmode, tmode;

      for (tmode = GET_CLASS_NARROWEST_MODE (MODE_INT); tmode != VOIDmode;
	   tmode = GET_MODE_WIDER_MODE (tmode))
	{
	  unit = GET_MODE_BITSIZE (tmode);
	  if (bitpos / unit == (bitpos + bitsize - 1) / unit
	      && unit <= BITS_PER_WORD
	      && unit <= MIN (align, BIGGEST_ALIGNMENT)
	      && (largest_mode == VOIDmode
		  || unit <= GET_MODE_BITSIZE (largest_mode)))
	    wide_mode = tmode;
	}

      if (wide_mode != VOIDmode)
	return wide_mode;
    }

  return mode;
}

/* Gets minimal and maximal values for MODE (signed or unsigned depending on
   SIGN).  The returned constants are made to be usable in TARGET_MODE.  */

void
get_mode_bounds (enum machine_mode mode, int sign,
		 enum machine_mode target_mode,
		 rtx *mmin, rtx *mmax)
{
  unsigned size = GET_MODE_BITSIZE (mode);
  unsigned HOST_WIDE_INT min_val, max_val;

  gcc_assert (size <= HOST_BITS_PER_WIDE_INT);

  if (sign)
    {
      min_val = -((unsigned HOST_WIDE_INT) 1 << (size - 1));
      max_val = ((unsigned HOST_WIDE_INT) 1 << (size - 1)) - 1;
    }
  else
    {
      min_val = 0;
      max_val = ((unsigned HOST_WIDE_INT) 1 << (size - 1) << 1) - 1;
    }

  *mmin = gen_int_mode (min_val, target_mode);
  *mmax = gen_int_mode (max_val, target_mode);
}

#include "gt-stor-layout.h"
