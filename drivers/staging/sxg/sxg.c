/**************************************************************************
 *
 * Copyright (C) 2000-2008 Alacritech, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: sxg.c
 *
 * The SXG driver for Alacritech's 10Gbe products.
 *
 * NOTE: This is the standard, non-accelerated version of Alacritech's
 *       IS-NIC driver.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/mii.h>

#define SLIC_DUMP_ENABLED		0
#define SLIC_GET_STATS_ENABLED		0
#define LINUX_FREES_ADAPTER_RESOURCES	1
#define SXG_OFFLOAD_IP_CHECKSUM		0
#define SXG_POWER_MANAGEMENT_ENABLED	0
#define VPCI				0
#define DBG				1
#define ATK_DEBUG			1

#include "sxg_os.h"
#include "sxghw.h"
#include "sxghif.h"
#include "sxg.h"
#include "sxgdbg.h"

#include "sxgphycode.h"
#include "saharadbgdownload.h"

static int sxg_allocate_buffer_memory(p_adapter_t adapter, u32 Size, SXG_BUFFER_TYPE BufferType);
static void sxg_allocate_rcvblock_complete(p_adapter_t adapter, void * RcvBlock, dma_addr_t PhysicalAddress, u32 Length);
static void sxg_allocate_sgl_buffer_complete(p_adapter_t adapter, PSXG_SCATTER_GATHER SxgSgl, dma_addr_t PhysicalAddress, u32 Length);

static void sxg_mcast_init_crc32(void);

static int sxg_entry_open(p_net_device dev);
static int sxg_entry_halt(p_net_device dev);
static int sxg_ioctl(p_net_device dev, struct ifreq *rq, int cmd);
static int sxg_send_packets(struct sk_buff *skb, p_net_device dev);
static int sxg_transmit_packet(p_adapter_t adapter, struct sk_buff *skb);
static void sxg_dumb_sgl(PSCATTER_GATHER_LIST pSgl, PSXG_SCATTER_GATHER SxgSgl);

static void sxg_handle_interrupt(p_adapter_t adapter);
static int sxg_process_isr(p_adapter_t adapter, u32 MessageId);
static u32 sxg_process_event_queue(p_adapter_t adapter, u32 RssId);
static void sxg_complete_slow_send(p_adapter_t adapter);
static struct sk_buff *sxg_slow_receive(p_adapter_t adapter, PSXG_EVENT Event);
static void sxg_process_rcv_error(p_adapter_t adapter, u32 ErrorStatus);
static bool sxg_mac_filter(p_adapter_t adapter,
		       p_ether_header EtherHdr, ushort length);

#if SLIC_GET_STATS_ENABLED
static struct net_device_stats *sxg_get_stats(p_net_device dev);
#endif

static int sxg_mac_set_address(p_net_device dev, void * ptr);

static void sxg_adapter_set_hwaddr(p_adapter_t adapter);

static void sxg_unmap_mmio_space(p_adapter_t adapter);
static void sxg_mcast_set_mask(p_adapter_t adapter);

static int sxg_initialize_adapter(p_adapter_t adapter);
static void sxg_stock_rcv_buffers(p_adapter_t adapter);
static void sxg_complete_descriptor_blocks(p_adapter_t adapter, unsigned char Index);
static int sxg_initialize_link(p_adapter_t adapter);
static int sxg_phy_init(p_adapter_t adapter);
static void sxg_link_event(p_adapter_t adapter);
static SXG_LINK_STATE sxg_get_link_state(p_adapter_t adapter);
static void sxg_link_state(p_adapter_t adapter, SXG_LINK_STATE LinkState);
static int sxg_write_mdio_reg(p_adapter_t adapter,
		       u32 DevAddr, u32 RegAddr, u32 Value);
static int sxg_read_mdio_reg(p_adapter_t adapter,
		      u32 DevAddr, u32 RegAddr, u32 * pValue);
static void sxg_mcast_set_list(p_net_device dev);



#define XXXTODO 0

static unsigned int sxg_first_init = 1;
static char *sxg_banner =
    "Alacritech SLIC Technology(tm) Server and Storage 10Gbe Accelerator (Non-Accelerated)\n";

static int sxg_debug = 1;
static int debug = -1;
static p_net_device head_netdevice = NULL;

static sxgbase_driver_t sxg_global = {
	.dynamic_intagg = 1,
};
static int intagg_delay = 100;
static u32 dynamic_intagg = 0;

#define DRV_NAME	"sxg"
#define DRV_VERSION	"1.0.1"
#define DRV_AUTHOR	"Alacritech, Inc. Engineering"
#define DRV_DESCRIPTION	"Alacritech SLIC Techonology(tm) Non-Accelerated 10Gbe Driver"
#define DRV_COPYRIGHT	"Copyright 2000-2008 Alacritech, Inc.  All rights reserved."

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("GPL");

module_param(dynamic_intagg, int, 0);
MODULE_PARM_DESC(dynamic_intagg, "Dynamic Interrupt Aggregation Setting");
module_param(intagg_delay, int, 0);
MODULE_PARM_DESC(intagg_delay, "uSec Interrupt Aggregation Delay");

static struct pci_device_id sxg_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(SXG_VENDOR_ID, SXG_DEVICE_ID)},
	{0,}
};
MODULE_DEVICE_TABLE(pci, sxg_pci_tbl);

/***********************************************************************
************************************************************************
************************************************************************
************************************************************************
************************************************************************/

static inline void sxg_reg32_write(void __iomem *reg, u32 value, bool flush)
{
	writel(value, reg);
	if (flush)
		mb();
}

static inline void sxg_reg64_write(p_adapter_t adapter, void __iomem *reg,
				   u64 value, u32 cpu)
{
	u32 value_high = (u32) (value >> 32);
	u32 value_low = (u32) (value & 0x00000000FFFFFFFF);
	unsigned long flags;

	spin_lock_irqsave(&adapter->Bit64RegLock, flags);
	writel(value_high, (void __iomem *)(&adapter->UcodeRegs[cpu].Upper));
	writel(value_low, reg);
	spin_unlock_irqrestore(&adapter->Bit64RegLock, flags);
}

static void sxg_init_driver(void)
{
	if (sxg_first_init) {
		DBG_ERROR("sxg: %s sxg_first_init set jiffies[%lx]\n",
			  __FUNCTION__, jiffies);
		sxg_first_init = 0;
		spin_lock_init(&sxg_global.driver_lock);
	}
}

static void sxg_dbg_macaddrs(p_adapter_t adapter)
{
	DBG_ERROR("  (%s) curr %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		  adapter->netdev->name, adapter->currmacaddr[0],
		  adapter->currmacaddr[1], adapter->currmacaddr[2],
		  adapter->currmacaddr[3], adapter->currmacaddr[4],
		  adapter->currmacaddr[5]);
	DBG_ERROR("  (%s) mac  %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		  adapter->netdev->name, adapter->macaddr[0],
		  adapter->macaddr[1], adapter->macaddr[2],
		  adapter->macaddr[3], adapter->macaddr[4],
		  adapter->macaddr[5]);
	return;
}

// SXG Globals
static SXG_DRIVER SxgDriver;

#ifdef  ATKDBG
static sxg_trace_buffer_t LSxgTraceBuffer;
#endif /* ATKDBG */
static sxg_trace_buffer_t *SxgTraceBuffer = NULL;

/*
 * sxg_download_microcode
 *
 * Download Microcode to Sahara adapter
 *
 * Arguments -
 *		adapter		- A pointer to our adapter structure
 *		UcodeSel	- microcode file selection
 *
 * Return
 *	int
 */
static bool sxg_download_microcode(p_adapter_t adapter, SXG_UCODE_SEL UcodeSel)
{
	PSXG_HW_REGS HwRegs = adapter->HwRegs;
	u32 Section;
	u32 ThisSectionSize;
	u32 * Instruction = NULL;
	u32 BaseAddress, AddressOffset, Address;
//      u32                         Failure;
	u32 ValueRead;
	u32 i;
	u32 numSections = 0;
	u32 sectionSize[16];
	u32 sectionStart[16];

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DnldUcod",
		  adapter, 0, 0, 0);
	DBG_ERROR("sxg: %s ENTER\n", __FUNCTION__);

	switch (UcodeSel) {
	case SXG_UCODE_SAHARA:	// Sahara operational ucode
		numSections = SNumSections;
		for (i = 0; i < numSections; i++) {
			sectionSize[i] = SSectionSize[i];
			sectionStart[i] = SSectionStart[i];
		}
		break;
	default:
		printk(KERN_ERR KBUILD_MODNAME
		       ": Woah, big error with the microcode!\n");
		break;
	}

	DBG_ERROR("sxg: RESET THE CARD\n");
	// First, reset the card
	WRITE_REG(HwRegs->Reset, 0xDEAD, FLUSH);

	// Download each section of the microcode as specified in
	// its download file.  The *download.c file is generated using
	// the saharaobjtoc facility which converts the metastep .obj
	// file to a .c file which contains a two dimentional array.
	for (Section = 0; Section < numSections; Section++) {
		DBG_ERROR("sxg: SECTION # %d\n", Section);
		switch (UcodeSel) {
		case SXG_UCODE_SAHARA:
			Instruction = (u32 *) & SaharaUCode[Section][0];
			break;
		default:
			ASSERT(0);
			break;
		}
		BaseAddress = sectionStart[Section];
		ThisSectionSize = sectionSize[Section] / 12;	// Size in instructions
		for (AddressOffset = 0; AddressOffset < ThisSectionSize;
		     AddressOffset++) {
			Address = BaseAddress + AddressOffset;
			ASSERT((Address & ~MICROCODE_ADDRESS_MASK) == 0);
			// Write instruction bits 31 - 0
			WRITE_REG(HwRegs->UcodeDataLow, *Instruction, FLUSH);
			// Write instruction bits 63-32
			WRITE_REG(HwRegs->UcodeDataMiddle, *(Instruction + 1),
				  FLUSH);
			// Write instruction bits 95-64
			WRITE_REG(HwRegs->UcodeDataHigh, *(Instruction + 2),
				  FLUSH);
			// Write instruction address with the WRITE bit set
			WRITE_REG(HwRegs->UcodeAddr,
				  (Address | MICROCODE_ADDRESS_WRITE), FLUSH);
			// Sahara bug in the ucode download logic - the write to DataLow
			// for the next instruction could get corrupted.  To avoid this,
			// write to DataLow again for this instruction (which may get
			// corrupted, but it doesn't matter), then increment the address
			// and write the data for the next instruction to DataLow.  That
			// write should succeed.
			WRITE_REG(HwRegs->UcodeDataLow, *Instruction, TRUE);
			// Advance 3 u32S to start of next instruction
			Instruction += 3;
		}
	}
	// Now repeat the entire operation reading the instruction back and
	// checking for parity errors
	for (Section = 0; Section < numSections; Section++) {
		DBG_ERROR("sxg: check SECTION # %d\n", Section);
		switch (UcodeSel) {
		case SXG_UCODE_SAHARA:
			Instruction = (u32 *) & SaharaUCode[Section][0];
			break;
		default:
			ASSERT(0);
			break;
		}
		BaseAddress = sectionStart[Section];
		ThisSectionSize = sectionSize[Section] / 12;	// Size in instructions
		for (AddressOffset = 0; AddressOffset < ThisSectionSize;
		     AddressOffset++) {
			Address = BaseAddress + AddressOffset;
			// Write the address with the READ bit set
			WRITE_REG(HwRegs->UcodeAddr,
				  (Address | MICROCODE_ADDRESS_READ), FLUSH);
			// Read it back and check parity bit.
			READ_REG(HwRegs->UcodeAddr, ValueRead);
			if (ValueRead & MICROCODE_ADDRESS_PARITY) {
				DBG_ERROR("sxg: %s PARITY ERROR\n",
					  __FUNCTION__);

				return (FALSE);	// Parity error
			}
			ASSERT((ValueRead & MICROCODE_ADDRESS_MASK) == Address);
			// Read the instruction back and compare
			READ_REG(HwRegs->UcodeDataLow, ValueRead);
			if (ValueRead != *Instruction) {
				DBG_ERROR("sxg: %s MISCOMPARE LOW\n",
					  __FUNCTION__);
				return (FALSE);	// Miscompare
			}
			READ_REG(HwRegs->UcodeDataMiddle, ValueRead);
			if (ValueRead != *(Instruction + 1)) {
				DBG_ERROR("sxg: %s MISCOMPARE MIDDLE\n",
					  __FUNCTION__);
				return (FALSE);	// Miscompare
			}
			READ_REG(HwRegs->UcodeDataHigh, ValueRead);
			if (ValueRead != *(Instruction + 2)) {
				DBG_ERROR("sxg: %s MISCOMPARE HIGH\n",
					  __FUNCTION__);
				return (FALSE);	// Miscompare
			}
			// Advance 3 u32S to start of next instruction
			Instruction += 3;
		}
	}

	// Everything OK, Go.
	WRITE_REG(HwRegs->UcodeAddr, MICROCODE_ADDRESS_GO, FLUSH);

	// Poll the CardUp register to wait for microcode to initialize
	// Give up after 10,000 attemps (500ms).
	for (i = 0; i < 10000; i++) {
		udelay(50);
		READ_REG(adapter->UcodeRegs[0].CardUp, ValueRead);
		if (ValueRead == 0xCAFE) {
			DBG_ERROR("sxg: %s BOO YA 0xCAFE\n", __FUNCTION__);
			break;
		}
	}
	if (i == 10000) {
		DBG_ERROR("sxg: %s TIMEOUT\n", __FUNCTION__);

		return (FALSE);	// Timeout
	}
	// Now write the LoadSync register.  This is used to
	// synchronize with the card so it can scribble on the memory
	// that contained 0xCAFE from the "CardUp" step above
	if (UcodeSel == SXG_UCODE_SAHARA) {
		WRITE_REG(adapter->UcodeRegs[0].LoadSync, 0, FLUSH);
	}

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XDnldUcd",
		  adapter, 0, 0, 0);
	DBG_ERROR("sxg: %s EXIT\n", __FUNCTION__);

	return (TRUE);
}

/*
 * sxg_allocate_resources - Allocate memory and locks
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	int
 */
