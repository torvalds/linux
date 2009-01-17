/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  Linux kernel module as wrapper of EPL API layer,
                i.e. counterpart to a Linux application

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: EplApiLinuxKernel.c,v $

                $Author: D.Krueger $

                $Revision: 1.9 $  $Date: 2008/11/21 09:00:38 $

                $State: Exp $

                Build Environment:
                GNU-Compiler for m68k

  -------------------------------------------------------------------------

  Revision History:

  2006/10/11 d.k.:  Initial Version
  2008/04/10 m.u.:  Changed to new char driver init

****************************************************************************/

// kernel modul and driver

//#include <linux/version.h>
//#include <linux/config.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/types.h>

//#include <linux/module.h>
//#include <linux/kernel.h>
//#include <linux/init.h>
//#include <linux/errno.h>

// scheduling
#include <linux/sched.h>

// memory access
#include <asm/uaccess.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_DEVFS_FS
#include <linux/major.h>
#include <linux/devfs_fs_kernel.h>
#endif

#include "Epl.h"
#include "EplApiLinux.h"
//#include "kernel/EplPdokCal.h"
#include "proc_fs.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    // remove ("make invisible") obsolete symbols for kernel versions 2.6
    // and higher
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#define EXPORT_NO_SYMBOLS
#else
#error "This driver needs a 2.6.x kernel or higher"
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

// Metainformation
MODULE_LICENSE("Dual BSD/GPL");
#ifdef MODULE_AUTHOR
MODULE_AUTHOR("Daniel.Krueger@SYSTEC-electronic.com");
MODULE_DESCRIPTION("EPL API driver");
#endif

//---------------------------------------------------------------------------
//  Configuration
//---------------------------------------------------------------------------

#define EPLLIN_DRV_NAME     "systec_epl"	// used for <register_chrdev>

//---------------------------------------------------------------------------
//  Constant definitions
//---------------------------------------------------------------------------

// TracePoint support for realtime-debugging
#ifdef _DBG_TRACE_POINTS_
void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p);
#define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
#else
#define TGT_DBG_SIGNAL_TRACE_POINT(p)
#endif

#define EVENT_STATE_INIT        0
#define EVENT_STATE_IOCTL       1	// ioctl entered and ready to receive EPL event
#define EVENT_STATE_READY       2	// EPL event can be forwarded to user application
#define EVENT_STATE_TERM        3	// terminate processing

#define EPL_STATE_NOTOPEN       0
#define EPL_STATE_NOTINIT       1
#define EPL_STATE_RUNNING       2
#define EPL_STATE_SHUTDOWN      3

//---------------------------------------------------------------------------
//  Global variables
//---------------------------------------------------------------------------

#ifdef CONFIG_DEVFS_FS

    // driver major number
static int nDrvMajorNumber_g;

#else

    // device number (major and minor)
static dev_t nDevNum_g;
static struct cdev *pEpl_cdev_g;

#endif

static volatile unsigned int uiEplState_g = EPL_STATE_NOTOPEN;

static struct semaphore SemaphoreCbEvent_g;	// semaphore for EplLinCbEvent
static wait_queue_head_t WaitQueueCbEvent_g;	// wait queue EplLinCbEvent
static wait_queue_head_t WaitQueueProcess_g;	// wait queue for EplApiProcess (user process)
static wait_queue_head_t WaitQueueRelease_g;	// wait queue for EplLinRelease
static atomic_t AtomicEventState_g = ATOMIC_INIT(EVENT_STATE_INIT);
static tEplApiEventType EventType_g;	// event type (enum)
static tEplApiEventArg *pEventArg_g;	// event argument (union)
static tEplKernel RetCbEvent_g;	// return code from event callback function
static wait_queue_head_t WaitQueueCbSync_g;	// wait queue EplLinCbSync
static wait_queue_head_t WaitQueuePI_In_g;	// wait queue for EplApiProcessImageExchangeIn (user process)
static atomic_t AtomicSyncState_g = ATOMIC_INIT(EVENT_STATE_INIT);

//---------------------------------------------------------------------------
//  Local types
//---------------------------------------------------------------------------

typedef struct {
	void *m_pUserArg;
	void *m_pData;

} tEplLinSdoBufHeader;

//---------------------------------------------------------------------------
//  Local variables
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Prototypes of internal functions
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplLinCbEvent(tEplApiEventType EventType_p,	// IN: event type (enum)
				tEplApiEventArg * pEventArg_p,	// IN: event argument (union)
				void GENERIC * pUserArg_p);

tEplKernel PUBLIC EplLinCbSync(void);

static int __init EplLinInit(void);
static void __exit EplLinExit(void);

