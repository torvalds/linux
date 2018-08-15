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

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* __ASM_NDS32_FTRACE_H */
