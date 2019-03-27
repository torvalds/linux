/* Instruction scheduling pass.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com) Enhanced by,
   and currently maintained by, Jim Wilson (wilson@cygnus.com)

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

/* Instruction scheduling pass.  This file, along with sched-deps.c,
   contains the generic parts.  The actual entry point is found for
   the normal instruction scheduling pass is found in sched-rgn.c.

   We compute insn priorities based on data dependencies.  Flow
   analysis only creates a fraction of the data-dependencies we must
   observe: namely, only those dependencies which the combiner can be
   expected to use.  For this pass, we must therefore create the
   remaining dependencies we need to observe: register dependencies,
   memory dependencies, dependencies to keep function calls in order,
   and the dependence between a conditional branch and the setting of
   condition codes are all dealt with here.

   The scheduler first traverses the data flow graph, starting with
   the last instruction, and proceeding to the first, assigning values
   to insn_priority as it goes.  This sorts the instructions
   topologically by data dependence.

   Once priorities have been established, we order the insns using
   list scheduling.  This works as follows: starting with a list of
   all the ready insns, and sorted according to priority number, we
   schedule the insn from the end of the list by placing its
   predecessors in the list according to their priority order.  We
   consider this insn scheduled by setting the pointer to the "end" of
   the list to point to the previous insn.  When an insn has no
   predecessors, we either queue it until sufficient time has elapsed
   or add it to the ready list.  As the instructions are scheduled or
   when stalls are introduced, the queue advances and dumps insns into
   the ready list.  When all insns down to the lowest priority have
   been scheduled, the critical path of the basic block has been made
   as short as possible.  The remaining insns are then scheduled in
   remaining slots.

   The following list shows the order in which we want to break ties
   among insns in the ready list:

   1.  choose insn with the longest path to end of bb, ties
   broken by
   2.  choose insn with least contribution to register pressure,
   ties broken by
   3.  prefer in-block upon interblock motion, ties broken by
   4.  prefer useful upon speculative motion, ties broken by
   5.  choose insn with largest control flow probability, ties
   broken by
   6.  choose insn with the least dependences upon the previously
   scheduled insn, or finally
   7   choose the insn which has the most insns dependent on it.
   8.  choose insn with lowest UID.

   Memory references complicate matters.  Only if we can be certain
   that memory references are not part of the data dependency graph
   (via true, anti, or output dependence), can we move operations past
   memory references.  To first approximation, reads can be done
   independently, while writes introduce dependencies.  Better
   approximations will yield fewer dependencies.

   Before reload, an extended analysis of interblock data dependences
   is required for interblock scheduling.  This is performed in
   compute_block_backward_dependences ().

   Dependencies set up by memory references are treated in exactly the
   same way as other dependencies, by using LOG_LINKS backward
   dependences.  LOG_LINKS are translated into INSN_DEPEND forward
   dependences for the purpose of forward list scheduling.

   Having optimized the critical path, we may have also unduly
   extended the lifetimes of some registers.  If an operation requires
   that constants be loaded into registers, it is certainly desirable
   to load those constants as early as necessary, but no earlier.
   I.e., it will not do to load up a bunch of registers at the
   beginning of a basic block only to use them at the end, if they
   could be loaded later, since this may result in excessive register
   utilization.

   Note that since branches are never in basic blocks, but only end
   basic blocks, this pass will not move branches.  But that is ok,
   since we can use GNU's delayed branch scheduling pass to take care
   of this case.

   Also note that no further optimizations based on algebraic
   identities are performed, so this pass would be a good one to
   perform instruction splitting, such as breaking up a multiply
   instruction into shifts and adds where that is profitable.

   Given the memory aliasing analysis that this pass should perform,
   it should be possible to remove redundant stores to memory, and to
   load values from registers instead of hitting memory.

   Before reload, speculative insns are moved only if a 'proof' exists
   that no exception will be caused by this, and if no live registers
   exist that inhibit the motion (live registers constraints are not
   represented by data dependence edges).

   This pass must update information that subsequent passes expect to
   be correct.  Namely: reg_n_refs, reg_n_sets, reg_n_deaths,
   reg_n_calls_crossed, and reg_live_length.  Also, BB_HEAD, BB_END.

   The information in the line number notes is carefully retained by
   this pass.  Notes that refer to the starting and ending of
   exception regions are also carefully retained by this pass.  All
   other NOTE insns are grouped in their same relative order at the
   beginning of basic blocks and regions that have been scheduled.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "function.h"
#include "flags.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "except.h"
#include "toplev.h"
#include "recog.h"
#include "sched-int.h"
#include "target.h"
#include "output.h"
#include "params.h"

#ifdef INSN_SCHEDULING

/* issue_rate is the number of insns that can be scheduled in the same
   machine cycle.  It can be defined in the config/mach/mach.h file,
   otherwise we set it to 1.  */

static int issue_rate;

/* sched-verbose controls the amount of debugging output the
   scheduler prints.  It is controlled by -fsched-verbose=N:
   N>0 and no -DSR : the output is directed to stderr.
   N>=10 will direct the printouts to stderr (regardless of -dSR).
   N=1: same as -dSR.
   N=2: bb's probabilities, detailed ready list info, unit/insn info.
   N=3: rtl at abort point, control-flow, regions info.
   N=5: dependences info.  */

static int sched_verbose_param = 0;
int sched_verbose = 0;

/* Debugging file.  All printouts are sent to dump, which is always set,
   either to stderr, or to the dump listing file (-dRS).  */
FILE *sched_dump = 0;

/* Highest uid before scheduling.  */
static int old_max_uid;

/* fix_sched_param() is called from toplev.c upon detection
   of the -fsched-verbose=N option.  */

void
fix_sched_param (const char *param, const char *val)
{
  if (!strcmp (param, "verbose"))
    sched_verbose_param = atoi (val);
  else
    warning (0, "fix_sched_param: unknown param: %s", param);
}

struct haifa_insn_data *h_i_d;

#define LINE_NOTE(INSN)		(h_i_d[INSN_UID (INSN)].line_note)
#define INSN_TICK(INSN)		(h_i_d[INSN_UID (INSN)].tick)
#define INTER_TICK(INSN)        (h_i_d[INSN_UID (INSN)].inter_tick)

/* If INSN_TICK of an instruction is equal to INVALID_TICK,
   then it should be recalculated from scratch.  */
#define INVALID_TICK (-(max_insn_queue_index + 1))
/* The minimal value of the INSN_TICK of an instruction.  */
#define MIN_TICK (-max_insn_queue_index)

/* Issue points are used to distinguish between instructions in max_issue ().
   For now, all instructions are equally good.  */
#define ISSUE_POINTS(INSN) 1

/* Vector indexed by basic block number giving the starting line-number
   for each basic block.  */
static rtx *line_note_head;

/* List of important notes we must keep around.  This is a pointer to the
   last element in the list.  */
static rtx note_list;

static struct spec_info_def spec_info_var;
/* Description of the speculative part of the scheduling.
   If NULL - no speculation.  */
static spec_info_t spec_info;

/* True, if recovery block was added during scheduling of current block.
   Used to determine, if we need to fix INSN_TICKs.  */
static bool added_recovery_block_p;

/* Counters of different types of speculative instructions.  */
static int nr_begin_data, nr_be_in_data, nr_begin_control, nr_be_in_control;

/* Pointers to GLAT data.  See init_glat for more information.  */
regset *glat_start, *glat_end;

/* Array used in {unlink, restore}_bb_notes.  */
static rtx *bb_header = 0;

/* Number of basic_blocks.  */
static int old_last_basic_block;

/* Basic block after which recovery blocks will be created.  */
static basic_block before_recovery;

/* Queues, etc.  */

/* An instruction is ready to be scheduled when all insns preceding it
   have already been scheduled.  It is important to ensure that all
   insns which use its result will not be executed until its result
   has been computed.  An insn is maintained in one of four structures:

   (P) the "Pending" set of insns which cannot be scheduled until
   their dependencies have been satisfied.
   (Q) the "Queued" set of insns that can be scheduled when sufficient
   time has passed.
   (R) the "Ready" list of unscheduled, uncommitted insns.
   (S) the "Scheduled" list of insns.

   Initially, all insns are either "Pending" or "Ready" depending on
   whether their dependencies are satisfied.

   Insns move from the "Ready" list to the "Scheduled" list as they
   are committed to the schedule.  As this occurs, the insns in the
   "Pending" list have their dependencies satisfied and move to either
   the "Ready" list or the "Queued" set depending on whether
   sufficient time has passed to make them ready.  As time passes,
   insns move from the "Queued" set to the "Ready" list.

   The "Pending" list (P) are the insns in the INSN_DEPEND of the unscheduled
   insns, i.e., those that are ready, queued, and pending.
   The "Queued" set (Q) is implemented by the variable `insn_queue'.
   The "Ready" list (R) is implemented by the variables `ready' and
   `n_ready'.
   The "Scheduled" list (S) is the new insn chain built by this pass.

   The transition (R->S) is implemented in the scheduling loop in
   `schedule_block' when the best insn to schedule is chosen.
   The transitions (P->R and P->Q) are implemented in `schedule_insn' as
   insns move from the ready list to the scheduled list.
   The transition (Q->R) is implemented in 'queue_to_insn' as time
   passes or stalls are introduced.  */

/* Implement a circular buffer to delay instructions until sufficient
   time has passed.  For the new pipeline description interface,
   MAX_INSN_QUEUE_INDEX is a power of two minus one which is not less
   than maximal time of instruction execution computed by genattr.c on
   the base maximal time of functional unit reservations and getting a
   result.  This is the longest time an insn may be queued.  */

static rtx *insn_queue;
static int q_ptr = 0;
static int q_size = 0;
#define NEXT_Q(X) (((X)+1) & max_insn_queue_index)
#define NEXT_Q_AFTER(X, C) (((X)+C) & max_insn_queue_index)

#define QUEUE_SCHEDULED (-3)
#define QUEUE_NOWHERE   (-2)
#define QUEUE_READY     (-1)
/* QUEUE_SCHEDULED - INSN is scheduled.
   QUEUE_NOWHERE   - INSN isn't scheduled yet and is neither in
   queue or ready list.
   QUEUE_READY     - INSN is in ready list.
   N >= 0 - INSN queued for X [where NEXT_Q_AFTER (q_ptr, X) == N] cycles.  */
   
#define QUEUE_INDEX(INSN) (h_i_d[INSN_UID (INSN)].queue_index)

/* The following variable value refers for all current and future
   reservations of the processor units.  */
state_t curr_state;

/* The following variable value is size of memory representing all
   current and future reservations of the processor units.  */
static size_t dfa_state_size;

/* The following array is used to find the best insn from ready when
   the automaton pipeline interface is used.  */
static char *ready_try;

/* Describe the ready list of the scheduler.
   VEC holds space enough for all insns in the current region.  VECLEN
   says how many exactly.
   FIRST is the index of the element with the highest priority; i.e. the
   last one in the ready list, since elements are ordered by ascending
   priority.
   N_READY determines how many insns are on the ready list.  */

struct ready_list
{
  rtx *vec;
  int veclen;
  int first;
  int n_ready;
};

/* The pointer to the ready list.  */
static struct ready_list *readyp;

/* Scheduling clock.  */
static int clock_var;

/* Number of instructions in current scheduling region.  */
static int rgn_n_insns;

static int may_trap_exp (rtx, int);

/* Nonzero iff the address is comprised from at most 1 register.  */
#define CONST_BASED_ADDRESS_P(x)			\
  (REG_P (x)					\
   || ((GET_CODE (x) == PLUS || GET_CODE (x) == MINUS	\
	|| (GET_CODE (x) == LO_SUM))			\
       && (CONSTANT_P (XEXP (x, 0))			\
	   || CONSTANT_P (XEXP (x, 1)))))

/* Returns a class that insn with GET_DEST(insn)=x may belong to,
   as found by analyzing insn's expression.  */

static int
may_trap_exp (rtx x, int is_store)
{
  enum rtx_code code;

  if (x == 0)
    return TRAP_FREE;
  code = GET_CODE (x);
  if (is_store)
    {
      if (code == MEM && may_trap_p (x))
	return TRAP_RISKY;
      else
	return TRAP_FREE;
    }
  if (code == MEM)
    {
      /* The insn uses memory:  a volatile load.  */
      if (MEM_VOLATILE_P (x))
	return IRISKY;
      /* An exception-free load.  */
      if (!may_trap_p (x))
	return IFREE;
      /* A load with 1 base register, to be further checked.  */
      if (CONST_BASED_ADDRESS_P (XEXP (x, 0)))
	return PFREE_CANDIDATE;
      /* No info on the load, to be further checked.  */
      return PRISKY_CANDIDATE;
    }
  else
    {
      const char *fmt;
      int i, insn_class = TRAP_FREE;

      /* Neither store nor load, check if it may cause a trap.  */
      if (may_trap_p (x))
	return TRAP_RISKY;
      /* Recursive step: walk the insn...  */
      fmt = GET_RTX_FORMAT (code);
      for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	{
	  if (fmt[i] == 'e')
	    {
	      int tmp_class = may_trap_exp (XEXP (x, i), is_store);
	      insn_class = WORST_CLASS (insn_class, tmp_class);
	    }
	  else if (fmt[i] == 'E')
	    {
	      int j;
	      for (j = 0; j < XVECLEN (x, i); j++)
		{
		  int tmp_class = may_trap_exp (XVECEXP (x, i, j), is_store);
		  insn_class = WORST_CLASS (insn_class, tmp_class);
		  if (insn_class == TRAP_RISKY || insn_class == IRISKY)
		    break;
		}
	    }
	  if (insn_class == TRAP_RISKY || insn_class == IRISKY)
	    break;
	}
      return insn_class;
    }
}

/* Classifies insn for the purpose of verifying that it can be
   moved speculatively, by examining it's patterns, returning:
   TRAP_RISKY: store, or risky non-load insn (e.g. division by variable).
   TRAP_FREE: non-load insn.
   IFREE: load from a globally safe location.
   IRISKY: volatile load.
   PFREE_CANDIDATE, PRISKY_CANDIDATE: load that need to be checked for
   being either PFREE or PRISKY.  */

int
haifa_classify_insn (rtx insn)
{
  rtx pat = PATTERN (insn);
  int tmp_class = TRAP_FREE;
  int insn_class = TRAP_FREE;
  enum rtx_code code;

  if (GET_CODE (pat) == PARALLEL)
    {
      int i, len = XVECLEN (pat, 0);

      for (i = len - 1; i >= 0; i--)
	{
	  code = GET_CODE (XVECEXP (pat, 0, i));
	  switch (code)
	    {
	    case CLOBBER:
	      /* Test if it is a 'store'.  */
	      tmp_class = may_trap_exp (XEXP (XVECEXP (pat, 0, i), 0), 1);
	      break;
	    case SET:
	      /* Test if it is a store.  */
	      tmp_class = may_trap_exp (SET_DEST (XVECEXP (pat, 0, i)), 1);
	      if (tmp_class == TRAP_RISKY)
		break;
	      /* Test if it is a load.  */
	      tmp_class
		= WORST_CLASS (tmp_class,
			       may_trap_exp (SET_SRC (XVECEXP (pat, 0, i)),
					     0));
	      break;
	    case COND_EXEC:
	    case TRAP_IF:
	      tmp_class = TRAP_RISKY;
	      break;
	    default:
	      ;
	    }
	  insn_class = WORST_CLASS (insn_class, tmp_class);
	  if (insn_class == TRAP_RISKY || insn_class == IRISKY)
	    break;
	}
    }
  else
    {
      code = GET_CODE (pat);
      switch (code)
	{
	case CLOBBER:
	  /* Test if it is a 'store'.  */
	  tmp_class = may_trap_exp (XEXP (pat, 0), 1);
	  break;
	case SET:
	  /* Test if it is a store.  */
	  tmp_class = may_trap_exp (SET_DEST (pat), 1);
	  if (tmp_class == TRAP_RISKY)
	    break;
	  /* Test if it is a load.  */
	  tmp_class =
	    WORST_CLASS (tmp_class,
			 may_trap_exp (SET_SRC (pat), 0));
	  break;
	case COND_EXEC:
	case TRAP_IF:
	  tmp_class = TRAP_RISKY;
	  break;
	default:;
	}
      insn_class = tmp_class;
    }

  return insn_class;
}

/* Forward declarations.  */

HAIFA_INLINE static int insn_cost1 (rtx, enum reg_note, rtx, rtx);
static int priority (rtx);
static int rank_for_schedule (const void *, const void *);
static void swap_sort (rtx *, int);
static void queue_insn (rtx, int);
static int schedule_insn (rtx);
static int find_set_reg_weight (rtx);
static void find_insn_reg_weight (basic_block);
static void find_insn_reg_weight1 (rtx);
static void adjust_priority (rtx);
static void advance_one_cycle (void);

/* Notes handling mechanism:
   =========================
   Generally, NOTES are saved before scheduling and restored after scheduling.
   The scheduler distinguishes between three types of notes:

   (1) LINE_NUMBER notes, generated and used for debugging.  Here,
   before scheduling a region, a pointer to the LINE_NUMBER note is
   added to the insn following it (in save_line_notes()), and the note
   is removed (in rm_line_notes() and unlink_line_notes()).  After
   scheduling the region, this pointer is used for regeneration of
   the LINE_NUMBER note (in restore_line_notes()).

   (2) LOOP_BEGIN, LOOP_END, SETJMP, EHREGION_BEG, EHREGION_END notes:
   Before scheduling a region, a pointer to the note is added to the insn
   that follows or precedes it.  (This happens as part of the data dependence
   computation).  After scheduling an insn, the pointer contained in it is
   used for regenerating the corresponding note (in reemit_notes).

   (3) All other notes (e.g. INSN_DELETED):  Before scheduling a block,
   these notes are put in a list (in rm_other_notes() and
   unlink_other_notes ()).  After scheduling the block, these notes are
   inserted at the beginning of the block (in schedule_block()).  */

static rtx unlink_other_notes (rtx, rtx);
static rtx unlink_line_notes (rtx, rtx);
static void reemit_notes (rtx);

static rtx *ready_lastpos (struct ready_list *);
static void ready_add (struct ready_list *, rtx, bool);
static void ready_sort (struct ready_list *);
static rtx ready_remove_first (struct ready_list *);

static void queue_to_ready (struct ready_list *);
static int early_queue_to_ready (state_t, struct ready_list *);

static void debug_ready_list (struct ready_list *);

static void move_insn (rtx);

/* The following functions are used to implement multi-pass scheduling
   on the first cycle.  */
