/* Perform branch target register load optimizations.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
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
#include "rtl.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "fibheap.h"
#include "output.h"
#include "target.h"
#include "expr.h"
#include "flags.h"
#include "insn-attr.h"
#include "function.h"
#include "except.h"
#include "tm_p.h"
#include "toplev.h"
#include "tree-pass.h"

/* Target register optimizations - these are performed after reload.  */

typedef struct btr_def_group_s
{
  struct btr_def_group_s *next;
  rtx src;
  struct btr_def_s *members;
} *btr_def_group;

typedef struct btr_user_s
{
  struct btr_user_s *next;
  basic_block bb;
  int luid;
  rtx insn;
  /* If INSN has a single use of a single branch register, then
     USE points to it within INSN.  If there is more than
     one branch register use, or the use is in some way ambiguous,
     then USE is NULL.  */
  rtx use;
  int n_reaching_defs;
  int first_reaching_def;
  char other_use_this_block;
} *btr_user;

/* btr_def structs appear on three lists:
     1. A list of all btr_def structures (head is
	ALL_BTR_DEFS, linked by the NEXT field).
     2. A list of branch reg definitions per basic block (head is
	BB_BTR_DEFS[i], linked by the NEXT_THIS_BB field).
     3. A list of all branch reg definitions belonging to the same
	group (head is in a BTR_DEF_GROUP struct, linked by
	NEXT_THIS_GROUP field).  */

typedef struct btr_def_s
{
  struct btr_def_s *next_this_bb;
  struct btr_def_s *next_this_group;
  basic_block bb;
  int luid;
  rtx insn;
  int btr;
  int cost;
  /* For a branch register setting insn that has a constant
     source (i.e. a label), group links together all the
     insns with the same source.  For other branch register
     setting insns, group is NULL.  */
  btr_def_group group;
  btr_user uses;
  /* If this def has a reaching use which is not a simple use
     in a branch instruction, then has_ambiguous_use will be true,
     and we will not attempt to migrate this definition.  */
  char has_ambiguous_use;
  /* live_range is an approximation to the true live range for this
     def/use web, because it records the set of blocks that contain
     the live range.  There could be other live ranges for the same
     branch register in that set of blocks, either in the block
     containing the def (before the def), or in a block containing
     a use (after the use).  If there are such other live ranges, then
     other_btr_uses_before_def or other_btr_uses_after_use must be set true
     as appropriate.  */
  char other_btr_uses_before_def;
  char other_btr_uses_after_use;
  /* We set own_end when we have moved a definition into a dominator.
     Thus, when a later combination removes this definition again, we know
     to clear out trs_live_at_end again.  */
  char own_end;
  bitmap live_range;
} *btr_def;

static int issue_rate;

static int basic_block_freq (basic_block);
static int insn_sets_btr_p (rtx, int, int *);
static rtx *find_btr_use (rtx);
static int btr_referenced_p (rtx, rtx *);
static int find_btr_reference (rtx *, void *);
static void find_btr_def_group (btr_def_group *, btr_def);
static btr_def add_btr_def (fibheap_t, basic_block, int, rtx,
			    unsigned int, int, btr_def_group *);
static btr_user new_btr_user (basic_block, int, rtx);
static void dump_hard_reg_set (HARD_REG_SET);
static void dump_btrs_live (int);
static void note_other_use_this_block (unsigned int, btr_user);
static void compute_defs_uses_and_gen (fibheap_t, btr_def *,btr_user *,
				       sbitmap *, sbitmap *, HARD_REG_SET *);
static void compute_kill (sbitmap *, sbitmap *, HARD_REG_SET *);
static void compute_out (sbitmap *bb_out, sbitmap *, sbitmap *, int);
static void link_btr_uses (btr_def *, btr_user *, sbitmap *, sbitmap *, int);
static void build_btr_def_use_webs (fibheap_t);
static int block_at_edge_of_live_range_p (int, btr_def);
static void clear_btr_from_live_range (btr_def def);
static void add_btr_to_live_range (btr_def, int);
static void augment_live_range (bitmap, HARD_REG_SET *, basic_block,
				basic_block, int);
static int choose_btr (HARD_REG_SET);
static void combine_btr_defs (btr_def, HARD_REG_SET *);
static void btr_def_live_range (btr_def, HARD_REG_SET *);
static void move_btr_def (basic_block, int, btr_def, bitmap, HARD_REG_SET *);
static int migrate_btr_def (btr_def, int);
static void migrate_btr_defs (enum reg_class, int);
static int can_move_up (basic_block, rtx, int);
static void note_btr_set (rtx, rtx, void *);

/* The following code performs code motion of target load instructions
   (instructions that set branch target registers), to move them
   forward away from the branch instructions and out of loops (or,
   more generally, from a more frequently executed place to a less
   frequently executed place).
   Moving target load instructions further in front of the branch
   instruction that uses the target register value means that the hardware
   has a better chance of preloading the instructions at the branch
   target by the time the branch is reached.  This avoids bubbles
   when a taken branch needs to flush out the pipeline.
   Moving target load instructions out of loops means they are executed
   less frequently.  */

/* An obstack to hold the def-use web data structures built up for
   migrating branch target load instructions.  */
static struct obstack migrate_btrl_obstack;

/* Array indexed by basic block number, giving the set of registers
   live in that block.  */
static HARD_REG_SET *btrs_live;

/* Array indexed by basic block number, giving the set of registers live at
  the end of that block, including any uses by a final jump insn, if any.  */
static HARD_REG_SET *btrs_live_at_end;

/* Set of all target registers that we are willing to allocate.  */
static HARD_REG_SET all_btrs;

/* Provide lower and upper bounds for target register numbers, so that
   we don't need to search through all the hard registers all the time.  */
static int first_btr, last_btr;



/* Return an estimate of the frequency of execution of block bb.  */
static int
basic_block_freq (basic_block bb)
{
  return bb->frequency;
}

static rtx *btr_reference_found;

/* A subroutine of btr_referenced_p, called through for_each_rtx.
   PREG is a pointer to an rtx that is to be excluded from the
   traversal.  If we find a reference to a target register anywhere
   else, return 1, and put a pointer to it into btr_reference_found.  */
static int
find_btr_reference (rtx *px, void *preg)
{
  rtx x;
  int regno, i;

  if (px == preg)
    return -1;
  x = *px;
  if (!REG_P (x))
    return 0;
  regno = REGNO (x);
  for (i = hard_regno_nregs[regno][GET_MODE (x)] - 1; i >= 0; i--)
    if (TEST_HARD_REG_BIT (all_btrs, regno+i))
      {
	btr_reference_found = px;
	return 1;
      }
  return -1;
}

