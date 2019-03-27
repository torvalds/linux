/* Interface to C preprocessor macro tables for GDB.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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

#ifndef MACROTAB_H
#define MACROTAB_H

struct obstack;
struct bcache;

/* How do we represent a source location?  I mean, how should we
   represent them within GDB; the user wants to use all sorts of
   ambiguous abbreviations, like "break 32" and "break foo.c:32"
   ("foo.c" may have been #included into several compilation units),
   but what do we disambiguate those things to?

   - Answer 1: "Filename and line number."  (Or column number, if
   you're picky.)  That's not quite good enough.  For example, the
   same source file can be #included into several different
   compilation units --- which #inclusion do you mean?

   - Answer 2: "Compilation unit, filename, and line number."  This is
   a pretty good answer; GDB's `struct symtab_and_line' basically
   embodies this representation.  But it's still ambiguous; what if a
   given compilation unit #includes the same file twice --- how can I
   set a breakpoint on line 12 of the fifth #inclusion of "foo.c"?

   - Answer 3: "Compilation unit, chain of #inclusions, and line
   number."  This is analogous to the way GCC reports errors in
   #include files:

        $ gcc -c base.c
        In file included from header2.h:8,
                         from header1.h:3,
                         from base.c:5:
        header3.h:1: parse error before ')' token
        $

   GCC tells you exactly what path of #inclusions led you to the
   problem.  It gives you complete information, in a way that the
   following would not:

        $ gcc -c base.c
        header3.h:1: parse error before ')' token
        $

   Converting all of GDB to use this is a big task, and I'm not really
   suggesting it should be a priority.  But this module's whole
   purpose is to maintain structures describing the macro expansion
   process, so I think it's appropriate for us to take a little care
   to do that in a complete fashion.

   In this interface, the first line of a file is numbered 1, not 0.
   This is the same convention the rest of GDB uses.  */


/* A table of all the macro definitions for a given compilation unit.  */
struct macro_table;


/* A source file that participated in a compilation unit --- either a
   main file, or an #included file.  If a file is #included more than
   once, the presence of the `included_from' and `included_at_line'
   members means that we need to make one instance of this structure
   for each #inclusion.  Taken as a group, these structures form a
   tree mapping the #inclusions that contributed to the compilation
   unit, with the main source file as its root.

   Beware --- not every source file mentioned in a compilation unit's
   symtab structures will appear in the #inclusion tree!  As of Oct
   2002, GCC does record the effect of #line directives in the source
   line info, but not in macro info.  This means that GDB's symtabs
   (built from the former, among other things) may mention filenames
   that the #inclusion tree (built from the latter) doesn't have any
   record of.  See macroscope.c:sal_macro_scope for how to accomodate
   this.

   It's worth noting that libcpp has a simpler way of representing all
   this, which we should consider switching to.  It might even be
   suitable for ordinary non-macro line number info.

   Suppose you take your main source file, and after each line
   containing an #include directive you insert the text of the
   #included file.  The result is a big file that pretty much
   corresponds to the full text the compiler's going to see.  There's
   a one-to-one correspondence between lines in the big file and
   per-inclusion lines in the source files.  (Obviously, #include
   directives that are #if'd out don't count.  And you'll need to
   append a newline to any file that doesn't end in one, to avoid
   splicing the last #included line with the next line of the
   #including file.)

   Libcpp calls line numbers in this big imaginary file "logical line
   numbers", and has a data structure called a "line map" that can map
   logical line numbers onto actual source filenames and line numbers,
   and also tell you the chain of #inclusions responsible for any
   particular logical line number.  Basically, this means you can pass
   around a single line number and some kind of "compilation unit"
   object and you get nice, unambiguous source code locations that
   distinguish between multiple #inclusions of the same file, etc.

   Pretty neat, huh?  */

struct macro_source_file
{

  /* The macro table for the compilation unit this source location is
     a part of.  */
  struct macro_table *table;

  /* A source file --- possibly a header file.  */
  const char *filename;

  /* The location we were #included from, or zero if we are the
     compilation unit's main source file.  */
  struct macro_source_file *included_by;

  /* If `included_from' is non-zero, the line number in that source
     file at which we were included.  */
  int included_at_line;

  /* Head of a linked list of the source files #included by this file;
     our children in the #inclusion tree.  This list is sorted by its
     elements' `included_at_line' values, which are unique.  (The
     macro splay tree's ordering function needs this property.)  */
  struct macro_source_file *includes;

  /* The next file #included by our `included_from' file; our sibling
     in the #inclusion tree.  */
  struct macro_source_file *next_included;
};


/* Create a new, empty macro table.  Allocate it in OBSTACK, or use
   xmalloc if OBSTACK is zero.  Use BCACHE to store all macro names,
   arguments, definitions, and anything else that might be the same
   amongst compilation units in an executable file; if BCACHE is zero,
   don't cache these things.

   Note that, if either OBSTACK or BCACHE are non-zero, then you
   should only ever add information the macro table --- you should
   never remove things from it.  You'll get an error if you try.  At
   the moment, since we only provide obstacks and bcaches for macro
   tables for symtabs, this restriction makes a nice sanity check.
   Obstacks and bcaches are pretty much grow-only structures anyway.
   However, if we find that it's occasionally useful to delete things
   even from the symtab's tables, and the storage leak isn't a
   problem, this restriction could be lifted.  */
