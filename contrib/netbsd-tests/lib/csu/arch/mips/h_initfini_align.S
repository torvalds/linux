/*	$NetBSD: h_initfini_align.S,v 1.1 2013/12/11 17:31:56 matt Exp $	*/

#include <machine/asm.h>

RCSID("$NetBSD: h_initfini_align.S,v 1.1 2013/12/11 17:31:56 matt Exp $")

/*
 * LINTSTUB: bool check_stack_alignment(void);
 */

LEAF_NOPROFILE(check_stack_alignment)
#ifdef __mips_o32
	andi	v1,sp,3
#else
	andi	v1,sp,7
#endif
	sltiu	v0,v1,1
	jr	ra
END(check_stack_alignment)
