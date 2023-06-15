#! /usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0

# This code is taken from CRYPTOGAMs[1] and is included here using the option
# in the license to distribute the code under the GPL. Therefore this program
# is free software; you can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2 as published by the Free Software
# Foundation.
#
# [1] https://www.openssl.org/~appro/cryptogams/

# Copyright (c) 2006-2017, CRYPTOGAMS by <appro@openssl.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#       * Redistributions of source code must retain copyright notices,
#         this list of conditions and the following disclaimer.
#
#       * Redistributions in binary form must reproduce the above
#         copyright notice, this list of conditions and the following
#         disclaimer in the documentation and/or other materials
#         provided with the distribution.
#
#       * Neither the name of the CRYPTOGAMS nor the names of its
#         copyright holder and contributors may be used to endorse or
#         promote products derived from this software without specific
#         prior written permission.
#
# ALTERNATIVELY, provided that this notice is retained in full, this
# product may be distributed under the terms of the GNU General Public
# License (GPL), in which case the provisions of the GPL apply INSTEAD OF
# those given above.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see https://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# This module implements support for AES instructions as per PowerISA
# specification version 2.07, first implemented by POWER8 processor.
# The module is endian-agnostic in sense that it supports both big-
# and little-endian cases. Data alignment in parallelizable modes is
# handled with VSX loads and stores, which implies MSR.VSX flag being
# set. It should also be noted that ISA specification doesn't prohibit
# alignment exceptions for these instructions on page boundaries.
# Initially alignment was handled in pure AltiVec/VMX way [when data
# is aligned programmatically, which in turn guarantees exception-
# free execution], but it turned to hamper performance when vcipher
# instructions are interleaved. It's reckoned that eventual
# misalignment penalties at page boundaries are in average lower
# than additional overhead in pure AltiVec approach.
#
# May 2016
#
# Add XTS subroutine, 9x on little- and 12x improvement on big-endian
# systems were measured.
#
######################################################################
# Current large-block performance in cycles per byte processed with
# 128-bit key (less is better).
#
#		CBC en-/decrypt	CTR	XTS
# POWER8[le]	3.96/0.72	0.74	1.1
# POWER8[be]	3.75/0.65	0.66	1.0

$flavour = shift;

if ($flavour =~ /64/) {
	$SIZE_T	=8;
	$LRSAVE	=2*$SIZE_T;
	$STU	="stdu";
	$POP	="ld";
	$PUSH	="std";
	$UCMP	="cmpld";
	$SHL	="sldi";
} elsif ($flavour =~ /32/) {
	$SIZE_T	=4;
	$LRSAVE	=$SIZE_T;
	$STU	="stwu";
	$POP	="lwz";
	$PUSH	="stw";
	$UCMP	="cmplw";
	$SHL	="slwi";
} else { die "nonsense $flavour"; }

$LITTLE_ENDIAN = ($flavour=~/le$/) ? $SIZE_T : 0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$FRAME=8*$SIZE_T;
$prefix="aes_p8";

$sp="r1";
$vrsave="r12";

