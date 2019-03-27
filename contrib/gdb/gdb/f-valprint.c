/* Support for printing Fortran values for GDB, the GNU debugger.
   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2003
   Free Software Foundation, Inc.
   Contributed by Motorola.  Adapted from the C definitions by Farooq Butt
   (fmbutt@engage.sps.mot.com), additionally worked over by Stan Shebs.

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
#include "gdb_string.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "value.h"
#include "valprint.h"
#include "language.h"
#include "f-lang.h"
#include "frame.h"
#include "gdbcore.h"
#include "command.h"
#include "block.h"

#if 0
static int there_is_a_visible_common_named (char *);
#endif

extern void _initialize_f_valprint (void);
static void info_common_command (char *, int);
static void list_all_visible_commons (char *);
static void f77_print_array (struct type *, char *, CORE_ADDR,
			     struct ui_file *, int, int, int,
			     enum val_prettyprint);
static void f77_print_array_1 (int, int, struct type *, char *,
			       CORE_ADDR, struct ui_file *, int, int, int,
			       enum val_prettyprint,
			       int *elts);
static void f77_create_arrayprint_offset_tbl (struct type *,
					      struct ui_file *);
static void f77_get_dynamic_length_of_aggregate (struct type *);

int f77_array_offset_tbl[MAX_FORTRAN_DIMS + 1][2];

/* Array which holds offsets to be applied to get a row's elements
   for a given array. Array also holds the size of each subarray.  */

/* The following macro gives us the size of the nth dimension, Where 
   n is 1 based. */

#define F77_DIM_SIZE(n) (f77_array_offset_tbl[n][1])

/* The following gives us the offset for row n where n is 1-based. */

#define F77_DIM_OFFSET(n) (f77_array_offset_tbl[n][0])

int
f77_get_dynamic_lowerbound (struct type *type, int *lower_bound)
{
  CORE_ADDR current_frame_addr;
  CORE_ADDR ptr_to_lower_bound;

  switch (TYPE_ARRAY_LOWER_BOUND_TYPE (type))
    {
    case BOUND_BY_VALUE_ON_STACK:
      current_frame_addr = get_frame_base (deprecated_selected_frame);
      if (current_frame_addr > 0)
	{
	  *lower_bound =
	    read_memory_integer (current_frame_addr +
				 TYPE_ARRAY_LOWER_BOUND_VALUE (type),
				 4);
	}
      else
	{
	  *lower_bound = DEFAULT_LOWER_BOUND;
	  return BOUND_FETCH_ERROR;
	}
      break;

    case BOUND_SIMPLE:
      *lower_bound = TYPE_ARRAY_LOWER_BOUND_VALUE (type);
      break;

    case BOUND_CANNOT_BE_DETERMINED:
      error ("Lower bound may not be '*' in F77");
      break;

    case BOUND_BY_REF_ON_STACK:
      current_frame_addr = get_frame_base (deprecated_selected_frame);
      if (current_frame_addr > 0)
	{
	  ptr_to_lower_bound =
	    read_memory_typed_address (current_frame_addr +
				       TYPE_ARRAY_LOWER_BOUND_VALUE (type),
				       builtin_type_void_data_ptr);
	  *lower_bound = read_memory_integer (ptr_to_lower_bound, 4);
	}
      else
	{
	  *lower_bound = DEFAULT_LOWER_BOUND;
	  return BOUND_FETCH_ERROR;
	}
      break;

    case BOUND_BY_REF_IN_REG:
    case BOUND_BY_VALUE_IN_REG:
    default:
      error ("??? unhandled dynamic array bound type ???");
      break;
    }
  return BOUND_FETCH_OK;
}

