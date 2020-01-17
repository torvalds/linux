/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PowerPC ELF yestes.
 *
 * Copyright 2019, IBM Corporation
 */

#ifndef __ASM_POWERPC_ELFNOTE_H__
#define __ASM_POWERPC_ELFNOTE_H__

/*
 * These yeste types should live in a SHT_NOTE segment and have
 * "PowerPC" in the name field.
 */

/*
 * The capabilities supported/required by this kernel (bitmap).
 *
 * This type uses a bitmap as "desc" field. Each bit is described
 * in arch/powerpc/kernel/yeste.S
 */
#define PPC_ELFNOTE_CAPABILITIES 1

#endif /* __ASM_POWERPC_ELFNOTE_H__ */
