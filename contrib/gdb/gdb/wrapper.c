/* Longjump free calls to gdb internal routines.
   Copyright 1999, 2000 Free Software Foundation, Inc.

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
#include "wrapper.h"

/* Use this struct to pass arguments to wrapper routines. We assume
   (arbitrarily) that no gdb function takes more than ten arguments. */
struct gdb_wrapper_arguments
  {

    /* Pointer to some result from the gdb function call, if any */
    union wrapper_results 
      {
	int   integer;
	void *pointer;
      } result;
	

    /* The list of arguments. */
    union wrapper_args 
      {
	int   integer;
	void *pointer;
      } args[10];
  };

struct captured_value_struct_elt_args
{
  struct value **argp;
  struct value **args;
  char *name;
  int *static_memfuncp;
  char *err;
  struct value **result_ptr;
};

static int wrap_parse_exp_1 (char *);

static int wrap_evaluate_expression (char *);

static int wrap_value_fetch_lazy (char *);

static int wrap_value_equal (char *);

static int wrap_value_assign (char *);

static int wrap_value_subscript (char *);

static int wrap_value_ind (char *opaque_arg);

static int do_captured_value_struct_elt (struct ui_out *uiout, void *data);

static int wrap_parse_and_eval_type (char *);

int
gdb_parse_exp_1 (char **stringptr, struct block *block, int comma,
		 struct expression **expression)
{
  struct gdb_wrapper_arguments args;
  args.args[0].pointer = stringptr;
  args.args[1].pointer = block;
  args.args[2].integer = comma;

  if (!catch_errors ((catch_errors_ftype *) wrap_parse_exp_1, &args,
		     "", RETURN_MASK_ERROR))
    {
      /* An error occurred */
      return 0;
    }

  *expression = (struct expression *) args.result.pointer;
  return 1;
  
}

static int
wrap_parse_exp_1 (char *argptr)
{
  struct gdb_wrapper_arguments *args 
    = (struct gdb_wrapper_arguments *) argptr;
  args->result.pointer = parse_exp_1((char **) args->args[0].pointer,
				     (struct block *) args->args[1].pointer,
				     args->args[2].integer);
  return 1;
}

int
gdb_evaluate_expression (struct expression *exp, struct value **value)
{
  struct gdb_wrapper_arguments args;
  args.args[0].pointer = exp;

  if (!catch_errors ((catch_errors_ftype *) wrap_evaluate_expression, &args,
		     "", RETURN_MASK_ERROR))
    {
      /* An error occurred */
      return 0;
    }

  *value = (struct value *) args.result.pointer;
  return 1;
}

static int
wrap_evaluate_expression (char *a)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) a;

  (args)->result.pointer =
    (char *) evaluate_expression ((struct expression *) args->args[0].pointer);
  return 1;
}

int
gdb_value_fetch_lazy (struct value *value)
{
  struct gdb_wrapper_arguments args;

  args.args[0].pointer = value;
  return catch_errors ((catch_errors_ftype *) wrap_value_fetch_lazy, &args,
		       "", RETURN_MASK_ERROR);
}

static int
wrap_value_fetch_lazy (char *a)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) a;

  value_fetch_lazy ((struct value *) (args)->args[0].pointer);
  return 1;
}

int
gdb_value_equal (struct value *val1, struct value *val2, int *result)
{
  struct gdb_wrapper_arguments args;

  args.args[0].pointer = val1;
  args.args[1].pointer = val2;

  if (!catch_errors ((catch_errors_ftype *) wrap_value_equal, &args,
		     "", RETURN_MASK_ERROR))
    {
      /* An error occurred */
      return 0;
    }

  *result = args.result.integer;
  return 1;
}

static int
wrap_value_equal (char *a)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) a;
  struct value *val1;
  struct value *val2;

  val1 = (struct value *) (args)->args[0].pointer;
  val2 = (struct value *) (args)->args[1].pointer;

  (args)->result.integer = value_equal (val1, val2);
  return 1;
}

