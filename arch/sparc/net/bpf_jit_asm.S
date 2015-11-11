#include <asm/ptrace.h>

#include "bpf_jit.h"

#ifdef CONFIG_SPARC64
#define SAVE_SZ		176
#define SCRATCH_OFF	STACK_BIAS + 128
#define BE_PTR(label)	be,pn %xcc, label
#define SIGN_EXTEND(reg)	sra reg, 0, reg
#else
#define SAVE_SZ		96
#define SCRATCH_OFF	72
#define BE_PTR(label)	be label
#define SIGN_EXTEND(reg)
#endif

#define SKF_MAX_NEG_OFF	(-0x200000) /* SKF_LL_OFF from filter.h */

	.text
	.globl	bpf_jit_load_word
bpf_jit_load_word:
	cmp	r_OFF, 0
	bl	bpf_slow_path_word_neg
	 nop
	.globl	bpf_jit_load_word_positive_offset
bpf_jit_load_word_positive_offset:
	sub	r_HEADLEN, r_OFF, r_TMP
	cmp	r_TMP, 3
	ble	bpf_slow_path_word
	 add	r_SKB_DATA, r_OFF, r_TMP
	andcc	r_TMP, 3, %g0
	bne	load_word_unaligned
	 nop
	retl
	 ld	[r_TMP], r_A
load_word_unaligned:
	ldub	[r_TMP + 0x0], r_OFF
	ldub	[r_TMP + 0x1], r_TMP2
	sll	r_OFF, 8, r_OFF
	or	r_OFF, r_TMP2, r_OFF
	ldub	[r_TMP + 0x2], r_TMP2
	sll	r_OFF, 8, r_OFF
	or	r_OFF, r_TMP2, r_OFF
	ldub	[r_TMP + 0x3], r_TMP2
	sll	r_OFF, 8, r_OFF
	retl
	 or	r_OFF, r_TMP2, r_A

	.globl	bpf_jit_load_half
bpf_jit_load_half:
	cmp	r_OFF, 0
	bl	bpf_slow_path_half_neg
	 nop
	.globl	bpf_jit_load_half_positive_offset
bpf_jit_load_half_positive_offset:
	sub	r_HEADLEN, r_OFF, r_TMP
	cmp	r_TMP, 1
	ble	bpf_slow_path_half
	 add	r_SKB_DATA, r_OFF, r_TMP
	andcc	r_TMP, 1, %g0
	bne	load_half_unaligned
	 nop
	retl
	 lduh	[r_TMP], r_A
load_half_unaligned:
	ldub	[r_TMP + 0x0], r_OFF
	ldub	[r_TMP + 0x1], r_TMP2
	sll	r_OFF, 8, r_OFF
	retl
	 or	r_OFF, r_TMP2, r_A

	.globl	bpf_jit_load_byte
bpf_jit_load_byte:
	cmp	r_OFF, 0
	bl	bpf_slow_path_byte_neg
	 nop
	.globl	bpf_jit_load_byte_positive_offset
bpf_jit_load_byte_positive_offset:
	cmp	r_OFF, r_HEADLEN
	bge	bpf_slow_path_byte
	 nop
	retl
	 ldub	[r_SKB_DATA + r_OFF], r_A

	.globl	bpf_jit_load_byte_msh
bpf_jit_load_byte_msh:
	cmp	r_OFF, 0
	bl	bpf_slow_path_byte_msh_neg
	 nop
	.globl	bpf_jit_load_byte_msh_positive_offset
bpf_jit_load_byte_msh_positive_offset:
	cmp	r_OFF, r_HEADLEN
	bge	bpf_slow_path_byte_msh
	 nop
	ldub	[r_SKB_DATA + r_OFF], r_OFF
	and	r_OFF, 0xf, r_OFF
	retl
	 sll	r_OFF, 2, r_X

