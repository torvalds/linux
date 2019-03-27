/* Perform non-arithmetic operations on values, for GDB.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
#include "gdbtypes.h"
#include "value.h"
#include "frame.h"
#include "inferior.h"
#include "gdbcore.h"
#include "target.h"
#include "demangle.h"
#include "language.h"
#include "gdbcmd.h"
#include "regcache.h"
#include "cp-abi.h"
#include "block.h"
#include "infcall.h"
#include "dictionary.h"
#include "cp-support.h"

#include <errno.h>
#include "gdb_string.h"
#include "gdb_assert.h"
#include "cp-support.h"

/* Flag indicating HP compilers were used; needed to correctly handle some
   value operations with HP aCC code/runtime. */
extern int hp_som_som_object_present;

extern int overload_debug;
/* Local functions.  */

static int typecmp (int staticp, int varargs, int nargs,
		    struct field t1[], struct value *t2[]);

static CORE_ADDR value_push (CORE_ADDR, struct value *);

static struct value *search_struct_field (char *, struct value *, int,
				      struct type *, int);

static struct value *search_struct_method (char *, struct value **,
				       struct value **,
				       int, int *, struct type *);

static int find_oload_champ_namespace (struct type **arg_types, int nargs,
				       const char *func_name,
				       const char *qualified_name,
				       struct symbol ***oload_syms,
				       struct badness_vector **oload_champ_bv);

static
int find_oload_champ_namespace_loop (struct type **arg_types, int nargs,
				     const char *func_name,
				     const char *qualified_name,
				     int namespace_len,
				     struct symbol ***oload_syms,
				     struct badness_vector **oload_champ_bv,
				     int *oload_champ);

static int find_oload_champ (struct type **arg_types, int nargs, int method,
			     int num_fns,
			     struct fn_field *fns_ptr,
			     struct symbol **oload_syms,
			     struct badness_vector **oload_champ_bv);

static int oload_method_static (int method, struct fn_field *fns_ptr,
				int index);

enum oload_classification { STANDARD, NON_STANDARD, INCOMPATIBLE };

static enum
oload_classification classify_oload_match (struct badness_vector
					   * oload_champ_bv,
					   int nargs,
					   int static_offset);

static int check_field_in (struct type *, const char *);

static struct value *value_struct_elt_for_reference (struct type *domain,
						     int offset,
						     struct type *curtype,
						     char *name,
						     struct type *intype,
						     enum noside noside);

static struct value *value_namespace_elt (const struct type *curtype,
					  char *name,
					  enum noside noside);

static struct value *value_maybe_namespace_elt (const struct type *curtype,
						char *name,
						enum noside noside);

static CORE_ADDR allocate_space_in_inferior (int);

static struct value *cast_into_complex (struct type *, struct value *);

static struct fn_field *find_method_list (struct value ** argp, char *method,
					  int offset,
					  struct type *type, int *num_fns,
					  struct type **basetype,
					  int *boffset);

void _initialize_valops (void);

/* Flag for whether we want to abandon failed expression evals by default.  */

#if 0
static int auto_abandon = 0;
#endif

int overload_resolution = 0;

/* Find the address of function name NAME in the inferior.  */

struct value *
find_function_in_inferior (const char *name)
{
  struct symbol *sym;
  sym = lookup_symbol (name, 0, VAR_DOMAIN, 0, NULL);
  if (sym != NULL)
    {
      if (SYMBOL_CLASS (sym) != LOC_BLOCK)
	{
	  error ("\"%s\" exists in this program but is not a function.",
		 name);
	}
      return value_of_variable (sym, NULL);
    }
  else
    {
      struct minimal_symbol *msymbol = lookup_minimal_symbol (name, NULL, NULL);
      if (msymbol != NULL)
	{
	  struct type *type;
	  CORE_ADDR maddr;
	  type = lookup_pointer_type (builtin_type_char);
	  type = lookup_function_type (type);
	  type = lookup_pointer_type (type);
	  maddr = SYMBOL_VALUE_ADDRESS (msymbol);
	  return value_from_pointer (type, maddr);
	}
      else
	{
	  if (!target_has_execution)
	    error ("evaluation of this expression requires the target program to be active");
	  else
	    error ("evaluation of this expression requires the program to have a function \"%s\".", name);
	}
    }
}

/* Allocate NBYTES of space in the inferior using the inferior's malloc
   and return a value that is a pointer to the allocated space. */

struct value *
value_allocate_space_in_inferior (int len)
{
  struct value *blocklen;
  struct value *val = find_function_in_inferior (NAME_OF_MALLOC);

  blocklen = value_from_longest (builtin_type_int, (LONGEST) len);
  val = call_function_by_hand (val, 1, &blocklen);
  if (value_logical_not (val))
    {
      if (!target_has_execution)
	error ("No memory available to program now: you need to start the target first");
      else
	error ("No memory available to program: call to malloc failed");
    }
  return val;
}

static CORE_ADDR
allocate_space_in_inferior (int len)
{
  return value_as_long (value_allocate_space_in_inferior (len));
}

/* Cast value ARG2 to type TYPE and return as a value.
   More general than a C cast: accepts any two types of the same length,
   and if ARG2 is an lvalue it can be cast into anything at all.  */
/* In C++, casts may change pointer or object representations.  */

struct value *
value_cast (struct type *type, struct value *arg2)
{
  enum type_code code1;
  enum type_code code2;
  int scalar;
  struct type *type2;

  int convert_to_boolean = 0;

  if (VALUE_TYPE (arg2) == type)
    return arg2;

  CHECK_TYPEDEF (type);
  code1 = TYPE_CODE (type);
  COERCE_REF (arg2);
  type2 = check_typedef (VALUE_TYPE (arg2));

  /* A cast to an undetermined-length array_type, such as (TYPE [])OBJECT,
     is treated like a cast to (TYPE [N])OBJECT,
     where N is sizeof(OBJECT)/sizeof(TYPE). */
  if (code1 == TYPE_CODE_ARRAY)
    {
      struct type *element_type = TYPE_TARGET_TYPE (type);
      unsigned element_length = TYPE_LENGTH (check_typedef (element_type));
      if (element_length > 0
	&& TYPE_ARRAY_UPPER_BOUND_TYPE (type) == BOUND_CANNOT_BE_DETERMINED)
	{
	  struct type *range_type = TYPE_INDEX_TYPE (type);
	  int val_length = TYPE_LENGTH (type2);
	  LONGEST low_bound, high_bound, new_length;
	  if (get_discrete_bounds (range_type, &low_bound, &high_bound) < 0)
	    low_bound = 0, high_bound = 0;
	  new_length = val_length / element_length;
	  if (val_length % element_length != 0)
	    warning ("array element type size does not divide object size in cast");
	  /* FIXME-type-allocation: need a way to free this type when we are
	     done with it.  */
	  range_type = create_range_type ((struct type *) NULL,
					  TYPE_TARGET_TYPE (range_type),
					  low_bound,
					  new_length + low_bound - 1);
	  VALUE_TYPE (arg2) = create_array_type ((struct type *) NULL,
						 element_type, range_type);
	  return arg2;
	}
    }

  if (current_language->c_style_arrays
      && TYPE_CODE (type2) == TYPE_CODE_ARRAY)
    arg2 = value_coerce_array (arg2);

  if (TYPE_CODE (type2) == TYPE_CODE_FUNC)
    arg2 = value_coerce_function (arg2);

  type2 = check_typedef (VALUE_TYPE (arg2));
  COERCE_VARYING_ARRAY (arg2, type2);
  code2 = TYPE_CODE (type2);

  if (code1 == TYPE_CODE_COMPLEX)
    return cast_into_complex (type, arg2);
  if (code1 == TYPE_CODE_BOOL)
    {
      code1 = TYPE_CODE_INT;
      convert_to_boolean = 1;
    }
  if (code1 == TYPE_CODE_CHAR)
    code1 = TYPE_CODE_INT;
  if (code2 == TYPE_CODE_BOOL || code2 == TYPE_CODE_CHAR)
    code2 = TYPE_CODE_INT;

  scalar = (code2 == TYPE_CODE_INT || code2 == TYPE_CODE_FLT
	    || code2 == TYPE_CODE_ENUM || code2 == TYPE_CODE_RANGE);

  if (code1 == TYPE_CODE_STRUCT
      && code2 == TYPE_CODE_STRUCT
      && TYPE_NAME (type) != 0)
    {
      /* Look in the type of the source to see if it contains the
         type of the target as a superclass.  If so, we'll need to
         offset the object in addition to changing its type.  */
      struct value *v = search_struct_field (type_name_no_tag (type),
					 arg2, 0, type2, 1);
      if (v)
	{
	  VALUE_TYPE (v) = type;
	  return v;
	}
    }
  if (code1 == TYPE_CODE_FLT && scalar)
    return value_from_double (type, value_as_double (arg2));
  else if ((code1 == TYPE_CODE_INT || code1 == TYPE_CODE_ENUM
	    || code1 == TYPE_CODE_RANGE)
	   && (scalar || code2 == TYPE_CODE_PTR))
    {
      LONGEST longest;

      if (hp_som_som_object_present &&	/* if target compiled by HP aCC */
	  (code2 == TYPE_CODE_PTR))
	{
	  unsigned int *ptr;
	  struct value *retvalp;

	  switch (TYPE_CODE (TYPE_TARGET_TYPE (type2)))
	    {
	      /* With HP aCC, pointers to data members have a bias */
	    case TYPE_CODE_MEMBER:
	      retvalp = value_from_longest (type, value_as_long (arg2));
	      /* force evaluation */
	      ptr = (unsigned int *) VALUE_CONTENTS (retvalp);
	      *ptr &= ~0x20000000;	/* zap 29th bit to remove bias */
	      return retvalp;

	      /* While pointers to methods don't really point to a function */
	    case TYPE_CODE_METHOD:
	      error ("Pointers to methods not supported with HP aCC");

	    default:
	      break;		/* fall out and go to normal handling */
	    }
	}

      /* When we cast pointers to integers, we mustn't use
         POINTER_TO_ADDRESS to find the address the pointer
         represents, as value_as_long would.  GDB should evaluate
         expressions just as the compiler would --- and the compiler
         sees a cast as a simple reinterpretation of the pointer's
         bits.  */
      if (code2 == TYPE_CODE_PTR)
        longest = extract_unsigned_integer (VALUE_CONTENTS (arg2),
                                            TYPE_LENGTH (type2));
      else
        longest = value_as_long (arg2);
      return value_from_longest (type, convert_to_boolean ?
				 (LONGEST) (longest ? 1 : 0) : longest);
    }
  else if (code1 == TYPE_CODE_PTR && (code2 == TYPE_CODE_INT  ||
				      code2 == TYPE_CODE_ENUM ||
				      code2 == TYPE_CODE_RANGE))
    {
      /* TYPE_LENGTH (type) is the length of a pointer, but we really
	 want the length of an address! -- we are really dealing with
	 addresses (i.e., gdb representations) not pointers (i.e.,
	 target representations) here.

	 This allows things like "print *(int *)0x01000234" to work
	 without printing a misleading message -- which would
	 otherwise occur when dealing with a target having two byte
	 pointers and four byte addresses.  */

      int addr_bit = TARGET_ADDR_BIT;

      LONGEST longest = value_as_long (arg2);
      if (addr_bit < sizeof (LONGEST) * HOST_CHAR_BIT)
	{
	  if (longest >= ((LONGEST) 1 << addr_bit)
	      || longest <= -((LONGEST) 1 << addr_bit))
	    warning ("value truncated");
	}
      return value_from_longest (type, longest);
    }
  else if (TYPE_LENGTH (type) == TYPE_LENGTH (type2))
    {
      if (code1 == TYPE_CODE_PTR && code2 == TYPE_CODE_PTR)
	{
	  struct type *t1 = check_typedef (TYPE_TARGET_TYPE (type));
	  struct type *t2 = check_typedef (TYPE_TARGET_TYPE (type2));
	  if (TYPE_CODE (t1) == TYPE_CODE_STRUCT
	      && TYPE_CODE (t2) == TYPE_CODE_STRUCT
	      && !value_logical_not (arg2))
	    {
	      struct value *v;

	      /* Look in the type of the source to see if it contains the
	         type of the target as a superclass.  If so, we'll need to
	         offset the pointer rather than just change its type.  */
	      if (TYPE_NAME (t1) != NULL)
		{
		  v = search_struct_field (type_name_no_tag (t1),
					   value_ind (arg2), 0, t2, 1);
		  if (v)
		    {
		      v = value_addr (v);
		      VALUE_TYPE (v) = type;
		      return v;
		    }
		}

	      /* Look in the type of the target to see if it contains the
	         type of the source as a superclass.  If so, we'll need to
	         offset the pointer rather than just change its type.
	         FIXME: This fails silently with virtual inheritance.  */
	      if (TYPE_NAME (t2) != NULL)
		{
		  v = search_struct_field (type_name_no_tag (t2),
				       value_zero (t1, not_lval), 0, t1, 1);
		  if (v)
		    {
                      CORE_ADDR addr2 = value_as_address (arg2);
                      addr2 -= (VALUE_ADDRESS (v)
                                + VALUE_OFFSET (v)
                                + VALUE_EMBEDDED_OFFSET (v));
                      return value_from_pointer (type, addr2);
		    }
		}
	    }
	  /* No superclass found, just fall through to change ptr type.  */
	}
      VALUE_TYPE (arg2) = type;
      arg2 = value_change_enclosing_type (arg2, type);
      VALUE_POINTED_TO_OFFSET (arg2) = 0;	/* pai: chk_val */
      return arg2;
    }
  else if (VALUE_LVAL (arg2) == lval_memory)
    {
      return value_at_lazy (type, VALUE_ADDRESS (arg2) + VALUE_OFFSET (arg2),
			    VALUE_BFD_SECTION (arg2));
    }
  else if (code1 == TYPE_CODE_VOID)
    {
      return value_zero (builtin_type_void, not_lval);
    }
  else
    {
      error ("Invalid cast.");
      return 0;
    }
}

