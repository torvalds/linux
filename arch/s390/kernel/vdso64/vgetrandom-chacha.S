/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/alternative.h>
#include <asm/dwarf.h>
#include <asm/fpu-insn.h>

#define STATE0	%v0
#define STATE1	%v1
#define STATE2	%v2
#define STATE3	%v3
#define COPY0	%v4
#define COPY1	%v5
#define COPY2	%v6
#define COPY3	%v7
#define BEPERM	%v19
#define TMP0	%v20
#define TMP1	%v21
#define TMP2	%v22
#define TMP3	%v23

	.section .rodata

	.balign 32
SYM_DATA_START_LOCAL(chacha20_constants)
	.long	0x61707865,0x3320646e,0x79622d32,0x6b206574 # endian-neutral
	.long	0x03020100,0x07060504,0x0b0a0908,0x0f0e0d0c # byte swap
SYM_DATA_END(chacha20_constants)

	.text
/*
 * s390 ChaCha20 implementation meant for vDSO. Produces a given positive
 * number of blocks of output with nonce 0, taking an input key and 8-bytes
 * counter. Does not spill to the stack.
 *
 * void __arch_chacha20_blocks_nostack(uint8_t *dst_bytes,
 *				       const uint8_t *key,
 *				       uint32_t *counter,
 *				       size_t nblocks)
 */
SYM_FUNC_START(__arch_chacha20_blocks_nostack)
	CFI_STARTPROC
	larl	%r1,chacha20_constants

	/* COPY0 = "expand 32-byte k" */
	VL	COPY0,0,,%r1

	/* BEPERM = byte selectors for VPERM */
	ALTERNATIVE __stringify(VL BEPERM,16,,%r1), "brcl 0,0", ALT_FACILITY(148)

	/* COPY1,COPY2 = key */
	VLM	COPY1,COPY2,0,%r3

	/* COPY3 = counter || zero nonce  */
	lg	%r3,0(%r4)
	VZERO	COPY3
	VLVGG	COPY3,%r3,0

	lghi	%r1,0
.Lblock:
	VLR	STATE0,COPY0
	VLR	STATE1,COPY1
	VLR	STATE2,COPY2
	VLR	STATE3,COPY3

	lghi	%r0,10
.Ldoubleround:
	/* STATE0 += STATE1, STATE3 = rotl32(STATE3 ^ STATE0, 16) */
	VAF	STATE0,STATE0,STATE1
	VX	STATE3,STATE3,STATE0
	VERLLF	STATE3,STATE3,16

	/* STATE2 += STATE3, STATE1 = rotl32(STATE1 ^ STATE2, 12) */
	VAF	STATE2,STATE2,STATE3
	VX	STATE1,STATE1,STATE2
	VERLLF	STATE1,STATE1,12

	/* STATE0 += STATE1, STATE3 = rotl32(STATE3 ^ STATE0, 8) */
	VAF	STATE0,STATE0,STATE1
	VX	STATE3,STATE3,STATE0
	VERLLF	STATE3,STATE3,8

	/* STATE2 += STATE3, STATE1 = rotl32(STATE1 ^ STATE2, 7) */
	VAF	STATE2,STATE2,STATE3
	VX	STATE1,STATE1,STATE2
	VERLLF	STATE1,STATE1,7

	/* STATE1[0,1,2,3] = STATE1[1,2,3,0] */
	VSLDB	STATE1,STATE1,STATE1,4
	/* STATE2[0,1,2,3] = STATE2[2,3,0,1] */
	VSLDB	STATE2,STATE2,STATE2,8
	/* STATE3[0,1,2,3] = STATE3[3,0,1,2] */
	VSLDB	STATE3,STATE3,STATE3,12

	/* STATE0 += STATE1, STATE3 = rotl32(STATE3 ^ STATE0, 16) */
	VAF	STATE0,STATE0,STATE1
	VX	STATE3,STATE3,STATE0
	VERLLF	STATE3,STATE3,16

	/* STATE2 += STATE3, STATE1 = rotl32(STATE1 ^ STATE2, 12) */
	VAF	STATE2,STATE2,STATE3
	VX	STATE1,STATE1,STATE2
	VERLLF	STATE1,STATE1,12

	/* STATE0 += STATE1, STATE3 = rotl32(STATE3 ^ STATE0, 8) */
	VAF	STATE0,STATE0,STATE1
	VX	STATE3,STATE3,STATE0
	VERLLF	STATE3,STATE3,8

	/* STATE2 += STATE3, STATE1 = rotl32(STATE1 ^ STATE2, 7) */
	VAF	STATE2,STATE2,STATE3
	VX	STATE1,STATE1,STATE2
	VERLLF	STATE1,STATE1,7

	/* STATE1[0,1,2,3] = STATE1[3,0,1,2] */
	VSLDB	STATE1,STATE1,STATE1,12
	/* STATE2[0,1,2,3] = STATE2[2,3,0,1] */
	VSLDB	STATE2,STATE2,STATE2,8
	/* STATE3[0,1,2,3] = STATE3[1,2,3,0] */
	VSLDB	STATE3,STATE3,STATE3,4
	brctg	%r0,.Ldoubleround

	/* OUTPUT0 = STATE0 + COPY0 */
	VAF	STATE0,STATE0,COPY0
	/* OUTPUT1 = STATE1 + COPY1 */
	VAF	STATE1,STATE1,COPY1
	/* OUTPUT2 = STATE2 + COPY2 */
	VAF	STATE2,STATE2,COPY2
	/* OUTPUT3 = STATE3 + COPY3 */
	VAF	STATE3,STATE3,COPY3

	ALTERNATIVE							\
		__stringify(						\
		/* Convert STATE to little endian and store to OUTPUT */\
		VPERM	TMP0,STATE0,STATE0,BEPERM;			\
		VPERM	TMP1,STATE1,STATE1,BEPERM;			\
		VPERM	TMP2,STATE2,STATE2,BEPERM;			\
		VPERM	TMP3,STATE3,STATE3,BEPERM;			\
		VSTM	TMP0,TMP3,0,%r2),				\
		__stringify(						\
		/* 32 bit wise little endian store to OUTPUT */		\
		VSTBRF	STATE0,0,,%r2;					\
		VSTBRF	STATE1,16,,%r2;					\
		VSTBRF	STATE2,32,,%r2;					\
		VSTBRF	STATE3,48,,%r2;					\
		brcl	0,0),						\
		ALT_FACILITY(148)

	/* ++COPY3.COUNTER */
	/* alsih %r3,1 */
	.insn	rilu,0xcc0a00000000,%r3,1
	alcr	%r3,%r1
	VLVGG	COPY3,%r3,0

	/* OUTPUT += 64, --NBLOCKS */
	aghi	%r2,64
	brctg	%r5,.Lblock

	/* COUNTER = COPY3.COUNTER */
	stg	%r3,0(%r4)

	/* Zero out potentially sensitive regs */
	VZERO	STATE0
	VZERO	STATE1
	VZERO	STATE2
	VZERO	STATE3
	VZERO	COPY1
	VZERO	COPY2

	/* Early exit if TMP0-TMP3 have not been used */
	ALTERNATIVE "nopr", "br %r14", ALT_FACILITY(148)

	VZERO	TMP0
	VZERO	TMP1
	VZERO	TMP2
	VZERO	TMP3

	br	%r14
	CFI_ENDPROC
SYM_FUNC_END(__arch_chacha20_blocks_nostack)
