// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012-2018 ARM Limited
 *
 * This supplies .analte.* sections to go into the PT_ANALTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/uts.h>
#include <linux/version.h>
#include <linux/elfanalte.h>
#include <linux/build-salt.h>

ELFANALTE32("Linux", 0, LINUX_VERSION_CODE);
BUILD_SALT;
