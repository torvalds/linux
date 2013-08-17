/*************************************************************************/ /*!
@Title          PVR Bridge Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the PVR Bridge code
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef __PVR_BRIDGE_H__
#define __PVR_BRIDGE_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "servicesint.h"

/*
 * Bridge Cmd Ids
 */


#ifdef __linux__

		#include <linux/ioctl.h>
    /*!< Nov 2006: according to ioctl-number.txt 'g' wasn't in use. */
    #define PVRSRV_IOC_GID      'g'
    #define PVRSRV_IO(INDEX)    _IO(PVRSRV_IOC_GID, INDEX, PVRSRV_BRIDGE_PACKAGE)
    #define PVRSRV_IOW(INDEX)   _IOW(PVRSRV_IOC_GID, INDEX, PVRSRV_BRIDGE_PACKAGE)
    #define PVRSRV_IOR(INDEX)   _IOR(PVRSRV_IOC_GID, INDEX, PVRSRV_BRIDGE_PACKAGE)
    #define PVRSRV_IOWR(INDEX)  _IOWR(PVRSRV_IOC_GID, INDEX, PVRSRV_BRIDGE_PACKAGE)

#else /* __linux__ */

 #if  defined(__QNXNTO__)
			#define PVRSRV_IOC_GID      (0x0UL)
		#else
			#error Unknown platform: Cannot define ioctls
		#endif

	#define PVRSRV_IO(INDEX)    (PVRSRV_IOC_GID + (INDEX))
	#define PVRSRV_IOW(INDEX)   (PVRSRV_IOC_GID + (INDEX))
	#define PVRSRV_IOR(INDEX)   (PVRSRV_IOC_GID + (INDEX))
	#define PVRSRV_IOWR(INDEX)  (PVRSRV_IOC_GID + (INDEX))

	#define PVRSRV_BRIDGE_BASE                  PVRSRV_IOC_GID
#endif /* __linux__ */


/*
 * Note *REMEMBER* to update PVRSRV_BRIDGE_LAST_CMD (below) if you add any new
 * bridge commands!
 */

#define PVRSRV_BRIDGE_CORE_CMD_FIRST			0UL
#define PVRSRV_BRIDGE_ENUM_DEVICES				PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+0)	/*!< enumerate device bridge index */
#define PVRSRV_BRIDGE_ACQUIRE_DEVICEINFO		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+1)	/*!< acquire device data bridge index */
#define PVRSRV_BRIDGE_RELEASE_DEVICEINFO		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+2)	/*!< release device data bridge index */
#define PVRSRV_BRIDGE_CREATE_DEVMEMCONTEXT		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+3)	/*!< create device addressable memory context */
#define PVRSRV_BRIDGE_DESTROY_DEVMEMCONTEXT		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+4)	/*!< destroy device addressable memory context */
#define PVRSRV_BRIDGE_GET_DEVMEM_HEAPINFO		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+5)	/*!< get device memory heap info */
#define PVRSRV_BRIDGE_ALLOC_DEVICEMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+6)	/*!< alloc device memory bridge index */
#define PVRSRV_BRIDGE_FREE_DEVICEMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+7)	/*!< free device memory bridge index */
#define PVRSRV_BRIDGE_GETFREE_DEVICEMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+8)	/*!< get free device memory bridge index */
#define PVRSRV_BRIDGE_CREATE_COMMANDQUEUE		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+9)	/*!< create Cmd Q bridge index */
#define PVRSRV_BRIDGE_DESTROY_COMMANDQUEUE		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+10)	/*!< destroy Cmd Q bridge index */
#define	PVRSRV_BRIDGE_MHANDLE_TO_MMAP_DATA		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+11)   /*!< generate mmap data from a memory handle */
#define PVRSRV_BRIDGE_CONNECT_SERVICES			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+12)	/*!< services connect bridge index */
#define PVRSRV_BRIDGE_DISCONNECT_SERVICES		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+13)	/*!< services disconnect bridge index */
#define PVRSRV_BRIDGE_WRAP_DEVICE_MEM			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+14)	/*!< wrap device memory bridge index */
#define PVRSRV_BRIDGE_GET_DEVICEMEMINFO			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+15)	/*!< read the kernel meminfo record */
#define PVRSRV_BRIDGE_RESERVE_DEV_VIRTMEM		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+16)
#define PVRSRV_BRIDGE_FREE_DEV_VIRTMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+17)
#define PVRSRV_BRIDGE_MAP_EXT_MEMORY			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+18)
#define PVRSRV_BRIDGE_UNMAP_EXT_MEMORY			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+19)
#define PVRSRV_BRIDGE_MAP_DEV_MEMORY			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+20)
#define PVRSRV_BRIDGE_UNMAP_DEV_MEMORY			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+21)
#define PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY	PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+22)
#define PVRSRV_BRIDGE_UNMAP_DEVICECLASS_MEMORY	PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+23)
#define PVRSRV_BRIDGE_MAP_MEM_INFO_TO_USER		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+24)
#define PVRSRV_BRIDGE_UNMAP_MEM_INFO_FROM_USER	PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+25)
#define PVRSRV_BRIDGE_EXPORT_DEVICEMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+26)
#define PVRSRV_BRIDGE_RELEASE_MMAP_DATA			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+27)
#define PVRSRV_BRIDGE_CHG_DEV_MEM_ATTRIBS		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+28)
#define PVRSRV_BRIDGE_MAP_DEV_MEMORY_2			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+29)
#define PVRSRV_BRIDGE_EXPORT_DEVICEMEM_2		PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+30)
#if defined (SUPPORT_ION)
#define PVRSRV_BRIDGE_MAP_ION_HANDLE			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+31)
#define PVRSRV_BRIDGE_UNMAP_ION_HANDLE			PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST+32)
#define PVRSRV_BRIDGE_CORE_CMD_LAST				(PVRSRV_BRIDGE_CORE_CMD_FIRST+32)
#else
#define PVRSRV_BRIDGE_CORE_CMD_LAST				(PVRSRV_BRIDGE_CORE_CMD_FIRST+30)
#endif
/* SIM */
#define PVRSRV_BRIDGE_SIM_CMD_FIRST				(PVRSRV_BRIDGE_CORE_CMD_LAST+1)
#define PVRSRV_BRIDGE_PROCESS_SIMISR_EVENT		PVRSRV_IOWR(PVRSRV_BRIDGE_SIM_CMD_FIRST+0)	/*!< RTSIM pseudo ISR */
#define PVRSRV_BRIDGE_REGISTER_SIM_PROCESS		PVRSRV_IOWR(PVRSRV_BRIDGE_SIM_CMD_FIRST+1)	/*!< Register RTSIM process thread */
#define PVRSRV_BRIDGE_UNREGISTER_SIM_PROCESS	PVRSRV_IOWR(PVRSRV_BRIDGE_SIM_CMD_FIRST+2)	/*!< Unregister RTSIM process thread */
#define PVRSRV_BRIDGE_SIM_CMD_LAST				(PVRSRV_BRIDGE_SIM_CMD_FIRST+2)

/* User Mapping */
#define PVRSRV_BRIDGE_MAPPING_CMD_FIRST			(PVRSRV_BRIDGE_SIM_CMD_LAST+1)
#define PVRSRV_BRIDGE_MAPPHYSTOUSERSPACE		PVRSRV_IOWR(PVRSRV_BRIDGE_MAPPING_CMD_FIRST+0)	/*!< map CPU phys to user space */
#define PVRSRV_BRIDGE_UNMAPPHYSTOUSERSPACE		PVRSRV_IOWR(PVRSRV_BRIDGE_MAPPING_CMD_FIRST+1)	/*!< unmap CPU phys to user space */
#define PVRSRV_BRIDGE_GETPHYSTOUSERSPACEMAP		PVRSRV_IOWR(PVRSRV_BRIDGE_MAPPING_CMD_FIRST+2)	/*!< get user copy of Phys to Lin loopup table */
#define PVRSRV_BRIDGE_MAPPING_CMD_LAST			(PVRSRV_BRIDGE_MAPPING_CMD_FIRST+2)

