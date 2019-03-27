/* Expands front end tree to back end RTL for GCC.
   Copyright (C) 1987, 1988, 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997,
   1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

/* $FreeBSD$ */

/* This file handles the generation of rtl code from tree structure
   at the level of the function as a whole.
   It creates the rtl expressions for parameters and auto variables
   and has full responsibility for allocating stack slots.

   `expand_function_start' is called at the beginning of a function,
   before the function body is parsed, and `expand_function_end' is
   called after parsing the body.

   Call `assign_stack_local' to allocate a stack slot for a local variable.
   This is usually done during the RTL generation for the function body,
   but it can also be done in the reload pass when a pseudo-register does
   not get a hard register.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "except.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "libfuncs.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "insn-config.h"
#include "recog.h"
#include "output.h"
#include "basic-block.h"
#include "toplev.h"
#include "hashtab.h"
#include "ggc.h"
#include "tm_p.h"
#include "integrate.h"
#include "langhooks.h"
#include "target.h"
#include "cfglayout.h"
#include "tree-gimple.h"
#include "tree-pass.h"
#include "predict.h"
#include "vecprim.h"

#ifndef LOCAL_ALIGNMENT
#define LOCAL_ALIGNMENT(TYPE, ALIGNMENT) ALIGNMENT
#endif

#ifndef STACK_ALIGNMENT_NEEDED
#define STACK_ALIGNMENT_NEEDED 1
#endif

#define STACK_BYTES (STACK_BOUNDARY / BITS_PER_UNIT)

/* Some systems use __main in a way incompatible with its use in gcc, in these
   cases use the macros NAME__MAIN to give a quoted symbol and SYMBOL__MAIN to
   give the same symbol without quotes for an alternative entry point.  You
   must define both, or neither.  */
#ifndef NAME__MAIN
#define NAME__MAIN "__main"
#endif

/* Round a value to the lowest integer less than it that is a multiple of
   the required alignment.  Avoid using division in case the value is
   negative.  Assume the alignment is a power of two.  */
#define FLOOR_ROUND(VALUE,ALIGN) ((VALUE) & ~((ALIGN) - 1))

/* Similar, but round to the next highest integer that meets the
   alignment.  */
#define CEIL_ROUND(VALUE,ALIGN)	(((VALUE) + (ALIGN) - 1) & ~((ALIGN)- 1))

/* Nonzero if function being compiled doesn't contain any calls
   (ignoring the prologue and epilogue).  This is set prior to
   local register allocation and is valid for the remaining
   compiler passes.  */
int current_function_is_leaf;

/* Nonzero if function being compiled doesn't modify the stack pointer
   (ignoring the prologue and epilogue).  This is only valid after
   life_analysis has run.  */
int current_function_sp_is_unchanging;

/* Nonzero if the function being compiled is a leaf function which only
   uses leaf registers.  This is valid after reload (specifically after
   sched2) and is useful only if the port defines LEAF_REGISTERS.  */
int current_function_uses_only_leaf_regs;

/* Nonzero once virtual register instantiation has been done.
   assign_stack_local uses frame_pointer_rtx when this is nonzero.
   calls.c:emit_library_call_value_1 uses it to set up
   post-instantiation libcalls.  */
int virtuals_instantiated;

/* APPLE LOCAL begin radar 5732232 - blocks */
struct block_sema_info *cur_block;
/* APPLE LOCAL end radar 5732232 - blocks */

/* Assign unique numbers to labels generated for profiling, debugging, etc.  */
static GTY(()) int funcdef_no;

/* These variables hold pointers to functions to create and destroy
   target specific, per-function data structures.  */
struct machine_function * (*init_machine_status) (void);

/* The currently compiled function.  */
struct function *cfun = 0;

/* These arrays record the INSN_UIDs of the prologue and epilogue insns.  */
static VEC(int,heap) *prologue;
static VEC(int,heap) *epilogue;

/* Array of INSN_UIDs to hold the INSN_UIDs for each sibcall epilogue
   in this function.  */
static VEC(int,heap) *sibcall_epilogue;

/* In order to evaluate some expressions, such as function calls returning
   structures in memory, we need to temporarily allocate stack locations.
   We record each allocated temporary in the following structure.

   Associated with each temporary slot is a nesting level.  When we pop up
   one level, all temporaries associated with the previous level are freed.
   Normally, all temporaries are freed after the execution of the statement
   in which they were created.  However, if we are inside a ({...}) grouping,
   the result may be in a temporary and hence must be preserved.  If the
   result could be in a temporary, we preserve it if we can determine which
   one it is in.  If we cannot determine which temporary may contain the
   result, all temporaries are preserved.  A temporary is preserved by
   pretending it was allocated at the previous nesting level.

   Automatic variables are also assigned temporary slots, at the nesting
   level where they are defined.  They are marked a "kept" so that
   free_temp_slots will not free them.  */

struct temp_slot GTY(())
{
  /* Points to next temporary slot.  */
  struct temp_slot *next;
  /* Points to previous temporary slot.  */
  struct temp_slot *prev;

  /* The rtx to used to reference the slot.  */
  rtx slot;
  /* The rtx used to represent the address if not the address of the
     slot above.  May be an EXPR_LIST if multiple addresses exist.  */
  rtx address;
  /* The alignment (in bits) of the slot.  */
  unsigned int align;
  /* The size, in units, of the slot.  */
  HOST_WIDE_INT size;
  /* The type of the object in the slot, or zero if it doesn't correspond
     to a type.  We use this to determine whether a slot can be reused.
     It can be reused if objects of the type of the new slot will always
     conflict with objects of the type of the old slot.  */
  tree type;
  /* Nonzero if this temporary is currently in use.  */
  char in_use;
  /* Nonzero if this temporary has its address taken.  */
  char addr_taken;
  /* Nesting level at which this slot is being used.  */
  int level;
  /* Nonzero if this should survive a call to free_temp_slots.  */
  int keep;
  /* The offset of the slot from the frame_pointer, including extra space
     for alignment.  This info is for combine_temp_slots.  */
  HOST_WIDE_INT base_offset;
  /* The size of the slot, including extra space for alignment.  This
     info is for combine_temp_slots.  */
  HOST_WIDE_INT full_size;
};

/* Forward declarations.  */

static rtx assign_stack_local_1 (enum machine_mode, HOST_WIDE_INT, int,
				 struct function *);
static struct temp_slot *find_temp_slot_from_address (rtx);
static void pad_to_arg_alignment (struct args_size *, int, struct args_size *);
static void pad_below (struct args_size *, enum machine_mode, tree);
static void reorder_blocks_1 (rtx, tree, VEC(tree,heap) **);
static int all_blocks (tree, tree *);
static tree *get_block_vector (tree, int *);
extern tree debug_find_var_in_block_tree (tree, tree);
/* We always define `record_insns' even if it's not used so that we
   can always export `prologue_epilogue_contains'.  */
static void record_insns (rtx, VEC(int,heap) **) ATTRIBUTE_UNUSED;
static int contains (rtx, VEC(int,heap) **);
#ifdef HAVE_return
static void emit_return_into_block (basic_block, rtx);
#endif
#if defined(HAVE_epilogue) && defined(INCOMING_RETURN_ADDR_RTX)
static rtx keep_stack_depressed (rtx);
#endif
static void prepare_function_start (tree);
static void do_clobber_return_reg (rtx, void *);
static void do_use_return_reg (rtx, void *);
static void set_insn_locators (rtx, int) ATTRIBUTE_UNUSED;
/* APPLE LOCAL radar 6163705, Blocks prologues  */
static rtx find_block_prologue_insns (void);

/* Pointer to chain of `struct function' for containing functions.  */
struct function *outer_function_chain;

/* Given a function decl for a containing function,
   return the `struct function' for it.  */

struct function *
find_function_data (tree decl)
{
  struct function *p;

  for (p = outer_function_chain; p; p = p->outer)
    if (p->decl == decl)
      return p;

  gcc_unreachable ();
}

/* Save the current context for compilation of a nested function.
   This is called from language-specific code.  The caller should use
   the enter_nested langhook to save any language-specific state,
   since this function knows only about language-independent
   variables.  */

void
push_function_context_to (tree context ATTRIBUTE_UNUSED)
{
  struct function *p;

  if (cfun == 0)
    init_dummy_function_start ();
  p = cfun;

  p->outer = outer_function_chain;
  outer_function_chain = p;

  lang_hooks.function.enter_nested (p);

  cfun = 0;
}

void
push_function_context (void)
{
  push_function_context_to (current_function_decl);
}

/* Restore the last saved context, at the end of a nested function.
   This function is called from language-specific code.  */

void
pop_function_context_from (tree context ATTRIBUTE_UNUSED)
{
  struct function *p = outer_function_chain;

  cfun = p;
  outer_function_chain = p->outer;

  current_function_decl = p->decl;

  lang_hooks.function.leave_nested (p);

  /* Reset variables that have known state during rtx generation.  */
  virtuals_instantiated = 0;
  generating_concat_p = 1;
}

void
pop_function_context (void)
{
  pop_function_context_from (current_function_decl);
}

/* Clear out all parts of the state in F that can safely be discarded
   after the function has been parsed, but not compiled, to let
   garbage collection reclaim the memory.  */

void
free_after_parsing (struct function *f)
{
  /* f->expr->forced_labels is used by code generation.  */
  /* f->emit->regno_reg_rtx is used by code generation.  */
  /* f->varasm is used by code generation.  */
  /* f->eh->eh_return_stub_label is used by code generation.  */

  lang_hooks.function.final (f);
}

/* Clear out all parts of the state in F that can safely be discarded
   after the function has been compiled, to let garbage collection
   reclaim the memory.  */

void
free_after_compilation (struct function *f)
{
  VEC_free (int, heap, prologue);
  VEC_free (int, heap, epilogue);
  VEC_free (int, heap, sibcall_epilogue);

  f->eh = NULL;
  f->expr = NULL;
  f->emit = NULL;
  f->varasm = NULL;
  f->machine = NULL;
  f->cfg = NULL;

  f->x_avail_temp_slots = NULL;
  f->x_used_temp_slots = NULL;
  f->arg_offset_rtx = NULL;
  f->return_rtx = NULL;
  f->internal_arg_pointer = NULL;
  f->x_nonlocal_goto_handler_labels = NULL;
  f->x_return_label = NULL;
  f->x_naked_return_label = NULL;
  f->x_stack_slot_list = NULL;
  f->x_stack_check_probe_note = NULL;
  f->x_arg_pointer_save_area = NULL;
  f->x_parm_birth_insn = NULL;
  f->epilogue_delay_list = NULL;
}

/* Allocate fixed slots in the stack frame of the current function.  */

/* Return size needed for stack frame based on slots so far allocated in
   function F.
   This size counts from zero.  It is not rounded to PREFERRED_STACK_BOUNDARY;
   the caller may have to do that.  */

static HOST_WIDE_INT
get_func_frame_size (struct function *f)
{
  if (FRAME_GROWS_DOWNWARD)
    return -f->x_frame_offset;
  else
    return f->x_frame_offset;
}

/* Return size needed for stack frame based on slots so far allocated.
   This size counts from zero.  It is not rounded to PREFERRED_STACK_BOUNDARY;
   the caller may have to do that.  */

HOST_WIDE_INT
get_frame_size (void)
{
  return get_func_frame_size (cfun);
}

/* Issue an error message and return TRUE if frame OFFSET overflows in
   the signed target pointer arithmetics for function FUNC.  Otherwise
   return FALSE.  */

bool
frame_offset_overflow (HOST_WIDE_INT offset, tree func)
{  
  unsigned HOST_WIDE_INT size = FRAME_GROWS_DOWNWARD ? -offset : offset;

  if (size > ((unsigned HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (Pmode) - 1))
	       /* Leave room for the fixed part of the frame.  */
	       - 64 * UNITS_PER_WORD)
    {
      error ("%Jtotal size of local objects too large", func);
      return TRUE;
    }

  return FALSE;
}

/* Allocate a stack slot of SIZE bytes and return a MEM rtx for it
   with machine mode MODE.

   ALIGN controls the amount of alignment for the address of the slot:
   0 means according to MODE,
   -1 means use BIGGEST_ALIGNMENT and round size to multiple of that,
   -2 means use BITS_PER_UNIT,
   positive specifies alignment boundary in bits.

   We do not round to stack_boundary here.

   FUNCTION specifies the function to allocate in.  */

static rtx
assign_stack_local_1 (enum machine_mode mode, HOST_WIDE_INT size, int align,
		      struct function *function)
{
  rtx x, addr;
  int bigend_correction = 0;
  unsigned int alignment;
  int frame_off, frame_alignment, frame_phase;

  if (align == 0)
    {
      tree type;

      if (mode == BLKmode)
	alignment = BIGGEST_ALIGNMENT;
      else
	alignment = GET_MODE_ALIGNMENT (mode);

      /* Allow the target to (possibly) increase the alignment of this
	 stack slot.  */
      type = lang_hooks.types.type_for_mode (mode, 0);
      if (type)
	alignment = LOCAL_ALIGNMENT (type, alignment);

      alignment /= BITS_PER_UNIT;
    }
  else if (align == -1)
    {
      alignment = BIGGEST_ALIGNMENT / BITS_PER_UNIT;
      size = CEIL_ROUND (size, alignment);
    }
  else if (align == -2)
    alignment = 1; /* BITS_PER_UNIT / BITS_PER_UNIT */
  else
    alignment = align / BITS_PER_UNIT;

  if (FRAME_GROWS_DOWNWARD)
    function->x_frame_offset -= size;

  /* Ignore alignment we can't do with expected alignment of the boundary.  */
  if (alignment * BITS_PER_UNIT > PREFERRED_STACK_BOUNDARY)
    alignment = PREFERRED_STACK_BOUNDARY / BITS_PER_UNIT;

  if (function->stack_alignment_needed < alignment * BITS_PER_UNIT)
    function->stack_alignment_needed = alignment * BITS_PER_UNIT;

  /* Calculate how many bytes the start of local variables is off from
     stack alignment.  */
  frame_alignment = PREFERRED_STACK_BOUNDARY / BITS_PER_UNIT;
  frame_off = STARTING_FRAME_OFFSET % frame_alignment;
  frame_phase = frame_off ? frame_alignment - frame_off : 0;

  /* Round the frame offset to the specified alignment.  The default is
     to always honor requests to align the stack but a port may choose to
     do its own stack alignment by defining STACK_ALIGNMENT_NEEDED.  */
  if (STACK_ALIGNMENT_NEEDED
      || mode != BLKmode
      || size != 0)
    {
      /*  We must be careful here, since FRAME_OFFSET might be negative and
	  division with a negative dividend isn't as well defined as we might
	  like.  So we instead assume that ALIGNMENT is a power of two and
	  use logical operations which are unambiguous.  */
      if (FRAME_GROWS_DOWNWARD)
	function->x_frame_offset
	  = (FLOOR_ROUND (function->x_frame_offset - frame_phase,
			  (unsigned HOST_WIDE_INT) alignment)
	     + frame_phase);
      else
	function->x_frame_offset
	  = (CEIL_ROUND (function->x_frame_offset - frame_phase,
			 (unsigned HOST_WIDE_INT) alignment)
	     + frame_phase);
    }

  /* On a big-endian machine, if we are allocating more space than we will use,
     use the least significant bytes of those that are allocated.  */
  if (BYTES_BIG_ENDIAN && mode != BLKmode && GET_MODE_SIZE (mode) < size)
    bigend_correction = size - GET_MODE_SIZE (mode);

  /* If we have already instantiated virtual registers, return the actual
     address relative to the frame pointer.  */
  if (function == cfun && virtuals_instantiated)
    addr = plus_constant (frame_pointer_rtx,
			  trunc_int_for_mode
			  (frame_offset + bigend_correction
			   + STARTING_FRAME_OFFSET, Pmode));
  else
    addr = plus_constant (virtual_stack_vars_rtx,
			  trunc_int_for_mode
			  (function->x_frame_offset + bigend_correction,
			   Pmode));

  if (!FRAME_GROWS_DOWNWARD)
    function->x_frame_offset += size;

  x = gen_rtx_MEM (mode, addr);
  MEM_NOTRAP_P (x) = 1;

  function->x_stack_slot_list
    = gen_rtx_EXPR_LIST (VOIDmode, x, function->x_stack_slot_list);

  if (frame_offset_overflow (function->x_frame_offset, function->decl))
    function->x_frame_offset = 0;

  return x;
}

/* Wrapper around assign_stack_local_1;  assign a local stack slot for the
   current function.  */

rtx
assign_stack_local (enum machine_mode mode, HOST_WIDE_INT size, int align)
{
  return assign_stack_local_1 (mode, size, align, cfun);
}


/* Removes temporary slot TEMP from LIST.  */

static void
cut_slot_from_list (struct temp_slot *temp, struct temp_slot **list)
{
  if (temp->next)
    temp->next->prev = temp->prev;
  if (temp->prev)
    temp->prev->next = temp->next;
  else
    *list = temp->next;

  temp->prev = temp->next = NULL;
}

/* Inserts temporary slot TEMP to LIST.  */

static void
insert_slot_to_list (struct temp_slot *temp, struct temp_slot **list)
{
  temp->next = *list;
  if (*list)
    (*list)->prev = temp;
  temp->prev = NULL;
  *list = temp;
}

/* Returns the list of used temp slots at LEVEL.  */

static struct temp_slot **
temp_slots_at_level (int level)
{
  if (level >= (int) VEC_length (temp_slot_p, used_temp_slots))
    {
      size_t old_length = VEC_length (temp_slot_p, used_temp_slots);
      temp_slot_p *p;

      VEC_safe_grow (temp_slot_p, gc, used_temp_slots, level + 1);
      p = VEC_address (temp_slot_p, used_temp_slots);
      memset (&p[old_length], 0,
	      sizeof (temp_slot_p) * (level + 1 - old_length));
    }

  return &(VEC_address (temp_slot_p, used_temp_slots)[level]);
}

/* Returns the maximal temporary slot level.  */

static int
max_slot_level (void)
{
  if (!used_temp_slots)
    return -1;

  return VEC_length (temp_slot_p, used_temp_slots) - 1;
}

/* Moves temporary slot TEMP to LEVEL.  */

static void
move_slot_to_level (struct temp_slot *temp, int level)
{
  cut_slot_from_list (temp, temp_slots_at_level (temp->level));
  insert_slot_to_list (temp, temp_slots_at_level (level));
  temp->level = level;
}

/* Make temporary slot TEMP available.  */

static void
make_slot_available (struct temp_slot *temp)
{
  cut_slot_from_list (temp, temp_slots_at_level (temp->level));
  insert_slot_to_list (temp, &avail_temp_slots);
  temp->in_use = 0;
  temp->level = -1;
}

/* Allocate a temporary stack slot and record it for possible later
   reuse.

   MODE is the machine mode to be given to the returned rtx.

   SIZE is the size in units of the space required.  We do no rounding here
   since assign_stack_local will do any required rounding.

   KEEP is 1 if this slot is to be retained after a call to
   free_temp_slots.  Automatic variables for a block are allocated
   with this flag.  KEEP values of 2 or 3 were needed respectively
   for variables whose lifetime is controlled by CLEANUP_POINT_EXPRs
   or for SAVE_EXPRs, but they are now unused.

   TYPE is the type that will be used for the stack slot.  */

rtx
assign_stack_temp_for_type (enum machine_mode mode, HOST_WIDE_INT size,
			    int keep, tree type)
{
  unsigned int align;
  struct temp_slot *p, *best_p = 0, *selected = NULL, **pp;
  rtx slot;

  /* If SIZE is -1 it means that somebody tried to allocate a temporary
     of a variable size.  */
  gcc_assert (size != -1);

  /* These are now unused.  */
  gcc_assert (keep <= 1);

  if (mode == BLKmode)
    align = BIGGEST_ALIGNMENT;
  else
    align = GET_MODE_ALIGNMENT (mode);

  if (! type)
    type = lang_hooks.types.type_for_mode (mode, 0);

  if (type)
    align = LOCAL_ALIGNMENT (type, align);

  /* Try to find an available, already-allocated temporary of the proper
     mode which meets the size and alignment requirements.  Choose the
     smallest one with the closest alignment.
   
     If assign_stack_temp is called outside of the tree->rtl expansion,
     we cannot reuse the stack slots (that may still refer to
     VIRTUAL_STACK_VARS_REGNUM).  */
  if (!virtuals_instantiated)
    {
      for (p = avail_temp_slots; p; p = p->next)
	{
	  if (p->align >= align && p->size >= size
	      && GET_MODE (p->slot) == mode
	      && objects_must_conflict_p (p->type, type)
	      && (best_p == 0 || best_p->size > p->size
		  || (best_p->size == p->size && best_p->align > p->align)))
	    {
	      if (p->align == align && p->size == size)
		{
		  selected = p;
		  cut_slot_from_list (selected, &avail_temp_slots);
		  best_p = 0;
		  break;
		}
	      best_p = p;
	    }
	}
    }

  /* Make our best, if any, the one to use.  */
  if (best_p)
    {
      selected = best_p;
      cut_slot_from_list (selected, &avail_temp_slots);

      /* If there are enough aligned bytes left over, make them into a new
	 temp_slot so that the extra bytes don't get wasted.  Do this only
	 for BLKmode slots, so that we can be sure of the alignment.  */
      if (GET_MODE (best_p->slot) == BLKmode)
	{
	  int alignment = best_p->align / BITS_PER_UNIT;
	  HOST_WIDE_INT rounded_size = CEIL_ROUND (size, alignment);

	  if (best_p->size - rounded_size >= alignment)
	    {
	      p = ggc_alloc (sizeof (struct temp_slot));
	      p->in_use = p->addr_taken = 0;
	      p->size = best_p->size - rounded_size;
	      p->base_offset = best_p->base_offset + rounded_size;
	      p->full_size = best_p->full_size - rounded_size;
	      p->slot = adjust_address_nv (best_p->slot, BLKmode, rounded_size);
	      p->align = best_p->align;
	      p->address = 0;
	      p->type = best_p->type;
	      insert_slot_to_list (p, &avail_temp_slots);

	      stack_slot_list = gen_rtx_EXPR_LIST (VOIDmode, p->slot,
						   stack_slot_list);

	      best_p->size = rounded_size;
	      best_p->full_size = rounded_size;
	    }
	}
    }

  /* If we still didn't find one, make a new temporary.  */
  if (selected == 0)
    {
      HOST_WIDE_INT frame_offset_old = frame_offset;

      p = ggc_alloc (sizeof (struct temp_slot));

      /* We are passing an explicit alignment request to assign_stack_local.
	 One side effect of that is assign_stack_local will not round SIZE
	 to ensure the frame offset remains suitably aligned.

	 So for requests which depended on the rounding of SIZE, we go ahead
	 and round it now.  We also make sure ALIGNMENT is at least
	 BIGGEST_ALIGNMENT.  */
      gcc_assert (mode != BLKmode || align == BIGGEST_ALIGNMENT);
      p->slot = assign_stack_local (mode,
				    (mode == BLKmode
				     ? CEIL_ROUND (size, (int) align / BITS_PER_UNIT)
				     : size),
				    align);

      p->align = align;

      /* The following slot size computation is necessary because we don't
	 know the actual size of the temporary slot until assign_stack_local
	 has performed all the frame alignment and size rounding for the
	 requested temporary.  Note that extra space added for alignment
	 can be either above or below this stack slot depending on which
	 way the frame grows.  We include the extra space if and only if it
	 is above this slot.  */
      if (FRAME_GROWS_DOWNWARD)
	p->size = frame_offset_old - frame_offset;
      else
	p->size = size;

      /* Now define the fields used by combine_temp_slots.  */
      if (FRAME_GROWS_DOWNWARD)
	{
	  p->base_offset = frame_offset;
	  p->full_size = frame_offset_old - frame_offset;
	}
      else
	{
	  p->base_offset = frame_offset_old;
	  p->full_size = frame_offset - frame_offset_old;
	}
      p->address = 0;

      selected = p;
    }

  p = selected;
  p->in_use = 1;
  p->addr_taken = 0;
  p->type = type;
  p->level = temp_slot_level;
  p->keep = keep;

  pp = temp_slots_at_level (p->level);
  insert_slot_to_list (p, pp);

  /* Create a new MEM rtx to avoid clobbering MEM flags of old slots.  */
  slot = gen_rtx_MEM (mode, XEXP (p->slot, 0));
  stack_slot_list = gen_rtx_EXPR_LIST (VOIDmode, slot, stack_slot_list);

  /* If we know the alias set for the memory that will be used, use
     it.  If there's no TYPE, then we don't know anything about the
     alias set for the memory.  */
  set_mem_alias_set (slot, type ? get_alias_set (type) : 0);
  set_mem_align (slot, align);

  /* If a type is specified, set the relevant flags.  */
  if (type != 0)
    {
      MEM_VOLATILE_P (slot) = TYPE_VOLATILE (type);
      MEM_SET_IN_STRUCT_P (slot, AGGREGATE_TYPE_P (type));
    }
  MEM_NOTRAP_P (slot) = 1;

  return slot;
}

