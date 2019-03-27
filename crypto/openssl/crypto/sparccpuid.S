! Copyright 2005-2016 The OpenSSL Project Authors. All Rights Reserved.
!
! Licensed under the OpenSSL license (the "License").  You may not use
! this file except in compliance with the License.  You can obtain a copy
! in the file LICENSE in the source distribution or at
! https://www.openssl.org/source/license.html

#if defined(__SUNPRO_C) && defined(__sparcv9)
# define ABI64  /* They've said -xarch=v9 at command line */
#elif defined(__GNUC__) && defined(__arch64__)
# define ABI64  /* They've said -m64 at command line */
#endif

#ifdef ABI64
  .register	%g2,#scratch
  .register	%g3,#scratch
# define	FRAME	-192
# define	BIAS	2047
#else
# define	FRAME	-96
# define	BIAS	0
#endif

.text
.align	32
.global	OPENSSL_wipe_cpu
.type	OPENSSL_wipe_cpu,#function
! Keep in mind that this does not excuse us from wiping the stack!
! This routine wipes registers, but not the backing store [which
! resides on the stack, toward lower addresses]. To facilitate for
! stack wiping I return pointer to the top of stack of the *caller*.
OPENSSL_wipe_cpu:
	save	%sp,FRAME,%sp
	nop
#ifdef __sun
#include <sys/trap.h>
	ta	ST_CLEAN_WINDOWS
#else
	call	.walk.reg.wins
#endif
	nop
	call	.PIC.zero.up
	mov	.zero-(.-4),%o0
	ld	[%o0],%f0
	ld	[%o0],%f1

	subcc	%g0,1,%o0
	! Following is V9 "rd %ccr,%o0" instruction. However! V8
	! specification says that it ("rd %asr2,%o0" in V8 terms) does
	! not cause illegal_instruction trap. It therefore can be used
	! to determine if the CPU the code is executing on is V8- or
	! V9-compliant, as V9 returns a distinct value of 0x99,
	! "negative" and "borrow" bits set in both %icc and %xcc.
	.word	0x91408000	!rd	%ccr,%o0
	cmp	%o0,0x99
	bne	.v8
	nop
			! Even though we do not use %fp register bank,
			! we wipe it as memcpy might have used it...
			.word	0xbfa00040	!fmovd	%f0,%f62
			.word	0xbba00040	!...
			.word	0xb7a00040
			.word	0xb3a00040
			.word	0xafa00040
			.word	0xaba00040
			.word	0xa7a00040
			.word	0xa3a00040
			.word	0x9fa00040
			.word	0x9ba00040
			.word	0x97a00040
			.word	0x93a00040
			.word	0x8fa00040
			.word	0x8ba00040
			.word	0x87a00040
			.word	0x83a00040	!fmovd	%f0,%f32
.v8:			fmovs	%f1,%f31
	clr	%o0
			fmovs	%f0,%f30
	clr	%o1
			fmovs	%f1,%f29
	clr	%o2
			fmovs	%f0,%f28
	clr	%o3
			fmovs	%f1,%f27
	clr	%o4
			fmovs	%f0,%f26
	clr	%o5
			fmovs	%f1,%f25
	clr	%o7
			fmovs	%f0,%f24
	clr	%l0
			fmovs	%f1,%f23
	clr	%l1
			fmovs	%f0,%f22
	clr	%l2
			fmovs	%f1,%f21
	clr	%l3
			fmovs	%f0,%f20
	clr	%l4
			fmovs	%f1,%f19
	clr	%l5
			fmovs	%f0,%f18
	clr	%l6
			fmovs	%f1,%f17
	clr	%l7
			fmovs	%f0,%f16
	clr	%i0
			fmovs	%f1,%f15
	clr	%i1
			fmovs	%f0,%f14
	clr	%i2
			fmovs	%f1,%f13
	clr	%i3
			fmovs	%f0,%f12
	clr	%i4
			fmovs	%f1,%f11
	clr	%i5
			fmovs	%f0,%f10
	clr	%g1
			fmovs	%f1,%f9
	clr	%g2
			fmovs	%f0,%f8
	clr	%g3
			fmovs	%f1,%f7
	clr	%g4
			fmovs	%f0,%f6
	clr	%g5
			fmovs	%f1,%f5
			fmovs	%f0,%f4
			fmovs	%f1,%f3
			fmovs	%f0,%f2

	add	%fp,BIAS,%i0	! return pointer to caller´s top of stack

	ret
	restore

