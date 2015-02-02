/*
 * BPF Jit compiler for s390, help functions.
 *
 * Copyright IBM Corp. 2012
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#include <linux/linkage.h>

/*
 * Calling convention:
 * registers %r2, %r6-%r8, %r10-%r11, %r13, %r15 are call saved
 *   %r2: skb pointer
 *   %r3: offset parameter
 *   %r5: BPF A accumulator
 *   %r8: return address
 *   %r9: save register for skb pointer
 *   %r10: skb->data
 *   %r11: skb->len - skb->data_len (headlen)
 *   %r12: BPF X accumulator
 *
 * skb_copy_bits takes 4 parameters:
 *   %r2 = skb pointer
 *   %r3 = offset into skb data
 *   %r4 = pointer to temp buffer
 *   %r5 = length to copy
 */
#define SKBDATA	%r8

	/* A = *(u32 *) (skb->data+K+X) */
ENTRY(sk_load_word_ind)
	ar	%r3,%r12		# offset += X
	bmr	%r8			# < 0 -> return with cc

	/* A = *(u32 *) (skb->data+K) */
ENTRY(sk_load_word)
	llgfr	%r1,%r3			# extend offset
	ahi	%r3,4			# offset + 4
	clr	%r11,%r3		# hlen <= offset + 4 ?
	jl	sk_load_word_slow
	l	%r5,0(%r1,%r10)		# get word from skb
	xr	%r1,%r1			# set cc to zero
	br	%r8

sk_load_word_slow:
	lgr	%r9,%r2			# save %r2
	lgr	%r3,%r1			# offset
	la	%r4,160(%r15)		# pointer to temp buffer
	lghi	%r5,4			# 4 bytes
	brasl	%r14,skb_copy_bits	# get data from skb
	l	%r5,160(%r15)		# load result from temp buffer
	ltgr	%r2,%r2			# set cc to (%r2 != 0)
	lgr	%r2,%r9			# restore %r2
	br	%r8

	/* A = *(u16 *) (skb->data+K+X) */
ENTRY(sk_load_half_ind)
	ar	%r3,%r12		# offset += X
	bmr	%r8			# < 0 -> return with cc

	/* A = *(u16 *) (skb->data+K) */
ENTRY(sk_load_half)
	llgfr	%r1,%r3			# extend offset
	ahi	%r3,2			# offset + 2
	clr	%r11,%r3		# hlen <= offset + 2 ?
	jl	sk_load_half_slow
	llgh	%r5,0(%r1,%r10)		# get half from skb
	xr	%r1,%r1			# set cc to zero
	br	%r8

sk_load_half_slow:
	lgr	%r9,%r2			# save %r2
	lgr	%r3,%r1			# offset
	la	%r4,162(%r15)		# pointer to temp buffer
	lghi	%r5,2			# 2 bytes
	brasl	%r14,skb_copy_bits	# get data from skb
	xc	160(2,%r15),160(%r15)
	l	%r5,160(%r15)		# load result from temp buffer
	ltgr	%r2,%r2			# set cc to (%r2 != 0)
	lgr	%r2,%r9			# restore %r2
	br	%r8

	/* A = *(u8 *) (skb->data+K+X) */
ENTRY(sk_load_byte_ind)
	ar	%r3,%r12		# offset += X
	bmr	%r8			# < 0 -> return with cc

	/* A = *(u8 *) (skb->data+K) */
ENTRY(sk_load_byte)
	llgfr	%r1,%r3			# extend offset
	clr	%r11,%r3		# hlen < offset ?
	jle	sk_load_byte_slow
	lhi	%r5,0
	ic	%r5,0(%r1,%r10)		# get byte from skb
	xr	%r1,%r1			# set cc to zero
	br	%r8

sk_load_byte_slow:
	lgr	%r9,%r2			# save %r2
	lgr	%r3,%r1			# offset
	la	%r4,163(%r15)		# pointer to temp buffer
	lghi	%r5,1			# 1 byte
	brasl	%r14,skb_copy_bits	# get data from skb
	xc	160(3,%r15),160(%r15)
	l	%r5,160(%r15)		# load result from temp buffer
	ltgr	%r2,%r2			# set cc to (%r2 != 0)
	lgr	%r2,%r9			# restore %r2
	br	%r8

	/* X = (*(u8 *)(skb->data+K) & 0xf) << 2 */
ENTRY(sk_load_byte_msh)
	llgfr	%r1,%r3			# extend offset
	clr	%r11,%r3		# hlen < offset ?
	jle	sk_load_byte_msh_slow
	lhi	%r12,0
	ic	%r12,0(%r1,%r10)	# get byte from skb
	nill	%r12,0x0f
	sll	%r12,2
	xr	%r1,%r1			# set cc to zero
	br	%r8

sk_load_byte_msh_slow:
	lgr	%r9,%r2			# save %r2
	lgr	%r3,%r1			# offset
	la	%r4,163(%r15)		# pointer to temp buffer
	lghi	%r5,1			# 1 byte
	brasl	%r14,skb_copy_bits	# get data from skb
	xc	160(3,%r15),160(%r15)
	l	%r12,160(%r15)		# load result from temp buffer
	nill	%r12,0x0f
	sll	%r12,2
	ltgr	%r2,%r2			# set cc to (%r2 != 0)
	lgr	%r2,%r9			# restore %r2
	br	%r8