int
f77_get_dynamic_upperbound (struct type *type, int *upper_bound)
{
  CORE_ADDR current_frame_addr = 0;
  CORE_ADDR ptr_to_upper_bound;

  switch (TYPE_ARRAY_UPPER_BOUND_TYPE (type))
    {
    case BOUND_BY_VALUE_ON_STACK:
      current_frame_addr = get_frame_base (deprecated_selected_frame);
      if (current_frame_addr > 0)
	{
	  *upper_bound =
	    read_memory_integer (current_frame_addr +
				 TYPE_ARRAY_UPPER_BOUND_VALUE (type),
				 4);
	}
      else
	{
	  *upper_bound = DEFAULT_UPPER_BOUND;
	  return BOUND_FETCH_ERROR;
	}
      break;

    case BOUND_SIMPLE:
      *upper_bound = TYPE_ARRAY_UPPER_BOUND_VALUE (type);
      break;

    case BOUND_CANNOT_BE_DETERMINED:
      /* we have an assumed size array on our hands. Assume that 
         upper_bound == lower_bound so that we show at least 
         1 element.If the user wants to see more elements, let 
         him manually ask for 'em and we'll subscript the 
         array and show him */
      f77_get_dynamic_lowerbound (type, upper_bound);
      break;

    case BOUND_BY_REF_ON_STACK:
      current_frame_addr = get_frame_base (deprecated_selected_frame);
      if (current_frame_addr > 0)
	{
	  ptr_to_upper_bound =
	    read_memory_typed_address (current_frame_addr +
				       TYPE_ARRAY_UPPER_BOUND_VALUE (type),
				       builtin_type_void_data_ptr);
	  *upper_bound = read_memory_integer (ptr_to_upper_bound, 4);
	}
      else
	{
	  *upper_bound = DEFAULT_UPPER_BOUND;
	  return BOUND_FETCH_ERROR;
	}
      break;

    case BOUND_BY_REF_IN_REG:
    case BOUND_BY_VALUE_IN_REG:
    default:
      error ("??? unhandled dynamic array bound type ???");
      break;
    }
  return BOUND_FETCH_OK;
}

/* Obtain F77 adjustable array dimensions */

static void
f77_get_dynamic_length_of_aggregate (struct type *type)
{
  int upper_bound = -1;
  int lower_bound = 1;
  int retcode;

  /* Recursively go all the way down into a possibly multi-dimensional
     F77 array and get the bounds.  For simple arrays, this is pretty
     easy but when the bounds are dynamic, we must be very careful 
     to add up all the lengths correctly.  Not doing this right 
     will lead to horrendous-looking arrays in parameter lists.

     This function also works for strings which behave very 
     similarly to arrays.  */

  if (TYPE_CODE (TYPE_TARGET_TYPE (type)) == TYPE_CODE_ARRAY
      || TYPE_CODE (TYPE_TARGET_TYPE (type)) == TYPE_CODE_STRING)
    f77_get_dynamic_length_of_aggregate (TYPE_TARGET_TYPE (type));

  /* Recursion ends here, start setting up lengths.  */
  retcode = f77_get_dynamic_lowerbound (type, &lower_bound);
  if (retcode == BOUND_FETCH_ERROR)
    error ("Cannot obtain valid array lower bound");

  retcode = f77_get_dynamic_upperbound (type, &upper_bound);
  if (retcode == BOUND_FETCH_ERROR)
    error ("Cannot obtain valid array upper bound");

  /* Patch in a valid length value. */

  TYPE_LENGTH (type) =
    (upper_bound - lower_bound + 1) * TYPE_LENGTH (check_typedef (TYPE_TARGET_TYPE (type)));
}

/* Function that sets up the array offset,size table for the array 
   type "type".  */

static void
f77_create_arrayprint_offset_tbl (struct type *type, struct ui_file *stream)
{
  struct type *tmp_type;
  int eltlen;
  int ndimen = 1;
  int upper, lower, retcode;

  tmp_type = type;

  while ((TYPE_CODE (tmp_type) == TYPE_CODE_ARRAY))
    {
      if (TYPE_ARRAY_UPPER_BOUND_TYPE (tmp_type) == BOUND_CANNOT_BE_DETERMINED)
	fprintf_filtered (stream, "<assumed size array> ");

      retcode = f77_get_dynamic_upperbound (tmp_type, &upper);
      if (retcode == BOUND_FETCH_ERROR)
	error ("Cannot obtain dynamic upper bound");

      retcode = f77_get_dynamic_lowerbound (tmp_type, &lower);
      if (retcode == BOUND_FETCH_ERROR)
	error ("Cannot obtain dynamic lower bound");

      F77_DIM_SIZE (ndimen) = upper - lower + 1;

      tmp_type = TYPE_TARGET_TYPE (tmp_type);
      ndimen++;
    }

  /* Now we multiply eltlen by all the offsets, so that later we 
     can print out array elements correctly.  Up till now we 
     know an offset to apply to get the item but we also 
     have to know how much to add to get to the next item */

  ndimen--;
  eltlen = TYPE_LENGTH (tmp_type);
  F77_DIM_OFFSET (ndimen) = eltlen;
  while (--ndimen > 0)
    {
      eltlen *= F77_DIM_SIZE (ndimen + 1);
      F77_DIM_OFFSET (ndimen) = eltlen;
    }
}



