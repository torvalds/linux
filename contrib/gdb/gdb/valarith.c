/* Perform arithmetic and other operations on values, for GDB.

   Copyright 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003 Free Software
   Foundation, Inc.

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
#include "value.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "target.h"
#include "language.h"
#include "gdb_string.h"
#include "doublest.h"
#include <math.h>
#include "infcall.h"

/* Define whether or not the C operator '/' truncates towards zero for
   differently signed operands (truncation direction is undefined in C). */

#ifndef TRUNCATION_TOWARDS_ZERO
#define TRUNCATION_TOWARDS_ZERO ((-5 / 2) == -2)
#endif

static struct value *value_subscripted_rvalue (struct value *, struct value *, int);

void _initialize_valarith (void);


/* Given a pointer, return the size of its target.
   If the pointer type is void *, then return 1.
   If the target type is incomplete, then error out.
   This isn't a general purpose function, but just a 
   helper for value_sub & value_add.
*/

static LONGEST
find_size_for_pointer_math (struct type *ptr_type)
{
  LONGEST sz = -1;
  struct type *ptr_target;

  ptr_target = check_typedef (TYPE_TARGET_TYPE (ptr_type));

  sz = TYPE_LENGTH (ptr_target);
  if (sz == 0)
    {
      if (TYPE_CODE (ptr_type) == TYPE_CODE_VOID)
	sz = 1;
      else
	{
	  char *name;
	  
	  name = TYPE_NAME (ptr_target);
	  if (name == NULL)
	    name = TYPE_TAG_NAME (ptr_target);
	  if (name == NULL)
	    error ("Cannot perform pointer math on incomplete types, "
		   "try casting to a known type, or void *.");
	  else
	    error ("Cannot perform pointer math on incomplete type \"%s\", "
		   "try casting to a known type, or void *.", name);
	}
    }
  return sz;
}

struct value *
value_add (struct value *arg1, struct value *arg2)
{
  struct value *valint;
  struct value *valptr;
  LONGEST sz;
  struct type *type1, *type2, *valptrtype;

  COERCE_NUMBER (arg1);
  COERCE_NUMBER (arg2);
  type1 = check_typedef (VALUE_TYPE (arg1));
  type2 = check_typedef (VALUE_TYPE (arg2));

  if ((TYPE_CODE (type1) == TYPE_CODE_PTR
       || TYPE_CODE (type2) == TYPE_CODE_PTR)
      &&
      (TYPE_CODE (type1) == TYPE_CODE_INT
       || TYPE_CODE (type2) == TYPE_CODE_INT))
    /* Exactly one argument is a pointer, and one is an integer.  */
    {
      struct value *retval;

      if (TYPE_CODE (type1) == TYPE_CODE_PTR)
	{
	  valptr = arg1;
	  valint = arg2;
	  valptrtype = type1;
	}
      else
	{
	  valptr = arg2;
	  valint = arg1;
	  valptrtype = type2;
	}

      sz = find_size_for_pointer_math (valptrtype);

      retval = value_from_pointer (valptrtype,
				   value_as_address (valptr)
				   + (sz * value_as_long (valint)));
      VALUE_BFD_SECTION (retval) = VALUE_BFD_SECTION (valptr);
      return retval;
    }

  return value_binop (arg1, arg2, BINOP_ADD);
}

struct value *
value_sub (struct value *arg1, struct value *arg2)
{
  struct type *type1, *type2;
  COERCE_NUMBER (arg1);
  COERCE_NUMBER (arg2);
  type1 = check_typedef (VALUE_TYPE (arg1));
  type2 = check_typedef (VALUE_TYPE (arg2));

  if (TYPE_CODE (type1) == TYPE_CODE_PTR)
    {
      if (TYPE_CODE (type2) == TYPE_CODE_INT)
	{
	  /* pointer - integer.  */
	  LONGEST sz = find_size_for_pointer_math (type1);

	  return value_from_pointer (type1,
				     (value_as_address (arg1)
				      - (sz * value_as_long (arg2))));
	}
      else if (TYPE_CODE (type2) == TYPE_CODE_PTR
	       && TYPE_LENGTH (check_typedef (TYPE_TARGET_TYPE (type1)))
	       == TYPE_LENGTH (check_typedef (TYPE_TARGET_TYPE (type2))))
	{
	  /* pointer to <type x> - pointer to <type x>.  */
	  LONGEST sz = TYPE_LENGTH (check_typedef (TYPE_TARGET_TYPE (type1)));
	  return value_from_longest
	    (builtin_type_long,	/* FIXME -- should be ptrdiff_t */
	     (value_as_long (arg1) - value_as_long (arg2)) / sz);
	}
      else
	{
	  error ("\
First argument of `-' is a pointer and second argument is neither\n\
an integer nor a pointer of the same type.");
	}
    }

  return value_binop (arg1, arg2, BINOP_SUB);
}

/* Return the value of ARRAY[IDX].
   See comments in value_coerce_array() for rationale for reason for
   doing lower bounds adjustment here rather than there.
   FIXME:  Perhaps we should validate that the index is valid and if
   verbosity is set, warn about invalid indices (but still use them). */