.zero:	.long	0x0,0x0
.PIC.zero.up:
	retl
	add	%o0,%o7,%o0
#ifdef DEBUG
.global	walk_reg_wins
.type	walk_reg_wins,#function
walk_reg_wins:
#endif
.walk.reg.wins:
	save	%sp,FRAME,%sp
	cmp	%i7,%o7
	be	2f
	clr	%o0
	cmp	%o7,0	! compiler never cleans %o7...
	be	1f	! could have been a leaf function...
	clr	%o1
	call	.walk.reg.wins
	nop
1:	clr	%o2
	clr	%o3
	clr	%o4
	clr	%o5
	clr	%o7
	clr	%l0
	clr	%l1
	clr	%l2
	clr	%l3
	clr	%l4
	clr	%l5
	clr	%l6
	clr	%l7
	add	%o0,1,%i0	! used for debugging
2:	ret
	restore
.size	OPENSSL_wipe_cpu,.-OPENSSL_wipe_cpu

.global	OPENSSL_atomic_add
.type	OPENSSL_atomic_add,#function
.align	32
OPENSSL_atomic_add:
#ifndef ABI64
	subcc	%g0,1,%o2
	.word	0x95408000	!rd	%ccr,%o2, see comment above
	cmp	%o2,0x99
	be	.v9
	nop
	save	%sp,FRAME,%sp
	ba	.enter
	nop
#ifdef __sun
! Note that you do not have to link with libthread to call thr_yield,
! as libc provides a stub, which is overloaded the moment you link
! with *either* libpthread or libthread...
#define	YIELD_CPU	thr_yield
#else
! applies at least to Linux and FreeBSD... Feedback expected...
#define	YIELD_CPU	sched_yield
#endif
.spin:	call	YIELD_CPU
	nop
.enter:	ld	[%i0],%i2
	cmp	%i2,-4096
	be	.spin
	mov	-1,%i2
	swap	[%i0],%i2
	cmp	%i2,-1
	be	.spin
	add	%i2,%i1,%i2
	stbar
	st	%i2,[%i0]
	sra	%i2,%g0,%i0
	ret
	restore
.v9:
#endif
	ld	[%o0],%o2
1:	add	%o1,%o2,%o3
	.word	0xd7e2100a	!cas [%o0],%o2,%o3, compare [%o0] with %o2 and swap %o3
	cmp	%o2,%o3
	bne	1b
	mov	%o3,%o2		! cas is always fetching to dest. register
	add	%o1,%o2,%o0	! OpenSSL expects the new value
	retl
	sra	%o0,%g0,%o0	! we return signed int, remember?
.size	OPENSSL_atomic_add,.-OPENSSL_atomic_add

.global	_sparcv9_rdtick
.align	32
_sparcv9_rdtick:
	subcc	%g0,1,%o0
	.word	0x91408000	!rd	%ccr,%o0
	cmp	%o0,0x99
	bne	.notick
	xor	%o0,%o0,%o0
	.word	0x91410000	!rd	%tick,%o0
	retl
	.word	0x93323020	!srlx	%o0,32,%o1
.notick:
	retl
	xor	%o1,%o1,%o1
.type	_sparcv9_rdtick,#function
.size	_sparcv9_rdtick,.-_sparcv9_rdtick

.global	_sparcv9_vis1_probe
.align	8
_sparcv9_vis1_probe:
	add	%sp,BIAS+2,%o1
	.word	0xc19a5a40	!ldda	[%o1]ASI_FP16_P,%f0
	retl
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
.type	_sparcv9_vis1_probe,#function
.size	_sparcv9_vis1_probe,.-_sparcv9_vis1_probe

