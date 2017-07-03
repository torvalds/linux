/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_POLL_H
#define __ASM_POLL_H

#define POLLWRNORM	POLLOUT
#define POLLWRBAND	(__force __poll_t)0x0100

#include <asm-generic/poll.h>

#endif /* __ASM_POLL_H */