struct value *
value_subscript (struct value *array, struct value *idx)
{
  struct value *bound;
  int c_style = current_language->c_style_arrays;
  struct type *tarray;

  COERCE_REF (array);
  tarray = check_typedef (VALUE_TYPE (array));
  COERCE_VARYING_ARRAY (array, tarray);

  if (TYPE_CODE (tarray) == TYPE_CODE_ARRAY
      || TYPE_CODE (tarray) == TYPE_CODE_STRING)
    {
      struct type *range_type = TYPE_INDEX_TYPE (tarray);
      LONGEST lowerbound, upperbound;
      get_discrete_bounds (range_type, &lowerbound, &upperbound);

      if (VALUE_LVAL (array) != lval_memory)
	return value_subscripted_rvalue (array, idx, lowerbound);

      if (c_style == 0)
	{
	  LONGEST index = value_as_long (idx);
	  if (index >= lowerbound && index <= upperbound)
	    return value_subscripted_rvalue (array, idx, lowerbound);
	  warning ("array or string index out of range");
	  /* fall doing C stuff */
	  c_style = 1;
	}

      if (lowerbound != 0)
	{
	  bound = value_from_longest (builtin_type_int, (LONGEST) lowerbound);
	  idx = value_sub (idx, bound);
	}

      array = value_coerce_array (array);
    }

  if (TYPE_CODE (tarray) == TYPE_CODE_BITSTRING)
    {
      struct type *range_type = TYPE_INDEX_TYPE (tarray);
      LONGEST index = value_as_long (idx);
      struct value *v;
      int offset, byte, bit_index;
      LONGEST lowerbound, upperbound;
      get_discrete_bounds (range_type, &lowerbound, &upperbound);
      if (index < lowerbound || index > upperbound)
	error ("bitstring index out of range");
      index -= lowerbound;
      offset = index / TARGET_CHAR_BIT;
      byte = *((char *) VALUE_CONTENTS (array) + offset);
      bit_index = index % TARGET_CHAR_BIT;
      byte >>= (BITS_BIG_ENDIAN ? TARGET_CHAR_BIT - 1 - bit_index : bit_index);
      v = value_from_longest (LA_BOOL_TYPE, byte & 1);
      VALUE_BITPOS (v) = bit_index;
      VALUE_BITSIZE (v) = 1;
      VALUE_LVAL (v) = VALUE_LVAL (array);
      if (VALUE_LVAL (array) == lval_internalvar)
	VALUE_LVAL (v) = lval_internalvar_component;
      VALUE_ADDRESS (v) = VALUE_ADDRESS (array);
      VALUE_OFFSET (v) = offset + VALUE_OFFSET (array);
      return v;
    }

  if (c_style)
    return value_ind (value_add (array, idx));
  else
    error ("not an array or string");
}

/* Return the value of EXPR[IDX], expr an aggregate rvalue
   (eg, a vector register).  This routine used to promote floats
   to doubles, but no longer does.  */

static struct value *
value_subscripted_rvalue (struct value *array, struct value *idx, int lowerbound)
{
  struct type *array_type = check_typedef (VALUE_TYPE (array));
  struct type *elt_type = check_typedef (TYPE_TARGET_TYPE (array_type));
  unsigned int elt_size = TYPE_LENGTH (elt_type);
  LONGEST index = value_as_long (idx);
  unsigned int elt_offs = elt_size * longest_to_int (index - lowerbound);
  struct value *v;

  if (index < lowerbound || elt_offs >= TYPE_LENGTH (array_type))
    error ("no such vector element");

  v = allocate_value (elt_type);
  if (VALUE_LAZY (array))
    VALUE_LAZY (v) = 1;
  else
    memcpy (VALUE_CONTENTS (v), VALUE_CONTENTS (array) + elt_offs, elt_size);

  if (VALUE_LVAL (array) == lval_internalvar)
    VALUE_LVAL (v) = lval_internalvar_component;
  else
    VALUE_LVAL (v) = VALUE_LVAL (array);
  VALUE_ADDRESS (v) = VALUE_ADDRESS (array);
  VALUE_REGNO (v) = VALUE_REGNO (array);
  VALUE_OFFSET (v) = VALUE_OFFSET (array) + elt_offs;
  return v;
}

/* Check to see if either argument is a structure.  This is called so
   we know whether to go ahead with the normal binop or look for a 
   user defined function instead.

   For now, we do not overload the `=' operator.  */

int
binop_user_defined_p (enum exp_opcode op, struct value *arg1, struct value *arg2)
{
  struct type *type1, *type2;
  if (op == BINOP_ASSIGN || op == BINOP_CONCAT)
    return 0;
  type1 = check_typedef (VALUE_TYPE (arg1));
  type2 = check_typedef (VALUE_TYPE (arg2));
  return (TYPE_CODE (type1) == TYPE_CODE_STRUCT
	  || TYPE_CODE (type2) == TYPE_CODE_STRUCT
	  || (TYPE_CODE (type1) == TYPE_CODE_REF
	      && TYPE_CODE (TYPE_TARGET_TYPE (type1)) == TYPE_CODE_STRUCT)
	  || (TYPE_CODE (type2) == TYPE_CODE_REF
	      && TYPE_CODE (TYPE_TARGET_TYPE (type2)) == TYPE_CODE_STRUCT));
}

/* Check to see if argument is a structure.  This is called so
   we know whether to go ahead with the normal unop or look for a 
   user defined function instead.

   For now, we do not overload the `&' operator.  */

int
unop_user_defined_p (enum exp_opcode op, struct value *arg1)
{
  struct type *type1;
  if (op == UNOP_ADDR)
    return 0;
  type1 = check_typedef (VALUE_TYPE (arg1));
  for (;;)
    {
      if (TYPE_CODE (type1) == TYPE_CODE_STRUCT)
	return 1;
      else if (TYPE_CODE (type1) == TYPE_CODE_REF)
	type1 = TYPE_TARGET_TYPE (type1);
      else
	return 0;
    }
}

/* We know either arg1 or arg2 is a structure, so try to find the right
   user defined function.  Create an argument vector that calls 
   arg1.operator @ (arg1,arg2) and return that value (where '@' is any
   binary operator which is legal for GNU C++).

   OP is the operatore, and if it is BINOP_ASSIGN_MODIFY, then OTHEROP
   is the opcode saying how to modify it.  Otherwise, OTHEROP is
   unused.  */

