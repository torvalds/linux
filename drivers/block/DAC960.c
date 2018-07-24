/*

  Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers

  Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>
  Portions Copyright 2002 by Mylex (An IBM Business Unit)

  This program is free software; you may redistribute and/or modify it under
  the terms of the GNU General Public License Version 2 as published by the
  Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for complete details.

*/


#define DAC960_DriverVersion			"2.5.49"
#define DAC960_DriverDate			"21 Aug 2007"


#include <linux/module.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/blkpg.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include "DAC960.h"

#define DAC960_GAM_MINOR	252


static DEFINE_MUTEX(DAC960_mutex);
static DAC960_Controller_T *DAC960_Controllers[DAC960_MaxControllers];
static int DAC960_ControllerCount;
static struct proc_dir_entry *DAC960_ProcDirectoryEntry;

static long disk_size(DAC960_Controller_T *p, int drive_nr)
{
	if (p->FirmwareType == DAC960_V1_Controller) {
		if (drive_nr >= p->LogicalDriveCount)
			return 0;
		return p->V1.LogicalDriveInformation[drive_nr].
			LogicalDriveSize;
	} else {
		DAC960_V2_LogicalDeviceInfo_T *i =
			p->V2.LogicalDeviceInformation[drive_nr];
		if (i == NULL)
			return 0;
		return i->ConfigurableDeviceSize;
	}
}

static int DAC960_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	DAC960_Controller_T *p = disk->queue->queuedata;
	int drive_nr = (long)disk->private_data;
	int ret = -ENXIO;

	mutex_lock(&DAC960_mutex);
	if (p->FirmwareType == DAC960_V1_Controller) {
		if (p->V1.LogicalDriveInformation[drive_nr].
		    LogicalDriveState == DAC960_V1_LogicalDrive_Offline)
			goto out;
	} else {
		DAC960_V2_LogicalDeviceInfo_T *i =
			p->V2.LogicalDeviceInformation[drive_nr];
		if (!i || i->LogicalDeviceState == DAC960_V2_LogicalDevice_Offline)
			goto out;
	}

	check_disk_change(bdev);

	if (!get_capacity(p->disks[drive_nr]))
		goto out;
	ret = 0;
out:
	mutex_unlock(&DAC960_mutex);
	return ret;
}

static int DAC960_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct gendisk *disk = bdev->bd_disk;
	DAC960_Controller_T *p = disk->queue->queuedata;
	int drive_nr = (long)disk->private_data;

	if (p->FirmwareType == DAC960_V1_Controller) {
		geo->heads = p->V1.GeometryTranslationHeads;
		geo->sectors = p->V1.GeometryTranslationSectors;
		geo->cylinders = p->V1.LogicalDriveInformation[drive_nr].
			LogicalDriveSize / (geo->heads * geo->sectors);
	} else {
		DAC960_V2_LogicalDeviceInfo_T *i =
			p->V2.LogicalDeviceInformation[drive_nr];
		switch (i->DriveGeometry) {
		case DAC960_V2_Geometry_128_32:
			geo->heads = 128;
			geo->sectors = 32;
			break;
		case DAC960_V2_Geometry_255_63:
			geo->heads = 255;
			geo->sectors = 63;
			break;
		default:
			DAC960_Error("Illegal Logical Device Geometry %d\n",
					p, i->DriveGeometry);
			return -EINVAL;
		}

		geo->cylinders = i->ConfigurableDeviceSize /
			(geo->heads * geo->sectors);
	}
	
	return 0;
}

static unsigned int DAC960_check_events(struct gendisk *disk,
					unsigned int clearing)
{
	DAC960_Controller_T *p = disk->queue->queuedata;
	int drive_nr = (long)disk->private_data;

	if (!p->LogicalDriveInitiallyAccessible[drive_nr])
		return DISK_EVENT_MEDIA_CHANGE;
	return 0;
}

static int DAC960_revalidate_disk(struct gendisk *disk)
{
	DAC960_Controller_T *p = disk->queue->queuedata;
	int unit = (long)disk->private_data;

	set_capacity(disk, disk_size(p, unit));
	return 0;
}

static const struct block_device_operations DAC960_BlockDeviceOperations = {
	.owner			= THIS_MODULE,
	.open			= DAC960_open,
	.getgeo			= DAC960_getgeo,
	.check_events		= DAC960_check_events,
	.revalidate_disk	= DAC960_revalidate_disk,
};


/*
  DAC960_AnnounceDriver announces the Driver Version and Date, Author's Name,
  Copyright Notice, and Electronic Mail Address.
*/

static void DAC960_AnnounceDriver(DAC960_Controller_T *Controller)
{
  DAC960_Announce("***** DAC960 RAID Driver Version "
		  DAC960_DriverVersion " of "
		  DAC960_DriverDate " *****\n", Controller);
  DAC960_Announce("Copyright 1998-2001 by Leonard N. Zubkoff "
		  "<lnz@dandelion.com>\n", Controller);
}


/*
  DAC960_Failure prints a standardized error message, and then returns false.
*/

static bool DAC960_Failure(DAC960_Controller_T *Controller,
			      unsigned char *ErrorMessage)
{
  DAC960_Error("While configuring DAC960 PCI RAID Controller at\n",
	       Controller);
  if (Controller->IO_Address == 0)
    DAC960_Error("PCI Bus %d Device %d Function %d I/O Address N/A "
		 "PCI Address 0x%X\n", Controller,
		 Controller->Bus, Controller->Device,
		 Controller->Function, Controller->PCI_Address);
  else DAC960_Error("PCI Bus %d Device %d Function %d I/O Address "
		    "0x%X PCI Address 0x%X\n", Controller,
		    Controller->Bus, Controller->Device,
		    Controller->Function, Controller->IO_Address,
		    Controller->PCI_Address);
  DAC960_Error("%s FAILED - DETACHING\n", Controller, ErrorMessage);
  return false;
}

/*
  init_dma_loaf() and slice_dma_loaf() are helper functions for
  aggregating the dma-mapped memory for a well-known collection of
  data structures that are of different lengths.

  These routines don't guarantee any alignment.  The caller must
  include any space needed for alignment in the sizes of the structures
  that are passed in.
 */

static bool init_dma_loaf(struct pci_dev *dev, struct dma_loaf *loaf,
								 size_t len)
{
	void *cpu_addr;
	dma_addr_t dma_handle;

	cpu_addr = pci_alloc_consistent(dev, len, &dma_handle);
	if (cpu_addr == NULL)
		return false;
	
	loaf->cpu_free = loaf->cpu_base = cpu_addr;
	loaf->dma_free =loaf->dma_base = dma_handle;
	loaf->length = len;
	memset(cpu_addr, 0, len);
	return true;
}

static void *slice_dma_loaf(struct dma_loaf *loaf, size_t len,
					dma_addr_t *dma_handle)
{
	void *cpu_end = loaf->cpu_free + len;
	void *cpu_addr = loaf->cpu_free;

	BUG_ON(cpu_end > loaf->cpu_base + loaf->length);
	*dma_handle = loaf->dma_free;
	loaf->cpu_free = cpu_end;
	loaf->dma_free += len;
	return cpu_addr;
}

static void free_dma_loaf(struct pci_dev *dev, struct dma_loaf *loaf_handle)
{
	if (loaf_handle->cpu_base != NULL)
		pci_free_consistent(dev, loaf_handle->length,
			loaf_handle->cpu_base, loaf_handle->dma_base);
}


/*
  DAC960_CreateAuxiliaryStructures allocates and initializes the auxiliary
  data structures for Controller.  It returns true on success and false on
  failure.
*/

static bool DAC960_CreateAuxiliaryStructures(DAC960_Controller_T *Controller)
{
  int CommandAllocationLength, CommandAllocationGroupSize;
  int CommandsRemaining = 0, CommandIdentifier, CommandGroupByteCount;
  void *AllocationPointer = NULL;
  void *ScatterGatherCPU = NULL;
  dma_addr_t ScatterGatherDMA;
  struct dma_pool *ScatterGatherPool;
  void *RequestSenseCPU = NULL;
  dma_addr_t RequestSenseDMA;
  struct dma_pool *RequestSensePool = NULL;

  if (Controller->FirmwareType == DAC960_V1_Controller)
    {
      CommandAllocationLength = offsetof(DAC960_Command_T, V1.EndMarker);
      CommandAllocationGroupSize = DAC960_V1_CommandAllocationGroupSize;
      ScatterGatherPool = dma_pool_create("DAC960_V1_ScatterGather",
		&Controller->PCIDevice->dev,
	DAC960_V1_ScatterGatherLimit * sizeof(DAC960_V1_ScatterGatherSegment_T),
	sizeof(DAC960_V1_ScatterGatherSegment_T), 0);
      if (ScatterGatherPool == NULL)
	    return DAC960_Failure(Controller,
			"AUXILIARY STRUCTURE CREATION (SG)");
      Controller->ScatterGatherPool = ScatterGatherPool;
    }
  else
    {
      CommandAllocationLength = offsetof(DAC960_Command_T, V2.EndMarker);
      CommandAllocationGroupSize = DAC960_V2_CommandAllocationGroupSize;
      ScatterGatherPool = dma_pool_create("DAC960_V2_ScatterGather",
		&Controller->PCIDevice->dev,
	DAC960_V2_ScatterGatherLimit * sizeof(DAC960_V2_ScatterGatherSegment_T),
	sizeof(DAC960_V2_ScatterGatherSegment_T), 0);
      if (ScatterGatherPool == NULL)
	    return DAC960_Failure(Controller,
			"AUXILIARY STRUCTURE CREATION (SG)");
      RequestSensePool = dma_pool_create("DAC960_V2_RequestSense",
		&Controller->PCIDevice->dev, sizeof(DAC960_SCSI_RequestSense_T),
		sizeof(int), 0);
      if (RequestSensePool == NULL) {
	    dma_pool_destroy(ScatterGatherPool);
	    return DAC960_Failure(Controller,
			"AUXILIARY STRUCTURE CREATION (SG)");
      }
      Controller->ScatterGatherPool = ScatterGatherPool;
      Controller->V2.RequestSensePool = RequestSensePool;
    }
  Controller->CommandAllocationGroupSize = CommandAllocationGroupSize;
  Controller->FreeCommands = NULL;
  for (CommandIdentifier = 1;
       CommandIdentifier <= Controller->DriverQueueDepth;
       CommandIdentifier++)
    {
      DAC960_Command_T *Command;
      if (--CommandsRemaining <= 0)
	{
	  CommandsRemaining =
		Controller->DriverQueueDepth - CommandIdentifier + 1;
	  if (CommandsRemaining > CommandAllocationGroupSize)
		CommandsRemaining = CommandAllocationGroupSize;
	  CommandGroupByteCount =
		CommandsRemaining * CommandAllocationLength;
	  AllocationPointer = kzalloc(CommandGroupByteCount, GFP_ATOMIC);
	  if (AllocationPointer == NULL)
		return DAC960_Failure(Controller,
					"AUXILIARY STRUCTURE CREATION");
	 }
      Command = (DAC960_Command_T *) AllocationPointer;
      AllocationPointer += CommandAllocationLength;
      Command->CommandIdentifier = CommandIdentifier;
      Command->Controller = Controller;
      Command->Next = Controller->FreeCommands;
      Controller->FreeCommands = Command;
      Controller->Commands[CommandIdentifier-1] = Command;
      ScatterGatherCPU = dma_pool_alloc(ScatterGatherPool, GFP_ATOMIC,
							&ScatterGatherDMA);
      if (ScatterGatherCPU == NULL)
	  return DAC960_Failure(Controller, "AUXILIARY STRUCTURE CREATION");

      if (RequestSensePool != NULL) {
	  RequestSenseCPU = dma_pool_alloc(RequestSensePool, GFP_ATOMIC,
						&RequestSenseDMA);
  	  if (RequestSenseCPU == NULL) {
                dma_pool_free(ScatterGatherPool, ScatterGatherCPU,
                                ScatterGatherDMA);
    		return DAC960_Failure(Controller,
					"AUXILIARY STRUCTURE CREATION");
	  }
        }
     if (Controller->FirmwareType == DAC960_V1_Controller) {
        Command->cmd_sglist = Command->V1.ScatterList;
	Command->V1.ScatterGatherList =
		(DAC960_V1_ScatterGatherSegment_T *)ScatterGatherCPU;
	Command->V1.ScatterGatherListDMA = ScatterGatherDMA;
	sg_init_table(Command->cmd_sglist, DAC960_V1_ScatterGatherLimit);
      } else {
        Command->cmd_sglist = Command->V2.ScatterList;
	Command->V2.ScatterGatherList =
		(DAC960_V2_ScatterGatherSegment_T *)ScatterGatherCPU;
	Command->V2.ScatterGatherListDMA = ScatterGatherDMA;
	Command->V2.RequestSense =
				(DAC960_SCSI_RequestSense_T *)RequestSenseCPU;
	Command->V2.RequestSenseDMA = RequestSenseDMA;
	sg_init_table(Command->cmd_sglist, DAC960_V2_ScatterGatherLimit);
      }
    }
  return true;
}


/*
  DAC960_DestroyAuxiliaryStructures deallocates the auxiliary data
  structures for Controller.
*/

static void DAC960_DestroyAuxiliaryStructures(DAC960_Controller_T *Controller)
{
  int i;
  struct dma_pool *ScatterGatherPool = Controller->ScatterGatherPool;
  struct dma_pool *RequestSensePool = NULL;
  void *ScatterGatherCPU;
  dma_addr_t ScatterGatherDMA;
  void *RequestSenseCPU;
  dma_addr_t RequestSenseDMA;
  DAC960_Command_T *CommandGroup = NULL;
  

  if (Controller->FirmwareType == DAC960_V2_Controller)
        RequestSensePool = Controller->V2.RequestSensePool;

  Controller->FreeCommands = NULL;
  for (i = 0; i < Controller->DriverQueueDepth; i++)
    {
      DAC960_Command_T *Command = Controller->Commands[i];

      if (Command == NULL)
	  continue;

      if (Controller->FirmwareType == DAC960_V1_Controller) {
	  ScatterGatherCPU = (void *)Command->V1.ScatterGatherList;
	  ScatterGatherDMA = Command->V1.ScatterGatherListDMA;
	  RequestSenseCPU = NULL;
	  RequestSenseDMA = (dma_addr_t)0;
      } else {
          ScatterGatherCPU = (void *)Command->V2.ScatterGatherList;
	  ScatterGatherDMA = Command->V2.ScatterGatherListDMA;
	  RequestSenseCPU = (void *)Command->V2.RequestSense;
	  RequestSenseDMA = Command->V2.RequestSenseDMA;
      }
      if (ScatterGatherCPU != NULL)
          dma_pool_free(ScatterGatherPool, ScatterGatherCPU, ScatterGatherDMA);
      if (RequestSenseCPU != NULL)
          dma_pool_free(RequestSensePool, RequestSenseCPU, RequestSenseDMA);

      if ((Command->CommandIdentifier
	   % Controller->CommandAllocationGroupSize) == 1) {
	   /*
	    * We can't free the group of commands until all of the
	    * request sense and scatter gather dma structures are free.
            * Remember the beginning of the group, but don't free it
	    * until we've reached the beginning of the next group.
	    */
	   kfree(CommandGroup);
	   CommandGroup = Command;
      }
      Controller->Commands[i] = NULL;
    }
  kfree(CommandGroup);

  if (Controller->CombinedStatusBuffer != NULL)
    {
      kfree(Controller->CombinedStatusBuffer);
      Controller->CombinedStatusBuffer = NULL;
      Controller->CurrentStatusBuffer = NULL;
    }

  dma_pool_destroy(ScatterGatherPool);
  if (Controller->FirmwareType == DAC960_V1_Controller)
  	return;

  dma_pool_destroy(RequestSensePool);

  for (i = 0; i < DAC960_MaxLogicalDrives; i++) {
	kfree(Controller->V2.LogicalDeviceInformation[i]);
	Controller->V2.LogicalDeviceInformation[i] = NULL;
  }

  for (i = 0; i < DAC960_V2_MaxPhysicalDevices; i++)
    {
      kfree(Controller->V2.PhysicalDeviceInformation[i]);
      Controller->V2.PhysicalDeviceInformation[i] = NULL;
      kfree(Controller->V2.InquiryUnitSerialNumber[i]);
      Controller->V2.InquiryUnitSerialNumber[i] = NULL;
    }
}


/*
  DAC960_V1_ClearCommand clears critical fields of Command for DAC960 V1
  Firmware Controllers.
*/

static inline void DAC960_V1_ClearCommand(DAC960_Command_T *Command)
{
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  memset(CommandMailbox, 0, sizeof(DAC960_V1_CommandMailbox_T));
  Command->V1.CommandStatus = 0;
}


/*
  DAC960_V2_ClearCommand clears critical fields of Command for DAC960 V2
  Firmware Controllers.
*/

static inline void DAC960_V2_ClearCommand(DAC960_Command_T *Command)
{
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  memset(CommandMailbox, 0, sizeof(DAC960_V2_CommandMailbox_T));
  Command->V2.CommandStatus = 0;
}


/*
  DAC960_AllocateCommand allocates a Command structure from Controller's
  free list.  During driver initialization, a special initialization command
  has been placed on the free list to guarantee that command allocation can
  never fail.
*/

static inline DAC960_Command_T *DAC960_AllocateCommand(DAC960_Controller_T
						       *Controller)
{
  DAC960_Command_T *Command = Controller->FreeCommands;
  if (Command == NULL) return NULL;
  Controller->FreeCommands = Command->Next;
  Command->Next = NULL;
  return Command;
}


/*
  DAC960_DeallocateCommand deallocates Command, returning it to Controller's
  free list.
*/

static inline void DAC960_DeallocateCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;

  Command->Request = NULL;
  Command->Next = Controller->FreeCommands;
  Controller->FreeCommands = Command;
}


/*
  DAC960_WaitForCommand waits for a wake_up on Controller's Command Wait Queue.
*/

static void DAC960_WaitForCommand(DAC960_Controller_T *Controller)
{
  spin_unlock_irq(&Controller->queue_lock);
  __wait_event(Controller->CommandWaitQueue, Controller->FreeCommands);
  spin_lock_irq(&Controller->queue_lock);
}

/*
  DAC960_GEM_QueueCommand queues Command for DAC960 GEM Series Controllers.
*/

static void DAC960_GEM_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandMailbox_T *NextCommandMailbox =
      Controller->V2.NextCommandMailbox;

  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_GEM_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);

  if (Controller->V2.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V2.PreviousCommandMailbox2->Words[0] == 0)
      DAC960_GEM_MemoryMailboxNewCommand(ControllerBaseAddress);

  Controller->V2.PreviousCommandMailbox2 =
      Controller->V2.PreviousCommandMailbox1;
  Controller->V2.PreviousCommandMailbox1 = NextCommandMailbox;

  if (++NextCommandMailbox > Controller->V2.LastCommandMailbox)
      NextCommandMailbox = Controller->V2.FirstCommandMailbox;

  Controller->V2.NextCommandMailbox = NextCommandMailbox;
}

/*
  DAC960_BA_QueueCommand queues Command for DAC960 BA Series Controllers.
*/

static void DAC960_BA_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandMailbox_T *NextCommandMailbox =
    Controller->V2.NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_BA_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
  if (Controller->V2.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V2.PreviousCommandMailbox2->Words[0] == 0)
    DAC960_BA_MemoryMailboxNewCommand(ControllerBaseAddress);
  Controller->V2.PreviousCommandMailbox2 =
    Controller->V2.PreviousCommandMailbox1;
  Controller->V2.PreviousCommandMailbox1 = NextCommandMailbox;
  if (++NextCommandMailbox > Controller->V2.LastCommandMailbox)
    NextCommandMailbox = Controller->V2.FirstCommandMailbox;
  Controller->V2.NextCommandMailbox = NextCommandMailbox;
}


/*
  DAC960_LP_QueueCommand queues Command for DAC960 LP Series Controllers.
*/

static void DAC960_LP_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandMailbox_T *NextCommandMailbox =
    Controller->V2.NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_LP_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
  if (Controller->V2.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V2.PreviousCommandMailbox2->Words[0] == 0)
    DAC960_LP_MemoryMailboxNewCommand(ControllerBaseAddress);
  Controller->V2.PreviousCommandMailbox2 =
    Controller->V2.PreviousCommandMailbox1;
  Controller->V2.PreviousCommandMailbox1 = NextCommandMailbox;
  if (++NextCommandMailbox > Controller->V2.LastCommandMailbox)
    NextCommandMailbox = Controller->V2.FirstCommandMailbox;
  Controller->V2.NextCommandMailbox = NextCommandMailbox;
}


/*
  DAC960_LA_QueueCommandDualMode queues Command for DAC960 LA Series
  Controllers with Dual Mode Firmware.
*/

static void DAC960_LA_QueueCommandDualMode(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandMailbox_T *NextCommandMailbox =
    Controller->V1.NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_LA_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
  if (Controller->V1.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V1.PreviousCommandMailbox2->Words[0] == 0)
    DAC960_LA_MemoryMailboxNewCommand(ControllerBaseAddress);
  Controller->V1.PreviousCommandMailbox2 =
    Controller->V1.PreviousCommandMailbox1;
  Controller->V1.PreviousCommandMailbox1 = NextCommandMailbox;
  if (++NextCommandMailbox > Controller->V1.LastCommandMailbox)
    NextCommandMailbox = Controller->V1.FirstCommandMailbox;
  Controller->V1.NextCommandMailbox = NextCommandMailbox;
}


/*
  DAC960_LA_QueueCommandSingleMode queues Command for DAC960 LA Series
  Controllers with Single Mode Firmware.
*/

static void DAC960_LA_QueueCommandSingleMode(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandMailbox_T *NextCommandMailbox =
    Controller->V1.NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_LA_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
  if (Controller->V1.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V1.PreviousCommandMailbox2->Words[0] == 0)
    DAC960_LA_HardwareMailboxNewCommand(ControllerBaseAddress);
  Controller->V1.PreviousCommandMailbox2 =
    Controller->V1.PreviousCommandMailbox1;
  Controller->V1.PreviousCommandMailbox1 = NextCommandMailbox;
  if (++NextCommandMailbox > Controller->V1.LastCommandMailbox)
    NextCommandMailbox = Controller->V1.FirstCommandMailbox;
  Controller->V1.NextCommandMailbox = NextCommandMailbox;
}


/*
  DAC960_PG_QueueCommandDualMode queues Command for DAC960 PG Series
  Controllers with Dual Mode Firmware.
*/

static void DAC960_PG_QueueCommandDualMode(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandMailbox_T *NextCommandMailbox =
    Controller->V1.NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_PG_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
  if (Controller->V1.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V1.PreviousCommandMailbox2->Words[0] == 0)
    DAC960_PG_MemoryMailboxNewCommand(ControllerBaseAddress);
  Controller->V1.PreviousCommandMailbox2 =
    Controller->V1.PreviousCommandMailbox1;
  Controller->V1.PreviousCommandMailbox1 = NextCommandMailbox;
  if (++NextCommandMailbox > Controller->V1.LastCommandMailbox)
    NextCommandMailbox = Controller->V1.FirstCommandMailbox;
  Controller->V1.NextCommandMailbox = NextCommandMailbox;
}


/*
  DAC960_PG_QueueCommandSingleMode queues Command for DAC960 PG Series
  Controllers with Single Mode Firmware.
*/

static void DAC960_PG_QueueCommandSingleMode(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandMailbox_T *NextCommandMailbox =
    Controller->V1.NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  DAC960_PG_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
  if (Controller->V1.PreviousCommandMailbox1->Words[0] == 0 ||
      Controller->V1.PreviousCommandMailbox2->Words[0] == 0)
    DAC960_PG_HardwareMailboxNewCommand(ControllerBaseAddress);
  Controller->V1.PreviousCommandMailbox2 =
    Controller->V1.PreviousCommandMailbox1;
  Controller->V1.PreviousCommandMailbox1 = NextCommandMailbox;
  if (++NextCommandMailbox > Controller->V1.LastCommandMailbox)
    NextCommandMailbox = Controller->V1.FirstCommandMailbox;
  Controller->V1.NextCommandMailbox = NextCommandMailbox;
}


/*
  DAC960_PD_QueueCommand queues Command for DAC960 PD Series Controllers.
*/

static void DAC960_PD_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  while (DAC960_PD_MailboxFullP(ControllerBaseAddress))
    udelay(1);
  DAC960_PD_WriteCommandMailbox(ControllerBaseAddress, CommandMailbox);
  DAC960_PD_NewCommand(ControllerBaseAddress);
}


/*
  DAC960_P_QueueCommand queues Command for DAC960 P Series Controllers.
*/

static void DAC960_P_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command->CommandIdentifier;
  switch (CommandMailbox->Common.CommandOpcode)
    {
    case DAC960_V1_Enquiry:
      CommandMailbox->Common.CommandOpcode = DAC960_V1_Enquiry_Old;
      break;
    case DAC960_V1_GetDeviceState:
      CommandMailbox->Common.CommandOpcode = DAC960_V1_GetDeviceState_Old;
      break;
    case DAC960_V1_Read:
      CommandMailbox->Common.CommandOpcode = DAC960_V1_Read_Old;
      DAC960_PD_To_P_TranslateReadWriteCommand(CommandMailbox);
      break;
    case DAC960_V1_Write:
      CommandMailbox->Common.CommandOpcode = DAC960_V1_Write_Old;
      DAC960_PD_To_P_TranslateReadWriteCommand(CommandMailbox);
      break;
    case DAC960_V1_ReadWithScatterGather:
      CommandMailbox->Common.CommandOpcode =
	DAC960_V1_ReadWithScatterGather_Old;
      DAC960_PD_To_P_TranslateReadWriteCommand(CommandMailbox);
      break;
    case DAC960_V1_WriteWithScatterGather:
      CommandMailbox->Common.CommandOpcode =
	DAC960_V1_WriteWithScatterGather_Old;
      DAC960_PD_To_P_TranslateReadWriteCommand(CommandMailbox);
      break;
    default:
      break;
    }
  while (DAC960_PD_MailboxFullP(ControllerBaseAddress))
    udelay(1);
  DAC960_PD_WriteCommandMailbox(ControllerBaseAddress, CommandMailbox);
  DAC960_PD_NewCommand(ControllerBaseAddress);
}


/*
  DAC960_ExecuteCommand executes Command and waits for completion.
*/

static void DAC960_ExecuteCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DECLARE_COMPLETION_ONSTACK(Completion);
  unsigned long flags;
  Command->Completion = &Completion;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_QueueCommand(Command);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
 
  if (in_interrupt())
	  return;
  wait_for_completion(&Completion);
}


/*
  DAC960_V1_ExecuteType3 executes a DAC960 V1 Firmware Controller Type 3
  Command and waits for completion.  It returns true on success and false
  on failure.
*/

static bool DAC960_V1_ExecuteType3(DAC960_Controller_T *Controller,
				      DAC960_V1_CommandOpcode_T CommandOpcode,
				      dma_addr_t DataDMA)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandStatus_T CommandStatus;
  DAC960_V1_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3.BusAddress = DataDMA;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V1.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V1_NormalCompletion);
}


/*
  DAC960_V1_ExecuteTypeB executes a DAC960 V1 Firmware Controller Type 3B
  Command and waits for completion.  It returns true on success and false
  on failure.
*/

static bool DAC960_V1_ExecuteType3B(DAC960_Controller_T *Controller,
				       DAC960_V1_CommandOpcode_T CommandOpcode,
				       unsigned char CommandOpcode2,
				       dma_addr_t DataDMA)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandStatus_T CommandStatus;
  DAC960_V1_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3B.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3B.CommandOpcode2 = CommandOpcode2;
  CommandMailbox->Type3B.BusAddress = DataDMA;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V1.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V1_NormalCompletion);
}


/*
  DAC960_V1_ExecuteType3D executes a DAC960 V1 Firmware Controller Type 3D
  Command and waits for completion.  It returns true on success and false
  on failure.
*/

static bool DAC960_V1_ExecuteType3D(DAC960_Controller_T *Controller,
				       DAC960_V1_CommandOpcode_T CommandOpcode,
				       unsigned char Channel,
				       unsigned char TargetID,
				       dma_addr_t DataDMA)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_CommandStatus_T CommandStatus;
  DAC960_V1_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3D.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3D.Channel = Channel;
  CommandMailbox->Type3D.TargetID = TargetID;
  CommandMailbox->Type3D.BusAddress = DataDMA;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V1.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V1_NormalCompletion);
}


/*
  DAC960_V2_GeneralInfo executes a DAC960 V2 Firmware General Information
  Reading IOCTL Command and waits for completion.  It returns true on success
  and false on failure.

  Return data in The controller's HealthStatusBuffer, which is dma-able memory
*/

static bool DAC960_V2_GeneralInfo(DAC960_Controller_T *Controller)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandStatus_T CommandStatus;
  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Common.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->Common.CommandControlBits
			.DataTransferControllerToHost = true;
  CommandMailbox->Common.CommandControlBits
			.NoAutoRequestSense = true;
  CommandMailbox->Common.DataTransferSize = sizeof(DAC960_V2_HealthStatusBuffer_T);
  CommandMailbox->Common.IOCTL_Opcode = DAC960_V2_GetHealthStatus;
  CommandMailbox->Common.DataTransferMemoryAddress
			.ScatterGatherSegments[0]
			.SegmentDataPointer =
    Controller->V2.HealthStatusBufferDMA;
  CommandMailbox->Common.DataTransferMemoryAddress
			.ScatterGatherSegments[0]
			.SegmentByteCount =
    CommandMailbox->Common.DataTransferSize;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V2.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V2_ControllerInfo executes a DAC960 V2 Firmware Controller
  Information Reading IOCTL Command and waits for completion.  It returns
  true on success and false on failure.

  Data is returned in the controller's V2.NewControllerInformation dma-able
  memory buffer.
*/

static bool DAC960_V2_NewControllerInfo(DAC960_Controller_T *Controller)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandStatus_T CommandStatus;
  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->ControllerInfo.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->ControllerInfo.CommandControlBits
				.DataTransferControllerToHost = true;
  CommandMailbox->ControllerInfo.CommandControlBits
				.NoAutoRequestSense = true;
  CommandMailbox->ControllerInfo.DataTransferSize = sizeof(DAC960_V2_ControllerInfo_T);
  CommandMailbox->ControllerInfo.ControllerNumber = 0;
  CommandMailbox->ControllerInfo.IOCTL_Opcode = DAC960_V2_GetControllerInfo;
  CommandMailbox->ControllerInfo.DataTransferMemoryAddress
				.ScatterGatherSegments[0]
				.SegmentDataPointer =
    	Controller->V2.NewControllerInformationDMA;
  CommandMailbox->ControllerInfo.DataTransferMemoryAddress
				.ScatterGatherSegments[0]
				.SegmentByteCount =
    CommandMailbox->ControllerInfo.DataTransferSize;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V2.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V2_LogicalDeviceInfo executes a DAC960 V2 Firmware Controller Logical
  Device Information Reading IOCTL Command and waits for completion.  It
  returns true on success and false on failure.

  Data is returned in the controller's V2.NewLogicalDeviceInformation
*/

static bool DAC960_V2_NewLogicalDeviceInfo(DAC960_Controller_T *Controller,
					   unsigned short LogicalDeviceNumber)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandStatus_T CommandStatus;

  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->LogicalDeviceInfo.CommandOpcode =
				DAC960_V2_IOCTL;
  CommandMailbox->LogicalDeviceInfo.CommandControlBits
				   .DataTransferControllerToHost = true;
  CommandMailbox->LogicalDeviceInfo.CommandControlBits
				   .NoAutoRequestSense = true;
  CommandMailbox->LogicalDeviceInfo.DataTransferSize = 
				sizeof(DAC960_V2_LogicalDeviceInfo_T);
  CommandMailbox->LogicalDeviceInfo.LogicalDevice.LogicalDeviceNumber =
    LogicalDeviceNumber;
  CommandMailbox->LogicalDeviceInfo.IOCTL_Opcode = DAC960_V2_GetLogicalDeviceInfoValid;
  CommandMailbox->LogicalDeviceInfo.DataTransferMemoryAddress
				   .ScatterGatherSegments[0]
				   .SegmentDataPointer =
    	Controller->V2.NewLogicalDeviceInformationDMA;
  CommandMailbox->LogicalDeviceInfo.DataTransferMemoryAddress
				   .ScatterGatherSegments[0]
				   .SegmentByteCount =
    CommandMailbox->LogicalDeviceInfo.DataTransferSize;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V2.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V2_PhysicalDeviceInfo executes a DAC960 V2 Firmware Controller "Read
  Physical Device Information" IOCTL Command and waits for completion.  It
  returns true on success and false on failure.

  The Channel, TargetID, LogicalUnit arguments should be 0 the first time
  this function is called for a given controller.  This will return data
  for the "first" device on that controller.  The returned data includes a
  Channel, TargetID, LogicalUnit that can be passed in to this routine to
  get data for the NEXT device on that controller.

  Data is stored in the controller's V2.NewPhysicalDeviceInfo dma-able
  memory buffer.

*/

static bool DAC960_V2_NewPhysicalDeviceInfo(DAC960_Controller_T *Controller,
					    unsigned char Channel,
					    unsigned char TargetID,
					    unsigned char LogicalUnit)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandStatus_T CommandStatus;

  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->PhysicalDeviceInfo.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->PhysicalDeviceInfo.CommandControlBits
				    .DataTransferControllerToHost = true;
  CommandMailbox->PhysicalDeviceInfo.CommandControlBits
				    .NoAutoRequestSense = true;
  CommandMailbox->PhysicalDeviceInfo.DataTransferSize =
				sizeof(DAC960_V2_PhysicalDeviceInfo_T);
  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.LogicalUnit = LogicalUnit;
  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.TargetID = TargetID;
  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.Channel = Channel;
  CommandMailbox->PhysicalDeviceInfo.IOCTL_Opcode =
					DAC960_V2_GetPhysicalDeviceInfoValid;
  CommandMailbox->PhysicalDeviceInfo.DataTransferMemoryAddress
				    .ScatterGatherSegments[0]
				    .SegmentDataPointer =
    					Controller->V2.NewPhysicalDeviceInformationDMA;
  CommandMailbox->PhysicalDeviceInfo.DataTransferMemoryAddress
				    .ScatterGatherSegments[0]
				    .SegmentByteCount =
    CommandMailbox->PhysicalDeviceInfo.DataTransferSize;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V2.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V2_NormalCompletion);
}


static void DAC960_V2_ConstructNewUnitSerialNumber(
	DAC960_Controller_T *Controller,
	DAC960_V2_CommandMailbox_T *CommandMailbox, int Channel, int TargetID,
	int LogicalUnit)
{
      CommandMailbox->SCSI_10.CommandOpcode = DAC960_V2_SCSI_10_Passthru;
      CommandMailbox->SCSI_10.CommandControlBits
			     .DataTransferControllerToHost = true;
      CommandMailbox->SCSI_10.CommandControlBits
			     .NoAutoRequestSense = true;
      CommandMailbox->SCSI_10.DataTransferSize =
	sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
      CommandMailbox->SCSI_10.PhysicalDevice.LogicalUnit = LogicalUnit;
      CommandMailbox->SCSI_10.PhysicalDevice.TargetID = TargetID;
      CommandMailbox->SCSI_10.PhysicalDevice.Channel = Channel;
      CommandMailbox->SCSI_10.CDBLength = 6;
      CommandMailbox->SCSI_10.SCSI_CDB[0] = 0x12; /* INQUIRY */
      CommandMailbox->SCSI_10.SCSI_CDB[1] = 1; /* EVPD = 1 */
      CommandMailbox->SCSI_10.SCSI_CDB[2] = 0x80; /* Page Code */
      CommandMailbox->SCSI_10.SCSI_CDB[3] = 0; /* Reserved */
      CommandMailbox->SCSI_10.SCSI_CDB[4] =
	sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
      CommandMailbox->SCSI_10.SCSI_CDB[5] = 0; /* Control */
      CommandMailbox->SCSI_10.DataTransferMemoryAddress
			     .ScatterGatherSegments[0]
			     .SegmentDataPointer =
		Controller->V2.NewInquiryUnitSerialNumberDMA;
      CommandMailbox->SCSI_10.DataTransferMemoryAddress
			     .ScatterGatherSegments[0]
			     .SegmentByteCount =
		CommandMailbox->SCSI_10.DataTransferSize;
}


