/*
 * arch/alpha/lib/copy_user.S
 *
 * Copy to/from user space, handling exceptions as we go..  This
 * isn't exactly pretty.
 *
 * This is essentially the same as "memcpy()", but with a few twists.
 * Notably, we have to make sure that $0 is always up-to-date and
 * contains the right "bytes left to copy" value (and that it is updated
 * only _after_ a successful copy). There is also some rather minor
 * exception setup stuff..
 *
 * NOTE! This is not directly C-callable, because the calling semantics are
 * different:
 *
 * Inputs:
 *	length in $0
 *	destination address in $6
 *	source address in $7
 *	return address in $28
 *
 * Outputs:
 *	bytes left to copy in $0
 *
 * Clobbers:
 *	$1,$2,$3,$4,$5,$6,$7
 */

#include <asm/export.h>

/* Allow an exception for an insn; exit if we get one.  */
#define EXI(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exitin-99b($31);	\
	.previous

#define EXO(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exitout-99b($31);	\
	.previous

	.set noat
	.align 4
	.globl __copy_user
	.ent __copy_user
__copy_user:
	.prologue 0
	and $6,7,$3
	beq $0,$35
	beq $3,$36
	subq $3,8,$3
	.align 4
$37:
	EXI( ldq_u $1,0($7) )
	EXO( ldq_u $2,0($6) )
	extbl $1,$7,$1
	mskbl $2,$6,$2
	insbl $1,$6,$1
	addq $3,1,$3
	bis $1,$2,$1
	EXO( stq_u $1,0($6) )
	subq $0,1,$0
	addq $6,1,$6
	addq $7,1,$7
	beq $0,$41
	bne $3,$37
$36:
	and $7,7,$1
	bic $0,7,$4
	beq $1,$43
	beq $4,$48
	EXI( ldq_u $3,0($7) )
	.align 4
$50:
	EXI( ldq_u $2,8($7) )
	subq $4,8,$4
	extql $3,$7,$3
	extqh $2,$7,$1
	bis $3,$1,$1
	EXO( stq $1,0($6) )
	addq $7,8,$7
	subq $0,8,$0
	addq $6,8,$6
	bis $2,$2,$3
	bne $4,$50
$48:
	beq $0,$41
	.align 4
$57:
	EXI( ldq_u $1,0($7) )
	EXO( ldq_u $2,0($6) )
	extbl $1,$7,$1
	mskbl $2,$6,$2
	insbl $1,$6,$1
	bis $1,$2,$1
	EXO( stq_u $1,0($6) )
	subq $0,1,$0
	addq $6,1,$6
	addq $7,1,$7
	bne $0,$57
	br $31,$41
	.align 4
$43:
	beq $4,$65
	.align 4
$66:
	EXI( ldq $1,0($7) )
	subq $4,8,$4
	EXO( stq $1,0($6) )
	addq $7,8,$7
	subq $0,8,$0
	addq $6,8,$6
	bne $4,$66
$65:
	beq $0,$41
	EXI( ldq $2,0($7) )
	EXO( ldq $1,0($6) )
	mskql $2,$0,$2
	mskqh $1,$0,$1
	bis $2,$1,$2
	EXO( stq $2,0($6) )
	bis $31,$31,$0
$41:
$35:
$exitout:
	ret $31,($28),1

$exitin:
	/* A stupid byte-by-byte zeroing of the rest of the output
	   buffer.  This cures security holes by never leaving 
	   random kernel data around to be copied elsewhere.  */

	mov $0,$1
$101:
	EXO ( ldq_u $2,0($6) )
	subq $1,1,$1
	mskbl $2,$6,$2
	EXO ( stq_u $2,0($6) )
	addq $6,1,$6
	bgt $1,$101
	ret $31,($28),1

	.end __copy_user
EXPORT_SYMBOL(__copy_user)
