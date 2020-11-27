########################################################################
# Copyright (c) 2013, Intel Corporation
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# OpenIB.org BSD license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the
#   distribution.
#
# * Neither the name of the Intel Corporation nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
#
# THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION ""AS IS"" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES# LOSS OF USE, DATA, OR
# PROFITS# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
########################################################################
##
## Authors:
##	Erdinc Ozturk <erdinc.ozturk@intel.com>
##	Vinodh Gopal <vinodh.gopal@intel.com>
##	James Guilford <james.guilford@intel.com>
##	Tim Chen <tim.c.chen@linux.intel.com>
##
## References:
##       This code was derived and highly optimized from the code described in paper:
##               Vinodh Gopal et. al. Optimized Galois-Counter-Mode Implementation
##			on Intel Architecture Processors. August, 2010
##       The details of the implementation is explained in:
##               Erdinc Ozturk et. al. Enabling High-Performance Galois-Counter-Mode
##			on Intel Architecture Processors. October, 2012.
##
## Assumptions:
##
##
##
## iv:
##       0                   1                   2                   3
##       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                             Salt  (From the SA)               |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                     Initialization Vector                     |
##       |         (This is the sequence number from IPSec header)       |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                              0x1                              |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##
##
##
## AAD:
##       AAD padded to 128 bits with 0
##       for example, assume AAD is a u32 vector
##
##       if AAD is 8 bytes:
##       AAD[3] = {A0, A1}#
##       padded AAD in xmm register = {A1 A0 0 0}
##
##       0                   1                   2                   3
##       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                               SPI (A1)                        |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                     32-bit Sequence Number (A0)               |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                              0x0                              |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##
##                                       AAD Format with 32-bit Sequence Number
##
##       if AAD is 12 bytes:
##       AAD[3] = {A0, A1, A2}#
##       padded AAD in xmm register = {A2 A1 A0 0}
##
##       0                   1                   2                   3
##       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                               SPI (A2)                        |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                 64-bit Extended Sequence Number {A1,A0}       |
##       |                                                               |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##       |                              0x0                              |
##       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
##
##        AAD Format with 64-bit Extended Sequence Number
##
##
## aadLen:
##       from the definition of the spec, aadLen can only be 8 or 12 bytes.
##	 The code additionally supports aadLen of length 16 bytes.
##
## TLen:
##       from the definition of the spec, TLen can only be 8, 12 or 16 bytes.
##
## poly = x^128 + x^127 + x^126 + x^121 + 1
## throughout the code, one tab and two tab indentations are used. one tab is
## for GHASH part, two tabs is for AES part.
##

#include <linux/linkage.h>

# constants in mergeable sections, linker can reorder and merge
.section	.rodata.cst16.POLY, "aM", @progbits, 16
.align 16
POLY:            .octa     0xC2000000000000000000000000000001

.section	.rodata.cst16.POLY2, "aM", @progbits, 16
.align 16
POLY2:           .octa     0xC20000000000000000000001C2000000

.section	.rodata.cst16.TWOONE, "aM", @progbits, 16
.align 16
TWOONE:          .octa     0x00000001000000000000000000000001

.section	.rodata.cst16.SHUF_MASK, "aM", @progbits, 16
.align 16
SHUF_MASK:       .octa     0x000102030405060708090A0B0C0D0E0F

.section	.rodata.cst16.ONE, "aM", @progbits, 16
.align 16
ONE:             .octa     0x00000000000000000000000000000001

.section	.rodata.cst16.ONEf, "aM", @progbits, 16
.align 16
ONEf:            .octa     0x01000000000000000000000000000000

# order of these constants should not change.
# more specifically, ALL_F should follow SHIFT_MASK, and zero should follow ALL_F
.section	.rodata, "a", @progbits
.align 16
SHIFT_MASK:      .octa     0x0f0e0d0c0b0a09080706050403020100
ALL_F:           .octa     0xffffffffffffffffffffffffffffffff
                 .octa     0x00000000000000000000000000000000

.section .rodata
.align 16
.type aad_shift_arr, @object
.size aad_shift_arr, 272
aad_shift_arr:
        .octa     0xffffffffffffffffffffffffffffffff
        .octa     0xffffffffffffffffffffffffffffff0C
        .octa     0xffffffffffffffffffffffffffff0D0C
        .octa     0xffffffffffffffffffffffffff0E0D0C
        .octa     0xffffffffffffffffffffffff0F0E0D0C
        .octa     0xffffffffffffffffffffff0C0B0A0908
        .octa     0xffffffffffffffffffff0D0C0B0A0908
        .octa     0xffffffffffffffffff0E0D0C0B0A0908
        .octa     0xffffffffffffffff0F0E0D0C0B0A0908
        .octa     0xffffffffffffff0C0B0A090807060504
        .octa     0xffffffffffff0D0C0B0A090807060504
        .octa     0xffffffffff0E0D0C0B0A090807060504
        .octa     0xffffffff0F0E0D0C0B0A090807060504
        .octa     0xffffff0C0B0A09080706050403020100
        .octa     0xffff0D0C0B0A09080706050403020100
        .octa     0xff0E0D0C0B0A09080706050403020100
        .octa     0x0F0E0D0C0B0A09080706050403020100


.text


#define AadHash 16*0
#define AadLen 16*1
#define InLen (16*1)+8
#define PBlockEncKey 16*2
#define OrigIV 16*3
#define CurCount 16*4
#define PBlockLen 16*5

HashKey        = 16*6   # store HashKey <<1 mod poly here
HashKey_2      = 16*7   # store HashKey^2 <<1 mod poly here
HashKey_3      = 16*8   # store HashKey^3 <<1 mod poly here
HashKey_4      = 16*9   # store HashKey^4 <<1 mod poly here
HashKey_5      = 16*10   # store HashKey^5 <<1 mod poly here
HashKey_6      = 16*11   # store HashKey^6 <<1 mod poly here
HashKey_7      = 16*12   # store HashKey^7 <<1 mod poly here
HashKey_8      = 16*13   # store HashKey^8 <<1 mod poly here
HashKey_k      = 16*14   # store XOR of HashKey <<1 mod poly here (for Karatsuba purposes)
HashKey_2_k    = 16*15   # store XOR of HashKey^2 <<1 mod poly here (for Karatsuba purposes)
HashKey_3_k    = 16*16   # store XOR of HashKey^3 <<1 mod poly here (for Karatsuba purposes)
HashKey_4_k    = 16*17   # store XOR of HashKey^4 <<1 mod poly here (for Karatsuba purposes)
HashKey_5_k    = 16*18   # store XOR of HashKey^5 <<1 mod poly here (for Karatsuba purposes)
HashKey_6_k    = 16*19   # store XOR of HashKey^6 <<1 mod poly here (for Karatsuba purposes)
HashKey_7_k    = 16*20   # store XOR of HashKey^7 <<1 mod poly here (for Karatsuba purposes)
HashKey_8_k    = 16*21   # store XOR of HashKey^8 <<1 mod poly here (for Karatsuba purposes)

#define arg1 %rdi
#define arg2 %rsi
#define arg3 %rdx
#define arg4 %rcx
#define arg5 %r8
#define arg6 %r9
#define arg7 STACK_OFFSET+8*1(%r14)
#define arg8 STACK_OFFSET+8*2(%r14)
#define arg9 STACK_OFFSET+8*3(%r14)
#define arg10 STACK_OFFSET+8*4(%r14)
#define keysize 2*15*16(arg1)

i = 0
j = 0

out_order = 0
in_order = 1
DEC = 0
ENC = 1

.macro define_reg r n
reg_\r = %xmm\n
.endm

.macro setreg
.altmacro
define_reg i %i
define_reg j %j
.noaltmacro
.endm

# need to push 4 registers into stack to maintain
STACK_OFFSET = 8*4

TMP1 =   16*0    # Temporary storage for AAD
TMP2 =   16*1    # Temporary storage for AES State 2 (State 1 is stored in an XMM register)
TMP3 =   16*2    # Temporary storage for AES State 3
TMP4 =   16*3    # Temporary storage for AES State 4
TMP5 =   16*4    # Temporary storage for AES State 5
TMP6 =   16*5    # Temporary storage for AES State 6
TMP7 =   16*6    # Temporary storage for AES State 7
TMP8 =   16*7    # Temporary storage for AES State 8

VARIABLE_OFFSET = 16*8

################################
# Utility Macros
################################

.macro FUNC_SAVE
        #the number of pushes must equal STACK_OFFSET
        push    %r12
        push    %r13
        push    %r14
        push    %r15

        mov     %rsp, %r14



        sub     $VARIABLE_OFFSET, %rsp
        and     $~63, %rsp                    # align rsp to 64 bytes
.endm

.macro FUNC_RESTORE
        mov     %r14, %rsp

        pop     %r15
        pop     %r14
        pop     %r13
        pop     %r12
.endm

# Encryption of a single block
.macro ENCRYPT_SINGLE_BLOCK REP XMM0
                vpxor    (arg1), \XMM0, \XMM0
               i = 1
               setreg
.rep \REP
                vaesenc  16*i(arg1), \XMM0, \XMM0
               i = (i+1)
               setreg
.endr
                vaesenclast 16*i(arg1), \XMM0, \XMM0
.endm

# combined for GCM encrypt and decrypt functions
# clobbering all xmm registers
# clobbering r10, r11, r12, r13, r14, r15
.macro  GCM_ENC_DEC INITIAL_BLOCKS GHASH_8_ENCRYPT_8_PARALLEL GHASH_LAST_8 GHASH_MUL ENC_DEC REP
        vmovdqu AadHash(arg2), %xmm8
        vmovdqu  HashKey(arg2), %xmm13      # xmm13 = HashKey
        add arg5, InLen(arg2)

        # initialize the data pointer offset as zero
        xor     %r11d, %r11d

        PARTIAL_BLOCK \GHASH_MUL, arg3, arg4, arg5, %r11, %xmm8, \ENC_DEC
        sub %r11, arg5

        mov     arg5, %r13                  # save the number of bytes of plaintext/ciphertext
        and     $-16, %r13                  # r13 = r13 - (r13 mod 16)

        mov     %r13, %r12
        shr     $4, %r12
        and     $7, %r12
        jz      _initial_num_blocks_is_0\@

        cmp     $7, %r12
        je      _initial_num_blocks_is_7\@
        cmp     $6, %r12
        je      _initial_num_blocks_is_6\@
        cmp     $5, %r12
        je      _initial_num_blocks_is_5\@
        cmp     $4, %r12
        je      _initial_num_blocks_is_4\@
        cmp     $3, %r12
        je      _initial_num_blocks_is_3\@
        cmp     $2, %r12
        je      _initial_num_blocks_is_2\@

        jmp     _initial_num_blocks_is_1\@

