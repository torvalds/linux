/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_UV_H
#define BOOT_UV_H

#if IS_ENABLED(CONFIG_KVM)
unsigned long adjust_to_uv_max(unsigned long limit);
void sanitize_prot_virt_host(void);
#else
static inline unsigned long adjust_to_uv_max(unsigned long limit)
{
	return limit;
}
static inline void sanitize_prot_virt_host(void) {}
#endif

#if defined(CONFIG_PROTECTED_VIRTUALIZATION_GUEST) || IS_ENABLED(CONFIG_KVM)
void uv_query_info(void);
#else
static inline void uv_query_info(void) {}
#endif

#endif /* BOOT_UV_H */
