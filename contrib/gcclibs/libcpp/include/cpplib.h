/* Definitions for CPP library.
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005
   Free Software Foundation, Inc.
   Written by Per Bothner, 1994-95.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */
#ifndef LIBCPP_CPPLIB_H
#define LIBCPP_CPPLIB_H

#include <sys/types.h>
#include "symtab.h"
#include "line-map.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cpp_reader cpp_reader;
typedef struct cpp_buffer cpp_buffer;
typedef struct cpp_options cpp_options;
typedef struct cpp_token cpp_token;
typedef struct cpp_string cpp_string;
typedef struct cpp_hashnode cpp_hashnode;
typedef struct cpp_macro cpp_macro;
typedef struct cpp_callbacks cpp_callbacks;
typedef struct cpp_dir cpp_dir;

struct answer;
struct _cpp_file;

/* The first three groups, apart from '=', can appear in preprocessor
   expressions (+= and -= are used to indicate unary + and - resp.).
   This allows a lookup table to be implemented in _cpp_parse_expr.

   The first group, to CPP_LAST_EQ, can be immediately followed by an
   '='.  The lexer needs operators ending in '=', like ">>=", to be in
   the same order as their counterparts without the '=', like ">>".

   See the cpp_operator table optab in expr.c if you change the order or
   add or remove anything in the first group.  */

#define TTYPE_TABLE							\
  OP(EQ,		"=")						\
  OP(NOT,		"!")						\
  OP(GREATER,		">")	/* compare */				\
  OP(LESS,		"<")						\
  OP(PLUS,		"+")	/* math */				\
  OP(MINUS,		"-")						\
  OP(MULT,		"*")						\
  OP(DIV,		"/")						\
  OP(MOD,		"%")						\
  OP(AND,		"&")	/* bit ops */				\
  OP(OR,		"|")						\
  OP(XOR,		"^")						\
  OP(RSHIFT,		">>")						\
  OP(LSHIFT,		"<<")						\
									\
  OP(COMPL,		"~")						\
  OP(AND_AND,		"&&")	/* logical */				\
  OP(OR_OR,		"||")						\
  OP(QUERY,		"?")						\
  OP(COLON,		":")						\
  OP(COMMA,		",")	/* grouping */				\
  OP(OPEN_PAREN,	"(")						\
  OP(CLOSE_PAREN,	")")						\
  TK(EOF,		NONE)						\
  OP(EQ_EQ,		"==")	/* compare */				\
  OP(NOT_EQ,		"!=")						\
  OP(GREATER_EQ,	">=")						\
  OP(LESS_EQ,		"<=")						\
									\
  /* These two are unary + / - in preprocessor expressions.  */		\
  OP(PLUS_EQ,		"+=")	/* math */				\
  OP(MINUS_EQ,		"-=")						\
									\
  OP(MULT_EQ,		"*=")						\
  OP(DIV_EQ,		"/=")						\
  OP(MOD_EQ,		"%=")						\
  OP(AND_EQ,		"&=")	/* bit ops */				\
  OP(OR_EQ,		"|=")						\
  OP(XOR_EQ,		"^=")						\
  OP(RSHIFT_EQ,		">>=")						\
  OP(LSHIFT_EQ,		"<<=")						\
  /* Digraphs together, beginning with CPP_FIRST_DIGRAPH.  */		\
  OP(HASH,		"#")	/* digraphs */				\
  OP(PASTE,		"##")						\
  OP(OPEN_SQUARE,	"[")						\
  OP(CLOSE_SQUARE,	"]")						\
  OP(OPEN_BRACE,	"{")						\
  OP(CLOSE_BRACE,	"}")						\
  /* The remainder of the punctuation.	Order is not significant.  */	\
  OP(SEMICOLON,		";")	/* structure */				\
  OP(ELLIPSIS,		"...")						\
  OP(PLUS_PLUS,		"++")	/* increment */				\
  OP(MINUS_MINUS,	"--")						\
  OP(DEREF,		"->")	/* accessors */				\
  OP(DOT,		".")						\
  OP(SCOPE,		"::")						\
  OP(DEREF_STAR,	"->*")						\
  OP(DOT_STAR,		".*")						\
  OP(ATSIGN,		"@")  /* used in Objective-C */			\
									\
  TK(NAME,		IDENT)	 /* word */				\
  TK(AT_NAME,		IDENT)	 /* @word - Objective-C */		\
  TK(NUMBER,		LITERAL) /* 34_be+ta  */			\
									\
  TK(CHAR,		LITERAL) /* 'char' */				\
  TK(WCHAR,		LITERAL) /* L'char' */				\
  TK(OTHER,		LITERAL) /* stray punctuation */		\
									\
  TK(STRING,		LITERAL) /* "string" */				\
  TK(WSTRING,		LITERAL) /* L"string" */			\
  TK(OBJC_STRING,	LITERAL) /* @"string" - Objective-C */		\
  TK(HEADER_NAME,	LITERAL) /* <stdio.h> in #include */		\
									\
  TK(COMMENT,		LITERAL) /* Only if output comments.  */	\
				 /* SPELL_LITERAL happens to DTRT.  */	\
  TK(MACRO_ARG,		NONE)	 /* Macro argument.  */			\
  TK(PRAGMA,		NONE)	 /* Only for deferred pragmas.  */	\
  TK(PRAGMA_EOL,	NONE)	 /* End-of-line for deferred pragmas.  */ \
  TK(PADDING,		NONE)	 /* Whitespace for -E.	*/

