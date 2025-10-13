/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014, Imagination Technologies Ltd.
 *
 * EVA functions for generic code
 */

#ifndef _ASM_EVA_H
#define _ASM_EVA_H

#include <kernel-entry-init.h>

#ifdef __ASSEMBLER__

#ifdef CONFIG_EVA

/*
 * EVA early init code
 *
 * Platforms must define their own 'platform_eva_init' macro in
 * their kernel-entry-init.h header. This macro usually does the
 * platform specific configuration of the segmentation registers,
 * and it is normally called from assembly code.
 *
 */

.macro eva_init
platform_eva_init
.endm

#else

.macro eva_init
.endm

#endif /* CONFIG_EVA */

#endif /* __ASSEMBLER__ */

#endif