#define PVRSRV_BRIDGE_STATS_CMD_FIRST			(PVRSRV_BRIDGE_MAPPING_CMD_LAST+1)
#define	PVRSRV_BRIDGE_GET_FB_STATS				PVRSRV_IOWR(PVRSRV_BRIDGE_STATS_CMD_FIRST+0)	/*!< Get FB memory stats */
#define PVRSRV_BRIDGE_STATS_CMD_LAST			(PVRSRV_BRIDGE_STATS_CMD_FIRST+0)

/* API to retrieve misc. info. from services */
#define PVRSRV_BRIDGE_MISC_CMD_FIRST			(PVRSRV_BRIDGE_STATS_CMD_LAST+1)
#define PVRSRV_BRIDGE_GET_MISC_INFO				PVRSRV_IOWR(PVRSRV_BRIDGE_MISC_CMD_FIRST+0)	/*!< misc. info. */
#define PVRSRV_BRIDGE_RELEASE_MISC_INFO			PVRSRV_IOWR(PVRSRV_BRIDGE_MISC_CMD_FIRST+1)	/*!< misc. info. */
#define PVRSRV_BRIDGE_MISC_CMD_LAST				(PVRSRV_BRIDGE_MISC_CMD_FIRST+1)

/* Overlay ioctls */

#if defined (SUPPORT_OVERLAY_ROTATE_BLIT)
#define PVRSRV_BRIDGE_OVERLAY_CMD_FIRST			(PVRSRV_BRIDGE_MISC_CMD_LAST+1)
#define PVRSRV_BRIDGE_INIT_3D_OVL_BLT_RES		PVRSRV_IOWR(PVRSRV_BRIDGE_OVERLAY_CMD_FIRST+0)	/*!< 3D Overlay rotate blit init */
#define PVRSRV_BRIDGE_DEINIT_3D_OVL_BLT_RES		PVRSRV_IOWR(PVRSRV_BRIDGE_OVERLAY_CMD_FIRST+1)	/*!< 3D Overlay rotate blit deinit */
#define PVRSRV_BRIDGE_OVERLAY_CMD_LAST			(PVRSRV_BRIDGE_OVERLAY_CMD_FIRST+1)
#else
#define PVRSRV_BRIDGE_OVERLAY_CMD_LAST			PVRSRV_BRIDGE_MISC_CMD_LAST
#endif

/* PDUMP */
#if defined(PDUMP)
#define PVRSRV_BRIDGE_PDUMP_CMD_FIRST			(PVRSRV_BRIDGE_OVERLAY_CMD_LAST+1)
#define PVRSRV_BRIDGE_PDUMP_INIT			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+0)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_MEMPOL			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+1)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_DUMPMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+2)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_REG				PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+3)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_REGPOL			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+4)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_COMMENT			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+5)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_SETFRAME			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+6)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_ISCAPTURING			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+7)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_DUMPBITMAP			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+8)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_DUMPREADREG			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+9)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_SYNCPOL			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+10)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_DUMPSYNC			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+11)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_MEMPAGES			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+12)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_DRIVERINFO			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+13)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_DUMPPDDEVPADDR		PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+15)	/*!< pdump command structure */
#define PVRSRV_BRIDGE_PDUMP_CYCLE_COUNT_REG_READ	PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+16)
#define PVRSRV_BRIDGE_PDUMP_STARTINITPHASE			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+17)
#define PVRSRV_BRIDGE_PDUMP_STOPINITPHASE			PVRSRV_IOWR(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+18)
#define PVRSRV_BRIDGE_PDUMP_CMD_LAST			(PVRSRV_BRIDGE_PDUMP_CMD_FIRST+18)
#else
/* Note we are carefull here not to leave a large gap in the ioctl numbers.
 * (Some ports may use these values to index into an array where large gaps can
 * waste memory) */
#define PVRSRV_BRIDGE_PDUMP_CMD_LAST			PVRSRV_BRIDGE_OVERLAY_CMD_LAST
#endif

/* DisplayClass APIs */
#define PVRSRV_BRIDGE_OEM_CMD_FIRST				(PVRSRV_BRIDGE_PDUMP_CMD_LAST+1)
#define PVRSRV_BRIDGE_GET_OEMJTABLE				PVRSRV_IOWR(PVRSRV_BRIDGE_OEM_CMD_FIRST+0)	/*!< Get OEM Jtable */
#define PVRSRV_BRIDGE_OEM_CMD_LAST				(PVRSRV_BRIDGE_OEM_CMD_FIRST+0)

/* device class enum */
#define PVRSRV_BRIDGE_DEVCLASS_CMD_FIRST		(PVRSRV_BRIDGE_OEM_CMD_LAST+1)
#define PVRSRV_BRIDGE_ENUM_CLASS				PVRSRV_IOWR(PVRSRV_BRIDGE_DEVCLASS_CMD_FIRST+0)
#define PVRSRV_BRIDGE_DEVCLASS_CMD_LAST			(PVRSRV_BRIDGE_DEVCLASS_CMD_FIRST+0)

/* display class API */
#define PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST		(PVRSRV_BRIDGE_DEVCLASS_CMD_LAST+1)
#define PVRSRV_BRIDGE_OPEN_DISPCLASS_DEVICE		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+0)
#define PVRSRV_BRIDGE_CLOSE_DISPCLASS_DEVICE	PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+1)
#define PVRSRV_BRIDGE_ENUM_DISPCLASS_FORMATS	PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+2)
#define PVRSRV_BRIDGE_ENUM_DISPCLASS_DIMS		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+3)
#define PVRSRV_BRIDGE_GET_DISPCLASS_SYSBUFFER	PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+4)
#define PVRSRV_BRIDGE_GET_DISPCLASS_INFO		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+5)
#define PVRSRV_BRIDGE_CREATE_DISPCLASS_SWAPCHAIN		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+6)
#define PVRSRV_BRIDGE_DESTROY_DISPCLASS_SWAPCHAIN		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+7)
#define PVRSRV_BRIDGE_SET_DISPCLASS_DSTRECT		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+8)
#define PVRSRV_BRIDGE_SET_DISPCLASS_SRCRECT		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+9)
#define PVRSRV_BRIDGE_SET_DISPCLASS_DSTCOLOURKEY		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+10)
#define PVRSRV_BRIDGE_SET_DISPCLASS_SRCCOLOURKEY		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+11)
#define PVRSRV_BRIDGE_GET_DISPCLASS_BUFFERS		PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+12)
#define PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER	PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+13)
#define PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER2	PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+14)
#define PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_SYSTEM	PVRSRV_IOWR(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+15)
#define PVRSRV_BRIDGE_DISPCLASS_CMD_LAST		(PVRSRV_BRIDGE_DISPCLASS_CMD_FIRST+15)

/* buffer class API */
#define PVRSRV_BRIDGE_BUFCLASS_CMD_FIRST		(PVRSRV_BRIDGE_DISPCLASS_CMD_LAST+1)
#define PVRSRV_BRIDGE_OPEN_BUFFERCLASS_DEVICE	PVRSRV_IOWR(PVRSRV_BRIDGE_BUFCLASS_CMD_FIRST+0)
#define PVRSRV_BRIDGE_CLOSE_BUFFERCLASS_DEVICE	PVRSRV_IOWR(PVRSRV_BRIDGE_BUFCLASS_CMD_FIRST+1)
#define PVRSRV_BRIDGE_GET_BUFFERCLASS_INFO		PVRSRV_IOWR(PVRSRV_BRIDGE_BUFCLASS_CMD_FIRST+2)
#define PVRSRV_BRIDGE_GET_BUFFERCLASS_BUFFER	PVRSRV_IOWR(PVRSRV_BRIDGE_BUFCLASS_CMD_FIRST+3)
#define PVRSRV_BRIDGE_BUFCLASS_CMD_LAST			(PVRSRV_BRIDGE_BUFCLASS_CMD_FIRST+3)