#define OP(e, s) CPP_ ## e,
#define TK(e, s) CPP_ ## e,
enum cpp_ttype
{
  TTYPE_TABLE
  N_TTYPES,

  /* Positions in the table.  */
  CPP_LAST_EQ        = CPP_LSHIFT,
  CPP_FIRST_DIGRAPH  = CPP_HASH,
  CPP_LAST_PUNCTUATOR= CPP_ATSIGN,
  CPP_LAST_CPP_OP    = CPP_LESS_EQ
};
#undef OP
#undef TK

/* C language kind, used when calling cpp_create_reader.  */
enum c_lang {CLK_GNUC89 = 0, CLK_GNUC99, CLK_STDC89, CLK_STDC94, CLK_STDC99,
	     CLK_GNUCXX, CLK_CXX98, CLK_ASM};

/* Payload of a NUMBER, STRING, CHAR or COMMENT token.  */
struct cpp_string GTY(())
{
  unsigned int len;
  const unsigned char *text;
};

/* Flags for the cpp_token structure.  */
#define PREV_WHITE	(1 << 0) /* If whitespace before this token.  */
#define DIGRAPH		(1 << 1) /* If it was a digraph.  */
#define STRINGIFY_ARG	(1 << 2) /* If macro argument to be stringified.  */
#define PASTE_LEFT	(1 << 3) /* If on LHS of a ## operator.  */
#define NAMED_OP	(1 << 4) /* C++ named operators.  */
#define NO_EXPAND	(1 << 5) /* Do not macro-expand this token.  */
#define BOL		(1 << 6) /* Token at beginning of line.  */
#define PURE_ZERO	(1 << 7) /* Single 0 digit, used by the C++ frontend,
				    set in c-lex.c.  */

/* Specify which field, if any, of the cpp_token union is used.  */

enum cpp_token_fld_kind {
  CPP_TOKEN_FLD_NODE,
  CPP_TOKEN_FLD_SOURCE,
  CPP_TOKEN_FLD_STR,
  CPP_TOKEN_FLD_ARG_NO,
  CPP_TOKEN_FLD_PRAGMA,
  CPP_TOKEN_FLD_NONE
};

/* A preprocessing token.  This has been carefully packed and should
   occupy 16 bytes on 32-bit hosts and 24 bytes on 64-bit hosts.  */
struct cpp_token GTY(())
{
  source_location src_loc;	/* Location of first char of token.  */
  ENUM_BITFIELD(cpp_ttype) type : CHAR_BIT;  /* token type */
  unsigned char flags;		/* flags - see above */

  union cpp_token_u
  {
    /* An identifier.  */
    cpp_hashnode *
      GTY ((nested_ptr (union tree_node,
		"%h ? CPP_HASHNODE (GCC_IDENT_TO_HT_IDENT (%h)) : NULL",
			"%h ? HT_IDENT_TO_GCC_IDENT (HT_NODE (%h)) : NULL"),
	    tag ("CPP_TOKEN_FLD_NODE")))
	 node;
	 
    /* Inherit padding from this token.  */
    cpp_token * GTY ((tag ("CPP_TOKEN_FLD_SOURCE"))) source;

    /* A string, or number.  */
    struct cpp_string GTY ((tag ("CPP_TOKEN_FLD_STR"))) str;

    /* Argument no. for a CPP_MACRO_ARG.  */
    unsigned int GTY ((tag ("CPP_TOKEN_FLD_ARG_NO"))) arg_no;

