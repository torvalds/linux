#! /usr/bin/env perl
# Copyright 2007-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# sha1_block for Thumb.
#
# January 2007.
#
# The code does not present direct interest to OpenSSL, because of low
# performance. Its purpose is to establish _size_ benchmark. Pretty
# useless one I must say, because 30% or 88 bytes larger ARMv4 code
# [available on demand] is almost _twice_ as fast. It should also be
# noted that in-lining of .Lcommon and .Lrotate improves performance
# by over 40%, while code increases by only 10% or 32 bytes. But once
# again, the goal was to establish _size_ benchmark, not performance.

$output=shift;
open STDOUT,">$output";

$inline=0;
#$cheat_on_binutils=1;

$t0="r0";
$t1="r1";
$t2="r2";
$a="r3";
$b="r4";
$c="r5";
$d="r6";
$e="r7";
$K="r8";	# "upper" registers can be used in add/sub and mov insns
$ctx="r9";
$inp="r10";
$len="r11";
$Xi="r12";

sub common {
<<___;
	sub	$t0,#4
	ldr	$t1,[$t0]
	add	$e,$K			@ E+=K_xx_xx
	lsl	$t2,$a,#5
	add	$t2,$e
	lsr	$e,$a,#27
	add	$t2,$e			@ E+=ROR(A,27)
	add	$t2,$t1			@ E+=X[i]
___
}
sub rotate {
<<___;
	mov	$e,$d			@ E=D
	mov	$d,$c			@ D=C
	lsl	$c,$b,#30
	lsr	$b,$b,#2
	orr	$c,$b			@ C=ROR(B,2)
	mov	$b,$a			@ B=A
	add	$a,$t2,$t1		@ A=E+F_xx_xx(B,C,D)
___
}

sub BODY_00_19 {
$code.=$inline?&common():"\tbl	.Lcommon\n";
$code.=<<___;
	mov	$t1,$c
	eor	$t1,$d
	and	$t1,$b
	eor	$t1,$d			@ F_00_19(B,C,D)
___
$code.=$inline?&rotate():"\tbl	.Lrotate\n";
}

sub BODY_20_39 {
$code.=$inline?&common():"\tbl	.Lcommon\n";
$code.=<<___;
	mov	$t1,$b
	eor	$t1,$c
	eor	$t1,$d			@ F_20_39(B,C,D)
___
$code.=$inline?&rotate():"\tbl	.Lrotate\n";
}

sub BODY_40_59 {
$code.=$inline?&common():"\tbl	.Lcommon\n";
$code.=<<___;
	mov	$t1,$b
	and	$t1,$c
	mov	$e,$b
	orr	$e,$c
	and	$e,$d
	orr	$t1,$e			@ F_40_59(B,C,D)
___
$code.=$inline?&rotate():"\tbl	.Lrotate\n";
}

$code=<<___;
.text
.code	16

.global	sha1_block_data_order
.type	sha1_block_data_order,%function

.align	2
sha1_block_data_order:
___
if ($cheat_on_binutils) {
$code.=<<___;
.code	32
	add	r3,pc,#1
	bx	r3			@ switch to Thumb ISA
.code	16
___
}
$code.=<<___;
	push	{r4-r7}
	mov	r3,r8
	mov	r4,r9
	mov	r5,r10
	mov	r6,r11
	mov	r7,r12
	push	{r3-r7,lr}
	lsl	r2,#6
	mov	$ctx,r0			@ save context
	mov	$inp,r1			@ save inp
	mov	$len,r2			@ save len
	add	$len,$inp		@ $len to point at inp end

.Lloop:
	mov	$Xi,sp
	mov	$t2,sp
	sub	$t2,#16*4		@ [3]