static rtx ready_element (struct ready_list *, int);
static rtx ready_remove (struct ready_list *, int);
static void ready_remove_insn (rtx);
static int max_issue (struct ready_list *, int *, int);

static rtx choose_ready (struct ready_list *);

static void fix_inter_tick (rtx, rtx);
static int fix_tick_ready (rtx);
static void change_queue_index (rtx, int);
static void resolve_dep (rtx, rtx);

/* The following functions are used to implement scheduling of data/control
   speculative instructions.  */

static void extend_h_i_d (void);
static void extend_ready (int);
static void extend_global (rtx);
static void extend_all (rtx);
static void init_h_i_d (rtx);
static void generate_recovery_code (rtx);
static void process_insn_depend_be_in_spec (rtx, rtx, ds_t);
static void begin_speculative_block (rtx);
static void add_to_speculative_block (rtx);
static dw_t dep_weak (ds_t);
static edge find_fallthru_edge (basic_block);
static void init_before_recovery (void);
static basic_block create_recovery_block (void);
static void create_check_block_twin (rtx, bool);
static void fix_recovery_deps (basic_block);
static void associate_line_notes_with_blocks (basic_block);
static void change_pattern (rtx, rtx);
static int speculate_insn (rtx, ds_t, rtx *);
static void dump_new_block_header (int, basic_block, rtx, rtx);
static void restore_bb_notes (basic_block);
static void extend_bb (basic_block);
static void fix_jump_move (rtx);
static void move_block_after_check (rtx);
static void move_succs (VEC(edge,gc) **, basic_block);
static void init_glat (void);
static void init_glat1 (basic_block);
static void attach_life_info1 (basic_block);
static void free_glat (void);
static void sched_remove_insn (rtx);
static void clear_priorities (rtx);
static void add_jump_dependencies (rtx, rtx);
static void calc_priorities (rtx);
#ifdef ENABLE_CHECKING
static int has_edge_p (VEC(edge,gc) *, int);
static void check_cfg (rtx, rtx);
static void check_sched_flags (void);
#endif

#endif /* INSN_SCHEDULING */

/* Point to state used for the current scheduling pass.  */
struct sched_info *current_sched_info;

#ifndef INSN_SCHEDULING
void
schedule_insns (void)
{
}
#else

/* Working copy of frontend's sched_info variable.  */
static struct sched_info current_sched_info_var;

/* Pointer to the last instruction scheduled.  Used by rank_for_schedule,
   so that insns independent of the last scheduled insn will be preferred
   over dependent instructions.  */

static rtx last_scheduled_insn;

/* Compute cost of executing INSN given the dependence LINK on the insn USED.
   This is the number of cycles between instruction issue and
   instruction results.  */

HAIFA_INLINE int
insn_cost (rtx insn, rtx link, rtx used)
{
  return insn_cost1 (insn, used ? REG_NOTE_KIND (link) : REG_NOTE_MAX,
		     link, used);
}

/* Compute cost of executing INSN given the dependence on the insn USED.
   If LINK is not NULL, then its REG_NOTE_KIND is used as a dependence type.
   Otherwise, dependence between INSN and USED is assumed to be of type
   DEP_TYPE.  This function was introduced as a workaround for
   targetm.adjust_cost hook.
   This is the number of cycles between instruction issue and
   instruction results.  */

HAIFA_INLINE static int
insn_cost1 (rtx insn, enum reg_note dep_type, rtx link, rtx used)
{
  int cost = INSN_COST (insn);

  if (cost < 0)
    {
      /* A USE insn, or something else we don't need to
	 understand.  We can't pass these directly to
	 result_ready_cost or insn_default_latency because it will
	 trigger a fatal error for unrecognizable insns.  */
      if (recog_memoized (insn) < 0)
	{
	  INSN_COST (insn) = 0;
	  return 0;
	}
      else
	{
	  cost = insn_default_latency (insn);
	  if (cost < 0)
	    cost = 0;

	  INSN_COST (insn) = cost;
	}
    }

  /* In this case estimate cost without caring how insn is used.  */
  if (used == 0)
    return cost;

  /* A USE insn should never require the value used to be computed.
     This allows the computation of a function's result and parameter
     values to overlap the return and call.  */
  if (recog_memoized (used) < 0)
    cost = 0;
  else
    {
      gcc_assert (!link || dep_type == REG_NOTE_KIND (link));

      if (INSN_CODE (insn) >= 0)
	{
	  if (dep_type == REG_DEP_ANTI)
	    cost = 0;
	  else if (dep_type == REG_DEP_OUTPUT)
	    {
	      cost = (insn_default_latency (insn)
		      - insn_default_latency (used));
	      if (cost <= 0)
		cost = 1;
	    }
	  else if (bypass_p (insn))
	    cost = insn_latency (insn, used);
	}

      if (targetm.sched.adjust_cost_2)
	cost = targetm.sched.adjust_cost_2 (used, (int) dep_type, insn, cost);
      else
	{
	  gcc_assert (link);
	  if (targetm.sched.adjust_cost)
	    cost = targetm.sched.adjust_cost (used, link, insn, cost);
	}

      if (cost < 0)
	cost = 0;
    }

  return cost;
}

/* Compute the priority number for INSN.  */

static int
priority (rtx insn)
{
  rtx link;

  if (! INSN_P (insn))
    return 0;

  if (! INSN_PRIORITY_KNOWN (insn))
    {
      int this_priority = 0;

      if (INSN_DEPEND (insn) == 0)
	this_priority = insn_cost (insn, 0, 0);
      else
	{
	  rtx prev_first, twin;
	  basic_block rec;

	  /* For recovery check instructions we calculate priority slightly
	     different than that of normal instructions.  Instead of walking
	     through INSN_DEPEND (check) list, we walk through INSN_DEPEND list
	     of each instruction in the corresponding recovery block.  */ 

	  rec = RECOVERY_BLOCK (insn);
	  if (!rec || rec == EXIT_BLOCK_PTR)
	    {
	      prev_first = PREV_INSN (insn);
	      twin = insn;
	    }
	  else
	    {
	      prev_first = NEXT_INSN (BB_HEAD (rec));
	      twin = PREV_INSN (BB_END (rec));
	    }

	  do
	    {
	      for (link = INSN_DEPEND (twin); link; link = XEXP (link, 1))
		{
		  rtx next;
		  int next_priority;
		  
		  next = XEXP (link, 0);
		  
		  if (BLOCK_FOR_INSN (next) != rec)
		    {
		      /* Critical path is meaningful in block boundaries
			 only.  */
		      if (! (*current_sched_info->contributes_to_priority)
			  (next, insn)
			  /* If flag COUNT_SPEC_IN_CRITICAL_PATH is set,
			     then speculative instructions will less likely be
			     scheduled.  That is because the priority of
			     their producers will increase, and, thus, the
			     producers will more likely be scheduled, thus,
			     resolving the dependence.  */
			  || ((current_sched_info->flags & DO_SPECULATION)
			      && (DEP_STATUS (link) & SPECULATIVE)
			      && !(spec_info->flags
				   & COUNT_SPEC_IN_CRITICAL_PATH)))
			continue;
		      
		      next_priority = insn_cost1 (insn,
						  twin == insn ?
						  REG_NOTE_KIND (link) :
						  REG_DEP_ANTI,
						  twin == insn ? link : 0,
						  next) + priority (next);

		      if (next_priority > this_priority)
			this_priority = next_priority;
		    }
		}
	      
	      twin = PREV_INSN (twin);
	    }
	  while (twin != prev_first);
	}
      INSN_PRIORITY (insn) = this_priority;
      INSN_PRIORITY_KNOWN (insn) = 1;
    }

  return INSN_PRIORITY (insn);
}

/* Macros and functions for keeping the priority queue sorted, and
   dealing with queuing and dequeuing of instructions.  */

#define SCHED_SORT(READY, N_READY)                                   \
do { if ((N_READY) == 2)				             \
       swap_sort (READY, N_READY);			             \
     else if ((N_READY) > 2)                                         \
         qsort (READY, N_READY, sizeof (rtx), rank_for_schedule); }  \
while (0)

/* Returns a positive value if x is preferred; returns a negative value if
   y is preferred.  Should never return 0, since that will make the sort
   unstable.  */

static int
rank_for_schedule (const void *x, const void *y)
{
  rtx tmp = *(const rtx *) y;
  rtx tmp2 = *(const rtx *) x;
  rtx link;
  int tmp_class, tmp2_class, depend_count1, depend_count2;
  int val, priority_val, weight_val, info_val;

  /* The insn in a schedule group should be issued the first.  */
  if (SCHED_GROUP_P (tmp) != SCHED_GROUP_P (tmp2))
    return SCHED_GROUP_P (tmp2) ? 1 : -1;

  /* Prefer insn with higher priority.  */
  priority_val = INSN_PRIORITY (tmp2) - INSN_PRIORITY (tmp);

  if (priority_val)
    return priority_val;

  /* Prefer speculative insn with greater dependencies weakness.  */
  if (spec_info)
    {
      ds_t ds1, ds2;
      dw_t dw1, dw2;
      int dw;

      ds1 = TODO_SPEC (tmp) & SPECULATIVE;
      if (ds1)
	dw1 = dep_weak (ds1);
      else
	dw1 = NO_DEP_WEAK;
      
      ds2 = TODO_SPEC (tmp2) & SPECULATIVE;
      if (ds2)
	dw2 = dep_weak (ds2);
      else
	dw2 = NO_DEP_WEAK;

      dw = dw2 - dw1;
      if (dw > (NO_DEP_WEAK / 8) || dw < -(NO_DEP_WEAK / 8))
	return dw;
    }

  /* Prefer an insn with smaller contribution to registers-pressure.  */
  if (!reload_completed &&
      (weight_val = INSN_REG_WEIGHT (tmp) - INSN_REG_WEIGHT (tmp2)))
    return weight_val;

  info_val = (*current_sched_info->rank) (tmp, tmp2);
  if (info_val)
    return info_val;

  /* Compare insns based on their relation to the last-scheduled-insn.  */
  if (INSN_P (last_scheduled_insn))
    {
      /* Classify the instructions into three classes:
         1) Data dependent on last schedule insn.
         2) Anti/Output dependent on last scheduled insn.
         3) Independent of last scheduled insn, or has latency of one.
         Choose the insn from the highest numbered class if different.  */
      link = find_insn_list (tmp, INSN_DEPEND (last_scheduled_insn));
      if (link == 0 || insn_cost (last_scheduled_insn, link, tmp) == 1)
	tmp_class = 3;
      else if (REG_NOTE_KIND (link) == 0)	/* Data dependence.  */
	tmp_class = 1;
      else
	tmp_class = 2;

      link = find_insn_list (tmp2, INSN_DEPEND (last_scheduled_insn));
      if (link == 0 || insn_cost (last_scheduled_insn, link, tmp2) == 1)
	tmp2_class = 3;
      else if (REG_NOTE_KIND (link) == 0)	/* Data dependence.  */
	tmp2_class = 1;
      else
	tmp2_class = 2;

      if ((val = tmp2_class - tmp_class))
	return val;
    }

  /* Prefer the insn which has more later insns that depend on it.
     This gives the scheduler more freedom when scheduling later
     instructions at the expense of added register pressure.  */
  depend_count1 = 0;
  for (link = INSN_DEPEND (tmp); link; link = XEXP (link, 1))
    depend_count1++;

  depend_count2 = 0;
  for (link = INSN_DEPEND (tmp2); link; link = XEXP (link, 1))
    depend_count2++;

  val = depend_count2 - depend_count1;
  if (val)
    return val;

  /* If insns are equally good, sort by INSN_LUID (original insn order),
     so that we make the sort stable.  This minimizes instruction movement,
     thus minimizing sched's effect on debugging and cross-jumping.  */
  return INSN_LUID (tmp) - INSN_LUID (tmp2);
}

/* Resort the array A in which only element at index N may be out of order.  */

HAIFA_INLINE static void
swap_sort (rtx *a, int n)
{
  rtx insn = a[n - 1];
  int i = n - 2;

  while (i >= 0 && rank_for_schedule (a + i, &insn) >= 0)
    {
      a[i + 1] = a[i];
      i -= 1;
    }
  a[i + 1] = insn;
}

/* Add INSN to the insn queue so that it can be executed at least
   N_CYCLES after the currently executing insn.  Preserve insns
   chain for debugging purposes.  */

HAIFA_INLINE static void
queue_insn (rtx insn, int n_cycles)
{
  int next_q = NEXT_Q_AFTER (q_ptr, n_cycles);
  rtx link = alloc_INSN_LIST (insn, insn_queue[next_q]);

  gcc_assert (n_cycles <= max_insn_queue_index);

  insn_queue[next_q] = link;
  q_size += 1;

  if (sched_verbose >= 2)
    {
      fprintf (sched_dump, ";;\t\tReady-->Q: insn %s: ",
	       (*current_sched_info->print_insn) (insn, 0));

      fprintf (sched_dump, "queued for %d cycles.\n", n_cycles);
    }
  
  QUEUE_INDEX (insn) = next_q;
}

/* Remove INSN from queue.  */
static void
queue_remove (rtx insn)
{
  gcc_assert (QUEUE_INDEX (insn) >= 0);
  remove_free_INSN_LIST_elem (insn, &insn_queue[QUEUE_INDEX (insn)]);
  q_size--;
  QUEUE_INDEX (insn) = QUEUE_NOWHERE;
}

/* Return a pointer to the bottom of the ready list, i.e. the insn
   with the lowest priority.  */

HAIFA_INLINE static rtx *
ready_lastpos (struct ready_list *ready)
{
  gcc_assert (ready->n_ready >= 1);
  return ready->vec + ready->first - ready->n_ready + 1;
}

/* Add an element INSN to the ready list so that it ends up with the
   lowest/highest priority depending on FIRST_P.  */

HAIFA_INLINE static void
ready_add (struct ready_list *ready, rtx insn, bool first_p)
{
  if (!first_p)
    {
      if (ready->first == ready->n_ready)
	{
	  memmove (ready->vec + ready->veclen - ready->n_ready,
		   ready_lastpos (ready),
		   ready->n_ready * sizeof (rtx));
	  ready->first = ready->veclen - 1;
	}
      ready->vec[ready->first - ready->n_ready] = insn;
    }
  else
    {
      if (ready->first == ready->veclen - 1)
	{
	  if (ready->n_ready)
	    /* ready_lastpos() fails when called with (ready->n_ready == 0).  */
	    memmove (ready->vec + ready->veclen - ready->n_ready - 1,
		     ready_lastpos (ready),
		     ready->n_ready * sizeof (rtx));
	  ready->first = ready->veclen - 2;
	}
      ready->vec[++(ready->first)] = insn;
    }

  ready->n_ready++;

  gcc_assert (QUEUE_INDEX (insn) != QUEUE_READY);
  QUEUE_INDEX (insn) = QUEUE_READY;
}

/* Remove the element with the highest priority from the ready list and
   return it.  */

HAIFA_INLINE static rtx
ready_remove_first (struct ready_list *ready)
{
  rtx t;
  
  gcc_assert (ready->n_ready);
  t = ready->vec[ready->first--];
  ready->n_ready--;
  /* If the queue becomes empty, reset it.  */
  if (ready->n_ready == 0)
    ready->first = ready->veclen - 1;

  gcc_assert (QUEUE_INDEX (t) == QUEUE_READY);
  QUEUE_INDEX (t) = QUEUE_NOWHERE;

  return t;
}

/* The following code implements multi-pass scheduling for the first
   cycle.  In other words, we will try to choose ready insn which
   permits to start maximum number of insns on the same cycle.  */

/* Return a pointer to the element INDEX from the ready.  INDEX for
   insn with the highest priority is 0, and the lowest priority has
   N_READY - 1.  */

HAIFA_INLINE static rtx
ready_element (struct ready_list *ready, int index)
{
  gcc_assert (ready->n_ready && index < ready->n_ready);
  
  return ready->vec[ready->first - index];
}

/* Remove the element INDEX from the ready list and return it.  INDEX
   for insn with the highest priority is 0, and the lowest priority
   has N_READY - 1.  */

HAIFA_INLINE static rtx
ready_remove (struct ready_list *ready, int index)
{
  rtx t;
  int i;

  if (index == 0)
    return ready_remove_first (ready);
  gcc_assert (ready->n_ready && index < ready->n_ready);
  t = ready->vec[ready->first - index];
  ready->n_ready--;
  for (i = index; i < ready->n_ready; i++)
    ready->vec[ready->first - i] = ready->vec[ready->first - i - 1];
  QUEUE_INDEX (t) = QUEUE_NOWHERE;
  return t;
}

/* Remove INSN from the ready list.  */
static void
ready_remove_insn (rtx insn)
{
  int i;

  for (i = 0; i < readyp->n_ready; i++)
    if (ready_element (readyp, i) == insn)
      {
        ready_remove (readyp, i);
        return;
      }
  gcc_unreachable ();
}

/* Sort the ready list READY by ascending priority, using the SCHED_SORT
   macro.  */

HAIFA_INLINE static void
ready_sort (struct ready_list *ready)
{
  rtx *first = ready_lastpos (ready);
  SCHED_SORT (first, ready->n_ready);
}

/* PREV is an insn that is ready to execute.  Adjust its priority if that
   will help shorten or lengthen register lifetimes as appropriate.  Also
   provide a hook for the target to tweek itself.  */

HAIFA_INLINE static void
adjust_priority (rtx prev)
{
  /* ??? There used to be code here to try and estimate how an insn
     affected register lifetimes, but it did it by looking at REG_DEAD
     notes, which we removed in schedule_region.  Nor did it try to
     take into account register pressure or anything useful like that.

     Revisit when we have a machine model to work with and not before.  */

  if (targetm.sched.adjust_priority)
    INSN_PRIORITY (prev) =
      targetm.sched.adjust_priority (prev, INSN_PRIORITY (prev));
}

/* Advance time on one cycle.  */
HAIFA_INLINE static void
advance_one_cycle (void)
{
  if (targetm.sched.dfa_pre_cycle_insn)
    state_transition (curr_state,
		      targetm.sched.dfa_pre_cycle_insn ());

  state_transition (curr_state, NULL);
  
  if (targetm.sched.dfa_post_cycle_insn)
    state_transition (curr_state,
		      targetm.sched.dfa_post_cycle_insn ());
}

/* Clock at which the previous instruction was issued.  */
static int last_clock_var;

/* INSN is the "currently executing insn".  Launch each insn which was
   waiting on INSN.  READY is the ready list which contains the insns
   that are ready to fire.  CLOCK is the current cycle.  The function
   returns necessary cycle advance after issuing the insn (it is not
   zero for insns in a schedule group).  */

