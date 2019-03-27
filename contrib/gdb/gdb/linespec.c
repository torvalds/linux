/* Parser for linespec for the GNU debugger, GDB.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
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
#include "frame.h"
#include "command.h"
#include "symfile.h"
#include "objfiles.h"
#include "source.h"
#include "demangle.h"
#include "value.h"
#include "completer.h"
#include "cp-abi.h"
#include "parser-defs.h"
#include "block.h"
#include "objc-lang.h"
#include "linespec.h"

/* We share this one with symtab.c, but it is not exported widely. */

extern char *operator_chars (char *, char **);

/* Prototypes for local functions */

static void initialize_defaults (struct symtab **default_symtab,
				 int *default_line);

static void set_flags (char *arg, int *is_quoted, char **paren_pointer);

static struct symtabs_and_lines decode_indirect (char **argptr);

static char *locate_first_half (char **argptr, int *is_quote_enclosed);

static struct symtabs_and_lines decode_objc (char **argptr,
					     int funfirstline,
					     struct symtab *file_symtab,
					     char ***canonical,
					     char *saved_arg);

static struct symtabs_and_lines decode_compound (char **argptr,
						 int funfirstline,
						 char ***canonical,
						 char *saved_arg,
						 char *p);

static struct symbol *lookup_prefix_sym (char **argptr, char *p);

static struct symtabs_and_lines find_method (int funfirstline,
					     char ***canonical,
					     char *saved_arg,
					     char *copy,
					     struct type *t,
					     struct symbol *sym_class);

static int collect_methods (char *copy, struct type *t,
			    struct symbol **sym_arr);

static NORETURN void cplusplus_error (const char *name,
				      const char *fmt, ...)
     ATTR_NORETURN ATTR_FORMAT (printf, 2, 3);

static int total_number_of_methods (struct type *type);

static int find_methods (struct type *, char *, struct symbol **);

static int add_matching_methods (int method_counter, struct type *t,
				 struct symbol **sym_arr);

static int add_constructors (int method_counter, struct type *t,
			     struct symbol **sym_arr);

static void build_canonical_line_spec (struct symtab_and_line *,
				       char *, char ***);

static char *find_toplevel_char (char *s, char c);

static int is_objc_method_format (const char *s);

static struct symtabs_and_lines decode_line_2 (struct symbol *[],
					       int, int, char ***);

static struct symtab *symtab_from_filename (char **argptr,
					    char *p, int is_quote_enclosed,
					    int *not_found_ptr);

static struct
symtabs_and_lines decode_all_digits (char **argptr,
				     struct symtab *default_symtab,
				     int default_line,
				     char ***canonical,
				     struct symtab *file_symtab,
				     char *q);

static struct symtabs_and_lines decode_dollar (char *copy,
					       int funfirstline,
					       struct symtab *default_symtab,
					       char ***canonical,
					       struct symtab *file_symtab);

static struct symtabs_and_lines decode_variable (char *copy,
						 int funfirstline,
						 char ***canonical,
						 struct symtab *file_symtab,
						 int *not_found_ptr);

static struct
symtabs_and_lines symbol_found (int funfirstline,
				char ***canonical,
				char *copy,
				struct symbol *sym,
				struct symtab *file_symtab,
				struct symtab *sym_symtab);

static struct
symtabs_and_lines minsym_found (int funfirstline,
				struct minimal_symbol *msymbol);

/* Helper functions. */

/* Issue a helpful hint on using the command completion feature on
   single quoted demangled C++ symbols as part of the completion
   error.  */

static NORETURN void
cplusplus_error (const char *name, const char *fmt, ...)
{
  struct ui_file *tmp_stream;
  tmp_stream = mem_fileopen ();
  make_cleanup_ui_file_delete (tmp_stream);

  {
    va_list args;
    va_start (args, fmt);
    vfprintf_unfiltered (tmp_stream, fmt, args);
    va_end (args);
  }

  while (*name == '\'')
    name++;
  fprintf_unfiltered (tmp_stream,
		      ("Hint: try '%s<TAB> or '%s<ESC-?>\n"
		       "(Note leading single quote.)"),
		      name, name);
  error_stream (tmp_stream);
}

/* Return the number of methods described for TYPE, including the
   methods from types it derives from. This can't be done in the symbol
   reader because the type of the baseclass might still be stubbed
   when the definition of the derived class is parsed.  */

static int
total_number_of_methods (struct type *type)
{
  int n;
  int count;

  CHECK_TYPEDEF (type);
  if (TYPE_CPLUS_SPECIFIC (type) == NULL)
    return 0;
  count = TYPE_NFN_FIELDS_TOTAL (type);

  for (n = 0; n < TYPE_N_BASECLASSES (type); n++)
    count += total_number_of_methods (TYPE_BASECLASS (type, n));

  return count;
}

/* Recursive helper function for decode_line_1.
   Look for methods named NAME in type T.
   Return number of matches.
   Put matches in SYM_ARR, which should have been allocated with
   a size of total_number_of_methods (T) * sizeof (struct symbol *).
   Note that this function is g++ specific.  */

static int
find_methods (struct type *t, char *name, struct symbol **sym_arr)
{
  int i1 = 0;
  int ibase;
  char *class_name = type_name_no_tag (t);

  /* Ignore this class if it doesn't have a name.  This is ugly, but
     unless we figure out how to get the physname without the name of
     the class, then the loop can't do any good.  */
  if (class_name
      && (lookup_symbol (class_name, (struct block *) NULL,
			 STRUCT_DOMAIN, (int *) NULL,
			 (struct symtab **) NULL)))
    {
      int method_counter;
      int name_len = strlen (name);

      CHECK_TYPEDEF (t);

      /* Loop over each method name.  At this level, all overloads of a name
         are counted as a single name.  There is an inner loop which loops over
         each overload.  */

      for (method_counter = TYPE_NFN_FIELDS (t) - 1;
	   method_counter >= 0;
	   --method_counter)
	{
	  char *method_name = TYPE_FN_FIELDLIST_NAME (t, method_counter);
	  char dem_opname[64];

	  if (strncmp (method_name, "__", 2) == 0 ||
	      strncmp (method_name, "op", 2) == 0 ||
	      strncmp (method_name, "type", 4) == 0)
	    {
	      if (cplus_demangle_opname (method_name, dem_opname, DMGL_ANSI))
		method_name = dem_opname;
	      else if (cplus_demangle_opname (method_name, dem_opname, 0))
		method_name = dem_opname;
	    }

	  if (strcmp_iw (name, method_name) == 0)
	    /* Find all the overloaded methods with that name.  */
	    i1 += add_matching_methods (method_counter, t,
					sym_arr + i1);
	  else if (strncmp (class_name, name, name_len) == 0
		   && (class_name[name_len] == '\0'
		       || class_name[name_len] == '<'))
	    i1 += add_constructors (method_counter, t,
				    sym_arr + i1);
	}
    }

  /* Only search baseclasses if there is no match yet, since names in
     derived classes override those in baseclasses.

     FIXME: The above is not true; it is only true of member functions
     if they have the same number of arguments (??? - section 13.1 of the
     ARM says the function members are not in the same scope but doesn't
     really spell out the rules in a way I understand.  In any case, if
     the number of arguments differ this is a case in which we can overload
     rather than hiding without any problem, and gcc 2.4.5 does overload
     rather than hiding in this case).  */

  if (i1 == 0)
    for (ibase = 0; ibase < TYPE_N_BASECLASSES (t); ibase++)
      i1 += find_methods (TYPE_BASECLASS (t, ibase), name, sym_arr + i1);

  return i1;
}