/* Wrap/Unwrap external memory */
#define PVRSRV_BRIDGE_WRAP_CMD_FIRST			(PVRSRV_BRIDGE_BUFCLASS_CMD_LAST+1)
#define PVRSRV_BRIDGE_WRAP_EXT_MEMORY			PVRSRV_IOWR(PVRSRV_BRIDGE_WRAP_CMD_FIRST+0)
#define PVRSRV_BRIDGE_UNWRAP_EXT_MEMORY			PVRSRV_IOWR(PVRSRV_BRIDGE_WRAP_CMD_FIRST+1)
#define PVRSRV_BRIDGE_WRAP_CMD_LAST				(PVRSRV_BRIDGE_WRAP_CMD_FIRST+1)

/* Shared memory */
#define PVRSRV_BRIDGE_SHAREDMEM_CMD_FIRST		(PVRSRV_BRIDGE_WRAP_CMD_LAST+1)
#define PVRSRV_BRIDGE_ALLOC_SHARED_SYS_MEM		PVRSRV_IOWR(PVRSRV_BRIDGE_SHAREDMEM_CMD_FIRST+0)
#define PVRSRV_BRIDGE_FREE_SHARED_SYS_MEM		PVRSRV_IOWR(PVRSRV_BRIDGE_SHAREDMEM_CMD_FIRST+1)
#define PVRSRV_BRIDGE_MAP_MEMINFO_MEM			PVRSRV_IOWR(PVRSRV_BRIDGE_SHAREDMEM_CMD_FIRST+2)
#define PVRSRV_BRIDGE_UNMAP_MEMINFO_MEM			PVRSRV_IOWR(PVRSRV_BRIDGE_SHAREDMEM_CMD_FIRST+3)
#define PVRSRV_BRIDGE_SHAREDMEM_CMD_LAST		(PVRSRV_BRIDGE_SHAREDMEM_CMD_FIRST+3)

/* Intialisation Service support */
#define PVRSRV_BRIDGE_INITSRV_CMD_FIRST			(PVRSRV_BRIDGE_SHAREDMEM_CMD_LAST+1)
#define PVRSRV_BRIDGE_INITSRV_CONNECT			PVRSRV_IOWR(PVRSRV_BRIDGE_INITSRV_CMD_FIRST+0)
#define PVRSRV_BRIDGE_INITSRV_DISCONNECT		PVRSRV_IOWR(PVRSRV_BRIDGE_INITSRV_CMD_FIRST+1)
#define PVRSRV_BRIDGE_INITSRV_CMD_LAST			(PVRSRV_BRIDGE_INITSRV_CMD_FIRST+1)

/* Event Objects */
#define PVRSRV_BRIDGE_EVENT_OBJECT_CMD_FIRST	(PVRSRV_BRIDGE_INITSRV_CMD_LAST+1)
#define PVRSRV_BRIDGE_EVENT_OBJECT_WAIT			PVRSRV_IOWR(PVRSRV_BRIDGE_EVENT_OBJECT_CMD_FIRST+0)
#define PVRSRV_BRIDGE_EVENT_OBJECT_OPEN			PVRSRV_IOWR(PVRSRV_BRIDGE_EVENT_OBJECT_CMD_FIRST+1)
#define PVRSRV_BRIDGE_EVENT_OBJECT_CLOSE		PVRSRV_IOWR(PVRSRV_BRIDGE_EVENT_OBJECT_CMD_FIRST+2)
#define PVRSRV_BRIDGE_EVENT_OBJECT_CMD_LAST		(PVRSRV_BRIDGE_EVENT_OBJECT_CMD_FIRST+2)

/* Sync ops */
#define PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST		(PVRSRV_BRIDGE_EVENT_OBJECT_CMD_LAST+1)
#define PVRSRV_BRIDGE_CREATE_SYNC_INFO_MOD_OBJ	PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+0)
#define PVRSRV_BRIDGE_DESTROY_SYNC_INFO_MOD_OBJ	PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+1)
#define PVRSRV_BRIDGE_MODIFY_PENDING_SYNC_OPS	PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+2)
#define PVRSRV_BRIDGE_MODIFY_COMPLETE_SYNC_OPS	PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+3)
#define PVRSRV_BRIDGE_SYNC_OPS_TAKE_TOKEN       PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+4)
#define PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_TOKEN   PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+5)
#define PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_MOD_OBJ	PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+6)
#define PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_DELTA	PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+7)
#define PVRSRV_BRIDGE_ALLOC_SYNC_INFO           PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+8)
#define PVRSRV_BRIDGE_FREE_SYNC_INFO            PVRSRV_IOWR(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+9)
#define PVRSRV_BRIDGE_SYNC_OPS_CMD_LAST			(PVRSRV_BRIDGE_SYNC_OPS_CMD_FIRST+9)

/* For sgx_bridge.h (msvdx_bridge.h should probably use these defines too) */
#define PVRSRV_BRIDGE_LAST_NON_DEVICE_CMD		(PVRSRV_BRIDGE_SYNC_OPS_CMD_LAST+1)


/******************************************************************************
 * Bridge flags
 *****************************************************************************/
#define PVRSRV_KERNEL_MODE_CLIENT				1

/******************************************************************************
 * Generic bridge structures
 *****************************************************************************/

/******************************************************************************
 *	bridge return structure
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_RETURN_TAG
{
	PVRSRV_ERROR eError;
	IMG_VOID *pvData;

}PVRSRV_BRIDGE_RETURN;


/******************************************************************************
 *	bridge packaging structure
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_PACKAGE_TAG
{
	IMG_UINT32				ui32BridgeID;			/*!< ioctl/drvesc index */
	IMG_UINT32				ui32Size;				/*!< size of structure */
	IMG_VOID				*pvParamIn;				/*!< input data buffer */
	IMG_UINT32				ui32InBufferSize;		/*!< size of input data buffer */
	IMG_VOID				*pvParamOut;			/*!< output data buffer */
	IMG_UINT32				ui32OutBufferSize;		/*!< size of output data buffer */

	IMG_HANDLE				hKernelServices;		/*!< kernel servcies handle */
}PVRSRV_BRIDGE_PACKAGE;


/******************************************************************************
 * Input structures for IOCTL/DRVESC
 *****************************************************************************/


/******************************************************************************
 *	'bridge in' connect to services
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_CONNECT_SERVICES_TAG
{
	IMG_UINT32		ui32BridgeFlags; /* Must be first member of structure */
	IMG_UINT32		ui32Flags;
} PVRSRV_BRIDGE_IN_CONNECT_SERVICES;

