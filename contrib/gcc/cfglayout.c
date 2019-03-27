/* Basic block reordering routines for the GNU compiler.
   Copyright (C) 2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "hard-reg-set.h"
#include "obstack.h"
#include "basic-block.h"
#include "insn-config.h"
#include "output.h"
#include "function.h"
#include "cfglayout.h"
#include "cfgloop.h"
#include "target.h"
#include "ggc.h"
#include "alloc-pool.h"
#include "flags.h"
#include "tree-pass.h"
#include "vecprim.h"

/* Holds the interesting trailing notes for the function.  */
rtx cfg_layout_function_footer, cfg_layout_function_header;

static rtx skip_insns_after_block (basic_block);
static void record_effective_endpoints (void);
static rtx label_for_bb (basic_block);
static void fixup_reorder_chain (void);

static void set_block_levels (tree, int);
static void change_scope (rtx, tree, tree);

void verify_insn_chain (void);
static void fixup_fallthru_exit_predecessor (void);
static tree insn_scope (rtx);

rtx
unlink_insn_chain (rtx first, rtx last)
{
  rtx prevfirst = PREV_INSN (first);
  rtx nextlast = NEXT_INSN (last);

  PREV_INSN (first) = NULL;
  NEXT_INSN (last) = NULL;
  if (prevfirst)
    NEXT_INSN (prevfirst) = nextlast;
  if (nextlast)
    PREV_INSN (nextlast) = prevfirst;
  else
    set_last_insn (prevfirst);
  if (!prevfirst)
    set_first_insn (nextlast);
  return first;
}

/* Skip over inter-block insns occurring after BB which are typically
   associated with BB (e.g., barriers). If there are any such insns,
   we return the last one. Otherwise, we return the end of BB.  */

static rtx
skip_insns_after_block (basic_block bb)
{
  rtx insn, last_insn, next_head, prev;

  next_head = NULL_RTX;
  if (bb->next_bb != EXIT_BLOCK_PTR)
    next_head = BB_HEAD (bb->next_bb);

  for (last_insn = insn = BB_END (bb); (insn = NEXT_INSN (insn)) != 0; )
    {
      if (insn == next_head)
	break;

      switch (GET_CODE (insn))
	{
	case BARRIER:
	  last_insn = insn;
	  continue;

	case NOTE:
	  switch (NOTE_LINE_NUMBER (insn))
	    {
	    case NOTE_INSN_BLOCK_END:
	      last_insn = insn;
	      continue;
	    case NOTE_INSN_DELETED:
	    case NOTE_INSN_DELETED_LABEL:
	      continue;

	    default:
	      continue;
	      break;
	    }
	  break;

	case CODE_LABEL:
	  if (NEXT_INSN (insn)
	      && JUMP_P (NEXT_INSN (insn))
	      && (GET_CODE (PATTERN (NEXT_INSN (insn))) == ADDR_VEC
		  || GET_CODE (PATTERN (NEXT_INSN (insn))) == ADDR_DIFF_VEC))
	    {
	      insn = NEXT_INSN (insn);
	      last_insn = insn;
	      continue;
	    }
	  break;

	default:
	  break;
	}

      break;
    }

  /* It is possible to hit contradictory sequence.  For instance:

     jump_insn
     NOTE_INSN_BLOCK_BEG
     barrier

     Where barrier belongs to jump_insn, but the note does not.  This can be
     created by removing the basic block originally following
     NOTE_INSN_BLOCK_BEG.  In such case reorder the notes.  */

  for (insn = last_insn; insn != BB_END (bb); insn = prev)
    {
      prev = PREV_INSN (insn);
      if (NOTE_P (insn))
	switch (NOTE_LINE_NUMBER (insn))
	  {
	  case NOTE_INSN_BLOCK_END:
	  case NOTE_INSN_DELETED:
	  case NOTE_INSN_DELETED_LABEL:
	    continue;
	  default:
	    reorder_insns (insn, insn, last_insn);
	  }
    }

  return last_insn;
}

/* Locate or create a label for a given basic block.  */

static rtx
label_for_bb (basic_block bb)
{
  rtx label = BB_HEAD (bb);

  if (!LABEL_P (label))
    {
      if (dump_file)
	fprintf (dump_file, "Emitting label for block %d\n", bb->index);

      label = block_label (bb);
    }

  return label;
}

/* Locate the effective beginning and end of the insn chain for each
   block, as defined by skip_insns_after_block above.  */

