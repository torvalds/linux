/* Process source files and output type information.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* A file position, mostly for error messages.  
   The FILE element may be compared using pointer equality.  */
struct fileloc {
  const char *file;
  int line;
};

/* Kinds of types we can understand.  */
enum typekind {
  TYPE_SCALAR,
  TYPE_STRING,
  TYPE_STRUCT,
  TYPE_UNION,
  TYPE_POINTER,
  TYPE_ARRAY,
  TYPE_LANG_STRUCT,
  TYPE_PARAM_STRUCT
};

typedef struct pair *pair_p;
typedef struct type *type_p;
typedef unsigned lang_bitmap;

/* Option data for the 'nested_ptr' option.  */
struct nested_ptr_data {
  type_p type;
  const char *convert_to;
  const char *convert_from;
};    

/* A way to pass data through to the output end.  */
typedef struct options {
  struct options *next;
  const char *name;
  const char *info;
} *options_p;

/* A name and a type.  */
struct pair {
  pair_p next;
  const char *name;
  type_p type;
  struct fileloc line;
  options_p opt;
};

#define NUM_PARAM 10

/* A description of a type.  */
enum gc_used_enum
  {
    GC_UNUSED = 0,
    GC_USED,
    GC_MAYBE_POINTED_TO,
    GC_POINTED_TO
  };

struct type {
  enum typekind kind;
  type_p next;
  type_p pointer_to;
  enum gc_used_enum gc_used;
  union {
    type_p p;
    struct {
      const char *tag;
      struct fileloc line;
      pair_p fields;
      options_p opt;
      lang_bitmap bitmap;
      type_p lang_struct;
    } s;
    char *sc;
    struct {
      type_p p;
      const char *len;
    } a;
    struct {
      type_p stru;
      type_p param[NUM_PARAM];
      struct fileloc line;
    } param_struct;
  } u;
};

#define UNION_P(x)					\
 ((x)->kind == TYPE_UNION || 				\
  ((x)->kind == TYPE_LANG_STRUCT 			\
   && (x)->u.s.lang_struct->kind == TYPE_UNION))
#define UNION_OR_STRUCT_P(x)			\
 ((x)->kind == TYPE_UNION 			\
  || (x)->kind == TYPE_STRUCT 			\
  || (x)->kind == TYPE_LANG_STRUCT)

/* The one and only TYPE_STRING.  */
extern struct type string_type;

/* Variables used to communicate between the lexer and the parser.  */
extern int lexer_toplevel_done;
extern struct fileloc lexer_line;

/* Print an error message.  */
extern void error_at_line 
  (struct fileloc *pos, const char *msg, ...) ATTRIBUTE_PRINTF_2;

/* Combines xmalloc() and vasprintf().  */
extern int xvasprintf (char **, const char *, va_list)
     ATTRIBUTE_PRINTF (2, 0);
/* Like the above, but more convenient for quick coding.  */
extern char * xasprintf (const char *, ...)
     ATTRIBUTE_PRINTF_1;

/* Constructor routines for types.  */
extern void do_typedef (const char *s, type_p t, struct fileloc *pos);
extern type_p resolve_typedef (const char *s, struct fileloc *pos);
extern type_p new_structure (const char *name, int isunion,
			     struct fileloc *pos, pair_p fields,
			     options_p o);
extern type_p find_structure (const char *s, int isunion);
extern type_p create_scalar_type (const char *name, size_t name_len);
extern type_p create_pointer (type_p t);
extern type_p create_array (type_p t, const char *len);
extern options_p create_option (options_p, const char *name, const void *info);
extern type_p adjust_field_type (type_p, options_p);
extern void note_variable (const char *s, type_p t, options_p o,
			   struct fileloc *pos);
extern void note_yacc_type (options_p o, pair_p fields,
			    pair_p typeinfo, struct fileloc *pos);

/* Lexer and parser routines, most automatically generated.  */
extern int yylex (void);
extern void yyerror (const char *);
extern int yyparse (void);
extern void parse_file (const char *name);

/* Output file handling.  */

/* Structure representing an output file.  */
struct outf 
{
  struct outf *next;
  const char *name;
  size_t buflength;
  size_t bufused;
  char *buf;
};

typedef struct outf * outf_p;

/* An output file, suitable for definitions, that can see declarations
   made in INPUT_FILE and is linked into every language that uses
   INPUT_FILE.  */
extern outf_p get_output_file_with_visibility 
   (const char *input_file);
const char *get_output_file_name (const char *);

/* A list of output files suitable for definitions.  There is one
   BASE_FILES entry for each language.  */
extern outf_p base_files[];

/* A bitmap that specifies which of BASE_FILES should be used to
   output a definition that is different for each language and must be
   defined once in each language that uses INPUT_FILE.  */
extern lang_bitmap get_base_file_bitmap (const char *input_file);

/* Print, like fprintf, to O.  */
extern void oprintf (outf_p o, const char *S, ...)
     ATTRIBUTE_PRINTF_2;
