
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
#include "divasync.h"
#define MIPS_SCOM
#include "pkmaint.h" /* pc_main.h, packed in os-dependent fashion */
#include "di.h"
#include "mi_pc.h"
#include "io.h"
extern ADAPTER * adapter[MAX_ADAPTER];
extern PISDN_ADAPTER IoAdapters[MAX_ADAPTER];
void request (PISDN_ADAPTER, ENTITY *);
static void pcm_req (PISDN_ADAPTER, ENTITY *);
/* --------------------------------------------------------------------------
  local functions
  -------------------------------------------------------------------------- */
#define ReqFunc(N) \
static void Request##N(ENTITY *e) \
{ if ( IoAdapters[N] ) (* IoAdapters[N]->DIRequest)(IoAdapters[N], e) ; }
ReqFunc(0)
ReqFunc(1)
ReqFunc(2)
ReqFunc(3)
ReqFunc(4)
ReqFunc(5)
ReqFunc(6)
ReqFunc(7)
ReqFunc(8)
ReqFunc(9)
ReqFunc(10)
ReqFunc(11)
ReqFunc(12)
ReqFunc(13)
ReqFunc(14)
ReqFunc(15)
IDI_CALL Requests[MAX_ADAPTER] =
{ &Request0, &Request1, &Request2, &Request3,
 &Request4, &Request5, &Request6, &Request7,
 &Request8, &Request9, &Request10, &Request11,
 &Request12, &Request13, &Request14, &Request15
};
/*****************************************************************************/
/*
  This array should indicate all new services, that this version of XDI
  is able to provide to his clients
  */