/* Create a value of type TYPE that is zero, and return it.  */

struct value *
value_zero (struct type *type, enum lval_type lv)
{
  struct value *val = allocate_value (type);

  memset (VALUE_CONTENTS (val), 0, TYPE_LENGTH (check_typedef (type)));
  VALUE_LVAL (val) = lv;

  return val;
}

/* Return a value with type TYPE located at ADDR.

   Call value_at only if the data needs to be fetched immediately;
   if we can be 'lazy' and defer the fetch, perhaps indefinately, call
   value_at_lazy instead.  value_at_lazy simply records the address of
   the data and sets the lazy-evaluation-required flag.  The lazy flag
   is tested in the VALUE_CONTENTS macro, which is used if and when
   the contents are actually required.

   Note: value_at does *NOT* handle embedded offsets; perform such
   adjustments before or after calling it. */

struct value *
value_at (struct type *type, CORE_ADDR addr, asection *sect)
{
  struct value *val;

  if (TYPE_CODE (check_typedef (type)) == TYPE_CODE_VOID)
    error ("Attempt to dereference a generic pointer.");

  val = allocate_value (type);

  read_memory (addr, VALUE_CONTENTS_ALL_RAW (val), TYPE_LENGTH (type));

  VALUE_LVAL (val) = lval_memory;
  VALUE_ADDRESS (val) = addr;
  VALUE_BFD_SECTION (val) = sect;

  return val;
}

/* Return a lazy value with type TYPE located at ADDR (cf. value_at).  */

struct value *
value_at_lazy (struct type *type, CORE_ADDR addr, asection *sect)
{
  struct value *val;

  if (TYPE_CODE (check_typedef (type)) == TYPE_CODE_VOID)
    error ("Attempt to dereference a generic pointer.");

  val = allocate_value (type);

  VALUE_LVAL (val) = lval_memory;
  VALUE_ADDRESS (val) = addr;
  VALUE_LAZY (val) = 1;
  VALUE_BFD_SECTION (val) = sect;

  return val;
}

/* Called only from the VALUE_CONTENTS and VALUE_CONTENTS_ALL macros,
   if the current data for a variable needs to be loaded into
   VALUE_CONTENTS(VAL).  Fetches the data from the user's process, and
   clears the lazy flag to indicate that the data in the buffer is valid.

   If the value is zero-length, we avoid calling read_memory, which would
   abort.  We mark the value as fetched anyway -- all 0 bytes of it.

   This function returns a value because it is used in the VALUE_CONTENTS
   macro as part of an expression, where a void would not work.  The
   value is ignored.  */

int
value_fetch_lazy (struct value *val)
{
  CORE_ADDR addr = VALUE_ADDRESS (val) + VALUE_OFFSET (val);
  int length = TYPE_LENGTH (VALUE_ENCLOSING_TYPE (val));

  struct type *type = VALUE_TYPE (val);
  if (length)
    read_memory (addr, VALUE_CONTENTS_ALL_RAW (val), length);

  VALUE_LAZY (val) = 0;
  return 0;
}


/* Store the contents of FROMVAL into the location of TOVAL.
   Return a new value with the location of TOVAL and contents of FROMVAL.  */

struct value *
value_assign (struct value *toval, struct value *fromval)
{
  struct type *type;
  struct value *val;
  char raw_buffer[MAX_REGISTER_SIZE];
  int use_buffer = 0;
  struct frame_id old_frame;

  if (!toval->modifiable)
    error ("Left operand of assignment is not a modifiable lvalue.");

  COERCE_REF (toval);

  type = VALUE_TYPE (toval);
  if (VALUE_LVAL (toval) != lval_internalvar)
    fromval = value_cast (type, fromval);
  else
    COERCE_ARRAY (fromval);
  CHECK_TYPEDEF (type);

  /* Since modifying a register can trash the frame chain, and modifying memory
     can trash the frame cache, we save the old frame and then restore the new
     frame afterwards.  */
  old_frame = get_frame_id (deprecated_selected_frame);

  switch (VALUE_LVAL (toval))
    {
    case lval_internalvar:
      set_internalvar (VALUE_INTERNALVAR (toval), fromval);
      val = value_copy (VALUE_INTERNALVAR (toval)->value);
      val = value_change_enclosing_type (val, VALUE_ENCLOSING_TYPE (fromval));
      VALUE_EMBEDDED_OFFSET (val) = VALUE_EMBEDDED_OFFSET (fromval);
      VALUE_POINTED_TO_OFFSET (val) = VALUE_POINTED_TO_OFFSET (fromval);
      return val;

    case lval_internalvar_component:
      set_internalvar_component (VALUE_INTERNALVAR (toval),
				 VALUE_OFFSET (toval),
				 VALUE_BITPOS (toval),
				 VALUE_BITSIZE (toval),
				 fromval);
      break;

    case lval_memory:
      {
	char *dest_buffer;
	CORE_ADDR changed_addr;
	int changed_len;

	if (VALUE_BITSIZE (toval))
	  {
	    char buffer[sizeof (LONGEST)];
	    /* We assume that the argument to read_memory is in units of
	       host chars.  FIXME:  Is that correct?  */
	    changed_len = (VALUE_BITPOS (toval)
			   + VALUE_BITSIZE (toval)
			   + HOST_CHAR_BIT - 1)
	      / HOST_CHAR_BIT;

	    if (changed_len > (int) sizeof (LONGEST))
	      error ("Can't handle bitfields which don't fit in a %d bit word.",
		     (int) sizeof (LONGEST) * HOST_CHAR_BIT);

	    read_memory (VALUE_ADDRESS (toval) + VALUE_OFFSET (toval),
			 buffer, changed_len);
	    modify_field (buffer, value_as_long (fromval),
			  VALUE_BITPOS (toval), VALUE_BITSIZE (toval));
	    changed_addr = VALUE_ADDRESS (toval) + VALUE_OFFSET (toval);
	    dest_buffer = buffer;
	  }
	else if (use_buffer)
	  {
	    changed_addr = VALUE_ADDRESS (toval) + VALUE_OFFSET (toval);
	    changed_len = use_buffer;
	    dest_buffer = raw_buffer;
	  }
	else
	  {
	    changed_addr = VALUE_ADDRESS (toval) + VALUE_OFFSET (toval);
	    changed_len = TYPE_LENGTH (type);
	    dest_buffer = VALUE_CONTENTS (fromval);
	  }

	write_memory (changed_addr, dest_buffer, changed_len);
	if (memory_changed_hook)
	  memory_changed_hook (changed_addr, changed_len);
	target_changed_event ();
      }
      break;

    case lval_reg_frame_relative:
    case lval_register:
      {
	struct frame_info *frame;
	int value_reg;

	/* Figure out which frame this is in currently.  */
	if (VALUE_LVAL (toval) == lval_register)
	  {
	    frame = get_current_frame ();
	    value_reg = VALUE_REGNO (toval);
	  }
	else
	  {
	    frame = frame_find_by_id (VALUE_FRAME_ID (toval));
	    value_reg = VALUE_FRAME_REGNUM (toval);
	  }

	if (!frame)
	  error ("Value being assigned to is no longer active.");
	
	if (VALUE_LVAL (toval) == lval_reg_frame_relative
	    && CONVERT_REGISTER_P (VALUE_FRAME_REGNUM (toval), type))
	  {
	    /* If TOVAL is a special machine register requiring
	       conversion of program values to a special raw format.  */
	    VALUE_TO_REGISTER (frame, VALUE_FRAME_REGNUM (toval),
			       type, VALUE_CONTENTS (fromval));
	  }
	else
	  {
	    /* TOVAL is stored in a series of registers in the frame
	       specified by the structure.  Copy that value out,
	       modify it, and copy it back in.  */
	    int amount_copied;
	    int amount_to_copy;
	    char *buffer;
	    int reg_offset;
	    int byte_offset;
	    int regno;

	    /* Locate the first register that falls in the value that
	       needs to be transfered.  Compute the offset of the
	       value in that register.  */
	    {
	      int offset;
	      for (reg_offset = value_reg, offset = 0;
		   offset + DEPRECATED_REGISTER_RAW_SIZE (reg_offset) <= VALUE_OFFSET (toval);
		   reg_offset++);
	      byte_offset = VALUE_OFFSET (toval) - offset;
	    }

	    /* Compute the number of register aligned values that need
	       to be copied.  */
	    if (VALUE_BITSIZE (toval))
	      amount_to_copy = byte_offset + 1;
	    else
	      amount_to_copy = byte_offset + TYPE_LENGTH (type);
	    
	    /* And a bounce buffer.  Be slightly over generous.  */
	    buffer = (char *) alloca (amount_to_copy + MAX_REGISTER_SIZE);

	    /* Copy it in.  */
	    for (regno = reg_offset, amount_copied = 0;
		 amount_copied < amount_to_copy;
		 amount_copied += DEPRECATED_REGISTER_RAW_SIZE (regno), regno++)
	      frame_register_read (frame, regno, buffer + amount_copied);
	    
	    /* Modify what needs to be modified.  */
	    if (VALUE_BITSIZE (toval))
	      modify_field (buffer + byte_offset,
			    value_as_long (fromval),
			    VALUE_BITPOS (toval), VALUE_BITSIZE (toval));
	    else if (use_buffer)
	      memcpy (buffer + VALUE_OFFSET (toval), raw_buffer, use_buffer);
	    else
	      memcpy (buffer + byte_offset, VALUE_CONTENTS (fromval),
		      TYPE_LENGTH (type));

	    /* Copy it out.  */
	    for (regno = reg_offset, amount_copied = 0;
		 amount_copied < amount_to_copy;
		 amount_copied += DEPRECATED_REGISTER_RAW_SIZE (regno), regno++)
	      put_frame_register (frame, regno, buffer + amount_copied);

	  }
	if (register_changed_hook)
	  register_changed_hook (-1);
	target_changed_event ();
	break;
      }
      
    default:
      error ("Left operand of assignment is not an lvalue.");
    }

  /* Assigning to the stack pointer, frame pointer, and other
     (architecture and calling convention specific) registers may
     cause the frame cache to be out of date.  Assigning to memory
     also can.  We just do this on all assignments to registers or
     memory, for simplicity's sake; I doubt the slowdown matters.  */
  switch (VALUE_LVAL (toval))
    {
    case lval_memory:
    case lval_register:
    case lval_reg_frame_relative:

      reinit_frame_cache ();

      /* Having destoroyed the frame cache, restore the selected frame.  */

      /* FIXME: cagney/2002-11-02: There has to be a better way of
	 doing this.  Instead of constantly saving/restoring the
	 frame.  Why not create a get_selected_frame() function that,
	 having saved the selected frame's ID can automatically
	 re-find the previously selected frame automatically.  */

      {
	struct frame_info *fi = frame_find_by_id (old_frame);
	if (fi != NULL)
	  select_frame (fi);
      }

      break;
    default:
      break;
    }
  
  /* If the field does not entirely fill a LONGEST, then zero the sign bits.
     If the field is signed, and is negative, then sign extend. */
  if ((VALUE_BITSIZE (toval) > 0)
      && (VALUE_BITSIZE (toval) < 8 * (int) sizeof (LONGEST)))
    {
      LONGEST fieldval = value_as_long (fromval);
      LONGEST valmask = (((ULONGEST) 1) << VALUE_BITSIZE (toval)) - 1;

      fieldval &= valmask;
      if (!TYPE_UNSIGNED (type) && (fieldval & (valmask ^ (valmask >> 1))))
	fieldval |= ~valmask;

      fromval = value_from_longest (type, fieldval);
    }

  val = value_copy (toval);
  memcpy (VALUE_CONTENTS_RAW (val), VALUE_CONTENTS (fromval),
	  TYPE_LENGTH (type));
  VALUE_TYPE (val) = type;
  val = value_change_enclosing_type (val, VALUE_ENCLOSING_TYPE (fromval));
  VALUE_EMBEDDED_OFFSET (val) = VALUE_EMBEDDED_OFFSET (fromval);
  VALUE_POINTED_TO_OFFSET (val) = VALUE_POINTED_TO_OFFSET (fromval);

  return val;
}

