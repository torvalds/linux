/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_DEF_H
#define __ASM_MTE_DEF_H

#define MTE_GRANULE_SIZE	UL(16)
#define MTE_GRANULE_MASK	(~(MTE_GRANULE_SIZE - 1))
#define MTE_TAG_SHIFT		56
#define MTE_TAG_SIZE		4
#define MTE_TAG_MASK		GENMASK((MTE_TAG_SHIFT + (MTE_TAG_SIZE - 1)), MTE_TAG_SHIFT)

#define __MTE_PREAMBLE		ARM64_ASM_PREAMBLE ".arch_extension memtag\n"

#endif /* __ASM_MTE_DEF_H  */
