
/*

  Linux Driver for BusLogic MultiMaster and FlashPoint SCSI Host Adapters

  Copyright 1995-1998 by Leonard N. Zubkoff <lnz@dandelion.com>

  This program is free software; you may redistribute and/or modify it under
  the terms of the GNU General Public License Version 2 as published by the
  Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for complete details.

  The author respectfully requests that any modifications to this software be
  sent directly to him for evaluation and testing.

  Special thanks to Wayne Yen, Jin-Lon Hon, and Alex Win of BusLogic, whose
  advice has been invaluable, to David Gentzel, for writing the original Linux
  BusLogic driver, and to Paul Gortmaker, for being such a dedicated test site.

  Finally, special thanks to Mylex/BusLogic for making the FlashPoint SCCB
  Manager available as freely redistributable source code.

*/

#define BusLogic_DriverVersion		"2.1.16"
#define BusLogic_DriverDate		"18 July 2002"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/dma-mapping.h>
#include <scsi/scsicam.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include "BusLogic.h"
#include "FlashPoint.c"

#ifndef FAILURE
#define FAILURE (-1)
#endif

static struct scsi_host_template Bus_Logic_template;

/*
  BusLogic_DriverOptionsCount is a count of the number of BusLogic Driver
  Options specifications provided via the Linux Kernel Command Line or via
  the Loadable Kernel Module Installation Facility.
*/

static int BusLogic_DriverOptionsCount;


/*
  BusLogic_DriverOptions is an array of Driver Options structures representing
  BusLogic Driver Options specifications provided via the Linux Kernel Command
  Line or via the Loadable Kernel Module Installation Facility.
*/

static struct BusLogic_DriverOptions BusLogic_DriverOptions[BusLogic_MaxHostAdapters];


/*
  BusLogic can be assigned a string by insmod.
*/

MODULE_LICENSE("GPL");
#ifdef MODULE
static char *BusLogic;
module_param(BusLogic, charp, 0);
#endif


/*
  BusLogic_ProbeOptions is a set of Probe Options to be applied across
  all BusLogic Host Adapters.
*/

static struct BusLogic_ProbeOptions BusLogic_ProbeOptions;


/*
  BusLogic_GlobalOptions is a set of Global Options to be applied across
  all BusLogic Host Adapters.
*/

static struct BusLogic_GlobalOptions BusLogic_GlobalOptions;

static LIST_HEAD(BusLogic_host_list);

/*
  BusLogic_ProbeInfoCount is the number of entries in BusLogic_ProbeInfoList.
*/

static int BusLogic_ProbeInfoCount;


/*
  BusLogic_ProbeInfoList is the list of I/O Addresses and Bus Probe Information
  to be checked for potential BusLogic Host Adapters.  It is initialized by
  interrogating the PCI Configuration Space on PCI machines as well as from the
  list of standard BusLogic I/O Addresses.
*/

static struct BusLogic_ProbeInfo *BusLogic_ProbeInfoList;


/*
  BusLogic_CommandFailureReason holds a string identifying the reason why a
  call to BusLogic_Command failed.  It is only non-NULL when BusLogic_Command
  returns a failure code.
*/

static char *BusLogic_CommandFailureReason;

/*
  BusLogic_AnnounceDriver announces the Driver Version and Date, Author's
  Name, Copyright Notice, and Electronic Mail Address.
*/

static void BusLogic_AnnounceDriver(struct BusLogic_HostAdapter *HostAdapter)
{
	BusLogic_Announce("***** BusLogic SCSI Driver Version " BusLogic_DriverVersion " of " BusLogic_DriverDate " *****\n", HostAdapter);
	BusLogic_Announce("Copyright 1995-1998 by Leonard N. Zubkoff " "<lnz@dandelion.com>\n", HostAdapter);
}


/*
  BusLogic_DriverInfo returns the Host Adapter Name to identify this SCSI
  Driver and Host Adapter.
*/

static const char *BusLogic_DriverInfo(struct Scsi_Host *Host)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) Host->hostdata;
	return HostAdapter->FullModelName;
}

/*
  BusLogic_InitializeCCBs initializes a group of Command Control Blocks (CCBs)
  for Host Adapter from the BlockSize bytes located at BlockPointer.  The newly
  created CCBs are added to Host Adapter's free list.
*/

static void BusLogic_InitializeCCBs(struct BusLogic_HostAdapter *HostAdapter, void *BlockPointer, int BlockSize, dma_addr_t BlockPointerHandle)
{
	struct BusLogic_CCB *CCB = (struct BusLogic_CCB *) BlockPointer;
	unsigned int offset = 0;
	memset(BlockPointer, 0, BlockSize);
	CCB->AllocationGroupHead = BlockPointerHandle;
	CCB->AllocationGroupSize = BlockSize;
	while ((BlockSize -= sizeof(struct BusLogic_CCB)) >= 0) {
		CCB->Status = BusLogic_CCB_Free;
		CCB->HostAdapter = HostAdapter;
		CCB->DMA_Handle = (u32) BlockPointerHandle + offset;
		if (BusLogic_FlashPointHostAdapterP(HostAdapter)) {
			CCB->CallbackFunction = BusLogic_QueueCompletedCCB;
			CCB->BaseAddress = HostAdapter->FlashPointInfo.BaseAddress;
		}
		CCB->Next = HostAdapter->Free_CCBs;
		CCB->NextAll = HostAdapter->All_CCBs;
		HostAdapter->Free_CCBs = CCB;
		HostAdapter->All_CCBs = CCB;
		HostAdapter->AllocatedCCBs++;
		CCB++;
		offset += sizeof(struct BusLogic_CCB);
	}
}


/*
  BusLogic_CreateInitialCCBs allocates the initial CCBs for Host Adapter.
*/

static bool __init BusLogic_CreateInitialCCBs(struct BusLogic_HostAdapter *HostAdapter)
{
	int BlockSize = BusLogic_CCB_AllocationGroupSize * sizeof(struct BusLogic_CCB);
	void *BlockPointer;
	dma_addr_t BlockPointerHandle;
	while (HostAdapter->AllocatedCCBs < HostAdapter->InitialCCBs) {
		BlockPointer = pci_alloc_consistent(HostAdapter->PCI_Device, BlockSize, &BlockPointerHandle);
		if (BlockPointer == NULL) {
			BusLogic_Error("UNABLE TO ALLOCATE CCB GROUP - DETACHING\n", HostAdapter);
			return false;
		}
		BusLogic_InitializeCCBs(HostAdapter, BlockPointer, BlockSize, BlockPointerHandle);
	}
	return true;
}


/*
  BusLogic_DestroyCCBs deallocates the CCBs for Host Adapter.
*/

static void BusLogic_DestroyCCBs(struct BusLogic_HostAdapter *HostAdapter)
{
	struct BusLogic_CCB *NextCCB = HostAdapter->All_CCBs, *CCB, *Last_CCB = NULL;
	HostAdapter->All_CCBs = NULL;
	HostAdapter->Free_CCBs = NULL;
	while ((CCB = NextCCB) != NULL) {
		NextCCB = CCB->NextAll;
		if (CCB->AllocationGroupHead) {
			if (Last_CCB)
				pci_free_consistent(HostAdapter->PCI_Device, Last_CCB->AllocationGroupSize, Last_CCB, Last_CCB->AllocationGroupHead);
			Last_CCB = CCB;
		}
	}
	if (Last_CCB)
		pci_free_consistent(HostAdapter->PCI_Device, Last_CCB->AllocationGroupSize, Last_CCB, Last_CCB->AllocationGroupHead);
}


/*
  BusLogic_CreateAdditionalCCBs allocates Additional CCBs for Host Adapter.  If
  allocation fails and there are no remaining CCBs available, the Driver Queue
  Depth is decreased to a known safe value to avoid potential deadlocks when
  multiple host adapters share the same IRQ Channel.
*/

static void BusLogic_CreateAdditionalCCBs(struct BusLogic_HostAdapter *HostAdapter, int AdditionalCCBs, bool SuccessMessageP)
{
	int BlockSize = BusLogic_CCB_AllocationGroupSize * sizeof(struct BusLogic_CCB);
	int PreviouslyAllocated = HostAdapter->AllocatedCCBs;
	void *BlockPointer;
	dma_addr_t BlockPointerHandle;
	if (AdditionalCCBs <= 0)
		return;
	while (HostAdapter->AllocatedCCBs - PreviouslyAllocated < AdditionalCCBs) {
		BlockPointer = pci_alloc_consistent(HostAdapter->PCI_Device, BlockSize, &BlockPointerHandle);
		if (BlockPointer == NULL)
			break;
		BusLogic_InitializeCCBs(HostAdapter, BlockPointer, BlockSize, BlockPointerHandle);
	}
	if (HostAdapter->AllocatedCCBs > PreviouslyAllocated) {
		if (SuccessMessageP)
			BusLogic_Notice("Allocated %d additional CCBs (total now %d)\n", HostAdapter, HostAdapter->AllocatedCCBs - PreviouslyAllocated, HostAdapter->AllocatedCCBs);
		return;
	}
	BusLogic_Notice("Failed to allocate additional CCBs\n", HostAdapter);
	if (HostAdapter->DriverQueueDepth > HostAdapter->AllocatedCCBs - HostAdapter->TargetDeviceCount) {
		HostAdapter->DriverQueueDepth = HostAdapter->AllocatedCCBs - HostAdapter->TargetDeviceCount;
		HostAdapter->SCSI_Host->can_queue = HostAdapter->DriverQueueDepth;
	}
}

/*
  BusLogic_AllocateCCB allocates a CCB from Host Adapter's free list,
  allocating more memory from the Kernel if necessary.  The Host Adapter's
  Lock should already have been acquired by the caller.
*/

static struct BusLogic_CCB *BusLogic_AllocateCCB(struct BusLogic_HostAdapter
						 *HostAdapter)
{
	static unsigned long SerialNumber = 0;
	struct BusLogic_CCB *CCB;
	CCB = HostAdapter->Free_CCBs;
	if (CCB != NULL) {
		CCB->SerialNumber = ++SerialNumber;
		HostAdapter->Free_CCBs = CCB->Next;
		CCB->Next = NULL;
		if (HostAdapter->Free_CCBs == NULL)
			BusLogic_CreateAdditionalCCBs(HostAdapter, HostAdapter->IncrementalCCBs, true);
		return CCB;
	}
	BusLogic_CreateAdditionalCCBs(HostAdapter, HostAdapter->IncrementalCCBs, true);
	CCB = HostAdapter->Free_CCBs;
	if (CCB == NULL)
		return NULL;
	CCB->SerialNumber = ++SerialNumber;
	HostAdapter->Free_CCBs = CCB->Next;
	CCB->Next = NULL;
	return CCB;
}


/*
  BusLogic_DeallocateCCB deallocates a CCB, returning it to the Host Adapter's
  free list.  The Host Adapter's Lock should already have been acquired by the
  caller.
*/

static void BusLogic_DeallocateCCB(struct BusLogic_CCB *CCB)
{
	struct BusLogic_HostAdapter *HostAdapter = CCB->HostAdapter;

	scsi_dma_unmap(CCB->Command);
	pci_unmap_single(HostAdapter->PCI_Device, CCB->SenseDataPointer,
			 CCB->SenseDataLength, PCI_DMA_FROMDEVICE);

	CCB->Command = NULL;
	CCB->Status = BusLogic_CCB_Free;
	CCB->Next = HostAdapter->Free_CCBs;
	HostAdapter->Free_CCBs = CCB;
}


/*
  BusLogic_Command sends the command OperationCode to HostAdapter, optionally
  providing ParameterLength bytes of ParameterData and receiving at most
  ReplyLength bytes of ReplyData; any excess reply data is received but
  discarded.

  On success, this function returns the number of reply bytes read from
  the Host Adapter (including any discarded data); on failure, it returns
  -1 if the command was invalid, or -2 if a timeout occurred.

  BusLogic_Command is called exclusively during host adapter detection and
  initialization, so performance and latency are not critical, and exclusive
  access to the Host Adapter hardware is assumed.  Once the host adapter and
  driver are initialized, the only Host Adapter command that is issued is the
  single byte Execute Mailbox Command operation code, which does not require
  waiting for the Host Adapter Ready bit to be set in the Status Register.
*/

static int BusLogic_Command(struct BusLogic_HostAdapter *HostAdapter, enum BusLogic_OperationCode OperationCode, void *ParameterData, int ParameterLength, void *ReplyData, int ReplyLength)
{
	unsigned char *ParameterPointer = (unsigned char *) ParameterData;
	unsigned char *ReplyPointer = (unsigned char *) ReplyData;
	union BusLogic_StatusRegister StatusRegister;
	union BusLogic_InterruptRegister InterruptRegister;
	unsigned long ProcessorFlags = 0;
	int ReplyBytes = 0, Result;
	long TimeoutCounter;
	/*
	   Clear out the Reply Data if provided.
	 */
	if (ReplyLength > 0)
		memset(ReplyData, 0, ReplyLength);
	/*
	   If the IRQ Channel has not yet been acquired, then interrupts must be
	   disabled while issuing host adapter commands since a Command Complete
	   interrupt could occur if the IRQ Channel was previously enabled by another
	   BusLogic Host Adapter or another driver sharing the same IRQ Channel.
	 */
	if (!HostAdapter->IRQ_ChannelAcquired)
		local_irq_save(ProcessorFlags);
	/*
	   Wait for the Host Adapter Ready bit to be set and the Command/Parameter
	   Register Busy bit to be reset in the Status Register.
	 */
	TimeoutCounter = 10000;
	while (--TimeoutCounter >= 0) {
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (StatusRegister.sr.HostAdapterReady && !StatusRegister.sr.CommandParameterRegisterBusy)
			break;
		udelay(100);
	}
	if (TimeoutCounter < 0) {
		BusLogic_CommandFailureReason = "Timeout waiting for Host Adapter Ready";
		Result = -2;
		goto Done;
	}
	/*
	   Write the OperationCode to the Command/Parameter Register.
	 */
	HostAdapter->HostAdapterCommandCompleted = false;
	BusLogic_WriteCommandParameterRegister(HostAdapter, OperationCode);
	/*
	   Write any additional Parameter Bytes.
	 */
	TimeoutCounter = 10000;
	while (ParameterLength > 0 && --TimeoutCounter >= 0) {
		/*
		   Wait 100 microseconds to give the Host Adapter enough time to determine
		   whether the last value written to the Command/Parameter Register was
		   valid or not.  If the Command Complete bit is set in the Interrupt
		   Register, then the Command Invalid bit in the Status Register will be
		   reset if the Operation Code or Parameter was valid and the command
		   has completed, or set if the Operation Code or Parameter was invalid.
		   If the Data In Register Ready bit is set in the Status Register, then
		   the Operation Code was valid, and data is waiting to be read back
		   from the Host Adapter.  Otherwise, wait for the Command/Parameter
		   Register Busy bit in the Status Register to be reset.
		 */
		udelay(100);
		InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (InterruptRegister.ir.CommandComplete)
			break;
		if (HostAdapter->HostAdapterCommandCompleted)
			break;
		if (StatusRegister.sr.DataInRegisterReady)
			break;
		if (StatusRegister.sr.CommandParameterRegisterBusy)
			continue;
		BusLogic_WriteCommandParameterRegister(HostAdapter, *ParameterPointer++);
		ParameterLength--;
	}
	if (TimeoutCounter < 0) {
		BusLogic_CommandFailureReason = "Timeout waiting for Parameter Acceptance";
		Result = -2;
		goto Done;
	}
	/*
	   The Modify I/O Address command does not cause a Command Complete Interrupt.
	 */
	if (OperationCode == BusLogic_ModifyIOAddress) {
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (StatusRegister.sr.CommandInvalid) {
			BusLogic_CommandFailureReason = "Modify I/O Address Invalid";
			Result = -1;
			goto Done;
		}
		if (BusLogic_GlobalOptions.TraceConfiguration)
			BusLogic_Notice("BusLogic_Command(%02X) Status = %02X: " "(Modify I/O Address)\n", HostAdapter, OperationCode, StatusRegister.All);
		Result = 0;
		goto Done;
	}
	/*
	   Select an appropriate timeout value for awaiting command completion.
	 */
	switch (OperationCode) {
	case BusLogic_InquireInstalledDevicesID0to7:
	case BusLogic_InquireInstalledDevicesID8to15:
	case BusLogic_InquireTargetDevices:
		/* Approximately 60 seconds. */
		TimeoutCounter = 60 * 10000;
		break;
	default:
		/* Approximately 1 second. */
		TimeoutCounter = 10000;
		break;
	}
	/*
	   Receive any Reply Bytes, waiting for either the Command Complete bit to
	   be set in the Interrupt Register, or for the Interrupt Handler to set the
	   Host Adapter Command Completed bit in the Host Adapter structure.
	 */
	while (--TimeoutCounter >= 0) {
		InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (InterruptRegister.ir.CommandComplete)
			break;
		if (HostAdapter->HostAdapterCommandCompleted)
			break;
		if (StatusRegister.sr.DataInRegisterReady) {
			if (++ReplyBytes <= ReplyLength)
				*ReplyPointer++ = BusLogic_ReadDataInRegister(HostAdapter);
			else
				BusLogic_ReadDataInRegister(HostAdapter);
		}
		if (OperationCode == BusLogic_FetchHostAdapterLocalRAM && StatusRegister.sr.HostAdapterReady)
			break;
		udelay(100);
	}
	if (TimeoutCounter < 0) {
		BusLogic_CommandFailureReason = "Timeout waiting for Command Complete";
		Result = -2;
		goto Done;
	}
	/*
	   Clear any pending Command Complete Interrupt.
	 */
	BusLogic_InterruptReset(HostAdapter);
	/*
	   Provide tracing information if requested.
	 */
	if (BusLogic_GlobalOptions.TraceConfiguration) {
		int i;
		BusLogic_Notice("BusLogic_Command(%02X) Status = %02X: %2d ==> %2d:", HostAdapter, OperationCode, StatusRegister.All, ReplyLength, ReplyBytes);
		if (ReplyLength > ReplyBytes)
			ReplyLength = ReplyBytes;
		for (i = 0; i < ReplyLength; i++)
			BusLogic_Notice(" %02X", HostAdapter, ((unsigned char *) ReplyData)[i]);
		BusLogic_Notice("\n", HostAdapter);
	}
	/*
	   Process Command Invalid conditions.
	 */
	if (StatusRegister.sr.CommandInvalid) {
		/*
		   Some early BusLogic Host Adapters may not recover properly from
		   a Command Invalid condition, so if this appears to be the case,
		   a Soft Reset is issued to the Host Adapter.  Potentially invalid
		   commands are never attempted after Mailbox Initialization is
		   performed, so there should be no Host Adapter state lost by a
		   Soft Reset in response to a Command Invalid condition.
		 */
		udelay(1000);
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (StatusRegister.sr.CommandInvalid ||
		    StatusRegister.sr.Reserved ||
		    StatusRegister.sr.DataInRegisterReady ||
		    StatusRegister.sr.CommandParameterRegisterBusy || !StatusRegister.sr.HostAdapterReady || !StatusRegister.sr.InitializationRequired || StatusRegister.sr.DiagnosticActive || StatusRegister.sr.DiagnosticFailure) {
			BusLogic_SoftReset(HostAdapter);
			udelay(1000);
		}
		BusLogic_CommandFailureReason = "Command Invalid";
		Result = -1;
		goto Done;
	}
	/*
	   Handle Excess Parameters Supplied conditions.
	 */
	if (ParameterLength > 0) {
		BusLogic_CommandFailureReason = "Excess Parameters Supplied";
		Result = -1;
		goto Done;
	}
	/*
	   Indicate the command completed successfully.
	 */
	BusLogic_CommandFailureReason = NULL;
	Result = ReplyBytes;
	/*
	   Restore the interrupt status if necessary and return.
	 */
      Done:
	if (!HostAdapter->IRQ_ChannelAcquired)
		local_irq_restore(ProcessorFlags);
	return Result;
}


/*
  BusLogic_AppendProbeAddressISA appends a single ISA I/O Address to the list
  of I/O Address and Bus Probe Information to be checked for potential BusLogic
  Host Adapters.
*/

static void __init BusLogic_AppendProbeAddressISA(unsigned long IO_Address)
{
	struct BusLogic_ProbeInfo *ProbeInfo;
	if (BusLogic_ProbeInfoCount >= BusLogic_MaxHostAdapters)
		return;
	ProbeInfo = &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
	ProbeInfo->HostAdapterType = BusLogic_MultiMaster;
	ProbeInfo->HostAdapterBusType = BusLogic_ISA_Bus;
	ProbeInfo->IO_Address = IO_Address;
	ProbeInfo->PCI_Device = NULL;
}


/*
  BusLogic_InitializeProbeInfoListISA initializes the list of I/O Address and
  Bus Probe Information to be checked for potential BusLogic SCSI Host Adapters
  only from the list of standard BusLogic MultiMaster ISA I/O Addresses.
*/

static void __init BusLogic_InitializeProbeInfoListISA(struct BusLogic_HostAdapter
						       *PrototypeHostAdapter)
{
	/*
	   If BusLogic Driver Options specifications requested that ISA Bus Probes
	   be inhibited, do not proceed further.
	 */
	if (BusLogic_ProbeOptions.NoProbeISA)
		return;
	/*
	   Append the list of standard BusLogic MultiMaster ISA I/O Addresses.
	 */
	if (!BusLogic_ProbeOptions.LimitedProbeISA || BusLogic_ProbeOptions.Probe330)
		BusLogic_AppendProbeAddressISA(0x330);
	if (!BusLogic_ProbeOptions.LimitedProbeISA || BusLogic_ProbeOptions.Probe334)
		BusLogic_AppendProbeAddressISA(0x334);
	if (!BusLogic_ProbeOptions.LimitedProbeISA || BusLogic_ProbeOptions.Probe230)
		BusLogic_AppendProbeAddressISA(0x230);
	if (!BusLogic_ProbeOptions.LimitedProbeISA || BusLogic_ProbeOptions.Probe234)
		BusLogic_AppendProbeAddressISA(0x234);
	if (!BusLogic_ProbeOptions.LimitedProbeISA || BusLogic_ProbeOptions.Probe130)
		BusLogic_AppendProbeAddressISA(0x130);
	if (!BusLogic_ProbeOptions.LimitedProbeISA || BusLogic_ProbeOptions.Probe134)
		BusLogic_AppendProbeAddressISA(0x134);
}


