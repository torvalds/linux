/* RTL reader for GCC.
   Copyright (C) 1987, 1988, 1991, 1994, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "bconfig.h"

/* Disable rtl checking; it conflicts with the macro handling.  */
#undef ENABLE_RTL_CHECKING

#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "obstack.h"
#include "hashtab.h"
#include "gensupport.h"

static htab_t md_constants;

/* One element in a singly-linked list of (integer, string) pairs.  */
struct map_value {
  struct map_value *next;
  int number;
  const char *string;
};

/* Maps a macro or attribute name to a list of (integer, string) pairs.
   The integers are mode or code values; the strings are either C conditions
   or attribute values.  */
struct mapping {
  /* The name of the macro or attribute.  */
  const char *name;

  /* The group (modes or codes) to which the macro or attribute belongs.  */
  struct macro_group *group;

  /* Gives a unique number to the attribute or macro.  Numbers are
     allocated consecutively, starting at 0.  */
  int index;

  /* The list of (integer, string) pairs.  */
  struct map_value *values;
};

/* A structure for abstracting the common parts of code and mode macros.  */
struct macro_group {
  /* Tables of "mapping" structures, one for attributes and one for macros.  */
  htab_t attrs, macros;

  /* The number of "real" modes or codes (and by extension, the first
     number available for use as a macro placeholder).  */
  int num_builtins;

  /* Treat the given string as the name of a standard mode or code and
     return its integer value.  Use the given file for error reporting.  */
  int (*find_builtin) (const char *, FILE *);

  /* Return true if the given rtx uses the given mode or code.  */
  bool (*uses_macro_p) (rtx, int);

  /* Make the given rtx use the given mode or code.  */
  void (*apply_macro) (rtx, int);
};

/* Associates PTR (which can be a string, etc.) with the file location
   specified by FILENAME and LINENO.  */
struct ptr_loc {
  const void *ptr;
  const char *filename;
  int lineno;
};

/* A structure used to pass data from read_rtx to apply_macro_traverse
   via htab_traverse.  */
struct macro_traverse_data {
  /* Instruction queue.  */
  rtx queue;
  /* Attributes seen for modes.  */
  struct map_value *mode_maps;
  /* Input file.  */
  FILE *infile;
  /* The last unknown attribute used as a mode.  */
  const char *unknown_mode_attr;
};

/* If CODE is the number of a code macro, return a real rtx code that
   has the same format.  Return CODE otherwise.  */
#define BELLWETHER_CODE(CODE) \
  ((CODE) < NUM_RTX_CODE ? CODE : bellwether_codes[CODE - NUM_RTX_CODE])

static void fatal_with_file_and_line (FILE *, const char *, ...)
  ATTRIBUTE_PRINTF_2 ATTRIBUTE_NORETURN;
static void fatal_expected_char (FILE *, int, int) ATTRIBUTE_NORETURN;
static int find_mode (const char *, FILE *);
static bool uses_mode_macro_p (rtx, int);
static void apply_mode_macro (rtx, int);
static int find_code (const char *, FILE *);
static bool uses_code_macro_p (rtx, int);
static void apply_code_macro (rtx, int);
static const char *apply_macro_to_string (const char *, struct mapping *, int);
static rtx apply_macro_to_rtx (rtx, struct mapping *, int,
			       struct map_value *, FILE *, const char **);
static bool uses_macro_p (rtx, struct mapping *);
static const char *add_condition_to_string (const char *, const char *);
static void add_condition_to_rtx (rtx, const char *);
static int apply_macro_traverse (void **, void *);
static struct mapping *add_mapping (struct macro_group *, htab_t t,
				    const char *, FILE *);
static struct map_value **add_map_value (struct map_value **,
					 int, const char *);
static void initialize_macros (void);
static void read_name (char *, FILE *);
static hashval_t leading_ptr_hash (const void *);
static int leading_ptr_eq_p (const void *, const void *);
static void set_rtx_ptr_loc (const void *, const char *, int);
static const struct ptr_loc *get_rtx_ptr_loc (const void *);
static char *read_string (FILE *, int);
static char *read_quoted_string (FILE *);
static char *read_braced_string (FILE *);
static void read_escape (FILE *);
static hashval_t def_hash (const void *);
static int def_name_eq_p (const void *, const void *);
static void read_constants (FILE *infile, char *tmp_char);
static void read_conditions (FILE *infile, char *tmp_char);
static void validate_const_int (FILE *, const char *);
static int find_macro (struct macro_group *, const char *, FILE *);
static struct mapping *read_mapping (struct macro_group *, htab_t, FILE *);
static void check_code_macro (struct mapping *, FILE *);
static rtx read_rtx_1 (FILE *, struct map_value **);
static rtx read_rtx_variadic (FILE *, struct map_value **, rtx);

/* The mode and code macro structures.  */
static struct macro_group modes, codes;

/* Index I is the value of BELLWETHER_CODE (I + NUM_RTX_CODE).  */
static enum rtx_code *bellwether_codes;

/* Obstack used for allocating RTL strings.  */
static struct obstack string_obstack;

/* A table of ptr_locs, hashed on the PTR field.  */
static htab_t ptr_locs;

/* An obstack for the above.  Plain xmalloc is a bit heavyweight for a
   small structure like ptr_loc.  */
static struct obstack ptr_loc_obstack;

/* A hash table of triples (A, B, C), where each of A, B and C is a condition
   and A is equivalent to "B && C".  This is used to keep track of the source
   of conditions that are made up of separate rtx strings (such as the split
   condition of a define_insn_and_split).  */
static htab_t joined_conditions;

/* An obstack for allocating joined_conditions entries.  */
static struct obstack joined_conditions_obstack;

/* Subroutines of read_rtx.  */

/* The current line number for the file.  */
int read_rtx_lineno = 1;

/* The filename for error reporting.  */
const char *read_rtx_filename = "<unknown>";

static void
fatal_with_file_and_line (FILE *infile, const char *msg, ...)
{
  char context[64];
  size_t i;
  int c;
  va_list ap;

  va_start (ap, msg);

  fprintf (stderr, "%s:%d: ", read_rtx_filename, read_rtx_lineno);
  vfprintf (stderr, msg, ap);
  putc ('\n', stderr);

  /* Gather some following context.  */
  for (i = 0; i < sizeof (context)-1; ++i)
    {
      c = getc (infile);
      if (c == EOF)
	break;
      if (c == '\r' || c == '\n')
	break;
      context[i] = c;
    }
  context[i] = '\0';

  fprintf (stderr, "%s:%d: following context is `%s'\n",
	   read_rtx_filename, read_rtx_lineno, context);

  va_end (ap);
  exit (1);
}

