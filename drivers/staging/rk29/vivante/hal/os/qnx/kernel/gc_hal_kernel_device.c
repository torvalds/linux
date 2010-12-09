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
#include <sys/mman.h>
#include <sys/siginfo.h>

#define _GC_OBJ_ZONE	gcvZONE_DEVICE

/******************************************************************************\
******************************** gckGALDEVICE Code *******************************
\******************************************************************************/

gceSTATUS
gckGALDEVICE_AllocateMemory(
	IN gckGALDEVICE Device,
	IN gctSIZE_T Bytes,
    OUT gctPOINTER *Logical,
    OUT gctPHYS_ADDR *Physical,
    OUT gctUINT32 *PhysAddr
	)
{
	gceSTATUS status;

	gcmkVERIFY_ARGUMENT(Device != NULL);
	gcmkVERIFY_ARGUMENT(Logical != NULL);
	gcmkVERIFY_ARGUMENT(Physical != NULL);
	gcmkVERIFY_ARGUMENT(PhysAddr != NULL);

	status = gckOS_AllocateContiguous(Device->os,
					  gcvFALSE,
					  &Bytes,
					  Physical,
					  Logical);

	if (gcmIS_ERROR(status))
	{
		gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    				   "gckGALDEVICE_AllocateMemory: error status->0x%x",
    				   status);

		return status;
	}

	*PhysAddr = (gctUINT32)(*(off_t*) Physical) - Device->baseAddress;
	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    			   "gckGALDEVICE_AllocateMemory: phys_addr->0x%x phsical->0x%x Logical->0x%x",
               	   (gctUINT32)*Physical,
				   (gctUINT32)*PhysAddr,
				   (gctUINT32)*Logical);

    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckGALDEVICE_FreeMemory(
	IN gckGALDEVICE Device,
	IN gctPOINTER Logical,
	IN gctPHYS_ADDR Physical)
{
	gcmkVERIFY_ARGUMENT(Device != NULL);

    return gckOS_FreeContiguous(Device->os,
								Physical,
								Logical,
								0);
}

/* TODO. fix global sigevent to be part of device. */
struct sigevent irqEvent;

const struct sigevent* isrRoutine(void* arg, int id)
{
    gckGALDEVICE device = (gckGALDEVICE)arg;

	/* Call kernel interrupt notification. */
    if (gckKERNEL_Notify(device->kernel,
						 gcvNOTIFY_INTERRUPT,
						 gcvTRUE) == gcvSTATUS_OK)
    {
		InterruptUnmask(device->irqLine, device->irqId);

		return &irqEvent;
    }

    return gcvNULL;
}

static void* threadRoutine(void *ctxt)
{
    gckGALDEVICE device = (gckGALDEVICE) ctxt;

	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
				"Starting ISR Thread with irq:%d\n",
				device->irqLine);

	SIGEV_INTR_INIT(&irqEvent);

	/* Obtain I/O privileges */
	ThreadCtl( _NTO_TCTL_IO, 0 );

	device->irqId = InterruptAttach(device->irqLine,
									isrRoutine,
									(void*)device,
									gcmSIZEOF(struct _gckGALDEVICE),
									0);

	gcmkTRACE_ZONE(gcvLEVEL_INFO,
				gcvZONE_DRIVER,
				"irqId:%d\n",
				device->irqId);

	if (device->irqId < 0) {
		gcmkTRACE_ZONE(gcvLEVEL_INFO,
				gcvZONE_DRIVER,
            	"[galcore] gckGALDEVICE_Setup_ISR: "
				"Could not register irq line->%d\n",
				device->irqLine);

		device->isrInitialized = gcvFALSE;

		return (void *)1;
	}

	printf("[Interrupt] Attached irqLine %d with id %d.\n",
		device->irqLine, device->irqId);

	device->isrInitialized = gcvTRUE;

	while (1)
	{
		if (InterruptWait(0, NULL) == -1)
		{
			printf("[Interrupt] IST exiting\n");
			/* Either something is wrong or the thread got canceled */
			InterruptUnmask(device->irqLine, device->irqId);
			pthread_exit(NULL);
		}

		gckKERNEL_Notify(device->kernel, gcvNOTIFY_INTERRUPT, gcvFALSE);
    }

    return (void *)0;
}

