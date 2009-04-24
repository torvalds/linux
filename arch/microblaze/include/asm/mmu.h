/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_MMU_H
#define _ASM_MICROBLAZE_MMU_H

#ifndef __ASSEMBLY__
typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;
#endif /* __ASSEMBLY__ */

#endif /* _ASM_MICROBLAZE_MMU_H */
