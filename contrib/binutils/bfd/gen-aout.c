/* Generate parameters for an a.out system.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 2001, 2002, 2007
   Free Software Foundation, Inc.

This file is part of BFD, the Binary File Descriptor library.

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

#include "/usr/include/a.out.h"
#include <stdio.h>

#ifndef _
#define _(X) X
#endif

int
main (argc, argv)
     int argc; char** argv;
{
  struct exec my_exec;
  int page_size;
  char *target = "unknown", *arch = "unknown";
  FILE *file = fopen("gen-aout", "r");

  if (file == NULL) {
      fprintf(stderr, "Cannot open gen-aout!\n");
      return -1;
  }
  if (fread(&my_exec, sizeof(struct exec), 1, file) != 1) {
      fprintf(stderr, "Cannot read gen-aout!\n");
      return -1;
  }

  target = argv[1];
  if (target == NULL) {
      fprintf(stderr, "Usage: gen-aout target_name\n");
      exit (1);
  }

#ifdef N_TXTOFF
  page_size = N_TXTOFF(my_exec);
  if (page_size == 0)
    printf("#define N_HEADER_IN_TEXT(x) 1\n");
  else
    printf("#define N_HEADER_IN_TEXT(x) 0\n");
#endif

  printf("#define BYTES_IN_WORD %d\n", sizeof (int));
  if (my_exec.a_entry == 0) {
      printf("#define ENTRY_CAN_BE_ZERO\n");
      printf("#define N_SHARED_LIB(x) 0 /* Avoids warning */\n");
  }
  else {
      printf("/*#define ENTRY_CAN_BE_ZERO*/\n");
      printf("/*#define N_SHARED_LIB(x) 0*/\n");
  }

  printf("#define TEXT_START_ADDR %d\n", my_exec.a_entry);

#ifdef PAGSIZ
  if (page_size == 0)
    page_size = PAGSIZ;
#endif
  if (page_size != 0)
    printf("#define TARGET_PAGE_SIZE %d\n", page_size);
  else
    printf("/* #define TARGET_PAGE_SIZE ??? */\n");
  printf("#define SEGMENT_SIZE TARGET_PAGE_SIZE\n");

#ifdef vax
  arch = "vax";
#endif
#ifdef m68k
  arch = "m68k";
#endif
  if (arch[0] == '1')
    {
      fprintf (stderr, _("warning: preprocessor substituted architecture name inside string;"));
      fprintf (stderr, _("         fix DEFAULT_ARCH in the output file yourself\n"));
      arch = "unknown";
    }
  printf("#define DEFAULT_ARCH bfd_arch_%s\n\n", arch);

  printf("/* Do not \"beautify\" the CONCAT* macro args.  Traditional C will not");
  printf("   remove whitespace added here, and thus will fail to concatenate");
  printf("   the tokens.  */");
  printf("\n#define MY(OP) CONCAT2 (%s_,OP)\n\n", target);
  printf("#define TARGETNAME \"a.out-%s\"\n\n", target);

  printf("#include \"sysdep.h\"\n");
  printf("#include \"bfd.h\"\n");
  printf("#include \"libbfd.h\"\n");
  printf("#include \"libaout.h\"\n");
  printf("\n#include \"aout-target.h\"\n");

  return 0;
}