static void
record_effective_endpoints (void)
{
  rtx next_insn;
  basic_block bb;
  rtx insn;

  for (insn = get_insns ();
       insn
       && NOTE_P (insn)
       && NOTE_LINE_NUMBER (insn) != NOTE_INSN_BASIC_BLOCK;
       insn = NEXT_INSN (insn))
    continue;
  /* No basic blocks at all?  */
  gcc_assert (insn);

  if (PREV_INSN (insn))
    cfg_layout_function_header =
	    unlink_insn_chain (get_insns (), PREV_INSN (insn));
  else
    cfg_layout_function_header = NULL_RTX;

  next_insn = get_insns ();
  FOR_EACH_BB (bb)
    {
      rtx end;

      if (PREV_INSN (BB_HEAD (bb)) && next_insn != BB_HEAD (bb))
	bb->il.rtl->header = unlink_insn_chain (next_insn,
					      PREV_INSN (BB_HEAD (bb)));
      end = skip_insns_after_block (bb);
      if (NEXT_INSN (BB_END (bb)) && BB_END (bb) != end)
	bb->il.rtl->footer = unlink_insn_chain (NEXT_INSN (BB_END (bb)), end);
      next_insn = NEXT_INSN (BB_END (bb));
    }

  cfg_layout_function_footer = next_insn;
  if (cfg_layout_function_footer)
    cfg_layout_function_footer = unlink_insn_chain (cfg_layout_function_footer, get_last_insn ());
}

/* Data structures representing mapping of INSN_LOCATOR into scope blocks, line
   numbers and files.  In order to be GGC friendly we need to use separate
   varrays.  This also slightly improve the memory locality in binary search.
   The _locs array contains locators where the given property change.  The
   block_locators_blocks contains the scope block that is used for all insn
   locator greater than corresponding block_locators_locs value and smaller
   than the following one.  Similarly for the other properties.  */
static VEC(int,heap) *block_locators_locs;
static GTY(()) VEC(tree,gc) *block_locators_blocks;
static VEC(int,heap) *line_locators_locs;
static VEC(int,heap) *line_locators_lines;
static VEC(int,heap) *file_locators_locs;
static GTY(()) varray_type file_locators_files;
int prologue_locator;
int epilogue_locator;

/* During the RTL expansion the lexical blocks and line numbers are
   represented via INSN_NOTEs.  Replace them by representation using
   INSN_LOCATORs.  */

unsigned int
insn_locators_initialize (void)
{
  tree block = NULL;
  tree last_block = NULL;
  rtx insn, next;
  int loc = 0;
  int line_number = 0, last_line_number = 0;
  const char *file_name = NULL, *last_file_name = NULL;

  prologue_locator = epilogue_locator = 0;

  block_locators_locs = VEC_alloc (int, heap, 32);
  block_locators_blocks = VEC_alloc (tree, gc, 32);
  line_locators_locs = VEC_alloc (int, heap, 32);
  line_locators_lines = VEC_alloc (int, heap, 32);
  file_locators_locs = VEC_alloc (int, heap, 32);
  VARRAY_CHAR_PTR_INIT (file_locators_files, 32, "file_locators_files");

  for (insn = get_insns (); insn; insn = next)
    {
      int active = 0;

      next = NEXT_INSN (insn);

      if (NOTE_P (insn))
	{
	  gcc_assert (NOTE_LINE_NUMBER (insn) != NOTE_INSN_BLOCK_BEG
		      && NOTE_LINE_NUMBER (insn) != NOTE_INSN_BLOCK_END);
	  if (NOTE_LINE_NUMBER (insn) > 0)
	    {
	      expanded_location xloc;
	      NOTE_EXPANDED_LOCATION (xloc, insn);
	      line_number = xloc.line;
	      file_name = xloc.file;
	    }
	}
      else
	active = (active_insn_p (insn)
		  && GET_CODE (PATTERN (insn)) != ADDR_VEC
		  && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC);

      check_block_change (insn, &block);

      if (active
	  || !next
	  || (!prologue_locator && file_name))
	{
	  if (last_block != block)
	    {
	      loc++;
	      VEC_safe_push (int, heap, block_locators_locs, loc);
	      VEC_safe_push (tree, gc, block_locators_blocks, block);
	      last_block = block;
	    }
	  if (last_line_number != line_number)
	    {
	      loc++;
	      VEC_safe_push (int, heap, line_locators_locs, loc);
	      VEC_safe_push (int, heap, line_locators_lines, line_number);
	      last_line_number = line_number;
	    }
	  if (last_file_name != file_name)
	    {
	      loc++;
	      VEC_safe_push (int, heap, file_locators_locs, loc);
	      VARRAY_PUSH_CHAR_PTR (file_locators_files, (char *) file_name);
	      last_file_name = file_name;
	    }
	  if (!prologue_locator && file_name)
	    prologue_locator = loc;
	  if (!next)
	    epilogue_locator = loc;
	  if (active)
	    INSN_LOCATOR (insn) = loc;
	}
    }

  /* Tag the blocks with a depth number so that change_scope can find
     the common parent easily.  */
  set_block_levels (DECL_INITIAL (cfun->decl), 0);

  free_block_changes ();
  return 0;
}

