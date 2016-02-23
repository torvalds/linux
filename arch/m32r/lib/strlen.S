/*
 *  linux/arch/m32r/strlen.S --  strlen code.
 *
 *  Copyright (C) 2001  Hirokazu Takata
 *
 *  size_t strlen(const char *s);
 *
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#ifdef CONFIG_ISA_DUAL_ISSUE

	.text
ENTRY(strlen)
	mv	r6, r0		    ||	ldi	r2, #0
	and3	r0, r0, #3
	bnez	r0, strlen_byte
;
strlen_word:
	ld	r0, @r6+
;
	seth	r5, #high(0x01010101)
	or3	r5, r5, #low(0x01010101)
	sll3	r7, r5, #7
strlen_word_loop:
	ld	r1, @r6+	    ||	not	r4, r0
	sub	r0, r5		    ||	and	r4, r7
	and	r4, r0
	bnez	r4, strlen_last_bytes
	ld	r0, @r6+	    ||	not	r4, r1
	sub	r1, r5		    ||	and	r4, r7
	and	r4, r1		    ||	addi	r2, #4
	bnez	r4, strlen_last_bytes
	addi	r2, #4		    ||	bra.s	strlen_word_loop

	; NOTE: If a null char. exists, return 0.
	; if ((x - 0x01010101) & ~x & 0x80808080)
	;     return 0;
;
strlen_byte:
	ldb	r1, @r6		    ||	addi	r6, #1
	beqz	r1, strlen_exit
	addi	r2, #1		    ||	bra.s	strlen_byte
;
strlen_last_bytes:
	ldi	r0, #4		    ||	addi	r6, #-8
;
strlen_byte_loop:
	ldb	r1, @r6		    ||	addi	r6, #1
	addi	r0, #-1		    ||  cmpz	r1
	bc.s	strlen_exit	    ||  cmpz	r0
	addi	r2, #1		    ||	bnc.s	strlen_byte_loop
;
strlen_exit:
	mv	r0, r2		    ||	jmp	r14

#else /* not CONFIG_ISA_DUAL_ISSUE */

	.text
ENTRY(strlen)
	mv	r6, r0
	ldi	r2, #0
	and3	r0, r0, #3
	bnez	r0, strlen_byte
;
strlen_word:
	ld	r0, @r6+
;
	seth	r5, #high(0x01010101)
	or3	r5, r5, #low(0x01010101)
	sll3	r7, r5, #7
strlen_word_loop:
	ld	r1, @r6+
	not	r4, r0		; NOTE: If a null char. exists, return 0.
	sub	r0, r5		; if ((x - 0x01010101) & ~x & 0x80808080)
	and	r4, r7		;     return 0;
	and	r4, r0
	bnez	r4, strlen_last_bytes
	addi	r2, #4
;
	ld	r0, @r6+
	not	r4, r1		; NOTE: If a null char. exists, return 0.
	sub	r1, r5		; if ((x - 0x01010101) & ~x & 0x80808080)
	and	r4, r7		;     return 0;
	and	r4, r1
	bnez	r4, strlen_last_bytes
	addi	r2, #4
	bra	strlen_word_loop
;
strlen_byte:
	ldb	r1, @r6
	addi	r6, #1
	beqz	r1, strlen_exit
	addi	r2, #1
	bra	strlen_byte
;
strlen_last_bytes:
	ldi	r0, #4
	addi	r6, #-8
;
strlen_byte_loop:
	ldb	r1, @r6
	addi	r6, #1
	addi	r0, #-1
	beqz	r1, strlen_exit
	addi	r2, #1
	bnez	r0, strlen_byte_loop
;
strlen_exit:
	mv	r0, r2
	jmp	r14

#endif /* not CONFIG_ISA_DUAL_ISSUE */

	.end