/* Actual function which prints out F77 arrays, Valaddr == address in 
   the superior.  Address == the address in the inferior.  */

static void
f77_print_array_1 (int nss, int ndimensions, struct type *type, char *valaddr,
		   CORE_ADDR address, struct ui_file *stream, int format,
		   int deref_ref, int recurse, enum val_prettyprint pretty,
		   int *elts)
{
  int i;

  if (nss != ndimensions)
    {
      for (i = 0; (i < F77_DIM_SIZE (nss) && (*elts) < print_max); i++)
	{
	  fprintf_filtered (stream, "( ");
	  f77_print_array_1 (nss + 1, ndimensions, TYPE_TARGET_TYPE (type),
			     valaddr + i * F77_DIM_OFFSET (nss),
			     address + i * F77_DIM_OFFSET (nss),
			     stream, format, deref_ref, recurse, pretty, elts);
	  fprintf_filtered (stream, ") ");
	}
      if (*elts >= print_max && i < F77_DIM_SIZE (nss)) 
	fprintf_filtered (stream, "...");
    }
  else
    {
      for (i = 0; i < F77_DIM_SIZE (nss) && (*elts) < print_max; 
	   i++, (*elts)++)
	{
	  val_print (TYPE_TARGET_TYPE (type),
		     valaddr + i * F77_DIM_OFFSET (ndimensions),
		     0,
		     address + i * F77_DIM_OFFSET (ndimensions),
		     stream, format, deref_ref, recurse, pretty);

	  if (i != (F77_DIM_SIZE (nss) - 1))
	    fprintf_filtered (stream, ", ");

	  if ((*elts == print_max - 1) && (i != (F77_DIM_SIZE (nss) - 1)))
	    fprintf_filtered (stream, "...");
	}
    }
}

/* This function gets called to print an F77 array, we set up some 
   stuff and then immediately call f77_print_array_1() */

static void
f77_print_array (struct type *type, char *valaddr, CORE_ADDR address,
		 struct ui_file *stream, int format, int deref_ref, int recurse,
		 enum val_prettyprint pretty)
{
  int ndimensions;
  int elts = 0;

  ndimensions = calc_f77_array_dims (type);

  if (ndimensions > MAX_FORTRAN_DIMS || ndimensions < 0)
    error ("Type node corrupt! F77 arrays cannot have %d subscripts (%d Max)",
	   ndimensions, MAX_FORTRAN_DIMS);

  /* Since F77 arrays are stored column-major, we set up an 
     offset table to get at the various row's elements. The 
     offset table contains entries for both offset and subarray size. */

  f77_create_arrayprint_offset_tbl (type, stream);

  f77_print_array_1 (1, ndimensions, type, valaddr, address, stream, format,
		     deref_ref, recurse, pretty, &elts);
}


/* Print data of type TYPE located at VALADDR (within GDB), which came from
   the inferior at address ADDRESS, onto stdio stream STREAM according to
   FORMAT (a letter or 0 for natural format).  The data at VALADDR is in
   target byte order.

   If the data are a string pointer, returns the number of string characters
   printed.

   If DEREF_REF is nonzero, then dereference references, otherwise just print
   them like pointers.

   The PRETTY parameter controls prettyprinting.  */

