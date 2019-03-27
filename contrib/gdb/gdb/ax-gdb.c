/* GDB-specific functions for operating on agent expressions.

   Copyright 1998, 1999, 2000, 2001, 2003 Free Software Foundation,
   Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "symtab.h"
#include "symfile.h"
#include "gdbtypes.h"
#include "value.h"
#include "expression.h"
#include "command.h"
#include "gdbcmd.h"
#include "frame.h"
#include "target.h"
#include "ax.h"
#include "ax-gdb.h"
#include "gdb_string.h"
#include "block.h"
#include "regcache.h"

/* To make sense of this file, you should read doc/agentexpr.texi.
   Then look at the types and enums in ax-gdb.h.  For the code itself,
   look at gen_expr, towards the bottom; that's the main function that
   looks at the GDB expressions and calls everything else to generate
   code.

   I'm beginning to wonder whether it wouldn't be nicer to internally
   generate trees, with types, and then spit out the bytecode in
   linear form afterwards; we could generate fewer `swap', `ext', and
   `zero_ext' bytecodes that way; it would make good constant folding
   easier, too.  But at the moment, I think we should be willing to
   pay for the simplicity of this code with less-than-optimal bytecode
   strings.

   Remember, "GBD" stands for "Great Britain, Dammit!"  So be careful.  */



/* Prototypes for local functions. */

/* There's a standard order to the arguments of these functions:
   union exp_element ** --- pointer into expression
   struct agent_expr * --- agent expression buffer to generate code into
   struct axs_value * --- describes value left on top of stack  */

static struct value *const_var_ref (struct symbol *var);
static struct value *const_expr (union exp_element **pc);
static struct value *maybe_const_expr (union exp_element **pc);

static void gen_traced_pop (struct agent_expr *, struct axs_value *);

static void gen_sign_extend (struct agent_expr *, struct type *);
static void gen_extend (struct agent_expr *, struct type *);
static void gen_fetch (struct agent_expr *, struct type *);
static void gen_left_shift (struct agent_expr *, int);


static void gen_frame_args_address (struct agent_expr *);
static void gen_frame_locals_address (struct agent_expr *);
static void gen_offset (struct agent_expr *ax, int offset);
static void gen_sym_offset (struct agent_expr *, struct symbol *);
static void gen_var_ref (struct agent_expr *ax,
			 struct axs_value *value, struct symbol *var);


static void gen_int_literal (struct agent_expr *ax,
			     struct axs_value *value,
			     LONGEST k, struct type *type);


static void require_rvalue (struct agent_expr *ax, struct axs_value *value);
static void gen_usual_unary (struct agent_expr *ax, struct axs_value *value);
static int type_wider_than (struct type *type1, struct type *type2);
static struct type *max_type (struct type *type1, struct type *type2);
static void gen_conversion (struct agent_expr *ax,
			    struct type *from, struct type *to);
static int is_nontrivial_conversion (struct type *from, struct type *to);
static void gen_usual_arithmetic (struct agent_expr *ax,
				  struct axs_value *value1,
				  struct axs_value *value2);
static void gen_integral_promotions (struct agent_expr *ax,
				     struct axs_value *value);
static void gen_cast (struct agent_expr *ax,
		      struct axs_value *value, struct type *type);
static void gen_scale (struct agent_expr *ax,
		       enum agent_op op, struct type *type);
static void gen_add (struct agent_expr *ax,
		     struct axs_value *value,
		     struct axs_value *value1,
		     struct axs_value *value2, char *name);
static void gen_sub (struct agent_expr *ax,
		     struct axs_value *value,
		     struct axs_value *value1, struct axs_value *value2);
static void gen_binop (struct agent_expr *ax,
		       struct axs_value *value,
		       struct axs_value *value1,
		       struct axs_value *value2,
		       enum agent_op op,
		       enum agent_op op_unsigned, int may_carry, char *name);
static void gen_logical_not (struct agent_expr *ax, struct axs_value *value);
static void gen_complement (struct agent_expr *ax, struct axs_value *value);
static void gen_deref (struct agent_expr *, struct axs_value *);
static void gen_address_of (struct agent_expr *, struct axs_value *);
static int find_field (struct type *type, char *name);
static void gen_bitfield_ref (struct agent_expr *ax,
			      struct axs_value *value,
			      struct type *type, int start, int end);
static void gen_struct_ref (struct agent_expr *ax,
			    struct axs_value *value,
			    char *field,
			    char *operator_name, char *operand_name);
static void gen_repeat (union exp_element **pc,
			struct agent_expr *ax, struct axs_value *value);
static void gen_sizeof (union exp_element **pc,
			struct agent_expr *ax, struct axs_value *value);
static void gen_expr (union exp_element **pc,
		      struct agent_expr *ax, struct axs_value *value);

static void agent_command (char *exp, int from_tty);


/* Detecting constant expressions.  */

/* If the variable reference at *PC is a constant, return its value.
   Otherwise, return zero.

   Hey, Wally!  How can a variable reference be a constant?

   Well, Beav, this function really handles the OP_VAR_VALUE operator,
   not specifically variable references.  GDB uses OP_VAR_VALUE to
   refer to any kind of symbolic reference: function names, enum
   elements, and goto labels are all handled through the OP_VAR_VALUE
   operator, even though they're constants.  It makes sense given the
   situation.

   Gee, Wally, don'cha wonder sometimes if data representations that
   subvert commonly accepted definitions of terms in favor of heavily
   context-specific interpretations are really just a tool of the
   programming hegemony to preserve their power and exclude the
   proletariat?  */

static struct value *
const_var_ref (struct symbol *var)
{
  struct type *type = SYMBOL_TYPE (var);

  switch (SYMBOL_CLASS (var))
    {
    case LOC_CONST:
      return value_from_longest (type, (LONGEST) SYMBOL_VALUE (var));

    case LOC_LABEL:
      return value_from_pointer (type, (CORE_ADDR) SYMBOL_VALUE_ADDRESS (var));

    default:
      return 0;
    }
}


/* If the expression starting at *PC has a constant value, return it.
   Otherwise, return zero.  If we return a value, then *PC will be
   advanced to the end of it.  If we return zero, *PC could be
   anywhere.  */
static struct value *
const_expr (union exp_element **pc)
{
  enum exp_opcode op = (*pc)->opcode;
  struct value *v1;

  switch (op)
    {
    case OP_LONG:
      {
	struct type *type = (*pc)[1].type;
	LONGEST k = (*pc)[2].longconst;
	(*pc) += 4;
	return value_from_longest (type, k);
      }

    case OP_VAR_VALUE:
      {
	struct value *v = const_var_ref ((*pc)[2].symbol);
	(*pc) += 4;
	return v;
      }

      /* We could add more operators in here.  */

    case UNOP_NEG:
      (*pc)++;
      v1 = const_expr (pc);
      if (v1)
	return value_neg (v1);
      else
	return 0;

    default:
      return 0;
    }
}


/* Like const_expr, but guarantee also that *PC is undisturbed if the
   expression is not constant.  */
static struct value *
maybe_const_expr (union exp_element **pc)
{
  union exp_element *tentative_pc = *pc;
  struct value *v = const_expr (&tentative_pc);

  /* If we got a value, then update the real PC.  */
  if (v)
    *pc = tentative_pc;

  return v;
}


/* Generating bytecode from GDB expressions: general assumptions */

