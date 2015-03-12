#!/usr/bin/env perl
#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
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
#########################################################################
{{{	# CBC en- and decrypt procedures				#
my ($inp,$out,$len,$key,$ivp,$enc,$rounds,$idx)=map("r$_",(3..10));
my ($rndkey0,$rndkey1,$inout,$tmp)=		map("v$_",(0..3));
my ($ivec,$inptail,$inpperm,$outhead,$outperm,$outmask,$keyperm)=
						map("v$_",(4..10));
$code.=<<___;
.globl	.${prefix}_cbc_encrypt
	${UCMP}i	$len,16
	bltlr-

	cmpwi		$enc,0			# test direction
	lis		r0,0xffe0
	mfspr		$vrsave,256
	mtspr		256,r0

	li		$idx,15
	vxor		$rndkey0,$rndkey0,$rndkey0
	le?vspltisb	$tmp,0x0f

	lvx		$ivec,0,$ivp		# load [unaligned] iv
	lvsl		$inpperm,0,$ivp
	lvx		$inptail,$idx,$ivp
	le?vxor		$inpperm,$inpperm,$tmp
	vperm		$ivec,$ivec,$inptail,$inpperm

	neg		r11,$inp
	?lvsl		$keyperm,0,$key		# prepare for unaligned key
	lwz		$rounds,240($key)

	lvsr		$inpperm,0,r11		# prepare for unaligned load
	lvx		$inptail,0,$inp
	addi		$inp,$inp,15		# 15 is not typo
	le?vxor		$inpperm,$inpperm,$tmp

	?lvsr		$outperm,0,$out		# prepare for unaligned store
	vspltisb	$outmask,-1
	lvx		$outhead,0,$out
	?vperm		$outmask,$rndkey0,$outmask,$outperm
	le?vxor		$outperm,$outperm,$tmp

	srwi		$rounds,$rounds,1
	li		$idx,16
	subi		$rounds,$rounds,1
	beq		Lcbc_dec

Lcbc_enc:
	vmr		$inout,$inptail
	lvx		$inptail,0,$inp
	addi		$inp,$inp,16
	mtctr		$rounds
	subi		$len,$len,16		# len-=16

	lvx		$rndkey0,0,$key
	 vperm		$inout,$inout,$inptail,$inpperm
	lvx		$rndkey1,$idx,$key
	addi		$idx,$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vxor		$inout,$inout,$rndkey0
	lvx		$rndkey0,$idx,$key
	addi		$idx,$idx,16
	vxor		$inout,$inout,$ivec

Loop_cbc_enc:
	?vperm		$rndkey1,$rndkey1,$rndkey0,$keyperm
	vcipher		$inout,$inout,$rndkey1
	lvx		$rndkey1,$idx,$key
	addi		$idx,$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vcipher		$inout,$inout,$rndkey0
	lvx		$rndkey0,$idx,$key
	addi		$idx,$idx,16
	bdnz		Loop_cbc_enc

	?vperm		$rndkey1,$rndkey1,$rndkey0,$keyperm
	vcipher		$inout,$inout,$rndkey1
	lvx		$rndkey1,$idx,$key
	li		$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vcipherlast	$ivec,$inout,$rndkey0
	${UCMP}i	$len,16

	vperm		$tmp,$ivec,$ivec,$outperm
	vsel		$inout,$outhead,$tmp,$outmask
	vmr		$outhead,$tmp
	stvx		$inout,0,$out
	addi		$out,$out,16
	bge		Lcbc_enc

	b		Lcbc_done

.align	4
Lcbc_dec:
	${UCMP}i	$len,128
	bge		_aesp8_cbc_decrypt8x
	vmr		$tmp,$inptail
	lvx		$inptail,0,$inp
	addi		$inp,$inp,16
	mtctr		$rounds
	subi		$len,$len,16		# len-=16

	lvx		$rndkey0,0,$key
	 vperm		$tmp,$tmp,$inptail,$inpperm
	lvx		$rndkey1,$idx,$key
	addi		$idx,$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vxor		$inout,$tmp,$rndkey0
	lvx		$rndkey0,$idx,$key
	addi		$idx,$idx,16

Loop_cbc_dec:
	?vperm		$rndkey1,$rndkey1,$rndkey0,$keyperm
	vncipher	$inout,$inout,$rndkey1
	lvx		$rndkey1,$idx,$key
	addi		$idx,$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vncipher	$inout,$inout,$rndkey0
	lvx		$rndkey0,$idx,$key
	addi		$idx,$idx,16
	bdnz		Loop_cbc_dec

	?vperm		$rndkey1,$rndkey1,$rndkey0,$keyperm
	vncipher	$inout,$inout,$rndkey1
	lvx		$rndkey1,$idx,$key
	li		$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vncipherlast	$inout,$inout,$rndkey0
	${UCMP}i	$len,16

	vxor		$inout,$inout,$ivec
	vmr		$ivec,$tmp
	vperm		$tmp,$inout,$inout,$outperm
	vsel		$inout,$outhead,$tmp,$outmask
	vmr		$outhead,$tmp
	stvx		$inout,0,$out
	addi		$out,$out,16
	bge		Lcbc_dec

Lcbc_done:
	addi		$out,$out,-1
	lvx		$inout,0,$out		# redundant in aligned case
	vsel		$inout,$outhead,$inout,$outmask
	stvx		$inout,0,$out

	neg		$enc,$ivp		# write [unaligned] iv
	li		$idx,15			# 15 is not typo
	vxor		$rndkey0,$rndkey0,$rndkey0
	vspltisb	$outmask,-1
	le?vspltisb	$tmp,0x0f
	?lvsl		$outperm,0,$enc
	?vperm		$outmask,$rndkey0,$outmask,$outperm
	le?vxor		$outperm,$outperm,$tmp
	lvx		$outhead,0,$ivp
	vperm		$ivec,$ivec,$ivec,$outperm
	vsel		$inout,$outhead,$ivec,$outmask
	lvx		$inptail,$idx,$ivp
	stvx		$inout,0,$ivp
	vsel		$inout,$ivec,$inptail,$outmask
	stvx		$inout,$idx,$ivp

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,6,0
	.long		0
___
#########################################################################
{{	# Optimized CBC decrypt procedure				#
my $key_="r11";
my ($x00,$x10,$x20,$x30,$x40,$x50,$x60,$x70)=map("r$_",(0,8,26..31));
my ($in0, $in1, $in2, $in3, $in4, $in5, $in6, $in7 )=map("v$_",(0..3,10..13));
my ($out0,$out1,$out2,$out3,$out4,$out5,$out6,$out7)=map("v$_",(14..21));
my $rndkey0="v23";	# v24-v25 rotating buffer for first found keys
			# v26-v31 last 6 round keys
my ($tmp,$keyperm)=($in3,$in4);	# aliases with "caller", redundant assignment

$code.=<<___;
.align	5
_aesp8_cbc_decrypt8x:
	$STU		$sp,-`($FRAME+21*16+6*$SIZE_T)`($sp)
	li		r10,`$FRAME+8*16+15`
	li		r11,`$FRAME+8*16+31`
	stvx		v20,r10,$sp		# ABI says so
	addi		r10,r10,32
	stvx		v21,r11,$sp
	addi		r11,r11,32
	stvx		v22,r10,$sp
	addi		r10,r10,32
	stvx		v23,r11,$sp
	addi		r11,r11,32
	stvx		v24,r10,$sp
	addi		r10,r10,32
	stvx		v25,r11,$sp
	addi		r11,r11,32
	stvx		v26,r10,$sp
	addi		r10,r10,32
	stvx		v27,r11,$sp
	addi		r11,r11,32
	stvx		v28,r10,$sp
	addi		r10,r10,32
	stvx		v29,r11,$sp
	addi		r11,r11,32
	stvx		v30,r10,$sp
	stvx		v31,r11,$sp
	li		r0,-1
	stw		$vrsave,`$FRAME+21*16-4`($sp)	# save vrsave
	li		$x10,0x10
	$PUSH		r26,`$FRAME+21*16+0*$SIZE_T`($sp)
	li		$x20,0x20
	$PUSH		r27,`$FRAME+21*16+1*$SIZE_T`($sp)
	li		$x30,0x30
	$PUSH		r28,`$FRAME+21*16+2*$SIZE_T`($sp)
	li		$x40,0x40
	$PUSH		r29,`$FRAME+21*16+3*$SIZE_T`($sp)
	li		$x50,0x50
	$PUSH		r30,`$FRAME+21*16+4*$SIZE_T`($sp)
	li		$x60,0x60
	$PUSH		r31,`$FRAME+21*16+5*$SIZE_T`($sp)
	li		$x70,0x70
	mtspr		256,r0

	subi		$rounds,$rounds,3	# -4 in total
	subi		$len,$len,128		# bias

	lvx		$rndkey0,$x00,$key	# load key schedule
	lvx		v30,$x10,$key
	addi		$key,$key,0x20
	lvx		v31,$x00,$key
	?vperm		$rndkey0,$rndkey0,v30,$keyperm
	addi		$key_,$sp,$FRAME+15
	mtctr		$rounds

Load_cbc_dec_key:
	?vperm		v24,v30,v31,$keyperm
	lvx		v30,$x10,$key
	addi		$key,$key,0x20
	stvx		v24,$x00,$key_		# off-load round[1]
	?vperm		v25,v31,v30,$keyperm
	lvx		v31,$x00,$key
	stvx		v25,$x10,$key_		# off-load round[2]
	addi		$key_,$key_,0x20
	bdnz		Load_cbc_dec_key

	lvx		v26,$x10,$key
	?vperm		v24,v30,v31,$keyperm
	lvx		v27,$x20,$key
	stvx		v24,$x00,$key_		# off-load round[3]
	?vperm		v25,v31,v26,$keyperm
	lvx		v28,$x30,$key
	stvx		v25,$x10,$key_		# off-load round[4]
	addi		$key_,$sp,$FRAME+15	# rewind $key_
	?vperm		v26,v26,v27,$keyperm
	lvx		v29,$x40,$key
	?vperm		v27,v27,v28,$keyperm
	lvx		v30,$x50,$key
	?vperm		v28,v28,v29,$keyperm
	lvx		v31,$x60,$key
	?vperm		v29,v29,v30,$keyperm
	lvx		$out0,$x70,$key		# borrow $out0
	?vperm		v30,v30,v31,$keyperm
	lvx		v24,$x00,$key_		# pre-load round[1]
	?vperm		v31,v31,$out0,$keyperm
	lvx		v25,$x10,$key_		# pre-load round[2]

	#lvx		$inptail,0,$inp		# "caller" already did this
	#addi		$inp,$inp,15		# 15 is not typo
	subi		$inp,$inp,15		# undo "caller"

	 le?li		$idx,8
	lvx_u		$in0,$x00,$inp		# load first 8 "words"
	 le?lvsl	$inpperm,0,$idx
	 le?vspltisb	$tmp,0x0f
	lvx_u		$in1,$x10,$inp
	 le?vxor	$inpperm,$inpperm,$tmp	# transform for lvx_u/stvx_u
	lvx_u		$in2,$x20,$inp
	 le?vperm	$in0,$in0,$in0,$inpperm
	lvx_u		$in3,$x30,$inp
	 le?vperm	$in1,$in1,$in1,$inpperm
	lvx_u		$in4,$x40,$inp
	 le?vperm	$in2,$in2,$in2,$inpperm
	vxor		$out0,$in0,$rndkey0
	lvx_u		$in5,$x50,$inp
	 le?vperm	$in3,$in3,$in3,$inpperm
	vxor		$out1,$in1,$rndkey0
	lvx_u		$in6,$x60,$inp
	 le?vperm	$in4,$in4,$in4,$inpperm
	vxor		$out2,$in2,$rndkey0
	lvx_u		$in7,$x70,$inp
	addi		$inp,$inp,0x80
	 le?vperm	$in5,$in5,$in5,$inpperm
	vxor		$out3,$in3,$rndkey0
	 le?vperm	$in6,$in6,$in6,$inpperm
	vxor		$out4,$in4,$rndkey0
	 le?vperm	$in7,$in7,$in7,$inpperm
	vxor		$out5,$in5,$rndkey0
	vxor		$out6,$in6,$rndkey0
	vxor		$out7,$in7,$rndkey0

	mtctr		$rounds
	b		Loop_cbc_dec8x
.align	5
Loop_cbc_dec8x:
	vncipher	$out0,$out0,v24
	vncipher	$out1,$out1,v24
	vncipher	$out2,$out2,v24
	vncipher	$out3,$out3,v24
	vncipher	$out4,$out4,v24
	vncipher	$out5,$out5,v24
	vncipher	$out6,$out6,v24
	vncipher	$out7,$out7,v24
	lvx		v24,$x20,$key_		# round[3]
	addi		$key_,$key_,0x20

	vncipher	$out0,$out0,v25
	vncipher	$out1,$out1,v25
	vncipher	$out2,$out2,v25
	vncipher	$out3,$out3,v25
	vncipher	$out4,$out4,v25
	vncipher	$out5,$out5,v25
	vncipher	$out6,$out6,v25
	vncipher	$out7,$out7,v25
	lvx		v25,$x10,$key_		# round[4]
	bdnz		Loop_cbc_dec8x

	subic		$len,$len,128		# $len-=128
	vncipher	$out0,$out0,v24
	vncipher	$out1,$out1,v24
	vncipher	$out2,$out2,v24
	vncipher	$out3,$out3,v24
	vncipher	$out4,$out4,v24
	vncipher	$out5,$out5,v24
	vncipher	$out6,$out6,v24
	vncipher	$out7,$out7,v24

	subfe.		r0,r0,r0		# borrow?-1:0
	vncipher	$out0,$out0,v25
	vncipher	$out1,$out1,v25
	vncipher	$out2,$out2,v25
	vncipher	$out3,$out3,v25
	vncipher	$out4,$out4,v25
	vncipher	$out5,$out5,v25
	vncipher	$out6,$out6,v25
	vncipher	$out7,$out7,v25

	and		r0,r0,$len
	vncipher	$out0,$out0,v26
	vncipher	$out1,$out1,v26
	vncipher	$out2,$out2,v26
	vncipher	$out3,$out3,v26
	vncipher	$out4,$out4,v26
	vncipher	$out5,$out5,v26
	vncipher	$out6,$out6,v26
	vncipher	$out7,$out7,v26

	add		$inp,$inp,r0		# $inp is adjusted in such
						# way that at exit from the
						# loop inX-in7 are loaded
						# with last "words"
	vncipher	$out0,$out0,v27
	vncipher	$out1,$out1,v27
	vncipher	$out2,$out2,v27
	vncipher	$out3,$out3,v27
	vncipher	$out4,$out4,v27
	vncipher	$out5,$out5,v27
	vncipher	$out6,$out6,v27
	vncipher	$out7,$out7,v27

	addi		$key_,$sp,$FRAME+15	# rewind $key_
	vncipher	$out0,$out0,v28
	vncipher	$out1,$out1,v28
	vncipher	$out2,$out2,v28
	vncipher	$out3,$out3,v28
	vncipher	$out4,$out4,v28
	vncipher	$out5,$out5,v28
	vncipher	$out6,$out6,v28
	vncipher	$out7,$out7,v28
	lvx		v24,$x00,$key_		# re-pre-load round[1]

	vncipher	$out0,$out0,v29
	vncipher	$out1,$out1,v29
	vncipher	$out2,$out2,v29
	vncipher	$out3,$out3,v29
	vncipher	$out4,$out4,v29
	vncipher	$out5,$out5,v29
	vncipher	$out6,$out6,v29
	vncipher	$out7,$out7,v29
	lvx		v25,$x10,$key_		# re-pre-load round[2]

	vncipher	$out0,$out0,v30
	 vxor		$ivec,$ivec,v31		# xor with last round key
	vncipher	$out1,$out1,v30
	 vxor		$in0,$in0,v31
	vncipher	$out2,$out2,v30
	 vxor		$in1,$in1,v31
	vncipher	$out3,$out3,v30
	 vxor		$in2,$in2,v31
	vncipher	$out4,$out4,v30
	 vxor		$in3,$in3,v31
	vncipher	$out5,$out5,v30
	 vxor		$in4,$in4,v31
	vncipher	$out6,$out6,v30
	 vxor		$in5,$in5,v31
	vncipher	$out7,$out7,v30
	 vxor		$in6,$in6,v31

	vncipherlast	$out0,$out0,$ivec
	vncipherlast	$out1,$out1,$in0
	 lvx_u		$in0,$x00,$inp		# load next input block
	vncipherlast	$out2,$out2,$in1
	 lvx_u		$in1,$x10,$inp
	vncipherlast	$out3,$out3,$in2
	 le?vperm	$in0,$in0,$in0,$inpperm
	 lvx_u		$in2,$x20,$inp
	vncipherlast	$out4,$out4,$in3
	 le?vperm	$in1,$in1,$in1,$inpperm
	 lvx_u		$in3,$x30,$inp
	vncipherlast	$out5,$out5,$in4
	 le?vperm	$in2,$in2,$in2,$inpperm
	 lvx_u		$in4,$x40,$inp
	vncipherlast	$out6,$out6,$in5
	 le?vperm	$in3,$in3,$in3,$inpperm
	 lvx_u		$in5,$x50,$inp
	vncipherlast	$out7,$out7,$in6
	 le?vperm	$in4,$in4,$in4,$inpperm
	 lvx_u		$in6,$x60,$inp
	vmr		$ivec,$in7
	 le?vperm	$in5,$in5,$in5,$inpperm
	 lvx_u		$in7,$x70,$inp
	 addi		$inp,$inp,0x80

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	 le?vperm	$in6,$in6,$in6,$inpperm
	 vxor		$out0,$in0,$rndkey0
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	 le?vperm	$in7,$in7,$in7,$inpperm
	 vxor		$out1,$in1,$rndkey0
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x20,$out
	 vxor		$out2,$in2,$rndkey0
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x30,$out
	 vxor		$out3,$in3,$rndkey0
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x40,$out
	 vxor		$out4,$in4,$rndkey0
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x50,$out
	 vxor		$out5,$in5,$rndkey0
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x60,$out
	 vxor		$out6,$in6,$rndkey0
	stvx_u		$out7,$x70,$out
	addi		$out,$out,0x80
	 vxor		$out7,$in7,$rndkey0

	mtctr		$rounds
	beq		Loop_cbc_dec8x		# did $len-=128 borrow?

	addic.		$len,$len,128
	beq		Lcbc_dec8x_done
	nop
	nop

Loop_cbc_dec8x_tail:				# up to 7 "words" tail...
	vncipher	$out1,$out1,v24
	vncipher	$out2,$out2,v24
	vncipher	$out3,$out3,v24
	vncipher	$out4,$out4,v24
	vncipher	$out5,$out5,v24
	vncipher	$out6,$out6,v24
	vncipher	$out7,$out7,v24
	lvx		v24,$x20,$key_		# round[3]
	addi		$key_,$key_,0x20

	vncipher	$out1,$out1,v25
	vncipher	$out2,$out2,v25
	vncipher	$out3,$out3,v25
	vncipher	$out4,$out4,v25
	vncipher	$out5,$out5,v25
	vncipher	$out6,$out6,v25
	vncipher	$out7,$out7,v25
	lvx		v25,$x10,$key_		# round[4]
	bdnz		Loop_cbc_dec8x_tail

	vncipher	$out1,$out1,v24
	vncipher	$out2,$out2,v24
	vncipher	$out3,$out3,v24
	vncipher	$out4,$out4,v24
	vncipher	$out5,$out5,v24
	vncipher	$out6,$out6,v24
	vncipher	$out7,$out7,v24

	vncipher	$out1,$out1,v25
	vncipher	$out2,$out2,v25
	vncipher	$out3,$out3,v25
	vncipher	$out4,$out4,v25
	vncipher	$out5,$out5,v25
	vncipher	$out6,$out6,v25
	vncipher	$out7,$out7,v25

	vncipher	$out1,$out1,v26
	vncipher	$out2,$out2,v26
	vncipher	$out3,$out3,v26
	vncipher	$out4,$out4,v26
	vncipher	$out5,$out5,v26
	vncipher	$out6,$out6,v26
	vncipher	$out7,$out7,v26

	vncipher	$out1,$out1,v27
	vncipher	$out2,$out2,v27
	vncipher	$out3,$out3,v27
	vncipher	$out4,$out4,v27
	vncipher	$out5,$out5,v27
	vncipher	$out6,$out6,v27
	vncipher	$out7,$out7,v27

	vncipher	$out1,$out1,v28
	vncipher	$out2,$out2,v28
	vncipher	$out3,$out3,v28
	vncipher	$out4,$out4,v28
	vncipher	$out5,$out5,v28
	vncipher	$out6,$out6,v28
	vncipher	$out7,$out7,v28

	vncipher	$out1,$out1,v29
	vncipher	$out2,$out2,v29
	vncipher	$out3,$out3,v29
	vncipher	$out4,$out4,v29
	vncipher	$out5,$out5,v29
	vncipher	$out6,$out6,v29
	vncipher	$out7,$out7,v29

	vncipher	$out1,$out1,v30
	 vxor		$ivec,$ivec,v31		# last round key
	vncipher	$out2,$out2,v30
	 vxor		$in1,$in1,v31
	vncipher	$out3,$out3,v30
	 vxor		$in2,$in2,v31
	vncipher	$out4,$out4,v30
	 vxor		$in3,$in3,v31
	vncipher	$out5,$out5,v30
	 vxor		$in4,$in4,v31
	vncipher	$out6,$out6,v30
	 vxor		$in5,$in5,v31
	vncipher	$out7,$out7,v30
	 vxor		$in6,$in6,v31

	cmplwi		$len,32			# switch($len)
	blt		Lcbc_dec8x_one
	nop
	beq		Lcbc_dec8x_two
	cmplwi		$len,64
	blt		Lcbc_dec8x_three
	nop
	beq		Lcbc_dec8x_four
	cmplwi		$len,96
	blt		Lcbc_dec8x_five
	nop
	beq		Lcbc_dec8x_six

Lcbc_dec8x_seven:
	vncipherlast	$out1,$out1,$ivec
	vncipherlast	$out2,$out2,$in1
	vncipherlast	$out3,$out3,$in2
	vncipherlast	$out4,$out4,$in3
	vncipherlast	$out5,$out5,$in4
	vncipherlast	$out6,$out6,$in5
	vncipherlast	$out7,$out7,$in6
	vmr		$ivec,$in7

	le?vperm	$out1,$out1,$out1,$inpperm
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x00,$out
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x10,$out
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x20,$out
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x30,$out
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x40,$out
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x50,$out
	stvx_u		$out7,$x60,$out
	addi		$out,$out,0x70
	b		Lcbc_dec8x_done

.align	5
Lcbc_dec8x_six:
	vncipherlast	$out2,$out2,$ivec
	vncipherlast	$out3,$out3,$in2
	vncipherlast	$out4,$out4,$in3
	vncipherlast	$out5,$out5,$in4
	vncipherlast	$out6,$out6,$in5
	vncipherlast	$out7,$out7,$in6
	vmr		$ivec,$in7

	le?vperm	$out2,$out2,$out2,$inpperm
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x00,$out
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x10,$out
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x20,$out
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x30,$out
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x40,$out
	stvx_u		$out7,$x50,$out
	addi		$out,$out,0x60
	b		Lcbc_dec8x_done

.align	5
Lcbc_dec8x_five:
	vncipherlast	$out3,$out3,$ivec
	vncipherlast	$out4,$out4,$in3
	vncipherlast	$out5,$out5,$in4
	vncipherlast	$out6,$out6,$in5
	vncipherlast	$out7,$out7,$in6
	vmr		$ivec,$in7

	le?vperm	$out3,$out3,$out3,$inpperm
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x00,$out
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x10,$out
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x20,$out
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x30,$out
	stvx_u		$out7,$x40,$out
	addi		$out,$out,0x50
	b		Lcbc_dec8x_done

.align	5
Lcbc_dec8x_four:
	vncipherlast	$out4,$out4,$ivec
	vncipherlast	$out5,$out5,$in4
	vncipherlast	$out6,$out6,$in5
	vncipherlast	$out7,$out7,$in6
	vmr		$ivec,$in7

	le?vperm	$out4,$out4,$out4,$inpperm
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x00,$out
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x10,$out
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x20,$out
	stvx_u		$out7,$x30,$out
	addi		$out,$out,0x40
	b		Lcbc_dec8x_done

.align	5
Lcbc_dec8x_three:
	vncipherlast	$out5,$out5,$ivec
	vncipherlast	$out6,$out6,$in5
	vncipherlast	$out7,$out7,$in6
	vmr		$ivec,$in7

	le?vperm	$out5,$out5,$out5,$inpperm
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x00,$out
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x10,$out
	stvx_u		$out7,$x20,$out
	addi		$out,$out,0x30
	b		Lcbc_dec8x_done

.align	5
Lcbc_dec8x_two:
	vncipherlast	$out6,$out6,$ivec
	vncipherlast	$out7,$out7,$in6
	vmr		$ivec,$in7

	le?vperm	$out6,$out6,$out6,$inpperm
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x00,$out
	stvx_u		$out7,$x10,$out
	addi		$out,$out,0x20
	b		Lcbc_dec8x_done

.align	5
Lcbc_dec8x_one:
	vncipherlast	$out7,$out7,$ivec
	vmr		$ivec,$in7

	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out7,0,$out
	addi		$out,$out,0x10

Lcbc_dec8x_done:
	le?vperm	$ivec,$ivec,$ivec,$inpperm
	stvx_u		$ivec,0,$ivp		# write [unaligned] iv

	li		r10,`$FRAME+15`
	li		r11,`$FRAME+31`
	stvx		$inpperm,r10,$sp	# wipe copies of round keys
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32
	stvx		$inpperm,r10,$sp
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32
	stvx		$inpperm,r10,$sp
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32
	stvx		$inpperm,r10,$sp
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32

	mtspr		256,$vrsave
	lvx		v20,r10,$sp		# ABI says so
	addi		r10,r10,32
	lvx		v21,r11,$sp
	addi		r11,r11,32
	lvx		v22,r10,$sp
	addi		r10,r10,32
	lvx		v23,r11,$sp
	addi		r11,r11,32
	lvx		v24,r10,$sp
	addi		r10,r10,32
	lvx		v25,r11,$sp
	addi		r11,r11,32
	lvx		v26,r10,$sp
	addi		r10,r10,32
	lvx		v27,r11,$sp
	addi		r11,r11,32
	lvx		v28,r10,$sp
	addi		r10,r10,32
	lvx		v29,r11,$sp
	addi		r11,r11,32
	lvx		v30,r10,$sp
	lvx		v31,r11,$sp
	$POP		r26,`$FRAME+21*16+0*$SIZE_T`($sp)
	$POP		r27,`$FRAME+21*16+1*$SIZE_T`($sp)
	$POP		r28,`$FRAME+21*16+2*$SIZE_T`($sp)
	$POP		r29,`$FRAME+21*16+3*$SIZE_T`($sp)
	$POP		r30,`$FRAME+21*16+4*$SIZE_T`($sp)
	$POP		r31,`$FRAME+21*16+5*$SIZE_T`($sp)
	addi		$sp,$sp,`$FRAME+21*16+6*$SIZE_T`
	blr
	.long		0
	.byte		0,12,0x14,0,0x80,6,6,0
	.long		0
.size	.${prefix}_cbc_encrypt,.-.${prefix}_cbc_encrypt
___
}}	}}}

