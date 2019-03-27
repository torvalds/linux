/* read.h - of read.c
   Copyright 1986, 1990, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

extern char *input_line_pointer;	/* -> char we are parsing now.  */

/* Define to make whitespace be allowed in many syntactically
   unnecessary places.  Normally undefined.  For compatibility with
   ancient GNU cc.  */
/* #undef PERMIT_WHITESPACE */
#define PERMIT_WHITESPACE

#ifdef PERMIT_WHITESPACE
#define SKIP_WHITESPACE()			\
  do { if (*input_line_pointer == ' ') ++input_line_pointer; } while (0)
#else
#define SKIP_WHITESPACE() know(*input_line_pointer != ' ' )
#endif

#define	LEX_NAME	(1)	/* may continue a name */
#define LEX_BEGIN_NAME	(2)	/* may begin a name */
#define LEX_END_NAME	(4)	/* ends a name */

#define is_name_beginner(c) \
  ( lex_type[(unsigned char) (c)] & LEX_BEGIN_NAME )
#define is_part_of_name(c) \
  ( lex_type[(unsigned char) (c)] & LEX_NAME       )
#define is_name_ender(c) \
  ( lex_type[(unsigned char) (c)] & LEX_END_NAME   )

#ifndef is_a_char
#define CHAR_MASK	(0xff)
#define NOT_A_CHAR	(CHAR_MASK+1)
#define is_a_char(c)	(((unsigned) (c)) <= CHAR_MASK)
#endif /* is_a_char() */

extern char lex_type[];
extern char is_end_of_line[];

extern int is_it_end_of_statement (void);
extern char *find_end_of_line (char *, int);

extern int target_big_endian;

/* These are initialized by the CPU specific target files (tc-*.c).  */
extern const char comment_chars[];
extern const char line_comment_chars[];
extern const char line_separator_chars[];

/* Table of -I directories.  */
extern char **include_dirs;
extern int include_dir_count;
extern int include_dir_maxlen;

/* The offset in the absolute section.  */
extern addressT abs_section_offset;

/* The label on a line, used by some of the pseudo-ops.  */
extern symbolS *line_label;

/* This is used to support MRI common sections.  */
extern symbolS *mri_common_symbol;

/* True if a stabs line debug statement is currently being emitted.  */
extern int outputting_stabs_line_debug;

/* Possible arguments to .linkonce.  */
enum linkonce_type {
  LINKONCE_UNSET = 0,
  LINKONCE_DISCARD,
  LINKONCE_ONE_ONLY,
  LINKONCE_SAME_SIZE,
  LINKONCE_SAME_CONTENTS
};

#ifndef TC_CASE_SENSITIVE
extern char original_case_string[];
#endif

extern void pop_insert (const pseudo_typeS *);
extern unsigned int get_stab_string_offset
  (const char *string, const char *stabstr_secname);
extern void aout_process_stab (int, const char *, int, int, int);
extern char *demand_copy_string (int *lenP);
extern char *demand_copy_C_string (int *len_pointer);
extern char get_absolute_expression_and_terminator (long *val_pointer);
extern offsetT get_absolute_expression (void);
extern unsigned int next_char_of_string (void);
extern void s_mri_sect (char *);
extern char *mri_comment_field (char *);
extern void mri_comment_end (char *, int);
extern void add_include_dir (char *path);
extern void cons (int nbytes);
extern void demand_empty_rest_of_line (void);
extern void emit_expr (expressionS *exp, unsigned int nbytes);
extern void equals (char *sym_name, int reassign);
extern void float_cons (int float_type);
extern void ignore_rest_of_line (void);
#define discard_rest_of_line ignore_rest_of_line
extern int output_leb128 (char *, valueT, int sign);
extern void pseudo_set (symbolS * symbolP);
extern void read_a_source_file (char *name);
extern void read_begin (void);
extern void read_print_statistics (FILE *);
extern int sizeof_leb128 (valueT, int sign);
extern void stabs_generate_asm_file (void);
extern void stabs_generate_asm_lineno (void);
extern void stabs_generate_asm_func (const char *, const char *);
extern void stabs_generate_asm_endfunc (const char *, const char *);
extern void do_repeat (int,const char *,const char *);
extern void end_repeat (int);
extern void do_parse_cons_expression (expressionS *, int);

extern void generate_lineno_debug (void);

extern void s_abort (int) ATTRIBUTE_NORETURN;
extern void s_align_bytes (int arg);
extern void s_align_ptwo (int);
extern void bss_alloc (symbolS *, addressT, int);
extern offsetT parse_align (int);
extern symbolS *s_comm_internal (int, symbolS *(*) (int, symbolS *, addressT));
extern symbolS *s_lcomm_internal (int, symbolS *, addressT);
extern void s_app_file_string (char *, int);
extern void s_app_file (int);
extern void s_app_line (int);
extern void s_comm (int);
extern void s_data (int);
extern void s_desc (int);
extern void s_else (int arg);
extern void s_elseif (int arg);
extern void s_end (int arg);
extern void s_endif (int arg);
extern void s_err (int);
extern void s_errwarn (int);
extern void s_fail (int);
extern void s_fill (int);
extern void s_float_space (int mult);
extern void s_func (int);
extern void s_globl (int arg);
extern void s_if (int arg);
extern void s_ifb (int arg);
extern void s_ifc (int arg);
extern void s_ifdef (int arg);
extern void s_ifeqs (int arg);
extern void s_ignore (int arg);
extern void s_include (int arg);
extern void s_irp (int arg);
extern void s_lcomm (int needs_align);
extern void s_lcomm_bytes (int needs_align);
extern void s_leb128 (int sign);
extern void s_linkonce (int);
extern void s_lsym (int);
extern void s_macro (int);
extern void s_mexit (int);
extern void s_mri (int);
extern void s_mri_common (int);
extern void s_org (int);
extern void s_print (int);
extern void s_purgem (int);
extern void s_rept (int);
extern void s_set (int);
extern void s_space (int mult);
extern void s_stab (int what);
extern void s_struct (int);
extern void s_text (int);
extern void stringer (int append_zero);
extern void s_xstab (int what);
extern void s_rva (int);
extern void s_incbin (int);
extern void s_vendor_attribute (int);
extern void s_weakref (int);
