/*
 * obio.h:  Some useful locations in 0xFXXXXXXXX PA obio space on sun4d.
 *
 * Copyright (C) 1997 Jakub Jelinek <jj@sunsite.mff.cuni.cz>
 */

#ifndef _SPARC_OBIO_H
#define _SPARC_OBIO_H

#include <asm/asi.h>

/* This weird monster likes to use the very upper parts of
   36bit PA for these things :) */
   
/* CSR space (for each XDBUS)
 *  ------------------------------------------------------------------------
 *  |   0xFE  |   DEVID    |                | XDBUS ID |                   |
 *  ------------------------------------------------------------------------
 *  35      28 27        20 19            10 9        8 7                 0
 */
   
#define CSR_BASE_ADDR		0xe0000000
#define CSR_CPU_SHIFT		(32 - 4 - 5)
#define CSR_XDBUS_SHIFT		8

#define CSR_BASE(cpu) (((CSR_BASE_ADDR >> CSR_CPU_SHIFT) + cpu) << CSR_CPU_SHIFT)

/* ECSR space (not for each XDBUS)
 *  ------------------------------------------------------------------------
 *  |   0xF  | DEVID[7:1] |                			           |
 *  ------------------------------------------------------------------------
 *  35     32 31        25 24                 				  0
 */
   
#define ECSR_BASE_ADDR		0x00000000
#define ECSR_CPU_SHIFT		(32 - 5)
#define ECSR_DEV_SHIFT		(32 - 8)

#define ECSR_BASE(cpu) ((cpu) << ECSR_CPU_SHIFT)
#define ECSR_DEV_BASE(devid) ((devid) << ECSR_DEV_SHIFT) 

/* Bus Watcher */
#define BW_LOCAL_BASE		0xfff00000

#define BW_CID			0x00000000
#define BW_DBUS_CTRL		0x00000008
#define BW_DBUS_DATA		0x00000010
#define BW_CTRL			0x00001000
#define BW_INTR_TABLE		0x00001040
#define BW_INTR_TABLE_CLEAR	0x00001080
#define BW_PRESCALER		0x000010c0
#define BW_PTIMER_LIMIT		0x00002000
#define BW_PTIMER_COUNTER2	0x00002004
#define BW_PTIMER_NDLIMIT	0x00002008
#define BW_PTIMER_CTRL		0x0000200c
#define BW_PTIMER_COUNTER	0x00002010
#define BW_TIMER_LIMIT		0x00003000
#define BW_TIMER_COUNTER2	0x00003004
#define BW_TIMER_NDLIMIT	0x00003008
#define BW_TIMER_CTRL		0x0000300c
#define BW_TIMER_COUNTER	0x00003010

/* BW Control */
#define BW_CTRL_USER_TIMER	0x00000004	/* Is User Timer Free run enabled */

/* Boot Bus */
#define BB_LOCAL_BASE		0xf0000000

#define BB_STAT1		0x00100000
#define BB_STAT2		0x00120000
#define BB_STAT3		0x00140000
#define BB_LEDS			0x002e0000

/* Bits in BB_STAT2 */
#define BB_STAT2_AC_INTR	0x04	/* Aiee! 5ms and power is gone... */
#define BB_STAT2_TMP_INTR	0x10	/* My Penguins are burning. Are you able to smell it? */
#define BB_STAT2_FAN_INTR	0x20	/* My fan refuses to work */
#define BB_STAT2_PWR_INTR	0x40	/* On SC2000, one of the two ACs died. Ok, we go on... */
#define BB_STAT2_MASK		(BB_STAT2_AC_INTR|BB_STAT2_TMP_INTR|BB_STAT2_FAN_INTR|BB_STAT2_PWR_INTR)

/* Cache Controller */
#define CC_BASE		0x1F00000
#define CC_DATSTREAM	0x1F00000  /* Data stream register */
#define CC_DATSIZE	0x1F0003F  /* Size */
#define CC_SRCSTREAM	0x1F00100  /* Source stream register */
#define CC_DESSTREAM	0x1F00200  /* Destination stream register */
#define CC_RMCOUNT	0x1F00300  /* Count of references and misses */
#define CC_IPEN		0x1F00406  /* Pending Interrupts */
#define CC_IMSK		0x1F00506  /* Interrupt Mask */
#define CC_ICLR		0x1F00606  /* Clear pending Interrupts */
#define CC_IGEN		0x1F00704  /* Generate Interrupt register */
#define CC_STEST	0x1F00804  /* Internal self-test */
#define CC_CREG		0x1F00A04  /* Control register */
#define CC_SREG		0x1F00B00  /* Status register */
#define CC_RREG		0x1F00C04  /* Reset register */
#define CC_EREG		0x1F00E00  /* Error code register */
#define CC_CID		0x1F00F04  /* Component ID */