/* Dump code after printing a message.  Used when read_rtx finds
   invalid data.  */

static void
fatal_expected_char (FILE *infile, int expected_c, int actual_c)
{
  fatal_with_file_and_line (infile, "expected character `%c', found `%c'",
			    expected_c, actual_c);
}

/* Implementations of the macro_group callbacks for modes.  */

static int
find_mode (const char *name, FILE *infile)
{
  int i;

  for (i = 0; i < NUM_MACHINE_MODES; i++)
    if (strcmp (GET_MODE_NAME (i), name) == 0)
      return i;

  fatal_with_file_and_line (infile, "unknown mode `%s'", name);
}

static bool
uses_mode_macro_p (rtx x, int mode)
{
  return (int) GET_MODE (x) == mode;
}

static void
apply_mode_macro (rtx x, int mode)
{
  PUT_MODE (x, (enum machine_mode) mode);
}

/* Implementations of the macro_group callbacks for codes.  */

static int
find_code (const char *name, FILE *infile)
{
  int i;

  for (i = 0; i < NUM_RTX_CODE; i++)
    if (strcmp (GET_RTX_NAME (i), name) == 0)
      return i;

  fatal_with_file_and_line (infile, "unknown rtx code `%s'", name);
}

static bool
uses_code_macro_p (rtx x, int code)
{
  return (int) GET_CODE (x) == code;
}

static void
apply_code_macro (rtx x, int code)
{
  PUT_CODE (x, (enum rtx_code) code);
}

/* Map a code or mode attribute string P to the underlying string for
   MACRO and VALUE.  */

static struct map_value *
map_attr_string (const char *p, struct mapping *macro, int value)
{
  const char *attr;
  struct mapping *m;
  struct map_value *v;

  /* If there's a "macro:" prefix, check whether the macro name matches.
     Set ATTR to the start of the attribute name.  */
  attr = strchr (p, ':');
  if (attr == 0)
    attr = p;
  else
    {
      if (strncmp (p, macro->name, attr - p) != 0
	  || macro->name[attr - p] != 0)
	return 0;
      attr++;
    }

  /* Find the attribute specification.  */
  m = (struct mapping *) htab_find (macro->group->attrs, &attr);
  if (m == 0)
    return 0;

  /* Find the attribute value for VALUE.  */
  for (v = m->values; v != 0; v = v->next)
    if (v->number == value)
      break;

  return v;
}

/* Given an attribute string used as a machine mode, return an index
   to store in the machine mode to be translated by
   apply_macro_to_rtx.  */

static unsigned int
mode_attr_index (struct map_value **mode_maps, const char *string)
{
  char *p;
  struct map_value *mv;

  /* Copy the attribute string into permanent storage, without the
     angle brackets around it.  */
  obstack_grow0 (&string_obstack, string + 1, strlen (string) - 2);
  p = XOBFINISH (&string_obstack, char *);

  mv = XNEW (struct map_value);
  mv->number = *mode_maps == 0 ? 0 : (*mode_maps)->number + 1;
  mv->string = p;
  mv->next = *mode_maps;
  *mode_maps = mv;

  /* We return a code which we can map back into this string: the
     number of machine modes + the number of mode macros + the index
     we just used.  */
  return MAX_MACHINE_MODE + htab_elements (modes.macros) + mv->number;
}

/* Apply MODE_MAPS to the top level of X, expanding cases where an
   attribute is used for a mode.  MACRO is the current macro we are
   expanding, and VALUE is the value to which we are expanding it.
   INFILE is used for error messages.  This sets *UNKNOWN to true if
   we find a mode attribute which has not yet been defined, and does
   not change it otherwise.  */

static void
apply_mode_maps (rtx x, struct map_value *mode_maps, struct mapping *macro,
		 int value, FILE *infile, const char **unknown)
{
  unsigned int offset;
  int indx;
  struct map_value *pm;

  offset = MAX_MACHINE_MODE + htab_elements (modes.macros);
  if (GET_MODE (x) < offset)
    return;

  indx = GET_MODE (x) - offset;
  for (pm = mode_maps; pm; pm = pm->next)
    {
      if (pm->number == indx)
	{
	  struct map_value *v;

	  v = map_attr_string (pm->string, macro, value);
	  if (v)
	    PUT_MODE (x, (enum machine_mode) find_mode (v->string, infile));
	  else
	    *unknown = pm->string;
	  return;
	}
    }
}

/* Given that MACRO is being expanded as VALUE, apply the appropriate
   string substitutions to STRING.  Return the new string if any changes
   were needed, otherwise return STRING itself.  */

static const char *
apply_macro_to_string (const char *string, struct mapping *macro, int value)
{
  char *base, *copy, *p, *start, *end;
  struct map_value *v;

  if (string == 0)
    return string;

  base = p = copy = ASTRDUP (string);
  while ((start = strchr (p, '<')) && (end = strchr (start, '>')))
    {
      p = start + 1;

      *end = 0;
      v = map_attr_string (p, macro, value);
      *end = '>';
      if (v == 0)
	continue;

      /* Add everything between the last copied byte and the '<',
	 then add in the attribute value.  */
      obstack_grow (&string_obstack, base, start - base);
      obstack_grow (&string_obstack, v->string, strlen (v->string));
      base = end + 1;
    }
  if (base != copy)
    {
      obstack_grow (&string_obstack, base, strlen (base) + 1);
      copy = XOBFINISH (&string_obstack, char *);
      copy_rtx_ptr_loc (copy, string);
      return copy;
    }
  return string;
}

/* Return a copy of ORIGINAL in which all uses of MACRO have been
   replaced by VALUE.  MODE_MAPS holds information about attribute
   strings used for modes.  INFILE is used for error messages.  This
   sets *UNKNOWN_MODE_ATTR to the value of an unknown mode attribute,
   and does not change it otherwise.  */