static int sxg_allocate_resources(p_adapter_t adapter)
{
	int status;
	u32 i;
	u32 RssIds, IsrCount;
//      PSXG_XMT_RING                                   XmtRing;
//      PSXG_RCV_RING                                   RcvRing;

	DBG_ERROR("%s ENTER\n", __FUNCTION__);

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "AllocRes",
		  adapter, 0, 0, 0);

	// Windows tells us how many CPUs it plans to use for
	// RSS
	RssIds = SXG_RSS_CPU_COUNT(adapter);
	IsrCount = adapter->MsiEnabled ? RssIds : 1;

	DBG_ERROR("%s Setup the spinlocks\n", __FUNCTION__);

	// Allocate spinlocks and initialize listheads first.
	spin_lock_init(&adapter->RcvQLock);
	spin_lock_init(&adapter->SglQLock);
	spin_lock_init(&adapter->XmtZeroLock);
	spin_lock_init(&adapter->Bit64RegLock);
	spin_lock_init(&adapter->AdapterLock);

	DBG_ERROR("%s Setup the lists\n", __FUNCTION__);

	InitializeListHead(&adapter->FreeRcvBuffers);
	InitializeListHead(&adapter->FreeRcvBlocks);
	InitializeListHead(&adapter->AllRcvBlocks);
	InitializeListHead(&adapter->FreeSglBuffers);
	InitializeListHead(&adapter->AllSglBuffers);

	// Mark these basic allocations done.  This flags essentially
	// tells the SxgFreeResources routine that it can grab spinlocks
	// and reference listheads.
	adapter->BasicAllocations = TRUE;
	// Main allocation loop.  Start with the maximum supported by
	// the microcode and back off if memory allocation
	// fails.  If we hit a minimum, fail.

	for (;;) {
		DBG_ERROR("%s Allocate XmtRings size[%x]\n", __FUNCTION__,
			  (sizeof(SXG_XMT_RING) * 1));

		// Start with big items first - receive and transmit rings.  At the moment
		// I'm going to keep the ring size fixed and adjust the number of
		// TCBs if we fail.  Later we might consider reducing the ring size as well..
		adapter->XmtRings = pci_alloc_consistent(adapter->pcidev,
							 sizeof(SXG_XMT_RING) *
							 1,
							 &adapter->PXmtRings);
		DBG_ERROR("%s XmtRings[%p]\n", __FUNCTION__, adapter->XmtRings);

		if (!adapter->XmtRings) {
			goto per_tcb_allocation_failed;
		}
		memset(adapter->XmtRings, 0, sizeof(SXG_XMT_RING) * 1);

		DBG_ERROR("%s Allocate RcvRings size[%x]\n", __FUNCTION__,
			  (sizeof(SXG_RCV_RING) * 1));
		adapter->RcvRings =
		    pci_alloc_consistent(adapter->pcidev,
					 sizeof(SXG_RCV_RING) * 1,
					 &adapter->PRcvRings);
		DBG_ERROR("%s RcvRings[%p]\n", __FUNCTION__, adapter->RcvRings);
		if (!adapter->RcvRings) {
			goto per_tcb_allocation_failed;
		}
		memset(adapter->RcvRings, 0, sizeof(SXG_RCV_RING) * 1);
		break;

	      per_tcb_allocation_failed:
		// an allocation failed.  Free any successful allocations.
		if (adapter->XmtRings) {
			pci_free_consistent(adapter->pcidev,
					    sizeof(SXG_XMT_RING) * 4096,
					    adapter->XmtRings,
					    adapter->PXmtRings);
			adapter->XmtRings = NULL;
		}
		if (adapter->RcvRings) {
			pci_free_consistent(adapter->pcidev,
					    sizeof(SXG_RCV_RING) * 4096,
					    adapter->RcvRings,
					    adapter->PRcvRings);
			adapter->RcvRings = NULL;
		}
		// Loop around and try again....
	}

	DBG_ERROR("%s Initialize RCV ZERO and XMT ZERO rings\n", __FUNCTION__);
	// Initialize rcv zero and xmt zero rings
	SXG_INITIALIZE_RING(adapter->RcvRingZeroInfo, SXG_RCV_RING_SIZE);
	SXG_INITIALIZE_RING(adapter->XmtRingZeroInfo, SXG_XMT_RING_SIZE);

	// Sanity check receive data structure format
	ASSERT((adapter->ReceiveBufferSize == SXG_RCV_DATA_BUFFER_SIZE) ||
	       (adapter->ReceiveBufferSize == SXG_RCV_JUMBO_BUFFER_SIZE));
	ASSERT(sizeof(SXG_RCV_DESCRIPTOR_BLOCK) ==
	       SXG_RCV_DESCRIPTOR_BLOCK_SIZE);

	// Allocate receive data buffers.  We allocate a block of buffers and
	// a corresponding descriptor block at once.  See sxghw.h:SXG_RCV_BLOCK
	for (i = 0; i < SXG_INITIAL_RCV_DATA_BUFFERS;
	     i += SXG_RCV_DESCRIPTORS_PER_BLOCK) {
		sxg_allocate_buffer_memory(adapter,
					   SXG_RCV_BLOCK_SIZE(adapter->
							      ReceiveBufferSize),
					   SXG_BUFFER_TYPE_RCV);
	}
	// NBL resource allocation can fail in the 'AllocateComplete' routine, which
	// doesn't return status.  Make sure we got the number of buffers we requested
	if (adapter->FreeRcvBufferCount < SXG_INITIAL_RCV_DATA_BUFFERS) {
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAResF6",
			  adapter, adapter->FreeRcvBufferCount, SXG_MAX_ENTRIES,
			  0);
		return (STATUS_RESOURCES);
	}

	DBG_ERROR("%s Allocate EventRings size[%x]\n", __FUNCTION__,
		  (sizeof(SXG_EVENT_RING) * RssIds));

	// Allocate event queues.
	adapter->EventRings = pci_alloc_consistent(adapter->pcidev,
						   sizeof(SXG_EVENT_RING) *
						   RssIds,
						   &adapter->PEventRings);

	if (!adapter->EventRings) {
		// Caller will call SxgFreeAdapter to clean up above allocations
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAResF8",
			  adapter, SXG_MAX_ENTRIES, 0, 0);
		status = STATUS_RESOURCES;
		goto per_tcb_allocation_failed;
	}
	memset(adapter->EventRings, 0, sizeof(SXG_EVENT_RING) * RssIds);

	DBG_ERROR("%s Allocate ISR size[%x]\n", __FUNCTION__, IsrCount);
	// Allocate ISR
	adapter->Isr = pci_alloc_consistent(adapter->pcidev,
					    IsrCount, &adapter->PIsr);
	if (!adapter->Isr) {
		// Caller will call SxgFreeAdapter to clean up above allocations
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAResF9",
			  adapter, SXG_MAX_ENTRIES, 0, 0);
		status = STATUS_RESOURCES;
		goto per_tcb_allocation_failed;
	}
	memset(adapter->Isr, 0, sizeof(u32) * IsrCount);

	DBG_ERROR("%s Allocate shared XMT ring zero index location size[%x]\n",
		  __FUNCTION__, sizeof(u32));

	// Allocate shared XMT ring zero index location
	adapter->XmtRingZeroIndex = pci_alloc_consistent(adapter->pcidev,
							 sizeof(u32),
							 &adapter->
							 PXmtRingZeroIndex);
	if (!adapter->XmtRingZeroIndex) {
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAResF10",
			  adapter, SXG_MAX_ENTRIES, 0, 0);
		status = STATUS_RESOURCES;
		goto per_tcb_allocation_failed;
	}
	memset(adapter->XmtRingZeroIndex, 0, sizeof(u32));

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAlcResS",
		  adapter, SXG_MAX_ENTRIES, 0, 0);

	DBG_ERROR("%s EXIT\n", __FUNCTION__);
	return (STATUS_SUCCESS);
}

/*
 * sxg_config_pci -
 *
 * Set up PCI Configuration space
 *
 * Arguments -
 *		pcidev			- A pointer to our adapter structure
 *
 */
static void sxg_config_pci(struct pci_dev *pcidev)
{
	u16 pci_command;
	u16 new_command;

	pci_read_config_word(pcidev, PCI_COMMAND, &pci_command);
	DBG_ERROR("sxg: %s  PCI command[%4.4x]\n", __FUNCTION__, pci_command);
	// Set the command register
	new_command = pci_command | (PCI_COMMAND_MEMORY |	// Memory Space Enable
				     PCI_COMMAND_MASTER |	// Bus master enable
				     PCI_COMMAND_INVALIDATE |	// Memory write and invalidate
				     PCI_COMMAND_PARITY |	// Parity error response
				     PCI_COMMAND_SERR |		// System ERR
				     PCI_COMMAND_FAST_BACK);	// Fast back-to-back
	if (pci_command != new_command) {
		DBG_ERROR("%s -- Updating PCI COMMAND register %4.4x->%4.4x.\n",
			  __FUNCTION__, pci_command, new_command);
		pci_write_config_word(pcidev, PCI_COMMAND, new_command);
	}
}

static int sxg_entry_probe(struct pci_dev *pcidev,
			   const struct pci_device_id *pci_tbl_entry)
{
	static int did_version = 0;
	int err;
	struct net_device *netdev;
	p_adapter_t adapter;
	void __iomem *memmapped_ioaddr;
	u32 status = 0;
	ulong mmio_start = 0;
	ulong mmio_len = 0;

	DBG_ERROR("sxg: %s 2.6 VERSION ENTER jiffies[%lx] cpu %d\n",
		  __FUNCTION__, jiffies, smp_processor_id());

	// Initialize trace buffer
#ifdef ATKDBG
	SxgTraceBuffer = &LSxgTraceBuffer;
	SXG_TRACE_INIT(SxgTraceBuffer, TRACE_NOISY);
#endif

	sxg_global.dynamic_intagg = dynamic_intagg;

	err = pci_enable_device(pcidev);

	DBG_ERROR("Call pci_enable_device(%p)  status[%x]\n", pcidev, err);
	if (err) {
		return err;
	}

	if (sxg_debug > 0 && did_version++ == 0) {
		printk(KERN_INFO "%s\n", sxg_banner);
		printk(KERN_INFO "%s\n", DRV_VERSION);
	}

	if (!(err = pci_set_dma_mask(pcidev, DMA_64BIT_MASK))) {
		DBG_ERROR("pci_set_dma_mask(DMA_64BIT_MASK) successful\n");
	} else {
		if ((err = pci_set_dma_mask(pcidev, DMA_32BIT_MASK))) {
			DBG_ERROR
			    ("No usable DMA configuration, aborting  err[%x]\n",
			     err);
			return err;
		}
		DBG_ERROR("pci_set_dma_mask(DMA_32BIT_MASK) successful\n");
	}

	DBG_ERROR("Call pci_request_regions\n");

	err = pci_request_regions(pcidev, DRV_NAME);
	if (err) {
		DBG_ERROR("pci_request_regions FAILED err[%x]\n", err);
		return err;
	}

	DBG_ERROR("call pci_set_master\n");
	pci_set_master(pcidev);

	DBG_ERROR("call alloc_etherdev\n");
	netdev = alloc_etherdev(sizeof(adapter_t));
	if (!netdev) {
		err = -ENOMEM;
		goto err_out_exit_sxg_probe;
	}
	DBG_ERROR("alloc_etherdev for slic netdev[%p]\n", netdev);

	SET_NETDEV_DEV(netdev, &pcidev->dev);

	pci_set_drvdata(pcidev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pcidev = pcidev;

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);

	DBG_ERROR("sxg: call ioremap(mmio_start[%lx], mmio_len[%lx])\n",
		  mmio_start, mmio_len);

	memmapped_ioaddr = ioremap(mmio_start, mmio_len);
	DBG_ERROR("sxg: %s MEMMAPPED_IOADDR [%p]\n", __FUNCTION__, memmapped_ioaddr);
	if (!memmapped_ioaddr) {
		DBG_ERROR("%s cannot remap MMIO region %lx @ %lx\n",
			  __FUNCTION__, mmio_len, mmio_start);
		goto err_out_free_mmio_region;
	}

	DBG_ERROR("sxg: %s found Alacritech SXG PCI, MMIO at %p, start[%lx] len[%lx], IRQ %d.\n",
	     __func__, memmapped_ioaddr, mmio_start, mmio_len, pcidev->irq);

	adapter->HwRegs = (void *) memmapped_ioaddr;
	adapter->base_addr = memmapped_ioaddr;

	mmio_start = pci_resource_start(pcidev, 2);
	mmio_len = pci_resource_len(pcidev, 2);

	DBG_ERROR("sxg: call ioremap(mmio_start[%lx], mmio_len[%lx])\n",
		  mmio_start, mmio_len);

	memmapped_ioaddr = ioremap(mmio_start, mmio_len);
	DBG_ERROR("sxg: %s MEMMAPPED_IOADDR [%p]\n", __func__, memmapped_ioaddr);
	if (!memmapped_ioaddr) {
		DBG_ERROR("%s cannot remap MMIO region %lx @ %lx\n",
			  __FUNCTION__, mmio_len, mmio_start);
		goto err_out_free_mmio_region;
	}

	DBG_ERROR("sxg: %s found Alacritech SXG PCI, MMIO at %p, "
		  "start[%lx] len[%lx], IRQ %d.\n", __func__,
		  memmapped_ioaddr, mmio_start, mmio_len, pcidev->irq);

	adapter->UcodeRegs = (void *)memmapped_ioaddr;

	adapter->State = SXG_STATE_INITIALIZING;
	// Maintain a list of all adapters anchored by
	// the global SxgDriver structure.
	adapter->Next = SxgDriver.Adapters;
	SxgDriver.Adapters = adapter;
	adapter->AdapterID = ++SxgDriver.AdapterID;

	// Initialize CRC table used to determine multicast hash
	sxg_mcast_init_crc32();

	adapter->JumboEnabled = FALSE;
	adapter->RssEnabled = FALSE;
	if (adapter->JumboEnabled) {
		adapter->FrameSize = JUMBOMAXFRAME;
		adapter->ReceiveBufferSize = SXG_RCV_JUMBO_BUFFER_SIZE;
	} else {
		adapter->FrameSize = ETHERMAXFRAME;
		adapter->ReceiveBufferSize = SXG_RCV_DATA_BUFFER_SIZE;
	}

//    status = SXG_READ_EEPROM(adapter);
//    if (!status) {
//        goto sxg_init_bad;
//    }

	DBG_ERROR("sxg: %s ENTER sxg_config_pci\n", __FUNCTION__);
	sxg_config_pci(pcidev);
	DBG_ERROR("sxg: %s EXIT sxg_config_pci\n", __FUNCTION__);

	DBG_ERROR("sxg: %s ENTER sxg_init_driver\n", __FUNCTION__);
	sxg_init_driver();
	DBG_ERROR("sxg: %s EXIT sxg_init_driver\n", __FUNCTION__);

	adapter->vendid = pci_tbl_entry->vendor;
	adapter->devid = pci_tbl_entry->device;
	adapter->subsysid = pci_tbl_entry->subdevice;
	adapter->busnumber = pcidev->bus->number;
	adapter->slotnumber = ((pcidev->devfn >> 3) & 0x1F);
	adapter->functionnumber = (pcidev->devfn & 0x7);
	adapter->memorylength = pci_resource_len(pcidev, 0);
	adapter->irq = pcidev->irq;
	adapter->next_netdevice = head_netdevice;
	head_netdevice = netdev;
//      adapter->chipid = chip_idx;
	adapter->port = 0;	//adapter->functionnumber;
	adapter->cardindex = adapter->port;

	// Allocate memory and other resources
	DBG_ERROR("sxg: %s ENTER sxg_allocate_resources\n", __FUNCTION__);
	status = sxg_allocate_resources(adapter);
	DBG_ERROR("sxg: %s EXIT sxg_allocate_resources status %x\n",
		  __FUNCTION__, status);
	if (status != STATUS_SUCCESS) {
		goto err_out_unmap;
	}

	DBG_ERROR("sxg: %s ENTER sxg_download_microcode\n", __FUNCTION__);
	if (sxg_download_microcode(adapter, SXG_UCODE_SAHARA)) {
		DBG_ERROR("sxg: %s ENTER sxg_adapter_set_hwaddr\n",
			  __FUNCTION__);
		sxg_adapter_set_hwaddr(adapter);
	} else {
		adapter->state = ADAPT_FAIL;
		adapter->linkstate = LINK_DOWN;
		DBG_ERROR("sxg_download_microcode FAILED status[%x]\n", status);
	}

	netdev->base_addr = (unsigned long)adapter->base_addr;
	netdev->irq = adapter->irq;
	netdev->open = sxg_entry_open;
	netdev->stop = sxg_entry_halt;
	netdev->hard_start_xmit = sxg_send_packets;
	netdev->do_ioctl = sxg_ioctl;
#if XXXTODO
	netdev->set_mac_address = sxg_mac_set_address;
#if SLIC_GET_STATS_ENABLED
	netdev->get_stats = sxg_get_stats;
#endif
	netdev->set_multicast_list = sxg_mcast_set_list;
#endif

	strcpy(netdev->name, "eth%d");
//  strcpy(netdev->name, pci_name(pcidev));
	if ((err = register_netdev(netdev))) {
		DBG_ERROR("Cannot register net device, aborting. %s\n",
			  netdev->name);
		goto err_out_unmap;
	}

	DBG_ERROR
	    ("sxg: %s addr 0x%lx, irq %d, MAC addr %02X:%02X:%02X:%02X:%02X:%02X\n",
	     netdev->name, netdev->base_addr, pcidev->irq, netdev->dev_addr[0],
	     netdev->dev_addr[1], netdev->dev_addr[2], netdev->dev_addr[3],
	     netdev->dev_addr[4], netdev->dev_addr[5]);

//sxg_init_bad:
	ASSERT(status == FALSE);
//      sxg_free_adapter(adapter);

	DBG_ERROR("sxg: %s EXIT status[%x] jiffies[%lx] cpu %d\n", __FUNCTION__,
		  status, jiffies, smp_processor_id());
	return status;

      err_out_unmap:
	iounmap((void *)memmapped_ioaddr);

      err_out_free_mmio_region:
	release_mem_region(mmio_start, mmio_len);

      err_out_exit_sxg_probe:

	DBG_ERROR("%s EXIT jiffies[%lx] cpu %d\n", __FUNCTION__, jiffies,
		  smp_processor_id());

	return -ENODEV;
}


/***********************************************************************
 * LINE BASE Interrupt routines..
 ***********************************************************************/
/*
 *
 * sxg_disable_interrupt
 *
 * DisableInterrupt Handler
 *
 * Arguments:
 *
 *   adapter:	Our adapter structure
 *
 * Return Value:
 * 	None.
 */
static void sxg_disable_interrupt(p_adapter_t adapter)
{
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DisIntr",
		  adapter, adapter->InterruptsEnabled, 0, 0);
	// For now, RSS is disabled with line based interrupts
	ASSERT(adapter->RssEnabled == FALSE);
	ASSERT(adapter->MsiEnabled == FALSE);
	//
	// Turn off interrupts by writing to the icr register.
	//
	WRITE_REG(adapter->UcodeRegs[0].Icr, SXG_ICR(0, SXG_ICR_DISABLE), TRUE);

	adapter->InterruptsEnabled = 0;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XDisIntr",
		  adapter, adapter->InterruptsEnabled, 0, 0);
}

/*
 *
 * sxg_enable_interrupt
 *
 * EnableInterrupt Handler
 *
 * Arguments:
 *
 *   adapter:	Our adapter structure
 *
 * Return Value:
 * 	None.
 */
static void sxg_enable_interrupt(p_adapter_t adapter)
{
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "EnIntr",
		  adapter, adapter->InterruptsEnabled, 0, 0);
	// For now, RSS is disabled with line based interrupts
	ASSERT(adapter->RssEnabled == FALSE);
	ASSERT(adapter->MsiEnabled == FALSE);
	//
	// Turn on interrupts by writing to the icr register.
	//
	WRITE_REG(adapter->UcodeRegs[0].Icr, SXG_ICR(0, SXG_ICR_ENABLE), TRUE);

	adapter->InterruptsEnabled = 1;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XEnIntr",
		  adapter, 0, 0, 0);
}

/*
 *
 * sxg_isr - Process an line-based interrupt
 *
 * Arguments:
 * 		Context			- Our adapter structure
 *		QueueDefault 	- Output parameter to queue to default CPU
 *		TargetCpus		- Output bitmap to schedule DPC's
 *
 * Return Value:
 * 	TRUE if our interrupt
 */
