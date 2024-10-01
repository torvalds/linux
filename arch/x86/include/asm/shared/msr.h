/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SHARED_MSR_H
#define _ASM_X86_SHARED_MSR_H

struct msr {
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	};
};

#endif /* _ASM_X86_SHARED_MSR_H */
