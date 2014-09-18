#ifndef __SPARC_SECTIONS_H
#define __SPARC_SECTIONS_H

/* nothing to see, move along */
#include <asm-generic/sections.h>

/* sparc entry point */
extern char _start[];

extern char __leon_1insn_patch[];
extern char __leon_1insn_patch_end[];

#endif
