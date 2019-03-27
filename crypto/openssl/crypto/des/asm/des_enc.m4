! Copyright 2000-2019 The OpenSSL Project Authors. All Rights Reserved.
!
! Licensed under the OpenSSL license (the "License").  You may not use
! this file except in compliance with the License.  You can obtain a copy
! in the file LICENSE in the source distribution or at
! https://www.openssl.org/source/license.html
!
!  To expand the m4 macros: m4 -B 8192 des_enc.m4 > des_enc.S
!
!  Global registers 1 to 5 are used. This is the same as done by the
!  cc compiler. The UltraSPARC load/store little endian feature is used.
!
!  Instruction grouping often refers to one CPU cycle.
!
!  Assemble through gcc: gcc -c -mcpu=ultrasparc -o des_enc.o des_enc.S
!
!  Assemble through cc:  cc -c -xarch=v8plusa -o des_enc.o des_enc.S
!
!  Performance improvement according to './apps/openssl speed des'
!
!	32-bit build:
!		23%  faster than cc-5.2 -xarch=v8plus -xO5
!		115% faster than gcc-3.2.1 -m32 -mcpu=ultrasparc -O5
!	64-bit build:
!		50%  faster than cc-5.2 -xarch=v9 -xO5
!		100% faster than gcc-3.2.1 -m64 -mcpu=ultrasparc -O5
!

.ident "des_enc.m4 2.1"
.file  "des_enc-sparc.S"

#include <openssl/opensslconf.h>

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
# define	LDPTR	ldx
# define	STPTR	stx
# define	ARG0	128
# define	ARGSZ	8
#else
# define	FRAME	-96
# define	BIAS	0
# define	LDPTR	ld
# define	STPTR	st
# define	ARG0	68
# define	ARGSZ	4
#endif

#define LOOPS 7

#define global0 %g0
#define global1 %g1
#define global2 %g2
#define global3 %g3
#define global4 %g4
#define global5 %g5

#define local0 %l0
#define local1 %l1
#define local2 %l2
#define local3 %l3
#define local4 %l4
#define local5 %l5
#define local7 %l6
#define local6 %l7

#define in0 %i0
#define in1 %i1
#define in2 %i2
#define in3 %i3
#define in4 %i4
#define in5 %i5
#define in6 %i6
#define in7 %i7

#define out0 %o0
#define out1 %o1
#define out2 %o2
#define out3 %o3
#define out4 %o4
#define out5 %o5
#define out6 %o6
#define out7 %o7

#define stub stb

changequote({,})


! Macro definitions:


! {ip_macro}
!
! The logic used in initial and final permutations is the same as in
! the C code. The permutations are done with a clever shift, xor, and
! technique.
!
! The macro also loads address sbox 1 to 5 to global 1 to 5, address
! sbox 6 to local6, and addres sbox 8 to out3.
!
! Rotates the halfs 3 left to bring the sbox bits in convenient positions.
!
! Loads key first round from address in parameter 5 to out0, out1.
!
! After the original LibDES initial permutation, the resulting left
! is in the variable initially used for right and vice versa. The macro
! implements the possibility to keep the halfs in the original registers.
!
! parameter 1  left
! parameter 2  right
! parameter 3  result left (modify in first round)
! parameter 4  result right (use in first round)
! parameter 5  key address
! parameter 6  1/2 for include encryption/decryption
! parameter 7  1 for move in1 to in3
! parameter 8  1 for move in3 to in4, 2 for move in4 to in3
! parameter 9  1 for load ks3 and ks2 to in4 and in3

define(ip_macro, {

! {ip_macro}
! $1 $2 $4 $3 $5 $6 $7 $8 $9

	ld	[out2+256], local1
	srl	$2, 4, local4

	xor	local4, $1, local4
	ifelse($7,1,{mov in1, in3},{nop})

	ld	[out2+260], local2
	and	local4, local1, local4
	ifelse($8,1,{mov in3, in4},{})
	ifelse($8,2,{mov in4, in3},{})

	ld	[out2+280], out4          ! loop counter
	sll	local4, 4, local1
	xor	$1, local4, $1

	ld	[out2+264], local3
	srl	$1, 16, local4
	xor	$2, local1, $2

	ifelse($9,1,{LDPTR	KS3, in4},{})
	xor	local4, $2, local4
	nop	!sethi	%hi(DES_SPtrans), global1 ! sbox addr

	ifelse($9,1,{LDPTR	KS2, in3},{})
	and	local4, local2, local4
	nop	!or	global1, %lo(DES_SPtrans), global1   ! sbox addr

	sll	local4, 16, local1
	xor	$2, local4, $2

	srl	$2, 2, local4
	xor	$1, local1, $1

	sethi	%hi(16711680), local5
	xor	local4, $1, local4

	and	local4, local3, local4
	or	local5, 255, local5

	sll	local4, 2, local2
	xor	$1, local4, $1

	srl	$1, 8, local4
	xor	$2, local2, $2

	xor	local4, $2, local4
	add	global1, 768, global4

	and	local4, local5, local4
	add	global1, 1024, global5

	ld	[out2+272], local7
	sll	local4, 8, local1
	xor	$2, local4, $2

	srl	$2, 1, local4
	xor	$1, local1, $1

	ld	[$5], out0                ! key 7531
	xor	local4, $1, local4
	add	global1, 256, global2

	ld	[$5+4], out1              ! key 8642
	and	local4, local7, local4
	add	global1, 512, global3

	sll	local4, 1, local1
	xor	$1, local4, $1

	sll	$1, 3, local3
	xor	$2, local1, $2

	sll	$2, 3, local2
	add	global1, 1280, local6     ! address sbox 8

	srl	$1, 29, local4
	add	global1, 1792, out3       ! address sbox 8

	srl	$2, 29, local1
	or	local4, local3, $4

	or	local2, local1, $3

	ifelse($6, 1, {

		ld	[out2+284], local5     ! 0x0000FC00 used in the rounds
		or	local2, local1, $3
		xor	$4, out0, local1

		call .des_enc.1
		and	local1, 252, local1

	},{})

	ifelse($6, 2, {

		ld	[out2+284], local5     ! 0x0000FC00 used in the rounds
		or	local2, local1, $3
		xor	$4, out0, local1

		call .des_dec.1
		and	local1, 252, local1

	},{})
})


! {rounds_macro}
!
! The logic used in the DES rounds is the same as in the C code,
! except that calculations for sbox 1 and sbox 5 begin before
! the previous round is finished.
!
! In each round one half (work) is modified based on key and the
! other half (use).
!
! In this version we do two rounds in a loop repeated 7 times
! and two rounds separately.
!
! One half has the bits for the sboxes in the following positions:
!
!	777777xx555555xx333333xx111111xx
!
!	88xx666666xx444444xx222222xx8888
!
! The bits for each sbox are xor-ed with the key bits for that box.
! The above xx bits are cleared, and the result used for lookup in
! the sbox table. Each sbox entry contains the 4 output bits permuted
! into 32 bits according to the P permutation.
!
! In the description of DES, left and right are switched after
! each round, except after last round. In this code the original
! left and right are kept in the same register in all rounds, meaning
! that after the 16 rounds the result for right is in the register
! originally used for left.
!
! parameter 1  first work (left in first round)
! parameter 2  first use (right in first round)
! parameter 3  enc/dec  1/-1
! parameter 4  loop label
! parameter 5  key address register
! parameter 6  optional address for key next encryption/decryption
! parameter 7  not empty for include retl
!
! also compares in2 to 8