static int
schedule_insn (rtx insn)
{
  rtx link;
  int advance = 0;

  if (sched_verbose >= 1)
    {
      char buf[2048];

      print_insn (buf, insn, 0);
      buf[40] = 0;
      fprintf (sched_dump, ";;\t%3i--> %-40s:", clock_var, buf);

      if (recog_memoized (insn) < 0)
	fprintf (sched_dump, "nothing");
      else
	print_reservation (sched_dump, insn);
      fputc ('\n', sched_dump);
    }

  /* Scheduling instruction should have all its dependencies resolved and
     should have been removed from the ready list.  */
  gcc_assert (INSN_DEP_COUNT (insn) == 0);
  gcc_assert (!LOG_LINKS (insn));
  gcc_assert (QUEUE_INDEX (insn) == QUEUE_NOWHERE);

  QUEUE_INDEX (insn) = QUEUE_SCHEDULED;
  
  /* Now we can free RESOLVED_DEPS list.  */
  if (current_sched_info->flags & USE_DEPS_LIST)
    free_DEPS_LIST_list (&RESOLVED_DEPS (insn));
  else
    free_INSN_LIST_list (&RESOLVED_DEPS (insn));
    
  gcc_assert (INSN_TICK (insn) >= MIN_TICK);
  if (INSN_TICK (insn) > clock_var)
    /* INSN has been prematurely moved from the queue to the ready list.
       This is possible only if following flag is set.  */
    gcc_assert (flag_sched_stalled_insns);    

  /* ??? Probably, if INSN is scheduled prematurely, we should leave
     INSN_TICK untouched.  This is a machine-dependent issue, actually.  */
  INSN_TICK (insn) = clock_var;

  /* Update dependent instructions.  */
  for (link = INSN_DEPEND (insn); link; link = XEXP (link, 1))
    {
      rtx next = XEXP (link, 0);

      resolve_dep (next, insn);

      if (!IS_SPECULATION_BRANCHY_CHECK_P (insn))
	{
	  int effective_cost;      
	  
	  effective_cost = try_ready (next);
	  
	  if (effective_cost >= 0
	      && SCHED_GROUP_P (next)
	      && advance < effective_cost)
	    advance = effective_cost;
	}
      else
	/* Check always has only one forward dependence (to the first insn in
	   the recovery block), therefore, this will be executed only once.  */
	{
	  gcc_assert (XEXP (link, 1) == 0);
	  fix_recovery_deps (RECOVERY_BLOCK (insn));
	}
    }

  /* Annotate the instruction with issue information -- TImode
     indicates that the instruction is expected not to be able
     to issue on the same cycle as the previous insn.  A machine
     may use this information to decide how the instruction should
     be aligned.  */
  if (issue_rate > 1
      && GET_CODE (PATTERN (insn)) != USE
      && GET_CODE (PATTERN (insn)) != CLOBBER)
    {
      if (reload_completed)
	PUT_MODE (insn, clock_var > last_clock_var ? TImode : VOIDmode);
      last_clock_var = clock_var;
    }

  return advance;
}

/* Functions for handling of notes.  */

/* Delete notes beginning with INSN and put them in the chain
   of notes ended by NOTE_LIST.
   Returns the insn following the notes.  */

static rtx
unlink_other_notes (rtx insn, rtx tail)
{
  rtx prev = PREV_INSN (insn);

  while (insn != tail && NOTE_NOT_BB_P (insn))
    {
      rtx next = NEXT_INSN (insn);
      basic_block bb = BLOCK_FOR_INSN (insn);

      /* Delete the note from its current position.  */
      if (prev)
	NEXT_INSN (prev) = next;
      if (next)
	PREV_INSN (next) = prev;

      if (bb)
        {
          /* Basic block can begin with either LABEL or
             NOTE_INSN_BASIC_BLOCK.  */
          gcc_assert (BB_HEAD (bb) != insn);

          /* Check if we are removing last insn in the BB.  */
          if (BB_END (bb) == insn)
            BB_END (bb) = prev;
        }

      /* See sched_analyze to see how these are handled.  */
      if (NOTE_LINE_NUMBER (insn) != NOTE_INSN_EH_REGION_BEG
	  && NOTE_LINE_NUMBER (insn) != NOTE_INSN_EH_REGION_END)
	{
	  /* Insert the note at the end of the notes list.  */
	  PREV_INSN (insn) = note_list;
	  if (note_list)
	    NEXT_INSN (note_list) = insn;
	  note_list = insn;
	}

      insn = next;
    }
  return insn;
}

/* Delete line notes beginning with INSN. Record line-number notes so
   they can be reused.  Returns the insn following the notes.  */

static rtx
unlink_line_notes (rtx insn, rtx tail)
{
  rtx prev = PREV_INSN (insn);

  while (insn != tail && NOTE_NOT_BB_P (insn))
    {
      rtx next = NEXT_INSN (insn);

      if (write_symbols != NO_DEBUG && NOTE_LINE_NUMBER (insn) > 0)
	{
          basic_block bb = BLOCK_FOR_INSN (insn);

	  /* Delete the note from its current position.  */
	  if (prev)
	    NEXT_INSN (prev) = next;
	  if (next)
	    PREV_INSN (next) = prev;

          if (bb)
            {
              /* Basic block can begin with either LABEL or
                 NOTE_INSN_BASIC_BLOCK.  */
              gcc_assert (BB_HEAD (bb) != insn);

              /* Check if we are removing last insn in the BB.  */
              if (BB_END (bb) == insn)
                BB_END (bb) = prev;
            }

	  /* Record line-number notes so they can be reused.  */
	  LINE_NOTE (insn) = insn;
	}
      else
	prev = insn;

      insn = next;
    }
  return insn;
}

/* Return the head and tail pointers of ebb starting at BEG and ending
   at END.  */

void
get_ebb_head_tail (basic_block beg, basic_block end, rtx *headp, rtx *tailp)
{
  rtx beg_head = BB_HEAD (beg);
  rtx beg_tail = BB_END (beg);
  rtx end_head = BB_HEAD (end);
  rtx end_tail = BB_END (end);

  /* Don't include any notes or labels at the beginning of the BEG
     basic block, or notes at the end of the END basic blocks.  */

  if (LABEL_P (beg_head))
    beg_head = NEXT_INSN (beg_head);

  while (beg_head != beg_tail)
    if (NOTE_P (beg_head))
      beg_head = NEXT_INSN (beg_head);
    else
      break;

  *headp = beg_head;

  if (beg == end)
    end_head = beg_head;
  else if (LABEL_P (end_head))
    end_head = NEXT_INSN (end_head);

  while (end_head != end_tail)
    if (NOTE_P (end_tail))
      end_tail = PREV_INSN (end_tail);
    else
      break;

  *tailp = end_tail;
}

/* Return nonzero if there are no real insns in the range [ HEAD, TAIL ].  */

int
no_real_insns_p (rtx head, rtx tail)
{
  while (head != NEXT_INSN (tail))
    {
      if (!NOTE_P (head) && !LABEL_P (head))
	return 0;
      head = NEXT_INSN (head);
    }
  return 1;
}

/* Delete line notes from one block. Save them so they can be later restored
   (in restore_line_notes).  HEAD and TAIL are the boundaries of the
   block in which notes should be processed.  */

void
rm_line_notes (rtx head, rtx tail)
{
  rtx next_tail;
  rtx insn;

  next_tail = NEXT_INSN (tail);
  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    {
      rtx prev;

      /* Farm out notes, and maybe save them in NOTE_LIST.
         This is needed to keep the debugger from
         getting completely deranged.  */
      if (NOTE_NOT_BB_P (insn))
	{
	  prev = insn;
	  insn = unlink_line_notes (insn, next_tail);

	  gcc_assert (prev != tail && prev != head && insn != next_tail);
	}
    }
}

/* Save line number notes for each insn in block B.  HEAD and TAIL are
   the boundaries of the block in which notes should be processed.  */

void
save_line_notes (int b, rtx head, rtx tail)
{
  rtx next_tail;

  /* We must use the true line number for the first insn in the block
     that was computed and saved at the start of this pass.  We can't
     use the current line number, because scheduling of the previous
     block may have changed the current line number.  */

  rtx line = line_note_head[b];
  rtx insn;

  next_tail = NEXT_INSN (tail);

  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
      line = insn;
    else
      LINE_NOTE (insn) = line;
}

/* After a block was scheduled, insert line notes into the insns list.
   HEAD and TAIL are the boundaries of the block in which notes should
   be processed.  */

void
restore_line_notes (rtx head, rtx tail)
{
  rtx line, note, prev, new;
  int added_notes = 0;
  rtx next_tail, insn;

  head = head;
  next_tail = NEXT_INSN (tail);

  /* Determine the current line-number.  We want to know the current
     line number of the first insn of the block here, in case it is
     different from the true line number that was saved earlier.  If
     different, then we need a line number note before the first insn
     of this block.  If it happens to be the same, then we don't want to
     emit another line number note here.  */
  for (line = head; line; line = PREV_INSN (line))
    if (NOTE_P (line) && NOTE_LINE_NUMBER (line) > 0)
      break;

  /* Walk the insns keeping track of the current line-number and inserting
     the line-number notes as needed.  */
  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
      line = insn;
  /* This used to emit line number notes before every non-deleted note.
     However, this confuses a debugger, because line notes not separated
     by real instructions all end up at the same address.  I can find no
     use for line number notes before other notes, so none are emitted.  */
    else if (!NOTE_P (insn)
	     && INSN_UID (insn) < old_max_uid
	     && (note = LINE_NOTE (insn)) != 0
	     && note != line
	     && (line == 0
#ifdef USE_MAPPED_LOCATION
		 || NOTE_SOURCE_LOCATION (note) != NOTE_SOURCE_LOCATION (line)
#else
		 || NOTE_LINE_NUMBER (note) != NOTE_LINE_NUMBER (line)
		 || NOTE_SOURCE_FILE (note) != NOTE_SOURCE_FILE (line)
#endif
		 ))
      {
	line = note;
	prev = PREV_INSN (insn);
	if (LINE_NOTE (note))
	  {
	    /* Re-use the original line-number note.  */
	    LINE_NOTE (note) = 0;
	    PREV_INSN (note) = prev;
	    NEXT_INSN (prev) = note;
	    PREV_INSN (insn) = note;
	    NEXT_INSN (note) = insn;
	    set_block_for_insn (note, BLOCK_FOR_INSN (insn));
	  }
	else
	  {
	    added_notes++;
	    new = emit_note_after (NOTE_LINE_NUMBER (note), prev);
#ifndef USE_MAPPED_LOCATION
	    NOTE_SOURCE_FILE (new) = NOTE_SOURCE_FILE (note);
#endif
	  }
      }
  if (sched_verbose && added_notes)
    fprintf (sched_dump, ";; added %d line-number notes\n", added_notes);
}

/* After scheduling the function, delete redundant line notes from the
   insns list.  */

void
rm_redundant_line_notes (void)
{
  rtx line = 0;
  rtx insn = get_insns ();
  int active_insn = 0;
  int notes = 0;

  /* Walk the insns deleting redundant line-number notes.  Many of these
     are already present.  The remainder tend to occur at basic
     block boundaries.  */
  for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
    if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
      {
	/* If there are no active insns following, INSN is redundant.  */
	if (active_insn == 0)
	  {
	    notes++;
	    SET_INSN_DELETED (insn);
	  }
	/* If the line number is unchanged, LINE is redundant.  */
	else if (line
#ifdef USE_MAPPED_LOCATION
		 && NOTE_SOURCE_LOCATION (line) == NOTE_SOURCE_LOCATION (insn)
#else
		 && NOTE_LINE_NUMBER (line) == NOTE_LINE_NUMBER (insn)
		 && NOTE_SOURCE_FILE (line) == NOTE_SOURCE_FILE (insn)
#endif
)
	  {
	    notes++;
	    SET_INSN_DELETED (line);
	    line = insn;
	  }
	else
	  line = insn;
	active_insn = 0;
      }
    else if (!((NOTE_P (insn)
		&& NOTE_LINE_NUMBER (insn) == NOTE_INSN_DELETED)
	       || (NONJUMP_INSN_P (insn)
		   && (GET_CODE (PATTERN (insn)) == USE
		       || GET_CODE (PATTERN (insn)) == CLOBBER))))
      active_insn++;

  if (sched_verbose && notes)
    fprintf (sched_dump, ";; deleted %d line-number notes\n", notes);
}

/* Delete notes between HEAD and TAIL and put them in the chain
   of notes ended by NOTE_LIST.  */

void
rm_other_notes (rtx head, rtx tail)
{
  rtx next_tail;
  rtx insn;

  note_list = 0;
  if (head == tail && (! INSN_P (head)))
    return;

  next_tail = NEXT_INSN (tail);
  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    {
      rtx prev;

      /* Farm out notes, and maybe save them in NOTE_LIST.
         This is needed to keep the debugger from
         getting completely deranged.  */
      if (NOTE_NOT_BB_P (insn))
	{
	  prev = insn;

	  insn = unlink_other_notes (insn, next_tail);

	  gcc_assert (prev != tail && prev != head && insn != next_tail);
	}
    }
}

/* Functions for computation of registers live/usage info.  */

/* This function looks for a new register being defined.
   If the destination register is already used by the source,
   a new register is not needed.  */

static int
find_set_reg_weight (rtx x)
{
  if (GET_CODE (x) == CLOBBER
      && register_operand (SET_DEST (x), VOIDmode))
    return 1;
  if (GET_CODE (x) == SET
      && register_operand (SET_DEST (x), VOIDmode))
    {
      if (REG_P (SET_DEST (x)))
	{
	  if (!reg_mentioned_p (SET_DEST (x), SET_SRC (x)))
	    return 1;
	  else
	    return 0;
	}
      return 1;
    }
  return 0;
}

/* Calculate INSN_REG_WEIGHT for all insns of a block.  */

static void
find_insn_reg_weight (basic_block bb)
{
  rtx insn, next_tail, head, tail;

  get_ebb_head_tail (bb, bb, &head, &tail);
  next_tail = NEXT_INSN (tail);

  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    find_insn_reg_weight1 (insn);    
}

/* Calculate INSN_REG_WEIGHT for single instruction.
   Separated from find_insn_reg_weight because of need
   to initialize new instruction in generate_recovery_code.  */
static void
find_insn_reg_weight1 (rtx insn)
{
  int reg_weight = 0;
  rtx x;
  
  /* Handle register life information.  */
  if (! INSN_P (insn))
    return;
  
  /* Increment weight for each register born here.  */
  x = PATTERN (insn);
  reg_weight += find_set_reg_weight (x);
  if (GET_CODE (x) == PARALLEL)
    {
      int j;
      for (j = XVECLEN (x, 0) - 1; j >= 0; j--)
	{
	  x = XVECEXP (PATTERN (insn), 0, j);
	  reg_weight += find_set_reg_weight (x);
	}
    }
  /* Decrement weight for each register that dies here.  */
  for (x = REG_NOTES (insn); x; x = XEXP (x, 1))
    {
      if (REG_NOTE_KIND (x) == REG_DEAD
	  || REG_NOTE_KIND (x) == REG_UNUSED)
	reg_weight--;
    }
  
  INSN_REG_WEIGHT (insn) = reg_weight;
}

/* Move insns that became ready to fire from queue to ready list.  */

static void
queue_to_ready (struct ready_list *ready)
{
  rtx insn;
  rtx link;

  q_ptr = NEXT_Q (q_ptr);

  /* Add all pending insns that can be scheduled without stalls to the
     ready list.  */
  for (link = insn_queue[q_ptr]; link; link = XEXP (link, 1))
    {
      insn = XEXP (link, 0);
      q_size -= 1;

      if (sched_verbose >= 2)
	fprintf (sched_dump, ";;\t\tQ-->Ready: insn %s: ",
		 (*current_sched_info->print_insn) (insn, 0));

      /* If the ready list is full, delay the insn for 1 cycle.
	 See the comment in schedule_block for the rationale.  */
      if (!reload_completed
	  && ready->n_ready > MAX_SCHED_READY_INSNS
	  && !SCHED_GROUP_P (insn))
	{
	  if (sched_verbose >= 2)
	    fprintf (sched_dump, "requeued because ready full\n");
	  queue_insn (insn, 1);
	}
      else
	{
	  ready_add (ready, insn, false);
	  if (sched_verbose >= 2)
	    fprintf (sched_dump, "moving to ready without stalls\n");
        }
    }
  free_INSN_LIST_list (&insn_queue[q_ptr]);

  /* If there are no ready insns, stall until one is ready and add all
     of the pending insns at that point to the ready list.  */
  if (ready->n_ready == 0)
    {
      int stalls;

      for (stalls = 1; stalls <= max_insn_queue_index; stalls++)
	{
	  if ((link = insn_queue[NEXT_Q_AFTER (q_ptr, stalls)]))
	    {
	      for (; link; link = XEXP (link, 1))
		{
		  insn = XEXP (link, 0);
		  q_size -= 1;

		  if (sched_verbose >= 2)
		    fprintf (sched_dump, ";;\t\tQ-->Ready: insn %s: ",
			     (*current_sched_info->print_insn) (insn, 0));

		  ready_add (ready, insn, false);
		  if (sched_verbose >= 2)
		    fprintf (sched_dump, "moving to ready with %d stalls\n", stalls);
		}
	      free_INSN_LIST_list (&insn_queue[NEXT_Q_AFTER (q_ptr, stalls)]);

	      advance_one_cycle ();

	      break;
	    }

	  advance_one_cycle ();
	}

      q_ptr = NEXT_Q_AFTER (q_ptr, stalls);
      clock_var += stalls;
    }
}

/* Used by early_queue_to_ready.  Determines whether it is "ok" to
   prematurely move INSN from the queue to the ready list.  Currently, 
   if a target defines the hook 'is_costly_dependence', this function 
   uses the hook to check whether there exist any dependences which are
   considered costly by the target, between INSN and other insns that 
   have already been scheduled.  Dependences are checked up to Y cycles
   back, with default Y=1; The flag -fsched-stalled-insns-dep=Y allows
   controlling this value. 
   (Other considerations could be taken into account instead (or in 
   addition) depending on user flags and target hooks.  */