/* Allocate a temporary stack slot and record it for possible later
   reuse.  First three arguments are same as in preceding function.  */

rtx
assign_stack_temp (enum machine_mode mode, HOST_WIDE_INT size, int keep)
{
  return assign_stack_temp_for_type (mode, size, keep, NULL_TREE);
}

/* Assign a temporary.
   If TYPE_OR_DECL is a decl, then we are doing it on behalf of the decl
   and so that should be used in error messages.  In either case, we
   allocate of the given type.
   KEEP is as for assign_stack_temp.
   MEMORY_REQUIRED is 1 if the result must be addressable stack memory;
   it is 0 if a register is OK.
   DONT_PROMOTE is 1 if we should not promote values in register
   to wider modes.  */

rtx
assign_temp (tree type_or_decl, int keep, int memory_required,
	     int dont_promote ATTRIBUTE_UNUSED)
{
  tree type, decl;
  enum machine_mode mode;
#ifdef PROMOTE_MODE
  int unsignedp;
#endif

  if (DECL_P (type_or_decl))
    decl = type_or_decl, type = TREE_TYPE (decl);
  else
    decl = NULL, type = type_or_decl;

  mode = TYPE_MODE (type);
#ifdef PROMOTE_MODE
  unsignedp = TYPE_UNSIGNED (type);
#endif

  if (mode == BLKmode || memory_required)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      rtx tmp;

      /* Zero sized arrays are GNU C extension.  Set size to 1 to avoid
	 problems with allocating the stack space.  */
      if (size == 0)
	size = 1;

      /* Unfortunately, we don't yet know how to allocate variable-sized
	 temporaries.  However, sometimes we can find a fixed upper limit on
	 the size, so try that instead.  */
      else if (size == -1)
	size = max_int_size_in_bytes (type);

      /* The size of the temporary may be too large to fit into an integer.  */
      /* ??? Not sure this should happen except for user silliness, so limit
	 this to things that aren't compiler-generated temporaries.  The
	 rest of the time we'll die in assign_stack_temp_for_type.  */
      if (decl && size == -1
	  && TREE_CODE (TYPE_SIZE_UNIT (type)) == INTEGER_CST)
	{
	  error ("size of variable %q+D is too large", decl);
	  size = 1;
	}

      tmp = assign_stack_temp_for_type (mode, size, keep, type);
      return tmp;
    }

#ifdef PROMOTE_MODE
  if (! dont_promote)
    mode = promote_mode (type, mode, &unsignedp, 0);
#endif

  return gen_reg_rtx (mode);
}

/* Combine temporary stack slots which are adjacent on the stack.

   This allows for better use of already allocated stack space.  This is only
   done for BLKmode slots because we can be sure that we won't have alignment
   problems in this case.  */

static void
combine_temp_slots (void)
{
  struct temp_slot *p, *q, *next, *next_q;
  int num_slots;

  /* We can't combine slots, because the information about which slot
     is in which alias set will be lost.  */
  if (flag_strict_aliasing)
    return;

  /* If there are a lot of temp slots, don't do anything unless
     high levels of optimization.  */
  if (! flag_expensive_optimizations)
    for (p = avail_temp_slots, num_slots = 0; p; p = p->next, num_slots++)
      if (num_slots > 100 || (num_slots > 10 && optimize == 0))
	return;

  for (p = avail_temp_slots; p; p = next)
    {
      int delete_p = 0;

      next = p->next;

      if (GET_MODE (p->slot) != BLKmode)
	continue;

      for (q = p->next; q; q = next_q)
	{
       	  int delete_q = 0;

	  next_q = q->next;

	  if (GET_MODE (q->slot) != BLKmode)
	    continue;

	  if (p->base_offset + p->full_size == q->base_offset)
	    {
	      /* Q comes after P; combine Q into P.  */
	      p->size += q->size;
	      p->full_size += q->full_size;
	      delete_q = 1;
	    }
	  else if (q->base_offset + q->full_size == p->base_offset)
	    {
	      /* P comes after Q; combine P into Q.  */
	      q->size += p->size;
	      q->full_size += p->full_size;
	      delete_p = 1;
	      break;
	    }
	  if (delete_q)
	    cut_slot_from_list (q, &avail_temp_slots);
	}

      /* Either delete P or advance past it.  */
      if (delete_p)
	cut_slot_from_list (p, &avail_temp_slots);
    }
}

/* Find the temp slot corresponding to the object at address X.  */

static struct temp_slot *
find_temp_slot_from_address (rtx x)
{
  struct temp_slot *p;
  rtx next;
  int i;

  for (i = max_slot_level (); i >= 0; i--)
    for (p = *temp_slots_at_level (i); p; p = p->next)
      {
	if (XEXP (p->slot, 0) == x
	    || p->address == x
	    || (GET_CODE (x) == PLUS
		&& XEXP (x, 0) == virtual_stack_vars_rtx
		&& GET_CODE (XEXP (x, 1)) == CONST_INT
		&& INTVAL (XEXP (x, 1)) >= p->base_offset
		&& INTVAL (XEXP (x, 1)) < p->base_offset + p->full_size))
	  return p;

	else if (p->address != 0 && GET_CODE (p->address) == EXPR_LIST)
	  for (next = p->address; next; next = XEXP (next, 1))
	    if (XEXP (next, 0) == x)
	      return p;
      }

  /* If we have a sum involving a register, see if it points to a temp
     slot.  */
  if (GET_CODE (x) == PLUS && REG_P (XEXP (x, 0))
      && (p = find_temp_slot_from_address (XEXP (x, 0))) != 0)
    return p;
  else if (GET_CODE (x) == PLUS && REG_P (XEXP (x, 1))
	   && (p = find_temp_slot_from_address (XEXP (x, 1))) != 0)
    return p;

  return 0;
}

/* Indicate that NEW is an alternate way of referring to the temp slot
   that previously was known by OLD.  */

void
update_temp_slot_address (rtx old, rtx new)
{
  struct temp_slot *p;

  if (rtx_equal_p (old, new))
    return;

  p = find_temp_slot_from_address (old);

  /* If we didn't find one, see if both OLD is a PLUS.  If so, and NEW
     is a register, see if one operand of the PLUS is a temporary
     location.  If so, NEW points into it.  Otherwise, if both OLD and
     NEW are a PLUS and if there is a register in common between them.
     If so, try a recursive call on those values.  */
  if (p == 0)
    {
      if (GET_CODE (old) != PLUS)
	return;

      if (REG_P (new))
	{
	  update_temp_slot_address (XEXP (old, 0), new);
	  update_temp_slot_address (XEXP (old, 1), new);
	  return;
	}
      else if (GET_CODE (new) != PLUS)
	return;

      if (rtx_equal_p (XEXP (old, 0), XEXP (new, 0)))
	update_temp_slot_address (XEXP (old, 1), XEXP (new, 1));
      else if (rtx_equal_p (XEXP (old, 1), XEXP (new, 0)))
	update_temp_slot_address (XEXP (old, 0), XEXP (new, 1));
      else if (rtx_equal_p (XEXP (old, 0), XEXP (new, 1)))
	update_temp_slot_address (XEXP (old, 1), XEXP (new, 0));
      else if (rtx_equal_p (XEXP (old, 1), XEXP (new, 1)))
	update_temp_slot_address (XEXP (old, 0), XEXP (new, 0));

      return;
    }

  /* Otherwise add an alias for the temp's address.  */
  else if (p->address == 0)
    p->address = new;
  else
    {
      if (GET_CODE (p->address) != EXPR_LIST)
	p->address = gen_rtx_EXPR_LIST (VOIDmode, p->address, NULL_RTX);

      p->address = gen_rtx_EXPR_LIST (VOIDmode, new, p->address);
    }
}

/* If X could be a reference to a temporary slot, mark the fact that its
   address was taken.  */

void
mark_temp_addr_taken (rtx x)
{
  struct temp_slot *p;

  if (x == 0)
    return;

  /* If X is not in memory or is at a constant address, it cannot be in
     a temporary slot.  */
  if (!MEM_P (x) || CONSTANT_P (XEXP (x, 0)))
    return;

  p = find_temp_slot_from_address (XEXP (x, 0));
  if (p != 0)
    p->addr_taken = 1;
}

/* If X could be a reference to a temporary slot, mark that slot as
   belonging to the to one level higher than the current level.  If X
   matched one of our slots, just mark that one.  Otherwise, we can't
   easily predict which it is, so upgrade all of them.  Kept slots
   need not be touched.

   This is called when an ({...}) construct occurs and a statement
   returns a value in memory.  */

void
preserve_temp_slots (rtx x)
{
  struct temp_slot *p = 0, *next;

  /* If there is no result, we still might have some objects whose address
     were taken, so we need to make sure they stay around.  */
  if (x == 0)
    {
      for (p = *temp_slots_at_level (temp_slot_level); p; p = next)
	{
	  next = p->next;

	  if (p->addr_taken)
	    move_slot_to_level (p, temp_slot_level - 1);
	}

      return;
    }

  /* If X is a register that is being used as a pointer, see if we have
     a temporary slot we know it points to.  To be consistent with
     the code below, we really should preserve all non-kept slots
     if we can't find a match, but that seems to be much too costly.  */
  if (REG_P (x) && REG_POINTER (x))
    p = find_temp_slot_from_address (x);

  /* If X is not in memory or is at a constant address, it cannot be in
     a temporary slot, but it can contain something whose address was
     taken.  */
  if (p == 0 && (!MEM_P (x) || CONSTANT_P (XEXP (x, 0))))
    {
      for (p = *temp_slots_at_level (temp_slot_level); p; p = next)
	{
	  next = p->next;

	  if (p->addr_taken)
	    move_slot_to_level (p, temp_slot_level - 1);
	}

      return;
    }

  /* First see if we can find a match.  */
  if (p == 0)
    p = find_temp_slot_from_address (XEXP (x, 0));

  if (p != 0)
    {
      /* Move everything at our level whose address was taken to our new
	 level in case we used its address.  */
      struct temp_slot *q;

      if (p->level == temp_slot_level)
	{
	  for (q = *temp_slots_at_level (temp_slot_level); q; q = next)
	    {
	      next = q->next;

	      if (p != q && q->addr_taken)
		move_slot_to_level (q, temp_slot_level - 1);
	    }

	  move_slot_to_level (p, temp_slot_level - 1);
	  p->addr_taken = 0;
	}
      return;
    }

  /* Otherwise, preserve all non-kept slots at this level.  */
  for (p = *temp_slots_at_level (temp_slot_level); p; p = next)
    {
      next = p->next;

      if (!p->keep)
	move_slot_to_level (p, temp_slot_level - 1);
    }
}

/* Free all temporaries used so far.  This is normally called at the
   end of generating code for a statement.  */

void
free_temp_slots (void)
{
  struct temp_slot *p, *next;

  for (p = *temp_slots_at_level (temp_slot_level); p; p = next)
    {
      next = p->next;

      if (!p->keep)
	make_slot_available (p);
    }

  combine_temp_slots ();
}

/* Push deeper into the nesting level for stack temporaries.  */

void
push_temp_slots (void)
{
  temp_slot_level++;
}

/* Pop a temporary nesting level.  All slots in use in the current level
   are freed.  */

void
pop_temp_slots (void)
{
  struct temp_slot *p, *next;

  for (p = *temp_slots_at_level (temp_slot_level); p; p = next)
    {
      next = p->next;
      make_slot_available (p);
    }

  combine_temp_slots ();

  temp_slot_level--;
}

/* Initialize temporary slots.  */

void
init_temp_slots (void)
{
  /* We have not allocated any temporaries yet.  */
  avail_temp_slots = 0;
  used_temp_slots = 0;
  temp_slot_level = 0;
}

/* These routines are responsible for converting virtual register references
   to the actual hard register references once RTL generation is complete.

   The following four variables are used for communication between the
   routines.  They contain the offsets of the virtual registers from their
   respective hard registers.  */

static int in_arg_offset;
static int var_offset;
static int dynamic_offset;
static int out_arg_offset;
static int cfa_offset;

/* In most machines, the stack pointer register is equivalent to the bottom
   of the stack.  */

#ifndef STACK_POINTER_OFFSET
#define STACK_POINTER_OFFSET	0
#endif

/* If not defined, pick an appropriate default for the offset of dynamically
   allocated memory depending on the value of ACCUMULATE_OUTGOING_ARGS,
   REG_PARM_STACK_SPACE, and OUTGOING_REG_PARM_STACK_SPACE.  */

#ifndef STACK_DYNAMIC_OFFSET

/* The bottom of the stack points to the actual arguments.  If
   REG_PARM_STACK_SPACE is defined, this includes the space for the register
   parameters.  However, if OUTGOING_REG_PARM_STACK space is not defined,
   stack space for register parameters is not pushed by the caller, but
   rather part of the fixed stack areas and hence not included in
   `current_function_outgoing_args_size'.  Nevertheless, we must allow
   for it when allocating stack dynamic objects.  */

#if defined(REG_PARM_STACK_SPACE) && ! defined(OUTGOING_REG_PARM_STACK_SPACE)
#define STACK_DYNAMIC_OFFSET(FNDECL)	\
((ACCUMULATE_OUTGOING_ARGS						      \
  ? (current_function_outgoing_args_size + REG_PARM_STACK_SPACE (FNDECL)) : 0)\
 + (STACK_POINTER_OFFSET))						      \

#else
#define STACK_DYNAMIC_OFFSET(FNDECL)	\
((ACCUMULATE_OUTGOING_ARGS ? current_function_outgoing_args_size : 0)	      \
 + (STACK_POINTER_OFFSET))
#endif
#endif


/* Given a piece of RTX and a pointer to a HOST_WIDE_INT, if the RTX
   is a virtual register, return the equivalent hard register and set the
   offset indirectly through the pointer.  Otherwise, return 0.  */

static rtx
instantiate_new_reg (rtx x, HOST_WIDE_INT *poffset)
{
  rtx new;
  HOST_WIDE_INT offset;

  if (x == virtual_incoming_args_rtx)
    new = arg_pointer_rtx, offset = in_arg_offset;
  else if (x == virtual_stack_vars_rtx)
    new = frame_pointer_rtx, offset = var_offset;
  else if (x == virtual_stack_dynamic_rtx)
    new = stack_pointer_rtx, offset = dynamic_offset;
  else if (x == virtual_outgoing_args_rtx)
    new = stack_pointer_rtx, offset = out_arg_offset;
  else if (x == virtual_cfa_rtx)
    {
#ifdef FRAME_POINTER_CFA_OFFSET
      new = frame_pointer_rtx;
#else
      new = arg_pointer_rtx;
#endif
      offset = cfa_offset;
    }
  else
    return NULL_RTX;

  *poffset = offset;
  return new;
}

/* A subroutine of instantiate_virtual_regs, called via for_each_rtx.
   Instantiate any virtual registers present inside of *LOC.  The expression
   is simplified, as much as possible, but is not to be considered "valid"
   in any sense implied by the target.  If any change is made, set CHANGED
   to true.  */

static int
instantiate_virtual_regs_in_rtx (rtx *loc, void *data)
{
  HOST_WIDE_INT offset;
  bool *changed = (bool *) data;
  rtx x, new;

  x = *loc;
  if (x == 0)
    return 0;

  switch (GET_CODE (x))
    {
    case REG:
      new = instantiate_new_reg (x, &offset);
      if (new)
	{
	  *loc = plus_constant (new, offset);
	  if (changed)
	    *changed = true;
	}
      return -1;

    case PLUS:
      new = instantiate_new_reg (XEXP (x, 0), &offset);
      if (new)
	{
	  new = plus_constant (new, offset);
	  *loc = simplify_gen_binary (PLUS, GET_MODE (x), new, XEXP (x, 1));
	  if (changed)
	    *changed = true;
	  return -1;
	}

      /* FIXME -- from old code */
	  /* If we have (plus (subreg (virtual-reg)) (const_int)), we know
	     we can commute the PLUS and SUBREG because pointers into the
	     frame are well-behaved.  */
      break;

    default:
      break;
    }

  return 0;
}

/* A subroutine of instantiate_virtual_regs_in_insn.  Return true if X
   matches the predicate for insn CODE operand OPERAND.  */

static int
safe_insn_predicate (int code, int operand, rtx x)
{
  const struct insn_operand_data *op_data;

  if (code < 0)
    return true;

  op_data = &insn_data[code].operand[operand];
  if (op_data->predicate == NULL)
    return true;

  return op_data->predicate (x, op_data->mode);
}

/* A subroutine of instantiate_virtual_regs.  Instantiate any virtual
   registers present inside of insn.  The result will be a valid insn.  */

static void
instantiate_virtual_regs_in_insn (rtx insn)
{
  HOST_WIDE_INT offset;
  int insn_code, i;
  bool any_change = false;
  rtx set, new, x, seq;

  /* There are some special cases to be handled first.  */
  set = single_set (insn);
  if (set)
    {
      /* We're allowed to assign to a virtual register.  This is interpreted
	 to mean that the underlying register gets assigned the inverse
	 transformation.  This is used, for example, in the handling of
	 non-local gotos.  */
      new = instantiate_new_reg (SET_DEST (set), &offset);
      if (new)
	{
	  start_sequence ();

	  for_each_rtx (&SET_SRC (set), instantiate_virtual_regs_in_rtx, NULL);
	  x = simplify_gen_binary (PLUS, GET_MODE (new), SET_SRC (set),
				   GEN_INT (-offset));
	  x = force_operand (x, new);
	  if (x != new)
	    emit_move_insn (new, x);

	  seq = get_insns ();
	  end_sequence ();

	  emit_insn_before (seq, insn);
	  delete_insn (insn);
	  return;
	}

      /* Handle a straight copy from a virtual register by generating a
	 new add insn.  The difference between this and falling through
	 to the generic case is avoiding a new pseudo and eliminating a
	 move insn in the initial rtl stream.  */
      new = instantiate_new_reg (SET_SRC (set), &offset);
      if (new && offset != 0
	  && REG_P (SET_DEST (set))
	  && REGNO (SET_DEST (set)) > LAST_VIRTUAL_REGISTER)
	{
	  start_sequence ();

	  x = expand_simple_binop (GET_MODE (SET_DEST (set)), PLUS,
				   new, GEN_INT (offset), SET_DEST (set),
				   1, OPTAB_LIB_WIDEN);
	  if (x != SET_DEST (set))
	    emit_move_insn (SET_DEST (set), x);

	  seq = get_insns ();
	  end_sequence ();

	  emit_insn_before (seq, insn);
	  delete_insn (insn);
	  return;
	}

      extract_insn (insn);
      insn_code = INSN_CODE (insn);

      /* Handle a plus involving a virtual register by determining if the
	 operands remain valid if they're modified in place.  */
      if (GET_CODE (SET_SRC (set)) == PLUS
	  && recog_data.n_operands >= 3
	  && recog_data.operand_loc[1] == &XEXP (SET_SRC (set), 0)
	  && recog_data.operand_loc[2] == &XEXP (SET_SRC (set), 1)
	  && GET_CODE (recog_data.operand[2]) == CONST_INT
	  && (new = instantiate_new_reg (recog_data.operand[1], &offset)))
	{
	  offset += INTVAL (recog_data.operand[2]);

	  /* If the sum is zero, then replace with a plain move.  */
	  if (offset == 0
	      && REG_P (SET_DEST (set))
	      && REGNO (SET_DEST (set)) > LAST_VIRTUAL_REGISTER)
	    {
	      start_sequence ();
	      emit_move_insn (SET_DEST (set), new);
	      seq = get_insns ();
	      end_sequence ();

	      emit_insn_before (seq, insn);
	      delete_insn (insn);
	      return;
	    }

	  x = gen_int_mode (offset, recog_data.operand_mode[2]);

	  /* Using validate_change and apply_change_group here leaves
	     recog_data in an invalid state.  Since we know exactly what
	     we want to check, do those two by hand.  */
	  if (safe_insn_predicate (insn_code, 1, new)
	      && safe_insn_predicate (insn_code, 2, x))
	    {
	      *recog_data.operand_loc[1] = recog_data.operand[1] = new;
	      *recog_data.operand_loc[2] = recog_data.operand[2] = x;
	      any_change = true;

	      /* Fall through into the regular operand fixup loop in
		 order to take care of operands other than 1 and 2.  */
	    }
	}
    }
  else
    {
      extract_insn (insn);
      insn_code = INSN_CODE (insn);
    }

  /* In the general case, we expect virtual registers to appear only in
     operands, and then only as either bare registers or inside memories.  */
  for (i = 0; i < recog_data.n_operands; ++i)
    {
      x = recog_data.operand[i];
      switch (GET_CODE (x))
	{
	case MEM:
	  {
	    rtx addr = XEXP (x, 0);
	    bool changed = false;

	    for_each_rtx (&addr, instantiate_virtual_regs_in_rtx, &changed);
	    if (!changed)
	      continue;

	    start_sequence ();
	    x = replace_equiv_address (x, addr);
	    seq = get_insns ();
	    end_sequence ();
	    if (seq)
	      emit_insn_before (seq, insn);
	  }
	  break;

	case REG:
	  new = instantiate_new_reg (x, &offset);
	  if (new == NULL)
	    continue;
	  if (offset == 0)
	    x = new;
	  else
	    {
	      start_sequence ();

	      /* Careful, special mode predicates may have stuff in
		 insn_data[insn_code].operand[i].mode that isn't useful
		 to us for computing a new value.  */
	      /* ??? Recognize address_operand and/or "p" constraints
		 to see if (plus new offset) is a valid before we put
		 this through expand_simple_binop.  */
	      x = expand_simple_binop (GET_MODE (x), PLUS, new,
				       GEN_INT (offset), NULL_RTX,
				       1, OPTAB_LIB_WIDEN);
	      seq = get_insns ();
	      end_sequence ();
	      emit_insn_before (seq, insn);
	    }
	  break;

	case SUBREG:
	  new = instantiate_new_reg (SUBREG_REG (x), &offset);
	  if (new == NULL)
	    continue;
	  if (offset != 0)
	    {
	      start_sequence ();
	      new = expand_simple_binop (GET_MODE (new), PLUS, new,
					 GEN_INT (offset), NULL_RTX,
					 1, OPTAB_LIB_WIDEN);
	      seq = get_insns ();
	      end_sequence ();
	      emit_insn_before (seq, insn);
	    }
	  x = simplify_gen_subreg (recog_data.operand_mode[i], new,
				   GET_MODE (new), SUBREG_BYTE (x));
	  break;

	default:
	  continue;
	}

      /* At this point, X contains the new value for the operand.
	 Validate the new value vs the insn predicate.  Note that
	 asm insns will have insn_code -1 here.  */
      if (!safe_insn_predicate (insn_code, i, x))
	{
	  start_sequence ();
	  x = force_reg (insn_data[insn_code].operand[i].mode, x);
	  seq = get_insns ();
	  end_sequence ();
	  if (seq)
	    emit_insn_before (seq, insn);
	}

      *recog_data.operand_loc[i] = recog_data.operand[i] = x;
      any_change = true;
    }

  if (any_change)
    {
      /* Propagate operand changes into the duplicates.  */
      for (i = 0; i < recog_data.n_dups; ++i)
	*recog_data.dup_loc[i]
	  = recog_data.operand[(unsigned)recog_data.dup_num[i]];

      /* Force re-recognition of the instruction for validation.  */
      INSN_CODE (insn) = -1;
    }

  if (asm_noperands (PATTERN (insn)) >= 0)
    {
      if (!check_asm_operands (PATTERN (insn)))
	{
	  error_for_asm (insn, "impossible constraint in %<asm%>");
	  delete_insn (insn);
	}
    }
  else
    {
      if (recog_memoized (insn) < 0)
	fatal_insn_not_found (insn);
    }
}