#ifndef __ASSEMBLY__

static inline int bw_get_intr_mask(int sbus_level)
{
	int mask;
	
	__asm__ __volatile__ ("lduha [%1] %2, %0" :
			      "=r" (mask) :
			      "r" (BW_LOCAL_BASE + BW_INTR_TABLE + (sbus_level << 3)),
			      "i" (ASI_M_CTL));
	return mask;
}

static inline void bw_clear_intr_mask(int sbus_level, int mask)
{
	__asm__ __volatile__ ("stha %0, [%1] %2" : :
			      "r" (mask),
			      "r" (BW_LOCAL_BASE + BW_INTR_TABLE_CLEAR + (sbus_level << 3)),
			      "i" (ASI_M_CTL));
}

static inline unsigned bw_get_prof_limit(int cpu)
{
	unsigned limit;
	
	__asm__ __volatile__ ("lda [%1] %2, %0" :
			      "=r" (limit) :
			      "r" (CSR_BASE(cpu) + BW_PTIMER_LIMIT),
			      "i" (ASI_M_CTL));
	return limit;
}

static inline void bw_set_prof_limit(int cpu, unsigned limit)
{
	__asm__ __volatile__ ("sta %0, [%1] %2" : :
			      "r" (limit),
			      "r" (CSR_BASE(cpu) + BW_PTIMER_LIMIT),
			      "i" (ASI_M_CTL));
}

static inline unsigned bw_get_ctrl(int cpu)
{
	unsigned ctrl;
	
	__asm__ __volatile__ ("lda [%1] %2, %0" :
			      "=r" (ctrl) :
			      "r" (CSR_BASE(cpu) + BW_CTRL),
			      "i" (ASI_M_CTL));
	return ctrl;
}

static inline void bw_set_ctrl(int cpu, unsigned ctrl)
{
	__asm__ __volatile__ ("sta %0, [%1] %2" : :
			      "r" (ctrl),
			      "r" (CSR_BASE(cpu) + BW_CTRL),
			      "i" (ASI_M_CTL));
}

static inline unsigned cc_get_ipen(void)
{
	unsigned pending;
	
	__asm__ __volatile__ ("lduha [%1] %2, %0" :
			      "=r" (pending) :
			      "r" (CC_IPEN),
			      "i" (ASI_M_MXCC));
	return pending;
}

static inline void cc_set_iclr(unsigned clear)
{
	__asm__ __volatile__ ("stha %0, [%1] %2" : :
			      "r" (clear),
			      "r" (CC_ICLR),
			      "i" (ASI_M_MXCC));
}

static inline unsigned cc_get_imsk(void)
{
	unsigned mask;
	
	__asm__ __volatile__ ("lduha [%1] %2, %0" :
			      "=r" (mask) :
			      "r" (CC_IMSK),
			      "i" (ASI_M_MXCC));
	return mask;
}

static inline void cc_set_imsk(unsigned mask)
{
	__asm__ __volatile__ ("stha %0, [%1] %2" : :
			      "r" (mask),
			      "r" (CC_IMSK),
			      "i" (ASI_M_MXCC));
}

static inline unsigned cc_get_imsk_other(int cpuid)
{
	unsigned mask;
	
	__asm__ __volatile__ ("lduha [%1] %2, %0" :
			      "=r" (mask) :
			      "r" (ECSR_BASE(cpuid) | CC_IMSK),
			      "i" (ASI_M_CTL));
	return mask;
}

static inline void cc_set_imsk_other(int cpuid, unsigned mask)
{
	__asm__ __volatile__ ("stha %0, [%1] %2" : :
			      "r" (mask),
			      "r" (ECSR_BASE(cpuid) | CC_IMSK),
			      "i" (ASI_M_CTL));
}

static inline void cc_set_igen(unsigned gen)
{
	__asm__ __volatile__ ("sta %0, [%1] %2" : :
			      "r" (gen),
			      "r" (CC_IGEN),
			      "i" (ASI_M_MXCC));
}

/* +-------+-------------+-----------+------------------------------------+
 * | bcast |  devid      |   sid     |              levels mask           |
 * +-------+-------------+-----------+------------------------------------+
 *  31      30         23 22       15 14                                 0
 */
#define IGEN_MESSAGE(bcast, devid, sid, levels) \
	(((bcast) << 31) | ((devid) << 23) | ((sid) << 15) | (levels))
            
static inline void sun4d_send_ipi(int cpu, int level)
{
	cc_set_igen(IGEN_MESSAGE(0, cpu << 3, 6 + ((level >> 1) & 7), 1 << (level - 1)));
}

#endif /* !__ASSEMBLY__ */

#endif /* !(_SPARC_OBIO_H) */