static int EplLinOpen(struct inode *pDeviceFile_p, struct file *pInstance_p);
static int EplLinRelease(struct inode *pDeviceFile_p, struct file *pInstance_p);
static ssize_t EplLinRead(struct file *pInstance_p, char *pDstBuff_p,
			  size_t BuffSize_p, loff_t * pFileOffs_p);
static ssize_t EplLinWrite(struct file *pInstance_p, const char *pSrcBuff_p,
			   size_t BuffSize_p, loff_t * pFileOffs_p);
static int EplLinIoctl(struct inode *pDeviceFile_p, struct file *pInstance_p,
		       unsigned int uiIoctlCmd_p, unsigned long ulArg_p);

//---------------------------------------------------------------------------
//  Kernel Module specific Data Structures
//---------------------------------------------------------------------------

EXPORT_NO_SYMBOLS;

module_init(EplLinInit);
module_exit(EplLinExit);

static struct file_operations EplLinFileOps_g = {
	.owner = THIS_MODULE,
	.open = EplLinOpen,
	.release = EplLinRelease,
	.read = EplLinRead,
	.write = EplLinWrite,
	.ioctl = EplLinIoctl,

};

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Initailize Driver
//---------------------------------------------------------------------------
//  -> insmod driver
//---------------------------------------------------------------------------

static int __init EplLinInit(void)
{

	tEplKernel EplRet;
	int iErr;
	int iRet;
#ifdef CONFIG_DEVFS_FS
	int nMinorNumber;
#endif

	TRACE0("EPL: + EplLinInit...\n");
	TRACE2("EPL:   Driver build: %s / %s\n", __DATE__, __TIME__);

	iRet = 0;

	// initialize global variables
	atomic_set(&AtomicEventState_g, EVENT_STATE_INIT);
	sema_init(&SemaphoreCbEvent_g, 1);
	init_waitqueue_head(&WaitQueueCbEvent_g);
	init_waitqueue_head(&WaitQueueProcess_g);
	init_waitqueue_head(&WaitQueueRelease_g);

#ifdef CONFIG_DEVFS_FS

	// register character device handler
	TRACE2("EPL:   Installing Driver '%s', Version %s...\n",
	       EPLLIN_DRV_NAME, EPL_PRODUCT_VERSION);
	TRACE0("EPL:   (using dynamic major number assignment)\n");
	nDrvMajorNumber_g =
	    register_chrdev(0, EPLLIN_DRV_NAME, &EplLinFileOps_g);
	if (nDrvMajorNumber_g != 0) {
		TRACE2
		    ("EPL:   Driver '%s' installed successful, assigned MajorNumber=%d\n",
		     EPLLIN_DRV_NAME, nDrvMajorNumber_g);
	} else {
		TRACE1
		    ("EPL:   ERROR: Driver '%s' is unable to get a free MajorNumber!\n",
		     EPLLIN_DRV_NAME);
		iRet = -EIO;
		goto Exit;
	}

	// create device node in DEVFS
	nMinorNumber = 0;
	TRACE1("EPL:   Creating device node '/dev/%s'...\n", EPLLIN_DEV_NAME);
	iErr =
	    devfs_mk_cdev(MKDEV(nDrvMajorNumber_g, nMinorNumber),
			  S_IFCHR | S_IRUGO | S_IWUGO, EPLLIN_DEV_NAME);
	if (iErr == 0) {
		TRACE1("EPL:   Device node '/dev/%s' created successful.\n",
		       EPLLIN_DEV_NAME);
	} else {
		TRACE1("EPL:   ERROR: unable to create device node '/dev/%s'\n",
		       EPLLIN_DEV_NAME);
		iRet = -EIO;
		goto Exit;
	}

#else

	// register character device handler
	// only one Minor required
	TRACE2("EPL:   Installing Driver '%s', Version %s...\n",
	       EPLLIN_DRV_NAME, EPL_PRODUCT_VERSION);
	iRet = alloc_chrdev_region(&nDevNum_g, 0, 1, EPLLIN_DRV_NAME);
	if (iRet == 0) {
		TRACE2
		    ("EPL:   Driver '%s' installed successful, assigned MajorNumber=%d\n",
		     EPLLIN_DRV_NAME, MAJOR(nDevNum_g));
	} else {
		TRACE1
		    ("EPL:   ERROR: Driver '%s' is unable to get a free MajorNumber!\n",
		     EPLLIN_DRV_NAME);
		iRet = -EIO;
		goto Exit;
	}

	// register cdev structure
	pEpl_cdev_g = cdev_alloc();
	pEpl_cdev_g->ops = &EplLinFileOps_g;
	pEpl_cdev_g->owner = THIS_MODULE;
	iErr = cdev_add(pEpl_cdev_g, nDevNum_g, 1);
	if (iErr) {
		TRACE2("EPL:   ERROR %d: Driver '%s' could not be added!\n",
		       iErr, EPLLIN_DRV_NAME);
		iRet = -EIO;
		goto Exit;
	}
#endif

	// create device node in PROCFS
	EplRet = EplLinProcInit();
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}

      Exit:

	TRACE1("EPL: - EplLinInit (iRet=%d)\n", iRet);
	return (iRet);

}