/* Extend a value VAL to COUNT repetitions of its type.  */

struct value *
value_repeat (struct value *arg1, int count)
{
  struct value *val;

  if (VALUE_LVAL (arg1) != lval_memory)
    error ("Only values in memory can be extended with '@'.");
  if (count < 1)
    error ("Invalid number %d of repetitions.", count);

  val = allocate_repeat_value (VALUE_ENCLOSING_TYPE (arg1), count);

  read_memory (VALUE_ADDRESS (arg1) + VALUE_OFFSET (arg1),
	       VALUE_CONTENTS_ALL_RAW (val),
	       TYPE_LENGTH (VALUE_ENCLOSING_TYPE (val)));
  VALUE_LVAL (val) = lval_memory;
  VALUE_ADDRESS (val) = VALUE_ADDRESS (arg1) + VALUE_OFFSET (arg1);

  return val;
}

struct value *
value_of_variable (struct symbol *var, struct block *b)
{
  struct value *val;
  struct frame_info *frame = NULL;

  if (!b)
    frame = NULL;		/* Use selected frame.  */
  else if (symbol_read_needs_frame (var))
    {
      frame = block_innermost_frame (b);
      if (!frame)
	{
	  if (BLOCK_FUNCTION (b)
	      && SYMBOL_PRINT_NAME (BLOCK_FUNCTION (b)))
	    error ("No frame is currently executing in block %s.",
		   SYMBOL_PRINT_NAME (BLOCK_FUNCTION (b)));
	  else
	    error ("No frame is currently executing in specified block");
	}
    }

  val = read_var_value (var, frame);
  if (!val)
    error ("Address of symbol \"%s\" is unknown.", SYMBOL_PRINT_NAME (var));

  return val;
}

/* Given a value which is an array, return a value which is a pointer to its
   first element, regardless of whether or not the array has a nonzero lower
   bound.

   FIXME:  A previous comment here indicated that this routine should be
   substracting the array's lower bound.  It's not clear to me that this
   is correct.  Given an array subscripting operation, it would certainly
   work to do the adjustment here, essentially computing:

   (&array[0] - (lowerbound * sizeof array[0])) + (index * sizeof array[0])

   However I believe a more appropriate and logical place to account for
   the lower bound is to do so in value_subscript, essentially computing:

   (&array[0] + ((index - lowerbound) * sizeof array[0]))

   As further evidence consider what would happen with operations other
   than array subscripting, where the caller would get back a value that
   had an address somewhere before the actual first element of the array,
   and the information about the lower bound would be lost because of
   the coercion to pointer type.
 */

struct value *
value_coerce_array (struct value *arg1)
{
  struct type *type = check_typedef (VALUE_TYPE (arg1));

  if (VALUE_LVAL (arg1) != lval_memory)
    error ("Attempt to take address of value not located in memory.");

  return value_from_pointer (lookup_pointer_type (TYPE_TARGET_TYPE (type)),
			     (VALUE_ADDRESS (arg1) + VALUE_OFFSET (arg1)));
}

/* Given a value which is a function, return a value which is a pointer
   to it.  */

struct value *
value_coerce_function (struct value *arg1)
{
  struct value *retval;

  if (VALUE_LVAL (arg1) != lval_memory)
    error ("Attempt to take address of value not located in memory.");

  retval = value_from_pointer (lookup_pointer_type (VALUE_TYPE (arg1)),
			       (VALUE_ADDRESS (arg1) + VALUE_OFFSET (arg1)));
  VALUE_BFD_SECTION (retval) = VALUE_BFD_SECTION (arg1);
  return retval;
}

/* Return a pointer value for the object for which ARG1 is the contents.  */

struct value *
value_addr (struct value *arg1)
{
  struct value *arg2;

  struct type *type = check_typedef (VALUE_TYPE (arg1));
  if (TYPE_CODE (type) == TYPE_CODE_REF)
    {
      /* Copy the value, but change the type from (T&) to (T*).
         We keep the same location information, which is efficient,
         and allows &(&X) to get the location containing the reference. */
      arg2 = value_copy (arg1);
      VALUE_TYPE (arg2) = lookup_pointer_type (TYPE_TARGET_TYPE (type));
      return arg2;
    }
  if (TYPE_CODE (type) == TYPE_CODE_FUNC)
    return value_coerce_function (arg1);

  if (VALUE_LVAL (arg1) != lval_memory)
    error ("Attempt to take address of value not located in memory.");

  /* Get target memory address */
  arg2 = value_from_pointer (lookup_pointer_type (VALUE_TYPE (arg1)),
			     (VALUE_ADDRESS (arg1)
			      + VALUE_OFFSET (arg1)
			      + VALUE_EMBEDDED_OFFSET (arg1)));

  /* This may be a pointer to a base subobject; so remember the
     full derived object's type ... */
  arg2 = value_change_enclosing_type (arg2, lookup_pointer_type (VALUE_ENCLOSING_TYPE (arg1)));
  /* ... and also the relative position of the subobject in the full object */
  VALUE_POINTED_TO_OFFSET (arg2) = VALUE_EMBEDDED_OFFSET (arg1);
  VALUE_BFD_SECTION (arg2) = VALUE_BFD_SECTION (arg1);
  return arg2;
}

/* Given a value of a pointer type, apply the C unary * operator to it.  */

struct value *
value_ind (struct value *arg1)
{
  struct type *base_type;
  struct value *arg2;

  COERCE_ARRAY (arg1);

  base_type = check_typedef (VALUE_TYPE (arg1));

  if (TYPE_CODE (base_type) == TYPE_CODE_MEMBER)
    error ("not implemented: member types in value_ind");

  /* Allow * on an integer so we can cast it to whatever we want.
     This returns an int, which seems like the most C-like thing
     to do.  "long long" variables are rare enough that
     BUILTIN_TYPE_LONGEST would seem to be a mistake.  */
  if (TYPE_CODE (base_type) == TYPE_CODE_INT)
    return value_at_lazy (builtin_type_int,
			  (CORE_ADDR) value_as_long (arg1),
			  VALUE_BFD_SECTION (arg1));
  else if (TYPE_CODE (base_type) == TYPE_CODE_PTR)
    {
      struct type *enc_type;
      /* We may be pointing to something embedded in a larger object */
      /* Get the real type of the enclosing object */
      enc_type = check_typedef (VALUE_ENCLOSING_TYPE (arg1));
      enc_type = TYPE_TARGET_TYPE (enc_type);
      /* Retrieve the enclosing object pointed to */
      arg2 = value_at_lazy (enc_type,
		   value_as_address (arg1) - VALUE_POINTED_TO_OFFSET (arg1),
			    VALUE_BFD_SECTION (arg1));
      /* Re-adjust type */
      VALUE_TYPE (arg2) = TYPE_TARGET_TYPE (base_type);
      /* Add embedding info */
      arg2 = value_change_enclosing_type (arg2, enc_type);
      VALUE_EMBEDDED_OFFSET (arg2) = VALUE_POINTED_TO_OFFSET (arg1);

      /* We may be pointing to an object of some derived type */
      arg2 = value_full_object (arg2, NULL, 0, 0, 0);
      return arg2;
    }

  error ("Attempt to take contents of a non-pointer value.");
  return 0;			/* For lint -- never reached */
}

/* Pushing small parts of stack frames.  */

/* Push one word (the size of object that a register holds).  */

CORE_ADDR
push_word (CORE_ADDR sp, ULONGEST word)
{
  int len = DEPRECATED_REGISTER_SIZE;
  char buffer[MAX_REGISTER_SIZE];

  store_unsigned_integer (buffer, len, word);
  if (INNER_THAN (1, 2))
    {
      /* stack grows downward */
      sp -= len;
      write_memory (sp, buffer, len);
    }
  else
    {
      /* stack grows upward */
      write_memory (sp, buffer, len);
      sp += len;
    }

  return sp;
}

/* Push LEN bytes with data at BUFFER.  */

CORE_ADDR
push_bytes (CORE_ADDR sp, char *buffer, int len)
{
  if (INNER_THAN (1, 2))
    {
      /* stack grows downward */
      sp -= len;
      write_memory (sp, buffer, len);
    }
  else
    {
      /* stack grows upward */
      write_memory (sp, buffer, len);
      sp += len;
    }

  return sp;
}

#ifndef PARM_BOUNDARY
#define PARM_BOUNDARY (0)
#endif

/* Push onto the stack the specified value VALUE.  Pad it correctly for
   it to be an argument to a function.  */

static CORE_ADDR
value_push (CORE_ADDR sp, struct value *arg)
{
  int len = TYPE_LENGTH (VALUE_ENCLOSING_TYPE (arg));
  int container_len = len;
  int offset;

  /* How big is the container we're going to put this value in?  */
  if (PARM_BOUNDARY)
    container_len = ((len + PARM_BOUNDARY / TARGET_CHAR_BIT - 1)
		     & ~(PARM_BOUNDARY / TARGET_CHAR_BIT - 1));

  /* Are we going to put it at the high or low end of the container?  */
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    offset = container_len - len;
  else
    offset = 0;

  if (INNER_THAN (1, 2))
    {
      /* stack grows downward */
      sp -= container_len;
      write_memory (sp + offset, VALUE_CONTENTS_ALL (arg), len);
    }
  else
    {
      /* stack grows upward */
      write_memory (sp + offset, VALUE_CONTENTS_ALL (arg), len);
      sp += container_len;
    }

  return sp;
}

CORE_ADDR
legacy_push_arguments (int nargs, struct value **args, CORE_ADDR sp,
		       int struct_return, CORE_ADDR struct_addr)
{
  /* ASSERT ( !struct_return); */
  int i;
  for (i = nargs - 1; i >= 0; i--)
    sp = value_push (sp, args[i]);
  return sp;
}

/* Create a value for an array by allocating space in the inferior, copying
   the data into that space, and then setting up an array value.

   The array bounds are set from LOWBOUND and HIGHBOUND, and the array is
   populated from the values passed in ELEMVEC.

   The element type of the array is inherited from the type of the
   first element, and all elements must have the same size (though we
   don't currently enforce any restriction on their types). */

struct value *
value_array (int lowbound, int highbound, struct value **elemvec)
{
  int nelem;
  int idx;
  unsigned int typelength;
  struct value *val;
  struct type *rangetype;
  struct type *arraytype;
  CORE_ADDR addr;

  /* Validate that the bounds are reasonable and that each of the elements
     have the same size. */

  nelem = highbound - lowbound + 1;
  if (nelem <= 0)
    {
      error ("bad array bounds (%d, %d)", lowbound, highbound);
    }
  typelength = TYPE_LENGTH (VALUE_ENCLOSING_TYPE (elemvec[0]));
  for (idx = 1; idx < nelem; idx++)
    {
      if (TYPE_LENGTH (VALUE_ENCLOSING_TYPE (elemvec[idx])) != typelength)
	{
	  error ("array elements must all be the same size");
	}
    }

  rangetype = create_range_type ((struct type *) NULL, builtin_type_int,
				 lowbound, highbound);
  arraytype = create_array_type ((struct type *) NULL,
			      VALUE_ENCLOSING_TYPE (elemvec[0]), rangetype);

  if (!current_language->c_style_arrays)
    {
      val = allocate_value (arraytype);
      for (idx = 0; idx < nelem; idx++)
	{
	  memcpy (VALUE_CONTENTS_ALL_RAW (val) + (idx * typelength),
		  VALUE_CONTENTS_ALL (elemvec[idx]),
		  typelength);
	}
      VALUE_BFD_SECTION (val) = VALUE_BFD_SECTION (elemvec[0]);
      return val;
    }

  /* Allocate space to store the array in the inferior, and then initialize
     it by copying in each element.  FIXME:  Is it worth it to create a
     local buffer in which to collect each value and then write all the
     bytes in one operation? */

  addr = allocate_space_in_inferior (nelem * typelength);
  for (idx = 0; idx < nelem; idx++)
    {
      write_memory (addr + (idx * typelength), VALUE_CONTENTS_ALL (elemvec[idx]),
		    typelength);
    }

  /* Create the array type and set up an array value to be evaluated lazily. */

  val = value_at_lazy (arraytype, addr, VALUE_BFD_SECTION (elemvec[0]));
  return (val);
}

