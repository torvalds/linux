/*---------------------------------------------------------------------------+
 |  status_w.h                                                               |
 |                                                                           |
 | Copyright (C) 1992,1993                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@vaxc.cc.monash.edu.au    |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#ifndef _STATUS_H_
#define _STATUS_H_

#include "fpu_emu.h"		/* for definition of PECULIAR_486 */

#ifdef __ASSEMBLY__
#define	Const__(x)	$##x
#else
#define	Const__(x)	x
#endif

#define SW_Backward    	Const__(0x8000)	/* backward compatibility */
#define SW_C3		Const__(0x4000)	/* condition bit 3 */
#define SW_Top		Const__(0x3800)	/* top of stack */
#define SW_Top_Shift 	Const__(11)	/* shift for top of stack bits */
#define SW_C2		Const__(0x0400)	/* condition bit 2 */
#define SW_C1		Const__(0x0200)	/* condition bit 1 */
#define SW_C0		Const__(0x0100)	/* condition bit 0 */
#define SW_Summary     	Const__(0x0080)	/* exception summary */
#define SW_Stack_Fault	Const__(0x0040)	/* stack fault */
#define SW_Precision   	Const__(0x0020)	/* loss of precision */
#define SW_Underflow   	Const__(0x0010)	/* underflow */
#define SW_Overflow    	Const__(0x0008)	/* overflow */
#define SW_Zero_Div    	Const__(0x0004)	/* divide by zero */
#define SW_Denorm_Op   	Const__(0x0002)	/* denormalized operand */
#define SW_Invalid     	Const__(0x0001)	/* invalid operation */

#define SW_Exc_Mask     Const__(0x27f)	/* Status word exception bit mask */

#ifndef __ASSEMBLY__

#define COMP_A_gt_B	1
#define COMP_A_eq_B	2
#define COMP_A_lt_B	3
#define COMP_No_Comp	4
#define COMP_Denormal   0x20
#define COMP_NaN	0x40
#define COMP_SNaN	0x80

#define status_word() \
  ((partial_status & ~SW_Top & 0xffff) | ((top << SW_Top_Shift) & SW_Top))
static inline void setcc(int cc)
{
	partial_status &= ~(SW_C0 | SW_C1 | SW_C2 | SW_C3);
	partial_status |= (cc) & (SW_C0 | SW_C1 | SW_C2 | SW_C3);
}

#ifdef PECULIAR_486
   /* Default, this conveys no information, but an 80486 does it. */
   /* Clear the SW_C1 bit, "other bits undefined". */
#  define clear_C1()  { partial_status &= ~SW_C1; }
# else
#  define clear_C1()
#endif /* PECULIAR_486 */

#endif /* __ASSEMBLY__ */

#endif /* _STATUS_H_ */
