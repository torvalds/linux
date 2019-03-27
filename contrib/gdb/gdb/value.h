/* Definitions for values of C expressions, for GDB.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
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

#if !defined (VALUE_H)
#define VALUE_H 1

#include "doublest.h"
#include "frame.h"		/* For struct frame_id.  */

struct block;
struct expression;
struct regcache;
struct symbol;
struct type;
struct ui_file;

/* The structure which defines the type of a value.  It should never
   be possible for a program lval value to survive over a call to the
   inferior (i.e. to be put into the history list or an internal
   variable).  */

struct value
{
  /* Type of value; either not an lval, or one of the various
     different possible kinds of lval.  */
  enum lval_type lval;

  /* Is it modifiable?  Only relevant if lval != not_lval.  */
  int modifiable;

  /* Location of value (if lval).  */
  union
  {
    /* If lval == lval_memory, this is the address in the inferior.
       If lval == lval_register, this is the byte offset into the
       registers structure.  */
    CORE_ADDR address;

    /* Pointer to internal variable.  */
    struct internalvar *internalvar;

    /* Number of register.  Only used with lval_reg_frame_relative.  */
    int regnum;
  } location;

  /* Describes offset of a value within lval of a structure in bytes.
     If lval == lval_memory, this is an offset to the address.
     If lval == lval_register, this is a further offset from
     location.address within the registers structure.  
     Note also the member embedded_offset below.  */
  int offset;

  /* Only used for bitfields; number of bits contained in them.  */
  int bitsize;

  /* Only used for bitfields; position of start of field.
     For BITS_BIG_ENDIAN=0 targets, it is the position of the LSB.
     For BITS_BIG_ENDIAN=1 targets, it is the position of the MSB. */
    int bitpos;

  /* Frame value is relative to.  In practice, this ID is only used if
     the value is stored in several registers in other than the
     current frame, and these registers have not all been saved at the
     same place in memory.  This will be described in the lval enum
     above as "lval_reg_frame_relative".  */
  struct frame_id frame_id;

  /* Type of the value.  */
  struct type *type;

  /* If a value represents a C++ object, then the `type' field gives
     the object's compile-time type.  If the object actually belongs
     to some class derived from `type', perhaps with other base
     classes and additional members, then `type' is just a subobject
     of the real thing, and the full object is probably larger than
     `type' would suggest.

     If `type' is a dynamic class (i.e. one with a vtable), then GDB
     can actually determine the object's run-time type by looking at
     the run-time type information in the vtable.  When this
     information is available, we may elect to read in the entire
     object, for several reasons:

     - When printing the value, the user would probably rather see the
       full object, not just the limited portion apparent from the
       compile-time type.

     - If `type' has virtual base classes, then even printing `type'
       alone may require reaching outside the `type' portion of the
       object to wherever the virtual base class has been stored.

     When we store the entire object, `enclosing_type' is the run-time
     type -- the complete object -- and `embedded_offset' is the
     offset of `type' within that larger type, in bytes.  The
     VALUE_CONTENTS macro takes `embedded_offset' into account, so
     most GDB code continues to see the `type' portion of the value,
     just as the inferior would.

     If `type' is a pointer to an object, then `enclosing_type' is a
     pointer to the object's run-time type, and `pointed_to_offset' is
     the offset in bytes from the full object to the pointed-to object
     -- that is, the value `embedded_offset' would have if we
     followed the pointer and fetched the complete object.  (I don't
     really see the point.  Why not just determine the run-time type
     when you indirect, and avoid the special case?  The contents
     don't matter until you indirect anyway.)

     If we're not doing anything fancy, `enclosing_type' is equal to
     `type', and `embedded_offset' is zero, so everything works
     normally.  */
    struct type *enclosing_type;
    int embedded_offset;
    int pointed_to_offset;

    /* Values are stored in a chain, so that they can be deleted
       easily over calls to the inferior.  Values assigned to internal
       variables or put into the value history are taken off this
       list.  */
    struct value *next;

    /* Register number if the value is from a register.  */
    short regno;

    /* If zero, contents of this value are in the contents field.  If
       nonzero, contents are in inferior memory at address in the
       location.address field plus the offset field (and the lval
       field should be lval_memory).

       WARNING: This field is used by the code which handles
       watchpoints (see breakpoint.c) to decide whether a particular
       value can be watched by hardware watchpoints.  If the lazy flag
       is set for some member of a value chain, it is assumed that
       this member of the chain doesn't need to be watched as part of
       watching the value itself.  This is how GDB avoids watching the
       entire struct or array when the user wants to watch a single
       struct member or array element.  If you ever change the way
       lazy flag is set and reset, be sure to consider this use as
       well!  */
    char lazy;

    /* If nonzero, this is the value of a variable which does not
       actually exist in the program.  */
    char optimized_out;

