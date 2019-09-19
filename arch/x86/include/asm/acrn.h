/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ACRN_H
#define _ASM_X86_ACRN_H

extern void acrn_hv_callback_vector(void);
#ifdef CONFIG_TRACING
#define trace_acrn_hv_callback_vector acrn_hv_callback_vector
#endif

extern void acrn_hv_vector_handler(struct pt_regs *regs);
#endif /* _ASM_X86_ACRN_H */