.LXload:
	ldrb	$a,[$t1,#0]		@ $t1 is r1 and holds inp
	ldrb	$b,[$t1,#1]
	ldrb	$c,[$t1,#2]
	ldrb	$d,[$t1,#3]
	lsl	$a,#24
	lsl	$b,#16
	lsl	$c,#8
	orr	$a,$b
	orr	$a,$c
	orr	$a,$d
	add	$t1,#4
	push	{$a}
	cmp	sp,$t2
	bne	.LXload			@ [+14*16]

	mov	$inp,$t1		@ update $inp
	sub	$t2,#32*4
	sub	$t2,#32*4
	mov	$e,#31			@ [+4]
.LXupdate:
	ldr	$a,[sp,#15*4]
	ldr	$b,[sp,#13*4]
	ldr	$c,[sp,#7*4]
	ldr	$d,[sp,#2*4]
	eor	$a,$b
	eor	$a,$c
	eor	$a,$d
	ror	$a,$e
	push	{$a}
	cmp	sp,$t2
	bne	.LXupdate		@ [+(11+1)*64]

	ldmia	$t0!,{$a,$b,$c,$d,$e}	@ $t0 is r0 and holds ctx
	mov	$t0,$Xi

	ldr	$t2,.LK_00_19
	mov	$t1,$t0
	sub	$t1,#20*4
	mov	$Xi,$t1
	mov	$K,$t2			@ [+7+4]
.L_00_19:
___
	&BODY_00_19();
$code.=<<___;
	cmp	$Xi,$t0
	bne	.L_00_19		@ [+(2+9+4+2+8+2)*20]

	ldr	$t2,.LK_20_39
	mov	$t1,$t0
	sub	$t1,#20*4
	mov	$Xi,$t1
	mov	$K,$t2			@ [+5]
.L_20_39_or_60_79:
___
	&BODY_20_39();
$code.=<<___;
	cmp	$Xi,$t0
	bne	.L_20_39_or_60_79	@ [+(2+9+3+2+8+2)*20*2]
	cmp	sp,$t0
	beq	.Ldone			@ [+2]

	ldr	$t2,.LK_40_59
	mov	$t1,$t0
	sub	$t1,#20*4
	mov	$Xi,$t1
	mov	$K,$t2			@ [+5]
.L_40_59:
___
	&BODY_40_59();
$code.=<<___;
	cmp	$Xi,$t0
	bne	.L_40_59		@ [+(2+9+6+2+8+2)*20]

	ldr	$t2,.LK_60_79
	mov	$Xi,sp
	mov	$K,$t2
	b	.L_20_39_or_60_79	@ [+4]
.Ldone:
	mov	$t0,$ctx
	ldr	$t1,[$t0,#0]
	ldr	$t2,[$t0,#4]
	add	$a,$t1
	ldr	$t1,[$t0,#8]
	add	$b,$t2
	ldr	$t2,[$t0,#12]
	add	$c,$t1
	ldr	$t1,[$t0,#16]
	add	$d,$t2
	add	$e,$t1
	stmia	$t0!,{$a,$b,$c,$d,$e}	@ [+20]

	add	sp,#80*4		@ deallocate stack frame
	mov	$t0,$ctx		@ restore ctx
	mov	$t1,$inp		@ restore inp
	cmp	$t1,$len
	beq	.Lexit
	b	.Lloop			@ [+6] total 3212 cycles
.Lexit:
	pop	{r2-r7}
	mov	r8,r2
	mov	r9,r3
	mov	r10,r4
	mov	r11,r5
	mov	r12,r6
	mov	lr,r7
	pop	{r4-r7}
	bx	lr
.align	2
___
$code.=".Lcommon:\n".&common()."\tmov	pc,lr\n" if (!$inline);
$code.=".Lrotate:\n".&rotate()."\tmov	pc,lr\n" if (!$inline);
$code.=<<___;
.align	2
.LK_00_19:	.word	0x5a827999
.LK_20_39:	.word	0x6ed9eba1
.LK_40_59:	.word	0x8f1bbcdc
.LK_60_79:	.word	0xca62c1d6
.size	sha1_block_data_order,.-sha1_block_data_order
.asciz	"SHA1 block transform for Thumb, CRYPTOGAMS by <appro\@openssl.org>"
___

print $code;
close STDOUT; # enforce flush