#########################################################################
{{{	# CTR procedure[s]						#
my ($inp,$out,$len,$key,$ivp,$x10,$rounds,$idx)=map("r$_",(3..10));
my ($rndkey0,$rndkey1,$inout,$tmp)=		map("v$_",(0..3));
my ($ivec,$inptail,$inpperm,$outhead,$outperm,$outmask,$keyperm,$one)=
						map("v$_",(4..11));
my $dat=$tmp;

$code.=<<___;
.globl	.${prefix}_ctr32_encrypt_blocks
	${UCMP}i	$len,1
	bltlr-

	lis		r0,0xfff0
	mfspr		$vrsave,256
	mtspr		256,r0

	li		$idx,15
	vxor		$rndkey0,$rndkey0,$rndkey0
	le?vspltisb	$tmp,0x0f

	lvx		$ivec,0,$ivp		# load [unaligned] iv
	lvsl		$inpperm,0,$ivp
	lvx		$inptail,$idx,$ivp
	 vspltisb	$one,1
	le?vxor		$inpperm,$inpperm,$tmp
	vperm		$ivec,$ivec,$inptail,$inpperm
	 vsldoi		$one,$rndkey0,$one,1

	neg		r11,$inp
	?lvsl		$keyperm,0,$key		# prepare for unaligned key
	lwz		$rounds,240($key)

	lvsr		$inpperm,0,r11		# prepare for unaligned load
	lvx		$inptail,0,$inp
	addi		$inp,$inp,15		# 15 is not typo
	le?vxor		$inpperm,$inpperm,$tmp

	srwi		$rounds,$rounds,1
	li		$idx,16
	subi		$rounds,$rounds,1

	${UCMP}i	$len,8
	bge		_aesp8_ctr32_encrypt8x

	?lvsr		$outperm,0,$out		# prepare for unaligned store
	vspltisb	$outmask,-1
	lvx		$outhead,0,$out
	?vperm		$outmask,$rndkey0,$outmask,$outperm
	le?vxor		$outperm,$outperm,$tmp

	lvx		$rndkey0,0,$key
	mtctr		$rounds
	lvx		$rndkey1,$idx,$key
	addi		$idx,$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vxor		$inout,$ivec,$rndkey0
	lvx		$rndkey0,$idx,$key
	addi		$idx,$idx,16
	b		Loop_ctr32_enc

.align	5
Loop_ctr32_enc:
	?vperm		$rndkey1,$rndkey1,$rndkey0,$keyperm
	vcipher		$inout,$inout,$rndkey1
	lvx		$rndkey1,$idx,$key
	addi		$idx,$idx,16
	?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vcipher		$inout,$inout,$rndkey0
	lvx		$rndkey0,$idx,$key
	addi		$idx,$idx,16
	bdnz		Loop_ctr32_enc

	vadduwm		$ivec,$ivec,$one
	 vmr		$dat,$inptail
	 lvx		$inptail,0,$inp
	 addi		$inp,$inp,16
	 subic.		$len,$len,1		# blocks--

	?vperm		$rndkey1,$rndkey1,$rndkey0,$keyperm
	vcipher		$inout,$inout,$rndkey1
	lvx		$rndkey1,$idx,$key
	 vperm		$dat,$dat,$inptail,$inpperm
	 li		$idx,16
	?vperm		$rndkey1,$rndkey0,$rndkey1,$keyperm
	 lvx		$rndkey0,0,$key
	vxor		$dat,$dat,$rndkey1	# last round key
	vcipherlast	$inout,$inout,$dat

	 lvx		$rndkey1,$idx,$key
	 addi		$idx,$idx,16
	vperm		$inout,$inout,$inout,$outperm
	vsel		$dat,$outhead,$inout,$outmask
	 mtctr		$rounds
	 ?vperm		$rndkey0,$rndkey0,$rndkey1,$keyperm
	vmr		$outhead,$inout
	 vxor		$inout,$ivec,$rndkey0
	 lvx		$rndkey0,$idx,$key
	 addi		$idx,$idx,16
	stvx		$dat,0,$out
	addi		$out,$out,16
	bne		Loop_ctr32_enc

	addi		$out,$out,-1
	lvx		$inout,0,$out		# redundant in aligned case
	vsel		$inout,$outhead,$inout,$outmask
	stvx		$inout,0,$out

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,6,0
	.long		0
___
#########################################################################
{{	# Optimized CTR procedure					#
my $key_="r11";
my ($x00,$x10,$x20,$x30,$x40,$x50,$x60,$x70)=map("r$_",(0,8,26..31));
my ($in0, $in1, $in2, $in3, $in4, $in5, $in6, $in7 )=map("v$_",(0..3,10,12..14));
my ($out0,$out1,$out2,$out3,$out4,$out5,$out6,$out7)=map("v$_",(15..22));
my $rndkey0="v23";	# v24-v25 rotating buffer for first found keys
			# v26-v31 last 6 round keys
my ($tmp,$keyperm)=($in3,$in4);	# aliases with "caller", redundant assignment
my ($two,$three,$four)=($outhead,$outperm,$outmask);

$code.=<<___;
.align	5
_aesp8_ctr32_encrypt8x:
	$STU		$sp,-`($FRAME+21*16+6*$SIZE_T)`($sp)
	li		r10,`$FRAME+8*16+15`
	li		r11,`$FRAME+8*16+31`
	stvx		v20,r10,$sp		# ABI says so
	addi		r10,r10,32
	stvx		v21,r11,$sp
	addi		r11,r11,32
	stvx		v22,r10,$sp
	addi		r10,r10,32
	stvx		v23,r11,$sp
	addi		r11,r11,32
	stvx		v24,r10,$sp
	addi		r10,r10,32
	stvx		v25,r11,$sp
	addi		r11,r11,32
	stvx		v26,r10,$sp
	addi		r10,r10,32
	stvx		v27,r11,$sp
	addi		r11,r11,32
	stvx		v28,r10,$sp
	addi		r10,r10,32
	stvx		v29,r11,$sp
	addi		r11,r11,32
	stvx		v30,r10,$sp
	stvx		v31,r11,$sp
	li		r0,-1
	stw		$vrsave,`$FRAME+21*16-4`($sp)	# save vrsave
	li		$x10,0x10
	$PUSH		r26,`$FRAME+21*16+0*$SIZE_T`($sp)
	li		$x20,0x20
	$PUSH		r27,`$FRAME+21*16+1*$SIZE_T`($sp)
	li		$x30,0x30
	$PUSH		r28,`$FRAME+21*16+2*$SIZE_T`($sp)
	li		$x40,0x40
	$PUSH		r29,`$FRAME+21*16+3*$SIZE_T`($sp)
	li		$x50,0x50
	$PUSH		r30,`$FRAME+21*16+4*$SIZE_T`($sp)
	li		$x60,0x60
	$PUSH		r31,`$FRAME+21*16+5*$SIZE_T`($sp)
	li		$x70,0x70
	mtspr		256,r0

	subi		$rounds,$rounds,3	# -4 in total

	lvx		$rndkey0,$x00,$key	# load key schedule
	lvx		v30,$x10,$key
	addi		$key,$key,0x20
	lvx		v31,$x00,$key
	?vperm		$rndkey0,$rndkey0,v30,$keyperm
	addi		$key_,$sp,$FRAME+15
	mtctr		$rounds

Load_ctr32_enc_key:
	?vperm		v24,v30,v31,$keyperm
	lvx		v30,$x10,$key
	addi		$key,$key,0x20
	stvx		v24,$x00,$key_		# off-load round[1]
	?vperm		v25,v31,v30,$keyperm
	lvx		v31,$x00,$key
	stvx		v25,$x10,$key_		# off-load round[2]
	addi		$key_,$key_,0x20
	bdnz		Load_ctr32_enc_key

	lvx		v26,$x10,$key
	?vperm		v24,v30,v31,$keyperm
	lvx		v27,$x20,$key
	stvx		v24,$x00,$key_		# off-load round[3]
	?vperm		v25,v31,v26,$keyperm
	lvx		v28,$x30,$key
	stvx		v25,$x10,$key_		# off-load round[4]
	addi		$key_,$sp,$FRAME+15	# rewind $key_
	?vperm		v26,v26,v27,$keyperm
	lvx		v29,$x40,$key
	?vperm		v27,v27,v28,$keyperm
	lvx		v30,$x50,$key
	?vperm		v28,v28,v29,$keyperm
	lvx		v31,$x60,$key
	?vperm		v29,v29,v30,$keyperm
	lvx		$out0,$x70,$key		# borrow $out0
	?vperm		v30,v30,v31,$keyperm
	lvx		v24,$x00,$key_		# pre-load round[1]
	?vperm		v31,v31,$out0,$keyperm
	lvx		v25,$x10,$key_		# pre-load round[2]

	vadduwm		$two,$one,$one
	subi		$inp,$inp,15		# undo "caller"
	$SHL		$len,$len,4

	vadduwm		$out1,$ivec,$one	# counter values ...
	vadduwm		$out2,$ivec,$two
	vxor		$out0,$ivec,$rndkey0	# ... xored with rndkey[0]
	 le?li		$idx,8
	vadduwm		$out3,$out1,$two
	vxor		$out1,$out1,$rndkey0
	 le?lvsl	$inpperm,0,$idx
	vadduwm		$out4,$out2,$two
	vxor		$out2,$out2,$rndkey0
	 le?vspltisb	$tmp,0x0f
	vadduwm		$out5,$out3,$two
	vxor		$out3,$out3,$rndkey0
	 le?vxor	$inpperm,$inpperm,$tmp	# transform for lvx_u/stvx_u
	vadduwm		$out6,$out4,$two
	vxor		$out4,$out4,$rndkey0
	vadduwm		$out7,$out5,$two
	vxor		$out5,$out5,$rndkey0
	vadduwm		$ivec,$out6,$two	# next counter value
	vxor		$out6,$out6,$rndkey0
	vxor		$out7,$out7,$rndkey0

	mtctr		$rounds
	b		Loop_ctr32_enc8x
.align	5
Loop_ctr32_enc8x:
	vcipher 	$out0,$out0,v24
	vcipher 	$out1,$out1,v24
	vcipher 	$out2,$out2,v24
	vcipher 	$out3,$out3,v24
	vcipher 	$out4,$out4,v24
	vcipher 	$out5,$out5,v24
	vcipher 	$out6,$out6,v24
	vcipher 	$out7,$out7,v24
Loop_ctr32_enc8x_middle:
	lvx		v24,$x20,$key_		# round[3]
	addi		$key_,$key_,0x20

	vcipher 	$out0,$out0,v25
	vcipher 	$out1,$out1,v25
	vcipher 	$out2,$out2,v25
	vcipher 	$out3,$out3,v25
	vcipher 	$out4,$out4,v25
	vcipher 	$out5,$out5,v25
	vcipher 	$out6,$out6,v25
	vcipher 	$out7,$out7,v25
	lvx		v25,$x10,$key_		# round[4]
	bdnz		Loop_ctr32_enc8x

	subic		r11,$len,256		# $len-256, borrow $key_
	vcipher 	$out0,$out0,v24
	vcipher 	$out1,$out1,v24
	vcipher 	$out2,$out2,v24
	vcipher 	$out3,$out3,v24
	vcipher 	$out4,$out4,v24
	vcipher 	$out5,$out5,v24
	vcipher 	$out6,$out6,v24
	vcipher 	$out7,$out7,v24

	subfe		r0,r0,r0		# borrow?-1:0
	vcipher 	$out0,$out0,v25
	vcipher 	$out1,$out1,v25
	vcipher 	$out2,$out2,v25
	vcipher 	$out3,$out3,v25
	vcipher 	$out4,$out4,v25
	vcipher		$out5,$out5,v25
	vcipher		$out6,$out6,v25
	vcipher		$out7,$out7,v25

	and		r0,r0,r11
	addi		$key_,$sp,$FRAME+15	# rewind $key_
	vcipher		$out0,$out0,v26
	vcipher		$out1,$out1,v26
	vcipher		$out2,$out2,v26
	vcipher		$out3,$out3,v26
	vcipher		$out4,$out4,v26
	vcipher		$out5,$out5,v26
	vcipher		$out6,$out6,v26
	vcipher		$out7,$out7,v26
	lvx		v24,$x00,$key_		# re-pre-load round[1]

	subic		$len,$len,129		# $len-=129
	vcipher		$out0,$out0,v27
	addi		$len,$len,1		# $len-=128 really
	vcipher		$out1,$out1,v27
	vcipher		$out2,$out2,v27
	vcipher		$out3,$out3,v27
	vcipher		$out4,$out4,v27
	vcipher		$out5,$out5,v27
	vcipher		$out6,$out6,v27
	vcipher		$out7,$out7,v27
	lvx		v25,$x10,$key_		# re-pre-load round[2]

	vcipher		$out0,$out0,v28
	 lvx_u		$in0,$x00,$inp		# load input
	vcipher		$out1,$out1,v28
	 lvx_u		$in1,$x10,$inp
	vcipher		$out2,$out2,v28
	 lvx_u		$in2,$x20,$inp
	vcipher		$out3,$out3,v28
	 lvx_u		$in3,$x30,$inp
	vcipher		$out4,$out4,v28
	 lvx_u		$in4,$x40,$inp
	vcipher		$out5,$out5,v28
	 lvx_u		$in5,$x50,$inp
	vcipher		$out6,$out6,v28
	 lvx_u		$in6,$x60,$inp
	vcipher		$out7,$out7,v28
	 lvx_u		$in7,$x70,$inp
	 addi		$inp,$inp,0x80

	vcipher		$out0,$out0,v29
	 le?vperm	$in0,$in0,$in0,$inpperm
	vcipher		$out1,$out1,v29
	 le?vperm	$in1,$in1,$in1,$inpperm
	vcipher		$out2,$out2,v29
	 le?vperm	$in2,$in2,$in2,$inpperm
	vcipher		$out3,$out3,v29
	 le?vperm	$in3,$in3,$in3,$inpperm
	vcipher		$out4,$out4,v29
	 le?vperm	$in4,$in4,$in4,$inpperm
	vcipher		$out5,$out5,v29
	 le?vperm	$in5,$in5,$in5,$inpperm
	vcipher		$out6,$out6,v29
	 le?vperm	$in6,$in6,$in6,$inpperm
	vcipher		$out7,$out7,v29
	 le?vperm	$in7,$in7,$in7,$inpperm

	add		$inp,$inp,r0		# $inp is adjusted in such
						# way that at exit from the
						# loop inX-in7 are loaded
						# with last "words"
	subfe.		r0,r0,r0		# borrow?-1:0
	vcipher		$out0,$out0,v30
	 vxor		$in0,$in0,v31		# xor with last round key
	vcipher		$out1,$out1,v30
	 vxor		$in1,$in1,v31
	vcipher		$out2,$out2,v30
	 vxor		$in2,$in2,v31
	vcipher		$out3,$out3,v30
	 vxor		$in3,$in3,v31
	vcipher		$out4,$out4,v30
	 vxor		$in4,$in4,v31
	vcipher		$out5,$out5,v30
	 vxor		$in5,$in5,v31
	vcipher		$out6,$out6,v30
	 vxor		$in6,$in6,v31
	vcipher		$out7,$out7,v30
	 vxor		$in7,$in7,v31

	bne		Lctr32_enc8x_break	# did $len-129 borrow?

	vcipherlast	$in0,$out0,$in0
	vcipherlast	$in1,$out1,$in1
	 vadduwm	$out1,$ivec,$one	# counter values ...
	vcipherlast	$in2,$out2,$in2
	 vadduwm	$out2,$ivec,$two
	 vxor		$out0,$ivec,$rndkey0	# ... xored with rndkey[0]
	vcipherlast	$in3,$out3,$in3
	 vadduwm	$out3,$out1,$two
	 vxor		$out1,$out1,$rndkey0
	vcipherlast	$in4,$out4,$in4
	 vadduwm	$out4,$out2,$two
	 vxor		$out2,$out2,$rndkey0
	vcipherlast	$in5,$out5,$in5
	 vadduwm	$out5,$out3,$two
	 vxor		$out3,$out3,$rndkey0
	vcipherlast	$in6,$out6,$in6
	 vadduwm	$out6,$out4,$two
	 vxor		$out4,$out4,$rndkey0
	vcipherlast	$in7,$out7,$in7
	 vadduwm	$out7,$out5,$two
	 vxor		$out5,$out5,$rndkey0
	le?vperm	$in0,$in0,$in0,$inpperm
	 vadduwm	$ivec,$out6,$two	# next counter value
	 vxor		$out6,$out6,$rndkey0
	le?vperm	$in1,$in1,$in1,$inpperm
	 vxor		$out7,$out7,$rndkey0
	mtctr		$rounds

	 vcipher	$out0,$out0,v24
	stvx_u		$in0,$x00,$out
	le?vperm	$in2,$in2,$in2,$inpperm
	 vcipher	$out1,$out1,v24
	stvx_u		$in1,$x10,$out
	le?vperm	$in3,$in3,$in3,$inpperm
	 vcipher	$out2,$out2,v24
	stvx_u		$in2,$x20,$out
	le?vperm	$in4,$in4,$in4,$inpperm
	 vcipher	$out3,$out3,v24
	stvx_u		$in3,$x30,$out
	le?vperm	$in5,$in5,$in5,$inpperm
	 vcipher	$out4,$out4,v24
	stvx_u		$in4,$x40,$out
	le?vperm	$in6,$in6,$in6,$inpperm
	 vcipher	$out5,$out5,v24
	stvx_u		$in5,$x50,$out
	le?vperm	$in7,$in7,$in7,$inpperm
	 vcipher	$out6,$out6,v24
	stvx_u		$in6,$x60,$out
	 vcipher	$out7,$out7,v24
	stvx_u		$in7,$x70,$out
	addi		$out,$out,0x80

	b		Loop_ctr32_enc8x_middle

.align	5
Lctr32_enc8x_break:
	cmpwi		$len,-0x60
	blt		Lctr32_enc8x_one
	nop
	beq		Lctr32_enc8x_two
	cmpwi		$len,-0x40
	blt		Lctr32_enc8x_three
	nop
	beq		Lctr32_enc8x_four
	cmpwi		$len,-0x20
	blt		Lctr32_enc8x_five
	nop
	beq		Lctr32_enc8x_six
	cmpwi		$len,0x00
	blt		Lctr32_enc8x_seven

Lctr32_enc8x_eight:
	vcipherlast	$out0,$out0,$in0
	vcipherlast	$out1,$out1,$in1
	vcipherlast	$out2,$out2,$in2
	vcipherlast	$out3,$out3,$in3
	vcipherlast	$out4,$out4,$in4
	vcipherlast	$out5,$out5,$in5
	vcipherlast	$out6,$out6,$in6
	vcipherlast	$out7,$out7,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x20,$out
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x30,$out
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x40,$out
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x50,$out
	le?vperm	$out7,$out7,$out7,$inpperm
	stvx_u		$out6,$x60,$out
	stvx_u		$out7,$x70,$out
	addi		$out,$out,0x80
	b		Lctr32_enc8x_done

.align	5
Lctr32_enc8x_seven:
	vcipherlast	$out0,$out0,$in1
	vcipherlast	$out1,$out1,$in2
	vcipherlast	$out2,$out2,$in3
	vcipherlast	$out3,$out3,$in4
	vcipherlast	$out4,$out4,$in5
	vcipherlast	$out5,$out5,$in6
	vcipherlast	$out6,$out6,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x20,$out
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x30,$out
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x40,$out
	le?vperm	$out6,$out6,$out6,$inpperm
	stvx_u		$out5,$x50,$out
	stvx_u		$out6,$x60,$out
	addi		$out,$out,0x70
	b		Lctr32_enc8x_done

.align	5
Lctr32_enc8x_six:
	vcipherlast	$out0,$out0,$in2
	vcipherlast	$out1,$out1,$in3
	vcipherlast	$out2,$out2,$in4
	vcipherlast	$out3,$out3,$in5
	vcipherlast	$out4,$out4,$in6
	vcipherlast	$out5,$out5,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x20,$out
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x30,$out
	le?vperm	$out5,$out5,$out5,$inpperm
	stvx_u		$out4,$x40,$out
	stvx_u		$out5,$x50,$out
	addi		$out,$out,0x60
	b		Lctr32_enc8x_done

.align	5
Lctr32_enc8x_five:
	vcipherlast	$out0,$out0,$in3
	vcipherlast	$out1,$out1,$in4
	vcipherlast	$out2,$out2,$in5
	vcipherlast	$out3,$out3,$in6
	vcipherlast	$out4,$out4,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x20,$out
	le?vperm	$out4,$out4,$out4,$inpperm
	stvx_u		$out3,$x30,$out
	stvx_u		$out4,$x40,$out
	addi		$out,$out,0x50
	b		Lctr32_enc8x_done

.align	5
Lctr32_enc8x_four:
	vcipherlast	$out0,$out0,$in4
	vcipherlast	$out1,$out1,$in5
	vcipherlast	$out2,$out2,$in6
	vcipherlast	$out3,$out3,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	le?vperm	$out3,$out3,$out3,$inpperm
	stvx_u		$out2,$x20,$out
	stvx_u		$out3,$x30,$out
	addi		$out,$out,0x40
	b		Lctr32_enc8x_done

.align	5
Lctr32_enc8x_three:
	vcipherlast	$out0,$out0,$in5
	vcipherlast	$out1,$out1,$in6
	vcipherlast	$out2,$out2,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	le?vperm	$out2,$out2,$out2,$inpperm
	stvx_u		$out1,$x10,$out
	stvx_u		$out2,$x20,$out
	addi		$out,$out,0x30
	b		Lcbc_dec8x_done

.align	5
Lctr32_enc8x_two:
	vcipherlast	$out0,$out0,$in6
	vcipherlast	$out1,$out1,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	le?vperm	$out1,$out1,$out1,$inpperm
	stvx_u		$out0,$x00,$out
	stvx_u		$out1,$x10,$out
	addi		$out,$out,0x20
	b		Lcbc_dec8x_done

.align	5
Lctr32_enc8x_one:
	vcipherlast	$out0,$out0,$in7

	le?vperm	$out0,$out0,$out0,$inpperm
	stvx_u		$out0,0,$out
	addi		$out,$out,0x10

Lctr32_enc8x_done:
	li		r10,`$FRAME+15`
	li		r11,`$FRAME+31`
	stvx		$inpperm,r10,$sp	# wipe copies of round keys
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32
	stvx		$inpperm,r10,$sp
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32
	stvx		$inpperm,r10,$sp
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32
	stvx		$inpperm,r10,$sp
	addi		r10,r10,32
	stvx		$inpperm,r11,$sp
	addi		r11,r11,32

	mtspr		256,$vrsave
	lvx		v20,r10,$sp		# ABI says so
	addi		r10,r10,32
	lvx		v21,r11,$sp
	addi		r11,r11,32
	lvx		v22,r10,$sp
	addi		r10,r10,32
	lvx		v23,r11,$sp
	addi		r11,r11,32
	lvx		v24,r10,$sp
	addi		r10,r10,32
	lvx		v25,r11,$sp
	addi		r11,r11,32
	lvx		v26,r10,$sp
	addi		r10,r10,32
	lvx		v27,r11,$sp
	addi		r11,r11,32
	lvx		v28,r10,$sp
	addi		r10,r10,32
	lvx		v29,r11,$sp
	addi		r11,r11,32
	lvx		v30,r10,$sp
	lvx		v31,r11,$sp
	$POP		r26,`$FRAME+21*16+0*$SIZE_T`($sp)
	$POP		r27,`$FRAME+21*16+1*$SIZE_T`($sp)
	$POP		r28,`$FRAME+21*16+2*$SIZE_T`($sp)
	$POP		r29,`$FRAME+21*16+3*$SIZE_T`($sp)
	$POP		r30,`$FRAME+21*16+4*$SIZE_T`($sp)
	$POP		r31,`$FRAME+21*16+5*$SIZE_T`($sp)
	addi		$sp,$sp,`$FRAME+21*16+6*$SIZE_T`
	blr
	.long		0
	.byte		0,12,0x14,0,0x80,6,6,0
	.long		0
.size	.${prefix}_ctr32_encrypt_blocks,.-.${prefix}_ctr32_encrypt_blocks
___
}}	}}}

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
