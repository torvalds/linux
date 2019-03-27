/* Generate from machine description:
   - prototype declarations for operand predicates (tm-preds.h)
   - function definitions of operand predicates, if defined new-style
     (insn-preds.c)
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "obstack.h"
#include "gensupport.h"

/* Given a predicate expression EXP, from form NAME at line LINENO,
   verify that it does not contain any RTL constructs which are not
   valid in predicate definitions.  Returns true if EXP is
   INvalid; issues error messages, caller need not.  */
static bool
validate_exp (rtx exp, const char *name, int lineno)
{
  if (exp == 0)
    {
      message_with_line (lineno, "%s: must give a predicate expression", name);
      return true;
    }

  switch (GET_CODE (exp))
    {
      /* Ternary, binary, unary expressions: recurse into subexpressions.  */
    case IF_THEN_ELSE:
      if (validate_exp (XEXP (exp, 2), name, lineno))
	return true;
      /* else fall through */
    case AND:
    case IOR:
      if (validate_exp (XEXP (exp, 1), name, lineno))
	return true;
      /* else fall through */
    case NOT:
      return validate_exp (XEXP (exp, 0), name, lineno);

      /* MATCH_CODE might have a syntax error in its path expression.  */
    case MATCH_CODE:
      {
	const char *p;
	for (p = XSTR (exp, 1); *p; p++)
	  {
	    if (!ISDIGIT (*p) && !ISLOWER (*p))
	      {
		message_with_line (lineno, "%s: invalid character in path "
				   "string '%s'", name, XSTR (exp, 1));
		have_error = 1;
		return true;
	      }
	  }
      }
      /* fall through */

      /* These need no special checking.  */
    case MATCH_OPERAND:
    case MATCH_TEST:
      return false;

    default:
      message_with_line (lineno,
			 "%s: cannot use '%s' in a predicate expression",
			 name, GET_RTX_NAME (GET_CODE (exp)));
      have_error = 1;
      return true;
    }
}

/* Predicates are defined with (define_predicate) or
   (define_special_predicate) expressions in the machine description.  */
static void
process_define_predicate (rtx defn, int lineno)
{
  struct pred_data *pred;
  const char *p;

  if (!ISALPHA (XSTR (defn, 0)[0]) && XSTR (defn, 0)[0] != '_')
    goto bad_name;
  for (p = XSTR (defn, 0) + 1; *p; p++)
    if (!ISALNUM (*p) && *p != '_')
      goto bad_name;
  
  if (validate_exp (XEXP (defn, 1), XSTR (defn, 0), lineno))
    return;

  pred = XCNEW (struct pred_data);
  pred->name = XSTR (defn, 0);
  pred->exp = XEXP (defn, 1);
  pred->c_block = XSTR (defn, 2);

  if (GET_CODE (defn) == DEFINE_SPECIAL_PREDICATE)
    pred->special = true;

  add_predicate (pred);
  return;

 bad_name:
  message_with_line (lineno,
		     "%s: predicate name must be a valid C function name",
		     XSTR (defn, 0));
  have_error = 1;
  return;
}

/* Given a predicate, if it has an embedded C block, write the block
   out as a static inline subroutine, and augment the RTL test with a
   match_test that calls that subroutine.  For instance,

       (define_predicate "basereg_operand"
         (match_operand 0 "register_operand")
       {
         if (GET_CODE (op) == SUBREG)
           op = SUBREG_REG (op);
         return REG_POINTER (op);
       })

   becomes

       static inline int basereg_operand_1(rtx op, enum machine_mode mode)
       {
         if (GET_CODE (op) == SUBREG)
           op = SUBREG_REG (op);
         return REG_POINTER (op);
       }

       (define_predicate "basereg_operand"
         (and (match_operand 0 "register_operand")
	      (match_test "basereg_operand_1 (op, mode)")))

   The only wart is that there's no way to insist on a { } string in
   an RTL template, so we have to handle "" strings.  */

   
static void
write_predicate_subfunction (struct pred_data *p)
{
  const char *match_test_str;
  rtx match_test_exp, and_exp;

  if (p->c_block[0] == '\0')
    return;

  /* Construct the function-call expression.  */
  obstack_grow (rtl_obstack, p->name, strlen (p->name));
  obstack_grow (rtl_obstack, "_1 (op, mode)",
		sizeof "_1 (op, mode)");
  match_test_str = XOBFINISH (rtl_obstack, const char *);

  /* Add the function-call expression to the complete expression to be
     evaluated.  */
  match_test_exp = rtx_alloc (MATCH_TEST);
  XSTR (match_test_exp, 0) = match_test_str;

  and_exp = rtx_alloc (AND);
  XEXP (and_exp, 0) = p->exp;
  XEXP (and_exp, 1) = match_test_exp;

  p->exp = and_exp;

  printf ("static inline int\n"
	  "%s_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)\n",
	  p->name);
  print_rtx_ptr_loc (p->c_block);
  if (p->c_block[0] == '{')
    fputs (p->c_block, stdout);
  else
    printf ("{\n  %s\n}", p->c_block);
  fputs ("\n\n", stdout);
}

/* Given a predicate expression EXP, from form NAME, determine whether
   it refers to the variable given as VAR.  */