static rtx
apply_macro_to_rtx (rtx original, struct mapping *macro, int value,
		    struct map_value *mode_maps, FILE *infile,
		    const char **unknown_mode_attr)
{
  struct macro_group *group;
  const char *format_ptr;
  int i, j;
  rtx x;
  enum rtx_code bellwether_code;

  if (original == 0)
    return original;

  /* Create a shallow copy of ORIGINAL.  */
  bellwether_code = BELLWETHER_CODE (GET_CODE (original));
  x = rtx_alloc (bellwether_code);
  memcpy (x, original, RTX_CODE_SIZE (bellwether_code));

  /* Change the mode or code itself.  */
  group = macro->group;
  if (group->uses_macro_p (x, macro->index + group->num_builtins))
    group->apply_macro (x, value);

  if (mode_maps)
    apply_mode_maps (x, mode_maps, macro, value, infile, unknown_mode_attr);

  /* Change each string and recursively change each rtx.  */
  format_ptr = GET_RTX_FORMAT (bellwether_code);
  for (i = 0; format_ptr[i] != 0; i++)
    switch (format_ptr[i])
      {
      case 'T':
	XTMPL (x, i) = apply_macro_to_string (XTMPL (x, i), macro, value);
	break;

      case 'S':
      case 's':
	XSTR (x, i) = apply_macro_to_string (XSTR (x, i), macro, value);
	break;

      case 'e':
	XEXP (x, i) = apply_macro_to_rtx (XEXP (x, i), macro, value,
					  mode_maps, infile,
					  unknown_mode_attr);
	break;

      case 'V':
      case 'E':
	if (XVEC (original, i))
	  {
	    XVEC (x, i) = rtvec_alloc (XVECLEN (original, i));
	    for (j = 0; j < XVECLEN (x, i); j++)
	      XVECEXP (x, i, j) = apply_macro_to_rtx (XVECEXP (original, i, j),
						      macro, value, mode_maps,
						      infile,
						      unknown_mode_attr);
	  }
	break;

      default:
	break;
      }
  return x;
}

/* Return true if X (or some subexpression of X) uses macro MACRO.  */

static bool
uses_macro_p (rtx x, struct mapping *macro)
{
  struct macro_group *group;
  const char *format_ptr;
  int i, j;

  if (x == 0)
    return false;

  group = macro->group;
  if (group->uses_macro_p (x, macro->index + group->num_builtins))
    return true;

  format_ptr = GET_RTX_FORMAT (BELLWETHER_CODE (GET_CODE (x)));
  for (i = 0; format_ptr[i] != 0; i++)
    switch (format_ptr[i])
      {
      case 'e':
	if (uses_macro_p (XEXP (x, i), macro))
	  return true;
	break;

      case 'V':
      case 'E':
	if (XVEC (x, i))
	  for (j = 0; j < XVECLEN (x, i); j++)
	    if (uses_macro_p (XVECEXP (x, i, j), macro))
	      return true;
	break;

      default:
	break;
      }
  return false;
}

/* Return a condition that must satisfy both ORIGINAL and EXTRA.  If ORIGINAL
   has the form "&& ..." (as used in define_insn_and_splits), assume that
   EXTRA is already satisfied.  Empty strings are treated like "true".  */

static const char *
add_condition_to_string (const char *original, const char *extra)
{
  if (original != 0 && original[0] == '&' && original[1] == '&')
    return original;
  return join_c_conditions (original, extra);
}

/* Like add_condition, but applied to all conditions in rtx X.  */

static void
add_condition_to_rtx (rtx x, const char *extra)
{
  switch (GET_CODE (x))
    {
    case DEFINE_INSN:
    case DEFINE_EXPAND:
      XSTR (x, 2) = add_condition_to_string (XSTR (x, 2), extra);
      break;

    case DEFINE_SPLIT:
    case DEFINE_PEEPHOLE:
    case DEFINE_PEEPHOLE2:
    case DEFINE_COND_EXEC:
      XSTR (x, 1) = add_condition_to_string (XSTR (x, 1), extra);
      break;

    case DEFINE_INSN_AND_SPLIT:
      XSTR (x, 2) = add_condition_to_string (XSTR (x, 2), extra);
      XSTR (x, 4) = add_condition_to_string (XSTR (x, 4), extra);
      break;

    default:
      break;
    }
}

/* A htab_traverse callback.  Search the EXPR_LIST given by DATA
   for rtxes that use the macro in *SLOT.  Replace each such rtx
   with a list of expansions.  */

static int
apply_macro_traverse (void **slot, void *data)
{
  struct macro_traverse_data *mtd = (struct macro_traverse_data *) data;
  struct mapping *macro;
  struct map_value *v;
  rtx elem, new_elem, original, x;

  macro = (struct mapping *) *slot;
  for (elem = mtd->queue; elem != 0; elem = XEXP (elem, 1))
    if (uses_macro_p (XEXP (elem, 0), macro))
      {
	/* For each macro we expand, we set UNKNOWN_MODE_ATTR to NULL.
	   If apply_macro_rtx finds an unknown attribute for a mode,
	   it will set it to the attribute.  We want to know whether
	   the attribute is unknown after we have expanded all
	   possible macros, so setting it to NULL here gives us the
	   right result when the hash table traversal is complete.  */
	mtd->unknown_mode_attr = NULL;

	original = XEXP (elem, 0);
	for (v = macro->values; v != 0; v = v->next)
	  {
	    x = apply_macro_to_rtx (original, macro, v->number,
				    mtd->mode_maps, mtd->infile,
				    &mtd->unknown_mode_attr);
	    add_condition_to_rtx (x, v->string);
	    if (v != macro->values)
	      {
		/* Insert a new EXPR_LIST node after ELEM and put the
		   new expansion there.  */
		new_elem = rtx_alloc (EXPR_LIST);
		XEXP (new_elem, 1) = XEXP (elem, 1);
		XEXP (elem, 1) = new_elem;
		elem = new_elem;
	      }
	    XEXP (elem, 0) = x;
	  }
    }
  return 1;
}

/* Add a new "mapping" structure to hashtable TABLE.  NAME is the name
   of the mapping, GROUP is the group to which it belongs, and INFILE
   is the file that defined the mapping.  */

static struct mapping *
add_mapping (struct macro_group *group, htab_t table,
	     const char *name, FILE *infile)
{
  struct mapping *m;
  void **slot;

  m = XNEW (struct mapping);
  m->name = xstrdup (name);
  m->group = group;
  m->index = htab_elements (table);
  m->values = 0;

  slot = htab_find_slot (table, m, INSERT);
  if (*slot != 0)
    fatal_with_file_and_line (infile, "`%s' already defined", name);

  *slot = m;
  return m;
}

/* Add the pair (NUMBER, STRING) to a list of map_value structures.
   END_PTR points to the current null terminator for the list; return
   a pointer the new null terminator.  */

