/* Copyright(c) 2000, Compaq Computer Corporation 
 * Fibre Channel Host Bus Adapter 
 * 64-bit, 66MHz PCI 
 * Originally developed and tested on:
 * (front): [chip] Tachyon TS HPFC-5166A/1.2  L2C1090 ...
 *          SP# P225CXCBFIEL6T, Rev XC
 *          SP# 161290-001, Rev XD
 * (back): Board No. 010008-001 A/W Rev X5, FAB REV X5
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * Written by Don Zimmerman
*/

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>

#define SHUTDOWN_SIGS	(sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM))

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include <scsi/scsi_host.h>   // struct Scsi_Host definition for T handler
#include "cpqfcTSchip.h"
#include "cpqfcTSstructs.h"
#include "cpqfcTStrigger.h"

//#define LOGIN_DBG 1

// REMARKS:
// Since Tachyon chips may be permitted to wait from 500ms up to 2 sec
// to empty an outgoing frame from its FIFO to the Fibre Channel stream,
// we cannot do everything we need to in the interrupt handler.  Specifically,
// every time a link re-init (e.g. LIP) takes place, all SCSI I/O has to be
// suspended until the login sequences have been completed.  Login commands
// are frames just like SCSI commands are frames; they are subject to the same
// timeout issues and delays.  Also, various specs provide up to 2 seconds for
// devices to log back in (i.e. respond with ACC to a login frame), so I/O to
// that device has to be suspended.
// A serious problem here occurs on highly loaded FC-AL systems.  If our FC port
// has a low priority (e.g. high arbitrated loop physical address, alpa), and
// some other device is hogging bandwidth (permissible under FC-AL), we might
// time out thinking the link is hung, when it's simply busy.  Many such
// considerations complicate the design.  Although Tachyon assumes control
// (in silicon) for many link-specific issues, the Linux driver is left with the
// rest, which turns out to be a difficult, time critical chore.

// These "worker" functions will handle things like FC Logins; all
// processes with I/O to our device must wait for the Login to complete
// and (if successful) I/O to resume.  In the event of a malfunctioning or  
// very busy loop, it may take hundreds of millisecs or even seconds to complete
// a frame send.  We don't want to hang up the entire server (and all
// processes which don't depend on Fibre) during this wait.

// The Tachyon chip can have around 30,000 I/O operations ("exchanges")
// open at one time.  However, each exchange must be initiated 
// synchronously (i.e. each of the 30k I/O had to be started one at a
// time by sending a starting frame via Tachyon's outbound que).  

// To accommodate kernel "module" build, this driver limits the exchanges
// to 256, because of the contiguous physical memory limitation of 128M.

// Typical FC Exchanges are opened presuming the FC frames start without errors,
// while Exchange completion is handled in the interrupt handler.  This
// optimizes performance for the "everything's working" case.
// However, when we have FC related errors or hot plugging of FC ports, we pause
// I/O and handle FC-specific tasks in the worker thread.  These FC-specific
// functions will handle things like FC Logins and Aborts.  As the Login sequence
// completes to each and every target, I/O can resume to that target.  

// Our kernel "worker thread" must share the HBA with threads calling 
// "queuecommand".  We define a "BoardLock" semaphore which indicates
// to "queuecommand" that the HBA is unavailable, and Cmnds are added to a
// board lock Q.  When the worker thread finishes with the board, the board
// lock Q commands are completed with status causing immediate retry.
// Typically, the board is locked while Logins are in progress after an
// FC Link Down condition.  When Cmnds are re-queued after board lock, the
// particular Scsi channel/target may or may not have logged back in.  When
// the device is waiting for login, the "prli" flag is clear, in which case
// commands are passed to a Link Down Q.  Whenever the login finally completes,
// the LinkDown Q is completed, again with status causing immediate retry.
// When FC devices are logged in, we build and start FC commands to the
// devices.

// NOTE!! As of May 2000, kernel 2.2.14, the error recovery logic for devices 
// that never log back in (e.g. physically removed) is NOT completely
// understood.  I've still seen instances of system hangs on failed Write 
// commands (possibly from the ext2 layer?) on device removal.  Such special
// cases need to be evaluated from a system/application view - e.g., how
// exactly does the system want me to complete commands when the device is
// physically removed??

// local functions

static void SetLoginFields(
  PFC_LOGGEDIN_PORT pLoggedInPort,
  TachFCHDR_GCMND* fchs,
  BOOLEAN PDisc,
  BOOLEAN Originator);

static void AnalyzeIncomingFrame( 
       CPQFCHBA *cpqfcHBAdata,
       ULONG QNdx );

static void SendLogins( CPQFCHBA *cpqfcHBAdata, __u32 *FabricPortIds );

static int verify_PLOGI( PTACHYON fcChip,
      TachFCHDR_GCMND* fchs, ULONG* reject_explain);
static int verify_PRLI( TachFCHDR_GCMND* fchs, ULONG* reject_explain);

static void LoadWWN( PTACHYON fcChip, UCHAR* dest, UCHAR type);
static void BuildLinkServicePayload( 
              PTACHYON fcChip, ULONG type, void* payload);

static void UnblockScsiDevice( struct Scsi_Host *HostAdapter, 
        PFC_LOGGEDIN_PORT pLoggedInPort);

static void cpqfcTSCheckandSnoopFCP( PTACHYON fcChip, ULONG x_ID);

static void CompleteBoardLockCmnd( CPQFCHBA *cpqfcHBAdata);

static void RevalidateSEST( struct Scsi_Host *HostAdapter, 
		        PFC_LOGGEDIN_PORT pLoggedInPort);

static void IssueReportLunsCommand( 
              CPQFCHBA* cpqfcHBAdata, 
	      TachFCHDR_GCMND* fchs);

// (see scsi_error.c comments on kernel task creation)

void cpqfcTSWorkerThread( void *host)
{
  struct Scsi_Host *HostAdapter = (struct Scsi_Host*)host;
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata; 
#ifdef PCI_KERNEL_TRACE
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
#endif
  DECLARE_MUTEX_LOCKED(fcQueReady);
  DECLARE_MUTEX_LOCKED(fcTYOBcomplete); 
  DECLARE_MUTEX_LOCKED(TachFrozen);  
  DECLARE_MUTEX_LOCKED(BoardLock);  

  ENTER("WorkerThread");

  lock_kernel();
  daemonize("cpqfcTS_wt_%d", HostAdapter->host_no);
  siginitsetinv(&current->blocked, SHUTDOWN_SIGS);


  cpqfcHBAdata->fcQueReady = &fcQueReady;  // primary wait point
  cpqfcHBAdata->TYOBcomplete = &fcTYOBcomplete;
  cpqfcHBAdata->TachFrozen = &TachFrozen;
    
 
  cpqfcHBAdata->worker_thread = current;
  
  unlock_kernel();

  if( cpqfcHBAdata->notify_wt != NULL )
    up( cpqfcHBAdata->notify_wt); // OK to continue

  while(1)
  {
    unsigned long flags;

    down_interruptible( &fcQueReady);  // wait for something to do

    if (signal_pending(current) )
      break;
    
    PCI_TRACE( 0x90)
    // first, take the IO lock so the SCSI upper layers can't call
    // into our _quecommand function (this also disables INTs)
    spin_lock_irqsave( HostAdapter->host_lock, flags); // STOP _que function
    PCI_TRACE( 0x90)
         
    CPQ_SPINLOCK_HBA( cpqfcHBAdata)
    // next, set this pointer to indicate to the _quecommand function
    // that the board is in use, so it should que the command and 
    // immediately return (we don't actually require the semaphore function
    // in this driver rev)

    cpqfcHBAdata->BoardLock = &BoardLock;

    PCI_TRACE( 0x90)

    // release the IO lock (and re-enable interrupts)
    spin_unlock_irqrestore( HostAdapter->host_lock, flags);

    // disable OUR HBA interrupt (keep them off as much as possible
    // during error recovery)
    disable_irq( cpqfcHBAdata->HostAdapter->irq);

    // OK, let's process the Fibre Channel Link Q and do the work
    cpqfcTS_WorkTask( HostAdapter);

    // hopefully, no more "work" to do;
    // re-enable our INTs for "normal" completion processing
    enable_irq( cpqfcHBAdata->HostAdapter->irq);
 

    cpqfcHBAdata->BoardLock = NULL; // allow commands to be queued
    CPQ_SPINUNLOCK_HBA( cpqfcHBAdata)


    // Now, complete any Cmnd we Q'd up while BoardLock was held

    CompleteBoardLockCmnd( cpqfcHBAdata);
  

  }
  // hopefully, the signal was for our module exit...
  if( cpqfcHBAdata->notify_wt != NULL )
    up( cpqfcHBAdata->notify_wt); // yep, we're outta here
}


// Freeze Tachyon routine.
// If Tachyon is already frozen, return FALSE
// If Tachyon is not frozen, call freeze function, return TRUE
//
static BOOLEAN FreezeTach( CPQFCHBA *cpqfcHBAdata)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  BOOLEAN FrozeTach = FALSE;
  // It's possible that the chip is already frozen; if so,
  // "Freezing" again will NOT! generate another Freeze
  // Completion Message.

  if( (fcChip->Registers.TYstatus.value & 0x70000) != 0x70000)
  {  // (need to freeze...)
    fcChip->FreezeTachyon( fcChip, 2);  // both ERQ and FCP assists

    // 2. Get Tach freeze confirmation
    // (synchronize SEST manipulation with Freeze Completion Message)
    // we need INTs on so semaphore can be set.	
    enable_irq( cpqfcHBAdata->HostAdapter->irq); // only way to get Semaphore
    down_interruptible( cpqfcHBAdata->TachFrozen); // wait for INT handler sem.
    // can we TIMEOUT semaphore wait?? TBD
    disable_irq( cpqfcHBAdata->HostAdapter->irq); 

    FrozeTach = TRUE;
  }  // (else, already frozen)
 
  return FrozeTach;
}  




// This is the kernel worker thread task, which processes FC
// tasks which were queued by the Interrupt handler or by
// other WorkTask functions.

#define DBG 1
//#undef DBG
void cpqfcTS_WorkTask( struct Scsi_Host *HostAdapter)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG QconsumerNdx;
  LONG ExchangeID;
  ULONG ulStatus=0;
  TachFCHDR_GCMND fchs;
  PFC_LINK_QUE fcLQ = cpqfcHBAdata->fcLQ;

  ENTER("WorkTask");

  // copy current index to work on
  QconsumerNdx = fcLQ->consumer;

  PCI_TRACEO( fcLQ->Qitem[QconsumerNdx].Type, 0x90)
  

  // NOTE: when this switch completes, we will "consume" the Que item
//  printk("Que type %Xh\n", fcLQ->Qitem[QconsumerNdx].Type);
  switch( fcLQ->Qitem[QconsumerNdx].Type )
  {
      // incoming frame - link service (ACC, UNSOL REQ, etc.)
      // or FCP-SCSI command
    case SFQ_UNKNOWN:  
      AnalyzeIncomingFrame( cpqfcHBAdata, QconsumerNdx );

      break;
  
    
    
    case EXCHANGE_QUEUED:  // an Exchange (i.e. FCP-SCSI) was previously
                           // Queued because the link was down.  The  
                           // heartbeat timer detected it and Queued it here.
                           // We attempt to start it again, and if
                           // successful we clear the EXCHANGE_Q flag.
                           // If the link doesn't come up, the Exchange
                           // will eventually time-out.

      ExchangeID = (LONG)  // x_ID copied from DPC timeout function
                   fcLQ->Qitem[QconsumerNdx].ulBuff[0];

      // It's possible that a Q'd exchange could have already
      // been started by other logic (e.g. ABTS process)
      // Don't start if already started (Q'd flag clear)

      if( Exchanges->fcExchange[ExchangeID].status & EXCHANGE_QUEUED )
      {
//        printk(" *Start Q'd x_ID %Xh: type %Xh ", 
//          ExchangeID, Exchanges->fcExchange[ExchangeID].type);
      
        ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, ExchangeID);
        if( !ulStatus )
        {
//          printk("success* ");
        }	
        else
        {
#ifdef DBG
      
          if( ulStatus == EXCHANGE_QUEUED)
            printk("Queued* ");
          else
            printk("failed* ");
		
#endif
	} 
      }
      break;


    case LINKDOWN:  
      // (lots of things already done in INT handler) future here?
      break;
    
    
    case LINKACTIVE:   // Tachyon set the Lup bit in FM status
                       // NOTE: some misbehaving FC ports (like Tach2.1)
                       // can re-LIP immediately after a LIP completes.
      
      // if "initiator", need to verify LOGs with ports
//      printk("\n*LNKUP* ");

      if( fcChip->Options.initiator )
        SendLogins( cpqfcHBAdata, NULL ); // PLOGI or PDISC, based on fcPort data
                  // if SendLogins successfully completes, PortDiscDone
                  // will be set.
      
      
      // If SendLogins was successful, then we expect to get incoming
      // ACCepts or REJECTs, which are handled below.

      break;

    // LinkService and Fabric request/reply processing
    case ELS_FDISC:      // need to send Fabric Discovery (Login)
    case ELS_FLOGI:      // need to send Fabric Login
    case ELS_SCR:        // need to send State Change Registration
    case FCS_NSR:        // need to send Name Service Request
    case ELS_PLOGI:      // need to send PLOGI
    case ELS_ACC:        // send generic ACCept
    case ELS_PLOGI_ACC:  // need to send ELS ACCept frame to recv'd PLOGI
    case ELS_PRLI_ACC:   // need to send ELS ACCept frame to recv'd PRLI
    case ELS_LOGO:      // need to send ELS LOGO (logout)
    case ELS_LOGO_ACC:  // need to send ELS ACCept frame to recv'd PLOGI
    case ELS_RJT:         // ReJecT reply
    case ELS_PRLI:       // need to send ELS PRLI
 
    
//      printk(" *ELS %Xh* ", fcLQ->Qitem[QconsumerNdx].Type);
      // if PortDiscDone is not set, it means the SendLogins routine
      // failed to complete -- assume that LDn occurred, so login frames
      // are invalid
      if( !cpqfcHBAdata->PortDiscDone) // cleared by LDn
      {
        printk("Discard Q'd ELS login frame\n");
        break;  
      }

      ulStatus = cpqfcTSBuildExchange(
          cpqfcHBAdata,
          fcLQ->Qitem[QconsumerNdx].Type, // e.g. PLOGI
          (TachFCHDR_GCMND*)
            fcLQ->Qitem[QconsumerNdx].ulBuff, // incoming fchs
          NULL,         // no data (no scatter/gather list)
          &ExchangeID );// fcController->fcExchanges index, -1 if failed

      if( !ulStatus ) // Exchange setup?
      {
        ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, ExchangeID );
        if( !ulStatus )
        {
          // submitted to Tach's Outbound Que (ERQ PI incremented)
          // waited for completion for ELS type (Login frames issued
          // synchronously)
        }
        else
          // check reason for Exchange not being started - we might
          // want to Queue and start later, or fail with error
        {

        }
      }

      else   // Xchange setup failed...
        printk(" cpqfcTSBuildExchange failed: %Xh\n", ulStatus );

      break;

    case SCSI_REPORT_LUNS:
      // pass the incoming frame (actually, it's a PRLI frame)
      // so we can send REPORT_LUNS, in order to determine VSA/PDU
      // FCP-SCSI Lun address mode
      IssueReportLunsCommand( cpqfcHBAdata, (TachFCHDR_GCMND*)
            fcLQ->Qitem[QconsumerNdx].ulBuff); 

      break;
      



    case BLS_ABTS:       // need to ABORT one or more exchanges
    {
      LONG x_ID = fcLQ->Qitem[QconsumerNdx].ulBuff[0];
      BOOLEAN FrozeTach = FALSE;   
     
      if ( x_ID >= TACH_SEST_LEN )  // (in)sanity check
      {
//	printk( " cpqfcTS ERROR! BOGUS x_ID %Xh", x_ID);
	break;
      }


      if( Exchanges->fcExchange[ x_ID].Cmnd == NULL ) // should be RARE
      {
//	printk(" ABTS %Xh Scsi Cmnd null! ", x_ID);
	
       break;  // nothing to abort!
      }

//#define ABTS_DBG
#ifdef ABTS_DBG
      printk("INV SEST[%X] ", x_ID); 
      if( Exchanges->fcExchange[x_ID].status & FC2_TIMEOUT)
      {
        printk("FC2TO");
      }
      if( Exchanges->fcExchange[x_ID].status & INITIATOR_ABORT)
      {
        printk("IA");
      }
      if( Exchanges->fcExchange[x_ID].status & PORTID_CHANGED)
      {
        printk("PORTID");
      }
      if( Exchanges->fcExchange[x_ID].status & DEVICE_REMOVED)
      {
        printk("DEVRM");
      }
      if( Exchanges->fcExchange[x_ID].status & LINKFAIL_TX)
      {
        printk("LKF");
      }
      if( Exchanges->fcExchange[x_ID].status & FRAME_TO)
      {
        printk("FRMTO");
      }
      if( Exchanges->fcExchange[x_ID].status & ABORTSEQ_NOTIFY)
      {
        printk("ABSQ");
      }
      if( Exchanges->fcExchange[x_ID].status & SFQ_FRAME)
      {
        printk("SFQFR");
      }

      if( Exchanges->fcExchange[ x_ID].type == 0x2000)
        printk(" WR");
      else if( Exchanges->fcExchange[ x_ID].type == 0x3000)
        printk(" RD");
      else if( Exchanges->fcExchange[ x_ID].type == 0x10)
        printk(" ABTS");
      else
        printk(" %Xh", Exchanges->fcExchange[ x_ID].type); 

      if( !(Exchanges->fcExchange[x_ID].status & INITIATOR_ABORT))
      {
	printk(" Cmd %p, ", 
          Exchanges->fcExchange[ x_ID].Cmnd);

        printk(" brd/chn/trg/lun %d/%d/%d/%d port_id %06X\n", 
          cpqfcHBAdata->HBAnum,
          Exchanges->fcExchange[ x_ID].Cmnd->channel,
          Exchanges->fcExchange[ x_ID].Cmnd->target,
          Exchanges->fcExchange[ x_ID].Cmnd->lun,
          Exchanges->fcExchange[ x_ID].fchs.d_id & 0xFFFFFF);
      }
      else  // assume that Cmnd ptr is invalid on _abort()
      {
	printk(" Cmd ptr invalid\n");
      }
     
#endif      

      
      // Steps to ABORT a SEST exchange:
      // 1. Freeze TL SCSI assists & ERQ (everything)
      // 2. Receive FROZEN inbound CM (must succeed!)
      // 3. Invalidate x_ID SEST entry 
      // 4. Resume TL SCSI assists & ERQ (everything)
      // 5. Build/start on exchange - change "type" to BLS_ABTS,
      //    timeout to X sec (RA_TOV from PLDA is actually 0)
      // 6. Set Exchange Q'd status if ABTS cannot be started,
      //    or simply complete Exchange in "Terminate" condition

  PCI_TRACEO( x_ID, 0xB4)
      
      // 1 & 2 . Freeze Tach & get confirmation of freeze
      FrozeTach = FreezeTach( cpqfcHBAdata);

      // 3. OK, Tachyon is frozen, so we can invalidate SEST exchange.
      // FC2_TIMEOUT means we are originating the abort, while
      // TARGET_ABORT means we are ACCepting an abort.
      // LINKFAIL_TX, ABORTSEQ_NOFITY, INV_ENTRY or FRAME_TO are 
      // all from Tachyon:
      // Exchange was corrupted by LDn or other FC physical failure
      // INITIATOR_ABORT means the upper layer driver/application
      // requested the abort.


	  
      // clear bit 31 (VALid), to invalidate & take control from TL
      fcChip->SEST->u[ x_ID].IWE.Hdr_Len &= 0x7FFFFFFF;


      // examine and Tach's "Linked List" for IWEs that 
      // received (nearly) simultaneous transfer ready (XRDY) 
      // repair linked list if necessary (TBD!)
      // (If we ignore the "Linked List", we will time out
      // WRITE commands where we received the FCP-SCSI XFRDY
      // frame (because Tachyon didn't processes it).  Linked List
      // management should be done as an optimization.

//       readl( fcChip->Registers.ReMapMemBase+TL_MEM_SEST_LINKED_LIST ));


      

      // 4. Resume all Tachlite functions (for other open Exchanges)
      // as quickly as possible to allow other exchanges to other ports
      // to resume.  Freezing Tachyon may cause cascading errors, because
      // any received SEST frame cannot be processed by the SEST.
      // Don't "unfreeze" unless Link is operational
      if( FrozeTach )  // did we just freeze it (above)?
        fcChip->UnFreezeTachyon( fcChip, 2);  // both ERQ and FCP assists
      

  PCI_TRACEO( x_ID, 0xB4)

      // Note there is no confirmation that the chip is "unfrozen".  Also,
      // if the Link is down when unfreeze is called, it has no effect.
      // Chip will unfreeze when the Link is back up.

      // 5. Now send out Abort commands if possible
      // Some Aborts can't be "sent" (Port_id changed or gone);
      // if the device is gone, there is no port_id to send the ABTS to.

      if( !(Exchanges->fcExchange[ x_ID].status & PORTID_CHANGED)
			  &&
          !(Exchanges->fcExchange[ x_ID].status & DEVICE_REMOVED) )
      {
        Exchanges->fcExchange[ x_ID].type = BLS_ABTS;
        fchs.s_id = Exchanges->fcExchange[ x_ID].fchs.d_id;
        ulStatus = cpqfcTSBuildExchange(
          cpqfcHBAdata,
          BLS_ABTS,
          &fchs,        // (uses only s_id)
          NULL,         // (no scatter/gather list for ABTS)
          &x_ID );// ABTS on this Exchange ID

        if( !ulStatus ) // Exchange setup build OK?
        {

            // ABTS may be needed because an Exchange was corrupted
            // by a Link disruption.  If the Link is UP, we can
	    // presume that this ABTS can start immediately; otherwise,
	    // set Que'd status so the Login functions
            // can restart it when the FC physical Link is restored
          if( ((fcChip->Registers.FMstatus.value &0xF0) &0x80)) // loop init?
          {			    
//                printk(" *set Q status x_ID %Xh on LDn* ", x_ID);
                Exchanges->fcExchange[ x_ID].status |= EXCHANGE_QUEUED;
          }

          else  // what FC device (port_id) does the Cmd belong to?
          {
            PFC_LOGGEDIN_PORT pLoggedInPort = 
              Exchanges->fcExchange[ x_ID].pLoggedInPort;
            
            // if Port is logged in, we might start the abort.
	
            if( (pLoggedInPort != NULL) 
			      &&
                (pLoggedInPort->prli == TRUE) ) 
            {
              // it's possible that an Exchange has already been Queued
              // to start after Login completes.  Check and don't
	      // start it (again) here if Q'd status set
//	    printk(" ABTS xchg %Xh ", x_ID);            
 	      if( Exchanges->fcExchange[x_ID].status & EXCHANGE_QUEUED)
	      {
//		    printk("already Q'd ");
	      }
	      else
	      {
//	            printk("starting ");
		
                fcChip->fcStats.FC2aborted++; 
                ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, x_ID );
                if( !ulStatus )
                {
                    // OK
                    // submitted to Tach's Outbound Que (ERQ PI incremented)
                }
                else
                {
/*                   printk("ABTS exchange start failed -status %Xh, x_ID %Xh ",
                        ulStatus, x_ID);
*/
                } 
	      }
	    }
	    else
	    {
/*         	  printk(" ABTS NOT starting xchg %Xh, %p ",
			       x_ID, pLoggedInPort);
	          if( pLoggedInPort )
	            printk("prli %d ", pLoggedInPort->prli);
*/
	    }		
 	  }
        }
        else  // what the #@!
        {  // how do we fail to build an Exchange for ABTS??
              printk("ABTS exchange build failed -status %Xh, x_ID %Xh\n",
                ulStatus, x_ID);
        }
      }
      else   // abort without ABTS -- just complete exchange/Cmnd to Linux
      {
//            printk(" *Terminating x_ID %Xh on %Xh* ", 
//		    x_ID, Exchanges->fcExchange[x_ID].status);
        cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, x_ID);

      }
    } // end of ABTS case
      break;



    case BLS_ABTS_ACC:   // need to ACCept one ABTS
                         // (NOTE! this code not updated for Linux yet..)
      

      printk(" *ABTS_ACC* ");
      // 1. Freeze TL

      fcChip->FreezeTachyon( fcChip, 2);  // both ERQ and FCP assists

      memcpy(  // copy the incoming ABTS frame
        &fchs,
        fcLQ->Qitem[QconsumerNdx].ulBuff, // incoming fchs
        sizeof( fchs));

      // 3. OK, Tachyon is frozen so we can invalidate SEST entry 
      // (if necessary)
      // Status FC2_TIMEOUT means we are originating the abort, while
      // TARGET_ABORT means we are ACCepting an abort
      
      ExchangeID = fchs.ox_rx_id & 0x7FFF; // RX_ID for exchange
//      printk("ABTS ACC for Target ExchangeID %Xh\n", ExchangeID);


      // sanity check on received ExchangeID
      if( Exchanges->fcExchange[ ExchangeID].status == TARGET_ABORT )
      {
          // clear bit 31 (VALid), to invalidate & take control from TL
//          printk("Invalidating SEST exchange %Xh\n", ExchangeID);
          fcChip->SEST->u[ ExchangeID].IWE.Hdr_Len &= 0x7FFFFFFF;
      }


      // 4. Resume all Tachlite functions (for other open Exchanges)
      // as quickly as possible to allow other exchanges to other ports
      // to resume.  Freezing Tachyon for too long may royally screw
      // up everything!
      fcChip->UnFreezeTachyon( fcChip, 2);  // both ERQ and FCP assists
      
      // Note there is no confirmation that the chip is "unfrozen".  Also,
      // if the Link is down when unfreeze is called, it has no effect.
      // Chip will unfreeze when the Link is back up.

      // 5. Now send out Abort ACC reply for this exchange
      Exchanges->fcExchange[ ExchangeID].type = BLS_ABTS_ACC;
      
      fchs.s_id = Exchanges->fcExchange[ ExchangeID].fchs.d_id;
      ulStatus = cpqfcTSBuildExchange(
            cpqfcHBAdata,
            BLS_ABTS_ACC,
            &fchs,
            NULL,         // no data (no scatter/gather list)
            &ExchangeID );// fcController->fcExchanges index, -1 if failed

      if( !ulStatus ) // Exchange setup?
      {
        ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, ExchangeID );
        if( !ulStatus )
        {
          // submitted to Tach's Outbound Que (ERQ PI incremented)
          // waited for completion for ELS type (Login frames issued
          // synchronously)
        }
        else
          // check reason for Exchange not being started - we might
          // want to Queue and start later, or fail with error
        {

        } 
      }
      break;


    case BLS_ABTS_RJT:   // need to ReJecT one ABTS; reject implies the
                         // exchange doesn't exist in the TARGET context.
                         // ExchangeID has to come from LinkService space.

      printk(" *ABTS_RJT* ");
      ulStatus = cpqfcTSBuildExchange(
            cpqfcHBAdata,
            BLS_ABTS_RJT,
            (TachFCHDR_GCMND*)
              fcLQ->Qitem[QconsumerNdx].ulBuff, // incoming fchs
            NULL,         // no data (no scatter/gather list)
            &ExchangeID );// fcController->fcExchanges index, -1 if failed

      if( !ulStatus ) // Exchange setup OK?
      {
        ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, ExchangeID );
        // If it fails, we aren't required to retry.
      }
      if( ulStatus )
      {
        printk("Failed to send BLS_RJT for ABTS, X_ID %Xh\n", ExchangeID);
      }
      else
      {
        printk("Sent BLS_RJT for ABTS, X_ID %Xh\n", ExchangeID);
      
      }

      break;



    default:
      break;
  }                   // end switch
//doNothing:
    // done with this item - now set the NEXT index

  if( QconsumerNdx+1 >= FC_LINKQ_DEPTH ) // rollover test
  {
    fcLQ->consumer = 0;
  }
  else
  { 
    fcLQ->consumer++;
  }

  PCI_TRACEO( fcLQ->Qitem[QconsumerNdx].Type, 0x94)

  LEAVE("WorkTask");
  return;
}




// When Tachyon reports link down, bad al_pa, or Link Service (e.g. Login)
// commands come in, post to the LinkQ so that action can be taken outside the
// interrupt handler.  
// This circular Q works like Tachyon's que - the producer points to the next
// (unused) entry.  Called by Interrupt handler, WorkerThread, Timer
// sputlinkq
void cpqfcTSPutLinkQue( CPQFCHBA *cpqfcHBAdata,
  int Type, 
  void *QueContent)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