static bool
needs_variable (rtx exp, const char *var)
{
  switch (GET_CODE (exp))
    {
      /* Ternary, binary, unary expressions need a variable if
	 any of their subexpressions do.  */
    case IF_THEN_ELSE:
      if (needs_variable (XEXP (exp, 2), var))
	return true;
      /* else fall through */
    case AND:
    case IOR:
      if (needs_variable (XEXP (exp, 1), var))
	return true;
      /* else fall through */
    case NOT:
      return needs_variable (XEXP (exp, 0), var);

      /* MATCH_CODE uses "op", but nothing else.  */
    case MATCH_CODE:
      return !strcmp (var, "op");

      /* MATCH_OPERAND uses "op" and may use "mode".  */
    case MATCH_OPERAND:
      if (!strcmp (var, "op"))
	return true;
      if (!strcmp (var, "mode") && GET_MODE (exp) == VOIDmode)
	return true;
      return false;

      /* MATCH_TEST uses var if XSTR (exp, 0) =~ /\b${var}\b/o; */
    case MATCH_TEST:
      {
	const char *p = XSTR (exp, 0);
	const char *q = strstr (p, var);
	if (!q)
	  return false;
	if (q != p && (ISALNUM (q[-1]) || q[-1] == '_'))
	  return false;
	q += strlen (var);
	if (ISALNUM (q[0] || q[0] == '_'))
	  return false;
      }
      return true;

    default:
      gcc_unreachable ();
    }
}

/* Given an RTL expression EXP, find all subexpressions which we may
   assume to perform mode tests.  Normal MATCH_OPERAND does;
   MATCH_CODE does if it applies to the whole expression and accepts
   CONST_INT or CONST_DOUBLE; and we have to assume that MATCH_TEST
   does not.  These combine in almost-boolean fashion - the only
   exception is that (not X) must be assumed not to perform a mode
   test, whether or not X does.

   The mark is the RTL /v flag, which is true for subexpressions which
   do *not* perform mode tests.
*/
#define NO_MODE_TEST(EXP) RTX_FLAG (EXP, volatil)
static void
mark_mode_tests (rtx exp)
{
  switch (GET_CODE (exp))
    {
    case MATCH_OPERAND:
      {
	struct pred_data *p = lookup_predicate (XSTR (exp, 1));
	if (!p)
	  error ("reference to undefined predicate '%s'", XSTR (exp, 1));
	else if (p->special || GET_MODE (exp) != VOIDmode)
	  NO_MODE_TEST (exp) = 1;
      }
      break;

    case MATCH_CODE:
      if (XSTR (exp, 1)[0] != '\0'
	  || (!strstr (XSTR (exp, 0), "const_int")
	      && !strstr (XSTR (exp, 0), "const_double")))
	NO_MODE_TEST (exp) = 1;
      break;

    case MATCH_TEST:
    case NOT:
      NO_MODE_TEST (exp) = 1;
      break;

    case AND:
      mark_mode_tests (XEXP (exp, 0));
      mark_mode_tests (XEXP (exp, 1));

      NO_MODE_TEST (exp) = (NO_MODE_TEST (XEXP (exp, 0))
			    && NO_MODE_TEST (XEXP (exp, 1)));
      break;
      
    case IOR:
      mark_mode_tests (XEXP (exp, 0));
      mark_mode_tests (XEXP (exp, 1));

      NO_MODE_TEST (exp) = (NO_MODE_TEST (XEXP (exp, 0))
			    || NO_MODE_TEST (XEXP (exp, 1)));
      break;

    case IF_THEN_ELSE:
      /* A ? B : C does a mode test if (one of A and B) does a mode
	 test, and C does too.  */
      mark_mode_tests (XEXP (exp, 0));
      mark_mode_tests (XEXP (exp, 1));
      mark_mode_tests (XEXP (exp, 2));

      NO_MODE_TEST (exp) = ((NO_MODE_TEST (XEXP (exp, 0))
			     && NO_MODE_TEST (XEXP (exp, 1)))
			    || NO_MODE_TEST (XEXP (exp, 2)));
      break;

    default:
      gcc_unreachable ();
    }
}

/* Determine whether the expression EXP is a MATCH_CODE that should
   be written as a switch statement.  */
static bool
generate_switch_p (rtx exp)
{
  return GET_CODE (exp) == MATCH_CODE
	 && strchr (XSTR (exp, 0), ',');
}

/* Given a predicate, work out where in its RTL expression to add
   tests for proper modes.  Special predicates do not get any such
   tests.  We try to avoid adding tests when we don't have to; in
   particular, other normal predicates can be counted on to do it for
   us.  */