#########################################################################
{{{	# Key setup procedures						#
my ($inp,$bits,$out,$ptr,$cnt,$rounds)=map("r$_",(3..8));
my ($zero,$in0,$in1,$key,$rcon,$mask,$tmp)=map("v$_",(0..6));
my ($stage,$outperm,$outmask,$outhead,$outtail)=map("v$_",(7..11));

$code.=<<___;
.machine	"any"

.text

.align	7
rcon:
.long	0x01000000, 0x01000000, 0x01000000, 0x01000000	?rev
.long	0x1b000000, 0x1b000000, 0x1b000000, 0x1b000000	?rev
.long	0x0d0e0f0c, 0x0d0e0f0c, 0x0d0e0f0c, 0x0d0e0f0c	?rev
.long	0,0,0,0						?asis
Lconsts:
	mflr	r0
	bcl	20,31,\$+4
	mflr	$ptr	 #vvvvv "distance between . and rcon
	addi	$ptr,$ptr,-0x48
	mtlr	r0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
.asciz	"AES for PowerISA 2.07, CRYPTOGAMS by <appro\@openssl.org>"

.globl	.${prefix}_set_encrypt_key
Lset_encrypt_key:
	mflr		r11
	$PUSH		r11,$LRSAVE($sp)

	li		$ptr,-1
	${UCMP}i	$inp,0
	beq-		Lenc_key_abort		# if ($inp==0) return -1;
	${UCMP}i	$out,0
	beq-		Lenc_key_abort		# if ($out==0) return -1;
	li		$ptr,-2
	cmpwi		$bits,128
	blt-		Lenc_key_abort
	cmpwi		$bits,256
	bgt-		Lenc_key_abort
	andi.		r0,$bits,0x3f
	bne-		Lenc_key_abort

	lis		r0,0xfff0
	mfspr		$vrsave,256
	mtspr		256,r0

	bl		Lconsts
	mtlr		r11

	neg		r9,$inp
	lvx		$in0,0,$inp
	addi		$inp,$inp,15		# 15 is not typo
	lvsr		$key,0,r9		# borrow $key
	li		r8,0x20
	cmpwi		$bits,192
	lvx		$in1,0,$inp
	le?vspltisb	$mask,0x0f		# borrow $mask
	lvx		$rcon,0,$ptr
	le?vxor		$key,$key,$mask		# adjust for byte swap
	lvx		$mask,r8,$ptr
	addi		$ptr,$ptr,0x10
	vperm		$in0,$in0,$in1,$key	# align [and byte swap in LE]
	li		$cnt,8
	vxor		$zero,$zero,$zero
	mtctr		$cnt

	?lvsr		$outperm,0,$out
	vspltisb	$outmask,-1
	lvx		$outhead,0,$out
	?vperm		$outmask,$zero,$outmask,$outperm

	blt		Loop128
	addi		$inp,$inp,8
	beq		L192
	addi		$inp,$inp,8
	b		L256

.align	4
Loop128:
	vperm		$key,$in0,$in0,$mask	# rotate-n-splat
	vsldoi		$tmp,$zero,$in0,12	# >>32
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	vcipherlast	$key,$key,$rcon
	 stvx		$stage,0,$out
	 addi		$out,$out,16

	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	 vadduwm	$rcon,$rcon,$rcon
	vxor		$in0,$in0,$key
	bdnz		Loop128

	lvx		$rcon,0,$ptr		# last two round keys

	vperm		$key,$in0,$in0,$mask	# rotate-n-splat
	vsldoi		$tmp,$zero,$in0,12	# >>32
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	vcipherlast	$key,$key,$rcon
	 stvx		$stage,0,$out
	 addi		$out,$out,16

	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	 vadduwm	$rcon,$rcon,$rcon
	vxor		$in0,$in0,$key

	vperm		$key,$in0,$in0,$mask	# rotate-n-splat
	vsldoi		$tmp,$zero,$in0,12	# >>32
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	vcipherlast	$key,$key,$rcon
	 stvx		$stage,0,$out
	 addi		$out,$out,16

	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	vxor		$in0,$in0,$key
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	 stvx		$stage,0,$out

	addi		$inp,$out,15		# 15 is not typo
	addi		$out,$out,0x50

	li		$rounds,10
	b		Ldone

.align	4
L192:
	lvx		$tmp,0,$inp
	li		$cnt,4
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	 stvx		$stage,0,$out
	 addi		$out,$out,16
	vperm		$in1,$in1,$tmp,$key	# align [and byte swap in LE]
	vspltisb	$key,8			# borrow $key
	mtctr		$cnt
	vsububm		$mask,$mask,$key	# adjust the mask

Loop192:
	vperm		$key,$in1,$in1,$mask	# roate-n-splat
	vsldoi		$tmp,$zero,$in0,12	# >>32
	vcipherlast	$key,$key,$rcon

	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp

	 vsldoi		$stage,$zero,$in1,8
	vspltw		$tmp,$in0,3
	vxor		$tmp,$tmp,$in1
	vsldoi		$in1,$zero,$in1,12	# >>32
	 vadduwm	$rcon,$rcon,$rcon
	vxor		$in1,$in1,$tmp
	vxor		$in0,$in0,$key
	vxor		$in1,$in1,$key
	 vsldoi		$stage,$stage,$in0,8

	vperm		$key,$in1,$in1,$mask	# rotate-n-splat
	vsldoi		$tmp,$zero,$in0,12	# >>32
	 vperm		$outtail,$stage,$stage,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	vcipherlast	$key,$key,$rcon
	 stvx		$stage,0,$out
	 addi		$out,$out,16

	 vsldoi		$stage,$in0,$in1,8
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	 vperm		$outtail,$stage,$stage,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	 stvx		$stage,0,$out
	 addi		$out,$out,16

	vspltw		$tmp,$in0,3
	vxor		$tmp,$tmp,$in1
	vsldoi		$in1,$zero,$in1,12	# >>32
	 vadduwm	$rcon,$rcon,$rcon
	vxor		$in1,$in1,$tmp
	vxor		$in0,$in0,$key
	vxor		$in1,$in1,$key
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	 stvx		$stage,0,$out
	 addi		$inp,$out,15		# 15 is not typo
	 addi		$out,$out,16
	bdnz		Loop192

	li		$rounds,12
	addi		$out,$out,0x20
	b		Ldone

.align	4
L256:
	lvx		$tmp,0,$inp
	li		$cnt,7
	li		$rounds,14
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	 stvx		$stage,0,$out
	 addi		$out,$out,16
	vperm		$in1,$in1,$tmp,$key	# align [and byte swap in LE]
	mtctr		$cnt

Loop256:
	vperm		$key,$in1,$in1,$mask	# rotate-n-splat
	vsldoi		$tmp,$zero,$in0,12	# >>32
	 vperm		$outtail,$in1,$in1,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	vcipherlast	$key,$key,$rcon
	 stvx		$stage,0,$out
	 addi		$out,$out,16

	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in0,$in0,$tmp
	 vadduwm	$rcon,$rcon,$rcon
	vxor		$in0,$in0,$key
	 vperm		$outtail,$in0,$in0,$outperm	# rotate
	 vsel		$stage,$outhead,$outtail,$outmask
	 vmr		$outhead,$outtail
	 stvx		$stage,0,$out
	 addi		$inp,$out,15		# 15 is not typo
	 addi		$out,$out,16
	bdz		Ldone

	vspltw		$key,$in0,3		# just splat
	vsldoi		$tmp,$zero,$in1,12	# >>32
	vsbox		$key,$key

	vxor		$in1,$in1,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in1,$in1,$tmp
	vsldoi		$tmp,$zero,$tmp,12	# >>32
	vxor		$in1,$in1,$tmp

	vxor		$in1,$in1,$key
	b		Loop256

.align	4
Ldone:
	lvx		$in1,0,$inp		# redundant in aligned case
	vsel		$in1,$outhead,$in1,$outmask
	stvx		$in1,0,$inp
	li		$ptr,0
	mtspr		256,$vrsave
	stw		$rounds,0($out)

Lenc_key_abort:
	mr		r3,$ptr
	blr
	.long		0
	.byte		0,12,0x14,1,0,0,3,0
	.long		0
.size	.${prefix}_set_encrypt_key,.-.${prefix}_set_encrypt_key

.globl	.${prefix}_set_decrypt_key
	$STU		$sp,-$FRAME($sp)
	mflr		r10
	$PUSH		r10,$FRAME+$LRSAVE($sp)
	bl		Lset_encrypt_key
	mtlr		r10

	cmpwi		r3,0
	bne-		Ldec_key_abort

	slwi		$cnt,$rounds,4
	subi		$inp,$out,240		# first round key
	srwi		$rounds,$rounds,1
	add		$out,$inp,$cnt		# last round key
	mtctr		$rounds

Ldeckey:
	lwz		r0, 0($inp)
	lwz		r6, 4($inp)
	lwz		r7, 8($inp)
	lwz		r8, 12($inp)
	addi		$inp,$inp,16
	lwz		r9, 0($out)
	lwz		r10,4($out)
	lwz		r11,8($out)
	lwz		r12,12($out)
	stw		r0, 0($out)
	stw		r6, 4($out)
	stw		r7, 8($out)
	stw		r8, 12($out)
	subi		$out,$out,16
	stw		r9, -16($inp)
	stw		r10,-12($inp)
	stw		r11,-8($inp)
	stw		r12,-4($inp)
	bdnz		Ldeckey

	xor		r3,r3,r3		# return value
Ldec_key_abort:
	addi		$sp,$sp,$FRAME
	blr
	.long		0
	.byte		0,12,4,1,0x80,0,3,0
	.long		0
.size	.${prefix}_set_decrypt_key,.-.${prefix}_set_decrypt_key
___
}}}
#########################################################################
{{{	# Single block en- and decrypt procedures			#
sub gen_block () {
my $dir = shift;
my $n   = $dir eq "de" ? "n" : "";
my ($inp,$out,$key,$rounds,$idx)=map("r$_",(3..7));

$code.=<<___;
.globl	.${prefix}_${dir}crypt
	lwz		$rounds,240($key)
	lis		r0,0xfc00
	mfspr		$vrsave,256
	li		$idx,15			# 15 is not typo
	mtspr		256,r0

	lvx		v0,0,$inp
	neg		r11,$out
	lvx		v1,$idx,$inp
	lvsl		v2,0,$inp		# inpperm
	le?vspltisb	v4,0x0f
	?lvsl		v3,0,r11		# outperm
	le?vxor		v2,v2,v4
	li		$idx,16
	vperm		v0,v0,v1,v2		# align [and byte swap in LE]
	lvx		v1,0,$key
	?lvsl		v5,0,$key		# keyperm
	srwi		$rounds,$rounds,1
	lvx		v2,$idx,$key
	addi		$idx,$idx,16
	subi		$rounds,$rounds,1
	?vperm		v1,v1,v2,v5		# align round key

	vxor		v0,v0,v1
	lvx		v1,$idx,$key
	addi		$idx,$idx,16
	mtctr		$rounds

Loop_${dir}c:
	?vperm		v2,v2,v1,v5
	v${n}cipher	v0,v0,v2
	lvx		v2,$idx,$key
	addi		$idx,$idx,16
	?vperm		v1,v1,v2,v5
	v${n}cipher	v0,v0,v1
	lvx		v1,$idx,$key
	addi		$idx,$idx,16
	bdnz		Loop_${dir}c

	?vperm		v2,v2,v1,v5
	v${n}cipher	v0,v0,v2
	lvx		v2,$idx,$key
	?vperm		v1,v1,v2,v5
	v${n}cipherlast	v0,v0,v1

	vspltisb	v2,-1
	vxor		v1,v1,v1
	li		$idx,15			# 15 is not typo
	?vperm		v2,v1,v2,v3		# outmask
	le?vxor		v3,v3,v4
	lvx		v1,0,$out		# outhead
	vperm		v0,v0,v0,v3		# rotate [and byte swap in LE]
	vsel		v1,v1,v0,v2
	lvx		v4,$idx,$out
	stvx		v1,0,$out
	vsel		v0,v0,v4,v2
	stvx		v0,$idx,$out

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,3,0
	.long		0
.size	.${prefix}_${dir}crypt,.-.${prefix}_${dir}crypt
___
}
&gen_block("en");
&gen_block("de");
}}}

