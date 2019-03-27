/* Output dbx-format symbol table information from GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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


/* Output dbx-format symbol table data.
   This consists of many symbol table entries, each of them
   a .stabs assembler pseudo-op with four operands:
   a "name" which is really a description of one symbol and its type,
   a "code", which is a symbol defined in stab.h whose name starts with N_,
   an unused operand always 0,
   and a "value" which is an address or an offset.
   The name is enclosed in doublequote characters.

   Each function, variable, typedef, and structure tag
   has a symbol table entry to define it.
   The beginning and end of each level of name scoping within
   a function are also marked by special symbol table entries.

   The "name" consists of the symbol name, a colon, a kind-of-symbol letter,
   and a data type number.  The data type number may be followed by
   "=" and a type definition; normally this will happen the first time
   the type number is mentioned.  The type definition may refer to
   other types by number, and those type numbers may be followed
   by "=" and nested definitions.

   This can make the "name" quite long.
   When a name is more than 80 characters, we split the .stabs pseudo-op
   into two .stabs pseudo-ops, both sharing the same "code" and "value".
   The first one is marked as continued with a double-backslash at the
   end of its "name".

   The kind-of-symbol letter distinguished function names from global
   variables from file-scope variables from parameters from auto
   variables in memory from typedef names from register variables.
   See `dbxout_symbol'.

   The "code" is mostly redundant with the kind-of-symbol letter
   that goes in the "name", but not entirely: for symbols located
   in static storage, the "code" says which segment the address is in,
   which controls how it is relocated.

   The "value" for a symbol in static storage
   is the core address of the symbol (actually, the assembler
   label for the symbol).  For a symbol located in a stack slot
   it is the stack offset; for one in a register, the register number.
   For a typedef symbol, it is zero.

   If DEBUG_SYMS_TEXT is defined, all debugging symbols must be
   output while in the text section.

   For more on data type definitions, see `dbxout_type'.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#include "tree.h"
#include "rtl.h"
#include "flags.h"
#include "regs.h"
#include "insn-config.h"
#include "reload.h"
#include "output.h"
#include "dbxout.h"
#include "toplev.h"
#include "tm_p.h"
#include "ggc.h"
#include "debug.h"
#include "function.h"
#include "target.h"
#include "langhooks.h"
#include "obstack.h"
#include "expr.h"

#ifdef XCOFF_DEBUGGING_INFO
#include "xcoffout.h"
#endif

#define DBXOUT_DECR_NESTING \
  if (--debug_nesting == 0 && symbol_queue_index > 0) \
    { emit_pending_bincls_if_required (); debug_flush_symbol_queue (); }

#define DBXOUT_DECR_NESTING_AND_RETURN(x) \
  do {--debug_nesting; return (x);} while (0)

#ifndef ASM_STABS_OP
# ifdef XCOFF_DEBUGGING_INFO
#  define ASM_STABS_OP "\t.stabx\t"
# else
#  define ASM_STABS_OP "\t.stabs\t"
# endif
#endif

#ifndef ASM_STABN_OP
#define ASM_STABN_OP "\t.stabn\t"
#endif

#ifndef ASM_STABD_OP
#define ASM_STABD_OP "\t.stabd\t"
#endif

#ifndef DBX_TYPE_DECL_STABS_CODE
#define DBX_TYPE_DECL_STABS_CODE N_LSYM
#endif

#ifndef DBX_STATIC_CONST_VAR_CODE
#define DBX_STATIC_CONST_VAR_CODE N_FUN
#endif

#ifndef DBX_REGPARM_STABS_CODE
#define DBX_REGPARM_STABS_CODE N_RSYM
#endif

#ifndef DBX_REGPARM_STABS_LETTER
#define DBX_REGPARM_STABS_LETTER 'P'
#endif

#ifndef NO_DBX_FUNCTION_END
#define NO_DBX_FUNCTION_END 0
#endif

#ifndef NO_DBX_BNSYM_ENSYM
#define NO_DBX_BNSYM_ENSYM 0
#endif

#ifndef NO_DBX_MAIN_SOURCE_DIRECTORY
#define NO_DBX_MAIN_SOURCE_DIRECTORY 0
#endif

#ifndef DBX_BLOCKS_FUNCTION_RELATIVE
#define DBX_BLOCKS_FUNCTION_RELATIVE 0
#endif

#ifndef DBX_LINES_FUNCTION_RELATIVE
#define DBX_LINES_FUNCTION_RELATIVE 0
#endif

#ifndef DBX_CONTIN_LENGTH
#define DBX_CONTIN_LENGTH 80
#endif

#ifndef DBX_CONTIN_CHAR
#define DBX_CONTIN_CHAR '\\'
#endif

enum typestatus {TYPE_UNSEEN, TYPE_XREF, TYPE_DEFINED};

/* Structure recording information about a C data type.
   The status element says whether we have yet output
   the definition of the type.  TYPE_XREF says we have
   output it as a cross-reference only.
   The file_number and type_number elements are used if DBX_USE_BINCL
   is defined.  */

struct typeinfo GTY(())
{
  enum typestatus status;
  int file_number;
  int type_number;
};

/* Vector recording information about C data types.
   When we first notice a data type (a tree node),
   we assign it a number using next_type_number.
   That is its index in this vector.  */

static GTY ((length ("typevec_len"))) struct typeinfo *typevec;

/* Number of elements of space allocated in `typevec'.  */

static GTY(()) int typevec_len;

/* In dbx output, each type gets a unique number.
   This is the number for the next type output.
   The number, once assigned, is in the TYPE_SYMTAB_ADDRESS field.  */

static GTY(()) int next_type_number;

/* The C front end may call dbxout_symbol before dbxout_init runs.
   We save all such decls in this list and output them when we get
   to dbxout_init.  */

static GTY(()) tree preinit_symbols;

enum binclstatus {BINCL_NOT_REQUIRED, BINCL_PENDING, BINCL_PROCESSED};

/* When using N_BINCL in dbx output, each type number is actually a
   pair of the file number and the type number within the file.
   This is a stack of input files.  */

struct dbx_file
{
  struct dbx_file *next;
  int file_number;
  int next_type_number;
  enum binclstatus bincl_status;  /* Keep track of lazy bincl.  */
  const char *pending_bincl_name; /* Name of bincl.  */
  struct dbx_file *prev;          /* Chain to traverse all pending bincls.  */
};

/* This is the top of the stack.  
   
   This is not saved for PCH, because restoring a PCH should not change it.
   next_file_number does have to be saved, because the PCH may use some
   file numbers; however, just before restoring a PCH, next_file_number
   should always be 0 because we should not have needed any file numbers
   yet.  */

#if (defined (DBX_DEBUGGING_INFO) || defined (XCOFF_DEBUGGING_INFO)) \
    && defined (DBX_USE_BINCL)
static struct dbx_file *current_file;
#endif

/* This is the next file number to use.  */

static GTY(()) int next_file_number;

/* A counter for dbxout_function_end.  */

static GTY(()) int scope_labelno;

/* A counter for dbxout_source_line.  */

static GTY(()) int dbxout_source_line_counter;

/* Number for the next N_SOL filename stabs label.  The number 0 is reserved
   for the N_SO filename stabs label.  */

static GTY(()) int source_label_number = 1;

/* Last source file name mentioned in a NOTE insn.  */

static GTY(()) const char *lastfile;

/* Used by PCH machinery to detect if 'lastfile' should be reset to
   base_input_file.  */
static GTY(()) int lastfile_is_base;

/* Typical USG systems don't have stab.h, and they also have
   no use for DBX-format debugging info.  */

#if defined (DBX_DEBUGGING_INFO) || defined (XCOFF_DEBUGGING_INFO)

#ifdef DBX_USE_BINCL
/* If zero then there is no pending BINCL.  */
static int pending_bincls = 0;
#endif

/* The original input file name.  */
static const char *base_input_file;

#ifdef DEBUG_SYMS_TEXT
#define FORCE_TEXT switch_to_section (current_function_section ())
#else
#define FORCE_TEXT
#endif

#include "gstab.h"

#define STAB_CODE_TYPE enum __stab_debug_code

/* 1 if PARM is passed to this function in memory.  */

#define PARM_PASSED_IN_MEMORY(PARM) \
 (MEM_P (DECL_INCOMING_RTL (PARM)))

/* A C expression for the integer offset value of an automatic variable
   (N_LSYM) having address X (an RTX).  */
#ifndef DEBUGGER_AUTO_OFFSET
#define DEBUGGER_AUTO_OFFSET(X) \
  (GET_CODE (X) == PLUS ? INTVAL (XEXP (X, 1)) : 0)
#endif

/* A C expression for the integer offset value of an argument (N_PSYM)
   having address X (an RTX).  The nominal offset is OFFSET.  */
#ifndef DEBUGGER_ARG_OFFSET
#define DEBUGGER_ARG_OFFSET(OFFSET, X) (OFFSET)
#endif

/* This obstack holds the stab string currently being constructed.  We
   build it up here, then write it out, so we can split long lines up
   properly (see dbxout_finish_complex_stabs).  */
static struct obstack stabstr_ob;
static size_t stabstr_last_contin_point;

#ifdef DBX_USE_BINCL
static void emit_bincl_stab             (const char *c);
static void emit_pending_bincls         (void);
#endif
static inline void emit_pending_bincls_if_required (void);

static void dbxout_init (const char *);
 
static void dbxout_finish (const char *);
static void dbxout_start_source_file (unsigned, const char *);
static void dbxout_end_source_file (unsigned);
static void dbxout_typedefs (tree);
static void dbxout_type_index (tree);
static void dbxout_args (tree);
static void dbxout_type_fields (tree);
static void dbxout_type_method_1 (tree);
static void dbxout_type_methods (tree);
static void dbxout_range_type (tree);
static void dbxout_type (tree, int);
static bool print_int_cst_bounds_in_octal_p (tree);
static void dbxout_type_name (tree);
static void dbxout_class_name_qualifiers (tree);
static int dbxout_symbol_location (tree, tree, const char *, rtx);
static void dbxout_symbol_name (tree, const char *, int);
static void dbxout_block (tree, int, tree);
static void dbxout_global_decl (tree);
static void dbxout_type_decl (tree, int);
static void dbxout_handle_pch (unsigned);

/* The debug hooks structure.  */
#if defined (DBX_DEBUGGING_INFO)

static void dbxout_source_line (unsigned int, const char *);
static void dbxout_begin_prologue (unsigned int, const char *);
static void dbxout_source_file (const char *);
static void dbxout_function_end (tree);
static void dbxout_begin_function (tree);
static void dbxout_begin_block (unsigned, unsigned);
static void dbxout_end_block (unsigned, unsigned);
static void dbxout_function_decl (tree);

const struct gcc_debug_hooks dbx_debug_hooks =
{
  dbxout_init,
  dbxout_finish,
  debug_nothing_int_charstar,
  debug_nothing_int_charstar,
  dbxout_start_source_file,
  dbxout_end_source_file,
  dbxout_begin_block,
  dbxout_end_block,
  debug_true_tree,		         /* ignore_block */
  dbxout_source_line,		         /* source_line */
  dbxout_begin_prologue,	         /* begin_prologue */
  debug_nothing_int_charstar,	         /* end_prologue */
  debug_nothing_int_charstar,	         /* end_epilogue */
#ifdef DBX_FUNCTION_FIRST
  dbxout_begin_function,
#else
  debug_nothing_tree,		         /* begin_function */
#endif
  debug_nothing_int,		         /* end_function */
  dbxout_function_decl,
  dbxout_global_decl,		         /* global_decl */
  dbxout_type_decl,			 /* type_decl */
  debug_nothing_tree_tree,               /* imported_module_or_decl */
  debug_nothing_tree,		         /* deferred_inline_function */
  debug_nothing_tree,		         /* outlining_inline_function */
  debug_nothing_rtx,		         /* label */
  dbxout_handle_pch,		         /* handle_pch */
  debug_nothing_rtx,		         /* var_location */
  debug_nothing_void,                    /* switch_text_section */
  0                                      /* start_end_main_source_file */
};
#endif /* DBX_DEBUGGING_INFO  */

#if defined (XCOFF_DEBUGGING_INFO)
const struct gcc_debug_hooks xcoff_debug_hooks =
{
  dbxout_init,
  dbxout_finish,
  debug_nothing_int_charstar,
  debug_nothing_int_charstar,
  dbxout_start_source_file,
  dbxout_end_source_file,
  xcoffout_begin_block,
  xcoffout_end_block,
  debug_true_tree,		         /* ignore_block */
  xcoffout_source_line,
  xcoffout_begin_prologue,	         /* begin_prologue */
  debug_nothing_int_charstar,	         /* end_prologue */
  xcoffout_end_epilogue,
  debug_nothing_tree,		         /* begin_function */
  xcoffout_end_function,
  debug_nothing_tree,		         /* function_decl */
  dbxout_global_decl,		         /* global_decl */
  dbxout_type_decl,			 /* type_decl */
  debug_nothing_tree_tree,               /* imported_module_or_decl */
  debug_nothing_tree,		         /* deferred_inline_function */
  debug_nothing_tree,		         /* outlining_inline_function */
  debug_nothing_rtx,		         /* label */
  dbxout_handle_pch,		         /* handle_pch */
  debug_nothing_rtx,		         /* var_location */
  debug_nothing_void,                    /* switch_text_section */
  0                                      /* start_end_main_source_file */
};
#endif /* XCOFF_DEBUGGING_INFO  */

/* Numeric formatting helper macro.  Note that this does not handle
   hexadecimal.  */
#define NUMBER_FMT_LOOP(P, NUM, BASE)		\
  do						\
    {						\
      int digit = NUM % BASE;			\
      NUM /= BASE;				\
      *--P = digit + '0';			\
    }						\
  while (NUM > 0)

/* Utility: write a decimal integer NUM to asm_out_file.  */
void
dbxout_int (int num)
{
  char buf[64];
  char *p = buf + sizeof buf;
  unsigned int unum;

  if (num == 0)
    {
      putc ('0', asm_out_file);
      return;
    }
  if (num < 0)
    {
      putc ('-', asm_out_file);
      unum = -num;
    }
  else
    unum = num;

  NUMBER_FMT_LOOP (p, unum, 10);

  while (p < buf + sizeof buf)
    {
      putc (*p, asm_out_file);
      p++;
    }
}


/* Primitives for emitting simple stabs directives.  All other stabs
   routines should use these functions instead of directly emitting
   stabs.  They are exported because machine-dependent code may need
   to invoke them, e.g. in a DBX_OUTPUT_* macro whose definition
   forwards to code in CPU.c.  */

/* The following functions should all be called immediately after one
   of the dbxout_begin_stab* functions (below).  They write out
   various things as the value of a stab.  */

/* Write out a literal zero as the value of a stab.  */
void
dbxout_stab_value_zero (void)
{
  fputs ("0\n", asm_out_file);
}

/* Write out the label LABEL as the value of a stab.  */
void
dbxout_stab_value_label (const char *label)
{
  assemble_name (asm_out_file, label);
  putc ('\n', asm_out_file);
}