static byte extended_xdi_features[DIVA_XDI_EXTENDED_FEATURES_MAX_SZ+1] = {
 (DIVA_XDI_EXTENDED_FEATURES_VALID       |
  DIVA_XDI_EXTENDED_FEATURE_SDRAM_BAR    |
  DIVA_XDI_EXTENDED_FEATURE_CAPI_PRMS    |
#if defined(DIVA_IDI_RX_DMA)
  DIVA_XDI_EXTENDED_FEATURE_CMA          |
  DIVA_XDI_EXTENDED_FEATURE_RX_DMA       |
  DIVA_XDI_EXTENDED_FEATURE_MANAGEMENT_DMA |
#endif
  DIVA_XDI_EXTENDED_FEATURE_NO_CANCEL_RC),
 0
};
/*****************************************************************************/
void
dump_xlog_buffer (PISDN_ADAPTER IoAdapter, Xdesc *xlogDesc)
{
 dword   logLen ;
 word *Xlog   = xlogDesc->buf ;
 word  logCnt = xlogDesc->cnt ;
 word  logOut = xlogDesc->out / sizeof(*Xlog) ;
 DBG_FTL(("%s: ************* XLOG recovery (%d) *************",
          &IoAdapter->Name[0], (int)logCnt))
 DBG_FTL(("Microcode: %s", &IoAdapter->ProtocolIdString[0]))
 for ( ; logCnt > 0 ; --logCnt )
 {
  if ( !GET_WORD(&Xlog[logOut]) )
  {
   if ( --logCnt == 0 )
    break ;
   logOut = 0 ;
  }
  if ( GET_WORD(&Xlog[logOut]) <= (logOut * sizeof(*Xlog)) )
  {
   if ( logCnt > 2 )
   {
    DBG_FTL(("Possibly corrupted XLOG: %d entries left",
             (int)logCnt))
   }
   break ;
  }
  logLen = (dword)(GET_WORD(&Xlog[logOut]) - (logOut * sizeof(*Xlog))) ;
  DBG_FTL_MXLOG(( (char *)&Xlog[logOut + 1], (dword)(logLen - 2) ))
  logOut = (GET_WORD(&Xlog[logOut]) + 1) / sizeof(*Xlog) ;
 }
 DBG_FTL(("%s: ***************** end of XLOG *****************",
          &IoAdapter->Name[0]))
}
/*****************************************************************************/
#if defined(XDI_USE_XLOG)
static char *(ExceptionCauseTable[]) =
{
 "Interrupt",
 "TLB mod /IBOUND",
 "TLB load /DBOUND",
 "TLB store",
 "Address error load",
 "Address error store",
 "Instruction load bus error",
 "Data load/store bus error",
 "Syscall",
 "Breakpoint",
 "Reverd instruction",
 "Coprocessor unusable",
 "Overflow",
 "TRAP",
 "VCEI",
 "Floating Point Exception",
 "CP2",
 "Reserved 17",
 "Reserved 18",
 "Reserved 19",
 "Reserved 20",
 "Reserved 21",
 "Reserved 22",
 "WATCH",
 "Reserved 24",
 "Reserved 25",
 "Reserved 26",
 "Reserved 27",
 "Reserved 28",
 "Reserved 29",
 "Reserved 30",
 "VCED"
} ;
#endif
void
dump_trap_frame (PISDN_ADAPTER IoAdapter, byte __iomem *exceptionFrame)
{
 MP_XCPTC __iomem *xcept = (MP_XCPTC __iomem *)exceptionFrame ;
 dword    __iomem *regs;
 regs  = &xcept->regs[0] ;
 DBG_FTL(("%s: ***************** CPU TRAPPED *****************",
          &IoAdapter->Name[0]))
 DBG_FTL(("Microcode: %s", &IoAdapter->ProtocolIdString[0]))
 DBG_FTL(("Cause: %s",
          ExceptionCauseTable[(READ_DWORD(&xcept->cr) & 0x0000007c) >> 2]))
 DBG_FTL(("sr    0x%08x cr    0x%08x epc   0x%08x vaddr 0x%08x",
          READ_DWORD(&xcept->sr), READ_DWORD(&xcept->cr),
					READ_DWORD(&xcept->epc), READ_DWORD(&xcept->vaddr)))
 DBG_FTL(("zero  0x%08x at    0x%08x v0    0x%08x v1    0x%08x",
          READ_DWORD(&regs[ 0]), READ_DWORD(&regs[ 1]),
					READ_DWORD(&regs[ 2]), READ_DWORD(&regs[ 3])))
 DBG_FTL(("a0    0x%08x a1    0x%08x a2    0x%08x a3    0x%08x",
          READ_DWORD(&regs[ 4]), READ_DWORD(&regs[ 5]),
					READ_DWORD(&regs[ 6]), READ_DWORD(&regs[ 7])))
 DBG_FTL(("t0    0x%08x t1    0x%08x t2    0x%08x t3    0x%08x",
          READ_DWORD(&regs[ 8]), READ_DWORD(&regs[ 9]),
					READ_DWORD(&regs[10]), READ_DWORD(&regs[11])))
 DBG_FTL(("t4    0x%08x t5    0x%08x t6    0x%08x t7    0x%08x",
          READ_DWORD(&regs[12]), READ_DWORD(&regs[13]),
					READ_DWORD(&regs[14]), READ_DWORD(&regs[15])))
 DBG_FTL(("s0    0x%08x s1    0x%08x s2    0x%08x s3    0x%08x",
          READ_DWORD(&regs[16]), READ_DWORD(&regs[17]),
					READ_DWORD(&regs[18]), READ_DWORD(&regs[19])))
 DBG_FTL(("s4    0x%08x s5    0x%08x s6    0x%08x s7    0x%08x",
          READ_DWORD(&regs[20]), READ_DWORD(&regs[21]),
					READ_DWORD(&regs[22]), READ_DWORD(&regs[23])))
 DBG_FTL(("t8    0x%08x t9    0x%08x k0    0x%08x k1    0x%08x",
          READ_DWORD(&regs[24]), READ_DWORD(&regs[25]),
					READ_DWORD(&regs[26]), READ_DWORD(&regs[27])))
 DBG_FTL(("gp    0x%08x sp    0x%08x s8    0x%08x ra    0x%08x",
          READ_DWORD(&regs[28]), READ_DWORD(&regs[29]),
					READ_DWORD(&regs[30]), READ_DWORD(&regs[31])))
 DBG_FTL(("md    0x%08x|%08x         resvd 0x%08x class 0x%08x",
          READ_DWORD(&xcept->mdhi), READ_DWORD(&xcept->mdlo),
					READ_DWORD(&xcept->reseverd), READ_DWORD(&xcept->xclass)))
}
/* --------------------------------------------------------------------------
  Real XDI Request function
  -------------------------------------------------------------------------- */