/* Here are a few general assumptions made throughout the code; if you
   want to make a change that contradicts one of these, then you'd
   better scan things pretty thoroughly.

   - We assume that all values occupy one stack element.  For example,
   sometimes we'll swap to get at the left argument to a binary
   operator.  If we decide that void values should occupy no stack
   elements, or that synthetic arrays (whose size is determined at
   run time, created by the `@' operator) should occupy two stack
   elements (address and length), then this will cause trouble.

   - We assume the stack elements are infinitely wide, and that we
   don't have to worry what happens if the user requests an
   operation that is wider than the actual interpreter's stack.
   That is, it's up to the interpreter to handle directly all the
   integer widths the user has access to.  (Woe betide the language
   with bignums!)

   - We don't support side effects.  Thus, we don't have to worry about
   GCC's generalized lvalues, function calls, etc.

   - We don't support floating point.  Many places where we switch on
   some type don't bother to include cases for floating point; there
   may be even more subtle ways this assumption exists.  For
   example, the arguments to % must be integers.

   - We assume all subexpressions have a static, unchanging type.  If
   we tried to support convenience variables, this would be a
   problem.

   - All values on the stack should always be fully zero- or
   sign-extended.

   (I wasn't sure whether to choose this or its opposite --- that
   only addresses are assumed extended --- but it turns out that
   neither convention completely eliminates spurious extend
   operations (if everything is always extended, then you have to
   extend after add, because it could overflow; if nothing is
   extended, then you end up producing extends whenever you change
   sizes), and this is simpler.)  */


/* Generating bytecode from GDB expressions: the `trace' kludge  */

/* The compiler in this file is a general-purpose mechanism for
   translating GDB expressions into bytecode.  One ought to be able to
   find a million and one uses for it.

   However, at the moment it is HOPELESSLY BRAIN-DAMAGED for the sake
   of expediency.  Let he who is without sin cast the first stone.

   For the data tracing facility, we need to insert `trace' bytecodes
   before each data fetch; this records all the memory that the
   expression touches in the course of evaluation, so that memory will
   be available when the user later tries to evaluate the expression
   in GDB.

   This should be done (I think) in a post-processing pass, that walks
   an arbitrary agent expression and inserts `trace' operations at the
   appropriate points.  But it's much faster to just hack them
   directly into the code.  And since we're in a crunch, that's what
   I've done.

   Setting the flag trace_kludge to non-zero enables the code that
   emits the trace bytecodes at the appropriate points.  */
static int trace_kludge;

/* Trace the lvalue on the stack, if it needs it.  In either case, pop
   the value.  Useful on the left side of a comma, and at the end of
   an expression being used for tracing.  */
static void
gen_traced_pop (struct agent_expr *ax, struct axs_value *value)
{
  if (trace_kludge)
    switch (value->kind)
      {
      case axs_rvalue:
	/* We don't trace rvalues, just the lvalues necessary to
	   produce them.  So just dispose of this value.  */
	ax_simple (ax, aop_pop);
	break;

      case axs_lvalue_memory:
	{
	  int length = TYPE_LENGTH (value->type);

	  /* There's no point in trying to use a trace_quick bytecode
	     here, since "trace_quick SIZE pop" is three bytes, whereas
	     "const8 SIZE trace" is also three bytes, does the same
	     thing, and the simplest code which generates that will also
	     work correctly for objects with large sizes.  */
	  ax_const_l (ax, length);
	  ax_simple (ax, aop_trace);
	}
	break;

      case axs_lvalue_register:
	/* We need to mention the register somewhere in the bytecode,
	   so ax_reqs will pick it up and add it to the mask of
	   registers used.  */
	ax_reg (ax, value->u.reg);
	ax_simple (ax, aop_pop);
	break;
      }
  else
    /* If we're not tracing, just pop the value.  */
    ax_simple (ax, aop_pop);
}



/* Generating bytecode from GDB expressions: helper functions */

/* Assume that the lower bits of the top of the stack is a value of
   type TYPE, and the upper bits are zero.  Sign-extend if necessary.  */
static void
gen_sign_extend (struct agent_expr *ax, struct type *type)
{
  /* Do we need to sign-extend this?  */
  if (!TYPE_UNSIGNED (type))
    ax_ext (ax, TYPE_LENGTH (type) * TARGET_CHAR_BIT);
}


/* Assume the lower bits of the top of the stack hold a value of type
   TYPE, and the upper bits are garbage.  Sign-extend or truncate as
   needed.  */
static void
gen_extend (struct agent_expr *ax, struct type *type)
{
  int bits = TYPE_LENGTH (type) * TARGET_CHAR_BIT;
  /* I just had to.  */
  ((TYPE_UNSIGNED (type) ? ax_zero_ext : ax_ext) (ax, bits));
}


/* Assume that the top of the stack contains a value of type "pointer
   to TYPE"; generate code to fetch its value.  Note that TYPE is the
   target type, not the pointer type.  */
static void
gen_fetch (struct agent_expr *ax, struct type *type)
{
  if (trace_kludge)
    {
      /* Record the area of memory we're about to fetch.  */
      ax_trace_quick (ax, TYPE_LENGTH (type));
    }

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_PTR:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_INT:
    case TYPE_CODE_CHAR:
      /* It's a scalar value, so we know how to dereference it.  How
         many bytes long is it?  */
      switch (TYPE_LENGTH (type))
	{
	case 8 / TARGET_CHAR_BIT:
	  ax_simple (ax, aop_ref8);
	  break;
	case 16 / TARGET_CHAR_BIT:
	  ax_simple (ax, aop_ref16);
	  break;
	case 32 / TARGET_CHAR_BIT:
	  ax_simple (ax, aop_ref32);
	  break;
	case 64 / TARGET_CHAR_BIT:
	  ax_simple (ax, aop_ref64);
	  break;

	  /* Either our caller shouldn't have asked us to dereference
	     that pointer (other code's fault), or we're not
	     implementing something we should be (this code's fault).
	     In any case, it's a bug the user shouldn't see.  */
	default:
	  internal_error (__FILE__, __LINE__,
			  "gen_fetch: strange size");
	}

      gen_sign_extend (ax, type);
      break;

    default:
      /* Either our caller shouldn't have asked us to dereference that
         pointer (other code's fault), or we're not implementing
         something we should be (this code's fault).  In any case,
         it's a bug the user shouldn't see.  */
      internal_error (__FILE__, __LINE__,
		      "gen_fetch: bad type code");
    }
}


/* Generate code to left shift the top of the stack by DISTANCE bits, or
   right shift it by -DISTANCE bits if DISTANCE < 0.  This generates
   unsigned (logical) right shifts.  */
static void
gen_left_shift (struct agent_expr *ax, int distance)
{
  if (distance > 0)
    {
      ax_const_l (ax, distance);
      ax_simple (ax, aop_lsh);
    }
  else if (distance < 0)
    {
      ax_const_l (ax, -distance);
      ax_simple (ax, aop_rsh_unsigned);
    }
}



/* Generating bytecode from GDB expressions: symbol references */

/* Generate code to push the base address of the argument portion of
   the top stack frame.  */
static void
gen_frame_args_address (struct agent_expr *ax)
{
  int frame_reg;
  LONGEST frame_offset;

  TARGET_VIRTUAL_FRAME_POINTER (ax->scope, &frame_reg, &frame_offset);
  ax_reg (ax, frame_reg);
  gen_offset (ax, frame_offset);
}


/* Generate code to push the base address of the locals portion of the
   top stack frame.  */
static void
gen_frame_locals_address (struct agent_expr *ax)
{
  int frame_reg;
  LONGEST frame_offset;

  TARGET_VIRTUAL_FRAME_POINTER (ax->scope, &frame_reg, &frame_offset);
  ax_reg (ax, frame_reg);
  gen_offset (ax, frame_offset);
}


