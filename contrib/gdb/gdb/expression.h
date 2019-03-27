/* Definitions for expressions stored in reversed prefix form, for GDB.

   Copyright 1986, 1989, 1992, 1994, 2000, 2003 Free Software
   Foundation, Inc.

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

#if !defined (EXPRESSION_H)
#define EXPRESSION_H 1


#include "symtab.h"		/* Needed for "struct block" type. */
#include "doublest.h"		/* Needed for DOUBLEST.  */


/* Definitions for saved C expressions.  */

/* An expression is represented as a vector of union exp_element's.
   Each exp_element is an opcode, except that some opcodes cause
   the following exp_element to be treated as a long or double constant
   or as a variable.  The opcodes are obeyed, using a stack for temporaries.
   The value is left on the temporary stack at the end.  */

/* When it is necessary to include a string,
   it can occupy as many exp_elements as it needs.
   We find the length of the string using strlen,
   divide to find out how many exp_elements are used up,
   and skip that many.  Strings, like numbers, are indicated
   by the preceding opcode.  */

enum exp_opcode
  {
    /* Used when it's necessary to pass an opcode which will be ignored,
       or to catch uninitialized values.  */
    OP_NULL,

/* BINOP_... operate on two values computed by following subexpressions,
   replacing them by one result value.  They take no immediate arguments.  */

    BINOP_ADD,			/* + */
    BINOP_SUB,			/* - */
    BINOP_MUL,			/* * */
    BINOP_DIV,			/* / */
    BINOP_REM,			/* % */
    BINOP_MOD,			/* mod (Knuth 1.2.4) */
    BINOP_LSH,			/* << */
    BINOP_RSH,			/* >> */
    BINOP_LOGICAL_AND,		/* && */
    BINOP_LOGICAL_OR,		/* || */
    BINOP_BITWISE_AND,		/* & */
    BINOP_BITWISE_IOR,		/* | */
    BINOP_BITWISE_XOR,		/* ^ */
    BINOP_EQUAL,		/* == */
    BINOP_NOTEQUAL,		/* != */
    BINOP_LESS,			/* < */
    BINOP_GTR,			/* > */
    BINOP_LEQ,			/* <= */
    BINOP_GEQ,			/* >= */
    BINOP_REPEAT,		/* @ */
    BINOP_ASSIGN,		/* = */
    BINOP_COMMA,		/* , */
    BINOP_SUBSCRIPT,		/* x[y] */
    BINOP_EXP,			/* Exponentiation */

    /* C++.  */

    BINOP_MIN,			/* <? */
    BINOP_MAX,			/* >? */

    /* STRUCTOP_MEMBER is used for pointer-to-member constructs.
       X . * Y translates into X STRUCTOP_MEMBER Y.  */
    STRUCTOP_MEMBER,

    /* STRUCTOP_MPTR is used for pointer-to-member constructs
       when X is a pointer instead of an aggregate.  */
    STRUCTOP_MPTR,

    /* end of C++.  */

    /* For Modula-2 integer division DIV */
    BINOP_INTDIV,

    BINOP_ASSIGN_MODIFY,	/* +=, -=, *=, and so on.
				   The following exp_element is another opcode,
				   a BINOP_, saying how to modify.
				   Then comes another BINOP_ASSIGN_MODIFY,
				   making three exp_elements in total.  */

    /* Modula-2 standard (binary) procedures */
    BINOP_VAL,
    BINOP_INCL,
    BINOP_EXCL,

    /* Concatenate two operands, such as character strings or bitstrings.
       If the first operand is a integer expression, then it means concatenate
       the second operand with itself that many times. */
    BINOP_CONCAT,

    /* For (the deleted) Chill and Pascal. */
    BINOP_IN,			/* Returns 1 iff ARG1 IN ARG2. */

    /* This is the "colon operator" used various places in (the
       deleted) Chill. */
    BINOP_RANGE,

    /* This must be the highest BINOP_ value, for expprint.c.  */
    BINOP_END,

    /* Operates on three values computed by following subexpressions.  */
    TERNOP_COND,		/* ?: */

    /* A sub-string/sub-array.  (the deleted) Chill syntax:
       OP1(OP2:OP3).  Return elements OP2 through OP3 of OP1.  */
    TERNOP_SLICE,

    /* A sub-string/sub-array.  (The deleted) Chill syntax: OP1(OP2 UP
       OP3).  Return OP3 elements of OP1, starting with element
       OP2. */
    TERNOP_SLICE_COUNT,

    /* Multidimensional subscript operator, such as Modula-2 x[a,b,...].
       The dimensionality is encoded in the operator, like the number of
       function arguments in OP_FUNCALL, I.E. <OP><dimension><OP>.
       The value of the first following subexpression is subscripted
       by each of the next following subexpressions, one per dimension. */
    MULTI_SUBSCRIPT,

    /* The OP_... series take immediate following arguments.
       After the arguments come another OP_... (the same one)
       so that the grouping can be recognized from the end.  */

    /* OP_LONG is followed by a type pointer in the next exp_element
       and the long constant value in the following exp_element.
       Then comes another OP_LONG.
       Thus, the operation occupies four exp_elements.  */
    OP_LONG,

    /* OP_DOUBLE is similar but takes a DOUBLEST constant instead of a long.  */
    OP_DOUBLE,

    /* OP_VAR_VALUE takes one struct block * in the following element,
       and one struct symbol * in the following exp_element, followed by
       another OP_VAR_VALUE, making four exp_elements.  If the block is
       non-NULL, evaluate the symbol relative to the innermost frame
       executing in that block; if the block is NULL use the selected frame.  */
    OP_VAR_VALUE,

    /* OP_LAST is followed by an integer in the next exp_element.
       The integer is zero for the last value printed,
       or it is the absolute number of a history element.
       With another OP_LAST at the end, this makes three exp_elements.  */
    OP_LAST,

    /* OP_REGISTER is followed by an integer in the next exp_element.
       This is the number of a register to fetch (as an int).
       With another OP_REGISTER at the end, this makes three exp_elements.  */
    OP_REGISTER,

    /* OP_INTERNALVAR is followed by an internalvar ptr in the next exp_element.
       With another OP_INTERNALVAR at the end, this makes three exp_elements.  */
    OP_INTERNALVAR,

    /* OP_FUNCALL is followed by an integer in the next exp_element.
       The integer is the number of args to the function call.
       That many plus one values from following subexpressions
       are used, the first one being the function.
       The integer is followed by a repeat of OP_FUNCALL,
       making three exp_elements.  */
    OP_FUNCALL,

    /* OP_OBJC_MSGCALL is followed by a string in the next exp_element and then an
       integer.  The string is the selector string.  The integer is the number
       of arguments to the message call.  That many plus one values are used, 
       the first one being the object pointer.  This is an Objective C message */
    OP_OBJC_MSGCALL,

    /* This is EXACTLY like OP_FUNCALL but is semantically different.  
       In F77, array subscript expressions, substring expressions
       and function calls are  all exactly the same syntactically. They may 
       only be dismabiguated at runtime.  Thus this operator, which 
       indicates that we have found something of the form <name> ( <stuff> ) */
    OP_F77_UNDETERMINED_ARGLIST,

    /* The following OP is a special one, it introduces a F77 complex
       literal. It is followed by exactly two args that are doubles.  */
    OP_COMPLEX,

    /* OP_STRING represents a string constant.
       Its format is the same as that of a STRUCTOP, but the string
       data is just made into a string constant when the operation
       is executed.  */
    OP_STRING,

    /* OP_BITSTRING represents a packed bitstring constant.
       Its format is the same as that of a STRUCTOP, but the bitstring
       data is just made into a bitstring constant when the operation
       is executed.  */
    OP_BITSTRING,

    /* OP_ARRAY creates an array constant out of the following subexpressions.
       It is followed by two exp_elements, the first containing an integer
       that is the lower bound of the array and the second containing another
       integer that is the upper bound of the array.  The second integer is
       followed by a repeat of OP_ARRAY, making four exp_elements total.
       The bounds are used to compute the number of following subexpressions
       to consume, as well as setting the bounds in the created array constant.
       The type of the elements is taken from the type of the first subexp,
       and they must all match. */
    OP_ARRAY,

    /* UNOP_CAST is followed by a type pointer in the next exp_element.
       With another UNOP_CAST at the end, this makes three exp_elements.
       It casts the value of the following subexpression.  */
    UNOP_CAST,

    /* UNOP_MEMVAL is followed by a type pointer in the next exp_element
       With another UNOP_MEMVAL at the end, this makes three exp_elements.
       It casts the contents of the word addressed by the value of the
       following subexpression.  */
    UNOP_MEMVAL,

    /* UNOP_... operate on one value from a following subexpression
       and replace it with a result.  They take no immediate arguments.  */

    UNOP_NEG,			/* Unary - */
    UNOP_LOGICAL_NOT,		/* Unary ! */
    UNOP_COMPLEMENT,		/* Unary ~ */
    UNOP_IND,			/* Unary * */
    UNOP_ADDR,			/* Unary & */
    UNOP_PREINCREMENT,		/* ++ before an expression */
    UNOP_POSTINCREMENT,		/* ++ after an expression */
    UNOP_PREDECREMENT,		/* -- before an expression */
    UNOP_POSTDECREMENT,		/* -- after an expression */
    UNOP_SIZEOF,		/* Unary sizeof (followed by expression) */

    UNOP_PLUS,			/* Unary plus */

    UNOP_CAP,			/* Modula-2 standard (unary) procedures */
    UNOP_CHR,
    UNOP_ORD,
    UNOP_ABS,
    UNOP_FLOAT,
    UNOP_HIGH,
    UNOP_MAX,
    UNOP_MIN,
    UNOP_ODD,
    UNOP_TRUNC,

    /* (The deleted) Chill builtin functions.  */
    UNOP_LOWER, UNOP_UPPER, UNOP_LENGTH, UNOP_CARD, UNOP_CHMAX, UNOP_CHMIN,

    OP_BOOL,			/* Modula-2 builtin BOOLEAN type */
    OP_M2_STRING,		/* Modula-2 string constants */

    /* STRUCTOP_... operate on a value from a following subexpression
       by extracting a structure component specified by a string
       that appears in the following exp_elements (as many as needed).
       STRUCTOP_STRUCT is used for "." and STRUCTOP_PTR for "->".
       They differ only in the error message given in case the value is
       not suitable or the structure component specified is not found.

       The length of the string follows the opcode, followed by
       BYTES_TO_EXP_ELEM(length) elements containing the data of the
       string, followed by the length again and the opcode again.  */

    STRUCTOP_STRUCT,
    STRUCTOP_PTR,

    /* C++: OP_THIS is just a placeholder for the class instance variable.
       It just comes in a tight (OP_THIS, OP_THIS) pair.  */
    OP_THIS,

    /* Objective-C: OP_OBJC_SELF is just a placeholder for the class instance
       variable.  It just comes in a tight (OP_OBJC_SELF, OP_OBJC_SELF) pair.  */
    OP_OBJC_SELF,

    /* Objective C: "@selector" pseudo-operator */
    OP_OBJC_SELECTOR,

    /* OP_SCOPE surrounds a type name and a field name.  The type
       name is encoded as one element, but the field name stays as
       a string, which, of course, is variable length.  */
    OP_SCOPE,

    /* Used to represent named structure field values in brace
       initializers (or tuples as they are called in (the deleted)
       Chill).

       The gcc C syntax is NAME:VALUE or .NAME=VALUE, the (the
       deleted) Chill syntax is .NAME:VALUE.  Multiple labels (as in
       the (the deleted) Chill syntax .NAME1,.NAME2:VALUE) is
       represented as if it were .NAME1:(.NAME2:VALUE) (though that is
       not valid (the deleted) Chill syntax).

       The NAME is represented as for STRUCTOP_STRUCT;  VALUE follows. */
    OP_LABELED,

    /* OP_TYPE is for parsing types, and used with the "ptype" command
       so we can look up types that are qualified by scope, either with
       the GDB "::" operator, or the Modula-2 '.' operator. */
    OP_TYPE,

    /* An un-looked-up identifier. */
    OP_NAME,

    /* An unparsed expression.  Used for Scheme (for now at least) */
    OP_EXPRSTRING,

    /* An Objective C Foundation Class NSString constant */
    OP_OBJC_NSSTRING,

     /* First extension operator.  Individual language modules define
        extra operators they need as constants with values 
        OP_LANGUAGE_SPECIFIC0 + k, for k >= 0, using a separate 
        enumerated type definition:
           enum foo_extension_operator {
             BINOP_MOGRIFY = OP_EXTENDED0,
 	     BINOP_FROB,
 	     ...
           };      */
    OP_EXTENDED0,
  
    /* Last possible extension operator.  Defined to provide an
       explicit and finite number of extended operators. */
    OP_EXTENDED_LAST = 0xff
    /* NOTE: Eventually, we expect to convert to an object-oriented 
       formulation for expression operators that does away with the
       need for these extension operators, and indeed for this
       entire enumeration type.  Therefore, consider the OP_EXTENDED
       definitions to be a temporary measure. */
  };