_initial_num_blocks_is_7\@:
        \INITIAL_BLOCKS  \REP, 7, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*7, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_6\@:
        \INITIAL_BLOCKS  \REP, 6, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*6, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_5\@:
        \INITIAL_BLOCKS  \REP, 5, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*5, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_4\@:
        \INITIAL_BLOCKS  \REP, 4, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*4, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_3\@:
        \INITIAL_BLOCKS  \REP, 3, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*3, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_2\@:
        \INITIAL_BLOCKS  \REP, 2, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*2, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_1\@:
        \INITIAL_BLOCKS  \REP, 1, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
        sub     $16*1, %r13
        jmp     _initial_blocks_encrypted\@

_initial_num_blocks_is_0\@:
        \INITIAL_BLOCKS  \REP, 0, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC


_initial_blocks_encrypted\@:
        test    %r13, %r13
        je      _zero_cipher_left\@

        sub     $128, %r13
        je      _eight_cipher_left\@




        vmovd   %xmm9, %r15d
        and     $255, %r15d
        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9


_encrypt_by_8_new\@:
        cmp     $(255-8), %r15d
        jg      _encrypt_by_8\@



        add     $8, %r15b
        \GHASH_8_ENCRYPT_8_PARALLEL      \REP, %xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm15, out_order, \ENC_DEC
        add     $128, %r11
        sub     $128, %r13
        jne     _encrypt_by_8_new\@

        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9
        jmp     _eight_cipher_left\@

_encrypt_by_8\@:
        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9
        add     $8, %r15b
        \GHASH_8_ENCRYPT_8_PARALLEL      \REP, %xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm15, in_order, \ENC_DEC
        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9
        add     $128, %r11
        sub     $128, %r13
        jne     _encrypt_by_8_new\@

        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9




_eight_cipher_left\@:
        \GHASH_LAST_8    %xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8


_zero_cipher_left\@:
        vmovdqu %xmm14, AadHash(arg2)
        vmovdqu %xmm9, CurCount(arg2)

        # check for 0 length
        mov     arg5, %r13
        and     $15, %r13                            # r13 = (arg5 mod 16)

        je      _multiple_of_16_bytes\@

        # handle the last <16 Byte block separately

        mov %r13, PBlockLen(arg2)

        vpaddd  ONE(%rip), %xmm9, %xmm9              # INCR CNT to get Yn
        vmovdqu %xmm9, CurCount(arg2)
        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9

        ENCRYPT_SINGLE_BLOCK    \REP, %xmm9                # E(K, Yn)
        vmovdqu %xmm9, PBlockEncKey(arg2)

        cmp $16, arg5
        jge _large_enough_update\@

        lea (arg4,%r11,1), %r10
        mov %r13, %r12

        READ_PARTIAL_BLOCK %r10 %r12 %xmm1

        lea     SHIFT_MASK+16(%rip), %r12
        sub     %r13, %r12                           # adjust the shuffle mask pointer to be
						     # able to shift 16-r13 bytes (r13 is the
	# number of bytes in plaintext mod 16)

        jmp _final_ghash_mul\@

_large_enough_update\@:
        sub $16, %r11
        add %r13, %r11

        # receive the last <16 Byte block
        vmovdqu	(arg4, %r11, 1), %xmm1

        sub	%r13, %r11
        add	$16, %r11

        lea	SHIFT_MASK+16(%rip), %r12
        # adjust the shuffle mask pointer to be able to shift 16-r13 bytes
        # (r13 is the number of bytes in plaintext mod 16)
        sub	%r13, %r12
        # get the appropriate shuffle mask
        vmovdqu	(%r12), %xmm2
        # shift right 16-r13 bytes
        vpshufb  %xmm2, %xmm1, %xmm1

_final_ghash_mul\@:
        .if  \ENC_DEC ==  DEC
        vmovdqa %xmm1, %xmm2
        vpxor   %xmm1, %xmm9, %xmm9                  # Plaintext XOR E(K, Yn)
        vmovdqu ALL_F-SHIFT_MASK(%r12), %xmm1        # get the appropriate mask to
						     # mask out top 16-r13 bytes of xmm9
        vpand   %xmm1, %xmm9, %xmm9                  # mask out top 16-r13 bytes of xmm9
        vpand   %xmm1, %xmm2, %xmm2
        vpshufb SHUF_MASK(%rip), %xmm2, %xmm2
        vpxor   %xmm2, %xmm14, %xmm14

        vmovdqu %xmm14, AadHash(arg2)
        .else
        vpxor   %xmm1, %xmm9, %xmm9                  # Plaintext XOR E(K, Yn)
        vmovdqu ALL_F-SHIFT_MASK(%r12), %xmm1        # get the appropriate mask to
						     # mask out top 16-r13 bytes of xmm9
        vpand   %xmm1, %xmm9, %xmm9                  # mask out top 16-r13 bytes of xmm9
        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9
        vpxor   %xmm9, %xmm14, %xmm14

        vmovdqu %xmm14, AadHash(arg2)
        vpshufb SHUF_MASK(%rip), %xmm9, %xmm9        # shuffle xmm9 back to output as ciphertext
        .endif


        #############################
        # output r13 Bytes
        vmovq   %xmm9, %rax
        cmp     $8, %r13
        jle     _less_than_8_bytes_left\@

        mov     %rax, (arg3 , %r11)
        add     $8, %r11
        vpsrldq $8, %xmm9, %xmm9
        vmovq   %xmm9, %rax
        sub     $8, %r13

_less_than_8_bytes_left\@:
        movb    %al, (arg3 , %r11)
        add     $1, %r11
        shr     $8, %rax
        sub     $1, %r13
        jne     _less_than_8_bytes_left\@
        #############################

_multiple_of_16_bytes\@:
.endm


# GCM_COMPLETE Finishes update of tag of last partial block
# Output: Authorization Tag (AUTH_TAG)
# Clobbers rax, r10-r12, and xmm0, xmm1, xmm5-xmm15
.macro GCM_COMPLETE GHASH_MUL REP AUTH_TAG AUTH_TAG_LEN
        vmovdqu AadHash(arg2), %xmm14
        vmovdqu HashKey(arg2), %xmm13

        mov PBlockLen(arg2), %r12
        test %r12, %r12
        je _partial_done\@

	#GHASH computation for the last <16 Byte block
        \GHASH_MUL       %xmm14, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6

_partial_done\@:
        mov AadLen(arg2), %r12                          # r12 = aadLen (number of bytes)
        shl     $3, %r12                             # convert into number of bits
        vmovd   %r12d, %xmm15                        # len(A) in xmm15

        mov InLen(arg2), %r12
        shl     $3, %r12                        # len(C) in bits  (*128)
        vmovq   %r12, %xmm1
        vpslldq $8, %xmm15, %xmm15                   # xmm15 = len(A)|| 0x0000000000000000
        vpxor   %xmm1, %xmm15, %xmm15                # xmm15 = len(A)||len(C)

        vpxor   %xmm15, %xmm14, %xmm14
        \GHASH_MUL       %xmm14, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6    # final GHASH computation
        vpshufb SHUF_MASK(%rip), %xmm14, %xmm14      # perform a 16Byte swap

        vmovdqu OrigIV(arg2), %xmm9

        ENCRYPT_SINGLE_BLOCK    \REP, %xmm9                # E(K, Y0)

        vpxor   %xmm14, %xmm9, %xmm9



_return_T\@:
        mov     \AUTH_TAG, %r10              # r10 = authTag
        mov     \AUTH_TAG_LEN, %r11              # r11 = auth_tag_len

        cmp     $16, %r11
        je      _T_16\@

        cmp     $8, %r11
        jl      _T_4\@

_T_8\@:
        vmovq   %xmm9, %rax
        mov     %rax, (%r10)
        add     $8, %r10
        sub     $8, %r11
        vpsrldq $8, %xmm9, %xmm9
        test    %r11, %r11
        je     _return_T_done\@
_T_4\@:
        vmovd   %xmm9, %eax
        mov     %eax, (%r10)
        add     $4, %r10
        sub     $4, %r11
        vpsrldq     $4, %xmm9, %xmm9
        test    %r11, %r11
        je     _return_T_done\@
_T_123\@:
        vmovd     %xmm9, %eax
        cmp     $2, %r11
        jl     _T_1\@
        mov     %ax, (%r10)
        cmp     $2, %r11
        je     _return_T_done\@
        add     $2, %r10
        sar     $16, %eax
_T_1\@:
        mov     %al, (%r10)
        jmp     _return_T_done\@

_T_16\@:
        vmovdqu %xmm9, (%r10)

_return_T_done\@:
.endm

.macro CALC_AAD_HASH GHASH_MUL AAD AADLEN T1 T2 T3 T4 T5 T6 T7 T8

	mov     \AAD, %r10                      # r10 = AAD
	mov     \AADLEN, %r12                      # r12 = aadLen


	mov     %r12, %r11

	vpxor   \T8, \T8, \T8
	vpxor   \T7, \T7, \T7
	cmp     $16, %r11
	jl      _get_AAD_rest8\@
_get_AAD_blocks\@:
	vmovdqu (%r10), \T7
	vpshufb SHUF_MASK(%rip), \T7, \T7
	vpxor   \T7, \T8, \T8
	\GHASH_MUL       \T8, \T2, \T1, \T3, \T4, \T5, \T6
	add     $16, %r10
	sub     $16, %r12
	sub     $16, %r11
	cmp     $16, %r11
	jge     _get_AAD_blocks\@
	vmovdqu \T8, \T7
	test    %r11, %r11
	je      _get_AAD_done\@

	vpxor   \T7, \T7, \T7

	/* read the last <16B of AAD. since we have at least 4B of
	data right after the AAD (the ICV, and maybe some CT), we can
	read 4B/8B blocks safely, and then get rid of the extra stuff */
_get_AAD_rest8\@:
	cmp     $4, %r11
	jle     _get_AAD_rest4\@
	movq    (%r10), \T1
	add     $8, %r10
	sub     $8, %r11
	vpslldq $8, \T1, \T1
	vpsrldq $8, \T7, \T7
	vpxor   \T1, \T7, \T7
	jmp     _get_AAD_rest8\@
_get_AAD_rest4\@:
	test    %r11, %r11
	jle      _get_AAD_rest0\@
	mov     (%r10), %eax
	movq    %rax, \T1
	add     $4, %r10
	sub     $4, %r11
	vpslldq $12, \T1, \T1
	vpsrldq $4, \T7, \T7
	vpxor   \T1, \T7, \T7
_get_AAD_rest0\@:
	/* finalize: shift out the extra bytes we read, and align
	left. since pslldq can only shift by an immediate, we use
	vpshufb and an array of shuffle masks */
	movq    %r12, %r11
	salq    $4, %r11
	vmovdqu  aad_shift_arr(%r11), \T1
	vpshufb \T1, \T7, \T7