/*
  DAC960_V2_NewUnitSerialNumber executes an SCSI pass-through
  Inquiry command to a SCSI device identified by Channel number,
  Target id, Logical Unit Number.  This function Waits for completion
  of the command.

  The return data includes Unit Serial Number information for the
  specified device.

  Data is stored in the controller's V2.NewPhysicalDeviceInfo dma-able
  memory buffer.
*/

static bool DAC960_V2_NewInquiryUnitSerialNumber(DAC960_Controller_T *Controller,
			int Channel, int TargetID, int LogicalUnit)
{
      DAC960_Command_T *Command;
      DAC960_V2_CommandMailbox_T *CommandMailbox;
      DAC960_V2_CommandStatus_T CommandStatus;

      Command = DAC960_AllocateCommand(Controller);
      CommandMailbox = &Command->V2.CommandMailbox;
      DAC960_V2_ClearCommand(Command);
      Command->CommandType = DAC960_ImmediateCommand;

      DAC960_V2_ConstructNewUnitSerialNumber(Controller, CommandMailbox,
			Channel, TargetID, LogicalUnit);

      DAC960_ExecuteCommand(Command);
      CommandStatus = Command->V2.CommandStatus;
      DAC960_DeallocateCommand(Command);
      return (CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V2_DeviceOperation executes a DAC960 V2 Firmware Controller Device
  Operation IOCTL Command and waits for completion.  It returns true on
  success and false on failure.
*/

static bool DAC960_V2_DeviceOperation(DAC960_Controller_T *Controller,
					 DAC960_V2_IOCTL_Opcode_T IOCTL_Opcode,
					 DAC960_V2_OperationDevice_T
					   OperationDevice)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_CommandStatus_T CommandStatus;
  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->DeviceOperation.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->DeviceOperation.CommandControlBits
				 .DataTransferControllerToHost = true;
  CommandMailbox->DeviceOperation.CommandControlBits
    				 .NoAutoRequestSense = true;
  CommandMailbox->DeviceOperation.IOCTL_Opcode = IOCTL_Opcode;
  CommandMailbox->DeviceOperation.OperationDevice = OperationDevice;
  DAC960_ExecuteCommand(Command);
  CommandStatus = Command->V2.CommandStatus;
  DAC960_DeallocateCommand(Command);
  return (CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V1_EnableMemoryMailboxInterface enables the Memory Mailbox Interface
  for DAC960 V1 Firmware Controllers.

  PD and P controller types have no memory mailbox, but still need the
  other dma mapped memory.
*/

static bool DAC960_V1_EnableMemoryMailboxInterface(DAC960_Controller_T
						      *Controller)
{
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_HardwareType_T hw_type = Controller->HardwareType;
  struct pci_dev *PCI_Device = Controller->PCIDevice;
  struct dma_loaf *DmaPages = &Controller->DmaPages;
  size_t DmaPagesSize;
  size_t CommandMailboxesSize;
  size_t StatusMailboxesSize;

  DAC960_V1_CommandMailbox_T *CommandMailboxesMemory;
  dma_addr_t CommandMailboxesMemoryDMA;

  DAC960_V1_StatusMailbox_T *StatusMailboxesMemory;
  dma_addr_t StatusMailboxesMemoryDMA;

  DAC960_V1_CommandMailbox_T CommandMailbox;
  DAC960_V1_CommandStatus_T CommandStatus;
  int TimeoutCounter;
  int i;

  memset(&CommandMailbox, 0, sizeof(DAC960_V1_CommandMailbox_T));

  if (pci_set_dma_mask(Controller->PCIDevice, DMA_BIT_MASK(32)))
	return DAC960_Failure(Controller, "DMA mask out of range");

  if ((hw_type == DAC960_PD_Controller) || (hw_type == DAC960_P_Controller)) {
    CommandMailboxesSize =  0;
    StatusMailboxesSize = 0;
  } else {
    CommandMailboxesSize =  DAC960_V1_CommandMailboxCount * sizeof(DAC960_V1_CommandMailbox_T);
    StatusMailboxesSize = DAC960_V1_StatusMailboxCount * sizeof(DAC960_V1_StatusMailbox_T);
  }
  DmaPagesSize = CommandMailboxesSize + StatusMailboxesSize + 
	sizeof(DAC960_V1_DCDB_T) + sizeof(DAC960_V1_Enquiry_T) +
	sizeof(DAC960_V1_ErrorTable_T) + sizeof(DAC960_V1_EventLogEntry_T) +
	sizeof(DAC960_V1_RebuildProgress_T) +
	sizeof(DAC960_V1_LogicalDriveInformationArray_T) +
	sizeof(DAC960_V1_BackgroundInitializationStatus_T) +
	sizeof(DAC960_V1_DeviceState_T) + sizeof(DAC960_SCSI_Inquiry_T) +
	sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);

  if (!init_dma_loaf(PCI_Device, DmaPages, DmaPagesSize))
	return false;


  if ((hw_type == DAC960_PD_Controller) || (hw_type == DAC960_P_Controller)) 
	goto skip_mailboxes;

  CommandMailboxesMemory = slice_dma_loaf(DmaPages,
                CommandMailboxesSize, &CommandMailboxesMemoryDMA);
  
  /* These are the base addresses for the command memory mailbox array */
  Controller->V1.FirstCommandMailbox = CommandMailboxesMemory;
  Controller->V1.FirstCommandMailboxDMA = CommandMailboxesMemoryDMA;

  CommandMailboxesMemory += DAC960_V1_CommandMailboxCount - 1;
  Controller->V1.LastCommandMailbox = CommandMailboxesMemory;
  Controller->V1.NextCommandMailbox = Controller->V1.FirstCommandMailbox;
  Controller->V1.PreviousCommandMailbox1 = Controller->V1.LastCommandMailbox;
  Controller->V1.PreviousCommandMailbox2 =
	  				Controller->V1.LastCommandMailbox - 1;

  /* These are the base addresses for the status memory mailbox array */
  StatusMailboxesMemory = slice_dma_loaf(DmaPages,
                StatusMailboxesSize, &StatusMailboxesMemoryDMA);

  Controller->V1.FirstStatusMailbox = StatusMailboxesMemory;
  Controller->V1.FirstStatusMailboxDMA = StatusMailboxesMemoryDMA;
  StatusMailboxesMemory += DAC960_V1_StatusMailboxCount - 1;
  Controller->V1.LastStatusMailbox = StatusMailboxesMemory;
  Controller->V1.NextStatusMailbox = Controller->V1.FirstStatusMailbox;

skip_mailboxes:
  Controller->V1.MonitoringDCDB = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_DCDB_T),
                &Controller->V1.MonitoringDCDB_DMA);

  Controller->V1.NewEnquiry = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_Enquiry_T),
                &Controller->V1.NewEnquiryDMA);

  Controller->V1.NewErrorTable = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_ErrorTable_T),
                &Controller->V1.NewErrorTableDMA);

  Controller->V1.EventLogEntry = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_EventLogEntry_T),
                &Controller->V1.EventLogEntryDMA);

  Controller->V1.RebuildProgress = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_RebuildProgress_T),
                &Controller->V1.RebuildProgressDMA);

  Controller->V1.NewLogicalDriveInformation = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_LogicalDriveInformationArray_T),
                &Controller->V1.NewLogicalDriveInformationDMA);

  Controller->V1.BackgroundInitializationStatus = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_BackgroundInitializationStatus_T),
                &Controller->V1.BackgroundInitializationStatusDMA);

  Controller->V1.NewDeviceState = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V1_DeviceState_T),
                &Controller->V1.NewDeviceStateDMA);

  Controller->V1.NewInquiryStandardData = slice_dma_loaf(DmaPages,
                sizeof(DAC960_SCSI_Inquiry_T),
                &Controller->V1.NewInquiryStandardDataDMA);

  Controller->V1.NewInquiryUnitSerialNumber = slice_dma_loaf(DmaPages,
                sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T),
                &Controller->V1.NewInquiryUnitSerialNumberDMA);

  if ((hw_type == DAC960_PD_Controller) || (hw_type == DAC960_P_Controller))
	return true;
 
  /* Enable the Memory Mailbox Interface. */
  Controller->V1.DualModeMemoryMailboxInterface = true;
  CommandMailbox.TypeX.CommandOpcode = 0x2B;
  CommandMailbox.TypeX.CommandIdentifier = 0;
  CommandMailbox.TypeX.CommandOpcode2 = 0x14;
  CommandMailbox.TypeX.CommandMailboxesBusAddress =
    				Controller->V1.FirstCommandMailboxDMA;
  CommandMailbox.TypeX.StatusMailboxesBusAddress =
    				Controller->V1.FirstStatusMailboxDMA;
#define TIMEOUT_COUNT 1000000

  for (i = 0; i < 2; i++)
    switch (Controller->HardwareType)
      {
      case DAC960_LA_Controller:
	TimeoutCounter = TIMEOUT_COUNT;
	while (--TimeoutCounter >= 0)
	  {
	    if (!DAC960_LA_HardwareMailboxFullP(ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	DAC960_LA_WriteHardwareMailbox(ControllerBaseAddress, &CommandMailbox);
	DAC960_LA_HardwareMailboxNewCommand(ControllerBaseAddress);
	TimeoutCounter = TIMEOUT_COUNT;
	while (--TimeoutCounter >= 0)
	  {
	    if (DAC960_LA_HardwareMailboxStatusAvailableP(
		  ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	CommandStatus = DAC960_LA_ReadStatusRegister(ControllerBaseAddress);
	DAC960_LA_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
	DAC960_LA_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
	if (CommandStatus == DAC960_V1_NormalCompletion) return true;
	Controller->V1.DualModeMemoryMailboxInterface = false;
	CommandMailbox.TypeX.CommandOpcode2 = 0x10;
	break;
      case DAC960_PG_Controller:
	TimeoutCounter = TIMEOUT_COUNT;
	while (--TimeoutCounter >= 0)
	  {
	    if (!DAC960_PG_HardwareMailboxFullP(ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	DAC960_PG_WriteHardwareMailbox(ControllerBaseAddress, &CommandMailbox);
	DAC960_PG_HardwareMailboxNewCommand(ControllerBaseAddress);

	TimeoutCounter = TIMEOUT_COUNT;
	while (--TimeoutCounter >= 0)
	  {
	    if (DAC960_PG_HardwareMailboxStatusAvailableP(
		  ControllerBaseAddress))
	      break;
	    udelay(10);
	  }
	if (TimeoutCounter < 0) return false;
	CommandStatus = DAC960_PG_ReadStatusRegister(ControllerBaseAddress);
	DAC960_PG_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
	DAC960_PG_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
	if (CommandStatus == DAC960_V1_NormalCompletion) return true;
	Controller->V1.DualModeMemoryMailboxInterface = false;
	CommandMailbox.TypeX.CommandOpcode2 = 0x10;
	break;
      default:
        DAC960_Failure(Controller, "Unknown Controller Type\n");
	break;
      }
  return false;
}


/*
  DAC960_V2_EnableMemoryMailboxInterface enables the Memory Mailbox Interface
  for DAC960 V2 Firmware Controllers.

  Aggregate the space needed for the controller's memory mailbox and
  the other data structures that will be targets of dma transfers with
  the controller.  Allocate a dma-mapped region of memory to hold these
  structures.  Then, save CPU pointers and dma_addr_t values to reference
  the structures that are contained in that region.
*/

static bool DAC960_V2_EnableMemoryMailboxInterface(DAC960_Controller_T
						      *Controller)
{
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  struct pci_dev *PCI_Device = Controller->PCIDevice;
  struct dma_loaf *DmaPages = &Controller->DmaPages;
  size_t DmaPagesSize;
  size_t CommandMailboxesSize;
  size_t StatusMailboxesSize;

  DAC960_V2_CommandMailbox_T *CommandMailboxesMemory;
  dma_addr_t CommandMailboxesMemoryDMA;

  DAC960_V2_StatusMailbox_T *StatusMailboxesMemory;
  dma_addr_t StatusMailboxesMemoryDMA;

  DAC960_V2_CommandMailbox_T *CommandMailbox;
  dma_addr_t	CommandMailboxDMA;
  DAC960_V2_CommandStatus_T CommandStatus;

	if (pci_set_dma_mask(Controller->PCIDevice, DMA_BIT_MASK(64)) &&
	    pci_set_dma_mask(Controller->PCIDevice, DMA_BIT_MASK(32)))
		return DAC960_Failure(Controller, "DMA mask out of range");

  /* This is a temporary dma mapping, used only in the scope of this function */
  CommandMailbox = pci_alloc_consistent(PCI_Device,
		sizeof(DAC960_V2_CommandMailbox_T), &CommandMailboxDMA);
  if (CommandMailbox == NULL)
	  return false;

  CommandMailboxesSize = DAC960_V2_CommandMailboxCount * sizeof(DAC960_V2_CommandMailbox_T);
  StatusMailboxesSize = DAC960_V2_StatusMailboxCount * sizeof(DAC960_V2_StatusMailbox_T);
  DmaPagesSize =
    CommandMailboxesSize + StatusMailboxesSize +
    sizeof(DAC960_V2_HealthStatusBuffer_T) +
    sizeof(DAC960_V2_ControllerInfo_T) +
    sizeof(DAC960_V2_LogicalDeviceInfo_T) +
    sizeof(DAC960_V2_PhysicalDeviceInfo_T) +
    sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T) +
    sizeof(DAC960_V2_Event_T) +
    sizeof(DAC960_V2_PhysicalToLogicalDevice_T);

  if (!init_dma_loaf(PCI_Device, DmaPages, DmaPagesSize)) {
  	pci_free_consistent(PCI_Device, sizeof(DAC960_V2_CommandMailbox_T),
					CommandMailbox, CommandMailboxDMA);
	return false;
  }

  CommandMailboxesMemory = slice_dma_loaf(DmaPages,
		CommandMailboxesSize, &CommandMailboxesMemoryDMA);

  /* These are the base addresses for the command memory mailbox array */
  Controller->V2.FirstCommandMailbox = CommandMailboxesMemory;
  Controller->V2.FirstCommandMailboxDMA = CommandMailboxesMemoryDMA;

  CommandMailboxesMemory += DAC960_V2_CommandMailboxCount - 1;
  Controller->V2.LastCommandMailbox = CommandMailboxesMemory;
  Controller->V2.NextCommandMailbox = Controller->V2.FirstCommandMailbox;
  Controller->V2.PreviousCommandMailbox1 = Controller->V2.LastCommandMailbox;
  Controller->V2.PreviousCommandMailbox2 =
    					Controller->V2.LastCommandMailbox - 1;

  /* These are the base addresses for the status memory mailbox array */
  StatusMailboxesMemory = slice_dma_loaf(DmaPages,
		StatusMailboxesSize, &StatusMailboxesMemoryDMA);

  Controller->V2.FirstStatusMailbox = StatusMailboxesMemory;
  Controller->V2.FirstStatusMailboxDMA = StatusMailboxesMemoryDMA;
  StatusMailboxesMemory += DAC960_V2_StatusMailboxCount - 1;
  Controller->V2.LastStatusMailbox = StatusMailboxesMemory;
  Controller->V2.NextStatusMailbox = Controller->V2.FirstStatusMailbox;

  Controller->V2.HealthStatusBuffer = slice_dma_loaf(DmaPages,
		sizeof(DAC960_V2_HealthStatusBuffer_T),
		&Controller->V2.HealthStatusBufferDMA);

  Controller->V2.NewControllerInformation = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V2_ControllerInfo_T), 
                &Controller->V2.NewControllerInformationDMA);

  Controller->V2.NewLogicalDeviceInformation =  slice_dma_loaf(DmaPages,
                sizeof(DAC960_V2_LogicalDeviceInfo_T),
                &Controller->V2.NewLogicalDeviceInformationDMA);

  Controller->V2.NewPhysicalDeviceInformation = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V2_PhysicalDeviceInfo_T),
                &Controller->V2.NewPhysicalDeviceInformationDMA);

  Controller->V2.NewInquiryUnitSerialNumber = slice_dma_loaf(DmaPages,
                sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T),
                &Controller->V2.NewInquiryUnitSerialNumberDMA);

  Controller->V2.Event = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V2_Event_T),
                &Controller->V2.EventDMA);

  Controller->V2.PhysicalToLogicalDevice = slice_dma_loaf(DmaPages,
                sizeof(DAC960_V2_PhysicalToLogicalDevice_T),
                &Controller->V2.PhysicalToLogicalDeviceDMA);

  /*
    Enable the Memory Mailbox Interface.
    
    I don't know why we can't just use one of the memory mailboxes
    we just allocated to do this, instead of using this temporary one.
    Try this change later.
  */
  memset(CommandMailbox, 0, sizeof(DAC960_V2_CommandMailbox_T));
  CommandMailbox->SetMemoryMailbox.CommandIdentifier = 1;
  CommandMailbox->SetMemoryMailbox.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->SetMemoryMailbox.CommandControlBits.NoAutoRequestSense = true;
  CommandMailbox->SetMemoryMailbox.FirstCommandMailboxSizeKB =
    (DAC960_V2_CommandMailboxCount * sizeof(DAC960_V2_CommandMailbox_T)) >> 10;
  CommandMailbox->SetMemoryMailbox.FirstStatusMailboxSizeKB =
    (DAC960_V2_StatusMailboxCount * sizeof(DAC960_V2_StatusMailbox_T)) >> 10;
  CommandMailbox->SetMemoryMailbox.SecondCommandMailboxSizeKB = 0;
  CommandMailbox->SetMemoryMailbox.SecondStatusMailboxSizeKB = 0;
  CommandMailbox->SetMemoryMailbox.RequestSenseSize = 0;
  CommandMailbox->SetMemoryMailbox.IOCTL_Opcode = DAC960_V2_SetMemoryMailbox;
  CommandMailbox->SetMemoryMailbox.HealthStatusBufferSizeKB = 1;
  CommandMailbox->SetMemoryMailbox.HealthStatusBufferBusAddress =
    					Controller->V2.HealthStatusBufferDMA;
  CommandMailbox->SetMemoryMailbox.FirstCommandMailboxBusAddress =
    					Controller->V2.FirstCommandMailboxDMA;
  CommandMailbox->SetMemoryMailbox.FirstStatusMailboxBusAddress =
    					Controller->V2.FirstStatusMailboxDMA;
  switch (Controller->HardwareType)
    {
    case DAC960_GEM_Controller:
      while (DAC960_GEM_HardwareMailboxFullP(ControllerBaseAddress))
	udelay(1);
      DAC960_GEM_WriteHardwareMailbox(ControllerBaseAddress, CommandMailboxDMA);
      DAC960_GEM_HardwareMailboxNewCommand(ControllerBaseAddress);
      while (!DAC960_GEM_HardwareMailboxStatusAvailableP(ControllerBaseAddress))
	udelay(1);
      CommandStatus = DAC960_GEM_ReadCommandStatus(ControllerBaseAddress);
      DAC960_GEM_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
      DAC960_GEM_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
      break;
    case DAC960_BA_Controller:
      while (DAC960_BA_HardwareMailboxFullP(ControllerBaseAddress))
	udelay(1);
      DAC960_BA_WriteHardwareMailbox(ControllerBaseAddress, CommandMailboxDMA);
      DAC960_BA_HardwareMailboxNewCommand(ControllerBaseAddress);
      while (!DAC960_BA_HardwareMailboxStatusAvailableP(ControllerBaseAddress))
	udelay(1);
      CommandStatus = DAC960_BA_ReadCommandStatus(ControllerBaseAddress);
      DAC960_BA_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
      DAC960_BA_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
      break;
    case DAC960_LP_Controller:
      while (DAC960_LP_HardwareMailboxFullP(ControllerBaseAddress))
	udelay(1);
      DAC960_LP_WriteHardwareMailbox(ControllerBaseAddress, CommandMailboxDMA);
      DAC960_LP_HardwareMailboxNewCommand(ControllerBaseAddress);
      while (!DAC960_LP_HardwareMailboxStatusAvailableP(ControllerBaseAddress))
	udelay(1);
      CommandStatus = DAC960_LP_ReadCommandStatus(ControllerBaseAddress);
      DAC960_LP_AcknowledgeHardwareMailboxInterrupt(ControllerBaseAddress);
      DAC960_LP_AcknowledgeHardwareMailboxStatus(ControllerBaseAddress);
      break;
    default:
      DAC960_Failure(Controller, "Unknown Controller Type\n");
      CommandStatus = DAC960_V2_AbormalCompletion;
      break;
    }
  pci_free_consistent(PCI_Device, sizeof(DAC960_V2_CommandMailbox_T),
					CommandMailbox, CommandMailboxDMA);
  return (CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V1_ReadControllerConfiguration reads the Configuration Information
  from DAC960 V1 Firmware Controllers and initializes the Controller structure.
*/

static bool DAC960_V1_ReadControllerConfiguration(DAC960_Controller_T
						     *Controller)
{
  DAC960_V1_Enquiry2_T *Enquiry2;
  dma_addr_t Enquiry2DMA;
  DAC960_V1_Config2_T *Config2;
  dma_addr_t Config2DMA;
  int LogicalDriveNumber, Channel, TargetID;
  struct dma_loaf local_dma;

  if (!init_dma_loaf(Controller->PCIDevice, &local_dma,
		sizeof(DAC960_V1_Enquiry2_T) + sizeof(DAC960_V1_Config2_T)))
	return DAC960_Failure(Controller, "LOGICAL DEVICE ALLOCATION");

  Enquiry2 = slice_dma_loaf(&local_dma, sizeof(DAC960_V1_Enquiry2_T), &Enquiry2DMA);
  Config2 = slice_dma_loaf(&local_dma, sizeof(DAC960_V1_Config2_T), &Config2DMA);

  if (!DAC960_V1_ExecuteType3(Controller, DAC960_V1_Enquiry,
			      Controller->V1.NewEnquiryDMA)) {
    free_dma_loaf(Controller->PCIDevice, &local_dma);
    return DAC960_Failure(Controller, "ENQUIRY");
  }
  memcpy(&Controller->V1.Enquiry, Controller->V1.NewEnquiry,
						sizeof(DAC960_V1_Enquiry_T));

  if (!DAC960_V1_ExecuteType3(Controller, DAC960_V1_Enquiry2, Enquiry2DMA)) {
    free_dma_loaf(Controller->PCIDevice, &local_dma);
    return DAC960_Failure(Controller, "ENQUIRY2");
  }

  if (!DAC960_V1_ExecuteType3(Controller, DAC960_V1_ReadConfig2, Config2DMA)) {
    free_dma_loaf(Controller->PCIDevice, &local_dma);
    return DAC960_Failure(Controller, "READ CONFIG2");
  }

  if (!DAC960_V1_ExecuteType3(Controller, DAC960_V1_GetLogicalDriveInformation,
			      Controller->V1.NewLogicalDriveInformationDMA)) {
    free_dma_loaf(Controller->PCIDevice, &local_dma);
    return DAC960_Failure(Controller, "GET LOGICAL DRIVE INFORMATION");
  }
  memcpy(&Controller->V1.LogicalDriveInformation,
		Controller->V1.NewLogicalDriveInformation,
		sizeof(DAC960_V1_LogicalDriveInformationArray_T));

  for (Channel = 0; Channel < Enquiry2->ActualChannels; Channel++)
    for (TargetID = 0; TargetID < Enquiry2->MaxTargets; TargetID++) {
      if (!DAC960_V1_ExecuteType3D(Controller, DAC960_V1_GetDeviceState,
				   Channel, TargetID,
				   Controller->V1.NewDeviceStateDMA)) {
    		free_dma_loaf(Controller->PCIDevice, &local_dma);
		return DAC960_Failure(Controller, "GET DEVICE STATE");
	}
	memcpy(&Controller->V1.DeviceState[Channel][TargetID],
		Controller->V1.NewDeviceState, sizeof(DAC960_V1_DeviceState_T));
     }
  /*
    Initialize the Controller Model Name and Full Model Name fields.
  */
  switch (Enquiry2->HardwareID.SubModel)
    {
    case DAC960_V1_P_PD_PU:
      if (Enquiry2->SCSICapability.BusSpeed == DAC960_V1_Ultra)
	strcpy(Controller->ModelName, "DAC960PU");
      else strcpy(Controller->ModelName, "DAC960PD");
      break;
    case DAC960_V1_PL:
      strcpy(Controller->ModelName, "DAC960PL");
      break;
    case DAC960_V1_PG:
      strcpy(Controller->ModelName, "DAC960PG");
      break;
    case DAC960_V1_PJ:
      strcpy(Controller->ModelName, "DAC960PJ");
      break;
    case DAC960_V1_PR:
      strcpy(Controller->ModelName, "DAC960PR");
      break;
    case DAC960_V1_PT:
      strcpy(Controller->ModelName, "DAC960PT");
      break;
    case DAC960_V1_PTL0:
      strcpy(Controller->ModelName, "DAC960PTL0");
      break;
    case DAC960_V1_PRL:
      strcpy(Controller->ModelName, "DAC960PRL");
      break;
    case DAC960_V1_PTL1:
      strcpy(Controller->ModelName, "DAC960PTL1");
      break;
    case DAC960_V1_1164P:
      strcpy(Controller->ModelName, "DAC1164P");
      break;
    default:
      free_dma_loaf(Controller->PCIDevice, &local_dma);
      return DAC960_Failure(Controller, "MODEL VERIFICATION");
    }
  strcpy(Controller->FullModelName, "Mylex ");
  strcat(Controller->FullModelName, Controller->ModelName);
  /*
    Initialize the Controller Firmware Version field and verify that it
    is a supported firmware version.  The supported firmware versions are:

    DAC1164P		    5.06 and above
    DAC960PTL/PRL/PJ/PG	    4.06 and above
    DAC960PU/PD/PL	    3.51 and above
    DAC960PU/PD/PL/P	    2.73 and above
  */
#if defined(CONFIG_ALPHA)
  /*
    DEC Alpha machines were often equipped with DAC960 cards that were
    OEMed from Mylex, and had their own custom firmware. Version 2.70,
    the last custom FW revision to be released by DEC for these older
    controllers, appears to work quite well with this driver.

    Cards tested successfully were several versions each of the PD and
    PU, called by DEC the KZPSC and KZPAC, respectively, and having
    the Manufacturer Numbers (from Mylex), usually on a sticker on the
    back of the board, of:

    KZPSC:  D040347 (1-channel) or D040348 (2-channel) or D040349 (3-channel)
    KZPAC:  D040395 (1-channel) or D040396 (2-channel) or D040397 (3-channel)
  */
# define FIRMWARE_27X	"2.70"
#else
# define FIRMWARE_27X	"2.73"
#endif

  if (Enquiry2->FirmwareID.MajorVersion == 0)
    {
      Enquiry2->FirmwareID.MajorVersion =
	Controller->V1.Enquiry.MajorFirmwareVersion;
      Enquiry2->FirmwareID.MinorVersion =
	Controller->V1.Enquiry.MinorFirmwareVersion;
      Enquiry2->FirmwareID.FirmwareType = '0';
      Enquiry2->FirmwareID.TurnID = 0;
    }
  snprintf(Controller->FirmwareVersion, sizeof(Controller->FirmwareVersion),
	   "%d.%02d-%c-%02d",
	   Enquiry2->FirmwareID.MajorVersion,
	   Enquiry2->FirmwareID.MinorVersion,
	   Enquiry2->FirmwareID.FirmwareType,
	   Enquiry2->FirmwareID.TurnID);
  if (!((Controller->FirmwareVersion[0] == '5' &&
	 strcmp(Controller->FirmwareVersion, "5.06") >= 0) ||
	(Controller->FirmwareVersion[0] == '4' &&
	 strcmp(Controller->FirmwareVersion, "4.06") >= 0) ||
	(Controller->FirmwareVersion[0] == '3' &&
	 strcmp(Controller->FirmwareVersion, "3.51") >= 0) ||
	(Controller->FirmwareVersion[0] == '2' &&
	 strcmp(Controller->FirmwareVersion, FIRMWARE_27X) >= 0)))
    {
      DAC960_Failure(Controller, "FIRMWARE VERSION VERIFICATION");
      DAC960_Error("Firmware Version = '%s'\n", Controller,
		   Controller->FirmwareVersion);
      free_dma_loaf(Controller->PCIDevice, &local_dma);
      return false;
    }
  /*
    Initialize the Controller Channels, Targets, Memory Size, and SAF-TE
    Enclosure Management Enabled fields.
  */
  Controller->Channels = Enquiry2->ActualChannels;
  Controller->Targets = Enquiry2->MaxTargets;
  Controller->MemorySize = Enquiry2->MemorySize >> 20;
  Controller->V1.SAFTE_EnclosureManagementEnabled =
    (Enquiry2->FaultManagementType == DAC960_V1_SAFTE);
  /*
    Initialize the Controller Queue Depth, Driver Queue Depth, Logical Drive
    Count, Maximum Blocks per Command, Controller Scatter/Gather Limit, and
    Driver Scatter/Gather Limit.  The Driver Queue Depth must be at most one
    less than the Controller Queue Depth to allow for an automatic drive
    rebuild operation.
  */
  Controller->ControllerQueueDepth = Controller->V1.Enquiry.MaxCommands;
  Controller->DriverQueueDepth = Controller->ControllerQueueDepth - 1;
  if (Controller->DriverQueueDepth > DAC960_MaxDriverQueueDepth)
    Controller->DriverQueueDepth = DAC960_MaxDriverQueueDepth;
  Controller->LogicalDriveCount =
    Controller->V1.Enquiry.NumberOfLogicalDrives;
  Controller->MaxBlocksPerCommand = Enquiry2->MaxBlocksPerCommand;
  Controller->ControllerScatterGatherLimit = Enquiry2->MaxScatterGatherEntries;
  Controller->DriverScatterGatherLimit =
    Controller->ControllerScatterGatherLimit;
  if (Controller->DriverScatterGatherLimit > DAC960_V1_ScatterGatherLimit)
    Controller->DriverScatterGatherLimit = DAC960_V1_ScatterGatherLimit;
  /*
    Initialize the Stripe Size, Segment Size, and Geometry Translation.
  */
  Controller->V1.StripeSize = Config2->BlocksPerStripe * Config2->BlockFactor
			      >> (10 - DAC960_BlockSizeBits);
  Controller->V1.SegmentSize = Config2->BlocksPerCacheLine * Config2->BlockFactor
			       >> (10 - DAC960_BlockSizeBits);
  switch (Config2->DriveGeometry)
    {
    case DAC960_V1_Geometry_128_32:
      Controller->V1.GeometryTranslationHeads = 128;
      Controller->V1.GeometryTranslationSectors = 32;
      break;
    case DAC960_V1_Geometry_255_63:
      Controller->V1.GeometryTranslationHeads = 255;
      Controller->V1.GeometryTranslationSectors = 63;
      break;
    default:
      free_dma_loaf(Controller->PCIDevice, &local_dma);
      return DAC960_Failure(Controller, "CONFIG2 DRIVE GEOMETRY");
    }
  /*
    Initialize the Background Initialization Status.
  */
  if ((Controller->FirmwareVersion[0] == '4' &&
      strcmp(Controller->FirmwareVersion, "4.08") >= 0) ||
      (Controller->FirmwareVersion[0] == '5' &&
       strcmp(Controller->FirmwareVersion, "5.08") >= 0))
    {
      Controller->V1.BackgroundInitializationStatusSupported = true;
      DAC960_V1_ExecuteType3B(Controller,
			      DAC960_V1_BackgroundInitializationControl, 0x20,
			      Controller->
			       V1.BackgroundInitializationStatusDMA);
      memcpy(&Controller->V1.LastBackgroundInitializationStatus,
		Controller->V1.BackgroundInitializationStatus,
		sizeof(DAC960_V1_BackgroundInitializationStatus_T));
    }
  /*
    Initialize the Logical Drive Initially Accessible flag.
  */
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
    if (Controller->V1.LogicalDriveInformation
		       [LogicalDriveNumber].LogicalDriveState !=
	DAC960_V1_LogicalDrive_Offline)
      Controller->LogicalDriveInitiallyAccessible[LogicalDriveNumber] = true;
  Controller->V1.LastRebuildStatus = DAC960_V1_NoRebuildOrCheckInProgress;
  free_dma_loaf(Controller->PCIDevice, &local_dma);
  return true;
}


/*
  DAC960_V2_ReadControllerConfiguration reads the Configuration Information
  from DAC960 V2 Firmware Controllers and initializes the Controller structure.
*/

static bool DAC960_V2_ReadControllerConfiguration(DAC960_Controller_T
						     *Controller)
{
  DAC960_V2_ControllerInfo_T *ControllerInfo =
    		&Controller->V2.ControllerInformation;
  unsigned short LogicalDeviceNumber = 0;
  int ModelNameLength;

  /* Get data into dma-able area, then copy into permanent location */
  if (!DAC960_V2_NewControllerInfo(Controller))
    return DAC960_Failure(Controller, "GET CONTROLLER INFO");
  memcpy(ControllerInfo, Controller->V2.NewControllerInformation,
			sizeof(DAC960_V2_ControllerInfo_T));
	 
  
  if (!DAC960_V2_GeneralInfo(Controller))
    return DAC960_Failure(Controller, "GET HEALTH STATUS");

  /*
    Initialize the Controller Model Name and Full Model Name fields.
  */
  ModelNameLength = sizeof(ControllerInfo->ControllerName);
  if (ModelNameLength > sizeof(Controller->ModelName)-1)
    ModelNameLength = sizeof(Controller->ModelName)-1;
  memcpy(Controller->ModelName, ControllerInfo->ControllerName,
	 ModelNameLength);
  ModelNameLength--;
  while (Controller->ModelName[ModelNameLength] == ' ' ||
	 Controller->ModelName[ModelNameLength] == '\0')
    ModelNameLength--;
  Controller->ModelName[++ModelNameLength] = '\0';
  strcpy(Controller->FullModelName, "Mylex ");
  strcat(Controller->FullModelName, Controller->ModelName);
  /*
    Initialize the Controller Firmware Version field.
  */
  sprintf(Controller->FirmwareVersion, "%d.%02d-%02d",
	  ControllerInfo->FirmwareMajorVersion,
	  ControllerInfo->FirmwareMinorVersion,
	  ControllerInfo->FirmwareTurnNumber);
  if (ControllerInfo->FirmwareMajorVersion == 6 &&
      ControllerInfo->FirmwareMinorVersion == 0 &&
      ControllerInfo->FirmwareTurnNumber < 1)
    {
      DAC960_Info("FIRMWARE VERSION %s DOES NOT PROVIDE THE CONTROLLER\n",
		  Controller, Controller->FirmwareVersion);
      DAC960_Info("STATUS MONITORING FUNCTIONALITY NEEDED BY THIS DRIVER.\n",
		  Controller);
      DAC960_Info("PLEASE UPGRADE TO VERSION 6.00-01 OR ABOVE.\n",
		  Controller);
    }
  /*
    Initialize the Controller Channels, Targets, and Memory Size.
  */
  Controller->Channels = ControllerInfo->NumberOfPhysicalChannelsPresent;
  Controller->Targets =
    ControllerInfo->MaximumTargetsPerChannel
		    [ControllerInfo->NumberOfPhysicalChannelsPresent-1];
  Controller->MemorySize = ControllerInfo->MemorySizeMB;
  /*
    Initialize the Controller Queue Depth, Driver Queue Depth, Logical Drive
    Count, Maximum Blocks per Command, Controller Scatter/Gather Limit, and
    Driver Scatter/Gather Limit.  The Driver Queue Depth must be at most one
    less than the Controller Queue Depth to allow for an automatic drive
    rebuild operation.
  */
  Controller->ControllerQueueDepth = ControllerInfo->MaximumParallelCommands;
  Controller->DriverQueueDepth = Controller->ControllerQueueDepth - 1;
  if (Controller->DriverQueueDepth > DAC960_MaxDriverQueueDepth)
    Controller->DriverQueueDepth = DAC960_MaxDriverQueueDepth;
  Controller->LogicalDriveCount = ControllerInfo->LogicalDevicesPresent;
  Controller->MaxBlocksPerCommand =
    ControllerInfo->MaximumDataTransferSizeInBlocks;
  Controller->ControllerScatterGatherLimit =
    ControllerInfo->MaximumScatterGatherEntries;
  Controller->DriverScatterGatherLimit =
    Controller->ControllerScatterGatherLimit;
  if (Controller->DriverScatterGatherLimit > DAC960_V2_ScatterGatherLimit)
    Controller->DriverScatterGatherLimit = DAC960_V2_ScatterGatherLimit;
  /*
    Initialize the Logical Device Information.
  */
  while (true)
    {
      DAC960_V2_LogicalDeviceInfo_T *NewLogicalDeviceInfo =
	Controller->V2.NewLogicalDeviceInformation;
      DAC960_V2_LogicalDeviceInfo_T *LogicalDeviceInfo;
      DAC960_V2_PhysicalDevice_T PhysicalDevice;

      if (!DAC960_V2_NewLogicalDeviceInfo(Controller, LogicalDeviceNumber))
	break;
      LogicalDeviceNumber = NewLogicalDeviceInfo->LogicalDeviceNumber;
      if (LogicalDeviceNumber >= DAC960_MaxLogicalDrives) {
	DAC960_Error("DAC960: Logical Drive Number %d not supported\n",
		       Controller, LogicalDeviceNumber);
		break;
      }
      if (NewLogicalDeviceInfo->DeviceBlockSizeInBytes != DAC960_BlockSize) {
	DAC960_Error("DAC960: Logical Drive Block Size %d not supported\n",
	      Controller, NewLogicalDeviceInfo->DeviceBlockSizeInBytes);
        LogicalDeviceNumber++;
        continue;
      }
      PhysicalDevice.Controller = 0;
      PhysicalDevice.Channel = NewLogicalDeviceInfo->Channel;
      PhysicalDevice.TargetID = NewLogicalDeviceInfo->TargetID;
      PhysicalDevice.LogicalUnit = NewLogicalDeviceInfo->LogicalUnit;
      Controller->V2.LogicalDriveToVirtualDevice[LogicalDeviceNumber] =
	PhysicalDevice;
      if (NewLogicalDeviceInfo->LogicalDeviceState !=
	  DAC960_V2_LogicalDevice_Offline)
	Controller->LogicalDriveInitiallyAccessible[LogicalDeviceNumber] = true;
      LogicalDeviceInfo = kmalloc(sizeof(DAC960_V2_LogicalDeviceInfo_T),
				   GFP_ATOMIC);
      if (LogicalDeviceInfo == NULL)
	return DAC960_Failure(Controller, "LOGICAL DEVICE ALLOCATION");
      Controller->V2.LogicalDeviceInformation[LogicalDeviceNumber] =
	LogicalDeviceInfo;
      memcpy(LogicalDeviceInfo, NewLogicalDeviceInfo,
	     sizeof(DAC960_V2_LogicalDeviceInfo_T));
      LogicalDeviceNumber++;
    }
  return true;
}


/*
  DAC960_ReportControllerConfiguration reports the Configuration Information
  for Controller.
*/

static bool DAC960_ReportControllerConfiguration(DAC960_Controller_T
						    *Controller)
{
  DAC960_Info("Configuring Mylex %s PCI RAID Controller\n",
	      Controller, Controller->ModelName);
  DAC960_Info("  Firmware Version: %s, Channels: %d, Memory Size: %dMB\n",
	      Controller, Controller->FirmwareVersion,
	      Controller->Channels, Controller->MemorySize);
  DAC960_Info("  PCI Bus: %d, Device: %d, Function: %d, I/O Address: ",
	      Controller, Controller->Bus,
	      Controller->Device, Controller->Function);
  if (Controller->IO_Address == 0)
    DAC960_Info("Unassigned\n", Controller);
  else DAC960_Info("0x%X\n", Controller, Controller->IO_Address);
  DAC960_Info("  PCI Address: 0x%X mapped at 0x%lX, IRQ Channel: %d\n",
	      Controller, Controller->PCI_Address,
	      (unsigned long) Controller->BaseAddress,
	      Controller->IRQ_Channel);
  DAC960_Info("  Controller Queue Depth: %d, "
	      "Maximum Blocks per Command: %d\n",
	      Controller, Controller->ControllerQueueDepth,
	      Controller->MaxBlocksPerCommand);
  DAC960_Info("  Driver Queue Depth: %d, "
	      "Scatter/Gather Limit: %d of %d Segments\n",
	      Controller, Controller->DriverQueueDepth,
	      Controller->DriverScatterGatherLimit,
	      Controller->ControllerScatterGatherLimit);
  if (Controller->FirmwareType == DAC960_V1_Controller)
    {
      DAC960_Info("  Stripe Size: %dKB, Segment Size: %dKB, "
		  "BIOS Geometry: %d/%d\n", Controller,
		  Controller->V1.StripeSize,
		  Controller->V1.SegmentSize,
		  Controller->V1.GeometryTranslationHeads,
		  Controller->V1.GeometryTranslationSectors);
      if (Controller->V1.SAFTE_EnclosureManagementEnabled)
	DAC960_Info("  SAF-TE Enclosure Management Enabled\n", Controller);
    }
  return true;
}


/*
  DAC960_V1_ReadDeviceConfiguration reads the Device Configuration Information
  for DAC960 V1 Firmware Controllers by requesting the SCSI Inquiry and SCSI
  Inquiry Unit Serial Number information for each device connected to
  Controller.
*/

static bool DAC960_V1_ReadDeviceConfiguration(DAC960_Controller_T
						 *Controller)
{
  struct dma_loaf local_dma;

  dma_addr_t DCDBs_dma[DAC960_V1_MaxChannels];
  DAC960_V1_DCDB_T *DCDBs_cpu[DAC960_V1_MaxChannels];

  dma_addr_t SCSI_Inquiry_dma[DAC960_V1_MaxChannels];
  DAC960_SCSI_Inquiry_T *SCSI_Inquiry_cpu[DAC960_V1_MaxChannels];

  dma_addr_t SCSI_NewInquiryUnitSerialNumberDMA[DAC960_V1_MaxChannels];
  DAC960_SCSI_Inquiry_UnitSerialNumber_T *SCSI_NewInquiryUnitSerialNumberCPU[DAC960_V1_MaxChannels];

  struct completion Completions[DAC960_V1_MaxChannels];
  unsigned long flags;
  int Channel, TargetID;

  if (!init_dma_loaf(Controller->PCIDevice, &local_dma, 
		DAC960_V1_MaxChannels*(sizeof(DAC960_V1_DCDB_T) +
			sizeof(DAC960_SCSI_Inquiry_T) +
			sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T))))
     return DAC960_Failure(Controller,
                        "DMA ALLOCATION FAILED IN ReadDeviceConfiguration"); 
   
  for (Channel = 0; Channel < Controller->Channels; Channel++) {
	DCDBs_cpu[Channel] = slice_dma_loaf(&local_dma,
			sizeof(DAC960_V1_DCDB_T), DCDBs_dma + Channel);
	SCSI_Inquiry_cpu[Channel] = slice_dma_loaf(&local_dma,
			sizeof(DAC960_SCSI_Inquiry_T),
			SCSI_Inquiry_dma + Channel);
	SCSI_NewInquiryUnitSerialNumberCPU[Channel] = slice_dma_loaf(&local_dma,
			sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T),
			SCSI_NewInquiryUnitSerialNumberDMA + Channel);
  }
		
  for (TargetID = 0; TargetID < Controller->Targets; TargetID++)
    {
      /*
       * For each channel, submit a probe for a device on that channel.
       * The timeout interval for a device that is present is 10 seconds.
       * With this approach, the timeout periods can elapse in parallel
       * on each channel.
       */
      for (Channel = 0; Channel < Controller->Channels; Channel++)
	{
	  dma_addr_t NewInquiryStandardDataDMA = SCSI_Inquiry_dma[Channel];
  	  DAC960_V1_DCDB_T *DCDB = DCDBs_cpu[Channel];
  	  dma_addr_t DCDB_dma = DCDBs_dma[Channel];
	  DAC960_Command_T *Command = Controller->Commands[Channel];
          struct completion *Completion = &Completions[Channel];

	  init_completion(Completion);
	  DAC960_V1_ClearCommand(Command);
	  Command->CommandType = DAC960_ImmediateCommand;
	  Command->Completion = Completion;
	  Command->V1.CommandMailbox.Type3.CommandOpcode = DAC960_V1_DCDB;
	  Command->V1.CommandMailbox.Type3.BusAddress = DCDB_dma;
	  DCDB->Channel = Channel;
	  DCDB->TargetID = TargetID;
	  DCDB->Direction = DAC960_V1_DCDB_DataTransferDeviceToSystem;
	  DCDB->EarlyStatus = false;
	  DCDB->Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	  DCDB->NoAutomaticRequestSense = false;
	  DCDB->DisconnectPermitted = true;
	  DCDB->TransferLength = sizeof(DAC960_SCSI_Inquiry_T);
	  DCDB->BusAddress = NewInquiryStandardDataDMA;
	  DCDB->CDBLength = 6;
	  DCDB->TransferLengthHigh4 = 0;
	  DCDB->SenseLength = sizeof(DCDB->SenseData);
	  DCDB->CDB[0] = 0x12; /* INQUIRY */
	  DCDB->CDB[1] = 0; /* EVPD = 0 */
	  DCDB->CDB[2] = 0; /* Page Code */
	  DCDB->CDB[3] = 0; /* Reserved */
	  DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_T);
	  DCDB->CDB[5] = 0; /* Control */

	  spin_lock_irqsave(&Controller->queue_lock, flags);
	  DAC960_QueueCommand(Command);
	  spin_unlock_irqrestore(&Controller->queue_lock, flags);
	}
      /*
       * Wait for the problems submitted in the previous loop
       * to complete.  On the probes that are successful, 
       * get the serial number of the device that was found.
       */
      for (Channel = 0; Channel < Controller->Channels; Channel++)
	{
	  DAC960_SCSI_Inquiry_T *InquiryStandardData =
	    &Controller->V1.InquiryStandardData[Channel][TargetID];
	  DAC960_SCSI_Inquiry_T *NewInquiryStandardData = SCSI_Inquiry_cpu[Channel];
	  dma_addr_t NewInquiryUnitSerialNumberDMA =
			SCSI_NewInquiryUnitSerialNumberDMA[Channel];
	  DAC960_SCSI_Inquiry_UnitSerialNumber_T *NewInquiryUnitSerialNumber =
	    		SCSI_NewInquiryUnitSerialNumberCPU[Channel];
	  DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
	    &Controller->V1.InquiryUnitSerialNumber[Channel][TargetID];
	  DAC960_Command_T *Command = Controller->Commands[Channel];
  	  DAC960_V1_DCDB_T *DCDB = DCDBs_cpu[Channel];
          struct completion *Completion = &Completions[Channel];

	  wait_for_completion(Completion);

	  if (Command->V1.CommandStatus != DAC960_V1_NormalCompletion) {
	    memset(InquiryStandardData, 0, sizeof(DAC960_SCSI_Inquiry_T));
	    InquiryStandardData->PeripheralDeviceType = 0x1F;
	    continue;
	  } else
	    memcpy(InquiryStandardData, NewInquiryStandardData, sizeof(DAC960_SCSI_Inquiry_T));
	
	  /* Preserve Channel and TargetID values from the previous loop */
	  Command->Completion = Completion;
	  DCDB->TransferLength = sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	  DCDB->BusAddress = NewInquiryUnitSerialNumberDMA;
	  DCDB->SenseLength = sizeof(DCDB->SenseData);
	  DCDB->CDB[0] = 0x12; /* INQUIRY */
	  DCDB->CDB[1] = 1; /* EVPD = 1 */
	  DCDB->CDB[2] = 0x80; /* Page Code */
	  DCDB->CDB[3] = 0; /* Reserved */
	  DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	  DCDB->CDB[5] = 0; /* Control */

	  spin_lock_irqsave(&Controller->queue_lock, flags);
	  DAC960_QueueCommand(Command);
	  spin_unlock_irqrestore(&Controller->queue_lock, flags);
	  wait_for_completion(Completion);

	  if (Command->V1.CommandStatus != DAC960_V1_NormalCompletion) {
	  	memset(InquiryUnitSerialNumber, 0,
			sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
	  	InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
	  } else
	  	memcpy(InquiryUnitSerialNumber, NewInquiryUnitSerialNumber,
			sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
	}
    }
    free_dma_loaf(Controller->PCIDevice, &local_dma);
  return true;
}


/*
  DAC960_V2_ReadDeviceConfiguration reads the Device Configuration Information
  for DAC960 V2 Firmware Controllers by requesting the Physical Device
  Information and SCSI Inquiry Unit Serial Number information for each
  device connected to Controller.
*/

static bool DAC960_V2_ReadDeviceConfiguration(DAC960_Controller_T
						 *Controller)
{
  unsigned char Channel = 0, TargetID = 0, LogicalUnit = 0;
  unsigned short PhysicalDeviceIndex = 0;

  while (true)
    {
      DAC960_V2_PhysicalDeviceInfo_T *NewPhysicalDeviceInfo =
		Controller->V2.NewPhysicalDeviceInformation;
      DAC960_V2_PhysicalDeviceInfo_T *PhysicalDeviceInfo;
      DAC960_SCSI_Inquiry_UnitSerialNumber_T *NewInquiryUnitSerialNumber =
		Controller->V2.NewInquiryUnitSerialNumber;
      DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber;

      if (!DAC960_V2_NewPhysicalDeviceInfo(Controller, Channel, TargetID, LogicalUnit))
	  break;

      PhysicalDeviceInfo = kmalloc(sizeof(DAC960_V2_PhysicalDeviceInfo_T),
				    GFP_ATOMIC);
      if (PhysicalDeviceInfo == NULL)
		return DAC960_Failure(Controller, "PHYSICAL DEVICE ALLOCATION");
      Controller->V2.PhysicalDeviceInformation[PhysicalDeviceIndex] =
		PhysicalDeviceInfo;
      memcpy(PhysicalDeviceInfo, NewPhysicalDeviceInfo,
		sizeof(DAC960_V2_PhysicalDeviceInfo_T));

      InquiryUnitSerialNumber = kmalloc(
	      sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T), GFP_ATOMIC);
      if (InquiryUnitSerialNumber == NULL) {
	kfree(PhysicalDeviceInfo);
	return DAC960_Failure(Controller, "SERIAL NUMBER ALLOCATION");
      }
      Controller->V2.InquiryUnitSerialNumber[PhysicalDeviceIndex] =
		InquiryUnitSerialNumber;

      Channel = NewPhysicalDeviceInfo->Channel;
      TargetID = NewPhysicalDeviceInfo->TargetID;
      LogicalUnit = NewPhysicalDeviceInfo->LogicalUnit;

      /*
	 Some devices do NOT have Unit Serial Numbers.
	 This command fails for them.  But, we still want to
	 remember those devices are there.  Construct a
	 UnitSerialNumber structure for the failure case.
      */
      if (!DAC960_V2_NewInquiryUnitSerialNumber(Controller, Channel, TargetID, LogicalUnit)) {
      	memset(InquiryUnitSerialNumber, 0,
             sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
     	InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
      } else
      	memcpy(InquiryUnitSerialNumber, NewInquiryUnitSerialNumber,
		sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));

      PhysicalDeviceIndex++;
      LogicalUnit++;
    }
  return true;
}