/* Write out the difference of two labels, LABEL - BASE, as the value
   of a stab.  */
void
dbxout_stab_value_label_diff (const char *label, const char *base)
{
  assemble_name (asm_out_file, label);
  putc ('-', asm_out_file);
  assemble_name (asm_out_file, base);
  putc ('\n', asm_out_file);
}

/* Write out an internal label as the value of a stab, and immediately
   emit that internal label.  This should be used only when
   dbxout_stabd will not work.  STEM is the name stem of the label,
   COUNTERP is a pointer to a counter variable which will be used to
   guarantee label uniqueness.  */
void
dbxout_stab_value_internal_label (const char *stem, int *counterp)
{
  char label[100];
  int counter = counterp ? (*counterp)++ : 0;

  ASM_GENERATE_INTERNAL_LABEL (label, stem, counter);
  dbxout_stab_value_label (label);
  targetm.asm_out.internal_label (asm_out_file, stem, counter);
}

/* Write out the difference between BASE and an internal label as the
   value of a stab, and immediately emit that internal label.  STEM and
   COUNTERP are as for dbxout_stab_value_internal_label.  */
void
dbxout_stab_value_internal_label_diff (const char *stem, int *counterp,
				       const char *base)
{
  char label[100];
  int counter = counterp ? (*counterp)++ : 0;

  ASM_GENERATE_INTERNAL_LABEL (label, stem, counter);
  dbxout_stab_value_label_diff (label, base);
  targetm.asm_out.internal_label (asm_out_file, stem, counter);
}

/* The following functions produce specific kinds of stab directives.  */

/* Write a .stabd directive with type STYPE and desc SDESC to asm_out_file.  */
void
dbxout_stabd (int stype, int sdesc)
{
  fputs (ASM_STABD_OP, asm_out_file);
  dbxout_int (stype);
  fputs (",0,", asm_out_file);
  dbxout_int (sdesc);
  putc ('\n', asm_out_file);
}

/* Write a .stabn directive with type STYPE.  This function stops
   short of emitting the value field, which is the responsibility of
   the caller (normally it will be either a symbol or the difference
   of two symbols).  */

void
dbxout_begin_stabn (int stype)
{
  fputs (ASM_STABN_OP, asm_out_file);
  dbxout_int (stype);
  fputs (",0,0,", asm_out_file);
}

/* Write a .stabn directive with type N_SLINE and desc LINE.  As above,
   the value field is the responsibility of the caller.  */
void
dbxout_begin_stabn_sline (int lineno)
{
  fputs (ASM_STABN_OP, asm_out_file);
  dbxout_int (N_SLINE);
  fputs (",0,", asm_out_file);
  dbxout_int (lineno);
  putc (',', asm_out_file);
}

/* Begin a .stabs directive with string "", type STYPE, and desc and
   other fields 0.  The value field is the responsibility of the
   caller.  This function cannot be used for .stabx directives.  */
void
dbxout_begin_empty_stabs (int stype)
{
  fputs (ASM_STABS_OP, asm_out_file);
  fputs ("\"\",", asm_out_file);
  dbxout_int (stype);
  fputs (",0,0,", asm_out_file);
}

/* Begin a .stabs directive with string STR, type STYPE, and desc 0.
   The value field is the responsibility of the caller.  */
void
dbxout_begin_simple_stabs (const char *str, int stype)
{
  fputs (ASM_STABS_OP, asm_out_file);
  output_quoted_string (asm_out_file, str);
  putc (',', asm_out_file);
  dbxout_int (stype);
  fputs (",0,0,", asm_out_file);
}

/* As above but use SDESC for the desc field.  */
void
dbxout_begin_simple_stabs_desc (const char *str, int stype, int sdesc)
{
  fputs (ASM_STABS_OP, asm_out_file);
  output_quoted_string (asm_out_file, str);
  putc (',', asm_out_file);
  dbxout_int (stype);
  fputs (",0,", asm_out_file);
  dbxout_int (sdesc);
  putc (',', asm_out_file);
}

/* The next set of functions are entirely concerned with production of
   "complex" .stabs directives: that is, .stabs directives whose
   strings have to be constructed piecemeal.  dbxout_type,
   dbxout_symbol, etc. use these routines heavily.  The string is queued
   up in an obstack, then written out by dbxout_finish_complex_stabs, which
   is also responsible for splitting it up if it exceeds DBX_CONTIN_LENGTH.
   (You might think it would be more efficient to go straight to stdio
   when DBX_CONTIN_LENGTH is 0 (i.e. no length limit) but that turns
   out not to be the case, and anyway this needs fewer #ifdefs.)  */

/* Begin a complex .stabs directive.  If we can, write the initial
   ASM_STABS_OP to the asm_out_file.  */

static void
dbxout_begin_complex_stabs (void)
{
  emit_pending_bincls_if_required ();
  FORCE_TEXT;
  fputs (ASM_STABS_OP, asm_out_file);
  putc ('"', asm_out_file);
  gcc_assert (stabstr_last_contin_point == 0);
}

/* As above, but do not force text or emit pending bincls.  This is
   used by dbxout_symbol_location, which needs to do something else.  */
static void
dbxout_begin_complex_stabs_noforcetext (void)
{
  fputs (ASM_STABS_OP, asm_out_file);
  putc ('"', asm_out_file);
  gcc_assert (stabstr_last_contin_point == 0);
}

/* Add CHR, a single character, to the string being built.  */
#define stabstr_C(chr) obstack_1grow (&stabstr_ob, chr)

/* Add STR, a normal C string, to the string being built.  */
#define stabstr_S(str) obstack_grow (&stabstr_ob, str, strlen(str))

/* Add the text of ID, an IDENTIFIER_NODE, to the string being built.  */
#define stabstr_I(id) obstack_grow (&stabstr_ob, \
                                    IDENTIFIER_POINTER (id), \
                                    IDENTIFIER_LENGTH (id))

/* Add NUM, a signed decimal number, to the string being built.  */
static void
stabstr_D (HOST_WIDE_INT num)
{
  char buf[64];
  char *p = buf + sizeof buf;
  unsigned int unum;

  if (num == 0)
    {
      stabstr_C ('0');
      return;
    }
  if (num < 0)
    {
      stabstr_C ('-');
      unum = -num;
    }
  else
    unum = num;

  NUMBER_FMT_LOOP (p, unum, 10);

  obstack_grow (&stabstr_ob, p, (buf + sizeof buf) - p);
}

/* Add NUM, an unsigned decimal number, to the string being built.  */
static void
stabstr_U (unsigned HOST_WIDE_INT num)
{
  char buf[64];
  char *p = buf + sizeof buf;
  if (num == 0)
    {
      stabstr_C ('0');
      return;
    }
  NUMBER_FMT_LOOP (p, num, 10);
  obstack_grow (&stabstr_ob, p, (buf + sizeof buf) - p);
}

/* Add CST, an INTEGER_CST tree, to the string being built as an
   unsigned octal number.  This routine handles values which are
   larger than a single HOST_WIDE_INT.  */
static void
stabstr_O (tree cst)
{
  unsigned HOST_WIDE_INT high = TREE_INT_CST_HIGH (cst);
  unsigned HOST_WIDE_INT low = TREE_INT_CST_LOW (cst);

  char buf[128];
  char *p = buf + sizeof buf;

  /* GDB wants constants with no extra leading "1" bits, so
     we need to remove any sign-extension that might be
     present.  */
  {
    const unsigned int width = TYPE_PRECISION (TREE_TYPE (cst));
    if (width == HOST_BITS_PER_WIDE_INT * 2)
      ;
    else if (width > HOST_BITS_PER_WIDE_INT)
      high &= (((HOST_WIDE_INT) 1 << (width - HOST_BITS_PER_WIDE_INT)) - 1);
    else if (width == HOST_BITS_PER_WIDE_INT)
      high = 0;
    else
      high = 0, low &= (((HOST_WIDE_INT) 1 << width) - 1);
  }

  /* Leading zero for base indicator.  */
  stabstr_C ('0');

  /* If the value is zero, the base indicator will serve as the value
     all by itself.  */
  if (high == 0 && low == 0)
    return;

  /* If the high half is zero, we need only print the low half normally.  */
  if (high == 0)
    NUMBER_FMT_LOOP (p, low, 8);
  else
    {
      /* When high != 0, we need to print enough zeroes from low to
	 give the digits from high their proper place-values.  Hence
	 NUMBER_FMT_LOOP cannot be used.  */
      const int n_digits = HOST_BITS_PER_WIDE_INT / 3;
      int i;

      for (i = 1; i <= n_digits; i++)
	{
	  unsigned int digit = low % 8;
	  low /= 8;
	  *--p = '0' + digit;
	}

      /* Octal digits carry exactly three bits of information.  The
	 width of a HOST_WIDE_INT is not normally a multiple of three.
	 Therefore, the next digit printed probably needs to carry
	 information from both low and high.  */
      if (HOST_BITS_PER_WIDE_INT % 3 != 0)
	{
	  const int n_leftover_bits = HOST_BITS_PER_WIDE_INT % 3;
	  const int n_bits_from_high = 3 - n_leftover_bits;

	  const unsigned HOST_WIDE_INT
	    low_mask = (((unsigned HOST_WIDE_INT)1) << n_leftover_bits) - 1;
	  const unsigned HOST_WIDE_INT
	    high_mask = (((unsigned HOST_WIDE_INT)1) << n_bits_from_high) - 1;

	  unsigned int digit;

	  /* At this point, only the bottom n_leftover_bits bits of low
	     should be set.  */
	  gcc_assert (!(low & ~low_mask));

	  digit = (low | ((high & high_mask) << n_leftover_bits));
	  high >>= n_bits_from_high;

	  *--p = '0' + digit;
	}

      /* Now we can format high in the normal manner.  However, if
	 the only bits of high that were set were handled by the
	 digit split between low and high, high will now be zero, and
	 we don't want to print extra digits in that case.  */
      if (high)
	NUMBER_FMT_LOOP (p, high, 8);
    }

  obstack_grow (&stabstr_ob, p, (buf + sizeof buf) - p);
}

/* Called whenever it is safe to break a stabs string into multiple
   .stabs directives.  If the current string has exceeded the limit
   set by DBX_CONTIN_LENGTH, mark the current position in the buffer
   as a continuation point by inserting DBX_CONTIN_CHAR (doubled if
   it is a backslash) and a null character.  */
static inline void
stabstr_continue (void)
{
  if (DBX_CONTIN_LENGTH > 0
      && obstack_object_size (&stabstr_ob) - stabstr_last_contin_point
	 > DBX_CONTIN_LENGTH)
    {
      if (DBX_CONTIN_CHAR == '\\')
	obstack_1grow (&stabstr_ob, '\\');
      obstack_1grow (&stabstr_ob, DBX_CONTIN_CHAR);
      obstack_1grow (&stabstr_ob, '\0');
      stabstr_last_contin_point = obstack_object_size (&stabstr_ob);
    }
}
#define CONTIN stabstr_continue ()

/* Macro subroutine of dbxout_finish_complex_stabs, which emits
   all of the arguments to the .stabs directive after the string.
   Overridden by xcoffout.h.  CODE is the stabs code for this symbol;
   LINE is the source line to write into the desc field (in extended
   mode); SYM is the symbol itself.

   ADDR, LABEL, and NUMBER are three different ways to represent the
   stabs value field.  At most one of these should be nonzero.

     ADDR is used most of the time; it represents the value as an
     RTL address constant.

     LABEL is used (currently) only for N_CATCH stabs; it represents
     the value as a string suitable for assemble_name.

     NUMBER is used when the value is an offset from an implicit base
     pointer (e.g. for a stack variable), or an index (e.g. for a
     register variable).  It represents the value as a decimal integer.  */

#ifndef DBX_FINISH_STABS
#define DBX_FINISH_STABS(SYM, CODE, LINE, ADDR, LABEL, NUMBER)	\
do {								\
  int line_ = use_gnu_debug_info_extensions ? LINE : 0;		\
								\
  dbxout_int (CODE);						\
  fputs (",0,", asm_out_file);					\
  dbxout_int (line_);						\
  putc (',', asm_out_file);					\
  if (ADDR)							\
    output_addr_const (asm_out_file, ADDR);			\
  else if (LABEL)						\
    assemble_name (asm_out_file, LABEL);			\
  else								\
    dbxout_int (NUMBER);					\
  putc ('\n', asm_out_file);					\
} while (0)
#endif

/* Finish the emission of a complex .stabs directive.  When DBX_CONTIN_LENGTH
   is zero, this has only to emit the close quote and the remainder of
   the arguments.  When it is nonzero, the string has been marshalled in
   stabstr_ob, and this routine is responsible for breaking it up into
   DBX_CONTIN_LENGTH-sized chunks.

   SYM is the DECL of the symbol under consideration; it is used only
   for its DECL_SOURCE_LINE.  The other arguments are all passed directly
   to DBX_FINISH_STABS; see above for details.  */
   
static void
dbxout_finish_complex_stabs (tree sym, STAB_CODE_TYPE code,
			     rtx addr, const char *label, int number)
{
  int line ATTRIBUTE_UNUSED;
  char *str;
  size_t len;

  line = sym ? DECL_SOURCE_LINE (sym) : 0;
  if (DBX_CONTIN_LENGTH > 0)
    {
      char *chunk;
      size_t chunklen;

      /* Nul-terminate the growing string, then get its size and
	 address.  */
      obstack_1grow (&stabstr_ob, '\0');

      len = obstack_object_size (&stabstr_ob);
      chunk = str = XOBFINISH (&stabstr_ob, char *);

      /* Within the buffer are a sequence of NUL-separated strings,
	 each of which is to be written out as a separate stab
	 directive.  */
      for (;;)
	{
	  chunklen = strlen (chunk);
	  fwrite (chunk, 1, chunklen, asm_out_file);
	  fputs ("\",", asm_out_file);

	  /* Must add an extra byte to account for the NUL separator.  */
	  chunk += chunklen + 1;
	  len   -= chunklen + 1;

	  /* Only put a line number on the last stab in the sequence.  */
	  DBX_FINISH_STABS (sym, code, len == 0 ? line : 0,
			    addr, label, number);
	  if (len == 0)
	    break;

	  fputs (ASM_STABS_OP, asm_out_file);
	  putc ('"', asm_out_file);
	}
      stabstr_last_contin_point = 0;
    }
  else
    {
      /* No continuations - we can put the whole string out at once.
	 It is faster to augment the string with the close quote and
	 comma than to do a two-character fputs.  */
      obstack_grow (&stabstr_ob, "\",", 2);
      len = obstack_object_size (&stabstr_ob);
      str = XOBFINISH (&stabstr_ob, char *);
      
      fwrite (str, 1, len, asm_out_file);
      DBX_FINISH_STABS (sym, code, line, addr, label, number);
    }
  obstack_free (&stabstr_ob, str);
}

#if defined (DBX_DEBUGGING_INFO)

