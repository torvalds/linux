.explicit
.text
.ident	"ia64.S, Version 2.1"
.ident	"IA-64 ISA artwork by Andy Polyakov <appro@openssl.org>"

// Copyright 2001-2019 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the OpenSSL license (the "License").  You may not use
// this file except in compliance with the License.  You can obtain a copy
// in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html

//
// ====================================================================
// Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
// project.
//
// Rights for redistribution and usage in source and binary forms are
// granted according to the OpenSSL license. Warranty of any kind is
// disclaimed.
// ====================================================================
//
// Version 2.x is Itanium2 re-tune. Few words about how Itanium2 is
// different from Itanium to this module viewpoint. Most notably, is it
// "wider" than Itanium? Can you experience loop scalability as
// discussed in commentary sections? Not really:-( Itanium2 has 6
// integer ALU ports, i.e. it's 2 ports wider, but it's not enough to
// spin twice as fast, as I need 8 IALU ports. Amount of floating point
// ports is the same, i.e. 2, while I need 4. In other words, to this
// module Itanium2 remains effectively as "wide" as Itanium. Yet it's
// essentially different in respect to this module, and a re-tune was
// required. Well, because some instruction latencies has changed. Most
// noticeably those intensively used:
//
//			Itanium	Itanium2
//	ldf8		9	6		L2 hit
//	ld8		2	1		L1 hit
//	getf		2	5
//	xma[->getf]	7[+1]	4[+0]
//	add[->st8]	1[+1]	1[+0]
//
// What does it mean? You might ratiocinate that the original code
// should run just faster... Because sum of latencies is smaller...
// Wrong! Note that getf latency increased. This means that if a loop is
// scheduled for lower latency (as they were), then it will suffer from
// stall condition and the code will therefore turn anti-scalable, e.g.
// original bn_mul_words spun at 5*n or 2.5 times slower than expected
// on Itanium2! What to do? Reschedule loops for Itanium2? But then
// Itanium would exhibit anti-scalability. So I've chosen to reschedule
// for worst latency for every instruction aiming for best *all-round*
// performance.

// Q.	How much faster does it get?
// A.	Here is the output from 'openssl speed rsa dsa' for vanilla
//	0.9.6a compiled with gcc version 2.96 20000731 (Red Hat
//	Linux 7.1 2.96-81):
//
//	                  sign    verify    sign/s verify/s
//	rsa  512 bits   0.0036s   0.0003s    275.3   2999.2
//	rsa 1024 bits   0.0203s   0.0011s     49.3    894.1
//	rsa 2048 bits   0.1331s   0.0040s      7.5    250.9
//	rsa 4096 bits   0.9270s   0.0147s      1.1     68.1
//	                  sign    verify    sign/s verify/s
//	dsa  512 bits   0.0035s   0.0043s    288.3    234.8
//	dsa 1024 bits   0.0111s   0.0135s     90.0     74.2
//
//	And here is similar output but for this assembler
//	implementation:-)
//
//	                  sign    verify    sign/s verify/s
//	rsa  512 bits   0.0021s   0.0001s    549.4   9638.5
//	rsa 1024 bits   0.0055s   0.0002s    183.8   4481.1
//	rsa 2048 bits   0.0244s   0.0006s     41.4   1726.3
//	rsa 4096 bits   0.1295s   0.0018s      7.7    561.5
//	                  sign    verify    sign/s verify/s
//	dsa  512 bits   0.0012s   0.0013s    891.9    756.6
//	dsa 1024 bits   0.0023s   0.0028s    440.4    376.2
//	
//	Yes, you may argue that it's not fair comparison as it's
//	possible to craft the C implementation with BN_UMULT_HIGH
//	inline assembler macro. But of course! Here is the output
//	with the macro:
//
//	                  sign    verify    sign/s verify/s
//	rsa  512 bits   0.0020s   0.0002s    495.0   6561.0
//	rsa 1024 bits   0.0086s   0.0004s    116.2   2235.7
//	rsa 2048 bits   0.0519s   0.0015s     19.3    667.3
//	rsa 4096 bits   0.3464s   0.0053s      2.9    187.7
//	                  sign    verify    sign/s verify/s
//	dsa  512 bits   0.0016s   0.0020s    613.1    510.5
//	dsa 1024 bits   0.0045s   0.0054s    221.0    183.9
//
//	My code is still way faster, huh:-) And I believe that even
//	higher performance can be achieved. Note that as keys get
//	longer, performance gain is larger. Why? According to the
//	profiler there is another player in the field, namely
//	BN_from_montgomery consuming larger and larger portion of CPU
//	time as keysize decreases. I therefore consider putting effort
//	to assembler implementation of the following routine:
//
//	void bn_mul_add_mont (BN_ULONG *rp,BN_ULONG *np,int nl,BN_ULONG n0)
//	{
//	int      i,j;
//	BN_ULONG v;
//
//	for (i=0; i<nl; i++)
//		{
//		v=bn_mul_add_words(rp,np,nl,(rp[0]*n0)&BN_MASK2);
//		nrp++;
//		rp++;
//		if (((nrp[-1]+=v)&BN_MASK2) < v)
//			for (j=0; ((++nrp[j])&BN_MASK2) == 0; j++) ;
//		}
//	}
//
//	It might as well be beneficial to implement even combaX
//	variants, as it appears as it can literally unleash the
//	performance (see comment section to bn_mul_comba8 below).
//
//	And finally for your reference the output for 0.9.6a compiled
//	with SGIcc version 0.01.0-12 (keep in mind that for the moment
//	of this writing it's not possible to convince SGIcc to use
//	BN_UMULT_HIGH inline assembler macro, yet the code is fast,
//	i.e. for a compiler generated one:-):
//
//	                  sign    verify    sign/s verify/s
//	rsa  512 bits   0.0022s   0.0002s    452.7   5894.3
//	rsa 1024 bits   0.0097s   0.0005s    102.7   2002.9
//	rsa 2048 bits   0.0578s   0.0017s     17.3    600.2
//	rsa 4096 bits   0.3838s   0.0061s      2.6    164.5
//	                  sign    verify    sign/s verify/s
//	dsa  512 bits   0.0018s   0.0022s    547.3    459.6
//	dsa 1024 bits   0.0051s   0.0062s    196.6    161.3
//
//	Oh! Benchmarks were performed on 733MHz Lion-class Itanium
//	system running Redhat Linux 7.1 (very special thanks to Ray
//	McCaffity of Williams Communications for providing an account).
//
// Q.	What's the heck with 'rum 1<<5' at the end of every function?
// A.	Well, by clearing the "upper FP registers written" bit of the
//	User Mask I want to excuse the kernel from preserving upper
//	(f32-f128) FP register bank over process context switch, thus
//	minimizing bus bandwidth consumption during the switch (i.e.
//	after PKI operation completes and the program is off doing
//	something else like bulk symmetric encryption). Having said
//	this, I also want to point out that it might be good idea
//	to compile the whole toolkit (as well as majority of the
//	programs for that matter) with -mfixed-range=f32-f127 command
//	line option. No, it doesn't prevent the compiler from writing
//	to upper bank, but at least discourages to do so. If you don't
//	like the idea you have the option to compile the module with
//	-Drum=nop.m in command line.
//

#if defined(_HPUX_SOURCE) && !defined(_LP64)
#define	ADDP	addp4
#else
#define	ADDP	add
#endif
#ifdef __VMS
.alias abort, "decc$abort"
#endif

#if 1
//
// bn_[add|sub]_words routines.
//
// Loops are spinning in 2*(n+5) ticks on Itanium (provided that the
// data reside in L1 cache, i.e. 2 ticks away). It's possible to
// compress the epilogue and get down to 2*n+6, but at the cost of
// scalability (the neat feature of this implementation is that it
// shall automagically spin in n+5 on "wider" IA-64 implementations:-)
// I consider that the epilogue is short enough as it is to trade tiny
// performance loss on Itanium for scalability.
//
// BN_ULONG bn_add_words(BN_ULONG *rp, BN_ULONG *ap, BN_ULONG *bp,int num)
//
.global	bn_add_words#
.proc	bn_add_words#
.align	64
.skip	32	// makes the loop body aligned at 64-byte boundary
bn_add_words:
	.prologue
	.save	ar.pfs,r2
{ .mii;	alloc		r2=ar.pfs,4,12,0,16
	cmp4.le		p6,p0=r35,r0	};;
{ .mfb;	mov		r8=r0			// return value
(p6)	br.ret.spnt.many	b0	};;

