/* Allocate registers within a basic block, for GNU compiler.
   Copyright (C) 1987, 1988, 1991, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation,
   Inc.

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

/* Allocation of hard register numbers to pseudo registers is done in
   two passes.  In this pass we consider only regs that are born and
   die once within one basic block.  We do this one basic block at a
   time.  Then the next pass allocates the registers that remain.
   Two passes are used because this pass uses methods that work only
   on linear code, but that do a better job than the general methods
   used in global_alloc, and more quickly too.

   The assignments made are recorded in the vector reg_renumber
   whose space is allocated here.  The rtl code itself is not altered.

   We assign each instruction in the basic block a number
   which is its order from the beginning of the block.
   Then we can represent the lifetime of a pseudo register with
   a pair of numbers, and check for conflicts easily.
   We can record the availability of hard registers with a
   HARD_REG_SET for each instruction.  The HARD_REG_SET
   contains 0 or 1 for each hard reg.

   To avoid register shuffling, we tie registers together when one
   dies by being copied into another, or dies in an instruction that
   does arithmetic to produce another.  The tied registers are
   allocated as one.  Registers with different reg class preferences
   can never be tied unless the class preferred by one is a subclass
   of the one preferred by the other.

   Tying is represented with "quantity numbers".
   A non-tied register is given a new quantity number.
   Tied registers have the same quantity number.

   We have provision to exempt registers, even when they are contained
   within the block, that can be tied to others that are not contained in it.
   This is so that global_alloc could process them both and tie them then.
   But this is currently disabled since tying in global_alloc is not
   yet implemented.  */

/* Pseudos allocated here can be reallocated by global.c if the hard register
   is used as a spill register.  Currently we don't allocate such pseudos
   here if their preferred class is likely to be used by spills.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "hard-reg-set.h"
#include "rtl.h"
#include "tm_p.h"
#include "flags.h"
#include "regs.h"
#include "function.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "output.h"
#include "toplev.h"
#include "except.h"
#include "integrate.h"
#include "reload.h"
#include "ggc.h"
#include "timevar.h"
#include "tree-pass.h"

/* Next quantity number available for allocation.  */

static int next_qty;

/* Information we maintain about each quantity.  */
struct qty
{
  /* The number of refs to quantity Q.  */

  int n_refs;

  /* The frequency of uses of quantity Q.  */

  int freq;

  /* Insn number (counting from head of basic block)
     where quantity Q was born.  -1 if birth has not been recorded.  */

  int birth;

  /* Insn number (counting from head of basic block)
     where given quantity died.  Due to the way tying is done,
     and the fact that we consider in this pass only regs that die but once,
     a quantity can die only once.  Each quantity's life span
     is a set of consecutive insns.  -1 if death has not been recorded.  */

  int death;

  /* Number of words needed to hold the data in given quantity.
     This depends on its machine mode.  It is used for these purposes:
     1. It is used in computing the relative importance of qtys,
	which determines the order in which we look for regs for them.
     2. It is used in rules that prevent tying several registers of
	different sizes in a way that is geometrically impossible
	(see combine_regs).  */

  int size;

  /* Number of times a reg tied to given qty lives across a CALL_INSN.  */

  int n_calls_crossed;

  /* Number of times a reg tied to given qty lives across a CALL_INSN
     that might throw.  */

  int n_throwing_calls_crossed;

  /* The register number of one pseudo register whose reg_qty value is Q.
     This register should be the head of the chain
     maintained in reg_next_in_qty.  */

  int first_reg;

  /* Reg class contained in (smaller than) the preferred classes of all
     the pseudo regs that are tied in given quantity.
     This is the preferred class for allocating that quantity.  */

  enum reg_class min_class;

  /* Register class within which we allocate given qty if we can't get
     its preferred class.  */

  enum reg_class alternate_class;

  /* This holds the mode of the registers that are tied to given qty,
     or VOIDmode if registers with differing modes are tied together.  */

  enum machine_mode mode;

  /* the hard reg number chosen for given quantity,
     or -1 if none was found.  */

  short phys_reg;
};

static struct qty *qty;

/* These fields are kept separately to speedup their clearing.  */

/* We maintain two hard register sets that indicate suggested hard registers
   for each quantity.  The first, phys_copy_sugg, contains hard registers
   that are tied to the quantity by a simple copy.  The second contains all
   hard registers that are tied to the quantity via an arithmetic operation.

   The former register set is given priority for allocation.  This tends to
   eliminate copy insns.  */

/* Element Q is a set of hard registers that are suggested for quantity Q by
   copy insns.  */

static HARD_REG_SET *qty_phys_copy_sugg;

/* Element Q is a set of hard registers that are suggested for quantity Q by
   arithmetic insns.  */

static HARD_REG_SET *qty_phys_sugg;

/* Element Q is the number of suggested registers in qty_phys_copy_sugg.  */

static short *qty_phys_num_copy_sugg;

/* Element Q is the number of suggested registers in qty_phys_sugg.  */

static short *qty_phys_num_sugg;

/* If (REG N) has been assigned a quantity number, is a register number
   of another register assigned the same quantity number, or -1 for the
   end of the chain.  qty->first_reg point to the head of this chain.  */

static int *reg_next_in_qty;

/* reg_qty[N] (where N is a pseudo reg number) is the qty number of that reg
   if it is >= 0,
   of -1 if this register cannot be allocated by local-alloc,
   or -2 if not known yet.

   Note that if we see a use or death of pseudo register N with
   reg_qty[N] == -2, register N must be local to the current block.  If
   it were used in more than one block, we would have reg_qty[N] == -1.
   This relies on the fact that if reg_basic_block[N] is >= 0, register N
   will not appear in any other block.  We save a considerable number of
   tests by exploiting this.

   If N is < FIRST_PSEUDO_REGISTER, reg_qty[N] is undefined and should not
   be referenced.  */

static int *reg_qty;

/* The offset (in words) of register N within its quantity.
   This can be nonzero if register N is SImode, and has been tied
   to a subreg of a DImode register.  */

static char *reg_offset;

/* Vector of substitutions of register numbers,
   used to map pseudo regs into hardware regs.
   This is set up as a result of register allocation.
   Element N is the hard reg assigned to pseudo reg N,
   or is -1 if no hard reg was assigned.
   If N is a hard reg number, element N is N.  */

short *reg_renumber;

/* Set of hard registers live at the current point in the scan
   of the instructions in a basic block.  */

static HARD_REG_SET regs_live;

/* Each set of hard registers indicates registers live at a particular
   point in the basic block.  For N even, regs_live_at[N] says which
   hard registers are needed *after* insn N/2 (i.e., they may not
   conflict with the outputs of insn N/2 or the inputs of insn N/2 + 1.

   If an object is to conflict with the inputs of insn J but not the
   outputs of insn J + 1, we say it is born at index J*2 - 1.  Similarly,
   if it is to conflict with the outputs of insn J but not the inputs of
   insn J + 1, it is said to die at index J*2 + 1.  */

static HARD_REG_SET *regs_live_at;

/* Communicate local vars `insn_number' and `insn'
   from `block_alloc' to `reg_is_set', `wipe_dead_reg', and `alloc_qty'.  */
static int this_insn_number;
static rtx this_insn;

struct equivalence
{
  /* Set when an attempt should be made to replace a register
     with the associated src_p entry.  */

  char replace;

  /* Set when a REG_EQUIV note is found or created.  Use to
     keep track of what memory accesses might be created later,
     e.g. by reload.  */

  rtx replacement;

  rtx *src_p;

  /* Loop depth is used to recognize equivalences which appear
     to be present within the same loop (or in an inner loop).  */

  int loop_depth;

  /* The list of each instruction which initializes this register.  */

  rtx init_insns;

  /* Nonzero if this had a preexisting REG_EQUIV note.  */

  int is_arg_equivalence;
};

/* reg_equiv[N] (where N is a pseudo reg number) is the equivalence
   structure for that register.  */

static struct equivalence *reg_equiv;

/* Nonzero if we recorded an equivalence for a LABEL_REF.  */
static int recorded_label_ref;

static void alloc_qty (int, enum machine_mode, int, int);
static void validate_equiv_mem_from_store (rtx, rtx, void *);
static int validate_equiv_mem (rtx, rtx, rtx);
static int equiv_init_varies_p (rtx);
static int equiv_init_movable_p (rtx, int);
static int contains_replace_regs (rtx);
static int memref_referenced_p (rtx, rtx);
static int memref_used_between_p (rtx, rtx, rtx);
static void update_equiv_regs (void);
static void no_equiv (rtx, rtx, void *);
static void block_alloc (int);
static int qty_sugg_compare (int, int);
static int qty_sugg_compare_1 (const void *, const void *);
static int qty_compare (int, int);
static int qty_compare_1 (const void *, const void *);
static int combine_regs (rtx, rtx, int, int, rtx, int);
static int reg_meets_class_p (int, enum reg_class);
static void update_qty_class (int, int);
static void reg_is_set (rtx, rtx, void *);
static void reg_is_born (rtx, int);
static void wipe_dead_reg (rtx, int);
static int find_free_reg (enum reg_class, enum machine_mode, int, int, int,
			  int, int);
static void mark_life (int, enum machine_mode, int);
static void post_mark_life (int, enum machine_mode, int, int, int);
static int no_conflict_p (rtx, rtx, rtx);
static int requires_inout (const char *);

/* Allocate a new quantity (new within current basic block)
   for register number REGNO which is born at index BIRTH
   within the block.  MODE and SIZE are info on reg REGNO.  */

static void
alloc_qty (int regno, enum machine_mode mode, int size, int birth)
{
  int qtyno = next_qty++;

  reg_qty[regno] = qtyno;
  reg_offset[regno] = 0;
  reg_next_in_qty[regno] = -1;

  qty[qtyno].first_reg = regno;
  qty[qtyno].size = size;
  qty[qtyno].mode = mode;
  qty[qtyno].birth = birth;
  qty[qtyno].n_calls_crossed = REG_N_CALLS_CROSSED (regno);
  qty[qtyno].n_throwing_calls_crossed = REG_N_THROWING_CALLS_CROSSED (regno);
  qty[qtyno].min_class = reg_preferred_class (regno);
  qty[qtyno].alternate_class = reg_alternate_class (regno);
  qty[qtyno].n_refs = REG_N_REFS (regno);
  qty[qtyno].freq = REG_FREQ (regno);
}

/* Main entry point of this file.  */