//---------------------------------------------------------------------------
//  Remove Driver
//---------------------------------------------------------------------------
//  -> rmmod driver
//---------------------------------------------------------------------------

static void __exit EplLinExit(void)
{

	tEplKernel EplRet;

	// delete instance for all modules
//    EplRet = EplApiShutdown();
//    printk("EplApiShutdown():  0x%X\n", EplRet);

	// deinitialize proc fs
	EplRet = EplLinProcFree();
	printk("EplLinProcFree():        0x%X\n", EplRet);

	TRACE0("EPL: + EplLinExit...\n");

#ifdef CONFIG_DEVFS_FS

	// remove device node from DEVFS
	devfs_remove(EPLLIN_DEV_NAME);
	TRACE1("EPL:   Device node '/dev/%s' removed.\n", EPLLIN_DEV_NAME);

	// unregister character device handler
	unregister_chrdev(nDrvMajorNumber_g, EPLLIN_DRV_NAME);

#else

	// remove cdev structure
	cdev_del(pEpl_cdev_g);

	// unregister character device handler
	unregister_chrdev_region(nDevNum_g, 1);

#endif

	TRACE1("EPL:   Driver '%s' removed.\n", EPLLIN_DRV_NAME);

	TRACE0("EPL: - EplLinExit\n");

}

//---------------------------------------------------------------------------
//  Open Driver
//---------------------------------------------------------------------------
//  -> open("/dev/driver", O_RDWR)...
//---------------------------------------------------------------------------

static int EplLinOpen(struct inode *pDeviceFile_p,	// information about the device to open
		      struct file *pInstance_p)	// information about driver instance
{

	int iRet;

	TRACE0("EPL: + EplLinOpen...\n");

	MOD_INC_USE_COUNT;

	if (uiEplState_g != EPL_STATE_NOTOPEN) {	// stack already initialized
		iRet = -EALREADY;
	} else {
		atomic_set(&AtomicEventState_g, EVENT_STATE_INIT);
		sema_init(&SemaphoreCbEvent_g, 1);
		init_waitqueue_head(&WaitQueueCbEvent_g);
		init_waitqueue_head(&WaitQueueProcess_g);
		init_waitqueue_head(&WaitQueueRelease_g);
		atomic_set(&AtomicSyncState_g, EVENT_STATE_INIT);
		init_waitqueue_head(&WaitQueueCbSync_g);
		init_waitqueue_head(&WaitQueuePI_In_g);

		uiEplState_g = EPL_STATE_NOTINIT;
		iRet = 0;
	}

	TRACE1("EPL: - EplLinOpen (iRet=%d)\n", iRet);
	return (iRet);

}

//---------------------------------------------------------------------------
//  Close Driver
//---------------------------------------------------------------------------
//  -> close(device)...
//---------------------------------------------------------------------------

static int EplLinRelease(struct inode *pDeviceFile_p,	// information about the device to open
			 struct file *pInstance_p)	// information about driver instance
{

	tEplKernel EplRet = kEplSuccessful;
	int iRet;

	TRACE0("EPL: + EplLinRelease...\n");

	if (uiEplState_g != EPL_STATE_NOTINIT) {
		// pass control to sync kernel thread, but signal termination
		atomic_set(&AtomicSyncState_g, EVENT_STATE_TERM);
		wake_up_interruptible(&WaitQueueCbSync_g);
		wake_up_interruptible(&WaitQueuePI_In_g);

		// pass control to event queue kernel thread
		atomic_set(&AtomicEventState_g, EVENT_STATE_TERM);
		wake_up_interruptible(&WaitQueueCbEvent_g);

		if (uiEplState_g == EPL_STATE_RUNNING) {	// post NmtEventSwitchOff
			EplRet = EplApiExecNmtCommand(kEplNmtEventSwitchOff);

		}

		if (EplRet == kEplSuccessful) {
			TRACE0("EPL:   waiting for NMT_GS_OFF\n");
			wait_event_interruptible(WaitQueueRelease_g,
						 (uiEplState_g ==
						  EPL_STATE_SHUTDOWN));
		} else {	// post NmtEventSwitchOff failed
			TRACE0("EPL:   event post failed\n");
		}

		// $$$ d.k.: What if waiting was interrupted by signal?

		TRACE0("EPL:   call EplApiShutdown()\n");
		// EPL stack can be safely shut down
		// delete instance for all EPL modules
		EplRet = EplApiShutdown();
		printk("EplApiShutdown():  0x%X\n", EplRet);
	}

