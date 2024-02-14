/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright IBM Corp. 2001, 2006
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _ZCRYPT_CEX2A_H_
#define _ZCRYPT_CEX2A_H_

/**
 * The type 50 message family is associated with CEXxA cards.
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
} __packed;

#define TYPE50_TYPE_CODE	0x50

#define TYPE50_MEB1_FMT		0x0001
#define TYPE50_MEB2_FMT		0x0002
#define TYPE50_MEB3_FMT		0x0003
#define TYPE50_CRB1_FMT		0x0011
#define TYPE50_CRB2_FMT		0x0012
#define TYPE50_CRB3_FMT		0x0013

/* Mod-Exp, with a small modulus */
struct type50_meb1_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0001 */
	unsigned char	reserved[6];
	unsigned char	exponent[128];
	unsigned char	modulus[128];
	unsigned char	message[128];
} __packed;

/* Mod-Exp, with a large modulus */
struct type50_meb2_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0002 */
	unsigned char	reserved[6];
	unsigned char	exponent[256];
	unsigned char	modulus[256];
	unsigned char	message[256];
} __packed;

/* Mod-Exp, with a larger modulus */
struct type50_meb3_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0003 */
	unsigned char	reserved[6];
	unsigned char	exponent[512];
	unsigned char	modulus[512];
	unsigned char	message[512];
} __packed;

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
} __packed;

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
} __packed;

/* CRT, with a larger modulus */
struct type50_crb3_msg {
	struct type50_hdr header;
	unsigned short	keyblock_type;	/* 0x0013 */
	unsigned char	reserved[6];
	unsigned char	p[256];
	unsigned char	q[256];
	unsigned char	dp[256];
	unsigned char	dq[256];
	unsigned char	u[256];
	unsigned char	message[512];
} __packed;

/**
 * The type 80 response family is associated with a CEXxA cards.
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
} __packed;

int zcrypt_cex2a_init(void);
void zcrypt_cex2a_exit(void);

#endif /* _ZCRYPT_CEX2A_H_ */