static void
add_mode_tests (struct pred_data *p)
{
  rtx match_test_exp, and_exp;
  rtx *pos;

  /* Don't touch special predicates.  */
  if (p->special)
    return;

  mark_mode_tests (p->exp);

  /* If the whole expression already tests the mode, we're done.  */
  if (!NO_MODE_TEST (p->exp))
    return;

  match_test_exp = rtx_alloc (MATCH_TEST);
  XSTR (match_test_exp, 0) = "mode == VOIDmode || GET_MODE (op) == mode";
  and_exp = rtx_alloc (AND);
  XEXP (and_exp, 1) = match_test_exp;

  /* It is always correct to rewrite p->exp as

        (and (...) (match_test "mode == VOIDmode || GET_MODE (op) == mode"))

     but there are a couple forms where we can do better.  If the
     top-level pattern is an IOR, and one of the two branches does test
     the mode, we can wrap just the branch that doesn't.  Likewise, if
     we have an IF_THEN_ELSE, and one side of it tests the mode, we can
     wrap just the side that doesn't.  And, of course, we can repeat this
     descent as many times as it works.  */

  pos = &p->exp;
  for (;;)
    {
      rtx subexp = *pos;

      switch (GET_CODE (subexp))
	{
	case AND:
	  /* The switch code generation in write_predicate_stmts prefers
	     rtx code tests to be at the top of the expression tree.  So
	     push this AND down into the second operand of an existing
	     AND expression.  */
	  if (generate_switch_p (XEXP (subexp, 0)))
	    pos = &XEXP (subexp, 1);
	  goto break_loop;

	case IOR:
	  {
	    int test0 = NO_MODE_TEST (XEXP (subexp, 0));
	    int test1 = NO_MODE_TEST (XEXP (subexp, 1));
	    
	    gcc_assert (test0 || test1);
	    
	    if (test0 && test1)
	      goto break_loop;
	    pos = test0 ? &XEXP (subexp, 0) : &XEXP (subexp, 1);
	  }
	  break;
	  
	case IF_THEN_ELSE:
	  {
	    int test0 = NO_MODE_TEST (XEXP (subexp, 0));
	    int test1 = NO_MODE_TEST (XEXP (subexp, 1));
	    int test2 = NO_MODE_TEST (XEXP (subexp, 2));
	    
	    gcc_assert ((test0 && test1) || test2);
	    
	    if (test0 && test1 && test2)
	      goto break_loop;
	    if (test0 && test1)
	      /* Must put it on the dependent clause, not the
	      	 controlling expression, or we change the meaning of
	      	 the test.  */
	      pos = &XEXP (subexp, 1);
	    else
	      pos = &XEXP (subexp, 2);
	  }
	  break;
	  
	default:
	  goto break_loop;
	}
    }
 break_loop:
  XEXP (and_exp, 0) = *pos;
  *pos = and_exp;
}

/* PATH is a string describing a path from the root of an RTL
   expression to an inner subexpression to be tested.  Output
   code which computes the subexpression from the variable
   holding the root of the expression.  */
static void
write_extract_subexp (const char *path)
{
  int len = strlen (path);
  int i;

  /* We first write out the operations (XEXP or XVECEXP) in reverse
     order, then write "op", then the indices in forward order.  */
  for (i = len - 1; i >= 0; i--)
    {
      if (ISLOWER (path[i]))
	fputs ("XVECEXP (", stdout);
      else if (ISDIGIT (path[i]))
	fputs ("XEXP (", stdout);
      else
	gcc_unreachable ();
    }

  fputs ("op", stdout);

  for (i = 0; i < len; i++)
    {
      if (ISLOWER (path[i]))
	printf (", 0, %d)", path[i] - 'a');
      else if (ISDIGIT (path[i]))
	printf (", %d)", path[i] - '0');
      else
	gcc_unreachable ();
    }
}

/* CODES is a list of RTX codes.  Write out an expression which
   determines whether the operand has one of those codes.  */
static void
write_match_code (const char *path, const char *codes)
{
  const char *code;

  while ((code = scan_comma_elt (&codes)) != 0)
    {
      fputs ("GET_CODE (", stdout);
      write_extract_subexp (path);
      fputs (") == ", stdout);
      while (code < codes)
	{
	  putchar (TOUPPER (*code));
	  code++;
	}
      
      if (*codes == ',')
	fputs (" || ", stdout);
    }
}

/* EXP is an RTL (sub)expression for a predicate.  Recursively
   descend the expression and write out an equivalent C expression.  */
static void
write_predicate_expr (rtx exp)
{
  switch (GET_CODE (exp))
    {
    case AND:
      putchar ('(');
      write_predicate_expr (XEXP (exp, 0));
      fputs (") && (", stdout);
      write_predicate_expr (XEXP (exp, 1));
      putchar (')');
      break;
  
    case IOR:
      putchar ('(');
      write_predicate_expr (XEXP (exp, 0));
      fputs (") || (", stdout);
      write_predicate_expr (XEXP (exp, 1));
      putchar (')');
      break;

    case NOT:
      fputs ("!(", stdout);
      write_predicate_expr (XEXP (exp, 0));
      putchar (')');
      break;

    case IF_THEN_ELSE:
      putchar ('(');
      write_predicate_expr (XEXP (exp, 0));
      fputs (") ? (", stdout);
      write_predicate_expr (XEXP (exp, 1));
      fputs (") : (", stdout);
      write_predicate_expr (XEXP (exp, 2));
      putchar (')');
      break;

    case MATCH_OPERAND:
      if (GET_MODE (exp) == VOIDmode)
        printf ("%s (op, mode)", XSTR (exp, 1));
      else
        printf ("%s (op, %smode)", XSTR (exp, 1), mode_name[GET_MODE (exp)]);
      break;

    case MATCH_CODE:
      write_match_code (XSTR (exp, 1), XSTR (exp, 0));
      break;

    case MATCH_TEST:
      print_c_condition (XSTR (exp, 0));
      break;

    default:
      gcc_unreachable ();
    }
}

/* Write the MATCH_CODE expression EXP as a switch statement.  */

static void
write_match_code_switch (rtx exp)
{
  const char *codes = XSTR (exp, 0);
  const char *path = XSTR (exp, 1);
  const char *code;

  fputs ("  switch (GET_CODE (", stdout);
  write_extract_subexp (path);
  fputs ("))\n    {\n", stdout);

  while ((code = scan_comma_elt (&codes)) != 0)
    {
      fputs ("    case ", stdout);
      while (code < codes)
	{
	  putchar (TOUPPER (*code));
	  code++;
	}
      fputs(":\n", stdout);
    }
}