/* Create a value for a string constant by allocating space in the inferior,
   copying the data into that space, and returning the address with type
   TYPE_CODE_STRING.  PTR points to the string constant data; LEN is number
   of characters.
   Note that string types are like array of char types with a lower bound of
   zero and an upper bound of LEN - 1.  Also note that the string may contain
   embedded null bytes. */

struct value *
value_string (char *ptr, int len)
{
  struct value *val;
  int lowbound = current_language->string_lower_bound;
  struct type *rangetype = create_range_type ((struct type *) NULL,
					      builtin_type_int,
					      lowbound, len + lowbound - 1);
  struct type *stringtype
  = create_string_type ((struct type *) NULL, rangetype);
  CORE_ADDR addr;

  if (current_language->c_style_arrays == 0)
    {
      val = allocate_value (stringtype);
      memcpy (VALUE_CONTENTS_RAW (val), ptr, len);
      return val;
    }


  /* Allocate space to store the string in the inferior, and then
     copy LEN bytes from PTR in gdb to that address in the inferior. */

  addr = allocate_space_in_inferior (len);
  write_memory (addr, ptr, len);

  val = value_at_lazy (stringtype, addr, NULL);
  return (val);
}

struct value *
value_bitstring (char *ptr, int len)
{
  struct value *val;
  struct type *domain_type = create_range_type (NULL, builtin_type_int,
						0, len - 1);
  struct type *type = create_set_type ((struct type *) NULL, domain_type);
  TYPE_CODE (type) = TYPE_CODE_BITSTRING;
  val = allocate_value (type);
  memcpy (VALUE_CONTENTS_RAW (val), ptr, TYPE_LENGTH (type));
  return val;
}

/* See if we can pass arguments in T2 to a function which takes arguments
   of types T1.  T1 is a list of NARGS arguments, and T2 is a NULL-terminated
   vector.  If some arguments need coercion of some sort, then the coerced
   values are written into T2.  Return value is 0 if the arguments could be
   matched, or the position at which they differ if not.

   STATICP is nonzero if the T1 argument list came from a
   static member function.  T2 will still include the ``this'' pointer,
   but it will be skipped.

   For non-static member functions, we ignore the first argument,
   which is the type of the instance variable.  This is because we want
   to handle calls with objects from derived classes.  This is not
   entirely correct: we should actually check to make sure that a
   requested operation is type secure, shouldn't we?  FIXME.  */

static int
typecmp (int staticp, int varargs, int nargs,
	 struct field t1[], struct value *t2[])
{
  int i;

  if (t2 == 0)
    internal_error (__FILE__, __LINE__, "typecmp: no argument list");

  /* Skip ``this'' argument if applicable.  T2 will always include THIS.  */
  if (staticp)
    t2 ++;

  for (i = 0;
       (i < nargs) && TYPE_CODE (t1[i].type) != TYPE_CODE_VOID;
       i++)
    {
      struct type *tt1, *tt2;

      if (!t2[i])
	return i + 1;

      tt1 = check_typedef (t1[i].type);
      tt2 = check_typedef (VALUE_TYPE (t2[i]));

      if (TYPE_CODE (tt1) == TYPE_CODE_REF
      /* We should be doing hairy argument matching, as below.  */
	  && (TYPE_CODE (check_typedef (TYPE_TARGET_TYPE (tt1))) == TYPE_CODE (tt2)))
	{
	  if (TYPE_CODE (tt2) == TYPE_CODE_ARRAY)
	    t2[i] = value_coerce_array (t2[i]);
	  else
	    t2[i] = value_addr (t2[i]);
	  continue;
	}

      /* djb - 20000715 - Until the new type structure is in the
	 place, and we can attempt things like implicit conversions,
	 we need to do this so you can take something like a map<const
	 char *>, and properly access map["hello"], because the
	 argument to [] will be a reference to a pointer to a char,
	 and the argument will be a pointer to a char. */
      while ( TYPE_CODE(tt1) == TYPE_CODE_REF ||
	      TYPE_CODE (tt1) == TYPE_CODE_PTR)
	{
	  tt1 = check_typedef( TYPE_TARGET_TYPE(tt1) );
	}
      while ( TYPE_CODE(tt2) == TYPE_CODE_ARRAY ||
	      TYPE_CODE(tt2) == TYPE_CODE_PTR ||
	      TYPE_CODE(tt2) == TYPE_CODE_REF)
	{
	  tt2 = check_typedef( TYPE_TARGET_TYPE(tt2) );
	}
      if (TYPE_CODE (tt1) == TYPE_CODE (tt2))
	continue;
      /* Array to pointer is a `trivial conversion' according to the ARM.  */

      /* We should be doing much hairier argument matching (see section 13.2
         of the ARM), but as a quick kludge, just check for the same type
         code.  */
      if (TYPE_CODE (t1[i].type) != TYPE_CODE (VALUE_TYPE (t2[i])))
	return i + 1;
    }
  if (varargs || t2[i] == NULL)
    return 0;
  return i + 1;
}

/* Helper function used by value_struct_elt to recurse through baseclasses.
   Look for a field NAME in ARG1. Adjust the address of ARG1 by OFFSET bytes,
   and search in it assuming it has (class) type TYPE.
   If found, return value, else return NULL.

   If LOOKING_FOR_BASECLASS, then instead of looking for struct fields,
   look for a baseclass named NAME.  */

static struct value *
search_struct_field (char *name, struct value *arg1, int offset,
		     struct type *type, int looking_for_baseclass)
{
  int i;
  int nbases = TYPE_N_BASECLASSES (type);

  CHECK_TYPEDEF (type);

  if (!looking_for_baseclass)
    for (i = TYPE_NFIELDS (type) - 1; i >= nbases; i--)
      {
	char *t_field_name = TYPE_FIELD_NAME (type, i);

	if (t_field_name && (strcmp_iw (t_field_name, name) == 0))
	  {
	    struct value *v;
	    if (TYPE_FIELD_STATIC (type, i))
	      {
		v = value_static_field (type, i);
		if (v == 0)
		  error ("field %s is nonexistent or has been optimised out",
			 name);
	      }
	    else
	      {
		v = value_primitive_field (arg1, offset, i, type);
		if (v == 0)
		  error ("there is no field named %s", name);
	      }
	    return v;
	  }

	if (t_field_name
	    && (t_field_name[0] == '\0'
		|| (TYPE_CODE (type) == TYPE_CODE_UNION
		    && (strcmp_iw (t_field_name, "else") == 0))))
	  {
	    struct type *field_type = TYPE_FIELD_TYPE (type, i);
	    if (TYPE_CODE (field_type) == TYPE_CODE_UNION
		|| TYPE_CODE (field_type) == TYPE_CODE_STRUCT)
	      {
		/* Look for a match through the fields of an anonymous union,
		   or anonymous struct.  C++ provides anonymous unions.

		   In the GNU Chill (now deleted from GDB)
		   implementation of variant record types, each
		   <alternative field> has an (anonymous) union type,
		   each member of the union represents a <variant
		   alternative>.  Each <variant alternative> is
		   represented as a struct, with a member for each
		   <variant field>.  */

		struct value *v;
		int new_offset = offset;

		/* This is pretty gross.  In G++, the offset in an
		   anonymous union is relative to the beginning of the
		   enclosing struct.  In the GNU Chill (now deleted
		   from GDB) implementation of variant records, the
		   bitpos is zero in an anonymous union field, so we
		   have to add the offset of the union here. */
		if (TYPE_CODE (field_type) == TYPE_CODE_STRUCT
		    || (TYPE_NFIELDS (field_type) > 0
			&& TYPE_FIELD_BITPOS (field_type, 0) == 0))
		  new_offset += TYPE_FIELD_BITPOS (type, i) / 8;

		v = search_struct_field (name, arg1, new_offset, field_type,
					 looking_for_baseclass);
		if (v)
		  return v;
	      }
	  }
      }

  for (i = 0; i < nbases; i++)
    {
      struct value *v;
      struct type *basetype = check_typedef (TYPE_BASECLASS (type, i));
      /* If we are looking for baseclasses, this is what we get when we
         hit them.  But it could happen that the base part's member name
         is not yet filled in.  */
      int found_baseclass = (looking_for_baseclass
			     && TYPE_BASECLASS_NAME (type, i) != NULL
			     && (strcmp_iw (name, TYPE_BASECLASS_NAME (type, i)) == 0));

      if (BASETYPE_VIA_VIRTUAL (type, i))
	{
	  int boffset;
	  struct value *v2 = allocate_value (basetype);

	  boffset = baseclass_offset (type, i,
				      VALUE_CONTENTS (arg1) + offset,
				      VALUE_ADDRESS (arg1)
				      + VALUE_OFFSET (arg1) + offset);
	  if (boffset == -1)
	    error ("virtual baseclass botch");

	  /* The virtual base class pointer might have been clobbered by the
	     user program. Make sure that it still points to a valid memory
	     location.  */

	  boffset += offset;
	  if (boffset < 0 || boffset >= TYPE_LENGTH (type))
	    {
	      CORE_ADDR base_addr;

	      base_addr = VALUE_ADDRESS (arg1) + VALUE_OFFSET (arg1) + boffset;
	      if (target_read_memory (base_addr, VALUE_CONTENTS_RAW (v2),
				      TYPE_LENGTH (basetype)) != 0)
		error ("virtual baseclass botch");
	      VALUE_LVAL (v2) = lval_memory;
	      VALUE_ADDRESS (v2) = base_addr;
	    }
	  else
	    {
	      VALUE_LVAL (v2) = VALUE_LVAL (arg1);
	      VALUE_ADDRESS (v2) = VALUE_ADDRESS (arg1);
	      VALUE_OFFSET (v2) = VALUE_OFFSET (arg1) + boffset;
	      if (VALUE_LAZY (arg1))
		VALUE_LAZY (v2) = 1;
	      else
		memcpy (VALUE_CONTENTS_RAW (v2),
			VALUE_CONTENTS_RAW (arg1) + boffset,
			TYPE_LENGTH (basetype));
	    }

	  if (found_baseclass)
	    return v2;
	  v = search_struct_field (name, v2, 0, TYPE_BASECLASS (type, i),
				   looking_for_baseclass);
	}
      else if (found_baseclass)
	v = value_primitive_field (arg1, offset, i, type);
      else
	v = search_struct_field (name, arg1,
			       offset + TYPE_BASECLASS_BITPOS (type, i) / 8,
				 basetype, looking_for_baseclass);
      if (v)
	return v;
    }
  return NULL;
}


/* Return the offset (in bytes) of the virtual base of type BASETYPE
 * in an object pointed to by VALADDR (on the host), assumed to be of
 * type TYPE.  OFFSET is number of bytes beyond start of ARG to start
 * looking (in case VALADDR is the contents of an enclosing object).
 *
 * This routine recurses on the primary base of the derived class because
 * the virtual base entries of the primary base appear before the other
 * virtual base entries.
 *
 * If the virtual base is not found, a negative integer is returned.
 * The magnitude of the negative integer is the number of entries in
 * the virtual table to skip over (entries corresponding to various
 * ancestral classes in the chain of primary bases).
 *
 * Important: This assumes the HP / Taligent C++ runtime
 * conventions. Use baseclass_offset() instead to deal with g++
 * conventions.  */

