/* Ada language support routines for GDB, the GNU debugger.  Copyright
   1992, 1993, 1994, 1997, 1998, 1999, 2000, 2003, 2004
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
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdio.h>
#include "gdb_string.h"
#include <ctype.h>
#include <stdarg.h>
#include "demangle.h"
#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcmd.h"
#include "expression.h"
#include "parser-defs.h"
#include "language.h"
#include "c-lang.h"
#include "inferior.h"
#include "symfile.h"
#include "objfiles.h"
#include "breakpoint.h"
#include "gdbcore.h"
#include "ada-lang.h"
#include "ui-out.h"
#include "block.h"
#include "infcall.h"
#include "dictionary.h"

struct cleanup *unresolved_names;

void extract_string (CORE_ADDR addr, char *buf);

static struct type *ada_create_fundamental_type (struct objfile *, int);

static void modify_general_field (char *, LONGEST, int, int);

static struct type *desc_base_type (struct type *);

static struct type *desc_bounds_type (struct type *);

static struct value *desc_bounds (struct value *);

static int fat_pntr_bounds_bitpos (struct type *);

static int fat_pntr_bounds_bitsize (struct type *);

static struct type *desc_data_type (struct type *);

static struct value *desc_data (struct value *);

static int fat_pntr_data_bitpos (struct type *);

static int fat_pntr_data_bitsize (struct type *);

static struct value *desc_one_bound (struct value *, int, int);

static int desc_bound_bitpos (struct type *, int, int);

static int desc_bound_bitsize (struct type *, int, int);

static struct type *desc_index_type (struct type *, int);

static int desc_arity (struct type *);

static int ada_type_match (struct type *, struct type *, int);

static int ada_args_match (struct symbol *, struct value **, int);

static struct value *place_on_stack (struct value *, CORE_ADDR *);

static struct value *convert_actual (struct value *, struct type *,
				     CORE_ADDR *);

static struct value *make_array_descriptor (struct type *, struct value *,
					    CORE_ADDR *);

static void ada_add_block_symbols (struct block *, const char *,
				   domain_enum, struct objfile *, int);

static void fill_in_ada_prototype (struct symbol *);

static int is_nonfunction (struct symbol **, int);

static void add_defn_to_vec (struct symbol *, struct block *);

static struct partial_symbol *ada_lookup_partial_symbol (struct partial_symtab
							 *, const char *, int,
							 domain_enum, int);

static struct symtab *symtab_for_sym (struct symbol *);

static struct value *ada_resolve_subexp (struct expression **, int *, int,
					 struct type *);

static void replace_operator_with_call (struct expression **, int, int, int,
					struct symbol *, struct block *);

static int possible_user_operator_p (enum exp_opcode, struct value **);

static const char *ada_op_name (enum exp_opcode);

static int numeric_type_p (struct type *);

static int integer_type_p (struct type *);

static int scalar_type_p (struct type *);

static int discrete_type_p (struct type *);

static char *extended_canonical_line_spec (struct symtab_and_line,
					   const char *);

static struct value *evaluate_subexp (struct type *, struct expression *,
				      int *, enum noside);

static struct value *evaluate_subexp_type (struct expression *, int *);

static struct type *ada_create_fundamental_type (struct objfile *, int);

static int is_dynamic_field (struct type *, int);

static struct type *to_fixed_variant_branch_type (struct type *, char *,
						  CORE_ADDR, struct value *);

static struct type *to_fixed_range_type (char *, struct value *,
					 struct objfile *);

static struct type *to_static_fixed_type (struct type *);

static struct value *unwrap_value (struct value *);

static struct type *packed_array_type (struct type *, long *);

static struct type *decode_packed_array_type (struct type *);

static struct value *decode_packed_array (struct value *);

static struct value *value_subscript_packed (struct value *, int,
					     struct value **);

static struct value *coerce_unspec_val_to_type (struct value *, long,
						struct type *);

static struct value *get_var_value (char *, char *);

static int lesseq_defined_than (struct symbol *, struct symbol *);

static int equiv_types (struct type *, struct type *);

static int is_name_suffix (const char *);

static int wild_match (const char *, int, const char *);

static struct symtabs_and_lines find_sal_from_funcs_and_line (const char *,
							      int,
							      struct symbol
							      **, int);

static int find_line_in_linetable (struct linetable *, int, struct symbol **,
				   int, int *);

static int find_next_line_in_linetable (struct linetable *, int, int, int);

static struct symtabs_and_lines all_sals_for_line (const char *, int,
						   char ***);

static void read_all_symtabs (const char *);

static int is_plausible_func_for_line (struct symbol *, int);

static struct value *ada_coerce_ref (struct value *);

static struct value *value_pos_atr (struct value *);

static struct value *value_val_atr (struct type *, struct value *);

static struct symbol *standard_lookup (const char *, domain_enum);

extern void markTimeStart (int index);
extern void markTimeStop (int index);



/* Maximum-sized dynamic type. */
static unsigned int varsize_limit;

static const char *ada_completer_word_break_characters =
  " \t\n!@#$%^&*()+=|~`}{[]\";:?/,-";

/* The name of the symbol to use to get the name of the main subprogram */
#define ADA_MAIN_PROGRAM_SYMBOL_NAME "__gnat_ada_main_program_name"

				/* Utilities */

/* extract_string
 *
 * read the string located at ADDR from the inferior and store the
 * result into BUF
 */
void
extract_string (CORE_ADDR addr, char *buf)
{
  int char_index = 0;

  /* Loop, reading one byte at a time, until we reach the '\000' 
     end-of-string marker */
  do
    {
      target_read_memory (addr + char_index * sizeof (char),
			  buf + char_index * sizeof (char), sizeof (char));
      char_index++;
    }
  while (buf[char_index - 1] != '\000');
}

/* Assuming *OLD_VECT points to an array of *SIZE objects of size
   ELEMENT_SIZE, grow it to contain at least MIN_SIZE objects,
   updating *OLD_VECT and *SIZE as necessary. */

void
grow_vect (void **old_vect, size_t * size, size_t min_size, int element_size)
{
  if (*size < min_size)
    {
      *size *= 2;
      if (*size < min_size)
	*size = min_size;
      *old_vect = xrealloc (*old_vect, *size * element_size);
    }
}

/* True (non-zero) iff TARGET matches FIELD_NAME up to any trailing
   suffix of FIELD_NAME beginning "___" */

static int
field_name_match (const char *field_name, const char *target)
{
  int len = strlen (target);
  return
    DEPRECATED_STREQN (field_name, target, len)
    && (field_name[len] == '\0'
	|| (DEPRECATED_STREQN (field_name + len, "___", 3)
	    && !DEPRECATED_STREQ (field_name + strlen (field_name) - 6, "___XVN")));
}


/* The length of the prefix of NAME prior to any "___" suffix. */

int
ada_name_prefix_len (const char *name)
{
  if (name == NULL)
    return 0;
  else
    {
      const char *p = strstr (name, "___");
      if (p == NULL)
	return strlen (name);
      else
	return p - name;
    }
}

/* SUFFIX is a suffix of STR. False if STR is null. */
static int
is_suffix (const char *str, const char *suffix)
{
  int len1, len2;
  if (str == NULL)
    return 0;
  len1 = strlen (str);
  len2 = strlen (suffix);
  return (len1 >= len2 && DEPRECATED_STREQ (str + len1 - len2, suffix));
}

/* Create a value of type TYPE whose contents come from VALADDR, if it
 * is non-null, and whose memory address (in the inferior) is
 * ADDRESS. */
struct value *
value_from_contents_and_address (struct type *type, char *valaddr,
				 CORE_ADDR address)
{
  struct value *v = allocate_value (type);
  if (valaddr == NULL)
    VALUE_LAZY (v) = 1;
  else
    memcpy (VALUE_CONTENTS_RAW (v), valaddr, TYPE_LENGTH (type));
  VALUE_ADDRESS (v) = address;
  if (address != 0)
    VALUE_LVAL (v) = lval_memory;
  return v;
}

/* The contents of value VAL, beginning at offset OFFSET, treated as a
   value of type TYPE.  The result is an lval in memory if VAL is. */

static struct value *
coerce_unspec_val_to_type (struct value *val, long offset, struct type *type)
{
  CHECK_TYPEDEF (type);
  if (VALUE_LVAL (val) == lval_memory)
    return value_at_lazy (type,
			  VALUE_ADDRESS (val) + VALUE_OFFSET (val) + offset,
			  NULL);
  else
    {
      struct value *result = allocate_value (type);
      VALUE_LVAL (result) = not_lval;
      if (VALUE_ADDRESS (val) == 0)
	memcpy (VALUE_CONTENTS_RAW (result), VALUE_CONTENTS (val) + offset,
		TYPE_LENGTH (type) > TYPE_LENGTH (VALUE_TYPE (val))
		? TYPE_LENGTH (VALUE_TYPE (val)) : TYPE_LENGTH (type));
      else
	{
	  VALUE_ADDRESS (result) =
	    VALUE_ADDRESS (val) + VALUE_OFFSET (val) + offset;
	  VALUE_LAZY (result) = 1;
	}
      return result;
    }
}

static char *
cond_offset_host (char *valaddr, long offset)
{
  if (valaddr == NULL)
    return NULL;
  else
    return valaddr + offset;
}

static CORE_ADDR
cond_offset_target (CORE_ADDR address, long offset)
{
  if (address == 0)
    return 0;
  else
    return address + offset;
}

/* Perform execute_command on the result of concatenating all
   arguments up to NULL. */
static void
do_command (const char *arg, ...)
{
  int len;
  char *cmd;
  const char *s;
  va_list ap;

  va_start (ap, arg);
  len = 0;
  s = arg;
  cmd = "";
  for (; s != NULL; s = va_arg (ap, const char *))
    {
      char *cmd1;
      len += strlen (s);
      cmd1 = alloca (len + 1);
      strcpy (cmd1, cmd);
      strcat (cmd1, s);
      cmd = cmd1;
    }
  va_end (ap);
  execute_command (cmd, 0);
}


				/* Language Selection */

/* If the main program is in Ada, return language_ada, otherwise return LANG
   (the main program is in Ada iif the adainit symbol is found).

   MAIN_PST is not used. */

enum language
ada_update_initial_language (enum language lang,
			     struct partial_symtab *main_pst)
{
  if (lookup_minimal_symbol ("adainit", (const char *) NULL,
			     (struct objfile *) NULL) != NULL)
    /*    return language_ada; */
    /* FIXME: language_ada should be defined in defs.h */
    return language_unknown;

  return lang;
}


				/* Symbols */

/* Table of Ada operators and their GNAT-mangled names.  Last entry is pair 
   of NULLs. */

const struct ada_opname_map ada_opname_table[] = {
  {"Oadd", "\"+\"", BINOP_ADD},
  {"Osubtract", "\"-\"", BINOP_SUB},
  {"Omultiply", "\"*\"", BINOP_MUL},
  {"Odivide", "\"/\"", BINOP_DIV},
  {"Omod", "\"mod\"", BINOP_MOD},
  {"Orem", "\"rem\"", BINOP_REM},
  {"Oexpon", "\"**\"", BINOP_EXP},
  {"Olt", "\"<\"", BINOP_LESS},
  {"Ole", "\"<=\"", BINOP_LEQ},
  {"Ogt", "\">\"", BINOP_GTR},
  {"Oge", "\">=\"", BINOP_GEQ},
  {"Oeq", "\"=\"", BINOP_EQUAL},
  {"One", "\"/=\"", BINOP_NOTEQUAL},
  {"Oand", "\"and\"", BINOP_BITWISE_AND},
  {"Oor", "\"or\"", BINOP_BITWISE_IOR},
  {"Oxor", "\"xor\"", BINOP_BITWISE_XOR},
  {"Oconcat", "\"&\"", BINOP_CONCAT},
  {"Oabs", "\"abs\"", UNOP_ABS},
  {"Onot", "\"not\"", UNOP_LOGICAL_NOT},
  {"Oadd", "\"+\"", UNOP_PLUS},
  {"Osubtract", "\"-\"", UNOP_NEG},
  {NULL, NULL}
};

/* True if STR should be suppressed in info listings. */
static int
is_suppressed_name (const char *str)
{
  if (DEPRECATED_STREQN (str, "_ada_", 5))
    str += 5;
  if (str[0] == '_' || str[0] == '\000')
    return 1;
  else
    {
      const char *p;
      const char *suffix = strstr (str, "___");
      if (suffix != NULL && suffix[3] != 'X')
	return 1;
      if (suffix == NULL)
	suffix = str + strlen (str);
      for (p = suffix - 1; p != str; p -= 1)
	if (isupper (*p))
	  {
	    int i;
	    if (p[0] == 'X' && p[-1] != '_')
	      goto OK;
	    if (*p != 'O')
	      return 1;
	    for (i = 0; ada_opname_table[i].mangled != NULL; i += 1)
	      if (DEPRECATED_STREQN (ada_opname_table[i].mangled, p,
			  strlen (ada_opname_table[i].mangled)))
		goto OK;
	    return 1;
	  OK:;
	  }
      return 0;
    }
}

/* The "mangled" form of DEMANGLED, according to GNAT conventions.
 * The result is valid until the next call to ada_mangle. */
char *
ada_mangle (const char *demangled)
{
  static char *mangling_buffer = NULL;
  static size_t mangling_buffer_size = 0;
  const char *p;
  int k;

  if (demangled == NULL)
    return NULL;

  GROW_VECT (mangling_buffer, mangling_buffer_size,
	     2 * strlen (demangled) + 10);

  k = 0;
  for (p = demangled; *p != '\0'; p += 1)
    {
      if (*p == '.')
	{
	  mangling_buffer[k] = mangling_buffer[k + 1] = '_';
	  k += 2;
	}
      else if (*p == '"')
	{
	  const struct ada_opname_map *mapping;

	  for (mapping = ada_opname_table;
	       mapping->mangled != NULL &&
	       !DEPRECATED_STREQN (mapping->demangled, p, strlen (mapping->demangled));
	       p += 1)
	    ;
	  if (mapping->mangled == NULL)
	    error ("invalid Ada operator name: %s", p);
	  strcpy (mangling_buffer + k, mapping->mangled);
	  k += strlen (mapping->mangled);
	  break;
	}
      else
	{
	  mangling_buffer[k] = *p;
	  k += 1;
	}
    }

  mangling_buffer[k] = '\0';
  return mangling_buffer;
}

/* Return NAME folded to lower case, or, if surrounded by single
 * quotes, unfolded, but with the quotes stripped away.  Result good
 * to next call. */
char *
ada_fold_name (const char *name)
{
  static char *fold_buffer = NULL;
  static size_t fold_buffer_size = 0;

  int len = strlen (name);
  GROW_VECT (fold_buffer, fold_buffer_size, len + 1);

  if (name[0] == '\'')
    {
      strncpy (fold_buffer, name + 1, len - 2);
      fold_buffer[len - 2] = '\000';
    }
  else
    {
      int i;
      for (i = 0; i <= len; i += 1)
	fold_buffer[i] = tolower (name[i]);
    }

  return fold_buffer;
}

/* Demangle: 
     1. Discard final __{DIGIT}+ or ${DIGIT}+
     2. Convert other instances of embedded "__" to `.'.
     3. Discard leading _ada_.
     4. Convert operator names to the appropriate quoted symbols.
     5. Remove everything after first ___ if it is followed by 
        'X'.
     6. Replace TK__ with __, and a trailing B or TKB with nothing.
     7. Put symbols that should be suppressed in <...> brackets.
     8. Remove trailing X[bn]* suffix (indicating names in package bodies).
   The resulting string is valid until the next call of ada_demangle.
  */

char *
ada_demangle (const char *mangled)
{
  int i, j;
  int len0;
  const char *p;
  char *demangled;
  int at_start_name;
  static char *demangling_buffer = NULL;
  static size_t demangling_buffer_size = 0;

  if (DEPRECATED_STREQN (mangled, "_ada_", 5))
    mangled += 5;

  if (mangled[0] == '_' || mangled[0] == '<')
    goto Suppress;

  p = strstr (mangled, "___");
  if (p == NULL)
    len0 = strlen (mangled);
  else
    {
      if (p[3] == 'X')
	len0 = p - mangled;
      else
	goto Suppress;
    }
  if (len0 > 3 && DEPRECATED_STREQ (mangled + len0 - 3, "TKB"))
    len0 -= 3;
  if (len0 > 1 && DEPRECATED_STREQ (mangled + len0 - 1, "B"))
    len0 -= 1;

  /* Make demangled big enough for possible expansion by operator name. */
  GROW_VECT (demangling_buffer, demangling_buffer_size, 2 * len0 + 1);
  demangled = demangling_buffer;

  if (isdigit (mangled[len0 - 1]))
    {
      for (i = len0 - 2; i >= 0 && isdigit (mangled[i]); i -= 1)
	;
      if (i > 1 && mangled[i] == '_' && mangled[i - 1] == '_')
	len0 = i - 1;
      else if (mangled[i] == '$')
	len0 = i;
    }

  for (i = 0, j = 0; i < len0 && !isalpha (mangled[i]); i += 1, j += 1)
    demangled[j] = mangled[i];

  at_start_name = 1;
  while (i < len0)
    {
      if (at_start_name && mangled[i] == 'O')
	{
	  int k;
	  for (k = 0; ada_opname_table[k].mangled != NULL; k += 1)
	    {
	      int op_len = strlen (ada_opname_table[k].mangled);
	      if (DEPRECATED_STREQN
		  (ada_opname_table[k].mangled + 1, mangled + i + 1,
		   op_len - 1) && !isalnum (mangled[i + op_len]))
		{
		  strcpy (demangled + j, ada_opname_table[k].demangled);
		  at_start_name = 0;
		  i += op_len;
		  j += strlen (ada_opname_table[k].demangled);
		  break;
		}
	    }
	  if (ada_opname_table[k].mangled != NULL)
	    continue;
	}
      at_start_name = 0;

      if (i < len0 - 4 && DEPRECATED_STREQN (mangled + i, "TK__", 4))
	i += 2;
      if (mangled[i] == 'X' && i != 0 && isalnum (mangled[i - 1]))
	{
	  do
	    i += 1;
	  while (i < len0 && (mangled[i] == 'b' || mangled[i] == 'n'));
	  if (i < len0)
	    goto Suppress;
	}
      else if (i < len0 - 2 && mangled[i] == '_' && mangled[i + 1] == '_')
	{
	  demangled[j] = '.';
	  at_start_name = 1;
	  i += 2;
	  j += 1;
	}
      else
	{
	  demangled[j] = mangled[i];
	  i += 1;
	  j += 1;
	}
    }
  demangled[j] = '\000';

  for (i = 0; demangled[i] != '\0'; i += 1)
    if (isupper (demangled[i]) || demangled[i] == ' ')
      goto Suppress;

  return demangled;

Suppress:
  GROW_VECT (demangling_buffer, demangling_buffer_size, strlen (mangled) + 3);
  demangled = demangling_buffer;
  if (mangled[0] == '<')
    strcpy (demangled, mangled);
  else
    sprintf (demangled, "<%s>", mangled);
  return demangled;

}

/* Returns non-zero iff SYM_NAME matches NAME, ignoring any trailing
 * suffixes that encode debugging information or leading _ada_ on
 * SYM_NAME (see is_name_suffix commentary for the debugging
 * information that is ignored).  If WILD, then NAME need only match a
 * suffix of SYM_NAME minus the same suffixes. Also returns 0 if
 * either argument is NULL. */

int
ada_match_name (const char *sym_name, const char *name, int wild)
{
  if (sym_name == NULL || name == NULL)
    return 0;
  else if (wild)
    return wild_match (name, strlen (name), sym_name);
  else
    {
      int len_name = strlen (name);
      return (DEPRECATED_STREQN (sym_name, name, len_name)
	      && is_name_suffix (sym_name + len_name))
	|| (DEPRECATED_STREQN (sym_name, "_ada_", 5)
	    && DEPRECATED_STREQN (sym_name + 5, name, len_name)
	    && is_name_suffix (sym_name + len_name + 5));
    }
}

/* True (non-zero) iff in Ada mode, the symbol SYM should be
   suppressed in info listings. */

int
ada_suppress_symbol_printing (struct symbol *sym)
{
  if (SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN)
    return 1;
  else
    return is_suppressed_name (DEPRECATED_SYMBOL_NAME (sym));
}


				/* Arrays */

/* Names of MAX_ADA_DIMENS bounds in P_BOUNDS fields of 
   array descriptors.  */

static char *bound_name[] = {
  "LB0", "UB0", "LB1", "UB1", "LB2", "UB2", "LB3", "UB3",
  "LB4", "UB4", "LB5", "UB5", "LB6", "UB6", "LB7", "UB7"
};

/* Maximum number of array dimensions we are prepared to handle.  */

#define MAX_ADA_DIMENS (sizeof(bound_name) / (2*sizeof(char*)))

/* Like modify_field, but allows bitpos > wordlength. */

static void
modify_general_field (char *addr, LONGEST fieldval, int bitpos, int bitsize)
{
  modify_field (addr + sizeof (LONGEST) * bitpos / (8 * sizeof (LONGEST)),
		fieldval, bitpos % (8 * sizeof (LONGEST)), bitsize);
}


/* The desc_* routines return primitive portions of array descriptors 
   (fat pointers). */

/* The descriptor or array type, if any, indicated by TYPE; removes
   level of indirection, if needed. */
static struct type *
desc_base_type (struct type *type)
{
  if (type == NULL)
    return NULL;
  CHECK_TYPEDEF (type);
  if (type != NULL && TYPE_CODE (type) == TYPE_CODE_PTR)
    return check_typedef (TYPE_TARGET_TYPE (type));
  else
    return type;
}

/* True iff TYPE indicates a "thin" array pointer type. */
static int
is_thin_pntr (struct type *type)
{
  return
    is_suffix (ada_type_name (desc_base_type (type)), "___XUT")
    || is_suffix (ada_type_name (desc_base_type (type)), "___XUT___XVE");
}

/* The descriptor type for thin pointer type TYPE. */
static struct type *
thin_descriptor_type (struct type *type)
{
  struct type *base_type = desc_base_type (type);
  if (base_type == NULL)
    return NULL;
  if (is_suffix (ada_type_name (base_type), "___XVE"))
    return base_type;
  else
    {
      struct type *alt_type = ada_find_parallel_type (base_type, "___XVE");
      if (alt_type == NULL)
	return base_type;
      else
	return alt_type;
    }
}

/* A pointer to the array data for thin-pointer value VAL. */
static struct value *
thin_data_pntr (struct value *val)
{
  struct type *type = VALUE_TYPE (val);
  if (TYPE_CODE (type) == TYPE_CODE_PTR)
    return value_cast (desc_data_type (thin_descriptor_type (type)),
		       value_copy (val));
  else
    return value_from_longest (desc_data_type (thin_descriptor_type (type)),
			       VALUE_ADDRESS (val) + VALUE_OFFSET (val));
}

/* True iff TYPE indicates a "thick" array pointer type. */
static int
is_thick_pntr (struct type *type)
{
  type = desc_base_type (type);
  return (type != NULL && TYPE_CODE (type) == TYPE_CODE_STRUCT
	  && lookup_struct_elt_type (type, "P_BOUNDS", 1) != NULL);
}

/* If TYPE is the type of an array descriptor (fat or thin pointer) or a 
   pointer to one, the type of its bounds data; otherwise, NULL. */
static struct type *
desc_bounds_type (struct type *type)
{
  struct type *r;

  type = desc_base_type (type);

  if (type == NULL)
    return NULL;
  else if (is_thin_pntr (type))
    {
      type = thin_descriptor_type (type);
      if (type == NULL)
	return NULL;
      r = lookup_struct_elt_type (type, "BOUNDS", 1);
      if (r != NULL)
	return check_typedef (r);
    }
  else if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    {
      r = lookup_struct_elt_type (type, "P_BOUNDS", 1);
      if (r != NULL)
	return check_typedef (TYPE_TARGET_TYPE (check_typedef (r)));
    }
  return NULL;
}

/* If ARR is an array descriptor (fat or thin pointer), or pointer to
   one, a pointer to its bounds data.   Otherwise NULL. */
static struct value *
desc_bounds (struct value *arr)
{
  struct type *type = check_typedef (VALUE_TYPE (arr));
  if (is_thin_pntr (type))
    {
      struct type *bounds_type =
	desc_bounds_type (thin_descriptor_type (type));
      LONGEST addr;

      if (desc_bounds_type == NULL)
	error ("Bad GNAT array descriptor");

      /* NOTE: The following calculation is not really kosher, but
         since desc_type is an XVE-encoded type (and shouldn't be),
         the correct calculation is a real pain. FIXME (and fix GCC). */
      if (TYPE_CODE (type) == TYPE_CODE_PTR)
	addr = value_as_long (arr);
      else
	addr = VALUE_ADDRESS (arr) + VALUE_OFFSET (arr);

      return
	value_from_longest (lookup_pointer_type (bounds_type),
			    addr - TYPE_LENGTH (bounds_type));
    }

  else if (is_thick_pntr (type))
    return value_struct_elt (&arr, NULL, "P_BOUNDS", NULL,
			     "Bad GNAT array descriptor");
  else
    return NULL;
}

/* If TYPE is the type of an array-descriptor (fat pointer), the bit
   position of the field containing the address of the bounds data. */
static int
fat_pntr_bounds_bitpos (struct type *type)
{
  return TYPE_FIELD_BITPOS (desc_base_type (type), 1);
}

/* If TYPE is the type of an array-descriptor (fat pointer), the bit
   size of the field containing the address of the bounds data. */
static int
fat_pntr_bounds_bitsize (struct type *type)
{
  type = desc_base_type (type);

  if (TYPE_FIELD_BITSIZE (type, 1) > 0)
    return TYPE_FIELD_BITSIZE (type, 1);
  else
    return 8 * TYPE_LENGTH (check_typedef (TYPE_FIELD_TYPE (type, 1)));
}

/* If TYPE is the type of an array descriptor (fat or thin pointer) or a 
   pointer to one, the type of its array data (a
   pointer-to-array-with-no-bounds type); otherwise,  NULL.  Use
   ada_type_of_array to get an array type with bounds data. */
static struct type *
desc_data_type (struct type *type)
{
  type = desc_base_type (type);

  /* NOTE: The following is bogus; see comment in desc_bounds. */
  if (is_thin_pntr (type))
    return lookup_pointer_type
      (desc_base_type (TYPE_FIELD_TYPE (thin_descriptor_type (type), 1)));
  else if (is_thick_pntr (type))
    return lookup_struct_elt_type (type, "P_ARRAY", 1);
  else
    return NULL;
}

/* If ARR is an array descriptor (fat or thin pointer), a pointer to
   its array data.  */
static struct value *
desc_data (struct value *arr)
{
  struct type *type = VALUE_TYPE (arr);
  if (is_thin_pntr (type))
    return thin_data_pntr (arr);
  else if (is_thick_pntr (type))
    return value_struct_elt (&arr, NULL, "P_ARRAY", NULL,
			     "Bad GNAT array descriptor");
  else
    return NULL;
}


/* If TYPE is the type of an array-descriptor (fat pointer), the bit
   position of the field containing the address of the data. */
static int
fat_pntr_data_bitpos (struct type *type)
{
  return TYPE_FIELD_BITPOS (desc_base_type (type), 0);
}

/* If TYPE is the type of an array-descriptor (fat pointer), the bit
   size of the field containing the address of the data. */
static int
fat_pntr_data_bitsize (struct type *type)
{
  type = desc_base_type (type);

  if (TYPE_FIELD_BITSIZE (type, 0) > 0)
    return TYPE_FIELD_BITSIZE (type, 0);
  else
    return TARGET_CHAR_BIT * TYPE_LENGTH (TYPE_FIELD_TYPE (type, 0));
}

/* If BOUNDS is an array-bounds structure (or pointer to one), return 
   the Ith lower bound stored in it, if WHICH is 0, and the Ith upper
   bound, if WHICH is 1.  The first bound is I=1. */
static struct value *
desc_one_bound (struct value *bounds, int i, int which)
{
  return value_struct_elt (&bounds, NULL, bound_name[2 * i + which - 2], NULL,
			   "Bad GNAT array descriptor bounds");
}

/* If BOUNDS is an array-bounds structure type, return the bit position
   of the Ith lower bound stored in it, if WHICH is 0, and the Ith upper
   bound, if WHICH is 1.  The first bound is I=1. */
static int
desc_bound_bitpos (struct type *type, int i, int which)
{
  return TYPE_FIELD_BITPOS (desc_base_type (type), 2 * i + which - 2);
}

/* If BOUNDS is an array-bounds structure type, return the bit field size
   of the Ith lower bound stored in it, if WHICH is 0, and the Ith upper
   bound, if WHICH is 1.  The first bound is I=1. */
static int
desc_bound_bitsize (struct type *type, int i, int which)
{
  type = desc_base_type (type);

  if (TYPE_FIELD_BITSIZE (type, 2 * i + which - 2) > 0)
    return TYPE_FIELD_BITSIZE (type, 2 * i + which - 2);
  else
    return 8 * TYPE_LENGTH (TYPE_FIELD_TYPE (type, 2 * i + which - 2));
}

/* If TYPE is the type of an array-bounds structure, the type of its
   Ith bound (numbering from 1). Otherwise, NULL. */
static struct type *
desc_index_type (struct type *type, int i)
{
  type = desc_base_type (type);

  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    return lookup_struct_elt_type (type, bound_name[2 * i - 2], 1);
  else
    return NULL;
}

/* The number of index positions in the array-bounds type TYPE.  0
   if TYPE is NULL. */
static int
desc_arity (struct type *type)
{
  type = desc_base_type (type);

  if (type != NULL)
    return TYPE_NFIELDS (type) / 2;
  return 0;
}


/* Non-zero iff type is a simple array type (or pointer to one). */
int
ada_is_simple_array (struct type *type)
{
  if (type == NULL)
    return 0;
  CHECK_TYPEDEF (type);
  return (TYPE_CODE (type) == TYPE_CODE_ARRAY
	  || (TYPE_CODE (type) == TYPE_CODE_PTR
	      && TYPE_CODE (TYPE_TARGET_TYPE (type)) == TYPE_CODE_ARRAY));
}

/* Non-zero iff type belongs to a GNAT array descriptor. */
int
ada_is_array_descriptor (struct type *type)
{
  struct type *data_type = desc_data_type (type);

  if (type == NULL)
    return 0;
  CHECK_TYPEDEF (type);
  return
    data_type != NULL
    && ((TYPE_CODE (data_type) == TYPE_CODE_PTR
	 && TYPE_TARGET_TYPE (data_type) != NULL
	 && TYPE_CODE (TYPE_TARGET_TYPE (data_type)) == TYPE_CODE_ARRAY)
	||
	TYPE_CODE (data_type) == TYPE_CODE_ARRAY)
    && desc_arity (desc_bounds_type (type)) > 0;
}

/* Non-zero iff type is a partially mal-formed GNAT array
   descriptor.  (FIXME: This is to compensate for some problems with 
   debugging output from GNAT.  Re-examine periodically to see if it
   is still needed. */
int
ada_is_bogus_array_descriptor (struct type *type)
{
  return
    type != NULL
    && TYPE_CODE (type) == TYPE_CODE_STRUCT
    && (lookup_struct_elt_type (type, "P_BOUNDS", 1) != NULL
	|| lookup_struct_elt_type (type, "P_ARRAY", 1) != NULL)
    && !ada_is_array_descriptor (type);
}


/* If ARR has a record type in the form of a standard GNAT array descriptor, 
   (fat pointer) returns the type of the array data described---specifically,
   a pointer-to-array type.  If BOUNDS is non-zero, the bounds data are filled 
   in from the descriptor; otherwise, they are left unspecified.  If
   the ARR denotes a null array descriptor and BOUNDS is non-zero, 
   returns NULL.  The result is simply the type of ARR if ARR is not 
   a descriptor.  */
struct type *
ada_type_of_array (struct value *arr, int bounds)
{
  if (ada_is_packed_array_type (VALUE_TYPE (arr)))
    return decode_packed_array_type (VALUE_TYPE (arr));

  if (!ada_is_array_descriptor (VALUE_TYPE (arr)))
    return VALUE_TYPE (arr);

  if (!bounds)
    return
      check_typedef (TYPE_TARGET_TYPE (desc_data_type (VALUE_TYPE (arr))));
  else
    {
      struct type *elt_type;
      int arity;
      struct value *descriptor;
      struct objfile *objf = TYPE_OBJFILE (VALUE_TYPE (arr));

      elt_type = ada_array_element_type (VALUE_TYPE (arr), -1);
      arity = ada_array_arity (VALUE_TYPE (arr));

      if (elt_type == NULL || arity == 0)
	return check_typedef (VALUE_TYPE (arr));

      descriptor = desc_bounds (arr);
      if (value_as_long (descriptor) == 0)
	return NULL;
      while (arity > 0)
	{
	  struct type *range_type = alloc_type (objf);
	  struct type *array_type = alloc_type (objf);
	  struct value *low = desc_one_bound (descriptor, arity, 0);
	  struct value *high = desc_one_bound (descriptor, arity, 1);
	  arity -= 1;

	  create_range_type (range_type, VALUE_TYPE (low),
			     (int) value_as_long (low),
			     (int) value_as_long (high));
	  elt_type = create_array_type (array_type, elt_type, range_type);
	}

      return lookup_pointer_type (elt_type);
    }
}