static struct map_value **
add_map_value (struct map_value **end_ptr, int number, const char *string)
{
  struct map_value *value;

  value = XNEW (struct map_value);
  value->next = 0;
  value->number = number;
  value->string = string;

  *end_ptr = value;
  return &value->next;
}

/* Do one-time initialization of the mode and code attributes.  */

static void
initialize_macros (void)
{
  struct mapping *lower, *upper;
  struct map_value **lower_ptr, **upper_ptr;
  char *copy, *p;
  int i;

  modes.attrs = htab_create (13, def_hash, def_name_eq_p, 0);
  modes.macros = htab_create (13, def_hash, def_name_eq_p, 0);
  modes.num_builtins = MAX_MACHINE_MODE;
  modes.find_builtin = find_mode;
  modes.uses_macro_p = uses_mode_macro_p;
  modes.apply_macro = apply_mode_macro;

  codes.attrs = htab_create (13, def_hash, def_name_eq_p, 0);
  codes.macros = htab_create (13, def_hash, def_name_eq_p, 0);
  codes.num_builtins = NUM_RTX_CODE;
  codes.find_builtin = find_code;
  codes.uses_macro_p = uses_code_macro_p;
  codes.apply_macro = apply_code_macro;

  lower = add_mapping (&modes, modes.attrs, "mode", 0);
  upper = add_mapping (&modes, modes.attrs, "MODE", 0);
  lower_ptr = &lower->values;
  upper_ptr = &upper->values;
  for (i = 0; i < MAX_MACHINE_MODE; i++)
    {
      copy = xstrdup (GET_MODE_NAME (i));
      for (p = copy; *p != 0; p++)
	*p = TOLOWER (*p);

      upper_ptr = add_map_value (upper_ptr, i, GET_MODE_NAME (i));
      lower_ptr = add_map_value (lower_ptr, i, copy);
    }

  lower = add_mapping (&codes, codes.attrs, "code", 0);
  upper = add_mapping (&codes, codes.attrs, "CODE", 0);
  lower_ptr = &lower->values;
  upper_ptr = &upper->values;
  for (i = 0; i < NUM_RTX_CODE; i++)
    {
      copy = xstrdup (GET_RTX_NAME (i));
      for (p = copy; *p != 0; p++)
	*p = TOUPPER (*p);

      lower_ptr = add_map_value (lower_ptr, i, GET_RTX_NAME (i));
      upper_ptr = add_map_value (upper_ptr, i, copy);
    }
}

/* Return a hash value for the pointer pointed to by DEF.  */

static hashval_t
leading_ptr_hash (const void *def)
{
  return htab_hash_pointer (*(const void *const *) def);
}

/* Return true if DEF1 and DEF2 are pointers to the same pointer.  */

static int
leading_ptr_eq_p (const void *def1, const void *def2)
{
  return *(const void *const *) def1 == *(const void *const *) def2;
}

/* Associate PTR with the file position given by FILENAME and LINENO.  */

static void
set_rtx_ptr_loc (const void *ptr, const char *filename, int lineno)
{
  struct ptr_loc *loc;

  loc = (struct ptr_loc *) obstack_alloc (&ptr_loc_obstack,
					  sizeof (struct ptr_loc));
  loc->ptr = ptr;
  loc->filename = filename;
  loc->lineno = lineno;
  *htab_find_slot (ptr_locs, loc, INSERT) = loc;
}

/* Return the position associated with pointer PTR.  Return null if no
   position was set.  */

static const struct ptr_loc *
get_rtx_ptr_loc (const void *ptr)
{
  return (const struct ptr_loc *) htab_find (ptr_locs, &ptr);
}

/* Associate NEW_PTR with the same file position as OLD_PTR.  */

void
copy_rtx_ptr_loc (const void *new_ptr, const void *old_ptr)
{
  const struct ptr_loc *loc = get_rtx_ptr_loc (old_ptr);
  if (loc != 0)
    set_rtx_ptr_loc (new_ptr, loc->filename, loc->lineno);
}

/* If PTR is associated with a known file position, print a #line
   directive for it.  */

void
print_rtx_ptr_loc (const void *ptr)
{
  const struct ptr_loc *loc = get_rtx_ptr_loc (ptr);
  if (loc != 0)
    printf ("#line %d \"%s\"\n", loc->lineno, loc->filename);
}

/* Return a condition that satisfies both COND1 and COND2.  Either string
   may be null or empty.  */

const char *
join_c_conditions (const char *cond1, const char *cond2)
{
  char *result;
  const void **entry;

  if (cond1 == 0 || cond1[0] == 0)
    return cond2;

  if (cond2 == 0 || cond2[0] == 0)
    return cond1;

  result = concat ("(", cond1, ") && (", cond2, ")", NULL);
  obstack_ptr_grow (&joined_conditions_obstack, result);
  obstack_ptr_grow (&joined_conditions_obstack, cond1);
  obstack_ptr_grow (&joined_conditions_obstack, cond2);
  entry = XOBFINISH (&joined_conditions_obstack, const void **);
  *htab_find_slot (joined_conditions, entry, INSERT) = entry;
  return result;
}

/* Print condition COND, wrapped in brackets.  If COND was created by
   join_c_conditions, recursively invoke this function for the original
   conditions and join the result with "&&".  Otherwise print a #line
   directive for COND if its original file position is known.  */

void
print_c_condition (const char *cond)
{
  const char **halves = (const char **) htab_find (joined_conditions, &cond);
  if (halves != 0)
    {
      printf ("(");
      print_c_condition (halves[1]);
      printf (" && ");
      print_c_condition (halves[2]);
      printf (")");
    }
  else
    {
      putc ('\n', stdout);
      print_rtx_ptr_loc (cond);
      printf ("(%s)", cond);
    }
}

/* Read chars from INFILE until a non-whitespace char
   and return that.  Comments, both Lisp style and C style,
   are treated as whitespace.
   Tools such as genflags use this function.  */

int
read_skip_spaces (FILE *infile)
{
  int c;

  while (1)
    {
      c = getc (infile);
      switch (c)
	{
	case '\n':
	  read_rtx_lineno++;
	  break;

	case ' ': case '\t': case '\f': case '\r':
	  break;

	case ';':
	  do
	    c = getc (infile);
	  while (c != '\n' && c != EOF);
	  read_rtx_lineno++;
	  break;

	case '/':
	  {
	    int prevc;
	    c = getc (infile);
	    if (c != '*')
	      fatal_expected_char (infile, '*', c);

	    prevc = 0;
	    while ((c = getc (infile)) && c != EOF)
	      {
		if (c == '\n')
		   read_rtx_lineno++;
	        else if (prevc == '*' && c == '/')
		  break;
	        prevc = c;
	      }
	  }
	  break;

	default:
	  return c;
	}
    }
}

