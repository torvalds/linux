#! /usr/bin/env perl
# Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Copyright (c) 2008 Andy Polyakov <appro@openssl.org>
#
# This module may be used under the terms of either the GNU General
# Public License version 2 or later, the GNU Lesser General Public
# License version 2.1 or later, the Mozilla Public License version
# 1.1 or the BSD License. The exact terms of either license are
# distributed along with this module. For further details see
# http://www.openssl.org/~appro/camellia/.
# ====================================================================

# Performance in cycles per processed byte (less is better) in
# 'openssl speed ...' benchmark:
#
#			AMD64	Core2	EM64T
# -evp camellia-128-ecb	16.7	21.0	22.7
# + over gcc 3.4.6	+25%	+5%	0%
#
# camellia-128-cbc	15.7	20.4	21.1
#
# 128-bit key setup	128	216	205	cycles/key
# + over gcc 3.4.6	+54%	+39%	+15%
#
# Numbers in "+" rows represent performance improvement over compiler
# generated code. Key setup timings are impressive on AMD and Core2
# thanks to 64-bit operations being covertly deployed. Improvement on
# EM64T, pre-Core2 Intel x86_64 CPU, is not as impressive, because it
# apparently emulates some of 64-bit operations in [32-bit] microcode.

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

sub hi() { my $r=shift; $r =~ s/%[er]([a-d])x/%\1h/;    $r; }
sub lo() { my $r=shift; $r =~ s/%[er]([a-d])x/%\1l/;
                        $r =~ s/%[er]([sd]i)/%\1l/;
                        $r =~ s/%(r[0-9]+)[d]?/%\1b/;   $r; }

$t0="%eax";$t1="%ebx";$t2="%ecx";$t3="%edx";
@S=("%r8d","%r9d","%r10d","%r11d");
$i0="%esi";
$i1="%edi";
$Tbl="%rbp";	# size optimization
$inp="%r12";
$out="%r13";
$key="%r14";
$keyend="%r15";
$arg0d=$win64?"%ecx":"%edi";

# const unsigned int Camellia_SBOX[4][256];
# Well, sort of... Camellia_SBOX[0][] is interleaved with [1][],
# and [2][] - with [3][]. This is done to minimize code size.
$SBOX1_1110=0;		# Camellia_SBOX[0]
$SBOX4_4404=4;		# Camellia_SBOX[1]
$SBOX2_0222=2048;	# Camellia_SBOX[2]
$SBOX3_3033=2052;	# Camellia_SBOX[3]

sub Camellia_Feistel {
my $i=@_[0];
my $seed=defined(@_[1])?@_[1]:0;
my $scale=$seed<0?-8:8;
my $j=($i&1)*2;
my ($s0,$s1,$s2,$s3)=(@S[($j)%4],@S[($j+1)%4],@S[($j+2)%4],@S[($j+3)%4]);

$code.=<<___;
	xor	$s0,$t0				# t0^=key[0]
	xor	$s1,$t1				# t1^=key[1]
	movz	`&hi("$t0")`,$i0		# (t0>>8)&0xff
	movz	`&lo("$t1")`,$i1		# (t1>>0)&0xff
	mov	$SBOX3_3033($Tbl,$i0,8),$t3	# t3=SBOX3_3033[0]
	mov	$SBOX1_1110($Tbl,$i1,8),$t2	# t2=SBOX1_1110[1]
	movz	`&lo("$t0")`,$i0		# (t0>>0)&0xff
	shr	\$16,$t0
	movz	`&hi("$t1")`,$i1		# (t1>>8)&0xff
	xor	$SBOX4_4404($Tbl,$i0,8),$t3	# t3^=SBOX4_4404[0]
	shr	\$16,$t1
	xor	$SBOX4_4404($Tbl,$i1,8),$t2	# t2^=SBOX4_4404[1]
	movz	`&hi("$t0")`,$i0		# (t0>>24)&0xff
	movz	`&lo("$t1")`,$i1		# (t1>>16)&0xff
	xor	$SBOX1_1110($Tbl,$i0,8),$t3	# t3^=SBOX1_1110[0]
	xor	$SBOX3_3033($Tbl,$i1,8),$t2	# t2^=SBOX3_3033[1]
	movz	`&lo("$t0")`,$i0		# (t0>>16)&0xff
	movz	`&hi("$t1")`,$i1		# (t1>>24)&0xff
	xor	$SBOX2_0222($Tbl,$i0,8),$t3	# t3^=SBOX2_0222[0]
	xor	$SBOX2_0222($Tbl,$i1,8),$t2	# t2^=SBOX2_0222[1]
	mov	`$seed+($i+1)*$scale`($key),$t1	# prefetch key[i+1]
	mov	`$seed+($i+1)*$scale+4`($key),$t0
	xor	$t3,$t2				# t2^=t3
	ror	\$8,$t3				# t3=RightRotate(t3,8)
	xor	$t2,$s2
	xor	$t2,$s3
	xor	$t3,$s3
___
}