/******************************************************************************
 *	'bridge in' acquire device info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_ACQUIRE_DEVICEINFO_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_UINT32			uiDevIndex;
	PVRSRV_DEVICE_TYPE	eDeviceType;

} PVRSRV_BRIDGE_IN_ACQUIRE_DEVICEINFO;


/******************************************************************************
 *	'bridge in' enum class
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_ENUMCLASS_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_DEVICE_CLASS sDeviceClass;
} PVRSRV_BRIDGE_IN_ENUMCLASS;


/******************************************************************************
 *	'bridge in' close display class device
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_CLOSE_DISPCLASS_DEVICE_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
} PVRSRV_BRIDGE_IN_CLOSE_DISPCLASS_DEVICE;


/******************************************************************************
 *	'bridge in' enum display class formats
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_FORMATS_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
} PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_FORMATS;


/******************************************************************************
 *	'bridge in' get display class sysbuffer
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_DISPCLASS_SYSBUFFER_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
} PVRSRV_BRIDGE_IN_GET_DISPCLASS_SYSBUFFER;


/******************************************************************************
 *	'bridge in' display class info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_DISPCLASS_INFO_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
} PVRSRV_BRIDGE_IN_GET_DISPCLASS_INFO;


/******************************************************************************
 *	'bridge in' close buffer class device
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_CLOSE_BUFFERCLASS_DEVICE_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
} PVRSRV_BRIDGE_IN_CLOSE_BUFFERCLASS_DEVICE;


/******************************************************************************
 *	'bridge in' close buffer class device
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_INFO_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
} PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_INFO;


/******************************************************************************
 *	'bridge out' acquire device info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_RELEASE_DEVICEINFO_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;

} PVRSRV_BRIDGE_IN_RELEASE_DEVICEINFO;


/******************************************************************************
 *	'bridge in' free class devices info.
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_FREE_CLASSDEVICEINFO_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_DEVICE_CLASS DeviceClass;
	IMG_VOID*			pvDevInfo;

}PVRSRV_BRIDGE_IN_FREE_CLASSDEVICEINFO;


/******************************************************************************
 *	'bridge in' get device memory heap info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_DEVMEM_HEAPINFO_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_HANDLE 			hDevMemContext;

}PVRSRV_BRIDGE_IN_GET_DEVMEM_HEAPINFO;


/******************************************************************************
 *	'bridge in' create device memory context
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_CREATE_DEVMEMCONTEXT_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;

}PVRSRV_BRIDGE_IN_CREATE_DEVMEMCONTEXT;


/******************************************************************************
 *	'bridge in' destroy device memory context
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_DESTROY_DEVMEMCONTEXT_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE 			hDevCookie;
	IMG_HANDLE 			hDevMemContext;

}PVRSRV_BRIDGE_IN_DESTROY_DEVMEMCONTEXT;


/******************************************************************************
 *	'bridge in' alloc device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_ALLOCDEVICEMEM_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_HANDLE			hDevMemHeap;
	IMG_UINT32			ui32Attribs;
	IMG_SIZE_T			uSize;
	IMG_SIZE_T			uAlignment;
	IMG_PVOID			pvPrivData;
	IMG_UINT32			ui32PrivDataLength;

	IMG_UINT32			ui32ChunkSize;
	IMG_UINT32			ui32NumVirtChunks;
	IMG_UINT32			ui32NumPhysChunks;
	IMG_BOOL			*pabMapChunk;
}PVRSRV_BRIDGE_IN_ALLOCDEVICEMEM;

/******************************************************************************
 *	'bridge in' map meminfo to user mode
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_MAPMEMINFOTOUSER_TAG
{
	IMG_UINT32  ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;

}PVRSRV_BRIDGE_IN_MAPMEMINFOTOUSER;

/******************************************************************************
 *	'bridge in' unmap meminfo from user mode
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNMAPMEMINFOFROMUSER_TAG
{
	IMG_UINT32      ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	IMG_PVOID				 pvLinAddr;
	IMG_HANDLE				 hMappingInfo;

}PVRSRV_BRIDGE_IN_UNMAPMEMINFOFROMUSER;

/******************************************************************************
 *	'bridge in' free device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_FREEDEVICEMEM_TAG
{
	IMG_UINT32              ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE				hDevCookie;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;

}PVRSRV_BRIDGE_IN_FREEDEVICEMEM;

/******************************************************************************
 *	'bridge in' export device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_EXPORTDEVICEMEM_TAG
{
	IMG_UINT32      ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE				hDevCookie;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;

}PVRSRV_BRIDGE_IN_EXPORTDEVICEMEM;

/******************************************************************************
 *	'bridge in' map ion handle
 *****************************************************************************/
#define ION_IMPORT_MAX_CHUNK_COUNT 3
typedef struct _PVRSRV_BRIDGE_IN_MAP_ION_HANDLE_
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			handle;
	IMG_UINT32			ui32Attribs;
	IMG_UINT32 			ui32ChunkCount;
	IMG_SIZE_T 			auiOffset[ION_IMPORT_MAX_CHUNK_COUNT];
	IMG_SIZE_T 			auiSize[ION_IMPORT_MAX_CHUNK_COUNT];
	IMG_HANDLE			hDevCookie;
	IMG_HANDLE			hDevMemHeap;
} PVRSRV_BRIDGE_IN_MAP_ION_HANDLE;

/******************************************************************************
 *	'bridge in' unmap ion handle
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNMAP_ION_HANDLE_TAG
{
	IMG_UINT32              ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
}PVRSRV_BRIDGE_IN_UNMAP_ION_HANDLE;

/******************************************************************************
 *	'bridge in' get free device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GETFREEDEVICEMEM_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_UINT32			ui32Flags;

} PVRSRV_BRIDGE_IN_GETFREEDEVICEMEM;

/******************************************************************************
 *	'bridge in' create Cmd Q
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_CREATECOMMANDQUEUE_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_SIZE_T			uQueueSize;

}PVRSRV_BRIDGE_IN_CREATECOMMANDQUEUE;


/******************************************************************************
 *	'bridge in' destroy Cmd Q
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_DESTROYCOMMANDQUEUE_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	PVRSRV_QUEUE_INFO	*psQueueInfo;

}PVRSRV_BRIDGE_IN_DESTROYCOMMANDQUEUE;


/******************************************************************************
 *	'bridge in' get full map data
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_MHANDLE_TO_MMAP_DATA_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hMHandle;	 /* Handle associated with the memory that needs to be mapped */
} PVRSRV_BRIDGE_IN_MHANDLE_TO_MMAP_DATA;


/******************************************************************************
 *	'bridge in' get full map data
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_RELEASE_MMAP_DATA_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hMHandle;	 /* Handle associated with the memory that needs to be mapped */
} PVRSRV_BRIDGE_IN_RELEASE_MMAP_DATA;


