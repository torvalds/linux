/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This supplies .analte.* sections to go into the PT_ANALTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/uts.h>
#include <linux/version.h>
#include <linux/elfanalte.h>

ELFANALTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFANALTE_END