/*******************************************************************************
**
**	gckGALDEVICE_Setup_ISR
**
**	Start the ISR routine.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		gcvSTATUS_OK
**			Setup successfully.
**		gcvSTATUS_GENERIC_IO
**			Setup failed.
*/
gceSTATUS
gckGALDEVICE_Setup_ISR(
	IN gckGALDEVICE Device
	)
{
	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Release_ISR
**
**	Release the irq line.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Release_ISR(
	IN gckGALDEVICE Device
	)
{
	gcmkVERIFY_ARGUMENT(Device != NULL);

	return gcvSTATUS_OK;
}


int
gsl_free_interrupts()
{

	return 0;
}
/*******************************************************************************
**
**	gckGALDEVICE_Start_Thread
**
**	Start the daemon thread.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		gcvSTATUS_OK
**			Start successfully.
**		gcvSTATUS_GENERIC_IO
**			Start failed.
*/
gceSTATUS
gckGALDEVICE_Start_Thread(
	IN gckGALDEVICE Device
	)
{
	pthread_attr_t attr;
	struct sched_param sched;
	gctINT ret;
	gcmkVERIFY_ARGUMENT(Device != NULL);

	gcmkTRACE_ZONE(gcvLEVEL_INFO,
				  gcvZONE_DRIVER,
				  "[galcore] gckGALDEVICE_Start_Thread: Creating threadRoutine\n");

	pthread_attr_init(&attr);
	pthread_attr_getschedparam(&attr, &sched);
	sched.sched_priority += 10;
	pthread_attr_setschedparam(&attr, &sched);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

	/* Start the interrupt service thread */
	if ((ret = pthread_create(&Device->threadCtxt, &attr, threadRoutine, Device)) != 0)
	{
		gcmkTRACE_ZONE(gcvLEVEL_ERROR,
					  gcvZONE_DRIVER,
					  "[galcore] gckGALDEVICE_Start_Thread: Failed with code %d\n",
					  ret);

		return gcvSTATUS_OUT_OF_RESOURCES;
	}

	pthread_setname_np(Device->threadCtxt, "galcore-IST");

	Device->threadInitialized = gcvTRUE;
	gcmkTRACE_ZONE(gcvLEVEL_INFO,
					gcvZONE_DRIVER,
					"[galcore] gckGALDEVICE_Start_Thread: "
					"Start the daemon thread.\n");

	return gcvSTATUS_OK;
}


/*******************************************************************************
**
**	gckGALDEVICE_Stop_Thread
**
**	Stop the gal device, including the following actions: stop the daemon
**	thread, release the irq.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Stop_Thread(
	gckGALDEVICE Device
	)
{
	gcmkVERIFY_ARGUMENT(Device != NULL);

	/* stop the thread */
	if (Device->threadInitialized)
	{
		InterruptDetach(Device->irqId);
		Device->irqId = 0;
		ConnectDetach(Device->coid);
		Device->coid = 0;
		ChannelDestroy(Device->chid);
		Device->chid = 0;

		pthread_cancel(Device->threadCtxt);
		pthread_join(Device->threadCtxt, NULL);
		Device->threadCtxt = 0;

		Device->threadInitialized = gcvFALSE;
		Device->isrInitialized = gcvFALSE;
	}

	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Start
**
**	Start the gal device, including the following actions: setup the isr routine
**  and start the daemon thread.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		gcvSTATUS_OK
**			Start successfully.
*/
gceSTATUS
gckGALDEVICE_Start(
	IN gckGALDEVICE Device
	)
{
	gceSTATUS ret;
	/* Start the daemon thread. */
	gcmkVERIFY_OK((ret = gckGALDEVICE_Start_Thread(Device)));

	return ret;
}

/*******************************************************************************
**
**	gckGALDEVICE_Stop
**
**	Stop the gal device, including the following actions: stop the daemon
**	thread, release the irq.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Stop(
    gckGALDEVICE Device
    )
{
    gcmkVERIFY_ARGUMENT(Device != NULL);

    if (Device->threadInitialized)
    {
    	gckGALDEVICE_Stop_Thread(Device);
    }

    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckGALDEVICE_Construct
**
**  Constructor.
**
**  INPUT:
**
**  OUTPUT:
**
**  	gckGALDEVICE * Device
**  	    Pointer to a variable receiving the gckGALDEVICE object pointer on
**  	    success.
*/
gceSTATUS
gckGALDEVICE_Construct(
    IN gctINT IrqLine,
    IN gctUINT32 RegisterMemBase,
    IN gctSIZE_T RegisterMemSize,
    IN gctUINT32 ContiguousBase,
    IN gctSIZE_T ContiguousSize,
    IN gctSIZE_T BankSize,
    IN gctINT FastClear,
	IN gctINT Compression,
	IN gctUINT32 BaseAddress,
    OUT gckGALDEVICE *Device
    )
{
    gctUINT32 internalBaseAddress, internalAlignment;
    gctUINT32 externalBaseAddress, externalAlignment;
    gctUINT32 horizontalTileSize, verticalTileSize;
    gctUINT32 physAddr;
    gctUINT32 physical;
    gckGALDEVICE device;
    gceSTATUS status;

    gcmkTRACE(gcvLEVEL_VERBOSE, "[galcore] Enter gckGALDEVICE_Construct\n");

    /* Allocate device structure. */
    device = calloc(1, sizeof(struct _gckGALDEVICE));
    if (!device)
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] gckGALDEVICE_Construct: Can't allocate memory.\n");

    	return gcvSTATUS_OUT_OF_MEMORY;
    }

    physical = RegisterMemBase;

    /* Set up register memory region */
    if (physical != 0)
    {
    	/* Request a region. */
		device->registerBase = (gctPOINTER)mmap_device_io(RegisterMemSize, RegisterMemBase);

        if ((uintptr_t)device->registerBase == MAP_DEVICE_FAILED)
		{
    	    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
	    	    	  "[galcore] gckGALDEVICE_Construct: Unable to map location->0x%lX for size->%ld\n",
					  RegisterMemBase,
					  RegisterMemSize);

    	    return gcvSTATUS_OUT_OF_RESOURCES;
        }

    	physical += RegisterMemSize;

		gcmkTRACE_ZONE(gcvLEVEL_INFO,
					gcvZONE_DRIVER,
        			"[galcore] gckGALDEVICE_Construct: "
					"RegisterBase after mapping Address->0x%x is 0x%x\n",
               		(gctUINT32)RegisterMemBase,
					(gctUINT32)device->registerBase);
    }

	/* construct the gckOS object */
	device->baseAddress = BaseAddress;
	gcmkONERROR(
			gckOS_Construct(device, &device->os));

    /* construct the gckKERNEL object. */
	gcmkONERROR(
			gckKERNEL_Construct(device->os, device, &device->kernel));

	gcmkONERROR(
			gckHARDWARE_SetFastClear(device->kernel->hardware,
    					  				  FastClear,
										  Compression));

    /* query the ceiling of the system memory */
	gcmkONERROR(
			gckHARDWARE_QuerySystemMemory(device->kernel->hardware,
					&device->systemMemorySize,
                    &device->systemMemoryBaseAddress));

	gcmkTRACE_ZONE(gcvLEVEL_INFO,
					gcvZONE_DRIVER,
					"[galcore] gckGALDEVICE_Construct: "
    				"Will be trying to allocate contiguous memory of 0x%x bytes\n",
           			(gctUINT32)device->systemMemoryBaseAddress);

