/*
 * Copyright (c) 1997 - 2000 Kungliga Tekniska Högskolan
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

static sig_atomic_t intr_flag;

static void
intr(int sig)
{
    intr_flag++;
}

#ifndef NSIG
#define NSIG 47
#endif

int
read_string(const char *prompt, char *buf, size_t len, int echo)
{
    struct sigaction sigs[NSIG];
    int oksigs[NSIG];
    struct sigaction sa;
    FILE *tty;
    int ret = 0;
    int of = 0;
    int i;
    int c;
    char *p;

    struct termios t_new, t_old;

    memset(&oksigs, 0, sizeof(oksigs));

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = intr;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    for(i = 1; i < sizeof(sigs) / sizeof(sigs[0]); i++)
	if (i != SIGALRM)
	    if (sigaction(i, &sa, &sigs[i]) == 0)
		oksigs[i] = 1;

    if((tty = fopen("/dev/tty", "r")) == NULL)
	tty = stdin;

    fprintf(stderr, "%s", prompt);
    fflush(stderr);

    if(echo == 0){
	tcgetattr(fileno(tty), &t_old);
	memcpy(&t_new, &t_old, sizeof(t_new));
	t_new.c_lflag &= ~ECHO;
	tcsetattr(fileno(tty), TCSANOW, &t_new);
    }
    intr_flag = 0;
    p = buf;
    while(intr_flag == 0){
	c = getc(tty);
	if(c == EOF){
	    if(!ferror(tty))
		ret = 1;
	    break;
	}
	if(c == '\n')
	    break;
	if(of == 0)
	    *p++ = c;
	of = (p == buf + len);
    }
    if(of)
	p--;
    *p = 0;

    if(echo == 0){
	printf("\n");
	tcsetattr(fileno(tty), TCSANOW, &t_old);
    }

    if(tty != stdin)
	fclose(tty);

    for(i = 1; i < sizeof(sigs) / sizeof(sigs[0]); i++)
	if (oksigs[i])
	    sigaction(i, &sigs[i], NULL);

    if(ret)
	return -3;
    if(intr_flag)
	return -2;
    if(of)
	return -1;
    return 0;
}


#if 0
int main()
{
    char s[128];
    int ret;
    ret = read_string("foo: ", s, sizeof(s), 0);
    printf("%d ->%s<-\n", ret, s);
}
#endif
