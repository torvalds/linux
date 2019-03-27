#! /usr/bin/env perl
# Copyright 2011-2019 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


######################################################################
## Constant-time SSSE3 AES core implementation.
## version 0.1
##
## By Mike Hamburg (Stanford University), 2009
## Public domain.
##
## For details see http://shiftleft.org/papers/vector_aes/ and
## http://crypto.stanford.edu/vpaes/.

######################################################################
# September 2011.
#
# Interface to OpenSSL as "almost" drop-in replacement for
# aes-x86_64.pl. "Almost" refers to the fact that AES_cbc_encrypt
# doesn't handle partial vectors (doesn't have to if called from
# EVP only). "Drop-in" implies that this module doesn't share key
# schedule structure with the original nor does it make assumption
# about its alignment...
#
# Performance summary. aes-x86_64.pl column lists large-block CBC
# encrypt/decrypt/with-hyper-threading-off(*) results in cycles per
# byte processed with 128-bit key, and vpaes-x86_64.pl column -
# [also large-block CBC] encrypt/decrypt.
#
#		aes-x86_64.pl		vpaes-x86_64.pl
#
# Core 2(**)	29.6/41.1/14.3		21.9/25.2(***)
# Nehalem	29.6/40.3/14.6		10.0/11.8
# Atom		57.3/74.2/32.1		60.9/77.2(***)
# Silvermont	52.7/64.0/19.5		48.8/60.8(***)
# Goldmont	38.9/49.0/17.8		10.6/12.6
#
# (*)	"Hyper-threading" in the context refers rather to cache shared
#	among multiple cores, than to specifically Intel HTT. As vast
#	majority of contemporary cores share cache, slower code path
#	is common place. In other words "with-hyper-threading-off"
#	results are presented mostly for reference purposes.
#
# (**)	"Core 2" refers to initial 65nm design, a.k.a. Conroe.
#
# (***)	Less impressive improvement on Core 2 and Atom is due to slow
#	pshufb,	yet it's respectable +36%/62% improvement on Core 2
#	(as implied, over "hyper-threading-safe" code path).
#
#						<appro@openssl.org>

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$PREFIX="vpaes";

$code.=<<___;
.text

##
##  _aes_encrypt_core
##
##  AES-encrypt %xmm0.
##
##  Inputs:
##     %xmm0 = input
##     %xmm9-%xmm15 as in _vpaes_preheat
##    (%rdx) = scheduled keys
##
##  Output in %xmm0
##  Clobbers  %xmm1-%xmm5, %r9, %r10, %r11, %rax
##  Preserves %xmm6 - %xmm8 so you get some local vectors
##
##
.type	_vpaes_encrypt_core,\@abi-omnipotent
.align 16
_vpaes_encrypt_core:
.cfi_startproc
	mov	%rdx,	%r9
	mov	\$16,	%r11
	mov	240(%rdx),%eax
	movdqa	%xmm9,	%xmm1
	movdqa	.Lk_ipt(%rip), %xmm2	# iptlo
	pandn	%xmm0,	%xmm1
	movdqu	(%r9),	%xmm5		# round0 key
	psrld	\$4,	%xmm1
	pand	%xmm9,	%xmm0
	pshufb	%xmm0,	%xmm2
	movdqa	.Lk_ipt+16(%rip), %xmm0	# ipthi
	pshufb	%xmm1,	%xmm0
	pxor	%xmm5,	%xmm2
	add	\$16,	%r9
	pxor	%xmm2,	%xmm0
	lea	.Lk_mc_backward(%rip),%r10
	jmp	.Lenc_entry

.align 16
.Lenc_loop:
	# middle of middle round
	movdqa  %xmm13,	%xmm4	# 4 : sb1u
	movdqa  %xmm12,	%xmm0	# 0 : sb1t
	pshufb  %xmm2,	%xmm4	# 4 = sb1u
	pshufb  %xmm3,	%xmm0	# 0 = sb1t
	pxor	%xmm5,	%xmm4	# 4 = sb1u + k
	movdqa  %xmm15,	%xmm5	# 4 : sb2u
	pxor	%xmm4,	%xmm0	# 0 = A
	movdqa	-0x40(%r11,%r10), %xmm1		# .Lk_mc_forward[]
	pshufb	%xmm2,	%xmm5	# 4 = sb2u
	movdqa	(%r11,%r10), %xmm4		# .Lk_mc_backward[]
	movdqa	%xmm14, %xmm2	# 2 : sb2t
	pshufb	%xmm3,  %xmm2	# 2 = sb2t
	movdqa	%xmm0,  %xmm3	# 3 = A
	pxor	%xmm5,	%xmm2	# 2 = 2A
	pshufb  %xmm1,  %xmm0	# 0 = B
	add	\$16,	%r9	# next key
	pxor	%xmm2,  %xmm0	# 0 = 2A+B
	pshufb	%xmm4,	%xmm3	# 3 = D
	add	\$16,	%r11	# next mc
	pxor	%xmm0,	%xmm3	# 3 = 2A+B+D
	pshufb  %xmm1,	%xmm0	# 0 = 2B+C
	and	\$0x30,	%r11	# ... mod 4
	sub	\$1,%rax	# nr--
	pxor	%xmm3,	%xmm0	# 0 = 2A+3B+C+D

