/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//kbuild:lib-y += bb_getsockname.o

#include "libbb.h"

int FAST_FUNC bb_getsockname(int sockfd, void *addr, socklen_t addrlen)
{
	/* The usefullness of this function is that for getsockname(),
	 * addrlen must go on stack (to _have_ an address to be passed),
	 * but many callers do not need its modified value.
	 * By using this shim, they can avoid unnecessary stack spillage.
	 */
	return getsockname(sockfd, (struct sockaddr *)addr, &addrlen);
}
