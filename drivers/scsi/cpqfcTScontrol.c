/* Copyright 2000, Compaq Computer Corporation 
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
/* These functions control the host bus adapter (HBA) hardware.  The main chip
   control takes place in the interrupt handler where we process the IMQ 
   (Inbound Message Queue).  The IMQ is Tachyon's way of communicating FC link
   events and state information to the driver.  The Single Frame Queue (SFQ)
   buffers incoming FC frames for processing by the driver.  References to 
   "TL/TS UG" are for:
   "HP HPFC-5100/5166 Tachyon TL/TS ICs User Guide", August 16, 1999, 1st Ed.
   Hewlitt Packard Manual Part Number 5968-1083E.
*/

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>  // request_region() prototype
#include <linux/sched.h>
#include <linux/slab.h>  // need "kfree" for ext. S/G pages
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/unistd.h>
#include <asm/io.h>  // struct pt_regs for IRQ handler & Port I/O
#include <asm/irq.h>
#include <linux/spinlock.h>

#include "scsi.h"
#include <scsi/scsi_host.h>   // Scsi_Host definition for INT handler
#include "cpqfcTSchip.h"
#include "cpqfcTSstructs.h"

//#define IMQ_DEBUG 1

static void fcParseLinkStatusCounters(TACHYON * fcChip);
static void CpqTsGetSFQEntry(TACHYON * fcChip, 
	      USHORT pi, ULONG * buffr, BOOLEAN UpdateChip); 

static void 
cpqfc_free_dma_consistent(CPQFCHBA *cpqfcHBAdata)
{
  	// free up the primary EXCHANGES struct and Link Q
	PTACHYON fcChip = &cpqfcHBAdata->fcChip;

	if (fcChip->Exchanges != NULL)
		pci_free_consistent(cpqfcHBAdata->PciDev, sizeof(FC_EXCHANGES),
			fcChip->Exchanges, fcChip->exch_dma_handle);
	fcChip->Exchanges = NULL;
	if (cpqfcHBAdata->fcLQ != NULL)
		pci_free_consistent(cpqfcHBAdata->PciDev, sizeof(FC_LINK_QUE),
			cpqfcHBAdata->fcLQ, cpqfcHBAdata->fcLQ_dma_handle);
	cpqfcHBAdata->fcLQ = NULL;
}

// Note special requirements for Q alignment!  (TL/TS UG pg. 190)
// We place critical index pointers at end of QUE elements to assist
// in non-symbolic (i.e. memory dump) debugging
// opcode defines placement of Queues (e.g. local/external RAM)

int CpqTsCreateTachLiteQues( void* pHBA, int opcode)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA*)pHBA;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;

  int iStatus=0;
  unsigned long ulAddr;
  dma_addr_t ERQdma, IMQdma, SPQdma, SESTdma;
  int i;

  // NOTE! fcMemManager() will return system virtual addresses.
  // System (kernel) virtual addresses, though non-paged, still
  // aren't physical addresses.  Convert to PHYSICAL_ADDRESS for Tachyon's
  // DMA use.
  ENTER("CreateTachLiteQues");


  // Allocate primary EXCHANGES array...
  fcChip->Exchanges = NULL;
  cpqfcHBAdata->fcLQ = NULL;
  
  /* printk("Allocating %u for %u Exchanges ", 
	  (ULONG)sizeof(FC_EXCHANGES), TACH_MAX_XID); */
  fcChip->Exchanges = pci_alloc_consistent(cpqfcHBAdata->PciDev, 
			sizeof(FC_EXCHANGES), &fcChip->exch_dma_handle);
  /* printk("@ %p\n", fcChip->Exchanges); */

  if( fcChip->Exchanges == NULL ) // fatal error!!
  {
    printk("pci_alloc_consistent failure on Exchanges: fatal error\n");
    return -1;
  }
  // zero out the entire EXCHANGE space
  memset( fcChip->Exchanges, 0, sizeof( FC_EXCHANGES));  


  /* printk("Allocating %u for LinkQ ", (ULONG)sizeof(FC_LINK_QUE)); */
  cpqfcHBAdata->fcLQ = pci_alloc_consistent(cpqfcHBAdata->PciDev,
				 sizeof( FC_LINK_QUE), &cpqfcHBAdata->fcLQ_dma_handle);
  /* printk("@ %p (%u elements)\n", cpqfcHBAdata->fcLQ, FC_LINKQ_DEPTH); */

  if( cpqfcHBAdata->fcLQ == NULL ) // fatal error!!
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk("pci_alloc_consistent() failure on fc Link Que: fatal error\n");
    return -1;
  }
  // zero out the entire EXCHANGE space
  memset( cpqfcHBAdata->fcLQ, 0, sizeof( FC_LINK_QUE));  
  
  // Verify that basic Tach I/O registers are not NULL  
  if( !fcChip->Registers.ReMapMemBase )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk("HBA base address NULL: fatal error\n");
    return -1;
  }


  // Initialize the fcMemManager memory pairs (stores allocated/aligned
  // pairs for future freeing)
  memset( cpqfcHBAdata->dynamic_mem, 0, sizeof(cpqfcHBAdata->dynamic_mem));
  

  // Allocate Tach's Exchange Request Queue (each ERQ entry 32 bytes)
  
  fcChip->ERQ = fcMemManager( cpqfcHBAdata->PciDev, 
			&cpqfcHBAdata->dynamic_mem[0], 
			sizeof( TachLiteERQ ), 32*(ERQ_LEN), 0L, &ERQdma);
  if( !fcChip->ERQ )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk("pci_alloc_consistent/alignment failure on ERQ: fatal error\n");
    return -1;
  }
  fcChip->ERQ->length = ERQ_LEN-1;
  ulAddr = (ULONG) ERQdma; 
#if BITS_PER_LONG > 32
  if( (ulAddr >> 32) )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk(" FATAL! ERQ ptr %p exceeds Tachyon's 32-bit register size\n",
		    (void*)ulAddr);
    return -1;  // failed
  }
#endif
  fcChip->ERQ->base = (ULONG)ulAddr;  // copy for quick reference


  // Allocate Tach's Inbound Message Queue (32 bytes per entry)
  
  fcChip->IMQ = fcMemManager( cpqfcHBAdata->PciDev, 
		  &cpqfcHBAdata->dynamic_mem[0],
		  sizeof( TachyonIMQ ), 32*(IMQ_LEN), 0L, &IMQdma );
  if( !fcChip->IMQ )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk("pci_alloc_consistent/alignment failure on IMQ: fatal error\n");
    return -1;
  }
  fcChip->IMQ->length = IMQ_LEN-1;

  ulAddr = IMQdma;
#if BITS_PER_LONG > 32
  if( (ulAddr >> 32) )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk(" FATAL! IMQ ptr %p exceeds Tachyon's 32-bit register size\n",
		    (void*)ulAddr);
    return -1;  // failed
  }
#endif
  fcChip->IMQ->base = (ULONG)ulAddr;  // copy for quick reference


  // Allocate Tach's  Single Frame Queue (64 bytes per entry)
  fcChip->SFQ = fcMemManager( cpqfcHBAdata->PciDev, 
		  &cpqfcHBAdata->dynamic_mem[0],
		  sizeof( TachLiteSFQ ), 64*(SFQ_LEN),0L, &SPQdma );
  if( !fcChip->SFQ )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk("pci_alloc_consistent/alignment failure on SFQ: fatal error\n");
    return -1;
  }
  fcChip->SFQ->length = SFQ_LEN-1;      // i.e. Que length [# entries -
                                       // min. 32; max.  4096 (0xffff)]
  
  ulAddr = SPQdma;
#if BITS_PER_LONG > 32
  if( (ulAddr >> 32) )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk(" FATAL! SFQ ptr %p exceeds Tachyon's 32-bit register size\n",
		    (void*)ulAddr);
    return -1;  // failed
  }
#endif
  fcChip->SFQ->base = (ULONG)ulAddr;  // copy for quick reference


  // Allocate SCSI Exchange State Table; aligned nearest @sizeof
  // power-of-2 boundary
  // LIVE DANGEROUSLY!  Assume the boundary for SEST mem will
  // be on physical page (e.g. 4k) boundary.
  /* printk("Allocating %u for TachSEST for %u Exchanges\n", 
		 (ULONG)sizeof(TachSEST), TACH_SEST_LEN); */
  fcChip->SEST = fcMemManager( cpqfcHBAdata->PciDev,
		  &cpqfcHBAdata->dynamic_mem[0],
		  sizeof(TachSEST),  4, 0L, &SESTdma );
//		  sizeof(TachSEST),  64*TACH_SEST_LEN, 0L );
  if( !fcChip->SEST )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk("pci_alloc_consistent/alignment failure on SEST: fatal error\n");
    return -1;
  }

  for( i=0; i < TACH_SEST_LEN; i++)  // for each exchange
      fcChip->SEST->sgPages[i] = NULL;

  fcChip->SEST->length = TACH_SEST_LEN;  // e.g. DON'T subtract one 
                                       // (TL/TS UG, pg 153)

  ulAddr = SESTdma; 
#if BITS_PER_LONG > 32
  if( (ulAddr >> 32) )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk(" FATAL! SFQ ptr %p exceeds Tachyon's 32-bit register size\n",
		    (void*)ulAddr);
    return -1;  // failed
  }
