/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __RT_LINUX_CMM_H__
#define __RT_LINUX_CMM_H__


typedef struct _OS_RSTRUC  {
	UCHAR *pContent; /* pointer to real structure content */
} OS_RSTRUC;


/* declare new chipset function here */
#ifdef OS_ABL_FUNC_SUPPORT

#define RTMP_DECLARE_DRV_OPS_FUNCTION(_func)					\
	void Rtmp_Drv_Ops_##_func(VOID *__pDrvOps, VOID *__pNetOps, \
						VOID *__pPciConfig, VOID *__pUsbConfig)

#define RTMP_BUILD_DRV_OPS_FUNCTION(_func)						\
void Rtmp_Drv_Ops_##_func(VOID *__pDrvOps, VOID *__pNetOps, 	\
					VOID *__pPciConfig, VOID *__pUsbConfig)		\
{																\
	RtmpDrvOpsInit(__pDrvOps, __pNetOps, __pPciConfig, __pUsbConfig);\
}

#define RTMP_GET_DRV_OPS_FUNCTION(_func)						\
	(PVOID)Rtmp_Drv_Ops_##_func

#define RTMP_DRV_OPS_FUNCTION_BODY(_func)						\
	Rtmp_Drv_Ops_##_func


#define xdef_to_str(s)   def_to_str(s) 
#define def_to_str(s)    #s






#ifdef RT3070
#define RTMP_DRV_NAME	"rt3070" xdef_to_str(RT28xx_MODE)
#define RT_CHIPSET		3070
RTMP_DECLARE_DRV_OPS_FUNCTION(3070);
#define RTMP_DRV_OPS_FUNCTION				RTMP_DRV_OPS_FUNCTION_BODY(3070)
#define RTMP_BUILD_DRV_OPS_FUNCTION_BODY	RTMP_BUILD_DRV_OPS_FUNCTION(3070)
#endif /* RT3070 */




#else

#ifdef RTMP_MAC_USB
#define RTMP_DRV_NAME	"rt2870"
#else
#define RTMP_DRV_NAME	"rt2860"
#endif /* RTMP_MAC_USB */

#endif /* OS_ABL_FUNC_SUPPORT */


/*****************************************************************************
 *	OS task related data structure and definitions
 ******************************************************************************/
#define RTMP_OS_TASK_INIT(__pTask, __pTaskName, __pAd)		\
	RtmpOSTaskInit(__pTask, __pTaskName, __pAd, &(__pAd)->RscTaskMemList, &(__pAd)->RscSemMemList);

#ifndef OS_ABL_FUNC_SUPPORT

/* rt_linux.h */
#define RTMP_OS_TASK				OS_TASK

#define RTMP_OS_TASK_GET(__pTask)							\
	(__pTask)

#define RTMP_OS_TASK_DATA_GET(__pTask)						\
	((__pTask)->priv)

#define RTMP_OS_TASK_IS_KILLED(__pTask)						\
	((__pTask)->task_killed)

#ifdef KTHREAD_SUPPORT
#define RTMP_OS_TASK_WAKE_UP(__pTask)						\
	WAKE_UP(pTask);
#else
#define RTMP_OS_TASK_WAKE_UP(__pTask)						\
	RTMP_SEM_EVENT_UP(&(pTask)->taskSema);
#endif /* KTHREAD_SUPPORT */

#ifdef KTHREAD_SUPPORT
#define RTMP_OS_TASK_LEGALITY(__pTask)						\
	if ((__pTask)->kthread_task != NULL)
#else
#define RTMP_OS_TASK_LEGALITY(__pTask)						\
	CHECK_PID_LEGALITY((__pTask)->taskPID)
#endif /* KTHREAD_SUPPORT */

#else

/* rt_linux_cmm.h */
#define RTMP_OS_TASK				OS_RSTRUC

#define RTMP_OS_TASK_GET(__pTask)							\
	((OS_TASK *)((__pTask)->pContent))

#define RTMP_OS_TASK_DATA_GET(__pTask)						\
	RtmpOsTaskDataGet(__pTask)

#define RTMP_OS_TASK_IS_KILLED(__pTask)						\
	RtmpOsTaskIsKilled(__pTask)

#define RTMP_OS_TASK_WAKE_UP(__pTask)						\
	RtmpOsTaskWakeUp(pTask)

