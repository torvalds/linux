/* Copyright (C) 2003 - 2004 PathScale, Inc
 * Released under the GPL
 */

#include <stdio.h>
#include <user-offsets.h>

#define SC_OFFSET(name) \
  printf("#define " #name \
	 "(sc) *((unsigned long *) &(((char *) (sc))[%d]))\n",\
	 name)

int main(int argc, char **argv)
{
  SC_OFFSET(SC_RBX);
  SC_OFFSET(SC_RCX);
  SC_OFFSET(SC_RDX);
  SC_OFFSET(SC_RSI);
  SC_OFFSET(SC_RDI);
  SC_OFFSET(SC_RBP);
  SC_OFFSET(SC_RAX);
  SC_OFFSET(SC_R8);
  SC_OFFSET(SC_R9);
  SC_OFFSET(SC_R10);
  SC_OFFSET(SC_R11);
  SC_OFFSET(SC_R12);
  SC_OFFSET(SC_R13);
  SC_OFFSET(SC_R14);
  SC_OFFSET(SC_R15);
  SC_OFFSET(SC_IP);
  SC_OFFSET(SC_SP);
  SC_OFFSET(SC_CR2);
  SC_OFFSET(SC_ERR);
  SC_OFFSET(SC_TRAPNO);
  SC_OFFSET(SC_CS);
  SC_OFFSET(SC_FS);
  SC_OFFSET(SC_GS);
  SC_OFFSET(SC_EFLAGS);
  SC_OFFSET(SC_SIGMASK);
#if 0
  SC_OFFSET(SC_ORIG_RAX);
  SC_OFFSET(SC_DS);
  SC_OFFSET(SC_ES);
  SC_OFFSET(SC_SS);
#endif
  return(0);
}
