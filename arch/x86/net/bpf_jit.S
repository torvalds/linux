/* bpf_jit.S : BPF JIT helper functions
 *
 * Copyright (C) 2011 Eric Dumazet (eric.dumazet@gmail.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/linkage.h>
#include <asm/dwarf2.h>

/*
 * Calling convention :
 * rdi : skb pointer
 * esi : offset of byte(s) to fetch in skb (can be scratched)
 * r8  : copy of skb->data
 * r9d : hlen = skb->len - skb->data_len
 */
#define SKBDATA	%r8

sk_load_word_ind:
	.globl	sk_load_word_ind

	add	%ebx,%esi	/* offset += X */
#	test    %esi,%esi	/* if (offset < 0) goto bpf_error; */
	js	bpf_error

sk_load_word:
	.globl	sk_load_word

	mov	%r9d,%eax		# hlen
	sub	%esi,%eax		# hlen - offset
	cmp	$3,%eax
	jle	bpf_slow_path_word
	mov     (SKBDATA,%rsi),%eax
	bswap   %eax  			/* ntohl() */
	ret


sk_load_half_ind:
	.globl sk_load_half_ind

	add	%ebx,%esi	/* offset += X */
	js	bpf_error

sk_load_half:
	.globl	sk_load_half

	mov	%r9d,%eax
	sub	%esi,%eax		#	hlen - offset
	cmp	$1,%eax
	jle	bpf_slow_path_half
	movzwl	(SKBDATA,%rsi),%eax
	rol	$8,%ax			# ntohs()
	ret

sk_load_byte_ind:
	.globl sk_load_byte_ind
	add	%ebx,%esi	/* offset += X */
	js	bpf_error

sk_load_byte:
	.globl	sk_load_byte

	cmp	%esi,%r9d   /* if (offset >= hlen) goto bpf_slow_path_byte */
	jle	bpf_slow_path_byte
	movzbl	(SKBDATA,%rsi),%eax
	ret

/**
 * sk_load_byte_msh - BPF_S_LDX_B_MSH helper
 *
 * Implements BPF_S_LDX_B_MSH : ldxb  4*([offset]&0xf)
 * Must preserve A accumulator (%eax)
 * Inputs : %esi is the offset value, already known positive
 */
ENTRY(sk_load_byte_msh)
	CFI_STARTPROC
	cmp	%esi,%r9d      /* if (offset >= hlen) goto bpf_slow_path_byte_msh */
	jle	bpf_slow_path_byte_msh
	movzbl	(SKBDATA,%rsi),%ebx
	and	$15,%bl
	shl	$2,%bl
	ret
	CFI_ENDPROC
ENDPROC(sk_load_byte_msh)

bpf_error:
# force a return 0 from jit handler
	xor		%eax,%eax
	mov		-8(%rbp),%rbx
	leaveq
	ret

/* rsi contains offset and can be scratched */
#define bpf_slow_path_common(LEN)		\
	push	%rdi;    /* save skb */		\
	push	%r9;				\
	push	SKBDATA;			\
/* rsi already has offset */			\
	mov	$LEN,%ecx;	/* len */	\
	lea	-12(%rbp),%rdx;			\
	call	skb_copy_bits;			\
	test    %eax,%eax;			\
	pop	SKBDATA;			\
	pop	%r9;				\
	pop	%rdi


bpf_slow_path_word:
	bpf_slow_path_common(4)
	js	bpf_error
	mov	-12(%rbp),%eax
	bswap	%eax
	ret

bpf_slow_path_half:
	bpf_slow_path_common(2)
	js	bpf_error
	mov	-12(%rbp),%ax
	rol	$8,%ax
	movzwl	%ax,%eax
	ret

bpf_slow_path_byte:
	bpf_slow_path_common(1)
	js	bpf_error
	movzbl	-12(%rbp),%eax
	ret

bpf_slow_path_byte_msh:
	xchg	%eax,%ebx /* dont lose A , X is about to be scratched */
	bpf_slow_path_common(1)
	js	bpf_error
	movzbl	-12(%rbp),%eax
	and	$15,%al
	shl	$2,%al
	xchg	%eax,%ebx
	ret