static bool 
ok_for_early_queue_removal (rtx insn)
{
  int n_cycles;
  rtx prev_insn = last_scheduled_insn;

  if (targetm.sched.is_costly_dependence)
    {
      for (n_cycles = flag_sched_stalled_insns_dep; n_cycles; n_cycles--)
	{
	  for ( ; prev_insn; prev_insn = PREV_INSN (prev_insn))
	    {
	      rtx dep_link = 0;
	      int dep_cost;

	      if (!NOTE_P (prev_insn))
		{
		  dep_link = find_insn_list (insn, INSN_DEPEND (prev_insn));
		  if (dep_link)
		    {
		      dep_cost = insn_cost (prev_insn, dep_link, insn) ;
		      if (targetm.sched.is_costly_dependence (prev_insn, insn, 
				dep_link, dep_cost, 
				flag_sched_stalled_insns_dep - n_cycles))
			return false;
		    }
		}

	      if (GET_MODE (prev_insn) == TImode) /* end of dispatch group */
		break;
	    }

	  if (!prev_insn) 
	    break;
	  prev_insn = PREV_INSN (prev_insn);     
	}
    }

  return true;
}


/* Remove insns from the queue, before they become "ready" with respect
   to FU latency considerations.  */

static int 
early_queue_to_ready (state_t state, struct ready_list *ready)
{
  rtx insn;
  rtx link;
  rtx next_link;
  rtx prev_link;
  bool move_to_ready;
  int cost;
  state_t temp_state = alloca (dfa_state_size);
  int stalls;
  int insns_removed = 0;

  /*
     Flag '-fsched-stalled-insns=X' determines the aggressiveness of this 
     function: 

     X == 0: There is no limit on how many queued insns can be removed          
             prematurely.  (flag_sched_stalled_insns = -1).

     X >= 1: Only X queued insns can be removed prematurely in each 
	     invocation.  (flag_sched_stalled_insns = X).

     Otherwise: Early queue removal is disabled.
         (flag_sched_stalled_insns = 0)
  */

  if (! flag_sched_stalled_insns)   
    return 0;

  for (stalls = 0; stalls <= max_insn_queue_index; stalls++)
    {
      if ((link = insn_queue[NEXT_Q_AFTER (q_ptr, stalls)]))
	{
	  if (sched_verbose > 6)
	    fprintf (sched_dump, ";; look at index %d + %d\n", q_ptr, stalls);

	  prev_link = 0;
	  while (link)
	    {
	      next_link = XEXP (link, 1);
	      insn = XEXP (link, 0);
	      if (insn && sched_verbose > 6)
		print_rtl_single (sched_dump, insn);

	      memcpy (temp_state, state, dfa_state_size);
	      if (recog_memoized (insn) < 0) 
		/* non-negative to indicate that it's not ready
		   to avoid infinite Q->R->Q->R... */
		cost = 0;
	      else
		cost = state_transition (temp_state, insn);

	      if (sched_verbose >= 6)
		fprintf (sched_dump, "transition cost = %d\n", cost);

	      move_to_ready = false;
	      if (cost < 0) 
		{
		  move_to_ready = ok_for_early_queue_removal (insn);
		  if (move_to_ready == true)
		    {
		      /* move from Q to R */
		      q_size -= 1;
		      ready_add (ready, insn, false);

		      if (prev_link)   
			XEXP (prev_link, 1) = next_link;
		      else
			insn_queue[NEXT_Q_AFTER (q_ptr, stalls)] = next_link;

		      free_INSN_LIST_node (link);

		      if (sched_verbose >= 2)
			fprintf (sched_dump, ";;\t\tEarly Q-->Ready: insn %s\n",
				 (*current_sched_info->print_insn) (insn, 0));

		      insns_removed++;
		      if (insns_removed == flag_sched_stalled_insns)
			/* Remove no more than flag_sched_stalled_insns insns
			   from Q at a time.  */
			return insns_removed;
		    }
		}

	      if (move_to_ready == false)
		prev_link = link;

	      link = next_link;
	    } /* while link */
	} /* if link */    

    } /* for stalls.. */

  return insns_removed; 
}


/* Print the ready list for debugging purposes.  Callable from debugger.  */

static void
debug_ready_list (struct ready_list *ready)
{
  rtx *p;
  int i;

  if (ready->n_ready == 0)
    {
      fprintf (sched_dump, "\n");
      return;
    }

  p = ready_lastpos (ready);
  for (i = 0; i < ready->n_ready; i++)
    fprintf (sched_dump, "  %s", (*current_sched_info->print_insn) (p[i], 0));
  fprintf (sched_dump, "\n");
}

/* Search INSN for REG_SAVE_NOTE note pairs for
   NOTE_INSN_EHREGION_{BEG,END}; and convert them back into
   NOTEs.  The REG_SAVE_NOTE note following first one is contains the
   saved value for NOTE_BLOCK_NUMBER which is useful for
   NOTE_INSN_EH_REGION_{BEG,END} NOTEs.  */

static void
reemit_notes (rtx insn)
{
  rtx note, last = insn;

  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
    {
      if (REG_NOTE_KIND (note) == REG_SAVE_NOTE)
	{
	  enum insn_note note_type = INTVAL (XEXP (note, 0));

	  last = emit_note_before (note_type, last);
	  remove_note (insn, note);
	}
    }
}

/* Move INSN.  Reemit notes if needed.  Update CFG, if needed.  */
static void
move_insn (rtx insn)
{
  rtx last = last_scheduled_insn;

  if (PREV_INSN (insn) != last)
    {
      basic_block bb;
      rtx note;
      int jump_p = 0;

      bb = BLOCK_FOR_INSN (insn);
 
      /* BB_HEAD is either LABEL or NOTE.  */
      gcc_assert (BB_HEAD (bb) != insn);      

      if (BB_END (bb) == insn)
	/* If this is last instruction in BB, move end marker one
	   instruction up.  */
	{
	  /* Jumps are always placed at the end of basic block.  */
	  jump_p = control_flow_insn_p (insn);

	  gcc_assert (!jump_p
		      || ((current_sched_info->flags & SCHED_RGN)
			  && IS_SPECULATION_BRANCHY_CHECK_P (insn))
		      || (current_sched_info->flags & SCHED_EBB));
	  
	  gcc_assert (BLOCK_FOR_INSN (PREV_INSN (insn)) == bb);

	  BB_END (bb) = PREV_INSN (insn);
	}

      gcc_assert (BB_END (bb) != last);

      if (jump_p)
	/* We move the block note along with jump.  */
	{
	  /* NT is needed for assertion below.  */
	  rtx nt = current_sched_info->next_tail;

	  note = NEXT_INSN (insn);
	  while (NOTE_NOT_BB_P (note) && note != nt)
	    note = NEXT_INSN (note);

	  if (note != nt
	      && (LABEL_P (note)
		  || BARRIER_P (note)))
	    note = NEXT_INSN (note);
      
	  gcc_assert (NOTE_INSN_BASIC_BLOCK_P (note));
	}
      else
	note = insn;

      NEXT_INSN (PREV_INSN (insn)) = NEXT_INSN (note);
      PREV_INSN (NEXT_INSN (note)) = PREV_INSN (insn);

      NEXT_INSN (note) = NEXT_INSN (last);
      PREV_INSN (NEXT_INSN (last)) = note;

      NEXT_INSN (last) = insn;
      PREV_INSN (insn) = last;

      bb = BLOCK_FOR_INSN (last);

      if (jump_p)
	{
	  fix_jump_move (insn);

	  if (BLOCK_FOR_INSN (insn) != bb)
	    move_block_after_check (insn);

	  gcc_assert (BB_END (bb) == last);
	}

      set_block_for_insn (insn, bb);    
  
      /* Update BB_END, if needed.  */
      if (BB_END (bb) == last)
	BB_END (bb) = insn;  
    }
  
  reemit_notes (insn);

  SCHED_GROUP_P (insn) = 0;  
}

/* The following structure describe an entry of the stack of choices.  */
struct choice_entry
{
  /* Ordinal number of the issued insn in the ready queue.  */
  int index;
  /* The number of the rest insns whose issues we should try.  */
  int rest;
  /* The number of issued essential insns.  */
  int n;
  /* State after issuing the insn.  */
  state_t state;
};

/* The following array is used to implement a stack of choices used in
   function max_issue.  */
static struct choice_entry *choice_stack;

/* The following variable value is number of essential insns issued on
   the current cycle.  An insn is essential one if it changes the
   processors state.  */
static int cycle_issued_insns;

/* The following variable value is maximal number of tries of issuing
   insns for the first cycle multipass insn scheduling.  We define
   this value as constant*(DFA_LOOKAHEAD**ISSUE_RATE).  We would not
   need this constraint if all real insns (with non-negative codes)
   had reservations because in this case the algorithm complexity is
   O(DFA_LOOKAHEAD**ISSUE_RATE).  Unfortunately, the dfa descriptions
   might be incomplete and such insn might occur.  For such
   descriptions, the complexity of algorithm (without the constraint)
   could achieve DFA_LOOKAHEAD ** N , where N is the queue length.  */
static int max_lookahead_tries;

/* The following value is value of hook
   `first_cycle_multipass_dfa_lookahead' at the last call of
   `max_issue'.  */
static int cached_first_cycle_multipass_dfa_lookahead = 0;

/* The following value is value of `issue_rate' at the last call of
   `sched_init'.  */
static int cached_issue_rate = 0;

/* The following function returns maximal (or close to maximal) number
   of insns which can be issued on the same cycle and one of which
   insns is insns with the best rank (the first insn in READY).  To
   make this function tries different samples of ready insns.  READY
   is current queue `ready'.  Global array READY_TRY reflects what
   insns are already issued in this try.  MAX_POINTS is the sum of points
   of all instructions in READY.  The function stops immediately,
   if it reached the such a solution, that all instruction can be issued.
   INDEX will contain index of the best insn in READY.  The following
   function is used only for first cycle multipass scheduling.  */
static int
max_issue (struct ready_list *ready, int *index, int max_points)
{
  int n, i, all, n_ready, best, delay, tries_num, points = -1;
  struct choice_entry *top;
  rtx insn;

  best = 0;
  memcpy (choice_stack->state, curr_state, dfa_state_size);
  top = choice_stack;
  top->rest = cached_first_cycle_multipass_dfa_lookahead;
  top->n = 0;
  n_ready = ready->n_ready;
  for (all = i = 0; i < n_ready; i++)
    if (!ready_try [i])
      all++;
  i = 0;
  tries_num = 0;
  for (;;)
    {
      if (top->rest == 0 || i >= n_ready)
	{
	  if (top == choice_stack)
	    break;
	  if (best < top - choice_stack && ready_try [0])
	    {
	      best = top - choice_stack;
	      *index = choice_stack [1].index;
	      points = top->n;
	      if (top->n == max_points || best == all)
		break;
	    }
	  i = top->index;
	  ready_try [i] = 0;
	  top--;
	  memcpy (curr_state, top->state, dfa_state_size);
	}
      else if (!ready_try [i])
	{
	  tries_num++;
	  if (tries_num > max_lookahead_tries)
	    break;
	  insn = ready_element (ready, i);
	  delay = state_transition (curr_state, insn);
	  if (delay < 0)
	    {
	      if (state_dead_lock_p (curr_state))
		top->rest = 0;
	      else
		top->rest--;
	      n = top->n;
	      if (memcmp (top->state, curr_state, dfa_state_size) != 0)
		n += ISSUE_POINTS (insn);
	      top++;
	      top->rest = cached_first_cycle_multipass_dfa_lookahead;
	      top->index = i;
	      top->n = n;
	      memcpy (top->state, curr_state, dfa_state_size);
	      ready_try [i] = 1;
	      i = -1;
	    }
	}
      i++;
    }
  while (top != choice_stack)
    {
      ready_try [top->index] = 0;
      top--;
    }
  memcpy (curr_state, choice_stack->state, dfa_state_size);  

  if (sched_verbose >= 4)    
    fprintf (sched_dump, ";;\t\tChoosed insn : %s; points: %d/%d\n",
	     (*current_sched_info->print_insn) (ready_element (ready, *index),
						0), 
	     points, max_points);
  
  return best;
}

/* The following function chooses insn from READY and modifies
   *N_READY and READY.  The following function is used only for first
   cycle multipass scheduling.  */

static rtx
choose_ready (struct ready_list *ready)
{
  int lookahead = 0;

  if (targetm.sched.first_cycle_multipass_dfa_lookahead)
    lookahead = targetm.sched.first_cycle_multipass_dfa_lookahead ();
  if (lookahead <= 0 || SCHED_GROUP_P (ready_element (ready, 0)))
    return ready_remove_first (ready);
  else
    {
      /* Try to choose the better insn.  */
      int index = 0, i, n;
      rtx insn;
      int more_issue, max_points, try_data = 1, try_control = 1;
      
      if (cached_first_cycle_multipass_dfa_lookahead != lookahead)
	{
	  cached_first_cycle_multipass_dfa_lookahead = lookahead;
	  max_lookahead_tries = 100;
	  for (i = 0; i < issue_rate; i++)
	    max_lookahead_tries *= lookahead;
	}
      insn = ready_element (ready, 0);
      if (INSN_CODE (insn) < 0)
	return ready_remove_first (ready);

      if (spec_info
	  && spec_info->flags & (PREFER_NON_DATA_SPEC
				 | PREFER_NON_CONTROL_SPEC))
	{
	  for (i = 0, n = ready->n_ready; i < n; i++)
	    {
	      rtx x;
	      ds_t s;

	      x = ready_element (ready, i);
	      s = TODO_SPEC (x);
	      
	      if (spec_info->flags & PREFER_NON_DATA_SPEC
		  && !(s & DATA_SPEC))
		{		  
		  try_data = 0;
		  if (!(spec_info->flags & PREFER_NON_CONTROL_SPEC)
		      || !try_control)
		    break;
		}
	      
	      if (spec_info->flags & PREFER_NON_CONTROL_SPEC
		  && !(s & CONTROL_SPEC))
		{
		  try_control = 0;
		  if (!(spec_info->flags & PREFER_NON_DATA_SPEC) || !try_data)
		    break;
		}
	    }
	}

      if ((!try_data && (TODO_SPEC (insn) & DATA_SPEC))
	  || (!try_control && (TODO_SPEC (insn) & CONTROL_SPEC))
	  || (targetm.sched.first_cycle_multipass_dfa_lookahead_guard_spec
	      && !targetm.sched.first_cycle_multipass_dfa_lookahead_guard_spec
	      (insn)))
	/* Discard speculative instruction that stands first in the ready
	   list.  */
	{
	  change_queue_index (insn, 1);
	  return 0;
	}

      max_points = ISSUE_POINTS (insn);
      more_issue = issue_rate - cycle_issued_insns - 1;

      for (i = 1; i < ready->n_ready; i++)
	{
	  insn = ready_element (ready, i);
	  ready_try [i]
	    = (INSN_CODE (insn) < 0
               || (!try_data && (TODO_SPEC (insn) & DATA_SPEC))
               || (!try_control && (TODO_SPEC (insn) & CONTROL_SPEC))
	       || (targetm.sched.first_cycle_multipass_dfa_lookahead_guard
		   && !targetm.sched.first_cycle_multipass_dfa_lookahead_guard
		   (insn)));

	  if (!ready_try [i] && more_issue-- > 0)
	    max_points += ISSUE_POINTS (insn);
	}

      if (max_issue (ready, &index, max_points) == 0)
	return ready_remove_first (ready);
      else
	return ready_remove (ready, index);
    }
}

/* Use forward list scheduling to rearrange insns of block pointed to by
   TARGET_BB, possibly bringing insns from subsequent blocks in the same
   region.  */