    /* The BFD section associated with this value.  */
    asection *bfd_section;

    /* If value is a variable, is it initialized or not.  */
    int initialized;

    /* Actual contents of the value.  For use of this value; setting
       it uses the stuff above.  Not valid if lazy is nonzero.
       Target byte-order.  We force it to be aligned properly for any
       possible value.  Note that a value therefore extends beyond
       what is declared here.  */
    union
    {
      long contents[1];
      DOUBLEST force_doublest_align;
      LONGEST force_longest_align;
      CORE_ADDR force_core_addr_align;
      void *force_pointer_align;
    } aligner;
    /* Do not add any new members here -- contents above will trash them.  */
};

#define VALUE_TYPE(val) (val)->type
#define VALUE_ENCLOSING_TYPE(val) (val)->enclosing_type
#define VALUE_LAZY(val) (val)->lazy

/* VALUE_CONTENTS and VALUE_CONTENTS_RAW both return the address of
   the gdb buffer used to hold a copy of the contents of the lval.
   VALUE_CONTENTS is used when the contents of the buffer are needed
   -- it uses value_fetch_lazy() to load the buffer from the process
   being debugged if it hasn't already been loaded.
   VALUE_CONTENTS_RAW is used when data is being stored into the
   buffer, or when it is certain that the contents of the buffer are
   valid.

   Note: The contents pointer is adjusted by the offset required to
   get to the real subobject, if the value happens to represent
   something embedded in a larger run-time object.  */

#define VALUE_CONTENTS_RAW(val) \
 ((char *) (val)->aligner.contents + (val)->embedded_offset)
#define VALUE_CONTENTS(val) \
 ((void)(VALUE_LAZY(val) && value_fetch_lazy(val)), VALUE_CONTENTS_RAW(val))

/* The ALL variants of the above two macros do not adjust the returned
   pointer by the embedded_offset value.  */

#define VALUE_CONTENTS_ALL_RAW(val) ((char *) (val)->aligner.contents)
#define VALUE_CONTENTS_ALL(val) \
  ((void) (VALUE_LAZY(val) && value_fetch_lazy(val)), \
   VALUE_CONTENTS_ALL_RAW(val))

extern int value_fetch_lazy (struct value *val);

#define VALUE_LVAL(val) (val)->lval
#define VALUE_ADDRESS(val) (val)->location.address
#define VALUE_INTERNALVAR(val) (val)->location.internalvar
#define VALUE_FRAME_REGNUM(val) ((val)->location.regnum)
#define VALUE_FRAME_ID(val) ((val)->frame_id)
#define VALUE_OFFSET(val) (val)->offset
#define VALUE_BITSIZE(val) (val)->bitsize
#define VALUE_BITPOS(val) (val)->bitpos
#define VALUE_NEXT(val) (val)->next
#define VALUE_REGNO(val) (val)->regno
#define VALUE_OPTIMIZED_OUT(val) ((val)->optimized_out)
#define VALUE_EMBEDDED_OFFSET(val) ((val)->embedded_offset)
#define VALUE_POINTED_TO_OFFSET(val) ((val)->pointed_to_offset)
#define VALUE_BFD_SECTION(val) ((val)->bfd_section)

/* Convert a REF to the object referenced.  */

#define COERCE_REF(arg) \
  do {									\
    struct type *value_type_arg_tmp = check_typedef (VALUE_TYPE (arg));	\
    if (TYPE_CODE (value_type_arg_tmp) == TYPE_CODE_REF)		\
      arg = value_at_lazy (TYPE_TARGET_TYPE (value_type_arg_tmp),	\
                           unpack_pointer (VALUE_TYPE (arg),		\
                                           VALUE_CONTENTS (arg)),	\
			                   VALUE_BFD_SECTION (arg));	\
  } while (0)

/* If ARG is an array, convert it to a pointer.
   If ARG is an enum, convert it to an integer.
   If ARG is a function, convert it to a function pointer.

   References are dereferenced.  */

#define COERCE_ARRAY(arg) \
  do {									\
    COERCE_REF(arg);							\
    if (current_language->c_style_arrays				\
        && TYPE_CODE (VALUE_TYPE (arg)) == TYPE_CODE_ARRAY)		\
      arg = value_coerce_array (arg);					\
    if (TYPE_CODE (VALUE_TYPE (arg)) == TYPE_CODE_FUNC)			\
      arg = value_coerce_function (arg);				\
  } while (0)

#define COERCE_NUMBER(arg) \
  do { COERCE_ARRAY(arg); COERCE_ENUM(arg); } while (0)

/* NOTE: cagney/2002-12-17: This macro was handling a chill language
   problem but that language has gone away.  */
#define COERCE_VARYING_ARRAY(arg, real_arg_type)

