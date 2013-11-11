!   Copyright (C) 2008-2012 Imagination Technologies Ltd.

	.text
	.global	_memmove
	.type	_memmove,function
! D1Ar1 dst
! D0Ar2 src
! D1Ar3 cnt
! D0Re0 dst
_memmove:
	CMP 	D1Ar3, #0
	MOV 	D0Re0, D1Ar1
	BZ 	$LEND2
	MSETL 	[A0StP], D0.5, D0.6, D0.7
	MOV 	D1Ar5, D0Ar2
	CMP 	D1Ar1, D1Ar5
	BLT 	$Lforwards_copy
	SUB 	D0Ar4, D1Ar1, D1Ar3
	ADD 	D0Ar4, D0Ar4, #1
	CMP 	D0Ar2, D0Ar4
	BLT 	$Lforwards_copy
	! should copy backwards
	MOV 	D1Re0, D0Ar2
	! adjust pointer to the end of mem
	ADD 	D0Ar2, D1Re0, D1Ar3
	ADD 	D1Ar1, D1Ar1, D1Ar3

	MOV 	A1.2, D0Ar2
	MOV 	A0.2, D1Ar1
	CMP 	D1Ar3, #8
	BLT 	$Lbbyte_loop

	MOV 	D0Ar4, D0Ar2
	MOV 	D1Ar5, D1Ar1

	! test 8 byte alignment
	ANDS 	D1Ar5, D1Ar5, #7
	BNE 	$Lbdest_unaligned

	ANDS 	D0Ar4, D0Ar4, #7
	BNE 	$Lbsrc_unaligned

	LSR 	D1Ar5, D1Ar3, #3

$Lbaligned_loop:
	GETL 	D0Re0, D1Re0, [--A1.2]
	SETL 	[--A0.2], D0Re0, D1Re0
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lbaligned_loop

	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lbbyte_loop_exit
$Lbbyte_loop:
	GETB 	D1Re0, [--A1.2]
	SETB 	[--A0.2], D1Re0
	SUBS 	D1Ar3, D1Ar3, #1
	BNE 	$Lbbyte_loop
$Lbbyte_loop_exit:
	MOV 	D0Re0, A0.2
$LEND:
	SUB 	A0.2, A0StP, #24
	MGETL 	D0.5, D0.6, D0.7, [A0.2]
	SUB 	A0StP, A0StP, #24
$LEND2:
	MOV 	PC, D1RtP

$Lbdest_unaligned:
	GETB 	D0Re0, [--A1.2]
	SETB 	[--A0.2], D0Re0
	SUBS 	D1Ar5, D1Ar5, #1
	SUB 	D1Ar3, D1Ar3, #1
	BNE 	$Lbdest_unaligned
	CMP 	D1Ar3, #8
	BLT 	$Lbbyte_loop
$Lbsrc_unaligned:
	LSR 	D1Ar5, D1Ar3, #3
	! adjust A1.2
	MOV 	D0Ar4, A1.2
	! save original address
	MOV 	D0Ar6, A1.2

	ADD 	D0Ar4, D0Ar4, #7
	ANDMB 	D0Ar4, D0Ar4, #0xfff8
	! new address is the 8-byte aligned one above the original
	MOV 	A1.2, D0Ar4

	! A0.2 dst 64-bit is aligned
	! measure the gap size
	SUB 	D0Ar6, D0Ar4, D0Ar6
	MOVS 	D0Ar4, D0Ar6
	! keep this information for the later adjustment
	! both aligned
	BZ 	$Lbaligned_loop

	! prefetch
	GETL 	D0Re0, D1Re0, [--A1.2]

	CMP 	D0Ar6, #4
	BLT 	$Lbunaligned_1_2_3
	! 32-bit aligned
	BZ 	$Lbaligned_4

	SUB 	D0Ar6, D0Ar6, #4
	! D1.6 stores the gap size in bits
	MULW 	D1.6, D0Ar6, #8
	MOV 	D0.6, #32
	! D0.6 stores the complement of the gap size
	SUB 	D0.6, D0.6, D1.6