/* Return nonzero if X references (sets or reads) any branch target register.
   If EXCLUDEP is set, disregard any references within the rtx pointed to
   by it.  If returning nonzero, also set btr_reference_found as above.  */
static int
btr_referenced_p (rtx x, rtx *excludep)
{
  return for_each_rtx (&x, find_btr_reference, excludep);
}

/* Return true if insn is an instruction that sets a target register.
   if CHECK_CONST is true, only return true if the source is constant.
   If such a set is found and REGNO is nonzero, assign the register number
   of the destination register to *REGNO.  */
static int
insn_sets_btr_p (rtx insn, int check_const, int *regno)
{
  rtx set;

  if (NONJUMP_INSN_P (insn)
      && (set = single_set (insn)))
    {
      rtx dest = SET_DEST (set);
      rtx src = SET_SRC (set);

      if (GET_CODE (dest) == SUBREG)
	dest = XEXP (dest, 0);

      if (REG_P (dest)
	  && TEST_HARD_REG_BIT (all_btrs, REGNO (dest)))
	{
	  gcc_assert (!btr_referenced_p (src, NULL));

	  if (!check_const || CONSTANT_P (src))
	    {
	      if (regno)
		*regno = REGNO (dest);
	      return 1;
	    }
	}
    }
  return 0;
}

/* Find and return a use of a target register within an instruction INSN.  */
static rtx *
find_btr_use (rtx insn)
{
  return btr_referenced_p (insn, NULL) ? btr_reference_found : NULL;
}

/* Find the group that the target register definition DEF belongs
   to in the list starting with *ALL_BTR_DEF_GROUPS.  If no such
   group exists, create one.  Add def to the group.  */
static void
find_btr_def_group (btr_def_group *all_btr_def_groups, btr_def def)
{
  if (insn_sets_btr_p (def->insn, 1, NULL))
    {
      btr_def_group this_group;
      rtx def_src = SET_SRC (single_set (def->insn));

      /* ?? This linear search is an efficiency concern, particularly
	 as the search will almost always fail to find a match.  */
      for (this_group = *all_btr_def_groups;
	   this_group != NULL;
	   this_group = this_group->next)
	if (rtx_equal_p (def_src, this_group->src))
	  break;

      if (!this_group)
	{
	  this_group = obstack_alloc (&migrate_btrl_obstack,
				      sizeof (struct btr_def_group_s));
	  this_group->src = def_src;
	  this_group->members = NULL;
	  this_group->next = *all_btr_def_groups;
	  *all_btr_def_groups = this_group;
	}
      def->group = this_group;
      def->next_this_group = this_group->members;
      this_group->members = def;
    }
  else
    def->group = NULL;
}

/* Create a new target register definition structure, for a definition in
   block BB, instruction INSN, and insert it into ALL_BTR_DEFS.  Return
   the new definition.  */
static btr_def
add_btr_def (fibheap_t all_btr_defs, basic_block bb, int insn_luid, rtx insn,
	     unsigned int dest_reg, int other_btr_uses_before_def,
	     btr_def_group *all_btr_def_groups)
{
  btr_def this
    = obstack_alloc (&migrate_btrl_obstack, sizeof (struct btr_def_s));
  this->bb = bb;
  this->luid = insn_luid;
  this->insn = insn;
  this->btr = dest_reg;
  this->cost = basic_block_freq (bb);
  this->has_ambiguous_use = 0;
  this->other_btr_uses_before_def = other_btr_uses_before_def;
  this->other_btr_uses_after_use = 0;
  this->next_this_bb = NULL;
  this->next_this_group = NULL;
  this->uses = NULL;
  this->live_range = NULL;
  find_btr_def_group (all_btr_def_groups, this);

  fibheap_insert (all_btr_defs, -this->cost, this);

  if (dump_file)
    fprintf (dump_file,
      "Found target reg definition: sets %u { bb %d, insn %d }%s priority %d\n",
      dest_reg, bb->index, INSN_UID (insn), (this->group ? "" : ":not const"),
      this->cost);

  return this;
}

/* Create a new target register user structure, for a use in block BB,
   instruction INSN.  Return the new user.  */
static btr_user
new_btr_user (basic_block bb, int insn_luid, rtx insn)
{
  /* This instruction reads target registers.  We need
     to decide whether we can replace all target register
     uses easily.
   */
  rtx *usep = find_btr_use (PATTERN (insn));
  rtx use;
  btr_user user = NULL;

  if (usep)
    {
      int unambiguous_single_use;

      /* We want to ensure that USE is the only use of a target
	 register in INSN, so that we know that to rewrite INSN to use
	 a different target register, all we have to do is replace USE.  */
      unambiguous_single_use = !btr_referenced_p (PATTERN (insn), usep);
      if (!unambiguous_single_use)
	usep = NULL;
    }
  use = usep ? *usep : NULL_RTX;
  user = obstack_alloc (&migrate_btrl_obstack, sizeof (struct btr_user_s));
  user->bb = bb;
  user->luid = insn_luid;
  user->insn = insn;
  user->use = use;
  user->other_use_this_block = 0;
  user->next = NULL;
  user->n_reaching_defs = 0;
  user->first_reaching_def = -1;

  if (dump_file)
    {
      fprintf (dump_file, "Uses target reg: { bb %d, insn %d }",
	       bb->index, INSN_UID (insn));

      if (user->use)
	fprintf (dump_file, ": unambiguous use of reg %d\n",
		 REGNO (user->use));
    }

  return user;
}

/* Write the contents of S to the dump file.  */
static void
dump_hard_reg_set (HARD_REG_SET s)
{
  int reg;
  for (reg = 0; reg < FIRST_PSEUDO_REGISTER; reg++)
    if (TEST_HARD_REG_BIT (s, reg))
      fprintf (dump_file, " %d", reg);
}

/* Write the set of target regs live in block BB to the dump file.  */
static void
dump_btrs_live (int bb)
{
  fprintf (dump_file, "BB%d live:", bb);
  dump_hard_reg_set (btrs_live[bb]);
  fprintf (dump_file, "\n");
}

/* REGNO is the number of a branch target register that is being used or
   set.  USERS_THIS_BB is a list of preceding branch target register users;
   If any of them use the same register, set their other_use_this_block
   flag.  */
