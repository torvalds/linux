/* Copyright 2007  Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler, and GDB, the GNU Debugger.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "getopt.h"
#include "libiberty.h"
#include "safe-ctype.h"

#include "i386-opc.h"

#include <libintl.h>
#define _(String) gettext (String)

static const char *program_name = NULL;
static int debug = 0;

static void
fail (const char *message, ...)
{
  va_list args;
  
  va_start (args, message);
  fprintf (stderr, _("%s: Error: "), program_name);
  vfprintf (stderr, message, args);
  va_end (args);
  xexit (1);
}

/* Remove leading white spaces.  */

static char *
remove_leading_whitespaces (char *str)
{
  while (ISSPACE (*str))
    str++;
  return str;
}

/* Remove trailing white spaces.  */

static void
remove_trailing_whitespaces (char *str)
{
  size_t last = strlen (str);

  if (last == 0)
    return;

  do
    {
      last--;
      if (ISSPACE (str [last]))
	str[last] = '\0';
      else
	break;
    }
  while (last != 0);
}

/* Find next field separated by '.' and terminate it. Return a
   pointer to the one after it.  */

static char *
next_field (char *str, char **next)
{
  char *p;

  p = remove_leading_whitespaces (str);
  for (str = p; *str != ',' && *str != '\0'; str++);

  *str = '\0';
  remove_trailing_whitespaces (p);

  *next = str + 1; 

  return p;
}