struct tree_opt_pass pass_insn_locators_initialize =
{
  "locators",                           /* name */
  NULL,                                 /* gate */
  insn_locators_initialize,             /* execute */
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


/* For each lexical block, set BLOCK_NUMBER to the depth at which it is
   found in the block tree.  */

static void
set_block_levels (tree block, int level)
{
  while (block)
    {
      BLOCK_NUMBER (block) = level;
      set_block_levels (BLOCK_SUBBLOCKS (block), level + 1);
      block = BLOCK_CHAIN (block);
    }
}

/* Return sope resulting from combination of S1 and S2.  */
static tree
choose_inner_scope (tree s1, tree s2)
{
   if (!s1)
     return s2;
   if (!s2)
     return s1;
   if (BLOCK_NUMBER (s1) > BLOCK_NUMBER (s2))
     return s1;
   return s2;
}

/* Emit lexical block notes needed to change scope from S1 to S2.  */

static void
change_scope (rtx orig_insn, tree s1, tree s2)
{
  rtx insn = orig_insn;
  tree com = NULL_TREE;
  tree ts1 = s1, ts2 = s2;
  tree s;

  while (ts1 != ts2)
    {
      gcc_assert (ts1 && ts2);
      if (BLOCK_NUMBER (ts1) > BLOCK_NUMBER (ts2))
	ts1 = BLOCK_SUPERCONTEXT (ts1);
      else if (BLOCK_NUMBER (ts1) < BLOCK_NUMBER (ts2))
	ts2 = BLOCK_SUPERCONTEXT (ts2);
      else
	{
	  ts1 = BLOCK_SUPERCONTEXT (ts1);
	  ts2 = BLOCK_SUPERCONTEXT (ts2);
	}
    }
  com = ts1;

  /* Close scopes.  */
  s = s1;
  while (s != com)
    {
      rtx note = emit_note_before (NOTE_INSN_BLOCK_END, insn);
      NOTE_BLOCK (note) = s;
      s = BLOCK_SUPERCONTEXT (s);
    }

  /* Open scopes.  */
  s = s2;
  while (s != com)
    {
      insn = emit_note_before (NOTE_INSN_BLOCK_BEG, insn);
      NOTE_BLOCK (insn) = s;
      s = BLOCK_SUPERCONTEXT (s);
    }
}

/* Return lexical scope block insn belong to.  */
static tree
insn_scope (rtx insn)
{
  int max = VEC_length (int, block_locators_locs);
  int min = 0;
  int loc = INSN_LOCATOR (insn);

  /* When block_locators_locs was initialized, the pro- and epilogue
     insns didn't exist yet and can therefore not be found this way.
     But we know that they belong to the outer most block of the
     current function.
     Without this test, the prologue would be put inside the block of
     the first valid instruction in the function and when that first
     insn is part of an inlined function then the low_pc of that
     inlined function is messed up.  Likewise for the epilogue and
     the last valid instruction.  */
  if (loc == prologue_locator || loc == epilogue_locator)
    return DECL_INITIAL (cfun->decl);

  if (!max || !loc)
    return NULL;
  while (1)
    {
      int pos = (min + max) / 2;
      int tmp = VEC_index (int, block_locators_locs, pos);

      if (tmp <= loc && min != pos)
	min = pos;
      else if (tmp > loc && max != pos)
	max = pos;
      else
	{
	  min = pos;
	  break;
	}
    }
  return VEC_index (tree, block_locators_blocks, min);
}

/* Return line number of the statement specified by the locator.  */
int
locator_line (int loc)
{
  int max = VEC_length (int, line_locators_locs);
  int min = 0;

  if (!max || !loc)
    return 0;
  while (1)
    {
      int pos = (min + max) / 2;
      int tmp = VEC_index (int, line_locators_locs, pos);

      if (tmp <= loc && min != pos)
	min = pos;
      else if (tmp > loc && max != pos)
	max = pos;
      else
	{
	  min = pos;
	  break;
	}
    }
  return VEC_index (int, line_locators_lines, min);
}

/* Return line number of the statement that produced this insn.  */
int
insn_line (rtx insn)
{
  return locator_line (INSN_LOCATOR (insn));
}

/* Return source file of the statement specified by LOC.  */
const char *
locator_file (int loc)
{
  int max = VEC_length (int, file_locators_locs);
  int min = 0;

  if (!max || !loc)
    return NULL;
  while (1)
    {
      int pos = (min + max) / 2;
      int tmp = VEC_index (int, file_locators_locs, pos);

      if (tmp <= loc && min != pos)
	min = pos;
      else if (tmp > loc && max != pos)
	max = pos;
      else
	{
	  min = pos;
	  break;
	}
    }
   return VARRAY_CHAR_PTR (file_locators_files, min);
}

/* Return source file of the statement that produced this insn.  */
const char *
insn_file (rtx insn)
{
  return locator_file (INSN_LOCATOR (insn));
}

/* Rebuild all the NOTE_INSN_BLOCK_BEG and NOTE_INSN_BLOCK_END notes based
   on the scope tree and the newly reordered instructions.  */

void
reemit_insn_block_notes (void)
{
  tree cur_block = DECL_INITIAL (cfun->decl);
  rtx insn, note;

  insn = get_insns ();
  if (!active_insn_p (insn))
    insn = next_active_insn (insn);
  for (; insn; insn = next_active_insn (insn))
    {
      tree this_block;

      /* Avoid putting scope notes between jump table and its label.  */
      if (JUMP_P (insn)
	  && (GET_CODE (PATTERN (insn)) == ADDR_VEC
	      || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC))
	continue;

      this_block = insn_scope (insn);
      /* For sequences compute scope resulting from merging all scopes
	 of instructions nested inside.  */
      if (GET_CODE (PATTERN (insn)) == SEQUENCE)
	{
	  int i;
	  rtx body = PATTERN (insn);

	  this_block = NULL;
	  for (i = 0; i < XVECLEN (body, 0); i++)
	    this_block = choose_inner_scope (this_block,
					 insn_scope (XVECEXP (body, 0, i)));
	}
      if (! this_block)
	continue;

      if (this_block != cur_block)
	{
	  change_scope (insn, cur_block, this_block);
	  cur_block = this_block;
	}
    }

  /* change_scope emits before the insn, not after.  */
  note = emit_note (NOTE_INSN_DELETED);
  change_scope (note, cur_block, DECL_INITIAL (cfun->decl));
  delete_insn (note);

  reorder_blocks ();
}

/* Given a reorder chain, rearrange the code to match.  */

static void
fixup_reorder_chain (void)
{
  basic_block bb, prev_bb;
  int index;
  rtx insn = NULL;

  if (cfg_layout_function_header)
    {
      set_first_insn (cfg_layout_function_header);
      insn = cfg_layout_function_header;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
    }

  /* First do the bulk reordering -- rechain the blocks without regard to
     the needed changes to jumps and labels.  */

  for (bb = ENTRY_BLOCK_PTR->next_bb, index = NUM_FIXED_BLOCKS;
       bb != 0;
       bb = bb->aux, index++)
    {
      if (bb->il.rtl->header)
	{
	  if (insn)
	    NEXT_INSN (insn) = bb->il.rtl->header;
	  else
	    set_first_insn (bb->il.rtl->header);
	  PREV_INSN (bb->il.rtl->header) = insn;
	  insn = bb->il.rtl->header;
	  while (NEXT_INSN (insn))
	    insn = NEXT_INSN (insn);
	}
      if (insn)
	NEXT_INSN (insn) = BB_HEAD (bb);
      else
	set_first_insn (BB_HEAD (bb));
      PREV_INSN (BB_HEAD (bb)) = insn;
      insn = BB_END (bb);
      if (bb->il.rtl->footer)
	{
	  NEXT_INSN (insn) = bb->il.rtl->footer;
	  PREV_INSN (bb->il.rtl->footer) = insn;
	  while (NEXT_INSN (insn))
	    insn = NEXT_INSN (insn);
	}
    }

  gcc_assert (index == n_basic_blocks);

  NEXT_INSN (insn) = cfg_layout_function_footer;
  if (cfg_layout_function_footer)
    PREV_INSN (cfg_layout_function_footer) = insn;

  while (NEXT_INSN (insn))
    insn = NEXT_INSN (insn);

  set_last_insn (insn);
#ifdef ENABLE_CHECKING
  verify_insn_chain ();
#endif
  delete_dead_jumptables ();

  /* Now add jumps and labels as needed to match the blocks new
     outgoing edges.  */

  for (bb = ENTRY_BLOCK_PTR->next_bb; bb ; bb = bb->aux)
    {
      edge e_fall, e_taken, e;
      rtx bb_end_insn;
      basic_block nb;
      edge_iterator ei;

      if (EDGE_COUNT (bb->succs) == 0)
	continue;

      /* Find the old fallthru edge, and another non-EH edge for
	 a taken jump.  */
      e_taken = e_fall = NULL;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->flags & EDGE_FALLTHRU)
	  e_fall = e;
	else if (! (e->flags & EDGE_EH))
	  e_taken = e;

      bb_end_insn = BB_END (bb);
      if (JUMP_P (bb_end_insn))
	{
	  if (any_condjump_p (bb_end_insn))
	    {
	      /* If the old fallthru is still next, nothing to do.  */
	      if (bb->aux == e_fall->dest
		  || e_fall->dest == EXIT_BLOCK_PTR)
		continue;

	      /* The degenerated case of conditional jump jumping to the next
		 instruction can happen for jumps with side effects.  We need
		 to construct a forwarder block and this will be done just
		 fine by force_nonfallthru below.  */
	      if (!e_taken)
		;

	      /* There is another special case: if *neither* block is next,
		 such as happens at the very end of a function, then we'll
		 need to add a new unconditional jump.  Choose the taken
		 edge based on known or assumed probability.  */
	      else if (bb->aux != e_taken->dest)
		{
		  rtx note = find_reg_note (bb_end_insn, REG_BR_PROB, 0);

		  if (note
		      && INTVAL (XEXP (note, 0)) < REG_BR_PROB_BASE / 2
		      && invert_jump (bb_end_insn,
				      (e_fall->dest == EXIT_BLOCK_PTR
				       ? NULL_RTX
				       : label_for_bb (e_fall->dest)), 0))
		    {
		      e_fall->flags &= ~EDGE_FALLTHRU;
#ifdef ENABLE_CHECKING
		      gcc_assert (could_fall_through
				  (e_taken->src, e_taken->dest));
#endif
		      e_taken->flags |= EDGE_FALLTHRU;
		      update_br_prob_note (bb);
		      e = e_fall, e_fall = e_taken, e_taken = e;
		    }
		}

	      /* If the "jumping" edge is a crossing edge, and the fall
		 through edge is non-crossing, leave things as they are.  */
	      else if ((e_taken->flags & EDGE_CROSSING)
		       && !(e_fall->flags & EDGE_CROSSING))
		continue;

	      /* Otherwise we can try to invert the jump.  This will
		 basically never fail, however, keep up the pretense.  */
	      else if (invert_jump (bb_end_insn,
				    (e_fall->dest == EXIT_BLOCK_PTR
				     ? NULL_RTX
				     : label_for_bb (e_fall->dest)), 0))
		{
		  e_fall->flags &= ~EDGE_FALLTHRU;
#ifdef ENABLE_CHECKING
		  gcc_assert (could_fall_through
			      (e_taken->src, e_taken->dest));
#endif
		  e_taken->flags |= EDGE_FALLTHRU;
		  update_br_prob_note (bb);
		  continue;
		}
	    }
	  else
	    {
	      /* Otherwise we have some return, switch or computed
		 jump.  In the 99% case, there should not have been a
		 fallthru edge.  */
	      gcc_assert (returnjump_p (bb_end_insn) || !e_fall);
	      continue;
	    }
	}
      else
	{
	  /* No fallthru implies a noreturn function with EH edges, or
	     something similarly bizarre.  In any case, we don't need to
	     do anything.  */
	  if (! e_fall)
	    continue;

	  /* If the fallthru block is still next, nothing to do.  */
	  if (bb->aux == e_fall->dest)
	    continue;

	  /* A fallthru to exit block.  */
	  if (e_fall->dest == EXIT_BLOCK_PTR)
	    continue;
	}

      /* We got here if we need to add a new jump insn.  */
      nb = force_nonfallthru (e_fall);
      if (nb)
	{
	  nb->il.rtl->visited = 1;
	  nb->aux = bb->aux;
	  bb->aux = nb;
	  /* Don't process this new block.  */
	  bb = nb;

	  /* Make sure new bb is tagged for correct section (same as
	     fall-thru source, since you cannot fall-throu across
	     section boundaries).  */
	  BB_COPY_PARTITION (e_fall->src, single_pred (bb));
	  if (flag_reorder_blocks_and_partition
	      && targetm.have_named_sections
	      && JUMP_P (BB_END (bb))
	      && !any_condjump_p (BB_END (bb))
	      && (EDGE_SUCC (bb, 0)->flags & EDGE_CROSSING))
	    REG_NOTES (BB_END (bb)) = gen_rtx_EXPR_LIST
	      (REG_CROSSING_JUMP, NULL_RTX, REG_NOTES (BB_END (bb)));
	}
    }