/******************************************************************************
 *	'bridge in' reserve vm
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_RESERVE_DEV_VIRTMEM_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevMemHeap;
	IMG_DEV_VIRTADDR	*psDevVAddr;
	IMG_SIZE_T			uSize;
	IMG_SIZE_T			uAlignment;

}PVRSRV_BRIDGE_IN_RESERVE_DEV_VIRTMEM;

/******************************************************************************
 *	'bridge out' connect to services
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_CONNECT_SERVICES_TAG
{
	PVRSRV_ERROR    eError;
	IMG_HANDLE		hKernelServices;
}PVRSRV_BRIDGE_OUT_CONNECT_SERVICES;

/******************************************************************************
 *	'bridge out' reserve vm
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_RESERVE_DEV_VIRTMEM_TAG
{
	PVRSRV_ERROR            eError;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;

}PVRSRV_BRIDGE_OUT_RESERVE_DEV_VIRTMEM;


/******************************************************************************
 *	'bridge in' free vm
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_FREE_DEV_VIRTMEM_TAG
{
	IMG_UINT32              ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;

}PVRSRV_BRIDGE_IN_FREE_DEV_VIRTMEM;


/******************************************************************************
 *	'bridge in' map dev memory allocation to another heap
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_MAP_DEV_MEMORY_TAG
{
	IMG_UINT32				ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE				hKernelMemInfo;
	IMG_HANDLE				hDstDevMemHeap;

}PVRSRV_BRIDGE_IN_MAP_DEV_MEMORY;


/******************************************************************************
 *	'bridge out' map dev memory allocation to another heap
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_MAP_DEV_MEMORY_TAG
{
	PVRSRV_ERROR            eError;
	PVRSRV_KERNEL_MEM_INFO	*psDstKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sDstClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sDstClientSyncInfo;

}PVRSRV_BRIDGE_OUT_MAP_DEV_MEMORY;


/******************************************************************************
 *	'bridge in' unmap dev memory allocation
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNMAP_DEV_MEMORY_TAG
{
	IMG_UINT32              ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO		*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;

}PVRSRV_BRIDGE_IN_UNMAP_DEV_MEMORY;


/******************************************************************************
 *	'bridge in' map pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_MAP_EXT_MEMORY_TAG
{
	IMG_UINT32       ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	IMG_SYS_PHYADDR *psSysPAddr;
	IMG_UINT32       ui32Flags;

}PVRSRV_BRIDGE_IN_MAP_EXT_MEMORY;

/******************************************************************************
 *	'bridge in' unmap pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNMAP_EXT_MEMORY_TAG
{
	IMG_UINT32					ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_CLIENT_MEM_INFO		sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO		sClientSyncInfo;
	IMG_UINT32					ui32Flags;

}PVRSRV_BRIDGE_IN_UNMAP_EXT_MEMORY;

/******************************************************************************
 *	'bridge in' map device class buffer pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_MAP_DEVICECLASS_MEMORY_TAG
{
	IMG_UINT32		ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE		hDeviceClassBuffer;
	IMG_HANDLE		hDevMemContext;

}PVRSRV_BRIDGE_IN_MAP_DEVICECLASS_MEMORY;


/******************************************************************************
 *	'bridge out' map device class buffer pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_MAP_DEVICECLASS_MEMORY_TAG
{
	PVRSRV_ERROR            eError;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	IMG_HANDLE				hMappingInfo;

}PVRSRV_BRIDGE_OUT_MAP_DEVICECLASS_MEMORY;


/******************************************************************************
 *	'bridge in' unmap device class buffer pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNMAP_DEVICECLASS_MEMORY_TAG
{
	IMG_UINT32              ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;

}PVRSRV_BRIDGE_IN_UNMAP_DEVICECLASS_MEMORY;


/******************************************************************************
 *	'bridge in' pdump memory poll
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_MEMPOL_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32Mask;
	PDUMP_POLL_OPERATOR		eOperator;
	IMG_UINT32 ui32Flags;

}PVRSRV_BRIDGE_IN_PDUMP_MEMPOL;

/******************************************************************************
 *	'bridge in' pdump sync poll
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_SYNCPOL_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo;
	IMG_BOOL   bIsRead;
	IMG_BOOL   bUseLastOpDumpVal;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32Mask;

}PVRSRV_BRIDGE_IN_PDUMP_SYNCPOL;


/******************************************************************************
 *	'bridge in' pdump dump memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_DUMPMEM_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_PVOID  pvLinAddr;
	IMG_PVOID  pvAltLinAddr;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Bytes;
	IMG_UINT32 ui32Flags;

}PVRSRV_BRIDGE_IN_PDUMP_DUMPMEM;


/******************************************************************************
 *	'bridge in' pdump dump sync
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_DUMPSYNC_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_PVOID  pvAltLinAddr;
	PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Bytes;

}PVRSRV_BRIDGE_IN_PDUMP_DUMPSYNC;


/******************************************************************************
 *	'bridge in' pdump dump reg
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_DUMPREG_TAG
{
	IMG_UINT32		ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE		hDevCookie;
	PVRSRV_HWREG	sHWReg;
	IMG_UINT32		ui32Flags;
	IMG_CHAR		szRegRegion[32];

}PVRSRV_BRIDGE_IN_PDUMP_DUMPREG;

/******************************************************************************
 *	'bridge in' pdump dump reg
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_REGPOL_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hDevCookie;
	PVRSRV_HWREG sHWReg;
	IMG_UINT32 ui32Mask;
	IMG_UINT32 ui32Flags;
	IMG_CHAR   szRegRegion[32];
}PVRSRV_BRIDGE_IN_PDUMP_REGPOL;

/******************************************************************************
 *	'bridge in' pdump dump PD reg
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_DUMPPDREG_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_HWREG sHWReg;
	IMG_UINT32 ui32Flags;

}PVRSRV_BRIDGE_IN_PDUMP_DUMPPDREG;

/******************************************************************************
 *	'bridge in' pdump dump mem pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_MEMPAGES_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_HANDLE			hKernelMemInfo;
	IMG_DEV_PHYADDR		*pPages;
	IMG_UINT32			ui32NumPages;
	IMG_DEV_VIRTADDR	sDevVAddr;
	IMG_UINT32			ui32Start;
	IMG_UINT32			ui32Length;
	IMG_UINT32			ui32Flags;

}PVRSRV_BRIDGE_IN_PDUMP_MEMPAGES;

/******************************************************************************
 *	'bridge in' pdump dump comment
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_COMMENT_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_CHAR szComment[PVRSRV_PDUMP_MAX_COMMENT_SIZE];
	IMG_UINT32 ui32Flags;

}PVRSRV_BRIDGE_IN_PDUMP_COMMENT;


/******************************************************************************
 *	'bridge in' pdump set frame
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_SETFRAME_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_UINT32 ui32Frame;

}PVRSRV_BRIDGE_IN_PDUMP_SETFRAME;


/******************************************************************************
 *	'bridge in' pdump dump bitmap
 *****************************************************************************/

typedef struct PVRSRV_BRIDGE_IN_PDUMP_BITMAP_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hDevCookie;
	IMG_CHAR   szFileName[PVRSRV_PDUMP_MAX_FILENAME_SIZE];
	IMG_UINT32 ui32FileOffset;
	IMG_UINT32 ui32Width;
	IMG_UINT32 ui32Height;
	IMG_UINT32 ui32StrideInBytes;
	IMG_DEV_VIRTADDR sDevBaseAddr;
	IMG_HANDLE hDevMemContext;
	IMG_UINT32 ui32Size;
	PDUMP_PIXEL_FORMAT ePixelFormat;
	PDUMP_MEM_FORMAT eMemFormat;
	IMG_UINT32 ui32Flags;

}PVRSRV_BRIDGE_IN_PDUMP_BITMAP;


/******************************************************************************
 *	'bridge in' pdump dump read reg
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_READREG_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hDevCookie;
	IMG_CHAR   szFileName[PVRSRV_PDUMP_MAX_FILENAME_SIZE];
	IMG_UINT32 ui32FileOffset;
	IMG_UINT32 ui32Address;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32Flags;
	IMG_CHAR   szRegRegion[32];

}PVRSRV_BRIDGE_IN_PDUMP_READREG;

/******************************************************************************
 *	'bridge in' pdump dump driver-info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PDUMP_DRIVERINFO_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_CHAR szString[PVRSRV_PDUMP_MAX_COMMENT_SIZE];
	IMG_BOOL bContinuous;

}PVRSRV_BRIDGE_IN_PDUMP_DRIVERINFO;

typedef struct PVRSRV_BRIDGE_IN_PDUMP_DUMPPDDEVPADDR_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelMemInfo;
	IMG_UINT32 ui32Offset;
	IMG_DEV_PHYADDR sPDDevPAddr;
}PVRSRV_BRIDGE_IN_PDUMP_DUMPPDDEVPADDR;

/******************************************************************************
 *	'bridge in' pdump cycle count register read
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_PDUM_IN_CYCLE_COUNT_REG_READ_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hDevCookie;
	IMG_UINT32 ui32RegOffset;
	IMG_BOOL bLastFrame;
}PVRSRV_BRIDGE_IN_PDUMP_CYCLE_COUNT_REG_READ;

/*****************************************************************************
 * Output structures for BRIDGEs
 ****************************************************************************/

/******************************************************************************
 *	'bridge out' enum. devices
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_ENUMDEVICE_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32NumDevices;
	PVRSRV_DEVICE_IDENTIFIER asDeviceIdentifier[PVRSRV_MAX_DEVICES];

}PVRSRV_BRIDGE_OUT_ENUMDEVICE;


/******************************************************************************
 *	'bridge out' acquire device info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_ACQUIRE_DEVICEINFO_TAG
{

	PVRSRV_ERROR		eError;
	IMG_HANDLE			hDevCookie;

} PVRSRV_BRIDGE_OUT_ACQUIRE_DEVICEINFO;


/******************************************************************************
 *	'bridge out' enum. class devices
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_ENUMCLASS_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32NumDevices;
	IMG_UINT32 ui32DevID[PVRSRV_MAX_DEVICES];

}PVRSRV_BRIDGE_OUT_ENUMCLASS;


/******************************************************************************
 *	'bridge in' open display class devices
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_OPEN_DISPCLASS_DEVICE_TAG
{
	IMG_UINT32		ui32BridgeFlags; /* Must be first member of structure */
	IMG_UINT32		ui32DeviceID;
	IMG_HANDLE		hDevCookie;

}PVRSRV_BRIDGE_IN_OPEN_DISPCLASS_DEVICE;