define(rounds_macro, {

! {rounds_macro}
! $1 $2 $3 $4 $5 $6 $7 $8 $9

	xor	$2, out0, local1

	ld	[out2+284], local5        ! 0x0000FC00
	ba	$4
	and	local1, 252, local1

	.align 32

$4:
	! local6 is address sbox 6
	! out3   is address sbox 8
	! out4   is loop counter

	ld	[global1+local1], local1
	xor	$2, out1, out1            ! 8642
	xor	$2, out0, out0            ! 7531
	! fmovs	%f0, %f0                  ! fxor used for alignment

	srl	out1, 4, local0           ! rotate 4 right
	and	out0, local5, local3      ! 3
	! fmovs	%f0, %f0

	ld	[$5+$3*8], local7         ! key 7531 next round
	srl	local3, 8, local3         ! 3
	and	local0, 252, local2       ! 2
	! fmovs	%f0, %f0

	ld	[global3+local3],local3   ! 3
	sll	out1, 28, out1            ! rotate
	xor	$1, local1, $1            ! 1 finished, local1 now sbox 7

	ld	[global2+local2], local2  ! 2
	srl	out0, 24, local1          ! 7
	or	out1, local0, out1        ! rotate

	ldub	[out2+local1], local1     ! 7 (and 0xFC)
	srl	out1, 24, local0          ! 8
	and	out1, local5, local4      ! 4

	ldub	[out2+local0], local0     ! 8 (and 0xFC)
	srl	local4, 8, local4         ! 4
	xor	$1, local2, $1            ! 2 finished local2 now sbox 6

	ld	[global4+local4],local4   ! 4
	srl	out1, 16, local2          ! 6
	xor	$1, local3, $1            ! 3 finished local3 now sbox 5

	ld	[out3+local0],local0      ! 8
	and	local2, 252, local2       ! 6
	add	global1, 1536, local5     ! address sbox 7

	ld	[local6+local2], local2   ! 6
	srl	out0, 16, local3          ! 5
	xor	$1, local4, $1            ! 4 finished

	ld	[local5+local1],local1    ! 7
	and	local3, 252, local3       ! 5
	xor	$1, local0, $1            ! 8 finished

	ld	[global5+local3],local3   ! 5
	xor	$1, local2, $1            ! 6 finished
	subcc	out4, 1, out4

	ld	[$5+$3*8+4], out0         ! key 8642 next round
	xor	$1, local7, local2        ! sbox 5 next round
	xor	$1, local1, $1            ! 7 finished

	srl	local2, 16, local2        ! sbox 5 next round
	xor	$1, local3, $1            ! 5 finished

	ld	[$5+$3*16+4], out1        ! key 8642 next round again
	and	local2, 252, local2       ! sbox5 next round
! next round
	xor	$1, local7, local7        ! 7531

	ld	[global5+local2], local2  ! 5
	srl	local7, 24, local3        ! 7
	xor	$1, out0, out0            ! 8642

	ldub	[out2+local3], local3     ! 7 (and 0xFC)
	srl	out0, 4, local0           ! rotate 4 right
	and	local7, 252, local1       ! 1

	sll	out0, 28, out0            ! rotate
	xor	$2, local2, $2            ! 5 finished local2 used

	srl	local0, 8, local4         ! 4
	and	local0, 252, local2       ! 2
	ld	[local5+local3], local3   ! 7

	srl	local0, 16, local5        ! 6
	or	out0, local0, out0        ! rotate
	ld	[global2+local2], local2  ! 2

	srl	out0, 24, local0
	ld	[$5+$3*16], out0          ! key 7531 next round
	and	local4, 252, local4	  ! 4

	and	local5, 252, local5       ! 6
	ld	[global4+local4], local4  ! 4
	xor	$2, local3, $2            ! 7 finished local3 used

	and	local0, 252, local0       ! 8
	ld	[local6+local5], local5   ! 6
	xor	$2, local2, $2            ! 2 finished local2 now sbox 3

	srl	local7, 8, local2         ! 3 start
	ld	[out3+local0], local0     ! 8
	xor	$2, local4, $2            ! 4 finished

	and	local2, 252, local2       ! 3
	ld	[global1+local1], local1  ! 1
	xor	$2, local5, $2            ! 6 finished local5 used

	ld	[global3+local2], local2  ! 3
	xor	$2, local0, $2            ! 8 finished
	add	$5, $3*16, $5             ! enc add 8, dec add -8 to key pointer

	ld	[out2+284], local5        ! 0x0000FC00
	xor	$2, out0, local4          ! sbox 1 next round
	xor	$2, local1, $2            ! 1 finished

	xor	$2, local2, $2            ! 3 finished
	bne	$4
	and	local4, 252, local1       ! sbox 1 next round

! two rounds more:

	ld	[global1+local1], local1
	xor	$2, out1, out1
	xor	$2, out0, out0

	srl	out1, 4, local0           ! rotate
	and	out0, local5, local3

	ld	[$5+$3*8], local7         ! key 7531
	srl	local3, 8, local3
	and	local0, 252, local2

	ld	[global3+local3],local3
	sll	out1, 28, out1            ! rotate
	xor	$1, local1, $1            ! 1 finished, local1 now sbox 7

	ld	[global2+local2], local2
	srl	out0, 24, local1
	or	out1, local0, out1        ! rotate

	ldub	[out2+local1], local1
	srl	out1, 24, local0
	and	out1, local5, local4

	ldub	[out2+local0], local0
	srl	local4, 8, local4
	xor	$1, local2, $1            ! 2 finished local2 now sbox 6

	ld	[global4+local4],local4
	srl	out1, 16, local2
	xor	$1, local3, $1            ! 3 finished local3 now sbox 5

	ld	[out3+local0],local0
	and	local2, 252, local2
	add	global1, 1536, local5     ! address sbox 7

	ld	[local6+local2], local2
	srl	out0, 16, local3
	xor	$1, local4, $1            ! 4 finished

	ld	[local5+local1],local1
	and	local3, 252, local3
	xor	$1, local0, $1

	ld	[global5+local3],local3
	xor	$1, local2, $1            ! 6 finished
	cmp	in2, 8

	ifelse($6,{}, {}, {ld	[out2+280], out4})  ! loop counter
	xor	$1, local7, local2        ! sbox 5 next round
	xor	$1, local1, $1            ! 7 finished

	ld	[$5+$3*8+4], out0
	srl	local2, 16, local2        ! sbox 5 next round
	xor	$1, local3, $1            ! 5 finished

	and	local2, 252, local2
! next round (two rounds more)
	xor	$1, local7, local7        ! 7531

	ld	[global5+local2], local2
	srl	local7, 24, local3
	xor	$1, out0, out0            ! 8642

	ldub	[out2+local3], local3
	srl	out0, 4, local0           ! rotate
	and	local7, 252, local1

	sll	out0, 28, out0            ! rotate
	xor	$2, local2, $2            ! 5 finished local2 used

	srl	local0, 8, local4
	and	local0, 252, local2
	ld	[local5+local3], local3

	srl	local0, 16, local5
	or	out0, local0, out0        ! rotate
	ld	[global2+local2], local2

	srl	out0, 24, local0
	ifelse($6,{}, {}, {ld	[$6], out0})   ! key next encryption/decryption
	and	local4, 252, local4

	and	local5, 252, local5
	ld	[global4+local4], local4
	xor	$2, local3, $2            ! 7 finished local3 used

	and	local0, 252, local0
	ld	[local6+local5], local5
	xor	$2, local2, $2            ! 2 finished local2 now sbox 3

	srl	local7, 8, local2         ! 3 start
	ld	[out3+local0], local0
	xor	$2, local4, $2

	and	local2, 252, local2
	ld	[global1+local1], local1
	xor	$2, local5, $2            ! 6 finished local5 used

	ld	[global3+local2], local2
	srl	$1, 3, local3
	xor	$2, local0, $2

	ifelse($6,{}, {}, {ld	[$6+4], out1}) ! key next encryption/decryption
	sll	$1, 29, local4
	xor	$2, local1, $2

	ifelse($7,{}, {}, {retl})
	xor	$2, local2, $2
})