#ifdef CONFIG_PCI


/*
  BusLogic_SortProbeInfo sorts a section of BusLogic_ProbeInfoList in order
  of increasing PCI Bus and Device Number.
*/

static void __init BusLogic_SortProbeInfo(struct BusLogic_ProbeInfo *ProbeInfoList, int ProbeInfoCount)
{
	int LastInterchange = ProbeInfoCount - 1, Bound, j;
	while (LastInterchange > 0) {
		Bound = LastInterchange;
		LastInterchange = 0;
		for (j = 0; j < Bound; j++) {
			struct BusLogic_ProbeInfo *ProbeInfo1 = &ProbeInfoList[j];
			struct BusLogic_ProbeInfo *ProbeInfo2 = &ProbeInfoList[j + 1];
			if (ProbeInfo1->Bus > ProbeInfo2->Bus || (ProbeInfo1->Bus == ProbeInfo2->Bus && (ProbeInfo1->Device > ProbeInfo2->Device))) {
				struct BusLogic_ProbeInfo TempProbeInfo;
				memcpy(&TempProbeInfo, ProbeInfo1, sizeof(struct BusLogic_ProbeInfo));
				memcpy(ProbeInfo1, ProbeInfo2, sizeof(struct BusLogic_ProbeInfo));
				memcpy(ProbeInfo2, &TempProbeInfo, sizeof(struct BusLogic_ProbeInfo));
				LastInterchange = j;
			}
		}
	}
}


/*
  BusLogic_InitializeMultiMasterProbeInfo initializes the list of I/O Address
  and Bus Probe Information to be checked for potential BusLogic MultiMaster
  SCSI Host Adapters by interrogating the PCI Configuration Space on PCI
  machines as well as from the list of standard BusLogic MultiMaster ISA
  I/O Addresses.  It returns the number of PCI MultiMaster Host Adapters found.
*/

static int __init BusLogic_InitializeMultiMasterProbeInfo(struct BusLogic_HostAdapter
							  *PrototypeHostAdapter)
{
	struct BusLogic_ProbeInfo *PrimaryProbeInfo = &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount];
	int NonPrimaryPCIMultiMasterIndex = BusLogic_ProbeInfoCount + 1;
	int NonPrimaryPCIMultiMasterCount = 0, PCIMultiMasterCount = 0;
	bool ForceBusDeviceScanningOrder = false;
	bool ForceBusDeviceScanningOrderChecked = false;
	bool StandardAddressSeen[6];
	struct pci_dev *PCI_Device = NULL;
	int i;
	if (BusLogic_ProbeInfoCount >= BusLogic_MaxHostAdapters)
		return 0;
	BusLogic_ProbeInfoCount++;
	for (i = 0; i < 6; i++)
		StandardAddressSeen[i] = false;
	/*
	   Iterate over the MultiMaster PCI Host Adapters.  For each enumerated host
	   adapter, determine whether its ISA Compatible I/O Port is enabled and if
	   so, whether it is assigned the Primary I/O Address.  A host adapter that is
	   assigned the Primary I/O Address will always be the preferred boot device.
	   The MultiMaster BIOS will first recognize a host adapter at the Primary I/O
	   Address, then any other PCI host adapters, and finally any host adapters
	   located at the remaining standard ISA I/O Addresses.  When a PCI host
	   adapter is found with its ISA Compatible I/O Port enabled, a command is
	   issued to disable the ISA Compatible I/O Port, and it is noted that the
	   particular standard ISA I/O Address need not be probed.
	 */
	PrimaryProbeInfo->IO_Address = 0;
	while ((PCI_Device = pci_get_device(PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER, PCI_Device)) != NULL) {
		struct BusLogic_HostAdapter *HostAdapter = PrototypeHostAdapter;
		struct BusLogic_PCIHostAdapterInformation PCIHostAdapterInformation;
		enum BusLogic_ISACompatibleIOPort ModifyIOAddressRequest;
		unsigned char Bus;
		unsigned char Device;
		unsigned int IRQ_Channel;
		unsigned long BaseAddress0;
		unsigned long BaseAddress1;
		unsigned long IO_Address;
		unsigned long PCI_Address;

		if (pci_enable_device(PCI_Device))
			continue;

		if (pci_set_dma_mask(PCI_Device, DMA_BIT_MASK(32) ))
			continue;

		Bus = PCI_Device->bus->number;
		Device = PCI_Device->devfn >> 3;
		IRQ_Channel = PCI_Device->irq;
		IO_Address = BaseAddress0 = pci_resource_start(PCI_Device, 0);
		PCI_Address = BaseAddress1 = pci_resource_start(PCI_Device, 1);

		if (pci_resource_flags(PCI_Device, 0) & IORESOURCE_MEM) {
			BusLogic_Error("BusLogic: Base Address0 0x%X not I/O for " "MultiMaster Host Adapter\n", NULL, BaseAddress0);
			BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n", NULL, Bus, Device, IO_Address);
			continue;
		}
		if (pci_resource_flags(PCI_Device, 1) & IORESOURCE_IO) {
			BusLogic_Error("BusLogic: Base Address1 0x%X not Memory for " "MultiMaster Host Adapter\n", NULL, BaseAddress1);
			BusLogic_Error("at PCI Bus %d Device %d PCI Address 0x%X\n", NULL, Bus, Device, PCI_Address);
			continue;
		}
		if (IRQ_Channel == 0) {
			BusLogic_Error("BusLogic: IRQ Channel %d invalid for " "MultiMaster Host Adapter\n", NULL, IRQ_Channel);
			BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n", NULL, Bus, Device, IO_Address);
			continue;
		}
		if (BusLogic_GlobalOptions.TraceProbe) {
			BusLogic_Notice("BusLogic: PCI MultiMaster Host Adapter " "detected at\n", NULL);
			BusLogic_Notice("BusLogic: PCI Bus %d Device %d I/O Address " "0x%X PCI Address 0x%X\n", NULL, Bus, Device, IO_Address, PCI_Address);
		}
		/*
		   Issue the Inquire PCI Host Adapter Information command to determine
		   the ISA Compatible I/O Port.  If the ISA Compatible I/O Port is
		   known and enabled, note that the particular Standard ISA I/O
		   Address should not be probed.
		 */
		HostAdapter->IO_Address = IO_Address;
		BusLogic_InterruptReset(HostAdapter);
		if (BusLogic_Command(HostAdapter, BusLogic_InquirePCIHostAdapterInformation, NULL, 0, &PCIHostAdapterInformation, sizeof(PCIHostAdapterInformation))
		    == sizeof(PCIHostAdapterInformation)) {
			if (PCIHostAdapterInformation.ISACompatibleIOPort < 6)
				StandardAddressSeen[PCIHostAdapterInformation.ISACompatibleIOPort] = true;
		} else
			PCIHostAdapterInformation.ISACompatibleIOPort = BusLogic_IO_Disable;
		/*
		 * Issue the Modify I/O Address command to disable the ISA Compatible
		 * I/O Port.  On PCI Host Adapters, the Modify I/O Address command
		 * allows modification of the ISA compatible I/O Address that the Host
		 * Adapter responds to; it does not affect the PCI compliant I/O Address
		 * assigned at system initialization.
		 */
		ModifyIOAddressRequest = BusLogic_IO_Disable;
		BusLogic_Command(HostAdapter, BusLogic_ModifyIOAddress, &ModifyIOAddressRequest, sizeof(ModifyIOAddressRequest), NULL, 0);
		/*
		   For the first MultiMaster Host Adapter enumerated, issue the Fetch
		   Host Adapter Local RAM command to read byte 45 of the AutoSCSI area,
		   for the setting of the "Use Bus And Device # For PCI Scanning Seq."
		   option.  Issue the Inquire Board ID command since this option is
		   only valid for the BT-948/958/958D.
		 */
		if (!ForceBusDeviceScanningOrderChecked) {
			struct BusLogic_FetchHostAdapterLocalRAMRequest FetchHostAdapterLocalRAMRequest;
			struct BusLogic_AutoSCSIByte45 AutoSCSIByte45;
			struct BusLogic_BoardID BoardID;
			FetchHostAdapterLocalRAMRequest.ByteOffset = BusLogic_AutoSCSI_BaseOffset + 45;
			FetchHostAdapterLocalRAMRequest.ByteCount = sizeof(AutoSCSIByte45);
			BusLogic_Command(HostAdapter, BusLogic_FetchHostAdapterLocalRAM, &FetchHostAdapterLocalRAMRequest, sizeof(FetchHostAdapterLocalRAMRequest), &AutoSCSIByte45, sizeof(AutoSCSIByte45));
			BusLogic_Command(HostAdapter, BusLogic_InquireBoardID, NULL, 0, &BoardID, sizeof(BoardID));
			if (BoardID.FirmwareVersion1stDigit == '5')
				ForceBusDeviceScanningOrder = AutoSCSIByte45.ForceBusDeviceScanningOrder;
			ForceBusDeviceScanningOrderChecked = true;
		}
		/*
		   Determine whether this MultiMaster Host Adapter has its ISA
		   Compatible I/O Port enabled and is assigned the Primary I/O Address.
		   If it does, then it is the Primary MultiMaster Host Adapter and must
		   be recognized first.  If it does not, then it is added to the list
		   for probing after any Primary MultiMaster Host Adapter is probed.
		 */
		if (PCIHostAdapterInformation.ISACompatibleIOPort == BusLogic_IO_330) {
			PrimaryProbeInfo->HostAdapterType = BusLogic_MultiMaster;
			PrimaryProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
			PrimaryProbeInfo->IO_Address = IO_Address;
			PrimaryProbeInfo->PCI_Address = PCI_Address;
			PrimaryProbeInfo->Bus = Bus;
			PrimaryProbeInfo->Device = Device;
			PrimaryProbeInfo->IRQ_Channel = IRQ_Channel;
			PrimaryProbeInfo->PCI_Device = pci_dev_get(PCI_Device);
			PCIMultiMasterCount++;
		} else if (BusLogic_ProbeInfoCount < BusLogic_MaxHostAdapters) {
			struct BusLogic_ProbeInfo *ProbeInfo = &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
			ProbeInfo->HostAdapterType = BusLogic_MultiMaster;
			ProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
			ProbeInfo->IO_Address = IO_Address;
			ProbeInfo->PCI_Address = PCI_Address;
			ProbeInfo->Bus = Bus;
			ProbeInfo->Device = Device;
			ProbeInfo->IRQ_Channel = IRQ_Channel;
			ProbeInfo->PCI_Device = pci_dev_get(PCI_Device);
			NonPrimaryPCIMultiMasterCount++;
			PCIMultiMasterCount++;
		} else
			BusLogic_Warning("BusLogic: Too many Host Adapters " "detected\n", NULL);
	}
	/*
	   If the AutoSCSI "Use Bus And Device # For PCI Scanning Seq." option is ON
	   for the first enumerated MultiMaster Host Adapter, and if that host adapter
	   is a BT-948/958/958D, then the MultiMaster BIOS will recognize MultiMaster
	   Host Adapters in the order of increasing PCI Bus and Device Number.  In
	   that case, sort the probe information into the same order the BIOS uses.
	   If this option is OFF, then the MultiMaster BIOS will recognize MultiMaster
	   Host Adapters in the order they are enumerated by the PCI BIOS, and hence
	   no sorting is necessary.
	 */
	if (ForceBusDeviceScanningOrder)
		BusLogic_SortProbeInfo(&BusLogic_ProbeInfoList[NonPrimaryPCIMultiMasterIndex], NonPrimaryPCIMultiMasterCount);
	/*
	   If no PCI MultiMaster Host Adapter is assigned the Primary I/O Address,
	   then the Primary I/O Address must be probed explicitly before any PCI
	   host adapters are probed.
	 */
	if (!BusLogic_ProbeOptions.NoProbeISA)
		if (PrimaryProbeInfo->IO_Address == 0 &&
				(!BusLogic_ProbeOptions.LimitedProbeISA ||
				 BusLogic_ProbeOptions.Probe330)) {
			PrimaryProbeInfo->HostAdapterType = BusLogic_MultiMaster;
			PrimaryProbeInfo->HostAdapterBusType = BusLogic_ISA_Bus;
			PrimaryProbeInfo->IO_Address = 0x330;
		}
	/*
	   Append the list of standard BusLogic MultiMaster ISA I/O Addresses,
	   omitting the Primary I/O Address which has already been handled.
	 */
	if (!BusLogic_ProbeOptions.NoProbeISA) {
		if (!StandardAddressSeen[1] &&
				(!BusLogic_ProbeOptions.LimitedProbeISA ||
				 BusLogic_ProbeOptions.Probe334))
			BusLogic_AppendProbeAddressISA(0x334);
		if (!StandardAddressSeen[2] &&
				(!BusLogic_ProbeOptions.LimitedProbeISA ||
				 BusLogic_ProbeOptions.Probe230))
			BusLogic_AppendProbeAddressISA(0x230);
		if (!StandardAddressSeen[3] &&
				(!BusLogic_ProbeOptions.LimitedProbeISA ||
				 BusLogic_ProbeOptions.Probe234))
			BusLogic_AppendProbeAddressISA(0x234);
		if (!StandardAddressSeen[4] &&
				(!BusLogic_ProbeOptions.LimitedProbeISA ||
				 BusLogic_ProbeOptions.Probe130))
			BusLogic_AppendProbeAddressISA(0x130);
		if (!StandardAddressSeen[5] &&
				(!BusLogic_ProbeOptions.LimitedProbeISA ||
				 BusLogic_ProbeOptions.Probe134))
			BusLogic_AppendProbeAddressISA(0x134);
	}
	/*
	   Iterate over the older non-compliant MultiMaster PCI Host Adapters,
	   noting the PCI bus location and assigned IRQ Channel.
	 */
	PCI_Device = NULL;
	while ((PCI_Device = pci_get_device(PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER_NC, PCI_Device)) != NULL) {
		unsigned char Bus;
		unsigned char Device;
		unsigned int IRQ_Channel;
		unsigned long IO_Address;

		if (pci_enable_device(PCI_Device))
			continue;

		if (pci_set_dma_mask(PCI_Device, DMA_BIT_MASK(32)))
			continue;

		Bus = PCI_Device->bus->number;
		Device = PCI_Device->devfn >> 3;
		IRQ_Channel = PCI_Device->irq;
		IO_Address = pci_resource_start(PCI_Device, 0);

		if (IO_Address == 0 || IRQ_Channel == 0)
			continue;
		for (i = 0; i < BusLogic_ProbeInfoCount; i++) {
			struct BusLogic_ProbeInfo *ProbeInfo = &BusLogic_ProbeInfoList[i];
			if (ProbeInfo->IO_Address == IO_Address && ProbeInfo->HostAdapterType == BusLogic_MultiMaster) {
				ProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
				ProbeInfo->PCI_Address = 0;
				ProbeInfo->Bus = Bus;
				ProbeInfo->Device = Device;
				ProbeInfo->IRQ_Channel = IRQ_Channel;
				ProbeInfo->PCI_Device = pci_dev_get(PCI_Device);
				break;
			}
		}
	}
	return PCIMultiMasterCount;
}


/*
  BusLogic_InitializeFlashPointProbeInfo initializes the list of I/O Address
  and Bus Probe Information to be checked for potential BusLogic FlashPoint
  Host Adapters by interrogating the PCI Configuration Space.  It returns the
  number of FlashPoint Host Adapters found.
*/

static int __init BusLogic_InitializeFlashPointProbeInfo(struct BusLogic_HostAdapter
							 *PrototypeHostAdapter)
{
	int FlashPointIndex = BusLogic_ProbeInfoCount, FlashPointCount = 0;
	struct pci_dev *PCI_Device = NULL;
	/*
	   Interrogate PCI Configuration Space for any FlashPoint Host Adapters.
	 */
	while ((PCI_Device = pci_get_device(PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_FLASHPOINT, PCI_Device)) != NULL) {
		unsigned char Bus;
		unsigned char Device;
		unsigned int IRQ_Channel;
		unsigned long BaseAddress0;
		unsigned long BaseAddress1;
		unsigned long IO_Address;
		unsigned long PCI_Address;

		if (pci_enable_device(PCI_Device))
			continue;

		if (pci_set_dma_mask(PCI_Device, DMA_BIT_MASK(32)))
			continue;

		Bus = PCI_Device->bus->number;
		Device = PCI_Device->devfn >> 3;
		IRQ_Channel = PCI_Device->irq;
		IO_Address = BaseAddress0 = pci_resource_start(PCI_Device, 0);
		PCI_Address = BaseAddress1 = pci_resource_start(PCI_Device, 1);
#ifdef CONFIG_SCSI_FLASHPOINT
		if (pci_resource_flags(PCI_Device, 0) & IORESOURCE_MEM) {
			BusLogic_Error("BusLogic: Base Address0 0x%X not I/O for " "FlashPoint Host Adapter\n", NULL, BaseAddress0);
			BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n", NULL, Bus, Device, IO_Address);
			continue;
		}
		if (pci_resource_flags(PCI_Device, 1) & IORESOURCE_IO) {
			BusLogic_Error("BusLogic: Base Address1 0x%X not Memory for " "FlashPoint Host Adapter\n", NULL, BaseAddress1);
			BusLogic_Error("at PCI Bus %d Device %d PCI Address 0x%X\n", NULL, Bus, Device, PCI_Address);
			continue;
		}
		if (IRQ_Channel == 0) {
			BusLogic_Error("BusLogic: IRQ Channel %d invalid for " "FlashPoint Host Adapter\n", NULL, IRQ_Channel);
			BusLogic_Error("at PCI Bus %d Device %d I/O Address 0x%X\n", NULL, Bus, Device, IO_Address);
			continue;
		}
		if (BusLogic_GlobalOptions.TraceProbe) {
			BusLogic_Notice("BusLogic: FlashPoint Host Adapter " "detected at\n", NULL);
			BusLogic_Notice("BusLogic: PCI Bus %d Device %d I/O Address " "0x%X PCI Address 0x%X\n", NULL, Bus, Device, IO_Address, PCI_Address);
		}
		if (BusLogic_ProbeInfoCount < BusLogic_MaxHostAdapters) {
			struct BusLogic_ProbeInfo *ProbeInfo = &BusLogic_ProbeInfoList[BusLogic_ProbeInfoCount++];
			ProbeInfo->HostAdapterType = BusLogic_FlashPoint;
			ProbeInfo->HostAdapterBusType = BusLogic_PCI_Bus;
			ProbeInfo->IO_Address = IO_Address;
			ProbeInfo->PCI_Address = PCI_Address;
			ProbeInfo->Bus = Bus;
			ProbeInfo->Device = Device;
			ProbeInfo->IRQ_Channel = IRQ_Channel;
			ProbeInfo->PCI_Device = pci_dev_get(PCI_Device);
			FlashPointCount++;
		} else
			BusLogic_Warning("BusLogic: Too many Host Adapters " "detected\n", NULL);
#else
		BusLogic_Error("BusLogic: FlashPoint Host Adapter detected at " "PCI Bus %d Device %d\n", NULL, Bus, Device);
		BusLogic_Error("BusLogic: I/O Address 0x%X PCI Address 0x%X, irq %d, " "but FlashPoint\n", NULL, IO_Address, PCI_Address, IRQ_Channel);
		BusLogic_Error("BusLogic: support was omitted in this kernel " "configuration.\n", NULL);
#endif
	}
	/*
	   The FlashPoint BIOS will scan for FlashPoint Host Adapters in the order of
	   increasing PCI Bus and Device Number, so sort the probe information into
	   the same order the BIOS uses.
	 */
	BusLogic_SortProbeInfo(&BusLogic_ProbeInfoList[FlashPointIndex], FlashPointCount);
	return FlashPointCount;
}


/*
  BusLogic_InitializeProbeInfoList initializes the list of I/O Address and Bus
  Probe Information to be checked for potential BusLogic SCSI Host Adapters by
  interrogating the PCI Configuration Space on PCI machines as well as from the
  list of standard BusLogic MultiMaster ISA I/O Addresses.  By default, if both
  FlashPoint and PCI MultiMaster Host Adapters are present, this driver will
  probe for FlashPoint Host Adapters first unless the BIOS primary disk is
  controlled by the first PCI MultiMaster Host Adapter, in which case
  MultiMaster Host Adapters will be probed first.  The BusLogic Driver Options
  specifications "MultiMasterFirst" and "FlashPointFirst" can be used to force
  a particular probe order.
*/

static void __init BusLogic_InitializeProbeInfoList(struct BusLogic_HostAdapter
						    *PrototypeHostAdapter)
{
	/*
	   If a PCI BIOS is present, interrogate it for MultiMaster and FlashPoint
	   Host Adapters; otherwise, default to the standard ISA MultiMaster probe.
	 */
	if (!BusLogic_ProbeOptions.NoProbePCI) {
		if (BusLogic_ProbeOptions.MultiMasterFirst) {
			BusLogic_InitializeMultiMasterProbeInfo(PrototypeHostAdapter);
			BusLogic_InitializeFlashPointProbeInfo(PrototypeHostAdapter);
		} else if (BusLogic_ProbeOptions.FlashPointFirst) {
			BusLogic_InitializeFlashPointProbeInfo(PrototypeHostAdapter);
			BusLogic_InitializeMultiMasterProbeInfo(PrototypeHostAdapter);
		} else {
			int FlashPointCount = BusLogic_InitializeFlashPointProbeInfo(PrototypeHostAdapter);
			int PCIMultiMasterCount = BusLogic_InitializeMultiMasterProbeInfo(PrototypeHostAdapter);
			if (FlashPointCount > 0 && PCIMultiMasterCount > 0) {
				struct BusLogic_ProbeInfo *ProbeInfo = &BusLogic_ProbeInfoList[FlashPointCount];
				struct BusLogic_HostAdapter *HostAdapter = PrototypeHostAdapter;
				struct BusLogic_FetchHostAdapterLocalRAMRequest FetchHostAdapterLocalRAMRequest;
				struct BusLogic_BIOSDriveMapByte Drive0MapByte;
				while (ProbeInfo->HostAdapterBusType != BusLogic_PCI_Bus)
					ProbeInfo++;
				HostAdapter->IO_Address = ProbeInfo->IO_Address;
				FetchHostAdapterLocalRAMRequest.ByteOffset = BusLogic_BIOS_BaseOffset + BusLogic_BIOS_DriveMapOffset + 0;
				FetchHostAdapterLocalRAMRequest.ByteCount = sizeof(Drive0MapByte);
				BusLogic_Command(HostAdapter, BusLogic_FetchHostAdapterLocalRAM, &FetchHostAdapterLocalRAMRequest, sizeof(FetchHostAdapterLocalRAMRequest), &Drive0MapByte, sizeof(Drive0MapByte));
				/*
				   If the Map Byte for BIOS Drive 0 indicates that BIOS Drive 0
				   is controlled by this PCI MultiMaster Host Adapter, then
				   reverse the probe order so that MultiMaster Host Adapters are
				   probed before FlashPoint Host Adapters.
				 */
				if (Drive0MapByte.DiskGeometry != BusLogic_BIOS_Disk_Not_Installed) {
					struct BusLogic_ProbeInfo SavedProbeInfo[BusLogic_MaxHostAdapters];
					int MultiMasterCount = BusLogic_ProbeInfoCount - FlashPointCount;
					memcpy(SavedProbeInfo, BusLogic_ProbeInfoList, BusLogic_ProbeInfoCount * sizeof(struct BusLogic_ProbeInfo));
					memcpy(&BusLogic_ProbeInfoList[0], &SavedProbeInfo[FlashPointCount], MultiMasterCount * sizeof(struct BusLogic_ProbeInfo));
					memcpy(&BusLogic_ProbeInfoList[MultiMasterCount], &SavedProbeInfo[0], FlashPointCount * sizeof(struct BusLogic_ProbeInfo));
				}
			}
		}
	} else
		BusLogic_InitializeProbeInfoListISA(PrototypeHostAdapter);
}