/* If ARR does not represent an array, returns ARR unchanged.
   Otherwise, returns either a standard GDB array with bounds set 
   appropriately or, if ARR is a non-null fat pointer, a pointer to a standard 
   GDB array.  Returns NULL if ARR is a null fat pointer. */
struct value *
ada_coerce_to_simple_array_ptr (struct value *arr)
{
  if (ada_is_array_descriptor (VALUE_TYPE (arr)))
    {
      struct type *arrType = ada_type_of_array (arr, 1);
      if (arrType == NULL)
	return NULL;
      return value_cast (arrType, value_copy (desc_data (arr)));
    }
  else if (ada_is_packed_array_type (VALUE_TYPE (arr)))
    return decode_packed_array (arr);
  else
    return arr;
}

/* If ARR does not represent an array, returns ARR unchanged.
   Otherwise, returns a standard GDB array describing ARR (which may
   be ARR itself if it already is in the proper form). */
struct value *
ada_coerce_to_simple_array (struct value *arr)
{
  if (ada_is_array_descriptor (VALUE_TYPE (arr)))
    {
      struct value *arrVal = ada_coerce_to_simple_array_ptr (arr);
      if (arrVal == NULL)
	error ("Bounds unavailable for null array pointer.");
      return value_ind (arrVal);
    }
  else if (ada_is_packed_array_type (VALUE_TYPE (arr)))
    return decode_packed_array (arr);
  else
    return arr;
}

/* If TYPE represents a GNAT array type, return it translated to an
   ordinary GDB array type (possibly with BITSIZE fields indicating
   packing). For other types, is the identity. */
struct type *
ada_coerce_to_simple_array_type (struct type *type)
{
  struct value *mark = value_mark ();
  struct value *dummy = value_from_longest (builtin_type_long, 0);
  struct type *result;
  VALUE_TYPE (dummy) = type;
  result = ada_type_of_array (dummy, 0);
  value_free_to_mark (dummy);
  return result;
}

/* Non-zero iff TYPE represents a standard GNAT packed-array type. */
int
ada_is_packed_array_type (struct type *type)
{
  if (type == NULL)
    return 0;
  CHECK_TYPEDEF (type);
  return
    ada_type_name (type) != NULL
    && strstr (ada_type_name (type), "___XP") != NULL;
}

/* Given that TYPE is a standard GDB array type with all bounds filled
   in, and that the element size of its ultimate scalar constituents
   (that is, either its elements, or, if it is an array of arrays, its
   elements' elements, etc.) is *ELT_BITS, return an identical type,
   but with the bit sizes of its elements (and those of any
   constituent arrays) recorded in the BITSIZE components of its
   TYPE_FIELD_BITSIZE values, and with *ELT_BITS set to its total size 
   in bits. */
static struct type *
packed_array_type (struct type *type, long *elt_bits)
{
  struct type *new_elt_type;
  struct type *new_type;
  LONGEST low_bound, high_bound;

  CHECK_TYPEDEF (type);
  if (TYPE_CODE (type) != TYPE_CODE_ARRAY)
    return type;

  new_type = alloc_type (TYPE_OBJFILE (type));
  new_elt_type = packed_array_type (check_typedef (TYPE_TARGET_TYPE (type)),
				    elt_bits);
  create_array_type (new_type, new_elt_type, TYPE_FIELD_TYPE (type, 0));
  TYPE_FIELD_BITSIZE (new_type, 0) = *elt_bits;
  TYPE_NAME (new_type) = ada_type_name (type);

  if (get_discrete_bounds (TYPE_FIELD_TYPE (type, 0),
			   &low_bound, &high_bound) < 0)
    low_bound = high_bound = 0;
  if (high_bound < low_bound)
    *elt_bits = TYPE_LENGTH (new_type) = 0;
  else
    {
      *elt_bits *= (high_bound - low_bound + 1);
      TYPE_LENGTH (new_type) =
	(*elt_bits + HOST_CHAR_BIT - 1) / HOST_CHAR_BIT;
    }

  /*  TYPE_FLAGS (new_type) |= TYPE_FLAG_FIXED_INSTANCE; */
  /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
  return new_type;
}

/* The array type encoded by TYPE, where ada_is_packed_array_type (TYPE).
 */
static struct type *
decode_packed_array_type (struct type *type)
{
  struct symbol **syms;
  struct block **blocks;
  const char *raw_name = ada_type_name (check_typedef (type));
  char *name = (char *) alloca (strlen (raw_name) + 1);
  char *tail = strstr (raw_name, "___XP");
  struct type *shadow_type;
  long bits;
  int i, n;

  memcpy (name, raw_name, tail - raw_name);
  name[tail - raw_name] = '\000';

  /* NOTE: Use ada_lookup_symbol_list because of bug in some versions
   * of gcc (Solaris, e.g.). FIXME when compiler is fixed. */
  n = ada_lookup_symbol_list (name, get_selected_block (NULL),
			      VAR_DOMAIN, &syms, &blocks);
  for (i = 0; i < n; i += 1)
    if (syms[i] != NULL && SYMBOL_CLASS (syms[i]) == LOC_TYPEDEF
	&& DEPRECATED_STREQ (name, ada_type_name (SYMBOL_TYPE (syms[i]))))
      break;
  if (i >= n)
    {
      warning ("could not find bounds information on packed array");
      return NULL;
    }
  shadow_type = SYMBOL_TYPE (syms[i]);

  if (TYPE_CODE (shadow_type) != TYPE_CODE_ARRAY)
    {
      warning ("could not understand bounds information on packed array");
      return NULL;
    }

  if (sscanf (tail + sizeof ("___XP") - 1, "%ld", &bits) != 1)
    {
      warning ("could not understand bit size information on packed array");
      return NULL;
    }

  return packed_array_type (shadow_type, &bits);
}

/* Given that ARR is a struct value* indicating a GNAT packed array,
   returns a simple array that denotes that array.  Its type is a
   standard GDB array type except that the BITSIZEs of the array
   target types are set to the number of bits in each element, and the
   type length is set appropriately. */

static struct value *
decode_packed_array (struct value *arr)
{
  struct type *type = decode_packed_array_type (VALUE_TYPE (arr));

  if (type == NULL)
    {
      error ("can't unpack array");
      return NULL;
    }
  else
    return coerce_unspec_val_to_type (arr, 0, type);
}


/* The value of the element of packed array ARR at the ARITY indices
   given in IND.   ARR must be a simple array. */

static struct value *
value_subscript_packed (struct value *arr, int arity, struct value **ind)
{
  int i;
  int bits, elt_off, bit_off;
  long elt_total_bit_offset;
  struct type *elt_type;
  struct value *v;

  bits = 0;
  elt_total_bit_offset = 0;
  elt_type = check_typedef (VALUE_TYPE (arr));
  for (i = 0; i < arity; i += 1)
    {
      if (TYPE_CODE (elt_type) != TYPE_CODE_ARRAY
	  || TYPE_FIELD_BITSIZE (elt_type, 0) == 0)
	error
	  ("attempt to do packed indexing of something other than a packed array");
      else
	{
	  struct type *range_type = TYPE_INDEX_TYPE (elt_type);
	  LONGEST lowerbound, upperbound;
	  LONGEST idx;

	  if (get_discrete_bounds (range_type, &lowerbound, &upperbound) < 0)
	    {
	      warning ("don't know bounds of array");
	      lowerbound = upperbound = 0;
	    }

	  idx = value_as_long (value_pos_atr (ind[i]));
	  if (idx < lowerbound || idx > upperbound)
	    warning ("packed array index %ld out of bounds", (long) idx);
	  bits = TYPE_FIELD_BITSIZE (elt_type, 0);
	  elt_total_bit_offset += (idx - lowerbound) * bits;
	  elt_type = check_typedef (TYPE_TARGET_TYPE (elt_type));
	}
    }
  elt_off = elt_total_bit_offset / HOST_CHAR_BIT;
  bit_off = elt_total_bit_offset % HOST_CHAR_BIT;

  v = ada_value_primitive_packed_val (arr, NULL, elt_off, bit_off,
				      bits, elt_type);
  if (VALUE_LVAL (arr) == lval_internalvar)
    VALUE_LVAL (v) = lval_internalvar_component;
  else
    VALUE_LVAL (v) = VALUE_LVAL (arr);
  return v;
}

/* Non-zero iff TYPE includes negative integer values. */

static int
has_negatives (struct type *type)
{
  switch (TYPE_CODE (type))
    {
    default:
      return 0;
    case TYPE_CODE_INT:
      return !TYPE_UNSIGNED (type);
    case TYPE_CODE_RANGE:
      return TYPE_LOW_BOUND (type) < 0;
    }
}


/* Create a new value of type TYPE from the contents of OBJ starting
   at byte OFFSET, and bit offset BIT_OFFSET within that byte,
   proceeding for BIT_SIZE bits.  If OBJ is an lval in memory, then
   assigning through the result will set the field fetched from. OBJ
   may also be NULL, in which case, VALADDR+OFFSET must address the
   start of storage containing the packed value.  The value returned 
   in this case is never an lval.   
   Assumes 0 <= BIT_OFFSET < HOST_CHAR_BIT. */

struct value *
ada_value_primitive_packed_val (struct value *obj, char *valaddr, long offset,
				int bit_offset, int bit_size,
				struct type *type)
{
  struct value *v;
  int src,			/* Index into the source area. */
    targ,			/* Index into the target area. */
    i, srcBitsLeft,		/* Number of source bits left to move. */
    nsrc, ntarg,		/* Number of source and target bytes. */
    unusedLS,			/* Number of bits in next significant
				 * byte of source that are unused. */
    accumSize;			/* Number of meaningful bits in accum */
  unsigned char *bytes;		/* First byte containing data to unpack. */
  unsigned char *unpacked;
  unsigned long accum;		/* Staging area for bits being transferred */
  unsigned char sign;
  int len = (bit_size + bit_offset + HOST_CHAR_BIT - 1) / 8;
  /* Transmit bytes from least to most significant; delta is the
   * direction the indices move. */
  int delta = BITS_BIG_ENDIAN ? -1 : 1;

  CHECK_TYPEDEF (type);

  if (obj == NULL)
    {
      v = allocate_value (type);
      bytes = (unsigned char *) (valaddr + offset);
    }
  else if (VALUE_LAZY (obj))
    {
      v = value_at (type,
		    VALUE_ADDRESS (obj) + VALUE_OFFSET (obj) + offset, NULL);
      bytes = (unsigned char *) alloca (len);
      read_memory (VALUE_ADDRESS (v), bytes, len);
    }
  else
    {
      v = allocate_value (type);
      bytes = (unsigned char *) VALUE_CONTENTS (obj) + offset;
    }

  if (obj != NULL)
    {
      VALUE_LVAL (v) = VALUE_LVAL (obj);
      if (VALUE_LVAL (obj) == lval_internalvar)
	VALUE_LVAL (v) = lval_internalvar_component;
      VALUE_ADDRESS (v) = VALUE_ADDRESS (obj) + VALUE_OFFSET (obj) + offset;
      VALUE_BITPOS (v) = bit_offset + VALUE_BITPOS (obj);
      VALUE_BITSIZE (v) = bit_size;
      if (VALUE_BITPOS (v) >= HOST_CHAR_BIT)
	{
	  VALUE_ADDRESS (v) += 1;
	  VALUE_BITPOS (v) -= HOST_CHAR_BIT;
	}
    }
  else
    VALUE_BITSIZE (v) = bit_size;
  unpacked = (unsigned char *) VALUE_CONTENTS (v);

  srcBitsLeft = bit_size;
  nsrc = len;
  ntarg = TYPE_LENGTH (type);
  sign = 0;
  if (bit_size == 0)
    {
      memset (unpacked, 0, TYPE_LENGTH (type));
      return v;
    }
  else if (BITS_BIG_ENDIAN)
    {
      src = len - 1;
      if (has_negatives (type) &&
	  ((bytes[0] << bit_offset) & (1 << (HOST_CHAR_BIT - 1))))
	sign = ~0;

      unusedLS =
	(HOST_CHAR_BIT - (bit_size + bit_offset) % HOST_CHAR_BIT)
	% HOST_CHAR_BIT;

      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_ARRAY:
	case TYPE_CODE_UNION:
	case TYPE_CODE_STRUCT:
	  /* Non-scalar values must be aligned at a byte boundary. */
	  accumSize =
	    (HOST_CHAR_BIT - bit_size % HOST_CHAR_BIT) % HOST_CHAR_BIT;
	  /* And are placed at the beginning (most-significant) bytes
	   * of the target. */
	  targ = src;
	  break;
	default:
	  accumSize = 0;
	  targ = TYPE_LENGTH (type) - 1;
	  break;
	}
    }
  else
    {
      int sign_bit_offset = (bit_size + bit_offset - 1) % 8;

      src = targ = 0;
      unusedLS = bit_offset;
      accumSize = 0;

      if (has_negatives (type) && (bytes[len - 1] & (1 << sign_bit_offset)))
	sign = ~0;
    }

  accum = 0;
  while (nsrc > 0)
    {
      /* Mask for removing bits of the next source byte that are not
       * part of the value. */
      unsigned int unusedMSMask =
	(1 << (srcBitsLeft >= HOST_CHAR_BIT ? HOST_CHAR_BIT : srcBitsLeft)) -
	1;
      /* Sign-extend bits for this byte. */
      unsigned int signMask = sign & ~unusedMSMask;
      accum |=
	(((bytes[src] >> unusedLS) & unusedMSMask) | signMask) << accumSize;
      accumSize += HOST_CHAR_BIT - unusedLS;
      if (accumSize >= HOST_CHAR_BIT)
	{
	  unpacked[targ] = accum & ~(~0L << HOST_CHAR_BIT);
	  accumSize -= HOST_CHAR_BIT;
	  accum >>= HOST_CHAR_BIT;
	  ntarg -= 1;
	  targ += delta;
	}
      srcBitsLeft -= HOST_CHAR_BIT - unusedLS;
      unusedLS = 0;
      nsrc -= 1;
      src += delta;
    }
  while (ntarg > 0)
    {
      accum |= sign << accumSize;
      unpacked[targ] = accum & ~(~0L << HOST_CHAR_BIT);
      accumSize -= HOST_CHAR_BIT;
      accum >>= HOST_CHAR_BIT;
      ntarg -= 1;
      targ += delta;
    }

  return v;
}

/* Move N bits from SOURCE, starting at bit offset SRC_OFFSET to
   TARGET, starting at bit offset TARG_OFFSET.  SOURCE and TARGET must
   not overlap. */
static void
move_bits (char *target, int targ_offset, char *source, int src_offset, int n)
{
  unsigned int accum, mask;
  int accum_bits, chunk_size;

  target += targ_offset / HOST_CHAR_BIT;
  targ_offset %= HOST_CHAR_BIT;
  source += src_offset / HOST_CHAR_BIT;
  src_offset %= HOST_CHAR_BIT;
  if (BITS_BIG_ENDIAN)
    {
      accum = (unsigned char) *source;
      source += 1;
      accum_bits = HOST_CHAR_BIT - src_offset;

      while (n > 0)
	{
	  int unused_right;
	  accum = (accum << HOST_CHAR_BIT) + (unsigned char) *source;
	  accum_bits += HOST_CHAR_BIT;
	  source += 1;
	  chunk_size = HOST_CHAR_BIT - targ_offset;
	  if (chunk_size > n)
	    chunk_size = n;
	  unused_right = HOST_CHAR_BIT - (chunk_size + targ_offset);
	  mask = ((1 << chunk_size) - 1) << unused_right;
	  *target =
	    (*target & ~mask)
	    | ((accum >> (accum_bits - chunk_size - unused_right)) & mask);
	  n -= chunk_size;
	  accum_bits -= chunk_size;
	  target += 1;
	  targ_offset = 0;
	}
    }
  else
    {
      accum = (unsigned char) *source >> src_offset;
      source += 1;
      accum_bits = HOST_CHAR_BIT - src_offset;

      while (n > 0)
	{
	  accum = accum + ((unsigned char) *source << accum_bits);
	  accum_bits += HOST_CHAR_BIT;
	  source += 1;
	  chunk_size = HOST_CHAR_BIT - targ_offset;
	  if (chunk_size > n)
	    chunk_size = n;
	  mask = ((1 << chunk_size) - 1) << targ_offset;
	  *target = (*target & ~mask) | ((accum << targ_offset) & mask);
	  n -= chunk_size;
	  accum_bits -= chunk_size;
	  accum >>= chunk_size;
	  target += 1;
	  targ_offset = 0;
	}
    }
}


/* Store the contents of FROMVAL into the location of TOVAL.
   Return a new value with the location of TOVAL and contents of
   FROMVAL.   Handles assignment into packed fields that have
   floating-point or non-scalar types. */

static struct value *
ada_value_assign (struct value *toval, struct value *fromval)
{
  struct type *type = VALUE_TYPE (toval);
  int bits = VALUE_BITSIZE (toval);

  if (!toval->modifiable)
    error ("Left operand of assignment is not a modifiable lvalue.");

  COERCE_REF (toval);

  if (VALUE_LVAL (toval) == lval_memory
      && bits > 0
      && (TYPE_CODE (type) == TYPE_CODE_FLT
	  || TYPE_CODE (type) == TYPE_CODE_STRUCT))
    {
      int len =
	(VALUE_BITPOS (toval) + bits + HOST_CHAR_BIT - 1) / HOST_CHAR_BIT;
      char *buffer = (char *) alloca (len);
      struct value *val;

      if (TYPE_CODE (type) == TYPE_CODE_FLT)
	fromval = value_cast (type, fromval);

      read_memory (VALUE_ADDRESS (toval) + VALUE_OFFSET (toval), buffer, len);
      if (BITS_BIG_ENDIAN)
	move_bits (buffer, VALUE_BITPOS (toval),
		   VALUE_CONTENTS (fromval),
		   TYPE_LENGTH (VALUE_TYPE (fromval)) * TARGET_CHAR_BIT -
		   bits, bits);
      else
	move_bits (buffer, VALUE_BITPOS (toval), VALUE_CONTENTS (fromval),
		   0, bits);
      write_memory (VALUE_ADDRESS (toval) + VALUE_OFFSET (toval), buffer,
		    len);

      val = value_copy (toval);
      memcpy (VALUE_CONTENTS_RAW (val), VALUE_CONTENTS (fromval),
	      TYPE_LENGTH (type));
      VALUE_TYPE (val) = type;

      return val;
    }

  return value_assign (toval, fromval);
}


/* The value of the element of array ARR at the ARITY indices given in IND. 
   ARR may be either a simple array, GNAT array descriptor, or pointer 
   thereto.  */

struct value *
ada_value_subscript (struct value *arr, int arity, struct value **ind)
{
  int k;
  struct value *elt;
  struct type *elt_type;

  elt = ada_coerce_to_simple_array (arr);

  elt_type = check_typedef (VALUE_TYPE (elt));
  if (TYPE_CODE (elt_type) == TYPE_CODE_ARRAY
      && TYPE_FIELD_BITSIZE (elt_type, 0) > 0)
    return value_subscript_packed (elt, arity, ind);

  for (k = 0; k < arity; k += 1)
    {
      if (TYPE_CODE (elt_type) != TYPE_CODE_ARRAY)
	error ("too many subscripts (%d expected)", k);
      elt = value_subscript (elt, value_pos_atr (ind[k]));
    }
  return elt;
}

/* Assuming ARR is a pointer to a standard GDB array of type TYPE, the
   value of the element of *ARR at the ARITY indices given in
   IND. Does not read the entire array into memory. */

struct value *
ada_value_ptr_subscript (struct value *arr, struct type *type, int arity,
			 struct value **ind)
{
  int k;

  for (k = 0; k < arity; k += 1)
    {
      LONGEST lwb, upb;
      struct value *idx;

      if (TYPE_CODE (type) != TYPE_CODE_ARRAY)
	error ("too many subscripts (%d expected)", k);
      arr = value_cast (lookup_pointer_type (TYPE_TARGET_TYPE (type)),
			value_copy (arr));
      get_discrete_bounds (TYPE_INDEX_TYPE (type), &lwb, &upb);
      if (lwb == 0)
	idx = ind[k];
      else
	idx = value_sub (ind[k], value_from_longest (builtin_type_int, lwb));
      arr = value_add (arr, idx);
      type = TYPE_TARGET_TYPE (type);
    }

  return value_ind (arr);
}

/* If type is a record type in the form of a standard GNAT array
   descriptor, returns the number of dimensions for type.  If arr is a
   simple array, returns the number of "array of"s that prefix its
   type designation. Otherwise, returns 0. */

int
ada_array_arity (struct type *type)
{
  int arity;

  if (type == NULL)
    return 0;

  type = desc_base_type (type);

  arity = 0;
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    return desc_arity (desc_bounds_type (type));
  else
    while (TYPE_CODE (type) == TYPE_CODE_ARRAY)
      {
	arity += 1;
	type = check_typedef (TYPE_TARGET_TYPE (type));
      }

  return arity;
}

/* If TYPE is a record type in the form of a standard GNAT array
   descriptor or a simple array type, returns the element type for
   TYPE after indexing by NINDICES indices, or by all indices if
   NINDICES is -1. Otherwise, returns NULL. */

struct type *
ada_array_element_type (struct type *type, int nindices)
{
  type = desc_base_type (type);

  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    {
      int k;
      struct type *p_array_type;

      p_array_type = desc_data_type (type);

      k = ada_array_arity (type);
      if (k == 0)
	return NULL;

      /* Initially p_array_type = elt_type(*)[]...(k times)...[] */
      if (nindices >= 0 && k > nindices)
	k = nindices;
      p_array_type = TYPE_TARGET_TYPE (p_array_type);
      while (k > 0 && p_array_type != NULL)
	{
	  p_array_type = check_typedef (TYPE_TARGET_TYPE (p_array_type));
	  k -= 1;
	}
      return p_array_type;
    }
  else if (TYPE_CODE (type) == TYPE_CODE_ARRAY)
    {
      while (nindices != 0 && TYPE_CODE (type) == TYPE_CODE_ARRAY)
	{
	  type = TYPE_TARGET_TYPE (type);
	  nindices -= 1;
	}
      return type;
    }

  return NULL;
}

/* The type of nth index in arrays of given type (n numbering from 1).  Does 
   not examine memory. */

struct type *
ada_index_type (struct type *type, int n)
{
  type = desc_base_type (type);

  if (n > ada_array_arity (type))
    return NULL;

  if (ada_is_simple_array (type))
    {
      int i;

      for (i = 1; i < n; i += 1)
	type = TYPE_TARGET_TYPE (type);

      return TYPE_TARGET_TYPE (TYPE_FIELD_TYPE (type, 0));
    }
  else
    return desc_index_type (desc_bounds_type (type), n);
}

/* Given that arr is an array type, returns the lower bound of the
   Nth index (numbering from 1) if WHICH is 0, and the upper bound if
   WHICH is 1. This returns bounds 0 .. -1 if ARR_TYPE is an
   array-descriptor type.  If TYPEP is non-null, *TYPEP is set to the 
   bounds type.  It works for other arrays with bounds supplied by 
   run-time quantities other than discriminants. */

LONGEST
ada_array_bound_from_type (struct type * arr_type, int n, int which,
			   struct type ** typep)
{
  struct type *type;
  struct type *index_type_desc;

  if (ada_is_packed_array_type (arr_type))
    arr_type = decode_packed_array_type (arr_type);

  if (arr_type == NULL || !ada_is_simple_array (arr_type))
    {
      if (typep != NULL)
	*typep = builtin_type_int;
      return (LONGEST) - which;
    }

  if (TYPE_CODE (arr_type) == TYPE_CODE_PTR)
    type = TYPE_TARGET_TYPE (arr_type);
  else
    type = arr_type;

  index_type_desc = ada_find_parallel_type (type, "___XA");
  if (index_type_desc == NULL)
    {
      struct type *range_type;
      struct type *index_type;

      while (n > 1)
	{
	  type = TYPE_TARGET_TYPE (type);
	  n -= 1;
	}

      range_type = TYPE_INDEX_TYPE (type);
      index_type = TYPE_TARGET_TYPE (range_type);
      if (TYPE_CODE (index_type) == TYPE_CODE_UNDEF)
	index_type = builtin_type_long;
      if (typep != NULL)
	*typep = index_type;
      return
	(LONGEST) (which == 0
		   ? TYPE_LOW_BOUND (range_type)
		   : TYPE_HIGH_BOUND (range_type));
    }
  else
    {
      struct type *index_type =
	to_fixed_range_type (TYPE_FIELD_NAME (index_type_desc, n - 1),
			     NULL, TYPE_OBJFILE (arr_type));
      if (typep != NULL)
	*typep = TYPE_TARGET_TYPE (index_type);
      return
	(LONGEST) (which == 0
		   ? TYPE_LOW_BOUND (index_type)
		   : TYPE_HIGH_BOUND (index_type));
    }
}

/* Given that arr is an array value, returns the lower bound of the
   nth index (numbering from 1) if which is 0, and the upper bound if
   which is 1. This routine will also work for arrays with bounds
   supplied by run-time quantities other than discriminants. */

struct value *
ada_array_bound (struct value *arr, int n, int which)
{
  struct type *arr_type = VALUE_TYPE (arr);

  if (ada_is_packed_array_type (arr_type))
    return ada_array_bound (decode_packed_array (arr), n, which);
  else if (ada_is_simple_array (arr_type))
    {
      struct type *type;
      LONGEST v = ada_array_bound_from_type (arr_type, n, which, &type);
      return value_from_longest (type, v);
    }
  else
    return desc_one_bound (desc_bounds (arr), n, which);
}

/* Given that arr is an array value, returns the length of the
   nth index.  This routine will also work for arrays with bounds
   supplied by run-time quantities other than discriminants. Does not
   work for arrays indexed by enumeration types with representation
   clauses at the moment. */

struct value *
ada_array_length (struct value *arr, int n)
{
  struct type *arr_type = check_typedef (VALUE_TYPE (arr));
  struct type *index_type_desc;

  if (ada_is_packed_array_type (arr_type))
    return ada_array_length (decode_packed_array (arr), n);

  if (ada_is_simple_array (arr_type))
    {
      struct type *type;
      LONGEST v =
	ada_array_bound_from_type (arr_type, n, 1, &type) -
	ada_array_bound_from_type (arr_type, n, 0, NULL) + 1;
      return value_from_longest (type, v);
    }
  else
    return
      value_from_longest (builtin_type_ada_int,
			  value_as_long (desc_one_bound (desc_bounds (arr),
							 n, 1))
			  - value_as_long (desc_one_bound (desc_bounds (arr),
							   n, 0)) + 1);
}


				/* Name resolution */

/* The "demangled" name for the user-definable Ada operator corresponding
   to op. */

static const char *
ada_op_name (enum exp_opcode op)
{
  int i;

  for (i = 0; ada_opname_table[i].mangled != NULL; i += 1)
    {
      if (ada_opname_table[i].op == op)
	return ada_opname_table[i].demangled;
    }
  error ("Could not find operator name for opcode");
}


/* Same as evaluate_type (*EXP), but resolves ambiguous symbol 
   references (OP_UNRESOLVED_VALUES) and converts operators that are 
   user-defined into appropriate function calls.  If CONTEXT_TYPE is 
   non-null, it provides a preferred result type [at the moment, only
   type void has any effect---causing procedures to be preferred over
   functions in calls].  A null CONTEXT_TYPE indicates that a non-void
   return type is preferred.  The variable unresolved_names contains a list
   of character strings referenced by expout that should be freed.  
   May change (expand) *EXP.  */

void
ada_resolve (struct expression **expp, struct type *context_type)
{
  int pc;
  pc = 0;
  ada_resolve_subexp (expp, &pc, 1, context_type);
}

/* Resolve the operator of the subexpression beginning at 
   position *POS of *EXPP. "Resolving" consists of replacing
   OP_UNRESOLVED_VALUE with an appropriate OP_VAR_VALUE, replacing
   built-in operators with function calls to user-defined operators,
   where appropriate, and (when DEPROCEDURE_P is non-zero), converting
   function-valued variables into parameterless calls.  May expand
   EXP. The CONTEXT_TYPE functions as in ada_resolve, above. */

static struct value *
ada_resolve_subexp (struct expression **expp, int *pos, int deprocedure_p,
		    struct type *context_type)
{
  int pc = *pos;
  int i;
  struct expression *exp;	/* Convenience: == *expp */
  enum exp_opcode op = (*expp)->elts[pc].opcode;
  struct value **argvec;	/* Vector of operand types (alloca'ed). */
  int nargs;			/* Number of operands */

  argvec = NULL;
  nargs = 0;
  exp = *expp;

  /* Pass one: resolve operands, saving their types and updating *pos. */
  switch (op)
    {
    case OP_VAR_VALUE:
      /*    case OP_UNRESOLVED_VALUE: */
      /* FIXME:  OP_UNRESOLVED_VALUE should be defined in expression.h */
      *pos += 4;
      break;

    case OP_FUNCALL:
      nargs = longest_to_int (exp->elts[pc + 1].longconst) + 1;
      /* FIXME:  OP_UNRESOLVED_VALUE should be defined in expression.h */
      /*      if (exp->elts[pc+3].opcode == OP_UNRESOLVED_VALUE)        
         {
         *pos += 7;

         argvec = (struct value* *) alloca (sizeof (struct value*) * (nargs + 1));
         for (i = 0; i < nargs-1; i += 1)
         argvec[i] = ada_resolve_subexp (expp, pos, 1, NULL);
         argvec[i] = NULL;
         }
         else
         {
         *pos += 3;
         ada_resolve_subexp (expp, pos, 0, NULL);
         for (i = 1; i < nargs; i += 1)
         ada_resolve_subexp (expp, pos, 1, NULL);
         }
       */
      exp = *expp;
      break;

      /* FIXME:  UNOP_QUAL should be defined in expression.h */
      /*    case UNOP_QUAL:
         nargs = 1;
         *pos += 3;
         ada_resolve_subexp (expp, pos, 1, exp->elts[pc + 1].type);
         exp = *expp;
         break;
       */
      /* FIXME:  OP_ATTRIBUTE should be defined in expression.h */
      /*    case OP_ATTRIBUTE:
         nargs = longest_to_int (exp->elts[pc + 1].longconst) + 1;
         *pos += 4;
         for (i = 0; i < nargs; i += 1)
         ada_resolve_subexp (expp, pos, 1, NULL);
         exp = *expp;
         break;
       */
    case UNOP_ADDR:
      nargs = 1;
      *pos += 1;
      ada_resolve_subexp (expp, pos, 0, NULL);
      exp = *expp;
      break;

    case BINOP_ASSIGN:
      {
	struct value *arg1;
	nargs = 2;
	*pos += 1;
	arg1 = ada_resolve_subexp (expp, pos, 0, NULL);
	if (arg1 == NULL)
	  ada_resolve_subexp (expp, pos, 1, NULL);
	else
	  ada_resolve_subexp (expp, pos, 1, VALUE_TYPE (arg1));
	break;
      }

    default:
      switch (op)
	{
	default:
	  error ("Unexpected operator during name resolution");
	case UNOP_CAST:
	  /*    case UNOP_MBR:
	     nargs = 1;
	     *pos += 3;
	     break;
	   */
	case BINOP_ADD:
	case BINOP_SUB:
	case BINOP_MUL:
	case BINOP_DIV:
	case BINOP_REM:
	case BINOP_MOD:
	case BINOP_EXP:
	case BINOP_CONCAT:
	case BINOP_LOGICAL_AND:
	case BINOP_LOGICAL_OR:
	case BINOP_BITWISE_AND:
	case BINOP_BITWISE_IOR:
	case BINOP_BITWISE_XOR:

	case BINOP_EQUAL:
	case BINOP_NOTEQUAL:
	case BINOP_LESS:
	case BINOP_GTR:
	case BINOP_LEQ:
	case BINOP_GEQ:

	case BINOP_REPEAT:
	case BINOP_SUBSCRIPT:
	case BINOP_COMMA:
	  nargs = 2;
	  *pos += 1;
	  break;

	case UNOP_NEG:
	case UNOP_PLUS:
	case UNOP_LOGICAL_NOT:
	case UNOP_ABS:
	case UNOP_IND:
	  nargs = 1;
	  *pos += 1;
	  break;

	case OP_LONG:
	case OP_DOUBLE:
	case OP_VAR_VALUE:
	  *pos += 4;
	  break;

	case OP_TYPE:
	case OP_BOOL:
	case OP_LAST:
	case OP_REGISTER:
	case OP_INTERNALVAR:
	  *pos += 3;
	  break;

	case UNOP_MEMVAL:
	  *pos += 3;
	  nargs = 1;
	  break;

	case STRUCTOP_STRUCT:
	case STRUCTOP_PTR:
	  nargs = 1;
	  *pos += 4 + BYTES_TO_EXP_ELEM (exp->elts[pc + 1].longconst + 1);
	  break;

	case OP_ARRAY:
	  *pos += 4;
	  nargs = longest_to_int (exp->elts[pc + 2].longconst) + 1;
	  nargs -= longest_to_int (exp->elts[pc + 1].longconst);
	  /* A null array contains one dummy element to give the type. */
	  /*      if (nargs == 0)
	     nargs = 1;
	     break; */

	case TERNOP_SLICE:
	  /* FIXME: TERNOP_MBR should be defined in expression.h */
	  /*    case TERNOP_MBR:
	     *pos += 1;
	     nargs = 3;
	     break;
	   */
	  /* FIXME: BINOP_MBR should be defined in expression.h */
	  /*    case BINOP_MBR:
	     *pos += 3;
	     nargs = 2;
	     break; */
	}

      argvec =
	(struct value * *) alloca (sizeof (struct value *) * (nargs + 1));
      for (i = 0; i < nargs; i += 1)
	argvec[i] = ada_resolve_subexp (expp, pos, 1, NULL);
      argvec[i] = NULL;
      exp = *expp;
      break;
    }