/* Read an rtx code name into the buffer STR[].
   It is terminated by any of the punctuation chars of rtx printed syntax.  */

static void
read_name (char *str, FILE *infile)
{
  char *p;
  int c;

  c = read_skip_spaces (infile);

  p = str;
  while (1)
    {
      if (c == ' ' || c == '\n' || c == '\t' || c == '\f' || c == '\r' || c == EOF)
	break;
      if (c == ':' || c == ')' || c == ']' || c == '"' || c == '/'
	  || c == '(' || c == '[')
	{
	  ungetc (c, infile);
	  break;
	}
      *p++ = c;
      c = getc (infile);
    }
  if (p == str)
    fatal_with_file_and_line (infile, "missing name or number");
  if (c == '\n')
    read_rtx_lineno++;

  *p = 0;

  if (md_constants)
    {
      /* Do constant expansion.  */
      struct md_constant *def;

      p = str;
      do
	{
	  struct md_constant tmp_def;

	  tmp_def.name = p;
	  def = (struct md_constant *) htab_find (md_constants, &tmp_def);
	  if (def)
	    p = def->value;
	} while (def);
      if (p != str)
	strcpy (str, p);
    }
}

/* Subroutine of the string readers.  Handles backslash escapes.
   Caller has read the backslash, but not placed it into the obstack.  */
static void
read_escape (FILE *infile)
{
  int c = getc (infile);

  switch (c)
    {
      /* Backslash-newline is replaced by nothing, as in C.  */
    case '\n':
      read_rtx_lineno++;
      return;

      /* \" \' \\ are replaced by the second character.  */
    case '\\':
    case '"':
    case '\'':
      break;

      /* Standard C string escapes:
	 \a \b \f \n \r \t \v
	 \[0-7] \x
	 all are passed through to the output string unmolested.
	 In normal use these wind up in a string constant processed
	 by the C compiler, which will translate them appropriately.
	 We do not bother checking that \[0-7] are followed by up to
	 two octal digits, or that \x is followed by N hex digits.
	 \? \u \U are left out because they are not in traditional C.  */
    case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
    case '0': case '1': case '2': case '3': case '4': case '5': case '6':
    case '7': case 'x':
      obstack_1grow (&string_obstack, '\\');
      break;

      /* \; makes stuff for a C string constant containing
	 newline and tab.  */
    case ';':
      obstack_grow (&string_obstack, "\\n\\t", 4);
      return;

      /* pass anything else through, but issue a warning.  */
    default:
      fprintf (stderr, "%s:%d: warning: unrecognized escape \\%c\n",
	       read_rtx_filename, read_rtx_lineno, c);
      obstack_1grow (&string_obstack, '\\');
      break;
    }

  obstack_1grow (&string_obstack, c);
}


/* Read a double-quoted string onto the obstack.  Caller has scanned
   the leading quote.  */
static char *
read_quoted_string (FILE *infile)
{
  int c;

  while (1)
    {
      c = getc (infile); /* Read the string  */
      if (c == '\n')
	read_rtx_lineno++;
      else if (c == '\\')
	{
	  read_escape (infile);
	  continue;
	}
      else if (c == '"' || c == EOF)
	break;

      obstack_1grow (&string_obstack, c);
    }

  obstack_1grow (&string_obstack, 0);
  return XOBFINISH (&string_obstack, char *);
}

/* Read a braced string (a la Tcl) onto the string obstack.  Caller
   has scanned the leading brace.  Note that unlike quoted strings,
   the outermost braces _are_ included in the string constant.  */
static char *
read_braced_string (FILE *infile)
{
  int c;
  int brace_depth = 1;  /* caller-processed */
  unsigned long starting_read_rtx_lineno = read_rtx_lineno;

  obstack_1grow (&string_obstack, '{');
  while (brace_depth)
    {
      c = getc (infile); /* Read the string  */

      if (c == '\n')
	read_rtx_lineno++;
      else if (c == '{')
	brace_depth++;
      else if (c == '}')
	brace_depth--;
      else if (c == '\\')
	{
	  read_escape (infile);
	  continue;
	}
      else if (c == EOF)
	fatal_with_file_and_line
	  (infile, "missing closing } for opening brace on line %lu",
	   starting_read_rtx_lineno);

      obstack_1grow (&string_obstack, c);
    }

  obstack_1grow (&string_obstack, 0);
  return XOBFINISH (&string_obstack, char *);
}

/* Read some kind of string constant.  This is the high-level routine
   used by read_rtx.  It handles surrounding parentheses, leading star,
   and dispatch to the appropriate string constant reader.  */

static char *
read_string (FILE *infile, int star_if_braced)
{
  char *stringbuf;
  int saw_paren = 0;
  int c, old_lineno;

  c = read_skip_spaces (infile);
  if (c == '(')
    {
      saw_paren = 1;
      c = read_skip_spaces (infile);
    }

  old_lineno = read_rtx_lineno;
  if (c == '"')
    stringbuf = read_quoted_string (infile);
  else if (c == '{')
    {
      if (star_if_braced)
	obstack_1grow (&string_obstack, '*');
      stringbuf = read_braced_string (infile);
    }
  else
    fatal_with_file_and_line (infile, "expected `\"' or `{', found `%c'", c);

  if (saw_paren)
    {
      c = read_skip_spaces (infile);
      if (c != ')')
	fatal_expected_char (infile, ')', c);
    }

  set_rtx_ptr_loc (stringbuf, read_rtx_filename, old_lineno);
  return stringbuf;
}

/* Provide a version of a function to read a long long if the system does
   not provide one.  */
#if HOST_BITS_PER_WIDE_INT > HOST_BITS_PER_LONG && !defined(HAVE_ATOLL) && !defined(HAVE_ATOQ)
HOST_WIDE_INT atoll (const char *);

HOST_WIDE_INT
atoll (const char *p)
{
  int neg = 0;
  HOST_WIDE_INT tmp_wide;

  while (ISSPACE (*p))
    p++;
  if (*p == '-')
    neg = 1, p++;
  else if (*p == '+')
    p++;

  tmp_wide = 0;
  while (ISDIGIT (*p))
    {
      HOST_WIDE_INT new_wide = tmp_wide*10 + (*p - '0');
      if (new_wide < tmp_wide)
	{
	  /* Return INT_MAX equiv on overflow.  */
	  tmp_wide = (~(unsigned HOST_WIDE_INT) 0) >> 1;
	  break;
	}
      tmp_wide = new_wide;
      p++;
    }

  if (neg)
    tmp_wide = -tmp_wide;
  return tmp_wide;
}
#endif