/* Subroutine of instantiate_decls.  Given RTL representing a decl,
   do any instantiation required.  */

static void
instantiate_decl (rtx x)
{
  rtx addr;

  if (x == 0)
    return;

  /* If this is a CONCAT, recurse for the pieces.  */
  if (GET_CODE (x) == CONCAT)
    {
      instantiate_decl (XEXP (x, 0));
      instantiate_decl (XEXP (x, 1));
      return;
    }

  /* If this is not a MEM, no need to do anything.  Similarly if the
     address is a constant or a register that is not a virtual register.  */
  if (!MEM_P (x))
    return;

  addr = XEXP (x, 0);
  if (CONSTANT_P (addr)
      || (REG_P (addr)
	  && (REGNO (addr) < FIRST_VIRTUAL_REGISTER
	      || REGNO (addr) > LAST_VIRTUAL_REGISTER)))
    return;

  for_each_rtx (&XEXP (x, 0), instantiate_virtual_regs_in_rtx, NULL);
}

/* Helper for instantiate_decls called via walk_tree: Process all decls
   in the given DECL_VALUE_EXPR.  */

static tree
instantiate_expr (tree *tp, int *walk_subtrees, void *data ATTRIBUTE_UNUSED)
{
  tree t = *tp;
  if (! EXPR_P (t))
    {
      *walk_subtrees = 0;
      if (DECL_P (t) && DECL_RTL_SET_P (t))
	instantiate_decl (DECL_RTL (t));
    }
  return NULL;
}

/* Subroutine of instantiate_decls: Process all decls in the given
   BLOCK node and all its subblocks.  */

static void
instantiate_decls_1 (tree let)
{
  tree t;

  for (t = BLOCK_VARS (let); t; t = TREE_CHAIN (t))
    {
      if (DECL_RTL_SET_P (t))
	instantiate_decl (DECL_RTL (t));
      if (TREE_CODE (t) == VAR_DECL && DECL_HAS_VALUE_EXPR_P (t))
	{
	  tree v = DECL_VALUE_EXPR (t);
	  walk_tree (&v, instantiate_expr, NULL, NULL);
	}
    }

  /* Process all subblocks.  */
  for (t = BLOCK_SUBBLOCKS (let); t; t = TREE_CHAIN (t))
    instantiate_decls_1 (t);
}

/* Scan all decls in FNDECL (both variables and parameters) and instantiate
   all virtual registers in their DECL_RTL's.  */

static void
instantiate_decls (tree fndecl)
{
  tree decl;

  /* Process all parameters of the function.  */
  for (decl = DECL_ARGUMENTS (fndecl); decl; decl = TREE_CHAIN (decl))
    {
      instantiate_decl (DECL_RTL (decl));
      instantiate_decl (DECL_INCOMING_RTL (decl));
      if (DECL_HAS_VALUE_EXPR_P (decl))
	{
	  tree v = DECL_VALUE_EXPR (decl);
	  walk_tree (&v, instantiate_expr, NULL, NULL);
	}
    }

  /* Now process all variables defined in the function or its subblocks.  */
  instantiate_decls_1 (DECL_INITIAL (fndecl));
}

/* Pass through the INSNS of function FNDECL and convert virtual register
   references to hard register references.  */

static unsigned int
instantiate_virtual_regs (void)
{
  rtx insn;

  /* Compute the offsets to use for this function.  */
  in_arg_offset = FIRST_PARM_OFFSET (current_function_decl);
  var_offset = STARTING_FRAME_OFFSET;
  dynamic_offset = STACK_DYNAMIC_OFFSET (current_function_decl);
  out_arg_offset = STACK_POINTER_OFFSET;
#ifdef FRAME_POINTER_CFA_OFFSET
  cfa_offset = FRAME_POINTER_CFA_OFFSET (current_function_decl);
#else
  cfa_offset = ARG_POINTER_CFA_OFFSET (current_function_decl);
#endif

  /* Initialize recognition, indicating that volatile is OK.  */
  init_recog ();

  /* Scan through all the insns, instantiating every virtual register still
     present.  */
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      {
	/* These patterns in the instruction stream can never be recognized.
	   Fortunately, they shouldn't contain virtual registers either.  */
	if (GET_CODE (PATTERN (insn)) == USE
	    || GET_CODE (PATTERN (insn)) == CLOBBER
	    || GET_CODE (PATTERN (insn)) == ADDR_VEC
	    || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC
	    || GET_CODE (PATTERN (insn)) == ASM_INPUT)
	  continue;

	instantiate_virtual_regs_in_insn (insn);

	if (INSN_DELETED_P (insn))
	  continue;

	for_each_rtx (&REG_NOTES (insn), instantiate_virtual_regs_in_rtx, NULL);

	/* Instantiate any virtual registers in CALL_INSN_FUNCTION_USAGE.  */
	if (GET_CODE (insn) == CALL_INSN)
	  for_each_rtx (&CALL_INSN_FUNCTION_USAGE (insn),
			instantiate_virtual_regs_in_rtx, NULL);
      }

  /* Instantiate the virtual registers in the DECLs for debugging purposes.  */
  instantiate_decls (current_function_decl);

  /* Indicate that, from now on, assign_stack_local should use
     frame_pointer_rtx.  */
  virtuals_instantiated = 1;
  return 0;
}

struct tree_opt_pass pass_instantiate_virtual_regs =
{
  "vregs",                              /* name */
  NULL,                                 /* gate */
  instantiate_virtual_regs,             /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  0                                     /* letter */
};


/* Return 1 if EXP is an aggregate type (or a value with aggregate type).
   This means a type for which function calls must pass an address to the
   function or get an address back from the function.
   EXP may be a type node or an expression (whose type is tested).  */

int
aggregate_value_p (tree exp, tree fntype)
{
  int i, regno, nregs;
  rtx reg;

  tree type = (TYPE_P (exp)) ? exp : TREE_TYPE (exp);

  /* DECL node associated with FNTYPE when relevant, which we might need to
     check for by-invisible-reference returns, typically for CALL_EXPR input
     EXPressions.  */
  tree fndecl = NULL_TREE;
  
  if (fntype)
    switch (TREE_CODE (fntype))
      {
      case CALL_EXPR:
	fndecl = get_callee_fndecl (fntype);
	fntype = fndecl ? TREE_TYPE (fndecl) : 0;
	break;
      case FUNCTION_DECL:
	fndecl = fntype;
	fntype = TREE_TYPE (fndecl);
	break;
      case FUNCTION_TYPE:
      case METHOD_TYPE:
        break;
      case IDENTIFIER_NODE:
	fntype = 0;
	break;
      default:
	/* We don't expect other rtl types here.  */
	gcc_unreachable ();
      }

  if (TREE_CODE (type) == VOID_TYPE)
    return 0;

  /* If the front end has decided that this needs to be passed by
     reference, do so.  */
  if ((TREE_CODE (exp) == PARM_DECL || TREE_CODE (exp) == RESULT_DECL)
      && DECL_BY_REFERENCE (exp))
    return 1;

  /* If the EXPression is a CALL_EXPR, honor DECL_BY_REFERENCE set on the
     called function RESULT_DECL, meaning the function returns in memory by
     invisible reference.  This check lets front-ends not set TREE_ADDRESSABLE
     on the function type, which used to be the way to request such a return
     mechanism but might now be causing troubles at gimplification time if
     temporaries with the function type need to be created.  */
  if (TREE_CODE (exp) == CALL_EXPR && fndecl && DECL_RESULT (fndecl)
      && DECL_BY_REFERENCE (DECL_RESULT (fndecl)))
    return 1;
      
  if (targetm.calls.return_in_memory (type, fntype))
    return 1;
  /* Types that are TREE_ADDRESSABLE must be constructed in memory,
     and thus can't be returned in registers.  */
  if (TREE_ADDRESSABLE (type))
    return 1;
  if (flag_pcc_struct_return && AGGREGATE_TYPE_P (type))
    return 1;
  /* Make sure we have suitable call-clobbered regs to return
     the value in; if not, we must return it in memory.  */
  reg = hard_function_value (type, 0, fntype, 0);

  /* If we have something other than a REG (e.g. a PARALLEL), then assume
     it is OK.  */
  if (!REG_P (reg))
    return 0;

  regno = REGNO (reg);
  nregs = hard_regno_nregs[regno][TYPE_MODE (type)];
  for (i = 0; i < nregs; i++)
    if (! call_used_regs[regno + i])
      return 1;
  return 0;
}

/* Return true if we should assign DECL a pseudo register; false if it
   should live on the local stack.  */

bool
use_register_for_decl (tree decl)
{
  /* Honor volatile.  */
  if (TREE_SIDE_EFFECTS (decl))
    return false;

  /* Honor addressability.  */
  if (TREE_ADDRESSABLE (decl))
    return false;

  /* Only register-like things go in registers.  */
  if (DECL_MODE (decl) == BLKmode)
    return false;

  /* If -ffloat-store specified, don't put explicit float variables
     into registers.  */
  /* ??? This should be checked after DECL_ARTIFICIAL, but tree-ssa
     propagates values across these stores, and it probably shouldn't.  */
  if (flag_float_store && FLOAT_TYPE_P (TREE_TYPE (decl)))
    return false;

  /* If we're not interested in tracking debugging information for
     this decl, then we can certainly put it in a register.  */
  if (DECL_IGNORED_P (decl))
    return true;

  return (optimize || DECL_REGISTER (decl));
}

/* Return true if TYPE should be passed by invisible reference.  */

bool
pass_by_reference (CUMULATIVE_ARGS *ca, enum machine_mode mode,
		   tree type, bool named_arg)
{
  if (type)
    {
      /* If this type contains non-trivial constructors, then it is
	 forbidden for the middle-end to create any new copies.  */
      if (TREE_ADDRESSABLE (type))
	return true;

      /* GCC post 3.4 passes *all* variable sized types by reference.  */
      if (!TYPE_SIZE (type) || TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST)
	return true;
    }

  return targetm.calls.pass_by_reference (ca, mode, type, named_arg);
}

/* Return true if TYPE, which is passed by reference, should be callee
   copied instead of caller copied.  */

bool
reference_callee_copied (CUMULATIVE_ARGS *ca, enum machine_mode mode,
			 tree type, bool named_arg)
{
  if (type && TREE_ADDRESSABLE (type))
    return false;
  return targetm.calls.callee_copies (ca, mode, type, named_arg);
}

/* Structures to communicate between the subroutines of assign_parms.
   The first holds data persistent across all parameters, the second
   is cleared out for each parameter.  */

struct assign_parm_data_all
{
  CUMULATIVE_ARGS args_so_far;
  struct args_size stack_args_size;
  tree function_result_decl;
  tree orig_fnargs;
  rtx conversion_insns;
  HOST_WIDE_INT pretend_args_size;
  HOST_WIDE_INT extra_pretend_bytes;
  int reg_parm_stack_space;
};

struct assign_parm_data_one
{
  tree nominal_type;
  tree passed_type;
  rtx entry_parm;
  rtx stack_parm;
  enum machine_mode nominal_mode;
  enum machine_mode passed_mode;
  enum machine_mode promoted_mode;
  struct locate_and_pad_arg_data locate;
  int partial;
  BOOL_BITFIELD named_arg : 1;
  BOOL_BITFIELD passed_pointer : 1;
  BOOL_BITFIELD on_stack : 1;
  BOOL_BITFIELD loaded_in_reg : 1;
};

/* A subroutine of assign_parms.  Initialize ALL.  */

static void
assign_parms_initialize_all (struct assign_parm_data_all *all)
{
  tree fntype;

  memset (all, 0, sizeof (*all));

  fntype = TREE_TYPE (current_function_decl);

#ifdef INIT_CUMULATIVE_INCOMING_ARGS
  INIT_CUMULATIVE_INCOMING_ARGS (all->args_so_far, fntype, NULL_RTX);
#else
  INIT_CUMULATIVE_ARGS (all->args_so_far, fntype, NULL_RTX,
			current_function_decl, -1);
#endif

#ifdef REG_PARM_STACK_SPACE
  all->reg_parm_stack_space = REG_PARM_STACK_SPACE (current_function_decl);
#endif
}

/* If ARGS contains entries with complex types, split the entry into two
   entries of the component type.  Return a new list of substitutions are
   needed, else the old list.  */

static tree
split_complex_args (tree args)
{
  tree p;

  /* Before allocating memory, check for the common case of no complex.  */
  for (p = args; p; p = TREE_CHAIN (p))
    {
      tree type = TREE_TYPE (p);
      if (TREE_CODE (type) == COMPLEX_TYPE
	  && targetm.calls.split_complex_arg (type))
        goto found;
    }
  return args;

 found:
  args = copy_list (args);

  for (p = args; p; p = TREE_CHAIN (p))
    {
      tree type = TREE_TYPE (p);
      if (TREE_CODE (type) == COMPLEX_TYPE
	  && targetm.calls.split_complex_arg (type))
	{
	  tree decl;
	  tree subtype = TREE_TYPE (type);
	  bool addressable = TREE_ADDRESSABLE (p);

	  /* Rewrite the PARM_DECL's type with its component.  */
	  TREE_TYPE (p) = subtype;
	  DECL_ARG_TYPE (p) = TREE_TYPE (DECL_ARG_TYPE (p));
	  DECL_MODE (p) = VOIDmode;
	  DECL_SIZE (p) = NULL;
	  DECL_SIZE_UNIT (p) = NULL;
	  /* If this arg must go in memory, put it in a pseudo here.
	     We can't allow it to go in memory as per normal parms,
	     because the usual place might not have the imag part
	     adjacent to the real part.  */
	  DECL_ARTIFICIAL (p) = addressable;
	  DECL_IGNORED_P (p) = addressable;
	  TREE_ADDRESSABLE (p) = 0;
	  layout_decl (p, 0);

	  /* Build a second synthetic decl.  */
	  decl = build_decl (PARM_DECL, NULL_TREE, subtype);
	  DECL_ARG_TYPE (decl) = DECL_ARG_TYPE (p);
	  DECL_ARTIFICIAL (decl) = addressable;
	  DECL_IGNORED_P (decl) = addressable;
	  layout_decl (decl, 0);

	  /* Splice it in; skip the new decl.  */
	  TREE_CHAIN (decl) = TREE_CHAIN (p);
	  TREE_CHAIN (p) = decl;
	  p = decl;
	}
    }

  return args;
}

/* A subroutine of assign_parms.  Adjust the parameter list to incorporate
   the hidden struct return argument, and (abi willing) complex args.
   Return the new parameter list.  */

static tree
assign_parms_augmented_arg_list (struct assign_parm_data_all *all)
{
  tree fndecl = current_function_decl;
  tree fntype = TREE_TYPE (fndecl);
  tree fnargs = DECL_ARGUMENTS (fndecl);

  /* If struct value address is treated as the first argument, make it so.  */
  if (aggregate_value_p (DECL_RESULT (fndecl), fndecl)
      && ! current_function_returns_pcc_struct
      && targetm.calls.struct_value_rtx (TREE_TYPE (fndecl), 1) == 0)
    {
      tree type = build_pointer_type (TREE_TYPE (fntype));
      tree decl;

      decl = build_decl (PARM_DECL, NULL_TREE, type);
      DECL_ARG_TYPE (decl) = type;
      DECL_ARTIFICIAL (decl) = 1;
      DECL_IGNORED_P (decl) = 1;

      TREE_CHAIN (decl) = fnargs;
      fnargs = decl;
      all->function_result_decl = decl;
    }

  all->orig_fnargs = fnargs;

  /* If the target wants to split complex arguments into scalars, do so.  */
  if (targetm.calls.split_complex_arg)
    fnargs = split_complex_args (fnargs);

  return fnargs;
}

/* A subroutine of assign_parms.  Examine PARM and pull out type and mode
   data for the parameter.  Incorporate ABI specifics such as pass-by-
   reference and type promotion.  */

static void
assign_parm_find_data_types (struct assign_parm_data_all *all, tree parm,
			     struct assign_parm_data_one *data)
{
  tree nominal_type, passed_type;
  enum machine_mode nominal_mode, passed_mode, promoted_mode;

  memset (data, 0, sizeof (*data));

  /* NAMED_ARG is a mis-nomer.  We really mean 'non-varadic'. */
  if (!current_function_stdarg)
    data->named_arg = 1;  /* No varadic parms.  */
  else if (TREE_CHAIN (parm))
    data->named_arg = 1;  /* Not the last non-varadic parm. */
  else if (targetm.calls.strict_argument_naming (&all->args_so_far))
    data->named_arg = 1;  /* Only varadic ones are unnamed.  */
  else
    data->named_arg = 0;  /* Treat as varadic.  */

  nominal_type = TREE_TYPE (parm);
  passed_type = DECL_ARG_TYPE (parm);

  /* Look out for errors propagating this far.  Also, if the parameter's
     type is void then its value doesn't matter.  */
  if (TREE_TYPE (parm) == error_mark_node
      /* This can happen after weird syntax errors
	 or if an enum type is defined among the parms.  */
      || TREE_CODE (parm) != PARM_DECL
      || passed_type == NULL
      || VOID_TYPE_P (nominal_type))
    {
      nominal_type = passed_type = void_type_node;
      nominal_mode = passed_mode = promoted_mode = VOIDmode;
      goto egress;
    }

  /* Find mode of arg as it is passed, and mode of arg as it should be
     during execution of this function.  */
  passed_mode = TYPE_MODE (passed_type);
  nominal_mode = TYPE_MODE (nominal_type);

  /* If the parm is to be passed as a transparent union, use the type of
     the first field for the tests below.  We have already verified that
     the modes are the same.  */
  if (TREE_CODE (passed_type) == UNION_TYPE
      && TYPE_TRANSPARENT_UNION (passed_type))
    passed_type = TREE_TYPE (TYPE_FIELDS (passed_type));

  /* See if this arg was passed by invisible reference.  */
  if (pass_by_reference (&all->args_so_far, passed_mode,
			 passed_type, data->named_arg))
    {
      passed_type = nominal_type = build_pointer_type (passed_type);
      data->passed_pointer = true;
      passed_mode = nominal_mode = Pmode;
    }

  /* Find mode as it is passed by the ABI.  */
  promoted_mode = passed_mode;
  if (targetm.calls.promote_function_args (TREE_TYPE (current_function_decl)))
    {
      int unsignedp = TYPE_UNSIGNED (passed_type);
      promoted_mode = promote_mode (passed_type, promoted_mode,
				    &unsignedp, 1);
    }

 egress:
  data->nominal_type = nominal_type;
  data->passed_type = passed_type;
  data->nominal_mode = nominal_mode;
  data->passed_mode = passed_mode;
  data->promoted_mode = promoted_mode;
}

/* A subroutine of assign_parms.  Invoke setup_incoming_varargs.  */

static void
assign_parms_setup_varargs (struct assign_parm_data_all *all,
			    struct assign_parm_data_one *data, bool no_rtl)
{
  int varargs_pretend_bytes = 0;

  targetm.calls.setup_incoming_varargs (&all->args_so_far,
					data->promoted_mode,
					data->passed_type,
					&varargs_pretend_bytes, no_rtl);

  /* If the back-end has requested extra stack space, record how much is
     needed.  Do not change pretend_args_size otherwise since it may be
     nonzero from an earlier partial argument.  */
  if (varargs_pretend_bytes > 0)
    all->pretend_args_size = varargs_pretend_bytes;
}

/* A subroutine of assign_parms.  Set DATA->ENTRY_PARM corresponding to
   the incoming location of the current parameter.  */

static void
assign_parm_find_entry_rtl (struct assign_parm_data_all *all,
			    struct assign_parm_data_one *data)
{
  HOST_WIDE_INT pretend_bytes = 0;
  rtx entry_parm;
  bool in_regs;

  if (data->promoted_mode == VOIDmode)
    {
      data->entry_parm = data->stack_parm = const0_rtx;
      return;
    }

#ifdef FUNCTION_INCOMING_ARG
  entry_parm = FUNCTION_INCOMING_ARG (all->args_so_far, data->promoted_mode,
				      data->passed_type, data->named_arg);
#else
  entry_parm = FUNCTION_ARG (all->args_so_far, data->promoted_mode,
			     data->passed_type, data->named_arg);
#endif

  if (entry_parm == 0)
    data->promoted_mode = data->passed_mode;

  /* Determine parm's home in the stack, in case it arrives in the stack
     or we should pretend it did.  Compute the stack position and rtx where
     the argument arrives and its size.

     There is one complexity here:  If this was a parameter that would
     have been passed in registers, but wasn't only because it is
     __builtin_va_alist, we want locate_and_pad_parm to treat it as if
     it came in a register so that REG_PARM_STACK_SPACE isn't skipped.
     In this case, we call FUNCTION_ARG with NAMED set to 1 instead of 0
     as it was the previous time.  */
  in_regs = entry_parm != 0;
#ifdef STACK_PARMS_IN_REG_PARM_AREA
  in_regs = true;
#endif
  if (!in_regs && !data->named_arg)
    {
      if (targetm.calls.pretend_outgoing_varargs_named (&all->args_so_far))
	{
	  rtx tem;
#ifdef FUNCTION_INCOMING_ARG
	  tem = FUNCTION_INCOMING_ARG (all->args_so_far, data->promoted_mode,
				       data->passed_type, true);
#else
	  tem = FUNCTION_ARG (all->args_so_far, data->promoted_mode,
			      data->passed_type, true);
#endif
	  in_regs = tem != NULL;
	}
    }

  /* If this parameter was passed both in registers and in the stack, use
     the copy on the stack.  */
  if (targetm.calls.must_pass_in_stack (data->promoted_mode,
					data->passed_type))
    entry_parm = 0;

  if (entry_parm)
    {
      int partial;

      partial = targetm.calls.arg_partial_bytes (&all->args_so_far,
						 data->promoted_mode,
						 data->passed_type,
						 data->named_arg);
      data->partial = partial;

      /* The caller might already have allocated stack space for the
	 register parameters.  */
      if (partial != 0 && all->reg_parm_stack_space == 0)
	{
	  /* Part of this argument is passed in registers and part
	     is passed on the stack.  Ask the prologue code to extend
	     the stack part so that we can recreate the full value.

	     PRETEND_BYTES is the size of the registers we need to store.
	     CURRENT_FUNCTION_PRETEND_ARGS_SIZE is the amount of extra
	     stack space that the prologue should allocate.

	     Internally, gcc assumes that the argument pointer is aligned
	     to STACK_BOUNDARY bits.  This is used both for alignment
	     optimizations (see init_emit) and to locate arguments that are
	     aligned to more than PARM_BOUNDARY bits.  We must preserve this
	     invariant by rounding CURRENT_FUNCTION_PRETEND_ARGS_SIZE up to
	     a stack boundary.  */

	  /* We assume at most one partial arg, and it must be the first
	     argument on the stack.  */
	  gcc_assert (!all->extra_pretend_bytes && !all->pretend_args_size);

	  pretend_bytes = partial;
	  all->pretend_args_size = CEIL_ROUND (pretend_bytes, STACK_BYTES);

	  /* We want to align relative to the actual stack pointer, so
	     don't include this in the stack size until later.  */
	  all->extra_pretend_bytes = all->pretend_args_size;
	}
    }

  locate_and_pad_parm (data->promoted_mode, data->passed_type, in_regs,
		       entry_parm ? data->partial : 0, current_function_decl,
		       &all->stack_args_size, &data->locate);

  /* Adjust offsets to include the pretend args.  */
  pretend_bytes = all->extra_pretend_bytes - pretend_bytes;
  data->locate.slot_offset.constant += pretend_bytes;
  data->locate.offset.constant += pretend_bytes;

  data->entry_parm = entry_parm;
}

/* A subroutine of assign_parms.  If there is actually space on the stack
   for this parm, count it in stack_args_size and return true.  */

