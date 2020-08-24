/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#ifndef __OSDEP_CE_SERVICE_H_
#define __OSDEP_CE_SERVICE_H_


#include <ndis.h>
#include <ntddndis.h>

#ifdef CONFIG_SDIO_HCI
#include "SDCardDDK.h"
#endif

#ifdef CONFIG_USB_HCI
#include <usbdi.h>
#endif

typedef HANDLE 	_sema;
typedef	LIST_ENTRY	_list;
typedef NDIS_STATUS _OS_STATUS;

typedef NDIS_SPIN_LOCK	_lock;

typedef HANDLE 		_rwlock; //Mutex

typedef u32	_irqL;

typedef NDIS_HANDLE  _nic_hdl;

struct rtw_timer_list {
	NDIS_MINIPORT_TIMER ndis_timer;
	void (*function)(void *);
	void *arg;
};

struct	__queue	{
	LIST_ENTRY	queue;
	_lock	lock;
};

typedef	NDIS_PACKET	_pkt;
typedef NDIS_BUFFER	_buffer;
typedef struct	__queue	_queue;

typedef HANDLE 	_thread_hdl_;
typedef DWORD thread_return;
typedef void*	thread_context;
typedef NDIS_WORK_ITEM _workitem;



#define SEMA_UPBND	(0x7FFFFFFF)   //8192

__inline static _list *get_prev(_list	*list)
{
	return list->Blink;
}
	
__inline static _list *get_next(_list	*list)
{
	return list->Flink;
}

__inline static _list	*get_list_head(_queue	*queue)
{
	return (&(queue->queue));
}

#define LIST_CONTAINOR(ptr, type, member) CONTAINING_RECORD(ptr, type, member)

__inline static void _enter_critical(_lock *plock, _irqL *pirqL)
{
	NdisAcquireSpinLock(plock);
}

__inline static void _exit_critical(_lock *plock, _irqL *pirqL)
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


__inline static void _enter_hwio_critical(_rwlock *prwlock, _irqL *pirqL)
{
	WaitForSingleObject(*prwlock, INFINITE );

}

__inline static void _exit_hwio_critical(_rwlock *prwlock, _irqL *pirqL)
{
	ReleaseMutex(*prwlock);
}

__inline static void rtw_list_delete(_list *plist)
{
	RemoveEntryList(plist);
	InitializeListHead(plist);
}

static inline void timer_hdl(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3)
{
	_timer *timer = (_timer *)FunctionContext;

	timer->function(timer->arg);
}

static inline void _init_timer(_timer *ptimer, _nic_hdl nic_hdl, void *pfunc, void *cntx)
{
	ptimer->function = pfunc;
	ptimer->arg = cntx;
	NdisMInitializeTimer(&ptimer->ndis_timer, nic_hdl, timer_hdl, ptimer);
}

static inline void _set_timer(_timer *ptimer, u32 delay_time)
{
	NdisMSetTimer(ptimer, delay_time);
}

static inline void _cancel_timer(_timer *ptimer, u8 *bcancelled)
{
	NdisMCancelTimer(ptimer, bcancelled);
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