! Probe and instrument VIS1 instruction. Output is number of cycles it
! takes to execute rdtick and pair of VIS1 instructions. US-Tx VIS unit
! is slow (documented to be 6 cycles on T2) and the core is in-order
! single-issue, it should be possible to distinguish Tx reliably...
! Observed return values are:
!
!	UltraSPARC IIe		7
!	UltraSPARC III		7
!	UltraSPARC T1		24
!	SPARC T4		65(*)
!
! (*)	result has lesser to do with VIS instruction latencies, rdtick
!	appears that slow, but it does the trick in sense that FP and
!	VIS code paths are still slower than integer-only ones.
!
! Numbers for T2 and SPARC64 V-VII are more than welcomed.
!
! It would be possible to detect specifically US-T1 by instrumenting
! fmul8ulx16, which is emulated on T1 and as such accounts for quite
! a lot of %tick-s, couple of thousand on Linux...
.global	_sparcv9_vis1_instrument
.align	8
_sparcv9_vis1_instrument:
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
	.word	0x85b08d82	!fxor	%f2,%f2,%f2
	.word	0x91410000	!rd	%tick,%o0
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
	.word	0x85b08d82	!fxor	%f2,%f2,%f2
	.word	0x93410000	!rd	%tick,%o1
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
	.word	0x85b08d82	!fxor	%f2,%f2,%f2
	.word	0x95410000	!rd	%tick,%o2
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
	.word	0x85b08d82	!fxor	%f2,%f2,%f2
	.word	0x97410000	!rd	%tick,%o3
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
	.word	0x85b08d82	!fxor	%f2,%f2,%f2
	.word	0x99410000	!rd	%tick,%o4

	! calculate intervals
	sub	%o1,%o0,%o0
	sub	%o2,%o1,%o1
	sub	%o3,%o2,%o2
	sub	%o4,%o3,%o3

	! find minimum value
	cmp	%o0,%o1
	.word	0x38680002	!bgu,a	%xcc,.+8
	mov	%o1,%o0
	cmp	%o0,%o2
	.word	0x38680002	!bgu,a	%xcc,.+8
	mov	%o2,%o0
	cmp	%o0,%o3
	.word	0x38680002	!bgu,a	%xcc,.+8
	mov	%o3,%o0

	retl
	nop
.type	_sparcv9_vis1_instrument,#function
.size	_sparcv9_vis1_instrument,.-_sparcv9_vis1_instrument

.global	_sparcv9_vis2_probe
.align	8
_sparcv9_vis2_probe:
	retl
	.word	0x81b00980	!bshuffle	%f0,%f0,%f0
.type	_sparcv9_vis2_probe,#function
.size	_sparcv9_vis2_probe,.-_sparcv9_vis2_probe

.global	_sparcv9_fmadd_probe
.align	8
_sparcv9_fmadd_probe:
	.word	0x81b00d80	!fxor	%f0,%f0,%f0
	.word	0x85b08d82	!fxor	%f2,%f2,%f2
	retl
	.word	0x81b80440	!fmaddd	%f0,%f0,%f2,%f0
.type	_sparcv9_fmadd_probe,#function
.size	_sparcv9_fmadd_probe,.-_sparcv9_fmadd_probe

.global	_sparcv9_rdcfr
.align	8
_sparcv9_rdcfr:
	retl
	.word	0x91468000	!rd	%asr26,%o0
.type	_sparcv9_rdcfr,#function
.size	_sparcv9_rdcfr,.-_sparcv9_rdcfr

.global	_sparcv9_vis3_probe
.align	8
_sparcv9_vis3_probe:
	retl
	.word	0x81b022a0	!xmulx	%g0,%g0,%g0
.type	_sparcv9_vis3_probe,#function
.size	_sparcv9_vis3_probe,.-_sparcv9_vis3_probe