static irqreturn_t sxg_isr(int irq, void *dev_id)
{
	p_net_device dev = (p_net_device) dev_id;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
//      u32                 CpuMask = 0, i;

	adapter->Stats.NumInts++;
	if (adapter->Isr[0] == 0) {
		// The SLIC driver used to experience a number of spurious interrupts
		// due to the delay associated with the masking of the interrupt
		// (we'd bounce back in here).  If we see that again with Sahara,
		// add a READ_REG of the Icr register after the WRITE_REG below.
		adapter->Stats.FalseInts++;
		return IRQ_NONE;
	}
	//
	// Move the Isr contents and clear the value in
	// shared memory, and mask interrupts
	//
	adapter->IsrCopy[0] = adapter->Isr[0];
	adapter->Isr[0] = 0;
	WRITE_REG(adapter->UcodeRegs[0].Icr, SXG_ICR(0, SXG_ICR_MASK), TRUE);
//      ASSERT(adapter->IsrDpcsPending == 0);
#if XXXTODO			// RSS Stuff
	// If RSS is enabled and the ISR specifies
	// SXG_ISR_EVENT, then schedule DPC's
	// based on event queues.
	if (adapter->RssEnabled && (adapter->IsrCopy[0] & SXG_ISR_EVENT)) {
		for (i = 0;
		     i < adapter->RssSystemInfo->ProcessorInfo.RssCpuCount;
		     i++) {
			PSXG_EVENT_RING EventRing = &adapter->EventRings[i];
			PSXG_EVENT Event =
			    &EventRing->Ring[adapter->NextEvent[i]];
			unsigned char Cpu = adapter->RssSystemInfo->RssIdToCpu[i];
			if (Event->Status & EVENT_STATUS_VALID) {
				adapter->IsrDpcsPending++;
				CpuMask |= (1 << Cpu);
			}
		}
	}
	// Now, either schedule the CPUs specified by the CpuMask,
	// or queue default
	if (CpuMask) {
		*QueueDefault = FALSE;
	} else {
		adapter->IsrDpcsPending = 1;
		*QueueDefault = TRUE;
	}
	*TargetCpus = CpuMask;
#endif
	//
	//  There are no DPCs in Linux, so call the handler now
	//
	sxg_handle_interrupt(adapter);

	return IRQ_HANDLED;
}

static void sxg_handle_interrupt(p_adapter_t adapter)
{
//    unsigned char           RssId   = 0;
	u32 NewIsr;

	if (adapter->Stats.RcvNoBuffer < 5) {
		DBG_ERROR("Enter sxg_handle_interrupt ISR[%x]\n",
			  adapter->IsrCopy[0]);
	}
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "HndlIntr",
		  adapter, adapter->IsrCopy[0], 0, 0);
	// For now, RSS is disabled with line based interrupts
	ASSERT(adapter->RssEnabled == FALSE);
	ASSERT(adapter->MsiEnabled == FALSE);
	ASSERT(adapter->IsrCopy[0]);
/////////////////////////////

	// Always process the event queue.
	sxg_process_event_queue(adapter,
				(adapter->RssEnabled ? /*RssId */ 0 : 0));

#if XXXTODO			// RSS stuff
	if (--adapter->IsrDpcsPending) {
		// We're done.
		ASSERT(adapter->RssEnabled);
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DPCsPend",
			  adapter, 0, 0, 0);
		return;
	}
#endif
	//
	// Last (or only) DPC processes the ISR and clears the interrupt.
	//
	NewIsr = sxg_process_isr(adapter, 0);
	//
	// Reenable interrupts
	//
	adapter->IsrCopy[0] = 0;
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "ClearIsr",
		  adapter, NewIsr, 0, 0);

	if (adapter->Stats.RcvNoBuffer < 5) {
		DBG_ERROR
		    ("Exit sxg_handle_interrupt2 after enabling interrupt\n");
	}

	WRITE_REG(adapter->UcodeRegs[0].Isr, NewIsr, TRUE);

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XHndlInt",
		  adapter, 0, 0, 0);
}

/*
 *
 * sxg_process_isr - Process an interrupt.  Called from the line-based and
 *			message based interrupt DPC routines
 *
 * Arguments:
 * 		adapter			- Our adapter structure
 *		Queue			- The ISR that needs processing
 *
 * Return Value:
 * 	None
 */
static int sxg_process_isr(p_adapter_t adapter, u32 MessageId)
{
	u32 Isr = adapter->IsrCopy[MessageId];
	u32 NewIsr = 0;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "ProcIsr",
		  adapter, Isr, 0, 0);

	// Error
	if (Isr & SXG_ISR_ERR) {
		if (Isr & SXG_ISR_PDQF) {
			adapter->Stats.PdqFull++;
			DBG_ERROR("%s: SXG_ISR_ERR  PDQF!!\n", __FUNCTION__);
		}
		// No host buffer
		if (Isr & SXG_ISR_RMISS) {
			// There is a bunch of code in the SLIC driver which
			// attempts to process more receive events per DPC
			// if we start to fall behind.  We'll probably
			// need to do something similar here, but hold
			// off for now.  I don't want to make the code more
			// complicated than strictly needed.
			adapter->Stats.RcvNoBuffer++;
			if (adapter->Stats.RcvNoBuffer < 5) {
				DBG_ERROR("%s: SXG_ISR_ERR  RMISS!!\n",
					  __FUNCTION__);
			}
		}
		// Card crash
		if (Isr & SXG_ISR_DEAD) {
			// Set aside the crash info and set the adapter state to RESET
			adapter->CrashCpu =
			    (unsigned char) ((Isr & SXG_ISR_CPU) >> SXG_ISR_CPU_SHIFT);
			adapter->CrashLocation = (ushort) (Isr & SXG_ISR_CRASH);
			adapter->Dead = TRUE;
			DBG_ERROR("%s: ISR_DEAD %x, CPU: %d\n", __FUNCTION__,
				  adapter->CrashLocation, adapter->CrashCpu);
		}
		// Event ring full
		if (Isr & SXG_ISR_ERFULL) {
			// Same issue as RMISS, really.  This means the
			// host is falling behind the card.  Need to increase
			// event ring size, process more events per interrupt,
			// and/or reduce/remove interrupt aggregation.
			adapter->Stats.EventRingFull++;
			DBG_ERROR("%s: SXG_ISR_ERR  EVENT RING FULL!!\n",
				  __FUNCTION__);
		}
		// Transmit drop - no DRAM buffers or XMT error
		if (Isr & SXG_ISR_XDROP) {
			adapter->Stats.XmtDrops++;
			adapter->Stats.XmtErrors++;
			DBG_ERROR("%s: SXG_ISR_ERR  XDROP!!\n", __FUNCTION__);
		}
	}
	// Slowpath send completions
	if (Isr & SXG_ISR_SPSEND) {
		sxg_complete_slow_send(adapter);
	}
	// Dump
	if (Isr & SXG_ISR_UPC) {
		ASSERT(adapter->DumpCmdRunning);	// Maybe change when debug is added..
		adapter->DumpCmdRunning = FALSE;
	}
	// Link event
	if (Isr & SXG_ISR_LINK) {
		sxg_link_event(adapter);
	}
	// Debug - breakpoint hit
	if (Isr & SXG_ISR_BREAK) {
		// At the moment AGDB isn't written to support interactive
		// debug sessions.  When it is, this interrupt will be used
		// to signal AGDB that it has hit a breakpoint.  For now, ASSERT.
		ASSERT(0);
	}
	// Heartbeat response
	if (Isr & SXG_ISR_PING) {
		adapter->PingOutstanding = FALSE;
	}
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XProcIsr",
		  adapter, Isr, NewIsr, 0);

	return (NewIsr);
}

/*
 *
 * sxg_process_event_queue - Process our event queue
 *
 * Arguments:
 * 		- adapter	- Adapter structure
 *		- RssId		- The event queue requiring processing
 *
 * Return Value:
 * 	None.
 */
static u32 sxg_process_event_queue(p_adapter_t adapter, u32 RssId)
{
	PSXG_EVENT_RING EventRing = &adapter->EventRings[RssId];
	PSXG_EVENT Event = &EventRing->Ring[adapter->NextEvent[RssId]];
	u32 EventsProcessed = 0, Batches = 0;
	u32 num_skbs = 0;
	struct sk_buff *skb;
#ifdef LINUX_HANDLES_RCV_INDICATION_LISTS
	struct sk_buff *prev_skb = NULL;
	struct sk_buff *IndicationList[SXG_RCV_ARRAYSIZE];
	u32 Index;
	PSXG_RCV_DATA_BUFFER_HDR RcvDataBufferHdr;
#endif
	u32 ReturnStatus = 0;

	ASSERT((adapter->State == SXG_STATE_RUNNING) ||
	       (adapter->State == SXG_STATE_PAUSING) ||
	       (adapter->State == SXG_STATE_PAUSED) ||
	       (adapter->State == SXG_STATE_HALTING));
	// We may still have unprocessed events on the queue if
	// the card crashed.  Don't process them.
	if (adapter->Dead) {
		return (0);
	}
	// In theory there should only be a single processor that
	// accesses this queue, and only at interrupt-DPC time.  So
	// we shouldn't need a lock for any of this.
	while (Event->Status & EVENT_STATUS_VALID) {
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "Event",
			  Event, Event->Code, Event->Status,
			  adapter->NextEvent);
		switch (Event->Code) {
		case EVENT_CODE_BUFFERS:
			ASSERT(!(Event->CommandIndex & 0xFF00));	// SXG_RING_INFO Head & Tail == unsigned char
			//
			sxg_complete_descriptor_blocks(adapter,
						       Event->CommandIndex);
			//
			break;
		case EVENT_CODE_SLOWRCV:
			--adapter->RcvBuffersOnCard;
			if ((skb = sxg_slow_receive(adapter, Event))) {
				u32 rx_bytes;
#ifdef LINUX_HANDLES_RCV_INDICATION_LISTS
				// Add it to our indication list
				SXG_ADD_RCV_PACKET(adapter, skb, prev_skb,
						   IndicationList, num_skbs);
				//  In Linux, we just pass up each skb to the protocol above at this point,
				//  there is no capability of an indication list.
#else
// CHECK            skb_pull(skb, INIC_RCVBUF_HEADSIZE);
				rx_bytes = Event->Length;	// (rcvbuf->length & IRHDDR_FLEN_MSK);
				skb_put(skb, rx_bytes);
				adapter->stats.rx_packets++;
				adapter->stats.rx_bytes += rx_bytes;
#if SXG_OFFLOAD_IP_CHECKSUM
				skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif
				skb->dev = adapter->netdev;
				skb->protocol = eth_type_trans(skb, skb->dev);
				netif_rx(skb);
#endif
			}
			break;
		default:
			DBG_ERROR("%s: ERROR  Invalid EventCode %d\n",
				  __FUNCTION__, Event->Code);
//                      ASSERT(0);
		}
		// See if we need to restock card receive buffers.
		// There are two things to note here:
		//      First - This test is not SMP safe.  The
		//              adapter->BuffersOnCard field is protected via atomic interlocked calls, but
		//              we do not protect it with respect to these tests.  The only way to do that
		//      is with a lock, and I don't want to grab a lock every time we adjust the
		//      BuffersOnCard count.  Instead, we allow the buffer replenishment to be off
		//      once in a while.  The worst that can happen is the card is given one
		//      more-or-less descriptor block than the arbitrary value we've chosen.
		//      No big deal
		//      In short DO NOT ADD A LOCK HERE, OR WHERE RcvBuffersOnCard is adjusted.
		//      Second - We expect this test to rarely evaluate to true.  We attempt to
		//      refill descriptor blocks as they are returned to us
		//      (sxg_complete_descriptor_blocks), so The only time this should evaluate
		//      to true is when sxg_complete_descriptor_blocks failed to allocate
		//              receive buffers.
		if (adapter->RcvBuffersOnCard < SXG_RCV_DATA_BUFFERS) {
			sxg_stock_rcv_buffers(adapter);
		}
		// It's more efficient to just set this to zero.
		// But clearing the top bit saves potential debug info...
		Event->Status &= ~EVENT_STATUS_VALID;
		// Advanct to the next event
		SXG_ADVANCE_INDEX(adapter->NextEvent[RssId], EVENT_RING_SIZE);
		Event = &EventRing->Ring[adapter->NextEvent[RssId]];
		EventsProcessed++;
		if (EventsProcessed == EVENT_RING_BATCH) {
			// Release a batch of events back to the card
			WRITE_REG(adapter->UcodeRegs[RssId].EventRelease,
				  EVENT_RING_BATCH, FALSE);
			EventsProcessed = 0;
			// If we've processed our batch limit, break out of the
			// loop and return SXG_ISR_EVENT to arrange for us to
			// be called again
			if (Batches++ == EVENT_BATCH_LIMIT) {
				SXG_TRACE(TRACE_SXG, SxgTraceBuffer,
					  TRACE_NOISY, "EvtLimit", Batches,
					  adapter->NextEvent, 0, 0);
				ReturnStatus = SXG_ISR_EVENT;
				break;
			}
		}
	}
#ifdef LINUX_HANDLES_RCV_INDICATION_LISTS
	//
	// Indicate any received dumb-nic frames
	//
	SXG_INDICATE_PACKETS(adapter, IndicationList, num_skbs);
#endif
	//
	// Release events back to the card.
	//
	if (EventsProcessed) {
		WRITE_REG(adapter->UcodeRegs[RssId].EventRelease,
			  EventsProcessed, FALSE);
	}
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XPrcEvnt",
		  Batches, EventsProcessed, adapter->NextEvent, num_skbs);

	return (ReturnStatus);
}

/*
 * sxg_complete_slow_send - Complete slowpath or dumb-nic sends
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure

 * Return
 *	None
 */
static void sxg_complete_slow_send(p_adapter_t adapter)
{
	PSXG_XMT_RING XmtRing = &adapter->XmtRings[0];
	PSXG_RING_INFO XmtRingInfo = &adapter->XmtRingZeroInfo;
	u32 * ContextType;
	PSXG_CMD XmtCmd;

	// NOTE - This lock is dropped and regrabbed in this loop.
	// This means two different processors can both be running
	// through this loop. Be *very* careful.
	spin_lock(&adapter->XmtZeroLock);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "CmpSnds",
		  adapter, XmtRingInfo->Head, XmtRingInfo->Tail, 0);

	while (XmtRingInfo->Tail != *adapter->XmtRingZeroIndex) {
		// Locate the current Cmd (ring descriptor entry), and
		// associated SGL, and advance the tail
		SXG_RETURN_CMD(XmtRing, XmtRingInfo, XmtCmd, ContextType);
		ASSERT(ContextType);
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "CmpSnd",
			  XmtRingInfo->Head, XmtRingInfo->Tail, XmtCmd, 0);
		// Clear the SGL field.
		XmtCmd->Sgl = 0;

		switch (*ContextType) {
		case SXG_SGL_DUMB:
			{
				struct sk_buff *skb;
				// Dumb-nic send.  Command context is the dumb-nic SGL
				skb = (struct sk_buff *)ContextType;
				// Complete the send
				SXG_TRACE(TRACE_SXG, SxgTraceBuffer,
					  TRACE_IMPORTANT, "DmSndCmp", skb, 0,
					  0, 0);
				ASSERT(adapter->Stats.XmtQLen);
				adapter->Stats.XmtQLen--;	// within XmtZeroLock
				adapter->Stats.XmtOk++;
				// Now drop the lock and complete the send back to
				// Microsoft.  We need to drop the lock because
				// Microsoft can come back with a chimney send, which
				// results in a double trip in SxgTcpOuput
				spin_unlock(&adapter->XmtZeroLock);
				SXG_COMPLETE_DUMB_SEND(adapter, skb);
				// and reacquire..
				spin_lock(&adapter->XmtZeroLock);
			}
			break;
		default:
			ASSERT(0);
		}
	}
	spin_unlock(&adapter->XmtZeroLock);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "CmpSnd",
		  adapter, XmtRingInfo->Head, XmtRingInfo->Tail, 0);
}

/*
 * sxg_slow_receive
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *	Event		- Receive event
 *
 * Return
 *	 skb
 */
static struct sk_buff *sxg_slow_receive(p_adapter_t adapter, PSXG_EVENT Event)
{
	PSXG_RCV_DATA_BUFFER_HDR RcvDataBufferHdr;
	struct sk_buff *Packet;

	RcvDataBufferHdr = (PSXG_RCV_DATA_BUFFER_HDR) Event->HostHandle;
	ASSERT(RcvDataBufferHdr);
	ASSERT(RcvDataBufferHdr->State == SXG_BUFFER_ONCARD);
	ASSERT(SXG_RECEIVE_DATA_LOCATION(RcvDataBufferHdr) ==
	       RcvDataBufferHdr->VirtualAddress);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_IMPORTANT, "SlowRcv", Event,
		  RcvDataBufferHdr, RcvDataBufferHdr->State,
		  RcvDataBufferHdr->VirtualAddress);
	// Drop rcv frames in non-running state
	switch (adapter->State) {
	case SXG_STATE_RUNNING:
		break;
	case SXG_STATE_PAUSING:
	case SXG_STATE_PAUSED:
	case SXG_STATE_HALTING:
		goto drop;
	default:
		ASSERT(0);
		goto drop;
	}

	// Change buffer state to UPSTREAM
	RcvDataBufferHdr->State = SXG_BUFFER_UPSTREAM;
	if (Event->Status & EVENT_STATUS_RCVERR) {
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "RcvError",
			  Event, Event->Status, Event->HostHandle, 0);
		// XXXTODO - Remove this print later
		DBG_ERROR("SXG: Receive error %x\n",
			  *(u32 *)
			  SXG_RECEIVE_DATA_LOCATION(RcvDataBufferHdr));
		sxg_process_rcv_error(adapter,
				      *(u32 *)
				      SXG_RECEIVE_DATA_LOCATION
				      (RcvDataBufferHdr));
		goto drop;
	}
