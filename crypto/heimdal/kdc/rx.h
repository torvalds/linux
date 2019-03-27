/*
 * Copyright (c) 1997 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef __RX_H__
#define __RX_H__

/* header of a RPC packet */

enum rx_header_type {
     HT_DATA = 1,
     HT_ACK = 2,
     HT_BUSY = 3,
     HT_ABORT = 4,
     HT_ACKALL = 5,
     HT_CHAL = 6,
     HT_RESP = 7,
     HT_DEBUG = 8
};

/* For flags in header */

enum rx_header_flag {
     HF_CLIENT_INITIATED = 1,
     HF_REQ_ACK = 2,
     HF_LAST = 4,
     HF_MORE = 8
};

struct rx_header {
     uint32_t epoch;
     uint32_t connid;		/* And channel ID */
     uint32_t callid;
     uint32_t seqno;
     uint32_t serialno;
     u_char type;
     u_char flags;
     u_char status;
     u_char secindex;
     uint16_t reserved;	/* ??? verifier? */
     uint16_t serviceid;
/* This should be the other way around according to everything but */
/* tcpdump */
};

#define RX_HEADER_SIZE 28

#endif /* __RX_H__ */