/* Add the symbols associated to methods of the class whose type is T
   and whose name matches the method indexed by METHOD_COUNTER in the
   array SYM_ARR.  Return the number of methods added.  */

static int
add_matching_methods (int method_counter, struct type *t,
		      struct symbol **sym_arr)
{
  int field_counter;
  int i1 = 0;

  for (field_counter = TYPE_FN_FIELDLIST_LENGTH (t, method_counter) - 1;
       field_counter >= 0;
       --field_counter)
    {
      struct fn_field *f;
      char *phys_name;

      f = TYPE_FN_FIELDLIST1 (t, method_counter);

      if (TYPE_FN_FIELD_STUB (f, field_counter))
	{
	  char *tmp_name;

	  tmp_name = gdb_mangle_name (t,
				      method_counter,
				      field_counter);
	  phys_name = alloca (strlen (tmp_name) + 1);
	  strcpy (phys_name, tmp_name);
	  xfree (tmp_name);
	}
      else
	phys_name = TYPE_FN_FIELD_PHYSNAME (f, field_counter);
		
      /* Destructor is handled by caller, don't add it to
	 the list.  */
      if (is_destructor_name (phys_name) != 0)
	continue;

      sym_arr[i1] = lookup_symbol (phys_name,
				   NULL, VAR_DOMAIN,
				   (int *) NULL,
				   (struct symtab **) NULL);
      if (sym_arr[i1])
	i1++;
      else
	{
	  /* This error message gets printed, but the method
	     still seems to be found
	     fputs_filtered("(Cannot find method ", gdb_stdout);
	     fprintf_symbol_filtered (gdb_stdout, phys_name,
	     language_cplus,
	     DMGL_PARAMS | DMGL_ANSI);
	     fputs_filtered(" - possibly inlined.)\n", gdb_stdout);
	  */
	}
    }

  return i1;
}

/* Add the symbols associated to constructors of the class whose type
   is CLASS_TYPE and which are indexed by by METHOD_COUNTER to the
   array SYM_ARR.  Return the number of methods added.  */

static int
add_constructors (int method_counter, struct type *t,
		  struct symbol **sym_arr)
{
  int field_counter;
  int i1 = 0;

  /* For GCC 3.x and stabs, constructors and destructors
     have names like __base_ctor and __complete_dtor.
     Check the physname for now if we're looking for a
     constructor.  */
  for (field_counter
	 = TYPE_FN_FIELDLIST_LENGTH (t, method_counter) - 1;
       field_counter >= 0;
       --field_counter)
    {
      struct fn_field *f;
      char *phys_name;
		  
      f = TYPE_FN_FIELDLIST1 (t, method_counter);

      /* GCC 3.x will never produce stabs stub methods, so
	 we don't need to handle this case.  */
      if (TYPE_FN_FIELD_STUB (f, field_counter))
	continue;
      phys_name = TYPE_FN_FIELD_PHYSNAME (f, field_counter);
      if (! is_constructor_name (phys_name))
	continue;

      /* If this method is actually defined, include it in the
	 list.  */
      sym_arr[i1] = lookup_symbol (phys_name,
				   NULL, VAR_DOMAIN,
				   (int *) NULL,
				   (struct symtab **) NULL);
      if (sym_arr[i1])
	i1++;
    }

  return i1;
}

/* Helper function for decode_line_1.
   Build a canonical line spec in CANONICAL if it is non-NULL and if
   the SAL has a symtab.
   If SYMNAME is non-NULL the canonical line spec is `filename:symname'.
   If SYMNAME is NULL the line number from SAL is used and the canonical
   line spec is `filename:linenum'.  */

static void
build_canonical_line_spec (struct symtab_and_line *sal, char *symname,
			   char ***canonical)
{
  char **canonical_arr;
  char *canonical_name;
  char *filename;
  struct symtab *s = sal->symtab;

  if (s == (struct symtab *) NULL
      || s->filename == (char *) NULL
      || canonical == (char ***) NULL)
    return;

  canonical_arr = (char **) xmalloc (sizeof (char *));
  *canonical = canonical_arr;

  filename = s->filename;
  if (symname != NULL)
    {
      canonical_name = xmalloc (strlen (filename) + strlen (symname) + 2);
      sprintf (canonical_name, "%s:%s", filename, symname);
    }
  else
    {
      canonical_name = xmalloc (strlen (filename) + 30);
      sprintf (canonical_name, "%s:%d", filename, sal->line);
    }
  canonical_arr[0] = canonical_name;
}



/* Find an instance of the character C in the string S that is outside
   of all parenthesis pairs, single-quoted strings, and double-quoted
   strings.  Also, ignore the char within a template name, like a ','
   within foo<int, int>.  */

static char *
find_toplevel_char (char *s, char c)
{
  int quoted = 0;		/* zero if we're not in quotes;
				   '"' if we're in a double-quoted string;
				   '\'' if we're in a single-quoted string.  */
  int depth = 0;		/* Number of unclosed parens we've seen.  */
  char *scan;

  for (scan = s; *scan; scan++)
    {
      if (quoted)
	{
	  if (*scan == quoted)
	    quoted = 0;
	  else if (*scan == '\\' && *(scan + 1))
	    scan++;
	}
      else if (*scan == c && ! quoted && depth == 0)
	return scan;
      else if (*scan == '"' || *scan == '\'')
	quoted = *scan;
      else if (*scan == '(' || *scan == '<')
	depth++;
      else if ((*scan == ')' || *scan == '>') && depth > 0)
	depth--;
    }

  return 0;
}

/* Determines if the gives string corresponds to an Objective-C method
   representation, such as -[Foo bar:] or +[Foo bar]. Objective-C symbols
   are allowed to have spaces and parentheses in them.  */

static int 
is_objc_method_format (const char *s)
{
  if (s == NULL || *s == '\0')
    return 0;
  /* Handle arguments with the format FILENAME:SYMBOL.  */
  if ((s[0] == ':') && (strchr ("+-", s[1]) != NULL) 
      && (s[2] == '[') && strchr(s, ']'))
    return 1;
  /* Handle arguments that are just SYMBOL.  */
  else if ((strchr ("+-", s[0]) != NULL) && (s[1] == '[') && strchr(s, ']'))
    return 1;
  return 0;
}

/* Given a list of NELTS symbols in SYM_ARR, return a list of lines to
   operate on (ask user if necessary).
   If CANONICAL is non-NULL return a corresponding array of mangled names
   as canonical line specs there.  */

