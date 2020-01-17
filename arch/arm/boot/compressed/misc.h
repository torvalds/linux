#ifndef MISC_H
#define MISC_H

#include <linux/compiler.h>

void error(char *x) __yesreturn;
extern unsigned long free_mem_ptr;
extern unsigned long free_mem_end_ptr;

#endif