/* If ARG is an enum, convert it to an integer.  */

#define COERCE_ENUM(arg) \
  do {									\
    if (TYPE_CODE (check_typedef (VALUE_TYPE (arg))) == TYPE_CODE_ENUM)	\
      arg = value_cast (builtin_type_unsigned_int, arg);		\
  } while (0)

/* Internal variables (variables for convenience of use of debugger)
   are recorded as a chain of these structures.  */

struct internalvar
{
  struct internalvar *next;
  char *name;
  struct value *value;
};

/* Pointer to member function.  Depends on compiler implementation.  */

#define METHOD_PTR_IS_VIRTUAL(ADDR)  ((ADDR) & 0x80000000)
#define METHOD_PTR_FROM_VOFFSET(OFFSET) (0x80000000 + (OFFSET))
#define METHOD_PTR_TO_VOFFSET(ADDR) (~0x80000000 & (ADDR))


#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"

struct frame_info;
struct fn_field;

extern void print_address_demangle (CORE_ADDR, struct ui_file *, int);

extern LONGEST value_as_long (struct value *val);
extern DOUBLEST value_as_double (struct value *val);
extern CORE_ADDR value_as_address (struct value *val);

extern LONGEST unpack_long (struct type *type, const char *valaddr);
extern DOUBLEST unpack_double (struct type *type, const char *valaddr,
			       int *invp);
extern CORE_ADDR unpack_pointer (struct type *type, const char *valaddr);
extern LONGEST unpack_field_as_long (struct type *type, const char *valaddr,
				     int fieldno);

extern struct value *value_from_longest (struct type *type, LONGEST num);
extern struct value *value_from_pointer (struct type *type, CORE_ADDR addr);
extern struct value *value_from_double (struct type *type, DOUBLEST num);
extern struct value *value_from_string (char *string);

extern struct value *value_at (struct type *type, CORE_ADDR addr,
			       asection * sect);
extern struct value *value_at_lazy (struct type *type, CORE_ADDR addr,
				    asection * sect);

extern struct value *value_from_register (struct type *type, int regnum,
					  struct frame_info *frame);

extern struct value *value_of_variable (struct symbol *var, struct block *b);

extern struct value *value_of_register (int regnum,
					struct frame_info *frame);

extern int symbol_read_needs_frame (struct symbol *);

extern struct value *read_var_value (struct symbol *var,
				     struct frame_info *frame);

extern struct value *locate_var_value (struct symbol *var,
				       struct frame_info *frame);

extern struct value *allocate_value (struct type *type);

extern struct value *allocate_repeat_value (struct type *type, int count);

extern struct value *value_change_enclosing_type (struct value *val,
						  struct type *new_type);

extern struct value *value_mark (void);

extern void value_free_to_mark (struct value *mark);

extern struct value *value_string (char *ptr, int len);
extern struct value *value_bitstring (char *ptr, int len);

extern struct value *value_array (int lowbound, int highbound,
				  struct value ** elemvec);

extern struct value *value_concat (struct value *arg1, struct value *arg2);

extern struct value *value_binop (struct value *arg1, struct value *arg2,
				  enum exp_opcode op);

extern struct value *value_add (struct value *arg1, struct value *arg2);

extern struct value *value_sub (struct value *arg1, struct value *arg2);

extern struct value *value_coerce_array (struct value *arg1);

extern struct value *value_coerce_function (struct value *arg1);

extern struct value *value_ind (struct value *arg1);

extern struct value *value_addr (struct value *arg1);

extern struct value *value_assign (struct value *toval, struct value *fromval);

extern struct value *value_neg (struct value *arg1);

extern struct value *value_complement (struct value *arg1);

extern struct value *value_struct_elt (struct value **argp,
				       struct value **args,
				       char *name, int *static_memfuncp,
				       char *err);

extern struct value *value_aggregate_elt (struct type *curtype,
					  char *name,
					  enum noside noside);

extern struct value *value_static_field (struct type *type, int fieldno);

extern struct fn_field *value_find_oload_method_list (struct value **, char *,
						      int, int *,
						      struct type **, int *);

extern int find_overload_match (struct type **arg_types, int nargs,
				char *name, int method, int lax,
				struct value **objp, struct symbol *fsym,
				struct value **valp, struct symbol **symp,
				int *staticp);

extern struct value *value_field (struct value *arg1, int fieldno);

extern struct value *value_primitive_field (struct value *arg1, int offset,
					    int fieldno,
					    struct type *arg_type);


extern struct type *value_rtti_target_type (struct value *, int *, int *,
					    int *);

extern struct value *value_full_object (struct value *, struct type *, int,
					int, int);

extern struct value *value_cast (struct type *type, struct value *arg2);

extern struct value *value_zero (struct type *type, enum lval_type lv);