int
f_val_print (struct type *type, char *valaddr, int embedded_offset,
	     CORE_ADDR address, struct ui_file *stream, int format,
	     int deref_ref, int recurse, enum val_prettyprint pretty)
{
  unsigned int i = 0;	/* Number of characters printed */
  struct type *elttype;
  LONGEST val;
  CORE_ADDR addr;

  CHECK_TYPEDEF (type);
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_STRING:
      f77_get_dynamic_length_of_aggregate (type);
      LA_PRINT_STRING (stream, valaddr, TYPE_LENGTH (type), 1, 0);
      break;

    case TYPE_CODE_ARRAY:
      fprintf_filtered (stream, "(");
      f77_print_array (type, valaddr, address, stream, format,
		       deref_ref, recurse, pretty);
      fprintf_filtered (stream, ")");
      break;

    case TYPE_CODE_PTR:
      if (format && format != 's')
	{
	  print_scalar_formatted (valaddr, type, format, 0, stream);
	  break;
	}
      else
	{
	  addr = unpack_pointer (type, valaddr);
	  elttype = check_typedef (TYPE_TARGET_TYPE (type));

	  if (TYPE_CODE (elttype) == TYPE_CODE_FUNC)
	    {
	      /* Try to print what function it points to.  */
	      print_address_demangle (addr, stream, demangle);
	      /* Return value is irrelevant except for string pointers.  */
	      return 0;
	    }

	  if (addressprint && format != 's')
	    print_address_numeric (addr, 1, stream);

	  /* For a pointer to char or unsigned char, also print the string
	     pointed to, unless pointer is null.  */
	  if (TYPE_LENGTH (elttype) == 1
	      && TYPE_CODE (elttype) == TYPE_CODE_INT
	      && (format == 0 || format == 's')
	      && addr != 0)
	    i = val_print_string (addr, -1, TYPE_LENGTH (elttype), stream);

	  /* Return number of characters printed, including the terminating
	     '\0' if we reached the end.  val_print_string takes care including
	     the terminating '\0' if necessary.  */
	  return i;
	}
      break;

    case TYPE_CODE_REF:
      elttype = check_typedef (TYPE_TARGET_TYPE (type));
      if (addressprint)
	{
	  CORE_ADDR addr
	    = extract_typed_address (valaddr + embedded_offset, type);
	  fprintf_filtered (stream, "@");
	  print_address_numeric (addr, 1, stream);
	  if (deref_ref)
	    fputs_filtered (": ", stream);
	}
      /* De-reference the reference.  */
      if (deref_ref)
	{
	  if (TYPE_CODE (elttype) != TYPE_CODE_UNDEF)
	    {
	      struct value *deref_val =
	      value_at
	      (TYPE_TARGET_TYPE (type),
	       unpack_pointer (lookup_pointer_type (builtin_type_void),
			       valaddr + embedded_offset),
	       NULL);
	      common_val_print (deref_val, stream, format, deref_ref, recurse,
				pretty);
	    }
	  else
	    fputs_filtered ("???", stream);
	}
      break;

    case TYPE_CODE_FUNC:
      if (format)
	{
	  print_scalar_formatted (valaddr, type, format, 0, stream);
	  break;
	}
      /* FIXME, we should consider, at least for ANSI C language, eliminating
         the distinction made between FUNCs and POINTERs to FUNCs.  */
      fprintf_filtered (stream, "{");
      type_print (type, "", stream, -1);
      fprintf_filtered (stream, "} ");
      /* Try to print what function it points to, and its address.  */
      print_address_demangle (address, stream, demangle);
      break;

    case TYPE_CODE_INT:
      format = format ? format : output_format;
      if (format)
	print_scalar_formatted (valaddr, type, format, 0, stream);
      else
	{
	  val_print_type_code_int (type, valaddr, stream);
	  /* C and C++ has no single byte int type, char is used instead.
	     Since we don't know whether the value is really intended to
	     be used as an integer or a character, print the character
	     equivalent as well. */
	  if (TYPE_LENGTH (type) == 1)
	    {
	      fputs_filtered (" ", stream);
	      LA_PRINT_CHAR ((unsigned char) unpack_long (type, valaddr),
			     stream);
	    }
	}
      break;

    case TYPE_CODE_FLT:
      if (format)
	print_scalar_formatted (valaddr, type, format, 0, stream);
      else
	print_floating (valaddr, type, stream);
      break;

    case TYPE_CODE_VOID:
      fprintf_filtered (stream, "VOID");
      break;

    case TYPE_CODE_ERROR:
      fprintf_filtered (stream, "<error type>");
      break;

    case TYPE_CODE_RANGE:
      /* FIXME, we should not ever have to print one of these yet.  */
      fprintf_filtered (stream, "<range type>");
      break;

    case TYPE_CODE_BOOL:
      format = format ? format : output_format;
      if (format)
	print_scalar_formatted (valaddr, type, format, 0, stream);
      else
	{
	  val = 0;
	  switch (TYPE_LENGTH (type))
	    {
	    case 1:
	      val = unpack_long (builtin_type_f_logical_s1, valaddr);
	      break;

	    case 2:
	      val = unpack_long (builtin_type_f_logical_s2, valaddr);
	      break;

	    case 4:
	      val = unpack_long (builtin_type_f_logical, valaddr);
	      break;

	    default:
	      error ("Logicals of length %d bytes not supported",
		     TYPE_LENGTH (type));

	    }

	  if (val == 0)
	    fprintf_filtered (stream, ".FALSE.");
	  else if (val == 1)
	    fprintf_filtered (stream, ".TRUE.");
	  else
	    /* Not a legitimate logical type, print as an integer.  */
	    {
	      /* Bash the type code temporarily.  */
	      TYPE_CODE (type) = TYPE_CODE_INT;
	      f_val_print (type, valaddr, 0, address, stream, format,
			   deref_ref, recurse, pretty);
	      /* Restore the type code so later uses work as intended. */
	      TYPE_CODE (type) = TYPE_CODE_BOOL;
	    }
	}
      break;

    case TYPE_CODE_COMPLEX:
      switch (TYPE_LENGTH (type))
	{
	case 8:
	  type = builtin_type_f_real;
	  break;
	case 16:
	  type = builtin_type_f_real_s8;
	  break;
	case 32:
	  type = builtin_type_f_real_s16;
	  break;
	default:
	  error ("Cannot print out complex*%d variables", TYPE_LENGTH (type));
	}
      fputs_filtered ("(", stream);
      print_floating (valaddr, type, stream);
      fputs_filtered (",", stream);
      print_floating (valaddr + TYPE_LENGTH (type), type, stream);
      fputs_filtered (")", stream);
      break;

    case TYPE_CODE_UNDEF:
      /* This happens (without TYPE_FLAG_STUB set) on systems which don't use
         dbx xrefs (NO_DBX_XREFS in gcc) if a file has a "struct foo *bar"
         and no complete type for struct foo in that file.  */
      fprintf_filtered (stream, "<incomplete type>");
      break;

    default:
      error ("Invalid F77 type code %d in symbol table.", TYPE_CODE (type));
    }
  gdb_flush (stream);
  return 0;
}