/* Generate code to add OFFSET to the top of the stack.  Try to
   generate short and readable code.  We use this for getting to
   variables on the stack, and structure members.  If we were
   programming in ML, it would be clearer why these are the same
   thing.  */
static void
gen_offset (struct agent_expr *ax, int offset)
{
  /* It would suffice to simply push the offset and add it, but this
     makes it easier to read positive and negative offsets in the
     bytecode.  */
  if (offset > 0)
    {
      ax_const_l (ax, offset);
      ax_simple (ax, aop_add);
    }
  else if (offset < 0)
    {
      ax_const_l (ax, -offset);
      ax_simple (ax, aop_sub);
    }
}


/* In many cases, a symbol's value is the offset from some other
   address (stack frame, base register, etc.)  Generate code to add
   VAR's value to the top of the stack.  */
static void
gen_sym_offset (struct agent_expr *ax, struct symbol *var)
{
  gen_offset (ax, SYMBOL_VALUE (var));
}


/* Generate code for a variable reference to AX.  The variable is the
   symbol VAR.  Set VALUE to describe the result.  */

static void
gen_var_ref (struct agent_expr *ax, struct axs_value *value, struct symbol *var)
{
  /* Dereference any typedefs. */
  value->type = check_typedef (SYMBOL_TYPE (var));

  /* I'm imitating the code in read_var_value.  */
  switch (SYMBOL_CLASS (var))
    {
    case LOC_CONST:		/* A constant, like an enum value.  */
      ax_const_l (ax, (LONGEST) SYMBOL_VALUE (var));
      value->kind = axs_rvalue;
      break;

    case LOC_LABEL:		/* A goto label, being used as a value.  */
      ax_const_l (ax, (LONGEST) SYMBOL_VALUE_ADDRESS (var));
      value->kind = axs_rvalue;
      break;

    case LOC_CONST_BYTES:
      internal_error (__FILE__, __LINE__,
		      "gen_var_ref: LOC_CONST_BYTES symbols are not supported");

      /* Variable at a fixed location in memory.  Easy.  */
    case LOC_STATIC:
      /* Push the address of the variable.  */
      ax_const_l (ax, SYMBOL_VALUE_ADDRESS (var));
      value->kind = axs_lvalue_memory;
      break;

    case LOC_ARG:		/* var lives in argument area of frame */
      gen_frame_args_address (ax);
      gen_sym_offset (ax, var);
      value->kind = axs_lvalue_memory;
      break;

    case LOC_REF_ARG:		/* As above, but the frame slot really
				   holds the address of the variable.  */
      gen_frame_args_address (ax);
      gen_sym_offset (ax, var);
      /* Don't assume any particular pointer size.  */
      gen_fetch (ax, lookup_pointer_type (builtin_type_void));
      value->kind = axs_lvalue_memory;
      break;

    case LOC_LOCAL:		/* var lives in locals area of frame */
    case LOC_LOCAL_ARG:
      gen_frame_locals_address (ax);
      gen_sym_offset (ax, var);
      value->kind = axs_lvalue_memory;
      break;

    case LOC_BASEREG:		/* relative to some base register */
    case LOC_BASEREG_ARG:
      ax_reg (ax, SYMBOL_BASEREG (var));
      gen_sym_offset (ax, var);
      value->kind = axs_lvalue_memory;
      break;

    case LOC_TYPEDEF:
      error ("Cannot compute value of typedef `%s'.",
	     SYMBOL_PRINT_NAME (var));
      break;

    case LOC_BLOCK:
      ax_const_l (ax, BLOCK_START (SYMBOL_BLOCK_VALUE (var)));
      value->kind = axs_rvalue;
      break;

    case LOC_REGISTER:
    case LOC_REGPARM:
      /* Don't generate any code at all; in the process of treating
         this as an lvalue or rvalue, the caller will generate the
         right code.  */
      value->kind = axs_lvalue_register;
      value->u.reg = SYMBOL_VALUE (var);
      break;

      /* A lot like LOC_REF_ARG, but the pointer lives directly in a
         register, not on the stack.  Simpler than LOC_REGISTER and
         LOC_REGPARM, because it's just like any other case where the
         thing has a real address.  */
    case LOC_REGPARM_ADDR:
      ax_reg (ax, SYMBOL_VALUE (var));
      value->kind = axs_lvalue_memory;
      break;

    case LOC_UNRESOLVED:
      {
	struct minimal_symbol *msym
	= lookup_minimal_symbol (DEPRECATED_SYMBOL_NAME (var), NULL, NULL);
	if (!msym)
	  error ("Couldn't resolve symbol `%s'.", SYMBOL_PRINT_NAME (var));

	/* Push the address of the variable.  */
	ax_const_l (ax, SYMBOL_VALUE_ADDRESS (msym));
	value->kind = axs_lvalue_memory;
      }
      break;

    case LOC_COMPUTED:
    case LOC_COMPUTED_ARG:
      /* FIXME: cagney/2004-01-26: It should be possible to
	 unconditionally call the SYMBOL_OPS method when available.
	 Unfortunately DWARF 2 stores the frame-base (instead of the
	 function) location in a function's symbol.  Oops!  For the
	 moment enable this when/where applicable.  */
      SYMBOL_OPS (var)->tracepoint_var_ref (var, ax, value);
      break;

    case LOC_OPTIMIZED_OUT:
      error ("The variable `%s' has been optimized out.",
	     SYMBOL_PRINT_NAME (var));
      break;

    default:
      error ("Cannot find value of botched symbol `%s'.",
	     SYMBOL_PRINT_NAME (var));
      break;
    }
}



/* Generating bytecode from GDB expressions: literals */

static void
gen_int_literal (struct agent_expr *ax, struct axs_value *value, LONGEST k,
		 struct type *type)
{
  ax_const_l (ax, k);
  value->kind = axs_rvalue;
  value->type = type;
}



/* Generating bytecode from GDB expressions: unary conversions, casts */

/* Take what's on the top of the stack (as described by VALUE), and
   try to make an rvalue out of it.  Signal an error if we can't do
   that.  */
static void
require_rvalue (struct agent_expr *ax, struct axs_value *value)
{
  switch (value->kind)
    {
    case axs_rvalue:
      /* It's already an rvalue.  */
      break;

    case axs_lvalue_memory:
      /* The top of stack is the address of the object.  Dereference.  */
      gen_fetch (ax, value->type);
      break;

    case axs_lvalue_register:
      /* There's nothing on the stack, but value->u.reg is the
         register number containing the value.

         When we add floating-point support, this is going to have to
         change.  What about SPARC register pairs, for example?  */
      ax_reg (ax, value->u.reg);
      gen_extend (ax, value->type);
      break;
    }

  value->kind = axs_rvalue;
}


/* Assume the top of the stack is described by VALUE, and perform the
   usual unary conversions.  This is motivated by ANSI 6.2.2, but of
   course GDB expressions are not ANSI; they're the mishmash union of
   a bunch of languages.  Rah.

   NOTE!  This function promises to produce an rvalue only when the
   incoming value is of an appropriate type.  In other words, the
   consumer of the value this function produces may assume the value
   is an rvalue only after checking its type.

   The immediate issue is that if the user tries to use a structure or
   union as an operand of, say, the `+' operator, we don't want to try
   to convert that structure to an rvalue; require_rvalue will bomb on
   structs and unions.  Rather, we want to simply pass the struct
   lvalue through unchanged, and let `+' raise an error.  */