static void
dbxout_function_end (tree decl)
{
  char lscope_label_name[100];

  /* The Lscope label must be emitted even if we aren't doing anything
     else; dbxout_block needs it.  */
  switch_to_section (function_section (current_function_decl));
  
  /* Convert Lscope into the appropriate format for local labels in case
     the system doesn't insert underscores in front of user generated
     labels.  */
  ASM_GENERATE_INTERNAL_LABEL (lscope_label_name, "Lscope", scope_labelno);
  targetm.asm_out.internal_label (asm_out_file, "Lscope", scope_labelno);

  /* The N_FUN tag at the end of the function is a GNU extension,
     which may be undesirable, and is unnecessary if we do not have
     named sections.  */
  if (!use_gnu_debug_info_extensions
      || NO_DBX_FUNCTION_END
      || !targetm.have_named_sections
      || DECL_IGNORED_P (decl))
    return;

  /* By convention, GCC will mark the end of a function with an N_FUN
     symbol and an empty string.  */
  if (flag_reorder_blocks_and_partition)
    {
      dbxout_begin_empty_stabs (N_FUN);
      dbxout_stab_value_label_diff (cfun->hot_section_end_label, 
				    cfun->hot_section_label);
      dbxout_begin_empty_stabs (N_FUN);
      dbxout_stab_value_label_diff (cfun->cold_section_end_label, 
				    cfun->cold_section_label);
    }
  else
    {
      char begin_label[20];
      /* Reference current function start using LFBB.  */
      ASM_GENERATE_INTERNAL_LABEL (begin_label, "LFBB", scope_labelno);
      dbxout_begin_empty_stabs (N_FUN);
      dbxout_stab_value_label_diff (lscope_label_name, begin_label);
    }

  if (!NO_DBX_BNSYM_ENSYM && !flag_debug_only_used_symbols)
    dbxout_stabd (N_ENSYM, 0);
}
#endif /* DBX_DEBUGGING_INFO */

/* Get lang description for N_SO stab.  */
static unsigned int ATTRIBUTE_UNUSED
get_lang_number (void)
{
  const char *language_string = lang_hooks.name;

  if (strcmp (language_string, "GNU C") == 0)
    return N_SO_C;
  else if (strcmp (language_string, "GNU C++") == 0)
    return N_SO_CC;
  else if (strcmp (language_string, "GNU F77") == 0)
    return N_SO_FORTRAN;
  else if (strcmp (language_string, "GNU F95") == 0)
    return N_SO_FORTRAN90; /* CHECKME */
  else if (strcmp (language_string, "GNU Pascal") == 0)
    return N_SO_PASCAL;
  else if (strcmp (language_string, "GNU Objective-C") == 0)
    return N_SO_OBJC;
  else if (strcmp (language_string, "GNU Objective-C++") == 0)
    return N_SO_OBJCPLUS;
  else
    return 0;

}

/* At the beginning of compilation, start writing the symbol table.
   Initialize `typevec' and output the standard data types of C.  */

static void
dbxout_init (const char *input_file_name)
{
  char ltext_label_name[100];
  bool used_ltext_label_name = false;
  tree syms = lang_hooks.decls.getdecls ();

  typevec_len = 100;
  typevec = ggc_calloc (typevec_len, sizeof typevec[0]);

  /* stabstr_ob contains one string, which will be just fine with
     1-byte alignment.  */
  obstack_specify_allocation (&stabstr_ob, 0, 1, xmalloc, free);

  /* Convert Ltext into the appropriate format for local labels in case
     the system doesn't insert underscores in front of user generated
     labels.  */
  ASM_GENERATE_INTERNAL_LABEL (ltext_label_name, "Ltext", 0);

  /* Put the current working directory in an N_SO symbol.  */
  if (use_gnu_debug_info_extensions && !NO_DBX_MAIN_SOURCE_DIRECTORY)
    {
      static const char *cwd;

      if (!cwd)
	{
	  cwd = get_src_pwd ();
	  if (cwd[0] == '\0')
	    cwd = "/";
	  else if (!IS_DIR_SEPARATOR (cwd[strlen (cwd) - 1]))
	    cwd = concat (cwd, "/", NULL);
	}
#ifdef DBX_OUTPUT_MAIN_SOURCE_DIRECTORY
      DBX_OUTPUT_MAIN_SOURCE_DIRECTORY (asm_out_file, cwd);
#else /* no DBX_OUTPUT_MAIN_SOURCE_DIRECTORY */
      dbxout_begin_simple_stabs_desc (cwd, N_SO, get_lang_number ());
      dbxout_stab_value_label (ltext_label_name);
      used_ltext_label_name = true;
#endif /* no DBX_OUTPUT_MAIN_SOURCE_DIRECTORY */
    }

#ifdef DBX_OUTPUT_MAIN_SOURCE_FILENAME
  DBX_OUTPUT_MAIN_SOURCE_FILENAME (asm_out_file, input_file_name);
#else
  dbxout_begin_simple_stabs_desc (input_file_name, N_SO, get_lang_number ());
  dbxout_stab_value_label (ltext_label_name);
  used_ltext_label_name = true;
#endif

  if (used_ltext_label_name)
    {
      switch_to_section (text_section);
      targetm.asm_out.internal_label (asm_out_file, "Ltext", 0);
    }

  /* Emit an N_OPT stab to indicate that this file was compiled by GCC.
     The string used is historical.  */
#ifndef NO_DBX_GCC_MARKER
  dbxout_begin_simple_stabs ("gcc2_compiled.", N_OPT);
  dbxout_stab_value_zero ();
#endif

  base_input_file = lastfile = input_file_name;

  next_type_number = 1;

#ifdef DBX_USE_BINCL
  current_file = XNEW (struct dbx_file);
  current_file->next = NULL;
  current_file->file_number = 0;
  current_file->next_type_number = 1;
  next_file_number = 1;
  current_file->prev = NULL;
  current_file->bincl_status = BINCL_NOT_REQUIRED;
  current_file->pending_bincl_name = NULL;
#endif

  /* Get all permanent types that have typedef names, and output them
     all, except for those already output.  Some language front ends
     put these declarations in the top-level scope; some do not;
     the latter are responsible for calling debug_hooks->type_decl from
     their record_builtin_type function.  */
  dbxout_typedefs (syms);

  if (preinit_symbols)
    {
      tree t;
      for (t = nreverse (preinit_symbols); t; t = TREE_CHAIN (t))
	dbxout_symbol (TREE_VALUE (t), 0);
      preinit_symbols = 0;
    }
}

/* Output any typedef names for types described by TYPE_DECLs in SYMS.  */

static void
dbxout_typedefs (tree syms)
{
  for (; syms != NULL_TREE; syms = TREE_CHAIN (syms))
    {
      if (TREE_CODE (syms) == TYPE_DECL)
	{
	  tree type = TREE_TYPE (syms);
	  if (TYPE_NAME (type)
	      && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
	      && COMPLETE_OR_VOID_TYPE_P (type)
	      && ! TREE_ASM_WRITTEN (TYPE_NAME (type)))
	    dbxout_symbol (TYPE_NAME (type), 0);
	}
    }
}

#ifdef DBX_USE_BINCL
/* Emit BINCL stab using given name.  */
static void
emit_bincl_stab (const char *name)
{
  dbxout_begin_simple_stabs (name, N_BINCL);
  dbxout_stab_value_zero ();
}

/* If there are pending bincls then it is time to emit all of them.  */

static inline void
emit_pending_bincls_if_required (void)
{
  if (pending_bincls)
    emit_pending_bincls ();
}

/* Emit all pending bincls.  */

static void
emit_pending_bincls (void)
{
  struct dbx_file *f = current_file;

  /* Find first pending bincl.  */
  while (f->bincl_status == BINCL_PENDING)
    f = f->next;

  /* Now emit all bincls.  */
  f = f->prev;

  while (f)
    {
      if (f->bincl_status == BINCL_PENDING)
        {
          emit_bincl_stab (f->pending_bincl_name);

	  /* Update file number and status.  */
          f->file_number = next_file_number++;
          f->bincl_status = BINCL_PROCESSED;
        }
      if (f == current_file)
        break;
      f = f->prev;
    }

  /* All pending bincls have been emitted.  */
  pending_bincls = 0;
}

#else

static inline void
emit_pending_bincls_if_required (void) {}
#endif

/* Change to reading from a new source file.  Generate a N_BINCL stab.  */

static void
dbxout_start_source_file (unsigned int line ATTRIBUTE_UNUSED,
			  const char *filename ATTRIBUTE_UNUSED)
{
#ifdef DBX_USE_BINCL
  struct dbx_file *n = XNEW (struct dbx_file);

  n->next = current_file;
  n->next_type_number = 1;
  /* Do not assign file number now. 
     Delay it until we actually emit BINCL.  */
  n->file_number = 0;
  n->prev = NULL;
  current_file->prev = n;
  n->bincl_status = BINCL_PENDING;
  n->pending_bincl_name = filename;
  pending_bincls = 1;
  current_file = n;
#endif
}

/* Revert to reading a previous source file.  Generate a N_EINCL stab.  */

static void
dbxout_end_source_file (unsigned int line ATTRIBUTE_UNUSED)
{
#ifdef DBX_USE_BINCL
  /* Emit EINCL stab only if BINCL is not pending.  */
  if (current_file->bincl_status == BINCL_PROCESSED)
    {
      dbxout_begin_stabn (N_EINCL);
      dbxout_stab_value_zero ();
    }
  current_file->bincl_status = BINCL_NOT_REQUIRED;
  current_file = current_file->next;
#endif
}

/* Handle a few odd cases that occur when trying to make PCH files work.  */

static void
dbxout_handle_pch (unsigned at_end)
{
  if (! at_end)
    {
      /* When using the PCH, this file will be included, so we need to output
	 a BINCL.  */
      dbxout_start_source_file (0, lastfile);

      /* The base file when using the PCH won't be the same as
	 the base file when it's being generated.  */
      lastfile = NULL;
    }
  else
    {
      /* ... and an EINCL.  */
      dbxout_end_source_file (0);

      /* Deal with cases where 'lastfile' was never actually changed.  */
      lastfile_is_base = lastfile == NULL;
    }
}

#if defined (DBX_DEBUGGING_INFO)
/* Output debugging info to FILE to switch to sourcefile FILENAME.  */

static void
dbxout_source_file (const char *filename)
{
  if (lastfile == 0 && lastfile_is_base)
    {
      lastfile = base_input_file;
      lastfile_is_base = 0;
    }

  if (filename && (lastfile == 0 || strcmp (filename, lastfile)))
    {
      /* Don't change section amid function.  */
      if (current_function_decl == NULL_TREE)
	switch_to_section (text_section);

      dbxout_begin_simple_stabs (filename, N_SOL);
      dbxout_stab_value_internal_label ("Ltext", &source_label_number);
      lastfile = filename;
    }
}

/* Output N_BNSYM, line number symbol entry, and local symbol at 
   function scope  */

static void
dbxout_begin_prologue (unsigned int lineno, const char *filename)
{
  if (use_gnu_debug_info_extensions
      && !NO_DBX_FUNCTION_END
      && !NO_DBX_BNSYM_ENSYM
      && !flag_debug_only_used_symbols)
    dbxout_stabd (N_BNSYM, 0);

  /* pre-increment the scope counter */
  scope_labelno++;

  dbxout_source_line (lineno, filename);
  /* Output function begin block at function scope, referenced 
     by dbxout_block, dbxout_source_line and dbxout_function_end.  */
  emit_pending_bincls_if_required ();
  targetm.asm_out.internal_label (asm_out_file, "LFBB", scope_labelno);
}

/* Output a line number symbol entry for source file FILENAME and line
   number LINENO.  */

static void
dbxout_source_line (unsigned int lineno, const char *filename)
{
  dbxout_source_file (filename);

#ifdef DBX_OUTPUT_SOURCE_LINE
  DBX_OUTPUT_SOURCE_LINE (asm_out_file, lineno, dbxout_source_line_counter);
#else
  if (DBX_LINES_FUNCTION_RELATIVE)
    {
      char begin_label[20];
      dbxout_begin_stabn_sline (lineno);
      /* Reference current function start using LFBB.  */
      ASM_GENERATE_INTERNAL_LABEL (begin_label, "LFBB", scope_labelno); 
      dbxout_stab_value_internal_label_diff ("LM", &dbxout_source_line_counter,
					     begin_label);
    }
  else
    dbxout_stabd (N_SLINE, lineno);
#endif
}

/* Describe the beginning of an internal block within a function.  */

static void
dbxout_begin_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int n)
{
  emit_pending_bincls_if_required ();
  targetm.asm_out.internal_label (asm_out_file, "LBB", n);
}

/* Describe the end line-number of an internal block within a function.  */

static void
dbxout_end_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int n)
{
  emit_pending_bincls_if_required ();
  targetm.asm_out.internal_label (asm_out_file, "LBE", n);
}

/* Output dbx data for a function definition.
   This includes a definition of the function name itself (a symbol),
   definitions of the parameters (locating them in the parameter list)
   and then output the block that makes up the function's body
   (including all the auto variables of the function).  */

static void
dbxout_function_decl (tree decl)
{
  emit_pending_bincls_if_required ();
#ifndef DBX_FUNCTION_FIRST
  dbxout_begin_function (decl);
#endif
  dbxout_block (DECL_INITIAL (decl), 0, DECL_ARGUMENTS (decl));
  dbxout_function_end (decl);
}

#endif /* DBX_DEBUGGING_INFO  */

/* Debug information for a global DECL.  Called from toplev.c after
   compilation proper has finished.  */
static void
dbxout_global_decl (tree decl)
{
  if (TREE_CODE (decl) == VAR_DECL && !DECL_EXTERNAL (decl))
    {
      int saved_tree_used = TREE_USED (decl);
      TREE_USED (decl) = 1;
      dbxout_symbol (decl, 0);
      TREE_USED (decl) = saved_tree_used;
    }
}

/* This is just a function-type adapter; dbxout_symbol does exactly
   what we want but returns an int.  */
static void
dbxout_type_decl (tree decl, int local)
{
  dbxout_symbol (decl, local);
}

/* At the end of compilation, finish writing the symbol table.
   The default is to call debug_free_queue but do nothing else.  */

static void
dbxout_finish (const char *filename ATTRIBUTE_UNUSED)
{
#ifdef DBX_OUTPUT_MAIN_SOURCE_FILE_END
  DBX_OUTPUT_MAIN_SOURCE_FILE_END (asm_out_file, filename);
#elif defined DBX_OUTPUT_NULL_N_SO_AT_MAIN_SOURCE_FILE_END
 {
   switch_to_section (text_section);
   dbxout_begin_empty_stabs (N_SO);
   dbxout_stab_value_internal_label ("Letext", 0);
 }
#endif
  debug_free_queue ();
}

/* Output the index of a type.  */

static void
dbxout_type_index (tree type)
{
#ifndef DBX_USE_BINCL
  stabstr_D (TYPE_SYMTAB_ADDRESS (type));
#else
  struct typeinfo *t = &typevec[TYPE_SYMTAB_ADDRESS (type)];
  stabstr_C ('(');
  stabstr_D (t->file_number);
  stabstr_C (',');
  stabstr_D (t->type_number);
  stabstr_C (')');
#endif
}