void
schedule_block (basic_block *target_bb, int rgn_n_insns1)
{
  struct ready_list ready;
  int i, first_cycle_insn_p;
  int can_issue_more;
  state_t temp_state = NULL;  /* It is used for multipass scheduling.  */
  int sort_p, advance, start_clock_var;

  /* Head/tail info for this block.  */
  rtx prev_head = current_sched_info->prev_head;
  rtx next_tail = current_sched_info->next_tail;
  rtx head = NEXT_INSN (prev_head);
  rtx tail = PREV_INSN (next_tail);

  /* We used to have code to avoid getting parameters moved from hard
     argument registers into pseudos.

     However, it was removed when it proved to be of marginal benefit
     and caused problems because schedule_block and compute_forward_dependences
     had different notions of what the "head" insn was.  */

  gcc_assert (head != tail || INSN_P (head));

  added_recovery_block_p = false;

  /* Debug info.  */
  if (sched_verbose)
    dump_new_block_header (0, *target_bb, head, tail);

  state_reset (curr_state);

  /* Allocate the ready list.  */
  readyp = &ready;
  ready.vec = NULL;
  ready_try = NULL;
  choice_stack = NULL;

  rgn_n_insns = -1;
  extend_ready (rgn_n_insns1 + 1);

  ready.first = ready.veclen - 1;
  ready.n_ready = 0;

  /* It is used for first cycle multipass scheduling.  */
  temp_state = alloca (dfa_state_size);

  if (targetm.sched.md_init)
    targetm.sched.md_init (sched_dump, sched_verbose, ready.veclen);

  /* We start inserting insns after PREV_HEAD.  */
  last_scheduled_insn = prev_head;

  gcc_assert (NOTE_P (last_scheduled_insn)
	      && BLOCK_FOR_INSN (last_scheduled_insn) == *target_bb);

  /* Initialize INSN_QUEUE.  Q_SIZE is the total number of insns in the
     queue.  */
  q_ptr = 0;
  q_size = 0;

  insn_queue = alloca ((max_insn_queue_index + 1) * sizeof (rtx));
  memset (insn_queue, 0, (max_insn_queue_index + 1) * sizeof (rtx));

  /* Start just before the beginning of time.  */
  clock_var = -1;

  /* We need queue and ready lists and clock_var be initialized 
     in try_ready () (which is called through init_ready_list ()).  */
  (*current_sched_info->init_ready_list) ();

  /* The algorithm is O(n^2) in the number of ready insns at any given
     time in the worst case.  Before reload we are more likely to have
     big lists so truncate them to a reasonable size.  */
  if (!reload_completed && ready.n_ready > MAX_SCHED_READY_INSNS)
    {
      ready_sort (&ready);

      /* Find first free-standing insn past MAX_SCHED_READY_INSNS.  */
      for (i = MAX_SCHED_READY_INSNS; i < ready.n_ready; i++)
	if (!SCHED_GROUP_P (ready_element (&ready, i)))
	  break;

      if (sched_verbose >= 2)
	{
	  fprintf (sched_dump,
		   ";;\t\tReady list on entry: %d insns\n", ready.n_ready);
	  fprintf (sched_dump,
		   ";;\t\t before reload => truncated to %d insns\n", i);
	}

      /* Delay all insns past it for 1 cycle.  */
      while (i < ready.n_ready)
	queue_insn (ready_remove (&ready, i), 1);
    }

  /* Now we can restore basic block notes and maintain precise cfg.  */
  restore_bb_notes (*target_bb);

  last_clock_var = -1;

  advance = 0;

  sort_p = TRUE;
  /* Loop until all the insns in BB are scheduled.  */
  while ((*current_sched_info->schedule_more_p) ())
    {
      do
	{
	  start_clock_var = clock_var;

	  clock_var++;

	  advance_one_cycle ();

	  /* Add to the ready list all pending insns that can be issued now.
	     If there are no ready insns, increment clock until one
	     is ready and add all pending insns at that point to the ready
	     list.  */
	  queue_to_ready (&ready);

	  gcc_assert (ready.n_ready);

	  if (sched_verbose >= 2)
	    {
	      fprintf (sched_dump, ";;\t\tReady list after queue_to_ready:  ");
	      debug_ready_list (&ready);
	    }
	  advance -= clock_var - start_clock_var;
	}
      while (advance > 0);

      if (sort_p)
	{
	  /* Sort the ready list based on priority.  */
	  ready_sort (&ready);

	  if (sched_verbose >= 2)
	    {
	      fprintf (sched_dump, ";;\t\tReady list after ready_sort:  ");
	      debug_ready_list (&ready);
	    }
	}

      /* Allow the target to reorder the list, typically for
	 better instruction bundling.  */
      if (sort_p && targetm.sched.reorder
	  && (ready.n_ready == 0
	      || !SCHED_GROUP_P (ready_element (&ready, 0))))
	can_issue_more =
	  targetm.sched.reorder (sched_dump, sched_verbose,
				 ready_lastpos (&ready),
				 &ready.n_ready, clock_var);
      else
	can_issue_more = issue_rate;

      first_cycle_insn_p = 1;
      cycle_issued_insns = 0;
      for (;;)
	{
	  rtx insn;
	  int cost;
	  bool asm_p = false;

	  if (sched_verbose >= 2)
	    {
	      fprintf (sched_dump, ";;\tReady list (t = %3d):  ",
		       clock_var);
	      debug_ready_list (&ready);
	    }

	  if (ready.n_ready == 0 
	      && can_issue_more 
	      && reload_completed) 
	    {
	      /* Allow scheduling insns directly from the queue in case
		 there's nothing better to do (ready list is empty) but
		 there are still vacant dispatch slots in the current cycle.  */
	      if (sched_verbose >= 6)
		fprintf(sched_dump,";;\t\tSecond chance\n");
	      memcpy (temp_state, curr_state, dfa_state_size);
	      if (early_queue_to_ready (temp_state, &ready))
		ready_sort (&ready);
	    }

	  if (ready.n_ready == 0 || !can_issue_more
	      || state_dead_lock_p (curr_state)
	      || !(*current_sched_info->schedule_more_p) ())
	    break;

	  /* Select and remove the insn from the ready list.  */
	  if (sort_p)
	    {
	      insn = choose_ready (&ready);
	      if (!insn)
		continue;
	    }
	  else
	    insn = ready_remove_first (&ready);

	  if (targetm.sched.dfa_new_cycle
	      && targetm.sched.dfa_new_cycle (sched_dump, sched_verbose,
					      insn, last_clock_var,
					      clock_var, &sort_p))
	    /* SORT_P is used by the target to override sorting
	       of the ready list.  This is needed when the target
	       has modified its internal structures expecting that
	       the insn will be issued next.  As we need the insn
	       to have the highest priority (so it will be returned by
	       the ready_remove_first call above), we invoke
	       ready_add (&ready, insn, true).
	       But, still, there is one issue: INSN can be later 
	       discarded by scheduler's front end through 
	       current_sched_info->can_schedule_ready_p, hence, won't
	       be issued next.  */ 
	    {
	      ready_add (&ready, insn, true);
              break;
	    }

	  sort_p = TRUE;
	  memcpy (temp_state, curr_state, dfa_state_size);
	  if (recog_memoized (insn) < 0)
	    {
	      asm_p = (GET_CODE (PATTERN (insn)) == ASM_INPUT
		       || asm_noperands (PATTERN (insn)) >= 0);
	      if (!first_cycle_insn_p && asm_p)
		/* This is asm insn which is tryed to be issued on the
		   cycle not first.  Issue it on the next cycle.  */
		cost = 1;
	      else
		/* A USE insn, or something else we don't need to
		   understand.  We can't pass these directly to
		   state_transition because it will trigger a
		   fatal error for unrecognizable insns.  */
		cost = 0;
	    }
	  else
	    {
	      cost = state_transition (temp_state, insn);
	      if (cost < 0)
		cost = 0;
	      else if (cost == 0)
		cost = 1;
	    }

	  if (cost >= 1)
	    {
	      queue_insn (insn, cost);
 	      if (SCHED_GROUP_P (insn))
 		{
 		  advance = cost;
 		  break;
 		}
 
	      continue;
	    }

	  if (current_sched_info->can_schedule_ready_p
	      && ! (*current_sched_info->can_schedule_ready_p) (insn))
	    /* We normally get here only if we don't want to move
	       insn from the split block.  */
	    {
	      TODO_SPEC (insn) = (TODO_SPEC (insn) & ~SPECULATIVE) | HARD_DEP;
	      continue;
	    }

	  /* DECISION is made.  */	
  
          if (TODO_SPEC (insn) & SPECULATIVE)
            generate_recovery_code (insn);

	  if (control_flow_insn_p (last_scheduled_insn)	     
	      /* This is used to to switch basic blocks by request
		 from scheduler front-end (actually, sched-ebb.c only).
		 This is used to process blocks with single fallthru
		 edge.  If succeeding block has jump, it [jump] will try
		 move at the end of current bb, thus corrupting CFG.  */
	      || current_sched_info->advance_target_bb (*target_bb, insn))
	    {
	      *target_bb = current_sched_info->advance_target_bb
		(*target_bb, 0);
	      
	      if (sched_verbose)
		{
		  rtx x;

		  x = next_real_insn (last_scheduled_insn);
		  gcc_assert (x);
		  dump_new_block_header (1, *target_bb, x, tail);
		}

	      last_scheduled_insn = bb_note (*target_bb);
	    }
 
	  /* Update counters, etc in the scheduler's front end.  */
	  (*current_sched_info->begin_schedule_ready) (insn,
						       last_scheduled_insn);
 
	  move_insn (insn);
	  last_scheduled_insn = insn;
	  
	  if (memcmp (curr_state, temp_state, dfa_state_size) != 0)
            {
              cycle_issued_insns++;
              memcpy (curr_state, temp_state, dfa_state_size);
            }

	  if (targetm.sched.variable_issue)
	    can_issue_more =
	      targetm.sched.variable_issue (sched_dump, sched_verbose,
					       insn, can_issue_more);
	  /* A naked CLOBBER or USE generates no instruction, so do
	     not count them against the issue rate.  */
	  else if (GET_CODE (PATTERN (insn)) != USE
		   && GET_CODE (PATTERN (insn)) != CLOBBER)
	    can_issue_more--;

	  advance = schedule_insn (insn);

	  /* After issuing an asm insn we should start a new cycle.  */
	  if (advance == 0 && asm_p)
	    advance = 1;
	  if (advance != 0)
	    break;

	  first_cycle_insn_p = 0;

	  /* Sort the ready list based on priority.  This must be
	     redone here, as schedule_insn may have readied additional
	     insns that will not be sorted correctly.  */
	  if (ready.n_ready > 0)
	    ready_sort (&ready);

	  if (targetm.sched.reorder2
	      && (ready.n_ready == 0
		  || !SCHED_GROUP_P (ready_element (&ready, 0))))
	    {
	      can_issue_more =
		targetm.sched.reorder2 (sched_dump, sched_verbose,
					ready.n_ready
					? ready_lastpos (&ready) : NULL,
					&ready.n_ready, clock_var);
	    }
	}
    }

  /* Debug info.  */
  if (sched_verbose)
    {
      fprintf (sched_dump, ";;\tReady list (final):  ");
      debug_ready_list (&ready);
    }

  if (current_sched_info->queue_must_finish_empty)
    /* Sanity check -- queue must be empty now.  Meaningless if region has
       multiple bbs.  */
    gcc_assert (!q_size && !ready.n_ready);
  else 
    {
      /* We must maintain QUEUE_INDEX between blocks in region.  */
      for (i = ready.n_ready - 1; i >= 0; i--)
	{
	  rtx x;
	  
	  x = ready_element (&ready, i);
	  QUEUE_INDEX (x) = QUEUE_NOWHERE;
	  TODO_SPEC (x) = (TODO_SPEC (x) & ~SPECULATIVE) | HARD_DEP;
	}

      if (q_size)   
	for (i = 0; i <= max_insn_queue_index; i++)
	  {
	    rtx link;
	    for (link = insn_queue[i]; link; link = XEXP (link, 1))
	      {
		rtx x;

		x = XEXP (link, 0);
		QUEUE_INDEX (x) = QUEUE_NOWHERE;
		TODO_SPEC (x) = (TODO_SPEC (x) & ~SPECULATIVE) | HARD_DEP;
	      }
	    free_INSN_LIST_list (&insn_queue[i]);
	  }
    }

  if (!current_sched_info->queue_must_finish_empty
      || added_recovery_block_p)
    {
      /* INSN_TICK (minimum clock tick at which the insn becomes
         ready) may be not correct for the insn in the subsequent
         blocks of the region.  We should use a correct value of
         `clock_var' or modify INSN_TICK.  It is better to keep
         clock_var value equal to 0 at the start of a basic block.
         Therefore we modify INSN_TICK here.  */
      fix_inter_tick (NEXT_INSN (prev_head), last_scheduled_insn);
    }

  if (targetm.sched.md_finish)
    targetm.sched.md_finish (sched_dump, sched_verbose);

  /* Update head/tail boundaries.  */
  head = NEXT_INSN (prev_head);
  tail = last_scheduled_insn;

  /* Restore-other-notes: NOTE_LIST is the end of a chain of notes
     previously found among the insns.  Insert them at the beginning
     of the insns.  */
  if (note_list != 0)
    {
      basic_block head_bb = BLOCK_FOR_INSN (head);
      rtx note_head = note_list;

      while (PREV_INSN (note_head))
	{
	  set_block_for_insn (note_head, head_bb);
	  note_head = PREV_INSN (note_head);
	}
      /* In the above cycle we've missed this note:  */
      set_block_for_insn (note_head, head_bb);

      PREV_INSN (note_head) = PREV_INSN (head);
      NEXT_INSN (PREV_INSN (head)) = note_head;
      PREV_INSN (head) = note_list;
      NEXT_INSN (note_list) = head;
      head = note_head;
    }

  /* Debugging.  */
  if (sched_verbose)
    {
      fprintf (sched_dump, ";;   total time = %d\n;;   new head = %d\n",
	       clock_var, INSN_UID (head));
      fprintf (sched_dump, ";;   new tail = %d\n\n",
	       INSN_UID (tail));
    }

  current_sched_info->head = head;
  current_sched_info->tail = tail;

  free (ready.vec);

  free (ready_try);
  for (i = 0; i <= rgn_n_insns; i++)
    free (choice_stack [i].state);
  free (choice_stack);
}

/* Set_priorities: compute priority of each insn in the block.  */

int
set_priorities (rtx head, rtx tail)
{
  rtx insn;
  int n_insn;
  int sched_max_insns_priority = 
	current_sched_info->sched_max_insns_priority;
  rtx prev_head;

  if (head == tail && (! INSN_P (head)))
    return 0;

  n_insn = 0;

  prev_head = PREV_INSN (head);
  for (insn = tail; insn != prev_head; insn = PREV_INSN (insn))
    {
      if (!INSN_P (insn))
	continue;

      n_insn++;
      (void) priority (insn);

      if (INSN_PRIORITY_KNOWN (insn))
	sched_max_insns_priority =
	  MAX (sched_max_insns_priority, INSN_PRIORITY (insn)); 
    }

  current_sched_info->sched_max_insns_priority = sched_max_insns_priority;

  return n_insn;
}

/* Next LUID to assign to an instruction.  */
static int luid;

/* Initialize some global state for the scheduler.  */

void
sched_init (void)
{
  basic_block b;
  rtx insn;
  int i;

  /* Switch to working copy of sched_info.  */
  memcpy (&current_sched_info_var, current_sched_info,
	  sizeof (current_sched_info_var));
  current_sched_info = &current_sched_info_var;
      
  /* Disable speculative loads in their presence if cc0 defined.  */
#ifdef HAVE_cc0
  flag_schedule_speculative_load = 0;
#endif

  /* Set dump and sched_verbose for the desired debugging output.  If no
     dump-file was specified, but -fsched-verbose=N (any N), print to stderr.
     For -fsched-verbose=N, N>=10, print everything to stderr.  */
  sched_verbose = sched_verbose_param;
  if (sched_verbose_param == 0 && dump_file)
    sched_verbose = 1;
  sched_dump = ((sched_verbose_param >= 10 || !dump_file)
		? stderr : dump_file);

  /* Initialize SPEC_INFO.  */
  if (targetm.sched.set_sched_flags)
    {
      spec_info = &spec_info_var;
      targetm.sched.set_sched_flags (spec_info);
      if (current_sched_info->flags & DO_SPECULATION)
	spec_info->weakness_cutoff =
	  (PARAM_VALUE (PARAM_SCHED_SPEC_PROB_CUTOFF) * MAX_DEP_WEAK) / 100;
      else
	/* So we won't read anything accidentally.  */
	spec_info = 0;
#ifdef ENABLE_CHECKING
      check_sched_flags ();
#endif
    }
  else
    /* So we won't read anything accidentally.  */
    spec_info = 0;

  /* Initialize issue_rate.  */
  if (targetm.sched.issue_rate)
    issue_rate = targetm.sched.issue_rate ();
  else
    issue_rate = 1;

  if (cached_issue_rate != issue_rate)
    {
      cached_issue_rate = issue_rate;
      /* To invalidate max_lookahead_tries:  */
      cached_first_cycle_multipass_dfa_lookahead = 0;
    }

  old_max_uid = 0;
  h_i_d = 0;
  extend_h_i_d ();

  for (i = 0; i < old_max_uid; i++)
    {
      h_i_d[i].cost = -1;
      h_i_d[i].todo_spec = HARD_DEP;
      h_i_d[i].queue_index = QUEUE_NOWHERE;
      h_i_d[i].tick = INVALID_TICK;
      h_i_d[i].inter_tick = INVALID_TICK;
    }

  if (targetm.sched.init_dfa_pre_cycle_insn)
    targetm.sched.init_dfa_pre_cycle_insn ();

  if (targetm.sched.init_dfa_post_cycle_insn)
    targetm.sched.init_dfa_post_cycle_insn ();

  dfa_start ();
  dfa_state_size = state_size ();
  curr_state = xmalloc (dfa_state_size);

  h_i_d[0].luid = 0;
  luid = 1;
  FOR_EACH_BB (b)
    for (insn = BB_HEAD (b); ; insn = NEXT_INSN (insn))
      {
	INSN_LUID (insn) = luid;

	/* Increment the next luid, unless this is a note.  We don't
	   really need separate IDs for notes and we don't want to
	   schedule differently depending on whether or not there are
	   line-number notes, i.e., depending on whether or not we're
	   generating debugging information.  */
	if (!NOTE_P (insn))
	  ++luid;

	if (insn == BB_END (b))
	  break;
      }

  init_dependency_caches (luid);

  init_alias_analysis ();

  line_note_head = 0;
  old_last_basic_block = 0;
  glat_start = 0;  
  glat_end = 0;
  extend_bb (0);

  if (current_sched_info->flags & USE_GLAT)
    init_glat ();

  /* Compute INSN_REG_WEIGHT for all blocks.  We must do this before
     removing death notes.  */
  FOR_EACH_BB_REVERSE (b)
    find_insn_reg_weight (b);

  if (targetm.sched.md_init_global)
      targetm.sched.md_init_global (sched_dump, sched_verbose, old_max_uid);

  nr_begin_data = nr_begin_control = nr_be_in_data = nr_be_in_control = 0;
  before_recovery = 0;

#ifdef ENABLE_CHECKING
  /* This is used preferably for finding bugs in check_cfg () itself.  */
  check_cfg (0, 0);
#endif
}

/* Free global data used during insn scheduling.  */

void
sched_finish (void)
{
  free (h_i_d);
  free (curr_state);
  dfa_finish ();
  free_dependency_caches ();
  end_alias_analysis ();
  free (line_note_head);
  free_glat ();

  if (targetm.sched.md_finish_global)
    targetm.sched.md_finish_global (sched_dump, sched_verbose);
  
  if (spec_info && spec_info->dump)
    {
      char c = reload_completed ? 'a' : 'b';

      fprintf (spec_info->dump,
	       ";; %s:\n", current_function_name ());

      fprintf (spec_info->dump,
               ";; Procedure %cr-begin-data-spec motions == %d\n",
               c, nr_begin_data);
      fprintf (spec_info->dump,
               ";; Procedure %cr-be-in-data-spec motions == %d\n",
               c, nr_be_in_data);
      fprintf (spec_info->dump,
               ";; Procedure %cr-begin-control-spec motions == %d\n",
               c, nr_begin_control);
      fprintf (spec_info->dump,
               ";; Procedure %cr-be-in-control-spec motions == %d\n",
               c, nr_be_in_control);
    }

#ifdef ENABLE_CHECKING
  /* After reload ia64 backend clobbers CFG, so can't check anything.  */
  if (!reload_completed)
    check_cfg (0, 0);
#endif

  current_sched_info = NULL;
}

/* Fix INSN_TICKs of the instructions in the current block as well as
   INSN_TICKs of their dependents.
   HEAD and TAIL are the begin and the end of the current scheduled block.  */
static void
fix_inter_tick (rtx head, rtx tail)
{
  /* Set of instructions with corrected INSN_TICK.  */
  bitmap_head processed;
  int next_clock = clock_var + 1;

  bitmap_initialize (&processed, 0);
  
  /* Iterates over scheduled instructions and fix their INSN_TICKs and
     INSN_TICKs of dependent instructions, so that INSN_TICKs are consistent
     across different blocks.  */
  for (tail = NEXT_INSN (tail); head != tail; head = NEXT_INSN (head))
    {
      if (INSN_P (head))
	{
	  int tick;
	  rtx link;
                  
	  tick = INSN_TICK (head);
	  gcc_assert (tick >= MIN_TICK);
	  
	  /* Fix INSN_TICK of instruction from just scheduled block.  */
	  if (!bitmap_bit_p (&processed, INSN_LUID (head)))
	    {
	      bitmap_set_bit (&processed, INSN_LUID (head));
	      tick -= next_clock;
	      
	      if (tick < MIN_TICK)
		tick = MIN_TICK;
	      
	      INSN_TICK (head) = tick;		 
	    }
	  
	  for (link = INSN_DEPEND (head); link; link = XEXP (link, 1))
	    {
	      rtx next;
	      
	      next = XEXP (link, 0);
	      tick = INSN_TICK (next);

	      if (tick != INVALID_TICK
		  /* If NEXT has its INSN_TICK calculated, fix it.
		     If not - it will be properly calculated from
		     scratch later in fix_tick_ready.  */
		  && !bitmap_bit_p (&processed, INSN_LUID (next)))
		{
		  bitmap_set_bit (&processed, INSN_LUID (next));
		  tick -= next_clock;
		  
		  if (tick < MIN_TICK)
		    tick = MIN_TICK;
		  
		  if (tick > INTER_TICK (next))
		    INTER_TICK (next) = tick;
		  else
		    tick = INTER_TICK (next);
		  
		  INSN_TICK (next) = tick;
		}
	    }
	}
    }
  bitmap_clear (&processed);
}
  
