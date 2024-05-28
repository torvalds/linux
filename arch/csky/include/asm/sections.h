/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_SECTIONS_H
#define __ASM_SECTIONS_H

#include <asm-generic/sections.h>

extern char _start[];

asmlinkage void csky_start(unsigned int unused, void *dtb_start);

#endif /* __ASM_SECTIONS_H */
