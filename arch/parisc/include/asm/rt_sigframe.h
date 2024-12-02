/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_RT_SIGFRAME_H
#define _ASM_PARISC_RT_SIGFRAME_H

struct rt_sigframe {
	unsigned int tramp[2]; /* holds original return address */
	struct siginfo info;
	struct ucontext uc;
};

#define	SIGFRAME		128
#define FUNCTIONCALLFRAME	96
#define PARISC_RT_SIGFRAME_SIZE					\
	(((sizeof(struct rt_sigframe) + FUNCTIONCALLFRAME) + SIGFRAME) & -SIGFRAME)

#endif