static struct symtabs_and_lines
decode_line_2 (struct symbol *sym_arr[], int nelts, int funfirstline,
	       char ***canonical)
{
  struct symtabs_and_lines values, return_values;
  char *args, *arg1;
  int i;
  char *prompt;
  char *symname;
  struct cleanup *old_chain;
  char **canonical_arr = (char **) NULL;

  values.sals = (struct symtab_and_line *)
    alloca (nelts * sizeof (struct symtab_and_line));
  return_values.sals = (struct symtab_and_line *)
    xmalloc (nelts * sizeof (struct symtab_and_line));
  old_chain = make_cleanup (xfree, return_values.sals);

  if (canonical)
    {
      canonical_arr = (char **) xmalloc (nelts * sizeof (char *));
      make_cleanup (xfree, canonical_arr);
      memset (canonical_arr, 0, nelts * sizeof (char *));
      *canonical = canonical_arr;
    }

  i = 0;
  printf_unfiltered ("[0] cancel\n[1] all\n");
  while (i < nelts)
    {
      init_sal (&return_values.sals[i]);	/* Initialize to zeroes.  */
      init_sal (&values.sals[i]);
      if (sym_arr[i] && SYMBOL_CLASS (sym_arr[i]) == LOC_BLOCK)
	{
	  values.sals[i] = find_function_start_sal (sym_arr[i], funfirstline);
	  if (values.sals[i].symtab)
	    printf_unfiltered ("[%d] %s at %s:%d\n",
			       (i + 2),
			       SYMBOL_PRINT_NAME (sym_arr[i]),
			       values.sals[i].symtab->filename,
			       values.sals[i].line);
	  else
	    printf_unfiltered ("[%d] %s at ?FILE:%d [No symtab? Probably broken debug info...]\n",
			       (i + 2),
			       SYMBOL_PRINT_NAME (sym_arr[i]),
			       values.sals[i].line);

	}
      else
	printf_unfiltered ("?HERE\n");
      i++;
    }

  prompt = getenv ("PS2");
  if (prompt == NULL)
    {
      prompt = "> ";
    }
  args = command_line_input (prompt, 0, "overload-choice");

  if (args == 0 || *args == 0)
    error_no_arg ("one or more choice numbers");

  i = 0;
  while (*args)
    {
      int num;

      arg1 = args;
      while (*arg1 >= '0' && *arg1 <= '9')
	arg1++;
      if (*arg1 && *arg1 != ' ' && *arg1 != '\t')
	error ("Arguments must be choice numbers.");

      num = atoi (args);

      if (num == 0)
	error ("canceled");
      else if (num == 1)
	{
	  if (canonical_arr)
	    {
	      for (i = 0; i < nelts; i++)
		{
		  if (canonical_arr[i] == NULL)
		    {
		      symname = DEPRECATED_SYMBOL_NAME (sym_arr[i]);
		      canonical_arr[i] = savestring (symname, strlen (symname));
		    }
		}
	    }
	  memcpy (return_values.sals, values.sals,
		  (nelts * sizeof (struct symtab_and_line)));
	  return_values.nelts = nelts;
	  discard_cleanups (old_chain);
	  return return_values;
	}

      if (num >= nelts + 2)
	{
	  printf_unfiltered ("No choice number %d.\n", num);
	}
      else
	{
	  num -= 2;
	  if (values.sals[num].pc)
	    {
	      if (canonical_arr)
		{
		  symname = DEPRECATED_SYMBOL_NAME (sym_arr[num]);
		  make_cleanup (xfree, symname);
		  canonical_arr[i] = savestring (symname, strlen (symname));
		}
	      return_values.sals[i++] = values.sals[num];
	      values.sals[num].pc = 0;
	    }
	  else
	    {
	      printf_unfiltered ("duplicate request for %d ignored.\n", num);
	    }
	}

      args = arg1;
      while (*args == ' ' || *args == '\t')
	args++;
    }
  return_values.nelts = i;
  discard_cleanups (old_chain);
  return return_values;
}

/* The parser of linespec itself. */

/* Parse a string that specifies a line number.
   Pass the address of a char * variable; that variable will be
   advanced over the characters actually parsed.

   The string can be:

   LINENUM -- that line number in current file.  PC returned is 0.
   FILE:LINENUM -- that line in that file.  PC returned is 0.
   FUNCTION -- line number of openbrace of that function.
   PC returned is the start of the function.
   VARIABLE -- line number of definition of that variable.
   PC returned is 0.
   FILE:FUNCTION -- likewise, but prefer functions in that file.
   *EXPR -- line in which address EXPR appears.

   This may all be followed by an "if EXPR", which we ignore.

   FUNCTION may be an undebuggable function found in minimal symbol table.

   If the argument FUNFIRSTLINE is nonzero, we want the first line
   of real code inside a function when a function is specified, and it is
   not OK to specify a variable or type to get its line number.

   DEFAULT_SYMTAB specifies the file to use if none is specified.
   It defaults to current_source_symtab.
   DEFAULT_LINE specifies the line number to use for relative
   line numbers (that start with signs).  Defaults to current_source_line.
   If CANONICAL is non-NULL, store an array of strings containing the canonical
   line specs there if necessary. Currently overloaded member functions and
   line numbers or static functions without a filename yield a canonical
   line spec. The array and the line spec strings are allocated on the heap,
   it is the callers responsibility to free them.

   Note that it is possible to return zero for the symtab
   if no file is validly specified.  Callers must check that.
   Also, the line number returned may be invalid.  
 
   If NOT_FOUND_PTR is not null, store a boolean true/false value at the location, based
   on whether or not failure occurs due to an unknown function or file.  In the case
   where failure does occur due to an unknown function or file, do not issue an error
   message.  */

/* We allow single quotes in various places.  This is a hideous
   kludge, which exists because the completer can't yet deal with the
   lack of single quotes.  FIXME: write a linespec_completer which we
   can use as appropriate instead of make_symbol_completion_list.  */