    /* Caller-supplied identifier for a CPP_PRAGMA.  */
    unsigned int GTY ((tag ("CPP_TOKEN_FLD_PRAGMA"))) pragma;
  } GTY ((desc ("cpp_token_val_index (&%1)"))) val;
};

/* Say which field is in use.  */
extern enum cpp_token_fld_kind cpp_token_val_index (cpp_token *tok);

/* A type wide enough to hold any multibyte source character.
   cpplib's character constant interpreter requires an unsigned type.
   Also, a typedef for the signed equivalent.
   The width of this type is capped at 32 bits; there do exist targets
   where wchar_t is 64 bits, but only in a non-default mode, and there
   would be no meaningful interpretation for a wchar_t value greater
   than 2^32 anyway -- the widest wide-character encoding around is
   ISO 10646, which stops at 2^31.  */
#if CHAR_BIT * SIZEOF_INT >= 32
# define CPPCHAR_SIGNED_T int
#elif CHAR_BIT * SIZEOF_LONG >= 32
# define CPPCHAR_SIGNED_T long
#else
# error "Cannot find a least-32-bit signed integer type"
#endif
typedef unsigned CPPCHAR_SIGNED_T cppchar_t;
typedef CPPCHAR_SIGNED_T cppchar_signed_t;

/* Style of header dependencies to generate.  */
enum cpp_deps_style { DEPS_NONE = 0, DEPS_USER, DEPS_SYSTEM };

/* The possible normalization levels, from most restrictive to least.  */
enum cpp_normalize_level {
  /* In NFKC.  */
  normalized_KC = 0,
  /* In NFC.  */
  normalized_C,
  /* In NFC, except for subsequences where being in NFC would make
     the identifier invalid.  */
  normalized_identifier_C,
  /* Not normalized at all.  */
  normalized_none
};

/* This structure is nested inside struct cpp_reader, and
   carries all the options visible to the command line.  */
struct cpp_options
{
  /* Characters between tab stops.  */
  unsigned int tabstop;

  /* The language we're preprocessing.  */
  enum c_lang lang;

  /* Nonzero means use extra default include directories for C++.  */
  unsigned char cplusplus;

  /* Nonzero means handle cplusplus style comments.  */
  unsigned char cplusplus_comments;

  /* Nonzero means define __OBJC__, treat @ as a special token, and
     use the OBJC[PLUS]_INCLUDE_PATH environment variable.  */
  unsigned char objc;

  /* Nonzero means don't copy comments into the output file.  */
  unsigned char discard_comments;

  /* Nonzero means don't copy comments into the output file during
     macro expansion.  */
  unsigned char discard_comments_in_macro_exp;

  /* Nonzero means process the ISO trigraph sequences.  */
  unsigned char trigraphs;

  /* Nonzero means process the ISO digraph sequences.  */
  unsigned char digraphs;

  /* Nonzero means to allow hexadecimal floats and LL suffixes.  */
  unsigned char extended_numbers;

  /* Nonzero means print names of header files (-H).  */
  unsigned char print_include_names;

  /* Nonzero means cpp_pedwarn causes a hard error.  */
  unsigned char pedantic_errors;

  /* Nonzero means don't print warning messages.  */
  unsigned char inhibit_warnings;

  /* Nonzero means complain about deprecated features.  */
  unsigned char warn_deprecated;

  /* Nonzero means don't suppress warnings from system headers.  */
  unsigned char warn_system_headers;

  /* Nonzero means don't print error messages.  Has no option to
     select it, but can be set by a user of cpplib (e.g. fix-header).  */
  unsigned char inhibit_errors;

  /* Nonzero means warn if slash-star appears in a comment.  */
  unsigned char warn_comments;

  /* Nonzero means warn if a user-supplied include directory does not
     exist.  */
  unsigned char warn_missing_include_dirs;

  /* Nonzero means warn if there are any trigraphs.  */
  unsigned char warn_trigraphs;

  /* APPLE LOCAL begin -Wnewline-eof 2001-08-23 --sts */
  /* Nonzero means warn if no newline at end of file.  */
  unsigned char warn_newline_at_eof;
  /* APPLE LOCAL end -Wnewline-eof 2001-08-23 --sts */

  /* Nonzero means warn about multicharacter charconsts.  */
  unsigned char warn_multichar;

  /* Nonzero means warn about various incompatibilities with
     traditional C.  */
  unsigned char warn_traditional;

