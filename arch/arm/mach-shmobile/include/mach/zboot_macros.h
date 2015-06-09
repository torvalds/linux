#ifndef __ZBOOT_MACRO_H
#define __ZBOOT_MACRO_H

/* The LIST command is used to include comments in the script */
.macro	LIST comment
.endm

/* The ED command is used to write a 32-bit word */
.macro ED, addr, data
	LDR	r0, 1f
	LDR	r1, 2f
	STR	r1, [r0]
	B	3f
1 :	.long	\addr
2 :	.long	\data
3 :
.endm

/* The EW command is used to write a 16-bit word */
.macro EW, addr, data
	LDR	r0, 1f
	LDR	r1, 2f
	STRH	r1, [r0]
	B	3f
1 :	.long	\addr
2 :	.long	\data
3 :
.endm

/* The EB command is used to write an 8-bit word */
.macro EB, addr, data
	LDR	r0, 1f
	LDR	r1, 2f
	STRB	r1, [r0]
	B	3f
1 :	.long	\addr
2 :	.long	\data
3 :
.endm

/* The WAIT command is used to delay the execution */
.macro  WAIT, time, reg
	LDR	r1, 1f
	LDR	r0, 2f
	STR	r0, [r1]
10 :
	LDR	r0, [r1]
	CMP	r0, #0x00000000
	BNE	10b
	NOP
	B	3f
1 :	.long	\reg
2 :	.long	\time * 100
3 :
.endm

/* The DD command is used to read a 32-bit word */
.macro  DD, start, end
	LDR	r1, 1f
	B	2f
1 :	.long	\start
2 :
.endm

/* loop until a given value has been read (with mask) */
.macro WAIT_MASK, addr, data, cmp
	LDR	r0, 2f
	LDR	r1, 3f
	LDR	r2, 4f
1:
	LDR	r3, [r0, #0]
	AND	r3, r1, r3
	CMP	r2, r3
	BNE	1b
	B	5f
2:	.long	\addr
3:	.long	\data
4:	.long	\cmp
5:
.endm

/* read 32-bit value from addr, "or" an immediate and write back */
.macro ED_OR, addr, data
	LDR r4, 1f
	LDR r5, 2f
	LDR r6, [r4]
	ORR r5, r6, r5
	STR r5, [r4]
	B	3f
1:	.long	\addr
2:	.long	\data
3:
.endm

/* read 32-bit value from addr, "and" an immediate and write back */
.macro ED_AND, addr, data
	LDR r4, 1f
	LDR r5, 2f
	LDR r6, [r4]
	AND r5, r6, r5
	STR r5, [r4]
	B	3f
1:	.long \addr
2:	.long \data
3:
.endm

#endif /* __ZBOOT_MACRO_H */
