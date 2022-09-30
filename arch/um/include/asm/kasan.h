/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_UM_KASAN_H
#define __ASM_UM_KASAN_H

#include <linux/init.h>
#include <linux/const.h>

#define KASAN_SHADOW_OFFSET _AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

/* used in kasan_mem_to_shadow to divide by 8 */
#define KASAN_SHADOW_SCALE_SHIFT 3

#ifdef CONFIG_X86_64
#define KASAN_HOST_USER_SPACE_END_ADDR 0x00007fffffffffffUL
/* KASAN_SHADOW_SIZE is the size of total address space divided by 8 */
#define KASAN_SHADOW_SIZE ((KASAN_HOST_USER_SPACE_END_ADDR + 1) >> \
			KASAN_SHADOW_SCALE_SHIFT)
#else
#error "KASAN_SHADOW_SIZE is not defined for this sub-architecture"
#endif /* CONFIG_X86_64 */

#define KASAN_SHADOW_START (KASAN_SHADOW_OFFSET)
#define KASAN_SHADOW_END (KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

#ifdef CONFIG_KASAN
void kasan_init(void);
void kasan_map_memory(void *start, unsigned long len);
extern int kasan_um_is_ready;

#ifdef CONFIG_STATIC_LINK
#define kasan_arch_is_ready() (kasan_um_is_ready)
#endif
#else
static inline void kasan_init(void) { }
#endif /* CONFIG_KASAN */

#endif /* __ASM_UM_KASAN_H */