_get_AAD_rest_final\@:
	vpshufb SHUF_MASK(%rip), \T7, \T7
	vpxor   \T8, \T7, \T7
	\GHASH_MUL       \T7, \T2, \T1, \T3, \T4, \T5, \T6

_get_AAD_done\@:
        vmovdqu \T7, AadHash(arg2)
.endm

.macro INIT GHASH_MUL PRECOMPUTE
        mov arg6, %r11
        mov %r11, AadLen(arg2) # ctx_data.aad_length = aad_length
        xor %r11d, %r11d
        mov %r11, InLen(arg2) # ctx_data.in_length = 0

        mov %r11, PBlockLen(arg2) # ctx_data.partial_block_length = 0
        mov %r11, PBlockEncKey(arg2) # ctx_data.partial_block_enc_key = 0
        mov arg3, %rax
        movdqu (%rax), %xmm0
        movdqu %xmm0, OrigIV(arg2) # ctx_data.orig_IV = iv

        vpshufb SHUF_MASK(%rip), %xmm0, %xmm0
        movdqu %xmm0, CurCount(arg2) # ctx_data.current_counter = iv

        vmovdqu  (arg4), %xmm6              # xmm6 = HashKey

        vpshufb  SHUF_MASK(%rip), %xmm6, %xmm6
        ###############  PRECOMPUTATION of HashKey<<1 mod poly from the HashKey
        vmovdqa  %xmm6, %xmm2
        vpsllq   $1, %xmm6, %xmm6
        vpsrlq   $63, %xmm2, %xmm2
        vmovdqa  %xmm2, %xmm1
        vpslldq  $8, %xmm2, %xmm2
        vpsrldq  $8, %xmm1, %xmm1
        vpor     %xmm2, %xmm6, %xmm6
        #reduction
        vpshufd  $0b00100100, %xmm1, %xmm2
        vpcmpeqd TWOONE(%rip), %xmm2, %xmm2
        vpand    POLY(%rip), %xmm2, %xmm2
        vpxor    %xmm2, %xmm6, %xmm6        # xmm6 holds the HashKey<<1 mod poly
        #######################################################################
        vmovdqu  %xmm6, HashKey(arg2)       # store HashKey<<1 mod poly

        CALC_AAD_HASH \GHASH_MUL, arg5, arg6, %xmm2, %xmm6, %xmm3, %xmm4, %xmm5, %xmm7, %xmm1, %xmm0

        \PRECOMPUTE  %xmm6, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5
.endm


# Reads DLEN bytes starting at DPTR and stores in XMMDst
# where 0 < DLEN < 16
# Clobbers %rax, DLEN
.macro READ_PARTIAL_BLOCK DPTR DLEN XMMDst
        vpxor \XMMDst, \XMMDst, \XMMDst

        cmp $8, \DLEN
        jl _read_lt8_\@
        mov (\DPTR), %rax
        vpinsrq $0, %rax, \XMMDst, \XMMDst
        sub $8, \DLEN
        jz _done_read_partial_block_\@
        xor %eax, %eax
_read_next_byte_\@:
        shl $8, %rax
        mov 7(\DPTR, \DLEN, 1), %al
        dec \DLEN
        jnz _read_next_byte_\@
        vpinsrq $1, %rax, \XMMDst, \XMMDst
        jmp _done_read_partial_block_\@
_read_lt8_\@:
        xor %eax, %eax
_read_next_byte_lt8_\@:
        shl $8, %rax
        mov -1(\DPTR, \DLEN, 1), %al
        dec \DLEN
        jnz _read_next_byte_lt8_\@
        vpinsrq $0, %rax, \XMMDst, \XMMDst
_done_read_partial_block_\@:
.endm

# PARTIAL_BLOCK: Handles encryption/decryption and the tag partial blocks
# between update calls.
# Requires the input data be at least 1 byte long due to READ_PARTIAL_BLOCK
# Outputs encrypted bytes, and updates hash and partial info in gcm_data_context
# Clobbers rax, r10, r12, r13, xmm0-6, xmm9-13
.macro PARTIAL_BLOCK GHASH_MUL CYPH_PLAIN_OUT PLAIN_CYPH_IN PLAIN_CYPH_LEN DATA_OFFSET \
        AAD_HASH ENC_DEC
        mov 	PBlockLen(arg2), %r13
        test	%r13, %r13
        je	_partial_block_done_\@	# Leave Macro if no partial blocks
        # Read in input data without over reading
        cmp	$16, \PLAIN_CYPH_LEN
        jl	_fewer_than_16_bytes_\@
        vmovdqu	(\PLAIN_CYPH_IN), %xmm1	# If more than 16 bytes, just fill xmm
        jmp	_data_read_\@

_fewer_than_16_bytes_\@:
        lea	(\PLAIN_CYPH_IN, \DATA_OFFSET, 1), %r10
        mov	\PLAIN_CYPH_LEN, %r12
        READ_PARTIAL_BLOCK %r10 %r12 %xmm1

        mov PBlockLen(arg2), %r13

_data_read_\@:				# Finished reading in data

        vmovdqu	PBlockEncKey(arg2), %xmm9
        vmovdqu	HashKey(arg2), %xmm13

        lea	SHIFT_MASK(%rip), %r12

        # adjust the shuffle mask pointer to be able to shift r13 bytes
        # r16-r13 is the number of bytes in plaintext mod 16)
        add	%r13, %r12
        vmovdqu	(%r12), %xmm2		# get the appropriate shuffle mask
        vpshufb %xmm2, %xmm9, %xmm9		# shift right r13 bytes

.if  \ENC_DEC ==  DEC
        vmovdqa	%xmm1, %xmm3
        pxor	%xmm1, %xmm9		# Cyphertext XOR E(K, Yn)

        mov	\PLAIN_CYPH_LEN, %r10
        add	%r13, %r10
        # Set r10 to be the amount of data left in CYPH_PLAIN_IN after filling
        sub	$16, %r10
        # Determine if if partial block is not being filled and
        # shift mask accordingly
        jge	_no_extra_mask_1_\@
        sub	%r10, %r12
_no_extra_mask_1_\@:

        vmovdqu	ALL_F-SHIFT_MASK(%r12), %xmm1
        # get the appropriate mask to mask out bottom r13 bytes of xmm9
        vpand	%xmm1, %xmm9, %xmm9		# mask out bottom r13 bytes of xmm9

        vpand	%xmm1, %xmm3, %xmm3
        vmovdqa	SHUF_MASK(%rip), %xmm10
        vpshufb	%xmm10, %xmm3, %xmm3
        vpshufb	%xmm2, %xmm3, %xmm3
        vpxor	%xmm3, \AAD_HASH, \AAD_HASH

        test	%r10, %r10
        jl	_partial_incomplete_1_\@

        # GHASH computation for the last <16 Byte block
        \GHASH_MUL \AAD_HASH, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6
        xor	%eax,%eax

        mov	%rax, PBlockLen(arg2)
        jmp	_dec_done_\@
_partial_incomplete_1_\@:
        add	\PLAIN_CYPH_LEN, PBlockLen(arg2)
_dec_done_\@:
        vmovdqu	\AAD_HASH, AadHash(arg2)
.else
        vpxor	%xmm1, %xmm9, %xmm9			# Plaintext XOR E(K, Yn)

        mov	\PLAIN_CYPH_LEN, %r10
        add	%r13, %r10
        # Set r10 to be the amount of data left in CYPH_PLAIN_IN after filling
        sub	$16, %r10
        # Determine if if partial block is not being filled and
        # shift mask accordingly
        jge	_no_extra_mask_2_\@
        sub	%r10, %r12
_no_extra_mask_2_\@:

        vmovdqu	ALL_F-SHIFT_MASK(%r12), %xmm1
        # get the appropriate mask to mask out bottom r13 bytes of xmm9
        vpand	%xmm1, %xmm9, %xmm9

        vmovdqa	SHUF_MASK(%rip), %xmm1
        vpshufb %xmm1, %xmm9, %xmm9
        vpshufb %xmm2, %xmm9, %xmm9
        vpxor	%xmm9, \AAD_HASH, \AAD_HASH

        test	%r10, %r10
        jl	_partial_incomplete_2_\@

        # GHASH computation for the last <16 Byte block
        \GHASH_MUL \AAD_HASH, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6
        xor	%eax,%eax

        mov	%rax, PBlockLen(arg2)
        jmp	_encode_done_\@
_partial_incomplete_2_\@:
        add	\PLAIN_CYPH_LEN, PBlockLen(arg2)
_encode_done_\@:
        vmovdqu	\AAD_HASH, AadHash(arg2)

        vmovdqa	SHUF_MASK(%rip), %xmm10
        # shuffle xmm9 back to output as ciphertext
        vpshufb	%xmm10, %xmm9, %xmm9
        vpshufb	%xmm2, %xmm9, %xmm9
.endif
        # output encrypted Bytes
        test	%r10, %r10
        jl	_partial_fill_\@
        mov	%r13, %r12
        mov	$16, %r13
        # Set r13 to be the number of bytes to write out
        sub	%r12, %r13
        jmp	_count_set_\@
_partial_fill_\@:
        mov	\PLAIN_CYPH_LEN, %r13
_count_set_\@:
        vmovdqa	%xmm9, %xmm0
        vmovq	%xmm0, %rax
        cmp	$8, %r13
        jle	_less_than_8_bytes_left_\@

        mov	%rax, (\CYPH_PLAIN_OUT, \DATA_OFFSET, 1)
        add	$8, \DATA_OFFSET
        psrldq	$8, %xmm0
        vmovq	%xmm0, %rax
        sub	$8, %r13
_less_than_8_bytes_left_\@:
        movb	%al, (\CYPH_PLAIN_OUT, \DATA_OFFSET, 1)
        add	$1, \DATA_OFFSET
        shr	$8, %rax
        sub	$1, %r13
        jne	_less_than_8_bytes_left_\@
_partial_block_done_\@:
.endm # PARTIAL_BLOCK

