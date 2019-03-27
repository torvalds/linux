/* Ada language support definitions for GDB, the GNU debugger.
   Copyright 1992, 1997 Free Software Foundation, Inc.

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
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#if !defined (ADA_LANG_H)
#define ADA_LANG_H 1

struct partial_symbol;

#include "value.h"
#include "gdbtypes.h"

struct block;

/* A macro to reorder the bytes of an address depending on the
   endiannes of the target.  */
#define EXTRACT_ADDRESS(x) ((void *) extract_unsigned_integer (&(x), sizeof (x)))
/* A macro to reorder the bytes of an int depending on the endiannes
   of the target */
#define EXTRACT_INT(x) ((int) extract_signed_integer (&(x), sizeof (x)))

/* Chain of cleanups for arguments of OP_UNRESOLVED_VALUE names.  Created in
   yyparse and freed in ada_resolve. */
extern struct cleanup *unresolved_names;

/* Corresponding mangled/demangled names and opcodes for Ada user-definable 
   operators. */
struct ada_opname_map
{
  const char *mangled;
  const char *demangled;
  enum exp_opcode op;
};

/* Table of Ada operators in mangled and demangled forms. */
/* Defined in ada-lang.c */
extern const struct ada_opname_map ada_opname_table[];

/* The maximum number of tasks known to the Ada runtime */
extern const int MAX_NUMBER_OF_KNOWN_TASKS;

/* Identifiers for Ada attributes that need special processing.  Be sure 
   to update the table attribute_names in ada-lang.c whenever you change this.
   */

enum ada_attribute
{
  /* Invalid attribute for error checking. */
  ATR_INVALID,

  ATR_FIRST,
  ATR_LAST,
  ATR_LENGTH,
  ATR_IMAGE,
  ATR_IMG,
  ATR_MAX,
  ATR_MIN,
  ATR_MODULUS,
  ATR_POS,
  ATR_SIZE,
  ATR_TAG,
  ATR_VAL,

  /* Dummy last attribute. */
  ATR_END
};

enum task_states
{
  Unactivated,
  Runnable,
  Terminated,
  Activator_Sleep,
  Acceptor_Sleep,
  Entry_Caller_Sleep,
  Async_Select_Sleep,
  Delay_Sleep,
  Master_Completion_Sleep,
  Master_Phase_2_Sleep
};

extern char *ada_task_states[];

typedef struct
{
  char *P_ARRAY;
  int *P_BOUNDS;
}
fat_string;

typedef struct entry_call
{
  void *self;
}
 *entry_call_link;

struct task_fields
{
  int entry_num;
#if (defined (VXWORKS_TARGET) || !defined (i386)) \
    && !(defined (VXWORKS_TARGET) && defined (M68K_TARGET))
  int pad1;
#endif
  char state;
#if (defined (VXWORKS_TARGET) && defined (M68K_TARGET))
  char pad_8bits;
#endif
  void *parent;
  int priority;
  int current_priority;
  fat_string image;
  entry_call_link call;
#if (defined (sun) && defined (__SVR4)) && !defined (VXWORKS_TARGET)
  int pad2;
  unsigned thread;
  unsigned lwp;
#else
  void *thread;
  void *lwp;
#endif
}
#if (defined (VXWORKS_TARGET) && defined (M68K_TARGET))
__attribute__ ((packed))
#endif
  ;

struct task_entry
{
  void *task_id;
  int task_num;
  int known_tasks_index;
  struct task_entry *next_task;
  void *thread;
  void *lwp;
  int stack_per;
};

extern struct type *builtin_type_ada_int;
extern struct type *builtin_type_ada_short;
extern struct type *builtin_type_ada_long;
extern struct type *builtin_type_ada_long_long;
extern struct type *builtin_type_ada_char;
extern struct type *builtin_type_ada_float;
extern struct type *builtin_type_ada_double;
extern struct type *builtin_type_ada_long_double;
extern struct type *builtin_type_ada_natural;
extern struct type *builtin_type_ada_positive;
extern struct type *builtin_type_ada_system_address;

/* Assuming V points to an array of S objects,  make sure that it contains at 
   least M objects, updating V and S as necessary. */

#define GROW_VECT(v, s, m) 						\
   if ((s) < (m)) grow_vect ((void**) &(v), &(s), (m), sizeof(*(v)));

extern void grow_vect (void **, size_t *, size_t, int);

extern int ada_parse (void);	/* Defined in ada-exp.y */

extern void ada_error (char *);	/* Defined in ada-exp.y */

			/* Defined in ada-typeprint.c */
extern void ada_print_type (struct type *, char *, struct ui_file *, int,
			    int);

extern int ada_val_print (struct type *, char *, int, CORE_ADDR,
			  struct ui_file *, int, int, int,
			  enum val_prettyprint);

extern int ada_value_print (struct value *, struct ui_file *, int,
			    enum val_prettyprint);

				/* Defined in ada-lang.c */

extern struct value *value_from_contents_and_address (struct type *, char *,
						      CORE_ADDR);

extern void ada_emit_char (int, struct ui_file *, int, int);

extern void ada_printchar (int, struct ui_file *);

extern void ada_printstr (struct ui_file *, char *, unsigned int, int, int);

extern void ada_convert_actuals (struct value *, int, struct value **,
				 CORE_ADDR *);