int
gdb_value_assign (struct value *val1, struct value *val2, struct value **result)
{
  struct gdb_wrapper_arguments args;

  args.args[0].pointer = val1;
  args.args[1].pointer = val2;

  if (!catch_errors ((catch_errors_ftype *) wrap_value_assign, &args,
		     "", RETURN_MASK_ERROR))
    {
      /* An error occurred */
      return 0;
    }

  *result = (struct value *) args.result.pointer;
  return 1;
}

static int
wrap_value_assign (char *a)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) a;
  struct value *val1;
  struct value *val2;

  val1 = (struct value *) (args)->args[0].pointer;
  val2 = (struct value *) (args)->args[1].pointer;

  (args)->result.pointer = value_assign (val1, val2);
  return 1;
}

int
gdb_value_subscript (struct value *val1, struct value *val2, struct value **rval)
{
  struct gdb_wrapper_arguments args;

  args.args[0].pointer = val1;
  args.args[1].pointer = val2;

  if (!catch_errors ((catch_errors_ftype *) wrap_value_subscript, &args,
		     "", RETURN_MASK_ERROR))
    {
      /* An error occurred */
      return 0;
    }

  *rval = (struct value *) args.result.pointer;
  return 1;
}

static int
wrap_value_subscript (char *a)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) a;
  struct value *val1;
  struct value *val2;

  val1 = (struct value *) (args)->args[0].pointer;
  val2 = (struct value *) (args)->args[1].pointer;

  (args)->result.pointer = value_subscript (val1, val2);
  return 1;
}

int
gdb_value_ind (struct value *val, struct value **rval)
{
  struct gdb_wrapper_arguments args;

  args.args[0].pointer = val;

  if (!catch_errors ((catch_errors_ftype *) wrap_value_ind, &args,
		     "", RETURN_MASK_ERROR))
    {
      /* An error occurred */
      return 0;
    }

  *rval = (struct value *) args.result.pointer;
  return 1;
}

static int
wrap_value_ind (char *opaque_arg)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) opaque_arg;
  struct value *val;

  val = (struct value *) (args)->args[0].pointer;
  (args)->result.pointer = value_ind (val);
  return 1;
}

int
gdb_parse_and_eval_type (char *p, int length, struct type **type)
{
  struct gdb_wrapper_arguments args;
  args.args[0].pointer = p;
  args.args[1].integer = length;

  if (!catch_errors ((catch_errors_ftype *) wrap_parse_and_eval_type, &args,
		     "", RETURN_MASK_ALL))
    {
      /* An error occurred */
      return 0;
    }

  *type = (struct type *) args.result.pointer;
  return 1;
}

static int
wrap_parse_and_eval_type (char *a)
{
  struct gdb_wrapper_arguments *args = (struct gdb_wrapper_arguments *) a;

  char *p = (char *) args->args[0].pointer;
  int length = args->args[1].integer;

  args->result.pointer = (char *) parse_and_eval_type (p, length);

  return 1;
}

enum gdb_rc
gdb_value_struct_elt (struct ui_out *uiout, struct value **result, struct value **argp,
		      struct value **args, char *name, int *static_memfuncp,
		      char *err)
{
  struct captured_value_struct_elt_args cargs;
  cargs.argp = argp;
  cargs.args = args;
  cargs.name = name;
  cargs.static_memfuncp = static_memfuncp;
  cargs.err = err;
  cargs.result_ptr = result;
  return catch_exceptions (uiout, do_captured_value_struct_elt, &cargs,
			   NULL, RETURN_MASK_ALL);
}

static int
do_captured_value_struct_elt (struct ui_out *uiout, void *data)
{
  struct captured_value_struct_elt_args *cargs = data;
  *cargs->result_ptr = value_struct_elt (cargs->argp, cargs->args, cargs->name,
			     cargs->static_memfuncp, cargs->err);
  return GDB_RC_OK;
}

