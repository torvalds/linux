;
;  linux/arch/c6x/lib/csum_64plus.s
;
;  Port on Texas Instruments TMS320C6x architecture
;
;  Copyright (C) 2006, 2009, 2010, 2011 Texas Instruments Incorporated
;  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
;
;  This program is free software; you can redistribute it and/or modify
;  it under the terms of the GNU General Public License version 2 as
;  published by the Free Software Foundation.
;
#include <linux/linkage.h>

;
;unsigned int csum_partial_copy(const char *src, char * dst,
;				int len, int sum)
;
; A4:	src
; B4:	dst
; A6:	len
; B6:	sum
; return csum in A4
;

	.text
ENTRY(csum_partial_copy)
	MVC	.S2	ILC,B30

	MV	.D1X	B6,A31		; given csum
	ZERO	.D1	A9		; csum (a side)
||	ZERO	.D2	B9		; csum (b side)
||	SHRU	.S2X	A6,2,B5		; len / 4

	;; Check alignment and size
	AND	.S1	3,A4,A1
||	AND	.S2	3,B4,B0
	OR	.L2X	B0,A1,B0	; non aligned condition
||	MVC	.S2	B5,ILC
||	MVK	.D2	1,B2
||	MV	.D1X	B5,A1		; words condition
  [!A1]	B	.S1	L8
   [B0] BNOP	.S1	L6,5

	SPLOOP		1

	;; Main loop for aligned words
	LDW	.D1T1	*A4++,A7
	NOP	4
	MV	.S2X	A7,B7
||	EXTU	.S1	A7,0,16,A16
	STW	.D2T2	B7,*B4++
||	MPYU	.M2	B7,B2,B8
||	ADD	.L1	A16,A9,A9
	NOP
	SPKERNEL	8,0
||	ADD	.L2	B8,B9,B9

	ZERO	.D1	A1
||	ADD	.L1X	A9,B9,A9	;  add csum from a and b sides

L6:
  [!A1]	BNOP	.S1	L8,5

	;; Main loop for non-aligned words
	SPLOOP		2
 ||	MVK	.L1	1,A2

	LDNW	.D1T1	*A4++,A7
	NOP		3

	NOP
	MV	.S2X	A7,B7
 ||	EXTU	.S1	A7,0,16,A16
 ||	MPYU	.M1	A7,A2,A8

	ADD	.L1	A16,A9,A9
	SPKERNEL	6,0
 ||	STNW	.D2T2	B7,*B4++
 ||	ADD	.L1	A8,A9,A9

L8:	AND	.S2X	2,A6,B5
	CMPGT	.L2	B5,0,B0
  [!B0]	BNOP	.S1	L82,4

	;; Manage half-word
	ZERO	.L1	A7
||	ZERO	.D1	A8

#ifdef CONFIG_CPU_BIG_ENDIAN

	LDBU	.D1T1	*A4++,A7
	LDBU	.D1T1	*A4++,A8
	NOP		3
	SHL	.S1	A7,8,A0
	ADD	.S1	A8,A9,A9
	STB	.D2T1	A7,*B4++
||	ADD	.S1	A0,A9,A9
	STB	.D2T1	A8,*B4++

#else

	LDBU	.D1T1	*A4++,A7
	LDBU	.D1T1	*A4++,A8
	NOP		3
	ADD	.S1	A7,A9,A9
	SHL	.S1	A8,8,A0

	STB	.D2T1	A7,*B4++
||	ADD	.S1	A0,A9,A9
	STB	.D2T1	A8,*B4++

#endif

	;; Manage eventually the last byte
L82:	AND	.S2X	1,A6,B0
  [!B0]	BNOP	.S1	L9,5

||	ZERO	.L1	A7

L83:	LDBU	.D1T1	*A4++,A7
	NOP		4

	MV	.L2X	A7,B7

#ifdef CONFIG_CPU_BIG_ENDIAN

	STB	.D2T2	B7,*B4++
||	SHL	.S1	A7,8,A7
	ADD	.S1	A7,A9,A9

#else

	STB	.D2T2	B7,*B4++
||	ADD	.S1	A7,A9,A9

#endif

	;; Fold the csum
L9:	SHRU	.S2X	A9,16,B0
  [!B0]	BNOP	.S1	L10,5

