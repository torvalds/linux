/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PowerPC ELF notes.
 *
 * Copyright 2019, IBM Corporation
 */

#include <linux/elfnote.h>
#include <asm/elfnote.h>

/*
 * Ultravisor-capable bit (PowerNV only).
 *
 * Bit 0 indicates that the powerpc kernel binary knows how to run in an
 * ultravisor-enabled system.
 *
 * In an ultravisor-enabled system, some machine resources are now controlled
 * by the ultravisor. If the kernel is not ultravisor-capable, but it ends up
 * being run on a machine with ultravisor, the kernel will probably crash
 * trying to access ultravisor resources. For instance, it may crash in early
 * boot trying to set the partition table entry 0.
 *
 * In an ultravisor-enabled system, a bootloader could warn the user or prevent
 * the kernel from being run if the PowerPC ultravisor capability doesn't exist
 * or the Ultravisor-capable bit is not set.
 */
#ifdef CONFIG_PPC_POWERNV
#define PPCCAP_ULTRAVISOR_BIT		(1 << 0)
#else
#define PPCCAP_ULTRAVISOR_BIT		0
#endif

/*
 * Add the PowerPC Capabilities in the binary ELF note. It is a bitmap that
 * can be used to advertise kernel capabilities to userland.
 */
#define PPC_CAPABILITIES_BITMAP (PPCCAP_ULTRAVISOR_BIT)

ELFNOTE(PowerPC, PPC_ELFNOTE_CAPABILITIES,
	.long PPC_CAPABILITIES_BITMAP)
