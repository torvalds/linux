/*-
 * Copyright (c) 2004 Apple Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _AUDITREDUCE_H_
#define _AUDITREDUCE_H_


struct re_entry {
	char		*re_pattern;
	int		 re_negate;
	regex_t		 re_regexp;
	TAILQ_ENTRY(re_entry) re_glue;
};

#define OPT_a	0x00000001
#define OPT_b	0x00000002
#define OPT_c	0x00000004
#define OPT_d 	(OPT_a | OPT_b)	
#define OPT_e	0x00000010
#define OPT_f	0x00000020
#define OPT_g	0x00000040
#define OPT_j	0x00000080
#define OPT_m	0x00000100
#define OPT_of	0x00000200
#define OPT_om	0x00000400
#define OPT_op	0x00000800
#define OPT_ose	0x00001000
#define OPT_osh	0x00002000
#define OPT_oso	0x00004000
#define OPT_r	0x00008000
#define OPT_u	0x00010000
#define OPT_A	0x00020000
#define OPT_v	0x00040000

#define FILEOBJ "file"
#define MSGQIDOBJ "msgqid"
#define PIDOBJ "pid"
#define SEMIDOBJ "semid"
#define SHMIDOBJ "shmid"
#define SOCKOBJ "sock"


#define SETOPT(optmask, bit)	(optmask |= bit)
#define ISOPTSET(optmask, bit)	(optmask & bit)


#endif /* !_AUDITREDUCE_H_ */
