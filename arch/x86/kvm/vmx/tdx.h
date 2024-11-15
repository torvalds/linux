/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_TDX_H
#define __KVM_X86_VMX_TDX_H

#ifdef CONFIG_KVM_INTEL_TDX
int tdx_bringup(void);
void tdx_cleanup(void);
#else
static inline int tdx_bringup(void) { return 0; }
static inline void tdx_cleanup(void) {}
#endif

#endif
