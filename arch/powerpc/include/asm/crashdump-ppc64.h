/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_CRASHDUMP_PPC64_H
#define _ASM_POWERPC_CRASHDUMP_PPC64_H

/*
 * Backup region - first 64KB of System RAM
 *
 * If ever the below macros are to be changed, please be judicious.
 * The implicit assumptions are:
 *     - start, end & size are less than UINT32_MAX.
 *     - start & size are at least 8 byte aligned.
 *
 * For implementation details: arch/powerpc/purgatory/trampoline_64.S
 */
#define BACKUP_SRC_START	0
#define BACKUP_SRC_END		0xffff
#define BACKUP_SRC_SIZE		(BACKUP_SRC_END - BACKUP_SRC_START + 1)

#endif /* __ASM_POWERPC_CRASHDUMP_PPC64_H */