static void
gen_usual_unary (struct agent_expr *ax, struct axs_value *value)
{
  /* We don't have to generate any code for the usual integral
     conversions, since values are always represented as full-width on
     the stack.  Should we tweak the type?  */

  /* Some types require special handling.  */
  switch (TYPE_CODE (value->type))
    {
      /* Functions get converted to a pointer to the function.  */
    case TYPE_CODE_FUNC:
      value->type = lookup_pointer_type (value->type);
      value->kind = axs_rvalue;	/* Should always be true, but just in case.  */
      break;

      /* Arrays get converted to a pointer to their first element, and
         are no longer an lvalue.  */
    case TYPE_CODE_ARRAY:
      {
	struct type *elements = TYPE_TARGET_TYPE (value->type);
	value->type = lookup_pointer_type (elements);
	value->kind = axs_rvalue;
	/* We don't need to generate any code; the address of the array
	   is also the address of its first element.  */
      }
      break;

      /* Don't try to convert structures and unions to rvalues.  Let the
         consumer signal an error.  */
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      return;

      /* If the value is an enum, call it an integer.  */
    case TYPE_CODE_ENUM:
      value->type = builtin_type_int;
      break;
    }

  /* If the value is an lvalue, dereference it.  */
  require_rvalue (ax, value);
}


/* Return non-zero iff the type TYPE1 is considered "wider" than the
   type TYPE2, according to the rules described in gen_usual_arithmetic.  */
static int
type_wider_than (struct type *type1, struct type *type2)
{
  return (TYPE_LENGTH (type1) > TYPE_LENGTH (type2)
	  || (TYPE_LENGTH (type1) == TYPE_LENGTH (type2)
	      && TYPE_UNSIGNED (type1)
	      && !TYPE_UNSIGNED (type2)));
}


/* Return the "wider" of the two types TYPE1 and TYPE2.  */
static struct type *
max_type (struct type *type1, struct type *type2)
{
  return type_wider_than (type1, type2) ? type1 : type2;
}


/* Generate code to convert a scalar value of type FROM to type TO.  */
static void
gen_conversion (struct agent_expr *ax, struct type *from, struct type *to)
{
  /* Perhaps there is a more graceful way to state these rules.  */

  /* If we're converting to a narrower type, then we need to clear out
     the upper bits.  */
  if (TYPE_LENGTH (to) < TYPE_LENGTH (from))
    gen_extend (ax, from);

  /* If the two values have equal width, but different signednesses,
     then we need to extend.  */
  else if (TYPE_LENGTH (to) == TYPE_LENGTH (from))
    {
      if (TYPE_UNSIGNED (from) != TYPE_UNSIGNED (to))
	gen_extend (ax, to);
    }

  /* If we're converting to a wider type, and becoming unsigned, then
     we need to zero out any possible sign bits.  */
  else if (TYPE_LENGTH (to) > TYPE_LENGTH (from))
    {
      if (TYPE_UNSIGNED (to))
	gen_extend (ax, to);
    }
}


/* Return non-zero iff the type FROM will require any bytecodes to be
   emitted to be converted to the type TO.  */
static int
is_nontrivial_conversion (struct type *from, struct type *to)
{
  struct agent_expr *ax = new_agent_expr (0);
  int nontrivial;

  /* Actually generate the code, and see if anything came out.  At the
     moment, it would be trivial to replicate the code in
     gen_conversion here, but in the future, when we're supporting
     floating point and the like, it may not be.  Doing things this
     way allows this function to be independent of the logic in
     gen_conversion.  */
  gen_conversion (ax, from, to);
  nontrivial = ax->len > 0;
  free_agent_expr (ax);
  return nontrivial;
}


/* Generate code to perform the "usual arithmetic conversions" (ANSI C
   6.2.1.5) for the two operands of an arithmetic operator.  This
   effectively finds a "least upper bound" type for the two arguments,
   and promotes each argument to that type.  *VALUE1 and *VALUE2
   describe the values as they are passed in, and as they are left.  */
static void
gen_usual_arithmetic (struct agent_expr *ax, struct axs_value *value1,
		      struct axs_value *value2)
{
  /* Do the usual binary conversions.  */
  if (TYPE_CODE (value1->type) == TYPE_CODE_INT
      && TYPE_CODE (value2->type) == TYPE_CODE_INT)
    {
      /* The ANSI integral promotions seem to work this way: Order the
         integer types by size, and then by signedness: an n-bit
         unsigned type is considered "wider" than an n-bit signed
         type.  Promote to the "wider" of the two types, and always
         promote at least to int.  */
      struct type *target = max_type (builtin_type_int,
				      max_type (value1->type, value2->type));

      /* Deal with value2, on the top of the stack.  */
      gen_conversion (ax, value2->type, target);

      /* Deal with value1, not on the top of the stack.  Don't
         generate the `swap' instructions if we're not actually going
         to do anything.  */
      if (is_nontrivial_conversion (value1->type, target))
	{
	  ax_simple (ax, aop_swap);
	  gen_conversion (ax, value1->type, target);
	  ax_simple (ax, aop_swap);
	}

      value1->type = value2->type = target;
    }
}


/* Generate code to perform the integral promotions (ANSI 6.2.1.1) on
   the value on the top of the stack, as described by VALUE.  Assume
   the value has integral type.  */
static void
gen_integral_promotions (struct agent_expr *ax, struct axs_value *value)
{
  if (!type_wider_than (value->type, builtin_type_int))
    {
      gen_conversion (ax, value->type, builtin_type_int);
      value->type = builtin_type_int;
    }
  else if (!type_wider_than (value->type, builtin_type_unsigned_int))
    {
      gen_conversion (ax, value->type, builtin_type_unsigned_int);
      value->type = builtin_type_unsigned_int;
    }
}


/* Generate code for a cast to TYPE.  */
static void
gen_cast (struct agent_expr *ax, struct axs_value *value, struct type *type)
{
  /* GCC does allow casts to yield lvalues, so this should be fixed
     before merging these changes into the trunk.  */
  require_rvalue (ax, value);
  /* Dereference typedefs. */
  type = check_typedef (type);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_PTR:
      /* It's implementation-defined, and I'll bet this is what GCC
         does.  */
      break;

    case TYPE_CODE_ARRAY:
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
    case TYPE_CODE_FUNC:
      error ("Illegal type cast: intended type must be scalar.");

    case TYPE_CODE_ENUM:
      /* We don't have to worry about the size of the value, because
         all our integral values are fully sign-extended, and when
         casting pointers we can do anything we like.  Is there any
         way for us to actually know what GCC actually does with a
         cast like this?  */
      value->type = type;
      break;

    case TYPE_CODE_INT:
      gen_conversion (ax, value->type, type);
      break;

    case TYPE_CODE_VOID:
      /* We could pop the value, and rely on everyone else to check
         the type and notice that this value doesn't occupy a stack
         slot.  But for now, leave the value on the stack, and
         preserve the "value == stack element" assumption.  */
      break;

    default:
      error ("Casts to requested type are not yet implemented.");
    }

  value->type = type;
}



/* Generating bytecode from GDB expressions: arithmetic */

/* Scale the integer on the top of the stack by the size of the target
   of the pointer type TYPE.  */
static void
gen_scale (struct agent_expr *ax, enum agent_op op, struct type *type)
{
  struct type *element = TYPE_TARGET_TYPE (type);

  if (TYPE_LENGTH (element) != 1)
    {
      ax_const_l (ax, TYPE_LENGTH (element));
      ax_simple (ax, op);
    }
}


/* Generate code for an addition; non-trivial because we deal with
   pointer arithmetic.  We set VALUE to describe the result value; we
   assume VALUE1 and VALUE2 describe the two operands, and that
   they've undergone the usual binary conversions.  Used by both
   BINOP_ADD and BINOP_SUBSCRIPT.  NAME is used in error messages.  */