extern struct value *value_repeat (struct value *arg1, int count);

extern struct value *value_subscript (struct value *array, struct value *idx);

extern struct value *register_value_being_returned (struct type *valtype,
						    struct regcache *retbuf);

extern struct value *value_in (struct value *element, struct value *set);

extern int value_bit_index (struct type *type, char *addr, int index);

extern int using_struct_return (struct type *value_type, int gcc_p);

extern struct value *evaluate_expression (struct expression *exp);

extern struct value *evaluate_type (struct expression *exp);

extern struct value *evaluate_subexp_with_coercion (struct expression *,
						    int *, enum noside);

extern struct value *parse_and_eval (char *exp);

extern struct value *parse_to_comma_and_eval (char **expp);

extern struct type *parse_and_eval_type (char *p, int length);

extern CORE_ADDR parse_and_eval_address (char *exp);

extern CORE_ADDR parse_and_eval_address_1 (char **expptr);

extern LONGEST parse_and_eval_long (char *exp);

extern struct value *access_value_history (int num);

extern struct value *value_of_internalvar (struct internalvar *var);

extern void set_internalvar (struct internalvar *var, struct value *val);

extern void set_internalvar_component (struct internalvar *var,
				       int offset,
				       int bitpos, int bitsize,
				       struct value *newvalue);

extern struct internalvar *lookup_internalvar (char *name);

extern int value_equal (struct value *arg1, struct value *arg2);

extern int value_less (struct value *arg1, struct value *arg2);

extern int value_logical_not (struct value *arg1);

/* C++ */

extern struct value *value_of_this (int complain);

extern struct value *value_x_binop (struct value *arg1, struct value *arg2,
				    enum exp_opcode op,
				    enum exp_opcode otherop,
				    enum noside noside);

extern struct value *value_x_unop (struct value *arg1, enum exp_opcode op,
				   enum noside noside);

extern struct value *value_fn_field (struct value ** arg1p, struct fn_field *f,
				     int j, struct type *type, int offset);

extern int binop_user_defined_p (enum exp_opcode op, struct value *arg1,
				 struct value *arg2);

extern int unop_user_defined_p (enum exp_opcode op, struct value *arg1);

extern int destructor_name_p (const char *name, const struct type *type);

#define value_free(val) xfree (val)

extern void free_all_values (void);

extern void release_value (struct value *val);

extern int record_latest_value (struct value *val);

extern void modify_field (char *addr, LONGEST fieldval, int bitpos,
			  int bitsize);

extern void type_print (struct type * type, char *varstring,
			struct ui_file * stream, int show);

extern char *baseclass_addr (struct type *type, int index, char *valaddr,
			     struct value **valuep, int *errp);

extern void print_longest (struct ui_file * stream, int format,
			   int use_local, LONGEST val);

extern void print_floating (char *valaddr, struct type * type,
			    struct ui_file * stream);

extern int value_print (struct value *val, struct ui_file *stream, int format,
			enum val_prettyprint pretty);

extern void value_print_array_elements (struct value *val,
					struct ui_file *stream, int format,
					enum val_prettyprint pretty);

extern struct value *value_release_to_mark (struct value *mark);

extern int val_print (struct type * type, char *valaddr,
		      int embedded_offset, CORE_ADDR address,
		      struct ui_file * stream, int format,
		      int deref_ref, int recurse,
		      enum val_prettyprint pretty);

extern int common_val_print (struct value *val,
			     struct ui_file *stream, int format,
			     int deref_ref, int recurse,
			     enum val_prettyprint pretty);

extern int val_print_string (CORE_ADDR addr, int len, int width, struct ui_file *stream);

extern void print_variable_value (struct symbol * var,
				  struct frame_info * frame,
				  struct ui_file *stream);

extern int check_field (struct value *, const char *);

extern void typedef_print (struct type * type, struct symbol * news,
			     struct ui_file * stream);

extern char *internalvar_name (struct internalvar *var);

extern void clear_value_history (void);

extern void clear_internalvars (void);

/* From values.c */

extern struct value *value_copy (struct value *);

/* From valops.c */

extern struct value *varying_to_slice (struct value *);

extern struct value *value_slice (struct value *, int, int);

extern struct value *value_literal_complex (struct value *, struct value *,
					    struct type *);

extern void find_rt_vbase_offset (struct type *, struct type *, char *, int,
				  int *, int *);

extern struct value *find_function_in_inferior (const char *);

extern struct value *value_allocate_space_in_inferior (int);

extern CORE_ADDR legacy_push_arguments (int nargs, struct value ** args,
					CORE_ADDR sp, int struct_return,
					CORE_ADDR struct_addr);

extern struct value *value_of_local (const char *name, int complain);

#endif /* !defined (VALUE_H) */