/******************************************************************************
 *	'bridge out' open display class devices
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_OPEN_DISPCLASS_DEVICE_TAG
{
	PVRSRV_ERROR	eError;
	IMG_HANDLE		hDeviceKM;

}PVRSRV_BRIDGE_OUT_OPEN_DISPCLASS_DEVICE;


/******************************************************************************
 *	'bridge in' wrap pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_WRAP_EXT_MEMORY_TAG
{
	IMG_UINT32				ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE              hDevCookie;
	IMG_HANDLE				hDevMemContext;
	IMG_VOID				*pvLinAddr;
	IMG_SIZE_T              uByteSize;
	IMG_SIZE_T              uPageOffset;
	IMG_BOOL                bPhysContig;
	IMG_UINT32				ui32NumPageTableEntries;
	IMG_SYS_PHYADDR         *psSysPAddr;
	IMG_UINT32				ui32Flags;

}PVRSRV_BRIDGE_IN_WRAP_EXT_MEMORY;

/******************************************************************************
 *	'bridge out' wrap pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_WRAP_EXT_MEMORY_TAG
{
	PVRSRV_ERROR	eError;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO	sClientSyncInfo;

}PVRSRV_BRIDGE_OUT_WRAP_EXT_MEMORY;

/******************************************************************************
 *	'bridge in' unwrap pages
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNWRAP_EXT_MEMORY_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO	sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;

}PVRSRV_BRIDGE_IN_UNWRAP_EXT_MEMORY;


#define PVRSRV_MAX_DC_DISPLAY_FORMATS			10
#define PVRSRV_MAX_DC_DISPLAY_DIMENSIONS		10
#define PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS			4
#define PVRSRV_MAX_DC_CLIP_RECTS				32

/******************************************************************************
 *	'bridge out' enum display class formats
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_FORMATS_TAG
{
	PVRSRV_ERROR	eError;
	IMG_UINT32		ui32Count;
	DISPLAY_FORMAT	asFormat[PVRSRV_MAX_DC_DISPLAY_FORMATS];

}PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_FORMATS;


/******************************************************************************
 *	'bridge in' enum display class dims
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_DIMS_TAG
{
	IMG_UINT32		ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE		hDeviceKM;
	DISPLAY_FORMAT	sFormat;

}PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_DIMS;


/******************************************************************************
 *	'bridge out' enum display class dims
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_DIMS_TAG
{
	PVRSRV_ERROR	eError;
	IMG_UINT32		ui32Count;
	DISPLAY_DIMS	asDim[PVRSRV_MAX_DC_DISPLAY_DIMENSIONS];

}PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_DIMS;


/******************************************************************************
 *	'bridge out' enum display class dims
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_DISPCLASS_INFO_TAG
{
	PVRSRV_ERROR	eError;
	DISPLAY_INFO	sDisplayInfo;

}PVRSRV_BRIDGE_OUT_GET_DISPCLASS_INFO;


/******************************************************************************
 *	'bridge out' get display class system buffer
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_DISPCLASS_SYSBUFFER_TAG
{
	PVRSRV_ERROR	eError;
	IMG_HANDLE		hBuffer;

}PVRSRV_BRIDGE_OUT_GET_DISPCLASS_SYSBUFFER;


/******************************************************************************
 *	'bridge in' create swap chain
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_CREATE_DISPCLASS_SWAPCHAIN_TAG
{
	IMG_UINT32				ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE				hDeviceKM;
	IMG_UINT32				ui32Flags;
	DISPLAY_SURF_ATTRIBUTES	sDstSurfAttrib;
	DISPLAY_SURF_ATTRIBUTES	sSrcSurfAttrib;
	IMG_UINT32				ui32BufferCount;
	IMG_UINT32				ui32OEMFlags;
	IMG_UINT32				ui32SwapChainID;

} PVRSRV_BRIDGE_IN_CREATE_DISPCLASS_SWAPCHAIN;


/******************************************************************************
 *	'bridge out' create swap chain
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_CREATE_DISPCLASS_SWAPCHAIN_TAG
{
	PVRSRV_ERROR		eError;
	IMG_HANDLE			hSwapChain;
	IMG_UINT32			ui32SwapChainID;

} PVRSRV_BRIDGE_OUT_CREATE_DISPCLASS_SWAPCHAIN;


/******************************************************************************
 *	'bridge in' destroy swap chain
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_DESTROY_DISPCLASS_SWAPCHAIN_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hSwapChain;

} PVRSRV_BRIDGE_IN_DESTROY_DISPCLASS_SWAPCHAIN;


/******************************************************************************
 *	'bridge in' set DST/SRC rect
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_SET_DISPCLASS_RECT_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hSwapChain;
	IMG_RECT			sRect;

} PVRSRV_BRIDGE_IN_SET_DISPCLASS_RECT;


/******************************************************************************
 *	'bridge in' set DST/SRC colourkey
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_SET_DISPCLASS_COLOURKEY_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hSwapChain;
	IMG_UINT32			ui32CKColour;

} PVRSRV_BRIDGE_IN_SET_DISPCLASS_COLOURKEY;


/******************************************************************************
 *	'bridge in' get buffers (from swapchain)
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_DISPCLASS_BUFFERS_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hSwapChain;

} PVRSRV_BRIDGE_IN_GET_DISPCLASS_BUFFERS;


/******************************************************************************
 *	'bridge out' get buffers (from swapchain)
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_DISPCLASS_BUFFERS_TAG
{
	PVRSRV_ERROR		eError;
	IMG_UINT32			ui32BufferCount;
	IMG_HANDLE			ahBuffer[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	IMG_SYS_PHYADDR		asPhyAddr[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
} PVRSRV_BRIDGE_OUT_GET_DISPCLASS_BUFFERS;


/******************************************************************************
 *	'bridge in' swap to buffer
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hBuffer;
	IMG_UINT32			ui32SwapInterval;
	IMG_HANDLE			hPrivateTag;
	IMG_UINT32			ui32ClipRectCount;
	IMG_RECT			sClipRect[PVRSRV_MAX_DC_CLIP_RECTS];

} PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER;

/******************************************************************************
 *	'bridge in' swap to buffer
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER2_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hSwapChain;
	IMG_UINT32			ui32SwapInterval;

	IMG_UINT32			ui32NumMemInfos;
	PVRSRV_KERNEL_MEM_INFO	**ppsKernelMemInfos;
	PVRSRV_KERNEL_SYNC_INFO	**ppsKernelSyncInfos;

	IMG_UINT32			ui32PrivDataLength;
	IMG_PVOID			pvPrivData;

} PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER2;

/******************************************************************************
 *	'bridge in' swap to system buffer (primary)
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_SYSTEM_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_HANDLE			hSwapChain;

} PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_SYSTEM;


/******************************************************************************
 *	'bridge in' open buffer class device
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_OPEN_BUFFERCLASS_DEVICE_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_UINT32			ui32DeviceID;
	IMG_HANDLE			hDevCookie;

} PVRSRV_BRIDGE_IN_OPEN_BUFFERCLASS_DEVICE;


/******************************************************************************
 *	'bridge out' open buffer class device
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_OPEN_BUFFERCLASS_DEVICE_TAG
{
	PVRSRV_ERROR eError;
	IMG_HANDLE			hDeviceKM;

} PVRSRV_BRIDGE_OUT_OPEN_BUFFERCLASS_DEVICE;


/******************************************************************************
 *	'bridge out' get buffer class info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_INFO_TAG
{
	PVRSRV_ERROR		eError;
	BUFFER_INFO			sBufferInfo;

} PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_INFO;


/******************************************************************************
 *	'bridge in' get buffer class buffer
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_BUFFER_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDeviceKM;
	IMG_UINT32 ui32BufferIndex;

} PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_BUFFER;


/******************************************************************************
 *	'bridge out' get buffer class buffer
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_BUFFER_TAG
{
	PVRSRV_ERROR eError;
	IMG_HANDLE			hBuffer;

} PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_BUFFER;


/******************************************************************************
 *	'bridge out' get heap info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_DEVMEM_HEAPINFO_TAG
{
	PVRSRV_ERROR		eError;
	IMG_UINT32			ui32ClientHeapCount;
	PVRSRV_HEAP_INFO	sHeapInfo[PVRSRV_MAX_CLIENT_HEAPS];

} PVRSRV_BRIDGE_OUT_GET_DEVMEM_HEAPINFO;


/******************************************************************************
 *	'bridge out' create device memory context
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_CREATE_DEVMEMCONTEXT_TAG
{
	PVRSRV_ERROR		eError;
	IMG_HANDLE			hDevMemContext;
	IMG_UINT32			ui32ClientHeapCount;
	PVRSRV_HEAP_INFO	sHeapInfo[PVRSRV_MAX_CLIENT_HEAPS];

} PVRSRV_BRIDGE_OUT_CREATE_DEVMEMCONTEXT;


/******************************************************************************
 *	'bridge out' create device memory context
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_CREATE_DEVMEMHEAP_TAG
{
	PVRSRV_ERROR		eError;
	IMG_HANDLE			hDevMemHeap;

} PVRSRV_BRIDGE_OUT_CREATE_DEVMEMHEAP;


/******************************************************************************
 *	'bridge out' alloc device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_ALLOCDEVICEMEM_TAG
{
	PVRSRV_ERROR            eError;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;

} PVRSRV_BRIDGE_OUT_ALLOCDEVICEMEM;


/******************************************************************************
 *	'bridge out' export device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_EXPORTDEVICEMEM_TAG
{
	PVRSRV_ERROR			eError;
	IMG_HANDLE				hMemInfo;
#if defined(SUPPORT_MEMINFO_IDS)
	IMG_UINT64				ui64Stamp;
#endif

} PVRSRV_BRIDGE_OUT_EXPORTDEVICEMEM;


/******************************************************************************
 *	'bridge out' map ion handle
 *****************************************************************************/