.global	_sparcv9_random
.align	8
_sparcv9_random:
	retl
	.word	0x91b002a0	!random	%o0
.type	_sparcv9_random,#function
.size	_sparcv9_random,.-_sparcv9_vis3_probe

.global	_sparcv9_fjaesx_probe
.align	8
_sparcv9_fjaesx_probe:
	.word	0x81b09206	!faesencx %f2,%f6,%f0
	retl
	nop
.size	_sparcv9_fjaesx_probe,.-_sparcv9_fjaesx_probe

.global	OPENSSL_cleanse
.align	32
OPENSSL_cleanse:
	cmp	%o1,14
	nop
#ifdef ABI64
	bgu	%xcc,.Lot
#else
	bgu	.Lot
#endif
	cmp	%o1,0
	bne	.Little
	nop
	retl
	nop

.Little:
	stb	%g0,[%o0]
	subcc	%o1,1,%o1
	bnz	.Little
	add	%o0,1,%o0
	retl
	nop
.align	32
.Lot:
#ifndef ABI64
	subcc	%g0,1,%g1
	! see above for explanation
	.word	0x83408000	!rd	%ccr,%g1
	cmp	%g1,0x99
	bne	.v8lot
	nop
#endif

.v9lot:	andcc	%o0,7,%g0
	bz	.v9aligned
	nop
	stb	%g0,[%o0]
	sub	%o1,1,%o1
	ba	.v9lot
	add	%o0,1,%o0
.align	16,0x01000000
.v9aligned:
	.word	0xc0720000	!stx	%g0,[%o0]
	sub	%o1,8,%o1
	andcc	%o1,-8,%g0
#ifdef ABI64
	.word	0x126ffffd	!bnz	%xcc,.v9aligned
#else
	.word	0x124ffffd	!bnz	%icc,.v9aligned
#endif
	add	%o0,8,%o0

	cmp	%o1,0
	bne	.Little
	nop
	retl
	nop
#ifndef ABI64
.v8lot:	andcc	%o0,3,%g0
	bz	.v8aligned
	nop
	stb	%g0,[%o0]
	sub	%o1,1,%o1
	ba	.v8lot
	add	%o0,1,%o0
	nop
.v8aligned:
	st	%g0,[%o0]
	sub	%o1,4,%o1
	andcc	%o1,-4,%g0
	bnz	.v8aligned
	add	%o0,4,%o0

	cmp	%o1,0
	bne	.Little
	nop
	retl
	nop
#endif
.type	OPENSSL_cleanse,#function
.size	OPENSSL_cleanse,.-OPENSSL_cleanse

.global	CRYPTO_memcmp
.align	16
CRYPTO_memcmp:
	cmp	%o2,0
#ifdef ABI64
	beq,pn	%xcc,.Lno_data
#else
	beq	.Lno_data
#endif
	xor	%g1,%g1,%g1
	nop

.Loop_cmp:
	ldub	[%o0],%o3
	add	%o0,1,%o0
	ldub	[%o1],%o4
	add	%o1,1,%o1
	subcc	%o2,1,%o2
	xor	%o3,%o4,%o4
#ifdef ABI64
	bnz	%xcc,.Loop_cmp
#else
	bnz	.Loop_cmp
#endif
	or	%o4,%g1,%g1

	sub	%g0,%g1,%g1
	srl	%g1,31,%g1
.Lno_data:
	retl
	mov	%g1,%o0
.type	CRYPTO_memcmp,#function
.size	CRYPTO_memcmp,.-CRYPTO_memcmp

.global	_sparcv9_vis1_instrument_bus
.align	8
_sparcv9_vis1_instrument_bus:
	mov	%o1,%o3					! save cnt
	.word	0x99410000	!rd	%tick,%o4	! tick
	mov	%o4,%o5					! lasttick = tick
	set	0,%g4					! diff

	andn	%o0,63,%g1
	.word	0xc1985e00	!ldda	[%g1]0xf0,%f0	! block load
	.word	0x8143e040	!membar	#Sync
	.word	0xc1b85c00	!stda	%f0,[%g1]0xe0	! block store and commit
	.word	0x8143e040	!membar	#Sync
	ld	[%o0],%o4
	add	%o4,%g4,%g4
	.word	0xc9e2100c	!cas	[%o0],%o4,%g4