static void
list_all_visible_commons (char *funname)
{
  SAVED_F77_COMMON_PTR tmp;

  tmp = head_common_list;

  printf_filtered ("All COMMON blocks visible at this level:\n\n");

  while (tmp != NULL)
    {
      if (strcmp (tmp->owning_function, funname) == 0)
	printf_filtered ("%s\n", tmp->name);

      tmp = tmp->next;
    }
}

/* This function is used to print out the values in a given COMMON 
   block. It will always use the most local common block of the 
   given name */

static void
info_common_command (char *comname, int from_tty)
{
  SAVED_F77_COMMON_PTR the_common;
  COMMON_ENTRY_PTR entry;
  struct frame_info *fi;
  char *funname = 0;
  struct symbol *func;

  /* We have been told to display the contents of F77 COMMON 
     block supposedly visible in this function.  Let us 
     first make sure that it is visible and if so, let 
     us display its contents */

  fi = deprecated_selected_frame;

  if (fi == NULL)
    error ("No frame selected");

  /* The following is generally ripped off from stack.c's routine 
     print_frame_info() */

  func = find_pc_function (get_frame_pc (fi));
  if (func)
    {
      /* In certain pathological cases, the symtabs give the wrong
         function (when we are in the first function in a file which
         is compiled without debugging symbols, the previous function
         is compiled with debugging symbols, and the "foo.o" symbol
         that is supposed to tell us where the file with debugging symbols
         ends has been truncated by ar because it is longer than 15
         characters).

         So look in the minimal symbol tables as well, and if it comes
         up with a larger address for the function use that instead.
         I don't think this can ever cause any problems; there shouldn't
         be any minimal symbols in the middle of a function.
         FIXME:  (Not necessarily true.  What about text labels) */

      struct minimal_symbol *msymbol = lookup_minimal_symbol_by_pc (get_frame_pc (fi));

      if (msymbol != NULL
	  && (SYMBOL_VALUE_ADDRESS (msymbol)
	      > BLOCK_START (SYMBOL_BLOCK_VALUE (func))))
	funname = DEPRECATED_SYMBOL_NAME (msymbol);
      else
	funname = DEPRECATED_SYMBOL_NAME (func);
    }
  else
    {
      struct minimal_symbol *msymbol =
      lookup_minimal_symbol_by_pc (get_frame_pc (fi));

      if (msymbol != NULL)
	funname = DEPRECATED_SYMBOL_NAME (msymbol);
    }

  /* If comname is NULL, we assume the user wishes to see the 
     which COMMON blocks are visible here and then return */

  if (comname == 0)
    {
      list_all_visible_commons (funname);
      return;
    }

  the_common = find_common_for_function (comname, funname);

  if (the_common)
    {
      if (strcmp (comname, BLANK_COMMON_NAME_LOCAL) == 0)
	printf_filtered ("Contents of blank COMMON block:\n");
      else
	printf_filtered ("Contents of F77 COMMON block '%s':\n", comname);

      printf_filtered ("\n");
      entry = the_common->entries;

      while (entry != NULL)
	{
	  printf_filtered ("%s = ", DEPRECATED_SYMBOL_NAME (entry->symbol));
	  print_variable_value (entry->symbol, fi, gdb_stdout);
	  printf_filtered ("\n");
	  entry = entry->next;
	}
    }
  else
    printf_filtered ("Cannot locate the common block %s in function '%s'\n",
		     comname, funname);
}

