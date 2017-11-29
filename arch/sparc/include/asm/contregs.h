/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_CONTREGS_H
#define _SPARC_CONTREGS_H

/* contregs.h:  Addresses of registers in the ASI_CONTROL alternate address
 *              space. These are for the mmu's context register, etc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

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

#endif /* _SPARC_CONTREGS_H */