#endif
  fcChip->SEST->base = (ULONG)ulAddr;  // copy for quick reference


			      // Now that structures are defined,
			      // fill in Tachyon chip registers...

			      // EEEEEEEE  EXCHANGE REQUEST QUEUE

  writel( fcChip->ERQ->base, 
    (fcChip->Registers.ReMapMemBase + TL_MEM_ERQ_BASE));
      
  writel( fcChip->ERQ->length,
    (fcChip->Registers.ReMapMemBase + TL_MEM_ERQ_LENGTH));
     

  fcChip->ERQ->producerIndex = 0L;
  writel( fcChip->ERQ->producerIndex,
    (fcChip->Registers.ReMapMemBase + TL_MEM_ERQ_PRODUCER_INDEX));
      

		// NOTE! write consumer index last, since the write
		// causes Tachyon to process the other registers

  ulAddr = ((unsigned long)&fcChip->ERQ->consumerIndex - 
		(unsigned long)fcChip->ERQ) + (unsigned long) ERQdma;

  // NOTE! Tachyon DMAs to the ERQ consumer Index host
		// address; must be correctly aligned
  writel( (ULONG)ulAddr,
    (fcChip->Registers.ReMapMemBase + TL_MEM_ERQ_CONSUMER_INDEX_ADR));



				 // IIIIIIIIIIIII  INBOUND MESSAGE QUEUE
				 // Tell Tachyon where the Que starts

  // set the Host's pointer for Tachyon to access

  /* printk("  cpqfcTS: writing IMQ BASE %Xh  ", fcChip->IMQ->base ); */
  writel( fcChip->IMQ->base, 
    (fcChip->Registers.ReMapMemBase + IMQ_BASE));

  writel( fcChip->IMQ->length,
    (fcChip->Registers.ReMapMemBase + IMQ_LENGTH));

  writel( fcChip->IMQ->consumerIndex,
    (fcChip->Registers.ReMapMemBase + IMQ_CONSUMER_INDEX));


		// NOTE: TachLite DMAs to the producerIndex host address
		// must be correctly aligned with address bits 1-0 cleared
    // Writing the BASE register clears the PI register, so write it last
  ulAddr = ((unsigned long)&fcChip->IMQ->producerIndex - 
		(unsigned long)fcChip->IMQ) + (unsigned long) IMQdma;

#if BITS_PER_LONG > 32
  if( (ulAddr >> 32) )
  {
    cpqfc_free_dma_consistent(cpqfcHBAdata);
    printk(" FATAL! IMQ ptr %p exceeds Tachyon's 32-bit register size\n",
		    (void*)ulAddr);
    return -1;  // failed
  }
#endif
#if DBG
  printk("  PI %Xh\n", (ULONG)ulAddr );
#endif
  writel( (ULONG)ulAddr, 
    (fcChip->Registers.ReMapMemBase + IMQ_PRODUCER_INDEX));



				 // SSSSSSSSSSSSSSS SINGLE FRAME SEQUENCE
				 // Tell TachLite where the Que starts

  writel( fcChip->SFQ->base, 
    (fcChip->Registers.ReMapMemBase + TL_MEM_SFQ_BASE));

  writel( fcChip->SFQ->length,
    (fcChip->Registers.ReMapMemBase + TL_MEM_SFQ_LENGTH));


         // tell TachLite where SEST table is & how long
  writel( fcChip->SEST->base,
    (fcChip->Registers.ReMapMemBase + TL_MEM_SEST_BASE));

  /* printk("  cpqfcTS: SEST %p(virt): Wrote base %Xh @ %p\n",
    fcChip->SEST, fcChip->SEST->base, 
    fcChip->Registers.ReMapMemBase + TL_MEM_SEST_BASE); */

  writel( fcChip->SEST->length,
    (fcChip->Registers.ReMapMemBase + TL_MEM_SEST_LENGTH));
      
  writel( (TL_EXT_SG_PAGE_COUNT-1),
    (fcChip->Registers.ReMapMemBase + TL_MEM_SEST_SG_PAGE));


  LEAVE("CreateTachLiteQues");

  return iStatus;
}



// function to return TachLite to Power On state
// 1st - reset tachyon ('SOFT' reset)
// others - future

int CpqTsResetTachLite(void *pHBA, int type)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA*)pHBA;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  ULONG ulBuff, i;
  int ret_status=0; // def. success

  ENTER("ResetTach");
  
  switch(type)
  {

    case CLEAR_FCPORTS:

      // in case he was running previously, mask Tach's interrupt
      writeb( 0, (fcChip->Registers.ReMapMemBase + IINTEN));
      
     // de-allocate mem for any Logged in ports
      // (e.g., our module is unloading)
      // search the forward linked list, de-allocating
      // the memory we allocated when the port was initially logged in
      {
        PFC_LOGGEDIN_PORT pLoggedInPort = fcChip->fcPorts.pNextPort;
        PFC_LOGGEDIN_PORT ptr;
//        printk("checking for allocated LoggedInPorts...\n");
			
        while( pLoggedInPort )
        {
          ptr = pLoggedInPort;
          pLoggedInPort = ptr->pNextPort;
//	  printk("kfree(%p) on FC LoggedInPort port_id 0x%06lX\n",
//			  ptr, ptr->port_id);
          kfree( ptr );
        }
      }
      // (continue resetting hardware...)

    case 1:                   // RESTART Tachyon (power-up state)

      // in case he was running previously, mask Tach's interrupt
      writeb( 0, (fcChip->Registers.ReMapMemBase + IINTEN));
			      // turn OFF laser (NOTE: laser is turned
                              // off during reset, because GPIO4 is cleared
                              // to 0 by reset action - see TLUM, sec 7.22)
                              // However, CPQ 64-bit HBAs have a "health
                              // circuit" which keeps laser ON for a brief
                              // period after it is turned off ( < 1s)
      
      fcChip->LaserControl( fcChip->Registers.ReMapMemBase, 0);
  


            // soft reset timing constraints require:
            //   1. set RST to 1
            //   2. read SOFTRST register 
            //      (128 times per R. Callison code)
            //   3. clear PCI ints
            //   4. clear RST to 0
      writel( 0xff000001L,
        (fcChip->Registers.ReMapMemBase + TL_MEM_SOFTRST));
        
      for( i=0; i<128; i++)
        ulBuff = readl( fcChip->Registers.ReMapMemBase + TL_MEM_SOFTRST);

        // clear the soft reset
      for( i=0; i<8; i++)
  	writel( 0, (fcChip->Registers.ReMapMemBase + TL_MEM_SOFTRST));

               

			       // clear out our copy of Tach regs,
			       // because they must be invalid now,
			       // since TachLite reset all his regs.
      CpqTsDestroyTachLiteQues(cpqfcHBAdata,0); // remove Host-based Que structs
      cpqfcTSClearLinkStatusCounters(fcChip);  // clear our s/w accumulators
                               // lower bits give GBIC info
      fcChip->Registers.TYstatus.value = 
	              readl( fcChip->Registers.TYstatus.address );
      break;

/*
    case 2:                   // freeze SCSI
    case 3:                   // reset Outbound command que (ERQ)
    case 4:                   // unfreeze OSM (Outbound Seq. Man.) 'er'
    case 5:                   // report status

    break;
*/
    default:
      ret_status = -1;  // invalid option passed to RESET function
      break;
  }
  LEAVE("ResetTach");
  return ret_status;
}






// 'addrBase' is IOBaseU for both TachLite and (older) Tachyon
int CpqTsLaserControl( void* addrBase, int opcode )
{
  ULONG dwBuff;

  dwBuff = readl((addrBase + TL_MEM_TACH_CONTROL) ); // read TL Control reg
                                                    // (change only bit 4)
  if( opcode == 1)
    dwBuff |= ~0xffffffefL; // set - ON
  else
    dwBuff &= 0xffffffefL;  // clear - OFF
  writel( dwBuff, (addrBase + TL_MEM_TACH_CONTROL)); // write TL Control reg
  return 0;
}





// Use controller's "Options" field to determine loopback mode (if any)
//   internal loopback (silicon - no GBIC)
//   external loopback (GBIC - no FC loop)
//   no loopback: L_PORT, external cable from GBIC required

int CpqTsInitializeFrameManager( void *pChip, int opcode)
{
  PTACHYON fcChip;
  int iStatus;
  ULONG wwnLo, wwnHi; // for readback verification

  ENTER("InitializeFrameManager");
  fcChip = (PTACHYON)pChip;
  if( !fcChip->Registers.ReMapMemBase )   // undefined controller?
    return -1;

  // TL/TS UG, pg. 184
  // 0x0065 = 100ms for RT_TOV
  // 0x01f5 = 500ms for ED_TOV
  // 0x07D1 = 2000ms 
  fcChip->Registers.ed_tov.value = 0x006507D1; 
  writel( fcChip->Registers.ed_tov.value,
    (fcChip->Registers.ed_tov.address));
      

  // Set LP_TOV to the FC-AL2 specified 2 secs.
  // TL/TS UG, pg. 185
  writel( 0x07d00010, fcChip->Registers.ReMapMemBase +TL_MEM_FM_TIMEOUT2);


  // Now try to read the WWN from the adapter's NVRAM
  iStatus = CpqTsReadWriteWWN( fcChip, 1); // '1' for READ

  if( iStatus )   // NVRAM read failed?
  {
    printk(" WARNING! HBA NVRAM WWN read failed - make alias\n");
    // make up a WWN.  If NULL or duplicated on loop, FC loop may hang!


    fcChip->Registers.wwn_hi = (__u32)jiffies;
    fcChip->Registers.wwn_hi |= 0x50000000L;
    fcChip->Registers.wwn_lo = 0x44556677L;
  }

  
  writel( fcChip->Registers.wwn_hi, 
	  fcChip->Registers.ReMapMemBase + TL_MEM_FM_WWN_HI);
  
  writel( fcChip->Registers.wwn_lo, 
	  fcChip->Registers.ReMapMemBase + TL_MEM_FM_WWN_LO);
	  

  // readback for verification:
  wwnHi = readl( fcChip->Registers.ReMapMemBase + TL_MEM_FM_WWN_HI ); 
          
  wwnLo = readl( fcChip->Registers.ReMapMemBase + TL_MEM_FM_WWN_LO);
  // test for correct chip register WRITE/READ
  DEBUG_PCI( printk("  WWN %08X%08X\n",
    fcChip->Registers.wwn_hi, fcChip->Registers.wwn_lo ) );
    
  if( wwnHi != fcChip->Registers.wwn_hi ||
      wwnLo != fcChip->Registers.wwn_lo )
  {
    printk( "cpqfcTS: WorldWideName register load failed\n");
    return -1; // FAILED!
  }



			// set Frame Manager Initialize command
  fcChip->Registers.FMcontrol.value = 0x06;

  // Note: for test/debug purposes, we may use "Hard" address,
  // but we completely support "soft" addressing, including
  // dynamically changing our address.
  if( fcChip->Options.intLoopback == 1 )            // internal loopback
    fcChip->Registers.FMconfig.value = 0x0f002080L;
  else if( fcChip->Options.extLoopback == 1 )            // internal loopback
    fcChip->Registers.FMconfig.value = 0x0f004080L;
  else                  // L_Port
    fcChip->Registers.FMconfig.value = 0x55000100L; // hard address (55h start)
//    fcChip->Registers.FMconfig.value = 0x01000080L; // soft address (can't pick)
//    fcChip->Registers.FMconfig.value = 0x55000100L; // hard address (55h start)
		
  // write config to FM

  if( !fcChip->Options.intLoopback && !fcChip->Options.extLoopback )
                               // (also need LASER for real LOOP)
    fcChip->LaserControl( fcChip->Registers.ReMapMemBase, 1); // turn on LASER

  writel( fcChip->Registers.FMconfig.value,
    fcChip->Registers.FMconfig.address);
    

			       // issue INITIALIZE command to FM - ACTION!
  writel( fcChip->Registers.FMcontrol.value,
    fcChip->Registers.FMcontrol.address);
    
  LEAVE("InitializeFrameManager");
  
  return 0;
}