void request(PISDN_ADAPTER IoAdapter, ENTITY * e)
{
 byte i;
 diva_os_spin_lock_magic_t irql;
/*
 * if the Req field in the entity structure is 0,
 * we treat this request as a special function call
 */
 if ( !e->Req )
 {
  IDI_SYNC_REQ *syncReq = (IDI_SYNC_REQ *)e ;
  switch (e->Rc)
  {
#if defined(DIVA_IDI_RX_DMA)
    case IDI_SYNC_REQ_DMA_DESCRIPTOR_OPERATION: {
      diva_xdi_dma_descriptor_operation_t* pI = \
                                   &syncReq->xdi_dma_descriptor_operation.info;
      if (!IoAdapter->dma_map) {
        pI->operation         = -1;
        pI->descriptor_number = -1;
        return;
      }
      diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "dma_op");
      if (pI->operation == IDI_SYNC_REQ_DMA_DESCRIPTOR_ALLOC) {
        pI->descriptor_number = diva_alloc_dma_map_entry (\
                               (struct _diva_dma_map_entry*)IoAdapter->dma_map);
        if (pI->descriptor_number >= 0) {
          dword dma_magic;
          void* local_addr;
          diva_get_dma_map_entry (\
                               (struct _diva_dma_map_entry*)IoAdapter->dma_map,
                               pI->descriptor_number,
                               &local_addr, &dma_magic);
          pI->descriptor_address  = local_addr;
          pI->descriptor_magic    = dma_magic;
          pI->operation           = 0;
        } else {
          pI->operation           = -1;
        }
      } else if ((pI->operation == IDI_SYNC_REQ_DMA_DESCRIPTOR_FREE) &&
                 (pI->descriptor_number >= 0)) {
        diva_free_dma_map_entry((struct _diva_dma_map_entry*)IoAdapter->dma_map,
                                pI->descriptor_number);
        pI->descriptor_number = -1;
        pI->operation         = 0;
      } else {
        pI->descriptor_number = -1;
        pI->operation         = -1;
      }
      diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "dma_op");
    } return;
#endif
    case IDI_SYNC_REQ_XDI_GET_LOGICAL_ADAPTER_NUMBER: {
      diva_xdi_get_logical_adapter_number_s_t *pI = \
                                     &syncReq->xdi_logical_adapter_number.info;
      pI->logical_adapter_number = IoAdapter->ANum;
      pI->controller = IoAdapter->ControllerNumber;
      pI->total_controllers = IoAdapter->Properties.Adapters;
    } return;
    case IDI_SYNC_REQ_XDI_GET_CAPI_PARAMS: {
       diva_xdi_get_capi_parameters_t prms, *pI = &syncReq->xdi_capi_prms.info;
       memset (&prms, 0x00, sizeof(prms));
       prms.structure_length = min_t(size_t, sizeof(prms), pI->structure_length);
       memset (pI, 0x00, pI->structure_length);
       prms.flag_dynamic_l1_down    = (IoAdapter->capi_cfg.cfg_1 & \
         DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON) ? 1 : 0;
       prms.group_optimization_enabled = (IoAdapter->capi_cfg.cfg_1 & \
         DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON) ? 1 : 0;
       memcpy (pI, &prms, prms.structure_length);
      } return;
    case IDI_SYNC_REQ_XDI_GET_ADAPTER_SDRAM_BAR:
      syncReq->xdi_sdram_bar.info.bar = IoAdapter->sdram_bar;
      return;
    case IDI_SYNC_REQ_XDI_GET_EXTENDED_FEATURES: {
      dword i;
      diva_xdi_get_extended_xdi_features_t* pI =\
                                 &syncReq->xdi_extended_features.info;
      pI->buffer_length_in_bytes &= ~0x80000000;
      if (pI->buffer_length_in_bytes && pI->features) {
        memset (pI->features, 0x00, pI->buffer_length_in_bytes);
      }
      for (i = 0; ((pI->features) && (i < pI->buffer_length_in_bytes) &&
                   (i < DIVA_XDI_EXTENDED_FEATURES_MAX_SZ)); i++) {
        pI->features[i] = extended_xdi_features[i];
      }
      if ((pI->buffer_length_in_bytes < DIVA_XDI_EXTENDED_FEATURES_MAX_SZ) ||
          (!pI->features)) {
        pI->buffer_length_in_bytes =\
                           (0x80000000 | DIVA_XDI_EXTENDED_FEATURES_MAX_SZ);
      }
     } return;
    case IDI_SYNC_REQ_XDI_GET_STREAM:
      if (IoAdapter) {
        diva_xdi_provide_istream_info (&IoAdapter->a,
                                       &syncReq->xdi_stream_info.info);
      } else {
        syncReq->xdi_stream_info.info.provided_service = 0;
      }
      return;
  case IDI_SYNC_REQ_GET_NAME:
   if ( IoAdapter )
   {
    strcpy (&syncReq->GetName.name[0], IoAdapter->Name) ;
    DBG_TRC(("xdi: Adapter %d / Name '%s'",
             IoAdapter->ANum, IoAdapter->Name))
    return ;
   }
   syncReq->GetName.name[0] = '\0' ;
   break ;
  case IDI_SYNC_REQ_GET_SERIAL:
   if ( IoAdapter )
   {
    syncReq->GetSerial.serial = IoAdapter->serialNo ;
    DBG_TRC(("xdi: Adapter %d / SerialNo %ld",
             IoAdapter->ANum, IoAdapter->serialNo))
    return ;
   }
   syncReq->GetSerial.serial = 0 ;
   break ;
  case IDI_SYNC_REQ_GET_CARDTYPE:
   if ( IoAdapter )
   {
    syncReq->GetCardType.cardtype = IoAdapter->cardType ;
    DBG_TRC(("xdi: Adapter %d / CardType %ld",
             IoAdapter->ANum, IoAdapter->cardType))
    return ;
   }
   syncReq->GetCardType.cardtype = 0 ;
   break ;
  case IDI_SYNC_REQ_GET_XLOG:
   if ( IoAdapter )
   {
    pcm_req (IoAdapter, e) ;
    return ;
   }
   e->Ind = 0 ;
   break ;
  case IDI_SYNC_REQ_GET_DBG_XLOG:
   if ( IoAdapter )
   {
    pcm_req (IoAdapter, e) ;
    return ;
   }
   e->Ind = 0 ;
   break ;
  case IDI_SYNC_REQ_GET_FEATURES:
   if ( IoAdapter )
   {
    syncReq->GetFeatures.features =
      (unsigned short)IoAdapter->features ;
    return ;
   }
   syncReq->GetFeatures.features = 0 ;
   break ;
        case IDI_SYNC_REQ_PORTDRV_HOOK:
            if ( IoAdapter )
            {
                DBG_TRC(("Xdi:IDI_SYNC_REQ_PORTDRV_HOOK - ignored"))
                return ;
            }
            break;
  }
  if ( IoAdapter )
  {
   return ;
  }
 }
 DBG_TRC(("xdi: Id 0x%x / Req 0x%x / Rc 0x%x", e->Id, e->Req, e->Rc))
 if ( !IoAdapter )
 {
  DBG_FTL(("xdi: uninitialized Adapter used - ignore request"))
  return ;
 }
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
/*
 * assign an entity
 */
 if ( !(e->Id &0x1f) )
 {
  if ( IoAdapter->e_count >= IoAdapter->e_max )
  {
   DBG_FTL(("xdi: all Ids in use (max=%d) --> Req ignored",
            IoAdapter->e_max))
   diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
   return ;
  }
/*
 * find a new free id
 */
  for ( i = 1 ; IoAdapter->e_tbl[i].e ; ++i ) ;
  IoAdapter->e_tbl[i].e = e ;
  IoAdapter->e_count++ ;
  e->No = (byte)i ;
  e->More = 0 ;
  e->RCurrent = 0xff ;
 }
 else
 {
  i = e->No ;
 }
