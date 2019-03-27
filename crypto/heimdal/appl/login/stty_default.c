/*
 * Copyright (c) 1995, 1996, 1997 Kungliga Tekniska Högskolan
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

#include "login_locl.h"

RCSID("$Id$");

#include <termios.h>

/* HP-UX 9.0 termios doesn't define these */
#ifndef FLUSHO
#define	FLUSHO	0
#endif

#ifndef XTABS
#define	XTABS	0
#endif

#ifndef OXTABS
#define OXTABS	XTABS
#endif

/* Ultrix... */
#ifndef ECHOPRT
#define ECHOPRT	0
#endif

#ifndef ECHOCTL
#define ECHOCTL	0
#endif

#ifndef ECHOKE
#define ECHOKE	0
#endif

#ifndef IMAXBEL
#define IMAXBEL	0
#endif

#define Ctl(x) ((x) ^ 0100)

void
stty_default(void)
{
    struct	termios termios;

    /*
     * Finalize the terminal settings. Some systems default to 8 bits,
     * others to 7, so we should leave that alone.
     */
    tcgetattr(0, &termios);

    termios.c_iflag |= (BRKINT|IGNPAR|ICRNL|IXON|IMAXBEL);
    termios.c_iflag &= ~IXANY;

    termios.c_lflag |= (ISIG|IEXTEN|ICANON|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE);
    termios.c_lflag &= ~(ECHOPRT|TOSTOP|FLUSHO);

    termios.c_oflag |= (OPOST|ONLCR);
    termios.c_oflag &= ~OXTABS;

    termios.c_cc[VINTR] = Ctl('C');
    termios.c_cc[VERASE] = Ctl('H');
    termios.c_cc[VKILL] = Ctl('U');
    termios.c_cc[VEOF] = Ctl('D');

    termios.c_cc[VSUSP] = Ctl('Z');

    tcsetattr(0, TCSANOW, &termios);
}