! {fp_macro}
!
!  parameter 1   right (original left)
!  parameter 2   left (original right)
!  parameter 3   1 for optional store to [in0]
!  parameter 4   1 for load input/output address to local5/7
!
!  The final permutation logic switches the halves, meaning that
!  left and right ends up the registers originally used.

define(fp_macro, {

! {fp_macro}
! $1 $2 $3 $4 $5 $6 $7 $8 $9

	! initially undo the rotate 3 left done after initial permutation
	! original left is received shifted 3 right and 29 left in local3/4

	sll	$2, 29, local1
	or	local3, local4, $1

	srl	$2, 3, $2
	sethi	%hi(0x55555555), local2

	or	$2, local1, $2
	or	local2, %lo(0x55555555), local2

	srl	$2, 1, local3
	sethi	%hi(0x00ff00ff), local1
	xor	local3, $1, local3
	or	local1, %lo(0x00ff00ff), local1
	and	local3, local2, local3
	sethi	%hi(0x33333333), local4
	sll	local3, 1, local2

	xor	$1, local3, $1

	srl	$1, 8, local3
	xor	$2, local2, $2
	xor	local3, $2, local3
	or	local4, %lo(0x33333333), local4
	and	local3, local1, local3
	sethi	%hi(0x0000ffff), local1
	sll	local3, 8, local2

	xor	$2, local3, $2

	srl	$2, 2, local3
	xor	$1, local2, $1
	xor	local3, $1, local3
	or	local1, %lo(0x0000ffff), local1
	and	local3, local4, local3
	sethi	%hi(0x0f0f0f0f), local4
	sll	local3, 2, local2

	ifelse($4,1, {LDPTR INPUT, local5})
	xor	$1, local3, $1

	ifelse($4,1, {LDPTR OUTPUT, local7})
	srl	$1, 16, local3
	xor	$2, local2, $2
	xor	local3, $2, local3
	or	local4, %lo(0x0f0f0f0f), local4
	and	local3, local1, local3
	sll	local3, 16, local2

	xor	$2, local3, local1

	srl	local1, 4, local3
	xor	$1, local2, $1
	xor	local3, $1, local3
	and	local3, local4, local3
	sll	local3, 4, local2

	xor	$1, local3, $1

	! optional store:

	ifelse($3,1, {st $1, [in0]})

	xor	local1, local2, $2

	ifelse($3,1, {st $2, [in0+4]})

})


! {fp_ip_macro}
!
! Does initial permutation for next block mixed with
! final permutation for current block.
!
! parameter 1   original left
! parameter 2   original right
! parameter 3   left ip
! parameter 4   right ip
! parameter 5   1: load ks1/ks2 to in3/in4, add 120 to in4
!                2: mov in4 to in3
!
! also adds -8 to length in2 and loads loop counter to out4

define(fp_ip_macro, {

! {fp_ip_macro}
! $1 $2 $3 $4 $5 $6 $7 $8 $9

	define({temp1},{out4})
	define({temp2},{local3})

	define({ip1},{local1})
	define({ip2},{local2})
	define({ip4},{local4})
	define({ip5},{local5})

	! $1 in local3, local4

	ld	[out2+256], ip1
	sll	out5, 29, temp1
	or	local3, local4, $1

	srl	out5, 3, $2
	ifelse($5,2,{mov in4, in3})

	ld	[out2+272], ip5
	srl	$4, 4, local0
	or	$2, temp1, $2

	srl	$2, 1, temp1
	xor	temp1, $1, temp1

	and	temp1, ip5, temp1
	xor	local0, $3, local0

	sll	temp1, 1, temp2
	xor	$1, temp1, $1

	and	local0, ip1, local0
	add	in2, -8, in2

	sll	local0, 4, local7
	xor	$3, local0, $3

	ld	[out2+268], ip4
	srl	$1, 8, temp1
	xor	$2, temp2, $2
	ld	[out2+260], ip2
	srl	$3, 16, local0
	xor	$4, local7, $4
	xor	temp1, $2, temp1
	xor	local0, $4, local0
	and	temp1, ip4, temp1
	and	local0, ip2, local0
	sll	temp1, 8, temp2
	xor	$2, temp1, $2
	sll	local0, 16, local7
	xor	$4, local0, $4

	srl	$2, 2, temp1
	xor	$1, temp2, $1

	ld	[out2+264], temp2         ! ip3
	srl	$4, 2, local0
	xor	$3, local7, $3
	xor	temp1, $1, temp1
	xor	local0, $3, local0
	and	temp1, temp2, temp1
	and	local0, temp2, local0
	sll	temp1, 2, temp2
	xor	$1, temp1, $1
	sll	local0, 2, local7
	xor	$3, local0, $3

	srl	$1, 16, temp1
	xor	$2, temp2, $2
	srl	$3, 8, local0
	xor	$4, local7, $4
	xor	temp1, $2, temp1
	xor	local0, $4, local0
	and	temp1, ip2, temp1
	and	local0, ip4, local0
	sll	temp1, 16, temp2
	xor	$2, temp1, local4
	sll	local0, 8, local7
	xor	$4, local0, $4

	srl	$4, 1, local0
	xor	$3, local7, $3

	srl	local4, 4, temp1
	xor	local0, $3, local0

	xor	$1, temp2, $1
	and	local0, ip5, local0

	sll	local0, 1, local7
	xor	temp1, $1, temp1

	xor	$3, local0, $3
	xor	$4, local7, $4

	sll	$3, 3, local5
	and	temp1, ip1, temp1

	sll	temp1, 4, temp2
	xor	$1, temp1, $1

	ifelse($5,1,{LDPTR	KS2, in4})
	sll	$4, 3, local2
	xor	local4, temp2, $2

	! reload since used as temporary:

	ld	[out2+280], out4          ! loop counter

	srl	$3, 29, local0
	ifelse($5,1,{add in4, 120, in4})

	ifelse($5,1,{LDPTR	KS1, in3})
	srl	$4, 29, local7

	or	local0, local5, $4
	or	local2, local7, $3

})



! {load_little_endian}
!
! parameter 1  address
! parameter 2  destination left
! parameter 3  destination right
! parameter 4  temporary
! parameter 5  label

define(load_little_endian, {

! {load_little_endian}
! $1 $2 $3 $4 $5 $6 $7 $8 $9

	! first in memory to rightmost in register

$5:
	ldub	[$1+3], $2

	ldub	[$1+2], $4
	sll	$2, 8, $2
	or	$2, $4, $2

	ldub	[$1+1], $4
	sll	$2, 8, $2
	or	$2, $4, $2

	ldub	[$1+0], $4
	sll	$2, 8, $2
	or	$2, $4, $2


	ldub	[$1+3+4], $3

	ldub	[$1+2+4], $4
	sll	$3, 8, $3
	or	$3, $4, $3

	ldub	[$1+1+4], $4
	sll	$3, 8, $3
	or	$3, $4, $3

	ldub	[$1+0+4], $4
	sll	$3, 8, $3
	or	$3, $4, $3
$5a:

})


! {load_little_endian_inc}
!
! parameter 1  address
! parameter 2  destination left
! parameter 3  destination right
! parameter 4  temporary
! parameter 4  label
!
! adds 8 to address

define(load_little_endian_inc, {

! {load_little_endian_inc}
! $1 $2 $3 $4 $5 $6 $7 $8 $9

	! first in memory to rightmost in register

$5:
	ldub	[$1+3], $2

	ldub	[$1+2], $4
	sll	$2, 8, $2
	or	$2, $4, $2

	ldub	[$1+1], $4
	sll	$2, 8, $2
	or	$2, $4, $2

	ldub	[$1+0], $4
	sll	$2, 8, $2
	or	$2, $4, $2

	ldub	[$1+3+4], $3
	add	$1, 8, $1

	ldub	[$1+2+4-8], $4
	sll	$3, 8, $3
	or	$3, $4, $3

	ldub	[$1+1+4-8], $4
	sll	$3, 8, $3
	or	$3, $4, $3

	ldub	[$1+0+4-8], $4
	sll	$3, 8, $3
	or	$3, $4, $3
$5a:

})


