/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __SPARC_POLL_H
#define __SPARC_POLL_H

#define POLLWRNORM	POLLOUT
#define POLLWRBAND	(__force __poll_t)256
#define POLLMSG		(__force __poll_t)512
#define POLLREMOVE	(__force __poll_t)1024
#define POLLRDHUP       (__force __poll_t)2048

#include <asm-generic/poll.h>

#endif
