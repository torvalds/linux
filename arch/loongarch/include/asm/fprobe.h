/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LOONGARCH_FPROBE_H
#define _ASM_LOONGARCH_FPROBE_H

/*
 * Explicitly undef ARCH_DEFINE_ENCODE_FPROBE_HEADER, because loongarch does not
 * have enough number of fixed MSBs of the address of kernel objects for
 * encoding the size of data in fprobe_header. Use 2-entries encoding instead.
 */
#undef ARCH_DEFINE_ENCODE_FPROBE_HEADER

#endif /* _ASM_LOONGARCH_FPROBE_H */