//  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  PFC_LINK_QUE fcLQ = cpqfcHBAdata->fcLQ;
  ULONG ndx;
  
  ENTER("cpqfcTSPutLinkQ");

  ndx = fcLQ->producer;
  
  ndx += 1;  // test for Que full


  
  if( ndx >= FC_LINKQ_DEPTH ) // rollover test
    ndx = 0;

  if( ndx == fcLQ->consumer )   // QUE full test
  {
                       // QUE was full! lost LK command (fatal to logic)
    fcChip->fcStats.lnkQueFull++;

    printk("*LinkQ Full!*");
    TriggerHBA( fcChip->Registers.ReMapMemBase, 1);
/*
    {
      int i;
      printk("LinkQ PI %d, CI %d\n", fcLQ->producer,
        fcLQ->consumer);
		      
      for( i=0; i< FC_LINKQ_DEPTH; )
      {
	printk(" [%d]%Xh ", i, fcLQ->Qitem[i].Type);
	if( (++i %8) == 0) printk("\n");
      }
  
    }
*/    
    printk( "cpqfcTS: WARNING!! PutLinkQue - FULL!\n"); // we're hung
  }
  else                        // QUE next element
  {
    // Prevent certain multiple (back-to-back) requests.
    // This is important in that we don't want to issue multiple
    // ABTS for the same Exchange, or do multiple FM inits, etc.
    // We can never be sure of the timing of events reported to
    // us by Tach's IMQ, which can depend on system/bus speeds,
    // FC physical link circumstances, etc.
     
    if( (fcLQ->producer != fcLQ->consumer)
	    && 
        (Type == FMINIT)  )
    {
      LONG lastNdx;  // compute previous producer index
      if( fcLQ->producer)
        lastNdx = fcLQ->producer- 1;
      else
	lastNdx = FC_LINKQ_DEPTH-1;


      if( fcLQ->Qitem[lastNdx].Type == FMINIT)
      {
//        printk(" *skip FMINIT Q post* ");
//        goto DoneWithPutQ;
      }

    }

    // OK, add the Q'd item...
    
    fcLQ->Qitem[fcLQ->producer].Type = Type;
   
    memcpy(
        fcLQ->Qitem[fcLQ->producer].ulBuff,
        QueContent, 
        sizeof(fcLQ->Qitem[fcLQ->producer].ulBuff));

    fcLQ->producer = ndx;  // increment Que producer

    // set semaphore to wake up Kernel (worker) thread
    // 
    up( cpqfcHBAdata->fcQueReady );
  }

//DoneWithPutQ:

  LEAVE("cpqfcTSPutLinkQ");
}




// reset device ext FC link Q
void cpqfcTSLinkQReset( CPQFCHBA *cpqfcHBAdata)
   
{
  PFC_LINK_QUE fcLQ = cpqfcHBAdata->fcLQ;
  fcLQ->producer = 0;
  fcLQ->consumer = 0;

}





// When Tachyon gets an unassisted FCP-SCSI frame, post here so
// an arbitrary context thread (e.g. IOCTL loopback test function)
// can process it.

// (NOTE: Not revised for Linux)
// This Q works like Tachyon's que - the producer points to the next
// (unused) entry.
void cpqfcTSPutScsiQue( CPQFCHBA *cpqfcHBAdata,
  int Type, 
  void *QueContent)
{
//  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata;
//  PTACHYON fcChip = &cpqfcHBAdata->fcChip;

//  ULONG ndx;

//  ULONG *pExchangeID;
//  LONG ExchangeID;

/*
  KeAcquireSpinLockAtDpcLevel( &pDevExt->fcScsiQueLock);
  ndx = pDevExt->fcScsiQue.producer + 1;  // test for Que full

  if( ndx >= FC_SCSIQ_DEPTH ) // rollover test
    ndx = 0;

  if( ndx == pDevExt->fcScsiQue.consumer )   // QUE full test
  {
                       // QUE was full! lost LK command (fatal to logic)
    fcChip->fcStats.ScsiQueFull++;
#ifdef DBG
    printk( "fcPutScsiQue - FULL!\n");
#endif

  }
  else                        // QUE next element
  {
    pDevExt->fcScsiQue.Qitem[pDevExt->fcScsiQue.producer].Type = Type;
    
    if( Type == FCP_RSP )
    {
      // this TL inbound message type means that a TL SEST exchange has
      // copied an FCP response frame into a buffer pointed to by the SEST
      // entry.  That buffer is allocated in the SEST structure at ->RspHDR.
      // Copy the RspHDR for use by the Que handler.
      pExchangeID = (ULONG *)QueContent;
      
      memcpy(
	      pDevExt->fcScsiQue.Qitem[pDevExt->fcScsiQue.producer].ulBuff,
        &fcChip->SEST->RspHDR[ *pExchangeID ],
	      sizeof(pDevExt->fcScsiQue.Qitem[0].ulBuff)); // (any element for size)
      
    }
    else
    {
      memcpy(
	      pDevExt->fcScsiQue.Qitem[pDevExt->fcScsiQue.producer].ulBuff,
        QueContent, 
	      sizeof(pDevExt->fcScsiQue.Qitem[pDevExt->fcScsiQue.producer].ulBuff));
    }
      
    pDevExt->fcScsiQue.producer = ndx;  // increment Que


    KeSetEvent( &pDevExt->TYIBscsi,  // signal any waiting thread
       0,                    // no priority boost
		   FALSE );              // no waiting later for this event
  }
  KeReleaseSpinLockFromDpcLevel( &pDevExt->fcScsiQueLock);
*/
}







static void ProcessELS_Request( CPQFCHBA*,TachFCHDR_GCMND*);

static void ProcessELS_Reply( CPQFCHBA*,TachFCHDR_GCMND*);

static void ProcessFCS_Reply( CPQFCHBA*,TachFCHDR_GCMND*);

void cpqfcTSImplicitLogout( CPQFCHBA* cpqfcHBAdata,
		PFC_LOGGEDIN_PORT pFcPort)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;

  if( pFcPort->port_id != 0xFFFC01 ) // don't care about Fabric
  {
    fcChip->fcStats.logouts++;
    printk("cpqfcTS: Implicit logout of WWN %08X%08X, port_id %06X\n", 
        (ULONG)pFcPort->u.liWWN,
        (ULONG)(pFcPort->u.liWWN >>32),
	pFcPort->port_id);

  // Terminate I/O with this (Linux) Scsi target
    cpqfcTSTerminateExchange( cpqfcHBAdata, 
                            &pFcPort->ScsiNexus,
	                    DEVICE_REMOVED);
  }
			
  // Do an "implicit logout" - we can't really Logout the device
  // (i.e. with LOGOut Request) because of port_id confusion
  // (i.e. the Other port has no port_id).
  // A new login for that WWN will have to re-write port_id (0 invalid)
  pFcPort->port_id = 0;  // invalid!
  pFcPort->pdisc = FALSE;
  pFcPort->prli = FALSE;
  pFcPort->plogi = FALSE;
  pFcPort->flogi = FALSE;
  pFcPort->LOGO_timer = 0;
  pFcPort->device_blocked = TRUE; // block Scsi Requests
  pFcPort->ScsiNexus.VolumeSetAddressing=0;	
}

  
// On FC-AL, there is a chance that a previously known device can
// be quietly removed (e.g. with non-managed hub), 
// while a NEW device (with different WWN) took the same alpa or
// even 24-bit port_id.  This chance is unlikely but we must always
// check for it.
static void TestDuplicatePortId( CPQFCHBA* cpqfcHBAdata,
		PFC_LOGGEDIN_PORT pLoggedInPort)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  // set "other port" at beginning of fcPorts list
  PFC_LOGGEDIN_PORT pOtherPortWithPortId = fcChip->fcPorts.pNextPort;
  while( pOtherPortWithPortId ) 
  {
    if( (pOtherPortWithPortId->port_id == 
         pLoggedInPort->port_id) 
		    &&
         (pOtherPortWithPortId != pLoggedInPort) )
    {
      // trouble!  (Implicitly) Log the other guy out
      printk(" *port_id %Xh is duplicated!* ", 
        pOtherPortWithPortId->port_id);
      cpqfcTSImplicitLogout( cpqfcHBAdata, pOtherPortWithPortId); 
   }
    pOtherPortWithPortId = pOtherPortWithPortId->pNextPort;
  }
}






// Dynamic Memory Allocation for newly discovered FC Ports.
// For simplicity, maintain fcPorts structs for ALL
// for discovered devices, including those we never do I/O with
// (e.g. Fabric addresses)

static PFC_LOGGEDIN_PORT CreateFcPort( 
	  CPQFCHBA* cpqfcHBAdata, 
	  PFC_LOGGEDIN_PORT pLastLoggedInPort, 
	  TachFCHDR_GCMND* fchs,
	  LOGIN_PAYLOAD* plogi)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  PFC_LOGGEDIN_PORT pNextLoggedInPort = NULL;
  int i;


  printk("cpqfcTS: New FC port %06Xh WWN: ", fchs->s_id);
  for( i=3; i>=0; i--)   // copy the LOGIN port's WWN
    printk("%02X", plogi->port_name[i]);
  for( i=7; i>3; i--)   // copy the LOGIN port's WWN
    printk("%02X", plogi->port_name[i]);


  // allocate mem for new port
  // (these are small and rare allocations...)
  pNextLoggedInPort = kmalloc( sizeof( FC_LOGGEDIN_PORT), GFP_ATOMIC );

    
  // allocation succeeded?  Fill out NEW PORT
  if( pNextLoggedInPort )
  {    
                              // clear out any garbage (sometimes exists)
    memset( pNextLoggedInPort, 0, sizeof( FC_LOGGEDIN_PORT));


    // If we login to a Fabric, we don't want to treat it
    // as a SCSI device...
    if( (fchs->s_id & 0xFFF000) != 0xFFF000)
    {
      int i;
      
      // create a unique "virtual" SCSI Nexus (for now, just a
      // new target ID) -- we will update channel/target on REPORT_LUNS
      // special case for very first SCSI target...
      if( cpqfcHBAdata->HostAdapter->max_id == 0)
      {
        pNextLoggedInPort->ScsiNexus.target = 0;
        fcChip->fcPorts.ScsiNexus.target = -1; // don't use "stub"
      }
      else
      {
        pNextLoggedInPort->ScsiNexus.target =
          cpqfcHBAdata->HostAdapter->max_id;
      }

      // initialize the lun[] Nexus struct for lun masking      
      for( i=0; i< CPQFCTS_MAX_LUN; i++)
        pNextLoggedInPort->ScsiNexus.lun[i] = 0xFF; // init to NOT USED
      
      pNextLoggedInPort->ScsiNexus.channel = 0; // cpqfcTS has 1 FC port
      
      printk(" SCSI Chan/Trgt %d/%d", 
          pNextLoggedInPort->ScsiNexus.channel,
          pNextLoggedInPort->ScsiNexus.target);
 
      // tell Scsi layers about the new target...
      cpqfcHBAdata->HostAdapter->max_id++; 
//    printk("HostAdapter->max_id = %d\n",
//      cpqfcHBAdata->HostAdapter->max_id);		    
    }                          
    else
    {
      // device is NOT SCSI (in case of Fabric)
      pNextLoggedInPort->ScsiNexus.target = -1;  // invalid
    }

	  // create forward link to new port
    pLastLoggedInPort->pNextPort = pNextLoggedInPort;
    printk("\n");

  }     
  return pNextLoggedInPort;  // NULL on allocation failure
}   // end NEW PORT (WWN) logic



// For certain cases, we want to terminate exchanges without
// sending ABTS to the device.  Examples include when an FC
// device changed it's port_id after Loop re-init, or when
// the device sent us a logout.  In the case of changed port_id,
// we want to complete the command and return SOFT_ERROR to
// force a re-try.  In the case of LOGOut, we might return
// BAD_TARGET if the device is really gone.
// Since we must ensure that Tachyon is not operating on the
// exchange, we have to freeze the chip
// sterminateex
void cpqfcTSTerminateExchange( 
  CPQFCHBA* cpqfcHBAdata, SCSI_NEXUS *ScsiNexus, int TerminateStatus)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG x_ID;

  if( ScsiNexus )
  {
//    printk("TerminateExchange: ScsiNexus chan/target %d/%d\n",
//		    ScsiNexus->channel, ScsiNexus->target);

  } 
  
  for( x_ID = 0; x_ID < TACH_SEST_LEN; x_ID++)
  {
    if( Exchanges->fcExchange[x_ID].type )  // in use?
    {
      if( ScsiNexus == NULL ) // our HBA changed - term. all
      {
	Exchanges->fcExchange[x_ID].status = TerminateStatus;
        cpqfcTSPutLinkQue( cpqfcHBAdata, BLS_ABTS, &x_ID ); 
      }
      else
      {
	// If a device, according to WWN, has been removed, it's
	// port_id may be used by another working device, so we
	// have to terminate by SCSI target, NOT port_id.
        if( Exchanges->fcExchange[x_ID].Cmnd) // Cmnd in progress?
	{	                 
	  if( (Exchanges->fcExchange[x_ID].Cmnd->device->id == ScsiNexus->target)
			&&
            (Exchanges->fcExchange[x_ID].Cmnd->device->channel == ScsiNexus->channel)) 
          {
            Exchanges->fcExchange[x_ID].status = TerminateStatus;
            cpqfcTSPutLinkQue( cpqfcHBAdata, BLS_ABTS, &x_ID ); // timed-out
          }
	}

	// (in case we ever need it...)
	// all SEST structures have a remote node ID at SEST DWORD 2
        //          if( (fcChip->SEST->u[ x_ID ].TWE.Remote_Node_ID >> 8)
        //                ==  port_id)
      } 
    }
  }
}


static void ProcessELS_Request( 
              CPQFCHBA* cpqfcHBAdata, TachFCHDR_GCMND* fchs)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
//  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
//  ULONG ox_id = (fchs->ox_rx_id >>16);
  PFC_LOGGEDIN_PORT pLoggedInPort=NULL, pLastLoggedInPort;
  BOOLEAN NeedReject = FALSE;
  ULONG ls_reject_code = 0; // default don'n know??


  // Check the incoming frame for a supported ELS type
  switch( fchs->pl[0] & 0xFFFF)
  {
  case 0x0050: //  PDISC?

    // Payload for PLOGI and PDISC is identical (request & reply)
    if( !verify_PLOGI( fcChip, fchs, &ls_reject_code) ) // valid payload?
    {
      LOGIN_PAYLOAD logi;       // FC-PH Port Login
      
      // PDISC payload OK. If critical login fields
      // (e.g. WWN) matches last login for this port_id,
      // we may resume any prior exchanges
      // with the other port

      
      BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&logi, sizeof(logi));
   
      pLoggedInPort = fcFindLoggedInPort( 
             fcChip, 
	     NULL,     // don't search Scsi Nexus
	     0,        // don't search linked list for port_id
             &logi.port_name[0],     // search linked list for WWN
             &pLastLoggedInPort);  // must return non-NULL; when a port_id
                                   // is not found, this pointer marks the
                                   // end of the singly linked list
    
      if( pLoggedInPort != NULL)   // WWN found (prior login OK)
      { 
           
 	if( (fchs->s_id & 0xFFFFFF) == pLoggedInPort->port_id)
	{
          // Yes.  We were expecting PDISC?
          if( pLoggedInPort->pdisc )
	  {
	    // Yes; set fields accordingly.     (PDISC, not Originator)
            SetLoginFields( pLoggedInPort, fchs, TRUE, FALSE);
       
            // send 'ACC' reply 
            cpqfcTSPutLinkQue( cpqfcHBAdata, 
                          ELS_PLOGI_ACC, // (PDISC same as PLOGI ACC)
                          fchs );

	    // OK to resume I/O...
	  }
	  else
 	  {
	    printk("Not expecting PDISC (pdisc=FALSE)\n");
	    NeedReject = TRUE;
	    // set reject reason code 
            ls_reject_code = 
              LS_RJT_REASON( PROTOCOL_ERROR, INITIATOR_CTL_ERROR);
	  }
	}
	else
	{
	  if( pLoggedInPort->port_id != 0)
	  {
  	    printk("PDISC PortID change: old %Xh, new %Xh\n",
              pLoggedInPort->port_id, fchs->s_id &0xFFFFFF);
	  }
          NeedReject = TRUE;
          // set reject reason code 
          ls_reject_code = 
	    LS_RJT_REASON( PROTOCOL_ERROR, INITIATOR_CTL_ERROR);
		  
	}
      }
      else
      {
	printk("PDISC Request from unknown WWN\n");
        NeedReject = TRUE;
          
	// set reject reason code 
        ls_reject_code = 
          LS_RJT_REASON( LOGICAL_ERROR, INVALID_PORT_NAME);
      }

    }
    else // Payload unacceptable
    {
      printk("payload unacceptable\n");
      NeedReject = TRUE;  // reject code already set
      
    }

    if( NeedReject)
    {
      ULONG port_id;
      // The PDISC failed.  Set login struct flags accordingly,
      // terminate any I/O to this port, and Q a PLOGI
      if( pLoggedInPort )
      {
        pLoggedInPort->pdisc = FALSE;
        pLoggedInPort->prli = FALSE;
        pLoggedInPort->plogi = FALSE;
	
        cpqfcTSTerminateExchange( cpqfcHBAdata, 
          &pLoggedInPort->ScsiNexus, PORTID_CHANGED);
	port_id = pLoggedInPort->port_id;
      }
      else
      {
	port_id = fchs->s_id &0xFFFFFF;
      }
      fchs->reserved = ls_reject_code; // borrow this (unused) field
      cpqfcTSPutLinkQue( cpqfcHBAdata, ELS_RJT, fchs );
    }
   
    break;



  case 0x0003: //  PLOGI?

    // Payload for PLOGI and PDISC is identical (request & reply)
    if( !verify_PLOGI( fcChip, fchs, &ls_reject_code) ) // valid payload?
    {
      LOGIN_PAYLOAD logi;       // FC-PH Port Login
      BOOLEAN NeedReject = FALSE;
      
      // PDISC payload OK. If critical login fields
      // (e.g. WWN) matches last login for this port_id,
      // we may resume any prior exchanges
      // with the other port

      
      BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&logi, sizeof(logi));
   
      pLoggedInPort = fcFindLoggedInPort( 
             fcChip, 
	     NULL,       // don't search Scsi Nexus
	     0,        // don't search linked list for port_id
             &logi.port_name[0],     // search linked list for WWN
             &pLastLoggedInPort);  // must return non-NULL; when a port_id
                                   // is not found, this pointer marks the
                                   // end of the singly linked list
    
      if( pLoggedInPort == NULL)   // WWN not found -New Port
      { 
      	pLoggedInPort = CreateFcPort( 
			  cpqfcHBAdata, 
			  pLastLoggedInPort, 
			  fchs,
			  &logi);
        if( pLoggedInPort == NULL )
        {
          printk(" cpqfcTS: New port allocation failed - lost FC device!\n");
          // Now Q a LOGOut Request, since we won't be talking to that device
	
          NeedReject = TRUE;  
	  
          // set reject reason code 
          ls_reject_code = 
            LS_RJT_REASON( LOGICAL_ERROR, NO_LOGIN_RESOURCES);
	    
	}
      }
      if( !NeedReject )
      {
      
        // OK - we have valid fcPort ptr; set fields accordingly.   
	//                         (not PDISC, not Originator)
        SetLoginFields( pLoggedInPort, fchs, FALSE, FALSE); 

        // send 'ACC' reply 
        cpqfcTSPutLinkQue( cpqfcHBAdata, 
                      ELS_PLOGI_ACC, // (PDISC same as PLOGI ACC)
                      fchs );
      }
    }
    else // Payload unacceptable
    {
      printk("payload unacceptable\n");
      NeedReject = TRUE;  // reject code already set
    }

    if( NeedReject)
    {
      // The PDISC failed.  Set login struct flags accordingly,
      // terminate any I/O to this port, and Q a PLOGI
      pLoggedInPort->pdisc = FALSE;
      pLoggedInPort->prli = FALSE;
      pLoggedInPort->plogi = FALSE;
	
      fchs->reserved = ls_reject_code; // borrow this (unused) field

      // send 'RJT' reply 
      cpqfcTSPutLinkQue( cpqfcHBAdata, ELS_RJT, fchs );
    }
   
    // terminate any exchanges with this device...
    if( pLoggedInPort )
    {
      cpqfcTSTerminateExchange( cpqfcHBAdata, 
        &pLoggedInPort->ScsiNexus, PORTID_CHANGED);
    }
    break;



  case 0x1020:  // PRLI?
  {
    BOOLEAN NeedReject = TRUE;
    pLoggedInPort = fcFindLoggedInPort( 
           fcChip, 
           NULL,       // don't search Scsi Nexus
	   (fchs->s_id & 0xFFFFFF),  // search linked list for port_id
           NULL,     // DON'T search linked list for WWN
           NULL);    // don't care
      
    if( pLoggedInPort == NULL ) 
    {
      // huh?
      printk(" Unexpected PRLI Request -not logged in!\n");

      // set reject reason code 
      ls_reject_code = LS_RJT_REASON( PROTOCOL_ERROR, INITIATOR_CTL_ERROR);
      
      // Q a LOGOut here?
    }
    else
    {
      // verify the PRLI ACC payload
      if( !verify_PRLI( fchs, &ls_reject_code) )
      {
        // PRLI Reply is acceptable; were we expecting it?
        if( pLoggedInPort->plogi ) 
        { 
  	  // yes, we expected the PRLI ACC  (not PDISC; not Originator)
          SetLoginFields( pLoggedInPort, fchs, FALSE, FALSE);

          // Q an ACCept Reply
	  cpqfcTSPutLinkQue( cpqfcHBAdata,
                        ELS_PRLI_ACC, 
                        fchs );   
	  
	  NeedReject = FALSE;
	}
        else
        {
          // huh?
          printk(" (unexpected) PRLI REQEST with plogi FALSE\n");

          // set reject reason code 
          ls_reject_code = LS_RJT_REASON( PROTOCOL_ERROR, INITIATOR_CTL_ERROR);
    
    	  // Q a LOGOut here?
	  
        }
      }
      else
      {
        printk(" PRLI REQUEST payload failed verify\n");
        // (reject code set by "verify")

        // Q a LOGOut here?
      }
    }

    if( NeedReject )
    {
      // Q a ReJecT Reply with reason code
      fchs->reserved = ls_reject_code;
      cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_RJT, // Q Type
                    fchs );  
    }
  }
    break;
 

    

  case  0x0005:  // LOGOut?
  {
  // was this LOGOUT because we sent a ELS_PDISC to an FC device
  // with changed (or new) port_id, or does the port refuse 
  // to communicate to us?
  // We maintain a logout counter - if we get 3 consecutive LOGOuts,
  // give up!
    LOGOUT_PAYLOAD logo;
    BOOLEAN GiveUpOnDevice = FALSE;
    ULONG ls_reject_code = 0;
    
    BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&logo, sizeof(logo));

    pLoggedInPort = fcFindLoggedInPort( 
           fcChip, 
           NULL,     // don't search Scsi Nexus
	   0,        // don't search linked list for port_id
           &logo.port_name[0],     // search linked list for WWN
           NULL);    // don't care about end of list
    
    if( pLoggedInPort ) // found the device?
    {
      // Q an ACC reply 
      cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_LOGO_ACC, // Q Type
                    fchs );       // device to respond to

      // set login struct fields (LOGO_counter increment)
      SetLoginFields( pLoggedInPort, fchs, FALSE, FALSE);
      
      // are we an Initiator?
      if( fcChip->Options.initiator)  
      {
        // we're an Initiator, so check if we should 
	// try (another?) login

	// Fabrics routinely log out from us after
	// getting device info - don't try to log them
	// back in.
	if( (fchs->s_id & 0xFFF000) == 0xFFF000 )
	{
	  ; // do nothing
	}
	else if( pLoggedInPort->LOGO_counter <= 3)
	{
	  // try (another) login (PLOGI request)
	  
          cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_PLOGI, // Q Type
                    fchs );  
	
	  // Terminate I/O with "retry" potential
	  cpqfcTSTerminateExchange( cpqfcHBAdata, 
			            &pLoggedInPort->ScsiNexus,
				    PORTID_CHANGED);
	}
	else
	{
	  printk(" Got 3 LOGOuts - terminating comm. with port_id %Xh\n",
			  fchs->s_id &&0xFFFFFF);
	  GiveUpOnDevice = TRUE;
	}
      }
      else
      {
	GiveUpOnDevice = TRUE;
      }


      if( GiveUpOnDevice == TRUE )
      {
        cpqfcTSTerminateExchange( cpqfcHBAdata, 
	                          &pLoggedInPort->ScsiNexus,
		                  DEVICE_REMOVED);
      }
    }  
    else  // we don't know this WWN!
    {
      // Q a ReJecT Reply with reason code
      fchs->reserved = ls_reject_code;
      cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_RJT, // Q Type
                    fchs );  
    }
  }
    break;




  // FABRIC only case
  case 0x0461:  // ELS RSCN (Registered State Change Notification)?
  {
    int Ports;
    int i;
    __u32 Buff;
    // Typically, one or more devices have been added to or dropped
    // from the Fabric.
    // The format of this frame is defined in FC-FLA (Rev 2.7, Aug 1997)
    // The first 32-bit word has a 2-byte Payload Length, which
    // includes the 4 bytes of the first word.  Consequently,
    // this PL len must never be less than 4, must be a multiple of 4,
    // and has a specified max value 256.
    // (Endianess!)
    Ports = ((fchs->pl[0] >>24) - 4) / 4;
    Ports = Ports > 63 ? 63 : Ports;
    
    printk(" RSCN ports: %d\n", Ports);
    if( Ports <= 0 )  // huh?
    {
      // ReJecT the command
      fchs->reserved = LS_RJT_REASON( UNABLE_TO_PERFORM, 0);
    
      cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_RJT, // Q Type
                    fchs ); 
      
      break;
    }
    else  // Accept the command
    {
       cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_ACC, // Q Type
                    fchs ); 
    }
    
      // Check the "address format" to determine action.
      // We have 3 cases:
      // 0 = Port Address; 24-bit address of affected device
      // 1 = Area Address; MS 16 bits valid
      // 2 = Domain Address; MS 8 bits valid
    for( i=0; i<Ports; i++)
    { 
      BigEndianSwap( (UCHAR*)&fchs->pl[i+1],(UCHAR*)&Buff, 4);
      switch( Buff & 0xFF000000)
      {

      case 0:  // Port Address?
	
      case 0x01000000: // Area Domain?
      case 0x02000000: // Domain Address
        // For example, "port_id" 0x201300 
	// OK, let's try a Name Service Request (Query)
      fchs->s_id = 0xFFFFFC;  // Name Server Address
      cpqfcTSPutLinkQue( cpqfcHBAdata, FCS_NSR, fchs);

      break;
	
	
      default:  // huh? new value on version change?
      break;
      }
    }
  }    
  break;    



    
  default:  // don't support this request (yet)
    // set reject reason code 
    fchs->reserved = LS_RJT_REASON( UNABLE_TO_PERFORM, 
		                    REQUEST_NOT_SUPPORTED);
    
    cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_RJT, // Q Type
                    fchs );     
    break;  
  }
}