void
find_rt_vbase_offset (struct type *type, struct type *basetype, char *valaddr,
		      int offset, int *boffset_p, int *skip_p)
{
  int boffset;			/* offset of virtual base */
  int index;			/* displacement to use in virtual table */
  int skip;

  struct value *vp;
  CORE_ADDR vtbl;		/* the virtual table pointer */
  struct type *pbc;		/* the primary base class */

  /* Look for the virtual base recursively in the primary base, first.
   * This is because the derived class object and its primary base
   * subobject share the primary virtual table.  */

  boffset = 0;
  pbc = TYPE_PRIMARY_BASE (type);
  if (pbc)
    {
      find_rt_vbase_offset (pbc, basetype, valaddr, offset, &boffset, &skip);
      if (skip < 0)
	{
	  *boffset_p = boffset;
	  *skip_p = -1;
	  return;
	}
    }
  else
    skip = 0;


  /* Find the index of the virtual base according to HP/Taligent
     runtime spec. (Depth-first, left-to-right.)  */
  index = virtual_base_index_skip_primaries (basetype, type);

  if (index < 0)
    {
      *skip_p = skip + virtual_base_list_length_skip_primaries (type);
      *boffset_p = 0;
      return;
    }

  /* pai: FIXME -- 32x64 possible problem */
  /* First word (4 bytes) in object layout is the vtable pointer */
  vtbl = *(CORE_ADDR *) (valaddr + offset);

  /* Before the constructor is invoked, things are usually zero'd out. */
  if (vtbl == 0)
    error ("Couldn't find virtual table -- object may not be constructed yet.");


  /* Find virtual base's offset -- jump over entries for primary base
   * ancestors, then use the index computed above.  But also adjust by
   * HP_ACC_VBASE_START for the vtable slots before the start of the
   * virtual base entries.  Offset is negative -- virtual base entries
   * appear _before_ the address point of the virtual table. */

  /* pai: FIXME -- 32x64 problem, if word = 8 bytes, change multiplier
     & use long type */

  /* epstein : FIXME -- added param for overlay section. May not be correct */
  vp = value_at (builtin_type_int, vtbl + 4 * (-skip - index - HP_ACC_VBASE_START), NULL);
  boffset = value_as_long (vp);
  *skip_p = -1;
  *boffset_p = boffset;
  return;
}


/* Helper function used by value_struct_elt to recurse through baseclasses.
   Look for a field NAME in ARG1. Adjust the address of ARG1 by OFFSET bytes,
   and search in it assuming it has (class) type TYPE.
   If found, return value, else if name matched and args not return (value)-1,
   else return NULL. */

static struct value *
search_struct_method (char *name, struct value **arg1p,
		      struct value **args, int offset,
		      int *static_memfuncp, struct type *type)
{
  int i;
  struct value *v;
  int name_matched = 0;
  char dem_opname[64];

  CHECK_TYPEDEF (type);
  for (i = TYPE_NFN_FIELDS (type) - 1; i >= 0; i--)
    {
      char *t_field_name = TYPE_FN_FIELDLIST_NAME (type, i);
      /* FIXME!  May need to check for ARM demangling here */
      if (strncmp (t_field_name, "__", 2) == 0 ||
	  strncmp (t_field_name, "op", 2) == 0 ||
	  strncmp (t_field_name, "type", 4) == 0)
	{
	  if (cplus_demangle_opname (t_field_name, dem_opname, DMGL_ANSI))
	    t_field_name = dem_opname;
	  else if (cplus_demangle_opname (t_field_name, dem_opname, 0))
	    t_field_name = dem_opname;
	}
      if (t_field_name && (strcmp_iw (t_field_name, name) == 0))
	{
	  int j = TYPE_FN_FIELDLIST_LENGTH (type, i) - 1;
	  struct fn_field *f = TYPE_FN_FIELDLIST1 (type, i);
	  name_matched = 1;

	  check_stub_method_group (type, i);
	  if (j > 0 && args == 0)
	    error ("cannot resolve overloaded method `%s': no arguments supplied", name);
	  else if (j == 0 && args == 0)
	    {
	      v = value_fn_field (arg1p, f, j, type, offset);
	      if (v != NULL)
		return v;
	    }
	  else
	    while (j >= 0)
	      {
		if (!typecmp (TYPE_FN_FIELD_STATIC_P (f, j),
			      TYPE_VARARGS (TYPE_FN_FIELD_TYPE (f, j)),
			      TYPE_NFIELDS (TYPE_FN_FIELD_TYPE (f, j)),
			      TYPE_FN_FIELD_ARGS (f, j), args))
		  {
		    if (TYPE_FN_FIELD_VIRTUAL_P (f, j))
		      return value_virtual_fn_field (arg1p, f, j, type, offset);
		    if (TYPE_FN_FIELD_STATIC_P (f, j) && static_memfuncp)
		      *static_memfuncp = 1;
		    v = value_fn_field (arg1p, f, j, type, offset);
		    if (v != NULL)
		      return v;       
		  }
		j--;
	      }
	}
    }

  for (i = TYPE_N_BASECLASSES (type) - 1; i >= 0; i--)
    {
      int base_offset;

      if (BASETYPE_VIA_VIRTUAL (type, i))
	{
	  if (TYPE_HAS_VTABLE (type))
	    {
	      /* HP aCC compiled type, search for virtual base offset
	         according to HP/Taligent runtime spec.  */
	      int skip;
	      find_rt_vbase_offset (type, TYPE_BASECLASS (type, i),
				    VALUE_CONTENTS_ALL (*arg1p),
				    offset + VALUE_EMBEDDED_OFFSET (*arg1p),
				    &base_offset, &skip);
	      if (skip >= 0)
		error ("Virtual base class offset not found in vtable");
	    }
	  else
	    {
	      struct type *baseclass = check_typedef (TYPE_BASECLASS (type, i));
	      char *base_valaddr;

	      /* The virtual base class pointer might have been clobbered by the
	         user program. Make sure that it still points to a valid memory
	         location.  */

	      if (offset < 0 || offset >= TYPE_LENGTH (type))
		{
		  base_valaddr = (char *) alloca (TYPE_LENGTH (baseclass));
		  if (target_read_memory (VALUE_ADDRESS (*arg1p)
					  + VALUE_OFFSET (*arg1p) + offset,
					  base_valaddr,
					  TYPE_LENGTH (baseclass)) != 0)
		    error ("virtual baseclass botch");
		}
	      else
		base_valaddr = VALUE_CONTENTS (*arg1p) + offset;

	      base_offset =
		baseclass_offset (type, i, base_valaddr,
				  VALUE_ADDRESS (*arg1p)
				  + VALUE_OFFSET (*arg1p) + offset);
	      if (base_offset == -1)
		error ("virtual baseclass botch");
	    }
	}
      else
	{
	  base_offset = TYPE_BASECLASS_BITPOS (type, i) / 8;
	}
      v = search_struct_method (name, arg1p, args, base_offset + offset,
				static_memfuncp, TYPE_BASECLASS (type, i));
      if (v == (struct value *) - 1)
	{
	  name_matched = 1;
	}
      else if (v)
	{
/* FIXME-bothner:  Why is this commented out?  Why is it here?  */
/*        *arg1p = arg1_tmp; */
	  return v;
	}
    }
  if (name_matched)
    return (struct value *) - 1;
  else
    return NULL;
}

/* Given *ARGP, a value of type (pointer to a)* structure/union,
   extract the component named NAME from the ultimate target structure/union
   and return it as a value with its appropriate type.
   ERR is used in the error message if *ARGP's type is wrong.

   C++: ARGS is a list of argument types to aid in the selection of
   an appropriate method. Also, handle derived types.

   STATIC_MEMFUNCP, if non-NULL, points to a caller-supplied location
   where the truthvalue of whether the function that was resolved was
   a static member function or not is stored.

   ERR is an error message to be printed in case the field is not found.  */

struct value *
value_struct_elt (struct value **argp, struct value **args,
		  char *name, int *static_memfuncp, char *err)
{
  struct type *t;
  struct value *v;

  COERCE_ARRAY (*argp);

  t = check_typedef (VALUE_TYPE (*argp));

  /* Follow pointers until we get to a non-pointer.  */

  while (TYPE_CODE (t) == TYPE_CODE_PTR || TYPE_CODE (t) == TYPE_CODE_REF)
    {
      *argp = value_ind (*argp);
      /* Don't coerce fn pointer to fn and then back again!  */
      if (TYPE_CODE (VALUE_TYPE (*argp)) != TYPE_CODE_FUNC)
	COERCE_ARRAY (*argp);
      t = check_typedef (VALUE_TYPE (*argp));
    }

  if (TYPE_CODE (t) == TYPE_CODE_MEMBER)
    error ("not implemented: member type in value_struct_elt");

  if (TYPE_CODE (t) != TYPE_CODE_STRUCT
      && TYPE_CODE (t) != TYPE_CODE_UNION)
    error ("Attempt to extract a component of a value that is not a %s.", err);

  /* Assume it's not, unless we see that it is.  */
  if (static_memfuncp)
    *static_memfuncp = 0;

  if (!args)
    {
      /* if there are no arguments ...do this...  */

      /* Try as a field first, because if we succeed, there
         is less work to be done.  */
      v = search_struct_field (name, *argp, 0, t, 0);
      if (v)
	return v;

      /* C++: If it was not found as a data field, then try to
         return it as a pointer to a method.  */

      if (destructor_name_p (name, t))
	error ("Cannot get value of destructor");

      v = search_struct_method (name, argp, args, 0, static_memfuncp, t);

      if (v == (struct value *) - 1)
	error ("Cannot take address of a method");
      else if (v == 0)
	{
	  if (TYPE_NFN_FIELDS (t))
	    error ("There is no member or method named %s.", name);
	  else
	    error ("There is no member named %s.", name);
	}
      return v;
    }

  if (destructor_name_p (name, t))
    {
      if (!args[1])
	{
	  /* Destructors are a special case.  */
	  int m_index, f_index;

	  v = NULL;
	  if (get_destructor_fn_field (t, &m_index, &f_index))
	    {
	      v = value_fn_field (NULL, TYPE_FN_FIELDLIST1 (t, m_index),
				  f_index, NULL, 0);
	    }
	  if (v == NULL)
	    error ("could not find destructor function named %s.", name);
	  else
	    return v;
	}
      else
	{
	  error ("destructor should not have any argument");
	}
    }
  else
    v = search_struct_method (name, argp, args, 0, static_memfuncp, t);
  
  if (v == (struct value *) - 1)
    {
      error ("One of the arguments you tried to pass to %s could not be converted to what the function wants.", name);
    }
  else if (v == 0)
    {
      /* See if user tried to invoke data as function.  If so,
         hand it back.  If it's not callable (i.e., a pointer to function),
         gdb should give an error.  */
      v = search_struct_field (name, *argp, 0, t, 0);
    }

  if (!v)
    error ("Structure has no component named %s.", name);
  return v;
}

/* Search through the methods of an object (and its bases)
 * to find a specified method. Return the pointer to the
 * fn_field list of overloaded instances.
 * Helper function for value_find_oload_list.
 * ARGP is a pointer to a pointer to a value (the object)
 * METHOD is a string containing the method name
 * OFFSET is the offset within the value
 * TYPE is the assumed type of the object
 * NUM_FNS is the number of overloaded instances
 * BASETYPE is set to the actual type of the subobject where the method is found
 * BOFFSET is the offset of the base subobject where the method is found */

static struct fn_field *
find_method_list (struct value **argp, char *method, int offset,
		  struct type *type, int *num_fns,
		  struct type **basetype, int *boffset)
{
  int i;
  struct fn_field *f;
  CHECK_TYPEDEF (type);

  *num_fns = 0;

  /* First check in object itself */
  for (i = TYPE_NFN_FIELDS (type) - 1; i >= 0; i--)
    {
      /* pai: FIXME What about operators and type conversions? */
      char *fn_field_name = TYPE_FN_FIELDLIST_NAME (type, i);
      if (fn_field_name && (strcmp_iw (fn_field_name, method) == 0))
	{
	  int len = TYPE_FN_FIELDLIST_LENGTH (type, i);
	  struct fn_field *f = TYPE_FN_FIELDLIST1 (type, i);

	  *num_fns = len;
	  *basetype = type;
	  *boffset = offset;

	  /* Resolve any stub methods.  */
	  check_stub_method_group (type, i);

	  return f;
	}
    }

