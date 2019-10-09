// SPDX-License-Identifier: GPL-2.0-only
#include <generated/utsrelease.h>
#include <generated/compile.h>
#include "boot.h"

const char kernel_version[] = UTS_RELEASE
	" (" LINUX_COMPILE_BY "@" LINUX_COMPILE_HOST ") " UTS_VERSION;
