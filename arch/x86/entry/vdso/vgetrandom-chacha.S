// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <linux/linkage.h>
#include <asm/frame.h>

.section	.rodata, "a"
.align 16
CONSTANTS:	.octa 0x6b20657479622d323320646e61707865
.text

/*
 * Very basic SSE2 implementation of ChaCha20. Produces a given positive number
 * of blocks of output with a nonce of 0, taking an input key and 8-byte
 * counter. Importantly does not spill to the stack. Its arguments are:
 *
 *	rdi: output bytes
 *	rsi: 32-byte key input
 *	rdx: 8-byte counter input/output
 *	rcx: number of 64-byte blocks to write to output
 */
SYM_FUNC_START(__arch_chacha20_blocks_nostack)

.set	output,		%rdi
.set	key,		%rsi
.set	counter,	%rdx
.set	nblocks,	%rcx
.set	i,		%al
/* xmm registers are *not* callee-save. */
.set	temp,		%xmm0
.set	state0,		%xmm1
.set	state1,		%xmm2
.set	state2,		%xmm3
.set	state3,		%xmm4
.set	copy0,		%xmm5
.set	copy1,		%xmm6
.set	copy2,		%xmm7
.set	copy3,		%xmm8
.set	one,		%xmm9

	/* copy0 = "expand 32-byte k" */
	movaps		CONSTANTS(%rip),copy0
	/* copy1,copy2 = key */
	movups		0x00(key),copy1
	movups		0x10(key),copy2
	/* copy3 = counter || zero nonce */
	movq		0x00(counter),copy3
	/* one = 1 || 0 */
	movq		$1,%rax
	movq		%rax,one

.Lblock:
	/* state0,state1,state2,state3 = copy0,copy1,copy2,copy3 */
	movdqa		copy0,state0
	movdqa		copy1,state1
	movdqa		copy2,state2
	movdqa		copy3,state3

	movb		$10,i
.Lpermute:
	/* state0 += state1, state3 = rotl32(state3 ^ state0, 16) */
	paddd		state1,state0
	pxor		state0,state3
	movdqa		state3,temp
	pslld		$16,temp
	psrld		$16,state3
	por		temp,state3

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 12) */
	paddd		state3,state2
	pxor		state2,state1
	movdqa		state1,temp
	pslld		$12,temp
	psrld		$20,state1
	por		temp,state1

	/* state0 += state1, state3 = rotl32(state3 ^ state0, 8) */
	paddd		state1,state0
	pxor		state0,state3
	movdqa		state3,temp
	pslld		$8,temp
	psrld		$24,state3
	por		temp,state3

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 7) */
	paddd		state3,state2
	pxor		state2,state1
	movdqa		state1,temp
	pslld		$7,temp
	psrld		$25,state1
	por		temp,state1

	/* state1[0,1,2,3] = state1[1,2,3,0] */
	pshufd		$0x39,state1,state1
	/* state2[0,1,2,3] = state2[2,3,0,1] */
	pshufd		$0x4e,state2,state2
	/* state3[0,1,2,3] = state3[3,0,1,2] */
	pshufd		$0x93,state3,state3

	/* state0 += state1, state3 = rotl32(state3 ^ state0, 16) */
	paddd		state1,state0
	pxor		state0,state3
	movdqa		state3,temp
	pslld		$16,temp
	psrld		$16,state3
	por		temp,state3

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 12) */
	paddd		state3,state2
	pxor		state2,state1
	movdqa		state1,temp
	pslld		$12,temp
	psrld		$20,state1
	por		temp,state1

	/* state0 += state1, state3 = rotl32(state3 ^ state0, 8) */
	paddd		state1,state0
	pxor		state0,state3
	movdqa		state3,temp
	pslld		$8,temp
	psrld		$24,state3
	por		temp,state3

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 7) */
	paddd		state3,state2
	pxor		state2,state1
	movdqa		state1,temp
	pslld		$7,temp
	psrld		$25,state1
	por		temp,state1

	/* state1[0,1,2,3] = state1[3,0,1,2] */
	pshufd		$0x93,state1,state1
	/* state2[0,1,2,3] = state2[2,3,0,1] */
	pshufd		$0x4e,state2,state2
	/* state3[0,1,2,3] = state3[1,2,3,0] */
	pshufd		$0x39,state3,state3

	decb		i
	jnz		.Lpermute

	/* output0 = state0 + copy0 */
	paddd		copy0,state0
	movups		state0,0x00(output)
	/* output1 = state1 + copy1 */
	paddd		copy1,state1
	movups		state1,0x10(output)
	/* output2 = state2 + copy2 */
	paddd		copy2,state2
	movups		state2,0x20(output)
	/* output3 = state3 + copy3 */
	paddd		copy3,state3
	movups		state3,0x30(output)

	/* ++copy3.counter */
	paddq		one,copy3

	/* output += 64, --nblocks */
	addq		$64,output
	decq		nblocks
	jnz		.Lblock

	/* counter = copy3.counter */
	movq		copy3,0x00(counter)

	/* Zero out the potentially sensitive regs, in case nothing uses these again. */
	pxor		state0,state0
	pxor		state1,state1
	pxor		state2,state2
	pxor		state3,state3
	pxor		copy1,copy1
	pxor		copy2,copy2
	pxor		temp,temp

	ret
SYM_FUNC_END(__arch_chacha20_blocks_nostack)