#define RTMP_OS_TASK_LEGALITY(__pTask)						\
	if (RtmpOsCheckTaskLegality(__pTask))

#endif /* OS_ABL_FUNC_SUPPORT */


/*****************************************************************************
 * Timer related definitions and data structures.
 ******************************************************************************/
#ifndef OS_ABL_FUNC_SUPPORT

/* rt_linux.h */
#define NDIS_MINIPORT_TIMER			OS_NDIS_MINIPORT_TIMER
#define RTMP_OS_TIMER				OS_TIMER

#define RTMP_OS_FREE_TIMER(__pAd)
#define RTMP_OS_FREE_LOCK(__pAd)
#define RTMP_OS_FREE_TASKLET(__pAd)
#define RTMP_OS_FREE_TASK(__pAd)
#define RTMP_OS_FREE_SEM(__pAd)
#define RTMP_OS_FREE_ATOMIC(__pAd)

#else

/* rt_linux_cmm.h */
#define NDIS_MINIPORT_TIMER			OS_RSTRUC
#define RTMP_OS_TIMER				OS_RSTRUC

#define RTMP_OS_FREE_TIMER(__pAd)											\
	DBGPRINT(RT_DEBUG_ERROR, ("%s: free timer resources!\n", __FUNCTION__));\
	RTMP_OS_Free_Rscs(&((__pAd)->RscTimerMemList))
#define RTMP_OS_FREE_TASK(__pAd)											\
	DBGPRINT(RT_DEBUG_ERROR, ("%s: free task resources!\n", __FUNCTION__));	\
	RTMP_OS_Free_Rscs(&((__pAd)->RscTaskMemList))
#define RTMP_OS_FREE_LOCK(__pAd)											\
	DBGPRINT(RT_DEBUG_ERROR, ("%s: free lock resources!\n", __FUNCTION__));	\
	RTMP_OS_Free_Rscs(&((__pAd)->RscLockMemList))
#define RTMP_OS_FREE_TASKLET(__pAd)											\
	DBGPRINT(RT_DEBUG_ERROR, ("%s: free tasklet resources!\n", __FUNCTION__));\
	RTMP_OS_Free_Rscs(&((__pAd)->RscTaskletMemList))
#define RTMP_OS_FREE_SEM(__pAd)												\
	DBGPRINT(RT_DEBUG_ERROR, ("%s: free semaphore resources!\n", __FUNCTION__));\
	RTMP_OS_Free_Rscs(&((__pAd)->RscSemMemList))
#define RTMP_OS_FREE_ATOMIC(__pAd)												\
	DBGPRINT(RT_DEBUG_ERROR, ("%s: free atomic resources!\n", __FUNCTION__));\
	RTMP_OS_Free_Rscs(&((__pAd)->RscAtomicMemList))

#endif /* OS_ABL_FUNC_SUPPORT */


/*****************************************************************************
 *	OS file operation related data structure definitions
 ******************************************************************************/
/* if you add any new type, please also modify RtmpOSFileOpen() */
#define RTMP_FILE_RDONLY			0x0F01
#define RTMP_FILE_WRONLY			0x0F02
#define RTMP_FILE_CREAT				0x0F03
#define RTMP_FILE_TRUNC				0x0F04

#ifndef OS_ABL_FUNC_SUPPORT

/* rt_linux.h */
#define RTMP_OS_FS_INFO				OS_FS_INFO

#else

/* rt_linux_cmm.h */
#define RTMP_OS_FS_INFO				OS_RSTRUC

#endif /* OS_ABL_FUNC_SUPPORT */


/*****************************************************************************
 *	OS semaphore related data structure and definitions
 ******************************************************************************/

#ifndef OS_ABL_FUNC_SUPPORT

#define NDIS_SPIN_LOCK							OS_NDIS_SPIN_LOCK
#define NdisAllocateSpinLock(__pReserved, __pLock)	OS_NdisAllocateSpinLock(__pLock)
#define NdisFreeSpinLock						OS_NdisFreeSpinLock
#define RTMP_SEM_LOCK							OS_SEM_LOCK
#define RTMP_SEM_UNLOCK							OS_SEM_UNLOCK
#define RTMP_IRQ_LOCK							OS_IRQ_LOCK
#define RTMP_IRQ_UNLOCK							OS_IRQ_UNLOCK
#define RTMP_INT_LOCK							OS_INT_LOCK
#define RTMP_INT_UNLOCK							OS_INT_UNLOCK
#define RTMP_OS_SEM								OS_SEM
#define RTMP_OS_ATOMIC							atomic_t