static void ProcessELS_Reply( 
		CPQFCHBA* cpqfcHBAdata, TachFCHDR_GCMND* fchs)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG ox_id = (fchs->ox_rx_id >>16);
  ULONG ls_reject_code;
  PFC_LOGGEDIN_PORT pLoggedInPort, pLastLoggedInPort;
  
  // If this is a valid reply, then we MUST have sent a request.
  // Verify that we can find a valid request OX_ID corresponding to
  // this reply

  
  if( Exchanges->fcExchange[(fchs->ox_rx_id >>16)].type == 0)
  {
    printk(" *Discarding ACC/RJT frame, xID %04X/%04X* ", 
		    ox_id, fchs->ox_rx_id & 0xffff);
    goto Quit;  // exit this routine
  }


  // Is the reply a RJT (reject)?
  if( (fchs->pl[0] & 0xFFFFL) == 0x01) // Reject reply?
  {
//  ******  REJECT REPLY  ********
    switch( Exchanges->fcExchange[ox_id].type )
    {
	  
    case ELS_FDISC:  // we sent out Fabric Discovery
    case ELS_FLOGI:  // we sent out FLOGI

      printk("RJT received on Fabric Login from %Xh, reason %Xh\n", 
        fchs->s_id, fchs->pl[1]);    

    break;

    default:
    break;
    }
      
    goto Done;
  }

  // OK, we have an ACCept...
  // What's the ACC type? (according to what we sent)
  switch( Exchanges->fcExchange[ox_id].type )
  {
	  
  case ELS_PLOGI:  // we sent out PLOGI
    if( !verify_PLOGI( fcChip, fchs, &ls_reject_code) )
    {
      LOGIN_PAYLOAD logi;       // FC-PH Port Login
      
      // login ACC payload acceptable; search for WWN in our list
      // of fcPorts
      
      BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&logi, sizeof(logi));
   
      pLoggedInPort = fcFindLoggedInPort( 
             fcChip, 
	     NULL,     // don't search Scsi Nexus
	     0,        // don't search linked list for port_id
             &logi.port_name[0],     // search linked list for WWN
             &pLastLoggedInPort);  // must return non-NULL; when a port_id
                                   // is not found, this pointer marks the
                                   // end of the singly linked list
    
      if( pLoggedInPort == NULL)         // WWN not found - new port
      {

	pLoggedInPort = CreateFcPort( 
			  cpqfcHBAdata, 
			  pLastLoggedInPort, 
			  fchs,
			  &logi);

        if( pLoggedInPort == NULL )
        {
          printk(" cpqfcTS: New port allocation failed - lost FC device!\n");
          // Now Q a LOGOut Request, since we won't be talking to that device
	
          goto Done;  // exit with error! dropped login frame
	}
      }
      else      // WWN was already known.  Ensure that any open
	        // exchanges for this WWN are terminated.
      	// NOTE: It's possible that a device can change its 
	// 24-bit port_id after a Link init or Fabric change 
	// (e.g. LIP or Fabric RSCN).  In that case, the old
	// 24-bit port_id may be duplicated, or no longer exist.
      {

        cpqfcTSTerminateExchange( cpqfcHBAdata, 
          &pLoggedInPort->ScsiNexus, PORTID_CHANGED);
      }

      // We have an fcPort struct - set fields accordingly
                                    // not PDISC, originator 
      SetLoginFields( pLoggedInPort, fchs, FALSE, TRUE);
			
      // We just set a "port_id"; is it duplicated?
      TestDuplicatePortId( cpqfcHBAdata, pLoggedInPort);

      // For Fabric operation, we issued PLOGI to 0xFFFFFC
      // so we can send SCR (State Change Registration) 
      // Check for this special case...
      if( fchs->s_id == 0xFFFFFC ) 
      {
        // PLOGI ACC was a Fabric response... issue SCR
	fchs->s_id = 0xFFFFFD;  // address for SCR
        cpqfcTSPutLinkQue( cpqfcHBAdata, ELS_SCR, fchs);
      }

      else
      {
      // Now we need a PRLI to enable FCP-SCSI operation
      // set flags and Q up a ELS_PRLI
        cpqfcTSPutLinkQue( cpqfcHBAdata, ELS_PRLI, fchs);
      }
    }
    else
    {
      // login payload unacceptable - reason in ls_reject_code
      // Q up a Logout Request
      printk("Login Payload unacceptable\n");

    }
    break;


  // PDISC logic very similar to PLOGI, except we never want
  // to allocate mem for "new" port, and we set flags differently
  // (might combine later with PLOGI logic for efficiency)  
  case ELS_PDISC:  // we sent out PDISC
    if( !verify_PLOGI( fcChip, fchs, &ls_reject_code) )
    {
      LOGIN_PAYLOAD logi;       // FC-PH Port Login
      BOOLEAN NeedLogin = FALSE;
      
      // login payload acceptable; search for WWN in our list
      // of (previously seen) fcPorts
      
      BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&logi, sizeof(logi));
   
      pLoggedInPort = fcFindLoggedInPort( 
             fcChip, 
	     NULL,     // don't search Scsi Nexus
	     0,        // don't search linked list for port_id
             &logi.port_name[0],     // search linked list for WWN
             &pLastLoggedInPort);  // must return non-NULL; when a port_id
                                   // is not found, this pointer marks the
                                   // end of the singly linked list
    
      if( pLoggedInPort != NULL)   // WWN found?
      {
        // WWN has same port_id as last login?  (Of course, a properly
	// working FC device should NEVER ACCept a PDISC if it's
	// port_id changed, but check just in case...)
	if( (fchs->s_id & 0xFFFFFF) == pLoggedInPort->port_id)
	{
          // Yes.  We were expecting PDISC?
          if( pLoggedInPort->pdisc )
	  {
            int i;
	    
	    
	    // PDISC expected -- set fields.  (PDISC, Originator)
            SetLoginFields( pLoggedInPort, fchs, TRUE, TRUE);

	    // We are ready to resume FCP-SCSI to this device...
            // Do we need to start anything that was Queued?

            for( i=0; i< TACH_SEST_LEN; i++)
            {
              // see if any exchange for this PDISC'd port was queued
              if( ((fchs->s_id &0xFFFFFF) == 
                   (Exchanges->fcExchange[i].fchs.d_id & 0xFFFFFF))
                      &&
                  (Exchanges->fcExchange[i].status & EXCHANGE_QUEUED))
              {
                fchs->reserved = i; // copy ExchangeID
//                printk(" *Q x_ID %Xh after PDISC* ",i);

                cpqfcTSPutLinkQue( cpqfcHBAdata, EXCHANGE_QUEUED, fchs );
              }
            }

	    // Complete commands Q'd while we were waiting for Login

	    UnblockScsiDevice( cpqfcHBAdata->HostAdapter, pLoggedInPort);
	  }
	  else
	  {
	    printk("Not expecting PDISC (pdisc=FALSE)\n");
	    NeedLogin = TRUE;
	  }
	}
	else
	{
	  printk("PDISC PortID change: old %Xh, new %Xh\n",
            pLoggedInPort->port_id, fchs->s_id &0xFFFFFF);
          NeedLogin = TRUE;
		  
	}
      }
      else
      {
	printk("PDISC ACC from unknown WWN\n");
        NeedLogin = TRUE;
      }

      if( NeedLogin)
      {
	
        // The PDISC failed.  Set login struct flags accordingly,
	// terminate any I/O to this port, and Q a PLOGI
	if( pLoggedInPort )  // FC device previously known?
	{

          cpqfcTSPutLinkQue( cpqfcHBAdata,
                    ELS_LOGO, // Q Type
                    fchs );   // has port_id to send to 

	  // There are a variety of error scenarios which can result
  	  // in PDISC failure, so as a catchall, add the check for
	  // duplicate port_id.
	  TestDuplicatePortId( cpqfcHBAdata, pLoggedInPort);

//    TriggerHBA( fcChip->Registers.ReMapMemBase, 0);
          pLoggedInPort->pdisc = FALSE;
          pLoggedInPort->prli = FALSE;
          pLoggedInPort->plogi = FALSE;
	
          cpqfcTSTerminateExchange( cpqfcHBAdata, 
	    &pLoggedInPort->ScsiNexus, PORTID_CHANGED);
        }
        cpqfcTSPutLinkQue( cpqfcHBAdata, ELS_PLOGI, fchs );
      }
    }
    else
    {
      // login payload unacceptable - reason in ls_reject_code
      // Q up a Logout Request
      printk("ERROR: Login Payload unacceptable!\n");

    }
   
    break;
    


  case ELS_PRLI:  // we sent out PRLI


    pLoggedInPort = fcFindLoggedInPort( 
           fcChip, 
           NULL,       // don't search Scsi Nexus
	   (fchs->s_id & 0xFFFFFF),  // search linked list for port_id
           NULL,     // DON'T search linked list for WWN
           NULL);    // don't care
      
    if( pLoggedInPort == NULL ) 
    {
      // huh?
      printk(" Unexpected PRLI ACCept frame!\n");

      // Q a LOGOut here?

      goto Done;
    }

    // verify the PRLI ACC payload
    if( !verify_PRLI( fchs, &ls_reject_code) )
    {
      // PRLI Reply is acceptable; were we expecting it?
      if( pLoggedInPort->plogi ) 
      { 
	// yes, we expected the PRLI ACC  (not PDISC; Originator)
	SetLoginFields( pLoggedInPort, fchs, FALSE, TRUE);

        // OK, let's send a REPORT_LUNS command to determine
	// whether VSA or PDA FCP-LUN addressing is used.
	
        cpqfcTSPutLinkQue( cpqfcHBAdata, SCSI_REPORT_LUNS, fchs );
	
	// It's possible that a device we were talking to changed 
	// port_id, and has logged back in.  This function ensures
	// that I/O will resume.
        UnblockScsiDevice( cpqfcHBAdata->HostAdapter, pLoggedInPort);

      }
      else
      {
        // huh?
        printk(" (unexpected) PRLI ACCept with plogi FALSE\n");

        // Q a LOGOut here?
        goto Done;
      }
    }
    else
    {
      printk(" PRLI ACCept payload failed verify\n");

      // Q a LOGOut here?
    }

    break;
 
  case ELS_FLOGI:  // we sent out FLOGI (Fabric Login)

    // update the upper 16 bits of our port_id in Tachyon
    // the switch adds those upper 16 bits when responding
    // to us (i.e. we are the destination_id)
    fcChip->Registers.my_al_pa = (fchs->d_id & 0xFFFFFF);
    writel( fcChip->Registers.my_al_pa,  
      fcChip->Registers.ReMapMemBase + TL_MEM_TACH_My_ID);

    // now send out a PLOGI to the well known port_id 0xFFFFFC
    fchs->s_id = 0xFFFFFC;
    cpqfcTSPutLinkQue( cpqfcHBAdata, ELS_PLOGI, fchs);
  
   break; 


  case ELS_FDISC:  // we sent out FDISC (Fabric Discovery (Login))

   printk( " ELS_FDISC success ");
   break;
   

  case ELS_SCR:  // we sent out State Change Registration
    // now we can issue Name Service Request to find any
    // Fabric-connected devices we might want to login to.
   
	
    fchs->s_id = 0xFFFFFC;  // Name Server Address
    cpqfcTSPutLinkQue( cpqfcHBAdata, FCS_NSR, fchs);
    

    break;

    
  default:
    printk(" *Discarding unknown ACC frame, xID %04X/%04X* ", 
   		    ox_id, fchs->ox_rx_id & 0xffff);
    break;
  }

  
Done:
  // Regardless of whether the Reply is valid or not, the
  // the exchange is done - complete
  cpqfcTSCompleteExchange(cpqfcHBAdata->PciDev, fcChip, (fchs->ox_rx_id >>16)); 
	  
Quit:    
  return;
}






// ****************  Fibre Channel Services  **************
// This is where we process the Directory (Name) Service Reply
// to know which devices are on the Fabric

static void ProcessFCS_Reply( 
	CPQFCHBA* cpqfcHBAdata, TachFCHDR_GCMND* fchs)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG ox_id = (fchs->ox_rx_id >>16);
//  ULONG ls_reject_code;
//  PFC_LOGGEDIN_PORT pLoggedInPort, pLastLoggedInPort;
  
  // If this is a valid reply, then we MUST have sent a request.
  // Verify that we can find a valid request OX_ID corresponding to
  // this reply

  if( Exchanges->fcExchange[(fchs->ox_rx_id >>16)].type == 0)
  {
    printk(" *Discarding Reply frame, xID %04X/%04X* ", 
		    ox_id, fchs->ox_rx_id & 0xffff);
    goto Quit;  // exit this routine
  }


  // OK, we were expecting it.  Now check to see if it's a
  // "Name Service" Reply, and if so force a re-validation of
  // Fabric device logins (i.e. Start the login timeout and
  // send PDISC or PLOGI)
  // (Endianess Byte Swap?)
  if( fchs->pl[1] == 0x02FC )  // Name Service
  {
    // got a new (or NULL) list of Fabric attach devices... 
    // Invalidate current logins
    
    PFC_LOGGEDIN_PORT pLoggedInPort = &fcChip->fcPorts;
    while( pLoggedInPort ) // for all ports which are expecting
                           // PDISC after the next LIP, set the
                           // logoutTimer
    {

      if( (pLoggedInPort->port_id & 0xFFFF00)  // Fabric device?
		      &&
          (pLoggedInPort->port_id != 0xFFFFFC) ) // NOT the F_Port
      {
        pLoggedInPort->LOGO_timer = 6;  // what's the Fabric timeout??
                                // suspend any I/O in progress until
                                // PDISC received...
        pLoggedInPort->prli = FALSE;   // block FCP-SCSI commands
      }
	    
      pLoggedInPort = pLoggedInPort->pNextPort;
    }
    
    if( fchs->pl[2] == 0x0280)  // ACCept?
    {
      // Send PLOGI or PDISC to these Fabric devices
      SendLogins( cpqfcHBAdata, &fchs->pl[4] );  
    }


    // As of this writing, the only reason to reject is because NO
    // devices are left on the Fabric.  We already started
    // "logged out" timers; if the device(s) don't come
    // back, we'll do the implicit logout in the heart beat 
    // timer routine
    else  // ReJecT
    {
      // this just means no Fabric device is visible at this instant
    } 
  }

  // Regardless of whether the Reply is valid or not, the
  // the exchange is done - complete
  cpqfcTSCompleteExchange(cpqfcHBAdata->PciDev, fcChip, (fchs->ox_rx_id >>16));
	  
Quit:    
  return;
}







static void AnalyzeIncomingFrame( 
        CPQFCHBA *cpqfcHBAdata,
        ULONG QNdx )
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  PFC_LINK_QUE fcLQ = cpqfcHBAdata->fcLQ;
  TachFCHDR_GCMND* fchs = 
    (TachFCHDR_GCMND*)fcLQ->Qitem[QNdx].ulBuff;
//  ULONG ls_reject_code;  // reason for rejecting login
  LONG ExchangeID;
//  FC_LOGGEDIN_PORT *pLoggedInPort;
  BOOLEAN AbortAccept;  

  ENTER("AnalyzeIncomingFrame");



  switch( fcLQ->Qitem[QNdx].Type) // FCP or Unknown
  {

  case SFQ_UNKNOWN:  // unknown frame (e.g. LIP position frame, NOP, etc.)
 

      // *********  FC-4 Device Data/ Fibre Channel Service *************
    if( ((fchs->d_id &0xF0000000) == 0)   // R_CTL (upper nibble) 0x0?
                &&   
      (fchs->f_ctl & 0x20000000) )  // TYPE 20h is Fibre Channel Service
    {

      // ************** FCS Reply **********************

      if( (fchs->d_id & 0xff000000L) == 0x03000000L)  // (31:23 R_CTL)
      {
	ProcessFCS_Reply( cpqfcHBAdata, fchs );

      }  // end of  FCS logic

    }
    

      // ***********  Extended Link Service **************

    else if( fchs->d_id & 0x20000000   // R_CTL 0x2?
                  &&   
      (fchs->f_ctl & 0x01000000) )  // TYPE = 1
    {

                           // these frames are either a response to
                           // something we sent (0x23) or "unsolicited"
                           // frames (0x22).


      // **************Extended Link REPLY **********************
                           // R_CTL Solicited Control Reply

      if( (fchs->d_id & 0xff000000L) == 0x23000000L)  // (31:23 R_CTL)
      {

	ProcessELS_Reply( cpqfcHBAdata, fchs );

      }  // end of  "R_CTL Solicited Control Reply"




       // **************Extended Link REQUEST **********************
       // (unsolicited commands from another port or task...)

                           // R_CTL Ext Link REQUEST
      else if( (fchs->d_id & 0xff000000L) == 0x22000000L &&
              (fchs->ox_rx_id != 0xFFFFFFFFL) ) // (ignore LIP frame)
      {



	ProcessELS_Request( cpqfcHBAdata, fchs );

      }



        // ************** LILP **********************
      else if( (fchs->d_id & 0xff000000L) == 0x22000000L &&
               (fchs->ox_rx_id == 0xFFFFFFFFL)) // (e.g., LIP frames)

      {
        // SANMark specifies that when available, we must use
	// the LILP frame to determine which ALPAs to send Port Discovery
	// to...

        if( fchs->pl[0] == 0x0711L) //  ELS_PLOGI?
	{
//	  UCHAR *ptr = (UCHAR*)&fchs->pl[1];
//	  printk(" %d ALPAs found\n", *ptr);
	  memcpy( fcChip->LILPmap, &fchs->pl[1], 32*4); // 32 DWORDs
	  fcChip->Options.LILPin = 1; // our LILPmap is valid!
	  // now post to make Port Discovery happen...
          cpqfcTSPutLinkQue( cpqfcHBAdata, LINKACTIVE, fchs);  
	}
      }
    }

     
    // *****************  BASIC LINK SERVICE *****************
    
    else if( fchs->d_id & 0x80000000  // R_CTL:
                    &&           // Basic Link Service Request
           !(fchs->f_ctl & 0xFF000000) )  // type=0 for BLS
    {

      // Check for ABTS (Abort Sequence)
      if( (fchs->d_id & 0x8F000000) == 0x81000000)
      {
        // look for OX_ID, S_ID pair that matches in our
        // fcExchanges table; if found, reply with ACCept and complete
        // the exchange

        // Per PLDA, an ABTS is sent by an initiator; therefore
        // assume that if we have an exhange open to the port who
        // sent ABTS, it will be the d_id of what we sent.  
        for( ExchangeID = 0, AbortAccept=FALSE;
             ExchangeID < TACH_SEST_LEN; ExchangeID++)
        {
            // Valid "target" exchange 24-bit port_id matches? 
            // NOTE: For the case of handling Intiator AND Target
            // functions on the same chip, we can have TWO Exchanges
            // with the same OX_ID -- OX_ID/FFFF for the CMND, and
            // OX_ID/RX_ID for the XRDY or DATA frame(s).  Ideally,
            // we would like to support ABTS from Initiators or Targets,
            // but it's not clear that can be supported on Tachyon for
            // all cases (requires more investigation).
            
          if( (Exchanges->fcExchange[ ExchangeID].type == SCSI_TWE ||
               Exchanges->fcExchange[ ExchangeID].type == SCSI_TRE)
                  &&
             ((Exchanges->fcExchange[ ExchangeID].fchs.d_id & 0xFFFFFF) ==
             (fchs->s_id & 0xFFFFFF)) )
          {
              
              // target xchnge port_id matches -- how about OX_ID?
            if( (Exchanges->fcExchange[ ExchangeID].fchs.ox_rx_id &0xFFFF0000)
                    == (fchs->ox_rx_id & 0xFFFF0000) )
                    // yes! post ACCept response; will be completed by fcStart
            {
              Exchanges->fcExchange[ ExchangeID].status = TARGET_ABORT;
                
                // copy (add) rx_id field for simplified ACCept reply
              fchs->ox_rx_id = 
                Exchanges->fcExchange[ ExchangeID].fchs.ox_rx_id;
                
              cpqfcTSPutLinkQue( cpqfcHBAdata,
                            BLS_ABTS_ACC, // Q Type 
                            fchs );    // void QueContent
              AbortAccept = TRUE;
      printk("ACCepting ABTS for x_ID %8.8Xh, SEST pair %8.8Xh\n", 
             fchs->ox_rx_id, Exchanges->fcExchange[ ExchangeID].fchs.ox_rx_id);
              break;      // ABTS can affect only ONE exchange -exit loop
            }
          }
        }  // end of FOR loop
        if( !AbortAccept ) // can't ACCept ABTS - send Reject
        {
      printk("ReJecTing: can't find ExchangeID %8.8Xh for ABTS command\n", 
            fchs->ox_rx_id);
          if( Exchanges->fcExchange[ ExchangeID].type 
                &&
              !(fcChip->SEST->u[ ExchangeID].IWE.Hdr_Len
               & 0x80000000))
          {
            cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, ExchangeID);
          }
          else
          {
            printk("Unexpected ABTS ReJecT! SEST[%X] Dword 0: %Xh\n", 
              ExchangeID, fcChip->SEST->u[ ExchangeID].IWE.Hdr_Len);
          }
        }
      }

      // Check for BLS {ABTS? (Abort Sequence)} ACCept
      else if( (fchs->d_id & 0x8F000000) == 0x84000000)
      {
        // target has responded with ACC for our ABTS;
	// complete the indicated exchange with ABORTED status 
        // Make no checks for correct RX_ID, since
	// all we need to conform ABTS ACC is the OX_ID.
        // Verify that the d_id matches!
 
        ExchangeID = (fchs->ox_rx_id >> 16) & 0x7FFF; // x_id from ACC
//	printk("ABTS ACC x_ID 0x%04X 0x%04X, status %Xh\n", 
//          fchs->ox_rx_id >> 16, fchs->ox_rx_id & 0xffff,
//          Exchanges->fcExchange[ExchangeID].status);


	
        if( ExchangeID < TACH_SEST_LEN ) // x_ID makes sense
        {
            // Does "target" exchange 24-bit port_id match? 
            // (See "NOTE" above for handling Intiator AND Target in
            // the same device driver)
            // First, if this is a target response, then we originated
	    // (initiated) it with BLS_ABTS:
	  
          if( (Exchanges->fcExchange[ ExchangeID].type == BLS_ABTS)

                  &&
	    // Second, does the source of this ACC match the destination
            // of who we originally sent it to?
             ((Exchanges->fcExchange[ ExchangeID].fchs.d_id & 0xFFFFFF) ==
             (fchs->s_id & 0xFFFFFF)) )
          {
            cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, ExchangeID );
	  }
        }              
      }
      // Check for BLS {ABTS? (Abort Sequence)} ReJecT
      else if( (fchs->d_id & 0x8F000000) == 0x85000000)
      {
        // target has responded with RJT for our ABTS;
	// complete the indicated exchange with ABORTED status 
        // Make no checks for correct RX_ID, since
	// all we need to conform ABTS ACC is the OX_ID.
        // Verify that the d_id matches!
 
        ExchangeID = (fchs->ox_rx_id >> 16) & 0x7FFF; // x_id from ACC
//	printk("BLS_ABTS RJT on Exchange 0x%04X 0x%04X\n", 
//          fchs->ox_rx_id >> 16, fchs->ox_rx_id & 0xffff);

        if( ExchangeID < TACH_SEST_LEN ) // x_ID makes sense
	{  
            // Does "target" exchange 24-bit port_id match? 
            // (See "NOTE" above for handling Intiator AND Target in
            // the same device driver)
            // First, if this is a target response, then we originated
	    // (initiated) it with BLS_ABTS:
		  
          if( (Exchanges->fcExchange[ ExchangeID].type == BLS_ABTS)

                  &&
	    // Second, does the source of this ACC match the destination
            // of who we originally sent it to?
             ((Exchanges->fcExchange[ ExchangeID].fchs.d_id & 0xFFFFFF) ==
             (fchs->s_id & 0xFFFFFF)) )
          {
	    // YES! NOTE: There is a bug in CPQ's RA-4000 box 
	    // where the "reason code" isn't returned in the payload
	    // For now, simply presume the reject is because the target
	    // already completed the exchange...
	    
//            printk("complete x_ID %Xh on ABTS RJT\n", ExchangeID);
            cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, ExchangeID );
	  }
	} 
      }  // end of ABTS check
    }  // end of Basic Link Service Request
    break;
  
    default:
      printk("AnalyzeIncomingFrame: unknown type: %Xh(%d)\n",
        fcLQ->Qitem[QNdx].Type,
        fcLQ->Qitem[QNdx].Type);
    break;
  }
}


// Function for Port Discovery necessary after every FC 
// initialization (e.g. LIP).
// Also may be called if from Fabric Name Service logic.

static void SendLogins( CPQFCHBA *cpqfcHBAdata, __u32 *FabricPortIds )
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG ulStatus=0;
  TachFCHDR_GCMND fchs;  // copy fields for transmission
  int i;
  ULONG loginType;
  LONG ExchangeID;
  PFC_LOGGEDIN_PORT pLoggedInPort;
  __u32 PortIds[ number_of_al_pa];
  int NumberOfPorts=0;

  // We're going to presume (for now) that our limit of Fabric devices
  // is the same as the number of alpa on a private loop (126 devices).
  // (Of course this could be changed to support however many we have
  // memory for).
  memset( &PortIds[0], 0, sizeof(PortIds));
   
  // First, check if this login is for our own Link Initialization
  // (e.g. LIP on FC-AL), or if we have knowledge of Fabric devices
  // from a switch.  If we are logging into Fabric devices, we'll
  // have a non-NULL FabricPortId pointer
  
  if( FabricPortIds != NULL) // may need logins
  {
    int LastPort=FALSE;
    i = 0;
    while( !LastPort)
    {
      // port IDs From NSR payload; byte swap needed?
      BigEndianSwap( (UCHAR*)FabricPortIds, (UCHAR*)&PortIds[i], 4);
 
//      printk("FPortId[%d] %Xh ", i, PortIds[i]);
      if( PortIds[i] & 0x80000000)
	LastPort = TRUE;
      
      PortIds[i] &= 0xFFFFFF; // get 24-bit port_id
      // some non-Fabric devices (like the Crossroads Fibre/Scsi bridge)
      // erroneously use ALPA 0.
      if( PortIds[i]  ) // need non-zero port_id...
        i++;
      
      if( i >= number_of_al_pa ) // (in)sanity check
	break;
      FabricPortIds++;  // next...
    }

    NumberOfPorts = i;
//    printk("NumberOf Fabric ports %d", NumberOfPorts);
  }
  
  else  // need to send logins on our "local" link
  {
  
    // are we a loop port?  If so, check for reception of LILP frame,
    // and if received use it (SANMark requirement)
    if( fcChip->Options.LILPin )
    {
      int j=0;
      // sanity check on number of ALPAs from LILP frame...
      // For format of LILP frame, see FC-AL specs or 
      // "Fibre Channel Bench Reference", J. Stai, 1995 (ISBN 1-879936-17-8)
      // First byte is number of ALPAs
      i = fcChip->LILPmap[0] >= (32*4) ? 32*4 : fcChip->LILPmap[0];
      NumberOfPorts = i;
//      printk(" LILP alpa count %d ", i);
      while( i > 0)
      {
	PortIds[j] = fcChip->LILPmap[1+ j];
	j++; i--;
      }
    }
    else  // have to send login to everybody
    {
      int j=0;
      i = number_of_al_pa;
      NumberOfPorts = i;
      while( i > 0)
      {
        PortIds[j] = valid_al_pa[j]; // all legal ALPAs
	j++; i--;
      }
    }
  }


  // Now we have a copy of the port_ids (and how many)...
  for( i = 0; i < NumberOfPorts; i++)
  {
    // 24-bit FC Port ID
    fchs.s_id = PortIds[i];  // note: only 8-bits used for ALPA


    // don't log into ourselves (Linux Scsi disk scan will stop on
    // no TARGET support error on us, and quit trying for rest of devices)
    if( (fchs.s_id & 0xFF ) == (fcChip->Registers.my_al_pa & 0xFF) )
      continue;

    // fabric login needed?
    if( (fchs.s_id == 0) || 
        (fcChip->Options.fabric == 1) )
    {
      fcChip->Options.flogi = 1;  // fabric needs longer for login
      // Do we need FLOGI or FDISC?
      pLoggedInPort = fcFindLoggedInPort( 
             fcChip, 
             NULL,           // don't search SCSI Nexus
             0xFFFFFC,       // search linked list for Fabric port_id
             NULL,           // don't search WWN
             NULL);          // (don't care about end of list)

      if( pLoggedInPort )    // If found, we have prior experience with
                             // this port -- check whether PDISC is needed
      {
        if( pLoggedInPort->flogi )
	{
	  // does the switch support FDISC?? (FLOGI for now...)
          loginType = ELS_FLOGI;  // prior FLOGI still valid
	}
        else
          loginType = ELS_FLOGI;  // expired FLOGI
      }
      else                      // first FLOGI?
        loginType = ELS_FLOGI;  


      fchs.s_id = 0xFFFFFE;   // well known F_Port address

      // Fabrics are not required to support FDISC, and
      // it's not clear if that helps us anyway, since
      // we'll want a Name Service Request to re-verify
      // visible devices...
      // Consequently, we always want our upper 16 bit
      // port_id to be zero (we'll be rejected if we
      // use our prior port_id if we've been plugged into
      // a different switch port).
      // Trick Tachyon to send to ALPA 0 (see TL/TS UG, pg 87)
      // If our ALPA is 55h for instance, we want the FC frame
      // s_id to be 0x000055, while Tach's my_al_pa register
      // must be 0x000155, to force an OPN at ALPA 0 
      // (the Fabric port)
      fcChip->Registers.my_al_pa &= 0xFF; // only use ALPA for FLOGI
      writel( fcChip->Registers.my_al_pa | 0x0100,  
        fcChip->Registers.ReMapMemBase + TL_MEM_TACH_My_ID);
    }

    else // not FLOGI...
    {
      // should we send PLOGI or PDISC?  Check if any prior port_id
      // (e.g. alpa) completed a PLOGI/PRLI exchange by checking 
      // the pdisc flag.

      pLoggedInPort = fcFindLoggedInPort( 
             fcChip, 
             NULL,           // don't search SCSI Nexus
             fchs.s_id,      // search linked list for al_pa
             NULL,           // don't search WWN
             NULL);          // (don't care about end of list)

             

      if( pLoggedInPort )      // If found, we have prior experience with
                             // this port -- check whether PDISC is needed
      {
        if( pLoggedInPort->pdisc )
	{
          loginType = ELS_PDISC;  // prior PLOGI and PRLI maybe still valid
           
	}
        else
          loginType = ELS_PLOGI;  // prior knowledge, but can't use PDISC
      }
      else                      // never talked to this port_id before
        loginType = ELS_PLOGI;  // prior knowledge, but can't use PDISC
    }


    
    ulStatus = cpqfcTSBuildExchange(
          cpqfcHBAdata,
          loginType,            // e.g. PLOGI
          &fchs,        // no incoming frame (we are originator)
          NULL,         // no data (no scatter/gather list)
          &ExchangeID );// fcController->fcExchanges index, -1 if failed

    if( !ulStatus ) // Exchange setup OK?
    {
      ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, ExchangeID );
      if( !ulStatus )
      {
          // submitted to Tach's Outbound Que (ERQ PI incremented)
          // waited for completion for ELS type (Login frames issued
          // synchronously)

	if( loginType == ELS_PDISC )
	{
	  // now, we really shouldn't Revalidate SEST exchanges until
	  // we get an ACC reply from our target and verify that
	  // the target address/WWN is unchanged.  However, when a fast
	  // target gets the PDISC, they can send SEST Exchange data
	  // before we even get around to processing the PDISC ACC.
	  // Consequently, we lose the I/O.
	  // To avoid this, go ahead and Revalidate when the PDISC goes
	  // out, anticipating that the ACC will be truly acceptable
	  // (this happens 99.9999....% of the time).
	  // If we revalidate a SEST write, and write data goes to a
	  // target that is NOT the one we originated the WRITE to,
	  // that target is required (FCP-SCSI specs, etc) to discard 
	  // our WRITE data.

          // Re-validate SEST entries (Tachyon hardware assists)
          RevalidateSEST( cpqfcHBAdata->HostAdapter, pLoggedInPort); 
    //TriggerHBA( fcChip->Registers.ReMapMemBase, 1);
	}
      }
      else  // give up immediately on error
      {
#ifdef LOGIN_DBG
        printk("SendLogins: fcStartExchange failed: %Xh\n", ulStatus );
#endif
        break;
      }

              
      if( fcChip->Registers.FMstatus.value & 0x080 ) // LDn during Port Disc.
      {
        ulStatus = LNKDWN_OSLS;
#ifdef LOGIN_DBG
        printk("SendLogins: PortDisc aborted (LDn) @alpa %Xh\n", fchs.s_id);
#endif
        break;
      }
        // Check the exchange for bad status (i.e. FrameTimeOut),
        // and complete on bad status (most likely due to BAD_ALPA)
        // on LDn, DPC function may already complete (ABORT) a started
        // exchange, so check type first (type = 0 on complete).
      if( Exchanges->fcExchange[ExchangeID].status )
      {
#ifdef LOGIN_DBG 
 	printk("completing x_ID %X on status %Xh\n", 
          ExchangeID, Exchanges->fcExchange[ExchangeID].status);
#endif
        cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, ExchangeID);
      }
    }
    else   // Xchange setup failed...
    {
#ifdef LOGIN_DBG
      printk("FC: cpqfcTSBuildExchange failed: %Xh\n", ulStatus );
#endif
      break;
    }
  }
  if( !ulStatus )
  {
    // set the event signifying that all ALPAs were sent out.
#ifdef LOGIN_DBG
    printk("SendLogins: PortDiscDone\n");
#endif
    cpqfcHBAdata->PortDiscDone = 1;


    // TL/TS UG, pg. 184
    // 0x0065 = 100ms for RT_TOV
    // 0x01f5 = 500ms for ED_TOV
    fcChip->Registers.ed_tov.value = 0x006501f5L; 
    writel( fcChip->Registers.ed_tov.value,
      (fcChip->Registers.ed_tov.address));

    // set the LP_TOV back to ED_TOV (i.e. 500 ms)
    writel( 0x00000010, fcChip->Registers.ReMapMemBase +TL_MEM_FM_TIMEOUT2);
  }
  else
  {
    printk("SendLogins: failed at xchng %Xh, alpa %Xh, status %Xh\n", 
      ExchangeID, fchs.s_id, ulStatus);
  }
  LEAVE("SendLogins");

}


