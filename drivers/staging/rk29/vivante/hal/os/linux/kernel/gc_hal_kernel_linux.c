/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#include "gc_hal_kernel_linux.h"

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

	gcmkHEADER_ARG("Kernel=%p", Kernel);

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

	gcmkHEADER_ARG("Kernel=%p Pool=%d", Kernel, Pool);

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
	gcmkFOOTER_ARG("*VideoMemory=%p", *VideoMemory);
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
**	Get the logical address for a hardware specific memory address for the
**	current process.
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
	OUT gctPOINTER * Logical
	)
{
    gckGALDEVICE device;
    PLINUX_MDL mdl;
	PLINUX_MDL_MAP mdlMap;
    gcePOOL pool;
    gctUINT32 offset, base;
    gceSTATUS status;
    gctPOINTER logical;

    gcmkHEADER_ARG("Kernel=%p InUserSpace=%d Address=%08x",
    			   Kernel, InUserSpace, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    /* Extract the pointer to the gckGALDEVICE class. */
    device = (gckGALDEVICE) Kernel->context;

    /* Split the memory address into a pool type and offset. */
    gcmkONERROR(
    	gckHARDWARE_SplitMemory(Kernel->hardware, Address, &pool, &offset));

    /* Dispatch on pool. */
    switch (pool)
    {
    case gcvPOOL_LOCAL_INTERNAL:
    	/* Internal memory. */
    	logical = device->internalLogical;
    	break;

    case gcvPOOL_LOCAL_EXTERNAL:
    	/* External memory. */
    	logical = device->externalLogical;
    	break;

    case gcvPOOL_SYSTEM:
		/* System memory. */
		if (device->contiguousMapped)
		{
			logical = device->contiguousBase;
		}
		else
		{
			mdl = (PLINUX_MDL) device->contiguousPhysical;

			mdlMap = FindMdlMap(mdl, current->tgid);
			gcmkASSERT(mdlMap);

			logical = (gctPOINTER) mdlMap->vmaAddr;
		}

		gcmkVERIFY_OK(
			gckHARDWARE_SplitMemory(Kernel->hardware,
									device->contiguousVidMem->baseAddress,
									&pool,
									&base));

		offset -= base;
		break;

    default:
    	/* Invalid memory pool. */
    	gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Build logical address of specified address. */
    *Logical = (gctPOINTER) ((gctUINT8_PTR) logical + offset);

    /* Success. */
    gcmkFOOTER_ARG("*Logical=%p", *Logical);
    return gcvSTATUS_OK;

OnError:
	/* Retunn the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckKERNEL_Notify
**
**	This function iscalled by clients to notify the gckKERNRL object of an event.
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

	gcmkHEADER_ARG("Kernel=%p Notification=%d Data=%d",
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

	gcmkHEADER_ARG("Kernel=%p", Kernel);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(Settings != gcvNULL);

	/* Extract the pointer to the gckGALDEVICE class. */
    device = (gckGALDEVICE) Kernel->context;

	/* Fill in signal. */
	Settings->signal = device->signal;

	/* Success. */
	gcmkFOOTER_ARG("Settings->signal=%d", Settings->signal);
	return gcvSTATUS_OK;
}

