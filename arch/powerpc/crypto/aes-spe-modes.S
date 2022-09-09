/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AES modes (ECB/CBC/CTR/XTS) for PPC AES implementation
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <asm/ppc_asm.h>
#include "aes-spe-regs.h"

#ifdef __BIG_ENDIAN__			/* Macros for big endian builds	*/

#define LOAD_DATA(reg, off) \
	lwz		reg,off(rSP);	/* load with offset		*/
#define SAVE_DATA(reg, off) \
	stw		reg,off(rDP);	/* save with offset		*/
#define NEXT_BLOCK \
	addi		rSP,rSP,16;	/* increment pointers per bloc	*/ \
	addi		rDP,rDP,16;
#define LOAD_IV(reg, off) \
	lwz		reg,off(rIP);	/* IV loading with offset	*/
#define SAVE_IV(reg, off) \
	stw		reg,off(rIP);	/* IV saving with offset	*/
#define START_IV			/* nothing to reset		*/
#define CBC_DEC 16			/* CBC decrement per block	*/
#define CTR_DEC 1			/* CTR decrement one byte	*/

#else					/* Macros for little endian	*/

#define LOAD_DATA(reg, off) \
	lwbrx		reg,0,rSP;	/* load reversed		*/ \
	addi		rSP,rSP,4;	/* and increment pointer	*/
#define SAVE_DATA(reg, off) \
	stwbrx		reg,0,rDP;	/* save reversed		*/ \
	addi		rDP,rDP,4;	/* and increment pointer	*/
#define NEXT_BLOCK			/* nothing todo			*/
#define LOAD_IV(reg, off) \
	lwbrx		reg,0,rIP;	/* load reversed		*/ \
	addi		rIP,rIP,4;	/* and increment pointer	*/
#define SAVE_IV(reg, off) \
	stwbrx		reg,0,rIP;	/* load reversed		*/ \
	addi		rIP,rIP,4;	/* and increment pointer	*/
#define START_IV \
	subi		rIP,rIP,16;	/* must reset pointer		*/
#define CBC_DEC 32			/* 2 blocks because of incs	*/
#define CTR_DEC 17			/* 1 block because of incs	*/

#endif

#define SAVE_0_REGS
#define LOAD_0_REGS

#define SAVE_4_REGS \
	stw		rI0,96(r1);	/* save 32 bit registers	*/ \
	stw		rI1,100(r1);					   \
	stw		rI2,104(r1);					   \
	stw		rI3,108(r1);

#define LOAD_4_REGS \
	lwz		rI0,96(r1);	/* restore 32 bit registers	*/ \
	lwz		rI1,100(r1);					   \
	lwz		rI2,104(r1);					   \
	lwz		rI3,108(r1);

#define SAVE_8_REGS \
	SAVE_4_REGS							   \
	stw		rG0,112(r1);	/* save 32 bit registers	*/ \
	stw		rG1,116(r1);					   \
	stw		rG2,120(r1);					   \
	stw		rG3,124(r1);

#define LOAD_8_REGS \
	LOAD_4_REGS							   \
	lwz		rG0,112(r1);	/* restore 32 bit registers	*/ \
	lwz		rG1,116(r1);					   \
	lwz		rG2,120(r1);					   \
	lwz		rG3,124(r1);

#define INITIALIZE_CRYPT(tab,nr32bitregs) \
	mflr		r0;						   \
	stwu		r1,-160(r1);	/* create stack frame		*/ \
	lis		rT0,tab@h;	/* en-/decryption table pointer	*/ \
	stw		r0,8(r1);	/* save link register		*/ \
	ori		rT0,rT0,tab@l;					   \
	evstdw		r14,16(r1);					   \
	mr		rKS,rKP;					   \
	evstdw		r15,24(r1);	/* We must save non volatile	*/ \
	evstdw		r16,32(r1);	/* registers. Take the chance	*/ \
	evstdw		r17,40(r1);	/* and save the SPE part too	*/ \
	evstdw		r18,48(r1);					   \
	evstdw		r19,56(r1);					   \
	evstdw		r20,64(r1);					   \
	evstdw		r21,72(r1);					   \
	evstdw		r22,80(r1);					   \
	evstdw		r23,88(r1);					   \
	SAVE_##nr32bitregs##_REGS