#if COMMAND_PROCESSOR_VERSION == 1
    /* start the command queue */
    gcmkVERIFY_OK(gckCOMMAND_Start(device->kernel->command));
#endif

    /* initialize the thread daemon */
	memset(&device->isrLock, 0, sizeof(device->isrLock));

	device->threadInitialized = gcvFALSE;
	device->killThread = gcvFALSE;

	/* initialize the isr */
	device->isrInitialized = gcvFALSE;
	device->dataReady = gcvFALSE;
	device->irqLine = IrqLine;

	/* query the amount of video memory */
    gcmkVERIFY_OK(gckHARDWARE_QueryMemory(device->kernel->hardware,
                    &device->internalSize,
                    &internalBaseAddress,
                    &internalAlignment,
                    &device->externalSize,
                    &externalBaseAddress,
                    &externalAlignment,
                    &horizontalTileSize,
                    &verticalTileSize));

	/* set up the internal memory region */
    if (device->internalSize > 0)
    {
        gceSTATUS status = gckVIDMEM_Construct(device->os,
                    internalBaseAddress,
                    device->internalSize,
                    internalAlignment,
                    0,
                    &device->internalVidMem);

        if (gcmIS_ERROR(status))
        {
            /* error, remove internal heap */
            device->internalSize = 0;
        }
        else
        {
            /* map internal memory */
            device->internalPhysical  = (gctPHYS_ADDR)physical;
			device->internalLogical = (gctPOINTER)mmap_device_io(device->internalSize, physical);

            gcmkASSERT(device->internalLogical != NULL);

			physical += device->internalSize;
        }
    }

    if (device->externalSize > 0)
    {
        /* create the external memory heap */
        gceSTATUS status = gckVIDMEM_Construct(device->os,
					externalBaseAddress,
					device->externalSize,
					externalAlignment,
					0,
					&device->externalVidMem);

        if (gcmIS_ERROR(status))
        {
            /* error, remove internal heap */
            device->externalSize = 0;
        }
        else
        {
            /* map internal memory */
            device->externalPhysical = (gctPHYS_ADDR)physical;
			device->externalLogical = (gctPOINTER)mmap_device_io(device->externalSize, physical);

			gcmkASSERT(device->externalLogical != NULL);

			physical += device->externalSize;
        }
    }

	/* set up the contiguous memory */
    device->contiguousSize = ContiguousSize;

	if (ContiguousBase == 0)
	{
		status = gcvSTATUS_OUT_OF_MEMORY;

		while (device->contiguousSize > 0)
		{
			gcmkTRACE_ZONE(
				gcvLEVEL_INFO, gcvZONE_DRIVER,
				"[galcore] gckGALDEVICE_Construct: Will be trying to allocate contiguous memory of %ld bytes\n",
				device->contiguousSize
				);

			/* allocate contiguous memory */
			status = gckGALDEVICE_AllocateMemory(
				device,
				device->contiguousSize,
				&device->contiguousBase,
				&device->contiguousPhysical,
				&physAddr
				);

			if (gcmIS_SUCCESS(status))
			{
	    		gcmkTRACE_ZONE(
					gcvLEVEL_INFO, gcvZONE_DRIVER,
					"[galcore] gckGALDEVICE_Construct: Contiguous allocated size->0x%08X Virt->0x%08lX physAddr->0x%08X\n",
					device->contiguousSize,
					device->contiguousBase,
					physAddr
					);

				status = gckVIDMEM_Construct(
					device->os,
					physAddr | device->systemMemoryBaseAddress,
					device->contiguousSize,
					64,
					BankSize,
					&device->contiguousVidMem
					);

				if (gcmIS_SUCCESS(status))
				{
					device->contiguousMapped = gcvFALSE;

					/* success, abort loop */
					gcmkTRACE_ZONE(
						gcvLEVEL_INFO, gcvZONE_DRIVER,
						"Using %u bytes of contiguous memory.\n",
						device->contiguousSize
						);

					break;
				}

				gcmkVERIFY_OK(gckGALDEVICE_FreeMemory(
					device,
					device->contiguousBase,
					device->contiguousPhysical
					));

				device->contiguousBase = NULL;
			}

			device->contiguousSize -= (4 << 20);
		}
	}
	else
	{
		/* Create the contiguous memory heap. */
		status = gckVIDMEM_Construct(
			device->os,
			(ContiguousBase - device->baseAddress) | device->systemMemoryBaseAddress,
			ContiguousSize,
			64,
			BankSize,
			&device->contiguousVidMem
			);

		if (gcmIS_ERROR(status))
		{
			/* Error, roll back. */
			device->contiguousVidMem = gcvNULL;
			device->contiguousSize   = 0;
		}
		else
		{
			/* Map the contiguous memory. */
			device->contiguousPhysical = (gctPHYS_ADDR) ContiguousBase;
			device->contiguousSize     = ContiguousSize;
			device->contiguousBase	   = (gctPOINTER) mmap_device_io(ContiguousSize, ContiguousBase);
			device->contiguousMapped   = gcvTRUE;

			if (device->contiguousBase == gcvNULL)
			{
    			/* Error, roll back. */
				gcmkVERIFY_OK(gckVIDMEM_Destroy(device->contiguousVidMem));
				device->contiguousVidMem = gcvNULL;
				device->contiguousSize   = 0;

				status = gcvSTATUS_OUT_OF_RESOURCES;
			}
		}
	}

    *Device = device;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] gckGALDEVICE_Construct: Initialized device->0x%x contiguous->%lu @ 0x%x (0x%08X)\n",
				  device,
				  device->contiguousSize,
				  device->contiguousBase,
				  device->contiguousPhysical);

    return gcvSTATUS_OK;
