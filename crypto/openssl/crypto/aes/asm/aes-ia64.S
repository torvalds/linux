// Copyright 2004-2016 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the OpenSSL license (the "License").  You may not use
// this file except in compliance with the License.  You can obtain a copy
// in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html
//
// ====================================================================
// Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
// project. Rights for redistribution and usage in source and binary
// forms are granted according to the OpenSSL license.
// ====================================================================
//
// What's wrong with compiler generated code? Compiler never uses
// variable 'shr' which is pairable with 'extr'/'dep' instructions.
// Then it uses 'zxt' which is an I-type, but can be replaced with
// 'and' which in turn can be assigned to M-port [there're double as
// much M-ports as there're I-ports on Itanium 2]. By sacrificing few
// registers for small constants (255, 24 and 16) to be used with
// 'shr' and 'and' instructions I can achieve better ILP, Instruction
// Level Parallelism, and performance. This code outperforms GCC 3.3
// generated code by over factor of 2 (two), GCC 3.4 - by 70% and
// HP C - by 40%. Measured best-case scenario, i.e. aligned
// big-endian input, ECB timing on Itanium 2 is (18 + 13*rounds)
// ticks per block, or 9.25 CPU cycles per byte for 128 bit key.

// Version 1.2 mitigates the hazard of cache-timing attacks by
// a) compressing S-boxes from 8KB to 2KB+256B, b) scheduling
// references to S-boxes for L2 cache latency, c) prefetching T[ed]4
// prior last round. As result performance dropped to (26 + 15*rounds)
// ticks per block or 11 cycles per byte processed with 128-bit key.
// This is ~16% deterioration. For reference Itanium 2 L1 cache has
// 64 bytes line size and L2 - 128 bytes...

.ident	"aes-ia64.S, version 1.2"
.ident	"IA-64 ISA artwork by Andy Polyakov <appro@openssl.org>"
.explicit
.text

rk0=r8;     rk1=r9;

pfssave=r2;
lcsave=r10;
prsave=r3;
maskff=r11;
twenty4=r14;
sixteen=r15;

te00=r16;   te11=r17;   te22=r18;   te33=r19;
te01=r20;   te12=r21;   te23=r22;   te30=r23;
te02=r24;   te13=r25;   te20=r26;   te31=r27;
te03=r28;   te10=r29;   te21=r30;   te32=r31;

// these are rotating...
t0=r32;     s0=r33;
t1=r34;     s1=r35;
t2=r36;     s2=r37;
t3=r38;     s3=r39;

te0=r40;    te1=r41;    te2=r42;    te3=r43;

#if defined(_HPUX_SOURCE) && !defined(_LP64)
# define ADDP	addp4
#else
# define ADDP	add
#endif

// Offsets from Te0
#define TE0	0
#define TE2	2
#if defined(_HPUX_SOURCE) || defined(B_ENDIAN)
#define TE1	3
#define TE3	1
#else
#define TE1	1
#define TE3	3
#endif

// This implies that AES_KEY comprises 32-bit key schedule elements
// even on LP64 platforms.
#ifndef	KSZ
# define KSZ	4
# define LDKEY	ld4
#endif

.proc	_ia64_AES_encrypt#
// Input:	rk0-rk1
//		te0
//		te3	as AES_KEY->rounds!!!
//		s0-s3
//		maskff,twenty4,sixteen
// Output:	r16,r20,r24,r28 as s0-s3
// Clobber:	r16-r31,rk0-rk1,r32-r43
.align	32
_ia64_AES_encrypt:
	.prologue
	.altrp	b6
	.body
{ .mmi;	alloc	r16=ar.pfs,12,0,0,8
	LDKEY	t0=[rk0],2*KSZ
	mov	pr.rot=1<<16	}
{ .mmi;	LDKEY	t1=[rk1],2*KSZ
	add	te1=TE1,te0
	add	te3=-3,te3	};;
{ .mib;	LDKEY	t2=[rk0],2*KSZ
	mov	ar.ec=2		}
{ .mib;	LDKEY	t3=[rk1],2*KSZ
	add	te2=TE2,te0
	brp.loop.imp	.Le_top,.Le_end-16	};;

{ .mmi;	xor	s0=s0,t0
	xor	s1=s1,t1
	mov	ar.lc=te3	}
{ .mmi;	xor	s2=s2,t2
	xor	s3=s3,t3
	add	te3=TE3,te0	};;

.align	32
.Le_top:
{ .mmi;	(p0)	LDKEY	t0=[rk0],2*KSZ		// 0/0:rk[0]
	(p0)	and	te33=s3,maskff		// 0/0:s3&0xff
	(p0)	extr.u	te22=s2,8,8	}	// 0/0:s2>>8&0xff
{ .mmi; (p0)	LDKEY	t1=[rk1],2*KSZ		// 0/1:rk[1]
	(p0)	and	te30=s0,maskff		// 0/1:s0&0xff
	(p0)	shr.u	te00=s0,twenty4	};;	// 0/0:s0>>24
{ .mmi;	(p0)	LDKEY	t2=[rk0],2*KSZ		// 1/2:rk[2]
	(p0)	shladd	te33=te33,3,te3		// 1/0:te0+s0>>24
	(p0)	extr.u	te23=s3,8,8	}	// 1/1:s3>>8&0xff
{ .mmi;	(p0)	LDKEY	t3=[rk1],2*KSZ		// 1/3:rk[3]
	(p0)	shladd	te30=te30,3,te3		// 1/1:te3+s0
	(p0)	shr.u	te01=s1,twenty4	};;	// 1/1:s1>>24
{ .mmi;	(p0)	ld4	te33=[te33]		// 2/0:te3[s3&0xff]
	(p0)	shladd	te22=te22,3,te2		// 2/0:te2+s2>>8&0xff
	(p0)	extr.u	te20=s0,8,8	}	// 2/2:s0>>8&0xff
{ .mmi;	(p0)	ld4	te30=[te30]		// 2/1:te3[s0]
	(p0)	shladd	te23=te23,3,te2		// 2/1:te2+s3>>8
	(p0)	shr.u	te02=s2,twenty4	};;	// 2/2:s2>>24
{ .mmi;	(p0)	ld4	te22=[te22]		// 3/0:te2[s2>>8]
	(p0)	shladd	te20=te20,3,te2		// 3/2:te2+s0>>8
	(p0)	extr.u	te21=s1,8,8	}	// 3/3:s1>>8&0xff
{ .mmi;	(p0)	ld4	te23=[te23]		// 3/1:te2[s3>>8]
	(p0)	shladd	te00=te00,3,te0		// 3/0:te0+s0>>24
	(p0)	shr.u	te03=s3,twenty4	};;	// 3/3:s3>>24
{ .mmi;	(p0)	ld4	te20=[te20]		// 4/2:te2[s0>>8]
	(p0)	shladd	te21=te21,3,te2		// 4/3:te3+s2
	(p0)	extr.u	te11=s1,16,8	}	// 4/0:s1>>16&0xff
{ .mmi;	(p0)	ld4	te00=[te00]		// 4/0:te0[s0>>24]
	(p0)	shladd	te01=te01,3,te0		// 4/1:te0+s1>>24
	(p0)	shr.u	te13=s3,sixteen	};;	// 4/2:s3>>16
{ .mmi;	(p0)	ld4	te21=[te21]		// 5/3:te2[s1>>8]
	(p0)	shladd	te11=te11,3,te1		// 5/0:te1+s1>>16
	(p0)	extr.u	te12=s2,16,8	}	// 5/1:s2>>16&0xff
{ .mmi;	(p0)	ld4	te01=[te01]		// 5/1:te0[s1>>24]
	(p0)	shladd	te02=te02,3,te0		// 5/2:te0+s2>>24
	(p0)	and	te31=s1,maskff	};;	// 5/2:s1&0xff
{ .mmi;	(p0)	ld4	te11=[te11]		// 6/0:te1[s1>>16]
	(p0)	shladd	te12=te12,3,te1		// 6/1:te1+s2>>16
	(p0)	extr.u	te10=s0,16,8	}	// 6/3:s0>>16&0xff
{ .mmi;	(p0)	ld4	te02=[te02]		// 6/2:te0[s2>>24]
	(p0)	shladd	te03=te03,3,te0		// 6/3:te1+s0>>16
	(p0)	and	te32=s2,maskff	};;	// 6/3:s2&0xff