/*
 * if the entity is still busy, ignore the request call
 */
 if ( e->More & XBUSY )
 {
  DBG_FTL(("xdi: Id 0x%x busy --> Req 0x%x ignored", e->Id, e->Req))
  if ( !IoAdapter->trapped && IoAdapter->trapFnc )
  {
   IoAdapter->trapFnc (IoAdapter) ;
      /*
        Firs trap, also notify user if supported
       */
      if (IoAdapter->trapped && IoAdapter->os_trap_nfy_Fnc) {
        (*(IoAdapter->os_trap_nfy_Fnc))(IoAdapter, IoAdapter->ANum);
      }
  }
  diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
  return ;
 }
/*
 * initialize transmit status variables
 */
 e->More |= XBUSY ;
 e->More &= ~XMOREF ;
 e->XCurrent = 0 ;
 e->XOffset = 0 ;
/*
 * queue this entity in the adapter request queue
 */
 IoAdapter->e_tbl[i].next = 0 ;
 if ( IoAdapter->head )
 {
  IoAdapter->e_tbl[IoAdapter->tail].next = i ;
  IoAdapter->tail = i ;
 }
 else
 {
  IoAdapter->head = i ;
  IoAdapter->tail = i ;
 }
/*
 * queue the DPC to process the request
 */
 diva_os_schedule_soft_isr (&IoAdapter->req_soft_isr);
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
}
/* ---------------------------------------------------------------------
  Main DPC routine
   --------------------------------------------------------------------- */
