/* Sysroff object format dumper.
   Copyright 1994, 1995, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2007
   Free Software Foundation, Inc.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */


/* Written by Steve Chamberlain <sac@cygnus.com>.

 This program reads a SYSROFF object file and prints it in an
 almost human readable form to stdout.  */

#include "sysdep.h"
#include "bfd.h"
#include "safe-ctype.h"
#include "libiberty.h"
#include "getopt.h"
#include "bucomm.h"
#include "sysroff.h"

static int dump = 1;
static int segmented_p;
static int code;
static int addrsize = 4;
static FILE *file;

static void dh (unsigned char *, int);
static void itheader (char *, int);
static void p (void);
static void tabout (void);
static void pbarray (barray *);
static int getone (int);
static int opt (int);
static void must (int);
static void tab (int, char *);
static void dump_symbol_info (void);
static void derived_type (void);
static void module (void);
static void show_usage (FILE *, int);

extern int main (int, char **);

static char *
getCHARS (unsigned char *ptr, int *idx, int size, int max)
{
  int oc = *idx / 8;
  char *r;
  int b = size;

  if (b >= max)
    return "*undefined*";

  if (b == 0)
    {
      /* Got to work out the length of the string from self.  */
      b = ptr[oc++];
      (*idx) += 8;
    }

  *idx += b * 8;
  r = xcalloc (b + 1, 1);
  memcpy (r, ptr + oc, b);
  r[b] = 0;

  return r;
}

static void
dh (unsigned char *ptr, int size)
{
  int i;
  int j;
  int span = 16;

  printf ("\n************************************************************\n");

  for (i = 0; i < size; i += span)
    {
      for (j = 0; j < span; j++)
	{
	  if (j + i < size)
	    printf ("%02x ", ptr[i + j]);
	  else
	    printf ("   ");
	}

      for (j = 0; j < span && j + i < size; j++)
	{
	  int c = ptr[i + j];

	  if (c < 32 || c > 127)
	    c = '.';
	  printf ("%c", c);
	}

      printf ("\n");
    }
}

static int
fillup (unsigned char *ptr)
{
  int size;
  int sum;
  int i;

  size = getc (file) - 2;
  fread (ptr, 1, size, file);
  sum = code + size + 2;

  for (i = 0; i < size; i++)
    sum += ptr[i];

  if ((sum & 0xff) != 0xff)
    printf ("SUM IS %x\n", sum);

  if (dump)
    dh (ptr, size);

  return size - 1;
}

static barray
getBARRAY (unsigned char *ptr, int *idx, int dsize ATTRIBUTE_UNUSED,
	   int max ATTRIBUTE_UNUSED)
{
  barray res;
  int i;
  int byte = *idx / 8;
  int size = ptr[byte++];

  res.len = size;
  res.data = (unsigned char *) xmalloc (size);

  for (i = 0; i < size; i++)
    res.data[i] = ptr[byte++];

  return res;
}

static int
getINT (unsigned char *ptr, int *idx, int size, int max)
{
  int n = 0;
  int byte = *idx / 8;

  if (byte >= max)
    return 0;

  if (size == -2)
    size = addrsize;

  if (size == -1)
    size = 0;

  switch (size)
    {
    case 0:
      return 0;
    case 1:
      n = (ptr[byte]);
      break;
    case 2:
      n = (ptr[byte + 0] << 8) + ptr[byte + 1];
      break;
    case 4:
      n = (ptr[byte + 0] << 24) + (ptr[byte + 1] << 16) + (ptr[byte + 2] << 8) + (ptr[byte + 3]);
      break;
    default:
      abort ();
    }

  *idx += size * 8;
  return n;
}

static int
getBITS (unsigned char *ptr, int *idx, int size, int max)
{
  int byte = *idx / 8;
  int bit = *idx % 8;

  if (byte >= max)
    return 0;

  *idx += size;

  return (ptr[byte] >> (8 - bit - size)) & ((1 << size) - 1);
}

static void
itheader (char *name, int code)
{
  printf ("\n%s 0x%02x\n", name, code);
}

static int indent;

static void
p (void)
{
  int i;

  for (i = 0; i < indent; i++)
    printf ("| ");

  printf ("> ");
}

static void
tabout (void)
{
  p ();
}

static void
pbarray (barray *y)
{
  int x;

  printf ("%d (", y->len);

  for (x = 0; x < y->len; x++)
    printf ("(%02x %c)", y->data[x],
	    ISPRINT (y->data[x]) ? y->data[x] : '.');

  printf (")\n");
}

#define SYSROFF_PRINT
#define SYSROFF_SWAP_IN

#include "sysroff.c"

/* FIXME: sysinfo, which generates sysroff.[ch] from sysroff.info, can't
   hack the special case of the tr block, which has no contents.  So we
   implement our own functions for reading in and printing out the tr
   block.  */

