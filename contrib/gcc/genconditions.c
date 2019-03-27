/* Process machine description and calculate constant conditions.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* In a machine description, all of the insn patterns - define_insn,
   define_expand, define_split, define_peephole, define_peephole2 -
   contain an optional C expression which makes the final decision
   about whether or not this pattern is usable.  That expression may
   turn out to be always false when the compiler is built.  If it is,
   most of the programs that generate code from the machine
   description can simply ignore the entire pattern.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "hashtab.h"
#include "gensupport.h"

/* so we can include except.h in the generated file.  */
static int saw_eh_return;

static void write_header	(void);
static void write_conditions	(void);
static int write_one_condition	(void **, void *);

/* Generate the header for insn-conditions.c.  */

static void
write_header (void)
{
  puts ("\
/* Generated automatically by the program `genconditions' from the target\n\
   machine description file.  */\n\
\n\
#include \"bconfig.h\"\n\
#include \"system.h\"\n\
\n\
/* It is necessary, but not entirely safe, to include the headers below\n\
   in a generator program.  As a defensive measure, don't do so when the\n\
   table isn't going to have anything in it.  */\n\
#if GCC_VERSION >= 3001\n\
\n\
/* Do not allow checking to confuse the issue.  */\n\
#undef ENABLE_CHECKING\n\
#undef ENABLE_TREE_CHECKING\n\
#undef ENABLE_RTL_CHECKING\n\
#undef ENABLE_RTL_FLAG_CHECKING\n\
#undef ENABLE_GC_CHECKING\n\
#undef ENABLE_GC_ALWAYS_COLLECT\n\
\n\
#include \"coretypes.h\"\n\
#include \"tm.h\"\n\
#include \"insn-constants.h\"\n\
#include \"rtl.h\"\n\
#include \"tm_p.h\"\n\
#include \"function.h\"\n\
\n\
/* Fake - insn-config.h doesn't exist yet.  */\n\
#define MAX_RECOG_OPERANDS 10\n\
#define MAX_DUP_OPERANDS 10\n\
#define MAX_INSNS_PER_SPLIT 5\n\
\n\
#include \"regs.h\"\n\
#include \"recog.h\"\n\
#include \"real.h\"\n\
#include \"output.h\"\n\
#include \"flags.h\"\n\
#include \"hard-reg-set.h\"\n\
#include \"resource.h\"\n\
#include \"toplev.h\"\n\
#include \"reload.h\"\n\
#include \"tm-constrs.h\"\n");

  if (saw_eh_return)
    puts ("#define HAVE_eh_return 1");
  puts ("#include \"except.h\"\n");

  puts ("\
/* Dummy external declarations.  */\n\
extern rtx insn;\n\
extern rtx ins1;\n\
extern rtx operands[];\n\
\n\
#endif /* gcc >= 3.0.1 */\n");
}

/* Write out one entry in the conditions table, using the data pointed
   to by SLOT.  Each entry looks like this:

   { "! optimize_size && ! TARGET_READ_MODIFY_WRITE",
     __builtin_constant_p (! optimize_size && ! TARGET_READ_MODIFY_WRITE)
     ? (int) (! optimize_size && ! TARGET_READ_MODIFY_WRITE)
     : -1) },  */

static int
write_one_condition (void **slot, void * ARG_UNUSED (dummy))
{
  const struct c_test *test = * (const struct c_test **) slot;
  const char *p;

  print_rtx_ptr_loc (test->expr);
  fputs ("  { \"", stdout);
  for (p = test->expr; *p; p++)
    {
      switch (*p)
	{
	case '\n': fputs ("\\n\\", stdout); break;
	case '\\':
	case '\"': putchar ('\\'); break;
	default: break;
	}
      putchar (*p);
    }

  fputs ("\",\n    __builtin_constant_p ", stdout);
  print_c_condition (test->expr);
  fputs ("\n    ? (int) ", stdout);
  print_c_condition (test->expr);
  fputs ("\n    : -1 },\n", stdout);
  return 1;
}

/* Write out the complete conditions table, its size, and a flag
   indicating that gensupport.c can now do insn elision.  */
static void
write_conditions (void)
{
  puts ("\
/* Structure definition duplicated from gensupport.h rather than\n\
   drag in that file and its dependencies.  */\n\
struct c_test\n\
{\n\
  const char *expr;\n\
  int value;\n\
};\n\
\n\
/* This table lists each condition found in the machine description.\n\
   Each condition is mapped to its truth value (0 or 1), or -1 if that\n\
   cannot be calculated at compile time.\n\
   If we don't have __builtin_constant_p, or it's not acceptable in array\n\
   initializers, fall back to assuming that all conditions potentially\n\
   vary at run time.  It works in 3.0.1 and later; 3.0 only when not\n\
   optimizing.  */\n\
\n\
#if GCC_VERSION >= 3001\n\
static const struct c_test insn_conditions[] = {\n");

  traverse_c_tests (write_one_condition, 0);

  puts ("\n};\n#endif /* gcc >= 3.0.1 */\n");
}

/* Emit code which will convert the C-format table to a
   (define_conditions) form, which the MD reader can understand.
   The result will be added to the set of files scanned by
   'downstream' generators.  */
static void
write_writer (void)
{
  puts ("int\n"
	"main(void)\n"
	"{\n"
	"  unsigned int i;\n"
        "  const char *p;\n"
        "  puts (\"(define_conditions [\");\n"
	"#if GCC_VERSION >= 3001\n"
	"  for (i = 0; i < ARRAY_SIZE (insn_conditions); i++)\n"
	"    {\n"
	"      printf (\"  (%d \\\"\", insn_conditions[i].value);\n"
	"      for (p = insn_conditions[i].expr; *p; p++)\n"
	"        {\n"
	"          switch (*p)\n"
	"	     {\n"
	"	     case '\\\\':\n"
	"	     case '\\\"': putchar ('\\\\'); break;\n"
	"	     default: break;\n"
	"	     }\n"
	"          putchar (*p);\n"
	"        }\n"
        "      puts (\"\\\")\");\n"
        "    }\n"
	"#endif /* gcc >= 3.0.1 */\n"
	"  puts (\"])\");\n"
        "  fflush (stdout);\n"
        "return ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE;\n"
	"}");
}

int
main (int argc, char **argv)
{
  rtx desc;
  int pattern_lineno; /* not used */
  int code;

  progname = "genconditions";

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  /* Read the machine description.  */
  while (1)
    {
      desc = read_md_rtx (&pattern_lineno, &code);
      if (desc == NULL)
	break;

      /* N.B. define_insn_and_split, define_cond_exec are handled
	 entirely within read_md_rtx; we never see them.  */
      switch (GET_CODE (desc))
	{
	default:
	  break;

	case DEFINE_INSN:
	case DEFINE_EXPAND:
	  add_c_test (XSTR (desc, 2), -1);
	  /* except.h needs to know whether there is an eh_return
	     pattern in the machine description.  */
	  if (!strcmp (XSTR (desc, 0), "eh_return"))
	    saw_eh_return = 1;
	  break;

	case DEFINE_SPLIT:
	case DEFINE_PEEPHOLE:
	case DEFINE_PEEPHOLE2:
	  add_c_test (XSTR (desc, 1), -1);
	  break;
	}
    }

  write_header ();
  write_conditions ();
  write_writer ();

  fflush (stdout);
  return (ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}
