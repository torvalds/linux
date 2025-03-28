/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_THEAD_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_THEAD_H

#include <asm/vendor_extensions.h>

#include <linux/types.h>

/*
 * Extension keys must be strictly less than RISCV_ISA_VENDOR_EXT_MAX.
 */
#define RISCV_ISA_VENDOR_EXT_XTHEADVECTOR		0

extern struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_thead;

#ifdef CONFIG_RISCV_ISA_VENDOR_EXT_THEAD
void disable_xtheadvector(void);
#else
static inline void disable_xtheadvector(void) { }
#endif

/* Extension specific helpers */

/*
 * Vector 0.7.1 as used for example on T-Head Xuantie cores, uses an older
 * encoding for vsetvli (ta, ma vs. d1), so provide an instruction for
 * vsetvli	t4, x0, e8, m8, d1
 */
#define THEAD_VSETVLI_T4X0E8M8D1	".long	0x00307ed7\n\t"

/*
 * While in theory, the vector-0.7.1 vsb.v and vlb.v result in the same
 * encoding as the standard vse8.v and vle8.v, compilers seem to optimize
 * the call resulting in a different encoding and then using a value for
 * the "mop" field that is not part of vector-0.7.1
 * So encode specific variants for vstate_save and _restore.
 */
#define THEAD_VSB_V_V0T0		".long	0x02028027\n\t"
#define THEAD_VSB_V_V8T0		".long	0x02028427\n\t"
#define THEAD_VSB_V_V16T0		".long	0x02028827\n\t"
#define THEAD_VSB_V_V24T0		".long	0x02028c27\n\t"
#define THEAD_VLB_V_V0T0		".long	0x012028007\n\t"
#define THEAD_VLB_V_V8T0		".long	0x012028407\n\t"
#define THEAD_VLB_V_V16T0		".long	0x012028807\n\t"
#define THEAD_VLB_V_V24T0		".long	0x012028c07\n\t"

#endif
