/* Compute register class preferences for pseudo-registers.
   Copyright (C) 1987, 1988, 1991, 1992, 1993, 1994, 1995, 1996
   1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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


/* This file contains two passes of the compiler: reg_scan and reg_class.
   It also defines some tables of information about the hardware registers
   and a function init_reg_sets to initialize the tables.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "hard-reg-set.h"
#include "rtl.h"
#include "expr.h"
#include "tm_p.h"
#include "flags.h"
#include "basic-block.h"
#include "regs.h"
#include "addresses.h"
#include "function.h"
#include "insn-config.h"
#include "recog.h"
#include "reload.h"
#include "real.h"
#include "toplev.h"
#include "output.h"
#include "ggc.h"
#include "timevar.h"
#include "hashtab.h"
#include "target.h"

static void init_reg_sets_1 (void);
static void init_reg_autoinc (void);

/* If we have auto-increment or auto-decrement and we can have secondary
   reloads, we are not allowed to use classes requiring secondary
   reloads for pseudos auto-incremented since reload can't handle it.  */
/* We leave it to target hooks to decide if we have secondary reloads, so
   assume that we might have them.  */
#if defined(AUTO_INC_DEC) /* */
#define FORBIDDEN_INC_DEC_CLASSES
#endif

/* Register tables used by many passes.  */

/* Indexed by hard register number, contains 1 for registers
   that are fixed use (stack pointer, pc, frame pointer, etc.).
   These are the registers that cannot be used to allocate
   a pseudo reg for general use.  */

char fixed_regs[FIRST_PSEUDO_REGISTER];

/* Same info as a HARD_REG_SET.  */

HARD_REG_SET fixed_reg_set;

/* Data for initializing the above.  */

static const char initial_fixed_regs[] = FIXED_REGISTERS;

/* Indexed by hard register number, contains 1 for registers
   that are fixed use or are clobbered by function calls.
   These are the registers that cannot be used to allocate
   a pseudo reg whose life crosses calls unless we are able
   to save/restore them across the calls.  */

char call_used_regs[FIRST_PSEUDO_REGISTER];

/* Same info as a HARD_REG_SET.  */

HARD_REG_SET call_used_reg_set;

/* HARD_REG_SET of registers we want to avoid caller saving.  */
HARD_REG_SET losing_caller_save_reg_set;

/* Data for initializing the above.  */

static const char initial_call_used_regs[] = CALL_USED_REGISTERS;

/* This is much like call_used_regs, except it doesn't have to
   be a superset of FIXED_REGISTERS. This vector indicates
   what is really call clobbered, and is used when defining
   regs_invalidated_by_call.  */

#ifdef CALL_REALLY_USED_REGISTERS
char call_really_used_regs[] = CALL_REALLY_USED_REGISTERS;
#endif

#ifdef CALL_REALLY_USED_REGISTERS
#define CALL_REALLY_USED_REGNO_P(X)  call_really_used_regs[X]
#else
#define CALL_REALLY_USED_REGNO_P(X)  call_used_regs[X]
#endif


/* Indexed by hard register number, contains 1 for registers that are
   fixed use or call used registers that cannot hold quantities across
   calls even if we are willing to save and restore them.  call fixed
   registers are a subset of call used registers.  */

char call_fixed_regs[FIRST_PSEUDO_REGISTER];

/* The same info as a HARD_REG_SET.  */

HARD_REG_SET call_fixed_reg_set;

/* Indexed by hard register number, contains 1 for registers
   that are being used for global register decls.
   These must be exempt from ordinary flow analysis
   and are also considered fixed.  */

char global_regs[FIRST_PSEUDO_REGISTER];

/* Contains 1 for registers that are set or clobbered by calls.  */
/* ??? Ideally, this would be just call_used_regs plus global_regs, but
   for someone's bright idea to have call_used_regs strictly include
   fixed_regs.  Which leaves us guessing as to the set of fixed_regs
   that are actually preserved.  We know for sure that those associated
   with the local stack frame are safe, but scant others.  */

HARD_REG_SET regs_invalidated_by_call;

/* Table of register numbers in the order in which to try to use them.  */
#ifdef REG_ALLOC_ORDER
int reg_alloc_order[FIRST_PSEUDO_REGISTER] = REG_ALLOC_ORDER;

/* The inverse of reg_alloc_order.  */
int inv_reg_alloc_order[FIRST_PSEUDO_REGISTER];
#endif

/* For each reg class, a HARD_REG_SET saying which registers are in it.  */

HARD_REG_SET reg_class_contents[N_REG_CLASSES];

/* The same information, but as an array of unsigned ints.  We copy from
   these unsigned ints to the table above.  We do this so the tm.h files
   do not have to be aware of the wordsize for machines with <= 64 regs.
   Note that we hard-code 32 here, not HOST_BITS_PER_INT.  */

#define N_REG_INTS  \
  ((FIRST_PSEUDO_REGISTER + (32 - 1)) / 32)

static const unsigned int_reg_class_contents[N_REG_CLASSES][N_REG_INTS]
  = REG_CLASS_CONTENTS;

/* For each reg class, number of regs it contains.  */

unsigned int reg_class_size[N_REG_CLASSES];

/* For each reg class, table listing all the containing classes.  */

static enum reg_class reg_class_superclasses[N_REG_CLASSES][N_REG_CLASSES];

/* For each reg class, table listing all the classes contained in it.  */

static enum reg_class reg_class_subclasses[N_REG_CLASSES][N_REG_CLASSES];

/* For each pair of reg classes,
   a largest reg class contained in their union.  */

enum reg_class reg_class_subunion[N_REG_CLASSES][N_REG_CLASSES];

/* For each pair of reg classes,
   the smallest reg class containing their union.  */

enum reg_class reg_class_superunion[N_REG_CLASSES][N_REG_CLASSES];

/* Array containing all of the register names.  */

const char * reg_names[] = REGISTER_NAMES;

/* Array containing all of the register class names.  */

const char * reg_class_names[] = REG_CLASS_NAMES;

/* For each hard register, the widest mode object that it can contain.
   This will be a MODE_INT mode if the register can hold integers.  Otherwise
   it will be a MODE_FLOAT or a MODE_CC mode, whichever is valid for the
   register.  */

enum machine_mode reg_raw_mode[FIRST_PSEUDO_REGISTER];

/* 1 if there is a register of given mode.  */

bool have_regs_of_mode [MAX_MACHINE_MODE];

/* 1 if class does contain register of given mode.  */

static char contains_reg_of_mode [N_REG_CLASSES] [MAX_MACHINE_MODE];

/* Maximum cost of moving from a register in one class to a register in
   another class.  Based on REGISTER_MOVE_COST.  */

static int move_cost[MAX_MACHINE_MODE][N_REG_CLASSES][N_REG_CLASSES];

/* Similar, but here we don't have to move if the first index is a subset
   of the second so in that case the cost is zero.  */

static int may_move_in_cost[MAX_MACHINE_MODE][N_REG_CLASSES][N_REG_CLASSES];

/* Similar, but here we don't have to move if the first index is a superset
   of the second so in that case the cost is zero.  */

static int may_move_out_cost[MAX_MACHINE_MODE][N_REG_CLASSES][N_REG_CLASSES];

#ifdef FORBIDDEN_INC_DEC_CLASSES

/* These are the classes that regs which are auto-incremented or decremented
   cannot be put in.  */

static int forbidden_inc_dec_class[N_REG_CLASSES];

/* Indexed by n, is nonzero if (REG n) is used in an auto-inc or auto-dec
   context.  */

static char *in_inc_dec;

#endif /* FORBIDDEN_INC_DEC_CLASSES */

/* Sample MEM values for use by memory_move_secondary_cost.  */

static GTY(()) rtx top_of_stack[MAX_MACHINE_MODE];

/* Linked list of reg_info structures allocated for reg_n_info array.
   Grouping all of the allocated structures together in one lump
   means only one call to bzero to clear them, rather than n smaller
   calls.  */
struct reg_info_data {
  struct reg_info_data *next;	/* next set of reg_info structures */
  size_t min_index;		/* minimum index # */
  size_t max_index;		/* maximum index # */
  char used_p;			/* nonzero if this has been used previously */
  reg_info data[1];		/* beginning of the reg_info data */
};

static struct reg_info_data *reg_info_head;

/* No more global register variables may be declared; true once
   regclass has been initialized.  */

static int no_global_reg_vars = 0;

/* Specify number of hard registers given machine mode occupy.  */
unsigned char hard_regno_nregs[FIRST_PSEUDO_REGISTER][MAX_MACHINE_MODE];

/* Function called only once to initialize the above data on reg usage.
   Once this is done, various switches may override.  */

void
init_reg_sets (void)
{
  int i, j;

  /* First copy the register information from the initial int form into
     the regsets.  */

  for (i = 0; i < N_REG_CLASSES; i++)
    {
      CLEAR_HARD_REG_SET (reg_class_contents[i]);

      /* Note that we hard-code 32 here, not HOST_BITS_PER_INT.  */
      for (j = 0; j < FIRST_PSEUDO_REGISTER; j++)
	if (int_reg_class_contents[i][j / 32]
	    & ((unsigned) 1 << (j % 32)))
	  SET_HARD_REG_BIT (reg_class_contents[i], j);
    }

  /* Sanity check: make sure the target macros FIXED_REGISTERS and
     CALL_USED_REGISTERS had the right number of initializers.  */
  gcc_assert (sizeof fixed_regs == sizeof initial_fixed_regs);
  gcc_assert (sizeof call_used_regs == sizeof initial_call_used_regs);

  memcpy (fixed_regs, initial_fixed_regs, sizeof fixed_regs);
  memcpy (call_used_regs, initial_call_used_regs, sizeof call_used_regs);
  memset (global_regs, 0, sizeof global_regs);

#ifdef REG_ALLOC_ORDER
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    inv_reg_alloc_order[reg_alloc_order[i]] = i;
#endif
}

/* After switches have been processed, which perhaps alter
   `fixed_regs' and `call_used_regs', convert them to HARD_REG_SETs.  */