.Lenc_entry:
	# top of round
	movdqa  %xmm9, 	%xmm1	# 1 : i
	movdqa	%xmm11, %xmm5	# 2 : a/k
	pandn	%xmm0, 	%xmm1	# 1 = i<<4
	psrld	\$4,   	%xmm1   # 1 = i
	pand	%xmm9, 	%xmm0   # 0 = k
	pshufb  %xmm0,  %xmm5	# 2 = a/k
	movdqa	%xmm10,	%xmm3  	# 3 : 1/i
	pxor	%xmm1,	%xmm0	# 0 = j
	pshufb  %xmm1, 	%xmm3  	# 3 = 1/i
	movdqa	%xmm10,	%xmm4  	# 4 : 1/j
	pxor	%xmm5, 	%xmm3  	# 3 = iak = 1/i + a/k
	pshufb	%xmm0, 	%xmm4  	# 4 = 1/j
	movdqa	%xmm10,	%xmm2  	# 2 : 1/iak
	pxor	%xmm5, 	%xmm4  	# 4 = jak = 1/j + a/k
	pshufb  %xmm3,	%xmm2  	# 2 = 1/iak
	movdqa	%xmm10, %xmm3   # 3 : 1/jak
	pxor	%xmm0, 	%xmm2  	# 2 = io
	pshufb  %xmm4,  %xmm3   # 3 = 1/jak
	movdqu	(%r9),	%xmm5
	pxor	%xmm1,  %xmm3   # 3 = jo
	jnz	.Lenc_loop

	# middle of last round
	movdqa	-0x60(%r10), %xmm4	# 3 : sbou	.Lk_sbo
	movdqa	-0x50(%r10), %xmm0	# 0 : sbot	.Lk_sbo+16
	pshufb  %xmm2,  %xmm4	# 4 = sbou
	pxor	%xmm5,  %xmm4	# 4 = sb1u + k
	pshufb  %xmm3,	%xmm0	# 0 = sb1t
	movdqa	0x40(%r11,%r10), %xmm1		# .Lk_sr[]
	pxor	%xmm4,	%xmm0	# 0 = A
	pshufb	%xmm1,	%xmm0
	ret
.cfi_endproc
.size	_vpaes_encrypt_core,.-_vpaes_encrypt_core

##
##  Decryption core
##
##  Same API as encryption core.
##
.type	_vpaes_decrypt_core,\@abi-omnipotent
.align	16
_vpaes_decrypt_core:
.cfi_startproc
	mov	%rdx,	%r9		# load key
	mov	240(%rdx),%eax
	movdqa	%xmm9,	%xmm1
	movdqa	.Lk_dipt(%rip), %xmm2	# iptlo
	pandn	%xmm0,	%xmm1
	mov	%rax,	%r11
	psrld	\$4,	%xmm1
	movdqu	(%r9),	%xmm5		# round0 key
	shl	\$4,	%r11
	pand	%xmm9,	%xmm0
	pshufb	%xmm0,	%xmm2
	movdqa	.Lk_dipt+16(%rip), %xmm0 # ipthi
	xor	\$0x30,	%r11
	lea	.Lk_dsbd(%rip),%r10
	pshufb	%xmm1,	%xmm0
	and	\$0x30,	%r11
	pxor	%xmm5,	%xmm2
	movdqa	.Lk_mc_forward+48(%rip), %xmm5
	pxor	%xmm2,	%xmm0
	add	\$16,	%r9
	add	%r10,	%r11
	jmp	.Ldec_entry

.align 16
.Ldec_loop:
##
##  Inverse mix columns
##
	movdqa  -0x20(%r10),%xmm4	# 4 : sb9u
	movdqa  -0x10(%r10),%xmm1	# 0 : sb9t
	pshufb	%xmm2,	%xmm4		# 4 = sb9u
	pshufb	%xmm3,	%xmm1		# 0 = sb9t
	pxor	%xmm4,	%xmm0
	movdqa  0x00(%r10),%xmm4	# 4 : sbdu
	pxor	%xmm1,	%xmm0		# 0 = ch
	movdqa  0x10(%r10),%xmm1	# 0 : sbdt

	pshufb	%xmm2,	%xmm4		# 4 = sbdu
	pshufb	%xmm5,	%xmm0		# MC ch
	pshufb	%xmm3,	%xmm1		# 0 = sbdt
	pxor	%xmm4,	%xmm0		# 4 = ch
	movdqa  0x20(%r10),%xmm4	# 4 : sbbu
	pxor	%xmm1,	%xmm0		# 0 = ch
	movdqa  0x30(%r10),%xmm1	# 0 : sbbt

	pshufb	%xmm2,	%xmm4		# 4 = sbbu
	pshufb	%xmm5,	%xmm0		# MC ch
	pshufb	%xmm3,	%xmm1		# 0 = sbbt
	pxor	%xmm4,	%xmm0		# 4 = ch
	movdqa  0x40(%r10),%xmm4	# 4 : sbeu
	pxor	%xmm1,	%xmm0		# 0 = ch
	movdqa  0x50(%r10),%xmm1	# 0 : sbet

	pshufb	%xmm2,	%xmm4		# 4 = sbeu
	pshufb	%xmm5,	%xmm0		# MC ch
	pshufb	%xmm3,	%xmm1		# 0 = sbet
	pxor	%xmm4,	%xmm0		# 4 = ch
	add	\$16, %r9		# next round key
	palignr	\$12,	%xmm5,	%xmm5
	pxor	%xmm1,	%xmm0		# 0 = ch
	sub	\$1,%rax		# nr--