#if XXXTODO			// VLAN stuff
	// If there's a VLAN tag, extract it and validate it
	if (((p_ether_header) (SXG_RECEIVE_DATA_LOCATION(RcvDataBufferHdr)))->
	    EtherType == ETHERTYPE_VLAN) {
		if (SxgExtractVlanHeader(adapter, RcvDataBufferHdr, Event) !=
		    STATUS_SUCCESS) {
			SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY,
				  "BadVlan", Event,
				  SXG_RECEIVE_DATA_LOCATION(RcvDataBufferHdr),
				  Event->Length, 0);
			goto drop;
		}
	}
#endif
	//
	// Dumb-nic frame.  See if it passes our mac filter and update stats
	//
	if (!sxg_mac_filter(adapter,
			    (p_ether_header)
			    SXG_RECEIVE_DATA_LOCATION(RcvDataBufferHdr),
			    Event->Length)) {
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "RcvFiltr",
			  Event, SXG_RECEIVE_DATA_LOCATION(RcvDataBufferHdr),
			  Event->Length, 0);
		goto drop;
	}

	Packet = RcvDataBufferHdr->SxgDumbRcvPacket;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_IMPORTANT, "DumbRcv",
		  RcvDataBufferHdr, Packet, Event->Length, 0);
	//
	// Lastly adjust the receive packet length.
	//
	SXG_ADJUST_RCV_PACKET(Packet, RcvDataBufferHdr, Event);

	return (Packet);

      drop:
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DropRcv",
		  RcvDataBufferHdr, Event->Length, 0, 0);
	adapter->Stats.RcvDiscards++;
	spin_lock(&adapter->RcvQLock);
	SXG_FREE_RCV_DATA_BUFFER(adapter, RcvDataBufferHdr);
	spin_unlock(&adapter->RcvQLock);
	return (NULL);
}

/*
 * sxg_process_rcv_error - process receive error and update
 * stats
 *
 * Arguments:
 *		adapter		- Adapter structure
 *		ErrorStatus	- 4-byte receive error status
 *
 * Return Value:
 * 	None
 */
static void sxg_process_rcv_error(p_adapter_t adapter, u32 ErrorStatus)
{
	u32 Error;

	adapter->Stats.RcvErrors++;

	if (ErrorStatus & SXG_RCV_STATUS_TRANSPORT_ERROR) {
		Error = ErrorStatus & SXG_RCV_STATUS_TRANSPORT_MASK;
		switch (Error) {
		case SXG_RCV_STATUS_TRANSPORT_CSUM:
			adapter->Stats.TransportCsum++;
			break;
		case SXG_RCV_STATUS_TRANSPORT_UFLOW:
			adapter->Stats.TransportUflow++;
			break;
		case SXG_RCV_STATUS_TRANSPORT_HDRLEN:
			adapter->Stats.TransportHdrLen++;
			break;
		}
	}
	if (ErrorStatus & SXG_RCV_STATUS_NETWORK_ERROR) {
		Error = ErrorStatus & SXG_RCV_STATUS_NETWORK_MASK;
		switch (Error) {
		case SXG_RCV_STATUS_NETWORK_CSUM:
			adapter->Stats.NetworkCsum++;
			break;
		case SXG_RCV_STATUS_NETWORK_UFLOW:
			adapter->Stats.NetworkUflow++;
			break;
		case SXG_RCV_STATUS_NETWORK_HDRLEN:
			adapter->Stats.NetworkHdrLen++;
			break;
		}
	}
	if (ErrorStatus & SXG_RCV_STATUS_PARITY) {
		adapter->Stats.Parity++;
	}
	if (ErrorStatus & SXG_RCV_STATUS_LINK_ERROR) {
		Error = ErrorStatus & SXG_RCV_STATUS_LINK_MASK;
		switch (Error) {
		case SXG_RCV_STATUS_LINK_PARITY:
			adapter->Stats.LinkParity++;
			break;
		case SXG_RCV_STATUS_LINK_EARLY:
			adapter->Stats.LinkEarly++;
			break;
		case SXG_RCV_STATUS_LINK_BUFOFLOW:
			adapter->Stats.LinkBufOflow++;
			break;
		case SXG_RCV_STATUS_LINK_CODE:
			adapter->Stats.LinkCode++;
			break;
		case SXG_RCV_STATUS_LINK_DRIBBLE:
			adapter->Stats.LinkDribble++;
			break;
		case SXG_RCV_STATUS_LINK_CRC:
			adapter->Stats.LinkCrc++;
			break;
		case SXG_RCV_STATUS_LINK_OFLOW:
			adapter->Stats.LinkOflow++;
			break;
		case SXG_RCV_STATUS_LINK_UFLOW:
			adapter->Stats.LinkUflow++;
			break;
		}
	}
}

/*
 * sxg_mac_filter
 *
 * Arguments:
 *		adapter		- Adapter structure
 *		pether		- Ethernet header
 *		length		- Frame length
 *
 * Return Value:
 * 	TRUE if the frame is to be allowed
 */
static bool sxg_mac_filter(p_adapter_t adapter, p_ether_header EtherHdr, ushort length)
{
	bool EqualAddr;

	if (SXG_MULTICAST_PACKET(EtherHdr)) {
		if (SXG_BROADCAST_PACKET(EtherHdr)) {
			// broadcast
			if (adapter->MacFilter & MAC_BCAST) {
				adapter->Stats.DumbRcvBcastPkts++;
				adapter->Stats.DumbRcvBcastBytes += length;
				adapter->Stats.DumbRcvPkts++;
				adapter->Stats.DumbRcvBytes += length;
				return (TRUE);
			}
		} else {
			// multicast
			if (adapter->MacFilter & MAC_ALLMCAST) {
				adapter->Stats.DumbRcvMcastPkts++;
				adapter->Stats.DumbRcvMcastBytes += length;
				adapter->Stats.DumbRcvPkts++;
				adapter->Stats.DumbRcvBytes += length;
				return (TRUE);
			}
			if (adapter->MacFilter & MAC_MCAST) {
				PSXG_MULTICAST_ADDRESS MulticastAddrs =
				    adapter->MulticastAddrs;
				while (MulticastAddrs) {
					ETHER_EQ_ADDR(MulticastAddrs->Address,
						      EtherHdr->ether_dhost,
						      EqualAddr);
					if (EqualAddr) {
						adapter->Stats.
						    DumbRcvMcastPkts++;
						adapter->Stats.
						    DumbRcvMcastBytes += length;
						adapter->Stats.DumbRcvPkts++;
						adapter->Stats.DumbRcvBytes +=
						    length;
						return (TRUE);
					}
					MulticastAddrs = MulticastAddrs->Next;
				}
			}
		}
	} else if (adapter->MacFilter & MAC_DIRECTED) {
		// Not broadcast or multicast.  Must be directed at us or
		// the card is in promiscuous mode.  Either way, consider it
		// ours if MAC_DIRECTED is set
		adapter->Stats.DumbRcvUcastPkts++;
		adapter->Stats.DumbRcvUcastBytes += length;
		adapter->Stats.DumbRcvPkts++;
		adapter->Stats.DumbRcvBytes += length;
		return (TRUE);
	}
	if (adapter->MacFilter & MAC_PROMISC) {
		// Whatever it is, keep it.
		adapter->Stats.DumbRcvPkts++;
		adapter->Stats.DumbRcvBytes += length;
		return (TRUE);
	}
	adapter->Stats.RcvDiscards++;
	return (FALSE);
}

static int sxg_register_interrupt(p_adapter_t adapter)
{
	if (!adapter->intrregistered) {
		int retval;

		DBG_ERROR
		    ("sxg: %s AllocAdaptRsrcs adapter[%p] dev->irq[%x] %x\n",
		     __FUNCTION__, adapter, adapter->netdev->irq, NR_IRQS);

		spin_unlock_irqrestore(&sxg_global.driver_lock, sxg_global.flags);

		retval = request_irq(adapter->netdev->irq,
				     &sxg_isr,
				     IRQF_SHARED,
				     adapter->netdev->name, adapter->netdev);

		spin_lock_irqsave(&sxg_global.driver_lock, sxg_global.flags);

		if (retval) {
			DBG_ERROR("sxg: request_irq (%s) FAILED [%x]\n",
				  adapter->netdev->name, retval);
			return (retval);
		}
		adapter->intrregistered = 1;
		adapter->IntRegistered = TRUE;
		// Disable RSS with line-based interrupts
		adapter->MsiEnabled = FALSE;
		adapter->RssEnabled = FALSE;
		DBG_ERROR("sxg: %s AllocAdaptRsrcs adapter[%p] dev->irq[%x]\n",
			  __FUNCTION__, adapter, adapter->netdev->irq);
	}
	return (STATUS_SUCCESS);
}

static void sxg_deregister_interrupt(p_adapter_t adapter)
{
	DBG_ERROR("sxg: %s ENTER adapter[%p]\n", __FUNCTION__, adapter);
#if XXXTODO
	slic_init_cleanup(adapter);
#endif
	memset(&adapter->stats, 0, sizeof(struct net_device_stats));
	adapter->error_interrupts = 0;
	adapter->rcv_interrupts = 0;
	adapter->xmit_interrupts = 0;
	adapter->linkevent_interrupts = 0;
	adapter->upr_interrupts = 0;
	adapter->num_isrs = 0;
	adapter->xmit_completes = 0;
	adapter->rcv_broadcasts = 0;
	adapter->rcv_multicasts = 0;
	adapter->rcv_unicasts = 0;
	DBG_ERROR("sxg: %s EXIT\n", __FUNCTION__);
}

/*
 *  sxg_if_init
 *
 *  Perform initialization of our slic interface.
 *
 */
static int sxg_if_init(p_adapter_t adapter)
{
	p_net_device dev = adapter->netdev;
	int status = 0;

	DBG_ERROR("sxg: %s (%s) ENTER states[%d:%d:%d] flags[%x]\n",
		  __FUNCTION__, adapter->netdev->name,
		  adapter->queues_initialized, adapter->state,
		  adapter->linkstate, dev->flags);

	/* adapter should be down at this point */
	if (adapter->state != ADAPT_DOWN) {
		DBG_ERROR("sxg_if_init adapter->state != ADAPT_DOWN\n");
		return (-EIO);
	}
	ASSERT(adapter->linkstate == LINK_DOWN);

	adapter->devflags_prev = dev->flags;
	adapter->macopts = MAC_DIRECTED;
	if (dev->flags) {
		DBG_ERROR("sxg: %s (%s) Set MAC options: ", __FUNCTION__,
			  adapter->netdev->name);
		if (dev->flags & IFF_BROADCAST) {
			adapter->macopts |= MAC_BCAST;
			DBG_ERROR("BCAST ");
		}
		if (dev->flags & IFF_PROMISC) {
			adapter->macopts |= MAC_PROMISC;
			DBG_ERROR("PROMISC ");
		}
		if (dev->flags & IFF_ALLMULTI) {
			adapter->macopts |= MAC_ALLMCAST;
			DBG_ERROR("ALL_MCAST ");
		}
		if (dev->flags & IFF_MULTICAST) {
			adapter->macopts |= MAC_MCAST;
			DBG_ERROR("MCAST ");
		}
		DBG_ERROR("\n");
	}
	status = sxg_register_interrupt(adapter);
	if (status != STATUS_SUCCESS) {
		DBG_ERROR("sxg_if_init: sxg_register_interrupt FAILED %x\n",
			  status);
		sxg_deregister_interrupt(adapter);
		return (status);
	}

	adapter->state = ADAPT_UP;

	/*
	 *    clear any pending events, then enable interrupts
	 */
	DBG_ERROR("sxg: %s ENABLE interrupts(slic)\n", __FUNCTION__);

	return (STATUS_SUCCESS);
}

static int sxg_entry_open(p_net_device dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	int status;

	ASSERT(adapter);
	DBG_ERROR("sxg: %s adapter->activated[%d]\n", __FUNCTION__,
		  adapter->activated);
	DBG_ERROR
	    ("sxg: %s (%s): [jiffies[%lx] cpu %d] dev[%p] adapt[%p] port[%d]\n",
	     __FUNCTION__, adapter->netdev->name, jiffies, smp_processor_id(),
	     adapter->netdev, adapter, adapter->port);

	netif_stop_queue(adapter->netdev);

	spin_lock_irqsave(&sxg_global.driver_lock, sxg_global.flags);
	if (!adapter->activated) {
		sxg_global.num_sxg_ports_active++;
		adapter->activated = 1;
	}

	// Initialize the adapter
	DBG_ERROR("sxg: %s ENTER sxg_initialize_adapter\n", __FUNCTION__);
	status = sxg_initialize_adapter(adapter);
	DBG_ERROR("sxg: %s EXIT sxg_initialize_adapter status[%x]\n",
		  __FUNCTION__, status);

	if (status == STATUS_SUCCESS) {
		DBG_ERROR("sxg: %s ENTER sxg_if_init\n", __FUNCTION__);
		status = sxg_if_init(adapter);
		DBG_ERROR("sxg: %s EXIT sxg_if_init status[%x]\n", __FUNCTION__,
			  status);
	}

	if (status != STATUS_SUCCESS) {
		if (adapter->activated) {
			sxg_global.num_sxg_ports_active--;
			adapter->activated = 0;
		}
		spin_unlock_irqrestore(&sxg_global.driver_lock,
				       sxg_global.flags);
		return (status);
	}
	DBG_ERROR("sxg: %s ENABLE ALL INTERRUPTS\n", __FUNCTION__);

	// Enable interrupts
	SXG_ENABLE_ALL_INTERRUPTS(adapter);

	DBG_ERROR("sxg: %s EXIT\n", __FUNCTION__);

	spin_unlock_irqrestore(&sxg_global.driver_lock, sxg_global.flags);
	return STATUS_SUCCESS;
}

static void __devexit sxg_entry_remove(struct pci_dev *pcidev)
{
	p_net_device dev = pci_get_drvdata(pcidev);
	u32 mmio_start = 0;
	unsigned int mmio_len = 0;
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);

	ASSERT(adapter);
	DBG_ERROR("sxg: %s ENTER dev[%p] adapter[%p]\n", __FUNCTION__, dev,
		  adapter);
	sxg_deregister_interrupt(adapter);
	sxg_unmap_mmio_space(adapter);
	DBG_ERROR("sxg: %s unregister_netdev\n", __FUNCTION__);
	unregister_netdev(dev);

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);

	DBG_ERROR("sxg: %s rel_region(0) start[%x] len[%x]\n", __FUNCTION__,
		  mmio_start, mmio_len);
	release_mem_region(mmio_start, mmio_len);

	DBG_ERROR("sxg: %s iounmap dev->base_addr[%x]\n", __FUNCTION__,
		  (unsigned int) dev->base_addr);
	iounmap((char *)dev->base_addr);

	DBG_ERROR("sxg: %s deallocate device\n", __FUNCTION__);
	kfree(dev);
	DBG_ERROR("sxg: %s EXIT\n", __FUNCTION__);
}

static int sxg_entry_halt(p_net_device dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);

	spin_lock_irqsave(&sxg_global.driver_lock, sxg_global.flags);
	DBG_ERROR("sxg: %s (%s) ENTER\n", __FUNCTION__, dev->name);

	netif_stop_queue(adapter->netdev);
	adapter->state = ADAPT_DOWN;
	adapter->linkstate = LINK_DOWN;
	adapter->devflags_prev = 0;
	DBG_ERROR("sxg: %s (%s) set adapter[%p] state to ADAPT_DOWN(%d)\n",
		  __FUNCTION__, dev->name, adapter, adapter->state);

	DBG_ERROR("sxg: %s (%s) EXIT\n", __FUNCTION__, dev->name);
	DBG_ERROR("sxg: %s EXIT\n", __FUNCTION__);
	spin_unlock_irqrestore(&sxg_global.driver_lock, sxg_global.flags);
	return (STATUS_SUCCESS);
}

static int sxg_ioctl(p_net_device dev, struct ifreq *rq, int cmd)
{
	ASSERT(rq);
//      DBG_ERROR("sxg: %s cmd[%x] rq[%p] dev[%p]\n", __FUNCTION__, cmd, rq, dev);
	switch (cmd) {
	case SIOCSLICSETINTAGG:
		{
//                      p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
			u32 data[7];
			u32 intagg;

			if (copy_from_user(data, rq->ifr_data, 28)) {
				DBG_ERROR
				    ("copy_from_user FAILED  getting initial params\n");
				return -EFAULT;
			}
			intagg = data[0];
			printk(KERN_EMERG
			       "%s: set interrupt aggregation to %d\n",
			       __FUNCTION__, intagg);
			return 0;
		}

	default:
//              DBG_ERROR("sxg: %s UNSUPPORTED[%x]\n", __FUNCTION__, cmd);
		return -EOPNOTSUPP;
	}
	return 0;
}