#else
#define BusLogic_InitializeProbeInfoList(adapter) \
		BusLogic_InitializeProbeInfoListISA(adapter)
#endif				/* CONFIG_PCI */


/*
  BusLogic_Failure prints a standardized error message, and then returns false.
*/

static bool BusLogic_Failure(struct BusLogic_HostAdapter *HostAdapter, char *ErrorMessage)
{
	BusLogic_AnnounceDriver(HostAdapter);
	if (HostAdapter->HostAdapterBusType == BusLogic_PCI_Bus) {
		BusLogic_Error("While configuring BusLogic PCI Host Adapter at\n", HostAdapter);
		BusLogic_Error("Bus %d Device %d I/O Address 0x%X PCI Address 0x%X:\n", HostAdapter, HostAdapter->Bus, HostAdapter->Device, HostAdapter->IO_Address, HostAdapter->PCI_Address);
	} else
		BusLogic_Error("While configuring BusLogic Host Adapter at " "I/O Address 0x%X:\n", HostAdapter, HostAdapter->IO_Address);
	BusLogic_Error("%s FAILED - DETACHING\n", HostAdapter, ErrorMessage);
	if (BusLogic_CommandFailureReason != NULL)
		BusLogic_Error("ADDITIONAL FAILURE INFO - %s\n", HostAdapter, BusLogic_CommandFailureReason);
	return false;
}


/*
  BusLogic_ProbeHostAdapter probes for a BusLogic Host Adapter.
*/

static bool __init BusLogic_ProbeHostAdapter(struct BusLogic_HostAdapter *HostAdapter)
{
	union BusLogic_StatusRegister StatusRegister;
	union BusLogic_InterruptRegister InterruptRegister;
	union BusLogic_GeometryRegister GeometryRegister;
	/*
	   FlashPoint Host Adapters are Probed by the FlashPoint SCCB Manager.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter)) {
		struct FlashPoint_Info *FlashPointInfo = &HostAdapter->FlashPointInfo;
		FlashPointInfo->BaseAddress = (u32) HostAdapter->IO_Address;
		FlashPointInfo->IRQ_Channel = HostAdapter->IRQ_Channel;
		FlashPointInfo->Present = false;
		if (!(FlashPoint_ProbeHostAdapter(FlashPointInfo) == 0 && FlashPointInfo->Present)) {
			BusLogic_Error("BusLogic: FlashPoint Host Adapter detected at " "PCI Bus %d Device %d\n", HostAdapter, HostAdapter->Bus, HostAdapter->Device);
			BusLogic_Error("BusLogic: I/O Address 0x%X PCI Address 0x%X, " "but FlashPoint\n", HostAdapter, HostAdapter->IO_Address, HostAdapter->PCI_Address);
			BusLogic_Error("BusLogic: Probe Function failed to validate it.\n", HostAdapter);
			return false;
		}
		if (BusLogic_GlobalOptions.TraceProbe)
			BusLogic_Notice("BusLogic_Probe(0x%X): FlashPoint Found\n", HostAdapter, HostAdapter->IO_Address);
		/*
		   Indicate the Host Adapter Probe completed successfully.
		 */
		return true;
	}
	/*
	   Read the Status, Interrupt, and Geometry Registers to test if there are I/O
	   ports that respond, and to check the values to determine if they are from a
	   BusLogic Host Adapter.  A nonexistent I/O port will return 0xFF, in which
	   case there is definitely no BusLogic Host Adapter at this base I/O Address.
	   The test here is a subset of that used by the BusLogic Host Adapter BIOS.
	 */
	StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
	InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
	GeometryRegister.All = BusLogic_ReadGeometryRegister(HostAdapter);
	if (BusLogic_GlobalOptions.TraceProbe)
		BusLogic_Notice("BusLogic_Probe(0x%X): Status 0x%02X, Interrupt 0x%02X, " "Geometry 0x%02X\n", HostAdapter, HostAdapter->IO_Address, StatusRegister.All, InterruptRegister.All, GeometryRegister.All);
	if (StatusRegister.All == 0 || StatusRegister.sr.DiagnosticActive || StatusRegister.sr.CommandParameterRegisterBusy || StatusRegister.sr.Reserved || StatusRegister.sr.CommandInvalid || InterruptRegister.ir.Reserved != 0)
		return false;
	/*
	   Check the undocumented Geometry Register to test if there is an I/O port
	   that responded.  Adaptec Host Adapters do not implement the Geometry
	   Register, so this test helps serve to avoid incorrectly recognizing an
	   Adaptec 1542A or 1542B as a BusLogic.  Unfortunately, the Adaptec 1542C
	   series does respond to the Geometry Register I/O port, but it will be
	   rejected later when the Inquire Extended Setup Information command is
	   issued in BusLogic_CheckHostAdapter.  The AMI FastDisk Host Adapter is a
	   BusLogic clone that implements the same interface as earlier BusLogic
	   Host Adapters, including the undocumented commands, and is therefore
	   supported by this driver.  However, the AMI FastDisk always returns 0x00
	   upon reading the Geometry Register, so the extended translation option
	   should always be left disabled on the AMI FastDisk.
	 */
	if (GeometryRegister.All == 0xFF)
		return false;
	/*
	   Indicate the Host Adapter Probe completed successfully.
	 */
	return true;
}


/*
  BusLogic_HardwareResetHostAdapter issues a Hardware Reset to the Host Adapter
  and waits for Host Adapter Diagnostics to complete.  If HardReset is true, a
  Hard Reset is performed which also initiates a SCSI Bus Reset.  Otherwise, a
  Soft Reset is performed which only resets the Host Adapter without forcing a
  SCSI Bus Reset.
*/

static bool BusLogic_HardwareResetHostAdapter(struct BusLogic_HostAdapter
						 *HostAdapter, bool HardReset)
{
	union BusLogic_StatusRegister StatusRegister;
	int TimeoutCounter;
	/*
	   FlashPoint Host Adapters are Hard Reset by the FlashPoint SCCB Manager.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter)) {
		struct FlashPoint_Info *FlashPointInfo = &HostAdapter->FlashPointInfo;
		FlashPointInfo->HostSoftReset = !HardReset;
		FlashPointInfo->ReportDataUnderrun = true;
		HostAdapter->CardHandle = FlashPoint_HardwareResetHostAdapter(FlashPointInfo);
		if (HostAdapter->CardHandle == FlashPoint_BadCardHandle)
			return false;
		/*
		   Indicate the Host Adapter Hard Reset completed successfully.
		 */
		return true;
	}
	/*
	   Issue a Hard Reset or Soft Reset Command to the Host Adapter.  The Host
	   Adapter should respond by setting Diagnostic Active in the Status Register.
	 */
	if (HardReset)
		BusLogic_HardReset(HostAdapter);
	else
		BusLogic_SoftReset(HostAdapter);
	/*
	   Wait until Diagnostic Active is set in the Status Register.
	 */
	TimeoutCounter = 5 * 10000;
	while (--TimeoutCounter >= 0) {
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (StatusRegister.sr.DiagnosticActive)
			break;
		udelay(100);
	}
	if (BusLogic_GlobalOptions.TraceHardwareReset)
		BusLogic_Notice("BusLogic_HardwareReset(0x%X): Diagnostic Active, " "Status 0x%02X\n", HostAdapter, HostAdapter->IO_Address, StatusRegister.All);
	if (TimeoutCounter < 0)
		return false;
	/*
	   Wait 100 microseconds to allow completion of any initial diagnostic
	   activity which might leave the contents of the Status Register
	   unpredictable.
	 */
	udelay(100);
	/*
	   Wait until Diagnostic Active is reset in the Status Register.
	 */
	TimeoutCounter = 10 * 10000;
	while (--TimeoutCounter >= 0) {
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (!StatusRegister.sr.DiagnosticActive)
			break;
		udelay(100);
	}
	if (BusLogic_GlobalOptions.TraceHardwareReset)
		BusLogic_Notice("BusLogic_HardwareReset(0x%X): Diagnostic Completed, " "Status 0x%02X\n", HostAdapter, HostAdapter->IO_Address, StatusRegister.All);
	if (TimeoutCounter < 0)
		return false;
	/*
	   Wait until at least one of the Diagnostic Failure, Host Adapter Ready,
	   or Data In Register Ready bits is set in the Status Register.
	 */
	TimeoutCounter = 10000;
	while (--TimeoutCounter >= 0) {
		StatusRegister.All = BusLogic_ReadStatusRegister(HostAdapter);
		if (StatusRegister.sr.DiagnosticFailure || StatusRegister.sr.HostAdapterReady || StatusRegister.sr.DataInRegisterReady)
			break;
		udelay(100);
	}
	if (BusLogic_GlobalOptions.TraceHardwareReset)
		BusLogic_Notice("BusLogic_HardwareReset(0x%X): Host Adapter Ready, " "Status 0x%02X\n", HostAdapter, HostAdapter->IO_Address, StatusRegister.All);
	if (TimeoutCounter < 0)
		return false;
	/*
	   If Diagnostic Failure is set or Host Adapter Ready is reset, then an
	   error occurred during the Host Adapter diagnostics.  If Data In Register
	   Ready is set, then there is an Error Code available.
	 */
	if (StatusRegister.sr.DiagnosticFailure || !StatusRegister.sr.HostAdapterReady) {
		BusLogic_CommandFailureReason = NULL;
		BusLogic_Failure(HostAdapter, "HARD RESET DIAGNOSTICS");
		BusLogic_Error("HOST ADAPTER STATUS REGISTER = %02X\n", HostAdapter, StatusRegister.All);
		if (StatusRegister.sr.DataInRegisterReady) {
			unsigned char ErrorCode = BusLogic_ReadDataInRegister(HostAdapter);
			BusLogic_Error("HOST ADAPTER ERROR CODE = %d\n", HostAdapter, ErrorCode);
		}
		return false;
	}
	/*
	   Indicate the Host Adapter Hard Reset completed successfully.
	 */
	return true;
}


/*
  BusLogic_CheckHostAdapter checks to be sure this really is a BusLogic
  Host Adapter.
*/

static bool __init BusLogic_CheckHostAdapter(struct BusLogic_HostAdapter *HostAdapter)
{
	struct BusLogic_ExtendedSetupInformation ExtendedSetupInformation;
	unsigned char RequestedReplyLength;
	bool Result = true;
	/*
	   FlashPoint Host Adapters do not require this protection.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter))
		return true;
	/*
	   Issue the Inquire Extended Setup Information command.  Only genuine
	   BusLogic Host Adapters and true clones support this command.  Adaptec 1542C
	   series Host Adapters that respond to the Geometry Register I/O port will
	   fail this command.
	 */
	RequestedReplyLength = sizeof(ExtendedSetupInformation);
	if (BusLogic_Command(HostAdapter, BusLogic_InquireExtendedSetupInformation, &RequestedReplyLength, sizeof(RequestedReplyLength), &ExtendedSetupInformation, sizeof(ExtendedSetupInformation))
	    != sizeof(ExtendedSetupInformation))
		Result = false;
	/*
	   Provide tracing information if requested and return.
	 */
	if (BusLogic_GlobalOptions.TraceProbe)
		BusLogic_Notice("BusLogic_Check(0x%X): MultiMaster %s\n", HostAdapter, HostAdapter->IO_Address, (Result ? "Found" : "Not Found"));
	return Result;
}


/*
  BusLogic_ReadHostAdapterConfiguration reads the Configuration Information
  from Host Adapter and initializes the Host Adapter structure.
*/

