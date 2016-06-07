#ifndef _S390_SECTIONS_H
#define _S390_SECTIONS_H

#include <asm-generic/sections.h>

extern char _eshared[], _ehead[];
extern char __start_ro_after_init[], __end_ro_after_init[];

#endif
