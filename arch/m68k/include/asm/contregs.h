/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_CONTREGS_H
#define _M68K_CONTREGS_H

/* contregs.h:  Addresses of registers in the ASI_CONTROL alternate address
 *              space. These are for the mmu's context register, etc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* 3=sun3
   4=sun4 (as in sun4 sysmaint student book)
   c=sun4c (according to davem) */

#define AC_IDPROM     0x00000000    /* 34  ID PROM, R/O, byte, 32 bytes      */
#define AC_PAGEMAP    0x10000000    /* 3   Pagemap R/W, long                 */
#define AC_SEGMAP     0x20000000    /* 3   Segment map, byte                 */
#define AC_CONTEXT    0x30000000    /* 34c current mmu-context               */
#define AC_SENABLE    0x40000000    /* 34c system dvma/cache/reset enable reg*/
#define AC_UDVMA_ENB  0x50000000    /* 34  Not used on Sun boards, byte      */
#define AC_BUS_ERROR  0x60000000    /* 34  Not cleared on read, byte.        */
#define AC_SYNC_ERR   0x60000000    /*  c fault type                         */
#define AC_SYNC_VA    0x60000004    /*  c fault virtual address              */
#define AC_ASYNC_ERR  0x60000008    /*  c asynchronous fault type            */
#define AC_ASYNC_VA   0x6000000c    /*  c async fault virtual address        */
#define AC_LEDS       0x70000000    /* 34  Zero turns on LEDs, byte          */
#define AC_CACHETAGS  0x80000000    /* 34c direct access to the VAC tags     */
#define AC_CACHEDDATA 0x90000000    /* 3 c direct access to the VAC data     */
#define AC_UDVMA_MAP  0xD0000000    /* 4  Not used on Sun boards, byte       */
#define AC_VME_VECTOR 0xE0000000    /* 4  For non-Autovector VME, byte       */
#define AC_BOOT_SCC   0xF0000000    /* 34  bypass to access Zilog 8530. byte.*/

/* s=Swift, h=Ross_HyperSPARC, v=TI_Viking, t=Tsunami, r=Ross_Cypress        */
#define AC_M_PCR      0x0000        /* shv Processor Control Reg             */
#define AC_M_CTPR     0x0100        /* shv Context Table Pointer Reg         */
#define AC_M_CXR      0x0200        /* shv Context Register                  */
#define AC_M_SFSR     0x0300        /* shv Synchronous Fault Status Reg      */
#define AC_M_SFAR     0x0400        /* shv Synchronous Fault Address Reg     */
#define AC_M_AFSR     0x0500        /*  hv Asynchronous Fault Status Reg     */
#define AC_M_AFAR     0x0600        /*  hv Asynchronous Fault Address Reg    */
#define AC_M_RESET    0x0700        /*  hv Reset Reg                         */
#define AC_M_RPR      0x1000        /*  hv Root Pointer Reg                  */
#define AC_M_TSUTRCR  0x1000        /* s   TLB Replacement Ctrl Reg          */
#define AC_M_IAPTP    0x1100        /*  hv Instruction Access PTP            */
#define AC_M_DAPTP    0x1200        /*  hv Data Access PTP                   */
#define AC_M_ITR      0x1300        /*  hv Index Tag Register                */
#define AC_M_TRCR     0x1400        /*  hv TLB Replacement Control Reg       */
#define AC_M_SFSRX    0x1300        /* s   Synch Fault Status Reg prim       */
#define AC_M_SFARX    0x1400        /* s   Synch Fault Address Reg prim      */
#define AC_M_RPR1     0x1500        /*  h  Root Pointer Reg (entry 2)        */
#define AC_M_IAPTP1   0x1600        /*  h  Instruction Access PTP (entry 2)  */
#define AC_M_DAPTP1   0x1700        /*  h  Data Access PTP (entry 2)         */

#endif /* _M68K_CONTREGS_H */
