/* Generate code from machine description to recognize rtl as insns.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */


/* This program is used to produce insn-recog.c, which contains a
   function called `recog' plus its subroutines.  These functions
   contain a decision tree that recognizes whether an rtx, the
   argument given to recog, is a valid instruction.

   recog returns -1 if the rtx is not valid.  If the rtx is valid,
   recog returns a nonnegative number which is the insn code number
   for the pattern that matched.  This is the same as the order in the
   machine description of the entry that matched.  This number can be
   used as an index into various insn_* tables, such as insn_template,
   insn_outfun, and insn_n_operands (found in insn-output.c).

   The third argument to recog is an optional pointer to an int.  If
   present, recog will accept a pattern if it matches except for
   missing CLOBBER expressions at the end.  In that case, the value
   pointed to by the optional pointer will be set to the number of
   CLOBBERs that need to be added (it should be initialized to zero by
   the caller).  If it is set nonzero, the caller should allocate a
   PARALLEL of the appropriate size, copy the initial entries, and
   call add_clobbers (found in insn-emit.c) to fill in the CLOBBERs.

   This program also generates the function `split_insns', which
   returns 0 if the rtl could not be split, or it returns the split
   rtl as an INSN list.

   This program also generates the function `peephole2_insns', which
   returns 0 if the rtl could not be matched.  If there was a match,
   the new rtl is returned in an INSN list, and LAST_INSN will point
   to the last recognized insn in the old sequence.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "gensupport.h"

#define OUTPUT_LABEL(INDENT_STRING, LABEL_NUMBER) \
  printf("%sL%d: ATTRIBUTE_UNUSED_LABEL\n", (INDENT_STRING), (LABEL_NUMBER))

/* A listhead of decision trees.  The alternatives to a node are kept
   in a doubly-linked list so we can easily add nodes to the proper
   place when merging.  */

struct decision_head
{
  struct decision *first;
  struct decision *last;
};

/* A single test.  The two accept types aren't tests per-se, but
   their equality (or lack thereof) does affect tree merging so
   it is convenient to keep them here.  */

struct decision_test
{
  /* A linked list through the tests attached to a node.  */
  struct decision_test *next;

  /* These types are roughly in the order in which we'd like to test them.  */
  enum decision_type
    {
      DT_num_insns,
      DT_mode, DT_code, DT_veclen,
      DT_elt_zero_int, DT_elt_one_int, DT_elt_zero_wide, DT_elt_zero_wide_safe,
      DT_const_int,
      DT_veclen_ge, DT_dup, DT_pred, DT_c_test,
      DT_accept_op, DT_accept_insn
    } type;

  union
  {
    int num_insns;		/* Number if insn in a define_peephole2.  */
    enum machine_mode mode;	/* Machine mode of node.  */
    RTX_CODE code;		/* Code to test.  */

    struct
    {
      const char *name;		/* Predicate to call.  */
      const struct pred_data *data;
                                /* Optimization hints for this predicate.  */
      enum machine_mode mode;	/* Machine mode for node.  */
    } pred;

    const char *c_test;		/* Additional test to perform.  */
    int veclen;			/* Length of vector.  */
    int dup;			/* Number of operand to compare against.  */
    HOST_WIDE_INT intval;	/* Value for XINT for XWINT.  */
    int opno;			/* Operand number matched.  */

    struct {
      int code_number;		/* Insn number matched.  */
      int lineno;		/* Line number of the insn.  */
      int num_clobbers_to_add;	/* Number of CLOBBERs to be added.  */
    } insn;
  } u;
};

/* Data structure for decision tree for recognizing legitimate insns.  */

struct decision
{
  struct decision_head success;	/* Nodes to test on success.  */
  struct decision *next;	/* Node to test on failure.  */
  struct decision *prev;	/* Node whose failure tests us.  */
  struct decision *afterward;	/* Node to test on success,
				   but failure of successor nodes.  */

  const char *position;		/* String denoting position in pattern.  */

  struct decision_test *tests;	/* The tests for this node.  */

  int number;			/* Node number, used for labels */
  int subroutine_number;	/* Number of subroutine this node starts */
  int need_label;		/* Label needs to be output.  */
};

#define SUBROUTINE_THRESHOLD	100

static int next_subroutine_number;

/* We can write three types of subroutines: One for insn recognition,
   one to split insns, and one for peephole-type optimizations.  This
   defines which type is being written.  */

enum routine_type {
  RECOG, SPLIT, PEEPHOLE2
};

#define IS_SPLIT(X) ((X) != RECOG)

/* Next available node number for tree nodes.  */

static int next_number;

/* Next number to use as an insn_code.  */

static int next_insn_code;

/* Record the highest depth we ever have so we know how many variables to
   allocate in each subroutine we make.  */

static int max_depth;

/* The line number of the start of the pattern currently being processed.  */
static int pattern_lineno;

/* Count of errors.  */
static int error_count;

/* Predicate handling. 

   We construct from the machine description a table mapping each
   predicate to a list of the rtl codes it can possibly match.  The
   function 'maybe_both_true' uses it to deduce that there are no
   expressions that can be matches by certain pairs of tree nodes.
   Also, if a predicate can match only one code, we can hardwire that
   code into the node testing the predicate.

   Some predicates are flagged as special.  validate_pattern will not
   warn about modeless match_operand expressions if they have a
   special predicate.  Predicates that allow only constants are also
   treated as special, for this purpose.

   validate_pattern will warn about predicates that allow non-lvalues
   when they appear in destination operands.

   Calculating the set of rtx codes that can possibly be accepted by a
   predicate expression EXP requires a three-state logic: any given
   subexpression may definitively accept a code C (Y), definitively
   reject a code C (N), or may have an indeterminate effect (I).  N
   and I is N; Y or I is Y; Y and I, N or I are both I.  Here are full
   truth tables.

     a b  a&b  a|b
     Y Y   Y    Y
     N Y   N    Y
     N N   N    N
     I Y   I    Y
     I N   N    I
     I I   I    I

   We represent Y with 1, N with 0, I with 2.  If any code is left in
   an I state by the complete expression, we must assume that that
   code can be accepted.  */

#define N 0
#define Y 1
#define I 2

#define TRISTATE_AND(a,b)			\
  ((a) == I ? ((b) == N ? N : I) :		\
   (b) == I ? ((a) == N ? N : I) :		\
   (a) && (b))

#define TRISTATE_OR(a,b)			\
  ((a) == I ? ((b) == Y ? Y : I) :		\
   (b) == I ? ((a) == Y ? Y : I) :		\
   (a) || (b))

#define TRISTATE_NOT(a)				\
  ((a) == I ? I : !(a))

/* 0 means no warning about that code yet, 1 means warned.  */
static char did_you_mean_codes[NUM_RTX_CODE];

/* Recursively calculate the set of rtx codes accepted by the
   predicate expression EXP, writing the result to CODES.  */
static void
compute_predicate_codes (rtx exp, char codes[NUM_RTX_CODE])
{
  char op0_codes[NUM_RTX_CODE];
  char op1_codes[NUM_RTX_CODE];
  char op2_codes[NUM_RTX_CODE];
  int i;

  switch (GET_CODE (exp))
    {
    case AND:
      compute_predicate_codes (XEXP (exp, 0), op0_codes);
      compute_predicate_codes (XEXP (exp, 1), op1_codes);
      for (i = 0; i < NUM_RTX_CODE; i++)
	codes[i] = TRISTATE_AND (op0_codes[i], op1_codes[i]);
      break;

    case IOR:
      compute_predicate_codes (XEXP (exp, 0), op0_codes);
      compute_predicate_codes (XEXP (exp, 1), op1_codes);
      for (i = 0; i < NUM_RTX_CODE; i++)
	codes[i] = TRISTATE_OR (op0_codes[i], op1_codes[i]);
      break;
    case NOT:
      compute_predicate_codes (XEXP (exp, 0), op0_codes);
      for (i = 0; i < NUM_RTX_CODE; i++)
	codes[i] = TRISTATE_NOT (op0_codes[i]);
      break;

    case IF_THEN_ELSE:
      /* a ? b : c  accepts the same codes as (a & b) | (!a & c).  */ 
      compute_predicate_codes (XEXP (exp, 0), op0_codes);
      compute_predicate_codes (XEXP (exp, 1), op1_codes);
      compute_predicate_codes (XEXP (exp, 2), op2_codes);
      for (i = 0; i < NUM_RTX_CODE; i++)
	codes[i] = TRISTATE_OR (TRISTATE_AND (op0_codes[i], op1_codes[i]),
				TRISTATE_AND (TRISTATE_NOT (op0_codes[i]),
					      op2_codes[i]));
      break;

    case MATCH_CODE:
      /* MATCH_CODE allows a specified list of codes.  However, if it
	 does not apply to the top level of the expression, it does not
	 constrain the set of codes for the top level.  */
      if (XSTR (exp, 1)[0] != '\0')
	{
	  memset (codes, Y, NUM_RTX_CODE);
	  break;
	}

      memset (codes, N, NUM_RTX_CODE);
      {
	const char *next_code = XSTR (exp, 0);
	const char *code;

	if (*next_code == '\0')
	  {
	    message_with_line (pattern_lineno, "empty match_code expression");
	    error_count++;
	    break;
	  }

	while ((code = scan_comma_elt (&next_code)) != 0)
	  {
	    size_t n = next_code - code;
	    int found_it = 0;
	    
	    for (i = 0; i < NUM_RTX_CODE; i++)
	      if (!strncmp (code, GET_RTX_NAME (i), n)
		  && GET_RTX_NAME (i)[n] == '\0')
		{
		  codes[i] = Y;
		  found_it = 1;
		  break;
		}
	    if (!found_it)
	      {
		message_with_line (pattern_lineno, "match_code \"%.*s\" matches nothing",
				   (int) n, code);
		error_count ++;
		for (i = 0; i < NUM_RTX_CODE; i++)
		  if (!strncasecmp (code, GET_RTX_NAME (i), n)
		      && GET_RTX_NAME (i)[n] == '\0'
		      && !did_you_mean_codes[i])
		    {
		      did_you_mean_codes[i] = 1;
		      message_with_line (pattern_lineno, "(did you mean \"%s\"?)", GET_RTX_NAME (i));
		    }
	      }

	  }
      }
      break;

    case MATCH_OPERAND:
      /* MATCH_OPERAND disallows the set of codes that the named predicate
	 disallows, and is indeterminate for the codes that it does allow.  */
      {
	struct pred_data *p = lookup_predicate (XSTR (exp, 1));
	if (!p)
	  {
	    message_with_line (pattern_lineno,
			       "reference to unknown predicate '%s'",
			       XSTR (exp, 1));
	    error_count++;
	    break;
	  }
	for (i = 0; i < NUM_RTX_CODE; i++)
	  codes[i] = p->codes[i] ? I : N;
      }
      break;


    case MATCH_TEST:
      /* (match_test WHATEVER) is completely indeterminate.  */
      memset (codes, I, NUM_RTX_CODE);
      break;

    default:
      message_with_line (pattern_lineno,
	 "'%s' cannot be used in a define_predicate expression",
	 GET_RTX_NAME (GET_CODE (exp)));
      error_count++;
      memset (codes, I, NUM_RTX_CODE);
      break;
    }
}

