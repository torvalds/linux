/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _ASM_X86_AMD_HSMP_H_
#define _ASM_X86_AMD_HSMP_H_

#include <uapi/asm/amd_hsmp.h>

#if IS_ENABLED(CONFIG_AMD_HSMP)
int hsmp_send_message(struct hsmp_message *msg);
#else
static inline int hsmp_send_message(struct hsmp_message *msg)
{
	return -ENODEV;
}
#endif
#endif /*_ASM_X86_AMD_HSMP_H_*/