/* Given a predicate expression EXP, write out a sequence of stmts
   to evaluate it.  This is similar to write_predicate_expr but can
   generate efficient switch statements.  */

static void
write_predicate_stmts (rtx exp)
{
  switch (GET_CODE (exp))
    {
    case MATCH_CODE:
      if (generate_switch_p (exp))
	{
	  write_match_code_switch (exp);
	  puts ("      return true;\n"
		"    default:\n"
		"      break;\n"
		"    }\n"
		"  return false;");
	  return;
	}
      break;

    case AND:
      if (generate_switch_p (XEXP (exp, 0)))
	{
	  write_match_code_switch (XEXP (exp, 0));
	  puts ("      break;\n"
		"    default:\n"
		"      return false;\n"
		"    }");
	  exp = XEXP (exp, 1);
	}
      break;

    case IOR:
      if (generate_switch_p (XEXP (exp, 0)))
	{
	  write_match_code_switch (XEXP (exp, 0));
	  puts ("      return true;\n"
		"    default:\n"
		"      break;\n"
		"    }");
	  exp = XEXP (exp, 1);
	}
      break;

    case NOT:
      if (generate_switch_p (XEXP (exp, 0)))
	{
	  write_match_code_switch (XEXP (exp, 0));
	  puts ("      return false;\n"
		"    default:\n"
		"      break;\n"
		"    }\n"
		"  return true;");
	  return;
	}
      break;

    default:
      break;
    }

  fputs("  return ",stdout);
  write_predicate_expr (exp);
  fputs(";\n", stdout);
}

/* Given a predicate, write out a complete C function to compute it.  */
static void
write_one_predicate_function (struct pred_data *p)
{
  if (!p->exp)
    return;

  write_predicate_subfunction (p);
  add_mode_tests (p);

  /* A normal predicate can legitimately not look at enum machine_mode
     if it accepts only CONST_INTs and/or CONST_DOUBLEs.  */
  printf ("int\n%s (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)\n{\n",
	  p->name);
  write_predicate_stmts (p->exp);
  fputs ("}\n\n", stdout);
}

/* Constraints fall into two categories: register constraints
   (define_register_constraint), and others (define_constraint,
   define_memory_constraint, define_address_constraint).  We
   work out automatically which of the various old-style macros
   they correspond to, and produce appropriate code.  They all
   go in the same hash table so we can verify that there are no
   duplicate names.  */

/* All data from one constraint definition.  */
struct constraint_data
{
  struct constraint_data *next_this_letter;
  struct constraint_data *next_textual;
  const char *name;
  const char *c_name;    /* same as .name unless mangling is necessary */
  size_t namelen;
  const char *regclass;  /* for register constraints */
  rtx exp;               /* for other constraints */
  unsigned int lineno;   /* line of definition */
  unsigned int is_register  : 1;
  unsigned int is_const_int : 1;
  unsigned int is_const_dbl : 1;
  unsigned int is_extra     : 1;
  unsigned int is_memory    : 1;
  unsigned int is_address   : 1;
};

/* Overview of all constraints beginning with a given letter.  */

static struct constraint_data *
constraints_by_letter_table[1<<CHAR_BIT];

/* For looking up all the constraints in the order that they appeared
   in the machine description.  */
static struct constraint_data *first_constraint;
static struct constraint_data **last_constraint_ptr = &first_constraint;

#define FOR_ALL_CONSTRAINTS(iter_) \
  for (iter_ = first_constraint; iter_; iter_ = iter_->next_textual)

/* These letters, and all names beginning with them, are reserved for
   generic constraints.  */
static const char generic_constraint_letters[] = "EFVXgimnoprs";

/* Machine-independent code expects that constraints with these
   (initial) letters will allow only (a subset of all) CONST_INTs.  */

static const char const_int_constraints[] = "IJKLMNOP";

/* Machine-independent code expects that constraints with these
   (initial) letters will allow only (a subset of all) CONST_DOUBLEs.  */

static const char const_dbl_constraints[] = "GH";

/* Summary data used to decide whether to output various functions and
   macro definitions.  */
static unsigned int constraint_max_namelen;
static bool have_register_constraints;
static bool have_memory_constraints;
static bool have_address_constraints;
static bool have_extra_constraints;
static bool have_const_int_constraints;
static bool have_const_dbl_constraints;

/* Convert NAME, which contains angle brackets and/or underscores, to
   a string that can be used as part of a C identifier.  The string
   comes from the rtl_obstack.  */
static const char *
mangle (const char *name)
{
  for (; *name; name++)
    switch (*name)
      {
      case '_': obstack_grow (rtl_obstack, "__", 2); break;
      case '<':	obstack_grow (rtl_obstack, "_l", 2); break;
      case '>':	obstack_grow (rtl_obstack, "_g", 2); break;
      default: obstack_1grow (rtl_obstack, *name); break;
      }

  obstack_1grow (rtl_obstack, '\0');
  return obstack_finish (rtl_obstack);
}

/* Add one constraint, of any sort, to the tables.  NAME is its name;
   REGCLASS is the register class, if any; EXP is the expression to
   test, if any;  IS_MEMORY and IS_ADDRESS indicate memory and address
   constraints, respectively; LINENO is the line number from the MD reader.
   Not all combinations of arguments are valid; most importantly, REGCLASS
   is mutually exclusive with EXP, and IS_MEMORY/IS_ADDRESS are only
   meaningful for constraints with EXP.

   This function enforces all syntactic and semantic rules about what
   constraints can be defined.  */

