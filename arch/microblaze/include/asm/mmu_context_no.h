/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_MMU_CONTEXT_H
#define _ASM_MICROBLAZE_MMU_CONTEXT_H

# define init_new_context(tsk, mm)		({ 0; })

# define enter_lazy_tlb(mm, tsk)		do {} while (0)
# define change_mm_context(old, ctx, _pml4)	do {} while (0)
# define destroy_context(mm)			do {} while (0)
# define deactivate_mm(tsk, mm)			do {} while (0)
# define switch_mm(prev, next, tsk)		do {} while (0)
# define activate_mm(prev, next)		do {} while (0)

#endif /* _ASM_MICROBLAZE_MMU_CONTEXT_H */