! {load_n_bytes}
!
! Loads 1 to 7 bytes little endian
! Remaining bytes are zeroed.
!
! parameter 1  address
! parameter 2  length
! parameter 3  destination register left
! parameter 4  destination register right
! parameter 5  temp
! parameter 6  temp2
! parameter 7  label
! parameter 8  return label

define(load_n_bytes, {

! {load_n_bytes}
! $1 $2 $5 $6 $7 $8 $7 $8 $9

$7.0:	call	.+8
	sll	$2, 2, $6

	add	%o7,$7.jmp.table-$7.0,$5

	add	$5, $6, $5
	mov	0, $4

	ld	[$5], $5

	jmp	%o7+$5
	mov	0, $3

$7.7:
	ldub	[$1+6], $5
	sll	$5, 16, $5
	or	$3, $5, $3
$7.6:
	ldub	[$1+5], $5
	sll	$5, 8, $5
	or	$3, $5, $3
$7.5:
	ldub	[$1+4], $5
	or	$3, $5, $3
$7.4:
	ldub	[$1+3], $5
	sll	$5, 24, $5
	or	$4, $5, $4
$7.3:
	ldub	[$1+2], $5
	sll	$5, 16, $5
	or	$4, $5, $4
$7.2:
	ldub	[$1+1], $5
	sll	$5, 8, $5
	or	$4, $5, $4
$7.1:
	ldub	[$1+0], $5
	ba	$8
	or	$4, $5, $4

	.align 4

$7.jmp.table:
	.word	0
	.word	$7.1-$7.0
	.word	$7.2-$7.0
	.word	$7.3-$7.0
	.word	$7.4-$7.0
	.word	$7.5-$7.0
	.word	$7.6-$7.0
	.word	$7.7-$7.0
})


! {store_little_endian}
!
! parameter 1  address
! parameter 2  source left
! parameter 3  source right
! parameter 4  temporary

define(store_little_endian, {

! {store_little_endian}
! $1 $2 $3 $4 $5 $6 $7 $8 $9

	! rightmost in register to first in memory

$5:
	and	$2, 255, $4
	stub	$4, [$1+0]

	srl	$2, 8, $4
	and	$4, 255, $4
	stub	$4, [$1+1]

	srl	$2, 16, $4
	and	$4, 255, $4
	stub	$4, [$1+2]

	srl	$2, 24, $4
	stub	$4, [$1+3]


	and	$3, 255, $4
	stub	$4, [$1+0+4]

	srl	$3, 8, $4
	and	$4, 255, $4
	stub	$4, [$1+1+4]

	srl	$3, 16, $4
	and	$4, 255, $4
	stub	$4, [$1+2+4]

	srl	$3, 24, $4
	stub	$4, [$1+3+4]

$5a:

})


! {store_n_bytes}
!
! Stores 1 to 7 bytes little endian
!
! parameter 1  address
! parameter 2  length
! parameter 3  source register left
! parameter 4  source register right
! parameter 5  temp
! parameter 6  temp2
! parameter 7  label
! parameter 8  return label

define(store_n_bytes, {

! {store_n_bytes}
! $1 $2 $5 $6 $7 $8 $7 $8 $9

$7.0:	call	.+8
	sll	$2, 2, $6

	add	%o7,$7.jmp.table-$7.0,$5

	add	$5, $6, $5

	ld	[$5], $5

	jmp	%o7+$5
	nop

$7.7:
	srl	$3, 16, $5
	and	$5, 0xff, $5
	stub	$5, [$1+6]
$7.6:
	srl	$3, 8, $5
	and	$5, 0xff, $5
	stub	$5, [$1+5]
$7.5:
	and	$3, 0xff, $5
	stub	$5, [$1+4]
$7.4:
	srl	$4, 24, $5
	stub	$5, [$1+3]
$7.3:
	srl	$4, 16, $5
	and	$5, 0xff, $5
	stub	$5, [$1+2]
$7.2:
	srl	$4, 8, $5
	and	$5, 0xff, $5
	stub	$5, [$1+1]
$7.1:
	and	$4, 0xff, $5


	ba	$8
	stub	$5, [$1]

	.align 4

$7.jmp.table:

	.word	0
	.word	$7.1-$7.0
	.word	$7.2-$7.0
	.word	$7.3-$7.0
	.word	$7.4-$7.0
	.word	$7.5-$7.0
	.word	$7.6-$7.0
	.word	$7.7-$7.0
})


define(testvalue,{1})

define(register_init, {

! For test purposes:

	sethi	%hi(testvalue), local0
	or	local0, %lo(testvalue), local0

	ifelse($1,{},{}, {mov	local0, $1})
	ifelse($2,{},{}, {mov	local0, $2})
	ifelse($3,{},{}, {mov	local0, $3})
	ifelse($4,{},{}, {mov	local0, $4})
	ifelse($5,{},{}, {mov	local0, $5})
	ifelse($6,{},{}, {mov	local0, $6})
	ifelse($7,{},{}, {mov	local0, $7})
	ifelse($8,{},{}, {mov	local0, $8})

	mov	local0, local1
	mov	local0, local2
	mov	local0, local3
	mov	local0, local4
	mov	local0, local5
	mov	local0, local7
	mov	local0, local6
	mov	local0, out0
	mov	local0, out1
	mov	local0, out2
	mov	local0, out3
	mov	local0, out4
	mov	local0, out5
	mov	local0, global1
	mov	local0, global2
	mov	local0, global3
	mov	local0, global4
	mov	local0, global5

})

.section	".text"

	.align 32

.des_enc:

	! key address in3
	! loads key next encryption/decryption first round from [in4]

	rounds_macro(in5, out5, 1, .des_enc.1, in3, in4, retl)


	.align 32

.des_dec:

	! implemented with out5 as first parameter to avoid
	! register exchange in ede modes

	! key address in4
	! loads key next encryption/decryption first round from [in3]

	rounds_macro(out5, in5, -1, .des_dec.1, in4, in3, retl)



! void DES_encrypt1(data, ks, enc)
! *******************************

	.align 32
	.global DES_encrypt1
	.type	 DES_encrypt1,#function

DES_encrypt1:

	save	%sp, FRAME, %sp

	sethi	%hi(.PIC.DES_SPtrans-1f),global1
	or	global1,%lo(.PIC.DES_SPtrans-1f),global1
1:	call	.+8
	add	%o7,global1,global1
	sub	global1,.PIC.DES_SPtrans-.des_and,out2

	ld	[in0], in5                ! left
	cmp	in2, 0                    ! enc

	be	.encrypt.dec
	ld	[in0+4], out5             ! right

	! parameter 6  1/2 for include encryption/decryption
	! parameter 7  1 for move in1 to in3
	! parameter 8  1 for move in3 to in4, 2 for move in4 to in3

	ip_macro(in5, out5, in5, out5, in3, 0, 1, 1)

	rounds_macro(in5, out5, 1, .des_encrypt1.1, in3, in4) ! in4 not used

	fp_macro(in5, out5, 1)            ! 1 for store to [in0]

	ret
	restore

.encrypt.dec:

	add	in1, 120, in3             ! use last subkey for first round

	! parameter 6  1/2 for include encryption/decryption
	! parameter 7  1 for move in1 to in3
	! parameter 8  1 for move in3 to in4, 2 for move in4 to in3

	ip_macro(in5, out5, out5, in5, in4, 2, 0, 1) ! include dec,  ks in4

	fp_macro(out5, in5, 1)            ! 1 for store to [in0]

	ret
	restore

.DES_encrypt1.end:
	.size	 DES_encrypt1,.DES_encrypt1.end-DES_encrypt1