static bool __init BusLogic_ReadHostAdapterConfiguration(struct BusLogic_HostAdapter
							    *HostAdapter)
{
	struct BusLogic_BoardID BoardID;
	struct BusLogic_Configuration Configuration;
	struct BusLogic_SetupInformation SetupInformation;
	struct BusLogic_ExtendedSetupInformation ExtendedSetupInformation;
	unsigned char HostAdapterModelNumber[5];
	unsigned char FirmwareVersion3rdDigit;
	unsigned char FirmwareVersionLetter;
	struct BusLogic_PCIHostAdapterInformation PCIHostAdapterInformation;
	struct BusLogic_FetchHostAdapterLocalRAMRequest FetchHostAdapterLocalRAMRequest;
	struct BusLogic_AutoSCSIData AutoSCSIData;
	union BusLogic_GeometryRegister GeometryRegister;
	unsigned char RequestedReplyLength;
	unsigned char *TargetPointer, Character;
	int TargetID, i;
	/*
	   Configuration Information for FlashPoint Host Adapters is provided in the
	   FlashPoint_Info structure by the FlashPoint SCCB Manager's Probe Function.
	   Initialize fields in the Host Adapter structure from the FlashPoint_Info
	   structure.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter)) {
		struct FlashPoint_Info *FlashPointInfo = &HostAdapter->FlashPointInfo;
		TargetPointer = HostAdapter->ModelName;
		*TargetPointer++ = 'B';
		*TargetPointer++ = 'T';
		*TargetPointer++ = '-';
		for (i = 0; i < sizeof(FlashPointInfo->ModelNumber); i++)
			*TargetPointer++ = FlashPointInfo->ModelNumber[i];
		*TargetPointer++ = '\0';
		strcpy(HostAdapter->FirmwareVersion, FlashPoint_FirmwareVersion);
		HostAdapter->SCSI_ID = FlashPointInfo->SCSI_ID;
		HostAdapter->ExtendedTranslationEnabled = FlashPointInfo->ExtendedTranslationEnabled;
		HostAdapter->ParityCheckingEnabled = FlashPointInfo->ParityCheckingEnabled;
		HostAdapter->BusResetEnabled = !FlashPointInfo->HostSoftReset;
		HostAdapter->LevelSensitiveInterrupt = true;
		HostAdapter->HostWideSCSI = FlashPointInfo->HostWideSCSI;
		HostAdapter->HostDifferentialSCSI = false;
		HostAdapter->HostSupportsSCAM = true;
		HostAdapter->HostUltraSCSI = true;
		HostAdapter->ExtendedLUNSupport = true;
		HostAdapter->TerminationInfoValid = true;
		HostAdapter->LowByteTerminated = FlashPointInfo->LowByteTerminated;
		HostAdapter->HighByteTerminated = FlashPointInfo->HighByteTerminated;
		HostAdapter->SCAM_Enabled = FlashPointInfo->SCAM_Enabled;
		HostAdapter->SCAM_Level2 = FlashPointInfo->SCAM_Level2;
		HostAdapter->DriverScatterGatherLimit = BusLogic_ScatterGatherLimit;
		HostAdapter->MaxTargetDevices = (HostAdapter->HostWideSCSI ? 16 : 8);
		HostAdapter->MaxLogicalUnits = 32;
		HostAdapter->InitialCCBs = 4 * BusLogic_CCB_AllocationGroupSize;
		HostAdapter->IncrementalCCBs = BusLogic_CCB_AllocationGroupSize;
		HostAdapter->DriverQueueDepth = 255;
		HostAdapter->HostAdapterQueueDepth = HostAdapter->DriverQueueDepth;
		HostAdapter->SynchronousPermitted = FlashPointInfo->SynchronousPermitted;
		HostAdapter->FastPermitted = FlashPointInfo->FastPermitted;
		HostAdapter->UltraPermitted = FlashPointInfo->UltraPermitted;
		HostAdapter->WidePermitted = FlashPointInfo->WidePermitted;
		HostAdapter->DisconnectPermitted = FlashPointInfo->DisconnectPermitted;
		HostAdapter->TaggedQueuingPermitted = 0xFFFF;
		goto Common;
	}
	/*
	   Issue the Inquire Board ID command.
	 */
	if (BusLogic_Command(HostAdapter, BusLogic_InquireBoardID, NULL, 0, &BoardID, sizeof(BoardID)) != sizeof(BoardID))
		return BusLogic_Failure(HostAdapter, "INQUIRE BOARD ID");
	/*
	   Issue the Inquire Configuration command.
	 */
	if (BusLogic_Command(HostAdapter, BusLogic_InquireConfiguration, NULL, 0, &Configuration, sizeof(Configuration))
	    != sizeof(Configuration))
		return BusLogic_Failure(HostAdapter, "INQUIRE CONFIGURATION");
	/*
	   Issue the Inquire Setup Information command.
	 */
	RequestedReplyLength = sizeof(SetupInformation);
	if (BusLogic_Command(HostAdapter, BusLogic_InquireSetupInformation, &RequestedReplyLength, sizeof(RequestedReplyLength), &SetupInformation, sizeof(SetupInformation))
	    != sizeof(SetupInformation))
		return BusLogic_Failure(HostAdapter, "INQUIRE SETUP INFORMATION");
	/*
	   Issue the Inquire Extended Setup Information command.
	 */
	RequestedReplyLength = sizeof(ExtendedSetupInformation);
	if (BusLogic_Command(HostAdapter, BusLogic_InquireExtendedSetupInformation, &RequestedReplyLength, sizeof(RequestedReplyLength), &ExtendedSetupInformation, sizeof(ExtendedSetupInformation))
	    != sizeof(ExtendedSetupInformation))
		return BusLogic_Failure(HostAdapter, "INQUIRE EXTENDED SETUP INFORMATION");
	/*
	   Issue the Inquire Firmware Version 3rd Digit command.
	 */
	FirmwareVersion3rdDigit = '\0';
	if (BoardID.FirmwareVersion1stDigit > '0')
		if (BusLogic_Command(HostAdapter, BusLogic_InquireFirmwareVersion3rdDigit, NULL, 0, &FirmwareVersion3rdDigit, sizeof(FirmwareVersion3rdDigit))
		    != sizeof(FirmwareVersion3rdDigit))
			return BusLogic_Failure(HostAdapter, "INQUIRE FIRMWARE 3RD DIGIT");
	/*
	   Issue the Inquire Host Adapter Model Number command.
	 */
	if (ExtendedSetupInformation.BusType == 'A' && BoardID.FirmwareVersion1stDigit == '2')
		/* BusLogic BT-542B ISA 2.xx */
		strcpy(HostAdapterModelNumber, "542B");
	else if (ExtendedSetupInformation.BusType == 'E' && BoardID.FirmwareVersion1stDigit == '2' && (BoardID.FirmwareVersion2ndDigit <= '1' || (BoardID.FirmwareVersion2ndDigit == '2' && FirmwareVersion3rdDigit == '0')))
		/* BusLogic BT-742A EISA 2.1x or 2.20 */
		strcpy(HostAdapterModelNumber, "742A");
	else if (ExtendedSetupInformation.BusType == 'E' && BoardID.FirmwareVersion1stDigit == '0')
		/* AMI FastDisk EISA Series 441 0.x */
		strcpy(HostAdapterModelNumber, "747A");
	else {
		RequestedReplyLength = sizeof(HostAdapterModelNumber);
		if (BusLogic_Command(HostAdapter, BusLogic_InquireHostAdapterModelNumber, &RequestedReplyLength, sizeof(RequestedReplyLength), &HostAdapterModelNumber, sizeof(HostAdapterModelNumber))
		    != sizeof(HostAdapterModelNumber))
			return BusLogic_Failure(HostAdapter, "INQUIRE HOST ADAPTER MODEL NUMBER");
	}
	/*
	   BusLogic MultiMaster Host Adapters can be identified by their model number
	   and the major version number of their firmware as follows:

	   5.xx       BusLogic "W" Series Host Adapters:
	   BT-948/958/958D
	   4.xx       BusLogic "C" Series Host Adapters:
	   BT-946C/956C/956CD/747C/757C/757CD/445C/545C/540CF
	   3.xx       BusLogic "S" Series Host Adapters:
	   BT-747S/747D/757S/757D/445S/545S/542D
	   BT-542B/742A (revision H)
	   2.xx       BusLogic "A" Series Host Adapters:
	   BT-542B/742A (revision G and below)
	   0.xx       AMI FastDisk VLB/EISA BusLogic Clone Host Adapter
	 */
	/*
	   Save the Model Name and Host Adapter Name in the Host Adapter structure.
	 */
	TargetPointer = HostAdapter->ModelName;
	*TargetPointer++ = 'B';
	*TargetPointer++ = 'T';
	*TargetPointer++ = '-';
	for (i = 0; i < sizeof(HostAdapterModelNumber); i++) {
		Character = HostAdapterModelNumber[i];
		if (Character == ' ' || Character == '\0')
			break;
		*TargetPointer++ = Character;
	}
	*TargetPointer++ = '\0';
	/*
	   Save the Firmware Version in the Host Adapter structure.
	 */
	TargetPointer = HostAdapter->FirmwareVersion;
	*TargetPointer++ = BoardID.FirmwareVersion1stDigit;
	*TargetPointer++ = '.';
	*TargetPointer++ = BoardID.FirmwareVersion2ndDigit;
	if (FirmwareVersion3rdDigit != ' ' && FirmwareVersion3rdDigit != '\0')
		*TargetPointer++ = FirmwareVersion3rdDigit;
	*TargetPointer = '\0';
	/*
	   Issue the Inquire Firmware Version Letter command.
	 */
	if (strcmp(HostAdapter->FirmwareVersion, "3.3") >= 0) {
		if (BusLogic_Command(HostAdapter, BusLogic_InquireFirmwareVersionLetter, NULL, 0, &FirmwareVersionLetter, sizeof(FirmwareVersionLetter))
		    != sizeof(FirmwareVersionLetter))
			return BusLogic_Failure(HostAdapter, "INQUIRE FIRMWARE VERSION LETTER");
		if (FirmwareVersionLetter != ' ' && FirmwareVersionLetter != '\0')
			*TargetPointer++ = FirmwareVersionLetter;
		*TargetPointer = '\0';
	}
	/*
	   Save the Host Adapter SCSI ID in the Host Adapter structure.
	 */
	HostAdapter->SCSI_ID = Configuration.HostAdapterID;
	/*
	   Determine the Bus Type and save it in the Host Adapter structure, determine
	   and save the IRQ Channel if necessary, and determine and save the DMA
	   Channel for ISA Host Adapters.
	 */
	HostAdapter->HostAdapterBusType = BusLogic_HostAdapterBusTypes[HostAdapter->ModelName[3] - '4'];
	if (HostAdapter->IRQ_Channel == 0) {
		if (Configuration.IRQ_Channel9)
			HostAdapter->IRQ_Channel = 9;
		else if (Configuration.IRQ_Channel10)
			HostAdapter->IRQ_Channel = 10;
		else if (Configuration.IRQ_Channel11)
			HostAdapter->IRQ_Channel = 11;
		else if (Configuration.IRQ_Channel12)
			HostAdapter->IRQ_Channel = 12;
		else if (Configuration.IRQ_Channel14)
			HostAdapter->IRQ_Channel = 14;
		else if (Configuration.IRQ_Channel15)
			HostAdapter->IRQ_Channel = 15;
	}
	if (HostAdapter->HostAdapterBusType == BusLogic_ISA_Bus) {
		if (Configuration.DMA_Channel5)
			HostAdapter->DMA_Channel = 5;
		else if (Configuration.DMA_Channel6)
			HostAdapter->DMA_Channel = 6;
		else if (Configuration.DMA_Channel7)
			HostAdapter->DMA_Channel = 7;
	}
	/*
	   Determine whether Extended Translation is enabled and save it in
	   the Host Adapter structure.
	 */
	GeometryRegister.All = BusLogic_ReadGeometryRegister(HostAdapter);
	HostAdapter->ExtendedTranslationEnabled = GeometryRegister.gr.ExtendedTranslationEnabled;
	/*
	   Save the Scatter Gather Limits, Level Sensitive Interrupt flag, Wide
	   SCSI flag, Differential SCSI flag, SCAM Supported flag, and
	   Ultra SCSI flag in the Host Adapter structure.
	 */
	HostAdapter->HostAdapterScatterGatherLimit = ExtendedSetupInformation.ScatterGatherLimit;
	HostAdapter->DriverScatterGatherLimit = HostAdapter->HostAdapterScatterGatherLimit;
	if (HostAdapter->HostAdapterScatterGatherLimit > BusLogic_ScatterGatherLimit)
		HostAdapter->DriverScatterGatherLimit = BusLogic_ScatterGatherLimit;
	if (ExtendedSetupInformation.Misc.LevelSensitiveInterrupt)
		HostAdapter->LevelSensitiveInterrupt = true;
	HostAdapter->HostWideSCSI = ExtendedSetupInformation.HostWideSCSI;
	HostAdapter->HostDifferentialSCSI = ExtendedSetupInformation.HostDifferentialSCSI;
	HostAdapter->HostSupportsSCAM = ExtendedSetupInformation.HostSupportsSCAM;
	HostAdapter->HostUltraSCSI = ExtendedSetupInformation.HostUltraSCSI;
	/*
	   Determine whether Extended LUN Format CCBs are supported and save the
	   information in the Host Adapter structure.
	 */
	if (HostAdapter->FirmwareVersion[0] == '5' || (HostAdapter->FirmwareVersion[0] == '4' && HostAdapter->HostWideSCSI))
		HostAdapter->ExtendedLUNSupport = true;
	/*
	   Issue the Inquire PCI Host Adapter Information command to read the
	   Termination Information from "W" series MultiMaster Host Adapters.
	 */
	if (HostAdapter->FirmwareVersion[0] == '5') {
		if (BusLogic_Command(HostAdapter, BusLogic_InquirePCIHostAdapterInformation, NULL, 0, &PCIHostAdapterInformation, sizeof(PCIHostAdapterInformation))
		    != sizeof(PCIHostAdapterInformation))
			return BusLogic_Failure(HostAdapter, "INQUIRE PCI HOST ADAPTER INFORMATION");
		/*
		   Save the Termination Information in the Host Adapter structure.
		 */
		if (PCIHostAdapterInformation.GenericInfoValid) {
			HostAdapter->TerminationInfoValid = true;
			HostAdapter->LowByteTerminated = PCIHostAdapterInformation.LowByteTerminated;
			HostAdapter->HighByteTerminated = PCIHostAdapterInformation.HighByteTerminated;
		}
	}
	/*
	   Issue the Fetch Host Adapter Local RAM command to read the AutoSCSI data
	   from "W" and "C" series MultiMaster Host Adapters.
	 */
	if (HostAdapter->FirmwareVersion[0] >= '4') {
		FetchHostAdapterLocalRAMRequest.ByteOffset = BusLogic_AutoSCSI_BaseOffset;
		FetchHostAdapterLocalRAMRequest.ByteCount = sizeof(AutoSCSIData);
		if (BusLogic_Command(HostAdapter, BusLogic_FetchHostAdapterLocalRAM, &FetchHostAdapterLocalRAMRequest, sizeof(FetchHostAdapterLocalRAMRequest), &AutoSCSIData, sizeof(AutoSCSIData))
		    != sizeof(AutoSCSIData))
			return BusLogic_Failure(HostAdapter, "FETCH HOST ADAPTER LOCAL RAM");
		/*
		   Save the Parity Checking Enabled, Bus Reset Enabled, and Termination
		   Information in the Host Adapter structure.
		 */
		HostAdapter->ParityCheckingEnabled = AutoSCSIData.ParityCheckingEnabled;
		HostAdapter->BusResetEnabled = AutoSCSIData.BusResetEnabled;
		if (HostAdapter->FirmwareVersion[0] == '4') {
			HostAdapter->TerminationInfoValid = true;
			HostAdapter->LowByteTerminated = AutoSCSIData.LowByteTerminated;
			HostAdapter->HighByteTerminated = AutoSCSIData.HighByteTerminated;
		}
		/*
		   Save the Wide Permitted, Fast Permitted, Synchronous Permitted,
		   Disconnect Permitted, Ultra Permitted, and SCAM Information in the
		   Host Adapter structure.
		 */
		HostAdapter->WidePermitted = AutoSCSIData.WidePermitted;
		HostAdapter->FastPermitted = AutoSCSIData.FastPermitted;
		HostAdapter->SynchronousPermitted = AutoSCSIData.SynchronousPermitted;
		HostAdapter->DisconnectPermitted = AutoSCSIData.DisconnectPermitted;
		if (HostAdapter->HostUltraSCSI)
			HostAdapter->UltraPermitted = AutoSCSIData.UltraPermitted;
		if (HostAdapter->HostSupportsSCAM) {
			HostAdapter->SCAM_Enabled = AutoSCSIData.SCAM_Enabled;
			HostAdapter->SCAM_Level2 = AutoSCSIData.SCAM_Level2;
		}
	}
	/*
	   Initialize fields in the Host Adapter structure for "S" and "A" series
	   MultiMaster Host Adapters.
	 */
	if (HostAdapter->FirmwareVersion[0] < '4') {
		if (SetupInformation.SynchronousInitiationEnabled) {
			HostAdapter->SynchronousPermitted = 0xFF;
			if (HostAdapter->HostAdapterBusType == BusLogic_EISA_Bus) {
				if (ExtendedSetupInformation.Misc.FastOnEISA)
					HostAdapter->FastPermitted = 0xFF;
				if (strcmp(HostAdapter->ModelName, "BT-757") == 0)
					HostAdapter->WidePermitted = 0xFF;
			}
		}
		HostAdapter->DisconnectPermitted = 0xFF;
		HostAdapter->ParityCheckingEnabled = SetupInformation.ParityCheckingEnabled;
		HostAdapter->BusResetEnabled = true;
	}
	/*
	   Determine the maximum number of Target IDs and Logical Units supported by
	   this driver for Wide and Narrow Host Adapters.
	 */
	HostAdapter->MaxTargetDevices = (HostAdapter->HostWideSCSI ? 16 : 8);
	HostAdapter->MaxLogicalUnits = (HostAdapter->ExtendedLUNSupport ? 32 : 8);
	/*
	   Select appropriate values for the Mailbox Count, Driver Queue Depth,
	   Initial CCBs, and Incremental CCBs variables based on whether or not Strict
	   Round Robin Mode is supported.  If Strict Round Robin Mode is supported,
	   then there is no performance degradation in using the maximum possible
	   number of Outgoing and Incoming Mailboxes and allowing the Tagged and
	   Untagged Queue Depths to determine the actual utilization.  If Strict Round
	   Robin Mode is not supported, then the Host Adapter must scan all the
	   Outgoing Mailboxes whenever an Outgoing Mailbox entry is made, which can
	   cause a substantial performance penalty.  The host adapters actually have
	   room to store the following number of CCBs internally; that is, they can
	   internally queue and manage this many active commands on the SCSI bus
	   simultaneously.  Performance measurements demonstrate that the Driver Queue
	   Depth should be set to the Mailbox Count, rather than the Host Adapter
	   Queue Depth (internal CCB capacity), as it is more efficient to have the
	   queued commands waiting in Outgoing Mailboxes if necessary than to block
	   the process in the higher levels of the SCSI Subsystem.

	   192          BT-948/958/958D
	   100          BT-946C/956C/956CD/747C/757C/757CD/445C
	   50   BT-545C/540CF
	   30   BT-747S/747D/757S/757D/445S/545S/542D/542B/742A
	 */
	if (HostAdapter->FirmwareVersion[0] == '5')
		HostAdapter->HostAdapterQueueDepth = 192;
	else if (HostAdapter->FirmwareVersion[0] == '4')
		HostAdapter->HostAdapterQueueDepth = (HostAdapter->HostAdapterBusType != BusLogic_ISA_Bus ? 100 : 50);
	else
		HostAdapter->HostAdapterQueueDepth = 30;
	if (strcmp(HostAdapter->FirmwareVersion, "3.31") >= 0) {
		HostAdapter->StrictRoundRobinModeSupport = true;
		HostAdapter->MailboxCount = BusLogic_MaxMailboxes;
	} else {
		HostAdapter->StrictRoundRobinModeSupport = false;
		HostAdapter->MailboxCount = 32;
	}
	HostAdapter->DriverQueueDepth = HostAdapter->MailboxCount;
	HostAdapter->InitialCCBs = 4 * BusLogic_CCB_AllocationGroupSize;
	HostAdapter->IncrementalCCBs = BusLogic_CCB_AllocationGroupSize;
	/*
	   Tagged Queuing support is available and operates properly on all "W" series
	   MultiMaster Host Adapters, on "C" series MultiMaster Host Adapters with
	   firmware version 4.22 and above, and on "S" series MultiMaster Host
	   Adapters with firmware version 3.35 and above.
	 */
	HostAdapter->TaggedQueuingPermitted = 0;
	switch (HostAdapter->FirmwareVersion[0]) {
	case '5':
		HostAdapter->TaggedQueuingPermitted = 0xFFFF;
		break;
	case '4':
		if (strcmp(HostAdapter->FirmwareVersion, "4.22") >= 0)
			HostAdapter->TaggedQueuingPermitted = 0xFFFF;
		break;
	case '3':
		if (strcmp(HostAdapter->FirmwareVersion, "3.35") >= 0)
			HostAdapter->TaggedQueuingPermitted = 0xFFFF;
		break;
	}
	/*
	   Determine the Host Adapter BIOS Address if the BIOS is enabled and
	   save it in the Host Adapter structure.  The BIOS is disabled if the
	   BIOS_Address is 0.
	 */
	HostAdapter->BIOS_Address = ExtendedSetupInformation.BIOS_Address << 12;
	/*
	   ISA Host Adapters require Bounce Buffers if there is more than 16MB memory.
	 */
	if (HostAdapter->HostAdapterBusType == BusLogic_ISA_Bus && (void *) high_memory > (void *) MAX_DMA_ADDRESS)
		HostAdapter->BounceBuffersRequired = true;
	/*
	   BusLogic BT-445S Host Adapters prior to board revision E have a hardware
	   bug whereby when the BIOS is enabled, transfers to/from the same address
	   range the BIOS occupies modulo 16MB are handled incorrectly.  Only properly
	   functioning BT-445S Host Adapters have firmware version 3.37, so require
	   that ISA Bounce Buffers be used for the buggy BT-445S models if there is
	   more than 16MB memory.
	 */
	if (HostAdapter->BIOS_Address > 0 && strcmp(HostAdapter->ModelName, "BT-445S") == 0 && strcmp(HostAdapter->FirmwareVersion, "3.37") < 0 && (void *) high_memory > (void *) MAX_DMA_ADDRESS)
		HostAdapter->BounceBuffersRequired = true;
	/*
	   Initialize parameters common to MultiMaster and FlashPoint Host Adapters.
	 */
      Common:
	/*
	   Initialize the Host Adapter Full Model Name from the Model Name.
	 */
	strcpy(HostAdapter->FullModelName, "BusLogic ");
	strcat(HostAdapter->FullModelName, HostAdapter->ModelName);
	/*
	   Select an appropriate value for the Tagged Queue Depth either from a
	   BusLogic Driver Options specification, or based on whether this Host
	   Adapter requires that ISA Bounce Buffers be used.  The Tagged Queue Depth
	   is left at 0 for automatic determination in BusLogic_SelectQueueDepths.
	   Initialize the Untagged Queue Depth.
	 */
	for (TargetID = 0; TargetID < BusLogic_MaxTargetDevices; TargetID++) {
		unsigned char QueueDepth = 0;
		if (HostAdapter->DriverOptions != NULL && HostAdapter->DriverOptions->QueueDepth[TargetID] > 0)
			QueueDepth = HostAdapter->DriverOptions->QueueDepth[TargetID];
		else if (HostAdapter->BounceBuffersRequired)
			QueueDepth = BusLogic_TaggedQueueDepthBB;
		HostAdapter->QueueDepth[TargetID] = QueueDepth;
	}
	if (HostAdapter->BounceBuffersRequired)
		HostAdapter->UntaggedQueueDepth = BusLogic_UntaggedQueueDepthBB;
	else
		HostAdapter->UntaggedQueueDepth = BusLogic_UntaggedQueueDepth;
	if (HostAdapter->DriverOptions != NULL)
		HostAdapter->CommonQueueDepth = HostAdapter->DriverOptions->CommonQueueDepth;
	if (HostAdapter->CommonQueueDepth > 0 && HostAdapter->CommonQueueDepth < HostAdapter->UntaggedQueueDepth)
		HostAdapter->UntaggedQueueDepth = HostAdapter->CommonQueueDepth;
	/*
	   Tagged Queuing is only allowed if Disconnect/Reconnect is permitted.
	   Therefore, mask the Tagged Queuing Permitted Default bits with the
	   Disconnect/Reconnect Permitted bits.
	 */
	HostAdapter->TaggedQueuingPermitted &= HostAdapter->DisconnectPermitted;
	/*
	   Combine the default Tagged Queuing Permitted bits with any BusLogic Driver
	   Options Tagged Queuing specification.
	 */
	if (HostAdapter->DriverOptions != NULL)
		HostAdapter->TaggedQueuingPermitted =
		    (HostAdapter->DriverOptions->TaggedQueuingPermitted & HostAdapter->DriverOptions->TaggedQueuingPermittedMask) | (HostAdapter->TaggedQueuingPermitted & ~HostAdapter->DriverOptions->TaggedQueuingPermittedMask);

	/*
	   Select an appropriate value for Bus Settle Time either from a BusLogic
	   Driver Options specification, or from BusLogic_DefaultBusSettleTime.
	 */
	if (HostAdapter->DriverOptions != NULL && HostAdapter->DriverOptions->BusSettleTime > 0)
		HostAdapter->BusSettleTime = HostAdapter->DriverOptions->BusSettleTime;
	else
		HostAdapter->BusSettleTime = BusLogic_DefaultBusSettleTime;
	/*
	   Indicate reading the Host Adapter Configuration completed successfully.
	 */
	return true;
}


/*
  BusLogic_ReportHostAdapterConfiguration reports the configuration of
  Host Adapter.
*/