{ .mib;	sub		r10=r35,r0,1
	.save	ar.lc,r3
	mov		r3=ar.lc
	brp.loop.imp	.L_bn_add_words_ctop,.L_bn_add_words_cend-16
					}
{ .mib;	ADDP		r14=0,r32		// rp
	.save	pr,r9
	mov		r9=pr		};;
	.body
{ .mii;	ADDP		r15=0,r33		// ap
	mov		ar.lc=r10
	mov		ar.ec=6		}
{ .mib;	ADDP		r16=0,r34		// bp
	mov		pr.rot=1<<16	};;

.L_bn_add_words_ctop:
{ .mii;	(p16)	ld8		r32=[r16],8	  // b=*(bp++)
	(p18)	add		r39=r37,r34
	(p19)	cmp.ltu.unc	p56,p0=r40,r38	}
{ .mfb;	(p0)	nop.m		0x0
	(p0)	nop.f		0x0
	(p0)	nop.b		0x0		}
{ .mii;	(p16)	ld8		r35=[r15],8	  // a=*(ap++)
	(p58)	cmp.eq.or	p57,p0=-1,r41	  // (p20)
	(p58)	add		r41=1,r41	} // (p20)
{ .mfb;	(p21)	st8		[r14]=r42,8	  // *(rp++)=r
	(p0)	nop.f		0x0
	br.ctop.sptk	.L_bn_add_words_ctop	};;
.L_bn_add_words_cend:

{ .mii;
(p59)	add		r8=1,r8		// return value
	mov		pr=r9,0x1ffff
	mov		ar.lc=r3	}
{ .mbb;	nop.b		0x0
	br.ret.sptk.many	b0	};;
.endp	bn_add_words#

//
// BN_ULONG bn_sub_words(BN_ULONG *rp, BN_ULONG *ap, BN_ULONG *bp,int num)
//
.global	bn_sub_words#
.proc	bn_sub_words#
.align	64
.skip	32	// makes the loop body aligned at 64-byte boundary
bn_sub_words:
	.prologue
	.save	ar.pfs,r2
{ .mii;	alloc		r2=ar.pfs,4,12,0,16
	cmp4.le		p6,p0=r35,r0	};;
{ .mfb;	mov		r8=r0			// return value
(p6)	br.ret.spnt.many	b0	};;

{ .mib;	sub		r10=r35,r0,1
	.save	ar.lc,r3
	mov		r3=ar.lc
	brp.loop.imp	.L_bn_sub_words_ctop,.L_bn_sub_words_cend-16
					}
{ .mib;	ADDP		r14=0,r32		// rp
	.save	pr,r9
	mov		r9=pr		};;
	.body
{ .mii;	ADDP		r15=0,r33		// ap
	mov		ar.lc=r10
	mov		ar.ec=6		}
{ .mib;	ADDP		r16=0,r34		// bp
	mov		pr.rot=1<<16	};;

.L_bn_sub_words_ctop:
{ .mii;	(p16)	ld8		r32=[r16],8	  // b=*(bp++)
	(p18)	sub		r39=r37,r34
	(p19)	cmp.gtu.unc	p56,p0=r40,r38	}
{ .mfb;	(p0)	nop.m		0x0
	(p0)	nop.f		0x0
	(p0)	nop.b		0x0		}
{ .mii;	(p16)	ld8		r35=[r15],8	  // a=*(ap++)
	(p58)	cmp.eq.or	p57,p0=0,r41	  // (p20)
	(p58)	add		r41=-1,r41	} // (p20)
{ .mbb;	(p21)	st8		[r14]=r42,8	  // *(rp++)=r
	(p0)	nop.b		0x0
	br.ctop.sptk	.L_bn_sub_words_ctop	};;
.L_bn_sub_words_cend:

{ .mii;
(p59)	add		r8=1,r8		// return value
	mov		pr=r9,0x1ffff
	mov		ar.lc=r3	}
{ .mbb;	nop.b		0x0
	br.ret.sptk.many	b0	};;
.endp	bn_sub_words#
#endif

#if 0
#define XMA_TEMPTATION
#endif

#if 1
//
// BN_ULONG bn_mul_words(BN_ULONG *rp, BN_ULONG *ap, int num, BN_ULONG w)
//
.global	bn_mul_words#
.proc	bn_mul_words#
.align	64
.skip	32	// makes the loop body aligned at 64-byte boundary
bn_mul_words:
	.prologue
	.save	ar.pfs,r2
#ifdef XMA_TEMPTATION
{ .mfi;	alloc		r2=ar.pfs,4,0,0,0	};;
#else
{ .mfi;	alloc		r2=ar.pfs,4,12,0,16	};;
#endif
{ .mib;	mov		r8=r0			// return value
	cmp4.le		p6,p0=r34,r0
(p6)	br.ret.spnt.many	b0		};;

{ .mii;	sub	r10=r34,r0,1
	.save	ar.lc,r3
	mov	r3=ar.lc
	.save	pr,r9
	mov	r9=pr			};;

	.body
{ .mib;	setf.sig	f8=r35	// w
	mov		pr.rot=0x800001<<16
			// ------^----- serves as (p50) at first (p27)
	brp.loop.imp	.L_bn_mul_words_ctop,.L_bn_mul_words_cend-16
					}

#ifndef XMA_TEMPTATION

{ .mmi;	ADDP		r14=0,r32	// rp
	ADDP		r15=0,r33	// ap
	mov		ar.lc=r10	}
{ .mmi;	mov		r40=0		// serves as r35 at first (p27)
	mov		ar.ec=13	};;

// This loop spins in 2*(n+12) ticks. It's scheduled for data in Itanium
// L2 cache (i.e. 9 ticks away) as floating point load/store instructions
// bypass L1 cache and L2 latency is actually best-case scenario for
// ldf8. The loop is not scalable and shall run in 2*(n+12) even on
// "wider" IA-64 implementations. It's a trade-off here. n+24 loop
// would give us ~5% in *overall* performance improvement on "wider"
// IA-64, but would hurt Itanium for about same because of longer
// epilogue. As it's a matter of few percents in either case I've
// chosen to trade the scalability for development time (you can see
// this very instruction sequence in bn_mul_add_words loop which in
// turn is scalable).
.L_bn_mul_words_ctop:
{ .mfi;	(p25)	getf.sig	r36=f52			// low
	(p21)	xmpy.lu		f48=f37,f8
	(p28)	cmp.ltu		p54,p50=r41,r39	}
{ .mfi;	(p16)	ldf8		f32=[r15],8
	(p21)	xmpy.hu		f40=f37,f8
	(p0)	nop.i		0x0		};;
{ .mii;	(p25)	getf.sig	r32=f44			// high
	.pred.rel	"mutex",p50,p54
	(p50)	add		r40=r38,r35		// (p27)
	(p54)	add		r40=r38,r35,1	}	// (p27)
{ .mfb;	(p28)	st8		[r14]=r41,8
	(p0)	nop.f		0x0
	br.ctop.sptk	.L_bn_mul_words_ctop	};;
.L_bn_mul_words_cend:

{ .mii;	nop.m		0x0
.pred.rel	"mutex",p51,p55
(p51)	add		r8=r36,r0
(p55)	add		r8=r36,r0,1	}
{ .mfb;	nop.m	0x0
	nop.f	0x0
	nop.b	0x0			}

#else	// XMA_TEMPTATION

	setf.sig	f37=r0	// serves as carry at (p18) tick
	mov		ar.lc=r10
	mov		ar.ec=5;;