  /* Put basic_block_info in the new order.  */

  if (dump_file)
    {
      fprintf (dump_file, "Reordered sequence:\n");
      for (bb = ENTRY_BLOCK_PTR->next_bb, index = NUM_FIXED_BLOCKS;
	   bb;
	   bb = bb->aux, index++)
	{
	  fprintf (dump_file, " %i ", index);
	  if (get_bb_original (bb))
	    fprintf (dump_file, "duplicate of %i ",
		     get_bb_original (bb)->index);
	  else if (forwarder_block_p (bb)
		   && !LABEL_P (BB_HEAD (bb)))
	    fprintf (dump_file, "compensation ");
	  else
	    fprintf (dump_file, "bb %i ", bb->index);
	  fprintf (dump_file, " [%i]\n", bb->frequency);
	}
    }

  prev_bb = ENTRY_BLOCK_PTR;
  bb = ENTRY_BLOCK_PTR->next_bb;
  index = NUM_FIXED_BLOCKS;

  for (; bb; prev_bb = bb, bb = bb->aux, index ++)
    {
      bb->index = index;
      SET_BASIC_BLOCK (index, bb);

      bb->prev_bb = prev_bb;
      prev_bb->next_bb = bb;
    }
  prev_bb->next_bb = EXIT_BLOCK_PTR;
  EXIT_BLOCK_PTR->prev_bb = prev_bb;