#define NORMAL_ETHFRAME     0

/*
 *
 * sxg_send_packets - Send a skb packet
 *
 * Arguments:
 *			skb                     - The packet to send
 *			dev                     - Our linux net device that refs our adapter
 *
 * Return:
 *		0   regardless of outcome    XXXTODO refer to e1000 driver
 */
static int sxg_send_packets(struct sk_buff *skb, p_net_device dev)
{
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	u32 status = STATUS_SUCCESS;

	DBG_ERROR("sxg: %s ENTER sxg_send_packets skb[%p]\n", __FUNCTION__,
		  skb);
	// Check the adapter state
	switch (adapter->State) {
	case SXG_STATE_INITIALIZING:
	case SXG_STATE_HALTED:
	case SXG_STATE_SHUTDOWN:
		ASSERT(0);	// unexpected
		// fall through
	case SXG_STATE_RESETTING:
	case SXG_STATE_SLEEP:
	case SXG_STATE_BOOTDIAG:
	case SXG_STATE_DIAG:
	case SXG_STATE_HALTING:
		status = STATUS_FAILURE;
		break;
	case SXG_STATE_RUNNING:
		if (adapter->LinkState != SXG_LINK_UP) {
			status = STATUS_FAILURE;
		}
		break;
	default:
		ASSERT(0);
		status = STATUS_FAILURE;
	}
	if (status != STATUS_SUCCESS) {
		goto xmit_fail;
	}
	// send a packet
	status = sxg_transmit_packet(adapter, skb);
	if (status == STATUS_SUCCESS) {
		goto xmit_done;
	}

      xmit_fail:
	// reject & complete all the packets if they cant be sent
	if (status != STATUS_SUCCESS) {
#if XXXTODO
//      sxg_send_packets_fail(adapter, skb, status);
#else
		SXG_DROP_DUMB_SEND(adapter, skb);
		adapter->stats.tx_dropped++;
#endif
	}
	DBG_ERROR("sxg: %s EXIT sxg_send_packets status[%x]\n", __FUNCTION__,
		  status);

      xmit_done:
	return 0;
}

/*
 * sxg_transmit_packet
 *
 * This function transmits a single packet.
 *
 * Arguments -
 *		adapter			- Pointer to our adapter structure
 *      skb             - The packet to be sent
 *
 * Return -
 * 		STATUS of send
 */
static int sxg_transmit_packet(p_adapter_t adapter, struct sk_buff *skb)
{
	PSCATTER_GATHER_LIST pSgl;
	PSXG_SCATTER_GATHER SxgSgl;
	void * SglBuffer;
	u32 SglBufferLength;

	// The vast majority of work is done in the shared
	// sxg_dumb_sgl routine.
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DumbSend",
		  adapter, skb, 0, 0);

	// Allocate a SGL buffer
	SXG_GET_SGL_BUFFER(adapter, SxgSgl);
	if (!SxgSgl) {
		adapter->Stats.NoSglBuf++;
		adapter->Stats.XmtErrors++;
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "SndPktF1",
			  adapter, skb, 0, 0);
		return (STATUS_RESOURCES);
	}
	ASSERT(SxgSgl->adapter == adapter);
	SglBuffer = SXG_SGL_BUFFER(SxgSgl);
	SglBufferLength = SXG_SGL_BUF_SIZE;
	SxgSgl->VlanTag.VlanTci = 0;
	SxgSgl->VlanTag.VlanTpid = 0;
	SxgSgl->Type = SXG_SGL_DUMB;
	SxgSgl->DumbPacket = skb;
	pSgl = NULL;

	// Call the common sxg_dumb_sgl routine to complete the send.
	sxg_dumb_sgl(pSgl, SxgSgl);
	// Return success   sxg_dumb_sgl (or something later) will complete it.
	return (STATUS_SUCCESS);
}

/*
 * sxg_dumb_sgl
 *
 * Arguments:
 *		pSgl     -
 *		SxgSgl   - SXG_SCATTER_GATHER
 *
 * Return Value:
 * 	None.
 */
static void sxg_dumb_sgl(PSCATTER_GATHER_LIST pSgl, PSXG_SCATTER_GATHER SxgSgl)
{
	p_adapter_t adapter = SxgSgl->adapter;
	struct sk_buff *skb = SxgSgl->DumbPacket;
	// For now, all dumb-nic sends go on RSS queue zero
	PSXG_XMT_RING XmtRing = &adapter->XmtRings[0];
	PSXG_RING_INFO XmtRingInfo = &adapter->XmtRingZeroInfo;
	PSXG_CMD XmtCmd = NULL;
//      u32                         Index = 0;
	u32 DataLength = skb->len;
//  unsigned int                                BufLen;
//      u32                         SglOffset;
	u64 phys_addr;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DumbSgl",
		  pSgl, SxgSgl, 0, 0);

	// Set aside a pointer to the sgl
	SxgSgl->pSgl = pSgl;

	// Sanity check that our SGL format is as we expect.
	ASSERT(sizeof(SXG_X64_SGE) == sizeof(SCATTER_GATHER_ELEMENT));
	// Shouldn't be a vlan tag on this frame
	ASSERT(SxgSgl->VlanTag.VlanTci == 0);
	ASSERT(SxgSgl->VlanTag.VlanTpid == 0);

	// From here below we work with the SGL placed in our
	// buffer.

	SxgSgl->Sgl.NumberOfElements = 1;

	// Grab the spinlock and acquire a command
	spin_lock(&adapter->XmtZeroLock);
	SXG_GET_CMD(XmtRing, XmtRingInfo, XmtCmd, SxgSgl);
	if (XmtCmd == NULL) {
		// Call sxg_complete_slow_send to see if we can
		// free up any XmtRingZero entries and then try again
		spin_unlock(&adapter->XmtZeroLock);
		sxg_complete_slow_send(adapter);
		spin_lock(&adapter->XmtZeroLock);
		SXG_GET_CMD(XmtRing, XmtRingInfo, XmtCmd, SxgSgl);
		if (XmtCmd == NULL) {
			adapter->Stats.XmtZeroFull++;
			goto abortcmd;
		}
	}
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DumbCmd",
		  XmtCmd, XmtRingInfo->Head, XmtRingInfo->Tail, 0);
	// Update stats
	adapter->Stats.DumbXmtPkts++;
	adapter->Stats.DumbXmtBytes += DataLength;
#if XXXTODO			// Stats stuff
	if (SXG_MULTICAST_PACKET(EtherHdr)) {
		if (SXG_BROADCAST_PACKET(EtherHdr)) {
			adapter->Stats.DumbXmtBcastPkts++;
			adapter->Stats.DumbXmtBcastBytes += DataLength;
		} else {
			adapter->Stats.DumbXmtMcastPkts++;
			adapter->Stats.DumbXmtMcastBytes += DataLength;
		}
	} else {
		adapter->Stats.DumbXmtUcastPkts++;
		adapter->Stats.DumbXmtUcastBytes += DataLength;
	}
#endif
	// Fill in the command
	// Copy out the first SGE to the command and adjust for offset
	phys_addr = pci_map_single(adapter->pcidev, skb->data, skb->len, PCI_DMA_TODEVICE);
	XmtCmd->Buffer.FirstSgeAddress = SXG_GET_ADDR_HIGH(phys_addr);
	XmtCmd->Buffer.FirstSgeAddress = XmtCmd->Buffer.FirstSgeAddress << 32;
	XmtCmd->Buffer.FirstSgeAddress =
	    XmtCmd->Buffer.FirstSgeAddress | SXG_GET_ADDR_LOW(phys_addr);
//      XmtCmd->Buffer.FirstSgeAddress = SxgSgl->Sgl.Elements[Index].Address;
//      XmtCmd->Buffer.FirstSgeAddress.LowPart += MdlOffset;
	XmtCmd->Buffer.FirstSgeLength = DataLength;
	// Set a pointer to the remaining SGL entries
//      XmtCmd->Sgl = SxgSgl->PhysicalAddress;
	// Advance the physical address of the SxgSgl structure to
	// the second SGE
//      SglOffset = (u32)((u32 *)(&SxgSgl->Sgl.Elements[Index+1]) -
//                                              (u32 *)SxgSgl);
//      XmtCmd->Sgl.LowPart += SglOffset;
	XmtCmd->Buffer.SgeOffset = 0;
	// Note - TotalLength might be overwritten with MSS below..
	XmtCmd->Buffer.TotalLength = DataLength;
	XmtCmd->SgEntries = 1;	//(ushort)(SxgSgl->Sgl.NumberOfElements - Index);
	XmtCmd->Flags = 0;
	//
	// Advance transmit cmd descripter by 1.
	// NOTE - See comments in SxgTcpOutput where we write
	// to the XmtCmd register regarding CPU ID values and/or
	// multiple commands.
	//
	//
	WRITE_REG(adapter->UcodeRegs[0].XmtCmd, 1, TRUE);
	//
	//
	adapter->Stats.XmtQLen++;	// Stats within lock
	spin_unlock(&adapter->XmtZeroLock);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XDumSgl2",
		  XmtCmd, pSgl, SxgSgl, 0);
	return;

      abortcmd:
	// NOTE - Only jump to this label AFTER grabbing the
	// XmtZeroLock, and DO NOT DROP IT between the
	// command allocation and the following abort.
	if (XmtCmd) {
		SXG_ABORT_CMD(XmtRingInfo);
	}
	spin_unlock(&adapter->XmtZeroLock);

// failsgl:
	// Jump to this label if failure occurs before the
	// XmtZeroLock is grabbed
	adapter->Stats.XmtErrors++;
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_IMPORTANT, "DumSGFal",
		  pSgl, SxgSgl, XmtRingInfo->Head, XmtRingInfo->Tail);

	SXG_COMPLETE_DUMB_SEND(adapter, SxgSgl->DumbPacket);	// SxgSgl->DumbPacket is the skb
}

/***************************************************************
 * Link management functions
 ***************************************************************/

/*
 * sxg_initialize_link - Initialize the link stuff
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	status
 */
static int sxg_initialize_link(p_adapter_t adapter)
{
	PSXG_HW_REGS HwRegs = adapter->HwRegs;
	u32 Value;
	u32 ConfigData;
	u32 MaxFrame;
	int status;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "InitLink",
		  adapter, 0, 0, 0);

	// Reset PHY and XGXS module
	WRITE_REG(HwRegs->LinkStatus, LS_SERDES_POWER_DOWN, TRUE);

	// Reset transmit configuration register
	WRITE_REG(HwRegs->XmtConfig, XMT_CONFIG_RESET, TRUE);

	// Reset receive configuration register
	WRITE_REG(HwRegs->RcvConfig, RCV_CONFIG_RESET, TRUE);

	// Reset all MAC modules
	WRITE_REG(HwRegs->MacConfig0, AXGMAC_CFG0_SUB_RESET, TRUE);

	// Link address 0
	// XXXTODO - This assumes the MAC address (0a:0b:0c:0d:0e:0f)
	// is stored with the first nibble (0a) in the byte 0
	// of the Mac address.  Possibly reverse?
	Value = *(u32 *) adapter->MacAddr;
	WRITE_REG(HwRegs->LinkAddress0Low, Value, TRUE);
	// also write the MAC address to the MAC.  Endian is reversed.
	WRITE_REG(HwRegs->MacAddressLow, ntohl(Value), TRUE);
	Value = (*(u16 *) & adapter->MacAddr[4] & 0x0000FFFF);
	WRITE_REG(HwRegs->LinkAddress0High, Value | LINK_ADDRESS_ENABLE, TRUE);
	// endian swap for the MAC (put high bytes in bits [31:16], swapped)
	Value = ntohl(Value);
	WRITE_REG(HwRegs->MacAddressHigh, Value, TRUE);
	// Link address 1
	WRITE_REG(HwRegs->LinkAddress1Low, 0, TRUE);
	WRITE_REG(HwRegs->LinkAddress1High, 0, TRUE);
	// Link address 2
	WRITE_REG(HwRegs->LinkAddress2Low, 0, TRUE);
	WRITE_REG(HwRegs->LinkAddress2High, 0, TRUE);
	// Link address 3
	WRITE_REG(HwRegs->LinkAddress3Low, 0, TRUE);
	WRITE_REG(HwRegs->LinkAddress3High, 0, TRUE);

	// Enable MAC modules
	WRITE_REG(HwRegs->MacConfig0, 0, TRUE);

	// Configure MAC
	WRITE_REG(HwRegs->MacConfig1, (AXGMAC_CFG1_XMT_PAUSE |	// Allow sending of pause
				       AXGMAC_CFG1_XMT_EN |	// Enable XMT
				       AXGMAC_CFG1_RCV_PAUSE |	// Enable detection of pause
				       AXGMAC_CFG1_RCV_EN |	// Enable receive
				       AXGMAC_CFG1_SHORT_ASSERT |	// short frame detection
				       AXGMAC_CFG1_CHECK_LEN |	// Verify frame length
				       AXGMAC_CFG1_GEN_FCS |	// Generate FCS
				       AXGMAC_CFG1_PAD_64),	// Pad frames to 64 bytes
		  TRUE);

	// Set AXGMAC max frame length if jumbo.  Not needed for standard MTU
	if (adapter->JumboEnabled) {
		WRITE_REG(HwRegs->MacMaxFrameLen, AXGMAC_MAXFRAME_JUMBO, TRUE);
	}
	// AMIIM Configuration Register -
	// The value placed in the AXGMAC_AMIIM_CFG_HALF_CLOCK portion
	// (bottom bits) of this register is used to determine the
	// MDC frequency as specified in the A-XGMAC Design Document.
	// This value must not be zero.  The following value (62 or 0x3E)
	// is based on our MAC transmit clock frequency (MTCLK) of 312.5 MHz.
	// Given a maximum MDIO clock frequency of 2.5 MHz (see the PHY spec),
	// we get:  312.5/(2*(X+1)) < 2.5  ==> X = 62.
	// This value happens to be the default value for this register,
	// so we really don't have to do this.
	WRITE_REG(HwRegs->MacAmiimConfig, 0x0000003E, TRUE);

	// Power up and enable PHY and XAUI/XGXS/Serdes logic
	WRITE_REG(HwRegs->LinkStatus,
		  (LS_PHY_CLR_RESET |
		   LS_XGXS_ENABLE |
		   LS_XGXS_CTL | LS_PHY_CLK_EN | LS_ATTN_ALARM), TRUE);
	DBG_ERROR("After Power Up and enable PHY in sxg_initialize_link\n");

	// Per information given by Aeluros, wait 100 ms after removing reset.
	// It's not enough to wait for the self-clearing reset bit in reg 0 to clear.
	mdelay(100);

	// Verify the PHY has come up by checking that the Reset bit has cleared.
	status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
				   PHY_PMA_CONTROL1,	// PMA/PMD control register
				   &Value);
	if (status != STATUS_SUCCESS)
		return (STATUS_FAILURE);
	if (Value & PMA_CONTROL1_RESET)	// reset complete if bit is 0
		return (STATUS_FAILURE);

	// The SERDES should be initialized by now - confirm
	READ_REG(HwRegs->LinkStatus, Value);
	if (Value & LS_SERDES_DOWN)	// verify SERDES is initialized
		return (STATUS_FAILURE);

	// The XAUI link should also be up - confirm
	if (!(Value & LS_XAUI_LINK_UP))	// verify XAUI link is up
		return (STATUS_FAILURE);

	// Initialize the PHY
	status = sxg_phy_init(adapter);
	if (status != STATUS_SUCCESS)
		return (STATUS_FAILURE);

	// Enable the Link Alarm
	status = sxg_write_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
				    LASI_CONTROL,	// LASI control register
				    LASI_CTL_LS_ALARM_ENABLE);	// enable link alarm bit
	if (status != STATUS_SUCCESS)
		return (STATUS_FAILURE);

	// XXXTODO - temporary - verify bit is set
	status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
				   LASI_CONTROL,	// LASI control register
				   &Value);
	if (status != STATUS_SUCCESS)
		return (STATUS_FAILURE);
	if (!(Value & LASI_CTL_LS_ALARM_ENABLE)) {
		DBG_ERROR("Error!  LASI Control Alarm Enable bit not set!\n");
	}
	// Enable receive
	MaxFrame = adapter->JumboEnabled ? JUMBOMAXFRAME : ETHERMAXFRAME;
	ConfigData = (RCV_CONFIG_ENABLE |
		      RCV_CONFIG_ENPARSE |
		      RCV_CONFIG_RCVBAD |
		      RCV_CONFIG_RCVPAUSE |
		      RCV_CONFIG_TZIPV6 |
		      RCV_CONFIG_TZIPV4 |
		      RCV_CONFIG_HASH_16 |
		      RCV_CONFIG_SOCKET | RCV_CONFIG_BUFSIZE(MaxFrame));
	WRITE_REG(HwRegs->RcvConfig, ConfigData, TRUE);

	WRITE_REG(HwRegs->XmtConfig, XMT_CONFIG_ENABLE, TRUE);

	// Mark the link as down.  We'll get a link event when it comes up.
	sxg_link_state(adapter, SXG_LINK_DOWN);

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XInitLnk",
		  adapter, 0, 0, 0);
	return (STATUS_SUCCESS);
}

/*
 * sxg_phy_init - Initialize the PHY
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	status
 */