// Most of you examining this code very likely wonder why in the name
// of Intel the following loop is commented out? Indeed, it looks so
// neat that you find it hard to believe that it's something wrong
// with it, right? The catch is that every iteration depends on the
// result from previous one and the latter isn't available instantly.
// The loop therefore spins at the latency of xma minus 1, or in other
// words at 6*(n+4) ticks:-( Compare to the "production" loop above
// that runs in 2*(n+11) where the low latency problem is worked around
// by moving the dependency to one-tick latent integer ALU. Note that
// "distance" between ldf8 and xma is not latency of ldf8, but the
// *difference* between xma and ldf8 latencies.
.L_bn_mul_words_ctop:
{ .mfi;	(p16)	ldf8		f32=[r33],8
	(p18)	xma.hu		f38=f34,f8,f39	}
{ .mfb;	(p20)	stf8		[r32]=f37,8
	(p18)	xma.lu		f35=f34,f8,f39
	br.ctop.sptk	.L_bn_mul_words_ctop	};;
.L_bn_mul_words_cend:

	getf.sig	r8=f41		// the return value

#endif	// XMA_TEMPTATION

{ .mii;	nop.m		0x0
	mov		pr=r9,0x1ffff
	mov		ar.lc=r3	}
{ .mfb;	rum		1<<5		// clear um.mfh
	nop.f		0x0
	br.ret.sptk.many	b0	};;
.endp	bn_mul_words#
#endif

#if 1
//
// BN_ULONG bn_mul_add_words(BN_ULONG *rp, BN_ULONG *ap, int num, BN_ULONG w)
//
.global	bn_mul_add_words#
.proc	bn_mul_add_words#
.align	64
.skip	48	// makes the loop body aligned at 64-byte boundary
bn_mul_add_words:
	.prologue
	.save	ar.pfs,r2
{ .mmi;	alloc		r2=ar.pfs,4,4,0,8
	cmp4.le		p6,p0=r34,r0
	.save	ar.lc,r3
	mov		r3=ar.lc	};;
{ .mib;	mov		r8=r0		// return value
	sub		r10=r34,r0,1
(p6)	br.ret.spnt.many	b0	};;

{ .mib;	setf.sig	f8=r35		// w
	.save	pr,r9
	mov		r9=pr
	brp.loop.imp	.L_bn_mul_add_words_ctop,.L_bn_mul_add_words_cend-16
					}
	.body
{ .mmi;	ADDP		r14=0,r32	// rp
	ADDP		r15=0,r33	// ap
	mov		ar.lc=r10	}
{ .mii;	ADDP		r16=0,r32	// rp copy
	mov		pr.rot=0x2001<<16
			// ------^----- serves as (p40) at first (p27)
	mov		ar.ec=11	};;

// This loop spins in 3*(n+10) ticks on Itanium and in 2*(n+10) on
// Itanium 2. Yes, unlike previous versions it scales:-) Previous
// version was performing *all* additions in IALU and was starving
// for those even on Itanium 2. In this version one addition is
// moved to FPU and is folded with multiplication. This is at cost
// of propagating the result from previous call to this subroutine
// to L2 cache... In other words negligible even for shorter keys.
// *Overall* performance improvement [over previous version] varies
// from 11 to 22 percent depending on key length.
.L_bn_mul_add_words_ctop:
.pred.rel	"mutex",p40,p42
{ .mfi;	(p23)	getf.sig	r36=f45			// low
	(p20)	xma.lu		f42=f36,f8,f50		// low
	(p40)	add		r39=r39,r35	}	// (p27)
{ .mfi;	(p16)	ldf8		f32=[r15],8		// *(ap++)
	(p20)	xma.hu		f36=f36,f8,f50		// high
	(p42)	add		r39=r39,r35,1	};;	// (p27)
{ .mmi;	(p24)	getf.sig	r32=f40			// high
	(p16)	ldf8		f46=[r16],8		// *(rp1++)
	(p40)	cmp.ltu		p41,p39=r39,r35	}	// (p27)
{ .mib;	(p26)	st8		[r14]=r39,8		// *(rp2++)
	(p42)	cmp.leu		p41,p39=r39,r35		// (p27)
	br.ctop.sptk	.L_bn_mul_add_words_ctop};;
.L_bn_mul_add_words_cend:

{ .mmi;	.pred.rel	"mutex",p40,p42
(p40)	add		r8=r35,r0
(p42)	add		r8=r35,r0,1
	mov		pr=r9,0x1ffff	}
{ .mib;	rum		1<<5		// clear um.mfh
	mov		ar.lc=r3
	br.ret.sptk.many	b0	};;
.endp	bn_mul_add_words#
#endif

#if 1
//
// void bn_sqr_words(BN_ULONG *rp, BN_ULONG *ap, int num)
//
.global	bn_sqr_words#
.proc	bn_sqr_words#
.align	64
.skip	32	// makes the loop body aligned at 64-byte boundary
bn_sqr_words:
	.prologue
	.save	ar.pfs,r2
{ .mii;	alloc		r2=ar.pfs,3,0,0,0
	sxt4		r34=r34		};;
{ .mii;	cmp.le		p6,p0=r34,r0
	mov		r8=r0		}	// return value
{ .mfb;	ADDP		r32=0,r32
	nop.f		0x0
(p6)	br.ret.spnt.many	b0	};;

{ .mii;	sub	r10=r34,r0,1
	.save	ar.lc,r3
	mov	r3=ar.lc
	.save	pr,r9
	mov	r9=pr			};;

	.body
{ .mib;	ADDP		r33=0,r33
	mov		pr.rot=1<<16
	brp.loop.imp	.L_bn_sqr_words_ctop,.L_bn_sqr_words_cend-16
					}
{ .mii;	add		r34=8,r32
	mov		ar.lc=r10
	mov		ar.ec=18	};;

// 2*(n+17) on Itanium, (n+17) on "wider" IA-64 implementations. It's
// possible to compress the epilogue (I'm getting tired to write this
// comment over and over) and get down to 2*n+16 at the cost of
// scalability. The decision will very likely be reconsidered after the
// benchmark program is profiled. I.e. if performance gain on Itanium
// will appear larger than loss on "wider" IA-64, then the loop should
// be explicitly split and the epilogue compressed.
.L_bn_sqr_words_ctop:
{ .mfi;	(p16)	ldf8		f32=[r33],8
	(p25)	xmpy.lu		f42=f41,f41
	(p0)	nop.i		0x0		}
{ .mib;	(p33)	stf8		[r32]=f50,16
	(p0)	nop.i		0x0
	(p0)	nop.b		0x0		}
{ .mfi;	(p0)	nop.m		0x0
	(p25)	xmpy.hu		f52=f41,f41
	(p0)	nop.i		0x0		}
{ .mib;	(p33)	stf8		[r34]=f60,16
	(p0)	nop.i		0x0
	br.ctop.sptk	.L_bn_sqr_words_ctop	};;
.L_bn_sqr_words_cend:

{ .mii;	nop.m		0x0
	mov		pr=r9,0x1ffff
	mov		ar.lc=r3	}
{ .mfb;	rum		1<<5		// clear um.mfh
	nop.f		0x0
	br.ret.sptk.many	b0	};;
.endp	bn_sqr_words#
#endif

#if 1
// Apparently we win nothing by implementing special bn_sqr_comba8.
// Yes, it is possible to reduce the number of multiplications by
// almost factor of two, but then the amount of additions would
// increase by factor of two (as we would have to perform those
// otherwise performed by xma ourselves). Normally we would trade
// anyway as multiplications are way more expensive, but not this
// time... Multiplication kernel is fully pipelined and as we drain
// one 128-bit multiplication result per clock cycle multiplications
// are effectively as inexpensive as additions. Special implementation
// might become of interest for "wider" IA-64 implementation as you'll
// be able to get through the multiplication phase faster (there won't
// be any stall issues as discussed in the commentary section below and
// you therefore will be able to employ all 4 FP units)... But these
// Itanium days it's simply too hard to justify the effort so I just
// drop down to bn_mul_comba8 code:-)
//
// void bn_sqr_comba8(BN_ULONG *r, BN_ULONG *a)
//
.global	bn_sqr_comba8#
.proc	bn_sqr_comba8#
.align	64
bn_sqr_comba8:
	.prologue
	.save	ar.pfs,r2