/*
  DAC960_SanitizeInquiryData sanitizes the Vendor, Model, Revision, and
  Product Serial Number fields of the Inquiry Standard Data and Inquiry
  Unit Serial Number structures.
*/

static void DAC960_SanitizeInquiryData(DAC960_SCSI_Inquiry_T
					 *InquiryStandardData,
				       DAC960_SCSI_Inquiry_UnitSerialNumber_T
					 *InquiryUnitSerialNumber,
				       unsigned char *Vendor,
				       unsigned char *Model,
				       unsigned char *Revision,
				       unsigned char *SerialNumber)
{
  int SerialNumberLength, i;
  if (InquiryStandardData->PeripheralDeviceType == 0x1F) return;
  for (i = 0; i < sizeof(InquiryStandardData->VendorIdentification); i++)
    {
      unsigned char VendorCharacter =
	InquiryStandardData->VendorIdentification[i];
      Vendor[i] = (VendorCharacter >= ' ' && VendorCharacter <= '~'
		   ? VendorCharacter : ' ');
    }
  Vendor[sizeof(InquiryStandardData->VendorIdentification)] = '\0';
  for (i = 0; i < sizeof(InquiryStandardData->ProductIdentification); i++)
    {
      unsigned char ModelCharacter =
	InquiryStandardData->ProductIdentification[i];
      Model[i] = (ModelCharacter >= ' ' && ModelCharacter <= '~'
		  ? ModelCharacter : ' ');
    }
  Model[sizeof(InquiryStandardData->ProductIdentification)] = '\0';
  for (i = 0; i < sizeof(InquiryStandardData->ProductRevisionLevel); i++)
    {
      unsigned char RevisionCharacter =
	InquiryStandardData->ProductRevisionLevel[i];
      Revision[i] = (RevisionCharacter >= ' ' && RevisionCharacter <= '~'
		     ? RevisionCharacter : ' ');
    }
  Revision[sizeof(InquiryStandardData->ProductRevisionLevel)] = '\0';
  if (InquiryUnitSerialNumber->PeripheralDeviceType == 0x1F) return;
  SerialNumberLength = InquiryUnitSerialNumber->PageLength;
  if (SerialNumberLength >
      sizeof(InquiryUnitSerialNumber->ProductSerialNumber))
    SerialNumberLength = sizeof(InquiryUnitSerialNumber->ProductSerialNumber);
  for (i = 0; i < SerialNumberLength; i++)
    {
      unsigned char SerialNumberCharacter =
	InquiryUnitSerialNumber->ProductSerialNumber[i];
      SerialNumber[i] =
	(SerialNumberCharacter >= ' ' && SerialNumberCharacter <= '~'
	 ? SerialNumberCharacter : ' ');
    }
  SerialNumber[SerialNumberLength] = '\0';
}


/*
  DAC960_V1_ReportDeviceConfiguration reports the Device Configuration
  Information for DAC960 V1 Firmware Controllers.
*/

static bool DAC960_V1_ReportDeviceConfiguration(DAC960_Controller_T
						   *Controller)
{
  int LogicalDriveNumber, Channel, TargetID;
  DAC960_Info("  Physical Devices:\n", Controller);
  for (Channel = 0; Channel < Controller->Channels; Channel++)
    for (TargetID = 0; TargetID < Controller->Targets; TargetID++)
      {
	DAC960_SCSI_Inquiry_T *InquiryStandardData =
	  &Controller->V1.InquiryStandardData[Channel][TargetID];
	DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
	  &Controller->V1.InquiryUnitSerialNumber[Channel][TargetID];
	DAC960_V1_DeviceState_T *DeviceState =
	  &Controller->V1.DeviceState[Channel][TargetID];
	DAC960_V1_ErrorTableEntry_T *ErrorEntry =
	  &Controller->V1.ErrorTable.ErrorTableEntries[Channel][TargetID];
	char Vendor[1+sizeof(InquiryStandardData->VendorIdentification)];
	char Model[1+sizeof(InquiryStandardData->ProductIdentification)];
	char Revision[1+sizeof(InquiryStandardData->ProductRevisionLevel)];
	char SerialNumber[1+sizeof(InquiryUnitSerialNumber
				   ->ProductSerialNumber)];
	if (InquiryStandardData->PeripheralDeviceType == 0x1F) continue;
	DAC960_SanitizeInquiryData(InquiryStandardData, InquiryUnitSerialNumber,
				   Vendor, Model, Revision, SerialNumber);
	DAC960_Info("    %d:%d%s Vendor: %s  Model: %s  Revision: %s\n",
		    Controller, Channel, TargetID, (TargetID < 10 ? " " : ""),
		    Vendor, Model, Revision);
	if (InquiryUnitSerialNumber->PeripheralDeviceType != 0x1F)
	  DAC960_Info("         Serial Number: %s\n", Controller, SerialNumber);
	if (DeviceState->Present &&
	    DeviceState->DeviceType == DAC960_V1_DiskType)
	  {
	    if (Controller->V1.DeviceResetCount[Channel][TargetID] > 0)
	      DAC960_Info("         Disk Status: %s, %u blocks, %d resets\n",
			  Controller,
			  (DeviceState->DeviceState == DAC960_V1_Device_Dead
			   ? "Dead"
			   : DeviceState->DeviceState
			     == DAC960_V1_Device_WriteOnly
			     ? "Write-Only"
			     : DeviceState->DeviceState
			       == DAC960_V1_Device_Online
			       ? "Online" : "Standby"),
			  DeviceState->DiskSize,
			  Controller->V1.DeviceResetCount[Channel][TargetID]);
	    else
	      DAC960_Info("         Disk Status: %s, %u blocks\n", Controller,
			  (DeviceState->DeviceState == DAC960_V1_Device_Dead
			   ? "Dead"
			   : DeviceState->DeviceState
			     == DAC960_V1_Device_WriteOnly
			     ? "Write-Only"
			     : DeviceState->DeviceState
			       == DAC960_V1_Device_Online
			       ? "Online" : "Standby"),
			  DeviceState->DiskSize);
	  }
	if (ErrorEntry->ParityErrorCount > 0 ||
	    ErrorEntry->SoftErrorCount > 0 ||
	    ErrorEntry->HardErrorCount > 0 ||
	    ErrorEntry->MiscErrorCount > 0)
	  DAC960_Info("         Errors - Parity: %d, Soft: %d, "
		      "Hard: %d, Misc: %d\n", Controller,
		      ErrorEntry->ParityErrorCount,
		      ErrorEntry->SoftErrorCount,
		      ErrorEntry->HardErrorCount,
		      ErrorEntry->MiscErrorCount);
      }
  DAC960_Info("  Logical Drives:\n", Controller);
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
    {
      DAC960_V1_LogicalDriveInformation_T *LogicalDriveInformation =
	&Controller->V1.LogicalDriveInformation[LogicalDriveNumber];
      DAC960_Info("    /dev/rd/c%dd%d: RAID-%d, %s, %u blocks, %s\n",
		  Controller, Controller->ControllerNumber, LogicalDriveNumber,
		  LogicalDriveInformation->RAIDLevel,
		  (LogicalDriveInformation->LogicalDriveState
		   == DAC960_V1_LogicalDrive_Online
		   ? "Online"
		   : LogicalDriveInformation->LogicalDriveState
		     == DAC960_V1_LogicalDrive_Critical
		     ? "Critical" : "Offline"),
		  LogicalDriveInformation->LogicalDriveSize,
		  (LogicalDriveInformation->WriteBack
		   ? "Write Back" : "Write Thru"));
    }
  return true;
}


/*
  DAC960_V2_ReportDeviceConfiguration reports the Device Configuration
  Information for DAC960 V2 Firmware Controllers.
*/

static bool DAC960_V2_ReportDeviceConfiguration(DAC960_Controller_T
						   *Controller)
{
  int PhysicalDeviceIndex, LogicalDriveNumber;
  DAC960_Info("  Physical Devices:\n", Controller);
  for (PhysicalDeviceIndex = 0;
       PhysicalDeviceIndex < DAC960_V2_MaxPhysicalDevices;
       PhysicalDeviceIndex++)
    {
      DAC960_V2_PhysicalDeviceInfo_T *PhysicalDeviceInfo =
	Controller->V2.PhysicalDeviceInformation[PhysicalDeviceIndex];
      DAC960_SCSI_Inquiry_T *InquiryStandardData =
	(DAC960_SCSI_Inquiry_T *) &PhysicalDeviceInfo->SCSI_InquiryData;
      DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
	Controller->V2.InquiryUnitSerialNumber[PhysicalDeviceIndex];
      char Vendor[1+sizeof(InquiryStandardData->VendorIdentification)];
      char Model[1+sizeof(InquiryStandardData->ProductIdentification)];
      char Revision[1+sizeof(InquiryStandardData->ProductRevisionLevel)];
      char SerialNumber[1+sizeof(InquiryUnitSerialNumber->ProductSerialNumber)];
      if (PhysicalDeviceInfo == NULL) break;
      DAC960_SanitizeInquiryData(InquiryStandardData, InquiryUnitSerialNumber,
				 Vendor, Model, Revision, SerialNumber);
      DAC960_Info("    %d:%d%s Vendor: %s  Model: %s  Revision: %s\n",
		  Controller,
		  PhysicalDeviceInfo->Channel,
		  PhysicalDeviceInfo->TargetID,
		  (PhysicalDeviceInfo->TargetID < 10 ? " " : ""),
		  Vendor, Model, Revision);
      if (PhysicalDeviceInfo->NegotiatedSynchronousMegaTransfers == 0)
	DAC960_Info("         %sAsynchronous\n", Controller,
		    (PhysicalDeviceInfo->NegotiatedDataWidthBits == 16
		     ? "Wide " :""));
      else
	DAC960_Info("         %sSynchronous at %d MB/sec\n", Controller,
		    (PhysicalDeviceInfo->NegotiatedDataWidthBits == 16
		     ? "Wide " :""),
		    (PhysicalDeviceInfo->NegotiatedSynchronousMegaTransfers
		     * PhysicalDeviceInfo->NegotiatedDataWidthBits/8));
      if (InquiryUnitSerialNumber->PeripheralDeviceType != 0x1F)
	DAC960_Info("         Serial Number: %s\n", Controller, SerialNumber);
      if (PhysicalDeviceInfo->PhysicalDeviceState ==
	  DAC960_V2_Device_Unconfigured)
	continue;
      DAC960_Info("         Disk Status: %s, %u blocks\n", Controller,
		  (PhysicalDeviceInfo->PhysicalDeviceState
		   == DAC960_V2_Device_Online
		   ? "Online"
		   : PhysicalDeviceInfo->PhysicalDeviceState
		     == DAC960_V2_Device_Rebuild
		     ? "Rebuild"
		     : PhysicalDeviceInfo->PhysicalDeviceState
		       == DAC960_V2_Device_Missing
		       ? "Missing"
		       : PhysicalDeviceInfo->PhysicalDeviceState
			 == DAC960_V2_Device_Critical
			 ? "Critical"
			 : PhysicalDeviceInfo->PhysicalDeviceState
			   == DAC960_V2_Device_Dead
			   ? "Dead"
			   : PhysicalDeviceInfo->PhysicalDeviceState
			     == DAC960_V2_Device_SuspectedDead
			     ? "Suspected-Dead"
			     : PhysicalDeviceInfo->PhysicalDeviceState
			       == DAC960_V2_Device_CommandedOffline
			       ? "Commanded-Offline"
			       : PhysicalDeviceInfo->PhysicalDeviceState
				 == DAC960_V2_Device_Standby
				 ? "Standby" : "Unknown"),
		  PhysicalDeviceInfo->ConfigurableDeviceSize);
      if (PhysicalDeviceInfo->ParityErrors == 0 &&
	  PhysicalDeviceInfo->SoftErrors == 0 &&
	  PhysicalDeviceInfo->HardErrors == 0 &&
	  PhysicalDeviceInfo->MiscellaneousErrors == 0 &&
	  PhysicalDeviceInfo->CommandTimeouts == 0 &&
	  PhysicalDeviceInfo->Retries == 0 &&
	  PhysicalDeviceInfo->Aborts == 0 &&
	  PhysicalDeviceInfo->PredictedFailuresDetected == 0)
	continue;
      DAC960_Info("         Errors - Parity: %d, Soft: %d, "
		  "Hard: %d, Misc: %d\n", Controller,
		  PhysicalDeviceInfo->ParityErrors,
		  PhysicalDeviceInfo->SoftErrors,
		  PhysicalDeviceInfo->HardErrors,
		  PhysicalDeviceInfo->MiscellaneousErrors);
      DAC960_Info("                  Timeouts: %d, Retries: %d, "
		  "Aborts: %d, Predicted: %d\n", Controller,
		  PhysicalDeviceInfo->CommandTimeouts,
		  PhysicalDeviceInfo->Retries,
		  PhysicalDeviceInfo->Aborts,
		  PhysicalDeviceInfo->PredictedFailuresDetected);
    }
  DAC960_Info("  Logical Drives:\n", Controller);
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < DAC960_MaxLogicalDrives;
       LogicalDriveNumber++)
    {
      DAC960_V2_LogicalDeviceInfo_T *LogicalDeviceInfo =
	Controller->V2.LogicalDeviceInformation[LogicalDriveNumber];
      unsigned char *ReadCacheStatus[] = { "Read Cache Disabled",
					   "Read Cache Enabled",
					   "Read Ahead Enabled",
					   "Intelligent Read Ahead Enabled",
					   "-", "-", "-", "-" };
      unsigned char *WriteCacheStatus[] = { "Write Cache Disabled",
					    "Logical Device Read Only",
					    "Write Cache Enabled",
					    "Intelligent Write Cache Enabled",
					    "-", "-", "-", "-" };
      unsigned char *GeometryTranslation;
      if (LogicalDeviceInfo == NULL) continue;
      switch (LogicalDeviceInfo->DriveGeometry)
	{
	case DAC960_V2_Geometry_128_32:
	  GeometryTranslation = "128/32";
	  break;
	case DAC960_V2_Geometry_255_63:
	  GeometryTranslation = "255/63";
	  break;
	default:
	  GeometryTranslation = "Invalid";
	  DAC960_Error("Illegal Logical Device Geometry %d\n",
		       Controller, LogicalDeviceInfo->DriveGeometry);
	  break;
	}
      DAC960_Info("    /dev/rd/c%dd%d: RAID-%d, %s, %u blocks\n",
		  Controller, Controller->ControllerNumber, LogicalDriveNumber,
		  LogicalDeviceInfo->RAIDLevel,
		  (LogicalDeviceInfo->LogicalDeviceState
		   == DAC960_V2_LogicalDevice_Online
		   ? "Online"
		   : LogicalDeviceInfo->LogicalDeviceState
		     == DAC960_V2_LogicalDevice_Critical
		     ? "Critical" : "Offline"),
		  LogicalDeviceInfo->ConfigurableDeviceSize);
      DAC960_Info("                  Logical Device %s, BIOS Geometry: %s\n",
		  Controller,
		  (LogicalDeviceInfo->LogicalDeviceControl
				     .LogicalDeviceInitialized
		   ? "Initialized" : "Uninitialized"),
		  GeometryTranslation);
      if (LogicalDeviceInfo->StripeSize == 0)
	{
	  if (LogicalDeviceInfo->CacheLineSize == 0)
	    DAC960_Info("                  Stripe Size: N/A, "
			"Segment Size: N/A\n", Controller);
	  else
	    DAC960_Info("                  Stripe Size: N/A, "
			"Segment Size: %dKB\n", Controller,
			1 << (LogicalDeviceInfo->CacheLineSize - 2));
	}
      else
	{
	  if (LogicalDeviceInfo->CacheLineSize == 0)
	    DAC960_Info("                  Stripe Size: %dKB, "
			"Segment Size: N/A\n", Controller,
			1 << (LogicalDeviceInfo->StripeSize - 2));
	  else
	    DAC960_Info("                  Stripe Size: %dKB, "
			"Segment Size: %dKB\n", Controller,
			1 << (LogicalDeviceInfo->StripeSize - 2),
			1 << (LogicalDeviceInfo->CacheLineSize - 2));
	}
      DAC960_Info("                  %s, %s\n", Controller,
		  ReadCacheStatus[
		    LogicalDeviceInfo->LogicalDeviceControl.ReadCache],
		  WriteCacheStatus[
		    LogicalDeviceInfo->LogicalDeviceControl.WriteCache]);
      if (LogicalDeviceInfo->SoftErrors > 0 ||
	  LogicalDeviceInfo->CommandsFailed > 0 ||
	  LogicalDeviceInfo->DeferredWriteErrors)
	DAC960_Info("                  Errors - Soft: %d, Failed: %d, "
		    "Deferred Write: %d\n", Controller,
		    LogicalDeviceInfo->SoftErrors,
		    LogicalDeviceInfo->CommandsFailed,
		    LogicalDeviceInfo->DeferredWriteErrors);

    }
  return true;
}

/*
  DAC960_RegisterBlockDevice registers the Block Device structures
  associated with Controller.
*/

static bool DAC960_RegisterBlockDevice(DAC960_Controller_T *Controller)
{
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  int n;

  /*
    Register the Block Device Major Number for this DAC960 Controller.
  */
  if (register_blkdev(MajorNumber, "dac960") < 0)
      return false;

  for (n = 0; n < DAC960_MaxLogicalDrives; n++) {
	struct gendisk *disk = Controller->disks[n];
  	struct request_queue *RequestQueue;

	/* for now, let all request queues share controller's lock */
  	RequestQueue = blk_init_queue(DAC960_RequestFunction,&Controller->queue_lock);
  	if (!RequestQueue) {
		printk("DAC960: failure to allocate request queue\n");
		continue;
  	}
  	Controller->RequestQueue[n] = RequestQueue;
  	RequestQueue->queuedata = Controller;
	blk_queue_max_segments(RequestQueue, Controller->DriverScatterGatherLimit);
	blk_queue_max_hw_sectors(RequestQueue, Controller->MaxBlocksPerCommand);
	disk->queue = RequestQueue;
	sprintf(disk->disk_name, "rd/c%dd%d", Controller->ControllerNumber, n);
	disk->major = MajorNumber;
	disk->first_minor = n << DAC960_MaxPartitionsBits;
	disk->fops = &DAC960_BlockDeviceOperations;
   }
  /*
    Indicate the Block Device Registration completed successfully,
  */
  return true;
}


/*
  DAC960_UnregisterBlockDevice unregisters the Block Device structures
  associated with Controller.
*/

static void DAC960_UnregisterBlockDevice(DAC960_Controller_T *Controller)
{
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  int disk;

  /* does order matter when deleting gendisk and cleanup in request queue? */
  for (disk = 0; disk < DAC960_MaxLogicalDrives; disk++) {
	del_gendisk(Controller->disks[disk]);
	blk_cleanup_queue(Controller->RequestQueue[disk]);
	Controller->RequestQueue[disk] = NULL;
  }

  /*
    Unregister the Block Device Major Number for this DAC960 Controller.
  */
  unregister_blkdev(MajorNumber, "dac960");
}

/*
  DAC960_ComputeGenericDiskInfo computes the values for the Generic Disk
  Information Partition Sector Counts and Block Sizes.
*/

static void DAC960_ComputeGenericDiskInfo(DAC960_Controller_T *Controller)
{
	int disk;
	for (disk = 0; disk < DAC960_MaxLogicalDrives; disk++)
		set_capacity(Controller->disks[disk], disk_size(Controller, disk));
}

/*
  DAC960_ReportErrorStatus reports Controller BIOS Messages passed through
  the Error Status Register when the driver performs the BIOS handshaking.
  It returns true for fatal errors and false otherwise.
*/

static bool DAC960_ReportErrorStatus(DAC960_Controller_T *Controller,
					unsigned char ErrorStatus,
					unsigned char Parameter0,
					unsigned char Parameter1)
{
  switch (ErrorStatus)
    {
    case 0x00:
      DAC960_Notice("Physical Device %d:%d Not Responding\n",
		    Controller, Parameter1, Parameter0);
      break;
    case 0x08:
      if (Controller->DriveSpinUpMessageDisplayed) break;
      DAC960_Notice("Spinning Up Drives\n", Controller);
      Controller->DriveSpinUpMessageDisplayed = true;
      break;
    case 0x30:
      DAC960_Notice("Configuration Checksum Error\n", Controller);
      break;
    case 0x60:
      DAC960_Notice("Mirror Race Recovery Failed\n", Controller);
      break;
    case 0x70:
      DAC960_Notice("Mirror Race Recovery In Progress\n", Controller);
      break;
    case 0x90:
      DAC960_Notice("Physical Device %d:%d COD Mismatch\n",
		    Controller, Parameter1, Parameter0);
      break;
    case 0xA0:
      DAC960_Notice("Logical Drive Installation Aborted\n", Controller);
      break;
    case 0xB0:
      DAC960_Notice("Mirror Race On A Critical Logical Drive\n", Controller);
      break;
    case 0xD0:
      DAC960_Notice("New Controller Configuration Found\n", Controller);
      break;
    case 0xF0:
      DAC960_Error("Fatal Memory Parity Error for Controller at\n", Controller);
      return true;
    default:
      DAC960_Error("Unknown Initialization Error %02X for Controller at\n",
		   Controller, ErrorStatus);
      return true;
    }
  return false;
}