static void
note_other_use_this_block (unsigned int regno, btr_user users_this_bb)
{
  btr_user user;

  for (user = users_this_bb; user != NULL; user = user->next)
    if (user->use && REGNO (user->use) == regno)
      user->other_use_this_block = 1;
}

typedef struct {
  btr_user users_this_bb;
  HARD_REG_SET btrs_written_in_block;
  HARD_REG_SET btrs_live_in_block;
  sbitmap bb_gen;
  sbitmap *btr_defset;
} defs_uses_info;

/* Called via note_stores or directly to register stores into /
   clobbers of a branch target register DEST that are not recognized as
   straightforward definitions.  DATA points to information about the
   current basic block that needs updating.  */
static void
note_btr_set (rtx dest, rtx set ATTRIBUTE_UNUSED, void *data)
{
  defs_uses_info *info = data;
  int regno, end_regno;

  if (!REG_P (dest))
    return;
  regno = REGNO (dest);
  end_regno = regno + hard_regno_nregs[regno][GET_MODE (dest)];
  for (; regno < end_regno; regno++)
    if (TEST_HARD_REG_BIT (all_btrs, regno))
      {
	note_other_use_this_block (regno, info->users_this_bb);
	SET_HARD_REG_BIT (info->btrs_written_in_block, regno);
	SET_HARD_REG_BIT (info->btrs_live_in_block, regno);
	sbitmap_difference (info->bb_gen, info->bb_gen,
			    info->btr_defset[regno - first_btr]);
      }
}

static void
compute_defs_uses_and_gen (fibheap_t all_btr_defs, btr_def *def_array,
			   btr_user *use_array, sbitmap *btr_defset,
			   sbitmap *bb_gen, HARD_REG_SET *btrs_written)
{
  /* Scan the code building up the set of all defs and all uses.
     For each target register, build the set of defs of that register.
     For each block, calculate the set of target registers
     written in that block.
     Also calculate the set of btrs ever live in that block.
  */
  int i;
  int insn_luid = 0;
  btr_def_group all_btr_def_groups = NULL;
  defs_uses_info info;

  sbitmap_vector_zero (bb_gen, n_basic_blocks);
  for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
    {
      basic_block bb = BASIC_BLOCK (i);
      int reg;
      btr_def defs_this_bb = NULL;
      rtx insn;
      rtx last;
      int can_throw = 0;

      info.users_this_bb = NULL;
      info.bb_gen = bb_gen[i];
      info.btr_defset = btr_defset;

      CLEAR_HARD_REG_SET (info.btrs_live_in_block);
      CLEAR_HARD_REG_SET (info.btrs_written_in_block);
      for (reg = first_btr; reg <= last_btr; reg++)
	if (TEST_HARD_REG_BIT (all_btrs, reg)
	    && REGNO_REG_SET_P (bb->il.rtl->global_live_at_start, reg))
	  SET_HARD_REG_BIT (info.btrs_live_in_block, reg);

      for (insn = BB_HEAD (bb), last = NEXT_INSN (BB_END (bb));
	   insn != last;
	   insn = NEXT_INSN (insn), insn_luid++)
	{
	  if (INSN_P (insn))
	    {
	      int regno;
	      int insn_uid = INSN_UID (insn);

	      if (insn_sets_btr_p (insn, 0, &regno))
		{
		  btr_def def = add_btr_def (
		      all_btr_defs, bb, insn_luid, insn, regno,
		      TEST_HARD_REG_BIT (info.btrs_live_in_block, regno),
		      &all_btr_def_groups);

		  def_array[insn_uid] = def;
		  SET_HARD_REG_BIT (info.btrs_written_in_block, regno);
		  SET_HARD_REG_BIT (info.btrs_live_in_block, regno);
		  sbitmap_difference (bb_gen[i], bb_gen[i],
				      btr_defset[regno - first_btr]);
		  SET_BIT (bb_gen[i], insn_uid);
		  def->next_this_bb = defs_this_bb;
		  defs_this_bb = def;
		  SET_BIT (btr_defset[regno - first_btr], insn_uid);
		  note_other_use_this_block (regno, info.users_this_bb);
		}
	      /* Check for the blockage emitted by expand_nl_goto_receiver.  */
	      else if (current_function_has_nonlocal_label
		       && GET_CODE (PATTERN (insn)) == ASM_INPUT)
		{
		  btr_user user;

		  /* Do the equivalent of calling note_other_use_this_block
		     for every target register.  */
		  for (user = info.users_this_bb; user != NULL;
		       user = user->next)
		    if (user->use)
		      user->other_use_this_block = 1;
		  IOR_HARD_REG_SET (info.btrs_written_in_block, all_btrs);
		  IOR_HARD_REG_SET (info.btrs_live_in_block, all_btrs);
		  sbitmap_zero (info.bb_gen);
		}
	      else
		{
		  if (btr_referenced_p (PATTERN (insn), NULL))
		    {
		      btr_user user = new_btr_user (bb, insn_luid, insn);

		      use_array[insn_uid] = user;
		      if (user->use)
			SET_HARD_REG_BIT (info.btrs_live_in_block,
					  REGNO (user->use));
		      else
			{
			  int reg;
			  for (reg = first_btr; reg <= last_btr; reg++)
			    if (TEST_HARD_REG_BIT (all_btrs, reg)
				&& refers_to_regno_p (reg, reg + 1, user->insn,
						      NULL))
			      {
				note_other_use_this_block (reg,
							   info.users_this_bb);
				SET_HARD_REG_BIT (info.btrs_live_in_block, reg);
			      }
			  note_stores (PATTERN (insn), note_btr_set, &info);
			}
		      user->next = info.users_this_bb;
		      info.users_this_bb = user;
		    }
		  if (CALL_P (insn))
		    {
		      HARD_REG_SET *clobbered = &call_used_reg_set;
		      HARD_REG_SET call_saved;
		      rtx pat = PATTERN (insn);
		      int i;

		      /* Check for sibcall.  */
		      if (GET_CODE (pat) == PARALLEL)
			for (i = XVECLEN (pat, 0) - 1; i >= 0; i--)
			  if (GET_CODE (XVECEXP (pat, 0, i)) == RETURN)
			    {
			      COMPL_HARD_REG_SET (call_saved,
						  call_used_reg_set);
			      clobbered = &call_saved;
			    }

		      for (regno = first_btr; regno <= last_btr; regno++)
			if (TEST_HARD_REG_BIT (*clobbered, regno))
			  note_btr_set (regno_reg_rtx[regno], NULL_RTX, &info);
		    }
		}
	    }
	}

      COPY_HARD_REG_SET (btrs_live[i], info.btrs_live_in_block);
      COPY_HARD_REG_SET (btrs_written[i], info.btrs_written_in_block);

      REG_SET_TO_HARD_REG_SET (btrs_live_at_end[i], bb->il.rtl->global_live_at_end);
      /* If this block ends in a jump insn, add any uses or even clobbers
	 of branch target registers that it might have.  */
      for (insn = BB_END (bb); insn != BB_HEAD (bb) && ! INSN_P (insn); )
	insn = PREV_INSN (insn);
      /* ??? for the fall-through edge, it would make sense to insert the
	 btr set on the edge, but that would require to split the block
	 early on so that we can distinguish between dominance from the fall
	 through edge - which can use the call-clobbered registers - from
	 dominance by the throw edge.  */
      if (can_throw_internal (insn))
	{
	  HARD_REG_SET tmp;

	  COPY_HARD_REG_SET (tmp, call_used_reg_set);
	  AND_HARD_REG_SET (tmp, all_btrs);
	  IOR_HARD_REG_SET (btrs_live_at_end[i], tmp);
	  can_throw = 1;
	}
      if (can_throw || JUMP_P (insn))
	{
	  int regno;

	  for (regno = first_btr; regno <= last_btr; regno++)
	    if (refers_to_regno_p (regno, regno+1, insn, NULL))
	      SET_HARD_REG_BIT (btrs_live_at_end[i], regno);
	}

      if (dump_file)
	dump_btrs_live(i);
    }
}