  /* Annoying special case - jump around dead jumptables left in the code.  */
  FOR_EACH_BB (bb)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->flags & EDGE_FALLTHRU)
	  break;

      if (e && !can_fallthru (e->src, e->dest))
	force_nonfallthru (e);
    }
}

/* Perform sanity checks on the insn chain.
   1. Check that next/prev pointers are consistent in both the forward and
      reverse direction.
   2. Count insns in chain, going both directions, and check if equal.
   3. Check that get_last_insn () returns the actual end of chain.  */

void
verify_insn_chain (void)
{
  rtx x, prevx, nextx;
  int insn_cnt1, insn_cnt2;

  for (prevx = NULL, insn_cnt1 = 1, x = get_insns ();
       x != 0;
       prevx = x, insn_cnt1++, x = NEXT_INSN (x))
    gcc_assert (PREV_INSN (x) == prevx);

  gcc_assert (prevx == get_last_insn ());

  for (nextx = NULL, insn_cnt2 = 1, x = get_last_insn ();
       x != 0;
       nextx = x, insn_cnt2++, x = PREV_INSN (x))
    gcc_assert (NEXT_INSN (x) == nextx);

  gcc_assert (insn_cnt1 == insn_cnt2);
}

/* If we have assembler epilogues, the block falling through to exit must
   be the last one in the reordered chain when we reach final.  Ensure
   that this condition is met.  */