#define FINALIZE_CRYPT(nr32bitregs) \
	lwz		r0,8(r1);					   \
	evldw		r14,16(r1);	/* restore SPE registers	*/ \
	evldw		r15,24(r1);					   \
	evldw		r16,32(r1);					   \
	evldw		r17,40(r1);					   \
	evldw		r18,48(r1);					   \
	evldw		r19,56(r1);					   \
	evldw		r20,64(r1);					   \
	evldw		r21,72(r1);					   \
	evldw		r22,80(r1);					   \
	evldw		r23,88(r1);					   \
	LOAD_##nr32bitregs##_REGS					   \
	mtlr		r0;		/* restore link register	*/ \
	xor		r0,r0,r0;					   \
	stw		r0,16(r1);	/* delete sensitive data	*/ \
	stw		r0,24(r1);	/* that we might have pushed	*/ \
	stw		r0,32(r1);	/* from other context that runs	*/ \
	stw		r0,40(r1);	/* the same code		*/ \
	stw		r0,48(r1);					   \
	stw		r0,56(r1);					   \
	stw		r0,64(r1);					   \
	stw		r0,72(r1);					   \
	stw		r0,80(r1);					   \
	stw		r0,88(r1);					   \
	addi		r1,r1,160;	/* cleanup stack frame		*/

#define ENDIAN_SWAP(t0, t1, s0, s1) \
	rotrwi		t0,s0,8;	/* swap endianness for 2 GPRs	*/ \
	rotrwi		t1,s1,8;					   \
	rlwimi		t0,s0,8,8,15;					   \
	rlwimi		t1,s1,8,8,15;					   \
	rlwimi		t0,s0,8,24,31;					   \
	rlwimi		t1,s1,8,24,31;

#define GF128_MUL(d0, d1, d2, d3, t0) \
	li		t0,0x87;	/* multiplication in GF128	*/ \
	cmpwi		d3,-1;						   \
	iselgt		t0,0,t0;					   \
	rlwimi		d3,d2,0,0,0;	/* propagate "carry" bits	*/ \
	rotlwi		d3,d3,1;					   \
	rlwimi		d2,d1,0,0,0;					   \
	rotlwi		d2,d2,1;					   \
	rlwimi		d1,d0,0,0,0;					   \
	slwi		d0,d0,1;	/* shift left 128 bit		*/ \
	rotlwi		d1,d1,1;					   \
	xor		d0,d0,t0;

#define START_KEY(d0, d1, d2, d3) \
	lwz		rW0,0(rKP);					   \
	mtctr		rRR;						   \
	lwz		rW1,4(rKP);					   \
	lwz		rW2,8(rKP);					   \
	lwz		rW3,12(rKP);					   \
	xor		rD0,d0,rW0;					   \
	xor		rD1,d1,rW1;					   \
	xor		rD2,d2,rW2;					   \
	xor		rD3,d3,rW3;

/*
 * ppc_encrypt_aes(u8 *out, const u8 *in, u32 *key_enc,
 *		   u32 rounds)
 *
 * called from glue layer to encrypt a single 16 byte block
 * round values are AES128 = 4, AES192 = 5, AES256 = 6
 *
 */
_GLOBAL(ppc_encrypt_aes)
	INITIALIZE_CRYPT(PPC_AES_4K_ENCTAB, 0)
	LOAD_DATA(rD0, 0)
	LOAD_DATA(rD1, 4)
	LOAD_DATA(rD2, 8)
	LOAD_DATA(rD3, 12)
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_encrypt_block
	xor		rD0,rD0,rW0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rW1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rW2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rW3
	SAVE_DATA(rD3, 12)
	FINALIZE_CRYPT(0)
	blr

/*
 * ppc_decrypt_aes(u8 *out, const u8 *in, u32 *key_dec,
 *		   u32 rounds)
 *
 * called from glue layer to decrypt a single 16 byte block
 * round values are AES128 = 4, AES192 = 5, AES256 = 6
 *
 */