.Ldec_entry:
	# top of round
	movdqa  %xmm9, 	%xmm1	# 1 : i
	pandn	%xmm0, 	%xmm1	# 1 = i<<4
	movdqa	%xmm11, %xmm2	# 2 : a/k
	psrld	\$4,    %xmm1	# 1 = i
	pand	%xmm9, 	%xmm0	# 0 = k
	pshufb  %xmm0,  %xmm2	# 2 = a/k
	movdqa	%xmm10,	%xmm3	# 3 : 1/i
	pxor	%xmm1,	%xmm0	# 0 = j
	pshufb  %xmm1, 	%xmm3	# 3 = 1/i
	movdqa	%xmm10,	%xmm4	# 4 : 1/j
	pxor	%xmm2, 	%xmm3	# 3 = iak = 1/i + a/k
	pshufb	%xmm0, 	%xmm4	# 4 = 1/j
	pxor	%xmm2, 	%xmm4	# 4 = jak = 1/j + a/k
	movdqa	%xmm10,	%xmm2	# 2 : 1/iak
	pshufb  %xmm3,	%xmm2	# 2 = 1/iak
	movdqa	%xmm10, %xmm3	# 3 : 1/jak
	pxor	%xmm0, 	%xmm2	# 2 = io
	pshufb  %xmm4,  %xmm3	# 3 = 1/jak
	movdqu	(%r9),	%xmm0
	pxor	%xmm1,  %xmm3	# 3 = jo
	jnz	.Ldec_loop

	# middle of last round
	movdqa	0x60(%r10), %xmm4	# 3 : sbou
	pshufb  %xmm2,  %xmm4	# 4 = sbou
	pxor	%xmm0,  %xmm4	# 4 = sb1u + k
	movdqa	0x70(%r10), %xmm0	# 0 : sbot
	movdqa	-0x160(%r11), %xmm2	# .Lk_sr-.Lk_dsbd=-0x160
	pshufb  %xmm3,	%xmm0	# 0 = sb1t
	pxor	%xmm4,	%xmm0	# 0 = A
	pshufb	%xmm2,	%xmm0
	ret
.cfi_endproc
.size	_vpaes_decrypt_core,.-_vpaes_decrypt_core

########################################################
##                                                    ##
##                  AES key schedule                  ##
##                                                    ##
########################################################
.type	_vpaes_schedule_core,\@abi-omnipotent
.align	16
_vpaes_schedule_core:
.cfi_startproc
	# rdi = key
	# rsi = size in bits
	# rdx = buffer
	# rcx = direction.  0=encrypt, 1=decrypt

	call	_vpaes_preheat		# load the tables
	movdqa	.Lk_rcon(%rip), %xmm8	# load rcon
	movdqu	(%rdi),	%xmm0		# load key (unaligned)

	# input transform
	movdqa	%xmm0,	%xmm3
	lea	.Lk_ipt(%rip), %r11
	call	_vpaes_schedule_transform
	movdqa	%xmm0,	%xmm7

	lea	.Lk_sr(%rip),%r10
	test	%rcx,	%rcx
	jnz	.Lschedule_am_decrypting

	# encrypting, output zeroth round key after transform
	movdqu	%xmm0,	(%rdx)
	jmp	.Lschedule_go

.Lschedule_am_decrypting:
	# decrypting, output zeroth round key after shiftrows
	movdqa	(%r8,%r10),%xmm1
	pshufb  %xmm1,	%xmm3
	movdqu	%xmm3,	(%rdx)
	xor	\$0x30, %r8

.Lschedule_go:
	cmp	\$192,	%esi
	ja	.Lschedule_256
	je	.Lschedule_192
	# 128: fall though

##
##  .schedule_128
##
##  128-bit specific part of key schedule.
##
##  This schedule is really simple, because all its parts
##  are accomplished by the subroutines.
##
.Lschedule_128:
	mov	\$10, %esi

.Loop_schedule_128:
	call 	_vpaes_schedule_round
	dec	%rsi
	jz 	.Lschedule_mangle_last
	call	_vpaes_schedule_mangle	# write output
	jmp 	.Loop_schedule_128

##
##  .aes_schedule_192
##
##  192-bit specific part of key schedule.
##
##  The main body of this schedule is the same as the 128-bit
##  schedule, but with more smearing.  The long, high side is
##  stored in %xmm7 as before, and the short, low side is in
##  the high bits of %xmm6.
##
##  This schedule is somewhat nastier, however, because each
##  round produces 192 bits of key material, or 1.5 round keys.
##  Therefore, on each cycle we do 2 rounds and produce 3 round
##  keys.
##
.align	16
.Lschedule_192:
	movdqu	8(%rdi),%xmm0		# load key part 2 (very unaligned)
	call	_vpaes_schedule_transform	# input transform
	movdqa	%xmm0,	%xmm6		# save short part
	pxor	%xmm4,	%xmm4		# clear 4
	movhlps	%xmm4,	%xmm6		# clobber low side with zeros
	mov	\$4,	%esi

.Loop_schedule_192:
	call	_vpaes_schedule_round
	palignr	\$8,%xmm6,%xmm0
	call	_vpaes_schedule_mangle	# save key n
	call	_vpaes_schedule_192_smear
	call	_vpaes_schedule_mangle	# save key n+1
	call	_vpaes_schedule_round
	dec	%rsi
	jz 	.Lschedule_mangle_last
	call	_vpaes_schedule_mangle	# save key n+2
	call	_vpaes_schedule_192_smear
	jmp	.Loop_schedule_192

##
##  .aes_schedule_256
##
##  256-bit specific part of key schedule.
##
##  The structure here is very similar to the 128-bit
##  schedule, but with an additional "low side" in
##  %xmm6.  The low side's rounds are the same as the
##  high side's, except no rcon and no rotation.
##
.align	16
.Lschedule_256:
	movdqu	16(%rdi),%xmm0		# load key part 2 (unaligned)
	call	_vpaes_schedule_transform	# input transform
	mov	\$7, %esi

.Loop_schedule_256:
	call	_vpaes_schedule_mangle	# output low result
	movdqa	%xmm0,	%xmm6		# save cur_lo in xmm6

	# high round
	call	_vpaes_schedule_round
	dec	%rsi
	jz 	.Lschedule_mangle_last
	call	_vpaes_schedule_mangle

	# low round. swap xmm7 and xmm6
	pshufd	\$0xFF,	%xmm0,	%xmm0
	movdqa	%xmm7,	%xmm5
	movdqa	%xmm6,	%xmm7
	call	_vpaes_schedule_low_round
	movdqa	%xmm5,	%xmm7

	jmp	.Loop_schedule_256