###############################################################################
# GHASH_MUL MACRO to implement: Data*HashKey mod (128,127,126,121,0)
# Input: A and B (128-bits each, bit-reflected)
# Output: C = A*B*x mod poly, (i.e. >>1 )
# To compute GH = GH*HashKey mod poly, give HK = HashKey<<1 mod poly as input
# GH = GH * HK * x mod poly which is equivalent to GH*HashKey mod poly.
###############################################################################
.macro  GHASH_MUL_AVX GH HK T1 T2 T3 T4 T5

        vpshufd         $0b01001110, \GH, \T2
        vpshufd         $0b01001110, \HK, \T3
        vpxor           \GH     , \T2, \T2      # T2 = (a1+a0)
        vpxor           \HK     , \T3, \T3      # T3 = (b1+b0)

        vpclmulqdq      $0x11, \HK, \GH, \T1    # T1 = a1*b1
        vpclmulqdq      $0x00, \HK, \GH, \GH    # GH = a0*b0
        vpclmulqdq      $0x00, \T3, \T2, \T2    # T2 = (a1+a0)*(b1+b0)
        vpxor           \GH, \T2,\T2
        vpxor           \T1, \T2,\T2            # T2 = a0*b1+a1*b0

        vpslldq         $8, \T2,\T3             # shift-L T3 2 DWs
        vpsrldq         $8, \T2,\T2             # shift-R T2 2 DWs
        vpxor           \T3, \GH, \GH
        vpxor           \T2, \T1, \T1           # <T1:GH> = GH x HK

        #first phase of the reduction
        vpslld  $31, \GH, \T2                   # packed right shifting << 31
        vpslld  $30, \GH, \T3                   # packed right shifting shift << 30
        vpslld  $25, \GH, \T4                   # packed right shifting shift << 25

        vpxor   \T3, \T2, \T2                   # xor the shifted versions
        vpxor   \T4, \T2, \T2

        vpsrldq $4, \T2, \T5                    # shift-R T5 1 DW

        vpslldq $12, \T2, \T2                   # shift-L T2 3 DWs
        vpxor   \T2, \GH, \GH                   # first phase of the reduction complete

        #second phase of the reduction

        vpsrld  $1,\GH, \T2                     # packed left shifting >> 1
        vpsrld  $2,\GH, \T3                     # packed left shifting >> 2
        vpsrld  $7,\GH, \T4                     # packed left shifting >> 7
        vpxor   \T3, \T2, \T2                   # xor the shifted versions
        vpxor   \T4, \T2, \T2

        vpxor   \T5, \T2, \T2
        vpxor   \T2, \GH, \GH
        vpxor   \T1, \GH, \GH                   # the result is in GH


.endm

.macro PRECOMPUTE_AVX HK T1 T2 T3 T4 T5 T6

        # Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
        vmovdqa  \HK, \T5

        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^2<<1 mod poly
        vmovdqu  \T5, HashKey_2(arg2)                    #  [HashKey_2] = HashKey^2<<1 mod poly
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_2_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^3<<1 mod poly
        vmovdqu  \T5, HashKey_3(arg2)
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_3_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^4<<1 mod poly
        vmovdqu  \T5, HashKey_4(arg2)
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_4_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^5<<1 mod poly
        vmovdqu  \T5, HashKey_5(arg2)
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_5_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^6<<1 mod poly
        vmovdqu  \T5, HashKey_6(arg2)
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_6_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^7<<1 mod poly
        vmovdqu  \T5, HashKey_7(arg2)
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_7_k(arg2)

        GHASH_MUL_AVX \T5, \HK, \T1, \T3, \T4, \T6, \T2  #  T5 = HashKey^8<<1 mod poly
        vmovdqu  \T5, HashKey_8(arg2)
        vpshufd  $0b01001110, \T5, \T1
        vpxor    \T5, \T1, \T1
        vmovdqu  \T1, HashKey_8_k(arg2)

.endm

## if a = number of total plaintext bytes
## b = floor(a/16)
## num_initial_blocks = b mod 4#
## encrypt the initial num_initial_blocks blocks and apply ghash on the ciphertext
## r10, r11, r12, rax are clobbered
## arg1, arg3, arg4, r14 are used as a pointer only, not modified

.macro INITIAL_BLOCKS_AVX REP num_initial_blocks T1 T2 T3 T4 T5 CTR XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8 T6 T_key ENC_DEC
	i = (8-\num_initial_blocks)
	setreg
        vmovdqu AadHash(arg2), reg_i

	# start AES for num_initial_blocks blocks
	vmovdqu CurCount(arg2), \CTR

	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
                vpaddd  ONE(%rip), \CTR, \CTR		# INCR Y0
                vmovdqa \CTR, reg_i
                vpshufb SHUF_MASK(%rip), reg_i, reg_i   # perform a 16Byte swap
	i = (i+1)
	setreg
.endr

	vmovdqa  (arg1), \T_key
	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
                vpxor   \T_key, reg_i, reg_i
	i = (i+1)
	setreg
.endr

       j = 1
       setreg
.rep \REP
       vmovdqa  16*j(arg1), \T_key
	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
        vaesenc \T_key, reg_i, reg_i
	i = (i+1)
	setreg
.endr

       j = (j+1)
       setreg
.endr

	vmovdqa  16*j(arg1), \T_key
	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
        vaesenclast      \T_key, reg_i, reg_i
	i = (i+1)
	setreg
.endr

	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
                vmovdqu (arg4, %r11), \T1
                vpxor   \T1, reg_i, reg_i
                vmovdqu reg_i, (arg3 , %r11)           # write back ciphertext for num_initial_blocks blocks
                add     $16, %r11
.if  \ENC_DEC == DEC
                vmovdqa \T1, reg_i
.endif
                vpshufb SHUF_MASK(%rip), reg_i, reg_i  # prepare ciphertext for GHASH computations
	i = (i+1)
	setreg
.endr


	i = (8-\num_initial_blocks)
	j = (9-\num_initial_blocks)
	setreg

.rep \num_initial_blocks
        vpxor    reg_i, reg_j, reg_j
        GHASH_MUL_AVX       reg_j, \T2, \T1, \T3, \T4, \T5, \T6 # apply GHASH on num_initial_blocks blocks
	i = (i+1)
	j = (j+1)
	setreg
.endr
        # XMM8 has the combined result here

        vmovdqa  \XMM8, TMP1(%rsp)
        vmovdqa  \XMM8, \T3

        cmp     $128, %r13
        jl      _initial_blocks_done\@                  # no need for precomputed constants

###############################################################################
# Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM1
                vpshufb  SHUF_MASK(%rip), \XMM1, \XMM1  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM2
                vpshufb  SHUF_MASK(%rip), \XMM2, \XMM2  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM3
                vpshufb  SHUF_MASK(%rip), \XMM3, \XMM3  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM4
                vpshufb  SHUF_MASK(%rip), \XMM4, \XMM4  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM5
                vpshufb  SHUF_MASK(%rip), \XMM5, \XMM5  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM6
                vpshufb  SHUF_MASK(%rip), \XMM6, \XMM6  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM7
                vpshufb  SHUF_MASK(%rip), \XMM7, \XMM7  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM8
                vpshufb  SHUF_MASK(%rip), \XMM8, \XMM8  # perform a 16Byte swap

                vmovdqa  (arg1), \T_key
                vpxor    \T_key, \XMM1, \XMM1
                vpxor    \T_key, \XMM2, \XMM2
                vpxor    \T_key, \XMM3, \XMM3
                vpxor    \T_key, \XMM4, \XMM4
                vpxor    \T_key, \XMM5, \XMM5
                vpxor    \T_key, \XMM6, \XMM6
                vpxor    \T_key, \XMM7, \XMM7
                vpxor    \T_key, \XMM8, \XMM8

               i = 1
               setreg
.rep    \REP       # do REP rounds
                vmovdqa  16*i(arg1), \T_key
                vaesenc  \T_key, \XMM1, \XMM1
                vaesenc  \T_key, \XMM2, \XMM2
                vaesenc  \T_key, \XMM3, \XMM3
                vaesenc  \T_key, \XMM4, \XMM4
                vaesenc  \T_key, \XMM5, \XMM5
                vaesenc  \T_key, \XMM6, \XMM6
                vaesenc  \T_key, \XMM7, \XMM7
                vaesenc  \T_key, \XMM8, \XMM8
               i = (i+1)
               setreg