  /* Pass two: perform any resolution on principal operator. */
  switch (op)
    {
    default:
      break;

      /* FIXME:  OP_UNRESOLVED_VALUE should be defined in expression.h */
      /*    case OP_UNRESOLVED_VALUE:
         {
         struct symbol** candidate_syms;
         struct block** candidate_blocks;
         int n_candidates;

         n_candidates = ada_lookup_symbol_list (exp->elts[pc + 2].name,
         exp->elts[pc + 1].block,
         VAR_DOMAIN,
         &candidate_syms,
         &candidate_blocks);

         if (n_candidates > 1) 
         { */
      /* Types tend to get re-introduced locally, so if there
         are any local symbols that are not types, first filter
   out all types. *//*
   int j;
   for (j = 0; j < n_candidates; j += 1) 
   switch (SYMBOL_CLASS (candidate_syms[j])) 
   {
   case LOC_REGISTER:
   case LOC_ARG:
   case LOC_REF_ARG:
   case LOC_REGPARM:
   case LOC_REGPARM_ADDR:
   case LOC_LOCAL:
   case LOC_LOCAL_ARG:
   case LOC_BASEREG:
   case LOC_BASEREG_ARG:
   case LOC_COMPUTED:
   case LOC_COMPUTED_ARG:
   goto FoundNonType;
   default:
   break;
   }
   FoundNonType:
   if (j < n_candidates) 
   {
   j = 0;
   while (j < n_candidates) 
   {
   if (SYMBOL_CLASS (candidate_syms[j]) == LOC_TYPEDEF)
   {
   candidate_syms[j] = candidate_syms[n_candidates-1];
   candidate_blocks[j] = candidate_blocks[n_candidates-1];
   n_candidates -= 1;
   }
   else
   j += 1;
   }
   }
   }

   if (n_candidates == 0)
   error ("No definition found for %s", 
   ada_demangle (exp->elts[pc + 2].name));
   else if (n_candidates == 1)
   i = 0;
   else if (deprocedure_p 
   && ! is_nonfunction (candidate_syms, n_candidates))
   {
   i = ada_resolve_function (candidate_syms, candidate_blocks,
   n_candidates, NULL, 0,
   exp->elts[pc + 2].name, context_type);
   if (i < 0) 
   error ("Could not find a match for %s", 
   ada_demangle (exp->elts[pc + 2].name));
   }
   else 
   {
   printf_filtered ("Multiple matches for %s\n", 
   ada_demangle (exp->elts[pc+2].name));
   user_select_syms (candidate_syms, candidate_blocks, 
   n_candidates, 1);
   i = 0;
   }

   exp->elts[pc].opcode = exp->elts[pc + 3].opcode = OP_VAR_VALUE;
   exp->elts[pc + 1].block = candidate_blocks[i];
   exp->elts[pc + 2].symbol = candidate_syms[i];
   if (innermost_block == NULL ||
   contained_in (candidate_blocks[i], innermost_block))
   innermost_block = candidate_blocks[i];
   } */
      /* FALL THROUGH */

    case OP_VAR_VALUE:
      if (deprocedure_p &&
	  TYPE_CODE (SYMBOL_TYPE (exp->elts[pc + 2].symbol)) ==
	  TYPE_CODE_FUNC)
	{
	  replace_operator_with_call (expp, pc, 0, 0,
				      exp->elts[pc + 2].symbol,
				      exp->elts[pc + 1].block);
	  exp = *expp;
	}
      break;

    case OP_FUNCALL:
      {
	/* FIXME:  OP_UNRESOLVED_VALUE should be defined in expression.h */
	/*      if (exp->elts[pc+3].opcode == OP_UNRESOLVED_VALUE)      
	   {
	   struct symbol** candidate_syms;
	   struct block** candidate_blocks;
	   int n_candidates;

	   n_candidates = ada_lookup_symbol_list (exp->elts[pc + 5].name,
	   exp->elts[pc + 4].block,
	   VAR_DOMAIN,
	   &candidate_syms,
	   &candidate_blocks);
	   if (n_candidates == 1)
	   i = 0;
	   else
	   {
	   i = ada_resolve_function (candidate_syms, candidate_blocks,
	   n_candidates, argvec, nargs-1,
	   exp->elts[pc + 5].name, context_type);
	   if (i < 0) 
	   error ("Could not find a match for %s", 
	   ada_demangle (exp->elts[pc + 5].name));
	   }

	   exp->elts[pc + 3].opcode = exp->elts[pc + 6].opcode = OP_VAR_VALUE;
	   exp->elts[pc + 4].block = candidate_blocks[i];
	   exp->elts[pc + 5].symbol = candidate_syms[i];
	   if (innermost_block == NULL ||
	   contained_in (candidate_blocks[i], innermost_block))
	   innermost_block = candidate_blocks[i];
	   } */

      }
      break;
    case BINOP_ADD:
    case BINOP_SUB:
    case BINOP_MUL:
    case BINOP_DIV:
    case BINOP_REM:
    case BINOP_MOD:
    case BINOP_CONCAT:
    case BINOP_BITWISE_AND:
    case BINOP_BITWISE_IOR:
    case BINOP_BITWISE_XOR:
    case BINOP_EQUAL:
    case BINOP_NOTEQUAL:
    case BINOP_LESS:
    case BINOP_GTR:
    case BINOP_LEQ:
    case BINOP_GEQ:
    case BINOP_EXP:
    case UNOP_NEG:
    case UNOP_PLUS:
    case UNOP_LOGICAL_NOT:
    case UNOP_ABS:
      if (possible_user_operator_p (op, argvec))
	{
	  struct symbol **candidate_syms;
	  struct block **candidate_blocks;
	  int n_candidates;

	  n_candidates =
	    ada_lookup_symbol_list (ada_mangle (ada_op_name (op)),
				    (struct block *) NULL, VAR_DOMAIN,
				    &candidate_syms, &candidate_blocks);
	  i =
	    ada_resolve_function (candidate_syms, candidate_blocks,
				  n_candidates, argvec, nargs,
				  ada_op_name (op), NULL);
	  if (i < 0)
	    break;

	  replace_operator_with_call (expp, pc, nargs, 1,
				      candidate_syms[i], candidate_blocks[i]);
	  exp = *expp;
	}
      break;
    }

  *pos = pc;
  return evaluate_subexp_type (exp, pos);
}

/* Return non-zero if formal type FTYPE matches actual type ATYPE.  If
   MAY_DEREF is non-zero, the formal may be a pointer and the actual 
   a non-pointer. */
/* The term "match" here is rather loose.  The match is heuristic and
   liberal.  FIXME: TOO liberal, in fact. */

static int
ada_type_match (struct type *ftype, struct type *atype, int may_deref)
{
  CHECK_TYPEDEF (ftype);
  CHECK_TYPEDEF (atype);

  if (TYPE_CODE (ftype) == TYPE_CODE_REF)
    ftype = TYPE_TARGET_TYPE (ftype);
  if (TYPE_CODE (atype) == TYPE_CODE_REF)
    atype = TYPE_TARGET_TYPE (atype);

  if (TYPE_CODE (ftype) == TYPE_CODE_VOID
      || TYPE_CODE (atype) == TYPE_CODE_VOID)
    return 1;

  switch (TYPE_CODE (ftype))
    {
    default:
      return 1;
    case TYPE_CODE_PTR:
      if (TYPE_CODE (atype) == TYPE_CODE_PTR)
	return ada_type_match (TYPE_TARGET_TYPE (ftype),
			       TYPE_TARGET_TYPE (atype), 0);
      else
	return (may_deref &&
		ada_type_match (TYPE_TARGET_TYPE (ftype), atype, 0));
    case TYPE_CODE_INT:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_RANGE:
      switch (TYPE_CODE (atype))
	{
	case TYPE_CODE_INT:
	case TYPE_CODE_ENUM:
	case TYPE_CODE_RANGE:
	  return 1;
	default:
	  return 0;
	}

    case TYPE_CODE_ARRAY:
      return (TYPE_CODE (atype) == TYPE_CODE_ARRAY
	      || ada_is_array_descriptor (atype));

    case TYPE_CODE_STRUCT:
      if (ada_is_array_descriptor (ftype))
	return (TYPE_CODE (atype) == TYPE_CODE_ARRAY
		|| ada_is_array_descriptor (atype));
      else
	return (TYPE_CODE (atype) == TYPE_CODE_STRUCT
		&& !ada_is_array_descriptor (atype));

    case TYPE_CODE_UNION:
    case TYPE_CODE_FLT:
      return (TYPE_CODE (atype) == TYPE_CODE (ftype));
    }
}

/* Return non-zero if the formals of FUNC "sufficiently match" the
   vector of actual argument types ACTUALS of size N_ACTUALS.  FUNC
   may also be an enumeral, in which case it is treated as a 0-
   argument function. */

static int
ada_args_match (struct symbol *func, struct value **actuals, int n_actuals)
{
  int i;
  struct type *func_type = SYMBOL_TYPE (func);

  if (SYMBOL_CLASS (func) == LOC_CONST &&
      TYPE_CODE (func_type) == TYPE_CODE_ENUM)
    return (n_actuals == 0);
  else if (func_type == NULL || TYPE_CODE (func_type) != TYPE_CODE_FUNC)
    return 0;

  if (TYPE_NFIELDS (func_type) != n_actuals)
    return 0;

  for (i = 0; i < n_actuals; i += 1)
    {
      struct type *ftype = check_typedef (TYPE_FIELD_TYPE (func_type, i));
      struct type *atype = check_typedef (VALUE_TYPE (actuals[i]));

      if (!ada_type_match (TYPE_FIELD_TYPE (func_type, i),
			   VALUE_TYPE (actuals[i]), 1))
	return 0;
    }
  return 1;
}

/* False iff function type FUNC_TYPE definitely does not produce a value
   compatible with type CONTEXT_TYPE.  Conservatively returns 1 if
   FUNC_TYPE is not a valid function type with a non-null return type
   or an enumerated type.  A null CONTEXT_TYPE indicates any non-void type.  */

static int
return_match (struct type *func_type, struct type *context_type)
{
  struct type *return_type;

  if (func_type == NULL)
    return 1;

  /* FIXME: base_type should be declared in gdbtypes.h, implemented in valarith.c */
  /*  if (TYPE_CODE (func_type) == TYPE_CODE_FUNC)
     return_type = base_type (TYPE_TARGET_TYPE (func_type));
     else 
     return_type = base_type (func_type); */
  if (return_type == NULL)
    return 1;

  /* FIXME: base_type should be declared in gdbtypes.h, implemented in valarith.c */
  /*  context_type = base_type (context_type); */

  if (TYPE_CODE (return_type) == TYPE_CODE_ENUM)
    return context_type == NULL || return_type == context_type;
  else if (context_type == NULL)
    return TYPE_CODE (return_type) != TYPE_CODE_VOID;
  else
    return TYPE_CODE (return_type) == TYPE_CODE (context_type);
}


/* Return the index in SYMS[0..NSYMS-1] of symbol for the 
   function (if any) that matches the types of the NARGS arguments in
   ARGS.  If CONTEXT_TYPE is non-null, and there is at least one match
   that returns type CONTEXT_TYPE, then eliminate other matches.  If
   CONTEXT_TYPE is null, prefer a non-void-returning function.
   Asks the user if there is more than one match remaining.  Returns -1
   if there is no such symbol or none is selected.  NAME is used
   solely for messages.   May re-arrange and modify SYMS in
   the process; the index returned is for the modified vector.  BLOCKS
   is modified in parallel to SYMS. */

int
ada_resolve_function (struct symbol *syms[], struct block *blocks[],
		      int nsyms, struct value **args, int nargs,
		      const char *name, struct type *context_type)
{
  int k;
  int m;			/* Number of hits */
  struct type *fallback;
  struct type *return_type;

  return_type = context_type;
  if (context_type == NULL)
    fallback = builtin_type_void;
  else
    fallback = NULL;

  m = 0;
  while (1)
    {
      for (k = 0; k < nsyms; k += 1)
	{
	  struct type *type = check_typedef (SYMBOL_TYPE (syms[k]));

	  if (ada_args_match (syms[k], args, nargs)
	      && return_match (SYMBOL_TYPE (syms[k]), return_type))
	    {
	      syms[m] = syms[k];
	      if (blocks != NULL)
		blocks[m] = blocks[k];
	      m += 1;
	    }
	}
      if (m > 0 || return_type == fallback)
	break;
      else
	return_type = fallback;
    }

  if (m == 0)
    return -1;
  else if (m > 1)
    {
      printf_filtered ("Multiple matches for %s\n", name);
      user_select_syms (syms, blocks, m, 1);
      return 0;
    }
  return 0;
}

/* Returns true (non-zero) iff demangled name N0 should appear before N1 */
/* in a listing of choices during disambiguation (see sort_choices, below). */
/* The idea is that overloadings of a subprogram name from the */
/* same package should sort in their source order.  We settle for ordering */
/* such symbols by their trailing number (__N  or $N). */
static int
mangled_ordered_before (char *N0, char *N1)
{
  if (N1 == NULL)
    return 0;
  else if (N0 == NULL)
    return 1;
  else
    {
      int k0, k1;
      for (k0 = strlen (N0) - 1; k0 > 0 && isdigit (N0[k0]); k0 -= 1)
	;
      for (k1 = strlen (N1) - 1; k1 > 0 && isdigit (N1[k1]); k1 -= 1)
	;
      if ((N0[k0] == '_' || N0[k0] == '$') && N0[k0 + 1] != '\000'
	  && (N1[k1] == '_' || N1[k1] == '$') && N1[k1 + 1] != '\000')
	{
	  int n0, n1;
	  n0 = k0;
	  while (N0[n0] == '_' && n0 > 0 && N0[n0 - 1] == '_')
	    n0 -= 1;
	  n1 = k1;
	  while (N1[n1] == '_' && n1 > 0 && N1[n1 - 1] == '_')
	    n1 -= 1;
	  if (n0 == n1 && DEPRECATED_STREQN (N0, N1, n0))
	    return (atoi (N0 + k0 + 1) < atoi (N1 + k1 + 1));
	}
      return (strcmp (N0, N1) < 0);
    }
}

/* Sort SYMS[0..NSYMS-1] to put the choices in a canonical order by their */
/* mangled names, rearranging BLOCKS[0..NSYMS-1] according to the same */
/* permutation. */
static void
sort_choices (struct symbol *syms[], struct block *blocks[], int nsyms)
{
  int i, j;
  for (i = 1; i < nsyms; i += 1)
    {
      struct symbol *sym = syms[i];
      struct block *block = blocks[i];
      int j;

      for (j = i - 1; j >= 0; j -= 1)
	{
	  if (mangled_ordered_before (DEPRECATED_SYMBOL_NAME (syms[j]),
				      DEPRECATED_SYMBOL_NAME (sym)))
	    break;
	  syms[j + 1] = syms[j];
	  blocks[j + 1] = blocks[j];
	}
      syms[j + 1] = sym;
      blocks[j + 1] = block;
    }
}

/* Given a list of NSYMS symbols in SYMS and corresponding blocks in */
/* BLOCKS, select up to MAX_RESULTS>0 by asking the user (if */
/* necessary), returning the number selected, and setting the first */
/* elements of SYMS and BLOCKS to the selected symbols and */
/* corresponding blocks.  Error if no symbols selected.   BLOCKS may */
/* be NULL, in which case it is ignored. */

/* NOTE: Adapted from decode_line_2 in symtab.c, with which it ought
   to be re-integrated one of these days. */

int
user_select_syms (struct symbol *syms[], struct block *blocks[], int nsyms,
		  int max_results)
{
  int i;
  int *chosen = (int *) alloca (sizeof (int) * nsyms);
  int n_chosen;
  int first_choice = (max_results == 1) ? 1 : 2;

  if (max_results < 1)
    error ("Request to select 0 symbols!");
  if (nsyms <= 1)
    return nsyms;

  printf_unfiltered ("[0] cancel\n");
  if (max_results > 1)
    printf_unfiltered ("[1] all\n");

  sort_choices (syms, blocks, nsyms);

  for (i = 0; i < nsyms; i += 1)
    {
      if (syms[i] == NULL)
	continue;

      if (SYMBOL_CLASS (syms[i]) == LOC_BLOCK)
	{
	  struct symtab_and_line sal = find_function_start_sal (syms[i], 1);
	  printf_unfiltered ("[%d] %s at %s:%d\n",
			     i + first_choice,
			     SYMBOL_PRINT_NAME (syms[i]),
			     sal.symtab == NULL
			     ? "<no source file available>"
			     : sal.symtab->filename, sal.line);
	  continue;
	}
      else
	{
	  int is_enumeral =
	    (SYMBOL_CLASS (syms[i]) == LOC_CONST
	     && SYMBOL_TYPE (syms[i]) != NULL
	     && TYPE_CODE (SYMBOL_TYPE (syms[i])) == TYPE_CODE_ENUM);
	  struct symtab *symtab = symtab_for_sym (syms[i]);

	  if (SYMBOL_LINE (syms[i]) != 0 && symtab != NULL)
	    printf_unfiltered ("[%d] %s at %s:%d\n",
			       i + first_choice,
			       SYMBOL_PRINT_NAME (syms[i]),
			       symtab->filename, SYMBOL_LINE (syms[i]));
	  else if (is_enumeral && TYPE_NAME (SYMBOL_TYPE (syms[i])) != NULL)
	    {
	      printf_unfiltered ("[%d] ", i + first_choice);
	      ada_print_type (SYMBOL_TYPE (syms[i]), NULL, gdb_stdout, -1, 0);
	      printf_unfiltered ("'(%s) (enumeral)\n",
				 SYMBOL_PRINT_NAME (syms[i]));
	    }
	  else if (symtab != NULL)
	    printf_unfiltered (is_enumeral
			       ? "[%d] %s in %s (enumeral)\n"
			       : "[%d] %s at %s:?\n",
			       i + first_choice,
			       SYMBOL_PRINT_NAME (syms[i]),
			       symtab->filename);
	  else
	    printf_unfiltered (is_enumeral
			       ? "[%d] %s (enumeral)\n"
			       : "[%d] %s at ?\n",
			       i + first_choice,
			       SYMBOL_PRINT_NAME (syms[i]));
	}
    }

  n_chosen = get_selections (chosen, nsyms, max_results, max_results > 1,
			     "overload-choice");

  for (i = 0; i < n_chosen; i += 1)
    {
      syms[i] = syms[chosen[i]];
      if (blocks != NULL)
	blocks[i] = blocks[chosen[i]];
    }

  return n_chosen;
}

/* Read and validate a set of numeric choices from the user in the
   range 0 .. N_CHOICES-1. Place the results in increasing
   order in CHOICES[0 .. N-1], and return N.

   The user types choices as a sequence of numbers on one line
   separated by blanks, encoding them as follows:

     + A choice of 0 means to cancel the selection, throwing an error.  
     + If IS_ALL_CHOICE, a choice of 1 selects the entire set 0 .. N_CHOICES-1.
     + The user chooses k by typing k+IS_ALL_CHOICE+1.

   The user is not allowed to choose more than MAX_RESULTS values. 

   ANNOTATION_SUFFIX, if present, is used to annotate the input
   prompts (for use with the -f switch). */

int
get_selections (int *choices, int n_choices, int max_results,
		int is_all_choice, char *annotation_suffix)
{
  int i;
  char *args;
  const char *prompt;
  int n_chosen;
  int first_choice = is_all_choice ? 2 : 1;

  prompt = getenv ("PS2");
  if (prompt == NULL)
    prompt = ">";

  printf_unfiltered ("%s ", prompt);
  gdb_flush (gdb_stdout);

  args = command_line_input ((char *) NULL, 0, annotation_suffix);

  if (args == NULL)
    error_no_arg ("one or more choice numbers");

  n_chosen = 0;

  /* Set choices[0 .. n_chosen-1] to the users' choices in ascending 
     order, as given in args.   Choices are validated. */
  while (1)
    {
      char *args2;
      int choice, j;

      while (isspace (*args))
	args += 1;
      if (*args == '\0' && n_chosen == 0)
	error_no_arg ("one or more choice numbers");
      else if (*args == '\0')
	break;

      choice = strtol (args, &args2, 10);
      if (args == args2 || choice < 0
	  || choice > n_choices + first_choice - 1)
	error ("Argument must be choice number");
      args = args2;

      if (choice == 0)
	error ("cancelled");

      if (choice < first_choice)
	{
	  n_chosen = n_choices;
	  for (j = 0; j < n_choices; j += 1)
	    choices[j] = j;
	  break;
	}
      choice -= first_choice;

      for (j = n_chosen - 1; j >= 0 && choice < choices[j]; j -= 1)
	{
	}

      if (j < 0 || choice != choices[j])
	{
	  int k;
	  for (k = n_chosen - 1; k > j; k -= 1)
	    choices[k + 1] = choices[k];
	  choices[j + 1] = choice;
	  n_chosen += 1;
	}
    }

  if (n_chosen > max_results)
    error ("Select no more than %d of the above", max_results);

  return n_chosen;
}

/* Replace the operator of length OPLEN at position PC in *EXPP with a call */
/* on the function identified by SYM and BLOCK, and taking NARGS */
/* arguments.  Update *EXPP as needed to hold more space. */

static void
replace_operator_with_call (struct expression **expp, int pc, int nargs,
			    int oplen, struct symbol *sym,
			    struct block *block)
{
  /* A new expression, with 6 more elements (3 for funcall, 4 for function
     symbol, -oplen for operator being replaced). */
  struct expression *newexp = (struct expression *)
    xmalloc (sizeof (struct expression)
	     + EXP_ELEM_TO_BYTES ((*expp)->nelts + 7 - oplen));
  struct expression *exp = *expp;

  newexp->nelts = exp->nelts + 7 - oplen;
  newexp->language_defn = exp->language_defn;
  memcpy (newexp->elts, exp->elts, EXP_ELEM_TO_BYTES (pc));
  memcpy (newexp->elts + pc + 7, exp->elts + pc + oplen,
	  EXP_ELEM_TO_BYTES (exp->nelts - pc - oplen));

  newexp->elts[pc].opcode = newexp->elts[pc + 2].opcode = OP_FUNCALL;
  newexp->elts[pc + 1].longconst = (LONGEST) nargs;

  newexp->elts[pc + 3].opcode = newexp->elts[pc + 6].opcode = OP_VAR_VALUE;
  newexp->elts[pc + 4].block = block;
  newexp->elts[pc + 5].symbol = sym;

  *expp = newexp;
  xfree (exp);
}

/* Type-class predicates */

/* True iff TYPE is numeric (i.e., an INT, RANGE (of numeric type), or */
/* FLOAT.) */

static int
numeric_type_p (struct type *type)
{
  if (type == NULL)
    return 0;
  else
    {
      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_INT:
	case TYPE_CODE_FLT:
	  return 1;
	case TYPE_CODE_RANGE:
	  return (type == TYPE_TARGET_TYPE (type)
		  || numeric_type_p (TYPE_TARGET_TYPE (type)));
	default:
	  return 0;
	}
    }
}

/* True iff TYPE is integral (an INT or RANGE of INTs). */

static int
integer_type_p (struct type *type)
{
  if (type == NULL)
    return 0;
  else
    {
      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_INT:
	  return 1;
	case TYPE_CODE_RANGE:
	  return (type == TYPE_TARGET_TYPE (type)
		  || integer_type_p (TYPE_TARGET_TYPE (type)));
	default:
	  return 0;
	}
    }
}

/* True iff TYPE is scalar (INT, RANGE, FLOAT, ENUM). */

static int
scalar_type_p (struct type *type)
{
  if (type == NULL)
    return 0;
  else
    {
      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_INT:
	case TYPE_CODE_RANGE:
	case TYPE_CODE_ENUM:
	case TYPE_CODE_FLT:
	  return 1;
	default:
	  return 0;
	}
    }
}

/* True iff TYPE is discrete (INT, RANGE, ENUM). */

static int
discrete_type_p (struct type *type)
{
  if (type == NULL)
    return 0;
  else
    {
      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_INT:
	case TYPE_CODE_RANGE:
	case TYPE_CODE_ENUM:
	  return 1;
	default:
	  return 0;
	}
    }
}

/* Returns non-zero if OP with operatands in the vector ARGS could be
   a user-defined function. Errs on the side of pre-defined operators
   (i.e., result 0). */

static int
possible_user_operator_p (enum exp_opcode op, struct value *args[])
{
  struct type *type0 = check_typedef (VALUE_TYPE (args[0]));
  struct type *type1 =
    (args[1] == NULL) ? NULL : check_typedef (VALUE_TYPE (args[1]));

  switch (op)
    {
    default:
      return 0;

    case BINOP_ADD:
    case BINOP_SUB:
    case BINOP_MUL:
    case BINOP_DIV:
      return (!(numeric_type_p (type0) && numeric_type_p (type1)));

    case BINOP_REM:
    case BINOP_MOD:
    case BINOP_BITWISE_AND:
    case BINOP_BITWISE_IOR:
    case BINOP_BITWISE_XOR:
      return (!(integer_type_p (type0) && integer_type_p (type1)));

    case BINOP_EQUAL:
    case BINOP_NOTEQUAL:
    case BINOP_LESS:
    case BINOP_GTR:
    case BINOP_LEQ:
    case BINOP_GEQ:
      return (!(scalar_type_p (type0) && scalar_type_p (type1)));

    case BINOP_CONCAT:
      return ((TYPE_CODE (type0) != TYPE_CODE_ARRAY &&
	       (TYPE_CODE (type0) != TYPE_CODE_PTR ||
		TYPE_CODE (TYPE_TARGET_TYPE (type0))
		!= TYPE_CODE_ARRAY))
	      || (TYPE_CODE (type1) != TYPE_CODE_ARRAY &&
		  (TYPE_CODE (type1) != TYPE_CODE_PTR ||
		   TYPE_CODE (TYPE_TARGET_TYPE (type1)) != TYPE_CODE_ARRAY)));

    case BINOP_EXP:
      return (!(numeric_type_p (type0) && integer_type_p (type1)));

    case UNOP_NEG:
    case UNOP_PLUS:
    case UNOP_LOGICAL_NOT:
    case UNOP_ABS:
      return (!numeric_type_p (type0));

    }
}

				/* Renaming */

/** NOTE: In the following, we assume that a renaming type's name may
 *  have an ___XD suffix.  It would be nice if this went away at some
 *  point. */

/* If TYPE encodes a renaming, returns the renaming suffix, which
 * is XR for an object renaming, XRP for a procedure renaming, XRE for
 * an exception renaming, and XRS for a subprogram renaming.  Returns
 * NULL if NAME encodes none of these. */
const char *
ada_renaming_type (struct type *type)
{
  if (type != NULL && TYPE_CODE (type) == TYPE_CODE_ENUM)
    {
      const char *name = type_name_no_tag (type);
      const char *suffix = (name == NULL) ? NULL : strstr (name, "___XR");
      if (suffix == NULL
	  || (suffix[5] != '\000' && strchr ("PES_", suffix[5]) == NULL))
	return NULL;
      else
	return suffix + 3;
    }
  else
    return NULL;
}

/* Return non-zero iff SYM encodes an object renaming. */
int
ada_is_object_renaming (struct symbol *sym)
{
  const char *renaming_type = ada_renaming_type (SYMBOL_TYPE (sym));
  return renaming_type != NULL
    && (renaming_type[2] == '\0' || renaming_type[2] == '_');
}

/* Assuming that SYM encodes a non-object renaming, returns the original
 * name of the renamed entity.   The name is good until the end of
 * parsing. */
const char *
ada_simple_renamed_entity (struct symbol *sym)
{
  struct type *type;
  const char *raw_name;
  int len;
  char *result;

  type = SYMBOL_TYPE (sym);
  if (type == NULL || TYPE_NFIELDS (type) < 1)
    error ("Improperly encoded renaming.");

  raw_name = TYPE_FIELD_NAME (type, 0);
  len = (raw_name == NULL ? 0 : strlen (raw_name)) - 5;
  if (len <= 0)
    error ("Improperly encoded renaming.");

  result = xmalloc (len + 1);
  /* FIXME: add_name_string_cleanup should be defined in parse.c */
  /*  add_name_string_cleanup (result); */
  strncpy (result, raw_name, len);
  result[len] = '\000';
  return result;
}


				/* Evaluation: Function Calls */

/* Copy VAL onto the stack, using and updating *SP as the stack 
   pointer. Return VAL as an lvalue. */

static struct value *
place_on_stack (struct value *val, CORE_ADDR *sp)
{
  CORE_ADDR old_sp = *sp;

#ifdef DEPRECATED_STACK_ALIGN
  *sp = push_bytes (*sp, VALUE_CONTENTS_RAW (val),
		    DEPRECATED_STACK_ALIGN (TYPE_LENGTH
				 (check_typedef (VALUE_TYPE (val)))));
#else
  *sp = push_bytes (*sp, VALUE_CONTENTS_RAW (val),
		    TYPE_LENGTH (check_typedef (VALUE_TYPE (val))));
#endif

  VALUE_LVAL (val) = lval_memory;
  if (INNER_THAN (1, 2))
    VALUE_ADDRESS (val) = *sp;
  else
    VALUE_ADDRESS (val) = old_sp;

  return val;
}

/* Return the value ACTUAL, converted to be an appropriate value for a
   formal of type FORMAL_TYPE.  Use *SP as a stack pointer for
   allocating any necessary descriptors (fat pointers), or copies of
   values not residing in memory, updating it as needed. */

static struct value *
convert_actual (struct value *actual, struct type *formal_type0,
		CORE_ADDR *sp)
{
  struct type *actual_type = check_typedef (VALUE_TYPE (actual));
  struct type *formal_type = check_typedef (formal_type0);
  struct type *formal_target =
    TYPE_CODE (formal_type) == TYPE_CODE_PTR
    ? check_typedef (TYPE_TARGET_TYPE (formal_type)) : formal_type;
  struct type *actual_target =
    TYPE_CODE (actual_type) == TYPE_CODE_PTR
    ? check_typedef (TYPE_TARGET_TYPE (actual_type)) : actual_type;

  if (ada_is_array_descriptor (formal_target)
      && TYPE_CODE (actual_target) == TYPE_CODE_ARRAY)
    return make_array_descriptor (formal_type, actual, sp);
  else if (TYPE_CODE (formal_type) == TYPE_CODE_PTR)
    {
      if (TYPE_CODE (formal_target) == TYPE_CODE_ARRAY
	  && ada_is_array_descriptor (actual_target))
	return desc_data (actual);
      else if (TYPE_CODE (actual_type) != TYPE_CODE_PTR)
	{
	  if (VALUE_LVAL (actual) != lval_memory)
	    {
	      struct value *val;
	      actual_type = check_typedef (VALUE_TYPE (actual));
	      val = allocate_value (actual_type);
	      memcpy ((char *) VALUE_CONTENTS_RAW (val),
		      (char *) VALUE_CONTENTS (actual),
		      TYPE_LENGTH (actual_type));
	      actual = place_on_stack (val, sp);
	    }
	  return value_addr (actual);
	}
    }
  else if (TYPE_CODE (actual_type) == TYPE_CODE_PTR)
    return ada_value_ind (actual);

  return actual;
}


/* Push a descriptor of type TYPE for array value ARR on the stack at 
   *SP, updating *SP to reflect the new descriptor.  Return either 
   an lvalue representing the new descriptor, or (if TYPE is a pointer-
   to-descriptor type rather than a descriptor type), a struct value*
   representing a pointer to this descriptor. */