static bool __init BusLogic_ReportHostAdapterConfiguration(struct BusLogic_HostAdapter
							      *HostAdapter)
{
	unsigned short AllTargetsMask = (1 << HostAdapter->MaxTargetDevices) - 1;
	unsigned short SynchronousPermitted, FastPermitted;
	unsigned short UltraPermitted, WidePermitted;
	unsigned short DisconnectPermitted, TaggedQueuingPermitted;
	bool CommonSynchronousNegotiation, CommonTaggedQueueDepth;
	char SynchronousString[BusLogic_MaxTargetDevices + 1];
	char WideString[BusLogic_MaxTargetDevices + 1];
	char DisconnectString[BusLogic_MaxTargetDevices + 1];
	char TaggedQueuingString[BusLogic_MaxTargetDevices + 1];
	char *SynchronousMessage = SynchronousString;
	char *WideMessage = WideString;
	char *DisconnectMessage = DisconnectString;
	char *TaggedQueuingMessage = TaggedQueuingString;
	int TargetID;
	BusLogic_Info("Configuring BusLogic Model %s %s%s%s%s SCSI Host Adapter\n",
		      HostAdapter, HostAdapter->ModelName,
		      BusLogic_HostAdapterBusNames[HostAdapter->HostAdapterBusType], (HostAdapter->HostWideSCSI ? " Wide" : ""), (HostAdapter->HostDifferentialSCSI ? " Differential" : ""), (HostAdapter->HostUltraSCSI ? " Ultra" : ""));
	BusLogic_Info("  Firmware Version: %s, I/O Address: 0x%X, " "IRQ Channel: %d/%s\n", HostAdapter, HostAdapter->FirmwareVersion, HostAdapter->IO_Address, HostAdapter->IRQ_Channel, (HostAdapter->LevelSensitiveInterrupt ? "Level" : "Edge"));
	if (HostAdapter->HostAdapterBusType != BusLogic_PCI_Bus) {
		BusLogic_Info("  DMA Channel: ", HostAdapter);
		if (HostAdapter->DMA_Channel > 0)
			BusLogic_Info("%d, ", HostAdapter, HostAdapter->DMA_Channel);
		else
			BusLogic_Info("None, ", HostAdapter);
		if (HostAdapter->BIOS_Address > 0)
			BusLogic_Info("BIOS Address: 0x%X, ", HostAdapter, HostAdapter->BIOS_Address);
		else
			BusLogic_Info("BIOS Address: None, ", HostAdapter);
	} else {
		BusLogic_Info("  PCI Bus: %d, Device: %d, Address: ", HostAdapter, HostAdapter->Bus, HostAdapter->Device);
		if (HostAdapter->PCI_Address > 0)
			BusLogic_Info("0x%X, ", HostAdapter, HostAdapter->PCI_Address);
		else
			BusLogic_Info("Unassigned, ", HostAdapter);
	}
	BusLogic_Info("Host Adapter SCSI ID: %d\n", HostAdapter, HostAdapter->SCSI_ID);
	BusLogic_Info("  Parity Checking: %s, Extended Translation: %s\n", HostAdapter, (HostAdapter->ParityCheckingEnabled ? "Enabled" : "Disabled"), (HostAdapter->ExtendedTranslationEnabled ? "Enabled" : "Disabled"));
	AllTargetsMask &= ~(1 << HostAdapter->SCSI_ID);
	SynchronousPermitted = HostAdapter->SynchronousPermitted & AllTargetsMask;
	FastPermitted = HostAdapter->FastPermitted & AllTargetsMask;
	UltraPermitted = HostAdapter->UltraPermitted & AllTargetsMask;
	if ((BusLogic_MultiMasterHostAdapterP(HostAdapter) && (HostAdapter->FirmwareVersion[0] >= '4' || HostAdapter->HostAdapterBusType == BusLogic_EISA_Bus)) || BusLogic_FlashPointHostAdapterP(HostAdapter)) {
		CommonSynchronousNegotiation = false;
		if (SynchronousPermitted == 0) {
			SynchronousMessage = "Disabled";
			CommonSynchronousNegotiation = true;
		} else if (SynchronousPermitted == AllTargetsMask) {
			if (FastPermitted == 0) {
				SynchronousMessage = "Slow";
				CommonSynchronousNegotiation = true;
			} else if (FastPermitted == AllTargetsMask) {
				if (UltraPermitted == 0) {
					SynchronousMessage = "Fast";
					CommonSynchronousNegotiation = true;
				} else if (UltraPermitted == AllTargetsMask) {
					SynchronousMessage = "Ultra";
					CommonSynchronousNegotiation = true;
				}
			}
		}
		if (!CommonSynchronousNegotiation) {
			for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
				SynchronousString[TargetID] = ((!(SynchronousPermitted & (1 << TargetID))) ? 'N' : (!(FastPermitted & (1 << TargetID)) ? 'S' : (!(UltraPermitted & (1 << TargetID)) ? 'F' : 'U')));
			SynchronousString[HostAdapter->SCSI_ID] = '#';
			SynchronousString[HostAdapter->MaxTargetDevices] = '\0';
		}
	} else
		SynchronousMessage = (SynchronousPermitted == 0 ? "Disabled" : "Enabled");
	WidePermitted = HostAdapter->WidePermitted & AllTargetsMask;
	if (WidePermitted == 0)
		WideMessage = "Disabled";
	else if (WidePermitted == AllTargetsMask)
		WideMessage = "Enabled";
	else {
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			WideString[TargetID] = ((WidePermitted & (1 << TargetID)) ? 'Y' : 'N');
		WideString[HostAdapter->SCSI_ID] = '#';
		WideString[HostAdapter->MaxTargetDevices] = '\0';
	}
	DisconnectPermitted = HostAdapter->DisconnectPermitted & AllTargetsMask;
	if (DisconnectPermitted == 0)
		DisconnectMessage = "Disabled";
	else if (DisconnectPermitted == AllTargetsMask)
		DisconnectMessage = "Enabled";
	else {
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			DisconnectString[TargetID] = ((DisconnectPermitted & (1 << TargetID)) ? 'Y' : 'N');
		DisconnectString[HostAdapter->SCSI_ID] = '#';
		DisconnectString[HostAdapter->MaxTargetDevices] = '\0';
	}
	TaggedQueuingPermitted = HostAdapter->TaggedQueuingPermitted & AllTargetsMask;
	if (TaggedQueuingPermitted == 0)
		TaggedQueuingMessage = "Disabled";
	else if (TaggedQueuingPermitted == AllTargetsMask)
		TaggedQueuingMessage = "Enabled";
	else {
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			TaggedQueuingString[TargetID] = ((TaggedQueuingPermitted & (1 << TargetID)) ? 'Y' : 'N');
		TaggedQueuingString[HostAdapter->SCSI_ID] = '#';
		TaggedQueuingString[HostAdapter->MaxTargetDevices] = '\0';
	}
	BusLogic_Info("  Synchronous Negotiation: %s, Wide Negotiation: %s\n", HostAdapter, SynchronousMessage, WideMessage);
	BusLogic_Info("  Disconnect/Reconnect: %s, Tagged Queuing: %s\n", HostAdapter, DisconnectMessage, TaggedQueuingMessage);
	if (BusLogic_MultiMasterHostAdapterP(HostAdapter)) {
		BusLogic_Info("  Scatter/Gather Limit: %d of %d segments, " "Mailboxes: %d\n", HostAdapter, HostAdapter->DriverScatterGatherLimit, HostAdapter->HostAdapterScatterGatherLimit, HostAdapter->MailboxCount);
		BusLogic_Info("  Driver Queue Depth: %d, " "Host Adapter Queue Depth: %d\n", HostAdapter, HostAdapter->DriverQueueDepth, HostAdapter->HostAdapterQueueDepth);
	} else
		BusLogic_Info("  Driver Queue Depth: %d, " "Scatter/Gather Limit: %d segments\n", HostAdapter, HostAdapter->DriverQueueDepth, HostAdapter->DriverScatterGatherLimit);
	BusLogic_Info("  Tagged Queue Depth: ", HostAdapter);
	CommonTaggedQueueDepth = true;
	for (TargetID = 1; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
		if (HostAdapter->QueueDepth[TargetID] != HostAdapter->QueueDepth[0]) {
			CommonTaggedQueueDepth = false;
			break;
		}
	if (CommonTaggedQueueDepth) {
		if (HostAdapter->QueueDepth[0] > 0)
			BusLogic_Info("%d", HostAdapter, HostAdapter->QueueDepth[0]);
		else
			BusLogic_Info("Automatic", HostAdapter);
	} else
		BusLogic_Info("Individual", HostAdapter);
	BusLogic_Info(", Untagged Queue Depth: %d\n", HostAdapter, HostAdapter->UntaggedQueueDepth);
	if (HostAdapter->TerminationInfoValid) {
		if (HostAdapter->HostWideSCSI)
			BusLogic_Info("  SCSI Bus Termination: %s", HostAdapter, (HostAdapter->LowByteTerminated ? (HostAdapter->HighByteTerminated ? "Both Enabled" : "Low Enabled")
										  : (HostAdapter->HighByteTerminated ? "High Enabled" : "Both Disabled")));
		else
			BusLogic_Info("  SCSI Bus Termination: %s", HostAdapter, (HostAdapter->LowByteTerminated ? "Enabled" : "Disabled"));
		if (HostAdapter->HostSupportsSCAM)
			BusLogic_Info(", SCAM: %s", HostAdapter, (HostAdapter->SCAM_Enabled ? (HostAdapter->SCAM_Level2 ? "Enabled, Level 2" : "Enabled, Level 1")
								  : "Disabled"));
		BusLogic_Info("\n", HostAdapter);
	}
	/*
	   Indicate reporting the Host Adapter configuration completed successfully.
	 */
	return true;
}


/*
  BusLogic_AcquireResources acquires the system resources necessary to use
  Host Adapter.
*/

static bool __init BusLogic_AcquireResources(struct BusLogic_HostAdapter *HostAdapter)
{
	if (HostAdapter->IRQ_Channel == 0) {
		BusLogic_Error("NO LEGAL INTERRUPT CHANNEL ASSIGNED - DETACHING\n", HostAdapter);
		return false;
	}
	/*
	   Acquire shared access to the IRQ Channel.
	 */
	if (request_irq(HostAdapter->IRQ_Channel, BusLogic_InterruptHandler, IRQF_SHARED, HostAdapter->FullModelName, HostAdapter) < 0) {
		BusLogic_Error("UNABLE TO ACQUIRE IRQ CHANNEL %d - DETACHING\n", HostAdapter, HostAdapter->IRQ_Channel);
		return false;
	}
	HostAdapter->IRQ_ChannelAcquired = true;
	/*
	   Acquire exclusive access to the DMA Channel.
	 */
	if (HostAdapter->DMA_Channel > 0) {
		if (request_dma(HostAdapter->DMA_Channel, HostAdapter->FullModelName) < 0) {
			BusLogic_Error("UNABLE TO ACQUIRE DMA CHANNEL %d - DETACHING\n", HostAdapter, HostAdapter->DMA_Channel);
			return false;
		}
		set_dma_mode(HostAdapter->DMA_Channel, DMA_MODE_CASCADE);
		enable_dma(HostAdapter->DMA_Channel);
		HostAdapter->DMA_ChannelAcquired = true;
	}
	/*
	   Indicate the System Resource Acquisition completed successfully,
	 */
	return true;
}


/*
  BusLogic_ReleaseResources releases any system resources previously acquired
  by BusLogic_AcquireResources.
*/

static void BusLogic_ReleaseResources(struct BusLogic_HostAdapter *HostAdapter)
{
	/*
	   Release shared access to the IRQ Channel.
	 */
	if (HostAdapter->IRQ_ChannelAcquired)
		free_irq(HostAdapter->IRQ_Channel, HostAdapter);
	/*
	   Release exclusive access to the DMA Channel.
	 */
	if (HostAdapter->DMA_ChannelAcquired)
		free_dma(HostAdapter->DMA_Channel);
	/*
	   Release any allocated memory structs not released elsewhere
	 */
	if (HostAdapter->MailboxSpace)
		pci_free_consistent(HostAdapter->PCI_Device, HostAdapter->MailboxSize, HostAdapter->MailboxSpace, HostAdapter->MailboxSpaceHandle);
	pci_dev_put(HostAdapter->PCI_Device);
	HostAdapter->MailboxSpace = NULL;
	HostAdapter->MailboxSpaceHandle = 0;
	HostAdapter->MailboxSize = 0;
}


/*
  BusLogic_InitializeHostAdapter initializes Host Adapter.  This is the only
  function called during SCSI Host Adapter detection which modifies the state
  of the Host Adapter from its initial power on or hard reset state.
*/

static bool BusLogic_InitializeHostAdapter(struct BusLogic_HostAdapter
					      *HostAdapter)
{
	struct BusLogic_ExtendedMailboxRequest ExtendedMailboxRequest;
	enum BusLogic_RoundRobinModeRequest RoundRobinModeRequest;
	enum BusLogic_SetCCBFormatRequest SetCCBFormatRequest;
	int TargetID;
	/*
	   Initialize the pointers to the first and last CCBs that are queued for
	   completion processing.
	 */
	HostAdapter->FirstCompletedCCB = NULL;
	HostAdapter->LastCompletedCCB = NULL;
	/*
	   Initialize the Bus Device Reset Pending CCB, Tagged Queuing Active,
	   Command Successful Flag, Active Commands, and Commands Since Reset
	   for each Target Device.
	 */
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		HostAdapter->BusDeviceResetPendingCCB[TargetID] = NULL;
		HostAdapter->TargetFlags[TargetID].TaggedQueuingActive = false;
		HostAdapter->TargetFlags[TargetID].CommandSuccessfulFlag = false;
		HostAdapter->ActiveCommands[TargetID] = 0;
		HostAdapter->CommandsSinceReset[TargetID] = 0;
	}
	/*
	   FlashPoint Host Adapters do not use Outgoing and Incoming Mailboxes.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter))
		goto Done;
	/*
	   Initialize the Outgoing and Incoming Mailbox pointers.
	 */
	HostAdapter->MailboxSize = HostAdapter->MailboxCount * (sizeof(struct BusLogic_OutgoingMailbox) + sizeof(struct BusLogic_IncomingMailbox));
	HostAdapter->MailboxSpace = pci_alloc_consistent(HostAdapter->PCI_Device, HostAdapter->MailboxSize, &HostAdapter->MailboxSpaceHandle);
	if (HostAdapter->MailboxSpace == NULL)
		return BusLogic_Failure(HostAdapter, "MAILBOX ALLOCATION");
	HostAdapter->FirstOutgoingMailbox = (struct BusLogic_OutgoingMailbox *) HostAdapter->MailboxSpace;
	HostAdapter->LastOutgoingMailbox = HostAdapter->FirstOutgoingMailbox + HostAdapter->MailboxCount - 1;
	HostAdapter->NextOutgoingMailbox = HostAdapter->FirstOutgoingMailbox;
	HostAdapter->FirstIncomingMailbox = (struct BusLogic_IncomingMailbox *) (HostAdapter->LastOutgoingMailbox + 1);
	HostAdapter->LastIncomingMailbox = HostAdapter->FirstIncomingMailbox + HostAdapter->MailboxCount - 1;
	HostAdapter->NextIncomingMailbox = HostAdapter->FirstIncomingMailbox;

	/*
	   Initialize the Outgoing and Incoming Mailbox structures.
	 */
	memset(HostAdapter->FirstOutgoingMailbox, 0, HostAdapter->MailboxCount * sizeof(struct BusLogic_OutgoingMailbox));
	memset(HostAdapter->FirstIncomingMailbox, 0, HostAdapter->MailboxCount * sizeof(struct BusLogic_IncomingMailbox));
	/*
	   Initialize the Host Adapter's Pointer to the Outgoing/Incoming Mailboxes.
	 */
	ExtendedMailboxRequest.MailboxCount = HostAdapter->MailboxCount;
	ExtendedMailboxRequest.BaseMailboxAddress = (u32) HostAdapter->MailboxSpaceHandle;
	if (BusLogic_Command(HostAdapter, BusLogic_InitializeExtendedMailbox, &ExtendedMailboxRequest, sizeof(ExtendedMailboxRequest), NULL, 0) < 0)
		return BusLogic_Failure(HostAdapter, "MAILBOX INITIALIZATION");
	/*
	   Enable Strict Round Robin Mode if supported by the Host Adapter.  In
	   Strict Round Robin Mode, the Host Adapter only looks at the next Outgoing
	   Mailbox for each new command, rather than scanning through all the
	   Outgoing Mailboxes to find any that have new commands in them.  Strict
	   Round Robin Mode is significantly more efficient.
	 */
	if (HostAdapter->StrictRoundRobinModeSupport) {
		RoundRobinModeRequest = BusLogic_StrictRoundRobinMode;
		if (BusLogic_Command(HostAdapter, BusLogic_EnableStrictRoundRobinMode, &RoundRobinModeRequest, sizeof(RoundRobinModeRequest), NULL, 0) < 0)
			return BusLogic_Failure(HostAdapter, "ENABLE STRICT ROUND ROBIN MODE");
	}
	/*
	   For Host Adapters that support Extended LUN Format CCBs, issue the Set CCB
	   Format command to allow 32 Logical Units per Target Device.
	 */
	if (HostAdapter->ExtendedLUNSupport) {
		SetCCBFormatRequest = BusLogic_ExtendedLUNFormatCCB;
		if (BusLogic_Command(HostAdapter, BusLogic_SetCCBFormat, &SetCCBFormatRequest, sizeof(SetCCBFormatRequest), NULL, 0) < 0)
			return BusLogic_Failure(HostAdapter, "SET CCB FORMAT");
	}
	/*
	   Announce Successful Initialization.
	 */
      Done:
	if (!HostAdapter->HostAdapterInitialized) {
		BusLogic_Info("*** %s Initialized Successfully ***\n", HostAdapter, HostAdapter->FullModelName);
		BusLogic_Info("\n", HostAdapter);
	} else
		BusLogic_Warning("*** %s Initialized Successfully ***\n", HostAdapter, HostAdapter->FullModelName);
	HostAdapter->HostAdapterInitialized = true;
	/*
	   Indicate the Host Adapter Initialization completed successfully.
	 */
	return true;
}


/*
  BusLogic_TargetDeviceInquiry inquires about the Target Devices accessible
  through Host Adapter.
*/

static bool __init BusLogic_TargetDeviceInquiry(struct BusLogic_HostAdapter
						   *HostAdapter)
{
	u16 InstalledDevices;
	u8 InstalledDevicesID0to7[8];
	struct BusLogic_SetupInformation SetupInformation;
	u8 SynchronousPeriod[BusLogic_MaxTargetDevices];
	unsigned char RequestedReplyLength;
	int TargetID;
	/*
	   Wait a few seconds between the Host Adapter Hard Reset which initiates
	   a SCSI Bus Reset and issuing any SCSI Commands.  Some SCSI devices get
	   confused if they receive SCSI Commands too soon after a SCSI Bus Reset.
	 */
	BusLogic_Delay(HostAdapter->BusSettleTime);
	/*
	   FlashPoint Host Adapters do not provide for Target Device Inquiry.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter))
		return true;
	/*
	   Inhibit the Target Device Inquiry if requested.
	 */
	if (HostAdapter->DriverOptions != NULL && HostAdapter->DriverOptions->LocalOptions.InhibitTargetInquiry)
		return true;
	/*
	   Issue the Inquire Target Devices command for host adapters with firmware
	   version 4.25 or later, or the Inquire Installed Devices ID 0 to 7 command
	   for older host adapters.  This is necessary to force Synchronous Transfer
	   Negotiation so that the Inquire Setup Information and Inquire Synchronous
	   Period commands will return valid data.  The Inquire Target Devices command
	   is preferable to Inquire Installed Devices ID 0 to 7 since it only probes
	   Logical Unit 0 of each Target Device.
	 */
	if (strcmp(HostAdapter->FirmwareVersion, "4.25") >= 0) {

		/*
		 * Issue a Inquire Target Devices command.  Inquire Target Devices only
		 * tests Logical Unit 0 of each Target Device unlike the Inquire Installed
		 * Devices commands which test Logical Units 0 - 7.  Two bytes are
		 * returned, where byte 0 bit 0 set indicates that Target Device 0 exists,
		 * and so on.
		 */

		if (BusLogic_Command(HostAdapter, BusLogic_InquireTargetDevices, NULL, 0, &InstalledDevices, sizeof(InstalledDevices))
		    != sizeof(InstalledDevices))
			return BusLogic_Failure(HostAdapter, "INQUIRE TARGET DEVICES");
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			HostAdapter->TargetFlags[TargetID].TargetExists = (InstalledDevices & (1 << TargetID) ? true : false);
	} else {

		/*
		 * Issue an Inquire Installed Devices command.  For each Target Device,
		 * a byte is returned where bit 0 set indicates that Logical Unit 0
		 * exists, bit 1 set indicates that Logical Unit 1 exists, and so on.
		 */

		if (BusLogic_Command(HostAdapter, BusLogic_InquireInstalledDevicesID0to7, NULL, 0, &InstalledDevicesID0to7, sizeof(InstalledDevicesID0to7))
		    != sizeof(InstalledDevicesID0to7))
			return BusLogic_Failure(HostAdapter, "INQUIRE INSTALLED DEVICES ID 0 TO 7");
		for (TargetID = 0; TargetID < 8; TargetID++)
			HostAdapter->TargetFlags[TargetID].TargetExists = (InstalledDevicesID0to7[TargetID] != 0 ? true : false);
	}
	/*
	   Issue the Inquire Setup Information command.
	 */
	RequestedReplyLength = sizeof(SetupInformation);
	if (BusLogic_Command(HostAdapter, BusLogic_InquireSetupInformation, &RequestedReplyLength, sizeof(RequestedReplyLength), &SetupInformation, sizeof(SetupInformation))
	    != sizeof(SetupInformation))
		return BusLogic_Failure(HostAdapter, "INQUIRE SETUP INFORMATION");
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
		HostAdapter->SynchronousOffset[TargetID] = (TargetID < 8 ? SetupInformation.SynchronousValuesID0to7[TargetID].Offset : SetupInformation.SynchronousValuesID8to15[TargetID - 8].Offset);
	if (strcmp(HostAdapter->FirmwareVersion, "5.06L") >= 0)
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			HostAdapter->TargetFlags[TargetID].WideTransfersActive = (TargetID < 8 ? (SetupInformation.WideTransfersActiveID0to7 & (1 << TargetID)
												  ? true : false)
										  : (SetupInformation.WideTransfersActiveID8to15 & (1 << (TargetID - 8))
										     ? true : false));
	/*
	   Issue the Inquire Synchronous Period command.
	 */
	if (HostAdapter->FirmwareVersion[0] >= '3') {

		/* Issue a Inquire Synchronous Period command.  For each Target Device,
		 * a byte is returned which represents the Synchronous Transfer Period
		 * in units of 10 nanoseconds.
		 */

		RequestedReplyLength = sizeof(SynchronousPeriod);
		if (BusLogic_Command(HostAdapter, BusLogic_InquireSynchronousPeriod, &RequestedReplyLength, sizeof(RequestedReplyLength), &SynchronousPeriod, sizeof(SynchronousPeriod))
		    != sizeof(SynchronousPeriod))
			return BusLogic_Failure(HostAdapter, "INQUIRE SYNCHRONOUS PERIOD");
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			HostAdapter->SynchronousPeriod[TargetID] = SynchronousPeriod[TargetID];
	} else
		for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
			if (SetupInformation.SynchronousValuesID0to7[TargetID].Offset > 0)
				HostAdapter->SynchronousPeriod[TargetID] = 20 + 5 * SetupInformation.SynchronousValuesID0to7[TargetID]
				    .TransferPeriod;
	/*
	   Indicate the Target Device Inquiry completed successfully.
	 */
	return true;
}

/*
  BusLogic_InitializeHostStructure initializes the fields in the SCSI Host
  structure.  The base, io_port, n_io_ports, irq, and dma_channel fields in the
  SCSI Host structure are intentionally left uninitialized, as this driver
  handles acquisition and release of these resources explicitly, as well as
  ensuring exclusive access to the Host Adapter hardware and data structures
  through explicit acquisition and release of the Host Adapter's Lock.
*/

static void __init BusLogic_InitializeHostStructure(struct BusLogic_HostAdapter
						    *HostAdapter, struct Scsi_Host *Host)
{
	Host->max_id = HostAdapter->MaxTargetDevices;
	Host->max_lun = HostAdapter->MaxLogicalUnits;
	Host->max_channel = 0;
	Host->unique_id = HostAdapter->IO_Address;
	Host->this_id = HostAdapter->SCSI_ID;
	Host->can_queue = HostAdapter->DriverQueueDepth;
	Host->sg_tablesize = HostAdapter->DriverScatterGatherLimit;
	Host->unchecked_isa_dma = HostAdapter->BounceBuffersRequired;
	Host->cmd_per_lun = HostAdapter->UntaggedQueueDepth;
}

/*
  BusLogic_SlaveConfigure will actually set the queue depth on individual
  scsi devices as they are permanently added to the device chain.  We
  shamelessly rip off the SelectQueueDepths code to make this work mostly
  like it used to.  Since we don't get called once at the end of the scan
  but instead get called for each device, we have to do things a bit
  differently.
*/
static int BusLogic_SlaveConfigure(struct scsi_device *Device)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) Device->host->hostdata;
	int TargetID = Device->id;
	int QueueDepth = HostAdapter->QueueDepth[TargetID];

	if (HostAdapter->TargetFlags[TargetID].TaggedQueuingSupported && (HostAdapter->TaggedQueuingPermitted & (1 << TargetID))) {
		if (QueueDepth == 0)
			QueueDepth = BusLogic_MaxAutomaticTaggedQueueDepth;
		HostAdapter->QueueDepth[TargetID] = QueueDepth;
		scsi_adjust_queue_depth(Device, MSG_SIMPLE_TAG, QueueDepth);
	} else {
		HostAdapter->TaggedQueuingPermitted &= ~(1 << TargetID);
		QueueDepth = HostAdapter->UntaggedQueueDepth;
		HostAdapter->QueueDepth[TargetID] = QueueDepth;
		scsi_adjust_queue_depth(Device, 0, QueueDepth);
	}
	QueueDepth = 0;
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++)
		if (HostAdapter->TargetFlags[TargetID].TargetExists) {
			QueueDepth += HostAdapter->QueueDepth[TargetID];
		}
	if (QueueDepth > HostAdapter->AllocatedCCBs)
		BusLogic_CreateAdditionalCCBs(HostAdapter, QueueDepth - HostAdapter->AllocatedCCBs, false);
	return 0;
}

/*
  BusLogic_DetectHostAdapter probes for BusLogic Host Adapters at the standard
  I/O Addresses where they may be located, initializing, registering, and
  reporting the configuration of each BusLogic Host Adapter it finds.  It
  returns the number of BusLogic Host Adapters successfully initialized and
  registered.
*/

