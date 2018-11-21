/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF Jit compiler for s390, help functions.
 *
 * Copyright IBM Corp. 2012,2015
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <linux/linkage.h>
#include <asm/nospec-insn.h>
#include "bpf_jit.h"

/*
 * Calling convention:
 * registers %r7-%r10, %r11,%r13, and %r15 are call saved
 *
 * Input (64 bit):
 *   %r3 (%b2) = offset into skb data
 *   %r6 (%b5) = return address
 *   %r7 (%b6) = skb pointer
 *   %r12      = skb data pointer
 *
 * Output:
 *   %r14= %b0 = return value (read skb value)
 *
 * Work registers: %r2,%r4,%r5,%r14
 *
 * skb_copy_bits takes 4 parameters:
 *   %r2 = skb pointer
 *   %r3 = offset into skb data
 *   %r4 = pointer to temp buffer
 *   %r5 = length to copy
 *   Return value in %r2: 0 = ok
 *
 * bpf_internal_load_pointer_neg_helper takes 3 parameters:
 *   %r2 = skb pointer
 *   %r3 = offset into data
 *   %r4 = length to copy
 *   Return value in %r2: Pointer to data
 */

#define SKF_MAX_NEG_OFF	-0x200000	/* SKF_LL_OFF from filter.h */

/*
 * Load SIZE bytes from SKB
 */
#define sk_load_common(NAME, SIZE, LOAD)				\
ENTRY(sk_load_##NAME);							\
	ltgr	%r3,%r3;		/* Is offset negative? */	\
	jl	sk_load_##NAME##_slow_neg;				\
ENTRY(sk_load_##NAME##_pos);						\
	aghi	%r3,SIZE;		/* Offset + SIZE */		\
	clg	%r3,STK_OFF_HLEN(%r15);	/* Offset + SIZE > hlen? */	\
	jh	sk_load_##NAME##_slow;					\
	LOAD	%r14,-SIZE(%r3,%r12);	/* Get data from skb */		\
	B_EX	OFF_OK,%r6;		/* Return */			\
									\
sk_load_##NAME##_slow:;							\
	lgr	%r2,%r7;		/* Arg1 = skb pointer */	\
	aghi	%r3,-SIZE;		/* Arg2 = offset */		\
	la	%r4,STK_OFF_TMP(%r15);	/* Arg3 = temp bufffer */	\
	lghi	%r5,SIZE;		/* Arg4 = size */		\
	brasl	%r14,skb_copy_bits;	/* Get data from skb */		\
	LOAD	%r14,STK_OFF_TMP(%r15);	/* Load from temp bufffer */	\
	ltgr	%r2,%r2;		/* Set cc to (%r2 != 0) */	\
	BR_EX	%r6;			/* Return */

sk_load_common(word, 4, llgf)	/* r14 = *(u32 *) (skb->data+offset) */
sk_load_common(half, 2, llgh)	/* r14 = *(u16 *) (skb->data+offset) */

	GEN_BR_THUNK %r6
	GEN_B_THUNK OFF_OK,%r6

/*
 * Load 1 byte from SKB (optimized version)
 */
	/* r14 = *(u8 *) (skb->data+offset) */
ENTRY(sk_load_byte)
	ltgr	%r3,%r3			# Is offset negative?
	jl	sk_load_byte_slow_neg
ENTRY(sk_load_byte_pos)
	clg	%r3,STK_OFF_HLEN(%r15)	# Offset >= hlen?
	jnl	sk_load_byte_slow
	llgc	%r14,0(%r3,%r12)	# Get byte from skb
	B_EX	OFF_OK,%r6		# Return OK

sk_load_byte_slow:
	lgr	%r2,%r7			# Arg1 = skb pointer
					# Arg2 = offset
	la	%r4,STK_OFF_TMP(%r15)	# Arg3 = pointer to temp buffer
	lghi	%r5,1			# Arg4 = size (1 byte)
	brasl	%r14,skb_copy_bits	# Get data from skb
	llgc	%r14,STK_OFF_TMP(%r15)	# Load result from temp buffer
	ltgr	%r2,%r2			# Set cc to (%r2 != 0)
	BR_EX	%r6			# Return cc

#define sk_negative_common(NAME, SIZE, LOAD)				\
sk_load_##NAME##_slow_neg:;						\
	cgfi	%r3,SKF_MAX_NEG_OFF;					\
	jl	bpf_error;						\
	lgr	%r2,%r7;		/* Arg1 = skb pointer */	\
					/* Arg2 = offset */		\
	lghi	%r4,SIZE;		/* Arg3 = size */		\
	brasl	%r14,bpf_internal_load_pointer_neg_helper;		\
	ltgr	%r2,%r2;						\
	jz	bpf_error;						\
	LOAD	%r14,0(%r2);		/* Get data from pointer */	\
	xr	%r3,%r3;		/* Set cc to zero */		\
	BR_EX	%r6;			/* Return cc */

sk_negative_common(word, 4, llgf)
sk_negative_common(half, 2, llgh)
sk_negative_common(byte, 1, llgc)

bpf_error:
# force a return 0 from jit handler
	ltgr	%r15,%r15	# Set condition code
	BR_EX	%r6