/* Used in several places: evaluates to '0' for a private decl,
   '1' for a protected decl, '2' for a public decl.  */
#define DECL_ACCESSIBILITY_CHAR(DECL) \
(TREE_PRIVATE (DECL) ? '0' : TREE_PROTECTED (DECL) ? '1' : '2')

/* Subroutine of `dbxout_type'.  Output the type fields of TYPE.
   This must be a separate function because anonymous unions require
   recursive calls.  */

static void
dbxout_type_fields (tree type)
{
  tree tem;

  /* Output the name, type, position (in bits), size (in bits) of each
     field that we can support.  */
  for (tem = TYPE_FIELDS (type); tem; tem = TREE_CHAIN (tem))
    {
      /* If one of the nodes is an error_mark or its type is then
	 return early.  */
      if (tem == error_mark_node || TREE_TYPE (tem) == error_mark_node)
	return;

      /* Omit here local type decls until we know how to support them.  */
      if (TREE_CODE (tem) == TYPE_DECL
	  /* Omit here the nameless fields that are used to skip bits.  */
	  || DECL_IGNORED_P (tem)
	  /* Omit fields whose position or size are variable or too large to
	     represent.  */
	  || (TREE_CODE (tem) == FIELD_DECL
	      && (! host_integerp (bit_position (tem), 0)
		  || ! DECL_SIZE (tem)
		  || ! host_integerp (DECL_SIZE (tem), 1))))
	continue;

      else if (TREE_CODE (tem) != CONST_DECL)
	{
	  /* Continue the line if necessary,
	     but not before the first field.  */
	  if (tem != TYPE_FIELDS (type))
	    CONTIN;

	  if (DECL_NAME (tem))
	    stabstr_I (DECL_NAME (tem));
	  stabstr_C (':');

	  if (use_gnu_debug_info_extensions
	      && (TREE_PRIVATE (tem) || TREE_PROTECTED (tem)
		  || TREE_CODE (tem) != FIELD_DECL))
	    {
	      stabstr_C ('/');
	      stabstr_C (DECL_ACCESSIBILITY_CHAR (tem));
	    }

	  dbxout_type ((TREE_CODE (tem) == FIELD_DECL
			&& DECL_BIT_FIELD_TYPE (tem))
		       ? DECL_BIT_FIELD_TYPE (tem) : TREE_TYPE (tem), 0);

	  if (TREE_CODE (tem) == VAR_DECL)
	    {
	      if (TREE_STATIC (tem) && use_gnu_debug_info_extensions)
		{
		  tree name = DECL_ASSEMBLER_NAME (tem);

		  stabstr_C (':');
		  stabstr_I (name);
		  stabstr_C (';');
		}
	      else
		/* If TEM is non-static, GDB won't understand it.  */
		stabstr_S (",0,0;");
	    }
	  else
	    {
	      stabstr_C (',');
	      stabstr_D (int_bit_position (tem));
	      stabstr_C (',');
	      stabstr_D (tree_low_cst (DECL_SIZE (tem), 1));
	      stabstr_C (';');
	    }
	}
    }
}

/* Subroutine of `dbxout_type_methods'.  Output debug info about the
   method described DECL.  */

static void
dbxout_type_method_1 (tree decl)
{
  char c1 = 'A', c2;

  if (TREE_CODE (TREE_TYPE (decl)) == FUNCTION_TYPE)
    c2 = '?';
  else /* it's a METHOD_TYPE.  */
    {
      tree firstarg = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (decl)));
      /* A for normal functions.
	 B for `const' member functions.
	 C for `volatile' member functions.
	 D for `const volatile' member functions.  */
      if (TYPE_READONLY (TREE_TYPE (firstarg)))
	c1 += 1;
      if (TYPE_VOLATILE (TREE_TYPE (firstarg)))
	c1 += 2;

      if (DECL_VINDEX (decl))
	c2 = '*';
      else
	c2 = '.';
    }

  /* ??? Output the mangled name, which contains an encoding of the
     method's type signature.  May not be necessary anymore.  */
  stabstr_C (':');
  stabstr_I (DECL_ASSEMBLER_NAME (decl));
  stabstr_C (';');
  stabstr_C (DECL_ACCESSIBILITY_CHAR (decl));
  stabstr_C (c1);
  stabstr_C (c2);

  if (DECL_VINDEX (decl) && host_integerp (DECL_VINDEX (decl), 0))
    {
      stabstr_D (tree_low_cst (DECL_VINDEX (decl), 0));
      stabstr_C (';');
      dbxout_type (DECL_CONTEXT (decl), 0);
      stabstr_C (';');
    }
}

/* Subroutine of `dbxout_type'.  Output debug info about the methods defined
   in TYPE.  */

static void
dbxout_type_methods (tree type)
{
  /* C++: put out the method names and their parameter lists */
  tree methods = TYPE_METHODS (type);
  tree fndecl;
  tree last;

  if (methods == NULL_TREE)
    return;

  if (TREE_CODE (methods) != TREE_VEC)
    fndecl = methods;
  else if (TREE_VEC_ELT (methods, 0) != NULL_TREE)
    fndecl = TREE_VEC_ELT (methods, 0);
  else
    fndecl = TREE_VEC_ELT (methods, 1);

  while (fndecl)
    {
      int need_prefix = 1;

      /* Group together all the methods for the same operation.
	 These differ in the types of the arguments.  */
      for (last = NULL_TREE;
	   fndecl && (last == NULL_TREE || DECL_NAME (fndecl) == DECL_NAME (last));
	   fndecl = TREE_CHAIN (fndecl))
	/* Output the name of the field (after overloading), as
	   well as the name of the field before overloading, along
	   with its parameter list */
	{
	  /* Skip methods that aren't FUNCTION_DECLs.  (In C++, these
	     include TEMPLATE_DECLs.)  The debugger doesn't know what
	     to do with such entities anyhow.  */
	  if (TREE_CODE (fndecl) != FUNCTION_DECL)
	    continue;

	  CONTIN;

	  last = fndecl;

	  /* Also ignore abstract methods; those are only interesting to
	     the DWARF backends.  */
	  if (DECL_IGNORED_P (fndecl) || DECL_ABSTRACT (fndecl))
	    continue;

	  /* Redundantly output the plain name, since that's what gdb
	     expects.  */
	  if (need_prefix)
	    {
	      stabstr_I (DECL_NAME (fndecl));
	      stabstr_S ("::");
	      need_prefix = 0;
	    }

	  dbxout_type (TREE_TYPE (fndecl), 0);
	  dbxout_type_method_1 (fndecl);
	}
      if (!need_prefix)
	stabstr_C (';');
    }
}

/* Emit a "range" type specification, which has the form:
   "r<index type>;<lower bound>;<upper bound>;".
   TYPE is an INTEGER_TYPE.  */

static void
dbxout_range_type (tree type)
{
  stabstr_C ('r');
  if (TREE_TYPE (type))
    dbxout_type (TREE_TYPE (type), 0);
  else if (TREE_CODE (type) != INTEGER_TYPE)
    dbxout_type (type, 0); /* E.g. Pascal's ARRAY [BOOLEAN] of INTEGER */
  else
    {
      /* Traditionally, we made sure 'int' was type 1, and builtin types
	 were defined to be sub-ranges of int.  Unfortunately, this
	 does not allow us to distinguish true sub-ranges from integer
	 types.  So, instead we define integer (non-sub-range) types as
	 sub-ranges of themselves.  This matters for Chill.  If this isn't
	 a subrange type, then we want to define it in terms of itself.
	 However, in C, this may be an anonymous integer type, and we don't
	 want to emit debug info referring to it.  Just calling
	 dbxout_type_index won't work anyways, because the type hasn't been
	 defined yet.  We make this work for both cases by checked to see
	 whether this is a defined type, referring to it if it is, and using
	 'int' otherwise.  */
      if (TYPE_SYMTAB_ADDRESS (type) != 0)
	dbxout_type_index (type);
      else
	dbxout_type_index (integer_type_node);
    }

  stabstr_C (';');
  if (TYPE_MIN_VALUE (type) != 0
      && host_integerp (TYPE_MIN_VALUE (type), 0))
    {
      if (print_int_cst_bounds_in_octal_p (type))
        stabstr_O (TYPE_MIN_VALUE (type));
      else
        stabstr_D (tree_low_cst (TYPE_MIN_VALUE (type), 0));
    }
  else
    stabstr_C ('0');

  stabstr_C (';');
  if (TYPE_MAX_VALUE (type) != 0
      && host_integerp (TYPE_MAX_VALUE (type), 0))
    {
      if (print_int_cst_bounds_in_octal_p (type))
        stabstr_O (TYPE_MAX_VALUE (type));
      else
        stabstr_D (tree_low_cst (TYPE_MAX_VALUE (type), 0));
      stabstr_C (';');
    }
  else
    stabstr_S ("-1;");
}


/* Output a reference to a type.  If the type has not yet been
   described in the dbx output, output its definition now.
   For a type already defined, just refer to its definition
   using the type number.

   If FULL is nonzero, and the type has been described only with
   a forward-reference, output the definition now.
   If FULL is zero in this case, just refer to the forward-reference
   using the number previously allocated.  */