static void
compute_kill (sbitmap *bb_kill, sbitmap *btr_defset,
	      HARD_REG_SET *btrs_written)
{
  int i;
  int regno;

  /* For each basic block, form the set BB_KILL - the set
     of definitions that the block kills.  */
  sbitmap_vector_zero (bb_kill, n_basic_blocks);
  for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
    {
      for (regno = first_btr; regno <= last_btr; regno++)
	if (TEST_HARD_REG_BIT (all_btrs, regno)
	    && TEST_HARD_REG_BIT (btrs_written[i], regno))
	  sbitmap_a_or_b (bb_kill[i], bb_kill[i],
			  btr_defset[regno - first_btr]);
    }
}

static void
compute_out (sbitmap *bb_out, sbitmap *bb_gen, sbitmap *bb_kill, int max_uid)
{
  /* Perform iterative dataflow:
      Initially, for all blocks, BB_OUT = BB_GEN.
      For each block,
	BB_IN  = union over predecessors of BB_OUT(pred)
	BB_OUT = (BB_IN - BB_KILL) + BB_GEN
     Iterate until the bb_out sets stop growing.  */
  int i;
  int changed;
  sbitmap bb_in = sbitmap_alloc (max_uid);

  for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
    sbitmap_copy (bb_out[i], bb_gen[i]);

  changed = 1;
  while (changed)
    {
      changed = 0;
      for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
	{
	  sbitmap_union_of_preds (bb_in, bb_out, i);
	  changed |= sbitmap_union_of_diff_cg (bb_out[i], bb_gen[i],
					       bb_in, bb_kill[i]);
	}
    }
  sbitmap_free (bb_in);
}

static void
link_btr_uses (btr_def *def_array, btr_user *use_array, sbitmap *bb_out,
	       sbitmap *btr_defset, int max_uid)
{
  int i;
  sbitmap reaching_defs = sbitmap_alloc (max_uid);

  /* Link uses to the uses lists of all of their reaching defs.
     Count up the number of reaching defs of each use.  */
  for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
    {
      basic_block bb = BASIC_BLOCK (i);
      rtx insn;
      rtx last;

      sbitmap_union_of_preds (reaching_defs, bb_out, i);
      for (insn = BB_HEAD (bb), last = NEXT_INSN (BB_END (bb));
	   insn != last;
	   insn = NEXT_INSN (insn))
	{
	  if (INSN_P (insn))
	    {
	      int insn_uid = INSN_UID (insn);

	      btr_def def   = def_array[insn_uid];
	      btr_user user = use_array[insn_uid];
	      if (def != NULL)
		{
		  /* Remove all reaching defs of regno except
		     for this one.  */
		  sbitmap_difference (reaching_defs, reaching_defs,
				      btr_defset[def->btr - first_btr]);
		  SET_BIT(reaching_defs, insn_uid);
		}

	      if (user != NULL)
		{
		  /* Find all the reaching defs for this use.  */
		  sbitmap reaching_defs_of_reg = sbitmap_alloc(max_uid);
		  unsigned int uid = 0;
		  sbitmap_iterator sbi;

		  if (user->use)
		    sbitmap_a_and_b (
		      reaching_defs_of_reg,
		      reaching_defs,
		      btr_defset[REGNO (user->use) - first_btr]);
		  else
		    {
		      int reg;

		      sbitmap_zero (reaching_defs_of_reg);
		      for (reg = first_btr; reg <= last_btr; reg++)
			if (TEST_HARD_REG_BIT (all_btrs, reg)
			    && refers_to_regno_p (reg, reg + 1, user->insn,
						  NULL))
			  sbitmap_a_or_b_and_c (reaching_defs_of_reg,
			    reaching_defs_of_reg,
			    reaching_defs,
			    btr_defset[reg - first_btr]);
		    }
		  EXECUTE_IF_SET_IN_SBITMAP (reaching_defs_of_reg, 0, uid, sbi)
		    {
		      btr_def def = def_array[uid];

		      /* We now know that def reaches user.  */

		      if (dump_file)
			fprintf (dump_file,
			  "Def in insn %d reaches use in insn %d\n",
			  uid, insn_uid);

		      user->n_reaching_defs++;
		      if (!user->use)
			def->has_ambiguous_use = 1;
		      if (user->first_reaching_def != -1)
			{ /* There is more than one reaching def.  This is
			     a rare case, so just give up on this def/use
			     web when it occurs.  */
			  def->has_ambiguous_use = 1;
			  def_array[user->first_reaching_def]
			    ->has_ambiguous_use = 1;
			  if (dump_file)
			    fprintf (dump_file,
				     "(use %d has multiple reaching defs)\n",
				     insn_uid);
			}
		      else
			user->first_reaching_def = uid;
		      if (user->other_use_this_block)
			def->other_btr_uses_after_use = 1;
		      user->next = def->uses;
		      def->uses = user;
		    }
		  sbitmap_free (reaching_defs_of_reg);
		}

	      if (CALL_P (insn))
		{
		  int regno;

		  for (regno = first_btr; regno <= last_btr; regno++)
		    if (TEST_HARD_REG_BIT (all_btrs, regno)
			&& TEST_HARD_REG_BIT (call_used_reg_set, regno))
		      sbitmap_difference (reaching_defs, reaching_defs,
					  btr_defset[regno - first_btr]);
		}
	    }
	}
    }
  sbitmap_free (reaching_defs);
}