// This "look ahead" function examines the IMQ for occurrence of
// "type".  Returns 1 if found, 0 if not.
static int PeekIMQEntry( PTACHYON fcChip, ULONG type)
{
  ULONG CI = fcChip->IMQ->consumerIndex;
  ULONG PI = fcChip->IMQ->producerIndex; // snapshot of IMQ indexes
  
  while( CI != PI )
  {                             // proceed with search
    if( (++CI) >= IMQ_LEN ) CI = 0; // rollover check
    
    switch( type )
    {
      case ELS_LILP_FRAME:
      {
      // first, we need to find an Inbound Completion message,
      // If we find it, check the incoming frame payload (1st word)
      // for LILP frame
        if( (fcChip->IMQ->QEntry[CI].type & 0x1FF) == 0x104 )
        { 
          TachFCHDR_GCMND* fchs;
#error This is too much stack
          ULONG ulFibreFrame[2048/4];  // max DWORDS in incoming FC Frame
	  USHORT SFQpi = (USHORT)(fcChip->IMQ->QEntry[CI].word[0] & 0x0fffL);

	  CpqTsGetSFQEntry( fcChip,
            SFQpi,        // SFQ producer ndx         
	    ulFibreFrame, // contiguous dest. buffer
	    FALSE);       // DON'T update chip--this is a "lookahead"
          
	  fchs = (TachFCHDR_GCMND*)&ulFibreFrame;
          if( fchs->pl[0] == ELS_LILP_FRAME)
	  {
            return 1; // found the LILP frame!
	  }
	  else
	  {
	    // keep looking...
	  }
	}  
      }
      break;

      case OUTBOUND_COMPLETION:
        if( (fcChip->IMQ->QEntry[CI].type & 0x1FF) == 0x00 )
	{

          // any OCM errors?
          if( fcChip->IMQ->QEntry[CI].word[2] & 0x7a000000L )
            return 1;   	    // found OCM error
	}
      break;


      
      default:
      break;
    }
  }
  return 0; // failed to find "type"
}

			
static void SetTachTOV( CPQFCHBA* cpqfcHBAdata)
{
  PTACHYON fcChip = &cpqfcHBAdata->fcChip; 
  
  // TL/TS UG, pg. 184
  // 0x0065 = 100ms for RT_TOV
  // 0x01f5 = 500ms for ED_TOV
  // 0x07d1 = 2000ms for ED_TOV

  // SANMark Level 1 requires an "initialization backoff"
  // (See "SANMark Test Suite Level 1":
  // initialization_timeout.fcal.SANMark-1.fc)
  // We have to use 2sec, 24sec, then 128sec when login/
  // port discovery processes fail to complete.
  
  // when port discovery completes (logins done), we set
  // ED_TOV to 500ms -- this is the normal operational case
  // On the first Link Down, we'll move to 2 secs (7D1 ms)
  if( (fcChip->Registers.ed_tov.value &0xFFFF) <= 0x1f5)
    fcChip->Registers.ed_tov.value = 0x006507D1; 
  
  // If we get another LST after we moved TOV to 2 sec,
  // increase to 24 seconds (5DC1 ms) per SANMark!
  else if( (fcChip->Registers.ed_tov.value &0xFFFF) <= 0x7D1)
    fcChip->Registers.ed_tov.value = 0x00655DC1; 

  // If we get still another LST, set the max TOV (Tachyon
  // has only 16 bits for ms timer, so the max is 65.5 sec)
  else if( (fcChip->Registers.ed_tov.value &0xFFFF) <= 0x5DC1)
    fcChip->Registers.ed_tov.value = 0x0065FFFF; 

  writel( fcChip->Registers.ed_tov.value,
    (fcChip->Registers.ed_tov.address));
  // keep the same 2sec LP_TOV 
  writel( 0x07D00010, fcChip->Registers.ReMapMemBase +TL_MEM_FM_TIMEOUT2);
}	


// The IMQ is an array with IMQ_LEN length, each element (QEntry)
// with eight 32-bit words.  Tachyon PRODUCES a QEntry with each
// message it wants to send to the host.  The host CONSUMES IMQ entries

// This function copies the current
// (or oldest not-yet-processed) QEntry to
// the caller, clears/ re-enables the interrupt, and updates the
// (Host) Consumer Index.
// Return value:
//  0   message processed, none remain (producer and consumer
//        indexes match)
//  1   message processed, more messages remain
// -1   no message processed - none were available to process
// Remarks:
//   TL/TS UG specifices that the following actions for
//   INTA_L handling:
//   1. read PCI Interrupt Status register (0xff)
//   2. all IMQ messages should be processed before writing the
//      IMQ consumer index.


int CpqTsProcessIMQEntry(void *host)
{
  struct Scsi_Host *HostAdapter = (struct Scsi_Host *)host;
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA *)HostAdapter->hostdata;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip; 
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  int iStatus;
  USHORT i, RPCset, DPCset;
  ULONG x_ID;
  ULONG ulBuff, dwStatus;
  TachFCHDR_GCMND* fchs;
#error This is too much stack
  ULONG ulFibreFrame[2048/4];  // max number of DWORDS in incoming Fibre Frame
  UCHAR ucInboundMessageType;  // Inbound CM, dword 3 "type" field

  ENTER("ProcessIMQEntry");
   

				// check TachLite's IMQ producer index -
				// is a new message waiting for us?
				// equal indexes means empty que

  if( fcChip->IMQ->producerIndex != fcChip->IMQ->consumerIndex )
  {                             // need to process message


#ifdef IMQ_DEBUG
    printk("PI %X, CI %X  type: %X\n", 
      fcChip->IMQ->producerIndex,fcChip->IMQ->consumerIndex,
      fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].type);
