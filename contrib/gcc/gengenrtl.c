/* Generate code to allocate RTL structures.
   Copyright (C) 1997, 1998, 1999, 2000, 2002, 2003, 2004
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


#include "bconfig.h"
#include "system.h"

struct rtx_definition
{
  const char *const enumname, *const name, *const format;
};

/* rtl.def needs CONST_DOUBLE_FORMAT, but we don't care what
   CONST_DOUBLE_FORMAT is because we're not going to be generating
   anything for CONST_DOUBLE anyway.  */
#define CONST_DOUBLE_FORMAT ""

#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS) { #ENUM, NAME, FORMAT },

static const struct rtx_definition defs[] =
{
#include "rtl.def"		/* rtl expressions are documented here */
};
#define NUM_RTX_CODE ARRAY_SIZE(defs)

static const char *formats[NUM_RTX_CODE];

static const char *type_from_format	(int);
static const char *accessor_from_format	(int);
static int special_format		(const char *);
static int special_rtx			(int);
static int excluded_rtx			(int);
static void find_formats		(void);
static void gendecl			(const char *);
static void genmacro			(int);
static void gendef			(const char *);
static void genlegend			(void);
static void genheader			(void);
static void gencode			(void);

/* Decode a format letter into a C type string.  */

static const char *
type_from_format (int c)
{
  switch (c)
    {
    case 'i':
      return "int ";

    case 'w':
      return "HOST_WIDE_INT ";

    case 's':
      return "const char *";

    case 'e':  case 'u':
      return "rtx ";

    case 'E':
      return "rtvec ";
    case 'b':
      return "struct bitmap_head_def *";  /* bitmap - typedef not available */
    case 't':
      return "union tree_node *";  /* tree - typedef not available */
    case 'B':
      return "struct basic_block_def *";  /* basic block - typedef not available */
    default:
      gcc_unreachable ();
    }
}

/* Decode a format letter into the proper accessor function.  */

static const char *
accessor_from_format (int c)
{
  switch (c)
    {
    case 'i':
      return "XINT";

    case 'w':
      return "XWINT";

    case 's':
      return "XSTR";

    case 'e':  case 'u':
      return "XEXP";

    case 'E':
      return "XVEC";

    case 'b':
      return "XBITMAP";

    case 't':
      return "XTREE";

    case 'B':
      return "XBBDEF";

    default:
      gcc_unreachable ();
    }
}

/* Return nonzero if we should ignore FMT, an RTL format, when making
   the list of formats we write routines to create.  */

static int
special_format (const char *fmt)
{
  return (strchr (fmt, '*') != 0
	  || strchr (fmt, 'V') != 0
	  || strchr (fmt, 'S') != 0
	  || strchr (fmt, 'n') != 0);
}

/* Return nonzero if the RTL code given by index IDX is one that we should
   generate a gen_rtx_raw_FOO macro for, not gen_rtx_FOO (because gen_rtx_FOO
   is a wrapper in emit-rtl.c).  */

static int
special_rtx (int idx)
{
  return (strcmp (defs[idx].enumname, "CONST_INT") == 0
	  || strcmp (defs[idx].enumname, "REG") == 0
	  || strcmp (defs[idx].enumname, "SUBREG") == 0
	  || strcmp (defs[idx].enumname, "MEM") == 0
	  || strcmp (defs[idx].enumname, "CONST_VECTOR") == 0);
}

/* Return nonzero if the RTL code given by index IDX is one that we should
   generate no macro for at all (because gen_rtx_FOO is never used or
   cannot have the obvious interface).  */

static int
excluded_rtx (int idx)
{
  return (strcmp (defs[idx].enumname, "CONST_DOUBLE") == 0);
}

/* Place a list of all format specifiers we use into the array FORMAT.  */

static void
find_formats (void)
{
  unsigned int i;

  for (i = 0; i < NUM_RTX_CODE; i++)
    {
      const char **f;

      if (special_format (defs[i].format))
	continue;

      for (f = formats; *f; f++)
	if (! strcmp (*f, defs[i].format))
	  break;

      if (*f == 0)
	*f = defs[i].format;
    }
}

/* Write the declarations for the routine to allocate RTL with FORMAT.  */

