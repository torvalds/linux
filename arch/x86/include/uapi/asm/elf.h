/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_ELF_H
#define _UAPI_ASM_X86_ELF_H

#include <linux/types.h>

struct x86_xfeat_component {
	__u32 type;
	__u32 size;
	__u32 offset;
	__u32 flags;
} __packed;

_Static_assert(sizeof(struct x86_xfeat_component) % 4 == 0, "x86_xfeat_component is not aligned");

#endif /* _UAPI_ASM_X86_ELF_H */