typedef struct _PVRSRV_BRIDGE_OUT_MAP_ION_HANDLE_
{
	PVRSRV_ERROR            eError;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;
	IMG_SIZE_T				uiIonBufferSize;

} PVRSRV_BRIDGE_OUT_MAP_ION_HANDLE;


/******************************************************************************
 *	'bridge out' map meminfo to user mode
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_MAPMEMINFOTOUSER_TAG
{
	PVRSRV_ERROR			eError;
	IMG_PVOID				pvLinAddr;
	IMG_HANDLE				hMappingInfo;

}PVRSRV_BRIDGE_OUT_MAPMEMINFOTOUSER;


/******************************************************************************
 *	'bridge out' get free device memory
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GETFREEDEVICEMEM_TAG
{
	PVRSRV_ERROR eError;
	IMG_SIZE_T uTotal;
	IMG_SIZE_T uFree;
	IMG_SIZE_T uLargestBlock;

} PVRSRV_BRIDGE_OUT_GETFREEDEVICEMEM;


//#ifdef LINUX
/******************************************************************************
 *	'bridge out' get full map data
 *****************************************************************************/
#include "pvrmmap.h"
typedef struct PVRSRV_BRIDGE_OUT_MHANDLE_TO_MMAP_DATA_TAG
{
    PVRSRV_ERROR		eError;

    /* This is a the offset you should pass to mmap(2) so that
     * the driver can look up the full details for the mapping
     * request. */
     IMG_UINTPTR_T		uiMMapOffset;

    /* This is the byte offset you should add to the mapping you
     * get from mmap */
    IMG_UINTPTR_T		uiByteOffset;

    /* This is the real size of the mapping that will be created
     * which should be passed to mmap _and_ munmap. */
    IMG_SIZE_T 			uiRealByteSize;

    /* User mode address associated with mapping */
    IMG_UINTPTR_T       uiUserVAddr;

} PVRSRV_BRIDGE_OUT_MHANDLE_TO_MMAP_DATA;

typedef struct PVRSRV_BRIDGE_OUT_RELEASE_MMAP_DATA_TAG
{
    PVRSRV_ERROR		eError;

    /* Flag that indicates whether the mapping should be destroyed */
    IMG_BOOL			bMUnmap;

    /* User mode address associated with mapping */
    IMG_UINTPTR_T		uiUserVAddr;

    /* Size of mapping */
    IMG_SIZE_T			uiRealByteSize;
} PVRSRV_BRIDGE_OUT_RELEASE_MMAP_DATA;
//#endif


/******************************************************************************
 *	'bridge in' get misc info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_MISC_INFO_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_MISC_INFO	sMiscInfo;

}PVRSRV_BRIDGE_IN_GET_MISC_INFO;


/******************************************************************************
 *	'bridge out' get misc info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GET_MISC_INFO_TAG
{
	PVRSRV_ERROR		eError;
	PVRSRV_MISC_INFO	sMiscInfo;

}PVRSRV_BRIDGE_OUT_GET_MISC_INFO;


/******************************************************************************
 *	'bridge in' get misc info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_RELEASE_MISC_INFO_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_MISC_INFO	sMiscInfo;

}PVRSRV_BRIDGE_IN_RELEASE_MISC_INFO;


/******************************************************************************
 *	'bridge out' get misc info
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_RELEASE_MISC_INFO_TAG
{
	PVRSRV_ERROR		eError;
	PVRSRV_MISC_INFO	sMiscInfo;

}PVRSRV_BRIDGE_OUT_RELEASE_MISC_INFO;


/******************************************************************************
 *	'bridge out' PDUMP is capturing
 *****************************************************************************/

typedef struct PVRSRV_BRIDGE_OUT_PDUMP_ISCAPTURING_TAG
{
	PVRSRV_ERROR eError;
	IMG_BOOL bIsCapturing;

} PVRSRV_BRIDGE_OUT_PDUMP_ISCAPTURING;

/******************************************************************************
 *	'bridge in' get FB mem stats
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_GET_FB_STATS_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_SIZE_T uTotal;
	IMG_SIZE_T uAvailable;

} PVRSRV_BRIDGE_IN_GET_FB_STATS;


/******************************************************************************
 *	'bridge in' Map CPU Physical to User Space
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_MAPPHYSTOUSERSPACE_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_SYS_PHYADDR		sSysPhysAddr;
	IMG_UINT32			uiSizeInBytes;

} PVRSRV_BRIDGE_IN_MAPPHYSTOUSERSPACE;


/******************************************************************************
 *	'bridge out' Map CPU Physical to User Space
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_MAPPHYSTOUSERSPACE_TAG
{
	IMG_PVOID			pvUserAddr;
	IMG_UINT32			uiActualSize;
	IMG_PVOID			pvProcess;

} PVRSRV_BRIDGE_OUT_MAPPHYSTOUSERSPACE;


/******************************************************************************
 *	'bridge in' Unmap CPU Physical to User Space
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNMAPPHYSTOUSERSPACE_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_PVOID			pvUserAddr;
	IMG_PVOID			pvProcess;

} PVRSRV_BRIDGE_IN_UNMAPPHYSTOUSERSPACE;


/******************************************************************************
 *	'bridge out' Get user space pointer to Phys to Lin lookup table
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_GETPHYSTOUSERSPACEMAP_TAG
{
	IMG_PVOID			*ppvTbl;
	IMG_UINT32			uiTblSize;

} PVRSRV_BRIDGE_OUT_GETPHYSTOUSERSPACEMAP;


/******************************************************************************
 *	'bridge in' Register RTSIM process thread
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_REGISTER_SIM_PROCESS_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_PVOID			pvProcess;

} PVRSRV_BRIDGE_IN_REGISTER_SIM_PROCESS;


/******************************************************************************
 *	'bridge out' Register RTSIM process thread
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_OUT_REGISTER_SIM_PROCESS_TAG
{
	IMG_SYS_PHYADDR		sRegsPhysBase;			/*!< Physical address of current device register */
	IMG_VOID			*pvRegsBase;			/*!< User mode linear address of SGX device registers */
	IMG_PVOID			pvProcess;
	IMG_UINT32			ulNoOfEntries;
	IMG_PVOID			pvTblLinAddr;

} PVRSRV_BRIDGE_OUT_REGISTER_SIM_PROCESS;


/******************************************************************************
 *	'bridge in' Unregister RTSIM process thread
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_UNREGISTER_SIM_PROCESS_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_PVOID			pvProcess;
	IMG_VOID			*pvRegsBase;			/*!< User mode linear address of SGX device registers */

} PVRSRV_BRIDGE_IN_UNREGISTER_SIM_PROCESS;

