/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_KASLR_H_
#define _ASM_KASLR_H_

unsigned long kaslr_get_random_long(const char *purpose);

#ifdef CONFIG_RANDOMIZE_MEMORY
void kernel_randomize_memory(void);
#else
static inline void kernel_randomize_memory(void) { }
#endif /* CONFIG_RANDOMIZE_MEMORY */

#endif
