/*
** atariints.h -- Atari Linux interrupt handling structs and prototypes
**
** Copyright 1994 by Björn Brauel
**
** 5/2/94 Roman Hodek:
**   TT interrupt definitions added.
**
** 12/02/96: (Roman)
**   Adapted to new int handling scheme (see ataints.c); revised numbering
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
*/

#ifndef _LINUX_ATARIINTS_H_
#define _LINUX_ATARIINTS_H_

#include <asm/irq.h>
#include <asm/atarihw.h>

/*
** Atari Interrupt sources.
**
*/

#define STMFP_SOURCE_BASE  8
#define TTMFP_SOURCE_BASE  24
#define SCC_SOURCE_BASE    40
#define VME_SOURCE_BASE    56
#define VME_MAX_SOURCES    16

#define NUM_ATARI_SOURCES  141

/* convert vector number to int source number */
#define IRQ_VECTOR_TO_SOURCE(v)	((v) - ((v) < 0x20 ? 0x18 : (0x40-8)))

/* convert irq_handler index to vector number */
#define IRQ_SOURCE_TO_VECTOR(i)	((i) + ((i) < 8 ? 0x18 : (0x40-8)))

/* interrupt service types */
#define IRQ_TYPE_SLOW     0
#define IRQ_TYPE_FAST     1
#define IRQ_TYPE_PRIO     2

/* ST-MFP interrupts */
#define IRQ_MFP_BUSY      (8)
#define IRQ_MFP_DCD       (9)
#define IRQ_MFP_CTS	  (10)
#define IRQ_MFP_GPU	  (11)
#define IRQ_MFP_TIMD      (12)
#define IRQ_MFP_TIMC	  (13)
#define IRQ_MFP_ACIA	  (14)
#define IRQ_MFP_FDC       (15)
#define IRQ_MFP_ACSI      IRQ_MFP_FDC
#define IRQ_MFP_FSCSI     IRQ_MFP_FDC
#define IRQ_MFP_IDE       IRQ_MFP_FDC
#define IRQ_MFP_TIMB      (16)
#define IRQ_MFP_SERERR    (17)
#define IRQ_MFP_SEREMPT   (18)
#define IRQ_MFP_RECERR    (19)
#define IRQ_MFP_RECFULL   (20)
#define IRQ_MFP_TIMA      (21)
#define IRQ_MFP_RI        (22)
#define IRQ_MFP_MMD       (23)

/* TT-MFP interrupts */
#define IRQ_TT_MFP_IO0       (24)
#define IRQ_TT_MFP_IO1       (25)
#define IRQ_TT_MFP_SCC	     (26)
#define IRQ_TT_MFP_RI	     (27)
#define IRQ_TT_MFP_TIMD      (28)
#define IRQ_TT_MFP_TIMC	     (29)
#define IRQ_TT_MFP_DRVRDY    (30)
#define IRQ_TT_MFP_SCSIDMA   (31)
#define IRQ_TT_MFP_TIMB      (32)
#define IRQ_TT_MFP_SERERR    (33)
#define IRQ_TT_MFP_SEREMPT   (34)
#define IRQ_TT_MFP_RECERR    (35)
#define IRQ_TT_MFP_RECFULL   (36)
#define IRQ_TT_MFP_TIMA      (37)
#define IRQ_TT_MFP_RTC       (38)
#define IRQ_TT_MFP_SCSI      (39)

/* SCC interrupts */
#define IRQ_SCCB_TX	     (40)
#define IRQ_SCCB_STAT	     (42)
#define IRQ_SCCB_RX	     (44)
#define IRQ_SCCB_SPCOND	     (46)
#define IRQ_SCCA_TX	     (48)
#define IRQ_SCCA_STAT	     (50)
#define IRQ_SCCA_RX	     (52)
#define IRQ_SCCA_SPCOND	     (54)

/* shared MFP timer D interrupts - hires timer for EtherNEC et al. */
#define IRQ_MFP_TIMER1       (64)
#define IRQ_MFP_TIMER2       (65)
#define IRQ_MFP_TIMER3       (66)
#define IRQ_MFP_TIMER4       (67)
#define IRQ_MFP_TIMER5       (68)
#define IRQ_MFP_TIMER6       (69)
#define IRQ_MFP_TIMER7       (70)
#define IRQ_MFP_TIMER8       (71)