void DIDpcRoutine (struct _diva_os_soft_isr* psoft_isr, void* Context) {
 PISDN_ADAPTER IoAdapter  = (PISDN_ADAPTER)Context ;
 ADAPTER* a        = &IoAdapter->a ;
 diva_os_atomic_t* pin_dpc = &IoAdapter->in_dpc;
 if (diva_os_atomic_increment (pin_dpc) == 1) {
  do {
   if ( IoAdapter->tst_irq (a) )
   {
    if ( !IoAdapter->Unavailable )
     IoAdapter->dpc (a) ;
    IoAdapter->clr_irq (a) ;
   }
   IoAdapter->out (a) ;
  } while (diva_os_atomic_decrement (pin_dpc) > 0);
  /* ----------------------------------------------------------------
    Look for XLOG request (cards with indirect addressing)
    ---------------------------------------------------------------- */
  if (IoAdapter->pcm_pending) {
   struct pc_maint *pcm;
   diva_os_spin_lock_magic_t OldIrql ;
   diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_dpc");
   pcm = (struct pc_maint *)IoAdapter->pcm_data;
   switch (IoAdapter->pcm_pending) {
    case 1: /* ask card for XLOG */
     a->ram_out (a, &IoAdapter->pcm->rc, 0) ;
     a->ram_out (a, &IoAdapter->pcm->req, pcm->req) ;
     IoAdapter->pcm_pending = 2;
     break;
    case 2: /* Try to get XLOG from the card */
     if ((int)(a->ram_in (a, &IoAdapter->pcm->rc))) {
      a->ram_in_buffer (a, IoAdapter->pcm, pcm, sizeof(*pcm)) ;
      IoAdapter->pcm_pending = 3;
     }
     break;
    case 3: /* let XDI recovery XLOG */
     break;
   }
   diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_dpc");
  }
  /* ---------------------------------------------------------------- */
 }
}
/* --------------------------------------------------------------------------
  XLOG interface
  -------------------------------------------------------------------------- */
static void
pcm_req (PISDN_ADAPTER IoAdapter, ENTITY *e)
{
 diva_os_spin_lock_magic_t OldIrql ;
 int              i, rc ;
 ADAPTER         *a = &IoAdapter->a ;
 struct pc_maint *pcm = (struct pc_maint *)&e->Ind ;
/*
 * special handling of I/O based card interface
 * the memory access isn't an atomic operation !
 */
 if ( IoAdapter->Properties.Card == CARD_MAE )
 {
  diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_1");
  IoAdapter->pcm_data = (void *)pcm;
  IoAdapter->pcm_pending = 1;
  diva_os_schedule_soft_isr (&IoAdapter->req_soft_isr);
  diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_1");
  for ( rc = 0, i = (IoAdapter->trapped ? 3000 : 250) ; !rc && (i > 0) ; --i )
  {
   diva_os_sleep (1) ;
   if (IoAdapter->pcm_pending == 3) {
    diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
                 &OldIrql,
                 "data_pcm_3");
    IoAdapter->pcm_pending = 0;
    IoAdapter->pcm_data    = NULL ;
    diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
                 &OldIrql,
                 "data_pcm_3");
    return ;
   }
   diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_pcm_2");
   diva_os_schedule_soft_isr (&IoAdapter->req_soft_isr);
   diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_pcm_2");
  }
  diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_4");
  IoAdapter->pcm_pending = 0;
  IoAdapter->pcm_data    = NULL ;
  diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_4");
  goto Trapped ;
 }
/*
 * memory based shared ram is accessible from different
 * processors without disturbing concurrent processes.
 */
 a->ram_out (a, &IoAdapter->pcm->rc, 0) ;
 a->ram_out (a, &IoAdapter->pcm->req, pcm->req) ;
 for ( i = (IoAdapter->trapped ? 3000 : 250) ; --i > 0 ; )
 {
  diva_os_sleep (1) ;
  rc = (int)(a->ram_in (a, &IoAdapter->pcm->rc)) ;
  if ( rc )
  {
   a->ram_in_buffer (a, IoAdapter->pcm, pcm, sizeof(*pcm)) ;
   return ;
  }
 }