// for REPORT_LUNS documentation, see "In-Depth Exploration of Scsi",
// D. Deming, 1994, pg 7-19 (ISBN 1-879936-08-9)
static void ScsiReportLunsDone(Scsi_Cmnd *Cmnd)
{
  struct Scsi_Host *HostAdapter = Cmnd->device->host;
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  PFC_LOGGEDIN_PORT pLoggedInPort; 
  int LunListLen=0;
  int i;
  ULONG x_ID = 0xFFFFFFFF;
  UCHAR *ucBuff = Cmnd->request_buffer;

//  printk("cpqfcTS: ReportLunsDone \n");
  // first, we need to find the Exchange for this command,
  // so we can find the fcPort struct to make the indicated
  // changes.
  for( i=0; i< TACH_SEST_LEN; i++)
  {
    if( Exchanges->fcExchange[i].type   // exchange defined?
                   &&
       (Exchanges->fcExchange[i].Cmnd == Cmnd) ) // matches?
	      
    {
      x_ID = i;  // found exchange!
      break;
    }
  }
  if( x_ID == 0xFFFFFFFF)
  {
//    printk("cpqfcTS: ReportLuns failed - no FC Exchange\n");
    goto Done;  // Report Luns FC Exchange gone; 
                // exchange probably Terminated by Implicit logout
  }


  // search linked list for the port_id we sent INQUIRY to
  pLoggedInPort = fcFindLoggedInPort( fcChip,
    NULL,     // DON'T search Scsi Nexus (we will set it)
    Exchanges->fcExchange[ x_ID].fchs.d_id & 0xFFFFFF,        
    NULL,     // DON'T search linked list for FC WWN
    NULL);    // DON'T care about end of list
 
  if( !pLoggedInPort )
  {
//    printk("cpqfcTS: ReportLuns failed - device gone\n");
    goto Done; // error! can't find logged in Port
  }    
  LunListLen = ucBuff[3];
  LunListLen += ucBuff[2]>>8;

  if( !LunListLen )  // failed
  {
    // generically speaking, a soft error means we should retry...
    if( (Cmnd->result >> 16) == DID_SOFT_ERROR )
    {
      if( ((Cmnd->sense_buffer[2] & 0xF) == 0x6) &&
	        (Cmnd->sense_buffer[12] == 0x29) ) // Sense Code "reset"
      {
        TachFCHDR_GCMND *fchs = &Exchanges->fcExchange[ x_ID].fchs;
      // did we fail because of "check condition, device reset?"
      // e.g. the device was reset (i.e., at every power up)
      // retry the Report Luns
      
      // who are we sending it to?
      // we know this because we have a copy of the command
      // frame from the original Report Lun command -
      // switch the d_id/s_id fields, because the Exchange Build
      // context is "reply to source".
      
        fchs->s_id = fchs->d_id; // (temporarily re-use the struct)
        cpqfcTSPutLinkQue( cpqfcHBAdata, SCSI_REPORT_LUNS, fchs );
      }
    }
    else  // probably, the device doesn't support Report Luns
      pLoggedInPort->ScsiNexus.VolumeSetAddressing = 0;  
  }
  else  // we have LUN info - check VSA mode
  {
    // for now, assume all LUNs will have same addr mode
    // for VSA, payload byte 8 will be 0x40; otherwise, 0
    pLoggedInPort->ScsiNexus.VolumeSetAddressing = ucBuff[8];  
      
    // Since we got a Report Luns answer, set lun masking flag
    pLoggedInPort->ScsiNexus.LunMasking = 1;

    if( LunListLen > 8*CPQFCTS_MAX_LUN)   // We expect CPQFCTS_MAX_LUN max
      LunListLen = 8*CPQFCTS_MAX_LUN;

/*   
    printk("Device WWN %08X%08X Reports Luns @: ", 
          (ULONG)(pLoggedInPort->u.liWWN &0xFFFFFFFF), 
          (ULONG)(pLoggedInPort->u.liWWN>>32));
	    
    for( i=8; i<LunListLen+8; i+=8)
    {  
      printk("%02X%02X ", ucBuff[i], ucBuff[i+1] );
    }
    printk("\n");
*/
    
    // Since the device was kind enough to tell us where the
    // LUNs are, lets ensure they are contiguous for Linux's
    // SCSI driver scan, which expects them to start at 0.
    // Since Linux only supports 8 LUNs, only copy the first
    // eight from the report luns command

    // e.g., the Compaq RA4x00 f/w Rev 2.54 and above may report
    // LUNs 4001, 4004, etc., because other LUNs are masked from
    // this HBA (owned by someone else).  We'll make those appear as
    // LUN 0, 1... to Linux
    {
      int j;
      int AppendLunList = 0;
      // Walk through the LUN list.  The 'j' array number is
      // Linux's lun #, while the value of .lun[j] is the target's
      // lun #.
      // Once we build a LUN list, it's possible for a known device 
      // to go offline while volumes (LUNs) are added.  Later,
      // the device will do another PLOGI ... Report Luns command,
      // and we must not alter the existing Linux Lun map.
      // (This will be very rare).
      for( j=0; j < CPQFCTS_MAX_LUN; j++)
      {
        if( pLoggedInPort->ScsiNexus.lun[j] != 0xFF )
	{
	  AppendLunList = 1;
	  break;
	}
      }
      if( AppendLunList )
      {
	int k;
        int FreeLunIndex;
//        printk("cpqfcTS: AppendLunList\n");

	// If we get a new Report Luns, we cannot change
	// any existing LUN mapping! (Only additive entry)
	// For all LUNs in ReportLun list
	// if RL lun != ScsiNexus lun
	//   if RL lun present in ScsiNexus lun[], continue
	//   else find ScsiNexus lun[]==FF and add, continue
	
        for( i=8, j=0; i<LunListLen+8 && j< CPQFCTS_MAX_LUN; i+=8, j++)
	{
          if( pLoggedInPort->ScsiNexus.lun[j] != ucBuff[i+1] )
	  {
	    // something changed from the last Report Luns
	    printk(" cpqfcTS: Report Lun change!\n");
	    for( k=0, FreeLunIndex=CPQFCTS_MAX_LUN; 
                 k < CPQFCTS_MAX_LUN; k++)
            {
	      if( pLoggedInPort->ScsiNexus.lun[k] == 0xFF)
	      {
		FreeLunIndex = k;
		break;
	      }
              if( pLoggedInPort->ScsiNexus.lun[k] == ucBuff[i+1] )
		break; // we already masked this lun
	    }
	    if( k >= CPQFCTS_MAX_LUN )
	    {
	      printk(" no room for new LUN %d\n", ucBuff[i+1]);
	    }
	    else if( k == FreeLunIndex )  // need to add LUN
	    {
	      pLoggedInPort->ScsiNexus.lun[k] = ucBuff[i+1];
//	      printk("add [%d]->%02d\n", k, pLoggedInPort->ScsiNexus.lun[k]);
	      
	    }
	    else
	    {
	      // lun already known
	    }
	    break;
	  }
	}
	// print out the new list...
	for( j=0; j< CPQFCTS_MAX_LUN; j++)
	{
	  if( pLoggedInPort->ScsiNexus.lun[j] == 0xFF)
	    break; // done
//	  printk("[%d]->%02d ", j, pLoggedInPort->ScsiNexus.lun[j]);
	}
      }
      else
      {
//	  printk("Linux SCSI LUNs[] -> Device LUNs: ");
	// first time - this is easy
        for( i=8, j=0; i<LunListLen+8 && j< CPQFCTS_MAX_LUN; i+=8, j++)
	{
          pLoggedInPort->ScsiNexus.lun[j] = ucBuff[i+1];
//	  printk("[%d]->%02d ", j, pLoggedInPort->ScsiNexus.lun[j]);
	}
//	printk("\n");
      }
    }
  }

Done: ;
}

extern int is_private_data_of_cpqfc(CPQFCHBA *hba, void * pointer);
extern void cpqfc_free_private_data(CPQFCHBA *hba, cpqfc_passthru_private_t *data);

static void 
call_scsi_done(Scsi_Cmnd *Cmnd)
{
	CPQFCHBA *hba;
	hba = (CPQFCHBA *) Cmnd->device->host->hostdata;
	// Was this command a cpqfc passthru ioctl ?
        if (Cmnd->sc_request != NULL && Cmnd->device->host != NULL && 
		Cmnd->device->host->hostdata != NULL &&
		is_private_data_of_cpqfc((CPQFCHBA *) Cmnd->device->host->hostdata,
			Cmnd->sc_request->upper_private_data)) {
		cpqfc_free_private_data(hba, 
			Cmnd->sc_request->upper_private_data);	
		Cmnd->sc_request->upper_private_data = NULL;
		Cmnd->result &= 0xff00ffff;
		Cmnd->result |= (DID_PASSTHROUGH << 16);  // prevents retry
	}
	if (Cmnd->scsi_done != NULL)
		(*Cmnd->scsi_done)(Cmnd);
}

// After successfully getting a "Process Login" (PRLI) from an
// FC port, we want to Discover the LUNs so that we know the
// addressing type (e.g., FCP-SCSI Volume Set Address, Peripheral
// Unit Device), and whether SSP (Selective Storage Presentation or
// Lun Masking) has made the LUN numbers non-zero based or 
// non-contiguous.  To remain backward compatible with the SCSI-2
// driver model, which expects a contiguous LUNs starting at 0,
// will use the ReportLuns info to map from "device" to "Linux" 
// LUNs.
static void IssueReportLunsCommand( 
              CPQFCHBA* cpqfcHBAdata, 
	      TachFCHDR_GCMND* fchs)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  PFC_LOGGEDIN_PORT pLoggedInPort; 
  struct scsi_cmnd *Cmnd = NULL;
  struct scsi_device *ScsiDev = NULL;
  LONG x_ID;
  ULONG ulStatus;
  UCHAR *ucBuff;

  if( !cpqfcHBAdata->PortDiscDone) // cleared by LDn
  {
    printk("Discard Q'd ReportLun command\n");
    goto Done;
  }

  // find the device (from port_id) we're talking to
  pLoggedInPort = fcFindLoggedInPort( fcChip,
        NULL,     // DON'T search Scsi Nexus 
  	fchs->s_id & 0xFFFFFF,        
	NULL,     // DON'T search linked list for FC WWN
        NULL);    // DON'T care about end of list
  if( pLoggedInPort ) // we'd BETTER find it!
  {


    if( !(pLoggedInPort->fcp_info & TARGET_FUNCTION) )
      goto Done;  // forget it - FC device not a "target"

 
    ScsiDev = scsi_get_host_dev (cpqfcHBAdata->HostAdapter);
    if (!ScsiDev)
      goto Done;
    
    Cmnd = scsi_get_command (ScsiDev, GFP_KERNEL);
    if (!Cmnd) 
      goto Done;

    ucBuff = pLoggedInPort->ReportLunsPayload;
    
    memset( ucBuff, 0, REPORT_LUNS_PL);
    
    Cmnd->scsi_done = ScsiReportLunsDone;

    Cmnd->request_buffer = pLoggedInPort->ReportLunsPayload; 
    Cmnd->request_bufflen = REPORT_LUNS_PL; 
	    
    Cmnd->cmnd[0] = 0xA0;
    Cmnd->cmnd[8] = REPORT_LUNS_PL >> 8;
    Cmnd->cmnd[9] = (UCHAR)REPORT_LUNS_PL;
    Cmnd->cmd_len = 12;

    Cmnd->device->channel = pLoggedInPort->ScsiNexus.channel;
    Cmnd->device->id = pLoggedInPort->ScsiNexus.target;

	    
    ulStatus = cpqfcTSBuildExchange(
      cpqfcHBAdata,
      SCSI_IRE, 
      fchs,
      Cmnd,         // buffer for Report Lun data
      &x_ID );// fcController->fcExchanges index, -1 if failed

    if( !ulStatus ) // Exchange setup?
    {
      ulStatus = cpqfcTSStartExchange( cpqfcHBAdata, x_ID );
      if( !ulStatus )
      {
        // submitted to Tach's Outbound Que (ERQ PI incremented)
        // waited for completion for ELS type (Login frames issued
        // synchronously)
      }
      else
        // check reason for Exchange not being started - we might
        // want to Queue and start later, or fail with error
      {
  
      }
    }

    else   // Xchange setup failed...
      printk(" cpqfcTSBuildExchange failed: %Xh\n", ulStatus );
  }
  else     // like, we just got a PRLI ACC, and now the port is gone?
  {
    printk(" can't send ReportLuns - no login for port_id %Xh\n",
	    fchs->s_id & 0xFFFFFF);
  }



Done:

  if (Cmnd)
    scsi_put_command (Cmnd);
  if (ScsiDev) 
    scsi_free_host_dev (ScsiDev);
}







static void CompleteBoardLockCmnd( CPQFCHBA *cpqfcHBAdata)
{
  int i;
  for( i = CPQFCTS_REQ_QUEUE_LEN-1; i>= 0; i--)
  {
    if( cpqfcHBAdata->BoardLockCmnd[i] != NULL )
    {
      Scsi_Cmnd *Cmnd = cpqfcHBAdata->BoardLockCmnd[i];
      cpqfcHBAdata->BoardLockCmnd[i] = NULL;
      Cmnd->result = (DID_SOFT_ERROR << 16);  // ask for retry
//      printk(" BoardLockCmnd[%d] %p Complete, chnl/target/lun %d/%d/%d\n",
//        i,Cmnd, Cmnd->channel, Cmnd->target, Cmnd->lun);
      call_scsi_done(Cmnd);
    }
  }
}






// runs every 1 second for FC exchange timeouts and implicit FC device logouts

void cpqfcTSheartbeat( unsigned long ptr )
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)ptr;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  PFC_LOGGEDIN_PORT pLoggedInPort = &fcChip->fcPorts; 
  ULONG i;
  unsigned long flags;
  DECLARE_MUTEX_LOCKED(BoardLock);
  
  PCI_TRACE( 0xA8)

  if( cpqfcHBAdata->BoardLock) // Worker Task Running?
    goto Skip;

  // STOP _que function
  spin_lock_irqsave( cpqfcHBAdata->HostAdapter->host_lock, flags); 

  PCI_TRACE( 0xA8)


  cpqfcHBAdata->BoardLock = &BoardLock; // stop Linux SCSI command queuing
  
  // release the IO lock (and re-enable interrupts)
  spin_unlock_irqrestore( cpqfcHBAdata->HostAdapter->host_lock, flags);
  
  // Ensure no contention from  _quecommand or Worker process 
  CPQ_SPINLOCK_HBA( cpqfcHBAdata)
  
  PCI_TRACE( 0xA8)
  

  disable_irq( cpqfcHBAdata->HostAdapter->irq);  // our IRQ

  // Complete the "bad target" commands (normally only used during
  // initialization, since we aren't supposed to call "scsi_done"
  // inside the queuecommand() function).  (this is overly contorted,
  // scsi_done can be safely called from queuecommand for
  // this bad target case.  May want to simplify this later)

  for( i=0; i< CPQFCTS_MAX_TARGET_ID; i++)
  {
    if( cpqfcHBAdata->BadTargetCmnd[i] )
    {
      Scsi_Cmnd *Cmnd = cpqfcHBAdata->BadTargetCmnd[i];
      cpqfcHBAdata->BadTargetCmnd[i] = NULL;
      Cmnd->result = (DID_BAD_TARGET << 16);
      call_scsi_done(Cmnd);
    }
    else
      break;
  }

  
  // logged in ports -- re-login check (ports required to verify login with
  // PDISC after LIP within 2 secs)

  // prevent contention
  while( pLoggedInPort ) // for all ports which are expecting
                         // PDISC after the next LIP, check to see if
                         // time is up!
  {
      // Important: we only detect "timeout" condition on TRANSITION
      // from non-zero to zero
    if( pLoggedInPort->LOGO_timer )  // time-out "armed"?
    {
      if( !(--pLoggedInPort->LOGO_timer) ) // DEC from 1 to 0?
      {
          // LOGOUT time!  Per PLDA, PDISC hasn't complete in 2 secs, so
          // issue LOGO request and destroy all I/O with other FC port(s).
        
/*          
        printk(" ~cpqfcTS heartbeat: LOGOut!~ ");
        printk("Linux SCSI Chanl/Target %d/%d (port_id %06Xh) WWN %08X%08X\n", 
        pLoggedInPort->ScsiNexus.channel, 
        pLoggedInPort->ScsiNexus.target, 
	pLoggedInPort->port_id,
          (ULONG)(pLoggedInPort->u.liWWN &0xFFFFFFFF), 
          (ULONG)(pLoggedInPort->u.liWWN>>32));

*/
        cpqfcTSImplicitLogout( cpqfcHBAdata, pLoggedInPort);

      }
      // else simply decremented - maybe next time...
    }
    pLoggedInPort = pLoggedInPort->pNextPort;
  }



  
  
  // ************  FC EXCHANGE TIMEOUT CHECK **************
  
  for( i=0; i< TACH_MAX_XID; i++)
  {
    if( Exchanges->fcExchange[i].type )  // exchange defined?
    {

      if( !Exchanges->fcExchange[i].timeOut ) // time expired
      {
        // Set Exchange timeout status
        Exchanges->fcExchange[i].status |= FC2_TIMEOUT;

        if( i >= TACH_SEST_LEN ) // Link Service Exchange
        {
          cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, i);  // Don't "abort" LinkService
        }
        
        else  // SEST Exchange TO -- may post ABTS to Worker Thread Que
        {
	  // (Make sure we don't keep timing it out; let other functions
	  // complete it or set the timeOut as needed)
	  Exchanges->fcExchange[i].timeOut = 30000; // seconds default

          if( Exchanges->fcExchange[i].type 
                  & 
              (BLS_ABTS | BLS_ABTS_ACC )  )
          {
	    // For BLS_ABTS*, an upper level might still have
	    // an outstanding command waiting for low-level completion.
	    // Also, in the case of a WRITE, we MUST get confirmation
	    // of either ABTS ACC or RJT before re-using the Exchange.
	    // It's possible that the RAID cache algorithm can hang
	    // if we fail to complete a WRITE to a LBA, when a READ
	    // comes later to that same LBA.  Therefore, we must
	    // ensure that the target verifies receipt of ABTS for
	    // the exchange
	   
	    printk("~TO Q'd ABTS (x_ID %Xh)~ ", i); 
//            TriggerHBA( fcChip->Registers.ReMapMemBase);

	    // On timeout of a ABTS exchange, check to
	    // see if the FC device has a current valid login.
	    // If so, restart it.
	    pLoggedInPort = fcFindLoggedInPort( fcChip,
              Exchanges->fcExchange[i].Cmnd, // find Scsi Nexus
              0,        // DON'T search linked list for FC port id
	      NULL,     // DON'T search linked list for FC WWN
              NULL);    // DON'T care about end of list

	    // device exists?
	    if( pLoggedInPort ) // device exists?
	    {
	      if( pLoggedInPort->prli ) // logged in for FCP-SCSI?
	      {
		// attempt to restart the ABTS
		printk(" ~restarting ABTS~ ");
                cpqfcTSStartExchange( cpqfcHBAdata, i );

	      }
	    }
          }
	  else  // not an ABTS
	  { 
           
            // We expect the WorkerThread to change the xchng type to
            // abort and set appropriate timeout.
            cpqfcTSPutLinkQue( cpqfcHBAdata, BLS_ABTS, &i ); // timed-out
	  }
        }
      }
      else  // time not expired...
      {
        // decrement timeout: 1 or more seconds left
        --Exchanges->fcExchange[i].timeOut;
      }
    }
  }


  enable_irq( cpqfcHBAdata->HostAdapter->irq);
 

  CPQ_SPINUNLOCK_HBA( cpqfcHBAdata)

  cpqfcHBAdata->BoardLock = NULL; // Linux SCSI commands may be queued

  // Now, complete any Cmnd we Q'd up while BoardLock was held

  CompleteBoardLockCmnd( cpqfcHBAdata);
 

  // restart the timer to run again (1 sec later)
Skip:
  mod_timer( &cpqfcHBAdata->cpqfcTStimer, jiffies + HZ);
  
  PCI_TRACEO( i, 0xA8)
  return;
}


// put valid FC-AL physical address in spec order
static const UCHAR valid_al_pa[]={
    0xef, 0xe8, 0xe4, 0xe2, 
    0xe1, 0xE0, 0xDC, 0xDA, 
    0xD9, 0xD6, 0xD5, 0xD4, 
    0xD3, 0xD2, 0xD1, 0xCe, 
    0xCd, 0xCc, 0xCb, 0xCa, 
    0xC9, 0xC7, 0xC6, 0xC5, 
    0xC3, 0xBc, 0xBa, 0xB9,
    0xB6, 0xB5, 0xB4, 0xB3, 
    0xB2, 0xB1, 0xae, 0xad,
    0xAc, 0xAb, 0xAa, 0xA9, 

    0xA7, 0xA6, 0xA5, 0xA3, 
    0x9f, 0x9e, 0x9d, 0x9b, 
    0x98, 0x97, 0x90, 0x8f, 
    0x88, 0x84, 0x82, 0x81, 
    0x80, 0x7c, 0x7a, 0x79, 
    0x76, 0x75, 0x74, 0x73, 
    0x72, 0x71, 0x6e, 0x6d, 
    0x6c, 0x6b, 0x6a, 0x69, 
    0x67, 0x66, 0x65, 0x63, 
    0x5c, 0x5a, 0x59, 0x56, 
    
    0x55, 0x54, 0x53, 0x52, 
    0x51, 0x4e, 0x4d, 0x4c, 
    0x4b, 0x4a, 0x49, 0x47, 
    0x46, 0x45, 0x43, 0x3c,
    0x3a, 0x39, 0x36, 0x35, 
    0x34, 0x33, 0x32, 0x31, 
    0x2e, 0x2d, 0x2c, 0x2b, 
    0x2a, 0x29, 0x27, 0x26, 
    0x25, 0x23, 0x1f, 0x1E,
    0x1d, 0x1b, 0x18, 0x17, 

    0x10, 0x0f, 8, 4, 2, 1 }; // ALPA 0 (Fabric) is special case

const int number_of_al_pa = (sizeof(valid_al_pa) );



// this function looks up an al_pa from the table of valid al_pa's
// we decrement from the last decimal loop ID, because soft al_pa
// (our typical case) are assigned with highest priority (and high al_pa)
// first.  See "In-Depth FC-AL", R. Kembel pg. 38
// INPUTS:
//   al_pa - 24 bit port identifier (8 bit al_pa on private loop)
// RETURN:
//  Loop ID - serves are index to array of logged in ports
//  -1      - invalid al_pa (not all 8 bit values are legal)

#if (0)
static int GetLoopID( ULONG al_pa )
{
  int i;

  for( i = number_of_al_pa -1; i >= 0; i--)  // dec.
  {
    if( valid_al_pa[i] == (UCHAR)al_pa )  // take lowest 8 bits
      return i;  // success - found valid al_pa; return decimal LoopID
  }
  return -1; // failed - not found
}
#endif

extern cpqfc_passthru_private_t *cpqfc_private(Scsi_Request *sr);

// Search the singly (forward) linked list "fcPorts" looking for 
// either the SCSI target (if != -1), port_id (if not NULL), 
// or WWN (if not null), in that specific order.
// If we find a SCSI nexus (from Cmnd arg), set the SCp.phase
// field according to VSA or PDU
// RETURNS:
//   Ptr to logged in port struct if found
//     (NULL if not found)
//   pLastLoggedInPort - ptr to last struct (for adding new ones)
// 
PFC_LOGGEDIN_PORT  fcFindLoggedInPort( 
  PTACHYON fcChip, 
  Scsi_Cmnd *Cmnd, // search linked list for Scsi Nexus (channel/target/lun)
  ULONG port_id,   // search linked list for al_pa, or
  UCHAR wwn[8],    // search linked list for WWN, or...
  PFC_LOGGEDIN_PORT *pLastLoggedInPort )
             
{
  PFC_LOGGEDIN_PORT pLoggedInPort = &fcChip->fcPorts; 
  BOOLEAN target_id_valid=FALSE;
  BOOLEAN port_id_valid=FALSE;
  BOOLEAN wwn_valid=FALSE;
  int i;


  if( Cmnd != NULL )
    target_id_valid = TRUE;
  
  else if( port_id ) // note! 24-bit NULL address is illegal
    port_id_valid = TRUE;

  else
  {
    if( wwn ) // non-null arg? (OK to pass NULL when not searching WWN)
    {
      for( i=0; i<8; i++)  // valid WWN passed?  NULL WWN invalid
      {
        if( wwn[i] != 0 )
          wwn_valid = TRUE;  // any non-zero byte makes (presumably) valid
      }
    }
  }
                // check other options ...


  // In case multiple search options are given, we use a priority
  // scheme:
  // While valid pLoggedIn Ptr
  //   If port_id is valid
  //     if port_id matches, return Ptr
  //   If wwn is valid
  //     if wwn matches, return Ptr
  //   Next Ptr in list
  //
  // Return NULL (not found)
 
      
  while( pLoggedInPort ) // NULL marks end of list (1st ptr always valid)
  {
    if( pLastLoggedInPort ) // caller's pointer valid?
      *pLastLoggedInPort = pLoggedInPort;  // end of linked list
    
    if( target_id_valid )
    {
      // check Linux Scsi Cmnd for channel/target Nexus match
      // (all luns are accessed through matching "pLoggedInPort")
      if( (pLoggedInPort->ScsiNexus.target == Cmnd->device->id)
                &&
          (pLoggedInPort->ScsiNexus.channel == Cmnd->device->channel))
      {
        // For "passthru" modes, the IOCTL caller is responsible
	// for setting the FCP-LUN addressing
	if (Cmnd->sc_request != NULL && Cmnd->device->host != NULL && 
		Cmnd->device->host->hostdata != NULL &&
		is_private_data_of_cpqfc((CPQFCHBA *) Cmnd->device->host->hostdata,
			Cmnd->sc_request->upper_private_data)) { 
		/* This is a passthru... */
		cpqfc_passthru_private_t *pd;
		pd = Cmnd->sc_request->upper_private_data;
        	Cmnd->SCp.phase = pd->bus;
		// Cmnd->SCp.have_data_in = pd->pdrive;
		Cmnd->SCp.have_data_in = Cmnd->device->lun;
	} else {
	  /* This is not a passthru... */
	
          // set the FCP-LUN addressing type
          Cmnd->SCp.phase = pLoggedInPort->ScsiNexus.VolumeSetAddressing;	

          // set the Device Type we got from the snooped INQUIRY string
	  Cmnd->SCp.Message = pLoggedInPort->ScsiNexus.InqDeviceType;

	  // handle LUN masking; if not "default" (illegal) lun value,
	  // the use it.  These lun values are set by a successful
	  // Report Luns command
          if( pLoggedInPort->ScsiNexus.LunMasking == 1) 
	  {
	    if (Cmnd->device->lun > sizeof(pLoggedInPort->ScsiNexus.lun))
		return NULL;
            // we KNOW all the valid LUNs... 0xFF is invalid!
            Cmnd->SCp.have_data_in = pLoggedInPort->ScsiNexus.lun[Cmnd->device->lun];
	    if (pLoggedInPort->ScsiNexus.lun[Cmnd->device->lun] == 0xFF)
		return NULL;
	    // printk("xlating lun %d to 0x%02x\n", Cmnd->lun, 
            //	pLoggedInPort->ScsiNexus.lun[Cmnd->lun]);
	  }
	  else
	    Cmnd->SCp.have_data_in = Cmnd->device->lun; // Linux & target luns match
	}
	break; // found it!
      }
    }
    
    if( port_id_valid ) // look for alpa first
    {
      if( pLoggedInPort->port_id == port_id )
          break;  // found it!
    }
    if( wwn_valid ) // look for wwn second
    {

      if( !memcmp( &pLoggedInPort->u.ucWWN[0], &wwn[0], 8))
      {  
                 // all 8 bytes of WWN match
        break;   // found it!
      }
    }
                
    pLoggedInPort = pLoggedInPort->pNextPort; // try next port
  }

  return pLoggedInPort;
}