struct symtabs_and_lines
decode_line_1 (char **argptr, int funfirstline, struct symtab *default_symtab,
	       int default_line, char ***canonical, int *not_found_ptr)
{
  char *p;
  char *q;
  /* If a file name is specified, this is its symtab.  */
  struct symtab *file_symtab = NULL;

  char *copy;
  /* This is NULL if there are no parens in *ARGPTR, or a pointer to
     the closing parenthesis if there are parens.  */
  char *paren_pointer;
  /* This says whether or not something in *ARGPTR is quoted with
     completer_quotes (i.e. with single quotes).  */
  int is_quoted;
  /* Is part of *ARGPTR is enclosed in double quotes?  */
  int is_quote_enclosed;
  int is_objc_method = 0;
  char *saved_arg = *argptr;

  if (not_found_ptr)
    *not_found_ptr = 0;

  /* Defaults have defaults.  */

  initialize_defaults (&default_symtab, &default_line);
  
  /* See if arg is *PC.  */

  if (**argptr == '*')
    return decode_indirect (argptr);

  /* Set various flags.  'paren_pointer' is important for overload
     checking, where we allow things like:
        (gdb) break c::f(int)
  */

  set_flags (*argptr, &is_quoted, &paren_pointer);

  /* Check to see if it's a multipart linespec (with colons or
     periods).  */

  /* Locate the end of the first half of the linespec.
     After the call, for instance, if the argptr string is "foo.c:123"
     p will point at "123".  If there is only one part, like "foo", p
     will point to "". If this is a C++ name, like "A::B::foo", p will
     point to "::B::foo". Argptr is not changed by this call.  */

  p = locate_first_half (argptr, &is_quote_enclosed);

  /* Check if this is an Objective-C method (anything that starts with
     a '+' or '-' and a '[').  */
  if (is_objc_method_format (p))
    {
      is_objc_method = 1;
      paren_pointer  = NULL; /* Just a category name.  Ignore it.  */
    }

  /* Check if the symbol could be an Objective-C selector.  */

  {
    struct symtabs_and_lines values;
    values = decode_objc (argptr, funfirstline, NULL,
			  canonical, saved_arg);
    if (values.sals != NULL)
      return values;
  }

  /* Does it look like there actually were two parts?  */

  if ((p[0] == ':' || p[0] == '.') && paren_pointer == NULL)
    {
      if (is_quoted)
	*argptr = *argptr + 1;
      
      /* Is it a C++ or Java compound data structure?
	 The check on p[1] == ':' is capturing the case of "::",
	 since p[0]==':' was checked above.  
	 Note that the call to decode_compound does everything
	 for us, including the lookup on the symbol table, so we
	 can return now. */
	
      if (p[0] == '.' || p[1] == ':')
	return decode_compound (argptr, funfirstline, canonical,
				saved_arg, p);

      /* No, the first part is a filename; set s to be that file's
	 symtab.  Also, move argptr past the filename.  */

      file_symtab = symtab_from_filename (argptr, p, is_quote_enclosed, 
		      			  not_found_ptr);
    }
#if 0
  /* No one really seems to know why this was added. It certainly
     breaks the command line, though, whenever the passed
     name is of the form ClassName::Method. This bit of code
     singles out the class name, and if funfirstline is set (for
     example, you are setting a breakpoint at this function),
     you get an error. This did not occur with earlier
     verions, so I am ifdef'ing this out. 3/29/99 */
  else
    {
      /* Check if what we have till now is a symbol name */

      /* We may be looking at a template instantiation such
         as "foo<int>".  Check here whether we know about it,
         instead of falling through to the code below which
         handles ordinary function names, because that code
         doesn't like seeing '<' and '>' in a name -- the
         skip_quoted call doesn't go past them.  So see if we
         can figure it out right now. */

      copy = (char *) alloca (p - *argptr + 1);
      memcpy (copy, *argptr, p - *argptr);
      copy[p - *argptr] = '\000';
      sym = lookup_symbol (copy, 0, VAR_DOMAIN, 0, &sym_symtab);
      if (sym)
	{
	  *argptr = (*p == '\'') ? p + 1 : p;
	  return symbol_found (funfirstline, canonical, copy, sym,
			       NULL, sym_symtab);
	}
      /* Otherwise fall out from here and go to file/line spec
         processing, etc. */
    }
#endif

  /* S is specified file's symtab, or 0 if no file specified.
     arg no longer contains the file name.  */

  /* Check whether arg is all digits (and sign).  */

  q = *argptr;
  if (*q == '-' || *q == '+')
    q++;
  while (*q >= '0' && *q <= '9')
    q++;

  if (q != *argptr && (*q == 0 || *q == ' ' || *q == '\t' || *q == ','))
    /* We found a token consisting of all digits -- at least one digit.  */
    return decode_all_digits (argptr, default_symtab, default_line,
			      canonical, file_symtab, q);

  /* Arg token is not digits => try it as a variable name
     Find the next token (everything up to end or next whitespace).  */

  if (**argptr == '$')		/* May be a convenience variable.  */
    /* One or two $ chars possible.  */
    p = skip_quoted (*argptr + (((*argptr)[1] == '$') ? 2 : 1));
  else if (is_quoted)
    {
      p = skip_quoted (*argptr);
      if (p[-1] != '\'')
	error ("Unmatched single quote.");
    }
  else if (is_objc_method)
    {
      /* allow word separators in method names for Obj-C */
      p = skip_quoted_chars (*argptr, NULL, "");
    }
  else if (paren_pointer != NULL)
    {
      p = paren_pointer + 1;
    }
  else
    {
      p = skip_quoted (*argptr);
    }

  copy = (char *) alloca (p - *argptr + 1);
  memcpy (copy, *argptr, p - *argptr);
  copy[p - *argptr] = '\0';
  if (p != *argptr
      && copy[0]
      && copy[0] == copy[p - *argptr - 1]
      && strchr (get_gdb_completer_quote_characters (), copy[0]) != NULL)
    {
      copy[p - *argptr - 1] = '\0';
      copy++;
    }
  while (*p == ' ' || *p == '\t')
    p++;
  *argptr = p;

  /* If it starts with $: may be a legitimate variable or routine name
     (e.g. HP-UX millicode routines such as $$dyncall), or it may
     be history value, or it may be a convenience variable.  */

  if (*copy == '$')
    return decode_dollar (copy, funfirstline, default_symtab,
			  canonical, file_symtab);

  /* Look up that token as a variable.
     If file specified, use that file's per-file block to start with.  */

  return decode_variable (copy, funfirstline, canonical,
			  file_symtab, not_found_ptr);
}



/* Now, more helper functions for decode_line_1.  Some conventions
   that these functions follow:

   Decode_line_1 typically passes along some of its arguments or local
   variables to the subfunctions.  It passes the variables by
   reference if they are modified by the subfunction, and by value
   otherwise.

   Some of the functions have side effects that don't arise from
   variables that are passed by reference.  In particular, if a
   function is passed ARGPTR as an argument, it modifies what ARGPTR
   points to; typically, it advances *ARGPTR past whatever substring
   it has just looked at.  (If it doesn't modify *ARGPTR, then the
   function gets passed *ARGPTR instead, which is then called ARG: see
   set_flags, for example.)  Also, functions that return a struct
   symtabs_and_lines may modify CANONICAL, as in the description of
   decode_line_1.

   If a function returns a struct symtabs_and_lines, then that struct
   will immediately make its way up the call chain to be returned by
   decode_line_1.  In particular, all of the functions decode_XXX
   calculate the appropriate struct symtabs_and_lines, under the
   assumption that their argument is of the form XXX.  */

/* First, some functions to initialize stuff at the beggining of the
   function.  */

static void
initialize_defaults (struct symtab **default_symtab, int *default_line)
{
  if (*default_symtab == 0)
    {
      /* Use whatever we have for the default source line.  We don't use
         get_current_or_default_symtab_and_line as it can recurse and call
	 us back! */
      struct symtab_and_line cursal = 
	get_current_source_symtab_and_line ();
      
      *default_symtab = cursal.symtab;
      *default_line = cursal.line;
    }
}

static void
set_flags (char *arg, int *is_quoted, char **paren_pointer)
{
  char *ii;
  int has_if = 0;

  /* 'has_if' is for the syntax:
        (gdb) break foo if (a==b)
  */
  if ((ii = strstr (arg, " if ")) != NULL ||
      (ii = strstr (arg, "\tif ")) != NULL ||
      (ii = strstr (arg, " if\t")) != NULL ||
      (ii = strstr (arg, "\tif\t")) != NULL ||
      (ii = strstr (arg, " if(")) != NULL ||
      (ii = strstr (arg, "\tif( ")) != NULL)
    has_if = 1;
  /* Temporarily zap out "if (condition)" to not confuse the
     parenthesis-checking code below.  This is undone below. Do not
     change ii!!  */
  if (has_if)
    {
      *ii = '\0';
    }

  *is_quoted = (*arg
		&& strchr (get_gdb_completer_quote_characters (),
			   *arg) != NULL);

  *paren_pointer = strchr (arg, '(');
  if (*paren_pointer != NULL)
    *paren_pointer = strrchr (*paren_pointer, ')');

  /* Now that we're safely past the paren_pointer check, put back " if
     (condition)" so outer layers can see it.  */
  if (has_if)
    *ii = ' ';
}



/* Decode arg of the form *PC.  */

static struct symtabs_and_lines
decode_indirect (char **argptr)
{
  struct symtabs_and_lines values;
  CORE_ADDR pc;
  
  (*argptr)++;
  pc = parse_and_eval_address_1 (argptr);

  values.sals = (struct symtab_and_line *)
    xmalloc (sizeof (struct symtab_and_line));

  values.nelts = 1;
  values.sals[0] = find_pc_line (pc, 0);
  values.sals[0].pc = pc;
  values.sals[0].section = find_pc_overlay (pc);

  return values;
}