static int __init BusLogic_init(void)
{
	int BusLogicHostAdapterCount = 0, DriverOptionsIndex = 0, ProbeIndex;
	struct BusLogic_HostAdapter *PrototypeHostAdapter;
	int ret = 0;

#ifdef MODULE
	if (BusLogic)
		BusLogic_Setup(BusLogic);
#endif

	if (BusLogic_ProbeOptions.NoProbe)
		return -ENODEV;
	BusLogic_ProbeInfoList =
	    kzalloc(BusLogic_MaxHostAdapters * sizeof(struct BusLogic_ProbeInfo), GFP_KERNEL);
	if (BusLogic_ProbeInfoList == NULL) {
		BusLogic_Error("BusLogic: Unable to allocate Probe Info List\n", NULL);
		return -ENOMEM;
	}

	PrototypeHostAdapter =
	    kzalloc(sizeof(struct BusLogic_HostAdapter), GFP_KERNEL);
	if (PrototypeHostAdapter == NULL) {
		kfree(BusLogic_ProbeInfoList);
		BusLogic_Error("BusLogic: Unable to allocate Prototype " "Host Adapter\n", NULL);
		return -ENOMEM;
	}

#ifdef MODULE
	if (BusLogic != NULL)
		BusLogic_Setup(BusLogic);
#endif
	BusLogic_InitializeProbeInfoList(PrototypeHostAdapter);
	for (ProbeIndex = 0; ProbeIndex < BusLogic_ProbeInfoCount; ProbeIndex++) {
		struct BusLogic_ProbeInfo *ProbeInfo = &BusLogic_ProbeInfoList[ProbeIndex];
		struct BusLogic_HostAdapter *HostAdapter = PrototypeHostAdapter;
		struct Scsi_Host *Host;
		if (ProbeInfo->IO_Address == 0)
			continue;
		memset(HostAdapter, 0, sizeof(struct BusLogic_HostAdapter));
		HostAdapter->HostAdapterType = ProbeInfo->HostAdapterType;
		HostAdapter->HostAdapterBusType = ProbeInfo->HostAdapterBusType;
		HostAdapter->IO_Address = ProbeInfo->IO_Address;
		HostAdapter->PCI_Address = ProbeInfo->PCI_Address;
		HostAdapter->Bus = ProbeInfo->Bus;
		HostAdapter->Device = ProbeInfo->Device;
		HostAdapter->PCI_Device = ProbeInfo->PCI_Device;
		HostAdapter->IRQ_Channel = ProbeInfo->IRQ_Channel;
		HostAdapter->AddressCount = BusLogic_HostAdapterAddressCount[HostAdapter->HostAdapterType];

		/*
		   Make sure region is free prior to probing.
		 */
		if (!request_region(HostAdapter->IO_Address, HostAdapter->AddressCount,
					"BusLogic"))
			continue;
		/*
		   Probe the Host Adapter.  If unsuccessful, abort further initialization.
		 */
		if (!BusLogic_ProbeHostAdapter(HostAdapter)) {
			release_region(HostAdapter->IO_Address, HostAdapter->AddressCount);
			continue;
		}
		/*
		   Hard Reset the Host Adapter.  If unsuccessful, abort further
		   initialization.
		 */
		if (!BusLogic_HardwareResetHostAdapter(HostAdapter, true)) {
			release_region(HostAdapter->IO_Address, HostAdapter->AddressCount);
			continue;
		}
		/*
		   Check the Host Adapter.  If unsuccessful, abort further initialization.
		 */
		if (!BusLogic_CheckHostAdapter(HostAdapter)) {
			release_region(HostAdapter->IO_Address, HostAdapter->AddressCount);
			continue;
		}
		/*
		   Initialize the Driver Options field if provided.
		 */
		if (DriverOptionsIndex < BusLogic_DriverOptionsCount)
			HostAdapter->DriverOptions = &BusLogic_DriverOptions[DriverOptionsIndex++];
		/*
		   Announce the Driver Version and Date, Author's Name, Copyright Notice,
		   and Electronic Mail Address.
		 */
		BusLogic_AnnounceDriver(HostAdapter);
		/*
		   Register the SCSI Host structure.
		 */

		Host = scsi_host_alloc(&Bus_Logic_template, sizeof(struct BusLogic_HostAdapter));
		if (Host == NULL) {
			release_region(HostAdapter->IO_Address, HostAdapter->AddressCount);
			continue;
		}
		HostAdapter = (struct BusLogic_HostAdapter *) Host->hostdata;
		memcpy(HostAdapter, PrototypeHostAdapter, sizeof(struct BusLogic_HostAdapter));
		HostAdapter->SCSI_Host = Host;
		HostAdapter->HostNumber = Host->host_no;
		/*
		   Add Host Adapter to the end of the list of registered BusLogic
		   Host Adapters.
		 */
		list_add_tail(&HostAdapter->host_list, &BusLogic_host_list);

		/*
		   Read the Host Adapter Configuration, Configure the Host Adapter,
		   Acquire the System Resources necessary to use the Host Adapter, then
		   Create the Initial CCBs, Initialize the Host Adapter, and finally
		   perform Target Device Inquiry.

		   From this point onward, any failure will be assumed to be due to a
		   problem with the Host Adapter, rather than due to having mistakenly
		   identified this port as belonging to a BusLogic Host Adapter.  The
		   I/O Address range will not be released, thereby preventing it from
		   being incorrectly identified as any other type of Host Adapter.
		 */
		if (BusLogic_ReadHostAdapterConfiguration(HostAdapter) &&
		    BusLogic_ReportHostAdapterConfiguration(HostAdapter) &&
		    BusLogic_AcquireResources(HostAdapter) &&
		    BusLogic_CreateInitialCCBs(HostAdapter) &&
		    BusLogic_InitializeHostAdapter(HostAdapter) &&
		    BusLogic_TargetDeviceInquiry(HostAdapter)) {
			/*
			   Initialization has been completed successfully.  Release and
			   re-register usage of the I/O Address range so that the Model
			   Name of the Host Adapter will appear, and initialize the SCSI
			   Host structure.
			 */
			release_region(HostAdapter->IO_Address,
				       HostAdapter->AddressCount);
			if (!request_region(HostAdapter->IO_Address,
					    HostAdapter->AddressCount,
					    HostAdapter->FullModelName)) {
				printk(KERN_WARNING
					"BusLogic: Release and re-register of "
					"port 0x%04lx failed \n",
					(unsigned long)HostAdapter->IO_Address);
				BusLogic_DestroyCCBs(HostAdapter);
				BusLogic_ReleaseResources(HostAdapter);
				list_del(&HostAdapter->host_list);
				scsi_host_put(Host);
				ret = -ENOMEM;
			} else {
				BusLogic_InitializeHostStructure(HostAdapter,
								 Host);
				if (scsi_add_host(Host, HostAdapter->PCI_Device
						? &HostAdapter->PCI_Device->dev
						  : NULL)) {
					printk(KERN_WARNING
					       "BusLogic: scsi_add_host()"
					       "failed!\n");
					BusLogic_DestroyCCBs(HostAdapter);
					BusLogic_ReleaseResources(HostAdapter);
					list_del(&HostAdapter->host_list);
					scsi_host_put(Host);
					ret = -ENODEV;
				} else {
					scsi_scan_host(Host);
					BusLogicHostAdapterCount++;
				}
			}
		} else {
			/*
			   An error occurred during Host Adapter Configuration Querying, Host
			   Adapter Configuration, Resource Acquisition, CCB Creation, Host
			   Adapter Initialization, or Target Device Inquiry, so remove Host
			   Adapter from the list of registered BusLogic Host Adapters, destroy
			   the CCBs, Release the System Resources, and Unregister the SCSI
			   Host.
			 */
			BusLogic_DestroyCCBs(HostAdapter);
			BusLogic_ReleaseResources(HostAdapter);
			list_del(&HostAdapter->host_list);
			scsi_host_put(Host);
			ret = -ENODEV;
		}
	}
	kfree(PrototypeHostAdapter);
	kfree(BusLogic_ProbeInfoList);
	BusLogic_ProbeInfoList = NULL;
	return ret;
}


/*
  BusLogic_ReleaseHostAdapter releases all resources previously acquired to
  support a specific Host Adapter, including the I/O Address range, and
  unregisters the BusLogic Host Adapter.
*/

static int __exit BusLogic_ReleaseHostAdapter(struct BusLogic_HostAdapter *HostAdapter)
{
	struct Scsi_Host *Host = HostAdapter->SCSI_Host;

	scsi_remove_host(Host);

	/*
	   FlashPoint Host Adapters must first be released by the FlashPoint
	   SCCB Manager.
	 */
	if (BusLogic_FlashPointHostAdapterP(HostAdapter))
		FlashPoint_ReleaseHostAdapter(HostAdapter->CardHandle);
	/*
	   Destroy the CCBs and release any system resources acquired to
	   support Host Adapter.
	 */
	BusLogic_DestroyCCBs(HostAdapter);
	BusLogic_ReleaseResources(HostAdapter);
	/*
	   Release usage of the I/O Address range.
	 */
	release_region(HostAdapter->IO_Address, HostAdapter->AddressCount);
	/*
	   Remove Host Adapter from the list of registered BusLogic Host Adapters.
	 */
	list_del(&HostAdapter->host_list);

	scsi_host_put(Host);
	return 0;
}


/*
  BusLogic_QueueCompletedCCB queues CCB for completion processing.
*/

static void BusLogic_QueueCompletedCCB(struct BusLogic_CCB *CCB)
{
	struct BusLogic_HostAdapter *HostAdapter = CCB->HostAdapter;
	CCB->Status = BusLogic_CCB_Completed;
	CCB->Next = NULL;
	if (HostAdapter->FirstCompletedCCB == NULL) {
		HostAdapter->FirstCompletedCCB = CCB;
		HostAdapter->LastCompletedCCB = CCB;
	} else {
		HostAdapter->LastCompletedCCB->Next = CCB;
		HostAdapter->LastCompletedCCB = CCB;
	}
	HostAdapter->ActiveCommands[CCB->TargetID]--;
}


/*
  BusLogic_ComputeResultCode computes a SCSI Subsystem Result Code from
  the Host Adapter Status and Target Device Status.
*/

static int BusLogic_ComputeResultCode(struct BusLogic_HostAdapter *HostAdapter, enum BusLogic_HostAdapterStatus HostAdapterStatus, enum BusLogic_TargetDeviceStatus TargetDeviceStatus)
{
	int HostStatus;
	switch (HostAdapterStatus) {
	case BusLogic_CommandCompletedNormally:
	case BusLogic_LinkedCommandCompleted:
	case BusLogic_LinkedCommandCompletedWithFlag:
		HostStatus = DID_OK;
		break;
	case BusLogic_SCSISelectionTimeout:
		HostStatus = DID_TIME_OUT;
		break;
	case BusLogic_InvalidOutgoingMailboxActionCode:
	case BusLogic_InvalidCommandOperationCode:
	case BusLogic_InvalidCommandParameter:
		BusLogic_Warning("BusLogic Driver Protocol Error 0x%02X\n", HostAdapter, HostAdapterStatus);
	case BusLogic_DataUnderRun:
	case BusLogic_DataOverRun:
	case BusLogic_UnexpectedBusFree:
	case BusLogic_LinkedCCBhasInvalidLUN:
	case BusLogic_AutoRequestSenseFailed:
	case BusLogic_TaggedQueuingMessageRejected:
	case BusLogic_UnsupportedMessageReceived:
	case BusLogic_HostAdapterHardwareFailed:
	case BusLogic_TargetDeviceReconnectedImproperly:
	case BusLogic_AbortQueueGenerated:
	case BusLogic_HostAdapterSoftwareError:
	case BusLogic_HostAdapterHardwareTimeoutError:
	case BusLogic_SCSIParityErrorDetected:
		HostStatus = DID_ERROR;
		break;
	case BusLogic_InvalidBusPhaseRequested:
	case BusLogic_TargetFailedResponseToATN:
	case BusLogic_HostAdapterAssertedRST:
	case BusLogic_OtherDeviceAssertedRST:
	case BusLogic_HostAdapterAssertedBusDeviceReset:
		HostStatus = DID_RESET;
		break;
	default:
		BusLogic_Warning("Unknown Host Adapter Status 0x%02X\n", HostAdapter, HostAdapterStatus);
		HostStatus = DID_ERROR;
		break;
	}
	return (HostStatus << 16) | TargetDeviceStatus;
}


/*
  BusLogic_ScanIncomingMailboxes scans the Incoming Mailboxes saving any
  Incoming Mailbox entries for completion processing.
*/

static void BusLogic_ScanIncomingMailboxes(struct BusLogic_HostAdapter *HostAdapter)
{
	/*
	   Scan through the Incoming Mailboxes in Strict Round Robin fashion, saving
	   any completed CCBs for further processing.  It is essential that for each
	   CCB and SCSI Command issued, command completion processing is performed
	   exactly once.  Therefore, only Incoming Mailboxes with completion code
	   Command Completed Without Error, Command Completed With Error, or Command
	   Aborted At Host Request are saved for completion processing.  When an
	   Incoming Mailbox has a completion code of Aborted Command Not Found, the
	   CCB had already completed or been aborted before the current Abort request
	   was processed, and so completion processing has already occurred and no
	   further action should be taken.
	 */
	struct BusLogic_IncomingMailbox *NextIncomingMailbox = HostAdapter->NextIncomingMailbox;
	enum BusLogic_CompletionCode CompletionCode;
	while ((CompletionCode = NextIncomingMailbox->CompletionCode) != BusLogic_IncomingMailboxFree) {
		/*
		   We are only allowed to do this because we limit our architectures we
		   run on to machines where bus_to_virt() actually works.  There *needs*
		   to be a dma_addr_to_virt() in the new PCI DMA mapping interface to
		   replace bus_to_virt() or else this code is going to become very
		   innefficient.
		 */
		struct BusLogic_CCB *CCB = (struct BusLogic_CCB *) Bus_to_Virtual(NextIncomingMailbox->CCB);
		if (CompletionCode != BusLogic_AbortedCommandNotFound) {
			if (CCB->Status == BusLogic_CCB_Active || CCB->Status == BusLogic_CCB_Reset) {
				/*
				   Save the Completion Code for this CCB and queue the CCB
				   for completion processing.
				 */
				CCB->CompletionCode = CompletionCode;
				BusLogic_QueueCompletedCCB(CCB);
			} else {
				/*
				   If a CCB ever appears in an Incoming Mailbox and is not marked
				   as status Active or Reset, then there is most likely a bug in
				   the Host Adapter firmware.
				 */
				BusLogic_Warning("Illegal CCB #%ld status %d in " "Incoming Mailbox\n", HostAdapter, CCB->SerialNumber, CCB->Status);
			}
		}
		NextIncomingMailbox->CompletionCode = BusLogic_IncomingMailboxFree;
		if (++NextIncomingMailbox > HostAdapter->LastIncomingMailbox)
			NextIncomingMailbox = HostAdapter->FirstIncomingMailbox;
	}
	HostAdapter->NextIncomingMailbox = NextIncomingMailbox;
}


/*
  BusLogic_ProcessCompletedCCBs iterates over the completed CCBs for Host
  Adapter setting the SCSI Command Result Codes, deallocating the CCBs, and
  calling the SCSI Subsystem Completion Routines.  The Host Adapter's Lock
  should already have been acquired by the caller.
*/

static void BusLogic_ProcessCompletedCCBs(struct BusLogic_HostAdapter *HostAdapter)
{
	if (HostAdapter->ProcessCompletedCCBsActive)
		return;
	HostAdapter->ProcessCompletedCCBsActive = true;
	while (HostAdapter->FirstCompletedCCB != NULL) {
		struct BusLogic_CCB *CCB = HostAdapter->FirstCompletedCCB;
		struct scsi_cmnd *Command = CCB->Command;
		HostAdapter->FirstCompletedCCB = CCB->Next;
		if (HostAdapter->FirstCompletedCCB == NULL)
			HostAdapter->LastCompletedCCB = NULL;
		/*
		   Process the Completed CCB.
		 */
		if (CCB->Opcode == BusLogic_BusDeviceReset) {
			int TargetID = CCB->TargetID;
			BusLogic_Warning("Bus Device Reset CCB #%ld to Target " "%d Completed\n", HostAdapter, CCB->SerialNumber, TargetID);
			BusLogic_IncrementErrorCounter(&HostAdapter->TargetStatistics[TargetID].BusDeviceResetsCompleted);
			HostAdapter->TargetFlags[TargetID].TaggedQueuingActive = false;
			HostAdapter->CommandsSinceReset[TargetID] = 0;
			HostAdapter->LastResetCompleted[TargetID] = jiffies;
			/*
			   Place CCB back on the Host Adapter's free list.
			 */
			BusLogic_DeallocateCCB(CCB);
#if 0				/* this needs to be redone different for new EH */
			/*
			   Bus Device Reset CCBs have the Command field non-NULL only when a
			   Bus Device Reset was requested for a Command that did not have a
			   currently active CCB in the Host Adapter (i.e., a Synchronous
			   Bus Device Reset), and hence would not have its Completion Routine
			   called otherwise.
			 */
			while (Command != NULL) {
				struct scsi_cmnd *NextCommand = Command->reset_chain;
				Command->reset_chain = NULL;
				Command->result = DID_RESET << 16;
				Command->scsi_done(Command);
				Command = NextCommand;
			}
#endif
			/*
			   Iterate over the CCBs for this Host Adapter performing completion
			   processing for any CCBs marked as Reset for this Target.
			 */
			for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
				if (CCB->Status == BusLogic_CCB_Reset && CCB->TargetID == TargetID) {
					Command = CCB->Command;
					BusLogic_DeallocateCCB(CCB);
					HostAdapter->ActiveCommands[TargetID]--;
					Command->result = DID_RESET << 16;
					Command->scsi_done(Command);
				}
			HostAdapter->BusDeviceResetPendingCCB[TargetID] = NULL;
		} else {
			/*
			   Translate the Completion Code, Host Adapter Status, and Target
			   Device Status into a SCSI Subsystem Result Code.
			 */
			switch (CCB->CompletionCode) {
			case BusLogic_IncomingMailboxFree:
			case BusLogic_AbortedCommandNotFound:
			case BusLogic_InvalidCCB:
				BusLogic_Warning("CCB #%ld to Target %d Impossible State\n", HostAdapter, CCB->SerialNumber, CCB->TargetID);
				break;
			case BusLogic_CommandCompletedWithoutError:
				HostAdapter->TargetStatistics[CCB->TargetID]
				    .CommandsCompleted++;
				HostAdapter->TargetFlags[CCB->TargetID]
				    .CommandSuccessfulFlag = true;
				Command->result = DID_OK << 16;
				break;
			case BusLogic_CommandAbortedAtHostRequest:
				BusLogic_Warning("CCB #%ld to Target %d Aborted\n", HostAdapter, CCB->SerialNumber, CCB->TargetID);
				BusLogic_IncrementErrorCounter(&HostAdapter->TargetStatistics[CCB->TargetID]
							       .CommandAbortsCompleted);
				Command->result = DID_ABORT << 16;
				break;
			case BusLogic_CommandCompletedWithError:
				Command->result = BusLogic_ComputeResultCode(HostAdapter, CCB->HostAdapterStatus, CCB->TargetDeviceStatus);
				if (CCB->HostAdapterStatus != BusLogic_SCSISelectionTimeout) {
					HostAdapter->TargetStatistics[CCB->TargetID]
					    .CommandsCompleted++;
					if (BusLogic_GlobalOptions.TraceErrors) {
						int i;
						BusLogic_Notice("CCB #%ld Target %d: Result %X Host "
								"Adapter Status %02X " "Target Status %02X\n", HostAdapter, CCB->SerialNumber, CCB->TargetID, Command->result, CCB->HostAdapterStatus, CCB->TargetDeviceStatus);
						BusLogic_Notice("CDB   ", HostAdapter);
						for (i = 0; i < CCB->CDB_Length; i++)
							BusLogic_Notice(" %02X", HostAdapter, CCB->CDB[i]);
						BusLogic_Notice("\n", HostAdapter);
						BusLogic_Notice("Sense ", HostAdapter);
						for (i = 0; i < CCB->SenseDataLength; i++)
							BusLogic_Notice(" %02X", HostAdapter, Command->sense_buffer[i]);
						BusLogic_Notice("\n", HostAdapter);
					}
				}
				break;
			}
			/*
			   When an INQUIRY command completes normally, save the
			   CmdQue (Tagged Queuing Supported) and WBus16 (16 Bit
			   Wide Data Transfers Supported) bits.
			 */
			if (CCB->CDB[0] == INQUIRY && CCB->CDB[1] == 0 && CCB->HostAdapterStatus == BusLogic_CommandCompletedNormally) {
				struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[CCB->TargetID];
				struct SCSI_Inquiry *InquiryResult =
					(struct SCSI_Inquiry *) scsi_sglist(Command);
				TargetFlags->TargetExists = true;
				TargetFlags->TaggedQueuingSupported = InquiryResult->CmdQue;
				TargetFlags->WideTransfersSupported = InquiryResult->WBus16;
			}
			/*
			   Place CCB back on the Host Adapter's free list.
			 */
			BusLogic_DeallocateCCB(CCB);
			/*
			   Call the SCSI Command Completion Routine.
			 */
			Command->scsi_done(Command);
		}
	}
	HostAdapter->ProcessCompletedCCBsActive = false;
}


/*
  BusLogic_InterruptHandler handles hardware interrupts from BusLogic Host
  Adapters.
*/

static irqreturn_t BusLogic_InterruptHandler(int IRQ_Channel, void *DeviceIdentifier)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) DeviceIdentifier;
	unsigned long ProcessorFlags;
	/*
	   Acquire exclusive access to Host Adapter.
	 */
	spin_lock_irqsave(HostAdapter->SCSI_Host->host_lock, ProcessorFlags);
	/*
	   Handle Interrupts appropriately for each Host Adapter type.
	 */
	if (BusLogic_MultiMasterHostAdapterP(HostAdapter)) {
		union BusLogic_InterruptRegister InterruptRegister;
		/*
		   Read the Host Adapter Interrupt Register.
		 */
		InterruptRegister.All = BusLogic_ReadInterruptRegister(HostAdapter);
		if (InterruptRegister.ir.InterruptValid) {
			/*
			   Acknowledge the interrupt and reset the Host Adapter
			   Interrupt Register.
			 */
			BusLogic_InterruptReset(HostAdapter);
			/*
			   Process valid External SCSI Bus Reset and Incoming Mailbox
			   Loaded Interrupts.  Command Complete Interrupts are noted,
			   and Outgoing Mailbox Available Interrupts are ignored, as
			   they are never enabled.
			 */
			if (InterruptRegister.ir.ExternalBusReset)
				HostAdapter->HostAdapterExternalReset = true;
			else if (InterruptRegister.ir.IncomingMailboxLoaded)
				BusLogic_ScanIncomingMailboxes(HostAdapter);
			else if (InterruptRegister.ir.CommandComplete)
				HostAdapter->HostAdapterCommandCompleted = true;
		}
	} else {
		/*
		   Check if there is a pending interrupt for this Host Adapter.
		 */
		if (FlashPoint_InterruptPending(HostAdapter->CardHandle))
			switch (FlashPoint_HandleInterrupt(HostAdapter->CardHandle)) {
			case FlashPoint_NormalInterrupt:
				break;
			case FlashPoint_ExternalBusReset:
				HostAdapter->HostAdapterExternalReset = true;
				break;
			case FlashPoint_InternalError:
				BusLogic_Warning("Internal FlashPoint Error detected" " - Resetting Host Adapter\n", HostAdapter);
				HostAdapter->HostAdapterInternalError = true;
				break;
			}
	}
	/*
	   Process any completed CCBs.
	 */
	if (HostAdapter->FirstCompletedCCB != NULL)
		BusLogic_ProcessCompletedCCBs(HostAdapter);
	/*
	   Reset the Host Adapter if requested.
	 */
	if (HostAdapter->HostAdapterExternalReset) {
		BusLogic_Warning("Resetting %s due to External SCSI Bus Reset\n", HostAdapter, HostAdapter->FullModelName);
		BusLogic_IncrementErrorCounter(&HostAdapter->ExternalHostAdapterResets);
		BusLogic_ResetHostAdapter(HostAdapter, false);
		HostAdapter->HostAdapterExternalReset = false;
	} else if (HostAdapter->HostAdapterInternalError) {
		BusLogic_Warning("Resetting %s due to Host Adapter Internal Error\n", HostAdapter, HostAdapter->FullModelName);
		BusLogic_IncrementErrorCounter(&HostAdapter->HostAdapterInternalErrors);
		BusLogic_ResetHostAdapter(HostAdapter, true);
		HostAdapter->HostAdapterInternalError = false;
	}
	/*
	   Release exclusive access to Host Adapter.
	 */
	spin_unlock_irqrestore(HostAdapter->SCSI_Host->host_lock, ProcessorFlags);
	return IRQ_HANDLED;
}


