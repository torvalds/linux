
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
#include "dsrv_pri.h"
#include "dsp_defs.h"
/*****************************************************************************/
#define MAX_XLOG_SIZE  (64 * 1024)
/* -------------------------------------------------------------------------
  Does return offset between ADAPTER->ram and real begin of memory
  ------------------------------------------------------------------------- */
static dword pri_ram_offset (ADAPTER* a) {
 return ((dword)MP_SHARED_RAM_OFFSET);
}
/* -------------------------------------------------------------------------
  Recovery XLOG buffer from the card
  ------------------------------------------------------------------------- */
static void pri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
 byte  __iomem *base ;
 word *Xlog ;
 dword   regs[4], TrapID, size ;
 Xdesc   xlogDesc ;
/*
 * check for trapped MIPS 46xx CPU, dump exception frame
 */
 base   = DIVA_OS_MEM_ATTACH_ADDRESS(IoAdapter);
 TrapID = READ_DWORD(&base[0x80]) ;
 if ( (TrapID == 0x99999999) || (TrapID == 0x99999901) )
 {
  dump_trap_frame (IoAdapter, &base[0x90]) ;
  IoAdapter->trapped = 1 ;
 }
 regs[0] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x70]);
 regs[1] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x74]);
 regs[2] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x78]);
 regs[3] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x7c]);
 regs[0] &= IoAdapter->MemorySize - 1 ;
 if ( (regs[0] < IoAdapter->MemorySize - 1) )
 {
  if ( !(Xlog = (word *)diva_os_malloc (0, MAX_XLOG_SIZE)) ) {
   DIVA_OS_MEM_DETACH_ADDRESS(IoAdapter, base);
   return ;
  }
  size = IoAdapter->MemorySize - regs[0] ;
  if ( size > MAX_XLOG_SIZE )
   size = MAX_XLOG_SIZE ;
  memcpy_fromio(Xlog, &base[regs[0]], size) ;
  xlogDesc.buf = Xlog ;
  xlogDesc.cnt = READ_WORD(&base[regs[1] & (IoAdapter->MemorySize - 1)]) ;
  xlogDesc.out = READ_WORD(&base[regs[2] & (IoAdapter->MemorySize - 1)]) ;
  dump_xlog_buffer (IoAdapter, &xlogDesc) ;
  diva_os_free (0, Xlog) ;
  IoAdapter->trapped = 2 ;
 }
 DIVA_OS_MEM_DETACH_ADDRESS(IoAdapter, base);
}
/* -------------------------------------------------------------------------
  Hardware reset of PRI card
  ------------------------------------------------------------------------- */
static void reset_pri_hardware (PISDN_ADAPTER IoAdapter) {
 byte __iomem *p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
 WRITE_BYTE(p, _MP_RISC_RESET | _MP_LED1 | _MP_LED2);
 diva_os_wait (50) ;
 WRITE_BYTE(p, 0x00);
 diva_os_wait (50) ;
 DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
}
/* -------------------------------------------------------------------------
  Stop Card Hardware
  ------------------------------------------------------------------------- */
static void stop_pri_hardware (PISDN_ADAPTER IoAdapter) {
 dword i;
 byte __iomem *p;
 dword volatile __iomem *cfgReg = (void __iomem *)DIVA_OS_MEM_ATTACH_CFG(IoAdapter);
 WRITE_DWORD(&cfgReg[3], 0);
 WRITE_DWORD(&cfgReg[1], 0);
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfgReg);
 IoAdapter->a.ram_out (&IoAdapter->a, &RAM->SWReg, SWREG_HALT_CPU) ;
 i = 0 ;
 while ( (i < 100) && (IoAdapter->a.ram_in (&IoAdapter->a, &RAM->SWReg) != 0) )
 {
  diva_os_wait (1) ;
  i++ ;
 }
 DBG_TRC(("%s: PRI stopped (%d)", IoAdapter->Name, i))
 cfgReg = (void __iomem *)DIVA_OS_MEM_ATTACH_CFG(IoAdapter);
 WRITE_DWORD(&cfgReg[0],((dword)(~0x03E00000)));
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfgReg);
 diva_os_wait (1) ;
 p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
 WRITE_BYTE(p, _MP_RISC_RESET | _MP_LED1 | _MP_LED2);
 DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
}
static int load_pri_hardware (PISDN_ADAPTER IoAdapter) {
 return (0);
}
/* --------------------------------------------------------------------------
  PRI Adapter interrupt Service Routine
   -------------------------------------------------------------------------- */