#endif                                
    // Examine Completion Messages in IMQ
    // what CM_Type?
    switch( (UCHAR)(fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].type
                    & 0xffL) )
    {
    case OUTBOUND_COMPLETION:

      // Remarks:
      // x_IDs (OX_ID, RX_ID) are partitioned by SEST entries
      // (starting at 0), and SFS entries (starting at
      // SEST_LEN -- outside the SEST space).
      // Psuedo code:
      // x_ID (OX_ID or RX_ID) from message is Trans_ID or SEST index
      // range check - x_ID
      //   if x_ID outside 'Transactions' length, error - exit
      // if any OCM error, copy error status to Exchange slot
      // if FCP ASSIST transaction (x_ID within SEST),
      //   call fcComplete (to App)
      // ...


      ulBuff = fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[1];
      x_ID = ulBuff & 0x7fffL;     // lower 14 bits SEST_Index/Trans_ID
                                     // Range check CM OX/RX_ID value...
      if( x_ID < TACH_MAX_XID )   // don't go beyond array space
      {


	if( ulBuff & 0x20000000L ) // RPC -Response Phase Complete?
          RPCset = 1;              // (SEST transactions only)
        else
          RPCset = 0;

        if( ulBuff & 0x40000000L ) // DPC -Data Phase Complete?
          DPCset = 1;              // (SEST transactions only)
        else
          DPCset = 0;
                // set the status for this Outbound transaction's ID
        dwStatus = 0L;
        if( ulBuff & 0x10000000L ) // SPE? (SEST Programming Error)
            dwStatus |= SESTPROG_ERR;

        ulBuff = fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[2];
        if( ulBuff & 0x7a000000L ) // any other errs?
        {
          if( ulBuff & 0x40000000L )
            dwStatus |= INV_ENTRY;
          if( ulBuff & 0x20000000L )
            dwStatus |= FRAME_TO;        // FTO
          if( ulBuff & 0x10000000L )
            dwStatus |= HOSTPROG_ERR;
          if( ulBuff & 0x08000000L )
            dwStatus |= LINKFAIL_TX;
          if( ulBuff & 0x02000000L )
            dwStatus |= ABORTSEQ_NOTIFY;  // ASN
        }

	  
	if( dwStatus )          // any errors?
        {
                  // set the Outbound Completion status
          Exchanges->fcExchange[ x_ID ].status |= dwStatus;

          // if this Outbound frame was for a SEST entry, automatically
          // reque it in the case of LINKFAIL (it will restart on PDISC)
          if( x_ID < TACH_SEST_LEN )
          {

            printk(" #OCM error %Xh x_ID %X# ", 
		    dwStatus, x_ID);

	    Exchanges->fcExchange[x_ID].timeOut = 30000; // seconds default
                                                 

	    // We Q ABTS for each exchange.
	    // NOTE: We can get FRAME_TO on bad alpa (device gone).  Since
	    // bad alpa is reported before FRAME_TO, examine the status
	    // flags to see if the device is removed.  If so, DON'T
	    // post an ABTS, since it will be terminated by the bad alpa
	    // message.
	    if( dwStatus & FRAME_TO ) // check for device removed...
	    {
	      if( !(Exchanges->fcExchange[x_ID].status & DEVICE_REMOVED) )
	      { 
		// presumes device is still there: send ABTS.
  
                cpqfcTSPutLinkQue( cpqfcHBAdata, BLS_ABTS, &x_ID);
	      }
	    }
	    else  // Abort all other errors
	    {
              cpqfcTSPutLinkQue( cpqfcHBAdata, BLS_ABTS, &x_ID);
	    }

            // if the HPE bit is set, we have to CLose the LOOP
            // (see TL/TS UG, pg. 239)

            if( dwStatus &= HOSTPROG_ERR )
            // set CL bit (see TL/TS UG, pg. 172)
              writel( 4, fcChip->Registers.FMcontrol.address);
          }
        }
          // NOTE: we don't necessarily care about ALL completion messages...
                                      // SCSI resp. complete OR
        if( ((x_ID < TACH_SEST_LEN) && RPCset)|| 
             (x_ID >= TACH_SEST_LEN) )  // non-SCSI command
        {
              // exchange done; complete to upper levels with status
              // (if necessary) and free the exchange slot
            

          if( x_ID >= TACH_SEST_LEN ) // Link Service Outbound frame?
                                    // A Request or Reply has been sent
          {                         // signal waiting WorkerThread

            up( cpqfcHBAdata->TYOBcomplete);   // frame is OUT of Tach

                                    // WorkerThread will complete Xchng
          }
          else  // X_ID is for FCP assist (SEST)
          {
              // TBD (target mode)
//            fcCompleteExchange( fcChip, x_ID); // TRE completed
          }
        }
      }
      else  // ERROR CONDITION!  bogus x_ID in completion message
      {

        printk(" ProcessIMQ (OBCM) x_id out of range %Xh\n", x_ID);

      }



          // Load the Frame Manager's error counters.  We check them here
          // because presumably the link is up and healthy enough for the
          // counters to be meaningful (i.e., don't check them while loop
          // is initializing).
      fcChip->Registers.FMLinkStatus1.value =    // get TL's counter
        readl(fcChip->Registers.FMLinkStatus1.address);
                  
      fcChip->Registers.FMLinkStatus2.value =    // get TL's counter
        readl(fcChip->Registers.FMLinkStatus2.address);
            

      fcParseLinkStatusCounters( fcChip); // load into 6 s/w accumulators
    break;



    case ERROR_IDLE_COMPLETION:  // TachLite Error Idle...
    
    // We usually get this when the link goes down during heavy traffic.
    // For now, presume that if SEST Exchanges are open, we will
    // get this as our cue to INVALIDATE all SEST entries
    // (and we OWN all the SEST entries).
    // See TL/TS UG, pg. 53
    
      for( x_ID = 0; x_ID < TACH_SEST_LEN; x_ID++)
      {

        // Does this VALid SEST entry need to be invalidated for Abort?
        fcChip->SEST->u[ x_ID].IWE.Hdr_Len &= 0x7FFFFFFF; 
      }
      
      CpqTsUnFreezeTachlite( fcChip, 2); // unfreeze Tachyon, if Link OK

    break;


    case INBOUND_SFS_COMPLETION:  //0x04
          // NOTE! we must process this SFQ message to avoid SFQ filling
          // up and stopping TachLite.  Incoming commands are placed here,
          // as well as 'unknown' frames (e.g. LIP loop position data)
          // write this CM's producer index to global...
          // TL/TS UG, pg 234:
          // Type: 0 - reserved
          //       1 - Unassisted FCP
          //       2 - BAD FCP
          //       3 - Unkown Frame
          //       4-F reserved


      fcChip->SFQ->producerIndex = (USHORT)
        (fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[0] & 0x0fffL);


      ucInboundMessageType = 0;  // default to useless frame

        // we can only process two Types: 1, Unassisted FCP, and 3, Unknown
        // Also, we aren't interested in processing frame fragments
        // so don't Que anything with 'LKF' bit set
      if( !(fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[2] 
        & 0x40000000) )  // 'LKF' link failure bit clear?
      {
        ucInboundMessageType = (UCHAR)  // ICM DWord3, "Type"
        (fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[2] & 0x0fL);
      }
      else
      {
	fcChip->fcStats.linkFailRX++;
//        printk("LKF (link failure) bit set on inbound message\n");
      }

          // clears SFQ entry from Tachyon buffer; copies to contiguous ulBuff
      CpqTsGetSFQEntry(
        fcChip,                  // i.e. this Device Object
        (USHORT)fcChip->SFQ->producerIndex,  // SFQ producer ndx         
        ulFibreFrame, TRUE);    // contiguous destination buffer, update chip
                     
        // analyze the incoming frame outside the INT handler...
        // (i.e., Worker)

      if( ucInboundMessageType == 1 )
      {
        fchs = (TachFCHDR_GCMND*)ulFibreFrame; // cast to examine IB frame
        // don't fill up our Q with garbage - only accept FCP-CMND  
        // or XRDY frames
        if( (fchs->d_id & 0xFF000000) == 0x06000000 ) // CMND
        {
	  // someone sent us a SCSI command
	  
//          fcPutScsiQue( cpqfcHBAdata, 
//                        SFQ_UNASSISTED_FCP, ulFibreFrame); 
	}
	else if( ((fchs->d_id & 0xFF000000) == 0x07000000) || // RSP (status)
            (fchs->d_id & 0xFF000000) == 0x05000000 )  // XRDY  
	{
	  ULONG x_ID;
	  // Unfortunately, ABTS requires a Freeze on the chip so
	  // we can modify the shared memory SEST.  When frozen,
	  // any received Exchange frames cannot be processed by
	  // Tachyon, so they will be dumped in here.  It is too
	  // complex to attempt the reconstruct these frames in
	  // the correct Exchange context, so we simply seek to
	  // find status or transfer ready frames, and cause the
	  // exchange to complete with errors before the timeout
	  // expires.  We use a Linux Scsi Cmnd result code that
	  // causes immediate retry.
	  

	  // Do we have an open exchange that matches this s_id
	  // and ox_id?
	  for( x_ID = 0; x_ID < TACH_SEST_LEN; x_ID++)
	  {
            if( (fchs->s_id & 0xFFFFFF) == 
                 (Exchanges->fcExchange[x_ID].fchs.d_id & 0xFFFFFF) 
		       &&
                (fchs->ox_rx_id & 0xFFFF0000) == 
                 (Exchanges->fcExchange[x_ID].fchs.ox_rx_id & 0xFFFF0000) )
	    {
    //          printk(" #R/X frame x_ID %08X# ", fchs->ox_rx_id );
              // simulate the anticipated error - since the
	      // SEST was frozen, frames were lost...
              Exchanges->fcExchange[ x_ID ].status |= SFQ_FRAME;
              
	      // presumes device is still there: send ABTS.
              cpqfcTSPutLinkQue( cpqfcHBAdata, BLS_ABTS, &x_ID);
	      break;  // done
	    }
	  }
	}
	  
      }
          
      else if( ucInboundMessageType == 3)
      {
        // FC Link Service frames (e.g. PLOGI, ACC) come in here.  
        cpqfcTSPutLinkQue( cpqfcHBAdata, SFQ_UNKNOWN, ulFibreFrame); 
                          
      }

      else if( ucInboundMessageType == 2 ) // "bad FCP"?
      {
#ifdef IMQ_DEBUG
        printk("Bad FCP incoming frame discarded\n");
#endif
      }

      else // don't know this type
      {
#ifdef IMQ_DEBUG 
        printk("Incoming frame discarded, type: %Xh\n", ucInboundMessageType);
#endif
      }
        
        // Check the Frame Manager's error counters.  We check them here
        // because presumably the link is up and healthy enough for the
        // counters to be meaningful (i.e., don't check them while loop
        // is initializing).
      fcChip->Registers.FMLinkStatus1.value =    // get TL's counter
        readl(fcChip->Registers.FMLinkStatus1.address);
                  

      fcChip->Registers.FMLinkStatus2.value =    // get TL's counter
        readl(fcChip->Registers.FMLinkStatus2.address);
                

      break;




                    // We get this CM because we issued a freeze
                    // command to stop outbound frames.  We issue the
                    // freeze command at Link Up time; when this message
                    // is received, the ERQ base can be switched and PDISC
                    // frames can be sent.

      
    case ERQ_FROZEN_COMPLETION:  // note: expect ERQ followed immediately
                                 // by FCP when freezing TL
      fcChip->Registers.TYstatus.value =         // read what's frozen
        readl(fcChip->Registers.TYstatus.address);
      // (do nothing; wait for FCP frozen message)
      break;
    case FCP_FROZEN_COMPLETION:
      
      fcChip->Registers.TYstatus.value =         // read what's frozen
        readl(fcChip->Registers.TYstatus.address);
      
      // Signal the kernel thread to proceed with SEST modification
      up( cpqfcHBAdata->TachFrozen);

      break;



    case INBOUND_C1_TIMEOUT:
    case MFS_BUF_WARN:
    case IMQ_BUF_WARN:
    break;





        // In older Tachyons, we 'clear' the internal 'core' interrupt state
        // by reading the FMstatus register.  In newer TachLite (Tachyon),
        // we must WRITE the register
        // to clear the condition (TL/TS UG, pg 179)
    case FRAME_MGR_INTERRUPT:
    {
      PFC_LOGGEDIN_PORT pLoggedInPort; 

      fcChip->Registers.FMstatus.value = 
        readl( fcChip->Registers.FMstatus.address );
                
      // PROBLEM: It is possible, especially with "dumb" hubs that
      // don't automatically LIP on by-pass of ports that are going
      // away, for the hub by-pass process to destroy critical 
      // ordered sets of a frame.  The result of this is a hung LPSM
      // (Loop Port State Machine), which on Tachyon results in a
      // (default 2 sec) Loop State Timeout (LST) FM message.  We 
      // want to avoid this relatively huge timeout by detecting
      // likely scenarios which will result in LST.
      // To do this, we could examine FMstatus for Loss of Synchronization
      // and/or Elastic Store (ES) errors.  Of these, Elastic Store is better
      // because we get this indication more quickly than the LOS.
      // Not all ES errors are harmfull, so we don't want to LIP on every
      // ES.  Instead, on every ES, detect whether our LPSM in in one
      // of the LST states: ARBITRATING, OPEN, OPENED, XMITTED CLOSE,
      // or RECEIVED CLOSE.  (See TL/TS UG, pg. 181)
      // If any of these LPSM states are detected
      // in combination with the LIP while LDn is not set, 
      // send an FM init (LIP F7,F7 for loops)!
      // It is critical to the physical link stability NOT to reset (LIP)
      // more than absolutely necessary; this is a basic premise of the
      // SANMark level 1 spec.
      {
	ULONG Lpsm = (fcChip->Registers.FMstatus.value & 0xF0) >>4;
	
	if( (fcChip->Registers.FMstatus.value & 0x400)  // ElasticStore?
                      &&
            !(fcChip->Registers.FMstatus.value & 0x100) // NOT LDn
	              &&
            !(fcChip->Registers.FMstatus.value & 0x1000)) // NOT LF
	{
	  if( (Lpsm != 0) || // not MONITORING? or
	      !(Lpsm & 0x8) )// not already offline?
	  {
	  // now check the particular LST states...
            if( (Lpsm == ARBITRATING) || (Lpsm == OPEN) ||
	      (Lpsm == OPENED)      || (Lpsm == XMITTD_CLOSE) ||
	      (Lpsm == RCVD_CLOSE) )
	    {
	      // re-init the loop before it hangs itself!
              printk(" #req FMinit on E-S: LPSM %Xh# ",Lpsm);


	      fcChip->fcStats.FMinits++;
              writel( 6, fcChip->Registers.FMcontrol.address); // LIP
	    }
	  }
	}
	else if( fcChip->Registers.FMstatus.value & 0x40000 ) // LST?
	{
          printk(" #req FMinit on LST, LPSM %Xh# ",Lpsm);
	 
          fcChip->fcStats.FMinits++;
          writel( 6, fcChip->Registers.FMcontrol.address);  // LIP
	}  
      }


      // clear only the 'interrupting' type bits for this REG read
      writel( (fcChip->Registers.FMstatus.value & 0xff3fff00L),
        fcChip->Registers.FMstatus.address);
                          

               // copy frame manager status to unused ULONG slot
      fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[0] =
          fcChip->Registers.FMstatus.value; // (for debugging)


          // Load the Frame Manager's error counters.  We check them here
          // because presumably the link is up and healthy enough for the
          // counters to be meaningful (i.e., don't check them while loop
          // is initializing).
      fcChip->Registers.FMLinkStatus1.value =   // get TL's counter
        readl(fcChip->Registers.FMLinkStatus1.address);
            
      fcChip->Registers.FMLinkStatus2.value =   // get TL's counter
        readl(fcChip->Registers.FMLinkStatus2.address);
          
          // Get FM BB_Credit Zero Reg - does not clear on READ
      fcChip->Registers.FMBB_CreditZero.value =   // get TL's counter
        readl(fcChip->Registers.FMBB_CreditZero.address);
            


      fcParseLinkStatusCounters( fcChip); // load into 6 s/w accumulators


               // LINK DOWN

      if( fcChip->Registers.FMstatus.value & 0x100L ) // Link DOWN bit
      {                                 
	
#ifdef IMQ_DEBUG
        printk("LinkDn\n");
#endif
        printk(" #LDn# ");
        
        fcChip->fcStats.linkDown++;
        
	SetTachTOV( cpqfcHBAdata);  // must set according to SANMark

	// Check the ERQ - force it to be "empty" to prevent Tach
	// from sending out frames before we do logins.


  	if( fcChip->ERQ->producerIndex != fcChip->ERQ->consumerIndex)
	{
//	  printk("#ERQ PI != CI#");
          CpqTsFreezeTachlite( fcChip, 1); // freeze ERQ only	  
	  fcChip->ERQ->producerIndex = fcChip->ERQ->consumerIndex = 0;
 	  writel( fcChip->ERQ->base, 
	    (fcChip->Registers.ReMapMemBase + TL_MEM_ERQ_BASE));
          // re-writing base forces ERQ PI to equal CI
  
	}
		
	// link down transition occurred -- port_ids can change
        // on next LinkUp, so we must invalidate current logins
        // (and any I/O in progress) until PDISC or PLOGI/PRLI
        // completes
        {
          pLoggedInPort = &fcChip->fcPorts; 
          while( pLoggedInPort ) // for all ports which are expecting
                                 // PDISC after the next LIP, set the
                                 // logoutTimer
          {

	    if( pLoggedInPort->pdisc) // expecting PDISC within 2 sec?
            {
              pLoggedInPort->LOGO_timer = 3;  // we want 2 seconds
                                              // but Timer granularity
                                              // is 1 second
            }
                                // suspend any I/O in progress until
                                // PDISC received...
            pLoggedInPort->prli = FALSE;   // block FCP-SCSI commands
	    
            pLoggedInPort = pLoggedInPort->pNextPort;
          }  // ... all Previously known ports checked
        }
        
	// since any hot plugging device may NOT support LILP frames
	// (such as early Tachyon chips), clear this flag indicating
	// we shouldn't use (our copy of) a LILP map.
	// If we receive an LILP frame, we'll set it again.
	fcChip->Options.LILPin = 0; // our LILPmap is invalid
        cpqfcHBAdata->PortDiscDone = 0; // must re-validate FC ports!

          // also, we want to invalidate (i.e. INITIATOR_ABORT) any
          // open Login exchanges, in case the LinkDown happened in the
          // middle of logins.  It's possible that some ports already
          // ACCepted login commands which we have not processed before
          // another LinkDown occurred.  Any accepted Login exhanges are
          // invalidated by LinkDown, even before they are acknowledged.
          // It's also possible for a port to have a Queued Reply or Request
          // for login which was interrupted by LinkDown; it may come later,
          // but it will be unacceptable to us.

          // we must scan the entire exchange space, find every Login type
          // originated by us, and abort it. This is NOT an abort due to
          // timeout, so we don't actually send abort to the other port -
          // we just complete it to free up the fcExchange slot.

        for( i=TACH_SEST_LEN; i< TACH_MAX_XID; i++)
        {                     // looking for Extended Link Serv.Exchanges
          if( Exchanges->fcExchange[i].type == ELS_PDISC ||
              Exchanges->fcExchange[i].type == ELS_PLOGI ||
              Exchanges->fcExchange[i].type == ELS_PRLI ) 
          {
              // ABORT the exchange!
#ifdef IMQ_DEBUG
            printk("Originator ABORT x_id %Xh, type %Xh, port_id %Xh on LDn\n",
              i, Exchanges->fcExchange[i].type,
            Exchanges->fcExchange[i].fchs.d_id);
#endif

            Exchanges->fcExchange[i].status |= INITIATOR_ABORT;
            cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev, fcChip, i); // abort on LDn
          }
        }

      }

             // ################   LINK UP   ##################
      if( fcChip->Registers.FMstatus.value & 0x200L ) // Link Up bit
      {                                 // AL_PA could have changed

          // We need the following code, duplicated from LinkDn condition,
          // because it's possible for the Tachyon to re-initialize (hard
          // reset) without ever getting a LinkDn indication.
        pLoggedInPort = &fcChip->fcPorts; 
        while( pLoggedInPort )   // for all ports which are expecting
                                 // PDISC after the next LIP, set the
                                 // logoutTimer
        {
          if( pLoggedInPort->pdisc) // expecting PDISC within 2 sec?
          {
            pLoggedInPort->LOGO_timer = 3;  // we want 2 seconds
                                              // but Timer granularity
                                              // is 1 second
             
                                  // suspend any I/O in progress until
                                  // PDISC received...

          }
          pLoggedInPort = pLoggedInPort->pNextPort;
        }  // ... all Previously known ports checked
 
          // CpqTs acquired AL_PA in register AL_PA (ACQ_ALPA)
        fcChip->Registers.rcv_al_pa.value = 
          readl(fcChip->Registers.rcv_al_pa.address);
 
	// Now, if our acquired address is DIFFERENT from our
        // previous one, we are not allow to do PDISC - we
        // must go back to PLOGI, which will terminate I/O in
        // progress for ALL logged in FC devices...
	// (This is highly unlikely).

	if( (fcChip->Registers.my_al_pa & 0xFF) != 
	    ((fcChip->Registers.rcv_al_pa.value >> 16) &0xFF) )
	{

//	  printk(" #our HBA port_id changed!# "); // FC port_id changed!!	

	  pLoggedInPort = &fcChip->fcPorts; 
          while( pLoggedInPort ) // for all ports which are expecting
                                 // PDISC after the next LIP, set the
                                 // logoutTimer
          {
	    pLoggedInPort->pdisc  = FALSE;
            pLoggedInPort->prli = FALSE;
            pLoggedInPort = pLoggedInPort->pNextPort;
          }  // ... all Previously known ports checked

	  // when the port_id changes, we must terminate
	  // all open exchanges.
          cpqfcTSTerminateExchange( cpqfcHBAdata, NULL, PORTID_CHANGED);

	}
	               
	// Replace the entire 24-bit port_id.  We only know the
	// lower 8 bits (alpa) from Tachyon; if a FLOGI is done,
	// we'll get the upper 16-bits from the FLOGI ACC frame.
	// If someone plugs into Fabric switch, we'll do FLOGI and
	// get full 24-bit port_id; someone could then remove and
	// hot-plug us into a dumb hub.  If we send a 24-bit PLOGI
	// to a "private" loop device, it might blow up.
	// Consequently, we force the upper 16-bits of port_id to
	// be re-set on every LinkUp transition
        fcChip->Registers.my_al_pa =
          (fcChip->Registers.rcv_al_pa.value >> 16) & 0xFF;

              
              // copy frame manager status to unused ULONG slot
        fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[1] =
          fcChip->Registers.my_al_pa; // (for debugging)

              // for TachLite, we need to write the acquired al_pa
              // back into the FMconfig register, because after
              // first initialization, the AQ (prev. acq.) bit gets
              // set, causing TL FM to use the AL_PA field in FMconfig.
              // (In Tachyon, FM writes the acquired AL_PA for us.)
        ulBuff = readl( fcChip->Registers.FMconfig.address);
        ulBuff &= 0x00ffffffL;  // mask out current al_pa
        ulBuff |= ( fcChip->Registers.my_al_pa << 24 ); // or in acq. al_pa
        fcChip->Registers.FMconfig.value = ulBuff; // copy it back
        writel( fcChip->Registers.FMconfig.value,  // put in TachLite
          fcChip->Registers.FMconfig.address);
            

#ifdef IMQ_DEBUG
        printk("#LUp %Xh, FMstat 0x%08X#", 
		fcChip->Registers.my_al_pa, fcChip->Registers.FMstatus.value);
#endif

              // also set the WRITE-ONLY My_ID Register (for Fabric
              // initialization)
        writel( fcChip->Registers.my_al_pa,
          fcChip->Registers.ReMapMemBase +TL_MEM_TACH_My_ID);
          

        fcChip->fcStats.linkUp++;

                                     // reset TL statistics counters
                                     // (we ignore these error counters
                                     // while link is down)
        ulBuff =                     // just reset TL's counter
                 readl( fcChip->Registers.FMLinkStatus1.address);
          
        ulBuff =                     // just reset TL's counter
                 readl( fcChip->Registers.FMLinkStatus2.address);

          // for initiator, need to start verifying ports (e.g. PDISC)



         
      
      
	CpqTsUnFreezeTachlite( fcChip, 2); // unfreeze Tachlite, if Link OK
	
	// Tachyon creates an interesting problem for us on LILP frames.
	// Instead of writing the incoming LILP frame into the SFQ before
	// indicating LINK UP (the actual order of events), Tachyon tells
	// us LINK UP, and later us the LILP.  So we delay, then examine the
	// IMQ for an Inbound CM (x04); if found, we can set
	// LINKACTIVE after processing the LILP.  Otherwise, just proceed.
	// Since Tachyon imposes this time delay (and doesn't tell us
	// what it is), we have to impose a delay before "Peeking" the IMQ
	// for Tach hardware (DMA) delivery.
	// Processing LILP is required by SANMark
	udelay( 1000);  // microsec delay waiting for LILP (if it comes)
        if( PeekIMQEntry( fcChip, ELS_LILP_FRAME) )
	{  // found SFQ LILP, which will post LINKACTIVE	  
//	  printk("skipping LINKACTIVE post\n");

	}
	else
          cpqfcTSPutLinkQue( cpqfcHBAdata, LINKACTIVE, ulFibreFrame);  
      }



      // ******* Set Fabric Login indication ********
      if( fcChip->Registers.FMstatus.value & 0x2000 )
      {
	printk(" #Fabric# ");
        fcChip->Options.fabric = 1;
      }
      else
        fcChip->Options.fabric = 0;

      
      
                             // ******* LIP(F8,x) or BAD AL_PA? ********
      if( fcChip->Registers.FMstatus.value & 0x30000L )
      {
                        // copy the error AL_PAs
        fcChip->Registers.rcv_al_pa.value = 
          readl(fcChip->Registers.rcv_al_pa.address);
            
                        // Bad AL_PA?
        if( fcChip->Registers.FMstatus.value & 0x10000L )
        {
          PFC_LOGGEDIN_PORT pLoggedInPort;
        
                       // copy "BAD" al_pa field
          fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[1] =
              (fcChip->Registers.rcv_al_pa.value & 0xff00L) >> 8;

	  pLoggedInPort = fcFindLoggedInPort( fcChip,
            NULL,     // DON'T search Scsi Nexus
            fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[1], // port id
            NULL,     // DON'T search linked list for FC WWN
            NULL);    // DON'T care about end of list
 
	  if( pLoggedInPort )
	  {
            // Just in case we got this BAD_ALPA because a device
	    // quietly disappeared (can happen on non-managed hubs such 
	    // as the Vixel Rapport 1000),
	    // do an Implicit Logout.  We never expect this on a Logged
	    // in port (but do expect it on port discovery).
	    // (As a reasonable alternative, this could be changed to 
	    // simply start the implicit logout timer, giving the device
	    // several seconds to "come back".)
	    // 
	    printk(" #BAD alpa %Xh# ",
		   fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[1]);
            cpqfcTSImplicitLogout( cpqfcHBAdata, pLoggedInPort);
	  }
        }
                        // LIP(f8,x)?
        if( fcChip->Registers.FMstatus.value & 0x20000L )
        {
                        // for debugging, copy al_pa field
          fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[2] =
              (fcChip->Registers.rcv_al_pa.value & 0xffL);
                        // get the other port's al_pa
                        // (one that sent LIP(F8,?) )
        }
      }

                             // Elastic store err
      if( fcChip->Registers.FMstatus.value & 0x400L )
      {
            // don't count e-s if loop is down!
        if( !(USHORT)(fcChip->Registers.FMstatus.value & 0x80) )
          fcChip->fcStats.e_stores++;
          
      }
    }
    break;


    case INBOUND_FCP_XCHG_COMPLETION:  // 0x0C

    // Remarks:
    // On Tachlite TL/TS, we get this message when the data phase
    // of a SEST inbound transfer is complete.  For example, if a WRITE command
    // was received with OX_ID 0, we might respond with XFER_RDY with
    // RX_ID 8001.  This would start the SEST controlled data phases.  When
    // all data frames are received, we get this inbound completion. This means
    // we should send a status frame to complete the status phase of the 
    // FCP-SCSI exchange, using the same OX_ID,RX_ID that we used for data
    // frames.
    // See Outbound CM discussion of x_IDs
    // Psuedo Code
    //   Get SEST index (x_ID)
    //     x_ID out of range, return (err condition)
    //   set status bits from 2nd dword
    //   free transactionID & SEST entry
    //   call fcComplete with transactionID & status

      ulBuff = fcChip->IMQ->QEntry[fcChip->IMQ->consumerIndex].word[0];
      x_ID = ulBuff & 0x7fffL;  // lower 14 bits SEST_Index/Trans_ID
                                // (mask out MSB "direction" bit)
                                // Range check CM OX/RX_ID value...
      if( x_ID < TACH_SEST_LEN )  // don't go beyond SEST array space
      {

//#define FCP_COMPLETION_DBG 1
#ifdef FCP_COMPLETION_DBG
        printk(" FCP_CM x_ID %Xh, status %Xh, Cmnd %p\n", 
          x_ID, ulBuff, Exchanges->fcExchange[x_ID].Cmnd);
#endif
        if( ulBuff & 0x08000000L ) // RPC -Response Phase Complete - or -
                                   // time to send response frame?
          RPCset = 1;             // (SEST transaction)
        else
          RPCset = 0;
                // set the status for this Inbound SCSI transaction's ID
        dwStatus = 0L;
        if( ulBuff & 0x70000000L ) // any errs?
        {
          
          if( ulBuff & 0x40000000L )
            dwStatus |= LINKFAIL_RX;
          
	  if( ulBuff & 0x20000000L )
            dwStatus |= COUNT_ERROR;
          
          if( ulBuff & 0x10000000L )
            dwStatus |= OVERFLOW;
        }
      
	
	  // FCP transaction done - copy status
        Exchanges->fcExchange[ x_ID ].status = dwStatus;


        // Did the exchange get an FCP-RSP response frame?
        // (Note the little endian/big endian FC payload difference)

        if( RPCset )             // SEST transaction Response frame rec'd
        {
    	  // complete the command in our driver...
          cpqfcTSCompleteExchange( cpqfcHBAdata->PciDev,fcChip, x_ID);

        }  // end "RPCset"
	
        else  // ("target" logic)
        {
            // Tachlite says all data frames have been received - now it's time
            // to analyze data transfer (successful?), then send a response 
            // frame for this exchange

          ulFibreFrame[0] = x_ID; // copy for later reference

          // if this was a TWE, we have to send satus response
          if( Exchanges->fcExchange[ x_ID].type == SCSI_TWE )
	  {
//            fcPutScsiQue( cpqfcHBAdata, 
//                NEED_FCP_RSP, ulFibreFrame);  // (ulFibreFrame not used here)
	  }
        }
      }
      else  // ERROR CONDITION!  bogus x_ID in completion message
      {
        printk("IN FCP_XCHG: bad x_ID: %Xh\n", x_ID);
      }

    break;




    case INBOUND_SCSI_DATA_COMMAND:
    case BAD_SCSI_FRAME:
    case INB_SCSI_STATUS_COMPLETION:
    case BUFFER_PROCESSED_COMPLETION:
    break;
    }

					   // Tachyon is producing;
					   // we are consuming
    fcChip->IMQ->consumerIndex++;             // increment OUR consumerIndex
    if( fcChip->IMQ->consumerIndex >= IMQ_LEN)// check for rollover
      fcChip->IMQ->consumerIndex = 0L;        // reset it


    if( fcChip->IMQ->producerIndex == fcChip->IMQ->consumerIndex )
    {                           // all Messages are processed -
      iStatus = 0;              // no more messages to process

    }
    else
      iStatus = 1;              // more messages to process

    // update TachLite's ConsumerIndex... (clears INTA_L)
    // NOTE: according to TL/TS UG, the 
    // "host must return completion messages in sequential order".
    // Does this mean one at a time, in the order received?  We
    // presume so.

    writel( fcChip->IMQ->consumerIndex,
      (fcChip->Registers.ReMapMemBase + IMQ_CONSUMER_INDEX));
		    
