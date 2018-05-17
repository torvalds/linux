/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _UAPI_BFIN_SIGINFO_H
#define _UAPI_BFIN_SIGINFO_H

#include <linux/types.h>
#include <asm-generic/siginfo.h>

#define si_uid16	_sifields._kill._uid

#endif /* _UAPI_BFIN_SIGINFO_H */