static int pri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
 byte __iomem *cfg = DIVA_OS_MEM_ATTACH_CFG(IoAdapter);
 if ( !(READ_DWORD(cfg) & 0x80000000) ) {
  DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfg);
  return (0) ;
 }
 /*
  clear interrupt line
  */
 WRITE_DWORD(cfg, (dword)~0x03E00000) ;
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfg);
 IoAdapter->IrqCount++ ;
 if ( IoAdapter->Initialized )
 {
  diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
 }
 return (1) ;
}
/* -------------------------------------------------------------------------
  Disable interrupt in the card hardware
  ------------------------------------------------------------------------- */
static void disable_pri_interrupt (PISDN_ADAPTER IoAdapter) {
 dword volatile __iomem *cfgReg = (dword volatile __iomem *)DIVA_OS_MEM_ATTACH_CFG(IoAdapter) ;
 WRITE_DWORD(&cfgReg[3], 0);
 WRITE_DWORD(&cfgReg[1], 0);
 WRITE_DWORD(&cfgReg[0], (dword)(~0x03E00000)) ;
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfgReg);
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Adapter
  ------------------------------------------------------------------------- */
static void prepare_common_pri_functions (PISDN_ADAPTER IoAdapter) {
 ADAPTER *a = &IoAdapter->a ;
 a->ram_in           = mem_in ;
 a->ram_inw          = mem_inw ;
 a->ram_in_buffer    = mem_in_buffer ;
 a->ram_look_ahead   = mem_look_ahead ;
 a->ram_out          = mem_out ;
 a->ram_outw         = mem_outw ;
 a->ram_out_buffer   = mem_out_buffer ;
 a->ram_inc          = mem_inc ;
 a->ram_offset       = pri_ram_offset ;
 a->ram_out_dw    = mem_out_dw;
 a->ram_in_dw    = mem_in_dw;
  a->istream_wakeup   = pr_stream;
 IoAdapter->out      = pr_out ;
 IoAdapter->dpc      = pr_dpc ;
 IoAdapter->tst_irq  = scom_test_int ;
 IoAdapter->clr_irq  = scom_clear_int ;
 IoAdapter->pcm      = (struct pc_maint *)(MIPS_MAINT_OFFS
                                        - MP_SHARED_RAM_OFFSET) ;
 IoAdapter->load     = load_pri_hardware ;
 IoAdapter->disIrq   = disable_pri_interrupt ;
 IoAdapter->rstFnc   = reset_pri_hardware ;
 IoAdapter->stop     = stop_pri_hardware ;
 IoAdapter->trapFnc  = pri_cpu_trapped ;
 IoAdapter->diva_isr_handler = pri_ISR;
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Adapter
  ------------------------------------------------------------------------- */
void prepare_pri_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MP_MEMORY_SIZE ;
 prepare_common_pri_functions (IoAdapter) ;
 diva_os_prepare_pri_functions (IoAdapter);
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Rev.2 Adapter
  ------------------------------------------------------------------------- */
void prepare_pri2_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MP2_MEMORY_SIZE ;
 prepare_common_pri_functions (IoAdapter) ;
 diva_os_prepare_pri2_functions (IoAdapter);
}
/* ------------------------------------------------------------------------- */
