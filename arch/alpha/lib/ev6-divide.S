/*
 * arch/alpha/lib/ev6-divide.S
 *
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Alpha division..
 */

/*
 * The alpha chip doesn't provide hardware division, so we have to do it
 * by hand.  The compiler expects the functions
 *
 *	__divqu: 64-bit unsigned long divide
 *	__remqu: 64-bit unsigned long remainder
 *	__divqs/__remqs: signed 64-bit
 *	__divlu/__remlu: unsigned 32-bit
 *	__divls/__remls: signed 32-bit
 *
 * These are not normal C functions: instead of the normal
 * calling sequence, these expect their arguments in registers
 * $24 and $25, and return the result in $27. Register $28 may
 * be clobbered (assembly temporary), anything else must be saved. 
 *
 * In short: painful.
 *
 * This is a rather simple bit-at-a-time algorithm: it's very good
 * at dividing random 64-bit numbers, but the more usual case where
 * the divisor is small is handled better by the DEC algorithm
 * using lookup tables. This uses much less memory, though, and is
 * nicer on the cache.. Besides, I don't know the copyright status
 * of the DEC code.
 */

/*
 * My temporaries:
 *	$0 - current bit
 *	$1 - shifted divisor
 *	$2 - modulus/quotient
 *
 *	$23 - return address
 *	$24 - dividend
 *	$25 - divisor
 *
 *	$27 - quotient/modulus
 *	$28 - compare status
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 * Try not to change the actual algorithm if possible for consistency.
 */

#include <asm/export.h>
#define halt .long 0

/*
 * Select function type and registers
 */
#define mask	$0
#define divisor	$1
#define compare $28
#define tmp1	$3
#define tmp2	$4

#ifdef DIV
#define DIV_ONLY(x,y...) x,##y
#define MOD_ONLY(x,y...)
#define func(x) __div##x
#define modulus $2
#define quotient $27
#define GETSIGN(x) xor $24,$25,x
#define STACK 48
#else
#define DIV_ONLY(x,y...)
#define MOD_ONLY(x,y...) x,##y
#define func(x) __rem##x
#define modulus $27
#define quotient $2
#define GETSIGN(x) bis $24,$24,x
#define STACK 32
#endif

/*
 * For 32-bit operations, we need to extend to 64-bit
 */
#ifdef INTSIZE
#define ufunction func(lu)
#define sfunction func(l)
#define LONGIFY(x) zapnot x,15,x
#define SLONGIFY(x) addl x,0,x
#else
#define ufunction func(qu)
#define sfunction func(q)
#define LONGIFY(x)
#define SLONGIFY(x)
#endif

.set noat
.align	4
.globl	ufunction
.ent	ufunction
ufunction:
	subq	$30,STACK,$30		# E :
	.frame	$30,STACK,$23
	.prologue 0

7:	stq	$1, 0($30)		# L :
	bis	$25,$25,divisor		# E :
	stq	$2, 8($30)		# L : L U L U

	bis	$24,$24,modulus		# E :
	stq	$0,16($30)		# L :
	bis	$31,$31,quotient	# E :
	LONGIFY(divisor)		# E : U L L U

	stq	tmp1,24($30)		# L :
	LONGIFY(modulus)		# E :
	bis	$31,1,mask		# E :
	DIV_ONLY(stq tmp2,32($30))	# L : L U U L

	beq	divisor, 9f			/* div by zero */
	/*
	 * In spite of the DIV_ONLY being either a non-instruction
	 * or an actual stq, the addition of the .align directive
	 * below ensures that label 1 is going to be nicely aligned
	 */

	.align	4
#ifdef INTSIZE
	/*
	 * shift divisor left, using 3-bit shifts for
	 * 32-bit divides as we can't overflow. Three-bit
	 * shifts will result in looping three times less
	 * here, but can result in two loops more later.
	 * Thus using a large shift isn't worth it (and
	 * s8add pairs better than a sll..)
	 */
1:	cmpult	divisor,modulus,compare	# E :
	s8addq	divisor,$31,divisor	# E :
	s8addq	mask,$31,mask		# E :
	bne	compare,1b		# U : U L U L