static bool
assign_parm_is_stack_parm (struct assign_parm_data_all *all,
			   struct assign_parm_data_one *data)
{
  /* Trivially true if we've no incoming register.  */
  if (data->entry_parm == NULL)
    ;
  /* Also true if we're partially in registers and partially not,
     since we've arranged to drop the entire argument on the stack.  */
  else if (data->partial != 0)
    ;
  /* Also true if the target says that it's passed in both registers
     and on the stack.  */
  else if (GET_CODE (data->entry_parm) == PARALLEL
	   && XEXP (XVECEXP (data->entry_parm, 0, 0), 0) == NULL_RTX)
    ;
  /* Also true if the target says that there's stack allocated for
     all register parameters.  */
  else if (all->reg_parm_stack_space > 0)
    ;
  /* Otherwise, no, this parameter has no ABI defined stack slot.  */
  else
    return false;

  all->stack_args_size.constant += data->locate.size.constant;
  if (data->locate.size.var)
    ADD_PARM_SIZE (all->stack_args_size, data->locate.size.var);

  return true;
}

/* A subroutine of assign_parms.  Given that this parameter is allocated
   stack space by the ABI, find it.  */

static void
assign_parm_find_stack_rtl (tree parm, struct assign_parm_data_one *data)
{
  rtx offset_rtx, stack_parm;
  unsigned int align, boundary;

  /* If we're passing this arg using a reg, make its stack home the
     aligned stack slot.  */
  if (data->entry_parm)
    offset_rtx = ARGS_SIZE_RTX (data->locate.slot_offset);
  else
    offset_rtx = ARGS_SIZE_RTX (data->locate.offset);

  stack_parm = current_function_internal_arg_pointer;
  if (offset_rtx != const0_rtx)
    stack_parm = gen_rtx_PLUS (Pmode, stack_parm, offset_rtx);
  stack_parm = gen_rtx_MEM (data->promoted_mode, stack_parm);

  set_mem_attributes (stack_parm, parm, 1);

  boundary = data->locate.boundary;
  align = BITS_PER_UNIT;

  /* If we're padding upward, we know that the alignment of the slot
     is FUNCTION_ARG_BOUNDARY.  If we're using slot_offset, we're
     intentionally forcing upward padding.  Otherwise we have to come
     up with a guess at the alignment based on OFFSET_RTX.  */
  if (data->locate.where_pad != downward || data->entry_parm)
    align = boundary;
  else if (GET_CODE (offset_rtx) == CONST_INT)
    {
      align = INTVAL (offset_rtx) * BITS_PER_UNIT | boundary;
      align = align & -align;
    }
  set_mem_align (stack_parm, align);

  if (data->entry_parm)
    set_reg_attrs_for_parm (data->entry_parm, stack_parm);

  data->stack_parm = stack_parm;
}

/* A subroutine of assign_parms.  Adjust DATA->ENTRY_RTL such that it's
   always valid and contiguous.  */

static void
assign_parm_adjust_entry_rtl (struct assign_parm_data_one *data)
{
  rtx entry_parm = data->entry_parm;
  rtx stack_parm = data->stack_parm;

  /* If this parm was passed part in regs and part in memory, pretend it
     arrived entirely in memory by pushing the register-part onto the stack.
     In the special case of a DImode or DFmode that is split, we could put
     it together in a pseudoreg directly, but for now that's not worth
     bothering with.  */
  if (data->partial != 0)
    {
      /* Handle calls that pass values in multiple non-contiguous
	 locations.  The Irix 6 ABI has examples of this.  */
      if (GET_CODE (entry_parm) == PARALLEL)
	emit_group_store (validize_mem (stack_parm), entry_parm,
			  data->passed_type, 
			  int_size_in_bytes (data->passed_type));
      else
	{
	  gcc_assert (data->partial % UNITS_PER_WORD == 0);
	  move_block_from_reg (REGNO (entry_parm), validize_mem (stack_parm),
			       data->partial / UNITS_PER_WORD);
	}

      entry_parm = stack_parm;
    }

  /* If we didn't decide this parm came in a register, by default it came
     on the stack.  */
  else if (entry_parm == NULL)
    entry_parm = stack_parm;

  /* When an argument is passed in multiple locations, we can't make use
     of this information, but we can save some copying if the whole argument
     is passed in a single register.  */
  else if (GET_CODE (entry_parm) == PARALLEL
	   && data->nominal_mode != BLKmode
	   && data->passed_mode != BLKmode)
    {
      size_t i, len = XVECLEN (entry_parm, 0);

      for (i = 0; i < len; i++)
	if (XEXP (XVECEXP (entry_parm, 0, i), 0) != NULL_RTX
	    && REG_P (XEXP (XVECEXP (entry_parm, 0, i), 0))
	    && (GET_MODE (XEXP (XVECEXP (entry_parm, 0, i), 0))
		== data->passed_mode)
	    && INTVAL (XEXP (XVECEXP (entry_parm, 0, i), 1)) == 0)
	  {
	    entry_parm = XEXP (XVECEXP (entry_parm, 0, i), 0);
	    break;
	  }
    }

  data->entry_parm = entry_parm;
}

/* A subroutine of assign_parms.  Adjust DATA->STACK_RTL such that it's
   always valid and properly aligned.  */

static void
assign_parm_adjust_stack_rtl (struct assign_parm_data_one *data)
{
  rtx stack_parm = data->stack_parm;

  /* If we can't trust the parm stack slot to be aligned enough for its
     ultimate type, don't use that slot after entry.  We'll make another
     stack slot, if we need one.  */
  if (stack_parm
      && ((STRICT_ALIGNMENT
	   && GET_MODE_ALIGNMENT (data->nominal_mode) > MEM_ALIGN (stack_parm))
	  || (data->nominal_type
	      && TYPE_ALIGN (data->nominal_type) > MEM_ALIGN (stack_parm)
	      && MEM_ALIGN (stack_parm) < PREFERRED_STACK_BOUNDARY)))
    stack_parm = NULL;

  /* If parm was passed in memory, and we need to convert it on entry,
     don't store it back in that same slot.  */
  else if (data->entry_parm == stack_parm
	   && data->nominal_mode != BLKmode
	   && data->nominal_mode != data->passed_mode)
    stack_parm = NULL;

  /* If stack protection is in effect for this function, don't leave any
     pointers in their passed stack slots.  */
  else if (cfun->stack_protect_guard
	   && (flag_stack_protect == 2
	       || data->passed_pointer
	       || POINTER_TYPE_P (data->nominal_type)))
    stack_parm = NULL;

  data->stack_parm = stack_parm;
}

/* A subroutine of assign_parms.  Return true if the current parameter
   should be stored as a BLKmode in the current frame.  */

static bool
assign_parm_setup_block_p (struct assign_parm_data_one *data)
{
  if (data->nominal_mode == BLKmode)
    return true;
  if (GET_CODE (data->entry_parm) == PARALLEL)
    return true;

#ifdef BLOCK_REG_PADDING
  /* Only assign_parm_setup_block knows how to deal with register arguments
     that are padded at the least significant end.  */
  if (REG_P (data->entry_parm)
      && GET_MODE_SIZE (data->promoted_mode) < UNITS_PER_WORD
      && (BLOCK_REG_PADDING (data->passed_mode, data->passed_type, 1)
	  == (BYTES_BIG_ENDIAN ? upward : downward)))
    return true;
#endif

  return false;
}

/* A subroutine of assign_parms.  Arrange for the parameter to be 
   present and valid in DATA->STACK_RTL.  */

static void
assign_parm_setup_block (struct assign_parm_data_all *all,
			 tree parm, struct assign_parm_data_one *data)
{
  rtx entry_parm = data->entry_parm;
  rtx stack_parm = data->stack_parm;
  HOST_WIDE_INT size;
  HOST_WIDE_INT size_stored;
  rtx orig_entry_parm = entry_parm;

  if (GET_CODE (entry_parm) == PARALLEL)
    entry_parm = emit_group_move_into_temps (entry_parm);

  /* If we've a non-block object that's nevertheless passed in parts,
     reconstitute it in register operations rather than on the stack.  */
  if (GET_CODE (entry_parm) == PARALLEL
      && data->nominal_mode != BLKmode)
    {
      rtx elt0 = XEXP (XVECEXP (orig_entry_parm, 0, 0), 0);

      if ((XVECLEN (entry_parm, 0) > 1
	   || hard_regno_nregs[REGNO (elt0)][GET_MODE (elt0)] > 1)
	  && use_register_for_decl (parm))
	{
	  rtx parmreg = gen_reg_rtx (data->nominal_mode);

	  push_to_sequence (all->conversion_insns);

	  /* For values returned in multiple registers, handle possible
	     incompatible calls to emit_group_store.

	     For example, the following would be invalid, and would have to
	     be fixed by the conditional below:

	     emit_group_store ((reg:SF), (parallel:DF))
	     emit_group_store ((reg:SI), (parallel:DI))

	     An example of this are doubles in e500 v2:
	     (parallel:DF (expr_list (reg:SI) (const_int 0))
	     (expr_list (reg:SI) (const_int 4))).  */
	  if (data->nominal_mode != data->passed_mode)
	    {
	      rtx t = gen_reg_rtx (GET_MODE (entry_parm));
	      emit_group_store (t, entry_parm, NULL_TREE,
				GET_MODE_SIZE (GET_MODE (entry_parm)));
	      convert_move (parmreg, t, 0);
	    }
	  else
	    emit_group_store (parmreg, entry_parm, data->nominal_type,
			      int_size_in_bytes (data->nominal_type));

	  all->conversion_insns = get_insns ();
	  end_sequence ();

	  SET_DECL_RTL (parm, parmreg);
	  return;
	}
    }

  size = int_size_in_bytes (data->passed_type);
  size_stored = CEIL_ROUND (size, UNITS_PER_WORD);
  if (stack_parm == 0)
    {
      DECL_ALIGN (parm) = MAX (DECL_ALIGN (parm), BITS_PER_WORD);
      stack_parm = assign_stack_local (BLKmode, size_stored,
				       DECL_ALIGN (parm));
      if (GET_MODE_SIZE (GET_MODE (entry_parm)) == size)
	PUT_MODE (stack_parm, GET_MODE (entry_parm));
      set_mem_attributes (stack_parm, parm, 1);
    }

  /* If a BLKmode arrives in registers, copy it to a stack slot.  Handle
     calls that pass values in multiple non-contiguous locations.  */
  if (REG_P (entry_parm) || GET_CODE (entry_parm) == PARALLEL)
    {
      rtx mem;

      /* Note that we will be storing an integral number of words.
	 So we have to be careful to ensure that we allocate an
	 integral number of words.  We do this above when we call
	 assign_stack_local if space was not allocated in the argument
	 list.  If it was, this will not work if PARM_BOUNDARY is not
	 a multiple of BITS_PER_WORD.  It isn't clear how to fix this
	 if it becomes a problem.  Exception is when BLKmode arrives
	 with arguments not conforming to word_mode.  */

      if (data->stack_parm == 0)
	;
      else if (GET_CODE (entry_parm) == PARALLEL)
	;
      else
	gcc_assert (!size || !(PARM_BOUNDARY % BITS_PER_WORD));

      mem = validize_mem (stack_parm);

      /* Handle values in multiple non-contiguous locations.  */
      if (GET_CODE (entry_parm) == PARALLEL)
	{
	  push_to_sequence (all->conversion_insns);
	  emit_group_store (mem, entry_parm, data->passed_type, size);
	  all->conversion_insns = get_insns ();
	  end_sequence ();
	}

      else if (size == 0)
	;

      /* If SIZE is that of a mode no bigger than a word, just use
	 that mode's store operation.  */
      else if (size <= UNITS_PER_WORD)
	{
	  enum machine_mode mode
	    = mode_for_size (size * BITS_PER_UNIT, MODE_INT, 0);

	  if (mode != BLKmode
#ifdef BLOCK_REG_PADDING
	      && (size == UNITS_PER_WORD
		  || (BLOCK_REG_PADDING (mode, data->passed_type, 1)
		      != (BYTES_BIG_ENDIAN ? upward : downward)))
#endif
	      )
	    {
	      rtx reg = gen_rtx_REG (mode, REGNO (entry_parm));
	      emit_move_insn (change_address (mem, mode, 0), reg);
	    }

	  /* Blocks smaller than a word on a BYTES_BIG_ENDIAN
	     machine must be aligned to the left before storing
	     to memory.  Note that the previous test doesn't
	     handle all cases (e.g. SIZE == 3).  */
	  else if (size != UNITS_PER_WORD
#ifdef BLOCK_REG_PADDING
		   && (BLOCK_REG_PADDING (mode, data->passed_type, 1)
		       == downward)
#else
		   && BYTES_BIG_ENDIAN
#endif
		   )
	    {
	      rtx tem, x;
	      int by = (UNITS_PER_WORD - size) * BITS_PER_UNIT;
	      rtx reg = gen_rtx_REG (word_mode, REGNO (entry_parm));

	      x = expand_shift (LSHIFT_EXPR, word_mode, reg,
				build_int_cst (NULL_TREE, by),
				NULL_RTX, 1);
	      tem = change_address (mem, word_mode, 0);
	      emit_move_insn (tem, x);
	    }
	  else
	    move_block_from_reg (REGNO (entry_parm), mem,
				 size_stored / UNITS_PER_WORD);
	}
      else
	move_block_from_reg (REGNO (entry_parm), mem,
			     size_stored / UNITS_PER_WORD);
    }
  else if (data->stack_parm == 0)
    {
      push_to_sequence (all->conversion_insns);
      emit_block_move (stack_parm, data->entry_parm, GEN_INT (size),
		       BLOCK_OP_NORMAL);
      all->conversion_insns = get_insns ();
      end_sequence ();
    }

  data->stack_parm = stack_parm;
  SET_DECL_RTL (parm, stack_parm);
}

/* A subroutine of assign_parms.  Allocate a pseudo to hold the current
   parameter.  Get it there.  Perform all ABI specified conversions.  */

static void
assign_parm_setup_reg (struct assign_parm_data_all *all, tree parm,
		       struct assign_parm_data_one *data)
{
  rtx parmreg;
  enum machine_mode promoted_nominal_mode;
  int unsignedp = TYPE_UNSIGNED (TREE_TYPE (parm));
  bool did_conversion = false;

  /* Store the parm in a pseudoregister during the function, but we may
     need to do it in a wider mode.  */

  /* This is not really promoting for a call.  However we need to be
     consistent with assign_parm_find_data_types and expand_expr_real_1.  */
  promoted_nominal_mode
    = promote_mode (data->nominal_type, data->nominal_mode, &unsignedp, 1);

  parmreg = gen_reg_rtx (promoted_nominal_mode);

  if (!DECL_ARTIFICIAL (parm))
    mark_user_reg (parmreg);

  /* If this was an item that we received a pointer to,
     set DECL_RTL appropriately.  */
  if (data->passed_pointer)
    {
      rtx x = gen_rtx_MEM (TYPE_MODE (TREE_TYPE (data->passed_type)), parmreg);
      set_mem_attributes (x, parm, 1);
      SET_DECL_RTL (parm, x);
    }
  else
    SET_DECL_RTL (parm, parmreg);

  /* Copy the value into the register.  */
  if (data->nominal_mode != data->passed_mode
      || promoted_nominal_mode != data->promoted_mode)
    {
      int save_tree_used;

      /* ENTRY_PARM has been converted to PROMOTED_MODE, its
	 mode, by the caller.  We now have to convert it to
	 NOMINAL_MODE, if different.  However, PARMREG may be in
	 a different mode than NOMINAL_MODE if it is being stored
	 promoted.

	 If ENTRY_PARM is a hard register, it might be in a register
	 not valid for operating in its mode (e.g., an odd-numbered
	 register for a DFmode).  In that case, moves are the only
	 thing valid, so we can't do a convert from there.  This
	 occurs when the calling sequence allow such misaligned
	 usages.

	 In addition, the conversion may involve a call, which could
	 clobber parameters which haven't been copied to pseudo
	 registers yet.  Therefore, we must first copy the parm to
	 a pseudo reg here, and save the conversion until after all
	 parameters have been moved.  */

      rtx tempreg = gen_reg_rtx (GET_MODE (data->entry_parm));

      emit_move_insn (tempreg, validize_mem (data->entry_parm));

      push_to_sequence (all->conversion_insns);
      tempreg = convert_to_mode (data->nominal_mode, tempreg, unsignedp);

      if (GET_CODE (tempreg) == SUBREG
	  && GET_MODE (tempreg) == data->nominal_mode
	  && REG_P (SUBREG_REG (tempreg))
	  && data->nominal_mode == data->passed_mode
	  && GET_MODE (SUBREG_REG (tempreg)) == GET_MODE (data->entry_parm)
	  && GET_MODE_SIZE (GET_MODE (tempreg))
	     < GET_MODE_SIZE (GET_MODE (data->entry_parm)))
	{
	  /* The argument is already sign/zero extended, so note it
	     into the subreg.  */
	  SUBREG_PROMOTED_VAR_P (tempreg) = 1;
	  SUBREG_PROMOTED_UNSIGNED_SET (tempreg, unsignedp);
	}

      /* TREE_USED gets set erroneously during expand_assignment.  */
      save_tree_used = TREE_USED (parm);
      expand_assignment (parm, make_tree (data->nominal_type, tempreg));
      TREE_USED (parm) = save_tree_used;
      all->conversion_insns = get_insns ();
      end_sequence ();

      did_conversion = true;
    }
  else
    emit_move_insn (parmreg, validize_mem (data->entry_parm));

  /* If we were passed a pointer but the actual value can safely live
     in a register, put it in one.  */
  if (data->passed_pointer
      && TYPE_MODE (TREE_TYPE (parm)) != BLKmode
      /* If by-reference argument was promoted, demote it.  */
      && (TYPE_MODE (TREE_TYPE (parm)) != GET_MODE (DECL_RTL (parm))
	  || use_register_for_decl (parm)))
    {
      /* We can't use nominal_mode, because it will have been set to
	 Pmode above.  We must use the actual mode of the parm.  */
      parmreg = gen_reg_rtx (TYPE_MODE (TREE_TYPE (parm)));
      mark_user_reg (parmreg);

      if (GET_MODE (parmreg) != GET_MODE (DECL_RTL (parm)))
	{
	  rtx tempreg = gen_reg_rtx (GET_MODE (DECL_RTL (parm)));
	  int unsigned_p = TYPE_UNSIGNED (TREE_TYPE (parm));

	  push_to_sequence (all->conversion_insns);
	  emit_move_insn (tempreg, DECL_RTL (parm));
	  tempreg = convert_to_mode (GET_MODE (parmreg), tempreg, unsigned_p);
	  emit_move_insn (parmreg, tempreg);
	  all->conversion_insns = get_insns ();
	  end_sequence ();

	  did_conversion = true;
	}
      else
	emit_move_insn (parmreg, DECL_RTL (parm));

      SET_DECL_RTL (parm, parmreg);

      /* STACK_PARM is the pointer, not the parm, and PARMREG is
	 now the parm.  */
      data->stack_parm = NULL;
    }

  /* Mark the register as eliminable if we did no conversion and it was
     copied from memory at a fixed offset, and the arg pointer was not
     copied to a pseudo-reg.  If the arg pointer is a pseudo reg or the
     offset formed an invalid address, such memory-equivalences as we
     make here would screw up life analysis for it.  */
  if (data->nominal_mode == data->passed_mode
      && !did_conversion
      && data->stack_parm != 0
      && MEM_P (data->stack_parm)
      && data->locate.offset.var == 0
      && reg_mentioned_p (virtual_incoming_args_rtx,
			  XEXP (data->stack_parm, 0)))
    {
      rtx linsn = get_last_insn ();
      rtx sinsn, set;

      /* Mark complex types separately.  */
      if (GET_CODE (parmreg) == CONCAT)
	{
	  enum machine_mode submode
	    = GET_MODE_INNER (GET_MODE (parmreg));
	  int regnor = REGNO (XEXP (parmreg, 0));
	  int regnoi = REGNO (XEXP (parmreg, 1));
	  rtx stackr = adjust_address_nv (data->stack_parm, submode, 0);
	  rtx stacki = adjust_address_nv (data->stack_parm, submode,
					  GET_MODE_SIZE (submode));

	  /* Scan backwards for the set of the real and
	     imaginary parts.  */
	  for (sinsn = linsn; sinsn != 0;
	       sinsn = prev_nonnote_insn (sinsn))
	    {
	      set = single_set (sinsn);
	      if (set == 0)
		continue;

	      if (SET_DEST (set) == regno_reg_rtx [regnoi])
		REG_NOTES (sinsn)
		  = gen_rtx_EXPR_LIST (REG_EQUIV, stacki,
				       REG_NOTES (sinsn));
	      else if (SET_DEST (set) == regno_reg_rtx [regnor])
		REG_NOTES (sinsn)
		  = gen_rtx_EXPR_LIST (REG_EQUIV, stackr,
				       REG_NOTES (sinsn));
	    }
	}
      else if ((set = single_set (linsn)) != 0
	       && SET_DEST (set) == parmreg)
	REG_NOTES (linsn)
	  = gen_rtx_EXPR_LIST (REG_EQUIV,
			       data->stack_parm, REG_NOTES (linsn));
    }

  /* For pointer data type, suggest pointer register.  */
  if (POINTER_TYPE_P (TREE_TYPE (parm)))
    mark_reg_pointer (parmreg,
		      TYPE_ALIGN (TREE_TYPE (TREE_TYPE (parm))));
}

/* A subroutine of assign_parms.  Allocate stack space to hold the current
   parameter.  Get it there.  Perform all ABI specified conversions.  */

static void
assign_parm_setup_stack (struct assign_parm_data_all *all, tree parm,
		         struct assign_parm_data_one *data)
{
  /* Value must be stored in the stack slot STACK_PARM during function
     execution.  */
  bool to_conversion = false;

  if (data->promoted_mode != data->nominal_mode)
    {
      /* Conversion is required.  */
      rtx tempreg = gen_reg_rtx (GET_MODE (data->entry_parm));

      emit_move_insn (tempreg, validize_mem (data->entry_parm));

      push_to_sequence (all->conversion_insns);
      to_conversion = true;

      data->entry_parm = convert_to_mode (data->nominal_mode, tempreg,
					  TYPE_UNSIGNED (TREE_TYPE (parm)));

      if (data->stack_parm)
	/* ??? This may need a big-endian conversion on sparc64.  */
	data->stack_parm
	  = adjust_address (data->stack_parm, data->nominal_mode, 0);
    }

  if (data->entry_parm != data->stack_parm)
    {
      rtx src, dest;

      if (data->stack_parm == 0)
	{
	  data->stack_parm
	    = assign_stack_local (GET_MODE (data->entry_parm),
				  GET_MODE_SIZE (GET_MODE (data->entry_parm)),
				  TYPE_ALIGN (data->passed_type));
	  set_mem_attributes (data->stack_parm, parm, 1);
	}

      dest = validize_mem (data->stack_parm);
      src = validize_mem (data->entry_parm);

      if (MEM_P (src))
	{
	  /* Use a block move to handle potentially misaligned entry_parm.  */
	  if (!to_conversion)
	    push_to_sequence (all->conversion_insns);
	  to_conversion = true;

	  emit_block_move (dest, src,
			   GEN_INT (int_size_in_bytes (data->passed_type)),
			   BLOCK_OP_NORMAL);
	}
      else
	emit_move_insn (dest, src);
    }

  if (to_conversion)
    {
      all->conversion_insns = get_insns ();
      end_sequence ();
    }

  SET_DECL_RTL (parm, data->stack_parm);
}

/* A subroutine of assign_parms.  If the ABI splits complex arguments, then
   undo the frobbing that we did in assign_parms_augmented_arg_list.  */

