/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _OPCODES_H
#define _OPCODES_H

#define SPARC_CR_OPCODE_PRIORITY	300

#define F3F(x,y,z)	(((x)<<30)|((y)<<19)|((z)<<5))

#define FPD_ENCODE(x)	(((x) >> 5) | ((x) & ~(0x20)))

#define RS1(x)		(FPD_ENCODE(x) << 14)
#define RS2(x)		(FPD_ENCODE(x) <<  0)
#define RS3(x)		(FPD_ENCODE(x) <<  9)
#define RD(x)		(FPD_ENCODE(x) << 25)
#define IMM5_0(x)	((x)           <<  0)
#define IMM5_9(x)	((x)           <<  9)

#define CRC32C(a,b,c)	\
	.word		(F3F(2,0x36,0x147)|RS1(a)|RS2(b)|RD(c));

#define MD5		\
	.word	0x81b02800;
#define SHA1		\
	.word	0x81b02820;
#define SHA256		\
	.word	0x81b02840;
#define SHA512		\
	.word	0x81b02860;

#define AES_EROUND01(a,b,c,d)	\
	.word	(F3F(2, 0x19, 0)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_EROUND23(a,b,c,d)	\
	.word	(F3F(2, 0x19, 1)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND01(a,b,c,d)	\
	.word	(F3F(2, 0x19, 2)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND23(a,b,c,d)	\
	.word	(F3F(2, 0x19, 3)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_EROUND01_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 4)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_EROUND23_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 5)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND01_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 6)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND23_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 7)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_KEXPAND1(a,b,c,d)	\
	.word	(F3F(2, 0x19, 8)|RS1(a)|RS2(b)|IMM5_9(c)|RD(d));
#define AES_KEXPAND0(a,b,c)	\
	.word	(F3F(2, 0x36, 0x130)|RS1(a)|RS2(b)|RD(c));
#define AES_KEXPAND2(a,b,c)	\
	.word	(F3F(2, 0x36, 0x131)|RS1(a)|RS2(b)|RD(c));

#define DES_IP(a,b)		\
	.word		(F3F(2, 0x36, 0x134)|RS1(a)|RD(b));
#define DES_IIP(a,b)		\
	.word		(F3F(2, 0x36, 0x135)|RS1(a)|RD(b));
#define DES_KEXPAND(a,b,c)	\
	.word		(F3F(2, 0x36, 0x136)|RS1(a)|IMM5_0(b)|RD(c));
#define DES_ROUND(a,b,c,d)	\
	.word		(F3F(2, 0x19, 0x009)|RS1(a)|RS2(b)|RS3(c)|RD(d));

#define CAMELLIA_F(a,b,c,d)		\
	.word		(F3F(2, 0x19, 0x00c)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define CAMELLIA_FL(a,b,c)		\
	.word		(F3F(2, 0x36, 0x13c)|RS1(a)|RS2(b)|RD(c));
#define CAMELLIA_FLI(a,b,c)		\
	.word		(F3F(2, 0x36, 0x13d)|RS1(a)|RS2(b)|RD(c));

#define MOVDTOX_F0_O4		\
	.word	0x99b02200
#define MOVDTOX_F2_O5		\
	.word	0x9bb02202
#define MOVXTOD_G1_F60 		\
	.word	0xbbb02301
#define MOVXTOD_G1_F62 		\
	.word	0xbfb02301
#define MOVXTOD_G3_F4		\
	.word	0x89b02303;
#define MOVXTOD_G7_F6		\
	.word	0x8db02307;
#define MOVXTOD_G3_F0		\
	.word	0x81b02303;
#define MOVXTOD_G7_F2		\
	.word	0x85b02307;
#define MOVXTOD_O0_F0		\
	.word	0x81b02308;
#define MOVXTOD_O5_F0		\
	.word	0x81b0230d;
#define MOVXTOD_O5_F2		\
	.word	0x85b0230d;
#define MOVXTOD_O5_F4		\
	.word	0x89b0230d;
#define MOVXTOD_O5_F6		\
	.word	0x8db0230d;
#define MOVXTOD_G3_F60		\
	.word	0xbbb02303;
#define MOVXTOD_G7_F62		\
	.word	0xbfb02307;

#endif /* _OPCODES_H */