#if defined(_HPUX_SOURCE) && !defined(_LP64)
{ .mii;	alloc	r2=ar.pfs,2,1,0,0
	addp4	r33=0,r33
	addp4	r32=0,r32		};;
{ .mii;
#else
{ .mii;	alloc	r2=ar.pfs,2,1,0,0
#endif
	mov	r34=r33
	add	r14=8,r33		};;
	.body
{ .mii;	add	r17=8,r34
	add	r15=16,r33
	add	r18=16,r34		}
{ .mfb;	add	r16=24,r33
	br	.L_cheat_entry_point8	};;
.endp	bn_sqr_comba8#
#endif

#if 1
// I've estimated this routine to run in ~120 ticks, but in reality
// (i.e. according to ar.itc) it takes ~160 ticks. Are those extra
// cycles consumed for instructions fetch? Or did I misinterpret some
// clause in Itanium µ-architecture manual? Comments are welcomed and
// highly appreciated.
//
// On Itanium 2 it takes ~190 ticks. This is because of stalls on
// result from getf.sig. I do nothing about it at this point for
// reasons depicted below.
//
// However! It should be noted that even 160 ticks is darn good result
// as it's over 10 (yes, ten, spelled as t-e-n) times faster than the
// C version (compiled with gcc with inline assembler). I really
// kicked compiler's butt here, didn't I? Yeah! This brings us to the
// following statement. It's damn shame that this routine isn't called
// very often nowadays! According to the profiler most CPU time is
// consumed by bn_mul_add_words called from BN_from_montgomery. In
// order to estimate what we're missing, I've compared the performance
// of this routine against "traditional" implementation, i.e. against
// following routine:
//
// void bn_mul_comba8(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
// {	r[ 8]=bn_mul_words(    &(r[0]),a,8,b[0]);
//	r[ 9]=bn_mul_add_words(&(r[1]),a,8,b[1]);
//	r[10]=bn_mul_add_words(&(r[2]),a,8,b[2]);
//	r[11]=bn_mul_add_words(&(r[3]),a,8,b[3]);
//	r[12]=bn_mul_add_words(&(r[4]),a,8,b[4]);
//	r[13]=bn_mul_add_words(&(r[5]),a,8,b[5]);
//	r[14]=bn_mul_add_words(&(r[6]),a,8,b[6]);
//	r[15]=bn_mul_add_words(&(r[7]),a,8,b[7]);
// }
//
// The one below is over 8 times faster than the one above:-( Even
// more reasons to "combafy" bn_mul_add_mont...
//
// And yes, this routine really made me wish there were an optimizing
// assembler! It also feels like it deserves a dedication.
//
//	To my wife for being there and to my kids...
//
// void bn_mul_comba8(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
//
#define	carry1	r14
#define	carry2	r15
#define	carry3	r34
.global	bn_mul_comba8#
.proc	bn_mul_comba8#
.align	64
bn_mul_comba8:
	.prologue
	.save	ar.pfs,r2
#if defined(_HPUX_SOURCE) && !defined(_LP64)
{ .mii;	alloc	r2=ar.pfs,3,0,0,0
	addp4	r33=0,r33
	addp4	r34=0,r34		};;
{ .mii;	addp4	r32=0,r32
#else
{ .mii;	alloc   r2=ar.pfs,3,0,0,0
#endif
	add	r14=8,r33
	add	r17=8,r34		}
	.body
{ .mii;	add	r15=16,r33
	add	r18=16,r34
	add	r16=24,r33		}
.L_cheat_entry_point8:
{ .mmi;	add	r19=24,r34

	ldf8	f32=[r33],32		};;

{ .mmi;	ldf8	f120=[r34],32
	ldf8	f121=[r17],32		}
{ .mmi;	ldf8	f122=[r18],32
	ldf8	f123=[r19],32		};;
{ .mmi;	ldf8	f124=[r34]
	ldf8	f125=[r17]		}
{ .mmi;	ldf8	f126=[r18]
	ldf8	f127=[r19]		}

{ .mmi;	ldf8	f33=[r14],32
	ldf8	f34=[r15],32		}
{ .mmi;	ldf8	f35=[r16],32;;
	ldf8	f36=[r33]		}
{ .mmi;	ldf8	f37=[r14]
	ldf8	f38=[r15]		}
{ .mfi;	ldf8	f39=[r16]
// -------\ Entering multiplier's heaven /-------
// ------------\                    /------------
// -----------------\          /-----------------
// ----------------------\/----------------------
		xma.hu	f41=f32,f120,f0		}
{ .mfi;		xma.lu	f40=f32,f120,f0		};; // (*)
{ .mfi;		xma.hu	f51=f32,f121,f0		}
{ .mfi;		xma.lu	f50=f32,f121,f0		};;
{ .mfi;		xma.hu	f61=f32,f122,f0		}
{ .mfi;		xma.lu	f60=f32,f122,f0		};;
{ .mfi;		xma.hu	f71=f32,f123,f0		}
{ .mfi;		xma.lu	f70=f32,f123,f0		};;
{ .mfi;		xma.hu	f81=f32,f124,f0		}
{ .mfi;		xma.lu	f80=f32,f124,f0		};;
{ .mfi;		xma.hu	f91=f32,f125,f0		}
{ .mfi;		xma.lu	f90=f32,f125,f0		};;
{ .mfi;		xma.hu	f101=f32,f126,f0	}
{ .mfi;		xma.lu	f100=f32,f126,f0	};;
{ .mfi;		xma.hu	f111=f32,f127,f0	}
{ .mfi;		xma.lu	f110=f32,f127,f0	};;//
// (*)	You can argue that splitting at every second bundle would
//	prevent "wider" IA-64 implementations from achieving the peak
//	performance. Well, not really... The catch is that if you
//	intend to keep 4 FP units busy by splitting at every fourth
//	bundle and thus perform these 16 multiplications in 4 ticks,
//	the first bundle *below* would stall because the result from
//	the first xma bundle *above* won't be available for another 3
//	ticks (if not more, being an optimist, I assume that "wider"
//	implementation will have same latency:-). This stall will hold
//	you back and the performance would be as if every second bundle
//	were split *anyway*...
{ .mfi;	getf.sig	r16=f40
		xma.hu	f42=f33,f120,f41
	add		r33=8,r32		}
{ .mfi;		xma.lu	f41=f33,f120,f41	};;
{ .mfi;	getf.sig	r24=f50
		xma.hu	f52=f33,f121,f51	}
{ .mfi;		xma.lu	f51=f33,f121,f51	};;
{ .mfi;	st8		[r32]=r16,16
		xma.hu	f62=f33,f122,f61	}
{ .mfi;		xma.lu	f61=f33,f122,f61	};;
{ .mfi;		xma.hu	f72=f33,f123,f71	}
{ .mfi;		xma.lu	f71=f33,f123,f71	};;
{ .mfi;		xma.hu	f82=f33,f124,f81	}
{ .mfi;		xma.lu	f81=f33,f124,f81	};;
{ .mfi;		xma.hu	f92=f33,f125,f91	}
{ .mfi;		xma.lu	f91=f33,f125,f91	};;
{ .mfi;		xma.hu	f102=f33,f126,f101	}
{ .mfi;		xma.lu	f101=f33,f126,f101	};;
{ .mfi;		xma.hu	f112=f33,f127,f111	}
{ .mfi;		xma.lu	f111=f33,f127,f111	};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r25=f41
		xma.hu	f43=f34,f120,f42	}
{ .mfi;		xma.lu	f42=f34,f120,f42	};;
{ .mfi;	getf.sig	r16=f60
		xma.hu	f53=f34,f121,f52	}
{ .mfi;		xma.lu	f52=f34,f121,f52	};;
{ .mfi;	getf.sig	r17=f51
		xma.hu	f63=f34,f122,f62
	add		r25=r25,r24		}
{ .mfi;		xma.lu	f62=f34,f122,f62
	mov		carry1=0		};;
{ .mfi;	cmp.ltu		p6,p0=r25,r24
		xma.hu	f73=f34,f123,f72	}
{ .mfi;		xma.lu	f72=f34,f123,f72	};;
{ .mfi;	st8		[r33]=r25,16
		xma.hu	f83=f34,f124,f82
(p6)	add		carry1=1,carry1		}
{ .mfi;		xma.lu	f82=f34,f124,f82	};;
{ .mfi;		xma.hu	f93=f34,f125,f92	}
{ .mfi;		xma.lu	f92=f34,f125,f92	};;
{ .mfi;		xma.hu	f103=f34,f126,f102	}
{ .mfi;		xma.lu	f102=f34,f126,f102	};;
{ .mfi;		xma.hu	f113=f34,f127,f112	}
{ .mfi;		xma.lu	f112=f34,f127,f112	};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r18=f42
		xma.hu	f44=f35,f120,f43
	add		r17=r17,r16		}
{ .mfi;		xma.lu	f43=f35,f120,f43	};;
{ .mfi;	getf.sig	r24=f70
		xma.hu	f54=f35,f121,f53	}
{ .mfi;	mov		carry2=0
		xma.lu	f53=f35,f121,f53	};;
{ .mfi;	getf.sig	r25=f61
		xma.hu	f64=f35,f122,f63
	cmp.ltu		p7,p0=r17,r16		}
{ .mfi;	add		r18=r18,r17
		xma.lu	f63=f35,f122,f63	};;
{ .mfi;	getf.sig	r26=f52
		xma.hu	f74=f35,f123,f73
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r18,r17
		xma.lu	f73=f35,f123,f73
	add		r18=r18,carry1		};;
{ .mfi;
		xma.hu	f84=f35,f124,f83
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r18,carry1
		xma.lu	f83=f35,f124,f83	};;
{ .mfi;	st8		[r32]=r18,16
		xma.hu	f94=f35,f125,f93
(p7)	add		carry2=1,carry2		}
{ .mfi;		xma.lu	f93=f35,f125,f93	};;
{ .mfi;		xma.hu	f104=f35,f126,f103	}
{ .mfi;		xma.lu	f103=f35,f126,f103	};;
{ .mfi;		xma.hu	f114=f35,f127,f113	}
{ .mfi;	mov		carry1=0
		xma.lu	f113=f35,f127,f113
	add		r25=r25,r24		};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r27=f43
		xma.hu	f45=f36,f120,f44
	cmp.ltu		p6,p0=r25,r24		}
{ .mfi;		xma.lu	f44=f36,f120,f44	
	add		r26=r26,r25		};;
{ .mfi;	getf.sig	r16=f80
		xma.hu	f55=f36,f121,f54
(p6)	add		carry1=1,carry1		}
{ .mfi;		xma.lu	f54=f36,f121,f54	};;
{ .mfi;	getf.sig	r17=f71
		xma.hu	f65=f36,f122,f64
	cmp.ltu		p6,p0=r26,r25		}
{ .mfi;		xma.lu	f64=f36,f122,f64
	add		r27=r27,r26		};;
{ .mfi;	getf.sig	r18=f62
		xma.hu	f75=f36,f123,f74
(p6)	add		carry1=1,carry1		}
{ .mfi;	cmp.ltu		p6,p0=r27,r26
		xma.lu	f74=f36,f123,f74
	add		r27=r27,carry2		};;
{ .mfi;	getf.sig	r19=f53
		xma.hu	f85=f36,f124,f84
(p6)	add		carry1=1,carry1		}
{ .mfi;		xma.lu	f84=f36,f124,f84
	cmp.ltu		p6,p0=r27,carry2	};;
{ .mfi;	st8		[r33]=r27,16
		xma.hu	f95=f36,f125,f94
(p6)	add		carry1=1,carry1		}
{ .mfi;		xma.lu	f94=f36,f125,f94	};;
{ .mfi;		xma.hu	f105=f36,f126,f104	}
{ .mfi;	mov		carry2=0
		xma.lu	f104=f36,f126,f104
	add		r17=r17,r16		};;
{ .mfi;		xma.hu	f115=f36,f127,f114
	cmp.ltu		p7,p0=r17,r16		}
{ .mfi;		xma.lu	f114=f36,f127,f114
	add		r18=r18,r17		};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r20=f44
		xma.hu	f46=f37,f120,f45
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r18,r17
		xma.lu	f45=f37,f120,f45
	add		r19=r19,r18		};;
{ .mfi;	getf.sig	r24=f90
		xma.hu	f56=f37,f121,f55	}
{ .mfi;		xma.lu	f55=f37,f121,f55	};;
{ .mfi;	getf.sig	r25=f81
		xma.hu	f66=f37,f122,f65
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r19,r18
		xma.lu	f65=f37,f122,f65
	add		r20=r20,r19		};;
{ .mfi;	getf.sig	r26=f72
		xma.hu	f76=f37,f123,f75
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r20,r19
		xma.lu	f75=f37,f123,f75
	add		r20=r20,carry1		};;
{ .mfi;	getf.sig	r27=f63
		xma.hu	f86=f37,f124,f85
(p7)	add		carry2=1,carry2		}
{ .mfi;		xma.lu	f85=f37,f124,f85
	cmp.ltu		p7,p0=r20,carry1	};;
{ .mfi;	getf.sig	r28=f54
		xma.hu	f96=f37,f125,f95
(p7)	add		carry2=1,carry2		}
{ .mfi;	st8		[r32]=r20,16
		xma.lu	f95=f37,f125,f95	};;
{ .mfi;		xma.hu	f106=f37,f126,f105	}
{ .mfi;	mov		carry1=0
		xma.lu	f105=f37,f126,f105
	add		r25=r25,r24		};;
{ .mfi;		xma.hu	f116=f37,f127,f115
	cmp.ltu		p6,p0=r25,r24		}
{ .mfi;		xma.lu	f115=f37,f127,f115
	add		r26=r26,r25		};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r29=f45
		xma.hu	f47=f38,f120,f46
