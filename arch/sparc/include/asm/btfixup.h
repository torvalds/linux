/*
 *  asm/btfixup.h:    Macros for boot time linking.
 *
 *  Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#ifndef _SPARC_BTFIXUP_H
#define _SPARC_BTFIXUP_H

#include <linux/init.h>

#ifndef __ASSEMBLY__

#ifdef MODULE
extern unsigned int ___illegal_use_of_BTFIXUP_SIMM13_in_module(void);
extern unsigned int ___illegal_use_of_BTFIXUP_SETHI_in_module(void);
extern unsigned int ___illegal_use_of_BTFIXUP_HALF_in_module(void);
extern unsigned int ___illegal_use_of_BTFIXUP_INT_in_module(void);

#define BTFIXUP_SIMM13(__name) ___illegal_use_of_BTFIXUP_SIMM13_in_module()
#define BTFIXUP_HALF(__name) ___illegal_use_of_BTFIXUP_HALF_in_module()
#define BTFIXUP_SETHI(__name) ___illegal_use_of_BTFIXUP_SETHI_in_module()
#define BTFIXUP_INT(__name) ___illegal_use_of_BTFIXUP_INT_in_module()
#define BTFIXUP_BLACKBOX(__name) ___illegal_use_of_BTFIXUP_BLACKBOX_in_module

#else

#define BTFIXUP_SIMM13(__name) ___sf_##__name()
#define BTFIXUP_HALF(__name) ___af_##__name()
#define BTFIXUP_SETHI(__name) ___hf_##__name()
#define BTFIXUP_INT(__name) ((unsigned int)&___i_##__name)
/* This must be written in assembly and present in a sethi */
#define BTFIXUP_BLACKBOX(__name) ___b_##__name
#endif /* MODULE */

/* Fixup call xx */

#define BTFIXUPDEF_CALL(__type, __name, __args...) 					\
	extern __type ___f_##__name(__args);						\
	extern unsigned ___fs_##__name[3];
#define BTFIXUPDEF_CALL_CONST(__type, __name, __args...) 				\
	extern __type ___f_##__name(__args) __attribute_const__;			\
	extern unsigned ___fs_##__name[3];
#define BTFIXUP_CALL(__name) ___f_##__name

#define BTFIXUPDEF_BLACKBOX(__name)							\
	extern unsigned ___bs_##__name[2];

/* Put bottom 13bits into some register variable */

#define BTFIXUPDEF_SIMM13(__name)							\
	static inline unsigned int ___sf_##__name(void) __attribute_const__;		\
	extern unsigned ___ss_##__name[2];						\
	static inline unsigned int ___sf_##__name(void) {				\
		unsigned int ret;							\
		__asm__ ("or %%g0, ___s_" #__name ", %0" : "=r"(ret));			\
		return ret;								\
	}
#define BTFIXUPDEF_SIMM13_INIT(__name,__val)						\
	static inline unsigned int ___sf_##__name(void) __attribute_const__;		\
	extern unsigned ___ss_##__name[2];						\
	static inline unsigned int ___sf_##__name(void) {				\
		unsigned int ret;							\
		__asm__ ("or %%g0, ___s_" #__name "__btset_" #__val ", %0" : "=r"(ret));\
		return ret;								\
	}

/* Put either bottom 13 bits, or upper 22 bits into some register variable
 * (depending on the value, this will lead into sethi FIX, reg; or
 * mov FIX, reg; )
 */

#define BTFIXUPDEF_HALF(__name)								\
	static inline unsigned int ___af_##__name(void) __attribute_const__;		\
	extern unsigned ___as_##__name[2];						\
	static inline unsigned int ___af_##__name(void) {				\
		unsigned int ret;							\
		__asm__ ("or %%g0, ___a_" #__name ", %0" : "=r"(ret));			\
		return ret;								\
	}
#define BTFIXUPDEF_HALF_INIT(__name,__val)						\
	static inline unsigned int ___af_##__name(void) __attribute_const__;		\
	extern unsigned ___as_##__name[2];						\
	static inline unsigned int ___af_##__name(void) {				\
		unsigned int ret;							\
		__asm__ ("or %%g0, ___a_" #__name "__btset_" #__val ", %0" : "=r"(ret));\
		return ret;								\
	}

/* Put upper 22 bits into some register variable */

#define BTFIXUPDEF_SETHI(__name)							\
	static inline unsigned int ___hf_##__name(void) __attribute_const__;		\
	extern unsigned ___hs_##__name[2];						\
	static inline unsigned int ___hf_##__name(void) {				\
		unsigned int ret;							\
		__asm__ ("sethi %%hi(___h_" #__name "), %0" : "=r"(ret));		\
		return ret;								\
	}