/*
 * DAC960_DetectCleanup releases the resources that were allocated
 * during DAC960_DetectController().  DAC960_DetectController can
 * has several internal failure points, so not ALL resources may 
 * have been allocated.  It's important to free only
 * resources that HAVE been allocated.  The code below always
 * tests that the resource has been allocated before attempting to
 * free it.
 */
static void DAC960_DetectCleanup(DAC960_Controller_T *Controller)
{
  int i;

  /* Free the memory mailbox, status, and related structures */
  free_dma_loaf(Controller->PCIDevice, &Controller->DmaPages);
  if (Controller->MemoryMappedAddress) {
  	switch(Controller->HardwareType)
  	{
		case DAC960_GEM_Controller:
			DAC960_GEM_DisableInterrupts(Controller->BaseAddress);
			break;
		case DAC960_BA_Controller:
			DAC960_BA_DisableInterrupts(Controller->BaseAddress);
			break;
		case DAC960_LP_Controller:
			DAC960_LP_DisableInterrupts(Controller->BaseAddress);
			break;
		case DAC960_LA_Controller:
			DAC960_LA_DisableInterrupts(Controller->BaseAddress);
			break;
		case DAC960_PG_Controller:
			DAC960_PG_DisableInterrupts(Controller->BaseAddress);
			break;
		case DAC960_PD_Controller:
			DAC960_PD_DisableInterrupts(Controller->BaseAddress);
			break;
		case DAC960_P_Controller:
			DAC960_PD_DisableInterrupts(Controller->BaseAddress);
			break;
  	}
  	iounmap(Controller->MemoryMappedAddress);
  }
  if (Controller->IRQ_Channel)
  	free_irq(Controller->IRQ_Channel, Controller);
  if (Controller->IO_Address)
	release_region(Controller->IO_Address, 0x80);
  pci_disable_device(Controller->PCIDevice);
  for (i = 0; (i < DAC960_MaxLogicalDrives) && Controller->disks[i]; i++)
       put_disk(Controller->disks[i]);
  DAC960_Controllers[Controller->ControllerNumber] = NULL;
  kfree(Controller);
}


/*
  DAC960_DetectController detects Mylex DAC960/AcceleRAID/eXtremeRAID
  PCI RAID Controllers by interrogating the PCI Configuration Space for
  Controller Type.
*/

static DAC960_Controller_T * 
DAC960_DetectController(struct pci_dev *PCI_Device,
			const struct pci_device_id *entry)
{
  struct DAC960_privdata *privdata =
	  	(struct DAC960_privdata *)entry->driver_data;
  irq_handler_t InterruptHandler = privdata->InterruptHandler;
  unsigned int MemoryWindowSize = privdata->MemoryWindowSize;
  DAC960_Controller_T *Controller = NULL;
  unsigned char DeviceFunction = PCI_Device->devfn;
  unsigned char ErrorStatus, Parameter0, Parameter1;
  unsigned int IRQ_Channel;
  void __iomem *BaseAddress;
  int i;

  Controller = kzalloc(sizeof(DAC960_Controller_T), GFP_ATOMIC);
  if (Controller == NULL) {
	DAC960_Error("Unable to allocate Controller structure for "
                       "Controller at\n", NULL);
	return NULL;
  }
  Controller->ControllerNumber = DAC960_ControllerCount;
  DAC960_Controllers[DAC960_ControllerCount++] = Controller;
  Controller->Bus = PCI_Device->bus->number;
  Controller->FirmwareType = privdata->FirmwareType;
  Controller->HardwareType = privdata->HardwareType;
  Controller->Device = DeviceFunction >> 3;
  Controller->Function = DeviceFunction & 0x7;
  Controller->PCIDevice = PCI_Device;
  strcpy(Controller->FullModelName, "DAC960");

  if (pci_enable_device(PCI_Device))
	goto Failure;

  switch (Controller->HardwareType)
  {
	case DAC960_GEM_Controller:
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 0);
	  break;
	case DAC960_BA_Controller:
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 0);
	  break;
	case DAC960_LP_Controller:
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 0);
	  break;
	case DAC960_LA_Controller:
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 0);
	  break;
	case DAC960_PG_Controller:
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 0);
	  break;
	case DAC960_PD_Controller:
	  Controller->IO_Address = pci_resource_start(PCI_Device, 0);
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 1);
	  break;
	case DAC960_P_Controller:
	  Controller->IO_Address = pci_resource_start(PCI_Device, 0);
	  Controller->PCI_Address = pci_resource_start(PCI_Device, 1);
	  break;
  }

  pci_set_drvdata(PCI_Device, (void *)((long)Controller->ControllerNumber));
  for (i = 0; i < DAC960_MaxLogicalDrives; i++) {
	Controller->disks[i] = alloc_disk(1<<DAC960_MaxPartitionsBits);
	if (!Controller->disks[i])
		goto Failure;
	Controller->disks[i]->private_data = (void *)((long)i);
  }
  init_waitqueue_head(&Controller->CommandWaitQueue);
  init_waitqueue_head(&Controller->HealthStatusWaitQueue);
  spin_lock_init(&Controller->queue_lock);
  DAC960_AnnounceDriver(Controller);
  /*
    Map the Controller Register Window.
  */
 if (MemoryWindowSize < PAGE_SIZE)
	MemoryWindowSize = PAGE_SIZE;
  Controller->MemoryMappedAddress =
	ioremap_nocache(Controller->PCI_Address & PAGE_MASK, MemoryWindowSize);
  Controller->BaseAddress =
	Controller->MemoryMappedAddress + (Controller->PCI_Address & ~PAGE_MASK);
  if (Controller->MemoryMappedAddress == NULL)
  {
	  DAC960_Error("Unable to map Controller Register Window for "
		       "Controller at\n", Controller);
	  goto Failure;
  }
  BaseAddress = Controller->BaseAddress;
  switch (Controller->HardwareType)
  {
	case DAC960_GEM_Controller:
	  DAC960_GEM_DisableInterrupts(BaseAddress);
	  DAC960_GEM_AcknowledgeHardwareMailboxStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_GEM_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_GEM_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V2_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_GEM_EnableInterrupts(BaseAddress);
	  Controller->QueueCommand = DAC960_GEM_QueueCommand;
	  Controller->ReadControllerConfiguration =
	    DAC960_V2_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V2_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V2_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V2_QueueReadWriteCommand;
	  break;
	case DAC960_BA_Controller:
	  DAC960_BA_DisableInterrupts(BaseAddress);
	  DAC960_BA_AcknowledgeHardwareMailboxStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_BA_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_BA_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V2_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_BA_EnableInterrupts(BaseAddress);
	  Controller->QueueCommand = DAC960_BA_QueueCommand;
	  Controller->ReadControllerConfiguration =
	    DAC960_V2_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V2_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V2_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V2_QueueReadWriteCommand;
	  break;
	case DAC960_LP_Controller:
	  DAC960_LP_DisableInterrupts(BaseAddress);
	  DAC960_LP_AcknowledgeHardwareMailboxStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_LP_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_LP_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V2_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_LP_EnableInterrupts(BaseAddress);
	  Controller->QueueCommand = DAC960_LP_QueueCommand;
	  Controller->ReadControllerConfiguration =
	    DAC960_V2_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V2_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V2_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V2_QueueReadWriteCommand;
	  break;
	case DAC960_LA_Controller:
	  DAC960_LA_DisableInterrupts(BaseAddress);
	  DAC960_LA_AcknowledgeHardwareMailboxStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_LA_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_LA_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V1_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_LA_EnableInterrupts(BaseAddress);
	  if (Controller->V1.DualModeMemoryMailboxInterface)
	    Controller->QueueCommand = DAC960_LA_QueueCommandDualMode;
	  else Controller->QueueCommand = DAC960_LA_QueueCommandSingleMode;
	  Controller->ReadControllerConfiguration =
	    DAC960_V1_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V1_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V1_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V1_QueueReadWriteCommand;
	  break;
	case DAC960_PG_Controller:
	  DAC960_PG_DisableInterrupts(BaseAddress);
	  DAC960_PG_AcknowledgeHardwareMailboxStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_PG_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_PG_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V1_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable Memory Mailbox Interface "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_PG_EnableInterrupts(BaseAddress);
	  if (Controller->V1.DualModeMemoryMailboxInterface)
	    Controller->QueueCommand = DAC960_PG_QueueCommandDualMode;
	  else Controller->QueueCommand = DAC960_PG_QueueCommandSingleMode;
	  Controller->ReadControllerConfiguration =
	    DAC960_V1_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V1_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V1_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V1_QueueReadWriteCommand;
	  break;
	case DAC960_PD_Controller:
	  if (!request_region(Controller->IO_Address, 0x80,
			      Controller->FullModelName)) {
		DAC960_Error("IO port 0x%lx busy for Controller at\n",
			     Controller, Controller->IO_Address);
		goto Failure;
	  }
	  DAC960_PD_DisableInterrupts(BaseAddress);
	  DAC960_PD_AcknowledgeStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_PD_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_PD_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V1_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to allocate DMA mapped memory "
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_PD_EnableInterrupts(BaseAddress);
	  Controller->QueueCommand = DAC960_PD_QueueCommand;
	  Controller->ReadControllerConfiguration =
	    DAC960_V1_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V1_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V1_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V1_QueueReadWriteCommand;
	  break;
	case DAC960_P_Controller:
	  if (!request_region(Controller->IO_Address, 0x80,
			      Controller->FullModelName)){
		DAC960_Error("IO port 0x%lx busy for Controller at\n",
		   	     Controller, Controller->IO_Address);
		goto Failure;
	  }
	  DAC960_PD_DisableInterrupts(BaseAddress);
	  DAC960_PD_AcknowledgeStatus(BaseAddress);
	  udelay(1000);
	  while (DAC960_PD_InitializationInProgressP(BaseAddress))
	    {
	      if (DAC960_PD_ReadErrorStatus(BaseAddress, &ErrorStatus,
					    &Parameter0, &Parameter1) &&
		  DAC960_ReportErrorStatus(Controller, ErrorStatus,
					   Parameter0, Parameter1))
		goto Failure;
	      udelay(10);
	    }
	  if (!DAC960_V1_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to allocate DMA mapped memory"
			   "for Controller at\n", Controller);
	      goto Failure;
	    }
	  DAC960_PD_EnableInterrupts(BaseAddress);
	  Controller->QueueCommand = DAC960_P_QueueCommand;
	  Controller->ReadControllerConfiguration =
	    DAC960_V1_ReadControllerConfiguration;
	  Controller->ReadDeviceConfiguration =
	    DAC960_V1_ReadDeviceConfiguration;
	  Controller->ReportDeviceConfiguration =
	    DAC960_V1_ReportDeviceConfiguration;
	  Controller->QueueReadWriteCommand =
	    DAC960_V1_QueueReadWriteCommand;
	  break;
  }
  /*
     Acquire shared access to the IRQ Channel.
  */
  IRQ_Channel = PCI_Device->irq;
  if (request_irq(IRQ_Channel, InterruptHandler, IRQF_SHARED,
		      Controller->FullModelName, Controller) < 0)
  {
	DAC960_Error("Unable to acquire IRQ Channel %d for Controller at\n",
		       Controller, Controller->IRQ_Channel);
	goto Failure;
  }
  Controller->IRQ_Channel = IRQ_Channel;
  Controller->InitialCommand.CommandIdentifier = 1;
  Controller->InitialCommand.Controller = Controller;
  Controller->Commands[0] = &Controller->InitialCommand;
  Controller->FreeCommands = &Controller->InitialCommand;
  return Controller;
      
Failure:
  if (Controller->IO_Address == 0)
	DAC960_Error("PCI Bus %d Device %d Function %d I/O Address N/A "
		     "PCI Address 0x%X\n", Controller,
		     Controller->Bus, Controller->Device,
		     Controller->Function, Controller->PCI_Address);
  else
	DAC960_Error("PCI Bus %d Device %d Function %d I/O Address "
			"0x%X PCI Address 0x%X\n", Controller,
			Controller->Bus, Controller->Device,
			Controller->Function, Controller->IO_Address,
			Controller->PCI_Address);
  DAC960_DetectCleanup(Controller);
  DAC960_ControllerCount--;
  return NULL;
}

/*
  DAC960_InitializeController initializes Controller.
*/

static bool 
DAC960_InitializeController(DAC960_Controller_T *Controller)
{
  if (DAC960_ReadControllerConfiguration(Controller) &&
      DAC960_ReportControllerConfiguration(Controller) &&
      DAC960_CreateAuxiliaryStructures(Controller) &&
      DAC960_ReadDeviceConfiguration(Controller) &&
      DAC960_ReportDeviceConfiguration(Controller) &&
      DAC960_RegisterBlockDevice(Controller))
    {
      /*
	Initialize the Monitoring Timer.
      */
      timer_setup(&Controller->MonitoringTimer,
                  DAC960_MonitoringTimerFunction, 0);
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_MonitoringTimerInterval;
      add_timer(&Controller->MonitoringTimer);
      Controller->ControllerInitialized = true;
      return true;
    }
  return false;
}


/*
  DAC960_FinalizeController finalizes Controller.
*/

static void DAC960_FinalizeController(DAC960_Controller_T *Controller)
{
  if (Controller->ControllerInitialized)
    {
      unsigned long flags;

      /*
       * Acquiring and releasing lock here eliminates
       * a very low probability race.
       *
       * The code below allocates controller command structures
       * from the free list without holding the controller lock.
       * This is safe assuming there is no other activity on
       * the controller at the time.
       * 
       * But, there might be a monitoring command still
       * in progress.  Setting the Shutdown flag while holding
       * the lock ensures that there is no monitoring command
       * in the interrupt handler currently, and any monitoring
       * commands that complete from this time on will NOT return
       * their command structure to the free list.
       */

      spin_lock_irqsave(&Controller->queue_lock, flags);
      Controller->ShutdownMonitoringTimer = 1;
      spin_unlock_irqrestore(&Controller->queue_lock, flags);

      del_timer_sync(&Controller->MonitoringTimer);
      if (Controller->FirmwareType == DAC960_V1_Controller)
	{
	  DAC960_Notice("Flushing Cache...", Controller);
	  DAC960_V1_ExecuteType3(Controller, DAC960_V1_Flush, 0);
	  DAC960_Notice("done\n", Controller);

	  if (Controller->HardwareType == DAC960_PD_Controller)
	      release_region(Controller->IO_Address, 0x80);
	}
      else
	{
	  DAC960_Notice("Flushing Cache...", Controller);
	  DAC960_V2_DeviceOperation(Controller, DAC960_V2_PauseDevice,
				    DAC960_V2_RAID_Controller);
	  DAC960_Notice("done\n", Controller);
	}
    }
  DAC960_UnregisterBlockDevice(Controller);
  DAC960_DestroyAuxiliaryStructures(Controller);
  DAC960_DestroyProcEntries(Controller);
  DAC960_DetectCleanup(Controller);
}


/*
  DAC960_Probe verifies controller's existence and
  initializes the DAC960 Driver for that controller.
*/

static int 
DAC960_Probe(struct pci_dev *dev, const struct pci_device_id *entry)
{
  int disk;
  DAC960_Controller_T *Controller;

  if (DAC960_ControllerCount == DAC960_MaxControllers)
  {
	DAC960_Error("More than %d DAC960 Controllers detected - "
                       "ignoring from Controller at\n",
                       NULL, DAC960_MaxControllers);
	return -ENODEV;
  }

  Controller = DAC960_DetectController(dev, entry);
  if (!Controller)
	return -ENODEV;

  if (!DAC960_InitializeController(Controller)) {
  	DAC960_FinalizeController(Controller);
	return -ENODEV;
  }

  for (disk = 0; disk < DAC960_MaxLogicalDrives; disk++) {
        set_capacity(Controller->disks[disk], disk_size(Controller, disk));
        add_disk(Controller->disks[disk]);
  }
  DAC960_CreateProcEntries(Controller);
  return 0;
}


/*
  DAC960_Finalize finalizes the DAC960 Driver.
*/

static void DAC960_Remove(struct pci_dev *PCI_Device)
{
  int Controller_Number = (long)pci_get_drvdata(PCI_Device);
  DAC960_Controller_T *Controller = DAC960_Controllers[Controller_Number];
  if (Controller != NULL)
      DAC960_FinalizeController(Controller);
}


/*
  DAC960_V1_QueueReadWriteCommand prepares and queues a Read/Write Command for
  DAC960 V1 Firmware Controllers.
*/

static void DAC960_V1_QueueReadWriteCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_ScatterGatherSegment_T *ScatterGatherList =
					Command->V1.ScatterGatherList;
  struct scatterlist *ScatterList = Command->V1.ScatterList;

  DAC960_V1_ClearCommand(Command);

  if (Command->SegmentCount == 1)
    {
      if (Command->DmaDirection == PCI_DMA_FROMDEVICE)
	CommandMailbox->Type5.CommandOpcode = DAC960_V1_Read;
      else 
        CommandMailbox->Type5.CommandOpcode = DAC960_V1_Write;

      CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
      CommandMailbox->Type5.LD.LogicalDriveNumber = Command->LogicalDriveNumber;
      CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
      CommandMailbox->Type5.BusAddress =
			(DAC960_BusAddress32_T)sg_dma_address(ScatterList);	
    }
  else
    {
      int i;

      if (Command->DmaDirection == PCI_DMA_FROMDEVICE)
	CommandMailbox->Type5.CommandOpcode = DAC960_V1_ReadWithScatterGather;
      else
	CommandMailbox->Type5.CommandOpcode = DAC960_V1_WriteWithScatterGather;

      CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
      CommandMailbox->Type5.LD.LogicalDriveNumber = Command->LogicalDriveNumber;
      CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
      CommandMailbox->Type5.BusAddress = Command->V1.ScatterGatherListDMA;

      CommandMailbox->Type5.ScatterGatherCount = Command->SegmentCount;

      for (i = 0; i < Command->SegmentCount; i++, ScatterList++, ScatterGatherList++) {
		ScatterGatherList->SegmentDataPointer =
			(DAC960_BusAddress32_T)sg_dma_address(ScatterList);
		ScatterGatherList->SegmentByteCount =
			(DAC960_ByteCount32_T)sg_dma_len(ScatterList);
      }
    }
  DAC960_QueueCommand(Command);
}


/*
  DAC960_V2_QueueReadWriteCommand prepares and queues a Read/Write Command for
  DAC960 V2 Firmware Controllers.
*/

static void DAC960_V2_QueueReadWriteCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  struct scatterlist *ScatterList = Command->V2.ScatterList;

  DAC960_V2_ClearCommand(Command);

  CommandMailbox->SCSI_10.CommandOpcode = DAC960_V2_SCSI_10;
  CommandMailbox->SCSI_10.CommandControlBits.DataTransferControllerToHost =
    (Command->DmaDirection == PCI_DMA_FROMDEVICE);
  CommandMailbox->SCSI_10.DataTransferSize =
    Command->BlockCount << DAC960_BlockSizeBits;
  CommandMailbox->SCSI_10.RequestSenseBusAddress = Command->V2.RequestSenseDMA;
  CommandMailbox->SCSI_10.PhysicalDevice =
    Controller->V2.LogicalDriveToVirtualDevice[Command->LogicalDriveNumber];
  CommandMailbox->SCSI_10.RequestSenseSize = sizeof(DAC960_SCSI_RequestSense_T);
  CommandMailbox->SCSI_10.CDBLength = 10;
  CommandMailbox->SCSI_10.SCSI_CDB[0] =
    (Command->DmaDirection == PCI_DMA_FROMDEVICE ? 0x28 : 0x2A);
  CommandMailbox->SCSI_10.SCSI_CDB[2] = Command->BlockNumber >> 24;
  CommandMailbox->SCSI_10.SCSI_CDB[3] = Command->BlockNumber >> 16;
  CommandMailbox->SCSI_10.SCSI_CDB[4] = Command->BlockNumber >> 8;
  CommandMailbox->SCSI_10.SCSI_CDB[5] = Command->BlockNumber;
  CommandMailbox->SCSI_10.SCSI_CDB[7] = Command->BlockCount >> 8;
  CommandMailbox->SCSI_10.SCSI_CDB[8] = Command->BlockCount;

  if (Command->SegmentCount == 1)
    {
      CommandMailbox->SCSI_10.DataTransferMemoryAddress
			     .ScatterGatherSegments[0]
			     .SegmentDataPointer =
	(DAC960_BusAddress64_T)sg_dma_address(ScatterList);
      CommandMailbox->SCSI_10.DataTransferMemoryAddress
			     .ScatterGatherSegments[0]
			     .SegmentByteCount =
	CommandMailbox->SCSI_10.DataTransferSize;
    }
  else
    {
      DAC960_V2_ScatterGatherSegment_T *ScatterGatherList;
      int i;

      if (Command->SegmentCount > 2)
	{
          ScatterGatherList = Command->V2.ScatterGatherList;
	  CommandMailbox->SCSI_10.CommandControlBits
			 .AdditionalScatterGatherListMemory = true;
	  CommandMailbox->SCSI_10.DataTransferMemoryAddress
		.ExtendedScatterGather.ScatterGatherList0Length = Command->SegmentCount;
	  CommandMailbox->SCSI_10.DataTransferMemoryAddress
			 .ExtendedScatterGather.ScatterGatherList0Address =
	    Command->V2.ScatterGatherListDMA;
	}
      else
	ScatterGatherList = CommandMailbox->SCSI_10.DataTransferMemoryAddress
				 .ScatterGatherSegments;

      for (i = 0; i < Command->SegmentCount; i++, ScatterList++, ScatterGatherList++) {
		ScatterGatherList->SegmentDataPointer =
			(DAC960_BusAddress64_T)sg_dma_address(ScatterList);
		ScatterGatherList->SegmentByteCount =
			(DAC960_ByteCount64_T)sg_dma_len(ScatterList);
      }
    }
  DAC960_QueueCommand(Command);
}


static int DAC960_process_queue(DAC960_Controller_T *Controller, struct request_queue *req_q)
{
	struct request *Request;
	DAC960_Command_T *Command;

   while(1) {
	Request = blk_peek_request(req_q);
	if (!Request)
		return 1;

	Command = DAC960_AllocateCommand(Controller);
	if (Command == NULL)
		return 0;

	if (rq_data_dir(Request) == READ) {
		Command->DmaDirection = PCI_DMA_FROMDEVICE;
		Command->CommandType = DAC960_ReadCommand;
	} else {
		Command->DmaDirection = PCI_DMA_TODEVICE;
		Command->CommandType = DAC960_WriteCommand;
	}
	Command->Completion = Request->end_io_data;
	Command->LogicalDriveNumber = (long)Request->rq_disk->private_data;
	Command->BlockNumber = blk_rq_pos(Request);
	Command->BlockCount = blk_rq_sectors(Request);
	Command->Request = Request;
	blk_start_request(Request);
	Command->SegmentCount = blk_rq_map_sg(req_q,
		  Command->Request, Command->cmd_sglist);
	/* pci_map_sg MAY change the value of SegCount */
	Command->SegmentCount = pci_map_sg(Controller->PCIDevice, Command->cmd_sglist,
		 Command->SegmentCount, Command->DmaDirection);

	DAC960_QueueReadWriteCommand(Command);
  }
}

/*
  DAC960_ProcessRequest attempts to remove one I/O Request from Controller's
  I/O Request Queue and queues it to the Controller.  WaitForCommand is true if
  this function should wait for a Command to become available if necessary.
  This function returns true if an I/O Request was queued and false otherwise.
*/
static void DAC960_ProcessRequest(DAC960_Controller_T *controller)
{
	int i;

	if (!controller->ControllerInitialized)
		return;

	/* Do this better later! */
	for (i = controller->req_q_index; i < DAC960_MaxLogicalDrives; i++) {
		struct request_queue *req_q = controller->RequestQueue[i];

		if (req_q == NULL)
			continue;

		if (!DAC960_process_queue(controller, req_q)) {
			controller->req_q_index = i;
			return;
		}
	}

	if (controller->req_q_index == 0)
		return;

	for (i = 0; i < controller->req_q_index; i++) {
		struct request_queue *req_q = controller->RequestQueue[i];

		if (req_q == NULL)
			continue;

		if (!DAC960_process_queue(controller, req_q)) {
			controller->req_q_index = i;
			return;
		}
	}
}


/*
  DAC960_queue_partial_rw extracts one bio from the request already
  associated with argument command, and construct a new command block to retry I/O
  only on that bio.  Queue that command to the controller.

  This function re-uses a previously-allocated Command,
  	there is no failure mode from trying to allocate a command.
*/

static void DAC960_queue_partial_rw(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  struct request *Request = Command->Request;
  struct request_queue *req_q = Controller->RequestQueue[Command->LogicalDriveNumber];

  if (Command->DmaDirection == PCI_DMA_FROMDEVICE)
    Command->CommandType = DAC960_ReadRetryCommand;
  else
    Command->CommandType = DAC960_WriteRetryCommand;

  /*
   * We could be more efficient with these mapping requests
   * and map only the portions that we need.  But since this
   * code should almost never be called, just go with a
   * simple coding.
   */
  (void)blk_rq_map_sg(req_q, Command->Request, Command->cmd_sglist);

  (void)pci_map_sg(Controller->PCIDevice, Command->cmd_sglist, 1, Command->DmaDirection);
  /*
   * Resubmitting the request sector at a time is really tedious.
   * But, this should almost never happen.  So, we're willing to pay
   * this price so that in the end, as much of the transfer is completed
   * successfully as possible.
   */
  Command->SegmentCount = 1;
  Command->BlockNumber = blk_rq_pos(Request);
  Command->BlockCount = 1;
  DAC960_QueueReadWriteCommand(Command);
  return;
}

/*
  DAC960_RequestFunction is the I/O Request Function for DAC960 Controllers.
*/

static void DAC960_RequestFunction(struct request_queue *RequestQueue)
{
	DAC960_ProcessRequest(RequestQueue->queuedata);
}

/*
  DAC960_ProcessCompletedBuffer performs completion processing for an
  individual Buffer.
*/

static inline bool DAC960_ProcessCompletedRequest(DAC960_Command_T *Command,
						 bool SuccessfulIO)
{
	struct request *Request = Command->Request;
	blk_status_t Error = SuccessfulIO ? BLK_STS_OK : BLK_STS_IOERR;

	pci_unmap_sg(Command->Controller->PCIDevice, Command->cmd_sglist,
		Command->SegmentCount, Command->DmaDirection);

	 if (!__blk_end_request(Request, Error, Command->BlockCount << 9)) {
		if (Command->Completion) {
			complete(Command->Completion);
			Command->Completion = NULL;
		}
		return true;
	}
	return false;
}

/*
  DAC960_V1_ReadWriteError prints an appropriate error message for Command
  when an error occurs on a Read or Write operation.
*/

static void DAC960_V1_ReadWriteError(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  unsigned char *CommandName = "UNKNOWN";
  switch (Command->CommandType)
    {
    case DAC960_ReadCommand:
    case DAC960_ReadRetryCommand:
      CommandName = "READ";
      break;
    case DAC960_WriteCommand:
    case DAC960_WriteRetryCommand:
      CommandName = "WRITE";
      break;
    case DAC960_MonitoringCommand:
    case DAC960_ImmediateCommand:
    case DAC960_QueuedCommand:
      break;
    }
  switch (Command->V1.CommandStatus)
    {
    case DAC960_V1_IrrecoverableDataError:
      DAC960_Error("Irrecoverable Data Error on %s:\n",
		   Controller, CommandName);
      break;
    case DAC960_V1_LogicalDriveNonexistentOrOffline:
      DAC960_Error("Logical Drive Nonexistent or Offline on %s:\n",
		   Controller, CommandName);
      break;
    case DAC960_V1_AccessBeyondEndOfLogicalDrive:
      DAC960_Error("Attempt to Access Beyond End of Logical Drive "
		   "on %s:\n", Controller, CommandName);
      break;
    case DAC960_V1_BadDataEncountered:
      DAC960_Error("Bad Data Encountered on %s:\n", Controller, CommandName);
      break;
    default:
      DAC960_Error("Unexpected Error Status %04X on %s:\n",
		   Controller, Command->V1.CommandStatus, CommandName);
      break;
    }
  DAC960_Error("  /dev/rd/c%dd%d:   absolute blocks %u..%u\n",
	       Controller, Controller->ControllerNumber,
	       Command->LogicalDriveNumber, Command->BlockNumber,
	       Command->BlockNumber + Command->BlockCount - 1);
}


/*
  DAC960_V1_ProcessCompletedCommand performs completion processing for Command
  for DAC960 V1 Firmware Controllers.
*/

