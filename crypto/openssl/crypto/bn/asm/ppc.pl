#! /usr/bin/env perl
# Copyright 2004-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

# Implemented as a Perl wrapper as we want to support several different
# architectures with single file. We pick up the target based on the
# file name we are asked to generate.
#
# It should be noted though that this perl code is nothing like
# <openssl>/crypto/perlasm/x86*. In this case perl is used pretty much
# as pre-processor to cover for platform differences in name decoration,
# linker tables, 32-/64-bit instruction sets...
#
# As you might know there're several PowerPC ABI in use. Most notably
# Linux and AIX use different 32-bit ABIs. Good news are that these ABIs
# are similar enough to implement leaf(!) functions, which would be ABI
# neutral. And that's what you find here: ABI neutral leaf functions.
# In case you wonder what that is...
#
#       AIX performance
#
#	MEASUREMENTS WITH cc ON a 200 MhZ PowerPC 604e.
#
#	The following is the performance of 32-bit compiler
#	generated code:
#
#	OpenSSL 0.9.6c 21 dec 2001
#	built on: Tue Jun 11 11:06:51 EDT 2002
#	options:bn(64,32) ...
#compiler: cc -DTHREADS  -DAIX -DB_ENDIAN -DBN_LLONG -O3
#                  sign    verify    sign/s verify/s
#rsa  512 bits   0.0098s   0.0009s    102.0   1170.6
#rsa 1024 bits   0.0507s   0.0026s     19.7    387.5
#rsa 2048 bits   0.3036s   0.0085s      3.3    117.1
#rsa 4096 bits   2.0040s   0.0299s      0.5     33.4
#dsa  512 bits   0.0087s   0.0106s    114.3     94.5
#dsa 1024 bits   0.0256s   0.0313s     39.0     32.0
#
#	Same benchmark with this assembler code:
#
#rsa  512 bits   0.0056s   0.0005s    178.6   2049.2
#rsa 1024 bits   0.0283s   0.0015s     35.3    674.1
#rsa 2048 bits   0.1744s   0.0050s      5.7    201.2
#rsa 4096 bits   1.1644s   0.0179s      0.9     55.7
#dsa  512 bits   0.0052s   0.0062s    191.6    162.0
#dsa 1024 bits   0.0149s   0.0180s     67.0     55.5
#
#	Number of operations increases by at almost 75%
#
#	Here are performance numbers for 64-bit compiler
#	generated code:
#
#	OpenSSL 0.9.6g [engine] 9 Aug 2002
#	built on: Fri Apr 18 16:59:20 EDT 2003
#	options:bn(64,64) ...
#	compiler: cc -DTHREADS -D_REENTRANT -q64 -DB_ENDIAN -O3
#                  sign    verify    sign/s verify/s
#rsa  512 bits   0.0028s   0.0003s    357.1   3844.4
#rsa 1024 bits   0.0148s   0.0008s     67.5   1239.7
#rsa 2048 bits   0.0963s   0.0028s     10.4    353.0
#rsa 4096 bits   0.6538s   0.0102s      1.5     98.1
#dsa  512 bits   0.0026s   0.0032s    382.5    313.7
#dsa 1024 bits   0.0081s   0.0099s    122.8    100.6
#
#	Same benchmark with this assembler code:
#
#rsa  512 bits   0.0020s   0.0002s    510.4   6273.7
#rsa 1024 bits   0.0088s   0.0005s    114.1   2128.3
#rsa 2048 bits   0.0540s   0.0016s     18.5    622.5
#rsa 4096 bits   0.3700s   0.0058s      2.7    171.0
#dsa  512 bits   0.0016s   0.0020s    610.7    507.1
#dsa 1024 bits   0.0047s   0.0058s    212.5    173.2
#
#	Again, performance increases by at about 75%
#
#       Mac OS X, Apple G5 1.8GHz (Note this is 32 bit code)
#       OpenSSL 0.9.7c 30 Sep 2003
#
#       Original code.
#
#rsa  512 bits   0.0011s   0.0001s    906.1  11012.5
#rsa 1024 bits   0.0060s   0.0003s    166.6   3363.1
#rsa 2048 bits   0.0370s   0.0010s     27.1    982.4
#rsa 4096 bits   0.2426s   0.0036s      4.1    280.4
#dsa  512 bits   0.0010s   0.0012s   1038.1    841.5
#dsa 1024 bits   0.0030s   0.0037s    329.6    269.7
#dsa 2048 bits   0.0101s   0.0127s     98.9     78.6
#
#       Same benchmark with this assembler code:
#
#rsa  512 bits   0.0007s   0.0001s   1416.2  16645.9
#rsa 1024 bits   0.0036s   0.0002s    274.4   5380.6
#rsa 2048 bits   0.0222s   0.0006s     45.1   1589.5
#rsa 4096 bits   0.1469s   0.0022s      6.8    449.6
#dsa  512 bits   0.0006s   0.0007s   1664.2   1376.2
#dsa 1024 bits   0.0018s   0.0023s    545.0    442.2
#dsa 2048 bits   0.0061s   0.0075s    163.5    132.8
#
#        Performance increase of ~60%
#        Based on submission from Suresh N. Chari of IBM

$flavour = shift;

