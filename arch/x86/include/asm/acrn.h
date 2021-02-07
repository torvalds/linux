/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ACRN_H
#define _ASM_X86_ACRN_H

void acrn_setup_intr_handler(void (*handler)(void));
void acrn_remove_intr_handler(void);

#endif /* _ASM_X86_ACRN_H */