struct value *
value_x_binop (struct value *arg1, struct value *arg2, enum exp_opcode op,
	       enum exp_opcode otherop, enum noside noside)
{
  struct value **argvec;
  char *ptr;
  char tstr[13];
  int static_memfuncp;

  COERCE_REF (arg1);
  COERCE_REF (arg2);
  COERCE_ENUM (arg1);
  COERCE_ENUM (arg2);

  /* now we know that what we have to do is construct our
     arg vector and find the right function to call it with.  */

  if (TYPE_CODE (check_typedef (VALUE_TYPE (arg1))) != TYPE_CODE_STRUCT)
    error ("Can't do that binary op on that type");	/* FIXME be explicit */

  argvec = (struct value **) alloca (sizeof (struct value *) * 4);
  argvec[1] = value_addr (arg1);
  argvec[2] = arg2;
  argvec[3] = 0;

  /* make the right function name up */
  strcpy (tstr, "operator__");
  ptr = tstr + 8;
  switch (op)
    {
    case BINOP_ADD:
      strcpy (ptr, "+");
      break;
    case BINOP_SUB:
      strcpy (ptr, "-");
      break;
    case BINOP_MUL:
      strcpy (ptr, "*");
      break;
    case BINOP_DIV:
      strcpy (ptr, "/");
      break;
    case BINOP_REM:
      strcpy (ptr, "%");
      break;
    case BINOP_LSH:
      strcpy (ptr, "<<");
      break;
    case BINOP_RSH:
      strcpy (ptr, ">>");
      break;
    case BINOP_BITWISE_AND:
      strcpy (ptr, "&");
      break;
    case BINOP_BITWISE_IOR:
      strcpy (ptr, "|");
      break;
    case BINOP_BITWISE_XOR:
      strcpy (ptr, "^");
      break;
    case BINOP_LOGICAL_AND:
      strcpy (ptr, "&&");
      break;
    case BINOP_LOGICAL_OR:
      strcpy (ptr, "||");
      break;
    case BINOP_MIN:
      strcpy (ptr, "<?");
      break;
    case BINOP_MAX:
      strcpy (ptr, ">?");
      break;
    case BINOP_ASSIGN:
      strcpy (ptr, "=");
      break;
    case BINOP_ASSIGN_MODIFY:
      switch (otherop)
	{
	case BINOP_ADD:
	  strcpy (ptr, "+=");
	  break;
	case BINOP_SUB:
	  strcpy (ptr, "-=");
	  break;
	case BINOP_MUL:
	  strcpy (ptr, "*=");
	  break;
	case BINOP_DIV:
	  strcpy (ptr, "/=");
	  break;
	case BINOP_REM:
	  strcpy (ptr, "%=");
	  break;
	case BINOP_BITWISE_AND:
	  strcpy (ptr, "&=");
	  break;
	case BINOP_BITWISE_IOR:
	  strcpy (ptr, "|=");
	  break;
	case BINOP_BITWISE_XOR:
	  strcpy (ptr, "^=");
	  break;
	case BINOP_MOD:	/* invalid */
	default:
	  error ("Invalid binary operation specified.");
	}
      break;
    case BINOP_SUBSCRIPT:
      strcpy (ptr, "[]");
      break;
    case BINOP_EQUAL:
      strcpy (ptr, "==");
      break;
    case BINOP_NOTEQUAL:
      strcpy (ptr, "!=");
      break;
    case BINOP_LESS:
      strcpy (ptr, "<");
      break;
    case BINOP_GTR:
      strcpy (ptr, ">");
      break;
    case BINOP_GEQ:
      strcpy (ptr, ">=");
      break;
    case BINOP_LEQ:
      strcpy (ptr, "<=");
      break;
    case BINOP_MOD:		/* invalid */
    default:
      error ("Invalid binary operation specified.");
    }

  argvec[0] = value_struct_elt (&arg1, argvec + 1, tstr, &static_memfuncp, "structure");

  if (argvec[0])
    {
      if (static_memfuncp)
	{
	  argvec[1] = argvec[0];
	  argvec++;
	}
      if (noside == EVAL_AVOID_SIDE_EFFECTS)
	{
	  struct type *return_type;
	  return_type
	    = TYPE_TARGET_TYPE (check_typedef (VALUE_TYPE (argvec[0])));
	  return value_zero (return_type, VALUE_LVAL (arg1));
	}
      return call_function_by_hand (argvec[0], 2 - static_memfuncp, argvec + 1);
    }
  error ("member function %s not found", tstr);
#ifdef lint
  return call_function_by_hand (argvec[0], 2 - static_memfuncp, argvec + 1);
#endif
}

/* We know that arg1 is a structure, so try to find a unary user
   defined operator that matches the operator in question.  
   Create an argument vector that calls arg1.operator @ (arg1)
   and return that value (where '@' is (almost) any unary operator which
   is legal for GNU C++).  */