static struct value *
make_array_descriptor (struct type *type, struct value *arr, CORE_ADDR *sp)
{
  struct type *bounds_type = desc_bounds_type (type);
  struct type *desc_type = desc_base_type (type);
  struct value *descriptor = allocate_value (desc_type);
  struct value *bounds = allocate_value (bounds_type);
  CORE_ADDR bounds_addr;
  int i;

  for (i = ada_array_arity (check_typedef (VALUE_TYPE (arr))); i > 0; i -= 1)
    {
      modify_general_field (VALUE_CONTENTS (bounds),
			    value_as_long (ada_array_bound (arr, i, 0)),
			    desc_bound_bitpos (bounds_type, i, 0),
			    desc_bound_bitsize (bounds_type, i, 0));
      modify_general_field (VALUE_CONTENTS (bounds),
			    value_as_long (ada_array_bound (arr, i, 1)),
			    desc_bound_bitpos (bounds_type, i, 1),
			    desc_bound_bitsize (bounds_type, i, 1));
    }

  bounds = place_on_stack (bounds, sp);

  modify_general_field (VALUE_CONTENTS (descriptor),
			arr,
			fat_pntr_data_bitpos (desc_type),
			fat_pntr_data_bitsize (desc_type));
  modify_general_field (VALUE_CONTENTS (descriptor),
			VALUE_ADDRESS (bounds),
			fat_pntr_bounds_bitpos (desc_type),
			fat_pntr_bounds_bitsize (desc_type));

  descriptor = place_on_stack (descriptor, sp);

  if (TYPE_CODE (type) == TYPE_CODE_PTR)
    return value_addr (descriptor);
  else
    return descriptor;
}


/* Assuming a dummy frame has been established on the target, perform any 
   conversions needed for calling function FUNC on the NARGS actual
   parameters in ARGS, other than standard C conversions.   Does
   nothing if FUNC does not have Ada-style prototype data, or if NARGS
   does not match the number of arguments expected.   Use *SP as a
   stack pointer for additional data that must be pushed, updating its
   value as needed. */

void
ada_convert_actuals (struct value *func, int nargs, struct value *args[],
		     CORE_ADDR *sp)
{
  int i;

  if (TYPE_NFIELDS (VALUE_TYPE (func)) == 0
      || nargs != TYPE_NFIELDS (VALUE_TYPE (func)))
    return;

  for (i = 0; i < nargs; i += 1)
    args[i] =
      convert_actual (args[i], TYPE_FIELD_TYPE (VALUE_TYPE (func), i), sp);
}


				/* Symbol Lookup */


/* The vectors of symbols and blocks ultimately returned from */
/* ada_lookup_symbol_list. */

/* Current size of defn_symbols and defn_blocks */
static size_t defn_vector_size = 0;

/* Current number of symbols found. */
static int ndefns = 0;

static struct symbol **defn_symbols = NULL;
static struct block **defn_blocks = NULL;

/* Return the result of a standard (literal, C-like) lookup of NAME in 
 * given DOMAIN. */

static struct symbol *
standard_lookup (const char *name, domain_enum domain)
{
  struct symbol *sym;
  sym = lookup_symbol (name, (struct block *) NULL, domain, 0, NULL);
  return sym;
}


/* Non-zero iff there is at least one non-function/non-enumeral symbol */
/* in SYMS[0..N-1].  We treat enumerals as functions, since they */
/* contend in overloading in the same way. */
static int
is_nonfunction (struct symbol *syms[], int n)
{
  int i;

  for (i = 0; i < n; i += 1)
    if (TYPE_CODE (SYMBOL_TYPE (syms[i])) != TYPE_CODE_FUNC
	&& TYPE_CODE (SYMBOL_TYPE (syms[i])) != TYPE_CODE_ENUM)
      return 1;

  return 0;
}

/* If true (non-zero), then TYPE0 and TYPE1 represent equivalent
   struct types.  Otherwise, they may not. */

static int
equiv_types (struct type *type0, struct type *type1)
{
  if (type0 == type1)
    return 1;
  if (type0 == NULL || type1 == NULL
      || TYPE_CODE (type0) != TYPE_CODE (type1))
    return 0;
  if ((TYPE_CODE (type0) == TYPE_CODE_STRUCT
       || TYPE_CODE (type0) == TYPE_CODE_ENUM)
      && ada_type_name (type0) != NULL && ada_type_name (type1) != NULL
      && DEPRECATED_STREQ (ada_type_name (type0), ada_type_name (type1)))
    return 1;

  return 0;
}

/* True iff SYM0 represents the same entity as SYM1, or one that is
   no more defined than that of SYM1. */

static int
lesseq_defined_than (struct symbol *sym0, struct symbol *sym1)
{
  if (sym0 == sym1)
    return 1;
  if (SYMBOL_DOMAIN (sym0) != SYMBOL_DOMAIN (sym1)
      || SYMBOL_CLASS (sym0) != SYMBOL_CLASS (sym1))
    return 0;

  switch (SYMBOL_CLASS (sym0))
    {
    case LOC_UNDEF:
      return 1;
    case LOC_TYPEDEF:
      {
	struct type *type0 = SYMBOL_TYPE (sym0);
	struct type *type1 = SYMBOL_TYPE (sym1);
	char *name0 = DEPRECATED_SYMBOL_NAME (sym0);
	char *name1 = DEPRECATED_SYMBOL_NAME (sym1);
	int len0 = strlen (name0);
	return
	  TYPE_CODE (type0) == TYPE_CODE (type1)
	  && (equiv_types (type0, type1)
	      || (len0 < strlen (name1) && DEPRECATED_STREQN (name0, name1, len0)
		  && DEPRECATED_STREQN (name1 + len0, "___XV", 5)));
      }
    case LOC_CONST:
      return SYMBOL_VALUE (sym0) == SYMBOL_VALUE (sym1)
	&& equiv_types (SYMBOL_TYPE (sym0), SYMBOL_TYPE (sym1));
    default:
      return 0;
    }
}

/* Append SYM to the end of defn_symbols, and BLOCK to the end of
   defn_blocks, updating ndefns, and expanding defn_symbols and
   defn_blocks as needed.   Do not include SYM if it is a duplicate.  */

static void
add_defn_to_vec (struct symbol *sym, struct block *block)
{
  int i;
  size_t tmp;

  if (SYMBOL_TYPE (sym) != NULL)
    CHECK_TYPEDEF (SYMBOL_TYPE (sym));
  for (i = 0; i < ndefns; i += 1)
    {
      if (lesseq_defined_than (sym, defn_symbols[i]))
	return;
      else if (lesseq_defined_than (defn_symbols[i], sym))
	{
	  defn_symbols[i] = sym;
	  defn_blocks[i] = block;
	  return;
	}
    }

  tmp = defn_vector_size;
  GROW_VECT (defn_symbols, tmp, ndefns + 2);
  GROW_VECT (defn_blocks, defn_vector_size, ndefns + 2);

  defn_symbols[ndefns] = sym;
  defn_blocks[ndefns] = block;
  ndefns += 1;
}

/* Look, in partial_symtab PST, for symbol NAME in given domain.
   Check the global symbols if GLOBAL, the static symbols if not.  Do
   wild-card match if WILD. */

static struct partial_symbol *
ada_lookup_partial_symbol (struct partial_symtab *pst, const char *name,
			   int global, domain_enum domain, int wild)
{
  struct partial_symbol **start;
  int name_len = strlen (name);
  int length = (global ? pst->n_global_syms : pst->n_static_syms);
  int i;

  if (length == 0)
    {
      return (NULL);
    }

  start = (global ?
	   pst->objfile->global_psymbols.list + pst->globals_offset :
	   pst->objfile->static_psymbols.list + pst->statics_offset);

  if (wild)
    {
      for (i = 0; i < length; i += 1)
	{
	  struct partial_symbol *psym = start[i];

	  if (SYMBOL_DOMAIN (psym) == domain &&
	      wild_match (name, name_len, DEPRECATED_SYMBOL_NAME (psym)))
	    return psym;
	}
      return NULL;
    }
  else
    {
      if (global)
	{
	  int U;
	  i = 0;
	  U = length - 1;
	  while (U - i > 4)
	    {
	      int M = (U + i) >> 1;
	      struct partial_symbol *psym = start[M];
	      if (DEPRECATED_SYMBOL_NAME (psym)[0] < name[0])
		i = M + 1;
	      else if (DEPRECATED_SYMBOL_NAME (psym)[0] > name[0])
		U = M - 1;
	      else if (strcmp (DEPRECATED_SYMBOL_NAME (psym), name) < 0)
		i = M + 1;
	      else
		U = M;
	    }
	}
      else
	i = 0;

      while (i < length)
	{
	  struct partial_symbol *psym = start[i];

	  if (SYMBOL_DOMAIN (psym) == domain)
	    {
	      int cmp = strncmp (name, DEPRECATED_SYMBOL_NAME (psym), name_len);

	      if (cmp < 0)
		{
		  if (global)
		    break;
		}
	      else if (cmp == 0
		       && is_name_suffix (DEPRECATED_SYMBOL_NAME (psym) + name_len))
		return psym;
	    }
	  i += 1;
	}

      if (global)
	{
	  int U;
	  i = 0;
	  U = length - 1;
	  while (U - i > 4)
	    {
	      int M = (U + i) >> 1;
	      struct partial_symbol *psym = start[M];
	      if (DEPRECATED_SYMBOL_NAME (psym)[0] < '_')
		i = M + 1;
	      else if (DEPRECATED_SYMBOL_NAME (psym)[0] > '_')
		U = M - 1;
	      else if (strcmp (DEPRECATED_SYMBOL_NAME (psym), "_ada_") < 0)
		i = M + 1;
	      else
		U = M;
	    }
	}
      else
	i = 0;

      while (i < length)
	{
	  struct partial_symbol *psym = start[i];

	  if (SYMBOL_DOMAIN (psym) == domain)
	    {
	      int cmp;

	      cmp = (int) '_' - (int) DEPRECATED_SYMBOL_NAME (psym)[0];
	      if (cmp == 0)
		{
		  cmp = strncmp ("_ada_", DEPRECATED_SYMBOL_NAME (psym), 5);
		  if (cmp == 0)
		    cmp = strncmp (name, DEPRECATED_SYMBOL_NAME (psym) + 5, name_len);
		}

	      if (cmp < 0)
		{
		  if (global)
		    break;
		}
	      else if (cmp == 0
		       && is_name_suffix (DEPRECATED_SYMBOL_NAME (psym) + name_len + 5))
		return psym;
	    }
	  i += 1;
	}

    }
  return NULL;
}


/* Find a symbol table containing symbol SYM or NULL if none.  */
static struct symtab *
symtab_for_sym (struct symbol *sym)
{
  struct symtab *s;
  struct objfile *objfile;
  struct block *b;
  struct symbol *tmp_sym;
  struct dict_iterator iter;
  int j;

  ALL_SYMTABS (objfile, s)
  {
    switch (SYMBOL_CLASS (sym))
      {
      case LOC_CONST:
      case LOC_STATIC:
      case LOC_TYPEDEF:
      case LOC_REGISTER:
      case LOC_LABEL:
      case LOC_BLOCK:
      case LOC_CONST_BYTES:
	b = BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), GLOBAL_BLOCK);
	ALL_BLOCK_SYMBOLS (b, iter, tmp_sym) if (sym == tmp_sym)
	  return s;
	b = BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), STATIC_BLOCK);
	ALL_BLOCK_SYMBOLS (b, iter, tmp_sym) if (sym == tmp_sym)
	  return s;
	break;
      default:
	break;
      }
    switch (SYMBOL_CLASS (sym))
      {
      case LOC_REGISTER:
      case LOC_ARG:
      case LOC_REF_ARG:
      case LOC_REGPARM:
      case LOC_REGPARM_ADDR:
      case LOC_LOCAL:
      case LOC_TYPEDEF:
      case LOC_LOCAL_ARG:
      case LOC_BASEREG:
      case LOC_BASEREG_ARG:
      case LOC_COMPUTED:
      case LOC_COMPUTED_ARG:
	for (j = FIRST_LOCAL_BLOCK;
	     j < BLOCKVECTOR_NBLOCKS (BLOCKVECTOR (s)); j += 1)
	  {
	    b = BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), j);
	    ALL_BLOCK_SYMBOLS (b, iter, tmp_sym) if (sym == tmp_sym)
	      return s;
	  }
	break;
      default:
	break;
      }
  }
  return NULL;
}

/* Return a minimal symbol matching NAME according to Ada demangling 
   rules. Returns NULL if there is no such minimal symbol. */

struct minimal_symbol *
ada_lookup_minimal_symbol (const char *name)
{
  struct objfile *objfile;
  struct minimal_symbol *msymbol;
  int wild_match = (strstr (name, "__") == NULL);

  ALL_MSYMBOLS (objfile, msymbol)
  {
    if (ada_match_name (DEPRECATED_SYMBOL_NAME (msymbol), name, wild_match)
	&& MSYMBOL_TYPE (msymbol) != mst_solib_trampoline)
      return msymbol;
  }

  return NULL;
}

/* For all subprograms that statically enclose the subprogram of the
 * selected frame, add symbols matching identifier NAME in DOMAIN
 * and their blocks to vectors *defn_symbols and *defn_blocks, as for
 * ada_add_block_symbols (q.v.).   If WILD, treat as NAME with a
 * wildcard prefix.  At the moment, this function uses a heuristic to
 * find the frames of enclosing subprograms: it treats the
 * pointer-sized value at location 0 from the local-variable base of a
 * frame as a static link, and then searches up the call stack for a
 * frame with that same local-variable base. */
static void
add_symbols_from_enclosing_procs (const char *name, domain_enum domain,
				  int wild_match)
{
#ifdef i386
  static struct symbol static_link_sym;
  static struct symbol *static_link;

  struct cleanup *old_chain = make_cleanup (null_cleanup, NULL);
  struct frame_info *frame;
  struct frame_info *target_frame;

  if (static_link == NULL)
    {
      /* Initialize the local variable symbol that stands for the
       * static link (when it exists). */
      static_link = &static_link_sym;
      DEPRECATED_SYMBOL_NAME (static_link) = "";
      SYMBOL_LANGUAGE (static_link) = language_unknown;
      SYMBOL_CLASS (static_link) = LOC_LOCAL;
      SYMBOL_DOMAIN (static_link) = VAR_DOMAIN;
      SYMBOL_TYPE (static_link) = lookup_pointer_type (builtin_type_void);
      SYMBOL_VALUE (static_link) =
	-(long) TYPE_LENGTH (SYMBOL_TYPE (static_link));
    }

  frame = deprecated_selected_frame;
  while (frame != NULL && ndefns == 0)
    {
      struct block *block;
      struct value *target_link_val = read_var_value (static_link, frame);
      CORE_ADDR target_link;

      if (target_link_val == NULL)
	break;
      QUIT;

      target_link = target_link_val;
      do
	{
	  QUIT;
	  frame = get_prev_frame (frame);
	}
      while (frame != NULL && DEPRECATED_FRAME_LOCALS_ADDRESS (frame) != target_link);

      if (frame == NULL)
	break;

      block = get_frame_block (frame, 0);
      while (block != NULL && block_function (block) != NULL && ndefns == 0)
	{
	  ada_add_block_symbols (block, name, domain, NULL, wild_match);

	  block = BLOCK_SUPERBLOCK (block);
	}
    }

  do_cleanups (old_chain);
#endif
}

/* True if TYPE is definitely an artificial type supplied to a symbol
 * for which no debugging information was given in the symbol file. */
static int
is_nondebugging_type (struct type *type)
{
  char *name = ada_type_name (type);
  return (name != NULL && DEPRECATED_STREQ (name, "<variable, no debug info>"));
}

/* Remove any non-debugging symbols in SYMS[0 .. NSYMS-1] that definitely 
 * duplicate other symbols in the list.  (The only case I know of where
 * this happens is when object files containing stabs-in-ecoff are
 * linked with files containing ordinary ecoff debugging symbols (or no
 * debugging symbols)). Modifies SYMS to squeeze out deleted symbols,
 * and applies the same modification to BLOCKS to maintain the
 * correspondence between SYMS[i] and BLOCKS[i].  Returns the number
 * of symbols in the modified list. */
static int
remove_extra_symbols (struct symbol **syms, struct block **blocks, int nsyms)
{
  int i, j;

  i = 0;
  while (i < nsyms)
    {
      if (DEPRECATED_SYMBOL_NAME (syms[i]) != NULL
	  && SYMBOL_CLASS (syms[i]) == LOC_STATIC
	  && is_nondebugging_type (SYMBOL_TYPE (syms[i])))
	{
	  for (j = 0; j < nsyms; j += 1)
	    {
	      if (i != j
		  && DEPRECATED_SYMBOL_NAME (syms[j]) != NULL
		  && DEPRECATED_STREQ (DEPRECATED_SYMBOL_NAME (syms[i]), DEPRECATED_SYMBOL_NAME (syms[j]))
		  && SYMBOL_CLASS (syms[i]) == SYMBOL_CLASS (syms[j])
		  && SYMBOL_VALUE_ADDRESS (syms[i])
		  == SYMBOL_VALUE_ADDRESS (syms[j]))
		{
		  int k;
		  for (k = i + 1; k < nsyms; k += 1)
		    {
		      syms[k - 1] = syms[k];
		      blocks[k - 1] = blocks[k];
		    }
		  nsyms -= 1;
		  goto NextSymbol;
		}
	    }
	}
      i += 1;
    NextSymbol:
      ;
    }
  return nsyms;
}

/* Find symbols in DOMAIN matching NAME, in BLOCK0 and enclosing 
   scope and in global scopes, returning the number of matches.  Sets 
   *SYMS to point to a vector of matching symbols, with *BLOCKS
   pointing to the vector of corresponding blocks in which those
   symbols reside.  These two vectors are transient---good only to the
   next call of ada_lookup_symbol_list.  Any non-function/non-enumeral symbol
   match within the nest of blocks whose innermost member is BLOCK0,
   is the outermost match returned (no other matches in that or
   enclosing blocks is returned).  If there are any matches in or
   surrounding BLOCK0, then these alone are returned. */

int
ada_lookup_symbol_list (const char *name, struct block *block0,
			domain_enum domain, struct symbol ***syms,
			struct block ***blocks)
{
  struct symbol *sym;
  struct symtab *s;
  struct partial_symtab *ps;
  struct blockvector *bv;
  struct objfile *objfile;
  struct block *b;
  struct block *block;
  struct minimal_symbol *msymbol;
  int wild_match = (strstr (name, "__") == NULL);
  int cacheIfUnique;

#ifdef TIMING
  markTimeStart (0);
#endif

  ndefns = 0;
  cacheIfUnique = 0;

  /* Search specified block and its superiors.  */

  block = block0;
  while (block != NULL)
    {
      ada_add_block_symbols (block, name, domain, NULL, wild_match);

      /* If we found a non-function match, assume that's the one. */
      if (is_nonfunction (defn_symbols, ndefns))
	goto done;

      block = BLOCK_SUPERBLOCK (block);
    }

  /* If we found ANY matches in the specified BLOCK, we're done. */

  if (ndefns > 0)
    goto done;

  cacheIfUnique = 1;

  /* Now add symbols from all global blocks: symbol tables, minimal symbol
     tables, and psymtab's */

  ALL_SYMTABS (objfile, s)
  {
    QUIT;
    if (!s->primary)
      continue;
    bv = BLOCKVECTOR (s);
    block = BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK);
    ada_add_block_symbols (block, name, domain, objfile, wild_match);
  }

  if (domain == VAR_DOMAIN)
    {
      ALL_MSYMBOLS (objfile, msymbol)
      {
	if (ada_match_name (DEPRECATED_SYMBOL_NAME (msymbol), name, wild_match))
	  {
	    switch (MSYMBOL_TYPE (msymbol))
	      {
	      case mst_solib_trampoline:
		break;
	      default:
		s = find_pc_symtab (SYMBOL_VALUE_ADDRESS (msymbol));
		if (s != NULL)
		  {
		    int old_ndefns = ndefns;
		    QUIT;
		    bv = BLOCKVECTOR (s);
		    block = BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK);
		    ada_add_block_symbols (block,
					   DEPRECATED_SYMBOL_NAME (msymbol),
					   domain, objfile, wild_match);
		    if (ndefns == old_ndefns)
		      {
			block = BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK);
			ada_add_block_symbols (block,
					       DEPRECATED_SYMBOL_NAME (msymbol),
					       domain, objfile,
					       wild_match);
		      }
		  }
	      }
	  }
      }
    }

  ALL_PSYMTABS (objfile, ps)
  {
    QUIT;
    if (!ps->readin
	&& ada_lookup_partial_symbol (ps, name, 1, domain, wild_match))
      {
	s = PSYMTAB_TO_SYMTAB (ps);
	if (!s->primary)
	  continue;
	bv = BLOCKVECTOR (s);
	block = BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK);
	ada_add_block_symbols (block, name, domain, objfile, wild_match);
      }
  }

  /* Now add symbols from all per-file blocks if we've gotten no hits.  
     (Not strictly correct, but perhaps better than an error).
     Do the symtabs first, then check the psymtabs */

  if (ndefns == 0)
    {

      ALL_SYMTABS (objfile, s)
      {
	QUIT;
	if (!s->primary)
	  continue;
	bv = BLOCKVECTOR (s);
	block = BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK);
	ada_add_block_symbols (block, name, domain, objfile, wild_match);
      }

      ALL_PSYMTABS (objfile, ps)
      {
	QUIT;
	if (!ps->readin
	    && ada_lookup_partial_symbol (ps, name, 0, domain, wild_match))
	  {
	    s = PSYMTAB_TO_SYMTAB (ps);
	    bv = BLOCKVECTOR (s);
	    if (!s->primary)
	      continue;
	    block = BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK);
	    ada_add_block_symbols (block, name, domain,
				   objfile, wild_match);
	  }
      }
    }

  /* Finally, we try to find NAME as a local symbol in some lexically
     enclosing block.  We do this last, expecting this case to be
     rare. */
  if (ndefns == 0)
    {
      add_symbols_from_enclosing_procs (name, domain, wild_match);
      if (ndefns > 0)
	goto done;
    }

done:
  ndefns = remove_extra_symbols (defn_symbols, defn_blocks, ndefns);


  *syms = defn_symbols;
  *blocks = defn_blocks;
#ifdef TIMING
  markTimeStop (0);
#endif
  return ndefns;
}

/* Return a symbol in DOMAIN matching NAME, in BLOCK0 and enclosing 
 * scope and in global scopes, or NULL if none.  NAME is folded to
 * lower case first, unless it is surrounded in single quotes. 
 * Otherwise, the result is as for ada_lookup_symbol_list, but is 
 * disambiguated by user query if needed. */

struct symbol *
ada_lookup_symbol (const char *name, struct block *block0,
		   domain_enum domain)
{
  struct symbol **candidate_syms;
  struct block **candidate_blocks;
  int n_candidates;

  n_candidates = ada_lookup_symbol_list (name,
					 block0, domain,
					 &candidate_syms, &candidate_blocks);

  if (n_candidates == 0)
    return NULL;
  else if (n_candidates != 1)
    user_select_syms (candidate_syms, candidate_blocks, n_candidates, 1);

  return candidate_syms[0];
}


/* True iff STR is a possible encoded suffix of a normal Ada name 
 * that is to be ignored for matching purposes.  Suffixes of parallel
 * names (e.g., XVE) are not included here.  Currently, the possible suffixes 
 * are given by the regular expression:
 *        (X[nb]*)?(__[0-9]+|\$[0-9]+|___(LJM|X([FDBUP].*|R[^T]?)))?$
 * 
 */
static int
is_name_suffix (const char *str)
{
  int k;
  if (str[0] == 'X')
    {
      str += 1;
      while (str[0] != '_' && str[0] != '\0')
	{
	  if (str[0] != 'n' && str[0] != 'b')
	    return 0;
	  str += 1;
	}
    }
  if (str[0] == '\000')
    return 1;
  if (str[0] == '_')
    {
      if (str[1] != '_' || str[2] == '\000')
	return 0;
      if (str[2] == '_')
	{
	  if (DEPRECATED_STREQ (str + 3, "LJM"))
	    return 1;
	  if (str[3] != 'X')
	    return 0;
	  if (str[4] == 'F' || str[4] == 'D' || str[4] == 'B' ||
	      str[4] == 'U' || str[4] == 'P')
	    return 1;
	  if (str[4] == 'R' && str[5] != 'T')
	    return 1;
	  return 0;
	}
      for (k = 2; str[k] != '\0'; k += 1)
	if (!isdigit (str[k]))
	  return 0;
      return 1;
    }
  if (str[0] == '$' && str[1] != '\000')
    {
      for (k = 1; str[k] != '\0'; k += 1)
	if (!isdigit (str[k]))
	  return 0;
      return 1;
    }
  return 0;
}

/* True if NAME represents a name of the form A1.A2....An, n>=1 and 
 * PATN[0..PATN_LEN-1] = Ak.Ak+1.....An for some k >= 1.  Ignores
 * informational suffixes of NAME (i.e., for which is_name_suffix is
 * true). */
static int
wild_match (const char *patn, int patn_len, const char *name)
{
  int name_len;
  int s, e;

  name_len = strlen (name);
  if (name_len >= patn_len + 5 && DEPRECATED_STREQN (name, "_ada_", 5)
      && DEPRECATED_STREQN (patn, name + 5, patn_len)
      && is_name_suffix (name + patn_len + 5))
    return 1;

  while (name_len >= patn_len)
    {
      if (DEPRECATED_STREQN (patn, name, patn_len) && is_name_suffix (name + patn_len))
	return 1;
      do
	{
	  name += 1;
	  name_len -= 1;
	}
      while (name_len > 0
	     && name[0] != '.' && (name[0] != '_' || name[1] != '_'));
      if (name_len <= 0)
	return 0;
      if (name[0] == '_')
	{
	  if (!islower (name[2]))
	    return 0;
	  name += 2;
	  name_len -= 2;
	}
      else
	{
	  if (!islower (name[1]))
	    return 0;
	  name += 1;
	  name_len -= 1;
	}
    }

  return 0;
}


/* Add symbols from BLOCK matching identifier NAME in DOMAIN to 
   vector *defn_symbols, updating *defn_symbols (if necessary), *SZ (the size of
   the vector *defn_symbols), and *ndefns (the number of symbols
   currently stored in *defn_symbols).  If WILD, treat as NAME with a
   wildcard prefix. OBJFILE is the section containing BLOCK. */

static void
ada_add_block_symbols (struct block *block, const char *name,
		       domain_enum domain, struct objfile *objfile,
		       int wild)
{
  struct dict_iterator iter;
  int name_len = strlen (name);
  /* A matching argument symbol, if any. */
  struct symbol *arg_sym;
  /* Set true when we find a matching non-argument symbol */
  int found_sym;
  struct symbol *sym;

  arg_sym = NULL;
  found_sym = 0;
  if (wild)
    {
      struct symbol *sym;
      ALL_BLOCK_SYMBOLS (block, iter, sym)
      {
	if (SYMBOL_DOMAIN (sym) == domain &&
	    wild_match (name, name_len, DEPRECATED_SYMBOL_NAME (sym)))
	  {
	    switch (SYMBOL_CLASS (sym))
	      {
	      case LOC_ARG:
	      case LOC_LOCAL_ARG:
	      case LOC_REF_ARG:
	      case LOC_REGPARM:
	      case LOC_REGPARM_ADDR:
	      case LOC_BASEREG_ARG:
	      case LOC_COMPUTED_ARG:
		arg_sym = sym;
		break;
	      case LOC_UNRESOLVED:
		continue;
	      default:
		found_sym = 1;
		fill_in_ada_prototype (sym);
		add_defn_to_vec (fixup_symbol_section (sym, objfile), block);
		break;
	      }
	  }
      }
    }
  else
    {
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	  {
	    if (SYMBOL_DOMAIN (sym) == domain)
	      {
		int cmp = strncmp (name, DEPRECATED_SYMBOL_NAME (sym), name_len);

		if (cmp == 0
		    && is_name_suffix (DEPRECATED_SYMBOL_NAME (sym) + name_len))
		  {
		    switch (SYMBOL_CLASS (sym))
		      {
		      case LOC_ARG:
		      case LOC_LOCAL_ARG:
		      case LOC_REF_ARG:
		      case LOC_REGPARM:
		      case LOC_REGPARM_ADDR:
		      case LOC_BASEREG_ARG:
		      case LOC_COMPUTED_ARG:
			arg_sym = sym;
			break;
		      case LOC_UNRESOLVED:
			break;
		      default:
			found_sym = 1;
			fill_in_ada_prototype (sym);
			add_defn_to_vec (fixup_symbol_section (sym, objfile),
					 block);
			break;
		      }
		  }
	      }
	  }
    }

  if (!found_sym && arg_sym != NULL)
    {
      fill_in_ada_prototype (arg_sym);
      add_defn_to_vec (fixup_symbol_section (arg_sym, objfile), block);
    }

  if (!wild)
    {
      arg_sym = NULL;
      found_sym = 0;

      ALL_BLOCK_SYMBOLS (block, iter, sym)
	  {
	    if (SYMBOL_DOMAIN (sym) == domain)
	      {
		int cmp;

		cmp = (int) '_' - (int) DEPRECATED_SYMBOL_NAME (sym)[0];
		if (cmp == 0)
		  {
		    cmp = strncmp ("_ada_", DEPRECATED_SYMBOL_NAME (sym), 5);
		    if (cmp == 0)
		      cmp = strncmp (name, DEPRECATED_SYMBOL_NAME (sym) + 5, name_len);
		  }

		if (cmp == 0
		    && is_name_suffix (DEPRECATED_SYMBOL_NAME (sym) + name_len + 5))
		  {
		    switch (SYMBOL_CLASS (sym))
		      {
		      case LOC_ARG:
		      case LOC_LOCAL_ARG:
		      case LOC_REF_ARG:
		      case LOC_REGPARM:
		      case LOC_REGPARM_ADDR:
		      case LOC_BASEREG_ARG:
		      case LOC_COMPUTED_ARG:
			arg_sym = sym;
			break;
		      case LOC_UNRESOLVED:
			break;
		      default:
			found_sym = 1;
			fill_in_ada_prototype (sym);
			add_defn_to_vec (fixup_symbol_section (sym, objfile),
					 block);
			break;
		      }
		  }
	      }
	  }

      /* NOTE: This really shouldn't be needed for _ada_ symbols.
         They aren't parameters, right? */
      if (!found_sym && arg_sym != NULL)
	{
	  fill_in_ada_prototype (arg_sym);
	  add_defn_to_vec (fixup_symbol_section (arg_sym, objfile), block);
	}
    }
}


				/* Function Types */

/* Assuming that SYM is the symbol for a function, fill in its type 
   with prototype information, if it is not already there.  */

static void
fill_in_ada_prototype (struct symbol *func)
{
  struct block *b;
  int nargs, nsyms;
  struct dict_iterator iter;
  struct type *ftype;
  struct type *rtype;
  size_t max_fields;
  struct symbol *sym;

  if (func == NULL
      || TYPE_CODE (SYMBOL_TYPE (func)) != TYPE_CODE_FUNC
      || TYPE_FIELDS (SYMBOL_TYPE (func)) != NULL)
    return;

  /* We make each function type unique, so that each may have its own */
  /* parameter types.  This particular way of doing so wastes space: */
  /* it would be nicer to build the argument types while the original */
  /* function type is being built (FIXME). */
  rtype = check_typedef (TYPE_TARGET_TYPE (SYMBOL_TYPE (func)));
  ftype = alloc_type (TYPE_OBJFILE (SYMBOL_TYPE (func)));
  make_function_type (rtype, &ftype);
  SYMBOL_TYPE (func) = ftype;

  b = SYMBOL_BLOCK_VALUE (func);

  nargs = 0;
  max_fields = 8;
  TYPE_FIELDS (ftype) =
    (struct field *) xmalloc (sizeof (struct field) * max_fields);
  ALL_BLOCK_SYMBOLS (b, iter, sym)
  {
    GROW_VECT (TYPE_FIELDS (ftype), max_fields, nargs + 1);

    switch (SYMBOL_CLASS (sym))
      {
      case LOC_REF_ARG:
      case LOC_REGPARM_ADDR:
	TYPE_FIELD_BITPOS (ftype, nargs) = nargs;
	TYPE_FIELD_BITSIZE (ftype, nargs) = 0;
	TYPE_FIELD_STATIC_KIND (ftype, nargs) = 0;
	TYPE_FIELD_TYPE (ftype, nargs) =
	  lookup_pointer_type (check_typedef (SYMBOL_TYPE (sym)));
	TYPE_FIELD_NAME (ftype, nargs) = DEPRECATED_SYMBOL_NAME (sym);
	nargs += 1;

	break;

      case LOC_ARG:
      case LOC_REGPARM:
      case LOC_LOCAL_ARG:
      case LOC_BASEREG_ARG:
      case LOC_COMPUTED_ARG:
	TYPE_FIELD_BITPOS (ftype, nargs) = nargs;
	TYPE_FIELD_BITSIZE (ftype, nargs) = 0;
	TYPE_FIELD_STATIC_KIND (ftype, nargs) = 0;
	TYPE_FIELD_TYPE (ftype, nargs) = check_typedef (SYMBOL_TYPE (sym));
	TYPE_FIELD_NAME (ftype, nargs) = DEPRECATED_SYMBOL_NAME (sym);
	nargs += 1;

	break;

      default:
	break;
      }
  }

  /* Re-allocate fields vector; if there are no fields, make the */
  /* fields pointer non-null anyway, to mark that this function type */
  /* has been filled in. */

  TYPE_NFIELDS (ftype) = nargs;
  if (nargs == 0)
    {
      static struct field dummy_field = { 0, 0, 0, 0 };
      xfree (TYPE_FIELDS (ftype));
      TYPE_FIELDS (ftype) = &dummy_field;
    }
  else
    {
      struct field *fields =
	(struct field *) TYPE_ALLOC (ftype, nargs * sizeof (struct field));
      memcpy ((char *) fields,
	      (char *) TYPE_FIELDS (ftype), nargs * sizeof (struct field));
      xfree (TYPE_FIELDS (ftype));
      TYPE_FIELDS (ftype) = fields;
    }
}


				/* Breakpoint-related */