static void
assign_parms_unsplit_complex (struct assign_parm_data_all *all, tree fnargs)
{
  tree parm;
  tree orig_fnargs = all->orig_fnargs;

  for (parm = orig_fnargs; parm; parm = TREE_CHAIN (parm))
    {
      if (TREE_CODE (TREE_TYPE (parm)) == COMPLEX_TYPE
	  && targetm.calls.split_complex_arg (TREE_TYPE (parm)))
	{
	  rtx tmp, real, imag;
	  enum machine_mode inner = GET_MODE_INNER (DECL_MODE (parm));

	  real = DECL_RTL (fnargs);
	  imag = DECL_RTL (TREE_CHAIN (fnargs));
	  if (inner != GET_MODE (real))
	    {
	      real = gen_lowpart_SUBREG (inner, real);
	      imag = gen_lowpart_SUBREG (inner, imag);
	    }

	  if (TREE_ADDRESSABLE (parm))
	    {
	      rtx rmem, imem;
	      HOST_WIDE_INT size = int_size_in_bytes (TREE_TYPE (parm));

	      /* split_complex_arg put the real and imag parts in
		 pseudos.  Move them to memory.  */
	      tmp = assign_stack_local (DECL_MODE (parm), size,
					TYPE_ALIGN (TREE_TYPE (parm)));
	      set_mem_attributes (tmp, parm, 1);
	      rmem = adjust_address_nv (tmp, inner, 0);
	      imem = adjust_address_nv (tmp, inner, GET_MODE_SIZE (inner));
	      push_to_sequence (all->conversion_insns);
	      emit_move_insn (rmem, real);
	      emit_move_insn (imem, imag);
	      all->conversion_insns = get_insns ();
	      end_sequence ();
	    }
	  else
	    tmp = gen_rtx_CONCAT (DECL_MODE (parm), real, imag);
	  SET_DECL_RTL (parm, tmp);

	  real = DECL_INCOMING_RTL (fnargs);
	  imag = DECL_INCOMING_RTL (TREE_CHAIN (fnargs));
	  if (inner != GET_MODE (real))
	    {
	      real = gen_lowpart_SUBREG (inner, real);
	      imag = gen_lowpart_SUBREG (inner, imag);
	    }
	  tmp = gen_rtx_CONCAT (DECL_MODE (parm), real, imag);
	  set_decl_incoming_rtl (parm, tmp);
	  fnargs = TREE_CHAIN (fnargs);
	}
      else
	{
	  SET_DECL_RTL (parm, DECL_RTL (fnargs));
	  set_decl_incoming_rtl (parm, DECL_INCOMING_RTL (fnargs));

	  /* Set MEM_EXPR to the original decl, i.e. to PARM,
	     instead of the copy of decl, i.e. FNARGS.  */
	  if (DECL_INCOMING_RTL (parm) && MEM_P (DECL_INCOMING_RTL (parm)))
	    set_mem_expr (DECL_INCOMING_RTL (parm), parm);
	}

      fnargs = TREE_CHAIN (fnargs);
    }
}

/* Assign RTL expressions to the function's parameters.  This may involve
   copying them into registers and using those registers as the DECL_RTL.  */

static void
assign_parms (tree fndecl)
{
  struct assign_parm_data_all all;
  tree fnargs, parm;

  current_function_internal_arg_pointer
    = targetm.calls.internal_arg_pointer ();

  assign_parms_initialize_all (&all);
  fnargs = assign_parms_augmented_arg_list (&all);

  for (parm = fnargs; parm; parm = TREE_CHAIN (parm))
    {
      struct assign_parm_data_one data;

      /* Extract the type of PARM; adjust it according to ABI.  */
      assign_parm_find_data_types (&all, parm, &data);

      /* Early out for errors and void parameters.  */
      if (data.passed_mode == VOIDmode)
	{
	  SET_DECL_RTL (parm, const0_rtx);
	  DECL_INCOMING_RTL (parm) = DECL_RTL (parm);
	  continue;
	}

      if (current_function_stdarg && !TREE_CHAIN (parm))
	assign_parms_setup_varargs (&all, &data, false);

      /* Find out where the parameter arrives in this function.  */
      assign_parm_find_entry_rtl (&all, &data);

      /* Find out where stack space for this parameter might be.  */
      if (assign_parm_is_stack_parm (&all, &data))
	{
	  assign_parm_find_stack_rtl (parm, &data);
	  assign_parm_adjust_entry_rtl (&data);
	}

      /* Record permanently how this parm was passed.  */
      set_decl_incoming_rtl (parm, data.entry_parm);

      /* Update info on where next arg arrives in registers.  */
      FUNCTION_ARG_ADVANCE (all.args_so_far, data.promoted_mode,
			    data.passed_type, data.named_arg);

      assign_parm_adjust_stack_rtl (&data);

      if (assign_parm_setup_block_p (&data))
	assign_parm_setup_block (&all, parm, &data);
      else if (data.passed_pointer || use_register_for_decl (parm))
	assign_parm_setup_reg (&all, parm, &data);
      else
	assign_parm_setup_stack (&all, parm, &data);
    }

  if (targetm.calls.split_complex_arg && fnargs != all.orig_fnargs)
    assign_parms_unsplit_complex (&all, fnargs);

  /* Output all parameter conversion instructions (possibly including calls)
     now that all parameters have been copied out of hard registers.  */
  emit_insn (all.conversion_insns);

  /* If we are receiving a struct value address as the first argument, set up
     the RTL for the function result. As this might require code to convert
     the transmitted address to Pmode, we do this here to ensure that possible
     preliminary conversions of the address have been emitted already.  */
  if (all.function_result_decl)
    {
      tree result = DECL_RESULT (current_function_decl);
      rtx addr = DECL_RTL (all.function_result_decl);
      rtx x;

      if (DECL_BY_REFERENCE (result))
	x = addr;
      else
	{
	  addr = convert_memory_address (Pmode, addr);
	  x = gen_rtx_MEM (DECL_MODE (result), addr);
	  set_mem_attributes (x, result, 1);
	}
      SET_DECL_RTL (result, x);
    }

  /* We have aligned all the args, so add space for the pretend args.  */
  current_function_pretend_args_size = all.pretend_args_size;
  all.stack_args_size.constant += all.extra_pretend_bytes;
  current_function_args_size = all.stack_args_size.constant;

  /* Adjust function incoming argument size for alignment and
     minimum length.  */

#ifdef REG_PARM_STACK_SPACE
  current_function_args_size = MAX (current_function_args_size,
				    REG_PARM_STACK_SPACE (fndecl));
#endif

  current_function_args_size = CEIL_ROUND (current_function_args_size,
					   PARM_BOUNDARY / BITS_PER_UNIT);

#ifdef ARGS_GROW_DOWNWARD
  current_function_arg_offset_rtx
    = (all.stack_args_size.var == 0 ? GEN_INT (-all.stack_args_size.constant)
       : expand_expr (size_diffop (all.stack_args_size.var,
				   size_int (-all.stack_args_size.constant)),
		      NULL_RTX, VOIDmode, 0));
#else
  current_function_arg_offset_rtx = ARGS_SIZE_RTX (all.stack_args_size);
#endif

  /* See how many bytes, if any, of its args a function should try to pop
     on return.  */

  current_function_pops_args = RETURN_POPS_ARGS (fndecl, TREE_TYPE (fndecl),
						 current_function_args_size);

  /* For stdarg.h function, save info about
     regs and stack space used by the named args.  */

  current_function_args_info = all.args_so_far;

  /* Set the rtx used for the function return value.  Put this in its
     own variable so any optimizers that need this information don't have
     to include tree.h.  Do this here so it gets done when an inlined
     function gets output.  */

  current_function_return_rtx
    = (DECL_RTL_SET_P (DECL_RESULT (fndecl))
       ? DECL_RTL (DECL_RESULT (fndecl)) : NULL_RTX);

  /* If scalar return value was computed in a pseudo-reg, or was a named
     return value that got dumped to the stack, copy that to the hard
     return register.  */
  if (DECL_RTL_SET_P (DECL_RESULT (fndecl)))
    {
      tree decl_result = DECL_RESULT (fndecl);
      rtx decl_rtl = DECL_RTL (decl_result);

      if (REG_P (decl_rtl)
	  ? REGNO (decl_rtl) >= FIRST_PSEUDO_REGISTER
	  : DECL_REGISTER (decl_result))
	{
	  rtx real_decl_rtl;

	  real_decl_rtl = targetm.calls.function_value (TREE_TYPE (decl_result),
							fndecl, true);
	  REG_FUNCTION_VALUE_P (real_decl_rtl) = 1;
	  /* The delay slot scheduler assumes that current_function_return_rtx
	     holds the hard register containing the return value, not a
	     temporary pseudo.  */
	  current_function_return_rtx = real_decl_rtl;
	}
    }
}

/* A subroutine of gimplify_parameters, invoked via walk_tree.
   For all seen types, gimplify their sizes.  */

static tree
gimplify_parm_type (tree *tp, int *walk_subtrees, void *data)
{
  tree t = *tp;

  *walk_subtrees = 0;
  if (TYPE_P (t))
    {
      if (POINTER_TYPE_P (t))
	*walk_subtrees = 1;
      else if (TYPE_SIZE (t) && !TREE_CONSTANT (TYPE_SIZE (t))
	       && !TYPE_SIZES_GIMPLIFIED (t))
	{
	  gimplify_type_sizes (t, (tree *) data);
	  *walk_subtrees = 1;
	}
    }

  return NULL;
}

/* Gimplify the parameter list for current_function_decl.  This involves
   evaluating SAVE_EXPRs of variable sized parameters and generating code
   to implement callee-copies reference parameters.  Returns a list of
   statements to add to the beginning of the function, or NULL if nothing
   to do.  */

tree
gimplify_parameters (void)
{
  struct assign_parm_data_all all;
  tree fnargs, parm, stmts = NULL;

  assign_parms_initialize_all (&all);
  fnargs = assign_parms_augmented_arg_list (&all);

  for (parm = fnargs; parm; parm = TREE_CHAIN (parm))
    {
      struct assign_parm_data_one data;

      /* Extract the type of PARM; adjust it according to ABI.  */
      assign_parm_find_data_types (&all, parm, &data);

      /* Early out for errors and void parameters.  */
      if (data.passed_mode == VOIDmode || DECL_SIZE (parm) == NULL)
	continue;

      /* Update info on where next arg arrives in registers.  */
      FUNCTION_ARG_ADVANCE (all.args_so_far, data.promoted_mode,
			    data.passed_type, data.named_arg);

      /* ??? Once upon a time variable_size stuffed parameter list
	 SAVE_EXPRs (amongst others) onto a pending sizes list.  This
	 turned out to be less than manageable in the gimple world.
	 Now we have to hunt them down ourselves.  */
      walk_tree_without_duplicates (&data.passed_type,
				    gimplify_parm_type, &stmts);

      if (!TREE_CONSTANT (DECL_SIZE (parm)))
	{
	  gimplify_one_sizepos (&DECL_SIZE (parm), &stmts);
	  gimplify_one_sizepos (&DECL_SIZE_UNIT (parm), &stmts);
	}

      if (data.passed_pointer)
	{
          tree type = TREE_TYPE (data.passed_type);
	  if (reference_callee_copied (&all.args_so_far, TYPE_MODE (type),
				       type, data.named_arg))
	    {
	      tree local, t;

	      /* For constant sized objects, this is trivial; for
		 variable-sized objects, we have to play games.  */
	      if (TREE_CONSTANT (DECL_SIZE (parm)))
		{
		  local = create_tmp_var (type, get_name (parm));
		  DECL_IGNORED_P (local) = 0;
		}
	      else
		{
		  tree ptr_type, addr, args;

		  ptr_type = build_pointer_type (type);
		  addr = create_tmp_var (ptr_type, get_name (parm));
		  DECL_IGNORED_P (addr) = 0;
		  local = build_fold_indirect_ref (addr);

		  args = tree_cons (NULL, DECL_SIZE_UNIT (parm), NULL);
		  t = built_in_decls[BUILT_IN_ALLOCA];
		  t = build_function_call_expr (t, args);
		  t = fold_convert (ptr_type, t);
		  t = build2 (MODIFY_EXPR, void_type_node, addr, t);
		  gimplify_and_add (t, &stmts);
		}

	      t = build2 (MODIFY_EXPR, void_type_node, local, parm);
	      gimplify_and_add (t, &stmts);

	      SET_DECL_VALUE_EXPR (parm, local);
	      DECL_HAS_VALUE_EXPR_P (parm) = 1;
	    }
	}
    }

  return stmts;
}

/* Indicate whether REGNO is an incoming argument to the current function
   that was promoted to a wider mode.  If so, return the RTX for the
   register (to get its mode).  PMODE and PUNSIGNEDP are set to the mode
   that REGNO is promoted from and whether the promotion was signed or
   unsigned.  */

rtx
promoted_input_arg (unsigned int regno, enum machine_mode *pmode, int *punsignedp)
{
  tree arg;

  for (arg = DECL_ARGUMENTS (current_function_decl); arg;
       arg = TREE_CHAIN (arg))
    if (REG_P (DECL_INCOMING_RTL (arg))
	&& REGNO (DECL_INCOMING_RTL (arg)) == regno
	&& TYPE_MODE (DECL_ARG_TYPE (arg)) == TYPE_MODE (TREE_TYPE (arg)))
      {
	enum machine_mode mode = TYPE_MODE (TREE_TYPE (arg));
	int unsignedp = TYPE_UNSIGNED (TREE_TYPE (arg));

	mode = promote_mode (TREE_TYPE (arg), mode, &unsignedp, 1);
	if (mode == GET_MODE (DECL_INCOMING_RTL (arg))
	    && mode != DECL_MODE (arg))
	  {
	    *pmode = DECL_MODE (arg);
	    *punsignedp = unsignedp;
	    return DECL_INCOMING_RTL (arg);
	  }
      }

  return 0;
}


/* Compute the size and offset from the start of the stacked arguments for a
   parm passed in mode PASSED_MODE and with type TYPE.

   INITIAL_OFFSET_PTR points to the current offset into the stacked
   arguments.

   The starting offset and size for this parm are returned in
   LOCATE->OFFSET and LOCATE->SIZE, respectively.  When IN_REGS is
   nonzero, the offset is that of stack slot, which is returned in
   LOCATE->SLOT_OFFSET.  LOCATE->ALIGNMENT_PAD is the amount of
   padding required from the initial offset ptr to the stack slot.

   IN_REGS is nonzero if the argument will be passed in registers.  It will
   never be set if REG_PARM_STACK_SPACE is not defined.

   FNDECL is the function in which the argument was defined.

   There are two types of rounding that are done.  The first, controlled by
   FUNCTION_ARG_BOUNDARY, forces the offset from the start of the argument
   list to be aligned to the specific boundary (in bits).  This rounding
   affects the initial and starting offsets, but not the argument size.

   The second, controlled by FUNCTION_ARG_PADDING and PARM_BOUNDARY,
   optionally rounds the size of the parm to PARM_BOUNDARY.  The
   initial offset is not affected by this rounding, while the size always
   is and the starting offset may be.  */

/*  LOCATE->OFFSET will be negative for ARGS_GROW_DOWNWARD case;
    INITIAL_OFFSET_PTR is positive because locate_and_pad_parm's
    callers pass in the total size of args so far as
    INITIAL_OFFSET_PTR.  LOCATE->SIZE is always positive.  */

void
locate_and_pad_parm (enum machine_mode passed_mode, tree type, int in_regs,
		     int partial, tree fndecl ATTRIBUTE_UNUSED,
		     struct args_size *initial_offset_ptr,
		     struct locate_and_pad_arg_data *locate)
{
  tree sizetree;
  enum direction where_pad;
  unsigned int boundary;
  int reg_parm_stack_space = 0;
  int part_size_in_regs;

#ifdef REG_PARM_STACK_SPACE
  reg_parm_stack_space = REG_PARM_STACK_SPACE (fndecl);

  /* If we have found a stack parm before we reach the end of the
     area reserved for registers, skip that area.  */
  if (! in_regs)
    {
      if (reg_parm_stack_space > 0)
	{
	  if (initial_offset_ptr->var)
	    {
	      initial_offset_ptr->var
		= size_binop (MAX_EXPR, ARGS_SIZE_TREE (*initial_offset_ptr),
			      ssize_int (reg_parm_stack_space));
	      initial_offset_ptr->constant = 0;
	    }
	  else if (initial_offset_ptr->constant < reg_parm_stack_space)
	    initial_offset_ptr->constant = reg_parm_stack_space;
	}
    }
#endif /* REG_PARM_STACK_SPACE */

  part_size_in_regs = (reg_parm_stack_space == 0 ? partial : 0);

  sizetree
    = type ? size_in_bytes (type) : size_int (GET_MODE_SIZE (passed_mode));
  where_pad = FUNCTION_ARG_PADDING (passed_mode, type);
  boundary = FUNCTION_ARG_BOUNDARY (passed_mode, type);
  locate->where_pad = where_pad;
  locate->boundary = boundary;

  /* Remember if the outgoing parameter requires extra alignment on the
     calling function side.  */
  if (boundary > PREFERRED_STACK_BOUNDARY)
    boundary = PREFERRED_STACK_BOUNDARY;
  if (cfun->stack_alignment_needed < boundary)
    cfun->stack_alignment_needed = boundary;

#ifdef ARGS_GROW_DOWNWARD
  locate->slot_offset.constant = -initial_offset_ptr->constant;
  if (initial_offset_ptr->var)
    locate->slot_offset.var = size_binop (MINUS_EXPR, ssize_int (0),
					  initial_offset_ptr->var);

  {
    tree s2 = sizetree;
    if (where_pad != none
	&& (!host_integerp (sizetree, 1)
	    || (tree_low_cst (sizetree, 1) * BITS_PER_UNIT) % PARM_BOUNDARY))
      s2 = round_up (s2, PARM_BOUNDARY / BITS_PER_UNIT);
    SUB_PARM_SIZE (locate->slot_offset, s2);
  }

  locate->slot_offset.constant += part_size_in_regs;

  if (!in_regs
#ifdef REG_PARM_STACK_SPACE
      || REG_PARM_STACK_SPACE (fndecl) > 0
#endif
     )
    pad_to_arg_alignment (&locate->slot_offset, boundary,
			  &locate->alignment_pad);

  locate->size.constant = (-initial_offset_ptr->constant
			   - locate->slot_offset.constant);
  if (initial_offset_ptr->var)
    locate->size.var = size_binop (MINUS_EXPR,
				   size_binop (MINUS_EXPR,
					       ssize_int (0),
					       initial_offset_ptr->var),
				   locate->slot_offset.var);

  /* Pad_below needs the pre-rounded size to know how much to pad
     below.  */
  locate->offset = locate->slot_offset;
  if (where_pad == downward)
    pad_below (&locate->offset, passed_mode, sizetree);

#else /* !ARGS_GROW_DOWNWARD */
  if (!in_regs
#ifdef REG_PARM_STACK_SPACE
      || REG_PARM_STACK_SPACE (fndecl) > 0
#endif
      )
    pad_to_arg_alignment (initial_offset_ptr, boundary,
			  &locate->alignment_pad);
  locate->slot_offset = *initial_offset_ptr;

#ifdef PUSH_ROUNDING
  if (passed_mode != BLKmode)
    sizetree = size_int (PUSH_ROUNDING (TREE_INT_CST_LOW (sizetree)));
#endif

  /* Pad_below needs the pre-rounded size to know how much to pad below
     so this must be done before rounding up.  */
  locate->offset = locate->slot_offset;
  if (where_pad == downward)
    pad_below (&locate->offset, passed_mode, sizetree);

  if (where_pad != none
      && (!host_integerp (sizetree, 1)
	  || (tree_low_cst (sizetree, 1) * BITS_PER_UNIT) % PARM_BOUNDARY))
    sizetree = round_up (sizetree, PARM_BOUNDARY / BITS_PER_UNIT);

  ADD_PARM_SIZE (locate->size, sizetree);

  locate->size.constant -= part_size_in_regs;
#endif /* ARGS_GROW_DOWNWARD */
}

/* Round the stack offset in *OFFSET_PTR up to a multiple of BOUNDARY.
   BOUNDARY is measured in bits, but must be a multiple of a storage unit.  */

static void
pad_to_arg_alignment (struct args_size *offset_ptr, int boundary,
		      struct args_size *alignment_pad)
{
  tree save_var = NULL_TREE;
  HOST_WIDE_INT save_constant = 0;
  int boundary_in_bytes = boundary / BITS_PER_UNIT;
  HOST_WIDE_INT sp_offset = STACK_POINTER_OFFSET;

#ifdef SPARC_STACK_BOUNDARY_HACK
  /* ??? The SPARC port may claim a STACK_BOUNDARY higher than
     the real alignment of %sp.  However, when it does this, the
     alignment of %sp+STACK_POINTER_OFFSET is STACK_BOUNDARY.  */
  if (SPARC_STACK_BOUNDARY_HACK)
    sp_offset = 0;
#endif

  if (boundary > PARM_BOUNDARY && boundary > STACK_BOUNDARY)
    {
      save_var = offset_ptr->var;
      save_constant = offset_ptr->constant;
    }

  alignment_pad->var = NULL_TREE;
  alignment_pad->constant = 0;

  if (boundary > BITS_PER_UNIT)
    {
      if (offset_ptr->var)
	{
	  tree sp_offset_tree = ssize_int (sp_offset);
	  tree offset = size_binop (PLUS_EXPR,
				    ARGS_SIZE_TREE (*offset_ptr),
				    sp_offset_tree);
#ifdef ARGS_GROW_DOWNWARD
	  tree rounded = round_down (offset, boundary / BITS_PER_UNIT);
#else
	  tree rounded = round_up   (offset, boundary / BITS_PER_UNIT);
#endif

	  offset_ptr->var = size_binop (MINUS_EXPR, rounded, sp_offset_tree);
	  /* ARGS_SIZE_TREE includes constant term.  */
	  offset_ptr->constant = 0;
	  if (boundary > PARM_BOUNDARY && boundary > STACK_BOUNDARY)
	    alignment_pad->var = size_binop (MINUS_EXPR, offset_ptr->var,
					     save_var);
	}
      else
	{
	  offset_ptr->constant = -sp_offset +
#ifdef ARGS_GROW_DOWNWARD
	    FLOOR_ROUND (offset_ptr->constant + sp_offset, boundary_in_bytes);
#else
	    CEIL_ROUND (offset_ptr->constant + sp_offset, boundary_in_bytes);
#endif
	    if (boundary > PARM_BOUNDARY && boundary > STACK_BOUNDARY)
	      alignment_pad->constant = offset_ptr->constant - save_constant;
	}
    }
}

static void
pad_below (struct args_size *offset_ptr, enum machine_mode passed_mode, tree sizetree)
{
  if (passed_mode != BLKmode)
    {
      if (GET_MODE_BITSIZE (passed_mode) % PARM_BOUNDARY)
	offset_ptr->constant
	  += (((GET_MODE_BITSIZE (passed_mode) + PARM_BOUNDARY - 1)
	       / PARM_BOUNDARY * PARM_BOUNDARY / BITS_PER_UNIT)
	      - GET_MODE_SIZE (passed_mode));
    }
  else
    {
      if (TREE_CODE (sizetree) != INTEGER_CST
	  || (TREE_INT_CST_LOW (sizetree) * BITS_PER_UNIT) % PARM_BOUNDARY)
	{
	  /* Round the size up to multiple of PARM_BOUNDARY bits.  */
	  tree s2 = round_up (sizetree, PARM_BOUNDARY / BITS_PER_UNIT);
	  /* Add it in.  */
	  ADD_PARM_SIZE (*offset_ptr, s2);
	  SUB_PARM_SIZE (*offset_ptr, sizetree);
	}
    }
}

/* Walk the tree of blocks describing the binding levels within a function
   and warn about variables the might be killed by setjmp or vfork.
   This is done after calling flow_analysis and before global_alloc
   clobbers the pseudo-regs to hard regs.  */

void
setjmp_vars_warning (tree block)
{
  tree decl, sub;

  for (decl = BLOCK_VARS (block); decl; decl = TREE_CHAIN (decl))
    {
      if (TREE_CODE (decl) == VAR_DECL
	  && DECL_RTL_SET_P (decl)
	  && REG_P (DECL_RTL (decl))
	  && regno_clobbered_at_setjmp (REGNO (DECL_RTL (decl))))
	warning (0, "variable %q+D might be clobbered by %<longjmp%>"
		 " or %<vfork%>",
		 decl);
    }

  for (sub = BLOCK_SUBBLOCKS (block); sub; sub = TREE_CHAIN (sub))
    setjmp_vars_warning (sub);
}

/* Do the appropriate part of setjmp_vars_warning
   but for arguments instead of local variables.  */