static void
dbxout_type (tree type, int full)
{
  tree tem;
  tree main_variant;
  static int anonymous_type_number = 0;
  bool vector_type = false;

  if (TREE_CODE (type) == VECTOR_TYPE)
    {
      /* The frontend feeds us a representation for the vector as a struct
	 containing an array.  Pull out the array type.  */
      type = TREE_TYPE (TYPE_FIELDS (TYPE_DEBUG_REPRESENTATION_TYPE (type)));
      vector_type = true;
    }

  /* If there was an input error and we don't really have a type,
     avoid crashing and write something that is at least valid
     by assuming `int'.  */
  if (type == error_mark_node)
    type = integer_type_node;
  else
    {
      if (TYPE_NAME (type)
	  && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
	  && TYPE_DECL_SUPPRESS_DEBUG (TYPE_NAME (type)))
	full = 0;
    }

  /* Try to find the "main variant" with the same name.  */
  if (TYPE_NAME (type) && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
      && DECL_ORIGINAL_TYPE (TYPE_NAME (type)))
    main_variant = TREE_TYPE (TYPE_NAME (type));
  else
    main_variant = TYPE_MAIN_VARIANT (type);

  /* If we are not using extensions, stabs does not distinguish const and
     volatile, so there is no need to make them separate types.  */
  if (!use_gnu_debug_info_extensions)
    type = main_variant;

  if (TYPE_SYMTAB_ADDRESS (type) == 0)
    {
      /* Type has no dbx number assigned.  Assign next available number.  */
      TYPE_SYMTAB_ADDRESS (type) = next_type_number++;

      /* Make sure type vector is long enough to record about this type.  */

      if (next_type_number == typevec_len)
	{
	  typevec
	    = ggc_realloc (typevec, (typevec_len * 2 * sizeof typevec[0]));
	  memset (typevec + typevec_len, 0, typevec_len * sizeof typevec[0]);
	  typevec_len *= 2;
	}

#ifdef DBX_USE_BINCL
      emit_pending_bincls_if_required ();
      typevec[TYPE_SYMTAB_ADDRESS (type)].file_number
	= current_file->file_number;
      typevec[TYPE_SYMTAB_ADDRESS (type)].type_number
	= current_file->next_type_number++;
#endif
    }

  if (flag_debug_only_used_symbols)
    {
      if ((TREE_CODE (type) == RECORD_TYPE
	   || TREE_CODE (type) == UNION_TYPE
	   || TREE_CODE (type) == QUAL_UNION_TYPE
	   || TREE_CODE (type) == ENUMERAL_TYPE)
	  && TYPE_STUB_DECL (type)
	  && DECL_P (TYPE_STUB_DECL (type))
	  && ! DECL_IGNORED_P (TYPE_STUB_DECL (type)))
	debug_queue_symbol (TYPE_STUB_DECL (type));
      else if (TYPE_NAME (type)
	       && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL)
	debug_queue_symbol (TYPE_NAME (type));
    }

  /* Output the number of this type, to refer to it.  */
  dbxout_type_index (type);

#ifdef DBX_TYPE_DEFINED
  if (DBX_TYPE_DEFINED (type))
    return;
#endif

  /* If this type's definition has been output or is now being output,
     that is all.  */

  switch (typevec[TYPE_SYMTAB_ADDRESS (type)].status)
    {
    case TYPE_UNSEEN:
      break;
    case TYPE_XREF:
      /* If we have already had a cross reference,
	 and either that's all we want or that's the best we could do,
	 don't repeat the cross reference.
	 Sun dbx crashes if we do.  */
      if (! full || !COMPLETE_TYPE_P (type)
	  /* No way in DBX fmt to describe a variable size.  */
	  || ! host_integerp (TYPE_SIZE (type), 1))
	return;
      break;
    case TYPE_DEFINED:
      return;
    }

#ifdef DBX_NO_XREFS
  /* For systems where dbx output does not allow the `=xsNAME:' syntax,
     leave the type-number completely undefined rather than output
     a cross-reference.  If we have already used GNU debug info extensions,
     then it is OK to output a cross reference.  This is necessary to get
     proper C++ debug output.  */
  if ((TREE_CODE (type) == RECORD_TYPE || TREE_CODE (type) == UNION_TYPE
       || TREE_CODE (type) == QUAL_UNION_TYPE
       || TREE_CODE (type) == ENUMERAL_TYPE)
      && ! use_gnu_debug_info_extensions)
    /* We must use the same test here as we use twice below when deciding
       whether to emit a cross-reference.  */
    if ((TYPE_NAME (type) != 0
	 && ! (TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
	       && DECL_IGNORED_P (TYPE_NAME (type)))
	 && !full)
	|| !COMPLETE_TYPE_P (type)
	/* No way in DBX fmt to describe a variable size.  */
	|| ! host_integerp (TYPE_SIZE (type), 1))
      {
	typevec[TYPE_SYMTAB_ADDRESS (type)].status = TYPE_XREF;
	return;
      }
#endif

  /* Output a definition now.  */
  stabstr_C ('=');

  /* Mark it as defined, so that if it is self-referent
     we will not get into an infinite recursion of definitions.  */

  typevec[TYPE_SYMTAB_ADDRESS (type)].status = TYPE_DEFINED;

  /* If this type is a variant of some other, hand off.  Types with
     different names are usefully distinguished.  We only distinguish
     cv-qualified types if we're using extensions.  */
  if (TYPE_READONLY (type) > TYPE_READONLY (main_variant))
    {
      stabstr_C ('k');
      dbxout_type (build_type_variant (type, 0, TYPE_VOLATILE (type)), 0);
      return;
    }
  else if (TYPE_VOLATILE (type) > TYPE_VOLATILE (main_variant))
    {
      stabstr_C ('B');
      dbxout_type (build_type_variant (type, TYPE_READONLY (type), 0), 0);
      return;
    }
  else if (main_variant != TYPE_MAIN_VARIANT (type))
    {
      if (flag_debug_only_used_symbols)
        {
          tree orig_type = DECL_ORIGINAL_TYPE (TYPE_NAME (type));

          if ((TREE_CODE (orig_type) == RECORD_TYPE
               || TREE_CODE (orig_type) == UNION_TYPE
               || TREE_CODE (orig_type) == QUAL_UNION_TYPE
               || TREE_CODE (orig_type) == ENUMERAL_TYPE)
              && TYPE_STUB_DECL (orig_type)
              && ! DECL_IGNORED_P (TYPE_STUB_DECL (orig_type)))
            debug_queue_symbol (TYPE_STUB_DECL (orig_type));
        }
      /* 'type' is a typedef; output the type it refers to.  */
      dbxout_type (DECL_ORIGINAL_TYPE (TYPE_NAME (type)), 0);
      return;
    }
  /* else continue.  */

  switch (TREE_CODE (type))
    {
    case VOID_TYPE:
    case LANG_TYPE:
      /* APPLE LOCAL blocks 6034272 */
    case BLOCK_POINTER_TYPE:
      /* For a void type, just define it as itself; i.e., "5=5".
	 This makes us consider it defined
	 without saying what it is.  The debugger will make it
	 a void type when the reference is seen, and nothing will
	 ever override that default.  */
      dbxout_type_index (type);
      break;

    case INTEGER_TYPE:
      if (type == char_type_node && ! TYPE_UNSIGNED (type))
	{
	  /* Output the type `char' as a subrange of itself!
	     I don't understand this definition, just copied it
	     from the output of pcc.
	     This used to use `r2' explicitly and we used to
	     take care to make sure that `char' was type number 2.  */
	  stabstr_C ('r');
	  dbxout_type_index (type);
	  stabstr_S (";0;127;");
	}

      /* If this is a subtype of another integer type, always prefer to
	 write it as a subtype.  */
      else if (TREE_TYPE (type) != 0
	       && TREE_CODE (TREE_TYPE (type)) == INTEGER_TYPE)
	{
	  /* If the size is non-standard, say what it is if we can use
	     GDB extensions.  */

	  if (use_gnu_debug_info_extensions
	      && TYPE_PRECISION (type) != TYPE_PRECISION (integer_type_node))
	    {
	      stabstr_S ("@s");
	      stabstr_D (TYPE_PRECISION (type));
	      stabstr_C (';');
	    }

	  dbxout_range_type (type);
	}

      else
	{
	  /* If the size is non-standard, say what it is if we can use
	     GDB extensions.  */

	  if (use_gnu_debug_info_extensions
	      && TYPE_PRECISION (type) != TYPE_PRECISION (integer_type_node))
	    {
	      stabstr_S ("@s");
	      stabstr_D (TYPE_PRECISION (type));
	      stabstr_C (';');
	    }

	  if (print_int_cst_bounds_in_octal_p (type))
	    {
	      stabstr_C ('r');

              /* If this type derives from another type, output type index of
		 parent type. This is particularly important when parent type
		 is an enumerated type, because not generating the parent type
		 index would transform the definition of this enumerated type
		 into a plain unsigned type.  */
              if (TREE_TYPE (type) != 0)
                dbxout_type_index (TREE_TYPE (type));
              else
                dbxout_type_index (type);

	      stabstr_C (';');
	      stabstr_O (TYPE_MIN_VALUE (type));
	      stabstr_C (';');
	      stabstr_O (TYPE_MAX_VALUE (type));
	      stabstr_C (';');
	    }

	  else
	    /* Output other integer types as subranges of `int'.  */
	    dbxout_range_type (type);
	}

      break;

    case REAL_TYPE:
      /* This used to say `r1' and we used to take care
	 to make sure that `int' was type number 1.  */
      stabstr_C ('r');
      dbxout_type_index (integer_type_node);
      stabstr_C (';');
      stabstr_D (int_size_in_bytes (type));
      stabstr_S (";0;");
      break;

    case BOOLEAN_TYPE:
      if (use_gnu_debug_info_extensions)
	{
	  stabstr_S ("@s");
	  stabstr_D (BITS_PER_UNIT * int_size_in_bytes (type));
	  stabstr_S (";-16;");
	}
      else /* Define as enumeral type (False, True) */
	stabstr_S ("eFalse:0,True:1,;");
      break;

    case COMPLEX_TYPE:
      /* Differs from the REAL_TYPE by its new data type number.
	 R3 is NF_COMPLEX.  We don't try to use any of the other NF_*
	 codes since gdb doesn't care anyway.  */

      if (TREE_CODE (TREE_TYPE (type)) == REAL_TYPE)
	{
	  stabstr_S ("R3;");
	  stabstr_D (2 * int_size_in_bytes (TREE_TYPE (type)));
	  stabstr_S (";0;");
	}
      else
	{
	  /* Output a complex integer type as a structure,
	     pending some other way to do it.  */
	  stabstr_C ('s');
	  stabstr_D (int_size_in_bytes (type));

	  stabstr_S ("real:");
	  dbxout_type (TREE_TYPE (type), 0);
	  stabstr_S (",0,");
	  stabstr_D (TYPE_PRECISION (TREE_TYPE (type)));

	  stabstr_S (";imag:");
	  dbxout_type (TREE_TYPE (type), 0);
	  stabstr_C (',');
	  stabstr_D (TYPE_PRECISION (TREE_TYPE (type)));
	  stabstr_C (',');
	  stabstr_D (TYPE_PRECISION (TREE_TYPE (type)));
	  stabstr_S (";;");
	}
      break;

    case ARRAY_TYPE:
      /* Make arrays of packed bits look like bitstrings for chill.  */
      if (TYPE_PACKED (type) && use_gnu_debug_info_extensions)
	{
	  stabstr_S ("@s");
	  stabstr_D (BITS_PER_UNIT * int_size_in_bytes (type));
	  stabstr_S (";@S;S");
	  dbxout_type (TYPE_DOMAIN (type), 0);
	  break;
	}

      if (use_gnu_debug_info_extensions && vector_type)
	stabstr_S ("@V;");

      /* Output "a" followed by a range type definition
	 for the index type of the array
	 followed by a reference to the target-type.
	 ar1;0;N;M for a C array of type M and size N+1.  */
      /* Check if a character string type, which in Chill is
	 different from an array of characters.  */
      if (TYPE_STRING_FLAG (type) && use_gnu_debug_info_extensions)
	{
	  stabstr_S ("@S;");
	}
      tem = TYPE_DOMAIN (type);
      if (tem == NULL)
	{
	  stabstr_S ("ar");
	  dbxout_type_index (integer_type_node);
	  stabstr_S (";0;-1;");
	}
      else
	{
	  stabstr_C ('a');
	  dbxout_range_type (tem);
	}

      dbxout_type (TREE_TYPE (type), 0);
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      {
	tree binfo = TYPE_BINFO (type);

	/* Output a structure type.  We must use the same test here as we
	   use in the DBX_NO_XREFS case above.  */
	if ((TYPE_NAME (type) != 0
	     && ! (TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
		   && DECL_IGNORED_P (TYPE_NAME (type)))
	     && !full)
	    || !COMPLETE_TYPE_P (type)
	    /* No way in DBX fmt to describe a variable size.  */
	    || ! host_integerp (TYPE_SIZE (type), 1))
	  {
	    /* If the type is just a cross reference, output one
	       and mark the type as partially described.
	       If it later becomes defined, we will output
	       its real definition.
	       If the type has a name, don't nest its definition within
	       another type's definition; instead, output an xref
	       and let the definition come when the name is defined.  */
	    stabstr_S ((TREE_CODE (type) == RECORD_TYPE) ? "xs" : "xu");
	    if (TYPE_NAME (type) != 0)
	      dbxout_type_name (type);
	    else
	      {
		stabstr_S ("$$");
		stabstr_D (anonymous_type_number++);
	      }

	    stabstr_C (':');
	    typevec[TYPE_SYMTAB_ADDRESS (type)].status = TYPE_XREF;
	    break;
	  }

	/* Identify record or union, and print its size.  */
	stabstr_C ((TREE_CODE (type) == RECORD_TYPE) ? 's' : 'u');
	stabstr_D (int_size_in_bytes (type));

	if (binfo)
	  {
	    int i;
	    tree child;
	    VEC(tree,gc) *accesses = BINFO_BASE_ACCESSES (binfo);
	    
	    if (use_gnu_debug_info_extensions)
	      {
		if (BINFO_N_BASE_BINFOS (binfo))
		  {
		    stabstr_C ('!');
		    stabstr_U (BINFO_N_BASE_BINFOS (binfo));
		    stabstr_C (',');
		  }
	      }
	    for (i = 0; BINFO_BASE_ITERATE (binfo, i, child); i++)
	      {
		tree access = (accesses ? VEC_index (tree, accesses, i)
			       : access_public_node);

		if (use_gnu_debug_info_extensions)
		  {
		    stabstr_C (BINFO_VIRTUAL_P (child) ? '1' : '0');
		    stabstr_C (access == access_public_node ? '2' :
				   access == access_protected_node
				   ? '1' :'0');
		    if (BINFO_VIRTUAL_P (child)
			&& (strcmp (lang_hooks.name, "GNU C++") == 0
			    || strcmp (lang_hooks.name, "GNU Objective-C++") == 0))
		      /* For a virtual base, print the (negative)
		     	 offset within the vtable where we must look
		     	 to find the necessary adjustment.  */
		      stabstr_D
			(tree_low_cst (BINFO_VPTR_FIELD (child), 0)
			 * BITS_PER_UNIT);
		    else
		      stabstr_D (tree_low_cst (BINFO_OFFSET (child), 0)
				       * BITS_PER_UNIT);
		    stabstr_C (',');
		    dbxout_type (BINFO_TYPE (child), 0);
		    stabstr_C (';');
		  }
		else
		  {
		    /* Print out the base class information with
		       fields which have the same names at the types
		       they hold.  */
		    dbxout_type_name (BINFO_TYPE (child));
		    stabstr_C (':');
		    dbxout_type (BINFO_TYPE (child), full);
		    stabstr_C (',');
		    stabstr_D (tree_low_cst (BINFO_OFFSET (child), 0)
				     * BITS_PER_UNIT);
		    stabstr_C (',');
		    stabstr_D
		      (tree_low_cst (TYPE_SIZE (BINFO_TYPE (child)), 0)
		       * BITS_PER_UNIT);
		    stabstr_C (';');
		  }
	      }
	  }
      }

      /* Write out the field declarations.  */
      dbxout_type_fields (type);
      if (use_gnu_debug_info_extensions && TYPE_METHODS (type) != NULL_TREE)
	{
	  dbxout_type_methods (type);
	}

      stabstr_C (';');

      if (use_gnu_debug_info_extensions && TREE_CODE (type) == RECORD_TYPE
	  /* Avoid the ~ if we don't really need it--it confuses dbx.  */
	  && TYPE_VFIELD (type))
	{

	  /* We need to write out info about what field this class
	     uses as its "main" vtable pointer field, because if this
	     field is inherited from a base class, GDB cannot necessarily
	     figure out which field it's using in time.  */
	  stabstr_S ("~%");
	  dbxout_type (DECL_FCONTEXT (TYPE_VFIELD (type)), 0);
	  stabstr_C (';');
	}
      break;

    case ENUMERAL_TYPE:
      /* We must use the same test here as we use in the DBX_NO_XREFS case
	 above.  We simplify it a bit since an enum will never have a variable
	 size.  */
      if ((TYPE_NAME (type) != 0
	   && ! (TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
		 && DECL_IGNORED_P (TYPE_NAME (type)))
	   && !full)
	  || !COMPLETE_TYPE_P (type))
	{
	  stabstr_S ("xe");
	  dbxout_type_name (type);
	  typevec[TYPE_SYMTAB_ADDRESS (type)].status = TYPE_XREF;
	  stabstr_C (':');
	  return;
	}
      if (use_gnu_debug_info_extensions
	  && TYPE_PRECISION (type) != TYPE_PRECISION (integer_type_node))
	{
	  stabstr_S ("@s");
	  stabstr_D (TYPE_PRECISION (type));
	  stabstr_C (';');
	}

      stabstr_C ('e');
      for (tem = TYPE_VALUES (type); tem; tem = TREE_CHAIN (tem))
	{
	  stabstr_I (TREE_PURPOSE (tem));
	  stabstr_C (':');

	  if (TREE_INT_CST_HIGH (TREE_VALUE (tem)) == 0)
	    stabstr_D (TREE_INT_CST_LOW (TREE_VALUE (tem)));
	  else if (TREE_INT_CST_HIGH (TREE_VALUE (tem)) == -1
		   && (HOST_WIDE_INT) TREE_INT_CST_LOW (TREE_VALUE (tem)) < 0)
	    stabstr_D (TREE_INT_CST_LOW (TREE_VALUE (tem)));
	  else
	    stabstr_O (TREE_VALUE (tem));

	  stabstr_C (',');
	  if (TREE_CHAIN (tem) != 0)
	    CONTIN;
	}

      stabstr_C (';');
      break;

    case POINTER_TYPE:
      stabstr_C ('*');
      dbxout_type (TREE_TYPE (type), 0);
      break;

    case METHOD_TYPE:
      if (use_gnu_debug_info_extensions)
	{
	  stabstr_C ('#');

	  /* Write the argument types out longhand.  */
	  dbxout_type (TYPE_METHOD_BASETYPE (type), 0);
	  stabstr_C (',');
	  dbxout_type (TREE_TYPE (type), 0);
	  dbxout_args (TYPE_ARG_TYPES (type));
	  stabstr_C (';');
	}
      else
	/* Treat it as a function type.  */
	dbxout_type (TREE_TYPE (type), 0);
      break;

    case OFFSET_TYPE:
      if (use_gnu_debug_info_extensions)
	{
	  stabstr_C ('@');
	  dbxout_type (TYPE_OFFSET_BASETYPE (type), 0);
	  stabstr_C (',');
	  dbxout_type (TREE_TYPE (type), 0);
	}
      else
	/* Should print as an int, because it is really just an offset.  */
	dbxout_type (integer_type_node, 0);
      break;

    case REFERENCE_TYPE:
      if (use_gnu_debug_info_extensions)
	{
	  stabstr_C ('&');
	}
      else
	stabstr_C ('*');
      dbxout_type (TREE_TYPE (type), 0);
      break;

    case FUNCTION_TYPE:
      stabstr_C ('f');
      dbxout_type (TREE_TYPE (type), 0);
      break;

    default:
      gcc_unreachable ();
    }
}

/* Return nonzero if the given type represents an integer whose bounds
   should be printed in octal format.  */