static void
process_i386_opcodes (void)
{
  FILE *fp = fopen ("i386-opc.tbl", "r");
  char buf[2048];
  unsigned int i;
  char *str, *p, *last;
  char *name, *operands, *base_opcode, *extension_opcode;
  char *cpu_flags, *opcode_modifier, *operand_types [MAX_OPERANDS];

  if (fp == NULL)
    fail (_("can't find i386-opc.tbl for reading\n"));

  printf ("\n/* i386 opcode table.  */\n\n");
  printf ("const template i386_optab[] =\n{\n");

  while (!feof (fp))
    {
      if (fgets (buf, sizeof (buf), fp) == NULL)
	break;

      p = remove_leading_whitespaces (buf);

      /* Skip comments.  */
      str = strstr (p, "//");
      if (str != NULL)
	str[0] = '\0';

      /* Remove trailing white spaces.  */
      remove_trailing_whitespaces (p);

      switch (p[0])
	{
	case '#':
	  printf ("%s\n", p);
	case '\0':
	  continue;
	  break;
	default:
	  break;
	}

      last = p + strlen (p);

      /* Find name.  */
      name = next_field (p, &str);

      if (str >= last)
	abort ();

      /* Find number of operands.  */
      operands = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Find base_opcode.  */
      base_opcode = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Find extension_opcode.  */
      extension_opcode = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Find cpu_flags.  */
      cpu_flags = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Find opcode_modifier.  */
      opcode_modifier = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Remove the first {.  */
      str = remove_leading_whitespaces (str);
      if (*str != '{')
	abort ();
      str = remove_leading_whitespaces (str + 1);

      i = strlen (str);

      /* There are at least "X}".  */
      if (i < 2)
	abort ();

      /* Remove trailing white spaces and }. */
      do
	{
	  i--;
	  if (ISSPACE (str[i]) || str[i] == '}')
	    str[i] = '\0';
	  else
	    break;
	}
      while (i != 0);

      last = str + i;

      /* Find operand_types.  */
      for (i = 0; i < ARRAY_SIZE (operand_types); i++)
	{
	  if (str >= last)
	    {
	      operand_types [i] = NULL;
	      break;
	    }

	  operand_types [i] = next_field (str, &str);
	  if (*operand_types[i] == '0')
	    {
	      if (i != 0)
		operand_types[i] = NULL;
	      break;
	    }
	}

      printf ("  { \"%s\", %s, %s, %s, %s,\n",
	      name, operands, base_opcode, extension_opcode,
	      cpu_flags);

      printf ("    %s,\n", opcode_modifier);

      printf ("    { ");

      for (i = 0; i < ARRAY_SIZE (operand_types); i++)
	{
	  if (operand_types[i] == NULL
	      || *operand_types[i] == '0')
	    {
	      if (i == 0)
		printf ("0");
	      break;
	    }

	  if (i != 0)
	    printf (",\n      ");

	  printf ("%s", operand_types[i]);
	}
      printf (" } },\n");
    }

  printf ("  { NULL, 0, 0, 0, 0, 0, { 0 } }\n");
  printf ("};\n");
}

static void
process_i386_registers (void)
{
  FILE *fp = fopen ("i386-reg.tbl", "r");
  char buf[2048];
  char *str, *p, *last;
  char *reg_name, *reg_type, *reg_flags, *reg_num;

  if (fp == NULL)
    fail (_("can't find i386-reg.tbl for reading\n"));

  printf ("\n/* i386 register table.  */\n\n");
  printf ("const reg_entry i386_regtab[] =\n{\n");

  while (!feof (fp))
    {
      if (fgets (buf, sizeof (buf), fp) == NULL)
	break;

      p = remove_leading_whitespaces (buf);

      /* Skip comments.  */
      str = strstr (p, "//");
      if (str != NULL)
	str[0] = '\0';

      /* Remove trailing white spaces.  */
      remove_trailing_whitespaces (p);

      switch (p[0])
	{
	case '#':
	  printf ("%s\n", p);
	case '\0':
	  continue;
	  break;
	default:
	  break;
	}

      last = p + strlen (p);

      /* Find reg_name.  */
      reg_name = next_field (p, &str);

      if (str >= last)
	abort ();

      /* Find reg_type.  */
      reg_type = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Find reg_flags.  */
      reg_flags = next_field (str, &str);

      if (str >= last)
	abort ();

      /* Find reg_num.  */
      reg_num = next_field (str, &str);

      printf ("  { \"%s\", %s, %s, %s },\n",
	      reg_name, reg_type, reg_flags, reg_num);
    }

  printf ("};\n");

  printf ("\nconst unsigned int i386_regtab_size = ARRAY_SIZE (i386_regtab);\n");
}

/* Program options.  */
#define OPTION_SRCDIR	200

struct option long_options[] = 
{
  {"srcdir",  required_argument, NULL, OPTION_SRCDIR},
  {"debug",   no_argument,       NULL, 'd'},
  {"version", no_argument,       NULL, 'V'},
  {"help",    no_argument,       NULL, 'h'},
  {0,         no_argument,       NULL, 0}
};

static void
print_version (void)
{
  printf ("%s: version 1.0\n", program_name);
  xexit (0);
}

static void
usage (FILE * stream, int status)
{
  fprintf (stream, "Usage: %s [-V | --version] [-d | --debug] [--srcdir=dirname] [--help]\n",
	   program_name);
  xexit (status);
}

int
main (int argc, char **argv)
{
  extern int chdir (char *);
  char *srcdir = NULL;
  int c;
  
  program_name = *argv;
  xmalloc_set_program_name (program_name);

  while ((c = getopt_long (argc, argv, "vVdh", long_options, 0)) != EOF)
    switch (c)
      {
      case OPTION_SRCDIR:
	srcdir = optarg;
	break;
      case 'V':
      case 'v':
	print_version ();
	break;
      case 'd':
	debug = 1;
	break;
      case 'h':
      case '?':
	usage (stderr, 0);
      default:
      case 0:
	break;
      }

  if (optind != argc)
    usage (stdout, 1);

  if (srcdir != NULL) 
    if (chdir (srcdir) != 0)
      fail (_("unable to change directory to \"%s\", errno = %s\n"),
	    srcdir, strerror (errno));

  printf ("/* This file is automatically generated by i386-gen.  Do not edit!  */\n");

  process_i386_opcodes ();
  process_i386_registers ();

  exit (0);
}