static void
fixup_fallthru_exit_predecessor (void)
{
  edge e;
  edge_iterator ei;
  basic_block bb = NULL;

  /* This transformation is not valid before reload, because we might
     separate a call from the instruction that copies the return
     value.  */
  gcc_assert (reload_completed);

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    if (e->flags & EDGE_FALLTHRU)
      bb = e->src;

  if (bb && bb->aux)
    {
      basic_block c = ENTRY_BLOCK_PTR->next_bb;

      /* If the very first block is the one with the fall-through exit
	 edge, we have to split that block.  */
      if (c == bb)
	{
	  bb = split_block (bb, NULL)->dest;
	  bb->aux = c->aux;
	  c->aux = bb;
	  bb->il.rtl->footer = c->il.rtl->footer;
	  c->il.rtl->footer = NULL;
	}

      while (c->aux != bb)
	c = c->aux;

      c->aux = bb->aux;
      while (c->aux)
	c = c->aux;

      c->aux = bb;
      bb->aux = NULL;
    }
}

/* Return true in case it is possible to duplicate the basic block BB.  */

/* We do not want to declare the function in a header file, since it should
   only be used through the cfghooks interface, and we do not want to move
   it to cfgrtl.c since it would require also moving quite a lot of related
   code.  */
extern bool cfg_layout_can_duplicate_bb_p (basic_block);

bool
cfg_layout_can_duplicate_bb_p (basic_block bb)
{
  /* Do not attempt to duplicate tablejumps, as we need to unshare
     the dispatch table.  This is difficult to do, as the instructions
     computing jump destination may be hoisted outside the basic block.  */
  if (tablejump_p (BB_END (bb), NULL, NULL))
    return false;

  /* Do not duplicate blocks containing insns that can't be copied.  */
  if (targetm.cannot_copy_insn_p)
    {
      rtx insn = BB_HEAD (bb);
      while (1)
	{
	  if (INSN_P (insn) && targetm.cannot_copy_insn_p (insn))
	    return false;
	  if (insn == BB_END (bb))
	    break;
	  insn = NEXT_INSN (insn);
	}
    }

  return true;
}

