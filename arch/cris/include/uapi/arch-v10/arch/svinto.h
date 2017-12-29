/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_CRIS_SVINTO_H
#define _ASM_CRIS_SVINTO_H

#include <arch/sv_addr_ag.h>

extern unsigned int genconfig_shadow; /* defined and set in head.S */

/* dma stuff */

enum {                          /* Available in:  */
	d_eol      = (1 << 0),  /* flags          */
	d_eop      = (1 << 1),  /* flags & status */
	d_wait     = (1 << 2),  /* flags          */
	d_int      = (1 << 3),  /* flags          */
	d_txerr    = (1 << 4),  /* flags          */
	d_stop     = (1 << 4),  /*         status */
	d_ecp      = (1 << 4),  /* flags & status */
	d_pri      = (1 << 5),  /* flags & status */
	d_alignerr = (1 << 6),  /*         status */
	d_crcerr   = (1 << 7)   /*         status */
};

/* Do remember that DMA does not go through the MMU and needs
 * a real physical address, not an address virtually mapped or
 * paged. Therefore the buf/next ptrs below are unsigned long instead
 * of void * to give a warning if you try to put a pointer directly
 * to them instead of going through virt_to_phys/phys_to_virt.
 */

typedef struct etrax_dma_descr {
	unsigned short sw_len;                /* 0-1 */
	unsigned short ctrl;                  /* 2-3 */
	unsigned long  next;                  /* 4-7 */
	unsigned long  buf;                   /* 8-11 */
	unsigned short hw_len;                /* 12-13 */
	unsigned char  status;                /* 14 */
	unsigned char  fifo_len;              /* 15 */
} etrax_dma_descr;


/* Use this for constant numbers only */
#define RESET_DMA_NUM( n ) \
  *R_DMA_CH##n##_CMD = IO_STATE( R_DMA_CH0_CMD, cmd, reset )

/* Use this for constant numbers or symbols, 
 * having two macros makes it possible to use constant expressions. 
 */
#define RESET_DMA( n ) RESET_DMA_NUM( n )


/* Use this for constant numbers only */
#define WAIT_DMA_NUM( n ) \
  while( (*R_DMA_CH##n##_CMD & IO_MASK( R_DMA_CH0_CMD, cmd )) != \
         IO_STATE( R_DMA_CH0_CMD, cmd, hold ) )

/* Use this for constant numbers or symbols 
 * having two macros makes it possible to use constant expressions. 
 */
#define WAIT_DMA( n ) WAIT_DMA_NUM( n )

extern void prepare_rx_descriptor(struct etrax_dma_descr *desc);
extern void flush_etrax_cache(void);

#endif
