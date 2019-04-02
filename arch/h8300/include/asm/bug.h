/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H8300__H
#define _H8300__H

/* always true */
#define is_valid_addr(addr) (1)

#include <asm-generic/.h>

struct pt_regs;
extern void die(const char *str, struct pt_regs *fp, unsigned long err);

#endif