rtx
duplicate_insn_chain (rtx from, rtx to)
{
  rtx insn, last;

  /* Avoid updating of boundaries of previous basic block.  The
     note will get removed from insn stream in fixup.  */
  last = emit_note (NOTE_INSN_DELETED);

  /* Create copy at the end of INSN chain.  The chain will
     be reordered later.  */
  for (insn = from; insn != NEXT_INSN (to); insn = NEXT_INSN (insn))
    {
      switch (GET_CODE (insn))
	{
	case INSN:
	case CALL_INSN:
	case JUMP_INSN:
	  /* Avoid copying of dispatch tables.  We never duplicate
	     tablejumps, so this can hit only in case the table got
	     moved far from original jump.  */
	  if (GET_CODE (PATTERN (insn)) == ADDR_VEC
	      || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC)
	    break;
	  emit_copy_of_insn_after (insn, get_last_insn ());
	  break;

	case CODE_LABEL:
	  break;

	case BARRIER:
	  emit_barrier ();
	  break;

	case NOTE:
	  switch (NOTE_LINE_NUMBER (insn))
	    {
	      /* In case prologue is empty and function contain label
		 in first BB, we may want to copy the block.  */
	    case NOTE_INSN_PROLOGUE_END:

	    case NOTE_INSN_DELETED:
	    case NOTE_INSN_DELETED_LABEL:
	      /* No problem to strip these.  */
	    case NOTE_INSN_EPILOGUE_BEG:
	    case NOTE_INSN_FUNCTION_END:
	      /* Debug code expect these notes to exist just once.
		 Keep them in the master copy.
		 ??? It probably makes more sense to duplicate them for each
		 epilogue copy.  */
	    case NOTE_INSN_FUNCTION_BEG:
	      /* There is always just single entry to function.  */
	    case NOTE_INSN_BASIC_BLOCK:
	      break;

	    case NOTE_INSN_REPEATED_LINE_NUMBER:
	    case NOTE_INSN_SWITCH_TEXT_SECTIONS:
	      emit_note_copy (insn);
	      break;

	    default:
	      /* All other notes should have already been eliminated.
	       */
	      gcc_assert (NOTE_LINE_NUMBER (insn) >= 0);

	      /* It is possible that no_line_number is set and the note
		 won't be emitted.  */
	      emit_note_copy (insn);
	    }
	  break;
	default:
	  gcc_unreachable ();
	}
    }
  insn = NEXT_INSN (last);
  delete_insn (last);
  return insn;
}
/* Create a duplicate of the basic block BB.  */

/* We do not want to declare the function in a header file, since it should
   only be used through the cfghooks interface, and we do not want to move
   it to cfgrtl.c since it would require also moving quite a lot of related
   code.  */
extern basic_block cfg_layout_duplicate_bb (basic_block);

basic_block
cfg_layout_duplicate_bb (basic_block bb)
{
  rtx insn;
  basic_block new_bb;

  insn = duplicate_insn_chain (BB_HEAD (bb), BB_END (bb));
  new_bb = create_basic_block (insn,
			       insn ? get_last_insn () : NULL,
			       EXIT_BLOCK_PTR->prev_bb);

  BB_COPY_PARTITION (new_bb, bb);
  if (bb->il.rtl->header)
    {
      insn = bb->il.rtl->header;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
      insn = duplicate_insn_chain (bb->il.rtl->header, insn);
      if (insn)
	new_bb->il.rtl->header = unlink_insn_chain (insn, get_last_insn ());
    }

  if (bb->il.rtl->footer)
    {
      insn = bb->il.rtl->footer;
      while (NEXT_INSN (insn))
	insn = NEXT_INSN (insn);
      insn = duplicate_insn_chain (bb->il.rtl->footer, insn);
      if (insn)
	new_bb->il.rtl->footer = unlink_insn_chain (insn, get_last_insn ());
    }

  if (bb->il.rtl->global_live_at_start)
    {
      new_bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
      new_bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
      COPY_REG_SET (new_bb->il.rtl->global_live_at_start,
		    bb->il.rtl->global_live_at_start);
      COPY_REG_SET (new_bb->il.rtl->global_live_at_end,
		    bb->il.rtl->global_live_at_end);
    }

  return new_bb;
}

/* Main entry point to this module - initialize the datastructures for
   CFG layout changes.  It keeps LOOPS up-to-date if not null.

   FLAGS is a set of additional flags to pass to cleanup_cfg().  It should
   include CLEANUP_UPDATE_LIFE if liveness information must be kept up
   to date.  */

void
cfg_layout_initialize (unsigned int flags)
{
  initialize_original_copy_tables ();

  cfg_layout_rtl_register_cfg_hooks ();

  record_effective_endpoints ();

  cleanup_cfg (CLEANUP_CFGLAYOUT | flags);
}

/* Splits superblocks.  */
void
break_superblocks (void)
{
  sbitmap superblocks;
  bool need = false;
  basic_block bb;

  superblocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (superblocks);

  FOR_EACH_BB (bb)
    if (bb->flags & BB_SUPERBLOCK)
      {
	bb->flags &= ~BB_SUPERBLOCK;
	SET_BIT (superblocks, bb->index);
	need = true;
      }

  if (need)
    {
      rebuild_jump_labels (get_insns ());
      find_many_sub_basic_blocks (superblocks);
    }

  free (superblocks);
}

