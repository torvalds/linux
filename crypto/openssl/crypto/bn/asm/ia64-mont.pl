#! /usr/bin/env perl
# Copyright 2010-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# January 2010
#
# "Teaser" Montgomery multiplication module for IA-64. There are
# several possibilities for improvement:
#
# - modulo-scheduling outer loop would eliminate quite a number of
#   stalls after ldf8, xma and getf.sig outside inner loop and
#   improve shorter key performance;
# - shorter vector support [with input vectors being fetched only
#   once] should be added;
# - 2x unroll with help of n0[1] would make the code scalable on
#   "wider" IA-64, "wider" than Itanium 2 that is, which is not of
#   acute interest, because upcoming Tukwila's individual cores are
#   reportedly based on Itanium 2 design;
# - dedicated squaring procedure(?);
#
# January 2010
#
# Shorter vector support is implemented by zero-padding ap and np
# vectors up to 8 elements, or 512 bits. This means that 256-bit
# inputs will be processed only 2 times faster than 512-bit inputs,
# not 4 [as one would expect, because algorithm complexity is n^2].
# The reason for padding is that inputs shorter than 512 bits won't
# be processed faster anyway, because minimal critical path of the
# core loop happens to match 512-bit timing. Either way, it resulted
# in >100% improvement of 512-bit RSA sign benchmark and 50% - of
# 1024-bit one [in comparison to original version of *this* module].
#
# So far 'openssl speed rsa dsa' output on 900MHz Itanium 2 *with*
# this module is:
#                   sign    verify    sign/s verify/s
# rsa  512 bits 0.000290s 0.000024s   3452.8  42031.4
# rsa 1024 bits 0.000793s 0.000058s   1261.7  17172.0
# rsa 2048 bits 0.005908s 0.000148s    169.3   6754.0
# rsa 4096 bits 0.033456s 0.000469s     29.9   2133.6
# dsa  512 bits 0.000253s 0.000198s   3949.9   5057.0
# dsa 1024 bits 0.000585s 0.000607s   1708.4   1647.4
# dsa 2048 bits 0.001453s 0.001703s    688.1    587.4
#
# ... and *without* (but still with ia64.S):
#
# rsa  512 bits 0.000670s 0.000041s   1491.8  24145.5
# rsa 1024 bits 0.001988s 0.000080s    502.9  12499.3
# rsa 2048 bits 0.008702s 0.000189s    114.9   5293.9
# rsa 4096 bits 0.043860s 0.000533s     22.8   1875.9
# dsa  512 bits 0.000441s 0.000427s   2265.3   2340.6
# dsa 1024 bits 0.000823s 0.000867s   1215.6   1153.2
# dsa 2048 bits 0.001894s 0.002179s    528.1    458.9
#
# As it can be seen, RSA sign performance improves by 130-30%,
# hereafter less for longer keys, while verify - by 74-13%.
# DSA performance improves by 115-30%.

$output=pop;

if ($^O eq "hpux") {
    $ADDP="addp4";
    for (@ARGV) { $ADDP="add" if (/[\+DD|\-mlp]64/); }
} else { $ADDP="add"; }

$code=<<___;
.explicit
.text

// int bn_mul_mont (BN_ULONG *rp,const BN_ULONG *ap,
//		    const BN_ULONG *bp,const BN_ULONG *np,
//		    const BN_ULONG *n0p,int num);
.align	64
.global	bn_mul_mont#
.proc	bn_mul_mont#
bn_mul_mont:
	.prologue
	.body
{ .mmi;	cmp4.le		p6,p7=2,r37;;
(p6)	cmp4.lt.unc	p8,p9=8,r37
	mov		ret0=r0		};;
{ .bbb;
(p9)	br.cond.dptk.many	bn_mul_mont_8
(p8)	br.cond.dpnt.many	bn_mul_mont_general
(p7)	br.ret.spnt.many	b0	};;
.endp	bn_mul_mont#

prevfs=r2;	prevpr=r3;	prevlc=r10;	prevsp=r11;

rptr=r8;	aptr=r9;	bptr=r14;	nptr=r15;
tptr=r16;	// &tp[0]
tp_1=r17;	// &tp[-1]
num=r18;	len=r19;	lc=r20;
topbit=r21;	// carry bit from tmp[num]

n0=f6;
m0=f7;
bi=f8;

.align	64
.local	bn_mul_mont_general#
.proc	bn_mul_mont_general#
bn_mul_mont_general:
	.prologue
{ .mmi;	.save	ar.pfs,prevfs
	alloc	prevfs=ar.pfs,6,2,0,8
	$ADDP	aptr=0,in1
	.save	ar.lc,prevlc
	mov	prevlc=ar.lc		}
{ .mmi;	.vframe	prevsp
	mov	prevsp=sp
	$ADDP	bptr=0,in2
	.save	pr,prevpr
	mov	prevpr=pr		};;

	.body
	.rotf		alo[6],nlo[4],ahi[8],nhi[6]
	.rotr		a[3],n[3],t[2]