static void DAC960_V1_ProcessCompletedCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_CommandType_T CommandType = Command->CommandType;
  DAC960_V1_CommandOpcode_T CommandOpcode =
    Command->V1.CommandMailbox.Common.CommandOpcode;
  DAC960_V1_CommandStatus_T CommandStatus = Command->V1.CommandStatus;

  if (CommandType == DAC960_ReadCommand ||
      CommandType == DAC960_WriteCommand)
    {

#ifdef FORCE_RETRY_DEBUG
      CommandStatus = DAC960_V1_IrrecoverableDataError;
#endif

      if (CommandStatus == DAC960_V1_NormalCompletion) {

		if (!DAC960_ProcessCompletedRequest(Command, true))
			BUG();

      } else if (CommandStatus == DAC960_V1_IrrecoverableDataError ||
		CommandStatus == DAC960_V1_BadDataEncountered)
	{
	  /*
	   * break the command down into pieces and resubmit each
	   * piece, hoping that some of them will succeed.
	   */
	   DAC960_queue_partial_rw(Command);
	   return;
	}
      else
	{
	  if (CommandStatus != DAC960_V1_LogicalDriveNonexistentOrOffline)
	    DAC960_V1_ReadWriteError(Command);

	 if (!DAC960_ProcessCompletedRequest(Command, false))
		BUG();
	}
    }
  else if (CommandType == DAC960_ReadRetryCommand ||
	   CommandType == DAC960_WriteRetryCommand)
    {
      bool normal_completion;
#ifdef FORCE_RETRY_FAILURE_DEBUG
      static int retry_count = 1;
#endif
      /*
        Perform completion processing for the portion that was
        retried, and submit the next portion, if any.
      */
      normal_completion = true;
      if (CommandStatus != DAC960_V1_NormalCompletion) {
        normal_completion = false;
        if (CommandStatus != DAC960_V1_LogicalDriveNonexistentOrOffline)
            DAC960_V1_ReadWriteError(Command);
      }

#ifdef FORCE_RETRY_FAILURE_DEBUG
      if (!(++retry_count % 10000)) {
	      printk("V1 error retry failure test\n");
	      normal_completion = false;
              DAC960_V1_ReadWriteError(Command);
      }
#endif

      if (!DAC960_ProcessCompletedRequest(Command, normal_completion)) {
        DAC960_queue_partial_rw(Command);
        return;
      }
    }

  else if (CommandType == DAC960_MonitoringCommand)
    {
      if (Controller->ShutdownMonitoringTimer)
	      return;
      if (CommandOpcode == DAC960_V1_Enquiry)
	{
	  DAC960_V1_Enquiry_T *OldEnquiry = &Controller->V1.Enquiry;
	  DAC960_V1_Enquiry_T *NewEnquiry = Controller->V1.NewEnquiry;
	  unsigned int OldCriticalLogicalDriveCount =
	    OldEnquiry->CriticalLogicalDriveCount;
	  unsigned int NewCriticalLogicalDriveCount =
	    NewEnquiry->CriticalLogicalDriveCount;
	  if (NewEnquiry->NumberOfLogicalDrives > Controller->LogicalDriveCount)
	    {
	      int LogicalDriveNumber = Controller->LogicalDriveCount - 1;
	      while (++LogicalDriveNumber < NewEnquiry->NumberOfLogicalDrives)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"Now Exists\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber);
	      Controller->LogicalDriveCount = NewEnquiry->NumberOfLogicalDrives;
	      DAC960_ComputeGenericDiskInfo(Controller);
	    }
	  if (NewEnquiry->NumberOfLogicalDrives < Controller->LogicalDriveCount)
	    {
	      int LogicalDriveNumber = NewEnquiry->NumberOfLogicalDrives - 1;
	      while (++LogicalDriveNumber < Controller->LogicalDriveCount)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"No Longer Exists\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber);
	      Controller->LogicalDriveCount = NewEnquiry->NumberOfLogicalDrives;
	      DAC960_ComputeGenericDiskInfo(Controller);
	    }
	  if (NewEnquiry->StatusFlags.DeferredWriteError !=
	      OldEnquiry->StatusFlags.DeferredWriteError)
	    DAC960_Critical("Deferred Write Error Flag is now %s\n", Controller,
			    (NewEnquiry->StatusFlags.DeferredWriteError
			     ? "TRUE" : "FALSE"));
	  if ((NewCriticalLogicalDriveCount > 0 ||
	       NewCriticalLogicalDriveCount != OldCriticalLogicalDriveCount) ||
	      (NewEnquiry->OfflineLogicalDriveCount > 0 ||
	       NewEnquiry->OfflineLogicalDriveCount !=
	       OldEnquiry->OfflineLogicalDriveCount) ||
	      (NewEnquiry->DeadDriveCount > 0 ||
	       NewEnquiry->DeadDriveCount !=
	       OldEnquiry->DeadDriveCount) ||
	      (NewEnquiry->EventLogSequenceNumber !=
	       OldEnquiry->EventLogSequenceNumber) ||
	      Controller->MonitoringTimerCount == 0 ||
	      time_after_eq(jiffies, Controller->SecondaryMonitoringTime
	       + DAC960_SecondaryMonitoringInterval))
	    {
	      Controller->V1.NeedLogicalDriveInformation = true;
	      Controller->V1.NewEventLogSequenceNumber =
		NewEnquiry->EventLogSequenceNumber;
	      Controller->V1.NeedErrorTableInformation = true;
	      Controller->V1.NeedDeviceStateInformation = true;
	      Controller->V1.StartDeviceStateScan = true;
	      Controller->V1.NeedBackgroundInitializationStatus =
		Controller->V1.BackgroundInitializationStatusSupported;
	      Controller->SecondaryMonitoringTime = jiffies;
	    }
	  if (NewEnquiry->RebuildFlag == DAC960_V1_StandbyRebuildInProgress ||
	      NewEnquiry->RebuildFlag
	      == DAC960_V1_BackgroundRebuildInProgress ||
	      OldEnquiry->RebuildFlag == DAC960_V1_StandbyRebuildInProgress ||
	      OldEnquiry->RebuildFlag == DAC960_V1_BackgroundRebuildInProgress)
	    {
	      Controller->V1.NeedRebuildProgress = true;
	      Controller->V1.RebuildProgressFirst =
		(NewEnquiry->CriticalLogicalDriveCount <
		 OldEnquiry->CriticalLogicalDriveCount);
	    }
	  if (OldEnquiry->RebuildFlag == DAC960_V1_BackgroundCheckInProgress)
	    switch (NewEnquiry->RebuildFlag)
	      {
	      case DAC960_V1_NoStandbyRebuildOrCheckInProgress:
		DAC960_Progress("Consistency Check Completed Successfully\n",
				Controller);
		break;
	      case DAC960_V1_StandbyRebuildInProgress:
	      case DAC960_V1_BackgroundRebuildInProgress:
		break;
	      case DAC960_V1_BackgroundCheckInProgress:
		Controller->V1.NeedConsistencyCheckProgress = true;
		break;
	      case DAC960_V1_StandbyRebuildCompletedWithError:
		DAC960_Progress("Consistency Check Completed with Error\n",
				Controller);
		break;
	      case DAC960_V1_BackgroundRebuildOrCheckFailed_DriveFailed:
		DAC960_Progress("Consistency Check Failed - "
				"Physical Device Failed\n", Controller);
		break;
	      case DAC960_V1_BackgroundRebuildOrCheckFailed_LogicalDriveFailed:
		DAC960_Progress("Consistency Check Failed - "
				"Logical Drive Failed\n", Controller);
		break;
	      case DAC960_V1_BackgroundRebuildOrCheckFailed_OtherCauses:
		DAC960_Progress("Consistency Check Failed - Other Causes\n",
				Controller);
		break;
	      case DAC960_V1_BackgroundRebuildOrCheckSuccessfullyTerminated:
		DAC960_Progress("Consistency Check Successfully Terminated\n",
				Controller);
		break;
	      }
	  else if (NewEnquiry->RebuildFlag
		   == DAC960_V1_BackgroundCheckInProgress)
	    Controller->V1.NeedConsistencyCheckProgress = true;
	  Controller->MonitoringAlertMode =
	    (NewEnquiry->CriticalLogicalDriveCount > 0 ||
	     NewEnquiry->OfflineLogicalDriveCount > 0 ||
	     NewEnquiry->DeadDriveCount > 0);
	  if (NewEnquiry->RebuildFlag > DAC960_V1_BackgroundCheckInProgress)
	    {
	      Controller->V1.PendingRebuildFlag = NewEnquiry->RebuildFlag;
	      Controller->V1.RebuildFlagPending = true;
	    }
	  memcpy(&Controller->V1.Enquiry, &Controller->V1.NewEnquiry,
		 sizeof(DAC960_V1_Enquiry_T));
	}
      else if (CommandOpcode == DAC960_V1_PerformEventLogOperation)
	{
	  static char
	    *DAC960_EventMessages[] =
	       { "killed because write recovery failed",
		 "killed because of SCSI bus reset failure",
		 "killed because of double check condition",
		 "killed because it was removed",
		 "killed because of gross error on SCSI chip",
		 "killed because of bad tag returned from drive",
		 "killed because of timeout on SCSI command",
		 "killed because of reset SCSI command issued from system",
		 "killed because busy or parity error count exceeded limit",
		 "killed because of 'kill drive' command from system",
		 "killed because of selection timeout",
		 "killed due to SCSI phase sequence error",
		 "killed due to unknown status" };
	  DAC960_V1_EventLogEntry_T *EventLogEntry =
	    	Controller->V1.EventLogEntry;
	  if (EventLogEntry->SequenceNumber ==
	      Controller->V1.OldEventLogSequenceNumber)
	    {
	      unsigned char SenseKey = EventLogEntry->SenseKey;
	      unsigned char AdditionalSenseCode =
		EventLogEntry->AdditionalSenseCode;
	      unsigned char AdditionalSenseCodeQualifier =
		EventLogEntry->AdditionalSenseCodeQualifier;
	      if (SenseKey == DAC960_SenseKey_VendorSpecific &&
		  AdditionalSenseCode == 0x80 &&
		  AdditionalSenseCodeQualifier <
		  ARRAY_SIZE(DAC960_EventMessages))
		DAC960_Critical("Physical Device %d:%d %s\n", Controller,
				EventLogEntry->Channel,
				EventLogEntry->TargetID,
				DAC960_EventMessages[
				  AdditionalSenseCodeQualifier]);
	      else if (SenseKey == DAC960_SenseKey_UnitAttention &&
		       AdditionalSenseCode == 0x29)
		{
		  if (Controller->MonitoringTimerCount > 0)
		    Controller->V1.DeviceResetCount[EventLogEntry->Channel]
						   [EventLogEntry->TargetID]++;
		}
	      else if (!(SenseKey == DAC960_SenseKey_NoSense ||
			 (SenseKey == DAC960_SenseKey_NotReady &&
			  AdditionalSenseCode == 0x04 &&
			  (AdditionalSenseCodeQualifier == 0x01 ||
			   AdditionalSenseCodeQualifier == 0x02))))
		{
		  DAC960_Critical("Physical Device %d:%d Error Log: "
				  "Sense Key = %X, ASC = %02X, ASCQ = %02X\n",
				  Controller,
				  EventLogEntry->Channel,
				  EventLogEntry->TargetID,
				  SenseKey,
				  AdditionalSenseCode,
				  AdditionalSenseCodeQualifier);
		  DAC960_Critical("Physical Device %d:%d Error Log: "
				  "Information = %02X%02X%02X%02X "
				  "%02X%02X%02X%02X\n",
				  Controller,
				  EventLogEntry->Channel,
				  EventLogEntry->TargetID,
				  EventLogEntry->Information[0],
				  EventLogEntry->Information[1],
				  EventLogEntry->Information[2],
				  EventLogEntry->Information[3],
				  EventLogEntry->CommandSpecificInformation[0],
				  EventLogEntry->CommandSpecificInformation[1],
				  EventLogEntry->CommandSpecificInformation[2],
				  EventLogEntry->CommandSpecificInformation[3]);
		}
	    }
	  Controller->V1.OldEventLogSequenceNumber++;
	}
      else if (CommandOpcode == DAC960_V1_GetErrorTable)
	{
	  DAC960_V1_ErrorTable_T *OldErrorTable = &Controller->V1.ErrorTable;
	  DAC960_V1_ErrorTable_T *NewErrorTable = Controller->V1.NewErrorTable;
	  int Channel, TargetID;
	  for (Channel = 0; Channel < Controller->Channels; Channel++)
	    for (TargetID = 0; TargetID < Controller->Targets; TargetID++)
	      {
		DAC960_V1_ErrorTableEntry_T *NewErrorEntry =
		  &NewErrorTable->ErrorTableEntries[Channel][TargetID];
		DAC960_V1_ErrorTableEntry_T *OldErrorEntry =
		  &OldErrorTable->ErrorTableEntries[Channel][TargetID];
		if ((NewErrorEntry->ParityErrorCount !=
		     OldErrorEntry->ParityErrorCount) ||
		    (NewErrorEntry->SoftErrorCount !=
		     OldErrorEntry->SoftErrorCount) ||
		    (NewErrorEntry->HardErrorCount !=
		     OldErrorEntry->HardErrorCount) ||
		    (NewErrorEntry->MiscErrorCount !=
		     OldErrorEntry->MiscErrorCount))
		  DAC960_Critical("Physical Device %d:%d Errors: "
				  "Parity = %d, Soft = %d, "
				  "Hard = %d, Misc = %d\n",
				  Controller, Channel, TargetID,
				  NewErrorEntry->ParityErrorCount,
				  NewErrorEntry->SoftErrorCount,
				  NewErrorEntry->HardErrorCount,
				  NewErrorEntry->MiscErrorCount);
	      }
	  memcpy(&Controller->V1.ErrorTable, Controller->V1.NewErrorTable,
		 sizeof(DAC960_V1_ErrorTable_T));
	}
      else if (CommandOpcode == DAC960_V1_GetDeviceState)
	{
	  DAC960_V1_DeviceState_T *OldDeviceState =
	    &Controller->V1.DeviceState[Controller->V1.DeviceStateChannel]
				       [Controller->V1.DeviceStateTargetID];
	  DAC960_V1_DeviceState_T *NewDeviceState =
	    Controller->V1.NewDeviceState;
	  if (NewDeviceState->DeviceState != OldDeviceState->DeviceState)
	    DAC960_Critical("Physical Device %d:%d is now %s\n", Controller,
			    Controller->V1.DeviceStateChannel,
			    Controller->V1.DeviceStateTargetID,
			    (NewDeviceState->DeviceState
			     == DAC960_V1_Device_Dead
			     ? "DEAD"
			     : NewDeviceState->DeviceState
			       == DAC960_V1_Device_WriteOnly
			       ? "WRITE-ONLY"
			       : NewDeviceState->DeviceState
				 == DAC960_V1_Device_Online
				 ? "ONLINE" : "STANDBY"));
	  if (OldDeviceState->DeviceState == DAC960_V1_Device_Dead &&
	      NewDeviceState->DeviceState != DAC960_V1_Device_Dead)
	    {
	      Controller->V1.NeedDeviceInquiryInformation = true;
	      Controller->V1.NeedDeviceSerialNumberInformation = true;
	      Controller->V1.DeviceResetCount
			     [Controller->V1.DeviceStateChannel]
			     [Controller->V1.DeviceStateTargetID] = 0;
	    }
	  memcpy(OldDeviceState, NewDeviceState,
		 sizeof(DAC960_V1_DeviceState_T));
	}
      else if (CommandOpcode == DAC960_V1_GetLogicalDriveInformation)
	{
	  int LogicalDriveNumber;
	  for (LogicalDriveNumber = 0;
	       LogicalDriveNumber < Controller->LogicalDriveCount;
	       LogicalDriveNumber++)
	    {
	      DAC960_V1_LogicalDriveInformation_T *OldLogicalDriveInformation =
		&Controller->V1.LogicalDriveInformation[LogicalDriveNumber];
	      DAC960_V1_LogicalDriveInformation_T *NewLogicalDriveInformation =
		&(*Controller->V1.NewLogicalDriveInformation)[LogicalDriveNumber];
	      if (NewLogicalDriveInformation->LogicalDriveState !=
		  OldLogicalDriveInformation->LogicalDriveState)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber,
				(NewLogicalDriveInformation->LogicalDriveState
				 == DAC960_V1_LogicalDrive_Online
				 ? "ONLINE"
				 : NewLogicalDriveInformation->LogicalDriveState
				   == DAC960_V1_LogicalDrive_Critical
				   ? "CRITICAL" : "OFFLINE"));
	      if (NewLogicalDriveInformation->WriteBack !=
		  OldLogicalDriveInformation->WriteBack)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber,
				(NewLogicalDriveInformation->WriteBack
				 ? "WRITE BACK" : "WRITE THRU"));
	    }
	  memcpy(&Controller->V1.LogicalDriveInformation,
		 Controller->V1.NewLogicalDriveInformation,
		 sizeof(DAC960_V1_LogicalDriveInformationArray_T));
	}
      else if (CommandOpcode == DAC960_V1_GetRebuildProgress)
	{
	  unsigned int LogicalDriveNumber =
	    Controller->V1.RebuildProgress->LogicalDriveNumber;
	  unsigned int LogicalDriveSize =
	    Controller->V1.RebuildProgress->LogicalDriveSize;
	  unsigned int BlocksCompleted =
	    LogicalDriveSize - Controller->V1.RebuildProgress->RemainingBlocks;
	  if (CommandStatus == DAC960_V1_NoRebuildOrCheckInProgress &&
	      Controller->V1.LastRebuildStatus == DAC960_V1_NormalCompletion)
	    CommandStatus = DAC960_V1_RebuildSuccessful;
	  switch (CommandStatus)
	    {
	    case DAC960_V1_NormalCompletion:
	      Controller->EphemeralProgressMessage = true;
	      DAC960_Progress("Rebuild in Progress: "
			      "Logical Drive %d (/dev/rd/c%dd%d) "
			      "%d%% completed\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber,
			      (100 * (BlocksCompleted >> 7))
			      / (LogicalDriveSize >> 7));
	      Controller->EphemeralProgressMessage = false;
	      break;
	    case DAC960_V1_RebuildFailed_LogicalDriveFailure:
	      DAC960_Progress("Rebuild Failed due to "
			      "Logical Drive Failure\n", Controller);
	      break;
	    case DAC960_V1_RebuildFailed_BadBlocksOnOther:
	      DAC960_Progress("Rebuild Failed due to "
			      "Bad Blocks on Other Drives\n", Controller);
	      break;
	    case DAC960_V1_RebuildFailed_NewDriveFailed:
	      DAC960_Progress("Rebuild Failed due to "
			      "Failure of Drive Being Rebuilt\n", Controller);
	      break;
	    case DAC960_V1_NoRebuildOrCheckInProgress:
	      break;
	    case DAC960_V1_RebuildSuccessful:
	      DAC960_Progress("Rebuild Completed Successfully\n", Controller);
	      break;
	    case DAC960_V1_RebuildSuccessfullyTerminated:
	      DAC960_Progress("Rebuild Successfully Terminated\n", Controller);
	      break;
	    }
	  Controller->V1.LastRebuildStatus = CommandStatus;
	  if (CommandType != DAC960_MonitoringCommand &&
	      Controller->V1.RebuildStatusPending)
	    {
	      Command->V1.CommandStatus = Controller->V1.PendingRebuildStatus;
	      Controller->V1.RebuildStatusPending = false;
	    }
	  else if (CommandType == DAC960_MonitoringCommand &&
		   CommandStatus != DAC960_V1_NormalCompletion &&
		   CommandStatus != DAC960_V1_NoRebuildOrCheckInProgress)
	    {
	      Controller->V1.PendingRebuildStatus = CommandStatus;
	      Controller->V1.RebuildStatusPending = true;
	    }
	}
      else if (CommandOpcode == DAC960_V1_RebuildStat)
	{
	  unsigned int LogicalDriveNumber =
	    Controller->V1.RebuildProgress->LogicalDriveNumber;
	  unsigned int LogicalDriveSize =
	    Controller->V1.RebuildProgress->LogicalDriveSize;
	  unsigned int BlocksCompleted =
	    LogicalDriveSize - Controller->V1.RebuildProgress->RemainingBlocks;
	  if (CommandStatus == DAC960_V1_NormalCompletion)
	    {
	      Controller->EphemeralProgressMessage = true;
	      DAC960_Progress("Consistency Check in Progress: "
			      "Logical Drive %d (/dev/rd/c%dd%d) "
			      "%d%% completed\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber,
			      (100 * (BlocksCompleted >> 7))
			      / (LogicalDriveSize >> 7));
	      Controller->EphemeralProgressMessage = false;
	    }
	}
      else if (CommandOpcode == DAC960_V1_BackgroundInitializationControl)
	{
	  unsigned int LogicalDriveNumber =
	    Controller->V1.BackgroundInitializationStatus->LogicalDriveNumber;
	  unsigned int LogicalDriveSize =
	    Controller->V1.BackgroundInitializationStatus->LogicalDriveSize;
	  unsigned int BlocksCompleted =
	    Controller->V1.BackgroundInitializationStatus->BlocksCompleted;
	  switch (CommandStatus)
	    {
	    case DAC960_V1_NormalCompletion:
	      switch (Controller->V1.BackgroundInitializationStatus->Status)
		{
		case DAC960_V1_BackgroundInitializationInvalid:
		  break;
		case DAC960_V1_BackgroundInitializationStarted:
		  DAC960_Progress("Background Initialization Started\n",
				  Controller);
		  break;
		case DAC960_V1_BackgroundInitializationInProgress:
		  if (BlocksCompleted ==
		      Controller->V1.LastBackgroundInitializationStatus.
				BlocksCompleted &&
		      LogicalDriveNumber ==
		      Controller->V1.LastBackgroundInitializationStatus.
				LogicalDriveNumber)
		    break;
		  Controller->EphemeralProgressMessage = true;
		  DAC960_Progress("Background Initialization in Progress: "
				  "Logical Drive %d (/dev/rd/c%dd%d) "
				  "%d%% completed\n",
				  Controller, LogicalDriveNumber,
				  Controller->ControllerNumber,
				  LogicalDriveNumber,
				  (100 * (BlocksCompleted >> 7))
				  / (LogicalDriveSize >> 7));
		  Controller->EphemeralProgressMessage = false;
		  break;
		case DAC960_V1_BackgroundInitializationSuspended:
		  DAC960_Progress("Background Initialization Suspended\n",
				  Controller);
		  break;
		case DAC960_V1_BackgroundInitializationCancelled:
		  DAC960_Progress("Background Initialization Cancelled\n",
				  Controller);
		  break;
		}
	      memcpy(&Controller->V1.LastBackgroundInitializationStatus,
		     Controller->V1.BackgroundInitializationStatus,
		     sizeof(DAC960_V1_BackgroundInitializationStatus_T));
	      break;
	    case DAC960_V1_BackgroundInitSuccessful:
	      if (Controller->V1.BackgroundInitializationStatus->Status ==
		  DAC960_V1_BackgroundInitializationInProgress)
		DAC960_Progress("Background Initialization "
				"Completed Successfully\n", Controller);
	      Controller->V1.BackgroundInitializationStatus->Status =
		DAC960_V1_BackgroundInitializationInvalid;
	      break;
	    case DAC960_V1_BackgroundInitAborted:
	      if (Controller->V1.BackgroundInitializationStatus->Status ==
		  DAC960_V1_BackgroundInitializationInProgress)
		DAC960_Progress("Background Initialization Aborted\n",
				Controller);
	      Controller->V1.BackgroundInitializationStatus->Status =
		DAC960_V1_BackgroundInitializationInvalid;
	      break;
	    case DAC960_V1_NoBackgroundInitInProgress:
	      break;
	    }
	} 
      else if (CommandOpcode == DAC960_V1_DCDB)
	{
	   /*
	     This is a bit ugly.

	     The InquiryStandardData and 
	     the InquiryUntitSerialNumber information
	     retrieval operations BOTH use the DAC960_V1_DCDB
	     commands.  the test above can't distinguish between
	     these two cases.

	     Instead, we rely on the order of code later in this
             function to ensure that DeviceInquiryInformation commands
             are submitted before DeviceSerialNumber commands.
	   */
	   if (Controller->V1.NeedDeviceInquiryInformation)
	     {
	        DAC960_SCSI_Inquiry_T *InquiryStandardData =
			&Controller->V1.InquiryStandardData
				[Controller->V1.DeviceStateChannel]
				[Controller->V1.DeviceStateTargetID];
	        if (CommandStatus != DAC960_V1_NormalCompletion)
		   {
			memset(InquiryStandardData, 0,
				sizeof(DAC960_SCSI_Inquiry_T));
	      		InquiryStandardData->PeripheralDeviceType = 0x1F;
		    }
	         else
			memcpy(InquiryStandardData, 
				Controller->V1.NewInquiryStandardData,
				sizeof(DAC960_SCSI_Inquiry_T));
	         Controller->V1.NeedDeviceInquiryInformation = false;
              }
	   else if (Controller->V1.NeedDeviceSerialNumberInformation) 
              {
	        DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
		  &Controller->V1.InquiryUnitSerialNumber
				[Controller->V1.DeviceStateChannel]
				[Controller->V1.DeviceStateTargetID];
	         if (CommandStatus != DAC960_V1_NormalCompletion)
		   {
			memset(InquiryUnitSerialNumber, 0,
				sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
	      		InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
		    }
	          else
			memcpy(InquiryUnitSerialNumber, 
				Controller->V1.NewInquiryUnitSerialNumber,
				sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
	      Controller->V1.NeedDeviceSerialNumberInformation = false;
	     }
	}
      /*
        Begin submitting new monitoring commands.
       */
      if (Controller->V1.NewEventLogSequenceNumber
	  - Controller->V1.OldEventLogSequenceNumber > 0)
	{
	  Command->V1.CommandMailbox.Type3E.CommandOpcode =
	    DAC960_V1_PerformEventLogOperation;
	  Command->V1.CommandMailbox.Type3E.OperationType =
	    DAC960_V1_GetEventLogEntry;
	  Command->V1.CommandMailbox.Type3E.OperationQualifier = 1;
	  Command->V1.CommandMailbox.Type3E.SequenceNumber =
	    Controller->V1.OldEventLogSequenceNumber;
	  Command->V1.CommandMailbox.Type3E.BusAddress =
	    	Controller->V1.EventLogEntryDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V1.NeedErrorTableInformation)
	{
	  Controller->V1.NeedErrorTableInformation = false;
	  Command->V1.CommandMailbox.Type3.CommandOpcode =
	    DAC960_V1_GetErrorTable;
	  Command->V1.CommandMailbox.Type3.BusAddress =
	    	Controller->V1.NewErrorTableDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V1.NeedRebuildProgress &&
	  Controller->V1.RebuildProgressFirst)
	{
	  Controller->V1.NeedRebuildProgress = false;
	  Command->V1.CommandMailbox.Type3.CommandOpcode =
	    DAC960_V1_GetRebuildProgress;
	  Command->V1.CommandMailbox.Type3.BusAddress =
	    Controller->V1.RebuildProgressDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V1.NeedDeviceStateInformation)
	{
	  if (Controller->V1.NeedDeviceInquiryInformation)
	    {
	      DAC960_V1_DCDB_T *DCDB = Controller->V1.MonitoringDCDB;
	      dma_addr_t DCDB_DMA = Controller->V1.MonitoringDCDB_DMA;

	      dma_addr_t NewInquiryStandardDataDMA =
		Controller->V1.NewInquiryStandardDataDMA;

	      Command->V1.CommandMailbox.Type3.CommandOpcode = DAC960_V1_DCDB;
	      Command->V1.CommandMailbox.Type3.BusAddress = DCDB_DMA;
	      DCDB->Channel = Controller->V1.DeviceStateChannel;
	      DCDB->TargetID = Controller->V1.DeviceStateTargetID;
	      DCDB->Direction = DAC960_V1_DCDB_DataTransferDeviceToSystem;
	      DCDB->EarlyStatus = false;
	      DCDB->Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	      DCDB->NoAutomaticRequestSense = false;
	      DCDB->DisconnectPermitted = true;
	      DCDB->TransferLength = sizeof(DAC960_SCSI_Inquiry_T);
	      DCDB->BusAddress = NewInquiryStandardDataDMA;
	      DCDB->CDBLength = 6;
	      DCDB->TransferLengthHigh4 = 0;
	      DCDB->SenseLength = sizeof(DCDB->SenseData);
	      DCDB->CDB[0] = 0x12; /* INQUIRY */
	      DCDB->CDB[1] = 0; /* EVPD = 0 */
	      DCDB->CDB[2] = 0; /* Page Code */
	      DCDB->CDB[3] = 0; /* Reserved */
	      DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_T);
	      DCDB->CDB[5] = 0; /* Control */
	      DAC960_QueueCommand(Command);
	      return;
	    }
	  if (Controller->V1.NeedDeviceSerialNumberInformation)
	    {
	      DAC960_V1_DCDB_T *DCDB = Controller->V1.MonitoringDCDB;
	      dma_addr_t DCDB_DMA = Controller->V1.MonitoringDCDB_DMA;
	      dma_addr_t NewInquiryUnitSerialNumberDMA = 
			Controller->V1.NewInquiryUnitSerialNumberDMA;

	      Command->V1.CommandMailbox.Type3.CommandOpcode = DAC960_V1_DCDB;
	      Command->V1.CommandMailbox.Type3.BusAddress = DCDB_DMA;
	      DCDB->Channel = Controller->V1.DeviceStateChannel;
	      DCDB->TargetID = Controller->V1.DeviceStateTargetID;
	      DCDB->Direction = DAC960_V1_DCDB_DataTransferDeviceToSystem;
	      DCDB->EarlyStatus = false;
	      DCDB->Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	      DCDB->NoAutomaticRequestSense = false;
	      DCDB->DisconnectPermitted = true;
	      DCDB->TransferLength =
		sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	      DCDB->BusAddress = NewInquiryUnitSerialNumberDMA;
	      DCDB->CDBLength = 6;
	      DCDB->TransferLengthHigh4 = 0;
	      DCDB->SenseLength = sizeof(DCDB->SenseData);
	      DCDB->CDB[0] = 0x12; /* INQUIRY */
	      DCDB->CDB[1] = 1; /* EVPD = 1 */
	      DCDB->CDB[2] = 0x80; /* Page Code */
	      DCDB->CDB[3] = 0; /* Reserved */
	      DCDB->CDB[4] = sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T);
	      DCDB->CDB[5] = 0; /* Control */
	      DAC960_QueueCommand(Command);
	      return;
	    }
	  if (Controller->V1.StartDeviceStateScan)
	    {
	      Controller->V1.DeviceStateChannel = 0;
	      Controller->V1.DeviceStateTargetID = 0;
	      Controller->V1.StartDeviceStateScan = false;
	    }
	  else if (++Controller->V1.DeviceStateTargetID == Controller->Targets)
	    {
	      Controller->V1.DeviceStateChannel++;
	      Controller->V1.DeviceStateTargetID = 0;
	    }
	  if (Controller->V1.DeviceStateChannel < Controller->Channels)
	    {
	      Controller->V1.NewDeviceState->DeviceState =
		DAC960_V1_Device_Dead;
	      Command->V1.CommandMailbox.Type3D.CommandOpcode =
		DAC960_V1_GetDeviceState;
	      Command->V1.CommandMailbox.Type3D.Channel =
		Controller->V1.DeviceStateChannel;
	      Command->V1.CommandMailbox.Type3D.TargetID =
		Controller->V1.DeviceStateTargetID;
	      Command->V1.CommandMailbox.Type3D.BusAddress =
		Controller->V1.NewDeviceStateDMA;
	      DAC960_QueueCommand(Command);
	      return;
	    }
	  Controller->V1.NeedDeviceStateInformation = false;
	}
      if (Controller->V1.NeedLogicalDriveInformation)
	{
	  Controller->V1.NeedLogicalDriveInformation = false;
	  Command->V1.CommandMailbox.Type3.CommandOpcode =
	    DAC960_V1_GetLogicalDriveInformation;
	  Command->V1.CommandMailbox.Type3.BusAddress =
	    Controller->V1.NewLogicalDriveInformationDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V1.NeedRebuildProgress)
	{
	  Controller->V1.NeedRebuildProgress = false;
	  Command->V1.CommandMailbox.Type3.CommandOpcode =
	    DAC960_V1_GetRebuildProgress;
	  Command->V1.CommandMailbox.Type3.BusAddress =
	    	Controller->V1.RebuildProgressDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V1.NeedConsistencyCheckProgress)
	{
	  Controller->V1.NeedConsistencyCheckProgress = false;
	  Command->V1.CommandMailbox.Type3.CommandOpcode =
	    DAC960_V1_RebuildStat;
	  Command->V1.CommandMailbox.Type3.BusAddress =
	    Controller->V1.RebuildProgressDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V1.NeedBackgroundInitializationStatus)
	{
	  Controller->V1.NeedBackgroundInitializationStatus = false;
	  Command->V1.CommandMailbox.Type3B.CommandOpcode =
	    DAC960_V1_BackgroundInitializationControl;
	  Command->V1.CommandMailbox.Type3B.CommandOpcode2 = 0x20;
	  Command->V1.CommandMailbox.Type3B.BusAddress =
	    Controller->V1.BackgroundInitializationStatusDMA;
	  DAC960_QueueCommand(Command);
	  return;
	}
      Controller->MonitoringTimerCount++;
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_MonitoringTimerInterval;
      	add_timer(&Controller->MonitoringTimer);
    }
  if (CommandType == DAC960_ImmediateCommand)
    {
      complete(Command->Completion);
      Command->Completion = NULL;
      return;
    }
  if (CommandType == DAC960_QueuedCommand)
    {
      DAC960_V1_KernelCommand_T *KernelCommand = Command->V1.KernelCommand;
      KernelCommand->CommandStatus = Command->V1.CommandStatus;
      Command->V1.KernelCommand = NULL;
      if (CommandOpcode == DAC960_V1_DCDB)
	Controller->V1.DirectCommandActive[KernelCommand->DCDB->Channel]
					  [KernelCommand->DCDB->TargetID] =
	  false;
      DAC960_DeallocateCommand(Command);
      KernelCommand->CompletionFunction(KernelCommand);
      return;
    }
  /*
    Queue a Status Monitoring Command to the Controller using the just
    completed Command if one was deferred previously due to lack of a
    free Command when the Monitoring Timer Function was called.
  */
  if (Controller->MonitoringCommandDeferred)
    {
      Controller->MonitoringCommandDeferred = false;
      DAC960_V1_QueueMonitoringCommand(Command);
      return;
    }
  /*
    Deallocate the Command.
  */
  DAC960_DeallocateCommand(Command);
  /*
    Wake up any processes waiting on a free Command.
  */
  wake_up(&Controller->CommandWaitQueue);
}


/*
  DAC960_V2_ReadWriteError prints an appropriate error message for Command
  when an error occurs on a Read or Write operation.
*/

static void DAC960_V2_ReadWriteError(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  unsigned char *SenseErrors[] = { "NO SENSE", "RECOVERED ERROR",
				   "NOT READY", "MEDIUM ERROR",
				   "HARDWARE ERROR", "ILLEGAL REQUEST",
				   "UNIT ATTENTION", "DATA PROTECT",
				   "BLANK CHECK", "VENDOR-SPECIFIC",
				   "COPY ABORTED", "ABORTED COMMAND",
				   "EQUAL", "VOLUME OVERFLOW",
				   "MISCOMPARE", "RESERVED" };
  unsigned char *CommandName = "UNKNOWN";
  switch (Command->CommandType)
    {
    case DAC960_ReadCommand:
    case DAC960_ReadRetryCommand:
      CommandName = "READ";
      break;
    case DAC960_WriteCommand:
    case DAC960_WriteRetryCommand:
      CommandName = "WRITE";
      break;
    case DAC960_MonitoringCommand:
    case DAC960_ImmediateCommand:
    case DAC960_QueuedCommand:
      break;
    }
  DAC960_Error("Error Condition %s on %s:\n", Controller,
	       SenseErrors[Command->V2.RequestSense->SenseKey], CommandName);
  DAC960_Error("  /dev/rd/c%dd%d:   absolute blocks %u..%u\n",
	       Controller, Controller->ControllerNumber,
	       Command->LogicalDriveNumber, Command->BlockNumber,
	       Command->BlockNumber + Command->BlockCount - 1);
}


/*
  DAC960_V2_ReportEvent prints an appropriate message when a Controller Event
  occurs.
*/

static void DAC960_V2_ReportEvent(DAC960_Controller_T *Controller,
				  DAC960_V2_Event_T *Event)
{
  DAC960_SCSI_RequestSense_T *RequestSense =
    (DAC960_SCSI_RequestSense_T *) &Event->RequestSenseData;
  unsigned char MessageBuffer[DAC960_LineBufferSize];
  static struct { int EventCode; unsigned char *EventMessage; } EventList[] =
    { /* Physical Device Events (0x0000 - 0x007F) */
      { 0x0001, "P Online" },
      { 0x0002, "P Standby" },
      { 0x0005, "P Automatic Rebuild Started" },
      { 0x0006, "P Manual Rebuild Started" },
      { 0x0007, "P Rebuild Completed" },
      { 0x0008, "P Rebuild Cancelled" },
      { 0x0009, "P Rebuild Failed for Unknown Reasons" },
      { 0x000A, "P Rebuild Failed due to New Physical Device" },
      { 0x000B, "P Rebuild Failed due to Logical Drive Failure" },
      { 0x000C, "S Offline" },
      { 0x000D, "P Found" },
      { 0x000E, "P Removed" },
      { 0x000F, "P Unconfigured" },
      { 0x0010, "P Expand Capacity Started" },
      { 0x0011, "P Expand Capacity Completed" },
      { 0x0012, "P Expand Capacity Failed" },
      { 0x0013, "P Command Timed Out" },
      { 0x0014, "P Command Aborted" },
      { 0x0015, "P Command Retried" },
      { 0x0016, "P Parity Error" },
      { 0x0017, "P Soft Error" },
      { 0x0018, "P Miscellaneous Error" },
      { 0x0019, "P Reset" },
      { 0x001A, "P Active Spare Found" },
      { 0x001B, "P Warm Spare Found" },
      { 0x001C, "S Sense Data Received" },
      { 0x001D, "P Initialization Started" },
      { 0x001E, "P Initialization Completed" },
      { 0x001F, "P Initialization Failed" },
      { 0x0020, "P Initialization Cancelled" },
      { 0x0021, "P Failed because Write Recovery Failed" },
      { 0x0022, "P Failed because SCSI Bus Reset Failed" },
      { 0x0023, "P Failed because of Double Check Condition" },
      { 0x0024, "P Failed because Device Cannot Be Accessed" },
      { 0x0025, "P Failed because of Gross Error on SCSI Processor" },
      { 0x0026, "P Failed because of Bad Tag from Device" },
      { 0x0027, "P Failed because of Command Timeout" },
      { 0x0028, "P Failed because of System Reset" },
      { 0x0029, "P Failed because of Busy Status or Parity Error" },
      { 0x002A, "P Failed because Host Set Device to Failed State" },
      { 0x002B, "P Failed because of Selection Timeout" },
      { 0x002C, "P Failed because of SCSI Bus Phase Error" },
      { 0x002D, "P Failed because Device Returned Unknown Status" },
      { 0x002E, "P Failed because Device Not Ready" },
      { 0x002F, "P Failed because Device Not Found at Startup" },
      { 0x0030, "P Failed because COD Write Operation Failed" },
      { 0x0031, "P Failed because BDT Write Operation Failed" },
      { 0x0039, "P Missing at Startup" },
      { 0x003A, "P Start Rebuild Failed due to Physical Drive Too Small" },
      { 0x003C, "P Temporarily Offline Device Automatically Made Online" },
      { 0x003D, "P Standby Rebuild Started" },
      /* Logical Device Events (0x0080 - 0x00FF) */
      { 0x0080, "M Consistency Check Started" },
      { 0x0081, "M Consistency Check Completed" },
      { 0x0082, "M Consistency Check Cancelled" },
      { 0x0083, "M Consistency Check Completed With Errors" },
      { 0x0084, "M Consistency Check Failed due to Logical Drive Failure" },
      { 0x0085, "M Consistency Check Failed due to Physical Device Failure" },
      { 0x0086, "L Offline" },
      { 0x0087, "L Critical" },
      { 0x0088, "L Online" },
      { 0x0089, "M Automatic Rebuild Started" },
      { 0x008A, "M Manual Rebuild Started" },
      { 0x008B, "M Rebuild Completed" },
      { 0x008C, "M Rebuild Cancelled" },
      { 0x008D, "M Rebuild Failed for Unknown Reasons" },
      { 0x008E, "M Rebuild Failed due to New Physical Device" },
      { 0x008F, "M Rebuild Failed due to Logical Drive Failure" },
      { 0x0090, "M Initialization Started" },
      { 0x0091, "M Initialization Completed" },
      { 0x0092, "M Initialization Cancelled" },
      { 0x0093, "M Initialization Failed" },
      { 0x0094, "L Found" },
      { 0x0095, "L Deleted" },
      { 0x0096, "M Expand Capacity Started" },
      { 0x0097, "M Expand Capacity Completed" },
      { 0x0098, "M Expand Capacity Failed" },
      { 0x0099, "L Bad Block Found" },
      { 0x009A, "L Size Changed" },
      { 0x009B, "L Type Changed" },
      { 0x009C, "L Bad Data Block Found" },
      { 0x009E, "L Read of Data Block in BDT" },
      { 0x009F, "L Write Back Data for Disk Block Lost" },
      { 0x00A0, "L Temporarily Offline RAID-5/3 Drive Made Online" },
      { 0x00A1, "L Temporarily Offline RAID-6/1/0/7 Drive Made Online" },
      { 0x00A2, "L Standby Rebuild Started" },
      /* Fault Management Events (0x0100 - 0x017F) */
      { 0x0140, "E Fan %d Failed" },
      { 0x0141, "E Fan %d OK" },
      { 0x0142, "E Fan %d Not Present" },
      { 0x0143, "E Power Supply %d Failed" },
      { 0x0144, "E Power Supply %d OK" },
      { 0x0145, "E Power Supply %d Not Present" },
      { 0x0146, "E Temperature Sensor %d Temperature Exceeds Safe Limit" },
      { 0x0147, "E Temperature Sensor %d Temperature Exceeds Working Limit" },
      { 0x0148, "E Temperature Sensor %d Temperature Normal" },
      { 0x0149, "E Temperature Sensor %d Not Present" },
      { 0x014A, "E Enclosure Management Unit %d Access Critical" },
      { 0x014B, "E Enclosure Management Unit %d Access OK" },
      { 0x014C, "E Enclosure Management Unit %d Access Offline" },
      /* Controller Events (0x0180 - 0x01FF) */
      { 0x0181, "C Cache Write Back Error" },
      { 0x0188, "C Battery Backup Unit Found" },
      { 0x0189, "C Battery Backup Unit Charge Level Low" },
      { 0x018A, "C Battery Backup Unit Charge Level OK" },
      { 0x0193, "C Installation Aborted" },
      { 0x0195, "C Battery Backup Unit Physically Removed" },
      { 0x0196, "C Memory Error During Warm Boot" },
      { 0x019E, "C Memory Soft ECC Error Corrected" },
      { 0x019F, "C Memory Hard ECC Error Corrected" },
      { 0x01A2, "C Battery Backup Unit Failed" },
      { 0x01AB, "C Mirror Race Recovery Failed" },
      { 0x01AC, "C Mirror Race on Critical Drive" },
      /* Controller Internal Processor Events */
      { 0x0380, "C Internal Controller Hung" },
      { 0x0381, "C Internal Controller Firmware Breakpoint" },
      { 0x0390, "C Internal Controller i960 Processor Specific Error" },
      { 0x03A0, "C Internal Controller StrongARM Processor Specific Error" },
      { 0, "" } };
  int EventListIndex = 0, EventCode;
  unsigned char EventType, *EventMessage;
  if (Event->EventCode == 0x1C &&
      RequestSense->SenseKey == DAC960_SenseKey_VendorSpecific &&
      (RequestSense->AdditionalSenseCode == 0x80 ||
       RequestSense->AdditionalSenseCode == 0x81))
    Event->EventCode = ((RequestSense->AdditionalSenseCode - 0x80) << 8) |
		       RequestSense->AdditionalSenseCodeQualifier;
  while (true)
    {
      EventCode = EventList[EventListIndex].EventCode;
      if (EventCode == Event->EventCode || EventCode == 0) break;
      EventListIndex++;
    }
  EventType = EventList[EventListIndex].EventMessage[0];
  EventMessage = &EventList[EventListIndex].EventMessage[2];
  if (EventCode == 0)
    {
      DAC960_Critical("Unknown Controller Event Code %04X\n",
		      Controller, Event->EventCode);
      return;
    }
  switch (EventType)
    {
    case 'P':
      DAC960_Critical("Physical Device %d:%d %s\n", Controller,
		      Event->Channel, Event->TargetID, EventMessage);
      break;
    case 'L':
      DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) %s\n", Controller,
		      Event->LogicalUnit, Controller->ControllerNumber,
		      Event->LogicalUnit, EventMessage);
      break;
    case 'M':
      DAC960_Progress("Logical Drive %d (/dev/rd/c%dd%d) %s\n", Controller,
		      Event->LogicalUnit, Controller->ControllerNumber,
		      Event->LogicalUnit, EventMessage);
      break;
    case 'S':
      if (RequestSense->SenseKey == DAC960_SenseKey_NoSense ||
	  (RequestSense->SenseKey == DAC960_SenseKey_NotReady &&
	   RequestSense->AdditionalSenseCode == 0x04 &&
	   (RequestSense->AdditionalSenseCodeQualifier == 0x01 ||
	    RequestSense->AdditionalSenseCodeQualifier == 0x02)))
	break;
      DAC960_Critical("Physical Device %d:%d %s\n", Controller,
		      Event->Channel, Event->TargetID, EventMessage);
      DAC960_Critical("Physical Device %d:%d Request Sense: "
		      "Sense Key = %X, ASC = %02X, ASCQ = %02X\n",
		      Controller,
		      Event->Channel,
		      Event->TargetID,
		      RequestSense->SenseKey,
		      RequestSense->AdditionalSenseCode,
		      RequestSense->AdditionalSenseCodeQualifier);
      DAC960_Critical("Physical Device %d:%d Request Sense: "
		      "Information = %02X%02X%02X%02X "
		      "%02X%02X%02X%02X\n",
		      Controller,
		      Event->Channel,
		      Event->TargetID,
		      RequestSense->Information[0],
		      RequestSense->Information[1],
		      RequestSense->Information[2],
		      RequestSense->Information[3],
		      RequestSense->CommandSpecificInformation[0],
		      RequestSense->CommandSpecificInformation[1],
		      RequestSense->CommandSpecificInformation[2],
		      RequestSense->CommandSpecificInformation[3]);
      break;
    case 'E':
      if (Controller->SuppressEnclosureMessages) break;
      sprintf(MessageBuffer, EventMessage, Event->LogicalUnit);
      DAC960_Critical("Enclosure %d %s\n", Controller,
		      Event->TargetID, MessageBuffer);
      break;
    case 'C':
      DAC960_Critical("Controller %s\n", Controller, EventMessage);
      break;
    default:
      DAC960_Critical("Unknown Controller Event Code %04X\n",
		      Controller, Event->EventCode);
      break;
    }
}


