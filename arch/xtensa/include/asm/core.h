/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Cadence Design Systems Inc. */

#ifndef _ASM_XTENSA_CORE_H
#define _ASM_XTENSA_CORE_H

#include <variant/core.h>

#ifndef XCHAL_HAVE_DIV32
#define XCHAL_HAVE_DIV32 0
#endif

#ifndef XCHAL_HAVE_EXCLUSIVE
#define XCHAL_HAVE_EXCLUSIVE 0
#endif

#ifndef XCHAL_HAVE_EXTERN_REGS
#define XCHAL_HAVE_EXTERN_REGS 0
#endif

#ifndef XCHAL_HAVE_MPU
#define XCHAL_HAVE_MPU 0
#endif

#ifndef XCHAL_HAVE_VECBASE
#define XCHAL_HAVE_VECBASE 0
#endif

#ifndef XCHAL_SPANNING_WAY
#define XCHAL_SPANNING_WAY 0
#endif

#ifndef XCHAL_HW_MIN_VERSION
#if defined(XCHAL_HW_MIN_VERSION_MAJOR) && defined(XCHAL_HW_MIN_VERSION_MINOR)
#define XCHAL_HW_MIN_VERSION (XCHAL_HW_MIN_VERSION_MAJOR * 100 + \
			      XCHAL_HW_MIN_VERSION_MINOR)
#else
#define XCHAL_HW_MIN_VERSION 0
#endif
#endif

#endif