static void
gendecl (const char *format)
{
  const char *p;
  int i, pos;

  printf ("extern rtx gen_rtx_fmt_%s\t (RTX_CODE, ", format);
  printf ("enum machine_mode mode");

  /* Write each parameter that is needed and start a new line when the line
     would overflow.  */
  for (p = format, i = 0, pos = 75; *p != 0; p++)
    if (*p != '0')
      {
	int ourlen = strlen (type_from_format (*p)) + 6 + (i > 9);

	printf (",");
	if (pos + ourlen > 76)
	  printf ("\n\t\t\t\t      "), pos = 39;

	printf (" %sarg%d", type_from_format (*p), i++);
	pos += ourlen;
      }

  printf (");\n");
}

/* Generate macros to generate RTL of code IDX using the functions we
   write.  */

static void
genmacro (int idx)
{
  const char *p;
  int i;

  /* We write a macro that defines gen_rtx_RTLCODE to be an equivalent to
     gen_rtx_fmt_FORMAT where FORMAT is the RTX_FORMAT of RTLCODE.  */

  if (excluded_rtx (idx))
    /* Don't define a macro for this code.  */
    return;

  printf ("#define gen_rtx_%s%s(MODE",
	   special_rtx (idx) ? "raw_" : "", defs[idx].enumname);

  for (p = defs[idx].format, i = 0; *p != 0; p++)
    if (*p != '0')
      printf (", ARG%d", i++);

  printf (") \\\n  gen_rtx_fmt_%s (%s, (MODE)",
	  defs[idx].format, defs[idx].enumname);

  for (p = defs[idx].format, i = 0; *p != 0; p++)
    if (*p != '0')
      printf (", (ARG%d)", i++);

  puts (")");
}

/* Generate the code for the function to generate RTL whose
   format is FORMAT.  */

static void
gendef (const char *format)
{
  const char *p;
  int i, j;

  /* Start by writing the definition of the function name and the types
     of the arguments.  */

  printf ("rtx\ngen_rtx_fmt_%s (RTX_CODE code, enum machine_mode mode", format);
  for (p = format, i = 0; *p != 0; p++)
    if (*p != '0')
      printf (",\n\t%sarg%d", type_from_format (*p), i++);

  puts (")");

  /* Now write out the body of the function itself, which allocates
     the memory and initializes it.  */
  puts ("{");
  puts ("  rtx rt;");
  puts ("  rt = rtx_alloc (code);\n");

  puts ("  PUT_MODE (rt, mode);");

  for (p = format, i = j = 0; *p ; ++p, ++i)
    if (*p != '0')
      printf ("  %s (rt, %d) = arg%d;\n", accessor_from_format (*p), i, j++);
    else
      printf ("  X0EXP (rt, %d) = NULL_RTX;\n", i);

  puts ("\n  return rt;\n}\n");
}

/* Generate the documentation header for files we write.  */

static void
genlegend (void)
{
  puts ("/* Generated automatically by gengenrtl from rtl.def.  */\n");
}

/* Generate the text of the header file we make, genrtl.h.  */

static void
genheader (void)
{
  unsigned int i;
  const char **fmt;

  puts ("#ifndef GCC_GENRTL_H");
  puts ("#define GCC_GENRTL_H\n");

  for (fmt = formats; *fmt; ++fmt)
    gendecl (*fmt);

  putchar ('\n');

  for (i = 0; i < NUM_RTX_CODE; i++)
    if (! special_format (defs[i].format))
      genmacro (i);

  puts ("\n#endif /* GCC_GENRTL_H */");
}

/* Generate the text of the code file we write, genrtl.c.  */

static void
gencode (void)
{
  const char **fmt;

  puts ("#include \"config.h\"");
  puts ("#include \"system.h\"");
  puts ("#include \"coretypes.h\"");
  puts ("#include \"tm.h\"");
  puts ("#include \"obstack.h\"");
  puts ("#include \"rtl.h\"");
  puts ("#include \"ggc.h\"\n");

  for (fmt = formats; *fmt != 0; fmt++)
    gendef (*fmt);
}

/* This is the main program.  We accept only one argument, "-h", which
   says we are writing the genrtl.h file.  Otherwise we are writing the
   genrtl.c file.  */

int
main (int argc, char **argv)
{
  find_formats ();
  genlegend ();

  if (argc == 2 && argv[1][0] == '-' && argv[1][1] == 'h')
    genheader ();
  else
    gencode ();

  if (ferror (stdout) || fflush (stdout) || fclose (stdout))
    return FATAL_EXIT_CODE;

  return SUCCESS_EXIT_CODE;
}
