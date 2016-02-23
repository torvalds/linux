!   Copyright (C) 2008-2012 Imagination Technologies Ltd.

	.text
	.global	_memcpy
	.type	_memcpy,function
! D1Ar1 dst
! D0Ar2 src
! D1Ar3 cnt
! D0Re0 dst
_memcpy:
	CMP 	D1Ar3, #16
	MOV 	A1.2, D0Ar2		! source pointer
	MOV 	A0.2, D1Ar1		! destination pointer
	MOV 	A0.3, D1Ar1		! for return value
! If there are less than 16 bytes to copy use the byte copy loop
	BGE 	$Llong_copy

$Lbyte_copy:
! Simply copy a byte at a time
	SUBS	TXRPT, D1Ar3, #1
	BLT	$Lend
$Lloop_byte:
	GETB 	D1Re0, [A1.2++]
	SETB 	[A0.2++], D1Re0
	BR	$Lloop_byte

$Lend:
! Finally set return value and return
	MOV 	D0Re0, A0.3
	MOV 	PC, D1RtP

$Llong_copy:
	ANDS 	D1Ar5, D1Ar1, #7	! test destination alignment
	BZ	$Laligned_dst

! The destination address is not 8 byte aligned. We will copy bytes from
! the source to the destination until the remaining data has an 8 byte
! destination address alignment (i.e we should never copy more than 7
! bytes here).
$Lalign_dst:
	GETB 	D0Re0, [A1.2++]
	ADD 	D1Ar5, D1Ar5, #1	! dest is aligned when D1Ar5 reaches #8
	SUB 	D1Ar3, D1Ar3, #1	! decrement count of remaining bytes
	SETB 	[A0.2++], D0Re0
	CMP 	D1Ar5, #8
	BNE 	$Lalign_dst

! We have at least (16 - 7) = 9 bytes to copy - calculate the number of 8 byte
! blocks, then jump to the unaligned copy loop or fall through to the aligned
! copy loop as appropriate.
$Laligned_dst:
	MOV	D0Ar4, A1.2
	LSR 	D1Ar5, D1Ar3, #3	! D1Ar5 = number of 8 byte blocks
	ANDS 	D0Ar4, D0Ar4, #7	! test source alignment
	BNZ 	$Lunaligned_copy	! if unaligned, use unaligned copy loop

! Both source and destination are 8 byte aligned - the easy case.
$Laligned_copy:
	LSRS	D1Ar5, D1Ar3, #5	! D1Ar5 = number of 32 byte blocks
	BZ	$Lbyte_copy
	SUB	TXRPT, D1Ar5, #1

$Laligned_32:
	GETL 	D0Re0, D1Re0, [A1.2++]
	GETL 	D0Ar6, D1Ar5, [A1.2++]
	SETL 	[A0.2++], D0Re0, D1Re0
	SETL 	[A0.2++], D0Ar6, D1Ar5
	GETL 	D0Re0, D1Re0, [A1.2++]
	GETL 	D0Ar6, D1Ar5, [A1.2++]
	SETL 	[A0.2++], D0Re0, D1Re0
	SETL 	[A0.2++], D0Ar6, D1Ar5
	BR	$Laligned_32

! If there are any remaining bytes use the byte copy loop, otherwise we are done
	ANDS 	D1Ar3, D1Ar3, #0x1f
	BNZ	$Lbyte_copy
	B	$Lend

! The destination is 8 byte aligned but the source is not, and there are 8
! or more bytes to be copied.
$Lunaligned_copy:
! Adjust the source pointer (A1.2) to the 8 byte boundary before its
! current value
	MOV 	D0Ar4, A1.2
	MOV 	D0Ar6, A1.2
	ANDMB 	D0Ar4, D0Ar4, #0xfff8
	MOV 	A1.2, D0Ar4
! Save the number of bytes of mis-alignment in D0Ar4 for use later
	SUBS 	D0Ar6, D0Ar6, D0Ar4
	MOV	D0Ar4, D0Ar6
! if there is no mis-alignment after all, use the aligned copy loop
	BZ 	$Laligned_copy