/* Check if NEXT is ready to be added to the ready or queue list.
   If "yes", add it to the proper list.
   Returns:
      -1 - is not ready yet,
       0 - added to the ready list,
   0 < N - queued for N cycles.  */
int
try_ready (rtx next)
{  
  ds_t old_ts, *ts;
  rtx link;

  ts = &TODO_SPEC (next);
  old_ts = *ts;

  gcc_assert (!(old_ts & ~(SPECULATIVE | HARD_DEP))
	      && ((old_ts & HARD_DEP)
		  || (old_ts & SPECULATIVE)));
  
  if (!(current_sched_info->flags & DO_SPECULATION))
    {
      if (!LOG_LINKS (next))
        *ts &= ~HARD_DEP;
    }
  else
    {
      *ts &= ~SPECULATIVE & ~HARD_DEP;          
  
      link = LOG_LINKS (next);
      if (link)
        {
          /* LOG_LINKS are maintained sorted. 
             So if DEP_STATUS of the first dep is SPECULATIVE,
             than all other deps are speculative too.  */
          if (DEP_STATUS (link) & SPECULATIVE)          
            {          
              /* Now we've got NEXT with speculative deps only.
                 1. Look at the deps to see what we have to do.
                 2. Check if we can do 'todo'.  */
	      *ts = DEP_STATUS (link) & SPECULATIVE;
              while ((link = XEXP (link, 1)))
		*ts = ds_merge (*ts, DEP_STATUS (link) & SPECULATIVE);

	      if (dep_weak (*ts) < spec_info->weakness_cutoff)
		/* Too few points.  */
		*ts = (*ts & ~SPECULATIVE) | HARD_DEP;
	    }
          else
            *ts |= HARD_DEP;
        }
    }
  
  if (*ts & HARD_DEP)
    gcc_assert (*ts == old_ts
		&& QUEUE_INDEX (next) == QUEUE_NOWHERE);
  else if (current_sched_info->new_ready)
    *ts = current_sched_info->new_ready (next, *ts);  

  /* * if !(old_ts & SPECULATIVE) (e.g. HARD_DEP or 0), then insn might 
     have its original pattern or changed (speculative) one.  This is due
     to changing ebb in region scheduling.
     * But if (old_ts & SPECULATIVE), then we are pretty sure that insn
     has speculative pattern.
     
     We can't assert (!(*ts & HARD_DEP) || *ts == old_ts) here because
     control-speculative NEXT could have been discarded by sched-rgn.c
     (the same case as when discarded by can_schedule_ready_p ()).  */
  
  if ((*ts & SPECULATIVE)
      /* If (old_ts == *ts), then (old_ts & SPECULATIVE) and we don't 
	 need to change anything.  */
      && *ts != old_ts)
    {
      int res;
      rtx new_pat;
      
      gcc_assert ((*ts & SPECULATIVE) && !(*ts & ~SPECULATIVE));
      
      res = speculate_insn (next, *ts, &new_pat);
	
      switch (res)
	{
	case -1:
	  /* It would be nice to change DEP_STATUS of all dependences,
	     which have ((DEP_STATUS & SPECULATIVE) == *ts) to HARD_DEP,
	     so we won't reanalyze anything.  */
	  *ts = (*ts & ~SPECULATIVE) | HARD_DEP;
	  break;
	  
	case 0:
	  /* We follow the rule, that every speculative insn
	     has non-null ORIG_PAT.  */
	  if (!ORIG_PAT (next))
	    ORIG_PAT (next) = PATTERN (next);
	  break;
	  
	case 1:                  
	  if (!ORIG_PAT (next))
	    /* If we gonna to overwrite the original pattern of insn,
	       save it.  */
	    ORIG_PAT (next) = PATTERN (next);
	  
	  change_pattern (next, new_pat);
	  break;
	  
	default:
	  gcc_unreachable ();
	}
    }
  
  /* We need to restore pattern only if (*ts == 0), because otherwise it is
     either correct (*ts & SPECULATIVE),
     or we simply don't care (*ts & HARD_DEP).  */
  
  gcc_assert (!ORIG_PAT (next)
	      || !IS_SPECULATION_BRANCHY_CHECK_P (next));
  
  if (*ts & HARD_DEP)
    {
      /* We can't assert (QUEUE_INDEX (next) == QUEUE_NOWHERE) here because
	 control-speculative NEXT could have been discarded by sched-rgn.c
	 (the same case as when discarded by can_schedule_ready_p ()).  */
      /*gcc_assert (QUEUE_INDEX (next) == QUEUE_NOWHERE);*/
      
      change_queue_index (next, QUEUE_NOWHERE);
      return -1;
    }
  else if (!(*ts & BEGIN_SPEC) && ORIG_PAT (next) && !IS_SPECULATION_CHECK_P (next))
    /* We should change pattern of every previously speculative 
       instruction - and we determine if NEXT was speculative by using
       ORIG_PAT field.  Except one case - speculation checks have ORIG_PAT
       pat too, so skip them.  */
    {
      change_pattern (next, ORIG_PAT (next));
      ORIG_PAT (next) = 0;
    }

  if (sched_verbose >= 2)
    {	      
      int s = TODO_SPEC (next);
          
      fprintf (sched_dump, ";;\t\tdependencies resolved: insn %s",
               (*current_sched_info->print_insn) (next, 0));
          
      if (spec_info && spec_info->dump)
        {
          if (s & BEGIN_DATA)
            fprintf (spec_info->dump, "; data-spec;");
          if (s & BEGIN_CONTROL)
            fprintf (spec_info->dump, "; control-spec;");
          if (s & BE_IN_CONTROL)
            fprintf (spec_info->dump, "; in-control-spec;");
        }

      fprintf (sched_dump, "\n");
    }          
  
  adjust_priority (next);
        
  return fix_tick_ready (next);
}

/* Calculate INSN_TICK of NEXT and add it to either ready or queue list.  */
static int
fix_tick_ready (rtx next)
{
  rtx link;
  int tick, delay;

  link = RESOLVED_DEPS (next);
      
  if (link)
    {
      int full_p;

      tick = INSN_TICK (next);
      /* if tick is not equal to INVALID_TICK, then update
	 INSN_TICK of NEXT with the most recent resolved dependence
	 cost.  Otherwise, recalculate from scratch.  */
      full_p = tick == INVALID_TICK;
      do
        {        
          rtx pro;
          int tick1;
              
          pro = XEXP (link, 0);
	  gcc_assert (INSN_TICK (pro) >= MIN_TICK);

          tick1 = INSN_TICK (pro) + insn_cost (pro, link, next);
          if (tick1 > tick)
            tick = tick1;
        }
      while ((link = XEXP (link, 1)) && full_p);
    }
  else
    tick = -1;

  INSN_TICK (next) = tick;

  delay = tick - clock_var;
  if (delay <= 0)
    delay = QUEUE_READY;

  change_queue_index (next, delay);

  return delay;
}

/* Move NEXT to the proper queue list with (DELAY >= 1),
   or add it to the ready list (DELAY == QUEUE_READY),
   or remove it from ready and queue lists at all (DELAY == QUEUE_NOWHERE).  */
static void
change_queue_index (rtx next, int delay)
{
  int i = QUEUE_INDEX (next);

  gcc_assert (QUEUE_NOWHERE <= delay && delay <= max_insn_queue_index 
	      && delay != 0);
  gcc_assert (i != QUEUE_SCHEDULED);
  
  if ((delay > 0 && NEXT_Q_AFTER (q_ptr, delay) == i)
      || (delay < 0 && delay == i))
    /* We have nothing to do.  */
    return;

  /* Remove NEXT from wherever it is now.  */
  if (i == QUEUE_READY)
    ready_remove_insn (next);
  else if (i >= 0)
    queue_remove (next);
    
  /* Add it to the proper place.  */
  if (delay == QUEUE_READY)
    ready_add (readyp, next, false);
  else if (delay >= 1)
    queue_insn (next, delay);
    
  if (sched_verbose >= 2)
    {	      
      fprintf (sched_dump, ";;\t\ttick updated: insn %s",
	       (*current_sched_info->print_insn) (next, 0));
      
      if (delay == QUEUE_READY)
	fprintf (sched_dump, " into ready\n");
      else if (delay >= 1)
	fprintf (sched_dump, " into queue with cost=%d\n", delay);
      else
	fprintf (sched_dump, " removed from ready or queue lists\n");
    }
}

/* INSN is being scheduled.  Resolve the dependence between INSN and NEXT.  */
static void
resolve_dep (rtx next, rtx insn)
{
  rtx dep;

  INSN_DEP_COUNT (next)--;
  
  dep = remove_list_elem (insn, &LOG_LINKS (next));
  XEXP (dep, 1) = RESOLVED_DEPS (next);
  RESOLVED_DEPS (next) = dep;
  
  gcc_assert ((INSN_DEP_COUNT (next) != 0 || !LOG_LINKS (next))
	      && (LOG_LINKS (next) || INSN_DEP_COUNT (next) == 0));
}

/* Extend H_I_D data.  */
static void
extend_h_i_d (void)
{
  /* We use LUID 0 for the fake insn (UID 0) which holds dependencies for
     pseudos which do not cross calls.  */
  int new_max_uid = get_max_uid() + 1;  

  h_i_d = xrecalloc (h_i_d, new_max_uid, old_max_uid, sizeof (*h_i_d));
  old_max_uid = new_max_uid;

  if (targetm.sched.h_i_d_extended)
    targetm.sched.h_i_d_extended ();
}

/* Extend READY, READY_TRY and CHOICE_STACK arrays.
   N_NEW_INSNS is the number of additional elements to allocate.  */
static void
extend_ready (int n_new_insns)
{
  int i;

  readyp->veclen = rgn_n_insns + n_new_insns + 1 + issue_rate;
  readyp->vec = XRESIZEVEC (rtx, readyp->vec, readyp->veclen);
 
  ready_try = xrecalloc (ready_try, rgn_n_insns + n_new_insns + 1,
			 rgn_n_insns + 1, sizeof (char));

  rgn_n_insns += n_new_insns;

  choice_stack = XRESIZEVEC (struct choice_entry, choice_stack,
			     rgn_n_insns + 1);

  for (i = rgn_n_insns; n_new_insns--; i--)
    choice_stack[i].state = xmalloc (dfa_state_size);
}

/* Extend global scheduler structures (those, that live across calls to
   schedule_block) to include information about just emitted INSN.  */
static void
extend_global (rtx insn)
{
  gcc_assert (INSN_P (insn));
  /* These structures have scheduler scope.  */
  extend_h_i_d ();
  init_h_i_d (insn);

  extend_dependency_caches (1, 0);
}

/* Extends global and local scheduler structures to include information
   about just emitted INSN.  */
static void
extend_all (rtx insn)
{ 
  extend_global (insn);

  /* These structures have block scope.  */
  extend_ready (1);
  
  (*current_sched_info->add_remove_insn) (insn, 0);
}

/* Initialize h_i_d entry of the new INSN with default values.
   Values, that are not explicitly initialized here, hold zero.  */
static void
init_h_i_d (rtx insn)
{
  INSN_LUID (insn) = luid++;
  INSN_COST (insn) = -1;
  TODO_SPEC (insn) = HARD_DEP;
  QUEUE_INDEX (insn) = QUEUE_NOWHERE;
  INSN_TICK (insn) = INVALID_TICK;
  INTER_TICK (insn) = INVALID_TICK;
  find_insn_reg_weight1 (insn);  
}

/* Generates recovery code for INSN.  */
static void
generate_recovery_code (rtx insn)
{
  if (TODO_SPEC (insn) & BEGIN_SPEC)
    begin_speculative_block (insn);
  
  /* Here we have insn with no dependencies to
     instructions other then CHECK_SPEC ones.  */
  
  if (TODO_SPEC (insn) & BE_IN_SPEC)
    add_to_speculative_block (insn);
}

/* Helper function.
   Tries to add speculative dependencies of type FS between instructions
   in LINK list and TWIN.  */
static void
process_insn_depend_be_in_spec (rtx link, rtx twin, ds_t fs)
{
  for (; link; link = XEXP (link, 1))
    {
      ds_t ds;
      rtx consumer;

      consumer = XEXP (link, 0);

      ds = DEP_STATUS (link);

      if (/* If we want to create speculative dep.  */
	  fs
	  /* And we can do that because this is a true dep.  */
	  && (ds & DEP_TYPES) == DEP_TRUE)
	{
	  gcc_assert (!(ds & BE_IN_SPEC));

	  if (/* If this dep can be overcome with 'begin speculation'.  */
	      ds & BEGIN_SPEC)
	    /* Then we have a choice: keep the dep 'begin speculative'
	       or transform it into 'be in speculative'.  */
	    {
	      if (/* In try_ready we assert that if insn once became ready
		     it can be removed from the ready (or queue) list only
		     due to backend decision.  Hence we can't let the
		     probability of the speculative dep to decrease.  */
		  dep_weak (ds) <= dep_weak (fs))
		/* Transform it to be in speculative.  */
		ds = (ds & ~BEGIN_SPEC) | fs;
	    }
	  else
	    /* Mark the dep as 'be in speculative'.  */
	    ds |= fs;
	}

      add_back_forw_dep (consumer, twin, REG_NOTE_KIND (link), ds);
    }
}

/* Generates recovery code for BEGIN speculative INSN.  */
static void
begin_speculative_block (rtx insn)
{
  if (TODO_SPEC (insn) & BEGIN_DATA)
    nr_begin_data++;      
  if (TODO_SPEC (insn) & BEGIN_CONTROL)
    nr_begin_control++;

  create_check_block_twin (insn, false);

  TODO_SPEC (insn) &= ~BEGIN_SPEC;
}

/* Generates recovery code for BE_IN speculative INSN.  */
static void
add_to_speculative_block (rtx insn)
{
  ds_t ts;
  rtx link, twins = NULL;

  ts = TODO_SPEC (insn);
  gcc_assert (!(ts & ~BE_IN_SPEC));

  if (ts & BE_IN_DATA)
    nr_be_in_data++;
  if (ts & BE_IN_CONTROL)
    nr_be_in_control++;

  TODO_SPEC (insn) &= ~BE_IN_SPEC;
  gcc_assert (!TODO_SPEC (insn));
  
  DONE_SPEC (insn) |= ts;

  /* First we convert all simple checks to branchy.  */
  for (link = LOG_LINKS (insn); link;)
    {
      rtx check;

      check = XEXP (link, 0);

      if (IS_SPECULATION_SIMPLE_CHECK_P (check))
	{
	  create_check_block_twin (check, true);
	  link = LOG_LINKS (insn);
	}
      else
	link = XEXP (link, 1);
    }

  clear_priorities (insn);
 
  do
    {
      rtx link, check, twin;
      basic_block rec;

      link = LOG_LINKS (insn);
      gcc_assert (!(DEP_STATUS (link) & BEGIN_SPEC)
		  && (DEP_STATUS (link) & BE_IN_SPEC)
		  && (DEP_STATUS (link) & DEP_TYPES) == DEP_TRUE);

      check = XEXP (link, 0);

      gcc_assert (!IS_SPECULATION_CHECK_P (check) && !ORIG_PAT (check)
		  && QUEUE_INDEX (check) == QUEUE_NOWHERE);
      
      rec = BLOCK_FOR_INSN (check);
      
      twin = emit_insn_before (copy_rtx (PATTERN (insn)), BB_END (rec));
      extend_global (twin);

      RESOLVED_DEPS (twin) = copy_DEPS_LIST_list (RESOLVED_DEPS (insn));

      if (sched_verbose && spec_info->dump)
        /* INSN_BB (insn) isn't determined for twin insns yet.
           So we can't use current_sched_info->print_insn.  */
        fprintf (spec_info->dump, ";;\t\tGenerated twin insn : %d/rec%d\n",
                 INSN_UID (twin), rec->index);

      twins = alloc_INSN_LIST (twin, twins);

      /* Add dependences between TWIN and all appropriate
	 instructions from REC.  */
      do
	{	  
	  add_back_forw_dep (twin, check, REG_DEP_TRUE, DEP_TRUE);
	  
	  do	    	  
	    {  
	      link = XEXP (link, 1);
	      if (link)
		{
		  check = XEXP (link, 0);
		  if (BLOCK_FOR_INSN (check) == rec)
		    break;
		}
	      else
		break;
	    }
	  while (1);
	}
      while (link);

      process_insn_depend_be_in_spec (INSN_DEPEND (insn), twin, ts);

      for (link = LOG_LINKS (insn); link;)
	{
	  check = XEXP (link, 0);

	  if (BLOCK_FOR_INSN (check) == rec)
	    {
	      delete_back_forw_dep (insn, check);
	      link = LOG_LINKS (insn);
	    }
	  else
	    link = XEXP (link, 1);
	}
    }
  while (LOG_LINKS (insn));

  /* We can't add the dependence between insn and twin earlier because
     that would make twin appear in the INSN_DEPEND (insn).  */
  while (twins)
    {
      rtx twin;

      twin = XEXP (twins, 0);
      calc_priorities (twin);
      add_back_forw_dep (twin, insn, REG_DEP_OUTPUT, DEP_OUTPUT);

      twin = XEXP (twins, 1);
      free_INSN_LIST_node (twins);
      twins = twin;      
    }
}

/* Extends and fills with zeros (only the new part) array pointed to by P.  */
void *
xrecalloc (void *p, size_t new_nmemb, size_t old_nmemb, size_t size)
{
  gcc_assert (new_nmemb >= old_nmemb);
  p = XRESIZEVAR (void, p, new_nmemb * size);
  memset (((char *) p) + old_nmemb * size, 0, (new_nmemb - old_nmemb) * size);
  return p;
}