{ .mmi;	(p0)	ld4	te12=[te12]		// 7/1:te1[s2>>16]
	(p0)	shladd	te31=te31,3,te3		// 7/2:te3+s1&0xff
	(p0)	and	te13=te13,maskff}	// 7/2:s3>>16&0xff
{ .mmi;	(p0)	ld4	te03=[te03]		// 7/3:te0[s3>>24]
	(p0)	shladd	te32=te32,3,te3		// 7/3:te3+s2
	(p0)	xor	t0=t0,te33	};;	// 7/0:
{ .mmi;	(p0)	ld4	te31=[te31]		// 8/2:te3[s1]
	(p0)	shladd	te13=te13,3,te1		// 8/2:te1+s3>>16
	(p0)	xor	t0=t0,te22	}	// 8/0:
{ .mmi;	(p0)	ld4	te32=[te32]		// 8/3:te3[s2]
	(p0)	shladd	te10=te10,3,te1		// 8/3:te1+s0>>16
	(p0)	xor	t1=t1,te30	};;	// 8/1:
{ .mmi;	(p0)	ld4	te13=[te13]		// 9/2:te1[s3>>16]
	(p0)	ld4	te10=[te10]		// 9/3:te1[s0>>16]
	(p0)	xor	t0=t0,te00	};;	// 9/0:		!L2 scheduling
{ .mmi;	(p0)	xor	t1=t1,te23		// 10[9]/1:	
	(p0)	xor	t2=t2,te20		// 10[9]/2:
	(p0)	xor	t3=t3,te21	};;	// 10[9]/3:
{ .mmi;	(p0)	xor	t0=t0,te11		// 11[10]/0:done!
	(p0)	xor	t1=t1,te01		// 11[10]/1:
	(p0)	xor	t2=t2,te02	};;	// 11[10]/2:	!L2 scheduling
{ .mmi;	(p0)	xor	t3=t3,te03		// 12[10]/3:
	(p16)	cmp.eq	p0,p17=r0,r0 	};;	// 12[10]/clear (p17)
{ .mmi;	(p0)	xor	t1=t1,te12		// 13[11]/1:done!
	(p0)	xor	t2=t2,te31		// 13[11]/2:
	(p0)	xor	t3=t3,te32	}	// 13[11]/3:
{ .mmi;	(p17)	add	te0=2048,te0		// 13[11]/
	(p17)	add	te1=2048+64-TE1,te1};;	// 13[11]/
{ .mib;	(p0)	xor	t2=t2,te13		// 14[12]/2:done!
	(p17)	add	te2=2048+128-TE2,te2}	// 14[12]/
{ .mib;	(p0)	xor	t3=t3,te10		// 14[12]/3:done!
	(p17)	add	te3=2048+192-TE3,te3	// 14[12]/
	br.ctop.sptk	.Le_top		};;
.Le_end:


{ .mmi;	ld8	te12=[te0]		// prefetch Te4
	ld8	te31=[te1]	}
{ .mmi;	ld8	te10=[te2]
	ld8	te32=[te3]	}

{ .mmi;	LDKEY	t0=[rk0],2*KSZ		// 0/0:rk[0]
	and	te33=s3,maskff		// 0/0:s3&0xff
	extr.u	te22=s2,8,8	}	// 0/0:s2>>8&0xff
{ .mmi; LDKEY	t1=[rk1],2*KSZ		// 0/1:rk[1]
	and	te30=s0,maskff		// 0/1:s0&0xff
	shr.u	te00=s0,twenty4	};;	// 0/0:s0>>24
{ .mmi;	LDKEY	t2=[rk0],2*KSZ		// 1/2:rk[2]
	add	te33=te33,te0		// 1/0:te0+s0>>24
	extr.u	te23=s3,8,8	}	// 1/1:s3>>8&0xff
{ .mmi;	LDKEY	t3=[rk1],2*KSZ		// 1/3:rk[3]
	add	te30=te30,te0		// 1/1:te0+s0
	shr.u	te01=s1,twenty4	};;	// 1/1:s1>>24
{ .mmi;	ld1	te33=[te33]		// 2/0:te0[s3&0xff]
	add	te22=te22,te0		// 2/0:te0+s2>>8&0xff
	extr.u	te20=s0,8,8	}	// 2/2:s0>>8&0xff
{ .mmi;	ld1	te30=[te30]		// 2/1:te0[s0]
	add	te23=te23,te0		// 2/1:te0+s3>>8
	shr.u	te02=s2,twenty4	};;	// 2/2:s2>>24
{ .mmi;	ld1	te22=[te22]		// 3/0:te0[s2>>8]
	add	te20=te20,te0		// 3/2:te0+s0>>8
	extr.u	te21=s1,8,8	}	// 3/3:s1>>8&0xff
{ .mmi;	ld1	te23=[te23]		// 3/1:te0[s3>>8]
	add	te00=te00,te0		// 3/0:te0+s0>>24
	shr.u	te03=s3,twenty4	};;	// 3/3:s3>>24
{ .mmi;	ld1	te20=[te20]		// 4/2:te0[s0>>8]
	add	te21=te21,te0		// 4/3:te0+s2
	extr.u	te11=s1,16,8	}	// 4/0:s1>>16&0xff
{ .mmi;	ld1	te00=[te00]		// 4/0:te0[s0>>24]
	add	te01=te01,te0		// 4/1:te0+s1>>24
	shr.u	te13=s3,sixteen	};;	// 4/2:s3>>16
{ .mmi;	ld1	te21=[te21]		// 5/3:te0[s1>>8]
	add	te11=te11,te0		// 5/0:te0+s1>>16
	extr.u	te12=s2,16,8	}	// 5/1:s2>>16&0xff
{ .mmi;	ld1	te01=[te01]		// 5/1:te0[s1>>24]
	add	te02=te02,te0		// 5/2:te0+s2>>24
	and	te31=s1,maskff	};;	// 5/2:s1&0xff
{ .mmi;	ld1	te11=[te11]		// 6/0:te0[s1>>16]
	add	te12=te12,te0		// 6/1:te0+s2>>16
	extr.u	te10=s0,16,8	}	// 6/3:s0>>16&0xff
{ .mmi;	ld1	te02=[te02]		// 6/2:te0[s2>>24]
	add	te03=te03,te0		// 6/3:te0+s0>>16
	and	te32=s2,maskff	};;	// 6/3:s2&0xff

{ .mmi;	ld1	te12=[te12]		// 7/1:te0[s2>>16]
	add	te31=te31,te0		// 7/2:te0+s1&0xff
	dep	te33=te22,te33,8,8}	// 7/0:
{ .mmi;	ld1	te03=[te03]		// 7/3:te0[s3>>24]
	add	te32=te32,te0		// 7/3:te0+s2
	and	te13=te13,maskff};;	// 7/2:s3>>16&0xff
{ .mmi;	ld1	te31=[te31]		// 8/2:te0[s1]
	add	te13=te13,te0		// 8/2:te0+s3>>16
	dep	te30=te23,te30,8,8}	// 8/1:
{ .mmi;	ld1	te32=[te32]		// 8/3:te0[s2]
	add	te10=te10,te0		// 8/3:te0+s0>>16
	shl	te00=te00,twenty4};;	// 8/0:
{ .mii;	ld1	te13=[te13]		// 9/2:te0[s3>>16]
	dep	te33=te11,te33,16,8	// 9/0:
	shl	te01=te01,twenty4};;	// 9/1:
{ .mii;	ld1	te10=[te10]		// 10/3:te0[s0>>16]
	dep	te31=te20,te31,8,8	// 10/2:
	shl	te02=te02,twenty4};;	// 10/2:
{ .mii;	xor	t0=t0,te33		// 11/0:
	dep	te32=te21,te32,8,8	// 11/3:
	shl	te12=te12,sixteen};;	// 11/1:
{ .mii;	xor	r16=t0,te00		// 12/0:done!
	dep	te31=te13,te31,16,8	// 12/2:
	shl	te03=te03,twenty4};;	// 12/3:
{ .mmi;	xor	t1=t1,te01		// 13/1:
	xor	t2=t2,te02		// 13/2:
	dep	te32=te10,te32,16,8};;	// 13/3:
{ .mmi;	xor	t1=t1,te30		// 14/1:
	xor	r24=t2,te31		// 14/2:done!
	xor	t3=t3,te32	};;	// 14/3:
{ .mib;	xor	r20=t1,te12		// 15/1:done!
	xor	r28=t3,te03		// 15/3:done!
	br.ret.sptk	b6	};;
.endp	_ia64_AES_encrypt#

// void AES_encrypt (const void *in,void *out,const AES_KEY *key);
.global	AES_encrypt#
.proc	AES_encrypt#
.align	32
AES_encrypt:
	.prologue
	.save	ar.pfs,pfssave
{ .mmi;	alloc	pfssave=ar.pfs,3,1,12,0
	and	out0=3,in0
	mov	r3=ip			}
{ .mmi;	ADDP	in0=0,in0
	mov	loc0=psr.um
	ADDP	out11=KSZ*60,in2	};;	// &AES_KEY->rounds