extern struct value *ada_value_subscript (struct value *, int,
					  struct value **);

extern struct type *ada_array_element_type (struct type *, int);

extern int ada_array_arity (struct type *);

struct type *ada_type_of_array (struct value *, int);

extern struct value *ada_coerce_to_simple_array (struct value *);

extern struct value *ada_coerce_to_simple_array_ptr (struct value *);

extern int ada_is_simple_array (struct type *);

extern int ada_is_array_descriptor (struct type *);

extern int ada_is_bogus_array_descriptor (struct type *);

extern struct type *ada_index_type (struct type *, int);

extern struct value *ada_array_bound (struct value *, int, int);

extern int ada_lookup_symbol_list (const char *, struct block *,
				   domain_enum, struct symbol ***,
				   struct block ***);

extern char *ada_fold_name (const char *);

extern struct symbol *ada_lookup_symbol (const char *, struct block *,
					 domain_enum);

extern struct minimal_symbol *ada_lookup_minimal_symbol (const char *);

extern void ada_resolve (struct expression **, struct type *);

extern int ada_resolve_function (struct symbol **, struct block **, int,
				 struct value **, int, const char *,
				 struct type *);

extern void ada_fill_in_ada_prototype (struct symbol *);

extern int user_select_syms (struct symbol **, struct block **, int, int);

extern int get_selections (int *, int, int, int, char *);

extern char *ada_start_decode_line_1 (char *);

extern struct symtabs_and_lines ada_finish_decode_line_1 (char **,
							  struct symtab *,
							  int, char ***);

extern int ada_scan_number (const char *, int, LONGEST *, int *);

extern struct type *ada_parent_type (struct type *);

extern int ada_is_ignored_field (struct type *, int);

extern int ada_is_packed_array_type (struct type *);

extern struct value *ada_value_primitive_packed_val (struct value *, char *,
						     long, int, int,
						     struct type *);

extern struct type *ada_coerce_to_simple_array_type (struct type *);

extern int ada_is_character_type (struct type *);

extern int ada_is_string_type (struct type *);

extern int ada_is_tagged_type (struct type *);

extern struct type *ada_tag_type (struct value *);

extern struct value *ada_value_tag (struct value *);

extern int ada_is_parent_field (struct type *, int);

extern int ada_is_wrapper_field (struct type *, int);

extern int ada_is_variant_part (struct type *, int);

extern struct type *ada_variant_discrim_type (struct type *, struct type *);

extern int ada_is_others_clause (struct type *, int);

extern int ada_in_variant (LONGEST, struct type *, int);

extern char *ada_variant_discrim_name (struct type *);

extern struct type *ada_lookup_struct_elt_type (struct type *, char *, int,
						int *);

extern struct value *ada_value_struct_elt (struct value *, char *, char *);

extern struct value *ada_search_struct_field (char *, struct value *, int,
					      struct type *);

extern int ada_is_aligner_type (struct type *);

extern struct type *ada_aligned_type (struct type *);

extern char *ada_aligned_value_addr (struct type *, char *);

extern const char *ada_attribute_name (int);

extern int ada_is_fixed_point_type (struct type *);

extern DOUBLEST ada_delta (struct type *);

extern DOUBLEST ada_fixed_to_float (struct type *, LONGEST);

extern LONGEST ada_float_to_fixed (struct type *, DOUBLEST);

extern int ada_is_vax_floating_type (struct type *);

extern int ada_vax_float_type_suffix (struct type *);

extern struct value *ada_vax_float_print_function (struct type *);

extern struct type *ada_system_address_type (void);

extern int ada_which_variant_applies (struct type *, struct type *, char *);

extern struct value *ada_to_fixed_value (struct type *, char *, CORE_ADDR,
					 struct value *);

extern struct type *ada_to_fixed_type (struct type *, char *, CORE_ADDR,
				       struct value *);

extern int ada_name_prefix_len (const char *);

extern char *ada_type_name (struct type *);

extern struct type *ada_find_parallel_type (struct type *,
					    const char *suffix);

extern LONGEST get_int_var_value (char *, char *, int *);

extern struct type *ada_find_any_type (const char *name);

extern int ada_prefer_type (struct type *, struct type *);

extern struct type *ada_get_base_type (struct type *);

extern struct type *ada_completed_type (struct type *);

extern char *ada_mangle (const char *);

extern const char *ada_enum_name (const char *);

extern int ada_is_modular_type (struct type *);

extern LONGEST ada_modulus (struct type *);

extern struct value *ada_value_ind (struct value *);

extern void ada_print_scalar (struct type *, LONGEST, struct ui_file *);

extern int ada_is_range_type_name (const char *);

extern const char *ada_renaming_type (struct type *);

extern int ada_is_object_renaming (struct symbol *);

extern const char *ada_simple_renamed_entity (struct symbol *);

extern char *ada_breakpoint_rewrite (char *, int *);

/* Tasking-related: ada-tasks.c */

extern int valid_task_id (int);

extern int get_current_task (void);

extern void init_task_list (void);

extern void *get_self_id (void);

extern int get_current_task (void);

extern int get_entry_number (void *);

extern void ada_report_exception_break (struct breakpoint *);

extern int ada_maybe_exception_partial_symbol (struct partial_symbol *sym);

extern int ada_is_exception_sym (struct symbol *sym);


#endif