  /* Not found in object, check in base subobjects */
  for (i = TYPE_N_BASECLASSES (type) - 1; i >= 0; i--)
    {
      int base_offset;
      if (BASETYPE_VIA_VIRTUAL (type, i))
	{
	  if (TYPE_HAS_VTABLE (type))
	    {
	      /* HP aCC compiled type, search for virtual base offset
	       * according to HP/Taligent runtime spec.  */
	      int skip;
	      find_rt_vbase_offset (type, TYPE_BASECLASS (type, i),
				    VALUE_CONTENTS_ALL (*argp),
				    offset + VALUE_EMBEDDED_OFFSET (*argp),
				    &base_offset, &skip);
	      if (skip >= 0)
		error ("Virtual base class offset not found in vtable");
	    }
	  else
	    {
	      /* probably g++ runtime model */
	      base_offset = VALUE_OFFSET (*argp) + offset;
	      base_offset =
		baseclass_offset (type, i,
				  VALUE_CONTENTS (*argp) + base_offset,
				  VALUE_ADDRESS (*argp) + base_offset);
	      if (base_offset == -1)
		error ("virtual baseclass botch");
	    }
	}
      else
	/* non-virtual base, simply use bit position from debug info */
	{
	  base_offset = TYPE_BASECLASS_BITPOS (type, i) / 8;
	}
      f = find_method_list (argp, method, base_offset + offset,
			    TYPE_BASECLASS (type, i), num_fns, basetype,
			    boffset);
      if (f)
	return f;
    }
  return NULL;
}

/* Return the list of overloaded methods of a specified name.
 * ARGP is a pointer to a pointer to a value (the object)
 * METHOD is the method name
 * OFFSET is the offset within the value contents
 * NUM_FNS is the number of overloaded instances
 * BASETYPE is set to the type of the base subobject that defines the method
 * BOFFSET is the offset of the base subobject which defines the method */

struct fn_field *
value_find_oload_method_list (struct value **argp, char *method, int offset,
			      int *num_fns, struct type **basetype,
			      int *boffset)
{
  struct type *t;

  t = check_typedef (VALUE_TYPE (*argp));

  /* code snarfed from value_struct_elt */
  while (TYPE_CODE (t) == TYPE_CODE_PTR || TYPE_CODE (t) == TYPE_CODE_REF)
    {
      *argp = value_ind (*argp);
      /* Don't coerce fn pointer to fn and then back again!  */
      if (TYPE_CODE (VALUE_TYPE (*argp)) != TYPE_CODE_FUNC)
	COERCE_ARRAY (*argp);
      t = check_typedef (VALUE_TYPE (*argp));
    }

  if (TYPE_CODE (t) == TYPE_CODE_MEMBER)
    error ("Not implemented: member type in value_find_oload_lis");

  if (TYPE_CODE (t) != TYPE_CODE_STRUCT
      && TYPE_CODE (t) != TYPE_CODE_UNION)
    error ("Attempt to extract a component of a value that is not a struct or union");

  return find_method_list (argp, method, 0, t, num_fns, basetype, boffset);
}

/* Given an array of argument types (ARGTYPES) (which includes an
   entry for "this" in the case of C++ methods), the number of
   arguments NARGS, the NAME of a function whether it's a method or
   not (METHOD), and the degree of laxness (LAX) in conforming to
   overload resolution rules in ANSI C++, find the best function that
   matches on the argument types according to the overload resolution
   rules.

   In the case of class methods, the parameter OBJ is an object value
   in which to search for overloaded methods.

   In the case of non-method functions, the parameter FSYM is a symbol
   corresponding to one of the overloaded functions.

   Return value is an integer: 0 -> good match, 10 -> debugger applied
   non-standard coercions, 100 -> incompatible.

   If a method is being searched for, VALP will hold the value.
   If a non-method is being searched for, SYMP will hold the symbol for it.

   If a method is being searched for, and it is a static method,
   then STATICP will point to a non-zero value.

   Note: This function does *not* check the value of
   overload_resolution.  Caller must check it to see whether overload
   resolution is permitted.
 */

int
find_overload_match (struct type **arg_types, int nargs, char *name, int method,
		     int lax, struct value **objp, struct symbol *fsym,
		     struct value **valp, struct symbol **symp, int *staticp)
{
  struct value *obj = (objp ? *objp : NULL);

  int oload_champ;		/* Index of best overloaded function */

  struct badness_vector *oload_champ_bv = NULL;		/* The measure for the current best match */

  struct value *temp = obj;
  struct fn_field *fns_ptr = NULL;	/* For methods, the list of overloaded methods */
  struct symbol **oload_syms = NULL;	/* For non-methods, the list of overloaded function symbols */
  int num_fns = 0;		/* Number of overloaded instances being considered */
  struct type *basetype = NULL;
  int boffset;
  int ix;
  int static_offset;
  struct cleanup *old_cleanups = NULL;

  const char *obj_type_name = NULL;
  char *func_name = NULL;
  enum oload_classification match_quality;

  /* Get the list of overloaded methods or functions */
  if (method)
    {
      obj_type_name = TYPE_NAME (VALUE_TYPE (obj));
      /* Hack: evaluate_subexp_standard often passes in a pointer
         value rather than the object itself, so try again */
      if ((!obj_type_name || !*obj_type_name) &&
	  (TYPE_CODE (VALUE_TYPE (obj)) == TYPE_CODE_PTR))
	obj_type_name = TYPE_NAME (TYPE_TARGET_TYPE (VALUE_TYPE (obj)));

      fns_ptr = value_find_oload_method_list (&temp, name, 0,
					      &num_fns,
					      &basetype, &boffset);
      if (!fns_ptr || !num_fns)
	error ("Couldn't find method %s%s%s",
	       obj_type_name,
	       (obj_type_name && *obj_type_name) ? "::" : "",
	       name);
      /* If we are dealing with stub method types, they should have
	 been resolved by find_method_list via value_find_oload_method_list
	 above.  */
      gdb_assert (TYPE_DOMAIN_TYPE (fns_ptr[0].type) != NULL);
      oload_champ = find_oload_champ (arg_types, nargs, method, num_fns,
				      fns_ptr, oload_syms, &oload_champ_bv);
    }
  else
    {
      const char *qualified_name = SYMBOL_CPLUS_DEMANGLED_NAME (fsym);
      func_name	= cp_func_name (qualified_name);

      /* If the name is NULL this must be a C-style function.
         Just return the same symbol. */
      if (func_name == NULL)
        {
	  *symp = fsym;
          return 0;
        }

      old_cleanups = make_cleanup (xfree, func_name);
      make_cleanup (xfree, oload_syms);
      make_cleanup (xfree, oload_champ_bv);

      oload_champ = find_oload_champ_namespace (arg_types, nargs,
						func_name,
						qualified_name,
						&oload_syms,
						&oload_champ_bv);
    }

  /* Check how bad the best match is.  */

  match_quality
    = classify_oload_match (oload_champ_bv, nargs,
			    oload_method_static (method, fns_ptr,
						 oload_champ));

  if (match_quality == INCOMPATIBLE)
    {
      if (method)
	error ("Cannot resolve method %s%s%s to any overloaded instance",
	       obj_type_name,
	       (obj_type_name && *obj_type_name) ? "::" : "",
	       name);
      else
	error ("Cannot resolve function %s to any overloaded instance",
	       func_name);
    }
  else if (match_quality == NON_STANDARD)
    {
      if (method)
	warning ("Using non-standard conversion to match method %s%s%s to supplied arguments",
		 obj_type_name,
		 (obj_type_name && *obj_type_name) ? "::" : "",
		 name);
      else
	warning ("Using non-standard conversion to match function %s to supplied arguments",
		 func_name);
    }

  if (method)
    {
      if (staticp != NULL)
	*staticp = oload_method_static (method, fns_ptr, oload_champ);
      if (TYPE_FN_FIELD_VIRTUAL_P (fns_ptr, oload_champ))
	*valp = value_virtual_fn_field (&temp, fns_ptr, oload_champ, basetype, boffset);
      else
	*valp = value_fn_field (&temp, fns_ptr, oload_champ, basetype, boffset);
    }
  else
    {
      *symp = oload_syms[oload_champ];
    }

  if (objp)
    {
      if (TYPE_CODE (VALUE_TYPE (temp)) != TYPE_CODE_PTR
	  && TYPE_CODE (VALUE_TYPE (*objp)) == TYPE_CODE_PTR)
	{
	  temp = value_addr (temp);
	}
      *objp = temp;
    }
  if (old_cleanups != NULL)
    do_cleanups (old_cleanups);

  switch (match_quality)
    {
    case INCOMPATIBLE:
      return 100;
    case NON_STANDARD:
      return 10;
    default:				/* STANDARD */
      return 0;
    }
}

/* Find the best overload match, searching for FUNC_NAME in namespaces
   contained in QUALIFIED_NAME until it either finds a good match or
   runs out of namespaces.  It stores the overloaded functions in
   *OLOAD_SYMS, and the badness vector in *OLOAD_CHAMP_BV.  The
   calling function is responsible for freeing *OLOAD_SYMS and
   *OLOAD_CHAMP_BV.  */

static int
find_oload_champ_namespace (struct type **arg_types, int nargs,
			    const char *func_name,
			    const char *qualified_name,
			    struct symbol ***oload_syms,
			    struct badness_vector **oload_champ_bv)
{
  int oload_champ;

  find_oload_champ_namespace_loop (arg_types, nargs,
				   func_name,
				   qualified_name, 0,
				   oload_syms, oload_champ_bv,
				   &oload_champ);

  return oload_champ;
}

/* Helper function for find_oload_champ_namespace; NAMESPACE_LEN is
   how deep we've looked for namespaces, and the champ is stored in
   OLOAD_CHAMP.  The return value is 1 if the champ is a good one, 0
   if it isn't.

   It is the caller's responsibility to free *OLOAD_SYMS and
   *OLOAD_CHAMP_BV.  */

static int
find_oload_champ_namespace_loop (struct type **arg_types, int nargs,
				 const char *func_name,
				 const char *qualified_name,
				 int namespace_len,
				 struct symbol ***oload_syms,
				 struct badness_vector **oload_champ_bv,
				 int *oload_champ)
{
  int next_namespace_len = namespace_len;
  int searched_deeper = 0;
  int num_fns = 0;
  struct cleanup *old_cleanups;
  int new_oload_champ;
  struct symbol **new_oload_syms;
  struct badness_vector *new_oload_champ_bv;
  char *new_namespace;

  if (next_namespace_len != 0)
    {
      gdb_assert (qualified_name[next_namespace_len] == ':');
      next_namespace_len +=  2;
    }
  next_namespace_len
    += cp_find_first_component (qualified_name + next_namespace_len);

  /* Initialize these to values that can safely be xfree'd.  */
  *oload_syms = NULL;
  *oload_champ_bv = NULL;

  /* First, see if we have a deeper namespace we can search in.  If we
     get a good match there, use it.  */

  if (qualified_name[next_namespace_len] == ':')
    {
      searched_deeper = 1;

      if (find_oload_champ_namespace_loop (arg_types, nargs,
					   func_name, qualified_name,
					   next_namespace_len,
					   oload_syms, oload_champ_bv,
					   oload_champ))
	{
	  return 1;
	}
    };

  /* If we reach here, either we're in the deepest namespace or we
     didn't find a good match in a deeper namespace.  But, in the
     latter case, we still have a bad match in a deeper namespace;
     note that we might not find any match at all in the current
     namespace.  (There's always a match in the deepest namespace,
     because this overload mechanism only gets called if there's a
     function symbol to start off with.)  */

  old_cleanups = make_cleanup (xfree, *oload_syms);
  old_cleanups = make_cleanup (xfree, *oload_champ_bv);
  new_namespace = alloca (namespace_len + 1);
  strncpy (new_namespace, qualified_name, namespace_len);
  new_namespace[namespace_len] = '\0';
  new_oload_syms = make_symbol_overload_list (func_name,
					      new_namespace);
  while (new_oload_syms[num_fns])
    ++num_fns;

  new_oload_champ = find_oload_champ (arg_types, nargs, 0, num_fns,
				      NULL, new_oload_syms,
				      &new_oload_champ_bv);

  /* Case 1: We found a good match.  Free earlier matches (if any),
     and return it.  Case 2: We didn't find a good match, but we're
     not the deepest function.  Then go with the bad match that the
     deeper function found.  Case 3: We found a bad match, and we're
     the deepest function.  Then return what we found, even though
     it's a bad match.  */

  if (new_oload_champ != -1
      && classify_oload_match (new_oload_champ_bv, nargs, 0) == STANDARD)
    {
      *oload_syms = new_oload_syms;
      *oload_champ = new_oload_champ;
      *oload_champ_bv = new_oload_champ_bv;
      do_cleanups (old_cleanups);
      return 1;
    }
  else if (searched_deeper)
    {
      xfree (new_oload_syms);
      xfree (new_oload_champ_bv);
      discard_cleanups (old_cleanups);
      return 0;
    }
  else
    {
      gdb_assert (new_oload_champ != -1);
      *oload_syms = new_oload_syms;
      *oload_champ = new_oload_champ;
      *oload_champ_bv = new_oload_champ_bv;
      discard_cleanups (old_cleanups);
      return 0;
    }
}