/* Given an object that starts with a char * name field, return a hash
   code for its name.  */
static hashval_t
def_hash (const void *def)
{
  unsigned result, i;
  const char *string = *(const char *const *) def;

  for (result = i = 0; *string++ != '\0'; i++)
    result += ((unsigned char) *string << (i % CHAR_BIT));
  return result;
}

/* Given two objects that start with char * name fields, return true if
   they have the same name.  */
static int
def_name_eq_p (const void *def1, const void *def2)
{
  return ! strcmp (*(const char *const *) def1,
		   *(const char *const *) def2);
}

/* INFILE is a FILE pointer to read text from.  TMP_CHAR is a buffer suitable
   to read a name or number into.  Process a define_constants directive,
   starting with the optional space after the "define_constants".  */
static void
read_constants (FILE *infile, char *tmp_char)
{
  int c;
  htab_t defs;

  c = read_skip_spaces (infile);
  if (c != '[')
    fatal_expected_char (infile, '[', c);
  defs = md_constants;
  if (! defs)
    defs = htab_create (32, def_hash, def_name_eq_p, (htab_del) 0);
  /* Disable constant expansion during definition processing.  */
  md_constants = 0;
  while ( (c = read_skip_spaces (infile)) != ']')
    {
      struct md_constant *def;
      void **entry_ptr;

      if (c != '(')
	fatal_expected_char (infile, '(', c);
      def = XNEW (struct md_constant);
      def->name = tmp_char;
      read_name (tmp_char, infile);
      entry_ptr = htab_find_slot (defs, def, INSERT);
      if (! *entry_ptr)
	def->name = xstrdup (tmp_char);
      c = read_skip_spaces (infile);
      ungetc (c, infile);
      read_name (tmp_char, infile);
      if (! *entry_ptr)
	{
	  def->value = xstrdup (tmp_char);
	  *entry_ptr = def;
	}
      else
	{
	  def = (struct md_constant *) *entry_ptr;
	  if (strcmp (def->value, tmp_char))
	    fatal_with_file_and_line (infile,
				      "redefinition of %s, was %s, now %s",
				      def->name, def->value, tmp_char);
	}
      c = read_skip_spaces (infile);
      if (c != ')')
	fatal_expected_char (infile, ')', c);
    }
  md_constants = defs;
  c = read_skip_spaces (infile);
  if (c != ')')
    fatal_expected_char (infile, ')', c);
}

/* For every constant definition, call CALLBACK with two arguments:
   a pointer a pointer to the constant definition and INFO.
   Stops when CALLBACK returns zero.  */
void
traverse_md_constants (htab_trav callback, void *info)
{
  if (md_constants)
    htab_traverse (md_constants, callback, info);
}

/* INFILE is a FILE pointer to read text from.  TMP_CHAR is a buffer
   suitable to read a name or number into.  Process a
   define_conditions directive, starting with the optional space after
   the "define_conditions".  The directive looks like this:

     (define_conditions [
        (number "string")
        (number "string")
        ...
     ])

   It's not intended to appear in machine descriptions.  It is
   generated by (the program generated by) genconditions.c, and
   slipped in at the beginning of the sequence of MD files read by
   most of the other generators.  */
static void
read_conditions (FILE *infile, char *tmp_char)
{
  int c;

  c = read_skip_spaces (infile);
  if (c != '[')
    fatal_expected_char (infile, '[', c);

  while ( (c = read_skip_spaces (infile)) != ']')
    {
      char *expr;
      int value;

      if (c != '(')
	fatal_expected_char (infile, '(', c);

      read_name (tmp_char, infile);
      validate_const_int (infile, tmp_char);
      value = atoi (tmp_char);

      c = read_skip_spaces (infile);
      if (c != '"')
	fatal_expected_char (infile, '"', c);
      expr = read_quoted_string (infile);

      c = read_skip_spaces (infile);
      if (c != ')')
	fatal_expected_char (infile, ')', c);

      add_c_test (expr, value);
    }
  c = read_skip_spaces (infile);
  if (c != ')')
    fatal_expected_char (infile, ')', c);
}

static void
validate_const_int (FILE *infile, const char *string)
{
  const char *cp;
  int valid = 1;

  cp = string;
  while (*cp && ISSPACE (*cp))
    cp++;
  if (*cp == '-' || *cp == '+')
    cp++;
  if (*cp == 0)
    valid = 0;
  for (; *cp; cp++)
    if (! ISDIGIT (*cp))
      valid = 0;
  if (!valid)
    fatal_with_file_and_line (infile, "invalid decimal constant \"%s\"\n", string);
}

/* Search GROUP for a mode or code called NAME and return its numerical
   identifier.  INFILE is the file that contained NAME.  */

static int
find_macro (struct macro_group *group, const char *name, FILE *infile)
{
  struct mapping *m;

  m = (struct mapping *) htab_find (group->macros, &name);
  if (m != 0)
    return m->index + group->num_builtins;
  return group->find_builtin (name, infile);
}

/* Finish reading a declaration of the form:

       (define... <name> [<value1> ... <valuen>])

   from INFILE, where each <valuei> is either a bare symbol name or a
   "(<name> <string>)" pair.  The "(define..." part has already been read.

   Represent the declaration as a "mapping" structure; add it to TABLE
   (which belongs to GROUP) and return it.  */

static struct mapping *
read_mapping (struct macro_group *group, htab_t table, FILE *infile)
{
  char tmp_char[256];
  struct mapping *m;
  struct map_value **end_ptr;
  const char *string;
  int number, c;

  /* Read the mapping name and create a structure for it.  */
  read_name (tmp_char, infile);
  m = add_mapping (group, table, tmp_char, infile);

  c = read_skip_spaces (infile);
  if (c != '[')
    fatal_expected_char (infile, '[', c);

  /* Read each value.  */
  end_ptr = &m->values;
  c = read_skip_spaces (infile);
  do
    {
      if (c != '(')
	{
	  /* A bare symbol name that is implicitly paired to an
	     empty string.  */
	  ungetc (c, infile);
	  read_name (tmp_char, infile);
	  string = "";
	}
      else
	{
	  /* A "(name string)" pair.  */
	  read_name (tmp_char, infile);
	  string = read_string (infile, false);
	  c = read_skip_spaces (infile);
	  if (c != ')')
	    fatal_expected_char (infile, ')', c);
	}
      number = group->find_builtin (tmp_char, infile);
      end_ptr = add_map_value (end_ptr, number, string);
      c = read_skip_spaces (infile);
    }
  while (c != ']');

  c = read_skip_spaces (infile);
  if (c != ')')
    fatal_expected_char (infile, ')', c);

  return m;
}