_GLOBAL(ppc_decrypt_aes)
	INITIALIZE_CRYPT(PPC_AES_4K_DECTAB,0)
	LOAD_DATA(rD0, 0)
	addi		rT1,rT0,4096
	LOAD_DATA(rD1, 4)
	LOAD_DATA(rD2, 8)
	LOAD_DATA(rD3, 12)
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_decrypt_block
	xor		rD0,rD0,rW0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rW1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rW2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rW3
	SAVE_DATA(rD3, 12)
	FINALIZE_CRYPT(0)
	blr

/*
 * ppc_encrypt_ecb(u8 *out, const u8 *in, u32 *key_enc,
 *		   u32 rounds, u32 bytes);
 *
 * called from glue layer to encrypt multiple blocks via ECB
 * Bytes must be larger or equal 16 and only whole blocks are
 * processed. round values are AES128 = 4, AES192 = 5 and
 * AES256 = 6
 *
 */
_GLOBAL(ppc_encrypt_ecb)
	INITIALIZE_CRYPT(PPC_AES_4K_ENCTAB, 0)
ppc_encrypt_ecb_loop:
	LOAD_DATA(rD0, 0)
	mr		rKP,rKS
	LOAD_DATA(rD1, 4)
	subi		rLN,rLN,16
	LOAD_DATA(rD2, 8)
	cmpwi		rLN,15
	LOAD_DATA(rD3, 12)
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_encrypt_block
	xor		rD0,rD0,rW0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rW1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rW2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rW3
	SAVE_DATA(rD3, 12)
	NEXT_BLOCK
	bt		gt,ppc_encrypt_ecb_loop
	FINALIZE_CRYPT(0)
	blr

/*
 * ppc_decrypt_ecb(u8 *out, const u8 *in, u32 *key_dec,
 *		   u32 rounds, u32 bytes);
 *
 * called from glue layer to decrypt multiple blocks via ECB
 * Bytes must be larger or equal 16 and only whole blocks are
 * processed. round values are AES128 = 4, AES192 = 5 and
 * AES256 = 6
 *
 */
_GLOBAL(ppc_decrypt_ecb)
	INITIALIZE_CRYPT(PPC_AES_4K_DECTAB, 0)
	addi		rT1,rT0,4096
ppc_decrypt_ecb_loop:
	LOAD_DATA(rD0, 0)
	mr		rKP,rKS
	LOAD_DATA(rD1, 4)
	subi		rLN,rLN,16
	LOAD_DATA(rD2, 8)
	cmpwi		rLN,15
	LOAD_DATA(rD3, 12)
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_decrypt_block
	xor		rD0,rD0,rW0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rW1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rW2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rW3
	SAVE_DATA(rD3, 12)
	NEXT_BLOCK
	bt		gt,ppc_decrypt_ecb_loop
	FINALIZE_CRYPT(0)
	blr

/*
 * ppc_encrypt_cbc(u8 *out, const u8 *in, u32 *key_enc,
 *		   32 rounds, u32 bytes, u8 *iv);
 *
 * called from glue layer to encrypt multiple blocks via CBC
 * Bytes must be larger or equal 16 and only whole blocks are
 * processed. round values are AES128 = 4, AES192 = 5 and
 * AES256 = 6
 *
 */
_GLOBAL(ppc_encrypt_cbc)
	INITIALIZE_CRYPT(PPC_AES_4K_ENCTAB, 4)
	LOAD_IV(rI0, 0)
	LOAD_IV(rI1, 4)
	LOAD_IV(rI2, 8)
	LOAD_IV(rI3, 12)
ppc_encrypt_cbc_loop:
	LOAD_DATA(rD0, 0)
	mr		rKP,rKS
	LOAD_DATA(rD1, 4)
	subi		rLN,rLN,16
	LOAD_DATA(rD2, 8)
	cmpwi		rLN,15
	LOAD_DATA(rD3, 12)
	xor		rD0,rD0,rI0
	xor		rD1,rD1,rI1
	xor		rD2,rD2,rI2
	xor		rD3,rD3,rI3
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_encrypt_block
	xor		rI0,rD0,rW0
	SAVE_DATA(rI0, 0)
	xor		rI1,rD1,rW1
	SAVE_DATA(rI1, 4)
	xor		rI2,rD2,rW2
	SAVE_DATA(rI2, 8)
	xor		rI3,rD3,rW3
	SAVE_DATA(rI3, 12)
	NEXT_BLOCK
	bt		gt,ppc_encrypt_cbc_loop
	START_IV
	SAVE_IV(rI0, 0)
	SAVE_IV(rI1, 4)
	SAVE_IV(rI2, 8)
	SAVE_IV(rI3, 12)
	FINALIZE_CRYPT(4)
	blr

