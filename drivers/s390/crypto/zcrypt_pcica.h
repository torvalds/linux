/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2006
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ZCRYPT_PCICA_H_
#define _ZCRYPT_PCICA_H_

/**
 * The type 4 message family is associated with a PCICA card.
 *
 * The four members of the family are described below.
 *
 * Note that all unsigned char arrays are right-justified and left-padded
 * with zeroes.
 *
 * Note that all reserved fields must be zeroes.
 */
struct type4_hdr {
	unsigned char  reserved1;
	unsigned char  msg_type_code;	/* 0x04 */
	unsigned short msg_len;
	unsigned char  request_code;	/* 0x40 */
	unsigned char  msg_fmt;
	unsigned short reserved2;
} __attribute__((packed));

#define TYPE4_TYPE_CODE 0x04
#define TYPE4_REQU_CODE 0x40

#define TYPE4_SME_FMT 0x00
#define TYPE4_LME_FMT 0x10
#define TYPE4_SCR_FMT 0x40
#define TYPE4_LCR_FMT 0x50

/* Mod-Exp, with a small modulus */
struct type4_sme {
	struct type4_hdr header;
	unsigned char	 message[128];
	unsigned char	 exponent[128];
	unsigned char	 modulus[128];
} __attribute__((packed));

/* Mod-Exp, with a large modulus */
struct type4_lme {
	struct type4_hdr header;
	unsigned char	 message[256];
	unsigned char	 exponent[256];
	unsigned char	 modulus[256];
} __attribute__((packed));

/* CRT, with a small modulus */
struct type4_scr {
	struct type4_hdr header;
	unsigned char	 message[128];
	unsigned char	 dp[72];
	unsigned char	 dq[64];
	unsigned char	 p[72];
	unsigned char	 q[64];
	unsigned char	 u[72];
} __attribute__((packed));

/* CRT, with a large modulus */
struct type4_lcr {
	struct type4_hdr header;
	unsigned char	 message[256];
	unsigned char	 dp[136];
	unsigned char	 dq[128];
	unsigned char	 p[136];
	unsigned char	 q[128];
	unsigned char	 u[136];
} __attribute__((packed));

/**
 * The type 84 response family is associated with a PCICA card.
 *
 * Note that all unsigned char arrays are right-justified and left-padded
 * with zeroes.
 *
 * Note that all reserved fields must be zeroes.
 */

struct type84_hdr {
	unsigned char  reserved1;
	unsigned char  code;
	unsigned short len;
	unsigned char  reserved2[4];
} __attribute__((packed));

#define TYPE84_RSP_CODE 0x84

int zcrypt_pcica_init(void);
void zcrypt_pcica_exit(void);

#endif /* _ZCRYPT_PCICA_H_ */
