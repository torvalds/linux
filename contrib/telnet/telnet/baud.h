/*
 * Copyright (c) 2014 EMC Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Try to guess whether speeds are "encoded" (4.2BSD) or just numeric (4.4BSD).
 */
#if B4800 != 4800
#define	DECODE_BAUD
#endif

#ifdef	DECODE_BAUD
#ifndef	B7200
#define B7200   B4800
#endif

#ifndef	B14400
#define B14400  B9600
#endif

#ifndef	B19200
#define B19200  B14400
#endif

#ifndef	B28800
#define B28800  B19200
#endif

#ifndef	B38400
#define B38400  B28800
#endif

#ifndef B57600
#define B57600  B38400
#endif

#ifndef B76800
#define B76800  B57600
#endif

#ifndef B115200
#define B115200 B76800
#endif

#ifndef B115200
#define B115200 B76800
#endif
#endif

#ifndef B230400
#define B230400 B115200
#endif

/*
 * A table of available terminal speeds
 */
struct termspeeds termspeeds[] = {
	{ 0,      B0 },
	{ 50,     B50 },
	{ 75,     B75 },
	{ 110,    B110 },
	{ 134,    B134 },
	{ 150,    B150 },
	{ 200,    B200 },
	{ 300,    B300 },
	{ 600,    B600 },
	{ 1200,   B1200 },
	{ 1800,   B1800 },
	{ 2400,   B2400 },
	{ 4800,   B4800 },
#ifdef	B7200
	{ 7200,   B7200 },
#endif
	{ 9600,   B9600 },
#ifdef	B14400
	{ 14400,  B14400 },
#endif
#ifdef	B19200
	{ 19200,  B19200 },
#endif
#ifdef	B28800
	{ 28800,  B28800 },
#endif
#ifdef	B38400
	{ 38400,  B38400 },
#endif
#ifdef	B57600
	{ 57600,  B57600 },
#endif
#ifdef	B115200
	{ 115200, B115200 },
#endif
#ifdef	B230400
	{ 230400, B230400 },
#endif
	{ -1,     0 }
};