/*
 * ppc_decrypt_cbc(u8 *out, const u8 *in, u32 *key_dec,
 *		   u32 rounds, u32 bytes, u8 *iv);
 *
 * called from glue layer to decrypt multiple blocks via CBC
 * round values are AES128 = 4, AES192 = 5, AES256 = 6
 *
 */
_GLOBAL(ppc_decrypt_cbc)
	INITIALIZE_CRYPT(PPC_AES_4K_DECTAB, 4)
	li		rT1,15
	LOAD_IV(rI0, 0)
	andc		rLN,rLN,rT1
	LOAD_IV(rI1, 4)
	subi		rLN,rLN,16
	LOAD_IV(rI2, 8)
	add		rSP,rSP,rLN	/* reverse processing		*/
	LOAD_IV(rI3, 12)
	add		rDP,rDP,rLN
	LOAD_DATA(rD0, 0)
	addi		rT1,rT0,4096
	LOAD_DATA(rD1, 4)
	LOAD_DATA(rD2, 8)
	LOAD_DATA(rD3, 12)
	START_IV
	SAVE_IV(rD0, 0)
	SAVE_IV(rD1, 4)
	SAVE_IV(rD2, 8)
	cmpwi		rLN,16
	SAVE_IV(rD3, 12)
	bt		lt,ppc_decrypt_cbc_end
ppc_decrypt_cbc_loop:
	mr		rKP,rKS
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_decrypt_block
	subi		rLN,rLN,16
	subi		rSP,rSP,CBC_DEC
	xor		rW0,rD0,rW0
	LOAD_DATA(rD0, 0)
	xor		rW1,rD1,rW1
	LOAD_DATA(rD1, 4)
	xor		rW2,rD2,rW2
	LOAD_DATA(rD2, 8)
	xor		rW3,rD3,rW3
	LOAD_DATA(rD3, 12)
	xor		rW0,rW0,rD0
	SAVE_DATA(rW0, 0)
	xor		rW1,rW1,rD1
	SAVE_DATA(rW1, 4)
	xor		rW2,rW2,rD2
	SAVE_DATA(rW2, 8)
	xor		rW3,rW3,rD3
	SAVE_DATA(rW3, 12)
	cmpwi		rLN,15
	subi		rDP,rDP,CBC_DEC
	bt		gt,ppc_decrypt_cbc_loop
ppc_decrypt_cbc_end:
	mr		rKP,rKS
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_decrypt_block
	xor		rW0,rW0,rD0
	xor		rW1,rW1,rD1
	xor		rW2,rW2,rD2
	xor		rW3,rW3,rD3
	xor		rW0,rW0,rI0	/* decrypt with initial IV	*/
	SAVE_DATA(rW0, 0)
	xor		rW1,rW1,rI1
	SAVE_DATA(rW1, 4)
	xor		rW2,rW2,rI2
	SAVE_DATA(rW2, 8)
	xor		rW3,rW3,rI3
	SAVE_DATA(rW3, 12)
	FINALIZE_CRYPT(4)
	blr

/*
 * ppc_crypt_ctr(u8 *out, const u8 *in, u32 *key_enc,
 *		 u32 rounds, u32 bytes, u8 *iv);
 *
 * called from glue layer to encrypt/decrypt multiple blocks
 * via CTR. Number of bytes does not need to be a multiple of
 * 16. Round values are AES128 = 4, AES192 = 5, AES256 = 6
 *
 */
_GLOBAL(ppc_crypt_ctr)
	INITIALIZE_CRYPT(PPC_AES_4K_ENCTAB, 4)
	LOAD_IV(rI0, 0)
	LOAD_IV(rI1, 4)
	LOAD_IV(rI2, 8)
	cmpwi		rLN,16
	LOAD_IV(rI3, 12)
	START_IV
	bt		lt,ppc_crypt_ctr_partial