/* Locate the first half of the linespec, ending in a colon, period,
   or whitespace.  (More or less.)  Also, check to see if *ARGPTR is
   enclosed in double quotes; if so, set is_quote_enclosed, advance
   ARGPTR past that and zero out the trailing double quote.
   If ARGPTR is just a simple name like "main", p will point to ""
   at the end.  */

static char *
locate_first_half (char **argptr, int *is_quote_enclosed)
{
  char *ii;
  char *p, *p1;
  int has_comma;

  /* Maybe we were called with a line range FILENAME:LINENUM,FILENAME:LINENUM
     and we must isolate the first half.  Outer layers will call again later
     for the second half.

     Don't count commas that appear in argument lists of overloaded
     functions, or in quoted strings.  It's stupid to go to this much
     trouble when the rest of the function is such an obvious roach hotel.  */
  ii = find_toplevel_char (*argptr, ',');
  has_comma = (ii != 0);

  /* Temporarily zap out second half to not confuse the code below.
     This is undone below. Do not change ii!!  */
  if (has_comma)
    {
      *ii = '\0';
    }

  /* Maybe arg is FILE : LINENUM or FILE : FUNCTION.  May also be
     CLASS::MEMBER, or NAMESPACE::NAME.  Look for ':', but ignore
     inside of <>.  */

  p = *argptr;
  if (p[0] == '"')
    {
      *is_quote_enclosed = 1;
      (*argptr)++;
      p++;
    }
  else
    *is_quote_enclosed = 0;
  for (; *p; p++)
    {
      if (p[0] == '<')
	{
	  char *temp_end = find_template_name_end (p);
	  if (!temp_end)
	    error ("malformed template specification in command");
	  p = temp_end;
	}
      /* Check for a colon and a plus or minus and a [ (which
         indicates an Objective-C method) */
      if (is_objc_method_format (p))
	{
	  break;
	}
      /* Check for the end of the first half of the linespec.  End of
         line, a tab, a double colon or the last single colon, or a
         space.  But if enclosed in double quotes we do not break on
         enclosed spaces.  */
      if (!*p
	  || p[0] == '\t'
	  || ((p[0] == ':')
	      && ((p[1] == ':') || (strchr (p + 1, ':') == NULL)))
	  || ((p[0] == ' ') && !*is_quote_enclosed))
	break;
      if (p[0] == '.' && strchr (p, ':') == NULL)
	{
	  /* Java qualified method.  Find the *last* '.', since the
	     others are package qualifiers.  */
	  for (p1 = p; *p1; p1++)
	    {
	      if (*p1 == '.')
		p = p1;
	    }
	  break;
	}
    }
  while (p[0] == ' ' || p[0] == '\t')
    p++;

  /* If the closing double quote was left at the end, remove it.  */
  if (*is_quote_enclosed)
    {
      char *closing_quote = strchr (p - 1, '"');
      if (closing_quote && closing_quote[1] == '\0')
	*closing_quote = '\0';
    }

  /* Now that we've safely parsed the first half, put back ',' so
     outer layers can see it.  */
  if (has_comma)
    *ii = ',';

  return p;
}



/* Here's where we recognise an Objective-C Selector.  An Objective C
   selector may be implemented by more than one class, therefore it
   may represent more than one method/function.  This gives us a
   situation somewhat analogous to C++ overloading.  If there's more
   than one method that could represent the selector, then use some of
   the existing C++ code to let the user choose one.  */

struct symtabs_and_lines
decode_objc (char **argptr, int funfirstline, struct symtab *file_symtab,
	     char ***canonical, char *saved_arg)
{
  struct symtabs_and_lines values;
  struct symbol **sym_arr = NULL;
  struct symbol *sym = NULL;
  char *copy = NULL;
  struct block *block = NULL;
  int i1 = 0;
  int i2 = 0;

  values.sals = NULL;
  values.nelts = 0;

  if (file_symtab != NULL)
    block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (file_symtab), STATIC_BLOCK);
  else
    block = get_selected_block (0);
    
  copy = find_imps (file_symtab, block, *argptr, NULL, &i1, &i2); 
    
  if (i1 > 0)
    {
      sym_arr = (struct symbol **) alloca ((i1 + 1) * sizeof (struct symbol *));
      sym_arr[i1] = 0;

      copy = find_imps (file_symtab, block, *argptr, sym_arr, &i1, &i2); 
      *argptr = copy;
    }

  /* i1 now represents the TOTAL number of matches found.
     i2 represents how many HIGH-LEVEL (struct symbol) matches,
     which will come first in the sym_arr array.  Any low-level
     (minimal_symbol) matches will follow those.  */
      
  if (i1 == 1)
    {
      if (i2 > 0)
	{
	  /* Already a struct symbol.  */
	  sym = sym_arr[0];
	}
      else
	{
	  sym = find_pc_function (SYMBOL_VALUE_ADDRESS (sym_arr[0]));
	  if ((sym != NULL) && strcmp (SYMBOL_LINKAGE_NAME (sym_arr[0]), SYMBOL_LINKAGE_NAME (sym)) != 0)
	    {
	      warning ("debugging symbol \"%s\" does not match selector; ignoring", SYMBOL_LINKAGE_NAME (sym));
	      sym = NULL;
	    }
	}
	      
      values.sals = (struct symtab_and_line *) xmalloc (sizeof (struct symtab_and_line));
      values.nelts = 1;
	      
      if (sym && SYMBOL_CLASS (sym) == LOC_BLOCK)
	{
	  /* Canonicalize this, so it remains resolved for dylib loads.  */
	  values.sals[0] = find_function_start_sal (sym, funfirstline);
	  build_canonical_line_spec (values.sals, SYMBOL_NATURAL_NAME (sym), canonical);
	}
      else
	{
	  /* The only match was a non-debuggable symbol.  */
	  values.sals[0].symtab = 0;
	  values.sals[0].line = 0;
	  values.sals[0].end = 0;
	  values.sals[0].pc = SYMBOL_VALUE_ADDRESS (sym_arr[0]);
	}
      return values;
    }

  if (i1 > 1)
    {
      /* More than one match. The user must choose one or more.  */
      return decode_line_2 (sym_arr, i2, funfirstline, canonical);
    }

  return values;
}

/* This handles C++ and Java compound data structures.  P should point
   at the first component separator, i.e. double-colon or period.  As
   an example, on entrance to this function we could have ARGPTR
   pointing to "AAA::inA::fun" and P pointing to "::inA::fun".  */

