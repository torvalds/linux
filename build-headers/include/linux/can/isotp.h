/* SPDX-License-Identifier: ((GPL-2.0-only WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * linux/can/isotp.h
 *
 * Definitions for isotp CAN sockets (ISO 15765-2:2016)
 *
 * Copyright (c) 2020 Volkswagen Group Electronic Research
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
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef _CAN_ISOTP_H
#define _CAN_ISOTP_H

#include <linux/types.h>
#include <linux/can.h>

#define SOL_CAN_ISOTP (SOL_CAN_BASE + CAN_ISOTP)

/* for socket options affecting the socket (not the global system) */

#define CAN_ISOTP_OPTS		1	/* pass struct can_isotp_options */

#define CAN_ISOTP_RECV_FC	2	/* pass struct can_isotp_fc_options */

/* sockopts to force stmin timer values for protocol regression tests */

#define CAN_ISOTP_TX_STMIN	3	/* pass __u32 value in nano secs    */
					/* use this time instead of value   */
					/* provided in FC from the receiver */

#define CAN_ISOTP_RX_STMIN	4	/* pass __u32 value in nano secs   */
					/* ignore received CF frames which */
					/* timestamps differ less than val */

#define CAN_ISOTP_LL_OPTS	5	/* pass struct can_isotp_ll_options */

struct can_isotp_options {

	__u32 flags;		/* set flags for isotp behaviour.	*/
				/* __u32 value : flags see below	*/

	__u32 frame_txtime;	/* frame transmission time (N_As/N_Ar)	*/
				/* __u32 value : time in nano secs	*/

	__u8  ext_address;	/* set address for extended addressing	*/
				/* __u8 value : extended address	*/

	__u8  txpad_content;	/* set content of padding byte (tx)	*/
				/* __u8 value : content	on tx path	*/

	__u8  rxpad_content;	/* set content of padding byte (rx)	*/
				/* __u8 value : content	on rx path	*/

	__u8  rx_ext_address;	/* set address for extended addressing	*/
				/* __u8 value : extended address (rx)	*/
};

struct can_isotp_fc_options {

	__u8  bs;		/* blocksize provided in FC frame	*/
				/* __u8 value : blocksize. 0 = off	*/

	__u8  stmin;		/* separation time provided in FC frame	*/
				/* __u8 value :				*/
				/* 0x00 - 0x7F : 0 - 127 ms		*/
				/* 0x80 - 0xF0 : reserved		*/
				/* 0xF1 - 0xF9 : 100 us - 900 us	*/
				/* 0xFA - 0xFF : reserved		*/

	__u8  wftmax;		/* max. number of wait frame transmiss.	*/
				/* __u8 value : 0 = omit FC N_PDU WT	*/
};

struct can_isotp_ll_options {

	__u8  mtu;		/* generated & accepted CAN frame type	*/
				/* __u8 value :				*/
				/* CAN_MTU   (16) -> standard CAN 2.0	*/
				/* CANFD_MTU (72) -> CAN FD frame	*/

	__u8  tx_dl;		/* tx link layer data length in bytes	*/
				/* (configured maximum payload length)	*/
				/* __u8 value : 8,12,16,20,24,32,48,64	*/
				/* => rx path supports all LL_DL values */

	__u8  tx_flags;		/* set into struct canfd_frame.flags	*/
				/* at frame creation: e.g. CANFD_BRS	*/
				/* Obsolete when the BRS flag is fixed	*/
				/* by the CAN netdriver configuration	*/
};

/* flags for isotp behaviour */

#define CAN_ISOTP_LISTEN_MODE	0x001	/* listen only (do not send FC) */
#define CAN_ISOTP_EXTEND_ADDR	0x002	/* enable extended addressing */
#define CAN_ISOTP_TX_PADDING	0x004	/* enable CAN frame padding tx path */
#define CAN_ISOTP_RX_PADDING	0x008	/* enable CAN frame padding rx path */
#define CAN_ISOTP_CHK_PAD_LEN	0x010	/* check received CAN frame padding */
#define CAN_ISOTP_CHK_PAD_DATA	0x020	/* check received CAN frame padding */
#define CAN_ISOTP_HALF_DUPLEX	0x040	/* half duplex error state handling */
#define CAN_ISOTP_FORCE_TXSTMIN	0x080	/* ignore stmin from received FC */
#define CAN_ISOTP_FORCE_RXSTMIN	0x100	/* ignore CFs depending on rx stmin */
#define CAN_ISOTP_RX_EXT_ADDR	0x200	/* different rx extended addressing */
#define CAN_ISOTP_WAIT_TX_DONE	0x400	/* wait for tx completion */
#define CAN_ISOTP_SF_BROADCAST	0x800	/* 1-to-N functional addressing */

/* default values */

#define CAN_ISOTP_DEFAULT_FLAGS		0
#define CAN_ISOTP_DEFAULT_EXT_ADDRESS	0x00
#define CAN_ISOTP_DEFAULT_PAD_CONTENT	0xCC /* prevent bit-stuffing */
#define CAN_ISOTP_DEFAULT_FRAME_TXTIME	0
#define CAN_ISOTP_DEFAULT_RECV_BS	0
#define CAN_ISOTP_DEFAULT_RECV_STMIN	0x00
#define CAN_ISOTP_DEFAULT_RECV_WFTMAX	0

#define CAN_ISOTP_DEFAULT_LL_MTU	CAN_MTU
#define CAN_ISOTP_DEFAULT_LL_TX_DL	CAN_MAX_DLEN
#define CAN_ISOTP_DEFAULT_LL_TX_FLAGS	0

/*
 * Remark on CAN_ISOTP_DEFAULT_RECV_* values:
 *
 * We can strongly assume, that the Linux Kernel implementation of
 * CAN_ISOTP is capable to run with BS=0, STmin=0 and WFTmax=0.
 * But as we like to be able to behave as a commonly available ECU,
 * these default settings can be changed via sockopts.
 * For that reason the STmin value is intentionally _not_ checked for
 * consistency and copied directly into the flow control (FC) frame.
 */

#endif /* !_UAPI_CAN_ISOTP_H */