  /* Nonzero means warn about long long numeric constants.  */
  unsigned char warn_long_long;

  /* Nonzero means warn about text after an #endif (or #else).  */
  unsigned char warn_endif_labels;

  /* Nonzero means warn about implicit sign changes owing to integer
     promotions.  */
  unsigned char warn_num_sign_change;

  /* Zero means don't warn about __VA_ARGS__ usage in c89 pedantic mode.
     Presumably the usage is protected by the appropriate #ifdef.  */
  unsigned char warn_variadic_macros;

  /* Nonzero means turn warnings into errors.  */
  unsigned char warnings_are_errors;

  /* Nonzero means we should look for header.gcc files that remap file
     names.  */
  unsigned char remap;

  /* Zero means dollar signs are punctuation.  */
  unsigned char dollars_in_ident;

  /* Nonzero means UCNs are accepted in identifiers.  */
  unsigned char extended_identifiers;

  /* True if we should warn about dollars in identifiers or numbers
     for this translation unit.  */
  unsigned char warn_dollars;

  /* Nonzero means warn if undefined identifiers are evaluated in an #if.  */
  unsigned char warn_undef;

  /* Nonzero means warn of unused macros from the main file.  */
  unsigned char warn_unused_macros;

  /* Nonzero for the 1999 C Standard, including corrigenda and amendments.  */
  unsigned char c99;

  /* Nonzero if we are conforming to a specific C or C++ standard.  */
  unsigned char std;

  /* Nonzero means give all the error messages the ANSI standard requires.  */
  unsigned char pedantic;

  /* Nonzero means we're looking at already preprocessed code, so don't
     bother trying to do macro expansion and whatnot.  */
  unsigned char preprocessed;

  /* Print column number in error messages.  */
  unsigned char show_column;

  /* Nonzero means handle C++ alternate operator names.  */
  unsigned char operator_names;

  /* True for traditional preprocessing.  */
  unsigned char traditional;

  /* Holds the name of the target (execution) character set.  */
  const char *narrow_charset;

  /* Holds the name of the target wide character set.  */
  const char *wide_charset;

  /* Holds the name of the input character set.  */
  const char *input_charset;

  /* The minimum permitted level of normalization before a warning
     is generated.  */
  enum cpp_normalize_level warn_normalize;

  /* True to warn about precompiled header files we couldn't use.  */
  bool warn_invalid_pch;

  /* True if dependencies should be restored from a precompiled header.  */
  bool restore_pch_deps;

  /* Dependency generation.  */
  struct
  {
    /* Style of header dependencies to generate.  */
    enum cpp_deps_style style;

    /* Assume missing files are generated files.  */
    bool missing_files;

    /* Generate phony targets for each dependency apart from the first
       one.  */
    bool phony_targets;

    /* If true, no dependency is generated on the main file.  */
    bool ignore_main_file;
  } deps;

  /* Target-specific features set by the front end or client.  */

  /* Precision for target CPP arithmetic, target characters, target
     ints and target wide characters, respectively.  */
  size_t precision, char_precision, int_precision, wchar_precision;

  /* True means chars (wide chars) are unsigned.  */
  bool unsigned_char, unsigned_wchar;

  /* True if the most significant byte in a word has the lowest
     address in memory.  */
  bool bytes_big_endian;

  /* Nonzero means __STDC__ should have the value 0 in system headers.  */
  unsigned char stdc_0_in_system_headers;

  /* True means error callback should be used for diagnostics.  */
  bool client_diagnostic;

  /* True disables tokenization outside of preprocessing directives. */
  bool directives_only;
};

/* Callback for header lookup for HEADER, which is the name of a
   source file.  It is used as a method of last resort to find headers
   that are not otherwise found during the normal include processing.
   The return value is the malloced name of a header to try and open,
   if any, or NULL otherwise.  This callback is called only if the
   header is otherwise unfound.  */
typedef const char *(*missing_header_cb)(cpp_reader *, const char *header, cpp_dir **);

/* Call backs to cpplib client.  */
struct cpp_callbacks
{
  /* Called when a new line of preprocessed output is started.  */
  void (*line_change) (cpp_reader *, const cpp_token *, int);

  /* Called when switching to/from a new file.
     The line_map is for the new file.  It is NULL if there is no new file.
     (In C this happens when done with <built-in>+<command line> and also
     when done with a main file.)  This can be used for resource cleanup.  */
  void (*file_change) (cpp_reader *, const struct line_map *);