! void DES_encrypt2(data, ks, enc)
!*********************************

	! encrypts/decrypts without initial/final permutation

	.align 32
	.global DES_encrypt2
	.type	 DES_encrypt2,#function

DES_encrypt2:

	save	%sp, FRAME, %sp

	sethi	%hi(.PIC.DES_SPtrans-1f),global1
	or	global1,%lo(.PIC.DES_SPtrans-1f),global1
1:	call	.+8
	add	%o7,global1,global1
	sub	global1,.PIC.DES_SPtrans-.des_and,out2

	! Set sbox address 1 to 6 and rotate halfs 3 left
	! Errors caught by destest? Yes. Still? *NO*

	!sethi	%hi(DES_SPtrans), global1 ! address sbox 1

	!or	global1, %lo(DES_SPtrans), global1  ! sbox 1

	add	global1, 256, global2     ! sbox 2
	add	global1, 512, global3     ! sbox 3

	ld	[in0], out5               ! right
	add	global1, 768, global4     ! sbox 4
	add	global1, 1024, global5    ! sbox 5

	ld	[in0+4], in5              ! left
	add	global1, 1280, local6     ! sbox 6
	add	global1, 1792, out3       ! sbox 8

	! rotate

	sll	in5, 3, local5
	mov	in1, in3                  ! key address to in3

	sll	out5, 3, local7
	srl	in5, 29, in5

	srl	out5, 29, out5
	add	in5, local5, in5

	add	out5, local7, out5
	cmp	in2, 0

	! we use our own stackframe

	be	.encrypt2.dec
	STPTR	in0, [%sp+BIAS+ARG0+0*ARGSZ]

	ld	[in3], out0               ! key 7531 first round
	mov	LOOPS, out4               ! loop counter

	ld	[in3+4], out1             ! key 8642 first round
	sethi	%hi(0x0000FC00), local5

	call .des_enc
	mov	in3, in4

	! rotate
	sll	in5, 29, in0
	srl	in5, 3, in5
	sll	out5, 29, in1
	add	in5, in0, in5
	srl	out5, 3, out5
	LDPTR	[%sp+BIAS+ARG0+0*ARGSZ], in0
	add	out5, in1, out5
	st	in5, [in0]
	st	out5, [in0+4]

	ret
	restore


.encrypt2.dec:

	add in3, 120, in4

	ld	[in4], out0               ! key 7531 first round
	mov	LOOPS, out4               ! loop counter

	ld	[in4+4], out1             ! key 8642 first round
	sethi	%hi(0x0000FC00), local5

	mov	in5, local1               ! left expected in out5
	mov	out5, in5

	call .des_dec
	mov	local1, out5

.encrypt2.finish:

	! rotate
	sll	in5, 29, in0
	srl	in5, 3, in5
	sll	out5, 29, in1
	add	in5, in0, in5
	srl	out5, 3, out5
	LDPTR	[%sp+BIAS+ARG0+0*ARGSZ], in0
	add	out5, in1, out5
	st	out5, [in0]
	st	in5, [in0+4]

	ret
	restore

.DES_encrypt2.end:
	.size	 DES_encrypt2, .DES_encrypt2.end-DES_encrypt2


! void DES_encrypt3(data, ks1, ks2, ks3)
! **************************************

	.align 32
	.global DES_encrypt3
	.type	 DES_encrypt3,#function

DES_encrypt3:

	save	%sp, FRAME, %sp
	
	sethi	%hi(.PIC.DES_SPtrans-1f),global1
	or	global1,%lo(.PIC.DES_SPtrans-1f),global1
1:	call	.+8
	add	%o7,global1,global1
	sub	global1,.PIC.DES_SPtrans-.des_and,out2

	ld	[in0], in5                ! left
	add	in2, 120, in4             ! ks2

	ld	[in0+4], out5             ! right
	mov	in3, in2                  ! save ks3

	! parameter 6  1/2 for include encryption/decryption
	! parameter 7  1 for mov in1 to in3
	! parameter 8  1 for mov in3 to in4
	! parameter 9  1 for load ks3 and ks2 to in4 and in3

	ip_macro(in5, out5, in5, out5, in3, 1, 1, 0, 0)

	call	.des_dec
	mov	in2, in3                  ! preload ks3

	call	.des_enc
	nop

	fp_macro(in5, out5, 1)

	ret
	restore

.DES_encrypt3.end:
	.size	 DES_encrypt3,.DES_encrypt3.end-DES_encrypt3


! void DES_decrypt3(data, ks1, ks2, ks3)
! **************************************

	.align 32
	.global DES_decrypt3
	.type	 DES_decrypt3,#function

DES_decrypt3:

	save	%sp, FRAME, %sp
	
	sethi	%hi(.PIC.DES_SPtrans-1f),global1
	or	global1,%lo(.PIC.DES_SPtrans-1f),global1
1:	call	.+8
	add	%o7,global1,global1
	sub	global1,.PIC.DES_SPtrans-.des_and,out2

	ld	[in0], in5                ! left
	add	in3, 120, in4             ! ks3

	ld	[in0+4], out5             ! right
	mov	in2, in3                  ! ks2

	! parameter 6  1/2 for include encryption/decryption
	! parameter 7  1 for mov in1 to in3
	! parameter 8  1 for mov in3 to in4
	! parameter 9  1 for load ks3 and ks2 to in4 and in3

	ip_macro(in5, out5, out5, in5, in4, 2, 0, 0, 0)

	call	.des_enc
	add	in1, 120, in4             ! preload ks1

	call	.des_dec
	nop

	fp_macro(out5, in5, 1)

	ret
	restore

.DES_decrypt3.end:
	.size	 DES_decrypt3,.DES_decrypt3.end-DES_decrypt3

! void DES_ncbc_encrypt(input, output, length, schedule, ivec, enc)
! *****************************************************************


	.align 32
	.global DES_ncbc_encrypt
	.type	 DES_ncbc_encrypt,#function

DES_ncbc_encrypt:

	save	%sp, FRAME, %sp
	
	define({INPUT},  { [%sp+BIAS+ARG0+0*ARGSZ] })
	define({OUTPUT}, { [%sp+BIAS+ARG0+1*ARGSZ] })
	define({IVEC},   { [%sp+BIAS+ARG0+4*ARGSZ] })

	sethi	%hi(.PIC.DES_SPtrans-1f),global1
	or	global1,%lo(.PIC.DES_SPtrans-1f),global1
1:	call	.+8
	add	%o7,global1,global1
	sub	global1,.PIC.DES_SPtrans-.des_and,out2

	cmp	in5, 0                    ! enc

	be	.ncbc.dec
	STPTR	in4, IVEC

	! addr  left  right  temp  label
	load_little_endian(in4, in5, out5, local3, .LLE1)  ! iv

	addcc	in2, -8, in2              ! bytes missing when first block done

	bl	.ncbc.enc.seven.or.less
	mov	in3, in4                  ! schedule

.ncbc.enc.next.block:

	load_little_endian(in0, out4, global4, local3, .LLE2)  ! block

.ncbc.enc.next.block_1:

	xor	in5, out4, in5            ! iv xor
	xor	out5, global4, out5       ! iv xor

	! parameter 8  1 for move in3 to in4, 2 for move in4 to in3
	ip_macro(in5, out5, in5, out5, in3, 0, 0, 2)

.ncbc.enc.next.block_2:

!//	call .des_enc                     ! compares in2 to 8
!	rounds inlined for alignment purposes

	add	global1, 768, global4     ! address sbox 4 since register used below

	rounds_macro(in5, out5, 1, .ncbc.enc.1, in3, in4) ! include encryption  ks in3

	bl	.ncbc.enc.next.block_fp
	add	in0, 8, in0               ! input address

	! If 8 or more bytes are to be encrypted after this block,
	! we combine final permutation for this block with initial
	! permutation for next block. Load next block:

	load_little_endian(in0, global3, global4, local5, .LLE12)

	!  parameter 1   original left
	!  parameter 2   original right
	!  parameter 3   left ip
	!  parameter 4   right ip
	!  parameter 5   1: load ks1/ks2 to in3/in4, add 120 to in4
	!                2: mov in4 to in3
	!
	! also adds -8 to length in2 and loads loop counter to out4

	fp_ip_macro(out0, out1, global3, global4, 2)

	store_little_endian(in1, out0, out1, local3, .SLE10)  ! block

	ld	[in3], out0               ! key 7531 first round next block
	mov 	in5, local1
	xor	global3, out5, in5        ! iv xor next block

	ld	[in3+4], out1             ! key 8642
	add	global1, 512, global3     ! address sbox 3 since register used
	xor	global4, local1, out5     ! iv xor next block

	ba	.ncbc.enc.next.block_2
	add	in1, 8, in1               ! output address

.ncbc.enc.next.block_fp:

	fp_macro(in5, out5)

	store_little_endian(in1, in5, out5, local3, .SLE1)  ! block

	addcc   in2, -8, in2              ! bytes missing when next block done

	bpos	.ncbc.enc.next.block
	add	in1, 8, in1

.ncbc.enc.seven.or.less:

	cmp	in2, -8

	ble	.ncbc.enc.finish
	nop

	add	in2, 8, local1            ! bytes to load

	! addr, length, dest left, dest right, temp, temp2, label, ret label
	load_n_bytes(in0, local1, global4, out4, local2, local3, .LNB1, .ncbc.enc.next.block_1)

	! Loads 1 to 7 bytes little endian to global4, out4


.ncbc.enc.finish:

	LDPTR	IVEC, local4
	store_little_endian(local4, in5, out5, local5, .SLE2)  ! ivec

	ret
	restore


.ncbc.dec:

	STPTR	in0, INPUT
	cmp	in2, 0                    ! length
	add	in3, 120, in3

	LDPTR	IVEC, local7              ! ivec
	ble	.ncbc.dec.finish
	mov	in3, in4                  ! schedule

	STPTR	in1, OUTPUT
	mov	in0, local5               ! input

	load_little_endian(local7, in0, in1, local3, .LLE3)   ! ivec

.ncbc.dec.next.block:

	load_little_endian(local5, in5, out5, local3, .LLE4)  ! block

	! parameter 6  1/2 for include encryption/decryption
	! parameter 7  1 for mov in1 to in3
	! parameter 8  1 for mov in3 to in4

	ip_macro(in5, out5, out5, in5, in4, 2, 0, 1) ! include decryption  ks in4

	fp_macro(out5, in5, 0, 1) ! 1 for input and output address to local5/7

	! in2 is bytes left to be stored
	! in2 is compared to 8 in the rounds

	xor	out5, in0, out4           ! iv xor
	bl	.ncbc.dec.seven.or.less
	xor	in5, in1, global4         ! iv xor

	! Load ivec next block now, since input and output address might be the same.

	load_little_endian_inc(local5, in0, in1, local3, .LLE5)  ! iv

	store_little_endian(local7, out4, global4, local3, .SLE3)

	STPTR	local5, INPUT
	add	local7, 8, local7
	addcc   in2, -8, in2

	bg	.ncbc.dec.next.block
	STPTR	local7, OUTPUT


.ncbc.dec.store.iv:

	LDPTR	IVEC, local4              ! ivec
	store_little_endian(local4, in0, in1, local5, .SLE4)

.ncbc.dec.finish:

	ret
	restore

.ncbc.dec.seven.or.less:

	load_little_endian_inc(local5, in0, in1, local3, .LLE13)     ! ivec

	store_n_bytes(local7, in2, global4, out4, local3, local4, .SNB1, .ncbc.dec.store.iv)


.DES_ncbc_encrypt.end:
	.size	 DES_ncbc_encrypt, .DES_ncbc_encrypt.end-DES_ncbc_encrypt


! void DES_ede3_cbc_encrypt(input, output, length, ks1, ks2, ks3, ivec, enc)
! **************************************************************************


	.align 32
	.global DES_ede3_cbc_encrypt
	.type	 DES_ede3_cbc_encrypt,#function

DES_ede3_cbc_encrypt:

	save	%sp, FRAME, %sp

	define({KS1}, { [%sp+BIAS+ARG0+3*ARGSZ] })
	define({KS2}, { [%sp+BIAS+ARG0+4*ARGSZ] })
	define({KS3}, { [%sp+BIAS+ARG0+5*ARGSZ] })

	sethi	%hi(.PIC.DES_SPtrans-1f),global1
	or	global1,%lo(.PIC.DES_SPtrans-1f),global1
1:	call	.+8
	add	%o7,global1,global1
	sub	global1,.PIC.DES_SPtrans-.des_and,out2

	LDPTR	[%fp+BIAS+ARG0+7*ARGSZ], local3          ! enc
	LDPTR	[%fp+BIAS+ARG0+6*ARGSZ], local4          ! ivec
	cmp	local3, 0                 ! enc

	be	.ede3.dec
	STPTR	in4, KS2

	STPTR	in5, KS3

	load_little_endian(local4, in5, out5, local3, .LLE6)  ! ivec

	addcc	in2, -8, in2              ! bytes missing after next block

	bl	.ede3.enc.seven.or.less
	STPTR	in3, KS1

.ede3.enc.next.block:

	load_little_endian(in0, out4, global4, local3, .LLE7)

.ede3.enc.next.block_1:

	LDPTR	KS2, in4
	xor	in5, out4, in5            ! iv xor
	xor	out5, global4, out5       ! iv xor

	LDPTR	KS1, in3
	add	in4, 120, in4             ! for decryption we use last subkey first
	nop

	ip_macro(in5, out5, in5, out5, in3)

.ede3.enc.next.block_2:

	call .des_enc                     ! ks1 in3
	nop

	call .des_dec                     ! ks2 in4
	LDPTR	KS3, in3

	call .des_enc                     ! ks3 in3  compares in2 to 8
	nop

	bl	.ede3.enc.next.block_fp
	add	in0, 8, in0

	! If 8 or more bytes are to be encrypted after this block,
	! we combine final permutation for this block with initial
	! permutation for next block. Load next block:

	load_little_endian(in0, global3, global4, local5, .LLE11)

	!  parameter 1   original left
	!  parameter 2   original right
	!  parameter 3   left ip
	!  parameter 4   right ip
	!  parameter 5   1: load ks1/ks2 to in3/in4, add 120 to in4
	!                2: mov in4 to in3
	!
	! also adds -8 to length in2 and loads loop counter to out4

	fp_ip_macro(out0, out1, global3, global4, 1)

	store_little_endian(in1, out0, out1, local3, .SLE9)  ! block

	mov 	in5, local1
	xor	global3, out5, in5        ! iv xor next block

	ld	[in3], out0               ! key 7531
	add	global1, 512, global3     ! address sbox 3
	xor	global4, local1, out5     ! iv xor next block

	ld	[in3+4], out1             ! key 8642
	add	global1, 768, global4     ! address sbox 4
	ba	.ede3.enc.next.block_2
	add	in1, 8, in1

.ede3.enc.next.block_fp:

	fp_macro(in5, out5)

	store_little_endian(in1, in5, out5, local3, .SLE5)  ! block

	addcc   in2, -8, in2              ! bytes missing when next block done

	bpos	.ede3.enc.next.block
	add	in1, 8, in1

.ede3.enc.seven.or.less:

	cmp	in2, -8

	ble	.ede3.enc.finish
	nop

	add	in2, 8, local1            ! bytes to load

	! addr, length, dest left, dest right, temp, temp2, label, ret label
	load_n_bytes(in0, local1, global4, out4, local2, local3, .LNB2, .ede3.enc.next.block_1)