/* Check newly-created code macro MACRO to see whether every code has the
   same format.  Initialize the macro's entry in bellwether_codes.  */

static void
check_code_macro (struct mapping *macro, FILE *infile)
{
  struct map_value *v;
  enum rtx_code bellwether;

  bellwether = (enum rtx_code) macro->values->number;
  for (v = macro->values->next; v != 0; v = v->next)
    if (strcmp (GET_RTX_FORMAT (bellwether), GET_RTX_FORMAT (v->number)) != 0)
      fatal_with_file_and_line (infile, "code macro `%s' combines "
				"different rtx formats", macro->name);

  bellwether_codes = XRESIZEVEC (enum rtx_code, bellwether_codes,
				 macro->index + 1);
  bellwether_codes[macro->index] = bellwether;
}

/* Read an rtx in printed representation from INFILE and store its
   core representation in *X.  Also store the line number of the
   opening '(' in *LINENO.  Return true on success or false if the
   end of file has been reached.

   read_rtx is not used in the compiler proper, but rather in
   the utilities gen*.c that construct C code from machine descriptions.  */

bool
read_rtx (FILE *infile, rtx *x, int *lineno)
{
  static rtx queue_head, queue_next;
  static int queue_lineno;
  int c;

  /* Do one-time initialization.  */
  if (queue_head == 0)
    {
      initialize_macros ();
      obstack_init (&string_obstack);
      queue_head = rtx_alloc (EXPR_LIST);
      ptr_locs = htab_create (161, leading_ptr_hash, leading_ptr_eq_p, 0);
      obstack_init (&ptr_loc_obstack);
      joined_conditions = htab_create (161, leading_ptr_hash,
				       leading_ptr_eq_p, 0);
      obstack_init (&joined_conditions_obstack);
    }

  if (queue_next == 0)
    {
      struct map_value *mode_maps;
      struct macro_traverse_data mtd;
      rtx from_file;

      c = read_skip_spaces (infile);
      if (c == EOF)
	return false;
      ungetc (c, infile);

      queue_lineno = read_rtx_lineno;
      mode_maps = 0;
      from_file = read_rtx_1 (infile, &mode_maps);
      if (from_file == 0)
	return false;  /* This confuses a top level (nil) with end of
			  file, but a top level (nil) would have
			  crashed our caller anyway.  */

      queue_next = queue_head;
      XEXP (queue_next, 0) = from_file;
      XEXP (queue_next, 1) = 0;

      mtd.queue = queue_next;
      mtd.mode_maps = mode_maps;
      mtd.infile = infile;
      mtd.unknown_mode_attr = mode_maps ? mode_maps->string : NULL;
      htab_traverse (modes.macros, apply_macro_traverse, &mtd);
      htab_traverse (codes.macros, apply_macro_traverse, &mtd);
      if (mtd.unknown_mode_attr)
	fatal_with_file_and_line (infile,
				  "undefined attribute '%s' used for mode",
				  mtd.unknown_mode_attr);
    }

  *x = XEXP (queue_next, 0);
  *lineno = queue_lineno;
  queue_next = XEXP (queue_next, 1);

  return true;
}

/* Subroutine of read_rtx that reads one construct from INFILE but
   doesn't apply any macros.  */