{ .mmi;	ld4	out11=[out11]			// AES_KEY->rounds
	add	out8=(AES_Te#-AES_encrypt#),r3	// Te0
	.save	pr,prsave
	mov	prsave=pr		}
{ .mmi;	rum	1<<3				// clear um.ac
	.save	ar.lc,lcsave
	mov	lcsave=ar.lc		};;

	.body
#if defined(_HPUX_SOURCE)	// HPUX is big-endian, cut 15+15 cycles...
{ .mib; cmp.ne	p6,p0=out0,r0
	add	out0=4,in0
(p6)	br.dpnt.many	.Le_i_unaligned	};;

{ .mmi;	ld4	out1=[in0],8		// s0
	and	out9=3,in1
	mov	twenty4=24		}
{ .mmi;	ld4	out3=[out0],8		// s1
	ADDP	rk0=0,in2
	mov	sixteen=16		};;
{ .mmi;	ld4	out5=[in0]		// s2
	cmp.ne	p6,p0=out9,r0
	mov	maskff=0xff		}
{ .mmb;	ld4	out7=[out0]		// s3
	ADDP	rk1=KSZ,in2
	br.call.sptk.many	b6=_ia64_AES_encrypt	};;

{ .mib;	ADDP	in0=4,in1
	ADDP	in1=0,in1
(p6)	br.spnt	.Le_o_unaligned		};;

{ .mii;	mov	psr.um=loc0
	mov	ar.pfs=pfssave
	mov	ar.lc=lcsave		};;
{ .mmi;	st4	[in1]=r16,8		// s0
	st4	[in0]=r20,8		// s1
	mov	pr=prsave,0x1ffff	};;
{ .mmb;	st4	[in1]=r24		// s2
	st4	[in0]=r28		// s3
	br.ret.sptk.many	b0	};;
#endif

.align	32
.Le_i_unaligned:
{ .mmi;	add	out0=1,in0
	add	out2=2,in0
	add	out4=3,in0	};;
{ .mmi;	ld1	r16=[in0],4
	ld1	r17=[out0],4	}//;;
{ .mmi;	ld1	r18=[out2],4
	ld1	out1=[out4],4	};;	// s0
{ .mmi;	ld1	r20=[in0],4
	ld1	r21=[out0],4	}//;;
{ .mmi;	ld1	r22=[out2],4
	ld1	out3=[out4],4	};;	// s1
{ .mmi;	ld1	r24=[in0],4
	ld1	r25=[out0],4	}//;;
{ .mmi;	ld1	r26=[out2],4
	ld1	out5=[out4],4	};;	// s2
{ .mmi;	ld1	r28=[in0]
	ld1	r29=[out0]	}//;;
{ .mmi;	ld1	r30=[out2]
	ld1	out7=[out4]	};;	// s3

{ .mii;
	dep	out1=r16,out1,24,8	//;;
	dep	out3=r20,out3,24,8	}//;;
{ .mii;	ADDP	rk0=0,in2
	dep	out5=r24,out5,24,8	//;;
	dep	out7=r28,out7,24,8	};;
{ .mii;	ADDP	rk1=KSZ,in2
	dep	out1=r17,out1,16,8	//;;
	dep	out3=r21,out3,16,8	}//;;
{ .mii;	mov	twenty4=24
	dep	out5=r25,out5,16,8	//;;
	dep	out7=r29,out7,16,8	};;
{ .mii;	mov	sixteen=16
	dep	out1=r18,out1,8,8	//;;
	dep	out3=r22,out3,8,8	}//;;
{ .mii;	mov	maskff=0xff
	dep	out5=r26,out5,8,8	//;;
	dep	out7=r30,out7,8,8	};;

{ .mib;	br.call.sptk.many	b6=_ia64_AES_encrypt	};;

.Le_o_unaligned:
{ .mii;	ADDP	out0=0,in1
	extr.u	r17=r16,8,8			// s0
	shr.u	r19=r16,twenty4		}//;;
{ .mii;	ADDP	out1=1,in1
	extr.u	r18=r16,16,8
	shr.u	r23=r20,twenty4		}//;;	// s1
{ .mii;	ADDP	out2=2,in1
	extr.u	r21=r20,8,8
	shr.u	r22=r20,sixteen		}//;;
{ .mii;	ADDP	out3=3,in1
	extr.u	r25=r24,8,8			// s2
	shr.u	r27=r24,twenty4		};;
{ .mii;	st1	[out3]=r16,4
	extr.u	r26=r24,16,8
	shr.u	r31=r28,twenty4		}//;;	// s3
{ .mii;	st1	[out2]=r17,4
	extr.u	r29=r28,8,8
	shr.u	r30=r28,sixteen		}//;;

{ .mmi;	st1	[out1]=r18,4
	st1	[out0]=r19,4		};;
{ .mmi;	st1	[out3]=r20,4
	st1	[out2]=r21,4		}//;;
{ .mmi;	st1	[out1]=r22,4
	st1	[out0]=r23,4		};;
{ .mmi;	st1	[out3]=r24,4
	st1	[out2]=r25,4
	mov	pr=prsave,0x1ffff	}//;;
{ .mmi;	st1	[out1]=r26,4
	st1	[out0]=r27,4
	mov	ar.pfs=pfssave		};;
{ .mmi;	st1	[out3]=r28
	st1	[out2]=r29
	mov	ar.lc=lcsave		}//;;
{ .mmi;	st1	[out1]=r30
	st1	[out0]=r31		}
{ .mfb;	mov	psr.um=loc0			// restore user mask
	br.ret.sptk.many	b0	};;
.endp	AES_encrypt#

// *AES_decrypt are autogenerated by the following script:
#if 0
#!/usr/bin/env perl
print "// *AES_decrypt are autogenerated by the following script:\n#if 0\n";
open(PROG,'<'.$0); while(<PROG>) { print; } close(PROG);
print "#endif\n";
while(<>) {
	$process=1	if (/\.proc\s+_ia64_AES_encrypt/);
	next		if (!$process);

	#s/te00=s0/td00=s0/;	s/te00/td00/g;
	s/te11=s1/td13=s3/;	s/te11/td13/g;
	#s/te22=s2/td22=s2/;	s/te22/td22/g;
	s/te33=s3/td31=s1/;	s/te33/td31/g;

	#s/te01=s1/td01=s1/;	s/te01/td01/g;
	s/te12=s2/td10=s0/;	s/te12/td10/g;
	#s/te23=s3/td23=s3/;	s/te23/td23/g;
	s/te30=s0/td32=s2/;	s/te30/td32/g;

	#s/te02=s2/td02=s2/;	s/te02/td02/g;
	s/te13=s3/td11=s1/;	s/te13/td11/g;
	#s/te20=s0/td20=s0/;	s/te20/td20/g;
	s/te31=s1/td33=s3/;	s/te31/td33/g;

	#s/te03=s3/td03=s3/;	s/te03/td03/g;
	s/te10=s0/td12=s2/;	s/te10/td12/g;
	#s/te21=s1/td21=s1/;	s/te21/td21/g;
	s/te32=s2/td30=s0/;	s/te32/td30/g;

	s/td/te/g;

	s/AES_encrypt/AES_decrypt/g;
	s/\.Le_/.Ld_/g;
	s/AES_Te#/AES_Td#/g;

	print;

	exit		if (/\.endp\s+AES_decrypt/);
}
#endif
.proc	_ia64_AES_decrypt#
// Input:	rk0-rk1
//		te0
//		te3	as AES_KEY->rounds!!!
//		s0-s3
//		maskff,twenty4,sixteen
// Output:	r16,r20,r24,r28 as s0-s3
// Clobber:	r16-r31,rk0-rk1,r32-r43
.align	32
_ia64_AES_decrypt:
	.prologue
	.altrp	b6
	.body
{ .mmi;	alloc	r16=ar.pfs,12,0,0,8
	LDKEY	t0=[rk0],2*KSZ
	mov	pr.rot=1<<16	}
{ .mmi;	LDKEY	t1=[rk1],2*KSZ
	add	te1=TE1,te0
	add	te3=-3,te3	};;
{ .mib;	LDKEY	t2=[rk0],2*KSZ
	mov	ar.ec=2		}
{ .mib;	LDKEY	t3=[rk1],2*KSZ
	add	te2=TE2,te0
	brp.loop.imp	.Ld_top,.Ld_end-16	};;

{ .mmi;	xor	s0=s0,t0
	xor	s1=s1,t1
	mov	ar.lc=te3	}
{ .mmi;	xor	s2=s2,t2
	xor	s3=s3,t3
	add	te3=TE3,te0	};;

.align	32
.Ld_top:
{ .mmi;	(p0)	LDKEY	t0=[rk0],2*KSZ		// 0/0:rk[0]
	(p0)	and	te31=s1,maskff		// 0/0:s3&0xff
	(p0)	extr.u	te22=s2,8,8	}	// 0/0:s2>>8&0xff
{ .mmi; (p0)	LDKEY	t1=[rk1],2*KSZ		// 0/1:rk[1]
	(p0)	and	te32=s2,maskff		// 0/1:s0&0xff
	(p0)	shr.u	te00=s0,twenty4	};;	// 0/0:s0>>24
{ .mmi;	(p0)	LDKEY	t2=[rk0],2*KSZ		// 1/2:rk[2]
	(p0)	shladd	te31=te31,3,te3		// 1/0:te0+s0>>24
	(p0)	extr.u	te23=s3,8,8	}	// 1/1:s3>>8&0xff
{ .mmi;	(p0)	LDKEY	t3=[rk1],2*KSZ		// 1/3:rk[3]
	(p0)	shladd	te32=te32,3,te3		// 1/1:te3+s0
	(p0)	shr.u	te01=s1,twenty4	};;	// 1/1:s1>>24
{ .mmi;	(p0)	ld4	te31=[te31]		// 2/0:te3[s3&0xff]
	(p0)	shladd	te22=te22,3,te2		// 2/0:te2+s2>>8&0xff
	(p0)	extr.u	te20=s0,8,8	}	// 2/2:s0>>8&0xff
{ .mmi;	(p0)	ld4	te32=[te32]		// 2/1:te3[s0]
	(p0)	shladd	te23=te23,3,te2		// 2/1:te2+s3>>8
	(p0)	shr.u	te02=s2,twenty4	};;	// 2/2:s2>>24
{ .mmi;	(p0)	ld4	te22=[te22]		// 3/0:te2[s2>>8]
	(p0)	shladd	te20=te20,3,te2		// 3/2:te2+s0>>8
	(p0)	extr.u	te21=s1,8,8	}	// 3/3:s1>>8&0xff
{ .mmi;	(p0)	ld4	te23=[te23]		// 3/1:te2[s3>>8]
	(p0)	shladd	te00=te00,3,te0		// 3/0:te0+s0>>24
	(p0)	shr.u	te03=s3,twenty4	};;	// 3/3:s3>>24
{ .mmi;	(p0)	ld4	te20=[te20]		// 4/2:te2[s0>>8]
	(p0)	shladd	te21=te21,3,te2		// 4/3:te3+s2
	(p0)	extr.u	te13=s3,16,8	}	// 4/0:s1>>16&0xff
{ .mmi;	(p0)	ld4	te00=[te00]		// 4/0:te0[s0>>24]
	(p0)	shladd	te01=te01,3,te0		// 4/1:te0+s1>>24
	(p0)	shr.u	te11=s1,sixteen	};;	// 4/2:s3>>16
{ .mmi;	(p0)	ld4	te21=[te21]		// 5/3:te2[s1>>8]
	(p0)	shladd	te13=te13,3,te1		// 5/0:te1+s1>>16
	(p0)	extr.u	te10=s0,16,8	}	// 5/1:s2>>16&0xff
{ .mmi;	(p0)	ld4	te01=[te01]		// 5/1:te0[s1>>24]
	(p0)	shladd	te02=te02,3,te0		// 5/2:te0+s2>>24
	(p0)	and	te33=s3,maskff	};;	// 5/2:s1&0xff
{ .mmi;	(p0)	ld4	te13=[te13]		// 6/0:te1[s1>>16]
	(p0)	shladd	te10=te10,3,te1		// 6/1:te1+s2>>16
	(p0)	extr.u	te12=s2,16,8	}	// 6/3:s0>>16&0xff
{ .mmi;	(p0)	ld4	te02=[te02]		// 6/2:te0[s2>>24]
	(p0)	shladd	te03=te03,3,te0		// 6/3:te1+s0>>16
	(p0)	and	te30=s0,maskff	};;	// 6/3:s2&0xff

{ .mmi;	(p0)	ld4	te10=[te10]		// 7/1:te1[s2>>16]
	(p0)	shladd	te33=te33,3,te3		// 7/2:te3+s1&0xff
	(p0)	and	te11=te11,maskff}	// 7/2:s3>>16&0xff
{ .mmi;	(p0)	ld4	te03=[te03]		// 7/3:te0[s3>>24]
	(p0)	shladd	te30=te30,3,te3		// 7/3:te3+s2
	(p0)	xor	t0=t0,te31	};;	// 7/0:
{ .mmi;	(p0)	ld4	te33=[te33]		// 8/2:te3[s1]
	(p0)	shladd	te11=te11,3,te1		// 8/2:te1+s3>>16
	(p0)	xor	t0=t0,te22	}	// 8/0:
{ .mmi;	(p0)	ld4	te30=[te30]		// 8/3:te3[s2]
	(p0)	shladd	te12=te12,3,te1		// 8/3:te1+s0>>16
	(p0)	xor	t1=t1,te32	};;	// 8/1:
{ .mmi;	(p0)	ld4	te11=[te11]		// 9/2:te1[s3>>16]
	(p0)	ld4	te12=[te12]		// 9/3:te1[s0>>16]
	(p0)	xor	t0=t0,te00	};;	// 9/0:		!L2 scheduling
{ .mmi;	(p0)	xor	t1=t1,te23		// 10[9]/1:	
	(p0)	xor	t2=t2,te20		// 10[9]/2:
	(p0)	xor	t3=t3,te21	};;	// 10[9]/3:
{ .mmi;	(p0)	xor	t0=t0,te13		// 11[10]/0:done!
	(p0)	xor	t1=t1,te01		// 11[10]/1:
	(p0)	xor	t2=t2,te02	};;	// 11[10]/2:	!L2 scheduling
{ .mmi;	(p0)	xor	t3=t3,te03		// 12[10]/3:
	(p16)	cmp.eq	p0,p17=r0,r0 	};;	// 12[10]/clear (p17)
{ .mmi;	(p0)	xor	t1=t1,te10		// 13[11]/1:done!
	(p0)	xor	t2=t2,te33		// 13[11]/2:
	(p0)	xor	t3=t3,te30	}	// 13[11]/3:
{ .mmi;	(p17)	add	te0=2048,te0		// 13[11]/
	(p17)	add	te1=2048+64-TE1,te1};;	// 13[11]/
{ .mib;	(p0)	xor	t2=t2,te11		// 14[12]/2:done!
	(p17)	add	te2=2048+128-TE2,te2}	// 14[12]/
{ .mib;	(p0)	xor	t3=t3,te12		// 14[12]/3:done!
	(p17)	add	te3=2048+192-TE3,te3	// 14[12]/
	br.ctop.sptk	.Ld_top		};;
.Ld_end:


{ .mmi;	ld8	te10=[te0]		// prefetch Td4
	ld8	te33=[te1]	}
{ .mmi;	ld8	te12=[te2]
	ld8	te30=[te3]	}

{ .mmi;	LDKEY	t0=[rk0],2*KSZ		// 0/0:rk[0]
	and	te31=s1,maskff		// 0/0:s3&0xff
	extr.u	te22=s2,8,8	}	// 0/0:s2>>8&0xff
{ .mmi; LDKEY	t1=[rk1],2*KSZ		// 0/1:rk[1]
	and	te32=s2,maskff		// 0/1:s0&0xff
	shr.u	te00=s0,twenty4	};;	// 0/0:s0>>24
{ .mmi;	LDKEY	t2=[rk0],2*KSZ		// 1/2:rk[2]
	add	te31=te31,te0		// 1/0:te0+s0>>24
	extr.u	te23=s3,8,8	}	// 1/1:s3>>8&0xff
{ .mmi;	LDKEY	t3=[rk1],2*KSZ		// 1/3:rk[3]
	add	te32=te32,te0		// 1/1:te0+s0
	shr.u	te01=s1,twenty4	};;	// 1/1:s1>>24
{ .mmi;	ld1	te31=[te31]		// 2/0:te0[s3&0xff]
	add	te22=te22,te0		// 2/0:te0+s2>>8&0xff
	extr.u	te20=s0,8,8	}	// 2/2:s0>>8&0xff
{ .mmi;	ld1	te32=[te32]		// 2/1:te0[s0]
	add	te23=te23,te0		// 2/1:te0+s3>>8
	shr.u	te02=s2,twenty4	};;	// 2/2:s2>>24
{ .mmi;	ld1	te22=[te22]		// 3/0:te0[s2>>8]
	add	te20=te20,te0		// 3/2:te0+s0>>8
	extr.u	te21=s1,8,8	}	// 3/3:s1>>8&0xff
{ .mmi;	ld1	te23=[te23]		// 3/1:te0[s3>>8]
	add	te00=te00,te0		// 3/0:te0+s0>>24
	shr.u	te03=s3,twenty4	};;	// 3/3:s3>>24
{ .mmi;	ld1	te20=[te20]		// 4/2:te0[s0>>8]
	add	te21=te21,te0		// 4/3:te0+s2
	extr.u	te13=s3,16,8	}	// 4/0:s1>>16&0xff
{ .mmi;	ld1	te00=[te00]		// 4/0:te0[s0>>24]
	add	te01=te01,te0		// 4/1:te0+s1>>24
	shr.u	te11=s1,sixteen	};;	// 4/2:s3>>16
{ .mmi;	ld1	te21=[te21]		// 5/3:te0[s1>>8]
	add	te13=te13,te0		// 5/0:te0+s1>>16
	extr.u	te10=s0,16,8	}	// 5/1:s2>>16&0xff
{ .mmi;	ld1	te01=[te01]		// 5/1:te0[s1>>24]
	add	te02=te02,te0		// 5/2:te0+s2>>24
	and	te33=s3,maskff	};;	// 5/2:s1&0xff
{ .mmi;	ld1	te13=[te13]		// 6/0:te0[s1>>16]
	add	te10=te10,te0		// 6/1:te0+s2>>16
	extr.u	te12=s2,16,8	}	// 6/3:s0>>16&0xff
{ .mmi;	ld1	te02=[te02]		// 6/2:te0[s2>>24]
	add	te03=te03,te0		// 6/3:te0+s0>>16
	and	te30=s0,maskff	};;	// 6/3:s2&0xff

{ .mmi;	ld1	te10=[te10]		// 7/1:te0[s2>>16]
	add	te33=te33,te0		// 7/2:te0+s1&0xff
	dep	te31=te22,te31,8,8}	// 7/0:
{ .mmi;	ld1	te03=[te03]		// 7/3:te0[s3>>24]
	add	te30=te30,te0		// 7/3:te0+s2
	and	te11=te11,maskff};;	// 7/2:s3>>16&0xff
{ .mmi;	ld1	te33=[te33]		// 8/2:te0[s1]
	add	te11=te11,te0		// 8/2:te0+s3>>16
	dep	te32=te23,te32,8,8}	// 8/1:
{ .mmi;	ld1	te30=[te30]		// 8/3:te0[s2]
	add	te12=te12,te0		// 8/3:te0+s0>>16
	shl	te00=te00,twenty4};;	// 8/0:
{ .mii;	ld1	te11=[te11]		// 9/2:te0[s3>>16]
	dep	te31=te13,te31,16,8	// 9/0:
	shl	te01=te01,twenty4};;	// 9/1:
{ .mii;	ld1	te12=[te12]		// 10/3:te0[s0>>16]
	dep	te33=te20,te33,8,8	// 10/2:
	shl	te02=te02,twenty4};;	// 10/2:
{ .mii;	xor	t0=t0,te31		// 11/0:
	dep	te30=te21,te30,8,8	// 11/3:
	shl	te10=te10,sixteen};;	// 11/1:
{ .mii;	xor	r16=t0,te00		// 12/0:done!
	dep	te33=te11,te33,16,8	// 12/2:
	shl	te03=te03,twenty4};;	// 12/3:
{ .mmi;	xor	t1=t1,te01		// 13/1:
	xor	t2=t2,te02		// 13/2:
	dep	te30=te12,te30,16,8};;	// 13/3:
{ .mmi;	xor	t1=t1,te32		// 14/1:
	xor	r24=t2,te33		// 14/2:done!
	xor	t3=t3,te30	};;	// 14/3:
{ .mib;	xor	r20=t1,te10		// 15/1:done!
	xor	r28=t3,te03		// 15/3:done!
	br.ret.sptk	b6	};;
.endp	_ia64_AES_decrypt#

// void AES_decrypt (const void *in,void *out,const AES_KEY *key);
.global	AES_decrypt#
.proc	AES_decrypt#
.align	32
AES_decrypt:
	.prologue
	.save	ar.pfs,pfssave
{ .mmi;	alloc	pfssave=ar.pfs,3,1,12,0
	and	out0=3,in0
	mov	r3=ip			}
{ .mmi;	ADDP	in0=0,in0
	mov	loc0=psr.um
	ADDP	out11=KSZ*60,in2	};;	// &AES_KEY->rounds

{ .mmi;	ld4	out11=[out11]			// AES_KEY->rounds
	add	out8=(AES_Td#-AES_decrypt#),r3	// Te0
	.save	pr,prsave
	mov	prsave=pr		}
{ .mmi;	rum	1<<3				// clear um.ac
	.save	ar.lc,lcsave
	mov	lcsave=ar.lc		};;

	.body
#if defined(_HPUX_SOURCE)	// HPUX is big-endian, cut 15+15 cycles...
{ .mib; cmp.ne	p6,p0=out0,r0
	add	out0=4,in0
(p6)	br.dpnt.many	.Ld_i_unaligned	};;

{ .mmi;	ld4	out1=[in0],8		// s0
	and	out9=3,in1
	mov	twenty4=24		}
{ .mmi;	ld4	out3=[out0],8		// s1
	ADDP	rk0=0,in2
	mov	sixteen=16		};;
{ .mmi;	ld4	out5=[in0]		// s2
	cmp.ne	p6,p0=out9,r0
	mov	maskff=0xff		}
{ .mmb;	ld4	out7=[out0]		// s3
	ADDP	rk1=KSZ,in2
	br.call.sptk.many	b6=_ia64_AES_decrypt	};;

{ .mib;	ADDP	in0=4,in1
	ADDP	in1=0,in1
(p6)	br.spnt	.Ld_o_unaligned		};;

{ .mii;	mov	psr.um=loc0
	mov	ar.pfs=pfssave
	mov	ar.lc=lcsave		};;
{ .mmi;	st4	[in1]=r16,8		// s0
	st4	[in0]=r20,8		// s1
	mov	pr=prsave,0x1ffff	};;
{ .mmb;	st4	[in1]=r24		// s2
	st4	[in0]=r28		// s3
	br.ret.sptk.many	b0	};;
#endif

.align	32
.Ld_i_unaligned:
{ .mmi;	add	out0=1,in0
	add	out2=2,in0
	add	out4=3,in0	};;
{ .mmi;	ld1	r16=[in0],4
	ld1	r17=[out0],4	}//;;
{ .mmi;	ld1	r18=[out2],4
	ld1	out1=[out4],4	};;	// s0
{ .mmi;	ld1	r20=[in0],4
	ld1	r21=[out0],4	}//;;
{ .mmi;	ld1	r22=[out2],4
	ld1	out3=[out4],4	};;	// s1
{ .mmi;	ld1	r24=[in0],4
	ld1	r25=[out0],4	}//;;
{ .mmi;	ld1	r26=[out2],4
	ld1	out5=[out4],4	};;	// s2
{ .mmi;	ld1	r28=[in0]
	ld1	r29=[out0]	}//;;
{ .mmi;	ld1	r30=[out2]
	ld1	out7=[out4]	};;	// s3

{ .mii;
	dep	out1=r16,out1,24,8	//;;
	dep	out3=r20,out3,24,8	}//;;
{ .mii;	ADDP	rk0=0,in2
	dep	out5=r24,out5,24,8	//;;
	dep	out7=r28,out7,24,8	};;
{ .mii;	ADDP	rk1=KSZ,in2
	dep	out1=r17,out1,16,8	//;;
	dep	out3=r21,out3,16,8	}//;;
{ .mii;	mov	twenty4=24
	dep	out5=r25,out5,16,8	//;;
	dep	out7=r29,out7,16,8	};;
{ .mii;	mov	sixteen=16
	dep	out1=r18,out1,8,8	//;;
	dep	out3=r22,out3,8,8	}//;;
{ .mii;	mov	maskff=0xff
	dep	out5=r26,out5,8,8	//;;
	dep	out7=r30,out7,8,8	};;

{ .mib;	br.call.sptk.many	b6=_ia64_AES_decrypt	};;

.Ld_o_unaligned:
{ .mii;	ADDP	out0=0,in1
	extr.u	r17=r16,8,8			// s0
	shr.u	r19=r16,twenty4		}//;;
{ .mii;	ADDP	out1=1,in1
	extr.u	r18=r16,16,8
	shr.u	r23=r20,twenty4		}//;;	// s1
{ .mii;	ADDP	out2=2,in1
	extr.u	r21=r20,8,8
	shr.u	r22=r20,sixteen		}//;;
{ .mii;	ADDP	out3=3,in1
	extr.u	r25=r24,8,8			// s2
	shr.u	r27=r24,twenty4		};;
{ .mii;	st1	[out3]=r16,4
	extr.u	r26=r24,16,8
	shr.u	r31=r28,twenty4		}//;;	// s3
{ .mii;	st1	[out2]=r17,4
	extr.u	r29=r28,8,8
	shr.u	r30=r28,sixteen		}//;;

{ .mmi;	st1	[out1]=r18,4
	st1	[out0]=r19,4		};;
{ .mmi;	st1	[out3]=r20,4
	st1	[out2]=r21,4		}//;;
{ .mmi;	st1	[out1]=r22,4
	st1	[out0]=r23,4		};;
{ .mmi;	st1	[out3]=r24,4
	st1	[out2]=r25,4
	mov	pr=prsave,0x1ffff	}//;;
{ .mmi;	st1	[out1]=r26,4
	st1	[out0]=r27,4
	mov	ar.pfs=pfssave		};;
{ .mmi;	st1	[out3]=r28
	st1	[out2]=r29
	mov	ar.lc=lcsave		}//;;
{ .mmi;	st1	[out1]=r30
	st1	[out0]=r31		}
{ .mfb;	mov	psr.um=loc0			// restore user mask
	br.ret.sptk.many	b0	};;
.endp	AES_decrypt#

// leave it in .text segment...
.align	64
.global	AES_Te#
.type	AES_Te#,@object
AES_Te:	data4	0xc66363a5,0xc66363a5, 0xf87c7c84,0xf87c7c84
	data4	0xee777799,0xee777799, 0xf67b7b8d,0xf67b7b8d
	data4	0xfff2f20d,0xfff2f20d, 0xd66b6bbd,0xd66b6bbd
	data4	0xde6f6fb1,0xde6f6fb1, 0x91c5c554,0x91c5c554
	data4	0x60303050,0x60303050, 0x02010103,0x02010103
	data4	0xce6767a9,0xce6767a9, 0x562b2b7d,0x562b2b7d
	data4	0xe7fefe19,0xe7fefe19, 0xb5d7d762,0xb5d7d762
	data4	0x4dababe6,0x4dababe6, 0xec76769a,0xec76769a
	data4	0x8fcaca45,0x8fcaca45, 0x1f82829d,0x1f82829d
	data4	0x89c9c940,0x89c9c940, 0xfa7d7d87,0xfa7d7d87
	data4	0xeffafa15,0xeffafa15, 0xb25959eb,0xb25959eb
	data4	0x8e4747c9,0x8e4747c9, 0xfbf0f00b,0xfbf0f00b
	data4	0x41adadec,0x41adadec, 0xb3d4d467,0xb3d4d467
	data4	0x5fa2a2fd,0x5fa2a2fd, 0x45afafea,0x45afafea
	data4	0x239c9cbf,0x239c9cbf, 0x53a4a4f7,0x53a4a4f7
	data4	0xe4727296,0xe4727296, 0x9bc0c05b,0x9bc0c05b
	data4	0x75b7b7c2,0x75b7b7c2, 0xe1fdfd1c,0xe1fdfd1c
	data4	0x3d9393ae,0x3d9393ae, 0x4c26266a,0x4c26266a
	data4	0x6c36365a,0x6c36365a, 0x7e3f3f41,0x7e3f3f41
	data4	0xf5f7f702,0xf5f7f702, 0x83cccc4f,0x83cccc4f
	data4	0x6834345c,0x6834345c, 0x51a5a5f4,0x51a5a5f4
	data4	0xd1e5e534,0xd1e5e534, 0xf9f1f108,0xf9f1f108
	data4	0xe2717193,0xe2717193, 0xabd8d873,0xabd8d873
	data4	0x62313153,0x62313153, 0x2a15153f,0x2a15153f
	data4	0x0804040c,0x0804040c, 0x95c7c752,0x95c7c752
	data4	0x46232365,0x46232365, 0x9dc3c35e,0x9dc3c35e
	data4	0x30181828,0x30181828, 0x379696a1,0x379696a1
	data4	0x0a05050f,0x0a05050f, 0x2f9a9ab5,0x2f9a9ab5
	data4	0x0e070709,0x0e070709, 0x24121236,0x24121236
	data4	0x1b80809b,0x1b80809b, 0xdfe2e23d,0xdfe2e23d
	data4	0xcdebeb26,0xcdebeb26, 0x4e272769,0x4e272769
	data4	0x7fb2b2cd,0x7fb2b2cd, 0xea75759f,0xea75759f
	data4	0x1209091b,0x1209091b, 0x1d83839e,0x1d83839e
	data4	0x582c2c74,0x582c2c74, 0x341a1a2e,0x341a1a2e
	data4	0x361b1b2d,0x361b1b2d, 0xdc6e6eb2,0xdc6e6eb2
	data4	0xb45a5aee,0xb45a5aee, 0x5ba0a0fb,0x5ba0a0fb
	data4	0xa45252f6,0xa45252f6, 0x763b3b4d,0x763b3b4d
	data4	0xb7d6d661,0xb7d6d661, 0x7db3b3ce,0x7db3b3ce
	data4	0x5229297b,0x5229297b, 0xdde3e33e,0xdde3e33e
	data4	0x5e2f2f71,0x5e2f2f71, 0x13848497,0x13848497
	data4	0xa65353f5,0xa65353f5, 0xb9d1d168,0xb9d1d168
	data4	0x00000000,0x00000000, 0xc1eded2c,0xc1eded2c
	data4	0x40202060,0x40202060, 0xe3fcfc1f,0xe3fcfc1f
	data4	0x79b1b1c8,0x79b1b1c8, 0xb65b5bed,0xb65b5bed
	data4	0xd46a6abe,0xd46a6abe, 0x8dcbcb46,0x8dcbcb46
	data4	0x67bebed9,0x67bebed9, 0x7239394b,0x7239394b
	data4	0x944a4ade,0x944a4ade, 0x984c4cd4,0x984c4cd4
	data4	0xb05858e8,0xb05858e8, 0x85cfcf4a,0x85cfcf4a
	data4	0xbbd0d06b,0xbbd0d06b, 0xc5efef2a,0xc5efef2a
	data4	0x4faaaae5,0x4faaaae5, 0xedfbfb16,0xedfbfb16
	data4	0x864343c5,0x864343c5, 0x9a4d4dd7,0x9a4d4dd7
	data4	0x66333355,0x66333355, 0x11858594,0x11858594
	data4	0x8a4545cf,0x8a4545cf, 0xe9f9f910,0xe9f9f910
	data4	0x04020206,0x04020206, 0xfe7f7f81,0xfe7f7f81
	data4	0xa05050f0,0xa05050f0, 0x783c3c44,0x783c3c44
	data4	0x259f9fba,0x259f9fba, 0x4ba8a8e3,0x4ba8a8e3
	data4	0xa25151f3,0xa25151f3, 0x5da3a3fe,0x5da3a3fe
	data4	0x804040c0,0x804040c0, 0x058f8f8a,0x058f8f8a
	data4	0x3f9292ad,0x3f9292ad, 0x219d9dbc,0x219d9dbc
	data4	0x70383848,0x70383848, 0xf1f5f504,0xf1f5f504
	data4	0x63bcbcdf,0x63bcbcdf, 0x77b6b6c1,0x77b6b6c1
	data4	0xafdada75,0xafdada75, 0x42212163,0x42212163
	data4	0x20101030,0x20101030, 0xe5ffff1a,0xe5ffff1a
	data4	0xfdf3f30e,0xfdf3f30e, 0xbfd2d26d,0xbfd2d26d
	data4	0x81cdcd4c,0x81cdcd4c, 0x180c0c14,0x180c0c14
	data4	0x26131335,0x26131335, 0xc3ecec2f,0xc3ecec2f
	data4	0xbe5f5fe1,0xbe5f5fe1, 0x359797a2,0x359797a2
	data4	0x884444cc,0x884444cc, 0x2e171739,0x2e171739
	data4	0x93c4c457,0x93c4c457, 0x55a7a7f2,0x55a7a7f2
	data4	0xfc7e7e82,0xfc7e7e82, 0x7a3d3d47,0x7a3d3d47
	data4	0xc86464ac,0xc86464ac, 0xba5d5de7,0xba5d5de7
	data4	0x3219192b,0x3219192b, 0xe6737395,0xe6737395
	data4	0xc06060a0,0xc06060a0, 0x19818198,0x19818198
	data4	0x9e4f4fd1,0x9e4f4fd1, 0xa3dcdc7f,0xa3dcdc7f
	data4	0x44222266,0x44222266, 0x542a2a7e,0x542a2a7e
	data4	0x3b9090ab,0x3b9090ab, 0x0b888883,0x0b888883
	data4	0x8c4646ca,0x8c4646ca, 0xc7eeee29,0xc7eeee29
	data4	0x6bb8b8d3,0x6bb8b8d3, 0x2814143c,0x2814143c
	data4	0xa7dede79,0xa7dede79, 0xbc5e5ee2,0xbc5e5ee2
	data4	0x160b0b1d,0x160b0b1d, 0xaddbdb76,0xaddbdb76
	data4	0xdbe0e03b,0xdbe0e03b, 0x64323256,0x64323256
	data4	0x743a3a4e,0x743a3a4e, 0x140a0a1e,0x140a0a1e
	data4	0x924949db,0x924949db, 0x0c06060a,0x0c06060a
	data4	0x4824246c,0x4824246c, 0xb85c5ce4,0xb85c5ce4
	data4	0x9fc2c25d,0x9fc2c25d, 0xbdd3d36e,0xbdd3d36e
	data4	0x43acacef,0x43acacef, 0xc46262a6,0xc46262a6
	data4	0x399191a8,0x399191a8, 0x319595a4,0x319595a4
	data4	0xd3e4e437,0xd3e4e437, 0xf279798b,0xf279798b
	data4	0xd5e7e732,0xd5e7e732, 0x8bc8c843,0x8bc8c843
	data4	0x6e373759,0x6e373759, 0xda6d6db7,0xda6d6db7
	data4	0x018d8d8c,0x018d8d8c, 0xb1d5d564,0xb1d5d564
	data4	0x9c4e4ed2,0x9c4e4ed2, 0x49a9a9e0,0x49a9a9e0
	data4	0xd86c6cb4,0xd86c6cb4, 0xac5656fa,0xac5656fa
	data4	0xf3f4f407,0xf3f4f407, 0xcfeaea25,0xcfeaea25
	data4	0xca6565af,0xca6565af, 0xf47a7a8e,0xf47a7a8e
	data4	0x47aeaee9,0x47aeaee9, 0x10080818,0x10080818
	data4	0x6fbabad5,0x6fbabad5, 0xf0787888,0xf0787888
	data4	0x4a25256f,0x4a25256f, 0x5c2e2e72,0x5c2e2e72
	data4	0x381c1c24,0x381c1c24, 0x57a6a6f1,0x57a6a6f1
	data4	0x73b4b4c7,0x73b4b4c7, 0x97c6c651,0x97c6c651
	data4	0xcbe8e823,0xcbe8e823, 0xa1dddd7c,0xa1dddd7c
	data4	0xe874749c,0xe874749c, 0x3e1f1f21,0x3e1f1f21
	data4	0x964b4bdd,0x964b4bdd, 0x61bdbddc,0x61bdbddc
	data4	0x0d8b8b86,0x0d8b8b86, 0x0f8a8a85,0x0f8a8a85
	data4	0xe0707090,0xe0707090, 0x7c3e3e42,0x7c3e3e42
	data4	0x71b5b5c4,0x71b5b5c4, 0xcc6666aa,0xcc6666aa
	data4	0x904848d8,0x904848d8, 0x06030305,0x06030305
	data4	0xf7f6f601,0xf7f6f601, 0x1c0e0e12,0x1c0e0e12
	data4	0xc26161a3,0xc26161a3, 0x6a35355f,0x6a35355f
	data4	0xae5757f9,0xae5757f9, 0x69b9b9d0,0x69b9b9d0
	data4	0x17868691,0x17868691, 0x99c1c158,0x99c1c158
	data4	0x3a1d1d27,0x3a1d1d27, 0x279e9eb9,0x279e9eb9
	data4	0xd9e1e138,0xd9e1e138, 0xebf8f813,0xebf8f813
	data4	0x2b9898b3,0x2b9898b3, 0x22111133,0x22111133
	data4	0xd26969bb,0xd26969bb, 0xa9d9d970,0xa9d9d970
	data4	0x078e8e89,0x078e8e89, 0x339494a7,0x339494a7
	data4	0x2d9b9bb6,0x2d9b9bb6, 0x3c1e1e22,0x3c1e1e22
	data4	0x15878792,0x15878792, 0xc9e9e920,0xc9e9e920
	data4	0x87cece49,0x87cece49, 0xaa5555ff,0xaa5555ff
	data4	0x50282878,0x50282878, 0xa5dfdf7a,0xa5dfdf7a
	data4	0x038c8c8f,0x038c8c8f, 0x59a1a1f8,0x59a1a1f8
	data4	0x09898980,0x09898980, 0x1a0d0d17,0x1a0d0d17
	data4	0x65bfbfda,0x65bfbfda, 0xd7e6e631,0xd7e6e631
	data4	0x844242c6,0x844242c6, 0xd06868b8,0xd06868b8
	data4	0x824141c3,0x824141c3, 0x299999b0,0x299999b0
	data4	0x5a2d2d77,0x5a2d2d77, 0x1e0f0f11,0x1e0f0f11
	data4	0x7bb0b0cb,0x7bb0b0cb, 0xa85454fc,0xa85454fc
	data4	0x6dbbbbd6,0x6dbbbbd6, 0x2c16163a,0x2c16163a
// Te4:
	data1	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5
	data1	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76
	data1	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0
	data1	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0
	data1	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc
	data1	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15
	data1	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a
	data1	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75
	data1	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0
	data1	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84
	data1	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b
	data1	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf
	data1	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85
	data1	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8
	data1	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5
	data1	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2
	data1	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17
	data1	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73
	data1	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88
	data1	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb
	data1	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c
	data1	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79
	data1	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9
	data1	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08
	data1	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6
	data1	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a
	data1	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e
	data1	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e
	data1	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94
	data1	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf
	data1	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68
	data1	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
.size	AES_Te#,2048+256	// HP-UX assembler fails to ".-AES_Te#"

.align	64
.global	AES_Td#
.type	AES_Td#,@object
AES_Td:	data4	0x51f4a750,0x51f4a750, 0x7e416553,0x7e416553
	data4	0x1a17a4c3,0x1a17a4c3, 0x3a275e96,0x3a275e96
	data4	0x3bab6bcb,0x3bab6bcb, 0x1f9d45f1,0x1f9d45f1
	data4	0xacfa58ab,0xacfa58ab, 0x4be30393,0x4be30393
	data4	0x2030fa55,0x2030fa55, 0xad766df6,0xad766df6
	data4	0x88cc7691,0x88cc7691, 0xf5024c25,0xf5024c25
	data4	0x4fe5d7fc,0x4fe5d7fc, 0xc52acbd7,0xc52acbd7
	data4	0x26354480,0x26354480, 0xb562a38f,0xb562a38f
	data4	0xdeb15a49,0xdeb15a49, 0x25ba1b67,0x25ba1b67
	data4	0x45ea0e98,0x45ea0e98, 0x5dfec0e1,0x5dfec0e1
	data4	0xc32f7502,0xc32f7502, 0x814cf012,0x814cf012
	data4	0x8d4697a3,0x8d4697a3, 0x6bd3f9c6,0x6bd3f9c6
	data4	0x038f5fe7,0x038f5fe7, 0x15929c95,0x15929c95
	data4	0xbf6d7aeb,0xbf6d7aeb, 0x955259da,0x955259da
	data4	0xd4be832d,0xd4be832d, 0x587421d3,0x587421d3
	data4	0x49e06929,0x49e06929, 0x8ec9c844,0x8ec9c844
	data4	0x75c2896a,0x75c2896a, 0xf48e7978,0xf48e7978
	data4	0x99583e6b,0x99583e6b, 0x27b971dd,0x27b971dd
	data4	0xbee14fb6,0xbee14fb6, 0xf088ad17,0xf088ad17
	data4	0xc920ac66,0xc920ac66, 0x7dce3ab4,0x7dce3ab4
	data4	0x63df4a18,0x63df4a18, 0xe51a3182,0xe51a3182
	data4	0x97513360,0x97513360, 0x62537f45,0x62537f45
	data4	0xb16477e0,0xb16477e0, 0xbb6bae84,0xbb6bae84
	data4	0xfe81a01c,0xfe81a01c, 0xf9082b94,0xf9082b94
	data4	0x70486858,0x70486858, 0x8f45fd19,0x8f45fd19
	data4	0x94de6c87,0x94de6c87, 0x527bf8b7,0x527bf8b7
	data4	0xab73d323,0xab73d323, 0x724b02e2,0x724b02e2
	data4	0xe31f8f57,0xe31f8f57, 0x6655ab2a,0x6655ab2a
	data4	0xb2eb2807,0xb2eb2807, 0x2fb5c203,0x2fb5c203
	data4	0x86c57b9a,0x86c57b9a, 0xd33708a5,0xd33708a5
	data4	0x302887f2,0x302887f2, 0x23bfa5b2,0x23bfa5b2
	data4	0x02036aba,0x02036aba, 0xed16825c,0xed16825c
	data4	0x8acf1c2b,0x8acf1c2b, 0xa779b492,0xa779b492
	data4	0xf307f2f0,0xf307f2f0, 0x4e69e2a1,0x4e69e2a1
	data4	0x65daf4cd,0x65daf4cd, 0x0605bed5,0x0605bed5
	data4	0xd134621f,0xd134621f, 0xc4a6fe8a,0xc4a6fe8a
	data4	0x342e539d,0x342e539d, 0xa2f355a0,0xa2f355a0
	data4	0x058ae132,0x058ae132, 0xa4f6eb75,0xa4f6eb75
	data4	0x0b83ec39,0x0b83ec39, 0x4060efaa,0x4060efaa
	data4	0x5e719f06,0x5e719f06, 0xbd6e1051,0xbd6e1051
	data4	0x3e218af9,0x3e218af9, 0x96dd063d,0x96dd063d
	data4	0xdd3e05ae,0xdd3e05ae, 0x4de6bd46,0x4de6bd46
	data4	0x91548db5,0x91548db5, 0x71c45d05,0x71c45d05
	data4	0x0406d46f,0x0406d46f, 0x605015ff,0x605015ff
	data4	0x1998fb24,0x1998fb24, 0xd6bde997,0xd6bde997
	data4	0x894043cc,0x894043cc, 0x67d99e77,0x67d99e77
	data4	0xb0e842bd,0xb0e842bd, 0x07898b88,0x07898b88
	data4	0xe7195b38,0xe7195b38, 0x79c8eedb,0x79c8eedb
	data4	0xa17c0a47,0xa17c0a47, 0x7c420fe9,0x7c420fe9
	data4	0xf8841ec9,0xf8841ec9, 0x00000000,0x00000000
	data4	0x09808683,0x09808683, 0x322bed48,0x322bed48
	data4	0x1e1170ac,0x1e1170ac, 0x6c5a724e,0x6c5a724e
	data4	0xfd0efffb,0xfd0efffb, 0x0f853856,0x0f853856
	data4	0x3daed51e,0x3daed51e, 0x362d3927,0x362d3927
	data4	0x0a0fd964,0x0a0fd964, 0x685ca621,0x685ca621
	data4	0x9b5b54d1,0x9b5b54d1, 0x24362e3a,0x24362e3a
	data4	0x0c0a67b1,0x0c0a67b1, 0x9357e70f,0x9357e70f
	data4	0xb4ee96d2,0xb4ee96d2, 0x1b9b919e,0x1b9b919e
	data4	0x80c0c54f,0x80c0c54f, 0x61dc20a2,0x61dc20a2
	data4	0x5a774b69,0x5a774b69, 0x1c121a16,0x1c121a16
	data4	0xe293ba0a,0xe293ba0a, 0xc0a02ae5,0xc0a02ae5
	data4	0x3c22e043,0x3c22e043, 0x121b171d,0x121b171d
	data4	0x0e090d0b,0x0e090d0b, 0xf28bc7ad,0xf28bc7ad
	data4	0x2db6a8b9,0x2db6a8b9, 0x141ea9c8,0x141ea9c8
	data4	0x57f11985,0x57f11985, 0xaf75074c,0xaf75074c
	data4	0xee99ddbb,0xee99ddbb, 0xa37f60fd,0xa37f60fd
	data4	0xf701269f,0xf701269f, 0x5c72f5bc,0x5c72f5bc
	data4	0x44663bc5,0x44663bc5, 0x5bfb7e34,0x5bfb7e34
	data4	0x8b432976,0x8b432976, 0xcb23c6dc,0xcb23c6dc
	data4	0xb6edfc68,0xb6edfc68, 0xb8e4f163,0xb8e4f163
	data4	0xd731dcca,0xd731dcca, 0x42638510,0x42638510
	data4	0x13972240,0x13972240, 0x84c61120,0x84c61120
	data4	0x854a247d,0x854a247d, 0xd2bb3df8,0xd2bb3df8
	data4	0xaef93211,0xaef93211, 0xc729a16d,0xc729a16d
	data4	0x1d9e2f4b,0x1d9e2f4b, 0xdcb230f3,0xdcb230f3
	data4	0x0d8652ec,0x0d8652ec, 0x77c1e3d0,0x77c1e3d0
	data4	0x2bb3166c,0x2bb3166c, 0xa970b999,0xa970b999
	data4	0x119448fa,0x119448fa, 0x47e96422,0x47e96422
	data4	0xa8fc8cc4,0xa8fc8cc4, 0xa0f03f1a,0xa0f03f1a
	data4	0x567d2cd8,0x567d2cd8, 0x223390ef,0x223390ef
	data4	0x87494ec7,0x87494ec7, 0xd938d1c1,0xd938d1c1
	data4	0x8ccaa2fe,0x8ccaa2fe, 0x98d40b36,0x98d40b36
	data4	0xa6f581cf,0xa6f581cf, 0xa57ade28,0xa57ade28
	data4	0xdab78e26,0xdab78e26, 0x3fadbfa4,0x3fadbfa4
	data4	0x2c3a9de4,0x2c3a9de4, 0x5078920d,0x5078920d
	data4	0x6a5fcc9b,0x6a5fcc9b, 0x547e4662,0x547e4662
	data4	0xf68d13c2,0xf68d13c2, 0x90d8b8e8,0x90d8b8e8
	data4	0x2e39f75e,0x2e39f75e, 0x82c3aff5,0x82c3aff5
	data4	0x9f5d80be,0x9f5d80be, 0x69d0937c,0x69d0937c
	data4	0x6fd52da9,0x6fd52da9, 0xcf2512b3,0xcf2512b3
	data4	0xc8ac993b,0xc8ac993b, 0x10187da7,0x10187da7
	data4	0xe89c636e,0xe89c636e, 0xdb3bbb7b,0xdb3bbb7b
	data4	0xcd267809,0xcd267809, 0x6e5918f4,0x6e5918f4
	data4	0xec9ab701,0xec9ab701, 0x834f9aa8,0x834f9aa8
	data4	0xe6956e65,0xe6956e65, 0xaaffe67e,0xaaffe67e
	data4	0x21bccf08,0x21bccf08, 0xef15e8e6,0xef15e8e6
	data4	0xbae79bd9,0xbae79bd9, 0x4a6f36ce,0x4a6f36ce
	data4	0xea9f09d4,0xea9f09d4, 0x29b07cd6,0x29b07cd6
	data4	0x31a4b2af,0x31a4b2af, 0x2a3f2331,0x2a3f2331
	data4	0xc6a59430,0xc6a59430, 0x35a266c0,0x35a266c0
	data4	0x744ebc37,0x744ebc37, 0xfc82caa6,0xfc82caa6
	data4	0xe090d0b0,0xe090d0b0, 0x33a7d815,0x33a7d815
	data4	0xf104984a,0xf104984a, 0x41ecdaf7,0x41ecdaf7
	data4	0x7fcd500e,0x7fcd500e, 0x1791f62f,0x1791f62f
	data4	0x764dd68d,0x764dd68d, 0x43efb04d,0x43efb04d
	data4	0xccaa4d54,0xccaa4d54, 0xe49604df,0xe49604df
	data4	0x9ed1b5e3,0x9ed1b5e3, 0x4c6a881b,0x4c6a881b
	data4	0xc12c1fb8,0xc12c1fb8, 0x4665517f,0x4665517f
	data4	0x9d5eea04,0x9d5eea04, 0x018c355d,0x018c355d
	data4	0xfa877473,0xfa877473, 0xfb0b412e,0xfb0b412e
	data4	0xb3671d5a,0xb3671d5a, 0x92dbd252,0x92dbd252
	data4	0xe9105633,0xe9105633, 0x6dd64713,0x6dd64713
	data4	0x9ad7618c,0x9ad7618c, 0x37a10c7a,0x37a10c7a
	data4	0x59f8148e,0x59f8148e, 0xeb133c89,0xeb133c89
	data4	0xcea927ee,0xcea927ee, 0xb761c935,0xb761c935
	data4	0xe11ce5ed,0xe11ce5ed, 0x7a47b13c,0x7a47b13c
	data4	0x9cd2df59,0x9cd2df59, 0x55f2733f,0x55f2733f
	data4	0x1814ce79,0x1814ce79, 0x73c737bf,0x73c737bf
	data4	0x53f7cdea,0x53f7cdea, 0x5ffdaa5b,0x5ffdaa5b
	data4	0xdf3d6f14,0xdf3d6f14, 0x7844db86,0x7844db86
	data4	0xcaaff381,0xcaaff381, 0xb968c43e,0xb968c43e
	data4	0x3824342c,0x3824342c, 0xc2a3405f,0xc2a3405f
	data4	0x161dc372,0x161dc372, 0xbce2250c,0xbce2250c
	data4	0x283c498b,0x283c498b, 0xff0d9541,0xff0d9541
	data4	0x39a80171,0x39a80171, 0x080cb3de,0x080cb3de
	data4	0xd8b4e49c,0xd8b4e49c, 0x6456c190,0x6456c190
	data4	0x7bcb8461,0x7bcb8461, 0xd532b670,0xd532b670
	data4	0x486c5c74,0x486c5c74, 0xd0b85742,0xd0b85742
// Td4:
	data1	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38
	data1	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
	data1	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87
	data1	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
	data1	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d
	data1	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
	data1	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2
	data1	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
	data1	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16
	data1	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
	data1	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda
	data1	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
	data1	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a
	data1	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
	data1	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02
	data1	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
	data1	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea
	data1	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
	data1	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85
	data1	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
	data1	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89
	data1	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
	data1	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20
	data1	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
	data1	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31
	data1	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
	data1	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d
	data1	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
	data1	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0
	data1	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
	data1	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26
	data1	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
.size	AES_Td#,2048+256	// HP-UX assembler fails to ".-AES_Td#"