{ .mmi;	ldf8		bi=[bptr],8		// (*bp++)
	ldf8		alo[4]=[aptr],16	// ap[0]
	$ADDP		r30=8,in1	};;
{ .mmi;	ldf8		alo[3]=[r30],16		// ap[1]
	ldf8		alo[2]=[aptr],16	// ap[2]
	$ADDP		in4=0,in4	};;
{ .mmi;	ldf8		alo[1]=[r30]		// ap[3]
	ldf8		n0=[in4]		// n0
	$ADDP		rptr=0,in0		}
{ .mmi;	$ADDP		nptr=0,in3
	mov		r31=16
	zxt4		num=in5		};;
{ .mmi;	ldf8		nlo[2]=[nptr],8		// np[0]
	shladd		len=num,3,r0
	shladd		r31=num,3,r31	};;
{ .mmi;	ldf8		nlo[1]=[nptr],8		// np[1]
	add		lc=-5,num
	sub		r31=sp,r31	};;
{ .mfb;	and		sp=-16,r31		// alloca
	xmpy.hu		ahi[2]=alo[4],bi	// ap[0]*bp[0]
	nop.b		0		}
{ .mfb;	nop.m		0
	xmpy.lu		alo[4]=alo[4],bi
	brp.loop.imp	.L1st_ctop,.L1st_cend-16
					};;
{ .mfi;	nop.m		0
	xma.hu		ahi[1]=alo[3],bi,ahi[2]	// ap[1]*bp[0]
	add		tp_1=8,sp	}
{ .mfi;	nop.m		0
	xma.lu		alo[3]=alo[3],bi,ahi[2]
	mov		pr.rot=0x20001f<<16
			// ------^----- (p40) at first (p23)
			// ----------^^ p[16:20]=1
					};;
{ .mfi;	nop.m		0
	xmpy.lu		m0=alo[4],n0		// (ap[0]*bp[0])*n0
	mov		ar.lc=lc	}
{ .mfi;	nop.m		0
	fcvt.fxu.s1	nhi[1]=f0
	mov		ar.ec=8		};;

.align	32
.L1st_ctop:
.pred.rel	"mutex",p40,p42
{ .mfi;	(p16)	ldf8		alo[0]=[aptr],8		    // *(aptr++)
	(p18)	xma.hu		ahi[0]=alo[2],bi,ahi[1]
	(p40)	add		n[2]=n[2],a[2]		}   // (p23)					}
{ .mfi;	(p18)	ldf8		nlo[0]=[nptr],8		    // *(nptr++)(p16)
	(p18)	xma.lu		alo[2]=alo[2],bi,ahi[1]
	(p42)	add		n[2]=n[2],a[2],1	};; // (p23)
{ .mfi;	(p21)	getf.sig	a[0]=alo[5]
	(p20)	xma.hu		nhi[0]=nlo[2],m0,nhi[1]
	(p42)	cmp.leu		p41,p39=n[2],a[2]   	}   // (p23)
{ .mfi;	(p23)	st8		[tp_1]=n[2],8
	(p20)	xma.lu		nlo[2]=nlo[2],m0,nhi[1]
	(p40)	cmp.ltu		p41,p39=n[2],a[2]	}   // (p23)
{ .mmb;	(p21)	getf.sig	n[0]=nlo[3]
	(p16)	nop.m		0
	br.ctop.sptk	.L1st_ctop			};;
.L1st_cend:

{ .mmi;	getf.sig	a[0]=ahi[6]		// (p24)
	getf.sig	n[0]=nhi[4]
	add		num=-1,num	};;	// num--
{ .mmi;	.pred.rel	"mutex",p40,p42
(p40)	add		n[0]=n[0],a[0]
(p42)	add		n[0]=n[0],a[0],1
	sub		aptr=aptr,len	};;	// rewind
{ .mmi;	.pred.rel	"mutex",p40,p42
(p40)	cmp.ltu		p41,p39=n[0],a[0]
(p42)	cmp.leu		p41,p39=n[0],a[0]
	sub		nptr=nptr,len	};;
{ .mmi;	.pred.rel	"mutex",p39,p41
(p39)	add		topbit=r0,r0
(p41)	add		topbit=r0,r0,1
	nop.i		0		}
{ .mmi;	st8		[tp_1]=n[0]
	add		tptr=16,sp
	add		tp_1=8,sp	};;

.Louter:
{ .mmi;	ldf8		bi=[bptr],8		// (*bp++)
	ldf8		ahi[3]=[tptr]		// tp[0]
	add		r30=8,aptr	};;
{ .mmi;	ldf8		alo[4]=[aptr],16	// ap[0]
	ldf8		alo[3]=[r30],16		// ap[1]
	add		r31=8,nptr	};;
{ .mfb;	ldf8		alo[2]=[aptr],16	// ap[2]
	xma.hu		ahi[2]=alo[4],bi,ahi[3]	// ap[0]*bp[i]+tp[0]
	brp.loop.imp	.Linner_ctop,.Linner_cend-16
					}
{ .mfb;	ldf8		alo[1]=[r30]		// ap[3]
	xma.lu		alo[4]=alo[4],bi,ahi[3]
	clrrrb.pr			};;
{ .mfi;	ldf8		nlo[2]=[nptr],16	// np[0]
	xma.hu		ahi[1]=alo[3],bi,ahi[2]	// ap[1]*bp[i]
	nop.i		0		}
{ .mfi;	ldf8		nlo[1]=[r31]		// np[1]
	xma.lu		alo[3]=alo[3],bi,ahi[2]
	mov		pr.rot=0x20101f<<16
			// ------^----- (p40) at first (p23)
			// --------^--- (p30) at first (p22)
			// ----------^^ p[16:20]=1
					};;
{ .mfi;	st8		[tptr]=r0		// tp[0] is already accounted
	xmpy.lu		m0=alo[4],n0		// (ap[0]*bp[i]+tp[0])*n0
	mov		ar.lc=lc	}
{ .mfi;
	fcvt.fxu.s1	nhi[1]=f0
	mov		ar.ec=8		};;

