
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "pr_pc.h"
#include "di.h"
#include "mi_pc.h"
#include "pc_maint.h"
#include "divasync.h"
#include "io.h"
#include "helpers.h"
#include "dsrv_bri.h"
#include "dsp_defs.h"
/*****************************************************************************/
#define MAX_XLOG_SIZE (64 * 1024)
/* --------------------------------------------------------------------------
  Investigate card state, recovery trace buffer
  -------------------------------------------------------------------------- */
static void bri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
 byte  __iomem *addrHi, *addrLo, *ioaddr ;
 word *Xlog ;
 dword   regs[4], i, size ;
 Xdesc   xlogDesc ;
 byte __iomem *Port;
/*
 * first read pointers and trap frame
 */
 if ( !(Xlog = (word *)diva_os_malloc (0, MAX_XLOG_SIZE)) )
  return ;
 Port = DIVA_OS_MEM_ATTACH_PORT(IoAdapter);
 addrHi =   Port + ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH) ;
 addrLo = Port + ADDR ;
 ioaddr = Port + DATA ;
 outpp (addrHi,  0) ;
 outppw (addrLo, 0) ;
 for ( i = 0 ; i < 0x100 ; Xlog[i++] = inppw(ioaddr) ) ;
/*
 * check for trapped MIPS 3xxx CPU, dump only exception frame
 */
 if ( GET_DWORD(&Xlog[0x80 / sizeof(Xlog[0])]) == 0x99999999 )
 {
  dump_trap_frame (IoAdapter, &((byte *)Xlog)[0x90]) ;
  IoAdapter->trapped = 1 ;
 }
 regs[0] = GET_DWORD(&((byte *)Xlog)[0x70]);
 regs[1] = GET_DWORD(&((byte *)Xlog)[0x74]);
 regs[2] = GET_DWORD(&((byte *)Xlog)[0x78]);
 regs[3] = GET_DWORD(&((byte *)Xlog)[0x7c]);
 outpp (addrHi, (regs[1] >> 16) & 0x7F) ;
 outppw (addrLo, regs[1] & 0xFFFF) ;
 xlogDesc.cnt = inppw(ioaddr) ;
 outpp (addrHi, (regs[2] >> 16) & 0x7F) ;
 outppw (addrLo, regs[2] & 0xFFFF) ;
 xlogDesc.out = inppw(ioaddr) ;
 xlogDesc.buf = Xlog ;
 regs[0] &= IoAdapter->MemorySize - 1 ;
 if ( (regs[0] < IoAdapter->MemorySize - 1) )
 {
  size = IoAdapter->MemorySize - regs[0] ;
  if ( size > MAX_XLOG_SIZE )
   size = MAX_XLOG_SIZE ;
  for ( i = 0 ; i < (size / sizeof(*Xlog)) ; regs[0] += 2 )
  {
   outpp (addrHi, (regs[0] >> 16) & 0x7F) ;
   outppw (addrLo, regs[0] & 0xFFFF) ;
   Xlog[i++] = inppw(ioaddr) ;
  }
  dump_xlog_buffer (IoAdapter, &xlogDesc) ;
  diva_os_free (0, Xlog) ;
  IoAdapter->trapped = 2 ;
 }
 outpp  (addrHi, (byte)((BRI_UNCACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                            BRI_SHARED_RAM_SIZE)) >> 16)) ;
 outppw (addrLo, 0x00) ;
 DIVA_OS_MEM_DETACH_PORT(IoAdapter, Port);
}
/* ---------------------------------------------------------------------
   Reset hardware
  --------------------------------------------------------------------- */
static void reset_bri_hardware (PISDN_ADAPTER IoAdapter) {
 byte __iomem *p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
 outpp (p, 0x00) ;
 DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
}
/* ---------------------------------------------------------------------
   Halt system
  --------------------------------------------------------------------- */
static void stop_bri_hardware (PISDN_ADAPTER IoAdapter) {
 byte __iomem *p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
 if (p) {
  outpp (p, 0x00) ; /* disable interrupts ! */
 }
 DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
 p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
 outpp (p, 0x00) ;    /* clear int, halt cpu */
 DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
}
static int load_bri_hardware (PISDN_ADAPTER IoAdapter) {
 return (0);
}
/******************************************************************************/
static int bri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
 byte __iomem *p;

 p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
 if ( !(inpp (p) & 0x01) ) {
  DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
  return (0) ;
 }
 /*
  clear interrupt line
  */
 outpp (p, 0x08) ;
 DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
 IoAdapter->IrqCount++ ;
 if ( IoAdapter->Initialized ) {
  diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
 }
 return (1) ;
}
/* --------------------------------------------------------------------------
  Disable IRQ in the card hardware
  -------------------------------------------------------------------------- */
static void disable_bri_interrupt (PISDN_ADAPTER IoAdapter) {
 byte __iomem *p;
 p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
 if ( p )
 {
  outpp (p, 0x00) ; /* disable interrupts ! */
 }
 DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
 p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
 outpp (p, 0x00) ; /* clear int, halt cpu */
 DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
}
/* -------------------------------------------------------------------------
  Fill card entry points
  ------------------------------------------------------------------------- */
void prepare_maestra_functions (PISDN_ADAPTER IoAdapter) {
 ADAPTER *a = &IoAdapter->a ;
 a->ram_in             = io_in ;
 a->ram_inw            = io_inw ;
 a->ram_in_buffer      = io_in_buffer ;
 a->ram_look_ahead     = io_look_ahead ;
 a->ram_out            = io_out ;
 a->ram_outw           = io_outw ;
 a->ram_out_buffer     = io_out_buffer ;
 a->ram_inc            = io_inc ;
 IoAdapter->MemoryBase = BRI_MEMORY_BASE ;
 IoAdapter->MemorySize = BRI_MEMORY_SIZE ;
 IoAdapter->out        = pr_out ;
 IoAdapter->dpc        = pr_dpc ;
 IoAdapter->tst_irq    = scom_test_int ;
 IoAdapter->clr_irq    = scom_clear_int ;
 IoAdapter->pcm        = (struct pc_maint *)MIPS_MAINT_OFFS ;
 IoAdapter->load       = load_bri_hardware ;
 IoAdapter->disIrq     = disable_bri_interrupt ;
 IoAdapter->rstFnc     = reset_bri_hardware ;
 IoAdapter->stop       = stop_bri_hardware ;
 IoAdapter->trapFnc    = bri_cpu_trapped ;
 IoAdapter->diva_isr_handler = bri_ISR;
 /*
  Prepare OS dependent functions
  */
 diva_os_prepare_maestra_functions (IoAdapter);
}
/* -------------------------------------------------------------------------- */
