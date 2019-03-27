/*
 * Copyright (c) 2002 - 2005 NetGroup, Politecnico di Torino (Italy)
 * Copyright (c) 2005 - 2008 CACE Technologies, Davis (California)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino, CACE Technologies
 * nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __PCAP_RPCAP_INT_H__
#define __PCAP_RPCAP_INT_H__

#include "pcap.h"
#include "sockutils.h"	/* Needed for some structures (like SOCKET, sockaddr_in) which are used here */

/*
 * \file pcap-rpcap-int.h
 *
 * This file keeps all the definitions used by the RPCAP client and server,
 * other than the protocol definitions.
 *
 * \warning All the RPCAP functions that are allowed to return a buffer containing
 * the error description can return max PCAP_ERRBUF_SIZE characters.
 * However there is no guarantees that the string will be zero-terminated.
 * Best practice is to define the errbuf variable as a char of size 'PCAP_ERRBUF_SIZE+1'
 * and to insert manually the termination char at the end of the buffer. This will
 * guarantee that no buffer overflows occur even if we use the printf() to show
 * the error on the screen.
 */

/*********************************************************
 *                                                       *
 * General definitions / typedefs for the RPCAP protocol *
 *                                                       *
 *********************************************************/

/*
 * \brief Buffer used by socket functions to send-receive packets.
 * In case you plan to have messages larger than this value, you have to increase it.
 */
#define RPCAP_NETBUF_SIZE 64000

/*********************************************************
 *                                                       *
 * Exported function prototypes                          *
 *                                                       *
 *********************************************************/
void rpcap_createhdr(struct rpcap_header *header, uint8 type, uint16 value, uint32 length);
int rpcap_senderror(SOCKET sock, char *error, unsigned short errcode, char *errbuf);

#endif
