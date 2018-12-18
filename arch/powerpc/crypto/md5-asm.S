/*
 * Fast MD5 implementation for PPC
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/asm-compat.h>

#define rHP	r3
#define rWP	r4

#define rH0	r0
#define rH1	r6
#define rH2	r7
#define rH3	r5

#define rW00	r8
#define rW01	r9
#define rW02	r10
#define rW03	r11
#define rW04	r12
#define rW05	r14
#define rW06	r15
#define rW07	r16
#define rW08	r17
#define rW09	r18
#define rW10	r19
#define rW11	r20
#define rW12	r21
#define rW13	r22
#define rW14	r23
#define rW15	r24

#define rT0	r25
#define rT1	r26

#define INITIALIZE \
	PPC_STLU r1,-INT_FRAME_SIZE(r1); \
	SAVE_8GPRS(14, r1);		/* push registers onto stack	*/ \
	SAVE_4GPRS(22, r1);						   \
	SAVE_GPR(26, r1)

#define FINALIZE \
	REST_8GPRS(14, r1);		/* pop registers from stack	*/ \
	REST_4GPRS(22, r1);						   \
	REST_GPR(26, r1);						   \
	addi	r1,r1,INT_FRAME_SIZE;

#ifdef __BIG_ENDIAN__
#define LOAD_DATA(reg, off) \
	lwbrx		reg,0,rWP;	/* load data			*/
#define INC_PTR \
	addi		rWP,rWP,4;	/* increment per word		*/
#define NEXT_BLOCK			/* nothing to do		*/
#else
#define LOAD_DATA(reg, off) \
	lwz		reg,off(rWP);	/* load data			*/
#define INC_PTR				/* nothing to do		*/
#define NEXT_BLOCK \
	addi		rWP,rWP,64;	/* increment per block		*/
#endif

#define R_00_15(a, b, c, d, w0, w1, p, q, off, k0h, k0l, k1h, k1l) \
	LOAD_DATA(w0, off)		/*    W				*/ \
	and		rT0,b,c;	/* 1: f = b and c		*/ \
	INC_PTR				/*    ptr++			*/ \
	andc		rT1,d,b;	/* 1: f' = ~b and d		*/ \
	LOAD_DATA(w1, off+4)		/*    W				*/ \
	or		rT0,rT0,rT1;	/* 1: f = f or f'		*/ \
	addi		w0,w0,k0l;	/* 1: wk = w + k		*/ \
	add		a,a,rT0;	/* 1: a = a + f			*/ \
	addis		w0,w0,k0h;	/* 1: wk = w + k'		*/ \
	addis		w1,w1,k1h;	/* 2: wk = w + k		*/ \
	add		a,a,w0;		/* 1: a = a + wk		*/ \
	addi		w1,w1,k1l;	/* 2: wk = w + k'		*/ \
	rotrwi		a,a,p;		/* 1: a = a rotl x		*/ \
	add		d,d,w1;		/* 2: a = a + wk		*/ \
	add		a,a,b;		/* 1: a = a + b			*/ \
	and		rT0,a,b;	/* 2: f = b and c		*/ \
	andc		rT1,c,a;	/* 2: f' = ~b and d		*/ \
	or		rT0,rT0,rT1;	/* 2: f = f or f'		*/ \
	add		d,d,rT0;	/* 2: a = a + f			*/ \
	INC_PTR				/*    ptr++			*/ \
	rotrwi		d,d,q;		/* 2: a = a rotl x		*/ \
	add		d,d,a;		/* 2: a = a + b			*/

#define R_16_31(a, b, c, d, w0, w1, p, q, k0h, k0l, k1h, k1l) \
	andc		rT0,c,d;	/* 1: f = c and ~d		*/ \
	and		rT1,b,d;	/* 1: f' = b and d		*/ \
	addi		w0,w0,k0l;	/* 1: wk = w + k		*/ \
	or		rT0,rT0,rT1;	/* 1: f = f or f'		*/ \
	addis		w0,w0,k0h;	/* 1: wk = w + k'		*/ \
	add		a,a,rT0;	/* 1: a = a + f			*/ \
	addi		w1,w1,k1l;	/* 2: wk = w + k		*/ \
	add		a,a,w0;		/* 1: a = a + wk		*/ \
	addis		w1,w1,k1h;	/* 2: wk = w + k'		*/ \
	andc		rT0,b,c;	/* 2: f = c and ~d		*/ \
	rotrwi		a,a,p;		/* 1: a = a rotl x		*/ \
	add		a,a,b;		/* 1: a = a + b			*/ \
	add		d,d,w1;		/* 2: a = a + wk		*/ \
	and		rT1,a,c;	/* 2: f' = b and d		*/ \
	or		rT0,rT0,rT1;	/* 2: f = f or f'		*/ \
	add		d,d,rT0;	/* 2: a = a + f			*/ \
	rotrwi		d,d,q;		/* 2: a = a rotl x		*/ \
	add		d,d,a;		/* 2: a = a +b			*/