#else
1:	cmpult	divisor,modulus,compare	# E :
	nop				# E :
	nop				# E :
	blt     divisor, 2f		# U : U L U L

	addq	divisor,divisor,divisor	# E :
	addq	mask,mask,mask		# E :
	unop				# E :
	bne	compare,1b		# U : U L U L
#endif

	/* ok, start to go right again.. */
2:
	/*
	 * Keep things nicely bundled... use a nop instead of not
	 * having an instruction for DIV_ONLY
	 */
#ifdef DIV
	DIV_ONLY(addq quotient,mask,tmp2) # E :
#else
	nop				# E :
#endif
	srl	mask,1,mask		# U :
	cmpule	divisor,modulus,compare	# E :
	subq	modulus,divisor,tmp1	# E :

#ifdef DIV
	DIV_ONLY(cmovne compare,tmp2,quotient)	# E : Latency 2, extra map slot
	nop				# E : as part of the cmovne
	srl	divisor,1,divisor	# U :
	nop				# E : L U L U

	nop				# E :
	cmovne	compare,tmp1,modulus	# E : Latency 2, extra map slot
	nop				# E : as part of the cmovne
	bne	mask,2b			# U : U L U L
#else
	srl	divisor,1,divisor	# U :
	cmovne	compare,tmp1,modulus	# E : Latency 2, extra map slot
	nop				# E : as part of the cmovne
	bne	mask,2b			# U : U L L U
#endif

9:	ldq	$1, 0($30)		# L :
	ldq	$2, 8($30)		# L :
	nop				# E :
	nop				# E : U U L L

	ldq	$0,16($30)		# L :
	ldq	tmp1,24($30)		# L :
	nop				# E :
	nop				# E :

#ifdef DIV
	DIV_ONLY(ldq tmp2,32($30))	# L :
#else
	nop				# E :
#endif
	addq	$30,STACK,$30		# E :
	ret	$31,($23),1		# L0 : L U U L
	.end	ufunction
EXPORT_SYMBOL(ufunction)

/*
 * Uhh.. Ugly signed division. I'd rather not have it at all, but
 * it's needed in some circumstances. There are different ways to
 * handle this, really. This does:
 * 	-a / b = a / -b = -(a / b)
 *	-a % b = -(a % b)
 *	a % -b = a % b
 * which is probably not the best solution, but at least should
 * have the property that (x/y)*y + (x%y) = x.
 */
.align 4
.globl	sfunction
.ent	sfunction
sfunction:
	subq	$30,STACK,$30		# E :
	.frame	$30,STACK,$23
	.prologue 0
	bis	$24,$25,$28		# E :
	SLONGIFY($28)			# E :
	bge	$28,7b			# U :

	stq	$24,0($30)		# L :
	subq	$31,$24,$28		# E :
	stq	$25,8($30)		# L :
	nop				# E : U L U L

	cmovlt	$24,$28,$24	/* abs($24) */ # E : Latency 2, extra map slot
	nop				# E : as part of the cmov
	stq	$23,16($30)		# L :
	subq	$31,$25,$28		# E : U L U L

	stq	tmp1,24($30)		# L :
	cmovlt	$25,$28,$25	/* abs($25) */ # E : Latency 2, extra map slot
	nop				# E :
	bsr	$23,ufunction		# L0: L U L U

	ldq	$24,0($30)		# L :
	ldq	$25,8($30)		# L :
	GETSIGN($28)			# E :
	subq	$31,$27,tmp1		# E : U U L L

	SLONGIFY($28)			# E :
	ldq	$23,16($30)		# L :
	cmovlt	$28,tmp1,$27		# E : Latency 2, extra map slot
	nop				# E : U L L U : as part of the cmov

	ldq	tmp1,24($30)		# L :
	nop				# E : as part of the cmov
	addq	$30,STACK,$30		# E :
	ret	$31,($23),1		# L0 : L U U L
	.end	sfunction
EXPORT_SYMBOL(sfunction)