#define NdisAcquireSpinLock						RTMP_SEM_LOCK
#define NdisReleaseSpinLock						RTMP_SEM_UNLOCK

#define RTMP_SEM_EVENT_INIT_LOCKED(__pSema, __pSemaList)	OS_SEM_EVENT_INIT_LOCKED(__pSema)
#define RTMP_SEM_EVENT_INIT(__pSema, __pSemaList)			OS_SEM_EVENT_INIT(__pSema)
#define RTMP_SEM_EVENT_DESTORY					OS_SEM_EVENT_DESTORY
#define RTMP_SEM_EVENT_WAIT						OS_SEM_EVENT_WAIT
#define RTMP_SEM_EVENT_UP						OS_SEM_EVENT_UP

#define RTUSBMlmeUp								OS_RTUSBMlmeUp

#define RTMP_OS_ATMOIC_INIT(__pAtomic, __pAtomicList)
#define RTMP_THREAD_PID_KILL(__PID)				KILL_THREAD_PID(__PID, SIGTERM, 1)

#else

#define NDIS_SPIN_LOCK							OS_RSTRUC
#define RTMP_OS_SEM								OS_RSTRUC
#define RTMP_OS_ATOMIC							OS_RSTRUC

#define RTMP_SEM_EVENT_INIT_LOCKED 				RtmpOsSemaInitLocked
#define RTMP_SEM_EVENT_INIT						RtmpOsSemaInit
#define RTMP_SEM_EVENT_DESTORY					RtmpOsSemaDestory
#define RTMP_SEM_EVENT_WAIT(_pSema, _status)	((_status) = RtmpOsSemaWaitInterruptible((_pSema)))
#define RTMP_SEM_EVENT_UP						RtmpOsSemaWakeUp

#define RTUSBMlmeUp								RtmpOsMlmeUp

#define RTMP_OS_ATMOIC_INIT						RtmpOsAtomicInit
#define RTMP_THREAD_PID_KILL					RtmpThreadPidKill

/* */
/*  spin_lock enhanced for Nested spin lock */
/* */
#define NdisAllocateSpinLock(__pAd, __pLock)		RtmpOsAllocateLock(__pLock, &(__pAd)->RscLockMemList)
#define NdisFreeSpinLock							RtmpOsFreeSpinLock

#define RTMP_SEM_LOCK(__lock)					\
{												\
	RtmpOsSpinLockBh(__lock);					\
}

#define RTMP_SEM_UNLOCK(__lock)					\
{												\
	RtmpOsSpinUnLockBh(__lock);					\
}

/* sample, use semaphore lock to replace IRQ lock, 2007/11/15 */
#ifdef MULTI_CORE_SUPPORT

#define RTMP_IRQ_LOCK(__lock, __irqflags)			\
{													\
	__irqflags = 0;									\
	spin_lock_irqsave((spinlock_t *)(__lock), __irqflags);			\
}

#define RTMP_IRQ_UNLOCK(__lock, __irqflag)			\
{													\
	spin_unlock_irqrestore((spinlock_t *)(__lock), __irqflag);			\
}
#else
#define RTMP_IRQ_LOCK(__lock, __irqflags)		\
{												\
	__irqflags = 0;								\
	RtmpOsSpinLockBh(__lock);					\
}

#define RTMP_IRQ_UNLOCK(__lock, __irqflag)		\
{												\
	RtmpOsSpinUnLockBh(__lock);					\
}
#endif // MULTI_CORE_SUPPORT //
#define RTMP_INT_LOCK(__Lock, __Flag)	RtmpOsIntLock(__Lock, &__Flag)
#define RTMP_INT_UNLOCK					RtmpOsIntUnLock

#define NdisAcquireSpinLock				RTMP_SEM_LOCK
#define NdisReleaseSpinLock				RTMP_SEM_UNLOCK

#endif /* OS_ABL_FUNC_SUPPORT */


/*****************************************************************************
 *	OS task related data structure and definitions
 ******************************************************************************/

