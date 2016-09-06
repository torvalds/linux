/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __OSDEP_LINUX_SERVICE_H_
#define __OSDEP_LINUX_SERVICE_H_

	#include <ndis.h>
	#include <ntddk.h>
	#include <ntddndis.h>
	#include <ntdef.h>

#ifdef CONFIG_USB_HCI
	#include <usb.h>
	#include <usbioctl.h>
	#include <usbdlib.h>
#endif

	typedef KSEMAPHORE 	_sema;
	typedef	LIST_ENTRY	_list;
	typedef NDIS_STATUS _OS_STATUS;
	

	typedef NDIS_SPIN_LOCK	_lock;

	typedef KMUTEX 			_mutex;

	typedef KIRQL	_irqL;

	// USB_PIPE for WINCE , but handle can be use just integer under windows
	typedef NDIS_HANDLE  _nic_hdl;


	typedef NDIS_MINIPORT_TIMER    _timer;

	struct	__queue	{
		LIST_ENTRY	queue;	
		_lock	lock;
	};

	typedef	NDIS_PACKET	_pkt;
	typedef NDIS_BUFFER	_buffer;
	typedef struct	__queue	_queue;
	
	typedef PKTHREAD _thread_hdl_;
	typedef void	thread_return;
	typedef void* thread_context;

	typedef NDIS_WORK_ITEM _workitem;

	#define thread_exit() PsTerminateSystemThread(STATUS_SUCCESS);

	#define HZ			10000000
	#define SEMA_UPBND	(0x7FFFFFFF)   //8192
	
__inline static _list *get_next(_list	*list)
{
	return list->Flink;
}	

__inline static _list	*get_list_head(_queue	*queue)
{
	return (&(queue->queue));
}
	

#define LIST_CONTAINOR(ptr, type, member) CONTAINING_RECORD(ptr, type, member)
     

__inline static _enter_critical(_lock *plock, _irqL *pirqL)
{
	NdisAcquireSpinLock(plock);	
}

__inline static _exit_critical(_lock *plock, _irqL *pirqL)
{
	NdisReleaseSpinLock(plock);	
}


__inline static _enter_critical_ex(_lock *plock, _irqL *pirqL)
{
	NdisDprAcquireSpinLock(plock);	
}

__inline static _exit_critical_ex(_lock *plock, _irqL *pirqL)
{
	NdisDprReleaseSpinLock(plock);	
}

__inline static void _enter_critical_bh(_lock *plock, _irqL *pirqL)
{
	NdisDprAcquireSpinLock(plock);
}

__inline static void _exit_critical_bh(_lock *plock, _irqL *pirqL)
{
	NdisDprReleaseSpinLock(plock);
}

__inline static _enter_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	KeWaitForSingleObject(pmutex, Executive, KernelMode, FALSE, NULL);
}


__inline static _exit_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	KeReleaseMutex(pmutex, FALSE);
}


__inline static void rtw_list_delete(_list *plist)
{
	RemoveEntryList(plist);
	InitializeListHead(plist);	
}

#define RTW_TIMER_HDL_ARGS IN PVOID SystemSpecific1, IN PVOID FunctionContext, IN PVOID SystemSpecific2, IN PVOID SystemSpecific3

__inline static void _init_timer(_timer *ptimer,_nic_hdl nic_hdl,void *pfunc,PVOID cntx)
{
	NdisMInitializeTimer(ptimer, nic_hdl, pfunc, cntx);
}

__inline static void _set_timer(_timer *ptimer,u32 delay_time)
{	
 	NdisMSetTimer(ptimer,delay_time);	
}

__inline static void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
	NdisMCancelTimer(ptimer,bcancelled);
}

__inline static void _init_workitem(_workitem *pwork, void *pfunc, PVOID cntx)
{

	NdisInitializeWorkItem(pwork, pfunc, cntx);
}

__inline static void _set_workitem(_workitem *pwork)
{
	NdisScheduleWorkItem(pwork);
}


#define ATOMIC_INIT(i)  { (i) }

//
// Global Mutex: can only be used at PASSIVE level.
//

#define ACQUIRE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
    while (NdisInterlockedIncrement((PULONG)&(_MutexCounter)) != 1)\
    {                                                           \
        NdisInterlockedDecrement((PULONG)&(_MutexCounter));        \
        NdisMSleep(10000);                          \
    }                                                           \
}

#define RELEASE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
    NdisInterlockedDecrement((PULONG)&(_MutexCounter));              \
}

// limitation of path length
#define PATH_LENGTH_MAX MAX_PATH

//Atomic integer operations
#define ATOMIC_T LONG


#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ""
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) ""
#define FUNC_NDEV_FMT "%s"
#define FUNC_NDEV_ARG(ndev) __func__
#define FUNC_ADPT_FMT "%s"
#define FUNC_ADPT_ARG(adapter) __func__

#define STRUCT_PACKED

#endif

