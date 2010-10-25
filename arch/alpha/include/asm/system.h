#ifndef __ALPHA_SYSTEM_H
#define __ALPHA_SYSTEM_H

#include <asm/pal.h>
#include <asm/page.h>
#include <asm/barrier.h>

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * We leave one page for the initial stack page, and one page for
 * the initial process structure. Also, the console eats 3 MB for
 * the initial bootloader (one of which we can reclaim later).
 */
#define BOOT_PCB	0x20000000
#define BOOT_ADDR	0x20000000
/* Remove when official MILO sources have ELF support: */
#define BOOT_SIZE	(16*1024)

#ifdef CONFIG_ALPHA_LEGACY_START_ADDRESS
#define KERNEL_START_PHYS	0x300000 /* Old bootloaders hardcoded this.  */
#else
#define KERNEL_START_PHYS	0x1000000 /* required: Wildfire/Titan/Marvel */
#endif

#define KERNEL_START	(PAGE_OFFSET+KERNEL_START_PHYS)
#define SWAPPER_PGD	KERNEL_START
#define INIT_STACK	(PAGE_OFFSET+KERNEL_START_PHYS+0x02000)
#define EMPTY_PGT	(PAGE_OFFSET+KERNEL_START_PHYS+0x04000)
#define EMPTY_PGE	(PAGE_OFFSET+KERNEL_START_PHYS+0x08000)
#define ZERO_PGE	(PAGE_OFFSET+KERNEL_START_PHYS+0x0A000)

#define START_ADDR	(PAGE_OFFSET+KERNEL_START_PHYS+0x10000)

/*
 * This is setup by the secondary bootstrap loader.  Because
 * the zero page is zeroed out as soon as the vm system is
 * initialized, we need to copy things out into a more permanent
 * place.
 */
#define PARAM			ZERO_PGE
#define COMMAND_LINE		((char*)(PARAM + 0x0000))
#define INITRD_START		(*(unsigned long *) (PARAM+0x100))
#define INITRD_SIZE		(*(unsigned long *) (PARAM+0x108))

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#define AT_VECTOR_SIZE_ARCH 4 /* entries in ARCH_DLINFO */

/*
 * This is the logout header that should be common to all platforms
 * (assuming they are running OSF/1 PALcode, I guess).
 */
struct el_common {
	unsigned int	size;		/* size in bytes of logout area */
	unsigned int	sbz1	: 30;	/* should be zero */
	unsigned int	err2	:  1;	/* second error */
	unsigned int	retry	:  1;	/* retry flag */
	unsigned int	proc_offset;	/* processor-specific offset */
	unsigned int	sys_offset;	/* system-specific offset */
	unsigned int	code;		/* machine check code */
	unsigned int	frame_rev;	/* frame revision */
};

/* Machine Check Frame for uncorrectable errors (Large format)
 *      --- This is used to log uncorrectable errors such as
 *          double bit ECC errors.
 *      --- These errors are detected by both processor and systems.
 */
struct el_common_EV5_uncorrectable_mcheck {
        unsigned long   shadow[8];        /* Shadow reg. 8-14, 25           */
        unsigned long   paltemp[24];      /* PAL TEMP REGS.                 */
        unsigned long   exc_addr;         /* Address of excepting instruction*/
        unsigned long   exc_sum;          /* Summary of arithmetic traps.   */
        unsigned long   exc_mask;         /* Exception mask (from exc_sum). */
        unsigned long   pal_base;         /* Base address for PALcode.      */
        unsigned long   isr;              /* Interrupt Status Reg.          */
        unsigned long   icsr;             /* CURRENT SETUP OF EV5 IBOX      */
        unsigned long   ic_perr_stat;     /* I-CACHE Reg. <11> set Data parity
                                                         <12> set TAG parity*/
        unsigned long   dc_perr_stat;     /* D-CACHE error Reg. Bits set to 1:
                                                     <2> Data error in bank 0
                                                     <3> Data error in bank 1
                                                     <4> Tag error in bank 0
                                                     <5> Tag error in bank 1 */
        unsigned long   va;               /* Effective VA of fault or miss. */
        unsigned long   mm_stat;          /* Holds the reason for D-stream 
                                             fault or D-cache parity errors */
        unsigned long   sc_addr;          /* Address that was being accessed
                                             when EV5 detected Secondary cache
                                             failure.                 */
        unsigned long   sc_stat;          /* Helps determine if the error was
                                             TAG/Data parity(Secondary Cache)*/
        unsigned long   bc_tag_addr;      /* Contents of EV5 BC_TAG_ADDR    */
        unsigned long   ei_addr;          /* Physical address of any transfer
                                             that is logged in EV5 EI_STAT */
        unsigned long   fill_syndrome;    /* For correcting ECC errors.     */
        unsigned long   ei_stat;          /* Helps identify reason of any 
                                             processor uncorrectable error
                                             at its external interface.     */
        unsigned long   ld_lock;          /* Contents of EV5 LD_LOCK register*/
};