/* Return the probability of speculation success for the speculation
   status DS.  */
static dw_t
dep_weak (ds_t ds)
{
  ds_t res = 1, dt;
  int n = 0;

  dt = FIRST_SPEC_TYPE;
  do
    {
      if (ds & dt)
	{
	  res *= (ds_t) get_dep_weak (ds, dt);
	  n++;
	}

      if (dt == LAST_SPEC_TYPE)
	break;
      dt <<= SPEC_TYPE_SHIFT;
    }
  while (1);

  gcc_assert (n);
  while (--n)
    res /= MAX_DEP_WEAK;

  if (res < MIN_DEP_WEAK)
    res = MIN_DEP_WEAK;

  gcc_assert (res <= MAX_DEP_WEAK);

  return (dw_t) res;
}

/* Helper function.
   Find fallthru edge from PRED.  */
static edge
find_fallthru_edge (basic_block pred)
{
  edge e;
  edge_iterator ei;
  basic_block succ;

  succ = pred->next_bb;
  gcc_assert (succ->prev_bb == pred);

  if (EDGE_COUNT (pred->succs) <= EDGE_COUNT (succ->preds))
    {
      FOR_EACH_EDGE (e, ei, pred->succs)
	if (e->flags & EDGE_FALLTHRU)
	  {
	    gcc_assert (e->dest == succ);
	    return e;
	  }
    }
  else
    {
      FOR_EACH_EDGE (e, ei, succ->preds)
	if (e->flags & EDGE_FALLTHRU)
	  {
	    gcc_assert (e->src == pred);
	    return e;
	  }
    }

  return NULL;
}

/* Initialize BEFORE_RECOVERY variable.  */
static void
init_before_recovery (void)
{
  basic_block last;
  edge e;

  last = EXIT_BLOCK_PTR->prev_bb;
  e = find_fallthru_edge (last);

  if (e)
    {
      /* We create two basic blocks: 
         1. Single instruction block is inserted right after E->SRC
         and has jump to 
         2. Empty block right before EXIT_BLOCK.
         Between these two blocks recovery blocks will be emitted.  */

      basic_block single, empty;
      rtx x, label;

      single = create_empty_bb (last);
      empty = create_empty_bb (single);            

      single->count = last->count;     
      empty->count = last->count;
      single->frequency = last->frequency;
      empty->frequency = last->frequency;
      BB_COPY_PARTITION (single, last);
      BB_COPY_PARTITION (empty, last);

      redirect_edge_succ (e, single);
      make_single_succ_edge (single, empty, 0);
      make_single_succ_edge (empty, EXIT_BLOCK_PTR,
			     EDGE_FALLTHRU | EDGE_CAN_FALLTHRU);

      label = block_label (empty);
      x = emit_jump_insn_after (gen_jump (label), BB_END (single));
      JUMP_LABEL (x) = label;
      LABEL_NUSES (label)++;
      extend_global (x);
          
      emit_barrier_after (x);

      add_block (empty, 0);
      add_block (single, 0);

      before_recovery = single;

      if (sched_verbose >= 2 && spec_info->dump)
        fprintf (spec_info->dump,
		 ";;\t\tFixed fallthru to EXIT : %d->>%d->%d->>EXIT\n", 
                 last->index, single->index, empty->index);      
    }
  else
    before_recovery = last;
}

/* Returns new recovery block.  */
static basic_block
create_recovery_block (void)
{
  rtx label;
  rtx barrier;
  basic_block rec;
  
  added_recovery_block_p = true;

  if (!before_recovery)
    init_before_recovery ();

  barrier = get_last_bb_insn (before_recovery);
  gcc_assert (BARRIER_P (barrier));

  label = emit_label_after (gen_label_rtx (), barrier);

  rec = create_basic_block (label, label, before_recovery);

  /* Recovery block always end with an unconditional jump.  */
  emit_barrier_after (BB_END (rec));

  if (BB_PARTITION (before_recovery) != BB_UNPARTITIONED)
    BB_SET_PARTITION (rec, BB_COLD_PARTITION);
  
  if (sched_verbose && spec_info->dump)    
    fprintf (spec_info->dump, ";;\t\tGenerated recovery block rec%d\n",
             rec->index);

  before_recovery = rec;

  return rec;
}

/* This function creates recovery code for INSN.  If MUTATE_P is nonzero,
   INSN is a simple check, that should be converted to branchy one.  */
static void
create_check_block_twin (rtx insn, bool mutate_p)
{
  basic_block rec;
  rtx label, check, twin, link;
  ds_t fs;

  gcc_assert (ORIG_PAT (insn)
	      && (!mutate_p 
		  || (IS_SPECULATION_SIMPLE_CHECK_P (insn)
		      && !(TODO_SPEC (insn) & SPECULATIVE))));

  /* Create recovery block.  */
  if (mutate_p || targetm.sched.needs_block_p (insn))
    {
      rec = create_recovery_block ();
      label = BB_HEAD (rec);
    }
  else
    {
      rec = EXIT_BLOCK_PTR;
      label = 0;
    }

  /* Emit CHECK.  */
  check = targetm.sched.gen_check (insn, label, mutate_p);

  if (rec != EXIT_BLOCK_PTR)
    {
      /* To have mem_reg alive at the beginning of second_bb,
	 we emit check BEFORE insn, so insn after splitting 
	 insn will be at the beginning of second_bb, which will
	 provide us with the correct life information.  */
      check = emit_jump_insn_before (check, insn);
      JUMP_LABEL (check) = label;
      LABEL_NUSES (label)++;
    }
  else
    check = emit_insn_before (check, insn);

  /* Extend data structures.  */
  extend_all (check);
  RECOVERY_BLOCK (check) = rec;

  if (sched_verbose && spec_info->dump)
    fprintf (spec_info->dump, ";;\t\tGenerated check insn : %s\n",
             (*current_sched_info->print_insn) (check, 0));

  gcc_assert (ORIG_PAT (insn));

  /* Initialize TWIN (twin is a duplicate of original instruction
     in the recovery block).  */
  if (rec != EXIT_BLOCK_PTR)
    {
      rtx link;

      for (link = RESOLVED_DEPS (insn); link; link = XEXP (link, 1))    
	if (DEP_STATUS (link) & DEP_OUTPUT)
	  {
	    RESOLVED_DEPS (check) = 
	      alloc_DEPS_LIST (XEXP (link, 0), RESOLVED_DEPS (check), DEP_TRUE);
	    PUT_REG_NOTE_KIND (RESOLVED_DEPS (check), REG_DEP_TRUE);
	  }

      twin = emit_insn_after (ORIG_PAT (insn), BB_END (rec));
      extend_global (twin);

      if (sched_verbose && spec_info->dump)
	/* INSN_BB (insn) isn't determined for twin insns yet.
	   So we can't use current_sched_info->print_insn.  */
	fprintf (spec_info->dump, ";;\t\tGenerated twin insn : %d/rec%d\n",
		 INSN_UID (twin), rec->index);
    }
  else
    {
      ORIG_PAT (check) = ORIG_PAT (insn);
      HAS_INTERNAL_DEP (check) = 1;
      twin = check;
      /* ??? We probably should change all OUTPUT dependencies to
	 (TRUE | OUTPUT).  */
    }

  RESOLVED_DEPS (twin) = copy_DEPS_LIST_list (RESOLVED_DEPS (insn));  

  if (rec != EXIT_BLOCK_PTR)
    /* In case of branchy check, fix CFG.  */
    {
      basic_block first_bb, second_bb;
      rtx jump;
      edge e;
      int edge_flags;

      first_bb = BLOCK_FOR_INSN (check);
      e = split_block (first_bb, check);
      /* split_block emits note if *check == BB_END.  Probably it 
	 is better to rip that note off.  */
      gcc_assert (e->src == first_bb);
      second_bb = e->dest;

      /* This is fixing of incoming edge.  */
      /* ??? Which other flags should be specified?  */      
      if (BB_PARTITION (first_bb) != BB_PARTITION (rec))
	/* Partition type is the same, if it is "unpartitioned".  */
	edge_flags = EDGE_CROSSING;
      else
	edge_flags = 0;
      
      e = make_edge (first_bb, rec, edge_flags);

      add_block (second_bb, first_bb);
      
      gcc_assert (NOTE_INSN_BASIC_BLOCK_P (BB_HEAD (second_bb)));
      label = block_label (second_bb);
      jump = emit_jump_insn_after (gen_jump (label), BB_END (rec));
      JUMP_LABEL (jump) = label;
      LABEL_NUSES (label)++;
      extend_global (jump);

      if (BB_PARTITION (second_bb) != BB_PARTITION (rec))
	/* Partition type is the same, if it is "unpartitioned".  */
	{
	  /* Rewritten from cfgrtl.c.  */
	  if (flag_reorder_blocks_and_partition
	      && targetm.have_named_sections
	      /*&& !any_condjump_p (jump)*/)
	    /* any_condjump_p (jump) == false.
	       We don't need the same note for the check because
	       any_condjump_p (check) == true.  */
	    {
	      REG_NOTES (jump) = gen_rtx_EXPR_LIST (REG_CROSSING_JUMP,
						    NULL_RTX,
						    REG_NOTES (jump));
	    }
	  edge_flags = EDGE_CROSSING;
	}
      else
	edge_flags = 0;  
      
      make_single_succ_edge (rec, second_bb, edge_flags);  
      
      add_block (rec, EXIT_BLOCK_PTR);
    }

  /* Move backward dependences from INSN to CHECK and 
     move forward dependences from INSN to TWIN.  */
  for (link = LOG_LINKS (insn); link; link = XEXP (link, 1))
    {
      ds_t ds;

      /* If BEGIN_DATA: [insn ~~TRUE~~> producer]:
	 check --TRUE--> producer  ??? or ANTI ???
	 twin  --TRUE--> producer
	 twin  --ANTI--> check
	 
	 If BEGIN_CONTROL: [insn ~~ANTI~~> producer]:
	 check --ANTI--> producer
	 twin  --ANTI--> producer
	 twin  --ANTI--> check

	 If BE_IN_SPEC: [insn ~~TRUE~~> producer]:
	 check ~~TRUE~~> producer
	 twin  ~~TRUE~~> producer
	 twin  --ANTI--> check  */	      	  

      ds = DEP_STATUS (link);

      if (ds & BEGIN_SPEC)
	{
	  gcc_assert (!mutate_p);
	  ds &= ~BEGIN_SPEC;
	}

      if (rec != EXIT_BLOCK_PTR)
	{
	  add_back_forw_dep (check, XEXP (link, 0), REG_NOTE_KIND (link), ds);
	  add_back_forw_dep (twin, XEXP (link, 0), REG_NOTE_KIND (link), ds);
	}    
      else
	add_back_forw_dep (check, XEXP (link, 0), REG_NOTE_KIND (link), ds);
    }

  for (link = LOG_LINKS (insn); link;)
    if ((DEP_STATUS (link) & BEGIN_SPEC)
	|| mutate_p)
      /* We can delete this dep only if we totally overcome it with
	 BEGIN_SPECULATION.  */
      {
        delete_back_forw_dep (insn, XEXP (link, 0));
        link = LOG_LINKS (insn);
      }
    else
      link = XEXP (link, 1);    

  fs = 0;

  /* Fields (DONE_SPEC (x) & BEGIN_SPEC) and CHECK_SPEC (x) are set only
     here.  */
  
  gcc_assert (!DONE_SPEC (insn));
  
  if (!mutate_p)
    { 
      ds_t ts = TODO_SPEC (insn);

      DONE_SPEC (insn) = ts & BEGIN_SPEC;
      CHECK_SPEC (check) = ts & BEGIN_SPEC;

      if (ts & BEGIN_DATA)
	fs = set_dep_weak (fs, BE_IN_DATA, get_dep_weak (ts, BEGIN_DATA));
      if (ts & BEGIN_CONTROL)
	fs = set_dep_weak (fs, BE_IN_CONTROL, get_dep_weak (ts, BEGIN_CONTROL));
    }
  else
    CHECK_SPEC (check) = CHECK_SPEC (insn);

  /* Future speculations: call the helper.  */
  process_insn_depend_be_in_spec (INSN_DEPEND (insn), twin, fs);

  if (rec != EXIT_BLOCK_PTR)
    {
      /* Which types of dependencies should we use here is,
	 generally, machine-dependent question...  But, for now,
	 it is not.  */

      if (!mutate_p)
	{
	  add_back_forw_dep (check, insn, REG_DEP_TRUE, DEP_TRUE);
	  add_back_forw_dep (twin, insn, REG_DEP_OUTPUT, DEP_OUTPUT);
	}
      else
	{
	  if (spec_info->dump)    
	    fprintf (spec_info->dump, ";;\t\tRemoved simple check : %s\n",
		     (*current_sched_info->print_insn) (insn, 0));

	  for (link = INSN_DEPEND (insn); link; link = INSN_DEPEND (insn))
	    delete_back_forw_dep (XEXP (link, 0), insn);

	  if (QUEUE_INDEX (insn) != QUEUE_NOWHERE)
	    try_ready (check);

	  sched_remove_insn (insn);
	}

      add_back_forw_dep (twin, check, REG_DEP_ANTI, DEP_ANTI);
    }
  else
    add_back_forw_dep (check, insn, REG_DEP_TRUE, DEP_TRUE | DEP_OUTPUT);

  if (!mutate_p)
    /* Fix priorities.  If MUTATE_P is nonzero, this is not necessary,
       because it'll be done later in add_to_speculative_block.  */
    {
      clear_priorities (twin);
      calc_priorities (twin);
    }
}

/* Removes dependency between instructions in the recovery block REC
   and usual region instructions.  It keeps inner dependences so it
   won't be necessary to recompute them.  */
static void
fix_recovery_deps (basic_block rec)
{
  rtx note, insn, link, jump, ready_list = 0;
  bitmap_head in_ready;

  bitmap_initialize (&in_ready, 0);
  
  /* NOTE - a basic block note.  */
  note = NEXT_INSN (BB_HEAD (rec));
  gcc_assert (NOTE_INSN_BASIC_BLOCK_P (note));
  insn = BB_END (rec);
  gcc_assert (JUMP_P (insn));
  insn = PREV_INSN (insn);

  do
    {    
      for (link = INSN_DEPEND (insn); link;)
	{
	  rtx consumer;

	  consumer = XEXP (link, 0);

	  if (BLOCK_FOR_INSN (consumer) != rec)
	    {
	      delete_back_forw_dep (consumer, insn);

	      if (!bitmap_bit_p (&in_ready, INSN_LUID (consumer)))
		{
		  ready_list = alloc_INSN_LIST (consumer, ready_list);
		  bitmap_set_bit (&in_ready, INSN_LUID (consumer));
		}
	      
	      link = INSN_DEPEND (insn);
	    }
	  else
	    {
	      gcc_assert ((DEP_STATUS (link) & DEP_TYPES) == DEP_TRUE);

	      link = XEXP (link, 1);
	    }
	}
      
      insn = PREV_INSN (insn);
    }
  while (insn != note);

  bitmap_clear (&in_ready);

  /* Try to add instructions to the ready or queue list.  */
  for (link = ready_list; link; link = XEXP (link, 1))
    try_ready (XEXP (link, 0));
  free_INSN_LIST_list (&ready_list);

  /* Fixing jump's dependences.  */
  insn = BB_HEAD (rec);
  jump = BB_END (rec);
      
  gcc_assert (LABEL_P (insn));
  insn = NEXT_INSN (insn);
  
  gcc_assert (NOTE_INSN_BASIC_BLOCK_P (insn));
  add_jump_dependencies (insn, jump);
}

/* The function saves line notes at the beginning of block B.  */
static void
associate_line_notes_with_blocks (basic_block b)
{
  rtx line;

  for (line = BB_HEAD (b); line; line = PREV_INSN (line))
    if (NOTE_P (line) && NOTE_LINE_NUMBER (line) > 0)
      {
        line_note_head[b->index] = line;
        break;
      }
  /* Do a forward search as well, since we won't get to see the first
     notes in a basic block.  */
  for (line = BB_HEAD (b); line; line = NEXT_INSN (line))
    {
      if (INSN_P (line))
        break;
      if (NOTE_P (line) && NOTE_LINE_NUMBER (line) > 0)
        line_note_head[b->index] = line;
    }
}

/* Changes pattern of the INSN to NEW_PAT.  */
static void
change_pattern (rtx insn, rtx new_pat)
{
  int t;

  t = validate_change (insn, &PATTERN (insn), new_pat, 0);
  gcc_assert (t);
  /* Invalidate INSN_COST, so it'll be recalculated.  */
  INSN_COST (insn) = -1;
  /* Invalidate INSN_TICK, so it'll be recalculated.  */
  INSN_TICK (insn) = INVALID_TICK;
  dfa_clear_single_insn_cache (insn);
}


/* -1 - can't speculate,
   0 - for speculation with REQUEST mode it is OK to use
   current instruction pattern,
   1 - need to change pattern for *NEW_PAT to be speculative.  */
static int
speculate_insn (rtx insn, ds_t request, rtx *new_pat)
{
  gcc_assert (current_sched_info->flags & DO_SPECULATION
              && (request & SPECULATIVE));

  if (!NONJUMP_INSN_P (insn)
      || HAS_INTERNAL_DEP (insn)
      || SCHED_GROUP_P (insn)
      || side_effects_p (PATTERN (insn))
      || (request & spec_info->mask) != request)    
    return -1;
  
  gcc_assert (!IS_SPECULATION_CHECK_P (insn));

  if (request & BE_IN_SPEC)
    {            
      if (may_trap_p (PATTERN (insn)))
        return -1;
      
      if (!(request & BEGIN_SPEC))
        return 0;
    }

  return targetm.sched.speculate_insn (insn, request & BEGIN_SPEC, new_pat);
}

/* Print some information about block BB, which starts with HEAD and
   ends with TAIL, before scheduling it.
   I is zero, if scheduler is about to start with the fresh ebb.  */
static void
dump_new_block_header (int i, basic_block bb, rtx head, rtx tail)
{
  if (!i)
    fprintf (sched_dump,
	     ";;   ======================================================\n");
  else
    fprintf (sched_dump,
	     ";;   =====================ADVANCING TO=====================\n");
  fprintf (sched_dump,
	   ";;   -- basic block %d from %d to %d -- %s reload\n",
	   bb->index, INSN_UID (head), INSN_UID (tail),
	   (reload_completed ? "after" : "before"));
  fprintf (sched_dump,
	   ";;   ======================================================\n");
  fprintf (sched_dump, "\n");
}

