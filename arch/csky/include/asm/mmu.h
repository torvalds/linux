/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_MMU_H
#define __ASM_CSKY_MMU_H

typedef struct {
	atomic64_t	asid;
	void *vdso;
} mm_context_t;

#endif /* __ASM_CSKY_MMU_H */