my $consts=1;
foreach(split("\n",$code)) {
        s/\`([^\`]*)\`/eval($1)/geo;

	# constants table endian-specific conversion
	if ($consts && m/\.(long|byte)\s+(.+)\s+(\?[a-z]*)$/o) {
	    my $conv=$3;
	    my @bytes=();

	    # convert to endian-agnostic format
	    if ($1 eq "long") {
	      foreach (split(/,\s*/,$2)) {
		my $l = /^0/?oct:int;
		push @bytes,($l>>24)&0xff,($l>>16)&0xff,($l>>8)&0xff,$l&0xff;
	      }
	    } else {
		@bytes = map(/^0/?oct:int,split(/,\s*/,$2));
	    }

	    # little-endian conversion
	    if ($flavour =~ /le$/o) {
		SWITCH: for($conv)  {
		    /\?inv/ && do   { @bytes=map($_^0xf,@bytes); last; };
		    /\?rev/ && do   { @bytes=reverse(@bytes);    last; };
		}
	    }

	    #emit
	    print ".byte\t",join(',',map (sprintf("0x%02x",$_),@bytes)),"\n";
	    next;
	}
	$consts=0 if (m/Lconsts:/o);	# end of table

	# instructions prefixed with '?' are endian-specific and need
	# to be adjusted accordingly...
	if ($flavour =~ /le$/o) {	# little-endian
	    s/le\?//o		or
	    s/be\?/#be#/o	or
	    s/\?lvsr/lvsl/o	or
	    s/\?lvsl/lvsr/o	or
	    s/\?(vperm\s+v[0-9]+,\s*)(v[0-9]+,\s*)(v[0-9]+,\s*)(v[0-9]+)/$1$3$2$4/o or
	    s/\?(vsldoi\s+v[0-9]+,\s*)(v[0-9]+,)\s*(v[0-9]+,\s*)([0-9]+)/$1$3$2 16-$4/o or
	    s/\?(vspltw\s+v[0-9]+,\s*)(v[0-9]+,)\s*([0-9])/$1$2 3-$3/o;
	} else {			# big-endian
	    s/le\?/#le#/o	or
	    s/be\?//o		or
	    s/\?([a-z]+)/$1/o;
	}

        print $_,"\n";
}

close STDOUT;