#define IT_tr_CODE	0x7f

static void
sysroff_swap_tr_in (void)
{
  unsigned char raw[255];

  memset (raw, 0, 255);
  fillup (raw);
}

static void
sysroff_print_tr_out (void)
{
  itheader ("tr", IT_tr_CODE);
}

static int
getone (int type)
{
  int c = getc (file);

  code = c;

  if ((c & 0x7f) != type)
    {
      ungetc (c, file);
      return 0;
    }

  switch (c & 0x7f)
    {
    case IT_cs_CODE:
      {
	struct IT_cs dummy;
	sysroff_swap_cs_in (&dummy);
	sysroff_print_cs_out (&dummy);
      }
      break;

    case IT_dln_CODE:
      {
	struct IT_dln dummy;
	sysroff_swap_dln_in (&dummy);
	sysroff_print_dln_out (&dummy);
      }
      break;

    case IT_hd_CODE:
      {
	struct IT_hd dummy;
	sysroff_swap_hd_in (&dummy);
	addrsize = dummy.afl;
	sysroff_print_hd_out (&dummy);
      }
      break;

    case IT_dar_CODE:
      {
	struct IT_dar dummy;
	sysroff_swap_dar_in (&dummy);
	sysroff_print_dar_out (&dummy);
      }
      break;

    case IT_dsy_CODE:
      {
	struct IT_dsy dummy;
	sysroff_swap_dsy_in (&dummy);
	sysroff_print_dsy_out (&dummy);
      }
      break;

    case IT_dfp_CODE:
      {
	struct IT_dfp dummy;
	sysroff_swap_dfp_in (&dummy);
	sysroff_print_dfp_out (&dummy);
      }
      break;

    case IT_dso_CODE:
      {
	struct IT_dso dummy;
	sysroff_swap_dso_in (&dummy);
	sysroff_print_dso_out (&dummy);
      }
      break;

    case IT_dpt_CODE:
      {
	struct IT_dpt dummy;
	sysroff_swap_dpt_in (&dummy);
	sysroff_print_dpt_out (&dummy);
      }
      break;

    case IT_den_CODE:
      {
	struct IT_den dummy;
	sysroff_swap_den_in (&dummy);
	sysroff_print_den_out (&dummy);
      }
      break;

    case IT_dbt_CODE:
      {
	struct IT_dbt dummy;
	sysroff_swap_dbt_in (&dummy);
	sysroff_print_dbt_out (&dummy);
      }
      break;

    case IT_dty_CODE:
      {
	struct IT_dty dummy;
	sysroff_swap_dty_in (&dummy);
	sysroff_print_dty_out (&dummy);
      }
      break;

    case IT_un_CODE:
      {
	struct IT_un dummy;
	sysroff_swap_un_in (&dummy);
	sysroff_print_un_out (&dummy);
      }
      break;

    case IT_sc_CODE:
      {
	struct IT_sc dummy;
	sysroff_swap_sc_in (&dummy);
	sysroff_print_sc_out (&dummy);
      }
      break;

    case IT_er_CODE:
      {
	struct IT_er dummy;
	sysroff_swap_er_in (&dummy);
	sysroff_print_er_out (&dummy);
      }
      break;

    case IT_ed_CODE:
      {
	struct IT_ed dummy;
	sysroff_swap_ed_in (&dummy);
	sysroff_print_ed_out (&dummy);
      }
      break;

    case IT_sh_CODE:
      {
	struct IT_sh dummy;
	sysroff_swap_sh_in (&dummy);
	sysroff_print_sh_out (&dummy);
      }
      break;

    case IT_ob_CODE:
      {
	struct IT_ob dummy;
	sysroff_swap_ob_in (&dummy);
	sysroff_print_ob_out (&dummy);
      }
      break;

    case IT_rl_CODE:
      {
	struct IT_rl dummy;
	sysroff_swap_rl_in (&dummy);
	sysroff_print_rl_out (&dummy);
      }
      break;

    case IT_du_CODE:
      {
	struct IT_du dummy;
	sysroff_swap_du_in (&dummy);

	sysroff_print_du_out (&dummy);
      }
      break;

    case IT_dus_CODE:
      {
	struct IT_dus dummy;
	sysroff_swap_dus_in (&dummy);
	sysroff_print_dus_out (&dummy);
      }
      break;

    case IT_dul_CODE:
      {
	struct IT_dul dummy;
	sysroff_swap_dul_in (&dummy);
	sysroff_print_dul_out (&dummy);
      }
      break;

    case IT_dss_CODE:
      {
	struct IT_dss dummy;
	sysroff_swap_dss_in (&dummy);
	sysroff_print_dss_out (&dummy);
      }
      break;

    case IT_hs_CODE:
      {
	struct IT_hs dummy;
	sysroff_swap_hs_in (&dummy);
	sysroff_print_hs_out (&dummy);
      }
      break;

    case IT_dps_CODE:
      {
	struct IT_dps dummy;
	sysroff_swap_dps_in (&dummy);
	sysroff_print_dps_out (&dummy);
      }
      break;

    case IT_tr_CODE:
      sysroff_swap_tr_in ();
      sysroff_print_tr_out ();
      break;

    case IT_dds_CODE:
      {
	struct IT_dds dummy;

	sysroff_swap_dds_in (&dummy);
	sysroff_print_dds_out (&dummy);
      }
      break;

    default:
      printf ("GOT A %x\n", c);
      return 0;
      break;
    }

  return 1;
}

