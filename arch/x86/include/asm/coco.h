/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_COCO_H
#define _ASM_X86_COCO_H

#include <asm/asm.h>
#include <asm/types.h>

enum cc_vendor {
	CC_VENDOR_NONE,
	CC_VENDOR_AMD,
	CC_VENDOR_INTEL,
};

#ifdef CONFIG_ARCH_HAS_CC_PLATFORM
extern enum cc_vendor cc_vendor;
extern u64 cc_mask;

static inline void cc_set_mask(u64 mask)
{
	RIP_REL_REF(cc_mask) = mask;
}

u64 cc_mkenc(u64 val);
u64 cc_mkdec(u64 val);
void cc_random_init(void);
#else
#define cc_vendor (CC_VENDOR_NONE)
static const u64 cc_mask = 0;

static inline u64 cc_mkenc(u64 val)
{
	return val;
}

static inline u64 cc_mkdec(u64 val)
{
	return val;
}
static inline void cc_random_init(void) { }
#endif

#endif /* _ASM_X86_COCO_H */