##
##  .aes_schedule_mangle_last
##
##  Mangler for last round of key schedule
##  Mangles %xmm0
##    when encrypting, outputs out(%xmm0) ^ 63
##    when decrypting, outputs unskew(%xmm0)
##
##  Always called right before return... jumps to cleanup and exits
##
.align	16
.Lschedule_mangle_last:
	# schedule last round key from xmm0
	lea	.Lk_deskew(%rip),%r11	# prepare to deskew
	test	%rcx, 	%rcx
	jnz	.Lschedule_mangle_last_dec

	# encrypting
	movdqa	(%r8,%r10),%xmm1
	pshufb	%xmm1,	%xmm0		# output permute
	lea	.Lk_opt(%rip),	%r11	# prepare to output transform
	add	\$32,	%rdx

.Lschedule_mangle_last_dec:
	add	\$-16,	%rdx
	pxor	.Lk_s63(%rip),	%xmm0
	call	_vpaes_schedule_transform # output transform
	movdqu	%xmm0,	(%rdx)		# save last key

	# cleanup
	pxor	%xmm0,  %xmm0
	pxor	%xmm1,  %xmm1
	pxor	%xmm2,  %xmm2
	pxor	%xmm3,  %xmm3
	pxor	%xmm4,  %xmm4
	pxor	%xmm5,  %xmm5
	pxor	%xmm6,  %xmm6
	pxor	%xmm7,  %xmm7
	ret
.cfi_endproc
.size	_vpaes_schedule_core,.-_vpaes_schedule_core

##
##  .aes_schedule_192_smear
##
##  Smear the short, low side in the 192-bit key schedule.
##
##  Inputs:
##    %xmm7: high side, b  a  x  y
##    %xmm6:  low side, d  c  0  0
##    %xmm13: 0
##
##  Outputs:
##    %xmm6: b+c+d  b+c  0  0
##    %xmm0: b+c+d  b+c  b  a
##
.type	_vpaes_schedule_192_smear,\@abi-omnipotent
.align	16
_vpaes_schedule_192_smear:
.cfi_startproc
	pshufd	\$0x80,	%xmm6,	%xmm1	# d c 0 0 -> c 0 0 0
	pshufd	\$0xFE,	%xmm7,	%xmm0	# b a _ _ -> b b b a
	pxor	%xmm1,	%xmm6		# -> c+d c 0 0
	pxor	%xmm1,	%xmm1
	pxor	%xmm0,	%xmm6		# -> b+c+d b+c b a
	movdqa	%xmm6,	%xmm0
	movhlps	%xmm1,	%xmm6		# clobber low side with zeros
	ret
.cfi_endproc
.size	_vpaes_schedule_192_smear,.-_vpaes_schedule_192_smear

##
##  .aes_schedule_round
##
##  Runs one main round of the key schedule on %xmm0, %xmm7
##
##  Specifically, runs subbytes on the high dword of %xmm0
##  then rotates it by one byte and xors into the low dword of
##  %xmm7.
##
##  Adds rcon from low byte of %xmm8, then rotates %xmm8 for
##  next rcon.
##
##  Smears the dwords of %xmm7 by xoring the low into the
##  second low, result into third, result into highest.
##
##  Returns results in %xmm7 = %xmm0.
##  Clobbers %xmm1-%xmm4, %r11.
##
.type	_vpaes_schedule_round,\@abi-omnipotent
.align	16
_vpaes_schedule_round:
.cfi_startproc
	# extract rcon from xmm8
	pxor	%xmm1,	%xmm1
	palignr	\$15,	%xmm8,	%xmm1
	palignr	\$15,	%xmm8,	%xmm8
	pxor	%xmm1,	%xmm7

	# rotate
	pshufd	\$0xFF,	%xmm0,	%xmm0
	palignr	\$1,	%xmm0,	%xmm0

	# fall through...

	# low round: same as high round, but no rotation and no rcon.
_vpaes_schedule_low_round:
	# smear xmm7
	movdqa	%xmm7,	%xmm1
	pslldq	\$4,	%xmm7
	pxor	%xmm1,	%xmm7
	movdqa	%xmm7,	%xmm1
	pslldq	\$8,	%xmm7
	pxor	%xmm1,	%xmm7
	pxor	.Lk_s63(%rip), %xmm7

	# subbytes
	movdqa  %xmm9, 	%xmm1
	pandn	%xmm0, 	%xmm1
	psrld	\$4,    %xmm1		# 1 = i
	pand	%xmm9, 	%xmm0		# 0 = k
	movdqa	%xmm11, %xmm2		# 2 : a/k
	pshufb  %xmm0,  %xmm2		# 2 = a/k
	pxor	%xmm1,	%xmm0		# 0 = j
	movdqa	%xmm10,	%xmm3		# 3 : 1/i
	pshufb  %xmm1, 	%xmm3		# 3 = 1/i
	pxor	%xmm2, 	%xmm3		# 3 = iak = 1/i + a/k
	movdqa	%xmm10,	%xmm4		# 4 : 1/j
	pshufb	%xmm0, 	%xmm4		# 4 = 1/j
	pxor	%xmm2, 	%xmm4		# 4 = jak = 1/j + a/k
	movdqa	%xmm10,	%xmm2		# 2 : 1/iak
	pshufb  %xmm3,	%xmm2		# 2 = 1/iak
	pxor	%xmm0, 	%xmm2		# 2 = io
	movdqa	%xmm10, %xmm3		# 3 : 1/jak
	pshufb  %xmm4,  %xmm3		# 3 = 1/jak
	pxor	%xmm1,  %xmm3		# 3 = jo
	movdqa	%xmm13, %xmm4		# 4 : sbou
	pshufb  %xmm2,  %xmm4		# 4 = sbou
	movdqa	%xmm12, %xmm0		# 0 : sbot
	pshufb  %xmm3,	%xmm0		# 0 = sb1t
	pxor	%xmm4, 	%xmm0		# 0 = sbox output

	# add in smeared stuff
	pxor	%xmm7,	%xmm0
	movdqa	%xmm0,	%xmm7
	ret
.cfi_endproc
.size	_vpaes_schedule_round,.-_vpaes_schedule_round