/*
  DAC960_V2_ReportProgress prints an appropriate progress message for
  Logical Device Long Operations.
*/

static void DAC960_V2_ReportProgress(DAC960_Controller_T *Controller,
				     unsigned char *MessageString,
				     unsigned int LogicalDeviceNumber,
				     unsigned long BlocksCompleted,
				     unsigned long LogicalDeviceSize)
{
  Controller->EphemeralProgressMessage = true;
  DAC960_Progress("%s in Progress: Logical Drive %d (/dev/rd/c%dd%d) "
		  "%d%% completed\n", Controller,
		  MessageString,
		  LogicalDeviceNumber,
		  Controller->ControllerNumber,
		  LogicalDeviceNumber,
		  (100 * (BlocksCompleted >> 7)) / (LogicalDeviceSize >> 7));
  Controller->EphemeralProgressMessage = false;
}


/*
  DAC960_V2_ProcessCompletedCommand performs completion processing for Command
  for DAC960 V2 Firmware Controllers.
*/

static void DAC960_V2_ProcessCompletedCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_CommandType_T CommandType = Command->CommandType;
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_IOCTL_Opcode_T IOCTLOpcode = CommandMailbox->Common.IOCTL_Opcode;
  DAC960_V2_CommandOpcode_T CommandOpcode = CommandMailbox->SCSI_10.CommandOpcode;
  DAC960_V2_CommandStatus_T CommandStatus = Command->V2.CommandStatus;

  if (CommandType == DAC960_ReadCommand ||
      CommandType == DAC960_WriteCommand)
    {

#ifdef FORCE_RETRY_DEBUG
      CommandStatus = DAC960_V2_AbormalCompletion;
#endif
      Command->V2.RequestSense->SenseKey = DAC960_SenseKey_MediumError;

      if (CommandStatus == DAC960_V2_NormalCompletion) {

		if (!DAC960_ProcessCompletedRequest(Command, true))
			BUG();

      } else if (Command->V2.RequestSense->SenseKey == DAC960_SenseKey_MediumError)
	{
	  /*
	   * break the command down into pieces and resubmit each
	   * piece, hoping that some of them will succeed.
	   */
	   DAC960_queue_partial_rw(Command);
	   return;
	}
      else
	{
	  if (Command->V2.RequestSense->SenseKey != DAC960_SenseKey_NotReady)
	    DAC960_V2_ReadWriteError(Command);
	  /*
	    Perform completion processing for all buffers in this I/O Request.
	  */
          (void)DAC960_ProcessCompletedRequest(Command, false);
	}
    }
  else if (CommandType == DAC960_ReadRetryCommand ||
	   CommandType == DAC960_WriteRetryCommand)
    {
      bool normal_completion;

#ifdef FORCE_RETRY_FAILURE_DEBUG
      static int retry_count = 1;
#endif
      /*
        Perform completion processing for the portion that was
	retried, and submit the next portion, if any.
      */
      normal_completion = true;
      if (CommandStatus != DAC960_V2_NormalCompletion) {
	normal_completion = false;
	if (Command->V2.RequestSense->SenseKey != DAC960_SenseKey_NotReady)
	    DAC960_V2_ReadWriteError(Command);
      }

#ifdef FORCE_RETRY_FAILURE_DEBUG
      if (!(++retry_count % 10000)) {
	      printk("V2 error retry failure test\n");
	      normal_completion = false;
	      DAC960_V2_ReadWriteError(Command);
      }
#endif

      if (!DAC960_ProcessCompletedRequest(Command, normal_completion)) {
		DAC960_queue_partial_rw(Command);
        	return;
      }
    }
  else if (CommandType == DAC960_MonitoringCommand)
    {
      if (Controller->ShutdownMonitoringTimer)
	      return;
      if (IOCTLOpcode == DAC960_V2_GetControllerInfo)
	{
	  DAC960_V2_ControllerInfo_T *NewControllerInfo =
	    Controller->V2.NewControllerInformation;
	  DAC960_V2_ControllerInfo_T *ControllerInfo =
	    &Controller->V2.ControllerInformation;
	  Controller->LogicalDriveCount =
	    NewControllerInfo->LogicalDevicesPresent;
	  Controller->V2.NeedLogicalDeviceInformation = true;
	  Controller->V2.NeedPhysicalDeviceInformation = true;
	  Controller->V2.StartLogicalDeviceInformationScan = true;
	  Controller->V2.StartPhysicalDeviceInformationScan = true;
	  Controller->MonitoringAlertMode =
	    (NewControllerInfo->LogicalDevicesCritical > 0 ||
	     NewControllerInfo->LogicalDevicesOffline > 0 ||
	     NewControllerInfo->PhysicalDisksCritical > 0 ||
	     NewControllerInfo->PhysicalDisksOffline > 0);
	  memcpy(ControllerInfo, NewControllerInfo,
		 sizeof(DAC960_V2_ControllerInfo_T));
	}
      else if (IOCTLOpcode == DAC960_V2_GetEvent)
	{
	  if (CommandStatus == DAC960_V2_NormalCompletion) {
	    DAC960_V2_ReportEvent(Controller, Controller->V2.Event);
	  }
	  Controller->V2.NextEventSequenceNumber++;
	}
      else if (IOCTLOpcode == DAC960_V2_GetPhysicalDeviceInfoValid &&
	       CommandStatus == DAC960_V2_NormalCompletion)
	{
	  DAC960_V2_PhysicalDeviceInfo_T *NewPhysicalDeviceInfo =
	    Controller->V2.NewPhysicalDeviceInformation;
	  unsigned int PhysicalDeviceIndex = Controller->V2.PhysicalDeviceIndex;
	  DAC960_V2_PhysicalDeviceInfo_T *PhysicalDeviceInfo =
	    Controller->V2.PhysicalDeviceInformation[PhysicalDeviceIndex];
	  DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
	    Controller->V2.InquiryUnitSerialNumber[PhysicalDeviceIndex];
	  unsigned int DeviceIndex;
	  while (PhysicalDeviceInfo != NULL &&
		 (NewPhysicalDeviceInfo->Channel >
		  PhysicalDeviceInfo->Channel ||
		  (NewPhysicalDeviceInfo->Channel ==
		   PhysicalDeviceInfo->Channel &&
		   (NewPhysicalDeviceInfo->TargetID >
		    PhysicalDeviceInfo->TargetID ||
		   (NewPhysicalDeviceInfo->TargetID ==
		    PhysicalDeviceInfo->TargetID &&
		    NewPhysicalDeviceInfo->LogicalUnit >
		    PhysicalDeviceInfo->LogicalUnit)))))
	    {
	      DAC960_Critical("Physical Device %d:%d No Longer Exists\n",
			      Controller,
			      PhysicalDeviceInfo->Channel,
			      PhysicalDeviceInfo->TargetID);
	      Controller->V2.PhysicalDeviceInformation
			     [PhysicalDeviceIndex] = NULL;
	      Controller->V2.InquiryUnitSerialNumber
			     [PhysicalDeviceIndex] = NULL;
	      kfree(PhysicalDeviceInfo);
	      kfree(InquiryUnitSerialNumber);
	      for (DeviceIndex = PhysicalDeviceIndex;
		   DeviceIndex < DAC960_V2_MaxPhysicalDevices - 1;
		   DeviceIndex++)
		{
		  Controller->V2.PhysicalDeviceInformation[DeviceIndex] =
		    Controller->V2.PhysicalDeviceInformation[DeviceIndex+1];
		  Controller->V2.InquiryUnitSerialNumber[DeviceIndex] =
		    Controller->V2.InquiryUnitSerialNumber[DeviceIndex+1];
		}
	      Controller->V2.PhysicalDeviceInformation
			     [DAC960_V2_MaxPhysicalDevices-1] = NULL;
	      Controller->V2.InquiryUnitSerialNumber
			     [DAC960_V2_MaxPhysicalDevices-1] = NULL;
	      PhysicalDeviceInfo =
		Controller->V2.PhysicalDeviceInformation[PhysicalDeviceIndex];
	      InquiryUnitSerialNumber =
		Controller->V2.InquiryUnitSerialNumber[PhysicalDeviceIndex];
	    }
	  if (PhysicalDeviceInfo == NULL ||
	      (NewPhysicalDeviceInfo->Channel !=
	       PhysicalDeviceInfo->Channel) ||
	      (NewPhysicalDeviceInfo->TargetID !=
	       PhysicalDeviceInfo->TargetID) ||
	      (NewPhysicalDeviceInfo->LogicalUnit !=
	       PhysicalDeviceInfo->LogicalUnit))
	    {
	      PhysicalDeviceInfo =
		kmalloc(sizeof(DAC960_V2_PhysicalDeviceInfo_T), GFP_ATOMIC);
	      InquiryUnitSerialNumber =
		  kmalloc(sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T),
			  GFP_ATOMIC);
	      if (InquiryUnitSerialNumber == NULL ||
		  PhysicalDeviceInfo == NULL)
		{
		  kfree(InquiryUnitSerialNumber);
		  InquiryUnitSerialNumber = NULL;
		  kfree(PhysicalDeviceInfo);
		  PhysicalDeviceInfo = NULL;
		}
	      DAC960_Critical("Physical Device %d:%d Now Exists%s\n",
			      Controller,
			      NewPhysicalDeviceInfo->Channel,
			      NewPhysicalDeviceInfo->TargetID,
			      (PhysicalDeviceInfo != NULL
			       ? "" : " - Allocation Failed"));
	      if (PhysicalDeviceInfo != NULL)
		{
		  memset(PhysicalDeviceInfo, 0,
			 sizeof(DAC960_V2_PhysicalDeviceInfo_T));
		  PhysicalDeviceInfo->PhysicalDeviceState =
		    DAC960_V2_Device_InvalidState;
		  memset(InquiryUnitSerialNumber, 0,
			 sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
		  InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
		  for (DeviceIndex = DAC960_V2_MaxPhysicalDevices - 1;
		       DeviceIndex > PhysicalDeviceIndex;
		       DeviceIndex--)
		    {
		      Controller->V2.PhysicalDeviceInformation[DeviceIndex] =
			Controller->V2.PhysicalDeviceInformation[DeviceIndex-1];
		      Controller->V2.InquiryUnitSerialNumber[DeviceIndex] =
			Controller->V2.InquiryUnitSerialNumber[DeviceIndex-1];
		    }
		  Controller->V2.PhysicalDeviceInformation
				 [PhysicalDeviceIndex] =
		    PhysicalDeviceInfo;
		  Controller->V2.InquiryUnitSerialNumber
				 [PhysicalDeviceIndex] =
		    InquiryUnitSerialNumber;
		  Controller->V2.NeedDeviceSerialNumberInformation = true;
		}
	    }
	  if (PhysicalDeviceInfo != NULL)
	    {
	      if (NewPhysicalDeviceInfo->PhysicalDeviceState !=
		  PhysicalDeviceInfo->PhysicalDeviceState)
		DAC960_Critical(
		  "Physical Device %d:%d is now %s\n", Controller,
		  NewPhysicalDeviceInfo->Channel,
		  NewPhysicalDeviceInfo->TargetID,
		  (NewPhysicalDeviceInfo->PhysicalDeviceState
		   == DAC960_V2_Device_Online
		   ? "ONLINE"
		   : NewPhysicalDeviceInfo->PhysicalDeviceState
		     == DAC960_V2_Device_Rebuild
		     ? "REBUILD"
		     : NewPhysicalDeviceInfo->PhysicalDeviceState
		       == DAC960_V2_Device_Missing
		       ? "MISSING"
		       : NewPhysicalDeviceInfo->PhysicalDeviceState
			 == DAC960_V2_Device_Critical
			 ? "CRITICAL"
			 : NewPhysicalDeviceInfo->PhysicalDeviceState
			   == DAC960_V2_Device_Dead
			   ? "DEAD"
			   : NewPhysicalDeviceInfo->PhysicalDeviceState
			     == DAC960_V2_Device_SuspectedDead
			     ? "SUSPECTED-DEAD"
			     : NewPhysicalDeviceInfo->PhysicalDeviceState
			       == DAC960_V2_Device_CommandedOffline
			       ? "COMMANDED-OFFLINE"
			       : NewPhysicalDeviceInfo->PhysicalDeviceState
				 == DAC960_V2_Device_Standby
				 ? "STANDBY" : "UNKNOWN"));
	      if ((NewPhysicalDeviceInfo->ParityErrors !=
		   PhysicalDeviceInfo->ParityErrors) ||
		  (NewPhysicalDeviceInfo->SoftErrors !=
		   PhysicalDeviceInfo->SoftErrors) ||
		  (NewPhysicalDeviceInfo->HardErrors !=
		   PhysicalDeviceInfo->HardErrors) ||
		  (NewPhysicalDeviceInfo->MiscellaneousErrors !=
		   PhysicalDeviceInfo->MiscellaneousErrors) ||
		  (NewPhysicalDeviceInfo->CommandTimeouts !=
		   PhysicalDeviceInfo->CommandTimeouts) ||
		  (NewPhysicalDeviceInfo->Retries !=
		   PhysicalDeviceInfo->Retries) ||
		  (NewPhysicalDeviceInfo->Aborts !=
		   PhysicalDeviceInfo->Aborts) ||
		  (NewPhysicalDeviceInfo->PredictedFailuresDetected !=
		   PhysicalDeviceInfo->PredictedFailuresDetected))
		{
		  DAC960_Critical("Physical Device %d:%d Errors: "
				  "Parity = %d, Soft = %d, "
				  "Hard = %d, Misc = %d\n",
				  Controller,
				  NewPhysicalDeviceInfo->Channel,
				  NewPhysicalDeviceInfo->TargetID,
				  NewPhysicalDeviceInfo->ParityErrors,
				  NewPhysicalDeviceInfo->SoftErrors,
				  NewPhysicalDeviceInfo->HardErrors,
				  NewPhysicalDeviceInfo->MiscellaneousErrors);
		  DAC960_Critical("Physical Device %d:%d Errors: "
				  "Timeouts = %d, Retries = %d, "
				  "Aborts = %d, Predicted = %d\n",
				  Controller,
				  NewPhysicalDeviceInfo->Channel,
				  NewPhysicalDeviceInfo->TargetID,
				  NewPhysicalDeviceInfo->CommandTimeouts,
				  NewPhysicalDeviceInfo->Retries,
				  NewPhysicalDeviceInfo->Aborts,
				  NewPhysicalDeviceInfo
				  ->PredictedFailuresDetected);
		}
	      if ((PhysicalDeviceInfo->PhysicalDeviceState
		   == DAC960_V2_Device_Dead ||
		   PhysicalDeviceInfo->PhysicalDeviceState
		   == DAC960_V2_Device_InvalidState) &&
		  NewPhysicalDeviceInfo->PhysicalDeviceState
		  != DAC960_V2_Device_Dead)
		Controller->V2.NeedDeviceSerialNumberInformation = true;
	      memcpy(PhysicalDeviceInfo, NewPhysicalDeviceInfo,
		     sizeof(DAC960_V2_PhysicalDeviceInfo_T));
	    }
	  NewPhysicalDeviceInfo->LogicalUnit++;
	  Controller->V2.PhysicalDeviceIndex++;
	}
      else if (IOCTLOpcode == DAC960_V2_GetPhysicalDeviceInfoValid)
	{
	  unsigned int DeviceIndex;
	  for (DeviceIndex = Controller->V2.PhysicalDeviceIndex;
	       DeviceIndex < DAC960_V2_MaxPhysicalDevices;
	       DeviceIndex++)
	    {
	      DAC960_V2_PhysicalDeviceInfo_T *PhysicalDeviceInfo =
		Controller->V2.PhysicalDeviceInformation[DeviceIndex];
	      DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
		Controller->V2.InquiryUnitSerialNumber[DeviceIndex];
	      if (PhysicalDeviceInfo == NULL) break;
	      DAC960_Critical("Physical Device %d:%d No Longer Exists\n",
			      Controller,
			      PhysicalDeviceInfo->Channel,
			      PhysicalDeviceInfo->TargetID);
	      Controller->V2.PhysicalDeviceInformation[DeviceIndex] = NULL;
	      Controller->V2.InquiryUnitSerialNumber[DeviceIndex] = NULL;
	      kfree(PhysicalDeviceInfo);
	      kfree(InquiryUnitSerialNumber);
	    }
	  Controller->V2.NeedPhysicalDeviceInformation = false;
	}
      else if (IOCTLOpcode == DAC960_V2_GetLogicalDeviceInfoValid &&
	       CommandStatus == DAC960_V2_NormalCompletion)
	{
	  DAC960_V2_LogicalDeviceInfo_T *NewLogicalDeviceInfo =
	    Controller->V2.NewLogicalDeviceInformation;
	  unsigned short LogicalDeviceNumber =
	    NewLogicalDeviceInfo->LogicalDeviceNumber;
	  DAC960_V2_LogicalDeviceInfo_T *LogicalDeviceInfo =
	    Controller->V2.LogicalDeviceInformation[LogicalDeviceNumber];
	  if (LogicalDeviceInfo == NULL)
	    {
	      DAC960_V2_PhysicalDevice_T PhysicalDevice;
	      PhysicalDevice.Controller = 0;
	      PhysicalDevice.Channel = NewLogicalDeviceInfo->Channel;
	      PhysicalDevice.TargetID = NewLogicalDeviceInfo->TargetID;
	      PhysicalDevice.LogicalUnit = NewLogicalDeviceInfo->LogicalUnit;
	      Controller->V2.LogicalDriveToVirtualDevice[LogicalDeviceNumber] =
		PhysicalDevice;
	      LogicalDeviceInfo = kmalloc(sizeof(DAC960_V2_LogicalDeviceInfo_T),
					  GFP_ATOMIC);
	      Controller->V2.LogicalDeviceInformation[LogicalDeviceNumber] =
		LogicalDeviceInfo;
	      DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
			      "Now Exists%s\n", Controller,
			      LogicalDeviceNumber,
			      Controller->ControllerNumber,
			      LogicalDeviceNumber,
			      (LogicalDeviceInfo != NULL
			       ? "" : " - Allocation Failed"));
	      if (LogicalDeviceInfo != NULL)
		{
		  memset(LogicalDeviceInfo, 0,
			 sizeof(DAC960_V2_LogicalDeviceInfo_T));
		  DAC960_ComputeGenericDiskInfo(Controller);
		}
	    }
	  if (LogicalDeviceInfo != NULL)
	    {
	      unsigned long LogicalDeviceSize =
		NewLogicalDeviceInfo->ConfigurableDeviceSize;
	      if (NewLogicalDeviceInfo->LogicalDeviceState !=
		  LogicalDeviceInfo->LogicalDeviceState)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDeviceNumber,
				Controller->ControllerNumber,
				LogicalDeviceNumber,
				(NewLogicalDeviceInfo->LogicalDeviceState
				 == DAC960_V2_LogicalDevice_Online
				 ? "ONLINE"
				 : NewLogicalDeviceInfo->LogicalDeviceState
				   == DAC960_V2_LogicalDevice_Critical
				   ? "CRITICAL" : "OFFLINE"));
	      if ((NewLogicalDeviceInfo->SoftErrors !=
		   LogicalDeviceInfo->SoftErrors) ||
		  (NewLogicalDeviceInfo->CommandsFailed !=
		   LogicalDeviceInfo->CommandsFailed) ||
		  (NewLogicalDeviceInfo->DeferredWriteErrors !=
		   LogicalDeviceInfo->DeferredWriteErrors))
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) Errors: "
				"Soft = %d, Failed = %d, Deferred Write = %d\n",
				Controller, LogicalDeviceNumber,
				Controller->ControllerNumber,
				LogicalDeviceNumber,
				NewLogicalDeviceInfo->SoftErrors,
				NewLogicalDeviceInfo->CommandsFailed,
				NewLogicalDeviceInfo->DeferredWriteErrors);
	      if (NewLogicalDeviceInfo->ConsistencyCheckInProgress)
		DAC960_V2_ReportProgress(Controller,
					 "Consistency Check",
					 LogicalDeviceNumber,
					 NewLogicalDeviceInfo
					 ->ConsistencyCheckBlockNumber,
					 LogicalDeviceSize);
	      else if (NewLogicalDeviceInfo->RebuildInProgress)
		DAC960_V2_ReportProgress(Controller,
					 "Rebuild",
					 LogicalDeviceNumber,
					 NewLogicalDeviceInfo
					 ->RebuildBlockNumber,
					 LogicalDeviceSize);
	      else if (NewLogicalDeviceInfo->BackgroundInitializationInProgress)
		DAC960_V2_ReportProgress(Controller,
					 "Background Initialization",
					 LogicalDeviceNumber,
					 NewLogicalDeviceInfo
					 ->BackgroundInitializationBlockNumber,
					 LogicalDeviceSize);
	      else if (NewLogicalDeviceInfo->ForegroundInitializationInProgress)
		DAC960_V2_ReportProgress(Controller,
					 "Foreground Initialization",
					 LogicalDeviceNumber,
					 NewLogicalDeviceInfo
					 ->ForegroundInitializationBlockNumber,
					 LogicalDeviceSize);
	      else if (NewLogicalDeviceInfo->DataMigrationInProgress)
		DAC960_V2_ReportProgress(Controller,
					 "Data Migration",
					 LogicalDeviceNumber,
					 NewLogicalDeviceInfo
					 ->DataMigrationBlockNumber,
					 LogicalDeviceSize);
	      else if (NewLogicalDeviceInfo->PatrolOperationInProgress)
		DAC960_V2_ReportProgress(Controller,
					 "Patrol Operation",
					 LogicalDeviceNumber,
					 NewLogicalDeviceInfo
					 ->PatrolOperationBlockNumber,
					 LogicalDeviceSize);
	      if (LogicalDeviceInfo->BackgroundInitializationInProgress &&
		  !NewLogicalDeviceInfo->BackgroundInitializationInProgress)
		DAC960_Progress("Logical Drive %d (/dev/rd/c%dd%d) "
				"Background Initialization %s\n",
				Controller,
				LogicalDeviceNumber,
				Controller->ControllerNumber,
				LogicalDeviceNumber,
				(NewLogicalDeviceInfo->LogicalDeviceControl
						      .LogicalDeviceInitialized
				 ? "Completed" : "Failed"));
	      memcpy(LogicalDeviceInfo, NewLogicalDeviceInfo,
		     sizeof(DAC960_V2_LogicalDeviceInfo_T));
	    }
	  Controller->V2.LogicalDriveFoundDuringScan
			 [LogicalDeviceNumber] = true;
	  NewLogicalDeviceInfo->LogicalDeviceNumber++;
	}
      else if (IOCTLOpcode == DAC960_V2_GetLogicalDeviceInfoValid)
	{
	  int LogicalDriveNumber;
	  for (LogicalDriveNumber = 0;
	       LogicalDriveNumber < DAC960_MaxLogicalDrives;
	       LogicalDriveNumber++)
	    {
	      DAC960_V2_LogicalDeviceInfo_T *LogicalDeviceInfo =
		Controller->V2.LogicalDeviceInformation[LogicalDriveNumber];
	      if (LogicalDeviceInfo == NULL ||
		  Controller->V2.LogicalDriveFoundDuringScan
				 [LogicalDriveNumber])
		continue;
	      DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
			      "No Longer Exists\n", Controller,
			      LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	      Controller->V2.LogicalDeviceInformation
			     [LogicalDriveNumber] = NULL;
	      kfree(LogicalDeviceInfo);
	      Controller->LogicalDriveInitiallyAccessible
			  [LogicalDriveNumber] = false;
	      DAC960_ComputeGenericDiskInfo(Controller);
	    }
	  Controller->V2.NeedLogicalDeviceInformation = false;
	}
      else if (CommandOpcode == DAC960_V2_SCSI_10_Passthru)
        {
	    DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
		Controller->V2.InquiryUnitSerialNumber[Controller->V2.PhysicalDeviceIndex - 1];

	    if (CommandStatus != DAC960_V2_NormalCompletion) {
		memset(InquiryUnitSerialNumber,
			0, sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));
		InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;
	    } else
	  	memcpy(InquiryUnitSerialNumber,
			Controller->V2.NewInquiryUnitSerialNumber,
			sizeof(DAC960_SCSI_Inquiry_UnitSerialNumber_T));

	     Controller->V2.NeedDeviceSerialNumberInformation = false;
        }

      if (Controller->V2.HealthStatusBuffer->NextEventSequenceNumber
	  - Controller->V2.NextEventSequenceNumber > 0)
	{
	  CommandMailbox->GetEvent.CommandOpcode = DAC960_V2_IOCTL;
	  CommandMailbox->GetEvent.DataTransferSize = sizeof(DAC960_V2_Event_T);
	  CommandMailbox->GetEvent.EventSequenceNumberHigh16 =
	    Controller->V2.NextEventSequenceNumber >> 16;
	  CommandMailbox->GetEvent.ControllerNumber = 0;
	  CommandMailbox->GetEvent.IOCTL_Opcode =
	    DAC960_V2_GetEvent;
	  CommandMailbox->GetEvent.EventSequenceNumberLow16 =
	    Controller->V2.NextEventSequenceNumber & 0xFFFF;
	  CommandMailbox->GetEvent.DataTransferMemoryAddress
				  .ScatterGatherSegments[0]
				  .SegmentDataPointer =
	    Controller->V2.EventDMA;
	  CommandMailbox->GetEvent.DataTransferMemoryAddress
				  .ScatterGatherSegments[0]
				  .SegmentByteCount =
	    CommandMailbox->GetEvent.DataTransferSize;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V2.NeedPhysicalDeviceInformation)
	{
	  if (Controller->V2.NeedDeviceSerialNumberInformation)
	    {
	      DAC960_SCSI_Inquiry_UnitSerialNumber_T *InquiryUnitSerialNumber =
                Controller->V2.NewInquiryUnitSerialNumber;
	      InquiryUnitSerialNumber->PeripheralDeviceType = 0x1F;

	      DAC960_V2_ConstructNewUnitSerialNumber(Controller, CommandMailbox,
			Controller->V2.NewPhysicalDeviceInformation->Channel,
			Controller->V2.NewPhysicalDeviceInformation->TargetID,
		Controller->V2.NewPhysicalDeviceInformation->LogicalUnit - 1);


	      DAC960_QueueCommand(Command);
	      return;
	    }
	  if (Controller->V2.StartPhysicalDeviceInformationScan)
	    {
	      Controller->V2.PhysicalDeviceIndex = 0;
	      Controller->V2.NewPhysicalDeviceInformation->Channel = 0;
	      Controller->V2.NewPhysicalDeviceInformation->TargetID = 0;
	      Controller->V2.NewPhysicalDeviceInformation->LogicalUnit = 0;
	      Controller->V2.StartPhysicalDeviceInformationScan = false;
	    }
	  CommandMailbox->PhysicalDeviceInfo.CommandOpcode = DAC960_V2_IOCTL;
	  CommandMailbox->PhysicalDeviceInfo.DataTransferSize =
	    sizeof(DAC960_V2_PhysicalDeviceInfo_T);
	  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.LogicalUnit =
	    Controller->V2.NewPhysicalDeviceInformation->LogicalUnit;
	  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.TargetID =
	    Controller->V2.NewPhysicalDeviceInformation->TargetID;
	  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.Channel =
	    Controller->V2.NewPhysicalDeviceInformation->Channel;
	  CommandMailbox->PhysicalDeviceInfo.IOCTL_Opcode =
	    DAC960_V2_GetPhysicalDeviceInfoValid;
	  CommandMailbox->PhysicalDeviceInfo.DataTransferMemoryAddress
					    .ScatterGatherSegments[0]
					    .SegmentDataPointer =
	    Controller->V2.NewPhysicalDeviceInformationDMA;
	  CommandMailbox->PhysicalDeviceInfo.DataTransferMemoryAddress
					    .ScatterGatherSegments[0]
					    .SegmentByteCount =
	    CommandMailbox->PhysicalDeviceInfo.DataTransferSize;
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->V2.NeedLogicalDeviceInformation)
	{
	  if (Controller->V2.StartLogicalDeviceInformationScan)
	    {
	      int LogicalDriveNumber;
	      for (LogicalDriveNumber = 0;
		   LogicalDriveNumber < DAC960_MaxLogicalDrives;
		   LogicalDriveNumber++)
		Controller->V2.LogicalDriveFoundDuringScan
			       [LogicalDriveNumber] = false;
	      Controller->V2.NewLogicalDeviceInformation->LogicalDeviceNumber = 0;
	      Controller->V2.StartLogicalDeviceInformationScan = false;
	    }
	  CommandMailbox->LogicalDeviceInfo.CommandOpcode = DAC960_V2_IOCTL;
	  CommandMailbox->LogicalDeviceInfo.DataTransferSize =
	    sizeof(DAC960_V2_LogicalDeviceInfo_T);
	  CommandMailbox->LogicalDeviceInfo.LogicalDevice.LogicalDeviceNumber =
	    Controller->V2.NewLogicalDeviceInformation->LogicalDeviceNumber;
	  CommandMailbox->LogicalDeviceInfo.IOCTL_Opcode =
	    DAC960_V2_GetLogicalDeviceInfoValid;
	  CommandMailbox->LogicalDeviceInfo.DataTransferMemoryAddress
					   .ScatterGatherSegments[0]
					   .SegmentDataPointer =
	    Controller->V2.NewLogicalDeviceInformationDMA;
	  CommandMailbox->LogicalDeviceInfo.DataTransferMemoryAddress
					   .ScatterGatherSegments[0]
					   .SegmentByteCount =
	    CommandMailbox->LogicalDeviceInfo.DataTransferSize;
	  DAC960_QueueCommand(Command);
	  return;
	}
      Controller->MonitoringTimerCount++;
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_HealthStatusMonitoringInterval;
      	add_timer(&Controller->MonitoringTimer);
    }
  if (CommandType == DAC960_ImmediateCommand)
    {
      complete(Command->Completion);
      Command->Completion = NULL;
      return;
    }
  if (CommandType == DAC960_QueuedCommand)
    {
      DAC960_V2_KernelCommand_T *KernelCommand = Command->V2.KernelCommand;
      KernelCommand->CommandStatus = CommandStatus;
      KernelCommand->RequestSenseLength = Command->V2.RequestSenseLength;
      KernelCommand->DataTransferLength = Command->V2.DataTransferResidue;
      Command->V2.KernelCommand = NULL;
      DAC960_DeallocateCommand(Command);
      KernelCommand->CompletionFunction(KernelCommand);
      return;
    }
  /*
    Queue a Status Monitoring Command to the Controller using the just
    completed Command if one was deferred previously due to lack of a
    free Command when the Monitoring Timer Function was called.
  */
  if (Controller->MonitoringCommandDeferred)
    {
      Controller->MonitoringCommandDeferred = false;
      DAC960_V2_QueueMonitoringCommand(Command);
      return;
    }
  /*
    Deallocate the Command.
  */
  DAC960_DeallocateCommand(Command);
  /*
    Wake up any processes waiting on a free Command.
  */
  wake_up(&Controller->CommandWaitQueue);
}

/*
  DAC960_GEM_InterruptHandler handles hardware interrupts from DAC960 GEM Series
  Controllers.
*/