#define INT_CLK   24576	    /* CLK while int_clk =2.456MHz and divide = 100 */
#define INT_TICKS 246	    /* to make sched_time = 99.902... HZ */


#define MFP_ENABLE	0
#define MFP_PENDING	1
#define MFP_SERVICE	2
#define MFP_MASK	3

/* Utility functions for setting/clearing bits in the interrupt registers of
 * the MFP. 'type' should be constant, if 'irq' is constant, too, code size is
 * reduced. set_mfp_bit() is nonsense for PENDING and SERVICE registers. */

static inline int get_mfp_bit( unsigned irq, int type )

{	unsigned char	mask, *reg;

	mask = 1 << (irq & 7);
	reg = (unsigned char *)&st_mfp.int_en_a + type*4 +
		  ((irq & 8) >> 2) + (((irq-8) & 16) << 3);
	return( *reg & mask );
}

static inline void set_mfp_bit( unsigned irq, int type )

{	unsigned char	mask, *reg;

	mask = 1 << (irq & 7);
	reg = (unsigned char *)&st_mfp.int_en_a + type*4 +
		  ((irq & 8) >> 2) + (((irq-8) & 16) << 3);
	__asm__ __volatile__ ( "orb %0,%1"
			      : : "di" (mask), "m" (*reg) : "memory" );
}

static inline void clear_mfp_bit( unsigned irq, int type )

{	unsigned char	mask, *reg;

	mask = ~(1 << (irq & 7));
	reg = (unsigned char *)&st_mfp.int_en_a + type*4 +
		  ((irq & 8) >> 2) + (((irq-8) & 16) << 3);
	if (type == MFP_PENDING || type == MFP_SERVICE)
		__asm__ __volatile__ ( "moveb %0,%1"
				      : : "di" (mask), "m" (*reg) : "memory" );
	else
		__asm__ __volatile__ ( "andb %0,%1"
				      : : "di" (mask), "m" (*reg) : "memory" );
}

/*
 * {en,dis}able_irq have the usual semantics of temporary blocking the
 * interrupt, but not losing requests that happen between disabling and
 * enabling. This is done with the MFP mask registers.
 */

static inline void atari_enable_irq( unsigned irq )

{
	if (irq < STMFP_SOURCE_BASE || irq >= SCC_SOURCE_BASE) return;
	set_mfp_bit( irq, MFP_MASK );
}

static inline void atari_disable_irq( unsigned irq )

{
	if (irq < STMFP_SOURCE_BASE || irq >= SCC_SOURCE_BASE) return;
	clear_mfp_bit( irq, MFP_MASK );
}

/*
 * In opposite to {en,dis}able_irq, requests between turn{off,on}_irq are not
 * "stored"
 */

static inline void atari_turnon_irq( unsigned irq )

{
	if (irq < STMFP_SOURCE_BASE || irq >= SCC_SOURCE_BASE) return;
	set_mfp_bit( irq, MFP_ENABLE );
}

static inline void atari_turnoff_irq( unsigned irq )

{
	if (irq < STMFP_SOURCE_BASE || irq >= SCC_SOURCE_BASE) return;
	clear_mfp_bit( irq, MFP_ENABLE );
	clear_mfp_bit( irq, MFP_PENDING );
}

static inline void atari_clear_pending_irq( unsigned irq )

{
	if (irq < STMFP_SOURCE_BASE || irq >= SCC_SOURCE_BASE) return;
	clear_mfp_bit( irq, MFP_PENDING );
}

static inline int atari_irq_pending( unsigned irq )

{
	if (irq < STMFP_SOURCE_BASE || irq >= SCC_SOURCE_BASE) return( 0 );
	return( get_mfp_bit( irq, MFP_PENDING ) );
}

unsigned int atari_register_vme_int(void);
void atari_unregister_vme_int(unsigned int);

#endif /* linux/atariints.h */