struct value *
value_x_unop (struct value *arg1, enum exp_opcode op, enum noside noside)
{
  struct value **argvec;
  char *ptr, *mangle_ptr;
  char tstr[13], mangle_tstr[13];
  int static_memfuncp, nargs;

  COERCE_REF (arg1);
  COERCE_ENUM (arg1);

  /* now we know that what we have to do is construct our
     arg vector and find the right function to call it with.  */

  if (TYPE_CODE (check_typedef (VALUE_TYPE (arg1))) != TYPE_CODE_STRUCT)
    error ("Can't do that unary op on that type");	/* FIXME be explicit */

  argvec = (struct value **) alloca (sizeof (struct value *) * 4);
  argvec[1] = value_addr (arg1);
  argvec[2] = 0;

  nargs = 1;

  /* make the right function name up */
  strcpy (tstr, "operator__");
  ptr = tstr + 8;
  strcpy (mangle_tstr, "__");
  mangle_ptr = mangle_tstr + 2;
  switch (op)
    {
    case UNOP_PREINCREMENT:
      strcpy (ptr, "++");
      break;
    case UNOP_PREDECREMENT:
      strcpy (ptr, "--");
      break;
    case UNOP_POSTINCREMENT:
      strcpy (ptr, "++");
      argvec[2] = value_from_longest (builtin_type_int, 0);
      argvec[3] = 0;
      nargs ++;
      break;
    case UNOP_POSTDECREMENT:
      strcpy (ptr, "--");
      argvec[2] = value_from_longest (builtin_type_int, 0);
      argvec[3] = 0;
      nargs ++;
      break;
    case UNOP_LOGICAL_NOT:
      strcpy (ptr, "!");
      break;
    case UNOP_COMPLEMENT:
      strcpy (ptr, "~");
      break;
    case UNOP_NEG:
      strcpy (ptr, "-");
      break;
    case UNOP_IND:
      strcpy (ptr, "*");
      break;
    default:
      error ("Invalid unary operation specified.");
    }

  argvec[0] = value_struct_elt (&arg1, argvec + 1, tstr, &static_memfuncp, "structure");

  if (argvec[0])
    {
      if (static_memfuncp)
	{
	  argvec[1] = argvec[0];
	  nargs --;
	  argvec++;
	}
      if (noside == EVAL_AVOID_SIDE_EFFECTS)
	{
	  struct type *return_type;
	  return_type
	    = TYPE_TARGET_TYPE (check_typedef (VALUE_TYPE (argvec[0])));
	  return value_zero (return_type, VALUE_LVAL (arg1));
	}
      return call_function_by_hand (argvec[0], nargs, argvec + 1);
    }
  error ("member function %s not found", tstr);
  return 0;			/* For lint -- never reached */
}


/* Concatenate two values with the following conditions:

   (1)  Both values must be either bitstring values or character string
   values and the resulting value consists of the concatenation of
   ARG1 followed by ARG2.

   or

   One value must be an integer value and the other value must be
   either a bitstring value or character string value, which is
   to be repeated by the number of times specified by the integer
   value.


   (2)  Boolean values are also allowed and are treated as bit string
   values of length 1.

   (3)  Character values are also allowed and are treated as character
   string values of length 1.
 */

struct value *
value_concat (struct value *arg1, struct value *arg2)
{
  struct value *inval1;
  struct value *inval2;
  struct value *outval = NULL;
  int inval1len, inval2len;
  int count, idx;
  char *ptr;
  char inchar;
  struct type *type1 = check_typedef (VALUE_TYPE (arg1));
  struct type *type2 = check_typedef (VALUE_TYPE (arg2));

  COERCE_VARYING_ARRAY (arg1, type1);
  COERCE_VARYING_ARRAY (arg2, type2);

  /* First figure out if we are dealing with two values to be concatenated
     or a repeat count and a value to be repeated.  INVAL1 is set to the
     first of two concatenated values, or the repeat count.  INVAL2 is set
     to the second of the two concatenated values or the value to be 
     repeated. */

  if (TYPE_CODE (type2) == TYPE_CODE_INT)
    {
      struct type *tmp = type1;
      type1 = tmp;
      tmp = type2;
      inval1 = arg2;
      inval2 = arg1;
    }
  else
    {
      inval1 = arg1;
      inval2 = arg2;
    }

  /* Now process the input values. */

  if (TYPE_CODE (type1) == TYPE_CODE_INT)
    {
      /* We have a repeat count.  Validate the second value and then
         construct a value repeated that many times. */
      if (TYPE_CODE (type2) == TYPE_CODE_STRING
	  || TYPE_CODE (type2) == TYPE_CODE_CHAR)
	{
	  count = longest_to_int (value_as_long (inval1));
	  inval2len = TYPE_LENGTH (type2);
	  ptr = (char *) alloca (count * inval2len);
	  if (TYPE_CODE (type2) == TYPE_CODE_CHAR)
	    {
	      inchar = (char) unpack_long (type2,
					   VALUE_CONTENTS (inval2));
	      for (idx = 0; idx < count; idx++)
		{
		  *(ptr + idx) = inchar;
		}
	    }
	  else
	    {
	      for (idx = 0; idx < count; idx++)
		{
		  memcpy (ptr + (idx * inval2len), VALUE_CONTENTS (inval2),
			  inval2len);
		}
	    }
	  outval = value_string (ptr, count * inval2len);
	}
      else if (TYPE_CODE (type2) == TYPE_CODE_BITSTRING
	       || TYPE_CODE (type2) == TYPE_CODE_BOOL)
	{
	  error ("unimplemented support for bitstring/boolean repeats");
	}
      else
	{
	  error ("can't repeat values of that type");
	}
    }
  else if (TYPE_CODE (type1) == TYPE_CODE_STRING
	   || TYPE_CODE (type1) == TYPE_CODE_CHAR)
    {
      /* We have two character strings to concatenate. */
      if (TYPE_CODE (type2) != TYPE_CODE_STRING
	  && TYPE_CODE (type2) != TYPE_CODE_CHAR)
	{
	  error ("Strings can only be concatenated with other strings.");
	}
      inval1len = TYPE_LENGTH (type1);
      inval2len = TYPE_LENGTH (type2);
      ptr = (char *) alloca (inval1len + inval2len);
      if (TYPE_CODE (type1) == TYPE_CODE_CHAR)
	{
	  *ptr = (char) unpack_long (type1, VALUE_CONTENTS (inval1));
	}
      else
	{
	  memcpy (ptr, VALUE_CONTENTS (inval1), inval1len);
	}
      if (TYPE_CODE (type2) == TYPE_CODE_CHAR)
	{
	  *(ptr + inval1len) =
	    (char) unpack_long (type2, VALUE_CONTENTS (inval2));
	}
      else
	{
	  memcpy (ptr + inval1len, VALUE_CONTENTS (inval2), inval2len);
	}
      outval = value_string (ptr, inval1len + inval2len);
    }
  else if (TYPE_CODE (type1) == TYPE_CODE_BITSTRING
	   || TYPE_CODE (type1) == TYPE_CODE_BOOL)
    {
      /* We have two bitstrings to concatenate. */
      if (TYPE_CODE (type2) != TYPE_CODE_BITSTRING
	  && TYPE_CODE (type2) != TYPE_CODE_BOOL)
	{
	  error ("Bitstrings or booleans can only be concatenated with other bitstrings or booleans.");
	}
      error ("unimplemented support for bitstring/boolean concatenation.");
    }
  else
    {
      /* We don't know how to concatenate these operands. */
      error ("illegal operands for concatenation.");
    }
  return (outval);
}



