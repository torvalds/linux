/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: libunimsg/sscop/common.h,v 1.5 2005/05/23 11:46:16 brandt_h Exp $
 *
 * Common declaration for the SAAL programs.
 */
#ifndef _SAAL_COMMON_H_
#define _SAAL_COMMON_H_

#ifdef USE_LIBBEGEMOT
#include <rpoll.h>
#define evFileID int
#define evTimerID int
#else
#include <isc/eventlib.h>
#endif

/*
 * Writes to a pipe must be in messages (if we don't use framing).
 * It is not clear, what is the maximum message size for this. It seems
 * to be PIPE_BUF, but be conservative.
 */
#define	MAXUSRMSG	4096
#define	MAXMSG		(MAXUSRMSG+4)

extern int useframe;		/* use frame functions */
extern int sscopframe;		/* use sscop framing */
extern u_int sscop_vflag;	/* be talkative */
extern int sscop_fd;		/* file descriptor for SSCOP protocol */
extern int user_fd;		/* file descriptor for USER */
extern int loose;		/* loose messages */
extern int user_out_fd;		/* file descriptor for output to user */
extern u_int verbose;		/* talk to me */
#ifndef USE_LIBBEGEMOT
extern evContext evctx;
#endif
extern evFileID sscop_h;
extern evFileID user_h;

void dump_buf(const char *, const u_char *, size_t);
struct uni_msg *proto_msgin(int);
struct uni_msg *user_msgin(int);
void proto_msgout(struct uni_msg *);
void user_msgout(struct uni_msg *);
void parse_param(struct sscop_param *, u_int *, int, char *);

void verb(const char *, ...) __printflike(1, 2);

void sscop_verbose(struct sscop *, void *, const char *, ...)
	__printflike(3, 4);
void *sscop_start_timer(struct sscop *, void *, u_int, void (*)(void *));
void sscop_stop_timer(struct sscop *, void *, void *);

#define VERBOSE(P)	do { if (verbose & 0x0001) verb P; } while(0)

#endif	/* _SAAL_COMMON_H_ */
