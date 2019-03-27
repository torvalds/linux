/* Support for printing C and C++ types for GDB, the GNU debugger.
   Copyright 1986, 1988, 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1998,
   1999, 2000, 2001, 2002, 2003
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
#include "gdb_obstack.h"
#include "bfd.h"		/* Binary File Description */
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "value.h"
#include "gdbcore.h"
#include "target.h"
#include "language.h"
#include "demangle.h"
#include "c-lang.h"
#include "typeprint.h"
#include "cp-abi.h"

#include "gdb_string.h"
#include <errno.h>

/* Flag indicating target was compiled by HP compiler */
extern int hp_som_som_object_present;

static void cp_type_print_method_args (struct type *mtype, char *prefix,
				       char *varstring, int staticp,
				       struct ui_file *stream);

static void c_type_print_args (struct type *, struct ui_file *);

static void cp_type_print_derivation_info (struct ui_file *, struct type *);

static void c_type_print_varspec_prefix (struct type *, struct ui_file *, int,
				         int, int);

/* Print "const", "volatile", or address space modifiers. */
static void c_type_print_modifier (struct type *, struct ui_file *,
				   int, int);




/* LEVEL is the depth to indent lines by.  */

void
c_print_type (struct type *type, char *varstring, struct ui_file *stream,
	      int show, int level)
{
  enum type_code code;
  int demangled_args;
  int need_post_space;

  if (show > 0)
    CHECK_TYPEDEF (type);

  c_type_print_base (type, stream, show, level);
  code = TYPE_CODE (type);
  if ((varstring != NULL && *varstring != '\0')
      ||
  /* Need a space if going to print stars or brackets;
     but not if we will print just a type name.  */
      ((show > 0 || TYPE_NAME (type) == 0)
       &&
       (code == TYPE_CODE_PTR || code == TYPE_CODE_FUNC
	|| code == TYPE_CODE_METHOD
	|| code == TYPE_CODE_ARRAY
	|| code == TYPE_CODE_MEMBER
	|| code == TYPE_CODE_REF)))
    fputs_filtered (" ", stream);
  need_post_space = (varstring != NULL && strcmp (varstring, "") != 0);
  c_type_print_varspec_prefix (type, stream, show, 0, need_post_space);

  if (varstring != NULL)
    {
      fputs_filtered (varstring, stream);

      /* For demangled function names, we have the arglist as part of the name,
         so don't print an additional pair of ()'s */

      demangled_args = strchr (varstring, '(') != NULL;
      c_type_print_varspec_suffix (type, stream, show, 0, demangled_args);
    }
}

/* If TYPE is a derived type, then print out derivation information.
   Print only the actual base classes of this type, not the base classes
   of the base classes.  I.E.  for the derivation hierarchy:

   class A { int a; };
   class B : public A {int b; };
   class C : public B {int c; };

   Print the type of class C as:

   class C : public B {
   int c;
   }

   Not as the following (like gdb used to), which is not legal C++ syntax for
   derived types and may be confused with the multiple inheritance form:

   class C : public B : public A {
   int c;
   }

   In general, gdb should try to print the types as closely as possible to
   the form that they appear in the source code. 
   Note that in case of protected derivation gcc will not say 'protected' 
   but 'private'. The HP's aCC compiler emits specific information for 
   derivation via protected inheritance, so gdb can print it out */

static void
cp_type_print_derivation_info (struct ui_file *stream, struct type *type)
{
  char *name;
  int i;

  for (i = 0; i < TYPE_N_BASECLASSES (type); i++)
    {
      fputs_filtered (i == 0 ? ": " : ", ", stream);
      fprintf_filtered (stream, "%s%s ",
			BASETYPE_VIA_PUBLIC (type, i) ? "public"
	       : (TYPE_FIELD_PROTECTED (type, i) ? "protected" : "private"),
			BASETYPE_VIA_VIRTUAL (type, i) ? " virtual" : "");
      name = type_name_no_tag (TYPE_BASECLASS (type, i));
      fprintf_filtered (stream, "%s", name ? name : "(null)");
    }
  if (i > 0)
    {
      fputs_filtered (" ", stream);
    }
}

/* Print the C++ method arguments ARGS to the file STREAM.  */

static void
cp_type_print_method_args (struct type *mtype, char *prefix, char *varstring,
			   int staticp, struct ui_file *stream)
{
  struct field *args = TYPE_FIELDS (mtype);
  int nargs = TYPE_NFIELDS (mtype);
  int varargs = TYPE_VARARGS (mtype);
  int i;

  fprintf_symbol_filtered (stream, prefix, language_cplus, DMGL_ANSI);
  fprintf_symbol_filtered (stream, varstring, language_cplus, DMGL_ANSI);
  fputs_filtered ("(", stream);

  /* Skip the class variable.  */
  i = staticp ? 0 : 1;
  if (nargs > i)
    {
      while (i < nargs)
	{
	  type_print (args[i++].type, "", stream, 0);

	  if (i == nargs && varargs)
	    fprintf_filtered (stream, ", ...");
	  else if (i < nargs)
	    fprintf_filtered (stream, ", ");
	}
    }
  else if (varargs)
    fprintf_filtered (stream, "...");
  else if (current_language->la_language == language_cplus)
    fprintf_filtered (stream, "void");

  fprintf_filtered (stream, ")");
}