/* Perform a binary operation on two operands which have reasonable
   representations as integers or floats.  This includes booleans,
   characters, integers, or floats.
   Does not support addition and subtraction on pointers;
   use value_add or value_sub if you want to handle those possibilities.  */

struct value *
value_binop (struct value *arg1, struct value *arg2, enum exp_opcode op)
{
  struct value *val;
  struct type *type1, *type2;

  COERCE_REF (arg1);
  COERCE_REF (arg2);
  COERCE_ENUM (arg1);
  COERCE_ENUM (arg2);
  type1 = check_typedef (VALUE_TYPE (arg1));
  type2 = check_typedef (VALUE_TYPE (arg2));

  if ((TYPE_CODE (type1) != TYPE_CODE_FLT
       && TYPE_CODE (type1) != TYPE_CODE_CHAR
       && TYPE_CODE (type1) != TYPE_CODE_INT
       && TYPE_CODE (type1) != TYPE_CODE_BOOL
       && TYPE_CODE (type1) != TYPE_CODE_RANGE)
      ||
      (TYPE_CODE (type2) != TYPE_CODE_FLT
       && TYPE_CODE (type2) != TYPE_CODE_CHAR
       && TYPE_CODE (type2) != TYPE_CODE_INT
       && TYPE_CODE (type2) != TYPE_CODE_BOOL
       && TYPE_CODE (type2) != TYPE_CODE_RANGE))
    error ("Argument to arithmetic operation not a number or boolean.");

  if (TYPE_CODE (type1) == TYPE_CODE_FLT
      ||
      TYPE_CODE (type2) == TYPE_CODE_FLT)
    {
      /* FIXME-if-picky-about-floating-accuracy: Should be doing this
         in target format.  real.c in GCC probably has the necessary
         code.  */
      DOUBLEST v1, v2, v = 0;
      v1 = value_as_double (arg1);
      v2 = value_as_double (arg2);
      switch (op)
	{
	case BINOP_ADD:
	  v = v1 + v2;
	  break;

	case BINOP_SUB:
	  v = v1 - v2;
	  break;

	case BINOP_MUL:
	  v = v1 * v2;
	  break;

	case BINOP_DIV:
	  v = v1 / v2;
	  break;

        case BINOP_EXP:
          v = pow (v1, v2);
          if (errno)
            error ("Cannot perform exponentiation: %s", safe_strerror (errno));
          break;

	default:
	  error ("Integer-only operation on floating point number.");
	}

      /* If either arg was long double, make sure that value is also long
         double.  */

      if (TYPE_LENGTH (type1) * 8 > TARGET_DOUBLE_BIT
	  || TYPE_LENGTH (type2) * 8 > TARGET_DOUBLE_BIT)
	val = allocate_value (builtin_type_long_double);
      else
	val = allocate_value (builtin_type_double);

      store_typed_floating (VALUE_CONTENTS_RAW (val), VALUE_TYPE (val), v);
    }
  else if (TYPE_CODE (type1) == TYPE_CODE_BOOL
	   &&
	   TYPE_CODE (type2) == TYPE_CODE_BOOL)
    {
      LONGEST v1, v2, v = 0;
      v1 = value_as_long (arg1);
      v2 = value_as_long (arg2);

      switch (op)
	{
	case BINOP_BITWISE_AND:
	  v = v1 & v2;
	  break;

	case BINOP_BITWISE_IOR:
	  v = v1 | v2;
	  break;

	case BINOP_BITWISE_XOR:
	  v = v1 ^ v2;
          break;
              
        case BINOP_EQUAL:
          v = v1 == v2;
          break;
          
        case BINOP_NOTEQUAL:
          v = v1 != v2;
	  break;

	default:
	  error ("Invalid operation on booleans.");
	}

      val = allocate_value (type1);
      store_signed_integer (VALUE_CONTENTS_RAW (val),
			    TYPE_LENGTH (type1),
			    v);
    }
  else
    /* Integral operations here.  */
    /* FIXME:  Also mixed integral/booleans, with result an integer. */
    /* FIXME: This implements ANSI C rules (also correct for C++).
       What about FORTRAN and (the deleted) chill ?  */
    {
      unsigned int promoted_len1 = TYPE_LENGTH (type1);
      unsigned int promoted_len2 = TYPE_LENGTH (type2);
      int is_unsigned1 = TYPE_UNSIGNED (type1);
      int is_unsigned2 = TYPE_UNSIGNED (type2);
      unsigned int result_len;
      int unsigned_operation;

      /* Determine type length and signedness after promotion for
         both operands.  */
      if (promoted_len1 < TYPE_LENGTH (builtin_type_int))
	{
	  is_unsigned1 = 0;
	  promoted_len1 = TYPE_LENGTH (builtin_type_int);
	}
      if (promoted_len2 < TYPE_LENGTH (builtin_type_int))
	{
	  is_unsigned2 = 0;
	  promoted_len2 = TYPE_LENGTH (builtin_type_int);
	}

      /* Determine type length of the result, and if the operation should
         be done unsigned.
         Use the signedness of the operand with the greater length.
         If both operands are of equal length, use unsigned operation
         if one of the operands is unsigned.  */
      if (promoted_len1 > promoted_len2)
	{
	  unsigned_operation = is_unsigned1;
	  result_len = promoted_len1;
	}
      else if (promoted_len2 > promoted_len1)
	{
	  unsigned_operation = is_unsigned2;
	  result_len = promoted_len2;
	}
      else
	{
	  unsigned_operation = is_unsigned1 || is_unsigned2;
	  result_len = promoted_len1;
	}

      if (unsigned_operation)
	{
	  ULONGEST v1, v2, v = 0;
	  v1 = (ULONGEST) value_as_long (arg1);
	  v2 = (ULONGEST) value_as_long (arg2);

	  /* Truncate values to the type length of the result.  */
	  if (result_len < sizeof (ULONGEST))
	    {
	      v1 &= ((LONGEST) 1 << HOST_CHAR_BIT * result_len) - 1;
	      v2 &= ((LONGEST) 1 << HOST_CHAR_BIT * result_len) - 1;
	    }

	  switch (op)
	    {
	    case BINOP_ADD:
	      v = v1 + v2;
	      break;

	    case BINOP_SUB:
	      v = v1 - v2;
	      break;

	    case BINOP_MUL:
	      v = v1 * v2;
	      break;

	    case BINOP_DIV:
	      v = v1 / v2;
	      break;

            case BINOP_EXP:
              v = pow (v1, v2);
              if (errno)
                error ("Cannot perform exponentiation: %s", safe_strerror (errno));
              break;

	    case BINOP_REM:
	      v = v1 % v2;
	      break;

	    case BINOP_MOD:
	      /* Knuth 1.2.4, integer only.  Note that unlike the C '%' op,
	         v1 mod 0 has a defined value, v1. */
	      if (v2 == 0)
		{
		  v = v1;
		}
	      else
		{
		  v = v1 / v2;
		  /* Note floor(v1/v2) == v1/v2 for unsigned. */
		  v = v1 - (v2 * v);
		}
	      break;

	    case BINOP_LSH:
	      v = v1 << v2;
	      break;

	    case BINOP_RSH:
	      v = v1 >> v2;
	      break;

	    case BINOP_BITWISE_AND:
	      v = v1 & v2;
	      break;

	    case BINOP_BITWISE_IOR:
	      v = v1 | v2;
	      break;

	    case BINOP_BITWISE_XOR:
	      v = v1 ^ v2;
	      break;

	    case BINOP_LOGICAL_AND:
	      v = v1 && v2;
	      break;

	    case BINOP_LOGICAL_OR:
	      v = v1 || v2;
	      break;

	    case BINOP_MIN:
	      v = v1 < v2 ? v1 : v2;
	      break;

	    case BINOP_MAX:
	      v = v1 > v2 ? v1 : v2;
	      break;

	    case BINOP_EQUAL:
	      v = v1 == v2;
	      break;

            case BINOP_NOTEQUAL:
              v = v1 != v2;
              break;

	    case BINOP_LESS:
	      v = v1 < v2;
	      break;

	    default:
	      error ("Invalid binary operation on numbers.");
	    }

	  /* This is a kludge to get around the fact that we don't
	     know how to determine the result type from the types of
	     the operands.  (I'm not really sure how much we feel the
	     need to duplicate the exact rules of the current
	     language.  They can get really hairy.  But not to do so
	     makes it hard to document just what we *do* do).  */

	  /* Can't just call init_type because we wouldn't know what
	     name to give the type.  */
	  val = allocate_value
	    (result_len > TARGET_LONG_BIT / HOST_CHAR_BIT
	     ? builtin_type_unsigned_long_long
	     : builtin_type_unsigned_long);
	  store_unsigned_integer (VALUE_CONTENTS_RAW (val),
				  TYPE_LENGTH (VALUE_TYPE (val)),
				  v);
	}
      else
	{
	  LONGEST v1, v2, v = 0;
	  v1 = value_as_long (arg1);
	  v2 = value_as_long (arg2);

	  switch (op)
	    {
	    case BINOP_ADD:
	      v = v1 + v2;
	      break;

	    case BINOP_SUB:
	      v = v1 - v2;
	      break;

	    case BINOP_MUL:
	      v = v1 * v2;
	      break;

	    case BINOP_DIV:
	      v = v1 / v2;
              break;

            case BINOP_EXP:
              v = pow (v1, v2);
              if (errno)
                error ("Cannot perform exponentiation: %s", safe_strerror (errno));
	      break;

	    case BINOP_REM:
	      v = v1 % v2;
	      break;

	    case BINOP_MOD:
	      /* Knuth 1.2.4, integer only.  Note that unlike the C '%' op,
	         X mod 0 has a defined value, X. */
	      if (v2 == 0)
		{
		  v = v1;
		}
	      else
		{
		  v = v1 / v2;
		  /* Compute floor. */
		  if (TRUNCATION_TOWARDS_ZERO && (v < 0) && ((v1 % v2) != 0))
		    {
		      v--;
		    }
		  v = v1 - (v2 * v);
		}
	      break;

	    case BINOP_LSH:
	      v = v1 << v2;
	      break;

	    case BINOP_RSH:
	      v = v1 >> v2;
	      break;

	    case BINOP_BITWISE_AND:
	      v = v1 & v2;
	      break;

	    case BINOP_BITWISE_IOR:
	      v = v1 | v2;
	      break;

	    case BINOP_BITWISE_XOR:
	      v = v1 ^ v2;
	      break;

	    case BINOP_LOGICAL_AND:
	      v = v1 && v2;
	      break;

	    case BINOP_LOGICAL_OR:
	      v = v1 || v2;
	      break;

	    case BINOP_MIN:
	      v = v1 < v2 ? v1 : v2;
	      break;

	    case BINOP_MAX:
	      v = v1 > v2 ? v1 : v2;
	      break;

	    case BINOP_EQUAL:
	      v = v1 == v2;
	      break;

	    case BINOP_LESS:
	      v = v1 < v2;
	      break;

	    default:
	      error ("Invalid binary operation on numbers.");
	    }

	  /* This is a kludge to get around the fact that we don't
	     know how to determine the result type from the types of
	     the operands.  (I'm not really sure how much we feel the
	     need to duplicate the exact rules of the current
	     language.  They can get really hairy.  But not to do so
	     makes it hard to document just what we *do* do).  */

	  /* Can't just call init_type because we wouldn't know what
	     name to give the type.  */
	  val = allocate_value
	    (result_len > TARGET_LONG_BIT / HOST_CHAR_BIT
	     ? builtin_type_long_long
	     : builtin_type_long);
	  store_signed_integer (VALUE_CONTENTS_RAW (val),
				TYPE_LENGTH (VALUE_TYPE (val)),
				v);
	}
    }

  return val;
}