##
##  .aes_schedule_transform
##
##  Linear-transform %xmm0 according to tables at (%r11)
##
##  Requires that %xmm9 = 0x0F0F... as in preheat
##  Output in %xmm0
##  Clobbers %xmm1, %xmm2
##
.type	_vpaes_schedule_transform,\@abi-omnipotent
.align	16
_vpaes_schedule_transform:
.cfi_startproc
	movdqa	%xmm9,	%xmm1
	pandn	%xmm0,	%xmm1
	psrld	\$4,	%xmm1
	pand	%xmm9,	%xmm0
	movdqa	(%r11), %xmm2 	# lo
	pshufb	%xmm0,	%xmm2
	movdqa	16(%r11), %xmm0 # hi
	pshufb	%xmm1,	%xmm0
	pxor	%xmm2,	%xmm0
	ret
.cfi_endproc
.size	_vpaes_schedule_transform,.-_vpaes_schedule_transform

##
##  .aes_schedule_mangle
##
##  Mangle xmm0 from (basis-transformed) standard version
##  to our version.
##
##  On encrypt,
##    xor with 0x63
##    multiply by circulant 0,1,1,1
##    apply shiftrows transform
##
##  On decrypt,
##    xor with 0x63
##    multiply by "inverse mixcolumns" circulant E,B,D,9
##    deskew
##    apply shiftrows transform
##
##
##  Writes out to (%rdx), and increments or decrements it
##  Keeps track of round number mod 4 in %r8
##  Preserves xmm0
##  Clobbers xmm1-xmm5
##
.type	_vpaes_schedule_mangle,\@abi-omnipotent
.align	16
_vpaes_schedule_mangle:
.cfi_startproc
	movdqa	%xmm0,	%xmm4	# save xmm0 for later
	movdqa	.Lk_mc_forward(%rip),%xmm5
	test	%rcx, 	%rcx
	jnz	.Lschedule_mangle_dec

	# encrypting
	add	\$16,	%rdx
	pxor	.Lk_s63(%rip),%xmm4
	pshufb	%xmm5,	%xmm4
	movdqa	%xmm4,	%xmm3
	pshufb	%xmm5,	%xmm4
	pxor	%xmm4,	%xmm3
	pshufb	%xmm5,	%xmm4
	pxor	%xmm4,	%xmm3

	jmp	.Lschedule_mangle_both
.align	16
.Lschedule_mangle_dec:
	# inverse mix columns
	lea	.Lk_dksd(%rip),%r11
	movdqa	%xmm9,	%xmm1
	pandn	%xmm4,	%xmm1
	psrld	\$4,	%xmm1	# 1 = hi
	pand	%xmm9,	%xmm4	# 4 = lo

	movdqa	0x00(%r11), %xmm2
	pshufb	%xmm4,	%xmm2
	movdqa	0x10(%r11), %xmm3
	pshufb	%xmm1,	%xmm3
	pxor	%xmm2,	%xmm3
	pshufb	%xmm5,	%xmm3

	movdqa	0x20(%r11), %xmm2
	pshufb	%xmm4,	%xmm2
	pxor	%xmm3,	%xmm2
	movdqa	0x30(%r11), %xmm3
	pshufb	%xmm1,	%xmm3
	pxor	%xmm2,	%xmm3
	pshufb	%xmm5,	%xmm3

	movdqa	0x40(%r11), %xmm2
	pshufb	%xmm4,	%xmm2
	pxor	%xmm3,	%xmm2
	movdqa	0x50(%r11), %xmm3
	pshufb	%xmm1,	%xmm3
	pxor	%xmm2,	%xmm3
	pshufb	%xmm5,	%xmm3

	movdqa	0x60(%r11), %xmm2
	pshufb	%xmm4,	%xmm2
	pxor	%xmm3,	%xmm2
	movdqa	0x70(%r11), %xmm3
	pshufb	%xmm1,	%xmm3
	pxor	%xmm2,	%xmm3

	add	\$-16,	%rdx

.Lschedule_mangle_both:
	movdqa	(%r8,%r10),%xmm1
	pshufb	%xmm1,%xmm3
	add	\$-16,	%r8
	and	\$0x30,	%r8
	movdqu	%xmm3,	(%rdx)
	ret
.cfi_endproc
.size	_vpaes_schedule_mangle,.-_vpaes_schedule_mangle

#
# Interface to OpenSSL
#
.globl	${PREFIX}_set_encrypt_key
.type	${PREFIX}_set_encrypt_key,\@function,3
.align	16
${PREFIX}_set_encrypt_key:
.cfi_startproc
___
$code.=<<___ if ($win64);
	lea	-0xb8(%rsp),%rsp
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
	movaps	%xmm10,0x50(%rsp)
	movaps	%xmm11,0x60(%rsp)
	movaps	%xmm12,0x70(%rsp)
	movaps	%xmm13,0x80(%rsp)
	movaps	%xmm14,0x90(%rsp)
	movaps	%xmm15,0xa0(%rsp)
.Lenc_key_body:
___
$code.=<<___;
	mov	%esi,%eax
	shr	\$5,%eax
	add	\$5,%eax
	mov	%eax,240(%rdx)	# AES_KEY->rounds = nbits/32+5;

	mov	\$0,%ecx
	mov	\$0x30,%r8d
	call	_vpaes_schedule_core
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	0x20(%rsp),%xmm7
	movaps	0x30(%rsp),%xmm8
	movaps	0x40(%rsp),%xmm9
	movaps	0x50(%rsp),%xmm10
	movaps	0x60(%rsp),%xmm11
	movaps	0x70(%rsp),%xmm12
	movaps	0x80(%rsp),%xmm13
	movaps	0x90(%rsp),%xmm14
	movaps	0xa0(%rsp),%xmm15
	lea	0xb8(%rsp),%rsp
.Lenc_key_epilogue:
___
$code.=<<___;
	xor	%eax,%eax
	ret