/* Print any asterisks or open-parentheses needed before the
   variable name (to describe its type).

   On outermost call, pass 0 for PASSED_A_PTR.
   On outermost call, SHOW > 0 means should ignore
   any typename for TYPE and show its details.
   SHOW is always zero on recursive calls.
   
   NEED_POST_SPACE is non-zero when a space will be be needed
   between a trailing qualifier and a field, variable, or function
   name.  */

void
c_type_print_varspec_prefix (struct type *type, struct ui_file *stream,
			     int show, int passed_a_ptr, int need_post_space)
{
  char *name;
  if (type == 0)
    return;

  if (TYPE_NAME (type) && show <= 0)
    return;

  QUIT;

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_PTR:
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 1, 1);
      fprintf_filtered (stream, "*");
      c_type_print_modifier (type, stream, 1, need_post_space);
      break;

    case TYPE_CODE_MEMBER:
      if (passed_a_ptr)
	fprintf_filtered (stream, "(");
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 0, 0);
      fprintf_filtered (stream, " ");
      name = type_name_no_tag (TYPE_DOMAIN_TYPE (type));
      if (name)
	fputs_filtered (name, stream);
      else
	c_type_print_base (TYPE_DOMAIN_TYPE (type), stream, 0, passed_a_ptr);
      fprintf_filtered (stream, "::");
      break;

    case TYPE_CODE_METHOD:
      if (passed_a_ptr)
	fprintf_filtered (stream, "(");
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 0, 0);
      if (passed_a_ptr)
	{
	  fprintf_filtered (stream, " ");
	  c_type_print_base (TYPE_DOMAIN_TYPE (type), stream, 0, passed_a_ptr);
	  fprintf_filtered (stream, "::");
	}
      break;

    case TYPE_CODE_REF:
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 1, 0);
      fprintf_filtered (stream, "&");
      c_type_print_modifier (type, stream, 1, need_post_space);
      break;

    case TYPE_CODE_FUNC:
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 0, 0);
      if (passed_a_ptr)
	fprintf_filtered (stream, "(");
      break;

    case TYPE_CODE_ARRAY:
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 0, 0);
      if (passed_a_ptr)
	fprintf_filtered (stream, "(");
      break;

    case TYPE_CODE_TYPEDEF:
      c_type_print_varspec_prefix (TYPE_TARGET_TYPE (type), stream, show, 0, 0);
      break;

    case TYPE_CODE_UNDEF:
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_INT:
    case TYPE_CODE_FLT:
    case TYPE_CODE_VOID:
    case TYPE_CODE_ERROR:
    case TYPE_CODE_CHAR:
    case TYPE_CODE_BOOL:
    case TYPE_CODE_SET:
    case TYPE_CODE_RANGE:
    case TYPE_CODE_STRING:
    case TYPE_CODE_BITSTRING:
    case TYPE_CODE_COMPLEX:
    case TYPE_CODE_TEMPLATE:
    case TYPE_CODE_NAMESPACE:
      /* These types need no prefix.  They are listed here so that
         gcc -Wall will reveal any types that haven't been handled.  */
      break;
    default:
      error ("type not handled in c_type_print_varspec_prefix()");
      break;
    }
}

/* Print out "const" and "volatile" attributes.
   TYPE is a pointer to the type being printed out.
   STREAM is the output destination.
   NEED_SPACE = 1 indicates an initial white space is needed */

static void
c_type_print_modifier (struct type *type, struct ui_file *stream,
		       int need_pre_space, int need_post_space)
{
  int did_print_modifier = 0;
  const char *address_space_id;

  /* We don't print `const' qualifiers for references --- since all
     operators affect the thing referenced, not the reference itself,
     every reference is `const'.  */
  if (TYPE_CONST (type)
      && TYPE_CODE (type) != TYPE_CODE_REF)
    {
      if (need_pre_space)
	fprintf_filtered (stream, " ");
      fprintf_filtered (stream, "const");
      did_print_modifier = 1;
    }

  if (TYPE_VOLATILE (type))
    {
      if (did_print_modifier || need_pre_space)
	fprintf_filtered (stream, " ");
      fprintf_filtered (stream, "volatile");
      did_print_modifier = 1;
    }

  address_space_id = address_space_int_to_name (TYPE_INSTANCE_FLAGS (type));
  if (address_space_id)
    {
      if (did_print_modifier || need_pre_space)
	fprintf_filtered (stream, " ");
      fprintf_filtered (stream, "@%s", address_space_id);
      did_print_modifier = 1;
    }

  if (did_print_modifier && need_post_space)
    fprintf_filtered (stream, " ");
}




