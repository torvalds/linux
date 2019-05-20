/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_KERNEL_PATCH_H
#define _PARISC_KERNEL_PATCH_H

/* stop machine and patch kernel text */
void patch_text(void *addr, unsigned int insn);

/* patch kernel text with machine already stopped (e.g. in kgdb) */
void __patch_text(void *addr, unsigned int insn);

#endif