static void
gen_add (struct agent_expr *ax, struct axs_value *value,
	 struct axs_value *value1, struct axs_value *value2, char *name)
{
  /* Is it INT+PTR?  */
  if (TYPE_CODE (value1->type) == TYPE_CODE_INT
      && TYPE_CODE (value2->type) == TYPE_CODE_PTR)
    {
      /* Swap the values and proceed normally.  */
      ax_simple (ax, aop_swap);
      gen_scale (ax, aop_mul, value2->type);
      ax_simple (ax, aop_add);
      gen_extend (ax, value2->type);	/* Catch overflow.  */
      value->type = value2->type;
    }

  /* Is it PTR+INT?  */
  else if (TYPE_CODE (value1->type) == TYPE_CODE_PTR
	   && TYPE_CODE (value2->type) == TYPE_CODE_INT)
    {
      gen_scale (ax, aop_mul, value1->type);
      ax_simple (ax, aop_add);
      gen_extend (ax, value1->type);	/* Catch overflow.  */
      value->type = value1->type;
    }

  /* Must be number + number; the usual binary conversions will have
     brought them both to the same width.  */
  else if (TYPE_CODE (value1->type) == TYPE_CODE_INT
	   && TYPE_CODE (value2->type) == TYPE_CODE_INT)
    {
      ax_simple (ax, aop_add);
      gen_extend (ax, value1->type);	/* Catch overflow.  */
      value->type = value1->type;
    }

  else
    error ("Illegal combination of types in %s.", name);

  value->kind = axs_rvalue;
}


/* Generate code for an addition; non-trivial because we have to deal
   with pointer arithmetic.  We set VALUE to describe the result
   value; we assume VALUE1 and VALUE2 describe the two operands, and
   that they've undergone the usual binary conversions.  */
static void
gen_sub (struct agent_expr *ax, struct axs_value *value,
	 struct axs_value *value1, struct axs_value *value2)
{
  if (TYPE_CODE (value1->type) == TYPE_CODE_PTR)
    {
      /* Is it PTR - INT?  */
      if (TYPE_CODE (value2->type) == TYPE_CODE_INT)
	{
	  gen_scale (ax, aop_mul, value1->type);
	  ax_simple (ax, aop_sub);
	  gen_extend (ax, value1->type);	/* Catch overflow.  */
	  value->type = value1->type;
	}

      /* Is it PTR - PTR?  Strictly speaking, the types ought to
         match, but this is what the normal GDB expression evaluator
         tests for.  */
      else if (TYPE_CODE (value2->type) == TYPE_CODE_PTR
	       && (TYPE_LENGTH (TYPE_TARGET_TYPE (value1->type))
		   == TYPE_LENGTH (TYPE_TARGET_TYPE (value2->type))))
	{
	  ax_simple (ax, aop_sub);
	  gen_scale (ax, aop_div_unsigned, value1->type);
	  value->type = builtin_type_long;	/* FIXME --- should be ptrdiff_t */
	}
      else
	error ("\
First argument of `-' is a pointer, but second argument is neither\n\
an integer nor a pointer of the same type.");
    }

  /* Must be number + number.  */
  else if (TYPE_CODE (value1->type) == TYPE_CODE_INT
	   && TYPE_CODE (value2->type) == TYPE_CODE_INT)
    {
      ax_simple (ax, aop_sub);
      gen_extend (ax, value1->type);	/* Catch overflow.  */
      value->type = value1->type;
    }

  else
    error ("Illegal combination of types in subtraction.");

  value->kind = axs_rvalue;
}

/* Generate code for a binary operator that doesn't do pointer magic.
   We set VALUE to describe the result value; we assume VALUE1 and
   VALUE2 describe the two operands, and that they've undergone the
   usual binary conversions.  MAY_CARRY should be non-zero iff the
   result needs to be extended.  NAME is the English name of the
   operator, used in error messages */
static void
gen_binop (struct agent_expr *ax, struct axs_value *value,
	   struct axs_value *value1, struct axs_value *value2, enum agent_op op,
	   enum agent_op op_unsigned, int may_carry, char *name)
{
  /* We only handle INT op INT.  */
  if ((TYPE_CODE (value1->type) != TYPE_CODE_INT)
      || (TYPE_CODE (value2->type) != TYPE_CODE_INT))
    error ("Illegal combination of types in %s.", name);

  ax_simple (ax,
	     TYPE_UNSIGNED (value1->type) ? op_unsigned : op);
  if (may_carry)
    gen_extend (ax, value1->type);	/* catch overflow */
  value->type = value1->type;
  value->kind = axs_rvalue;
}


static void
gen_logical_not (struct agent_expr *ax, struct axs_value *value)
{
  if (TYPE_CODE (value->type) != TYPE_CODE_INT
      && TYPE_CODE (value->type) != TYPE_CODE_PTR)
    error ("Illegal type of operand to `!'.");

  gen_usual_unary (ax, value);
  ax_simple (ax, aop_log_not);
  value->type = builtin_type_int;
}


static void
gen_complement (struct agent_expr *ax, struct axs_value *value)
{
  if (TYPE_CODE (value->type) != TYPE_CODE_INT)
    error ("Illegal type of operand to `~'.");

  gen_usual_unary (ax, value);
  gen_integral_promotions (ax, value);
  ax_simple (ax, aop_bit_not);
  gen_extend (ax, value->type);
}



/* Generating bytecode from GDB expressions: * & . -> @ sizeof */

/* Dereference the value on the top of the stack.  */
static void
gen_deref (struct agent_expr *ax, struct axs_value *value)
{
  /* The caller should check the type, because several operators use
     this, and we don't know what error message to generate.  */
  if (TYPE_CODE (value->type) != TYPE_CODE_PTR)
    internal_error (__FILE__, __LINE__,
		    "gen_deref: expected a pointer");

  /* We've got an rvalue now, which is a pointer.  We want to yield an
     lvalue, whose address is exactly that pointer.  So we don't
     actually emit any code; we just change the type from "Pointer to
     T" to "T", and mark the value as an lvalue in memory.  Leave it
     to the consumer to actually dereference it.  */
  value->type = check_typedef (TYPE_TARGET_TYPE (value->type));
  value->kind = ((TYPE_CODE (value->type) == TYPE_CODE_FUNC)
		 ? axs_rvalue : axs_lvalue_memory);
}


/* Produce the address of the lvalue on the top of the stack.  */
static void
gen_address_of (struct agent_expr *ax, struct axs_value *value)
{
  /* Special case for taking the address of a function.  The ANSI
     standard describes this as a special case, too, so this
     arrangement is not without motivation.  */
  if (TYPE_CODE (value->type) == TYPE_CODE_FUNC)
    /* The value's already an rvalue on the stack, so we just need to
       change the type.  */
    value->type = lookup_pointer_type (value->type);
  else
    switch (value->kind)
      {
      case axs_rvalue:
	error ("Operand of `&' is an rvalue, which has no address.");

      case axs_lvalue_register:
	error ("Operand of `&' is in a register, and has no address.");

      case axs_lvalue_memory:
	value->kind = axs_rvalue;
	value->type = lookup_pointer_type (value->type);
	break;
      }
}


/* A lot of this stuff will have to change to support C++.  But we're
   not going to deal with that at the moment.  */

/* Find the field in the structure type TYPE named NAME, and return
   its index in TYPE's field array.  */