/******************************************************************************
 *	'bridge in' process simulator ISR event
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_PROCESS_SIMISR_EVENT_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE			hDevCookie;
	IMG_UINT32			ui32StatusAndMask;
	PVRSRV_ERROR 		eError;

} PVRSRV_BRIDGE_IN_PROCESS_SIMISR_EVENT;

/******************************************************************************
 *	'bridge in' initialisation server disconnect
 *****************************************************************************/
typedef struct PVRSRV_BRIDGE_IN_INITSRV_DISCONNECT_TAG
{
	IMG_UINT32			ui32BridgeFlags; /* Must be first member of structure */
	IMG_BOOL			bInitSuccesful;
} PVRSRV_BRIDGE_IN_INITSRV_DISCONNECT;


typedef struct PVRSRV_BRIDGE_IN_ALLOC_SHARED_SYS_MEM_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
    IMG_UINT32 ui32Flags;
    IMG_SIZE_T uSize;
}PVRSRV_BRIDGE_IN_ALLOC_SHARED_SYS_MEM;

typedef struct PVRSRV_BRIDGE_OUT_ALLOC_SHARED_SYS_MEM_TAG
{
	PVRSRV_ERROR            eError;
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
}PVRSRV_BRIDGE_OUT_ALLOC_SHARED_SYS_MEM;

typedef struct PVRSRV_BRIDGE_IN_FREE_SHARED_SYS_MEM_TAG
{
	IMG_UINT32              ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo;
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
}PVRSRV_BRIDGE_IN_FREE_SHARED_SYS_MEM;

typedef struct PVRSRV_BRIDGE_OUT_FREE_SHARED_SYS_MEM_TAG
{
	PVRSRV_ERROR eError;
}PVRSRV_BRIDGE_OUT_FREE_SHARED_SYS_MEM;

typedef struct PVRSRV_BRIDGE_IN_MAP_MEMINFO_MEM_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelMemInfo;
}PVRSRV_BRIDGE_IN_MAP_MEMINFO_MEM;

typedef struct PVRSRV_BRIDGE_OUT_MAP_MEMINFO_MEM_TAG
{
	PVRSRV_CLIENT_MEM_INFO  sClientMemInfo;
	PVRSRV_CLIENT_SYNC_INFO sClientSyncInfo;
	PVRSRV_KERNEL_MEM_INFO  *psKernelMemInfo;
	PVRSRV_ERROR eError;
}PVRSRV_BRIDGE_OUT_MAP_MEMINFO_MEM;

typedef struct PVRSRV_BRIDGE_IN_UNMAP_MEMINFO_MEM_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	PVRSRV_CLIENT_MEM_INFO sClientMemInfo;
}PVRSRV_BRIDGE_IN_UNMAP_MEMINFO_MEM;

typedef struct PVRSRV_BRIDGE_OUT_UNMAP_MEMINFO_MEM_TAG
{
	PVRSRV_ERROR eError;
}PVRSRV_BRIDGE_OUT_UNMAP_MEMINFO_MEM;

typedef struct PVRSRV_BRIDGE_IN_EVENT_OBJECT_WAI_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE	hOSEventKM;
} PVRSRV_BRIDGE_IN_EVENT_OBJECT_WAIT;

typedef struct PVRSRV_BRIDGE_IN_EVENT_OBJECT_OPEN_TAG
{
	PVRSRV_EVENTOBJECT sEventObject;
} PVRSRV_BRIDGE_IN_EVENT_OBJECT_OPEN;

typedef struct	PVRSRV_BRIDGE_OUT_EVENT_OBJECT_OPEN_TAG
{
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_EVENT_OBJECT_OPEN;

typedef struct PVRSRV_BRIDGE_IN_EVENT_OBJECT_CLOSE_TAG
{
	PVRSRV_EVENTOBJECT sEventObject;
	IMG_HANDLE hOSEventKM;
} PVRSRV_BRIDGE_IN_EVENT_OBJECT_CLOSE;

typedef struct PVRSRV_BRIDGE_OUT_CREATE_SYNC_INFO_MOD_OBJ_TAG
{
	PVRSRV_ERROR eError;

	IMG_HANDLE hKernelSyncInfoModObj;

} PVRSRV_BRIDGE_OUT_CREATE_SYNC_INFO_MOD_OBJ;

typedef struct PVRSRV_BRIDGE_IN_DESTROY_SYNC_INFO_MOD_OBJ
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfoModObj;
} PVRSRV_BRIDGE_IN_DESTROY_SYNC_INFO_MOD_OBJ;

typedef struct PVRSRV_BRIDGE_IN_MODIFY_PENDING_SYNC_OPS_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfoModObj;
	IMG_HANDLE hKernelSyncInfo;
	IMG_UINT32 ui32ModifyFlags;

} PVRSRV_BRIDGE_IN_MODIFY_PENDING_SYNC_OPS;

typedef struct PVRSRV_BRIDGE_IN_MODIFY_COMPLETE_SYNC_OPS_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfoModObj;
} PVRSRV_BRIDGE_IN_MODIFY_COMPLETE_SYNC_OPS;

typedef struct PVRSRV_BRIDGE_OUT_MODIFY_PENDING_SYNC_OPS_TAG
{
	PVRSRV_ERROR eError;

	/* The following variable are used to return the PRE-INCREMENTED op vals */
	IMG_UINT32 ui32ReadOpsPending;
	IMG_UINT32 ui32WriteOpsPending;
	IMG_UINT32 ui32ReadOps2Pending;

} PVRSRV_BRIDGE_OUT_MODIFY_PENDING_SYNC_OPS;

typedef struct PVRSRV_BRIDGE_IN_SYNC_OPS_TAKE_TOKEN_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfo;

} PVRSRV_BRIDGE_IN_SYNC_OPS_TAKE_TOKEN;

typedef struct PVRSRV_BRIDGE_OUT_SYNC_OPS_TAKE_TOKEN_TAG
{
	PVRSRV_ERROR eError;

	IMG_UINT32 ui32ReadOpsPending;
	IMG_UINT32 ui32WriteOpsPending;
	IMG_UINT32 ui32ReadOps2Pending;

} PVRSRV_BRIDGE_OUT_SYNC_OPS_TAKE_TOKEN;

typedef struct PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_TOKEN_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfo;
	IMG_UINT32 ui32ReadOpsPendingSnapshot;
	IMG_UINT32 ui32WriteOpsPendingSnapshot;
	IMG_UINT32 ui32ReadOps2PendingSnapshot;
} PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_TOKEN;

typedef struct PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_MOD_OBJ_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfoModObj;
} PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_MOD_OBJ;

typedef struct PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_DELTA_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */
	IMG_HANDLE hKernelSyncInfo;
	IMG_UINT32 ui32Delta;
} PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_DELTA;

typedef struct PVRSRV_BRIDGE_IN_ALLOC_SYNC_INFO_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */

	IMG_HANDLE hDevCookie;
} PVRSRV_BRIDGE_IN_ALLOC_SYNC_INFO;

typedef struct PVRSRV_BRIDGE_OUT_ALLOC_SYNC_INFO_TAG
{
	PVRSRV_ERROR eError;

	IMG_HANDLE hKernelSyncInfo;
} PVRSRV_BRIDGE_OUT_ALLOC_SYNC_INFO;

typedef struct PVRSRV_BRIDGE_IN_FREE_SYNC_INFO_TAG
{
	IMG_UINT32 ui32BridgeFlags; /* Must be first member of structure */

	IMG_HANDLE hKernelSyncInfo;
} PVRSRV_BRIDGE_IN_FREE_SYNC_INFO;

typedef struct PVRSRV_BRIDGE_IN_CHG_DEV_MEM_ATTRIBS_TAG
{
	IMG_SID				hKernelMemInfo;
	IMG_UINT32			ui32Attribs;
} PVRSRV_BRIDGE_IN_CHG_DEV_MEM_ATTRIBS;


#if defined (__cplusplus)
}
#endif

#endif /* __PVR_BRIDGE_H__ */

/******************************************************************************
 End of file (pvr_bridge.h)
******************************************************************************/

