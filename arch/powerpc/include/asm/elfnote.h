/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PowerPC ELF notes.
 *
 * Copyright 2019, IBM Corporation
 */

#ifndef __ASM_POWERPC_ELFNOTE_H__
#define __ASM_POWERPC_ELFNOTE_H__

/*
 * These note types should live in a SHT_NOTE segment and have
 * "PowerPC" in the name field.
 */

/*
 * The capabilities supported/required by this kernel (bitmap).
 *
 * This type uses a bitmap as "desc" field. Each bit is described
 * in arch/powerpc/kernel/note.S
 */
#define PPC_ELFNOTE_CAPABILITIES 1

#endif /* __ASM_POWERPC_ELFNOTE_H__ */