#define R_32_47(a, b, c, d, w0, w1, p, q, k0h, k0l, k1h, k1l) \
	xor		rT0,b,c;	/* 1: f' = b xor c		*/ \
	addi		w0,w0,k0l;	/* 1: wk = w + k		*/ \
	xor		rT1,rT0,d;	/* 1: f = f xor f'		*/ \
	addis		w0,w0,k0h;	/* 1: wk = w + k'		*/ \
	add		a,a,rT1;	/* 1: a = a + f			*/ \
	addi		w1,w1,k1l;	/* 2: wk = w + k		*/ \
	add		a,a,w0;		/* 1: a = a + wk		*/ \
	addis		w1,w1,k1h;	/* 2: wk = w + k'		*/ \
	rotrwi		a,a,p;		/* 1: a = a rotl x		*/ \
	add		d,d,w1;		/* 2: a = a + wk		*/ \
	add		a,a,b;		/* 1: a = a + b			*/ \
	xor		rT1,rT0,a;	/* 2: f = b xor f'		*/ \
	add		d,d,rT1;	/* 2: a = a + f			*/ \
	rotrwi		d,d,q;		/* 2: a = a rotl x		*/ \
	add		d,d,a;		/* 2: a = a + b			*/

#define R_48_63(a, b, c, d, w0, w1, p, q, k0h, k0l, k1h, k1l) \
	addi		w0,w0,k0l;	/* 1: w = w + k			*/ \
	orc		rT0,b,d;	/* 1: f = b or ~d		*/ \
	addis		w0,w0,k0h;	/* 1: w = w + k'		*/ \
	xor		rT0,rT0,c;	/* 1: f = f xor c		*/ \
	add		a,a,w0;		/* 1: a = a + wk		*/ \
	addi		w1,w1,k1l;	/* 2: w = w + k			*/ \
	add		a,a,rT0;	/* 1: a = a + f			*/ \
	addis		w1,w1,k1h;	/* 2: w = w + k'		*/ \
	rotrwi		a,a,p;		/* 1: a = a rotl x		*/ \
	add		a,a,b;		/* 1: a = a + b			*/ \
	orc		rT0,a,c;	/* 2: f = b or ~d		*/ \
	add		d,d,w1;		/* 2: a = a + wk		*/ \
	xor		rT0,rT0,b;	/* 2: f = f xor c		*/ \
	add		d,d,rT0;	/* 2: a = a + f			*/ \
	rotrwi		d,d,q;		/* 2: a = a rotl x		*/ \
	add		d,d,a;		/* 2: a = a + b			*/

_GLOBAL(ppc_md5_transform)
	INITIALIZE

	mtctr		r5
	lwz		rH0,0(rHP)
	lwz		rH1,4(rHP)
	lwz		rH2,8(rHP)
	lwz		rH3,12(rHP)