static void
c_type_print_args (struct type *type, struct ui_file *stream)
{
  int i;
  struct field *args;

  fprintf_filtered (stream, "(");
  args = TYPE_FIELDS (type);
  if (args != NULL)
    {
      int i;

      /* FIXME drow/2002-05-31: Always skips the first argument,
	 should we be checking for static members?  */

      for (i = 1; i < TYPE_NFIELDS (type); i++)
	{
	  c_print_type (args[i].type, "", stream, -1, 0);
	  if (i != TYPE_NFIELDS (type))
	    {
	      fprintf_filtered (stream, ",");
	      wrap_here ("    ");
	    }
	}
      if (TYPE_VARARGS (type))
	fprintf_filtered (stream, "...");
      else if (i == 1
	       && (current_language->la_language == language_cplus))
	fprintf_filtered (stream, "void");
    }
  else if (current_language->la_language == language_cplus)
    {
      fprintf_filtered (stream, "void");
    }

  fprintf_filtered (stream, ")");
}


/* Return true iff the j'th overloading of the i'th method of TYPE
   is a type conversion operator, like `operator int () { ... }'.
   When listing a class's methods, we don't print the return type of
   such operators.  */
static int
is_type_conversion_operator (struct type *type, int i, int j)
{
  /* I think the whole idea of recognizing type conversion operators
     by their name is pretty terrible.  But I don't think our present
     data structure gives us any other way to tell.  If you know of
     some other way, feel free to rewrite this function.  */
  char *name = TYPE_FN_FIELDLIST_NAME (type, i);

  if (strncmp (name, "operator", 8) != 0)
    return 0;

  name += 8;
  if (! strchr (" \t\f\n\r", *name))
    return 0;

  while (strchr (" \t\f\n\r", *name))
    name++;

  if (!('a' <= *name && *name <= 'z')
      && !('A' <= *name && *name <= 'Z')
      && *name != '_')
    /* If this doesn't look like the start of an identifier, then it
       isn't a type conversion operator.  */
    return 0;
  else if (strncmp (name, "new", 3) == 0)
    name += 3;
  else if (strncmp (name, "delete", 6) == 0)
    name += 6;
  else
    /* If it doesn't look like new or delete, it's a type conversion
       operator.  */
    return 1;

  /* Is that really the end of the name?  */
  if (('a' <= *name && *name <= 'z')
      || ('A' <= *name && *name <= 'Z')
      || ('0' <= *name && *name <= '9')
      || *name == '_')
    /* No, so the identifier following "operator" must be a type name,
       and this is a type conversion operator.  */
    return 1;

  /* That was indeed the end of the name, so it was `operator new' or
     `operator delete', neither of which are type conversion operators.  */
  return 0;
}


/* Given a C++ qualified identifier QID, strip off the qualifiers,
   yielding the unqualified name.  The return value is a pointer into
   the original string.

   It's a pity we don't have this information in some more structured
   form.  Even the author of this function feels that writing little
   parsers like this everywhere is stupid.  */
static char *
remove_qualifiers (char *qid)
{
  int quoted = 0;		/* zero if we're not in quotes;
				   '"' if we're in a double-quoted string;
				   '\'' if we're in a single-quoted string.  */
  int depth = 0;		/* number of unclosed parens we've seen */
  char *parenstack = (char *) alloca (strlen (qid));
  char *scan;
  char *last = 0;		/* The character after the rightmost
				   `::' token we've seen so far.  */

  for (scan = qid; *scan; scan++)
    {
      if (quoted)
	{
	  if (*scan == quoted)
	    quoted = 0;
	  else if (*scan == '\\' && *(scan + 1))
	    scan++;
	}
      else if (scan[0] == ':' && scan[1] == ':')
	{
	  /* If we're inside parenthesis (i.e., an argument list) or
	     angle brackets (i.e., a list of template arguments), then
	     we don't record the position of this :: token, since it's
	     not relevant to the top-level structure we're trying
	     to operate on.  */
	  if (depth == 0)
	    {
	      last = scan + 2;
	      scan++;
	    }
	}
      else if (*scan == '"' || *scan == '\'')
	quoted = *scan;
      else if (*scan == '(')
	parenstack[depth++] = ')';
      else if (*scan == '[')
	parenstack[depth++] = ']';
      /* We're going to treat <> as a pair of matching characters,
	 since we're more likely to see those in template id's than
	 real less-than characters.  What a crock.  */
      else if (*scan == '<')
	parenstack[depth++] = '>';
      else if (*scan == ')' || *scan == ']' || *scan == '>')
	{
	  if (depth > 0 && parenstack[depth - 1] == *scan)
	    depth--;
	  else
	    {
	      /* We're going to do a little error recovery here.  If we
		 don't find a match for *scan on the paren stack, but
		 there is something lower on the stack that does match, we
		 pop the stack to that point.  */
	      int i;

	      for (i = depth - 1; i >= 0; i--)
		if (parenstack[i] == *scan)
		  {
		    depth = i;
		    break;
		  }
	    }
	}
    }

  if (last)
    return last;
  else
    /* We didn't find any :: tokens at the top level, so declare the
       whole thing an unqualified identifier.  */
    return qid;
}


