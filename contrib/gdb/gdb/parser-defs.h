/* Parser definitions for GDB.

   Copyright 1986, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2002 Free Software Foundation, Inc.

   Modified from expread.y by the Department of Computer Science at the
   State University of New York at Buffalo.

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

#if !defined (PARSER_DEFS_H)
#define PARSER_DEFS_H 1

#include "doublest.h"

struct block;

extern struct expression *expout;
extern int expout_size;
extern int expout_ptr;

/* If this is nonzero, this block is used as the lexical context
   for symbol names.  */

extern struct block *expression_context_block;

/* If expression_context_block is non-zero, then this is the PC within
   the block that we want to evaluate expressions at.  When debugging
   C or C++ code, we use this to find the exact line we're at, and
   then look up the macro definitions active at that point.  */
extern CORE_ADDR expression_context_pc;

/* The innermost context required by the stack and register variables
   we've encountered so far. */
extern struct block *innermost_block;

/* The block in which the most recently discovered symbol was found.
   FIXME: Should be declared along with lookup_symbol in symtab.h; is not
   related specifically to parsing.  */
extern struct block *block_found;

/* Number of arguments seen so far in innermost function call.  */
extern int arglist_len;

/* A string token, either a char-string or bit-string.  Char-strings are
   used, for example, for the names of symbols. */

struct stoken
  {
    /* Pointer to first byte of char-string or first bit of bit-string */
    char *ptr;
    /* Length of string in bytes for char-string or bits for bit-string */
    int length;
  };

struct ttype
  {
    struct stoken stoken;
    struct type *type;
  };

struct symtoken
  {
    struct stoken stoken;
    struct symbol *sym;
    int is_a_field_of_this;
  };

struct objc_class_str
  {
    struct stoken stoken;
    struct type *type;
    int class;
  };


/* For parsing of complicated types.
   An array should be preceded in the list by the size of the array.  */
enum type_pieces
  {
    tp_end = -1, 
    tp_pointer, 
    tp_reference, 
    tp_array, 
    tp_function, 
    tp_const, 
    tp_volatile, 
    tp_space_identifier
  };
/* The stack can contain either an enum type_pieces or an int.  */
union type_stack_elt
  {
    enum type_pieces piece;
    int int_val;
  };
extern union type_stack_elt *type_stack;
extern int type_stack_depth, type_stack_size;

extern void write_exp_elt (union exp_element);

extern void write_exp_elt_opcode (enum exp_opcode);

extern void write_exp_elt_sym (struct symbol *);

extern void write_exp_elt_longcst (LONGEST);

extern void write_exp_elt_dblcst (DOUBLEST);

extern void write_exp_elt_type (struct type *);

extern void write_exp_elt_intern (struct internalvar *);

extern void write_exp_string (struct stoken);

extern void write_exp_bitstring (struct stoken);

extern void write_exp_elt_block (struct block *);

extern void write_exp_msymbol (struct minimal_symbol *,
			       struct type *, struct type *);

extern void write_dollar_variable (struct stoken str);

extern struct symbol *parse_nested_classes_for_hpacc (char *, int, char **,
						      int *, char **);

extern char *find_template_name_end (char *);

extern void start_arglist (void);

extern int end_arglist (void);

extern char *copy_name (struct stoken);

extern void push_type (enum type_pieces);

extern void push_type_int (int);

extern void push_type_address_space (char *);

extern enum type_pieces pop_type (void);

extern int pop_type_int (void);

extern int length_of_subexp (struct expression *, int);

extern int dump_subexp (struct expression *, struct ui_file *, int);

extern int dump_subexp_body_standard (struct expression *, 
				      struct ui_file *, int);

extern void operator_length (struct expression *, int, int *, int *);

extern void operator_length_standard (struct expression *, int, int *, int *);

extern char *op_name_standard (enum exp_opcode);

extern struct type *follow_types (struct type *);

/* During parsing of a C expression, the pointer to the next character
   is in this variable.  */

extern char *lexptr;

/* After a token has been recognized, this variable points to it.  
   Currently used only for error reporting.  */
extern char *prev_lexptr;

/* Tokens that refer to names do so with explicit pointer and length,
   so they can share the storage that lexptr is parsing.

   When it is necessary to pass a name to a function that expects
   a null-terminated string, the substring is copied out
   into a block of storage that namecopy points to.

   namecopy is allocated once, guaranteed big enough, for each parsing.  */

extern char *namecopy;

/* Current depth in parentheses within the expression.  */

extern int paren_depth;

/* Nonzero means stop parsing on first comma (if not within parentheses).  */

extern int comma_terminates;

/* These codes indicate operator precedences for expression printing,
   least tightly binding first.  */
/* Adding 1 to a precedence value is done for binary operators,
   on the operand which is more tightly bound, so that operators
   of equal precedence within that operand will get parentheses.  */
/* PREC_HYPER and PREC_ABOVE_COMMA are not the precedence of any operator;
   they are used as the "surrounding precedence" to force
   various kinds of things to be parenthesized.  */
enum precedence
  {
    PREC_NULL, PREC_COMMA, PREC_ABOVE_COMMA, PREC_ASSIGN, PREC_LOGICAL_OR,
    PREC_LOGICAL_AND, PREC_BITWISE_IOR, PREC_BITWISE_AND, PREC_BITWISE_XOR,
    PREC_EQUAL, PREC_ORDER, PREC_SHIFT, PREC_ADD, PREC_MUL, PREC_REPEAT,
    PREC_HYPER, PREC_PREFIX, PREC_SUFFIX, PREC_BUILTIN_FUNCTION
  };

/* Table mapping opcodes into strings for printing operators
   and precedences of the operators.  */

struct op_print
  {
    char *string;
    enum exp_opcode opcode;
    /* Precedence of operator.  These values are used only by comparisons.  */
    enum precedence precedence;

    /* For a binary operator:  1 iff right associate.
       For a unary operator:  1 iff postfix. */
    int right_assoc;
  };

/* Information needed to print, prefixify, and evaluate expressions for 
   a given language.  */

struct exp_descriptor
  {
    /* Print subexpression.  */
    void (*print_subexp) (struct expression *, int *, struct ui_file *,
			  enum precedence);

    /* Returns number of exp_elements needed to represent an operator and
       the number of subexpressions it takes.  */
    void (*operator_length) (struct expression*, int, int*, int *);

    /* Name of this operator for dumping purposes.  */
    char *(*op_name) (enum exp_opcode);

    /* Dump the rest of this (prefix) expression after the operator
       itself has been printed.  See dump_subexp_body_standard in
       (expprint.c).  */
    int (*dump_subexp_body) (struct expression *, struct ui_file *, int);

    /* Evaluate an expression.  */
    struct value *(*evaluate_exp) (struct type *, struct expression *,
				   int *, enum noside);
  };


/* Default descriptor containing standard definitions of all
   elements.  */
extern const struct exp_descriptor exp_descriptor_standard;

/* Functions used by language-specific extended operators to (recursively)
   print/dump subexpressions.  */

extern void print_subexp (struct expression *, int *, struct ui_file *,
			  enum precedence);

extern void print_subexp_standard (struct expression *, int *, 
				   struct ui_file *, enum precedence);

/* Function used to avoid direct calls to fprintf
   in the code generated by the bison parser.  */

extern void parser_fprintf (FILE *, const char *, ...) ATTR_FORMAT (printf, 2 ,3);

#endif /* PARSER_DEFS_H */