// 
// We need to examine the SEST table and re-validate
// any open Exchanges for this LoggedInPort
// To make Tachyon pay attention, Freeze FCP assists,
// set VAL bits, Unfreeze FCP assists
static void RevalidateSEST( struct Scsi_Host *HostAdapter, 
		        PFC_LOGGEDIN_PORT pLoggedInPort)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG x_ID;
  BOOLEAN TachFroze = FALSE;
  
  
  // re-validate any SEST exchanges that are permitted
  // to survive the link down (e.g., good PDISC performed)
  for( x_ID = 0; x_ID < TACH_SEST_LEN; x_ID++)
  {

    // If the SEST entry port_id matches the pLoggedInPort,
    // we need to re-validate
    if( (Exchanges->fcExchange[ x_ID].type == SCSI_IRE)
         || 
	(Exchanges->fcExchange[ x_ID].type == SCSI_IWE))
    {
		     
      if( (Exchanges->fcExchange[ x_ID].fchs.d_id & 0xFFFFFF)  // (24-bit port ID)
            == pLoggedInPort->port_id) 
      {
//	printk(" re-val xID %Xh ", x_ID);
        if( !TachFroze )  // freeze if not already frozen
          TachFroze |= FreezeTach( cpqfcHBAdata);
        fcChip->SEST->u[ x_ID].IWE.Hdr_Len |= 0x80000000; // set VAL bit
      }
    } 
  }

  if( TachFroze) 
  { 
    fcChip->UnFreezeTachyon( fcChip, 2);  // both ERQ and FCP assists
  }
} 


// Complete an Linux Cmnds that we Queued because
// our FC link was down (cause immediate retry)

static void UnblockScsiDevice( struct Scsi_Host *HostAdapter, 
		        PFC_LOGGEDIN_PORT pLoggedInPort)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata;
  Scsi_Cmnd* *SCptr = &cpqfcHBAdata->LinkDnCmnd[0];
  Scsi_Cmnd *Cmnd;
  int indx;

 
  
  // if the device was previously "blocked", make sure
  // we unblock it so Linux SCSI will resume

  pLoggedInPort->device_blocked = FALSE; // clear our flag

  // check the Link Down command ptr buffer;
  // we can complete now causing immediate retry
  for( indx=0; indx < CPQFCTS_REQ_QUEUE_LEN; indx++, SCptr++)
  {
    if( *SCptr != NULL ) // scsi command to complete?
    {
#ifdef DUMMYCMND_DBG
      printk("complete Cmnd %p in LinkDnCmnd[%d]\n", *SCptr,indx);
#endif
      Cmnd = *SCptr;


      // Are there any Q'd commands for this target?
      if( (Cmnd->device->id == pLoggedInPort->ScsiNexus.target)
	       &&
          (Cmnd->device->channel == pLoggedInPort->ScsiNexus.channel) )
      {
        Cmnd->result = (DID_SOFT_ERROR <<16); // force retry
        if( Cmnd->scsi_done == NULL) 
	{
          printk("LinkDnCmnd scsi_done ptr null, port_id %Xh\n",
		  pLoggedInPort->port_id);
	}
	else
	  call_scsi_done(Cmnd);
        *SCptr = NULL;  // free this slot for next use
      }
    }
  }
}

  
//#define WWN_DBG 1

static void SetLoginFields(
  PFC_LOGGEDIN_PORT pLoggedInPort,
  TachFCHDR_GCMND* fchs,
  BOOLEAN PDisc,
  BOOLEAN Originator)
{
  LOGIN_PAYLOAD logi;       // FC-PH Port Login
  PRLI_REQUEST prli;  // copy for BIG ENDIAN switch
  int i;
#ifdef WWN_DBG
  ULONG ulBuff;
#endif

  BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&logi, sizeof(logi));

  pLoggedInPort->Originator = Originator;
  pLoggedInPort->port_id = fchs->s_id & 0xFFFFFF;
  
  switch( fchs->pl[0] & 0xffff )
  {
  case 0x00000002:  //  PLOGI or PDISC ACCept?
    if( PDisc )     // PDISC accept
      goto PDISC_case;

  case 0x00000003:  //  ELS_PLOGI or ELS_PLOGI_ACC

  // Login BB_credit typically 0 for Tachyons
    pLoggedInPort->BB_credit = logi.cmn_services.bb_credit;

    // e.g. 128, 256, 1024, 2048 per FC-PH spec
    // We have to use this when setting up SEST Writes,
    // since that determines frame size we send.
    pLoggedInPort->rx_data_size = logi.class3.rx_data_size;
    pLoggedInPort->plogi = TRUE;
    pLoggedInPort->pdisc = FALSE;
    pLoggedInPort->prli = FALSE;    // ELS_PLOGI resets
    pLoggedInPort->flogi = FALSE;   // ELS_PLOGI resets
    pLoggedInPort->logo = FALSE;    // ELS_PLOGI resets
    pLoggedInPort->LOGO_counter = 0;// ELS_PLOGI resets
    pLoggedInPort->LOGO_timer = 0;// ELS_PLOGI resets

    // was this PLOGI to a Fabric?
    if( pLoggedInPort->port_id == 0xFFFFFC ) // well know address
      pLoggedInPort->flogi = TRUE;


    for( i=0; i<8; i++)   // copy the LOGIN port's WWN
      pLoggedInPort->u.ucWWN[i] = logi.port_name[i];  

#ifdef WWN_DBG
    ulBuff = (ULONG)pLoggedInPort->u.liWWN;
    if( pLoggedInPort->Originator)
      printk("o");
    else
      printk("r");
    printk("PLOGI port_id %Xh, WWN %08X",
      pLoggedInPort->port_id, ulBuff);

    ulBuff = (ULONG)(pLoggedInPort->u.liWWN >> 32);
    printk("%08Xh fcPort %p\n", ulBuff, pLoggedInPort);
#endif
    break;



  
  case 0x00000005:  //  ELS_LOGO (logout)


    pLoggedInPort->plogi = FALSE;
    pLoggedInPort->pdisc = FALSE;
    pLoggedInPort->prli = FALSE;   // ELS_PLOGI resets
    pLoggedInPort->flogi = FALSE;  // ELS_PLOGI resets
    pLoggedInPort->logo = TRUE;    // ELS_PLOGI resets
    pLoggedInPort->LOGO_counter++; // ELS_PLOGI resets
    pLoggedInPort->LOGO_timer = 0;
#ifdef WWN_DBG
    ulBuff = (ULONG)pLoggedInPort->u.liWWN;
    if( pLoggedInPort->Originator)
      printk("o");
    else
      printk("r");
    printk("LOGO port_id %Xh, WWN %08X",
      pLoggedInPort->port_id, ulBuff);

    ulBuff = (ULONG)(pLoggedInPort->u.liWWN >> 32);
    printk("%08Xh\n", ulBuff);
#endif
    break;



PDISC_case:
  case 0x00000050: //  ELS_PDISC or ELS_PDISC_ACC
    pLoggedInPort->LOGO_timer = 0;  // stop the time-out
      
    pLoggedInPort->prli = TRUE;     // ready to accept FCP-SCSI I/O
    

      
#ifdef WWN_DBG
    ulBuff = (ULONG)pLoggedInPort->u.liWWN;
    if( pLoggedInPort->Originator)
      printk("o");
    else
      printk("r");
    printk("PDISC port_id %Xh, WWN %08X",
      pLoggedInPort->port_id, ulBuff);

    ulBuff = (ULONG)(pLoggedInPort->u.liWWN >> 32);
    printk("%08Xh\n", ulBuff);
#endif


    
    break;


    
  case  0x1020L: //  PRLI?
  case  0x1002L: //  PRLI ACCept?
    BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&prli, sizeof(prli));

    pLoggedInPort->fcp_info = prli.fcp_info; // target/initiator flags
    pLoggedInPort->prli = TRUE;  // PLOGI resets, PDISC doesn't

    pLoggedInPort->pdisc = TRUE;  // expect to send (or receive) PDISC
                                  // next time
    pLoggedInPort->LOGO_timer = 0;  // will be set next LinkDown
#ifdef WWN_DBG
    ulBuff = (ULONG)pLoggedInPort->u.liWWN;
    if( pLoggedInPort->Originator)
      printk("o");
    else
      printk("r");
    printk("PRLI port_id %Xh, WWN %08X",
      pLoggedInPort->port_id, ulBuff);

    ulBuff = (ULONG)(pLoggedInPort->u.liWWN >> 32);
      printk("%08Xh\n", ulBuff);
#endif

    break;

  }

  return;
}






static void BuildLinkServicePayload( PTACHYON fcChip, ULONG type, void* payload)
{
  LOGIN_PAYLOAD *plogi;  // FC-PH Port Login
  LOGIN_PAYLOAD PlogiPayload;   // copy for BIG ENDIAN switch
  PRLI_REQUEST  *prli;          // FCP-SCSI Process Login
  PRLI_REQUEST  PrliPayload;    // copy for BIG ENDIAN switch
  LOGOUT_PAYLOAD  *logo;
  LOGOUT_PAYLOAD  LogoutPayload;
//  PRLO_REQUEST  *prlo;
//  PRLO_REQUEST  PrloPayload;
  REJECT_MESSAGE rjt, *prjt;

  memset( &PlogiPayload, 0, sizeof( PlogiPayload));
  plogi = &PlogiPayload;    // load into stack buffer,
                                // then BIG-ENDIAN switch a copy to caller


  switch( type )  // payload type can be ELS_PLOGI, ELS_PRLI, ADISC, ...
  {
    case ELS_FDISC:
    case ELS_FLOGI:
    case ELS_PLOGI_ACC:   // FC-PH PORT Login Accept
    case ELS_PLOGI:   // FC-PH PORT Login
    case ELS_PDISC:   // FC-PH2 Port Discovery - same payload as ELS_PLOGI
      plogi->login_cmd = LS_PLOGI;
      if( type == ELS_PDISC)
        plogi->login_cmd = LS_PDISC;
      else if( type == ELS_PLOGI_ACC )
        plogi->login_cmd = LS_ACC;

      plogi->cmn_services.bb_credit = 0x00;
      plogi->cmn_services.lowest_ver = fcChip->lowest_FCPH_ver;
      plogi->cmn_services.highest_ver = fcChip->highest_FCPH_ver;
      plogi->cmn_services.bb_rx_size = TACHLITE_TS_RX_SIZE;
      plogi->cmn_services.common_features = CONTINUOSLY_INCREASING |
              RANDOM_RELATIVE_OFFSET;

             // fill in with World Wide Name based Port Name - 8 UCHARs
             // get from Tach registers WWN hi & lo
      LoadWWN( fcChip, plogi->port_name, 0);
             // fill in with World Wide Name based Node/Fabric Name - 8 UCHARs
             // get from Tach registers WWN hi & lo
      LoadWWN( fcChip, plogi->node_name, 1);

	// For Seagate Drives.
	//
      plogi->cmn_services.common_features |= 0x800;
      plogi->cmn_services.rel_offset = 0xFE;
      plogi->cmn_services.concurrent_seq = 1;
      plogi->class1.service_options = 0x00;
      plogi->class2.service_options = 0x00;
      plogi->class3.service_options = CLASS_VALID;
      plogi->class3.initiator_control = 0x00;
      plogi->class3.rx_data_size = MAX_RX_PAYLOAD;
      plogi->class3.recipient_control =
             ERROR_DISCARD | ONE_CATEGORY_SEQUENCE;
      plogi->class3.concurrent_sequences = 1;
      plogi->class3.open_sequences = 1;
      plogi->vendor_id[0] = 'C'; plogi->vendor_id[1] = 'Q';
      plogi->vendor_version[0] = 'C'; plogi->vendor_version[1] = 'Q';
      plogi->vendor_version[2] = ' '; plogi->vendor_version[3] = '0';
      plogi->vendor_version[4] = '0'; plogi->vendor_version[5] = '0';


      // FLOGI specific fields... (see FC-FLA, Rev 2.7, Aug 1999, sec 5.1)
      if( (type == ELS_FLOGI) || (type == ELS_FDISC) )
      {
        if( type == ELS_FLOGI )
  	  plogi->login_cmd = LS_FLOGI;  
	else
  	  plogi->login_cmd = LS_FDISC;  

        plogi->cmn_services.lowest_ver = 0x20;
        plogi->cmn_services.common_features = 0x0800;
        plogi->cmn_services.rel_offset = 0;
        plogi->cmn_services.concurrent_seq = 0;

        plogi->class3.service_options = 0x8800;
        plogi->class3.rx_data_size = 0;
        plogi->class3.recipient_control = 0;
        plogi->class3.concurrent_sequences = 0;
        plogi->class3.open_sequences = 0;
      }
      
              // copy back to caller's buff, w/ BIG ENDIAN swap
      BigEndianSwap( (UCHAR*)&PlogiPayload, payload,  sizeof(PlogiPayload));
      break;

    
    case ELS_ACC:       // generic Extended Link Service ACCept	    
      plogi->login_cmd = LS_ACC;
              // copy back to caller's buff, w/ BIG ENDIAN swap
      BigEndianSwap( (UCHAR*)&PlogiPayload, payload,  4);
      break;


      
    case ELS_SCR:    // Fabric State Change Registration
    {
      SCR_PL scr;     // state change registration

      memset( &scr, 0, sizeof(scr));

      scr.command = LS_SCR;  // 0x62000000
                             // see FC-FLA, Rev 2.7, Table A.22 (pg 82)
      scr.function = 3;      // 1 = Events detected by Fabric
                             // 2 = N_Port detected registration
                             // 3 = Full registration
      
      // copy back to caller's buff, w/ BIG ENDIAN swap
      BigEndianSwap( (UCHAR*)&scr, payload,  sizeof(SCR_PL));
    }
    
    break;

    
    case FCS_NSR:    // Fabric Name Service Request
    {
      NSR_PL nsr;    // Name Server Req. payload

      memset( &nsr, 0, sizeof(NSR_PL));

                             // see Brocade Fabric Programming Guide,
                             // Rev 1.3, pg 4-44
      nsr.CT_Rev = 0x01000000;
      nsr.FCS_Type = 0xFC020000;
      nsr.Command_code = 0x01710000;
      nsr.FCP = 8;
     
      // copy back to caller's buff, w/ BIG ENDIAN swap
      BigEndianSwap( (UCHAR*)&nsr, payload,  sizeof(NSR_PL));
    }
    
    break;



    
    case ELS_LOGO:   // FC-PH PORT LogOut
      logo = &LogoutPayload;    // load into stack buffer,
                                // then BIG-ENDIAN switch a copy to caller
      logo->cmd = LS_LOGO;
                                // load the 3 UCHARs of the node name
                                // (if private loop, upper two UCHARs 0)
      logo->reserved = 0;

      logo->n_port_identifier[0] = (UCHAR)(fcChip->Registers.my_al_pa);
      logo->n_port_identifier[1] =
                     (UCHAR)(fcChip->Registers.my_al_pa>>8);
      logo->n_port_identifier[2] =
                     (UCHAR)(fcChip->Registers.my_al_pa>>16);
             // fill in with World Wide Name based Port Name - 8 UCHARs
             // get from Tach registers WWN hi & lo
      LoadWWN( fcChip, logo->port_name, 0);

      BigEndianSwap( (UCHAR*)&LogoutPayload,
                     payload,  sizeof(LogoutPayload) );  // 16 UCHAR struct
      break;


    case ELS_LOGO_ACC:     // Logout Accept (FH-PH pg 149, table 74)
      logo = &LogoutPayload;    // load into stack buffer,
                                // then BIG-ENDIAN switch a copy to caller
      logo->cmd = LS_ACC;
      BigEndianSwap( (UCHAR*)&LogoutPayload, payload, 4 );  // 4 UCHAR cmnd
      break;
      

    case ELS_RJT:          // ELS_RJT link service reject (FH-PH pg 155)

      prjt = (REJECT_MESSAGE*)payload;  // pick up passed data
      rjt.command_code = ELS_RJT;
                       // reverse fields, because of Swap that follows...
      rjt.vendor = prjt->reserved; // vendor specific
      rjt.explain = prjt->reason; //
      rjt.reason = prjt->explain; //
      rjt.reserved = prjt->vendor; //
                       // BIG-ENDIAN switch a copy to caller
      BigEndianSwap( (UCHAR*)&rjt, payload, 8 );  // 8 UCHAR cmnd
      break;





    case ELS_PRLI_ACC:  // Process Login ACCept
    case ELS_PRLI:  // Process Login
    case ELS_PRLO:  // Process Logout
      memset( &PrliPayload, 0, sizeof( PrliPayload));
      prli = &PrliPayload;      // load into stack buffer,

      if( type == ELS_PRLI )
        prli->cmd = 0x20;  // Login
      else if( type == ELS_PRLO )
        prli->cmd = 0x21;  // Logout
      else if( type == ELS_PRLI_ACC )
      {
        prli->cmd = 0x02;  // Login ACCept
        prli->valid = REQUEST_EXECUTED;
      }


      prli->valid |= SCSI_FCP | ESTABLISH_PAIR;
      prli->fcp_info = READ_XFER_RDY;
      prli->page_length = 0x10;
      prli->payload_length = 20;
                                // Can be initiator AND target

      if( fcChip->Options.initiator )
        prli->fcp_info |= INITIATOR_FUNCTION;
      if( fcChip->Options.target )
        prli->fcp_info |= TARGET_FUNCTION;

      BigEndianSwap( (UCHAR*)&PrliPayload, payload,  prli->payload_length);
      break;



    default:  // no can do - programming error
      printk(" BuildLinkServicePayload unknown!\n");
      break;
  }
}

// loads 8 UCHARs for PORT name or NODE name base on
// controller's WWN.
void LoadWWN( PTACHYON fcChip, UCHAR* dest, UCHAR type)
{
  UCHAR* bPtr, i;

  switch( type )
  {
    case 0:  // Port_Name
      bPtr = (UCHAR*)&fcChip->Registers.wwn_hi;
      for( i =0; i<4; i++)
        dest[i] = *bPtr++;
      bPtr = (UCHAR*)&fcChip->Registers.wwn_lo;
      for( i =4; i<8; i++)
        dest[i] = *bPtr++;
      break;
    case 1:  // Node/Fabric _Name
      bPtr = (UCHAR*)&fcChip->Registers.wwn_hi;
      for( i =0; i<4; i++)
        dest[i] = *bPtr++;
      bPtr = (UCHAR*)&fcChip->Registers.wwn_lo;
      for( i =4; i<8; i++)
        dest[i] = *bPtr++;
      break;
  }
  
}



// We check the Port Login payload for required values.  Note that
// ELS_PLOGI and ELS_PDISC (Port DISCover) use the same payload.


int verify_PLOGI( PTACHYON fcChip,
                  TachFCHDR_GCMND* fchs, 
                  ULONG* reject_explain)
{
  LOGIN_PAYLOAD	login;

                  // source, dest, len (should be mult. of 4)
  BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&login,  sizeof(login));

                            // check FC version
                            // if other port's highest supported version
                            // is less than our lowest, and 
                            // if other port's lowest
  if( login.cmn_services.highest_ver < fcChip->lowest_FCPH_ver ||
      login.cmn_services.lowest_ver > fcChip->highest_FCPH_ver )
  {
    *reject_explain = LS_RJT_REASON( LOGICAL_ERROR, OPTIONS_ERROR);
    return LOGICAL_ERROR;
  }

                            // Receive Data Field Size must be >=128
                            // per FC-PH
  if (login.cmn_services.bb_rx_size < 128)
  {
    *reject_explain = LS_RJT_REASON( LOGICAL_ERROR, DATA_FIELD_SIZE_ERROR);
    return LOGICAL_ERROR;
  }

                            // Only check Class 3 params
  if( login.class3.service_options & CLASS_VALID)
  {
    if (login.class3.rx_data_size < 128)
    {
      *reject_explain = LS_RJT_REASON( LOGICAL_ERROR, INVALID_CSP);
      return LOGICAL_ERROR;
    }
    if( login.class3.initiator_control & XID_REQUIRED)
    {
      *reject_explain = LS_RJT_REASON( LOGICAL_ERROR, INITIATOR_CTL_ERROR);
      return LOGICAL_ERROR;
    }
  }
  return 0;   // success
}




int verify_PRLI( TachFCHDR_GCMND* fchs, ULONG* reject_explain)
{
  PRLI_REQUEST prli;  // buffer for BIG ENDIAN

                  // source, dest, len (should be mult. of 4)
  BigEndianSwap( (UCHAR*)&fchs->pl[0], (UCHAR*)&prli,  sizeof(prli));

  if( prli.fcp_info == 0 )  // i.e., not target or initiator?
  {
    *reject_explain = LS_RJT_REASON( LOGICAL_ERROR, OPTIONS_ERROR);
    return LOGICAL_ERROR;
  }

  return 0;  // success
}


// SWAP UCHARs as required by Fibre Channel (i.e. BIG ENDIAN)
// INPUTS:
//   source   - ptr to LITTLE ENDIAN ULONGS
//   cnt      - number of UCHARs to switch (should be mult. of ULONG)
// OUTPUTS:
//   dest     - ptr to BIG ENDIAN copy
// RETURN:
//   none
//
void BigEndianSwap( UCHAR *source, UCHAR *dest,  USHORT cnt)
{
  int i,j;

  source+=3;   // start at MSB of 1st ULONG
  for( j=0; j < cnt; j+=4, source+=4, dest+=4)  // every ULONG
  {
    for( i=0; i<4; i++)  // every UCHAR in ULONG
          *(dest+i) = *(source-i);
  }
}




// Build FC Exchanges............

static void  buildFCPstatus( 
  PTACHYON fcChip, 
  ULONG ExchangeID);

static LONG FindFreeExchange( PTACHYON fcChip, ULONG type );

static ULONG build_SEST_sgList( 
  struct pci_dev *pcidev,
  ULONG *SESTalPairStart,
  Scsi_Cmnd *Cmnd,
  ULONG *sgPairs,
  PSGPAGES *sgPages_head  // link list of TL Ext. S/G pages from O/S Pool
);

static int build_FCP_payload( Scsi_Cmnd *Cmnd, 
  UCHAR* payload, ULONG type, ULONG fcp_dl );


/*
                             IRB
      ERQ           __________________
  |          |   / | Req_A_SFS_Len    |        ____________________
  |----------|  /  | Req_A_SFS_Addr   |------->|  Reserved         |
  |   IRB    | /   | Req_A_D_ID       |        | SOF EOF TimeStamp |
  |-----------/    | Req_A_SEST_Index |-+      | R_CTL |   D_ID    |
  |   IRB    |     | Req_B...         | |      | CS_CTL|   S_ID    | 
  |-----------\    |                  | |      | TYPE  |   F_CTL   |
  |   IRB    | \   |                  | |      | SEQ_ID  | SEQ_CNT |
  |-----------  \  |                  | +-->+--| OX_ID   | RX_ID   |
  |          |   \ |__________________|     |  |       RO          |
                                            |  | pl (payload/cmnd) |
                                            |  |        .....      |
                                            |  |___________________|
                                            |
                                            |
+-------------------------------------------+
|
|
|                        e.g. IWE    
|    SEST           __________________             for FCP_DATA
| |          |   / |       | Hdr_Len  |        ____________________
| |----------|  /  |  Hdr_Addr_Addr   |------->|  Reserved         |
| |   [0]    | /   |Remote_ID| RSP_Len|        | SOF EOF TimeStamp |
| |-----------/    |   RSP_Addr       |---+    | R_CTL |   D_ID    |
+->   [1]    |     |       | Buff_Off |   |    | CS_CTL|   S_ID    | 
  |-----------\    |BuffIndex| Link   |   |    | TYPE  |   F_CTL   |
  |   [2]    | \   | Rsvd  |   RX_ID  |   |    | SEQ_ID  | SEQ_CNT |
  |-----------  \  |    Data_Len      |   |    | OX_ID   | RX_ID   |
  |    ...   |   \ |     Exp_RO       |   |    |       RO          |
  |----------|     |   Exp_Byte_Cnt   |   |    |___________________|
  | SEST_LEN |  +--|    Len           |   |                                             
  |__________|  |  |   Address        |   |                                              
                |  |    ...           |   |         for FCP_RSP  
                |  |__________________|   |    ____________________
                |                         +----|  Reserved         |   
                |                              | SOF EOF TimeStamp |
                |                              | R_CTL |   D_ID    |
                |                              | CS_CTL|   S_ID    | 
                +--- local or extended         |     ....          |
                     scatter/gather lists
                     defining upper-layer
                     data (e.g. from user's App)


*/
// All TachLite commands must start with a SFS (Single Frame Sequence)
// command.  In the simplest case (a NOP Basic Link command),
// only one frame header and ERQ entry is required.  The most complex
// case is the SCSI assisted command, which requires an ERQ entry,
// SEST entry, and several frame headers and data buffers all
// logically linked together.
// Inputs:
//   cpqfcHBAdata  - controller struct
//   type          - PLOGI, SCSI_IWE, etc.
//   InFCHS        - Incoming Tachlite FCHS which prompted this exchange
//                   (only s_id set if we are originating)
//   Data          - PVOID to data struct consistent with "type"
//   fcExchangeIndex - pointer to OX/RD ID value of built exchange
// Return:
//   fcExchangeIndex - OX/RD ID value if successful
//   0    - success
//  INVALID_ARGS    - NULL/ invalid passed args
//  BAD_ALPA        - Bad source al_pa address
//  LNKDWN_OSLS     - Link Down (according to this controller)
//  OUTQUE_FULL     - Outbound Que full
//  DRIVERQ_FULL    - controller's Exchange array full
//  SEST_FULL       - SEST table full
//
// Remarks:
// Psuedo code:
// Check for NULL pointers / bad args
// Build outgoing FCHS - the header/payload struct
// Build IRB (for ERQ entry)
// if SCSI command, build SEST entry (e.g. IWE, TRE,...)
// return success