char no_symtab_msg[] =
  "No symbol table is loaded.  Use the \"file\" command.";

/* Assuming that LINE is pointing at the beginning of an argument to
   'break', return a pointer to the delimiter for the initial segment
   of that name.  This is the first ':', ' ', or end of LINE. 
*/
char *
ada_start_decode_line_1 (char *line)
{
  /* [NOTE: strpbrk would be more elegant, but I am reluctant to be
     the first to use such a library function in GDB code.] */
  char *p;
  for (p = line; *p != '\000' && *p != ' ' && *p != ':'; p += 1)
    ;
  return p;
}

/* *SPEC points to a function and line number spec (as in a break
   command), following any initial file name specification.

   Return all symbol table/line specfications (sals) consistent with the
   information in *SPEC and FILE_TABLE in the
   following sense: 
     + FILE_TABLE is null, or the sal refers to a line in the file
       named by FILE_TABLE.
     + If *SPEC points to an argument with a trailing ':LINENUM',
       then the sal refers to that line (or one following it as closely as 
       possible).
     + If *SPEC does not start with '*', the sal is in a function with 
       that name.

   Returns with 0 elements if no matching non-minimal symbols found.

   If *SPEC begins with a function name of the form <NAME>, then NAME
   is taken as a literal name; otherwise the function name is subject
   to the usual mangling.

   *SPEC is updated to point after the function/line number specification.

   FUNFIRSTLINE is non-zero if we desire the first line of real code
   in each function (this is ignored in the presence of a LINENUM spec.).

   If CANONICAL is non-NULL, and if any of the sals require a
   'canonical line spec', then *CANONICAL is set to point to an array
   of strings, corresponding to and equal in length to the returned
   list of sals, such that (*CANONICAL)[i] is non-null and contains a 
   canonical line spec for the ith returned sal, if needed.  If no 
   canonical line specs are required and CANONICAL is non-null, 
   *CANONICAL is set to NULL.

   A 'canonical line spec' is simply a name (in the format of the
   breakpoint command) that uniquely identifies a breakpoint position,
   with no further contextual information or user selection.  It is
   needed whenever the file name, function name, and line number
   information supplied is insufficient for this unique
   identification.  Currently overloaded functions, the name '*', 
   or static functions without a filename yield a canonical line spec.
   The array and the line spec strings are allocated on the heap; it
   is the caller's responsibility to free them.   */

struct symtabs_and_lines
ada_finish_decode_line_1 (char **spec, struct symtab *file_table,
			  int funfirstline, char ***canonical)
{
  struct symbol **symbols;
  struct block **blocks;
  struct block *block;
  int n_matches, i, line_num;
  struct symtabs_and_lines selected;
  struct cleanup *old_chain = make_cleanup (null_cleanup, NULL);
  char *name;

  int len;
  char *lower_name;
  char *unquoted_name;

  if (file_table == NULL)
    block = get_selected_block (NULL);
  else
    block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (file_table), STATIC_BLOCK);

  if (canonical != NULL)
    *canonical = (char **) NULL;

  name = *spec;
  if (**spec == '*')
    *spec += 1;
  else
    {
      while (**spec != '\000' &&
	     !strchr (ada_completer_word_break_characters, **spec))
	*spec += 1;
    }
  len = *spec - name;

  line_num = -1;
  if (file_table != NULL && (*spec)[0] == ':' && isdigit ((*spec)[1]))
    {
      line_num = strtol (*spec + 1, spec, 10);
      while (**spec == ' ' || **spec == '\t')
	*spec += 1;
    }

  if (name[0] == '*')
    {
      if (line_num == -1)
	error ("Wild-card function with no line number or file name.");

      return all_sals_for_line (file_table->filename, line_num, canonical);
    }

  if (name[0] == '\'')
    {
      name += 1;
      len -= 2;
    }

  if (name[0] == '<')
    {
      unquoted_name = (char *) alloca (len - 1);
      memcpy (unquoted_name, name + 1, len - 2);
      unquoted_name[len - 2] = '\000';
      lower_name = NULL;
    }
  else
    {
      unquoted_name = (char *) alloca (len + 1);
      memcpy (unquoted_name, name, len);
      unquoted_name[len] = '\000';
      lower_name = (char *) alloca (len + 1);
      for (i = 0; i < len; i += 1)
	lower_name[i] = tolower (name[i]);
      lower_name[len] = '\000';
    }

  n_matches = 0;
  if (lower_name != NULL)
    n_matches = ada_lookup_symbol_list (ada_mangle (lower_name), block,
					VAR_DOMAIN, &symbols, &blocks);
  if (n_matches == 0)
    n_matches = ada_lookup_symbol_list (unquoted_name, block,
					VAR_DOMAIN, &symbols, &blocks);
  if (n_matches == 0 && line_num >= 0)
    error ("No line number information found for %s.", unquoted_name);
  else if (n_matches == 0)
    {
#ifdef HPPA_COMPILER_BUG
      /* FIXME: See comment in symtab.c::decode_line_1 */
#undef volatile
      volatile struct symtab_and_line val;
#define volatile		/*nothing */
#else
      struct symtab_and_line val;
#endif
      struct minimal_symbol *msymbol;

      init_sal (&val);

      msymbol = NULL;
      if (lower_name != NULL)
	msymbol = ada_lookup_minimal_symbol (ada_mangle (lower_name));
      if (msymbol == NULL)
	msymbol = ada_lookup_minimal_symbol (unquoted_name);
      if (msymbol != NULL)
	{
	  val.pc = SYMBOL_VALUE_ADDRESS (msymbol);
	  val.section = SYMBOL_BFD_SECTION (msymbol);
	  if (funfirstline)
	    {
	      val.pc += FUNCTION_START_OFFSET;
	      SKIP_PROLOGUE (val.pc);
	    }
	  selected.sals = (struct symtab_and_line *)
	    xmalloc (sizeof (struct symtab_and_line));
	  selected.sals[0] = val;
	  selected.nelts = 1;
	  return selected;
	}

      if (!have_full_symbols () &&
	  !have_partial_symbols () && !have_minimal_symbols ())
	error (no_symtab_msg);

      error ("Function \"%s\" not defined.", unquoted_name);
      return selected;		/* for lint */
    }

  if (line_num >= 0)
    {
      return
	find_sal_from_funcs_and_line (file_table->filename, line_num,
				      symbols, n_matches);
    }
  else
    {
      selected.nelts =
	user_select_syms (symbols, blocks, n_matches, n_matches);
    }

  selected.sals = (struct symtab_and_line *)
    xmalloc (sizeof (struct symtab_and_line) * selected.nelts);
  memset (selected.sals, 0, selected.nelts * sizeof (selected.sals[i]));
  make_cleanup (xfree, selected.sals);

  i = 0;
  while (i < selected.nelts)
    {
      if (SYMBOL_CLASS (symbols[i]) == LOC_BLOCK)
	selected.sals[i] = find_function_start_sal (symbols[i], funfirstline);
      else if (SYMBOL_LINE (symbols[i]) != 0)
	{
	  selected.sals[i].symtab = symtab_for_sym (symbols[i]);
	  selected.sals[i].line = SYMBOL_LINE (symbols[i]);
	}
      else if (line_num >= 0)
	{
	  /* Ignore this choice */
	  symbols[i] = symbols[selected.nelts - 1];
	  blocks[i] = blocks[selected.nelts - 1];
	  selected.nelts -= 1;
	  continue;
	}
      else
	error ("Line number not known for symbol \"%s\"", unquoted_name);
      i += 1;
    }

  if (canonical != NULL && (line_num >= 0 || n_matches > 1))
    {
      *canonical = (char **) xmalloc (sizeof (char *) * selected.nelts);
      for (i = 0; i < selected.nelts; i += 1)
	(*canonical)[i] =
	  extended_canonical_line_spec (selected.sals[i],
					SYMBOL_PRINT_NAME (symbols[i]));
    }

  discard_cleanups (old_chain);
  return selected;
}

/* The (single) sal corresponding to line LINE_NUM in a symbol table
   with file name FILENAME that occurs in one of the functions listed 
   in SYMBOLS[0 .. NSYMS-1]. */
static struct symtabs_and_lines
find_sal_from_funcs_and_line (const char *filename, int line_num,
			      struct symbol **symbols, int nsyms)
{
  struct symtabs_and_lines sals;
  int best_index, best;
  struct linetable *best_linetable;
  struct objfile *objfile;
  struct symtab *s;
  struct symtab *best_symtab;

  read_all_symtabs (filename);

  best_index = 0;
  best_linetable = NULL;
  best_symtab = NULL;
  best = 0;
  ALL_SYMTABS (objfile, s)
  {
    struct linetable *l;
    int ind, exact;

    QUIT;

    if (!DEPRECATED_STREQ (filename, s->filename))
      continue;
    l = LINETABLE (s);
    ind = find_line_in_linetable (l, line_num, symbols, nsyms, &exact);
    if (ind >= 0)
      {
	if (exact)
	  {
	    best_index = ind;
	    best_linetable = l;
	    best_symtab = s;
	    goto done;
	  }
	if (best == 0 || l->item[ind].line < best)
	  {
	    best = l->item[ind].line;
	    best_index = ind;
	    best_linetable = l;
	    best_symtab = s;
	  }
      }
  }

  if (best == 0)
    error ("Line number not found in designated function.");

done:

  sals.nelts = 1;
  sals.sals = (struct symtab_and_line *) xmalloc (sizeof (sals.sals[0]));

  init_sal (&sals.sals[0]);

  sals.sals[0].line = best_linetable->item[best_index].line;
  sals.sals[0].pc = best_linetable->item[best_index].pc;
  sals.sals[0].symtab = best_symtab;

  return sals;
}

/* Return the index in LINETABLE of the best match for LINE_NUM whose
   pc falls within one of the functions denoted by SYMBOLS[0..NSYMS-1].  
   Set *EXACTP to the 1 if the match is exact, and 0 otherwise. */
static int
find_line_in_linetable (struct linetable *linetable, int line_num,
			struct symbol **symbols, int nsyms, int *exactp)
{
  int i, len, best_index, best;

  if (line_num <= 0 || linetable == NULL)
    return -1;

  len = linetable->nitems;
  for (i = 0, best_index = -1, best = 0; i < len; i += 1)
    {
      int k;
      struct linetable_entry *item = &(linetable->item[i]);

      for (k = 0; k < nsyms; k += 1)
	{
	  if (symbols[k] != NULL && SYMBOL_CLASS (symbols[k]) == LOC_BLOCK
	      && item->pc >= BLOCK_START (SYMBOL_BLOCK_VALUE (symbols[k]))
	      && item->pc < BLOCK_END (SYMBOL_BLOCK_VALUE (symbols[k])))
	    goto candidate;
	}
      continue;

    candidate:

      if (item->line == line_num)
	{
	  *exactp = 1;
	  return i;
	}

      if (item->line > line_num && (best == 0 || item->line < best))
	{
	  best = item->line;
	  best_index = i;
	}
    }

  *exactp = 0;
  return best_index;
}

/* Find the smallest k >= LINE_NUM such that k is a line number in
   LINETABLE, and k falls strictly within a named function that begins at
   or before LINE_NUM.  Return -1 if there is no such k. */
static int
nearest_line_number_in_linetable (struct linetable *linetable, int line_num)
{
  int i, len, best;

  if (line_num <= 0 || linetable == NULL || linetable->nitems == 0)
    return -1;
  len = linetable->nitems;

  i = 0;
  best = INT_MAX;
  while (i < len)
    {
      int k;
      struct linetable_entry *item = &(linetable->item[i]);

      if (item->line >= line_num && item->line < best)
	{
	  char *func_name;
	  CORE_ADDR start, end;

	  func_name = NULL;
	  find_pc_partial_function (item->pc, &func_name, &start, &end);

	  if (func_name != NULL && item->pc < end)
	    {
	      if (item->line == line_num)
		return line_num;
	      else
		{
		  struct symbol *sym =
		    standard_lookup (func_name, VAR_DOMAIN);
		  if (is_plausible_func_for_line (sym, line_num))
		    best = item->line;
		  else
		    {
		      do
			i += 1;
		      while (i < len && linetable->item[i].pc < end);
		      continue;
		    }
		}
	    }
	}

      i += 1;
    }

  return (best == INT_MAX) ? -1 : best;
}


/* Return the next higher index, k, into LINETABLE such that k > IND, 
   entry k in LINETABLE has a line number equal to LINE_NUM, k
   corresponds to a PC that is in a function different from that 
   corresponding to IND, and falls strictly within a named function
   that begins at a line at or preceding STARTING_LINE.  
   Return -1 if there is no such k.  
   IND == -1 corresponds to no function. */

static int
find_next_line_in_linetable (struct linetable *linetable, int line_num,
			     int starting_line, int ind)
{
  int i, len;

  if (line_num <= 0 || linetable == NULL || ind >= linetable->nitems)
    return -1;
  len = linetable->nitems;

  if (ind >= 0)
    {
      CORE_ADDR start, end;

      if (find_pc_partial_function (linetable->item[ind].pc,
				    (char **) NULL, &start, &end))
	{
	  while (ind < len && linetable->item[ind].pc < end)
	    ind += 1;
	}
      else
	ind += 1;
    }
  else
    ind = 0;

  i = ind;
  while (i < len)
    {
      int k;
      struct linetable_entry *item = &(linetable->item[i]);

      if (item->line >= line_num)
	{
	  char *func_name;
	  CORE_ADDR start, end;

	  func_name = NULL;
	  find_pc_partial_function (item->pc, &func_name, &start, &end);

	  if (func_name != NULL && item->pc < end)
	    {
	      if (item->line == line_num)
		{
		  struct symbol *sym =
		    standard_lookup (func_name, VAR_DOMAIN);
		  if (is_plausible_func_for_line (sym, starting_line))
		    return i;
		  else
		    {
		      while ((i + 1) < len && linetable->item[i + 1].pc < end)
			i += 1;
		    }
		}
	    }
	}
      i += 1;
    }

  return -1;
}

/* True iff function symbol SYM starts somewhere at or before line #
   LINE_NUM. */
static int
is_plausible_func_for_line (struct symbol *sym, int line_num)
{
  struct symtab_and_line start_sal;

  if (sym == NULL)
    return 0;

  start_sal = find_function_start_sal (sym, 0);

  return (start_sal.line != 0 && line_num >= start_sal.line);
}

static void
debug_print_lines (struct linetable *lt)
{
  int i;

  if (lt == NULL)
    return;

  fprintf (stderr, "\t");
  for (i = 0; i < lt->nitems; i += 1)
    fprintf (stderr, "(%d->%p) ", lt->item[i].line, (void *) lt->item[i].pc);
  fprintf (stderr, "\n");
}

static void
debug_print_block (struct block *b)
{
  struct dict_iterator iter;
  struct symbol *sym;

  fprintf (stderr, "Block: %p; [0x%lx, 0x%lx]",
	   b, BLOCK_START (b), BLOCK_END (b));
  if (BLOCK_FUNCTION (b) != NULL)
    fprintf (stderr, " Function: %s", DEPRECATED_SYMBOL_NAME (BLOCK_FUNCTION (b)));
  fprintf (stderr, "\n");
  fprintf (stderr, "\t    Superblock: %p\n", BLOCK_SUPERBLOCK (b));
  fprintf (stderr, "\t    Symbols:");
  ALL_BLOCK_SYMBOLS (b, iter, sym)
  {
    fprintf (stderr, " %s", DEPRECATED_SYMBOL_NAME (sym));
  }
  fprintf (stderr, "\n");
}

static void
debug_print_blocks (struct blockvector *bv)
{
  int i;

  if (bv == NULL)
    return;
  for (i = 0; i < BLOCKVECTOR_NBLOCKS (bv); i += 1)
    {
      fprintf (stderr, "%6d. ", i);
      debug_print_block (BLOCKVECTOR_BLOCK (bv, i));
    }
}

static void
debug_print_symtab (struct symtab *s)
{
  fprintf (stderr, "Symtab %p\n    File: %s; Dir: %s\n", s,
	   s->filename, s->dirname);
  fprintf (stderr, "    Blockvector: %p, Primary: %d\n",
	   BLOCKVECTOR (s), s->primary);
  debug_print_blocks (BLOCKVECTOR (s));
  fprintf (stderr, "    Line table: %p\n", LINETABLE (s));
  debug_print_lines (LINETABLE (s));
}

/* Read in all symbol tables corresponding to partial symbol tables
   with file name FILENAME. */
static void
read_all_symtabs (const char *filename)
{
  struct partial_symtab *ps;
  struct objfile *objfile;

  ALL_PSYMTABS (objfile, ps)
  {
    QUIT;

    if (DEPRECATED_STREQ (filename, ps->filename))
      PSYMTAB_TO_SYMTAB (ps);
  }
}

/* All sals corresponding to line LINE_NUM in a symbol table from file
   FILENAME, as filtered by the user.  If CANONICAL is not null, set
   it to a corresponding array of canonical line specs. */
static struct symtabs_and_lines
all_sals_for_line (const char *filename, int line_num, char ***canonical)
{
  struct symtabs_and_lines result;
  struct objfile *objfile;
  struct symtab *s;
  struct cleanup *old_chain = make_cleanup (null_cleanup, NULL);
  size_t len;

  read_all_symtabs (filename);

  result.sals =
    (struct symtab_and_line *) xmalloc (4 * sizeof (result.sals[0]));
  result.nelts = 0;
  len = 4;
  make_cleanup (free_current_contents, &result.sals);

  ALL_SYMTABS (objfile, s)
  {
    int ind, target_line_num;

    QUIT;

    if (!DEPRECATED_STREQ (s->filename, filename))
      continue;

    target_line_num =
      nearest_line_number_in_linetable (LINETABLE (s), line_num);
    if (target_line_num == -1)
      continue;

    ind = -1;
    while (1)
      {
	ind =
	  find_next_line_in_linetable (LINETABLE (s),
				       target_line_num, line_num, ind);

	if (ind < 0)
	  break;

	GROW_VECT (result.sals, len, result.nelts + 1);
	init_sal (&result.sals[result.nelts]);
	result.sals[result.nelts].line = LINETABLE (s)->item[ind].line;
	result.sals[result.nelts].pc = LINETABLE (s)->item[ind].pc;
	result.sals[result.nelts].symtab = s;
	result.nelts += 1;
      }
  }

  if (canonical != NULL || result.nelts > 1)
    {
      int k;
      char **func_names = (char **) alloca (result.nelts * sizeof (char *));
      int first_choice = (result.nelts > 1) ? 2 : 1;
      int n;
      int *choices = (int *) alloca (result.nelts * sizeof (int));

      for (k = 0; k < result.nelts; k += 1)
	{
	  find_pc_partial_function (result.sals[k].pc, &func_names[k],
				    (CORE_ADDR *) NULL, (CORE_ADDR *) NULL);
	  if (func_names[k] == NULL)
	    error ("Could not find function for one or more breakpoints.");
	}

      if (result.nelts > 1)
	{
	  printf_unfiltered ("[0] cancel\n");
	  if (result.nelts > 1)
	    printf_unfiltered ("[1] all\n");
	  for (k = 0; k < result.nelts; k += 1)
	    printf_unfiltered ("[%d] %s\n", k + first_choice,
			       ada_demangle (func_names[k]));

	  n = get_selections (choices, result.nelts, result.nelts,
			      result.nelts > 1, "instance-choice");

	  for (k = 0; k < n; k += 1)
	    {
	      result.sals[k] = result.sals[choices[k]];
	      func_names[k] = func_names[choices[k]];
	    }
	  result.nelts = n;
	}

      if (canonical != NULL)
	{
	  *canonical = (char **) xmalloc (result.nelts * sizeof (char **));
	  make_cleanup (xfree, *canonical);
	  for (k = 0; k < result.nelts; k += 1)
	    {
	      (*canonical)[k] =
		extended_canonical_line_spec (result.sals[k], func_names[k]);
	      if ((*canonical)[k] == NULL)
		error ("Could not locate one or more breakpoints.");
	      make_cleanup (xfree, (*canonical)[k]);
	    }
	}
    }

  discard_cleanups (old_chain);
  return result;
}


/* A canonical line specification of the form FILE:NAME:LINENUM for
   symbol table and line data SAL.  NULL if insufficient
   information. The caller is responsible for releasing any space
   allocated. */

static char *
extended_canonical_line_spec (struct symtab_and_line sal, const char *name)
{
  char *r;

  if (sal.symtab == NULL || sal.symtab->filename == NULL || sal.line <= 0)
    return NULL;

  r = (char *) xmalloc (strlen (name) + strlen (sal.symtab->filename)
			+ sizeof (sal.line) * 3 + 3);
  sprintf (r, "%s:'%s':%d", sal.symtab->filename, name, sal.line);
  return r;
}

#if 0
int begin_bnum = -1;
#endif
int begin_annotate_level = 0;

static void
begin_cleanup (void *dummy)
{
  begin_annotate_level = 0;
}

static void
begin_command (char *args, int from_tty)
{
  struct minimal_symbol *msym;
  CORE_ADDR main_program_name_addr;
  char main_program_name[1024];
  struct cleanup *old_chain = make_cleanup (begin_cleanup, NULL);
  begin_annotate_level = 2;

  /* Check that there is a program to debug */
  if (!have_full_symbols () && !have_partial_symbols ())
    error ("No symbol table is loaded.  Use the \"file\" command.");

  /* Check that we are debugging an Ada program */
  /*  if (ada_update_initial_language (language_unknown, NULL) != language_ada)
     error ("Cannot find the Ada initialization procedure.  Is this an Ada main program?");
   */
  /* FIXME: language_ada should be defined in defs.h */

  /* Get the address of the name of the main procedure */
  msym = lookup_minimal_symbol (ADA_MAIN_PROGRAM_SYMBOL_NAME, NULL, NULL);

  if (msym != NULL)
    {
      main_program_name_addr = SYMBOL_VALUE_ADDRESS (msym);
      if (main_program_name_addr == 0)
	error ("Invalid address for Ada main program name.");

      /* Read the name of the main procedure */
      extract_string (main_program_name_addr, main_program_name);

      /* Put a temporary breakpoint in the Ada main program and run */
      do_command ("tbreak ", main_program_name, 0);
      do_command ("run ", args, 0);
    }
  else
    {
      /* If we could not find the symbol containing the name of the
         main program, that means that the compiler that was used to build
         was not recent enough. In that case, we fallback to the previous
         mechanism, which is a little bit less reliable, but has proved to work
         in most cases. The only cases where it will fail is when the user
         has set some breakpoints which will be hit before the end of the
         begin command processing (eg in the initialization code).

         The begining of the main Ada subprogram is located by breaking
         on the adainit procedure. Since we know that the binder generates
         the call to this procedure exactly 2 calls before the call to the
         Ada main subprogram, it is then easy to put a breakpoint on this
         Ada main subprogram once we hit adainit.
       */
      do_command ("tbreak adainit", 0);
      do_command ("run ", args, 0);
      do_command ("up", 0);
      do_command ("tbreak +2", 0);
      do_command ("continue", 0);
      do_command ("step", 0);
    }

  do_cleanups (old_chain);
}

int
is_ada_runtime_file (char *filename)
{
  return (DEPRECATED_STREQN (filename, "s-", 2) ||
	  DEPRECATED_STREQN (filename, "a-", 2) ||
	  DEPRECATED_STREQN (filename, "g-", 2) || DEPRECATED_STREQN (filename, "i-", 2));
}

/* find the first frame that contains debugging information and that is not
   part of the Ada run-time, starting from fi and moving upward. */

int
find_printable_frame (struct frame_info *fi, int level)
{
  struct symtab_and_line sal;

  for (; fi != NULL; level += 1, fi = get_prev_frame (fi))
    {
      find_frame_sal (fi, &sal);
      if (sal.symtab && !is_ada_runtime_file (sal.symtab->filename))
	{
#if defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET)
	  /* libpthread.so contains some debugging information that prevents us
	     from finding the right frame */

	  if (sal.symtab->objfile &&
	      DEPRECATED_STREQ (sal.symtab->objfile->name, "/usr/shlib/libpthread.so"))
	    continue;
#endif
	  deprecated_selected_frame = fi;
	  break;
	}
    }

  return level;
}

void
ada_report_exception_break (struct breakpoint *b)
{
  /* FIXME: break_on_exception should be defined in breakpoint.h */
  /*  if (b->break_on_exception == 1)
     {
     /* Assume that cond has 16 elements, the 15th
   being the exception *//*
   if (b->cond && b->cond->nelts == 16)
   {
   ui_out_text (uiout, "on ");
   ui_out_field_string (uiout, "exception",
   SYMBOL_NAME (b->cond->elts[14].symbol));
   }
   else
   ui_out_text (uiout, "on all exceptions");
   }
   else if (b->break_on_exception == 2)
   ui_out_text (uiout, "on unhandled exception");
   else if (b->break_on_exception == 3)
   ui_out_text (uiout, "on assert failure");
   #else
   if (b->break_on_exception == 1)
   { */
  /* Assume that cond has 16 elements, the 15th
   being the exception *//*
   if (b->cond && b->cond->nelts == 16)
   {
   fputs_filtered ("on ", gdb_stdout);
   fputs_filtered (SYMBOL_NAME
   (b->cond->elts[14].symbol), gdb_stdout);
   }
   else
   fputs_filtered ("on all exceptions", gdb_stdout);
   }
   else if (b->break_on_exception == 2)
   fputs_filtered ("on unhandled exception", gdb_stdout);
   else if (b->break_on_exception == 3)
   fputs_filtered ("on assert failure", gdb_stdout);
 */
}

int
ada_is_exception_sym (struct symbol *sym)
{
  char *type_name = type_name_no_tag (SYMBOL_TYPE (sym));

  return (SYMBOL_CLASS (sym) != LOC_TYPEDEF
	  && SYMBOL_CLASS (sym) != LOC_BLOCK
	  && SYMBOL_CLASS (sym) != LOC_CONST
	  && type_name != NULL && DEPRECATED_STREQ (type_name, "exception"));
}

int
ada_maybe_exception_partial_symbol (struct partial_symbol *sym)
{
  return (SYMBOL_CLASS (sym) != LOC_TYPEDEF
	  && SYMBOL_CLASS (sym) != LOC_BLOCK
	  && SYMBOL_CLASS (sym) != LOC_CONST);
}

/* If ARG points to an Ada exception or assert breakpoint, rewrite
   into equivalent form.  Return resulting argument string. Set
   *BREAK_ON_EXCEPTIONP to 1 for ordinary break on exception, 2 for
   break on unhandled, 3 for assert, 0 otherwise. */
char *
ada_breakpoint_rewrite (char *arg, int *break_on_exceptionp)
{
  if (arg == NULL)
    return arg;
  *break_on_exceptionp = 0;
  /* FIXME: language_ada should be defined in defs.h */
  /*  if (current_language->la_language == language_ada
     && DEPRECATED_STREQN (arg, "exception", 9) &&
     (arg[9] == ' ' || arg[9] == '\t' || arg[9] == '\0'))
     {
     char *tok, *end_tok;
     int toklen;

     *break_on_exceptionp = 1;

     tok = arg+9;
     while (*tok == ' ' || *tok == '\t')
     tok += 1;

     end_tok = tok;

     while (*end_tok != ' ' && *end_tok != '\t' && *end_tok != '\000')
     end_tok += 1;

     toklen = end_tok - tok;

     arg = (char*) xmalloc (sizeof ("__gnat_raise_nodefer_with_msg if "
     "long_integer(e) = long_integer(&)")
     + toklen + 1);
     make_cleanup (xfree, arg);
     if (toklen == 0)
     strcpy (arg, "__gnat_raise_nodefer_with_msg");
     else if (DEPRECATED_STREQN (tok, "unhandled", toklen))
     {
     *break_on_exceptionp = 2;
     strcpy (arg, "__gnat_unhandled_exception");
     }
     else
     {
     sprintf (arg, "__gnat_raise_nodefer_with_msg if "
     "long_integer(e) = long_integer(&%.*s)", 
     toklen, tok);
     }
     }
     else if (current_language->la_language == language_ada
     && DEPRECATED_STREQN (arg, "assert", 6) &&
     (arg[6] == ' ' || arg[6] == '\t' || arg[6] == '\0'))
     {
     char *tok = arg + 6;

     *break_on_exceptionp = 3;

     arg = (char*) 
     xmalloc (sizeof ("system__assertions__raise_assert_failure")
     + strlen (tok) + 1);
     make_cleanup (xfree, arg);
     sprintf (arg, "system__assertions__raise_assert_failure%s", tok);
     }
   */
  return arg;
}


				/* Field Access */

/* True if field number FIELD_NUM in struct or union type TYPE is supposed
   to be invisible to users. */

int
ada_is_ignored_field (struct type *type, int field_num)
{
  if (field_num < 0 || field_num > TYPE_NFIELDS (type))
    return 1;
  else
    {
      const char *name = TYPE_FIELD_NAME (type, field_num);
      return (name == NULL
	      || (name[0] == '_' && !DEPRECATED_STREQN (name, "_parent", 7)));
    }
}

/* True iff structure type TYPE has a tag field. */

int
ada_is_tagged_type (struct type *type)
{
  if (type == NULL || TYPE_CODE (type) != TYPE_CODE_STRUCT)
    return 0;

  return (ada_lookup_struct_elt_type (type, "_tag", 1, NULL) != NULL);
}

/* The type of the tag on VAL. */

struct type *
ada_tag_type (struct value *val)
{
  return ada_lookup_struct_elt_type (VALUE_TYPE (val), "_tag", 0, NULL);
}

/* The value of the tag on VAL. */

struct value *
ada_value_tag (struct value *val)
{
  return ada_value_struct_elt (val, "_tag", "record");
}

/* The parent type of TYPE, or NULL if none. */

struct type *
ada_parent_type (struct type *type)
{
  int i;

  CHECK_TYPEDEF (type);

  if (type == NULL || TYPE_CODE (type) != TYPE_CODE_STRUCT)
    return NULL;

  for (i = 0; i < TYPE_NFIELDS (type); i += 1)
    if (ada_is_parent_field (type, i))
      return check_typedef (TYPE_FIELD_TYPE (type, i));

  return NULL;
}

/* True iff field number FIELD_NUM of structure type TYPE contains the 
   parent-type (inherited) fields of a derived type.  Assumes TYPE is 
   a structure type with at least FIELD_NUM+1 fields. */

int
ada_is_parent_field (struct type *type, int field_num)
{
  const char *name = TYPE_FIELD_NAME (check_typedef (type), field_num);
  return (name != NULL &&
	  (DEPRECATED_STREQN (name, "PARENT", 6) || DEPRECATED_STREQN (name, "_parent", 7)));
}

/* True iff field number FIELD_NUM of structure type TYPE is a 
   transparent wrapper field (which should be silently traversed when doing
   field selection and flattened when printing).  Assumes TYPE is a 
   structure type with at least FIELD_NUM+1 fields.  Such fields are always
   structures. */

int
ada_is_wrapper_field (struct type *type, int field_num)
{
  const char *name = TYPE_FIELD_NAME (type, field_num);
  return (name != NULL
	  && (DEPRECATED_STREQN (name, "PARENT", 6) || DEPRECATED_STREQ (name, "REP")
	      || DEPRECATED_STREQN (name, "_parent", 7)
	      || name[0] == 'S' || name[0] == 'R' || name[0] == 'O'));
}

/* True iff field number FIELD_NUM of structure or union type TYPE 
   is a variant wrapper.  Assumes TYPE is a structure type with at least 
   FIELD_NUM+1 fields. */

int
ada_is_variant_part (struct type *type, int field_num)
{
  struct type *field_type = TYPE_FIELD_TYPE (type, field_num);
  return (TYPE_CODE (field_type) == TYPE_CODE_UNION
	  || (is_dynamic_field (type, field_num)
	      && TYPE_CODE (TYPE_TARGET_TYPE (field_type)) ==
	      TYPE_CODE_UNION));
}