/* Simulate the C operator ! -- return 1 if ARG1 contains zero.  */

int
value_logical_not (struct value *arg1)
{
  int len;
  char *p;
  struct type *type1;

  COERCE_NUMBER (arg1);
  type1 = check_typedef (VALUE_TYPE (arg1));

  if (TYPE_CODE (type1) == TYPE_CODE_FLT)
    return 0 == value_as_double (arg1);

  len = TYPE_LENGTH (type1);
  p = VALUE_CONTENTS (arg1);

  while (--len >= 0)
    {
      if (*p++)
	break;
    }

  return len < 0;
}

/* Perform a comparison on two string values (whose content are not
   necessarily null terminated) based on their length */

static int
value_strcmp (struct value *arg1, struct value *arg2)
{
  int len1 = TYPE_LENGTH (VALUE_TYPE (arg1));
  int len2 = TYPE_LENGTH (VALUE_TYPE (arg2));
  char *s1 = VALUE_CONTENTS (arg1);
  char *s2 = VALUE_CONTENTS (arg2);
  int i, len = len1 < len2 ? len1 : len2;

  for (i = 0; i < len; i++)
    {
      if (s1[i] < s2[i])
        return -1;
      else if (s1[i] > s2[i])
        return 1;
      else
        continue;
    }

  if (len1 < len2)
    return -1;
  else if (len1 > len2)
    return 1;
  else
    return 0;
}

