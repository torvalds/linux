/*---------------------------------------------------------------------------+
 |  fpu_emu.h                                                                |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997                                         |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@suburbia.net             |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#ifndef _FPU_EMU_H_
#define _FPU_EMU_H_

/*
 * Define PECULIAR_486 to get a closer approximation to 80486 behaviour,
 * rather than behaviour which appears to be cleaner.
 * This is a matter of opinion: for all I know, the 80486 may simply
 * be complying with the IEEE spec. Maybe one day I'll get to see the
 * spec...
 */
#define PECULIAR_486

#ifdef __ASSEMBLY__
#include "fpu_asm.h"
#define	Const(x)	$##x
#else
#define	Const(x)	x
#endif

#define EXP_BIAS	Const(0)
#define EXP_OVER	Const(0x4000)	/* smallest invalid large exponent */
#define	EXP_UNDER	Const(-0x3fff)	/* largest invalid small exponent */
#define EXP_WAY_UNDER   Const(-0x6000)	/* Below the smallest denormal, but
					   still a 16 bit nr. */
#define EXP_Infinity    EXP_OVER
#define EXP_NaN         EXP_OVER

#define EXTENDED_Ebias Const(0x3fff)
#define EXTENDED_Emin (-0x3ffe)	/* smallest valid exponent */

#define SIGN_POS	Const(0)
#define SIGN_NEG	Const(0x80)

#define SIGN_Positive	Const(0)
#define SIGN_Negative	Const(0x8000)

/* Keep the order TAG_Valid, TAG_Zero, TW_Denormal */
/* The following fold to 2 (Special) in the Tag Word */
#define TW_Denormal     Const(4)	/* De-normal */
#define TW_Infinity	Const(5)	/* + or - infinity */
#define	TW_NaN		Const(6)	/* Not a Number */
#define	TW_Unsupported	Const(7)	/* Not supported by an 80486 */

#define TAG_Valid	Const(0)	/* valid */
#define TAG_Zero	Const(1)	/* zero */
#define TAG_Special	Const(2)	/* De-normal, + or - infinity,
					   or Not a Number */
#define TAG_Empty	Const(3)	/* empty */
#define TAG_Error	Const(0x80)	/* probably need to abort */

#define LOADED_DATA	Const(10101)	/* Special st() number to identify
					   loaded data (not on stack). */

/* A few flags (must be >= 0x10). */
#define REV             0x10
#define DEST_RM         0x20
#define LOADED          0x40

#define FPU_Exception   Const(0x80000000)	/* Added to tag returns. */

#ifndef __ASSEMBLY__

#include "fpu_system.h"

#include <uapi/asm/sigcontext.h>	/* for struct _fpstate */
#include <asm/math_emu.h>
#include <linux/linkage.h>

/*
#define RE_ENTRANT_CHECKING
 */

#ifdef RE_ENTRANT_CHECKING
extern u_char emulating;
#  define RE_ENTRANT_CHECK_OFF emulating = 0
#  define RE_ENTRANT_CHECK_ON emulating = 1
#else
#  define RE_ENTRANT_CHECK_OFF
#  define RE_ENTRANT_CHECK_ON
#endif /* RE_ENTRANT_CHECKING */

#define FWAIT_OPCODE 0x9b
#define OP_SIZE_PREFIX 0x66
#define ADDR_SIZE_PREFIX 0x67
#define PREFIX_CS 0x2e
#define PREFIX_DS 0x3e
#define PREFIX_ES 0x26
#define PREFIX_SS 0x36
#define PREFIX_FS 0x64
#define PREFIX_GS 0x65
#define PREFIX_REPE 0xf3
#define PREFIX_REPNE 0xf2
#define PREFIX_LOCK 0xf0
#define PREFIX_CS_ 1
#define PREFIX_DS_ 2
#define PREFIX_ES_ 3
#define PREFIX_FS_ 4
#define PREFIX_GS_ 5
#define PREFIX_SS_ 6
#define PREFIX_DEFAULT 7

struct address {
	unsigned int offset;
	unsigned int selector:16;
	unsigned int opcode:11;
	unsigned int empty:5;
};
struct fpu__reg {
	unsigned sigl;
	unsigned sigh;
	short exp;
};

typedef void (*FUNC) (void);
typedef struct fpu__reg FPU_REG;
typedef void (*FUNC_ST0) (FPU_REG *st0_ptr, u_char st0_tag);
typedef struct {
	u_char address_size, operand_size, segment;
} overrides;
/* This structure is 32 bits: */
typedef struct {
	overrides override;
	u_char default_mode;
} fpu_addr_modes;
/* PROTECTED has a restricted meaning in the emulator; it is used
   to signal that the emulator needs to do special things to ensure
   that protection is respected in a segmented model. */
