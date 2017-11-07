/* SPDX-License-Identifier: GPL-2.0 */

	.text
/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.
 *
 * extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);
 *
 */
	.global _ip_fast_csum
	.type   _ip_fast_csum,function
_ip_fast_csum:
	!! TXRPT needs loops - 1
	SUBS	TXRPT,D0Ar2,#1
	MOV	D0Re0,#0
	BLO	$Lfast_csum_exit
$Lfast_csum_loop:
	GETD	D1Ar3,[D1Ar1++]
	ADDS	D0Re0,D0Re0,D1Ar3
	ADDCS   D0Re0,D0Re0,#1
	BR	$Lfast_csum_loop
	LSR	D0Ar4,D0Re0,#16
	AND	D0Re0,D0Re0,#0xffff
	AND	D0Ar4,D0Ar4,#0xffff
	ADD	D0Re0,D0Re0,D0Ar4
	LSR	D0Ar4,D0Re0,#16
	ADD	D0Re0,D0Re0,D0Ar4
	XOR	D0Re0,D0Re0,#-1
	AND	D0Re0,D0Re0,#0xffff
$Lfast_csum_exit:
	MOV	PC,D1RtP
	.size _ip_fast_csum,.-_ip_fast_csum
