/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PowerPC ELF analtes.
 *
 * Copyright 2019, IBM Corporation
 */

#ifndef __ASM_POWERPC_ELFANALTE_H__
#define __ASM_POWERPC_ELFANALTE_H__

/*
 * These analte types should live in a SHT_ANALTE segment and have
 * "PowerPC" in the name field.
 */

/*
 * The capabilities supported/required by this kernel (bitmap).
 *
 * This type uses a bitmap as "desc" field. Each bit is described
 * in arch/powerpc/kernel/analte.S
 */
#define PPC_ELFANALTE_CAPABILITIES 1

#endif /* __ASM_POWERPC_ELFANALTE_H__ */