L91:	SHRU	.S2X	A9,16,B4
||	EXTU	.S1	A9,16,16,A3
	ADD	.D1X	A3,B4,A9

	SHRU	.S1	A9,16,A0
   [A0]	BNOP	.S1	L91,5

L10:	ADD	.D1	A31,A9,A9
	MV	.D1	A9,A4

	BNOP	.S2	B3,4
	MVC	.S2	B30,ILC
ENDPROC(csum_partial_copy)

;
;unsigned short
;ip_fast_csum(unsigned char *iph, unsigned int ihl)
;{
;	unsigned int checksum = 0;
;	unsigned short *tosum = (unsigned short *) iph;
;	int len;
;
;	len = ihl*4;
;
;	if (len <= 0)
;		return 0;
;
;	while(len) {
;		len -= 2;
;		checksum += *tosum++;
;	}
;	if (len & 1)
;		checksum += *(unsigned char*) tosum;
;
;	while(checksum >> 16)
;		checksum = (checksum & 0xffff) + (checksum >> 16);
;
;	return ~checksum;
;}
;
; A4:	iph
; B4:	ihl
; return checksum in A4
;
	.text

ENTRY(ip_fast_csum)
	ZERO	.D1	A5
 ||	MVC	.S2	ILC,B30
	SHL	.S2	B4,2,B0
	CMPGT	.L2	B0,0,B1
  [!B1] BNOP	.S1	L15,4
  [!B1]	ZERO	.D1	A3

  [!B0]	B	.S1	L12
	SHRU	.S2	B0,1,B0
	MVC	.S2	B0,ILC
	NOP	3

	SPLOOP	1
	LDHU	.D1T1	*A4++,A3
	NOP	3
	NOP
	SPKERNEL	5,0
 ||	ADD	.L1	A3,A5,A5

L12:	SHRU	.S1	A5,16,A0
  [!A0]	BNOP	.S1	L14,5

L13:	SHRU	.S2X	A5,16,B4
	EXTU	.S1	A5,16,16,A3
	ADD	.D1X	A3,B4,A5
	SHRU	.S1	A5,16,A0
  [A0]	BNOP	.S1	L13,5

L14:	NOT	.D1	A5,A3
	EXTU	.S1	A3,16,16,A3

L15:	BNOP	.S2	B3,3
	MVC	.S2	B30,ILC
	MV	.D1	A3,A4
ENDPROC(ip_fast_csum)

;
;unsigned short
;do_csum(unsigned char *buff, unsigned int len)
;{
;	int odd, count;
;	unsigned int result = 0;
;
;	if (len <= 0)
;		goto out;
;	odd = 1 & (unsigned long) buff;
;	if (odd) {
;#ifdef __LITTLE_ENDIAN
;		result += (*buff << 8);
;#else
;		result = *buff;
;#endif
;		len--;
;		buff++;
;	}
;	count = len >> 1;		/* nr of 16-bit words.. */
;	if (count) {
;		if (2 & (unsigned long) buff) {
;			result += *(unsigned short *) buff;
;			count--;
;			len -= 2;
;			buff += 2;
;		}
;		count >>= 1;		/* nr of 32-bit words.. */
;		if (count) {
;			unsigned int carry = 0;
;			do {
;				unsigned int w = *(unsigned int *) buff;
;				count--;
;				buff += 4;
;				result += carry;
;				result += w;
;				carry = (w > result);
;			} while (count);
;			result += carry;
;			result = (result & 0xffff) + (result >> 16);
;		}
;		if (len & 2) {
;			result += *(unsigned short *) buff;
;			buff += 2;
;		}
;	}
;	if (len & 1)
;#ifdef __LITTLE_ENDIAN
;		result += *buff;
;#else
;		result += (*buff << 8);
;#endif
;	result = (result & 0xffff) + (result >> 16);
;	/* add up carry.. */
;	result = (result & 0xffff) + (result >> 16);
;	if (odd)
;		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
;out:
;	return result;
;}
;
; A4:	buff
; B4:	len
; return checksum in A4
;

ENTRY(do_csum)
	   CMPGT   .L2	   B4,0,B0
   [!B0]   BNOP    .S1	   L26,3
	   EXTU    .S1	   A4,31,31,A0

	   MV	   .L1	   A0,A3
||	   MV	   .S1X    B3,A5
||	   MV	   .L2	   B4,B3
||	   ZERO    .D1	   A1

#ifdef CONFIG_CPU_BIG_ENDIAN
   [A0]    SUB	   .L2	   B3,1,B3
