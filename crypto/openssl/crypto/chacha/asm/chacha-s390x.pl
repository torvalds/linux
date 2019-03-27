#! /usr/bin/env perl
# Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
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
#
# December 2015
#
# ChaCha20 for s390x.
#
# 3 times faster than compiler-generated code.

$flavour = shift;

if ($flavour =~ /3[12]/) {
	$SIZE_T=4;
	$g="";
} else {
	$SIZE_T=8;
	$g="g";
}

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

sub AUTOLOAD()		# thunk [simplified] x86-style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
    $code .= "\t$opcode\t".join(',',@_)."\n";
}

my $sp="%r15";

my $stdframe=16*$SIZE_T+4*8;
my $frame=$stdframe+4*20;

my ($out,$inp,$len,$key,$counter)=map("%r$_",(2..6));

my @x=map("%r$_",(0..7,"x","x","x","x",(10..13)));
my @t=map("%r$_",(8,9));

sub ROUND {
my ($a0,$b0,$c0,$d0)=@_;
my ($a1,$b1,$c1,$d1)=map(($_&~3)+(($_+1)&3),($a0,$b0,$c0,$d0));
my ($a2,$b2,$c2,$d2)=map(($_&~3)+(($_+1)&3),($a1,$b1,$c1,$d1));
my ($a3,$b3,$c3,$d3)=map(($_&~3)+(($_+1)&3),($a2,$b2,$c2,$d2));
my ($xc,$xc_)=map("\"$_\"",@t);
my @x=map("\"$_\"",@x);

	# Consider order in which variables are addressed by their
	# index:
	#
	#	a   b   c   d
	#
	#	0   4   8  12 < even round
	#	1   5   9  13
	#	2   6  10  14
	#	3   7  11  15
	#	0   5  10  15 < odd round
	#	1   6  11  12
	#	2   7   8  13
	#	3   4   9  14
	#
	# 'a', 'b' and 'd's are permanently allocated in registers,
	# @x[0..7,12..15], while 'c's are maintained in memory. If
	# you observe 'c' column, you'll notice that pair of 'c's is
	# invariant between rounds. This means that we have to reload
	# them once per round, in the middle. This is why you'll see
	# 'c' stores and loads in the middle, but none in the beginning
	# or end.

	(
	"&alr	(@x[$a0],@x[$b0])",	# Q1
	 "&alr	(@x[$a1],@x[$b1])",	# Q2
	"&xr	(@x[$d0],@x[$a0])",
	 "&xr	(@x[$d1],@x[$a1])",
	"&rll	(@x[$d0],@x[$d0],16)",
	 "&rll	(@x[$d1],@x[$d1],16)",

	"&alr	($xc,@x[$d0])",
	 "&alr	($xc_,@x[$d1])",
	"&xr	(@x[$b0],$xc)",
	 "&xr	(@x[$b1],$xc_)",
	"&rll	(@x[$b0],@x[$b0],12)",
	 "&rll	(@x[$b1],@x[$b1],12)",

	"&alr	(@x[$a0],@x[$b0])",
	 "&alr	(@x[$a1],@x[$b1])",
	"&xr	(@x[$d0],@x[$a0])",
	 "&xr	(@x[$d1],@x[$a1])",
	"&rll	(@x[$d0],@x[$d0],8)",
	 "&rll	(@x[$d1],@x[$d1],8)",

	"&alr	($xc,@x[$d0])",
	 "&alr	($xc_,@x[$d1])",
	"&xr	(@x[$b0],$xc)",
	 "&xr	(@x[$b1],$xc_)",
	"&rll	(@x[$b0],@x[$b0],7)",
	 "&rll	(@x[$b1],@x[$b1],7)",

	"&stm	($xc,$xc_,'$stdframe+4*8+4*$c0($sp)')",	# reload pair of 'c's
	"&lm	($xc,$xc_,'$stdframe+4*8+4*$c2($sp)')",

	"&alr	(@x[$a2],@x[$b2])",	# Q3
	 "&alr	(@x[$a3],@x[$b3])",	# Q4
	"&xr	(@x[$d2],@x[$a2])",
	 "&xr	(@x[$d3],@x[$a3])",
	"&rll	(@x[$d2],@x[$d2],16)",
	 "&rll	(@x[$d3],@x[$d3],16)",

	"&alr	($xc,@x[$d2])",
	 "&alr	($xc_,@x[$d3])",
	"&xr	(@x[$b2],$xc)",
	 "&xr	(@x[$b3],$xc_)",
	"&rll	(@x[$b2],@x[$b2],12)",
	 "&rll	(@x[$b3],@x[$b3],12)",

	"&alr	(@x[$a2],@x[$b2])",
	 "&alr	(@x[$a3],@x[$b3])",
	"&xr	(@x[$d2],@x[$a2])",
	 "&xr	(@x[$d3],@x[$a3])",
	"&rll	(@x[$d2],@x[$d2],8)",
	 "&rll	(@x[$d3],@x[$d3],8)",

	"&alr	($xc,@x[$d2])",
	 "&alr	($xc_,@x[$d3])",
	"&xr	(@x[$b2],$xc)",
	 "&xr	(@x[$b3],$xc_)",
	"&rll	(@x[$b2],@x[$b2],7)",
	 "&rll	(@x[$b3],@x[$b3],7)"
	);
}