.endr

                vmovdqa  16*i(arg1), \T_key
                vaesenclast  \T_key, \XMM1, \XMM1
                vaesenclast  \T_key, \XMM2, \XMM2
                vaesenclast  \T_key, \XMM3, \XMM3
                vaesenclast  \T_key, \XMM4, \XMM4
                vaesenclast  \T_key, \XMM5, \XMM5
                vaesenclast  \T_key, \XMM6, \XMM6
                vaesenclast  \T_key, \XMM7, \XMM7
                vaesenclast  \T_key, \XMM8, \XMM8

                vmovdqu  (arg4, %r11), \T1
                vpxor    \T1, \XMM1, \XMM1
                vmovdqu  \XMM1, (arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM1
                .endif

                vmovdqu  16*1(arg4, %r11), \T1
                vpxor    \T1, \XMM2, \XMM2
                vmovdqu  \XMM2, 16*1(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM2
                .endif

                vmovdqu  16*2(arg4, %r11), \T1
                vpxor    \T1, \XMM3, \XMM3
                vmovdqu  \XMM3, 16*2(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM3
                .endif

                vmovdqu  16*3(arg4, %r11), \T1
                vpxor    \T1, \XMM4, \XMM4
                vmovdqu  \XMM4, 16*3(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM4
                .endif

                vmovdqu  16*4(arg4, %r11), \T1
                vpxor    \T1, \XMM5, \XMM5
                vmovdqu  \XMM5, 16*4(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM5
                .endif

                vmovdqu  16*5(arg4, %r11), \T1
                vpxor    \T1, \XMM6, \XMM6
                vmovdqu  \XMM6, 16*5(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM6
                .endif

                vmovdqu  16*6(arg4, %r11), \T1
                vpxor    \T1, \XMM7, \XMM7
                vmovdqu  \XMM7, 16*6(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM7
                .endif

                vmovdqu  16*7(arg4, %r11), \T1
                vpxor    \T1, \XMM8, \XMM8
                vmovdqu  \XMM8, 16*7(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM8
                .endif

                add     $128, %r11

                vpshufb  SHUF_MASK(%rip), \XMM1, \XMM1     # perform a 16Byte swap
                vpxor    TMP1(%rsp), \XMM1, \XMM1          # combine GHASHed value with the corresponding ciphertext
                vpshufb  SHUF_MASK(%rip), \XMM2, \XMM2     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM3, \XMM3     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM4, \XMM4     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM5, \XMM5     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM6, \XMM6     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM7, \XMM7     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM8, \XMM8     # perform a 16Byte swap

###############################################################################

_initial_blocks_done\@:

.endm

# encrypt 8 blocks at a time
# ghash the 8 previously encrypted ciphertext blocks
# arg1, arg3, arg4 are used as pointers only, not modified
# r11 is the data offset value
.macro GHASH_8_ENCRYPT_8_PARALLEL_AVX REP T1 T2 T3 T4 T5 T6 CTR XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8 T7 loop_idx ENC_DEC

        vmovdqa \XMM1, \T2
        vmovdqa \XMM2, TMP2(%rsp)
        vmovdqa \XMM3, TMP3(%rsp)
        vmovdqa \XMM4, TMP4(%rsp)
        vmovdqa \XMM5, TMP5(%rsp)
        vmovdqa \XMM6, TMP6(%rsp)
        vmovdqa \XMM7, TMP7(%rsp)
        vmovdqa \XMM8, TMP8(%rsp)

.if \loop_idx == in_order
                vpaddd  ONE(%rip), \CTR, \XMM1           # INCR CNT
                vpaddd  ONE(%rip), \XMM1, \XMM2
                vpaddd  ONE(%rip), \XMM2, \XMM3
                vpaddd  ONE(%rip), \XMM3, \XMM4
                vpaddd  ONE(%rip), \XMM4, \XMM5
                vpaddd  ONE(%rip), \XMM5, \XMM6
                vpaddd  ONE(%rip), \XMM6, \XMM7
                vpaddd  ONE(%rip), \XMM7, \XMM8
                vmovdqa \XMM8, \CTR

                vpshufb SHUF_MASK(%rip), \XMM1, \XMM1    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM2, \XMM2    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM3, \XMM3    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM4, \XMM4    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM5, \XMM5    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM6, \XMM6    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM7, \XMM7    # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM8, \XMM8    # perform a 16Byte swap
.else
                vpaddd  ONEf(%rip), \CTR, \XMM1           # INCR CNT
                vpaddd  ONEf(%rip), \XMM1, \XMM2
                vpaddd  ONEf(%rip), \XMM2, \XMM3
                vpaddd  ONEf(%rip), \XMM3, \XMM4
                vpaddd  ONEf(%rip), \XMM4, \XMM5
                vpaddd  ONEf(%rip), \XMM5, \XMM6
                vpaddd  ONEf(%rip), \XMM6, \XMM7
                vpaddd  ONEf(%rip), \XMM7, \XMM8
                vmovdqa \XMM8, \CTR
.endif


        #######################################################################

                vmovdqu (arg1), \T1
                vpxor   \T1, \XMM1, \XMM1
                vpxor   \T1, \XMM2, \XMM2
                vpxor   \T1, \XMM3, \XMM3
                vpxor   \T1, \XMM4, \XMM4
                vpxor   \T1, \XMM5, \XMM5
                vpxor   \T1, \XMM6, \XMM6
                vpxor   \T1, \XMM7, \XMM7
                vpxor   \T1, \XMM8, \XMM8

        #######################################################################





                vmovdqu 16*1(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

                vmovdqu 16*2(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8


        #######################################################################

        vmovdqu         HashKey_8(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T2, \T4             # T4 = a1*b1
        vpclmulqdq      $0x00, \T5, \T2, \T7             # T7 = a0*b0

        vpshufd         $0b01001110, \T2, \T6
        vpxor           \T2, \T6, \T6

        vmovdqu         HashKey_8_k(arg2), \T5
        vpclmulqdq      $0x00, \T5, \T6, \T6

                vmovdqu 16*3(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP2(%rsp), \T1
        vmovdqu         HashKey_7(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_7_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*4(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        #######################################################################

        vmovdqa         TMP3(%rsp), \T1
        vmovdqu         HashKey_6(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_6_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*5(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP4(%rsp), \T1
        vmovdqu         HashKey_5(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_5_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*6(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8


        vmovdqa         TMP5(%rsp), \T1
        vmovdqu         HashKey_4(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_4_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*7(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP6(%rsp), \T1
        vmovdqu         HashKey_3(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_3_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6


                vmovdqu 16*8(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP7(%rsp), \T1
        vmovdqu         HashKey_2(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_2_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6

        #######################################################################

                vmovdqu 16*9(arg1), \T5
                vaesenc \T5, \XMM1, \XMM1
                vaesenc \T5, \XMM2, \XMM2
                vaesenc \T5, \XMM3, \XMM3
                vaesenc \T5, \XMM4, \XMM4
                vaesenc \T5, \XMM5, \XMM5
                vaesenc \T5, \XMM6, \XMM6
                vaesenc \T5, \XMM7, \XMM7
                vaesenc \T5, \XMM8, \XMM8

        vmovdqa         TMP8(%rsp), \T1
        vmovdqu         HashKey(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4
        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpshufd         $0b01001110, \T1, \T3
        vpxor           \T1, \T3, \T3
        vmovdqu         HashKey_k(arg2), \T5
        vpclmulqdq      $0x10, \T5, \T3, \T3
        vpxor           \T3, \T6, \T6

        vpxor           \T4, \T6, \T6
        vpxor           \T7, \T6, \T6

                vmovdqu 16*10(arg1), \T5

        i = 11
        setreg
.rep (\REP-9)

        vaesenc \T5, \XMM1, \XMM1
        vaesenc \T5, \XMM2, \XMM2
        vaesenc \T5, \XMM3, \XMM3
        vaesenc \T5, \XMM4, \XMM4
        vaesenc \T5, \XMM5, \XMM5
        vaesenc \T5, \XMM6, \XMM6
        vaesenc \T5, \XMM7, \XMM7
        vaesenc \T5, \XMM8, \XMM8

        vmovdqu 16*i(arg1), \T5
        i = i + 1
        setreg
.endr

	i = 0
	j = 1
	setreg
.rep 8
		vpxor	16*i(arg4, %r11), \T5, \T2
                .if \ENC_DEC == ENC
                vaesenclast     \T2, reg_j, reg_j
                .else
                vaesenclast     \T2, reg_j, \T3
                vmovdqu 16*i(arg4, %r11), reg_j
                vmovdqu \T3, 16*i(arg3, %r11)
                .endif
	i = (i+1)
	j = (j+1)
	setreg
.endr
	#######################################################################


	vpslldq	$8, \T6, \T3				# shift-L T3 2 DWs
	vpsrldq	$8, \T6, \T6				# shift-R T2 2 DWs
	vpxor	\T3, \T7, \T7
	vpxor	\T4, \T6, \T6				# accumulate the results in T6:T7



	#######################################################################
	#first phase of the reduction
	#######################################################################
        vpslld  $31, \T7, \T2                           # packed right shifting << 31
        vpslld  $30, \T7, \T3                           # packed right shifting shift << 30
        vpslld  $25, \T7, \T4                           # packed right shifting shift << 25

        vpxor   \T3, \T2, \T2                           # xor the shifted versions
        vpxor   \T4, \T2, \T2

        vpsrldq $4, \T2, \T1                            # shift-R T1 1 DW

        vpslldq $12, \T2, \T2                           # shift-L T2 3 DWs
        vpxor   \T2, \T7, \T7                           # first phase of the reduction complete
	#######################################################################
                .if \ENC_DEC == ENC
		vmovdqu	 \XMM1,	16*0(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM2,	16*1(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM3,	16*2(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM4,	16*3(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM5,	16*4(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM6,	16*5(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM7,	16*6(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM8,	16*7(arg3,%r11)		# Write to the Ciphertext buffer
                .endif

	#######################################################################
	#second phase of the reduction
        vpsrld  $1, \T7, \T2                            # packed left shifting >> 1
        vpsrld  $2, \T7, \T3                            # packed left shifting >> 2
        vpsrld  $7, \T7, \T4                            # packed left shifting >> 7
        vpxor   \T3, \T2, \T2                           # xor the shifted versions
        vpxor   \T4, \T2, \T2

        vpxor   \T1, \T2, \T2
        vpxor   \T2, \T7, \T7
        vpxor   \T7, \T6, \T6                           # the result is in T6
	#######################################################################

		vpshufb	SHUF_MASK(%rip), \XMM1, \XMM1	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM2, \XMM2	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM3, \XMM3	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM4, \XMM4	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM5, \XMM5	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM6, \XMM6	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM7, \XMM7	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM8, \XMM8	# perform a 16Byte swap


	vpxor	\T6, \XMM1, \XMM1



.endm


# GHASH the last 4 ciphertext blocks.
.macro  GHASH_LAST_8_AVX T1 T2 T3 T4 T5 T6 T7 XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8

        ## Karatsuba Method


        vpshufd         $0b01001110, \XMM1, \T2
        vpxor           \XMM1, \T2, \T2
        vmovdqu         HashKey_8(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM1, \T6
        vpclmulqdq      $0x00, \T5, \XMM1, \T7

        vmovdqu         HashKey_8_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM2, \T2
        vpxor           \XMM2, \T2, \T2
        vmovdqu         HashKey_7(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM2, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM2, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_7_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2
        vpxor           \T2, \XMM1, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM3, \T2
        vpxor           \XMM3, \T2, \T2
        vmovdqu         HashKey_6(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM3, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM3, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_6_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2
        vpxor           \T2, \XMM1, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM4, \T2
        vpxor           \XMM4, \T2, \T2
        vmovdqu         HashKey_5(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM4, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM4, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_5_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2
        vpxor           \T2, \XMM1, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM5, \T2
        vpxor           \XMM5, \T2, \T2
        vmovdqu         HashKey_4(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM5, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM5, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_4_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2
        vpxor           \T2, \XMM1, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM6, \T2
        vpxor           \XMM6, \T2, \T2
        vmovdqu         HashKey_3(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM6, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM6, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_3_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2
        vpxor           \T2, \XMM1, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM7, \T2
        vpxor           \XMM7, \T2, \T2
        vmovdqu         HashKey_2(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM7, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM7, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_2_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2
        vpxor           \T2, \XMM1, \XMM1

        ######################

        vpshufd         $0b01001110, \XMM8, \T2
        vpxor           \XMM8, \T2, \T2
        vmovdqu         HashKey(arg2), \T5
        vpclmulqdq      $0x11, \T5, \XMM8, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM8, \T4
        vpxor           \T4, \T7, \T7

        vmovdqu         HashKey_k(arg2), \T3
        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1
        vpxor           \T6, \XMM1, \XMM1
        vpxor           \T7, \XMM1, \T2




        vpslldq $8, \T2, \T4
        vpsrldq $8, \T2, \T2

        vpxor   \T4, \T7, \T7
        vpxor   \T2, \T6, \T6   # <T6:T7> holds the result of
				# the accumulated carry-less multiplications

        #######################################################################
        #first phase of the reduction
        vpslld  $31, \T7, \T2   # packed right shifting << 31
        vpslld  $30, \T7, \T3   # packed right shifting shift << 30
        vpslld  $25, \T7, \T4   # packed right shifting shift << 25

        vpxor   \T3, \T2, \T2   # xor the shifted versions
        vpxor   \T4, \T2, \T2

        vpsrldq $4, \T2, \T1    # shift-R T1 1 DW

        vpslldq $12, \T2, \T2   # shift-L T2 3 DWs
        vpxor   \T2, \T7, \T7   # first phase of the reduction complete
        #######################################################################


        #second phase of the reduction
        vpsrld  $1, \T7, \T2    # packed left shifting >> 1
        vpsrld  $2, \T7, \T3    # packed left shifting >> 2
        vpsrld  $7, \T7, \T4    # packed left shifting >> 7
        vpxor   \T3, \T2, \T2   # xor the shifted versions
        vpxor   \T4, \T2, \T2

        vpxor   \T1, \T2, \T2
        vpxor   \T2, \T7, \T7
        vpxor   \T7, \T6, \T6   # the result is in T6

.endm

#############################################################
#void   aesni_gcm_precomp_avx_gen2
#        (gcm_data     *my_ctx_data,
#         gcm_context_data *data,
#        u8     *hash_subkey# /* H, the Hash sub key input. Data starts on a 16-byte boundary. */
#        u8      *iv, /* Pre-counter block j0: 4 byte salt
#			(from Security Association) concatenated with 8 byte
#			Initialisation Vector (from IPSec ESP Payload)
#			concatenated with 0x00000001. 16-byte aligned pointer. */
#        const   u8 *aad, /* Additional Authentication Data (AAD)*/
#        u64     aad_len) /* Length of AAD in bytes. With RFC4106 this is going to be 8 or 12 Bytes */
#############################################################
SYM_FUNC_START(aesni_gcm_init_avx_gen2)
        FUNC_SAVE
        INIT GHASH_MUL_AVX, PRECOMPUTE_AVX
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_init_avx_gen2)

###############################################################################
#void   aesni_gcm_enc_update_avx_gen2(
#        gcm_data        *my_ctx_data,     /* aligned to 16 Bytes */
#        gcm_context_data *data,
#        u8      *out, /* Ciphertext output. Encrypt in-place is allowed.  */
#        const   u8 *in, /* Plaintext input */
#        u64     plaintext_len) /* Length of data in Bytes for encryption. */
###############################################################################
SYM_FUNC_START(aesni_gcm_enc_update_avx_gen2)
        FUNC_SAVE
        mov     keysize, %eax
        cmp     $32, %eax
        je      key_256_enc_update
        cmp     $16, %eax
        je      key_128_enc_update
        # must be 192
        GCM_ENC_DEC INITIAL_BLOCKS_AVX, GHASH_8_ENCRYPT_8_PARALLEL_AVX, GHASH_LAST_8_AVX, GHASH_MUL_AVX, ENC, 11
        FUNC_RESTORE
        ret
key_128_enc_update:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX, GHASH_8_ENCRYPT_8_PARALLEL_AVX, GHASH_LAST_8_AVX, GHASH_MUL_AVX, ENC, 9
        FUNC_RESTORE
        ret
key_256_enc_update:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX, GHASH_8_ENCRYPT_8_PARALLEL_AVX, GHASH_LAST_8_AVX, GHASH_MUL_AVX, ENC, 13
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_enc_update_avx_gen2)

###############################################################################
#void   aesni_gcm_dec_update_avx_gen2(
#        gcm_data        *my_ctx_data,     /* aligned to 16 Bytes */
#        gcm_context_data *data,
#        u8      *out, /* Plaintext output. Decrypt in-place is allowed.  */
#        const   u8 *in, /* Ciphertext input */
#        u64     plaintext_len) /* Length of data in Bytes for encryption. */
###############################################################################
SYM_FUNC_START(aesni_gcm_dec_update_avx_gen2)
        FUNC_SAVE
        mov     keysize,%eax
        cmp     $32, %eax
        je      key_256_dec_update
        cmp     $16, %eax
        je      key_128_dec_update
        # must be 192
        GCM_ENC_DEC INITIAL_BLOCKS_AVX, GHASH_8_ENCRYPT_8_PARALLEL_AVX, GHASH_LAST_8_AVX, GHASH_MUL_AVX, DEC, 11
        FUNC_RESTORE
        ret
key_128_dec_update:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX, GHASH_8_ENCRYPT_8_PARALLEL_AVX, GHASH_LAST_8_AVX, GHASH_MUL_AVX, DEC, 9
        FUNC_RESTORE
        ret
key_256_dec_update:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX, GHASH_8_ENCRYPT_8_PARALLEL_AVX, GHASH_LAST_8_AVX, GHASH_MUL_AVX, DEC, 13
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_dec_update_avx_gen2)

###############################################################################
#void   aesni_gcm_finalize_avx_gen2(
#        gcm_data        *my_ctx_data,     /* aligned to 16 Bytes */
#        gcm_context_data *data,
#        u8      *auth_tag, /* Authenticated Tag output. */
#        u64     auth_tag_len)# /* Authenticated Tag Length in bytes.
#				Valid values are 16 (most likely), 12 or 8. */
###############################################################################
SYM_FUNC_START(aesni_gcm_finalize_avx_gen2)
        FUNC_SAVE
        mov	keysize,%eax
        cmp     $32, %eax
        je      key_256_finalize
        cmp     $16, %eax
        je      key_128_finalize
        # must be 192
        GCM_COMPLETE GHASH_MUL_AVX, 11, arg3, arg4
        FUNC_RESTORE
        ret
key_128_finalize:
        GCM_COMPLETE GHASH_MUL_AVX, 9, arg3, arg4
        FUNC_RESTORE
        ret
key_256_finalize:
        GCM_COMPLETE GHASH_MUL_AVX, 13, arg3, arg4
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_finalize_avx_gen2)

###############################################################################
# GHASH_MUL MACRO to implement: Data*HashKey mod (128,127,126,121,0)
# Input: A and B (128-bits each, bit-reflected)
# Output: C = A*B*x mod poly, (i.e. >>1 )
# To compute GH = GH*HashKey mod poly, give HK = HashKey<<1 mod poly as input
# GH = GH * HK * x mod poly which is equivalent to GH*HashKey mod poly.
###############################################################################
.macro  GHASH_MUL_AVX2 GH HK T1 T2 T3 T4 T5

        vpclmulqdq      $0x11,\HK,\GH,\T1      # T1 = a1*b1
        vpclmulqdq      $0x00,\HK,\GH,\T2      # T2 = a0*b0
        vpclmulqdq      $0x01,\HK,\GH,\T3      # T3 = a1*b0
        vpclmulqdq      $0x10,\HK,\GH,\GH      # GH = a0*b1
        vpxor           \T3, \GH, \GH


        vpsrldq         $8 , \GH, \T3          # shift-R GH 2 DWs
        vpslldq         $8 , \GH, \GH          # shift-L GH 2 DWs

        vpxor           \T3, \T1, \T1
        vpxor           \T2, \GH, \GH

        #######################################################################
        #first phase of the reduction
        vmovdqa         POLY2(%rip), \T3

        vpclmulqdq      $0x01, \GH, \T3, \T2
        vpslldq         $8, \T2, \T2           # shift-L T2 2 DWs

        vpxor           \T2, \GH, \GH          # first phase of the reduction complete
        #######################################################################
        #second phase of the reduction
        vpclmulqdq      $0x00, \GH, \T3, \T2
        vpsrldq         $4, \T2, \T2           # shift-R T2 1 DW (Shift-R only 1-DW to obtain 2-DWs shift-R)

        vpclmulqdq      $0x10, \GH, \T3, \GH
        vpslldq         $4, \GH, \GH           # shift-L GH 1 DW (Shift-L 1-DW to obtain result with no shifts)

        vpxor           \T2, \GH, \GH          # second phase of the reduction complete
        #######################################################################
        vpxor           \T1, \GH, \GH          # the result is in GH


.endm

.macro PRECOMPUTE_AVX2 HK T1 T2 T3 T4 T5 T6

        # Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
        vmovdqa  \HK, \T5
        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^2<<1 mod poly
        vmovdqu  \T5, HashKey_2(arg2)                       #  [HashKey_2] = HashKey^2<<1 mod poly

        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^3<<1 mod poly
        vmovdqu  \T5, HashKey_3(arg2)

        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^4<<1 mod poly
        vmovdqu  \T5, HashKey_4(arg2)

        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^5<<1 mod poly
        vmovdqu  \T5, HashKey_5(arg2)

        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^6<<1 mod poly
        vmovdqu  \T5, HashKey_6(arg2)

        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^7<<1 mod poly
        vmovdqu  \T5, HashKey_7(arg2)

        GHASH_MUL_AVX2 \T5, \HK, \T1, \T3, \T4, \T6, \T2    #  T5 = HashKey^8<<1 mod poly
        vmovdqu  \T5, HashKey_8(arg2)

.endm

## if a = number of total plaintext bytes
## b = floor(a/16)
## num_initial_blocks = b mod 4#
## encrypt the initial num_initial_blocks blocks and apply ghash on the ciphertext
## r10, r11, r12, rax are clobbered
## arg1, arg3, arg4, r14 are used as a pointer only, not modified

.macro INITIAL_BLOCKS_AVX2 REP num_initial_blocks T1 T2 T3 T4 T5 CTR XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8 T6 T_key ENC_DEC VER
	i = (8-\num_initial_blocks)
	setreg
	vmovdqu AadHash(arg2), reg_i

	# start AES for num_initial_blocks blocks
	vmovdqu CurCount(arg2), \CTR

	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
                vpaddd  ONE(%rip), \CTR, \CTR   # INCR Y0
                vmovdqa \CTR, reg_i
                vpshufb SHUF_MASK(%rip), reg_i, reg_i     # perform a 16Byte swap
	i = (i+1)
	setreg
.endr

	vmovdqa  (arg1), \T_key
	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
                vpxor   \T_key, reg_i, reg_i
	i = (i+1)
	setreg
.endr

	j = 1
	setreg
.rep \REP
	vmovdqa  16*j(arg1), \T_key
	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
        vaesenc \T_key, reg_i, reg_i
	i = (i+1)
	setreg
.endr

	j = (j+1)
	setreg
.endr


	vmovdqa  16*j(arg1), \T_key
	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
        vaesenclast      \T_key, reg_i, reg_i
	i = (i+1)
	setreg
.endr

	i = (9-\num_initial_blocks)
	setreg
.rep \num_initial_blocks
                vmovdqu (arg4, %r11), \T1
                vpxor   \T1, reg_i, reg_i
                vmovdqu reg_i, (arg3 , %r11)           # write back ciphertext for
						       # num_initial_blocks blocks
                add     $16, %r11
.if  \ENC_DEC == DEC
                vmovdqa \T1, reg_i
.endif
                vpshufb SHUF_MASK(%rip), reg_i, reg_i  # prepare ciphertext for GHASH computations
	i = (i+1)
	setreg
.endr


	i = (8-\num_initial_blocks)
	j = (9-\num_initial_blocks)
	setreg

.rep \num_initial_blocks
        vpxor    reg_i, reg_j, reg_j
        GHASH_MUL_AVX2       reg_j, \T2, \T1, \T3, \T4, \T5, \T6  # apply GHASH on num_initial_blocks blocks
	i = (i+1)
	j = (j+1)
	setreg
.endr
        # XMM8 has the combined result here

        vmovdqa  \XMM8, TMP1(%rsp)
        vmovdqa  \XMM8, \T3

        cmp     $128, %r13
        jl      _initial_blocks_done\@                  # no need for precomputed constants

###############################################################################
# Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM1
                vpshufb  SHUF_MASK(%rip), \XMM1, \XMM1  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM2
                vpshufb  SHUF_MASK(%rip), \XMM2, \XMM2  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM3
                vpshufb  SHUF_MASK(%rip), \XMM3, \XMM3  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM4
                vpshufb  SHUF_MASK(%rip), \XMM4, \XMM4  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM5
                vpshufb  SHUF_MASK(%rip), \XMM5, \XMM5  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM6
                vpshufb  SHUF_MASK(%rip), \XMM6, \XMM6  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM7
                vpshufb  SHUF_MASK(%rip), \XMM7, \XMM7  # perform a 16Byte swap

                vpaddd   ONE(%rip), \CTR, \CTR          # INCR Y0
                vmovdqa  \CTR, \XMM8
                vpshufb  SHUF_MASK(%rip), \XMM8, \XMM8  # perform a 16Byte swap

                vmovdqa  (arg1), \T_key
                vpxor    \T_key, \XMM1, \XMM1
                vpxor    \T_key, \XMM2, \XMM2
                vpxor    \T_key, \XMM3, \XMM3
                vpxor    \T_key, \XMM4, \XMM4
                vpxor    \T_key, \XMM5, \XMM5
                vpxor    \T_key, \XMM6, \XMM6
                vpxor    \T_key, \XMM7, \XMM7
                vpxor    \T_key, \XMM8, \XMM8

		i = 1
		setreg
.rep    \REP       # do REP rounds
                vmovdqa  16*i(arg1), \T_key
                vaesenc  \T_key, \XMM1, \XMM1
                vaesenc  \T_key, \XMM2, \XMM2
                vaesenc  \T_key, \XMM3, \XMM3
                vaesenc  \T_key, \XMM4, \XMM4
                vaesenc  \T_key, \XMM5, \XMM5
                vaesenc  \T_key, \XMM6, \XMM6
                vaesenc  \T_key, \XMM7, \XMM7
                vaesenc  \T_key, \XMM8, \XMM8
		i = (i+1)
		setreg
.endr


                vmovdqa  16*i(arg1), \T_key
                vaesenclast  \T_key, \XMM1, \XMM1
                vaesenclast  \T_key, \XMM2, \XMM2
                vaesenclast  \T_key, \XMM3, \XMM3
                vaesenclast  \T_key, \XMM4, \XMM4
                vaesenclast  \T_key, \XMM5, \XMM5
                vaesenclast  \T_key, \XMM6, \XMM6
                vaesenclast  \T_key, \XMM7, \XMM7
                vaesenclast  \T_key, \XMM8, \XMM8

                vmovdqu  (arg4, %r11), \T1
                vpxor    \T1, \XMM1, \XMM1
                vmovdqu  \XMM1, (arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM1
                .endif

                vmovdqu  16*1(arg4, %r11), \T1
                vpxor    \T1, \XMM2, \XMM2
                vmovdqu  \XMM2, 16*1(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM2
                .endif

                vmovdqu  16*2(arg4, %r11), \T1
                vpxor    \T1, \XMM3, \XMM3
                vmovdqu  \XMM3, 16*2(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM3
                .endif

                vmovdqu  16*3(arg4, %r11), \T1
                vpxor    \T1, \XMM4, \XMM4
                vmovdqu  \XMM4, 16*3(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM4
                .endif

                vmovdqu  16*4(arg4, %r11), \T1
                vpxor    \T1, \XMM5, \XMM5
                vmovdqu  \XMM5, 16*4(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM5
                .endif

                vmovdqu  16*5(arg4, %r11), \T1
                vpxor    \T1, \XMM6, \XMM6
                vmovdqu  \XMM6, 16*5(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM6
                .endif

                vmovdqu  16*6(arg4, %r11), \T1
                vpxor    \T1, \XMM7, \XMM7
                vmovdqu  \XMM7, 16*6(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM7
                .endif

                vmovdqu  16*7(arg4, %r11), \T1
                vpxor    \T1, \XMM8, \XMM8
                vmovdqu  \XMM8, 16*7(arg3 , %r11)
                .if   \ENC_DEC == DEC
                vmovdqa  \T1, \XMM8
                .endif

                add     $128, %r11

                vpshufb  SHUF_MASK(%rip), \XMM1, \XMM1     # perform a 16Byte swap
                vpxor    TMP1(%rsp), \XMM1, \XMM1          # combine GHASHed value with
							   # the corresponding ciphertext
                vpshufb  SHUF_MASK(%rip), \XMM2, \XMM2     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM3, \XMM3     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM4, \XMM4     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM5, \XMM5     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM6, \XMM6     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM7, \XMM7     # perform a 16Byte swap
                vpshufb  SHUF_MASK(%rip), \XMM8, \XMM8     # perform a 16Byte swap

###############################################################################

_initial_blocks_done\@:


.endm



# encrypt 8 blocks at a time
# ghash the 8 previously encrypted ciphertext blocks
# arg1, arg3, arg4 are used as pointers only, not modified
# r11 is the data offset value
.macro GHASH_8_ENCRYPT_8_PARALLEL_AVX2 REP T1 T2 T3 T4 T5 T6 CTR XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8 T7 loop_idx ENC_DEC

        vmovdqa \XMM1, \T2
        vmovdqa \XMM2, TMP2(%rsp)
        vmovdqa \XMM3, TMP3(%rsp)
        vmovdqa \XMM4, TMP4(%rsp)
        vmovdqa \XMM5, TMP5(%rsp)
        vmovdqa \XMM6, TMP6(%rsp)
        vmovdqa \XMM7, TMP7(%rsp)
        vmovdqa \XMM8, TMP8(%rsp)

.if \loop_idx == in_order
                vpaddd  ONE(%rip), \CTR, \XMM1            # INCR CNT
                vpaddd  ONE(%rip), \XMM1, \XMM2
                vpaddd  ONE(%rip), \XMM2, \XMM3
                vpaddd  ONE(%rip), \XMM3, \XMM4
                vpaddd  ONE(%rip), \XMM4, \XMM5
                vpaddd  ONE(%rip), \XMM5, \XMM6
                vpaddd  ONE(%rip), \XMM6, \XMM7
                vpaddd  ONE(%rip), \XMM7, \XMM8
                vmovdqa \XMM8, \CTR

                vpshufb SHUF_MASK(%rip), \XMM1, \XMM1     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM2, \XMM2     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM3, \XMM3     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM4, \XMM4     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM5, \XMM5     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM6, \XMM6     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM7, \XMM7     # perform a 16Byte swap
                vpshufb SHUF_MASK(%rip), \XMM8, \XMM8     # perform a 16Byte swap
.else
                vpaddd  ONEf(%rip), \CTR, \XMM1            # INCR CNT
                vpaddd  ONEf(%rip), \XMM1, \XMM2
                vpaddd  ONEf(%rip), \XMM2, \XMM3
                vpaddd  ONEf(%rip), \XMM3, \XMM4
                vpaddd  ONEf(%rip), \XMM4, \XMM5
                vpaddd  ONEf(%rip), \XMM5, \XMM6
                vpaddd  ONEf(%rip), \XMM6, \XMM7
                vpaddd  ONEf(%rip), \XMM7, \XMM8
                vmovdqa \XMM8, \CTR
.endif


        #######################################################################

                vmovdqu (arg1), \T1
                vpxor   \T1, \XMM1, \XMM1
                vpxor   \T1, \XMM2, \XMM2
                vpxor   \T1, \XMM3, \XMM3
                vpxor   \T1, \XMM4, \XMM4
                vpxor   \T1, \XMM5, \XMM5
                vpxor   \T1, \XMM6, \XMM6
                vpxor   \T1, \XMM7, \XMM7
                vpxor   \T1, \XMM8, \XMM8

        #######################################################################





                vmovdqu 16*1(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

                vmovdqu 16*2(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8


        #######################################################################

        vmovdqu         HashKey_8(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T2, \T4              # T4 = a1*b1
        vpclmulqdq      $0x00, \T5, \T2, \T7              # T7 = a0*b0
        vpclmulqdq      $0x01, \T5, \T2, \T6              # T6 = a1*b0
        vpclmulqdq      $0x10, \T5, \T2, \T5              # T5 = a0*b1
        vpxor           \T5, \T6, \T6

                vmovdqu 16*3(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP2(%rsp), \T1
        vmovdqu         HashKey_7(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*4(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        #######################################################################

        vmovdqa         TMP3(%rsp), \T1
        vmovdqu         HashKey_6(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*5(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP4(%rsp), \T1
        vmovdqu         HashKey_5(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*6(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8


        vmovdqa         TMP5(%rsp), \T1
        vmovdqu         HashKey_4(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*7(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP6(%rsp), \T1
        vmovdqu         HashKey_3(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

                vmovdqu 16*8(arg1), \T1
                vaesenc \T1, \XMM1, \XMM1
                vaesenc \T1, \XMM2, \XMM2
                vaesenc \T1, \XMM3, \XMM3
                vaesenc \T1, \XMM4, \XMM4
                vaesenc \T1, \XMM5, \XMM5
                vaesenc \T1, \XMM6, \XMM6
                vaesenc \T1, \XMM7, \XMM7
                vaesenc \T1, \XMM8, \XMM8

        vmovdqa         TMP7(%rsp), \T1
        vmovdqu         HashKey_2(arg2), \T5
        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T4

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6


        #######################################################################

                vmovdqu 16*9(arg1), \T5
                vaesenc \T5, \XMM1, \XMM1
                vaesenc \T5, \XMM2, \XMM2
                vaesenc \T5, \XMM3, \XMM3
                vaesenc \T5, \XMM4, \XMM4
                vaesenc \T5, \XMM5, \XMM5
                vaesenc \T5, \XMM6, \XMM6
                vaesenc \T5, \XMM7, \XMM7
                vaesenc \T5, \XMM8, \XMM8

        vmovdqa         TMP8(%rsp), \T1
        vmovdqu         HashKey(arg2), \T5

        vpclmulqdq      $0x00, \T5, \T1, \T3
        vpxor           \T3, \T7, \T7

        vpclmulqdq      $0x01, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x10, \T5, \T1, \T3
        vpxor           \T3, \T6, \T6

        vpclmulqdq      $0x11, \T5, \T1, \T3
        vpxor           \T3, \T4, \T1


                vmovdqu 16*10(arg1), \T5

        i = 11
        setreg
.rep (\REP-9)
        vaesenc \T5, \XMM1, \XMM1
        vaesenc \T5, \XMM2, \XMM2
        vaesenc \T5, \XMM3, \XMM3
        vaesenc \T5, \XMM4, \XMM4
        vaesenc \T5, \XMM5, \XMM5
        vaesenc \T5, \XMM6, \XMM6
        vaesenc \T5, \XMM7, \XMM7
        vaesenc \T5, \XMM8, \XMM8

        vmovdqu 16*i(arg1), \T5
        i = i + 1
        setreg
.endr

	i = 0
	j = 1
	setreg
.rep 8
		vpxor	16*i(arg4, %r11), \T5, \T2
                .if \ENC_DEC == ENC
                vaesenclast     \T2, reg_j, reg_j
                .else
                vaesenclast     \T2, reg_j, \T3
                vmovdqu 16*i(arg4, %r11), reg_j
                vmovdqu \T3, 16*i(arg3, %r11)
                .endif
	i = (i+1)
	j = (j+1)
	setreg
.endr
	#######################################################################


	vpslldq	$8, \T6, \T3				# shift-L T3 2 DWs
	vpsrldq	$8, \T6, \T6				# shift-R T2 2 DWs
	vpxor	\T3, \T7, \T7
	vpxor	\T6, \T1, \T1				# accumulate the results in T1:T7



	#######################################################################
	#first phase of the reduction
	vmovdqa         POLY2(%rip), \T3

	vpclmulqdq	$0x01, \T7, \T3, \T2
	vpslldq		$8, \T2, \T2			# shift-L xmm2 2 DWs

	vpxor		\T2, \T7, \T7			# first phase of the reduction complete
	#######################################################################
                .if \ENC_DEC == ENC
		vmovdqu	 \XMM1,	16*0(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM2,	16*1(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM3,	16*2(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM4,	16*3(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM5,	16*4(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM6,	16*5(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM7,	16*6(arg3,%r11)		# Write to the Ciphertext buffer
		vmovdqu	 \XMM8,	16*7(arg3,%r11)		# Write to the Ciphertext buffer
                .endif

	#######################################################################
	#second phase of the reduction
	vpclmulqdq	$0x00, \T7, \T3, \T2
	vpsrldq		$4, \T2, \T2			# shift-R xmm2 1 DW (Shift-R only 1-DW to obtain 2-DWs shift-R)

	vpclmulqdq	$0x10, \T7, \T3, \T4
	vpslldq		$4, \T4, \T4			# shift-L xmm0 1 DW (Shift-L 1-DW to obtain result with no shifts)

	vpxor		\T2, \T4, \T4			# second phase of the reduction complete
	#######################################################################
	vpxor		\T4, \T1, \T1			# the result is in T1

		vpshufb	SHUF_MASK(%rip), \XMM1, \XMM1	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM2, \XMM2	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM3, \XMM3	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM4, \XMM4	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM5, \XMM5	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM6, \XMM6	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM7, \XMM7	# perform a 16Byte swap
		vpshufb	SHUF_MASK(%rip), \XMM8, \XMM8	# perform a 16Byte swap


	vpxor	\T1, \XMM1, \XMM1



.endm


# GHASH the last 4 ciphertext blocks.
.macro  GHASH_LAST_8_AVX2 T1 T2 T3 T4 T5 T6 T7 XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8

        ## Karatsuba Method

        vmovdqu         HashKey_8(arg2), \T5

        vpshufd         $0b01001110, \XMM1, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM1, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM1, \T6
        vpclmulqdq      $0x00, \T5, \XMM1, \T7

        vpclmulqdq      $0x00, \T3, \T2, \XMM1

        ######################

        vmovdqu         HashKey_7(arg2), \T5
        vpshufd         $0b01001110, \XMM2, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM2, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM2, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM2, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1

        ######################

        vmovdqu         HashKey_6(arg2), \T5
        vpshufd         $0b01001110, \XMM3, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM3, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM3, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM3, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1

        ######################

        vmovdqu         HashKey_5(arg2), \T5
        vpshufd         $0b01001110, \XMM4, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM4, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM4, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM4, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1

        ######################

        vmovdqu         HashKey_4(arg2), \T5
        vpshufd         $0b01001110, \XMM5, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM5, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM5, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM5, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1

        ######################

        vmovdqu         HashKey_3(arg2), \T5
        vpshufd         $0b01001110, \XMM6, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM6, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM6, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM6, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1

        ######################

        vmovdqu         HashKey_2(arg2), \T5
        vpshufd         $0b01001110, \XMM7, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM7, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM7, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM7, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1

        ######################

        vmovdqu         HashKey(arg2), \T5
        vpshufd         $0b01001110, \XMM8, \T2
        vpshufd         $0b01001110, \T5, \T3
        vpxor           \XMM8, \T2, \T2
        vpxor           \T5, \T3, \T3

        vpclmulqdq      $0x11, \T5, \XMM8, \T4
        vpxor           \T4, \T6, \T6

        vpclmulqdq      $0x00, \T5, \XMM8, \T4
        vpxor           \T4, \T7, \T7

        vpclmulqdq      $0x00, \T3, \T2, \T2

        vpxor           \T2, \XMM1, \XMM1
        vpxor           \T6, \XMM1, \XMM1
        vpxor           \T7, \XMM1, \T2




        vpslldq $8, \T2, \T4
        vpsrldq $8, \T2, \T2

        vpxor   \T4, \T7, \T7
        vpxor   \T2, \T6, \T6                      # <T6:T7> holds the result of the
						   # accumulated carry-less multiplications

        #######################################################################
        #first phase of the reduction
        vmovdqa         POLY2(%rip), \T3

        vpclmulqdq      $0x01, \T7, \T3, \T2
        vpslldq         $8, \T2, \T2               # shift-L xmm2 2 DWs

        vpxor           \T2, \T7, \T7              # first phase of the reduction complete
        #######################################################################


        #second phase of the reduction
        vpclmulqdq      $0x00, \T7, \T3, \T2
        vpsrldq         $4, \T2, \T2               # shift-R T2 1 DW (Shift-R only 1-DW to obtain 2-DWs shift-R)

        vpclmulqdq      $0x10, \T7, \T3, \T4
        vpslldq         $4, \T4, \T4               # shift-L T4 1 DW (Shift-L 1-DW to obtain result with no shifts)

        vpxor           \T2, \T4, \T4              # second phase of the reduction complete
        #######################################################################
        vpxor           \T4, \T6, \T6              # the result is in T6
.endm



#############################################################
#void   aesni_gcm_init_avx_gen4
#        (gcm_data     *my_ctx_data,
#         gcm_context_data *data,
#        u8      *iv, /* Pre-counter block j0: 4 byte salt
#			(from Security Association) concatenated with 8 byte
#			Initialisation Vector (from IPSec ESP Payload)
#			concatenated with 0x00000001. 16-byte aligned pointer. */
#        u8     *hash_subkey# /* H, the Hash sub key input. Data starts on a 16-byte boundary. */
#        const   u8 *aad, /* Additional Authentication Data (AAD)*/
#        u64     aad_len) /* Length of AAD in bytes. With RFC4106 this is going to be 8 or 12 Bytes */
#############################################################
SYM_FUNC_START(aesni_gcm_init_avx_gen4)
        FUNC_SAVE
        INIT GHASH_MUL_AVX2, PRECOMPUTE_AVX2
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_init_avx_gen4)

###############################################################################
#void   aesni_gcm_enc_avx_gen4(
#        gcm_data        *my_ctx_data,     /* aligned to 16 Bytes */
#        gcm_context_data *data,
#        u8      *out, /* Ciphertext output. Encrypt in-place is allowed.  */
#        const   u8 *in, /* Plaintext input */
#        u64     plaintext_len) /* Length of data in Bytes for encryption. */
###############################################################################
SYM_FUNC_START(aesni_gcm_enc_update_avx_gen4)
        FUNC_SAVE
        mov     keysize,%eax
        cmp     $32, %eax
        je      key_256_enc_update4
        cmp     $16, %eax
        je      key_128_enc_update4
        # must be 192
        GCM_ENC_DEC INITIAL_BLOCKS_AVX2, GHASH_8_ENCRYPT_8_PARALLEL_AVX2, GHASH_LAST_8_AVX2, GHASH_MUL_AVX2, ENC, 11
        FUNC_RESTORE
	ret
key_128_enc_update4:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX2, GHASH_8_ENCRYPT_8_PARALLEL_AVX2, GHASH_LAST_8_AVX2, GHASH_MUL_AVX2, ENC, 9
        FUNC_RESTORE
	ret
key_256_enc_update4:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX2, GHASH_8_ENCRYPT_8_PARALLEL_AVX2, GHASH_LAST_8_AVX2, GHASH_MUL_AVX2, ENC, 13
        FUNC_RESTORE
	ret
SYM_FUNC_END(aesni_gcm_enc_update_avx_gen4)

###############################################################################
#void   aesni_gcm_dec_update_avx_gen4(
#        gcm_data        *my_ctx_data,     /* aligned to 16 Bytes */
#        gcm_context_data *data,
#        u8      *out, /* Plaintext output. Decrypt in-place is allowed.  */
#        const   u8 *in, /* Ciphertext input */
#        u64     plaintext_len) /* Length of data in Bytes for encryption. */
###############################################################################
SYM_FUNC_START(aesni_gcm_dec_update_avx_gen4)
        FUNC_SAVE
        mov     keysize,%eax
        cmp     $32, %eax
        je      key_256_dec_update4
        cmp     $16, %eax
        je      key_128_dec_update4
        # must be 192
        GCM_ENC_DEC INITIAL_BLOCKS_AVX2, GHASH_8_ENCRYPT_8_PARALLEL_AVX2, GHASH_LAST_8_AVX2, GHASH_MUL_AVX2, DEC, 11
        FUNC_RESTORE
        ret
key_128_dec_update4:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX2, GHASH_8_ENCRYPT_8_PARALLEL_AVX2, GHASH_LAST_8_AVX2, GHASH_MUL_AVX2, DEC, 9
        FUNC_RESTORE
        ret
key_256_dec_update4:
        GCM_ENC_DEC INITIAL_BLOCKS_AVX2, GHASH_8_ENCRYPT_8_PARALLEL_AVX2, GHASH_LAST_8_AVX2, GHASH_MUL_AVX2, DEC, 13
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_dec_update_avx_gen4)

###############################################################################
#void   aesni_gcm_finalize_avx_gen4(
#        gcm_data        *my_ctx_data,     /* aligned to 16 Bytes */
#        gcm_context_data *data,
#        u8      *auth_tag, /* Authenticated Tag output. */
#        u64     auth_tag_len)# /* Authenticated Tag Length in bytes.
#                              Valid values are 16 (most likely), 12 or 8. */
###############################################################################
SYM_FUNC_START(aesni_gcm_finalize_avx_gen4)
        FUNC_SAVE
        mov	keysize,%eax
        cmp     $32, %eax
        je      key_256_finalize4
        cmp     $16, %eax
        je      key_128_finalize4
        # must be 192
        GCM_COMPLETE GHASH_MUL_AVX2, 11, arg3, arg4
        FUNC_RESTORE
        ret
key_128_finalize4:
        GCM_COMPLETE GHASH_MUL_AVX2, 9, arg3, arg4
        FUNC_RESTORE
        ret
key_256_finalize4:
        GCM_COMPLETE GHASH_MUL_AVX2, 13, arg3, arg4
        FUNC_RESTORE
        ret
SYM_FUNC_END(aesni_gcm_finalize_avx_gen4)