static void
build_btr_def_use_webs (fibheap_t all_btr_defs)
{
  const int max_uid = get_max_uid ();
  btr_def  *def_array   = XCNEWVEC (btr_def, max_uid);
  btr_user *use_array   = XCNEWVEC (btr_user, max_uid);
  sbitmap *btr_defset   = sbitmap_vector_alloc (
			   (last_btr - first_btr) + 1, max_uid);
  sbitmap *bb_gen      = sbitmap_vector_alloc (n_basic_blocks, max_uid);
  HARD_REG_SET *btrs_written = XCNEWVEC (HARD_REG_SET, n_basic_blocks);
  sbitmap *bb_kill;
  sbitmap *bb_out;

  sbitmap_vector_zero (btr_defset, (last_btr - first_btr) + 1);

  compute_defs_uses_and_gen (all_btr_defs, def_array, use_array, btr_defset,
			     bb_gen, btrs_written);

  bb_kill = sbitmap_vector_alloc (n_basic_blocks, max_uid);
  compute_kill (bb_kill, btr_defset, btrs_written);
  free (btrs_written);

  bb_out = sbitmap_vector_alloc (n_basic_blocks, max_uid);
  compute_out (bb_out, bb_gen, bb_kill, max_uid);

  sbitmap_vector_free (bb_gen);
  sbitmap_vector_free (bb_kill);

  link_btr_uses (def_array, use_array, bb_out, btr_defset, max_uid);

  sbitmap_vector_free (bb_out);
  sbitmap_vector_free (btr_defset);
  free (use_array);
  free (def_array);
}

/* Return true if basic block BB contains the start or end of the
   live range of the definition DEF, AND there are other live
   ranges of the same target register that include BB.  */
static int
block_at_edge_of_live_range_p (int bb, btr_def def)
{
  if (def->other_btr_uses_before_def && BASIC_BLOCK (bb) == def->bb)
    return 1;
  else if (def->other_btr_uses_after_use)
    {
      btr_user user;
      for (user = def->uses; user != NULL; user = user->next)
	if (BASIC_BLOCK (bb) == user->bb)
	  return 1;
    }
  return 0;
}

/* We are removing the def/use web DEF.  The target register
   used in this web is therefore no longer live in the live range
   of this web, so remove it from the live set of all basic blocks
   in the live range of the web.
   Blocks at the boundary of the live range may contain other live
   ranges for the same target register, so we have to be careful
   to remove the target register from the live set of these blocks
   only if they do not contain other live ranges for the same register.  */
static void
clear_btr_from_live_range (btr_def def)
{
  unsigned bb;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (def->live_range, 0, bb, bi)
    {
      if ((!def->other_btr_uses_before_def
	   && !def->other_btr_uses_after_use)
	  || !block_at_edge_of_live_range_p (bb, def))
	{
	  CLEAR_HARD_REG_BIT (btrs_live[bb], def->btr);
	  CLEAR_HARD_REG_BIT (btrs_live_at_end[bb], def->btr);
	  if (dump_file)
	    dump_btrs_live (bb);
	}
    }
 if (def->own_end)
   CLEAR_HARD_REG_BIT (btrs_live_at_end[def->bb->index], def->btr);
}


/* We are adding the def/use web DEF.  Add the target register used
   in this web to the live set of all of the basic blocks that contain
   the live range of the web.
   If OWN_END is set, also show that the register is live from our
   definitions at the end of the basic block where it is defined.  */
static void
add_btr_to_live_range (btr_def def, int own_end)
{
  unsigned bb;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (def->live_range, 0, bb, bi)
    {
      SET_HARD_REG_BIT (btrs_live[bb], def->btr);
      SET_HARD_REG_BIT (btrs_live_at_end[bb], def->btr);
      if (dump_file)
	dump_btrs_live (bb);
    }
  if (own_end)
    {
      SET_HARD_REG_BIT (btrs_live_at_end[def->bb->index], def->btr);
      def->own_end = 1;
    }
}

/* Update a live range to contain the basic block NEW_BLOCK, and all
   blocks on paths between the existing live range and NEW_BLOCK.
   HEAD is a block contained in the existing live range that dominates
   all other blocks in the existing live range.
   Also add to the set BTRS_LIVE_IN_RANGE all target registers that
   are live in the blocks that we add to the live range.
   If FULL_RANGE is set, include the full live range of NEW_BB;
   otherwise, if NEW_BB dominates HEAD_BB, only add registers that
   are life at the end of NEW_BB for NEW_BB itself.
   It is a precondition that either NEW_BLOCK dominates HEAD,or
   HEAD dom NEW_BLOCK.  This is used to speed up the
   implementation of this function.  */
static void
augment_live_range (bitmap live_range, HARD_REG_SET *btrs_live_in_range,
		    basic_block head_bb, basic_block new_bb, int full_range)
{
  basic_block *worklist, *tos;

  tos = worklist = XNEWVEC (basic_block, n_basic_blocks + 1);

  if (dominated_by_p (CDI_DOMINATORS, new_bb, head_bb))
    {
      if (new_bb == head_bb)
	{
	  if (full_range)
	    IOR_HARD_REG_SET (*btrs_live_in_range, btrs_live[new_bb->index]);
	  free (tos);
	  return;
	}
      *tos++ = new_bb;
    }
  else
    {
      edge e;
      edge_iterator ei;
      int new_block = new_bb->index;

      gcc_assert (dominated_by_p (CDI_DOMINATORS, head_bb, new_bb));

      IOR_HARD_REG_SET (*btrs_live_in_range, btrs_live[head_bb->index]);
      bitmap_set_bit (live_range, new_block);
      /* A previous btr migration could have caused a register to be
	live just at the end of new_block which we need in full, so
	use trs_live_at_end even if full_range is set.  */
      IOR_HARD_REG_SET (*btrs_live_in_range, btrs_live_at_end[new_block]);
      if (full_range)
	IOR_HARD_REG_SET (*btrs_live_in_range, btrs_live[new_block]);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "Adding end of block %d and rest of %d to live range\n",
		   new_block, head_bb->index);
	  fprintf (dump_file,"Now live btrs are ");
	  dump_hard_reg_set (*btrs_live_in_range);
	  fprintf (dump_file, "\n");
	}
      FOR_EACH_EDGE (e, ei, head_bb->preds)
	*tos++ = e->src;
    }

  while (tos != worklist)
    {
      basic_block bb = *--tos;
      if (!bitmap_bit_p (live_range, bb->index))
	{
	  edge e;
	  edge_iterator ei;

	  bitmap_set_bit (live_range, bb->index);
	  IOR_HARD_REG_SET (*btrs_live_in_range,
	    btrs_live[bb->index]);
	  /* A previous btr migration could have caused a register to be
	     live just at the end of a block which we need in full.  */
	  IOR_HARD_REG_SET (*btrs_live_in_range,
	    btrs_live_at_end[bb->index]);
	  if (dump_file)
	    {
	      fprintf (dump_file,
		"Adding block %d to live range\n", bb->index);
	      fprintf (dump_file,"Now live btrs are ");
	      dump_hard_reg_set (*btrs_live_in_range);
	      fprintf (dump_file, "\n");
	    }

	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      basic_block pred = e->src;
	      if (!bitmap_bit_p (live_range, pred->index))
		*tos++ = pred;
	    }
	}
    }

  free (worklist);
}