//sbuildex
ULONG cpqfcTSBuildExchange(
  CPQFCHBA *cpqfcHBAdata,
  ULONG type, // e.g. PLOGI
  TachFCHDR_GCMND* InFCHS,  // incoming FCHS
  void *Data,               // the CDB, scatter/gather, etc.  
  LONG *fcExchangeIndex )   // points to allocated exchange, 
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG ulStatus = 0;  // assume OK
  USHORT ox_ID, rx_ID=0xFFFF;
  ULONG SfsLen=0L;
  TachLiteIRB* pIRB;
  IRBflags IRB_flags;
  UCHAR *pIRB_flags = (UCHAR*)&IRB_flags;
  TachFCHDR_GCMND* CMDfchs;
  TachFCHDR* dataHDR;     // 32 byte HEADER ONLY FCP-DATA buffer
  TachFCHDR_RSP* rspHDR;     // 32 byte header + RSP payload
  Scsi_Cmnd *Cmnd = (Scsi_Cmnd*)Data;   // Linux Scsi CDB, S/G, ...
  TachLiteIWE* pIWE;
  TachLiteIRE* pIRE;
  TachLiteTWE* pTWE;
  TachLiteTRE* pTRE;
  ULONG fcp_dl;           // total byte length of DATA transferred
  ULONG fl;               // frame length (FC frame size, 128, 256, 512, 1024)
  ULONG sgPairs;          // number of valid scatter/gather pairs
  int FCP_SCSI_command;
  BA_ACC_PAYLOAD *ba_acc;
  BA_RJT_PAYLOAD *ba_rjt;

                          // check passed ARGS
  if( !fcChip->ERQ )      // NULL ptr means uninitialized Tachlite chip
    return INVALID_ARGS;


  if( type == SCSI_IRE ||
      type == SCSI_TRE ||
      type == SCSI_IWE ||
      type == SCSI_TWE)
    FCP_SCSI_command = 1;

  else
    FCP_SCSI_command = 0;


                     // for commands that pass payload data (e.g. SCSI write)
                     // examine command struct - verify that the
                     // length of s/g buffers is adequate for total payload
                     // length (end of list is NULL address)

  if( FCP_SCSI_command )
  {
    if( Data )     // must have data descriptor (S/G list -- at least
                   // one address with at least 1 byte of data)
    {
      // something to do (later)?
    }

    else
      return INVALID_ARGS;  // invalid DATA ptr
  }

    

         // we can build an Exchange for later Queuing (on the TL chip)
         // if an empty slot is available in the DevExt for this controller
         // look for available Exchange slot...

  if( type != FCP_RESPONSE &&
      type != BLS_ABTS &&
      type != BLS_ABTS_ACC )  // already have Exchange slot!
    *fcExchangeIndex = FindFreeExchange( fcChip, type );

  if( *fcExchangeIndex != -1 )   // Exchange is available?
  {
                     // assign tmp ptr (shorthand)
    CMDfchs = &Exchanges->fcExchange[ *fcExchangeIndex].fchs; 

    if( Cmnd != NULL ) // (necessary for ABTS cases)
    {
      Exchanges->fcExchange[ *fcExchangeIndex].Cmnd = Cmnd; // Linux Scsi
      Exchanges->fcExchange[ *fcExchangeIndex].pLoggedInPort = 
	fcFindLoggedInPort( fcChip,
          Exchanges->fcExchange[ *fcExchangeIndex].Cmnd, // find Scsi Nexus
          0,        // DON'T search linked list for FC port id
	  NULL,     // DON'T search linked list for FC WWN
          NULL);    // DON'T care about end of list

    }


                     // Build the command frame header (& data) according
                     // to command type

                     // fields common for all SFS frame types
    CMDfchs->reserved = 0L; // must clear
    CMDfchs->sof_eof = 0x75000000L;  // SOFi3:EOFn  no UAM; LCr=0, no TS
    
             // get the destination port_id from incoming FCHS
             // (initialized before calling if we're Originator)
             // Frame goes to port it was from - the source_id
    
    CMDfchs->d_id = InFCHS->s_id &0xFFFFFF; // destination (add R_CTL later)
    CMDfchs->s_id = fcChip->Registers.my_al_pa; // CS_CTL = 0


    // now enter command-specific fields
    switch( type )
    {

    case BLS_NOP:   // FC defined basic link service command NO-OP
                // ensure unique X_IDs! (use tracking function)

      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += 32L;        // add len to LSB (header only - no payload)

                   // TYPE[31-24] 00 Basic Link Service
                   // f_ctl[23:0] exchg originator, 1st seq, xfer S.I.
      CMDfchs->d_id |= 0x80000000L;  // R_CTL = 80 for NOP (Basic Link Ser.)
      CMDfchs->f_ctl = 0x00310000L;  // xchng originator, 1st seq,....
      CMDfchs->seq_cnt = 0x0L;
      CMDfchs->ox_rx_id = 0xFFFF;        // RX_ID for now; OX_ID on start
      CMDfchs->ro = 0x0L;    // relative offset (n/a)
      CMDfchs->pl[0] = 0xaabbccddL;   // words 8-15 frame data payload (n/a)
      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 1; // seconds
                                      // (NOP should complete ~instantly)
      break;


    
    
    case BLS_ABTS_ACC:  // Abort Sequence ACCept
      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += 32 + 12;    // add len to LSB (header + 3 DWORD payload)

      CMDfchs->d_id |= 0x84000000L;  // R_CTL = 84 for BASIC ACCept
                   // TYPE[31-24] 00 Basic Link Service
                   // f_ctl[23:0] exchg originator, not 1st seq, xfer S.I.
      CMDfchs->f_ctl = 0x00910000L;  // xchnge responder, last seq, xfer SI
                   // CMDfchs->seq_id & count might be set from DataHdr?
      CMDfchs->ro = 0x0L;    // relative offset (n/a)
      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 5; // seconds
                        // (Timeout in case of weird error)
      
      // now set the ACCept payload...
      ba_acc = (BA_ACC_PAYLOAD*)&CMDfchs->pl[0];
      memset( ba_acc, 0, sizeof( BA_ACC_PAYLOAD));
      // Since PLDA requires (only) entire Exchange aborts, we don't need
      // to worry about what the last sequence was.

      // We expect that a "target" task is accepting the abort, so we
      // can use the OX/RX ID pair 
      ba_acc->ox_rx_id = CMDfchs->ox_rx_id;
 
      // source, dest, #bytes
      BigEndianSwap((UCHAR *)&CMDfchs->ox_rx_id, (UCHAR *)&ba_acc->ox_rx_id, 4);

      ba_acc->low_seq_cnt = 0;
      ba_acc->high_seq_cnt = 0xFFFF;


      break;
    

    case BLS_ABTS_RJT:  // Abort Sequence ACCept
      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += 32 + 12;    // add len to LSB (header + 3 DWORD payload)

      CMDfchs->d_id |= 0x85000000L;  // R_CTL = 85 for BASIC ReJecT
                   // f_ctl[23:0] exchg originator, not 1st seq, xfer S.I.
                   // TYPE[31-24] 00 Basic Link Service
      CMDfchs->f_ctl = 0x00910000L;  // xchnge responder, last seq, xfer SI
                   // CMDfchs->seq_id & count might be set from DataHdr?
      CMDfchs->ro = 0x0L;    // relative offset (n/a)
      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 5; // seconds
                        // (Timeout in case of weird error)
      
      CMDfchs->ox_rx_id = InFCHS->ox_rx_id; // copy from sender!
      
      // now set the ReJecT payload...
      ba_rjt = (BA_RJT_PAYLOAD*)&CMDfchs->pl[0];
      memset( ba_rjt, 0, sizeof( BA_RJT_PAYLOAD));

      // We expect that a "target" task couldn't find the Exhange in the
      // array of active exchanges, so we use a new LinkService X_ID.
      // See Reject payload description in FC-PH (Rev 4.3), pg. 140
      ba_rjt->reason_code = 0x09; // "unable to perform command request"
      ba_rjt->reason_explain = 0x03; // invalid OX/RX ID pair


      break;
    
    
    case BLS_ABTS:   // FC defined basic link service command ABTS 
                     // Abort Sequence
                     

      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += 32L;        // add len to LSB (header only - no payload)

                   // TYPE[31-24] 00 Basic Link Service
                   // f_ctl[23:0] exchg originator, not 1st seq, xfer S.I.
      CMDfchs->d_id |= 0x81000000L;  // R_CTL = 81 for ABTS
      CMDfchs->f_ctl = 0x00110000L;  // xchnge originator, last seq, xfer SI
                   // CMDfchs->seq_id & count might be set from DataHdr?
      CMDfchs->ro = 0x0L;    // relative offset (n/a)
      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 2; // seconds
                        // (ABTS must timeout when responder is gone)
      break;

    
    
    case FCS_NSR:    // Fabric Name Service Request
       Exchanges->fcExchange[ *fcExchangeIndex].reTries = 2;


      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 2; // seconds
                         // OX_ID, linked to Driver Transaction ID
                         // (fix-up at Queing time)
      CMDfchs->ox_rx_id = 0xFFFF; // RX_ID - Responder (target) to modify
                                    // OX_ID set at ERQueing time
      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += (32L + sizeof(NSR_PL)); // add len (header & NSR payload)

      CMDfchs->d_id |= 0x02000000L;  // R_CTL = 02 for -
                                   // Name Service Request: Unsolicited 
                   // TYPE[31-24] 01 Extended Link Service
                   // f_ctl[23:0] exchg originator, 1st seq, xfer S.I.
      CMDfchs->f_ctl = 0x20210000L;
                   // OX_ID will be fixed-up at Tachyon enqueing time
      CMDfchs->seq_cnt = 0; // seq ID, DF_ctl, seq cnt
      CMDfchs->ro = 0x0L;    // relative offset (n/a)

      BuildLinkServicePayload( fcChip, type, &CMDfchs->pl[0]);

   
    
    
    
    
      break;
    
    
    
    
    case ELS_PLOGI:  // FC-PH extended link service command Port Login
      // (May, 2000)
      // NOTE! This special case facilitates SANMark testing.  The SANMark
      // test script for initialization-timeout.fcal.SANMark-1.fc
      // "eats" the OPN() primitive without issuing an R_RDY, causing
      // Tachyon to report LST (loop state timeout), which causes a
      // LIP.  To avoid this, simply send out the frame (i.e. assuming a
      // buffer credit of 1) without waiting for R_RDY.  Many FC devices
      // (other than Tachyon) have been doing this for years.  We don't
      // ever want to do this for non-Link Service frames unless the
      // other device really did report non-zero login BB credit (i.e.
      // in the PLOGI ACCept frame).
//      CMDfchs->sof_eof |= 0x00000400L;  // LCr=1
    
    case ELS_FDISC:  // Fabric Discovery (Login)
    case ELS_FLOGI:  // Fabric Login
    case ELS_SCR:    // Fabric State Change Registration
    case ELS_LOGO:   // FC-PH extended link service command Port Logout
    case ELS_PDISC:  // FC-PH extended link service cmnd Port Discovery
    case ELS_PRLI:   // FC-PH extended link service cmnd Process Login

      Exchanges->fcExchange[ *fcExchangeIndex].reTries = 2;


      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 2; // seconds
                         // OX_ID, linked to Driver Transaction ID
                         // (fix-up at Queing time)
      CMDfchs->ox_rx_id = 0xFFFF; // RX_ID - Responder (target) to modify
                                    // OX_ID set at ERQueing time
      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      if( type == ELS_LOGO )
        SfsLen += (32L + 16L); //  add len (header & PLOGI payload)
      else if( type == ELS_PRLI )
        SfsLen += (32L + 20L); //  add len (header & PRLI payload)
      else if( type == ELS_SCR )
        SfsLen += (32L + sizeof(SCR_PL)); //  add len (header & SCR payload)
      else
        SfsLen += (32L + 116L); //  add len (header & PLOGI payload)

      CMDfchs->d_id |= 0x22000000L;  // R_CTL = 22 for -
                                   // Extended Link_Data: Unsolicited Control
                   // TYPE[31-24] 01 Extended Link Service
                   // f_ctl[23:0] exchg originator, 1st seq, xfer S.I.
      CMDfchs->f_ctl = 0x01210000L;
                   // OX_ID will be fixed-up at Tachyon enqueing time
      CMDfchs->seq_cnt = 0; // seq ID, DF_ctl, seq cnt
      CMDfchs->ro = 0x0L;    // relative offset (n/a)

      BuildLinkServicePayload( fcChip, type, &CMDfchs->pl[0]);

      break;



    case ELS_LOGO_ACC: // FC-PH extended link service logout accept
    case ELS_RJT:          // extended link service reject (add reason)
    case ELS_ACC:      // ext. link service generic accept
    case ELS_PLOGI_ACC:// ext. link service login accept (PLOGI or PDISC)
    case ELS_PRLI_ACC: // ext. link service process login accept


      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 1; // assume done
                // ensure unique X_IDs! (use tracking function)
                // OX_ID from initiator cmd
      ox_ID = (USHORT)(InFCHS->ox_rx_id >> 16); 
      rx_ID = 0xFFFF; // RX_ID, linked to Driver Exchange ID

      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (not SEST index)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      if( type == ELS_RJT )
      {
        SfsLen += (32L + 8L); //  add len (header + payload)

        // ELS_RJT reason codes (utilize unused "reserved" field)
        CMDfchs->pl[0] = 1;
        CMDfchs->pl[1] = InFCHS->reserved;  
          
      }
      else if( (type == ELS_LOGO_ACC) || (type == ELS_ACC)  )
        SfsLen += (32L + 4L); //  add len (header + payload)
      else if( type == ELS_PLOGI_ACC )
        SfsLen += (32L + 116L); //  add len (header + payload)
      else if( type == ELS_PRLI_ACC )
        SfsLen += (32L + 20L); //  add len (header + payload)

      CMDfchs->d_id |= 0x23000000L;  // R_CTL = 23 for -
                                   // Extended Link_Data: Control Reply
                   // TYPE[31-24] 01 Extended Link Service
                   // f_ctl[23:0] exchg responder, last seq, e_s, tsi
      CMDfchs->f_ctl = 0x01990000L;
      CMDfchs->seq_cnt = 0x0L;
      CMDfchs->ox_rx_id = 0L;        // clear
      CMDfchs->ox_rx_id = ox_ID; // load upper 16 bits
      CMDfchs->ox_rx_id <<= 16;      // shift them

      CMDfchs->ro = 0x0L;    // relative offset (n/a)

      BuildLinkServicePayload( fcChip, type, &CMDfchs->pl[0]);

      break;


                         // Fibre Channel SCSI 'originator' sequences...
                         // (originator means 'initiator' in FCP-SCSI)

    case SCSI_IWE: // TachLite Initiator Write Entry
    {
      PFC_LOGGEDIN_PORT pLoggedInPort = 
        Exchanges->fcExchange[ *fcExchangeIndex].pLoggedInPort;

      Exchanges->fcExchange[ *fcExchangeIndex].reTries = 1;
      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 7; // FC2 timeout
                       
      // first, build FCP_CMND
      // unique X_ID fix-ups in StartExchange 

      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS FCP-CMND (not SEST index)

      // NOTE: unlike FC LinkService login frames, normal
      // SCSI commands are sent without outgoing verification
      IRB_flags.DCM = 1;    // Disable completion message for Cmnd frame
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += 64L;        // add len to LSB (header & CMND payload)

      CMDfchs->d_id |= (0x06000000L);  // R_CTL = 6 for command

                   // TYPE[31-24] 8 for FCP SCSI
                   // f_ctl[23:0] exchg originator, 1st seq, xfer S.I.
                   //             valid RO
      CMDfchs->f_ctl = 0x08210008L;
      CMDfchs->seq_cnt = 0x0L;
      CMDfchs->ox_rx_id = 0L;        // clear for now (-or- in later)
      CMDfchs->ro = 0x0L;    // relative offset (n/a)

                   // now, fill out FCP-DATA header
                   // (use buffer inside SEST object)
      dataHDR = &fcChip->SEST->DataHDR[ *fcExchangeIndex ];
      dataHDR->reserved = 0L; // must clear
      dataHDR->sof_eof = 0x75002000L;  // SOFi3:EOFn  no UAM; no CLS, noLCr, no TS
      dataHDR->d_id = (InFCHS->s_id | 0x01000000L); // R_CTL= FCP_DATA
      dataHDR->s_id = fcChip->Registers.my_al_pa; // CS_CTL = 0
                   // TYPE[31-24] 8 for FCP SCSI
                   // f_ctl[23:0] xfer S.I.| valid RO
      dataHDR->f_ctl = 0x08010008L;
      dataHDR->seq_cnt = 0x02000000L;  // sequence ID: df_ctl : seqence count
      dataHDR->ox_rx_id = 0L;        // clear; fix-up dataHDR fields later
      dataHDR->ro = 0x0L;    // relative offset (n/a)

                   // Now setup the SEST entry
      pIWE = &fcChip->SEST->u[ *fcExchangeIndex ].IWE;
      
                   // fill out the IWE:

                // VALid entry:Dir outbound:DCM:enable CM:enal INT: FC frame len
      pIWE->Hdr_Len = 0x8e000020L; // data frame Len always 32 bytes
      
      
      // from login parameters with other port, what's the largest frame
      // we can send? 
      if( pLoggedInPort == NULL) 
      {
	ulStatus = INVALID_ARGS;  // failed! give up
	break;
      }
      if( pLoggedInPort->rx_data_size  >= 2048)
        fl = 0x00020000;  // 2048 code (only support 1024!)
      else if( pLoggedInPort->rx_data_size  >= 1024)
        fl = 0x00020000;  // 1024 code
      else if( pLoggedInPort->rx_data_size  >= 512)
        fl = 0x00010000;  // 512 code
      else
	fl = 0;  // 128 bytes -- should never happen
      
      
      pIWE->Hdr_Len |= fl; // add xmit FC frame len for data phase
      pIWE->Hdr_Addr = fcChip->SEST->base + 
		((unsigned long)&fcChip->SEST->DataHDR[*fcExchangeIndex] - 
			(unsigned long)fcChip->SEST);

      pIWE->RSP_Len = sizeof(TachFCHDR_RSP) ; // hdr+data (recv'd RSP frame)
      pIWE->RSP_Len |= (InFCHS->s_id << 8); // MS 24 bits Remote_ID
      
      memset( &fcChip->SEST->RspHDR[ *fcExchangeIndex].pl, 0, 
        sizeof( FCP_STATUS_RESPONSE) );  // clear out previous status
 
      pIWE->RSP_Addr = fcChip->SEST->base + 
		((unsigned long)&fcChip->SEST->RspHDR[*fcExchangeIndex] - 
			(unsigned long)fcChip->SEST);

                   // Do we need local or extended gather list?
                   // depends on size - we can handle 3 len/addr pairs
                   // locally.

      fcp_dl = build_SEST_sgList( 
	cpqfcHBAdata->PciDev,
        &pIWE->GLen1, 
        Cmnd,       // S/G list
        &sgPairs,   // return # of pairs in S/G list (from "Data" descriptor)
        &fcChip->SEST->sgPages[ *fcExchangeIndex ]);// (for Freeing later)

      if( !fcp_dl ) // error building S/G list?
      {
        ulStatus = MEMPOOL_FAIL;
        break;      // give up
      }

                             // Now that we know total data length in
                             // the passed S/G buffer, set FCP CMND frame
      build_FCP_payload( Cmnd, (UCHAR*)&CMDfchs->pl[0], type, fcp_dl );


      
      if( sgPairs > 3 )  // need extended s/g list
        pIWE->Buff_Off = 0x78000000L; // extended data | (no offset)
      else               // local data pointers (in SEST)
        pIWE->Buff_Off = 0xf8000000L; // local data | (no offset)

                              // ULONG 5
      pIWE->Link = 0x0000ffffL;   // Buff_Index | Link

      pIWE->RX_ID = 0x0L;     // DWord 6: RX_ID set by target XFER_RDY

                                      // DWord 7
      pIWE->Data_Len = 0L;    // TL enters rcv'd XFER_RDY BURST_LEN
      pIWE->Exp_RO = 0L;      // DWord 8
                              // DWord 9
      pIWE->Exp_Byte_Cnt = fcp_dl;  // sum of gather buffers
    }
    break;





    case SCSI_IRE: // TachLite Initiator Read Entry

      if( Cmnd->timeout != 0)
      {
//	printk("Cmnd->timeout %d\n", Cmnd->timeout);
	// per Linux Scsi
        Exchanges->fcExchange[ *fcExchangeIndex].timeOut = Cmnd->timeout;
      }
      else  // use our best guess, based on FC & device
      {

        if( Cmnd->SCp.Message == 1 ) // Tape device? (from INQUIRY)	
	{
	  // turn off our timeouts (for now...)
          Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 0xFFFFFFFF; 
	}
	else
	{
          Exchanges->fcExchange[ *fcExchangeIndex].reTries = 1;
          Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 7; // per SCSI req.
	}
      }

  
      // first, build FCP_CMND


      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS FCP-CMND (not SEST index)
                            // NOTE: unlike FC LinkService login frames,
                            // normal SCSI commands are sent "open loop"
      IRB_flags.DCM = 1;    // Disable completion message for Cmnd frame
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += 64L;        // add len to LSB (header & CMND payload)

      CMDfchs->d_id |= (0x06000000L);  // R_CTL = 6 for command

             // TYPE[31-24] 8 for FCP SCSI
             // f_ctl[23:0] exchg originator, 1st seq, xfer S.I.
             //             valid RO
      CMDfchs->f_ctl = 0x08210008L;
      CMDfchs->seq_cnt = 0x0L;
      // x_ID & data direction bit set later
      CMDfchs->ox_rx_id = 0xFFFF;        // clear
      CMDfchs->ro = 0x0L;    // relative offset (n/a)



                   // Now setup the SEST entry
      pIRE = &fcChip->SEST->u[ *fcExchangeIndex ].IRE;

      // fill out the IRE:
      // VALid entry:Dir outbound:enable CM:enal INT:
      pIRE->Seq_Accum = 0xCE000000L; // VAL,DIR inbound,DCM| INI,DAT,RSP

      pIRE->reserved = 0L;
      pIRE->RSP_Len = sizeof(TachFCHDR_RSP) ; // hdr+data (recv'd RSP frame)
      pIRE->RSP_Len |= (InFCHS->s_id << 8); // MS 24 bits Remote_ID

      pIRE->RSP_Addr = fcChip->SEST->base + 
		((unsigned long)&fcChip->SEST->RspHDR[*fcExchangeIndex] - 
			(unsigned long)fcChip->SEST);
      
                   // Do we need local or extended gather list?
                   // depends on size - we can handle 3 len/addr pairs
                   // locally.

      fcp_dl = build_SEST_sgList( 
	cpqfcHBAdata->PciDev,
        &pIRE->SLen1, 
        Cmnd,       // SCSI command Data desc. with S/G list
        &sgPairs,   // return # of pairs in S/G list (from "Data" descriptor)
        &fcChip->SEST->sgPages[ *fcExchangeIndex ]);// (for Freeing later)
      
      
      if( !fcp_dl ) // error building S/G list?
      {
	// It is permissible to have a ZERO LENGTH Read command.
	// If there is the case, simply set fcp_dl (and Exp_Byte_Cnt)
	// to 0 and continue.
	if( Cmnd->request_bufflen == 0 )
	{
	  fcp_dl = 0; // no FC DATA frames expected

	}
	else
	{
          ulStatus = MEMPOOL_FAIL;
          break;      // give up
	}
      }

      // now that we know the S/G length, build CMND payload
      build_FCP_payload( Cmnd, (UCHAR*)&CMDfchs->pl[0], type, fcp_dl );

      
      if( sgPairs > 3 )  // need extended s/g list
        pIRE->Buff_Off = 0x00000000; // DWord 4: extended s/g list, no offset
      else
        pIRE->Buff_Off = 0x80000000; // local data, no offset
      
      pIRE->Buff_Index = 0x0L;    // DWord 5: Buff_Index | Reserved

      pIRE->Exp_RO  = 0x0L;       // DWord 6: Expected Rel. Offset

      pIRE->Byte_Count = 0;  // DWord 7: filled in by TL on err
      pIRE->reserved_ = 0;   // DWord 8: reserved
                             // NOTE: 0 length READ is OK.
      pIRE->Exp_Byte_Cnt = fcp_dl;// DWord 9: sum of scatter buffers
      
      break;




                         // Fibre Channel SCSI 'responder' sequences...
                         // (originator means 'target' in FCP-SCSI)
    case SCSI_TWE: // TachLite Target Write Entry

      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 10; // per SCSI req.

                       // first, build FCP_CMND

      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (XFER_RDY)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += (32L + 12L);// add SFS len (header & XFER_RDY payload)

      CMDfchs->d_id |= (0x05000000L);  // R_CTL = 5 for XFER_RDY

                   // TYPE[31-24] 8 for FCP SCSI
                   // f_ctl[23:0] exchg responder, 1st seq, xfer S.I.
                //             valid RO
      CMDfchs->f_ctl = 0x08810008L;
      CMDfchs->seq_cnt = 0x01000000; // sequence ID: df_ctl: sequence count
                       // use originator (other port's) OX_ID
      CMDfchs->ox_rx_id = InFCHS->ox_rx_id;     // we want upper 16 bits
      CMDfchs->ro = 0x0L;    // relative offset (n/a)

                   // now, fill out FCP-RSP header
                   // (use buffer inside SEST object)

      rspHDR = &fcChip->SEST->RspHDR[ *fcExchangeIndex ];
      rspHDR->reserved = 0L; // must clear
      rspHDR->sof_eof = 0x75000000L;  // SOFi3:EOFn  no UAM; no CLS, noLCr, no TS
      rspHDR->d_id = (InFCHS->s_id | 0x07000000L); // R_CTL= FCP_RSP
      rspHDR->s_id = fcChip->Registers.my_al_pa; // CS_CTL = 0
                   // TYPE[31-24] 8 for FCP SCSI
                   // f_ctl[23:0] responder|last seq| xfer S.I.
      rspHDR->f_ctl = 0x08910000L;
      rspHDR->seq_cnt = 0x03000000;  // sequence ID
      rspHDR->ox_rx_id = InFCHS->ox_rx_id; // gives us OX_ID
      rspHDR->ro = 0x0L;    // relative offset (n/a)


                   // Now setup the SEST entry
                   
      pTWE = &fcChip->SEST->u[ *fcExchangeIndex ].TWE;

      // fill out the TWE:

      // VALid entry:Dir outbound:enable CM:enal INT:
      pTWE->Seq_Accum = 0xC4000000L;  // upper word flags
      pTWE->reserved = 0L;
      pTWE->Remote_Node_ID = 0L; // no more auto RSP frame! (TL/TS change)
      pTWE->Remote_Node_ID |= (InFCHS->s_id << 8); // MS 24 bits Remote_ID
      

                   // Do we need local or extended gather list?
                   // depends on size - we can handle 3 len/addr pairs
                   // locally.

      fcp_dl = build_SEST_sgList( 
	cpqfcHBAdata->PciDev,
        &pTWE->SLen1, 
        Cmnd,       // S/G list
        &sgPairs,   // return # of pairs in S/G list (from "Data" descriptor)
        &fcChip->SEST->sgPages[ *fcExchangeIndex ]);// (for Freeing later)
      
      
      if( !fcp_dl ) // error building S/G list?
      {
        ulStatus = MEMPOOL_FAIL;
        break;      // give up
      }

      // now that we know the S/G length, build CMND payload
      build_FCP_payload( Cmnd, (UCHAR*)&CMDfchs->pl[0], type, fcp_dl );

      
      if( sgPairs > 3 )  // need extended s/g list
        pTWE->Buff_Off = 0x00000000; // extended s/g list, no offset
      else
        pTWE->Buff_Off = 0x80000000; // local data, no offset
      
      pTWE->Buff_Index = 0;     // Buff_Index | Link
      pTWE->Exp_RO = 0;
      pTWE->Byte_Count = 0;  // filled in by TL on err
      pTWE->reserved_ = 0;
      pTWE->Exp_Byte_Cnt = fcp_dl;// sum of scatter buffers
      
      break;






    case SCSI_TRE: // TachLite Target Read Entry

      // It doesn't make much sense for us to "time-out" a READ,
      // but we'll use it for design consistency and internal error recovery.
      Exchanges->fcExchange[ *fcExchangeIndex].timeOut = 10; // per SCSI req.

      // I/O request block settings...
      *pIRB_flags = 0;      // clear IRB flags
                                  // check PRLI (process login) info
                                  // to see if Initiator Requires XFER_RDY
                                  // if not, don't send one!
                                  // { PRLI check...}
      IRB_flags.SFA = 0;    // don't send XFER_RDY - start data
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += (32L + 12L);// add SFS len (header & XFER_RDY payload)


      
      // now, fill out FCP-DATA header
                   // (use buffer inside SEST object)
      dataHDR = &fcChip->SEST->DataHDR[ *fcExchangeIndex ];

      dataHDR->reserved = 0L; // must clear
      dataHDR->sof_eof = 0x75000000L;  // SOFi3:EOFn no UAM; no CLS,noLCr,no TS
      dataHDR->d_id = (InFCHS->s_id | 0x01000000L); // R_CTL= FCP_DATA
      dataHDR->s_id = fcChip->Registers.my_al_pa; // CS_CTL = 0


                   // TYPE[31-24] 8 for FCP SCSI
                   // f_ctl[23:0] exchg responder, not 1st seq, xfer S.I.
                   //             valid RO
      dataHDR->f_ctl = 0x08810008L;
      dataHDR->seq_cnt = 0x01000000;  // sequence ID (no XRDY)
      dataHDR->ox_rx_id = InFCHS->ox_rx_id & 0xFFFF0000; // we want upper 16 bits
      dataHDR->ro = 0x0L;    // relative offset (n/a)

                   // now, fill out FCP-RSP header
                   // (use buffer inside SEST object)
      rspHDR = &fcChip->SEST->RspHDR[ *fcExchangeIndex ];

      rspHDR->reserved = 0L; // must clear
      rspHDR->sof_eof = 0x75000000L;  // SOFi3:EOFn  no UAM; no CLS, noLCr, no TS
      rspHDR->d_id = (InFCHS->s_id | 0x07000000L); // R_CTL= FCP_RSP
      rspHDR->s_id = fcChip->Registers.my_al_pa; // CS_CTL = 0
                   // TYPE[31-24] 8 for FCP SCSI
                   // f_ctl[23:0] responder|last seq| xfer S.I.
      rspHDR->f_ctl = 0x08910000L;
      rspHDR->seq_cnt = 0x02000000;  // sequence ID: df_ctl: sequence count

      rspHDR->ro = 0x0L;    // relative offset (n/a)


      // Now setup the SEST entry
      pTRE = &fcChip->SEST->u[ *fcExchangeIndex ].TRE;


      // VALid entry:Dir outbound:enable CM:enal INT:
      pTRE->Hdr_Len = 0x86010020L; // data frame Len always 32 bytes
      pTRE->Hdr_Addr =  // bus address of dataHDR;
            fcChip->SEST->base + 
		((unsigned long)&fcChip->SEST->DataHDR[ *fcExchangeIndex ] -
			(unsigned long)fcChip->SEST);
	
      pTRE->RSP_Len = 64L; // hdr+data (TL assisted RSP frame)
      pTRE->RSP_Len |= (InFCHS->s_id << 8); // MS 24 bits Remote_ID
      pTRE->RSP_Addr = // bus address of rspHDR
		fcChip->SEST->base + 
			((unsigned long)&fcChip->SEST->RspHDR[ *fcExchangeIndex ] -
				(unsigned long)fcChip->SEST);

                   // Do we need local or extended gather list?
                   // depends on size - we can handle 3 len/addr pairs
                   // locally.

      fcp_dl = build_SEST_sgList( 
	cpqfcHBAdata->PciDev,
        &pTRE->GLen1, 
        Cmnd,       // S/G list
        &sgPairs,   // return # of pairs in S/G list (from "Data" descriptor)
        &fcChip->SEST->sgPages[ *fcExchangeIndex ]);// (for Freeing later)
      
      
      if( !fcp_dl ) // error building S/G list?
      {
        ulStatus = MEMPOOL_FAIL;
        break;      // give up
      }

      // no payload or command to build -- READ doesn't need XRDY

      
      if( sgPairs > 3 )  // need extended s/g list
        pTRE->Buff_Off = 0x78000000L; // extended data | (no offset)
      else               // local data pointers (in SEST)
        pTRE->Buff_Off = 0xf8000000L; // local data | (no offset)

                                            // ULONG 5
      pTRE->Buff_Index = 0L;   // Buff_Index | reserved
      pTRE->reserved = 0x0L;   // DWord 6

                                     // DWord 7: NOTE: zero length will
                                     // hang TachLite!
      pTRE->Data_Len = fcp_dl; // e.g. sum of scatter buffers

      pTRE->reserved_ = 0L;     // DWord 8
      pTRE->reserved__ = 0L;    // DWord 9

      break;





    

    case FCP_RESPONSE: 
                  // Target response frame: this sequence uses an OX/RX ID
                  // pair from a completed SEST exchange.  We built most
                  // of the response frame when we created the TWE/TRE.

      *pIRB_flags = 0;      // clear IRB flags
      IRB_flags.SFA = 1;    // send SFS (RSP)
      SfsLen = *pIRB_flags;

      SfsLen <<= 24;        // shift flags to MSB
      SfsLen += sizeof(TachFCHDR_RSP);// add SFS len (header & RSP payload)
      

      Exchanges->fcExchange[ *fcExchangeIndex].type = 
        FCP_RESPONSE; // change Exchange type to "response" phase

      // take advantage of prior knowledge of OX/RX_ID pair from
      // previous XFER outbound frame (still in fchs of exchange)
      fcChip->SEST->RspHDR[ *fcExchangeIndex ].ox_rx_id = 
        CMDfchs->ox_rx_id;

      // Check the status of the DATA phase of the exchange so we can report
      // status to the initiator
      buildFCPstatus( fcChip, *fcExchangeIndex); // set RSP payload fields

      memcpy(
        CMDfchs,  // re-use same XFER fchs for Response frame
        &fcChip->SEST->RspHDR[ *fcExchangeIndex ],
        sizeof( TachFCHDR_RSP ));
      
        
      break;

    default:
      printk("cpqfcTS: don't know how to build FC type: %Xh(%d)\n", type,type);
      break;

    }

    
    
    if( !ulStatus)  // no errors above?
    {
      // FCHS is built; now build IRB

      // link the just built FCHS (the "command") to the IRB entry 
      // for this Exchange.
      pIRB = &Exchanges->fcExchange[ *fcExchangeIndex].IRB; 
    
                          // len & flags according to command type above
      pIRB->Req_A_SFS_Len = SfsLen;  // includes IRB flags & len
      pIRB->Req_A_SFS_Addr = // TL needs physical addr of frame to send
		fcChip->exch_dma_handle + (unsigned long)CMDfchs - 
			(unsigned long)Exchanges;

      pIRB->Req_A_SFS_D_ID = CMDfchs->d_id << 8; // Dest_ID must be consistent!

    // Exchange is complete except for "fix-up" fields to be set
    // at Tachyon Queuing time:
    //    IRB->Req_A_Trans_ID (OX_ID/ RX_ID):  
    //        for SEST entry, lower bits correspond to actual FC Exchange ID
    //    fchs->OX_ID or RX_ID
    }
    else
    {
#ifdef DBG     
      printk( "FC Error: SEST build Pool Allocation failed\n");
#endif
      // return resources...
      cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, *fcExchangeIndex);  // SEST build failed
    }
  }
  else  // no Exchanges available
  {
    ulStatus = SEST_FULL;
    printk( "FC Error: no fcExchanges available\n");
  }
  return ulStatus;
}