union exp_element
  {
    enum exp_opcode opcode;
    struct symbol *symbol;
    LONGEST longconst;
    DOUBLEST doubleconst;
    /* Really sizeof (union exp_element) characters (or less for the last
       element of a string).  */
    char string;
    struct type *type;
    struct internalvar *internalvar;
    struct block *block;
  };

struct expression
  {
    const struct language_defn *language_defn;	/* language it was entered in */
    int nelts;
    union exp_element elts[1];
  };

/* Macros for converting between number of expression elements and bytes
   to store that many expression elements. */

#define EXP_ELEM_TO_BYTES(elements) \
    ((elements) * sizeof (union exp_element))
#define BYTES_TO_EXP_ELEM(bytes) \
    (((bytes) + sizeof (union exp_element) - 1) / sizeof (union exp_element))

/* From parse.c */

extern struct expression *parse_expression (char *);

extern struct expression *parse_exp_1 (char **, struct block *, int);

/* The innermost context required by the stack and register variables
   we've encountered so far.  To use this, set it to NULL, then call
   parse_<whatever>, then look at it.  */
extern struct block *innermost_block;

/* From eval.c */

/* Values of NOSIDE argument to eval_subexp.  */

enum noside
  {
    EVAL_NORMAL,
    EVAL_SKIP,			/* Only effect is to increment pos.  */
    EVAL_AVOID_SIDE_EFFECTS	/* Don't modify any variables or
				   call any functions.  The value
				   returned will have the correct
				   type, and will have an
				   approximately correct lvalue
				   type (inaccuracy: anything that is
				   listed as being in a register in
				   the function in which it was
				   declared will be lval_register).  */
  };

extern struct value *evaluate_subexp_standard
  (struct type *, struct expression *, int *, enum noside);

/* From expprint.c */

extern void print_expression (struct expression *, struct ui_file *);

extern char *op_string (enum exp_opcode);

extern void dump_raw_expression (struct expression *, struct ui_file *, char *);
extern void dump_prefix_expression (struct expression *, struct ui_file *);

#endif /* !defined (EXPRESSION_H) */