static int sxg_phy_init(p_adapter_t adapter)
{
	u32 Value;
	PPHY_UCODE p;
	int status;

	DBG_ERROR("ENTER %s\n", __FUNCTION__);

	// Read a register to identify the PHY type
	status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
				   0xC205,	// PHY ID register (?)
				   &Value);	//    XXXTODO - add def
	if (status != STATUS_SUCCESS)
		return (STATUS_FAILURE);

	if (Value == 0x0012) {	// 0x0012 == AEL2005C PHY(?) - XXXTODO - add def
		DBG_ERROR
		    ("AEL2005C PHY detected.  Downloading PHY microcode.\n");

		// Initialize AEL2005C PHY and download PHY microcode
		for (p = PhyUcode; p->Addr != 0xFFFF; p++) {
			if (p->Addr == 0) {
				// if address == 0, data == sleep time in ms
				mdelay(p->Data);
			} else {
				// write the given data to the specified address
				status = sxg_write_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
							    p->Addr,	// PHY address
							    p->Data);	// PHY data
				if (status != STATUS_SUCCESS)
					return (STATUS_FAILURE);
			}
		}
	}
	DBG_ERROR("EXIT %s\n", __FUNCTION__);

	return (STATUS_SUCCESS);
}

/*
 * sxg_link_event - Process a link event notification from the card
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	None
 */
static void sxg_link_event(p_adapter_t adapter)
{
	PSXG_HW_REGS HwRegs = adapter->HwRegs;
	SXG_LINK_STATE LinkState;
	int status;
	u32 Value;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "LinkEvnt",
		  adapter, 0, 0, 0);
	DBG_ERROR("ENTER %s\n", __FUNCTION__);

	// Check the Link Status register.  We should have a Link Alarm.
	READ_REG(HwRegs->LinkStatus, Value);
	if (Value & LS_LINK_ALARM) {
		// We got a Link Status alarm.  First, pause to let the
		// link state settle (it can bounce a number of times)
		mdelay(10);

		// Now clear the alarm by reading the LASI status register.
		status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
					   LASI_STATUS,	// LASI status register
					   &Value);
		if (status != STATUS_SUCCESS) {
			DBG_ERROR("Error reading LASI Status MDIO register!\n");
			sxg_link_state(adapter, SXG_LINK_DOWN);
//                      ASSERT(0);
		}
		ASSERT(Value & LASI_STATUS_LS_ALARM);

		// Now get and set the link state
		LinkState = sxg_get_link_state(adapter);
		sxg_link_state(adapter, LinkState);
		DBG_ERROR("SXG: Link Alarm occurred.  Link is %s\n",
			  ((LinkState == SXG_LINK_UP) ? "UP" : "DOWN"));
	} else {
		// XXXTODO - Assuming Link Attention is only being generated for the
		// Link Alarm pin (and not for a XAUI Link Status change), then it's
		// impossible to get here.  Yet we've gotten here twice (under extreme
		// conditions - bouncing the link up and down many times a second).
		// Needs further investigation.
		DBG_ERROR("SXG: sxg_link_event: Can't get here!\n");
		DBG_ERROR("SXG: Link Status == 0x%08X.\n", Value);
//              ASSERT(0);
	}
	DBG_ERROR("EXIT %s\n", __FUNCTION__);

}

/*
 * sxg_get_link_state - Determine if the link is up or down
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	Link State
 */
static SXG_LINK_STATE sxg_get_link_state(p_adapter_t adapter)
{
	int status;
	u32 Value;

	DBG_ERROR("ENTER %s\n", __FUNCTION__);

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "GetLink",
		  adapter, 0, 0, 0);

	// Per the Xenpak spec (and the IEEE 10Gb spec?), the link is up if
	// the following 3 bits (from 3 different MDIO registers) are all true.
	status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_PMA,	// PHY PMA/PMD module
				   PHY_PMA_RCV_DET,	// PMA/PMD Receive Signal Detect register
				   &Value);
	if (status != STATUS_SUCCESS)
		goto bad;

	// If PMA/PMD receive signal detect is 0, then the link is down
	if (!(Value & PMA_RCV_DETECT))
		return (SXG_LINK_DOWN);

	status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_PCS,	// PHY PCS module
				   PHY_PCS_10G_STATUS1,	// PCS 10GBASE-R Status 1 register
				   &Value);
	if (status != STATUS_SUCCESS)
		goto bad;

	// If PCS is not locked to receive blocks, then the link is down
	if (!(Value & PCS_10B_BLOCK_LOCK))
		return (SXG_LINK_DOWN);

	status = sxg_read_mdio_reg(adapter, MIIM_DEV_PHY_XS,	// PHY XS module
				   PHY_XS_LANE_STATUS,	// XS Lane Status register
				   &Value);
	if (status != STATUS_SUCCESS)
		goto bad;

	// If XS transmit lanes are not aligned, then the link is down
	if (!(Value & XS_LANE_ALIGN))
		return (SXG_LINK_DOWN);

	// All 3 bits are true, so the link is up
	DBG_ERROR("EXIT %s\n", __FUNCTION__);

	return (SXG_LINK_UP);

      bad:
	// An error occurred reading an MDIO register.  This shouldn't happen.
	DBG_ERROR("Error reading an MDIO register!\n");
	ASSERT(0);
	return (SXG_LINK_DOWN);
}

static void sxg_indicate_link_state(p_adapter_t adapter, SXG_LINK_STATE LinkState)
{
	if (adapter->LinkState == SXG_LINK_UP) {
		DBG_ERROR("%s: LINK now UP, call netif_start_queue\n",
			  __FUNCTION__);
		netif_start_queue(adapter->netdev);
	} else {
		DBG_ERROR("%s: LINK now DOWN, call netif_stop_queue\n",
			  __FUNCTION__);
		netif_stop_queue(adapter->netdev);
	}
}

/*
 * sxg_link_state - Set the link state and if necessary, indicate.
 *	This routine the central point of processing for all link state changes.
 *	Nothing else in the driver should alter the link state or perform
 *	link state indications
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *	LinkState 	- The link state
 *
 * Return
 *	None
 */
static void sxg_link_state(p_adapter_t adapter, SXG_LINK_STATE LinkState)
{
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_IMPORTANT, "LnkINDCT",
		  adapter, LinkState, adapter->LinkState, adapter->State);

	DBG_ERROR("ENTER %s\n", __FUNCTION__);

	// Hold the adapter lock during this routine.  Maybe move
	// the lock to the caller.
	spin_lock(&adapter->AdapterLock);
	if (LinkState == adapter->LinkState) {
		// Nothing changed..
		spin_unlock(&adapter->AdapterLock);
		DBG_ERROR("EXIT #0 %s\n", __FUNCTION__);
		return;
	}
	// Save the adapter state
	adapter->LinkState = LinkState;

	// Drop the lock and indicate link state
	spin_unlock(&adapter->AdapterLock);
	DBG_ERROR("EXIT #1 %s\n", __FUNCTION__);

	sxg_indicate_link_state(adapter, LinkState);
}

/*
 * sxg_write_mdio_reg - Write to a register on the MDIO bus
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *  DevAddr     - MDIO device number being addressed
 *  RegAddr     - register address for the specified MDIO device
 *  Value		- value to write to the MDIO register
 *
 * Return
 *	status
 */
static int sxg_write_mdio_reg(p_adapter_t adapter,
		   u32 DevAddr, u32 RegAddr, u32 Value)
{
	PSXG_HW_REGS HwRegs = adapter->HwRegs;
	u32 AddrOp;		// Address operation (written to MIIM field reg)
	u32 WriteOp;	// Write operation (written to MIIM field reg)
	u32 Cmd;		// Command (written to MIIM command reg)
	u32 ValueRead;
	u32 Timeout;

//  DBG_ERROR("ENTER %s\n", __FUNCTION__);

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "WrtMDIO",
		  adapter, 0, 0, 0);

	// Ensure values don't exceed field width
	DevAddr &= 0x001F;	// 5-bit field
	RegAddr &= 0xFFFF;	// 16-bit field
	Value &= 0xFFFF;	// 16-bit field

	// Set MIIM field register bits for an MIIM address operation
	AddrOp = (MIIM_PORT_NUM << AXGMAC_AMIIM_FIELD_PORT_SHIFT) |
	    (DevAddr << AXGMAC_AMIIM_FIELD_DEV_SHIFT) |
	    (MIIM_TA_10GB << AXGMAC_AMIIM_FIELD_TA_SHIFT) |
	    (MIIM_OP_ADDR << AXGMAC_AMIIM_FIELD_OP_SHIFT) | RegAddr;

	// Set MIIM field register bits for an MIIM write operation
	WriteOp = (MIIM_PORT_NUM << AXGMAC_AMIIM_FIELD_PORT_SHIFT) |
	    (DevAddr << AXGMAC_AMIIM_FIELD_DEV_SHIFT) |
	    (MIIM_TA_10GB << AXGMAC_AMIIM_FIELD_TA_SHIFT) |
	    (MIIM_OP_WRITE << AXGMAC_AMIIM_FIELD_OP_SHIFT) | Value;

	// Set MIIM command register bits to execute an MIIM command
	Cmd = AXGMAC_AMIIM_CMD_START | AXGMAC_AMIIM_CMD_10G_OPERATION;

	// Reset the command register command bit (in case it's not 0)
	WRITE_REG(HwRegs->MacAmiimCmd, 0, TRUE);

	// MIIM write to set the address of the specified MDIO register
	WRITE_REG(HwRegs->MacAmiimField, AddrOp, TRUE);

	// Write to MIIM Command Register to execute to address operation
	WRITE_REG(HwRegs->MacAmiimCmd, Cmd, TRUE);

	// Poll AMIIM Indicator register to wait for completion
	Timeout = SXG_LINK_TIMEOUT;
	do {
		udelay(100);	// Timeout in 100us units
		READ_REG(HwRegs->MacAmiimIndicator, ValueRead);
		if (--Timeout == 0) {
			return (STATUS_FAILURE);
		}
	} while (ValueRead & AXGMAC_AMIIM_INDC_BUSY);

	// Reset the command register command bit
	WRITE_REG(HwRegs->MacAmiimCmd, 0, TRUE);

	// MIIM write to set up an MDIO write operation
	WRITE_REG(HwRegs->MacAmiimField, WriteOp, TRUE);

	// Write to MIIM Command Register to execute the write operation
	WRITE_REG(HwRegs->MacAmiimCmd, Cmd, TRUE);

	// Poll AMIIM Indicator register to wait for completion
	Timeout = SXG_LINK_TIMEOUT;
	do {
		udelay(100);	// Timeout in 100us units
		READ_REG(HwRegs->MacAmiimIndicator, ValueRead);
		if (--Timeout == 0) {
			return (STATUS_FAILURE);
		}
	} while (ValueRead & AXGMAC_AMIIM_INDC_BUSY);

//  DBG_ERROR("EXIT %s\n", __FUNCTION__);

	return (STATUS_SUCCESS);
}

/*
 * sxg_read_mdio_reg - Read a register on the MDIO bus
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *  DevAddr     - MDIO device number being addressed
 *  RegAddr     - register address for the specified MDIO device
 *  pValue		- pointer to where to put data read from the MDIO register
 *
 * Return
 *	status
 */
static int sxg_read_mdio_reg(p_adapter_t adapter,
		  u32 DevAddr, u32 RegAddr, u32 * pValue)
{
	PSXG_HW_REGS HwRegs = adapter->HwRegs;
	u32 AddrOp;		// Address operation (written to MIIM field reg)
	u32 ReadOp;		// Read operation (written to MIIM field reg)
	u32 Cmd;		// Command (written to MIIM command reg)
	u32 ValueRead;
	u32 Timeout;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "WrtMDIO",
		  adapter, 0, 0, 0);
//  DBG_ERROR("ENTER %s\n", __FUNCTION__);

	// Ensure values don't exceed field width
	DevAddr &= 0x001F;	// 5-bit field
	RegAddr &= 0xFFFF;	// 16-bit field

	// Set MIIM field register bits for an MIIM address operation
	AddrOp = (MIIM_PORT_NUM << AXGMAC_AMIIM_FIELD_PORT_SHIFT) |
	    (DevAddr << AXGMAC_AMIIM_FIELD_DEV_SHIFT) |
	    (MIIM_TA_10GB << AXGMAC_AMIIM_FIELD_TA_SHIFT) |
	    (MIIM_OP_ADDR << AXGMAC_AMIIM_FIELD_OP_SHIFT) | RegAddr;

	// Set MIIM field register bits for an MIIM read operation
	ReadOp = (MIIM_PORT_NUM << AXGMAC_AMIIM_FIELD_PORT_SHIFT) |
	    (DevAddr << AXGMAC_AMIIM_FIELD_DEV_SHIFT) |
	    (MIIM_TA_10GB << AXGMAC_AMIIM_FIELD_TA_SHIFT) |
	    (MIIM_OP_READ << AXGMAC_AMIIM_FIELD_OP_SHIFT);

	// Set MIIM command register bits to execute an MIIM command
	Cmd = AXGMAC_AMIIM_CMD_START | AXGMAC_AMIIM_CMD_10G_OPERATION;

	// Reset the command register command bit (in case it's not 0)
	WRITE_REG(HwRegs->MacAmiimCmd, 0, TRUE);

	// MIIM write to set the address of the specified MDIO register
	WRITE_REG(HwRegs->MacAmiimField, AddrOp, TRUE);

	// Write to MIIM Command Register to execute to address operation
	WRITE_REG(HwRegs->MacAmiimCmd, Cmd, TRUE);

	// Poll AMIIM Indicator register to wait for completion
	Timeout = SXG_LINK_TIMEOUT;
	do {
		udelay(100);	// Timeout in 100us units
		READ_REG(HwRegs->MacAmiimIndicator, ValueRead);
		if (--Timeout == 0) {
			return (STATUS_FAILURE);
		}
	} while (ValueRead & AXGMAC_AMIIM_INDC_BUSY);

	// Reset the command register command bit
	WRITE_REG(HwRegs->MacAmiimCmd, 0, TRUE);

	// MIIM write to set up an MDIO register read operation
	WRITE_REG(HwRegs->MacAmiimField, ReadOp, TRUE);

	// Write to MIIM Command Register to execute the read operation
	WRITE_REG(HwRegs->MacAmiimCmd, Cmd, TRUE);

	// Poll AMIIM Indicator register to wait for completion
	Timeout = SXG_LINK_TIMEOUT;
	do {
		udelay(100);	// Timeout in 100us units
		READ_REG(HwRegs->MacAmiimIndicator, ValueRead);
		if (--Timeout == 0) {
			return (STATUS_FAILURE);
		}
	} while (ValueRead & AXGMAC_AMIIM_INDC_BUSY);

	// Read the MDIO register data back from the field register
	READ_REG(HwRegs->MacAmiimField, *pValue);
	*pValue &= 0xFFFF;	// data is in the lower 16 bits

//  DBG_ERROR("EXIT %s\n", __FUNCTION__);

	return (STATUS_SUCCESS);
}

/*
 *  Allocate a mcast_address structure to hold the multicast address.
 *  Link it in.
 */
static int sxg_mcast_add_list(p_adapter_t adapter, char *address)
{
	p_mcast_address_t mcaddr, mlist;
	bool equaladdr;

	/* Check to see if it already exists */
	mlist = adapter->mcastaddrs;
	while (mlist) {
		ETHER_EQ_ADDR(mlist->address, address, equaladdr);
		if (equaladdr) {
			return (STATUS_SUCCESS);
		}
		mlist = mlist->next;
	}

	/* Doesn't already exist.  Allocate a structure to hold it */
	mcaddr = kmalloc(sizeof(mcast_address_t), GFP_ATOMIC);
	if (mcaddr == NULL)
		return 1;

	memcpy(mcaddr->address, address, 6);

	mcaddr->next = adapter->mcastaddrs;
	adapter->mcastaddrs = mcaddr;

	return (STATUS_SUCCESS);
}

/*
 * Functions to obtain the CRC corresponding to the destination mac address.
 * This is a standard ethernet CRC in that it is a 32-bit, reflected CRC using
 * the polynomial:
 *   x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1.
 *
 * After the CRC for the 6 bytes is generated (but before the value is complemented),
 * we must then transpose the value and return bits 30-23.
 *
 */
static u32 sxg_crc_table[256]; /* Table of CRC's for all possible byte values */
static u32 sxg_crc_init;	/* Is table initialized                        */

/*
 *  Contruct the CRC32 table
 */
static void sxg_mcast_init_crc32(void)
{
	u32 c;		/*  CRC shit reg                 */
	u32 e = 0;		/*  Poly X-or pattern            */
	int i;			/*  counter                      */
	int k;			/*  byte being shifted into crc  */

	static int p[] = { 0, 1, 2, 4, 5, 7, 8, 10, 11, 12, 16, 22, 23, 26 };

	for (i = 0; i < sizeof(p) / sizeof(int); i++) {
		e |= 1L << (31 - p[i]);
	}

	for (i = 1; i < 256; i++) {
		c = i;
		for (k = 8; k; k--) {
			c = c & 1 ? (c >> 1) ^ e : c >> 1;
		}
		sxg_crc_table[i] = c;
	}
}

/*
 *  Return the MAC hast as described above.
 */