/* Simulate the C operator == by returning a 1
   iff ARG1 and ARG2 have equal contents.  */

int
value_equal (struct value *arg1, struct value *arg2)
{
  int len;
  char *p1, *p2;
  struct type *type1, *type2;
  enum type_code code1;
  enum type_code code2;

  COERCE_NUMBER (arg1);
  COERCE_NUMBER (arg2);

  type1 = check_typedef (VALUE_TYPE (arg1));
  type2 = check_typedef (VALUE_TYPE (arg2));
  code1 = TYPE_CODE (type1);
  code2 = TYPE_CODE (type2);

  if ((code1 == TYPE_CODE_INT || code1 == TYPE_CODE_BOOL) &&
      (code2 == TYPE_CODE_INT || code2 == TYPE_CODE_BOOL))
    return longest_to_int (value_as_long (value_binop (arg1, arg2,
						       BINOP_EQUAL)));
  else if ((code1 == TYPE_CODE_FLT || code1 == TYPE_CODE_INT || code1 == TYPE_CODE_BOOL)
	   && (code2 == TYPE_CODE_FLT || code2 == TYPE_CODE_INT || code2 == TYPE_CODE_BOOL))
    return value_as_double (arg1) == value_as_double (arg2);

  /* FIXME: Need to promote to either CORE_ADDR or LONGEST, whichever
     is bigger.  */
  else if (code1 == TYPE_CODE_PTR && (code2 == TYPE_CODE_INT || code2 == TYPE_CODE_BOOL))
    return value_as_address (arg1) == (CORE_ADDR) value_as_long (arg2);
  else if (code2 == TYPE_CODE_PTR && (code1 == TYPE_CODE_INT || code1 == TYPE_CODE_BOOL))
    return (CORE_ADDR) value_as_long (arg1) == value_as_address (arg2);

  else if (code1 == code2
	   && ((len = (int) TYPE_LENGTH (type1))
	       == (int) TYPE_LENGTH (type2)))
    {
      p1 = VALUE_CONTENTS (arg1);
      p2 = VALUE_CONTENTS (arg2);
      while (--len >= 0)
	{
	  if (*p1++ != *p2++)
	    break;
	}
      return len < 0;
    }
  else if (code1 == TYPE_CODE_STRING && code2 == TYPE_CODE_STRING)
    {
      return value_strcmp (arg1, arg2) == 0;
    }
  else
    {
      error ("Invalid type combination in equality test.");
      return 0;			/* For lint -- never reached */
    }
}

/* Simulate the C operator < by returning 1
   iff ARG1's contents are less than ARG2's.  */