.Loop:	.word	0x99410000	!rd	%tick,%o4
	sub	%o4,%o5,%g4				! diff=tick-lasttick
	mov	%o4,%o5					! lasttick=tick

	andn	%o0,63,%g1
	.word	0xc1985e00	!ldda	[%g1]0xf0,%f0	! block load
	.word	0x8143e040	!membar	#Sync
	.word	0xc1b85c00	!stda	%f0,[%g1]0xe0	! block store and commit
	.word	0x8143e040	!membar	#Sync
	ld	[%o0],%o4
	add	%o4,%g4,%g4
	.word	0xc9e2100c	!cas	[%o0],%o4,%g4
	subcc	%o1,1,%o1				! --$cnt
	bnz	.Loop
	add	%o0,4,%o0				! ++$out

	retl
	mov	%o3,%o0
.type	_sparcv9_vis1_instrument_bus,#function
.size	_sparcv9_vis1_instrument_bus,.-_sparcv9_vis1_instrument_bus

.global	_sparcv9_vis1_instrument_bus2
.align	8
_sparcv9_vis1_instrument_bus2:
	mov	%o1,%o3					! save cnt
	sll	%o1,2,%o1				! cnt*=4

	.word	0x99410000	!rd	%tick,%o4	! tick
	mov	%o4,%o5					! lasttick = tick
	set	0,%g4					! diff

	andn	%o0,63,%g1
	.word	0xc1985e00	!ldda	[%g1]0xf0,%f0	! block load
	.word	0x8143e040	!membar	#Sync
	.word	0xc1b85c00	!stda	%f0,[%g1]0xe0	! block store and commit
	.word	0x8143e040	!membar	#Sync
	ld	[%o0],%o4
	add	%o4,%g4,%g4
	.word	0xc9e2100c	!cas	[%o0],%o4,%g4

	.word	0x99410000	!rd	%tick,%o4	! tick
	sub	%o4,%o5,%g4				! diff=tick-lasttick
	mov	%o4,%o5					! lasttick=tick
	mov	%g4,%g5					! lastdiff=diff
.Loop2:
	andn	%o0,63,%g1
	.word	0xc1985e00	!ldda	[%g1]0xf0,%f0	! block load
	.word	0x8143e040	!membar	#Sync
	.word	0xc1b85c00	!stda	%f0,[%g1]0xe0	! block store and commit
	.word	0x8143e040	!membar	#Sync
	ld	[%o0],%o4
	add	%o4,%g4,%g4
	.word	0xc9e2100c	!cas	[%o0],%o4,%g4

	subcc	%o2,1,%o2				! --max
	bz	.Ldone2
	nop

	.word	0x99410000	!rd	%tick,%o4	! tick
	sub	%o4,%o5,%g4				! diff=tick-lasttick
	mov	%o4,%o5					! lasttick=tick
	cmp	%g4,%g5
	mov	%g4,%g5					! lastdiff=diff

	.word	0x83408000	!rd	%ccr,%g1
	and	%g1,4,%g1				! isolate zero flag
	xor	%g1,4,%g1				! flip zero flag

	subcc	%o1,%g1,%o1				! conditional --$cnt
	bnz	.Loop2
	add	%o0,%g1,%o0				! conditional ++$out

.Ldone2:
	srl	%o1,2,%o1
	retl
	sub	%o3,%o1,%o0
.type	_sparcv9_vis1_instrument_bus2,#function
.size	_sparcv9_vis1_instrument_bus2,.-_sparcv9_vis1_instrument_bus2

.section	".init",#alloc,#execinstr
	call	OPENSSL_cpuid_setup
	nop