#define BTFIXUPDEF_SETHI_INIT(__name,__val)						\
	static inline unsigned int ___hf_##__name(void) __attribute_const__;		\
	extern unsigned ___hs_##__name[2];						\
	static inline unsigned int ___hf_##__name(void) {				\
		unsigned int ret;							\
		__asm__ ("sethi %%hi(___h_" #__name "__btset_" #__val "), %0" : 	\
			 "=r"(ret));							\
		return ret;								\
	}

/* Put a full 32bit integer into some register variable */

#define BTFIXUPDEF_INT(__name)								\
	extern unsigned char ___i_##__name;						\
	extern unsigned ___is_##__name[2];

#define BTFIXUPCALL_NORM	0x00000000			/* Always call */
#define BTFIXUPCALL_NOP		0x01000000			/* Possibly optimize to nop */
#define BTFIXUPCALL_RETINT(i)	(0x90102000|((i) & 0x1fff))	/* Possibly optimize to mov i, %o0 */
#define BTFIXUPCALL_ORINT(i)	(0x90122000|((i) & 0x1fff))	/* Possibly optimize to or %o0, i, %o0 */
#define BTFIXUPCALL_RETO0	0x01000000			/* Return first parameter, actually a nop */
#define BTFIXUPCALL_ANDNINT(i)	(0x902a2000|((i) & 0x1fff))	/* Possibly optimize to andn %o0, i, %o0 */
#define BTFIXUPCALL_SWAPO0O1	0xd27a0000			/* Possibly optimize to swap [%o0],%o1 */
#define BTFIXUPCALL_SWAPO0G0	0xc07a0000			/* Possibly optimize to swap [%o0],%g0 */
#define BTFIXUPCALL_SWAPG1G2	0xc4784000			/* Possibly optimize to swap [%g1],%g2 */
#define BTFIXUPCALL_STG0O0	0xc0220000			/* Possibly optimize to st %g0,[%o0] */
#define BTFIXUPCALL_STO1O0	0xd2220000			/* Possibly optimize to st %o1,[%o0] */

#define BTFIXUPSET_CALL(__name, __addr, __insn)						\
	do {										\
		___fs_##__name[0] |= 1;							\
		___fs_##__name[1] = (unsigned long)__addr;				\
		___fs_##__name[2] = __insn;						\
	} while (0)
	
#define BTFIXUPSET_BLACKBOX(__name, __func)						\
	do {										\
		___bs_##__name[0] |= 1;							\
		___bs_##__name[1] = (unsigned long)__func;				\
	} while (0)
	
#define BTFIXUPCOPY_CALL(__name, __from)						\
	do {										\
		___fs_##__name[0] |= 1;							\
		___fs_##__name[1] = ___fs_##__from[1];					\
		___fs_##__name[2] = ___fs_##__from[2];					\
	} while (0)
		
#define BTFIXUPSET_SIMM13(__name, __val)						\
	do {										\
		___ss_##__name[0] |= 1;							\
		___ss_##__name[1] = (unsigned)__val;					\
	} while (0)
	
#define BTFIXUPCOPY_SIMM13(__name, __from)						\
	do {										\
		___ss_##__name[0] |= 1;							\
		___ss_##__name[1] = ___ss_##__from[1];					\
	} while (0)
		
#define BTFIXUPSET_HALF(__name, __val)							\
	do {										\
		___as_##__name[0] |= 1;							\
		___as_##__name[1] = (unsigned)__val;					\
	} while (0)
	
#define BTFIXUPCOPY_HALF(__name, __from)						\
	do {										\
		___as_##__name[0] |= 1;							\
		___as_##__name[1] = ___as_##__from[1];					\
	} while (0)
		
#define BTFIXUPSET_SETHI(__name, __val)							\
	do {										\
		___hs_##__name[0] |= 1;							\
		___hs_##__name[1] = (unsigned)__val;					\
	} while (0)
	
#define BTFIXUPCOPY_SETHI(__name, __from)						\
	do {										\
		___hs_##__name[0] |= 1;							\
		___hs_##__name[1] = ___hs_##__from[1];					\
	} while (0)
		
#define BTFIXUPSET_INT(__name, __val)							\
	do {										\
		___is_##__name[0] |= 1;							\
		___is_##__name[1] = (unsigned)__val;					\
	} while (0)
	
#define BTFIXUPCOPY_INT(__name, __from)							\
	do {										\
		___is_##__name[0] |= 1;							\
		___is_##__name[1] = ___is_##__from[1];					\
	} while (0)
	
#define BTFIXUPVAL_CALL(__name)								\
	((unsigned long)___fs_##__name[1])
	
extern void btfixup(void);

#else /* __ASSEMBLY__ */

#define BTFIXUP_SETHI(__name)			%hi(___h_ ## __name)
#define BTFIXUP_SETHI_INIT(__name,__val)	%hi(___h_ ## __name ## __btset_ ## __val)

#endif /* __ASSEMBLY__ */
	
#endif /* !(_SPARC_BTFIXUP_H) */