static bool
print_int_cst_bounds_in_octal_p (tree type)
{
  /* If we can use GDB extensions and the size is wider than a long
     (the size used by GDB to read them) or we may have trouble writing
     the bounds the usual way, write them in octal.  Note the test is for
     the *target's* size of "long", not that of the host.  The host test
     is just to make sure we can write it out in case the host wide int
     is narrower than the target "long".

     For unsigned types, we use octal if they are the same size or larger.
     This is because we print the bounds as signed decimal, and hence they
     can't span same size unsigned types.  */

  if (use_gnu_debug_info_extensions
      && TYPE_MIN_VALUE (type) != 0
      && TREE_CODE (TYPE_MIN_VALUE (type)) == INTEGER_CST
      && TYPE_MAX_VALUE (type) != 0
      && TREE_CODE (TYPE_MAX_VALUE (type)) == INTEGER_CST
      && (TYPE_PRECISION (type) > TYPE_PRECISION (integer_type_node)
	  || ((TYPE_PRECISION (type) == TYPE_PRECISION (integer_type_node))
	      && TYPE_UNSIGNED (type))
	  || TYPE_PRECISION (type) > HOST_BITS_PER_WIDE_INT
	  || (TYPE_PRECISION (type) == HOST_BITS_PER_WIDE_INT
	      && TYPE_UNSIGNED (type))))
    return TRUE;
  else
    return FALSE;
}

/* Output the name of type TYPE, with no punctuation.
   Such names can be set up either by typedef declarations
   or by struct, enum and union tags.  */

static void
dbxout_type_name (tree type)
{
  tree t = TYPE_NAME (type);
  
  gcc_assert (t);
  switch (TREE_CODE (t))
    {
    case IDENTIFIER_NODE:
      break;
    case TYPE_DECL:
      t = DECL_NAME (t);
      break;
    default:
      gcc_unreachable ();
    }

  stabstr_I (t);
}

/* Output leading leading struct or class names needed for qualifying
   type whose scope is limited to a struct or class.  */

static void
dbxout_class_name_qualifiers (tree decl)
{
  tree context = decl_type_context (decl);

  if (context != NULL_TREE
      && TREE_CODE(context) == RECORD_TYPE
      && TYPE_NAME (context) != 0
      && (TREE_CODE (TYPE_NAME (context)) == IDENTIFIER_NODE
          || (DECL_NAME (TYPE_NAME (context)) != 0)))
    {
      tree name = TYPE_NAME (context);

      if (TREE_CODE (name) == TYPE_DECL)
	{
	  dbxout_class_name_qualifiers (name);
	  name = DECL_NAME (name);
	}
      stabstr_I (name);
      stabstr_S ("::");
    }
}

/* This is a specialized subset of expand_expr for use by dbxout_symbol in
   evaluating DECL_VALUE_EXPR.  In particular, we stop if we find decls that
   havn't been expanded, or if the expression is getting so complex we won't
   be able to represent it in stabs anyway.  Returns NULL on failure.  */

static rtx
dbxout_expand_expr (tree expr)
{
  switch (TREE_CODE (expr))
    {
    case VAR_DECL:
    case PARM_DECL:
      if (DECL_HAS_VALUE_EXPR_P (expr))
	return dbxout_expand_expr (DECL_VALUE_EXPR (expr));
      /* FALLTHRU */

    case CONST_DECL:
    case RESULT_DECL:
      return DECL_RTL_IF_SET (expr);

    case INTEGER_CST:
      return expand_expr (expr, NULL_RTX, VOIDmode, EXPAND_INITIALIZER);

    case COMPONENT_REF:
    case ARRAY_REF:
    case ARRAY_RANGE_REF:
    case BIT_FIELD_REF:
      {
	enum machine_mode mode;
	HOST_WIDE_INT bitsize, bitpos;
	tree offset, tem;
	int volatilep = 0, unsignedp = 0;
	rtx x;

	tem = get_inner_reference (expr, &bitsize, &bitpos, &offset,
				   &mode, &unsignedp, &volatilep, true);

	x = dbxout_expand_expr (tem);
	if (x == NULL || !MEM_P (x))
	  return NULL;
	if (offset != NULL)
	  {
	    if (!host_integerp (offset, 0))
	      return NULL;
	    x = adjust_address_nv (x, mode, tree_low_cst (offset, 0));
	  }
	if (bitpos != 0)
	  x = adjust_address_nv (x, mode, bitpos / BITS_PER_UNIT);

	return x;
      }

    default:
      return NULL;
    }
}

/* Helper function for output_used_types.  Queue one entry from the
   used types hash to be output.  */

static int
output_used_types_helper (void **slot, void *data)
{
  tree type = *slot;
  VEC(tree, heap) **types_p = data;

  if ((TREE_CODE (type) == RECORD_TYPE
       || TREE_CODE (type) == UNION_TYPE
       || TREE_CODE (type) == QUAL_UNION_TYPE
       || TREE_CODE (type) == ENUMERAL_TYPE)
      && TYPE_STUB_DECL (type)
      && DECL_P (TYPE_STUB_DECL (type))
      && ! DECL_IGNORED_P (TYPE_STUB_DECL (type)))
    VEC_quick_push (tree, *types_p, TYPE_STUB_DECL (type));
  else if (TYPE_NAME (type)
	   && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL)
    VEC_quick_push (tree, *types_p, TYPE_NAME (type));

  return 1;
}

/* This is a qsort callback which sorts types and declarations into a
   predictable order (types, then declarations, sorted by UID
   within).  */

static int
output_types_sort (const void *pa, const void *pb)
{
  const tree lhs = *((const tree *)pa);
  const tree rhs = *((const tree *)pb);

  if (TYPE_P (lhs))
    {
      if (TYPE_P (rhs))
	return TYPE_UID (lhs) - TYPE_UID (rhs);
      else
	return 1;
    }
  else
    {
      if (TYPE_P (rhs))
	return -1;
      else
	return DECL_UID (lhs) - DECL_UID (rhs);
    }
}


/* Force all types used by this function to be output in debug
   information.  */

static void
output_used_types (void)
{
  if (cfun && cfun->used_types_hash)
    {
      VEC(tree, heap) *types;
      int i;
      tree type;

      types = VEC_alloc (tree, heap, htab_elements (cfun->used_types_hash));
      htab_traverse (cfun->used_types_hash, output_used_types_helper, &types);

      /* Sort by UID to prevent dependence on hash table ordering.  */
      qsort (VEC_address (tree, types), VEC_length (tree, types),
	     sizeof (tree), output_types_sort);

      for (i = 0; VEC_iterate (tree, types, i, type); i++)
	debug_queue_symbol (type);

      VEC_free (tree, heap, types);
    }
}

/* Output a .stabs for the symbol defined by DECL,
   which must be a ..._DECL node in the normal namespace.
   It may be a CONST_DECL, a FUNCTION_DECL, a PARM_DECL or a VAR_DECL.
   LOCAL is nonzero if the scope is less than the entire file.
   Return 1 if a stabs might have been emitted.  */

int
dbxout_symbol (tree decl, int local ATTRIBUTE_UNUSED)
{
  tree type = TREE_TYPE (decl);
  tree context = NULL_TREE;
  int result = 0;
  rtx decl_rtl;

  /* "Intercept" dbxout_symbol() calls like we do all debug_hooks.  */
  ++debug_nesting;

  /* Ignore nameless syms, but don't ignore type tags.  */

  if ((DECL_NAME (decl) == 0 && TREE_CODE (decl) != TYPE_DECL)
      || DECL_IGNORED_P (decl))
    DBXOUT_DECR_NESTING_AND_RETURN (0);

  /* If we are to generate only the symbols actually used then such
     symbol nodes are flagged with TREE_USED.  Ignore any that
     aren't flaged as TREE_USED.  */

  if (flag_debug_only_used_symbols
      && (!TREE_USED (decl)
          && (TREE_CODE (decl) != VAR_DECL || !DECL_INITIAL (decl))))
    DBXOUT_DECR_NESTING_AND_RETURN (0);

  /* If dbxout_init has not yet run, queue this symbol for later.  */
  if (!typevec)
    {
      preinit_symbols = tree_cons (0, decl, preinit_symbols);
      DBXOUT_DECR_NESTING_AND_RETURN (0);
    }

  if (flag_debug_only_used_symbols)
    {
      tree t;

      /* We now have a used symbol.  We need to generate the info for
         the symbol's type in addition to the symbol itself.  These
         type symbols are queued to be generated after were done with
         the symbol itself (otherwise they would fight over the
         stabstr obstack).

         Note, because the TREE_TYPE(type) might be something like a
         pointer to a named type we need to look for the first name
         we see following the TREE_TYPE chain.  */

      t = type;
      while (POINTER_TYPE_P (t))
        t = TREE_TYPE (t);

      /* RECORD_TYPE, UNION_TYPE, QUAL_UNION_TYPE, and ENUMERAL_TYPE
         need special treatment.  The TYPE_STUB_DECL field in these
         types generally represents the tag name type we want to
         output.  In addition there  could be a typedef type with
         a different name.  In that case we also want to output
         that.  */

      if (TREE_CODE (t) == RECORD_TYPE
           || TREE_CODE (t) == UNION_TYPE
           || TREE_CODE (t) == QUAL_UNION_TYPE
           || TREE_CODE (t) == ENUMERAL_TYPE)
        {
	    if (TYPE_STUB_DECL (t)
		&& TYPE_STUB_DECL (t) != decl
		&& DECL_P (TYPE_STUB_DECL (t))
		&& ! DECL_IGNORED_P (TYPE_STUB_DECL (t)))
	    {
	      debug_queue_symbol (TYPE_STUB_DECL (t));
	      if (TYPE_NAME (t)
		  && TYPE_NAME (t) != TYPE_STUB_DECL (t)
		  && TYPE_NAME (t) != decl
		  && DECL_P (TYPE_NAME (t)))
		debug_queue_symbol (TYPE_NAME (t));
	    }
	}
      else if (TYPE_NAME (t)
	       && TYPE_NAME (t) != decl
	       && DECL_P (TYPE_NAME (t)))
        debug_queue_symbol (TYPE_NAME (t));
    }

  emit_pending_bincls_if_required ();

  switch (TREE_CODE (decl))
    {
    case CONST_DECL:
      /* Enum values are defined by defining the enum type.  */
      break;

    case FUNCTION_DECL:
      decl_rtl = DECL_RTL_IF_SET (decl);
      if (!decl_rtl)
	DBXOUT_DECR_NESTING_AND_RETURN (0);
      if (DECL_EXTERNAL (decl))
	break;
      /* Don't mention a nested function under its parent.  */
      context = decl_function_context (decl);
      if (context == current_function_decl)
	break;
      /* Don't mention an inline instance of a nested function.  */
      if (context && DECL_FROM_INLINE (decl))
	break;
      if (!MEM_P (decl_rtl)
	  || GET_CODE (XEXP (decl_rtl, 0)) != SYMBOL_REF)
	break;

      if (flag_debug_only_used_symbols)
	output_used_types ();

      dbxout_begin_complex_stabs ();
      stabstr_I (DECL_ASSEMBLER_NAME (decl));
      stabstr_S (TREE_PUBLIC (decl) ? ":F" : ":f");
      result = 1;

      if (TREE_TYPE (type))
	dbxout_type (TREE_TYPE (type), 0);
      else
	dbxout_type (void_type_node, 0);

      /* For a nested function, when that function is compiled,
	 mention the containing function name
	 as well as (since dbx wants it) our own assembler-name.  */
      if (context != 0)
	{
	  stabstr_C (',');
	  stabstr_I (DECL_ASSEMBLER_NAME (decl));
	  stabstr_C (',');
	  stabstr_I (DECL_NAME (context));
	}

      dbxout_finish_complex_stabs (decl, N_FUN, XEXP (decl_rtl, 0), 0, 0);
      break;

    case TYPE_DECL:
      /* Don't output the same typedef twice.
         And don't output what language-specific stuff doesn't want output.  */
      if (TREE_ASM_WRITTEN (decl) || TYPE_DECL_SUPPRESS_DEBUG (decl))
	DBXOUT_DECR_NESTING_AND_RETURN (0);

      /* Don't output typedefs for types with magic type numbers (XCOFF).  */
#ifdef DBX_ASSIGN_FUNDAMENTAL_TYPE_NUMBER
      {
	int fundamental_type_number =
	  DBX_ASSIGN_FUNDAMENTAL_TYPE_NUMBER (decl);

	if (fundamental_type_number != 0)
	  {
	    TREE_ASM_WRITTEN (decl) = 1;
	    TYPE_SYMTAB_ADDRESS (TREE_TYPE (decl)) = fundamental_type_number;
	    DBXOUT_DECR_NESTING_AND_RETURN (0);
	  }
      }
#endif
      FORCE_TEXT;
      result = 1;
      {
	int tag_needed = 1;
	int did_output = 0;

	if (DECL_NAME (decl))
	  {
	    /* Nonzero means we must output a tag as well as a typedef.  */
	    tag_needed = 0;

	    /* Handle the case of a C++ structure or union
	       where the TYPE_NAME is a TYPE_DECL
	       which gives both a typedef name and a tag.  */
	    /* dbx requires the tag first and the typedef second.  */
	    if ((TREE_CODE (type) == RECORD_TYPE
		 || TREE_CODE (type) == UNION_TYPE
		 || TREE_CODE (type) == QUAL_UNION_TYPE)
		&& TYPE_NAME (type) == decl
		&& !use_gnu_debug_info_extensions
		&& !TREE_ASM_WRITTEN (TYPE_NAME (type))
		/* Distinguish the implicit typedefs of C++
		   from explicit ones that might be found in C.  */
		&& DECL_ARTIFICIAL (decl)
                /* Do not generate a tag for incomplete records.  */
                && COMPLETE_TYPE_P (type)
		/* Do not generate a tag for records of variable size,
		   since this type can not be properly described in the
		   DBX format, and it confuses some tools such as objdump.  */
		&& host_integerp (TYPE_SIZE (type), 1))
	      {
		tree name = TYPE_NAME (type);
		if (TREE_CODE (name) == TYPE_DECL)
		  name = DECL_NAME (name);

		dbxout_begin_complex_stabs ();
		stabstr_I (name);
		stabstr_S (":T");
		dbxout_type (type, 1);
		dbxout_finish_complex_stabs (0, DBX_TYPE_DECL_STABS_CODE,
					     0, 0, 0);
	      }

	    dbxout_begin_complex_stabs ();

	    /* Output leading class/struct qualifiers.  */
	    if (use_gnu_debug_info_extensions)
	      dbxout_class_name_qualifiers (decl);

	    /* Output typedef name.  */
	    stabstr_I (DECL_NAME (decl));
	    stabstr_C (':');

	    /* Short cut way to output a tag also.  */
	    if ((TREE_CODE (type) == RECORD_TYPE
		 || TREE_CODE (type) == UNION_TYPE
		 || TREE_CODE (type) == QUAL_UNION_TYPE)
		&& TYPE_NAME (type) == decl
		/* Distinguish the implicit typedefs of C++
		   from explicit ones that might be found in C.  */
		&& DECL_ARTIFICIAL (decl))
	      {
		if (use_gnu_debug_info_extensions)
		  {
		    stabstr_C ('T');
		    TREE_ASM_WRITTEN (TYPE_NAME (type)) = 1;
		  }
	      }

	    stabstr_C ('t');
	    dbxout_type (type, 1);
	    dbxout_finish_complex_stabs (decl, DBX_TYPE_DECL_STABS_CODE,
					 0, 0, 0);
	    did_output = 1;
	  }

	/* Don't output a tag if this is an incomplete type.  This prevents
	   the sun4 Sun OS 4.x dbx from crashing.  */

	if (tag_needed && TYPE_NAME (type) != 0
	    && (TREE_CODE (TYPE_NAME (type)) == IDENTIFIER_NODE
		|| (DECL_NAME (TYPE_NAME (type)) != 0))
	    && COMPLETE_TYPE_P (type)
	    && !TREE_ASM_WRITTEN (TYPE_NAME (type)))
	  {
	    /* For a TYPE_DECL with no name, but the type has a name,
	       output a tag.
	       This is what represents `struct foo' with no typedef.  */
	    /* In C++, the name of a type is the corresponding typedef.
	       In C, it is an IDENTIFIER_NODE.  */
	    tree name = TYPE_NAME (type);
	    if (TREE_CODE (name) == TYPE_DECL)
	      name = DECL_NAME (name);

	    dbxout_begin_complex_stabs ();
	    stabstr_I (name);
	    stabstr_S (":T");
	    dbxout_type (type, 1);
	    dbxout_finish_complex_stabs (0, DBX_TYPE_DECL_STABS_CODE, 0, 0, 0);
	    did_output = 1;
	  }

	/* If an enum type has no name, it cannot be referred to, but
	   we must output it anyway, to record the enumeration
	   constants.  */

	if (!did_output && TREE_CODE (type) == ENUMERAL_TYPE)
	  {
	    dbxout_begin_complex_stabs ();
	    /* Some debuggers fail when given NULL names, so give this a
	       harmless name of " " (Why not "(anon)"?).  */
	    stabstr_S (" :T");
	    dbxout_type (type, 1);
	    dbxout_finish_complex_stabs (0, DBX_TYPE_DECL_STABS_CODE, 0, 0, 0);
	  }

	/* Prevent duplicate output of a typedef.  */
	TREE_ASM_WRITTEN (decl) = 1;
	break;
      }

    case PARM_DECL:
      /* Parm decls go in their own separate chains
	 and are output by dbxout_reg_parms and dbxout_parms.  */
      gcc_unreachable ();

    case RESULT_DECL:
    case VAR_DECL:
      /* Don't mention a variable that is external.
	 Let the file that defines it describe it.  */
      if (DECL_EXTERNAL (decl))
	break;

      /* If the variable is really a constant
	 and not written in memory, inform the debugger.

	 ??? Why do we skip emitting the type and location in this case?  */
      if (TREE_STATIC (decl) && TREE_READONLY (decl)
	  && DECL_INITIAL (decl) != 0
	  && host_integerp (DECL_INITIAL (decl), 0)
	  && ! TREE_ASM_WRITTEN (decl)
	  && (DECL_CONTEXT (decl) == NULL_TREE
	      || TREE_CODE (DECL_CONTEXT (decl)) == BLOCK
	      || TREE_CODE (DECL_CONTEXT (decl)) == NAMESPACE_DECL)
	  && TREE_PUBLIC (decl) == 0)
	{
	  /* The sun4 assembler does not grok this.  */

	  if (TREE_CODE (TREE_TYPE (decl)) == INTEGER_TYPE
	      || TREE_CODE (TREE_TYPE (decl)) == ENUMERAL_TYPE)
	    {
	      HOST_WIDE_INT ival = TREE_INT_CST_LOW (DECL_INITIAL (decl));

	      dbxout_begin_complex_stabs ();
	      dbxout_symbol_name (decl, NULL, 'c');
	      stabstr_S ("=i");
	      stabstr_D (ival);
	      dbxout_finish_complex_stabs (0, N_LSYM, 0, 0, 0);
	      DBXOUT_DECR_NESTING;
	      return 1;
	    }
	  else
	    break;
	}
      /* else it is something we handle like a normal variable.  */

      decl_rtl = dbxout_expand_expr (decl);
      if (!decl_rtl)
	DBXOUT_DECR_NESTING_AND_RETURN (0);

      decl_rtl = eliminate_regs (decl_rtl, 0, NULL_RTX);
#ifdef LEAF_REG_REMAP
      if (current_function_uses_only_leaf_regs)
	leaf_renumber_regs_insn (decl_rtl);
#endif

      result = dbxout_symbol_location (decl, type, 0, decl_rtl);
      break;

    default:
      break;
    }
  DBXOUT_DECR_NESTING;
  return result;
}