// This loop spins in 4*(n+7) ticks on Itanium 2 and should spin in
// 7*(n+7) ticks on Itanium (the one codenamed Merced). Factor of 7
// in latter case accounts for two-tick pipeline stall, which means
// that its performance would be ~20% lower than optimal one. No
// attempt was made to address this, because original Itanium is
// hardly represented out in the wild...
.align	32
.Linner_ctop:
.pred.rel	"mutex",p40,p42
.pred.rel	"mutex",p30,p32
{ .mfi;	(p16)	ldf8		alo[0]=[aptr],8		    // *(aptr++)
	(p18)	xma.hu		ahi[0]=alo[2],bi,ahi[1]
	(p40)	add		n[2]=n[2],a[2]		}   // (p23)
{ .mfi;	(p16)	nop.m		0
	(p18)	xma.lu		alo[2]=alo[2],bi,ahi[1]
	(p42)	add		n[2]=n[2],a[2],1	};; // (p23)
{ .mfi;	(p21)	getf.sig	a[0]=alo[5]
	(p16)	nop.f		0
	(p40)	cmp.ltu		p41,p39=n[2],a[2]	}   // (p23)
{ .mfi;	(p21)	ld8		t[0]=[tptr],8
	(p16)	nop.f		0
	(p42)	cmp.leu		p41,p39=n[2],a[2]	};; // (p23)
{ .mfi;	(p18)	ldf8		nlo[0]=[nptr],8		    // *(nptr++)
	(p20)	xma.hu		nhi[0]=nlo[2],m0,nhi[1]
	(p30)	add		a[1]=a[1],t[1]		}   // (p22)
{ .mfi;	(p16)	nop.m		0
	(p20)	xma.lu		nlo[2]=nlo[2],m0,nhi[1]
	(p32)	add		a[1]=a[1],t[1],1	};; // (p22)
{ .mmi;	(p21)	getf.sig	n[0]=nlo[3]
	(p16)	nop.m		0
	(p30)	cmp.ltu		p31,p29=a[1],t[1]	}   // (p22)
{ .mmb;	(p23)	st8		[tp_1]=n[2],8
	(p32)	cmp.leu		p31,p29=a[1],t[1]	    // (p22)
	br.ctop.sptk	.Linner_ctop			};;
.Linner_cend:

{ .mmi;	getf.sig	a[0]=ahi[6]		// (p24)
	getf.sig	n[0]=nhi[4]
	nop.i		0		};;

{ .mmi;	.pred.rel	"mutex",p31,p33
(p31)	add		a[0]=a[0],topbit
(p33)	add		a[0]=a[0],topbit,1
	mov		topbit=r0	};;
{ .mfi; .pred.rel	"mutex",p31,p33
(p31)	cmp.ltu		p32,p30=a[0],topbit
(p33)	cmp.leu		p32,p30=a[0],topbit
					}
{ .mfi;	.pred.rel	"mutex",p40,p42
(p40)	add		n[0]=n[0],a[0]
(p42)	add		n[0]=n[0],a[0],1
					};;
{ .mmi;	.pred.rel	"mutex",p44,p46
(p40)	cmp.ltu		p41,p39=n[0],a[0]
(p42)	cmp.leu		p41,p39=n[0],a[0]
(p32)	add		topbit=r0,r0,1	}

{ .mmi;	st8		[tp_1]=n[0],8
	cmp4.ne		p6,p0=1,num
	sub		aptr=aptr,len	};;	// rewind
{ .mmi;	sub		nptr=nptr,len
(p41)	add		topbit=r0,r0,1
	add		tptr=16,sp	}
{ .mmb;	add		tp_1=8,sp
	add		num=-1,num		// num--
(p6)	br.cond.sptk.many	.Louter	};;

{ .mbb;	add		lc=4,lc
	brp.loop.imp	.Lsub_ctop,.Lsub_cend-16
	clrrrb.pr			};;
{ .mii;	nop.m		0
	mov		pr.rot=0x10001<<16
			// ------^---- (p33) at first (p17)
	mov		ar.lc=lc	}
{ .mii;	nop.m		0
	mov		ar.ec=3
	nop.i		0		};;

.Lsub_ctop:
.pred.rel	"mutex",p33,p35
{ .mfi;	(p16)	ld8		t[0]=[tptr],8		    // t=*(tp++)
	(p16)	nop.f		0
	(p33)	sub		n[1]=t[1],n[1]		}   // (p17)
{ .mfi;	(p16)	ld8		n[0]=[nptr],8		    // n=*(np++)
	(p16)	nop.f		0
	(p35)	sub		n[1]=t[1],n[1],1	};; // (p17)
{ .mib;	(p18)	st8		[rptr]=n[2],8		    // *(rp++)=r
	(p33)	cmp.gtu		p34,p32=n[1],t[1]	    // (p17)
	(p18)	nop.b		0			}
{ .mib;	(p18)	nop.m		0
	(p35)	cmp.geu		p34,p32=n[1],t[1]	    // (p17)
	br.ctop.sptk	.Lsub_ctop			};;
.Lsub_cend:

{ .mmb;	.pred.rel	"mutex",p34,p36
(p34)	sub	topbit=topbit,r0	// (p19)
(p36)	sub	topbit=topbit,r0,1
	brp.loop.imp	.Lcopy_ctop,.Lcopy_cend-16
					}
{ .mmb;	sub	rptr=rptr,len		// rewind
	sub	tptr=tptr,len
	clrrrb.pr			};;
{ .mmi;	mov	aptr=rptr
	mov	bptr=tptr
	mov	pr.rot=1<<16		};;
{ .mii;	cmp.eq	p0,p6=topbit,r0
	mov	ar.lc=lc
	mov	ar.ec=2			};;

.Lcopy_ctop:
{ .mmi;	(p16)	ld8	a[0]=[aptr],8
	(p16)	ld8	t[0]=[bptr],8
	(p6)	mov	a[1]=t[1]	};;	// (p17)
{ .mmb;	(p17)	st8	[rptr]=a[1],8
	(p17)	st8	[tptr]=r0,8
	br.ctop.sptk	.Lcopy_ctop	};;
.Lcopy_cend:

{ .mmi;	mov		ret0=1			// signal "handled"
	rum		1<<5			// clear um.mfh
	mov		ar.lc=prevlc	}
{ .mib;	.restore	sp
	mov		sp=prevsp
	mov		pr=prevpr,0x1ffff
	br.ret.sptk.many	b0	};;
