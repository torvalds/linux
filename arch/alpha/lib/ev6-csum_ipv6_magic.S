/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/ev6-csum_ipv6_magic.S
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * unsigned short csum_ipv6_magic(struct in6_addr *saddr,
 *                                struct in6_addr *daddr,
 *                                __u32 len,
 *                                unsigned short proto,
 *                                unsigned int csum);
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 * Try not to change the actual algorithm if possible for consistency.
 * Determining actual stalls (other than slotting) doesn't appear to be easy to do.
 *
 * unsigned short csum_ipv6_magic(struct in6_addr *saddr,
 *                                struct in6_addr *daddr,
 *                                __u32 len,
 *                                unsigned short proto,
 *                                unsigned int csum);
 *
 * Swap <proto> (takes form 0xaabb)
 * Then shift it left by 48, so result is:
 *	0xbbaa0000 00000000
 * Then turn it back into a sign extended 32-bit item
 *	0xbbaa0000
 *
 * Swap <len> (an unsigned int) using Mike Burrows' 7-instruction sequence
 * (we can't hide the 3-cycle latency of the unpkbw in the 6-instruction sequence)
 * Assume input takes form 0xAABBCCDD
 *
 * Finally, original 'folding' approach is to split the long into 4 unsigned shorts
 * add 4 ushorts, resulting in ushort/carry
 * add carry bits + ushort --> ushort
 * add carry bits + ushort --> ushort (in case the carry results in an overflow)
 * Truncate to a ushort.  (took 13 instructions)
 * From doing some testing, using the approach in checksum.c:from64to16()
 * results in the same outcome:
 * split into 2 uints, add those, generating a ulong
 * add the 3 low ushorts together, generating a uint
 * a final add of the 2 lower ushorts
 * truncating the result.
 *
 * Misalignment handling added by Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 * The cost is 16 instructions (~8 cycles), including two extra loads which
 * may cause additional delay in rare cases (load-load replay traps).
 */

#include <asm/export.h>
	.globl csum_ipv6_magic
	.align 4
	.ent csum_ipv6_magic
	.frame $30,0,$26,0
csum_ipv6_magic:
	.prologue 0

	ldq_u	$0,0($16)	# L : Latency: 3
	inslh	$18,7,$4	# U : 0000000000AABBCC
	ldq_u	$1,8($16)	# L : Latency: 3
	sll	$19,8,$7	# U : U L U L : 0x00000000 00aabb00

	and	$16,7,$6	# E : src misalignment
	ldq_u	$5,15($16)	# L : Latency: 3
	zapnot	$20,15,$20	# U : zero extend incoming csum
	ldq_u	$2,0($17)	# L : U L U L : Latency: 3

	extql	$0,$6,$0	# U :
	extqh	$1,$6,$22	# U :
	ldq_u	$3,8($17)	# L : Latency: 3
	sll	$19,24,$19	# U : U U L U : 0x000000aa bb000000

	cmoveq	$6,$31,$22	# E : src aligned?
	ldq_u	$23,15($17)	# L : Latency: 3
	inswl	$18,3,$18	# U : 000000CCDD000000
	addl	$19,$7,$19	# E : U L U L : <sign bits>bbaabb00

	or	$0,$22,$0	# E : 1st src word complete
	extql	$1,$6,$1	# U :
	or	$18,$4,$18	# E : 000000CCDDAABBCC
	extqh	$5,$6,$5	# U : L U L U

	and	$17,7,$6	# E : dst misalignment
	extql	$2,$6,$2	# U :
	or	$1,$5,$1	# E : 2nd src word complete
	extqh	$3,$6,$22	# U : L U L U :

	cmoveq	$6,$31,$22	# E : dst aligned?
	extql	$3,$6,$3	# U :
	addq	$20,$0,$20	# E : begin summing the words
	extqh	$23,$6,$23	# U : L U L U :

	srl	$18,16,$4	# U : 0000000000CCDDAA
	or	$2,$22,$2	# E : 1st dst word complete
	zap	$19,0x3,$19	# U : <sign bits>bbaa0000
	or	$3,$23,$3	# E : U L U L : 2nd dst word complete

	cmpult	$20,$0,$0	# E :
	addq	$20,$1,$20	# E :
	zapnot	$18,0xa,$18	# U : 00000000DD00BB00
	zap	$4,0xa,$4	# U : U U L L : 0000000000CC00AA

	or	$18,$4,$18	# E : 00000000DDCCBBAA
	nop			# E :
	cmpult	$20,$1,$1	# E :
	addq	$20,$2,$20	# E : U L U L

	cmpult	$20,$2,$2	# E :
	addq	$20,$3,$20	# E :
	cmpult	$20,$3,$3	# E : (1 cycle stall on $20)
	addq	$20,$18,$20	# E : U L U L (1 cycle stall on $20)

	cmpult	$20,$18,$18	# E :
	addq	$20,$19,$20	# E : (1 cycle stall on $20)
	addq	$0,$1,$0	# E : merge the carries back into the csum
	addq	$2,$3,$2	# E :

	cmpult	$20,$19,$19	# E :
	addq	$18,$19,$18	# E : (1 cycle stall on $19)
	addq	$0,$2,$0	# E :
	addq	$20,$18,$20	# E : U L U L :
		/* (1 cycle stall on $18, 2 cycles on $20) */

	addq	$0,$20,$0	# E :
	zapnot	$0,15,$1	# U : Start folding output (1 cycle stall on $0)
	nop			# E :
	srl	$0,32,$0	# U : U L U L : (1 cycle stall on $0)

	addq	$1,$0,$1	# E : Finished generating ulong
	extwl	$1,2,$2		# U : ushort[1] (1 cycle stall on $1)
	zapnot	$1,3,$0		# U : ushort[0] (1 cycle stall on $1)
	extwl	$1,4,$1		# U : ushort[2] (1 cycle stall on $1)

	addq	$0,$2,$0	# E
	addq	$0,$1,$3	# E : Finished generating uint
		/* (1 cycle stall on $0) */
	extwl	$3,2,$1		# U : ushort[1] (1 cycle stall on $3)
	nop			# E : L U L U

	addq	$1,$3,$0	# E : Final carry
	not	$0,$4		# E : complement (1 cycle stall on $0)
	zapnot	$4,3,$0		# U : clear upper garbage bits
		/* (1 cycle stall on $4) */
	ret			# L0 : L U L U

	.end csum_ipv6_magic
	EXPORT_SYMBOL(csum_ipv6_magic)