  void (*dir_change) (cpp_reader *, const char *);
  void (*include) (cpp_reader *, unsigned int, const unsigned char *,
		   const char *, int, const cpp_token **);
  void (*define) (cpp_reader *, unsigned int, cpp_hashnode *);
  void (*undef) (cpp_reader *, unsigned int, cpp_hashnode *);
  void (*ident) (cpp_reader *, unsigned int, const cpp_string *);
  void (*def_pragma) (cpp_reader *, unsigned int);
  int (*valid_pch) (cpp_reader *, const char *, int);
  void (*read_pch) (cpp_reader *, const char *, int, const char *);
  missing_header_cb missing_header;

  /* Called to emit a diagnostic if client_diagnostic option is true.
     This callback receives the translated message.  */
  void (*error) (cpp_reader *, int, const char *, va_list *)
       ATTRIBUTE_FPTR_PRINTF(3,0);
};

/* Chain of directories to look for include files in.  */
struct cpp_dir
{
  /* NULL-terminated singly-linked list.  */
  struct cpp_dir *next;

  /* NAME of the directory, NUL-terminated.  */
  char *name;
  unsigned int len;

  /* One if a system header, two if a system header that has extern
     "C" guards for C++.  */
  unsigned char sysp;

  /* Mapping of file names for this directory for MS-DOS and related
     platforms.  A NULL-terminated array of (from, to) pairs.  */
  const char **name_map;

  /* Routine to construct pathname, given the search path name and the
     HEADER we are trying to find, return a constructed pathname to
     try and open.  If this is NULL, the constructed pathname is as
     constructed by append_file_to_dir.  */
  char *(*construct) (const char *header, cpp_dir *dir);

  /* The C front end uses these to recognize duplicated
     directories in the search path.  */
  ino_t ino;
  dev_t dev;

  /* Is this a user-supplied directory? */
  bool user_supplied_p;
};

/* Name under which this program was invoked.  */
extern const char *progname;

/* The structure of a node in the hash table.  The hash table has
   entries for all identifiers: either macros defined by #define
   commands (type NT_MACRO), assertions created with #assert
   (NT_ASSERTION), or neither of the above (NT_VOID).  Builtin macros
   like __LINE__ are flagged NODE_BUILTIN.  Poisoned identifiers are
   flagged NODE_POISONED.  NODE_OPERATOR (C++ only) indicates an
   identifier that behaves like an operator such as "xor".
   NODE_DIAGNOSTIC is for speed in lex_token: it indicates a
   diagnostic may be required for this node.  Currently this only
   applies to __VA_ARGS__ and poisoned identifiers.  */

/* Hash node flags.  */
#define NODE_OPERATOR	(1 << 0)	/* C++ named operator.  */
#define NODE_POISONED	(1 << 1)	/* Poisoned identifier.  */
#define NODE_BUILTIN	(1 << 2)	/* Builtin macro.  */
#define NODE_DIAGNOSTIC (1 << 3)	/* Possible diagnostic when lexed.  */
#define NODE_WARN	(1 << 4)	/* Warn if redefined or undefined.  */
#define NODE_DISABLED	(1 << 5)	/* A disabled macro.  */
#define NODE_MACRO_ARG	(1 << 6)	/* Used during #define processing.  */

/* Different flavors of hash node.  */
enum node_type
{
  NT_VOID = 0,	   /* No definition yet.  */
  NT_MACRO,	   /* A macro of some form.  */
  NT_ASSERTION	   /* Predicate for #assert.  */
};

/* Different flavors of builtin macro.  _Pragma is an operator, but we
   handle it with the builtin code for efficiency reasons.  */
enum builtin_type
{
  BT_SPECLINE = 0,		/* `__LINE__' */
  BT_DATE,			/* `__DATE__' */
  BT_FILE,			/* `__FILE__' */
  BT_BASE_FILE,			/* `__BASE_FILE__' */
  BT_INCLUDE_LEVEL,		/* `__INCLUDE_LEVEL__' */
  BT_TIME,			/* `__TIME__' */
  BT_STDC,			/* `__STDC__' */
  BT_PRAGMA,			/* `_Pragma' operator */
  BT_TIMESTAMP,			/* `__TIMESTAMP__' */
  BT_COUNTER			/* `__COUNTER__' */
};

#define CPP_HASHNODE(HNODE)	((cpp_hashnode *) (HNODE))
#define HT_NODE(NODE)		((ht_identifier *) (NODE))
#define NODE_LEN(NODE)		HT_LEN (&(NODE)->ident)
#define NODE_NAME(NODE)		HT_STR (&(NODE)->ident)

