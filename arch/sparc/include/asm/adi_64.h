/* SPDX-License-Identifier: GPL-2.0-only */
/* adi_64.h: ADI related data structures
 *
 * Copyright (c) 2016 Oracle and/or its affiliates. All rights reserved.
 * Author: Khalid Aziz (khalid.aziz@oracle.com)
 */
#ifndef __ASM_SPARC64_ADI_H
#define __ASM_SPARC64_ADI_H

#include <linux/types.h>

#ifndef __ASSEMBLY__

struct adi_caps {
	__u64 blksz;
	__u64 nbits;
	__u64 ue_on_adi;
};

struct adi_config {
	bool enabled;
	struct adi_caps caps;
};

extern struct adi_config adi_state;

extern void mdesc_adi_init(void);

static inline bool adi_capable(void)
{
	return adi_state.enabled;
}

static inline unsigned long adi_blksize(void)
{
	return adi_state.caps.blksz;
}

static inline unsigned long adi_nbits(void)
{
	return adi_state.caps.nbits;
}

#endif	/* __ASSEMBLY__ */

#endif	/* !(__ASM_SPARC64_ADI_H) */