$code.=<<___;
.text

.globl	ChaCha20_ctr32
.type	ChaCha20_ctr32,\@function
.align	32
ChaCha20_ctr32:
	lt${g}r	$len,$len			# $len==0?
	bzr	%r14
	a${g}hi	$len,-64
	l${g}hi	%r1,-$frame
	stm${g}	%r6,%r15,`6*$SIZE_T`($sp)
	sl${g}r	$out,$inp			# difference
	la	$len,0($inp,$len)		# end of input minus 64
	larl	%r7,.Lsigma
	lgr	%r0,$sp
	la	$sp,0(%r1,$sp)
	st${g}	%r0,0($sp)

	lmg	%r8,%r11,0($key)		# load key
	lmg	%r12,%r13,0($counter)		# load counter
	lmg	%r6,%r7,0(%r7)			# load sigma constant

	la	%r14,0($inp)
	st${g}	$out,$frame+3*$SIZE_T($sp)
	st${g}	$len,$frame+4*$SIZE_T($sp)
	stmg	%r6,%r13,$stdframe($sp)		# copy key schedule to stack
	srlg	@x[12],%r12,32			# 32-bit counter value
	j	.Loop_outer

.align	16
.Loop_outer:
	lm	@x[0],@x[7],$stdframe+4*0($sp)		# load x[0]-x[7]
	lm	@t[0],@t[1],$stdframe+4*10($sp)		# load x[10]-x[11]
	lm	@x[13],@x[15],$stdframe+4*13($sp)	# load x[13]-x[15]
	stm	@t[0],@t[1],$stdframe+4*8+4*10($sp)	# offload x[10]-x[11]
	lm	@t[0],@t[1],$stdframe+4*8($sp)		# load x[8]-x[9]
	st	@x[12],$stdframe+4*12($sp)		# save counter
	st${g}	%r14,$frame+2*$SIZE_T($sp)		# save input pointer
	lhi	%r14,10
	j	.Loop

.align	4
.Loop:
___
	foreach (&ROUND(0, 4, 8,12)) { eval; }
	foreach (&ROUND(0, 5,10,15)) { eval; }