#define PROTECTED 4
#define SIXTEEN   1		/* We rely upon this being 1 (true) */
#define VM86      SIXTEEN
#define PM16      (SIXTEEN | PROTECTED)
#define SEG32     PROTECTED
extern u_char const data_sizes_16[32];

#define register_base ((u_char *) registers )
#define fpu_register(x)  ( * ((FPU_REG *)( register_base + 10 * (x & 7) )) )
#define	st(x)      ( * ((FPU_REG *)( register_base + 10 * ((top+x) & 7) )) )

#define	STACK_OVERFLOW	(FPU_stackoverflow(&st_new_ptr))
#define	NOT_EMPTY(i)	(!FPU_empty_i(i))

#define	NOT_EMPTY_ST0	(st0_tag ^ TAG_Empty)

#define poppop() { FPU_pop(); FPU_pop(); }

/* push() does not affect the tags */
#define push()	{ top--; }

#define signbyte(a) (((u_char *)(a))[9])
#define getsign(a) (signbyte(a) & 0x80)
#define setsign(a,b) { if ((b) != 0) signbyte(a) |= 0x80; else signbyte(a) &= 0x7f; }
#define copysign(a,b) { if (getsign(a)) signbyte(b) |= 0x80; \
                        else signbyte(b) &= 0x7f; }
#define changesign(a) { signbyte(a) ^= 0x80; }
#define setpositive(a) { signbyte(a) &= 0x7f; }
#define setnegative(a) { signbyte(a) |= 0x80; }
#define signpositive(a) ( (signbyte(a) & 0x80) == 0 )
#define signnegative(a) (signbyte(a) & 0x80)

static inline void reg_copy(FPU_REG const *x, FPU_REG *y)
{
	*(short *)&(y->exp) = *(const short *)&(x->exp);
	*(long long *)&(y->sigl) = *(const long long *)&(x->sigl);
}

#define exponent(x)  (((*(short *)&((x)->exp)) & 0x7fff) - EXTENDED_Ebias)
#define setexponentpos(x,y) { (*(short *)&((x)->exp)) = \
  ((y) + EXTENDED_Ebias) & 0x7fff; }
#define exponent16(x)         (*(short *)&((x)->exp))
#define setexponent16(x,y)  { (*(short *)&((x)->exp)) = (y); }
#define addexponent(x,y)    { (*(short *)&((x)->exp)) += (y); }
#define stdexp(x)           { (*(short *)&((x)->exp)) += EXTENDED_Ebias; }

#define isdenormal(ptr)   (exponent(ptr) == EXP_BIAS+EXP_UNDER)

#define significand(x) ( ((unsigned long long *)&((x)->sigl))[0] )

/*----- Prototypes for functions written in assembler -----*/
/* extern void reg_move(FPU_REG *a, FPU_REG *b); */

asmlinkage int FPU_normalize(FPU_REG *x);
asmlinkage int FPU_normalize_nuo(FPU_REG *x);
asmlinkage int FPU_u_sub(FPU_REG const *arg1, FPU_REG const *arg2,
			 FPU_REG * answ, unsigned int control_w, u_char sign,
			 int expa, int expb);
asmlinkage int FPU_u_mul(FPU_REG const *arg1, FPU_REG const *arg2,
			 FPU_REG * answ, unsigned int control_w, u_char sign,
			 int expon);
asmlinkage int FPU_u_div(FPU_REG const *arg1, FPU_REG const *arg2,
			 FPU_REG * answ, unsigned int control_w, u_char sign);
asmlinkage int FPU_u_add(FPU_REG const *arg1, FPU_REG const *arg2,
			 FPU_REG * answ, unsigned int control_w, u_char sign,
			 int expa, int expb);
asmlinkage int wm_sqrt(FPU_REG *n, int dummy1, int dummy2,
		       unsigned int control_w, u_char sign);
asmlinkage unsigned FPU_shrx(void *l, unsigned x);
asmlinkage unsigned FPU_shrxs(void *v, unsigned x);
asmlinkage unsigned long FPU_div_small(unsigned long long *x, unsigned long y);
asmlinkage int FPU_round(FPU_REG *arg, unsigned int extent, int dummy,
			 unsigned int control_w, u_char sign);

#ifndef MAKING_PROTO
#include "fpu_proto.h"
#endif

#endif /* __ASSEMBLY__ */

#endif /* _FPU_EMU_H_ */