# void Camellia_EncryptBlock_Rounds(
#		int grandRounds,
#		const Byte plaintext[],
#		const KEY_TABLE_TYPE keyTable,
#		Byte ciphertext[])
$code=<<___;
.text

# V1.x API
.globl	Camellia_EncryptBlock
.type	Camellia_EncryptBlock,\@abi-omnipotent
.align	16
Camellia_EncryptBlock:
	movl	\$128,%eax
	subl	$arg0d,%eax
	movl	\$3,$arg0d
	adcl	\$0,$arg0d	# keyBitLength==128?3:4
	jmp	.Lenc_rounds
.size	Camellia_EncryptBlock,.-Camellia_EncryptBlock
# V2
.globl	Camellia_EncryptBlock_Rounds
.type	Camellia_EncryptBlock_Rounds,\@function,4
.align	16
.Lenc_rounds:
Camellia_EncryptBlock_Rounds:
.cfi_startproc
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lenc_prologue:

	#mov	%rsi,$inp		# put away arguments
	mov	%rcx,$out
	mov	%rdx,$key

	shl	\$6,%edi		# process grandRounds
	lea	.LCamellia_SBOX(%rip),$Tbl
	lea	($key,%rdi),$keyend

	mov	0(%rsi),@S[0]		# load plaintext
	mov	4(%rsi),@S[1]
	mov	8(%rsi),@S[2]
	bswap	@S[0]
	mov	12(%rsi),@S[3]
	bswap	@S[1]
	bswap	@S[2]
	bswap	@S[3]

	call	_x86_64_Camellia_encrypt

	bswap	@S[0]
	bswap	@S[1]
	bswap	@S[2]
	mov	@S[0],0($out)
	bswap	@S[3]
	mov	@S[1],4($out)
	mov	@S[2],8($out)
	mov	@S[3],12($out)

	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%rbp
.cfi_restore	%rbp
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	lea	40(%rsp),%rsp
.cfi_adjust_cfa_offset	-40
.Lenc_epilogue:
	ret
.cfi_endproc
.size	Camellia_EncryptBlock_Rounds,.-Camellia_EncryptBlock_Rounds

.type	_x86_64_Camellia_encrypt,\@abi-omnipotent
.align	16
_x86_64_Camellia_encrypt:
	xor	0($key),@S[1]
	xor	4($key),@S[0]		# ^=key[0-3]
	xor	8($key),@S[3]
	xor	12($key),@S[2]
.align	16
.Leloop:
	mov	16($key),$t1		# prefetch key[4-5]
	mov	20($key),$t0

___
	for ($i=0;$i<6;$i++) { Camellia_Feistel($i,16); }
$code.=<<___;
	lea	16*4($key),$key
	cmp	$keyend,$key
	mov	8($key),$t3		# prefetch key[2-3]
	mov	12($key),$t2
	je	.Ledone

	and	@S[0],$t0
	or	@S[3],$t3
	rol	\$1,$t0
	xor	$t3,@S[2]		# s2^=s3|key[3];
	xor	$t0,@S[1]		# s1^=LeftRotate(s0&key[0],1);
	and	@S[2],$t2
	or	@S[1],$t1
	rol	\$1,$t2
	xor	$t1,@S[0]		# s0^=s1|key[1];
	xor	$t2,@S[3]		# s3^=LeftRotate(s2&key[2],1);
	jmp	.Leloop

.align	16
.Ledone:
	xor	@S[2],$t0		# SwapHalf
	xor	@S[3],$t1
	xor	@S[0],$t2
	xor	@S[1],$t3

	mov	$t0,@S[0]
	mov	$t1,@S[1]
	mov	$t2,@S[2]
	mov	$t3,@S[3]

	.byte	0xf3,0xc3		# rep ret
.size	_x86_64_Camellia_encrypt,.-_x86_64_Camellia_encrypt

# V1.x API
.globl	Camellia_DecryptBlock
.type	Camellia_DecryptBlock,\@abi-omnipotent
.align	16
Camellia_DecryptBlock:
	movl	\$128,%eax
	subl	$arg0d,%eax
	movl	\$3,$arg0d
	adcl	\$0,$arg0d	# keyBitLength==128?3:4
	jmp	.Ldec_rounds
