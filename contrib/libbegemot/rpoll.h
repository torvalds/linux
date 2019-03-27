/*
 * Copyright (c)1996-2002 by Hartmut Brandt
 *	All rights reserved.
 *
 * Author: Hartmut Brandt
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 * 
 * 1. Redistributions of source code or documentation must retain the above
 *   copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR 
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * $Begemot: libbegemot/rpoll.h,v 1.5 2004/09/21 15:49:26 brandt Exp $
 */
# ifndef rpoll_h_
# define rpoll_h_

# ifdef __cplusplus
extern "C" {
# endif

typedef void (*poll_f)(int fd, int mask, void *arg);
typedef void (*timer_f)(int, void *);

int	poll_register(int fd, poll_f func, void *arg, int mask);
void	poll_unregister(int);
void	poll_dispatch(int wait);
int	poll_start_timer(u_int msecs, int repeat, timer_f func, void *arg);
int	poll_start_utimer(unsigned long long usecs, int repeat, timer_f func,
    void *arg);
void	poll_stop_timer(int);

enum {
	RPOLL_IN	= 1,
	RPOLL_OUT	= 2,
	RPOLL_EXCEPT	= 4,
};

extern int	rpoll_policy;
extern int	rpoll_trace;

# ifdef __cplusplus
}
# endif

# endif