static int
find_field (struct type *type, char *name)
{
  int i;

  CHECK_TYPEDEF (type);

  /* Make sure this isn't C++.  */
  if (TYPE_N_BASECLASSES (type) != 0)
    internal_error (__FILE__, __LINE__,
		    "find_field: derived classes supported");

  for (i = 0; i < TYPE_NFIELDS (type); i++)
    {
      char *this_name = TYPE_FIELD_NAME (type, i);

      if (this_name && strcmp (name, this_name) == 0)
	return i;

      if (this_name[0] == '\0')
	internal_error (__FILE__, __LINE__,
			"find_field: anonymous unions not supported");
    }

  error ("Couldn't find member named `%s' in struct/union `%s'",
	 name, TYPE_TAG_NAME (type));

  return 0;
}


/* Generate code to push the value of a bitfield of a structure whose
   address is on the top of the stack.  START and END give the
   starting and one-past-ending *bit* numbers of the field within the
   structure.  */
static void
gen_bitfield_ref (struct agent_expr *ax, struct axs_value *value,
		  struct type *type, int start, int end)
{
  /* Note that ops[i] fetches 8 << i bits.  */
  static enum agent_op ops[]
  =
  {aop_ref8, aop_ref16, aop_ref32, aop_ref64};
  static int num_ops = (sizeof (ops) / sizeof (ops[0]));

  /* We don't want to touch any byte that the bitfield doesn't
     actually occupy; we shouldn't make any accesses we're not
     explicitly permitted to.  We rely here on the fact that the
     bytecode `ref' operators work on unaligned addresses.

     It takes some fancy footwork to get the stack to work the way
     we'd like.  Say we're retrieving a bitfield that requires three
     fetches.  Initially, the stack just contains the address:
     addr
     For the first fetch, we duplicate the address
     addr addr
     then add the byte offset, do the fetch, and shift and mask as
     needed, yielding a fragment of the value, properly aligned for
     the final bitwise or:
     addr frag1
     then we swap, and repeat the process:
     frag1 addr                    --- address on top
     frag1 addr addr               --- duplicate it
     frag1 addr frag2              --- get second fragment
     frag1 frag2 addr              --- swap again
     frag1 frag2 frag3             --- get third fragment
     Notice that, since the third fragment is the last one, we don't
     bother duplicating the address this time.  Now we have all the
     fragments on the stack, and we can simply `or' them together,
     yielding the final value of the bitfield.  */

  /* The first and one-after-last bits in the field, but rounded down
     and up to byte boundaries.  */
  int bound_start = (start / TARGET_CHAR_BIT) * TARGET_CHAR_BIT;
  int bound_end = (((end + TARGET_CHAR_BIT - 1)
		    / TARGET_CHAR_BIT)
		   * TARGET_CHAR_BIT);

  /* current bit offset within the structure */
  int offset;

  /* The index in ops of the opcode we're considering.  */
  int op;

  /* The number of fragments we generated in the process.  Probably
     equal to the number of `one' bits in bytesize, but who cares?  */
  int fragment_count;

  /* Dereference any typedefs. */
  type = check_typedef (type);

  /* Can we fetch the number of bits requested at all?  */
  if ((end - start) > ((1 << num_ops) * 8))
    internal_error (__FILE__, __LINE__,
		    "gen_bitfield_ref: bitfield too wide");

  /* Note that we know here that we only need to try each opcode once.
     That may not be true on machines with weird byte sizes.  */
  offset = bound_start;
  fragment_count = 0;
  for (op = num_ops - 1; op >= 0; op--)
    {
      /* number of bits that ops[op] would fetch */
      int op_size = 8 << op;

      /* The stack at this point, from bottom to top, contains zero or
         more fragments, then the address.  */

      /* Does this fetch fit within the bitfield?  */
      if (offset + op_size <= bound_end)
	{
	  /* Is this the last fragment?  */
	  int last_frag = (offset + op_size == bound_end);

	  if (!last_frag)
	    ax_simple (ax, aop_dup);	/* keep a copy of the address */

	  /* Add the offset.  */
	  gen_offset (ax, offset / TARGET_CHAR_BIT);

	  if (trace_kludge)
	    {
	      /* Record the area of memory we're about to fetch.  */
	      ax_trace_quick (ax, op_size / TARGET_CHAR_BIT);
	    }

	  /* Perform the fetch.  */
	  ax_simple (ax, ops[op]);

	  /* Shift the bits we have to their proper position.
	     gen_left_shift will generate right shifts when the operand
	     is negative.

	     A big-endian field diagram to ponder:
	     byte 0  byte 1  byte 2  byte 3  byte 4  byte 5  byte 6  byte 7
	     +------++------++------++------++------++------++------++------+
	     xxxxAAAAAAAAAAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBCCCCCxxxxxxxxxxx
	     ^               ^               ^    ^
	     bit number      16              32              48   53
	     These are bit numbers as supplied by GDB.  Note that the
	     bit numbers run from right to left once you've fetched the
	     value!

	     A little-endian field diagram to ponder:
	     byte 7  byte 6  byte 5  byte 4  byte 3  byte 2  byte 1  byte 0
	     +------++------++------++------++------++------++------++------+
	     xxxxxxxxxxxAAAAABBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCCCCCCCCCCCCCCxxxx
	     ^               ^               ^           ^   ^
	     bit number     48              32              16          4   0

	     In both cases, the most significant end is on the left
	     (i.e. normal numeric writing order), which means that you
	     don't go crazy thinking about `left' and `right' shifts.

	     We don't have to worry about masking yet:
	     - If they contain garbage off the least significant end, then we
	     must be looking at the low end of the field, and the right
	     shift will wipe them out.
	     - If they contain garbage off the most significant end, then we
	     must be looking at the most significant end of the word, and
	     the sign/zero extension will wipe them out.
	     - If we're in the interior of the word, then there is no garbage
	     on either end, because the ref operators zero-extend.  */
	  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
	    gen_left_shift (ax, end - (offset + op_size));
	  else
	    gen_left_shift (ax, offset - start);

	  if (!last_frag)
	    /* Bring the copy of the address up to the top.  */
	    ax_simple (ax, aop_swap);

	  offset += op_size;
	  fragment_count++;
	}
    }

  /* Generate enough bitwise `or' operations to combine all the
     fragments we left on the stack.  */
  while (fragment_count-- > 1)
    ax_simple (ax, aop_bit_or);

  /* Sign- or zero-extend the value as appropriate.  */
  ((TYPE_UNSIGNED (type) ? ax_zero_ext : ax_ext) (ax, end - start));

  /* This is *not* an lvalue.  Ugh.  */
  value->kind = axs_rvalue;
  value->type = type;
}


/* Generate code to reference the member named FIELD of a structure or
   union.  The top of the stack, as described by VALUE, should have
   type (pointer to a)* struct/union.  OPERATOR_NAME is the name of
   the operator being compiled, and OPERAND_NAME is the kind of thing
   it operates on; we use them in error messages.  */
static void
gen_struct_ref (struct agent_expr *ax, struct axs_value *value, char *field,
		char *operator_name, char *operand_name)
{
  struct type *type;
  int i;

  /* Follow pointers until we reach a non-pointer.  These aren't the C
     semantics, but they're what the normal GDB evaluator does, so we
     should at least be consistent.  */
  while (TYPE_CODE (value->type) == TYPE_CODE_PTR)
    {
      gen_usual_unary (ax, value);
      gen_deref (ax, value);
    }
  type = check_typedef (value->type);

  /* This must yield a structure or a union.  */
  if (TYPE_CODE (type) != TYPE_CODE_STRUCT
      && TYPE_CODE (type) != TYPE_CODE_UNION)
    error ("The left operand of `%s' is not a %s.",
	   operator_name, operand_name);

  /* And it must be in memory; we don't deal with structure rvalues,
     or structures living in registers.  */
  if (value->kind != axs_lvalue_memory)
    error ("Structure does not live in memory.");

  i = find_field (type, field);

  /* Is this a bitfield?  */
  if (TYPE_FIELD_PACKED (type, i))
    gen_bitfield_ref (ax, value, TYPE_FIELD_TYPE (type, i),
		      TYPE_FIELD_BITPOS (type, i),
		      (TYPE_FIELD_BITPOS (type, i)
		       + TYPE_FIELD_BITSIZE (type, i)));
  else
    {
      gen_offset (ax, TYPE_FIELD_BITPOS (type, i) / TARGET_CHAR_BIT);
      value->kind = axs_lvalue_memory;
      value->type = TYPE_FIELD_TYPE (type, i);
    }
}