.endp	bn_mul_mont_general#

a1=r16;  a2=r17;  a3=r18;  a4=r19;  a5=r20;  a6=r21;  a7=r22;  a8=r23;
n1=r24;  n2=r25;  n3=r26;  n4=r27;  n5=r28;  n6=r29;  n7=r30;  n8=r31;
t0=r15;

ai0=f8;  ai1=f9;  ai2=f10; ai3=f11; ai4=f12; ai5=f13; ai6=f14; ai7=f15;
ni0=f16; ni1=f17; ni2=f18; ni3=f19; ni4=f20; ni5=f21; ni6=f22; ni7=f23;

.align	64
.skip	48		// aligns loop body
.local	bn_mul_mont_8#
.proc	bn_mul_mont_8#
bn_mul_mont_8:
	.prologue
{ .mmi;	.save		ar.pfs,prevfs
	alloc		prevfs=ar.pfs,6,2,0,8
	.vframe		prevsp
	mov		prevsp=sp
	.save		ar.lc,prevlc
	mov		prevlc=ar.lc	}
{ .mmi;	add		r17=-6*16,sp
	add		sp=-7*16,sp
	.save		pr,prevpr
	mov		prevpr=pr	};;

{ .mmi;	.save.gf	0,0x10
	stf.spill	[sp]=f16,-16
	.save.gf	0,0x20
	stf.spill	[r17]=f17,32
	add		r16=-5*16,prevsp};;
{ .mmi;	.save.gf	0,0x40
	stf.spill	[r16]=f18,32
	.save.gf	0,0x80
	stf.spill	[r17]=f19,32
	$ADDP		aptr=0,in1	};;
{ .mmi;	.save.gf	0,0x100
	stf.spill	[r16]=f20,32
	.save.gf	0,0x200
	stf.spill	[r17]=f21,32
	$ADDP		r29=8,in1	};;
{ .mmi;	.save.gf	0,0x400
	stf.spill	[r16]=f22
	.save.gf	0,0x800
	stf.spill	[r17]=f23
	$ADDP		rptr=0,in0	};;

	.body
	.rotf		bj[8],mj[2],tf[2],alo[10],ahi[10],nlo[10],nhi[10]
	.rotr		t[8]

// load input vectors padding them to 8 elements
{ .mmi;	ldf8		ai0=[aptr],16		// ap[0]
	ldf8		ai1=[r29],16		// ap[1]
	$ADDP		bptr=0,in2	}
{ .mmi;	$ADDP		r30=8,in2
	$ADDP		nptr=0,in3
	$ADDP		r31=8,in3	};;
{ .mmi;	ldf8		bj[7]=[bptr],16		// bp[0]
	ldf8		bj[6]=[r30],16		// bp[1]
	cmp4.le		p4,p5=3,in5	}
{ .mmi;	ldf8		ni0=[nptr],16		// np[0]
	ldf8		ni1=[r31],16		// np[1]
	cmp4.le		p6,p7=4,in5	};;

{ .mfi;	(p4)ldf8	ai2=[aptr],16		// ap[2]
	(p5)fcvt.fxu	ai2=f0
	cmp4.le		p8,p9=5,in5	}
{ .mfi;	(p6)ldf8	ai3=[r29],16		// ap[3]
	(p7)fcvt.fxu	ai3=f0
	cmp4.le		p10,p11=6,in5	}
{ .mfi;	(p4)ldf8	bj[5]=[bptr],16		// bp[2]
	(p5)fcvt.fxu	bj[5]=f0
	cmp4.le		p12,p13=7,in5	}
{ .mfi;	(p6)ldf8	bj[4]=[r30],16		// bp[3]
	(p7)fcvt.fxu	bj[4]=f0
	cmp4.le		p14,p15=8,in5	}
{ .mfi;	(p4)ldf8	ni2=[nptr],16		// np[2]
	(p5)fcvt.fxu	ni2=f0
	addp4		r28=-1,in5	}
{ .mfi;	(p6)ldf8	ni3=[r31],16		// np[3]
	(p7)fcvt.fxu	ni3=f0
	$ADDP		in4=0,in4	};;

{ .mfi;	ldf8		n0=[in4]
	fcvt.fxu	tf[1]=f0
	nop.i		0		}

{ .mfi;	(p8)ldf8	ai4=[aptr],16		// ap[4]
	(p9)fcvt.fxu	ai4=f0
	mov		t[0]=r0		}
{ .mfi;	(p10)ldf8	ai5=[r29],16		// ap[5]
	(p11)fcvt.fxu	ai5=f0
	mov		t[1]=r0		}
{ .mfi;	(p8)ldf8	bj[3]=[bptr],16		// bp[4]
	(p9)fcvt.fxu	bj[3]=f0
	mov		t[2]=r0		}
{ .mfi;	(p10)ldf8	bj[2]=[r30],16		// bp[5]
	(p11)fcvt.fxu	bj[2]=f0
	mov		t[3]=r0		}
{ .mfi;	(p8)ldf8	ni4=[nptr],16		// np[4]
	(p9)fcvt.fxu	ni4=f0
	mov		t[4]=r0		}
{ .mfi;	(p10)ldf8	ni5=[r31],16		// np[5]
	(p11)fcvt.fxu	ni5=f0
	mov		t[5]=r0		};;