.cfi_endproc
.size	${PREFIX}_set_encrypt_key,.-${PREFIX}_set_encrypt_key

.globl	${PREFIX}_set_decrypt_key
.type	${PREFIX}_set_decrypt_key,\@function,3
.align	16
${PREFIX}_set_decrypt_key:
.cfi_startproc
___
$code.=<<___ if ($win64);
	lea	-0xb8(%rsp),%rsp
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
	movaps	%xmm10,0x50(%rsp)
	movaps	%xmm11,0x60(%rsp)
	movaps	%xmm12,0x70(%rsp)
	movaps	%xmm13,0x80(%rsp)
	movaps	%xmm14,0x90(%rsp)
	movaps	%xmm15,0xa0(%rsp)
.Ldec_key_body:
___
$code.=<<___;
	mov	%esi,%eax
	shr	\$5,%eax
	add	\$5,%eax
	mov	%eax,240(%rdx)	# AES_KEY->rounds = nbits/32+5;
	shl	\$4,%eax
	lea	16(%rdx,%rax),%rdx

	mov	\$1,%ecx
	mov	%esi,%r8d
	shr	\$1,%r8d
	and	\$32,%r8d
	xor	\$32,%r8d	# nbits==192?0:32
	call	_vpaes_schedule_core
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	0x20(%rsp),%xmm7
	movaps	0x30(%rsp),%xmm8
	movaps	0x40(%rsp),%xmm9
	movaps	0x50(%rsp),%xmm10
	movaps	0x60(%rsp),%xmm11
	movaps	0x70(%rsp),%xmm12
	movaps	0x80(%rsp),%xmm13
	movaps	0x90(%rsp),%xmm14
	movaps	0xa0(%rsp),%xmm15
	lea	0xb8(%rsp),%rsp
.Ldec_key_epilogue:
___
$code.=<<___;
	xor	%eax,%eax
	ret
.cfi_endproc
.size	${PREFIX}_set_decrypt_key,.-${PREFIX}_set_decrypt_key

.globl	${PREFIX}_encrypt
.type	${PREFIX}_encrypt,\@function,3
.align	16
${PREFIX}_encrypt:
.cfi_startproc
___
$code.=<<___ if ($win64);
	lea	-0xb8(%rsp),%rsp
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
	movaps	%xmm10,0x50(%rsp)
	movaps	%xmm11,0x60(%rsp)
	movaps	%xmm12,0x70(%rsp)
	movaps	%xmm13,0x80(%rsp)
	movaps	%xmm14,0x90(%rsp)
	movaps	%xmm15,0xa0(%rsp)
.Lenc_body:
___
$code.=<<___;
	movdqu	(%rdi),%xmm0
	call	_vpaes_preheat
	call	_vpaes_encrypt_core
	movdqu	%xmm0,(%rsi)
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	0x20(%rsp),%xmm7
	movaps	0x30(%rsp),%xmm8
	movaps	0x40(%rsp),%xmm9
	movaps	0x50(%rsp),%xmm10
	movaps	0x60(%rsp),%xmm11
	movaps	0x70(%rsp),%xmm12
	movaps	0x80(%rsp),%xmm13
	movaps	0x90(%rsp),%xmm14
	movaps	0xa0(%rsp),%xmm15
	lea	0xb8(%rsp),%rsp
.Lenc_epilogue:
___
$code.=<<___;
	ret
.cfi_endproc
.size	${PREFIX}_encrypt,.-${PREFIX}_encrypt

.globl	${PREFIX}_decrypt
.type	${PREFIX}_decrypt,\@function,3
.align	16
${PREFIX}_decrypt:
.cfi_startproc
___
$code.=<<___ if ($win64);
	lea	-0xb8(%rsp),%rsp
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
	movaps	%xmm10,0x50(%rsp)
	movaps	%xmm11,0x60(%rsp)
	movaps	%xmm12,0x70(%rsp)
	movaps	%xmm13,0x80(%rsp)
	movaps	%xmm14,0x90(%rsp)
	movaps	%xmm15,0xa0(%rsp)
.Ldec_body:
___
$code.=<<___;
	movdqu	(%rdi),%xmm0
	call	_vpaes_preheat
	call	_vpaes_decrypt_core
	movdqu	%xmm0,(%rsi)
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	0x20(%rsp),%xmm7
	movaps	0x30(%rsp),%xmm8
	movaps	0x40(%rsp),%xmm9
	movaps	0x50(%rsp),%xmm10
	movaps	0x60(%rsp),%xmm11
	movaps	0x70(%rsp),%xmm12
	movaps	0x80(%rsp),%xmm13
	movaps	0x90(%rsp),%xmm14
	movaps	0xa0(%rsp),%xmm15
	lea	0xb8(%rsp),%rsp
.Ldec_epilogue:
___
$code.=<<___;
	ret
