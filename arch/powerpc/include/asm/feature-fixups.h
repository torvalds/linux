#ifndef __ASM_POWERPC_FEATURE_FIXUPS_H
#define __ASM_POWERPC_FEATURE_FIXUPS_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * Feature section common macros
 *
 * Note that the entries now contain offsets between the table entry
 * and the code rather than absolute code pointers in order to be
 * useable with the vdso shared library. There is also an assumption
 * that values will be negative, that is, the fixup table has to be
 * located after the code it fixes up.
 */
#if defined(CONFIG_PPC64) && !defined(__powerpc64__)
/* 64 bits kernel, 32 bits code (ie. vdso32) */
#define FTR_ENTRY_LONG		.8byte
#define FTR_ENTRY_OFFSET	.long 0xffffffff; .long
#elif defined(CONFIG_PPC64)
#define FTR_ENTRY_LONG		.8byte
#define FTR_ENTRY_OFFSET	.8byte
#else
#define FTR_ENTRY_LONG		.long
#define FTR_ENTRY_OFFSET	.long
#endif

#define START_FTR_SECTION(label)	label##1:

#define FTR_SECTION_ELSE_NESTED(label)			\
label##2:						\
	.pushsection __ftr_alt_##label,"a";		\
	.align 2;					\
label##3:

#define MAKE_FTR_SECTION_ENTRY(msk, val, label, sect)		\
label##4:							\
	.popsection;						\
	.pushsection sect,"a";					\
	.align 3;						\
label##5:							\
	FTR_ENTRY_LONG msk;					\
	FTR_ENTRY_LONG val;					\
	FTR_ENTRY_OFFSET label##1b-label##5b;			\
	FTR_ENTRY_OFFSET label##2b-label##5b;			\
	FTR_ENTRY_OFFSET label##3b-label##5b;			\
	FTR_ENTRY_OFFSET label##4b-label##5b;			\
	.ifgt (label##4b- label##3b)-(label##2b- label##1b);	\
	.error "Feature section else case larger than body";	\
	.endif;							\
	.popsection;


/* CPU feature dependent sections */
#define BEGIN_FTR_SECTION_NESTED(label)	START_FTR_SECTION(label)
#define BEGIN_FTR_SECTION		START_FTR_SECTION(97)

#define END_FTR_SECTION_NESTED(msk, val, label) 		\
	FTR_SECTION_ELSE_NESTED(label)				\
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __ftr_fixup)

#define END_FTR_SECTION(msk, val)		\
	END_FTR_SECTION_NESTED(msk, val, 97)

#define END_FTR_SECTION_NESTED_IFSET(msk, label)	\
	END_FTR_SECTION_NESTED((msk), (msk), label)

#define END_FTR_SECTION_IFSET(msk)	END_FTR_SECTION((msk), (msk))
#define END_FTR_SECTION_IFCLR(msk)	END_FTR_SECTION((msk), 0)

/* CPU feature sections with alternatives, use BEGIN_FTR_SECTION to start */
#define FTR_SECTION_ELSE	FTR_SECTION_ELSE_NESTED(97)
#define ALT_FTR_SECTION_END_NESTED(msk, val, label)	\
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __ftr_fixup)
#define ALT_FTR_SECTION_END_NESTED_IFSET(msk, label)	\
	ALT_FTR_SECTION_END_NESTED(msk, msk, label)
#define ALT_FTR_SECTION_END_NESTED_IFCLR(msk, label)	\
	ALT_FTR_SECTION_END_NESTED(msk, 0, label)
#define ALT_FTR_SECTION_END(msk, val)	\
	ALT_FTR_SECTION_END_NESTED(msk, val, 97)
#define ALT_FTR_SECTION_END_IFSET(msk)	\
	ALT_FTR_SECTION_END_NESTED_IFSET(msk, 97)
#define ALT_FTR_SECTION_END_IFCLR(msk)	\
	ALT_FTR_SECTION_END_NESTED_IFCLR(msk, 97)

/* MMU feature dependent sections */
#define BEGIN_MMU_FTR_SECTION_NESTED(label)	START_FTR_SECTION(label)
#define BEGIN_MMU_FTR_SECTION			START_FTR_SECTION(97)

#define END_MMU_FTR_SECTION_NESTED(msk, val, label) 		\
	FTR_SECTION_ELSE_NESTED(label)				\
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __mmu_ftr_fixup)

#define END_MMU_FTR_SECTION(msk, val)		\
	END_MMU_FTR_SECTION_NESTED(msk, val, 97)

#define END_MMU_FTR_SECTION_IFSET(msk)	END_MMU_FTR_SECTION((msk), (msk))
#define END_MMU_FTR_SECTION_IFCLR(msk)	END_MMU_FTR_SECTION((msk), 0)

/* MMU feature sections with alternatives, use BEGIN_FTR_SECTION to start */
#define MMU_FTR_SECTION_ELSE_NESTED(label)	FTR_SECTION_ELSE_NESTED(label)
#define MMU_FTR_SECTION_ELSE	MMU_FTR_SECTION_ELSE_NESTED(97)
#define ALT_MMU_FTR_SECTION_END_NESTED(msk, val, label)	\
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __mmu_ftr_fixup)
#define ALT_MMU_FTR_SECTION_END_NESTED_IFSET(msk, label)	\
	ALT_MMU_FTR_SECTION_END_NESTED(msk, msk, label)