/* Output the stab for DECL, a VAR_DECL, RESULT_DECL or PARM_DECL.
   Add SUFFIX to its name, if SUFFIX is not 0.
   Describe the variable as residing in HOME
   (usually HOME is DECL_RTL (DECL), but not always).
   Returns 1 if the stab was really emitted.  */

static int
dbxout_symbol_location (tree decl, tree type, const char *suffix, rtx home)
{
  int letter = 0;
  STAB_CODE_TYPE code;
  rtx addr = 0;
  int number = 0;
  int regno = -1;

  /* Don't mention a variable at all
     if it was completely optimized into nothingness.

     If the decl was from an inline function, then its rtl
     is not identically the rtl that was used in this
     particular compilation.  */
  if (GET_CODE (home) == SUBREG)
    {
      rtx value = home;

      while (GET_CODE (value) == SUBREG)
	value = SUBREG_REG (value);
      if (REG_P (value))
	{
	  if (REGNO (value) >= FIRST_PSEUDO_REGISTER)
	    return 0;
	}
      home = alter_subreg (&home);
    }
  if (REG_P (home))
    {
      regno = REGNO (home);
      if (regno >= FIRST_PSEUDO_REGISTER)
	return 0;
    }

  /* The kind-of-variable letter depends on where
     the variable is and on the scope of its name:
     G and N_GSYM for static storage and global scope,
     S for static storage and file scope,
     V for static storage and local scope,
     for those two, use N_LCSYM if data is in bss segment,
     N_STSYM if in data segment, N_FUN otherwise.
     (We used N_FUN originally, then changed to N_STSYM
     to please GDB.  However, it seems that confused ld.
     Now GDB has been fixed to like N_FUN, says Kingdon.)
     no letter at all, and N_LSYM, for auto variable,
     r and N_RSYM for register variable.  */

  if (MEM_P (home) && GET_CODE (XEXP (home, 0)) == SYMBOL_REF)
    {
      if (TREE_PUBLIC (decl))
	{
	  letter = 'G';
	  code = N_GSYM;
	}
      else
	{
	  addr = XEXP (home, 0);

	  letter = decl_function_context (decl) ? 'V' : 'S';

	  /* Some ports can transform a symbol ref into a label ref,
	     because the symbol ref is too far away and has to be
	     dumped into a constant pool.  Alternatively, the symbol
	     in the constant pool might be referenced by a different
	     symbol.  */
	  if (GET_CODE (addr) == SYMBOL_REF
	      && CONSTANT_POOL_ADDRESS_P (addr))
	    {
	      bool marked;
	      rtx tmp = get_pool_constant_mark (addr, &marked);

	      if (GET_CODE (tmp) == SYMBOL_REF)
		{
		  addr = tmp;
		  if (CONSTANT_POOL_ADDRESS_P (addr))
		    get_pool_constant_mark (addr, &marked);
		  else
		    marked = true;
		}
	      else if (GET_CODE (tmp) == LABEL_REF)
		{
		  addr = tmp;
		  marked = true;
		}

	      /* If all references to the constant pool were optimized
		 out, we just ignore the symbol.  */
	      if (!marked)
		return 0;
	    }

	  /* This should be the same condition as in assemble_variable, but
	     we don't have access to dont_output_data here.  So, instead,
	     we rely on the fact that error_mark_node initializers always
	     end up in bss for C++ and never end up in bss for C.  */
	  if (DECL_INITIAL (decl) == 0
	      || (!strcmp (lang_hooks.name, "GNU C++")
		  && DECL_INITIAL (decl) == error_mark_node))
	    code = N_LCSYM;
	  else if (DECL_IN_TEXT_SECTION (decl))
	    /* This is not quite right, but it's the closest
	       of all the codes that Unix defines.  */
	    code = DBX_STATIC_CONST_VAR_CODE;
	  else
	    {
	      /* Ultrix `as' seems to need this.  */
#ifdef DBX_STATIC_STAB_DATA_SECTION
	      switch_to_section (data_section);
#endif
	      code = N_STSYM;
	    }
	}
    }
  else if (regno >= 0)
    {
      letter = 'r';
      code = N_RSYM;
      number = DBX_REGISTER_NUMBER (regno);
    }
  else if (MEM_P (home)
	   && (MEM_P (XEXP (home, 0))
	       || (REG_P (XEXP (home, 0))
		   && REGNO (XEXP (home, 0)) != HARD_FRAME_POINTER_REGNUM
		   && REGNO (XEXP (home, 0)) != STACK_POINTER_REGNUM
#if ARG_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
		   && REGNO (XEXP (home, 0)) != ARG_POINTER_REGNUM
#endif
		   )))
    /* If the value is indirect by memory or by a register
       that isn't the frame pointer
       then it means the object is variable-sized and address through
       that register or stack slot.  DBX has no way to represent this
       so all we can do is output the variable as a pointer.
       If it's not a parameter, ignore it.  */
    {
      if (REG_P (XEXP (home, 0)))
	{
	  letter = 'r';
	  code = N_RSYM;
	  if (REGNO (XEXP (home, 0)) >= FIRST_PSEUDO_REGISTER)
	    return 0;
	  number = DBX_REGISTER_NUMBER (REGNO (XEXP (home, 0)));
	}
      else
	{
	  code = N_LSYM;
	  /* RTL looks like (MEM (MEM (PLUS (REG...) (CONST_INT...)))).
	     We want the value of that CONST_INT.  */
	  number = DEBUGGER_AUTO_OFFSET (XEXP (XEXP (home, 0), 0));
	}

      /* Effectively do build_pointer_type, but don't cache this type,
	 since it might be temporary whereas the type it points to
	 might have been saved for inlining.  */
      /* Don't use REFERENCE_TYPE because dbx can't handle that.  */
      type = make_node (POINTER_TYPE);
      TREE_TYPE (type) = TREE_TYPE (decl);
    }
  else if (MEM_P (home)
	   && REG_P (XEXP (home, 0)))
    {
      code = N_LSYM;
      number = DEBUGGER_AUTO_OFFSET (XEXP (home, 0));
    }
  else if (MEM_P (home)
	   && GET_CODE (XEXP (home, 0)) == PLUS
	   && GET_CODE (XEXP (XEXP (home, 0), 1)) == CONST_INT)
    {
      code = N_LSYM;
      /* RTL looks like (MEM (PLUS (REG...) (CONST_INT...)))
	 We want the value of that CONST_INT.  */
      number = DEBUGGER_AUTO_OFFSET (XEXP (home, 0));
    }
  else if (MEM_P (home)
	   && GET_CODE (XEXP (home, 0)) == CONST)
    {
      /* Handle an obscure case which can arise when optimizing and
	 when there are few available registers.  (This is *always*
	 the case for i386/i486 targets).  The RTL looks like
	 (MEM (CONST ...)) even though this variable is a local `auto'
	 or a local `register' variable.  In effect, what has happened
	 is that the reload pass has seen that all assignments and
	 references for one such a local variable can be replaced by
	 equivalent assignments and references to some static storage
	 variable, thereby avoiding the need for a register.  In such
	 cases we're forced to lie to debuggers and tell them that
	 this variable was itself `static'.  */
      code = N_LCSYM;
      letter = 'V';
      addr = XEXP (XEXP (home, 0), 0);
    }
  else if (GET_CODE (home) == CONCAT)
    {
      tree subtype;

      /* If TYPE is not a COMPLEX_TYPE (it might be a RECORD_TYPE,
	 for example), then there is no easy way to figure out
	 what SUBTYPE should be.  So, we give up.  */
      if (TREE_CODE (type) != COMPLEX_TYPE)
	return 0;

      subtype = TREE_TYPE (type);

      /* If the variable's storage is in two parts,
	 output each as a separate stab with a modified name.  */
      if (WORDS_BIG_ENDIAN)
	dbxout_symbol_location (decl, subtype, "$imag", XEXP (home, 0));
      else
	dbxout_symbol_location (decl, subtype, "$real", XEXP (home, 0));

      if (WORDS_BIG_ENDIAN)
	dbxout_symbol_location (decl, subtype, "$real", XEXP (home, 1));
      else
	dbxout_symbol_location (decl, subtype, "$imag", XEXP (home, 1));
      return 1;
    }
  else
    /* Address might be a MEM, when DECL is a variable-sized object.
       Or it might be const0_rtx, meaning previous passes
       want us to ignore this variable.  */
    return 0;

  /* Ok, start a symtab entry and output the variable name.  */
  emit_pending_bincls_if_required ();
  FORCE_TEXT;

#ifdef DBX_STATIC_BLOCK_START
  DBX_STATIC_BLOCK_START (asm_out_file, code);
#endif

  dbxout_begin_complex_stabs_noforcetext ();
  dbxout_symbol_name (decl, suffix, letter);
  dbxout_type (type, 0);
  dbxout_finish_complex_stabs (decl, code, addr, 0, number);

#ifdef DBX_STATIC_BLOCK_END
  DBX_STATIC_BLOCK_END (asm_out_file, code);
#endif
  return 1;
}

/* Output the symbol name of DECL for a stabs, with suffix SUFFIX.
   Then output LETTER to indicate the kind of location the symbol has.  */

static void
dbxout_symbol_name (tree decl, const char *suffix, int letter)
{
  tree name;

  if (DECL_CONTEXT (decl) 
      && (TYPE_P (DECL_CONTEXT (decl))
	  || TREE_CODE (DECL_CONTEXT (decl)) == NAMESPACE_DECL))
    /* One slight hitch: if this is a VAR_DECL which is a class member
       or a namespace member, we must put out the mangled name instead of the
       DECL_NAME.  Note also that static member (variable) names DO NOT begin
       with underscores in .stabs directives.  */
    name = DECL_ASSEMBLER_NAME (decl);
  else
    /* ...but if we're function-local, we don't want to include the junk
       added by ASM_FORMAT_PRIVATE_NAME.  */
    name = DECL_NAME (decl);

  if (name)
    stabstr_I (name);
  else
    stabstr_S ("(anon)");

  if (suffix)
    stabstr_S (suffix);
  stabstr_C (':');
  if (letter)
    stabstr_C (letter);
}

/* Output definitions of all the decls in a chain. Return nonzero if
   anything was output */

int
dbxout_syms (tree syms)
{
  int result = 0;
  while (syms)
    {
      result += dbxout_symbol (syms, 1);
      syms = TREE_CHAIN (syms);
    }
  return result;
}

/* The following two functions output definitions of function parameters.
   Each parameter gets a definition locating it in the parameter list.
   Each parameter that is a register variable gets a second definition
   locating it in the register.

   Printing or argument lists in gdb uses the definitions that
   locate in the parameter list.  But reference to the variable in
   expressions uses preferentially the definition as a register.  */

/* Output definitions, referring to storage in the parmlist,
   of all the parms in PARMS, which is a chain of PARM_DECL nodes.  */