static void
add_constraint (const char *name, const char *regclass,
		rtx exp, bool is_memory, bool is_address,
		int lineno)
{
  struct constraint_data *c, **iter, **slot;
  const char *p;
  bool need_mangled_name = false;
  bool is_const_int;
  bool is_const_dbl;
  size_t namelen;

  if (exp && validate_exp (exp, name, lineno))
    return;

  if (!ISALPHA (name[0]) && name[0] != '_')
    {
      if (name[1] == '\0')
	message_with_line (lineno, "constraint name '%s' is not "
			   "a letter or underscore", name);
      else
	message_with_line (lineno, "constraint name '%s' does not begin "
			   "with a letter or underscore", name);
      have_error = 1;
      return;
    }
  for (p = name; *p; p++)
    if (!ISALNUM (*p))
      {
	if (*p == '<' || *p == '>' || *p == '_')
	  need_mangled_name = true;
	else
	  {
	    message_with_line (lineno,
			       "constraint name '%s' must be composed of "
			       "letters, digits, underscores, and "
			       "angle brackets", name);
	    have_error = 1;
	    return;
	  }
      }

  if (strchr (generic_constraint_letters, name[0]))
    {
      if (name[1] == '\0')
	message_with_line (lineno, "constraint letter '%s' cannot be "
			   "redefined by the machine description", name);
      else
	message_with_line (lineno, "constraint name '%s' cannot be defined by "
			   "the machine description, as it begins with '%c'",
			   name, name[0]);
      have_error = 1;
      return;
    }

  
  namelen = strlen (name);
  slot = &constraints_by_letter_table[(unsigned int)name[0]];
  for (iter = slot; *iter; iter = &(*iter)->next_this_letter)
    {
      /* This causes slot to end up pointing to the
	 next_this_letter field of the last constraint with a name
	 of equal or greater length than the new constraint; hence
	 the new constraint will be inserted after all previous
	 constraints with names of the same length.  */
      if ((*iter)->namelen >= namelen)
	slot = iter;

      if (!strcmp ((*iter)->name, name))
	{
	  message_with_line (lineno, "redefinition of constraint '%s'", name);
	  message_with_line ((*iter)->lineno, "previous definition is here");
	  have_error = 1;
	  return;
	}
      else if (!strncmp ((*iter)->name, name, (*iter)->namelen))
	{
	  message_with_line (lineno, "defining constraint '%s' here", name);
	  message_with_line ((*iter)->lineno, "renders constraint '%s' "
			     "(defined here) a prefix", (*iter)->name);
	  have_error = 1;
	  return;
	}
      else if (!strncmp ((*iter)->name, name, namelen))
	{
	  message_with_line (lineno, "constraint '%s' is a prefix", name);
	  message_with_line ((*iter)->lineno, "of constraint '%s' "
			     "(defined here)", (*iter)->name);
	  have_error = 1;
	  return;
	}
    }

  is_const_int = strchr (const_int_constraints, name[0]) != 0;
  is_const_dbl = strchr (const_dbl_constraints, name[0]) != 0;

  if (is_const_int || is_const_dbl)
    {
      enum rtx_code appropriate_code
	= is_const_int ? CONST_INT : CONST_DOUBLE;

      /* Consider relaxing this requirement in the future.  */
      if (regclass
	  || GET_CODE (exp) != AND
	  || GET_CODE (XEXP (exp, 0)) != MATCH_CODE
	  || strcmp (XSTR (XEXP (exp, 0), 0),
		     GET_RTX_NAME (appropriate_code)))
	{
	  if (name[1] == '\0')
	    message_with_line (lineno, "constraint letter '%c' is reserved "
			       "for %s constraints",
			       name[0], GET_RTX_NAME (appropriate_code));
	  else
	    message_with_line (lineno, "constraint names beginning with '%c' "
			       "(%s) are reserved for %s constraints",
			       name[0], name, 
			       GET_RTX_NAME (appropriate_code));

	  have_error = 1;
	  return;
	}

      if (is_memory)
	{
	  if (name[1] == '\0')
	    message_with_line (lineno, "constraint letter '%c' cannot be a "
			       "memory constraint", name[0]);
	  else
	    message_with_line (lineno, "constraint name '%s' begins with '%c', "
			       "and therefore cannot be a memory constraint",
			       name, name[0]);

	  have_error = 1;
	  return;
	}
      else if (is_address)
	{
	  if (name[1] == '\0')
	    message_with_line (lineno, "constraint letter '%c' cannot be a "
			       "memory constraint", name[0]);
	  else
	    message_with_line (lineno, "constraint name '%s' begins with '%c', "
			       "and therefore cannot be a memory constraint",
			       name, name[0]);

	  have_error = 1;
	  return;
	}
    }

  
  c = obstack_alloc (rtl_obstack, sizeof (struct constraint_data));
  c->name = name;
  c->c_name = need_mangled_name ? mangle (name) : name;
  c->lineno = lineno;
  c->namelen = namelen;
  c->regclass = regclass;
  c->exp = exp;
  c->is_register = regclass != 0;
  c->is_const_int = is_const_int;
  c->is_const_dbl = is_const_dbl;
  c->is_extra = !(regclass || is_const_int || is_const_dbl);
  c->is_memory = is_memory;
  c->is_address = is_address;

  c->next_this_letter = *slot;
  *slot = c;

  /* Insert this constraint in the list of all constraints in textual
     order.  */
  c->next_textual = 0;
  *last_constraint_ptr = c;
  last_constraint_ptr = &c->next_textual;

  constraint_max_namelen = MAX (constraint_max_namelen, strlen (name));
  have_register_constraints |= c->is_register;
  have_const_int_constraints |= c->is_const_int;
  have_const_dbl_constraints |= c->is_const_dbl;
  have_extra_constraints |= c->is_extra;
  have_memory_constraints |= c->is_memory;
  have_address_constraints |= c->is_address;
}