#define bpf_slow_path_common(LEN)	\
	save	%sp, -SAVE_SZ, %sp;	\
	mov	%i0, %o0;		\
	mov	r_OFF, %o1;		\
	add	%fp, SCRATCH_OFF, %o2;	\
	call	skb_copy_bits;		\
	 mov	(LEN), %o3;		\
	cmp	%o0, 0;			\
	restore;

bpf_slow_path_word:
	bpf_slow_path_common(4)
	bl	bpf_error
	 ld	[%sp + SCRATCH_OFF], r_A
	retl
	 nop
bpf_slow_path_half:
	bpf_slow_path_common(2)
	bl	bpf_error
	 lduh	[%sp + SCRATCH_OFF], r_A
	retl
	 nop
bpf_slow_path_byte:
	bpf_slow_path_common(1)
	bl	bpf_error
	 ldub	[%sp + SCRATCH_OFF], r_A
	retl
	 nop
bpf_slow_path_byte_msh:
	bpf_slow_path_common(1)
	bl	bpf_error
	 ldub	[%sp + SCRATCH_OFF], r_A
	and	r_OFF, 0xf, r_OFF
	retl
	 sll	r_OFF, 2, r_X

#define bpf_negative_common(LEN)			\
	save	%sp, -SAVE_SZ, %sp;			\
	mov	%i0, %o0;				\
	mov	r_OFF, %o1;				\
	SIGN_EXTEND(%o1);				\
	call	bpf_internal_load_pointer_neg_helper;	\
	 mov	(LEN), %o2;				\
	mov	%o0, r_TMP;				\
	cmp	%o0, 0;					\
	BE_PTR(bpf_error);				\
	 restore;

bpf_slow_path_word_neg:
	sethi	%hi(SKF_MAX_NEG_OFF), r_TMP
	cmp	r_OFF, r_TMP
	bl	bpf_error
	 nop
	.globl	bpf_jit_load_word_negative_offset
bpf_jit_load_word_negative_offset:
	bpf_negative_common(4)
	andcc	r_TMP, 3, %g0
	bne	load_word_unaligned
	 nop
	retl
	 ld	[r_TMP], r_A

bpf_slow_path_half_neg:
	sethi	%hi(SKF_MAX_NEG_OFF), r_TMP
	cmp	r_OFF, r_TMP
	bl	bpf_error
	 nop
	.globl	bpf_jit_load_half_negative_offset
bpf_jit_load_half_negative_offset:
	bpf_negative_common(2)
	andcc	r_TMP, 1, %g0
	bne	load_half_unaligned
	 nop
	retl
	 lduh	[r_TMP], r_A

bpf_slow_path_byte_neg:
	sethi	%hi(SKF_MAX_NEG_OFF), r_TMP
	cmp	r_OFF, r_TMP
	bl	bpf_error
	 nop
	.globl	bpf_jit_load_byte_negative_offset
bpf_jit_load_byte_negative_offset:
	bpf_negative_common(1)
	retl
	 ldub	[r_TMP], r_A

bpf_slow_path_byte_msh_neg:
	sethi	%hi(SKF_MAX_NEG_OFF), r_TMP
	cmp	r_OFF, r_TMP
	bl	bpf_error
	 nop
	.globl	bpf_jit_load_byte_msh_negative_offset
bpf_jit_load_byte_msh_negative_offset:
	bpf_negative_common(1)
	ldub	[r_TMP], r_OFF
	and	r_OFF, 0xf, r_OFF
	retl
	 sll	r_OFF, 2, r_X

bpf_error:
	/* Make the JIT program return zero.  The JIT epilogue
	 * stores away the original %o7 into r_saved_O7.  The
	 * normal leaf function return is to use "retl" which
	 * would evalute to "jmpl %o7 + 8, %g0" but we want to
	 * use the saved value thus the sequence you see here.
	 */
	jmpl	r_saved_O7 + 8, %g0
	 clr	%o0
