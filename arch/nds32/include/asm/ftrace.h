/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_NDS32_FTRACE_H
#define __ASM_NDS32_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER

#define HAVE_FUNCTION_GRAPH_FP_TEST

#define MCOUNT_ADDR ((unsigned long)(_mcount))
/* mcount call is composed of three instructions:
 * sethi + ori + jral
 */
#define MCOUNT_INSN_SIZE 12

extern void _mcount(unsigned long parent_ip);

#ifdef CONFIG_DYNAMIC_FTRACE

#define FTRACE_ADDR ((unsigned long)_ftrace_caller)

#ifdef __NDS32_EL__
#define INSN_NOP		0x09000040
#define INSN_SIZE(insn)		(((insn & 0x00000080) == 0) ? 4 : 2)
#define IS_SETHI(insn)		((insn & 0x000000fe) == 0x00000046)
#define ENDIAN_CONVERT(insn)	be32_to_cpu(insn)
#else /* __NDS32_EB__ */
#define INSN_NOP		0x40000009
#define INSN_SIZE(insn)		(((insn & 0x80000000) == 0) ? 4 : 2)
#define IS_SETHI(insn)		((insn & 0xfe000000) == 0x46000000)
#define ENDIAN_CONVERT(insn)	(insn)
#endif

extern void _ftrace_caller(unsigned long parent_ip);
static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}
struct dyn_arch_ftrace {
};

#endif /* CONFIG_DYNAMIC_FTRACE */

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* __ASM_NDS32_FTRACE_H */