	uiEplState_g = EPL_STATE_NOTOPEN;
	iRet = 0;

	MOD_DEC_USE_COUNT;

	TRACE1("EPL: - EplLinRelease (iRet=%d)\n", iRet);
	return (iRet);

}

//---------------------------------------------------------------------------
//  Read Data from Driver
//---------------------------------------------------------------------------
//  -> read(...)
//---------------------------------------------------------------------------

static ssize_t EplLinRead(struct file *pInstance_p,	// information about driver instance
			  char *pDstBuff_p,	// address of buffer to fill with data
			  size_t BuffSize_p,	// length of the buffer
			  loff_t * pFileOffs_p)	// offset in the file
{

	int iRet;

	TRACE0("EPL: + EplLinRead...\n");

	TRACE0("EPL:   Sorry, this operation isn't supported.\n");
	iRet = -EINVAL;

	TRACE1("EPL: - EplLinRead (iRet=%d)\n", iRet);
	return (iRet);

}

//---------------------------------------------------------------------------
//  Write Data to Driver
//---------------------------------------------------------------------------
//  -> write(...)
//---------------------------------------------------------------------------

static ssize_t EplLinWrite(struct file *pInstance_p,	// information about driver instance
			   const char *pSrcBuff_p,	// address of buffer to get data from
			   size_t BuffSize_p,	// length of the buffer
			   loff_t * pFileOffs_p)	// offset in the file
{

	int iRet;

	TRACE0("EPL: + EplLinWrite...\n");

	TRACE0("EPL:   Sorry, this operation isn't supported.\n");
	iRet = -EINVAL;

	TRACE1("EPL: - EplLinWrite (iRet=%d)\n", iRet);
	return (iRet);

}

//---------------------------------------------------------------------------
//  Generic Access to Driver
//---------------------------------------------------------------------------
//  -> ioctl(...)
//---------------------------------------------------------------------------