OnError:
	/* Roll back. */

	/* Destroy the gckKERNEL object. */
	if ( Device != gcvNULL)
	{
		gcmkVERIFY_OK(gckGALDEVICE_Destroy(*Device));
	}

	/* Return the status. */
	return status;
}

/*******************************************************************************
**
**	gckGALDEVICE_Destroy
**
**	Class destructor.
**
**	INPUT:
**
**		Nothing.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Destroy(
	gckGALDEVICE Device)
{
	gcmkVERIFY_ARGUMENT(Device != NULL);

    gcmkTRACE(gcvLEVEL_VERBOSE, "[ENTER] gckGALDEVICE_Destroy\n");

    /* Destroy the gckKERNEL object. */
    gcmkVERIFY_OK(gckKERNEL_Destroy(Device->kernel));

    if (Device->internalVidMem != gcvNULL)
    {
        /* destroy the internal heap */
        gcmkVERIFY_OK(gckVIDMEM_Destroy(Device->internalVidMem));

		/* unmap the internal memory */
		munmap_device_io((uintptr_t)Device->internalLogical, Device->internalSize);
    }

    if (Device->externalVidMem != gcvNULL)
    {
        /* destroy the internal heap */
        gcmkVERIFY_OK(gckVIDMEM_Destroy(Device->externalVidMem));

        /* unmap the external memory */
		munmap_device_io((uintptr_t)Device->externalLogical, Device->externalSize);
    }

    if (Device->contiguousVidMem != gcvNULL)
    {
        /* Destroy the contiguous heap */
        gcmkVERIFY_OK(gckVIDMEM_Destroy(Device->contiguousVidMem));

		if (Device->contiguousMapped)
		{
			gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
				  "[galcore] gckGALDEVICE_Destroy: "
			  "Unmapping contiguous memory->0x%08lX\n",
			  Device->contiguousBase);

			munmap_device_io((uintptr_t)Device->contiguousBase, Device->contiguousSize);
		}
		else
		{
    	    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
	        	  "[galcore] gckGALDEVICE_Destroy: "
			  "Freeing contiguous memory->0x%08lX\n",
			  Device->contiguousBase);

    	    gcmkVERIFY_OK(gckGALDEVICE_FreeMemory(Device,
						 Device->contiguousBase,
						 Device->contiguousPhysical));
    	}
    }

    if (Device->registerBase)
    {
		munmap_device_io((uintptr_t)Device->registerBase, Device->registerSize);
    }

    /* Destroy the gckOS object. */
    gcmkVERIFY_OK(gckOS_Destroy(Device->os));

    free(Device);

    gcmkTRACE(gcvLEVEL_VERBOSE, "[galcore] Leave gckGALDEVICE_Destroy\n");

    return gcvSTATUS_OK;
}