(p6)	add		carry1=1,carry1		}
{ .mfi;	cmp.ltu		p6,p0=r26,r25
		xma.lu	f46=f38,f120,f46
	add		r27=r27,r26		};;
{ .mfi;	getf.sig	r16=f100
		xma.hu	f57=f38,f121,f56
(p6)	add		carry1=1,carry1		}
{ .mfi;	cmp.ltu		p6,p0=r27,r26
		xma.lu	f56=f38,f121,f56
	add		r28=r28,r27		};;
{ .mfi;	getf.sig	r17=f91
		xma.hu	f67=f38,f122,f66
(p6)	add		carry1=1,carry1		}
{ .mfi;	cmp.ltu		p6,p0=r28,r27
		xma.lu	f66=f38,f122,f66
	add		r29=r29,r28		};;
{ .mfi;	getf.sig	r18=f82
		xma.hu	f77=f38,f123,f76
(p6)	add		carry1=1,carry1		}
{ .mfi;	cmp.ltu		p6,p0=r29,r28
		xma.lu	f76=f38,f123,f76
	add		r29=r29,carry2		};;
{ .mfi;	getf.sig	r19=f73
		xma.hu	f87=f38,f124,f86
(p6)	add		carry1=1,carry1		}
{ .mfi;		xma.lu	f86=f38,f124,f86
	cmp.ltu		p6,p0=r29,carry2	};;
{ .mfi;	getf.sig	r20=f64
		xma.hu	f97=f38,f125,f96
(p6)	add		carry1=1,carry1		}
{ .mfi;	st8		[r33]=r29,16
		xma.lu	f96=f38,f125,f96	};;
{ .mfi;	getf.sig	r21=f55
		xma.hu	f107=f38,f126,f106	}
{ .mfi;	mov		carry2=0
		xma.lu	f106=f38,f126,f106
	add		r17=r17,r16		};;
{ .mfi;		xma.hu	f117=f38,f127,f116
	cmp.ltu		p7,p0=r17,r16		}
{ .mfi;		xma.lu	f116=f38,f127,f116
	add		r18=r18,r17		};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r22=f46
		xma.hu	f48=f39,f120,f47
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r18,r17
		xma.lu	f47=f39,f120,f47
	add		r19=r19,r18		};;
{ .mfi;	getf.sig	r24=f110
		xma.hu	f58=f39,f121,f57
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r19,r18
		xma.lu	f57=f39,f121,f57
	add		r20=r20,r19		};;
{ .mfi;	getf.sig	r25=f101
		xma.hu	f68=f39,f122,f67
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r20,r19
		xma.lu	f67=f39,f122,f67
	add		r21=r21,r20		};;
{ .mfi;	getf.sig	r26=f92
		xma.hu	f78=f39,f123,f77
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r21,r20
		xma.lu	f77=f39,f123,f77
	add		r22=r22,r21		};;
{ .mfi;	getf.sig	r27=f83
		xma.hu	f88=f39,f124,f87
(p7)	add		carry2=1,carry2		}
{ .mfi;	cmp.ltu		p7,p0=r22,r21
		xma.lu	f87=f39,f124,f87
	add		r22=r22,carry1		};;
{ .mfi;	getf.sig	r28=f74
		xma.hu	f98=f39,f125,f97
(p7)	add		carry2=1,carry2		}
{ .mfi;		xma.lu	f97=f39,f125,f97
	cmp.ltu		p7,p0=r22,carry1	};;
{ .mfi;	getf.sig	r29=f65
		xma.hu	f108=f39,f126,f107
(p7)	add		carry2=1,carry2		}
{ .mfi;	st8		[r32]=r22,16
		xma.lu	f107=f39,f126,f107	};;
{ .mfi;	getf.sig	r30=f56
		xma.hu	f118=f39,f127,f117	}
{ .mfi;		xma.lu	f117=f39,f127,f117	};;//
//-------------------------------------------------//
// Leaving multiplier's heaven... Quite a ride, huh?