{ .mfi;	(p12)ldf8	ai6=[aptr],16		// ap[6]
	(p13)fcvt.fxu	ai6=f0
	mov		t[6]=r0		}
{ .mfi;	(p14)ldf8	ai7=[r29],16		// ap[7]
	(p15)fcvt.fxu	ai7=f0
	mov		t[7]=r0		}
{ .mfi;	(p12)ldf8	bj[1]=[bptr],16		// bp[6]
	(p13)fcvt.fxu	bj[1]=f0
	mov		ar.lc=r28	}
{ .mfi;	(p14)ldf8	bj[0]=[r30],16		// bp[7]
	(p15)fcvt.fxu	bj[0]=f0
	mov		ar.ec=1		}
{ .mfi;	(p12)ldf8	ni6=[nptr],16		// np[6]
	(p13)fcvt.fxu	ni6=f0
	mov		pr.rot=1<<16	}
{ .mfb;	(p14)ldf8	ni7=[r31],16		// np[7]
	(p15)fcvt.fxu	ni7=f0
	brp.loop.imp	.Louter_8_ctop,.Louter_8_cend-16
					};;

// The loop is scheduled for 32*n ticks on Itanium 2. Actual attempt
// to measure with help of Interval Time Counter indicated that the
// factor is a tad higher: 33 or 34, if not 35. Exact measurement and
// addressing the issue is problematic, because I don't have access
// to platform-specific instruction-level profiler. On Itanium it
// should run in 56*n ticks, because of higher xma latency...
.Louter_8_ctop:
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mfi;	(p16)	nop.m		0			// 0:
	(p16)	xma.hu		ahi[0]=ai0,bj[7],tf[1]	//	ap[0]*b[i]+t[0]
	(p40)	add		a3=a3,n3	}	//	(p17) a3+=n3
{ .mfi;	(p42)	add		a3=a3,n3,1
	(p16)	xma.lu		alo[0]=ai0,bj[7],tf[1]
	(p16)	nop.i		0		};;
{ .mii;	(p17)	getf.sig	a7=alo[8]		// 1:
	(p48)	add		t[6]=t[6],a3		//	(p17) t[6]+=a3
	(p50)	add		t[6]=t[6],a3,1	};;
{ .mfi;	(p17)	getf.sig	a8=ahi[8]		// 2:
	(p17)	xma.hu		nhi[7]=ni6,mj[1],nhi[6]	//	np[6]*m0
	(p40)	cmp.ltu		p43,p41=a3,n3	}
{ .mfi;	(p42)	cmp.leu		p43,p41=a3,n3
	(p17)	xma.lu		nlo[7]=ni6,mj[1],nhi[6]
	(p16)	nop.i		0		};;
{ .mii;	(p17)	getf.sig	n5=nlo[6]		// 3:
	(p48)	cmp.ltu		p51,p49=t[6],a3
	(p50)	cmp.leu		p51,p49=t[6],a3	};;
	.pred.rel		"mutex",p41,p43
	.pred.rel		"mutex",p49,p51
{ .mfi;	(p16)	nop.m		0			// 4:
	(p16)	xma.hu		ahi[1]=ai1,bj[7],ahi[0]	//	ap[1]*b[i]
	(p41)	add		a4=a4,n4	}	//	(p17) a4+=n4
{ .mfi;	(p43)	add		a4=a4,n4,1
	(p16)	xma.lu		alo[1]=ai1,bj[7],ahi[0]
	(p16)	nop.i		0		};;
{ .mfi;	(p49)	add		t[5]=t[5],a4		// 5:	(p17) t[5]+=a4
	(p16)	xmpy.lu		mj[0]=alo[0],n0		//	(ap[0]*b[i]+t[0])*n0
	(p51)	add		t[5]=t[5],a4,1	};;
{ .mfi;	(p16)	nop.m		0			// 6:
	(p17)	xma.hu		nhi[8]=ni7,mj[1],nhi[7]	//	np[7]*m0
	(p41)	cmp.ltu		p42,p40=a4,n4	}
{ .mfi;	(p43)	cmp.leu		p42,p40=a4,n4
	(p17)	xma.lu		nlo[8]=ni7,mj[1],nhi[7]
	(p16)	nop.i		0		};;
{ .mii;	(p17)	getf.sig	n6=nlo[7]		// 7:
	(p49)	cmp.ltu		p50,p48=t[5],a4
	(p51)	cmp.leu		p50,p48=t[5],a4	};;
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mfi;	(p16)	nop.m		0			// 8:
	(p16)	xma.hu		ahi[2]=ai2,bj[7],ahi[1]	//	ap[2]*b[i]
	(p40)	add		a5=a5,n5	}	//	(p17) a5+=n5
{ .mfi;	(p42)	add		a5=a5,n5,1
	(p16)	xma.lu		alo[2]=ai2,bj[7],ahi[1]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	a1=alo[1]		// 9:
	(p48)	add		t[4]=t[4],a5		//	p(17) t[4]+=a5
	(p50)	add		t[4]=t[4],a5,1	};;
{ .mfi;	(p16)	nop.m		0			// 10:
	(p16)	xma.hu		nhi[0]=ni0,mj[0],alo[0]	//	np[0]*m0
	(p40)	cmp.ltu		p43,p41=a5,n5	}
{ .mfi;	(p42)	cmp.leu		p43,p41=a5,n5
	(p16)	xma.lu		nlo[0]=ni0,mj[0],alo[0]
	(p16)	nop.i		0		};;
{ .mii;	(p17)	getf.sig	n7=nlo[8]		// 11:
	(p48)	cmp.ltu		p51,p49=t[4],a5
	(p50)	cmp.leu		p51,p49=t[4],a5	};;
	.pred.rel		"mutex",p41,p43
	.pred.rel		"mutex",p49,p51
{ .mfi;	(p17)	getf.sig	n8=nhi[8]		// 12:
	(p16)	xma.hu		ahi[3]=ai3,bj[7],ahi[2]	//	ap[3]*b[i]
	(p41)	add		a6=a6,n6	}	//	(p17) a6+=n6
{ .mfi;	(p43)	add		a6=a6,n6,1
	(p16)	xma.lu		alo[3]=ai3,bj[7],ahi[2]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	a2=alo[2]		// 13:
	(p49)	add		t[3]=t[3],a6		//	(p17) t[3]+=a6
	(p51)	add		t[3]=t[3],a6,1	};;
{ .mfi;	(p16)	nop.m		0			// 14:
	(p16)	xma.hu		nhi[1]=ni1,mj[0],nhi[0]	//	np[1]*m0
	(p41)	cmp.ltu		p42,p40=a6,n6	}
{ .mfi;	(p43)	cmp.leu		p42,p40=a6,n6
	(p16)	xma.lu		nlo[1]=ni1,mj[0],nhi[0]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	nop.m		0			// 15:
	(p49)	cmp.ltu		p50,p48=t[3],a6
	(p51)	cmp.leu		p50,p48=t[3],a6	};;
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mfi;	(p16)	nop.m		0			// 16:
	(p16)	xma.hu		ahi[4]=ai4,bj[7],ahi[3]	//	ap[4]*b[i]
	(p40)	add		a7=a7,n7	}	//	(p17) a7+=n7
{ .mfi;	(p42)	add		a7=a7,n7,1
	(p16)	xma.lu		alo[4]=ai4,bj[7],ahi[3]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	a3=alo[3]		// 17:
	(p48)	add		t[2]=t[2],a7		//	(p17) t[2]+=a7
	(p50)	add		t[2]=t[2],a7,1	};;
{ .mfi;	(p16)	nop.m		0			// 18:
	(p16)	xma.hu		nhi[2]=ni2,mj[0],nhi[1]	//	np[2]*m0
	(p40)	cmp.ltu		p43,p41=a7,n7	}
{ .mfi;	(p42)	cmp.leu		p43,p41=a7,n7
	(p16)	xma.lu		nlo[2]=ni2,mj[0],nhi[1]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	n1=nlo[1]		// 19:
	(p48)	cmp.ltu		p51,p49=t[2],a7
	(p50)	cmp.leu		p51,p49=t[2],a7	};;
	.pred.rel		"mutex",p41,p43
	.pred.rel		"mutex",p49,p51
{ .mfi;	(p16)	nop.m		0			// 20:
	(p16)	xma.hu		ahi[5]=ai5,bj[7],ahi[4]	//	ap[5]*b[i]
	(p41)	add		a8=a8,n8	}	//	(p17) a8+=n8
{ .mfi;	(p43)	add		a8=a8,n8,1
	(p16)	xma.lu		alo[5]=ai5,bj[7],ahi[4]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	a4=alo[4]		// 21:
	(p49)	add		t[1]=t[1],a8		//	(p17) t[1]+=a8
	(p51)	add		t[1]=t[1],a8,1	};;
{ .mfi;	(p16)	nop.m		0			// 22:
	(p16)	xma.hu		nhi[3]=ni3,mj[0],nhi[2]	//	np[3]*m0
	(p41)	cmp.ltu		p42,p40=a8,n8	}
{ .mfi;	(p43)	cmp.leu		p42,p40=a8,n8
	(p16)	xma.lu		nlo[3]=ni3,mj[0],nhi[2]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	n2=nlo[2]		// 23:
	(p49)	cmp.ltu		p50,p48=t[1],a8
	(p51)	cmp.leu		p50,p48=t[1],a8	};;
{ .mfi;	(p16)	nop.m		0			// 24:
	(p16)	xma.hu		ahi[6]=ai6,bj[7],ahi[5]	//	ap[6]*b[i]
	(p16)	add		a1=a1,n1	}	//	(p16) a1+=n1
{ .mfi;	(p16)	nop.m		0
	(p16)	xma.lu		alo[6]=ai6,bj[7],ahi[5]
	(p17)	mov		t[0]=r0		};;
{ .mii;	(p16)	getf.sig	a5=alo[5]		// 25:
	(p16)	add		t0=t[7],a1		//	(p16) t[7]+=a1
	(p42)	add		t[0]=t[0],r0,1	};;
{ .mfi;	(p16)	setf.sig	tf[0]=t0		// 26:
	(p16)	xma.hu		nhi[4]=ni4,mj[0],nhi[3]	//	np[4]*m0
	(p50)	add		t[0]=t[0],r0,1	}
{ .mfi;	(p16)	cmp.ltu.unc	p42,p40=a1,n1
	(p16)	xma.lu		nlo[4]=ni4,mj[0],nhi[3]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	n3=nlo[3]		// 27:
	(p16)	cmp.ltu.unc	p50,p48=t0,a1
	(p16)	nop.i		0		};;
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mfi;	(p16)	nop.m		0			// 28:
	(p16)	xma.hu		ahi[7]=ai7,bj[7],ahi[6]	//	ap[7]*b[i]
	(p40)	add		a2=a2,n2	}	//	(p16) a2+=n2
{ .mfi;	(p42)	add		a2=a2,n2,1
	(p16)	xma.lu		alo[7]=ai7,bj[7],ahi[6]
	(p16)	nop.i		0		};;
{ .mii;	(p16)	getf.sig	a6=alo[6]		// 29:
	(p48)	add		t[6]=t[6],a2		//	(p16) t[6]+=a2
	(p50)	add		t[6]=t[6],a2,1	};;
{ .mfi;	(p16)	nop.m		0			// 30:
	(p16)	xma.hu		nhi[5]=ni5,mj[0],nhi[4]	//	np[5]*m0
	(p40)	cmp.ltu		p41,p39=a2,n2	}
{ .mfi;	(p42)	cmp.leu		p41,p39=a2,n2
	(p16)	xma.lu		nlo[5]=ni5,mj[0],nhi[4]
	(p16)	nop.i		0		};;
{ .mfi;	(p16)	getf.sig	n4=nlo[4]		// 31:
	(p16)	nop.f		0
	(p48)	cmp.ltu		p49,p47=t[6],a2	}
{ .mfb;	(p50)	cmp.leu		p49,p47=t[6],a2
	(p16)	nop.f		0
	br.ctop.sptk.many	.Louter_8_ctop	};;
