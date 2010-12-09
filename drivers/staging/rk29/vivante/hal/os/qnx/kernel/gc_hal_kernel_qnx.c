/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/8/2010. Do not edit!!!
*
*****************************************************************************/




#include "gc_hal_kernel_qnx.h"

#define _GC_OBJ_ZONE	gcvZONE_KERNEL

/******************************************************************************\
******************************* gckKERNEL API Code ******************************
\******************************************************************************/

/*******************************************************************************
**
**	gckKERNEL_QueryVideoMemory
**
**	Query the amount of video memory.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**	OUTPUT:
**
**		gcsHAL_INTERFACE * Interface
**			Pointer to an gcsHAL_INTERFACE structure that will be filled in with
**			the memory information.
*/
gceSTATUS
gckKERNEL_QueryVideoMemory(
	IN gckKERNEL Kernel,
	OUT gcsHAL_INTERFACE * Interface
	)
{
	gckGALDEVICE device;

	gcmkHEADER_ARG("Kernel=0x%x", Kernel);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(Interface != NULL);

	/* Extract the pointer to the gckGALDEVICE class. */
	device = (gckGALDEVICE) Kernel->context;

	/* Get internal memory size and physical address. */
	Interface->u.QueryVideoMemory.internalSize = device->internalSize;
	Interface->u.QueryVideoMemory.internalPhysical = device->internalPhysical;

	/* Get external memory size and physical address. */
	Interface->u.QueryVideoMemory.externalSize = device->externalSize;
	Interface->u.QueryVideoMemory.externalPhysical = device->externalPhysical;

	/* Get contiguous memory size and physical address. */
	Interface->u.QueryVideoMemory.contiguousSize = device->contiguousSize;
	Interface->u.QueryVideoMemory.contiguousPhysical = device->contiguousPhysical;

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckKERNEL_GetVideoMemoryPool
**
**	Get the gckVIDMEM object belonging to the specified pool.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**		gcePOOL Pool
**			Pool to query gckVIDMEM object for.
**
**	OUTPUT:
**
**		gckVIDMEM * VideoMemory
**			Pointer to a variable that will hold the pointer to the gckVIDMEM
**			object belonging to the requested pool.
*/
gceSTATUS
gckKERNEL_GetVideoMemoryPool(
	IN gckKERNEL Kernel,
	IN gcePOOL Pool,
	OUT gckVIDMEM * VideoMemory
	)
{
	gckGALDEVICE device;
	gckVIDMEM videoMemory;

	gcmkHEADER_ARG("Kernel=0x%x Pool=0x%x", Kernel, Pool);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(VideoMemory != NULL);

    /* Extract the pointer to the gckGALDEVICE class. */
    device = (gckGALDEVICE) Kernel->context;

	/* Dispatch on pool. */
	switch (Pool)
	{
	case gcvPOOL_LOCAL_INTERNAL:
		/* Internal memory. */
		videoMemory = device->internalVidMem;
		break;

	case gcvPOOL_LOCAL_EXTERNAL:
		/* External memory. */
		videoMemory = device->externalVidMem;
		break;

	case gcvPOOL_SYSTEM:
		/* System memory. */
		videoMemory = device->contiguousVidMem;
		break;

	default:
		/* Unknown pool. */
		videoMemory = NULL;
	}

	/* Return pointer to the gckVIDMEM object. */
	*VideoMemory = videoMemory;

	/* Return status. */
	gcmkFOOTER_ARG("*VideoMemory=0x%x", *VideoMemory);
	return (videoMemory == NULL) ? gcvSTATUS_OUT_OF_MEMORY : gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckKERNEL_MapMemory
**
**	Map video memory into the current process space.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**		gctPHYS_ADDR Physical
**			Physical address of video memory to map.
**
**		gctSIZE_T Bytes
**			Number of bytes to map.
**
**	OUTPUT:
**
**		gctPOINTER * Logical
**			Pointer to a variable that will hold the base address of the mapped
**			memory region.
*/
gceSTATUS
gckKERNEL_MapMemory(
	IN gckKERNEL Kernel,
	IN gctPHYS_ADDR Physical,
	IN gctSIZE_T Bytes,
	OUT gctPOINTER * Logical
	)
{
	return gckOS_MapMemory(Kernel->os, Physical, Bytes, Logical);
}

/*******************************************************************************
**
**	gckKERNEL_UnmapMemory
**
**	Unmap video memory from the current process space.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**		gctPHYS_ADDR Physical
**			Physical address of video memory to map.
**
**		gctSIZE_T Bytes
**			Number of bytes to map.
**
**		gctPOINTER Logical
**			Base address of the mapped memory region.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckKERNEL_UnmapMemory(
	IN gckKERNEL Kernel,
	IN gctPHYS_ADDR Physical,
	IN gctSIZE_T Bytes,
	IN gctPOINTER Logical
	)
{
	return gckOS_UnmapMemory(Kernel->os, Physical, Bytes, Logical);
}

/*******************************************************************************
**
**	gckKERNEL_MapVideoMemory
**
**	Map video memory for the current process.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**      gctBOOL InUserSpace
**          gcvTRUE to map the memory into the user space.
**
**		gctUINT32 Address
**			Hardware specific memory address.
**
**		gctUINT32 Pid
**			Process ID of the current process.
**
**		gctUINT32 Bytes
**			Number of bytes to map.
**
**	OUTPUT:
**
**		gctPOINTER * Logical
**			Pointer to a variable that will hold the logical address of the
**			specified memory address.
*/
gceSTATUS
gckKERNEL_MapVideoMemory(
	IN gckKERNEL Kernel,
	IN gctBOOL InUserSpace,
	IN gctUINT32 Address,
	IN gctUINT32 Pid,
	IN gctUINT32 Bytes,
	OUT gctPOINTER * Logical
	)
{
    off64_t offset = (off64_t)Address - (off64_t)drv_mempool_get_basePAddress();

    gcmkHEADER_ARG("Kernel=0x%x InUserSpace=%d Address=%08x",
    			   Kernel, InUserSpace, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    *Logical = (gctPOINTER)mmap64_peer(Pid, gcvNULL, Bytes,
    		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NOINIT,
    		drv_mempool_get_fileDescriptor(), offset);
    if (*Logical == MAP_FAILED) {
        *Logical = NULL;
        return gcvSTATUS_INVALID_ADDRESS;
    }

    /* Success. */
	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckKERNEL_UnmapVideoMemory
**
**	Unmap video memory for the current process.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**		gctUINT32 Address
**			Hardware specific memory address.
**
**		gctUINT32 Pid
**			Process ID of the current process.
**
**		gctUINT32 Bytes
**			Number of bytes to map.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckKERNEL_UnmapVideoMemory(
	IN gckKERNEL Kernel,
	IN gctPOINTER Logical,
	IN gctUINT32 Pid,
	IN gctUINT32 Bytes
	)
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    if (munmap_peer(Pid, Logical, Bytes) == -1)
    {
    	return gcvSTATUS_INVALID_ADDRESS;
    }

    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckKERNEL_Notify
**
**	This function is called by clients to notify the gckKERNEL object of an event.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**		gceNOTIFY Notification
**			Notification event.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckKERNEL_Notify(
	IN gckKERNEL Kernel,
	IN gceNOTIFY Notification,
	IN gctBOOL Data
	)
{
	gceSTATUS status;

	gcmkHEADER_ARG("Kernel=0x%x Notification=%d Data=%d",
				   Kernel, Notification, Data);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

	/* Dispatch on notifcation. */
	switch (Notification)
	{
	case gcvNOTIFY_INTERRUPT:
		/* Process the interrupt. */
#if COMMAND_PROCESSOR_VERSION > 1
		status = gckINTERRUPT_Notify(Kernel->interrupt, Data);
#else
		status = gckHARDWARE_Interrupt(Kernel->hardware, Data);
#endif
		break;

	default:
		status = gcvSTATUS_OK;
		break;
	}

	/* Success. */
	gcmkFOOTER();
	return status;
}

gceSTATUS
gckKERNEL_QuerySettings(
	IN gckKERNEL Kernel,
	OUT gcsKERNEL_SETTINGS * Settings
	)
{
	gckGALDEVICE device;

	gcmkHEADER_ARG("Kernel=0x%x", Kernel);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(Settings != gcvNULL);

	/* Extract the pointer to the gckGALDEVICE class. */
    device = (gckGALDEVICE) Kernel->context;

	/* Fill in signal. */
	Settings->signal = -1;

	/* Success. */
	gcmkFOOTER_ARG("Settings->signal=%d", Settings->signal);
	return gcvSTATUS_OK;
}