/* Look for a function to take NARGS args of types ARG_TYPES.  Find
   the best match from among the overloaded methods or functions
   (depending on METHOD) given by FNS_PTR or OLOAD_SYMS, respectively.
   The number of methods/functions in the list is given by NUM_FNS.
   Return the index of the best match; store an indication of the
   quality of the match in OLOAD_CHAMP_BV.

   It is the caller's responsibility to free *OLOAD_CHAMP_BV.  */

static int
find_oload_champ (struct type **arg_types, int nargs, int method,
		  int num_fns, struct fn_field *fns_ptr,
		  struct symbol **oload_syms,
		  struct badness_vector **oload_champ_bv)
{
  int ix;
  struct badness_vector *bv;	/* A measure of how good an overloaded instance is */
  int oload_champ = -1;		/* Index of best overloaded function */
  int oload_ambiguous = 0;	/* Current ambiguity state for overload resolution */
  /* 0 => no ambiguity, 1 => two good funcs, 2 => incomparable funcs */

  *oload_champ_bv = NULL;

  /* Consider each candidate in turn */
  for (ix = 0; ix < num_fns; ix++)
    {
      int jj;
      int static_offset = oload_method_static (method, fns_ptr, ix);
      int nparms;
      struct type **parm_types;

      if (method)
	{
	  nparms = TYPE_NFIELDS (TYPE_FN_FIELD_TYPE (fns_ptr, ix));
	}
      else
	{
	  /* If it's not a method, this is the proper place */
	  nparms=TYPE_NFIELDS(SYMBOL_TYPE(oload_syms[ix]));
	}

      /* Prepare array of parameter types */
      parm_types = (struct type **) xmalloc (nparms * (sizeof (struct type *)));
      for (jj = 0; jj < nparms; jj++)
	parm_types[jj] = (method
			  ? (TYPE_FN_FIELD_ARGS (fns_ptr, ix)[jj].type)
			  : TYPE_FIELD_TYPE (SYMBOL_TYPE (oload_syms[ix]), jj));

      /* Compare parameter types to supplied argument types.  Skip THIS for
         static methods.  */
      bv = rank_function (parm_types, nparms, arg_types + static_offset,
			  nargs - static_offset);

      if (!*oload_champ_bv)
	{
	  *oload_champ_bv = bv;
	  oload_champ = 0;
	}
      else
	/* See whether current candidate is better or worse than previous best */
	switch (compare_badness (bv, *oload_champ_bv))
	  {
	  case 0:
	    oload_ambiguous = 1;	/* top two contenders are equally good */
	    break;
	  case 1:
	    oload_ambiguous = 2;	/* incomparable top contenders */
	    break;
	  case 2:
	    *oload_champ_bv = bv;	/* new champion, record details */
	    oload_ambiguous = 0;
	    oload_champ = ix;
	    break;
	  case 3:
	  default:
	    break;
	  }
      xfree (parm_types);
      if (overload_debug)
	{
	  if (method)
	    fprintf_filtered (gdb_stderr,"Overloaded method instance %s, # of parms %d\n", fns_ptr[ix].physname, nparms);
	  else
	    fprintf_filtered (gdb_stderr,"Overloaded function instance %s # of parms %d\n", SYMBOL_DEMANGLED_NAME (oload_syms[ix]), nparms);
	  for (jj = 0; jj < nargs - static_offset; jj++)
	    fprintf_filtered (gdb_stderr,"...Badness @ %d : %d\n", jj, bv->rank[jj]);
	  fprintf_filtered (gdb_stderr,"Overload resolution champion is %d, ambiguous? %d\n", oload_champ, oload_ambiguous);
	}
    }

  return oload_champ;
}

/* Return 1 if we're looking at a static method, 0 if we're looking at
   a non-static method or a function that isn't a method.  */

static int
oload_method_static (int method, struct fn_field *fns_ptr, int index)
{
  if (method && TYPE_FN_FIELD_STATIC_P (fns_ptr, index))
    return 1;
  else
    return 0;
}

/* Check how good an overload match OLOAD_CHAMP_BV represents.  */

static enum oload_classification
classify_oload_match (struct badness_vector *oload_champ_bv,
		      int nargs,
		      int static_offset)
{
  int ix;

  for (ix = 1; ix <= nargs - static_offset; ix++)
    {
      if (oload_champ_bv->rank[ix] >= 100)
	return INCOMPATIBLE;	/* truly mismatched types */
      else if (oload_champ_bv->rank[ix] >= 10)
	return NON_STANDARD;	/* non-standard type conversions needed */
    }

  return STANDARD;		/* Only standard conversions needed.  */
}

/* C++: return 1 is NAME is a legitimate name for the destructor
   of type TYPE.  If TYPE does not have a destructor, or
   if NAME is inappropriate for TYPE, an error is signaled.  */
int
destructor_name_p (const char *name, const struct type *type)
{
  /* destructors are a special case.  */

  if (name[0] == '~')
    {
      char *dname = type_name_no_tag (type);
      char *cp = strchr (dname, '<');
      unsigned int len;

      /* Do not compare the template part for template classes.  */
      if (cp == NULL)
	len = strlen (dname);
      else
	len = cp - dname;
      if (strlen (name + 1) != len || strncmp (dname, name + 1, len) != 0)
	error ("name of destructor must equal name of class");
      else
	return 1;
    }
  return 0;
}

/* Helper function for check_field: Given TYPE, a structure/union,
   return 1 if the component named NAME from the ultimate
   target structure/union is defined, otherwise, return 0. */

static int
check_field_in (struct type *type, const char *name)
{
  int i;

  for (i = TYPE_NFIELDS (type) - 1; i >= TYPE_N_BASECLASSES (type); i--)
    {
      char *t_field_name = TYPE_FIELD_NAME (type, i);
      if (t_field_name && (strcmp_iw (t_field_name, name) == 0))
	return 1;
    }

  /* C++: If it was not found as a data field, then try to
     return it as a pointer to a method.  */

  /* Destructors are a special case.  */
  if (destructor_name_p (name, type))
    {
      int m_index, f_index;

      return get_destructor_fn_field (type, &m_index, &f_index);
    }

  for (i = TYPE_NFN_FIELDS (type) - 1; i >= 0; --i)
    {
      if (strcmp_iw (TYPE_FN_FIELDLIST_NAME (type, i), name) == 0)
	return 1;
    }

  for (i = TYPE_N_BASECLASSES (type) - 1; i >= 0; i--)
    if (check_field_in (TYPE_BASECLASS (type, i), name))
      return 1;

  return 0;
}


/* C++: Given ARG1, a value of type (pointer to a)* structure/union,
   return 1 if the component named NAME from the ultimate
   target structure/union is defined, otherwise, return 0.  */

int
check_field (struct value *arg1, const char *name)
{
  struct type *t;

  COERCE_ARRAY (arg1);

  t = VALUE_TYPE (arg1);

  /* Follow pointers until we get to a non-pointer.  */

  for (;;)
    {
      CHECK_TYPEDEF (t);
      if (TYPE_CODE (t) != TYPE_CODE_PTR && TYPE_CODE (t) != TYPE_CODE_REF)
	break;
      t = TYPE_TARGET_TYPE (t);
    }

  if (TYPE_CODE (t) == TYPE_CODE_MEMBER)
    error ("not implemented: member type in check_field");

  if (TYPE_CODE (t) != TYPE_CODE_STRUCT
      && TYPE_CODE (t) != TYPE_CODE_UNION)
    error ("Internal error: `this' is not an aggregate");

  return check_field_in (t, name);
}

/* C++: Given an aggregate type CURTYPE, and a member name NAME,
   return the appropriate member.  This function is used to resolve
   user expressions of the form "DOMAIN::NAME".  For more details on
   what happens, see the comment before
   value_struct_elt_for_reference.  */

struct value *
value_aggregate_elt (struct type *curtype,
		     char *name,
		     enum noside noside)
{
  switch (TYPE_CODE (curtype))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      return value_struct_elt_for_reference (curtype, 0, curtype, name, NULL,
					     noside);
    case TYPE_CODE_NAMESPACE:
      return value_namespace_elt (curtype, name, noside);
    default:
      internal_error (__FILE__, __LINE__,
		      "non-aggregate type in value_aggregate_elt");
    }
}

/* C++: Given an aggregate type CURTYPE, and a member name NAME,
   return the address of this member as a "pointer to member"
   type.  If INTYPE is non-null, then it will be the type
   of the member we are looking for.  This will help us resolve
   "pointers to member functions".  This function is used
   to resolve user expressions of the form "DOMAIN::NAME".  */

static struct value *
value_struct_elt_for_reference (struct type *domain, int offset,
				struct type *curtype, char *name,
				struct type *intype,
				enum noside noside)
{
  struct type *t = curtype;
  int i;
  struct value *v;

  if (TYPE_CODE (t) != TYPE_CODE_STRUCT
      && TYPE_CODE (t) != TYPE_CODE_UNION)
    error ("Internal error: non-aggregate type to value_struct_elt_for_reference");

  for (i = TYPE_NFIELDS (t) - 1; i >= TYPE_N_BASECLASSES (t); i--)
    {
      char *t_field_name = TYPE_FIELD_NAME (t, i);

      if (t_field_name && strcmp (t_field_name, name) == 0)
	{
	  if (TYPE_FIELD_STATIC (t, i))
	    {
	      v = value_static_field (t, i);
	      if (v == NULL)
		error ("static field %s has been optimized out",
		       name);
	      return v;
	    }
	  if (TYPE_FIELD_PACKED (t, i))
	    error ("pointers to bitfield members not allowed");

	  return value_from_longest
	    (lookup_reference_type (lookup_member_type (TYPE_FIELD_TYPE (t, i),
							domain)),
	     offset + (LONGEST) (TYPE_FIELD_BITPOS (t, i) >> 3));
	}
    }

  /* C++: If it was not found as a data field, then try to
     return it as a pointer to a method.  */

  /* Destructors are a special case.  */
  if (destructor_name_p (name, t))
    {
      error ("member pointers to destructors not implemented yet");
    }

  /* Perform all necessary dereferencing.  */
  while (intype && TYPE_CODE (intype) == TYPE_CODE_PTR)
    intype = TYPE_TARGET_TYPE (intype);

  for (i = TYPE_NFN_FIELDS (t) - 1; i >= 0; --i)
    {
      char *t_field_name = TYPE_FN_FIELDLIST_NAME (t, i);
      char dem_opname[64];

      if (strncmp (t_field_name, "__", 2) == 0 ||
	  strncmp (t_field_name, "op", 2) == 0 ||
	  strncmp (t_field_name, "type", 4) == 0)
	{
	  if (cplus_demangle_opname (t_field_name, dem_opname, DMGL_ANSI))
	    t_field_name = dem_opname;
	  else if (cplus_demangle_opname (t_field_name, dem_opname, 0))
	    t_field_name = dem_opname;
	}
      if (t_field_name && strcmp (t_field_name, name) == 0)
	{
	  int j = TYPE_FN_FIELDLIST_LENGTH (t, i);
	  struct fn_field *f = TYPE_FN_FIELDLIST1 (t, i);

	  check_stub_method_group (t, i);

	  if (intype == 0 && j > 1)
	    error ("non-unique member `%s' requires type instantiation", name);
	  if (intype)
	    {
	      while (j--)
		if (TYPE_FN_FIELD_TYPE (f, j) == intype)
		  break;
	      if (j < 0)
		error ("no member function matches that type instantiation");
	    }
	  else
	    j = 0;

	  if (TYPE_FN_FIELD_VIRTUAL_P (f, j))
	    {
	      return value_from_longest
		(lookup_reference_type
		 (lookup_member_type (TYPE_FN_FIELD_TYPE (f, j),
				      domain)),
		 (LONGEST) METHOD_PTR_FROM_VOFFSET (TYPE_FN_FIELD_VOFFSET (f, j)));
	    }
	  else
	    {
	      struct symbol *s = lookup_symbol (TYPE_FN_FIELD_PHYSNAME (f, j),
						0, VAR_DOMAIN, 0, NULL);
	      if (s == NULL)
		{
		  v = 0;
		}
	      else
		{
		  v = read_var_value (s, 0);
#if 0
		  VALUE_TYPE (v) = lookup_reference_type
		    (lookup_member_type (TYPE_FN_FIELD_TYPE (f, j),
					 domain));
#endif
		}
	      return v;
	    }
	}
    }
  for (i = TYPE_N_BASECLASSES (t) - 1; i >= 0; i--)
    {
      struct value *v;
      int base_offset;

      if (BASETYPE_VIA_VIRTUAL (t, i))
	base_offset = 0;
      else
	base_offset = TYPE_BASECLASS_BITPOS (t, i) / 8;
      v = value_struct_elt_for_reference (domain,
					  offset + base_offset,
					  TYPE_BASECLASS (t, i),
					  name,
					  intype,
					  noside);
      if (v)
	return v;
    }