/* Finalize the changes: reorder insn list according to the sequence specified
   by aux pointers, enter compensation code, rebuild scope forest.  */

void
cfg_layout_finalize (void)
{
  basic_block bb;

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif
  rtl_register_cfg_hooks ();
  if (reload_completed
#ifdef HAVE_epilogue
      && !HAVE_epilogue
#endif
      )
    fixup_fallthru_exit_predecessor ();
  fixup_reorder_chain ();

#ifdef ENABLE_CHECKING
  verify_insn_chain ();
#endif
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
  {
    bb->il.rtl->header = bb->il.rtl->footer = NULL;
    bb->aux = NULL;
    bb->il.rtl->visited = 0;
  }

  break_superblocks ();

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif

  free_original_copy_tables ();
}

/* Checks whether all N blocks in BBS array can be copied.  */
bool
can_copy_bbs_p (basic_block *bbs, unsigned n)
{
  unsigned i;
  edge e;
  int ret = true;

  for (i = 0; i < n; i++)
    bbs[i]->flags |= BB_DUPLICATED;

  for (i = 0; i < n; i++)
    {
      /* In case we should redirect abnormal edge during duplication, fail.  */
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bbs[i]->succs)
	if ((e->flags & EDGE_ABNORMAL)
	    && (e->dest->flags & BB_DUPLICATED))
	  {
	    ret = false;
	    goto end;
	  }

      if (!can_duplicate_block_p (bbs[i]))
	{
	  ret = false;
	  break;
	}
    }

end:
  for (i = 0; i < n; i++)
    bbs[i]->flags &= ~BB_DUPLICATED;

  return ret;
}

/* Duplicates N basic blocks stored in array BBS.  Newly created basic blocks
   are placed into array NEW_BBS in the same order.  Edges from basic blocks
   in BBS are also duplicated and copies of those of them
   that lead into BBS are redirected to appropriate newly created block.  The
   function assigns bbs into loops (copy of basic block bb is assigned to
   bb->loop_father->copy loop, so this must be set up correctly in advance)
   and updates dominators locally (LOOPS structure that contains the information
   about dominators is passed to enable this).

   BASE is the superloop to that basic block belongs; if its header or latch
   is copied, we do not set the new blocks as header or latch.

   Created copies of N_EDGES edges in array EDGES are stored in array NEW_EDGES,
   also in the same order.

   Newly created basic blocks are put after the basic block AFTER in the
   instruction stream, and the order of the blocks in BBS array is preserved.  */

void
copy_bbs (basic_block *bbs, unsigned n, basic_block *new_bbs,
	  edge *edges, unsigned num_edges, edge *new_edges,
	  struct loop *base, basic_block after)
{
  unsigned i, j;
  basic_block bb, new_bb, dom_bb;
  edge e;

  /* Duplicate bbs, update dominators, assign bbs to loops.  */
  for (i = 0; i < n; i++)
    {
      /* Duplicate.  */
      bb = bbs[i];
      new_bb = new_bbs[i] = duplicate_block (bb, NULL, after);
      after = new_bb;
      bb->flags |= BB_DUPLICATED;
      /* Add to loop.  */
      add_bb_to_loop (new_bb, bb->loop_father->copy);
      /* Possibly set header.  */
      if (bb->loop_father->header == bb && bb->loop_father != base)
	new_bb->loop_father->header = new_bb;
      /* Or latch.  */
      if (bb->loop_father->latch == bb && bb->loop_father != base)
	new_bb->loop_father->latch = new_bb;
    }

  /* Set dominators.  */
  for (i = 0; i < n; i++)
    {
      bb = bbs[i];
      new_bb = new_bbs[i];

      dom_bb = get_immediate_dominator (CDI_DOMINATORS, bb);
      if (dom_bb->flags & BB_DUPLICATED)
	{
	  dom_bb = get_bb_copy (dom_bb);
	  set_immediate_dominator (CDI_DOMINATORS, new_bb, dom_bb);
	}
    }

  /* Redirect edges.  */
  for (j = 0; j < num_edges; j++)
    new_edges[j] = NULL;
  for (i = 0; i < n; i++)
    {
      edge_iterator ei;
      new_bb = new_bbs[i];
      bb = bbs[i];

      FOR_EACH_EDGE (e, ei, new_bb->succs)
	{
	  for (j = 0; j < num_edges; j++)
	    if (edges[j] && edges[j]->src == bb && edges[j]->dest == e->dest)
	      new_edges[j] = e;

	  if (!(e->dest->flags & BB_DUPLICATED))
	    continue;
	  redirect_edge_and_branch_force (e, get_bb_copy (e->dest));
	}
    }

  /* Clear information about duplicates.  */
  for (i = 0; i < n; i++)
    bbs[i]->flags &= ~BB_DUPLICATED;
}

#include "gt-cfglayout.h"
