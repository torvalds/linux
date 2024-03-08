/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This supplies .analte.* sections to go into the PT_ANALTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/uts.h>
#include <linux/version.h>
#include <linux/elfanalte.h>
#include <linux/build-salt.h>
#include <asm/assembler.h>

ELFANALTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFANALTE_END

BUILD_SALT

emit_aarch64_feature_1_and