{ .mii;	getf.sig	r31=f47
	add		r25=r25,r24
	mov		carry1=0		};;
{ .mii;		getf.sig	r16=f111
	cmp.ltu		p6,p0=r25,r24
	add		r26=r26,r25		};;
{ .mfb;		getf.sig	r17=f102	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,r25
	add		r27=r27,r26		};;
{ .mfb;	nop.m	0x0				}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r27,r26
	add		r28=r28,r27		};;
{ .mii;		getf.sig	r18=f93
		add		r17=r17,r16
		mov		carry3=0	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r28,r27
	add		r29=r29,r28		};;
{ .mii;		getf.sig	r19=f84
		cmp.ltu		p7,p0=r17,r16	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r29,r28
	add		r30=r30,r29		};;
{ .mii;		getf.sig	r20=f75
		add		r18=r18,r17	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r30,r29
	add		r31=r31,r30		};;
{ .mfb;		getf.sig	r21=f66		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r18,r17
		add		r19=r19,r18	}
{ .mfb;	nop.m	0x0				}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r31,r30
	add		r31=r31,carry2		};;
{ .mfb;		getf.sig	r22=f57		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r19,r18
		add		r20=r20,r19	}
{ .mfb;	nop.m	0x0				}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r31,carry2	};;
{ .mfb;		getf.sig	r23=f48		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r20,r19
		add		r21=r21,r20	}
{ .mii;
(p6)	add		carry1=1,carry1		}
{ .mfb;	st8		[r33]=r31,16		};;

{ .mfb;	getf.sig	r24=f112		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r21,r20
		add		r22=r22,r21	};;
{ .mfb;	getf.sig	r25=f103		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r22,r21
		add		r23=r23,r22	};;
{ .mfb;	getf.sig	r26=f94			}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r23,r22
		add		r23=r23,carry1	};;
{ .mfb;	getf.sig	r27=f85			}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p8=r23,carry1};;
{ .mii;	getf.sig	r28=f76
	add		r25=r25,r24
	mov		carry1=0		}
{ .mii;		st8		[r32]=r23,16
	(p7)	add		carry2=1,carry3
	(p8)	add		carry2=0,carry3	};;

{ .mfb;	nop.m	0x0				}
{ .mii;	getf.sig	r29=f67
	cmp.ltu		p6,p0=r25,r24
	add		r26=r26,r25		};;
{ .mfb;	getf.sig	r30=f58			}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,r25
	add		r27=r27,r26		};;
{ .mfb;		getf.sig	r16=f113	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r27,r26
	add		r28=r28,r27		};;
{ .mfb;		getf.sig	r17=f104	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r28,r27
	add		r29=r29,r28		};;
{ .mfb;		getf.sig	r18=f95		}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r29,r28
	add		r30=r30,r29		};;
{ .mii;		getf.sig	r19=f86
		add		r17=r17,r16
		mov		carry3=0	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r30,r29
	add		r30=r30,carry2		};;
{ .mii;		getf.sig	r20=f77
		cmp.ltu		p7,p0=r17,r16
		add		r18=r18,r17	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r30,carry2	};;
{ .mfb;		getf.sig	r21=f68		}
{ .mii;	st8		[r33]=r30,16
(p6)	add		carry1=1,carry1		};;

{ .mfb;	getf.sig	r24=f114		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r18,r17
		add		r19=r19,r18	};;
{ .mfb;	getf.sig	r25=f105		}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r19,r18
		add		r20=r20,r19	};;
{ .mfb;	getf.sig	r26=f96			}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r20,r19
		add		r21=r21,r20	};;
{ .mfb;	getf.sig	r27=f87			}
{ .mii;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p0=r21,r20
		add		r21=r21,carry1	};;
{ .mib;	getf.sig	r28=f78			
	add		r25=r25,r24		}
{ .mib;	(p7)	add		carry3=1,carry3
		cmp.ltu		p7,p8=r21,carry1};;
{ .mii;		st8		[r32]=r21,16
	(p7)	add		carry2=1,carry3
	(p8)	add		carry2=0,carry3	}

{ .mii;	mov		carry1=0
	cmp.ltu		p6,p0=r25,r24
	add		r26=r26,r25		};;
{ .mfb;		getf.sig	r16=f115	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,r25
	add		r27=r27,r26		};;
{ .mfb;		getf.sig	r17=f106	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r27,r26
	add		r28=r28,r27		};;
{ .mfb;		getf.sig	r18=f97		}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r28,r27
	add		r28=r28,carry2		};;
{ .mib;		getf.sig	r19=f88
		add		r17=r17,r16	}
{ .mib;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r28,carry2	};;
{ .mii;	st8		[r33]=r28,16
(p6)	add		carry1=1,carry1		}

{ .mii;		mov		carry2=0
		cmp.ltu		p7,p0=r17,r16
		add		r18=r18,r17	};;
{ .mfb;	getf.sig	r24=f116		}
{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r18,r17
		add		r19=r19,r18	};;
{ .mfb;	getf.sig	r25=f107		}
{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r19,r18
		add		r19=r19,carry1	};;
{ .mfb;	getf.sig	r26=f98			}
{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r19,carry1};;
{ .mii;		st8		[r32]=r19,16
	(p7)	add		carry2=1,carry2	}

{ .mfb;	add		r25=r25,r24		};;

{ .mfb;		getf.sig	r16=f117	}
{ .mii;	mov		carry1=0
	cmp.ltu		p6,p0=r25,r24
	add		r26=r26,r25		};;
{ .mfb;		getf.sig	r17=f108	}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,r25
	add		r26=r26,carry2		};;
{ .mfb;	nop.m	0x0				}
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,carry2	};;
{ .mii;	st8		[r33]=r26,16
(p6)	add		carry1=1,carry1		}

{ .mfb;		add		r17=r17,r16	};;
{ .mfb;	getf.sig	r24=f118		}
{ .mii;		mov		carry2=0
		cmp.ltu		p7,p0=r17,r16
		add		r17=r17,carry1	};;
{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r17,carry1};;
{ .mii;		st8		[r32]=r17
	(p7)	add		carry2=1,carry2	};;
{ .mfb;	add		r24=r24,carry2		};;
{ .mib;	st8		[r33]=r24		}

