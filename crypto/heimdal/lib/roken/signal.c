/*
 * Copyright (c) 1995 - 2000 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <signal.h>
#include "roken.h"

/*
 * We would like to always use this signal but there is a link error
 * on NEXTSTEP
 */
#if !defined(NeXT) && !defined(__APPLE__)
/*
 * Bugs:
 *
 * Do we need any extra hacks for SIGCLD and/or SIGCHLD?
 */

ROKEN_LIB_FUNCTION SigAction ROKEN_LIB_CALL
signal(int iSig, SigAction pAction)
{
    struct sigaction saNew, saOld;

    saNew.sa_handler = pAction;
    sigemptyset(&saNew.sa_mask);
    saNew.sa_flags = 0;

    if (iSig == SIGALRM)
	{
#ifdef SA_INTERRUPT
	    saNew.sa_flags |= SA_INTERRUPT;
#endif
	}
    else
	{
#ifdef SA_RESTART
	    saNew.sa_flags |= SA_RESTART;
#endif
	}

    if (sigaction(iSig, &saNew, &saOld) < 0)
	return(SIG_ERR);

    return(saOld.sa_handler);
}
#endif