/* Process a DEFINE_CONSTRAINT, DEFINE_MEMORY_CONSTRAINT, or
   DEFINE_ADDRESS_CONSTRAINT expression, C.  */
static void
process_define_constraint (rtx c, int lineno)
{
  add_constraint (XSTR (c, 0), 0, XEXP (c, 2),
		  GET_CODE (c) == DEFINE_MEMORY_CONSTRAINT,
		  GET_CODE (c) == DEFINE_ADDRESS_CONSTRAINT,
		  lineno);
}

/* Process a DEFINE_REGISTER_CONSTRAINT expression, C.  */
static void
process_define_register_constraint (rtx c, int lineno)
{
  add_constraint (XSTR (c, 0), XSTR (c, 1), 0, false, false, lineno);
}

/* Write out an enumeration with one entry per machine-specific
   constraint.  */
static void
write_enum_constraint_num (void)
{
  struct constraint_data *c;

  fputs ("enum constraint_num\n"
	 "{\n"
	 "  CONSTRAINT__UNKNOWN = 0", stdout);
  FOR_ALL_CONSTRAINTS (c)
    printf (",\n  CONSTRAINT_%s", c->c_name);
  puts ("\n};\n");
}

/* Write out a function which looks at a string and determines what
   constraint name, if any, it begins with.  */
static void
write_lookup_constraint (void)
{
  unsigned int i;
  puts ("enum constraint_num\n"
	"lookup_constraint (const char *str)\n"
	"{\n"
	"  switch (str[0])\n"
	"    {");

  for (i = 0; i < ARRAY_SIZE(constraints_by_letter_table); i++)
    {
      struct constraint_data *c = constraints_by_letter_table[i];
      if (!c)
	continue;

      printf ("    case '%c':\n", i);
      if (c->namelen == 1)
	printf ("      return CONSTRAINT_%s;\n", c->c_name);
      else
	{
	  do
	    {
	      printf ("      if (!strncmp (str, \"%s\", %lu))\n"
		      "        return CONSTRAINT_%s;\n",
		      c->name, (unsigned long int) c->namelen, c->c_name);
	      c = c->next_this_letter;
	    }
	  while (c);
	  puts ("      break;");
	}
    }

  puts ("    default: break;\n"
	"    }\n"
	"  return CONSTRAINT__UNKNOWN;\n"
	"}\n");
}

/* Write out the function which computes constraint name lengths from
   their enumerators. */
static void
write_insn_constraint_len (void)
{
  struct constraint_data *c;

  if (constraint_max_namelen == 1)
    return;

  puts ("size_t\n"
	"insn_constraint_len (enum constraint_num c)\n"
	"{\n"
	"  switch (c)\n"
	"    {");

  FOR_ALL_CONSTRAINTS (c)
    if (c->namelen > 1)
      printf ("    case CONSTRAINT_%s: return %lu;\n", c->c_name,
	      (unsigned long int) c->namelen);

  puts ("    default: break;\n"
	"    }\n"
	"  return 1;\n"
	"}\n");
}
  
/* Write out the function which computes the register class corresponding
   to a register constraint.  */
static void
write_regclass_for_constraint (void)
{
  struct constraint_data *c;

  puts ("enum reg_class\n"
	"regclass_for_constraint (enum constraint_num c)\n"
	"{\n"
	"  switch (c)\n"
	"    {");

  FOR_ALL_CONSTRAINTS (c)
    if (c->is_register)
      printf ("    case CONSTRAINT_%s: return %s;\n", c->c_name, c->regclass);

  puts ("    default: break;\n"
	"    }\n"
	"  return NO_REGS;\n"
	"}\n");
}

/* Write out the functions which compute whether a given value matches
   a given non-register constraint.  */
static void
write_tm_constrs_h (void)
{
  struct constraint_data *c;

  printf ("\
/* Generated automatically by the program '%s'\n\
   from the machine description file '%s'.  */\n\n", progname, in_fname);

  puts ("\
#ifndef GCC_TM_CONSTRS_H\n\
#define GCC_TM_CONSTRS_H\n");

  FOR_ALL_CONSTRAINTS (c)
    if (!c->is_register)
      {
	bool needs_ival = needs_variable (c->exp, "ival");
	bool needs_hval = needs_variable (c->exp, "hval");
	bool needs_lval = needs_variable (c->exp, "lval");
	bool needs_rval = needs_variable (c->exp, "rval");
	bool needs_mode = (needs_variable (c->exp, "mode")
			   || needs_hval || needs_lval || needs_rval);
	bool needs_op = (needs_variable (c->exp, "op")
			 || needs_ival || needs_mode);

	printf ("static inline bool\n"
		"satisfies_constraint_%s (rtx %s)\n"
		"{\n", c->c_name,
		needs_op ? "op" : "ARG_UNUSED (op)");
	if (needs_mode)
	  puts ("enum machine_mode mode = GET_MODE (op);");
	if (needs_ival)
	  puts ("  HOST_WIDE_INT ival = 0;");
	if (needs_hval)
	  puts ("  HOST_WIDE_INT hval = 0;");
	if (needs_lval)
	  puts ("  unsigned HOST_WIDE_INT lval = 0;");
	if (needs_rval)
	  puts ("  const REAL_VALUE_TYPE *rval = 0;");

	if (needs_ival)
	  puts ("  if (GET_CODE (op) == CONST_INT)\n"
		"    ival = INTVAL (op);");
	if (needs_hval)
	  puts ("  if (GET_CODE (op) == CONST_DOUBLE && mode == VOIDmode)"
		"    hval = CONST_DOUBLE_HIGH (op);");
	if (needs_lval)
	  puts ("  if (GET_CODE (op) == CONST_DOUBLE && mode == VOIDmode)"
		"    lval = CONST_DOUBLE_LOW (op);");
	if (needs_rval)
	  puts ("  if (GET_CODE (op) == CONST_DOUBLE && mode != VOIDmode)"
		"    rval = CONST_DOUBLE_REAL_VALUE (op);");

	write_predicate_stmts (c->exp);
	fputs ("}\n", stdout);
      }
  puts ("#endif /* tm-constrs.h */");
}