#define ALT_MMU_FTR_SECTION_END_NESTED_IFCLR(msk, label)	\
	ALT_MMU_FTR_SECTION_END_NESTED(msk, 0, label)
#define ALT_MMU_FTR_SECTION_END(msk, val)	\
	ALT_MMU_FTR_SECTION_END_NESTED(msk, val, 97)
#define ALT_MMU_FTR_SECTION_END_IFSET(msk)	\
	ALT_MMU_FTR_SECTION_END_NESTED_IFSET(msk, 97)
#define ALT_MMU_FTR_SECTION_END_IFCLR(msk)	\
	ALT_MMU_FTR_SECTION_END_NESTED_IFCLR(msk, 97)

/* Firmware feature dependent sections */
#define BEGIN_FW_FTR_SECTION_NESTED(label)	START_FTR_SECTION(label)
#define BEGIN_FW_FTR_SECTION			START_FTR_SECTION(97)

#define END_FW_FTR_SECTION_NESTED(msk, val, label) 		\
	FTR_SECTION_ELSE_NESTED(label)				\
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __fw_ftr_fixup)

#define END_FW_FTR_SECTION(msk, val)		\
	END_FW_FTR_SECTION_NESTED(msk, val, 97)

#define END_FW_FTR_SECTION_IFSET(msk)	END_FW_FTR_SECTION((msk), (msk))
#define END_FW_FTR_SECTION_IFCLR(msk)	END_FW_FTR_SECTION((msk), 0)

/* Firmware feature sections with alternatives */
#define FW_FTR_SECTION_ELSE_NESTED(label)	FTR_SECTION_ELSE_NESTED(label)
#define FW_FTR_SECTION_ELSE	FTR_SECTION_ELSE_NESTED(97)
#define ALT_FW_FTR_SECTION_END_NESTED(msk, val, label)	\
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __fw_ftr_fixup)
#define ALT_FW_FTR_SECTION_END_NESTED_IFSET(msk, label)	\
	ALT_FW_FTR_SECTION_END_NESTED(msk, msk, label)
#define ALT_FW_FTR_SECTION_END_NESTED_IFCLR(msk, label)	\
	ALT_FW_FTR_SECTION_END_NESTED(msk, 0, label)
#define ALT_FW_FTR_SECTION_END(msk, val)	\
	ALT_FW_FTR_SECTION_END_NESTED(msk, val, 97)
#define ALT_FW_FTR_SECTION_END_IFSET(msk)	\
	ALT_FW_FTR_SECTION_END_NESTED_IFSET(msk, 97)
#define ALT_FW_FTR_SECTION_END_IFCLR(msk)	\
	ALT_FW_FTR_SECTION_END_NESTED_IFCLR(msk, 97)

#ifndef __ASSEMBLY__

#define ASM_FTR_IF(section_if, section_else, msk, val)	\
	stringify_in_c(BEGIN_FTR_SECTION)			\
	section_if "; "						\
	stringify_in_c(FTR_SECTION_ELSE)			\
	section_else "; "					\
	stringify_in_c(ALT_FTR_SECTION_END((msk), (val)))

#define ASM_FTR_IFSET(section_if, section_else, msk)	\
	ASM_FTR_IF(section_if, section_else, (msk), (msk))

#define ASM_FTR_IFCLR(section_if, section_else, msk)	\
	ASM_FTR_IF(section_if, section_else, (msk), 0)

#define ASM_MMU_FTR_IF(section_if, section_else, msk, val)	\
	stringify_in_c(BEGIN_MMU_FTR_SECTION)			\
	section_if "; "						\
	stringify_in_c(MMU_FTR_SECTION_ELSE)			\
	section_else "; "					\
	stringify_in_c(ALT_MMU_FTR_SECTION_END((msk), (val)))

#define ASM_MMU_FTR_IFSET(section_if, section_else, msk)	\
	ASM_MMU_FTR_IF(section_if, section_else, (msk), (msk))

#define ASM_MMU_FTR_IFCLR(section_if, section_else, msk)	\
	ASM_MMU_FTR_IF(section_if, section_else, (msk), 0)

#endif /* __ASSEMBLY__ */

/* LWSYNC feature sections */
#define START_LWSYNC_SECTION(label)	label##1:
#define MAKE_LWSYNC_SECTION_ENTRY(label, sect)		\
label##2:						\
	.pushsection sect,"a";				\
	.align 2;					\
label##3:					       	\
	FTR_ENTRY_OFFSET label##1b-label##3b;		\
	.popsection;

#define RFI_FLUSH_FIXUP_SECTION				\
951:							\
	.pushsection __rfi_flush_fixup,"a";		\
	.align 2;					\
952:							\
	FTR_ENTRY_OFFSET 951b-952b;			\
	.popsection;


#ifndef __ASSEMBLY__
#include <linux/types.h>

extern long __start___rfi_flush_fixup, __stop___rfi_flush_fixup;

void apply_feature_fixups(void);
void setup_feature_keys(void);
#endif

#endif /* __ASM_POWERPC_FEATURE_FIXUPS_H */