static int
opt (int x)
{
  return getone (x);
}

static void
must (int x)
{
  if (!getone (x))
    printf ("WANTED %x!!\n", x);
}

static void
tab (int i, char *s)
{
  indent += i;

  if (s)
    {
      p ();
      printf (s);
      printf ("\n");
    }
}

static void
dump_symbol_info (void)
{
  tab (1, "SYMBOL INFO");

  while (opt (IT_dsy_CODE))
    {
      if (opt (IT_dty_CODE))
	{
	  must (IT_dbt_CODE);
	  derived_type ();
	  must (IT_dty_CODE);
	}
    }

  tab (-1, "");
}

static void
derived_type (void)
{
  tab (1, "DERIVED TYPE");

  while (1)
    {
      if (opt (IT_dpp_CODE))
	{
	  dump_symbol_info ();
	  must (IT_dpp_CODE);
	}
      else if (opt (IT_dfp_CODE))
	{
	  dump_symbol_info ();
	  must (IT_dfp_CODE);
	}
      else if (opt (IT_den_CODE))
	{
	  dump_symbol_info ();
	  must (IT_den_CODE);
	}
      else if (opt (IT_den_CODE))
	{
	  dump_symbol_info ();
	  must (IT_den_CODE);
	}
      else if (opt (IT_dds_CODE))
	{
	  dump_symbol_info ();
	  must (IT_dds_CODE);
	}
      else if (opt (IT_dar_CODE))
	{
	}
      else if (opt (IT_dpt_CODE))
	{
	}
      else if (opt (IT_dul_CODE))
	{
	}
      else if (opt (IT_dse_CODE))
	{
	}
      else if (opt (IT_dot_CODE))
	{
	}
      else
	break;
    }

  tab (-1, "");
}

static void
module (void)
{
  int c = 0;
  int l = 0;

  tab (1, "MODULE***\n");

  do
    {
      c = getc (file);
      ungetc (c, file);

      c &= 0x7f;
    }
  while (getone (c) && c != IT_tr_CODE);

  tab (-1, "");

  c = getc (file);
  while (c != EOF)
    {
      printf ("%02x ", c);
      l++;
      if (l == 32)
	{
	  printf ("\n");
	  l = 0;
	}
      c = getc (file);
    }
}

char *program_name;

static void
show_usage (FILE *file, int status)
{
  fprintf (file, _("Usage: %s [option(s)] in-file\n"), program_name);
  fprintf (file, _("Print a human readable interpretation of a SYSROFF object file\n"));
  fprintf (file, _(" The options are:\n\
  -h --help        Display this information\n\
  -v --version     Print the program's version number\n"));

  if (REPORT_BUGS_TO[0] && status == 0)
    fprintf (file, _("Report bugs to %s\n"), REPORT_BUGS_TO);
  exit (status);
}

int
main (int ac, char **av)
{
  char *input_file = NULL;
  int opt;
  static struct option long_options[] =
  {
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'V'},
    {NULL, no_argument, 0, 0}
  };

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = av[0];
  xmalloc_set_program_name (program_name);

  expandargv (&ac, &av);

  while ((opt = getopt_long (ac, av, "HhVv", long_options, (int *) NULL)) != EOF)
    {
      switch (opt)
	{
	case 'H':
	case 'h':
	  show_usage (stdout, 0);
	  /*NOTREACHED*/
	case 'v':
	case 'V':
	  print_version ("sysdump");
	  exit (0);
	  /*NOTREACHED*/
	case 0:
	  break;
	default:
	  show_usage (stderr, 1);
	  /*NOTREACHED*/
	}
    }

  /* The input and output files may be named on the command line.  */

  if (optind < ac)
    input_file = av[optind];

  if (!input_file)
    fatal (_("no input file specified"));

  file = fopen (input_file, FOPEN_RB);

  if (!file)
    fatal (_("cannot open input file %s"), input_file);

  module ();
  return 0;
}