|| [A0]    LDBU    .D1T1   *A4++,A1
#else
   [!A0]   BNOP    .S1	   L21,5
|| [A0]    LDBU    .D1T1   *A4++,A0
	   SUB	   .L2	   B3,1,B3
||	   SHL	   .S1	   A0,8,A1
L21:
#endif
	   SHR	   .S2	   B3,1,B0
   [!B0]   BNOP    .S1	   L24,3
	   MVK	   .L1	   2,A0
	   AND	   .L1	   A4,A0,A0

   [!A0]   BNOP    .S1	   L22,5
|| [A0]    LDHU    .D1T1   *A4++,A0
	   SUB	   .L2	   B0,1,B0
||	   SUB	   .S2	   B3,2,B3
||	   ADD	   .L1	   A0,A1,A1
L22:
	   SHR	   .S2	   B0,1,B0
||	   ZERO    .L1	   A0

   [!B0]   BNOP    .S1	   L23,5
|| [B0]    MVC	   .S2	   B0,ILC

	   SPLOOP  3
	   SPMASK  L1
||	   MV	   .L1	   A1,A2
||	   LDW	   .D1T1   *A4++,A1

	   NOP	   4
	   ADD	   .L1	   A0,A1,A0
	   ADD	   .L1	   A2,A0,A2

	   SPKERNEL 1,2
||	   CMPGTU  .L1	   A1,A2,A0

	   ADD	   .L1	   A0,A2,A6
	   EXTU    .S1	   A6,16,16,A7
	   SHRU    .S2X    A6,16,B0
	   NOP		   1
	   ADD	   .L1X    A7,B0,A1
L23:
	   MVK	   .L2	   2,B0
	   AND	   .L2	   B3,B0,B0
   [B0]    LDHU    .D1T1   *A4++,A0
	   NOP	   4
   [B0]    ADD	   .L1	   A0,A1,A1
L24:
	   EXTU    .S2	   B3,31,31,B0
#ifdef CONFIG_CPU_BIG_ENDIAN
   [!B0]   BNOP    .S1	   L25,4
|| [B0]    LDBU    .D1T1   *A4,A0
	   SHL	   .S1	   A0,8,A0
	   ADD	   .L1	   A0,A1,A1
L25:
#else
   [B0]    LDBU    .D1T1   *A4,A0
	   NOP	   4
   [B0]    ADD	   .L1	   A0,A1,A1
#endif
	   EXTU    .S1	   A1,16,16,A0
	   SHRU    .S2X    A1,16,B0
	   NOP	   1
	   ADD	   .L1X    A0,B0,A0
	   SHRU    .S1	   A0,16,A1
	   ADD	   .L1	   A0,A1,A0
	   EXTU    .S1	   A0,16,16,A1
	   EXTU    .S1	   A1,16,24,A2

	   EXTU    .S1	   A1,24,16,A0
||	   MV	   .L2X    A3,B0

   [B0]    OR	   .L1	   A0,A2,A1
L26:
	   NOP	   1
	   BNOP    .S2X    A5,4
	   MV	   .L1	   A1,A4
ENDPROC(do_csum)

;__wsum csum_partial(const void *buff, int len, __wsum wsum)
;{
;	unsigned int sum = (__force unsigned int)wsum;
;	unsigned int result = do_csum(buff, len);
;
;	/* add in old sum, and carry.. */
;	result += sum;
;	if (sum > result)
;		result += 1;
;	return (__force __wsum)result;
;}
;
ENTRY(csum_partial)
	   MV	   .L1X    B3,A9
||	   CALLP   .S2	   do_csum,B3
||	   MV	   .S1	   A6,A8
	   BNOP    .S2X    A9,2
	   ADD	   .L1	   A8,A4,A1
	   CMPGTU  .L1	   A8,A1,A0
	   ADD	   .L1	   A1,A0,A4
ENDPROC(csum_partial)

;unsigned short
;ip_compute_csum(unsigned char *buff, unsigned int len)
;
; A4:	buff
; B4:	len
; return checksum in A4

ENTRY(ip_compute_csum)
	   MV	   .L1X    B3,A9
||	   CALLP   .S2	   do_csum,B3
	   BNOP    .S2X    A9,3
	   NOT	   .S1	   A4,A4
	   CLR     .S1	   A4,16,31,A4
ENDPROC(ip_compute_csum)