/*  Return the most desirable target register that is not in
    the set USED_BTRS.  */
static int
choose_btr (HARD_REG_SET used_btrs)
{
  int i;
  GO_IF_HARD_REG_SUBSET (all_btrs, used_btrs, give_up);

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
#ifdef REG_ALLOC_ORDER
      int regno = reg_alloc_order[i];
#else
      int regno = i;
#endif
      if (TEST_HARD_REG_BIT (all_btrs, regno)
	  && !TEST_HARD_REG_BIT (used_btrs, regno))
	return regno;
    }
give_up:
  return -1;
}

/* Calculate the set of basic blocks that contain the live range of
   the def/use web DEF.
   Also calculate the set of target registers that are live at time
   in this live range, but ignore the live range represented by DEF
   when calculating this set.  */
static void
btr_def_live_range (btr_def def, HARD_REG_SET *btrs_live_in_range)
{
  if (!def->live_range)
    {
      btr_user user;

      def->live_range = BITMAP_ALLOC (NULL);

      bitmap_set_bit (def->live_range, def->bb->index);
      COPY_HARD_REG_SET (*btrs_live_in_range,
			 (flag_btr_bb_exclusive
			  ? btrs_live : btrs_live_at_end)[def->bb->index]);

      for (user = def->uses; user != NULL; user = user->next)
	augment_live_range (def->live_range, btrs_live_in_range,
			    def->bb, user->bb,
			    (flag_btr_bb_exclusive
			     || user->insn != BB_END (def->bb)
			     || !JUMP_P (user->insn)));
    }
  else
    {
      /* def->live_range is accurate, but we need to recompute
	 the set of target registers live over it, because migration
	 of other PT instructions may have affected it.
      */
      unsigned bb;
      unsigned def_bb = flag_btr_bb_exclusive ? -1 : def->bb->index;
      bitmap_iterator bi;

      CLEAR_HARD_REG_SET (*btrs_live_in_range);
      EXECUTE_IF_SET_IN_BITMAP (def->live_range, 0, bb, bi)
	{
	  IOR_HARD_REG_SET (*btrs_live_in_range,
			    (def_bb == bb
			     ? btrs_live_at_end : btrs_live) [bb]);
	}
    }
  if (!def->other_btr_uses_before_def &&
      !def->other_btr_uses_after_use)
    CLEAR_HARD_REG_BIT (*btrs_live_in_range, def->btr);
}

/* Merge into the def/use web DEF any other def/use webs in the same
   group that are dominated by DEF, provided that there is a target
   register available to allocate to the merged web.  */
static void
combine_btr_defs (btr_def def, HARD_REG_SET *btrs_live_in_range)
{
  btr_def other_def;

  for (other_def = def->group->members;
       other_def != NULL;
       other_def = other_def->next_this_group)
    {
      if (other_def != def
	  && other_def->uses != NULL
	  && ! other_def->has_ambiguous_use
	  && dominated_by_p (CDI_DOMINATORS, other_def->bb, def->bb))
	{
	  /* def->bb dominates the other def, so def and other_def could
	     be combined.  */
	  /* Merge their live ranges, and get the set of
	     target registers live over the merged range.  */
	  int btr;
	  HARD_REG_SET combined_btrs_live;
	  bitmap combined_live_range = BITMAP_ALLOC (NULL);
	  btr_user user;

	  if (other_def->live_range == NULL)
	    {
	      HARD_REG_SET dummy_btrs_live_in_range;
	      btr_def_live_range (other_def, &dummy_btrs_live_in_range);
	    }
	  COPY_HARD_REG_SET (combined_btrs_live, *btrs_live_in_range);
	  bitmap_copy (combined_live_range, def->live_range);

	  for (user = other_def->uses; user != NULL; user = user->next)
	    augment_live_range (combined_live_range, &combined_btrs_live,
				def->bb, user->bb,
				(flag_btr_bb_exclusive
				 || user->insn != BB_END (def->bb)
				 || !JUMP_P (user->insn)));

	  btr = choose_btr (combined_btrs_live);
	  if (btr != -1)
	    {
	      /* We can combine them.  */
	      if (dump_file)
		fprintf (dump_file,
			 "Combining def in insn %d with def in insn %d\n",
			 INSN_UID (other_def->insn), INSN_UID (def->insn));

	      def->btr = btr;
	      user = other_def->uses;
	      while (user != NULL)
		{
		  btr_user next = user->next;

		  user->next = def->uses;
		  def->uses = user;
		  user = next;
		}
	      /* Combining def/use webs can make target registers live
		 after uses where they previously were not.  This means
		 some REG_DEAD notes may no longer be correct.  We could
		 be more precise about this if we looked at the combined
		 live range, but here I just delete any REG_DEAD notes
		 in case they are no longer correct.  */
	      for (user = def->uses; user != NULL; user = user->next)
		remove_note (user->insn,
			     find_regno_note (user->insn, REG_DEAD,
					      REGNO (user->use)));
	      clear_btr_from_live_range (other_def);
	      other_def->uses = NULL;
	      bitmap_copy (def->live_range, combined_live_range);
	      if (other_def->btr == btr && other_def->other_btr_uses_after_use)
		def->other_btr_uses_after_use = 1;
	      COPY_HARD_REG_SET (*btrs_live_in_range, combined_btrs_live);

	      /* Delete the old target register initialization.  */
	      delete_insn (other_def->insn);

	    }
	  BITMAP_FREE (combined_live_range);
	}
    }
}

