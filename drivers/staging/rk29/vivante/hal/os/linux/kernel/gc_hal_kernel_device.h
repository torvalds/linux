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




#ifndef __gc_hal_kernel_device_h_
#define __gc_hal_kernel_device_h_

#define gcdkUSE_MEMORY_RECORD		1

#ifdef ANDROID
#define gcdkREPORT_VIDMEM_LEAK		0
#else
#define gcdkREPORT_VIDMEM_LEAK		1
#endif

/******************************************************************************\
******************************* gckGALDEVICE Structure *******************************
\******************************************************************************/

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
	gctBOOL				isrInitialized;
	gctBOOL				dataReady;

	/* Thread management. */
	struct task_struct	*threadCtxt;
	struct semaphore	sema;
	gctBOOL				threadInitialized;
	gctBOOL				killThread;

	/* Signal management. */
	gctINT				signal;
}
* gckGALDEVICE;

#if gcdkUSE_MEMORY_RECORD
typedef struct MEMORY_RECORD
{
	gcuVIDMEM_NODE_PTR		node;

	struct MEMORY_RECORD *	prev;
	struct MEMORY_RECORD *	next;
}
MEMORY_RECORD, * MEMORY_RECORD_PTR;
#endif

typedef struct _gcsHAL_PRIVATE_DATA
{
    gckGALDEVICE		device;
    gctPOINTER			mappedMemory;
	gctPOINTER			contiguousLogical;

#if gcdkUSE_MEMORY_RECORD
	MEMORY_RECORD		memoryRecordList;
#endif
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
	IN gctINT Signal,
	OUT gckGALDEVICE *Device
	);

gceSTATUS gckGALDEVICE_Destroy(
	IN gckGALDEVICE Device
	);

#endif /* __gc_hal_kernel_device_h_ */