static unsigned char sxg_mcast_get_mac_hash(char *macaddr)
{
	u32 crc;
	char *p;
	int i;
	unsigned char machash = 0;

	if (!sxg_crc_init) {
		sxg_mcast_init_crc32();
		sxg_crc_init = 1;
	}

	crc = 0xFFFFFFFF;	/* Preload shift register, per crc-32 spec */
	for (i = 0, p = macaddr; i < 6; ++p, ++i) {
		crc = (crc >> 8) ^ sxg_crc_table[(crc ^ *p) & 0xFF];
	}

	/* Return bits 1-8, transposed */
	for (i = 1; i < 9; i++) {
		machash |= (((crc >> i) & 1) << (8 - i));
	}

	return (machash);
}

static void sxg_mcast_set_bit(p_adapter_t adapter, char *address)
{
	unsigned char crcpoly;

	/* Get the CRC polynomial for the mac address */
	crcpoly = sxg_mcast_get_mac_hash(address);

	/* We only have space on the SLIC for 64 entries.  Lop
	 * off the top two bits. (2^6 = 64)
	 */
	crcpoly &= 0x3F;

	/* OR in the new bit into our 64 bit mask. */
	adapter->MulticastMask |= (u64) 1 << crcpoly;
}

static void sxg_mcast_set_list(p_net_device dev)
{
#if XXXTODO
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	int status = STATUS_SUCCESS;
	int i;
	char *addresses;
	struct dev_mc_list *mc_list = dev->mc_list;
	int mc_count = dev->mc_count;

	ASSERT(adapter);

	for (i = 1; i <= mc_count; i++) {
		addresses = (char *) & mc_list->dmi_addr;
		if (mc_list->dmi_addrlen == 6) {
			status = sxg_mcast_add_list(adapter, addresses);
			if (status != STATUS_SUCCESS) {
				break;
			}
		} else {
			status = -EINVAL;
			break;
		}
		sxg_mcast_set_bit(adapter, addresses);
		mc_list = mc_list->next;
	}

	DBG_ERROR("%s a->devflags_prev[%x] dev->flags[%x] status[%x]\n",
		  __FUNCTION__, adapter->devflags_prev, dev->flags, status);
	if (adapter->devflags_prev != dev->flags) {
		adapter->macopts = MAC_DIRECTED;
		if (dev->flags) {
			if (dev->flags & IFF_BROADCAST) {
				adapter->macopts |= MAC_BCAST;
			}
			if (dev->flags & IFF_PROMISC) {
				adapter->macopts |= MAC_PROMISC;
			}
			if (dev->flags & IFF_ALLMULTI) {
				adapter->macopts |= MAC_ALLMCAST;
			}
			if (dev->flags & IFF_MULTICAST) {
				adapter->macopts |= MAC_MCAST;
			}
		}
		adapter->devflags_prev = dev->flags;
		DBG_ERROR("%s call sxg_config_set adapter->macopts[%x]\n",
			  __FUNCTION__, adapter->macopts);
		sxg_config_set(adapter, TRUE);
	} else {
		if (status == STATUS_SUCCESS) {
			sxg_mcast_set_mask(adapter);
		}
	}
#endif
	return;
}

static void sxg_mcast_set_mask(p_adapter_t adapter)
{
	PSXG_UCODE_REGS sxg_regs = adapter->UcodeRegs;

	DBG_ERROR("%s ENTER (%s) macopts[%x] mask[%llx]\n", __FUNCTION__,
		  adapter->netdev->name, (unsigned int) adapter->MacFilter,
		  adapter->MulticastMask);

	if (adapter->MacFilter & (MAC_ALLMCAST | MAC_PROMISC)) {
		/* Turn on all multicast addresses. We have to do this for promiscuous
		 * mode as well as ALLMCAST mode.  It saves the Microcode from having
		 * to keep state about the MAC configuration.
		 */
//              DBG_ERROR("sxg: %s macopts = MAC_ALLMCAST | MAC_PROMISC\n      SLUT MODE!!!\n",__FUNCTION__);
		WRITE_REG(sxg_regs->McastLow, 0xFFFFFFFF, FLUSH);
		WRITE_REG(sxg_regs->McastHigh, 0xFFFFFFFF, FLUSH);
//        DBG_ERROR("%s (%s) WRITE to slic_regs slic_mcastlow&high 0xFFFFFFFF\n",__FUNCTION__, adapter->netdev->name);

	} else {
		/* Commit our multicast mast to the SLIC by writing to the multicast
		 * address mask registers
		 */
		DBG_ERROR("%s (%s) WRITE mcastlow[%lx] mcasthigh[%lx]\n",
			  __FUNCTION__, adapter->netdev->name,
			  ((ulong) (adapter->MulticastMask & 0xFFFFFFFF)),
			  ((ulong)
			   ((adapter->MulticastMask >> 32) & 0xFFFFFFFF)));

		WRITE_REG(sxg_regs->McastLow,
			  (u32) (adapter->MulticastMask & 0xFFFFFFFF),
			  FLUSH);
		WRITE_REG(sxg_regs->McastHigh,
			  (u32) ((adapter->
				      MulticastMask >> 32) & 0xFFFFFFFF),
			  FLUSH);
	}
}

static void sxg_unmap_mmio_space(p_adapter_t adapter)
{
#if LINUX_FREES_ADAPTER_RESOURCES
//      if (adapter->Regs) {
//              iounmap(adapter->Regs);
//      }
//      adapter->slic_regs = NULL;
#endif
}

#if XXXTODO
/*
 * SxgFreeResources - Free everything allocated in SxgAllocateResources
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	none
 */
void SxgFreeResources(p_adapter_t adapter)
{
	u32 RssIds, IsrCount;
	PTCP_OBJECT TcpObject;
	u32 i;
	BOOLEAN TimerCancelled;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "FreeRes",
		  adapter, adapter->MaxTcbs, 0, 0);

	RssIds = SXG_RSS_CPU_COUNT(adapter);
	IsrCount = adapter->MsiEnabled ? RssIds : 1;

	if (adapter->BasicAllocations == FALSE) {
		// No allocations have been made, including spinlocks,
		// or listhead initializations.  Return.
		return;
	}

	if (!(IsListEmpty(&adapter->AllRcvBlocks))) {
		SxgFreeRcvBlocks(adapter);
	}
	if (!(IsListEmpty(&adapter->AllSglBuffers))) {
		SxgFreeSglBuffers(adapter);
	}
	// Free event queues.
	if (adapter->EventRings) {
		pci_free_consistent(adapter->pcidev,
				    sizeof(SXG_EVENT_RING) * RssIds,
				    adapter->EventRings, adapter->PEventRings);
	}
	if (adapter->Isr) {
		pci_free_consistent(adapter->pcidev,
				    sizeof(u32) * IsrCount,
				    adapter->Isr, adapter->PIsr);
	}
	if (adapter->XmtRingZeroIndex) {
		pci_free_consistent(adapter->pcidev,
				    sizeof(u32),
				    adapter->XmtRingZeroIndex,
				    adapter->PXmtRingZeroIndex);
	}
	if (adapter->IndirectionTable) {
		pci_free_consistent(adapter->pcidev,
				    SXG_MAX_RSS_TABLE_SIZE,
				    adapter->IndirectionTable,
				    adapter->PIndirectionTable);
	}

	SXG_FREE_PACKET_POOL(adapter->PacketPoolHandle);
	SXG_FREE_BUFFER_POOL(adapter->BufferPoolHandle);

	// Unmap register spaces
	SxgUnmapResources(adapter);

	// Deregister DMA
	if (adapter->DmaHandle) {
		SXG_DEREGISTER_DMA(adapter->DmaHandle);
	}
	// Deregister interrupt
	SxgDeregisterInterrupt(adapter);

	// Possibly free system info (5.2 only)
	SXG_RELEASE_SYSTEM_INFO(adapter);

	SxgDiagFreeResources(adapter);

	SxgFreeMCastAddrs(adapter);

	if (SXG_TIMER_ALLOCATED(adapter->ResetTimer)) {
		SXG_CANCEL_TIMER(adapter->ResetTimer, TimerCancelled);
		SXG_FREE_TIMER(adapter->ResetTimer);
	}
	if (SXG_TIMER_ALLOCATED(adapter->RssTimer)) {
		SXG_CANCEL_TIMER(adapter->RssTimer, TimerCancelled);
		SXG_FREE_TIMER(adapter->RssTimer);
	}
	if (SXG_TIMER_ALLOCATED(adapter->OffloadTimer)) {
		SXG_CANCEL_TIMER(adapter->OffloadTimer, TimerCancelled);
		SXG_FREE_TIMER(adapter->OffloadTimer);
	}

	adapter->BasicAllocations = FALSE;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XFreeRes",
		  adapter, adapter->MaxTcbs, 0, 0);
}
#endif

/*
 * sxg_allocate_complete -
 *
 * This routine is called when a memory allocation has completed.
 *
 * Arguments -
 *	p_adapter_t    	- Our adapter structure
 *	VirtualAddress	- Memory virtual address
 *	PhysicalAddress	- Memory physical address
 *	Length		- Length of memory allocated (or 0)
 *	Context		- The type of buffer allocated
 *
 * Return
 *	None.
 */
static void sxg_allocate_complete(p_adapter_t adapter,
		      void *VirtualAddress,
		      dma_addr_t PhysicalAddress,
		      u32 Length, SXG_BUFFER_TYPE Context)
{
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "AllocCmp",
		  adapter, VirtualAddress, Length, Context);
	ASSERT(adapter->AllocationsPending);
	--adapter->AllocationsPending;

	switch (Context) {

	case SXG_BUFFER_TYPE_RCV:
		sxg_allocate_rcvblock_complete(adapter,
					       VirtualAddress,
					       PhysicalAddress, Length);
		break;
	case SXG_BUFFER_TYPE_SGL:
		sxg_allocate_sgl_buffer_complete(adapter,
						 (PSXG_SCATTER_GATHER)
						 VirtualAddress,
						 PhysicalAddress, Length);
		break;
	}
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAlocCmp",
		  adapter, VirtualAddress, Length, Context);
}

/*
 * sxg_allocate_buffer_memory - Shared memory allocation routine used for
 *		synchronous and asynchronous buffer allocations
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *	Size		- block size to allocate
 *	BufferType	- Type of buffer to allocate
 *
 * Return
 *	int
 */
static int sxg_allocate_buffer_memory(p_adapter_t adapter,
			   u32 Size, SXG_BUFFER_TYPE BufferType)
{
	int status;
	void * Buffer;
	dma_addr_t pBuffer;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "AllocMem",
		  adapter, Size, BufferType, 0);
	// Grab the adapter lock and check the state.
	// If we're in anything other than INITIALIZING or
	// RUNNING state, fail.  This is to prevent
	// allocations in an improper driver state
	spin_lock(&adapter->AdapterLock);

	// Increment the AllocationsPending count while holding
	// the lock.  Pause processing relies on this
	++adapter->AllocationsPending;
	spin_unlock(&adapter->AdapterLock);

	// At initialization time allocate resources synchronously.
	Buffer = pci_alloc_consistent(adapter->pcidev, Size, &pBuffer);
	if (Buffer == NULL) {
		spin_lock(&adapter->AdapterLock);
		// Decrement the AllocationsPending count while holding
		// the lock.  Pause processing relies on this
		--adapter->AllocationsPending;
		spin_unlock(&adapter->AdapterLock);
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "AlcMemF1",
			  adapter, Size, BufferType, 0);
		return (STATUS_RESOURCES);
	}
	sxg_allocate_complete(adapter, Buffer, pBuffer, Size, BufferType);
	status = STATUS_SUCCESS;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAlocMem",
		  adapter, Size, BufferType, status);
	return (status);
}

/*
 * sxg_allocate_rcvblock_complete - Complete a receive descriptor block allocation
 *
 * Arguments -
 *	adapter				- A pointer to our adapter structure
 *	RcvBlock			- receive block virtual address
 *	PhysicalAddress		- Physical address
 *	Length				- Memory length
 *
 * Return
 *
 */
static void sxg_allocate_rcvblock_complete(p_adapter_t adapter,
			       void * RcvBlock,
			       dma_addr_t PhysicalAddress, u32 Length)
{
	u32 i;
	u32 BufferSize = adapter->ReceiveBufferSize;
	u64 Paddr;
	PSXG_RCV_BLOCK_HDR RcvBlockHdr;
	unsigned char *RcvDataBuffer;
	PSXG_RCV_DATA_BUFFER_HDR RcvDataBufferHdr;
	PSXG_RCV_DESCRIPTOR_BLOCK RcvDescriptorBlock;
	PSXG_RCV_DESCRIPTOR_BLOCK_HDR RcvDescriptorBlockHdr;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "AlRcvBlk",
		  adapter, RcvBlock, Length, 0);
	if (RcvBlock == NULL) {
		goto fail;
	}
	memset(RcvBlock, 0, Length);
	ASSERT((BufferSize == SXG_RCV_DATA_BUFFER_SIZE) ||
	       (BufferSize == SXG_RCV_JUMBO_BUFFER_SIZE));
	ASSERT(Length == SXG_RCV_BLOCK_SIZE(BufferSize));
	// First, initialize the contained pool of receive data
	// buffers.  This initialization requires NBL/NB/MDL allocations,
	// If any of them fail, free the block and return without
	// queueing the shared memory
	RcvDataBuffer = RcvBlock;
#if 0
	for (i = 0, Paddr = *PhysicalAddress;
	     i < SXG_RCV_DESCRIPTORS_PER_BLOCK;
	     i++, Paddr.LowPart += BufferSize, RcvDataBuffer += BufferSize)
#endif
		for (i = 0, Paddr = PhysicalAddress;
		     i < SXG_RCV_DESCRIPTORS_PER_BLOCK;
		     i++, Paddr += BufferSize, RcvDataBuffer += BufferSize) {
			//
			RcvDataBufferHdr =
			    (PSXG_RCV_DATA_BUFFER_HDR) (RcvDataBuffer +
							SXG_RCV_DATA_BUFFER_HDR_OFFSET
							(BufferSize));
			RcvDataBufferHdr->VirtualAddress = RcvDataBuffer;
			RcvDataBufferHdr->PhysicalAddress = Paddr;
			RcvDataBufferHdr->State = SXG_BUFFER_UPSTREAM;	// For FREE macro assertion
			RcvDataBufferHdr->Size =
			    SXG_RCV_BUFFER_DATA_SIZE(BufferSize);

			SXG_ALLOCATE_RCV_PACKET(adapter, RcvDataBufferHdr);
			if (RcvDataBufferHdr->SxgDumbRcvPacket == NULL)
				goto fail;

		}

	// Place this entire block of memory on the AllRcvBlocks queue so it can be
	// free later
	RcvBlockHdr =
	    (PSXG_RCV_BLOCK_HDR) ((unsigned char *)RcvBlock +
				  SXG_RCV_BLOCK_HDR_OFFSET(BufferSize));
	RcvBlockHdr->VirtualAddress = RcvBlock;
	RcvBlockHdr->PhysicalAddress = PhysicalAddress;
	spin_lock(&adapter->RcvQLock);
	adapter->AllRcvBlockCount++;
	InsertTailList(&adapter->AllRcvBlocks, &RcvBlockHdr->AllList);
	spin_unlock(&adapter->RcvQLock);

	// Now free the contained receive data buffers that we initialized above
	RcvDataBuffer = RcvBlock;
	for (i = 0, Paddr = PhysicalAddress;
	     i < SXG_RCV_DESCRIPTORS_PER_BLOCK;
	     i++, Paddr += BufferSize, RcvDataBuffer += BufferSize) {
		RcvDataBufferHdr = (PSXG_RCV_DATA_BUFFER_HDR) (RcvDataBuffer +
							       SXG_RCV_DATA_BUFFER_HDR_OFFSET
							       (BufferSize));
		spin_lock(&adapter->RcvQLock);
		SXG_FREE_RCV_DATA_BUFFER(adapter, RcvDataBufferHdr);
		spin_unlock(&adapter->RcvQLock);
	}

	// Locate the descriptor block and put it on a separate free queue
	RcvDescriptorBlock = (PSXG_RCV_DESCRIPTOR_BLOCK) ((unsigned char *)RcvBlock +
							  SXG_RCV_DESCRIPTOR_BLOCK_OFFSET
							  (BufferSize));
	RcvDescriptorBlockHdr =
	    (PSXG_RCV_DESCRIPTOR_BLOCK_HDR) ((unsigned char *)RcvBlock +
					     SXG_RCV_DESCRIPTOR_BLOCK_HDR_OFFSET
					     (BufferSize));
	RcvDescriptorBlockHdr->VirtualAddress = RcvDescriptorBlock;
	RcvDescriptorBlockHdr->PhysicalAddress = Paddr;
	spin_lock(&adapter->RcvQLock);
	SXG_FREE_RCV_DESCRIPTOR_BLOCK(adapter, RcvDescriptorBlockHdr);
	spin_unlock(&adapter->RcvQLock);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAlRBlk",
		  adapter, RcvBlock, Length, 0);
	return;
      fail:
	// Free any allocated resources
	if (RcvBlock) {
		RcvDataBuffer = RcvBlock;
		for (i = 0; i < SXG_RCV_DESCRIPTORS_PER_BLOCK;
		     i++, RcvDataBuffer += BufferSize) {
			RcvDataBufferHdr =
			    (PSXG_RCV_DATA_BUFFER_HDR) (RcvDataBuffer +
							SXG_RCV_DATA_BUFFER_HDR_OFFSET
							(BufferSize));
			SXG_FREE_RCV_PACKET(RcvDataBufferHdr);
		}
		pci_free_consistent(adapter->pcidev,
				    Length, RcvBlock, PhysicalAddress);
	}
	DBG_ERROR("%s: OUT OF RESOURCES\n", __FUNCTION__);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_IMPORTANT, "RcvAFail",
		  adapter, adapter->FreeRcvBufferCount,
		  adapter->FreeRcvBlockCount, adapter->AllRcvBlockCount);
	adapter->Stats.NoMem++;
}