/* Assuming that VAR_TYPE is a variant wrapper (type of the variant part)
   whose discriminants are contained in the record type OUTER_TYPE, 
   returns the type of the controlling discriminant for the variant.  */

struct type *
ada_variant_discrim_type (struct type *var_type, struct type *outer_type)
{
  char *name = ada_variant_discrim_name (var_type);
  struct type *type = ada_lookup_struct_elt_type (outer_type, name, 1, NULL);
  if (type == NULL)
    return builtin_type_int;
  else
    return type;
}

/* Assuming that TYPE is the type of a variant wrapper, and FIELD_NUM is a 
   valid field number within it, returns 1 iff field FIELD_NUM of TYPE
   represents a 'when others' clause; otherwise 0. */

int
ada_is_others_clause (struct type *type, int field_num)
{
  const char *name = TYPE_FIELD_NAME (type, field_num);
  return (name != NULL && name[0] == 'O');
}

/* Assuming that TYPE0 is the type of the variant part of a record,
   returns the name of the discriminant controlling the variant.  The
   value is valid until the next call to ada_variant_discrim_name. */

char *
ada_variant_discrim_name (struct type *type0)
{
  static char *result = NULL;
  static size_t result_len = 0;
  struct type *type;
  const char *name;
  const char *discrim_end;
  const char *discrim_start;

  if (TYPE_CODE (type0) == TYPE_CODE_PTR)
    type = TYPE_TARGET_TYPE (type0);
  else
    type = type0;

  name = ada_type_name (type);

  if (name == NULL || name[0] == '\000')
    return "";

  for (discrim_end = name + strlen (name) - 6; discrim_end != name;
       discrim_end -= 1)
    {
      if (DEPRECATED_STREQN (discrim_end, "___XVN", 6))
	break;
    }
  if (discrim_end == name)
    return "";

  for (discrim_start = discrim_end; discrim_start != name + 3;
       discrim_start -= 1)
    {
      if (discrim_start == name + 1)
	return "";
      if ((discrim_start > name + 3 && DEPRECATED_STREQN (discrim_start - 3, "___", 3))
	  || discrim_start[-1] == '.')
	break;
    }

  GROW_VECT (result, result_len, discrim_end - discrim_start + 1);
  strncpy (result, discrim_start, discrim_end - discrim_start);
  result[discrim_end - discrim_start] = '\0';
  return result;
}

/* Scan STR for a subtype-encoded number, beginning at position K. Put the 
   position of the character just past the number scanned in *NEW_K, 
   if NEW_K!=NULL. Put the scanned number in *R, if R!=NULL.  Return 1 
   if there was a valid number at the given position, and 0 otherwise.  A 
   "subtype-encoded" number consists of the absolute value in decimal, 
   followed by the letter 'm' to indicate a negative number.  Assumes 0m 
   does not occur. */

int
ada_scan_number (const char str[], int k, LONGEST * R, int *new_k)
{
  ULONGEST RU;

  if (!isdigit (str[k]))
    return 0;

  /* Do it the hard way so as not to make any assumption about 
     the relationship of unsigned long (%lu scan format code) and
     LONGEST. */
  RU = 0;
  while (isdigit (str[k]))
    {
      RU = RU * 10 + (str[k] - '0');
      k += 1;
    }

  if (str[k] == 'm')
    {
      if (R != NULL)
	*R = (-(LONGEST) (RU - 1)) - 1;
      k += 1;
    }
  else if (R != NULL)
    *R = (LONGEST) RU;

  /* NOTE on the above: Technically, C does not say what the results of 
     - (LONGEST) RU or (LONGEST) -RU are for RU == largest positive
     number representable as a LONGEST (although either would probably work
     in most implementations).  When RU>0, the locution in the then branch
     above is always equivalent to the negative of RU. */

  if (new_k != NULL)
    *new_k = k;
  return 1;
}

/* Assuming that TYPE is a variant part wrapper type (a VARIANTS field), 
   and FIELD_NUM is a valid field number within it, returns 1 iff VAL is 
   in the range encoded by field FIELD_NUM of TYPE; otherwise 0. */

int
ada_in_variant (LONGEST val, struct type *type, int field_num)
{
  const char *name = TYPE_FIELD_NAME (type, field_num);
  int p;

  p = 0;
  while (1)
    {
      switch (name[p])
	{
	case '\0':
	  return 0;
	case 'S':
	  {
	    LONGEST W;
	    if (!ada_scan_number (name, p + 1, &W, &p))
	      return 0;
	    if (val == W)
	      return 1;
	    break;
	  }
	case 'R':
	  {
	    LONGEST L, U;
	    if (!ada_scan_number (name, p + 1, &L, &p)
		|| name[p] != 'T' || !ada_scan_number (name, p + 1, &U, &p))
	      return 0;
	    if (val >= L && val <= U)
	      return 1;
	    break;
	  }
	case 'O':
	  return 1;
	default:
	  return 0;
	}
    }
}

/* Given a value ARG1 (offset by OFFSET bytes)
   of a struct or union type ARG_TYPE,
   extract and return the value of one of its (non-static) fields.
   FIELDNO says which field.   Differs from value_primitive_field only
   in that it can handle packed values of arbitrary type. */

struct value *
ada_value_primitive_field (struct value *arg1, int offset, int fieldno,
			   struct type *arg_type)
{
  struct value *v;
  struct type *type;

  CHECK_TYPEDEF (arg_type);
  type = TYPE_FIELD_TYPE (arg_type, fieldno);

  /* Handle packed fields */

  if (TYPE_FIELD_BITSIZE (arg_type, fieldno) != 0)
    {
      int bit_pos = TYPE_FIELD_BITPOS (arg_type, fieldno);
      int bit_size = TYPE_FIELD_BITSIZE (arg_type, fieldno);

      return ada_value_primitive_packed_val (arg1, VALUE_CONTENTS (arg1),
					     offset + bit_pos / 8,
					     bit_pos % 8, bit_size, type);
    }
  else
    return value_primitive_field (arg1, offset, fieldno, arg_type);
}


/* Look for a field NAME in ARG. Adjust the address of ARG by OFFSET bytes,
   and search in it assuming it has (class) type TYPE.
   If found, return value, else return NULL.

   Searches recursively through wrapper fields (e.g., '_parent'). */

struct value *
ada_search_struct_field (char *name, struct value *arg, int offset,
			 struct type *type)
{
  int i;
  CHECK_TYPEDEF (type);

  for (i = TYPE_NFIELDS (type) - 1; i >= 0; i -= 1)
    {
      char *t_field_name = TYPE_FIELD_NAME (type, i);

      if (t_field_name == NULL)
	continue;

      else if (field_name_match (t_field_name, name))
	return ada_value_primitive_field (arg, offset, i, type);

      else if (ada_is_wrapper_field (type, i))
	{
	  struct value *v = ada_search_struct_field (name, arg,
						     offset +
						     TYPE_FIELD_BITPOS (type,
									i) /
						     8,
						     TYPE_FIELD_TYPE (type,
								      i));
	  if (v != NULL)
	    return v;
	}

      else if (ada_is_variant_part (type, i))
	{
	  int j;
	  struct type *field_type = check_typedef (TYPE_FIELD_TYPE (type, i));
	  int var_offset = offset + TYPE_FIELD_BITPOS (type, i) / 8;

	  for (j = TYPE_NFIELDS (field_type) - 1; j >= 0; j -= 1)
	    {
	      struct value *v = ada_search_struct_field (name, arg,
							 var_offset
							 +
							 TYPE_FIELD_BITPOS
							 (field_type, j) / 8,
							 TYPE_FIELD_TYPE
							 (field_type, j));
	      if (v != NULL)
		return v;
	    }
	}
    }
  return NULL;
}

/* Given ARG, a value of type (pointer to a)* structure/union,
   extract the component named NAME from the ultimate target structure/union
   and return it as a value with its appropriate type.

   The routine searches for NAME among all members of the structure itself 
   and (recursively) among all members of any wrapper members 
   (e.g., '_parent').

   ERR is a name (for use in error messages) that identifies the class 
   of entity that ARG is supposed to be. */

struct value *
ada_value_struct_elt (struct value *arg, char *name, char *err)
{
  struct type *t;
  struct value *v;

  arg = ada_coerce_ref (arg);
  t = check_typedef (VALUE_TYPE (arg));

  /* Follow pointers until we get to a non-pointer.  */

  while (TYPE_CODE (t) == TYPE_CODE_PTR || TYPE_CODE (t) == TYPE_CODE_REF)
    {
      arg = ada_value_ind (arg);
      t = check_typedef (VALUE_TYPE (arg));
    }

  if (TYPE_CODE (t) != TYPE_CODE_STRUCT && TYPE_CODE (t) != TYPE_CODE_UNION)
    error ("Attempt to extract a component of a value that is not a %s.",
	   err);

  v = ada_search_struct_field (name, arg, 0, t);
  if (v == NULL)
    error ("There is no member named %s.", name);

  return v;
}

/* Given a type TYPE, look up the type of the component of type named NAME.
   If DISPP is non-null, add its byte displacement from the beginning of a 
   structure (pointed to by a value) of type TYPE to *DISPP (does not 
   work for packed fields).

   Matches any field whose name has NAME as a prefix, possibly
   followed by "___". 

   TYPE can be either a struct or union, or a pointer or reference to 
   a struct or union.  If it is a pointer or reference, its target 
   type is automatically used.

   Looks recursively into variant clauses and parent types.

   If NOERR is nonzero, return NULL if NAME is not suitably defined. */

struct type *
ada_lookup_struct_elt_type (struct type *type, char *name, int noerr,
			    int *dispp)
{
  int i;

  if (name == NULL)
    goto BadName;

  while (1)
    {
      CHECK_TYPEDEF (type);
      if (TYPE_CODE (type) != TYPE_CODE_PTR
	  && TYPE_CODE (type) != TYPE_CODE_REF)
	break;
      type = TYPE_TARGET_TYPE (type);
    }

  if (TYPE_CODE (type) != TYPE_CODE_STRUCT &&
      TYPE_CODE (type) != TYPE_CODE_UNION)
    {
      target_terminal_ours ();
      gdb_flush (gdb_stdout);
      fprintf_unfiltered (gdb_stderr, "Type ");
      type_print (type, "", gdb_stderr, -1);
      error (" is not a structure or union type");
    }

  type = to_static_fixed_type (type);

  for (i = 0; i < TYPE_NFIELDS (type); i += 1)
    {
      char *t_field_name = TYPE_FIELD_NAME (type, i);
      struct type *t;
      int disp;

      if (t_field_name == NULL)
	continue;

      else if (field_name_match (t_field_name, name))
	{
	  if (dispp != NULL)
	    *dispp += TYPE_FIELD_BITPOS (type, i) / 8;
	  return check_typedef (TYPE_FIELD_TYPE (type, i));
	}

      else if (ada_is_wrapper_field (type, i))
	{
	  disp = 0;
	  t = ada_lookup_struct_elt_type (TYPE_FIELD_TYPE (type, i), name,
					  1, &disp);
	  if (t != NULL)
	    {
	      if (dispp != NULL)
		*dispp += disp + TYPE_FIELD_BITPOS (type, i) / 8;
	      return t;
	    }
	}

      else if (ada_is_variant_part (type, i))
	{
	  int j;
	  struct type *field_type = check_typedef (TYPE_FIELD_TYPE (type, i));

	  for (j = TYPE_NFIELDS (field_type) - 1; j >= 0; j -= 1)
	    {
	      disp = 0;
	      t = ada_lookup_struct_elt_type (TYPE_FIELD_TYPE (field_type, j),
					      name, 1, &disp);
	      if (t != NULL)
		{
		  if (dispp != NULL)
		    *dispp += disp + TYPE_FIELD_BITPOS (type, i) / 8;
		  return t;
		}
	    }
	}

    }

BadName:
  if (!noerr)
    {
      target_terminal_ours ();
      gdb_flush (gdb_stdout);
      fprintf_unfiltered (gdb_stderr, "Type ");
      type_print (type, "", gdb_stderr, -1);
      fprintf_unfiltered (gdb_stderr, " has no component named ");
      error ("%s", name == NULL ? "<null>" : name);
    }

  return NULL;
}

/* Assuming that VAR_TYPE is the type of a variant part of a record (a union),
   within a value of type OUTER_TYPE that is stored in GDB at
   OUTER_VALADDR, determine which variant clause (field number in VAR_TYPE, 
   numbering from 0) is applicable.  Returns -1 if none are. */

int
ada_which_variant_applies (struct type *var_type, struct type *outer_type,
			   char *outer_valaddr)
{
  int others_clause;
  int i;
  int disp;
  struct type *discrim_type;
  char *discrim_name = ada_variant_discrim_name (var_type);
  LONGEST discrim_val;

  disp = 0;
  discrim_type =
    ada_lookup_struct_elt_type (outer_type, discrim_name, 1, &disp);
  if (discrim_type == NULL)
    return -1;
  discrim_val = unpack_long (discrim_type, outer_valaddr + disp);

  others_clause = -1;
  for (i = 0; i < TYPE_NFIELDS (var_type); i += 1)
    {
      if (ada_is_others_clause (var_type, i))
	others_clause = i;
      else if (ada_in_variant (discrim_val, var_type, i))
	return i;
    }

  return others_clause;
}



				/* Dynamic-Sized Records */

/* Strategy: The type ostensibly attached to a value with dynamic size
   (i.e., a size that is not statically recorded in the debugging
   data) does not accurately reflect the size or layout of the value.
   Our strategy is to convert these values to values with accurate,
   conventional types that are constructed on the fly. */

/* There is a subtle and tricky problem here.  In general, we cannot
   determine the size of dynamic records without its data.  However,
   the 'struct value' data structure, which GDB uses to represent
   quantities in the inferior process (the target), requires the size
   of the type at the time of its allocation in order to reserve space
   for GDB's internal copy of the data.  That's why the
   'to_fixed_xxx_type' routines take (target) addresses as parameters,
   rather than struct value*s.  

   However, GDB's internal history variables ($1, $2, etc.) are
   struct value*s containing internal copies of the data that are not, in
   general, the same as the data at their corresponding addresses in
   the target.  Fortunately, the types we give to these values are all
   conventional, fixed-size types (as per the strategy described
   above), so that we don't usually have to perform the
   'to_fixed_xxx_type' conversions to look at their values.
   Unfortunately, there is one exception: if one of the internal
   history variables is an array whose elements are unconstrained
   records, then we will need to create distinct fixed types for each
   element selected.  */

/* The upshot of all of this is that many routines take a (type, host
   address, target address) triple as arguments to represent a value.
   The host address, if non-null, is supposed to contain an internal
   copy of the relevant data; otherwise, the program is to consult the
   target at the target address. */

/* Assuming that VAL0 represents a pointer value, the result of
   dereferencing it.  Differs from value_ind in its treatment of
   dynamic-sized types. */

struct value *
ada_value_ind (struct value *val0)
{
  struct value *val = unwrap_value (value_ind (val0));
  return ada_to_fixed_value (VALUE_TYPE (val), 0,
			     VALUE_ADDRESS (val) + VALUE_OFFSET (val), val);
}

/* The value resulting from dereferencing any "reference to"
 * qualifiers on VAL0. */
static struct value *
ada_coerce_ref (struct value *val0)
{
  if (TYPE_CODE (VALUE_TYPE (val0)) == TYPE_CODE_REF)
    {
      struct value *val = val0;
      COERCE_REF (val);
      val = unwrap_value (val);
      return ada_to_fixed_value (VALUE_TYPE (val), 0,
				 VALUE_ADDRESS (val) + VALUE_OFFSET (val),
				 val);
    }
  else
    return val0;
}

/* Return OFF rounded upward if necessary to a multiple of
   ALIGNMENT (a power of 2). */

static unsigned int
align_value (unsigned int off, unsigned int alignment)
{
  return (off + alignment - 1) & ~(alignment - 1);
}

/* Return the additional bit offset required by field F of template
   type TYPE. */

static unsigned int
field_offset (struct type *type, int f)
{
  int n = TYPE_FIELD_BITPOS (type, f);
  /* Kludge (temporary?) to fix problem with dwarf output. */
  if (n < 0)
    return (unsigned int) n & 0xffff;
  else
    return n;
}


/* Return the bit alignment required for field #F of template type TYPE. */

static unsigned int
field_alignment (struct type *type, int f)
{
  const char *name = TYPE_FIELD_NAME (type, f);
  int len = (name == NULL) ? 0 : strlen (name);
  int align_offset;

  if (len < 8 || !isdigit (name[len - 1]))
    return TARGET_CHAR_BIT;

  if (isdigit (name[len - 2]))
    align_offset = len - 2;
  else
    align_offset = len - 1;

  if (align_offset < 7 || !DEPRECATED_STREQN ("___XV", name + align_offset - 6, 5))
    return TARGET_CHAR_BIT;

  return atoi (name + align_offset) * TARGET_CHAR_BIT;
}

/* Find a type named NAME.  Ignores ambiguity.  */
struct type *
ada_find_any_type (const char *name)
{
  struct symbol *sym;

  sym = standard_lookup (name, VAR_DOMAIN);
  if (sym != NULL && SYMBOL_CLASS (sym) == LOC_TYPEDEF)
    return SYMBOL_TYPE (sym);

  sym = standard_lookup (name, STRUCT_DOMAIN);
  if (sym != NULL)
    return SYMBOL_TYPE (sym);

  return NULL;
}

/* Because of GNAT encoding conventions, several GDB symbols may match a
   given type name. If the type denoted by TYPE0 is to be preferred to
   that of TYPE1 for purposes of type printing, return non-zero;
   otherwise return 0. */
int
ada_prefer_type (struct type *type0, struct type *type1)
{
  if (type1 == NULL)
    return 1;
  else if (type0 == NULL)
    return 0;
  else if (TYPE_CODE (type1) == TYPE_CODE_VOID)
    return 1;
  else if (TYPE_CODE (type0) == TYPE_CODE_VOID)
    return 0;
  else if (ada_is_packed_array_type (type0))
    return 1;
  else if (ada_is_array_descriptor (type0)
	   && !ada_is_array_descriptor (type1))
    return 1;
  else if (ada_renaming_type (type0) != NULL
	   && ada_renaming_type (type1) == NULL)
    return 1;
  return 0;
}

/* The name of TYPE, which is either its TYPE_NAME, or, if that is
   null, its TYPE_TAG_NAME.  Null if TYPE is null. */
char *
ada_type_name (struct type *type)
{
  if (type == NULL)
    return NULL;
  else if (TYPE_NAME (type) != NULL)
    return TYPE_NAME (type);
  else
    return TYPE_TAG_NAME (type);
}

/* Find a parallel type to TYPE whose name is formed by appending
   SUFFIX to the name of TYPE. */

struct type *
ada_find_parallel_type (struct type *type, const char *suffix)
{
  static char *name;
  static size_t name_len = 0;
  struct symbol **syms;
  struct block **blocks;
  int nsyms;
  int len;
  char *typename = ada_type_name (type);

  if (typename == NULL)
    return NULL;

  len = strlen (typename);

  GROW_VECT (name, name_len, len + strlen (suffix) + 1);

  strcpy (name, typename);
  strcpy (name + len, suffix);

  return ada_find_any_type (name);
}


/* If TYPE is a variable-size record type, return the corresponding template
   type describing its fields.  Otherwise, return NULL. */

static struct type *
dynamic_template_type (struct type *type)
{
  CHECK_TYPEDEF (type);

  if (type == NULL || TYPE_CODE (type) != TYPE_CODE_STRUCT
      || ada_type_name (type) == NULL)
    return NULL;
  else
    {
      int len = strlen (ada_type_name (type));
      if (len > 6 && DEPRECATED_STREQ (ada_type_name (type) + len - 6, "___XVE"))
	return type;
      else
	return ada_find_parallel_type (type, "___XVE");
    }
}

/* Assuming that TEMPL_TYPE is a union or struct type, returns
   non-zero iff field FIELD_NUM of TEMPL_TYPE has dynamic size. */

static int
is_dynamic_field (struct type *templ_type, int field_num)
{
  const char *name = TYPE_FIELD_NAME (templ_type, field_num);
  return name != NULL
    && TYPE_CODE (TYPE_FIELD_TYPE (templ_type, field_num)) == TYPE_CODE_PTR
    && strstr (name, "___XVL") != NULL;
}

/* Assuming that TYPE is a struct type, returns non-zero iff TYPE
   contains a variant part. */

static int
contains_variant_part (struct type *type)
{
  int f;

  if (type == NULL || TYPE_CODE (type) != TYPE_CODE_STRUCT
      || TYPE_NFIELDS (type) <= 0)
    return 0;
  return ada_is_variant_part (type, TYPE_NFIELDS (type) - 1);
}

/* A record type with no fields, . */
static struct type *
empty_record (struct objfile *objfile)
{
  struct type *type = alloc_type (objfile);
  TYPE_CODE (type) = TYPE_CODE_STRUCT;
  TYPE_NFIELDS (type) = 0;
  TYPE_FIELDS (type) = NULL;
  TYPE_NAME (type) = "<empty>";
  TYPE_TAG_NAME (type) = NULL;
  TYPE_FLAGS (type) = 0;
  TYPE_LENGTH (type) = 0;
  return type;
}

/* An ordinary record type (with fixed-length fields) that describes
   the value of type TYPE at VALADDR or ADDRESS (see comments at 
   the beginning of this section) VAL according to GNAT conventions.  
   DVAL0 should describe the (portion of a) record that contains any 
   necessary discriminants.  It should be NULL if VALUE_TYPE (VAL) is
   an outer-level type (i.e., as opposed to a branch of a variant.)  A
   variant field (unless unchecked) is replaced by a particular branch
   of the variant. */
/* NOTE: Limitations: For now, we assume that dynamic fields and
 * variants occupy whole numbers of bytes.  However, they need not be
 * byte-aligned.  */

static struct type *
template_to_fixed_record_type (struct type *type, char *valaddr,
			       CORE_ADDR address, struct value *dval0)
{
  struct value *mark = value_mark ();
  struct value *dval;
  struct type *rtype;
  int nfields, bit_len;
  long off;
  int f;

  nfields = TYPE_NFIELDS (type);
  rtype = alloc_type (TYPE_OBJFILE (type));
  TYPE_CODE (rtype) = TYPE_CODE_STRUCT;
  INIT_CPLUS_SPECIFIC (rtype);
  TYPE_NFIELDS (rtype) = nfields;
  TYPE_FIELDS (rtype) = (struct field *)
    TYPE_ALLOC (rtype, nfields * sizeof (struct field));
  memset (TYPE_FIELDS (rtype), 0, sizeof (struct field) * nfields);
  TYPE_NAME (rtype) = ada_type_name (type);
  TYPE_TAG_NAME (rtype) = NULL;
  /* FIXME: TYPE_FLAG_FIXED_INSTANCE should be defined in
     gdbtypes.h */
  /*  TYPE_FLAGS (rtype) |= TYPE_FLAG_FIXED_INSTANCE; */

  off = 0;
  bit_len = 0;
  for (f = 0; f < nfields; f += 1)
    {
      int fld_bit_len, bit_incr;
      off =
	align_value (off,
		     field_alignment (type, f)) + TYPE_FIELD_BITPOS (type, f);
      /* NOTE: used to use field_offset above, but that causes
       * problems with really negative bit positions.  So, let's
       * rediscover why we needed field_offset and fix it properly. */
      TYPE_FIELD_BITPOS (rtype, f) = off;
      TYPE_FIELD_BITSIZE (rtype, f) = 0;
      TYPE_FIELD_STATIC_KIND (rtype, f) = 0;

      if (ada_is_variant_part (type, f))
	{
	  struct type *branch_type;

	  if (dval0 == NULL)
	    dval = value_from_contents_and_address (rtype, valaddr, address);
	  else
	    dval = dval0;

	  branch_type =
	    to_fixed_variant_branch_type
	    (TYPE_FIELD_TYPE (type, f),
	     cond_offset_host (valaddr, off / TARGET_CHAR_BIT),
	     cond_offset_target (address, off / TARGET_CHAR_BIT), dval);
	  if (branch_type == NULL)
	    TYPE_NFIELDS (rtype) -= 1;
	  else
	    {
	      TYPE_FIELD_TYPE (rtype, f) = branch_type;
	      TYPE_FIELD_NAME (rtype, f) = "S";
	    }
	  bit_incr = 0;
	  fld_bit_len =
	    TYPE_LENGTH (TYPE_FIELD_TYPE (rtype, f)) * TARGET_CHAR_BIT;
	}
      else if (is_dynamic_field (type, f))
	{
	  if (dval0 == NULL)
	    dval = value_from_contents_and_address (rtype, valaddr, address);
	  else
	    dval = dval0;

	  TYPE_FIELD_TYPE (rtype, f) =
	    ada_to_fixed_type
	    (ada_get_base_type
	     (TYPE_TARGET_TYPE (TYPE_FIELD_TYPE (type, f))),
	     cond_offset_host (valaddr, off / TARGET_CHAR_BIT),
	     cond_offset_target (address, off / TARGET_CHAR_BIT), dval);
	  TYPE_FIELD_NAME (rtype, f) = TYPE_FIELD_NAME (type, f);
	  bit_incr = fld_bit_len =
	    TYPE_LENGTH (TYPE_FIELD_TYPE (rtype, f)) * TARGET_CHAR_BIT;
	}
      else
	{
	  TYPE_FIELD_TYPE (rtype, f) = TYPE_FIELD_TYPE (type, f);
	  TYPE_FIELD_NAME (rtype, f) = TYPE_FIELD_NAME (type, f);
	  if (TYPE_FIELD_BITSIZE (type, f) > 0)
	    bit_incr = fld_bit_len =
	      TYPE_FIELD_BITSIZE (rtype, f) = TYPE_FIELD_BITSIZE (type, f);
	  else
	    bit_incr = fld_bit_len =
	      TYPE_LENGTH (TYPE_FIELD_TYPE (type, f)) * TARGET_CHAR_BIT;
	}
      if (off + fld_bit_len > bit_len)
	bit_len = off + fld_bit_len;
      off += bit_incr;
      TYPE_LENGTH (rtype) = bit_len / TARGET_CHAR_BIT;
    }
  TYPE_LENGTH (rtype) = align_value (TYPE_LENGTH (rtype), TYPE_LENGTH (type));

  value_free_to_mark (mark);
  if (TYPE_LENGTH (rtype) > varsize_limit)
    error ("record type with dynamic size is larger than varsize-limit");
  return rtype;
}

/* As for template_to_fixed_record_type, but uses no run-time values.
   As a result, this type can only be approximate, but that's OK,
   since it is used only for type determinations.   Works on both
   structs and unions.
   Representation note: to save space, we memoize the result of this
   function in the TYPE_TARGET_TYPE of the template type. */

static struct type *
template_to_static_fixed_type (struct type *templ_type)
{
  struct type *type;
  int nfields;
  int f;

  if (TYPE_TARGET_TYPE (templ_type) != NULL)
    return TYPE_TARGET_TYPE (templ_type);

  nfields = TYPE_NFIELDS (templ_type);
  TYPE_TARGET_TYPE (templ_type) = type =
    alloc_type (TYPE_OBJFILE (templ_type));
  TYPE_CODE (type) = TYPE_CODE (templ_type);
  INIT_CPLUS_SPECIFIC (type);
  TYPE_NFIELDS (type) = nfields;
  TYPE_FIELDS (type) = (struct field *)
    TYPE_ALLOC (type, nfields * sizeof (struct field));
  memset (TYPE_FIELDS (type), 0, sizeof (struct field) * nfields);
  TYPE_NAME (type) = ada_type_name (templ_type);
  TYPE_TAG_NAME (type) = NULL;
  /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
  /*  TYPE_FLAGS (type) |= TYPE_FLAG_FIXED_INSTANCE; */
  TYPE_LENGTH (type) = 0;

  for (f = 0; f < nfields; f += 1)
    {
      TYPE_FIELD_BITPOS (type, f) = 0;
      TYPE_FIELD_BITSIZE (type, f) = 0;
      TYPE_FIELD_STATIC_KIND (type, f) = 0;

      if (is_dynamic_field (templ_type, f))
	{
	  TYPE_FIELD_TYPE (type, f) =
	    to_static_fixed_type (TYPE_TARGET_TYPE
				  (TYPE_FIELD_TYPE (templ_type, f)));
	  TYPE_FIELD_NAME (type, f) = TYPE_FIELD_NAME (templ_type, f);
	}
      else
	{
	  TYPE_FIELD_TYPE (type, f) =
	    check_typedef (TYPE_FIELD_TYPE (templ_type, f));
	  TYPE_FIELD_NAME (type, f) = TYPE_FIELD_NAME (templ_type, f);
	}
    }

  return type;
}

/* A revision of TYPE0 -- a non-dynamic-sized record with a variant
   part -- in which the variant part is replaced with the appropriate
   branch. */
static struct type *
to_record_with_fixed_variant_part (struct type *type, char *valaddr,
				   CORE_ADDR address, struct value *dval)
{
  struct value *mark = value_mark ();
  struct type *rtype;
  struct type *branch_type;
  int nfields = TYPE_NFIELDS (type);

  if (dval == NULL)
    return type;

  rtype = alloc_type (TYPE_OBJFILE (type));
  TYPE_CODE (rtype) = TYPE_CODE_STRUCT;
  INIT_CPLUS_SPECIFIC (type);
  TYPE_NFIELDS (rtype) = TYPE_NFIELDS (type);
  TYPE_FIELDS (rtype) =
    (struct field *) TYPE_ALLOC (rtype, nfields * sizeof (struct field));
  memcpy (TYPE_FIELDS (rtype), TYPE_FIELDS (type),
	  sizeof (struct field) * nfields);
  TYPE_NAME (rtype) = ada_type_name (type);
  TYPE_TAG_NAME (rtype) = NULL;
  /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
  /*  TYPE_FLAGS (rtype) |= TYPE_FLAG_FIXED_INSTANCE; */
  TYPE_LENGTH (rtype) = TYPE_LENGTH (type);

  branch_type =
    to_fixed_variant_branch_type
    (TYPE_FIELD_TYPE (type, nfields - 1),
     cond_offset_host (valaddr,
		       TYPE_FIELD_BITPOS (type,
					  nfields - 1) / TARGET_CHAR_BIT),
     cond_offset_target (address,
			 TYPE_FIELD_BITPOS (type,
					    nfields - 1) / TARGET_CHAR_BIT),
     dval);
  if (branch_type == NULL)
    {
      TYPE_NFIELDS (rtype) -= 1;
      TYPE_LENGTH (rtype) -=
	TYPE_LENGTH (TYPE_FIELD_TYPE (type, nfields - 1));
    }
  else
    {
      TYPE_FIELD_TYPE (rtype, nfields - 1) = branch_type;
      TYPE_FIELD_NAME (rtype, nfields - 1) = "S";
      TYPE_FIELD_BITSIZE (rtype, nfields - 1) = 0;
      TYPE_FIELD_STATIC_KIND (rtype, nfields - 1) = 0;
      TYPE_LENGTH (rtype) += TYPE_LENGTH (branch_type);
      -TYPE_LENGTH (TYPE_FIELD_TYPE (type, nfields - 1));
    }

  return rtype;
}

/* An ordinary record type (with fixed-length fields) that describes
   the value at (TYPE0, VALADDR, ADDRESS) [see explanation at
   beginning of this section].   Any necessary discriminants' values
   should be in DVAL, a record value; it should be NULL if the object
   at ADDR itself contains any necessary  discriminant values.  A
   variant field (unless unchecked) is replaced by a particular branch
   of the variant. */

static struct type *
to_fixed_record_type (struct type *type0, char *valaddr, CORE_ADDR address,
		      struct value *dval)
{
  struct type *templ_type;

  /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
  /*  if (TYPE_FLAGS (type0) & TYPE_FLAG_FIXED_INSTANCE)
     return type0;
   */
  templ_type = dynamic_template_type (type0);

  if (templ_type != NULL)
    return template_to_fixed_record_type (templ_type, valaddr, address, dval);
  else if (contains_variant_part (type0))
    return to_record_with_fixed_variant_part (type0, valaddr, address, dval);
  else
    {
      /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
      /*      TYPE_FLAGS (type0) |= TYPE_FLAG_FIXED_INSTANCE; */
      return type0;
    }

}

/* An ordinary record type (with fixed-length fields) that describes
   the value at (VAR_TYPE0, VALADDR, ADDRESS), where VAR_TYPE0 is a
   union type.  Any necessary discriminants' values should be in DVAL,
   a record value.  That is, this routine selects the appropriate
   branch of the union at ADDR according to the discriminant value
   indicated in the union's type name. */

