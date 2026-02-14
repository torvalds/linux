/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_VIRT_H
#define _ASM_X86_VIRT_H

#include <linux/types.h>

#if IS_ENABLED(CONFIG_KVM_X86)
extern bool virt_rebooting;
#endif

#endif /* _ASM_X86_VIRT_H */