$code.=<<___;
	brct	%r14,.Loop

	l${g}	%r14,$frame+2*$SIZE_T($sp)		# pull input pointer
	stm	@t[0],@t[1],$stdframe+4*8+4*8($sp)	# offload x[8]-x[9]
	lm${g}	@t[0],@t[1],$frame+3*$SIZE_T($sp)

	al	@x[0],$stdframe+4*0($sp)	# accumulate key schedule
	al	@x[1],$stdframe+4*1($sp)
	al	@x[2],$stdframe+4*2($sp)
	al	@x[3],$stdframe+4*3($sp)
	al	@x[4],$stdframe+4*4($sp)
	al	@x[5],$stdframe+4*5($sp)
	al	@x[6],$stdframe+4*6($sp)
	al	@x[7],$stdframe+4*7($sp)
	lrvr	@x[0],@x[0]
	lrvr	@x[1],@x[1]
	lrvr	@x[2],@x[2]
	lrvr	@x[3],@x[3]
	lrvr	@x[4],@x[4]
	lrvr	@x[5],@x[5]
	lrvr	@x[6],@x[6]
	lrvr	@x[7],@x[7]
	al	@x[12],$stdframe+4*12($sp)
	al	@x[13],$stdframe+4*13($sp)
	al	@x[14],$stdframe+4*14($sp)
	al	@x[15],$stdframe+4*15($sp)
	lrvr	@x[12],@x[12]
	lrvr	@x[13],@x[13]
	lrvr	@x[14],@x[14]
	lrvr	@x[15],@x[15]

	la	@t[0],0(@t[0],%r14)		# reconstruct output pointer
	cl${g}r	%r14,@t[1]
	jh	.Ltail

	x	@x[0],4*0(%r14)			# xor with input
	x	@x[1],4*1(%r14)
	st	@x[0],4*0(@t[0])		# store output
	x	@x[2],4*2(%r14)
	st	@x[1],4*1(@t[0])
	x	@x[3],4*3(%r14)
	st	@x[2],4*2(@t[0])
	x	@x[4],4*4(%r14)
	st	@x[3],4*3(@t[0])
	 lm	@x[0],@x[3],$stdframe+4*8+4*8($sp)	# load x[8]-x[11]
	x	@x[5],4*5(%r14)
	st	@x[4],4*4(@t[0])
	x	@x[6],4*6(%r14)
	 al	@x[0],$stdframe+4*8($sp)
	st	@x[5],4*5(@t[0])
	x	@x[7],4*7(%r14)
	 al	@x[1],$stdframe+4*9($sp)
	st	@x[6],4*6(@t[0])
	x	@x[12],4*12(%r14)
	 al	@x[2],$stdframe+4*10($sp)
	st	@x[7],4*7(@t[0])
	x	@x[13],4*13(%r14)
	 al	@x[3],$stdframe+4*11($sp)
	st	@x[12],4*12(@t[0])
	x	@x[14],4*14(%r14)
	st	@x[13],4*13(@t[0])
	x	@x[15],4*15(%r14)
	st	@x[14],4*14(@t[0])
	 lrvr	@x[0],@x[0]
	st	@x[15],4*15(@t[0])
	 lrvr	@x[1],@x[1]
	 lrvr	@x[2],@x[2]
	 lrvr	@x[3],@x[3]
	lhi	@x[12],1
	 x	@x[0],4*8(%r14)
	al	@x[12],$stdframe+4*12($sp)	# increment counter
	 x	@x[1],4*9(%r14)
	 st	@x[0],4*8(@t[0])
	 x	@x[2],4*10(%r14)
	 st	@x[1],4*9(@t[0])
	 x	@x[3],4*11(%r14)
	 st	@x[2],4*10(@t[0])
	 st	@x[3],4*11(@t[0])

	cl${g}r	%r14,@t[1]			# done yet?
	la	%r14,64(%r14)
	jl	.Loop_outer

.Ldone:
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r2,%r2
	xgr	%r3,%r3
	stmg	%r0,%r3,$stdframe+4*4($sp)	# wipe key copy
	stmg	%r0,%r3,$stdframe+4*12($sp)

	lm${g}	%r6,%r15,`$frame+6*$SIZE_T`($sp)
	br	%r14

.align	16
.Ltail:
	la	@t[1],64($t[1])
	stm	@x[0],@x[7],$stdframe+4*0($sp)
	sl${g}r	@t[1],%r14
	lm	@x[0],@x[3],$stdframe+4*8+4*8($sp)
	l${g}hi	@x[6],0
	stm	@x[12],@x[15],$stdframe+4*12($sp)
	al	@x[0],$stdframe+4*8($sp)
	al	@x[1],$stdframe+4*9($sp)
	al	@x[2],$stdframe+4*10($sp)
	al	@x[3],$stdframe+4*11($sp)
	lrvr	@x[0],@x[0]
	lrvr	@x[1],@x[1]
	lrvr	@x[2],@x[2]
	lrvr	@x[3],@x[3]
	stm	@x[0],@x[3],$stdframe+4*8($sp)

.Loop_tail:
	llgc	@x[4],0(@x[6],%r14)
	llgc	@x[5],$stdframe(@x[6],$sp)
	xr	@x[5],@x[4]
	stc	@x[5],0(@x[6],@t[0])
	la	@x[6],1(@x[6])
	brct	@t[1],.Loop_tail

	j	.Ldone
.size	ChaCha20_ctr32,.-ChaCha20_ctr32

.align	32
.Lsigma:
.long	0x61707865,0x3320646e,0x79622d32,0x6b206574	# endian-neutral
.asciz	"ChaCha20 for s390x, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	print $_,"\n";
}
close STDOUT;