// set RSP payload fields
static void  buildFCPstatus( PTACHYON fcChip, ULONG ExchangeID) 
{
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  FC_EXCHANGE *pExchange = &Exchanges->fcExchange[ExchangeID];  // shorthand
  PFCP_STATUS_RESPONSE pFcpStatus;
  
  memset( &fcChip->SEST->RspHDR[ ExchangeID ].pl, 0,
    sizeof( FCP_STATUS_RESPONSE) );
  if( pExchange->status ) // something wrong?
  {
    pFcpStatus = (PFCP_STATUS_RESPONSE)  // cast RSP buffer for this xchng
      &fcChip->SEST->RspHDR[ ExchangeID ].pl;
    if( pExchange->status & COUNT_ERROR )
    {
      
      // set FCP response len valid (so we can report count error)
      pFcpStatus->fcp_status |= FCP_RSP_LEN_VALID;
      pFcpStatus->fcp_rsp_len = 0x04000000;  // 4 byte len (BIG Endian)

      pFcpStatus->fcp_rsp_info = FCP_DATA_LEN_NOT_BURST_LEN; // RSP_CODE
    }
  }
}


static dma_addr_t 
cpqfc_pci_map_sg_page(
    		struct pci_dev *pcidev,
		ULONG *hw_paddr, 	// where to put phys addr for HW use
		void *sgp_vaddr,	// the virtual address of the sg page 
	 	dma_addr_t *umap_paddr,	// where to put phys addr for unmap
		unsigned int *maplen,	// where to store sg entry length
		int PairCount)		// number of sg pairs used in the page.	
{
	unsigned long aligned_addr = (unsigned long) sgp_vaddr;

	*maplen = PairCount * 8;
        aligned_addr += TL_EXT_SG_PAGE_BYTELEN;
        aligned_addr &= ~(TL_EXT_SG_PAGE_BYTELEN -1);
	
	*umap_paddr = pci_map_single(pcidev, (void *) aligned_addr, 
				*maplen, PCI_DMA_TODEVICE);
	*hw_paddr = (ULONG) *umap_paddr;

#       if BITS_PER_LONG > 32
       		if( *umap_paddr >>32 ) {
       	  		printk("cqpfcTS:Tach SG DMA addr %p>32 bits\n", 
	  			(void*)umap_paddr);
       			return 0;
       		}
#       endif
	return *umap_paddr;
}

static void
cpqfc_undo_SEST_mappings(struct pci_dev *pcidev,
			unsigned long contigaddr, int len, int dir,
  			struct scatterlist *sgl, int use_sg,
    			PSGPAGES *sgPages_head,
			int allocated_pages)
{
	PSGPAGES i, next;

	if (contigaddr != (unsigned long) NULL)
		pci_unmap_single(pcidev, contigaddr, len, dir);

	if (sgl != NULL)
		pci_unmap_sg(pcidev, sgl, use_sg, dir);

	for (i=*sgPages_head; i != NULL ;i = next)
	{
		pci_unmap_single(pcidev, i->busaddr, i->maplen, 
			scsi_to_pci_dma_dir(PCI_DMA_TODEVICE));
		i->busaddr = (dma_addr_t) NULL; 
		i->maplen = 0L; 
		next = i->next;
		kfree(i); 
	}
	*sgPages_head = NULL;
}

// This routine builds scatter/gather lists into SEST entries
// INPUTS:
//   SESTalPair - SEST address @DWordA "Local Buffer Length"
//   sgList     - Scatter/Gather linked list of Len/Address data buffers
// OUTPUT:
//   sgPairs - number of valid address/length pairs
// Remarks:
//   The SEST data buffer pointers only depend on number of
//   length/ address pairs, NOT on the type (IWE, TRE,...)
//   Up to 3 pairs can be referenced in the SEST - more than 3
//   require this Extended S/G list page.  The page holds 4, 8, 16...
//   len/addr pairs, per Scatter/Gather List Page Length Reg.
//   TachLite allows pages to be linked to any depth.

//#define DBG_SEST_SGLIST 1 // for printing out S/G pairs with Ext. pages

static int ap_hi_water = TL_DANGER_SGPAGES;

static ULONG build_SEST_sgList( 
    struct pci_dev *pcidev,
    ULONG *SESTalPairStart,  // the 3 len/address buffers in SEST
    Scsi_Cmnd *Cmnd,
    ULONG *sgPairs, 
    PSGPAGES *sgPages_head)  // link list of TL Ext. S/G pages from O/S Pool
    
{
  ULONG i, AllocatedPages=0; // Tach Ext. S/G page allocations
  ULONG* alPair = SESTalPairStart;
  ULONG* ext_sg_page_phys_addr_place = NULL;
  int PairCount;
  unsigned long ulBuff, contigaddr;
  ULONG total_data_len=0; // (in bytes)
  ULONG bytes_to_go = Cmnd->request_bufflen; // total xfer (S/G sum)
  ULONG thisMappingLen;
  struct scatterlist *sgl = NULL;  // S/G list (Linux format)
  int sg_count, totalsgs; 
  dma_addr_t busaddr;
  unsigned long thislen, offset;
  PSGPAGES *sgpage = sgPages_head;
  PSGPAGES prev_page = NULL;

# define WE_HAVE_SG_LIST (sgl != (unsigned long) NULL)
  contigaddr = (unsigned long) NULL;

  if( !Cmnd->use_sg )  // no S/G list?
  {
	if (bytes_to_go <= TL_MAX_SG_ELEM_LEN)
	{
    		*sgPairs = 1;	// use "local" S/G pair in SEST entry
				// (for now, ignore address bits above #31)

		*alPair++ = bytes_to_go; // bits 18-0, length

		if (bytes_to_go != 0) {
			contigaddr = ulBuff = pci_map_single(pcidev, 
				Cmnd->request_buffer, 
				Cmnd->request_bufflen,
				scsi_to_pci_dma_dir(Cmnd->sc_data_direction));
			// printk("ms %p ", ulBuff);
		}
		else {
			// No data transfer, (e.g.: Test Unit Ready)
			// printk("btg=0 ");
			*sgPairs = 0;
			memset(alPair, 0, sizeof(*alPair));
			return 0;
		}

#		if BITS_PER_LONG > 32
    			if( ulBuff >>32 ) {
      				printk("FATAL! Tachyon DMA address %p "
					"exceeds 32 bits\n", (void*)ulBuff );
      				return 0;
    			}
#		endif
    		*alPair = (ULONG)ulBuff;      
    		return bytes_to_go;
	} 
	else	// We have a single large (too big) contiguous buffer.
	{	// We will have to break it up.  We'll use the scatter
		// gather code way below, but use contigaddr instead
		// of sg_dma_addr(). (this is a very rare case).

		unsigned long btg;
		contigaddr = pci_map_single(pcidev, Cmnd->request_buffer, 
				Cmnd->request_bufflen,
				scsi_to_pci_dma_dir(Cmnd->sc_data_direction));

		// printk("contigaddr = %p, len = %d\n", 
		//	(void *) contigaddr, bytes_to_go);
		totalsgs = 0;
		for (btg = bytes_to_go; btg > 0; ) {
			btg -= ( btg > TL_MAX_SG_ELEM_LEN ? 
				TL_MAX_SG_ELEM_LEN : btg );
			totalsgs++;
		}
		sgl = NULL;
		*sgPairs = totalsgs;
	}
  }
  else  // we do have a scatter gather list
  {
	// [TBD - update for Linux to support > 32 bits addressing]
	// since the format for local & extended S/G lists is different,
	// check if S/G pairs exceeds 3.
	// *sgPairs = Cmnd->use_sg; Nope, that's wrong.
 
	sgl = (struct scatterlist*)Cmnd->request_buffer;  
	sg_count = pci_map_sg(pcidev, sgl, Cmnd->use_sg, 
		scsi_to_pci_dma_dir(Cmnd->sc_data_direction));
  	if( sg_count <= 3 ) {

	// we need to be careful here that no individual mapping
	// is too large, and if any is, that breaking it up
	// doesn't push us over 3 sgs, or, if it does, that we
	// handle that case.  Tachyon can take 0x7FFFF bits for length,
	// but sg structure uses "unsigned int", on the face of it, 
	// up to 0xFFFFFFFF or even more.

		int i;
		unsigned long thislen;

		totalsgs = 0;
		for (i=0;i<sg_count;i++) {
      			thislen = sg_dma_len(&sgl[i]);
			while (thislen >= TL_MAX_SG_ELEM_LEN) {
				totalsgs++;
				thislen -= TL_MAX_SG_ELEM_LEN;
			}
			if (thislen > 0) totalsgs++;
		}
		*sgPairs = totalsgs;
  	} else totalsgs = 999; // as a first estimate, definitely >3, 
			      
	// if (totalsgs != sg_count) 
	//	printk("totalsgs = %d, sgcount=%d\n",totalsgs,sg_count);
  }

  if( totalsgs <= 3 ) // can (must) use "local" SEST list
  {
    while( bytes_to_go)
    {
      offset = 0L;

      if ( WE_HAVE_SG_LIST ) 
	thisMappingLen = sg_dma_len(sgl);
      else					// or contiguous buffer?
	thisMappingLen = bytes_to_go;

      while (thisMappingLen > 0)
      {  
	thislen = thisMappingLen > TL_MAX_SG_ELEM_LEN ? 
		TL_MAX_SG_ELEM_LEN : thisMappingLen;
	bytes_to_go = bytes_to_go - thislen;

	// we have L/A pair; L = thislen, A = physicalAddress
	// load into SEST...

	total_data_len += thislen;
	*alPair = thislen; // bits 18-0, length

	alPair++;

	if ( WE_HAVE_SG_LIST ) 
		ulBuff = sg_dma_address(sgl) + offset;
	else
		ulBuff = contigaddr + offset;

	offset += thislen;

#	if BITS_PER_LONG > 32
		if( ulBuff >>32 ) {
        		printk("cqpfcTS: 2Tach DMA address %p > 32 bits\n", 
				(void*)ulBuff );
		    printk("%s = %p, offset = %ld\n", 
			WE_HAVE_SG_LIST ? "ulBuff" : "contigaddr",
			WE_HAVE_SG_LIST ? (void *) ulBuff : (void *) contigaddr,
				offset);
        		return 0;
      		}
#	endif
        *alPair++ = (ULONG)ulBuff; // lower 32 bits (31-0)
	thisMappingLen -= thislen;
      }

      if ( WE_HAVE_SG_LIST ) ++sgl;  // next S/G pair
	else if (bytes_to_go != 0) printk("BTG not zero!\n");

#     ifdef DBG_SEST_SGLIST
	printk("L=%d ", thisMappingLen);
	printk("btg=%d ", bytes_to_go);
#     endif

    }
    // printk("i:%d\n", *sgPairs);
  }
  else    // more than 3 pairs requires Extended S/G page (Pool Allocation)
  {
    // clear out SEST DWORDs (local S/G addr) C-F (A-B set in following logic)
    for( i=2; i<6; i++)
      alPair[i] = 0;

    PairCount = TL_EXT_SG_PAGE_COUNT;    // forces initial page allocation
    totalsgs = 0;
    while( bytes_to_go )
    {
      // Per SEST format, we can support 524287 byte lengths per
      // S/G pair.  Typical user buffers are 4k, and very rarely
      // exceed 12k due to fragmentation of physical memory pages.
      // However, on certain O/S system (not "user") buffers (on platforms 
      // with huge memories), it's possible to exceed this
      // length in a single S/G address/len mapping, so we have to handle
      // that.

      offset = 0L;
      if ( WE_HAVE_SG_LIST ) 
	thisMappingLen = sg_dma_len(sgl);
      else
	thisMappingLen = bytes_to_go;

      while (thisMappingLen > 0)
      {
	thislen = thisMappingLen > TL_MAX_SG_ELEM_LEN ? 
		TL_MAX_SG_ELEM_LEN : thisMappingLen;
	// printk("%d/%d/%d\n", thislen, thisMappingLen, bytes_to_go);
      
  	// should we load into "this" extended S/G page, or allocate
	// new page?

	if( PairCount >= TL_EXT_SG_PAGE_COUNT )
	{
	  // Now, we have to map the previous page, (triggering buffer bounce)
	  // The first time thru the loop, there won't be a previous page.
	  if (prev_page != NULL) // is there a prev page? 
	  {
		// this code is normally kind of hard to trigger, 
		// you have to use up more than 256 scatter gather 
		// elements to get here.  Cranking down TL_MAX_SG_ELEM_LEN
		// to an absurdly low value (128 bytes or so) to artificially
		// break i/o's into a zillion pieces is how I tested it. 
		busaddr = cpqfc_pci_map_sg_page(pcidev,
				ext_sg_page_phys_addr_place,
				prev_page->page,
        			&prev_page->busaddr,
        			&prev_page->maplen,
				PairCount);
          } 
          // Allocate the TL Extended S/G list page.  We have
          // to allocate twice what we want to ensure required TL alignment
          // (Tachlite TL/TS User Man. Rev 6.0, p 168)
          // We store the original allocated PVOID so we can free later
	  *sgpage = kmalloc( sizeof(SGPAGES), GFP_ATOMIC);
	  if ( ! *sgpage )
          {
		printk("cpqfc: Allocation failed @ %d S/G page allocations\n",
			AllocatedPages);
		total_data_len = 0;  // failure!! Ext. S/G is All-or-none affair

	        // unmap the previous mappings, if any.

	        cpqfc_undo_SEST_mappings(pcidev, contigaddr, 
			Cmnd->request_bufflen,
			scsi_to_pci_dma_dir(Cmnd->sc_data_direction),
  			sgl, Cmnd->use_sg, sgPages_head, AllocatedPages+1);

		// FIXME: testing shows that if we get here, 
		// it's bad news.  (this has been this way for a long 
		// time though, AFAIK.  Not that that excuses it.)

		return 0; // give up (and probably hang the system)
          }
                               // clear out memory we just allocated
	  memset( (*sgpage)->page,0,TL_EXT_SG_PAGE_BYTELEN*2);
	  (*sgpage)->next = NULL;
	  (*sgpage)->busaddr = (dma_addr_t) NULL;
	  (*sgpage)->maplen = 0L;
      
	  // align the memory - TL requires sizeof() Ext. S/G page alignment.
	  // We doubled the actual required size so we could mask off LSBs 
	  // to get desired offset 

	  ulBuff = (unsigned long) (*sgpage)->page;
	  ulBuff += TL_EXT_SG_PAGE_BYTELEN;
	  ulBuff &= ~(TL_EXT_SG_PAGE_BYTELEN -1);

	  // set pointer, in SEST if first Ext. S/G page, or in last pair
	  // of linked Ext. S/G pages... (Only 32-bit PVOIDs, so just 
	  // load lower 32 bits)
	  // NOTE: the Len field must be '0' if this is the first Ext. S/G
	  // pointer in SEST, and not 0 otherwise (we know thislen != 0).

	  *alPair = (alPair != SESTalPairStart) ? thislen : 0;

#	  ifdef DBG_SEST_SGLIST
        	printk("PairCount %d @%p even %Xh, ", 
			PairCount, alPair, *alPair);
#	  endif

	  // Save the place where we need to store the physical
	  // address of this scatter gather page which we get when we map it
	  // (and mapping we can do only after we fill it in.)
	  alPair++;  // next DWORD, will contain phys addr of the ext page
	  ext_sg_page_phys_addr_place = alPair;

	  // Now, set alPair = the virtual addr of the (Extended) S/G page
	  // which will accept the Len/ PhysicalAddress pairs
	  alPair = (ULONG *) ulBuff;

	  AllocatedPages++;
	  if (AllocatedPages >= ap_hi_water)
	  {
		// This message should rarely, if ever, come out.
		// Previously (cpqfc version <= 2.0.5) the driver would
		// just puke if more than 4 SG pages were used, and nobody
		// ever complained about that.  This only comes out if 
		// more than 8 pages are used.

		printk(KERN_WARNING
		"cpqfc: Possible danger.  %d scatter gather pages used.\n"
			"cpqfc: detected seemingly extreme memory "
			"fragmentation or huge data transfers.\n", 
			AllocatedPages);
		ap_hi_water = AllocatedPages+1;
	  }
		
	  PairCount = 1;  // starting new Ext. S/G page
	  prev_page = (*sgpage);  // remember this page, for next time thru
	  sgpage = &((*sgpage)->next);
	}  // end of new TL Ext. S/G page allocation

	*alPair = thislen; // bits 18-0, length (range check above)
      
#	ifdef DBG_SEST_SGLIST
	  printk("PairCount %d @%p, even %Xh, ", PairCount, alPair, *alPair);
#	endif

	alPair++;    // next DWORD, physical address 

	if ( WE_HAVE_SG_LIST ) 
		ulBuff = sg_dma_address(sgl) + offset;
	else
		ulBuff = contigaddr + offset;
	offset += thislen;

#	if BITS_PER_LONG > 32
          if( ulBuff >>32 )
	  {
	    printk("cqpfcTS: 1Tach DMA address %p > 32 bits\n", (void*)ulBuff );
	    printk("%s = %p, offset = %ld\n", 
		WE_HAVE_SG_LIST ? "ulBuff" : "contigaddr",
		WE_HAVE_SG_LIST ? (void *) ulBuff : (void *) contigaddr,
			offset);
	    return 0;
	  }
#	endif

	*alPair = (ULONG) ulBuff; // lower 32 bits (31-0)

#       ifdef DBG_SEST_SGLIST
        printk("odd %Xh\n", *alPair);
#       endif
	alPair++;    // next DWORD, next address/length pair
                                           
	PairCount++; // next Length/Address pair

	// if (PairCount > pc_hi_water)
	// {
		// printk("pc hi = %d ", PairCount);
		// pc_hi_water = PairCount;
	// }
	bytes_to_go -= thislen;
	total_data_len += thislen;  
	thisMappingLen -= thislen;
	totalsgs++;
      } // while (thisMappingLen > 0)
      if ( WE_HAVE_SG_LIST ) sgl++;  // next S/G pair
    } // while (bytes_to_go)

    // printk("Totalsgs=%d\n", totalsgs);
    *sgPairs = totalsgs;

    // PCI map (and bounce) the last (and usually only) extended SG page
    busaddr = cpqfc_pci_map_sg_page(pcidev,
		ext_sg_page_phys_addr_place,
		prev_page->page, 
		&prev_page->busaddr, 
		&prev_page->maplen,
		PairCount);
  }
  return total_data_len;
}



// The Tachlite SEST table is referenced to OX_ID (or RX_ID).  To optimize
// performance and debuggability, we index the Exchange structure to FC X_ID
// This enables us to build exchanges for later en-queing to Tachyon,
// provided we have an open X_ID slot. At Tachyon queing time, we only 
// need an ERQ slot; then "fix-up" references in the 
// IRB, FCHS, etc. as needed.
// RETURNS:
// 0 if successful
// non-zero on error
//sstartex
ULONG cpqfcTSStartExchange( 
  CPQFCHBA *cpqfcHBAdata,                      
  LONG ExchangeID )
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  FC_EXCHANGE *pExchange = &Exchanges->fcExchange[ ExchangeID ]; // shorthand
  USHORT producer, consumer;
  ULONG ulStatus=0;
  short int ErqIndex;
  BOOLEAN CompleteExchange = FALSE;  // e.g. ACC replies are complete
  BOOLEAN SestType=FALSE;
  ULONG InboundData=0;

  // We will manipulate Tachlite chip registers here to successfully
  // start exchanges. 

  // Check that link is not down -- we can't start an exchange on a
  // down link!

  if( fcChip->Registers.FMstatus.value & 0x80) // LPSM offline?
  {
printk("fcStartExchange: PSM offline (%Xh), x_ID %Xh, type %Xh, port_id %Xh\n",
         fcChip->Registers.FMstatus.value & 0xFF,
         ExchangeID,
         pExchange->type,
         pExchange->fchs.d_id);

    if( ExchangeID >= TACH_SEST_LEN )  // Link Service Outbound frame?
    {
      // Our most popular LinkService commands are port discovery types
      // (PLOGI/ PDISC...), which are implicitly nullified by Link Down
      // events, so it makes no sense to Que them.  However, ABTS should
      // be queued, since exchange sequences are likely destroyed by
      // Link Down events, and we want to notify other ports of broken
      // sequences by aborting the corresponding exchanges.
      if( pExchange->type != BLS_ABTS )
      {
	ulStatus = LNKDWN_OSLS;
	goto Done;
        // don't Que most LinkServ exchanges on LINK DOWN
      }
    }

    printk("fcStartExchange: Que x_ID %Xh, type %Xh\n", 
      ExchangeID, pExchange->type);
    pExchange->status |= EXCHANGE_QUEUED;
    ulStatus = EXCHANGE_QUEUED;
    goto Done;
  }

  // Make sure ERQ has available space.
  
  producer = (USHORT)fcChip->ERQ->producerIndex; // copies for logical arith.
  consumer = (USHORT)fcChip->ERQ->consumerIndex;
  producer++;  // We are testing for full que by incrementing
  
  if( producer >= ERQ_LEN )  // rollover condition?
    producer = 0;
  if( consumer != producer ) // ERQ not full?
  {
    // ****************** Need Atomic access to chip registers!!********
    
    // remember ERQ PI for copying IRB
    ErqIndex = (USHORT)fcChip->ERQ->producerIndex; 
    fcChip->ERQ->producerIndex = producer; // this is written to Tachyon
               // we have an ERQ slot! If SCSI command, need SEST slot
               // otherwise we are done.

    // Note that Tachyon requires that bit 15 of the OX_ID or RX_ID be
    // set according to direction of data to/from Tachyon for SEST assists.
    // For consistency, enforce this rule for Link Service (non-SEST)
    // exchanges as well.

    // fix-up the X_ID field in IRB
    pExchange->IRB.Req_A_Trans_ID = ExchangeID & 0x7FFF; // 15-bit field

    // fix-up the X_ID field in fchs -- depends on Originator or Responder,
    // outgoing or incoming data?
    switch( pExchange->type )
    {
               // ORIGINATOR types...  we're setting our OX_ID and
               // defaulting the responder's RX_ID to 0xFFFF
    
    case SCSI_IRE:
      // Requirement: set MSB of x_ID for Incoming TL data
      // (see "Tachyon TL/TS User's Manual", Rev 6.0, Sept.'98, pg. 50)
      InboundData = 0x8000;

    case SCSI_IWE:   
      SestType = TRUE;
      pExchange->fchs.ox_rx_id = (ExchangeID | InboundData);
      pExchange->fchs.ox_rx_id <<= 16;     // MSW shift
      pExchange->fchs.ox_rx_id |= 0xffff;  // add default RX_ID
      
      // now fix-up the Data HDR OX_ID (TL automatically does rx_id)
      // (not necessary for IRE -- data buffer unused)
      if( pExchange->type == SCSI_IWE)
      {
        fcChip->SEST->DataHDR[ ExchangeID ].ox_rx_id = 
          pExchange->fchs.ox_rx_id;

      }

      break;


    case FCS_NSR:  // ext. link service Name Service Request
    case ELS_SCR:  // ext. link service State Change Registration
    case ELS_FDISC:// ext. link service login
    case ELS_FLOGI:// ext. link service login
    case ELS_LOGO: // FC-PH extended link service logout
    case BLS_NOP:  // Basic link service No OPeration
    case ELS_PLOGI:// ext. link service login (PLOGI)
    case ELS_PDISC:// ext. link service login (PDISC)
    case ELS_PRLI: // ext. link service process login

      pExchange->fchs.ox_rx_id = ExchangeID;
      pExchange->fchs.ox_rx_id <<= 16;  // MSW shift
      pExchange->fchs.ox_rx_id |= 0xffff;  // and RX_ID

      break;
      



               // RESPONDER types... we must set our RX_ID while preserving
               // sender's OX_ID
               // outgoing (or no) data
    case ELS_RJT:       // extended link service reject 
    case ELS_LOGO_ACC: // FC-PH extended link service logout accept
    case ELS_ACC:      // ext. generic link service accept
    case ELS_PLOGI_ACC:// ext. link service login accept (PLOGI or PDISC)
    case ELS_PRLI_ACC: // ext. link service process login accept

      CompleteExchange = TRUE;   // Reply (ACC or RJT) is end of exchange
      pExchange->fchs.ox_rx_id |= (ExchangeID & 0xFFFF);

      break;


      // since we are a Responder, OX_ID should already be set by
      // cpqfcTSBuildExchange().  We need to -OR- in RX_ID
    case SCSI_TWE:
      SestType = TRUE;
      // Requirement: set MSB of x_ID for Incoming TL data
      // (see "Tachyon TL/TS User's Manual", Rev 6.0, Sept.'98, pg. 50)

      pExchange->fchs.ox_rx_id &= 0xFFFF0000;  // clear RX_ID
      // Requirement: set MSB of RX_ID for Incoming TL data
      // (see "Tachyon TL/TS User's Manual", Rev 6.0, Sept.'98, pg. 50)
      pExchange->fchs.ox_rx_id |= (ExchangeID | 0x8000);
      break;
          
    
    case SCSI_TRE:
      SestType = TRUE;
      
      // there is no XRDY for SEST target read; the data
      // header needs to be updated. Also update the RSP
      // exchange IDs for the status frame, in case it is sent automatically
      fcChip->SEST->DataHDR[ ExchangeID ].ox_rx_id |= ExchangeID;
      fcChip->SEST->RspHDR[ ExchangeID ].ox_rx_id = 
        fcChip->SEST->DataHDR[ ExchangeID ].ox_rx_id;
      
      // for easier FCP response logic (works for TWE and TRE), 
      // copy exchange IDs.  (Not needed if TRE 'RSP' bit set)
      pExchange->fchs.ox_rx_id =
        fcChip->SEST->DataHDR[ ExchangeID ].ox_rx_id;

      break;


    case FCP_RESPONSE:  // using existing OX_ID/ RX_ID pair,
                        // start SFS FCP-RESPONSE frame
      // OX/RX_ID should already be set! (See "fcBuild" above)
      CompleteExchange = TRUE;   // RSP is end of FCP-SCSI exchange

      
      break;


    case BLS_ABTS_RJT:  // uses new RX_ID, since SEST x_ID non-existent
    case BLS_ABTS_ACC:  // using existing OX_ID/ RX_ID pair from SEST entry
      CompleteExchange = TRUE;   // ACC or RJT marks end of FCP-SCSI exchange
    case BLS_ABTS:  // using existing OX_ID/ RX_ID pair from SEST entry


      break;


    default:
      printk("Error on fcStartExchange: undefined type %Xh(%d)\n",
        pExchange->type, pExchange->type);
      return INVALID_ARGS;
    }
    
    
      // X_ID fields are entered -- copy IRB to Tachyon's ERQ
    

    memcpy(
        &fcChip->ERQ->QEntry[ ErqIndex ],  // dest.
        &pExchange->IRB,
        32);  // fixed (hardware) length!

    PCI_TRACEO( ExchangeID, 0xA0)

    // ACTION!  May generate INT and IMQ entry
    writel( fcChip->ERQ->producerIndex,
          fcChip->Registers.ERQproducerIndex.address);

  
    if( ExchangeID >= TACH_SEST_LEN )  // Link Service Outbound frame?
    {
    
      // wait for completion! (TDB -- timeout and chip reset)
      

  PCI_TRACEO( ExchangeID, 0xA4)
  
      enable_irq( cpqfcHBAdata->HostAdapter->irq); // only way to get Sem.
      
      down_interruptible( cpqfcHBAdata->TYOBcomplete); 
  
      disable_irq( cpqfcHBAdata->HostAdapter->irq);
  PCI_TRACE( 0xA4)

      // On login exchanges, BAD_ALPA (non-existent port_id) results in 
      // FTO (Frame Time Out) on the Outbound Completion message.
      // If we got an FTO status, complete the exchange (free up slot)
      if( CompleteExchange ||   // flag from Reply frames
          pExchange->status )   // typically, can get FRAME_TO
      {
    	cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, ExchangeID);  
      }
    }

    else                         // SEST Exchange
    {
      ulStatus = 0;   // ship & pray success (e.g. FCP-SCSI)
      
      if( CompleteExchange )   // by Type of exchange (e.g. end-of-xchng)
      {
    	cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, ExchangeID);  
      }
       
      else
        pExchange->status &= ~EXCHANGE_QUEUED;  // clear ExchangeQueued flag 

    }
  }

  
  else                // ERQ 'producer' = 'consumer' and QUE is full
  {
    ulStatus = OUTQUE_FULL; // Outbound (ERQ) Que full
  }
 