static struct symtabs_and_lines
decode_compound (char **argptr, int funfirstline, char ***canonical,
		 char *saved_arg, char *p)
{
  struct symtabs_and_lines values;
  char *p2;
  char *saved_arg2 = *argptr;
  char *temp_end;
  struct symbol *sym;
  /* The symtab that SYM was found in.  */
  struct symtab *sym_symtab;
  char *copy;
  struct symbol *sym_class;
  struct symbol **sym_arr;
  struct type *t;

  /* First check for "global" namespace specification, of the form
     "::foo".  If found, skip over the colons and jump to normal
     symbol processing.  I.e. the whole line specification starts with
     "::" (note the condition that *argptr == p). */
  if (p[0] == ':' 
      && ((*argptr == p) || (p[-1] == ' ') || (p[-1] == '\t')))
    saved_arg2 += 2;

  /* Given our example "AAA::inA::fun", we have two cases to consider:

     1) AAA::inA is the name of a class.  In that case, presumably it
        has a method called "fun"; we then look up that method using
        find_method.

     2) AAA::inA isn't the name of a class.  In that case, either the
        user made a typo or AAA::inA is the name of a namespace.
        Either way, we just look up AAA::inA::fun with lookup_symbol.

     Thus, our first task is to find everything before the last set of
     double-colons and figure out if it's the name of a class.  So we
     first loop through all of the double-colons.  */

  p2 = p;		/* Save for restart.  */

  /* This is very messy. Following the example above we have now the
     following pointers:
     p -> "::inA::fun"
     argptr -> "AAA::inA::fun
     saved_arg -> "AAA::inA::fun
     saved_arg2 -> "AAA::inA::fun
     p2 -> "::inA::fun". */

  /* In the loop below, with these strings, we'll make 2 passes, each
     is marked in comments.*/

  while (1)
    {
      /* Move pointer up to next possible class/namespace token.  */

      p = p2 + 1;	/* Restart with old value +1.  */

      /* PASS1: at this point p2->"::inA::fun", so p->":inA::fun",
	 i.e. if there is a double-colon, p will now point to the
	 second colon. */
      /* PASS2: p2->"::fun", p->":fun" */

      /* Move pointer ahead to next double-colon.  */
      while (*p && (p[0] != ' ') && (p[0] != '\t') && (p[0] != '\''))
	{
	  if (p[0] == '<')
	    {
	      temp_end = find_template_name_end (p);
	      if (!temp_end)
		error ("malformed template specification in command");
	      p = temp_end;
	    }
	  /* Note that, since, at the start of this loop, p would be
	     pointing to the second colon in a double-colon, we only
	     satisfy the condition below if there is another
	     double-colon to the right (after). I.e. there is another
	     component that can be a class or a namespace. I.e, if at
	     the beginning of this loop (PASS1), we had
	     p->":inA::fun", we'll trigger this when p has been
	     advanced to point to "::fun".  */
	  /* PASS2: we will not trigger this. */
	  else if ((p[0] == ':') && (p[1] == ':'))
	    break;	/* Found double-colon.  */
	  else
	    /* PASS2: We'll keep getting here, until p->"", at which point
	       we exit this loop.  */
	    p++;
	}

      if (*p != ':')
	break;		/* Out of the while (1).  This would happen
			   for instance if we have looked up
			   unsuccessfully all the components of the
			   string, and p->""(PASS2)  */

      /* We get here if p points to ' ', '\t', '\'', "::" or ""(i.e
	 string ended). */
      /* Save restart for next time around.  */
      p2 = p;
      /* Restore argptr as it was on entry to this function.  */
      *argptr = saved_arg2;
      /* PASS1: at this point p->"::fun" argptr->"AAA::inA::fun",
	 p2->"::fun".  */

      /* All ready for next pass through the loop.  */
    }			/* while (1) */


  /* Start of lookup in the symbol tables. */

  /* Lookup in the symbol table the substring between argptr and
     p. Note, this call changes the value of argptr.  */
  /* Before the call, argptr->"AAA::inA::fun",
     p->"", p2->"::fun".  After the call: argptr->"fun", p, p2
     unchanged.  */
  sym_class = lookup_prefix_sym (argptr, p2);

  /* If sym_class has been found, and if "AAA::inA" is a class, then
     we're in case 1 above.  So we look up "fun" as a method of that
     class.  */
  if (sym_class &&
      (t = check_typedef (SYMBOL_TYPE (sym_class)),
       (TYPE_CODE (t) == TYPE_CODE_STRUCT
	|| TYPE_CODE (t) == TYPE_CODE_UNION)))
    {
      /* Arg token is not digits => try it as a function name.
	 Find the next token (everything up to end or next
	 blank).  */
      if (**argptr
	  && strchr (get_gdb_completer_quote_characters (),
		     **argptr) != NULL)
	{
	  p = skip_quoted (*argptr);
	  *argptr = *argptr + 1;
	}
      else
	{
	  /* At this point argptr->"fun".  */
	  p = *argptr;
	  while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ':')
	    p++;
	  /* At this point p->"".  String ended.  */
	}

      /* Allocate our own copy of the substring between argptr and
	 p. */
      copy = (char *) alloca (p - *argptr + 1);
      memcpy (copy, *argptr, p - *argptr);
      copy[p - *argptr] = '\0';
      if (p != *argptr
	  && copy[p - *argptr - 1]
	  && strchr (get_gdb_completer_quote_characters (),
		     copy[p - *argptr - 1]) != NULL)
	copy[p - *argptr - 1] = '\0';

      /* At this point copy->"fun", p->"" */

      /* No line number may be specified.  */
      while (*p == ' ' || *p == '\t')
	p++;
      *argptr = p;
      /* At this point arptr->"".  */

      /* Look for copy as a method of sym_class. */
      /* At this point copy->"fun", sym_class is "AAA:inA",
	 saved_arg->"AAA::inA::fun".  This concludes the scanning of
	 the string for possible components matches.  If we find it
	 here, we return. If not, and we are at the and of the string,
	 we'll lookup the whole string in the symbol tables.  */

      return find_method (funfirstline, canonical, saved_arg,
			  copy, t, sym_class);

    } /* End if symbol found */


  /* We couldn't find a class, so we're in case 2 above.  We check the
     entire name as a symbol instead.  */

  copy = (char *) alloca (p - saved_arg2 + 1);
  memcpy (copy, saved_arg2, p - saved_arg2);
  /* Note: if is_quoted should be true, we snuff out quote here
     anyway.  */
  copy[p - saved_arg2] = '\000';
  /* Set argptr to skip over the name.  */
  *argptr = (*p == '\'') ? p + 1 : p;

  /* Look up entire name */
  sym = lookup_symbol (copy, 0, VAR_DOMAIN, 0, &sym_symtab);
  if (sym)
    return symbol_found (funfirstline, canonical, copy, sym,
			 NULL, sym_symtab);

  /* Couldn't find any interpretation as classes/namespaces, so give
     up.  The quotes are important if copy is empty.  */
  cplusplus_error (saved_arg,
		   "Can't find member of namespace, class, struct, or union named \"%s\"\n",
		   copy);
}

/* Next come some helper functions for decode_compound.  */

/* Return the symbol corresponding to the substring of *ARGPTR ending
   at P, allowing whitespace.  Also, advance *ARGPTR past the symbol
   name in question, the compound object separator ("::" or "."), and
   whitespace.  Note that *ARGPTR is changed whether or not the
   lookup_symbol call finds anything (i.e we return NULL).  As an
   example, say ARGPTR is "AAA::inA::fun" and P is "::inA::fun".  */

static struct symbol *
lookup_prefix_sym (char **argptr, char *p)
{
  char *p1;
  char *copy;

  /* Extract the class name.  */
  p1 = p;
  while (p != *argptr && p[-1] == ' ')
    --p;
  copy = (char *) alloca (p - *argptr + 1);
  memcpy (copy, *argptr, p - *argptr);
  copy[p - *argptr] = 0;

  /* Discard the class name from the argptr.  */
  p = p1 + (p1[0] == ':' ? 2 : 1);
  while (*p == ' ' || *p == '\t')
    p++;
  *argptr = p;

  /* At this point p1->"::inA::fun", p->"inA::fun" copy->"AAA",
     argptr->"inA::fun" */

  return lookup_symbol (copy, 0, STRUCT_DOMAIN, 0,
			(struct symtab **) NULL);
}