#if IMQ_DEBUG
    printk("Process IMQ: writing consumer ndx %d\n ", 
      fcChip->IMQ->consumerIndex);
    printk("PI %X, CI %X\n", 
    fcChip->IMQ->producerIndex,fcChip->IMQ->consumerIndex );
#endif
  


  }
  else
  {
   // hmmm... why did we get interrupted/called with no message?
    iStatus = -1;               // nothing to process
#if IMQ_DEBUG
    printk("Process IMQ: no message PI %Xh  CI %Xh", 
      fcChip->IMQ->producerIndex,
      fcChip->IMQ->consumerIndex);
#endif
  }

  LEAVE("ProcessIMQEntry");
  
  return iStatus;
}





// This routine initializes Tachyon according to the following
// options (opcode1):
// 1 - RESTART Tachyon, simulate power on condition by shutting
//     down laser, resetting the hardware, de-allocating all buffers;
//     continue
// 2 - Config Tachyon / PCI registers;
//     continue
// 3 - Allocating memory and setting Tachyon queues (write Tachyon regs);
//     continue
// 4 - Config frame manager registers, initialize, turn on laser
//
// Returns:
//  -1 on fatal error
//   0 on success

int CpqTsInitializeTachLite( void *pHBA, int opcode1, int opcode2)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA*)pHBA;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  ULONG ulBuff;
  UCHAR bBuff;
  int iStatus=-1;  // assume failure

  ENTER("InitializeTachLite");

  // verify board's base address (sanity check)

  if( !fcChip->Registers.ReMapMemBase)                // NULL address for card?
    return -1;                         // FATAL error!



  switch( opcode1 )
  {
    case 1:       // restore hardware to power-on (hard) restart


      iStatus = fcChip->ResetTachyon( 
		  cpqfcHBAdata, opcode2); // laser off, reset hardware
				      // de-allocate aligned buffers


/* TBD      // reset FC link Q (producer and consumer = 0)
      fcLinkQReset(cpqfcHBAdata); 

*/

      if( iStatus )
        break;

    case 2:       // Config PCI/Tachyon registers
      // NOTE: For Tach TL/TS, bit 31 must be set to 1.  For TS chips, a read
      // of bit 31 indicates state of M66EN signal; if 1, chip may run at 
      // 33-66MHz  (see TL/TS UG, pg 159)

      ulBuff = 0x80000000;  // TachLite Configuration Register

      writel( ulBuff, fcChip->Registers.TYconfig.address);
//      ulBuff = 0x0147L;  // CpqTs PCI CFGCMD register
//      WritePCIConfiguration( fcChip->Backplane.bus,
//                           fcChip->Backplane.slot, TLCFGCMD, ulBuff, 4);
//      ulBuff = 0x0L;  // test!
//      ReadPCIConfiguration( fcChip->Backplane.bus,
//                           fcChip->Backplane.slot, TLCFGCMD, &ulBuff, 4);

      // read back for reference...
      fcChip->Registers.TYconfig.value = 
         readl( fcChip->Registers.TYconfig.address );

      // what is the PCI bus width?
      pci_read_config_byte( cpqfcHBAdata->PciDev,
                                0x43, // PCIMCTR offset
                                &bBuff);
      
      fcChip->Registers.PCIMCTR = bBuff;

      // set string identifying the chip on the circuit board

      fcChip->Registers.TYstatus.value =
        readl( fcChip->Registers.TYstatus.address);
      
      {
// Now that we are supporting multiple boards, we need to change
// this logic to check for PCI vendor/device IDs...
// for now, quick & dirty is simply checking Chip rev
	
	ULONG RevId = (fcChip->Registers.TYstatus.value &0x3E0)>>5;
	UCHAR Minor = (UCHAR)(RevId & 0x3);
	UCHAR Major = (UCHAR)((RevId & 0x1C) >>2);
  
	/* printk("  HBA Tachyon RevId %d.%d\n", Major, Minor); */
  	if( (Major == 1) && (Minor == 2) )
        {
	  sprintf( cpqfcHBAdata->fcChip.Name, STACHLITE66_TS12);

	}
	else if( (Major == 1) && (Minor == 3) )
        {
	  sprintf( cpqfcHBAdata->fcChip.Name, STACHLITE66_TS13);
	}
	else if( (Major == 2) && (Minor == 1) )
        {
	  sprintf( cpqfcHBAdata->fcChip.Name, SAGILENT_XL2_21);
	}
	else
	  sprintf( cpqfcHBAdata->fcChip.Name, STACHLITE_UNKNOWN);
      }



    case 3:       // allocate mem, set Tachyon Que registers
      iStatus = CpqTsCreateTachLiteQues( cpqfcHBAdata, opcode2);

      if( iStatus )
        break;

      // now that the Queues exist, Tach can DMA to them, so
      // we can begin processing INTs
      // INTEN register - enable INT (TachLite interrupt)
      writeb( 0x1F, fcChip->Registers.ReMapMemBase + IINTEN);

	// Fall through
    case 4:       // Config Fame Manager, Init Loop Command, laser on

                 // L_PORT or loopback
                 // depending on Options
      iStatus = CpqTsInitializeFrameManager( fcChip,0 );
      if( iStatus )
      {
           // failed to initialize Frame Manager
	      break;
      }

    default:
      break;
  }
  LEAVE("InitializeTachLite");
  
  return iStatus;
}




