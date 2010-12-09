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




#ifndef __gc_hal_kernel_device_h_
#define __gc_hal_kernel_device_h_

/******************************************************************************\
******************************* gckGALDEVICE Structure *******************************
\******************************************************************************/

#define GALCORE_INTERRUPT_PULSE 0x5D

typedef struct _gckGALDEVICE
{
	/* Objects. */
	gckOS				os;
	gckKERNEL			kernel;

	/* Attributes. */
	gctSIZE_T			internalSize;
	gctPHYS_ADDR		internalPhysical;
	gctPOINTER			internalLogical;
	gckVIDMEM			internalVidMem;
	gctSIZE_T			externalSize;
	gctPHYS_ADDR		externalPhysical;
	gctPOINTER			externalLogical;
	gckVIDMEM			externalVidMem;
	gckVIDMEM			contiguousVidMem;
	gctPOINTER			contiguousBase;
	gctPHYS_ADDR		contiguousPhysical;
	gctSIZE_T			contiguousSize;
	gctBOOL				contiguousMapped;
	gctPOINTER			contiguousMappedUser;
	gctSIZE_T			systemMemorySize;
	gctUINT32			systemMemoryBaseAddress;
	gctPOINTER			registerBase;
	gctSIZE_T			registerSize;
	gctUINT32			baseAddress;

	/* IRQ management. */
	gctINT				irqLine;
	gctINT				irqId;
	gctBOOL				isrInitialized;
	gctBOOL				dataReady;
	intrspin_t			isrLock;
	struct sigevent		event;
	int					chid;
	int					coid;

	/* Thread management. */
	pthread_t			threadCtxt;
	gctBOOL				threadInitialized;
	gctBOOL				killThread;
}
* gckGALDEVICE;

typedef struct _gcsHAL_PRIVATE_DATA
{
    gckGALDEVICE		device;
    gctPOINTER			mappedMemory;
	gctPOINTER			contiguousLogical;
}
gcsHAL_PRIVATE_DATA, * gcsHAL_PRIVATE_DATA_PTR;

gceSTATUS gckGALDEVICE_Setup_ISR(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Release_ISR(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Start_Thread(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Stop_Thread(
	gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Start(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Stop(
	gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Construct(
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
	);

gceSTATUS gckGALDEVICE_Destroy(
	IN gckGALDEVICE Device
	);

#endif /* __gc_hal_kernel_device_h_ */

