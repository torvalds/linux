/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_FTRACE_H
#define _ASM_ALPHA_FTRACE_H

static void *alpha_ftrace_return_address0(void)
	noinline notrace;
static void *alpha_ftrace_return_address0(void)
{
	return __builtin_return_address(0);
}

#define ftrace_return_address0 alpha_ftrace_return_address0()

#ifdef CONFIG_FRAME_POINTER

/*
 * __builtin_return_address() requires a constant integer argument.
 * Keep this as a macro so the value is seen at the callsite.
 */
#define ftrace_return_address(n) __builtin_return_address(n)

#else  /* !CONFIG_FRAME_POINTER */

#define ftrace_return_address(n) ((void)(n), 0UL)

#endif /* CONFIG_FRAME_POINTER */

#endif /* _ASM_ALPHA_FTRACE_H */