/*
  BusLogic_WriteOutgoingMailbox places CCB and Action Code into an Outgoing
  Mailbox for execution by Host Adapter.  The Host Adapter's Lock should
  already have been acquired by the caller.
*/

static bool BusLogic_WriteOutgoingMailbox(struct BusLogic_HostAdapter
					     *HostAdapter, enum BusLogic_ActionCode ActionCode, struct BusLogic_CCB *CCB)
{
	struct BusLogic_OutgoingMailbox *NextOutgoingMailbox;
	NextOutgoingMailbox = HostAdapter->NextOutgoingMailbox;
	if (NextOutgoingMailbox->ActionCode == BusLogic_OutgoingMailboxFree) {
		CCB->Status = BusLogic_CCB_Active;
		/*
		   The CCB field must be written before the Action Code field since
		   the Host Adapter is operating asynchronously and the locking code
		   does not protect against simultaneous access by the Host Adapter.
		 */
		NextOutgoingMailbox->CCB = CCB->DMA_Handle;
		NextOutgoingMailbox->ActionCode = ActionCode;
		BusLogic_StartMailboxCommand(HostAdapter);
		if (++NextOutgoingMailbox > HostAdapter->LastOutgoingMailbox)
			NextOutgoingMailbox = HostAdapter->FirstOutgoingMailbox;
		HostAdapter->NextOutgoingMailbox = NextOutgoingMailbox;
		if (ActionCode == BusLogic_MailboxStartCommand) {
			HostAdapter->ActiveCommands[CCB->TargetID]++;
			if (CCB->Opcode != BusLogic_BusDeviceReset)
				HostAdapter->TargetStatistics[CCB->TargetID].CommandsAttempted++;
		}
		return true;
	}
	return false;
}

/* Error Handling (EH) support */

static int BusLogic_host_reset(struct scsi_cmnd * SCpnt)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) SCpnt->device->host->hostdata;

	unsigned int id = SCpnt->device->id;
	struct BusLogic_TargetStatistics *stats = &HostAdapter->TargetStatistics[id];
	int rc;

	spin_lock_irq(SCpnt->device->host->host_lock);

	BusLogic_IncrementErrorCounter(&stats->HostAdapterResetsRequested);

	rc = BusLogic_ResetHostAdapter(HostAdapter, false);
	spin_unlock_irq(SCpnt->device->host->host_lock);
	return rc;
}

/*
  BusLogic_QueueCommand creates a CCB for Command and places it into an
  Outgoing Mailbox for execution by the associated Host Adapter.
*/

static int BusLogic_QueueCommand(struct scsi_cmnd *Command, void (*CompletionRoutine) (struct scsi_cmnd *))
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) Command->device->host->hostdata;
	struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[Command->device->id];
	struct BusLogic_TargetStatistics *TargetStatistics = HostAdapter->TargetStatistics;
	unsigned char *CDB = Command->cmnd;
	int CDB_Length = Command->cmd_len;
	int TargetID = Command->device->id;
	int LogicalUnit = Command->device->lun;
	int BufferLength = scsi_bufflen(Command);
	int Count;
	struct BusLogic_CCB *CCB;
	/*
	   SCSI REQUEST_SENSE commands will be executed automatically by the Host
	   Adapter for any errors, so they should not be executed explicitly unless
	   the Sense Data is zero indicating that no error occurred.
	 */
	if (CDB[0] == REQUEST_SENSE && Command->sense_buffer[0] != 0) {
		Command->result = DID_OK << 16;
		CompletionRoutine(Command);
		return 0;
	}
	/*
	   Allocate a CCB from the Host Adapter's free list.  In the unlikely event
	   that there are none available and memory allocation fails, wait 1 second
	   and try again.  If that fails, the Host Adapter is probably hung so signal
	   an error as a Host Adapter Hard Reset should be initiated soon.
	 */
	CCB = BusLogic_AllocateCCB(HostAdapter);
	if (CCB == NULL) {
		spin_unlock_irq(HostAdapter->SCSI_Host->host_lock);
		BusLogic_Delay(1);
		spin_lock_irq(HostAdapter->SCSI_Host->host_lock);
		CCB = BusLogic_AllocateCCB(HostAdapter);
		if (CCB == NULL) {
			Command->result = DID_ERROR << 16;
			CompletionRoutine(Command);
			return 0;
		}
	}

	/*
	   Initialize the fields in the BusLogic Command Control Block (CCB).
	 */
	Count = scsi_dma_map(Command);
	BUG_ON(Count < 0);
	if (Count) {
		struct scatterlist *sg;
		int i;

		CCB->Opcode = BusLogic_InitiatorCCB_ScatterGather;
		CCB->DataLength = Count * sizeof(struct BusLogic_ScatterGatherSegment);
		if (BusLogic_MultiMasterHostAdapterP(HostAdapter))
			CCB->DataPointer = (unsigned int) CCB->DMA_Handle + ((unsigned long) &CCB->ScatterGatherList - (unsigned long) CCB);
		else
			CCB->DataPointer = Virtual_to_32Bit_Virtual(CCB->ScatterGatherList);

		scsi_for_each_sg(Command, sg, Count, i) {
			CCB->ScatterGatherList[i].SegmentByteCount =
				sg_dma_len(sg);
			CCB->ScatterGatherList[i].SegmentDataPointer =
				sg_dma_address(sg);
		}
	} else if (!Count) {
		CCB->Opcode = BusLogic_InitiatorCCB;
		CCB->DataLength = BufferLength;
		CCB->DataPointer = 0;
	}

	switch (CDB[0]) {
	case READ_6:
	case READ_10:
		CCB->DataDirection = BusLogic_DataInLengthChecked;
		TargetStatistics[TargetID].ReadCommands++;
		BusLogic_IncrementByteCounter(&TargetStatistics[TargetID].TotalBytesRead, BufferLength);
		BusLogic_IncrementSizeBucket(TargetStatistics[TargetID].ReadCommandSizeBuckets, BufferLength);
		break;
	case WRITE_6:
	case WRITE_10:
		CCB->DataDirection = BusLogic_DataOutLengthChecked;
		TargetStatistics[TargetID].WriteCommands++;
		BusLogic_IncrementByteCounter(&TargetStatistics[TargetID].TotalBytesWritten, BufferLength);
		BusLogic_IncrementSizeBucket(TargetStatistics[TargetID].WriteCommandSizeBuckets, BufferLength);
		break;
	default:
		CCB->DataDirection = BusLogic_UncheckedDataTransfer;
		break;
	}
	CCB->CDB_Length = CDB_Length;
	CCB->HostAdapterStatus = 0;
	CCB->TargetDeviceStatus = 0;
	CCB->TargetID = TargetID;
	CCB->LogicalUnit = LogicalUnit;
	CCB->TagEnable = false;
	CCB->LegacyTagEnable = false;
	/*
	   BusLogic recommends that after a Reset the first couple of commands that
	   are sent to a Target Device be sent in a non Tagged Queue fashion so that
	   the Host Adapter and Target Device can establish Synchronous and Wide
	   Transfer before Queue Tag messages can interfere with the Synchronous and
	   Wide Negotiation messages.  By waiting to enable Tagged Queuing until after
	   the first BusLogic_MaxTaggedQueueDepth commands have been queued, it is
	   assured that after a Reset any pending commands are requeued before Tagged
	   Queuing is enabled and that the Tagged Queuing message will not occur while
	   the partition table is being printed.  In addition, some devices do not
	   properly handle the transition from non-tagged to tagged commands, so it is
	   necessary to wait until there are no pending commands for a target device
	   before queuing tagged commands.
	 */
	if (HostAdapter->CommandsSinceReset[TargetID]++ >=
	    BusLogic_MaxTaggedQueueDepth && !TargetFlags->TaggedQueuingActive && HostAdapter->ActiveCommands[TargetID] == 0 && TargetFlags->TaggedQueuingSupported && (HostAdapter->TaggedQueuingPermitted & (1 << TargetID))) {
		TargetFlags->TaggedQueuingActive = true;
		BusLogic_Notice("Tagged Queuing now active for Target %d\n", HostAdapter, TargetID);
	}
	if (TargetFlags->TaggedQueuingActive) {
		enum BusLogic_QueueTag QueueTag = BusLogic_SimpleQueueTag;
		/*
		   When using Tagged Queuing with Simple Queue Tags, it appears that disk
		   drive controllers do not guarantee that a queued command will not
		   remain in a disconnected state indefinitely if commands that read or
		   write nearer the head position continue to arrive without interruption.
		   Therefore, for each Target Device this driver keeps track of the last
		   time either the queue was empty or an Ordered Queue Tag was issued.  If
		   more than 4 seconds (one fifth of the 20 second disk timeout) have
		   elapsed since this last sequence point, this command will be issued
		   with an Ordered Queue Tag rather than a Simple Queue Tag, which forces
		   the Target Device to complete all previously queued commands before
		   this command may be executed.
		 */
		if (HostAdapter->ActiveCommands[TargetID] == 0)
			HostAdapter->LastSequencePoint[TargetID] = jiffies;
		else if (time_after(jiffies, HostAdapter->LastSequencePoint[TargetID] + 4 * HZ)) {
			HostAdapter->LastSequencePoint[TargetID] = jiffies;
			QueueTag = BusLogic_OrderedQueueTag;
		}
		if (HostAdapter->ExtendedLUNSupport) {
			CCB->TagEnable = true;
			CCB->QueueTag = QueueTag;
		} else {
			CCB->LegacyTagEnable = true;
			CCB->LegacyQueueTag = QueueTag;
		}
	}
	memcpy(CCB->CDB, CDB, CDB_Length);
	CCB->SenseDataLength = SCSI_SENSE_BUFFERSIZE;
	CCB->SenseDataPointer = pci_map_single(HostAdapter->PCI_Device, Command->sense_buffer, CCB->SenseDataLength, PCI_DMA_FROMDEVICE);
	CCB->Command = Command;
	Command->scsi_done = CompletionRoutine;
	if (BusLogic_MultiMasterHostAdapterP(HostAdapter)) {
		/*
		   Place the CCB in an Outgoing Mailbox.  The higher levels of the SCSI
		   Subsystem should not attempt to queue more commands than can be placed
		   in Outgoing Mailboxes, so there should always be one free.  In the
		   unlikely event that there are none available, wait 1 second and try
		   again.  If that fails, the Host Adapter is probably hung so signal an
		   error as a Host Adapter Hard Reset should be initiated soon.
		 */
		if (!BusLogic_WriteOutgoingMailbox(HostAdapter, BusLogic_MailboxStartCommand, CCB)) {
			spin_unlock_irq(HostAdapter->SCSI_Host->host_lock);
			BusLogic_Warning("Unable to write Outgoing Mailbox - " "Pausing for 1 second\n", HostAdapter);
			BusLogic_Delay(1);
			spin_lock_irq(HostAdapter->SCSI_Host->host_lock);
			if (!BusLogic_WriteOutgoingMailbox(HostAdapter, BusLogic_MailboxStartCommand, CCB)) {
				BusLogic_Warning("Still unable to write Outgoing Mailbox - " "Host Adapter Dead?\n", HostAdapter);
				BusLogic_DeallocateCCB(CCB);
				Command->result = DID_ERROR << 16;
				Command->scsi_done(Command);
			}
		}
	} else {
		/*
		   Call the FlashPoint SCCB Manager to start execution of the CCB.
		 */
		CCB->Status = BusLogic_CCB_Active;
		HostAdapter->ActiveCommands[TargetID]++;
		TargetStatistics[TargetID].CommandsAttempted++;
		FlashPoint_StartCCB(HostAdapter->CardHandle, CCB);
		/*
		   The Command may have already completed and BusLogic_QueueCompletedCCB
		   been called, or it may still be pending.
		 */
		if (CCB->Status == BusLogic_CCB_Completed)
			BusLogic_ProcessCompletedCCBs(HostAdapter);
	}
	return 0;
}


#if 0
/*
  BusLogic_AbortCommand aborts Command if possible.
*/

static int BusLogic_AbortCommand(struct scsi_cmnd *Command)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) Command->device->host->hostdata;

	int TargetID = Command->device->id;
	struct BusLogic_CCB *CCB;
	BusLogic_IncrementErrorCounter(&HostAdapter->TargetStatistics[TargetID].CommandAbortsRequested);
	/*
	   Attempt to find an Active CCB for this Command.  If no Active CCB for this
	   Command is found, then no Abort is necessary.
	 */
	for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
		if (CCB->Command == Command)
			break;
	if (CCB == NULL) {
		BusLogic_Warning("Unable to Abort Command to Target %d - " "No CCB Found\n", HostAdapter, TargetID);
		return SUCCESS;
	} else if (CCB->Status == BusLogic_CCB_Completed) {
		BusLogic_Warning("Unable to Abort Command to Target %d - " "CCB Completed\n", HostAdapter, TargetID);
		return SUCCESS;
	} else if (CCB->Status == BusLogic_CCB_Reset) {
		BusLogic_Warning("Unable to Abort Command to Target %d - " "CCB Reset\n", HostAdapter, TargetID);
		return SUCCESS;
	}
	if (BusLogic_MultiMasterHostAdapterP(HostAdapter)) {
		/*
		   Attempt to Abort this CCB.  MultiMaster Firmware versions prior to 5.xx
		   do not generate Abort Tag messages, but only generate the non-tagged
		   Abort message.  Since non-tagged commands are not sent by the Host
		   Adapter until the queue of outstanding tagged commands has completed,
		   and the Abort message is treated as a non-tagged command, it is
		   effectively impossible to abort commands when Tagged Queuing is active.
		   Firmware version 5.xx does generate Abort Tag messages, so it is
		   possible to abort commands when Tagged Queuing is active.
		 */
		if (HostAdapter->TargetFlags[TargetID].TaggedQueuingActive && HostAdapter->FirmwareVersion[0] < '5') {
			BusLogic_Warning("Unable to Abort CCB #%ld to Target %d - " "Abort Tag Not Supported\n", HostAdapter, CCB->SerialNumber, TargetID);
			return FAILURE;
		} else if (BusLogic_WriteOutgoingMailbox(HostAdapter, BusLogic_MailboxAbortCommand, CCB)) {
			BusLogic_Warning("Aborting CCB #%ld to Target %d\n", HostAdapter, CCB->SerialNumber, TargetID);
			BusLogic_IncrementErrorCounter(&HostAdapter->TargetStatistics[TargetID].CommandAbortsAttempted);
			return SUCCESS;
		} else {
			BusLogic_Warning("Unable to Abort CCB #%ld to Target %d - " "No Outgoing Mailboxes\n", HostAdapter, CCB->SerialNumber, TargetID);
			return FAILURE;
		}
	} else {
		/*
		   Call the FlashPoint SCCB Manager to abort execution of the CCB.
		 */
		BusLogic_Warning("Aborting CCB #%ld to Target %d\n", HostAdapter, CCB->SerialNumber, TargetID);
		BusLogic_IncrementErrorCounter(&HostAdapter->TargetStatistics[TargetID].CommandAbortsAttempted);
		FlashPoint_AbortCCB(HostAdapter->CardHandle, CCB);
		/*
		   The Abort may have already been completed and
		   BusLogic_QueueCompletedCCB been called, or it
		   may still be pending.
		 */
		if (CCB->Status == BusLogic_CCB_Completed) {
			BusLogic_ProcessCompletedCCBs(HostAdapter);
		}
		return SUCCESS;
	}
	return SUCCESS;
}

#endif
/*
  BusLogic_ResetHostAdapter resets Host Adapter if possible, marking all
  currently executing SCSI Commands as having been Reset.
*/

static int BusLogic_ResetHostAdapter(struct BusLogic_HostAdapter *HostAdapter, bool HardReset)
{
	struct BusLogic_CCB *CCB;
	int TargetID;

	/*
	 * Attempt to Reset and Reinitialize the Host Adapter.
	 */

	if (!(BusLogic_HardwareResetHostAdapter(HostAdapter, HardReset) && BusLogic_InitializeHostAdapter(HostAdapter))) {
		BusLogic_Error("Resetting %s Failed\n", HostAdapter, HostAdapter->FullModelName);
		return FAILURE;
	}

	/*
	 * Deallocate all currently executing CCBs.
	 */

	for (CCB = HostAdapter->All_CCBs; CCB != NULL; CCB = CCB->NextAll)
		if (CCB->Status == BusLogic_CCB_Active)
			BusLogic_DeallocateCCB(CCB);
	/*
	 * Wait a few seconds between the Host Adapter Hard Reset which
	 * initiates a SCSI Bus Reset and issuing any SCSI Commands.  Some
	 * SCSI devices get confused if they receive SCSI Commands too soon
	 * after a SCSI Bus Reset.
	 */

	if (HardReset) {
		spin_unlock_irq(HostAdapter->SCSI_Host->host_lock);
		BusLogic_Delay(HostAdapter->BusSettleTime);
		spin_lock_irq(HostAdapter->SCSI_Host->host_lock);
	}

	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		HostAdapter->LastResetAttempted[TargetID] = jiffies;
		HostAdapter->LastResetCompleted[TargetID] = jiffies;
	}
	return SUCCESS;
}

/*
  BusLogic_BIOSDiskParameters returns the Heads/Sectors/Cylinders BIOS Disk
  Parameters for Disk.  The default disk geometry is 64 heads, 32 sectors, and
  the appropriate number of cylinders so as not to exceed drive capacity.  In
  order for disks equal to or larger than 1 GB to be addressable by the BIOS
  without exceeding the BIOS limitation of 1024 cylinders, Extended Translation
  may be enabled in AutoSCSI on FlashPoint Host Adapters and on "W" and "C"
  series MultiMaster Host Adapters, or by a dip switch setting on "S" and "A"
  series MultiMaster Host Adapters.  With Extended Translation enabled, drives
  between 1 GB inclusive and 2 GB exclusive are given a disk geometry of 128
  heads and 32 sectors, and drives above 2 GB inclusive are given a disk
  geometry of 255 heads and 63 sectors.  However, if the BIOS detects that the
  Extended Translation setting does not match the geometry in the partition
  table, then the translation inferred from the partition table will be used by
  the BIOS, and a warning may be displayed.
*/

static int BusLogic_BIOSDiskParameters(struct scsi_device *sdev, struct block_device *Device, sector_t capacity, int *Parameters)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) sdev->host->hostdata;
	struct BIOS_DiskParameters *DiskParameters = (struct BIOS_DiskParameters *) Parameters;
	unsigned char *buf;
	if (HostAdapter->ExtendedTranslationEnabled && capacity >= 2 * 1024 * 1024 /* 1 GB in 512 byte sectors */ ) {
		if (capacity >= 4 * 1024 * 1024 /* 2 GB in 512 byte sectors */ ) {
			DiskParameters->Heads = 255;
			DiskParameters->Sectors = 63;
		} else {
			DiskParameters->Heads = 128;
			DiskParameters->Sectors = 32;
		}
	} else {
		DiskParameters->Heads = 64;
		DiskParameters->Sectors = 32;
	}
	DiskParameters->Cylinders = (unsigned long) capacity / (DiskParameters->Heads * DiskParameters->Sectors);
	buf = scsi_bios_ptable(Device);
	if (buf == NULL)
		return 0;
	/*
	   If the boot sector partition table flag is valid, search for a partition
	   table entry whose end_head matches one of the standard BusLogic geometry
	   translations (64/32, 128/32, or 255/63).
	 */
	if (*(unsigned short *) (buf + 64) == 0xAA55) {
		struct partition *FirstPartitionEntry = (struct partition *) buf;
		struct partition *PartitionEntry = FirstPartitionEntry;
		int SavedCylinders = DiskParameters->Cylinders, PartitionNumber;
		unsigned char PartitionEntryEndHead = 0, PartitionEntryEndSector = 0;
		for (PartitionNumber = 0; PartitionNumber < 4; PartitionNumber++) {
			PartitionEntryEndHead = PartitionEntry->end_head;
			PartitionEntryEndSector = PartitionEntry->end_sector & 0x3F;
			if (PartitionEntryEndHead == 64 - 1) {
				DiskParameters->Heads = 64;
				DiskParameters->Sectors = 32;
				break;
			} else if (PartitionEntryEndHead == 128 - 1) {
				DiskParameters->Heads = 128;
				DiskParameters->Sectors = 32;
				break;
			} else if (PartitionEntryEndHead == 255 - 1) {
				DiskParameters->Heads = 255;
				DiskParameters->Sectors = 63;
				break;
			}
			PartitionEntry++;
		}
		if (PartitionNumber == 4) {
			PartitionEntryEndHead = FirstPartitionEntry->end_head;
			PartitionEntryEndSector = FirstPartitionEntry->end_sector & 0x3F;
		}
		DiskParameters->Cylinders = (unsigned long) capacity / (DiskParameters->Heads * DiskParameters->Sectors);
		if (PartitionNumber < 4 && PartitionEntryEndSector == DiskParameters->Sectors) {
			if (DiskParameters->Cylinders != SavedCylinders)
				BusLogic_Warning("Adopting Geometry %d/%d from Partition Table\n", HostAdapter, DiskParameters->Heads, DiskParameters->Sectors);
		} else if (PartitionEntryEndHead > 0 || PartitionEntryEndSector > 0) {
			BusLogic_Warning("Warning: Partition Table appears to " "have Geometry %d/%d which is\n", HostAdapter, PartitionEntryEndHead + 1, PartitionEntryEndSector);
			BusLogic_Warning("not compatible with current BusLogic " "Host Adapter Geometry %d/%d\n", HostAdapter, DiskParameters->Heads, DiskParameters->Sectors);
		}
	}
	kfree(buf);
	return 0;
}