static rtx
read_rtx_1 (FILE *infile, struct map_value **mode_maps)
{
  int i;
  RTX_CODE real_code, bellwether_code;
  const char *format_ptr;
  /* tmp_char is a buffer used for reading decimal integers
     and names of rtx types and machine modes.
     Therefore, 256 must be enough.  */
  char tmp_char[256];
  rtx return_rtx;
  int c;
  int tmp_int;
  HOST_WIDE_INT tmp_wide;

  /* Linked list structure for making RTXs: */
  struct rtx_list
    {
      struct rtx_list *next;
      rtx value;		/* Value of this node.  */
    };

 again:
  c = read_skip_spaces (infile); /* Should be open paren.  */

  if (c == EOF)
    return 0;
  
  if (c != '(')
    fatal_expected_char (infile, '(', c);

  read_name (tmp_char, infile);
  if (strcmp (tmp_char, "nil") == 0)
    {
      /* (nil) stands for an expression that isn't there.  */
      c = read_skip_spaces (infile);
      if (c != ')')
	fatal_expected_char (infile, ')', c);
      return 0;
    }
  if (strcmp (tmp_char, "define_constants") == 0)
    {
      read_constants (infile, tmp_char);
      goto again;
    }
  if (strcmp (tmp_char, "define_conditions") == 0)
    {
      read_conditions (infile, tmp_char);
      goto again;
    }
  if (strcmp (tmp_char, "define_mode_attr") == 0)
    {
      read_mapping (&modes, modes.attrs, infile);
      goto again;
    }
  if (strcmp (tmp_char, "define_mode_macro") == 0)
    {
      read_mapping (&modes, modes.macros, infile);
      goto again;
    }
  if (strcmp (tmp_char, "define_code_attr") == 0)
    {
      read_mapping (&codes, codes.attrs, infile);
      goto again;
    }
  if (strcmp (tmp_char, "define_code_macro") == 0)
    {
      check_code_macro (read_mapping (&codes, codes.macros, infile), infile);
      goto again;
    }
  real_code = (enum rtx_code) find_macro (&codes, tmp_char, infile);
  bellwether_code = BELLWETHER_CODE (real_code);

  /* If we end up with an insn expression then we free this space below.  */
  return_rtx = rtx_alloc (bellwether_code);
  format_ptr = GET_RTX_FORMAT (bellwether_code);
  PUT_CODE (return_rtx, real_code);

  /* If what follows is `: mode ', read it and
     store the mode in the rtx.  */

  i = read_skip_spaces (infile);
  if (i == ':')
    {
      unsigned int mode;

      read_name (tmp_char, infile);
      if (tmp_char[0] != '<' || tmp_char[strlen (tmp_char) - 1] != '>')
	mode = find_macro (&modes, tmp_char, infile);
      else
	mode = mode_attr_index (mode_maps, tmp_char);
      PUT_MODE (return_rtx, (enum machine_mode) mode);
      if (GET_MODE (return_rtx) != mode)
	fatal_with_file_and_line (infile, "mode too large");
    }
  else
    ungetc (i, infile);

  for (i = 0; format_ptr[i] != 0; i++)
    switch (format_ptr[i])
      {
	/* 0 means a field for internal use only.
	   Don't expect it to be present in the input.  */
      case '0':
	break;

      case 'e':
      case 'u':
	XEXP (return_rtx, i) = read_rtx_1 (infile, mode_maps);
	break;

      case 'V':
	/* 'V' is an optional vector: if a closeparen follows,
	   just store NULL for this element.  */
	c = read_skip_spaces (infile);
	ungetc (c, infile);
	if (c == ')')
	  {
	    XVEC (return_rtx, i) = 0;
	    break;
	  }
	/* Now process the vector.  */

      case 'E':
	{
	  /* Obstack to store scratch vector in.  */
	  struct obstack vector_stack;
	  int list_counter = 0;
	  rtvec return_vec = NULL_RTVEC;

	  c = read_skip_spaces (infile);
	  if (c != '[')
	    fatal_expected_char (infile, '[', c);

	  /* Add expressions to a list, while keeping a count.  */
	  obstack_init (&vector_stack);
	  while ((c = read_skip_spaces (infile)) && c != ']')
	    {
	      ungetc (c, infile);
	      list_counter++;
	      obstack_ptr_grow (&vector_stack, read_rtx_1 (infile, mode_maps));
	    }
	  if (list_counter > 0)
	    {
	      return_vec = rtvec_alloc (list_counter);
	      memcpy (&return_vec->elem[0], obstack_finish (&vector_stack),
		      list_counter * sizeof (rtx));
	    }
	  else if (format_ptr[i] == 'E')
	    fatal_with_file_and_line (infile,
				      "vector must have at least one element");
	  XVEC (return_rtx, i) = return_vec;
	  obstack_free (&vector_stack, NULL);
	  /* close bracket gotten */
	}
	break;

      case 'S':
      case 'T':
      case 's':
	{
	  char *stringbuf;
	  int star_if_braced;

	  c = read_skip_spaces (infile);
	  ungetc (c, infile);
	  if (c == ')')
	    {
	      /* 'S' fields are optional and should be NULL if no string
		 was given.  Also allow normal 's' and 'T' strings to be
		 omitted, treating them in the same way as empty strings.  */
	      XSTR (return_rtx, i) = (format_ptr[i] == 'S' ? NULL : "");
	      break;
	    }

	  /* The output template slot of a DEFINE_INSN,
	     DEFINE_INSN_AND_SPLIT, or DEFINE_PEEPHOLE automatically
	     gets a star inserted as its first character, if it is
	     written with a brace block instead of a string constant.  */
	  star_if_braced = (format_ptr[i] == 'T');

	  stringbuf = read_string (infile, star_if_braced);

	  /* For insn patterns, we want to provide a default name
	     based on the file and line, like "*foo.md:12", if the
	     given name is blank.  These are only for define_insn and
	     define_insn_and_split, to aid debugging.  */
	  if (*stringbuf == '\0'
	      && i == 0
	      && (GET_CODE (return_rtx) == DEFINE_INSN
		  || GET_CODE (return_rtx) == DEFINE_INSN_AND_SPLIT))
	    {
	      char line_name[20];
	      const char *fn = (read_rtx_filename ? read_rtx_filename : "rtx");
	      const char *slash;
	      for (slash = fn; *slash; slash ++)
		if (*slash == '/' || *slash == '\\' || *slash == ':')
		  fn = slash + 1;
	      obstack_1grow (&string_obstack, '*');
	      obstack_grow (&string_obstack, fn, strlen (fn));
	      sprintf (line_name, ":%d", read_rtx_lineno);
	      obstack_grow (&string_obstack, line_name, strlen (line_name)+1);
	      stringbuf = XOBFINISH (&string_obstack, char *);
	    }

	  if (star_if_braced)
	    XTMPL (return_rtx, i) = stringbuf;
	  else
	    XSTR (return_rtx, i) = stringbuf;
	}
	break;

      case 'w':
	read_name (tmp_char, infile);
	validate_const_int (infile, tmp_char);
#if HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_INT
	tmp_wide = atoi (tmp_char);
#else
#if HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG
	tmp_wide = atol (tmp_char);
#else
	/* Prefer atoll over atoq, since the former is in the ISO C99 standard.
	   But prefer not to use our hand-rolled function above either.  */
#if defined(HAVE_ATOLL) || !defined(HAVE_ATOQ)
	tmp_wide = atoll (tmp_char);
#else
	tmp_wide = atoq (tmp_char);
#endif
#endif
#endif
	XWINT (return_rtx, i) = tmp_wide;
	break;

      case 'i':
      case 'n':
	read_name (tmp_char, infile);
	validate_const_int (infile, tmp_char);
	tmp_int = atoi (tmp_char);
	XINT (return_rtx, i) = tmp_int;
	break;

      default:
	gcc_unreachable ();
      }

  c = read_skip_spaces (infile);
  if (c != ')')
    {
      /* Syntactic sugar for AND and IOR, allowing Lisp-like
	 arbitrary number of arguments for them.  */
      if (c == '(' && (GET_CODE (return_rtx) == AND
		       || GET_CODE (return_rtx) == IOR))
	return read_rtx_variadic (infile, mode_maps, return_rtx);
      else
	fatal_expected_char (infile, ')', c);
    }

  return return_rtx;
}

/* Mutually recursive subroutine of read_rtx which reads
   (thing x1 x2 x3 ...) and produces RTL as if
   (thing x1 (thing x2 (thing x3 ...)))  had been written.
   When called, FORM is (thing x1 x2), and the file position
   is just past the leading parenthesis of x3.  Only works
   for THINGs which are dyadic expressions, e.g. AND, IOR.  */
static rtx
read_rtx_variadic (FILE *infile, struct map_value **mode_maps, rtx form)
{
  char c = '(';
  rtx p = form, q;

  do
    {
      ungetc (c, infile);

      q = rtx_alloc (GET_CODE (p));
      PUT_MODE (q, GET_MODE (p));

      XEXP (q, 0) = XEXP (p, 1);
      XEXP (q, 1) = read_rtx_1 (infile, mode_maps);
      
      XEXP (p, 1) = q;
      p = q;
      c = read_skip_spaces (infile);
    }
  while (c == '(');

  if (c != ')')
    fatal_expected_char (infile, ')', c);

  return form;
}