struct macro_table *new_macro_table (struct obstack *obstack,
                                     struct bcache *bcache);


/* Free TABLE, and any macro definitions, source file structures,
   etc. it owns.  This will raise an internal error if TABLE was
   allocated on an obstack, or if it uses a bcache.  */
void free_macro_table (struct macro_table *table);


/* Set FILENAME as the main source file of TABLE.  Return a source
   file structure describing that file; if we record the #definition
   of macros, or the #inclusion of other files into FILENAME, we'll
   use that source file structure to indicate the context.

   The "main source file" is the one that was given to the compiler;
   all other source files that contributed to the compilation unit are
   #included, directly or indirectly, from this one.

   The macro table makes its own copy of FILENAME; the caller is
   responsible for freeing FILENAME when it is no longer needed.  */
struct macro_source_file *macro_set_main (struct macro_table *table,
                                          const char *filename);


/* Return the main source file of the macro table TABLE.  */
struct macro_source_file *macro_main (struct macro_table *table);


/* Record a #inclusion.
   Record in SOURCE's macro table that, at line number LINE in SOURCE,
   we #included the file INCLUDED.  Return a source file structure we
   can use for symbols #defined or files #included into that.  If we've
   already created a source file structure for this #inclusion, return
   the same structure we created last time.

   The first line of the source file has a line number of 1, not 0.

   The macro table makes its own copy of INCLUDED; the caller is
   responsible for freeing INCLUDED when it is no longer needed.  */
struct macro_source_file *macro_include (struct macro_source_file *source,
                                         int line,
                                         const char *included);


/* Find any source file structure for a file named NAME, either
   included into SOURCE, or SOURCE itself.  Return zero if we have
   none.  NAME is only the final portion of the filename, not the full
   path.  e.g., `stdio.h', not `/usr/include/stdio.h'.  If NAME
   appears more than once in the inclusion tree, return the
   least-nested inclusion --- the one closest to the main source file.  */
struct macro_source_file *(macro_lookup_inclusion
                           (struct macro_source_file *source,
                            const char *name));


/* Record an object-like #definition (i.e., one with no parameter list).
   Record in SOURCE's macro table that, at line number LINE in SOURCE,
   we #defined a preprocessor symbol named NAME, whose replacement
   string is REPLACEMENT.  This function makes copies of NAME and
   REPLACEMENT; the caller is responsible for freeing them.  */
void macro_define_object (struct macro_source_file *source, int line,
                          const char *name, const char *replacement);


/* Record an function-like #definition (i.e., one with a parameter list).

   Record in SOURCE's macro table that, at line number LINE in SOURCE,
   we #defined a preprocessor symbol named NAME, with ARGC arguments
   whose names are given in ARGV, whose replacement string is REPLACEMENT.  If
   the macro takes a variable number of arguments, then ARGC should be
   one greater than the number of named arguments, and ARGV[ARGC-1]
   should be the string "...".  This function makes its own copies of
   NAME, ARGV, and REPLACEMENT; the caller is responsible for freeing
   them.  */
void macro_define_function (struct macro_source_file *source, int line,
                            const char *name, int argc, const char **argv,
                            const char *replacement);


/* Record an #undefinition.
   Record in SOURCE's macro table that, at line number LINE in SOURCE,
   we removed the definition for the preprocessor symbol named NAME.  */
void macro_undef (struct macro_source_file *source, int line,
                  const char *name);


/* Different kinds of macro definitions.  */
enum macro_kind
{
  macro_object_like,
  macro_function_like
};


/* A preprocessor symbol definition.  */
struct macro_definition
{
  /* The table this definition lives in.  */
  struct macro_table *table;

  /* What kind of macro it is.  */
  enum macro_kind kind;

  /* If `kind' is `macro_function_like', the number of arguments it
     takes, and their names.  The names, and the array of pointers to
     them, are in the table's bcache, if it has one.  */
  int argc;
  const char * const *argv;

  /* The replacement string (body) of the macro.  This is in the
     table's bcache, if it has one.  */
  const char *replacement;
};


/* Return a pointer to the macro definition for NAME in scope at line
   number LINE of SOURCE.  If LINE is -1, return the definition in
   effect at the end of the file.  The macro table owns the structure;
   the caller need not free it.  Return zero if NAME is not #defined
   at that point.  */
struct macro_definition *(macro_lookup_definition
                          (struct macro_source_file *source,
                           int line, const char *name));


/* Return the source location of the definition for NAME in scope at
   line number LINE of SOURCE.  Set *DEFINITION_LINE to the line
   number of the definition, and return a source file structure for
   the file.  Return zero if NAME has no definition in scope at that
   point, and leave *DEFINITION_LINE unchanged.  */
struct macro_source_file *(macro_definition_location
                           (struct macro_source_file *source,
                            int line,
                            const char *name,
                            int *definition_line));


#endif /* MACROTAB_H */
