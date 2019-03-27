/*
 * Copyright (c) 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <signal.h>
#ifdef HAVE_SIGACTION
#include <string.h>
#endif

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "setsignal.h"

/*
 * An OS-independent signal() with, whenever possible, partial BSD
 * semantics, i.e. the signal handler is restored following service
 * of the signal, but system calls are *not* restarted, so that if
 * "pcap_breakloop()" is called in a signal handler in a live capture,
 * the read/recvfrom/whatever in the live capture doesn't get restarted,
 * it returns -1 and sets "errno" to EINTR, so we can break out of the
 * live capture loop.
 *
 * We use "sigaction()" if available.  We don't specify that the signal
 * should restart system calls, so that should always do what we want.
 *
 * Otherwise, if "sigset()" is available, it probably has BSD semantics
 * while "signal()" has traditional semantics, so we use "sigset()"; it
 * might cause system calls to be restarted for the signal, however.
 * I don't know whether, in any systems where it did cause system calls to
 * be restarted, there was a way to ask it not to do so; there may no
 * longer be any interesting systems without "sigaction()", however,
 * and, if there are, they might have "sigvec()" with SV_INTERRUPT
 * (which I think first appeared in 4.3BSD).
 *
 * Otherwise, we use "signal()" - which means we might get traditional
 * semantics, wherein system calls don't get restarted *but* the
 * signal handler is reset to SIG_DFL and the signal is not blocked,
 * so that a subsequent signal would kill the process immediately.
 *
 * Did I mention that signals suck?  At least in POSIX-compliant systems
 * they suck far less, as those systems have "sigaction()".
 */
RETSIGTYPE
(*setsignal (int sig, RETSIGTYPE (*func)(int)))(int)
{
#ifdef HAVE_SIGACTION
	struct sigaction old, new;

	memset(&new, 0, sizeof(new));
	new.sa_handler = func;
	if (sig == SIGCHLD)
		new.sa_flags = SA_RESTART;
	if (sigaction(sig, &new, &old) < 0)
		return (SIG_ERR);
	return (old.sa_handler);

#else
#ifdef HAVE_SIGSET
	return (sigset(sig, func));
#else
	return (signal(sig, func));
#endif
#endif
}