void
setjmp_args_warning (void)
{
  tree decl;
  for (decl = DECL_ARGUMENTS (current_function_decl);
       decl; decl = TREE_CHAIN (decl))
    if (DECL_RTL (decl) != 0
	&& REG_P (DECL_RTL (decl))
	&& regno_clobbered_at_setjmp (REGNO (DECL_RTL (decl))))
      warning (0, "argument %q+D might be clobbered by %<longjmp%> or %<vfork%>",
	       decl);
}


/* Identify BLOCKs referenced by more than one NOTE_INSN_BLOCK_{BEG,END},
   and create duplicate blocks.  */
/* ??? Need an option to either create block fragments or to create
   abstract origin duplicates of a source block.  It really depends
   on what optimization has been performed.  */

void
reorder_blocks (void)
{
  tree block = DECL_INITIAL (current_function_decl);
  VEC(tree,heap) *block_stack;

  if (block == NULL_TREE)
    return;

  block_stack = VEC_alloc (tree, heap, 10);

  /* Reset the TREE_ASM_WRITTEN bit for all blocks.  */
  clear_block_marks (block);

  /* Prune the old trees away, so that they don't get in the way.  */
  BLOCK_SUBBLOCKS (block) = NULL_TREE;
  BLOCK_CHAIN (block) = NULL_TREE;

  /* Recreate the block tree from the note nesting.  */
  reorder_blocks_1 (get_insns (), block, &block_stack);
  BLOCK_SUBBLOCKS (block) = blocks_nreverse (BLOCK_SUBBLOCKS (block));

  VEC_free (tree, heap, block_stack);
}

/* Helper function for reorder_blocks.  Reset TREE_ASM_WRITTEN.  */

void
clear_block_marks (tree block)
{
  while (block)
    {
      TREE_ASM_WRITTEN (block) = 0;
      clear_block_marks (BLOCK_SUBBLOCKS (block));
      block = BLOCK_CHAIN (block);
    }
}

static void
reorder_blocks_1 (rtx insns, tree current_block, VEC(tree,heap) **p_block_stack)
{
  rtx insn;

  for (insn = insns; insn; insn = NEXT_INSN (insn))
    {
      if (NOTE_P (insn))
	{
	  if (NOTE_LINE_NUMBER (insn) == NOTE_INSN_BLOCK_BEG)
	    {
	      tree block = NOTE_BLOCK (insn);
	      tree origin;

	      origin = (BLOCK_FRAGMENT_ORIGIN (block)
			? BLOCK_FRAGMENT_ORIGIN (block)
			: block);

	      /* If we have seen this block before, that means it now
		 spans multiple address regions.  Create a new fragment.  */
	      if (TREE_ASM_WRITTEN (block))
		{
		  tree new_block = copy_node (block);

		  BLOCK_FRAGMENT_ORIGIN (new_block) = origin;
		  BLOCK_FRAGMENT_CHAIN (new_block)
		    = BLOCK_FRAGMENT_CHAIN (origin);
		  BLOCK_FRAGMENT_CHAIN (origin) = new_block;

		  NOTE_BLOCK (insn) = new_block;
		  block = new_block;
		}

	      BLOCK_SUBBLOCKS (block) = 0;
	      TREE_ASM_WRITTEN (block) = 1;
	      /* When there's only one block for the entire function,
		 current_block == block and we mustn't do this, it
		 will cause infinite recursion.  */
	      if (block != current_block)
		{
		  if (block != origin)
		    gcc_assert (BLOCK_SUPERCONTEXT (origin) == current_block);

		  BLOCK_SUPERCONTEXT (block) = current_block;
		  BLOCK_CHAIN (block) = BLOCK_SUBBLOCKS (current_block);
		  BLOCK_SUBBLOCKS (current_block) = block;
		  current_block = origin;
		}
	      VEC_safe_push (tree, heap, *p_block_stack, block);
	    }
	  else if (NOTE_LINE_NUMBER (insn) == NOTE_INSN_BLOCK_END)
	    {
	      NOTE_BLOCK (insn) = VEC_pop (tree, *p_block_stack);
	      BLOCK_SUBBLOCKS (current_block)
		= blocks_nreverse (BLOCK_SUBBLOCKS (current_block));
	      current_block = BLOCK_SUPERCONTEXT (current_block);
	    }
	}
    }
}

/* Reverse the order of elements in the chain T of blocks,
   and return the new head of the chain (old last element).  */

tree
blocks_nreverse (tree t)
{
  tree prev = 0, decl, next;
  for (decl = t; decl; decl = next)
    {
      next = BLOCK_CHAIN (decl);
      BLOCK_CHAIN (decl) = prev;
      prev = decl;
    }
  return prev;
}

/* Count the subblocks of the list starting with BLOCK.  If VECTOR is
   non-NULL, list them all into VECTOR, in a depth-first preorder
   traversal of the block tree.  Also clear TREE_ASM_WRITTEN in all
   blocks.  */

static int
all_blocks (tree block, tree *vector)
{
  int n_blocks = 0;

  while (block)
    {
      TREE_ASM_WRITTEN (block) = 0;

      /* Record this block.  */
      if (vector)
	vector[n_blocks] = block;

      ++n_blocks;

      /* Record the subblocks, and their subblocks...  */
      n_blocks += all_blocks (BLOCK_SUBBLOCKS (block),
			      vector ? vector + n_blocks : 0);
      block = BLOCK_CHAIN (block);
    }

  return n_blocks;
}

/* Return a vector containing all the blocks rooted at BLOCK.  The
   number of elements in the vector is stored in N_BLOCKS_P.  The
   vector is dynamically allocated; it is the caller's responsibility
   to call `free' on the pointer returned.  */

static tree *
get_block_vector (tree block, int *n_blocks_p)
{
  tree *block_vector;

  *n_blocks_p = all_blocks (block, NULL);
  block_vector = XNEWVEC (tree, *n_blocks_p);
  all_blocks (block, block_vector);

  return block_vector;
}

static GTY(()) int next_block_index = 2;

/* Set BLOCK_NUMBER for all the blocks in FN.  */

void
number_blocks (tree fn)
{
  int i;
  int n_blocks;
  tree *block_vector;

  /* For SDB and XCOFF debugging output, we start numbering the blocks
     from 1 within each function, rather than keeping a running
     count.  */
#if defined (SDB_DEBUGGING_INFO) || defined (XCOFF_DEBUGGING_INFO)
  if (write_symbols == SDB_DEBUG || write_symbols == XCOFF_DEBUG)
    next_block_index = 1;
#endif

  block_vector = get_block_vector (DECL_INITIAL (fn), &n_blocks);

  /* The top-level BLOCK isn't numbered at all.  */
  for (i = 1; i < n_blocks; ++i)
    /* We number the blocks from two.  */
    BLOCK_NUMBER (block_vector[i]) = next_block_index++;

  free (block_vector);

  return;
}

/* If VAR is present in a subblock of BLOCK, return the subblock.  */

tree
debug_find_var_in_block_tree (tree var, tree block)
{
  tree t;

  for (t = BLOCK_VARS (block); t; t = TREE_CHAIN (t))
    if (t == var)
      return block;

  for (t = BLOCK_SUBBLOCKS (block); t; t = TREE_CHAIN (t))
    {
      tree ret = debug_find_var_in_block_tree (var, t);
      if (ret)
	return ret;
    }

  return NULL_TREE;
}

/* Allocate a function structure for FNDECL and set its contents
   to the defaults.  */

void
allocate_struct_function (tree fndecl)
{
  tree result;
  tree fntype = fndecl ? TREE_TYPE (fndecl) : NULL_TREE;

  cfun = ggc_alloc_cleared (sizeof (struct function));

  cfun->stack_alignment_needed = STACK_BOUNDARY;
  cfun->preferred_stack_boundary = STACK_BOUNDARY;

  current_function_funcdef_no = funcdef_no++;

  cfun->function_frequency = FUNCTION_FREQUENCY_NORMAL;

  init_eh_for_function ();

  lang_hooks.function.init (cfun);
  if (init_machine_status)
    cfun->machine = (*init_machine_status) ();

  if (fndecl == NULL)
    return;

  DECL_STRUCT_FUNCTION (fndecl) = cfun;
  cfun->decl = fndecl;

  /* APPLE LOCAL begin radar 5732232 - blocks */
  /* We cannot support blocks which return aggregates because at this
     point we do not have info on the return type. */
  if (!cur_block)
  {
    result = DECL_RESULT (fndecl);
    if (aggregate_value_p (result, fndecl))
    {
#ifdef PCC_STATIC_STRUCT_RETURN
      current_function_returns_pcc_struct = 1;
#endif
      current_function_returns_struct = 1;
    }
    /* This code is not used anywhere ! */
    current_function_returns_pointer = POINTER_TYPE_P (TREE_TYPE (result));
  }
  /* APPLE LOCAL end radar 5732232 - blocks */
  current_function_stdarg
    = (fntype
       && TYPE_ARG_TYPES (fntype) != 0
       && (TREE_VALUE (tree_last (TYPE_ARG_TYPES (fntype)))
	   != void_type_node));

  /* Assume all registers in stdarg functions need to be saved.  */
  cfun->va_list_gpr_size = VA_LIST_MAX_GPR_SIZE;
  cfun->va_list_fpr_size = VA_LIST_MAX_FPR_SIZE;
}

/* Reset cfun, and other non-struct-function variables to defaults as
   appropriate for emitting rtl at the start of a function.  */

static void
prepare_function_start (tree fndecl)
{
  if (fndecl && DECL_STRUCT_FUNCTION (fndecl))
    cfun = DECL_STRUCT_FUNCTION (fndecl);
  else
    allocate_struct_function (fndecl);
  init_emit ();
  init_varasm_status (cfun);
  init_expr ();

  cse_not_expected = ! optimize;

  /* Caller save not needed yet.  */
  caller_save_needed = 0;

  /* We haven't done register allocation yet.  */
  reg_renumber = 0;

  /* Indicate that we have not instantiated virtual registers yet.  */
  virtuals_instantiated = 0;

  /* Indicate that we want CONCATs now.  */
  generating_concat_p = 1;

  /* Indicate we have no need of a frame pointer yet.  */
  frame_pointer_needed = 0;
}

/* Initialize the rtl expansion mechanism so that we can do simple things
   like generate sequences.  This is used to provide a context during global
   initialization of some passes.  */
void
init_dummy_function_start (void)
{
  prepare_function_start (NULL);
}

/* Generate RTL for the start of the function SUBR (a FUNCTION_DECL tree node)
   and initialize static variables for generating RTL for the statements
   of the function.  */

void
init_function_start (tree subr)
{
  prepare_function_start (subr);

  /* Prevent ever trying to delete the first instruction of a
     function.  Also tell final how to output a linenum before the
     function prologue.  Note linenums could be missing, e.g. when
     compiling a Java .class file.  */
  if (! DECL_IS_BUILTIN (subr))
    emit_line_note (DECL_SOURCE_LOCATION (subr));

  /* Make sure first insn is a note even if we don't want linenums.
     This makes sure the first insn will never be deleted.
     Also, final expects a note to appear there.  */
  emit_note (NOTE_INSN_DELETED);

  /* Warn if this value is an aggregate type,
     regardless of which calling convention we are using for it.  */
  if (AGGREGATE_TYPE_P (TREE_TYPE (DECL_RESULT (subr))))
    warning (OPT_Waggregate_return, "function returns an aggregate");
}

/* Make sure all values used by the optimization passes have sane
   defaults.  */
unsigned int
init_function_for_compilation (void)
{
  reg_renumber = 0;

  /* No prologue/epilogue insns yet.  Make sure that these vectors are
     empty.  */
  gcc_assert (VEC_length (int, prologue) == 0);
  gcc_assert (VEC_length (int, epilogue) == 0);
  gcc_assert (VEC_length (int, sibcall_epilogue) == 0);
  return 0;
}

struct tree_opt_pass pass_init_function =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */   
  init_function_for_compilation,        /* execute */       
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};


void
expand_main_function (void)
{
#if (defined(INVOKE__main)				\
     || (!defined(HAS_INIT_SECTION)			\
	 && !defined(INIT_SECTION_ASM_OP)		\
	 && !defined(INIT_ARRAY_SECTION_ASM_OP)))
  emit_library_call (init_one_libfunc (NAME__MAIN), LCT_NORMAL, VOIDmode, 0);
#endif
}

/* Expand code to initialize the stack_protect_guard.  This is invoked at
   the beginning of a function to be protected.  */

#ifndef HAVE_stack_protect_set
# define HAVE_stack_protect_set		0
# define gen_stack_protect_set(x,y)	(gcc_unreachable (), NULL_RTX)
#endif

void
stack_protect_prologue (void)
{
  tree guard_decl = targetm.stack_protect_guard ();
  rtx x, y;

  /* Avoid expand_expr here, because we don't want guard_decl pulled
     into registers unless absolutely necessary.  And we know that
     cfun->stack_protect_guard is a local stack slot, so this skips
     all the fluff.  */
  x = validize_mem (DECL_RTL (cfun->stack_protect_guard));
  y = validize_mem (DECL_RTL (guard_decl));

  /* Allow the target to copy from Y to X without leaking Y into a
     register.  */
  if (HAVE_stack_protect_set)
    {
      rtx insn = gen_stack_protect_set (x, y);
      if (insn)
	{
	  emit_insn (insn);
	  return;
	}
    }

  /* Otherwise do a straight move.  */
  emit_move_insn (x, y);
}

/* Expand code to verify the stack_protect_guard.  This is invoked at
   the end of a function to be protected.  */

#ifndef HAVE_stack_protect_test
# define HAVE_stack_protect_test		0
# define gen_stack_protect_test(x, y, z)	(gcc_unreachable (), NULL_RTX)
#endif

void
stack_protect_epilogue (void)
{
  tree guard_decl = targetm.stack_protect_guard ();
  rtx label = gen_label_rtx ();
  rtx x, y, tmp;

  /* Avoid expand_expr here, because we don't want guard_decl pulled
     into registers unless absolutely necessary.  And we know that
     cfun->stack_protect_guard is a local stack slot, so this skips
     all the fluff.  */
  x = validize_mem (DECL_RTL (cfun->stack_protect_guard));
  y = validize_mem (DECL_RTL (guard_decl));

  /* Allow the target to compare Y with X without leaking either into
     a register.  */
  if (HAVE_stack_protect_test != 0)
    {
      tmp = gen_stack_protect_test (x, y, label);
      if (tmp)
	{
	  emit_insn (tmp);
	  goto done;
	}
    }

  emit_cmp_and_jump_insns (x, y, EQ, NULL_RTX, ptr_mode, 1, label);
 done:

  /* The noreturn predictor has been moved to the tree level.  The rtl-level
     predictors estimate this branch about 20%, which isn't enough to get
     things moved out of line.  Since this is the only extant case of adding
     a noreturn function at the rtl level, it doesn't seem worth doing ought
     except adding the prediction by hand.  */
  tmp = get_last_insn ();
  if (JUMP_P (tmp))
    predict_insn_def (tmp, PRED_NORETURN, TAKEN);

  expand_expr_stmt (targetm.stack_protect_fail ());
  emit_label (label);
}

/* Start the RTL for a new function, and set variables used for
   emitting RTL.
   SUBR is the FUNCTION_DECL node.
   PARMS_HAVE_CLEANUPS is nonzero if there are cleanups associated with
   the function's parameters, which must be run at any return statement.  */

void
expand_function_start (tree subr)
{
  /* Make sure volatile mem refs aren't considered
     valid operands of arithmetic insns.  */
  init_recog_no_volatile ();

  current_function_profile
    = (profile_flag
       && ! DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (subr));

  current_function_limit_stack
    = (stack_limit_rtx != NULL_RTX && ! DECL_NO_LIMIT_STACK (subr));

  /* Make the label for return statements to jump to.  Do not special
     case machines with special return instructions -- they will be
     handled later during jump, ifcvt, or epilogue creation.  */
  return_label = gen_label_rtx ();

  /* Initialize rtx used to return the value.  */
  /* Do this before assign_parms so that we copy the struct value address
     before any library calls that assign parms might generate.  */

  /* Decide whether to return the value in memory or in a register.  */
  if (aggregate_value_p (DECL_RESULT (subr), subr))
    {
      /* Returning something that won't go in a register.  */
      rtx value_address = 0;

#ifdef PCC_STATIC_STRUCT_RETURN
      if (current_function_returns_pcc_struct)
	{
	  int size = int_size_in_bytes (TREE_TYPE (DECL_RESULT (subr)));
	  value_address = assemble_static_space (size);
	}
      else
#endif
	{
	  rtx sv = targetm.calls.struct_value_rtx (TREE_TYPE (subr), 2);
	  /* Expect to be passed the address of a place to store the value.
	     If it is passed as an argument, assign_parms will take care of
	     it.  */
	  if (sv)
	    {
	      value_address = gen_reg_rtx (Pmode);
	      emit_move_insn (value_address, sv);
	    }
	}
      if (value_address)
	{
	  rtx x = value_address;
	  if (!DECL_BY_REFERENCE (DECL_RESULT (subr)))
	    {
	      x = gen_rtx_MEM (DECL_MODE (DECL_RESULT (subr)), x);
	      set_mem_attributes (x, DECL_RESULT (subr), 1);
	    }
	  SET_DECL_RTL (DECL_RESULT (subr), x);
	}
    }
  else if (DECL_MODE (DECL_RESULT (subr)) == VOIDmode)
    /* If return mode is void, this decl rtl should not be used.  */
    SET_DECL_RTL (DECL_RESULT (subr), NULL_RTX);
  else
    {
      /* Compute the return values into a pseudo reg, which we will copy
	 into the true return register after the cleanups are done.  */
      tree return_type = TREE_TYPE (DECL_RESULT (subr));
      if (TYPE_MODE (return_type) != BLKmode
	  && targetm.calls.return_in_msb (return_type))
	/* expand_function_end will insert the appropriate padding in
	   this case.  Use the return value's natural (unpadded) mode
	   within the function proper.  */
	SET_DECL_RTL (DECL_RESULT (subr),
		      gen_reg_rtx (TYPE_MODE (return_type)));
      else
	{
	  /* In order to figure out what mode to use for the pseudo, we
	     figure out what the mode of the eventual return register will
	     actually be, and use that.  */
	  rtx hard_reg = hard_function_value (return_type, subr, 0, 1);

	  /* Structures that are returned in registers are not
	     aggregate_value_p, so we may see a PARALLEL or a REG.  */
	  if (REG_P (hard_reg))
	    SET_DECL_RTL (DECL_RESULT (subr),
			  gen_reg_rtx (GET_MODE (hard_reg)));
	  else
	    {
	      gcc_assert (GET_CODE (hard_reg) == PARALLEL);
	      SET_DECL_RTL (DECL_RESULT (subr), gen_group_rtx (hard_reg));
	    }
	}

      /* Set DECL_REGISTER flag so that expand_function_end will copy the
	 result to the real return register(s).  */
      DECL_REGISTER (DECL_RESULT (subr)) = 1;
    }

  /* Initialize rtx for parameters and local variables.
     In some cases this requires emitting insns.  */
  assign_parms (subr);

  /* If function gets a static chain arg, store it.  */
  if (cfun->static_chain_decl)
    {
      tree parm = cfun->static_chain_decl;
      rtx local = gen_reg_rtx (Pmode);

      set_decl_incoming_rtl (parm, static_chain_incoming_rtx);
      SET_DECL_RTL (parm, local);
      mark_reg_pointer (local, TYPE_ALIGN (TREE_TYPE (TREE_TYPE (parm))));

      emit_move_insn (local, static_chain_incoming_rtx);
    }

  /* If the function receives a non-local goto, then store the
     bits we need to restore the frame pointer.  */
  if (cfun->nonlocal_goto_save_area)
    {
      tree t_save;
      rtx r_save;

      /* ??? We need to do this save early.  Unfortunately here is
	 before the frame variable gets declared.  Help out...  */
      expand_var (TREE_OPERAND (cfun->nonlocal_goto_save_area, 0));

      t_save = build4 (ARRAY_REF, ptr_type_node,
		       cfun->nonlocal_goto_save_area,
		       integer_zero_node, NULL_TREE, NULL_TREE);
      r_save = expand_expr (t_save, NULL_RTX, VOIDmode, EXPAND_WRITE);
      r_save = convert_memory_address (Pmode, r_save);

      emit_move_insn (r_save, virtual_stack_vars_rtx);
      update_nonlocal_goto_save_area ();
    }

  /* The following was moved from init_function_start.
     The move is supposed to make sdb output more accurate.  */
  /* Indicate the beginning of the function body,
     as opposed to parm setup.  */
  emit_note (NOTE_INSN_FUNCTION_BEG);

  gcc_assert (NOTE_P (get_last_insn ()));

  parm_birth_insn = get_last_insn ();

  if (current_function_profile)
    {
#ifdef PROFILE_HOOK
      PROFILE_HOOK (current_function_funcdef_no);
#endif
    }

  /* After the display initializations is where the stack checking
     probe should go.  */
  if(flag_stack_check)
    stack_check_probe_note = emit_note (NOTE_INSN_DELETED);

  /* Make sure there is a line number after the function entry setup code.  */
  force_next_line_note ();
}

/* Undo the effects of init_dummy_function_start.  */
void
expand_dummy_function_end (void)
{
  /* End any sequences that failed to be closed due to syntax errors.  */
  while (in_sequence_p ())
    end_sequence ();

  /* Outside function body, can't compute type's actual size
     until next function's body starts.  */

  free_after_parsing (cfun);
  free_after_compilation (cfun);
  cfun = 0;
}

/* Call DOIT for each hard register used as a return value from
   the current function.  */

void
diddle_return_value (void (*doit) (rtx, void *), void *arg)
{
  rtx outgoing = current_function_return_rtx;

  if (! outgoing)
    return;

  if (REG_P (outgoing))
    (*doit) (outgoing, arg);
  else if (GET_CODE (outgoing) == PARALLEL)
    {
      int i;

      for (i = 0; i < XVECLEN (outgoing, 0); i++)
	{
	  rtx x = XEXP (XVECEXP (outgoing, 0, i), 0);

	  if (REG_P (x) && REGNO (x) < FIRST_PSEUDO_REGISTER)
	    (*doit) (x, arg);
	}
    }
}

static void
do_clobber_return_reg (rtx reg, void *arg ATTRIBUTE_UNUSED)
{
  emit_insn (gen_rtx_CLOBBER (VOIDmode, reg));
}

void
clobber_return_register (void)
{
  diddle_return_value (do_clobber_return_reg, NULL);

  /* In case we do use pseudo to return value, clobber it too.  */
  if (DECL_RTL_SET_P (DECL_RESULT (current_function_decl)))
    {
      tree decl_result = DECL_RESULT (current_function_decl);
      rtx decl_rtl = DECL_RTL (decl_result);
      if (REG_P (decl_rtl) && REGNO (decl_rtl) >= FIRST_PSEUDO_REGISTER)
	{
	  do_clobber_return_reg (decl_rtl, NULL);
	}
    }
}

static void
do_use_return_reg (rtx reg, void *arg ATTRIBUTE_UNUSED)
{
  emit_insn (gen_rtx_USE (VOIDmode, reg));
}

static void
use_return_register (void)
{
  diddle_return_value (do_use_return_reg, NULL);
}

/* Possibly warn about unused parameters.  */
void
do_warn_unused_parameter (tree fn)
{
  tree decl;

  for (decl = DECL_ARGUMENTS (fn);
       decl; decl = TREE_CHAIN (decl))
    if (!TREE_USED (decl) && TREE_CODE (decl) == PARM_DECL
	&& DECL_NAME (decl) && !DECL_ARTIFICIAL (decl))
      warning (OPT_Wunused_parameter, "unused parameter %q+D", decl);
}

static GTY(()) rtx initial_trampoline;

/* Generate RTL for the end of the current function.  */