static void
init_reg_sets_1 (void)
{
  unsigned int i, j;
  unsigned int /* enum machine_mode */ m;

  /* This macro allows the fixed or call-used registers
     and the register classes to depend on target flags.  */

#ifdef CONDITIONAL_REGISTER_USAGE
  CONDITIONAL_REGISTER_USAGE;
#endif

  /* Compute number of hard regs in each class.  */

  memset (reg_class_size, 0, sizeof reg_class_size);
  for (i = 0; i < N_REG_CLASSES; i++)
    for (j = 0; j < FIRST_PSEUDO_REGISTER; j++)
      if (TEST_HARD_REG_BIT (reg_class_contents[i], j))
	reg_class_size[i]++;

  /* Initialize the table of subunions.
     reg_class_subunion[I][J] gets the largest-numbered reg-class
     that is contained in the union of classes I and J.  */

  for (i = 0; i < N_REG_CLASSES; i++)
    {
      for (j = 0; j < N_REG_CLASSES; j++)
	{
	  HARD_REG_SET c;
	  int k;

	  COPY_HARD_REG_SET (c, reg_class_contents[i]);
	  IOR_HARD_REG_SET (c, reg_class_contents[j]);
	  for (k = 0; k < N_REG_CLASSES; k++)
	    {
	      GO_IF_HARD_REG_SUBSET (reg_class_contents[k], c,
				     subclass1);
	      continue;

	    subclass1:
	      /* Keep the largest subclass.  */		/* SPEE 900308 */
	      GO_IF_HARD_REG_SUBSET (reg_class_contents[k],
				     reg_class_contents[(int) reg_class_subunion[i][j]],
				     subclass2);
	      reg_class_subunion[i][j] = (enum reg_class) k;
	    subclass2:
	      ;
	    }
	}
    }

  /* Initialize the table of superunions.
     reg_class_superunion[I][J] gets the smallest-numbered reg-class
     containing the union of classes I and J.  */

  for (i = 0; i < N_REG_CLASSES; i++)
    {
      for (j = 0; j < N_REG_CLASSES; j++)
	{
	  HARD_REG_SET c;
	  int k;

	  COPY_HARD_REG_SET (c, reg_class_contents[i]);
	  IOR_HARD_REG_SET (c, reg_class_contents[j]);
	  for (k = 0; k < N_REG_CLASSES; k++)
	    GO_IF_HARD_REG_SUBSET (c, reg_class_contents[k], superclass);

	superclass:
	  reg_class_superunion[i][j] = (enum reg_class) k;
	}
    }

  /* Initialize the tables of subclasses and superclasses of each reg class.
     First clear the whole table, then add the elements as they are found.  */

  for (i = 0; i < N_REG_CLASSES; i++)
    {
      for (j = 0; j < N_REG_CLASSES; j++)
	{
	  reg_class_superclasses[i][j] = LIM_REG_CLASSES;
	  reg_class_subclasses[i][j] = LIM_REG_CLASSES;
	}
    }

  for (i = 0; i < N_REG_CLASSES; i++)
    {
      if (i == (int) NO_REGS)
	continue;

      for (j = i + 1; j < N_REG_CLASSES; j++)
	{
	  enum reg_class *p;

	  GO_IF_HARD_REG_SUBSET (reg_class_contents[i], reg_class_contents[j],
				 subclass);
	  continue;
	subclass:
	  /* Reg class I is a subclass of J.
	     Add J to the table of superclasses of I.  */
	  p = &reg_class_superclasses[i][0];
	  while (*p != LIM_REG_CLASSES) p++;
	  *p = (enum reg_class) j;
	  /* Add I to the table of superclasses of J.  */
	  p = &reg_class_subclasses[j][0];
	  while (*p != LIM_REG_CLASSES) p++;
	  *p = (enum reg_class) i;
	}
    }

  /* Initialize "constant" tables.  */

  CLEAR_HARD_REG_SET (fixed_reg_set);
  CLEAR_HARD_REG_SET (call_used_reg_set);
  CLEAR_HARD_REG_SET (call_fixed_reg_set);
  CLEAR_HARD_REG_SET (regs_invalidated_by_call);

  memcpy (call_fixed_regs, fixed_regs, sizeof call_fixed_regs);

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      /* call_used_regs must include fixed_regs.  */
      gcc_assert (!fixed_regs[i] || call_used_regs[i]);
#ifdef CALL_REALLY_USED_REGISTERS
      /* call_used_regs must include call_really_used_regs.  */
      gcc_assert (!call_really_used_regs[i] || call_used_regs[i]);
#endif

      if (fixed_regs[i])
	SET_HARD_REG_BIT (fixed_reg_set, i);

      if (call_used_regs[i])
	SET_HARD_REG_BIT (call_used_reg_set, i);
      if (call_fixed_regs[i])
	SET_HARD_REG_BIT (call_fixed_reg_set, i);
      if (CLASS_LIKELY_SPILLED_P (REGNO_REG_CLASS (i)))
	SET_HARD_REG_BIT (losing_caller_save_reg_set, i);

      /* There are a couple of fixed registers that we know are safe to
	 exclude from being clobbered by calls:

	 The frame pointer is always preserved across calls.  The arg pointer
	 is if it is fixed.  The stack pointer usually is, unless
	 RETURN_POPS_ARGS, in which case an explicit CLOBBER will be present.
	 If we are generating PIC code, the PIC offset table register is
	 preserved across calls, though the target can override that.  */

      if (i == STACK_POINTER_REGNUM)
	;
      else if (global_regs[i])
	SET_HARD_REG_BIT (regs_invalidated_by_call, i);
      else if (i == FRAME_POINTER_REGNUM)
	;
#if HARD_FRAME_POINTER_REGNUM != FRAME_POINTER_REGNUM
      else if (i == HARD_FRAME_POINTER_REGNUM)
	;
#endif
#if ARG_POINTER_REGNUM != FRAME_POINTER_REGNUM
      else if (i == ARG_POINTER_REGNUM && fixed_regs[i])
	;
#endif
#ifndef PIC_OFFSET_TABLE_REG_CALL_CLOBBERED
      else if (i == (unsigned) PIC_OFFSET_TABLE_REGNUM && fixed_regs[i])
	;
#endif
      else if (CALL_REALLY_USED_REGNO_P (i))
	SET_HARD_REG_BIT (regs_invalidated_by_call, i);
    }

  memset (have_regs_of_mode, 0, sizeof (have_regs_of_mode));
  memset (contains_reg_of_mode, 0, sizeof (contains_reg_of_mode));
  for (m = 0; m < (unsigned int) MAX_MACHINE_MODE; m++)
    for (i = 0; i < N_REG_CLASSES; i++)
      if ((unsigned) CLASS_MAX_NREGS (i, m) <= reg_class_size[i])
	for (j = 0; j < FIRST_PSEUDO_REGISTER; j++)
	  if (!fixed_regs [j] && TEST_HARD_REG_BIT (reg_class_contents[i], j)
	      && HARD_REGNO_MODE_OK (j, m))
	     {
	       contains_reg_of_mode [i][m] = 1;
	       have_regs_of_mode [m] = 1;
	       break;
	     }

  /* Initialize the move cost table.  Find every subset of each class
     and take the maximum cost of moving any subset to any other.  */

  for (m = 0; m < (unsigned int) MAX_MACHINE_MODE; m++)
    if (have_regs_of_mode [m])
      {
	for (i = 0; i < N_REG_CLASSES; i++)
	  if (contains_reg_of_mode [i][m])
	    for (j = 0; j < N_REG_CLASSES; j++)
	      {
		int cost;
		enum reg_class *p1, *p2;

		if (!contains_reg_of_mode [j][m])
		  {
		    move_cost[m][i][j] = 65536;
		    may_move_in_cost[m][i][j] = 65536;
		    may_move_out_cost[m][i][j] = 65536;
		  }
		else
		  {
		    cost = REGISTER_MOVE_COST (m, i, j);

		    for (p2 = &reg_class_subclasses[j][0];
			 *p2 != LIM_REG_CLASSES;
			 p2++)
		      if (*p2 != i && contains_reg_of_mode [*p2][m])
			cost = MAX (cost, move_cost [m][i][*p2]);

		    for (p1 = &reg_class_subclasses[i][0];
			 *p1 != LIM_REG_CLASSES;
			 p1++)
		      if (*p1 != j && contains_reg_of_mode [*p1][m])
			cost = MAX (cost, move_cost [m][*p1][j]);

		    move_cost[m][i][j] = cost;

		    if (reg_class_subset_p (i, j))
		      may_move_in_cost[m][i][j] = 0;
		    else
		      may_move_in_cost[m][i][j] = cost;

		    if (reg_class_subset_p (j, i))
		      may_move_out_cost[m][i][j] = 0;
		    else
		      may_move_out_cost[m][i][j] = cost;
		  }
	      }
	  else
	    for (j = 0; j < N_REG_CLASSES; j++)
	      {
		move_cost[m][i][j] = 65536;
		may_move_in_cost[m][i][j] = 65536;
		may_move_out_cost[m][i][j] = 65536;
	      }
      }
}

/* Compute the table of register modes.
   These values are used to record death information for individual registers
   (as opposed to a multi-register mode).  */

void
init_reg_modes_once (void)
{
  int i, j;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    for (j = 0; j < MAX_MACHINE_MODE; j++)
      hard_regno_nregs[i][j] = HARD_REGNO_NREGS(i, (enum machine_mode)j);

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      reg_raw_mode[i] = choose_hard_reg_mode (i, 1, false);

      /* If we couldn't find a valid mode, just use the previous mode.
         ??? One situation in which we need to do this is on the mips where
	 HARD_REGNO_NREGS (fpreg, [SD]Fmode) returns 2.  Ideally we'd like
	 to use DF mode for the even registers and VOIDmode for the odd
	 (for the cpu models where the odd ones are inaccessible).  */
      if (reg_raw_mode[i] == VOIDmode)
	reg_raw_mode[i] = i == 0 ? word_mode : reg_raw_mode[i-1];
    }
}

/* Finish initializing the register sets and
   initialize the register modes.  */

void
init_regs (void)
{
  /* This finishes what was started by init_reg_sets, but couldn't be done
     until after register usage was specified.  */
  init_reg_sets_1 ();

  init_reg_autoinc ();
}

/* Initialize some fake stack-frame MEM references for use in
   memory_move_secondary_cost.  */

void
init_fake_stack_mems (void)
{
  {
    int i;

    for (i = 0; i < MAX_MACHINE_MODE; i++)
      top_of_stack[i] = gen_rtx_MEM (i, stack_pointer_rtx);
  }
}


/* Compute extra cost of moving registers to/from memory due to reloads.
   Only needed if secondary reloads are required for memory moves.  */

int
memory_move_secondary_cost (enum machine_mode mode, enum reg_class class, int in)
{
  enum reg_class altclass;
  int partial_cost = 0;
  /* We need a memory reference to feed to SECONDARY... macros.  */
  /* mem may be unused even if the SECONDARY_ macros are defined.  */
  rtx mem ATTRIBUTE_UNUSED = top_of_stack[(int) mode];


  altclass = secondary_reload_class (in ? 1 : 0, class, mode, mem);

  if (altclass == NO_REGS)
    return 0;

  if (in)
    partial_cost = REGISTER_MOVE_COST (mode, altclass, class);
  else
    partial_cost = REGISTER_MOVE_COST (mode, class, altclass);

  if (class == altclass)
    /* This isn't simply a copy-to-temporary situation.  Can't guess
       what it is, so MEMORY_MOVE_COST really ought not to be calling
       here in that case.

       I'm tempted to put in an assert here, but returning this will
       probably only give poor estimates, which is what we would've
       had before this code anyways.  */
    return partial_cost;

  /* Check if the secondary reload register will also need a
     secondary reload.  */
  return memory_move_secondary_cost (mode, altclass, in) + partial_cost;
}

/* Return a machine mode that is legitimate for hard reg REGNO and large
   enough to save nregs.  If we can't find one, return VOIDmode.
   If CALL_SAVED is true, only consider modes that are call saved.  */