/* This function is used to determine whether there is a
   F77 common block visible at the current scope called 'comname'. */

#if 0
static int
there_is_a_visible_common_named (char *comname)
{
  SAVED_F77_COMMON_PTR the_common;
  struct frame_info *fi;
  char *funname = 0;
  struct symbol *func;

  if (comname == NULL)
    error ("Cannot deal with NULL common name!");

  fi = deprecated_selected_frame;

  if (fi == NULL)
    error ("No frame selected");

  /* The following is generally ripped off from stack.c's routine 
     print_frame_info() */

  func = find_pc_function (fi->pc);
  if (func)
    {
      /* In certain pathological cases, the symtabs give the wrong
         function (when we are in the first function in a file which
         is compiled without debugging symbols, the previous function
         is compiled with debugging symbols, and the "foo.o" symbol
         that is supposed to tell us where the file with debugging symbols
         ends has been truncated by ar because it is longer than 15
         characters).

         So look in the minimal symbol tables as well, and if it comes
         up with a larger address for the function use that instead.
         I don't think this can ever cause any problems; there shouldn't
         be any minimal symbols in the middle of a function.
         FIXME:  (Not necessarily true.  What about text labels) */

      struct minimal_symbol *msymbol = lookup_minimal_symbol_by_pc (fi->pc);

      if (msymbol != NULL
	  && (SYMBOL_VALUE_ADDRESS (msymbol)
	      > BLOCK_START (SYMBOL_BLOCK_VALUE (func))))
	funname = DEPRECATED_SYMBOL_NAME (msymbol);
      else
	funname = DEPRECATED_SYMBOL_NAME (func);
    }
  else
    {
      struct minimal_symbol *msymbol =
      lookup_minimal_symbol_by_pc (fi->pc);

      if (msymbol != NULL)
	funname = DEPRECATED_SYMBOL_NAME (msymbol);
    }

  the_common = find_common_for_function (comname, funname);

  return (the_common ? 1 : 0);
}
#endif

void
_initialize_f_valprint (void)
{
  add_info ("common", info_common_command,
	    "Print out the values contained in a Fortran COMMON block.");
  if (xdb_commands)
    add_com ("lc", class_info, info_common_command,
	     "Print out the values contained in a Fortran COMMON block.");
}