/* Move the definition DEF from its current position to basic
   block NEW_DEF_BB, and modify it to use branch target register BTR.
   Delete the old defining insn, and insert a new one in NEW_DEF_BB.
   Update all reaching uses of DEF in the RTL to use BTR.
   If this new position means that other defs in the
   same group can be combined with DEF then combine them.  */
static void
move_btr_def (basic_block new_def_bb, int btr, btr_def def, bitmap live_range,
	     HARD_REG_SET *btrs_live_in_range)
{
  /* We can move the instruction.
     Set a target register in block NEW_DEF_BB to the value
     needed for this target register definition.
     Replace all uses of the old target register definition by
     uses of the new definition.  Delete the old definition.  */
  basic_block b = new_def_bb;
  rtx insp = BB_HEAD (b);
  rtx old_insn = def->insn;
  rtx src;
  rtx btr_rtx;
  rtx new_insn;
  enum machine_mode btr_mode;
  btr_user user;
  rtx set;

  if (dump_file)
    fprintf(dump_file, "migrating to basic block %d, using reg %d\n",
	    new_def_bb->index, btr);

  clear_btr_from_live_range (def);
  def->btr = btr;
  def->bb = new_def_bb;
  def->luid = 0;
  def->cost = basic_block_freq (new_def_bb);
  bitmap_copy (def->live_range, live_range);
  combine_btr_defs (def, btrs_live_in_range);
  btr = def->btr;
  def->other_btr_uses_before_def
    = TEST_HARD_REG_BIT (btrs_live[b->index], btr) ? 1 : 0;
  add_btr_to_live_range (def, 1);
  if (LABEL_P (insp))
    insp = NEXT_INSN (insp);
  /* N.B.: insp is expected to be NOTE_INSN_BASIC_BLOCK now.  Some
     optimizations can result in insp being both first and last insn of
     its basic block.  */
  /* ?? some assertions to check that insp is sensible? */

  if (def->other_btr_uses_before_def)
    {
      insp = BB_END (b);
      for (insp = BB_END (b); ! INSN_P (insp); insp = PREV_INSN (insp))
	gcc_assert (insp != BB_HEAD (b));

      if (JUMP_P (insp) || can_throw_internal (insp))
	insp = PREV_INSN (insp);
    }

  set = single_set (old_insn);
  src = SET_SRC (set);
  btr_mode = GET_MODE (SET_DEST (set));
  btr_rtx = gen_rtx_REG (btr_mode, btr);

  new_insn = gen_move_insn (btr_rtx, src);

  /* Insert target register initialization at head of basic block.  */
  def->insn = emit_insn_after (new_insn, insp);

  regs_ever_live[btr] = 1;

  if (dump_file)
    fprintf (dump_file, "New pt is insn %d, inserted after insn %d\n",
	     INSN_UID (def->insn), INSN_UID (insp));

  /* Delete the old target register initialization.  */
  delete_insn (old_insn);

  /* Replace each use of the old target register by a use of the new target
     register.  */
  for (user = def->uses; user != NULL; user = user->next)
    {
      /* Some extra work here to ensure consistent modes, because
	 it seems that a target register REG rtx can be given a different
	 mode depending on the context (surely that should not be
	 the case?).  */
      rtx replacement_rtx;
      if (GET_MODE (user->use) == GET_MODE (btr_rtx)
	  || GET_MODE (user->use) == VOIDmode)
	replacement_rtx = btr_rtx;
      else
	replacement_rtx = gen_rtx_REG (GET_MODE (user->use), btr);
      replace_rtx (user->insn, user->use, replacement_rtx);
      user->use = replacement_rtx;
    }
}

/* We anticipate intra-block scheduling to be done.  See if INSN could move
   up within BB by N_INSNS.  */
static int
can_move_up (basic_block bb, rtx insn, int n_insns)
{
  while (insn != BB_HEAD (bb) && n_insns > 0)
    {
      insn = PREV_INSN (insn);
      /* ??? What if we have an anti-dependency that actually prevents the
	 scheduler from doing the move?  We'd like to re-allocate the register,
	 but not necessarily put the load into another basic block.  */
      if (INSN_P (insn))
	n_insns--;
    }
  return n_insns <= 0;
}

/* Attempt to migrate the target register definition DEF to an
   earlier point in the flowgraph.

   It is a precondition of this function that DEF is migratable:
   i.e. it has a constant source, and all uses are unambiguous.

   Only migrations that reduce the cost of DEF will be made.
   MIN_COST is the lower bound on the cost of the DEF after migration.
   If we migrate DEF so that its cost falls below MIN_COST,
   then we do not attempt to migrate further.  The idea is that
   we migrate definitions in a priority order based on their cost,
   when the cost of this definition falls below MIN_COST, then
   there is another definition with cost == MIN_COST which now
   has a higher priority than this definition.

   Return nonzero if there may be benefit from attempting to
   migrate this DEF further (i.e. we have reduced the cost below
   MIN_COST, but we may be able to reduce it further).
   Return zero if no further migration is possible.  */