Done: 
  PCI_TRACE( 0xA0)
  return ulStatus; 
}





// Scan fcController->fcExchanges array for a usuable index (a "free"
// exchange).
// Inputs:
//   fcChip - pointer to TachLite chip structure
// Return:
//  index - exchange array element where exchange can be built
//  -1    - exchange array is full
// REMARKS:
// Although this is a (yuk!) linear search, we presume
// that the system will complete exchanges about as quickly as
// they are submitted.  A full Exchange array (and hence, max linear
// search time for free exchange slot) almost guarantees a Fibre problem 
// of some sort.
// In the interest of making exchanges easier to debug, we want a LRU
// (Least Recently Used) scheme.


static LONG FindFreeExchange( PTACHYON fcChip, ULONG type )
{
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  ULONG i;
  ULONG ulStatus=-1;  // assume failure


  if( type == SCSI_IRE ||
      type == SCSI_TRE ||
      type == SCSI_IWE ||
      type == SCSI_TWE)
  {
        // SCSI type - X_IDs should be from 0 to TACH_SEST_LEN-1
    if( fcChip->fcSestExchangeLRU >= TACH_SEST_LEN) // rollover?
      fcChip->fcSestExchangeLRU = 0;
    i = fcChip->fcSestExchangeLRU; // typically it's already free!

    if( Exchanges->fcExchange[i].type == 0 ) // check for "free" element
    {
      ulStatus = 0; // success!
    }
    
    else
    {         // YUK! we need to do a linear search for free element.
              // Fragmentation of the fcExchange array is due to excessively
              // long completions or timeouts.
      
      while( TRUE )
      {
        if( ++i >= TACH_SEST_LEN ) // rollover check
          i = 0;  // beginning of SEST X_IDs

//        printk( "looping for SCSI xchng ID: i=%d, type=%Xh\n", 
//         i, Exchanges->fcExchange[i].type);

        if( Exchanges->fcExchange[i].type == 0 ) // "free"?
        {
          ulStatus = 0; // success!
          break;
        }
        if( i == fcChip->fcSestExchangeLRU ) // wrapped-around array?
        {
          printk( "SEST X_ID space full\n");
          break;       // failed - prevent inf. loop
        }
      }
    }
    fcChip->fcSestExchangeLRU = i + 1; // next! (rollover check next pass)
  }

  
  
  else  // Link Service type - X_IDs should be from TACH_SEST_LEN 
        // to TACH_MAX_XID
  {
    if( fcChip->fcLsExchangeLRU >= TACH_MAX_XID || // range check
        fcChip->fcLsExchangeLRU < TACH_SEST_LEN ) // (e.g. startup)
      fcChip->fcLsExchangeLRU = TACH_SEST_LEN;

    i = fcChip->fcLsExchangeLRU; // typically it's already free!
    if( Exchanges->fcExchange[i].type == 0 ) // check for "free" element
    {
      ulStatus = 0; // success!
    }
    
    else
    {         // YUK! we need to do a linear search for free element
              // Fragmentation of the fcExchange array is due to excessively
              // long completions or timeouts.
      
      while( TRUE )
      {
        if( ++i >= TACH_MAX_XID ) // rollover check
          i = TACH_SEST_LEN;// beginning of Link Service X_IDs

//        printk( "looping for xchng ID: i=%d, type=%Xh\n", 
//         i, Exchanges->fcExchange[i].type);

        if( Exchanges->fcExchange[i].type == 0 ) // "free"?
        {
          ulStatus = 0; // success!
          break;
        }
        if( i == fcChip->fcLsExchangeLRU ) // wrapped-around array?
        {
          printk( "LinkService X_ID space full\n");
          break;       // failed - prevent inf. loop
        }
      }
    }
    fcChip->fcLsExchangeLRU = i + 1; // next! (rollover check next pass)

  }

  if( !ulStatus )  // success?
    Exchanges->fcExchange[i].type = type; // allocate it.
  
  else
    i = -1;  // error - all exchanges "open"

  return i;  
}

static void
cpqfc_pci_unmap_extended_sg(struct pci_dev *pcidev,
	PTACHYON fcChip,
	ULONG x_ID)
{
	// Unmaps the memory regions used to hold the scatter gather lists

	PSGPAGES i;

	// Were there any such regions needing unmapping?
	if (! USES_EXTENDED_SGLIST(fcChip->SEST, x_ID))
		return;	// No such regions, we're outta here.

	// for each extended scatter gather region needing unmapping... 
	for (i=fcChip->SEST->sgPages[x_ID] ; i != NULL ; i = i->next)
		pci_unmap_single(pcidev, i->busaddr, i->maplen,
			scsi_to_pci_dma_dir(PCI_DMA_TODEVICE));
}

// Called also from cpqfcTScontrol.o, so can't be static
void
cpqfc_pci_unmap(struct pci_dev *pcidev, 
	Scsi_Cmnd *cmd, 
	PTACHYON fcChip, 
	ULONG x_ID)
{
	// Undo the DMA mappings
	if (cmd->use_sg) {	// Used scatter gather list for data buffer?
		cpqfc_pci_unmap_extended_sg(pcidev, fcChip, x_ID);
		pci_unmap_sg(pcidev, cmd->buffer, cmd->use_sg,
			scsi_to_pci_dma_dir(cmd->sc_data_direction));
		// printk("umsg %d\n", cmd->use_sg);
	}
	else if (cmd->request_bufflen) {
		// printk("ums %p ", fcChip->SEST->u[ x_ID ].IWE.GAddr1);
		pci_unmap_single(pcidev, fcChip->SEST->u[ x_ID ].IWE.GAddr1,
			cmd->request_bufflen,
			scsi_to_pci_dma_dir(cmd->sc_data_direction));
	}	 
}

// We call this routine to free an Exchange for any reason:
// completed successfully, completed with error, aborted, etc.

// returns FALSE if Exchange failed and "retry" is acceptable
// returns TRUE if Exchange was successful, or retry is impossible
// (e.g. port/device gone).
//scompleteexchange

void cpqfcTSCompleteExchange( 
       struct pci_dev *pcidev,
       PTACHYON fcChip, 
       ULONG x_ID)
{
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  int already_unmapped = 0;
  
  if( x_ID < TACH_SEST_LEN ) // SEST-based (or LinkServ for FCP exchange)
  {
    if( Exchanges->fcExchange[ x_ID ].Cmnd == NULL ) // what#@!
    {
//      TriggerHBA( fcChip->Registers.ReMapMemBase, 0);
      printk(" x_ID %Xh, type %Xh, NULL ptr!\n", x_ID,
			Exchanges->fcExchange[ x_ID ].type);

      goto CleanUpSestResources;  // this path should be very rare.
    }

    // we have Linux Scsi Cmnd ptr..., now check our Exchange status
    // to decide how to complete this SEST FCP exchange

    if( Exchanges->fcExchange[ x_ID ].status ) // perhaps a Tach indicated problem,
                                             // or abnormal exchange completion
    {
      // set FCP Link statistics
     
      if( Exchanges->fcExchange[ x_ID ].status & FC2_TIMEOUT)
        fcChip->fcStats.timeouts++;
      if( Exchanges->fcExchange[ x_ID ].status & INITIATOR_ABORT)
        fcChip->fcStats.FC4aborted++;
      if( Exchanges->fcExchange[ x_ID ].status & COUNT_ERROR)
        fcChip->fcStats.CntErrors++;
      if( Exchanges->fcExchange[ x_ID ].status & LINKFAIL_TX)
        fcChip->fcStats.linkFailTX++;
      if( Exchanges->fcExchange[ x_ID ].status & LINKFAIL_RX)
        fcChip->fcStats.linkFailRX++;
      if( Exchanges->fcExchange[ x_ID ].status & OVERFLOW)
        fcChip->fcStats.CntErrors++;

      // First, see if the Scsi upper level initiated an ABORT on this
      // exchange...
      if( Exchanges->fcExchange[ x_ID ].status == INITIATOR_ABORT )
      {
        printk(" DID_ABORT, x_ID %Xh, Cmnd %p ", 
            x_ID, Exchanges->fcExchange[ x_ID ].Cmnd);
        goto CleanUpSestResources;  // (we don't expect Linux _aborts)
      }

      // Did our driver timeout the Exchange, or did Tachyon indicate
      // a failure during transmission?  Ask for retry with "SOFT_ERROR"
      else if( Exchanges->fcExchange[ x_ID ].status & FC2_TIMEOUT) 
      {
//        printk("result DID_SOFT_ERROR, x_ID %Xh, Cmnd %p\n", 
//            x_ID, Exchanges->fcExchange[ x_ID ].Cmnd);
        Exchanges->fcExchange[ x_ID ].Cmnd->result = (DID_SOFT_ERROR <<16);
      }
      
      // Did frame(s) for an open exchange arrive in the SFQ,
      // meaning the SEST was unable to process them?
      else if( Exchanges->fcExchange[ x_ID ].status & SFQ_FRAME) 
      {
//        printk("result DID_SOFT_ERROR, x_ID %Xh, Cmnd %p\n", 
//            x_ID, Exchanges->fcExchange[ x_ID ].Cmnd);
        Exchanges->fcExchange[ x_ID ].Cmnd->result = (DID_SOFT_ERROR <<16);
      }
      
      // Did our driver timeout the Exchange, or did Tachyon indicate
      // a failure during transmission?  Ask for retry with "SOFT_ERROR"
      else if( 
               (Exchanges->fcExchange[ x_ID ].status & LINKFAIL_TX) ||
               (Exchanges->fcExchange[ x_ID ].status & PORTID_CHANGED) ||
	       (Exchanges->fcExchange[ x_ID ].status & FRAME_TO)    ||
	       (Exchanges->fcExchange[ x_ID ].status & INV_ENTRY)    ||
	       (Exchanges->fcExchange[ x_ID ].status & ABORTSEQ_NOTIFY)    )


      {
//        printk("result DID_SOFT_ERROR, x_ID %Xh, Cmnd %p\n", 
//            x_ID, Exchanges->fcExchange[ x_ID ].Cmnd);
        Exchanges->fcExchange[ x_ID ].Cmnd->result = (DID_SOFT_ERROR <<16);


      }

      // e.g., a LOGOut happened, or device never logged back in.
      else if( Exchanges->fcExchange[ x_ID ].status & DEVICE_REMOVED) 
      {
//	printk(" *LOGOut or timeout on login!* ");
	// trigger?
//        TriggerHBA( fcChip->Registers.ReMapMemBase, 0);

        Exchanges->fcExchange[ x_ID ].Cmnd->result = (DID_BAD_TARGET <<16);
      }      
		
		      
      // Did Tachyon indicate a CNT error?  We need further analysis
      // to determine if the exchange is acceptable
      else if( Exchanges->fcExchange[ x_ID ].status == COUNT_ERROR)
      {
        UCHAR ScsiStatus;
        FCP_STATUS_RESPONSE *pFcpStatus = 
	  (PFCP_STATUS_RESPONSE)&fcChip->SEST->RspHDR[ x_ID ].pl;

      	ScsiStatus = pFcpStatus->fcp_status >>24;
  
	// If the command is a SCSI Read/Write type, we don't tolerate
	// count errors of any kind; assume the count error is due to
	// a dropped frame and ask for retry...
	
	if(( (Exchanges->fcExchange[ x_ID ].Cmnd->cmnd[0] == 0x8) ||
	    (Exchanges->fcExchange[ x_ID ].Cmnd->cmnd[0] == 0x28) ||		
            (Exchanges->fcExchange[ x_ID ].Cmnd->cmnd[0] == 0xA) ||
            (Exchanges->fcExchange[ x_ID ].Cmnd->cmnd[0] == 0x2A) )
	                   &&
                     ScsiStatus == 0 )
	{
          // ask for retry
/*          printk("COUNT_ERROR retry, x_ID %Xh, status %Xh, Cmnd %p\n", 
            x_ID, Exchanges->fcExchange[ x_ID ].status,
            Exchanges->fcExchange[ x_ID ].Cmnd);*/
          Exchanges->fcExchange[ x_ID ].Cmnd->result = (DID_SOFT_ERROR <<16);
	}
	
	else  // need more analysis
	{
	  cpqfcTSCheckandSnoopFCP(fcChip, x_ID);  // (will set ->result)
	}
      }
      
      // default: NOTE! We don't ever want to get here.  Getting here
      // implies something new is happening that we've never had a test
      // case for.  Need code maintenance!  Return "ERROR"
      else
      {
	unsigned int stat = Exchanges->fcExchange[ x_ID ].status;
        printk("DEFAULT result %Xh, x_ID %Xh, Cmnd %p", 
          Exchanges->fcExchange[ x_ID ].status, x_ID, 
	  Exchanges->fcExchange[ x_ID ].Cmnd);

	if (stat & INVALID_ARGS)	printk(" INVALID_ARGS ");
	if (stat & LNKDWN_OSLS)		printk(" LNKDWN_OSLS ");
	if (stat & LNKDWN_LASER)	printk(" LNKDWN_LASER ");
	if (stat & OUTQUE_FULL)		printk(" OUTQUE_FULL ");
	if (stat & DRIVERQ_FULL)	printk(" DRIVERQ_FULL ");
	if (stat & SEST_FULL)		printk(" SEST_FULL ");
	if (stat & BAD_ALPA)		printk(" BAD_ALPA ");
	if (stat & OVERFLOW)		printk(" OVERFLOW ");
	if (stat & COUNT_ERROR)		printk(" COUNT_ERROR ");
	if (stat & LINKFAIL_RX)		printk(" LINKFAIL_RX ");
	if (stat & ABORTSEQ_NOTIFY)	printk(" ABORTSEQ_NOTIFY ");
	if (stat & LINKFAIL_TX)		printk(" LINKFAIL_TX ");
	if (stat & HOSTPROG_ERR)	printk(" HOSTPROG_ERR ");
	if (stat & FRAME_TO)		printk(" FRAME_TO ");
	if (stat & INV_ENTRY)		printk(" INV_ENTRY ");
	if (stat & SESTPROG_ERR)	printk(" SESTPROG_ERR ");
	if (stat & OUTBOUND_TIMEOUT)	printk(" OUTBOUND_TIMEOUT ");
	if (stat & INITIATOR_ABORT)	printk(" INITIATOR_ABORT ");
	if (stat & MEMPOOL_FAIL)	printk(" MEMPOOL_FAIL ");
	if (stat & FC2_TIMEOUT)		printk(" FC2_TIMEOUT ");
	if (stat & TARGET_ABORT)	printk(" TARGET_ABORT ");
	if (stat & EXCHANGE_QUEUED)	printk(" EXCHANGE_QUEUED ");
	if (stat & PORTID_CHANGED)	printk(" PORTID_CHANGED ");
	if (stat & DEVICE_REMOVED)	printk(" DEVICE_REMOVED ");
	if (stat & SFQ_FRAME)		printk(" SFQ_FRAME ");
	printk("\n");

        Exchanges->fcExchange[ x_ID ].Cmnd->result = (DID_ERROR <<16);
      }
    }
    else    // definitely no Tach problem, but perhaps an FCP problem
    {
      // set FCP Link statistic
      fcChip->fcStats.ok++;
      cpqfcTSCheckandSnoopFCP( fcChip, x_ID);  // (will set ->result)    
    }

    cpqfc_pci_unmap(pcidev, Exchanges->fcExchange[x_ID].Cmnd, 
			fcChip, x_ID); // undo DMA mappings.
    already_unmapped = 1;

    // OK, we've set the Scsi "->result" field, so proceed with calling
    // Linux Scsi "done" (if not NULL), and free any kernel memory we
    // may have allocated for the exchange.

  PCI_TRACEO( (ULONG)Exchanges->fcExchange[x_ID].Cmnd, 0xAC);
    // complete the command back to upper Scsi drivers
    if( Exchanges->fcExchange[ x_ID ].Cmnd->scsi_done != NULL)
    {
      // Calling "done" on an Linux _abort() aborted
      // Cmnd causes a kernel panic trying to re-free mem.
      // Actually, we shouldn't do anything with an _abort CMND
      if( Exchanges->fcExchange[ x_ID ].Cmnd->result != (DID_ABORT<<16) )
      {
        PCI_TRACE(0xAC)
	call_scsi_done(Exchanges->fcExchange[ x_ID ].Cmnd);
      }
      else
      {
//	printk(" not calling scsi_done on x_ID %Xh, Cmnd %p\n",
//			x_ID, Exchanges->fcExchange[ x_ID ].Cmnd);
      }
    }
    else{
      printk(" x_ID %Xh, type %Xh, Cdb0 %Xh\n", x_ID,
	Exchanges->fcExchange[ x_ID ].type, 
	Exchanges->fcExchange[ x_ID ].Cmnd->cmnd[0]);	      
      printk(" cpqfcTS: Null scsi_done function pointer!\n");
    }


    // Now, clean up non-Scsi_Cmnd items...
CleanUpSestResources:
   
    if (!already_unmapped) 
	cpqfc_pci_unmap(pcidev, Exchanges->fcExchange[x_ID].Cmnd, 
			fcChip, x_ID); // undo DMA mappings.

    // Was an Extended Scatter/Gather page allocated?  We know
    // this by checking DWORD 4, bit 31 ("LOC") of SEST entry
    if( !(fcChip->SEST->u[ x_ID ].IWE.Buff_Off & 0x80000000))
    {
      PSGPAGES p, next;

      // extended S/G list was used -- Free the allocated ext. S/G pages
      for (p = fcChip->SEST->sgPages[x_ID]; p != NULL; p = next) {
	 next = p->next;
	 kfree(p);
      }
      fcChip->SEST->sgPages[x_ID] = NULL;
    }
  
    Exchanges->fcExchange[ x_ID ].Cmnd = NULL; 
  }  // Done with FCP (SEST) exchanges


  // the remaining logic is common to ALL Exchanges: 
  // FCP(SEST) and LinkServ.

  Exchanges->fcExchange[ x_ID ].type = 0; // there -- FREE!  
  Exchanges->fcExchange[ x_ID ].status = 0; 

  PCI_TRACEO( x_ID, 0xAC)
     
  
  return;
}   // (END of CompleteExchange function)
 



// Unfortunately, we must snoop all command completions in
// order to manipulate certain return fields, and take note of
// device types, etc., to facilitate the Fibre-Channel to SCSI
// "mapping".  
// (Watch for BIG Endian confusion on some payload fields)
void cpqfcTSCheckandSnoopFCP( PTACHYON fcChip, ULONG x_ID)
{
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  Scsi_Cmnd *Cmnd = Exchanges->fcExchange[ x_ID].Cmnd;
  FCP_STATUS_RESPONSE *pFcpStatus = 
    (PFCP_STATUS_RESPONSE)&fcChip->SEST->RspHDR[ x_ID ].pl;
  UCHAR ScsiStatus;

  ScsiStatus = pFcpStatus->fcp_status >>24;

#ifdef FCP_COMPLETION_DBG
  printk("ScsiStatus = 0x%X\n", ScsiStatus);
#endif	

  // First, check FCP status
  if( pFcpStatus->fcp_status & FCP_RSP_LEN_VALID )
  {
    // check response code (RSP_CODE) -- most popular is bad len
    // 1st 4 bytes of rsp info -- only byte 3 interesting
    if( pFcpStatus->fcp_rsp_info & FCP_DATA_LEN_NOT_BURST_LEN )
    { 

      // do we EVER get here?
      printk("cpqfcTS: FCP data len not burst len, x_ID %Xh\n", x_ID);
    }
  }

  // for now, go by the ScsiStatus, and manipulate certain
  // commands when necessary...
  if( ScsiStatus == 0) // SCSI status byte "good"?
  {
    Cmnd->result = 0; // everything's OK

    if( (Cmnd->cmnd[0] == INQUIRY)) 
    {
      UCHAR *InquiryData = Cmnd->request_buffer;
      PFC_LOGGEDIN_PORT pLoggedInPort;

      // We need to manipulate INQUIRY
      // strings for COMPAQ RAID controllers to force
      // Linux to scan additional LUNs.  Namely, set
      // the Inquiry string byte 2 (ANSI-approved version)
      // to 2.

      if( !memcmp( &InquiryData[8], "COMPAQ", 6 ))
      {
        InquiryData[2] = 0x2;  // claim SCSI-2 compliance,
                               // so multiple LUNs may be scanned.
                               // (no SCSI-2 problems known in CPQ)
      }
        
      // snoop the Inquiry to detect Disk, Tape, etc. type
      // (search linked list for the port_id we sent INQUIRY to)
      pLoggedInPort = fcFindLoggedInPort( fcChip,
        NULL,     // DON'T search Scsi Nexus (we will set it)
        Exchanges->fcExchange[ x_ID].fchs.d_id & 0xFFFFFF,        
        NULL,     // DON'T search linked list for FC WWN
        NULL);    // DON'T care about end of list
 
      if( pLoggedInPort )
      {
        pLoggedInPort->ScsiNexus.InqDeviceType = InquiryData[0];
      }
      else
      {
	printk("cpqfcTS: can't find LoggedIn FC port %06X for INQUIRY\n",
          Exchanges->fcExchange[ x_ID].fchs.d_id & 0xFFFFFF);
      }
    }
  }


  // Scsi Status not good -- pass it back to caller 

  else
  {
    Cmnd->result = ScsiStatus; // SCSI status byte is 1st
    
    // check for valid "sense" data

    if( pFcpStatus->fcp_status & FCP_SNS_LEN_VALID ) 
    {            // limit Scsi Sense field length!
      int SenseLen = pFcpStatus->fcp_sns_len >>24; // (BigEndian) lower byte
      
      SenseLen = SenseLen > sizeof( Cmnd->sense_buffer) ? 
        sizeof( Cmnd->sense_buffer) : SenseLen;
	   

#ifdef FCP_COMPLETION_DBG	    
      printk("copy sense_buffer %p, len %d, result %Xh\n",
        Cmnd->sense_buffer, SenseLen, Cmnd->result);
#endif	  

      // NOTE: There is some dispute over the FCP response
      // format.  Most FC devices assume that FCP_RSP_INFO
      // is 8 bytes long, in spite of the fact that FCP_RSP_LEN
      // is (virtually) always 0 and the field is "invalid".  
      // Some other devices assume that
      // the FCP_SNS_INFO begins after FCP_RSP_LEN bytes (i.e. 0)
      // when the FCP_RSP is invalid (this almost appears to be
      // one of those "religious" issues).
      // Consequently, we test the usual position of FCP_SNS_INFO
      // for 7Xh, since the SCSI sense format says the first
      // byte ("error code") should be 0x70 or 0x71.  In practice,
      // we find that every device does in fact have 0x70 or 0x71
      // in the first byte position, so this test works for all
      // FC devices.  
      // (This logic is especially effective for the CPQ/DEC HSG80
      // & HSG60 controllers).

      if( (pFcpStatus->fcp_sns_info[0] & 0x70) == 0x70 )
        memcpy( Cmnd->sense_buffer, 
          &pFcpStatus->fcp_sns_info[0], SenseLen);
      else
      {
        unsigned char *sbPtr = 
		(unsigned char *)&pFcpStatus->fcp_sns_info[0];
        sbPtr -= 8;  // back up 8 bytes hoping to find the
	             // start of the sense buffer
        memcpy( Cmnd->sense_buffer, sbPtr, SenseLen);
      }

      // in the special case of Device Reset, tell upper layer
      // to immediately retry (with SOFT_ERROR status)
      // look for Sense Key Unit Attention (0x6) with ASC Device
      // Reset (0x29)
      //	    printk("SenseLen %d, Key = 0x%X, ASC = 0x%X\n",
      //		    SenseLen, Cmnd->sense_buffer[2], 
      //                   Cmnd->sense_buffer[12]);
      if( ((Cmnd->sense_buffer[2] & 0xF) == 0x6) &&
	        (Cmnd->sense_buffer[12] == 0x29) ) // Sense Code "reset"
      {
        Cmnd->result |= (DID_SOFT_ERROR << 16); // "Host" status byte 3rd
      }
 
      // check for SenseKey "HARDWARE ERROR", ASC InternalTargetFailure
      else if(  ((Cmnd->sense_buffer[2] & 0xF) == 0x4) &&  // "hardware error"
      	        (Cmnd->sense_buffer[12] == 0x44) ) // Addtl. Sense Code 
      {
//        printk("HARDWARE_ERROR, Channel/Target/Lun %d/%d/%d\n",
//		Cmnd->channel, Cmnd->target, Cmnd->lun);
      	Cmnd->result |= (DID_ERROR << 16); // "Host" status byte 3rd
      }
      
    }  // (end of sense len valid)

    // there is no sense data to help out Linux's Scsi layers...
    // We'll just return the Scsi status and hope he will "do the 
    // right thing"
    else
    {
      // as far as we know, the Scsi status is sufficient
      Cmnd->result |= (DID_OK << 16); // "Host" status byte 3rd
    }
  }
}



//PPPPPPPPPPPPPPPPPPPPPPPPP  PAYLOAD  PPPPPPPPP
// build data PAYLOAD; SCSI FCP_CMND I.U.
// remember BIG ENDIAN payload - DWord values must be byte-reversed
// (hence the affinity for byte pointer building).

static int build_FCP_payload( Scsi_Cmnd *Cmnd, 
      UCHAR* payload, ULONG type, ULONG fcp_dl )
{
  int i;

  
  switch( type)
  {
		  
    case SCSI_IWE: 
    case SCSI_IRE:        
      // 8 bytes FCP_LUN
      // Peripheral Device or Volume Set addressing, and LUN mapping
      // When the FC port was looked up, we copied address mode
      // and any LUN mask to the scratch pad SCp.phase & .mode

      *payload++ = (UCHAR)Cmnd->SCp.phase;

      // Now, because of "lun masking" 
      // (aka selective storage presentation),
      // the contiguous Linux Scsi lun number may not match the
      // device's lun number, so we may have to "map".  
      
      *payload++ = (UCHAR)Cmnd->SCp.have_data_in;
      
      // We don't know of anyone in the FC business using these 
      // extra "levels" of addressing.  In fact, confusion still exists
      // just using the FIRST level... ;-)
      
      *payload++ = 0;  // 2nd level addressing
      *payload++ = 0;
      *payload++ = 0;  // 3rd level addressing
      *payload++ = 0;
      *payload++ = 0;  // 4th level addressing
      *payload++ = 0;

      // 4 bytes Control Field FCP_CNTL
      *payload++ = 0;    // byte 0: (MSB) reserved
      *payload++ = 0;    // byte 1: task codes

                         // byte 2: task management flags
      // another "use" of the spare field to accomplish TDR
      // note combination needed
      if( (Cmnd->cmnd[0] == RELEASE) &&
          (Cmnd->SCp.buffers_residual == FCP_TARGET_RESET) )
      {
        Cmnd->cmnd[0] = 0;    // issue "Test Unit Ready" for TDR
        *payload++ = 0x20;    // target device reset bit
      }
      else
        *payload++ = 0;    // no TDR
		      // byte 3: (LSB) execution management codes
		      // bit 0 write, bit 1 read (don't set together)
      
      if( fcp_dl != 0 )
      {
        if( type == SCSI_IWE )         // WRITE
          *payload++ = 1;
        else                           // READ
          *payload++ = 2;
      }
      else
      {
	// On some devices, if RD or WR bits are set,
	// and fcp_dl is 0, they will generate an error on the command.
	// (i.e., if direction is specified, they insist on a length).
	*payload++ = 0;                // no data (necessary for CPQ)
      }


      // NOTE: clean this up if/when MAX_COMMAND_SIZE is increased to 16
      // FCP_CDB allows 16 byte SCSI command descriptor blk;
      // Linux SCSI CDB array is MAX_COMMAND_SIZE (12 at this time...)
      for( i=0; (i < Cmnd->cmd_len) && i < MAX_COMMAND_SIZE; i++)
	*payload++ = Cmnd->cmnd[i];

      // if( Cmnd->cmd_len == 16 )
      // {
      //  memcpy( payload, &Cmnd->SCp.buffers_residual, 4);
      // }
      payload+= (16 - i);  

		      // FCP_DL is largest number of expected data bytes
		      // per CDB (i.e. read/write command)
      *payload++ = (UCHAR)(fcp_dl >>24);  // (MSB) 8 bytes data len FCP_DL
      *payload++ = (UCHAR)(fcp_dl >>16);
      *payload++ = (UCHAR)(fcp_dl >>8);
      *payload++ = (UCHAR)fcp_dl;    // (LSB)
      break;

    case SCSI_TWE:          // need FCP_XFER_RDY
      *payload++ = 0;     // (4 bytes) DATA_RO (MSB byte 0)
      *payload++ = 0;
      *payload++ = 0;
      *payload++ = 0;     // LSB (byte 3)
			     // (4 bytes) BURST_LEN
			     // size of following FCP_DATA payload
      *payload++ = (UCHAR)(fcp_dl >>24);  // (MSB) 8 bytes data len FCP_DL
      *payload++ = (UCHAR)(fcp_dl >>16);
      *payload++ = (UCHAR)(fcp_dl >>8);
      *payload++ = (UCHAR)fcp_dl;    // (LSB)
		       // 4 bytes RESERVED
      *payload++ = 0;
      *payload++ = 0;
      *payload++ = 0;
      *payload++ = 0;
      break;

    default:
      break;
  }

  return 0;
}