static int
local_alloc (void)
{
  int i;
  int max_qty;
  basic_block b;

  /* We need to keep track of whether or not we recorded a LABEL_REF so
     that we know if the jump optimizer needs to be rerun.  */
  recorded_label_ref = 0;

  /* Leaf functions and non-leaf functions have different needs.
     If defined, let the machine say what kind of ordering we
     should use.  */
#ifdef ORDER_REGS_FOR_LOCAL_ALLOC
  ORDER_REGS_FOR_LOCAL_ALLOC;
#endif

  /* Promote REG_EQUAL notes to REG_EQUIV notes and adjust status of affected
     registers.  */
  update_equiv_regs ();

  /* This sets the maximum number of quantities we can have.  Quantity
     numbers start at zero and we can have one for each pseudo.  */
  max_qty = (max_regno - FIRST_PSEUDO_REGISTER);

  /* Allocate vectors of temporary data.
     See the declarations of these variables, above,
     for what they mean.  */

  qty = XNEWVEC (struct qty, max_qty);
  qty_phys_copy_sugg = XNEWVEC (HARD_REG_SET, max_qty);
  qty_phys_num_copy_sugg = XNEWVEC (short, max_qty);
  qty_phys_sugg = XNEWVEC (HARD_REG_SET, max_qty);
  qty_phys_num_sugg = XNEWVEC (short, max_qty);

  reg_qty = XNEWVEC (int, max_regno);
  reg_offset = XNEWVEC (char, max_regno);
  reg_next_in_qty = XNEWVEC (int, max_regno);

  /* Determine which pseudo-registers can be allocated by local-alloc.
     In general, these are the registers used only in a single block and
     which only die once.

     We need not be concerned with which block actually uses the register
     since we will never see it outside that block.  */

  for (i = FIRST_PSEUDO_REGISTER; i < max_regno; i++)
    {
      if (REG_BASIC_BLOCK (i) >= 0 && REG_N_DEATHS (i) == 1)
	reg_qty[i] = -2;
      else
	reg_qty[i] = -1;
    }

  /* Force loop below to initialize entire quantity array.  */
  next_qty = max_qty;

  /* Allocate each block's local registers, block by block.  */

  FOR_EACH_BB (b)
    {
      /* NEXT_QTY indicates which elements of the `qty_...'
	 vectors might need to be initialized because they were used
	 for the previous block; it is set to the entire array before
	 block 0.  Initialize those, with explicit loop if there are few,
	 else with bzero and bcopy.  Do not initialize vectors that are
	 explicit set by `alloc_qty'.  */

      if (next_qty < 6)
	{
	  for (i = 0; i < next_qty; i++)
	    {
	      CLEAR_HARD_REG_SET (qty_phys_copy_sugg[i]);
	      qty_phys_num_copy_sugg[i] = 0;
	      CLEAR_HARD_REG_SET (qty_phys_sugg[i]);
	      qty_phys_num_sugg[i] = 0;
	    }
	}
      else
	{
#define CLEAR(vector)  \
	  memset ((vector), 0, (sizeof (*(vector))) * next_qty);

	  CLEAR (qty_phys_copy_sugg);
	  CLEAR (qty_phys_num_copy_sugg);
	  CLEAR (qty_phys_sugg);
	  CLEAR (qty_phys_num_sugg);
	}

      next_qty = 0;

      block_alloc (b->index);
    }

  free (qty);
  free (qty_phys_copy_sugg);
  free (qty_phys_num_copy_sugg);
  free (qty_phys_sugg);
  free (qty_phys_num_sugg);

  free (reg_qty);
  free (reg_offset);
  free (reg_next_in_qty);

  return recorded_label_ref;
}

/* Used for communication between the following two functions: contains
   a MEM that we wish to ensure remains unchanged.  */
static rtx equiv_mem;

/* Set nonzero if EQUIV_MEM is modified.  */
static int equiv_mem_modified;

/* If EQUIV_MEM is modified by modifying DEST, indicate that it is modified.
   Called via note_stores.  */

static void
validate_equiv_mem_from_store (rtx dest, rtx set ATTRIBUTE_UNUSED,
			       void *data ATTRIBUTE_UNUSED)
{
  if ((REG_P (dest)
       && reg_overlap_mentioned_p (dest, equiv_mem))
      || (MEM_P (dest)
	  && true_dependence (dest, VOIDmode, equiv_mem, rtx_varies_p)))
    equiv_mem_modified = 1;
}

/* Verify that no store between START and the death of REG invalidates
   MEMREF.  MEMREF is invalidated by modifying a register used in MEMREF,
   by storing into an overlapping memory location, or with a non-const
   CALL_INSN.

   Return 1 if MEMREF remains valid.  */

static int
validate_equiv_mem (rtx start, rtx reg, rtx memref)
{
  rtx insn;
  rtx note;

  equiv_mem = memref;
  equiv_mem_modified = 0;

  /* If the memory reference has side effects or is volatile, it isn't a
     valid equivalence.  */
  if (side_effects_p (memref))
    return 0;

  for (insn = start; insn && ! equiv_mem_modified; insn = NEXT_INSN (insn))
    {
      if (! INSN_P (insn))
	continue;

      if (find_reg_note (insn, REG_DEAD, reg))
	return 1;

      if (CALL_P (insn) && ! MEM_READONLY_P (memref)
	  && ! CONST_OR_PURE_CALL_P (insn))
	return 0;

      note_stores (PATTERN (insn), validate_equiv_mem_from_store, NULL);

      /* If a register mentioned in MEMREF is modified via an
	 auto-increment, we lose the equivalence.  Do the same if one
	 dies; although we could extend the life, it doesn't seem worth
	 the trouble.  */

      for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
	if ((REG_NOTE_KIND (note) == REG_INC
	     || REG_NOTE_KIND (note) == REG_DEAD)
	    && REG_P (XEXP (note, 0))
	    && reg_overlap_mentioned_p (XEXP (note, 0), memref))
	  return 0;
    }

  return 0;
}

/* Returns zero if X is known to be invariant.  */