.ede3.enc.finish:

	LDPTR	[%fp+BIAS+ARG0+6*ARGSZ], local4          ! ivec
	store_little_endian(local4, in5, out5, local5, .SLE6)  ! ivec

	ret
	restore

.ede3.dec:

	STPTR	in0, INPUT
	add	in5, 120, in5

	STPTR	in1, OUTPUT
	mov	in0, local5
	add	in3, 120, in3

	STPTR	in3, KS1
	cmp	in2, 0

	ble	.ede3.dec.finish
	STPTR	in5, KS3

	LDPTR	[%fp+BIAS+ARG0+6*ARGSZ], local7          ! iv
	load_little_endian(local7, in0, in1, local3, .LLE8)

.ede3.dec.next.block:

	load_little_endian(local5, in5, out5, local3, .LLE9)

	! parameter 6  1/2 for include encryption/decryption
	! parameter 7  1 for mov in1 to in3
	! parameter 8  1 for mov in3 to in4
	! parameter 9  1 for load ks3 and ks2 to in4 and in3

	ip_macro(in5, out5, out5, in5, in4, 2, 0, 0, 1) ! inc .des_dec ks3 in4

	call .des_enc                     ! ks2 in3
	LDPTR	KS1, in4

	call .des_dec                     ! ks1 in4
	nop

	fp_macro(out5, in5, 0, 1)   ! 1 for input and output address local5/7

	! in2 is bytes left to be stored
	! in2 is compared to 8 in the rounds

	xor	out5, in0, out4
	bl	.ede3.dec.seven.or.less
	xor	in5, in1, global4

	load_little_endian_inc(local5, in0, in1, local3, .LLE10)   ! iv next block

	store_little_endian(local7, out4, global4, local3, .SLE7)  ! block

	STPTR	local5, INPUT
	addcc   in2, -8, in2
	add	local7, 8, local7

	bg	.ede3.dec.next.block
	STPTR	local7, OUTPUT

.ede3.dec.store.iv:

	LDPTR	[%fp+BIAS+ARG0+6*ARGSZ], local4          ! ivec
	store_little_endian(local4, in0, in1, local5, .SLE8)  ! ivec

.ede3.dec.finish:

	ret
	restore

.ede3.dec.seven.or.less:

	load_little_endian_inc(local5, in0, in1, local3, .LLE14)     ! iv

	store_n_bytes(local7, in2, global4, out4, local3, local4, .SNB2, .ede3.dec.store.iv)


.DES_ede3_cbc_encrypt.end:
	.size	 DES_ede3_cbc_encrypt,.DES_ede3_cbc_encrypt.end-DES_ede3_cbc_encrypt

	.align	256
	.type	 .des_and,#object
	.size	 .des_and,284

.des_and:

! This table is used for AND 0xFC when it is known that register
! bits 8-31 are zero. Makes it possible to do three arithmetic
! operations in one cycle.

	.byte  0, 0, 0, 0, 4, 4, 4, 4
	.byte  8, 8, 8, 8, 12, 12, 12, 12
	.byte  16, 16, 16, 16, 20, 20, 20, 20
	.byte  24, 24, 24, 24, 28, 28, 28, 28
	.byte  32, 32, 32, 32, 36, 36, 36, 36
	.byte  40, 40, 40, 40, 44, 44, 44, 44
	.byte  48, 48, 48, 48, 52, 52, 52, 52
	.byte  56, 56, 56, 56, 60, 60, 60, 60
	.byte  64, 64, 64, 64, 68, 68, 68, 68
	.byte  72, 72, 72, 72, 76, 76, 76, 76
	.byte  80, 80, 80, 80, 84, 84, 84, 84
	.byte  88, 88, 88, 88, 92, 92, 92, 92
	.byte  96, 96, 96, 96, 100, 100, 100, 100
	.byte  104, 104, 104, 104, 108, 108, 108, 108
	.byte  112, 112, 112, 112, 116, 116, 116, 116
	.byte  120, 120, 120, 120, 124, 124, 124, 124
	.byte  128, 128, 128, 128, 132, 132, 132, 132
	.byte  136, 136, 136, 136, 140, 140, 140, 140
	.byte  144, 144, 144, 144, 148, 148, 148, 148
	.byte  152, 152, 152, 152, 156, 156, 156, 156
	.byte  160, 160, 160, 160, 164, 164, 164, 164
	.byte  168, 168, 168, 168, 172, 172, 172, 172
	.byte  176, 176, 176, 176, 180, 180, 180, 180
	.byte  184, 184, 184, 184, 188, 188, 188, 188
	.byte  192, 192, 192, 192, 196, 196, 196, 196
	.byte  200, 200, 200, 200, 204, 204, 204, 204
	.byte  208, 208, 208, 208, 212, 212, 212, 212
	.byte  216, 216, 216, 216, 220, 220, 220, 220
	.byte  224, 224, 224, 224, 228, 228, 228, 228
	.byte  232, 232, 232, 232, 236, 236, 236, 236
	.byte  240, 240, 240, 240, 244, 244, 244, 244
	.byte  248, 248, 248, 248, 252, 252, 252, 252

	! 5 numbers for initial/final permutation

	.word   0x0f0f0f0f                ! offset 256
	.word	0x0000ffff                ! 260
	.word	0x33333333                ! 264
	.word	0x00ff00ff                ! 268
	.word	0x55555555                ! 272

	.word	0                         ! 276
	.word	LOOPS                     ! 280
	.word	0x0000FC00                ! 284

	.global	DES_SPtrans
	.type	DES_SPtrans,#object
	.size	DES_SPtrans,2048