.cfi_endproc
.size	${PREFIX}_decrypt,.-${PREFIX}_decrypt
___
{
my ($inp,$out,$len,$key,$ivp,$enc)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9");
# void AES_cbc_encrypt (const void char *inp, unsigned char *out,
#                       size_t length, const AES_KEY *key,
#                       unsigned char *ivp,const int enc);
$code.=<<___;
.globl	${PREFIX}_cbc_encrypt
.type	${PREFIX}_cbc_encrypt,\@function,6
.align	16
${PREFIX}_cbc_encrypt:
.cfi_startproc
	xchg	$key,$len
___
($len,$key)=($key,$len);
$code.=<<___;
	sub	\$16,$len
	jc	.Lcbc_abort
___
$code.=<<___ if ($win64);
	lea	-0xb8(%rsp),%rsp
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
	movaps	%xmm10,0x50(%rsp)
	movaps	%xmm11,0x60(%rsp)
	movaps	%xmm12,0x70(%rsp)
	movaps	%xmm13,0x80(%rsp)
	movaps	%xmm14,0x90(%rsp)
	movaps	%xmm15,0xa0(%rsp)
.Lcbc_body:
___
$code.=<<___;
	movdqu	($ivp),%xmm6		# load IV
	sub	$inp,$out
	call	_vpaes_preheat
	cmp	\$0,${enc}d
	je	.Lcbc_dec_loop
	jmp	.Lcbc_enc_loop
.align	16
.Lcbc_enc_loop:
	movdqu	($inp),%xmm0
	pxor	%xmm6,%xmm0
	call	_vpaes_encrypt_core
	movdqa	%xmm0,%xmm6
	movdqu	%xmm0,($out,$inp)
	lea	16($inp),$inp
	sub	\$16,$len
	jnc	.Lcbc_enc_loop
	jmp	.Lcbc_done
.align	16
.Lcbc_dec_loop:
	movdqu	($inp),%xmm0
	movdqa	%xmm0,%xmm7
	call	_vpaes_decrypt_core
	pxor	%xmm6,%xmm0
	movdqa	%xmm7,%xmm6
	movdqu	%xmm0,($out,$inp)
	lea	16($inp),$inp
	sub	\$16,$len
	jnc	.Lcbc_dec_loop
.Lcbc_done:
	movdqu	%xmm6,($ivp)		# save IV
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	0x20(%rsp),%xmm7
	movaps	0x30(%rsp),%xmm8
	movaps	0x40(%rsp),%xmm9
	movaps	0x50(%rsp),%xmm10
	movaps	0x60(%rsp),%xmm11
	movaps	0x70(%rsp),%xmm12
	movaps	0x80(%rsp),%xmm13
	movaps	0x90(%rsp),%xmm14
	movaps	0xa0(%rsp),%xmm15
	lea	0xb8(%rsp),%rsp
.Lcbc_epilogue:
___
$code.=<<___;
.Lcbc_abort:
	ret
.cfi_endproc
.size	${PREFIX}_cbc_encrypt,.-${PREFIX}_cbc_encrypt
___
}
$code.=<<___;
##
##  _aes_preheat
##
##  Fills register %r10 -> .aes_consts (so you can -fPIC)
##  and %xmm9-%xmm15 as specified below.
##
.type	_vpaes_preheat,\@abi-omnipotent
.align	16
_vpaes_preheat:
.cfi_startproc
	lea	.Lk_s0F(%rip), %r10
	movdqa	-0x20(%r10), %xmm10	# .Lk_inv
	movdqa	-0x10(%r10), %xmm11	# .Lk_inv+16
	movdqa	0x00(%r10), %xmm9	# .Lk_s0F
	movdqa	0x30(%r10), %xmm13	# .Lk_sb1
	movdqa	0x40(%r10), %xmm12	# .Lk_sb1+16
	movdqa	0x50(%r10), %xmm15	# .Lk_sb2
	movdqa	0x60(%r10), %xmm14	# .Lk_sb2+16
	ret
.cfi_endproc
.size	_vpaes_preheat,.-_vpaes_preheat
########################################################
##                                                    ##
##                     Constants                      ##
##                                                    ##
########################################################
.type	_vpaes_consts,\@object
.align	64
_vpaes_consts:
.Lk_inv:	# inv, inva
	.quad	0x0E05060F0D080180, 0x040703090A0B0C02
	.quad	0x01040A060F0B0780, 0x030D0E0C02050809

.Lk_s0F:	# s0F
	.quad	0x0F0F0F0F0F0F0F0F, 0x0F0F0F0F0F0F0F0F

.Lk_ipt:	# input transform (lo, hi)
	.quad	0xC2B2E8985A2A7000, 0xCABAE09052227808
	.quad	0x4C01307D317C4D00, 0xCD80B1FCB0FDCC81

.Lk_sb1:	# sb1u, sb1t
	.quad	0xB19BE18FCB503E00, 0xA5DF7A6E142AF544
	.quad	0x3618D415FAE22300, 0x3BF7CCC10D2ED9EF
.Lk_sb2:	# sb2u, sb2t
	.quad	0xE27A93C60B712400, 0x5EB7E955BC982FCD
	.quad	0x69EB88400AE12900, 0xC2A163C8AB82234A
.Lk_sbo:	# sbou, sbot
	.quad	0xD0D26D176FBDC700, 0x15AABF7AC502A878
	.quad	0xCFE474A55FBB6A00, 0x8E1E90D1412B35FA

.Lk_mc_forward:	# mc_forward
	.quad	0x0407060500030201, 0x0C0F0E0D080B0A09
	.quad	0x080B0A0904070605, 0x000302010C0F0E0D
	.quad	0x0C0F0E0D080B0A09, 0x0407060500030201
	.quad	0x000302010C0F0E0D, 0x080B0A0904070605

.Lk_mc_backward:# mc_backward
	.quad	0x0605040702010003, 0x0E0D0C0F0A09080B
	.quad	0x020100030E0D0C0F, 0x0A09080B06050407
	.quad	0x0E0D0C0F0A09080B, 0x0605040702010003
	.quad	0x0A09080B06050407, 0x020100030E0D0C0F

.Lk_sr:		# sr
	.quad	0x0706050403020100, 0x0F0E0D0C0B0A0908
	.quad	0x030E09040F0A0500, 0x0B06010C07020D08
	.quad	0x0F060D040B020900, 0x070E050C030A0108
	.quad	0x0B0E0104070A0D00, 0x0306090C0F020508

.Lk_rcon:	# rcon
	.quad	0x1F8391B9AF9DEEB6, 0x702A98084D7C7D81

.Lk_s63:	# s63: all equal to 0x63 transformed
	.quad	0x5B5B5B5B5B5B5B5B, 0x5B5B5B5B5B5B5B5B

.Lk_opt:	# output transform
	.quad	0xFF9F4929D6B66000, 0xF7974121DEBE6808
	.quad	0x01EDBD5150BCEC00, 0xE10D5DB1B05C0CE0

