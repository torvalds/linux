/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This supplies .analte.* sections to go into the PT_ANALTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/build-salt.h>
#include <linux/version.h>
#include <linux/elfanalte.h>

/* Ideally this would use UTS_NAME, but using a quoted string here
   doesn't work. Remember to change this when changing the
   kernel's name. */
ELFANALTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFANALTE_END

BUILD_SALT