$Lbunaligned_5_6_7:
	GETL 	D0.7, D1.7, [--A1.2]
	! form 64-bit data in D0Re0, D1Re0
	MOV 	D1Re0, D0Re0
	! D1Re0 << gap-size
	LSL 	D1Re0, D1Re0, D1.6
	MOV 	D0Re0, D1.7
	! D0Re0 >> complement
	LSR 	D0Re0, D0Re0, D0.6
	MOV 	D1.5, D0Re0
	! combine the both
	ADD 	D1Re0, D1Re0, D1.5

	MOV 	D1.5, D1.7
	LSL 	D1.5, D1.5, D1.6
	MOV 	D0Re0, D0.7
	LSR 	D0Re0, D0Re0, D0.6
	MOV 	D0.5, D1.5
	ADD 	D0Re0, D0Re0, D0.5

	SETL 	[--A0.2], D0Re0, D1Re0
	MOV 	D0Re0, D0.7
	MOV 	D1Re0, D1.7
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lbunaligned_5_6_7

	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lbbyte_loop_exit
	! Adjust A1.2
	! A1.2 <- A1.2 +8 - gapsize
	ADD 	A1.2, A1.2, #8
	SUB 	A1.2, A1.2, D0Ar4
	B 	$Lbbyte_loop

$Lbunaligned_1_2_3:
	MULW 	D1.6, D0Ar6, #8
	MOV 	D0.6, #32
	SUB 	D0.6, D0.6, D1.6

$Lbunaligned_1_2_3_loop:
	GETL 	D0.7, D1.7, [--A1.2]
	! form 64-bit data in D0Re0, D1Re0
	LSL 	D1Re0, D1Re0, D1.6
	! save D0Re0 for later use
	MOV 	D0.5, D0Re0
	LSR 	D0Re0, D0Re0, D0.6
	MOV 	D1.5, D0Re0
	ADD 	D1Re0, D1Re0, D1.5

	! orignal data in D0Re0
	MOV 	D1.5, D0.5
	LSL 	D1.5, D1.5, D1.6
	MOV 	D0Re0, D1.7
	LSR 	D0Re0, D0Re0, D0.6
	MOV 	D0.5, D1.5
	ADD 	D0Re0, D0Re0, D0.5

	SETL 	[--A0.2], D0Re0, D1Re0
	MOV 	D0Re0, D0.7
	MOV 	D1Re0, D1.7
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lbunaligned_1_2_3_loop

	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lbbyte_loop_exit
	! Adjust A1.2
	ADD 	A1.2, A1.2, #8
	SUB 	A1.2, A1.2, D0Ar4
	B 	$Lbbyte_loop

$Lbaligned_4:
	GETL 	D0.7, D1.7, [--A1.2]
	MOV 	D1Re0, D0Re0
	MOV 	D0Re0, D1.7
	SETL 	[--A0.2], D0Re0, D1Re0
	MOV 	D0Re0, D0.7
	MOV 	D1Re0, D1.7
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lbaligned_4
	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lbbyte_loop_exit
	! Adjust A1.2
	ADD 	A1.2, A1.2, #8
	SUB 	A1.2, A1.2, D0Ar4
	B 	$Lbbyte_loop

$Lforwards_copy:
	MOV 	A1.2, D0Ar2
	MOV 	A0.2, D1Ar1
	CMP 	D1Ar3, #8
	BLT 	$Lfbyte_loop

	MOV 	D0Ar4, D0Ar2
	MOV 	D1Ar5, D1Ar1

	ANDS 	D1Ar5, D1Ar5, #7
	BNE 	$Lfdest_unaligned

	ANDS 	D0Ar4, D0Ar4, #7
	BNE 	$Lfsrc_unaligned

	LSR 	D1Ar5, D1Ar3, #3

$Lfaligned_loop:
	GETL 	D0Re0, D1Re0, [A1.2++]
	SUBS 	D1Ar5, D1Ar5, #1
	SETL 	[A0.2++], D0Re0, D1Re0
	BNE 	$Lfaligned_loop

	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lfbyte_loop_exit
$Lfbyte_loop:
	GETB 	D1Re0, [A1.2++]
	SETB 	[A0.2++], D1Re0
	SUBS 	D1Ar3, D1Ar3, #1
	BNE 	$Lfbyte_loop
$Lfbyte_loop_exit:
	MOV 	D0Re0, D1Ar1
	B 	$LEND