static struct type *
to_fixed_variant_branch_type (struct type *var_type0, char *valaddr,
			      CORE_ADDR address, struct value *dval)
{
  int which;
  struct type *templ_type;
  struct type *var_type;

  if (TYPE_CODE (var_type0) == TYPE_CODE_PTR)
    var_type = TYPE_TARGET_TYPE (var_type0);
  else
    var_type = var_type0;

  templ_type = ada_find_parallel_type (var_type, "___XVU");

  if (templ_type != NULL)
    var_type = templ_type;

  which =
    ada_which_variant_applies (var_type,
			       VALUE_TYPE (dval), VALUE_CONTENTS (dval));

  if (which < 0)
    return empty_record (TYPE_OBJFILE (var_type));
  else if (is_dynamic_field (var_type, which))
    return
      to_fixed_record_type
      (TYPE_TARGET_TYPE (TYPE_FIELD_TYPE (var_type, which)),
       valaddr, address, dval);
  else if (contains_variant_part (TYPE_FIELD_TYPE (var_type, which)))
    return
      to_fixed_record_type
      (TYPE_FIELD_TYPE (var_type, which), valaddr, address, dval);
  else
    return TYPE_FIELD_TYPE (var_type, which);
}

/* Assuming that TYPE0 is an array type describing the type of a value
   at ADDR, and that DVAL describes a record containing any
   discriminants used in TYPE0, returns a type for the value that
   contains no dynamic components (that is, no components whose sizes
   are determined by run-time quantities).  Unless IGNORE_TOO_BIG is
   true, gives an error message if the resulting type's size is over
   varsize_limit.
*/

static struct type *
to_fixed_array_type (struct type *type0, struct value *dval,
		     int ignore_too_big)
{
  struct type *index_type_desc;
  struct type *result;

  /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
/*  if (ada_is_packed_array_type (type0)  /* revisit? *//*
   || (TYPE_FLAGS (type0) & TYPE_FLAG_FIXED_INSTANCE))
   return type0; */

  index_type_desc = ada_find_parallel_type (type0, "___XA");
  if (index_type_desc == NULL)
    {
      struct type *elt_type0 = check_typedef (TYPE_TARGET_TYPE (type0));
      /* NOTE: elt_type---the fixed version of elt_type0---should never
       * depend on the contents of the array in properly constructed
       * debugging data. */
      struct type *elt_type = ada_to_fixed_type (elt_type0, 0, 0, dval);

      if (elt_type0 == elt_type)
	result = type0;
      else
	result = create_array_type (alloc_type (TYPE_OBJFILE (type0)),
				    elt_type, TYPE_INDEX_TYPE (type0));
    }
  else
    {
      int i;
      struct type *elt_type0;

      elt_type0 = type0;
      for (i = TYPE_NFIELDS (index_type_desc); i > 0; i -= 1)
	elt_type0 = TYPE_TARGET_TYPE (elt_type0);

      /* NOTE: result---the fixed version of elt_type0---should never
       * depend on the contents of the array in properly constructed
       * debugging data. */
      result = ada_to_fixed_type (check_typedef (elt_type0), 0, 0, dval);
      for (i = TYPE_NFIELDS (index_type_desc) - 1; i >= 0; i -= 1)
	{
	  struct type *range_type =
	    to_fixed_range_type (TYPE_FIELD_NAME (index_type_desc, i),
				 dval, TYPE_OBJFILE (type0));
	  result = create_array_type (alloc_type (TYPE_OBJFILE (type0)),
				      result, range_type);
	}
      if (!ignore_too_big && TYPE_LENGTH (result) > varsize_limit)
	error ("array type with dynamic size is larger than varsize-limit");
    }

/* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
/*  TYPE_FLAGS (result) |= TYPE_FLAG_FIXED_INSTANCE; */
  return result;
}


/* A standard type (containing no dynamically sized components)
   corresponding to TYPE for the value (TYPE, VALADDR, ADDRESS)
   DVAL describes a record containing any discriminants used in TYPE0,
   and may be NULL if there are none. */

struct type *
ada_to_fixed_type (struct type *type, char *valaddr, CORE_ADDR address,
		   struct value *dval)
{
  CHECK_TYPEDEF (type);
  switch (TYPE_CODE (type))
    {
    default:
      return type;
    case TYPE_CODE_STRUCT:
      return to_fixed_record_type (type, valaddr, address, NULL);
    case TYPE_CODE_ARRAY:
      return to_fixed_array_type (type, dval, 0);
    case TYPE_CODE_UNION:
      if (dval == NULL)
	return type;
      else
	return to_fixed_variant_branch_type (type, valaddr, address, dval);
    }
}

/* A standard (static-sized) type corresponding as well as possible to
   TYPE0, but based on no runtime data. */

static struct type *
to_static_fixed_type (struct type *type0)
{
  struct type *type;

  if (type0 == NULL)
    return NULL;

  /* FIXME:  TYPE_FLAG_FIXED_INSTANCE should be defined in gdbtypes.h */
  /*  if (TYPE_FLAGS (type0) & TYPE_FLAG_FIXED_INSTANCE)
     return type0;
   */
  CHECK_TYPEDEF (type0);

  switch (TYPE_CODE (type0))
    {
    default:
      return type0;
    case TYPE_CODE_STRUCT:
      type = dynamic_template_type (type0);
      if (type != NULL)
	return template_to_static_fixed_type (type);
      return type0;
    case TYPE_CODE_UNION:
      type = ada_find_parallel_type (type0, "___XVU");
      if (type != NULL)
	return template_to_static_fixed_type (type);
      return type0;
    }
}

/* A static approximation of TYPE with all type wrappers removed. */
static struct type *
static_unwrap_type (struct type *type)
{
  if (ada_is_aligner_type (type))
    {
      struct type *type1 = TYPE_FIELD_TYPE (check_typedef (type), 0);
      if (ada_type_name (type1) == NULL)
	TYPE_NAME (type1) = ada_type_name (type);

      return static_unwrap_type (type1);
    }
  else
    {
      struct type *raw_real_type = ada_get_base_type (type);
      if (raw_real_type == type)
	return type;
      else
	return to_static_fixed_type (raw_real_type);
    }
}

/* In some cases, incomplete and private types require
   cross-references that are not resolved as records (for example, 
      type Foo;
      type FooP is access Foo;
      V: FooP;
      type Foo is array ...;
   ). In these cases, since there is no mechanism for producing 
   cross-references to such types, we instead substitute for FooP a
   stub enumeration type that is nowhere resolved, and whose tag is
   the name of the actual type.  Call these types "non-record stubs". */

/* A type equivalent to TYPE that is not a non-record stub, if one
   exists, otherwise TYPE. */
struct type *
ada_completed_type (struct type *type)
{
  CHECK_TYPEDEF (type);
  if (type == NULL || TYPE_CODE (type) != TYPE_CODE_ENUM
      || (TYPE_FLAGS (type) & TYPE_FLAG_STUB) == 0
      || TYPE_TAG_NAME (type) == NULL)
    return type;
  else
    {
      char *name = TYPE_TAG_NAME (type);
      struct type *type1 = ada_find_any_type (name);
      return (type1 == NULL) ? type : type1;
    }
}

/* A value representing the data at VALADDR/ADDRESS as described by
   type TYPE0, but with a standard (static-sized) type that correctly
   describes it.  If VAL0 is not NULL and TYPE0 already is a standard
   type, then return VAL0 [this feature is simply to avoid redundant
   creation of struct values]. */

struct value *
ada_to_fixed_value (struct type *type0, char *valaddr, CORE_ADDR address,
		    struct value *val0)
{
  struct type *type = ada_to_fixed_type (type0, valaddr, address, NULL);
  if (type == type0 && val0 != NULL)
    return val0;
  else
    return value_from_contents_and_address (type, valaddr, address);
}

/* A value representing VAL, but with a standard (static-sized) type 
   chosen to approximate the real type of VAL as well as possible, but
   without consulting any runtime values.  For Ada dynamic-sized
   types, therefore, the type of the result is likely to be inaccurate. */

struct value *
ada_to_static_fixed_value (struct value *val)
{
  struct type *type =
    to_static_fixed_type (static_unwrap_type (VALUE_TYPE (val)));
  if (type == VALUE_TYPE (val))
    return val;
  else
    return coerce_unspec_val_to_type (val, 0, type);
}





/* Attributes */

/* Table mapping attribute numbers to names */
/* NOTE: Keep up to date with enum ada_attribute definition in ada-lang.h */

static const char *attribute_names[] = {
  "<?>",

  "first",
  "last",
  "length",
  "image",
  "img",
  "max",
  "min",
  "pos" "tag",
  "val",

  0
};

const char *
ada_attribute_name (int n)
{
  if (n > 0 && n < (int) ATR_END)
    return attribute_names[n];
  else
    return attribute_names[0];
}

/* Evaluate the 'POS attribute applied to ARG. */

static struct value *
value_pos_atr (struct value *arg)
{
  struct type *type = VALUE_TYPE (arg);

  if (!discrete_type_p (type))
    error ("'POS only defined on discrete types");

  if (TYPE_CODE (type) == TYPE_CODE_ENUM)
    {
      int i;
      LONGEST v = value_as_long (arg);

      for (i = 0; i < TYPE_NFIELDS (type); i += 1)
	{
	  if (v == TYPE_FIELD_BITPOS (type, i))
	    return value_from_longest (builtin_type_ada_int, i);
	}
      error ("enumeration value is invalid: can't find 'POS");
    }
  else
    return value_from_longest (builtin_type_ada_int, value_as_long (arg));
}

/* Evaluate the TYPE'VAL attribute applied to ARG. */

static struct value *
value_val_atr (struct type *type, struct value *arg)
{
  if (!discrete_type_p (type))
    error ("'VAL only defined on discrete types");
  if (!integer_type_p (VALUE_TYPE (arg)))
    error ("'VAL requires integral argument");

  if (TYPE_CODE (type) == TYPE_CODE_ENUM)
    {
      long pos = value_as_long (arg);
      if (pos < 0 || pos >= TYPE_NFIELDS (type))
	error ("argument to 'VAL out of range");
      return value_from_longest (type, TYPE_FIELD_BITPOS (type, pos));
    }
  else
    return value_from_longest (type, value_as_long (arg));
}


				/* Evaluation */

/* True if TYPE appears to be an Ada character type.  
 * [At the moment, this is true only for Character and Wide_Character;
 * It is a heuristic test that could stand improvement]. */

int
ada_is_character_type (struct type *type)
{
  const char *name = ada_type_name (type);
  return
    name != NULL
    && (TYPE_CODE (type) == TYPE_CODE_CHAR
	|| TYPE_CODE (type) == TYPE_CODE_INT
	|| TYPE_CODE (type) == TYPE_CODE_RANGE)
    && (DEPRECATED_STREQ (name, "character") || DEPRECATED_STREQ (name, "wide_character")
	|| DEPRECATED_STREQ (name, "unsigned char"));
}

/* True if TYPE appears to be an Ada string type. */

int
ada_is_string_type (struct type *type)
{
  CHECK_TYPEDEF (type);
  if (type != NULL
      && TYPE_CODE (type) != TYPE_CODE_PTR
      && (ada_is_simple_array (type) || ada_is_array_descriptor (type))
      && ada_array_arity (type) == 1)
    {
      struct type *elttype = ada_array_element_type (type, 1);

      return ada_is_character_type (elttype);
    }
  else
    return 0;
}


/* True if TYPE is a struct type introduced by the compiler to force the
   alignment of a value.  Such types have a single field with a
   distinctive name. */

int
ada_is_aligner_type (struct type *type)
{
  CHECK_TYPEDEF (type);
  return (TYPE_CODE (type) == TYPE_CODE_STRUCT
	  && TYPE_NFIELDS (type) == 1
	  && DEPRECATED_STREQ (TYPE_FIELD_NAME (type, 0), "F"));
}

/* If there is an ___XVS-convention type parallel to SUBTYPE, return
   the parallel type. */

struct type *
ada_get_base_type (struct type *raw_type)
{
  struct type *real_type_namer;
  struct type *raw_real_type;
  struct type *real_type;

  if (raw_type == NULL || TYPE_CODE (raw_type) != TYPE_CODE_STRUCT)
    return raw_type;

  real_type_namer = ada_find_parallel_type (raw_type, "___XVS");
  if (real_type_namer == NULL
      || TYPE_CODE (real_type_namer) != TYPE_CODE_STRUCT
      || TYPE_NFIELDS (real_type_namer) != 1)
    return raw_type;

  raw_real_type = ada_find_any_type (TYPE_FIELD_NAME (real_type_namer, 0));
  if (raw_real_type == NULL)
    return raw_type;
  else
    return raw_real_type;
}

/* The type of value designated by TYPE, with all aligners removed. */

struct type *
ada_aligned_type (struct type *type)
{
  if (ada_is_aligner_type (type))
    return ada_aligned_type (TYPE_FIELD_TYPE (type, 0));
  else
    return ada_get_base_type (type);
}


/* The address of the aligned value in an object at address VALADDR
   having type TYPE.  Assumes ada_is_aligner_type (TYPE). */

char *
ada_aligned_value_addr (struct type *type, char *valaddr)
{
  if (ada_is_aligner_type (type))
    return ada_aligned_value_addr (TYPE_FIELD_TYPE (type, 0),
				   valaddr +
				   TYPE_FIELD_BITPOS (type,
						      0) / TARGET_CHAR_BIT);
  else
    return valaddr;
}

/* The printed representation of an enumeration literal with encoded
   name NAME. The value is good to the next call of ada_enum_name. */
const char *
ada_enum_name (const char *name)
{
  char *tmp;

  while (1)
    {
      if ((tmp = strstr (name, "__")) != NULL)
	name = tmp + 2;
      else if ((tmp = strchr (name, '.')) != NULL)
	name = tmp + 1;
      else
	break;
    }

  if (name[0] == 'Q')
    {
      static char result[16];
      int v;
      if (name[1] == 'U' || name[1] == 'W')
	{
	  if (sscanf (name + 2, "%x", &v) != 1)
	    return name;
	}
      else
	return name;

      if (isascii (v) && isprint (v))
	sprintf (result, "'%c'", v);
      else if (name[1] == 'U')
	sprintf (result, "[\"%02x\"]", v);
      else
	sprintf (result, "[\"%04x\"]", v);

      return result;
    }
  else
    return name;
}

static struct value *
evaluate_subexp (struct type *expect_type, struct expression *exp, int *pos,
		 enum noside noside)
{
  return (*exp->language_defn->evaluate_exp) (expect_type, exp, pos, noside);
}

/* Evaluate the subexpression of EXP starting at *POS as for
   evaluate_type, updating *POS to point just past the evaluated
   expression. */

static struct value *
evaluate_subexp_type (struct expression *exp, int *pos)
{
  return (*exp->language_defn->evaluate_exp)
    (NULL_TYPE, exp, pos, EVAL_AVOID_SIDE_EFFECTS);
}

/* If VAL is wrapped in an aligner or subtype wrapper, return the
   value it wraps. */

static struct value *
unwrap_value (struct value *val)
{
  struct type *type = check_typedef (VALUE_TYPE (val));
  if (ada_is_aligner_type (type))
    {
      struct value *v = value_struct_elt (&val, NULL, "F",
					  NULL, "internal structure");
      struct type *val_type = check_typedef (VALUE_TYPE (v));
      if (ada_type_name (val_type) == NULL)
	TYPE_NAME (val_type) = ada_type_name (type);

      return unwrap_value (v);
    }
  else
    {
      struct type *raw_real_type =
	ada_completed_type (ada_get_base_type (type));

      if (type == raw_real_type)
	return val;

      return
	coerce_unspec_val_to_type
	(val, 0, ada_to_fixed_type (raw_real_type, 0,
				    VALUE_ADDRESS (val) + VALUE_OFFSET (val),
				    NULL));
    }
}

static struct value *
cast_to_fixed (struct type *type, struct value *arg)
{
  LONGEST val;

  if (type == VALUE_TYPE (arg))
    return arg;
  else if (ada_is_fixed_point_type (VALUE_TYPE (arg)))
    val = ada_float_to_fixed (type,
			      ada_fixed_to_float (VALUE_TYPE (arg),
						  value_as_long (arg)));
  else
    {
      DOUBLEST argd =
	value_as_double (value_cast (builtin_type_double, value_copy (arg)));
      val = ada_float_to_fixed (type, argd);
    }

  return value_from_longest (type, val);
}

static struct value *
cast_from_fixed_to_double (struct value *arg)
{
  DOUBLEST val = ada_fixed_to_float (VALUE_TYPE (arg),
				     value_as_long (arg));
  return value_from_double (builtin_type_double, val);
}

/* Coerce VAL as necessary for assignment to an lval of type TYPE, and 
 * return the converted value. */
static struct value *
coerce_for_assign (struct type *type, struct value *val)
{
  struct type *type2 = VALUE_TYPE (val);
  if (type == type2)
    return val;

  CHECK_TYPEDEF (type2);
  CHECK_TYPEDEF (type);

  if (TYPE_CODE (type2) == TYPE_CODE_PTR
      && TYPE_CODE (type) == TYPE_CODE_ARRAY)
    {
      val = ada_value_ind (val);
      type2 = VALUE_TYPE (val);
    }

  if (TYPE_CODE (type2) == TYPE_CODE_ARRAY
      && TYPE_CODE (type) == TYPE_CODE_ARRAY)
    {
      if (TYPE_LENGTH (type2) != TYPE_LENGTH (type)
	  || TYPE_LENGTH (TYPE_TARGET_TYPE (type2))
	  != TYPE_LENGTH (TYPE_TARGET_TYPE (type2)))
	error ("Incompatible types in assignment");
      VALUE_TYPE (val) = type;
    }
  return val;
}