/*
 * sxg_allocate_sgl_buffer_complete - Complete a SGL buffer allocation
 *
 * Arguments -
 *	adapter				- A pointer to our adapter structure
 *	SxgSgl				- SXG_SCATTER_GATHER buffer
 *	PhysicalAddress		- Physical address
 *	Length				- Memory length
 *
 * Return
 *
 */
static void sxg_allocate_sgl_buffer_complete(p_adapter_t adapter,
				 PSXG_SCATTER_GATHER SxgSgl,
				 dma_addr_t PhysicalAddress, u32 Length)
{
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "AlSglCmp",
		  adapter, SxgSgl, Length, 0);
	spin_lock(&adapter->SglQLock);
	adapter->AllSglBufferCount++;
	memset(SxgSgl, 0, sizeof(SXG_SCATTER_GATHER));
	SxgSgl->PhysicalAddress = PhysicalAddress;	/* *PhysicalAddress; */
	SxgSgl->adapter = adapter;	// Initialize backpointer once
	InsertTailList(&adapter->AllSglBuffers, &SxgSgl->AllList);
	spin_unlock(&adapter->SglQLock);
	SxgSgl->State = SXG_BUFFER_BUSY;
	SXG_FREE_SGL_BUFFER(adapter, SxgSgl, NULL);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XAlSgl",
		  adapter, SxgSgl, Length, 0);
}

static unsigned char temp_mac_address[6] = { 0x00, 0xab, 0xcd, 0xef, 0x12, 0x69 };

static void sxg_adapter_set_hwaddr(p_adapter_t adapter)
{
//  DBG_ERROR ("%s ENTER card->config_set[%x] port[%d] physport[%d] funct#[%d]\n", __FUNCTION__,
//             card->config_set, adapter->port, adapter->physport, adapter->functionnumber);
//
//  sxg_dbg_macaddrs(adapter);

	memcpy(adapter->macaddr, temp_mac_address, sizeof(SXG_CONFIG_MAC));
//      DBG_ERROR ("%s AFTER copying from config.macinfo into currmacaddr\n", __FUNCTION__);
//      sxg_dbg_macaddrs(adapter);
	if (!(adapter->currmacaddr[0] ||
	      adapter->currmacaddr[1] ||
	      adapter->currmacaddr[2] ||
	      adapter->currmacaddr[3] ||
	      adapter->currmacaddr[4] || adapter->currmacaddr[5])) {
		memcpy(adapter->currmacaddr, adapter->macaddr, 6);
	}
	if (adapter->netdev) {
		memcpy(adapter->netdev->dev_addr, adapter->currmacaddr, 6);
	}
//  DBG_ERROR ("%s EXIT port %d\n", __FUNCTION__, adapter->port);
	sxg_dbg_macaddrs(adapter);

}

static int sxg_mac_set_address(p_net_device dev, void * ptr)
{
#if XXXTODO
	p_adapter_t adapter = (p_adapter_t) netdev_priv(dev);
	struct sockaddr *addr = ptr;

	DBG_ERROR("%s ENTER (%s)\n", __FUNCTION__, adapter->netdev->name);

	if (netif_running(dev)) {
		return -EBUSY;
	}
	if (!adapter) {
		return -EBUSY;
	}
	DBG_ERROR("sxg: %s (%s) curr %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		  __FUNCTION__, adapter->netdev->name, adapter->currmacaddr[0],
		  adapter->currmacaddr[1], adapter->currmacaddr[2],
		  adapter->currmacaddr[3], adapter->currmacaddr[4],
		  adapter->currmacaddr[5]);
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	memcpy(adapter->currmacaddr, addr->sa_data, dev->addr_len);
	DBG_ERROR("sxg: %s (%s) new %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		  __FUNCTION__, adapter->netdev->name, adapter->currmacaddr[0],
		  adapter->currmacaddr[1], adapter->currmacaddr[2],
		  adapter->currmacaddr[3], adapter->currmacaddr[4],
		  adapter->currmacaddr[5]);

	sxg_config_set(adapter, TRUE);
#endif
	return 0;
}

/*****************************************************************************/
/*************  SXG DRIVER FUNCTIONS  (below) ********************************/
/*****************************************************************************/

/*
 * sxg_initialize_adapter - Initialize adapter
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	int
 */
static int sxg_initialize_adapter(p_adapter_t adapter)
{
	u32 RssIds, IsrCount;
	u32 i;
	int status;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "InitAdpt",
		  adapter, 0, 0, 0);

	RssIds = 1;		//  XXXTODO  SXG_RSS_CPU_COUNT(adapter);
	IsrCount = adapter->MsiEnabled ? RssIds : 1;

	// Sanity check SXG_UCODE_REGS structure definition to
	// make sure the length is correct
	ASSERT(sizeof(SXG_UCODE_REGS) == SXG_REGISTER_SIZE_PER_CPU);

	// Disable interrupts
	SXG_DISABLE_ALL_INTERRUPTS(adapter);

	// Set MTU
	ASSERT((adapter->FrameSize == ETHERMAXFRAME) ||
	       (adapter->FrameSize == JUMBOMAXFRAME));
	WRITE_REG(adapter->UcodeRegs[0].LinkMtu, adapter->FrameSize, TRUE);

	// Set event ring base address and size
	WRITE_REG64(adapter,
		    adapter->UcodeRegs[0].EventBase, adapter->PEventRings, 0);
	WRITE_REG(adapter->UcodeRegs[0].EventSize, EVENT_RING_SIZE, TRUE);

	// Per-ISR initialization
	for (i = 0; i < IsrCount; i++) {
		u64 Addr;
		// Set interrupt status pointer
		Addr = adapter->PIsr + (i * sizeof(u32));
		WRITE_REG64(adapter, adapter->UcodeRegs[i].Isp, Addr, i);
	}

	// XMT ring zero index
	WRITE_REG64(adapter,
		    adapter->UcodeRegs[0].SPSendIndex,
		    adapter->PXmtRingZeroIndex, 0);

	// Per-RSS initialization
	for (i = 0; i < RssIds; i++) {
		// Release all event ring entries to the Microcode
		WRITE_REG(adapter->UcodeRegs[i].EventRelease, EVENT_RING_SIZE,
			  TRUE);
	}

	// Transmit ring base and size
	WRITE_REG64(adapter,
		    adapter->UcodeRegs[0].XmtBase, adapter->PXmtRings, 0);
	WRITE_REG(adapter->UcodeRegs[0].XmtSize, SXG_XMT_RING_SIZE, TRUE);

	// Receive ring base and size
	WRITE_REG64(adapter,
		    adapter->UcodeRegs[0].RcvBase, adapter->PRcvRings, 0);
	WRITE_REG(adapter->UcodeRegs[0].RcvSize, SXG_RCV_RING_SIZE, TRUE);

	// Populate the card with receive buffers
	sxg_stock_rcv_buffers(adapter);

	// Initialize checksum offload capabilities.  At the moment
	// we always enable IP and TCP receive checksums on the card.
	// Depending on the checksum configuration specified by the
	// user, we can choose to report or ignore the checksum
	// information provided by the card.
	WRITE_REG(adapter->UcodeRegs[0].ReceiveChecksum,
		  SXG_RCV_TCP_CSUM_ENABLED | SXG_RCV_IP_CSUM_ENABLED, TRUE);

	// Initialize the MAC, XAUI
	DBG_ERROR("sxg: %s ENTER sxg_initialize_link\n", __FUNCTION__);
	status = sxg_initialize_link(adapter);
	DBG_ERROR("sxg: %s EXIT sxg_initialize_link status[%x]\n", __FUNCTION__,
		  status);
	if (status != STATUS_SUCCESS) {
		return (status);
	}
	// Initialize Dead to FALSE.
	// SlicCheckForHang or SlicDumpThread will take it from here.
	adapter->Dead = FALSE;
	adapter->PingOutstanding = FALSE;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XInit",
		  adapter, 0, 0, 0);
	return (STATUS_SUCCESS);
}

/*
 * sxg_fill_descriptor_block - Populate a descriptor block and give it to
 * the card.  The caller should hold the RcvQLock
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *  RcvDescriptorBlockHdr	- Descriptor block to fill
 *
 * Return
 *	status
 */
static int sxg_fill_descriptor_block(p_adapter_t adapter,
			  PSXG_RCV_DESCRIPTOR_BLOCK_HDR RcvDescriptorBlockHdr)
{
	u32 i;
	PSXG_RING_INFO RcvRingInfo = &adapter->RcvRingZeroInfo;
	PSXG_RCV_DATA_BUFFER_HDR RcvDataBufferHdr;
	PSXG_RCV_DESCRIPTOR_BLOCK RcvDescriptorBlock;
	PSXG_CMD RingDescriptorCmd;
	PSXG_RCV_RING RingZero = &adapter->RcvRings[0];

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "FilBlk",
		  adapter, adapter->RcvBuffersOnCard,
		  adapter->FreeRcvBufferCount, adapter->AllRcvBlockCount);

	ASSERT(RcvDescriptorBlockHdr);

	// If we don't have the resources to fill the descriptor block,
	// return failure
	if ((adapter->FreeRcvBufferCount < SXG_RCV_DESCRIPTORS_PER_BLOCK) ||
	    SXG_RING_FULL(RcvRingInfo)) {
		adapter->Stats.NoMem++;
		return (STATUS_FAILURE);
	}
	// Get a ring descriptor command
	SXG_GET_CMD(RingZero,
		    RcvRingInfo, RingDescriptorCmd, RcvDescriptorBlockHdr);
	ASSERT(RingDescriptorCmd);
	RcvDescriptorBlockHdr->State = SXG_BUFFER_ONCARD;
	RcvDescriptorBlock =
	    (PSXG_RCV_DESCRIPTOR_BLOCK) RcvDescriptorBlockHdr->VirtualAddress;

	// Fill in the descriptor block
	for (i = 0; i < SXG_RCV_DESCRIPTORS_PER_BLOCK; i++) {
		SXG_GET_RCV_DATA_BUFFER(adapter, RcvDataBufferHdr);
		ASSERT(RcvDataBufferHdr);
		SXG_REINIATIALIZE_PACKET(RcvDataBufferHdr->SxgDumbRcvPacket);
		RcvDataBufferHdr->State = SXG_BUFFER_ONCARD;
		RcvDescriptorBlock->Descriptors[i].VirtualAddress = (void *)RcvDataBufferHdr;
		RcvDescriptorBlock->Descriptors[i].PhysicalAddress =
		    RcvDataBufferHdr->PhysicalAddress;
	}
	// Add the descriptor block to receive descriptor ring 0
	RingDescriptorCmd->Sgl = RcvDescriptorBlockHdr->PhysicalAddress;

	// RcvBuffersOnCard is not protected via the receive lock (see
	// sxg_process_event_queue) We don't want to grap a lock every time a
	// buffer is returned to us, so we use atomic interlocked functions
	// instead.
	adapter->RcvBuffersOnCard += SXG_RCV_DESCRIPTORS_PER_BLOCK;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "DscBlk",
		  RcvDescriptorBlockHdr,
		  RingDescriptorCmd, RcvRingInfo->Head, RcvRingInfo->Tail);

	WRITE_REG(adapter->UcodeRegs[0].RcvCmd, 1, true);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XFilBlk",
		  adapter, adapter->RcvBuffersOnCard,
		  adapter->FreeRcvBufferCount, adapter->AllRcvBlockCount);
	return (STATUS_SUCCESS);
}

/*
 * sxg_stock_rcv_buffers - Stock the card with receive buffers
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *
 * Return
 *	None
 */
static void sxg_stock_rcv_buffers(p_adapter_t adapter)
{
	PSXG_RCV_DESCRIPTOR_BLOCK_HDR RcvDescriptorBlockHdr;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "StockBuf",
		  adapter, adapter->RcvBuffersOnCard,
		  adapter->FreeRcvBufferCount, adapter->AllRcvBlockCount);
	// First, see if we've got less than our minimum threshold of
	// receive buffers, there isn't an allocation in progress, and
	// we haven't exceeded our maximum.. get another block of buffers
	// None of this needs to be SMP safe.  It's round numbers.
	if ((adapter->FreeRcvBufferCount < SXG_MIN_RCV_DATA_BUFFERS) &&
	    (adapter->AllRcvBlockCount < SXG_MAX_RCV_BLOCKS) &&
	    (adapter->AllocationsPending == 0)) {
		sxg_allocate_buffer_memory(adapter,
					   SXG_RCV_BLOCK_SIZE(adapter->
							      ReceiveBufferSize),
					   SXG_BUFFER_TYPE_RCV);
	}
	// Now grab the RcvQLock lock and proceed
	spin_lock(&adapter->RcvQLock);
	while (adapter->RcvBuffersOnCard < SXG_RCV_DATA_BUFFERS) {
		PLIST_ENTRY _ple;

		// Get a descriptor block
		RcvDescriptorBlockHdr = NULL;
		if (adapter->FreeRcvBlockCount) {
			_ple = RemoveHeadList(&adapter->FreeRcvBlocks);
			RcvDescriptorBlockHdr = container_of(_ple, SXG_RCV_DESCRIPTOR_BLOCK_HDR, FreeList);
			adapter->FreeRcvBlockCount--;
			RcvDescriptorBlockHdr->State = SXG_BUFFER_BUSY;
		}

		if (RcvDescriptorBlockHdr == NULL) {
			// Bail out..
			adapter->Stats.NoMem++;
			break;
		}
		// Fill in the descriptor block and give it to the card
		if (sxg_fill_descriptor_block(adapter, RcvDescriptorBlockHdr) ==
		    STATUS_FAILURE) {
			// Free the descriptor block
			SXG_FREE_RCV_DESCRIPTOR_BLOCK(adapter,
						      RcvDescriptorBlockHdr);
			break;
		}
	}
	spin_unlock(&adapter->RcvQLock);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XFilBlks",
		  adapter, adapter->RcvBuffersOnCard,
		  adapter->FreeRcvBufferCount, adapter->AllRcvBlockCount);
}

/*
 * sxg_complete_descriptor_blocks - Return descriptor blocks that have been
 * completed by the microcode
 *
 * Arguments -
 *	adapter		- A pointer to our adapter structure
 *	Index		- Where the microcode is up to
 *
 * Return
 *	None
 */
static void sxg_complete_descriptor_blocks(p_adapter_t adapter, unsigned char Index)
{
	PSXG_RCV_RING RingZero = &adapter->RcvRings[0];
	PSXG_RING_INFO RcvRingInfo = &adapter->RcvRingZeroInfo;
	PSXG_RCV_DESCRIPTOR_BLOCK_HDR RcvDescriptorBlockHdr;
	PSXG_CMD RingDescriptorCmd;

	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "CmpRBlks",
		  adapter, Index, RcvRingInfo->Head, RcvRingInfo->Tail);

	// Now grab the RcvQLock lock and proceed
	spin_lock(&adapter->RcvQLock);
	ASSERT(Index != RcvRingInfo->Tail);
	while (RcvRingInfo->Tail != Index) {
		//
		// Locate the current Cmd (ring descriptor entry), and
		// associated receive descriptor block, and advance
		// the tail
		//
		SXG_RETURN_CMD(RingZero,
			       RcvRingInfo,
			       RingDescriptorCmd, RcvDescriptorBlockHdr);
		SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "CmpRBlk",
			  RcvRingInfo->Head, RcvRingInfo->Tail,
			  RingDescriptorCmd, RcvDescriptorBlockHdr);

		// Clear the SGL field
		RingDescriptorCmd->Sgl = 0;
		// Attempt to refill it and hand it right back to the
		// card.  If we fail to refill it, free the descriptor block
		// header.  The card will be restocked later via the
		// RcvBuffersOnCard test
		if (sxg_fill_descriptor_block(adapter, RcvDescriptorBlockHdr) ==
		    STATUS_FAILURE) {
			SXG_FREE_RCV_DESCRIPTOR_BLOCK(adapter,
						      RcvDescriptorBlockHdr);
		}
	}
	spin_unlock(&adapter->RcvQLock);
	SXG_TRACE(TRACE_SXG, SxgTraceBuffer, TRACE_NOISY, "XCRBlks",
		  adapter, Index, RcvRingInfo->Head, RcvRingInfo->Tail);
}


static struct pci_driver sxg_driver = {
	.name = DRV_NAME,
	.id_table = sxg_pci_tbl,
	.probe = sxg_entry_probe,
	.remove = sxg_entry_remove,
#if SXG_POWER_MANAGEMENT_ENABLED
	.suspend = sxgpm_suspend,
	.resume = sxgpm_resume,
#endif
/*    .shutdown   =     slic_shutdown,  MOOK_INVESTIGATE */
};

static int __init sxg_module_init(void)
{
	sxg_init_driver();

	if (debug >= 0)
		sxg_debug = debug;

	return pci_register_driver(&sxg_driver);
}

static void __exit sxg_module_cleanup(void)
{
	pci_unregister_driver(&sxg_driver);
}

module_init(sxg_module_init);
module_exit(sxg_module_cleanup);