$Lfdest_unaligned:
	GETB 	D0Re0, [A1.2++]
	ADD 	D1Ar5, D1Ar5, #1
	SUB 	D1Ar3, D1Ar3, #1
	SETB 	[A0.2++], D0Re0
	CMP 	D1Ar5, #8
	BNE 	$Lfdest_unaligned
	CMP 	D1Ar3, #8
	BLT 	$Lfbyte_loop
$Lfsrc_unaligned:
	! adjust A1.2
	LSR 	D1Ar5, D1Ar3, #3

	MOV 	D0Ar4, A1.2
	MOV 	D0Ar6, A1.2
	ANDMB 	D0Ar4, D0Ar4, #0xfff8
	MOV 	A1.2, D0Ar4

	! A0.2 dst 64-bit is aligned
	SUB 	D0Ar6, D0Ar6, D0Ar4
	! keep the information for the later adjustment
	MOVS 	D0Ar4, D0Ar6

	! both aligned
	BZ 	$Lfaligned_loop

	! prefetch
	GETL 	D0Re0, D1Re0, [A1.2]

	CMP 	D0Ar6, #4
	BLT 	$Lfunaligned_1_2_3
	BZ 	$Lfaligned_4

	SUB 	D0Ar6, D0Ar6, #4
	MULW 	D0.6, D0Ar6, #8
	MOV 	D1.6, #32
	SUB 	D1.6, D1.6, D0.6

$Lfunaligned_5_6_7:
	GETL 	D0.7, D1.7, [++A1.2]
	! form 64-bit data in D0Re0, D1Re0
	MOV 	D0Re0, D1Re0
	LSR 	D0Re0, D0Re0, D0.6
	MOV 	D1Re0, D0.7
	LSL 	D1Re0, D1Re0, D1.6
	MOV 	D0.5, D1Re0
	ADD 	D0Re0, D0Re0, D0.5

	MOV 	D0.5, D0.7
	LSR 	D0.5, D0.5, D0.6
	MOV 	D1Re0, D1.7
	LSL 	D1Re0, D1Re0, D1.6
	MOV 	D1.5, D0.5
	ADD 	D1Re0, D1Re0, D1.5

	SETL 	[A0.2++], D0Re0, D1Re0
	MOV 	D0Re0, D0.7
	MOV 	D1Re0, D1.7
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lfunaligned_5_6_7

	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lfbyte_loop_exit
	! Adjust A1.2
	ADD	A1.2, A1.2, D0Ar4
	B 	$Lfbyte_loop

$Lfunaligned_1_2_3:
	MULW 	D0.6, D0Ar6, #8
	MOV 	D1.6, #32
	SUB 	D1.6, D1.6, D0.6

$Lfunaligned_1_2_3_loop:
	GETL 	D0.7, D1.7, [++A1.2]
	! form 64-bit data in D0Re0, D1Re0
	LSR 	D0Re0, D0Re0, D0.6
	MOV 	D1.5, D1Re0
	LSL 	D1Re0, D1Re0, D1.6
	MOV 	D0.5, D1Re0
	ADD 	D0Re0, D0Re0, D0.5

	MOV 	D0.5, D1.5
	LSR 	D0.5, D0.5, D0.6
	MOV 	D1Re0, D0.7
	LSL 	D1Re0, D1Re0, D1.6
	MOV 	D1.5, D0.5
	ADD 	D1Re0, D1Re0, D1.5

	SETL 	[A0.2++], D0Re0, D1Re0
	MOV 	D0Re0, D0.7
	MOV 	D1Re0, D1.7
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lfunaligned_1_2_3_loop

	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lfbyte_loop_exit
	! Adjust A1.2
	ADD	A1.2, A1.2, D0Ar4
	B 	$Lfbyte_loop

$Lfaligned_4:
	GETL 	D0.7, D1.7, [++A1.2]
	MOV 	D0Re0, D1Re0
	MOV 	D1Re0, D0.7
	SETL 	[A0.2++], D0Re0, D1Re0
	MOV 	D0Re0, D0.7
	MOV 	D1Re0, D1.7
	SUBS 	D1Ar5, D1Ar5, #1
	BNE 	$Lfaligned_4
	ANDS 	D1Ar3, D1Ar3, #7
	BZ 	$Lfbyte_loop_exit
	! Adjust A1.2
	ADD	A1.2, A1.2, D0Ar4
	B 	$Lfbyte_loop

	.size _memmove,.-_memmove