/* Write out the wrapper function, constraint_satisfied_p, that maps
   a CONSTRAINT_xxx constant to one of the predicate functions generated
   above.  */
static void
write_constraint_satisfied_p (void)
{
  struct constraint_data *c;

  puts ("bool\n"
	"constraint_satisfied_p (rtx op, enum constraint_num c)\n"
	"{\n"
	"  switch (c)\n"
	"    {");

  FOR_ALL_CONSTRAINTS (c)
    if (!c->is_register)
      printf ("    case CONSTRAINT_%s: "
	      "return satisfies_constraint_%s (op);\n",
	      c->c_name, c->c_name);

  puts ("    default: break;\n"
	"    }\n"
	"  return false;\n"
	"}\n");
}

/* Write out the function which computes whether a given value matches
   a given CONST_INT constraint.  This doesn't just forward to
   constraint_satisfied_p because caller passes the INTVAL, not the RTX.  */
static void
write_insn_const_int_ok_for_constraint (void)
{
  struct constraint_data *c;

  puts ("bool\n"
	"insn_const_int_ok_for_constraint (HOST_WIDE_INT ival, "
	                                  "enum constraint_num c)\n"
	"{\n"
	"  switch (c)\n"
	"    {");

  FOR_ALL_CONSTRAINTS (c)
    if (c->is_const_int)
      {
	printf ("    case CONSTRAINT_%s:\n      return ", c->c_name);
	/* c->exp is guaranteed to be (and (match_code "const_int") (...));
	   we know at this point that we have a const_int, so we need not
	   bother with that part of the test.  */
	write_predicate_expr (XEXP (c->exp, 1));
	fputs (";\n\n", stdout);
      }

  puts ("    default: break;\n"
	"    }\n"
	"  return false;\n"
	"}\n");
}


/* Write out the function which computes whether a given constraint is
   a memory constraint.  */
static void
write_insn_extra_memory_constraint (void)
{
  struct constraint_data *c;

  puts ("bool\n"
	"insn_extra_memory_constraint (enum constraint_num c)\n"
	"{\n"
	"  switch (c)\n"
	"    {");

  FOR_ALL_CONSTRAINTS (c)
    if (c->is_memory)
      printf ("    case CONSTRAINT_%s:\n      return true;\n\n", c->c_name);

  puts ("    default: break;\n"
	"    }\n"
	"  return false;\n"
	"}\n");
}

/* Write out the function which computes whether a given constraint is
   an address constraint.  */
static void
write_insn_extra_address_constraint (void)
{
  struct constraint_data *c;

  puts ("bool\n"
	"insn_extra_address_constraint (enum constraint_num c)\n"
	"{\n"
	"  switch (c)\n"
	"    {");

  FOR_ALL_CONSTRAINTS (c)
    if (c->is_address)
      printf ("    case CONSTRAINT_%s:\n      return true;\n\n", c->c_name);

  puts ("    default: break;\n"
	"    }\n"
	"  return false;\n"
	"}\n");
}


/* Write tm-preds.h.  Unfortunately, it is impossible to forward-declare
   an enumeration in portable C, so we have to condition all these
   prototypes on HAVE_MACHINE_MODES.  */