static int EplLinIoctl(struct inode *pDeviceFile_p,	// information about the device to open
		       struct file *pInstance_p,	// information about driver instance
		       unsigned int uiIoctlCmd_p,	// Ioctl command to execute
		       unsigned long ulArg_p)	// Ioctl command specific argument/parameter
{

	tEplKernel EplRet;
	int iErr;
	int iRet;

//    TRACE1("EPL: + EplLinIoctl (uiIoctlCmd_p=%d)...\n", uiIoctlCmd_p);

	iRet = -EINVAL;

	switch (uiIoctlCmd_p) {
		// ----------------------------------------------------------
	case EPLLIN_CMD_INITIALIZE:
		{
			tEplApiInitParam EplApiInitParam;

			iErr =
			    copy_from_user(&EplApiInitParam,
					   (const void *)ulArg_p,
					   sizeof(EplApiInitParam));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			EplApiInitParam.m_pfnCbEvent = EplLinCbEvent;
			EplApiInitParam.m_pfnCbSync = EplLinCbSync;

			EplRet = EplApiInitialize(&EplApiInitParam);

			uiEplState_g = EPL_STATE_RUNNING;

			iRet = (int)EplRet;
			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_SHUTDOWN:
		{		// shutdown the threads

			// pass control to sync kernel thread, but signal termination
			atomic_set(&AtomicSyncState_g, EVENT_STATE_TERM);
			wake_up_interruptible(&WaitQueueCbSync_g);
			wake_up_interruptible(&WaitQueuePI_In_g);

			// pass control to event queue kernel thread
			atomic_set(&AtomicEventState_g, EVENT_STATE_TERM);
			wake_up_interruptible(&WaitQueueCbEvent_g);

			if (uiEplState_g == EPL_STATE_RUNNING) {	// post NmtEventSwitchOff
				EplRet =
				    EplApiExecNmtCommand(kEplNmtEventSwitchOff);

			}

			iRet = 0;
			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_READ_LOCAL_OBJECT:
		{
			tEplLinLocalObject LocalObject;
			void *pData;

			iErr =
			    copy_from_user(&LocalObject, (const void *)ulArg_p,
					   sizeof(LocalObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if ((LocalObject.m_pData == NULL)
			    || (LocalObject.m_uiSize == 0)) {
				iRet = (int)kEplApiInvalidParam;
				goto Exit;
			}

			pData = vmalloc(LocalObject.m_uiSize);
			if (pData == NULL) {	// no memory available
				iRet = -ENOMEM;
				goto Exit;
			}

			EplRet =
			    EplApiReadLocalObject(LocalObject.m_uiIndex,
						  LocalObject.m_uiSubindex,
						  pData, &LocalObject.m_uiSize);

			if (EplRet == kEplSuccessful) {
				iErr =
				    copy_to_user(LocalObject.m_pData, pData,
						 LocalObject.m_uiSize);

				vfree(pData);

				if (iErr != 0) {
					iRet = -EIO;
					goto Exit;
				}
				// return actual size (LocalObject.m_uiSize)
				iErr = put_user(LocalObject.m_uiSize,
						(unsigned int *)(ulArg_p +
								 (unsigned long)
								 &LocalObject.
								 m_uiSize -
								 (unsigned long)
								 &LocalObject));
				if (iErr != 0) {
					iRet = -EIO;
					goto Exit;
				}

			} else {
				vfree(pData);
			}

			iRet = (int)EplRet;
			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_WRITE_LOCAL_OBJECT:
		{
			tEplLinLocalObject LocalObject;
			void *pData;

			iErr =
			    copy_from_user(&LocalObject, (const void *)ulArg_p,
					   sizeof(LocalObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if ((LocalObject.m_pData == NULL)
			    || (LocalObject.m_uiSize == 0)) {
				iRet = (int)kEplApiInvalidParam;
				goto Exit;
			}

			pData = vmalloc(LocalObject.m_uiSize);
			if (pData == NULL) {	// no memory available
				iRet = -ENOMEM;
				goto Exit;
			}
			iErr =
			    copy_from_user(pData, LocalObject.m_pData,
					   LocalObject.m_uiSize);
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			EplRet =
			    EplApiWriteLocalObject(LocalObject.m_uiIndex,
						   LocalObject.m_uiSubindex,
						   pData, LocalObject.m_uiSize);

			vfree(pData);

			iRet = (int)EplRet;
			break;
		}

	case EPLLIN_CMD_READ_OBJECT:
		{
			tEplLinSdoObject SdoObject;
			void *pData;
			tEplLinSdoBufHeader *pBufHeader;
			tEplSdoComConHdl *pSdoComConHdl;

			iErr =
			    copy_from_user(&SdoObject, (const void *)ulArg_p,
					   sizeof(SdoObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if ((SdoObject.m_le_pData == NULL)
			    || (SdoObject.m_uiSize == 0)) {
				iRet = (int)kEplApiInvalidParam;
				goto Exit;
			}

			pBufHeader =
			    (tEplLinSdoBufHeader *)
			    vmalloc(sizeof(tEplLinSdoBufHeader) +
				    SdoObject.m_uiSize);
			if (pBufHeader == NULL) {	// no memory available
				iRet = -ENOMEM;
				goto Exit;
			}
			// initiate temporary buffer
			pBufHeader->m_pUserArg = SdoObject.m_pUserArg;	// original user argument pointer
			pBufHeader->m_pData = SdoObject.m_le_pData;	// original data pointer from app
			pData = pBufHeader + sizeof(tEplLinSdoBufHeader);

			if (SdoObject.m_fValidSdoComConHdl != FALSE) {
				pSdoComConHdl = &SdoObject.m_SdoComConHdl;
			} else {
				pSdoComConHdl = NULL;
			}

			EplRet =
			    EplApiReadObject(pSdoComConHdl,
					     SdoObject.m_uiNodeId,
					     SdoObject.m_uiIndex,
					     SdoObject.m_uiSubindex, pData,
					     &SdoObject.m_uiSize,
					     SdoObject.m_SdoType, pBufHeader);

			// return actual SDO handle (SdoObject.m_SdoComConHdl)
			iErr = put_user(SdoObject.m_SdoComConHdl,
					(unsigned int *)(ulArg_p +
							 (unsigned long)
							 &SdoObject.
							 m_SdoComConHdl -
							 (unsigned long)
							 &SdoObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if (EplRet == kEplSuccessful) {
				iErr =
				    copy_to_user(SdoObject.m_le_pData, pData,
						 SdoObject.m_uiSize);

				vfree(pBufHeader);

				if (iErr != 0) {
					iRet = -EIO;
					goto Exit;
				}
				// return actual size (SdoObject.m_uiSize)
				iErr = put_user(SdoObject.m_uiSize,
						(unsigned int *)(ulArg_p +
								 (unsigned long)
								 &SdoObject.
								 m_uiSize -
								 (unsigned long)
								 &SdoObject));
				if (iErr != 0) {
					iRet = -EIO;
					goto Exit;
				}
			} else if (EplRet != kEplApiTaskDeferred) {	// error ocurred
				vfree(pBufHeader);
				if (iErr != 0) {
					iRet = -EIO;
					goto Exit;
				}
			}

			iRet = (int)EplRet;
			break;
		}

	case EPLLIN_CMD_WRITE_OBJECT:
		{
			tEplLinSdoObject SdoObject;
			void *pData;
			tEplLinSdoBufHeader *pBufHeader;
			tEplSdoComConHdl *pSdoComConHdl;

			iErr =
			    copy_from_user(&SdoObject, (const void *)ulArg_p,
					   sizeof(SdoObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if ((SdoObject.m_le_pData == NULL)
			    || (SdoObject.m_uiSize == 0)) {
				iRet = (int)kEplApiInvalidParam;
				goto Exit;
			}

			pBufHeader =
			    (tEplLinSdoBufHeader *)
			    vmalloc(sizeof(tEplLinSdoBufHeader) +
				    SdoObject.m_uiSize);
			if (pBufHeader == NULL) {	// no memory available
				iRet = -ENOMEM;
				goto Exit;
			}
			// initiate temporary buffer
			pBufHeader->m_pUserArg = SdoObject.m_pUserArg;	// original user argument pointer
			pBufHeader->m_pData = SdoObject.m_le_pData;	// original data pointer from app
			pData = pBufHeader + sizeof(tEplLinSdoBufHeader);

			iErr =
			    copy_from_user(pData, SdoObject.m_le_pData,
					   SdoObject.m_uiSize);

			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if (SdoObject.m_fValidSdoComConHdl != FALSE) {
				pSdoComConHdl = &SdoObject.m_SdoComConHdl;
			} else {
				pSdoComConHdl = NULL;
			}

			EplRet =
			    EplApiWriteObject(pSdoComConHdl,
					      SdoObject.m_uiNodeId,
					      SdoObject.m_uiIndex,
					      SdoObject.m_uiSubindex, pData,
					      SdoObject.m_uiSize,
					      SdoObject.m_SdoType, pBufHeader);

			// return actual SDO handle (SdoObject.m_SdoComConHdl)
			iErr = put_user(SdoObject.m_SdoComConHdl,
					(unsigned int *)(ulArg_p +
							 (unsigned long)
							 &SdoObject.
							 m_SdoComConHdl -
							 (unsigned long)
							 &SdoObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if (EplRet != kEplApiTaskDeferred) {	// succeeded or error ocurred, but task not deferred
				vfree(pBufHeader);
			}

			iRet = (int)EplRet;
			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_FREE_SDO_CHANNEL:
		{
			// forward SDO handle to EPL stack
			EplRet =
			    EplApiFreeSdoChannel((tEplSdoComConHdl) ulArg_p);

			iRet = (int)EplRet;
			break;
		}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
		// ----------------------------------------------------------
	case EPLLIN_CMD_MN_TRIGGER_STATE_CHANGE:
		{
			tEplLinNodeCmdObject NodeCmdObject;

			iErr =
			    copy_from_user(&NodeCmdObject,
					   (const void *)ulArg_p,
					   sizeof(NodeCmdObject));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			EplRet =
			    EplApiMnTriggerStateChange(NodeCmdObject.m_uiNodeId,
						       NodeCmdObject.
						       m_NodeCommand);
			iRet = (int)EplRet;
			break;
		}
#endif

		// ----------------------------------------------------------
	case EPLLIN_CMD_GET_EVENT:
		{
			tEplLinEvent Event;

			// save event structure
			iErr =
			    copy_from_user(&Event, (const void *)ulArg_p,
					   sizeof(Event));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}
			// save return code from application's event callback function
			RetCbEvent_g = Event.m_RetCbEvent;

			if (RetCbEvent_g == kEplShutdown) {
				// pass control to event queue kernel thread, but signal termination
				atomic_set(&AtomicEventState_g,
					   EVENT_STATE_TERM);
				wake_up_interruptible(&WaitQueueCbEvent_g);
				// exit with error -> EplApiProcess() will leave the infinite loop
				iRet = 1;
				goto Exit;
			}
			// pass control to event queue kernel thread
			atomic_set(&AtomicEventState_g, EVENT_STATE_IOCTL);
			wake_up_interruptible(&WaitQueueCbEvent_g);

			// fall asleep itself in own wait queue
			iErr = wait_event_interruptible(WaitQueueProcess_g,
							(atomic_read
							 (&AtomicEventState_g)
							 == EVENT_STATE_READY)
							||
							(atomic_read
							 (&AtomicEventState_g)
							 == EVENT_STATE_TERM));
			if (iErr != 0) {	// waiting was interrupted by signal
				// pass control to event queue kernel thread, but signal termination
				atomic_set(&AtomicEventState_g,
					   EVENT_STATE_TERM);
				wake_up_interruptible(&WaitQueueCbEvent_g);
				// exit with this error -> EplApiProcess() will leave the infinite loop
				iRet = iErr;
				goto Exit;
			} else if (atomic_read(&AtomicEventState_g) == EVENT_STATE_TERM) {	// termination in progress
				// pass control to event queue kernel thread, but signal termination
				wake_up_interruptible(&WaitQueueCbEvent_g);
				// exit with this error -> EplApiProcess() will leave the infinite loop
				iRet = 1;
				goto Exit;
			}
			// copy event to user space
			iErr =
			    copy_to_user(Event.m_pEventType, &EventType_g,
					 sizeof(EventType_g));
			if (iErr != 0) {	// not all data could be copied
				iRet = -EIO;
				goto Exit;
			}
			// $$$ d.k. perform SDO event processing
			if (EventType_g == kEplApiEventSdo) {
				void *pData;
				tEplLinSdoBufHeader *pBufHeader;

				pBufHeader =
				    (tEplLinSdoBufHeader *) pEventArg_g->m_Sdo.
				    m_pUserArg;
				pData =
				    pBufHeader + sizeof(tEplLinSdoBufHeader);

				if (pEventArg_g->m_Sdo.m_SdoAccessType ==
				    kEplSdoAccessTypeRead) {
					// copy read data to user space
					iErr =
					    copy_to_user(pBufHeader->m_pData,
							 pData,
							 pEventArg_g->m_Sdo.
							 m_uiTransferredByte);
					if (iErr != 0) {	// not all data could be copied
						iRet = -EIO;
						goto Exit;
					}
				}
				pEventArg_g->m_Sdo.m_pUserArg =
				    pBufHeader->m_pUserArg;
				vfree(pBufHeader);
			}

			iErr =
			    copy_to_user(Event.m_pEventArg, pEventArg_g,
					 min(sizeof(tEplApiEventArg),
					     Event.m_uiEventArgSize));
			if (iErr != 0) {	// not all data could be copied
				iRet = -EIO;
				goto Exit;
			}
			// return to EplApiProcess(), which will call the application's event callback function
			iRet = 0;

			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_PI_SETUP:
		{
			EplRet = EplApiProcessImageSetup();
			iRet = (int)EplRet;

			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_PI_IN:
		{
			tEplApiProcessImage ProcessImageIn;

			// save process image structure
			iErr =
			    copy_from_user(&ProcessImageIn,
					   (const void *)ulArg_p,
					   sizeof(ProcessImageIn));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}
			// pass control to event queue kernel thread
			atomic_set(&AtomicSyncState_g, EVENT_STATE_IOCTL);

			// fall asleep itself in own wait queue
			iErr = wait_event_interruptible(WaitQueuePI_In_g,
							(atomic_read
							 (&AtomicSyncState_g) ==
							 EVENT_STATE_READY)
							||
							(atomic_read
							 (&AtomicSyncState_g) ==
							 EVENT_STATE_TERM));
			if (iErr != 0) {	// waiting was interrupted by signal
				// pass control to sync kernel thread, but signal termination
				atomic_set(&AtomicSyncState_g,
					   EVENT_STATE_TERM);
				wake_up_interruptible(&WaitQueueCbSync_g);
				// exit with this error -> application will leave the infinite loop
				iRet = iErr;
				goto Exit;
			} else if (atomic_read(&AtomicSyncState_g) == EVENT_STATE_TERM) {	// termination in progress
				// pass control to sync kernel thread, but signal termination
				wake_up_interruptible(&WaitQueueCbSync_g);
				// exit with this error -> application will leave the infinite loop
				iRet = 1;
				goto Exit;
			}
			// exchange process image
			EplRet = EplApiProcessImageExchangeIn(&ProcessImageIn);

			// return to EplApiProcessImageExchangeIn()
			iRet = (int)EplRet;

			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_PI_OUT:
		{
			tEplApiProcessImage ProcessImageOut;

			// save process image structure
			iErr =
			    copy_from_user(&ProcessImageOut,
					   (const void *)ulArg_p,
					   sizeof(ProcessImageOut));
			if (iErr != 0) {
				iRet = -EIO;
				goto Exit;
			}

			if (atomic_read(&AtomicSyncState_g) !=
			    EVENT_STATE_READY) {
				iRet = (int)kEplInvalidOperation;
				goto Exit;
			}
			// exchange process image
			EplRet =
			    EplApiProcessImageExchangeOut(&ProcessImageOut);

			// pass control to sync kernel thread
			atomic_set(&AtomicSyncState_g, EVENT_STATE_TERM);
			wake_up_interruptible(&WaitQueueCbSync_g);

			// return to EplApiProcessImageExchangeout()
			iRet = (int)EplRet;

			break;
		}

		// ----------------------------------------------------------
	case EPLLIN_CMD_NMT_COMMAND:
		{
			// forward NMT command to EPL stack
			EplRet = EplApiExecNmtCommand((tEplNmtEvent) ulArg_p);

			iRet = (int)EplRet;

			break;
		}

		// ----------------------------------------------------------
	default:
		{
			break;
		}
	}

      Exit:

//    TRACE1("EPL: - EplLinIoctl (iRet=%d)\n", iRet);
	return (iRet);

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

tEplKernel PUBLIC EplLinCbEvent(tEplApiEventType EventType_p,	// IN: event type (enum)
				tEplApiEventArg * pEventArg_p,	// IN: event argument (union)
				void GENERIC * pUserArg_p)
{
	tEplKernel EplRet = kEplSuccessful;
	int iErr;

	// block any further call to this function, i.e. enter critical section
	iErr = down_interruptible(&SemaphoreCbEvent_g);
	if (iErr != 0) {	// waiting was interrupted by signal
		EplRet = kEplShutdown;
		goto Exit;
	}
	// wait for EplApiProcess() to call ioctl
	// normally it should be waiting already for us to pass a new event
	iErr = wait_event_interruptible(WaitQueueCbEvent_g,
					(atomic_read(&AtomicEventState_g) ==
					 EVENT_STATE_IOCTL)
					|| (atomic_read(&AtomicEventState_g) ==
					    EVENT_STATE_TERM));
	if ((iErr != 0) || (atomic_read(&AtomicEventState_g) == EVENT_STATE_TERM)) {	// waiting was interrupted by signal
		EplRet = kEplShutdown;
		goto LeaveCriticalSection;
	}
	// save event information for ioctl
	EventType_g = EventType_p;
	pEventArg_g = pEventArg_p;

	// pass control to application's event callback function, i.e. EplApiProcess()
	atomic_set(&AtomicEventState_g, EVENT_STATE_READY);
	wake_up_interruptible(&WaitQueueProcess_g);

	// now, the application's event callback function processes the event

	// wait for completion of application's event callback function, i.e. EplApiProcess() calls ioctl again
	iErr = wait_event_interruptible(WaitQueueCbEvent_g,
					(atomic_read(&AtomicEventState_g) ==
					 EVENT_STATE_IOCTL)
					|| (atomic_read(&AtomicEventState_g) ==
					    EVENT_STATE_TERM));
	if ((iErr != 0) || (atomic_read(&AtomicEventState_g) == EVENT_STATE_TERM)) {	// waiting was interrupted by signal
		EplRet = kEplShutdown;
		goto LeaveCriticalSection;
	}
	// read return code from application's event callback function
	EplRet = RetCbEvent_g;

      LeaveCriticalSection:
	up(&SemaphoreCbEvent_g);

      Exit:
	// check if NMT_GS_OFF is reached
	if (EventType_p == kEplApiEventNmtStateChange) {
		if (pEventArg_p->m_NmtStateChange.m_NewNmtState == kEplNmtGsOff) {	// NMT state machine was shut down
			TRACE0("EPL:   EplLinCbEvent(NMT_GS_OFF)\n");
			uiEplState_g = EPL_STATE_SHUTDOWN;
			atomic_set(&AtomicEventState_g, EVENT_STATE_TERM);
			wake_up(&WaitQueueRelease_g);
		} else {	// NMT state machine is running
			uiEplState_g = EPL_STATE_RUNNING;
		}
	}

	return EplRet;
}

tEplKernel PUBLIC EplLinCbSync(void)
{
	tEplKernel EplRet = kEplSuccessful;
	int iErr;

	// check if user process waits for sync
	if (atomic_read(&AtomicSyncState_g) == EVENT_STATE_IOCTL) {
		// pass control to application, i.e. EplApiProcessImageExchangeIn()
		atomic_set(&AtomicSyncState_g, EVENT_STATE_READY);
		wake_up_interruptible(&WaitQueuePI_In_g);

		// now, the application processes the sync event

		// wait for call of EplApiProcessImageExchangeOut()
		iErr = wait_event_interruptible(WaitQueueCbSync_g,
						(atomic_read(&AtomicSyncState_g)
						 == EVENT_STATE_IOCTL)
						||
						(atomic_read(&AtomicSyncState_g)
						 == EVENT_STATE_TERM));
		if ((iErr != 0) || (atomic_read(&AtomicEventState_g) == EVENT_STATE_IOCTL)) {	// waiting was interrupted by signal or application called wrong function
			EplRet = kEplShutdown;
		}
	} else {		// application is currently not waiting for sync
		// continue without interruption
		// TPDO are set valid by caller (i.e. EplEventkProcess())
	}

	TGT_DBG_SIGNAL_TRACE_POINT(1);

	return EplRet;
}

// EOF
