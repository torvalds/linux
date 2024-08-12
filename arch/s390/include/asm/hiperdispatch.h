/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2024
 */

#ifndef _ASM_HIPERDISPATCH_H
#define _ASM_HIPERDISPATCH_H

void hd_reset_state(void);
void hd_add_core(int cpu);
void hd_disable_hiperdispatch(void);
int hd_enable_hiperdispatch(void);

#endif /* _ASM_HIPERDISPATCH_H */