.Lk_deskew:	# deskew tables: inverts the sbox's "skew"
	.quad	0x07E4A34047A4E300, 0x1DFEB95A5DBEF91A
	.quad	0x5F36B5DC83EA6900, 0x2841C2ABF49D1E77

##
##  Decryption stuff
##  Key schedule constants
##
.Lk_dksd:	# decryption key schedule: invskew x*D
	.quad	0xFEB91A5DA3E44700, 0x0740E3A45A1DBEF9
	.quad	0x41C277F4B5368300, 0x5FDC69EAAB289D1E
.Lk_dksb:	# decryption key schedule: invskew x*B
	.quad	0x9A4FCA1F8550D500, 0x03D653861CC94C99
	.quad	0x115BEDA7B6FC4A00, 0xD993256F7E3482C8
.Lk_dkse:	# decryption key schedule: invskew x*E + 0x63
	.quad	0xD5031CCA1FC9D600, 0x53859A4C994F5086
	.quad	0xA23196054FDC7BE8, 0xCD5EF96A20B31487
.Lk_dks9:	# decryption key schedule: invskew x*9
	.quad	0xB6116FC87ED9A700, 0x4AED933482255BFC
	.quad	0x4576516227143300, 0x8BB89FACE9DAFDCE

##
##  Decryption stuff
##  Round function constants
##
.Lk_dipt:	# decryption input transform
	.quad	0x0F505B040B545F00, 0x154A411E114E451A
	.quad	0x86E383E660056500, 0x12771772F491F194

.Lk_dsb9:	# decryption sbox output *9*u, *9*t
	.quad	0x851C03539A86D600, 0xCAD51F504F994CC9
	.quad	0xC03B1789ECD74900, 0x725E2C9EB2FBA565
.Lk_dsbd:	# decryption sbox output *D*u, *D*t
	.quad	0x7D57CCDFE6B1A200, 0xF56E9B13882A4439
	.quad	0x3CE2FAF724C6CB00, 0x2931180D15DEEFD3
.Lk_dsbb:	# decryption sbox output *B*u, *B*t
	.quad	0xD022649296B44200, 0x602646F6B0F2D404
	.quad	0xC19498A6CD596700, 0xF3FF0C3E3255AA6B
.Lk_dsbe:	# decryption sbox output *E*u, *E*t
	.quad	0x46F2929626D4D000, 0x2242600464B4F6B0
	.quad	0x0C55A6CDFFAAC100, 0x9467F36B98593E32
.Lk_dsbo:	# decryption sbox final output
	.quad	0x1387EA537EF94000, 0xC7AA6DB9D4943E2D
	.quad	0x12D7560F93441D00, 0xCA4B8159D8C58E9C
.asciz	"Vector Permutation AES for x86_64/SSSE3, Mike Hamburg (Stanford University)"
.align	64
.size	_vpaes_consts,.-_vpaes_consts
___

if ($win64) {
# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	se_handler,\@abi-omnipotent
.align	16
se_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue

	lea	16(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq
	lea	0xb8(%rax),%rax		# adjust stack pointer

.Lin_prologue:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$`1232/8`,%ecx		# sizeof(CONTEXT)
	.long	0xa548f3fc		# cld; rep movsq

	mov	$disp,%rsi
	xor	%rcx,%rcx		# arg1, UNW_FLAG_NHANDLER
	mov	8(%rsi),%rdx		# arg2, disp->ImageBase
	mov	0(%rsi),%r8		# arg3, disp->ControlPc
	mov	16(%rsi),%r9		# arg4, disp->FunctionEntry
	mov	40(%rsi),%r10		# disp->ContextRecord
	lea	56(%rsi),%r11		# &disp->HandlerData
	lea	24(%rsi),%r12		# &disp->EstablisherFrame
	mov	%r10,32(%rsp)		# arg5
	mov	%r11,40(%rsp)		# arg6
	mov	%r12,48(%rsp)		# arg7
	mov	%rcx,56(%rsp)		# arg8, (NULL)
	call	*__imp_RtlVirtualUnwind(%rip)

	mov	\$1,%eax		# ExceptionContinueSearch
	add	\$64,%rsp
	popfq
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	pop	%rdi
	pop	%rsi
	ret
.size	se_handler,.-se_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_${PREFIX}_set_encrypt_key
	.rva	.LSEH_end_${PREFIX}_set_encrypt_key
	.rva	.LSEH_info_${PREFIX}_set_encrypt_key

	.rva	.LSEH_begin_${PREFIX}_set_decrypt_key
	.rva	.LSEH_end_${PREFIX}_set_decrypt_key
	.rva	.LSEH_info_${PREFIX}_set_decrypt_key

	.rva	.LSEH_begin_${PREFIX}_encrypt
	.rva	.LSEH_end_${PREFIX}_encrypt
	.rva	.LSEH_info_${PREFIX}_encrypt

	.rva	.LSEH_begin_${PREFIX}_decrypt
	.rva	.LSEH_end_${PREFIX}_decrypt
	.rva	.LSEH_info_${PREFIX}_decrypt

	.rva	.LSEH_begin_${PREFIX}_cbc_encrypt
	.rva	.LSEH_end_${PREFIX}_cbc_encrypt
	.rva	.LSEH_info_${PREFIX}_cbc_encrypt

.section	.xdata
.align	8
.LSEH_info_${PREFIX}_set_encrypt_key:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lenc_key_body,.Lenc_key_epilogue	# HandlerData[]
.LSEH_info_${PREFIX}_set_decrypt_key:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Ldec_key_body,.Ldec_key_epilogue	# HandlerData[]
.LSEH_info_${PREFIX}_encrypt:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lenc_body,.Lenc_epilogue		# HandlerData[]
.LSEH_info_${PREFIX}_decrypt:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Ldec_body,.Ldec_epilogue		# HandlerData[]
.LSEH_info_${PREFIX}_cbc_encrypt:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lcbc_body,.Lcbc_epilogue		# HandlerData[]
___
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;
