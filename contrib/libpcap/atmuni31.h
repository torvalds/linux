/*
 * Copyright (c) 1997 Yen Yen Lim and North Dakota State University
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Yen Yen Lim and
        North Dakota State University
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Based on UNI3.1 standard by ATM Forum */

/* ATM traffic types based on VPI=0 and (the following VCI */
#define VCI_PPC			0x05	/* Point-to-point signal msg */
#define VCI_BCC			0x02	/* Broadcast signal msg */
#define VCI_OAMF4SC		0x03	/* Segment OAM F4 flow cell */
#define VCI_OAMF4EC		0x04	/* End-to-end OAM F4 flow cell */
#define VCI_METAC		0x01	/* Meta signal msg */
#define VCI_ILMIC		0x10	/* ILMI msg */

/* Q.2931 signalling messages */
#define CALL_PROCEED		0x02	/* call proceeding */
#define CONNECT			0x07	/* connect */
#define CONNECT_ACK		0x0f	/* connect_ack */
#define SETUP			0x05	/* setup */
#define RELEASE			0x4d	/* release */
#define RELEASE_DONE		0x5a	/* release_done */
#define RESTART			0x46	/* restart */
#define RESTART_ACK		0x4e	/* restart ack */
#define STATUS			0x7d	/* status */
#define STATUS_ENQ		0x75	/* status ack */
#define ADD_PARTY		0x80	/* add party */
#define ADD_PARTY_ACK		0x81	/* add party ack */
#define ADD_PARTY_REJ		0x82	/* add party rej */
#define DROP_PARTY		0x83	/* drop party */
#define DROP_PARTY_ACK		0x84	/* drop party ack */

/* Information Element Parameters in the signalling messages */
#define CAUSE			0x08	/* cause */
#define ENDPT_REF		0x54	/* endpoint reference */
#define AAL_PARA		0x58	/* ATM adaptation layer parameters */
#define TRAFF_DESCRIP		0x59	/* atm traffic descriptors */
#define CONNECT_ID		0x5a	/* connection identifier */
#define QOS_PARA		0x5c	/* quality of service parameters */
#define B_HIGHER		0x5d	/* broadband higher layer information */
#define B_BEARER		0x5e	/* broadband bearer capability */
#define B_LOWER			0x5f	/* broadband lower information */
#define CALLING_PARTY		0x6c	/* calling party number */
#define CALLED_PARTY		0x70	/* called party nmber */

#define Q2931			0x09

/* Q.2931 signalling general messages format */
#define PROTO_POS       0	/* offset of protocol discriminator */
#define CALL_REF_POS    2	/* offset of call reference value */
#define MSG_TYPE_POS    5	/* offset of message type */
#define MSG_LEN_POS     7	/* offset of mesage length */
#define IE_BEGIN_POS    9	/* offset of first information element */

/* format of signalling messages */
#define TYPE_POS	0
#define LEN_POS		2
#define FIELD_BEGIN_POS 4