{ .mib;	rum		1<<5		// clear um.mfh
	br.ret.sptk.many	b0	};;
.endp	bn_mul_comba8#
#undef	carry3
#undef	carry2
#undef	carry1
#endif

#if 1
// It's possible to make it faster (see comment to bn_sqr_comba8), but
// I reckon it doesn't worth the effort. Basically because the routine
// (actually both of them) practically never called... So I just play
// same trick as with bn_sqr_comba8.
//
// void bn_sqr_comba4(BN_ULONG *r, BN_ULONG *a)
//
.global	bn_sqr_comba4#
.proc	bn_sqr_comba4#
.align	64
bn_sqr_comba4:
	.prologue
	.save	ar.pfs,r2
#if defined(_HPUX_SOURCE) && !defined(_LP64)
{ .mii;	alloc   r2=ar.pfs,2,1,0,0
	addp4	r32=0,r32
	addp4	r33=0,r33		};;
{ .mii;
#else
{ .mii;	alloc	r2=ar.pfs,2,1,0,0
#endif
	mov	r34=r33
	add	r14=8,r33		};;
	.body
{ .mii;	add	r17=8,r34
	add	r15=16,r33
	add	r18=16,r34		}
{ .mfb;	add	r16=24,r33
	br	.L_cheat_entry_point4	};;
.endp	bn_sqr_comba4#
#endif

#if 1
// Runs in ~115 cycles and ~4.5 times faster than C. Well, whatever...
//
// void bn_mul_comba4(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
//
#define	carry1	r14
#define	carry2	r15
.global	bn_mul_comba4#
.proc	bn_mul_comba4#
.align	64
bn_mul_comba4:
	.prologue
	.save	ar.pfs,r2
#if defined(_HPUX_SOURCE) && !defined(_LP64)
{ .mii;	alloc   r2=ar.pfs,3,0,0,0
	addp4	r33=0,r33
	addp4	r34=0,r34		};;
{ .mii;	addp4	r32=0,r32
#else
{ .mii;	alloc	r2=ar.pfs,3,0,0,0
#endif
	add	r14=8,r33
	add	r17=8,r34		}
	.body
{ .mii;	add	r15=16,r33
	add	r18=16,r34
	add	r16=24,r33		};;
.L_cheat_entry_point4:
{ .mmi;	add	r19=24,r34

	ldf8	f32=[r33]		}

{ .mmi;	ldf8	f120=[r34]
	ldf8	f121=[r17]		};;
{ .mmi;	ldf8	f122=[r18]
	ldf8	f123=[r19]		}

{ .mmi;	ldf8	f33=[r14]
	ldf8	f34=[r15]		}
{ .mfi;	ldf8	f35=[r16]

		xma.hu	f41=f32,f120,f0		}
{ .mfi;		xma.lu	f40=f32,f120,f0		};;
{ .mfi;		xma.hu	f51=f32,f121,f0		}
{ .mfi;		xma.lu	f50=f32,f121,f0		};;
{ .mfi;		xma.hu	f61=f32,f122,f0		}
{ .mfi;		xma.lu	f60=f32,f122,f0		};;
{ .mfi;		xma.hu	f71=f32,f123,f0		}
{ .mfi;		xma.lu	f70=f32,f123,f0		};;//
// Major stall takes place here, and 3 more places below. Result from
// first xma is not available for another 3 ticks.
{ .mfi;	getf.sig	r16=f40
		xma.hu	f42=f33,f120,f41
	add		r33=8,r32		}
{ .mfi;		xma.lu	f41=f33,f120,f41	};;
{ .mfi;	getf.sig	r24=f50
		xma.hu	f52=f33,f121,f51	}
{ .mfi;		xma.lu	f51=f33,f121,f51	};;
{ .mfi;	st8		[r32]=r16,16
		xma.hu	f62=f33,f122,f61	}
{ .mfi;		xma.lu	f61=f33,f122,f61	};;
{ .mfi;		xma.hu	f72=f33,f123,f71	}
{ .mfi;		xma.lu	f71=f33,f123,f71	};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r25=f41
		xma.hu	f43=f34,f120,f42	}
{ .mfi;		xma.lu	f42=f34,f120,f42	};;
{ .mfi;	getf.sig	r16=f60
		xma.hu	f53=f34,f121,f52	}
{ .mfi;		xma.lu	f52=f34,f121,f52	};;
{ .mfi;	getf.sig	r17=f51
		xma.hu	f63=f34,f122,f62
	add		r25=r25,r24		}
{ .mfi;	mov		carry1=0
		xma.lu	f62=f34,f122,f62	};;
{ .mfi;	st8		[r33]=r25,16
		xma.hu	f73=f34,f123,f72
	cmp.ltu		p6,p0=r25,r24		}
{ .mfi;		xma.lu	f72=f34,f123,f72	};;//
//-------------------------------------------------//
{ .mfi;	getf.sig	r18=f42
		xma.hu	f44=f35,f120,f43
(p6)	add		carry1=1,carry1		}
{ .mfi;	add		r17=r17,r16
		xma.lu	f43=f35,f120,f43
	mov		carry2=0		};;
{ .mfi;	getf.sig	r24=f70
		xma.hu	f54=f35,f121,f53
	cmp.ltu		p7,p0=r17,r16		}
{ .mfi;		xma.lu	f53=f35,f121,f53	};;
{ .mfi;	getf.sig	r25=f61
		xma.hu	f64=f35,f122,f63
	add		r18=r18,r17		}
{ .mfi;		xma.lu	f63=f35,f122,f63
(p7)	add		carry2=1,carry2		};;
{ .mfi;	getf.sig	r26=f52
		xma.hu	f74=f35,f123,f73
	cmp.ltu		p7,p0=r18,r17		}
{ .mfi;		xma.lu	f73=f35,f123,f73
	add		r18=r18,carry1		};;
//-------------------------------------------------//
{ .mii;	st8		[r32]=r18,16
(p7)	add		carry2=1,carry2
	cmp.ltu		p7,p0=r18,carry1	};;

{ .mfi;	getf.sig	r27=f43	// last major stall
(p7)	add		carry2=1,carry2		};;
{ .mii;		getf.sig	r16=f71
	add		r25=r25,r24
	mov		carry1=0		};;
{ .mii;		getf.sig	r17=f62	
	cmp.ltu		p6,p0=r25,r24
	add		r26=r26,r25		};;
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,r25
	add		r27=r27,r26		};;
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r27,r26
	add		r27=r27,carry2		};;
{ .mii;		getf.sig	r18=f53
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r27,carry2	};;
{ .mfi;	st8		[r33]=r27,16
(p6)	add		carry1=1,carry1		}

{ .mii;		getf.sig	r19=f44
		add		r17=r17,r16
		mov		carry2=0	};;
{ .mii;	getf.sig	r24=f72
		cmp.ltu		p7,p0=r17,r16
		add		r18=r18,r17	};;
{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r18,r17
		add		r19=r19,r18	};;
{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r19,r18
		add		r19=r19,carry1	};;
{ .mii;	getf.sig	r25=f63
	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r19,carry1};;
{ .mii;		st8		[r32]=r19,16
	(p7)	add		carry2=1,carry2	}

{ .mii;	getf.sig	r26=f54
	add		r25=r25,r24
	mov		carry1=0		};;
{ .mii;		getf.sig	r16=f73
	cmp.ltu		p6,p0=r25,r24
	add		r26=r26,r25		};;
{ .mii;
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,r25
	add		r26=r26,carry2		};;
{ .mii;		getf.sig	r17=f64
(p6)	add		carry1=1,carry1
	cmp.ltu		p6,p0=r26,carry2	};;
{ .mii;	st8		[r33]=r26,16
(p6)	add		carry1=1,carry1		}

{ .mii;	getf.sig	r24=f74
		add		r17=r17,r16	
		mov		carry2=0	};;
{ .mii;		cmp.ltu		p7,p0=r17,r16
		add		r17=r17,carry1	};;