ppc_md5_main:
	R_00_15(rH0, rH1, rH2, rH3, rW00, rW01, 25, 20, 0,
		0xd76b, -23432, 0xe8c8, -18602)
	R_00_15(rH2, rH3, rH0, rH1, rW02, rW03, 15, 10, 8,
		0x2420, 0x70db, 0xc1be, -12562)
	R_00_15(rH0, rH1, rH2, rH3, rW04, rW05, 25, 20, 16,
		0xf57c, 0x0faf, 0x4788, -14806)
	R_00_15(rH2, rH3, rH0, rH1, rW06, rW07, 15, 10, 24,
		0xa830, 0x4613, 0xfd47, -27391)
	R_00_15(rH0, rH1, rH2, rH3, rW08, rW09, 25, 20, 32,
		0x6981, -26408, 0x8b45,  -2129)
	R_00_15(rH2, rH3, rH0, rH1, rW10, rW11, 15, 10, 40,
		0xffff, 0x5bb1, 0x895d, -10306)
	R_00_15(rH0, rH1, rH2, rH3, rW12, rW13, 25, 20, 48,
		0x6b90, 0x1122, 0xfd98, 0x7193)
	R_00_15(rH2, rH3, rH0, rH1, rW14, rW15, 15, 10, 56,
		0xa679, 0x438e, 0x49b4, 0x0821)

	R_16_31(rH0, rH1, rH2, rH3, rW01, rW06, 27, 23,
		0x0d56, 0x6e0c, 0x1810, 0x6d2d)
	R_16_31(rH2, rH3, rH0, rH1, rW11, rW00, 18, 12,
		0x9d02, -32109, 0x124c, 0x2332)
	R_16_31(rH0, rH1, rH2, rH3, rW05, rW10, 27, 23,
		0x8ea7, 0x4a33, 0x0245, -18270)
	R_16_31(rH2, rH3, rH0, rH1, rW15, rW04, 18, 12,
		0x8eee,  -8608, 0xf258,  -5095)
	R_16_31(rH0, rH1, rH2, rH3, rW09, rW14, 27, 23,
		0x969d, -10697, 0x1cbe, -15288)
	R_16_31(rH2, rH3, rH0, rH1, rW03, rW08, 18, 12,
		0x3317, 0x3e99, 0xdbd9, 0x7c15)
	R_16_31(rH0, rH1, rH2, rH3, rW13, rW02, 27, 23,
		0xac4b, 0x7772, 0xd8cf, 0x331d)
	R_16_31(rH2, rH3, rH0, rH1, rW07, rW12, 18, 12,
		0x6a28, 0x6dd8, 0x219a, 0x3b68)

	R_32_47(rH0, rH1, rH2, rH3, rW05, rW08, 28, 21,
		0x29cb, 0x28e5, 0x4218,  -7788)
	R_32_47(rH2, rH3, rH0, rH1, rW11, rW14, 16,  9,
		0x473f, 0x06d1, 0x3aae, 0x3036)
	R_32_47(rH0, rH1, rH2, rH3, rW01, rW04, 28, 21,
		0xaea1, -15134, 0x640b, -11295)
	R_32_47(rH2, rH3, rH0, rH1, rW07, rW10, 16,  9,
		0x8f4c, 0x4887, 0xbc7c, -22499)
	R_32_47(rH0, rH1, rH2, rH3, rW13, rW00, 28, 21,
		0x7eb8, -27199, 0x00ea, 0x6050)
	R_32_47(rH2, rH3, rH0, rH1, rW03, rW06, 16,  9,
		0xe01a, 0x22fe, 0x4447, 0x69c5)
	R_32_47(rH0, rH1, rH2, rH3, rW09, rW12, 28, 21,
		0xb7f3, 0x0253, 0x59b1, 0x4d5b)
	R_32_47(rH2, rH3, rH0, rH1, rW15, rW02, 16,  9,
		0x4701, -27017, 0xc7bd, -19859)

	R_48_63(rH0, rH1, rH2, rH3, rW00, rW07, 26, 22,
		0x0988,  -1462, 0x4c70, -19401)
	R_48_63(rH2, rH3, rH0, rH1, rW14, rW05, 17, 11,
		0xadaf,  -5221, 0xfc99, 0x66f7)
	R_48_63(rH0, rH1, rH2, rH3, rW12, rW03, 26, 22,
		0x7e80, -16418, 0xba1e, -25587)
	R_48_63(rH2, rH3, rH0, rH1, rW10, rW01, 17, 11,
		0x4130, 0x380d, 0xe0c5, 0x738d)
	lwz		rW00,0(rHP)
	R_48_63(rH0, rH1, rH2, rH3, rW08, rW15, 26, 22,
		0xe837, -30770, 0xde8a, 0x69e8)
	lwz		rW14,4(rHP)
	R_48_63(rH2, rH3, rH0, rH1, rW06, rW13, 17, 11,
		0x9e79, 0x260f, 0x256d, -27941)
	lwz		rW12,8(rHP)
	R_48_63(rH0, rH1, rH2, rH3, rW04, rW11, 26, 22,
		0xab75, -20775, 0x4f9e, -28397)
	lwz		rW10,12(rHP)
	R_48_63(rH2, rH3, rH0, rH1, rW02, rW09, 17, 11,
		0x662b, 0x7c56, 0x11b2, 0x0358)

	add		rH0,rH0,rW00
	stw		rH0,0(rHP)
	add		rH1,rH1,rW14
	stw		rH1,4(rHP)
	add		rH2,rH2,rW12
	stw		rH2,8(rHP)
	add		rH3,rH3,rW10
	stw		rH3,12(rHP)
	NEXT_BLOCK

	bdnz		ppc_md5_main

	FINALIZE
	blr
