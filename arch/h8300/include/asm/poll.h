#ifndef __H8300_POLL_H
#define __H8300_POLL_H

#define POLLWRNORM	POLLOUT
#define POLLWRBAND	256

#include <asm-generic/poll.h>

#undef POLLREMOVE

#endif