#undef TRISTATE_OR
#undef TRISTATE_AND
#undef TRISTATE_NOT

/* Process a define_predicate expression: compute the set of predicates
   that can be matched, and record this as a known predicate.  */
static void
process_define_predicate (rtx desc)
{
  struct pred_data *pred = xcalloc (sizeof (struct pred_data), 1);
  char codes[NUM_RTX_CODE];
  bool seen_one = false;
  int i;

  pred->name = XSTR (desc, 0);
  if (GET_CODE (desc) == DEFINE_SPECIAL_PREDICATE)
    pred->special = 1;

  compute_predicate_codes (XEXP (desc, 1), codes);

  for (i = 0; i < NUM_RTX_CODE; i++)
    if (codes[i] != N)
      {
	pred->codes[i] = true;
	if (GET_RTX_CLASS (i) != RTX_CONST_OBJ)
	  pred->allows_non_const = true;
	if (i != REG
	    && i != SUBREG
	    && i != MEM
	    && i != CONCAT
	    && i != PARALLEL
	    && i != STRICT_LOW_PART)
	  pred->allows_non_lvalue = true;

	if (seen_one)
	  pred->singleton = UNKNOWN;
	else
	  {
	    pred->singleton = i;
	    seen_one = true;
	  }
      }
  add_predicate (pred);
}
#undef I
#undef N
#undef Y


static struct decision *new_decision
  (const char *, struct decision_head *);
static struct decision_test *new_decision_test
  (enum decision_type, struct decision_test ***);
static rtx find_operand
  (rtx, int, rtx);
static rtx find_matching_operand
  (rtx, int);
static void validate_pattern
  (rtx, rtx, rtx, int);
static struct decision *add_to_sequence
  (rtx, struct decision_head *, const char *, enum routine_type, int);

static int maybe_both_true_2
  (struct decision_test *, struct decision_test *);
static int maybe_both_true_1
  (struct decision_test *, struct decision_test *);
static int maybe_both_true
  (struct decision *, struct decision *, int);

static int nodes_identical_1
  (struct decision_test *, struct decision_test *);
static int nodes_identical
  (struct decision *, struct decision *);
static void merge_accept_insn
  (struct decision *, struct decision *);
static void merge_trees
  (struct decision_head *, struct decision_head *);

static void factor_tests
  (struct decision_head *);
static void simplify_tests
  (struct decision_head *);
static int break_out_subroutines
  (struct decision_head *, int);
static void find_afterward
  (struct decision_head *, struct decision *);

static void change_state
  (const char *, const char *, const char *);
static void print_code
  (enum rtx_code);
static void write_afterward
  (struct decision *, struct decision *, const char *);
static struct decision *write_switch
  (struct decision *, int);
static void write_cond
  (struct decision_test *, int, enum routine_type);
static void write_action
  (struct decision *, struct decision_test *, int, int,
   struct decision *, enum routine_type);
static int is_unconditional
  (struct decision_test *, enum routine_type);
static int write_node
  (struct decision *, int, enum routine_type);
static void write_tree_1
  (struct decision_head *, int, enum routine_type);
static void write_tree
  (struct decision_head *, const char *, enum routine_type, int);
static void write_subroutine
  (struct decision_head *, enum routine_type);
static void write_subroutines
  (struct decision_head *, enum routine_type);
static void write_header
  (void);

static struct decision_head make_insn_sequence
  (rtx, enum routine_type);
static void process_tree
  (struct decision_head *, enum routine_type);

static void debug_decision_0
  (struct decision *, int, int);
static void debug_decision_1
  (struct decision *, int);
static void debug_decision_2
  (struct decision_test *);
extern void debug_decision
  (struct decision *);
extern void debug_decision_list
  (struct decision *);

/* Create a new node in sequence after LAST.  */

static struct decision *
new_decision (const char *position, struct decision_head *last)
{
  struct decision *new = xcalloc (1, sizeof (struct decision));

  new->success = *last;
  new->position = xstrdup (position);
  new->number = next_number++;

  last->first = last->last = new;
  return new;
}

/* Create a new test and link it in at PLACE.  */

static struct decision_test *
new_decision_test (enum decision_type type, struct decision_test ***pplace)
{
  struct decision_test **place = *pplace;
  struct decision_test *test;

  test = XNEW (struct decision_test);
  test->next = *place;
  test->type = type;
  *place = test;

  place = &test->next;
  *pplace = place;

  return test;
}

/* Search for and return operand N, stop when reaching node STOP.  */