static int
migrate_btr_def (btr_def def, int min_cost)
{
  bitmap live_range;
  HARD_REG_SET btrs_live_in_range;
  int btr_used_near_def = 0;
  int def_basic_block_freq;
  basic_block try;
  int give_up = 0;
  int def_moved = 0;
  btr_user user;
  int def_latency;

  if (dump_file)
    fprintf (dump_file,
	     "Attempting to migrate pt from insn %d (cost = %d, min_cost = %d) ... ",
	     INSN_UID (def->insn), def->cost, min_cost);

  if (!def->group || def->has_ambiguous_use)
    /* These defs are not migratable.  */
    {
      if (dump_file)
	fprintf (dump_file, "it's not migratable\n");
      return 0;
    }

  if (!def->uses)
    /* We have combined this def with another in the same group, so
       no need to consider it further.
    */
    {
      if (dump_file)
	fprintf (dump_file, "it's already combined with another pt\n");
      return 0;
    }

  btr_def_live_range (def, &btrs_live_in_range);
  live_range = BITMAP_ALLOC (NULL);
  bitmap_copy (live_range, def->live_range);

#ifdef INSN_SCHEDULING
  def_latency = insn_default_latency (def->insn) * issue_rate;
#else
  def_latency = issue_rate;
#endif

  for (user = def->uses; user != NULL; user = user->next)
    {
      if (user->bb == def->bb
	  && user->luid > def->luid
	  && (def->luid + def_latency) > user->luid
	  && ! can_move_up (def->bb, def->insn,
			    (def->luid + def_latency) - user->luid))
	{
	  btr_used_near_def = 1;
	  break;
	}
    }

  def_basic_block_freq = basic_block_freq (def->bb);

  for (try = get_immediate_dominator (CDI_DOMINATORS, def->bb);
       !give_up && try && try != ENTRY_BLOCK_PTR && def->cost >= min_cost;
       try = get_immediate_dominator (CDI_DOMINATORS, try))
    {
      /* Try to move the instruction that sets the target register into
	 basic block TRY.  */
      int try_freq = basic_block_freq (try);
      edge_iterator ei;
      edge e;

      /* If TRY has abnormal edges, skip it.  */
      FOR_EACH_EDGE (e, ei, try->succs)
	if (e->flags & EDGE_COMPLEX)
	  break;
      if (e)
	continue;

      if (dump_file)
	fprintf (dump_file, "trying block %d ...", try->index);

      if (try_freq < def_basic_block_freq
	  || (try_freq == def_basic_block_freq && btr_used_near_def))
	{
	  int btr;
	  augment_live_range (live_range, &btrs_live_in_range, def->bb, try,
			      flag_btr_bb_exclusive);
	  if (dump_file)
	    {
	      fprintf (dump_file, "Now btrs live in range are: ");
	      dump_hard_reg_set (btrs_live_in_range);
	      fprintf (dump_file, "\n");
	    }
	  btr = choose_btr (btrs_live_in_range);
	  if (btr != -1)
	    {
	      move_btr_def (try, btr, def, live_range, &btrs_live_in_range);
	      bitmap_copy(live_range, def->live_range);
	      btr_used_near_def = 0;
	      def_moved = 1;
	      def_basic_block_freq = basic_block_freq (def->bb);
	    }
	  else
	    {
	      /* There are no free target registers available to move
		 this far forward, so give up */
	      give_up = 1;
	      if (dump_file)
		fprintf (dump_file,
			 "giving up because there are no free target registers\n");
	    }

	}
    }
  if (!def_moved)
    {
      give_up = 1;
      if (dump_file)
	fprintf (dump_file, "failed to move\n");
    }
  BITMAP_FREE (live_range);
  return !give_up;
}

/* Attempt to move instructions that set target registers earlier
   in the flowgraph, away from their corresponding uses.  */
static void
migrate_btr_defs (enum reg_class btr_class, int allow_callee_save)
{
  fibheap_t all_btr_defs = fibheap_new ();
  int reg;

  gcc_obstack_init (&migrate_btrl_obstack);
  if (dump_file)
    {
      int i;

      for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
	{
	  basic_block bb = BASIC_BLOCK (i);
	  fprintf(dump_file,
	    "Basic block %d: count = " HOST_WIDEST_INT_PRINT_DEC
	    " loop-depth = %d idom = %d\n",
	    i, (HOST_WIDEST_INT) bb->count, bb->loop_depth,
	    get_immediate_dominator (CDI_DOMINATORS, bb)->index);
	}
    }

  CLEAR_HARD_REG_SET (all_btrs);
  for (first_btr = -1, reg = 0; reg < FIRST_PSEUDO_REGISTER; reg++)
    if (TEST_HARD_REG_BIT (reg_class_contents[(int) btr_class], reg)
	&& (allow_callee_save || call_used_regs[reg] || regs_ever_live[reg]))
      {
	SET_HARD_REG_BIT (all_btrs, reg);
	last_btr = reg;
	if (first_btr < 0)
	  first_btr = reg;
      }

  btrs_live = xcalloc (n_basic_blocks, sizeof (HARD_REG_SET));
  btrs_live_at_end = xcalloc (n_basic_blocks, sizeof (HARD_REG_SET));

  build_btr_def_use_webs (all_btr_defs);

  while (!fibheap_empty (all_btr_defs))
    {
      btr_def def = fibheap_extract_min (all_btr_defs);
      int min_cost = -fibheap_min_key (all_btr_defs);
      if (migrate_btr_def (def, min_cost))
	{
	  fibheap_insert (all_btr_defs, -def->cost, (void *) def);
	  if (dump_file)
	    {
	      fprintf (dump_file,
		"Putting insn %d back on queue with priority %d\n",
		INSN_UID (def->insn), def->cost);
	    }
	}
      else
	BITMAP_FREE (def->live_range);
    }

  free (btrs_live);
  free (btrs_live_at_end);
  obstack_free (&migrate_btrl_obstack, NULL);
  fibheap_delete (all_btr_defs);
}

void
branch_target_load_optimize (bool after_prologue_epilogue_gen)
{
  enum reg_class class = targetm.branch_target_register_class ();
  if (class != NO_REGS)
    {
      /* Initialize issue_rate.  */
      if (targetm.sched.issue_rate)
	issue_rate = targetm.sched.issue_rate ();
      else
	issue_rate = 1;

      /* Build the CFG for migrate_btr_defs.  */
#if 1
      /* This may or may not be needed, depending on where we
	 run this phase.  */
      cleanup_cfg (optimize ? CLEANUP_EXPENSIVE : 0);
#endif

      life_analysis (0);

      /* Dominator info is also needed for migrate_btr_def.  */
      calculate_dominance_info (CDI_DOMINATORS);
      migrate_btr_defs (class,
		       (targetm.branch_target_register_callee_saved
			(after_prologue_epilogue_gen)));

      free_dominance_info (CDI_DOMINATORS);

      update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES,
			PROP_DEATH_NOTES | PROP_REG_INFO);
    }
}

static bool
gate_handle_branch_target_load_optimize (void)
{
  return (optimize > 0 && flag_branch_target_load_optimize2);
}


static unsigned int
rest_of_handle_branch_target_load_optimize (void)
{
  static int warned = 0;

  /* Leave this a warning for now so that it is possible to experiment
     with running this pass twice.  In 3.6, we should either make this
     an error, or use separate dump files.  */
  if (flag_branch_target_load_optimize
      && flag_branch_target_load_optimize2
      && !warned)
    {
      warning (0, "branch target register load optimization is not intended "
		  "to be run twice");

      warned = 1;
    }

  branch_target_load_optimize (epilogue_completed);
  return 0;
}

struct tree_opt_pass pass_branch_target_load_optimize =
{
  "btl",                               /* name */
  gate_handle_branch_target_load_optimize,      /* gate */
  rest_of_handle_branch_target_load_optimize,   /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,					/* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'd'                                   /* letter */
};

