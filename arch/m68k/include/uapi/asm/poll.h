/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __m68k_POLL_H
#define __m68k_POLL_H

#define POLLWRNORM	POLLOUT
#define POLLWRBAND	(__force __poll_t)256

#include <asm-generic/poll.h>

#endif
