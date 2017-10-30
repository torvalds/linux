#ifndef _BPF_JIT_H
#define _BPF_JIT_H

#ifndef __ASSEMBLER__
#define G0		0x00
#define G1		0x01
#define G2		0x02
#define G3		0x03
#define G6		0x06
#define G7		0x07
#define O0		0x08
#define O1		0x09
#define O2		0x0a
#define O3		0x0b
#define O4		0x0c
#define O5		0x0d
#define SP		0x0e
#define O7		0x0f
#define L0		0x10
#define L1		0x11
#define L2		0x12
#define L3		0x13
#define L4		0x14
#define L5		0x15
#define L6		0x16
#define L7		0x17
#define I0		0x18
#define I1		0x19
#define I2		0x1a
#define I3		0x1b
#define I4		0x1c
#define I5		0x1d
#define FP		0x1e
#define I7		0x1f

#define r_SKB		L0
#define r_HEADLEN	L4
#define r_SKB_DATA	L5
#define r_TMP		G1
#define r_TMP2		G3

/* assembly code in arch/sparc/net/bpf_jit_asm_64.S */
extern u32 bpf_jit_load_word[];
extern u32 bpf_jit_load_half[];
extern u32 bpf_jit_load_byte[];
extern u32 bpf_jit_load_byte_msh[];
extern u32 bpf_jit_load_word_positive_offset[];
extern u32 bpf_jit_load_half_positive_offset[];
extern u32 bpf_jit_load_byte_positive_offset[];
extern u32 bpf_jit_load_byte_msh_positive_offset[];
extern u32 bpf_jit_load_word_negative_offset[];
extern u32 bpf_jit_load_half_negative_offset[];
extern u32 bpf_jit_load_byte_negative_offset[];
extern u32 bpf_jit_load_byte_msh_negative_offset[];

#else
#define r_RESULT	%o0
#define r_SKB		%o0
#define r_OFF		%o1
#define r_HEADLEN	%l4
#define r_SKB_DATA	%l5
#define r_TMP		%g1
#define r_TMP2		%g3
#endif

#endif /* _BPF_JIT_H */