struct value *
ada_evaluate_subexp (struct type *expect_type, struct expression *exp,
		     int *pos, enum noside noside)
{
  enum exp_opcode op;
  enum ada_attribute atr;
  int tem, tem2, tem3;
  int pc;
  struct value *arg1 = NULL, *arg2 = NULL, *arg3;
  struct type *type;
  int nargs;
  struct value **argvec;

  pc = *pos;
  *pos += 1;
  op = exp->elts[pc].opcode;

  switch (op)
    {
    default:
      *pos -= 1;
      return
	unwrap_value (evaluate_subexp_standard
		      (expect_type, exp, pos, noside));

    case UNOP_CAST:
      (*pos) += 2;
      type = exp->elts[pc + 1].type;
      arg1 = evaluate_subexp (type, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (type != check_typedef (VALUE_TYPE (arg1)))
	{
	  if (ada_is_fixed_point_type (type))
	    arg1 = cast_to_fixed (type, arg1);
	  else if (ada_is_fixed_point_type (VALUE_TYPE (arg1)))
	    arg1 = value_cast (type, cast_from_fixed_to_double (arg1));
	  else if (VALUE_LVAL (arg1) == lval_memory)
	    {
	      /* This is in case of the really obscure (and undocumented,
	         but apparently expected) case of (Foo) Bar.all, where Bar 
	         is an integer constant and Foo is a dynamic-sized type.
	         If we don't do this, ARG1 will simply be relabeled with
	         TYPE. */
	      if (noside == EVAL_AVOID_SIDE_EFFECTS)
		return value_zero (to_static_fixed_type (type), not_lval);
	      arg1 =
		ada_to_fixed_value
		(type, 0, VALUE_ADDRESS (arg1) + VALUE_OFFSET (arg1), 0);
	    }
	  else
	    arg1 = value_cast (type, arg1);
	}
      return arg1;

      /* FIXME:  UNOP_QUAL should be defined in expression.h */
      /*    case UNOP_QUAL:
         (*pos) += 2;
         type = exp->elts[pc + 1].type;
         return ada_evaluate_subexp (type, exp, pos, noside);
       */
    case BINOP_ASSIGN:
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      arg2 = evaluate_subexp (VALUE_TYPE (arg1), exp, pos, noside);
      if (noside == EVAL_SKIP || noside == EVAL_AVOID_SIDE_EFFECTS)
	return arg1;
      if (binop_user_defined_p (op, arg1, arg2))
	return value_x_binop (arg1, arg2, op, OP_NULL, EVAL_NORMAL);
      else
	{
	  if (ada_is_fixed_point_type (VALUE_TYPE (arg1)))
	    arg2 = cast_to_fixed (VALUE_TYPE (arg1), arg2);
	  else if (ada_is_fixed_point_type (VALUE_TYPE (arg2)))
	    error
	      ("Fixed-point values must be assigned to fixed-point variables");
	  else
	    arg2 = coerce_for_assign (VALUE_TYPE (arg1), arg2);
	  return ada_value_assign (arg1, arg2);
	}

    case BINOP_ADD:
      arg1 = evaluate_subexp_with_coercion (exp, pos, noside);
      arg2 = evaluate_subexp_with_coercion (exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (binop_user_defined_p (op, arg1, arg2))
	return value_x_binop (arg1, arg2, op, OP_NULL, EVAL_NORMAL);
      else
	{
	  if ((ada_is_fixed_point_type (VALUE_TYPE (arg1))
	       || ada_is_fixed_point_type (VALUE_TYPE (arg2)))
	      && VALUE_TYPE (arg1) != VALUE_TYPE (arg2))
	    error
	      ("Operands of fixed-point addition must have the same type");
	  return value_cast (VALUE_TYPE (arg1), value_add (arg1, arg2));
	}

    case BINOP_SUB:
      arg1 = evaluate_subexp_with_coercion (exp, pos, noside);
      arg2 = evaluate_subexp_with_coercion (exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (binop_user_defined_p (op, arg1, arg2))
	return value_x_binop (arg1, arg2, op, OP_NULL, EVAL_NORMAL);
      else
	{
	  if ((ada_is_fixed_point_type (VALUE_TYPE (arg1))
	       || ada_is_fixed_point_type (VALUE_TYPE (arg2)))
	      && VALUE_TYPE (arg1) != VALUE_TYPE (arg2))
	    error
	      ("Operands of fixed-point subtraction must have the same type");
	  return value_cast (VALUE_TYPE (arg1), value_sub (arg1, arg2));
	}

    case BINOP_MUL:
    case BINOP_DIV:
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      arg2 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (binop_user_defined_p (op, arg1, arg2))
	return value_x_binop (arg1, arg2, op, OP_NULL, EVAL_NORMAL);
      else
	if (noside == EVAL_AVOID_SIDE_EFFECTS
	    && (op == BINOP_DIV || op == BINOP_REM || op == BINOP_MOD))
	return value_zero (VALUE_TYPE (arg1), not_lval);
      else
	{
	  if (ada_is_fixed_point_type (VALUE_TYPE (arg1)))
	    arg1 = cast_from_fixed_to_double (arg1);
	  if (ada_is_fixed_point_type (VALUE_TYPE (arg2)))
	    arg2 = cast_from_fixed_to_double (arg2);
	  return value_binop (arg1, arg2, op);
	}

    case UNOP_NEG:
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (unop_user_defined_p (op, arg1))
	return value_x_unop (arg1, op, EVAL_NORMAL);
      else if (ada_is_fixed_point_type (VALUE_TYPE (arg1)))
	return value_cast (VALUE_TYPE (arg1), value_neg (arg1));
      else
	return value_neg (arg1);

      /* FIXME:  OP_UNRESOLVED_VALUE should be defined in expression.h */
      /*    case OP_UNRESOLVED_VALUE:
         /* Only encountered when an unresolved symbol occurs in a
         context other than a function call, in which case, it is
   illegal. *//*
   (*pos) += 3;
   if (noside == EVAL_SKIP)
   goto nosideret;
   else 
   error ("Unexpected unresolved symbol, %s, during evaluation",
   ada_demangle (exp->elts[pc + 2].name));
 */
    case OP_VAR_VALUE:
      *pos -= 1;
      if (noside == EVAL_SKIP)
	{
	  *pos += 4;
	  goto nosideret;
	}
      else if (noside == EVAL_AVOID_SIDE_EFFECTS)
	{
	  *pos += 4;
	  return value_zero
	    (to_static_fixed_type
	     (static_unwrap_type (SYMBOL_TYPE (exp->elts[pc + 2].symbol))),
	     not_lval);
	}
      else
	{
	  arg1 =
	    unwrap_value (evaluate_subexp_standard
			  (expect_type, exp, pos, noside));
	  return ada_to_fixed_value (VALUE_TYPE (arg1), 0,
				     VALUE_ADDRESS (arg1) +
				     VALUE_OFFSET (arg1), arg1);
	}

    case OP_ARRAY:
      (*pos) += 3;
      tem2 = longest_to_int (exp->elts[pc + 1].longconst);
      tem3 = longest_to_int (exp->elts[pc + 2].longconst);
      nargs = tem3 - tem2 + 1;
      type = expect_type ? check_typedef (expect_type) : NULL_TYPE;

      argvec =
	(struct value * *) alloca (sizeof (struct value *) * (nargs + 1));
      for (tem = 0; tem == 0 || tem < nargs; tem += 1)
	/* At least one element gets inserted for the type */
	{
	  /* Ensure that array expressions are coerced into pointer objects. */
	  argvec[tem] = evaluate_subexp_with_coercion (exp, pos, noside);
	}
      if (noside == EVAL_SKIP)
	goto nosideret;
      return value_array (tem2, tem3, argvec);

    case OP_FUNCALL:
      (*pos) += 2;

      /* Allocate arg vector, including space for the function to be
         called in argvec[0] and a terminating NULL */
      nargs = longest_to_int (exp->elts[pc + 1].longconst);
      argvec =
	(struct value * *) alloca (sizeof (struct value *) * (nargs + 2));

      /* FIXME: OP_UNRESOLVED_VALUE should be defined in expression.h */
      /* FIXME: name should be defined in expresion.h */
      /*      if (exp->elts[*pos].opcode == OP_UNRESOLVED_VALUE)
         error ("Unexpected unresolved symbol, %s, during evaluation",
         ada_demangle (exp->elts[pc + 5].name));
       */
      if (0)
	{
	  error ("unexpected code path, FIXME");
	}
      else
	{
	  for (tem = 0; tem <= nargs; tem += 1)
	    argvec[tem] = evaluate_subexp (NULL_TYPE, exp, pos, noside);
	  argvec[tem] = 0;

	  if (noside == EVAL_SKIP)
	    goto nosideret;
	}

      if (TYPE_CODE (VALUE_TYPE (argvec[0])) == TYPE_CODE_REF)
	argvec[0] = value_addr (argvec[0]);

      if (ada_is_packed_array_type (VALUE_TYPE (argvec[0])))
	argvec[0] = ada_coerce_to_simple_array (argvec[0]);

      type = check_typedef (VALUE_TYPE (argvec[0]));
      if (TYPE_CODE (type) == TYPE_CODE_PTR)
	{
	  switch (TYPE_CODE (check_typedef (TYPE_TARGET_TYPE (type))))
	    {
	    case TYPE_CODE_FUNC:
	      type = check_typedef (TYPE_TARGET_TYPE (type));
	      break;
	    case TYPE_CODE_ARRAY:
	      break;
	    case TYPE_CODE_STRUCT:
	      if (noside != EVAL_AVOID_SIDE_EFFECTS)
		argvec[0] = ada_value_ind (argvec[0]);
	      type = check_typedef (TYPE_TARGET_TYPE (type));
	      break;
	    default:
	      error ("cannot subscript or call something of type `%s'",
		     ada_type_name (VALUE_TYPE (argvec[0])));
	      break;
	    }
	}

      switch (TYPE_CODE (type))
	{
	case TYPE_CODE_FUNC:
	  if (noside == EVAL_AVOID_SIDE_EFFECTS)
	    return allocate_value (TYPE_TARGET_TYPE (type));
	  return call_function_by_hand (argvec[0], nargs, argvec + 1);
	case TYPE_CODE_STRUCT:
	  {
	    int arity = ada_array_arity (type);
	    type = ada_array_element_type (type, nargs);
	    if (type == NULL)
	      error ("cannot subscript or call a record");
	    if (arity != nargs)
	      error ("wrong number of subscripts; expecting %d", arity);
	    if (noside == EVAL_AVOID_SIDE_EFFECTS)
	      return allocate_value (ada_aligned_type (type));
	    return
	      unwrap_value (ada_value_subscript
			    (argvec[0], nargs, argvec + 1));
	  }
	case TYPE_CODE_ARRAY:
	  if (noside == EVAL_AVOID_SIDE_EFFECTS)
	    {
	      type = ada_array_element_type (type, nargs);
	      if (type == NULL)
		error ("element type of array unknown");
	      else
		return allocate_value (ada_aligned_type (type));
	    }
	  return
	    unwrap_value (ada_value_subscript
			  (ada_coerce_to_simple_array (argvec[0]),
			   nargs, argvec + 1));
	case TYPE_CODE_PTR:	/* Pointer to array */
	  type = to_fixed_array_type (TYPE_TARGET_TYPE (type), NULL, 1);
	  if (noside == EVAL_AVOID_SIDE_EFFECTS)
	    {
	      type = ada_array_element_type (type, nargs);
	      if (type == NULL)
		error ("element type of array unknown");
	      else
		return allocate_value (ada_aligned_type (type));
	    }
	  return
	    unwrap_value (ada_value_ptr_subscript (argvec[0], type,
						   nargs, argvec + 1));

	default:
	  error ("Internal error in evaluate_subexp");
	}

    case TERNOP_SLICE:
      {
	struct value *array = evaluate_subexp (NULL_TYPE, exp, pos, noside);
	int lowbound
	  = value_as_long (evaluate_subexp (NULL_TYPE, exp, pos, noside));
	int upper
	  = value_as_long (evaluate_subexp (NULL_TYPE, exp, pos, noside));
	if (noside == EVAL_SKIP)
	  goto nosideret;

	/* If this is a reference to an array, then dereference it */
	if (TYPE_CODE (VALUE_TYPE (array)) == TYPE_CODE_REF
	    && TYPE_TARGET_TYPE (VALUE_TYPE (array)) != NULL
	    && TYPE_CODE (TYPE_TARGET_TYPE (VALUE_TYPE (array))) ==
	    TYPE_CODE_ARRAY
	    && !ada_is_array_descriptor (check_typedef (VALUE_TYPE (array))))
	  {
	    array = ada_coerce_ref (array);
	  }

	if (noside == EVAL_AVOID_SIDE_EFFECTS &&
	    ada_is_array_descriptor (check_typedef (VALUE_TYPE (array))))
	  {
	    /* Try to dereference the array, in case it is an access to array */
	    struct type *arrType = ada_type_of_array (array, 0);
	    if (arrType != NULL)
	      array = value_at_lazy (arrType, 0, NULL);
	  }
	if (ada_is_array_descriptor (VALUE_TYPE (array)))
	  array = ada_coerce_to_simple_array (array);

	/* If at this point we have a pointer to an array, it means that
	   it is a pointer to a simple (non-ada) array. We just then
	   dereference it */
	if (TYPE_CODE (VALUE_TYPE (array)) == TYPE_CODE_PTR
	    && TYPE_TARGET_TYPE (VALUE_TYPE (array)) != NULL
	    && TYPE_CODE (TYPE_TARGET_TYPE (VALUE_TYPE (array))) ==
	    TYPE_CODE_ARRAY)
	  {
	    array = ada_value_ind (array);
	  }

	if (noside == EVAL_AVOID_SIDE_EFFECTS)
	  /* The following will get the bounds wrong, but only in contexts
	     where the value is not being requested (FIXME?). */
	  return array;
	else
	  return value_slice (array, lowbound, upper - lowbound + 1);
      }

      /* FIXME: UNOP_MBR should be defined in expression.h */
      /*    case UNOP_MBR:
         (*pos) += 2;
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         type = exp->elts[pc + 1].type;

         if (noside == EVAL_SKIP)
         goto nosideret;

         switch (TYPE_CODE (type)) 
         {
         default:
         warning ("Membership test incompletely implemented; always returns true");
         return value_from_longest (builtin_type_int, (LONGEST) 1);

         case TYPE_CODE_RANGE:
         arg2 = value_from_longest (builtin_type_int, 
         (LONGEST) TYPE_LOW_BOUND (type));
         arg3 = value_from_longest (builtin_type_int, 
         (LONGEST) TYPE_HIGH_BOUND (type));
         return 
         value_from_longest (builtin_type_int,
         (value_less (arg1,arg3) 
         || value_equal (arg1,arg3))
         && (value_less (arg2,arg1)
         || value_equal (arg2,arg1)));
         }
       */
      /* FIXME: BINOP_MBR should be defined in expression.h */
      /*    case BINOP_MBR:
         (*pos) += 2;
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         arg2 = evaluate_subexp (NULL_TYPE, exp, pos, noside);

         if (noside == EVAL_SKIP)
         goto nosideret;

         if (noside == EVAL_AVOID_SIDE_EFFECTS)
         return value_zero (builtin_type_int, not_lval);

         tem = longest_to_int (exp->elts[pc + 1].longconst);

         if (tem < 1 || tem > ada_array_arity (VALUE_TYPE (arg2)))
         error ("invalid dimension number to '%s", "range");

         arg3 = ada_array_bound (arg2, tem, 1);
         arg2 = ada_array_bound (arg2, tem, 0);

         return 
         value_from_longest (builtin_type_int,
         (value_less (arg1,arg3) 
         || value_equal (arg1,arg3))
         && (value_less (arg2,arg1)
         || value_equal (arg2,arg1)));
       */
      /* FIXME: TERNOP_MBR should be defined in expression.h */
      /*    case TERNOP_MBR:
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         arg2 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         arg3 = evaluate_subexp (NULL_TYPE, exp, pos, noside);

         if (noside == EVAL_SKIP)
         goto nosideret;

         return 
         value_from_longest (builtin_type_int,
         (value_less (arg1,arg3) 
         || value_equal (arg1,arg3))
         && (value_less (arg2,arg1)
         || value_equal (arg2,arg1)));
       */
      /* FIXME: OP_ATTRIBUTE should be defined in expression.h */
      /*    case OP_ATTRIBUTE:
         *pos += 3;
         atr = (enum ada_attribute) longest_to_int (exp->elts[pc + 2].longconst);
         switch (atr) 
         {
         default:
         error ("unexpected attribute encountered");

         case ATR_FIRST:
         case ATR_LAST:
         case ATR_LENGTH:
         {
         struct type* type_arg;
         if (exp->elts[*pos].opcode == OP_TYPE)
         {
         evaluate_subexp (NULL_TYPE, exp, pos, EVAL_SKIP);
         arg1 = NULL;
         type_arg = exp->elts[pc + 5].type;
         }
         else
         {
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         type_arg = NULL;
         }

         if (exp->elts[*pos].opcode != OP_LONG) 
         error ("illegal operand to '%s", ada_attribute_name (atr));
         tem = longest_to_int (exp->elts[*pos+2].longconst);
         *pos += 4;

         if (noside == EVAL_SKIP)
         goto nosideret;

         if (type_arg == NULL)
         {
         arg1 = ada_coerce_ref (arg1);

         if (ada_is_packed_array_type (VALUE_TYPE (arg1)))
         arg1 = ada_coerce_to_simple_array (arg1);

         if (tem < 1 || tem > ada_array_arity (VALUE_TYPE (arg1)))
         error ("invalid dimension number to '%s", 
         ada_attribute_name (atr));

         if (noside == EVAL_AVOID_SIDE_EFFECTS)
         {
         type = ada_index_type (VALUE_TYPE (arg1), tem);
         if (type == NULL) 
         error ("attempt to take bound of something that is not an array");
         return allocate_value (type);
         }

         switch (atr) 
         {
         default: 
         error ("unexpected attribute encountered");
         case ATR_FIRST:
         return ada_array_bound (arg1, tem, 0);
         case ATR_LAST:
         return ada_array_bound (arg1, tem, 1);
         case ATR_LENGTH:
         return ada_array_length (arg1, tem);
         }
         }
         else if (TYPE_CODE (type_arg) == TYPE_CODE_RANGE
         || TYPE_CODE (type_arg) == TYPE_CODE_INT) 
         {
         struct type* range_type;
         char* name = ada_type_name (type_arg);
         if (name == NULL)
         {
         if (TYPE_CODE (type_arg) == TYPE_CODE_RANGE) 
         range_type = type_arg;
         else
         error ("unimplemented type attribute");
         }
         else 
         range_type = 
         to_fixed_range_type (name, NULL, TYPE_OBJFILE (type_arg));
         switch (atr) 
         {
         default: 
         error ("unexpected attribute encountered");
         case ATR_FIRST:
         return value_from_longest (TYPE_TARGET_TYPE (range_type),
         TYPE_LOW_BOUND (range_type));
         case ATR_LAST:
         return value_from_longest (TYPE_TARGET_TYPE (range_type),
         TYPE_HIGH_BOUND (range_type));
         }
         }              
         else if (TYPE_CODE (type_arg) == TYPE_CODE_ENUM)
         {
         switch (atr) 
         {
         default: 
         error ("unexpected attribute encountered");
         case ATR_FIRST:
         return value_from_longest 
         (type_arg, TYPE_FIELD_BITPOS (type_arg, 0));
         case ATR_LAST:
         return value_from_longest 
         (type_arg, 
         TYPE_FIELD_BITPOS (type_arg,
         TYPE_NFIELDS (type_arg) - 1));
         }
         }
         else if (TYPE_CODE (type_arg) == TYPE_CODE_FLT)
         error ("unimplemented type attribute");
         else 
         {
         LONGEST low, high;

         if (ada_is_packed_array_type (type_arg))
         type_arg = decode_packed_array_type (type_arg);

         if (tem < 1 || tem > ada_array_arity (type_arg))
         error ("invalid dimension number to '%s", 
         ada_attribute_name (atr));

         if (noside == EVAL_AVOID_SIDE_EFFECTS)
         {
         type = ada_index_type (type_arg, tem);
         if (type == NULL) 
         error ("attempt to take bound of something that is not an array");
         return allocate_value (type);
         }

         switch (atr) 
         {
         default: 
         error ("unexpected attribute encountered");
         case ATR_FIRST:
         low = ada_array_bound_from_type (type_arg, tem, 0, &type);
         return value_from_longest (type, low);
         case ATR_LAST:
         high = ada_array_bound_from_type (type_arg, tem, 1, &type);
         return value_from_longest (type, high);
         case ATR_LENGTH:
         low = ada_array_bound_from_type (type_arg, tem, 0, &type);
         high = ada_array_bound_from_type (type_arg, tem, 1, NULL);
         return value_from_longest (type, high-low+1);
         }
         }
         }

         case ATR_TAG:
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         if (noside == EVAL_SKIP)
         goto nosideret;

         if (noside == EVAL_AVOID_SIDE_EFFECTS)
         return         
         value_zero (ada_tag_type (arg1), not_lval);

         return ada_value_tag (arg1);

         case ATR_MIN:
         case ATR_MAX:
         evaluate_subexp (NULL_TYPE, exp, pos, EVAL_SKIP);
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         arg2 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         if (noside == EVAL_SKIP)
         goto nosideret;
         else if (noside == EVAL_AVOID_SIDE_EFFECTS)
         return value_zero (VALUE_TYPE (arg1), not_lval);
         else
         return value_binop (arg1, arg2, 
         atr == ATR_MIN ? BINOP_MIN : BINOP_MAX);

         case ATR_MODULUS:
         {
         struct type* type_arg = exp->elts[pc + 5].type;
         evaluate_subexp (NULL_TYPE, exp, pos, EVAL_SKIP);
         *pos += 4;

         if (noside == EVAL_SKIP)
         goto nosideret;

         if (! ada_is_modular_type (type_arg))
         error ("'modulus must be applied to modular type");

         return value_from_longest (TYPE_TARGET_TYPE (type_arg),
         ada_modulus (type_arg));
         }


         case ATR_POS:
         evaluate_subexp (NULL_TYPE, exp, pos, EVAL_SKIP);
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         if (noside == EVAL_SKIP)
         goto nosideret;
         else if (noside == EVAL_AVOID_SIDE_EFFECTS)
         return value_zero (builtin_type_ada_int, not_lval);
         else 
         return value_pos_atr (arg1);

         case ATR_SIZE:
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         if (noside == EVAL_SKIP)
         goto nosideret;
         else if (noside == EVAL_AVOID_SIDE_EFFECTS)
         return value_zero (builtin_type_ada_int, not_lval);
         else
         return value_from_longest (builtin_type_ada_int,
         TARGET_CHAR_BIT 
         * TYPE_LENGTH (VALUE_TYPE (arg1)));

         case ATR_VAL:
         evaluate_subexp (NULL_TYPE, exp, pos, EVAL_SKIP);
         arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
         type = exp->elts[pc + 5].type;
         if (noside == EVAL_SKIP)
         goto nosideret;
         else if (noside == EVAL_AVOID_SIDE_EFFECTS)
         return value_zero (type, not_lval);
         else 
         return value_val_atr (type, arg1);
         } */
    case BINOP_EXP:
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      arg2 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (binop_user_defined_p (op, arg1, arg2))
	return unwrap_value (value_x_binop (arg1, arg2, op, OP_NULL,
					    EVAL_NORMAL));
      else if (noside == EVAL_AVOID_SIDE_EFFECTS)
	return value_zero (VALUE_TYPE (arg1), not_lval);
      else
	return value_binop (arg1, arg2, op);

    case UNOP_PLUS:
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (unop_user_defined_p (op, arg1))
	return unwrap_value (value_x_unop (arg1, op, EVAL_NORMAL));
      else
	return arg1;

    case UNOP_ABS:
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (value_less (arg1, value_zero (VALUE_TYPE (arg1), not_lval)))
	return value_neg (arg1);
      else
	return arg1;

    case UNOP_IND:
      if (expect_type && TYPE_CODE (expect_type) == TYPE_CODE_PTR)
	expect_type = TYPE_TARGET_TYPE (check_typedef (expect_type));
      arg1 = evaluate_subexp (expect_type, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      type = check_typedef (VALUE_TYPE (arg1));
      if (noside == EVAL_AVOID_SIDE_EFFECTS)
	{
	  if (ada_is_array_descriptor (type))
	    /* GDB allows dereferencing GNAT array descriptors. */
	    {
	      struct type *arrType = ada_type_of_array (arg1, 0);
	      if (arrType == NULL)
		error ("Attempt to dereference null array pointer.");
	      return value_at_lazy (arrType, 0, NULL);
	    }
	  else if (TYPE_CODE (type) == TYPE_CODE_PTR
		   || TYPE_CODE (type) == TYPE_CODE_REF
		   /* In C you can dereference an array to get the 1st elt.  */
		   || TYPE_CODE (type) == TYPE_CODE_ARRAY)
	    return
	      value_zero
	      (to_static_fixed_type
	       (ada_aligned_type (check_typedef (TYPE_TARGET_TYPE (type)))),
	       lval_memory);
	  else if (TYPE_CODE (type) == TYPE_CODE_INT)
	    /* GDB allows dereferencing an int.  */
	    return value_zero (builtin_type_int, lval_memory);
	  else
	    error ("Attempt to take contents of a non-pointer value.");
	}
      arg1 = ada_coerce_ref (arg1);
      type = check_typedef (VALUE_TYPE (arg1));

      if (ada_is_array_descriptor (type))
	/* GDB allows dereferencing GNAT array descriptors. */
	return ada_coerce_to_simple_array (arg1);
      else
	return ada_value_ind (arg1);

    case STRUCTOP_STRUCT:
      tem = longest_to_int (exp->elts[pc + 1].longconst);
      (*pos) += 3 + BYTES_TO_EXP_ELEM (tem + 1);
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (noside == EVAL_AVOID_SIDE_EFFECTS)
	return value_zero (ada_aligned_type
			   (ada_lookup_struct_elt_type (VALUE_TYPE (arg1),
							&exp->elts[pc +
								   2].string,
							0, NULL)),
			   lval_memory);
      else
	return unwrap_value (ada_value_struct_elt (arg1,
						   &exp->elts[pc + 2].string,
						   "record"));
    case OP_TYPE:
      /* The value is not supposed to be used. This is here to make it
         easier to accommodate expressions that contain types. */
      (*pos) += 2;
      if (noside == EVAL_SKIP)
	goto nosideret;
      else if (noside == EVAL_AVOID_SIDE_EFFECTS)
	return allocate_value (builtin_type_void);
      else
	error ("Attempt to use a type name as an expression");

    case STRUCTOP_PTR:
      tem = longest_to_int (exp->elts[pc + 1].longconst);
      (*pos) += 3 + BYTES_TO_EXP_ELEM (tem + 1);
      arg1 = evaluate_subexp (NULL_TYPE, exp, pos, noside);
      if (noside == EVAL_SKIP)
	goto nosideret;
      if (noside == EVAL_AVOID_SIDE_EFFECTS)
	return value_zero (ada_aligned_type
			   (ada_lookup_struct_elt_type (VALUE_TYPE (arg1),
							&exp->elts[pc +
								   2].string,
							0, NULL)),
			   lval_memory);
      else
	return unwrap_value (ada_value_struct_elt (arg1,
						   &exp->elts[pc + 2].string,
						   "record access"));
    }

nosideret:
  return value_from_longest (builtin_type_long, (LONGEST) 1);
}


				/* Fixed point */

/* If TYPE encodes an Ada fixed-point type, return the suffix of the
   type name that encodes the 'small and 'delta information.
   Otherwise, return NULL. */

static const char *
fixed_type_info (struct type *type)
{
  const char *name = ada_type_name (type);
  enum type_code code = (type == NULL) ? TYPE_CODE_UNDEF : TYPE_CODE (type);

  if ((code == TYPE_CODE_INT || code == TYPE_CODE_RANGE) && name != NULL)
    {
      const char *tail = strstr (name, "___XF_");
      if (tail == NULL)
	return NULL;
      else
	return tail + 5;
    }
  else if (code == TYPE_CODE_RANGE && TYPE_TARGET_TYPE (type) != type)
    return fixed_type_info (TYPE_TARGET_TYPE (type));
  else
    return NULL;
}

/* Returns non-zero iff TYPE represents an Ada fixed-point type. */

int
ada_is_fixed_point_type (struct type *type)
{
  return fixed_type_info (type) != NULL;
}

/* Assuming that TYPE is the representation of an Ada fixed-point
   type, return its delta, or -1 if the type is malformed and the
   delta cannot be determined. */

DOUBLEST
ada_delta (struct type *type)
{
  const char *encoding = fixed_type_info (type);
  long num, den;

  if (sscanf (encoding, "_%ld_%ld", &num, &den) < 2)
    return -1.0;
  else
    return (DOUBLEST) num / (DOUBLEST) den;
}

/* Assuming that ada_is_fixed_point_type (TYPE), return the scaling
   factor ('SMALL value) associated with the type. */

static DOUBLEST
scaling_factor (struct type *type)
{
  const char *encoding = fixed_type_info (type);
  unsigned long num0, den0, num1, den1;
  int n;

  n = sscanf (encoding, "_%lu_%lu_%lu_%lu", &num0, &den0, &num1, &den1);

  if (n < 2)
    return 1.0;
  else if (n == 4)
    return (DOUBLEST) num1 / (DOUBLEST) den1;
  else
    return (DOUBLEST) num0 / (DOUBLEST) den0;
}


/* Assuming that X is the representation of a value of fixed-point
   type TYPE, return its floating-point equivalent. */

DOUBLEST
ada_fixed_to_float (struct type *type, LONGEST x)
{
  return (DOUBLEST) x *scaling_factor (type);
}

/* The representation of a fixed-point value of type TYPE 
   corresponding to the value X. */

LONGEST
ada_float_to_fixed (struct type *type, DOUBLEST x)
{
  return (LONGEST) (x / scaling_factor (type) + 0.5);
}


				/* VAX floating formats */

/* Non-zero iff TYPE represents one of the special VAX floating-point
   types. */
int
ada_is_vax_floating_type (struct type *type)
{
  int name_len =
    (ada_type_name (type) == NULL) ? 0 : strlen (ada_type_name (type));
  return
    name_len > 6
    && (TYPE_CODE (type) == TYPE_CODE_INT
	|| TYPE_CODE (type) == TYPE_CODE_RANGE)
    && DEPRECATED_STREQN (ada_type_name (type) + name_len - 6, "___XF", 5);
}

/* The type of special VAX floating-point type this is, assuming
   ada_is_vax_floating_point */
int
ada_vax_float_type_suffix (struct type *type)
{
  return ada_type_name (type)[strlen (ada_type_name (type)) - 1];
}

/* A value representing the special debugging function that outputs 
   VAX floating-point values of the type represented by TYPE.  Assumes
   ada_is_vax_floating_type (TYPE). */
struct value *
ada_vax_float_print_function (struct type *type)
{
  switch (ada_vax_float_type_suffix (type))
    {
    case 'F':
      return get_var_value ("DEBUG_STRING_F", 0);
    case 'D':
      return get_var_value ("DEBUG_STRING_D", 0);
    case 'G':
      return get_var_value ("DEBUG_STRING_G", 0);
    default:
      error ("invalid VAX floating-point type");
    }
}


				/* Range types */

/* Scan STR beginning at position K for a discriminant name, and
   return the value of that discriminant field of DVAL in *PX.  If
   PNEW_K is not null, put the position of the character beyond the
   name scanned in *PNEW_K.  Return 1 if successful; return 0 and do
   not alter *PX and *PNEW_K if unsuccessful. */

static int
scan_discrim_bound (char *str, int k, struct value *dval, LONGEST * px,
		    int *pnew_k)
{
  static char *bound_buffer = NULL;
  static size_t bound_buffer_len = 0;
  char *bound;
  char *pend;
  struct value *bound_val;

  if (dval == NULL || str == NULL || str[k] == '\0')
    return 0;

  pend = strstr (str + k, "__");
  if (pend == NULL)
    {
      bound = str + k;
      k += strlen (bound);
    }
  else
    {
      GROW_VECT (bound_buffer, bound_buffer_len, pend - (str + k) + 1);
      bound = bound_buffer;
      strncpy (bound_buffer, str + k, pend - (str + k));
      bound[pend - (str + k)] = '\0';
      k = pend - str;
    }

  bound_val = ada_search_struct_field (bound, dval, 0, VALUE_TYPE (dval));
  if (bound_val == NULL)
    return 0;

  *px = value_as_long (bound_val);
  if (pnew_k != NULL)
    *pnew_k = k;
  return 1;
}

/* Value of variable named NAME in the current environment.  If
   no such variable found, then if ERR_MSG is null, returns 0, and
   otherwise causes an error with message ERR_MSG. */
static struct value *
get_var_value (char *name, char *err_msg)
{
  struct symbol **syms;
  struct block **blocks;
  int nsyms;

  nsyms =
    ada_lookup_symbol_list (name, get_selected_block (NULL), VAR_DOMAIN,
			    &syms, &blocks);

  if (nsyms != 1)
    {
      if (err_msg == NULL)
	return 0;
      else
	error ("%s", err_msg);
    }

  return value_of_variable (syms[0], blocks[0]);
}

/* Value of integer variable named NAME in the current environment.  If
   no such variable found, then if ERR_MSG is null, returns 0, and sets
   *FLAG to 0.  If successful, sets *FLAG to 1. */
LONGEST
get_int_var_value (char *name, char *err_msg, int *flag)
{
  struct value *var_val = get_var_value (name, err_msg);

  if (var_val == 0)
    {
      if (flag != NULL)
	*flag = 0;
      return 0;
    }
  else
    {
      if (flag != NULL)
	*flag = 1;
      return value_as_long (var_val);
    }
}


/* Return a range type whose base type is that of the range type named
   NAME in the current environment, and whose bounds are calculated
   from NAME according to the GNAT range encoding conventions. 
   Extract discriminant values, if needed, from DVAL.  If a new type
   must be created, allocate in OBJFILE's space.  The bounds
   information, in general, is encoded in NAME, the base type given in
   the named range type. */

static struct type *
to_fixed_range_type (char *name, struct value *dval, struct objfile *objfile)
{
  struct type *raw_type = ada_find_any_type (name);
  struct type *base_type;
  LONGEST low, high;
  char *subtype_info;

  if (raw_type == NULL)
    base_type = builtin_type_int;
  else if (TYPE_CODE (raw_type) == TYPE_CODE_RANGE)
    base_type = TYPE_TARGET_TYPE (raw_type);
  else
    base_type = raw_type;

  subtype_info = strstr (name, "___XD");
  if (subtype_info == NULL)
    return raw_type;
  else
    {
      static char *name_buf = NULL;
      static size_t name_len = 0;
      int prefix_len = subtype_info - name;
      LONGEST L, U;
      struct type *type;
      char *bounds_str;
      int n;

      GROW_VECT (name_buf, name_len, prefix_len + 5);
      strncpy (name_buf, name, prefix_len);
      name_buf[prefix_len] = '\0';

      subtype_info += 5;
      bounds_str = strchr (subtype_info, '_');
      n = 1;

      if (*subtype_info == 'L')
	{
	  if (!ada_scan_number (bounds_str, n, &L, &n)
	      && !scan_discrim_bound (bounds_str, n, dval, &L, &n))
	    return raw_type;
	  if (bounds_str[n] == '_')
	    n += 2;
	  else if (bounds_str[n] == '.')	/* FIXME? SGI Workshop kludge. */
	    n += 1;
	  subtype_info += 1;
	}
      else
	{
	  strcpy (name_buf + prefix_len, "___L");
	  L = get_int_var_value (name_buf, "Index bound unknown.", NULL);
	}

      if (*subtype_info == 'U')
	{
	  if (!ada_scan_number (bounds_str, n, &U, &n)
	      && !scan_discrim_bound (bounds_str, n, dval, &U, &n))
	    return raw_type;
	}
      else
	{
	  strcpy (name_buf + prefix_len, "___U");
	  U = get_int_var_value (name_buf, "Index bound unknown.", NULL);
	}

      if (objfile == NULL)
	objfile = TYPE_OBJFILE (base_type);
      type = create_range_type (alloc_type (objfile), base_type, L, U);
      TYPE_NAME (type) = name;
      return type;
    }
}

/* True iff NAME is the name of a range type. */
int
ada_is_range_type_name (const char *name)
{
  return (name != NULL && strstr (name, "___XD"));
}


				/* Modular types */

/* True iff TYPE is an Ada modular type. */
int
ada_is_modular_type (struct type *type)
{
  /* FIXME: base_type should be declared in gdbtypes.h, implemented in
     valarith.c */
  struct type *subranged_type;	/* = base_type (type); */

  return (subranged_type != NULL && TYPE_CODE (type) == TYPE_CODE_RANGE
	  && TYPE_CODE (subranged_type) != TYPE_CODE_ENUM
	  && TYPE_UNSIGNED (subranged_type));
}

/* Assuming ada_is_modular_type (TYPE), the modulus of TYPE. */
LONGEST
ada_modulus (struct type * type)
{
  return TYPE_HIGH_BOUND (type) + 1;
}



				/* Operators */

/* Table mapping opcodes into strings for printing operators
   and precedences of the operators.  */

static const struct op_print ada_op_print_tab[] = {
  {":=", BINOP_ASSIGN, PREC_ASSIGN, 1},
  {"or else", BINOP_LOGICAL_OR, PREC_LOGICAL_OR, 0},
  {"and then", BINOP_LOGICAL_AND, PREC_LOGICAL_AND, 0},
  {"or", BINOP_BITWISE_IOR, PREC_BITWISE_IOR, 0},
  {"xor", BINOP_BITWISE_XOR, PREC_BITWISE_XOR, 0},
  {"and", BINOP_BITWISE_AND, PREC_BITWISE_AND, 0},
  {"=", BINOP_EQUAL, PREC_EQUAL, 0},
  {"/=", BINOP_NOTEQUAL, PREC_EQUAL, 0},
  {"<=", BINOP_LEQ, PREC_ORDER, 0},
  {">=", BINOP_GEQ, PREC_ORDER, 0},
  {">", BINOP_GTR, PREC_ORDER, 0},
  {"<", BINOP_LESS, PREC_ORDER, 0},
  {">>", BINOP_RSH, PREC_SHIFT, 0},
  {"<<", BINOP_LSH, PREC_SHIFT, 0},
  {"+", BINOP_ADD, PREC_ADD, 0},
  {"-", BINOP_SUB, PREC_ADD, 0},
  {"&", BINOP_CONCAT, PREC_ADD, 0},
  {"*", BINOP_MUL, PREC_MUL, 0},
  {"/", BINOP_DIV, PREC_MUL, 0},
  {"rem", BINOP_REM, PREC_MUL, 0},
  {"mod", BINOP_MOD, PREC_MUL, 0},
  {"**", BINOP_EXP, PREC_REPEAT, 0},
  {"@", BINOP_REPEAT, PREC_REPEAT, 0},
  {"-", UNOP_NEG, PREC_PREFIX, 0},
  {"+", UNOP_PLUS, PREC_PREFIX, 0},
  {"not ", UNOP_LOGICAL_NOT, PREC_PREFIX, 0},
  {"not ", UNOP_COMPLEMENT, PREC_PREFIX, 0},
  {"abs ", UNOP_ABS, PREC_PREFIX, 0},
  {".all", UNOP_IND, PREC_SUFFIX, 1},	/* FIXME: postfix .ALL */
  {"'access", UNOP_ADDR, PREC_SUFFIX, 1},	/* FIXME: postfix 'ACCESS */
  {NULL, 0, 0, 0}
};

			/* Assorted Types and Interfaces */

struct type *builtin_type_ada_int;
struct type *builtin_type_ada_short;
struct type *builtin_type_ada_long;
struct type *builtin_type_ada_long_long;
struct type *builtin_type_ada_char;
struct type *builtin_type_ada_float;
struct type *builtin_type_ada_double;
struct type *builtin_type_ada_long_double;
struct type *builtin_type_ada_natural;
struct type *builtin_type_ada_positive;
struct type *builtin_type_ada_system_address;

struct type **const (ada_builtin_types[]) =
{

  &builtin_type_ada_int,
    &builtin_type_ada_long,
    &builtin_type_ada_short,
    &builtin_type_ada_char,
    &builtin_type_ada_float,
    &builtin_type_ada_double,
    &builtin_type_ada_long_long,
    &builtin_type_ada_long_double,
    &builtin_type_ada_natural, &builtin_type_ada_positive,
    /* The following types are carried over from C for convenience. */
&builtin_type_int,
    &builtin_type_long,
    &builtin_type_short,
    &builtin_type_char,
    &builtin_type_float,
    &builtin_type_double,
    &builtin_type_long_long,
    &builtin_type_void,
    &builtin_type_signed_char,
    &builtin_type_unsigned_char,
    &builtin_type_unsigned_short,
    &builtin_type_unsigned_int,
    &builtin_type_unsigned_long,
    &builtin_type_unsigned_long_long,
    &builtin_type_long_double,
    &builtin_type_complex, &builtin_type_double_complex, 0};

/* Not really used, but needed in the ada_language_defn. */
static void
emit_char (int c, struct ui_file *stream, int quoter)
{
  ada_emit_char (c, stream, quoter, 1);
}

const struct language_defn ada_language_defn = {
  "ada",			/* Language name */
  /*  language_ada, */
  language_unknown,
  /* FIXME: language_ada should be defined in defs.h */
  ada_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_on,		/* Yes, Ada is case-insensitive, but
				 * that's not quite what this means. */
  ada_parse,
  ada_error,
  ada_evaluate_subexp,
  ada_printchar,		/* Print a character constant */
  ada_printstr,			/* Function to print string constant */
  emit_char,			/* Function to print single char (not used) */
  ada_create_fundamental_type,	/* Create fundamental type in this language */
  ada_print_type,		/* Print a type using appropriate syntax */
  ada_val_print,		/* Print a value using appropriate syntax */
  ada_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  value_of_this,		/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal  */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
#if 0
  {"8#%lo#", "8#", "o", "#"},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"16#%lx#", "16#", "x", "#"},	/* Hex format info */
#else
  /* Copied from c-lang.c. */
  {"0%lo", "0", "o", ""},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"0x%lx", "0x", "x", ""},	/* Hex format info */
#endif
  ada_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays (FIXME?) */
  0,				/* String lower bound (FIXME?) */
  &builtin_type_ada_char,
  default_word_break_characters,
  LANG_MAGIC
};

void
_initialize_ada_language (void)
{
  builtin_type_ada_int =
    init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
	       0, "integer", (struct objfile *) NULL);
  builtin_type_ada_long =
    init_type (TYPE_CODE_INT, TARGET_LONG_BIT / TARGET_CHAR_BIT,
	       0, "long_integer", (struct objfile *) NULL);
  builtin_type_ada_short =
    init_type (TYPE_CODE_INT, TARGET_SHORT_BIT / TARGET_CHAR_BIT,
	       0, "short_integer", (struct objfile *) NULL);
  builtin_type_ada_char =
    init_type (TYPE_CODE_INT, TARGET_CHAR_BIT / TARGET_CHAR_BIT,
	       0, "character", (struct objfile *) NULL);
  builtin_type_ada_float =
    init_type (TYPE_CODE_FLT, TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
	       0, "float", (struct objfile *) NULL);
  builtin_type_ada_double =
    init_type (TYPE_CODE_FLT, TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0, "long_float", (struct objfile *) NULL);
  builtin_type_ada_long_long =
    init_type (TYPE_CODE_INT, TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
	       0, "long_long_integer", (struct objfile *) NULL);
  builtin_type_ada_long_double =
    init_type (TYPE_CODE_FLT, TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0, "long_long_float", (struct objfile *) NULL);
  builtin_type_ada_natural =
    init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
	       0, "natural", (struct objfile *) NULL);
  builtin_type_ada_positive =
    init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
	       0, "positive", (struct objfile *) NULL);


  builtin_type_ada_system_address =
    lookup_pointer_type (init_type (TYPE_CODE_VOID, 1, 0, "void",
				    (struct objfile *) NULL));
  TYPE_NAME (builtin_type_ada_system_address) = "system__address";

  add_language (&ada_language_defn);

  add_show_from_set
    (add_set_cmd ("varsize-limit", class_support, var_uinteger,
		  (char *) &varsize_limit,
		  "Set maximum bytes in dynamic-sized object.",
		  &setlist), &showlist);
  varsize_limit = 65536;

  add_com ("begin", class_breakpoint, begin_command,
	   "Start the debugged program, stopping at the beginning of the\n\
main program.  You may specify command-line arguments to give it, as for\n\
the \"run\" command (q.v.).");
}


/* Create a fundamental Ada type using default reasonable for the current
   target machine.

   Some object/debugging file formats (DWARF version 1, COFF, etc) do not
   define fundamental types such as "int" or "double".  Others (stabs or
   DWARF version 2, etc) do define fundamental types.  For the formats which
   don't provide fundamental types, gdb can create such types using this
   function.

   FIXME:  Some compilers distinguish explicitly signed integral types
   (signed short, signed int, signed long) from "regular" integral types
   (short, int, long) in the debugging information.  There is some dis-
   agreement as to how useful this feature is.  In particular, gcc does
   not support this.  Also, only some debugging formats allow the
   distinction to be passed on to a debugger.  For now, we always just
   use "short", "int", or "long" as the type name, for both the implicit
   and explicitly signed types.  This also makes life easier for the
   gdb test suite since we don't have to account for the differences
   in output depending upon what the compiler and debugging format
   support.  We will probably have to re-examine the issue when gdb
   starts taking it's fundamental type information directly from the
   debugging information supplied by the compiler.  fnf@cygnus.com */

static struct type *
ada_create_fundamental_type (struct objfile *objfile, int typeid)
{
  struct type *type = NULL;

  switch (typeid)
    {
    default:
      /* FIXME:  For now, if we are asked to produce a type not in this
         language, create the equivalent of a C integer type with the
         name "<?type?>".  When all the dust settles from the type
         reconstruction work, this should probably become an error. */
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "<?type?>", objfile);
      warning ("internal error: no Ada fundamental type %d", typeid);
      break;
    case FT_VOID:
      type = init_type (TYPE_CODE_VOID,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "void", objfile);
      break;
    case FT_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "character", objfile);
      break;
    case FT_SIGNED_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "signed char", objfile);
      break;
    case FT_UNSIGNED_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned char", objfile);
      break;
    case FT_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			0, "short_integer", objfile);
      break;
    case FT_SIGNED_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			0, "short_integer", objfile);
      break;
    case FT_UNSIGNED_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned short", objfile);
      break;
    case FT_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "integer", objfile);
      break;
    case FT_SIGNED_INTEGER:
      type = init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT, 0, "integer", objfile);	/* FIXME -fnf */
      break;
    case FT_UNSIGNED_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned int", objfile);
      break;
    case FT_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_BIT / TARGET_CHAR_BIT,
			0, "long_integer", objfile);
      break;
    case FT_SIGNED_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_BIT / TARGET_CHAR_BIT,
			0, "long_integer", objfile);
      break;
    case FT_UNSIGNED_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned long", objfile);
      break;
    case FT_LONG_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
			0, "long_long_integer", objfile);
      break;
    case FT_SIGNED_LONG_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
			0, "long_long_integer", objfile);
      break;
    case FT_UNSIGNED_LONG_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned long long", objfile);
      break;
    case FT_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
			0, "float", objfile);
      break;
    case FT_DBL_PREC_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "long_float", objfile);
      break;
    case FT_EXT_PREC_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "long_long_float", objfile);
      break;
    }
  return (type);
}

void
ada_dump_symtab (struct symtab *s)
{
  int i;
  fprintf (stderr, "New symtab: [\n");
  fprintf (stderr, "  Name: %s/%s;\n",
	   s->dirname ? s->dirname : "?", s->filename ? s->filename : "?");
  fprintf (stderr, "  Format: %s;\n", s->debugformat);
  if (s->linetable != NULL)
    {
      fprintf (stderr, "  Line table (section %d):\n", s->block_line_section);
      for (i = 0; i < s->linetable->nitems; i += 1)
	{
	  struct linetable_entry *e = s->linetable->item + i;
	  fprintf (stderr, "    %4ld: %8lx\n", (long) e->line, (long) e->pc);
	}
    }
  fprintf (stderr, "]\n");
}
