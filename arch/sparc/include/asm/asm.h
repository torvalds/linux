#ifndef _SPARC_ASM_H
#define _SPARC_ASM_H

/* Macros to assist the sharing of assembler code between 32-bit and
 * 64-bit sparc.
 */

#ifdef CONFIG_SPARC64
#define BRANCH32(TYPE, PREDICT, DEST) \
	TYPE,PREDICT	%icc, DEST
#define BRANCH32_ANNUL(TYPE, PREDICT, DEST) \
	TYPE,a,PREDICT	%icc, DEST
#define BRANCH_REG_ZERO(PREDICT, REG, DEST) \
	brz,PREDICT	REG, DEST
#define BRANCH_REG_ZERO_ANNUL(PREDICT, REG, DEST) \
	brz,a,PREDICT	REG, DEST
#define BRANCH_REG_NOT_ZERO(PREDICT, REG, DEST) \
	brnz,PREDICT	REG, DEST
#define BRANCH_REG_NOT_ZERO_ANNUL(PREDICT, REG, DEST) \
	brnz,a,PREDICT	REG, DEST
#else
#define BRANCH32(TYPE, PREDICT, DEST) \
	TYPE		DEST
#define BRANCH32_ANNUL(TYPE, PREDICT, DEST) \
	TYPE,a		DEST
#define BRANCH_REG_ZERO(PREDICT, REG, DEST) \
	cmp		REG, 0; \
	be		DEST
#define BRANCH_REG_ZERO_ANNUL(PREDICT, REG, DEST) \
	cmp		REG, 0; \
	be,a		DEST
#define BRANCH_REG_NOT_ZERO(PREDICT, REG, DEST) \
	cmp		REG, 0; \
	bne		DEST
#define BRANCH_REG_NOT_ZERO_ANNUL(PREDICT, REG, DEST) \
	cmp		REG, 0; \
	bne,a		DEST
#endif

#endif /* _SPARC_ASM_H */