/*
  BugLogic_ProcDirectoryInfo implements /proc/scsi/BusLogic/<N>.
*/

static int BusLogic_ProcDirectoryInfo(struct Scsi_Host *shost, char *ProcBuffer, char **StartPointer, off_t Offset, int BytesAvailable, int WriteFlag)
{
	struct BusLogic_HostAdapter *HostAdapter = (struct BusLogic_HostAdapter *) shost->hostdata;
	struct BusLogic_TargetStatistics *TargetStatistics;
	int TargetID, Length;
	char *Buffer;

	TargetStatistics = HostAdapter->TargetStatistics;
	if (WriteFlag) {
		HostAdapter->ExternalHostAdapterResets = 0;
		HostAdapter->HostAdapterInternalErrors = 0;
		memset(TargetStatistics, 0, BusLogic_MaxTargetDevices * sizeof(struct BusLogic_TargetStatistics));
		return 0;
	}
	Buffer = HostAdapter->MessageBuffer;
	Length = HostAdapter->MessageBufferLength;
	Length += sprintf(&Buffer[Length], "\n\
Current Driver Queue Depth:	%d\n\
Currently Allocated CCBs:	%d\n", HostAdapter->DriverQueueDepth, HostAdapter->AllocatedCCBs);
	Length += sprintf(&Buffer[Length], "\n\n\
			   DATA TRANSFER STATISTICS\n\
\n\
Target	Tagged Queuing	Queue Depth  Active  Attempted	Completed\n\
======	==============	===========  ======  =========	=========\n");
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[TargetID];
		if (!TargetFlags->TargetExists)
			continue;
		Length += sprintf(&Buffer[Length], "  %2d	%s", TargetID, (TargetFlags->TaggedQueuingSupported ? (TargetFlags->TaggedQueuingActive ? "    Active" : (HostAdapter->TaggedQueuingPermitted & (1 << TargetID)
																				    ? "  Permitted" : "   Disabled"))
									  : "Not Supported"));
		Length += sprintf(&Buffer[Length],
				  "	    %3d       %3u    %9u	%9u\n", HostAdapter->QueueDepth[TargetID], HostAdapter->ActiveCommands[TargetID], TargetStatistics[TargetID].CommandsAttempted, TargetStatistics[TargetID].CommandsCompleted);
	}
	Length += sprintf(&Buffer[Length], "\n\
Target  Read Commands  Write Commands   Total Bytes Read    Total Bytes Written\n\
======  =============  ==============  ===================  ===================\n");
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[TargetID];
		if (!TargetFlags->TargetExists)
			continue;
		Length += sprintf(&Buffer[Length], "  %2d	  %9u	 %9u", TargetID, TargetStatistics[TargetID].ReadCommands, TargetStatistics[TargetID].WriteCommands);
		if (TargetStatistics[TargetID].TotalBytesRead.Billions > 0)
			Length += sprintf(&Buffer[Length], "     %9u%09u", TargetStatistics[TargetID].TotalBytesRead.Billions, TargetStatistics[TargetID].TotalBytesRead.Units);
		else
			Length += sprintf(&Buffer[Length], "		%9u", TargetStatistics[TargetID].TotalBytesRead.Units);
		if (TargetStatistics[TargetID].TotalBytesWritten.Billions > 0)
			Length += sprintf(&Buffer[Length], "   %9u%09u\n", TargetStatistics[TargetID].TotalBytesWritten.Billions, TargetStatistics[TargetID].TotalBytesWritten.Units);
		else
			Length += sprintf(&Buffer[Length], "	     %9u\n", TargetStatistics[TargetID].TotalBytesWritten.Units);
	}
	Length += sprintf(&Buffer[Length], "\n\
Target  Command    0-1KB      1-2KB      2-4KB      4-8KB     8-16KB\n\
======  =======  =========  =========  =========  =========  =========\n");
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[TargetID];
		if (!TargetFlags->TargetExists)
			continue;
		Length +=
		    sprintf(&Buffer[Length],
			    "  %2d	 Read	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
			    TargetStatistics[TargetID].ReadCommandSizeBuckets[0],
			    TargetStatistics[TargetID].ReadCommandSizeBuckets[1], TargetStatistics[TargetID].ReadCommandSizeBuckets[2], TargetStatistics[TargetID].ReadCommandSizeBuckets[3], TargetStatistics[TargetID].ReadCommandSizeBuckets[4]);
		Length +=
		    sprintf(&Buffer[Length],
			    "  %2d	 Write	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
			    TargetStatistics[TargetID].WriteCommandSizeBuckets[0],
			    TargetStatistics[TargetID].WriteCommandSizeBuckets[1], TargetStatistics[TargetID].WriteCommandSizeBuckets[2], TargetStatistics[TargetID].WriteCommandSizeBuckets[3], TargetStatistics[TargetID].WriteCommandSizeBuckets[4]);
	}
	Length += sprintf(&Buffer[Length], "\n\
Target  Command   16-32KB    32-64KB   64-128KB   128-256KB   256KB+\n\
======  =======  =========  =========  =========  =========  =========\n");
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[TargetID];
		if (!TargetFlags->TargetExists)
			continue;
		Length +=
		    sprintf(&Buffer[Length],
			    "  %2d	 Read	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
			    TargetStatistics[TargetID].ReadCommandSizeBuckets[5],
			    TargetStatistics[TargetID].ReadCommandSizeBuckets[6], TargetStatistics[TargetID].ReadCommandSizeBuckets[7], TargetStatistics[TargetID].ReadCommandSizeBuckets[8], TargetStatistics[TargetID].ReadCommandSizeBuckets[9]);
		Length +=
		    sprintf(&Buffer[Length],
			    "  %2d	 Write	 %9u  %9u  %9u  %9u  %9u\n", TargetID,
			    TargetStatistics[TargetID].WriteCommandSizeBuckets[5],
			    TargetStatistics[TargetID].WriteCommandSizeBuckets[6], TargetStatistics[TargetID].WriteCommandSizeBuckets[7], TargetStatistics[TargetID].WriteCommandSizeBuckets[8], TargetStatistics[TargetID].WriteCommandSizeBuckets[9]);
	}
	Length += sprintf(&Buffer[Length], "\n\n\
			   ERROR RECOVERY STATISTICS\n\
\n\
	  Command Aborts      Bus Device Resets	  Host Adapter Resets\n\
Target	Requested Completed  Requested Completed  Requested Completed\n\
  ID	\\\\\\\\ Attempted ////  \\\\\\\\ Attempted ////  \\\\\\\\ Attempted ////\n\
======	 ===== ===== =====    ===== ===== =====	   ===== ===== =====\n");
	for (TargetID = 0; TargetID < HostAdapter->MaxTargetDevices; TargetID++) {
		struct BusLogic_TargetFlags *TargetFlags = &HostAdapter->TargetFlags[TargetID];
		if (!TargetFlags->TargetExists)
			continue;
		Length += sprintf(&Buffer[Length], "\
  %2d	 %5d %5d %5d    %5d %5d %5d	   %5d %5d %5d\n", TargetID, TargetStatistics[TargetID].CommandAbortsRequested, TargetStatistics[TargetID].CommandAbortsAttempted, TargetStatistics[TargetID].CommandAbortsCompleted, TargetStatistics[TargetID].BusDeviceResetsRequested, TargetStatistics[TargetID].BusDeviceResetsAttempted, TargetStatistics[TargetID].BusDeviceResetsCompleted, TargetStatistics[TargetID].HostAdapterResetsRequested, TargetStatistics[TargetID].HostAdapterResetsAttempted, TargetStatistics[TargetID].HostAdapterResetsCompleted);
	}
	Length += sprintf(&Buffer[Length], "\nExternal Host Adapter Resets: %d\n", HostAdapter->ExternalHostAdapterResets);
	Length += sprintf(&Buffer[Length], "Host Adapter Internal Errors: %d\n", HostAdapter->HostAdapterInternalErrors);
	if (Length >= BusLogic_MessageBufferSize)
		BusLogic_Error("Message Buffer length %d exceeds size %d\n", HostAdapter, Length, BusLogic_MessageBufferSize);
	if ((Length -= Offset) <= 0)
		return 0;
	if (Length >= BytesAvailable)
		Length = BytesAvailable;
	memcpy(ProcBuffer, HostAdapter->MessageBuffer + Offset, Length);
	*StartPointer = ProcBuffer;
	return Length;
}


/*
  BusLogic_Message prints Driver Messages.
*/

static void BusLogic_Message(enum BusLogic_MessageLevel MessageLevel, char *Format, struct BusLogic_HostAdapter *HostAdapter, ...)
{
	static char Buffer[BusLogic_LineBufferSize];
	static bool BeginningOfLine = true;
	va_list Arguments;
	int Length = 0;
	va_start(Arguments, HostAdapter);
	Length = vsprintf(Buffer, Format, Arguments);
	va_end(Arguments);
	if (MessageLevel == BusLogic_AnnounceLevel) {
		static int AnnouncementLines = 0;
		strcpy(&HostAdapter->MessageBuffer[HostAdapter->MessageBufferLength], Buffer);
		HostAdapter->MessageBufferLength += Length;
		if (++AnnouncementLines <= 2)
			printk("%sscsi: %s", BusLogic_MessageLevelMap[MessageLevel], Buffer);
	} else if (MessageLevel == BusLogic_InfoLevel) {
		strcpy(&HostAdapter->MessageBuffer[HostAdapter->MessageBufferLength], Buffer);
		HostAdapter->MessageBufferLength += Length;
		if (BeginningOfLine) {
			if (Buffer[0] != '\n' || Length > 1)
				printk("%sscsi%d: %s", BusLogic_MessageLevelMap[MessageLevel], HostAdapter->HostNumber, Buffer);
		} else
			printk("%s", Buffer);
	} else {
		if (BeginningOfLine) {
			if (HostAdapter != NULL && HostAdapter->HostAdapterInitialized)
				printk("%sscsi%d: %s", BusLogic_MessageLevelMap[MessageLevel], HostAdapter->HostNumber, Buffer);
			else
				printk("%s%s", BusLogic_MessageLevelMap[MessageLevel], Buffer);
		} else
			printk("%s", Buffer);
	}
	BeginningOfLine = (Buffer[Length - 1] == '\n');
}


/*
  BusLogic_ParseKeyword parses an individual option keyword.  It returns true
  and updates the pointer if the keyword is recognized and false otherwise.
*/

static bool __init BusLogic_ParseKeyword(char **StringPointer, char *Keyword)
{
	char *Pointer = *StringPointer;
	while (*Keyword != '\0') {
		char StringChar = *Pointer++;
		char KeywordChar = *Keyword++;
		if (StringChar >= 'A' && StringChar <= 'Z')
			StringChar += 'a' - 'Z';
		if (KeywordChar >= 'A' && KeywordChar <= 'Z')
			KeywordChar += 'a' - 'Z';
		if (StringChar != KeywordChar)
			return false;
	}
	*StringPointer = Pointer;
	return true;
}


/*
  BusLogic_ParseDriverOptions handles processing of BusLogic Driver Options
  specifications.

  BusLogic Driver Options may be specified either via the Linux Kernel Command
  Line or via the Loadable Kernel Module Installation Facility.  Driver Options
  for multiple host adapters may be specified either by separating the option
  strings by a semicolon, or by specifying multiple "BusLogic=" strings on the
  command line.  Individual option specifications for a single host adapter are
  separated by commas.  The Probing and Debugging Options apply to all host
  adapters whereas the remaining options apply individually only to the
  selected host adapter.

  The BusLogic Driver Probing Options are described in
  <file:Documentation/scsi/BusLogic.txt>.
*/

static int __init BusLogic_ParseDriverOptions(char *OptionsString)
{
	while (true) {
		struct BusLogic_DriverOptions *DriverOptions = &BusLogic_DriverOptions[BusLogic_DriverOptionsCount++];
		int TargetID;
		memset(DriverOptions, 0, sizeof(struct BusLogic_DriverOptions));
		while (*OptionsString != '\0' && *OptionsString != ';') {
			/* Probing Options. */
			if (BusLogic_ParseKeyword(&OptionsString, "IO:")) {
				unsigned long IO_Address = simple_strtoul(OptionsString, &OptionsString, 0);
				BusLogic_ProbeOptions.LimitedProbeISA = true;
				switch (IO_Address) {
				case 0x330:
					BusLogic_ProbeOptions.Probe330 = true;
					break;
				case 0x334:
					BusLogic_ProbeOptions.Probe334 = true;
					break;
				case 0x230:
					BusLogic_ProbeOptions.Probe230 = true;
					break;
				case 0x234:
					BusLogic_ProbeOptions.Probe234 = true;
					break;
				case 0x130:
					BusLogic_ProbeOptions.Probe130 = true;
					break;
				case 0x134:
					BusLogic_ProbeOptions.Probe134 = true;
					break;
				default:
					BusLogic_Error("BusLogic: Invalid Driver Options " "(invalid I/O Address 0x%X)\n", NULL, IO_Address);
					return 0;
				}
			} else if (BusLogic_ParseKeyword(&OptionsString, "NoProbeISA"))
				BusLogic_ProbeOptions.NoProbeISA = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "NoProbePCI"))
				BusLogic_ProbeOptions.NoProbePCI = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "NoProbe"))
				BusLogic_ProbeOptions.NoProbe = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "NoSortPCI"))
				BusLogic_ProbeOptions.NoSortPCI = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "MultiMasterFirst"))
				BusLogic_ProbeOptions.MultiMasterFirst = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "FlashPointFirst"))
				BusLogic_ProbeOptions.FlashPointFirst = true;
			/* Tagged Queuing Options. */
			else if (BusLogic_ParseKeyword(&OptionsString, "QueueDepth:[") || BusLogic_ParseKeyword(&OptionsString, "QD:[")) {
				for (TargetID = 0; TargetID < BusLogic_MaxTargetDevices; TargetID++) {
					unsigned short QueueDepth = simple_strtoul(OptionsString, &OptionsString, 0);
					if (QueueDepth > BusLogic_MaxTaggedQueueDepth) {
						BusLogic_Error("BusLogic: Invalid Driver Options " "(invalid Queue Depth %d)\n", NULL, QueueDepth);
						return 0;
					}
					DriverOptions->QueueDepth[TargetID] = QueueDepth;
					if (*OptionsString == ',')
						OptionsString++;
					else if (*OptionsString == ']')
						break;
					else {
						BusLogic_Error("BusLogic: Invalid Driver Options " "(',' or ']' expected at '%s')\n", NULL, OptionsString);
						return 0;
					}
				}
				if (*OptionsString != ']') {
					BusLogic_Error("BusLogic: Invalid Driver Options " "(']' expected at '%s')\n", NULL, OptionsString);
					return 0;
				} else
					OptionsString++;
			} else if (BusLogic_ParseKeyword(&OptionsString, "QueueDepth:") || BusLogic_ParseKeyword(&OptionsString, "QD:")) {
				unsigned short QueueDepth = simple_strtoul(OptionsString, &OptionsString, 0);
				if (QueueDepth == 0 || QueueDepth > BusLogic_MaxTaggedQueueDepth) {
					BusLogic_Error("BusLogic: Invalid Driver Options " "(invalid Queue Depth %d)\n", NULL, QueueDepth);
					return 0;
				}
				DriverOptions->CommonQueueDepth = QueueDepth;
				for (TargetID = 0; TargetID < BusLogic_MaxTargetDevices; TargetID++)
					DriverOptions->QueueDepth[TargetID] = QueueDepth;
			} else if (BusLogic_ParseKeyword(&OptionsString, "TaggedQueuing:") || BusLogic_ParseKeyword(&OptionsString, "TQ:")) {
				if (BusLogic_ParseKeyword(&OptionsString, "Default")) {
					DriverOptions->TaggedQueuingPermitted = 0x0000;
					DriverOptions->TaggedQueuingPermittedMask = 0x0000;
				} else if (BusLogic_ParseKeyword(&OptionsString, "Enable")) {
					DriverOptions->TaggedQueuingPermitted = 0xFFFF;
					DriverOptions->TaggedQueuingPermittedMask = 0xFFFF;
				} else if (BusLogic_ParseKeyword(&OptionsString, "Disable")) {
					DriverOptions->TaggedQueuingPermitted = 0x0000;
					DriverOptions->TaggedQueuingPermittedMask = 0xFFFF;
				} else {
					unsigned short TargetBit;
					for (TargetID = 0, TargetBit = 1; TargetID < BusLogic_MaxTargetDevices; TargetID++, TargetBit <<= 1)
						switch (*OptionsString++) {
						case 'Y':
							DriverOptions->TaggedQueuingPermitted |= TargetBit;
							DriverOptions->TaggedQueuingPermittedMask |= TargetBit;
							break;
						case 'N':
							DriverOptions->TaggedQueuingPermitted &= ~TargetBit;
							DriverOptions->TaggedQueuingPermittedMask |= TargetBit;
							break;
						case 'X':
							break;
						default:
							OptionsString--;
							TargetID = BusLogic_MaxTargetDevices;
							break;
						}
				}
			}
			/* Miscellaneous Options. */
			else if (BusLogic_ParseKeyword(&OptionsString, "BusSettleTime:") || BusLogic_ParseKeyword(&OptionsString, "BST:")) {
				unsigned short BusSettleTime = simple_strtoul(OptionsString, &OptionsString, 0);
				if (BusSettleTime > 5 * 60) {
					BusLogic_Error("BusLogic: Invalid Driver Options " "(invalid Bus Settle Time %d)\n", NULL, BusSettleTime);
					return 0;
				}
				DriverOptions->BusSettleTime = BusSettleTime;
			} else if (BusLogic_ParseKeyword(&OptionsString, "InhibitTargetInquiry"))
				DriverOptions->LocalOptions.InhibitTargetInquiry = true;
			/* Debugging Options. */
			else if (BusLogic_ParseKeyword(&OptionsString, "TraceProbe"))
				BusLogic_GlobalOptions.TraceProbe = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "TraceHardwareReset"))
				BusLogic_GlobalOptions.TraceHardwareReset = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "TraceConfiguration"))
				BusLogic_GlobalOptions.TraceConfiguration = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "TraceErrors"))
				BusLogic_GlobalOptions.TraceErrors = true;
			else if (BusLogic_ParseKeyword(&OptionsString, "Debug")) {
				BusLogic_GlobalOptions.TraceProbe = true;
				BusLogic_GlobalOptions.TraceHardwareReset = true;
				BusLogic_GlobalOptions.TraceConfiguration = true;
				BusLogic_GlobalOptions.TraceErrors = true;
			}
			if (*OptionsString == ',')
				OptionsString++;
			else if (*OptionsString != ';' && *OptionsString != '\0') {
				BusLogic_Error("BusLogic: Unexpected Driver Option '%s' " "ignored\n", NULL, OptionsString);
				*OptionsString = '\0';
			}
		}
		if (!(BusLogic_DriverOptionsCount == 0 || BusLogic_ProbeInfoCount == 0 || BusLogic_DriverOptionsCount == BusLogic_ProbeInfoCount)) {
			BusLogic_Error("BusLogic: Invalid Driver Options " "(all or no I/O Addresses must be specified)\n", NULL);
			return 0;
		}
		/*
		   Tagged Queuing is disabled when the Queue Depth is 1 since queuing
		   multiple commands is not possible.
		 */
		for (TargetID = 0; TargetID < BusLogic_MaxTargetDevices; TargetID++)
			if (DriverOptions->QueueDepth[TargetID] == 1) {
				unsigned short TargetBit = 1 << TargetID;
				DriverOptions->TaggedQueuingPermitted &= ~TargetBit;
				DriverOptions->TaggedQueuingPermittedMask |= TargetBit;
			}
		if (*OptionsString == ';')
			OptionsString++;
		if (*OptionsString == '\0')
			return 0;
	}
	return 1;
}

/*
  Get it all started
*/

static struct scsi_host_template Bus_Logic_template = {
	.module = THIS_MODULE,
	.proc_name = "BusLogic",
	.proc_info = BusLogic_ProcDirectoryInfo,
	.name = "BusLogic",
	.info = BusLogic_DriverInfo,
	.queuecommand = BusLogic_QueueCommand,
	.slave_configure = BusLogic_SlaveConfigure,
	.bios_param = BusLogic_BIOSDiskParameters,
	.eh_host_reset_handler = BusLogic_host_reset,
#if 0
	.eh_abort_handler = BusLogic_AbortCommand,
#endif
	.unchecked_isa_dma = 1,
	.max_sectors = 128,
	.use_clustering = ENABLE_CLUSTERING,
};

/*
  BusLogic_Setup handles processing of Kernel Command Line Arguments.
*/

static int __init BusLogic_Setup(char *str)
{
	int ints[3];

	(void) get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] != 0) {
		BusLogic_Error("BusLogic: Obsolete Command Line Entry " "Format Ignored\n", NULL);
		return 0;
	}
	if (str == NULL || *str == '\0')
		return 0;
	return BusLogic_ParseDriverOptions(str);
}

/*
 * Exit function.  Deletes all hosts associated with this driver.
 */

static void __exit BusLogic_exit(void)
{
	struct BusLogic_HostAdapter *ha, *next;

	list_for_each_entry_safe(ha, next, &BusLogic_host_list, host_list)
		BusLogic_ReleaseHostAdapter(ha);
}

__setup("BusLogic=", BusLogic_Setup);

#ifdef MODULE
static struct pci_device_id BusLogic_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_MULTIMASTER_NC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_BUSLOGIC, PCI_DEVICE_ID_BUSLOGIC_FLASHPOINT,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};
#endif
MODULE_DEVICE_TABLE(pci, BusLogic_pci_tbl);

module_init(BusLogic_init);
module_exit(BusLogic_exit);