/* Print any array sizes, function arguments or close parentheses
   needed after the variable name (to describe its type).
   Args work like c_type_print_varspec_prefix.  */

void
c_type_print_varspec_suffix (struct type *type, struct ui_file *stream,
			     int show, int passed_a_ptr, int demangled_args)
{
  if (type == 0)
    return;

  if (TYPE_NAME (type) && show <= 0)
    return;

  QUIT;

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_ARRAY:
      if (passed_a_ptr)
	fprintf_filtered (stream, ")");

      fprintf_filtered (stream, "[");
      if (TYPE_LENGTH (type) >= 0 && TYPE_LENGTH (TYPE_TARGET_TYPE (type)) > 0
	&& TYPE_ARRAY_UPPER_BOUND_TYPE (type) != BOUND_CANNOT_BE_DETERMINED)
	fprintf_filtered (stream, "%d",
			  (TYPE_LENGTH (type)
			   / TYPE_LENGTH (TYPE_TARGET_TYPE (type))));
      fprintf_filtered (stream, "]");

      c_type_print_varspec_suffix (TYPE_TARGET_TYPE (type), stream, show,
				   0, 0);
      break;

    case TYPE_CODE_MEMBER:
      if (passed_a_ptr)
	fprintf_filtered (stream, ")");
      c_type_print_varspec_suffix (TYPE_TARGET_TYPE (type), stream, show,
				   0, 0);
      break;

    case TYPE_CODE_METHOD:
      if (passed_a_ptr)
	fprintf_filtered (stream, ")");
      c_type_print_varspec_suffix (TYPE_TARGET_TYPE (type), stream, show,
				   0, 0);
      if (passed_a_ptr)
	{
	  c_type_print_args (type, stream);
	}
      break;

    case TYPE_CODE_PTR:
    case TYPE_CODE_REF:
      c_type_print_varspec_suffix (TYPE_TARGET_TYPE (type), stream, show,
				   1, 0);
      break;

    case TYPE_CODE_FUNC:
      if (passed_a_ptr)
	fprintf_filtered (stream, ")");
      if (!demangled_args)
	{
	  int i, len = TYPE_NFIELDS (type);
	  fprintf_filtered (stream, "(");
	  if (len == 0
              && (TYPE_PROTOTYPED (type)
                  || current_language->la_language == language_cplus))
	    {
	      fprintf_filtered (stream, "void");
	    }
	  else
	    for (i = 0; i < len; i++)
	      {
		if (i > 0)
		  {
		    fputs_filtered (", ", stream);
		    wrap_here ("    ");
		  }
		c_print_type (TYPE_FIELD_TYPE (type, i), "", stream, -1, 0);
	      }
	  fprintf_filtered (stream, ")");
	}
      c_type_print_varspec_suffix (TYPE_TARGET_TYPE (type), stream, show,
				   passed_a_ptr, 0);
      break;

    case TYPE_CODE_TYPEDEF:
      c_type_print_varspec_suffix (TYPE_TARGET_TYPE (type), stream, show,
				   passed_a_ptr, 0);
      break;

    case TYPE_CODE_UNDEF:
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_INT:
    case TYPE_CODE_FLT:
    case TYPE_CODE_VOID:
    case TYPE_CODE_ERROR:
    case TYPE_CODE_CHAR:
    case TYPE_CODE_BOOL:
    case TYPE_CODE_SET:
    case TYPE_CODE_RANGE:
    case TYPE_CODE_STRING:
    case TYPE_CODE_BITSTRING:
    case TYPE_CODE_COMPLEX:
    case TYPE_CODE_TEMPLATE:
    case TYPE_CODE_NAMESPACE:
      /* These types do not need a suffix.  They are listed so that
         gcc -Wall will report types that may not have been considered.  */
      break;
    default:
      error ("type not handled in c_type_print_varspec_suffix()");
      break;
    }
}

/* Print the name of the type (or the ultimate pointer target,
   function value or array element), or the description of a
   structure or union.

   SHOW positive means print details about the type (e.g. enum values),
   and print structure elements passing SHOW - 1 for show.
   SHOW negative means just print the type name or struct tag if there is one.
   If there is no name, print something sensible but concise like
   "struct {...}".
   SHOW zero means just print the type name or struct tag if there is one.
   If there is no name, print something sensible but not as concise like
   "struct {int x; int y;}".

   LEVEL is the number of spaces to indent by.
   We increase it for some recursive calls.  */