#ifndef OS_ABL_FUNC_SUPPORT

/* rt_linux.h */
#define RTMP_NET_TASK_STRUCT		OS_NET_TASK_STRUCT
#define PRTMP_NET_TASK_STRUCT		POS_NET_TASK_STRUCT

#ifdef WORKQUEUE_BH	
#define RTMP_OS_TASKLET_SCHE(__pTasklet)							\
		schedule_work(__pTasklet)
#define RTMP_OS_TASKLET_INIT(__pAd, __pTasklet, __pFunc)			\
		INIT_WORK(__pTasklet, __pFunc)
#define RTMP_OS_TASKLET_KILL(__pTasklet)
#else
#define RTMP_OS_TASKLET_SCHE(__pTasklet)							\
		tasklet_hi_schedule(__pTasklet)
#define RTMP_OS_TASKLET_INIT(__pAd, __pTasklet, __pFunc, __Data)	\
		tasklet_init(__pTasklet, __pFunc, __Data)
#define RTMP_OS_TASKLET_KILL(__pTasklet)							\
		tasklet_kill(__pTasklet)
#endif /* WORKQUEUE_BH */

#define RTMP_NET_TASK_DATA_ASSIGN(__Tasklet, __Data)		\
	(__Tasklet)->data = (unsigned long)__Data

#else

/* rt_linux_cmm.h */
typedef OS_RSTRUC					RTMP_NET_TASK_STRUCT;
typedef OS_RSTRUC					*PRTMP_NET_TASK_STRUCT;

#define RTMP_OS_TASKLET_SCHE(__pTasklet)					\
		RtmpOsTaskletSche(__pTasklet)

#define RTMP_OS_TASKLET_INIT(__pAd, __pTasklet, __pFunc, __Data)	\
		RtmpOsTaskletInit(__pTasklet, __pFunc, __Data, &(__pAd)->RscTaskletMemList)

#define RTMP_OS_TASKLET_KILL(__pTasklet)					\
		RtmpOsTaskletKill(__pTasklet)

#define RTMP_NET_TASK_DATA_ASSIGN(__pTasklet, __Data)		\
		RtmpOsTaskletDataAssign(__pTasklet, __Data)

#endif /* OS_ABL_FUNC_SUPPORT */




/*****************************************************************************
 *	OS definition related data structure and definitions
 ******************************************************************************/

#ifdef OS_ABL_SUPPORT

#define RTMP_USB_CONTROL_MSG_ENODEV		-1
#define RTMP_USB_CONTROL_MSG_FAIL		-2

typedef struct __RTMP_PCI_CONFIG {

	UINT32	ConfigVendorID;
} RTMP_PCI_CONFIG;

typedef struct __RTMP_USB_CONFIG {

	UINT32	Reserved;
} RTMP_USB_CONFIG;

extern RTMP_PCI_CONFIG *pRtmpPciConfig;
extern RTMP_USB_CONFIG *pRtmpUsbConfig;

#define RTMP_OS_PCI_VENDOR_ID			pRtmpPciConfig->ConfigVendorID

/*
	Declare dma_addr_t here, can not define it in rt_drv.h

	If you define it in include/os/rt_drv.h, then the size in DRIVER module
	will be 64-bit, but in UTIL/NET modules, it maybe 32-bit.
	This will cause size mismatch problem when OS_ABL = yes.
*/
#define ra_dma_addr_t					unsigned long long

#else

#ifdef RTMP_USB_SUPPORT
#define RTMP_USB_CONTROL_MSG_ENODEV		(-ENODEV)
#define RTMP_USB_CONTROL_MSG_FAIL		(-EFAULT)
#endif /* RTMP_USB_SUPPORT */

#define RTMP_OS_PCI_VENDOR_ID			PCI_VENDOR_ID

#define ra_dma_addr_t					dma_addr_t

#endif /* OS_ABL_SUPPORT */

#define PCI_MAP_SINGLE					RtmpDrvPciMapSingle


/***********************************************************************************
 *	Others
 ***********************************************************************************/
#define APCLI_IF_UP_CHECK(pAd, ifidx) (RtmpOSNetDevIsUp((pAd)->ApCfg.ApCliTab[(ifidx)].dev) == TRUE)


#endif /* __RT_LINUX_CMM_H__ */

/* End of rt_linux_cmm.h */