/* Unlink basic block notes and labels and saves them, so they
   can be easily restored.  We unlink basic block notes in EBB to
   provide back-compatibility with the previous code, as target backends
   assume, that there'll be only instructions between
   current_sched_info->{head and tail}.  We restore these notes as soon
   as we can.
   FIRST (LAST) is the first (last) basic block in the ebb.
   NB: In usual case (FIRST == LAST) nothing is really done.  */
void
unlink_bb_notes (basic_block first, basic_block last)
{
  /* We DON'T unlink basic block notes of the first block in the ebb.  */
  if (first == last)
    return;

  bb_header = xmalloc (last_basic_block * sizeof (*bb_header));

  /* Make a sentinel.  */
  if (last->next_bb != EXIT_BLOCK_PTR)
    bb_header[last->next_bb->index] = 0;

  first = first->next_bb;
  do
    {
      rtx prev, label, note, next;

      label = BB_HEAD (last);
      if (LABEL_P (label))
	note = NEXT_INSN (label);
      else
	note = label;      
      gcc_assert (NOTE_INSN_BASIC_BLOCK_P (note));

      prev = PREV_INSN (label);
      next = NEXT_INSN (note);
      gcc_assert (prev && next);

      NEXT_INSN (prev) = next;
      PREV_INSN (next) = prev;

      bb_header[last->index] = label;

      if (last == first)
	break;
      
      last = last->prev_bb;
    }
  while (1);
}

/* Restore basic block notes.
   FIRST is the first basic block in the ebb.  */
static void
restore_bb_notes (basic_block first)
{
  if (!bb_header)
    return;

  /* We DON'T unlink basic block notes of the first block in the ebb.  */
  first = first->next_bb;  
  /* Remember: FIRST is actually a second basic block in the ebb.  */

  while (first != EXIT_BLOCK_PTR
	 && bb_header[first->index])
    {
      rtx prev, label, note, next;
      
      label = bb_header[first->index];
      prev = PREV_INSN (label);
      next = NEXT_INSN (prev);

      if (LABEL_P (label))
	note = NEXT_INSN (label);
      else
	note = label;      
      gcc_assert (NOTE_INSN_BASIC_BLOCK_P (note));

      bb_header[first->index] = 0;

      NEXT_INSN (prev) = label;
      NEXT_INSN (note) = next;
      PREV_INSN (next) = note;
      
      first = first->next_bb;
    }

  free (bb_header);
  bb_header = 0;
}

/* Extend per basic block data structures of the scheduler.
   If BB is NULL, initialize structures for the whole CFG.
   Otherwise, initialize them for the just created BB.  */
static void
extend_bb (basic_block bb)
{
  rtx insn;

  if (write_symbols != NO_DEBUG)
    {
      /* Save-line-note-head:
         Determine the line-number at the start of each basic block.
         This must be computed and saved now, because after a basic block's
         predecessor has been scheduled, it is impossible to accurately
         determine the correct line number for the first insn of the block.  */
      line_note_head = xrecalloc (line_note_head, last_basic_block, 
				  old_last_basic_block,
				  sizeof (*line_note_head));

      if (bb)
	associate_line_notes_with_blocks (bb);
      else
	FOR_EACH_BB (bb)
	  associate_line_notes_with_blocks (bb);
    }        
  
  old_last_basic_block = last_basic_block;

  if (current_sched_info->flags & USE_GLAT)
    {
      glat_start = xrealloc (glat_start,
                             last_basic_block * sizeof (*glat_start));
      glat_end = xrealloc (glat_end, last_basic_block * sizeof (*glat_end));
    }

  /* The following is done to keep current_sched_info->next_tail non null.  */

  insn = BB_END (EXIT_BLOCK_PTR->prev_bb);
  if (NEXT_INSN (insn) == 0
      || (!NOTE_P (insn)
	  && !LABEL_P (insn)
	  /* Don't emit a NOTE if it would end up before a BARRIER.  */
	  && !BARRIER_P (NEXT_INSN (insn))))
    {
      emit_note_after (NOTE_INSN_DELETED, insn);
      /* Make insn to appear outside BB.  */
      BB_END (EXIT_BLOCK_PTR->prev_bb) = insn;
    }
}

/* Add a basic block BB to extended basic block EBB.
   If EBB is EXIT_BLOCK_PTR, then BB is recovery block.
   If EBB is NULL, then BB should be a new region.  */
void
add_block (basic_block bb, basic_block ebb)
{
  gcc_assert (current_sched_info->flags & DETACH_LIFE_INFO
	      && bb->il.rtl->global_live_at_start == 0
	      && bb->il.rtl->global_live_at_end == 0);

  extend_bb (bb);

  glat_start[bb->index] = 0;
  glat_end[bb->index] = 0;

  if (current_sched_info->add_block)
    /* This changes only data structures of the front-end.  */
    current_sched_info->add_block (bb, ebb);
}

/* Helper function.
   Fix CFG after both in- and inter-block movement of
   control_flow_insn_p JUMP.  */
static void
fix_jump_move (rtx jump)
{
  basic_block bb, jump_bb, jump_bb_next;

  bb = BLOCK_FOR_INSN (PREV_INSN (jump));
  jump_bb = BLOCK_FOR_INSN (jump);
  jump_bb_next = jump_bb->next_bb;

  gcc_assert (current_sched_info->flags & SCHED_EBB
	      || IS_SPECULATION_BRANCHY_CHECK_P (jump));
  
  if (!NOTE_INSN_BASIC_BLOCK_P (BB_END (jump_bb_next)))
    /* if jump_bb_next is not empty.  */
    BB_END (jump_bb) = BB_END (jump_bb_next);

  if (BB_END (bb) != PREV_INSN (jump))
    /* Then there are instruction after jump that should be placed
       to jump_bb_next.  */
    BB_END (jump_bb_next) = BB_END (bb);
  else
    /* Otherwise jump_bb_next is empty.  */
    BB_END (jump_bb_next) = NEXT_INSN (BB_HEAD (jump_bb_next));

  /* To make assertion in move_insn happy.  */
  BB_END (bb) = PREV_INSN (jump);

  update_bb_for_insn (jump_bb_next);
}

/* Fix CFG after interblock movement of control_flow_insn_p JUMP.  */
static void
move_block_after_check (rtx jump)
{
  basic_block bb, jump_bb, jump_bb_next;
  VEC(edge,gc) *t;

  bb = BLOCK_FOR_INSN (PREV_INSN (jump));
  jump_bb = BLOCK_FOR_INSN (jump);
  jump_bb_next = jump_bb->next_bb;
  
  update_bb_for_insn (jump_bb);
  
  gcc_assert (IS_SPECULATION_CHECK_P (jump)
	      || IS_SPECULATION_CHECK_P (BB_END (jump_bb_next)));

  unlink_block (jump_bb_next);
  link_block (jump_bb_next, bb);

  t = bb->succs;
  bb->succs = 0;
  move_succs (&(jump_bb->succs), bb);
  move_succs (&(jump_bb_next->succs), jump_bb);
  move_succs (&t, jump_bb_next);
  
  if (current_sched_info->fix_recovery_cfg)
    current_sched_info->fix_recovery_cfg 
      (bb->index, jump_bb->index, jump_bb_next->index);
}

/* Helper function for move_block_after_check.
   This functions attaches edge vector pointed to by SUCCSP to
   block TO.  */
static void
move_succs (VEC(edge,gc) **succsp, basic_block to)
{
  edge e;
  edge_iterator ei;

  gcc_assert (to->succs == 0);

  to->succs = *succsp;

  FOR_EACH_EDGE (e, ei, to->succs)
    e->src = to;

  *succsp = 0;
}

/* Initialize GLAT (global_live_at_{start, end}) structures.
   GLAT structures are used to substitute global_live_{start, end}
   regsets during scheduling.  This is necessary to use such functions as
   split_block (), as they assume consistency of register live information.  */
static void
init_glat (void)
{
  basic_block bb;

  FOR_ALL_BB (bb)
    init_glat1 (bb);
}

/* Helper function for init_glat.  */
static void
init_glat1 (basic_block bb)
{
  gcc_assert (bb->il.rtl->global_live_at_start != 0
	      && bb->il.rtl->global_live_at_end != 0);

  glat_start[bb->index] = bb->il.rtl->global_live_at_start;
  glat_end[bb->index] = bb->il.rtl->global_live_at_end;
  
  if (current_sched_info->flags & DETACH_LIFE_INFO)
    {
      bb->il.rtl->global_live_at_start = 0;
      bb->il.rtl->global_live_at_end = 0;
    }
}

/* Attach reg_live_info back to basic blocks.
   Also save regsets, that should not have been changed during scheduling,
   for checking purposes (see check_reg_live).  */
void
attach_life_info (void)
{
  basic_block bb;

  FOR_ALL_BB (bb)
    attach_life_info1 (bb);
}

/* Helper function for attach_life_info.  */
static void
attach_life_info1 (basic_block bb)
{
  gcc_assert (bb->il.rtl->global_live_at_start == 0
	      && bb->il.rtl->global_live_at_end == 0);

  if (glat_start[bb->index])
    {
      gcc_assert (glat_end[bb->index]);    

      bb->il.rtl->global_live_at_start = glat_start[bb->index];
      bb->il.rtl->global_live_at_end = glat_end[bb->index];

      /* Make them NULL, so they won't be freed in free_glat.  */
      glat_start[bb->index] = 0;
      glat_end[bb->index] = 0;

#ifdef ENABLE_CHECKING
      if (bb->index < NUM_FIXED_BLOCKS
	  || current_sched_info->region_head_or_leaf_p (bb, 0))
	{
	  glat_start[bb->index] = ALLOC_REG_SET (&reg_obstack);
	  COPY_REG_SET (glat_start[bb->index],
			bb->il.rtl->global_live_at_start);
	}

      if (bb->index < NUM_FIXED_BLOCKS
	  || current_sched_info->region_head_or_leaf_p (bb, 1))
	{       
	  glat_end[bb->index] = ALLOC_REG_SET (&reg_obstack);
	  COPY_REG_SET (glat_end[bb->index], bb->il.rtl->global_live_at_end);
	}
#endif
    }
  else
    {
      gcc_assert (!glat_end[bb->index]);

      bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
      bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
    }
}

/* Free GLAT information.  */
static void
free_glat (void)
{
#ifdef ENABLE_CHECKING
  if (current_sched_info->flags & DETACH_LIFE_INFO)
    {
      basic_block bb;

      FOR_ALL_BB (bb)
	{
	  if (glat_start[bb->index])
	    FREE_REG_SET (glat_start[bb->index]);
	  if (glat_end[bb->index])
	    FREE_REG_SET (glat_end[bb->index]);
	}
    }
#endif

  free (glat_start);
  free (glat_end);
}

/* Remove INSN from the instruction stream.
   INSN should have any dependencies.  */
static void
sched_remove_insn (rtx insn)
{
  change_queue_index (insn, QUEUE_NOWHERE);
  current_sched_info->add_remove_insn (insn, 1);
  remove_insn (insn);
}

/* Clear priorities of all instructions, that are
   forward dependent on INSN.  */
static void
clear_priorities (rtx insn)
{
  rtx link;

  for (link = LOG_LINKS (insn); link; link = XEXP (link, 1))
    {
      rtx pro;

      pro = XEXP (link, 0);
      if (INSN_PRIORITY_KNOWN (pro))
	{
	  INSN_PRIORITY_KNOWN (pro) = 0;
	  clear_priorities (pro);
	}
    }
}

/* Recompute priorities of instructions, whose priorities might have been
   changed due to changes in INSN.  */
static void
calc_priorities (rtx insn)
{
  rtx link;

  for (link = LOG_LINKS (insn); link; link = XEXP (link, 1))
    {
      rtx pro;

      pro = XEXP (link, 0);
      if (!INSN_PRIORITY_KNOWN (pro))
	{
	  priority (pro);
	  calc_priorities (pro);
	}
    }
}


/* Add dependences between JUMP and other instructions in the recovery
   block.  INSN is the first insn the recovery block.  */
static void
add_jump_dependencies (rtx insn, rtx jump)
{
  do
    {
      insn = NEXT_INSN (insn);
      if (insn == jump)
	break;
      
      if (!INSN_DEPEND (insn))	    
	add_back_forw_dep (jump, insn, REG_DEP_ANTI, DEP_ANTI);
    }
  while (1);
  gcc_assert (LOG_LINKS (jump));
}

/* Return the NOTE_INSN_BASIC_BLOCK of BB.  */
rtx
bb_note (basic_block bb)
{
  rtx note;

  note = BB_HEAD (bb);
  if (LABEL_P (note))
    note = NEXT_INSN (note);

  gcc_assert (NOTE_INSN_BASIC_BLOCK_P (note));
  return note;
}

#ifdef ENABLE_CHECKING
extern void debug_spec_status (ds_t);

/* Dump information about the dependence status S.  */
void
debug_spec_status (ds_t s)
{
  FILE *f = stderr;

  if (s & BEGIN_DATA)
    fprintf (f, "BEGIN_DATA: %d; ", get_dep_weak (s, BEGIN_DATA));
  if (s & BE_IN_DATA)
    fprintf (f, "BE_IN_DATA: %d; ", get_dep_weak (s, BE_IN_DATA));
  if (s & BEGIN_CONTROL)
    fprintf (f, "BEGIN_CONTROL: %d; ", get_dep_weak (s, BEGIN_CONTROL));
  if (s & BE_IN_CONTROL)
    fprintf (f, "BE_IN_CONTROL: %d; ", get_dep_weak (s, BE_IN_CONTROL));

  if (s & HARD_DEP)
    fprintf (f, "HARD_DEP; ");

  if (s & DEP_TRUE)
    fprintf (f, "DEP_TRUE; ");
  if (s & DEP_ANTI)
    fprintf (f, "DEP_ANTI; ");
  if (s & DEP_OUTPUT)
    fprintf (f, "DEP_OUTPUT; ");

  fprintf (f, "\n");
}

/* Helper function for check_cfg.
   Return nonzero, if edge vector pointed to by EL has edge with TYPE in
   its flags.  */
static int
has_edge_p (VEC(edge,gc) *el, int type)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, el)
    if (e->flags & type)
      return 1;
  return 0;
}

/* Check few properties of CFG between HEAD and TAIL.
   If HEAD (TAIL) is NULL check from the beginning (till the end) of the
   instruction stream.  */
static void
check_cfg (rtx head, rtx tail)
{
  rtx next_tail;
  basic_block bb = 0;
  int not_first = 0, not_last;

  if (head == NULL)
    head = get_insns ();
  if (tail == NULL)
    tail = get_last_insn ();
  next_tail = NEXT_INSN (tail);

  do
    {      
      not_last = head != tail;        

      if (not_first)
	gcc_assert (NEXT_INSN (PREV_INSN (head)) == head);
      if (not_last)
	gcc_assert (PREV_INSN (NEXT_INSN (head)) == head);

      if (LABEL_P (head) 
	  || (NOTE_INSN_BASIC_BLOCK_P (head)
	      && (!not_first
		  || (not_first && !LABEL_P (PREV_INSN (head))))))
	{
	  gcc_assert (bb == 0);	  
	  bb = BLOCK_FOR_INSN (head);
	  if (bb != 0)
	    gcc_assert (BB_HEAD (bb) == head);      
	  else
	    /* This is the case of jump table.  See inside_basic_block_p ().  */
	    gcc_assert (LABEL_P (head) && !inside_basic_block_p (head));
	}

      if (bb == 0)
	{
	  gcc_assert (!inside_basic_block_p (head));
	  head = NEXT_INSN (head);
	}
      else
	{
	  gcc_assert (inside_basic_block_p (head)
		      || NOTE_P (head));
	  gcc_assert (BLOCK_FOR_INSN (head) == bb);
	
	  if (LABEL_P (head))
	    {
	      head = NEXT_INSN (head);
	      gcc_assert (NOTE_INSN_BASIC_BLOCK_P (head));
	    }
	  else
	    {
	      if (control_flow_insn_p (head))
		{
		  gcc_assert (BB_END (bb) == head);
		  
		  if (any_uncondjump_p (head))
		    gcc_assert (EDGE_COUNT (bb->succs) == 1
				&& BARRIER_P (NEXT_INSN (head)));
		  else if (any_condjump_p (head))
		    gcc_assert (/* Usual case.  */
                                (EDGE_COUNT (bb->succs) > 1
                                 && !BARRIER_P (NEXT_INSN (head)))
                                /* Or jump to the next instruction.  */
                                || (EDGE_COUNT (bb->succs) == 1
                                    && (BB_HEAD (EDGE_I (bb->succs, 0)->dest)
                                        == JUMP_LABEL (head))));
		}
	      if (BB_END (bb) == head)
		{
		  if (EDGE_COUNT (bb->succs) > 1)
		    gcc_assert (control_flow_insn_p (head)
				|| has_edge_p (bb->succs, EDGE_COMPLEX));
		  bb = 0;
		}
			      
	      head = NEXT_INSN (head);
	    }
	}

      not_first = 1;
    }
  while (head != next_tail);

  gcc_assert (bb == 0);
}

/* Perform a few consistency checks of flags in different data structures.  */
static void
check_sched_flags (void)
{
  unsigned int f = current_sched_info->flags;

  if (flag_sched_stalled_insns)
    gcc_assert (!(f & DO_SPECULATION));
  if (f & DO_SPECULATION)
    gcc_assert (!flag_sched_stalled_insns
		&& (f & DETACH_LIFE_INFO)
		&& spec_info
		&& spec_info->mask);
  if (f & DETACH_LIFE_INFO)
    gcc_assert (f & USE_GLAT);
}

/* Check global_live_at_{start, end} regsets.
   If FATAL_P is TRUE, then abort execution at the first failure.
   Otherwise, print diagnostics to STDERR (this mode is for calling
   from debugger).  */
void
check_reg_live (bool fatal_p)
{
  basic_block bb;

  FOR_ALL_BB (bb)
    {
      int i;

      i = bb->index;

      if (glat_start[i])
	{
	  bool b = bitmap_equal_p (bb->il.rtl->global_live_at_start,
				   glat_start[i]);

	  if (!b)
	    {
	      gcc_assert (!fatal_p);

	      fprintf (stderr, ";; check_reg_live_at_start (%d) failed.\n", i);
	    }
	}

      if (glat_end[i])
	{
	  bool b = bitmap_equal_p (bb->il.rtl->global_live_at_end,
				   glat_end[i]);

	  if (!b)
	    {
	      gcc_assert (!fatal_p);

	      fprintf (stderr, ";; check_reg_live_at_end (%d) failed.\n", i);
	    }
	}
    }
}
#endif /* ENABLE_CHECKING */

#endif /* INSN_SCHEDULING */