void
c_type_print_base (struct type *type, struct ui_file *stream, int show,
		   int level)
{
  int i;
  int len, real_len;
  int lastval;
  char *mangled_name;
  char *demangled_name;
  char *demangled_no_static;
  enum
    {
      s_none, s_public, s_private, s_protected
    }
  section_type;
  int need_access_label = 0;
  int j, len2;

  QUIT;

  wrap_here ("    ");
  if (type == NULL)
    {
      fputs_filtered ("<type unknown>", stream);
      return;
    }

  /* When SHOW is zero or less, and there is a valid type name, then always
     just print the type name directly from the type.  */
  /* If we have "typedef struct foo {. . .} bar;" do we want to print it
     as "struct foo" or as "bar"?  Pick the latter, because C++ folk tend
     to expect things like "class5 *foo" rather than "struct class5 *foo".  */

  if (show <= 0
      && TYPE_NAME (type) != NULL)
    {
      c_type_print_modifier (type, stream, 0, 1);
      fputs_filtered (TYPE_NAME (type), stream);
      return;
    }

  CHECK_TYPEDEF (type);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_TYPEDEF:
    case TYPE_CODE_ARRAY:
    case TYPE_CODE_PTR:
    case TYPE_CODE_MEMBER:
    case TYPE_CODE_REF:
    case TYPE_CODE_FUNC:
    case TYPE_CODE_METHOD:
      c_type_print_base (TYPE_TARGET_TYPE (type), stream, show, level);
      break;

    case TYPE_CODE_STRUCT:
      c_type_print_modifier (type, stream, 0, 1);
      /* Note TYPE_CODE_STRUCT and TYPE_CODE_CLASS have the same value,
       * so we use another means for distinguishing them.
       */
      if (HAVE_CPLUS_STRUCT (type))
	{
	  switch (TYPE_DECLARED_TYPE (type))
	    {
	    case DECLARED_TYPE_CLASS:
	      fprintf_filtered (stream, "class ");
	      break;
	    case DECLARED_TYPE_UNION:
	      fprintf_filtered (stream, "union ");
	      break;
	    case DECLARED_TYPE_STRUCT:
	      fprintf_filtered (stream, "struct ");
	      break;
	    default:
	      /* If there is a CPLUS_STRUCT, assume class if not
	       * otherwise specified in the declared_type field.
	       */
	      fprintf_filtered (stream, "class ");
	      break;
	    }			/* switch */
	}
      else
	{
	  /* If not CPLUS_STRUCT, then assume it's a C struct */
	  fprintf_filtered (stream, "struct ");
	}
      goto struct_union;

    case TYPE_CODE_UNION:
      c_type_print_modifier (type, stream, 0, 1);
      fprintf_filtered (stream, "union ");

    struct_union:

      /* Print the tag if it exists. 
       * The HP aCC compiler emits
       * a spurious "{unnamed struct}"/"{unnamed union}"/"{unnamed enum}"
       * tag  for unnamed struct/union/enum's, which we don't
       * want to print.
       */
      if (TYPE_TAG_NAME (type) != NULL &&
	  strncmp (TYPE_TAG_NAME (type), "{unnamed", 8))
	{
	  fputs_filtered (TYPE_TAG_NAME (type), stream);
	  if (show > 0)
	    fputs_filtered (" ", stream);
	}
      wrap_here ("    ");
      if (show < 0)
	{
	  /* If we just printed a tag name, no need to print anything else.  */
	  if (TYPE_TAG_NAME (type) == NULL)
	    fprintf_filtered (stream, "{...}");
	}
      else if (show > 0 || TYPE_TAG_NAME (type) == NULL)
	{
	  cp_type_print_derivation_info (stream, type);

	  fprintf_filtered (stream, "{\n");
	  if ((TYPE_NFIELDS (type) == 0) && (TYPE_NFN_FIELDS (type) == 0))
	    {
	      if (TYPE_STUB (type))
		fprintfi_filtered (level + 4, stream, "<incomplete type>\n");
	      else
		fprintfi_filtered (level + 4, stream, "<no data fields>\n");
	    }

	  /* Start off with no specific section type, so we can print
	     one for the first field we find, and use that section type
	     thereafter until we find another type. */

	  section_type = s_none;

	  /* For a class, if all members are private, there's no need
	     for a "private:" label; similarly, for a struct or union
	     masquerading as a class, if all members are public, there's
	     no need for a "public:" label. */

	  if ((TYPE_DECLARED_TYPE (type) == DECLARED_TYPE_CLASS) ||
	      (TYPE_DECLARED_TYPE (type) == DECLARED_TYPE_TEMPLATE))
	    {
	      QUIT;
	      len = TYPE_NFIELDS (type);
	      for (i = TYPE_N_BASECLASSES (type); i < len; i++)
		if (!TYPE_FIELD_PRIVATE (type, i))
		  {
		    need_access_label = 1;
		    break;
		  }
	      QUIT;
	      if (!need_access_label)
		{
		  len2 = TYPE_NFN_FIELDS (type);
		  for (j = 0; j < len2; j++)
		    {
		      len = TYPE_FN_FIELDLIST_LENGTH (type, j);
		      for (i = 0; i < len; i++)
			if (!TYPE_FN_FIELD_PRIVATE (TYPE_FN_FIELDLIST1 (type, j), i))
			  {
			    need_access_label = 1;
			    break;
			  }
		      if (need_access_label)
			break;
		    }
		}
	    }
	  else if ((TYPE_DECLARED_TYPE (type) == DECLARED_TYPE_STRUCT) ||
		   (TYPE_DECLARED_TYPE (type) == DECLARED_TYPE_UNION))
	    {
	      QUIT;
	      len = TYPE_NFIELDS (type);
	      for (i = TYPE_N_BASECLASSES (type); i < len; i++)
		if (TYPE_FIELD_PRIVATE (type, i) || TYPE_FIELD_PROTECTED (type, i))
		  {
		    need_access_label = 1;
		    break;
		  }
	      QUIT;
	      if (!need_access_label)
		{
		  len2 = TYPE_NFN_FIELDS (type);
		  for (j = 0; j < len2; j++)
		    {
		      QUIT;
		      len = TYPE_FN_FIELDLIST_LENGTH (type, j);
		      for (i = 0; i < len; i++)
			if (TYPE_FN_FIELD_PRIVATE (TYPE_FN_FIELDLIST1 (type, j), i) ||
			    TYPE_FN_FIELD_PROTECTED (TYPE_FN_FIELDLIST1 (type, j), i))
			  {
			    need_access_label = 1;
			    break;
			  }
		      if (need_access_label)
			break;
		    }
		}
	    }

	  /* If there is a base class for this type,
	     do not print the field that it occupies.  */

	  len = TYPE_NFIELDS (type);
	  for (i = TYPE_N_BASECLASSES (type); i < len; i++)
	    {
	      QUIT;
	      /* Don't print out virtual function table.  */
	      /* HP ANSI C++ case */
	      if (TYPE_HAS_VTABLE (type)
		  && (strncmp (TYPE_FIELD_NAME (type, i), "__vfp", 5) == 0))
		continue;
	      /* Other compilers */
	      if (strncmp (TYPE_FIELD_NAME (type, i), "_vptr", 5) == 0
		  && is_cplus_marker ((TYPE_FIELD_NAME (type, i))[5]))
		continue;

	      /* If this is a C++ class we can print the various C++ section
	         labels. */

	      if (HAVE_CPLUS_STRUCT (type) && need_access_label)
		{
		  if (TYPE_FIELD_PROTECTED (type, i))
		    {
		      if (section_type != s_protected)
			{
			  section_type = s_protected;
			  fprintfi_filtered (level + 2, stream,
					     "protected:\n");
			}
		    }
		  else if (TYPE_FIELD_PRIVATE (type, i))
		    {
		      if (section_type != s_private)
			{
			  section_type = s_private;
			  fprintfi_filtered (level + 2, stream, "private:\n");
			}
		    }
		  else
		    {
		      if (section_type != s_public)
			{
			  section_type = s_public;
			  fprintfi_filtered (level + 2, stream, "public:\n");
			}
		    }
		}

	      print_spaces_filtered (level + 4, stream);
	      if (TYPE_FIELD_STATIC (type, i))
		{
		  fprintf_filtered (stream, "static ");
		}
	      c_print_type (TYPE_FIELD_TYPE (type, i),
			    TYPE_FIELD_NAME (type, i),
			    stream, show - 1, level + 4);
	      if (!TYPE_FIELD_STATIC (type, i)
		  && TYPE_FIELD_PACKED (type, i))
		{
		  /* It is a bitfield.  This code does not attempt
		     to look at the bitpos and reconstruct filler,
		     unnamed fields.  This would lead to misleading
		     results if the compiler does not put out fields
		     for such things (I don't know what it does).  */
		  fprintf_filtered (stream, " : %d",
				    TYPE_FIELD_BITSIZE (type, i));
		}
	      fprintf_filtered (stream, ";\n");
	    }

	  /* If there are both fields and methods, put a blank line
	      between them.  Make sure to count only method that we will
	      display; artificial methods will be hidden.  */
	  len = TYPE_NFN_FIELDS (type);
	  real_len = 0;
	  for (i = 0; i < len; i++)
	    {
	      struct fn_field *f = TYPE_FN_FIELDLIST1 (type, i);
	      int len2 = TYPE_FN_FIELDLIST_LENGTH (type, i);
	      int j;
	      for (j = 0; j < len2; j++)
		if (!TYPE_FN_FIELD_ARTIFICIAL (f, j))
		  real_len++;
	    }
	  if (real_len > 0 && section_type != s_none)
	    fprintf_filtered (stream, "\n");

	  /* C++: print out the methods */
	  for (i = 0; i < len; i++)
	    {
	      struct fn_field *f = TYPE_FN_FIELDLIST1 (type, i);
	      int j, len2 = TYPE_FN_FIELDLIST_LENGTH (type, i);
	      char *method_name = TYPE_FN_FIELDLIST_NAME (type, i);
	      char *name = type_name_no_tag (type);
	      int is_constructor = name && strcmp (method_name, name) == 0;
	      for (j = 0; j < len2; j++)
		{
		  char *physname = TYPE_FN_FIELD_PHYSNAME (f, j);
		  int is_full_physname_constructor =
		   is_constructor_name (physname) 
		   || is_destructor_name (physname)
		   || method_name[0] == '~';

		  /* Do not print out artificial methods.  */
		  if (TYPE_FN_FIELD_ARTIFICIAL (f, j))
		    continue;

		  QUIT;
		  if (TYPE_FN_FIELD_PROTECTED (f, j))
		    {
		      if (section_type != s_protected)
			{
			  section_type = s_protected;
			  fprintfi_filtered (level + 2, stream,
					     "protected:\n");
			}
		    }
		  else if (TYPE_FN_FIELD_PRIVATE (f, j))
		    {
		      if (section_type != s_private)
			{
			  section_type = s_private;
			  fprintfi_filtered (level + 2, stream, "private:\n");
			}
		    }
		  else
		    {
		      if (section_type != s_public)
			{
			  section_type = s_public;
			  fprintfi_filtered (level + 2, stream, "public:\n");
			}
		    }

		  print_spaces_filtered (level + 4, stream);
		  if (TYPE_FN_FIELD_VIRTUAL_P (f, j))
		    fprintf_filtered (stream, "virtual ");
		  else if (TYPE_FN_FIELD_STATIC_P (f, j))
		    fprintf_filtered (stream, "static ");
		  if (TYPE_TARGET_TYPE (TYPE_FN_FIELD_TYPE (f, j)) == 0)
		    {
		      /* Keep GDB from crashing here.  */
		      fprintf_filtered (stream, "<undefined type> %s;\n",
					TYPE_FN_FIELD_PHYSNAME (f, j));
		      break;
		    }
		  else if (!is_constructor &&	/* constructors don't have declared types */
			   !is_full_physname_constructor &&	/*    " "  */
			   !is_type_conversion_operator (type, i, j))
		    {
		      type_print (TYPE_TARGET_TYPE (TYPE_FN_FIELD_TYPE (f, j)),
				  "", stream, -1);
		      fputs_filtered (" ", stream);
		    }
		  if (TYPE_FN_FIELD_STUB (f, j))
		    /* Build something we can demangle.  */
		    mangled_name = gdb_mangle_name (type, i, j);
		  else
		    mangled_name = TYPE_FN_FIELD_PHYSNAME (f, j);

		  demangled_name =
		    cplus_demangle (mangled_name,
				    DMGL_ANSI | DMGL_PARAMS);
		  if (demangled_name == NULL)
		    {
		      /* in some cases (for instance with the HP demangling),
		         if a function has more than 10 arguments, 
		         the demangling will fail.
		         Let's try to reconstruct the function signature from 
		         the symbol information */
		      if (!TYPE_FN_FIELD_STUB (f, j))
			{
			  int staticp = TYPE_FN_FIELD_STATIC_P (f, j);
			  struct type *mtype = TYPE_FN_FIELD_TYPE (f, j);
			  cp_type_print_method_args (mtype,
						     "",
						     method_name,
						     staticp,
						     stream);
			}
		      else
			fprintf_filtered (stream, "<badly mangled name '%s'>",
					  mangled_name);
		    }
		  else
		    {
		      char *p;
		      char *demangled_no_class
			= remove_qualifiers (demangled_name);

		      /* get rid of the `static' appended by the demangler */
		      p = strstr (demangled_no_class, " static");
		      if (p != NULL)
			{
			  int length = p - demangled_no_class;
			  demangled_no_static = (char *) xmalloc (length + 1);
			  strncpy (demangled_no_static, demangled_no_class, length);
			  *(demangled_no_static + length) = '\0';
			  fputs_filtered (demangled_no_static, stream);
			  xfree (demangled_no_static);
			}
		      else
			fputs_filtered (demangled_no_class, stream);
		      xfree (demangled_name);
		    }

		  if (TYPE_FN_FIELD_STUB (f, j))
		    xfree (mangled_name);

		  fprintf_filtered (stream, ";\n");
		}
	    }

	  fprintfi_filtered (level, stream, "}");

	  if (TYPE_LOCALTYPE_PTR (type) && show >= 0)
	    fprintfi_filtered (level, stream, " (Local at %s:%d)\n",
			       TYPE_LOCALTYPE_FILE (type),
			       TYPE_LOCALTYPE_LINE (type));
	}
      if (TYPE_CODE (type) == TYPE_CODE_TEMPLATE)
	goto go_back;
      break;

    case TYPE_CODE_ENUM:
      c_type_print_modifier (type, stream, 0, 1);
      /* HP C supports sized enums */
      if (hp_som_som_object_present)
	switch (TYPE_LENGTH (type))
	  {
	  case 1:
	    fputs_filtered ("char ", stream);
	    break;
	  case 2:
	    fputs_filtered ("short ", stream);
	    break;
	  default:
	    break;
	  }
      fprintf_filtered (stream, "enum ");
      /* Print the tag name if it exists.
         The aCC compiler emits a spurious 
         "{unnamed struct}"/"{unnamed union}"/"{unnamed enum}"
         tag for unnamed struct/union/enum's, which we don't
         want to print. */
      if (TYPE_TAG_NAME (type) != NULL &&
	  strncmp (TYPE_TAG_NAME (type), "{unnamed", 8))
	{
	  fputs_filtered (TYPE_TAG_NAME (type), stream);
	  if (show > 0)
	    fputs_filtered (" ", stream);
	}

      wrap_here ("    ");
      if (show < 0)
	{
	  /* If we just printed a tag name, no need to print anything else.  */
	  if (TYPE_TAG_NAME (type) == NULL)
	    fprintf_filtered (stream, "{...}");
	}
      else if (show > 0 || TYPE_TAG_NAME (type) == NULL)
	{
	  fprintf_filtered (stream, "{");
	  len = TYPE_NFIELDS (type);
	  lastval = 0;
	  for (i = 0; i < len; i++)
	    {
	      QUIT;
	      if (i)
		fprintf_filtered (stream, ", ");
	      wrap_here ("    ");
	      fputs_filtered (TYPE_FIELD_NAME (type, i), stream);
	      if (lastval != TYPE_FIELD_BITPOS (type, i))
		{
		  fprintf_filtered (stream, " = %d", TYPE_FIELD_BITPOS (type, i));
		  lastval = TYPE_FIELD_BITPOS (type, i);
		}
	      lastval++;
	    }
	  fprintf_filtered (stream, "}");
	}
      break;

    case TYPE_CODE_VOID:
      fprintf_filtered (stream, "void");
      break;

    case TYPE_CODE_UNDEF:
      fprintf_filtered (stream, "struct <unknown>");
      break;

    case TYPE_CODE_ERROR:
      fprintf_filtered (stream, "<unknown type>");
      break;

    case TYPE_CODE_RANGE:
      /* This should not occur */
      fprintf_filtered (stream, "<range type>");
      break;

    case TYPE_CODE_TEMPLATE:
      /* Called on "ptype t" where "t" is a template.
         Prints the template header (with args), e.g.:
         template <class T1, class T2> class "
         and then merges with the struct/union/class code to
         print the rest of the definition. */
      c_type_print_modifier (type, stream, 0, 1);
      fprintf_filtered (stream, "template <");
      for (i = 0; i < TYPE_NTEMPLATE_ARGS (type); i++)
	{
	  struct template_arg templ_arg;
	  templ_arg = TYPE_TEMPLATE_ARG (type, i);
	  fprintf_filtered (stream, "class %s", templ_arg.name);
	  if (i < TYPE_NTEMPLATE_ARGS (type) - 1)
	    fprintf_filtered (stream, ", ");
	}
      fprintf_filtered (stream, "> class ");
      /* Yuck, factor this out to a subroutine so we can call
         it and return to the point marked with the "goback:" label... - RT */
      goto struct_union;
    go_back:
      if (TYPE_NINSTANTIATIONS (type) > 0)
	{
	  fprintf_filtered (stream, "\ntemplate instantiations:\n");
	  for (i = 0; i < TYPE_NINSTANTIATIONS (type); i++)
	    {
	      fprintf_filtered (stream, "  ");
	      c_type_print_base (TYPE_INSTANTIATION (type, i), stream, 0, level);
	      if (i < TYPE_NINSTANTIATIONS (type) - 1)
		fprintf_filtered (stream, "\n");
	    }
	}
      break;

    case TYPE_CODE_NAMESPACE:
      fputs_filtered ("namespace ", stream);
      fputs_filtered (TYPE_TAG_NAME (type), stream);
      break;

    default:
      /* Handle types not explicitly handled by the other cases,
         such as fundamental types.  For these, just print whatever
         the type name is, as recorded in the type itself.  If there
         is no type name, then complain. */
      if (TYPE_NAME (type) != NULL)
	{
	  c_type_print_modifier (type, stream, 0, 1);
	  fputs_filtered (TYPE_NAME (type), stream);
	}
      else
	{
	  /* At least for dump_symtab, it is important that this not be
	     an error ().  */
	  fprintf_filtered (stream, "<invalid type code %d>",
			    TYPE_CODE (type));
	}
      break;
    }
}