static rtx
find_operand (rtx pattern, int n, rtx stop)
{
  const char *fmt;
  RTX_CODE code;
  int i, j, len;
  rtx r;

  if (pattern == stop)
    return stop;

  code = GET_CODE (pattern);
  if ((code == MATCH_SCRATCH
       || code == MATCH_OPERAND
       || code == MATCH_OPERATOR
       || code == MATCH_PARALLEL)
      && XINT (pattern, 0) == n)
    return pattern;

  fmt = GET_RTX_FORMAT (code);
  len = GET_RTX_LENGTH (code);
  for (i = 0; i < len; i++)
    {
      switch (fmt[i])
	{
	case 'e': case 'u':
	  if ((r = find_operand (XEXP (pattern, i), n, stop)) != NULL_RTX)
	    return r;
	  break;

	case 'V':
	  if (! XVEC (pattern, i))
	    break;
	  /* Fall through.  */

	case 'E':
	  for (j = 0; j < XVECLEN (pattern, i); j++)
	    if ((r = find_operand (XVECEXP (pattern, i, j), n, stop))
		!= NULL_RTX)
	      return r;
	  break;

	case 'i': case 'w': case '0': case 's':
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  return NULL;
}

/* Search for and return operand M, such that it has a matching
   constraint for operand N.  */

static rtx
find_matching_operand (rtx pattern, int n)
{
  const char *fmt;
  RTX_CODE code;
  int i, j, len;
  rtx r;

  code = GET_CODE (pattern);
  if (code == MATCH_OPERAND
      && (XSTR (pattern, 2)[0] == '0' + n
	  || (XSTR (pattern, 2)[0] == '%'
	      && XSTR (pattern, 2)[1] == '0' + n)))
    return pattern;

  fmt = GET_RTX_FORMAT (code);
  len = GET_RTX_LENGTH (code);
  for (i = 0; i < len; i++)
    {
      switch (fmt[i])
	{
	case 'e': case 'u':
	  if ((r = find_matching_operand (XEXP (pattern, i), n)))
	    return r;
	  break;

	case 'V':
	  if (! XVEC (pattern, i))
	    break;
	  /* Fall through.  */

	case 'E':
	  for (j = 0; j < XVECLEN (pattern, i); j++)
	    if ((r = find_matching_operand (XVECEXP (pattern, i, j), n)))
	      return r;
	  break;

	case 'i': case 'w': case '0': case 's':
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  return NULL;
}


/* Check for various errors in patterns.  SET is nonnull for a destination,
   and is the complete set pattern.  SET_CODE is '=' for normal sets, and
   '+' within a context that requires in-out constraints.  */

static void
validate_pattern (rtx pattern, rtx insn, rtx set, int set_code)
{
  const char *fmt;
  RTX_CODE code;
  size_t i, len;
  int j;

  code = GET_CODE (pattern);
  switch (code)
    {
    case MATCH_SCRATCH:
      return;
    case MATCH_DUP:
    case MATCH_OP_DUP:
    case MATCH_PAR_DUP:
      if (find_operand (insn, XINT (pattern, 0), pattern) == pattern)
	{
	  message_with_line (pattern_lineno,
			     "operand %i duplicated before defined",
			     XINT (pattern, 0));
          error_count++;
	}
      break;
    case MATCH_OPERAND:
    case MATCH_OPERATOR:
      {
	const char *pred_name = XSTR (pattern, 1);
	const struct pred_data *pred;
	const char *c_test;

	if (GET_CODE (insn) == DEFINE_INSN)
	  c_test = XSTR (insn, 2);
	else
	  c_test = XSTR (insn, 1);

	if (pred_name[0] != 0)
	  {
	    pred = lookup_predicate (pred_name);
	    if (!pred)
	      message_with_line (pattern_lineno,
				 "warning: unknown predicate '%s'",
				 pred_name);
	  }
	else
	  pred = 0;

	if (code == MATCH_OPERAND)
	  {
	    const char constraints0 = XSTR (pattern, 2)[0];

	    /* In DEFINE_EXPAND, DEFINE_SPLIT, and DEFINE_PEEPHOLE2, we
	       don't use the MATCH_OPERAND constraint, only the predicate.
	       This is confusing to folks doing new ports, so help them
	       not make the mistake.  */
	    if (GET_CODE (insn) == DEFINE_EXPAND
		|| GET_CODE (insn) == DEFINE_SPLIT
		|| GET_CODE (insn) == DEFINE_PEEPHOLE2)
	      {
		if (constraints0)
		  message_with_line (pattern_lineno,
				     "warning: constraints not supported in %s",
				     rtx_name[GET_CODE (insn)]);
	      }

	    /* A MATCH_OPERAND that is a SET should have an output reload.  */
	    else if (set && constraints0)
	      {
		if (set_code == '+')
		  {
		    if (constraints0 == '+')
		      ;
		    /* If we've only got an output reload for this operand,
		       we'd better have a matching input operand.  */
		    else if (constraints0 == '='
			     && find_matching_operand (insn, XINT (pattern, 0)))
		      ;
		    else
		      {
			message_with_line (pattern_lineno,
					   "operand %d missing in-out reload",
					   XINT (pattern, 0));
			error_count++;
		      }
		  }
		else if (constraints0 != '=' && constraints0 != '+')
		  {
		    message_with_line (pattern_lineno,
				       "operand %d missing output reload",
				       XINT (pattern, 0));
		    error_count++;
		  }
	      }
	  }

	/* Allowing non-lvalues in destinations -- particularly CONST_INT --
	   while not likely to occur at runtime, results in less efficient
	   code from insn-recog.c.  */
	if (set && pred && pred->allows_non_lvalue)
	  message_with_line (pattern_lineno,
			     "warning: destination operand %d "
			     "allows non-lvalue",
			     XINT (pattern, 0));

	/* A modeless MATCH_OPERAND can be handy when we can check for
	   multiple modes in the c_test.  In most other cases, it is a
	   mistake.  Only DEFINE_INSN is eligible, since SPLIT and
	   PEEP2 can FAIL within the output pattern.  Exclude special
	   predicates, which check the mode themselves.  Also exclude
	   predicates that allow only constants.  Exclude the SET_DEST
	   of a call instruction, as that is a common idiom.  */

	if (GET_MODE (pattern) == VOIDmode
	    && code == MATCH_OPERAND
	    && GET_CODE (insn) == DEFINE_INSN
	    && pred
	    && !pred->special
	    && pred->allows_non_const
	    && strstr (c_test, "operands") == NULL
	    && ! (set
		  && GET_CODE (set) == SET
		  && GET_CODE (SET_SRC (set)) == CALL))
	  message_with_line (pattern_lineno,
			     "warning: operand %d missing mode?",
			     XINT (pattern, 0));
	return;
      }

    case SET:
      {
	enum machine_mode dmode, smode;
	rtx dest, src;

	dest = SET_DEST (pattern);
	src = SET_SRC (pattern);

	/* STRICT_LOW_PART is a wrapper.  Its argument is the real
	   destination, and it's mode should match the source.  */
	if (GET_CODE (dest) == STRICT_LOW_PART)
	  dest = XEXP (dest, 0);

	/* Find the referent for a DUP.  */

	if (GET_CODE (dest) == MATCH_DUP
	    || GET_CODE (dest) == MATCH_OP_DUP
	    || GET_CODE (dest) == MATCH_PAR_DUP)
	  dest = find_operand (insn, XINT (dest, 0), NULL);

	if (GET_CODE (src) == MATCH_DUP
	    || GET_CODE (src) == MATCH_OP_DUP
	    || GET_CODE (src) == MATCH_PAR_DUP)
	  src = find_operand (insn, XINT (src, 0), NULL);

	dmode = GET_MODE (dest);
	smode = GET_MODE (src);

	/* The mode of an ADDRESS_OPERAND is the mode of the memory
	   reference, not the mode of the address.  */
	if (GET_CODE (src) == MATCH_OPERAND
	    && ! strcmp (XSTR (src, 1), "address_operand"))
	  ;

        /* The operands of a SET must have the same mode unless one
	   is VOIDmode.  */
        else if (dmode != VOIDmode && smode != VOIDmode && dmode != smode)
	  {
	    message_with_line (pattern_lineno,
			       "mode mismatch in set: %smode vs %smode",
			       GET_MODE_NAME (dmode), GET_MODE_NAME (smode));
	    error_count++;
	  }

	/* If only one of the operands is VOIDmode, and PC or CC0 is
	   not involved, it's probably a mistake.  */
	else if (dmode != smode
		 && GET_CODE (dest) != PC
		 && GET_CODE (dest) != CC0
		 && GET_CODE (src) != PC
		 && GET_CODE (src) != CC0
		 && GET_CODE (src) != CONST_INT)
	  {
	    const char *which;
	    which = (dmode == VOIDmode ? "destination" : "source");
	    message_with_line (pattern_lineno,
			       "warning: %s missing a mode?", which);
	  }

	if (dest != SET_DEST (pattern))
	  validate_pattern (dest, insn, pattern, '=');
	validate_pattern (SET_DEST (pattern), insn, pattern, '=');
        validate_pattern (SET_SRC (pattern), insn, NULL_RTX, 0);
        return;
      }

    case CLOBBER:
      validate_pattern (SET_DEST (pattern), insn, pattern, '=');
      return;

    case ZERO_EXTRACT:
      validate_pattern (XEXP (pattern, 0), insn, set, set ? '+' : 0);
      validate_pattern (XEXP (pattern, 1), insn, NULL_RTX, 0);
      validate_pattern (XEXP (pattern, 2), insn, NULL_RTX, 0);
      return;

    case STRICT_LOW_PART:
      validate_pattern (XEXP (pattern, 0), insn, set, set ? '+' : 0);
      return;

    case LABEL_REF:
      if (GET_MODE (XEXP (pattern, 0)) != VOIDmode)
	{
	  message_with_line (pattern_lineno,
			     "operand to label_ref %smode not VOIDmode",
			     GET_MODE_NAME (GET_MODE (XEXP (pattern, 0))));
	  error_count++;
	}
      break;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  len = GET_RTX_LENGTH (code);
  for (i = 0; i < len; i++)
    {
      switch (fmt[i])
	{
	case 'e': case 'u':
	  validate_pattern (XEXP (pattern, i), insn, NULL_RTX, 0);
	  break;

	case 'E':
	  for (j = 0; j < XVECLEN (pattern, i); j++)
	    validate_pattern (XVECEXP (pattern, i, j), insn, NULL_RTX, 0);
	  break;

	case 'i': case 'w': case '0': case 's':
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}

/* Create a chain of nodes to verify that an rtl expression matches
   PATTERN.

   LAST is a pointer to the listhead in the previous node in the chain (or
   in the calling function, for the first node).

   POSITION is the string representing the current position in the insn.

   INSN_TYPE is the type of insn for which we are emitting code.

   A pointer to the final node in the chain is returned.  */

static struct decision *
add_to_sequence (rtx pattern, struct decision_head *last, const char *position,
		 enum routine_type insn_type, int top)
{
  RTX_CODE code;
  struct decision *this, *sub;
  struct decision_test *test;
  struct decision_test **place;
  char *subpos;
  size_t i;
  const char *fmt;
  int depth = strlen (position);
  int len;
  enum machine_mode mode;

  if (depth > max_depth)
    max_depth = depth;

  subpos = xmalloc (depth + 2);
  strcpy (subpos, position);
  subpos[depth + 1] = 0;

  sub = this = new_decision (position, last);
  place = &this->tests;

 restart:
  mode = GET_MODE (pattern);
  code = GET_CODE (pattern);

  switch (code)
    {
    case PARALLEL:
      /* Toplevel peephole pattern.  */
      if (insn_type == PEEPHOLE2 && top)
	{
	  int num_insns;

	  /* Check we have sufficient insns.  This avoids complications
	     because we then know peep2_next_insn never fails.  */
	  num_insns = XVECLEN (pattern, 0);
	  if (num_insns > 1)
	    {
	      test = new_decision_test (DT_num_insns, &place);
	      test->u.num_insns = num_insns;
	      last = &sub->success;
	    }
	  else
	    {
	      /* We don't need the node we just created -- unlink it.  */
	      last->first = last->last = NULL;
	    }

	  for (i = 0; i < (size_t) XVECLEN (pattern, 0); i++)
	    {
	      /* Which insn we're looking at is represented by A-Z. We don't
	         ever use 'A', however; it is always implied.  */

	      subpos[depth] = (i > 0 ? 'A' + i : 0);
	      sub = add_to_sequence (XVECEXP (pattern, 0, i),
				     last, subpos, insn_type, 0);
	      last = &sub->success;
	    }
	  goto ret;
	}

      /* Else nothing special.  */
      break;

    case MATCH_PARALLEL:
      /* The explicit patterns within a match_parallel enforce a minimum
	 length on the vector.  The match_parallel predicate may allow
	 for more elements.  We do need to check for this minimum here
	 or the code generated to match the internals may reference data
	 beyond the end of the vector.  */
      test = new_decision_test (DT_veclen_ge, &place);
      test->u.veclen = XVECLEN (pattern, 2);
      /* Fall through.  */

    case MATCH_OPERAND:
    case MATCH_SCRATCH:
    case MATCH_OPERATOR:
      {
	RTX_CODE was_code = code;
	const char *pred_name;
	bool allows_const_int = true;

	if (code == MATCH_SCRATCH)
	  {
	    pred_name = "scratch_operand";
	    code = UNKNOWN;
	  }
	else
	  {
	    pred_name = XSTR (pattern, 1);
	    if (code == MATCH_PARALLEL)
	      code = PARALLEL;
	    else
	      code = UNKNOWN;
	  }

	if (pred_name[0] != 0)
	  {
	    const struct pred_data *pred;

	    test = new_decision_test (DT_pred, &place);
	    test->u.pred.name = pred_name;
	    test->u.pred.mode = mode;

	    /* See if we know about this predicate.
	       If we do, remember it for use below.

	       We can optimize the generated code a little if either
	       (a) the predicate only accepts one code, or (b) the
	       predicate does not allow CONST_INT, in which case it
	       can match only if the modes match.  */
	    pred = lookup_predicate (pred_name);
	    if (pred)
	      {
		test->u.pred.data = pred;
		allows_const_int = pred->codes[CONST_INT];
		if (was_code == MATCH_PARALLEL
		    && pred->singleton != PARALLEL)
		  message_with_line (pattern_lineno,
			"predicate '%s' used in match_parallel "
			"does not allow only PARALLEL", pred->name);
		else
		  code = pred->singleton;
	      }
	    else
	      message_with_line (pattern_lineno,
			"warning: unknown predicate '%s' in '%s' expression",
			pred_name, GET_RTX_NAME (was_code));
	  }

	/* Can't enforce a mode if we allow const_int.  */
	if (allows_const_int)
	  mode = VOIDmode;

	/* Accept the operand, i.e. record it in `operands'.  */
	test = new_decision_test (DT_accept_op, &place);
	test->u.opno = XINT (pattern, 0);

	if (was_code == MATCH_OPERATOR || was_code == MATCH_PARALLEL)
	  {
	    char base = (was_code == MATCH_OPERATOR ? '0' : 'a');
	    for (i = 0; i < (size_t) XVECLEN (pattern, 2); i++)
	      {
		subpos[depth] = i + base;
		sub = add_to_sequence (XVECEXP (pattern, 2, i),
				       &sub->success, subpos, insn_type, 0);
	      }
	  }
	goto fini;
      }

    case MATCH_OP_DUP:
      code = UNKNOWN;

      test = new_decision_test (DT_dup, &place);
      test->u.dup = XINT (pattern, 0);

      test = new_decision_test (DT_accept_op, &place);
      test->u.opno = XINT (pattern, 0);

      for (i = 0; i < (size_t) XVECLEN (pattern, 1); i++)
	{
	  subpos[depth] = i + '0';
	  sub = add_to_sequence (XVECEXP (pattern, 1, i),
				 &sub->success, subpos, insn_type, 0);
	}
      goto fini;

    case MATCH_DUP:
    case MATCH_PAR_DUP:
      code = UNKNOWN;

      test = new_decision_test (DT_dup, &place);
      test->u.dup = XINT (pattern, 0);
      goto fini;

    case ADDRESS:
      pattern = XEXP (pattern, 0);
      goto restart;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  len = GET_RTX_LENGTH (code);

  /* Do tests against the current node first.  */
  for (i = 0; i < (size_t) len; i++)
    {
      if (fmt[i] == 'i')
	{
	  gcc_assert (i < 2);
	  
	  if (!i)
	    {
	      test = new_decision_test (DT_elt_zero_int, &place);
	      test->u.intval = XINT (pattern, i);
	    }
	  else
	    {
	      test = new_decision_test (DT_elt_one_int, &place);
	      test->u.intval = XINT (pattern, i);
	    }
	}
      else if (fmt[i] == 'w')
	{
	  /* If this value actually fits in an int, we can use a switch
	     statement here, so indicate that.  */
	  enum decision_type type
	    = ((int) XWINT (pattern, i) == XWINT (pattern, i))
	      ? DT_elt_zero_wide_safe : DT_elt_zero_wide;

	  gcc_assert (!i);

	  test = new_decision_test (type, &place);
	  test->u.intval = XWINT (pattern, i);
	}
      else if (fmt[i] == 'E')
	{
	  gcc_assert (!i);

	  test = new_decision_test (DT_veclen, &place);
	  test->u.veclen = XVECLEN (pattern, i);
	}
    }

  /* Now test our sub-patterns.  */
  for (i = 0; i < (size_t) len; i++)
    {
      switch (fmt[i])
	{
	case 'e': case 'u':
	  subpos[depth] = '0' + i;
	  sub = add_to_sequence (XEXP (pattern, i), &sub->success,
				 subpos, insn_type, 0);
	  break;

	case 'E':
	  {
	    int j;
	    for (j = 0; j < XVECLEN (pattern, i); j++)
	      {
		subpos[depth] = 'a' + j;
		sub = add_to_sequence (XVECEXP (pattern, i, j),
				       &sub->success, subpos, insn_type, 0);
	      }
	    break;
	  }

	case 'i': case 'w':
	  /* Handled above.  */
	  break;
	case '0':
	  break;

	default:
	  gcc_unreachable ();
	}
    }

 fini:
  /* Insert nodes testing mode and code, if they're still relevant,
     before any of the nodes we may have added above.  */
  if (code != UNKNOWN)
    {
      place = &this->tests;
      test = new_decision_test (DT_code, &place);
      test->u.code = code;
    }

  if (mode != VOIDmode)
    {
      place = &this->tests;
      test = new_decision_test (DT_mode, &place);
      test->u.mode = mode;
    }

  /* If we didn't insert any tests or accept nodes, hork.  */
  gcc_assert (this->tests);

 ret:
  free (subpos);
  return sub;
}

/* A subroutine of maybe_both_true; examines only one test.
   Returns > 0 for "definitely both true" and < 0 for "maybe both true".  */

static int
maybe_both_true_2 (struct decision_test *d1, struct decision_test *d2)
{
  if (d1->type == d2->type)
    {
      switch (d1->type)
	{
	case DT_num_insns:
	  if (d1->u.num_insns == d2->u.num_insns)
	    return 1;
	  else
	    return -1;

	case DT_mode:
	  return d1->u.mode == d2->u.mode;

	case DT_code:
	  return d1->u.code == d2->u.code;

	case DT_veclen:
	  return d1->u.veclen == d2->u.veclen;

	case DT_elt_zero_int:
	case DT_elt_one_int:
	case DT_elt_zero_wide:
	case DT_elt_zero_wide_safe:
	  return d1->u.intval == d2->u.intval;

	default:
	  break;
	}
    }

  /* If either has a predicate that we know something about, set
     things up so that D1 is the one that always has a known
     predicate.  Then see if they have any codes in common.  */

  if (d1->type == DT_pred || d2->type == DT_pred)
    {
      if (d2->type == DT_pred)
	{
	  struct decision_test *tmp;
	  tmp = d1, d1 = d2, d2 = tmp;
	}

      /* If D2 tests a mode, see if it matches D1.  */
      if (d1->u.pred.mode != VOIDmode)
	{
	  if (d2->type == DT_mode)
	    {
	      if (d1->u.pred.mode != d2->u.mode
		  /* The mode of an address_operand predicate is the
		     mode of the memory, not the operand.  It can only
		     be used for testing the predicate, so we must
		     ignore it here.  */
		  && strcmp (d1->u.pred.name, "address_operand") != 0)
		return 0;
	    }
	  /* Don't check two predicate modes here, because if both predicates
	     accept CONST_INT, then both can still be true even if the modes
	     are different.  If they don't accept CONST_INT, there will be a
	     separate DT_mode that will make maybe_both_true_1 return 0.  */
	}

      if (d1->u.pred.data)
	{
	  /* If D2 tests a code, see if it is in the list of valid
	     codes for D1's predicate.  */
	  if (d2->type == DT_code)
	    {
	      if (!d1->u.pred.data->codes[d2->u.code])
		return 0;
	    }

	  /* Otherwise see if the predicates have any codes in common.  */
	  else if (d2->type == DT_pred && d2->u.pred.data)
	    {
	      bool common = false;
	      enum rtx_code c;

	      for (c = 0; c < NUM_RTX_CODE; c++)
		if (d1->u.pred.data->codes[c] && d2->u.pred.data->codes[c])
		  {
		    common = true;
		    break;
		  }

	      if (!common)
		return 0;
	    }
	}
    }

  /* Tests vs veclen may be known when strict equality is involved.  */
  if (d1->type == DT_veclen && d2->type == DT_veclen_ge)
    return d1->u.veclen >= d2->u.veclen;
  if (d1->type == DT_veclen_ge && d2->type == DT_veclen)
    return d2->u.veclen >= d1->u.veclen;

  return -1;
}

/* A subroutine of maybe_both_true; examines all the tests for a given node.
   Returns > 0 for "definitely both true" and < 0 for "maybe both true".  */

static int
maybe_both_true_1 (struct decision_test *d1, struct decision_test *d2)
{
  struct decision_test *t1, *t2;

  /* A match_operand with no predicate can match anything.  Recognize
     this by the existence of a lone DT_accept_op test.  */
  if (d1->type == DT_accept_op || d2->type == DT_accept_op)
    return 1;

  /* Eliminate pairs of tests while they can exactly match.  */
  while (d1 && d2 && d1->type == d2->type)
    {
      if (maybe_both_true_2 (d1, d2) == 0)
	return 0;
      d1 = d1->next, d2 = d2->next;
    }

  /* After that, consider all pairs.  */
  for (t1 = d1; t1 ; t1 = t1->next)
    for (t2 = d2; t2 ; t2 = t2->next)
      if (maybe_both_true_2 (t1, t2) == 0)
	return 0;

  return -1;
}

/* Return 0 if we can prove that there is no RTL that can match both
   D1 and D2.  Otherwise, return 1 (it may be that there is an RTL that
   can match both or just that we couldn't prove there wasn't such an RTL).

   TOPLEVEL is nonzero if we are to only look at the top level and not
   recursively descend.  */

static int
maybe_both_true (struct decision *d1, struct decision *d2,
		 int toplevel)
{
  struct decision *p1, *p2;
  int cmp;

  /* Don't compare strings on the different positions in insn.  Doing so
     is incorrect and results in false matches from constructs like

	[(set (subreg:HI (match_operand:SI "register_operand" "r") 0)
	      (subreg:HI (match_operand:SI "register_operand" "r") 0))]
     vs
	[(set (match_operand:HI "register_operand" "r")
	      (match_operand:HI "register_operand" "r"))]

     If we are presented with such, we are recursing through the remainder
     of a node's success nodes (from the loop at the end of this function).
     Skip forward until we come to a position that matches.

     Due to the way position strings are constructed, we know that iterating
     forward from the lexically lower position (e.g. "00") will run into
     the lexically higher position (e.g. "1") and not the other way around.
     This saves a bit of effort.  */

  cmp = strcmp (d1->position, d2->position);
  if (cmp != 0)
    {
      gcc_assert (!toplevel);

      /* If the d2->position was lexically lower, swap.  */
      if (cmp > 0)
	p1 = d1, d1 = d2, d2 = p1;

      if (d1->success.first == 0)
	return 1;
      for (p1 = d1->success.first; p1; p1 = p1->next)
	if (maybe_both_true (p1, d2, 0))
	  return 1;

      return 0;
    }

  /* Test the current level.  */
  cmp = maybe_both_true_1 (d1->tests, d2->tests);
  if (cmp >= 0)
    return cmp;

  /* We can't prove that D1 and D2 cannot both be true.  If we are only
     to check the top level, return 1.  Otherwise, see if we can prove
     that all choices in both successors are mutually exclusive.  If
     either does not have any successors, we can't prove they can't both
     be true.  */

  if (toplevel || d1->success.first == 0 || d2->success.first == 0)
    return 1;

  for (p1 = d1->success.first; p1; p1 = p1->next)
    for (p2 = d2->success.first; p2; p2 = p2->next)
      if (maybe_both_true (p1, p2, 0))
	return 1;

  return 0;
}

/* A subroutine of nodes_identical.  Examine two tests for equivalence.  */

static int
nodes_identical_1 (struct decision_test *d1, struct decision_test *d2)
{
  switch (d1->type)
    {
    case DT_num_insns:
      return d1->u.num_insns == d2->u.num_insns;

    case DT_mode:
      return d1->u.mode == d2->u.mode;

    case DT_code:
      return d1->u.code == d2->u.code;

    case DT_pred:
      return (d1->u.pred.mode == d2->u.pred.mode
	      && strcmp (d1->u.pred.name, d2->u.pred.name) == 0);

    case DT_c_test:
      return strcmp (d1->u.c_test, d2->u.c_test) == 0;

    case DT_veclen:
    case DT_veclen_ge:
      return d1->u.veclen == d2->u.veclen;

    case DT_dup:
      return d1->u.dup == d2->u.dup;

    case DT_elt_zero_int:
    case DT_elt_one_int:
    case DT_elt_zero_wide:
    case DT_elt_zero_wide_safe:
      return d1->u.intval == d2->u.intval;

    case DT_accept_op:
      return d1->u.opno == d2->u.opno;

    case DT_accept_insn:
      /* Differences will be handled in merge_accept_insn.  */
      return 1;

    default:
      gcc_unreachable ();
    }
}

/* True iff the two nodes are identical (on one level only).  Due
   to the way these lists are constructed, we shouldn't have to
   consider different orderings on the tests.  */

static int
nodes_identical (struct decision *d1, struct decision *d2)
{
  struct decision_test *t1, *t2;

  for (t1 = d1->tests, t2 = d2->tests; t1 && t2; t1 = t1->next, t2 = t2->next)
    {
      if (t1->type != t2->type)
	return 0;
      if (! nodes_identical_1 (t1, t2))
	return 0;
    }

  /* For success, they should now both be null.  */
  if (t1 != t2)
    return 0;

  /* Check that their subnodes are at the same position, as any one set
     of sibling decisions must be at the same position.  Allowing this
     requires complications to find_afterward and when change_state is
     invoked.  */
  if (d1->success.first
      && d2->success.first
      && strcmp (d1->success.first->position, d2->success.first->position))
    return 0;

  return 1;
}

/* A subroutine of merge_trees; given two nodes that have been declared
   identical, cope with two insn accept states.  If they differ in the
   number of clobbers, then the conflict was created by make_insn_sequence
   and we can drop the with-clobbers version on the floor.  If both
   nodes have no additional clobbers, we have found an ambiguity in the
   source machine description.  */

static void
merge_accept_insn (struct decision *oldd, struct decision *addd)
{
  struct decision_test *old, *add;

  for (old = oldd->tests; old; old = old->next)
    if (old->type == DT_accept_insn)
      break;
  if (old == NULL)
    return;

  for (add = addd->tests; add; add = add->next)
    if (add->type == DT_accept_insn)
      break;
  if (add == NULL)
    return;

  /* If one node is for a normal insn and the second is for the base
     insn with clobbers stripped off, the second node should be ignored.  */

  if (old->u.insn.num_clobbers_to_add == 0
      && add->u.insn.num_clobbers_to_add > 0)
    {
      /* Nothing to do here.  */
    }
  else if (old->u.insn.num_clobbers_to_add > 0
	   && add->u.insn.num_clobbers_to_add == 0)
    {
      /* In this case, replace OLD with ADD.  */
      old->u.insn = add->u.insn;
    }
  else
    {
      message_with_line (add->u.insn.lineno, "`%s' matches `%s'",
			 get_insn_name (add->u.insn.code_number),
			 get_insn_name (old->u.insn.code_number));
      message_with_line (old->u.insn.lineno, "previous definition of `%s'",
			 get_insn_name (old->u.insn.code_number));
      error_count++;
    }
}

/* Merge two decision trees OLDH and ADDH, modifying OLDH destructively.  */

static void
merge_trees (struct decision_head *oldh, struct decision_head *addh)
{
  struct decision *next, *add;

  if (addh->first == 0)
    return;
  if (oldh->first == 0)
    {
      *oldh = *addh;
      return;
    }

  /* Trying to merge bits at different positions isn't possible.  */
  gcc_assert (!strcmp (oldh->first->position, addh->first->position));

  for (add = addh->first; add ; add = next)
    {
      struct decision *old, *insert_before = NULL;

      next = add->next;

      /* The semantics of pattern matching state that the tests are
	 done in the order given in the MD file so that if an insn
	 matches two patterns, the first one will be used.  However,
	 in practice, most, if not all, patterns are unambiguous so
	 that their order is independent.  In that case, we can merge
	 identical tests and group all similar modes and codes together.

	 Scan starting from the end of OLDH until we reach a point
	 where we reach the head of the list or where we pass a
	 pattern that could also be true if NEW is true.  If we find
	 an identical pattern, we can merge them.  Also, record the
	 last node that tests the same code and mode and the last one
	 that tests just the same mode.

	 If we have no match, place NEW after the closest match we found.  */

      for (old = oldh->last; old; old = old->prev)
	{
	  if (nodes_identical (old, add))
	    {
	      merge_accept_insn (old, add);
	      merge_trees (&old->success, &add->success);
	      goto merged_nodes;
	    }

	  if (maybe_both_true (old, add, 0))
	    break;

	  /* Insert the nodes in DT test type order, which is roughly
	     how expensive/important the test is.  Given that the tests
	     are also ordered within the list, examining the first is
	     sufficient.  */
	  if ((int) add->tests->type < (int) old->tests->type)
	    insert_before = old;
	}

      if (insert_before == NULL)
	{
	  add->next = NULL;
	  add->prev = oldh->last;
	  oldh->last->next = add;
	  oldh->last = add;
	}
      else
	{
	  if ((add->prev = insert_before->prev) != NULL)
	    add->prev->next = add;
	  else
	    oldh->first = add;
	  add->next = insert_before;
	  insert_before->prev = add;
	}

    merged_nodes:;
    }
}

/* Walk the tree looking for sub-nodes that perform common tests.
   Factor out the common test into a new node.  This enables us
   (depending on the test type) to emit switch statements later.  */

static void
factor_tests (struct decision_head *head)
{
  struct decision *first, *next;

  for (first = head->first; first && first->next; first = next)
    {
      enum decision_type type;
      struct decision *new, *old_last;

      type = first->tests->type;
      next = first->next;

      /* Want at least two compatible sequential nodes.  */
      if (next->tests->type != type)
	continue;

      /* Don't want all node types, just those we can turn into
	 switch statements.  */
      if (type != DT_mode
	  && type != DT_code
	  && type != DT_veclen
	  && type != DT_elt_zero_int
	  && type != DT_elt_one_int
	  && type != DT_elt_zero_wide_safe)
	continue;

      /* If we'd been performing more than one test, create a new node
         below our first test.  */
      if (first->tests->next != NULL)
	{
	  new = new_decision (first->position, &first->success);
	  new->tests = first->tests->next;
	  first->tests->next = NULL;
	}

      /* Crop the node tree off after our first test.  */
      first->next = NULL;
      old_last = head->last;
      head->last = first;

      /* For each compatible test, adjust to perform only one test in
	 the top level node, then merge the node back into the tree.  */
      do
	{
	  struct decision_head h;

	  if (next->tests->next != NULL)
	    {
	      new = new_decision (next->position, &next->success);
	      new->tests = next->tests->next;
	      next->tests->next = NULL;
	    }
	  new = next;
	  next = next->next;
	  new->next = NULL;
	  h.first = h.last = new;

	  merge_trees (head, &h);
	}
      while (next && next->tests->type == type);

      /* After we run out of compatible tests, graft the remaining nodes
	 back onto the tree.  */
      if (next)
	{
	  next->prev = head->last;
	  head->last->next = next;
	  head->last = old_last;
	}
    }

  /* Recurse.  */
  for (first = head->first; first; first = first->next)
    factor_tests (&first->success);
}

/* After factoring, try to simplify the tests on any one node.
   Tests that are useful for switch statements are recognizable
   by having only a single test on a node -- we'll be manipulating
   nodes with multiple tests:

   If we have mode tests or code tests that are redundant with
   predicates, remove them.  */

static void
simplify_tests (struct decision_head *head)
{
  struct decision *tree;

  for (tree = head->first; tree; tree = tree->next)
    {
      struct decision_test *a, *b;

      a = tree->tests;
      b = a->next;
      if (b == NULL)
	continue;

      /* Find a predicate node.  */
      while (b && b->type != DT_pred)
	b = b->next;
      if (b)
	{
	  /* Due to how these tests are constructed, we don't even need
	     to check that the mode and code are compatible -- they were
	     generated from the predicate in the first place.  */
	  while (a->type == DT_mode || a->type == DT_code)
	    a = a->next;
	  tree->tests = a;
	}
    }

  /* Recurse.  */
  for (tree = head->first; tree; tree = tree->next)
    simplify_tests (&tree->success);
}

/* Count the number of subnodes of HEAD.  If the number is high enough,
   make the first node in HEAD start a separate subroutine in the C code
   that is generated.  */

static int
break_out_subroutines (struct decision_head *head, int initial)
{
  int size = 0;
  struct decision *sub;

  for (sub = head->first; sub; sub = sub->next)
    size += 1 + break_out_subroutines (&sub->success, 0);

  if (size > SUBROUTINE_THRESHOLD && ! initial)
    {
      head->first->subroutine_number = ++next_subroutine_number;
      size = 1;
    }
  return size;
}

/* For each node p, find the next alternative that might be true
   when p is true.  */

static void
find_afterward (struct decision_head *head, struct decision *real_afterward)
{
  struct decision *p, *q, *afterward;

  /* We can't propagate alternatives across subroutine boundaries.
     This is not incorrect, merely a minor optimization loss.  */

  p = head->first;
  afterward = (p->subroutine_number > 0 ? NULL : real_afterward);

  for ( ; p ; p = p->next)
    {
      /* Find the next node that might be true if this one fails.  */
      for (q = p->next; q ; q = q->next)
	if (maybe_both_true (p, q, 1))
	  break;

      /* If we reached the end of the list without finding one,
	 use the incoming afterward position.  */
      if (!q)
	q = afterward;
      p->afterward = q;
      if (q)
	q->need_label = 1;
    }

  /* Recurse.  */
  for (p = head->first; p ; p = p->next)
    if (p->success.first)
      find_afterward (&p->success, p->afterward);

  /* When we are generating a subroutine, record the real afterward
     position in the first node where write_tree can find it, and we
     can do the right thing at the subroutine call site.  */
  p = head->first;
  if (p->subroutine_number > 0)
    p->afterward = real_afterward;
}

/* Assuming that the state of argument is denoted by OLDPOS, take whatever
   actions are necessary to move to NEWPOS.  If we fail to move to the
   new state, branch to node AFTERWARD if nonzero, otherwise return.

   Failure to move to the new state can only occur if we are trying to
   match multiple insns and we try to step past the end of the stream.  */

static void
change_state (const char *oldpos, const char *newpos, const char *indent)
{
  int odepth = strlen (oldpos);
  int ndepth = strlen (newpos);
  int depth;
  int old_has_insn, new_has_insn;

  /* Pop up as many levels as necessary.  */
  for (depth = odepth; strncmp (oldpos, newpos, depth) != 0; --depth)
    continue;

  /* Hunt for the last [A-Z] in both strings.  */
  for (old_has_insn = odepth - 1; old_has_insn >= 0; --old_has_insn)
    if (ISUPPER (oldpos[old_has_insn]))
      break;
  for (new_has_insn = ndepth - 1; new_has_insn >= 0; --new_has_insn)
    if (ISUPPER (newpos[new_has_insn]))
      break;

  /* Go down to desired level.  */
  while (depth < ndepth)
    {
      /* It's a different insn from the first one.  */
      if (ISUPPER (newpos[depth]))
	{
	  printf ("%stem = peep2_next_insn (%d);\n",
		  indent, newpos[depth] - 'A');
	  printf ("%sx%d = PATTERN (tem);\n", indent, depth + 1);
	}
      else if (ISLOWER (newpos[depth]))
	printf ("%sx%d = XVECEXP (x%d, 0, %d);\n",
		indent, depth + 1, depth, newpos[depth] - 'a');
      else
	printf ("%sx%d = XEXP (x%d, %c);\n",
		indent, depth + 1, depth, newpos[depth]);
      ++depth;
    }
}

/* Print the enumerator constant for CODE -- the upcase version of
   the name.  */

static void
print_code (enum rtx_code code)
{
  const char *p;
  for (p = GET_RTX_NAME (code); *p; p++)
    putchar (TOUPPER (*p));
}

/* Emit code to cross an afterward link -- change state and branch.  */

static void
write_afterward (struct decision *start, struct decision *afterward,
		 const char *indent)
{
  if (!afterward || start->subroutine_number > 0)
    printf("%sgoto ret0;\n", indent);
  else
    {
      change_state (start->position, afterward->position, indent);
      printf ("%sgoto L%d;\n", indent, afterward->number);
    }
}

/* Emit a HOST_WIDE_INT as an integer constant expression.  We need to take
   special care to avoid "decimal constant is so large that it is unsigned"
   warnings in the resulting code.  */

static void
print_host_wide_int (HOST_WIDE_INT val)
{
  HOST_WIDE_INT min = (unsigned HOST_WIDE_INT)1 << (HOST_BITS_PER_WIDE_INT-1);
  if (val == min)
    printf ("(" HOST_WIDE_INT_PRINT_DEC_C "-1)", val + 1);
  else
    printf (HOST_WIDE_INT_PRINT_DEC_C, val);
}

/* Emit a switch statement, if possible, for an initial sequence of
   nodes at START.  Return the first node yet untested.  */

static struct decision *
write_switch (struct decision *start, int depth)
{
  struct decision *p = start;
  enum decision_type type = p->tests->type;
  struct decision *needs_label = NULL;

  /* If we have two or more nodes in sequence that test the same one
     thing, we may be able to use a switch statement.  */

  if (!p->next
      || p->tests->next
      || p->next->tests->type != type
      || p->next->tests->next
      || nodes_identical_1 (p->tests, p->next->tests))
    return p;

  /* DT_code is special in that we can do interesting things with
     known predicates at the same time.  */
  if (type == DT_code)
    {
      char codemap[NUM_RTX_CODE];
      struct decision *ret;
      RTX_CODE code;

      memset (codemap, 0, sizeof(codemap));

      printf ("  switch (GET_CODE (x%d))\n    {\n", depth);
      code = p->tests->u.code;
      do
	{
	  if (p != start && p->need_label && needs_label == NULL)
	    needs_label = p;

	  printf ("    case ");
	  print_code (code);
	  printf (":\n      goto L%d;\n", p->success.first->number);
	  p->success.first->need_label = 1;

	  codemap[code] = 1;
	  p = p->next;
	}
      while (p
	     && ! p->tests->next
	     && p->tests->type == DT_code
	     && ! codemap[code = p->tests->u.code]);

      /* If P is testing a predicate that we know about and we haven't
	 seen any of the codes that are valid for the predicate, we can
	 write a series of "case" statement, one for each possible code.
	 Since we are already in a switch, these redundant tests are very
	 cheap and will reduce the number of predicates called.  */

      /* Note that while we write out cases for these predicates here,
	 we don't actually write the test here, as it gets kinda messy.
	 It is trivial to leave this to later by telling our caller that
	 we only processed the CODE tests.  */
      if (needs_label != NULL)
	ret = needs_label;
      else
	ret = p;

      while (p && p->tests->type == DT_pred && p->tests->u.pred.data)
	{
	  const struct pred_data *data = p->tests->u.pred.data;
	  RTX_CODE c;
	  for (c = 0; c < NUM_RTX_CODE; c++)
	    if (codemap[c] && data->codes[c])
	      goto pred_done;

	  for (c = 0; c < NUM_RTX_CODE; c++)
	    if (data->codes[c])
	      {
		fputs ("    case ", stdout);
		print_code (c);
		fputs (":\n", stdout);
		codemap[c] = 1;
	      }

	  printf ("      goto L%d;\n", p->number);
	  p->need_label = 1;
	  p = p->next;
	}

    pred_done:
      /* Make the default case skip the predicates we managed to match.  */

      printf ("    default:\n");
      if (p != ret)
	{
	  if (p)
	    {
	      printf ("      goto L%d;\n", p->number);
	      p->need_label = 1;
	    }
	  else
	    write_afterward (start, start->afterward, "      ");
	}
      else
	printf ("     break;\n");
      printf ("   }\n");

      return ret;
    }
  else if (type == DT_mode
	   || type == DT_veclen
	   || type == DT_elt_zero_int
	   || type == DT_elt_one_int
	   || type == DT_elt_zero_wide_safe)
    {
      const char *indent = "";

      /* We cast switch parameter to integer, so we must ensure that the value
	 fits.  */
      if (type == DT_elt_zero_wide_safe)
	{
	  indent = "  ";
	  printf("  if ((int) XWINT (x%d, 0) == XWINT (x%d, 0))\n", depth, depth);
	}
      printf ("%s  switch (", indent);
      switch (type)
	{
	case DT_mode:
	  printf ("GET_MODE (x%d)", depth);
	  break;
	case DT_veclen:
	  printf ("XVECLEN (x%d, 0)", depth);
	  break;
	case DT_elt_zero_int:
	  printf ("XINT (x%d, 0)", depth);
	  break;
	case DT_elt_one_int:
	  printf ("XINT (x%d, 1)", depth);
	  break;
	case DT_elt_zero_wide_safe:
	  /* Convert result of XWINT to int for portability since some C
	     compilers won't do it and some will.  */
	  printf ("(int) XWINT (x%d, 0)", depth);
	  break;
	default:
	  gcc_unreachable ();
	}
      printf (")\n%s    {\n", indent);

      do
	{
	  /* Merge trees will not unify identical nodes if their
	     sub-nodes are at different levels.  Thus we must check
	     for duplicate cases.  */
	  struct decision *q;
	  for (q = start; q != p; q = q->next)
	    if (nodes_identical_1 (p->tests, q->tests))
	      goto case_done;

	  if (p != start && p->need_label && needs_label == NULL)
	    needs_label = p;

	  printf ("%s    case ", indent);
	  switch (type)
	    {
	    case DT_mode:
	      printf ("%smode", GET_MODE_NAME (p->tests->u.mode));
	      break;
	    case DT_veclen:
	      printf ("%d", p->tests->u.veclen);
	      break;
	    case DT_elt_zero_int:
	    case DT_elt_one_int:
	    case DT_elt_zero_wide:
	    case DT_elt_zero_wide_safe:
	      print_host_wide_int (p->tests->u.intval);
	      break;
	    default:
	      gcc_unreachable ();
	    }
	  printf (":\n%s      goto L%d;\n", indent, p->success.first->number);
	  p->success.first->need_label = 1;

	  p = p->next;
	}
      while (p && p->tests->type == type && !p->tests->next);

    case_done:
      printf ("%s    default:\n%s      break;\n%s    }\n",
	      indent, indent, indent);

      return needs_label != NULL ? needs_label : p;
    }
  else
    {
      /* None of the other tests are amenable.  */
      return p;
    }
}

/* Emit code for one test.  */

static void
write_cond (struct decision_test *p, int depth,
	    enum routine_type subroutine_type)
{
  switch (p->type)
    {
    case DT_num_insns:
      printf ("peep2_current_count >= %d", p->u.num_insns);
      break;

    case DT_mode:
      printf ("GET_MODE (x%d) == %smode", depth, GET_MODE_NAME (p->u.mode));
      break;

    case DT_code:
      printf ("GET_CODE (x%d) == ", depth);
      print_code (p->u.code);
      break;

    case DT_veclen:
      printf ("XVECLEN (x%d, 0) == %d", depth, p->u.veclen);
      break;

    case DT_elt_zero_int:
      printf ("XINT (x%d, 0) == %d", depth, (int) p->u.intval);
      break;

    case DT_elt_one_int:
      printf ("XINT (x%d, 1) == %d", depth, (int) p->u.intval);
      break;

    case DT_elt_zero_wide:
    case DT_elt_zero_wide_safe:
      printf ("XWINT (x%d, 0) == ", depth);
      print_host_wide_int (p->u.intval);
      break;

    case DT_const_int:
      printf ("x%d == const_int_rtx[MAX_SAVED_CONST_INT + (%d)]",
	      depth, (int) p->u.intval);
      break;

    case DT_veclen_ge:
      printf ("XVECLEN (x%d, 0) >= %d", depth, p->u.veclen);
      break;

    case DT_dup:
      printf ("rtx_equal_p (x%d, operands[%d])", depth, p->u.dup);
      break;

    case DT_pred:
      printf ("%s (x%d, %smode)", p->u.pred.name, depth,
	      GET_MODE_NAME (p->u.pred.mode));
      break;

    case DT_c_test:
      print_c_condition (p->u.c_test);
      break;

    case DT_accept_insn:
      gcc_assert (subroutine_type == RECOG);
      gcc_assert (p->u.insn.num_clobbers_to_add);
      printf ("pnum_clobbers != NULL");
      break;

    default:
      gcc_unreachable ();
    }
}

/* Emit code for one action.  The previous tests have succeeded;
   TEST is the last of the chain.  In the normal case we simply
   perform a state change.  For the `accept' tests we must do more work.  */

static void
write_action (struct decision *p, struct decision_test *test,
	      int depth, int uncond, struct decision *success,
	      enum routine_type subroutine_type)
{
  const char *indent;
  int want_close = 0;

  if (uncond)
    indent = "  ";
  else if (test->type == DT_accept_op || test->type == DT_accept_insn)
    {
      fputs ("    {\n", stdout);
      indent = "      ";
      want_close = 1;
    }
  else
    indent = "    ";

  if (test->type == DT_accept_op)
    {
      printf("%soperands[%d] = x%d;\n", indent, test->u.opno, depth);

      /* Only allow DT_accept_insn to follow.  */
      if (test->next)
	{
	  test = test->next;
	  gcc_assert (test->type == DT_accept_insn);
	}
    }

  /* Sanity check that we're now at the end of the list of tests.  */
  gcc_assert (!test->next);

  if (test->type == DT_accept_insn)
    {
      switch (subroutine_type)
	{
	case RECOG:
	  if (test->u.insn.num_clobbers_to_add != 0)
	    printf ("%s*pnum_clobbers = %d;\n",
		    indent, test->u.insn.num_clobbers_to_add);
	  printf ("%sreturn %d;  /* %s */\n", indent,
		  test->u.insn.code_number,
		  get_insn_name (test->u.insn.code_number));
	  break;

	case SPLIT:
	  printf ("%sreturn gen_split_%d (insn, operands);\n",
		  indent, test->u.insn.code_number);
	  break;

	case PEEPHOLE2:
	  {
	    int match_len = 0, i;

	    for (i = strlen (p->position) - 1; i >= 0; --i)
	      if (ISUPPER (p->position[i]))
		{
		  match_len = p->position[i] - 'A';
		  break;
		}
	    printf ("%s*_pmatch_len = %d;\n", indent, match_len);
	    printf ("%stem = gen_peephole2_%d (insn, operands);\n",
		    indent, test->u.insn.code_number);
	    printf ("%sif (tem != 0)\n%s  return tem;\n", indent, indent);
	  }
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else
    {
      printf("%sgoto L%d;\n", indent, success->number);
      success->need_label = 1;
    }

  if (want_close)
    fputs ("    }\n", stdout);
}

/* Return 1 if the test is always true and has no fallthru path.  Return -1
   if the test does have a fallthru path, but requires that the condition be
   terminated.  Otherwise return 0 for a normal test.  */
/* ??? is_unconditional is a stupid name for a tri-state function.  */

static int
is_unconditional (struct decision_test *t, enum routine_type subroutine_type)
{
  if (t->type == DT_accept_op)
    return 1;

  if (t->type == DT_accept_insn)
    {
      switch (subroutine_type)
	{
	case RECOG:
	  return (t->u.insn.num_clobbers_to_add == 0);
	case SPLIT:
	  return 1;
	case PEEPHOLE2:
	  return -1;
	default:
	  gcc_unreachable ();
	}
    }

  return 0;
}

/* Emit code for one node -- the conditional and the accompanying action.
   Return true if there is no fallthru path.  */

static int
write_node (struct decision *p, int depth,
	    enum routine_type subroutine_type)
{
  struct decision_test *test, *last_test;
  int uncond;

  /* Scan the tests and simplify comparisons against small
     constants.  */
  for (test = p->tests; test; test = test->next)
    {
      if (test->type == DT_code
	  && test->u.code == CONST_INT
	  && test->next
	  && test->next->type == DT_elt_zero_wide_safe
	  && -MAX_SAVED_CONST_INT <= test->next->u.intval
	  && test->next->u.intval <= MAX_SAVED_CONST_INT)
	{
	  test->type = DT_const_int;
	  test->u.intval = test->next->u.intval;
	  test->next = test->next->next;
	}
    }

  last_test = test = p->tests;
  uncond = is_unconditional (test, subroutine_type);
  if (uncond == 0)
    {
      printf ("  if (");
      write_cond (test, depth, subroutine_type);

      while ((test = test->next) != NULL)
	{
	  last_test = test;
	  if (is_unconditional (test, subroutine_type))
	    break;

	  printf ("\n      && ");
	  write_cond (test, depth, subroutine_type);
	}

      printf (")\n");
    }

  write_action (p, last_test, depth, uncond, p->success.first, subroutine_type);

  return uncond > 0;
}

/* Emit code for all of the sibling nodes of HEAD.  */

static void
write_tree_1 (struct decision_head *head, int depth,
	      enum routine_type subroutine_type)
{
  struct decision *p, *next;
  int uncond = 0;

  for (p = head->first; p ; p = next)
    {
      /* The label for the first element was printed in write_tree.  */
      if (p != head->first && p->need_label)
	OUTPUT_LABEL (" ", p->number);

      /* Attempt to write a switch statement for a whole sequence.  */
      next = write_switch (p, depth);
      if (p != next)
	uncond = 0;
      else
	{
	  /* Failed -- fall back and write one node.  */
	  uncond = write_node (p, depth, subroutine_type);
	  next = p->next;
	}
    }

  /* Finished with this chain.  Close a fallthru path by branching
     to the afterward node.  */
  if (! uncond)
    write_afterward (head->last, head->last->afterward, "  ");
}

/* Write out the decision tree starting at HEAD.  PREVPOS is the
   position at the node that branched to this node.  */

static void
write_tree (struct decision_head *head, const char *prevpos,
	    enum routine_type type, int initial)
{
  struct decision *p = head->first;

  putchar ('\n');
  if (p->need_label)
    OUTPUT_LABEL (" ", p->number);

  if (! initial && p->subroutine_number > 0)
    {
      static const char * const name_prefix[] = {
	  "recog", "split", "peephole2"
      };

      static const char * const call_suffix[] = {
	  ", pnum_clobbers", "", ", _pmatch_len"
      };

      /* This node has been broken out into a separate subroutine.
	 Call it, test the result, and branch accordingly.  */

      if (p->afterward)
	{
	  printf ("  tem = %s_%d (x0, insn%s);\n",
		  name_prefix[type], p->subroutine_number, call_suffix[type]);
	  if (IS_SPLIT (type))
	    printf ("  if (tem != 0)\n    return tem;\n");
	  else
	    printf ("  if (tem >= 0)\n    return tem;\n");

	  change_state (p->position, p->afterward->position, "  ");
	  printf ("  goto L%d;\n", p->afterward->number);
	}
      else
	{
	  printf ("  return %s_%d (x0, insn%s);\n",
		  name_prefix[type], p->subroutine_number, call_suffix[type]);
	}
    }
  else
    {
      int depth = strlen (p->position);

      change_state (prevpos, p->position, "  ");
      write_tree_1 (head, depth, type);

      for (p = head->first; p; p = p->next)
        if (p->success.first)
          write_tree (&p->success, p->position, type, 0);
    }
}

/* Write out a subroutine of type TYPE to do comparisons starting at
   node TREE.  */

static void
write_subroutine (struct decision_head *head, enum routine_type type)
{
  int subfunction = head->first ? head->first->subroutine_number : 0;
  const char *s_or_e;
  char extension[32];
  int i;

  s_or_e = subfunction ? "static " : "";

  if (subfunction)
    sprintf (extension, "_%d", subfunction);
  else if (type == RECOG)
    extension[0] = '\0';
  else
    strcpy (extension, "_insns");

  switch (type)
    {
    case RECOG:
      printf ("%sint\n\
recog%s (rtx x0 ATTRIBUTE_UNUSED,\n\trtx insn ATTRIBUTE_UNUSED,\n\tint *pnum_clobbers ATTRIBUTE_UNUSED)\n", s_or_e, extension);
      break;
    case SPLIT:
      printf ("%srtx\n\
split%s (rtx x0 ATTRIBUTE_UNUSED, rtx insn ATTRIBUTE_UNUSED)\n",
	      s_or_e, extension);
      break;
    case PEEPHOLE2:
      printf ("%srtx\n\
peephole2%s (rtx x0 ATTRIBUTE_UNUSED,\n\trtx insn ATTRIBUTE_UNUSED,\n\tint *_pmatch_len ATTRIBUTE_UNUSED)\n",
	      s_or_e, extension);
      break;
    }

  printf ("{\n  rtx * const operands ATTRIBUTE_UNUSED = &recog_data.operand[0];\n");
  for (i = 1; i <= max_depth; i++)
    printf ("  rtx x%d ATTRIBUTE_UNUSED;\n", i);

  printf ("  %s tem ATTRIBUTE_UNUSED;\n", IS_SPLIT (type) ? "rtx" : "int");

  if (!subfunction)
    printf ("  recog_data.insn = NULL_RTX;\n");

  if (head->first)
    write_tree (head, "", type, 1);
  else
    printf ("  goto ret0;\n");

  printf (" ret0:\n  return %d;\n}\n\n", IS_SPLIT (type) ? 0 : -1);
}

/* In break_out_subroutines, we discovered the boundaries for the
   subroutines, but did not write them out.  Do so now.  */

static void
write_subroutines (struct decision_head *head, enum routine_type type)
{
  struct decision *p;

  for (p = head->first; p ; p = p->next)
    if (p->success.first)
      write_subroutines (&p->success, type);

  if (head->first->subroutine_number > 0)
    write_subroutine (head, type);
}

/* Begin the output file.  */

static void
write_header (void)
{
  puts ("\
/* Generated automatically by the program `genrecog' from the target\n\
   machine description file.  */\n\
\n\
#include \"config.h\"\n\
#include \"system.h\"\n\
#include \"coretypes.h\"\n\
#include \"tm.h\"\n\
#include \"rtl.h\"\n\
#include \"tm_p.h\"\n\
#include \"function.h\"\n\
#include \"insn-config.h\"\n\
#include \"recog.h\"\n\
#include \"real.h\"\n\
#include \"output.h\"\n\
#include \"flags.h\"\n\
#include \"hard-reg-set.h\"\n\
#include \"resource.h\"\n\
#include \"toplev.h\"\n\
#include \"reload.h\"\n\
#include \"tm-constrs.h\"\n\
\n");

  puts ("\n\
/* `recog' contains a decision tree that recognizes whether the rtx\n\
   X0 is a valid instruction.\n\
\n\
   recog returns -1 if the rtx is not valid.  If the rtx is valid, recog\n\
   returns a nonnegative number which is the insn code number for the\n\
   pattern that matched.  This is the same as the order in the machine\n\
   description of the entry that matched.  This number can be used as an\n\
   index into `insn_data' and other tables.\n");
  puts ("\
   The third argument to recog is an optional pointer to an int.  If\n\
   present, recog will accept a pattern if it matches except for missing\n\
   CLOBBER expressions at the end.  In that case, the value pointed to by\n\
   the optional pointer will be set to the number of CLOBBERs that need\n\
   to be added (it should be initialized to zero by the caller).  If it");
  puts ("\
   is set nonzero, the caller should allocate a PARALLEL of the\n\
   appropriate size, copy the initial entries, and call add_clobbers\n\
   (found in insn-emit.c) to fill in the CLOBBERs.\n\
");

  puts ("\n\
   The function split_insns returns 0 if the rtl could not\n\
   be split or the split rtl as an INSN list if it can be.\n\
\n\
   The function peephole2_insns returns 0 if the rtl could not\n\
   be matched. If there was a match, the new rtl is returned in an INSN list,\n\
   and LAST_INSN will point to the last recognized insn in the old sequence.\n\
*/\n\n");
}


/* Construct and return a sequence of decisions
   that will recognize INSN.

   TYPE says what type of routine we are recognizing (RECOG or SPLIT).  */

static struct decision_head
make_insn_sequence (rtx insn, enum routine_type type)
{
  rtx x;
  const char *c_test = XSTR (insn, type == RECOG ? 2 : 1);
  int truth = maybe_eval_c_test (c_test);
  struct decision *last;
  struct decision_test *test, **place;
  struct decision_head head;
  char c_test_pos[2];

  /* We should never see an insn whose C test is false at compile time.  */
  gcc_assert (truth);

  c_test_pos[0] = '\0';
  if (type == PEEPHOLE2)
    {
      int i, j;

      /* peephole2 gets special treatment:
	 - X always gets an outer parallel even if it's only one entry
	 - we remove all traces of outer-level match_scratch and match_dup
           expressions here.  */
      x = rtx_alloc (PARALLEL);
      PUT_MODE (x, VOIDmode);
      XVEC (x, 0) = rtvec_alloc (XVECLEN (insn, 0));
      for (i = j = 0; i < XVECLEN (insn, 0); i++)
	{
	  rtx tmp = XVECEXP (insn, 0, i);
	  if (GET_CODE (tmp) != MATCH_SCRATCH && GET_CODE (tmp) != MATCH_DUP)
	    {
	      XVECEXP (x, 0, j) = tmp;
	      j++;
	    }
	}
      XVECLEN (x, 0) = j;

      c_test_pos[0] = 'A' + j - 1;
      c_test_pos[1] = '\0';
    }
  else if (XVECLEN (insn, type == RECOG) == 1)
    x = XVECEXP (insn, type == RECOG, 0);
  else
    {
      x = rtx_alloc (PARALLEL);
      XVEC (x, 0) = XVEC (insn, type == RECOG);
      PUT_MODE (x, VOIDmode);
    }

  validate_pattern (x, insn, NULL_RTX, 0);

  memset(&head, 0, sizeof(head));
  last = add_to_sequence (x, &head, "", type, 1);

  /* Find the end of the test chain on the last node.  */
  for (test = last->tests; test->next; test = test->next)
    continue;
  place = &test->next;

  /* Skip the C test if it's known to be true at compile time.  */
  if (truth == -1)
    {
      /* Need a new node if we have another test to add.  */
      if (test->type == DT_accept_op)
	{
	  last = new_decision (c_test_pos, &last->success);
	  place = &last->tests;
	}
      test = new_decision_test (DT_c_test, &place);
      test->u.c_test = c_test;
    }

  test = new_decision_test (DT_accept_insn, &place);
  test->u.insn.code_number = next_insn_code;
  test->u.insn.lineno = pattern_lineno;
  test->u.insn.num_clobbers_to_add = 0;

  switch (type)
    {
    case RECOG:
      /* If this is a DEFINE_INSN and X is a PARALLEL, see if it ends
	 with a group of CLOBBERs of (hard) registers or MATCH_SCRATCHes.
	 If so, set up to recognize the pattern without these CLOBBERs.  */

      if (GET_CODE (x) == PARALLEL)
	{
	  int i;

	  /* Find the last non-clobber in the parallel.  */
	  for (i = XVECLEN (x, 0); i > 0; i--)
	    {
	      rtx y = XVECEXP (x, 0, i - 1);
	      if (GET_CODE (y) != CLOBBER
		  || (!REG_P (XEXP (y, 0))
		      && GET_CODE (XEXP (y, 0)) != MATCH_SCRATCH))
		break;
	    }

	  if (i != XVECLEN (x, 0))
	    {
	      rtx new;
	      struct decision_head clobber_head;

	      /* Build a similar insn without the clobbers.  */
	      if (i == 1)
		new = XVECEXP (x, 0, 0);
	      else
		{
		  int j;

		  new = rtx_alloc (PARALLEL);
		  XVEC (new, 0) = rtvec_alloc (i);
		  for (j = i - 1; j >= 0; j--)
		    XVECEXP (new, 0, j) = XVECEXP (x, 0, j);
		}

	      /* Recognize it.  */
	      memset (&clobber_head, 0, sizeof(clobber_head));
	      last = add_to_sequence (new, &clobber_head, "", type, 1);

	      /* Find the end of the test chain on the last node.  */
	      for (test = last->tests; test->next; test = test->next)
		continue;

	      /* We definitely have a new test to add -- create a new
		 node if needed.  */
	      place = &test->next;
	      if (test->type == DT_accept_op)
		{
		  last = new_decision ("", &last->success);
		  place = &last->tests;
		}

	      /* Skip the C test if it's known to be true at compile
                 time.  */
	      if (truth == -1)
		{
		  test = new_decision_test (DT_c_test, &place);
		  test->u.c_test = c_test;
		}

	      test = new_decision_test (DT_accept_insn, &place);
	      test->u.insn.code_number = next_insn_code;
	      test->u.insn.lineno = pattern_lineno;
	      test->u.insn.num_clobbers_to_add = XVECLEN (x, 0) - i;

	      merge_trees (&head, &clobber_head);
	    }
	}
      break;

    case SPLIT:
      /* Define the subroutine we will call below and emit in genemit.  */
      printf ("extern rtx gen_split_%d (rtx, rtx *);\n", next_insn_code);
      break;

    case PEEPHOLE2:
      /* Define the subroutine we will call below and emit in genemit.  */
      printf ("extern rtx gen_peephole2_%d (rtx, rtx *);\n",
	      next_insn_code);
      break;
    }

  return head;
}

static void
process_tree (struct decision_head *head, enum routine_type subroutine_type)
{
  if (head->first == NULL)
    {
      /* We can elide peephole2_insns, but not recog or split_insns.  */
      if (subroutine_type == PEEPHOLE2)
	return;
    }
  else
    {
      factor_tests (head);

      next_subroutine_number = 0;
      break_out_subroutines (head, 1);
      find_afterward (head, NULL);

      /* We run this after find_afterward, because find_afterward needs
	 the redundant DT_mode tests on predicates to determine whether
	 two tests can both be true or not.  */
      simplify_tests(head);

      write_subroutines (head, subroutine_type);
    }

  write_subroutine (head, subroutine_type);
}

extern int main (int, char **);

int
main (int argc, char **argv)
{
  rtx desc;
  struct decision_head recog_tree, split_tree, peephole2_tree, h;

  progname = "genrecog";

  memset (&recog_tree, 0, sizeof recog_tree);
  memset (&split_tree, 0, sizeof split_tree);
  memset (&peephole2_tree, 0, sizeof peephole2_tree);

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  next_insn_code = 0;

  write_header ();

  /* Read the machine description.  */

  while (1)
    {
      desc = read_md_rtx (&pattern_lineno, &next_insn_code);
      if (desc == NULL)
	break;

      switch (GET_CODE (desc))
	{
	case DEFINE_PREDICATE:
	case DEFINE_SPECIAL_PREDICATE:
	  process_define_predicate (desc);
	  break;

	case DEFINE_INSN:
	  h = make_insn_sequence (desc, RECOG);
	  merge_trees (&recog_tree, &h);
	  break;

	case DEFINE_SPLIT:
	  h = make_insn_sequence (desc, SPLIT);
	  merge_trees (&split_tree, &h);
	  break;

	case DEFINE_PEEPHOLE2:
	  h = make_insn_sequence (desc, PEEPHOLE2);
	  merge_trees (&peephole2_tree, &h);

	default:
	  /* do nothing */;
	}
    }

  if (error_count || have_error)
    return FATAL_EXIT_CODE;

  puts ("\n\n");

  process_tree (&recog_tree, RECOG);
  process_tree (&split_tree, SPLIT);
  process_tree (&peephole2_tree, PEEPHOLE2);

  fflush (stdout);
  return (ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}

static void
debug_decision_2 (struct decision_test *test)
{
  switch (test->type)
    {
    case DT_num_insns:
      fprintf (stderr, "num_insns=%d", test->u.num_insns);
      break;
    case DT_mode:
      fprintf (stderr, "mode=%s", GET_MODE_NAME (test->u.mode));
      break;
    case DT_code:
      fprintf (stderr, "code=%s", GET_RTX_NAME (test->u.code));
      break;
    case DT_veclen:
      fprintf (stderr, "veclen=%d", test->u.veclen);
      break;
    case DT_elt_zero_int:
      fprintf (stderr, "elt0_i=%d", (int) test->u.intval);
      break;
    case DT_elt_one_int:
      fprintf (stderr, "elt1_i=%d", (int) test->u.intval);
      break;
    case DT_elt_zero_wide:
      fprintf (stderr, "elt0_w=" HOST_WIDE_INT_PRINT_DEC, test->u.intval);
      break;
    case DT_elt_zero_wide_safe:
      fprintf (stderr, "elt0_ws=" HOST_WIDE_INT_PRINT_DEC, test->u.intval);
      break;
    case DT_veclen_ge:
      fprintf (stderr, "veclen>=%d", test->u.veclen);
      break;
    case DT_dup:
      fprintf (stderr, "dup=%d", test->u.dup);
      break;
    case DT_pred:
      fprintf (stderr, "pred=(%s,%s)",
	       test->u.pred.name, GET_MODE_NAME(test->u.pred.mode));
      break;
    case DT_c_test:
      {
	char sub[16+4];
	strncpy (sub, test->u.c_test, sizeof(sub));
	memcpy (sub+16, "...", 4);
	fprintf (stderr, "c_test=\"%s\"", sub);
      }
      break;
    case DT_accept_op:
      fprintf (stderr, "A_op=%d", test->u.opno);
      break;
    case DT_accept_insn:
      fprintf (stderr, "A_insn=(%d,%d)",
	       test->u.insn.code_number, test->u.insn.num_clobbers_to_add);
      break;

    default:
      gcc_unreachable ();
    }
}

static void
debug_decision_1 (struct decision *d, int indent)
{
  int i;
  struct decision_test *test;

  if (d == NULL)
    {
      for (i = 0; i < indent; ++i)
	putc (' ', stderr);
      fputs ("(nil)\n", stderr);
      return;
    }

  for (i = 0; i < indent; ++i)
    putc (' ', stderr);

  putc ('{', stderr);
  test = d->tests;
  if (test)
    {
      debug_decision_2 (test);
      while ((test = test->next) != NULL)
	{
	  fputs (" + ", stderr);
	  debug_decision_2 (test);
	}
    }
  fprintf (stderr, "} %d n %d a %d\n", d->number,
	   (d->next ? d->next->number : -1),
	   (d->afterward ? d->afterward->number : -1));
}

static void
debug_decision_0 (struct decision *d, int indent, int maxdepth)
{
  struct decision *n;
  int i;

  if (maxdepth < 0)
    return;
  if (d == NULL)
    {
      for (i = 0; i < indent; ++i)
	putc (' ', stderr);
      fputs ("(nil)\n", stderr);
      return;
    }

  debug_decision_1 (d, indent);
  for (n = d->success.first; n ; n = n->next)
    debug_decision_0 (n, indent + 2, maxdepth - 1);
}

void
debug_decision (struct decision *d)
{
  debug_decision_0 (d, 0, 1000000);
}

void
debug_decision_list (struct decision *d)
{
  while (d)
    {
      debug_decision_0 (d, 0, 0);
      d = d->next;
    }
}