ppc_crypt_ctr_loop:
	mr		rKP,rKS
	START_KEY(rI0, rI1, rI2, rI3)
	bl		ppc_encrypt_block
	xor		rW0,rD0,rW0
	xor		rW1,rD1,rW1
	xor		rW2,rD2,rW2
	xor		rW3,rD3,rW3
	LOAD_DATA(rD0, 0)
	subi		rLN,rLN,16
	LOAD_DATA(rD1, 4)
	LOAD_DATA(rD2, 8)
	LOAD_DATA(rD3, 12)
	xor		rD0,rD0,rW0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rW1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rW2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rW3
	SAVE_DATA(rD3, 12)
	addic		rI3,rI3,1	/* increase counter			*/
	addze		rI2,rI2
	addze		rI1,rI1
	addze		rI0,rI0
	NEXT_BLOCK
	cmpwi		rLN,15
	bt		gt,ppc_crypt_ctr_loop
ppc_crypt_ctr_partial:
	cmpwi		rLN,0
	bt		eq,ppc_crypt_ctr_end
	mr		rKP,rKS
	START_KEY(rI0, rI1, rI2, rI3)
	bl		ppc_encrypt_block
	xor		rW0,rD0,rW0
	SAVE_IV(rW0, 0)
	xor		rW1,rD1,rW1
	SAVE_IV(rW1, 4)
	xor		rW2,rD2,rW2
	SAVE_IV(rW2, 8)
	xor		rW3,rD3,rW3
	SAVE_IV(rW3, 12)
	mtctr		rLN
	subi		rIP,rIP,CTR_DEC
	subi		rSP,rSP,1
	subi		rDP,rDP,1
ppc_crypt_ctr_xorbyte:
	lbzu		rW4,1(rIP)	/* bytewise xor for partial block	*/
	lbzu		rW5,1(rSP)
	xor		rW4,rW4,rW5
	stbu		rW4,1(rDP)
	bdnz		ppc_crypt_ctr_xorbyte
	subf		rIP,rLN,rIP
	addi		rIP,rIP,1
	addic		rI3,rI3,1
	addze		rI2,rI2
	addze		rI1,rI1
	addze		rI0,rI0
ppc_crypt_ctr_end:
	SAVE_IV(rI0, 0)
	SAVE_IV(rI1, 4)
	SAVE_IV(rI2, 8)
	SAVE_IV(rI3, 12)
	FINALIZE_CRYPT(4)
	blr

/*
 * ppc_encrypt_xts(u8 *out, const u8 *in, u32 *key_enc,
 *		   u32 rounds, u32 bytes, u8 *iv, u32 *key_twk);
 *
 * called from glue layer to encrypt multiple blocks via XTS
 * If key_twk is given, the initial IV encryption will be
 * processed too. Round values are AES128 = 4, AES192 = 5,
 * AES256 = 6
 *
 */
_GLOBAL(ppc_encrypt_xts)
	INITIALIZE_CRYPT(PPC_AES_4K_ENCTAB, 8)
	LOAD_IV(rI0, 0)
	LOAD_IV(rI1, 4)
	LOAD_IV(rI2, 8)
	cmpwi		rKT,0
	LOAD_IV(rI3, 12)
	bt		eq,ppc_encrypt_xts_notweak
	mr		rKP,rKT
	START_KEY(rI0, rI1, rI2, rI3)
	bl		ppc_encrypt_block
	xor		rI0,rD0,rW0
	xor		rI1,rD1,rW1
	xor		rI2,rD2,rW2
	xor		rI3,rD3,rW3
ppc_encrypt_xts_notweak:
	ENDIAN_SWAP(rG0, rG1, rI0, rI1)
	ENDIAN_SWAP(rG2, rG3, rI2, rI3)