if ($flavour =~ /32/) {
	$BITS=	32;
	$BNSZ=	$BITS/8;
	$ISA=	"\"ppc\"";

	$LD=	"lwz";		# load
	$LDU=	"lwzu";		# load and update
	$ST=	"stw";		# store
	$STU=	"stwu";		# store and update
	$UMULL=	"mullw";	# unsigned multiply low
	$UMULH=	"mulhwu";	# unsigned multiply high
	$UDIV=	"divwu";	# unsigned divide
	$UCMPI=	"cmplwi";	# unsigned compare with immediate
	$UCMP=	"cmplw";	# unsigned compare
	$CNTLZ=	"cntlzw";	# count leading zeros
	$SHL=	"slw";		# shift left
	$SHR=	"srw";		# unsigned shift right
	$SHRI=	"srwi";		# unsigned shift right by immediate
	$SHLI=	"slwi";		# shift left by immediate
	$CLRU=	"clrlwi";	# clear upper bits
	$INSR=	"insrwi";	# insert right
	$ROTL=	"rotlwi";	# rotate left by immediate
	$TR=	"tw";		# conditional trap
} elsif ($flavour =~ /64/) {
	$BITS=	64;
	$BNSZ=	$BITS/8;
	$ISA=	"\"ppc64\"";

	# same as above, but 64-bit mnemonics...
	$LD=	"ld";		# load
	$LDU=	"ldu";		# load and update
	$ST=	"std";		# store
	$STU=	"stdu";		# store and update
	$UMULL=	"mulld";	# unsigned multiply low
	$UMULH=	"mulhdu";	# unsigned multiply high
	$UDIV=	"divdu";	# unsigned divide
	$UCMPI=	"cmpldi";	# unsigned compare with immediate
	$UCMP=	"cmpld";	# unsigned compare
	$CNTLZ=	"cntlzd";	# count leading zeros
	$SHL=	"sld";		# shift left
	$SHR=	"srd";		# unsigned shift right
	$SHRI=	"srdi";		# unsigned shift right by immediate
	$SHLI=	"sldi";		# shift left by immediate
	$CLRU=	"clrldi";	# clear upper bits
	$INSR=	"insrdi";	# insert right
	$ROTL=	"rotldi";	# rotate left by immediate
	$TR=	"td";		# conditional trap
} else { die "nonsense $flavour"; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$data=<<EOF;
#--------------------------------------------------------------------
#
#
#
#
#	File:		ppc32.s
#
#	Created by:	Suresh Chari
#			IBM Thomas J. Watson Research Library
#			Hawthorne, NY
#
#
#	Description:	Optimized assembly routines for OpenSSL crypto
#			on the 32 bitPowerPC platform.
#
#
#	Version History
#
#	2. Fixed bn_add,bn_sub and bn_div_words, added comments,
#	   cleaned up code. Also made a single version which can
#	   be used for both the AIX and Linux compilers. See NOTE
#	   below.
#				12/05/03		Suresh Chari
#			(with lots of help from)        Andy Polyakov
##
#	1. Initial version	10/20/02		Suresh Chari
#
#
#	The following file works for the xlc,cc
#	and gcc compilers.
#
#	NOTE:	To get the file to link correctly with the gcc compiler
#	        you have to change the names of the routines and remove
#		the first .(dot) character. This should automatically
#		be done in the build process.
#
#	Hand optimized assembly code for the following routines
#
#	bn_sqr_comba4
#	bn_sqr_comba8
#	bn_mul_comba4
#	bn_mul_comba8
#	bn_sub_words
#	bn_add_words
#	bn_div_words
#	bn_sqr_words
#	bn_mul_words
#	bn_mul_add_words
#
#	NOTE:	It is possible to optimize this code more for
#	specific PowerPC or Power architectures. On the Northstar
#	architecture the optimizations in this file do
#	 NOT provide much improvement.
#
#	If you have comments or suggestions to improve code send
#	me a note at schari\@us.ibm.com
#
#--------------------------------------------------------------------------
#
#	Defines to be used in the assembly code.
#
#.set r0,0	# we use it as storage for value of 0
#.set SP,1	# preserved
#.set RTOC,2	# preserved
#.set r3,3	# 1st argument/return value
#.set r4,4	# 2nd argument/volatile register
#.set r5,5	# 3rd argument/volatile register
#.set r6,6	# ...
#.set r7,7
#.set r8,8
#.set r9,9
#.set r10,10
#.set r11,11
#.set r12,12
#.set r13,13	# not used, nor any other "below" it...

#	Declare function names to be global
#	NOTE:	For gcc these names MUST be changed to remove
#	        the first . i.e. for example change ".bn_sqr_comba4"
#		to "bn_sqr_comba4". This should be automatically done
#		in the build.

	.globl	.bn_sqr_comba4
	.globl	.bn_sqr_comba8
	.globl	.bn_mul_comba4
	.globl	.bn_mul_comba8
	.globl	.bn_sub_words
	.globl	.bn_add_words
	.globl	.bn_div_words
	.globl	.bn_sqr_words
	.globl	.bn_mul_words
	.globl	.bn_mul_add_words

# .text section

	.machine	"any"

#
#	NOTE:	The following label name should be changed to
#		"bn_sqr_comba4" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_sqr_comba4:
#
# Optimized version of bn_sqr_comba4.
#
# void bn_sqr_comba4(BN_ULONG *r, BN_ULONG *a)
# r3 contains r
# r4 contains a
#
# Freely use registers r5,r6,r7,r8,r9,r10,r11 as follows:
#
# r5,r6 are the two BN_ULONGs being multiplied.
# r7,r8 are the results of the 32x32 giving 64 bit multiply.
# r9,r10, r11 are the equivalents of c1,c2, c3.
# Here's the assembly
#
#
	xor		r0,r0,r0		# set r0 = 0. Used in the addze
						# instructions below

						#sqr_add_c(a,0,c1,c2,c3)
	$LD		r5,`0*$BNSZ`(r4)
	$UMULL		r9,r5,r5
	$UMULH		r10,r5,r5		#in first iteration. No need
						#to add since c1=c2=c3=0.
						# Note c3(r11) is NOT set to 0
						# but will be.

	$ST		r9,`0*$BNSZ`(r3)	# r[0]=c1;
						# sqr_add_c2(a,1,0,c2,c3,c1);
	$LD		r6,`1*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r7,r7,r7		# compute (r7,r8)=2*(r7,r8)
	adde		r8,r8,r8
	addze		r9,r0			# catch carry if any.
						# r9= r0(=0) and carry

	addc		r10,r7,r10		# now add to temp result.
	addze		r11,r8                  # r8 added to r11 which is 0
	addze		r9,r9

	$ST		r10,`1*$BNSZ`(r3)	#r[1]=c2;
						#sqr_add_c(a,1,c3,c1,c2)
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r0
						#sqr_add_c2(a,2,0,c3,c1,c2)
	$LD		r6,`2*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r7,r7,r7
	adde		r8,r8,r8
	addze		r10,r10

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	$ST		r11,`2*$BNSZ`(r3)	#r[2]=c3
						#sqr_add_c2(a,3,0,c1,c2,c3);
	$LD		r6,`3*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r7,r7,r7
	adde		r8,r8,r8
	addze		r11,r0

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
						#sqr_add_c2(a,2,1,c1,c2,c3);
	$LD		r5,`1*$BNSZ`(r4)
	$LD		r6,`2*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r7,r7,r7
	adde		r8,r8,r8
	addze		r11,r11
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	$ST		r9,`3*$BNSZ`(r3)	#r[3]=c1
						#sqr_add_c(a,2,c2,c3,c1);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r0
						#sqr_add_c2(a,3,1,c2,c3,c1);
	$LD		r6,`3*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r7,r7,r7
	adde		r8,r8,r8
	addze		r9,r9

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	$ST		r10,`4*$BNSZ`(r3)	#r[4]=c2
						#sqr_add_c2(a,3,2,c3,c1,c2);
	$LD		r5,`2*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r7,r7,r7
	adde		r8,r8,r8
	addze		r10,r0

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	$ST		r11,`5*$BNSZ`(r3)	#r[5] = c3
						#sqr_add_c(a,3,c1,c2,c3);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r9,r7,r9
	adde		r10,r8,r10

	$ST		r9,`6*$BNSZ`(r3)	#r[6]=c1
	$ST		r10,`7*$BNSZ`(r3)	#r[7]=c2
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,2,0
	.long	0
.size	.bn_sqr_comba4,.-.bn_sqr_comba4

#
#	NOTE:	The following label name should be changed to
#		"bn_sqr_comba8" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_sqr_comba8:
#
# This is an optimized version of the bn_sqr_comba8 routine.
# Tightly uses the adde instruction
#
#
# void bn_sqr_comba8(BN_ULONG *r, BN_ULONG *a)
# r3 contains r
# r4 contains a
#
# Freely use registers r5,r6,r7,r8,r9,r10,r11 as follows:
#
# r5,r6 are the two BN_ULONGs being multiplied.
# r7,r8 are the results of the 32x32 giving 64 bit multiply.
# r9,r10, r11 are the equivalents of c1,c2, c3.
#
# Possible optimization of loading all 8 longs of a into registers
# doesn't provide any speedup
#

	xor		r0,r0,r0		#set r0 = 0.Used in addze
						#instructions below.

						#sqr_add_c(a,0,c1,c2,c3);
	$LD		r5,`0*$BNSZ`(r4)
	$UMULL		r9,r5,r5		#1st iteration:	no carries.
	$UMULH		r10,r5,r5
	$ST		r9,`0*$BNSZ`(r3)	# r[0]=c1;
						#sqr_add_c2(a,1,0,c2,c3,c1);
	$LD		r6,`1*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r10,r7,r10		#add the two register number
	adde		r11,r8,r0 		# (r8,r7) to the three register
	addze		r9,r0			# number (r9,r11,r10).NOTE:r0=0

	addc		r10,r7,r10		#add the two register number
	adde		r11,r8,r11 		# (r8,r7) to the three register
	addze		r9,r9			# number (r9,r11,r10).

	$ST		r10,`1*$BNSZ`(r3)	# r[1]=c2

						#sqr_add_c(a,1,c3,c1,c2);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r0
						#sqr_add_c2(a,2,0,c3,c1,c2);
	$LD		r6,`2*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10

	$ST		r11,`2*$BNSZ`(r3)	#r[2]=c3
						#sqr_add_c2(a,3,0,c1,c2,c3);
	$LD		r6,`3*$BNSZ`(r4)	#r6 = a[3]. r5 is already a[0].
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r0

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
						#sqr_add_c2(a,2,1,c1,c2,c3);
	$LD		r5,`1*$BNSZ`(r4)
	$LD		r6,`2*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11

	$ST		r9,`3*$BNSZ`(r3)	#r[3]=c1;
						#sqr_add_c(a,2,c2,c3,c1);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r0
						#sqr_add_c2(a,3,1,c2,c3,c1);
	$LD		r6,`3*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
						#sqr_add_c2(a,4,0,c2,c3,c1);
	$LD		r5,`0*$BNSZ`(r4)
	$LD		r6,`4*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	$ST		r10,`4*$BNSZ`(r3)	#r[4]=c2;
						#sqr_add_c2(a,5,0,c3,c1,c2);
	$LD		r6,`5*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r0

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
						#sqr_add_c2(a,4,1,c3,c1,c2);
	$LD		r5,`1*$BNSZ`(r4)
	$LD		r6,`4*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
						#sqr_add_c2(a,3,2,c3,c1,c2);
	$LD		r5,`2*$BNSZ`(r4)
	$LD		r6,`3*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	$ST		r11,`5*$BNSZ`(r3)	#r[5]=c3;
						#sqr_add_c(a,3,c1,c2,c3);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r0
						#sqr_add_c2(a,4,2,c1,c2,c3);
	$LD		r6,`4*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
						#sqr_add_c2(a,5,1,c1,c2,c3);
	$LD		r5,`1*$BNSZ`(r4)
	$LD		r6,`5*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
						#sqr_add_c2(a,6,0,c1,c2,c3);
	$LD		r5,`0*$BNSZ`(r4)
	$LD		r6,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	$ST		r9,`6*$BNSZ`(r3)	#r[6]=c1;
						#sqr_add_c2(a,7,0,c2,c3,c1);
	$LD		r6,`7*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r0
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
						#sqr_add_c2(a,6,1,c2,c3,c1);
	$LD		r5,`1*$BNSZ`(r4)
	$LD		r6,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
						#sqr_add_c2(a,5,2,c2,c3,c1);
	$LD		r5,`2*$BNSZ`(r4)
	$LD		r6,`5*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
						#sqr_add_c2(a,4,3,c2,c3,c1);
	$LD		r5,`3*$BNSZ`(r4)
	$LD		r6,`4*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	$ST		r10,`7*$BNSZ`(r3)	#r[7]=c2;
						#sqr_add_c(a,4,c3,c1,c2);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r0
						#sqr_add_c2(a,5,3,c3,c1,c2);
	$LD		r6,`5*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
						#sqr_add_c2(a,6,2,c3,c1,c2);
	$LD		r5,`2*$BNSZ`(r4)
	$LD		r6,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10

	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
						#sqr_add_c2(a,7,1,c3,c1,c2);
	$LD		r5,`1*$BNSZ`(r4)
	$LD		r6,`7*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	$ST		r11,`8*$BNSZ`(r3)	#r[8]=c3;
						#sqr_add_c2(a,7,2,c1,c2,c3);
	$LD		r5,`2*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6

	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r0
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
						#sqr_add_c2(a,6,3,c1,c2,c3);
	$LD		r5,`3*$BNSZ`(r4)
	$LD		r6,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
						#sqr_add_c2(a,5,4,c1,c2,c3);
	$LD		r5,`4*$BNSZ`(r4)
	$LD		r6,`5*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	$ST		r9,`9*$BNSZ`(r3)	#r[9]=c1;
						#sqr_add_c(a,5,c2,c3,c1);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r0
						#sqr_add_c2(a,6,4,c2,c3,c1);
	$LD		r6,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
						#sqr_add_c2(a,7,3,c2,c3,c1);
	$LD		r5,`3*$BNSZ`(r4)
	$LD		r6,`7*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	$ST		r10,`10*$BNSZ`(r3)	#r[10]=c2;
						#sqr_add_c2(a,7,4,c3,c1,c2);
	$LD		r5,`4*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r0
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
						#sqr_add_c2(a,6,5,c3,c1,c2);
	$LD		r5,`5*$BNSZ`(r4)
	$LD		r6,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	addc		r11,r7,r11
	adde		r9,r8,r9
	addze		r10,r10
	$ST		r11,`11*$BNSZ`(r3)	#r[11]=c3;
						#sqr_add_c(a,6,c1,c2,c3);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r0
						#sqr_add_c2(a,7,5,c1,c2,c3)
	$LD		r6,`7*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	addc		r9,r7,r9
	adde		r10,r8,r10
	addze		r11,r11
	$ST		r9,`12*$BNSZ`(r3)	#r[12]=c1;

						#sqr_add_c2(a,7,6,c2,c3,c1)
	$LD		r5,`6*$BNSZ`(r4)
	$UMULL		r7,r5,r6
	$UMULH		r8,r5,r6
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r0
	addc		r10,r7,r10
	adde		r11,r8,r11
	addze		r9,r9
	$ST		r10,`13*$BNSZ`(r3)	#r[13]=c2;
						#sqr_add_c(a,7,c3,c1,c2);
	$UMULL		r7,r6,r6
	$UMULH		r8,r6,r6
	addc		r11,r7,r11
	adde		r9,r8,r9
	$ST		r11,`14*$BNSZ`(r3)	#r[14]=c3;
	$ST		r9, `15*$BNSZ`(r3)	#r[15]=c1;


	blr
	.long	0
	.byte	0,12,0x14,0,0,0,2,0
	.long	0
.size	.bn_sqr_comba8,.-.bn_sqr_comba8

#
#	NOTE:	The following label name should be changed to
#		"bn_mul_comba4" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_mul_comba4:
#
# This is an optimized version of the bn_mul_comba4 routine.
#
# void bn_mul_comba4(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
# r3 contains r
# r4 contains a
# r5 contains b
# r6, r7 are the 2 BN_ULONGs being multiplied.
# r8, r9 are the results of the 32x32 giving 64 multiply.
# r10, r11, r12 are the equivalents of c1, c2, and c3.
#
	xor	r0,r0,r0		#r0=0. Used in addze below.
					#mul_add_c(a[0],b[0],c1,c2,c3);
	$LD	r6,`0*$BNSZ`(r4)
	$LD	r7,`0*$BNSZ`(r5)
	$UMULL	r10,r6,r7
	$UMULH	r11,r6,r7
	$ST	r10,`0*$BNSZ`(r3)	#r[0]=c1
					#mul_add_c(a[0],b[1],c2,c3,c1);
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r8,r11
	adde	r12,r9,r0
	addze	r10,r0
					#mul_add_c(a[1],b[0],c2,c3,c1);
	$LD	r6, `1*$BNSZ`(r4)
	$LD	r7, `0*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r8,r11
	adde	r12,r9,r12
	addze	r10,r10
	$ST	r11,`1*$BNSZ`(r3)	#r[1]=c2
					#mul_add_c(a[2],b[0],c3,c1,c2);
	$LD	r6,`2*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r8,r12
	adde	r10,r9,r10
	addze	r11,r0
					#mul_add_c(a[1],b[1],c3,c1,c2);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r8,r12
	adde	r10,r9,r10
	addze	r11,r11
					#mul_add_c(a[0],b[2],c3,c1,c2);
	$LD	r6,`0*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r8,r12
	adde	r10,r9,r10
	addze	r11,r11
	$ST	r12,`2*$BNSZ`(r3)	#r[2]=c3
					#mul_add_c(a[0],b[3],c1,c2,c3);
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r8,r10
	adde	r11,r9,r11
	addze	r12,r0
					#mul_add_c(a[1],b[2],c1,c2,c3);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r8,r10
	adde	r11,r9,r11
	addze	r12,r12
					#mul_add_c(a[2],b[1],c1,c2,c3);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r8,r10
	adde	r11,r9,r11
	addze	r12,r12
					#mul_add_c(a[3],b[0],c1,c2,c3);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`0*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r8,r10
	adde	r11,r9,r11
	addze	r12,r12
	$ST	r10,`3*$BNSZ`(r3)	#r[3]=c1
					#mul_add_c(a[3],b[1],c2,c3,c1);
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r8,r11
	adde	r12,r9,r12
	addze	r10,r0
					#mul_add_c(a[2],b[2],c2,c3,c1);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r8,r11
	adde	r12,r9,r12
	addze	r10,r10
					#mul_add_c(a[1],b[3],c2,c3,c1);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r8,r11
	adde	r12,r9,r12
	addze	r10,r10
	$ST	r11,`4*$BNSZ`(r3)	#r[4]=c2
					#mul_add_c(a[2],b[3],c3,c1,c2);
	$LD	r6,`2*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r8,r12
	adde	r10,r9,r10
	addze	r11,r0
					#mul_add_c(a[3],b[2],c3,c1,c2);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r8,r12
	adde	r10,r9,r10
	addze	r11,r11
	$ST	r12,`5*$BNSZ`(r3)	#r[5]=c3
					#mul_add_c(a[3],b[3],c1,c2,c3);
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r8,r10
	adde	r11,r9,r11

	$ST	r10,`6*$BNSZ`(r3)	#r[6]=c1
	$ST	r11,`7*$BNSZ`(r3)	#r[7]=c2
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	.bn_mul_comba4,.-.bn_mul_comba4

#
#	NOTE:	The following label name should be changed to
#		"bn_mul_comba8" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_mul_comba8:
#
# Optimized version of the bn_mul_comba8 routine.
#
# void bn_mul_comba8(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
# r3 contains r
# r4 contains a
# r5 contains b
# r6, r7 are the 2 BN_ULONGs being multiplied.
# r8, r9 are the results of the 32x32 giving 64 multiply.
# r10, r11, r12 are the equivalents of c1, c2, and c3.
#
	xor	r0,r0,r0		#r0=0. Used in addze below.

					#mul_add_c(a[0],b[0],c1,c2,c3);
	$LD	r6,`0*$BNSZ`(r4)	#a[0]
	$LD	r7,`0*$BNSZ`(r5)	#b[0]
	$UMULL	r10,r6,r7
	$UMULH	r11,r6,r7
	$ST	r10,`0*$BNSZ`(r3)	#r[0]=c1;
					#mul_add_c(a[0],b[1],c2,c3,c1);
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	addze	r12,r9			# since we didn't set r12 to zero before.
	addze	r10,r0
					#mul_add_c(a[1],b[0],c2,c3,c1);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`0*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
	$ST	r11,`1*$BNSZ`(r3)	#r[1]=c2;
					#mul_add_c(a[2],b[0],c3,c1,c2);
	$LD	r6,`2*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r0
					#mul_add_c(a[1],b[1],c3,c1,c2);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[0],b[2],c3,c1,c2);
	$LD	r6,`0*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
	$ST	r12,`2*$BNSZ`(r3)	#r[2]=c3;
					#mul_add_c(a[0],b[3],c1,c2,c3);
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r0
					#mul_add_c(a[1],b[2],c1,c2,c3);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12

					#mul_add_c(a[2],b[1],c1,c2,c3);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[3],b[0],c1,c2,c3);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`0*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
	$ST	r10,`3*$BNSZ`(r3)	#r[3]=c1;
					#mul_add_c(a[4],b[0],c2,c3,c1);
	$LD	r6,`4*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r0
					#mul_add_c(a[3],b[1],c2,c3,c1);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[2],b[2],c2,c3,c1);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[1],b[3],c2,c3,c1);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[0],b[4],c2,c3,c1);
	$LD	r6,`0*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
	$ST	r11,`4*$BNSZ`(r3)	#r[4]=c2;
					#mul_add_c(a[0],b[5],c3,c1,c2);
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r0
					#mul_add_c(a[1],b[4],c3,c1,c2);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[2],b[3],c3,c1,c2);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[3],b[2],c3,c1,c2);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[4],b[1],c3,c1,c2);
	$LD	r6,`4*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[5],b[0],c3,c1,c2);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`0*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
	$ST	r12,`5*$BNSZ`(r3)	#r[5]=c3;
					#mul_add_c(a[6],b[0],c1,c2,c3);
	$LD	r6,`6*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r0
					#mul_add_c(a[5],b[1],c1,c2,c3);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[4],b[2],c1,c2,c3);
	$LD	r6,`4*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[3],b[3],c1,c2,c3);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[2],b[4],c1,c2,c3);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[1],b[5],c1,c2,c3);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[0],b[6],c1,c2,c3);
	$LD	r6,`0*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
	$ST	r10,`6*$BNSZ`(r3)	#r[6]=c1;
					#mul_add_c(a[0],b[7],c2,c3,c1);
	$LD	r7,`7*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r0
					#mul_add_c(a[1],b[6],c2,c3,c1);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[2],b[5],c2,c3,c1);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[3],b[4],c2,c3,c1);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[4],b[3],c2,c3,c1);
	$LD	r6,`4*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[5],b[2],c2,c3,c1);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[6],b[1],c2,c3,c1);
	$LD	r6,`6*$BNSZ`(r4)
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[7],b[0],c2,c3,c1);
	$LD	r6,`7*$BNSZ`(r4)
	$LD	r7,`0*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
	$ST	r11,`7*$BNSZ`(r3)	#r[7]=c2;
					#mul_add_c(a[7],b[1],c3,c1,c2);
	$LD	r7,`1*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r0
					#mul_add_c(a[6],b[2],c3,c1,c2);
	$LD	r6,`6*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[5],b[3],c3,c1,c2);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[4],b[4],c3,c1,c2);
	$LD	r6,`4*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[3],b[5],c3,c1,c2);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[2],b[6],c3,c1,c2);
	$LD	r6,`2*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[1],b[7],c3,c1,c2);
	$LD	r6,`1*$BNSZ`(r4)
	$LD	r7,`7*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
	$ST	r12,`8*$BNSZ`(r3)	#r[8]=c3;
					#mul_add_c(a[2],b[7],c1,c2,c3);
	$LD	r6,`2*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r0
					#mul_add_c(a[3],b[6],c1,c2,c3);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[4],b[5],c1,c2,c3);
	$LD	r6,`4*$BNSZ`(r4)
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[5],b[4],c1,c2,c3);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[6],b[3],c1,c2,c3);
	$LD	r6,`6*$BNSZ`(r4)
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[7],b[2],c1,c2,c3);
	$LD	r6,`7*$BNSZ`(r4)
	$LD	r7,`2*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
	$ST	r10,`9*$BNSZ`(r3)	#r[9]=c1;
					#mul_add_c(a[7],b[3],c2,c3,c1);
	$LD	r7,`3*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r0
					#mul_add_c(a[6],b[4],c2,c3,c1);
	$LD	r6,`6*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[5],b[5],c2,c3,c1);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[4],b[6],c2,c3,c1);
	$LD	r6,`4*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
					#mul_add_c(a[3],b[7],c2,c3,c1);
	$LD	r6,`3*$BNSZ`(r4)
	$LD	r7,`7*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
	$ST	r11,`10*$BNSZ`(r3)	#r[10]=c2;
					#mul_add_c(a[4],b[7],c3,c1,c2);
	$LD	r6,`4*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r0
					#mul_add_c(a[5],b[6],c3,c1,c2);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[6],b[5],c3,c1,c2);
	$LD	r6,`6*$BNSZ`(r4)
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
					#mul_add_c(a[7],b[4],c3,c1,c2);
	$LD	r6,`7*$BNSZ`(r4)
	$LD	r7,`4*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	addze	r11,r11
	$ST	r12,`11*$BNSZ`(r3)	#r[11]=c3;
					#mul_add_c(a[7],b[5],c1,c2,c3);
	$LD	r7,`5*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r0
					#mul_add_c(a[6],b[6],c1,c2,c3);
	$LD	r6,`6*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
					#mul_add_c(a[5],b[7],c1,c2,c3);
	$LD	r6,`5*$BNSZ`(r4)
	$LD	r7,`7*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r10,r10,r8
	adde	r11,r11,r9
	addze	r12,r12
	$ST	r10,`12*$BNSZ`(r3)	#r[12]=c1;
					#mul_add_c(a[6],b[7],c2,c3,c1);
	$LD	r6,`6*$BNSZ`(r4)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r0
					#mul_add_c(a[7],b[6],c2,c3,c1);
	$LD	r6,`7*$BNSZ`(r4)
	$LD	r7,`6*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r11,r11,r8
	adde	r12,r12,r9
	addze	r10,r10
	$ST	r11,`13*$BNSZ`(r3)	#r[13]=c2;
					#mul_add_c(a[7],b[7],c3,c1,c2);
	$LD	r7,`7*$BNSZ`(r5)
	$UMULL	r8,r6,r7
	$UMULH	r9,r6,r7
	addc	r12,r12,r8
	adde	r10,r10,r9
	$ST	r12,`14*$BNSZ`(r3)	#r[14]=c3;
	$ST	r10,`15*$BNSZ`(r3)	#r[15]=c1;
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	.bn_mul_comba8,.-.bn_mul_comba8

#
#	NOTE:	The following label name should be changed to
#		"bn_sub_words" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#
#
.align	4
.bn_sub_words:
#
#	Handcoded version of bn_sub_words
#
#BN_ULONG bn_sub_words(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b, int n)
#
#	r3 = r
#	r4 = a
#	r5 = b
#	r6 = n
#
#       Note:	No loop unrolling done since this is not a performance
#               critical loop.

	xor	r0,r0,r0	#set r0 = 0
#
#	check for r6 = 0 AND set carry bit.
#
	subfc.	r7,r0,r6        # If r6 is 0 then result is 0.
				# if r6 > 0 then result !=0
				# In either case carry bit is set.
	beq	Lppcasm_sub_adios
	addi	r4,r4,-$BNSZ
	addi	r3,r3,-$BNSZ
	addi	r5,r5,-$BNSZ
	mtctr	r6
Lppcasm_sub_mainloop:
	$LDU	r7,$BNSZ(r4)
	$LDU	r8,$BNSZ(r5)
	subfe	r6,r8,r7	# r6 = r7+carry bit + onescomplement(r8)
				# if carry = 1 this is r7-r8. Else it
				# is r7-r8 -1 as we need.
	$STU	r6,$BNSZ(r3)
	bdnz	Lppcasm_sub_mainloop
Lppcasm_sub_adios:
	subfze	r3,r0		# if carry bit is set then r3 = 0 else -1
	andi.	r3,r3,1         # keep only last bit.
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,4,0
	.long	0
.size	.bn_sub_words,.-.bn_sub_words

#
#	NOTE:	The following label name should be changed to
#		"bn_add_words" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_add_words:
#
#	Handcoded version of bn_add_words
#
#BN_ULONG bn_add_words(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b, int n)
#
#	r3 = r
#	r4 = a
#	r5 = b
#	r6 = n
#
#       Note:	No loop unrolling done since this is not a performance
#               critical loop.

	xor	r0,r0,r0
#
#	check for r6 = 0. Is this needed?
#
	addic.	r6,r6,0		#test r6 and clear carry bit.
	beq	Lppcasm_add_adios
	addi	r4,r4,-$BNSZ
	addi	r3,r3,-$BNSZ
	addi	r5,r5,-$BNSZ
	mtctr	r6
Lppcasm_add_mainloop:
	$LDU	r7,$BNSZ(r4)
	$LDU	r8,$BNSZ(r5)
	adde	r8,r7,r8
	$STU	r8,$BNSZ(r3)
	bdnz	Lppcasm_add_mainloop
Lppcasm_add_adios:
	addze	r3,r0			#return carry bit.
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,4,0
	.long	0
.size	.bn_add_words,.-.bn_add_words

#
#	NOTE:	The following label name should be changed to
#		"bn_div_words" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_div_words:
#
#	This is a cleaned up version of code generated by
#	the AIX compiler. The only optimization is to use
#	the PPC instruction to count leading zeros instead
#	of call to num_bits_word. Since this was compiled
#	only at level -O2 we can possibly squeeze it more?
#
#	r3 = h
#	r4 = l
#	r5 = d

	$UCMPI	0,r5,0			# compare r5 and 0
	bne	Lppcasm_div1		# proceed if d!=0
	li	r3,-1			# d=0 return -1
	blr
Lppcasm_div1:
	xor	r0,r0,r0		#r0=0
	li	r8,$BITS
	$CNTLZ.	r7,r5			#r7 = num leading 0s in d.
	beq	Lppcasm_div2		#proceed if no leading zeros
	subf	r8,r7,r8		#r8 = BN_num_bits_word(d)
	$SHR.	r9,r3,r8		#are there any bits above r8'th?
	$TR	16,r9,r0		#if there're, signal to dump core...
Lppcasm_div2:
	$UCMP	0,r3,r5			#h>=d?
	blt	Lppcasm_div3		#goto Lppcasm_div3 if not
	subf	r3,r5,r3		#h-=d ;
Lppcasm_div3:				#r7 = BN_BITS2-i. so r7=i
	cmpi	0,0,r7,0		# is (i == 0)?
	beq	Lppcasm_div4
	$SHL	r3,r3,r7		# h = (h<< i)
	$SHR	r8,r4,r8		# r8 = (l >> BN_BITS2 -i)
	$SHL	r5,r5,r7		# d<<=i
	or	r3,r3,r8		# h = (h<<i)|(l>>(BN_BITS2-i))
	$SHL	r4,r4,r7		# l <<=i
Lppcasm_div4:
	$SHRI	r9,r5,`$BITS/2`		# r9 = dh
					# dl will be computed when needed
					# as it saves registers.
	li	r6,2			#r6=2
	mtctr	r6			#counter will be in count.
Lppcasm_divouterloop:
	$SHRI	r8,r3,`$BITS/2`		#r8 = (h>>BN_BITS4)
	$SHRI	r11,r4,`$BITS/2`	#r11= (l&BN_MASK2h)>>BN_BITS4
					# compute here for innerloop.
	$UCMP	0,r8,r9			# is (h>>BN_BITS4)==dh
	bne	Lppcasm_div5		# goto Lppcasm_div5 if not

	li	r8,-1
	$CLRU	r8,r8,`$BITS/2`		#q = BN_MASK2l
	b	Lppcasm_div6
Lppcasm_div5:
	$UDIV	r8,r3,r9		#q = h/dh
Lppcasm_div6:
	$UMULL	r12,r9,r8		#th = q*dh
	$CLRU	r10,r5,`$BITS/2`	#r10=dl
	$UMULL	r6,r8,r10		#tl = q*dl

Lppcasm_divinnerloop:
	subf	r10,r12,r3		#t = h -th
	$SHRI	r7,r10,`$BITS/2`	#r7= (t &BN_MASK2H), sort of...
	addic.	r7,r7,0			#test if r7 == 0. used below.
					# now want to compute
					# r7 = (t<<BN_BITS4)|((l&BN_MASK2h)>>BN_BITS4)
					# the following 2 instructions do that
	$SHLI	r7,r10,`$BITS/2`	# r7 = (t<<BN_BITS4)
	or	r7,r7,r11		# r7|=((l&BN_MASK2h)>>BN_BITS4)
	$UCMP	cr1,r6,r7		# compare (tl <= r7)
	bne	Lppcasm_divinnerexit
	ble	cr1,Lppcasm_divinnerexit
	addi	r8,r8,-1		#q--
	subf	r12,r9,r12		#th -=dh
	$CLRU	r10,r5,`$BITS/2`	#r10=dl. t is no longer needed in loop.
	subf	r6,r10,r6		#tl -=dl
	b	Lppcasm_divinnerloop
Lppcasm_divinnerexit:
	$SHRI	r10,r6,`$BITS/2`	#t=(tl>>BN_BITS4)
	$SHLI	r11,r6,`$BITS/2`	#tl=(tl<<BN_BITS4)&BN_MASK2h;
	$UCMP	cr1,r4,r11		# compare l and tl
	add	r12,r12,r10		# th+=t
	bge	cr1,Lppcasm_div7	# if (l>=tl) goto Lppcasm_div7
	addi	r12,r12,1		# th++
Lppcasm_div7:
	subf	r11,r11,r4		#r11=l-tl
	$UCMP	cr1,r3,r12		#compare h and th
	bge	cr1,Lppcasm_div8	#if (h>=th) goto Lppcasm_div8
	addi	r8,r8,-1		# q--
	add	r3,r5,r3		# h+=d
Lppcasm_div8:
	subf	r12,r12,r3		#r12 = h-th
	$SHLI	r4,r11,`$BITS/2`	#l=(l&BN_MASK2l)<<BN_BITS4
					# want to compute
					# h = ((h<<BN_BITS4)|(l>>BN_BITS4))&BN_MASK2
					# the following 2 instructions will do this.
	$INSR	r11,r12,`$BITS/2`,`$BITS/2`	# r11 is the value we want rotated $BITS/2.
	$ROTL	r3,r11,`$BITS/2`	# rotate by $BITS/2 and store in r3
	bdz	Lppcasm_div9		#if (count==0) break ;
	$SHLI	r0,r8,`$BITS/2`		#ret =q<<BN_BITS4
	b	Lppcasm_divouterloop
Lppcasm_div9:
	or	r3,r8,r0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	.bn_div_words,.-.bn_div_words

#
#	NOTE:	The following label name should be changed to
#		"bn_sqr_words" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#
.align	4
.bn_sqr_words:
#
#	Optimized version of bn_sqr_words
#
#	void bn_sqr_words(BN_ULONG *r, BN_ULONG *a, int n)
#
#	r3 = r
#	r4 = a
#	r5 = n
#
#	r6 = a[i].
#	r7,r8 = product.
#
#	No unrolling done here. Not performance critical.

	addic.	r5,r5,0			#test r5.
	beq	Lppcasm_sqr_adios
	addi	r4,r4,-$BNSZ
	addi	r3,r3,-$BNSZ
	mtctr	r5
Lppcasm_sqr_mainloop:
					#sqr(r[0],r[1],a[0]);
	$LDU	r6,$BNSZ(r4)
	$UMULL	r7,r6,r6
	$UMULH  r8,r6,r6
	$STU	r7,$BNSZ(r3)
	$STU	r8,$BNSZ(r3)
	bdnz	Lppcasm_sqr_mainloop
Lppcasm_sqr_adios:
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	.bn_sqr_words,.-.bn_sqr_words

#
#	NOTE:	The following label name should be changed to
#		"bn_mul_words" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_mul_words:
#
# BN_ULONG bn_mul_words(BN_ULONG *rp, BN_ULONG *ap, int num, BN_ULONG w)
#
# r3 = rp
# r4 = ap
# r5 = num
# r6 = w
	xor	r0,r0,r0
	xor	r12,r12,r12		# used for carry
	rlwinm.	r7,r5,30,2,31		# num >> 2
	beq	Lppcasm_mw_REM
	mtctr	r7
Lppcasm_mw_LOOP:
					#mul(rp[0],ap[0],w,c1);
	$LD	r8,`0*$BNSZ`(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	addc	r9,r9,r12
	#addze	r10,r10			#carry is NOT ignored.
					#will be taken care of
					#in second spin below
					#using adde.
	$ST	r9,`0*$BNSZ`(r3)
					#mul(rp[1],ap[1],w,c1);
	$LD	r8,`1*$BNSZ`(r4)
	$UMULL	r11,r6,r8
	$UMULH  r12,r6,r8
	adde	r11,r11,r10
	#addze	r12,r12
	$ST	r11,`1*$BNSZ`(r3)
					#mul(rp[2],ap[2],w,c1);
	$LD	r8,`2*$BNSZ`(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	adde	r9,r9,r12
	#addze	r10,r10
	$ST	r9,`2*$BNSZ`(r3)
					#mul_add(rp[3],ap[3],w,c1);
	$LD	r8,`3*$BNSZ`(r4)
	$UMULL	r11,r6,r8
	$UMULH  r12,r6,r8
	adde	r11,r11,r10
	addze	r12,r12			#this spin we collect carry into
					#r12
	$ST	r11,`3*$BNSZ`(r3)

	addi	r3,r3,`4*$BNSZ`
	addi	r4,r4,`4*$BNSZ`
	bdnz	Lppcasm_mw_LOOP

Lppcasm_mw_REM:
	andi.	r5,r5,0x3
	beq	Lppcasm_mw_OVER
					#mul(rp[0],ap[0],w,c1);
	$LD	r8,`0*$BNSZ`(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	addc	r9,r9,r12
	addze	r10,r10
	$ST	r9,`0*$BNSZ`(r3)
	addi	r12,r10,0

	addi	r5,r5,-1
	cmpli	0,0,r5,0
	beq	Lppcasm_mw_OVER


					#mul(rp[1],ap[1],w,c1);
	$LD	r8,`1*$BNSZ`(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	addc	r9,r9,r12
	addze	r10,r10
	$ST	r9,`1*$BNSZ`(r3)
	addi	r12,r10,0

	addi	r5,r5,-1
	cmpli	0,0,r5,0
	beq	Lppcasm_mw_OVER

					#mul_add(rp[2],ap[2],w,c1);
	$LD	r8,`2*$BNSZ`(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	addc	r9,r9,r12
	addze	r10,r10
	$ST	r9,`2*$BNSZ`(r3)
	addi	r12,r10,0

Lppcasm_mw_OVER:
	addi	r3,r12,0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,4,0
	.long	0
.size	.bn_mul_words,.-.bn_mul_words

#
#	NOTE:	The following label name should be changed to
#		"bn_mul_add_words" i.e. remove the first dot
#		for the gcc compiler. This should be automatically
#		done in the build
#

.align	4
.bn_mul_add_words:
#
# BN_ULONG bn_mul_add_words(BN_ULONG *rp, BN_ULONG *ap, int num, BN_ULONG w)
#
# r3 = rp
# r4 = ap
# r5 = num
# r6 = w
#
# empirical evidence suggests that unrolled version performs best!!
#
	xor	r0,r0,r0		#r0 = 0
	xor	r12,r12,r12  		#r12 = 0 . used for carry
	rlwinm.	r7,r5,30,2,31		# num >> 2
	beq	Lppcasm_maw_leftover	# if (num < 4) go LPPCASM_maw_leftover
	mtctr	r7
Lppcasm_maw_mainloop:
					#mul_add(rp[0],ap[0],w,c1);
	$LD	r8,`0*$BNSZ`(r4)
	$LD	r11,`0*$BNSZ`(r3)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	addc	r9,r9,r12		#r12 is carry.
	addze	r10,r10
	addc	r9,r9,r11
	#addze	r10,r10
					#the above instruction addze
					#is NOT needed. Carry will NOT
					#be ignored. It's not affected
					#by multiply and will be collected
					#in the next spin
	$ST	r9,`0*$BNSZ`(r3)

					#mul_add(rp[1],ap[1],w,c1);
	$LD	r8,`1*$BNSZ`(r4)
	$LD	r9,`1*$BNSZ`(r3)
	$UMULL	r11,r6,r8
	$UMULH  r12,r6,r8
	adde	r11,r11,r10		#r10 is carry.
	addze	r12,r12
	addc	r11,r11,r9
	#addze	r12,r12
	$ST	r11,`1*$BNSZ`(r3)

					#mul_add(rp[2],ap[2],w,c1);
	$LD	r8,`2*$BNSZ`(r4)
	$UMULL	r9,r6,r8
	$LD	r11,`2*$BNSZ`(r3)
	$UMULH  r10,r6,r8
	adde	r9,r9,r12
	addze	r10,r10
	addc	r9,r9,r11
	#addze	r10,r10
	$ST	r9,`2*$BNSZ`(r3)

					#mul_add(rp[3],ap[3],w,c1);
	$LD	r8,`3*$BNSZ`(r4)
	$UMULL	r11,r6,r8
	$LD	r9,`3*$BNSZ`(r3)
	$UMULH  r12,r6,r8
	adde	r11,r11,r10
	addze	r12,r12
	addc	r11,r11,r9
	addze	r12,r12
	$ST	r11,`3*$BNSZ`(r3)
	addi	r3,r3,`4*$BNSZ`
	addi	r4,r4,`4*$BNSZ`
	bdnz	Lppcasm_maw_mainloop

Lppcasm_maw_leftover:
	andi.	r5,r5,0x3
	beq	Lppcasm_maw_adios
	addi	r3,r3,-$BNSZ
	addi	r4,r4,-$BNSZ
					#mul_add(rp[0],ap[0],w,c1);
	mtctr	r5
	$LDU	r8,$BNSZ(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	$LDU	r11,$BNSZ(r3)
	addc	r9,r9,r11
	addze	r10,r10
	addc	r9,r9,r12
	addze	r12,r10
	$ST	r9,0(r3)

	bdz	Lppcasm_maw_adios
					#mul_add(rp[1],ap[1],w,c1);
	$LDU	r8,$BNSZ(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	$LDU	r11,$BNSZ(r3)
	addc	r9,r9,r11
	addze	r10,r10
	addc	r9,r9,r12
	addze	r12,r10
	$ST	r9,0(r3)

	bdz	Lppcasm_maw_adios
					#mul_add(rp[2],ap[2],w,c1);
	$LDU	r8,$BNSZ(r4)
	$UMULL	r9,r6,r8
	$UMULH  r10,r6,r8
	$LDU	r11,$BNSZ(r3)
	addc	r9,r9,r11
	addze	r10,r10
	addc	r9,r9,r12
	addze	r12,r10
	$ST	r9,0(r3)

Lppcasm_maw_adios:
	addi	r3,r12,0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,4,0
	.long	0
.size	.bn_mul_add_words,.-.bn_mul_add_words
	.align	4
EOF
$data =~ s/\`([^\`]*)\`/eval $1/gem;
print $data;
close STDOUT;