.Louter_8_cend:

// above loop has to execute one more time, without (p16), which is
// replaced with merged move of np[8] to GPR bank
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mmi;	(p0)	getf.sig	n1=ni0			// 0:
	(p40)	add		a3=a3,n3		//	(p17) a3+=n3
	(p42)	add		a3=a3,n3,1	};;
{ .mii;	(p17)	getf.sig	a7=alo[8]		// 1:
	(p48)	add		t[6]=t[6],a3		//	(p17) t[6]+=a3
	(p50)	add		t[6]=t[6],a3,1	};;
{ .mfi;	(p17)	getf.sig	a8=ahi[8]		// 2:
	(p17)	xma.hu		nhi[7]=ni6,mj[1],nhi[6]	//	np[6]*m0
	(p40)	cmp.ltu		p43,p41=a3,n3	}
{ .mfi;	(p42)	cmp.leu		p43,p41=a3,n3
	(p17)	xma.lu		nlo[7]=ni6,mj[1],nhi[6]
	(p0)	nop.i		0		};;
{ .mii;	(p17)	getf.sig	n5=nlo[6]		// 3:
	(p48)	cmp.ltu		p51,p49=t[6],a3
	(p50)	cmp.leu		p51,p49=t[6],a3	};;
	.pred.rel		"mutex",p41,p43
	.pred.rel		"mutex",p49,p51
{ .mmi;	(p0)	getf.sig	n2=ni1			// 4:
	(p41)	add		a4=a4,n4		//	(p17) a4+=n4
	(p43)	add		a4=a4,n4,1	};;
{ .mfi;	(p49)	add		t[5]=t[5],a4		// 5:	(p17) t[5]+=a4
	(p0)	nop.f		0
	(p51)	add		t[5]=t[5],a4,1	};;
{ .mfi;	(p0)	getf.sig	n3=ni2			// 6:
	(p17)	xma.hu		nhi[8]=ni7,mj[1],nhi[7]	//	np[7]*m0
	(p41)	cmp.ltu		p42,p40=a4,n4	}
{ .mfi;	(p43)	cmp.leu		p42,p40=a4,n4
	(p17)	xma.lu		nlo[8]=ni7,mj[1],nhi[7]
	(p0)	nop.i		0		};;
{ .mii;	(p17)	getf.sig	n6=nlo[7]		// 7:
	(p49)	cmp.ltu		p50,p48=t[5],a4
	(p51)	cmp.leu		p50,p48=t[5],a4	};;
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mii;	(p0)	getf.sig	n4=ni3			// 8:
	(p40)	add		a5=a5,n5		//	(p17) a5+=n5
	(p42)	add		a5=a5,n5,1	};;
{ .mii;	(p0)	nop.m		0			// 9:
	(p48)	add		t[4]=t[4],a5		//	p(17) t[4]+=a5
	(p50)	add		t[4]=t[4],a5,1	};;
{ .mii;	(p0)	nop.m		0			// 10:
	(p40)	cmp.ltu		p43,p41=a5,n5
	(p42)	cmp.leu		p43,p41=a5,n5	};;
{ .mii;	(p17)	getf.sig	n7=nlo[8]		// 11:
	(p48)	cmp.ltu		p51,p49=t[4],a5
	(p50)	cmp.leu		p51,p49=t[4],a5	};;
	.pred.rel		"mutex",p41,p43
	.pred.rel		"mutex",p49,p51
{ .mii;	(p17)	getf.sig	n8=nhi[8]		// 12:
	(p41)	add		a6=a6,n6		//	(p17) a6+=n6
	(p43)	add		a6=a6,n6,1	};;
{ .mii;	(p0)	getf.sig	n5=ni4			// 13:
	(p49)	add		t[3]=t[3],a6		//	(p17) t[3]+=a6
	(p51)	add		t[3]=t[3],a6,1	};;
{ .mii;	(p0)	nop.m		0			// 14:
	(p41)	cmp.ltu		p42,p40=a6,n6
	(p43)	cmp.leu		p42,p40=a6,n6	};;
{ .mii;	(p0)	getf.sig	n6=ni5			// 15:
	(p49)	cmp.ltu		p50,p48=t[3],a6
	(p51)	cmp.leu		p50,p48=t[3],a6	};;
	.pred.rel		"mutex",p40,p42
	.pred.rel		"mutex",p48,p50
{ .mii;	(p0)	nop.m		0			// 16:
	(p40)	add		a7=a7,n7		//	(p17) a7+=n7
	(p42)	add		a7=a7,n7,1	};;
{ .mii;	(p0)	nop.m		0			// 17:
	(p48)	add		t[2]=t[2],a7		//	(p17) t[2]+=a7
	(p50)	add		t[2]=t[2],a7,1	};;
{ .mii;	(p0)	nop.m		0			// 18:
	(p40)	cmp.ltu		p43,p41=a7,n7
	(p42)	cmp.leu		p43,p41=a7,n7	};;
{ .mii;	(p0)	getf.sig	n7=ni6			// 19:
	(p48)	cmp.ltu		p51,p49=t[2],a7
	(p50)	cmp.leu		p51,p49=t[2],a7	};;
	.pred.rel		"mutex",p41,p43
	.pred.rel		"mutex",p49,p51
{ .mii;	(p0)	nop.m		0			// 20:
	(p41)	add		a8=a8,n8		//	(p17) a8+=n8
	(p43)	add		a8=a8,n8,1	};;
{ .mmi;	(p0)	nop.m		0			// 21:
	(p49)	add		t[1]=t[1],a8		//	(p17) t[1]+=a8
	(p51)	add		t[1]=t[1],a8,1	}
{ .mmi;	(p17)	mov		t[0]=r0
	(p41)	cmp.ltu		p42,p40=a8,n8
	(p43)	cmp.leu		p42,p40=a8,n8	};;
{ .mmi;	(p0)	getf.sig	n8=ni7			// 22:
	(p49)	cmp.ltu		p50,p48=t[1],a8
	(p51)	cmp.leu		p50,p48=t[1],a8	}
{ .mmi;	(p42)	add		t[0]=t[0],r0,1
	(p0)	add		r16=-7*16,prevsp
	(p0)	add		r17=-6*16,prevsp	};;