/* Specify which field, if any, of the union is used.  */

enum {
  NTV_MACRO,
  NTV_ANSWER,
  NTV_BUILTIN,
  NTV_ARGUMENT,
  NTV_NONE
};

#define CPP_HASHNODE_VALUE_IDX(HNODE)				\
  ((HNODE.flags & NODE_MACRO_ARG) ? NTV_ARGUMENT		\
   : HNODE.type == NT_MACRO ? ((HNODE.flags & NODE_BUILTIN) 	\
			       ? NTV_BUILTIN : NTV_MACRO)	\
   : HNODE.type == NT_ASSERTION ? NTV_ANSWER			\
   : NTV_NONE)

/* The common part of an identifier node shared amongst all 3 C front
   ends.  Also used to store CPP identifiers, which are a superset of
   identifiers in the grammatical sense.  */

union _cpp_hashnode_value GTY(())
{
  /* If a macro.  */
  cpp_macro * GTY((tag ("NTV_MACRO"))) macro;
  /* Answers to an assertion.  */
  struct answer * GTY ((tag ("NTV_ANSWER"))) answers;
  /* Code for a builtin macro.  */
  enum builtin_type GTY ((tag ("NTV_BUILTIN"))) builtin;
  /* Macro argument index.  */
  unsigned short GTY ((tag ("NTV_ARGUMENT"))) arg_index;
};

struct cpp_hashnode GTY(())
{
  struct ht_identifier ident;
  unsigned int is_directive : 1;
  unsigned int directive_index : 7;	/* If is_directive,
					   then index into directive table.
					   Otherwise, a NODE_OPERATOR.  */
  unsigned char rid_code;		/* Rid code - for front ends.  */
  ENUM_BITFIELD(node_type) type : 8;	/* CPP node type.  */
  unsigned char flags;			/* CPP flags.  */

  union _cpp_hashnode_value GTY ((desc ("CPP_HASHNODE_VALUE_IDX (%1)"))) value;
};

/* Call this first to get a handle to pass to other functions.

   If you want cpplib to manage its own hashtable, pass in a NULL
   pointer.  Otherwise you should pass in an initialized hash table
   that cpplib will share; this technique is used by the C front
   ends.  */
extern cpp_reader *cpp_create_reader (enum c_lang, struct ht *,
				      struct line_maps *);

/* Call this to change the selected language standard (e.g. because of
   command line options).  */
extern void cpp_set_lang (cpp_reader *, enum c_lang);

/* Set the include paths.  */
extern void cpp_set_include_chains (cpp_reader *, cpp_dir *, cpp_dir *, int);

/* Call these to get pointers to the options, callback, and deps
   structures for a given reader.  These pointers are good until you
   call cpp_finish on that reader.  You can either edit the callbacks
   through the pointer returned from cpp_get_callbacks, or set them
   with cpp_set_callbacks.  */
extern cpp_options *cpp_get_options (cpp_reader *);
extern cpp_callbacks *cpp_get_callbacks (cpp_reader *);
extern void cpp_set_callbacks (cpp_reader *, cpp_callbacks *);
extern struct deps *cpp_get_deps (cpp_reader *);

/* This function reads the file, but does not start preprocessing.  It
   returns the name of the original file; this is the same as the
   input file, except for preprocessed input.  This will generate at
   least one file change callback, and possibly a line change callback
   too.  If there was an error opening the file, it returns NULL.  */
extern const char *cpp_read_main_file (cpp_reader *, const char *);

/* Set up built-ins with special behavior.  Use cpp_init_builtins()
   instead unless your know what you are doing.  */
extern void cpp_init_special_builtins (cpp_reader *);

/* Set up built-ins like __FILE__.  */
extern void cpp_init_builtins (cpp_reader *, int);

/* This is called after options have been parsed, and partially
   processed.  */
extern void cpp_post_options (cpp_reader *);

/* Set up translation to the target character set.  */
extern void cpp_init_iconv (cpp_reader *);

/* Call this to finish preprocessing.  If you requested dependency
   generation, pass an open stream to write the information to,
   otherwise NULL.  It is your responsibility to close the stream.

   Returns cpp_errors (pfile).  */
extern int cpp_finish (cpp_reader *, FILE *deps_stream);

/* Call this to release the handle at the end of preprocessing.  Any
   use of the handle after this function returns is invalid.  Returns
   cpp_errors (pfile).  */