void
expand_function_end (void)
{
  rtx clobber_after;

  /* If arg_pointer_save_area was referenced only from a nested
     function, we will not have initialized it yet.  Do that now.  */
  if (arg_pointer_save_area && ! cfun->arg_pointer_save_area_init)
    get_arg_pointer_save_area (cfun);

  /* If we are doing stack checking and this function makes calls,
     do a stack probe at the start of the function to ensure we have enough
     space for another stack frame.  */
  if (flag_stack_check && ! STACK_CHECK_BUILTIN)
    {
      rtx insn, seq;

      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	if (CALL_P (insn))
	  {
	    start_sequence ();
	    probe_stack_range (STACK_CHECK_PROTECT,
			       GEN_INT (STACK_CHECK_MAX_FRAME_SIZE));
	    seq = get_insns ();
	    end_sequence ();
	    emit_insn_before (seq, stack_check_probe_note);
	    break;
	  }
    }

  /* Possibly warn about unused parameters.
     When frontend does unit-at-a-time, the warning is already
     issued at finalization time.  */
  if (warn_unused_parameter
      && !lang_hooks.callgraph.expand_function)
    do_warn_unused_parameter (current_function_decl);

  /* End any sequences that failed to be closed due to syntax errors.  */
  while (in_sequence_p ())
    end_sequence ();

  clear_pending_stack_adjust ();
  do_pending_stack_adjust ();

  /* Mark the end of the function body.
     If control reaches this insn, the function can drop through
     without returning a value.  */
  emit_note (NOTE_INSN_FUNCTION_END);

  /* Must mark the last line number note in the function, so that the test
     coverage code can avoid counting the last line twice.  This just tells
     the code to ignore the immediately following line note, since there
     already exists a copy of this note somewhere above.  This line number
     note is still needed for debugging though, so we can't delete it.  */
  if (flag_test_coverage)
    emit_note (NOTE_INSN_REPEATED_LINE_NUMBER);

  /* Output a linenumber for the end of the function.
     SDB depends on this.  */
  force_next_line_note ();
  emit_line_note (input_location);

  /* Before the return label (if any), clobber the return
     registers so that they are not propagated live to the rest of
     the function.  This can only happen with functions that drop
     through; if there had been a return statement, there would
     have either been a return rtx, or a jump to the return label.

     We delay actual code generation after the current_function_value_rtx
     is computed.  */
  clobber_after = get_last_insn ();

  /* Output the label for the actual return from the function.  */
  emit_label (return_label);

#ifdef TARGET_PROFILER_EPILOGUE
  if (current_function_profile && TARGET_PROFILER_EPILOGUE)
    {
      static rtx mexitcount_libfunc;
      static int initialized;

      if (!initialized)
	{
	  mexitcount_libfunc = init_one_libfunc (".mexitcount");
	  initialized = 0;
	}
      emit_library_call (mexitcount_libfunc, LCT_NORMAL, VOIDmode, 0);
    }
#endif

  if (USING_SJLJ_EXCEPTIONS)
    {
      /* Let except.c know where it should emit the call to unregister
	 the function context for sjlj exceptions.  */
      if (flag_exceptions)
	sjlj_emit_function_exit_after (get_last_insn ());
    }
  else
    {
      /* @@@ This is a kludge.  We want to ensure that instructions that
	 may trap are not moved into the epilogue by scheduling, because
	 we don't always emit unwind information for the epilogue.
	 However, not all machine descriptions define a blockage insn, so
	 emit an ASM_INPUT to act as one.  */
      if (flag_non_call_exceptions)
	emit_insn (gen_rtx_ASM_INPUT (VOIDmode, ""));
    }

  /* If this is an implementation of throw, do what's necessary to
     communicate between __builtin_eh_return and the epilogue.  */
  expand_eh_return ();

  /* If scalar return value was computed in a pseudo-reg, or was a named
     return value that got dumped to the stack, copy that to the hard
     return register.  */
  if (DECL_RTL_SET_P (DECL_RESULT (current_function_decl)))
    {
      tree decl_result = DECL_RESULT (current_function_decl);
      rtx decl_rtl = DECL_RTL (decl_result);

      if (REG_P (decl_rtl)
	  ? REGNO (decl_rtl) >= FIRST_PSEUDO_REGISTER
	  : DECL_REGISTER (decl_result))
	{
	  rtx real_decl_rtl = current_function_return_rtx;

	  /* This should be set in assign_parms.  */
	  gcc_assert (REG_FUNCTION_VALUE_P (real_decl_rtl));

	  /* If this is a BLKmode structure being returned in registers,
	     then use the mode computed in expand_return.  Note that if
	     decl_rtl is memory, then its mode may have been changed,
	     but that current_function_return_rtx has not.  */
	  if (GET_MODE (real_decl_rtl) == BLKmode)
	    PUT_MODE (real_decl_rtl, GET_MODE (decl_rtl));

	  /* If a non-BLKmode return value should be padded at the least
	     significant end of the register, shift it left by the appropriate
	     amount.  BLKmode results are handled using the group load/store
	     machinery.  */
	  if (TYPE_MODE (TREE_TYPE (decl_result)) != BLKmode
	      && targetm.calls.return_in_msb (TREE_TYPE (decl_result)))
	    {
	      emit_move_insn (gen_rtx_REG (GET_MODE (decl_rtl),
					   REGNO (real_decl_rtl)),
			      decl_rtl);
	      shift_return_value (GET_MODE (decl_rtl), true, real_decl_rtl);
	    }
	  /* If a named return value dumped decl_return to memory, then
	     we may need to re-do the PROMOTE_MODE signed/unsigned
	     extension.  */
	  else if (GET_MODE (real_decl_rtl) != GET_MODE (decl_rtl))
	    {
	      int unsignedp = TYPE_UNSIGNED (TREE_TYPE (decl_result));

	      if (targetm.calls.promote_function_return (TREE_TYPE (current_function_decl)))
		promote_mode (TREE_TYPE (decl_result), GET_MODE (decl_rtl),
			      &unsignedp, 1);

	      convert_move (real_decl_rtl, decl_rtl, unsignedp);
	    }
	  else if (GET_CODE (real_decl_rtl) == PARALLEL)
	    {
	      /* If expand_function_start has created a PARALLEL for decl_rtl,
		 move the result to the real return registers.  Otherwise, do
		 a group load from decl_rtl for a named return.  */
	      if (GET_CODE (decl_rtl) == PARALLEL)
		emit_group_move (real_decl_rtl, decl_rtl);
	      else
		emit_group_load (real_decl_rtl, decl_rtl,
				 TREE_TYPE (decl_result),
				 int_size_in_bytes (TREE_TYPE (decl_result)));
	    }
	  /* In the case of complex integer modes smaller than a word, we'll
	     need to generate some non-trivial bitfield insertions.  Do that
	     on a pseudo and not the hard register.  */
	  else if (GET_CODE (decl_rtl) == CONCAT
		   && GET_MODE_CLASS (GET_MODE (decl_rtl)) == MODE_COMPLEX_INT
		   && GET_MODE_BITSIZE (GET_MODE (decl_rtl)) <= BITS_PER_WORD)
	    {
	      int old_generating_concat_p;
	      rtx tmp;

	      old_generating_concat_p = generating_concat_p;
	      generating_concat_p = 0;
	      tmp = gen_reg_rtx (GET_MODE (decl_rtl));
	      generating_concat_p = old_generating_concat_p;

	      emit_move_insn (tmp, decl_rtl);
	      emit_move_insn (real_decl_rtl, tmp);
	    }
	  else
	    emit_move_insn (real_decl_rtl, decl_rtl);
	}
    }

  /* If returning a structure, arrange to return the address of the value
     in a place where debuggers expect to find it.

     If returning a structure PCC style,
     the caller also depends on this value.
     And current_function_returns_pcc_struct is not necessarily set.  */
  if (current_function_returns_struct
      || current_function_returns_pcc_struct)
    {
      rtx value_address = DECL_RTL (DECL_RESULT (current_function_decl));
      tree type = TREE_TYPE (DECL_RESULT (current_function_decl));
      rtx outgoing;

      if (DECL_BY_REFERENCE (DECL_RESULT (current_function_decl)))
	type = TREE_TYPE (type);
      else
	value_address = XEXP (value_address, 0);

      outgoing = targetm.calls.function_value (build_pointer_type (type),
					       current_function_decl, true);

      /* Mark this as a function return value so integrate will delete the
	 assignment and USE below when inlining this function.  */
      REG_FUNCTION_VALUE_P (outgoing) = 1;

      /* The address may be ptr_mode and OUTGOING may be Pmode.  */
      value_address = convert_memory_address (GET_MODE (outgoing),
					      value_address);

      emit_move_insn (outgoing, value_address);

      /* Show return register used to hold result (in this case the address
	 of the result.  */
      current_function_return_rtx = outgoing;
    }

  /* Emit the actual code to clobber return register.  */
  {
    rtx seq;

    start_sequence ();
    clobber_return_register ();
    expand_naked_return ();
    seq = get_insns ();
    end_sequence ();

    emit_insn_after (seq, clobber_after);
  }

  /* Output the label for the naked return from the function.  */
  emit_label (naked_return_label);

  /* If stack protection is enabled for this function, check the guard.  */
  if (cfun->stack_protect_guard)
    stack_protect_epilogue ();

  /* If we had calls to alloca, and this machine needs
     an accurate stack pointer to exit the function,
     insert some code to save and restore the stack pointer.  */
  if (! EXIT_IGNORE_STACK
      && current_function_calls_alloca)
    {
      rtx tem = 0;

      emit_stack_save (SAVE_FUNCTION, &tem, parm_birth_insn);
      emit_stack_restore (SAVE_FUNCTION, tem, NULL_RTX);
    }

  /* ??? This should no longer be necessary since stupid is no longer with
     us, but there are some parts of the compiler (eg reload_combine, and
     sh mach_dep_reorg) that still try and compute their own lifetime info
     instead of using the general framework.  */
  use_return_register ();
}

rtx
get_arg_pointer_save_area (struct function *f)
{
  rtx ret = f->x_arg_pointer_save_area;

  if (! ret)
    {
      ret = assign_stack_local_1 (Pmode, GET_MODE_SIZE (Pmode), 0, f);
      f->x_arg_pointer_save_area = ret;
    }

  if (f == cfun && ! f->arg_pointer_save_area_init)
    {
      rtx seq;

      /* Save the arg pointer at the beginning of the function.  The
	 generated stack slot may not be a valid memory address, so we
	 have to check it and fix it if necessary.  */
      start_sequence ();
      emit_move_insn (validize_mem (ret), virtual_incoming_args_rtx);
      seq = get_insns ();
      end_sequence ();

      push_topmost_sequence ();
      emit_insn_after (seq, entry_of_function ());
      pop_topmost_sequence ();
    }

  return ret;
}

/* Extend a vector that records the INSN_UIDs of INSNS
   (a list of one or more insns).  */

static void
record_insns (rtx insns, VEC(int,heap) **vecp)
{
  rtx tmp;

  for (tmp = insns; tmp != NULL_RTX; tmp = NEXT_INSN (tmp))
    VEC_safe_push (int, heap, *vecp, INSN_UID (tmp));
}

/* Set the locator of the insn chain starting at INSN to LOC.  */
static void
set_insn_locators (rtx insn, int loc)
{
  while (insn != NULL_RTX)
    {
      if (INSN_P (insn))
	INSN_LOCATOR (insn) = loc;
      insn = NEXT_INSN (insn);
    }
}

/* Determine how many INSN_UIDs in VEC are part of INSN.  Because we can
   be running after reorg, SEQUENCE rtl is possible.  */

static int
contains (rtx insn, VEC(int,heap) **vec)
{
  int i, j;

  if (NONJUMP_INSN_P (insn)
      && GET_CODE (PATTERN (insn)) == SEQUENCE)
    {
      int count = 0;
      for (i = XVECLEN (PATTERN (insn), 0) - 1; i >= 0; i--)
	for (j = VEC_length (int, *vec) - 1; j >= 0; --j)
	  if (INSN_UID (XVECEXP (PATTERN (insn), 0, i))
	      == VEC_index (int, *vec, j))
	    count++;
      return count;
    }
  else
    {
      for (j = VEC_length (int, *vec) - 1; j >= 0; --j)
	if (INSN_UID (insn) == VEC_index (int, *vec, j))
	  return 1;
    }
  return 0;
}

int
prologue_epilogue_contains (rtx insn)
{
  if (contains (insn, &prologue))
    return 1;
  if (contains (insn, &epilogue))
    return 1;
  return 0;
}

int
sibcall_epilogue_contains (rtx insn)
{
  if (sibcall_epilogue)
    return contains (insn, &sibcall_epilogue);
  return 0;
}

#ifdef HAVE_return
/* Insert gen_return at the end of block BB.  This also means updating
   block_for_insn appropriately.  */

static void
emit_return_into_block (basic_block bb, rtx line_note)
{
  emit_jump_insn_after (gen_return (), BB_END (bb));
  if (line_note)
    emit_note_copy_after (line_note, PREV_INSN (BB_END (bb)));
}
#endif /* HAVE_return */

#if defined(HAVE_epilogue) && defined(INCOMING_RETURN_ADDR_RTX)

/* These functions convert the epilogue into a variant that does not
   modify the stack pointer.  This is used in cases where a function
   returns an object whose size is not known until it is computed.
   The called function leaves the object on the stack, leaves the
   stack depressed, and returns a pointer to the object.

   What we need to do is track all modifications and references to the
   stack pointer, deleting the modifications and changing the
   references to point to the location the stack pointer would have
   pointed to had the modifications taken place.

   These functions need to be portable so we need to make as few
   assumptions about the epilogue as we can.  However, the epilogue
   basically contains three things: instructions to reset the stack
   pointer, instructions to reload registers, possibly including the
   frame pointer, and an instruction to return to the caller.

   We must be sure of what a relevant epilogue insn is doing.  We also
   make no attempt to validate the insns we make since if they are
   invalid, we probably can't do anything valid.  The intent is that
   these routines get "smarter" as more and more machines start to use
   them and they try operating on different epilogues.

   We use the following structure to track what the part of the
   epilogue that we've already processed has done.  We keep two copies
   of the SP equivalence, one for use during the insn we are
   processing and one for use in the next insn.  The difference is
   because one part of a PARALLEL may adjust SP and the other may use
   it.  */

struct epi_info
{
  rtx sp_equiv_reg;		/* REG that SP is set from, perhaps SP.  */
  HOST_WIDE_INT sp_offset;	/* Offset from SP_EQUIV_REG of present SP.  */
  rtx new_sp_equiv_reg;		/* REG to be used at end of insn.  */
  HOST_WIDE_INT new_sp_offset;	/* Offset to be used at end of insn.  */
  rtx equiv_reg_src;		/* If nonzero, the value that SP_EQUIV_REG
				   should be set to once we no longer need
				   its value.  */
  rtx const_equiv[FIRST_PSEUDO_REGISTER]; /* Any known constant equivalences
					     for registers.  */
};

static void handle_epilogue_set (rtx, struct epi_info *);
static void update_epilogue_consts (rtx, rtx, void *);
static void emit_equiv_load (struct epi_info *);

/* Modify INSN, a list of one or more insns that is part of the epilogue, to
   no modifications to the stack pointer.  Return the new list of insns.  */

static rtx
keep_stack_depressed (rtx insns)
{
  int j;
  struct epi_info info;
  rtx insn, next;

  /* If the epilogue is just a single instruction, it must be OK as is.  */
  if (NEXT_INSN (insns) == NULL_RTX)
    return insns;

  /* Otherwise, start a sequence, initialize the information we have, and
     process all the insns we were given.  */
  start_sequence ();

  info.sp_equiv_reg = stack_pointer_rtx;
  info.sp_offset = 0;
  info.equiv_reg_src = 0;

  for (j = 0; j < FIRST_PSEUDO_REGISTER; j++)
    info.const_equiv[j] = 0;

  insn = insns;
  next = NULL_RTX;
  while (insn != NULL_RTX)
    {
      next = NEXT_INSN (insn);

      if (!INSN_P (insn))
	{
	  add_insn (insn);
	  insn = next;
	  continue;
	}

      /* If this insn references the register that SP is equivalent to and
	 we have a pending load to that register, we must force out the load
	 first and then indicate we no longer know what SP's equivalent is.  */
      if (info.equiv_reg_src != 0
	  && reg_referenced_p (info.sp_equiv_reg, PATTERN (insn)))
	{
	  emit_equiv_load (&info);
	  info.sp_equiv_reg = 0;
	}

      info.new_sp_equiv_reg = info.sp_equiv_reg;
      info.new_sp_offset = info.sp_offset;

      /* If this is a (RETURN) and the return address is on the stack,
	 update the address and change to an indirect jump.  */
      if (GET_CODE (PATTERN (insn)) == RETURN
	  || (GET_CODE (PATTERN (insn)) == PARALLEL
	      && GET_CODE (XVECEXP (PATTERN (insn), 0, 0)) == RETURN))
	{
	  rtx retaddr = INCOMING_RETURN_ADDR_RTX;
	  rtx base = 0;
	  HOST_WIDE_INT offset = 0;
	  rtx jump_insn, jump_set;

	  /* If the return address is in a register, we can emit the insn
	     unchanged.  Otherwise, it must be a MEM and we see what the
	     base register and offset are.  In any case, we have to emit any
	     pending load to the equivalent reg of SP, if any.  */
	  if (REG_P (retaddr))
	    {
	      emit_equiv_load (&info);
	      add_insn (insn);
	      insn = next;
	      continue;
	    }
	  else
	    {
	      rtx ret_ptr;
	      gcc_assert (MEM_P (retaddr));

	      ret_ptr = XEXP (retaddr, 0);
	      
	      if (REG_P (ret_ptr))
		{
		  base = gen_rtx_REG (Pmode, REGNO (ret_ptr));
		  offset = 0;
		}
	      else
		{
		  gcc_assert (GET_CODE (ret_ptr) == PLUS
			      && REG_P (XEXP (ret_ptr, 0))
			      && GET_CODE (XEXP (ret_ptr, 1)) == CONST_INT);
		  base = gen_rtx_REG (Pmode, REGNO (XEXP (ret_ptr, 0)));
		  offset = INTVAL (XEXP (ret_ptr, 1));
		}
	    }

	  /* If the base of the location containing the return pointer
	     is SP, we must update it with the replacement address.  Otherwise,
	     just build the necessary MEM.  */
	  retaddr = plus_constant (base, offset);
	  if (base == stack_pointer_rtx)
	    retaddr = simplify_replace_rtx (retaddr, stack_pointer_rtx,
					    plus_constant (info.sp_equiv_reg,
							   info.sp_offset));

	  retaddr = gen_rtx_MEM (Pmode, retaddr);
	  MEM_NOTRAP_P (retaddr) = 1;

	  /* If there is a pending load to the equivalent register for SP
	     and we reference that register, we must load our address into
	     a scratch register and then do that load.  */
	  if (info.equiv_reg_src
	      && reg_overlap_mentioned_p (info.equiv_reg_src, retaddr))
	    {
	      unsigned int regno;
	      rtx reg;

	      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
		if (HARD_REGNO_MODE_OK (regno, Pmode)
		    && !fixed_regs[regno]
		    && TEST_HARD_REG_BIT (regs_invalidated_by_call, regno)
		    && !REGNO_REG_SET_P
		         (EXIT_BLOCK_PTR->il.rtl->global_live_at_start, regno)
		    && !refers_to_regno_p (regno,
					   regno + hard_regno_nregs[regno]
								   [Pmode],
					   info.equiv_reg_src, NULL)
		    && info.const_equiv[regno] == 0)
		  break;

	      gcc_assert (regno < FIRST_PSEUDO_REGISTER);

	      reg = gen_rtx_REG (Pmode, regno);
	      emit_move_insn (reg, retaddr);
	      retaddr = reg;
	    }

	  emit_equiv_load (&info);
	  jump_insn = emit_jump_insn (gen_indirect_jump (retaddr));

	  /* Show the SET in the above insn is a RETURN.  */
	  jump_set = single_set (jump_insn);
	  gcc_assert (jump_set);
	  SET_IS_RETURN_P (jump_set) = 1;
	}

      /* If SP is not mentioned in the pattern and its equivalent register, if
	 any, is not modified, just emit it.  Otherwise, if neither is set,
	 replace the reference to SP and emit the insn.  If none of those are
	 true, handle each SET individually.  */
      else if (!reg_mentioned_p (stack_pointer_rtx, PATTERN (insn))
	       && (info.sp_equiv_reg == stack_pointer_rtx
		   || !reg_set_p (info.sp_equiv_reg, insn)))
	add_insn (insn);
      else if (! reg_set_p (stack_pointer_rtx, insn)
	       && (info.sp_equiv_reg == stack_pointer_rtx
		   || !reg_set_p (info.sp_equiv_reg, insn)))
	{
	  int changed;

	  changed = validate_replace_rtx (stack_pointer_rtx,
					  plus_constant (info.sp_equiv_reg,
							 info.sp_offset),
					  insn);
	  gcc_assert (changed);

	  add_insn (insn);
	}
      else if (GET_CODE (PATTERN (insn)) == SET)
	handle_epilogue_set (PATTERN (insn), &info);
      else if (GET_CODE (PATTERN (insn)) == PARALLEL)
	{
	  for (j = 0; j < XVECLEN (PATTERN (insn), 0); j++)
	    if (GET_CODE (XVECEXP (PATTERN (insn), 0, j)) == SET)
	      handle_epilogue_set (XVECEXP (PATTERN (insn), 0, j), &info);
	}
      else
	add_insn (insn);

      info.sp_equiv_reg = info.new_sp_equiv_reg;
      info.sp_offset = info.new_sp_offset;

      /* Now update any constants this insn sets.  */
      note_stores (PATTERN (insn), update_epilogue_consts, &info);
      insn = next;
    }

  insns = get_insns ();
  end_sequence ();
  return insns;
}

/* SET is a SET from an insn in the epilogue.  P is a pointer to the epi_info
   structure that contains information about what we've seen so far.  We
   process this SET by either updating that data or by emitting one or
   more insns.  */

static void
handle_epilogue_set (rtx set, struct epi_info *p)
{
  /* First handle the case where we are setting SP.  Record what it is being
     set from, which we must be able to determine  */
  if (reg_set_p (stack_pointer_rtx, set))
    {
      gcc_assert (SET_DEST (set) == stack_pointer_rtx);

      if (GET_CODE (SET_SRC (set)) == PLUS)
	{
	  p->new_sp_equiv_reg = XEXP (SET_SRC (set), 0);
	  if (GET_CODE (XEXP (SET_SRC (set), 1)) == CONST_INT)
	    p->new_sp_offset = INTVAL (XEXP (SET_SRC (set), 1));
	  else
	    {
	      gcc_assert (REG_P (XEXP (SET_SRC (set), 1))
			  && (REGNO (XEXP (SET_SRC (set), 1))
			      < FIRST_PSEUDO_REGISTER)
			  && p->const_equiv[REGNO (XEXP (SET_SRC (set), 1))]);
	      p->new_sp_offset
		= INTVAL (p->const_equiv[REGNO (XEXP (SET_SRC (set), 1))]);
	    }
	}
      else
	p->new_sp_equiv_reg = SET_SRC (set), p->new_sp_offset = 0;

      /* If we are adjusting SP, we adjust from the old data.  */
      if (p->new_sp_equiv_reg == stack_pointer_rtx)
	{
	  p->new_sp_equiv_reg = p->sp_equiv_reg;
	  p->new_sp_offset += p->sp_offset;
	}

      gcc_assert (p->new_sp_equiv_reg && REG_P (p->new_sp_equiv_reg));

      return;
    }

  /* Next handle the case where we are setting SP's equivalent
     register.  We must not already have a value to set it to.  We
     could update, but there seems little point in handling that case.
     Note that we have to allow for the case where we are setting the
     register set in the previous part of a PARALLEL inside a single
     insn.  But use the old offset for any updates within this insn.
     We must allow for the case where the register is being set in a
     different (usually wider) mode than Pmode).  */
  else if (p->new_sp_equiv_reg != 0 && reg_set_p (p->new_sp_equiv_reg, set))
    {
      gcc_assert (!p->equiv_reg_src
		  && REG_P (p->new_sp_equiv_reg)
		  && REG_P (SET_DEST (set))
		  && (GET_MODE_BITSIZE (GET_MODE (SET_DEST (set)))
		      <= BITS_PER_WORD)
		  && REGNO (p->new_sp_equiv_reg) == REGNO (SET_DEST (set)));
      p->equiv_reg_src
	= simplify_replace_rtx (SET_SRC (set), stack_pointer_rtx,
				plus_constant (p->sp_equiv_reg,
					       p->sp_offset));
    }

  /* Otherwise, replace any references to SP in the insn to its new value
     and emit the insn.  */
  else
    {
      SET_SRC (set) = simplify_replace_rtx (SET_SRC (set), stack_pointer_rtx,
					    plus_constant (p->sp_equiv_reg,
							   p->sp_offset));
      SET_DEST (set) = simplify_replace_rtx (SET_DEST (set), stack_pointer_rtx,
					     plus_constant (p->sp_equiv_reg,
							    p->sp_offset));
      emit_insn (set);
    }
}