// subtract np[8] from carrybit|tmp[8]
// carrybit|tmp[8] layout upon exit from above loop is:
//	t[0]|t[1]|t[2]|t[3]|t[4]|t[5]|t[6]|t[7]|t0 (least significant)
{ .mmi;	(p50)add	t[0]=t[0],r0,1
	add		r18=-5*16,prevsp
	sub		n1=t0,n1	};;
{ .mmi;	cmp.gtu		p34,p32=n1,t0;;
	.pred.rel	"mutex",p32,p34
	(p32)sub	n2=t[7],n2
	(p34)sub	n2=t[7],n2,1	};;
{ .mii;	(p32)cmp.gtu	p35,p33=n2,t[7]
	(p34)cmp.geu	p35,p33=n2,t[7];;
	.pred.rel	"mutex",p33,p35
	(p33)sub	n3=t[6],n3	}
{ .mmi;	(p35)sub	n3=t[6],n3,1;;
	(p33)cmp.gtu	p34,p32=n3,t[6]
	(p35)cmp.geu	p34,p32=n3,t[6]	};;
	.pred.rel	"mutex",p32,p34
{ .mii;	(p32)sub	n4=t[5],n4
	(p34)sub	n4=t[5],n4,1;;
	(p32)cmp.gtu	p35,p33=n4,t[5]	}
{ .mmi;	(p34)cmp.geu	p35,p33=n4,t[5];;
	.pred.rel	"mutex",p33,p35
	(p33)sub	n5=t[4],n5
	(p35)sub	n5=t[4],n5,1	};;
{ .mii;	(p33)cmp.gtu	p34,p32=n5,t[4]
	(p35)cmp.geu	p34,p32=n5,t[4];;
	.pred.rel	"mutex",p32,p34
	(p32)sub	n6=t[3],n6	}
{ .mmi;	(p34)sub	n6=t[3],n6,1;;
	(p32)cmp.gtu	p35,p33=n6,t[3]
	(p34)cmp.geu	p35,p33=n6,t[3]	};;
	.pred.rel	"mutex",p33,p35
{ .mii;	(p33)sub	n7=t[2],n7
	(p35)sub	n7=t[2],n7,1;;
	(p33)cmp.gtu	p34,p32=n7,t[2]	}
{ .mmi;	(p35)cmp.geu	p34,p32=n7,t[2];;
	.pred.rel	"mutex",p32,p34
	(p32)sub	n8=t[1],n8
	(p34)sub	n8=t[1],n8,1	};;
{ .mii;	(p32)cmp.gtu	p35,p33=n8,t[1]
	(p34)cmp.geu	p35,p33=n8,t[1];;
	.pred.rel	"mutex",p33,p35
	(p33)sub	a8=t[0],r0	}
{ .mmi;	(p35)sub	a8=t[0],r0,1;;
	(p33)cmp.gtu	p34,p32=a8,t[0]
	(p35)cmp.geu	p34,p32=a8,t[0]	};;