extern void cpp_destroy (cpp_reader *);

/* Error count.  */
extern unsigned int cpp_errors (cpp_reader *);

extern unsigned int cpp_token_len (const cpp_token *);
extern unsigned char *cpp_token_as_text (cpp_reader *, const cpp_token *);
extern unsigned char *cpp_spell_token (cpp_reader *, const cpp_token *,
				       unsigned char *, bool);
extern void cpp_register_pragma (cpp_reader *, const char *, const char *,
				 void (*) (cpp_reader *), bool);
extern void cpp_register_deferred_pragma (cpp_reader *, const char *,
					  const char *, unsigned, bool, bool);
extern int cpp_avoid_paste (cpp_reader *, const cpp_token *,
			    const cpp_token *);
extern const cpp_token *cpp_get_token (cpp_reader *);
extern const unsigned char *cpp_macro_definition (cpp_reader *,
						  const cpp_hashnode *);
extern void _cpp_backup_tokens (cpp_reader *, unsigned int);

/* Evaluate a CPP_CHAR or CPP_WCHAR token.  */
extern cppchar_t cpp_interpret_charconst (cpp_reader *, const cpp_token *,
					  unsigned int *, int *);
/* Evaluate a vector of CPP_STRING or CPP_WSTRING tokens.  */
extern bool cpp_interpret_string (cpp_reader *,
				  const cpp_string *, size_t,
				  cpp_string *, bool);
extern bool cpp_interpret_string_notranslate (cpp_reader *,
					      const cpp_string *, size_t,
					      cpp_string *, bool);

/* Convert a host character constant to the execution character set.  */
extern cppchar_t cpp_host_to_exec_charset (cpp_reader *, cppchar_t);

/* Used to register macros and assertions, perhaps from the command line.
   The text is the same as the command line argument.  */
extern void cpp_define (cpp_reader *, const char *);
extern void cpp_assert (cpp_reader *, const char *);
extern void cpp_undef (cpp_reader *, const char *);
extern void cpp_unassert (cpp_reader *, const char *);

/* Undefine all macros and assertions.  */
extern void cpp_undef_all (cpp_reader *);

extern cpp_buffer *cpp_push_buffer (cpp_reader *, const unsigned char *,
				    size_t, int);
extern int cpp_defined (cpp_reader *, const unsigned char *, int);

/* A preprocessing number.  Code assumes that any unused high bits of
   the double integer are set to zero.  */
typedef unsigned HOST_WIDE_INT cpp_num_part;
typedef struct cpp_num cpp_num;
struct cpp_num
{
  cpp_num_part high;
  cpp_num_part low;
  bool unsignedp;  /* True if value should be treated as unsigned.  */
  bool overflow;   /* True if the most recent calculation overflowed.  */
};

/* cpplib provides two interfaces for interpretation of preprocessing
   numbers.

   cpp_classify_number categorizes numeric constants according to
   their field (integer, floating point, or invalid), radix (decimal,
   octal, hexadecimal), and type suffixes.  */

#define CPP_N_CATEGORY  0x000F
#define CPP_N_INVALID	0x0000
#define CPP_N_INTEGER	0x0001
#define CPP_N_FLOATING	0x0002

#define CPP_N_WIDTH	0x00F0
#define CPP_N_SMALL	0x0010	/* int, float.  */
#define CPP_N_MEDIUM	0x0020	/* long, double.  */
#define CPP_N_LARGE	0x0040	/* long long, long double.  */

#define CPP_N_RADIX	0x0F00
#define CPP_N_DECIMAL	0x0100
#define CPP_N_HEX	0x0200
#define CPP_N_OCTAL	0x0400
#define CPP_N_BINARY	0x0800

#define CPP_N_UNSIGNED	0x1000	/* Properties.  */
#define CPP_N_IMAGINARY	0x2000
#define CPP_N_DFLOAT	0x4000
#define CPP_N_DEFAULT	0x8000

/* Classify a CPP_NUMBER token.  The return value is a combination of
   the flags from the above sets.  */
extern unsigned cpp_classify_number (cpp_reader *, const cpp_token *);

/* Evaluate a token classified as category CPP_N_INTEGER.  */
extern cpp_num cpp_interpret_integer (cpp_reader *, const cpp_token *,
				      unsigned int type);

/* Sign extend a number, with PRECISION significant bits and all
   others assumed clear, to fill out a cpp_num structure.  */