enum machine_mode
choose_hard_reg_mode (unsigned int regno ATTRIBUTE_UNUSED,
		      unsigned int nregs, bool call_saved)
{
  unsigned int /* enum machine_mode */ m;
  enum machine_mode found_mode = VOIDmode, mode;

  /* We first look for the largest integer mode that can be validly
     held in REGNO.  If none, we look for the largest floating-point mode.
     If we still didn't find a valid mode, try CCmode.  */

  for (mode = GET_CLASS_NARROWEST_MODE (MODE_INT);
       mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if ((unsigned) hard_regno_nregs[regno][mode] == nregs
	&& HARD_REGNO_MODE_OK (regno, mode)
	&& (! call_saved || ! HARD_REGNO_CALL_PART_CLOBBERED (regno, mode)))
      found_mode = mode;

  if (found_mode != VOIDmode)
    return found_mode;

  for (mode = GET_CLASS_NARROWEST_MODE (MODE_FLOAT);
       mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if ((unsigned) hard_regno_nregs[regno][mode] == nregs
	&& HARD_REGNO_MODE_OK (regno, mode)
	&& (! call_saved || ! HARD_REGNO_CALL_PART_CLOBBERED (regno, mode)))
      found_mode = mode;

  if (found_mode != VOIDmode)
    return found_mode;

  for (mode = GET_CLASS_NARROWEST_MODE (MODE_VECTOR_FLOAT);
       mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if ((unsigned) hard_regno_nregs[regno][mode] == nregs
	&& HARD_REGNO_MODE_OK (regno, mode)
	&& (! call_saved || ! HARD_REGNO_CALL_PART_CLOBBERED (regno, mode)))
      found_mode = mode;

  if (found_mode != VOIDmode)
    return found_mode;

  for (mode = GET_CLASS_NARROWEST_MODE (MODE_VECTOR_INT);
       mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    if ((unsigned) hard_regno_nregs[regno][mode] == nregs
	&& HARD_REGNO_MODE_OK (regno, mode)
	&& (! call_saved || ! HARD_REGNO_CALL_PART_CLOBBERED (regno, mode)))
      found_mode = mode;

  if (found_mode != VOIDmode)
    return found_mode;

  /* Iterate over all of the CCmodes.  */
  for (m = (unsigned int) CCmode; m < (unsigned int) NUM_MACHINE_MODES; ++m)
    {
      mode = (enum machine_mode) m;
      if ((unsigned) hard_regno_nregs[regno][mode] == nregs
	  && HARD_REGNO_MODE_OK (regno, mode)
	  && (! call_saved || ! HARD_REGNO_CALL_PART_CLOBBERED (regno, mode)))
	return mode;
    }

  /* We can't find a mode valid for this register.  */
  return VOIDmode;
}

/* Specify the usage characteristics of the register named NAME.
   It should be a fixed register if FIXED and a
   call-used register if CALL_USED.  */

void
fix_register (const char *name, int fixed, int call_used)
{
  int i;

  /* Decode the name and update the primary form of
     the register info.  */

  if ((i = decode_reg_name (name)) >= 0)
    {
      if ((i == STACK_POINTER_REGNUM
#ifdef HARD_FRAME_POINTER_REGNUM
	   || i == HARD_FRAME_POINTER_REGNUM
#else
	   || i == FRAME_POINTER_REGNUM
#endif
	   )
	  && (fixed == 0 || call_used == 0))
	{
	  static const char * const what_option[2][2] = {
	    { "call-saved", "call-used" },
	    { "no-such-option", "fixed" }};

	  error ("can't use '%s' as a %s register", name,
		 what_option[fixed][call_used]);
	}
      else
	{
	  fixed_regs[i] = fixed;
	  call_used_regs[i] = call_used;
#ifdef CALL_REALLY_USED_REGISTERS
	  if (fixed == 0)
	    call_really_used_regs[i] = call_used;
#endif
	}
    }
  else
    {
      warning (0, "unknown register name: %s", name);
    }
}

/* Mark register number I as global.  */

void
globalize_reg (int i)
{
  if (fixed_regs[i] == 0 && no_global_reg_vars)
    error ("global register variable follows a function definition");

  if (global_regs[i])
    {
      warning (0, "register used for two global register variables");
      return;
    }

  if (call_used_regs[i] && ! fixed_regs[i])
    warning (0, "call-clobbered register used for global register variable");

  global_regs[i] = 1;

  /* If we're globalizing the frame pointer, we need to set the
     appropriate regs_invalidated_by_call bit, even if it's already
     set in fixed_regs.  */
  if (i != STACK_POINTER_REGNUM)
    SET_HARD_REG_BIT (regs_invalidated_by_call, i);

  /* If already fixed, nothing else to do.  */
  if (fixed_regs[i])
    return;

  fixed_regs[i] = call_used_regs[i] = call_fixed_regs[i] = 1;
#ifdef CALL_REALLY_USED_REGISTERS
  call_really_used_regs[i] = 1;
#endif

  SET_HARD_REG_BIT (fixed_reg_set, i);
  SET_HARD_REG_BIT (call_used_reg_set, i);
  SET_HARD_REG_BIT (call_fixed_reg_set, i);
}

/* Now the data and code for the `regclass' pass, which happens
   just before local-alloc.  */

/* The `costs' struct records the cost of using a hard register of each class
   and of using memory for each pseudo.  We use this data to set up
   register class preferences.  */

struct costs
{
  int cost[N_REG_CLASSES];
  int mem_cost;
};

/* Structure used to record preferences of given pseudo.  */
struct reg_pref
{
  /* (enum reg_class) prefclass is the preferred class.  May be
     NO_REGS if no class is better than memory.  */
  char prefclass;

  /* altclass is a register class that we should use for allocating
     pseudo if no register in the preferred class is available.
     If no register in this class is available, memory is preferred.

     It might appear to be more general to have a bitmask of classes here,
     but since it is recommended that there be a class corresponding to the
     union of most major pair of classes, that generality is not required.  */
  char altclass;
};

/* Record the cost of each class for each pseudo.  */

static struct costs *costs;

/* Initialized once, and used to initialize cost values for each insn.  */

static struct costs init_cost;

/* Record preferences of each pseudo.
   This is available after `regclass' is run.  */

static struct reg_pref *reg_pref;

/* Allocated buffers for reg_pref.  */

static struct reg_pref *reg_pref_buffer;

/* Frequency of executions of current insn.  */

static int frequency;

static rtx scan_one_insn (rtx, int);
static void record_operand_costs (rtx, struct costs *, struct reg_pref *);
static void dump_regclass (FILE *);
static void record_reg_classes (int, int, rtx *, enum machine_mode *,
				const char **, rtx, struct costs *,
				struct reg_pref *);
static int copy_cost (rtx, enum machine_mode, enum reg_class, int,
		      secondary_reload_info *);
static void record_address_regs (enum machine_mode, rtx, int, enum rtx_code,
				 enum rtx_code, int);
#ifdef FORBIDDEN_INC_DEC_CLASSES
static int auto_inc_dec_reg_p (rtx, enum machine_mode);
#endif
static void reg_scan_mark_refs (rtx, rtx, int);

/* Wrapper around REGNO_OK_FOR_INDEX_P, to allow pseudo registers.  */

static inline bool
ok_for_index_p_nonstrict (rtx reg)
{
  unsigned regno = REGNO (reg);
  return regno >= FIRST_PSEUDO_REGISTER || REGNO_OK_FOR_INDEX_P (regno);
}

/* A version of regno_ok_for_base_p for use during regclass, when all pseudos
   should count as OK.  Arguments as for regno_ok_for_base_p.  */

static inline bool
ok_for_base_p_nonstrict (rtx reg, enum machine_mode mode,
			 enum rtx_code outer_code, enum rtx_code index_code)
{
  unsigned regno = REGNO (reg);
  if (regno >= FIRST_PSEUDO_REGISTER)
    return true;

  return ok_for_base_p_1 (regno, mode, outer_code, index_code);
}

/* Return the reg_class in which pseudo reg number REGNO is best allocated.
   This function is sometimes called before the info has been computed.
   When that happens, just return GENERAL_REGS, which is innocuous.  */

enum reg_class
reg_preferred_class (int regno)
{
  if (reg_pref == 0)
    return GENERAL_REGS;
  return (enum reg_class) reg_pref[regno].prefclass;
}

enum reg_class
reg_alternate_class (int regno)
{
  if (reg_pref == 0)
    return ALL_REGS;

  return (enum reg_class) reg_pref[regno].altclass;
}

/* Initialize some global data for this pass.  */

void
regclass_init (void)
{
  int i;

  init_cost.mem_cost = 10000;
  for (i = 0; i < N_REG_CLASSES; i++)
    init_cost.cost[i] = 10000;

  /* This prevents dump_flow_info from losing if called
     before regclass is run.  */
  reg_pref = NULL;

  /* No more global register variables may be declared.  */
  no_global_reg_vars = 1;
}

/* Dump register costs.  */
static void
dump_regclass (FILE *dump)
{
  int i;
  for (i = FIRST_PSEUDO_REGISTER; i < max_regno; i++)
    {
      int /* enum reg_class */ class;
      if (REG_N_REFS (i))
	{
	  fprintf (dump, "  Register %i costs:", i);
	  for (class = 0; class < (int) N_REG_CLASSES; class++)
	    if (contains_reg_of_mode [(enum reg_class) class][PSEUDO_REGNO_MODE (i)]
#ifdef FORBIDDEN_INC_DEC_CLASSES
		&& (!in_inc_dec[i]
		    || !forbidden_inc_dec_class[(enum reg_class) class])
#endif
#ifdef CANNOT_CHANGE_MODE_CLASS
		&& ! invalid_mode_change_p (i, (enum reg_class) class,
					    PSEUDO_REGNO_MODE (i))
#endif
		)
	    fprintf (dump, " %s:%i", reg_class_names[class],
		     costs[i].cost[(enum reg_class) class]);
	  fprintf (dump, " MEM:%i\n", costs[i].mem_cost);
	}
    }
}


/* Calculate the costs of insn operands.  */

static void
record_operand_costs (rtx insn, struct costs *op_costs,
		      struct reg_pref *reg_pref)
{
  const char *constraints[MAX_RECOG_OPERANDS];
  enum machine_mode modes[MAX_RECOG_OPERANDS];
  int i;

  for (i = 0; i < recog_data.n_operands; i++)
    {
      constraints[i] = recog_data.constraints[i];
      modes[i] = recog_data.operand_mode[i];
    }

  /* If we get here, we are set up to record the costs of all the
     operands for this insn.  Start by initializing the costs.
     Then handle any address registers.  Finally record the desired
     classes for any pseudos, doing it twice if some pair of
     operands are commutative.  */

  for (i = 0; i < recog_data.n_operands; i++)
    {
      op_costs[i] = init_cost;

      if (GET_CODE (recog_data.operand[i]) == SUBREG)
	recog_data.operand[i] = SUBREG_REG (recog_data.operand[i]);

      if (MEM_P (recog_data.operand[i]))
	record_address_regs (GET_MODE (recog_data.operand[i]),
			     XEXP (recog_data.operand[i], 0),
			     0, MEM, SCRATCH, frequency * 2);
      else if (constraints[i][0] == 'p'
	       || EXTRA_ADDRESS_CONSTRAINT (constraints[i][0], constraints[i]))
	record_address_regs (VOIDmode, recog_data.operand[i], 0, ADDRESS,
			     SCRATCH, frequency * 2);
    }

  /* Check for commutative in a separate loop so everything will
     have been initialized.  We must do this even if one operand
     is a constant--see addsi3 in m68k.md.  */

  for (i = 0; i < (int) recog_data.n_operands - 1; i++)
    if (constraints[i][0] == '%')
      {
	const char *xconstraints[MAX_RECOG_OPERANDS];
	int j;

	/* Handle commutative operands by swapping the constraints.
	   We assume the modes are the same.  */

	for (j = 0; j < recog_data.n_operands; j++)
	  xconstraints[j] = constraints[j];

	xconstraints[i] = constraints[i+1];
	xconstraints[i+1] = constraints[i];
	record_reg_classes (recog_data.n_alternatives, recog_data.n_operands,
			    recog_data.operand, modes,
			    xconstraints, insn, op_costs, reg_pref);
      }

  record_reg_classes (recog_data.n_alternatives, recog_data.n_operands,
		      recog_data.operand, modes,
		      constraints, insn, op_costs, reg_pref);
}

/* Subroutine of regclass, processes one insn INSN.  Scan it and record each
   time it would save code to put a certain register in a certain class.
   PASS, when nonzero, inhibits some optimizations which need only be done
   once.
   Return the last insn processed, so that the scan can be continued from
   there.  */

static rtx
scan_one_insn (rtx insn, int pass)
{
  enum rtx_code pat_code;
  rtx set, note;
  int i, j;
  struct costs op_costs[MAX_RECOG_OPERANDS];

  if (!INSN_P (insn))
    return insn;

  pat_code = GET_CODE (PATTERN (insn));
  if (pat_code == USE
      || pat_code == CLOBBER
      || pat_code == ASM_INPUT
      || pat_code == ADDR_VEC
      || pat_code == ADDR_DIFF_VEC)
    return insn;

  set = single_set (insn);
  extract_insn (insn);

  /* If this insn loads a parameter from its stack slot, then
     it represents a savings, rather than a cost, if the
     parameter is stored in memory.  Record this fact.  */

  if (set != 0 && REG_P (SET_DEST (set))
      && MEM_P (SET_SRC (set))
      && (note = find_reg_note (insn, REG_EQUIV,
				NULL_RTX)) != 0
      && MEM_P (XEXP (note, 0)))
    {
      costs[REGNO (SET_DEST (set))].mem_cost
	-= (MEMORY_MOVE_COST (GET_MODE (SET_DEST (set)),
			      GENERAL_REGS, 1)
	    * frequency);
      record_address_regs (GET_MODE (SET_SRC (set)), XEXP (SET_SRC (set), 0),
			   0, MEM, SCRATCH, frequency * 2);
      return insn;
    }

  /* Improve handling of two-address insns such as
     (set X (ashift CONST Y)) where CONST must be made to
     match X. Change it into two insns: (set X CONST)
     (set X (ashift X Y)).  If we left this for reloading, it
     would probably get three insns because X and Y might go
     in the same place. This prevents X and Y from receiving
     the same hard reg.

     We can only do this if the modes of operands 0 and 1
     (which might not be the same) are tieable and we only need
     do this during our first pass.  */

  if (pass == 0 && optimize
      && recog_data.n_operands >= 3
      && recog_data.constraints[1][0] == '0'
      && recog_data.constraints[1][1] == 0
      && CONSTANT_P (recog_data.operand[1])
      && ! rtx_equal_p (recog_data.operand[0], recog_data.operand[1])
      && ! rtx_equal_p (recog_data.operand[0], recog_data.operand[2])
      && REG_P (recog_data.operand[0])
      && MODES_TIEABLE_P (GET_MODE (recog_data.operand[0]),
			  recog_data.operand_mode[1]))
    {
      rtx previnsn = prev_real_insn (insn);
      rtx dest
	= gen_lowpart (recog_data.operand_mode[1],
		       recog_data.operand[0]);
      rtx newinsn
	= emit_insn_before (gen_move_insn (dest, recog_data.operand[1]), insn);

      /* If this insn was the start of a basic block,
	 include the new insn in that block.
	 We need not check for code_label here;
	 while a basic block can start with a code_label,
	 INSN could not be at the beginning of that block.  */
      if (previnsn == 0 || JUMP_P (previnsn))
	{
	  basic_block b;
	  FOR_EACH_BB (b)
	    if (insn == BB_HEAD (b))
	      BB_HEAD (b) = newinsn;
	}

      /* This makes one more setting of new insns's dest.  */
      REG_N_SETS (REGNO (recog_data.operand[0]))++;
      REG_N_REFS (REGNO (recog_data.operand[0]))++;
      REG_FREQ (REGNO (recog_data.operand[0])) += frequency;

      *recog_data.operand_loc[1] = recog_data.operand[0];
      REG_N_REFS (REGNO (recog_data.operand[0]))++;
      REG_FREQ (REGNO (recog_data.operand[0])) += frequency;
      for (i = recog_data.n_dups - 1; i >= 0; i--)
	if (recog_data.dup_num[i] == 1)
	  {
	    *recog_data.dup_loc[i] = recog_data.operand[0];
	    REG_N_REFS (REGNO (recog_data.operand[0]))++;
	    REG_FREQ (REGNO (recog_data.operand[0])) += frequency;
	  }

      return PREV_INSN (newinsn);
    }

  record_operand_costs (insn, op_costs, reg_pref);

  /* Now add the cost for each operand to the total costs for
     its register.  */

  for (i = 0; i < recog_data.n_operands; i++)
    if (REG_P (recog_data.operand[i])
	&& REGNO (recog_data.operand[i]) >= FIRST_PSEUDO_REGISTER)
      {
	int regno = REGNO (recog_data.operand[i]);
	struct costs *p = &costs[regno], *q = &op_costs[i];

	p->mem_cost += q->mem_cost * frequency;
	for (j = 0; j < N_REG_CLASSES; j++)
	  p->cost[j] += q->cost[j] * frequency;
      }

  return insn;
}

/* Initialize information about which register classes can be used for
   pseudos that are auto-incremented or auto-decremented.  */

static void
init_reg_autoinc (void)
{
#ifdef FORBIDDEN_INC_DEC_CLASSES
  int i;

  for (i = 0; i < N_REG_CLASSES; i++)
    {
      rtx r = gen_rtx_raw_REG (VOIDmode, 0);
      enum machine_mode m;
      int j;

      for (j = 0; j < FIRST_PSEUDO_REGISTER; j++)
	if (TEST_HARD_REG_BIT (reg_class_contents[i], j))
	  {
	    REGNO (r) = j;

	    for (m = VOIDmode; (int) m < (int) MAX_MACHINE_MODE;
		 m = (enum machine_mode) ((int) m + 1))
	      if (HARD_REGNO_MODE_OK (j, m))
		{
		  /* ??? There are two assumptions here; that the base class does not
		     depend on the exact outer code (POST_INC vs. PRE_INC etc.), and
		     that it does not depend on the machine mode of the memory
		     reference.  */
		  enum reg_class base_class
		    = base_reg_class (VOIDmode, POST_INC, SCRATCH);

		  PUT_MODE (r, m);

		  /* If a register is not directly suitable for an
		     auto-increment or decrement addressing mode and
		     requires secondary reloads, disallow its class from
		     being used in such addresses.  */

		  if ((secondary_reload_class (1, base_class, m, r)
		       || secondary_reload_class (1, base_class, m, r))
		      && ! auto_inc_dec_reg_p (r, m))
		    forbidden_inc_dec_class[i] = 1;
		}
	  }
    }
#endif /* FORBIDDEN_INC_DEC_CLASSES */
}

/* This is a pass of the compiler that scans all instructions
   and calculates the preferred class for each pseudo-register.
   This information can be accessed later by calling `reg_preferred_class'.
   This pass comes just before local register allocation.  */

void
regclass (rtx f, int nregs)
{
  rtx insn;
  int i;
  int pass;

  init_recog ();

  costs = XNEWVEC (struct costs, nregs);

#ifdef FORBIDDEN_INC_DEC_CLASSES

  in_inc_dec = XNEWVEC (char, nregs);

#endif /* FORBIDDEN_INC_DEC_CLASSES */

  /* Normally we scan the insns once and determine the best class to use for
     each register.  However, if -fexpensive_optimizations are on, we do so
     twice, the second time using the tentative best classes to guide the
     selection.  */

  for (pass = 0; pass <= flag_expensive_optimizations; pass++)
    {
      basic_block bb;

      if (dump_file)
	fprintf (dump_file, "\n\nPass %i\n\n",pass);
      /* Zero out our accumulation of the cost of each class for each reg.  */

      memset (costs, 0, nregs * sizeof (struct costs));

#ifdef FORBIDDEN_INC_DEC_CLASSES
      memset (in_inc_dec, 0, nregs);
#endif

      /* Scan the instructions and record each time it would
	 save code to put a certain register in a certain class.  */

      if (!optimize)
	{
	  frequency = REG_FREQ_MAX;
	  for (insn = f; insn; insn = NEXT_INSN (insn))
	    insn = scan_one_insn (insn, pass);
	}
      else
	FOR_EACH_BB (bb)
	  {
	    /* Show that an insn inside a loop is likely to be executed three
	       times more than insns outside a loop.  This is much more
	       aggressive than the assumptions made elsewhere and is being
	       tried as an experiment.  */
	    frequency = REG_FREQ_FROM_BB (bb);
	    for (insn = BB_HEAD (bb); ; insn = NEXT_INSN (insn))
	      {
		insn = scan_one_insn (insn, pass);
		if (insn == BB_END (bb))
		  break;
	      }
	  }

      /* Now for each register look at how desirable each class is
	 and find which class is preferred.  Store that in
	 `prefclass'.  Record in `altclass' the largest register
	 class any of whose registers is better than memory.  */

      if (pass == 0)
	reg_pref = reg_pref_buffer;

      if (dump_file)
	{
	  dump_regclass (dump_file);
	  fprintf (dump_file,"\n");
	}
      for (i = FIRST_PSEUDO_REGISTER; i < nregs; i++)
	{
	  int best_cost = (1 << (HOST_BITS_PER_INT - 2)) - 1;
	  enum reg_class best = ALL_REGS, alt = NO_REGS;
	  /* This is an enum reg_class, but we call it an int
	     to save lots of casts.  */
	  int class;
	  struct costs *p = &costs[i];

	  /* In non-optimizing compilation REG_N_REFS is not initialized
	     yet.  */
	  if (optimize && !REG_N_REFS (i) && !REG_N_SETS (i))
	    continue;

	  for (class = (int) ALL_REGS - 1; class > 0; class--)
	    {
	      /* Ignore classes that are too small for this operand or
		 invalid for an operand that was auto-incremented.  */
	      if (!contains_reg_of_mode [class][PSEUDO_REGNO_MODE (i)]
#ifdef FORBIDDEN_INC_DEC_CLASSES
		  || (in_inc_dec[i] && forbidden_inc_dec_class[class])
#endif
#ifdef CANNOT_CHANGE_MODE_CLASS
		  || invalid_mode_change_p (i, (enum reg_class) class,
					    PSEUDO_REGNO_MODE (i))
#endif
		  )
		;
	      else if (p->cost[class] < best_cost)
		{
		  best_cost = p->cost[class];
		  best = (enum reg_class) class;
		}
	      else if (p->cost[class] == best_cost)
		best = reg_class_subunion[(int) best][class];
	    }

	  /* If no register class is better than memory, use memory. */
	  if (p->mem_cost < best_cost)
	    best = NO_REGS;

	  /* Record the alternate register class; i.e., a class for which
	     every register in it is better than using memory.  If adding a
	     class would make a smaller class (i.e., no union of just those
	     classes exists), skip that class.  The major unions of classes
	     should be provided as a register class.  Don't do this if we
	     will be doing it again later.  */

	  if ((pass == 1  || dump_file) || ! flag_expensive_optimizations)
	    for (class = 0; class < N_REG_CLASSES; class++)
	      if (p->cost[class] < p->mem_cost
		  && (reg_class_size[(int) reg_class_subunion[(int) alt][class]]
		      > reg_class_size[(int) alt])
#ifdef FORBIDDEN_INC_DEC_CLASSES
		  && ! (in_inc_dec[i] && forbidden_inc_dec_class[class])
#endif
#ifdef CANNOT_CHANGE_MODE_CLASS
		  && ! invalid_mode_change_p (i, (enum reg_class) class,
					      PSEUDO_REGNO_MODE (i))
#endif
		  )
		alt = reg_class_subunion[(int) alt][class];

	  /* If we don't add any classes, nothing to try.  */
	  if (alt == best)
	    alt = NO_REGS;

	  if (dump_file
	      && (reg_pref[i].prefclass != (int) best
		  || reg_pref[i].altclass != (int) alt))
	    {
	      fprintf (dump_file, "  Register %i", i);
	      if (alt == ALL_REGS || best == ALL_REGS)
		fprintf (dump_file, " pref %s\n", reg_class_names[(int) best]);
	      else if (alt == NO_REGS)
		fprintf (dump_file, " pref %s or none\n", reg_class_names[(int) best]);
	      else
		fprintf (dump_file, " pref %s, else %s\n",
			 reg_class_names[(int) best],
			 reg_class_names[(int) alt]);
	    }

	  /* We cast to (int) because (char) hits bugs in some compilers.  */
	  reg_pref[i].prefclass = (int) best;
	  reg_pref[i].altclass = (int) alt;
	}
    }

#ifdef FORBIDDEN_INC_DEC_CLASSES
  free (in_inc_dec);
#endif
  free (costs);
}

/* Record the cost of using memory or registers of various classes for
   the operands in INSN.

   N_ALTS is the number of alternatives.

   N_OPS is the number of operands.

   OPS is an array of the operands.

   MODES are the modes of the operands, in case any are VOIDmode.

   CONSTRAINTS are the constraints to use for the operands.  This array
   is modified by this procedure.

   This procedure works alternative by alternative.  For each alternative
   we assume that we will be able to allocate all pseudos to their ideal
   register class and calculate the cost of using that alternative.  Then
   we compute for each operand that is a pseudo-register, the cost of
   having the pseudo allocated to each register class and using it in that
   alternative.  To this cost is added the cost of the alternative.

   The cost of each class for this insn is its lowest cost among all the
   alternatives.  */

static void
record_reg_classes (int n_alts, int n_ops, rtx *ops,
		    enum machine_mode *modes, const char **constraints,
		    rtx insn, struct costs *op_costs,
		    struct reg_pref *reg_pref)
{
  int alt;
  int i, j;
  rtx set;

  /* Process each alternative, each time minimizing an operand's cost with
     the cost for each operand in that alternative.  */

  for (alt = 0; alt < n_alts; alt++)
    {
      struct costs this_op_costs[MAX_RECOG_OPERANDS];
      int alt_fail = 0;
      int alt_cost = 0;
      enum reg_class classes[MAX_RECOG_OPERANDS];
      int allows_mem[MAX_RECOG_OPERANDS];
      int class;

      for (i = 0; i < n_ops; i++)
	{
	  const char *p = constraints[i];
	  rtx op = ops[i];
	  enum machine_mode mode = modes[i];
	  int allows_addr = 0;
	  int win = 0;
	  unsigned char c;

	  /* Initially show we know nothing about the register class.  */
	  classes[i] = NO_REGS;
	  allows_mem[i] = 0;

	  /* If this operand has no constraints at all, we can conclude
	     nothing about it since anything is valid.  */

	  if (*p == 0)
	    {
	      if (REG_P (op) && REGNO (op) >= FIRST_PSEUDO_REGISTER)
		memset (&this_op_costs[i], 0, sizeof this_op_costs[i]);

	      continue;
	    }

	  /* If this alternative is only relevant when this operand
	     matches a previous operand, we do different things depending
	     on whether this operand is a pseudo-reg or not.  We must process
	     any modifiers for the operand before we can make this test.  */

	  while (*p == '%' || *p == '=' || *p == '+' || *p == '&')
	    p++;

	  if (p[0] >= '0' && p[0] <= '0' + i && (p[1] == ',' || p[1] == 0))
	    {
	      /* Copy class and whether memory is allowed from the matching
		 alternative.  Then perform any needed cost computations
		 and/or adjustments.  */
	      j = p[0] - '0';
	      classes[i] = classes[j];
	      allows_mem[i] = allows_mem[j];

	      if (!REG_P (op) || REGNO (op) < FIRST_PSEUDO_REGISTER)
		{
		  /* If this matches the other operand, we have no added
		     cost and we win.  */
		  if (rtx_equal_p (ops[j], op))
		    win = 1;

		  /* If we can put the other operand into a register, add to
		     the cost of this alternative the cost to copy this
		     operand to the register used for the other operand.  */

		  else if (classes[j] != NO_REGS)
		    {
		      alt_cost += copy_cost (op, mode, classes[j], 1, NULL);
		      win = 1;
		    }
		}
	      else if (!REG_P (ops[j])
		       || REGNO (ops[j]) < FIRST_PSEUDO_REGISTER)
		{
		  /* This op is a pseudo but the one it matches is not.  */

		  /* If we can't put the other operand into a register, this
		     alternative can't be used.  */

		  if (classes[j] == NO_REGS)
		    alt_fail = 1;

		  /* Otherwise, add to the cost of this alternative the cost
		     to copy the other operand to the register used for this
		     operand.  */

		  else
		    alt_cost += copy_cost (ops[j], mode, classes[j], 1, NULL);
		}
	      else
		{
		  /* The costs of this operand are not the same as the other
		     operand since move costs are not symmetric.  Moreover,
		     if we cannot tie them, this alternative needs to do a
		     copy, which is one instruction.  */

		  struct costs *pp = &this_op_costs[i];

		  for (class = 0; class < N_REG_CLASSES; class++)
		    pp->cost[class]
		      = ((recog_data.operand_type[i] != OP_OUT
			  ? may_move_in_cost[mode][class][(int) classes[i]]
			  : 0)
			 + (recog_data.operand_type[i] != OP_IN
			    ? may_move_out_cost[mode][(int) classes[i]][class]
			    : 0));

		  /* If the alternative actually allows memory, make things
		     a bit cheaper since we won't need an extra insn to
		     load it.  */

		  pp->mem_cost
		    = ((recog_data.operand_type[i] != OP_IN
		        ? MEMORY_MOVE_COST (mode, classes[i], 0)
			: 0)
		       + (recog_data.operand_type[i] != OP_OUT
			  ? MEMORY_MOVE_COST (mode, classes[i], 1)
			  : 0) - allows_mem[i]);

		  /* If we have assigned a class to this register in our
		     first pass, add a cost to this alternative corresponding
		     to what we would add if this register were not in the
		     appropriate class.  */

		  if (reg_pref && reg_pref[REGNO (op)].prefclass != NO_REGS)
		    alt_cost
		      += (may_move_in_cost[mode]
			  [(unsigned char) reg_pref[REGNO (op)].prefclass]
			  [(int) classes[i]]);

		  if (REGNO (ops[i]) != REGNO (ops[j])
		      && ! find_reg_note (insn, REG_DEAD, op))
		    alt_cost += 2;

		  /* This is in place of ordinary cost computation
		     for this operand, so skip to the end of the
		     alternative (should be just one character).  */
		  while (*p && *p++ != ',')
		    ;

		  constraints[i] = p;
		  continue;
		}
	    }

	  /* Scan all the constraint letters.  See if the operand matches
	     any of the constraints.  Collect the valid register classes
	     and see if this operand accepts memory.  */

	  while ((c = *p))
	    {
	      switch (c)
		{
		case ',':
		  break;
		case '*':
		  /* Ignore the next letter for this pass.  */
		  c = *++p;
		  break;

		case '?':
		  alt_cost += 2;
		case '!':  case '#':  case '&':
		case '0':  case '1':  case '2':  case '3':  case '4':
		case '5':  case '6':  case '7':  case '8':  case '9':
		  break;

		case 'p':
		  allows_addr = 1;
		  win = address_operand (op, GET_MODE (op));
		  /* We know this operand is an address, so we want it to be
		     allocated to a register that can be the base of an
		     address, i.e. BASE_REG_CLASS.  */
		  classes[i]
		    = reg_class_subunion[(int) classes[i]]
		    [(int) base_reg_class (VOIDmode, ADDRESS, SCRATCH)];
		  break;

		case 'm':  case 'o':  case 'V':
		  /* It doesn't seem worth distinguishing between offsettable
		     and non-offsettable addresses here.  */
		  allows_mem[i] = 1;
		  if (MEM_P (op))
		    win = 1;
		  break;

		case '<':
		  if (MEM_P (op)
		      && (GET_CODE (XEXP (op, 0)) == PRE_DEC
			  || GET_CODE (XEXP (op, 0)) == POST_DEC))
		    win = 1;
		  break;

		case '>':
		  if (MEM_P (op)
		      && (GET_CODE (XEXP (op, 0)) == PRE_INC
			  || GET_CODE (XEXP (op, 0)) == POST_INC))
		    win = 1;
		  break;

		case 'E':
		case 'F':
		  if (GET_CODE (op) == CONST_DOUBLE
		      || (GET_CODE (op) == CONST_VECTOR
			  && (GET_MODE_CLASS (GET_MODE (op))
			      == MODE_VECTOR_FLOAT)))
		    win = 1;
		  break;

		case 'G':
		case 'H':
		  if (GET_CODE (op) == CONST_DOUBLE
		      && CONST_DOUBLE_OK_FOR_CONSTRAINT_P (op, c, p))
		    win = 1;
		  break;

		case 's':
		  if (GET_CODE (op) == CONST_INT
		      || (GET_CODE (op) == CONST_DOUBLE
			  && GET_MODE (op) == VOIDmode))
		    break;
		case 'i':
		  if (CONSTANT_P (op)
		      && (! flag_pic || LEGITIMATE_PIC_OPERAND_P (op)))
		    win = 1;
		  break;

		case 'n':
		  if (GET_CODE (op) == CONST_INT
		      || (GET_CODE (op) == CONST_DOUBLE
			  && GET_MODE (op) == VOIDmode))
		    win = 1;
		  break;

		case 'I':
		case 'J':
		case 'K':
		case 'L':
		case 'M':
		case 'N':
		case 'O':
		case 'P':
		  if (GET_CODE (op) == CONST_INT
		      && CONST_OK_FOR_CONSTRAINT_P (INTVAL (op), c, p))
		    win = 1;
		  break;

		case 'X':
		  win = 1;
		  break;

		case 'g':
		  if (MEM_P (op)
		      || (CONSTANT_P (op)
			  && (! flag_pic || LEGITIMATE_PIC_OPERAND_P (op))))
		    win = 1;
		  allows_mem[i] = 1;
		case 'r':
		  classes[i]
		    = reg_class_subunion[(int) classes[i]][(int) GENERAL_REGS];
		  break;

		default:
		  if (REG_CLASS_FROM_CONSTRAINT (c, p) != NO_REGS)
		    classes[i]
		      = reg_class_subunion[(int) classes[i]]
			[(int) REG_CLASS_FROM_CONSTRAINT (c, p)];
#ifdef EXTRA_CONSTRAINT_STR
		  else if (EXTRA_CONSTRAINT_STR (op, c, p))
		    win = 1;

		  if (EXTRA_MEMORY_CONSTRAINT (c, p))
		    {
		      /* Every MEM can be reloaded to fit.  */
		      allows_mem[i] = 1;
		      if (MEM_P (op))
			win = 1;
		    }
		  if (EXTRA_ADDRESS_CONSTRAINT (c, p))
		    {
		      /* Every address can be reloaded to fit.  */
		      allows_addr = 1;
		      if (address_operand (op, GET_MODE (op)))
			win = 1;
		      /* We know this operand is an address, so we want it to
			 be allocated to a register that can be the base of an
			 address, i.e. BASE_REG_CLASS.  */
		      classes[i]
			= reg_class_subunion[(int) classes[i]]
			[(int) base_reg_class (VOIDmode, ADDRESS, SCRATCH)];
		    }
#endif
		  break;
		}
	      p += CONSTRAINT_LEN (c, p);
	      if (c == ',')
		break;
	    }

	  constraints[i] = p;

	  /* How we account for this operand now depends on whether it is  a
	     pseudo register or not.  If it is, we first check if any
	     register classes are valid.  If not, we ignore this alternative,
	     since we want to assume that all pseudos get allocated for
	     register preferencing.  If some register class is valid, compute
	     the costs of moving the pseudo into that class.  */

	  if (REG_P (op) && REGNO (op) >= FIRST_PSEUDO_REGISTER)
	    {
	      if (classes[i] == NO_REGS)
		{
		  /* We must always fail if the operand is a REG, but
		     we did not find a suitable class.

		     Otherwise we may perform an uninitialized read
		     from this_op_costs after the `continue' statement
		     below.  */
		  alt_fail = 1;
		}
	      else
		{
		  struct costs *pp = &this_op_costs[i];

		  for (class = 0; class < N_REG_CLASSES; class++)
		    pp->cost[class]
		      = ((recog_data.operand_type[i] != OP_OUT
			  ? may_move_in_cost[mode][class][(int) classes[i]]
			  : 0)
			 + (recog_data.operand_type[i] != OP_IN
			    ? may_move_out_cost[mode][(int) classes[i]][class]
			    : 0));

		  /* If the alternative actually allows memory, make things
		     a bit cheaper since we won't need an extra insn to
		     load it.  */

		  pp->mem_cost
		    = ((recog_data.operand_type[i] != OP_IN
		        ? MEMORY_MOVE_COST (mode, classes[i], 0)
			: 0)
		       + (recog_data.operand_type[i] != OP_OUT
			  ? MEMORY_MOVE_COST (mode, classes[i], 1)
			  : 0) - allows_mem[i]);

		  /* If we have assigned a class to this register in our
		     first pass, add a cost to this alternative corresponding
		     to what we would add if this register were not in the
		     appropriate class.  */

		  if (reg_pref && reg_pref[REGNO (op)].prefclass != NO_REGS)
		    alt_cost
		      += (may_move_in_cost[mode]
			  [(unsigned char) reg_pref[REGNO (op)].prefclass]
			  [(int) classes[i]]);
		}
	    }

	  /* Otherwise, if this alternative wins, either because we
	     have already determined that or if we have a hard register of
	     the proper class, there is no cost for this alternative.  */

	  else if (win
		   || (REG_P (op)
		       && reg_fits_class_p (op, classes[i], 0, GET_MODE (op))))
	    ;

	  /* If registers are valid, the cost of this alternative includes
	     copying the object to and/or from a register.  */

	  else if (classes[i] != NO_REGS)
	    {
	      if (recog_data.operand_type[i] != OP_OUT)
		alt_cost += copy_cost (op, mode, classes[i], 1, NULL);

	      if (recog_data.operand_type[i] != OP_IN)
		alt_cost += copy_cost (op, mode, classes[i], 0, NULL);
	    }

	  /* The only other way this alternative can be used is if this is a
	     constant that could be placed into memory.  */

	  else if (CONSTANT_P (op) && (allows_addr || allows_mem[i]))
	    alt_cost += MEMORY_MOVE_COST (mode, classes[i], 1);
	  else
	    alt_fail = 1;
	}

      if (alt_fail)
	continue;

      /* Finally, update the costs with the information we've calculated
	 about this alternative.  */

      for (i = 0; i < n_ops; i++)
	if (REG_P (ops[i])
	    && REGNO (ops[i]) >= FIRST_PSEUDO_REGISTER)
	  {
	    struct costs *pp = &op_costs[i], *qq = &this_op_costs[i];
	    int scale = 1 + (recog_data.operand_type[i] == OP_INOUT);

	    pp->mem_cost = MIN (pp->mem_cost,
				(qq->mem_cost + alt_cost) * scale);

	    for (class = 0; class < N_REG_CLASSES; class++)
	      pp->cost[class] = MIN (pp->cost[class],
				     (qq->cost[class] + alt_cost) * scale);
	  }
    }

  /* If this insn is a single set copying operand 1 to operand 0
     and one operand is a pseudo with the other a hard reg or a pseudo
     that prefers a register that is in its own register class then
     we may want to adjust the cost of that register class to -1.

     Avoid the adjustment if the source does not die to avoid stressing of
     register allocator by preferrencing two colliding registers into single
     class.

     Also avoid the adjustment if a copy between registers of the class
     is expensive (ten times the cost of a default copy is considered
     arbitrarily expensive).  This avoids losing when the preferred class
     is very expensive as the source of a copy instruction.  */

  if ((set = single_set (insn)) != 0
      && ops[0] == SET_DEST (set) && ops[1] == SET_SRC (set)
      && REG_P (ops[0]) && REG_P (ops[1])
      && find_regno_note (insn, REG_DEAD, REGNO (ops[1])))
    for (i = 0; i <= 1; i++)
      if (REGNO (ops[i]) >= FIRST_PSEUDO_REGISTER)
	{
	  unsigned int regno = REGNO (ops[!i]);
	  enum machine_mode mode = GET_MODE (ops[!i]);
	  int class;
	  unsigned int nr;

	  if (regno >= FIRST_PSEUDO_REGISTER && reg_pref != 0
	      && reg_pref[regno].prefclass != NO_REGS)
	    {
	      enum reg_class pref = reg_pref[regno].prefclass;

	      if ((reg_class_size[(unsigned char) pref]
		   == (unsigned) CLASS_MAX_NREGS (pref, mode))
		  && REGISTER_MOVE_COST (mode, pref, pref) < 10 * 2)
		op_costs[i].cost[(unsigned char) pref] = -1;
	    }
	  else if (regno < FIRST_PSEUDO_REGISTER)
	    for (class = 0; class < N_REG_CLASSES; class++)
	      if (TEST_HARD_REG_BIT (reg_class_contents[class], regno)
		  && reg_class_size[class] == (unsigned) CLASS_MAX_NREGS (class, mode))
		{
		  if (reg_class_size[class] == 1)
		    op_costs[i].cost[class] = -1;
		  else
		    {
		      for (nr = 0; nr < (unsigned) hard_regno_nregs[regno][mode]; nr++)
			{
			  if (! TEST_HARD_REG_BIT (reg_class_contents[class],
						   regno + nr))
			    break;
			}

		      if (nr == (unsigned) hard_regno_nregs[regno][mode])
			op_costs[i].cost[class] = -1;
		    }
		}
	}
}

/* Compute the cost of loading X into (if TO_P is nonzero) or from (if
   TO_P is zero) a register of class CLASS in mode MODE.

   X must not be a pseudo.  */

static int
copy_cost (rtx x, enum machine_mode mode, enum reg_class class, int to_p,
	   secondary_reload_info *prev_sri)
{
  enum reg_class secondary_class = NO_REGS;
  secondary_reload_info sri;

  /* If X is a SCRATCH, there is actually nothing to move since we are
     assuming optimal allocation.  */

  if (GET_CODE (x) == SCRATCH)
    return 0;

  /* Get the class we will actually use for a reload.  */
  class = PREFERRED_RELOAD_CLASS (x, class);

  /* If we need a secondary reload for an intermediate, the
     cost is that to load the input into the intermediate register, then
     to copy it.  */

  sri.prev_sri = prev_sri;
  sri.extra_cost = 0;
  secondary_class = targetm.secondary_reload (to_p, x, class, mode, &sri);

  if (secondary_class != NO_REGS)
    return (move_cost[mode][(int) secondary_class][(int) class]
	    + sri.extra_cost
	    + copy_cost (x, mode, secondary_class, to_p, &sri));

  /* For memory, use the memory move cost, for (hard) registers, use the
     cost to move between the register classes, and use 2 for everything
     else (constants).  */

  if (MEM_P (x) || class == NO_REGS)
    return sri.extra_cost + MEMORY_MOVE_COST (mode, class, to_p);

  else if (REG_P (x))
    return (sri.extra_cost
	    + move_cost[mode][(int) REGNO_REG_CLASS (REGNO (x))][(int) class]);

  else
    /* If this is a constant, we may eventually want to call rtx_cost here.  */
    return sri.extra_cost + COSTS_N_INSNS (1);
}

/* Record the pseudo registers we must reload into hard registers
   in a subexpression of a memory address, X.

   If CONTEXT is 0, we are looking at the base part of an address, otherwise we
   are looking at the index part.

   MODE is the mode of the memory reference; OUTER_CODE and INDEX_CODE
   give the context that the rtx appears in.  These three arguments are
   passed down to base_reg_class.

   SCALE is twice the amount to multiply the cost by (it is twice so we
   can represent half-cost adjustments).  */

static void
record_address_regs (enum machine_mode mode, rtx x, int context,
		     enum rtx_code outer_code, enum rtx_code index_code,
		     int scale)
{
  enum rtx_code code = GET_CODE (x);
  enum reg_class class;

  if (context == 1)
    class = INDEX_REG_CLASS;
  else
    class = base_reg_class (mode, outer_code, index_code);

  switch (code)
    {
    case CONST_INT:
    case CONST:
    case CC0:
    case PC:
    case SYMBOL_REF:
    case LABEL_REF:
      return;

    case PLUS:
      /* When we have an address that is a sum,
	 we must determine whether registers are "base" or "index" regs.
	 If there is a sum of two registers, we must choose one to be
	 the "base".  Luckily, we can use the REG_POINTER to make a good
	 choice most of the time.  We only need to do this on machines
	 that can have two registers in an address and where the base
	 and index register classes are different.

	 ??? This code used to set REGNO_POINTER_FLAG in some cases, but
	 that seems bogus since it should only be set when we are sure
	 the register is being used as a pointer.  */

      {
	rtx arg0 = XEXP (x, 0);
	rtx arg1 = XEXP (x, 1);
	enum rtx_code code0 = GET_CODE (arg0);
	enum rtx_code code1 = GET_CODE (arg1);

	/* Look inside subregs.  */
	if (code0 == SUBREG)
	  arg0 = SUBREG_REG (arg0), code0 = GET_CODE (arg0);
	if (code1 == SUBREG)
	  arg1 = SUBREG_REG (arg1), code1 = GET_CODE (arg1);

	/* If this machine only allows one register per address, it must
	   be in the first operand.  */

	if (MAX_REGS_PER_ADDRESS == 1)
	  record_address_regs (mode, arg0, 0, PLUS, code1, scale);

	/* If index and base registers are the same on this machine, just
	   record registers in any non-constant operands.  We assume here,
	   as well as in the tests below, that all addresses are in
	   canonical form.  */

	else if (INDEX_REG_CLASS == base_reg_class (VOIDmode, PLUS, SCRATCH))
	  {
	    record_address_regs (mode, arg0, context, PLUS, code1, scale);
	    if (! CONSTANT_P (arg1))
	      record_address_regs (mode, arg1, context, PLUS, code0, scale);
	  }

	/* If the second operand is a constant integer, it doesn't change
	   what class the first operand must be.  */

	else if (code1 == CONST_INT || code1 == CONST_DOUBLE)
	  record_address_regs (mode, arg0, context, PLUS, code1, scale);

	/* If the second operand is a symbolic constant, the first operand
	   must be an index register.  */

	else if (code1 == SYMBOL_REF || code1 == CONST || code1 == LABEL_REF)
	  record_address_regs (mode, arg0, 1, PLUS, code1, scale);

	/* If both operands are registers but one is already a hard register
	   of index or reg-base class, give the other the class that the
	   hard register is not.  */

	else if (code0 == REG && code1 == REG
		 && REGNO (arg0) < FIRST_PSEUDO_REGISTER
		 && (ok_for_base_p_nonstrict (arg0, mode, PLUS, REG)
		     || ok_for_index_p_nonstrict (arg0)))
	  record_address_regs (mode, arg1,
			       ok_for_base_p_nonstrict (arg0, mode, PLUS, REG)
			       ? 1 : 0,
			       PLUS, REG, scale);
	else if (code0 == REG && code1 == REG
		 && REGNO (arg1) < FIRST_PSEUDO_REGISTER
		 && (ok_for_base_p_nonstrict (arg1, mode, PLUS, REG)
		     || ok_for_index_p_nonstrict (arg1)))
	  record_address_regs (mode, arg0,
			       ok_for_base_p_nonstrict (arg1, mode, PLUS, REG)
			       ? 1 : 0,
			       PLUS, REG, scale);

	/* If one operand is known to be a pointer, it must be the base
	   with the other operand the index.  Likewise if the other operand
	   is a MULT.  */

	else if ((code0 == REG && REG_POINTER (arg0))
		 || code1 == MULT)
	  {
	    record_address_regs (mode, arg0, 0, PLUS, code1, scale);
	    record_address_regs (mode, arg1, 1, PLUS, code0, scale);
	  }
	else if ((code1 == REG && REG_POINTER (arg1))
		 || code0 == MULT)
	  {
	    record_address_regs (mode, arg0, 1, PLUS, code1, scale);
	    record_address_regs (mode, arg1, 0, PLUS, code0, scale);
	  }

	/* Otherwise, count equal chances that each might be a base
	   or index register.  This case should be rare.  */

	else
	  {
	    record_address_regs (mode, arg0, 0, PLUS, code1, scale / 2);
	    record_address_regs (mode, arg0, 1, PLUS, code1, scale / 2);
	    record_address_regs (mode, arg1, 0, PLUS, code0, scale / 2);
	    record_address_regs (mode, arg1, 1, PLUS, code0, scale / 2);
	  }
      }
      break;

      /* Double the importance of a pseudo register that is incremented
	 or decremented, since it would take two extra insns
	 if it ends up in the wrong place.  */
    case POST_MODIFY:
    case PRE_MODIFY:
      record_address_regs (mode, XEXP (x, 0), 0, code,
			   GET_CODE (XEXP (XEXP (x, 1), 1)), 2 * scale);
      if (REG_P (XEXP (XEXP (x, 1), 1)))
	record_address_regs (mode, XEXP (XEXP (x, 1), 1), 1, code, REG,
			     2 * scale);
      break;

    case POST_INC:
    case PRE_INC:
    case POST_DEC:
    case PRE_DEC:
      /* Double the importance of a pseudo register that is incremented
	 or decremented, since it would take two extra insns
	 if it ends up in the wrong place.  If the operand is a pseudo,
	 show it is being used in an INC_DEC context.  */

#ifdef FORBIDDEN_INC_DEC_CLASSES
      if (REG_P (XEXP (x, 0))
	  && REGNO (XEXP (x, 0)) >= FIRST_PSEUDO_REGISTER)
	in_inc_dec[REGNO (XEXP (x, 0))] = 1;
#endif

      record_address_regs (mode, XEXP (x, 0), 0, code, SCRATCH, 2 * scale);
      break;

    case REG:
      {
	struct costs *pp = &costs[REGNO (x)];
	int i;

	pp->mem_cost += (MEMORY_MOVE_COST (Pmode, class, 1) * scale) / 2;

	for (i = 0; i < N_REG_CLASSES; i++)
	  pp->cost[i] += (may_move_in_cost[Pmode][i][(int) class] * scale) / 2;
      }
      break;

    default:
      {
	const char *fmt = GET_RTX_FORMAT (code);
	int i;
	for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	  if (fmt[i] == 'e')
	    record_address_regs (mode, XEXP (x, i), context, code, SCRATCH,
				 scale);
      }
    }
}

#ifdef FORBIDDEN_INC_DEC_CLASSES

/* Return 1 if REG is valid as an auto-increment memory reference
   to an object of MODE.  */

static int
auto_inc_dec_reg_p (rtx reg, enum machine_mode mode)
{
  if (HAVE_POST_INCREMENT
      && memory_address_p (mode, gen_rtx_POST_INC (Pmode, reg)))
    return 1;

  if (HAVE_POST_DECREMENT
      && memory_address_p (mode, gen_rtx_POST_DEC (Pmode, reg)))
    return 1;

  if (HAVE_PRE_INCREMENT
      && memory_address_p (mode, gen_rtx_PRE_INC (Pmode, reg)))
    return 1;

  if (HAVE_PRE_DECREMENT
      && memory_address_p (mode, gen_rtx_PRE_DEC (Pmode, reg)))
    return 1;

  return 0;
}
#endif

static short *renumber;
static size_t regno_allocated;
static unsigned int reg_n_max;

/* Allocate enough space to hold NUM_REGS registers for the tables used for
   reg_scan and flow_analysis that are indexed by the register number.  If
   NEW_P is nonzero, initialize all of the registers, otherwise only
   initialize the new registers allocated.  The same table is kept from
   function to function, only reallocating it when we need more room.  If
   RENUMBER_P is nonzero, allocate the reg_renumber array also.  */

void
allocate_reg_info (size_t num_regs, int new_p, int renumber_p)
{
  size_t size_info;
  size_t size_renumber;
  size_t min = (new_p) ? 0 : reg_n_max;
  struct reg_info_data *reg_data;

  if (num_regs > regno_allocated)
    {
      size_t old_allocated = regno_allocated;

      regno_allocated = num_regs + (num_regs / 20);	/* Add some slop space.  */
      size_renumber = regno_allocated * sizeof (short);

      if (!reg_n_info)
	{
	  reg_n_info = VEC_alloc (reg_info_p, heap, regno_allocated);
	  VEC_safe_grow (reg_info_p, heap, reg_n_info, regno_allocated);
	  memset (VEC_address (reg_info_p, reg_n_info), 0,
		  sizeof (reg_info_p) * regno_allocated);
	  renumber = xmalloc (size_renumber);
	  reg_pref_buffer = XNEWVEC (struct reg_pref, regno_allocated);
	}
      else
	{
	  size_t old_length = VEC_length (reg_info_p, reg_n_info);
	  if (old_length < regno_allocated)
	    {
	      reg_info_p *addr;
	      VEC_safe_grow (reg_info_p, heap, reg_n_info, regno_allocated);
	      addr = VEC_address (reg_info_p, reg_n_info);
	      memset (&addr[old_length], 0,
		      sizeof (reg_info_p) * (regno_allocated - old_length));
	    }
	  else if (regno_allocated < old_length)
	    {
	      VEC_truncate (reg_info_p, reg_n_info, regno_allocated);
	    }

	  if (new_p)		/* If we're zapping everything, no need to realloc.  */
	    {
	      free ((char *) renumber);
	      free ((char *) reg_pref);
	      renumber = xmalloc (size_renumber);
	      reg_pref_buffer = XNEWVEC (struct reg_pref, regno_allocated);
	    }

	  else
	    {
	      renumber = xrealloc (renumber, size_renumber);
	      reg_pref_buffer = (struct reg_pref *) xrealloc (reg_pref_buffer,
					  regno_allocated
					  * sizeof (struct reg_pref));
	    }
	}

      size_info = (regno_allocated - old_allocated) * sizeof (reg_info)
	+ sizeof (struct reg_info_data) - sizeof (reg_info);
      reg_data = xcalloc (size_info, 1);
      reg_data->min_index = old_allocated;
      reg_data->max_index = regno_allocated - 1;
      reg_data->next = reg_info_head;
      reg_info_head = reg_data;
    }

  reg_n_max = num_regs;
  if (min < num_regs)
    {
      /* Loop through each of the segments allocated for the actual
	 reg_info pages, and set up the pointers, zero the pages, etc.  */
      for (reg_data = reg_info_head;
	   reg_data && reg_data->max_index >= min;
	   reg_data = reg_data->next)
	{
	  size_t min_index = reg_data->min_index;
	  size_t max_index = reg_data->max_index;
	  size_t max = MIN (max_index, num_regs);
	  size_t local_min = min - min_index;
	  size_t i;

	  if (reg_data->min_index > num_regs)
	    continue;

	  if (min < min_index)
	    local_min = 0;
	  if (!reg_data->used_p)	/* page just allocated with calloc */
	    reg_data->used_p = 1;	/* no need to zero */
	  else
	    memset (&reg_data->data[local_min], 0,
		    sizeof (reg_info) * (max - min_index - local_min + 1));

	  for (i = min_index+local_min; i <= max; i++)
	    {
	      VEC_replace (reg_info_p, reg_n_info, i,
			   &reg_data->data[i-min_index]);
	      REG_BASIC_BLOCK (i) = REG_BLOCK_UNKNOWN;
	      renumber[i] = -1;
	      reg_pref_buffer[i].prefclass = (char) NO_REGS;
	      reg_pref_buffer[i].altclass = (char) NO_REGS;
	    }
	}
    }

  /* If {pref,alt}class have already been allocated, update the pointers to
     the newly realloced ones.  */
  if (reg_pref)
    reg_pref = reg_pref_buffer;

  if (renumber_p)
    reg_renumber = renumber;
}

/* Free up the space allocated by allocate_reg_info.  */
void
free_reg_info (void)
{
  if (reg_n_info)
    {
      struct reg_info_data *reg_data;
      struct reg_info_data *reg_next;

      VEC_free (reg_info_p, heap, reg_n_info);
      for (reg_data = reg_info_head; reg_data; reg_data = reg_next)
	{
	  reg_next = reg_data->next;
	  free ((char *) reg_data);
	}

      free (reg_pref_buffer);
      reg_pref_buffer = (struct reg_pref *) 0;
      reg_info_head = (struct reg_info_data *) 0;
      renumber = (short *) 0;
    }
  regno_allocated = 0;
  reg_n_max = 0;
}

/* This is the `regscan' pass of the compiler, run just before cse
   and again just before loop.

   It finds the first and last use of each pseudo-register
   and records them in the vectors regno_first_uid, regno_last_uid
   and counts the number of sets in the vector reg_n_sets.

   REPEAT is nonzero the second time this is called.  */

/* Maximum number of parallel sets and clobbers in any insn in this fn.
   Always at least 3, since the combiner could put that many together
   and we want this to remain correct for all the remaining passes.
   This corresponds to the maximum number of times note_stores will call
   a function for any insn.  */

int max_parallel;

/* Used as a temporary to record the largest number of registers in
   PARALLEL in a SET_DEST.  This is added to max_parallel.  */

static int max_set_parallel;

void
reg_scan (rtx f, unsigned int nregs)
{
  rtx insn;

  timevar_push (TV_REG_SCAN);

  allocate_reg_info (nregs, TRUE, FALSE);
  max_parallel = 3;
  max_set_parallel = 0;

  for (insn = f; insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      {
	rtx pat = PATTERN (insn);
	if (GET_CODE (pat) == PARALLEL
	    && XVECLEN (pat, 0) > max_parallel)
	  max_parallel = XVECLEN (pat, 0);
	reg_scan_mark_refs (pat, insn, 0);

	if (REG_NOTES (insn))
	  reg_scan_mark_refs (REG_NOTES (insn), insn, 1);
      }

  max_parallel += max_set_parallel;

  timevar_pop (TV_REG_SCAN);
}

/* X is the expression to scan.  INSN is the insn it appears in.
   NOTE_FLAG is nonzero if X is from INSN's notes rather than its body.  */

static void
reg_scan_mark_refs (rtx x, rtx insn, int note_flag)
{
  enum rtx_code code;
  rtx dest;
  rtx note;

  if (!x)
    return;
  code = GET_CODE (x);
  switch (code)
    {
    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CC0:
    case PC:
    case SYMBOL_REF:
    case LABEL_REF:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return;

    case REG:
      {
	unsigned int regno = REGNO (x);

	if (!note_flag)
	  REGNO_LAST_UID (regno) = INSN_UID (insn);
	if (REGNO_FIRST_UID (regno) == 0)
	  REGNO_FIRST_UID (regno) = INSN_UID (insn);
      }
      break;

    case EXPR_LIST:
      if (XEXP (x, 0))
	reg_scan_mark_refs (XEXP (x, 0), insn, note_flag);
      if (XEXP (x, 1))
	reg_scan_mark_refs (XEXP (x, 1), insn, note_flag);
      break;

    case INSN_LIST:
      if (XEXP (x, 1))
	reg_scan_mark_refs (XEXP (x, 1), insn, note_flag);
      break;

    case CLOBBER:
      {
	rtx reg = XEXP (x, 0);
	if (REG_P (reg))
	  {
	    REG_N_SETS (REGNO (reg))++;
	    REG_N_REFS (REGNO (reg))++;
	  }
	else if (MEM_P (reg))
	  reg_scan_mark_refs (XEXP (reg, 0), insn, note_flag);
      }
      break;

    case SET:
      /* Count a set of the destination if it is a register.  */
      for (dest = SET_DEST (x);
	   GET_CODE (dest) == SUBREG || GET_CODE (dest) == STRICT_LOW_PART
	   || GET_CODE (dest) == ZERO_EXTEND;
	   dest = XEXP (dest, 0))
	;

      /* For a PARALLEL, record the number of things (less the usual one for a
	 SET) that are set.  */
      if (GET_CODE (dest) == PARALLEL)
	max_set_parallel = MAX (max_set_parallel, XVECLEN (dest, 0) - 1);

      if (REG_P (dest))
	{
	  REG_N_SETS (REGNO (dest))++;
	  REG_N_REFS (REGNO (dest))++;
	}

      /* If this is setting a pseudo from another pseudo or the sum of a
	 pseudo and a constant integer and the other pseudo is known to be
	 a pointer, set the destination to be a pointer as well.

	 Likewise if it is setting the destination from an address or from a
	 value equivalent to an address or to the sum of an address and
	 something else.

	 But don't do any of this if the pseudo corresponds to a user
	 variable since it should have already been set as a pointer based
	 on the type.  */

      if (REG_P (SET_DEST (x))
	  && REGNO (SET_DEST (x)) >= FIRST_PSEUDO_REGISTER
	  /* If the destination pseudo is set more than once, then other
	     sets might not be to a pointer value (consider access to a
	     union in two threads of control in the presence of global
	     optimizations).  So only set REG_POINTER on the destination
	     pseudo if this is the only set of that pseudo.  */
	  && REG_N_SETS (REGNO (SET_DEST (x))) == 1
	  && ! REG_USERVAR_P (SET_DEST (x))
	  && ! REG_POINTER (SET_DEST (x))
	  && ((REG_P (SET_SRC (x))
	       && REG_POINTER (SET_SRC (x)))
	      || ((GET_CODE (SET_SRC (x)) == PLUS
		   || GET_CODE (SET_SRC (x)) == LO_SUM)
		  && GET_CODE (XEXP (SET_SRC (x), 1)) == CONST_INT
		  && REG_P (XEXP (SET_SRC (x), 0))
		  && REG_POINTER (XEXP (SET_SRC (x), 0)))
	      || GET_CODE (SET_SRC (x)) == CONST
	      || GET_CODE (SET_SRC (x)) == SYMBOL_REF
	      || GET_CODE (SET_SRC (x)) == LABEL_REF
	      || (GET_CODE (SET_SRC (x)) == HIGH
		  && (GET_CODE (XEXP (SET_SRC (x), 0)) == CONST
		      || GET_CODE (XEXP (SET_SRC (x), 0)) == SYMBOL_REF
		      || GET_CODE (XEXP (SET_SRC (x), 0)) == LABEL_REF))
	      || ((GET_CODE (SET_SRC (x)) == PLUS
		   || GET_CODE (SET_SRC (x)) == LO_SUM)
		  && (GET_CODE (XEXP (SET_SRC (x), 1)) == CONST
		      || GET_CODE (XEXP (SET_SRC (x), 1)) == SYMBOL_REF
		      || GET_CODE (XEXP (SET_SRC (x), 1)) == LABEL_REF))
	      || ((note = find_reg_note (insn, REG_EQUAL, 0)) != 0
		  && (GET_CODE (XEXP (note, 0)) == CONST
		      || GET_CODE (XEXP (note, 0)) == SYMBOL_REF
		      || GET_CODE (XEXP (note, 0)) == LABEL_REF))))
	REG_POINTER (SET_DEST (x)) = 1;

      /* If this is setting a register from a register or from a simple
	 conversion of a register, propagate REG_EXPR.  */
      if (REG_P (dest))
	{
	  rtx src = SET_SRC (x);

	  while (GET_CODE (src) == SIGN_EXTEND
		 || GET_CODE (src) == ZERO_EXTEND
		 || GET_CODE (src) == TRUNCATE
		 || (GET_CODE (src) == SUBREG && subreg_lowpart_p (src)))
	    src = XEXP (src, 0);

	  if (!REG_ATTRS (dest) && REG_P (src))
	    REG_ATTRS (dest) = REG_ATTRS (src);
	  if (!REG_ATTRS (dest) && MEM_P (src))
	    set_reg_attrs_from_mem (dest, src);
	}

      /* ... fall through ...  */

    default:
      {
	const char *fmt = GET_RTX_FORMAT (code);
	int i;
	for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	  {
	    if (fmt[i] == 'e')
	      reg_scan_mark_refs (XEXP (x, i), insn, note_flag);
	    else if (fmt[i] == 'E' && XVEC (x, i) != 0)
	      {
		int j;
		for (j = XVECLEN (x, i) - 1; j >= 0; j--)
		  reg_scan_mark_refs (XVECEXP (x, i, j), insn, note_flag);
	      }
	  }
      }
    }
}

/* Return nonzero if C1 is a subset of C2, i.e., if every register in C1
   is also in C2.  */

int
reg_class_subset_p (enum reg_class c1, enum reg_class c2)
{
  if (c1 == c2) return 1;

  if (c2 == ALL_REGS)
  win:
    return 1;
  GO_IF_HARD_REG_SUBSET (reg_class_contents[(int) c1],
			 reg_class_contents[(int) c2],
			 win);
  return 0;
}

/* Return nonzero if there is a register that is in both C1 and C2.  */

int
reg_classes_intersect_p (enum reg_class c1, enum reg_class c2)
{
  HARD_REG_SET c;

  if (c1 == c2) return 1;

  if (c1 == ALL_REGS || c2 == ALL_REGS)
    return 1;

  COPY_HARD_REG_SET (c, reg_class_contents[(int) c1]);
  AND_HARD_REG_SET (c, reg_class_contents[(int) c2]);

  GO_IF_HARD_REG_SUBSET (c, reg_class_contents[(int) NO_REGS], lose);
  return 1;

 lose:
  return 0;
}

#ifdef CANNOT_CHANGE_MODE_CLASS

struct subregs_of_mode_node
{
  unsigned int block;
  unsigned char modes[MAX_MACHINE_MODE];
};

static htab_t subregs_of_mode;

static hashval_t
som_hash (const void *x)
{
  const struct subregs_of_mode_node *a = x;
  return a->block;
}

static int
som_eq (const void *x, const void *y)
{
  const struct subregs_of_mode_node *a = x;
  const struct subregs_of_mode_node *b = y;
  return a->block == b->block;
}

void
init_subregs_of_mode (void)
{
  if (subregs_of_mode)
    htab_empty (subregs_of_mode);
  else
    subregs_of_mode = htab_create (100, som_hash, som_eq, free);
}

void
record_subregs_of_mode (rtx subreg)
{
  struct subregs_of_mode_node dummy, *node;
  enum machine_mode mode;
  unsigned int regno;
  void **slot;

  if (!REG_P (SUBREG_REG (subreg)))
    return;

  regno = REGNO (SUBREG_REG (subreg));
  mode = GET_MODE (subreg);

  if (regno < FIRST_PSEUDO_REGISTER)
    return;

  dummy.block = regno & -8;
  slot = htab_find_slot_with_hash (subregs_of_mode, &dummy,
				   dummy.block, INSERT);
  node = *slot;
  if (node == NULL)
    {
      node = XCNEW (struct subregs_of_mode_node);
      node->block = regno & -8;
      *slot = node;
    }

  node->modes[mode] |= 1 << (regno & 7);
}

/* Set bits in *USED which correspond to registers which can't change
   their mode from FROM to any mode in which REGNO was encountered.  */

void
cannot_change_mode_set_regs (HARD_REG_SET *used, enum machine_mode from,
			     unsigned int regno)
{
  struct subregs_of_mode_node dummy, *node;
  enum machine_mode to;
  unsigned char mask;
  unsigned int i;

  dummy.block = regno & -8;
  node = htab_find_with_hash (subregs_of_mode, &dummy, dummy.block);
  if (node == NULL)
    return;

  mask = 1 << (regno & 7);
  for (to = VOIDmode; to < NUM_MACHINE_MODES; to++)
    if (node->modes[to] & mask)
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (!TEST_HARD_REG_BIT (*used, i)
	    && REG_CANNOT_CHANGE_MODE_P (i, from, to))
	  SET_HARD_REG_BIT (*used, i);
}

/* Return 1 if REGNO has had an invalid mode change in CLASS from FROM
   mode.  */

bool
invalid_mode_change_p (unsigned int regno, enum reg_class class,
		       enum machine_mode from)
{
  struct subregs_of_mode_node dummy, *node;
  enum machine_mode to;
  unsigned char mask;

  dummy.block = regno & -8;
  node = htab_find_with_hash (subregs_of_mode, &dummy, dummy.block);
  if (node == NULL)
    return false;

  mask = 1 << (regno & 7);
  for (to = VOIDmode; to < NUM_MACHINE_MODES; to++)
    if (node->modes[to] & mask)
      if (CANNOT_CHANGE_MODE_CLASS (from, to, class))
	return true;

  return false;
}
#endif /* CANNOT_CHANGE_MODE_CLASS */

#include "gt-regclass.h"