.size	Camellia_DecryptBlock,.-Camellia_DecryptBlock
# V2
.globl	Camellia_DecryptBlock_Rounds
.type	Camellia_DecryptBlock_Rounds,\@function,4
.align	16
.Ldec_rounds:
Camellia_DecryptBlock_Rounds:
.cfi_startproc
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Ldec_prologue:

	#mov	%rsi,$inp		# put away arguments
	mov	%rcx,$out
	mov	%rdx,$keyend

	shl	\$6,%edi		# process grandRounds
	lea	.LCamellia_SBOX(%rip),$Tbl
	lea	($keyend,%rdi),$key

	mov	0(%rsi),@S[0]		# load plaintext
	mov	4(%rsi),@S[1]
	mov	8(%rsi),@S[2]
	bswap	@S[0]
	mov	12(%rsi),@S[3]
	bswap	@S[1]
	bswap	@S[2]
	bswap	@S[3]

	call	_x86_64_Camellia_decrypt

	bswap	@S[0]
	bswap	@S[1]
	bswap	@S[2]
	mov	@S[0],0($out)
	bswap	@S[3]
	mov	@S[1],4($out)
	mov	@S[2],8($out)
	mov	@S[3],12($out)

	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%rbp
.cfi_restore	%rbp
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	lea	40(%rsp),%rsp
.cfi_adjust_cfa_offset	-40
.Ldec_epilogue:
	ret
.cfi_endproc
.size	Camellia_DecryptBlock_Rounds,.-Camellia_DecryptBlock_Rounds

.type	_x86_64_Camellia_decrypt,\@abi-omnipotent
.align	16
_x86_64_Camellia_decrypt:
	xor	0($key),@S[1]
	xor	4($key),@S[0]		# ^=key[0-3]
	xor	8($key),@S[3]
	xor	12($key),@S[2]
.align	16
.Ldloop:
	mov	-8($key),$t1		# prefetch key[4-5]
	mov	-4($key),$t0

___
	for ($i=0;$i<6;$i++) { Camellia_Feistel($i,-8); }
$code.=<<___;
	lea	-16*4($key),$key
	cmp	$keyend,$key
	mov	0($key),$t3		# prefetch key[2-3]
	mov	4($key),$t2
	je	.Lddone

	and	@S[0],$t0
	or	@S[3],$t3
	rol	\$1,$t0
	xor	$t3,@S[2]		# s2^=s3|key[3];
	xor	$t0,@S[1]		# s1^=LeftRotate(s0&key[0],1);
	and	@S[2],$t2
	or	@S[1],$t1
	rol	\$1,$t2
	xor	$t1,@S[0]		# s0^=s1|key[1];
	xor	$t2,@S[3]		# s3^=LeftRotate(s2&key[2],1);

	jmp	.Ldloop

.align	16
.Lddone:
	xor	@S[2],$t2
	xor	@S[3],$t3
	xor	@S[0],$t0
	xor	@S[1],$t1

	mov	$t2,@S[0]		# SwapHalf
	mov	$t3,@S[1]
	mov	$t0,@S[2]
	mov	$t1,@S[3]

	.byte	0xf3,0xc3		# rep ret
.size	_x86_64_Camellia_decrypt,.-_x86_64_Camellia_decrypt
___