cpp_num cpp_num_sign_extend (cpp_num, size_t);

/* Diagnostic levels.  To get a diagnostic without associating a
   position in the translation unit with it, use cpp_error_with_line
   with a line number of zero.  */

/* Warning, an error with -Werror.  */
#define CPP_DL_WARNING		0x00
/* Same as CPP_DL_WARNING, except it is not suppressed in system headers.  */
#define CPP_DL_WARNING_SYSHDR	0x01
/* Warning, an error with -pedantic-errors or -Werror.  */
#define CPP_DL_PEDWARN		0x02
/* An error.  */
#define CPP_DL_ERROR		0x03
/* An internal consistency check failed.  Prints "internal error: ",
   otherwise the same as CPP_DL_ERROR.  */
#define CPP_DL_ICE		0x04
/* Extracts a diagnostic level from an int.  */
#define CPP_DL_EXTRACT(l)	(l & 0xf)
/* Nonzero if a diagnostic level is one of the warnings.  */
#define CPP_DL_WARNING_P(l)	(CPP_DL_EXTRACT (l) >= CPP_DL_WARNING \
				 && CPP_DL_EXTRACT (l) <= CPP_DL_PEDWARN)

/* Output a diagnostic of some kind.  */
extern void cpp_error (cpp_reader *, int, const char *msgid, ...)
  ATTRIBUTE_PRINTF_3;

/* Output a diagnostic with "MSGID: " preceding the
   error string of errno.  No location is printed.  */
extern void cpp_errno (cpp_reader *, int, const char *msgid);

/* Same as cpp_error, except additionally specifies a position as a
   (translation unit) physical line and physical column.  If the line is
   zero, then no location is printed.  */
extern void cpp_error_with_line (cpp_reader *, int, source_location, unsigned,
				 const char *msgid, ...) ATTRIBUTE_PRINTF_5;

/* In cpplex.c */
extern int cpp_ideq (const cpp_token *, const char *);
extern void cpp_output_line (cpp_reader *, FILE *);
extern void cpp_output_token (const cpp_token *, FILE *);
extern const char *cpp_type2name (enum cpp_ttype);
/* Returns the value of an escape sequence, truncated to the correct
   target precision.  PSTR points to the input pointer, which is just
   after the backslash.  LIMIT is how much text we have.  WIDE is true
   if the escape sequence is part of a wide character constant or
   string literal.  Handles all relevant diagnostics.  */
extern cppchar_t cpp_parse_escape (cpp_reader *, const unsigned char ** pstr,
				   const unsigned char *limit, int wide);

/* In cpphash.c */

/* Lookup an identifier in the hashtable.  Puts the identifier in the
   table if it is not already there.  */
extern cpp_hashnode *cpp_lookup (cpp_reader *, const unsigned char *,
				 unsigned int);

typedef int (*cpp_cb) (cpp_reader *, cpp_hashnode *, void *);
extern void cpp_forall_identifiers (cpp_reader *, cpp_cb, void *);

/* In cppmacro.c */
extern void cpp_scan_nooutput (cpp_reader *);
extern int  cpp_sys_macro_p (cpp_reader *);
extern unsigned char *cpp_quote_string (unsigned char *, const unsigned char *,
					unsigned int);

/* In cppfiles.c */
extern bool cpp_included (cpp_reader *, const char *);
extern void cpp_make_system_header (cpp_reader *, int, int);
extern bool cpp_push_include (cpp_reader *, const char *);
extern void cpp_change_file (cpp_reader *, enum lc_reason, const char *);
extern const char *cpp_get_path (struct _cpp_file *);
extern cpp_dir *cpp_get_dir (struct _cpp_file *);
extern cpp_buffer *cpp_get_buffer (cpp_reader *);
extern struct _cpp_file *cpp_get_file (cpp_buffer *);
extern cpp_buffer *cpp_get_prev (cpp_buffer *);

/* In cpppch.c */
struct save_macro_data;
extern int cpp_save_state (cpp_reader *, FILE *);
extern int cpp_write_pch_deps (cpp_reader *, FILE *);
extern int cpp_write_pch_state (cpp_reader *, FILE *);
extern int cpp_valid_state (cpp_reader *, const char *, int);
extern void cpp_prepare_state (cpp_reader *, struct save_macro_data **);
extern int cpp_read_state (cpp_reader *, const char *, FILE *,
			   struct save_macro_data *);

#ifdef __cplusplus
}
#endif

#endif /* ! LIBCPP_CPPLIB_H */