/* Generate code for GDB's magical `repeat' operator.  
   LVALUE @ INT creates an array INT elements long, and whose elements
   have the same type as LVALUE, located in memory so that LVALUE is
   its first element.  For example, argv[0]@argc gives you the array
   of command-line arguments.

   Unfortunately, because we have to know the types before we actually
   have a value for the expression, we can't implement this perfectly
   without changing the type system, having values that occupy two
   stack slots, doing weird things with sizeof, etc.  So we require
   the right operand to be a constant expression.  */
static void
gen_repeat (union exp_element **pc, struct agent_expr *ax,
	    struct axs_value *value)
{
  struct axs_value value1;
  /* We don't want to turn this into an rvalue, so no conversions
     here.  */
  gen_expr (pc, ax, &value1);
  if (value1.kind != axs_lvalue_memory)
    error ("Left operand of `@' must be an object in memory.");

  /* Evaluate the length; it had better be a constant.  */
  {
    struct value *v = const_expr (pc);
    int length;

    if (!v)
      error ("Right operand of `@' must be a constant, in agent expressions.");
    if (TYPE_CODE (v->type) != TYPE_CODE_INT)
      error ("Right operand of `@' must be an integer.");
    length = value_as_long (v);
    if (length <= 0)
      error ("Right operand of `@' must be positive.");

    /* The top of the stack is already the address of the object, so
       all we need to do is frob the type of the lvalue.  */
    {
      /* FIXME-type-allocation: need a way to free this type when we are
         done with it.  */
      struct type *range
      = create_range_type (0, builtin_type_int, 0, length - 1);
      struct type *array = create_array_type (0, value1.type, range);

      value->kind = axs_lvalue_memory;
      value->type = array;
    }
  }
}


/* Emit code for the `sizeof' operator.
   *PC should point at the start of the operand expression; we advance it
   to the first instruction after the operand.  */
static void
gen_sizeof (union exp_element **pc, struct agent_expr *ax,
	    struct axs_value *value)
{
  /* We don't care about the value of the operand expression; we only
     care about its type.  However, in the current arrangement, the
     only way to find an expression's type is to generate code for it.
     So we generate code for the operand, and then throw it away,
     replacing it with code that simply pushes its size.  */
  int start = ax->len;
  gen_expr (pc, ax, value);

  /* Throw away the code we just generated.  */
  ax->len = start;

  ax_const_l (ax, TYPE_LENGTH (value->type));
  value->kind = axs_rvalue;
  value->type = builtin_type_int;
}


/* Generating bytecode from GDB expressions: general recursive thingy  */

/* A gen_expr function written by a Gen-X'er guy.
   Append code for the subexpression of EXPR starting at *POS_P to AX.  */
static void
gen_expr (union exp_element **pc, struct agent_expr *ax,
	  struct axs_value *value)
{
  /* Used to hold the descriptions of operand expressions.  */
  struct axs_value value1, value2;
  enum exp_opcode op = (*pc)[0].opcode;

  /* If we're looking at a constant expression, just push its value.  */
  {
    struct value *v = maybe_const_expr (pc);

    if (v)
      {
	ax_const_l (ax, value_as_long (v));
	value->kind = axs_rvalue;
	value->type = check_typedef (VALUE_TYPE (v));
	return;
      }
  }