int
value_less (struct value *arg1, struct value *arg2)
{
  enum type_code code1;
  enum type_code code2;
  struct type *type1, *type2;

  COERCE_NUMBER (arg1);
  COERCE_NUMBER (arg2);

  type1 = check_typedef (VALUE_TYPE (arg1));
  type2 = check_typedef (VALUE_TYPE (arg2));
  code1 = TYPE_CODE (type1);
  code2 = TYPE_CODE (type2);

  if ((code1 == TYPE_CODE_INT || code1 == TYPE_CODE_BOOL) &&
      (code2 == TYPE_CODE_INT || code2 == TYPE_CODE_BOOL))
    return longest_to_int (value_as_long (value_binop (arg1, arg2,
						       BINOP_LESS)));
  else if ((code1 == TYPE_CODE_FLT || code1 == TYPE_CODE_INT || code1 == TYPE_CODE_BOOL)
	   && (code2 == TYPE_CODE_FLT || code2 == TYPE_CODE_INT || code2 == TYPE_CODE_BOOL))
    return value_as_double (arg1) < value_as_double (arg2);
  else if (code1 == TYPE_CODE_PTR && code2 == TYPE_CODE_PTR)
    return value_as_address (arg1) < value_as_address (arg2);

  /* FIXME: Need to promote to either CORE_ADDR or LONGEST, whichever
     is bigger.  */
  else if (code1 == TYPE_CODE_PTR && (code2 == TYPE_CODE_INT || code2 == TYPE_CODE_BOOL))
    return value_as_address (arg1) < (CORE_ADDR) value_as_long (arg2);
  else if (code2 == TYPE_CODE_PTR && (code1 == TYPE_CODE_INT || code1 == TYPE_CODE_BOOL))
    return (CORE_ADDR) value_as_long (arg1) < value_as_address (arg2);
  else if (code1 == TYPE_CODE_STRING && code2 == TYPE_CODE_STRING)
    return value_strcmp (arg1, arg2) < 0;
  else
    {
      error ("Invalid type combination in ordering comparison.");
      return 0;
    }
}

/* The unary operators - and ~.  Both free the argument ARG1.  */

struct value *
value_neg (struct value *arg1)
{
  struct type *type;
  struct type *result_type = VALUE_TYPE (arg1);

  COERCE_REF (arg1);
  COERCE_ENUM (arg1);

  type = check_typedef (VALUE_TYPE (arg1));

  if (TYPE_CODE (type) == TYPE_CODE_FLT)
    return value_from_double (result_type, -value_as_double (arg1));
  else if (TYPE_CODE (type) == TYPE_CODE_INT || TYPE_CODE (type) == TYPE_CODE_BOOL)
    {
      /* Perform integral promotion for ANSI C/C++.  FIXME: What about
         FORTRAN and (the deleted) chill ?  */
      if (TYPE_LENGTH (type) < TYPE_LENGTH (builtin_type_int))
	result_type = builtin_type_int;

      return value_from_longest (result_type, -value_as_long (arg1));
    }
  else
    {
      error ("Argument to negate operation not a number.");
      return 0;			/* For lint -- never reached */
    }
}

struct value *
value_complement (struct value *arg1)
{
  struct type *type;
  struct type *result_type = VALUE_TYPE (arg1);
  int typecode;

  COERCE_REF (arg1);
  COERCE_ENUM (arg1);

  type = check_typedef (VALUE_TYPE (arg1));

  typecode = TYPE_CODE (type);
  if ((typecode != TYPE_CODE_INT) && (typecode != TYPE_CODE_BOOL))
    error ("Argument to complement operation not an integer or boolean.");

  /* Perform integral promotion for ANSI C/C++.
     FIXME: What about FORTRAN ?  */
  if (TYPE_LENGTH (type) < TYPE_LENGTH (builtin_type_int))
    result_type = builtin_type_int;

  return value_from_longest (result_type, ~value_as_long (arg1));
}

/* The INDEX'th bit of SET value whose VALUE_TYPE is TYPE,
   and whose VALUE_CONTENTS is valaddr.
   Return -1 if out of range, -2 other error. */

int
value_bit_index (struct type *type, char *valaddr, int index)
{
  LONGEST low_bound, high_bound;
  LONGEST word;
  unsigned rel_index;
  struct type *range = TYPE_FIELD_TYPE (type, 0);
  if (get_discrete_bounds (range, &low_bound, &high_bound) < 0)
    return -2;
  if (index < low_bound || index > high_bound)
    return -1;
  rel_index = index - low_bound;
  word = unpack_long (builtin_type_unsigned_char,
		      valaddr + (rel_index / TARGET_CHAR_BIT));
  rel_index %= TARGET_CHAR_BIT;
  if (BITS_BIG_ENDIAN)
    rel_index = TARGET_CHAR_BIT - 1 - rel_index;
  return (word >> rel_index) & 1;
}

struct value *
value_in (struct value *element, struct value *set)
{
  int member;
  struct type *settype = check_typedef (VALUE_TYPE (set));
  struct type *eltype = check_typedef (VALUE_TYPE (element));
  if (TYPE_CODE (eltype) == TYPE_CODE_RANGE)
    eltype = TYPE_TARGET_TYPE (eltype);
  if (TYPE_CODE (settype) != TYPE_CODE_SET)
    error ("Second argument of 'IN' has wrong type");
  if (TYPE_CODE (eltype) != TYPE_CODE_INT
      && TYPE_CODE (eltype) != TYPE_CODE_CHAR
      && TYPE_CODE (eltype) != TYPE_CODE_ENUM
      && TYPE_CODE (eltype) != TYPE_CODE_BOOL)
    error ("First argument of 'IN' has wrong type");
  member = value_bit_index (settype, VALUE_CONTENTS (set),
			    value_as_long (element));
  if (member < 0)
    error ("First argument of 'IN' not in range");
  return value_from_longest (LA_BOOL_TYPE, member);
}

void
_initialize_valarith (void)
{
}