struct el_common_EV6_mcheck {
	unsigned int FrameSize;		/* Bytes, including this field */
	unsigned int FrameFlags;	/* <31> = Retry, <30> = Second Error */
	unsigned int CpuOffset;		/* Offset to CPU-specific info */
	unsigned int SystemOffset;	/* Offset to system-specific info */
	unsigned int MCHK_Code;
	unsigned int MCHK_Frame_Rev;
	unsigned long I_STAT;		/* EV6 Internal Processor Registers */
	unsigned long DC_STAT;		/* (See the 21264 Spec) */
	unsigned long C_ADDR;
	unsigned long DC1_SYNDROME;
	unsigned long DC0_SYNDROME;
	unsigned long C_STAT;
	unsigned long C_STS;
	unsigned long MM_STAT;
	unsigned long EXC_ADDR;
	unsigned long IER_CM;
	unsigned long ISUM;
	unsigned long RESERVED0;
	unsigned long PAL_BASE;
	unsigned long I_CTL;
	unsigned long PCTX;
};

extern void halt(void) __attribute__((noreturn));
#define __halt() __asm__ __volatile__ ("call_pal %0 #halt" : : "i" (PAL_halt))

#define switch_to(P,N,L)						 \
  do {									 \
    (L) = alpha_switch_to(virt_to_phys(&task_thread_info(N)->pcb), (P)); \
    check_mmu_context();						 \
  } while (0)

struct task_struct;
extern struct task_struct *alpha_switch_to(unsigned long, struct task_struct*);

#define imb() \
__asm__ __volatile__ ("call_pal %0 #imb" : : "i" (PAL_imb) : "memory")

#define draina() \
__asm__ __volatile__ ("call_pal %0 #draina" : : "i" (PAL_draina) : "memory")

enum implver_enum {
	IMPLVER_EV4,
	IMPLVER_EV5,
	IMPLVER_EV6
};

#ifdef CONFIG_ALPHA_GENERIC
#define implver()				\
({ unsigned long __implver;			\
   __asm__ ("implver %0" : "=r"(__implver));	\
   (enum implver_enum) __implver; })
#else
/* Try to eliminate some dead code.  */
#ifdef CONFIG_ALPHA_EV4
#define implver() IMPLVER_EV4
#endif
#ifdef CONFIG_ALPHA_EV5
#define implver() IMPLVER_EV5
#endif
#if defined(CONFIG_ALPHA_EV6)
#define implver() IMPLVER_EV6
#endif
#endif

enum amask_enum {
	AMASK_BWX = (1UL << 0),
	AMASK_FIX = (1UL << 1),
	AMASK_CIX = (1UL << 2),
	AMASK_MAX = (1UL << 8),
	AMASK_PRECISE_TRAP = (1UL << 9),
};

#define amask(mask)						\
({ unsigned long __amask, __input = (mask);			\
   __asm__ ("amask %1,%0" : "=r"(__amask) : "rI"(__input));	\
   __amask; })

#define __CALL_PAL_R0(NAME, TYPE)				\
extern inline TYPE NAME(void)					\
{								\
	register TYPE __r0 __asm__("$0");			\
	__asm__ __volatile__(					\
		"call_pal %1 # " #NAME				\
		:"=r" (__r0)					\
		:"i" (PAL_ ## NAME)				\
		:"$1", "$16", "$22", "$23", "$24", "$25");	\
	return __r0;						\
}

#define __CALL_PAL_W1(NAME, TYPE0)				\
extern inline void NAME(TYPE0 arg0)				\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %1 # "#NAME				\
		: "=r"(__r16)					\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_W2(NAME, TYPE0, TYPE1)			\
extern inline void NAME(TYPE0 arg0, TYPE1 arg1)			\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r17)			\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_RW1(NAME, RTYPE, TYPE0)			\
extern inline RTYPE NAME(TYPE0 arg0)				\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r0)			\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