.align	64
DES_SPtrans:
.PIC.DES_SPtrans:
	! nibble 0
	.word	0x02080800, 0x00080000, 0x02000002, 0x02080802
	.word	0x02000000, 0x00080802, 0x00080002, 0x02000002
	.word	0x00080802, 0x02080800, 0x02080000, 0x00000802
	.word	0x02000802, 0x02000000, 0x00000000, 0x00080002
	.word	0x00080000, 0x00000002, 0x02000800, 0x00080800
	.word	0x02080802, 0x02080000, 0x00000802, 0x02000800
	.word	0x00000002, 0x00000800, 0x00080800, 0x02080002
	.word	0x00000800, 0x02000802, 0x02080002, 0x00000000
	.word	0x00000000, 0x02080802, 0x02000800, 0x00080002
	.word	0x02080800, 0x00080000, 0x00000802, 0x02000800
	.word	0x02080002, 0x00000800, 0x00080800, 0x02000002
	.word	0x00080802, 0x00000002, 0x02000002, 0x02080000
	.word	0x02080802, 0x00080800, 0x02080000, 0x02000802
	.word	0x02000000, 0x00000802, 0x00080002, 0x00000000
	.word	0x00080000, 0x02000000, 0x02000802, 0x02080800
	.word	0x00000002, 0x02080002, 0x00000800, 0x00080802
	! nibble 1
	.word	0x40108010, 0x00000000, 0x00108000, 0x40100000
	.word	0x40000010, 0x00008010, 0x40008000, 0x00108000
	.word	0x00008000, 0x40100010, 0x00000010, 0x40008000
	.word	0x00100010, 0x40108000, 0x40100000, 0x00000010
	.word	0x00100000, 0x40008010, 0x40100010, 0x00008000
	.word	0x00108010, 0x40000000, 0x00000000, 0x00100010
	.word	0x40008010, 0x00108010, 0x40108000, 0x40000010
	.word	0x40000000, 0x00100000, 0x00008010, 0x40108010
	.word	0x00100010, 0x40108000, 0x40008000, 0x00108010
	.word	0x40108010, 0x00100010, 0x40000010, 0x00000000
	.word	0x40000000, 0x00008010, 0x00100000, 0x40100010
	.word	0x00008000, 0x40000000, 0x00108010, 0x40008010
	.word	0x40108000, 0x00008000, 0x00000000, 0x40000010
	.word	0x00000010, 0x40108010, 0x00108000, 0x40100000
	.word	0x40100010, 0x00100000, 0x00008010, 0x40008000
	.word	0x40008010, 0x00000010, 0x40100000, 0x00108000
	! nibble 2
	.word	0x04000001, 0x04040100, 0x00000100, 0x04000101
	.word	0x00040001, 0x04000000, 0x04000101, 0x00040100
	.word	0x04000100, 0x00040000, 0x04040000, 0x00000001
	.word	0x04040101, 0x00000101, 0x00000001, 0x04040001
	.word	0x00000000, 0x00040001, 0x04040100, 0x00000100
	.word	0x00000101, 0x04040101, 0x00040000, 0x04000001
	.word	0x04040001, 0x04000100, 0x00040101, 0x04040000
	.word	0x00040100, 0x00000000, 0x04000000, 0x00040101
	.word	0x04040100, 0x00000100, 0x00000001, 0x00040000
	.word	0x00000101, 0x00040001, 0x04040000, 0x04000101
	.word	0x00000000, 0x04040100, 0x00040100, 0x04040001
	.word	0x00040001, 0x04000000, 0x04040101, 0x00000001
	.word	0x00040101, 0x04000001, 0x04000000, 0x04040101
	.word	0x00040000, 0x04000100, 0x04000101, 0x00040100
	.word	0x04000100, 0x00000000, 0x04040001, 0x00000101
	.word	0x04000001, 0x00040101, 0x00000100, 0x04040000
	! nibble 3
	.word	0x00401008, 0x10001000, 0x00000008, 0x10401008
	.word	0x00000000, 0x10400000, 0x10001008, 0x00400008
	.word	0x10401000, 0x10000008, 0x10000000, 0x00001008
	.word	0x10000008, 0x00401008, 0x00400000, 0x10000000
	.word	0x10400008, 0x00401000, 0x00001000, 0x00000008
	.word	0x00401000, 0x10001008, 0x10400000, 0x00001000
	.word	0x00001008, 0x00000000, 0x00400008, 0x10401000
	.word	0x10001000, 0x10400008, 0x10401008, 0x00400000
	.word	0x10400008, 0x00001008, 0x00400000, 0x10000008
	.word	0x00401000, 0x10001000, 0x00000008, 0x10400000
	.word	0x10001008, 0x00000000, 0x00001000, 0x00400008
	.word	0x00000000, 0x10400008, 0x10401000, 0x00001000
	.word	0x10000000, 0x10401008, 0x00401008, 0x00400000
	.word	0x10401008, 0x00000008, 0x10001000, 0x00401008
	.word	0x00400008, 0x00401000, 0x10400000, 0x10001008
	.word	0x00001008, 0x10000000, 0x10000008, 0x10401000
	! nibble 4
	.word	0x08000000, 0x00010000, 0x00000400, 0x08010420
	.word	0x08010020, 0x08000400, 0x00010420, 0x08010000
	.word	0x00010000, 0x00000020, 0x08000020, 0x00010400
	.word	0x08000420, 0x08010020, 0x08010400, 0x00000000
	.word	0x00010400, 0x08000000, 0x00010020, 0x00000420
	.word	0x08000400, 0x00010420, 0x00000000, 0x08000020
	.word	0x00000020, 0x08000420, 0x08010420, 0x00010020
	.word	0x08010000, 0x00000400, 0x00000420, 0x08010400
	.word	0x08010400, 0x08000420, 0x00010020, 0x08010000
	.word	0x00010000, 0x00000020, 0x08000020, 0x08000400
	.word	0x08000000, 0x00010400, 0x08010420, 0x00000000
	.word	0x00010420, 0x08000000, 0x00000400, 0x00010020
	.word	0x08000420, 0x00000400, 0x00000000, 0x08010420
	.word	0x08010020, 0x08010400, 0x00000420, 0x00010000
	.word	0x00010400, 0x08010020, 0x08000400, 0x00000420
	.word	0x00000020, 0x00010420, 0x08010000, 0x08000020
	! nibble 5
	.word	0x80000040, 0x00200040, 0x00000000, 0x80202000
	.word	0x00200040, 0x00002000, 0x80002040, 0x00200000
	.word	0x00002040, 0x80202040, 0x00202000, 0x80000000
	.word	0x80002000, 0x80000040, 0x80200000, 0x00202040
	.word	0x00200000, 0x80002040, 0x80200040, 0x00000000
	.word	0x00002000, 0x00000040, 0x80202000, 0x80200040
	.word	0x80202040, 0x80200000, 0x80000000, 0x00002040
	.word	0x00000040, 0x00202000, 0x00202040, 0x80002000
	.word	0x00002040, 0x80000000, 0x80002000, 0x00202040
	.word	0x80202000, 0x00200040, 0x00000000, 0x80002000
	.word	0x80000000, 0x00002000, 0x80200040, 0x00200000
	.word	0x00200040, 0x80202040, 0x00202000, 0x00000040
	.word	0x80202040, 0x00202000, 0x00200000, 0x80002040
	.word	0x80000040, 0x80200000, 0x00202040, 0x00000000
	.word	0x00002000, 0x80000040, 0x80002040, 0x80202000
	.word	0x80200000, 0x00002040, 0x00000040, 0x80200040
	! nibble 6
	.word	0x00004000, 0x00000200, 0x01000200, 0x01000004
	.word	0x01004204, 0x00004004, 0x00004200, 0x00000000
	.word	0x01000000, 0x01000204, 0x00000204, 0x01004000
	.word	0x00000004, 0x01004200, 0x01004000, 0x00000204
	.word	0x01000204, 0x00004000, 0x00004004, 0x01004204
	.word	0x00000000, 0x01000200, 0x01000004, 0x00004200
	.word	0x01004004, 0x00004204, 0x01004200, 0x00000004
	.word	0x00004204, 0x01004004, 0x00000200, 0x01000000
	.word	0x00004204, 0x01004000, 0x01004004, 0x00000204
	.word	0x00004000, 0x00000200, 0x01000000, 0x01004004
	.word	0x01000204, 0x00004204, 0x00004200, 0x00000000
	.word	0x00000200, 0x01000004, 0x00000004, 0x01000200
	.word	0x00000000, 0x01000204, 0x01000200, 0x00004200
	.word	0x00000204, 0x00004000, 0x01004204, 0x01000000
	.word	0x01004200, 0x00000004, 0x00004004, 0x01004204
	.word	0x01000004, 0x01004200, 0x01004000, 0x00004004
	! nibble 7
	.word	0x20800080, 0x20820000, 0x00020080, 0x00000000
	.word	0x20020000, 0x00800080, 0x20800000, 0x20820080
	.word	0x00000080, 0x20000000, 0x00820000, 0x00020080
	.word	0x00820080, 0x20020080, 0x20000080, 0x20800000
	.word	0x00020000, 0x00820080, 0x00800080, 0x20020000
	.word	0x20820080, 0x20000080, 0x00000000, 0x00820000
	.word	0x20000000, 0x00800000, 0x20020080, 0x20800080
	.word	0x00800000, 0x00020000, 0x20820000, 0x00000080
	.word	0x00800000, 0x00020000, 0x20000080, 0x20820080
	.word	0x00020080, 0x20000000, 0x00000000, 0x00820000
	.word	0x20800080, 0x20020080, 0x20020000, 0x00800080
	.word	0x20820000, 0x00000080, 0x00800080, 0x20020000
	.word	0x20820080, 0x00800000, 0x20800000, 0x20000080
	.word	0x00820000, 0x00020080, 0x20020080, 0x20800000
	.word	0x00000080, 0x20820000, 0x00820080, 0x00000000
	.word	0x20000000, 0x20800080, 0x00020000, 0x00820080