// Depending on the type of platform memory allocation (e.g. dynamic),
// it's probably best to free memory in opposite order as it was allocated.
// Order of allocation: see other function


int CpqTsDestroyTachLiteQues( void *pHBA, int opcode)
{
  CPQFCHBA *cpqfcHBAdata = (CPQFCHBA*)pHBA;
  PTACHYON fcChip = &cpqfcHBAdata->fcChip;
  USHORT i, iStatus=0;
  void* vPtr;  // mem Align manager sets this to the freed address on success
  unsigned long ulPtr;  // for 64-bit pointer cast (e.g. Alpa machine)
  FC_EXCHANGES *Exchanges = fcChip->Exchanges;
  PSGPAGES j, next;

  ENTER("DestroyTachLiteQues");

  if( fcChip->SEST )
  {
                // search out and free Pool for Extended S/G list pages

    for( i=0; i < TACH_SEST_LEN; i++)  // for each exchange
    {
      // It's possible that extended S/G pages were allocated, mapped, and
      // not cleared due to error conditions or O/S driver termination.
      // Make sure they're all gone.
      if (Exchanges->fcExchange[i].Cmnd != NULL) 
      	cpqfc_pci_unmap(cpqfcHBAdata->PciDev, Exchanges->fcExchange[i].Cmnd, 
			fcChip, i); // undo DMA mappings.

      for (j=fcChip->SEST->sgPages[i] ; j != NULL ; j = next) {
		next = j->next;
		kfree(j);
      }
      fcChip->SEST->sgPages[i] = NULL;
    }
    ulPtr = (unsigned long)fcChip->SEST;
    vPtr = fcMemManager( cpqfcHBAdata->PciDev, 
		    &cpqfcHBAdata->dynamic_mem[0],
		    0,0, (ULONG)ulPtr, NULL ); // 'free' mem
    fcChip->SEST = 0L;  // null invalid ptr
    if( !vPtr )
    {
      printk("SEST mem not freed\n");
      iStatus = -1;
    }
  }

  if( fcChip->SFQ )
  {

    ulPtr = (unsigned long)fcChip->SFQ;
    vPtr = fcMemManager( cpqfcHBAdata->PciDev, 
		    &cpqfcHBAdata->dynamic_mem[0],
		    0,0, (ULONG)ulPtr, NULL ); // 'free' mem
    fcChip->SFQ = 0L;  // null invalid ptr
    if( !vPtr )
    {
      printk("SFQ mem not freed\n");
      iStatus = -2;
    }
  }


  if( fcChip->IMQ )
  {
      // clear Indexes to show empty Queue
    fcChip->IMQ->producerIndex = 0;
    fcChip->IMQ->consumerIndex = 0;

    ulPtr = (unsigned long)fcChip->IMQ;
    vPtr = fcMemManager( cpqfcHBAdata->PciDev, &cpqfcHBAdata->dynamic_mem[0],
		    0,0, (ULONG)ulPtr, NULL ); // 'free' mem
    fcChip->IMQ = 0L;  // null invalid ptr
    if( !vPtr )
    {
      printk("IMQ mem not freed\n");
      iStatus = -3;
    }
  }

  if( fcChip->ERQ )         // release memory blocks used by the queues
  {
    ulPtr = (unsigned long)fcChip->ERQ;
    vPtr = fcMemManager( cpqfcHBAdata->PciDev, &cpqfcHBAdata->dynamic_mem[0],
		    0,0, (ULONG)ulPtr, NULL ); // 'free' mem
    fcChip->ERQ = 0L;  // null invalid ptr
    if( !vPtr )
    {
      printk("ERQ mem not freed\n");
      iStatus = -4;
    }
  }
    
  // free up the primary EXCHANGES struct and Link Q
  cpqfc_free_dma_consistent(cpqfcHBAdata);
  
  LEAVE("DestroyTachLiteQues");
  
  return iStatus;     // non-zero (failed) if any memory not freed
}