#define __CALL_PAL_RW2(NAME, RTYPE, TYPE0, TYPE1)		\
extern inline RTYPE NAME(TYPE0 arg0, TYPE1 arg1)		\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %3 # "#NAME				\
		: "=r"(__r16), "=r"(__r17), "=r"(__r0)		\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

__CALL_PAL_W1(cflush, unsigned long);
__CALL_PAL_R0(rdmces, unsigned long);
__CALL_PAL_R0(rdps, unsigned long);
__CALL_PAL_R0(rdusp, unsigned long);
__CALL_PAL_RW1(swpipl, unsigned long, unsigned long);
__CALL_PAL_R0(whami, unsigned long);
__CALL_PAL_W2(wrent, void*, unsigned long);
__CALL_PAL_W1(wripir, unsigned long);
__CALL_PAL_W1(wrkgp, unsigned long);
__CALL_PAL_W1(wrmces, unsigned long);
__CALL_PAL_RW2(wrperfmon, unsigned long, unsigned long, unsigned long);
__CALL_PAL_W1(wrusp, unsigned long);
__CALL_PAL_W1(wrvptptr, unsigned long);

/*
 * TB routines..
 */
#define __tbi(nr,arg,arg1...)					\
({								\
	register unsigned long __r16 __asm__("$16") = (nr);	\
	register unsigned long __r17 __asm__("$17"); arg;	\
	__asm__ __volatile__(					\
		"call_pal %3 #__tbi"				\
		:"=r" (__r16),"=r" (__r17)			\
		:"0" (__r16),"i" (PAL_tbi) ,##arg1		\
		:"$0", "$1", "$22", "$23", "$24", "$25");	\
})

#define tbi(x,y)	__tbi(x,__r17=(y),"1" (__r17))
#define tbisi(x)	__tbi(1,__r17=(x),"1" (__r17))
#define tbisd(x)	__tbi(2,__r17=(x),"1" (__r17))
#define tbis(x)		__tbi(3,__r17=(x),"1" (__r17))
#define tbiap()		__tbi(-1, /* no second argument */)
#define tbia()		__tbi(-2, /* no second argument */)

/*
 * Atomic exchange routines.
 */

#define __ASM__MB
#define ____xchg(type, args...)		__xchg ## type ## _local(args)
#define ____cmpxchg(type, args...)	__cmpxchg ## type ## _local(args)
#include <asm/xchg.h>

#define xchg_local(ptr,x)						\
  ({									\
     __typeof__(*(ptr)) _x_ = (x);					\
     (__typeof__(*(ptr))) __xchg_local((ptr), (unsigned long)_x_,	\
				       sizeof(*(ptr)));			\
  })

#define cmpxchg_local(ptr, o, n)					\
  ({									\
     __typeof__(*(ptr)) _o_ = (o);					\
     __typeof__(*(ptr)) _n_ = (n);					\
     (__typeof__(*(ptr))) __cmpxchg_local((ptr), (unsigned long)_o_,	\
					  (unsigned long)_n_,		\
					  sizeof(*(ptr)));		\
  })

#define cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
  })

#ifdef CONFIG_SMP
#undef __ASM__MB
#define __ASM__MB	"\tmb\n"
#endif
#undef ____xchg
#undef ____cmpxchg
#define ____xchg(type, args...)		__xchg ##type(args)
#define ____cmpxchg(type, args...)	__cmpxchg ##type(args)
#include <asm/xchg.h>

#define xchg(ptr,x)							\
  ({									\
     __typeof__(*(ptr)) _x_ = (x);					\
     (__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_,		\
				 sizeof(*(ptr)));			\
  })

#define cmpxchg(ptr, o, n)						\
  ({									\
     __typeof__(*(ptr)) _o_ = (o);					\
     __typeof__(*(ptr)) _n_ = (n);					\
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		\
				    (unsigned long)_n_,	sizeof(*(ptr)));\
  })

#define cmpxchg64(ptr, o, n)						\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg((ptr), (o), (n));					\
  })

#undef __ASM__MB
#undef ____cmpxchg

#define __HAVE_ARCH_CMPXCHG 1

#endif /* __ASSEMBLY__ */

#define arch_align_stack(x) (x)

#endif