sub _saveround {
my ($rnd,$key,@T)=@_;
my $bias=int(@T[0])?shift(@T):0;

    if ($#T==3) {
	$code.=<<___;
	mov	@T[1],`$bias+$rnd*8+0`($key)
	mov	@T[0],`$bias+$rnd*8+4`($key)
	mov	@T[3],`$bias+$rnd*8+8`($key)
	mov	@T[2],`$bias+$rnd*8+12`($key)
___
    } else {
	$code.="	mov	@T[0],`$bias+$rnd*8+0`($key)\n";
	$code.="	mov	@T[1],`$bias+$rnd*8+8`($key)\n"	if ($#T>=1);
    }
}

sub _loadround {
my ($rnd,$key,@T)=@_;
my $bias=int(@T[0])?shift(@T):0;

$code.="	mov	`$bias+$rnd*8+0`($key),@T[0]\n";
$code.="	mov	`$bias+$rnd*8+8`($key),@T[1]\n"	if ($#T>=1);
}

# shld is very slow on Intel EM64T family. Even on AMD it limits
# instruction decode rate [because it's VectorPath] and consequently
# performance...
sub __rotl128 {
my ($i0,$i1,$rot)=@_;

    if ($rot) {
	$code.=<<___;
	mov	$i0,%r11
	shld	\$$rot,$i1,$i0
	shld	\$$rot,%r11,$i1
___
    }
}

# ... Implementing 128-bit rotate without shld gives 80% better
# performance EM64T, +15% on AMD64 and only ~7% degradation on
# Core2. This is therefore preferred.
sub _rotl128 {
my ($i0,$i1,$rot)=@_;

    if ($rot) {
	$code.=<<___;
	mov	$i0,%r11
	shl	\$$rot,$i0
	mov	$i1,%r9
	shr	\$`64-$rot`,%r9
	shr	\$`64-$rot`,%r11
	or	%r9,$i0
	shl	\$$rot,$i1
	or	%r11,$i1
___
    }
}

{ my $step=0;

$code.=<<___;
.globl	Camellia_Ekeygen
.type	Camellia_Ekeygen,\@function,3
.align	16
Camellia_Ekeygen:
.cfi_startproc
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lkey_prologue:

	mov	%edi,${keyend}d		# put away arguments, keyBitLength
	mov	%rdx,$out		# keyTable

	mov	0(%rsi),@S[0]		# load 0-127 bits
	mov	4(%rsi),@S[1]
	mov	8(%rsi),@S[2]
	mov	12(%rsi),@S[3]

	bswap	@S[0]
	bswap	@S[1]
	bswap	@S[2]
	bswap	@S[3]
___
	&_saveround	(0,$out,@S);	# KL<<<0
$code.=<<___;
	cmp	\$128,$keyend		# check keyBitLength
	je	.L1st128

	mov	16(%rsi),@S[0]		# load 128-191 bits
	mov	20(%rsi),@S[1]
	cmp	\$192,$keyend
	je	.L1st192
	mov	24(%rsi),@S[2]		# load 192-255 bits
	mov	28(%rsi),@S[3]
	jmp	.L1st256
.L1st192:
	mov	@S[0],@S[2]
	mov	@S[1],@S[3]
	not	@S[2]
	not	@S[3]
.L1st256:
	bswap	@S[0]
	bswap	@S[1]
	bswap	@S[2]
	bswap	@S[3]
___
	&_saveround	(4,$out,@S);	# temp storage for KR!
$code.=<<___;
	xor	0($out),@S[1]		# KR^KL
	xor	4($out),@S[0]
	xor	8($out),@S[3]
	xor	12($out),@S[2]

.L1st128:
	lea	.LCamellia_SIGMA(%rip),$key
	lea	.LCamellia_SBOX(%rip),$Tbl

	mov	0($key),$t1
	mov	4($key),$t0
___
	&Camellia_Feistel($step++);
	&Camellia_Feistel($step++);
$code.=<<___;
	xor	0($out),@S[1]		# ^KL
	xor	4($out),@S[0]
	xor	8($out),@S[3]
	xor	12($out),@S[2]
___
	&Camellia_Feistel($step++);
	&Camellia_Feistel($step++);
$code.=<<___;
	cmp	\$128,$keyend
	jne	.L2nd256

	lea	128($out),$out		# size optimization
	shl	\$32,%r8		# @S[0]||
	shl	\$32,%r10		# @S[2]||
	or	%r9,%r8			# ||@S[1]
	or	%r11,%r10		# ||@S[3]
___
	&_loadround	(0,$out,-128,"%rax","%rbx");	# KL
	&_saveround	(2,$out,-128,"%r8","%r10");	# KA<<<0
	&_rotl128	("%rax","%rbx",15);
	&_saveround	(4,$out,-128,"%rax","%rbx");	# KL<<<15
	&_rotl128	("%r8","%r10",15);
	&_saveround	(6,$out,-128,"%r8","%r10");	# KA<<<15
	&_rotl128	("%r8","%r10",15);		# 15+15=30
	&_saveround	(8,$out,-128,"%r8","%r10");	# KA<<<30
	&_rotl128	("%rax","%rbx",30);		# 15+30=45
	&_saveround	(10,$out,-128,"%rax","%rbx");	# KL<<<45
	&_rotl128	("%r8","%r10",15);		# 30+15=45
	&_saveround	(12,$out,-128,"%r8");		# KA<<<45
	&_rotl128	("%rax","%rbx",15);		# 45+15=60
	&_saveround	(13,$out,-128,"%rbx");		# KL<<<60
	&_rotl128	("%r8","%r10",15);		# 45+15=60
	&_saveround	(14,$out,-128,"%r8","%r10");	# KA<<<60
	&_rotl128	("%rax","%rbx",17);		# 60+17=77
	&_saveround	(16,$out,-128,"%rax","%rbx");	# KL<<<77
	&_rotl128	("%rax","%rbx",17);		# 77+17=94
	&_saveround	(18,$out,-128,"%rax","%rbx");	# KL<<<94
	&_rotl128	("%r8","%r10",34);		# 60+34=94
	&_saveround	(20,$out,-128,"%r8","%r10");	# KA<<<94
	&_rotl128	("%rax","%rbx",17);		# 94+17=111
	&_saveround	(22,$out,-128,"%rax","%rbx");	# KL<<<111
	&_rotl128	("%r8","%r10",17);		# 94+17=111
	&_saveround	(24,$out,-128,"%r8","%r10");	# KA<<<111
$code.=<<___;
	mov	\$3,%eax
	jmp	.Ldone
.align	16
.L2nd256:
___
	&_saveround	(6,$out,@S);	# temp storage for KA!
$code.=<<___;
	xor	`4*8+0`($out),@S[1]	# KA^KR
	xor	`4*8+4`($out),@S[0]
	xor	`5*8+0`($out),@S[3]
	xor	`5*8+4`($out),@S[2]
___
	&Camellia_Feistel($step++);
	&Camellia_Feistel($step++);

	&_loadround	(0,$out,"%rax","%rbx");	# KL
	&_loadround	(4,$out,"%rcx","%rdx");	# KR
	&_loadround	(6,$out,"%r14","%r15");	# KA
$code.=<<___;
	lea	128($out),$out		# size optimization
	shl	\$32,%r8		# @S[0]||
	shl	\$32,%r10		# @S[2]||
	or	%r9,%r8			# ||@S[1]
	or	%r11,%r10		# ||@S[3]
___
	&_saveround	(2,$out,-128,"%r8","%r10");	# KB<<<0
	&_rotl128	("%rcx","%rdx",15);
	&_saveround	(4,$out,-128,"%rcx","%rdx");	# KR<<<15
	&_rotl128	("%r14","%r15",15);
	&_saveround	(6,$out,-128,"%r14","%r15");	# KA<<<15
	&_rotl128	("%rcx","%rdx",15);		# 15+15=30
	&_saveround	(8,$out,-128,"%rcx","%rdx");	# KR<<<30
	&_rotl128	("%r8","%r10",30);
	&_saveround	(10,$out,-128,"%r8","%r10");	# KB<<<30
	&_rotl128	("%rax","%rbx",45);
	&_saveround	(12,$out,-128,"%rax","%rbx");	# KL<<<45
	&_rotl128	("%r14","%r15",30);		# 15+30=45
	&_saveround	(14,$out,-128,"%r14","%r15");	# KA<<<45
	&_rotl128	("%rax","%rbx",15);		# 45+15=60
	&_saveround	(16,$out,-128,"%rax","%rbx");	# KL<<<60
	&_rotl128	("%rcx","%rdx",30);		# 30+30=60
	&_saveround	(18,$out,-128,"%rcx","%rdx");	# KR<<<60
	&_rotl128	("%r8","%r10",30);		# 30+30=60
	&_saveround	(20,$out,-128,"%r8","%r10");	# KB<<<60
	&_rotl128	("%rax","%rbx",17);		# 60+17=77
	&_saveround	(22,$out,-128,"%rax","%rbx");	# KL<<<77
	&_rotl128	("%r14","%r15",32);		# 45+32=77
	&_saveround	(24,$out,-128,"%r14","%r15");	# KA<<<77
	&_rotl128	("%rcx","%rdx",34);		# 60+34=94
	&_saveround	(26,$out,-128,"%rcx","%rdx");	# KR<<<94
	&_rotl128	("%r14","%r15",17);		# 77+17=94
	&_saveround	(28,$out,-128,"%r14","%r15");	# KA<<<77
	&_rotl128	("%rax","%rbx",34);		# 77+34=111
	&_saveround	(30,$out,-128,"%rax","%rbx");	# KL<<<111
	&_rotl128	("%r8","%r10",51);		# 60+51=111
	&_saveround	(32,$out,-128,"%r8","%r10");	# KB<<<111
$code.=<<___;
	mov	\$4,%eax
.Ldone:
	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%rbp
.cfi_restore	%rbp
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	lea	40(%rsp),%rsp
.cfi_adjust_cfa_offset	-40
.Lkey_epilogue:
	ret
.cfi_endproc
.size	Camellia_Ekeygen,.-Camellia_Ekeygen
___
}

@SBOX=(
112,130, 44,236,179, 39,192,229,228,133, 87, 53,234, 12,174, 65,
 35,239,107,147, 69, 25,165, 33,237, 14, 79, 78, 29,101,146,189,
134,184,175,143,124,235, 31,206, 62, 48,220, 95, 94,197, 11, 26,
166,225, 57,202,213, 71, 93, 61,217,  1, 90,214, 81, 86,108, 77,
139, 13,154,102,251,204,176, 45,116, 18, 43, 32,240,177,132,153,
223, 76,203,194, 52,126,118,  5,109,183,169, 49,209, 23,  4,215,
 20, 88, 58, 97,222, 27, 17, 28, 50, 15,156, 22, 83, 24,242, 34,
254, 68,207,178,195,181,122,145, 36,  8,232,168, 96,252,105, 80,
170,208,160,125,161,137, 98,151, 84, 91, 30,149,224,255,100,210,
 16,196,  0, 72,163,247,117,219,138,  3,230,218,  9, 63,221,148,
135, 92,131,  2,205, 74,144, 51,115,103,246,243,157,127,191,226,
 82,155,216, 38,200, 55,198, 59,129,150,111, 75, 19,190, 99, 46,
233,121,167,140,159,110,188,142, 41,245,249,182, 47,253,180, 89,
120,152,  6,106,231, 70,113,186,212, 37,171, 66,136,162,141,250,
114,  7,185, 85,248,238,172, 10, 54, 73, 42,104, 60, 56,241,164,
 64, 40,211,123,187,201, 67,193, 21,227,173,244,119,199,128,158);

sub S1110 { my $i=shift; $i=@SBOX[$i]; $i=$i<<24|$i<<16|$i<<8; sprintf("0x%08x",$i); }
sub S4404 { my $i=shift; $i=($i<<1|$i>>7)&0xff; $i=@SBOX[$i]; $i=$i<<24|$i<<16|$i; sprintf("0x%08x",$i); }
sub S0222 { my $i=shift; $i=@SBOX[$i]; $i=($i<<1|$i>>7)&0xff; $i=$i<<16|$i<<8|$i; sprintf("0x%08x",$i); }
sub S3033 { my $i=shift; $i=@SBOX[$i]; $i=($i>>1|$i<<7)&0xff; $i=$i<<24|$i<<8|$i; sprintf("0x%08x",$i); }

$code.=<<___;
.align	64
.LCamellia_SIGMA:
.long	0x3bcc908b, 0xa09e667f, 0x4caa73b2, 0xb67ae858
.long	0xe94f82be, 0xc6ef372f, 0xf1d36f1c, 0x54ff53a5
.long	0xde682d1d, 0x10e527fa, 0xb3e6c1fd, 0xb05688c2
.long	0,          0,          0,          0
.LCamellia_SBOX:
___
# tables are interleaved, remember?
sub data_word { $code.=".long\t".join(',',@_)."\n"; }
for ($i=0;$i<256;$i++) { &data_word(&S1110($i),&S4404($i)); }
for ($i=0;$i<256;$i++) { &data_word(&S0222($i),&S3033($i)); }

# void Camellia_cbc_encrypt (const void char *inp, unsigned char *out,
#			size_t length, const CAMELLIA_KEY *key,
#			unsigned char *ivp,const int enc);
{
$_key="0(%rsp)";
$_end="8(%rsp)";	# inp+len&~15
$_res="16(%rsp)";	# len&15
$ivec="24(%rsp)";
$_ivp="40(%rsp)";
$_rsp="48(%rsp)";

$code.=<<___;
.globl	Camellia_cbc_encrypt
.type	Camellia_cbc_encrypt,\@function,6
.align	16
Camellia_cbc_encrypt:
.cfi_startproc
	cmp	\$0,%rdx
	je	.Lcbc_abort
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lcbc_prologue:

	mov	%rsp,%rbp
.cfi_def_cfa_register	%rbp
	sub	\$64,%rsp
	and	\$-64,%rsp

	# place stack frame just "above mod 1024" the key schedule,
	# this ensures that cache associativity suffices
	lea	-64-63(%rcx),%r10
	sub	%rsp,%r10
	neg	%r10
	and	\$0x3C0,%r10
	sub	%r10,%rsp
	#add	\$8,%rsp		# 8 is reserved for callee's ra

	mov	%rdi,$inp		# inp argument
	mov	%rsi,$out		# out argument
	mov	%r8,%rbx		# ivp argument
	mov	%rcx,$key		# key argument
	mov	272(%rcx),${keyend}d	# grandRounds

	mov	%r8,$_ivp
	mov	%rbp,$_rsp
.cfi_cfa_expression	$_rsp,deref,+56

.Lcbc_body:
	lea	.LCamellia_SBOX(%rip),$Tbl

	mov	\$32,%ecx
.align	4
.Lcbc_prefetch_sbox:
	mov	0($Tbl),%rax
	mov	32($Tbl),%rsi
	mov	64($Tbl),%rdi
	mov	96($Tbl),%r11
	lea	128($Tbl),$Tbl
	loop	.Lcbc_prefetch_sbox
	sub	\$4096,$Tbl
	shl	\$6,$keyend
	mov	%rdx,%rcx		# len argument
	lea	($key,$keyend),$keyend

	cmp	\$0,%r9d		# enc argument
	je	.LCBC_DECRYPT

	and	\$-16,%rdx
	and	\$15,%rcx		# length residue
	lea	($inp,%rdx),%rdx
	mov	$key,$_key
	mov	%rdx,$_end
	mov	%rcx,$_res

	cmp	$inp,%rdx
	mov	0(%rbx),@S[0]		# load IV
	mov	4(%rbx),@S[1]
	mov	8(%rbx),@S[2]
	mov	12(%rbx),@S[3]
	je	.Lcbc_enc_tail
	jmp	.Lcbc_eloop

.align	16
.Lcbc_eloop:
	xor	0($inp),@S[0]
	xor	4($inp),@S[1]
	xor	8($inp),@S[2]
	bswap	@S[0]
	xor	12($inp),@S[3]
	bswap	@S[1]
	bswap	@S[2]
	bswap	@S[3]

	call	_x86_64_Camellia_encrypt

	mov	$_key,$key		# "rewind" the key
	bswap	@S[0]
	mov	$_end,%rdx
	bswap	@S[1]
	mov	$_res,%rcx
	bswap	@S[2]
	mov	@S[0],0($out)
	bswap	@S[3]
	mov	@S[1],4($out)
	mov	@S[2],8($out)
	lea	16($inp),$inp
	mov	@S[3],12($out)
	cmp	%rdx,$inp
	lea	16($out),$out
	jne	.Lcbc_eloop

	cmp	\$0,%rcx
	jne	.Lcbc_enc_tail

	mov	$_ivp,$out
	mov	@S[0],0($out)		# write out IV residue
	mov	@S[1],4($out)
	mov	@S[2],8($out)
	mov	@S[3],12($out)
	jmp	.Lcbc_done

.align	16
.Lcbc_enc_tail:
	xor	%rax,%rax
	mov	%rax,0+$ivec
	mov	%rax,8+$ivec
	mov	%rax,$_res

.Lcbc_enc_pushf:
	pushfq
	cld
	mov	$inp,%rsi
	lea	8+$ivec,%rdi
	.long	0x9066A4F3		# rep movsb
	popfq
.Lcbc_enc_popf:

	lea	$ivec,$inp
	lea	16+$ivec,%rax
	mov	%rax,$_end
	jmp	.Lcbc_eloop		# one more time

.align	16
.LCBC_DECRYPT:
	xchg	$key,$keyend
	add	\$15,%rdx
	and	\$15,%rcx		# length residue
	and	\$-16,%rdx
	mov	$key,$_key
	lea	($inp,%rdx),%rdx
	mov	%rdx,$_end
	mov	%rcx,$_res

	mov	(%rbx),%rax		# load IV
	mov	8(%rbx),%rbx
	jmp	.Lcbc_dloop
.align	16
.Lcbc_dloop:
	mov	0($inp),@S[0]
	mov	4($inp),@S[1]
	mov	8($inp),@S[2]
	bswap	@S[0]
	mov	12($inp),@S[3]
	bswap	@S[1]
	mov	%rax,0+$ivec		# save IV to temporary storage
	bswap	@S[2]
	mov	%rbx,8+$ivec
	bswap	@S[3]

	call	_x86_64_Camellia_decrypt

	mov	$_key,$key		# "rewind" the key
	mov	$_end,%rdx
	mov	$_res,%rcx

	bswap	@S[0]
	mov	($inp),%rax		# load IV for next iteration
	bswap	@S[1]
	mov	8($inp),%rbx
	bswap	@S[2]
	xor	0+$ivec,@S[0]
	bswap	@S[3]
	xor	4+$ivec,@S[1]
	xor	8+$ivec,@S[2]
	lea	16($inp),$inp
	xor	12+$ivec,@S[3]
	cmp	%rdx,$inp
	je	.Lcbc_ddone

	mov	@S[0],0($out)
	mov	@S[1],4($out)
	mov	@S[2],8($out)
	mov	@S[3],12($out)

	lea	16($out),$out
	jmp	.Lcbc_dloop

.align	16
.Lcbc_ddone:
	mov	$_ivp,%rdx
	cmp	\$0,%rcx
	jne	.Lcbc_dec_tail

	mov	@S[0],0($out)
	mov	@S[1],4($out)
	mov	@S[2],8($out)
	mov	@S[3],12($out)

	mov	%rax,(%rdx)		# write out IV residue
	mov	%rbx,8(%rdx)
	jmp	.Lcbc_done
.align	16
.Lcbc_dec_tail:
	mov	@S[0],0+$ivec
	mov	@S[1],4+$ivec
	mov	@S[2],8+$ivec
	mov	@S[3],12+$ivec

.Lcbc_dec_pushf:
	pushfq
	cld
	lea	8+$ivec,%rsi
	lea	($out),%rdi
	.long	0x9066A4F3		# rep movsb
	popfq
.Lcbc_dec_popf:

	mov	%rax,(%rdx)		# write out IV residue
	mov	%rbx,8(%rdx)
	jmp	.Lcbc_done

.align	16
.Lcbc_done:
	mov	$_rsp,%rcx
.cfi_def_cfa	%rcx,56
	mov	0(%rcx),%r15
.cfi_restore	%r15
	mov	8(%rcx),%r14
.cfi_restore	%r14
	mov	16(%rcx),%r13
.cfi_restore	%r13
	mov	24(%rcx),%r12
.cfi_restore	%r12
	mov	32(%rcx),%rbp
.cfi_restore	%rbp
	mov	40(%rcx),%rbx
.cfi_restore	%rbx
	lea	48(%rcx),%rsp
.cfi_def_cfa	%rsp,8
.Lcbc_abort:
	ret
.cfi_endproc
.size	Camellia_cbc_encrypt,.-Camellia_cbc_encrypt

.asciz	"Camellia for x86_64 by <appro\@openssl.org>"
___
}

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	common_se_handler,\@abi-omnipotent
.align	16
common_se_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	lea	-64(%rsp),%rsp

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

	lea	40(%rax),%rax
	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r13
	mov	-32(%rax),%r14
	mov	-40(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

.Lin_prologue:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	jmp	.Lcommon_seh_exit
.size	common_se_handler,.-common_se_handler

.type	cbc_se_handler,\@abi-omnipotent
.align	16
cbc_se_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	lea	-64(%rsp),%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	lea	.Lcbc_prologue(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lcbc_prologue
	jb	.Lin_cbc_prologue

	lea	.Lcbc_body(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lcbc_body
	jb	.Lin_cbc_frame_setup

	mov	152($context),%rax	# pull context->Rsp

	lea	.Lcbc_abort(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>=.Lcbc_abort
	jae	.Lin_cbc_prologue

	# handle pushf/popf in Camellia_cbc_encrypt
	lea	.Lcbc_enc_pushf(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<=.Lcbc_enc_pushf
	jbe	.Lin_cbc_no_flag
	lea	8(%rax),%rax
	lea	.Lcbc_enc_popf(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lcbc_enc_popf
	jb	.Lin_cbc_no_flag
	lea	-8(%rax),%rax
	lea	.Lcbc_dec_pushf(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<=.Lcbc_dec_pushf
	jbe	.Lin_cbc_no_flag
	lea	8(%rax),%rax
	lea	.Lcbc_dec_popf(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lcbc_dec_popf
	jb	.Lin_cbc_no_flag
	lea	-8(%rax),%rax

.Lin_cbc_no_flag:
	mov	48(%rax),%rax		# $_rsp
	lea	48(%rax),%rax

.Lin_cbc_frame_setup:
	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	-48(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

.Lin_cbc_prologue:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

.align	4
.Lcommon_seh_exit:

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
	lea	64(%rsp),%rsp
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
.size	cbc_se_handler,.-cbc_se_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_Camellia_EncryptBlock_Rounds
	.rva	.LSEH_end_Camellia_EncryptBlock_Rounds
	.rva	.LSEH_info_Camellia_EncryptBlock_Rounds

	.rva	.LSEH_begin_Camellia_DecryptBlock_Rounds
	.rva	.LSEH_end_Camellia_DecryptBlock_Rounds
	.rva	.LSEH_info_Camellia_DecryptBlock_Rounds

	.rva	.LSEH_begin_Camellia_Ekeygen
	.rva	.LSEH_end_Camellia_Ekeygen
	.rva	.LSEH_info_Camellia_Ekeygen

	.rva	.LSEH_begin_Camellia_cbc_encrypt
	.rva	.LSEH_end_Camellia_cbc_encrypt
	.rva	.LSEH_info_Camellia_cbc_encrypt

.section	.xdata
.align	8
.LSEH_info_Camellia_EncryptBlock_Rounds:
	.byte	9,0,0,0
	.rva	common_se_handler
	.rva	.Lenc_prologue,.Lenc_epilogue	# HandlerData[]
.LSEH_info_Camellia_DecryptBlock_Rounds:
	.byte	9,0,0,0
	.rva	common_se_handler
	.rva	.Ldec_prologue,.Ldec_epilogue	# HandlerData[]
.LSEH_info_Camellia_Ekeygen:
	.byte	9,0,0,0
	.rva	common_se_handler
	.rva	.Lkey_prologue,.Lkey_epilogue	# HandlerData[]
.LSEH_info_Camellia_cbc_encrypt:
	.byte	9,0,0,0
	.rva	cbc_se_handler
___
}

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;