void
dbxout_parms (tree parms)
{
  ++debug_nesting;
  emit_pending_bincls_if_required ();

  for (; parms; parms = TREE_CHAIN (parms))
    if (DECL_NAME (parms)
	&& TREE_TYPE (parms) != error_mark_node
	&& DECL_RTL_SET_P (parms)
	&& DECL_INCOMING_RTL (parms))
      {
	tree eff_type;
	char letter;
	STAB_CODE_TYPE code;
	int number;

	/* Perform any necessary register eliminations on the parameter's rtl,
	   so that the debugging output will be accurate.  */
	DECL_INCOMING_RTL (parms)
	  = eliminate_regs (DECL_INCOMING_RTL (parms), 0, NULL_RTX);
	SET_DECL_RTL (parms, eliminate_regs (DECL_RTL (parms), 0, NULL_RTX));
#ifdef LEAF_REG_REMAP
	if (current_function_uses_only_leaf_regs)
	  {
	    leaf_renumber_regs_insn (DECL_INCOMING_RTL (parms));
	    leaf_renumber_regs_insn (DECL_RTL (parms));
	  }
#endif

	if (PARM_PASSED_IN_MEMORY (parms))
	  {
	    rtx inrtl = XEXP (DECL_INCOMING_RTL (parms), 0);

	    /* ??? Here we assume that the parm address is indexed
	       off the frame pointer or arg pointer.
	       If that is not true, we produce meaningless results,
	       but do not crash.  */
	    if (GET_CODE (inrtl) == PLUS
		&& GET_CODE (XEXP (inrtl, 1)) == CONST_INT)
	      number = INTVAL (XEXP (inrtl, 1));
	    else
	      number = 0;

	    code = N_PSYM;
	    number = DEBUGGER_ARG_OFFSET (number, inrtl);
	    letter = 'p';

	    /* It is quite tempting to use TREE_TYPE (parms) instead
	       of DECL_ARG_TYPE (parms) for the eff_type, so that gcc
	       reports the actual type of the parameter, rather than
	       the promoted type.  This certainly makes GDB's life
	       easier, at least for some ports.  The change is a bad
	       idea however, since GDB expects to be able access the
	       type without performing any conversions.  So for
	       example, if we were passing a float to an unprototyped
	       function, gcc will store a double on the stack, but if
	       we emit a stab saying the type is a float, then gdb
	       will only read in a single value, and this will produce
	       an erroneous value.  */
	    eff_type = DECL_ARG_TYPE (parms);
	  }
	else if (REG_P (DECL_RTL (parms)))
	  {
	    rtx best_rtl;

	    /* Parm passed in registers and lives in registers or nowhere.  */
	    code = DBX_REGPARM_STABS_CODE;
	    letter = DBX_REGPARM_STABS_LETTER;

	    /* For parms passed in registers, it is better to use the
	       declared type of the variable, not the type it arrived in.  */
	    eff_type = TREE_TYPE (parms);

	    /* If parm lives in a register, use that register; pretend
	       the parm was passed there.  It would be more consistent
	       to describe the register where the parm was passed, but
	       in practice that register usually holds something else.
	       If the parm lives nowhere, use the register where it
	       was passed.  */
	    if (REGNO (DECL_RTL (parms)) < FIRST_PSEUDO_REGISTER)
	      best_rtl = DECL_RTL (parms);
	    else
	      best_rtl = DECL_INCOMING_RTL (parms);

	    number = DBX_REGISTER_NUMBER (REGNO (best_rtl));
	  }
	else if (MEM_P (DECL_RTL (parms))
		 && REG_P (XEXP (DECL_RTL (parms), 0))
		 && REGNO (XEXP (DECL_RTL (parms), 0)) != HARD_FRAME_POINTER_REGNUM
		 && REGNO (XEXP (DECL_RTL (parms), 0)) != STACK_POINTER_REGNUM
#if ARG_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
		 && REGNO (XEXP (DECL_RTL (parms), 0)) != ARG_POINTER_REGNUM
#endif
		 )
	  {
	    /* Parm was passed via invisible reference.
	       That is, its address was passed in a register.
	       Output it as if it lived in that register.
	       The debugger will know from the type
	       that it was actually passed by invisible reference.  */

	    code = DBX_REGPARM_STABS_CODE;
 
	    /* GDB likes this marked with a special letter.  */
	    letter = (use_gnu_debug_info_extensions
		      ? 'a' : DBX_REGPARM_STABS_LETTER);
	    eff_type = TREE_TYPE (parms);

	    /* DECL_RTL looks like (MEM (REG...).  Get the register number.
	       If it is an unallocated pseudo-reg, then use the register where
	       it was passed instead.
	       ??? Why is DBX_REGISTER_NUMBER not used here?  */

	    if (REGNO (XEXP (DECL_RTL (parms), 0)) < FIRST_PSEUDO_REGISTER)
	      number = REGNO (XEXP (DECL_RTL (parms), 0));
	    else
	      number = REGNO (DECL_INCOMING_RTL (parms));
	  }
	else if (MEM_P (DECL_RTL (parms))
		 && MEM_P (XEXP (DECL_RTL (parms), 0)))
	  {
	    /* Parm was passed via invisible reference, with the reference
	       living on the stack.  DECL_RTL looks like
	       (MEM (MEM (PLUS (REG ...) (CONST_INT ...)))) or it
	       could look like (MEM (MEM (REG))).  */

	    code = N_PSYM;
	    letter = 'v';
	    eff_type = TREE_TYPE (parms);

	    if (!REG_P (XEXP (XEXP (DECL_RTL (parms), 0), 0)))
	      number = INTVAL (XEXP (XEXP (XEXP (DECL_RTL (parms), 0), 0), 1));
	    else
	      number = 0;

	    number = DEBUGGER_ARG_OFFSET (number,
					  XEXP (XEXP (DECL_RTL (parms), 0), 0));
	  }
	else if (MEM_P (DECL_RTL (parms))
		 && XEXP (DECL_RTL (parms), 0) != const0_rtx
		 /* ??? A constant address for a parm can happen
		    when the reg it lives in is equiv to a constant in memory.
		    Should make this not happen, after 2.4.  */
		 && ! CONSTANT_P (XEXP (DECL_RTL (parms), 0)))
	  {
	    /* Parm was passed in registers but lives on the stack.  */

	    code = N_PSYM;
	    letter = 'p';
	    eff_type = TREE_TYPE (parms);

	    /* DECL_RTL looks like (MEM (PLUS (REG...) (CONST_INT...))),
	       in which case we want the value of that CONST_INT,
	       or (MEM (REG ...)),
	       in which case we use a value of zero.  */
	    if (!REG_P (XEXP (DECL_RTL (parms), 0)))
	      number = INTVAL (XEXP (XEXP (DECL_RTL (parms), 0), 1));
	    else
	      number = 0;

	    /* Make a big endian correction if the mode of the type of the
	       parameter is not the same as the mode of the rtl.  */
	    if (BYTES_BIG_ENDIAN
		&& TYPE_MODE (TREE_TYPE (parms)) != GET_MODE (DECL_RTL (parms))
		&& GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (parms))) < UNITS_PER_WORD)
	      number += (GET_MODE_SIZE (GET_MODE (DECL_RTL (parms)))
			 - GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (parms))));
	  }
	else
	  /* ??? We don't know how to represent this argument.  */
	  continue;

	dbxout_begin_complex_stabs ();
	    
	if (DECL_NAME (parms))
	  {
	    stabstr_I (DECL_NAME (parms));
	    stabstr_C (':');
	  }
	else
	  stabstr_S ("(anon):");
	stabstr_C (letter);
	dbxout_type (eff_type, 0);
	dbxout_finish_complex_stabs (parms, code, 0, 0, number);
      }
  DBXOUT_DECR_NESTING;
}

/* Output definitions for the places where parms live during the function,
   when different from where they were passed, when the parms were passed
   in memory.

   It is not useful to do this for parms passed in registers
   that live during the function in different registers, because it is
   impossible to look in the passed register for the passed value,
   so we use the within-the-function register to begin with.

   PARMS is a chain of PARM_DECL nodes.  */

void
dbxout_reg_parms (tree parms)
{
  ++debug_nesting;

  for (; parms; parms = TREE_CHAIN (parms))
    if (DECL_NAME (parms) && PARM_PASSED_IN_MEMORY (parms))
      {
	/* Report parms that live in registers during the function
	   but were passed in memory.  */
	if (REG_P (DECL_RTL (parms))
	    && REGNO (DECL_RTL (parms)) < FIRST_PSEUDO_REGISTER)
	  dbxout_symbol_location (parms, TREE_TYPE (parms),
				  0, DECL_RTL (parms));
	else if (GET_CODE (DECL_RTL (parms)) == CONCAT)
	  dbxout_symbol_location (parms, TREE_TYPE (parms),
				  0, DECL_RTL (parms));
	/* Report parms that live in memory but not where they were passed.  */
	else if (MEM_P (DECL_RTL (parms))
		 && ! rtx_equal_p (DECL_RTL (parms), DECL_INCOMING_RTL (parms)))
	  dbxout_symbol_location (parms, TREE_TYPE (parms),
				  0, DECL_RTL (parms));
      }
  DBXOUT_DECR_NESTING;
}

/* Given a chain of ..._TYPE nodes (as come in a parameter list),
   output definitions of those names, in raw form */

static void
dbxout_args (tree args)
{
  while (args)
    {
      stabstr_C (',');
      dbxout_type (TREE_VALUE (args), 0);
      args = TREE_CHAIN (args);
    }
}

/* Subroutine of dbxout_block.  Emit an N_LBRAC stab referencing LABEL.
   BEGIN_LABEL is the name of the beginning of the function, which may
   be required.  */
static void
dbx_output_lbrac (const char *label,
		  const char *begin_label ATTRIBUTE_UNUSED)
{
  dbxout_begin_stabn (N_LBRAC);
  if (DBX_BLOCKS_FUNCTION_RELATIVE)
    dbxout_stab_value_label_diff (label, begin_label);
  else
    dbxout_stab_value_label (label);
}

/* Subroutine of dbxout_block.  Emit an N_RBRAC stab referencing LABEL.
   BEGIN_LABEL is the name of the beginning of the function, which may
   be required.  */
static void
dbx_output_rbrac (const char *label,
		  const char *begin_label ATTRIBUTE_UNUSED)
{
  dbxout_begin_stabn (N_RBRAC);
  if (DBX_BLOCKS_FUNCTION_RELATIVE)
    dbxout_stab_value_label_diff (label, begin_label);
  else
    dbxout_stab_value_label (label);
}

/* Output everything about a symbol block (a BLOCK node
   that represents a scope level),
   including recursive output of contained blocks.

   BLOCK is the BLOCK node.
   DEPTH is its depth within containing symbol blocks.
   ARGS is usually zero; but for the outermost block of the
   body of a function, it is a chain of PARM_DECLs for the function parameters.
   We output definitions of all the register parms
   as if they were local variables of that block.

   If -g1 was used, we count blocks just the same, but output nothing
   except for the outermost block.

   Actually, BLOCK may be several blocks chained together.
   We handle them all in sequence.  */

static void
dbxout_block (tree block, int depth, tree args)
{
  char begin_label[20];
  /* Reference current function start using LFBB.  */
  ASM_GENERATE_INTERNAL_LABEL (begin_label, "LFBB", scope_labelno);

  while (block)
    {
      /* Ignore blocks never expanded or otherwise marked as real.  */
      if (TREE_USED (block) && TREE_ASM_WRITTEN (block))
	{
	  int did_output;
	  int blocknum = BLOCK_NUMBER (block);

	  /* In dbx format, the syms of a block come before the N_LBRAC.
	     If nothing is output, we don't need the N_LBRAC, either.  */
	  did_output = 0;
	  if (debug_info_level != DINFO_LEVEL_TERSE || depth == 0)
	    did_output = dbxout_syms (BLOCK_VARS (block));
	  if (args)
	    dbxout_reg_parms (args);

	  /* Now output an N_LBRAC symbol to represent the beginning of
	     the block.  Use the block's tree-walk order to generate
	     the assembler symbols LBBn and LBEn
	     that final will define around the code in this block.  */
	  if (did_output)
	    {
	      char buf[20];
	      const char *scope_start;

	      if (depth == 0)
		/* The outermost block doesn't get LBB labels; use
		   the LFBB local symbol emitted by dbxout_begin_prologue.  */
		scope_start = begin_label;
	      else
		{
		  ASM_GENERATE_INTERNAL_LABEL (buf, "LBB", blocknum);
		  scope_start = buf;
		}

	      if (BLOCK_HANDLER_BLOCK (block))
		{
		  /* A catch block.  Must precede N_LBRAC.  */
		  tree decl = BLOCK_VARS (block);
		  while (decl)
		    {
		      dbxout_begin_complex_stabs ();
		      stabstr_I (DECL_NAME (decl));
		      stabstr_S (":C1");
		      dbxout_finish_complex_stabs (0, N_CATCH, 0,
						   scope_start, 0);
		      decl = TREE_CHAIN (decl);
		    }
		}
	      dbx_output_lbrac (scope_start, begin_label);
	    }

	  /* Output the subblocks.  */
	  dbxout_block (BLOCK_SUBBLOCKS (block), depth + 1, NULL_TREE);

	  /* Refer to the marker for the end of the block.  */
	  if (did_output)
	    {
	      char buf[100];
	      if (depth == 0)
		/* The outermost block doesn't get LBE labels;
		   use the "scope" label which will be emitted
		   by dbxout_function_end.  */
		ASM_GENERATE_INTERNAL_LABEL (buf, "Lscope", scope_labelno);
	      else
		ASM_GENERATE_INTERNAL_LABEL (buf, "LBE", blocknum);

	      dbx_output_rbrac (buf, begin_label);
	    }
	}
      block = BLOCK_CHAIN (block);
    }
}

/* Output the information about a function and its arguments and result.
   Usually this follows the function's code,
   but on some systems, it comes before.  */

#if defined (DBX_DEBUGGING_INFO)
static void
dbxout_begin_function (tree decl)
{
  int saved_tree_used1;

  if (DECL_IGNORED_P (decl))
    return;

  saved_tree_used1 = TREE_USED (decl);
  TREE_USED (decl) = 1;
  if (DECL_NAME (DECL_RESULT (decl)) != 0)
    {
      int saved_tree_used2 = TREE_USED (DECL_RESULT (decl));
      TREE_USED (DECL_RESULT (decl)) = 1;
      dbxout_symbol (decl, 0);
      TREE_USED (DECL_RESULT (decl)) = saved_tree_used2;
    }
  else
    dbxout_symbol (decl, 0);
  TREE_USED (decl) = saved_tree_used1;

  dbxout_parms (DECL_ARGUMENTS (decl));
  if (DECL_NAME (DECL_RESULT (decl)) != 0)
    dbxout_symbol (DECL_RESULT (decl), 1);
}
#endif /* DBX_DEBUGGING_INFO */

#endif /* DBX_DEBUGGING_INFO || XCOFF_DEBUGGING_INFO */

#include "gt-dbxout.h"