/* This finds the method COPY in the class whose type is T and whose
   symbol is SYM_CLASS.  */

static struct symtabs_and_lines
find_method (int funfirstline, char ***canonical, char *saved_arg,
	     char *copy, struct type *t, struct symbol *sym_class)
{
  struct symtabs_and_lines values;
  struct symbol *sym = 0;
  int i1;	/*  Counter for the symbol array.  */
  struct symbol **sym_arr =  alloca (total_number_of_methods (t)
				     * sizeof (struct symbol *));

  /* Find all methods with a matching name, and put them in
     sym_arr.  */

  i1 = collect_methods (copy, t, sym_arr);

  if (i1 == 1)
    {
      /* There is exactly one field with that name.  */
      sym = sym_arr[0];

      if (sym && SYMBOL_CLASS (sym) == LOC_BLOCK)
	{
	  values.sals = (struct symtab_and_line *)
	    xmalloc (sizeof (struct symtab_and_line));
	  values.nelts = 1;
	  values.sals[0] = find_function_start_sal (sym,
						    funfirstline);
	}
      else
	{
	  values.nelts = 0;
	}
      return values;
    }
  if (i1 > 0)
    {
      /* There is more than one field with that name
	 (overloaded).  Ask the user which one to use.  */
      return decode_line_2 (sym_arr, i1, funfirstline, canonical);
    }
  else
    {
      char *tmp;

      if (is_operator_name (copy))
	{
	  tmp = (char *) alloca (strlen (copy + 3) + 9);
	  strcpy (tmp, "operator ");
	  strcat (tmp, copy + 3);
	}
      else
	tmp = copy;
      if (tmp[0] == '~')
	cplusplus_error (saved_arg,
			 "the class `%s' does not have destructor defined\n",
			 SYMBOL_PRINT_NAME (sym_class));
      else
	cplusplus_error (saved_arg,
			 "the class %s does not have any method named %s\n",
			 SYMBOL_PRINT_NAME (sym_class), tmp);
    }
}

/* Find all methods named COPY in the class whose type is T, and put
   them in SYM_ARR.  Return the number of methods found.  */

static int
collect_methods (char *copy, struct type *t,
		 struct symbol **sym_arr)
{
  int i1 = 0;	/*  Counter for the symbol array.  */

  if (destructor_name_p (copy, t))
    {
      /* Destructors are a special case.  */
      int m_index, f_index;

      if (get_destructor_fn_field (t, &m_index, &f_index))
	{
	  struct fn_field *f = TYPE_FN_FIELDLIST1 (t, m_index);

	  sym_arr[i1] =
	    lookup_symbol (TYPE_FN_FIELD_PHYSNAME (f, f_index),
			   NULL, VAR_DOMAIN, (int *) NULL,
			   (struct symtab **) NULL);
	  if (sym_arr[i1])
	    i1++;
	}
    }
  else
    i1 = find_methods (t, copy, sym_arr);

  return i1;
}



/* Return the symtab associated to the filename given by the substring
   of *ARGPTR ending at P, and advance ARGPTR past that filename.  If
   NOT_FOUND_PTR is not null and the source file is not found, store
   boolean true at the location pointed to and do not issue an
   error message.  */

static struct symtab *
symtab_from_filename (char **argptr, char *p, int is_quote_enclosed, 
		      int *not_found_ptr)
{
  char *p1;
  char *copy;
  struct symtab *file_symtab;
  
  p1 = p;
  while (p != *argptr && p[-1] == ' ')
    --p;
  if ((*p == '"') && is_quote_enclosed)
    --p;
  copy = (char *) alloca (p - *argptr + 1);
  memcpy (copy, *argptr, p - *argptr);
  /* It may have the ending quote right after the file name.  */
  if (is_quote_enclosed && copy[p - *argptr - 1] == '"')
    copy[p - *argptr - 1] = 0;
  else
    copy[p - *argptr] = 0;

  /* Find that file's data.  */
  file_symtab = lookup_symtab (copy);
  if (file_symtab == 0)
    {
      if (!have_full_symbols () && !have_partial_symbols ())
	error ("No symbol table is loaded.  Use the \"file\" command.");
      if (not_found_ptr)
	{
	  *not_found_ptr = 1;
	  /* The caller has indicated that it wishes quiet notification of any
	     error where the function or file is not found.  A call to 
	     error_silent causes an error to occur, but it does not issue 
	     the supplied message.  The message can be manually output by
	     the caller, if desired.  This is used, for example, when 
	     attempting to set breakpoints for functions in shared libraries 
	     that have not yet been loaded.  */
	  error_silent ("No source file named %s.", copy);
	}
      error ("No source file named %s.", copy);
    }

  /* Discard the file name from the arg.  */
  p = p1 + 1;
  while (*p == ' ' || *p == '\t')
    p++;
  *argptr = p;

  return file_symtab;
}



/* This decodes a line where the argument is all digits (possibly
   preceded by a sign).  Q should point to the end of those digits;
   the other arguments are as usual.  */

static struct symtabs_and_lines
decode_all_digits (char **argptr, struct symtab *default_symtab,
		   int default_line, char ***canonical,
		   struct symtab *file_symtab, char *q)

{
  struct symtabs_and_lines values;
  struct symtab_and_line val;

  enum sign
    {
      none, plus, minus
    }
  sign = none;

  /* We might need a canonical line spec if no file was specified.  */
  int need_canonical = (file_symtab == 0) ? 1 : 0;

  init_sal (&val);

  /* This is where we need to make sure that we have good defaults.
     We must guarantee that this section of code is never executed
     when we are called with just a function name, since
     set_default_source_symtab_and_line uses
     select_source_symtab that calls us with such an argument.  */

  if (file_symtab == 0 && default_symtab == 0)
    {
      /* Make sure we have at least a default source file.  */
      set_default_source_symtab_and_line ();
      initialize_defaults (&default_symtab, &default_line);
    }

  if (**argptr == '+')
    sign = plus, (*argptr)++;
  else if (**argptr == '-')
    sign = minus, (*argptr)++;
  val.line = atoi (*argptr);
  switch (sign)
    {
    case plus:
      if (q == *argptr)
	val.line = 5;
      if (file_symtab == 0)
	val.line = default_line + val.line;
      break;
    case minus:
      if (q == *argptr)
	val.line = 15;
      if (file_symtab == 0)
	val.line = default_line - val.line;
      else
	val.line = 1;
      break;
    case none:
      break;		/* No need to adjust val.line.  */
    }

  while (*q == ' ' || *q == '\t')
    q++;
  *argptr = q;
  if (file_symtab == 0)
    file_symtab = default_symtab;

  /* It is possible that this source file has more than one symtab, 
     and that the new line number specification has moved us from the
     default (in file_symtab) to a new one.  */
  val.symtab = find_line_symtab (file_symtab, val.line, NULL, NULL);
  if (val.symtab == 0)
    val.symtab = file_symtab;

  val.pc = 0;
  values.sals = (struct symtab_and_line *)
    xmalloc (sizeof (struct symtab_and_line));
  values.sals[0] = val;
  values.nelts = 1;
  if (need_canonical)
    build_canonical_line_spec (values.sals, NULL, canonical);
  return values;
}



/* Decode a linespec starting with a dollar sign.  */