static int
equiv_init_varies_p (rtx x)
{
  RTX_CODE code = GET_CODE (x);
  int i;
  const char *fmt;

  switch (code)
    {
    case MEM:
      return !MEM_READONLY_P (x) || equiv_init_varies_p (XEXP (x, 0));

    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
      return 0;

    case REG:
      return reg_equiv[REGNO (x)].replace == 0 && rtx_varies_p (x, 0);

    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 1;

      /* Fall through.  */

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    if (fmt[i] == 'e')
      {
	if (equiv_init_varies_p (XEXP (x, i)))
	  return 1;
      }
    else if (fmt[i] == 'E')
      {
	int j;
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (equiv_init_varies_p (XVECEXP (x, i, j)))
	    return 1;
      }

  return 0;
}

/* Returns nonzero if X (used to initialize register REGNO) is movable.
   X is only movable if the registers it uses have equivalent initializations
   which appear to be within the same loop (or in an inner loop) and movable
   or if they are not candidates for local_alloc and don't vary.  */

static int
equiv_init_movable_p (rtx x, int regno)
{
  int i, j;
  const char *fmt;
  enum rtx_code code = GET_CODE (x);

  switch (code)
    {
    case SET:
      return equiv_init_movable_p (SET_SRC (x), regno);

    case CC0:
    case CLOBBER:
      return 0;

    case PRE_INC:
    case PRE_DEC:
    case POST_INC:
    case POST_DEC:
    case PRE_MODIFY:
    case POST_MODIFY:
      return 0;

    case REG:
      return (reg_equiv[REGNO (x)].loop_depth >= reg_equiv[regno].loop_depth
	      && reg_equiv[REGNO (x)].replace)
	     || (REG_BASIC_BLOCK (REGNO (x)) < 0 && ! rtx_varies_p (x, 0));

    case UNSPEC_VOLATILE:
      return 0;

    case ASM_OPERANDS:
      if (MEM_VOLATILE_P (x))
	return 0;

      /* Fall through.  */

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    switch (fmt[i])
      {
      case 'e':
	if (! equiv_init_movable_p (XEXP (x, i), regno))
	  return 0;
	break;
      case 'E':
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  if (! equiv_init_movable_p (XVECEXP (x, i, j), regno))
	    return 0;
	break;
      }

  return 1;
}

/* TRUE if X uses any registers for which reg_equiv[REGNO].replace is true.  */

static int
contains_replace_regs (rtx x)
{
  int i, j;
  const char *fmt;
  enum rtx_code code = GET_CODE (x);

  switch (code)
    {
    case CONST_INT:
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case PC:
    case CC0:
    case HIGH:
      return 0;

    case REG:
      return reg_equiv[REGNO (x)].replace;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    switch (fmt[i])
      {
      case 'e':
	if (contains_replace_regs (XEXP (x, i)))
	  return 1;
	break;
      case 'E':
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  if (contains_replace_regs (XVECEXP (x, i, j)))
	    return 1;
	break;
      }

  return 0;
}

/* TRUE if X references a memory location that would be affected by a store
   to MEMREF.  */

static int
memref_referenced_p (rtx memref, rtx x)
{
  int i, j;
  const char *fmt;
  enum rtx_code code = GET_CODE (x);

  switch (code)
    {
    case CONST_INT:
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case PC:
    case CC0:
    case HIGH:
    case LO_SUM:
      return 0;

    case REG:
      return (reg_equiv[REGNO (x)].replacement
	      && memref_referenced_p (memref,
				      reg_equiv[REGNO (x)].replacement));

    case MEM:
      if (true_dependence (memref, VOIDmode, x, rtx_varies_p))
	return 1;
      break;

    case SET:
      /* If we are setting a MEM, it doesn't count (its address does), but any
	 other SET_DEST that has a MEM in it is referencing the MEM.  */
      if (MEM_P (SET_DEST (x)))
	{
	  if (memref_referenced_p (memref, XEXP (SET_DEST (x), 0)))
	    return 1;
	}
      else if (memref_referenced_p (memref, SET_DEST (x)))
	return 1;

      return memref_referenced_p (memref, SET_SRC (x));

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    switch (fmt[i])
      {
      case 'e':
	if (memref_referenced_p (memref, XEXP (x, i)))
	  return 1;
	break;
      case 'E':
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  if (memref_referenced_p (memref, XVECEXP (x, i, j)))
	    return 1;
	break;
      }

  return 0;
}

/* TRUE if some insn in the range (START, END] references a memory location
   that would be affected by a store to MEMREF.  */

static int
memref_used_between_p (rtx memref, rtx start, rtx end)
{
  rtx insn;

  for (insn = NEXT_INSN (start); insn != NEXT_INSN (end);
       insn = NEXT_INSN (insn))
    {
      if (!INSN_P (insn))
	continue;
      
      if (memref_referenced_p (memref, PATTERN (insn)))
	return 1;

      /* Nonconst functions may access memory.  */
      if (CALL_P (insn)
	  && (! CONST_OR_PURE_CALL_P (insn)
	      || pure_call_p (insn)))
	return 1;
    }

  return 0;
}

/* Find registers that are equivalent to a single value throughout the
   compilation (either because they can be referenced in memory or are set once
   from a single constant).  Lower their priority for a register.

   If such a register is only referenced once, try substituting its value
   into the using insn.  If it succeeds, we can eliminate the register
   completely.

   Initialize the REG_EQUIV_INIT array of initializing insns.  */

static void
update_equiv_regs (void)
{
  rtx insn;
  basic_block bb;
  int loop_depth;
  regset_head cleared_regs;
  int clear_regnos = 0;

  reg_equiv = XCNEWVEC (struct equivalence, max_regno);
  INIT_REG_SET (&cleared_regs);
  reg_equiv_init = ggc_alloc_cleared (max_regno * sizeof (rtx));
  reg_equiv_init_size = max_regno;

  init_alias_analysis ();

  /* Scan the insns and find which registers have equivalences.  Do this
     in a separate scan of the insns because (due to -fcse-follow-jumps)
     a register can be set below its use.  */
  FOR_EACH_BB (bb)
    {
      loop_depth = bb->loop_depth;

      for (insn = BB_HEAD (bb);
	   insn != NEXT_INSN (BB_END (bb));
	   insn = NEXT_INSN (insn))
	{
	  rtx note;
	  rtx set;
	  rtx dest, src;
	  int regno;

	  if (! INSN_P (insn))
	    continue;

	  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
	    if (REG_NOTE_KIND (note) == REG_INC)
	      no_equiv (XEXP (note, 0), note, NULL);

	  set = single_set (insn);

	  /* If this insn contains more (or less) than a single SET,
	     only mark all destinations as having no known equivalence.  */
	  if (set == 0)
	    {
	      note_stores (PATTERN (insn), no_equiv, NULL);
	      continue;
	    }
	  else if (GET_CODE (PATTERN (insn)) == PARALLEL)
	    {
	      int i;

	      for (i = XVECLEN (PATTERN (insn), 0) - 1; i >= 0; i--)
		{
		  rtx part = XVECEXP (PATTERN (insn), 0, i);
		  if (part != set)
		    note_stores (part, no_equiv, NULL);
		}
	    }

	  dest = SET_DEST (set);
	  src = SET_SRC (set);

	  /* See if this is setting up the equivalence between an argument
	     register and its stack slot.  */
	  note = find_reg_note (insn, REG_EQUIV, NULL_RTX);
	  if (note)
	    {
	      gcc_assert (REG_P (dest));
	      regno = REGNO (dest);

	      /* Note that we don't want to clear reg_equiv_init even if there
		 are multiple sets of this register.  */
	      reg_equiv[regno].is_arg_equivalence = 1;

	      /* Record for reload that this is an equivalencing insn.  */
	      if (rtx_equal_p (src, XEXP (note, 0)))
		reg_equiv_init[regno]
		  = gen_rtx_INSN_LIST (VOIDmode, insn, reg_equiv_init[regno]);

	      /* Continue normally in case this is a candidate for
		 replacements.  */
	    }

	  if (!optimize)
	    continue;

	  /* We only handle the case of a pseudo register being set
	     once, or always to the same value.  */
	  /* ??? The mn10200 port breaks if we add equivalences for
	     values that need an ADDRESS_REGS register and set them equivalent
	     to a MEM of a pseudo.  The actual problem is in the over-conservative
	     handling of INPADDR_ADDRESS / INPUT_ADDRESS / INPUT triples in
	     calculate_needs, but we traditionally work around this problem
	     here by rejecting equivalences when the destination is in a register
	     that's likely spilled.  This is fragile, of course, since the
	     preferred class of a pseudo depends on all instructions that set
	     or use it.  */

	  if (!REG_P (dest)
	      || (regno = REGNO (dest)) < FIRST_PSEUDO_REGISTER
	      || reg_equiv[regno].init_insns == const0_rtx
	      || (CLASS_LIKELY_SPILLED_P (reg_preferred_class (regno))
		  && MEM_P (src) && ! reg_equiv[regno].is_arg_equivalence))
	    {
	      /* This might be setting a SUBREG of a pseudo, a pseudo that is
		 also set somewhere else to a constant.  */
	      note_stores (set, no_equiv, NULL);
	      continue;
	    }

	  note = find_reg_note (insn, REG_EQUAL, NULL_RTX);

	  /* cse sometimes generates function invariants, but doesn't put a
	     REG_EQUAL note on the insn.  Since this note would be redundant,
	     there's no point creating it earlier than here.  */
	  if (! note && ! rtx_varies_p (src, 0))
	    note = set_unique_reg_note (insn, REG_EQUAL, src);

	  /* Don't bother considering a REG_EQUAL note containing an EXPR_LIST
	     since it represents a function call */
	  if (note && GET_CODE (XEXP (note, 0)) == EXPR_LIST)
	    note = NULL_RTX;

	  if (REG_N_SETS (regno) != 1
	      && (! note
		  || rtx_varies_p (XEXP (note, 0), 0)
		  || (reg_equiv[regno].replacement
		      && ! rtx_equal_p (XEXP (note, 0),
					reg_equiv[regno].replacement))))
	    {
	      no_equiv (dest, set, NULL);
	      continue;
	    }
	  /* Record this insn as initializing this register.  */
	  reg_equiv[regno].init_insns
	    = gen_rtx_INSN_LIST (VOIDmode, insn, reg_equiv[regno].init_insns);

	  /* If this register is known to be equal to a constant, record that
	     it is always equivalent to the constant.  */
	  if (note && ! rtx_varies_p (XEXP (note, 0), 0))
	    PUT_MODE (note, (enum machine_mode) REG_EQUIV);

	  /* If this insn introduces a "constant" register, decrease the priority
	     of that register.  Record this insn if the register is only used once
	     more and the equivalence value is the same as our source.

	     The latter condition is checked for two reasons:  First, it is an
	     indication that it may be more efficient to actually emit the insn
	     as written (if no registers are available, reload will substitute
	     the equivalence).  Secondly, it avoids problems with any registers
	     dying in this insn whose death notes would be missed.

	     If we don't have a REG_EQUIV note, see if this insn is loading
	     a register used only in one basic block from a MEM.  If so, and the
	     MEM remains unchanged for the life of the register, add a REG_EQUIV
	     note.  */

	  note = find_reg_note (insn, REG_EQUIV, NULL_RTX);

	  if (note == 0 && REG_BASIC_BLOCK (regno) >= 0
	      && MEM_P (SET_SRC (set))
	      && validate_equiv_mem (insn, dest, SET_SRC (set)))
	    REG_NOTES (insn) = note = gen_rtx_EXPR_LIST (REG_EQUIV, SET_SRC (set),
							 REG_NOTES (insn));

	  if (note)
	    {
	      int regno = REGNO (dest);
	      rtx x = XEXP (note, 0);

	      /* If we haven't done so, record for reload that this is an
		 equivalencing insn.  */
	      if (!reg_equiv[regno].is_arg_equivalence)
		reg_equiv_init[regno]
		  = gen_rtx_INSN_LIST (VOIDmode, insn, reg_equiv_init[regno]);

	      /* Record whether or not we created a REG_EQUIV note for a LABEL_REF.
		 We might end up substituting the LABEL_REF for uses of the
		 pseudo here or later.  That kind of transformation may turn an
		 indirect jump into a direct jump, in which case we must rerun the
		 jump optimizer to ensure that the JUMP_LABEL fields are valid.  */
	      if (GET_CODE (x) == LABEL_REF
		  || (GET_CODE (x) == CONST
		      && GET_CODE (XEXP (x, 0)) == PLUS
		      && (GET_CODE (XEXP (XEXP (x, 0), 0)) == LABEL_REF)))
		recorded_label_ref = 1;

	      reg_equiv[regno].replacement = x;
	      reg_equiv[regno].src_p = &SET_SRC (set);
	      reg_equiv[regno].loop_depth = loop_depth;

	      /* Don't mess with things live during setjmp.  */
	      if (REG_LIVE_LENGTH (regno) >= 0 && optimize)
		{
		  /* Note that the statement below does not affect the priority
		     in local-alloc!  */
		  REG_LIVE_LENGTH (regno) *= 2;

		  /* If the register is referenced exactly twice, meaning it is
		     set once and used once, indicate that the reference may be
		     replaced by the equivalence we computed above.  Do this
		     even if the register is only used in one block so that
		     dependencies can be handled where the last register is
		     used in a different block (i.e. HIGH / LO_SUM sequences)
		     and to reduce the number of registers alive across
		     calls.  */

		  if (REG_N_REFS (regno) == 2
		      && (rtx_equal_p (x, src)
			  || ! equiv_init_varies_p (src))
		      && NONJUMP_INSN_P (insn)
		      && equiv_init_movable_p (PATTERN (insn), regno))
		    reg_equiv[regno].replace = 1;
		}
	    }
	}
    }

  if (!optimize)
    goto out;

  /* A second pass, to gather additional equivalences with memory.  This needs
     to be done after we know which registers we are going to replace.  */

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      rtx set, src, dest;
      unsigned regno;

      if (! INSN_P (insn))
	continue;

      set = single_set (insn);
      if (! set)
	continue;

      dest = SET_DEST (set);
      src = SET_SRC (set);

      /* If this sets a MEM to the contents of a REG that is only used
	 in a single basic block, see if the register is always equivalent
	 to that memory location and if moving the store from INSN to the
	 insn that set REG is safe.  If so, put a REG_EQUIV note on the
	 initializing insn.

	 Don't add a REG_EQUIV note if the insn already has one.  The existing
	 REG_EQUIV is likely more useful than the one we are adding.

	 If one of the regs in the address has reg_equiv[REGNO].replace set,
	 then we can't add this REG_EQUIV note.  The reg_equiv[REGNO].replace
	 optimization may move the set of this register immediately before
	 insn, which puts it after reg_equiv[REGNO].init_insns, and hence
	 the mention in the REG_EQUIV note would be to an uninitialized
	 pseudo.  */

      if (MEM_P (dest) && REG_P (src)
	  && (regno = REGNO (src)) >= FIRST_PSEUDO_REGISTER
	  && REG_BASIC_BLOCK (regno) >= 0
	  && REG_N_SETS (regno) == 1
	  && reg_equiv[regno].init_insns != 0
	  && reg_equiv[regno].init_insns != const0_rtx
	  && ! find_reg_note (XEXP (reg_equiv[regno].init_insns, 0),
			      REG_EQUIV, NULL_RTX)
	  && ! contains_replace_regs (XEXP (dest, 0)))
	{
	  rtx init_insn = XEXP (reg_equiv[regno].init_insns, 0);
	  if (validate_equiv_mem (init_insn, src, dest)
	      && ! memref_used_between_p (dest, init_insn, insn))
	    {
	      REG_NOTES (init_insn)
		= gen_rtx_EXPR_LIST (REG_EQUIV, dest,
				     REG_NOTES (init_insn));
	      /* This insn makes the equivalence, not the one initializing
		 the register.  */
	      reg_equiv_init[regno]
		= gen_rtx_INSN_LIST (VOIDmode, insn, NULL_RTX);
	    }
	}
    }

  /* Now scan all regs killed in an insn to see if any of them are
     registers only used that once.  If so, see if we can replace the
     reference with the equivalent form.  If we can, delete the
     initializing reference and this register will go away.  If we
     can't replace the reference, and the initializing reference is
     within the same loop (or in an inner loop), then move the register
     initialization just before the use, so that they are in the same
     basic block.  */
  FOR_EACH_BB_REVERSE (bb)
    {
      loop_depth = bb->loop_depth;
      for (insn = BB_END (bb);
	   insn != PREV_INSN (BB_HEAD (bb));
	   insn = PREV_INSN (insn))
	{
	  rtx link;

	  if (! INSN_P (insn))
	    continue;

	  /* Don't substitute into a non-local goto, this confuses CFG.  */
	  if (JUMP_P (insn)
	      && find_reg_note (insn, REG_NON_LOCAL_GOTO, NULL_RTX))
	    continue;

	  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
	    {
	      if (REG_NOTE_KIND (link) == REG_DEAD
		  /* Make sure this insn still refers to the register.  */
		  && reg_mentioned_p (XEXP (link, 0), PATTERN (insn)))
		{
		  int regno = REGNO (XEXP (link, 0));
		  rtx equiv_insn;

		  if (! reg_equiv[regno].replace
		      || reg_equiv[regno].loop_depth < loop_depth)
		    continue;

		  /* reg_equiv[REGNO].replace gets set only when
		     REG_N_REFS[REGNO] is 2, i.e. the register is set
		     once and used once.  (If it were only set, but not used,
		     flow would have deleted the setting insns.)  Hence
		     there can only be one insn in reg_equiv[REGNO].init_insns.  */
		  gcc_assert (reg_equiv[regno].init_insns
			      && !XEXP (reg_equiv[regno].init_insns, 1));
		  equiv_insn = XEXP (reg_equiv[regno].init_insns, 0);

		  /* We may not move instructions that can throw, since
		     that changes basic block boundaries and we are not
		     prepared to adjust the CFG to match.  */
		  if (can_throw_internal (equiv_insn))
		    continue;

		  if (asm_noperands (PATTERN (equiv_insn)) < 0
		      && validate_replace_rtx (regno_reg_rtx[regno],
					       *(reg_equiv[regno].src_p), insn))
		    {
		      rtx equiv_link;
		      rtx last_link;
		      rtx note;

		      /* Find the last note.  */
		      for (last_link = link; XEXP (last_link, 1);
			   last_link = XEXP (last_link, 1))
			;

		      /* Append the REG_DEAD notes from equiv_insn.  */
		      equiv_link = REG_NOTES (equiv_insn);
		      while (equiv_link)
			{
			  note = equiv_link;
			  equiv_link = XEXP (equiv_link, 1);
			  if (REG_NOTE_KIND (note) == REG_DEAD)
			    {
			      remove_note (equiv_insn, note);
			      XEXP (last_link, 1) = note;
			      XEXP (note, 1) = NULL_RTX;
			      last_link = note;
			    }
			}

		      remove_death (regno, insn);
		      REG_N_REFS (regno) = 0;
		      REG_FREQ (regno) = 0;
		      delete_insn (equiv_insn);

		      reg_equiv[regno].init_insns
			= XEXP (reg_equiv[regno].init_insns, 1);

		      /* Remember to clear REGNO from all basic block's live
			 info.  */
		      SET_REGNO_REG_SET (&cleared_regs, regno);
		      clear_regnos++;
		      reg_equiv_init[regno] = NULL_RTX;
		    }
		  /* Move the initialization of the register to just before
		     INSN.  Update the flow information.  */
		  else if (PREV_INSN (insn) != equiv_insn)
		    {
		      rtx new_insn;

		      new_insn = emit_insn_before (PATTERN (equiv_insn), insn);
		      REG_NOTES (new_insn) = REG_NOTES (equiv_insn);
		      REG_NOTES (equiv_insn) = 0;

		      /* Make sure this insn is recognized before
			 reload begins, otherwise
			 eliminate_regs_in_insn will die.  */
		      INSN_CODE (new_insn) = INSN_CODE (equiv_insn);

		      delete_insn (equiv_insn);

		      XEXP (reg_equiv[regno].init_insns, 0) = new_insn;

		      REG_BASIC_BLOCK (regno) = bb->index;
		      REG_N_CALLS_CROSSED (regno) = 0;
		      REG_N_THROWING_CALLS_CROSSED (regno) = 0;
		      REG_LIVE_LENGTH (regno) = 2;

		      if (insn == BB_HEAD (bb))
			BB_HEAD (bb) = PREV_INSN (insn);

		      /* Remember to clear REGNO from all basic block's live
			 info.  */
		      SET_REGNO_REG_SET (&cleared_regs, regno);
		      clear_regnos++;
		      reg_equiv_init[regno]
			= gen_rtx_INSN_LIST (VOIDmode, new_insn, NULL_RTX);
		    }
		}
	    }
	}
    }

  /* Clear all dead REGNOs from all basic block's live info.  */
  if (clear_regnos)
    {
      unsigned j;
      
      if (clear_regnos > 8)
	{
	  FOR_EACH_BB (bb)
	    {
	      AND_COMPL_REG_SET (bb->il.rtl->global_live_at_start,
			         &cleared_regs);
	      AND_COMPL_REG_SET (bb->il.rtl->global_live_at_end,
			         &cleared_regs);
	    }
	}
      else
	{
	  reg_set_iterator rsi;
	  EXECUTE_IF_SET_IN_REG_SET (&cleared_regs, 0, j, rsi)
	    {
	      FOR_EACH_BB (bb)
		{
		  CLEAR_REGNO_REG_SET (bb->il.rtl->global_live_at_start, j);
		  CLEAR_REGNO_REG_SET (bb->il.rtl->global_live_at_end, j);
		}
	    }
	}
    }

  out:
  /* Clean up.  */
  end_alias_analysis ();
  CLEAR_REG_SET (&cleared_regs);
  free (reg_equiv);
}

/* Mark REG as having no known equivalence.
   Some instructions might have been processed before and furnished
   with REG_EQUIV notes for this register; these notes will have to be
   removed.
   STORE is the piece of RTL that does the non-constant / conflicting
   assignment - a SET, CLOBBER or REG_INC note.  It is currently not used,
   but needs to be there because this function is called from note_stores.  */
static void
no_equiv (rtx reg, rtx store ATTRIBUTE_UNUSED, void *data ATTRIBUTE_UNUSED)
{
  int regno;
  rtx list;

  if (!REG_P (reg))
    return;
  regno = REGNO (reg);
  list = reg_equiv[regno].init_insns;
  if (list == const0_rtx)
    return;
  reg_equiv[regno].init_insns = const0_rtx;
  reg_equiv[regno].replacement = NULL_RTX;
  /* This doesn't matter for equivalences made for argument registers, we
     should keep their initialization insns.  */
  if (reg_equiv[regno].is_arg_equivalence)
    return;
  reg_equiv_init[regno] = NULL_RTX;
  for (; list; list =  XEXP (list, 1))
    {
      rtx insn = XEXP (list, 0);
      remove_note (insn, find_reg_note (insn, REG_EQUIV, NULL_RTX));
    }
}

/* Allocate hard regs to the pseudo regs used only within block number B.
   Only the pseudos that die but once can be handled.  */

static void
block_alloc (int b)
{
  int i, q;
  rtx insn;
  rtx note, hard_reg;
  int insn_number = 0;
  int insn_count = 0;
  int max_uid = get_max_uid ();
  int *qty_order;
  int no_conflict_combined_regno = -1;

  /* Count the instructions in the basic block.  */

  insn = BB_END (BASIC_BLOCK (b));
  while (1)
    {
      if (!NOTE_P (insn))
	{
	  ++insn_count;
	  gcc_assert (insn_count <= max_uid);
	}
      if (insn == BB_HEAD (BASIC_BLOCK (b)))
	break;
      insn = PREV_INSN (insn);
    }

  /* +2 to leave room for a post_mark_life at the last insn and for
     the birth of a CLOBBER in the first insn.  */
  regs_live_at = XCNEWVEC (HARD_REG_SET, 2 * insn_count + 2);

  /* Initialize table of hardware registers currently live.  */

  REG_SET_TO_HARD_REG_SET (regs_live,
		  	   BASIC_BLOCK (b)->il.rtl->global_live_at_start);

  /* This loop scans the instructions of the basic block
     and assigns quantities to registers.
     It computes which registers to tie.  */

  insn = BB_HEAD (BASIC_BLOCK (b));
  while (1)
    {
      if (!NOTE_P (insn))
	insn_number++;

      if (INSN_P (insn))
	{
	  rtx link, set;
	  int win = 0;
	  rtx r0, r1 = NULL_RTX;
	  int combined_regno = -1;
	  int i;

	  this_insn_number = insn_number;
	  this_insn = insn;

	  extract_insn (insn);
	  which_alternative = -1;

	  /* Is this insn suitable for tying two registers?
	     If so, try doing that.
	     Suitable insns are those with at least two operands and where
	     operand 0 is an output that is a register that is not
	     earlyclobber.

	     We can tie operand 0 with some operand that dies in this insn.
	     First look for operands that are required to be in the same
	     register as operand 0.  If we find such, only try tying that
	     operand or one that can be put into that operand if the
	     operation is commutative.  If we don't find an operand
	     that is required to be in the same register as operand 0,
	     we can tie with any operand.

	     Subregs in place of regs are also ok.

	     If tying is done, WIN is set nonzero.  */

	  if (optimize
	      && recog_data.n_operands > 1
	      && recog_data.constraints[0][0] == '='
	      && recog_data.constraints[0][1] != '&')
	    {
	      /* If non-negative, is an operand that must match operand 0.  */
	      int must_match_0 = -1;
	      /* Counts number of alternatives that require a match with
		 operand 0.  */
	      int n_matching_alts = 0;

	      for (i = 1; i < recog_data.n_operands; i++)
		{
		  const char *p = recog_data.constraints[i];
		  int this_match = requires_inout (p);

		  n_matching_alts += this_match;
		  if (this_match == recog_data.n_alternatives)
		    must_match_0 = i;
		}

	      r0 = recog_data.operand[0];
	      for (i = 1; i < recog_data.n_operands; i++)
		{
		  /* Skip this operand if we found an operand that
		     must match operand 0 and this operand isn't it
		     and can't be made to be it by commutativity.  */

		  if (must_match_0 >= 0 && i != must_match_0
		      && ! (i == must_match_0 + 1
			    && recog_data.constraints[i-1][0] == '%')
		      && ! (i == must_match_0 - 1
			    && recog_data.constraints[i][0] == '%'))
		    continue;

		  /* Likewise if each alternative has some operand that
		     must match operand zero.  In that case, skip any
		     operand that doesn't list operand 0 since we know that
		     the operand always conflicts with operand 0.  We
		     ignore commutativity in this case to keep things simple.  */
		  if (n_matching_alts == recog_data.n_alternatives
		      && 0 == requires_inout (recog_data.constraints[i]))
		    continue;

		  r1 = recog_data.operand[i];

		  /* If the operand is an address, find a register in it.
		     There may be more than one register, but we only try one
		     of them.  */
		  if (recog_data.constraints[i][0] == 'p'
		      || EXTRA_ADDRESS_CONSTRAINT (recog_data.constraints[i][0],
						   recog_data.constraints[i]))
		    while (GET_CODE (r1) == PLUS || GET_CODE (r1) == MULT)
		      r1 = XEXP (r1, 0);

		  /* Avoid making a call-saved register unnecessarily
                     clobbered.  */
		  hard_reg = get_hard_reg_initial_reg (cfun, r1);
		  if (hard_reg != NULL_RTX)
		    {
		      if (REG_P (hard_reg)
			  && REGNO (hard_reg) < FIRST_PSEUDO_REGISTER
			  && !call_used_regs[REGNO (hard_reg)])
			continue;
		    }

		  if (REG_P (r0) || GET_CODE (r0) == SUBREG)
		    {
		      /* We have two priorities for hard register preferences.
			 If we have a move insn or an insn whose first input
			 can only be in the same register as the output, give
			 priority to an equivalence found from that insn.  */
		      int may_save_copy
			= (r1 == recog_data.operand[i] && must_match_0 >= 0);

		      if (REG_P (r1) || GET_CODE (r1) == SUBREG)
			win = combine_regs (r1, r0, may_save_copy,
					    insn_number, insn, 0);
		    }
		  if (win)
		    break;
		}
	    }

	  /* Recognize an insn sequence with an ultimate result
	     which can safely overlap one of the inputs.
	     The sequence begins with a CLOBBER of its result,
	     and ends with an insn that copies the result to itself
	     and has a REG_EQUAL note for an equivalent formula.
	     That note indicates what the inputs are.
	     The result and the input can overlap if each insn in
	     the sequence either doesn't mention the input
	     or has a REG_NO_CONFLICT note to inhibit the conflict.

	     We do the combining test at the CLOBBER so that the
	     destination register won't have had a quantity number
	     assigned, since that would prevent combining.  */

	  if (optimize
	      && GET_CODE (PATTERN (insn)) == CLOBBER
	      && (r0 = XEXP (PATTERN (insn), 0),
		  REG_P (r0))
	      && (link = find_reg_note (insn, REG_LIBCALL, NULL_RTX)) != 0
	      && XEXP (link, 0) != 0
	      && NONJUMP_INSN_P (XEXP (link, 0))
	      && (set = single_set (XEXP (link, 0))) != 0
	      && SET_DEST (set) == r0 && SET_SRC (set) == r0
	      && (note = find_reg_note (XEXP (link, 0), REG_EQUAL,
					NULL_RTX)) != 0)
	    {
	      if (r1 = XEXP (note, 0), REG_P (r1)
		  /* Check that we have such a sequence.  */
		  && no_conflict_p (insn, r0, r1))
		win = combine_regs (r1, r0, 1, insn_number, insn, 1);
	      else if (GET_RTX_FORMAT (GET_CODE (XEXP (note, 0)))[0] == 'e'
		       && (r1 = XEXP (XEXP (note, 0), 0),
			   REG_P (r1) || GET_CODE (r1) == SUBREG)
		       && no_conflict_p (insn, r0, r1))
		win = combine_regs (r1, r0, 0, insn_number, insn, 1);

	      /* Here we care if the operation to be computed is
		 commutative.  */
	      else if (COMMUTATIVE_P (XEXP (note, 0))
		       && (r1 = XEXP (XEXP (note, 0), 1),
			   (REG_P (r1) || GET_CODE (r1) == SUBREG))
		       && no_conflict_p (insn, r0, r1))
		win = combine_regs (r1, r0, 0, insn_number, insn, 1);

	      /* If we did combine something, show the register number
		 in question so that we know to ignore its death.  */
	      if (win)
		no_conflict_combined_regno = REGNO (r1);
	    }

	  /* If registers were just tied, set COMBINED_REGNO
	     to the number of the register used in this insn
	     that was tied to the register set in this insn.
	     This register's qty should not be "killed".  */

	  if (win)
	    {
	      while (GET_CODE (r1) == SUBREG)
		r1 = SUBREG_REG (r1);
	      combined_regno = REGNO (r1);
	    }

	  /* Mark the death of everything that dies in this instruction,
	     except for anything that was just combined.  */

	  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
	    if (REG_NOTE_KIND (link) == REG_DEAD
		&& REG_P (XEXP (link, 0))
		&& combined_regno != (int) REGNO (XEXP (link, 0))
		&& (no_conflict_combined_regno != (int) REGNO (XEXP (link, 0))
		    || ! find_reg_note (insn, REG_NO_CONFLICT,
					XEXP (link, 0))))
	      wipe_dead_reg (XEXP (link, 0), 0);

	  /* Allocate qty numbers for all registers local to this block
	     that are born (set) in this instruction.
	     A pseudo that already has a qty is not changed.  */

	  note_stores (PATTERN (insn), reg_is_set, NULL);

	  /* If anything is set in this insn and then unused, mark it as dying
	     after this insn, so it will conflict with our outputs.  This
	     can't match with something that combined, and it doesn't matter
	     if it did.  Do this after the calls to reg_is_set since these
	     die after, not during, the current insn.  */

	  for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
	    if (REG_NOTE_KIND (link) == REG_UNUSED
		&& REG_P (XEXP (link, 0)))
	      wipe_dead_reg (XEXP (link, 0), 1);

	  /* If this is an insn that has a REG_RETVAL note pointing at a
	     CLOBBER insn, we have reached the end of a REG_NO_CONFLICT
	     block, so clear any register number that combined within it.  */
	  if ((note = find_reg_note (insn, REG_RETVAL, NULL_RTX)) != 0
	      && NONJUMP_INSN_P (XEXP (note, 0))
	      && GET_CODE (PATTERN (XEXP (note, 0))) == CLOBBER)
	    no_conflict_combined_regno = -1;
	}

      /* Set the registers live after INSN_NUMBER.  Note that we never
	 record the registers live before the block's first insn, since no
	 pseudos we care about are live before that insn.  */

      IOR_HARD_REG_SET (regs_live_at[2 * insn_number], regs_live);
      IOR_HARD_REG_SET (regs_live_at[2 * insn_number + 1], regs_live);

      if (insn == BB_END (BASIC_BLOCK (b)))
	break;

      insn = NEXT_INSN (insn);
    }

  /* Now every register that is local to this basic block
     should have been given a quantity, or else -1 meaning ignore it.
     Every quantity should have a known birth and death.

     Order the qtys so we assign them registers in order of the
     number of suggested registers they need so we allocate those with
     the most restrictive needs first.  */

  qty_order = XNEWVEC (int, next_qty);
  for (i = 0; i < next_qty; i++)
    qty_order[i] = i;

#define EXCHANGE(I1, I2)  \
  { i = qty_order[I1]; qty_order[I1] = qty_order[I2]; qty_order[I2] = i; }

  switch (next_qty)
    {
    case 3:
      /* Make qty_order[2] be the one to allocate last.  */
      if (qty_sugg_compare (0, 1) > 0)
	EXCHANGE (0, 1);
      if (qty_sugg_compare (1, 2) > 0)
	EXCHANGE (2, 1);

      /* ... Fall through ...  */
    case 2:
      /* Put the best one to allocate in qty_order[0].  */
      if (qty_sugg_compare (0, 1) > 0)
	EXCHANGE (0, 1);

      /* ... Fall through ...  */

    case 1:
    case 0:
      /* Nothing to do here.  */
      break;

    default:
      qsort (qty_order, next_qty, sizeof (int), qty_sugg_compare_1);
    }

  /* Try to put each quantity in a suggested physical register, if it has one.
     This may cause registers to be allocated that otherwise wouldn't be, but
     this seems acceptable in local allocation (unlike global allocation).  */
  for (i = 0; i < next_qty; i++)
    {
      q = qty_order[i];
      if (qty_phys_num_sugg[q] != 0 || qty_phys_num_copy_sugg[q] != 0)
	qty[q].phys_reg = find_free_reg (qty[q].min_class, qty[q].mode, q,
					 0, 1, qty[q].birth, qty[q].death);
      else
	qty[q].phys_reg = -1;
    }

  /* Order the qtys so we assign them registers in order of
     decreasing length of life.  Normally call qsort, but if we
     have only a very small number of quantities, sort them ourselves.  */

  for (i = 0; i < next_qty; i++)
    qty_order[i] = i;

#define EXCHANGE(I1, I2)  \
  { i = qty_order[I1]; qty_order[I1] = qty_order[I2]; qty_order[I2] = i; }

  switch (next_qty)
    {
    case 3:
      /* Make qty_order[2] be the one to allocate last.  */
      if (qty_compare (0, 1) > 0)
	EXCHANGE (0, 1);
      if (qty_compare (1, 2) > 0)
	EXCHANGE (2, 1);

      /* ... Fall through ...  */
    case 2:
      /* Put the best one to allocate in qty_order[0].  */
      if (qty_compare (0, 1) > 0)
	EXCHANGE (0, 1);

      /* ... Fall through ...  */

    case 1:
    case 0:
      /* Nothing to do here.  */
      break;

    default:
      qsort (qty_order, next_qty, sizeof (int), qty_compare_1);
    }

  /* Now for each qty that is not a hardware register,
     look for a hardware register to put it in.
     First try the register class that is cheapest for this qty,
     if there is more than one class.  */

  for (i = 0; i < next_qty; i++)
    {
      q = qty_order[i];
      if (qty[q].phys_reg < 0)
	{
#ifdef INSN_SCHEDULING
	  /* These values represent the adjusted lifetime of a qty so
	     that it conflicts with qtys which appear near the start/end
	     of this qty's lifetime.

	     The purpose behind extending the lifetime of this qty is to
	     discourage the register allocator from creating false
	     dependencies.

	     The adjustment value is chosen to indicate that this qty
	     conflicts with all the qtys in the instructions immediately
	     before and after the lifetime of this qty.

	     Experiments have shown that higher values tend to hurt
	     overall code performance.

	     If allocation using the extended lifetime fails we will try
	     again with the qty's unadjusted lifetime.  */
	  int fake_birth = MAX (0, qty[q].birth - 2 + qty[q].birth % 2);
	  int fake_death = MIN (insn_number * 2 + 1,
				qty[q].death + 2 - qty[q].death % 2);
#endif

	  if (N_REG_CLASSES > 1)
	    {
#ifdef INSN_SCHEDULING
	      /* We try to avoid using hard registers allocated to qtys which
		 are born immediately after this qty or die immediately before
		 this qty.

		 This optimization is only appropriate when we will run
		 a scheduling pass after reload and we are not optimizing
		 for code size.  */
	      if (flag_schedule_insns_after_reload
		  && !optimize_size
		  && !SMALL_REGISTER_CLASSES)
		{
		  qty[q].phys_reg = find_free_reg (qty[q].min_class,
						   qty[q].mode, q, 0, 0,
						   fake_birth, fake_death);
		  if (qty[q].phys_reg >= 0)
		    continue;
		}
#endif
	      qty[q].phys_reg = find_free_reg (qty[q].min_class,
					       qty[q].mode, q, 0, 0,
					       qty[q].birth, qty[q].death);
	      if (qty[q].phys_reg >= 0)
		continue;
	    }

#ifdef INSN_SCHEDULING
	  /* Similarly, avoid false dependencies.  */
	  if (flag_schedule_insns_after_reload
	      && !optimize_size
	      && !SMALL_REGISTER_CLASSES
	      && qty[q].alternate_class != NO_REGS)
	    qty[q].phys_reg = find_free_reg (qty[q].alternate_class,
					     qty[q].mode, q, 0, 0,
					     fake_birth, fake_death);
#endif
	  if (qty[q].alternate_class != NO_REGS)
	    qty[q].phys_reg = find_free_reg (qty[q].alternate_class,
					     qty[q].mode, q, 0, 0,
					     qty[q].birth, qty[q].death);
	}
    }

  /* Now propagate the register assignments
     to the pseudo regs belonging to the qtys.  */

  for (q = 0; q < next_qty; q++)
    if (qty[q].phys_reg >= 0)
      {
	for (i = qty[q].first_reg; i >= 0; i = reg_next_in_qty[i])
	  reg_renumber[i] = qty[q].phys_reg + reg_offset[i];
      }

  /* Clean up.  */
  free (regs_live_at);
  free (qty_order);
}

/* Compare two quantities' priority for getting real registers.
   We give shorter-lived quantities higher priority.
   Quantities with more references are also preferred, as are quantities that
   require multiple registers.  This is the identical prioritization as
   done by global-alloc.

   We used to give preference to registers with *longer* lives, but using
   the same algorithm in both local- and global-alloc can speed up execution
   of some programs by as much as a factor of three!  */

/* Note that the quotient will never be bigger than
   the value of floor_log2 times the maximum number of
   times a register can occur in one insn (surely less than 100)
   weighted by frequency (max REG_FREQ_MAX).
   Multiplying this by 10000/REG_FREQ_MAX can't overflow.
   QTY_CMP_PRI is also used by qty_sugg_compare.  */

#define QTY_CMP_PRI(q)		\
  ((int) (((double) (floor_log2 (qty[q].n_refs) * qty[q].freq * qty[q].size) \
	  / (qty[q].death - qty[q].birth)) * (10000 / REG_FREQ_MAX)))

static int
qty_compare (int q1, int q2)
{
  return QTY_CMP_PRI (q2) - QTY_CMP_PRI (q1);
}

static int
qty_compare_1 (const void *q1p, const void *q2p)
{
  int q1 = *(const int *) q1p, q2 = *(const int *) q2p;
  int tem = QTY_CMP_PRI (q2) - QTY_CMP_PRI (q1);

  if (tem != 0)
    return tem;

  /* If qtys are equally good, sort by qty number,
     so that the results of qsort leave nothing to chance.  */
  return q1 - q2;
}

/* Compare two quantities' priority for getting real registers.  This version
   is called for quantities that have suggested hard registers.  First priority
   goes to quantities that have copy preferences, then to those that have
   normal preferences.  Within those groups, quantities with the lower
   number of preferences have the highest priority.  Of those, we use the same
   algorithm as above.  */

#define QTY_CMP_SUGG(q)		\
  (qty_phys_num_copy_sugg[q]		\
    ? qty_phys_num_copy_sugg[q]	\
    : qty_phys_num_sugg[q] * FIRST_PSEUDO_REGISTER)

static int
qty_sugg_compare (int q1, int q2)
{
  int tem = QTY_CMP_SUGG (q1) - QTY_CMP_SUGG (q2);

  if (tem != 0)
    return tem;

  return QTY_CMP_PRI (q2) - QTY_CMP_PRI (q1);
}

static int
qty_sugg_compare_1 (const void *q1p, const void *q2p)
{
  int q1 = *(const int *) q1p, q2 = *(const int *) q2p;
  int tem = QTY_CMP_SUGG (q1) - QTY_CMP_SUGG (q2);

  if (tem != 0)
    return tem;

  tem = QTY_CMP_PRI (q2) - QTY_CMP_PRI (q1);
  if (tem != 0)
    return tem;

  /* If qtys are equally good, sort by qty number,
     so that the results of qsort leave nothing to chance.  */
  return q1 - q2;
}

#undef QTY_CMP_SUGG
#undef QTY_CMP_PRI

/* Attempt to combine the two registers (rtx's) USEDREG and SETREG.
   Returns 1 if have done so, or 0 if cannot.

   Combining registers means marking them as having the same quantity
   and adjusting the offsets within the quantity if either of
   them is a SUBREG.

   We don't actually combine a hard reg with a pseudo; instead
   we just record the hard reg as the suggestion for the pseudo's quantity.
   If we really combined them, we could lose if the pseudo lives
   across an insn that clobbers the hard reg (eg, movmem).

   ALREADY_DEAD is nonzero if USEDREG is known to be dead even though
   there is no REG_DEAD note on INSN.  This occurs during the processing
   of REG_NO_CONFLICT blocks.

   MAY_SAVE_COPY is nonzero if this insn is simply copying USEDREG to
   SETREG or if the input and output must share a register.
   In that case, we record a hard reg suggestion in QTY_PHYS_COPY_SUGG.

   There are elaborate checks for the validity of combining.  */

static int
combine_regs (rtx usedreg, rtx setreg, int may_save_copy, int insn_number,
	      rtx insn, int already_dead)
{
  int ureg, sreg;
  int offset = 0;
  int usize, ssize;
  int sqty;

  /* Determine the numbers and sizes of registers being used.  If a subreg
     is present that does not change the entire register, don't consider
     this a copy insn.  */

  while (GET_CODE (usedreg) == SUBREG)
    {
      rtx subreg = SUBREG_REG (usedreg);

      if (REG_P (subreg))
	{
	  if (GET_MODE_SIZE (GET_MODE (subreg)) > UNITS_PER_WORD)
	    may_save_copy = 0;

	  if (REGNO (subreg) < FIRST_PSEUDO_REGISTER)
	    offset += subreg_regno_offset (REGNO (subreg),
					   GET_MODE (subreg),
					   SUBREG_BYTE (usedreg),
					   GET_MODE (usedreg));
	  else
	    offset += (SUBREG_BYTE (usedreg)
		      / REGMODE_NATURAL_SIZE (GET_MODE (usedreg)));
	}

      usedreg = subreg;
    }

  if (!REG_P (usedreg))
    return 0;

  ureg = REGNO (usedreg);
  if (ureg < FIRST_PSEUDO_REGISTER)
    usize = hard_regno_nregs[ureg][GET_MODE (usedreg)];
  else
    usize = ((GET_MODE_SIZE (GET_MODE (usedreg))
	      + (REGMODE_NATURAL_SIZE (GET_MODE (usedreg)) - 1))
	     / REGMODE_NATURAL_SIZE (GET_MODE (usedreg)));

  while (GET_CODE (setreg) == SUBREG)
    {
      rtx subreg = SUBREG_REG (setreg);

      if (REG_P (subreg))
	{
	  if (GET_MODE_SIZE (GET_MODE (subreg)) > UNITS_PER_WORD)
	    may_save_copy = 0;

	  if (REGNO (subreg) < FIRST_PSEUDO_REGISTER)
	    offset -= subreg_regno_offset (REGNO (subreg),
					   GET_MODE (subreg),
					   SUBREG_BYTE (setreg),
					   GET_MODE (setreg));
	  else
	    offset -= (SUBREG_BYTE (setreg)
		      / REGMODE_NATURAL_SIZE (GET_MODE (setreg)));
	}

      setreg = subreg;
    }

  if (!REG_P (setreg))
    return 0;

  sreg = REGNO (setreg);
  if (sreg < FIRST_PSEUDO_REGISTER)
    ssize = hard_regno_nregs[sreg][GET_MODE (setreg)];
  else
    ssize = ((GET_MODE_SIZE (GET_MODE (setreg))
	      + (REGMODE_NATURAL_SIZE (GET_MODE (setreg)) - 1))
	     / REGMODE_NATURAL_SIZE (GET_MODE (setreg)));

  /* If UREG is a pseudo-register that hasn't already been assigned a
     quantity number, it means that it is not local to this block or dies
     more than once.  In either event, we can't do anything with it.  */
  if ((ureg >= FIRST_PSEUDO_REGISTER && reg_qty[ureg] < 0)
      /* Do not combine registers unless one fits within the other.  */
      || (offset > 0 && usize + offset > ssize)
      || (offset < 0 && usize + offset < ssize)
      /* Do not combine with a smaller already-assigned object
	 if that smaller object is already combined with something bigger.  */
      || (ssize > usize && ureg >= FIRST_PSEUDO_REGISTER
	  && usize < qty[reg_qty[ureg]].size)
      /* Can't combine if SREG is not a register we can allocate.  */
      || (sreg >= FIRST_PSEUDO_REGISTER && reg_qty[sreg] == -1)
      /* Don't combine with a pseudo mentioned in a REG_NO_CONFLICT note.
	 These have already been taken care of.  This probably wouldn't
	 combine anyway, but don't take any chances.  */
      || (ureg >= FIRST_PSEUDO_REGISTER
	  && find_reg_note (insn, REG_NO_CONFLICT, usedreg))
      /* Don't tie something to itself.  In most cases it would make no
	 difference, but it would screw up if the reg being tied to itself
	 also dies in this insn.  */
      || ureg == sreg
      /* Don't try to connect two different hardware registers.  */
      || (ureg < FIRST_PSEUDO_REGISTER && sreg < FIRST_PSEUDO_REGISTER)
      /* Don't connect two different machine modes if they have different
	 implications as to which registers may be used.  */
      || !MODES_TIEABLE_P (GET_MODE (usedreg), GET_MODE (setreg)))
    return 0;

  /* Now, if UREG is a hard reg and SREG is a pseudo, record the hard reg in
     qty_phys_sugg for the pseudo instead of tying them.

     Return "failure" so that the lifespan of UREG is terminated here;
     that way the two lifespans will be disjoint and nothing will prevent
     the pseudo reg from being given this hard reg.  */

  if (ureg < FIRST_PSEUDO_REGISTER)
    {
      /* Allocate a quantity number so we have a place to put our
	 suggestions.  */
      if (reg_qty[sreg] == -2)
	reg_is_born (setreg, 2 * insn_number);

      if (reg_qty[sreg] >= 0)
	{
	  if (may_save_copy
	      && ! TEST_HARD_REG_BIT (qty_phys_copy_sugg[reg_qty[sreg]], ureg))
	    {
	      SET_HARD_REG_BIT (qty_phys_copy_sugg[reg_qty[sreg]], ureg);
	      qty_phys_num_copy_sugg[reg_qty[sreg]]++;
	    }
	  else if (! TEST_HARD_REG_BIT (qty_phys_sugg[reg_qty[sreg]], ureg))
	    {
	      SET_HARD_REG_BIT (qty_phys_sugg[reg_qty[sreg]], ureg);
	      qty_phys_num_sugg[reg_qty[sreg]]++;
	    }
	}
      return 0;
    }

  /* Similarly for SREG a hard register and UREG a pseudo register.  */

  if (sreg < FIRST_PSEUDO_REGISTER)
    {
      if (may_save_copy
	  && ! TEST_HARD_REG_BIT (qty_phys_copy_sugg[reg_qty[ureg]], sreg))
	{
	  SET_HARD_REG_BIT (qty_phys_copy_sugg[reg_qty[ureg]], sreg);
	  qty_phys_num_copy_sugg[reg_qty[ureg]]++;
	}
      else if (! TEST_HARD_REG_BIT (qty_phys_sugg[reg_qty[ureg]], sreg))
	{
	  SET_HARD_REG_BIT (qty_phys_sugg[reg_qty[ureg]], sreg);
	  qty_phys_num_sugg[reg_qty[ureg]]++;
	}
      return 0;
    }

  /* At this point we know that SREG and UREG are both pseudos.
     Do nothing if SREG already has a quantity or is a register that we
     don't allocate.  */
  if (reg_qty[sreg] >= -1
      /* If we are not going to let any regs live across calls,
	 don't tie a call-crossing reg to a non-call-crossing reg.  */
      || (current_function_has_nonlocal_label
	  && ((REG_N_CALLS_CROSSED (ureg) > 0)
	      != (REG_N_CALLS_CROSSED (sreg) > 0))))
    return 0;

  /* We don't already know about SREG, so tie it to UREG
     if this is the last use of UREG, provided the classes they want
     are compatible.  */

  if ((already_dead || find_regno_note (insn, REG_DEAD, ureg))
      && reg_meets_class_p (sreg, qty[reg_qty[ureg]].min_class))
    {
      /* Add SREG to UREG's quantity.  */
      sqty = reg_qty[ureg];
      reg_qty[sreg] = sqty;
      reg_offset[sreg] = reg_offset[ureg] + offset;
      reg_next_in_qty[sreg] = qty[sqty].first_reg;
      qty[sqty].first_reg = sreg;

      /* If SREG's reg class is smaller, set qty[SQTY].min_class.  */
      update_qty_class (sqty, sreg);

      /* Update info about quantity SQTY.  */
      qty[sqty].n_calls_crossed += REG_N_CALLS_CROSSED (sreg);
      qty[sqty].n_throwing_calls_crossed
	+= REG_N_THROWING_CALLS_CROSSED (sreg);
      qty[sqty].n_refs += REG_N_REFS (sreg);
      qty[sqty].freq += REG_FREQ (sreg);
      if (usize < ssize)
	{
	  int i;

	  for (i = qty[sqty].first_reg; i >= 0; i = reg_next_in_qty[i])
	    reg_offset[i] -= offset;

	  qty[sqty].size = ssize;
	  qty[sqty].mode = GET_MODE (setreg);
	}
    }
  else
    return 0;

  return 1;
}

/* Return 1 if the preferred class of REG allows it to be tied
   to a quantity or register whose class is CLASS.
   True if REG's reg class either contains or is contained in CLASS.  */

static int
reg_meets_class_p (int reg, enum reg_class class)
{
  enum reg_class rclass = reg_preferred_class (reg);
  return (reg_class_subset_p (rclass, class)
	  || reg_class_subset_p (class, rclass));
}

/* Update the class of QTYNO assuming that REG is being tied to it.  */

static void
update_qty_class (int qtyno, int reg)
{
  enum reg_class rclass = reg_preferred_class (reg);
  if (reg_class_subset_p (rclass, qty[qtyno].min_class))
    qty[qtyno].min_class = rclass;

  rclass = reg_alternate_class (reg);
  if (reg_class_subset_p (rclass, qty[qtyno].alternate_class))
    qty[qtyno].alternate_class = rclass;
}

/* Handle something which alters the value of an rtx REG.

   REG is whatever is set or clobbered.  SETTER is the rtx that
   is modifying the register.

   If it is not really a register, we do nothing.
   The file-global variables `this_insn' and `this_insn_number'
   carry info from `block_alloc'.  */

static void
reg_is_set (rtx reg, rtx setter, void *data ATTRIBUTE_UNUSED)
{
  /* Note that note_stores will only pass us a SUBREG if it is a SUBREG of
     a hard register.  These may actually not exist any more.  */

  if (GET_CODE (reg) != SUBREG
      && !REG_P (reg))
    return;

  /* Mark this register as being born.  If it is used in a CLOBBER, mark
     it as being born halfway between the previous insn and this insn so that
     it conflicts with our inputs but not the outputs of the previous insn.  */

  reg_is_born (reg, 2 * this_insn_number - (GET_CODE (setter) == CLOBBER));
}

/* Handle beginning of the life of register REG.
   BIRTH is the index at which this is happening.  */

static void
reg_is_born (rtx reg, int birth)
{
  int regno;

  if (GET_CODE (reg) == SUBREG)
    {
      regno = REGNO (SUBREG_REG (reg));
      if (regno < FIRST_PSEUDO_REGISTER)
	regno = subreg_regno (reg);
    }
  else
    regno = REGNO (reg);

  if (regno < FIRST_PSEUDO_REGISTER)
    {
      mark_life (regno, GET_MODE (reg), 1);

      /* If the register was to have been born earlier that the present
	 insn, mark it as live where it is actually born.  */
      if (birth < 2 * this_insn_number)
	post_mark_life (regno, GET_MODE (reg), 1, birth, 2 * this_insn_number);
    }
  else
    {
      if (reg_qty[regno] == -2)
	alloc_qty (regno, GET_MODE (reg), PSEUDO_REGNO_SIZE (regno), birth);

      /* If this register has a quantity number, show that it isn't dead.  */
      if (reg_qty[regno] >= 0)
	qty[reg_qty[regno]].death = -1;
    }
}

/* Record the death of REG in the current insn.  If OUTPUT_P is nonzero,
   REG is an output that is dying (i.e., it is never used), otherwise it
   is an input (the normal case).
   If OUTPUT_P is 1, then we extend the life past the end of this insn.  */

static void
wipe_dead_reg (rtx reg, int output_p)
{
  int regno = REGNO (reg);

  /* If this insn has multiple results,
     and the dead reg is used in one of the results,
     extend its life to after this insn,
     so it won't get allocated together with any other result of this insn.

     It is unsafe to use !single_set here since it will ignore an unused
     output.  Just because an output is unused does not mean the compiler
     can assume the side effect will not occur.   Consider if REG appears
     in the address of an output and we reload the output.  If we allocate
     REG to the same hard register as an unused output we could set the hard
     register before the output reload insn.  */
  if (GET_CODE (PATTERN (this_insn)) == PARALLEL
      && multiple_sets (this_insn))
    {
      int i;
      for (i = XVECLEN (PATTERN (this_insn), 0) - 1; i >= 0; i--)
	{
	  rtx set = XVECEXP (PATTERN (this_insn), 0, i);
	  if (GET_CODE (set) == SET
	      && !REG_P (SET_DEST (set))
	      && !rtx_equal_p (reg, SET_DEST (set))
	      && reg_overlap_mentioned_p (reg, SET_DEST (set)))
	    output_p = 1;
	}
    }

  /* If this register is used in an auto-increment address, then extend its
     life to after this insn, so that it won't get allocated together with
     the result of this insn.  */
  if (! output_p && find_regno_note (this_insn, REG_INC, regno))
    output_p = 1;

  if (regno < FIRST_PSEUDO_REGISTER)
    {
      mark_life (regno, GET_MODE (reg), 0);

      /* If a hard register is dying as an output, mark it as in use at
	 the beginning of this insn (the above statement would cause this
	 not to happen).  */
      if (output_p)
	post_mark_life (regno, GET_MODE (reg), 1,
			2 * this_insn_number, 2 * this_insn_number + 1);
    }

  else if (reg_qty[regno] >= 0)
    qty[reg_qty[regno]].death = 2 * this_insn_number + output_p;
}

/* Find a block of SIZE words of hard regs in reg_class CLASS
   that can hold something of machine-mode MODE
     (but actually we test only the first of the block for holding MODE)
   and still free between insn BORN_INDEX and insn DEAD_INDEX,
   and return the number of the first of them.
   Return -1 if such a block cannot be found.
   If QTYNO crosses calls, insist on a register preserved by calls,
   unless ACCEPT_CALL_CLOBBERED is nonzero.

   If JUST_TRY_SUGGESTED is nonzero, only try to see if the suggested
   register is available.  If not, return -1.  */

static int
find_free_reg (enum reg_class class, enum machine_mode mode, int qtyno,
	       int accept_call_clobbered, int just_try_suggested,
	       int born_index, int dead_index)
{
  int i, ins;
  HARD_REG_SET first_used, used;
#ifdef ELIMINABLE_REGS
  static const struct {const int from, to; } eliminables[] = ELIMINABLE_REGS;
#endif

  /* Validate our parameters.  */
  gcc_assert (born_index >= 0 && born_index <= dead_index);

  /* Don't let a pseudo live in a reg across a function call
     if we might get a nonlocal goto.  */
  if (current_function_has_nonlocal_label
      && qty[qtyno].n_calls_crossed > 0)
    return -1;

  if (accept_call_clobbered)
    COPY_HARD_REG_SET (used, call_fixed_reg_set);
  else if (qty[qtyno].n_calls_crossed == 0)
    COPY_HARD_REG_SET (used, fixed_reg_set);
  else
    COPY_HARD_REG_SET (used, call_used_reg_set);

  if (accept_call_clobbered)
    IOR_HARD_REG_SET (used, losing_caller_save_reg_set);

  for (ins = born_index; ins < dead_index; ins++)
    IOR_HARD_REG_SET (used, regs_live_at[ins]);

  IOR_COMPL_HARD_REG_SET (used, reg_class_contents[(int) class]);

  /* Don't use the frame pointer reg in local-alloc even if
     we may omit the frame pointer, because if we do that and then we
     need a frame pointer, reload won't know how to move the pseudo
     to another hard reg.  It can move only regs made by global-alloc.

     This is true of any register that can be eliminated.  */
#ifdef ELIMINABLE_REGS
  for (i = 0; i < (int) ARRAY_SIZE (eliminables); i++)
    SET_HARD_REG_BIT (used, eliminables[i].from);
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
  /* If FRAME_POINTER_REGNUM is not a real register, then protect the one
     that it might be eliminated into.  */
  SET_HARD_REG_BIT (used, HARD_FRAME_POINTER_REGNUM);
#endif
#else
  SET_HARD_REG_BIT (used, FRAME_POINTER_REGNUM);
#endif

#ifdef CANNOT_CHANGE_MODE_CLASS
  cannot_change_mode_set_regs (&used, mode, qty[qtyno].first_reg);
#endif

  /* Normally, the registers that can be used for the first register in
     a multi-register quantity are the same as those that can be used for
     subsequent registers.  However, if just trying suggested registers,
     restrict our consideration to them.  If there are copy-suggested
     register, try them.  Otherwise, try the arithmetic-suggested
     registers.  */
  COPY_HARD_REG_SET (first_used, used);

  if (just_try_suggested)
    {
      if (qty_phys_num_copy_sugg[qtyno] != 0)
	IOR_COMPL_HARD_REG_SET (first_used, qty_phys_copy_sugg[qtyno]);
      else
	IOR_COMPL_HARD_REG_SET (first_used, qty_phys_sugg[qtyno]);
    }

  /* If all registers are excluded, we can't do anything.  */
  GO_IF_HARD_REG_SUBSET (reg_class_contents[(int) ALL_REGS], first_used, fail);

  /* If at least one would be suitable, test each hard reg.  */

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
#ifdef REG_ALLOC_ORDER
      int regno = reg_alloc_order[i];
#else
      int regno = i;
#endif
      if (! TEST_HARD_REG_BIT (first_used, regno)
	  && HARD_REGNO_MODE_OK (regno, mode)
	  && (qty[qtyno].n_calls_crossed == 0
	      || accept_call_clobbered
	      || ! HARD_REGNO_CALL_PART_CLOBBERED (regno, mode)))
	{
	  int j;
	  int size1 = hard_regno_nregs[regno][mode];
	  for (j = 1; j < size1 && ! TEST_HARD_REG_BIT (used, regno + j); j++);
	  if (j == size1)
	    {
	      /* Mark that this register is in use between its birth and death
		 insns.  */
	      post_mark_life (regno, mode, 1, born_index, dead_index);
	      return regno;
	    }
#ifndef REG_ALLOC_ORDER
	  /* Skip starting points we know will lose.  */
	  i += j;
#endif
	}
    }

 fail:
  /* If we are just trying suggested register, we have just tried copy-
     suggested registers, and there are arithmetic-suggested registers,
     try them.  */

  /* If it would be profitable to allocate a call-clobbered register
     and save and restore it around calls, do that.  */
  if (just_try_suggested && qty_phys_num_copy_sugg[qtyno] != 0
      && qty_phys_num_sugg[qtyno] != 0)
    {
      /* Don't try the copy-suggested regs again.  */
      qty_phys_num_copy_sugg[qtyno] = 0;
      return find_free_reg (class, mode, qtyno, accept_call_clobbered, 1,
			    born_index, dead_index);
    }

  /* We need not check to see if the current function has nonlocal
     labels because we don't put any pseudos that are live over calls in
     registers in that case.  Avoid putting pseudos crossing calls that
     might throw into call used registers.  */

  if (! accept_call_clobbered
      && flag_caller_saves
      && ! just_try_suggested
      && qty[qtyno].n_calls_crossed != 0
      && qty[qtyno].n_throwing_calls_crossed == 0
      && CALLER_SAVE_PROFITABLE (qty[qtyno].n_refs,
				 qty[qtyno].n_calls_crossed))
    {
      i = find_free_reg (class, mode, qtyno, 1, 0, born_index, dead_index);
      if (i >= 0)
	caller_save_needed = 1;
      return i;
    }
  return -1;
}

/* Mark that REGNO with machine-mode MODE is live starting from the current
   insn (if LIFE is nonzero) or dead starting at the current insn (if LIFE
   is zero).  */

static void
mark_life (int regno, enum machine_mode mode, int life)
{
  int j = hard_regno_nregs[regno][mode];
  if (life)
    while (--j >= 0)
      SET_HARD_REG_BIT (regs_live, regno + j);
  else
    while (--j >= 0)
      CLEAR_HARD_REG_BIT (regs_live, regno + j);
}

/* Mark register number REGNO (with machine-mode MODE) as live (if LIFE
   is nonzero) or dead (if LIFE is zero) from insn number BIRTH (inclusive)
   to insn number DEATH (exclusive).  */

static void
post_mark_life (int regno, enum machine_mode mode, int life, int birth,
		int death)
{
  int j = hard_regno_nregs[regno][mode];
  HARD_REG_SET this_reg;

  CLEAR_HARD_REG_SET (this_reg);
  while (--j >= 0)
    SET_HARD_REG_BIT (this_reg, regno + j);

  if (life)
    while (birth < death)
      {
	IOR_HARD_REG_SET (regs_live_at[birth], this_reg);
	birth++;
      }
  else
    while (birth < death)
      {
	AND_COMPL_HARD_REG_SET (regs_live_at[birth], this_reg);
	birth++;
      }
}

/* INSN is the CLOBBER insn that starts a REG_NO_NOCONFLICT block, R0
   is the register being clobbered, and R1 is a register being used in
   the equivalent expression.

   If R1 dies in the block and has a REG_NO_CONFLICT note on every insn
   in which it is used, return 1.

   Otherwise, return 0.  */

static int
no_conflict_p (rtx insn, rtx r0 ATTRIBUTE_UNUSED, rtx r1)
{
  int ok = 0;
  rtx note = find_reg_note (insn, REG_LIBCALL, NULL_RTX);
  rtx p, last;

  /* If R1 is a hard register, return 0 since we handle this case
     when we scan the insns that actually use it.  */

  if (note == 0
      || (REG_P (r1) && REGNO (r1) < FIRST_PSEUDO_REGISTER)
      || (GET_CODE (r1) == SUBREG && REG_P (SUBREG_REG (r1))
	  && REGNO (SUBREG_REG (r1)) < FIRST_PSEUDO_REGISTER))
    return 0;

  last = XEXP (note, 0);

  for (p = NEXT_INSN (insn); p && p != last; p = NEXT_INSN (p))
    if (INSN_P (p))
      {
	if (find_reg_note (p, REG_DEAD, r1))
	  ok = 1;

	/* There must be a REG_NO_CONFLICT note on every insn, otherwise
	   some earlier optimization pass has inserted instructions into
	   the sequence, and it is not safe to perform this optimization.
	   Note that emit_no_conflict_block always ensures that this is
	   true when these sequences are created.  */
	if (! find_reg_note (p, REG_NO_CONFLICT, r1))
	  return 0;
      }

  return ok;
}

/* Return the number of alternatives for which the constraint string P
   indicates that the operand must be equal to operand 0 and that no register
   is acceptable.  */

static int
requires_inout (const char *p)
{
  char c;
  int found_zero = 0;
  int reg_allowed = 0;
  int num_matching_alts = 0;
  int len;

  for ( ; (c = *p); p += len)
    {
      len = CONSTRAINT_LEN (c, p);
      switch (c)
	{
	case '=':  case '+':  case '?':
	case '#':  case '&':  case '!':
	case '*':  case '%':
	case 'm':  case '<':  case '>':  case 'V':  case 'o':
	case 'E':  case 'F':  case 'G':  case 'H':
	case 's':  case 'i':  case 'n':
	case 'I':  case 'J':  case 'K':  case 'L':
	case 'M':  case 'N':  case 'O':  case 'P':
	case 'X':
	  /* These don't say anything we care about.  */
	  break;

	case ',':
	  if (found_zero && ! reg_allowed)
	    num_matching_alts++;

	  found_zero = reg_allowed = 0;
	  break;

	case '0':
	  found_zero = 1;
	  break;

	case '1':  case '2':  case '3':  case '4': case '5':
	case '6':  case '7':  case '8':  case '9':
	  /* Skip the balance of the matching constraint.  */
	  do
	    p++;
	  while (ISDIGIT (*p));
	  len = 0;
	  break;

	default:
	  if (REG_CLASS_FROM_CONSTRAINT (c, p) == NO_REGS
	      && !EXTRA_ADDRESS_CONSTRAINT (c, p))
	    break;
	  /* Fall through.  */
	case 'p':
	case 'g': case 'r':
	  reg_allowed = 1;
	  break;
	}
    }

  if (found_zero && ! reg_allowed)
    num_matching_alts++;

  return num_matching_alts;
}

void
dump_local_alloc (FILE *file)
{
  int i;
  for (i = FIRST_PSEUDO_REGISTER; i < max_regno; i++)
    if (reg_renumber[i] != -1)
      fprintf (file, ";; Register %d in %d.\n", i, reg_renumber[i]);
}

/* Run old register allocator.  Return TRUE if we must exit
   rest_of_compilation upon return.  */
static unsigned int
rest_of_handle_local_alloc (void)
{
  int rebuild_notes;

  /* Determine if the current function is a leaf before running reload
     since this can impact optimizations done by the prologue and
     epilogue thus changing register elimination offsets.  */
  current_function_is_leaf = leaf_function_p ();

  /* Allocate the reg_renumber array.  */
  allocate_reg_info (max_regno, FALSE, TRUE);

  /* And the reg_equiv_memory_loc array.  */
  VEC_safe_grow (rtx, gc, reg_equiv_memory_loc_vec, max_regno);
  memset (VEC_address (rtx, reg_equiv_memory_loc_vec), 0,
	  sizeof (rtx) * max_regno);
  reg_equiv_memory_loc = VEC_address (rtx, reg_equiv_memory_loc_vec);

  allocate_initial_values (reg_equiv_memory_loc);

  regclass (get_insns (), max_reg_num ());
  rebuild_notes = local_alloc ();

  /* Local allocation may have turned an indirect jump into a direct
     jump.  If so, we must rebuild the JUMP_LABEL fields of jumping
     instructions.  */
  if (rebuild_notes)
    {
      timevar_push (TV_JUMP);

      rebuild_jump_labels (get_insns ());
      purge_all_dead_edges ();
      delete_unreachable_blocks ();

      timevar_pop (TV_JUMP);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      timevar_push (TV_DUMP);
      dump_flow_info (dump_file, dump_flags);
      dump_local_alloc (dump_file);
      timevar_pop (TV_DUMP);
    }
  return 0;
}

struct tree_opt_pass pass_local_alloc =
{
  "lreg",                               /* name */
  NULL,                                 /* gate */
  rest_of_handle_local_alloc,           /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_LOCAL_ALLOC,                       /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'l'                                   /* letter */
};

