/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_POLL_H
#define _ASM_POLL_H

#define POLLWRNORM	POLLOUT
#define POLLWRBAND	256

#include <asm-generic/poll.h>

#undef POLLREMOVE

#endif