! prefetch 8 bytes
	GETL 	D0Re0, D1Re0, [A1.2]

	SUB	TXRPT, D1Ar5, #1

! There are 3 mis-alignment cases to be considered. Less than 4 bytes, exactly
! 4 bytes, and more than 4 bytes.
	CMP 	D0Ar6, #4
	BLT 	$Lunaligned_1_2_3	! use 1-3 byte mis-alignment loop
	BZ 	$Lunaligned_4		! use 4 byte mis-alignment loop

! The mis-alignment is more than 4 bytes
$Lunaligned_5_6_7:
	SUB 	D0Ar6, D0Ar6, #4
! Calculate the bit offsets required for the shift operations necesssary
! to align the data.
! D0Ar6 = bit offset, D1Ar5 = (32 - bit offset)
	MULW 	D0Ar6, D0Ar6, #8
	MOV	D1Ar5, #32
	SUB	D1Ar5, D1Ar5, D0Ar6
! Move data 4 bytes before we enter the main loop
	MOV 	D0Re0, D1Re0

$Lloop_5_6_7:
	GETL 	D0Ar2, D1Ar1, [++A1.2]
! form 64-bit data in D0Re0, D1Re0
	LSR 	D0Re0, D0Re0, D0Ar6
	MOV 	D1Re0, D0Ar2
	LSL 	D1Re0, D1Re0, D1Ar5
	ADD 	D0Re0, D0Re0, D1Re0

	LSR 	D0Ar2, D0Ar2, D0Ar6
	LSL 	D1Re0, D1Ar1, D1Ar5
	ADD 	D1Re0, D1Re0, D0Ar2

	SETL 	[A0.2++], D0Re0, D1Re0
	MOV 	D0Re0, D1Ar1
	BR	$Lloop_5_6_7

	B 	$Lunaligned_end

$Lunaligned_1_2_3:
! Calculate the bit offsets required for the shift operations necesssary
! to align the data.
! D0Ar6 = bit offset, D1Ar5 = (32 - bit offset)
	MULW 	D0Ar6, D0Ar6, #8
	MOV	D1Ar5, #32
	SUB	D1Ar5, D1Ar5, D0Ar6

$Lloop_1_2_3:
! form 64-bit data in D0Re0,D1Re0
	LSR 	D0Re0, D0Re0, D0Ar6
	LSL 	D1Ar1, D1Re0, D1Ar5
	ADD 	D0Re0, D0Re0, D1Ar1
	MOV	D0Ar2, D1Re0
	LSR 	D0FrT, D0Ar2, D0Ar6
	GETL 	D0Ar2, D1Ar1, [++A1.2]

	MOV 	D1Re0, D0Ar2
	LSL 	D1Re0, D1Re0, D1Ar5
	ADD 	D1Re0, D1Re0, D0FrT

	SETL 	[A0.2++], D0Re0, D1Re0
	MOV 	D0Re0, D0Ar2
	MOV 	D1Re0, D1Ar1
	BR	$Lloop_1_2_3

	B 	$Lunaligned_end

! The 4 byte mis-alignment case - this does not require any shifting, just a
! shuffling of registers.
$Lunaligned_4:
	MOV 	D0Re0, D1Re0
$Lloop_4:
	GETL 	D0Ar2, D1Ar1, [++A1.2]
	MOV 	D1Re0, D0Ar2
	SETL 	[A0.2++], D0Re0, D1Re0
	MOV 	D0Re0, D1Ar1
	BR	$Lloop_4

$Lunaligned_end:
! If there are no remaining bytes to copy, we are done.
	ANDS 	D1Ar3, D1Ar3, #7
	BZ	$Lend
! Re-adjust the source pointer (A1.2) back to the actual (unaligned) byte
! address of the remaining bytes, and fall through to the byte copy loop.
	MOV 	D0Ar6, A1.2
	ADD 	D1Ar5, D0Ar4, D0Ar6
	MOV 	A1.2, D1Ar5
	B	$Lbyte_copy

	.size _memcpy,.-_memcpy