static void
write_tm_preds_h (void)
{
  struct pred_data *p;

  printf ("\
/* Generated automatically by the program '%s'\n\
   from the machine description file '%s'.  */\n\n", progname, in_fname);

  puts ("\
#ifndef GCC_TM_PREDS_H\n\
#define GCC_TM_PREDS_H\n\
\n\
#ifdef HAVE_MACHINE_MODES");

  FOR_ALL_PREDICATES (p)
    printf ("extern int %s (rtx, enum machine_mode);\n", p->name);

  puts ("#endif /* HAVE_MACHINE_MODES */\n");

  if (constraint_max_namelen > 0)
    {
      write_enum_constraint_num ();
      puts ("extern enum constraint_num lookup_constraint (const char *);\n"
	    "extern bool constraint_satisfied_p (rtx, enum constraint_num);\n");

      if (constraint_max_namelen > 1)
	puts ("extern size_t insn_constraint_len (enum constraint_num);\n"
	      "#define CONSTRAINT_LEN(c_,s_) "
	      "insn_constraint_len (lookup_constraint (s_))\n");
      else
	puts ("#define CONSTRAINT_LEN(c_,s_) 1\n");
      if (have_register_constraints)
	puts ("extern enum reg_class regclass_for_constraint "
	      "(enum constraint_num);\n"
	      "#define REG_CLASS_FROM_CONSTRAINT(c_,s_) \\\n"
	      "    regclass_for_constraint (lookup_constraint (s_))\n");
      else
	puts ("#define REG_CLASS_FROM_CONSTRAINT(c_,s_) NO_REGS");
      if (have_const_int_constraints)
	puts ("extern bool insn_const_int_ok_for_constraint "
	      "(HOST_WIDE_INT, enum constraint_num);\n"
	      "#define CONST_OK_FOR_CONSTRAINT_P(v_,c_,s_) \\\n"
	      "    insn_const_int_ok_for_constraint (v_, "
	      "lookup_constraint (s_))\n");
      if (have_const_dbl_constraints)
	puts ("#define CONST_DOUBLE_OK_FOR_CONSTRAINT_P(v_,c_,s_) \\\n"
	      "    constraint_satisfied_p (v_, lookup_constraint (s_))\n");
      else
	puts ("#define CONST_DOUBLE_OK_FOR_CONSTRAINT_P(v_,c_,s_) 0\n");
      if (have_extra_constraints)
	puts ("#define EXTRA_CONSTRAINT_STR(v_,c_,s_) \\\n"
	      "    constraint_satisfied_p (v_, lookup_constraint (s_))\n");
      if (have_memory_constraints)
	puts ("extern bool "
	      "insn_extra_memory_constraint (enum constraint_num);\n"
	      "#define EXTRA_MEMORY_CONSTRAINT(c_,s_) "
	      "insn_extra_memory_constraint (lookup_constraint (s_))\n");
      else
	puts ("#define EXTRA_MEMORY_CONSTRAINT(c_,s_) false\n");
      if (have_address_constraints)
	puts ("extern bool "
	      "insn_extra_address_constraint (enum constraint_num);\n"
	      "#define EXTRA_ADDRESS_CONSTRAINT(c_,s_) "
	      "insn_extra_address_constraint (lookup_constraint (s_))\n");
      else
	puts ("#define EXTRA_ADDRESS_CONSTRAINT(c_,s_) false\n");
    }

  puts ("#endif /* tm-preds.h */");
}

/* Write insn-preds.c.  
   N.B. the list of headers to include was copied from genrecog; it
   may not be ideal.

   FUTURE: Write #line markers referring back to the machine
   description.  (Can't practically do this now since we don't know
   the line number of the C block - just the line number of the enclosing
   expression.)  */
static void
write_insn_preds_c (void)
{
  struct pred_data *p;

  printf ("\
/* Generated automatically by the program '%s'\n\
   from the machine description file '%s'.  */\n\n", progname, in_fname);

  puts ("\
#include \"config.h\"\n\
#include \"system.h\"\n\
#include \"coretypes.h\"\n\
#include \"tm.h\"\n\
#include \"rtl.h\"\n\
#include \"tree.h\"\n\
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
#include \"regs.h\"\n\
#include \"tm-constrs.h\"\n");

  FOR_ALL_PREDICATES (p)
    write_one_predicate_function (p);

  if (constraint_max_namelen > 0)
    {
      write_lookup_constraint ();
      write_regclass_for_constraint ();
      write_constraint_satisfied_p ();
      
      if (constraint_max_namelen > 1)
	write_insn_constraint_len ();

      if (have_const_int_constraints)
	write_insn_const_int_ok_for_constraint ();

      if (have_memory_constraints)
	write_insn_extra_memory_constraint ();
      if (have_address_constraints)
	write_insn_extra_address_constraint ();
    }
}

/* Argument parsing.  */
static bool gen_header;
static bool gen_constrs;

static bool
parse_option (const char *opt)
{
  if (!strcmp (opt, "-h"))
    {
      gen_header = true;
      return 1;
    }
  else if (!strcmp (opt, "-c"))
    {
      gen_constrs = true;
      return 1;
    }
  else
    return 0;
}

/* Master control.  */
int
main (int argc, char **argv)
{
  rtx defn;
  int pattern_lineno, next_insn_code = 0;

  progname = argv[0];
  if (argc <= 1)
    fatal ("no input file name");
  if (init_md_reader_args_cb (argc, argv, parse_option) != SUCCESS_EXIT_CODE)
    return FATAL_EXIT_CODE;

  while ((defn = read_md_rtx (&pattern_lineno, &next_insn_code)) != 0)
    switch (GET_CODE (defn))
      {
      case DEFINE_PREDICATE:
      case DEFINE_SPECIAL_PREDICATE:
	process_define_predicate (defn, pattern_lineno);
	break;

      case DEFINE_CONSTRAINT:
      case DEFINE_MEMORY_CONSTRAINT:
      case DEFINE_ADDRESS_CONSTRAINT:
	process_define_constraint (defn, pattern_lineno);
	break;

      case DEFINE_REGISTER_CONSTRAINT:
	process_define_register_constraint (defn, pattern_lineno);
	break;

      default:
	break;
      }

  if (gen_header)
    write_tm_preds_h ();
  else if (gen_constrs)
    write_tm_constrs_h ();
  else
    write_insn_preds_c ();

  if (have_error || ferror (stdout) || fflush (stdout) || fclose (stdout))
    return FATAL_EXIT_CODE;

  return SUCCESS_EXIT_CODE;
}