static struct symtabs_and_lines
decode_dollar (char *copy, int funfirstline, struct symtab *default_symtab,
	       char ***canonical, struct symtab *file_symtab)
{
  struct value *valx;
  int index = 0;
  int need_canonical = 0;
  struct symtabs_and_lines values;
  struct symtab_and_line val;
  char *p;
  struct symbol *sym;
  /* The symtab that SYM was found in.  */
  struct symtab *sym_symtab;
  struct minimal_symbol *msymbol;

  p = (copy[1] == '$') ? copy + 2 : copy + 1;
  while (*p >= '0' && *p <= '9')
    p++;
  if (!*p)		/* Reached end of token without hitting non-digit.  */
    {
      /* We have a value history reference.  */
      sscanf ((copy[1] == '$') ? copy + 2 : copy + 1, "%d", &index);
      valx = access_value_history ((copy[1] == '$') ? -index : index);
      if (TYPE_CODE (VALUE_TYPE (valx)) != TYPE_CODE_INT)
	error ("History values used in line specs must have integer values.");
    }
  else
    {
      /* Not all digits -- may be user variable/function or a
	 convenience variable.  */

      /* Look up entire name as a symbol first.  */
      sym = lookup_symbol (copy, 0, VAR_DOMAIN, 0, &sym_symtab);
      file_symtab = (struct symtab *) 0;
      need_canonical = 1;
      /* Symbol was found --> jump to normal symbol processing.  */
      if (sym)
	return symbol_found (funfirstline, canonical, copy, sym,
			     NULL, sym_symtab);

      /* If symbol was not found, look in minimal symbol tables.  */
      msymbol = lookup_minimal_symbol (copy, NULL, NULL);
      /* Min symbol was found --> jump to minsym processing.  */
      if (msymbol)
	return minsym_found (funfirstline, msymbol);

      /* Not a user variable or function -- must be convenience variable.  */
      need_canonical = (file_symtab == 0) ? 1 : 0;
      valx = value_of_internalvar (lookup_internalvar (copy + 1));
      if (TYPE_CODE (VALUE_TYPE (valx)) != TYPE_CODE_INT)
	error ("Convenience variables used in line specs must have integer values.");
    }

  init_sal (&val);

  /* Either history value or convenience value from above, in valx.  */
  val.symtab = file_symtab ? file_symtab : default_symtab;
  val.line = value_as_long (valx);
  val.pc = 0;

  values.sals = (struct symtab_and_line *) xmalloc (sizeof val);
  values.sals[0] = val;
  values.nelts = 1;

  if (need_canonical)
    build_canonical_line_spec (values.sals, NULL, canonical);

  return values;
}



/* Decode a linespec that's a variable.  If FILE_SYMTAB is non-NULL,
   look in that symtab's static variables first.  If NOT_FOUND_PTR is not NULL and
   the function cannot be found, store boolean true in the location pointed to
   and do not issue an error message.  */ 

static struct symtabs_and_lines
decode_variable (char *copy, int funfirstline, char ***canonical,
		 struct symtab *file_symtab, int *not_found_ptr)
{
  struct symbol *sym;
  /* The symtab that SYM was found in.  */
  struct symtab *sym_symtab;

  struct minimal_symbol *msymbol;

  sym = lookup_symbol (copy,
		       (file_symtab
			? BLOCKVECTOR_BLOCK (BLOCKVECTOR (file_symtab),
					     STATIC_BLOCK)
			: get_selected_block (0)),
		       VAR_DOMAIN, 0, &sym_symtab);

  if (sym != NULL)
    return symbol_found (funfirstline, canonical, copy, sym,
			 file_symtab, sym_symtab);

  msymbol = lookup_minimal_symbol (copy, NULL, NULL);

  if (msymbol != NULL)
    return minsym_found (funfirstline, msymbol);

  if (!have_full_symbols () &&
      !have_partial_symbols () && !have_minimal_symbols ())
    error ("No symbol table is loaded.  Use the \"file\" command.");

  if (not_found_ptr)
    {
      *not_found_ptr = 1;
      /* The caller has indicated that it wishes quiet notification of any
	 error where the function or file is not found.  A call to 
	 error_silent causes an error to occur, but it does not issue 
	 the supplied message.  The message can be manually output by
	 the caller, if desired.  This is used, for example, when 
	 attempting to set breakpoints for functions in shared libraries 
	 that have not yet been loaded.  */
      error_silent ("Function \"%s\" not defined.", copy);
    }
  
  error ("Function \"%s\" not defined.", copy);
}




/* Now come some functions that are called from multiple places within
   decode_line_1.  */

/* We've found a symbol SYM to associate with our linespec; build a
   corresponding struct symtabs_and_lines.  */

static struct symtabs_and_lines
symbol_found (int funfirstline, char ***canonical, char *copy,
	      struct symbol *sym, struct symtab *file_symtab,
	      struct symtab *sym_symtab)
{
  struct symtabs_and_lines values;
  
  if (SYMBOL_CLASS (sym) == LOC_BLOCK)
    {
      /* Arg is the name of a function */
      values.sals = (struct symtab_and_line *)
	xmalloc (sizeof (struct symtab_and_line));
      values.sals[0] = find_function_start_sal (sym, funfirstline);
      values.nelts = 1;

      /* Don't use the SYMBOL_LINE; if used at all it points to
	 the line containing the parameters or thereabouts, not
	 the first line of code.  */

      /* We might need a canonical line spec if it is a static
	 function.  */
      if (file_symtab == 0)
	{
	  struct blockvector *bv = BLOCKVECTOR (sym_symtab);
	  struct block *b = BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK);
	  if (lookup_block_symbol (b, copy, NULL, VAR_DOMAIN) != NULL)
	    build_canonical_line_spec (values.sals, copy, canonical);
	}
      return values;
    }
  else
    {
      if (funfirstline)
	error ("\"%s\" is not a function", copy);
      else if (SYMBOL_LINE (sym) != 0)
	{
	  /* We know its line number.  */
	  values.sals = (struct symtab_and_line *)
	    xmalloc (sizeof (struct symtab_and_line));
	  values.nelts = 1;
	  memset (&values.sals[0], 0, sizeof (values.sals[0]));
	  values.sals[0].symtab = sym_symtab;
	  values.sals[0].line = SYMBOL_LINE (sym);
	  return values;
	}
      else
	/* This can happen if it is compiled with a compiler which doesn't
	   put out line numbers for variables.  */
	/* FIXME: Shouldn't we just set .line and .symtab to zero
	   and return?  For example, "info line foo" could print
	   the address.  */
	error ("Line number not known for symbol \"%s\"", copy);
    }
}

/* We've found a minimal symbol MSYMBOL to associate with our
   linespec; build a corresponding struct symtabs_and_lines.  */

static struct symtabs_and_lines
minsym_found (int funfirstline, struct minimal_symbol *msymbol)
{
  struct symtabs_and_lines values;

  values.sals = (struct symtab_and_line *)
    xmalloc (sizeof (struct symtab_and_line));
  values.sals[0] = find_pc_sect_line (SYMBOL_VALUE_ADDRESS (msymbol),
				      (struct bfd_section *) 0, 0);
  values.sals[0].section = SYMBOL_BFD_SECTION (msymbol);
  if (funfirstline)
    {
      values.sals[0].pc += FUNCTION_START_OFFSET;
      values.sals[0].pc = SKIP_PROLOGUE (values.sals[0].pc);
    }
  values.nelts = 1;
  return values;
}