Trapped:
 if ( IoAdapter->trapFnc )
 {
    int trapped = IoAdapter->trapped;
  IoAdapter->trapFnc (IoAdapter) ;
    /*
      Firs trap, also notify user if supported
     */
    if (!trapped && IoAdapter->trapped && IoAdapter->os_trap_nfy_Fnc) {
      (*(IoAdapter->os_trap_nfy_Fnc))(IoAdapter, IoAdapter->ANum);
    }
 }
}
/*------------------------------------------------------------------*/
/* ram access functions for memory mapped cards                     */
/*------------------------------------------------------------------*/
byte mem_in (ADAPTER *a, void *addr)
{
 byte val;
 volatile byte __iomem *Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 val = READ_BYTE(Base + (unsigned long)addr);
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
 return (val);
}
word mem_inw (ADAPTER *a, void *addr)
{
 word val;
 volatile byte __iomem *Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 val = READ_WORD((Base + (unsigned long)addr));
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
 return (val);
}
void mem_in_dw (ADAPTER *a, void *addr, dword* data, int dwords)
{
 volatile byte __iomem * Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 while (dwords--) {
  *data++ = READ_DWORD((Base + (unsigned long)addr));
  addr+=4;
 }
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
void mem_in_buffer (ADAPTER *a, void *addr, void *buffer, word length)
{
 volatile byte __iomem *Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 memcpy_fromio(buffer, (Base + (unsigned long)addr), length);
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
void mem_look_ahead (ADAPTER *a, PBUFFER *RBuffer, ENTITY *e)
{
 PISDN_ADAPTER IoAdapter = (PISDN_ADAPTER)a->io ;
 IoAdapter->RBuffer.length = mem_inw (a, &RBuffer->length) ;
 mem_in_buffer (a, RBuffer->P, IoAdapter->RBuffer.P,
                IoAdapter->RBuffer.length) ;
 e->RBuffer = (DBUFFER *)&IoAdapter->RBuffer ;
}
void mem_out (ADAPTER *a, void *addr, byte data)
{
 volatile byte __iomem *Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 WRITE_BYTE(Base + (unsigned long)addr, data);
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
void mem_outw (ADAPTER *a, void *addr, word data)
{
 volatile byte __iomem * Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 WRITE_WORD((Base + (unsigned long)addr), data);
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
void mem_out_dw (ADAPTER *a, void *addr, const dword* data, int dwords)
{
 volatile byte __iomem * Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 while (dwords--) {
	WRITE_DWORD((Base + (unsigned long)addr), *data);
  	addr+=4;
	data++;
 }
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
void mem_out_buffer (ADAPTER *a, void *addr, void *buffer, word length)
{
 volatile byte __iomem * Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 memcpy_toio((Base + (unsigned long)addr), buffer, length) ;
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
void mem_inc (ADAPTER *a, void *addr)
{
 volatile byte __iomem *Base = DIVA_OS_MEM_ATTACH_RAM((PISDN_ADAPTER)a->io);
 byte  x = READ_BYTE(Base + (unsigned long)addr);
 WRITE_BYTE(Base + (unsigned long)addr, x + 1);
 DIVA_OS_MEM_DETACH_RAM((PISDN_ADAPTER)a->io, Base);
}
/*------------------------------------------------------------------*/
/* ram access functions for io-mapped cards                         */
/*------------------------------------------------------------------*/
byte io_in(ADAPTER * a, void * adr)
{
  byte val;
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  outppw(Port + 4, (word)(unsigned long)adr);
  val = inpp(Port);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
  return(val);
}
word io_inw(ADAPTER * a, void * adr)
{
  word val;
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  outppw(Port + 4, (word)(unsigned long)adr);
  val = inppw(Port);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
  return(val);
}
void io_in_buffer(ADAPTER * a, void * adr, void * buffer, word len)
{
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  byte* P = (byte*)buffer;
  if ((long)adr & 1) {
    outppw(Port+4, (word)(unsigned long)adr);
    *P = inpp(Port);
    P++;
    adr = ((byte *) adr) + 1;
    len--;
    if (!len) {
	DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
	return;
    }
  }
  outppw(Port+4, (word)(unsigned long)adr);
  inppw_buffer (Port, P, len+1);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
}
void io_look_ahead(ADAPTER * a, PBUFFER * RBuffer, ENTITY * e)
{
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  outppw(Port+4, (word)(unsigned long)RBuffer);
  ((PISDN_ADAPTER)a->io)->RBuffer.length = inppw(Port);
  inppw_buffer (Port, ((PISDN_ADAPTER)a->io)->RBuffer.P, ((PISDN_ADAPTER)a->io)->RBuffer.length + 1);
  e->RBuffer = (DBUFFER *) &(((PISDN_ADAPTER)a->io)->RBuffer);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
}
void io_out(ADAPTER * a, void * adr, byte data)
{
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  outppw(Port+4, (word)(unsigned long)adr);
  outpp(Port, data);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
}
void io_outw(ADAPTER * a, void * adr, word data)
{
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  outppw(Port+4, (word)(unsigned long)adr);
  outppw(Port, data);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
}
void io_out_buffer(ADAPTER * a, void * adr, void * buffer, word len)
{
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  byte* P = (byte*)buffer;
  if ((long)adr & 1) {
    outppw(Port+4, (word)(unsigned long)adr);
    outpp(Port, *P);
    P++;
    adr = ((byte *) adr) + 1;
    len--;
    if (!len) {
	DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
	return;
    }
  }
  outppw(Port+4, (word)(unsigned long)adr);
  outppw_buffer (Port, P, len+1);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
}
void io_inc(ADAPTER * a, void * adr)
{
  byte x;
  byte __iomem *Port = DIVA_OS_MEM_ATTACH_PORT((PISDN_ADAPTER)a->io);
  outppw(Port+4, (word)(unsigned long)adr);
  x = inpp(Port);
  outppw(Port+4, (word)(unsigned long)adr);
  outpp(Port, x+1);
  DIVA_OS_MEM_DETACH_PORT((PISDN_ADAPTER)a->io, Port);
}
/*------------------------------------------------------------------*/
/* OS specific functions related to queuing of entities             */
/*------------------------------------------------------------------*/
void free_entity(ADAPTER * a, byte e_no)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_free");
  IoAdapter->e_tbl[e_no].e = NULL;
  IoAdapter->e_count--;
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_free");
}
void assign_queue(ADAPTER * a, byte e_no, word ref)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_assign");
  IoAdapter->e_tbl[e_no].assign_ref = ref;
  IoAdapter->e_tbl[e_no].next = (byte)IoAdapter->assign;
  IoAdapter->assign = e_no;
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_assign");
}
byte get_assign(ADAPTER * a, word ref)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  byte e_no;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
              &irql,
              "data_assign_get");
  for(e_no = (byte)IoAdapter->assign;
      e_no && IoAdapter->e_tbl[e_no].assign_ref!=ref;
      e_no = IoAdapter->e_tbl[e_no].next);
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
              &irql,
              "data_assign_get");
  return e_no;
}
void req_queue(ADAPTER * a, byte e_no)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_q");
  IoAdapter->e_tbl[e_no].next = 0;
  if(IoAdapter->head) {
    IoAdapter->e_tbl[IoAdapter->tail].next = e_no;
    IoAdapter->tail = e_no;
  }
  else {
    IoAdapter->head = e_no;
    IoAdapter->tail = e_no;
  }
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_q");
}
byte look_req(ADAPTER * a)
{
  PISDN_ADAPTER IoAdapter;
  IoAdapter = (PISDN_ADAPTER) a->io;
  return ((byte)IoAdapter->head) ;
}
void next_req(ADAPTER * a)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_next");
  IoAdapter->head = IoAdapter->e_tbl[IoAdapter->head].next;
  if(!IoAdapter->head) IoAdapter->tail = 0;
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_next");
}
/*------------------------------------------------------------------*/
/* memory map functions                                             */
/*------------------------------------------------------------------*/
ENTITY * entity_ptr(ADAPTER * a, byte e_no)
{
  PISDN_ADAPTER IoAdapter;
  IoAdapter = (PISDN_ADAPTER) a->io;
  return (IoAdapter->e_tbl[e_no].e);
}
void * PTR_X(ADAPTER * a, ENTITY * e)
{
  return ((void *) e->X);
}
void * PTR_R(ADAPTER * a, ENTITY * e)
{
  return ((void *) e->R);
}
void * PTR_P(ADAPTER * a, ENTITY * e, void * P)
{
  return P;
}
void CALLBACK(ADAPTER * a, ENTITY * e)
{
 if ( e && e->callback )
  e->callback (e) ;
}