// save the result, either tmp[num] or tmp[num]-np[num]
	.pred.rel	"mutex",p32,p34
{ .mmi;	(p32)st8	[rptr]=n1,8
	(p34)st8	[rptr]=t0,8
	add		r19=-4*16,prevsp};;
{ .mmb;	(p32)st8	[rptr]=n2,8
	(p34)st8	[rptr]=t[7],8
	(p5)br.cond.dpnt.few	.Ldone	};;
{ .mmb;	(p32)st8	[rptr]=n3,8
	(p34)st8	[rptr]=t[6],8
	(p7)br.cond.dpnt.few	.Ldone	};;
{ .mmb;	(p32)st8	[rptr]=n4,8
	(p34)st8	[rptr]=t[5],8
	(p9)br.cond.dpnt.few	.Ldone	};;
{ .mmb;	(p32)st8	[rptr]=n5,8
	(p34)st8	[rptr]=t[4],8
	(p11)br.cond.dpnt.few	.Ldone	};;
{ .mmb;	(p32)st8	[rptr]=n6,8
	(p34)st8	[rptr]=t[3],8
	(p13)br.cond.dpnt.few	.Ldone	};;
{ .mmb;	(p32)st8	[rptr]=n7,8
	(p34)st8	[rptr]=t[2],8
	(p15)br.cond.dpnt.few	.Ldone	};;
{ .mmb;	(p32)st8	[rptr]=n8,8
	(p34)st8	[rptr]=t[1],8
	nop.b		0		};;
.Ldone:						// epilogue
{ .mmi;	ldf.fill	f16=[r16],64
	ldf.fill	f17=[r17],64
	nop.i		0		}
{ .mmi;	ldf.fill	f18=[r18],64
	ldf.fill	f19=[r19],64
	mov		pr=prevpr,0x1ffff	};;
{ .mmi;	ldf.fill	f20=[r16]
	ldf.fill	f21=[r17]
	mov		ar.lc=prevlc	}
{ .mmi;	ldf.fill	f22=[r18]
	ldf.fill	f23=[r19]
	mov		ret0=1		}	// signal "handled"
{ .mib;	rum		1<<5
	.restore	sp
	mov		sp=prevsp
	br.ret.sptk.many	b0	};;
.endp	bn_mul_mont_8#

.type	copyright#,\@object
copyright:
stringz	"Montgomery multiplication for IA-64, CRYPTOGAMS by <appro\@openssl.org>"
___

open STDOUT,">$output" if $output;
print $code;
close STDOUT;