static irqreturn_t DAC960_GEM_InterruptHandler(int IRQ_Channel,
				       void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V2_StatusMailbox_T *NextStatusMailbox;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_GEM_AcknowledgeInterrupt(ControllerBaseAddress);
  NextStatusMailbox = Controller->V2.NextStatusMailbox;
  while (NextStatusMailbox->Fields.CommandIdentifier > 0)
    {
       DAC960_V2_CommandIdentifier_T CommandIdentifier =
           NextStatusMailbox->Fields.CommandIdentifier;
       DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
       Command->V2.CommandStatus = NextStatusMailbox->Fields.CommandStatus;
       Command->V2.RequestSenseLength =
           NextStatusMailbox->Fields.RequestSenseLength;
       Command->V2.DataTransferResidue =
           NextStatusMailbox->Fields.DataTransferResidue;
       NextStatusMailbox->Words[0] = 0;
       if (++NextStatusMailbox > Controller->V2.LastStatusMailbox)
           NextStatusMailbox = Controller->V2.FirstStatusMailbox;
       DAC960_V2_ProcessCompletedCommand(Command);
    }
  Controller->V2.NextStatusMailbox = NextStatusMailbox;
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}

/*
  DAC960_BA_InterruptHandler handles hardware interrupts from DAC960 BA Series
  Controllers.
*/

static irqreturn_t DAC960_BA_InterruptHandler(int IRQ_Channel,
				       void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V2_StatusMailbox_T *NextStatusMailbox;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_BA_AcknowledgeInterrupt(ControllerBaseAddress);
  NextStatusMailbox = Controller->V2.NextStatusMailbox;
  while (NextStatusMailbox->Fields.CommandIdentifier > 0)
    {
      DAC960_V2_CommandIdentifier_T CommandIdentifier =
	NextStatusMailbox->Fields.CommandIdentifier;
      DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
      Command->V2.CommandStatus = NextStatusMailbox->Fields.CommandStatus;
      Command->V2.RequestSenseLength =
	NextStatusMailbox->Fields.RequestSenseLength;
      Command->V2.DataTransferResidue =
	NextStatusMailbox->Fields.DataTransferResidue;
      NextStatusMailbox->Words[0] = 0;
      if (++NextStatusMailbox > Controller->V2.LastStatusMailbox)
	NextStatusMailbox = Controller->V2.FirstStatusMailbox;
      DAC960_V2_ProcessCompletedCommand(Command);
    }
  Controller->V2.NextStatusMailbox = NextStatusMailbox;
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}


/*
  DAC960_LP_InterruptHandler handles hardware interrupts from DAC960 LP Series
  Controllers.
*/

static irqreturn_t DAC960_LP_InterruptHandler(int IRQ_Channel,
				       void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V2_StatusMailbox_T *NextStatusMailbox;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_LP_AcknowledgeInterrupt(ControllerBaseAddress);
  NextStatusMailbox = Controller->V2.NextStatusMailbox;
  while (NextStatusMailbox->Fields.CommandIdentifier > 0)
    {
      DAC960_V2_CommandIdentifier_T CommandIdentifier =
	NextStatusMailbox->Fields.CommandIdentifier;
      DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
      Command->V2.CommandStatus = NextStatusMailbox->Fields.CommandStatus;
      Command->V2.RequestSenseLength =
	NextStatusMailbox->Fields.RequestSenseLength;
      Command->V2.DataTransferResidue =
	NextStatusMailbox->Fields.DataTransferResidue;
      NextStatusMailbox->Words[0] = 0;
      if (++NextStatusMailbox > Controller->V2.LastStatusMailbox)
	NextStatusMailbox = Controller->V2.FirstStatusMailbox;
      DAC960_V2_ProcessCompletedCommand(Command);
    }
  Controller->V2.NextStatusMailbox = NextStatusMailbox;
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}


/*
  DAC960_LA_InterruptHandler handles hardware interrupts from DAC960 LA Series
  Controllers.
*/

static irqreturn_t DAC960_LA_InterruptHandler(int IRQ_Channel,
				       void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_StatusMailbox_T *NextStatusMailbox;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_LA_AcknowledgeInterrupt(ControllerBaseAddress);
  NextStatusMailbox = Controller->V1.NextStatusMailbox;
  while (NextStatusMailbox->Fields.Valid)
    {
      DAC960_V1_CommandIdentifier_T CommandIdentifier =
	NextStatusMailbox->Fields.CommandIdentifier;
      DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
      Command->V1.CommandStatus = NextStatusMailbox->Fields.CommandStatus;
      NextStatusMailbox->Word = 0;
      if (++NextStatusMailbox > Controller->V1.LastStatusMailbox)
	NextStatusMailbox = Controller->V1.FirstStatusMailbox;
      DAC960_V1_ProcessCompletedCommand(Command);
    }
  Controller->V1.NextStatusMailbox = NextStatusMailbox;
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}


/*
  DAC960_PG_InterruptHandler handles hardware interrupts from DAC960 PG Series
  Controllers.
*/

static irqreturn_t DAC960_PG_InterruptHandler(int IRQ_Channel,
				       void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V1_StatusMailbox_T *NextStatusMailbox;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_PG_AcknowledgeInterrupt(ControllerBaseAddress);
  NextStatusMailbox = Controller->V1.NextStatusMailbox;
  while (NextStatusMailbox->Fields.Valid)
    {
      DAC960_V1_CommandIdentifier_T CommandIdentifier =
	NextStatusMailbox->Fields.CommandIdentifier;
      DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
      Command->V1.CommandStatus = NextStatusMailbox->Fields.CommandStatus;
      NextStatusMailbox->Word = 0;
      if (++NextStatusMailbox > Controller->V1.LastStatusMailbox)
	NextStatusMailbox = Controller->V1.FirstStatusMailbox;
      DAC960_V1_ProcessCompletedCommand(Command);
    }
  Controller->V1.NextStatusMailbox = NextStatusMailbox;
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}


/*
  DAC960_PD_InterruptHandler handles hardware interrupts from DAC960 PD Series
  Controllers.
*/

static irqreturn_t DAC960_PD_InterruptHandler(int IRQ_Channel,
				       void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  while (DAC960_PD_StatusAvailableP(ControllerBaseAddress))
    {
      DAC960_V1_CommandIdentifier_T CommandIdentifier =
	DAC960_PD_ReadStatusCommandIdentifier(ControllerBaseAddress);
      DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
      Command->V1.CommandStatus =
	DAC960_PD_ReadStatusRegister(ControllerBaseAddress);
      DAC960_PD_AcknowledgeInterrupt(ControllerBaseAddress);
      DAC960_PD_AcknowledgeStatus(ControllerBaseAddress);
      DAC960_V1_ProcessCompletedCommand(Command);
    }
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}


/*
  DAC960_P_InterruptHandler handles hardware interrupts from DAC960 P Series
  Controllers.

  Translations of DAC960_V1_Enquiry and DAC960_V1_GetDeviceState rely
  on the data having been placed into DAC960_Controller_T, rather than
  an arbitrary buffer.
*/

static irqreturn_t DAC960_P_InterruptHandler(int IRQ_Channel,
				      void *DeviceIdentifier)
{
  DAC960_Controller_T *Controller = DeviceIdentifier;
  void __iomem *ControllerBaseAddress = Controller->BaseAddress;
  unsigned long flags;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  while (DAC960_PD_StatusAvailableP(ControllerBaseAddress))
    {
      DAC960_V1_CommandIdentifier_T CommandIdentifier =
	DAC960_PD_ReadStatusCommandIdentifier(ControllerBaseAddress);
      DAC960_Command_T *Command = Controller->Commands[CommandIdentifier-1];
      DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
      DAC960_V1_CommandOpcode_T CommandOpcode =
	CommandMailbox->Common.CommandOpcode;
      Command->V1.CommandStatus =
	DAC960_PD_ReadStatusRegister(ControllerBaseAddress);
      DAC960_PD_AcknowledgeInterrupt(ControllerBaseAddress);
      DAC960_PD_AcknowledgeStatus(ControllerBaseAddress);
      switch (CommandOpcode)
	{
	case DAC960_V1_Enquiry_Old:
	  Command->V1.CommandMailbox.Common.CommandOpcode = DAC960_V1_Enquiry;
	  DAC960_P_To_PD_TranslateEnquiry(Controller->V1.NewEnquiry);
	  break;
	case DAC960_V1_GetDeviceState_Old:
	  Command->V1.CommandMailbox.Common.CommandOpcode =
	    					DAC960_V1_GetDeviceState;
	  DAC960_P_To_PD_TranslateDeviceState(Controller->V1.NewDeviceState);
	  break;
	case DAC960_V1_Read_Old:
	  Command->V1.CommandMailbox.Common.CommandOpcode = DAC960_V1_Read;
	  DAC960_P_To_PD_TranslateReadWriteCommand(CommandMailbox);
	  break;
	case DAC960_V1_Write_Old:
	  Command->V1.CommandMailbox.Common.CommandOpcode = DAC960_V1_Write;
	  DAC960_P_To_PD_TranslateReadWriteCommand(CommandMailbox);
	  break;
	case DAC960_V1_ReadWithScatterGather_Old:
	  Command->V1.CommandMailbox.Common.CommandOpcode =
	    DAC960_V1_ReadWithScatterGather;
	  DAC960_P_To_PD_TranslateReadWriteCommand(CommandMailbox);
	  break;
	case DAC960_V1_WriteWithScatterGather_Old:
	  Command->V1.CommandMailbox.Common.CommandOpcode =
	    DAC960_V1_WriteWithScatterGather;
	  DAC960_P_To_PD_TranslateReadWriteCommand(CommandMailbox);
	  break;
	default:
	  break;
	}
      DAC960_V1_ProcessCompletedCommand(Command);
    }
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequest(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return IRQ_HANDLED;
}


/*
  DAC960_V1_QueueMonitoringCommand queues a Monitoring Command to DAC960 V1
  Firmware Controllers.
*/

static void DAC960_V1_QueueMonitoringCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  DAC960_V1_ClearCommand(Command);
  Command->CommandType = DAC960_MonitoringCommand;
  CommandMailbox->Type3.CommandOpcode = DAC960_V1_Enquiry;
  CommandMailbox->Type3.BusAddress = Controller->V1.NewEnquiryDMA;
  DAC960_QueueCommand(Command);
}


/*
  DAC960_V2_QueueMonitoringCommand queues a Monitoring Command to DAC960 V2
  Firmware Controllers.
*/

static void DAC960_V2_QueueMonitoringCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_V2_CommandMailbox_T *CommandMailbox = &Command->V2.CommandMailbox;
  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_MonitoringCommand;
  CommandMailbox->ControllerInfo.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->ControllerInfo.CommandControlBits
				.DataTransferControllerToHost = true;
  CommandMailbox->ControllerInfo.CommandControlBits
				.NoAutoRequestSense = true;
  CommandMailbox->ControllerInfo.DataTransferSize =
    sizeof(DAC960_V2_ControllerInfo_T);
  CommandMailbox->ControllerInfo.ControllerNumber = 0;
  CommandMailbox->ControllerInfo.IOCTL_Opcode = DAC960_V2_GetControllerInfo;
  CommandMailbox->ControllerInfo.DataTransferMemoryAddress
				.ScatterGatherSegments[0]
				.SegmentDataPointer =
    Controller->V2.NewControllerInformationDMA;
  CommandMailbox->ControllerInfo.DataTransferMemoryAddress
				.ScatterGatherSegments[0]
				.SegmentByteCount =
    CommandMailbox->ControllerInfo.DataTransferSize;
  DAC960_QueueCommand(Command);
}


/*
  DAC960_MonitoringTimerFunction is the timer function for monitoring
  the status of DAC960 Controllers.
*/

static void DAC960_MonitoringTimerFunction(struct timer_list *t)
{
  DAC960_Controller_T *Controller = from_timer(Controller, t, MonitoringTimer);
  DAC960_Command_T *Command;
  unsigned long flags;

  if (Controller->FirmwareType == DAC960_V1_Controller)
    {
      spin_lock_irqsave(&Controller->queue_lock, flags);
      /*
	Queue a Status Monitoring Command to Controller.
      */
      Command = DAC960_AllocateCommand(Controller);
      if (Command != NULL)
	DAC960_V1_QueueMonitoringCommand(Command);
      else Controller->MonitoringCommandDeferred = true;
      spin_unlock_irqrestore(&Controller->queue_lock, flags);
    }
  else
    {
      DAC960_V2_ControllerInfo_T *ControllerInfo =
	&Controller->V2.ControllerInformation;
      unsigned int StatusChangeCounter =
	Controller->V2.HealthStatusBuffer->StatusChangeCounter;
      bool ForceMonitoringCommand = false;
      if (time_after(jiffies, Controller->SecondaryMonitoringTime
	  + DAC960_SecondaryMonitoringInterval))
	{
	  int LogicalDriveNumber;
	  for (LogicalDriveNumber = 0;
	       LogicalDriveNumber < DAC960_MaxLogicalDrives;
	       LogicalDriveNumber++)
	    {
	      DAC960_V2_LogicalDeviceInfo_T *LogicalDeviceInfo =
		Controller->V2.LogicalDeviceInformation[LogicalDriveNumber];
	      if (LogicalDeviceInfo == NULL) continue;
	      if (!LogicalDeviceInfo->LogicalDeviceControl
				     .LogicalDeviceInitialized)
		{
		  ForceMonitoringCommand = true;
		  break;
		}
	    }
	  Controller->SecondaryMonitoringTime = jiffies;
	}
      if (StatusChangeCounter == Controller->V2.StatusChangeCounter &&
	  Controller->V2.HealthStatusBuffer->NextEventSequenceNumber
	  == Controller->V2.NextEventSequenceNumber &&
	  (ControllerInfo->BackgroundInitializationsActive +
	   ControllerInfo->LogicalDeviceInitializationsActive +
	   ControllerInfo->PhysicalDeviceInitializationsActive +
	   ControllerInfo->ConsistencyChecksActive +
	   ControllerInfo->RebuildsActive +
	   ControllerInfo->OnlineExpansionsActive == 0 ||
	   time_before(jiffies, Controller->PrimaryMonitoringTime
	   + DAC960_MonitoringTimerInterval)) &&
	  !ForceMonitoringCommand)
	{
	  Controller->MonitoringTimer.expires =
	    jiffies + DAC960_HealthStatusMonitoringInterval;
	    add_timer(&Controller->MonitoringTimer);
	  return;
	}
      Controller->V2.StatusChangeCounter = StatusChangeCounter;
      Controller->PrimaryMonitoringTime = jiffies;

      spin_lock_irqsave(&Controller->queue_lock, flags);
      /*
	Queue a Status Monitoring Command to Controller.
      */
      Command = DAC960_AllocateCommand(Controller);
      if (Command != NULL)
	DAC960_V2_QueueMonitoringCommand(Command);
      else Controller->MonitoringCommandDeferred = true;
      spin_unlock_irqrestore(&Controller->queue_lock, flags);
      /*
	Wake up any processes waiting on a Health Status Buffer change.
      */
      wake_up(&Controller->HealthStatusWaitQueue);
    }
}

/*
  DAC960_CheckStatusBuffer verifies that there is room to hold ByteCount
  additional bytes in the Combined Status Buffer and grows the buffer if
  necessary.  It returns true if there is enough room and false otherwise.
*/

static bool DAC960_CheckStatusBuffer(DAC960_Controller_T *Controller,
					unsigned int ByteCount)
{
  unsigned char *NewStatusBuffer;
  if (Controller->InitialStatusLength + 1 +
      Controller->CurrentStatusLength + ByteCount + 1 <=
      Controller->CombinedStatusBufferLength)
    return true;
  if (Controller->CombinedStatusBufferLength == 0)
    {
      unsigned int NewStatusBufferLength = DAC960_InitialStatusBufferSize;
      while (NewStatusBufferLength < ByteCount)
	NewStatusBufferLength *= 2;
      Controller->CombinedStatusBuffer = kmalloc(NewStatusBufferLength,
						  GFP_ATOMIC);
      if (Controller->CombinedStatusBuffer == NULL) return false;
      Controller->CombinedStatusBufferLength = NewStatusBufferLength;
      return true;
    }
  NewStatusBuffer = kmalloc_array(2, Controller->CombinedStatusBufferLength,
                                  GFP_ATOMIC);
  if (NewStatusBuffer == NULL)
    {
      DAC960_Warning("Unable to expand Combined Status Buffer - Truncating\n",
		     Controller);
      return false;
    }
  memcpy(NewStatusBuffer, Controller->CombinedStatusBuffer,
	 Controller->CombinedStatusBufferLength);
  kfree(Controller->CombinedStatusBuffer);
  Controller->CombinedStatusBuffer = NewStatusBuffer;
  Controller->CombinedStatusBufferLength *= 2;
  Controller->CurrentStatusBuffer =
    &NewStatusBuffer[Controller->InitialStatusLength + 1];
  return true;
}


/*
  DAC960_Message prints Driver Messages.
*/

static void DAC960_Message(DAC960_MessageLevel_T MessageLevel,
			   unsigned char *Format,
			   DAC960_Controller_T *Controller,
			   ...)
{
  static unsigned char Buffer[DAC960_LineBufferSize];
  static bool BeginningOfLine = true;
  va_list Arguments;
  int Length = 0;
  va_start(Arguments, Controller);
  Length = vsprintf(Buffer, Format, Arguments);
  va_end(Arguments);
  if (Controller == NULL)
    printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	   DAC960_ControllerCount, Buffer);
  else if (MessageLevel == DAC960_AnnounceLevel ||
	   MessageLevel == DAC960_InfoLevel)
    {
      if (!Controller->ControllerInitialized)
	{
	  if (DAC960_CheckStatusBuffer(Controller, Length))
	    {
	      strcpy(&Controller->CombinedStatusBuffer
				  [Controller->InitialStatusLength],
		     Buffer);
	      Controller->InitialStatusLength += Length;
	      Controller->CurrentStatusBuffer =
		&Controller->CombinedStatusBuffer
			     [Controller->InitialStatusLength + 1];
	    }
	  if (MessageLevel == DAC960_AnnounceLevel)
	    {
	      static int AnnouncementLines = 0;
	      if (++AnnouncementLines <= 2)
		printk("%sDAC960: %s", DAC960_MessageLevelMap[MessageLevel],
		       Buffer);
	    }
	  else
	    {
	      if (BeginningOfLine)
		{
		  if (Buffer[0] != '\n' || Length > 1)
		    printk("%sDAC960#%d: %s",
			   DAC960_MessageLevelMap[MessageLevel],
			   Controller->ControllerNumber, Buffer);
		}
	      else printk("%s", Buffer);
	    }
	}
      else if (DAC960_CheckStatusBuffer(Controller, Length))
	{
	  strcpy(&Controller->CurrentStatusBuffer[
		    Controller->CurrentStatusLength], Buffer);
	  Controller->CurrentStatusLength += Length;
	}
    }
  else if (MessageLevel == DAC960_ProgressLevel)
    {
      strcpy(Controller->ProgressBuffer, Buffer);
      Controller->ProgressBufferLength = Length;
      if (Controller->EphemeralProgressMessage)
	{
	  if (time_after_eq(jiffies, Controller->LastProgressReportTime
	      + DAC960_ProgressReportingInterval))
	    {
	      printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
		     Controller->ControllerNumber, Buffer);
	      Controller->LastProgressReportTime = jiffies;
	    }
	}
      else printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
		  Controller->ControllerNumber, Buffer);
    }
  else if (MessageLevel == DAC960_UserCriticalLevel)
    {
      strcpy(&Controller->UserStatusBuffer[Controller->UserStatusLength],
	     Buffer);
      Controller->UserStatusLength += Length;
      if (Buffer[0] != '\n' || Length > 1)
	printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	       Controller->ControllerNumber, Buffer);
    }
  else
    {
      if (BeginningOfLine)
	printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	       Controller->ControllerNumber, Buffer);
      else printk("%s", Buffer);
    }
  BeginningOfLine = (Buffer[Length-1] == '\n');
}


/*
  DAC960_ParsePhysicalDevice parses spaces followed by a Physical Device
  Channel:TargetID specification from a User Command string.  It updates
  Channel and TargetID and returns true on success and false on failure.
*/

static bool DAC960_ParsePhysicalDevice(DAC960_Controller_T *Controller,
					  char *UserCommandString,
					  unsigned char *Channel,
					  unsigned char *TargetID)
{
  char *NewUserCommandString = UserCommandString;
  unsigned long XChannel, XTargetID;
  while (*UserCommandString == ' ') UserCommandString++;
  if (UserCommandString == NewUserCommandString)
    return false;
  XChannel = simple_strtoul(UserCommandString, &NewUserCommandString, 10);
  if (NewUserCommandString == UserCommandString ||
      *NewUserCommandString != ':' ||
      XChannel >= Controller->Channels)
    return false;
  UserCommandString = ++NewUserCommandString;
  XTargetID = simple_strtoul(UserCommandString, &NewUserCommandString, 10);
  if (NewUserCommandString == UserCommandString ||
      *NewUserCommandString != '\0' ||
      XTargetID >= Controller->Targets)
    return false;
  *Channel = XChannel;
  *TargetID = XTargetID;
  return true;
}


/*
  DAC960_ParseLogicalDrive parses spaces followed by a Logical Drive Number
  specification from a User Command string.  It updates LogicalDriveNumber and
  returns true on success and false on failure.
*/

static bool DAC960_ParseLogicalDrive(DAC960_Controller_T *Controller,
					char *UserCommandString,
					unsigned char *LogicalDriveNumber)
{
  char *NewUserCommandString = UserCommandString;
  unsigned long XLogicalDriveNumber;
  while (*UserCommandString == ' ') UserCommandString++;
  if (UserCommandString == NewUserCommandString)
    return false;
  XLogicalDriveNumber =
    simple_strtoul(UserCommandString, &NewUserCommandString, 10);
  if (NewUserCommandString == UserCommandString ||
      *NewUserCommandString != '\0' ||
      XLogicalDriveNumber > DAC960_MaxLogicalDrives - 1)
    return false;
  *LogicalDriveNumber = XLogicalDriveNumber;
  return true;
}


/*
  DAC960_V1_SetDeviceState sets the Device State for a Physical Device for
  DAC960 V1 Firmware Controllers.
*/

static void DAC960_V1_SetDeviceState(DAC960_Controller_T *Controller,
				     DAC960_Command_T *Command,
				     unsigned char Channel,
				     unsigned char TargetID,
				     DAC960_V1_PhysicalDeviceState_T
				       DeviceState,
				     const unsigned char *DeviceStateString)
{
  DAC960_V1_CommandMailbox_T *CommandMailbox = &Command->V1.CommandMailbox;
  CommandMailbox->Type3D.CommandOpcode = DAC960_V1_StartDevice;
  CommandMailbox->Type3D.Channel = Channel;
  CommandMailbox->Type3D.TargetID = TargetID;
  CommandMailbox->Type3D.DeviceState = DeviceState;
  CommandMailbox->Type3D.Modifier = 0;
  DAC960_ExecuteCommand(Command);
  switch (Command->V1.CommandStatus)
    {
    case DAC960_V1_NormalCompletion:
      DAC960_UserCritical("%s of Physical Device %d:%d Succeeded\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    case DAC960_V1_UnableToStartDevice:
      DAC960_UserCritical("%s of Physical Device %d:%d Failed - "
			  "Unable to Start Device\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    case DAC960_V1_NoDeviceAtAddress:
      DAC960_UserCritical("%s of Physical Device %d:%d Failed - "
			  "No Device at Address\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    case DAC960_V1_InvalidChannelOrTargetOrModifier:
      DAC960_UserCritical("%s of Physical Device %d:%d Failed - "
			  "Invalid Channel or Target or Modifier\n",
			  Controller, DeviceStateString, Channel, TargetID);
      break;
    case DAC960_V1_ChannelBusy:
      DAC960_UserCritical("%s of Physical Device %d:%d Failed - "
			  "Channel Busy\n", Controller,
			  DeviceStateString, Channel, TargetID);
      break;
    default:
      DAC960_UserCritical("%s of Physical Device %d:%d Failed - "
			  "Unexpected Status %04X\n", Controller,
			  DeviceStateString, Channel, TargetID,
			  Command->V1.CommandStatus);
      break;
    }
}


/*
  DAC960_V1_ExecuteUserCommand executes a User Command for DAC960 V1 Firmware
  Controllers.
*/

static bool DAC960_V1_ExecuteUserCommand(DAC960_Controller_T *Controller,
					    unsigned char *UserCommand)
{
  DAC960_Command_T *Command;
  DAC960_V1_CommandMailbox_T *CommandMailbox;
  unsigned long flags;
  unsigned char Channel, TargetID, LogicalDriveNumber;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  while ((Command = DAC960_AllocateCommand(Controller)) == NULL)
    DAC960_WaitForCommand(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  Controller->UserStatusLength = 0;
  DAC960_V1_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox = &Command->V1.CommandMailbox;
  if (strcmp(UserCommand, "flush-cache") == 0)
    {
      CommandMailbox->Type3.CommandOpcode = DAC960_V1_Flush;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Cache Flush Completed\n", Controller);
    }
  else if (strncmp(UserCommand, "kill", 4) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[4],
				      &Channel, &TargetID))
    {
      DAC960_V1_DeviceState_T *DeviceState =
	&Controller->V1.DeviceState[Channel][TargetID];
      if (DeviceState->Present &&
	  DeviceState->DeviceType == DAC960_V1_DiskType &&
	  DeviceState->DeviceState != DAC960_V1_Device_Dead)
	DAC960_V1_SetDeviceState(Controller, Command, Channel, TargetID,
				 DAC960_V1_Device_Dead, "Kill");
      else DAC960_UserCritical("Kill of Physical Device %d:%d Illegal\n",
			       Controller, Channel, TargetID);
    }
  else if (strncmp(UserCommand, "make-online", 11) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[11],
				      &Channel, &TargetID))
    {
      DAC960_V1_DeviceState_T *DeviceState =
	&Controller->V1.DeviceState[Channel][TargetID];
      if (DeviceState->Present &&
	  DeviceState->DeviceType == DAC960_V1_DiskType &&
	  DeviceState->DeviceState == DAC960_V1_Device_Dead)
	DAC960_V1_SetDeviceState(Controller, Command, Channel, TargetID,
				 DAC960_V1_Device_Online, "Make Online");
      else DAC960_UserCritical("Make Online of Physical Device %d:%d Illegal\n",
			       Controller, Channel, TargetID);

    }
  else if (strncmp(UserCommand, "make-standby", 12) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[12],
				      &Channel, &TargetID))
    {
      DAC960_V1_DeviceState_T *DeviceState =
	&Controller->V1.DeviceState[Channel][TargetID];
      if (DeviceState->Present &&
	  DeviceState->DeviceType == DAC960_V1_DiskType &&
	  DeviceState->DeviceState == DAC960_V1_Device_Dead)
	DAC960_V1_SetDeviceState(Controller, Command, Channel, TargetID,
				 DAC960_V1_Device_Standby, "Make Standby");
      else DAC960_UserCritical("Make Standby of Physical "
			       "Device %d:%d Illegal\n",
			       Controller, Channel, TargetID);
    }
  else if (strncmp(UserCommand, "rebuild", 7) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[7],
				      &Channel, &TargetID))
    {
      CommandMailbox->Type3D.CommandOpcode = DAC960_V1_RebuildAsync;
      CommandMailbox->Type3D.Channel = Channel;
      CommandMailbox->Type3D.TargetID = TargetID;
      DAC960_ExecuteCommand(Command);
      switch (Command->V1.CommandStatus)
	{
	case DAC960_V1_NormalCompletion:
	  DAC960_UserCritical("Rebuild of Physical Device %d:%d Initiated\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_V1_AttemptToRebuildOnlineDrive:
	  DAC960_UserCritical("Rebuild of Physical Device %d:%d Failed - "
			      "Attempt to Rebuild Online or "
			      "Unresponsive Drive\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_V1_NewDiskFailedDuringRebuild:
	  DAC960_UserCritical("Rebuild of Physical Device %d:%d Failed - "
			      "New Disk Failed During Rebuild\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_V1_InvalidDeviceAddress:
	  DAC960_UserCritical("Rebuild of Physical Device %d:%d Failed - "
			      "Invalid Device Address\n",
			      Controller, Channel, TargetID);
	  break;
	case DAC960_V1_RebuildOrCheckAlreadyInProgress:
	  DAC960_UserCritical("Rebuild of Physical Device %d:%d Failed - "
			      "Rebuild or Consistency Check Already "
			      "in Progress\n", Controller, Channel, TargetID);
	  break;
	default:
	  DAC960_UserCritical("Rebuild of Physical Device %d:%d Failed - "
			      "Unexpected Status %04X\n", Controller,
			      Channel, TargetID, Command->V1.CommandStatus);
	  break;
	}
    }
  else if (strncmp(UserCommand, "check-consistency", 17) == 0 &&
	   DAC960_ParseLogicalDrive(Controller, &UserCommand[17],
				    &LogicalDriveNumber))
    {
      CommandMailbox->Type3C.CommandOpcode = DAC960_V1_CheckConsistencyAsync;
      CommandMailbox->Type3C.LogicalDriveNumber = LogicalDriveNumber;
      CommandMailbox->Type3C.AutoRestore = true;
      DAC960_ExecuteCommand(Command);
      switch (Command->V1.CommandStatus)
	{
	case DAC960_V1_NormalCompletion:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Initiated\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	case DAC960_V1_DependentDiskIsDead:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - "
			      "Dependent Physical Device is DEAD\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	case DAC960_V1_InvalidOrNonredundantLogicalDrive:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - "
			      "Invalid or Nonredundant Logical Drive\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	case DAC960_V1_RebuildOrCheckAlreadyInProgress:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - Rebuild or "
			      "Consistency Check Already in Progress\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber);
	  break;
	default:
	  DAC960_UserCritical("Consistency Check of Logical Drive %d "
			      "(/dev/rd/c%dd%d) Failed - "
			      "Unexpected Status %04X\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber, Command->V1.CommandStatus);
	  break;
	}
    }
  else if (strcmp(UserCommand, "cancel-rebuild") == 0 ||
	   strcmp(UserCommand, "cancel-consistency-check") == 0)
    {
      /*
        the OldRebuildRateConstant is never actually used
        once its value is retrieved from the controller.
       */
      unsigned char *OldRebuildRateConstant;
      dma_addr_t OldRebuildRateConstantDMA;

      OldRebuildRateConstant = pci_alloc_consistent( Controller->PCIDevice,
		sizeof(char), &OldRebuildRateConstantDMA);
      if (OldRebuildRateConstant == NULL) {
         DAC960_UserCritical("Cancellation of Rebuild or "
			     "Consistency Check Failed - "
			     "Out of Memory",
                             Controller);
	 goto failure;
      }
      CommandMailbox->Type3R.CommandOpcode = DAC960_V1_RebuildControl;
      CommandMailbox->Type3R.RebuildRateConstant = 0xFF;
      CommandMailbox->Type3R.BusAddress = OldRebuildRateConstantDMA;
      DAC960_ExecuteCommand(Command);
      switch (Command->V1.CommandStatus)
	{
	case DAC960_V1_NormalCompletion:
	  DAC960_UserCritical("Rebuild or Consistency Check Cancelled\n",
			      Controller);
	  break;
	default:
	  DAC960_UserCritical("Cancellation of Rebuild or "
			      "Consistency Check Failed - "
			      "Unexpected Status %04X\n",
			      Controller, Command->V1.CommandStatus);
	  break;
	}
failure:
  	pci_free_consistent(Controller->PCIDevice, sizeof(char),
		OldRebuildRateConstant, OldRebuildRateConstantDMA);
    }
  else DAC960_UserCritical("Illegal User Command: '%s'\n",
			   Controller, UserCommand);

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_DeallocateCommand(Command);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return true;
}


/*
  DAC960_V2_TranslatePhysicalDevice translates a Physical Device Channel and
  TargetID into a Logical Device.  It returns true on success and false
  on failure.
*/

static bool DAC960_V2_TranslatePhysicalDevice(DAC960_Command_T *Command,
						 unsigned char Channel,
						 unsigned char TargetID,
						 unsigned short
						   *LogicalDeviceNumber)
{
  DAC960_V2_CommandMailbox_T SavedCommandMailbox, *CommandMailbox;
  DAC960_Controller_T *Controller =  Command->Controller;

  CommandMailbox = &Command->V2.CommandMailbox;
  memcpy(&SavedCommandMailbox, CommandMailbox,
	 sizeof(DAC960_V2_CommandMailbox_T));

  CommandMailbox->PhysicalDeviceInfo.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->PhysicalDeviceInfo.CommandControlBits
				    .DataTransferControllerToHost = true;
  CommandMailbox->PhysicalDeviceInfo.CommandControlBits
				    .NoAutoRequestSense = true;
  CommandMailbox->PhysicalDeviceInfo.DataTransferSize =
    sizeof(DAC960_V2_PhysicalToLogicalDevice_T);
  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.TargetID = TargetID;
  CommandMailbox->PhysicalDeviceInfo.PhysicalDevice.Channel = Channel;
  CommandMailbox->PhysicalDeviceInfo.IOCTL_Opcode =
    DAC960_V2_TranslatePhysicalToLogicalDevice;
  CommandMailbox->Common.DataTransferMemoryAddress
			.ScatterGatherSegments[0]
			.SegmentDataPointer =
    		Controller->V2.PhysicalToLogicalDeviceDMA;
  CommandMailbox->Common.DataTransferMemoryAddress
			.ScatterGatherSegments[0]
			.SegmentByteCount =
    		CommandMailbox->Common.DataTransferSize;

  DAC960_ExecuteCommand(Command);
  *LogicalDeviceNumber = Controller->V2.PhysicalToLogicalDevice->LogicalDeviceNumber;

  memcpy(CommandMailbox, &SavedCommandMailbox,
	 sizeof(DAC960_V2_CommandMailbox_T));
  return (Command->V2.CommandStatus == DAC960_V2_NormalCompletion);
}


/*
  DAC960_V2_ExecuteUserCommand executes a User Command for DAC960 V2 Firmware
  Controllers.
*/

static bool DAC960_V2_ExecuteUserCommand(DAC960_Controller_T *Controller,
					    unsigned char *UserCommand)
{
  DAC960_Command_T *Command;
  DAC960_V2_CommandMailbox_T *CommandMailbox;
  unsigned long flags;
  unsigned char Channel, TargetID, LogicalDriveNumber;
  unsigned short LogicalDeviceNumber;

  spin_lock_irqsave(&Controller->queue_lock, flags);
  while ((Command = DAC960_AllocateCommand(Controller)) == NULL)
    DAC960_WaitForCommand(Controller);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  Controller->UserStatusLength = 0;
  DAC960_V2_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox = &Command->V2.CommandMailbox;
  CommandMailbox->Common.CommandOpcode = DAC960_V2_IOCTL;
  CommandMailbox->Common.CommandControlBits.DataTransferControllerToHost = true;
  CommandMailbox->Common.CommandControlBits.NoAutoRequestSense = true;
  if (strcmp(UserCommand, "flush-cache") == 0)
    {
      CommandMailbox->DeviceOperation.IOCTL_Opcode = DAC960_V2_PauseDevice;
      CommandMailbox->DeviceOperation.OperationDevice =
	DAC960_V2_RAID_Controller;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Cache Flush Completed\n", Controller);
    }
  else if (strncmp(UserCommand, "kill", 4) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[4],
				      &Channel, &TargetID) &&
	   DAC960_V2_TranslatePhysicalDevice(Command, Channel, TargetID,
					     &LogicalDeviceNumber))
    {
      CommandMailbox->SetDeviceState.LogicalDevice.LogicalDeviceNumber =
	LogicalDeviceNumber;
      CommandMailbox->SetDeviceState.IOCTL_Opcode =
	DAC960_V2_SetDeviceState;
      CommandMailbox->SetDeviceState.DeviceState.PhysicalDeviceState =
	DAC960_V2_Device_Dead;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Kill of Physical Device %d:%d %s\n",
			  Controller, Channel, TargetID,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Succeeded" : "Failed"));
    }
  else if (strncmp(UserCommand, "make-online", 11) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[11],
				      &Channel, &TargetID) &&
	   DAC960_V2_TranslatePhysicalDevice(Command, Channel, TargetID,
					     &LogicalDeviceNumber))
    {
      CommandMailbox->SetDeviceState.LogicalDevice.LogicalDeviceNumber =
	LogicalDeviceNumber;
      CommandMailbox->SetDeviceState.IOCTL_Opcode =
	DAC960_V2_SetDeviceState;
      CommandMailbox->SetDeviceState.DeviceState.PhysicalDeviceState =
	DAC960_V2_Device_Online;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Make Online of Physical Device %d:%d %s\n",
			  Controller, Channel, TargetID,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Succeeded" : "Failed"));
    }
  else if (strncmp(UserCommand, "make-standby", 12) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[12],
				      &Channel, &TargetID) &&
	   DAC960_V2_TranslatePhysicalDevice(Command, Channel, TargetID,
					     &LogicalDeviceNumber))
    {
      CommandMailbox->SetDeviceState.LogicalDevice.LogicalDeviceNumber =
	LogicalDeviceNumber;
      CommandMailbox->SetDeviceState.IOCTL_Opcode =
	DAC960_V2_SetDeviceState;
      CommandMailbox->SetDeviceState.DeviceState.PhysicalDeviceState =
	DAC960_V2_Device_Standby;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Make Standby of Physical Device %d:%d %s\n",
			  Controller, Channel, TargetID,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Succeeded" : "Failed"));
    }
  else if (strncmp(UserCommand, "rebuild", 7) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[7],
				      &Channel, &TargetID) &&
	   DAC960_V2_TranslatePhysicalDevice(Command, Channel, TargetID,
					     &LogicalDeviceNumber))
    {
      CommandMailbox->LogicalDeviceInfo.LogicalDevice.LogicalDeviceNumber =
	LogicalDeviceNumber;
      CommandMailbox->LogicalDeviceInfo.IOCTL_Opcode =
	DAC960_V2_RebuildDeviceStart;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Rebuild of Physical Device %d:%d %s\n",
			  Controller, Channel, TargetID,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Initiated" : "Not Initiated"));
    }
  else if (strncmp(UserCommand, "cancel-rebuild", 14) == 0 &&
	   DAC960_ParsePhysicalDevice(Controller, &UserCommand[14],
				      &Channel, &TargetID) &&
	   DAC960_V2_TranslatePhysicalDevice(Command, Channel, TargetID,
					     &LogicalDeviceNumber))
    {
      CommandMailbox->LogicalDeviceInfo.LogicalDevice.LogicalDeviceNumber =
	LogicalDeviceNumber;
      CommandMailbox->LogicalDeviceInfo.IOCTL_Opcode =
	DAC960_V2_RebuildDeviceStop;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Rebuild of Physical Device %d:%d %s\n",
			  Controller, Channel, TargetID,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Cancelled" : "Not Cancelled"));
    }
  else if (strncmp(UserCommand, "check-consistency", 17) == 0 &&
	   DAC960_ParseLogicalDrive(Controller, &UserCommand[17],
				    &LogicalDriveNumber))
    {
      CommandMailbox->ConsistencyCheck.LogicalDevice.LogicalDeviceNumber =
	LogicalDriveNumber;
      CommandMailbox->ConsistencyCheck.IOCTL_Opcode =
	DAC960_V2_ConsistencyCheckStart;
      CommandMailbox->ConsistencyCheck.RestoreConsistency = true;
      CommandMailbox->ConsistencyCheck.InitializedAreaOnly = false;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Consistency Check of Logical Drive %d "
			  "(/dev/rd/c%dd%d) %s\n",
			  Controller, LogicalDriveNumber,
			  Controller->ControllerNumber,
			  LogicalDriveNumber,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Initiated" : "Not Initiated"));
    }
  else if (strncmp(UserCommand, "cancel-consistency-check", 24) == 0 &&
	   DAC960_ParseLogicalDrive(Controller, &UserCommand[24],
				    &LogicalDriveNumber))
    {
      CommandMailbox->ConsistencyCheck.LogicalDevice.LogicalDeviceNumber =
	LogicalDriveNumber;
      CommandMailbox->ConsistencyCheck.IOCTL_Opcode =
	DAC960_V2_ConsistencyCheckStop;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Consistency Check of Logical Drive %d "
			  "(/dev/rd/c%dd%d) %s\n",
			  Controller, LogicalDriveNumber,
			  Controller->ControllerNumber,
			  LogicalDriveNumber,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Cancelled" : "Not Cancelled"));
    }
  else if (strcmp(UserCommand, "perform-discovery") == 0)
    {
      CommandMailbox->Common.IOCTL_Opcode = DAC960_V2_StartDiscovery;
      DAC960_ExecuteCommand(Command);
      DAC960_UserCritical("Discovery %s\n", Controller,
			  (Command->V2.CommandStatus
			   == DAC960_V2_NormalCompletion
			   ? "Initiated" : "Not Initiated"));
      if (Command->V2.CommandStatus == DAC960_V2_NormalCompletion)
	{
	  CommandMailbox->ControllerInfo.CommandOpcode = DAC960_V2_IOCTL;
	  CommandMailbox->ControllerInfo.CommandControlBits
					.DataTransferControllerToHost = true;
	  CommandMailbox->ControllerInfo.CommandControlBits
					.NoAutoRequestSense = true;
	  CommandMailbox->ControllerInfo.DataTransferSize =
	    sizeof(DAC960_V2_ControllerInfo_T);
	  CommandMailbox->ControllerInfo.ControllerNumber = 0;
	  CommandMailbox->ControllerInfo.IOCTL_Opcode =
	    DAC960_V2_GetControllerInfo;
	  /*
	   * How does this NOT race with the queued Monitoring
	   * usage of this structure?
	   */
	  CommandMailbox->ControllerInfo.DataTransferMemoryAddress
					.ScatterGatherSegments[0]
					.SegmentDataPointer =
	    Controller->V2.NewControllerInformationDMA;
	  CommandMailbox->ControllerInfo.DataTransferMemoryAddress
					.ScatterGatherSegments[0]
					.SegmentByteCount =
	    CommandMailbox->ControllerInfo.DataTransferSize;
	  while (1) {
	    DAC960_ExecuteCommand(Command);
	    if (!Controller->V2.NewControllerInformation->PhysicalScanActive)
		break;
	    msleep(1000);
	  }
	  DAC960_UserCritical("Discovery Completed\n", Controller);
 	}
    }
  else if (strcmp(UserCommand, "suppress-enclosure-messages") == 0)
    Controller->SuppressEnclosureMessages = true;
  else DAC960_UserCritical("Illegal User Command: '%s'\n",
			   Controller, UserCommand);

  spin_lock_irqsave(&Controller->queue_lock, flags);
  DAC960_DeallocateCommand(Command);
  spin_unlock_irqrestore(&Controller->queue_lock, flags);
  return true;
}