/* Update the tracking information for registers set to constants.  */

static void
update_epilogue_consts (rtx dest, rtx x, void *data)
{
  struct epi_info *p = (struct epi_info *) data;
  rtx new;

  if (!REG_P (dest) || REGNO (dest) >= FIRST_PSEUDO_REGISTER)
    return;

  /* If we are either clobbering a register or doing a partial set,
     show we don't know the value.  */
  else if (GET_CODE (x) == CLOBBER || ! rtx_equal_p (dest, SET_DEST (x)))
    p->const_equiv[REGNO (dest)] = 0;

  /* If we are setting it to a constant, record that constant.  */
  else if (GET_CODE (SET_SRC (x)) == CONST_INT)
    p->const_equiv[REGNO (dest)] = SET_SRC (x);

  /* If this is a binary operation between a register we have been tracking
     and a constant, see if we can compute a new constant value.  */
  else if (ARITHMETIC_P (SET_SRC (x))
	   && REG_P (XEXP (SET_SRC (x), 0))
	   && REGNO (XEXP (SET_SRC (x), 0)) < FIRST_PSEUDO_REGISTER
	   && p->const_equiv[REGNO (XEXP (SET_SRC (x), 0))] != 0
	   && GET_CODE (XEXP (SET_SRC (x), 1)) == CONST_INT
	   && 0 != (new = simplify_binary_operation
		    (GET_CODE (SET_SRC (x)), GET_MODE (dest),
		     p->const_equiv[REGNO (XEXP (SET_SRC (x), 0))],
		     XEXP (SET_SRC (x), 1)))
	   && GET_CODE (new) == CONST_INT)
    p->const_equiv[REGNO (dest)] = new;

  /* Otherwise, we can't do anything with this value.  */
  else
    p->const_equiv[REGNO (dest)] = 0;
}

/* Emit an insn to do the load shown in p->equiv_reg_src, if needed.  */

static void
emit_equiv_load (struct epi_info *p)
{
  if (p->equiv_reg_src != 0)
    {
      rtx dest = p->sp_equiv_reg;

      if (GET_MODE (p->equiv_reg_src) != GET_MODE (dest))
	dest = gen_rtx_REG (GET_MODE (p->equiv_reg_src),
			    REGNO (p->sp_equiv_reg));

      emit_move_insn (dest, p->equiv_reg_src);
      p->equiv_reg_src = 0;
    }
}
#endif

/* APPLE LOCAL begin radar 6163705, Blocks prologues  */

/* The function should only be called for Blocks functions.
   
   On being called, the main instruction list for the Blocks function
   may contain instructions for setting up the ref_decl and byref_decl
   variables in the Block.  Those isns really need to go before the
   function prologue note rather than after.  If such instructions are
   present, they are identifiable by their source line number, which
   will be one line preceding the declaration of the function.  If
   they are present, there will also be a source line note instruction
   for that line.

   This function does a set of things:
   - It finds the first such prologue insn.
   - It finds the last such prologue insn.
   - It changes the insn locator of all such prologue insns to
     the prologue locator.
   - It finds the source line note for the bogus location and
     removes it.
   - It decides if it is safe to place the prolgoue end note
     after the last prologue insn it finds, and if so, returns
     the last prologue insn (otherwise it returns NULL).
   
   This function makes the following checks to determine if it is
   safe to move the prologue end note to just below the last
   prologue insn it finds.  If ALL of the checks succeed then it
   is safe.  If any check fails, this function returns NULL.  The
   checks it makes are:
   
	- There were no INSN_P instructions that occurred before the
	  first prologue insn.
	- If there are any non-prologue insns between the first & last
	  prologue insn, the non-prologue insns do not outnumber the
	 prologue insns.
	- The first prologue insn & the last prologue insn are in the
	  same basic block.
*/

static rtx
find_block_prologue_insns (void)
{
  rtx first_prologue_insn = NULL;
  rtx last_prologue_insn = NULL;
  rtx line_number_note = NULL;
  rtx tmp_insn;
  int num_prologue_insns = 0;
  int total_insns = 0;
  int prologue_line = DECL_SOURCE_LINE (cfun->decl) - 1;
  bool other_insns_before_prologue = false;
  bool start_of_fnbody_found = false;

  /* Go through all the insns and find the first prologue insn, the
     last prologue insn, the source line location note, and whether or
     not there are any "real" insns that occur before the first
     prologue insn.  Re-set the insn locator for prologue insns to the
     prologue locator.  */

  for (tmp_insn = get_insns(); tmp_insn; tmp_insn = NEXT_INSN (tmp_insn))
    {
      if (INSN_P (tmp_insn))
	{
	  if (insn_line (tmp_insn) == prologue_line)
	    {
	      if (!first_prologue_insn)
		first_prologue_insn = tmp_insn;
	      num_prologue_insns++;
	      last_prologue_insn = tmp_insn;
	      INSN_LOCATOR (tmp_insn) = prologue_locator;
	    }
	  else if (!first_prologue_insn
		   && start_of_fnbody_found)
	    other_insns_before_prologue = true;
	}
      else if (NOTE_P (tmp_insn)
	       && NOTE_LINE_NUMBER (tmp_insn) == NOTE_INSN_FUNCTION_BEG)
	start_of_fnbody_found = true;
      else if (NOTE_P (tmp_insn)
	       && (XINT (tmp_insn, 5) == prologue_line))
	line_number_note = tmp_insn;
    }

  /* If there were no prologue insns, return now.  */

  if (!first_prologue_insn)
    return NULL;

  /* If the source location note for the line before the beginning of the
     function was found, remove it.  */

  if (line_number_note)
    remove_insn (line_number_note);

  /* If other real insns got moved above the prologue insns, we can't
     pull out the prologue insns, so return now.  */

  if (other_insns_before_prologue && (optimize > 0))
    return NULL;

  /* Count the number of insns between the first prologue insn and the
     last prologue insn; also count the number of non-prologue insns
     between the first prologue insn and the last prologue insn.  */

  tmp_insn = first_prologue_insn;
  while (tmp_insn != last_prologue_insn)
    {
      total_insns++;
      tmp_insn = NEXT_INSN (tmp_insn);
    }
  total_insns++;

  /* If more than half of the insns between the first & last prologue
     insns are not prologue insns, then there is too much code that
     got moved in between prologue insns (by optimizations), so we
     will not try to pull it out.  */
  
  if ((num_prologue_insns * 2) <= total_insns)
    return NULL;

  /* Make sure all the prologue insns are within one basic block.
     If the insns cross a basic block boundary, then there is a chance
     that moving them will cause incorrect code, so don't do it.  */

  gcc_assert (first_prologue_insn != NULL);
  gcc_assert (last_prologue_insn != NULL);

  if (BLOCK_FOR_INSN (first_prologue_insn) != 
      BLOCK_FOR_INSN (last_prologue_insn))
    return NULL;

  return last_prologue_insn;
}
/* APPLE LOCAL end radar 6163705, Blocks prologues  */

/* Generate the prologue and epilogue RTL if the machine supports it.  Thread
   this into place with notes indicating where the prologue ends and where
   the epilogue begins.  Update the basic block information when possible.  */

void
thread_prologue_and_epilogue_insns (rtx f ATTRIBUTE_UNUSED)
{
  int inserted = 0;
  edge e;
#if defined (HAVE_sibcall_epilogue) || defined (HAVE_epilogue) || defined (HAVE_return) || defined (HAVE_prologue)
  rtx seq;
#endif
#ifdef HAVE_prologue
  rtx prologue_end = NULL_RTX;
#endif
#if defined (HAVE_epilogue) || defined(HAVE_return)
  rtx epilogue_end = NULL_RTX;
#endif
  edge_iterator ei;

#ifdef HAVE_prologue
  if (HAVE_prologue)
    {
      /* APPLE LOCAL begin radar 6163705, Blocks prologues  */
      rtx last_prologue_insn = NULL;

      if (BLOCK_SYNTHESIZED_FUNC (cfun->decl))
	last_prologue_insn = find_block_prologue_insns();
      /* APPLE LOCAL end radar 6163705, Blocks prologues  */

      start_sequence ();
      seq = gen_prologue ();
      emit_insn (seq);

      /* Retain a map of the prologue insns.  */
      record_insns (seq, &prologue);
      /* APPLE LOCAL begin radar 6163705, Blocks prologues  */
      if (!last_prologue_insn)
	prologue_end = emit_note (NOTE_INSN_PROLOGUE_END);
      /* APPLE LOCAL end radar 6163705, Blocks prologues  */
 
#ifndef PROFILE_BEFORE_PROLOGUE
      /* Ensure that instructions are not moved into the prologue when
	 profiling is on.  The call to the profiling routine can be
	 emitted within the live range of a call-clobbered register.  */
      if (current_function_profile)
	emit_insn (gen_rtx_ASM_INPUT (VOIDmode, ""));
#endif

      seq = get_insns ();
      end_sequence ();
      set_insn_locators (seq, prologue_locator);

      /* Can't deal with multiple successors of the entry block
         at the moment.  Function should always have at least one
         entry point.  */
      gcc_assert (single_succ_p (ENTRY_BLOCK_PTR));

      insert_insn_on_edge (seq, single_succ_edge (ENTRY_BLOCK_PTR));
      inserted = 1;

      /* APPLE LOCAL begin radar 6163705, Blocks prologues  */
      if (last_prologue_insn)
	emit_note_after (NOTE_INSN_PROLOGUE_END, last_prologue_insn);
      /* APPLE LOCAL end radar 6163705, Blocks prologues  */    }
#endif

  /* If the exit block has no non-fake predecessors, we don't need
     an epilogue.  */
  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    if ((e->flags & EDGE_FAKE) == 0)
      break;
  if (e == NULL)
    goto epilogue_done;

#ifdef HAVE_return
  if (optimize && HAVE_return)
    {
      /* If we're allowed to generate a simple return instruction,
	 then by definition we don't need a full epilogue.  Examine
	 the block that falls through to EXIT.   If it does not
	 contain any code, examine its predecessors and try to
	 emit (conditional) return instructions.  */

      basic_block last;
      rtx label;

      FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
	if (e->flags & EDGE_FALLTHRU)
	  break;
      if (e == NULL)
	goto epilogue_done;
      last = e->src;

      /* Verify that there are no active instructions in the last block.  */
      label = BB_END (last);
      while (label && !LABEL_P (label))
	{
	  if (active_insn_p (label))
	    break;
	  label = PREV_INSN (label);
	}

      if (BB_HEAD (last) == label && LABEL_P (label))
	{
	  edge_iterator ei2;
	  rtx epilogue_line_note = NULL_RTX;

	  /* Locate the line number associated with the closing brace,
	     if we can find one.  */
	  for (seq = get_last_insn ();
	       seq && ! active_insn_p (seq);
	       seq = PREV_INSN (seq))
	    if (NOTE_P (seq) && NOTE_LINE_NUMBER (seq) > 0)
	      {
		epilogue_line_note = seq;
		break;
	      }

	  for (ei2 = ei_start (last->preds); (e = ei_safe_edge (ei2)); )
	    {
	      basic_block bb = e->src;
	      rtx jump;

	      if (bb == ENTRY_BLOCK_PTR)
		{
		  ei_next (&ei2);
		  continue;
		}

	      jump = BB_END (bb);
	      if (!JUMP_P (jump) || JUMP_LABEL (jump) != label)
		{
		  ei_next (&ei2);
		  continue;
		}

	      /* If we have an unconditional jump, we can replace that
		 with a simple return instruction.  */
	      if (simplejump_p (jump))
		{
		  emit_return_into_block (bb, epilogue_line_note);
		  delete_insn (jump);
		}

	      /* If we have a conditional jump, we can try to replace
		 that with a conditional return instruction.  */
	      else if (condjump_p (jump))
		{
		  if (! redirect_jump (jump, 0, 0))
		    {
		      ei_next (&ei2);
		      continue;
		    }

		  /* If this block has only one successor, it both jumps
		     and falls through to the fallthru block, so we can't
		     delete the edge.  */
		  if (single_succ_p (bb))
		    {
		      ei_next (&ei2);
		      continue;
		    }
		}
	      else
		{
		  ei_next (&ei2);
		  continue;
		}

	      /* Fix up the CFG for the successful change we just made.  */
	      redirect_edge_succ (e, EXIT_BLOCK_PTR);
	    }

	  /* Emit a return insn for the exit fallthru block.  Whether
	     this is still reachable will be determined later.  */

	  emit_barrier_after (BB_END (last));
	  emit_return_into_block (last, epilogue_line_note);
	  epilogue_end = BB_END (last);
	  single_succ_edge (last)->flags &= ~EDGE_FALLTHRU;
	  goto epilogue_done;
	}
    }
#endif
  /* Find the edge that falls through to EXIT.  Other edges may exist
     due to RETURN instructions, but those don't need epilogues.
     There really shouldn't be a mixture -- either all should have
     been converted or none, however...  */

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    if (e->flags & EDGE_FALLTHRU)
      break;
  if (e == NULL)
    goto epilogue_done;

#ifdef HAVE_epilogue
  if (HAVE_epilogue)
    {
      start_sequence ();
      epilogue_end = emit_note (NOTE_INSN_EPILOGUE_BEG);

      seq = gen_epilogue ();

#ifdef INCOMING_RETURN_ADDR_RTX
      /* If this function returns with the stack depressed and we can support
	 it, massage the epilogue to actually do that.  */
      if (TREE_CODE (TREE_TYPE (current_function_decl)) == FUNCTION_TYPE
	  && TYPE_RETURNS_STACK_DEPRESSED (TREE_TYPE (current_function_decl)))
	seq = keep_stack_depressed (seq);
#endif

      emit_jump_insn (seq);

      /* Retain a map of the epilogue insns.  */
      record_insns (seq, &epilogue);
      set_insn_locators (seq, epilogue_locator);

      seq = get_insns ();
      end_sequence ();

      insert_insn_on_edge (seq, e);
      inserted = 1;
    }
  else
#endif
    {
      basic_block cur_bb;

      if (! next_active_insn (BB_END (e->src)))
	goto epilogue_done;
      /* We have a fall-through edge to the exit block, the source is not
         at the end of the function, and there will be an assembler epilogue
         at the end of the function.
         We can't use force_nonfallthru here, because that would try to
         use return.  Inserting a jump 'by hand' is extremely messy, so
	 we take advantage of cfg_layout_finalize using
	fixup_fallthru_exit_predecessor.  */
      cfg_layout_initialize (0);
      FOR_EACH_BB (cur_bb)
	if (cur_bb->index >= NUM_FIXED_BLOCKS
	    && cur_bb->next_bb->index >= NUM_FIXED_BLOCKS)
	  cur_bb->aux = cur_bb->next_bb;
      cfg_layout_finalize ();
    }
epilogue_done:

  if (inserted)
    commit_edge_insertions ();

#ifdef HAVE_sibcall_epilogue
  /* Emit sibling epilogues before any sibling call sites.  */
  for (ei = ei_start (EXIT_BLOCK_PTR->preds); (e = ei_safe_edge (ei)); )
    {
      basic_block bb = e->src;
      rtx insn = BB_END (bb);

      if (!CALL_P (insn)
	  || ! SIBLING_CALL_P (insn))
	{
	  ei_next (&ei);
	  continue;
	}

      start_sequence ();
      emit_insn (gen_sibcall_epilogue ());
      seq = get_insns ();
      end_sequence ();

      /* Retain a map of the epilogue insns.  Used in life analysis to
	 avoid getting rid of sibcall epilogue insns.  Do this before we
	 actually emit the sequence.  */
      record_insns (seq, &sibcall_epilogue);
      set_insn_locators (seq, epilogue_locator);

      emit_insn_before (seq, insn);
      ei_next (&ei);
    }
#endif

#ifdef HAVE_prologue
  /* This is probably all useless now that we use locators.  */
  if (prologue_end)
    {
      rtx insn, prev;

      /* GDB handles `break f' by setting a breakpoint on the first
	 line note after the prologue.  Which means (1) that if
	 there are line number notes before where we inserted the
	 prologue we should move them, and (2) we should generate a
	 note before the end of the first basic block, if there isn't
	 one already there.

	 ??? This behavior is completely broken when dealing with
	 multiple entry functions.  We simply place the note always
	 into first basic block and let alternate entry points
	 to be missed.
       */

      for (insn = prologue_end; insn; insn = prev)
	{
	  prev = PREV_INSN (insn);
	  if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
	    {
	      /* Note that we cannot reorder the first insn in the
		 chain, since rest_of_compilation relies on that
		 remaining constant.  */
	      if (prev == NULL)
		break;
	      reorder_insns (insn, insn, prologue_end);
	    }
	}

      /* Find the last line number note in the first block.  */
      for (insn = BB_END (ENTRY_BLOCK_PTR->next_bb);
	   insn != prologue_end && insn;
	   insn = PREV_INSN (insn))
	if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
	  break;

      /* If we didn't find one, make a copy of the first line number
	 we run across.  */
      if (! insn)
	{
	  for (insn = next_active_insn (prologue_end);
	       insn;
	       insn = PREV_INSN (insn))
	    if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
	      {
		emit_note_copy_after (insn, prologue_end);
		break;
	      }
	}
    }
#endif
#ifdef HAVE_epilogue
  if (epilogue_end)
    {
      rtx insn, next;

      /* Similarly, move any line notes that appear after the epilogue.
         There is no need, however, to be quite so anal about the existence
	 of such a note.  Also move the NOTE_INSN_FUNCTION_END and (possibly)
	 NOTE_INSN_FUNCTION_BEG notes, as those can be relevant for debug
	 info generation.  */
      for (insn = epilogue_end; insn; insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (NOTE_P (insn) 
	      && (NOTE_LINE_NUMBER (insn) > 0
		  || NOTE_LINE_NUMBER (insn) == NOTE_INSN_FUNCTION_BEG
		  || NOTE_LINE_NUMBER (insn) == NOTE_INSN_FUNCTION_END))
	    reorder_insns (insn, insn, PREV_INSN (epilogue_end));
	}
    }
#endif
}

/* Reposition the prologue-end and epilogue-begin notes after instruction
   scheduling and delayed branch scheduling.  */

void
reposition_prologue_and_epilogue_notes (rtx f ATTRIBUTE_UNUSED)
{
#if defined (HAVE_prologue) || defined (HAVE_epilogue)
  rtx insn, last, note;
  int len;

  if ((len = VEC_length (int, prologue)) > 0)
    {
      last = 0, note = 0;

      /* Scan from the beginning until we reach the last prologue insn.
	 We apparently can't depend on basic_block_{head,end} after
	 reorg has run.  */
      for (insn = f; insn; insn = NEXT_INSN (insn))
	{
	  if (NOTE_P (insn))
	    {
	      if (NOTE_LINE_NUMBER (insn) == NOTE_INSN_PROLOGUE_END)
		note = insn;
	    }
	  else if (contains (insn, &prologue))
	    {
	      last = insn;
	      if (--len == 0)
		break;
	    }
	}

      if (last)
	{
	  /* Find the prologue-end note if we haven't already, and
	     move it to just after the last prologue insn.  */
	  if (note == 0)
	    {
	      for (note = last; (note = NEXT_INSN (note));)
		if (NOTE_P (note)
		    && NOTE_LINE_NUMBER (note) == NOTE_INSN_PROLOGUE_END)
		  break;
	    }

	  /* Avoid placing note between CODE_LABEL and BASIC_BLOCK note.  */
	  if (LABEL_P (last))
	    last = NEXT_INSN (last);
	  reorder_insns (note, note, last);
	}
    }

  if ((len = VEC_length (int, epilogue)) > 0)
    {
      last = 0, note = 0;

      /* Scan from the end until we reach the first epilogue insn.
	 We apparently can't depend on basic_block_{head,end} after
	 reorg has run.  */
      for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
	{
	  if (NOTE_P (insn))
	    {
	      if (NOTE_LINE_NUMBER (insn) == NOTE_INSN_EPILOGUE_BEG)
		note = insn;
	    }
	  else if (contains (insn, &epilogue))
	    {
	      last = insn;
	      if (--len == 0)
		break;
	    }
	}

      if (last)
	{
	  /* Find the epilogue-begin note if we haven't already, and
	     move it to just before the first epilogue insn.  */
	  if (note == 0)
	    {
	      for (note = insn; (note = PREV_INSN (note));)
		if (NOTE_P (note)
		    && NOTE_LINE_NUMBER (note) == NOTE_INSN_EPILOGUE_BEG)
		  break;
	    }

	  if (PREV_INSN (last) != note)
	    reorder_insns (note, note, PREV_INSN (last));
	}
    }
#endif /* HAVE_prologue or HAVE_epilogue */
}

/* Resets insn_block_boundaries array.  */

void
reset_block_changes (void)
{
  cfun->ib_boundaries_block = VEC_alloc (tree, gc, 100);
  VEC_quick_push (tree, cfun->ib_boundaries_block, NULL_TREE);
}

/* Record the boundary for BLOCK.  */
void
record_block_change (tree block)
{
  int i, n;
  tree last_block;

  if (!block)
    return;

  if(!cfun->ib_boundaries_block)
    return;

  last_block = VEC_pop (tree, cfun->ib_boundaries_block);
  n = get_max_uid ();
  for (i = VEC_length (tree, cfun->ib_boundaries_block); i < n; i++)
    VEC_safe_push (tree, gc, cfun->ib_boundaries_block, last_block);

  VEC_safe_push (tree, gc, cfun->ib_boundaries_block, block);
}

/* Finishes record of boundaries.  */
void
finalize_block_changes (void)
{
  record_block_change (DECL_INITIAL (current_function_decl));
}

/* For INSN return the BLOCK it belongs to.  */ 
void
check_block_change (rtx insn, tree *block)
{
  unsigned uid = INSN_UID (insn);

  if (uid >= VEC_length (tree, cfun->ib_boundaries_block))
    return;

  *block = VEC_index (tree, cfun->ib_boundaries_block, uid);
}

/* Releases the ib_boundaries_block records.  */
void
free_block_changes (void)
{
  VEC_free (tree, gc, cfun->ib_boundaries_block);
}

/* Returns the name of the current function.  */
const char *
current_function_name (void)
{
  return lang_hooks.decl_printable_name (cfun->decl, 2);
}


static unsigned int
rest_of_handle_check_leaf_regs (void)
{
#ifdef LEAF_REGISTERS
  current_function_uses_only_leaf_regs
    = optimize > 0 && only_leaf_regs_used () && leaf_function_p ();
#endif
  return 0;
}

/* Insert a TYPE into the used types hash table of CFUN.  */
static void
used_types_insert_helper (tree type, struct function *func)
{
  if (type != NULL && func != NULL)
    {
      void **slot;

      if (func->used_types_hash == NULL)
	func->used_types_hash = htab_create_ggc (37, htab_hash_pointer,
						 htab_eq_pointer, NULL);
      slot = htab_find_slot (func->used_types_hash, type, INSERT);
      if (*slot == NULL)
	*slot = type;
    }
}

/* Given a type, insert it into the used hash table in cfun.  */
void
used_types_insert (tree t)
{
  while (POINTER_TYPE_P (t) || TREE_CODE (t) == ARRAY_TYPE)
    t = TREE_TYPE (t);
  t = TYPE_MAIN_VARIANT (t);
  if (debug_info_level > DINFO_LEVEL_NONE)
    used_types_insert_helper (t, cfun);
}

struct tree_opt_pass pass_leaf_regs =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */
  rest_of_handle_check_leaf_regs,       /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};


#include "gt-function.h"