  /* Otherwise, go ahead and generate code for it.  */
  switch (op)
    {
      /* Binary arithmetic operators.  */
    case BINOP_ADD:
    case BINOP_SUB:
    case BINOP_MUL:
    case BINOP_DIV:
    case BINOP_REM:
    case BINOP_SUBSCRIPT:
    case BINOP_BITWISE_AND:
    case BINOP_BITWISE_IOR:
    case BINOP_BITWISE_XOR:
      (*pc)++;
      gen_expr (pc, ax, &value1);
      gen_usual_unary (ax, &value1);
      gen_expr (pc, ax, &value2);
      gen_usual_unary (ax, &value2);
      gen_usual_arithmetic (ax, &value1, &value2);
      switch (op)
	{
	case BINOP_ADD:
	  gen_add (ax, value, &value1, &value2, "addition");
	  break;
	case BINOP_SUB:
	  gen_sub (ax, value, &value1, &value2);
	  break;
	case BINOP_MUL:
	  gen_binop (ax, value, &value1, &value2,
		     aop_mul, aop_mul, 1, "multiplication");
	  break;
	case BINOP_DIV:
	  gen_binop (ax, value, &value1, &value2,
		     aop_div_signed, aop_div_unsigned, 1, "division");
	  break;
	case BINOP_REM:
	  gen_binop (ax, value, &value1, &value2,
		     aop_rem_signed, aop_rem_unsigned, 1, "remainder");
	  break;
	case BINOP_SUBSCRIPT:
	  gen_add (ax, value, &value1, &value2, "array subscripting");
	  if (TYPE_CODE (value->type) != TYPE_CODE_PTR)
	    error ("Illegal combination of types in array subscripting.");
	  gen_deref (ax, value);
	  break;
	case BINOP_BITWISE_AND:
	  gen_binop (ax, value, &value1, &value2,
		     aop_bit_and, aop_bit_and, 0, "bitwise and");
	  break;

	case BINOP_BITWISE_IOR:
	  gen_binop (ax, value, &value1, &value2,
		     aop_bit_or, aop_bit_or, 0, "bitwise or");
	  break;

	case BINOP_BITWISE_XOR:
	  gen_binop (ax, value, &value1, &value2,
		     aop_bit_xor, aop_bit_xor, 0, "bitwise exclusive-or");
	  break;

	default:
	  /* We should only list operators in the outer case statement
	     that we actually handle in the inner case statement.  */
	  internal_error (__FILE__, __LINE__,
			  "gen_expr: op case sets don't match");
	}
      break;

      /* Note that we need to be a little subtle about generating code
         for comma.  In C, we can do some optimizations here because
         we know the left operand is only being evaluated for effect.
         However, if the tracing kludge is in effect, then we always
         need to evaluate the left hand side fully, so that all the
         variables it mentions get traced.  */
    case BINOP_COMMA:
      (*pc)++;
      gen_expr (pc, ax, &value1);
      /* Don't just dispose of the left operand.  We might be tracing,
         in which case we want to emit code to trace it if it's an
         lvalue.  */
      gen_traced_pop (ax, &value1);
      gen_expr (pc, ax, value);
      /* It's the consumer's responsibility to trace the right operand.  */
      break;

    case OP_LONG:		/* some integer constant */
      {
	struct type *type = (*pc)[1].type;
	LONGEST k = (*pc)[2].longconst;
	(*pc) += 4;
	gen_int_literal (ax, value, k, type);
      }
      break;

    case OP_VAR_VALUE:
      gen_var_ref (ax, value, (*pc)[2].symbol);
      (*pc) += 4;
      break;

    case OP_REGISTER:
      {
	int reg = (int) (*pc)[1].longconst;
	(*pc) += 3;
	value->kind = axs_lvalue_register;
	value->u.reg = reg;
	value->type = register_type (current_gdbarch, reg);
      }
      break;

    case OP_INTERNALVAR:
      error ("GDB agent expressions cannot use convenience variables.");

      /* Weirdo operator: see comments for gen_repeat for details.  */
    case BINOP_REPEAT:
      /* Note that gen_repeat handles its own argument evaluation.  */
      (*pc)++;
      gen_repeat (pc, ax, value);
      break;

    case UNOP_CAST:
      {
	struct type *type = (*pc)[1].type;
	(*pc) += 3;
	gen_expr (pc, ax, value);
	gen_cast (ax, value, type);
      }
      break;

    case UNOP_MEMVAL:
      {
	struct type *type = check_typedef ((*pc)[1].type);
	(*pc) += 3;
	gen_expr (pc, ax, value);
	/* I'm not sure I understand UNOP_MEMVAL entirely.  I think
	   it's just a hack for dealing with minsyms; you take some
	   integer constant, pretend it's the address of an lvalue of
	   the given type, and dereference it.  */
	if (value->kind != axs_rvalue)
	  /* This would be weird.  */
	  internal_error (__FILE__, __LINE__,
			  "gen_expr: OP_MEMVAL operand isn't an rvalue???");
	value->type = type;
	value->kind = axs_lvalue_memory;
      }
      break;

    case UNOP_NEG:
      (*pc)++;
      /* -FOO is equivalent to 0 - FOO.  */
      gen_int_literal (ax, &value1, (LONGEST) 0, builtin_type_int);
      gen_usual_unary (ax, &value1);	/* shouldn't do much */
      gen_expr (pc, ax, &value2);
      gen_usual_unary (ax, &value2);
      gen_usual_arithmetic (ax, &value1, &value2);
      gen_sub (ax, value, &value1, &value2);
      break;

    case UNOP_LOGICAL_NOT:
      (*pc)++;
      gen_expr (pc, ax, value);
      gen_logical_not (ax, value);
      break;

    case UNOP_COMPLEMENT:
      (*pc)++;
      gen_expr (pc, ax, value);
      gen_complement (ax, value);
      break;

    case UNOP_IND:
      (*pc)++;
      gen_expr (pc, ax, value);
      gen_usual_unary (ax, value);
      if (TYPE_CODE (value->type) != TYPE_CODE_PTR)
	error ("Argument of unary `*' is not a pointer.");
      gen_deref (ax, value);
      break;

    case UNOP_ADDR:
      (*pc)++;
      gen_expr (pc, ax, value);
      gen_address_of (ax, value);
      break;

    case UNOP_SIZEOF:
      (*pc)++;
      /* Notice that gen_sizeof handles its own operand, unlike most
         of the other unary operator functions.  This is because we
         have to throw away the code we generate.  */
      gen_sizeof (pc, ax, value);
      break;

    case STRUCTOP_STRUCT:
    case STRUCTOP_PTR:
      {
	int length = (*pc)[1].longconst;
	char *name = &(*pc)[2].string;

	(*pc) += 4 + BYTES_TO_EXP_ELEM (length + 1);
	gen_expr (pc, ax, value);
	if (op == STRUCTOP_STRUCT)
	  gen_struct_ref (ax, value, name, ".", "structure or union");
	else if (op == STRUCTOP_PTR)
	  gen_struct_ref (ax, value, name, "->",
			  "pointer to a structure or union");
	else
	  /* If this `if' chain doesn't handle it, then the case list
	     shouldn't mention it, and we shouldn't be here.  */
	  internal_error (__FILE__, __LINE__,
			  "gen_expr: unhandled struct case");
      }
      break;

    case OP_TYPE:
      error ("Attempt to use a type name as an expression.");

    default:
      error ("Unsupported operator in expression.");
    }
}



/* Generating bytecode from GDB expressions: driver */

/* Given a GDB expression EXPR, produce a string of agent bytecode
   which computes its value.  Return the agent expression, and set
   *VALUE to describe its type, and whether it's an lvalue or rvalue.  */
struct agent_expr *
expr_to_agent (struct expression *expr, struct axs_value *value)
{
  struct cleanup *old_chain = 0;
  struct agent_expr *ax = new_agent_expr (0);
  union exp_element *pc;

  old_chain = make_cleanup_free_agent_expr (ax);

  pc = expr->elts;
  trace_kludge = 0;
  gen_expr (&pc, ax, value);

  /* We have successfully built the agent expr, so cancel the cleanup
     request.  If we add more cleanups that we always want done, this
     will have to get more complicated.  */
  discard_cleanups (old_chain);
  return ax;
}


#if 0				/* not used */
/* Given a GDB expression EXPR denoting an lvalue in memory, produce a
   string of agent bytecode which will leave its address and size on
   the top of stack.  Return the agent expression.

   Not sure this function is useful at all.  */
struct agent_expr *
expr_to_address_and_size (struct expression *expr)
{
  struct axs_value value;
  struct agent_expr *ax = expr_to_agent (expr, &value);

  /* Complain if the result is not a memory lvalue.  */
  if (value.kind != axs_lvalue_memory)
    {
      free_agent_expr (ax);
      error ("Expression does not denote an object in memory.");
    }

  /* Push the object's size on the stack.  */
  ax_const_l (ax, TYPE_LENGTH (value.type));

  return ax;
}
#endif

/* Given a GDB expression EXPR, return bytecode to trace its value.
   The result will use the `trace' and `trace_quick' bytecodes to
   record the value of all memory touched by the expression.  The
   caller can then use the ax_reqs function to discover which
   registers it relies upon.  */
struct agent_expr *
gen_trace_for_expr (CORE_ADDR scope, struct expression *expr)
{
  struct cleanup *old_chain = 0;
  struct agent_expr *ax = new_agent_expr (scope);
  union exp_element *pc;
  struct axs_value value;

  old_chain = make_cleanup_free_agent_expr (ax);

  pc = expr->elts;
  trace_kludge = 1;
  gen_expr (&pc, ax, &value);

  /* Make sure we record the final object, and get rid of it.  */
  gen_traced_pop (ax, &value);

  /* Oh, and terminate.  */
  ax_simple (ax, aop_end);

  /* We have successfully built the agent expr, so cancel the cleanup
     request.  If we add more cleanups that we always want done, this
     will have to get more complicated.  */
  discard_cleanups (old_chain);
  return ax;
}

static void
agent_command (char *exp, int from_tty)
{
  struct cleanup *old_chain = 0;
  struct expression *expr;
  struct agent_expr *agent;
  struct frame_info *fi = get_current_frame ();	/* need current scope */

  /* We don't deal with overlay debugging at the moment.  We need to
     think more carefully about this.  If you copy this code into
     another command, change the error message; the user shouldn't
     have to know anything about agent expressions.  */
  if (overlay_debugging)
    error ("GDB can't do agent expression translation with overlays.");

  if (exp == 0)
    error_no_arg ("expression to translate");

  expr = parse_expression (exp);
  old_chain = make_cleanup (free_current_contents, &expr);
  agent = gen_trace_for_expr (get_frame_pc (fi), expr);
  make_cleanup_free_agent_expr (agent);
  ax_print (gdb_stdout, agent);

  /* It would be nice to call ax_reqs here to gather some general info
     about the expression, and then print out the result.  */

  do_cleanups (old_chain);
  dont_repeat ();
}


/* Initialization code.  */

void _initialize_ax_gdb (void);
void
_initialize_ax_gdb (void)
{
  add_cmd ("agent", class_maintenance, agent_command,
	   "Translate an expression into remote agent bytecode.",
	   &maintenancelist);
}
