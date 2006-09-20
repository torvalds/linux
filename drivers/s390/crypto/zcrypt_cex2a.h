/*
 *  linux/drivers/s390/crypto/zcrypt_cex2a.h
 *
 *  zcrypt 2.1.0
 *
 *  Copyright (C)  2001, 2006 IBM Corporation
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

#ifndef _ZCRYPT_CEX2A_H_
#define _ZCRYPT_CEX2A_H_

/**
 * The type 50 message family is associated with a CEX2A card.
 *
 * The four members of the family are described below.
 *
 * Note that all unsigned char arrays are right-justified and left-padded
 * with zeroes.
 *
 * Note that all reserved fields must be zeroes.
 */
struct type50_hdr {
	unsigned char	reserved1;
	unsigned char	msg_type_code;	/* 0x50 */
	unsigned short	msg_len;
	unsigned char	reserved2;
	unsigned char	ignored;
	unsigned short	reserved3;
} __attribute__((packed));

#define TYPE50_TYPE_CODE	0x50

#define TYPE50_MEB1_FMT		0x0001
#define TYPE50_MEB2_FMT		0x0002
#define TYPE50_CRB1_FMT		0x0011
#define TYPE50_CRB2_FMT		0x0012

/* Mod-Exp, with a small modulus */
struct type50_meb1_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0001 */
	unsigned char	reserved[6];
	unsigned char	exponent[128];
	unsigned char	modulus[128];
	unsigned char	message[128];
} __attribute__((packed));

/* Mod-Exp, with a large modulus */
struct type50_meb2_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0002 */
	unsigned char	reserved[6];
	unsigned char	exponent[256];
	unsigned char	modulus[256];
	unsigned char	message[256];
} __attribute__((packed));

/* CRT, with a small modulus */
struct type50_crb1_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0011 */
	unsigned char	reserved[6];
	unsigned char	p[64];
	unsigned char	q[64];
	unsigned char	dp[64];
	unsigned char	dq[64];
	unsigned char	u[64];
	unsigned char	message[128];
} __attribute__((packed));

/* CRT, with a large modulus */
struct type50_crb2_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0012 */
	unsigned char	reserved[6];
	unsigned char	p[128];
	unsigned char	q[128];
	unsigned char	dp[128];
	unsigned char	dq[128];
	unsigned char	u[128];
	unsigned char	message[256];
} __attribute__((packed));

/**
 * The type 80 response family is associated with a CEX2A card.
 *
 * Note that all unsigned char arrays are right-justified and left-padded
 * with zeroes.
 *
 * Note that all reserved fields must be zeroes.
 */

#define TYPE80_RSP_CODE 0x80

struct type80_hdr {
	unsigned char	reserved1;
	unsigned char	type;		/* 0x80 */
	unsigned short	len;
	unsigned char	code;		/* 0x00 */
	unsigned char	reserved2[3];
	unsigned char	reserved3[8];
} __attribute__((packed));

int zcrypt_cex2a_init(void);
void zcrypt_cex2a_exit(void);

#endif /* _ZCRYPT_CEX2A_H_ */