static int dac960_proc_show(struct seq_file *m, void *v)
{
  unsigned char *StatusMessage = "OK\n";
  int ControllerNumber;
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    {
      DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
      if (Controller == NULL) continue;
      if (Controller->MonitoringAlertMode)
	{
	  StatusMessage = "ALERT\n";
	  break;
	}
    }
  seq_puts(m, StatusMessage);
  return 0;
}

static int dac960_initial_status_proc_show(struct seq_file *m, void *v)
{
	DAC960_Controller_T *Controller = (DAC960_Controller_T *)m->private;
	seq_printf(m, "%.*s", Controller->InitialStatusLength, Controller->CombinedStatusBuffer);
	return 0;
}

static int dac960_current_status_proc_show(struct seq_file *m, void *v)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) m->private;
  unsigned char *StatusMessage =
    "No Rebuild or Consistency Check in Progress\n";
  int ProgressMessageLength = strlen(StatusMessage);
  if (jiffies != Controller->LastCurrentStatusTime)
    {
      Controller->CurrentStatusLength = 0;
      DAC960_AnnounceDriver(Controller);
      DAC960_ReportControllerConfiguration(Controller);
      DAC960_ReportDeviceConfiguration(Controller);
      if (Controller->ProgressBufferLength > 0)
	ProgressMessageLength = Controller->ProgressBufferLength;
      if (DAC960_CheckStatusBuffer(Controller, 2 + ProgressMessageLength))
	{
	  unsigned char *CurrentStatusBuffer = Controller->CurrentStatusBuffer;
	  CurrentStatusBuffer[Controller->CurrentStatusLength++] = ' ';
	  CurrentStatusBuffer[Controller->CurrentStatusLength++] = ' ';
	  if (Controller->ProgressBufferLength > 0)
	    strcpy(&CurrentStatusBuffer[Controller->CurrentStatusLength],
		   Controller->ProgressBuffer);
	  else
	    strcpy(&CurrentStatusBuffer[Controller->CurrentStatusLength],
		   StatusMessage);
	  Controller->CurrentStatusLength += ProgressMessageLength;
	}
      Controller->LastCurrentStatusTime = jiffies;
    }
	seq_printf(m, "%.*s", Controller->CurrentStatusLength, Controller->CurrentStatusBuffer);
	return 0;
}

static int dac960_user_command_proc_show(struct seq_file *m, void *v)
{
	DAC960_Controller_T *Controller = (DAC960_Controller_T *)m->private;

	seq_printf(m, "%.*s", Controller->UserStatusLength, Controller->UserStatusBuffer);
	return 0;
}

static int dac960_user_command_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dac960_user_command_proc_show, PDE_DATA(inode));
}

static ssize_t dac960_user_command_proc_write(struct file *file,
				       const char __user *Buffer,
				       size_t Count, loff_t *pos)
{
  DAC960_Controller_T *Controller = PDE_DATA(file_inode(file));
  unsigned char CommandBuffer[80];
  int Length;
  if (Count > sizeof(CommandBuffer)-1) return -EINVAL;
  if (copy_from_user(CommandBuffer, Buffer, Count)) return -EFAULT;
  CommandBuffer[Count] = '\0';
  Length = strlen(CommandBuffer);
  if (Length > 0 && CommandBuffer[Length-1] == '\n')
    CommandBuffer[--Length] = '\0';
  if (Controller->FirmwareType == DAC960_V1_Controller)
    return (DAC960_V1_ExecuteUserCommand(Controller, CommandBuffer)
	    ? Count : -EBUSY);
  else
    return (DAC960_V2_ExecuteUserCommand(Controller, CommandBuffer)
	    ? Count : -EBUSY);
}

static const struct file_operations dac960_user_command_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= dac960_user_command_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dac960_user_command_proc_write,
};

/*
  DAC960_CreateProcEntries creates the /proc/rd/... entries for the
  DAC960 Driver.
*/

static void DAC960_CreateProcEntries(DAC960_Controller_T *Controller)
{
	struct proc_dir_entry *ControllerProcEntry;

	if (DAC960_ProcDirectoryEntry == NULL) {
		DAC960_ProcDirectoryEntry = proc_mkdir("rd", NULL);
		proc_create_single("status", 0, DAC960_ProcDirectoryEntry,
				dac960_proc_show);
	}

	snprintf(Controller->ControllerName, sizeof(Controller->ControllerName),
		 "c%d", Controller->ControllerNumber);
	ControllerProcEntry = proc_mkdir(Controller->ControllerName,
					 DAC960_ProcDirectoryEntry);
	proc_create_single_data("initial_status", 0, ControllerProcEntry,
			dac960_initial_status_proc_show, Controller);
	proc_create_single_data("current_status", 0, ControllerProcEntry,
			dac960_current_status_proc_show, Controller);
	proc_create_data("user_command", 0600, ControllerProcEntry, &dac960_user_command_proc_fops, Controller);
	Controller->ControllerProcEntry = ControllerProcEntry;
}


/*
  DAC960_DestroyProcEntries destroys the /proc/rd/... entries for the
  DAC960 Driver.
*/

static void DAC960_DestroyProcEntries(DAC960_Controller_T *Controller)
{
      if (Controller->ControllerProcEntry == NULL)
	      return;
      remove_proc_entry("initial_status", Controller->ControllerProcEntry);
      remove_proc_entry("current_status", Controller->ControllerProcEntry);
      remove_proc_entry("user_command", Controller->ControllerProcEntry);
      remove_proc_entry(Controller->ControllerName, DAC960_ProcDirectoryEntry);
      Controller->ControllerProcEntry = NULL;
}

#ifdef DAC960_GAM_MINOR

static long DAC960_gam_get_controller_info(DAC960_ControllerInfo_T __user *UserSpaceControllerInfo)
{
	DAC960_ControllerInfo_T ControllerInfo;
	DAC960_Controller_T *Controller;
	int ControllerNumber;
	long ErrorCode;

	if (UserSpaceControllerInfo == NULL)
		ErrorCode = -EINVAL;
	else ErrorCode = get_user(ControllerNumber,
			     &UserSpaceControllerInfo->ControllerNumber);
	if (ErrorCode != 0)
		goto out;
	ErrorCode = -ENXIO;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1) {
		goto out;
	}
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
		goto out;
	memset(&ControllerInfo, 0, sizeof(DAC960_ControllerInfo_T));
	ControllerInfo.ControllerNumber = ControllerNumber;
	ControllerInfo.FirmwareType = Controller->FirmwareType;
	ControllerInfo.Channels = Controller->Channels;
	ControllerInfo.Targets = Controller->Targets;
	ControllerInfo.PCI_Bus = Controller->Bus;
	ControllerInfo.PCI_Device = Controller->Device;
	ControllerInfo.PCI_Function = Controller->Function;
	ControllerInfo.IRQ_Channel = Controller->IRQ_Channel;
	ControllerInfo.PCI_Address = Controller->PCI_Address;
	strcpy(ControllerInfo.ModelName, Controller->ModelName);
	strcpy(ControllerInfo.FirmwareVersion, Controller->FirmwareVersion);
	ErrorCode = (copy_to_user(UserSpaceControllerInfo, &ControllerInfo,
			     sizeof(DAC960_ControllerInfo_T)) ? -EFAULT : 0);
out:
	return ErrorCode;
}

static long DAC960_gam_v1_execute_command(DAC960_V1_UserCommand_T __user *UserSpaceUserCommand)
{
	DAC960_V1_UserCommand_T UserCommand;
	DAC960_Controller_T *Controller;
	DAC960_Command_T *Command = NULL;
	DAC960_V1_CommandOpcode_T CommandOpcode;
	DAC960_V1_CommandStatus_T CommandStatus;
	DAC960_V1_DCDB_T DCDB;
	DAC960_V1_DCDB_T *DCDB_IOBUF = NULL;
	dma_addr_t	DCDB_IOBUFDMA;
	unsigned long flags;
	int ControllerNumber, DataTransferLength;
	unsigned char *DataTransferBuffer = NULL;
	dma_addr_t DataTransferBufferDMA;
        long ErrorCode;

	if (UserSpaceUserCommand == NULL) {
		ErrorCode = -EINVAL;
		goto out;
	}
	if (copy_from_user(&UserCommand, UserSpaceUserCommand,
				   sizeof(DAC960_V1_UserCommand_T))) {
		ErrorCode = -EFAULT;
		goto out;
	}
	ControllerNumber = UserCommand.ControllerNumber;
    	ErrorCode = -ENXIO;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
		goto out;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
		goto out;
	ErrorCode = -EINVAL;
	if (Controller->FirmwareType != DAC960_V1_Controller)
		goto out;
	CommandOpcode = UserCommand.CommandMailbox.Common.CommandOpcode;
	DataTransferLength = UserCommand.DataTransferLength;
	if (CommandOpcode & 0x80)
		goto out;
	if (CommandOpcode == DAC960_V1_DCDB)
	  {
	    if (copy_from_user(&DCDB, UserCommand.DCDB,
			       sizeof(DAC960_V1_DCDB_T))) {
		ErrorCode = -EFAULT;
		goto out;
	    }
	    if (DCDB.Channel >= DAC960_V1_MaxChannels)
		goto out;
	    if (!((DataTransferLength == 0 &&
		   DCDB.Direction
		   == DAC960_V1_DCDB_NoDataTransfer) ||
		  (DataTransferLength > 0 &&
		   DCDB.Direction
		   == DAC960_V1_DCDB_DataTransferDeviceToSystem) ||
		  (DataTransferLength < 0 &&
		   DCDB.Direction
		   == DAC960_V1_DCDB_DataTransferSystemToDevice)))
			goto out;
	    if (((DCDB.TransferLengthHigh4 << 16) | DCDB.TransferLength)
		!= abs(DataTransferLength))
			goto out;
	    DCDB_IOBUF = pci_alloc_consistent(Controller->PCIDevice,
			sizeof(DAC960_V1_DCDB_T), &DCDB_IOBUFDMA);
	    if (DCDB_IOBUF == NULL) {
	    		ErrorCode = -ENOMEM;
			goto out;
		}
	  }
	ErrorCode = -ENOMEM;
	if (DataTransferLength > 0)
	  {
	    DataTransferBuffer = pci_zalloc_consistent(Controller->PCIDevice,
                                                       DataTransferLength,
                                                       &DataTransferBufferDMA);
	    if (DataTransferBuffer == NULL)
		goto out;
	  }
	else if (DataTransferLength < 0)
	  {
	    DataTransferBuffer = pci_alloc_consistent(Controller->PCIDevice,
				-DataTransferLength, &DataTransferBufferDMA);
	    if (DataTransferBuffer == NULL)
		goto out;
	    if (copy_from_user(DataTransferBuffer,
			       UserCommand.DataTransferBuffer,
			       -DataTransferLength)) {
		ErrorCode = -EFAULT;
		goto out;
	    }
	  }
	if (CommandOpcode == DAC960_V1_DCDB)
	  {
	    spin_lock_irqsave(&Controller->queue_lock, flags);
	    while ((Command = DAC960_AllocateCommand(Controller)) == NULL)
	      DAC960_WaitForCommand(Controller);
	    while (Controller->V1.DirectCommandActive[DCDB.Channel]
						     [DCDB.TargetID])
	      {
		spin_unlock_irq(&Controller->queue_lock);
		__wait_event(Controller->CommandWaitQueue,
			     !Controller->V1.DirectCommandActive
					     [DCDB.Channel][DCDB.TargetID]);
		spin_lock_irq(&Controller->queue_lock);
	      }
	    Controller->V1.DirectCommandActive[DCDB.Channel]
					      [DCDB.TargetID] = true;
	    spin_unlock_irqrestore(&Controller->queue_lock, flags);
	    DAC960_V1_ClearCommand(Command);
	    Command->CommandType = DAC960_ImmediateCommand;
	    memcpy(&Command->V1.CommandMailbox, &UserCommand.CommandMailbox,
		   sizeof(DAC960_V1_CommandMailbox_T));
	    Command->V1.CommandMailbox.Type3.BusAddress = DCDB_IOBUFDMA;
	    DCDB.BusAddress = DataTransferBufferDMA;
	    memcpy(DCDB_IOBUF, &DCDB, sizeof(DAC960_V1_DCDB_T));
	  }
	else
	  {
	    spin_lock_irqsave(&Controller->queue_lock, flags);
	    while ((Command = DAC960_AllocateCommand(Controller)) == NULL)
	      DAC960_WaitForCommand(Controller);
	    spin_unlock_irqrestore(&Controller->queue_lock, flags);
	    DAC960_V1_ClearCommand(Command);
	    Command->CommandType = DAC960_ImmediateCommand;
	    memcpy(&Command->V1.CommandMailbox, &UserCommand.CommandMailbox,
		   sizeof(DAC960_V1_CommandMailbox_T));
	    if (DataTransferBuffer != NULL)
	      Command->V1.CommandMailbox.Type3.BusAddress =
		DataTransferBufferDMA;
	  }
	DAC960_ExecuteCommand(Command);
	CommandStatus = Command->V1.CommandStatus;
	spin_lock_irqsave(&Controller->queue_lock, flags);
	DAC960_DeallocateCommand(Command);
	spin_unlock_irqrestore(&Controller->queue_lock, flags);
	if (DataTransferLength > 0)
	  {
	    if (copy_to_user(UserCommand.DataTransferBuffer,
			     DataTransferBuffer, DataTransferLength)) {
		ErrorCode = -EFAULT;
		goto Failure1;
            }
	  }
	if (CommandOpcode == DAC960_V1_DCDB)
	  {
	    /*
	      I don't believe Target or Channel in the DCDB_IOBUF
	      should be any different from the contents of DCDB.
	     */
	    Controller->V1.DirectCommandActive[DCDB.Channel]
					      [DCDB.TargetID] = false;
	    if (copy_to_user(UserCommand.DCDB, DCDB_IOBUF,
			     sizeof(DAC960_V1_DCDB_T))) {
		ErrorCode = -EFAULT;
		goto Failure1;
	    }
	  }
	ErrorCode = CommandStatus;
      Failure1:
	if (DataTransferBuffer != NULL)
	  pci_free_consistent(Controller->PCIDevice, abs(DataTransferLength),
			DataTransferBuffer, DataTransferBufferDMA);
	if (DCDB_IOBUF != NULL)
	  pci_free_consistent(Controller->PCIDevice, sizeof(DAC960_V1_DCDB_T),
			DCDB_IOBUF, DCDB_IOBUFDMA);
	out:
	return ErrorCode;
}

static long DAC960_gam_v2_execute_command(DAC960_V2_UserCommand_T __user *UserSpaceUserCommand)
{
	DAC960_V2_UserCommand_T UserCommand;
	DAC960_Controller_T *Controller;
	DAC960_Command_T *Command = NULL;
	DAC960_V2_CommandMailbox_T *CommandMailbox;
	DAC960_V2_CommandStatus_T CommandStatus;
	unsigned long flags;
	int ControllerNumber, DataTransferLength;
	int DataTransferResidue, RequestSenseLength;
	unsigned char *DataTransferBuffer = NULL;
	dma_addr_t DataTransferBufferDMA;
	unsigned char *RequestSenseBuffer = NULL;
	dma_addr_t RequestSenseBufferDMA;
	long ErrorCode = -EINVAL;

	if (UserSpaceUserCommand == NULL)
		goto out;
	if (copy_from_user(&UserCommand, UserSpaceUserCommand,
			   sizeof(DAC960_V2_UserCommand_T))) {
		ErrorCode = -EFAULT;
		goto out;
	}
	ErrorCode = -ENXIO;
	ControllerNumber = UserCommand.ControllerNumber;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
		goto out;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
		goto out;
	if (Controller->FirmwareType != DAC960_V2_Controller){
		ErrorCode = -EINVAL;
		goto out;
	}
	DataTransferLength = UserCommand.DataTransferLength;
    	ErrorCode = -ENOMEM;
	if (DataTransferLength > 0)
	  {
	    DataTransferBuffer = pci_zalloc_consistent(Controller->PCIDevice,
                                                       DataTransferLength,
                                                       &DataTransferBufferDMA);
	    if (DataTransferBuffer == NULL)
		goto out;
	  }
	else if (DataTransferLength < 0)
	  {
	    DataTransferBuffer = pci_alloc_consistent(Controller->PCIDevice,
				-DataTransferLength, &DataTransferBufferDMA);
	    if (DataTransferBuffer == NULL)
		goto out;
	    if (copy_from_user(DataTransferBuffer,
			       UserCommand.DataTransferBuffer,
			       -DataTransferLength)) {
		ErrorCode = -EFAULT;
		goto Failure2;
	    }
	  }
	RequestSenseLength = UserCommand.RequestSenseLength;
	if (RequestSenseLength > 0)
	  {
	    RequestSenseBuffer = pci_zalloc_consistent(Controller->PCIDevice,
                                                       RequestSenseLength,
                                                       &RequestSenseBufferDMA);
	    if (RequestSenseBuffer == NULL)
	      {
		ErrorCode = -ENOMEM;
		goto Failure2;
	      }
	  }
	spin_lock_irqsave(&Controller->queue_lock, flags);
	while ((Command = DAC960_AllocateCommand(Controller)) == NULL)
	  DAC960_WaitForCommand(Controller);
	spin_unlock_irqrestore(&Controller->queue_lock, flags);
	DAC960_V2_ClearCommand(Command);
	Command->CommandType = DAC960_ImmediateCommand;
	CommandMailbox = &Command->V2.CommandMailbox;
	memcpy(CommandMailbox, &UserCommand.CommandMailbox,
	       sizeof(DAC960_V2_CommandMailbox_T));
	CommandMailbox->Common.CommandControlBits
			      .AdditionalScatterGatherListMemory = false;
	CommandMailbox->Common.CommandControlBits
			      .NoAutoRequestSense = true;
	CommandMailbox->Common.DataTransferSize = 0;
	CommandMailbox->Common.DataTransferPageNumber = 0;
	memset(&CommandMailbox->Common.DataTransferMemoryAddress, 0,
	       sizeof(DAC960_V2_DataTransferMemoryAddress_T));
	if (DataTransferLength != 0)
	  {
	    if (DataTransferLength > 0)
	      {
		CommandMailbox->Common.CommandControlBits
				      .DataTransferControllerToHost = true;
		CommandMailbox->Common.DataTransferSize = DataTransferLength;
	      }
	    else
	      {
		CommandMailbox->Common.CommandControlBits
				      .DataTransferControllerToHost = false;
		CommandMailbox->Common.DataTransferSize = -DataTransferLength;
	      }
	    CommandMailbox->Common.DataTransferMemoryAddress
				  .ScatterGatherSegments[0]
				  .SegmentDataPointer = DataTransferBufferDMA;
	    CommandMailbox->Common.DataTransferMemoryAddress
				  .ScatterGatherSegments[0]
				  .SegmentByteCount =
	      CommandMailbox->Common.DataTransferSize;
	  }
	if (RequestSenseLength > 0)
	  {
	    CommandMailbox->Common.CommandControlBits
				  .NoAutoRequestSense = false;
	    CommandMailbox->Common.RequestSenseSize = RequestSenseLength;
	    CommandMailbox->Common.RequestSenseBusAddress =
	      						RequestSenseBufferDMA;
	  }
	DAC960_ExecuteCommand(Command);
	CommandStatus = Command->V2.CommandStatus;
	RequestSenseLength = Command->V2.RequestSenseLength;
	DataTransferResidue = Command->V2.DataTransferResidue;
	spin_lock_irqsave(&Controller->queue_lock, flags);
	DAC960_DeallocateCommand(Command);
	spin_unlock_irqrestore(&Controller->queue_lock, flags);
	if (RequestSenseLength > UserCommand.RequestSenseLength)
	  RequestSenseLength = UserCommand.RequestSenseLength;
	if (copy_to_user(&UserSpaceUserCommand->DataTransferLength,
				 &DataTransferResidue,
				 sizeof(DataTransferResidue))) {
		ErrorCode = -EFAULT;
		goto Failure2;
	}
	if (copy_to_user(&UserSpaceUserCommand->RequestSenseLength,
			 &RequestSenseLength, sizeof(RequestSenseLength))) {
		ErrorCode = -EFAULT;
		goto Failure2;
	}
	if (DataTransferLength > 0)
	  {
	    if (copy_to_user(UserCommand.DataTransferBuffer,
			     DataTransferBuffer, DataTransferLength)) {
		ErrorCode = -EFAULT;
		goto Failure2;
	    }
	  }
	if (RequestSenseLength > 0)
	  {
	    if (copy_to_user(UserCommand.RequestSenseBuffer,
			     RequestSenseBuffer, RequestSenseLength)) {
		ErrorCode = -EFAULT;
		goto Failure2;
	    }
	  }
	ErrorCode = CommandStatus;
      Failure2:
	  pci_free_consistent(Controller->PCIDevice, abs(DataTransferLength),
		DataTransferBuffer, DataTransferBufferDMA);
	if (RequestSenseBuffer != NULL)
	  pci_free_consistent(Controller->PCIDevice, RequestSenseLength,
		RequestSenseBuffer, RequestSenseBufferDMA);
out:
        return ErrorCode;
}

static long DAC960_gam_v2_get_health_status(DAC960_V2_GetHealthStatus_T __user *UserSpaceGetHealthStatus)
{
	DAC960_V2_GetHealthStatus_T GetHealthStatus;
	DAC960_V2_HealthStatusBuffer_T HealthStatusBuffer;
	DAC960_Controller_T *Controller;
	int ControllerNumber;
	long ErrorCode;

	if (UserSpaceGetHealthStatus == NULL) {
		ErrorCode = -EINVAL;
		goto out;
	}
	if (copy_from_user(&GetHealthStatus, UserSpaceGetHealthStatus,
			   sizeof(DAC960_V2_GetHealthStatus_T))) {
		ErrorCode = -EFAULT;
		goto out;
	}
	ErrorCode = -ENXIO;
	ControllerNumber = GetHealthStatus.ControllerNumber;
	if (ControllerNumber < 0 ||
	    ControllerNumber > DAC960_ControllerCount - 1)
		goto out;
	Controller = DAC960_Controllers[ControllerNumber];
	if (Controller == NULL)
		goto out;
	if (Controller->FirmwareType != DAC960_V2_Controller) {
		ErrorCode = -EINVAL;
		goto out;
	}
	if (copy_from_user(&HealthStatusBuffer,
			   GetHealthStatus.HealthStatusBuffer,
			   sizeof(DAC960_V2_HealthStatusBuffer_T))) {
		ErrorCode = -EFAULT;
		goto out;
	}
	ErrorCode = wait_event_interruptible_timeout(Controller->HealthStatusWaitQueue,
			!(Controller->V2.HealthStatusBuffer->StatusChangeCounter
			    == HealthStatusBuffer.StatusChangeCounter &&
			  Controller->V2.HealthStatusBuffer->NextEventSequenceNumber
			    == HealthStatusBuffer.NextEventSequenceNumber),
			DAC960_MonitoringTimerInterval);
	if (ErrorCode == -ERESTARTSYS) {
		ErrorCode = -EINTR;
		goto out;
	}
	if (copy_to_user(GetHealthStatus.HealthStatusBuffer,
			 Controller->V2.HealthStatusBuffer,
			 sizeof(DAC960_V2_HealthStatusBuffer_T)))
		ErrorCode = -EFAULT;
	else
		ErrorCode =  0;

out:
	return ErrorCode;
}

/*
 * DAC960_gam_ioctl is the ioctl function for performing RAID operations.
*/

static long DAC960_gam_ioctl(struct file *file, unsigned int Request,
						unsigned long Argument)
{
  long ErrorCode = 0;
  void __user *argp = (void __user *)Argument;
  if (!capable(CAP_SYS_ADMIN)) return -EACCES;

  mutex_lock(&DAC960_mutex);
  switch (Request)
    {
    case DAC960_IOCTL_GET_CONTROLLER_COUNT:
      ErrorCode = DAC960_ControllerCount;
      break;
    case DAC960_IOCTL_GET_CONTROLLER_INFO:
      ErrorCode = DAC960_gam_get_controller_info(argp);
      break;
    case DAC960_IOCTL_V1_EXECUTE_COMMAND:
      ErrorCode = DAC960_gam_v1_execute_command(argp);
      break;
    case DAC960_IOCTL_V2_EXECUTE_COMMAND:
      ErrorCode = DAC960_gam_v2_execute_command(argp);
      break;
    case DAC960_IOCTL_V2_GET_HEALTH_STATUS:
      ErrorCode = DAC960_gam_v2_get_health_status(argp);
      break;
      default:
	ErrorCode = -ENOTTY;
    }
  mutex_unlock(&DAC960_mutex);
  return ErrorCode;
}

static const struct file_operations DAC960_gam_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= DAC960_gam_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice DAC960_gam_dev = {
	DAC960_GAM_MINOR,
	"dac960_gam",
	&DAC960_gam_fops
};

static int DAC960_gam_init(void)
{
	int ret;

	ret = misc_register(&DAC960_gam_dev);
	if (ret)
		printk(KERN_ERR "DAC960_gam: can't misc_register on minor %d\n", DAC960_GAM_MINOR);
	return ret;
}

static void DAC960_gam_cleanup(void)
{
	misc_deregister(&DAC960_gam_dev);
}

#endif /* DAC960_GAM_MINOR */

static struct DAC960_privdata DAC960_GEM_privdata = {
	.HardwareType =		DAC960_GEM_Controller,
	.FirmwareType 	=	DAC960_V2_Controller,
	.InterruptHandler =	DAC960_GEM_InterruptHandler,
	.MemoryWindowSize =	DAC960_GEM_RegisterWindowSize,
};


static struct DAC960_privdata DAC960_BA_privdata = {
	.HardwareType =		DAC960_BA_Controller,
	.FirmwareType 	=	DAC960_V2_Controller,
	.InterruptHandler =	DAC960_BA_InterruptHandler,
	.MemoryWindowSize =	DAC960_BA_RegisterWindowSize,
};

static struct DAC960_privdata DAC960_LP_privdata = {
	.HardwareType =		DAC960_LP_Controller,
	.FirmwareType 	=	DAC960_V2_Controller,
	.InterruptHandler =	DAC960_LP_InterruptHandler,
	.MemoryWindowSize =	DAC960_LP_RegisterWindowSize,
};

static struct DAC960_privdata DAC960_LA_privdata = {
	.HardwareType =		DAC960_LA_Controller,
	.FirmwareType 	=	DAC960_V1_Controller,
	.InterruptHandler =	DAC960_LA_InterruptHandler,
	.MemoryWindowSize =	DAC960_LA_RegisterWindowSize,
};

static struct DAC960_privdata DAC960_PG_privdata = {
	.HardwareType =		DAC960_PG_Controller,
	.FirmwareType 	=	DAC960_V1_Controller,
	.InterruptHandler =	DAC960_PG_InterruptHandler,
	.MemoryWindowSize =	DAC960_PG_RegisterWindowSize,
};

static struct DAC960_privdata DAC960_PD_privdata = {
	.HardwareType =		DAC960_PD_Controller,
	.FirmwareType 	=	DAC960_V1_Controller,
	.InterruptHandler =	DAC960_PD_InterruptHandler,
	.MemoryWindowSize =	DAC960_PD_RegisterWindowSize,
};

static struct DAC960_privdata DAC960_P_privdata = {
	.HardwareType =		DAC960_P_Controller,
	.FirmwareType 	=	DAC960_V1_Controller,
	.InterruptHandler =	DAC960_P_InterruptHandler,
	.MemoryWindowSize =	DAC960_PD_RegisterWindowSize,
};

static const struct pci_device_id DAC960_id_table[] = {
	{
		.vendor 	= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_GEM,
		.subvendor	= PCI_VENDOR_ID_MYLEX,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_GEM_privdata,
	},
	{
		.vendor 	= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_BA,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_BA_privdata,
	},
	{
		.vendor 	= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_LP,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_LP_privdata,
	},
	{
		.vendor 	= PCI_VENDOR_ID_DEC,
		.device		= PCI_DEVICE_ID_DEC_21285,
		.subvendor	= PCI_VENDOR_ID_MYLEX,
		.subdevice	= PCI_DEVICE_ID_MYLEX_DAC960_LA,
		.driver_data	= (unsigned long) &DAC960_LA_privdata,
	},
	{
		.vendor 	= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_PG,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_PG_privdata,
	},
	{
		.vendor 	= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_PD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_PD_privdata,
	},
	{
		.vendor 	= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_P,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_P_privdata,
	},
	{0, },
};

MODULE_DEVICE_TABLE(pci, DAC960_id_table);

static struct pci_driver DAC960_pci_driver = {
	.name		= "DAC960",
	.id_table	= DAC960_id_table,
	.probe		= DAC960_Probe,
	.remove		= DAC960_Remove,
};

static int __init DAC960_init_module(void)
{
	int ret;

	ret =  pci_register_driver(&DAC960_pci_driver);
#ifdef DAC960_GAM_MINOR
	if (!ret)
		DAC960_gam_init();
#endif
	return ret;
}

static void __exit DAC960_cleanup_module(void)
{
	int i;

#ifdef DAC960_GAM_MINOR
	DAC960_gam_cleanup();
#endif

	for (i = 0; i < DAC960_ControllerCount; i++) {
		DAC960_Controller_T *Controller = DAC960_Controllers[i];
		if (Controller == NULL)
			continue;
		DAC960_FinalizeController(Controller);
	}
	if (DAC960_ProcDirectoryEntry != NULL) {
  		remove_proc_entry("rd/status", NULL);
  		remove_proc_entry("rd", NULL);
	}
	DAC960_ControllerCount = 0;
	pci_unregister_driver(&DAC960_pci_driver);
}

module_init(DAC960_init_module);
module_exit(DAC960_cleanup_module);

MODULE_LICENSE("GPL");