// The SFQ is an array with SFQ_LEN length, each element (QEntry)
// with eight 32-bit words.  TachLite places incoming FC frames (i.e.
// a valid FC frame with our AL_PA ) in contiguous SFQ entries
// and sends a completion message telling the host where the frame is
// in the que.
// This function copies the current (or oldest not-yet-processed) QEntry to
// a caller's contiguous buffer and updates the Tachyon chip's consumer index
//
// NOTE:
//   An FC frame may consume one or many SFQ entries.  We know the total
//   length from the completion message.  The caller passes a buffer large
//   enough for the complete message (max 2k).

static void CpqTsGetSFQEntry(
         PTACHYON fcChip,
         USHORT producerNdx,
         ULONG *ulDestPtr,            // contiguous destination buffer
	 BOOLEAN UpdateChip)
{
  ULONG total_bytes=0;
  ULONG consumerIndex = fcChip->SFQ->consumerIndex;
  
				// check passed copy of SFQ producer index -
				// is a new message waiting for us?
				// equal indexes means SFS is copied

  while( producerNdx != consumerIndex )
  {                             // need to process message
    total_bytes += 64;   // maintain count to prevent writing past buffer
                   // don't allow copies over Fibre Channel defined length!
    if( total_bytes <= 2048 )
    {
      memcpy( ulDestPtr, 
              &fcChip->SFQ->QEntry[consumerIndex],
              64 );  // each SFQ entry is 64 bytes
      ulDestPtr += 16;   // advance pointer to next 64 byte block
    }
		         // Tachyon is producing,
                         // and we are consuming

    if( ++consumerIndex >= SFQ_LEN)// check for rollover
      consumerIndex = 0L;        // reset it
  }

  // if specified, update the Tachlite chip ConsumerIndex...
  if( UpdateChip )
  {
    fcChip->SFQ->consumerIndex = consumerIndex;
    writel( fcChip->SFQ->consumerIndex,
      fcChip->Registers.SFQconsumerIndex.address);
  }
}



// TachLite routinely freezes it's core ques - Outbound FIFO, Inbound FIFO,
// and Exchange Request Queue (ERQ) on error recover - 
// (e.g. whenever a LIP occurs).  Here
// we routinely RESUME by clearing these bits, but only if the loop is up
// to avoid ERROR IDLE messages forever.

