// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * Kernel version string
 */

#include "boot.h"
#include <generated/utsversion.h>
#include <generated/utsrelease.h>
#include <generated/compile.h>

const char kernel_version[] =
	UTS_RELEASE " (" LINUX_COMPILE_BY "@" LINUX_COMPILE_HOST ") "
	UTS_VERSION;
