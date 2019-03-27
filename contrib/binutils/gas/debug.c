/* This file is debug.c
   Copyright 1987, 1988, 1989, 1990, 1991, 1992, 2000, 2006
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
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Routines for debug use only.  */

#include "as.h"
#include "subsegs.h"

dmp_frags ()
{
  asection *s;
  frchainS *chp;
  char *p;

  for (s = stdoutput->sections; s; s = s->next)
    for (chp = seg_info (s)->frchainP; chp; chp = chp->frch_next)
      {
	switch (s)
	  {
	  case SEG_DATA:
	    p = "Data";
	    break;
	  case SEG_TEXT:
	    p = "Text";
	    break;
	  default:
	    p = "???";
	    break;
	  }
	printf ("\nSEGMENT %s %d\n", p, chp->frch_subseg);
	dmp_frag (chp->frch_root, "\t");
      }
}

dmp_frag (fp, indent)
     struct frag *fp;
     char *indent;
{
  for (; fp; fp = fp->fr_next)
    {
      printf ("%sFRAGMENT @ 0x%x\n", indent, fp);
      switch (fp->fr_type)
	{
	case rs_align:
	  printf ("%srs_align(%d)\n", indent, fp->fr_offset);
	  break;
	case rs_fill:
	  printf ("%srs_fill(%d)\n", indent, fp->fr_offset);
	  printf ("%s", indent);
	  var_chars (fp, fp->fr_var + fp->fr_fix);
	  printf ("%s\t repeated %d times,", indent, fp->fr_offset);
	  printf (" fixed length if # chars == 0)\n");
	  break;
	case rs_org:
	  printf ("%srs_org(%d+sym @0x%x)\n", indent,
		  fp->fr_offset, fp->fr_symbol);
	  printf ("%sfill with ", indent);
	  var_chars (fp, 1);
	  printf ("\n");
	  break;
	case rs_machine_dependent:
	  printf ("%smachine_dep\n", indent);
	  break;
	default:
	  printf ("%sunknown type\n", indent);
	  break;
	}
      printf ("%saddr=%d(0x%x)\n", indent, fp->fr_address, fp->fr_address);
      printf ("%sfr_fix=%d\n", indent, fp->fr_fix);
      printf ("%sfr_var=%d\n", indent, fp->fr_var);
      printf ("%sfr_offset=%d\n", indent, fp->fr_offset);
      printf ("%schars @ 0x%x\n", indent, fp->fr_literal);
      printf ("\n");
    }
}

var_chars (fp, n)
     struct frag *fp;
     int n;
{
  unsigned char *p;

  for (p = (unsigned char *) fp->fr_literal; n; n--, p++)
    {
      printf ("%02x ", *p);
    }
}

/* end of debug.c */