void CpqTsUnFreezeTachlite( void *pChip, int type )
{
  PTACHYON fcChip = (PTACHYON)pChip;
  fcChip->Registers.TYcontrol.value = 
    readl(fcChip->Registers.TYcontrol.address);
            
  // (bit 4 of value is GBIC LASER)
  // if we 'unfreeze' the core machines before the loop is healthy
  // (i.e. FLT, OS, LS failure bits set in FMstatus)
  // we can get 'error idle' messages forever.  Verify that
  // FMstatus (Link Status) is OK before unfreezing.

  if( !(fcChip->Registers.FMstatus.value & 0x07000000L) && // bits clear?
      !(fcChip->Registers.FMstatus.value & 0x80  ))  // Active LPSM?
  {
    fcChip->Registers.TYcontrol.value &=  ~0x300L; // clear FEQ, FFA
    if( type == 1 )  // unfreeze ERQ only
    {
//      printk("Unfreezing ERQ\n");
      fcChip->Registers.TYcontrol.value |= 0x10000L; // set REQ
    }
    else             // unfreeze both ERQ and FCP-ASSIST (SEST)
    {
//      printk("Unfreezing ERQ & FCP-ASSIST\n");

                     // set ROF, RIF, REQ - resume Outbound FCP, Inbnd FCP, ERQ
      fcChip->Registers.TYcontrol.value |= 0x70000L; // set ROF, RIF, REQ
    }

    writel( fcChip->Registers.TYcontrol.value,
      fcChip->Registers.TYcontrol.address);
              
  }
          // readback for verify (TachLite still frozen?)
  fcChip->Registers.TYstatus.value = 
    readl(fcChip->Registers.TYstatus.address);
}


// Whenever an FC Exchange Abort is required, we must manipulate the
// Host/Tachyon shared memory SEST table.  Before doing this, we
// must freeze Tachyon, which flushes certain buffers and ensure we
// can manipulate the SEST without contention.
// This freeze function will result in FCP & ERQ FROZEN completion
// messages (per argument "type").

void CpqTsFreezeTachlite( void *pChip, int type )
{
  PTACHYON fcChip = (PTACHYON)pChip;
  fcChip->Registers.TYcontrol.value = 
    readl(fcChip->Registers.TYcontrol.address);
    
                     //set FFA, FEQ - freezes SCSI assist and ERQ
  if( type == 1)    // freeze ERQ only
    fcChip->Registers.TYcontrol.value |= 0x100L; // (bit 4 is laser)
  else              // freeze both FCP assists (SEST) and ERQ
    fcChip->Registers.TYcontrol.value |= 0x300L; // (bit 4 is laser)
  
  writel( fcChip->Registers.TYcontrol.value,
    fcChip->Registers.TYcontrol.address);
              
}




// TL has two Frame Manager Link Status Registers, with three 8-bit
// fields each. These eight bit counters are cleared after each read,
// so we define six 32-bit accumulators for these TL counters. This
// function breaks out each 8-bit field and adds the value to the existing
// sum.  (s/w counters cleared independently)

void fcParseLinkStatusCounters(PTACHYON fcChip)
{
  UCHAR bBuff;
  ULONG ulBuff;


// The BB0 timer usually increments when TL is initialized, resulting
// in an initially bogus count.  If our own counter is ZERO, it means we
// are reading this thing for the first time, so we ignore the first count.
// Also, reading the register does not clear it, so we have to keep an
// additional static counter to detect rollover (yuk).

  if( fcChip->fcStats.lastBB0timer == 0L)  // TL was reset? (ignore 1st values)
  {
                           // get TL's register counter - the "last" count
    fcChip->fcStats.lastBB0timer = 
      fcChip->Registers.FMBB_CreditZero.value & 0x00ffffffL;
  }
  else  // subsequent pass - check for rollover
  {
                              // "this" count
    ulBuff = fcChip->Registers.FMBB_CreditZero.value & 0x00ffffffL;
    if( fcChip->fcStats.lastBB0timer > ulBuff ) // rollover happened
    {
                                // counter advanced to max...
      fcChip->fcStats.BB0_Timer += (0x00FFFFFFL - fcChip->fcStats.lastBB0timer);
      fcChip->fcStats.BB0_Timer += ulBuff;  // plus some more


    }
    else // no rollover -- more counts or no change
    {
      fcChip->fcStats.BB0_Timer +=  (ulBuff - fcChip->fcStats.lastBB0timer);

    }

    fcChip->fcStats.lastBB0timer = ulBuff;
  }



  bBuff = (UCHAR)(fcChip->Registers.FMLinkStatus1.value >> 24);
  fcChip->fcStats.LossofSignal += bBuff;

  bBuff = (UCHAR)(fcChip->Registers.FMLinkStatus1.value >> 16);
  fcChip->fcStats.BadRXChar += bBuff;

  bBuff = (UCHAR)(fcChip->Registers.FMLinkStatus1.value >> 8);
  fcChip->fcStats.LossofSync += bBuff;


  bBuff = (UCHAR)(fcChip->Registers.FMLinkStatus2.value >> 24);
  fcChip->fcStats.Rx_EOFa += bBuff;

  bBuff = (UCHAR)(fcChip->Registers.FMLinkStatus2.value >> 16);
  fcChip->fcStats.Dis_Frm += bBuff;

  bBuff = (UCHAR)(fcChip->Registers.FMLinkStatus2.value >> 8);
  fcChip->fcStats.Bad_CRC += bBuff;
}


void cpqfcTSClearLinkStatusCounters(PTACHYON fcChip)
{
  ENTER("ClearLinkStatusCounters");
  memset( &fcChip->fcStats, 0, sizeof( FCSTATS));
  LEAVE("ClearLinkStatusCounters");

}




// The following function reads the I2C hardware to get the adapter's
// World Wide Name (WWN).
// If the WWN is "500805f1fadb43e8" (as printed on the card), the
// Tachyon WWN_hi (32-bit) register is 500805f1, and WWN_lo register
// is fadb43e8.
// In the NVRAM, the bytes appear as:
// [2d] ..
// [2e] .. 
// [2f] 50
// [30] 08
// [31] 05
// [32] f1
// [33] fa
// [34] db
// [35] 43
// [36] e8
//
// In the Fibre Channel (Big Endian) format, the FC-AL LISM frame will
// be correctly loaded by Tachyon silicon.  In the login payload, bytes
// must be correctly swapped for Big Endian format.

int CpqTsReadWriteWWN( PVOID pChip, int Read)
{
  PTACHYON fcChip = (PTACHYON)pChip;
#define NVRAM_SIZE 512
  unsigned short i, count = NVRAM_SIZE;
  UCHAR nvRam[NVRAM_SIZE], WWNbuf[8];
  ULONG ulBuff;
  int iStatus=-1;  // assume failure
  int WWNoffset;

  ENTER("ReadWriteWWN");
  // Now try to read the WWN from the adapter's NVRAM

  if( Read )  // READing NVRAM WWN?
  {
    ulBuff = cpqfcTS_ReadNVRAM( fcChip->Registers.TYstatus.address,
                              fcChip->Registers.TYcontrol.address,
                              count, &nvRam[0] );

    if( ulBuff )   // NVRAM read successful?
    {
      iStatus = 0; // success!
      
                   // for engineering/ prototype boards, the data may be
                   // invalid (GIGO, usually all "FF"); this prevents the
                   // parse routine from working correctly, which means
                   // nothing will be written to our passed buffer.

      WWNoffset = cpqfcTS_GetNVRAM_data( WWNbuf, nvRam );

      if( !WWNoffset ) // uninitialized NVRAM -- copy bytes directly
      {
        printk( "CAUTION: Copying NVRAM data on fcChip\n");
        for( i= 0; i < 8; i++)
          WWNbuf[i] = nvRam[i +0x2f]; // dangerous! some formats won't work
      }
      
      fcChip->Registers.wwn_hi = 0L;
      fcChip->Registers.wwn_lo = 0L;
      for( i=0; i<4; i++)  // WWN bytes are big endian in NVRAM
      {
        ulBuff = 0L;
        ulBuff = (ULONG)(WWNbuf[i]) << (8 * (3-i));
        fcChip->Registers.wwn_hi |= ulBuff;
      }
      for( i=0; i<4; i++)  // WWN bytes are big endian in NVRAM
      {
        ulBuff = 0L;
        ulBuff = (ULONG)(WWNbuf[i+4]) << (8 * (3-i));
        fcChip->Registers.wwn_lo |= ulBuff;
      }
    }  // done reading
    else
    {

      printk( "cpqfcTS: NVRAM read failed\n");

    }
  }

  else  // WRITE
  {

    // NOTE: WRITE not supported & not used in released driver.

   
    printk("ReadWriteNRAM: can't write NVRAM; aborting write\n");
  }
  
  LEAVE("ReadWriteWWN");
  return iStatus;
}





// The following function reads or writes the entire "NVRAM" contents of 
// the I2C hardware (i.e. the NM24C03).  Note that HP's 5121A (TS 66Mhz)
// adapter does not use the NM24C03 chip, so this function only works on
// Compaq's adapters.

int CpqTsReadWriteNVRAM( PVOID pChip, PVOID buf, int Read)
{
  PTACHYON fcChip = (PTACHYON)pChip;
#define NVRAM_SIZE 512
  ULONG ulBuff;
  UCHAR *ucPtr = buf; // cast caller's void ptr to UCHAR array
  int iStatus=-1;  // assume failure

     
  if( Read )  // READing NVRAM?
  {
    ulBuff = cpqfcTS_ReadNVRAM(   // TRUE on success
                fcChip->Registers.TYstatus.address,
                fcChip->Registers.TYcontrol.address,
                256,            // bytes to write
                ucPtr );        // source ptr


    if( ulBuff )
      iStatus = 0; // success
    else
    {
#ifdef DBG
      printk( "CAUTION: NVRAM read failed\n");
#endif
    }
  }  // done reading

  else  // WRITING NVRAM 
  {

    printk("cpqfcTS: WRITE of FC Controller's NVRAM disabled\n");
  }
    
  return iStatus;
}