  /* As a last chance, pretend that CURTYPE is a namespace, and look
     it up that way; this (frequently) works for types nested inside
     classes.  */

  return value_maybe_namespace_elt (curtype, name, noside);
}

/* C++: Return the member NAME of the namespace given by the type
   CURTYPE.  */

static struct value *
value_namespace_elt (const struct type *curtype,
		     char *name,
		     enum noside noside)
{
  struct value *retval = value_maybe_namespace_elt (curtype, name,
						    noside);

  if (retval == NULL)
    error ("No symbol \"%s\" in namespace \"%s\".", name,
	   TYPE_TAG_NAME (curtype));

  return retval;
}

/* A helper function used by value_namespace_elt and
   value_struct_elt_for_reference.  It looks up NAME inside the
   context CURTYPE; this works if CURTYPE is a namespace or if CURTYPE
   is a class and NAME refers to a type in CURTYPE itself (as opposed
   to, say, some base class of CURTYPE).  */

static struct value *
value_maybe_namespace_elt (const struct type *curtype,
			   char *name,
			   enum noside noside)
{
  const char *namespace_name = TYPE_TAG_NAME (curtype);
  struct symbol *sym;

  sym = cp_lookup_symbol_namespace (namespace_name, name, NULL,
				    get_selected_block (0), VAR_DOMAIN,
				    NULL);

  if (sym == NULL)
    return NULL;
  else if ((noside == EVAL_AVOID_SIDE_EFFECTS)
	   && (SYMBOL_CLASS (sym) == LOC_TYPEDEF))
    return allocate_value (SYMBOL_TYPE (sym));
  else
    return value_of_variable (sym, get_selected_block (0));
}

/* Given a pointer value V, find the real (RTTI) type
   of the object it points to.
   Other parameters FULL, TOP, USING_ENC as with value_rtti_type()
   and refer to the values computed for the object pointed to. */

struct type *
value_rtti_target_type (struct value *v, int *full, int *top, int *using_enc)
{
  struct value *target;

  target = value_ind (v);

  return value_rtti_type (target, full, top, using_enc);
}

/* Given a value pointed to by ARGP, check its real run-time type, and
   if that is different from the enclosing type, create a new value
   using the real run-time type as the enclosing type (and of the same
   type as ARGP) and return it, with the embedded offset adjusted to
   be the correct offset to the enclosed object
   RTYPE is the type, and XFULL, XTOP, and XUSING_ENC are the other
   parameters, computed by value_rtti_type(). If these are available,
   they can be supplied and a second call to value_rtti_type() is avoided.
   (Pass RTYPE == NULL if they're not available */

struct value *
value_full_object (struct value *argp, struct type *rtype, int xfull, int xtop,
		   int xusing_enc)
{
  struct type *real_type;
  int full = 0;
  int top = -1;
  int using_enc = 0;
  struct value *new_val;

  if (rtype)
    {
      real_type = rtype;
      full = xfull;
      top = xtop;
      using_enc = xusing_enc;
    }
  else
    real_type = value_rtti_type (argp, &full, &top, &using_enc);

  /* If no RTTI data, or if object is already complete, do nothing */
  if (!real_type || real_type == VALUE_ENCLOSING_TYPE (argp))
    return argp;

  /* If we have the full object, but for some reason the enclosing
     type is wrong, set it *//* pai: FIXME -- sounds iffy */
  if (full)
    {
      argp = value_change_enclosing_type (argp, real_type);
      return argp;
    }

  /* Check if object is in memory */
  if (VALUE_LVAL (argp) != lval_memory)
    {
      warning ("Couldn't retrieve complete object of RTTI type %s; object may be in register(s).", TYPE_NAME (real_type));

      return argp;
    }

  /* All other cases -- retrieve the complete object */
  /* Go back by the computed top_offset from the beginning of the object,
     adjusting for the embedded offset of argp if that's what value_rtti_type
     used for its computation. */
  new_val = value_at_lazy (real_type, VALUE_ADDRESS (argp) - top +
			   (using_enc ? 0 : VALUE_EMBEDDED_OFFSET (argp)),
			   VALUE_BFD_SECTION (argp));
  VALUE_TYPE (new_val) = VALUE_TYPE (argp);
  VALUE_EMBEDDED_OFFSET (new_val) = using_enc ? top + VALUE_EMBEDDED_OFFSET (argp) : top;
  return new_val;
}




/* Return the value of the local variable, if one exists.
   Flag COMPLAIN signals an error if the request is made in an
   inappropriate context.  */

struct value *
value_of_local (const char *name, int complain)
{
  struct symbol *func, *sym;
  struct block *b;
  struct value * ret;

  if (deprecated_selected_frame == 0)
    {
      if (complain)
	error ("no frame selected");
      else
	return 0;
    }

  func = get_frame_function (deprecated_selected_frame);
  if (!func)
    {
      if (complain)
	error ("no `%s' in nameless context", name);
      else
	return 0;
    }

  b = SYMBOL_BLOCK_VALUE (func);
  if (dict_empty (BLOCK_DICT (b)))
    {
      if (complain)
	error ("no args, no `%s'", name);
      else
	return 0;
    }

  /* Calling lookup_block_symbol is necessary to get the LOC_REGISTER
     symbol instead of the LOC_ARG one (if both exist).  */
  sym = lookup_block_symbol (b, name, NULL, VAR_DOMAIN);
  if (sym == NULL)
    {
      if (complain)
	error ("current stack frame does not contain a variable named `%s'", name);
      else
	return NULL;
    }

  ret = read_var_value (sym, deprecated_selected_frame);
  if (ret == 0 && complain)
    error ("`%s' argument unreadable", name);
  return ret;
}

/* C++/Objective-C: return the value of the class instance variable,
   if one exists.  Flag COMPLAIN signals an error if the request is
   made in an inappropriate context.  */

struct value *
value_of_this (int complain)
{
  if (current_language->la_language == language_objc)
    return value_of_local ("self", complain);
  else
    return value_of_local ("this", complain);
}

/* Create a slice (sub-string, sub-array) of ARRAY, that is LENGTH elements
   long, starting at LOWBOUND.  The result has the same lower bound as
   the original ARRAY.  */

struct value *
value_slice (struct value *array, int lowbound, int length)
{
  struct type *slice_range_type, *slice_type, *range_type;
  LONGEST lowerbound, upperbound;
  struct value *slice;
  struct type *array_type;
  array_type = check_typedef (VALUE_TYPE (array));
  COERCE_VARYING_ARRAY (array, array_type);
  if (TYPE_CODE (array_type) != TYPE_CODE_ARRAY
      && TYPE_CODE (array_type) != TYPE_CODE_STRING
      && TYPE_CODE (array_type) != TYPE_CODE_BITSTRING)
    error ("cannot take slice of non-array");
  range_type = TYPE_INDEX_TYPE (array_type);
  if (get_discrete_bounds (range_type, &lowerbound, &upperbound) < 0)
    error ("slice from bad array or bitstring");
  if (lowbound < lowerbound || length < 0
      || lowbound + length - 1 > upperbound)
    error ("slice out of range");
  /* FIXME-type-allocation: need a way to free this type when we are
     done with it.  */
  slice_range_type = create_range_type ((struct type *) NULL,
					TYPE_TARGET_TYPE (range_type),
					lowbound, lowbound + length - 1);
  if (TYPE_CODE (array_type) == TYPE_CODE_BITSTRING)
    {
      int i;
      slice_type = create_set_type ((struct type *) NULL, slice_range_type);
      TYPE_CODE (slice_type) = TYPE_CODE_BITSTRING;
      slice = value_zero (slice_type, not_lval);
      for (i = 0; i < length; i++)
	{
	  int element = value_bit_index (array_type,
					 VALUE_CONTENTS (array),
					 lowbound + i);
	  if (element < 0)
	    error ("internal error accessing bitstring");
	  else if (element > 0)
	    {
	      int j = i % TARGET_CHAR_BIT;
	      if (BITS_BIG_ENDIAN)
		j = TARGET_CHAR_BIT - 1 - j;
	      VALUE_CONTENTS_RAW (slice)[i / TARGET_CHAR_BIT] |= (1 << j);
	    }
	}
      /* We should set the address, bitssize, and bitspos, so the clice
         can be used on the LHS, but that may require extensions to
         value_assign.  For now, just leave as a non_lval.  FIXME.  */
    }
  else
    {
      struct type *element_type = TYPE_TARGET_TYPE (array_type);
      LONGEST offset
	= (lowbound - lowerbound) * TYPE_LENGTH (check_typedef (element_type));
      slice_type = create_array_type ((struct type *) NULL, element_type,
				      slice_range_type);
      TYPE_CODE (slice_type) = TYPE_CODE (array_type);
      slice = allocate_value (slice_type);
      if (VALUE_LAZY (array))
	VALUE_LAZY (slice) = 1;
      else
	memcpy (VALUE_CONTENTS (slice), VALUE_CONTENTS (array) + offset,
		TYPE_LENGTH (slice_type));
      if (VALUE_LVAL (array) == lval_internalvar)
	VALUE_LVAL (slice) = lval_internalvar_component;
      else
	VALUE_LVAL (slice) = VALUE_LVAL (array);
      VALUE_ADDRESS (slice) = VALUE_ADDRESS (array);
      VALUE_OFFSET (slice) = VALUE_OFFSET (array) + offset;
    }
  return slice;
}

/* Create a value for a FORTRAN complex number.  Currently most of
   the time values are coerced to COMPLEX*16 (i.e. a complex number
   composed of 2 doubles.  This really should be a smarter routine
   that figures out precision inteligently as opposed to assuming
   doubles. FIXME: fmb */

struct value *
value_literal_complex (struct value *arg1, struct value *arg2, struct type *type)
{
  struct value *val;
  struct type *real_type = TYPE_TARGET_TYPE (type);

  val = allocate_value (type);
  arg1 = value_cast (real_type, arg1);
  arg2 = value_cast (real_type, arg2);

  memcpy (VALUE_CONTENTS_RAW (val),
	  VALUE_CONTENTS (arg1), TYPE_LENGTH (real_type));
  memcpy (VALUE_CONTENTS_RAW (val) + TYPE_LENGTH (real_type),
	  VALUE_CONTENTS (arg2), TYPE_LENGTH (real_type));
  return val;
}

/* Cast a value into the appropriate complex data type. */

static struct value *
cast_into_complex (struct type *type, struct value *val)
{
  struct type *real_type = TYPE_TARGET_TYPE (type);
  if (TYPE_CODE (VALUE_TYPE (val)) == TYPE_CODE_COMPLEX)
    {
      struct type *val_real_type = TYPE_TARGET_TYPE (VALUE_TYPE (val));
      struct value *re_val = allocate_value (val_real_type);
      struct value *im_val = allocate_value (val_real_type);

      memcpy (VALUE_CONTENTS_RAW (re_val),
	      VALUE_CONTENTS (val), TYPE_LENGTH (val_real_type));
      memcpy (VALUE_CONTENTS_RAW (im_val),
	      VALUE_CONTENTS (val) + TYPE_LENGTH (val_real_type),
	      TYPE_LENGTH (val_real_type));

      return value_literal_complex (re_val, im_val, type);
    }
  else if (TYPE_CODE (VALUE_TYPE (val)) == TYPE_CODE_FLT
	   || TYPE_CODE (VALUE_TYPE (val)) == TYPE_CODE_INT)
    return value_literal_complex (val, value_zero (real_type, not_lval), type);
  else
    error ("cannot cast non-number to complex");
}

void
_initialize_valops (void)
{
#if 0
  add_show_from_set
    (add_set_cmd ("abandon", class_support, var_boolean, (char *) &auto_abandon,
		  "Set automatic abandonment of expressions upon failure.",
		  &setlist),
     &showlist);
#endif

  add_show_from_set
    (add_set_cmd ("overload-resolution", class_support, var_boolean, (char *) &overload_resolution,
		  "Set overload resolution in evaluating C++ functions.",
		  &setlist),
     &showlist);
  overload_resolution = 1;
}
