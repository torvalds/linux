/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AUDIT_H
#define _ASM_X86_AUDIT_H

int ia32_classify_syscall(unsigned int syscall);

extern unsigned ia32_dir_class[];
extern unsigned ia32_write_class[];
extern unsigned ia32_read_class[];
extern unsigned ia32_chattr_class[];
extern unsigned ia32_signal_class[];


#endif /* _ASM_X86_AUDIT_H */
