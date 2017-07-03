/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 */

#ifndef _UAPI__BFIN_POLL_H
#define _UAPI__BFIN_POLL_H

#define POLLWRNORM	(__force __poll_t)4 /* POLLOUT */
#define POLLWRBAND	(__force __poll_t)256

#include <asm-generic/poll.h>

#endif /* _UAPI__BFIN_POLL_H */