ppc_encrypt_xts_loop:
	LOAD_DATA(rD0, 0)
	mr		rKP,rKS
	LOAD_DATA(rD1, 4)
	subi		rLN,rLN,16
	LOAD_DATA(rD2, 8)
	LOAD_DATA(rD3, 12)
	xor		rD0,rD0,rI0
	xor		rD1,rD1,rI1
	xor		rD2,rD2,rI2
	xor		rD3,rD3,rI3
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_encrypt_block
	xor		rD0,rD0,rW0
	xor		rD1,rD1,rW1
	xor		rD2,rD2,rW2
	xor		rD3,rD3,rW3
	xor		rD0,rD0,rI0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rI1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rI2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rI3
	SAVE_DATA(rD3, 12)
	GF128_MUL(rG0, rG1, rG2, rG3, rW0)
	ENDIAN_SWAP(rI0, rI1, rG0, rG1)
	ENDIAN_SWAP(rI2, rI3, rG2, rG3)
	cmpwi		rLN,0
	NEXT_BLOCK
	bt		gt,ppc_encrypt_xts_loop
	START_IV
	SAVE_IV(rI0, 0)
	SAVE_IV(rI1, 4)
	SAVE_IV(rI2, 8)
	SAVE_IV(rI3, 12)
	FINALIZE_CRYPT(8)
	blr

/*
 * ppc_decrypt_xts(u8 *out, const u8 *in, u32 *key_dec,
 *		   u32 rounds, u32 blocks, u8 *iv, u32 *key_twk);
 *
 * called from glue layer to decrypt multiple blocks via XTS
 * If key_twk is given, the initial IV encryption will be
 * processed too. Round values are AES128 = 4, AES192 = 5,
 * AES256 = 6
 *
 */
_GLOBAL(ppc_decrypt_xts)
	INITIALIZE_CRYPT(PPC_AES_4K_DECTAB, 8)
	LOAD_IV(rI0, 0)
	addi		rT1,rT0,4096
	LOAD_IV(rI1, 4)
	LOAD_IV(rI2, 8)
	cmpwi		rKT,0
	LOAD_IV(rI3, 12)
	bt		eq,ppc_decrypt_xts_notweak
	subi		rT0,rT0,4096
	mr		rKP,rKT
	START_KEY(rI0, rI1, rI2, rI3)
	bl		ppc_encrypt_block
	xor		rI0,rD0,rW0
	xor		rI1,rD1,rW1
	xor		rI2,rD2,rW2
	xor		rI3,rD3,rW3
	addi		rT0,rT0,4096
ppc_decrypt_xts_notweak:
	ENDIAN_SWAP(rG0, rG1, rI0, rI1)
	ENDIAN_SWAP(rG2, rG3, rI2, rI3)
ppc_decrypt_xts_loop:
	LOAD_DATA(rD0, 0)
	mr		rKP,rKS
	LOAD_DATA(rD1, 4)
	subi		rLN,rLN,16
	LOAD_DATA(rD2, 8)
	LOAD_DATA(rD3, 12)
	xor		rD0,rD0,rI0
	xor		rD1,rD1,rI1
	xor		rD2,rD2,rI2
	xor		rD3,rD3,rI3
	START_KEY(rD0, rD1, rD2, rD3)
	bl		ppc_decrypt_block
	xor		rD0,rD0,rW0
	xor		rD1,rD1,rW1
	xor		rD2,rD2,rW2
	xor		rD3,rD3,rW3
	xor		rD0,rD0,rI0
	SAVE_DATA(rD0, 0)
	xor		rD1,rD1,rI1
	SAVE_DATA(rD1, 4)
	xor		rD2,rD2,rI2
	SAVE_DATA(rD2, 8)
	xor		rD3,rD3,rI3
	SAVE_DATA(rD3, 12)
	GF128_MUL(rG0, rG1, rG2, rG3, rW0)
	ENDIAN_SWAP(rI0, rI1, rG0, rG1)
	ENDIAN_SWAP(rI2, rI3, rG2, rG3)
	cmpwi		rLN,0
	NEXT_BLOCK
	bt		gt,ppc_decrypt_xts_loop
	START_IV
	SAVE_IV(rI0, 0)
	SAVE_IV(rI1, 4)
	SAVE_IV(rI2, 8)
	SAVE_IV(rI3, 12)
	FINALIZE_CRYPT(8)
	blr