{ .mii;	(p7)	add		carry2=1,carry2
		cmp.ltu		p7,p0=r17,carry1};;
{ .mii;		st8		[r32]=r17,16
	(p7)	add		carry2=1,carry2	};;

{ .mii;	add		r24=r24,carry2		};;
{ .mii;	st8		[r33]=r24		}

{ .mib;	rum		1<<5		// clear um.mfh
	br.ret.sptk.many	b0	};;
.endp	bn_mul_comba4#
#undef	carry2
#undef	carry1
#endif

#if 1
//
// BN_ULONG bn_div_words(BN_ULONG h, BN_ULONG l, BN_ULONG d)
//
// In the nutshell it's a port of my MIPS III/IV implementation.
//
#define	AT	r14
#define	H	r16
#define	HH	r20
#define	L	r17
#define	D	r18
#define	DH	r22
#define	I	r21

#if 0
// Some preprocessors (most notably HP-UX) appear to be allergic to
// macros enclosed to parenthesis [as these three were].
#define	cont	p16
#define	break	p0	// p20
#define	equ	p24
#else
cont=p16
break=p0
equ=p24
#endif

.global	abort#
.global	bn_div_words#
.proc	bn_div_words#
.align	64
bn_div_words:
	.prologue
	.save	ar.pfs,r2
{ .mii;	alloc		r2=ar.pfs,3,5,0,8
	.save	b0,r3
	mov		r3=b0
	.save	pr,r10
	mov		r10=pr		};;
{ .mmb;	cmp.eq		p6,p0=r34,r0
	mov		r8=-1
(p6)	br.ret.spnt.many	b0	};;

	.body
{ .mii;	mov		H=r32		// save h
	mov		ar.ec=0		// don't rotate at exit
	mov		pr.rot=0	}
{ .mii;	mov		L=r33		// save l
	mov		r25=r0		// needed if abort is called on VMS
	mov		r36=r0		};;

.L_divw_shift:	// -vv- note signed comparison
{ .mfi;	(p0)	cmp.lt		p16,p0=r0,r34	// d
	(p0)	shladd		r33=r34,1,r0	}
{ .mfb;	(p0)	add		r35=1,r36
	(p0)	nop.f		0x0
(p16)	br.wtop.dpnt		.L_divw_shift	};;

{ .mii;	mov		D=r34
	shr.u		DH=r34,32
	sub		r35=64,r36		};;
{ .mii;	setf.sig	f7=DH
	shr.u		AT=H,r35
	mov		I=r36			};;
{ .mib;	cmp.ne		p6,p0=r0,AT
	shl		H=H,r36
(p6)	br.call.spnt.clr	b0=abort	};;	// overflow, die...

{ .mfi;	fcvt.xuf.s1	f7=f7
	shr.u		AT=L,r35		};;
{ .mii;	shl		L=L,r36
	or		H=H,AT			};;

{ .mii;	nop.m		0x0
	cmp.leu		p6,p0=D,H;;
(p6)	sub		H=H,D			}

{ .mlx;	setf.sig	f14=D
	movl		AT=0xffffffff		};;
///////////////////////////////////////////////////////////
{ .mii;	setf.sig	f6=H
	shr.u		HH=H,32;;
	cmp.eq		p6,p7=HH,DH		};;
{ .mfb;
(p6)	setf.sig	f8=AT
(p7)	fcvt.xuf.s1	f6=f6
(p7)	br.call.sptk	b6=.L_udiv64_32_b6	};;

{ .mfi;	getf.sig	r33=f8				// q
	xmpy.lu		f9=f8,f14		}
{ .mfi;	xmpy.hu		f10=f8,f14
	shrp		H=H,L,32		};;

{ .mmi;	getf.sig	r35=f9				// tl
	getf.sig	r31=f10			};;	// th

.L_divw_1st_iter:
{ .mii;	(p0)	add		r32=-1,r33
	(p0)	cmp.eq		equ,cont=HH,r31		};;
{ .mii;	(p0)	cmp.ltu		p8,p0=r35,D
	(p0)	sub		r34=r35,D
	(equ)	cmp.leu		break,cont=r35,H	};;
{ .mib;	(cont)	cmp.leu		cont,break=HH,r31
	(p8)	add		r31=-1,r31
(cont)	br.wtop.spnt		.L_divw_1st_iter	};;
///////////////////////////////////////////////////////////
{ .mii;	sub		H=H,r35
	shl		r8=r33,32
	shl		L=L,32			};;
///////////////////////////////////////////////////////////
{ .mii;	setf.sig	f6=H
	shr.u		HH=H,32;;
	cmp.eq		p6,p7=HH,DH		};;
{ .mfb;
(p6)	setf.sig	f8=AT
(p7)	fcvt.xuf.s1	f6=f6
(p7)	br.call.sptk	b6=.L_udiv64_32_b6	};;

{ .mfi;	getf.sig	r33=f8				// q
	xmpy.lu		f9=f8,f14		}
{ .mfi;	xmpy.hu		f10=f8,f14
	shrp		H=H,L,32		};;

{ .mmi;	getf.sig	r35=f9				// tl
	getf.sig	r31=f10			};;	// th

.L_divw_2nd_iter:
{ .mii;	(p0)	add		r32=-1,r33
	(p0)	cmp.eq		equ,cont=HH,r31		};;
{ .mii;	(p0)	cmp.ltu		p8,p0=r35,D
	(p0)	sub		r34=r35,D
	(equ)	cmp.leu		break,cont=r35,H	};;
{ .mib;	(cont)	cmp.leu		cont,break=HH,r31
	(p8)	add		r31=-1,r31
(cont)	br.wtop.spnt		.L_divw_2nd_iter	};;
///////////////////////////////////////////////////////////
{ .mii;	sub	H=H,r35
	or	r8=r8,r33
	mov	ar.pfs=r2		};;
{ .mii;	shr.u	r9=H,I			// remainder if anybody wants it
	mov	pr=r10,0x1ffff		}
{ .mfb;	br.ret.sptk.many	b0	};;

// Unsigned 64 by 32 (well, by 64 for the moment) bit integer division
// procedure.
//
// inputs:	f6 = (double)a, f7 = (double)b
// output:	f8 = (int)(a/b)
// clobbered:	f8,f9,f10,f11,pred
pred=p15
// This snippet is based on text found in the "Divide, Square
// Root and Remainder" section at
// http://www.intel.com/software/products/opensource/libraries/num.htm.
// Yes, I admit that the referred code was used as template,
// but after I realized that there hardly is any other instruction
// sequence which would perform this operation. I mean I figure that
// any independent attempt to implement high-performance division
// will result in code virtually identical to the Intel code. It
// should be noted though that below division kernel is 1 cycle
// faster than Intel one (note commented splits:-), not to mention
// original prologue (rather lack of one) and epilogue.
.align	32
.skip	16
.L_udiv64_32_b6:
	frcpa.s1	f8,pred=f6,f7;;		// [0]  y0 = 1 / b

(pred)	fnma.s1		f9=f7,f8,f1		// [5]  e0 = 1 - b * y0
(pred)	fmpy.s1		f10=f6,f8;;		// [5]  q0 = a * y0
(pred)	fmpy.s1		f11=f9,f9		// [10] e1 = e0 * e0
(pred)	fma.s1		f10=f9,f10,f10;;	// [10] q1 = q0 + e0 * q0
(pred)	fma.s1		f8=f9,f8,f8	//;;	// [15] y1 = y0 + e0 * y0
(pred)	fma.s1		f9=f11,f10,f10;;	// [15] q2 = q1 + e1 * q1
(pred)	fma.s1		f8=f11,f8,f8	//;;	// [20] y2 = y1 + e1 * y1
(pred)	fnma.s1		f10=f7,f9,f6;;		// [20] r2 = a - b * q2
(pred)	fma.s1		f8=f10,f8,f9;;		// [25] q3 = q2 + r2 * y2

	fcvt.fxu.trunc.s1	f8=f8		// [30] q = trunc(q3)
	br.ret.sptk.many	b6;;
.endp	bn_div_words#
#endif
