/*
 * Copyright (c) 2008-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "nvos.h"
#include "nvos_trace.h"
#include "nvutil.h"
#include "nverror.h"
#include "nvassert.h"
#include "nvbootargs.h"
#include "nvio.h"
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/div64.h>
#include <asm/setup.h>
#include <asm/cacheflush.h>
#include <mach/irqs.h>
#include <linux/freezer.h>
#include <linux/slab.h>

#if NVOS_TRACE || NV_DEBUG
#undef NvOsAlloc
#undef NvOsFree
#undef NvOsRealloc
#undef NvOsSharedMemAlloc
#undef NvOsSharedMemMap
#undef NvOsSharedMemUnmap
#undef NvOsSharedMemFree
#undef NvOsMutexCreate
#undef NvOsExecAlloc
#undef NvOsExecFree
#undef NvOsPageAlloc
#undef NvOsPageLock
#undef NvOsPageFree
#undef NvOsPageMap
#undef NvOsPageMapIntoPtr
#undef NvOsPageUnmap
#undef NvOsPageAddress
#undef NvOsIntrMutexCreate
#undef NvOsIntrMutexLock
#undef NvOsIntrMutexUnlock
#undef NvOsIntrMutexDestroy
#undef NvOsInterruptRegister
#undef NvOsInterruptUnregister
#undef NvOsInterruptEnable
#undef NvOsInterruptDone
#undef NvOsPhysicalMemMapIntoCaller
#undef NvOsMutexLock
#undef NvOsMutexUnlock
#undef NvOsMutexDestroy
#undef NvOsPhysicalMemMap
#undef NvOsPhysicalMemUnmap
#undef NvOsSemaphoreCreate
#undef NvOsSemaphoreWait
#undef NvOsSemaphoreWaitTimeout
#undef NvOsSemaphoreSignal
#undef NvOsSemaphoreDestroy
#undef NvOsSemaphoreClone
#undef NvOsSemaphoreUnmarshal
#undef NvOsThreadCreate
#undef NvOsInterruptPriorityThreadCreate
#undef NvOsThreadJoin
#undef NvOsThreadYield
#endif

#define KTHREAD_IRQ_PRIO (MAX_RT_PRIO>>1)

#define NVOS_MAX_SYSTEM_IRQS NR_IRQS

#define NVOS_IRQ_IS_ENABLED 0x1

/* NVOS_IRQ_IS_ flags are mutually exclusive.
 * IS_TASKLET executes the handler in a tasklet (used for kernel drivers)
 * IS_KERNEL_THREAD executes in a kernel thread (used for kernel GPIOs)
 * IS_USER simply signals an NvOs semaphore (used for user-mode interrupts)
 *
 * Currently the choice is based on the IRQ number and if the requester is
 * an IOCTL. Later this can be modified to be exposed in the public APIs.
 *
 * If no flag is set, the IRQ is handled in the interrupt handler itself.
 */

#define NVOS_IRQ_TYPE_SHIFT 1
#define NVOS_IRQ_TYPE_MASK (0x3 << NVOS_IRQ_TYPE_SHIFT)

#define NVOS_IRQ_IS_IRQ           (0)
#define NVOS_IRQ_IS_TASKLET       (0x1 << NVOS_IRQ_TYPE_SHIFT)
#define NVOS_IRQ_IS_KERNEL_THREAD (0x2 << NVOS_IRQ_TYPE_SHIFT)
#define NVOS_IRQ_IS_USER          (0x3 << NVOS_IRQ_TYPE_SHIFT)

static DEFINE_SPINLOCK(gs_NvOsSpinLock);

typedef struct NvOsIrqHandlerRec
{
    union
    {
        NvOsInterruptHandler  pHandler;
        NvOsSemaphoreHandle   pSem;
    };
    NvU32                 Irq;
    char                  IrqName[16];
    struct semaphore      sem;
    struct task_struct    *task;
    struct tasklet_struct Tasklet;
} NvOsIrqHandler;

typedef struct NvOsInterruptBlockRec
{
    void                *pArg;
    NvU32               Flags;
    NvU32               NumIrqs;
    NvU32               Shutdown;
    NvOsIrqHandler      IrqList[1];
} NvOsInterruptBlock;

#define INTBLOCK_SIZE(NUMIRQS) \
    (sizeof(NvOsInterruptBlock) + ((NUMIRQS)-1)*sizeof(NvOsIrqHandler))

static NvOsInterruptBlock *s_pIrqList[NVOS_MAX_SYSTEM_IRQS] = { NULL };

static NvBootArgs s_BootArgs = { {0}, {0}, {0}, {0}, {0}, {0}, {{0}} };

/*  The tasklet "data" parameter is a munging of the s_pIrqList index
 *  (just the IRQ number), and the InterruptBlock's IrqList index, to
 *  make interrupt handler lookups O(n)
 */
static void NvOsTaskletWrapper(
       unsigned long data)
{
    NvOsInterruptBlock *pBlock = s_pIrqList[(data&0xffff)];
    if (pBlock)
        (*pBlock->IrqList[data>>16].pHandler)(pBlock->pArg);    
}

/*  The thread "pdata" parameter is a munging of the s_pIrqList index
 *  (just the IRQ number), and the InterruptBlock's IrqList index, to
 *  make interrupt handler lookups O(n)
 */
static int NvOsInterruptThreadWrapper(
     void *pdata)
{
    unsigned long data = (unsigned long)pdata;
    NvOsInterruptBlock *pBlock = s_pIrqList[(data&0xffff)];

    if (!pBlock)
    {
        return 0;
    } 
    while (!pBlock->Shutdown)
    {
        int t;

        /* Is the timeout large enough? */
        t = down_interruptible(&pBlock->IrqList[data>>16].sem);

        if (pBlock->Shutdown)
            break;

        if (t)
            continue;

        (*pBlock->IrqList[data>>16].pHandler)(pBlock->pArg);
    }

    return 0;
}

static irqreturn_t NvOsIrqWrapper(
    int irq,
    void *dev_id)
{
    unsigned long data = (unsigned long)dev_id;
    NvOsInterruptBlock *pBlock = s_pIrqList[irq];

    disable_irq_nosync(irq);
    switch (pBlock->Flags & NVOS_IRQ_TYPE_MASK)
    {
    case NVOS_IRQ_IS_TASKLET:
        tasklet_schedule(&pBlock->IrqList[data].Tasklet);
        break;
    case NVOS_IRQ_IS_KERNEL_THREAD:
        up(&(pBlock->IrqList[data].sem));
        break;
    case NVOS_IRQ_IS_USER:
        NvOsSemaphoreSignal(pBlock->IrqList[data].pSem);
        break;
    case NVOS_IRQ_IS_IRQ:
        (*pBlock->IrqList[data].pHandler)(pBlock->pArg);
        break;
    }

    return IRQ_HANDLED;
}

NvError NvOsFprintf(NvOsFileHandle stream, const char *format, ...)
{
    return NvError_NotImplemented;
}

NvS32 NvOsSnprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    va_start( ap, format );
    return vsnprintf( str, size, format, ap );
    va_end( ap );
}

NvError NvOsVfprintf(NvOsFileHandle stream, const char *format, va_list ap)
{
    return NvError_NotImplemented;
}

NvS32 NvOsVsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    return vsnprintf( str, size, format, ap );
}

void NvOsDebugPrintf(const char *format, ...)
{
    va_list ap;
    va_start( ap, format );
    vprintk( format, ap );
    va_end( ap );
}

void
NvOsDebugVprintf( const char *format, va_list ap )
{
    vprintk( format, ap );
}

NvS32 NvOsDebugNprintf(const char *format, ...)
{
    NvS32 r;
    va_list ap;
    va_start( ap, format );
    r = vprintk( format, ap );
    va_end( ap );
    return r;
}


NvError NvOsGetOsInformation(NvOsOsInfo *pOsInfo)
{
    if (pOsInfo)
    {
        NvOsMemset(pOsInfo, 0, sizeof(NvOsOsInfo));
        pOsInfo->OsType = NvOsOs_Linux;
    }
    else
    {
        return NvError_BadParameter;
    }
    return NvError_Success;
}

void NvOsStrncpy(char *dest, const char *src, size_t size)
{
    strncpy( dest, src, size );
}

NvOsCodePage NvOsStrGetSystemCodePage(void)
{
    return (NvOsCodePage)0;
}

size_t NvOsStrlen(const char *s)
{
    return strlen(s);
}

int NvOsStrcmp(const char *s1, const char *s2)
{
    return strcmp(s1, s2);
}

int NvOsStrncmp(const char *s1, const char *s2, size_t size)
{
    return strncmp(s1, s2, size);
}

void NvOsMemcpy(void *dest, const void *src, size_t size)
{
    memcpy(dest, src, size);
}

int NvOsMemcmp(const void *s1, const void *s2, size_t size)
{
    return memcmp(s1, s2, size);
}

void NvOsMemset(void *s, NvU8 c, size_t size)
{
    memset(s, c, size);
}

void NvOsMemmove(void *dest, const void *src, size_t size)
{
    memmove(dest, src, size);
}

NvError NvOsCopyIn(void *pDst, const void *pSrc, size_t Bytes)
{
    if (!Bytes)
        return NvSuccess;

    if( access_ok( VERIFY_READ, pSrc, Bytes ) )
    {
        __copy_from_user(pDst, pSrc, Bytes);
        return NvSuccess;
    }

    return NvError_InvalidAddress;
}

NvError NvOsCopyOut(void *pDst, const void *pSrc, size_t Bytes)
{
    if (!Bytes)
        return NvSuccess;

    if( access_ok( VERIFY_WRITE, pDst, Bytes ) )
    {
        __copy_to_user(pDst, pSrc, Bytes);
        return NvSuccess;
    }

    return NvError_InvalidAddress;
}

NvError NvOsFopen(const char *path, NvU32 flags, NvOsFileHandle *file)
{
    return NvError_NotImplemented;
}

void NvOsFclose(NvOsFileHandle stream)
{
}

NvError NvOsFwrite(NvOsFileHandle stream, const void *ptr, size_t size)
{
    return NvError_NotImplemented;
}

NvError NvOsFread(
    NvOsFileHandle stream,
    void *ptr,
    size_t size,
    size_t *bytes)
{
    return NvError_NotImplemented;
}

NvError NvOsFreadTimeout(
    NvOsFileHandle stream,
    void *ptr,
    size_t size,
    size_t *bytes,
    NvU32 timeout_msec)
{
    return NvError_NotImplemented;
}

NvError NvOsFgetc(NvOsFileHandle stream, NvU8 *c)
{
    return NvError_NotImplemented;
}

NvError NvOsFseek(NvOsFileHandle file, NvS64 offset, NvOsSeekEnum whence)
{
    return NvError_NotImplemented;
}

NvError NvOsFtell(NvOsFileHandle file, NvU64 *position)
{
    return NvError_NotImplemented;
}

NvError NvOsStat(const char *filename, NvOsStatType *stat)
{
    return NvError_NotImplemented;
}

NvError NvOsFstat(NvOsFileHandle file, NvOsStatType *stat)
{
    return NvError_NotImplemented;
}

NvError NvOsFflush(NvOsFileHandle stream)
{
    return NvError_NotImplemented;
}

NvError NvOsFsync(NvOsFileHandle stream)
{
    return NvError_NotImplemented;
}

NvError NvOsIoctl(
    NvOsFileHandle hFile,
    NvU32 IoctlCode,
    void *pBuffer,
    NvU32 InBufferSize,
    NvU32 InOutBufferSize,
    NvU32 OutBufferSize)
{
    return NvError_NotImplemented;
}

NvError NvOsOpendir(const char *path, NvOsDirHandle *dir)
{
    return NvError_NotImplemented;
}

NvError NvOsReaddir(NvOsDirHandle dir, char *name, size_t size)
{
    return NvError_NotImplemented;
}

void NvOsClosedir(NvOsDirHandle dir)
{
}

const NvOsFileHooks *NvOsSetFileHooks(NvOsFileHooks *newHooks)
{
    return 0;
}

NvError NvOsGetConfigU32(const char *name, NvU32 *value)
{
    return NvError_NotImplemented;
}

NvError NvOsGetConfigString(const char *name, char *value, NvU32 size)
{
    return NvError_NotImplemented;
}

void *NvOsAlloc(size_t size)
{
    size_t AllocSize = size + sizeof(size_t);
    size_t* ptr = NULL;
    ptr = vmalloc(AllocSize);
    if (!ptr)
        return ptr;
    *ptr = size;
    ptr++;
    return (ptr);
}

void *NvOsRealloc(void *ptr, size_t size)
{
    size_t* NewPtr = NULL;
    size_t OldSize = 0;
    size_t SmallerSize = 0;

    if( !ptr )
    {
        return NvOsAlloc(size);
    }
    if (!size)
    {
        NvOsFree(ptr);
        return NULL;
    }

    // Get the size of the memory allocated for ptr.
    NewPtr = (size_t*)ptr;
    NewPtr--;
    OldSize = *NewPtr;
    if (size == OldSize)
        return ptr;
    SmallerSize = (OldSize > size) ? size : OldSize;

    NewPtr = NvOsAlloc(size);
    if(!NewPtr)
        return NULL;
    NvOsMemcpy(NewPtr, ptr, SmallerSize);
    NvOsFree(ptr);
    return NewPtr;
}

void NvOsFree(void *ptr)
{
    size_t* AllocPtr = NULL;
    if (ptr)
    {
        AllocPtr = (size_t*)ptr;
        AllocPtr--;
    }
    else
        return;
    vfree(AllocPtr);
}

void *NvOsExecAlloc(size_t size)
{
    return vmalloc_exec( size );
}

NvError NvOsSharedMemAlloc(
    const char *key,
    size_t size,
    NvOsSharedMemHandle *descriptor)
{
    return NvError_NotImplemented;
}

NvError NvOsSharedMemMap(
    NvOsSharedMemHandle descriptor,
    size_t offset,
    size_t size,
    void **ptr)
{
    return NvError_NotImplemented;
}


void NvOsSharedMemUnmap(void *ptr, size_t size)
{
}

void NvOsSharedMemFree(NvOsSharedMemHandle descriptor)
{
}    

NvError NvOsPhysicalMemMap(
    NvOsPhysAddr phys,
    size_t size,
    NvOsMemAttribute attrib,
    NvU32 flags,
    void **ptr)
{
    /*  For apertures in the static kernel mapping, just return the
     *  static VA rather than creating a new mapping 
     *  FIXME:  Eventually, the static phyiscal apertures should be
     *  registered with NvOs when mapped, since they could be
     *  chip-dependent
     */
#define aperture_comp_map(_name, _pa, _len)                             \
    if ((phys >= (NvOsPhysAddr)(_pa)) &&                                \
        ((NvOsPhysAddr)(phys+size)<=(NvOsPhysAddr)((_pa)+(_len)))) {    \
            *ptr = (void *)tegra_munge_pa(phys);                        \
            return NvSuccess;                                           \
    }

    tegra_apertures(aperture_comp_map);

    if (attrib == NvOsMemAttribute_WriteCombined)
    {
        *ptr = ioremap_wc(phys, size);
    }
    else if (attrib == NvOsMemAttribute_WriteBack)
    {
        *ptr = ioremap_cached(phys, size);
    }
    else
    {
        *ptr = ioremap_nocache(phys, size);
    }

    if (*ptr == 0)
        return NvError_InsufficientMemory;

    return NvSuccess;
}

NvError NvOsPhysicalMemMapIntoCaller(
    void *pCallerPtr,
    NvOsPhysAddr phys,
    size_t size,
    NvOsMemAttribute attrib,
    NvU32 flags)
{
    return NvError_NotImplemented;
}

void NvOsPhysicalMemUnmap(void *ptr, size_t size)
{
    NvUPtr va = (NvUPtr)ptr;

    /*  No unmapping required for statically mapped I/O space */
#define aperture_comp_unmap(_name, _pa, _len)                           \
    if ((tegra_munge_pa((_pa)) <= va) &&                                \
        (tegra_munge_pa((_pa))+(_len) >= (va+size)))                    \
        return;


    tegra_apertures(aperture_comp_unmap);
    iounmap(ptr);
}

NvError NvOsLibraryLoad(const char *name, NvOsLibraryHandle *library)
{
    return NvError_NotImplemented;
}

void* NvOsLibraryGetSymbol(NvOsLibraryHandle library, const char *symbol)
{
    return 0;
}

void NvOsLibraryUnload(NvOsLibraryHandle library)
{
}

void NvOsSleepMS(NvU32 msec)
{
    msleep( msec );
}

void NvOsWaitUS(NvU32 usec)
{
    udelay( usec );
}

typedef struct NvOsMutexRec
{
    struct mutex mutex;
    volatile NvU32 count;
    volatile struct thread_info *owner;
} NvOsMutex;

/**
 * nvos mutexes are recursive.
 */
NvError NvOsMutexCreate(NvOsMutexHandle *mutex)
{
    NvOsMutex *m;

    m = kzalloc( sizeof(NvOsMutex), GFP_KERNEL );
    if( !m )
        return NvError_InsufficientMemory;

    mutex_init( &m->mutex );
    m->count = 0;
    m->owner = 0;

    *mutex = m;
    return NvSuccess;
}

void NvOsMutexLock(NvOsMutexHandle mutex)
{
    struct task_struct *task = current;
    struct thread_info *info = task_thread_info(task);
    int ret;

    NV_ASSERT( mutex );

    /* if we own the lock, increment the count and bail out */
    if( mutex->owner == info )
    {
        mutex->count++;
        return;
    }

    /* lock as normal, then setup the recursive stuff */
    do
    {
        /*  FIXME: interruptible mutexes may not be necessary, since this
         *  implementation is only used by the kernel tasks. */
        ret = mutex_lock_interruptible( &mutex->mutex );
        //  If a signal arrives while the task is sleeping,
        //  re-schedule it and attempt to reacquire the mutex
        if (ret && !try_to_freeze())
            schedule();
    } while (ret);
    mutex->owner = info;
    mutex->count = 1;
}

void NvOsMutexUnlock(NvOsMutexHandle mutex)
{
    NV_ASSERT( mutex );

    mutex->count--;
    if( mutex->count == 0 )
    {
        /* prevent the same thread from unlocking, then doing a recursive
         * lock (skip mutex_lock).
         */
        mutex->owner = 0;

        mutex_unlock( &mutex->mutex );
    }
}

void NvOsMutexDestroy(NvOsMutexHandle mutex)
{

    if( !mutex )
        return;
    kfree( mutex );
}

typedef struct NvOsIntrMutexRec
{
    spinlock_t lock;
    unsigned long flags;
} NvOsIntrMutex;

NvError NvOsIntrMutexCreate(NvOsIntrMutexHandle *mutex)
{
    NvOsIntrMutex *m;

    m = kzalloc( sizeof(NvOsIntrMutex), GFP_KERNEL );
    if( !m )
        return NvError_InsufficientMemory;

    spin_lock_init( &m->lock );
    *mutex = m;
    return NvSuccess;
}

void NvOsIntrMutexLock(NvOsIntrMutexHandle mutex)
{
    NV_ASSERT( mutex );
    spin_lock_irqsave( &mutex->lock, mutex->flags );
}

void NvOsIntrMutexUnlock(NvOsIntrMutexHandle mutex)
{
    NV_ASSERT( mutex );
    spin_unlock_irqrestore( &mutex->lock, mutex->flags );
}

void NvOsIntrMutexDestroy(NvOsIntrMutexHandle mutex)
{
    if (mutex)
        kfree(mutex);
}

typedef struct NvOsSpinMutexRec
{
    spinlock_t lock;
} NvOsSpinMutex;

NvError NvOsSpinMutexCreate(NvOsSpinMutexHandle *mutex)
{
    NvOsSpinMutex *m;

    m = kzalloc( sizeof(NvOsSpinMutex), GFP_KERNEL );
    if( !m )
        return NvError_InsufficientMemory;

    spin_lock_init( &m->lock );
    *mutex = m;
    return NvSuccess;
}

void NvOsSpinMutexLock(NvOsSpinMutexHandle mutex)
{
    NV_ASSERT( mutex );
    spin_lock( &mutex->lock );
}

void NvOsSpinMutexUnlock(NvOsSpinMutexHandle mutex)
{
    NV_ASSERT( mutex );
    spin_unlock( &mutex->lock );
}

void NvOsSpinMutexDestroy(NvOsSpinMutexHandle mutex)
{
    if (mutex)
        kfree(mutex);
}

typedef struct NvOsSemaphoreRec
{
    struct semaphore sem;
    atomic_t refcount;
} NvOsSemaphore;

NvError NvOsSemaphoreCreate(
    NvOsSemaphoreHandle *semaphore,
    NvU32 value)
{
    NvOsSemaphore *s;

    s = kzalloc( sizeof(NvOsSemaphore), GFP_KERNEL );
    if( !s )
        return NvError_InsufficientMemory;

    sema_init( &s->sem, value );
    atomic_set( &s->refcount, 1 );

    *semaphore = s;

    return NvSuccess;
}

NvError NvOsSemaphoreClone(
    NvOsSemaphoreHandle orig,
    NvOsSemaphoreHandle *semaphore)
{
    NV_ASSERT( orig );
    NV_ASSERT( semaphore );

    atomic_inc( &orig->refcount );
    *semaphore = orig;

    return NvSuccess;
}

NvError NvOsSemaphoreUnmarshal(
    NvOsSemaphoreHandle hClientSema,
    NvOsSemaphoreHandle *phDriverSema)
{
    NV_ASSERT( hClientSema );
    NV_ASSERT( phDriverSema );

    atomic_inc( &hClientSema->refcount );
    *phDriverSema = hClientSema;

    return NvSuccess;
}

int NvOsSemaphoreWaitInterruptible(NvOsSemaphoreHandle semaphore);
int NvOsSemaphoreWaitInterruptible(NvOsSemaphoreHandle semaphore)
{
    NV_ASSERT(semaphore);

    return down_interruptible(&semaphore->sem);
}

void NvOsSemaphoreWait(NvOsSemaphoreHandle semaphore)
{
    int ret;
    
    NV_ASSERT(semaphore);

    do
    {
        /* FIXME: We should split the implementation into two parts -
         * one for semaphore that were created by users ioctl'ing into
         * the nvos device (which need down_interruptible), and others that
         * are created and used by the kernel drivers, which do not */
        ret = down_interruptible(&semaphore->sem);
        /* The kernel doesn't reschedule tasks
         * that have pending signals. If a signal
         * is pending, forcibly reschedule the task.
         */
        if (ret && !try_to_freeze())
            schedule();
    } while (ret);
}

NvError NvOsSemaphoreWaitTimeout(
    NvOsSemaphoreHandle semaphore,
    NvU32 msec)
{
    int t;

    NV_ASSERT( semaphore );

    if (!semaphore)
        return NvError_Timeout;

    if (msec==NV_WAIT_INFINITE)
    {
        NvOsSemaphoreWait(semaphore);
        return NvSuccess;
    }
    else if (msec==0)
    {
        t = down_trylock(&semaphore->sem);
        if (!t)
            return NvSuccess;
        else
            return NvError_Timeout;
    }

    /* FIXME:  The kernel doesn't provide an interruptible timed
     * semaphore wait, which would be preferable for our the ioctl'd
     * NvOs sempahores. */
    t = down_timeout(&semaphore->sem, (long)msecs_to_jiffies( msec ));

    if (t == -ETIME)
        return NvError_Timeout;
    else if (!t)
        return NvSuccess;

    return NvError_AccessDenied;
}

void NvOsSemaphoreSignal(NvOsSemaphoreHandle semaphore)
{
    NV_ASSERT( semaphore );

    up( &semaphore->sem );
}

void NvOsSemaphoreDestroy(NvOsSemaphoreHandle semaphore)
{
    if (!semaphore)
        return;

    if( atomic_dec_return( &semaphore->refcount ) == 0 )
        kfree( semaphore );
}

NvError NvOsThreadMode(int coop)
{
    return NvError_NotImplemented;
}

typedef struct NvOsThreadRec
{
    struct task_struct *task;
    NvOsThreadFunction func;
    void *arg;
} NvOsThread;

static int thread_wrapper( void *arg )
{
    NvOsThread *t = (NvOsThread *)arg;
    t->func(t->arg);
    return 0;
}

static NvError NvOsThreadCreateInternal(
    NvOsThreadFunction function,
    void *args,
    NvOsThreadHandle *thread,
    NvBool elevatedPriority)
{
    NvError e;
    NvOsThread *t = 0;
    static NvU32 NvOsKernelThreadIndex = 0;
    struct sched_param sched;
    int scheduler;
    NvU32 ThreadId;

    t = kzalloc( sizeof(NvOsThread), GFP_KERNEL );
    if( !t )
    {
        return NvError_InsufficientMemory;
    }

    t->func = function;
    t->arg = args;

    ThreadId = (NvU32)NvOsAtomicExchangeAdd32((NvS32*)&NvOsKernelThreadIndex,1);
    t->task =
        kthread_create(thread_wrapper, t, "NvOsKernelThread/%d", ThreadId);

    if(IS_ERR(t->task))
    {
        e = NvError_InsufficientMemory;
        goto fail;
    }

    if (elevatedPriority)
    {
        scheduler = SCHED_FIFO;
        sched.sched_priority = KTHREAD_IRQ_PRIO+1;
    }
    else
    {
        scheduler = SCHED_NORMAL;
        sched.sched_priority = 0;
    }

    if (sched_setscheduler_nocheck( t->task, scheduler, &sched ) < 0)
        NvOsDebugPrintf("Failed to set task priority to %d\n",
            sched.sched_priority);
    
    *thread = t;
    wake_up_process( t->task );
    e = NvSuccess;
    goto clean;

fail:
    kfree( t );

clean:
    return e;
}


NvError NvOsInterruptPriorityThreadCreate(
    NvOsThreadFunction function,
    void *args,
    NvOsThreadHandle *thread)
{
    return NvOsThreadCreateInternal(function, args, thread, NV_TRUE);
}

NvError NvOsThreadCreate(
    NvOsThreadFunction function,
    void *args,
    NvOsThreadHandle *thread)
{
    return NvOsThreadCreateInternal(function, args, thread, NV_FALSE);
}

NvError NvOsThreadSetLowPriority(void)
{
    struct sched_param sched;
    struct task_struct *curr;

    curr = get_current();
    sched.sched_priority = 0;

    if (unlikely(!curr))
        return NvError_NotInitialized;

    if (sched_setscheduler_nocheck( curr, SCHED_IDLE, &sched )<0)
    {
        NvOsDebugPrintf("Failed to set low priority for thread %p\n", curr);
        return NvError_NotSupported;
    }

    return NvSuccess;
}

void NvOsThreadJoin(NvOsThreadHandle thread)
{
    if (!thread)
        return;

    (void)kthread_stop(thread->task);
    kfree(thread);
}

void NvOsThreadYield(void)
{
    schedule();
}

NvS32 NvOsAtomicCompareExchange32(
    NvS32 *pTarget,
    NvS32 OldValue,
    NvS32 NewValue)
{
    return atomic_cmpxchg( (atomic_t *)pTarget, OldValue, NewValue );
}

NvS32 NvOsAtomicExchange32(NvS32 *pTarget, NvS32 Value)
{
    return atomic_xchg( (atomic_t *)pTarget, Value );
}

NvS32 NvOsAtomicExchangeAdd32(NvS32 *pTarget, NvS32 Value)
{
    NvS32 new;
    new = atomic_add_return( Value, (atomic_t *)pTarget );
    return new + (-Value);
}

NvU32 NvOsTlsAlloc(void)
{
    return 0;
}

void NvOsTlsFree(NvU32 TlsIndex)
{
}

void *NvOsTlsGet(NvU32 TlsIndex)
{
    return 0;
}

void NvOsTlsSet(NvU32 TlsIndex, void *Value)
{
}

NvU32 NvOsGetTimeMS(void)
{
    struct timespec ts;
    s64 nsec;
    getnstimeofday(&ts);
    nsec = timespec_to_ns(&ts);
    do_div(nsec, 1000000);
    return (NvU32)nsec;
}

NvU64 NvOsGetTimeUS(void)
{
    struct timespec ts;
    s64 nsec;
    getnstimeofday(&ts);
    nsec = timespec_to_ns(&ts);
    do_div(nsec, 1000);
    return (NvU32)nsec;
}

void NvOsDataCacheWritebackRange(
    void *start,
    NvU32 length)
{
	dmac_map_area(start, length, DMA_TO_DEVICE);
}

void NvOsDataCacheWritebackInvalidateRange(
    void *start,
    NvU32 length)
{
    dmac_flush_range(start, (NvU8*)start+length);
}

void NvOsInstrCacheInvalidate(void)
{
}

void NvOsInstrCacheInvalidateRange(
    void *start,
    NvU32 length)
{
    __cpuc_coherent_kern_range((unsigned long)start,
         (unsigned long)start+length);
}

void NvOsFlushWriteCombineBuffer( void )
{
    dsb();
    outer_sync();
}

NvError NvOsInterruptRegisterInternal(
    NvU32 IrqListSize,
    const NvU32 *pIrqList,
    const void *pList,
    void* context,
    NvOsInterruptHandle *handle,
    NvBool InterruptEnable,
    NvBool IsUser)
{
    const NvOsSemaphoreHandle *pSemList = NULL;
    const NvOsInterruptHandler *pFnList = NULL;
    NvError e = NvSuccess;
    NvOsInterruptBlock *pNewBlock;
    NvU32 i;

    if (!IrqListSize)
        return NvError_BadValue;

    if (IsUser)
        pSemList = (const NvOsSemaphoreHandle *)pList;
    else
        pFnList = (const NvOsInterruptHandler *)pList;

    *handle = (NvOsInterruptHandle) 0;
    pNewBlock = (NvOsInterruptBlock *)NvOsAlloc(INTBLOCK_SIZE(IrqListSize));
    if (!pNewBlock)
        return NvError_InsufficientMemory;

    NvOsMemset(pNewBlock, 0, INTBLOCK_SIZE(IrqListSize));

    pNewBlock->pArg = context;
    pNewBlock->NumIrqs = IrqListSize;
    pNewBlock->Shutdown = 0;
    for (i=0; i<IrqListSize; i++)
    {
        if (pIrqList[i] >= NVOS_MAX_SYSTEM_IRQS)
        {
            BUG();
            e = NvError_InsufficientMemory;
            goto clean_fail;
        }

        if (NvOsAtomicCompareExchange32((NvS32*)&s_pIrqList[pIrqList[i]], 0,
                (NvS32)pNewBlock)!=0)
        {
            e = NvError_AlreadyAllocated;
            goto clean_fail;
        }
        snprintf(pNewBlock->IrqList[i].IrqName, 
            sizeof(pNewBlock->IrqList[i].IrqName),
            "NvOsIrq%s%04d", (IsUser)?"User":"Kern", pIrqList[i]);

        pNewBlock->IrqList[i].Irq = pIrqList[i];

        /* HACK use threads for GPIO and tasklets for all other interrupts. */
        if (IsUser)
        {
            pNewBlock->IrqList[i].pSem = pSemList[i];
            pNewBlock->Flags |= NVOS_IRQ_IS_USER;
        }
        else
        {
            pNewBlock->IrqList[i].pHandler = pFnList[i];
            if (pIrqList[i] >= INT_GPIO_BASE)
                pNewBlock->Flags |= NVOS_IRQ_IS_KERNEL_THREAD;
            else
                pNewBlock->Flags |= NVOS_IRQ_IS_TASKLET;
        }
    
        if ((pNewBlock->Flags & NVOS_IRQ_TYPE_MASK)==NVOS_IRQ_IS_KERNEL_THREAD)
        {
            struct sched_param p;
            p.sched_priority = KTHREAD_IRQ_PRIO;
            sema_init(&(pNewBlock->IrqList[i].sem), 0);
            pNewBlock->IrqList[i].task = 
                kthread_create(NvOsInterruptThreadWrapper,
                    (void *)((pIrqList[i]&0xffff) | ((i&0xffff)<<16)), 
                    pNewBlock->IrqList[i].IrqName);
            if (sched_setscheduler(pNewBlock->IrqList[i].task,
                    SCHED_FIFO, &p)<0)
                NvOsDebugPrintf("Failed to elevate priority for IRQ %u\n",
                    pIrqList[i]);
            wake_up_process( pNewBlock->IrqList[i].task );
        }

        if ((pNewBlock->Flags & NVOS_IRQ_TYPE_MASK)==NVOS_IRQ_IS_TASKLET)
        {
            tasklet_init(&pNewBlock->IrqList[i].Tasklet, NvOsTaskletWrapper,
                (pIrqList[i]&0xffff) | ((i&0xffff)<<16)); 
        }

        /* NvOs specifies that the interrupt handler is responsible for
         * re-enabling the interrupt.  This is not the standard behavior
         * for Linux IRQs, so only interrupts which are installed through
         * NvOs will have the no-auto-enable flag specified
         */
        set_irq_flags(pIrqList[i], IRQF_VALID | IRQF_NOAUTOEN);

        if (request_irq(pIrqList[i], NvOsIrqWrapper, 
                0, pNewBlock->IrqList[i].IrqName, (void*)i)!=0)
        {
            e = NvError_ResourceError;
            goto clean_fail;
        }
    }
    *handle = (NvOsInterruptHandle)pNewBlock;
    if (InterruptEnable)
    {
        pNewBlock->Flags |= NVOS_IRQ_IS_ENABLED;
        i = 0;
    }
    for ( ; i<IrqListSize; i++)
        enable_irq(pIrqList[i]);

    return NvSuccess;

 clean_fail:
    while (i)
    {
        --i;
        if ((pNewBlock->Flags & NVOS_IRQ_TYPE_MASK)==NVOS_IRQ_IS_KERNEL_THREAD)
        {
            up(&pNewBlock->IrqList[i].sem);
            (void)kthread_stop(pNewBlock->IrqList[i].task);
        }
        if ((pNewBlock->Flags & NVOS_IRQ_TYPE_MASK) == NVOS_IRQ_IS_TASKLET)
        {
            tasklet_kill(&pNewBlock->IrqList[i].Tasklet); 
        }
        free_irq(pIrqList[i], (void*)i);
        set_irq_flags(pIrqList[i], IRQF_VALID);
        NvOsAtomicCompareExchange32((NvS32*)&s_pIrqList[pIrqList[i]],
            (NvS32)pNewBlock, 0);
    }
    *handle = NULL;
    NvOsFree(pNewBlock);

    return e;
}

NvError NvOsInterruptRegister(
    NvU32 IrqListSize,
    const NvU32 *pIrqList,
    const NvOsInterruptHandler *pIrqHandlerList,
    void *context,
    NvOsInterruptHandle *handle,
    NvBool InterruptEnable)
{
    return NvOsInterruptRegisterInternal(IrqListSize, pIrqList,
               (const void*)pIrqHandlerList, context, handle, 
               InterruptEnable, NV_FALSE);
}

void NvOsInterruptUnregister(NvOsInterruptHandle handle)
{
    NvOsInterruptBlock *pBlock = (NvOsInterruptBlock *)handle;
    NvU32 i;

    if (!pBlock)
        return;

    pBlock->Shutdown = 1;

    for (i=0; i<pBlock->NumIrqs; i++)
    {
        free_irq(pBlock->IrqList[i].Irq, (void*)i);
        NvOsAtomicCompareExchange32(
            (NvS32*)&s_pIrqList[pBlock->IrqList[i].Irq], (NvS32)pBlock, 0);

        if ((pBlock->Flags & NVOS_IRQ_TYPE_MASK) == NVOS_IRQ_IS_KERNEL_THREAD)
        {
            up(&pBlock->IrqList[i].sem);
            (void)kthread_stop(pBlock->IrqList[i].task);
        }
        if ((pBlock->Flags & NVOS_IRQ_TYPE_MASK) == NVOS_IRQ_IS_TASKLET)
        {
            tasklet_kill(&pBlock->IrqList[i].Tasklet); 
        }
        set_irq_flags(pBlock->IrqList[i].Irq, IRQF_VALID);
    }

    NvOsFree(pBlock);
}

NvError NvOsInterruptEnable(NvOsInterruptHandle handle)
{
    NvOsInterruptBlock *pBlock = (NvOsInterruptBlock *)handle;
    NvU32 i;

    if (pBlock == NULL)
        BUG();

    if (!(pBlock->Flags & NVOS_IRQ_IS_ENABLED))
    {
        pBlock->Flags |= NVOS_IRQ_IS_ENABLED;
        for (i=0; i<pBlock->NumIrqs; i++)
            enable_irq(pBlock->IrqList[i].Irq);
    }

    return NvSuccess;
}

void NvOsInterruptDone(NvOsInterruptHandle handle)
{
    NvOsInterruptBlock *pBlock = (NvOsInterruptBlock *)handle;
    NvU32 i;

    if (pBlock == NULL)
        BUG();

    for (i=0; i<pBlock->NumIrqs; i++)
        enable_irq(pBlock->IrqList[i].Irq);
}

void NvOsInterruptMask(NvOsInterruptHandle handle, NvBool mask)
{
    NvOsInterruptBlock *pBlock = (NvOsInterruptBlock *)handle;
    NvU32 i;

    if (pBlock == NULL)
        BUG();

    if (mask)
    {
        for (i=0; i<pBlock->NumIrqs; i++)
            disable_irq(pBlock->IrqList[i].Irq);
    }
    else
    {
        for (i=0; i<pBlock->NumIrqs; i++)
            enable_irq(pBlock->IrqList[i].Irq);
    }
}

void NvOsProfileApertureSizes(NvU32 *apertures, NvU32 *sizes)
{
}

void NvOsProfileStart(void **apertures)
{
}

void NvOsProfileStop(void **apertures)
{
}

NvError NvOsProfileWrite(
    NvOsFileHandle file, NvU32 index,
    void *aperture)
{
    return NvError_NotImplemented;
}

NvError NvOsBootArgSet(NvU32 key, void *arg, NvU32 size)
{
    return NvError_NotImplemented;
}

NvError NvOsBootArgGet(NvU32 key, void *arg, NvU32 size)
{
    const void *src;
    NvU32 size_src;

    if (key>=NvBootArgKey_PreservedMemHandle_0 &&
        key<NvBootArgKey_PreservedMemHandle_Num)
    {
        int Index = key - NvBootArgKey_PreservedMemHandle_0;

        src = &s_BootArgs.MemHandleArgs[Index];
        size_src = sizeof(NvBootArgsPreservedMemHandle);
    }
    else
    {
        switch (key)
        {
        case NvBootArgKey_ChipShmoo:
            src = &s_BootArgs.ChipShmooArgs;
            size_src = sizeof(NvBootArgsChipShmoo);
            break;
        case NvBootArgKey_Framebuffer:
            src = &s_BootArgs.FramebufferArgs;
            size_src = sizeof(NvBootArgsFramebuffer);
            break;
        case NvBootArgKey_Display:
            src = &s_BootArgs.DisplayArgs;
            size_src = sizeof(NvBootArgsDisplay);
            break;
        case NvBootArgKey_Rm:            
            src = &s_BootArgs.RmArgs;
            size_src = sizeof(NvBootArgsRm);
            break;
        case NvBootArgKey_ChipShmooPhys:
            src = &s_BootArgs.ChipShmooPhysArgs;
            size_src = sizeof(NvBootArgsChipShmooPhys);
            break;
        case NvBootArgKey_WarmBoot:
            src = &s_BootArgs.WarmbootArgs;
            size_src = sizeof(NvBootArgsWarmboot);
            break;
        default:
            src = NULL;
            size_src = 0;
            break;
        }
    }

    if (!arg || !src || (size_src!=size))
        return NvError_BadParameter;

    NvOsMemcpy(arg, src, size_src);
    return NvSuccess;
}

/** nvassert functions */

void NvOsBreakPoint(const char* file, NvU32 line, const char* condition)
{
    printk( "assert: %s:%d: %s\n", file, line, (condition) ? condition : " " );
    dump_stack();
}

/** trace functions */

void NvOsTraceLogPrintf( const char *format, ... )
{

}

void NvOsTraceLogStart(void)
{
}

void NvOsTraceLogEnd(void)
{
}

/* resource tracking */

#if NV_DEBUG
void *NvOsAllocLeak( size_t size, const char *f, int l )
{
    return NvOsAlloc( size );
}

void *NvOsReallocLeak( void *ptr, size_t size, const char *f, int l )
{
    return NvOsRealloc( ptr, size );
}

void NvOsFreeLeak( void *ptr, const char *f, int l )
{
    NvOsFree( ptr );
}
#endif

void NvOsGetProcessInfo(char* buf, NvU32 len)
{
	NvOsSnprintf(buf,len, "(kernel pid=%d)", current->pid);
}

#if (NVOS_TRACE || NV_DEBUG)
void NvOsSetResourceAllocFileLine(void* userptr, const char* file, int line)
{
}
#endif

#ifdef GHACK

static int __init parse_tegra_tag(const struct tag *tag)
{
    const struct tag_nvidia_tegra *nvtag = &tag->u.tegra;

    if (nvtag->bootarg_key >= NvBootArgKey_PreservedMemHandle_0 &&
        nvtag->bootarg_key < NvBootArgKey_PreservedMemHandle_Num)
    {
        int Index = nvtag->bootarg_key - NvBootArgKey_PreservedMemHandle_0;
        NvBootArgsPreservedMemHandle *dst = &s_BootArgs.MemHandleArgs[Index];
        const NvBootArgsPreservedMemHandle *src =
            (const NvBootArgsPreservedMemHandle *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsPreservedMemHandle))
            printk("Unexpected preserved memory handle tag length!\n");
        else
            *dst = *src;
        return 0;
    }

    switch (nvtag->bootarg_key)
    {
    case NvBootArgKey_ChipShmoo:
    {
        NvBootArgsChipShmoo *dst = &s_BootArgs.ChipShmooArgs;
        const NvBootArgsChipShmoo *src =
            (const NvBootArgsChipShmoo *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsChipShmoo))
            printk("Unexpected shmoo tag length!\n");
        else
        {
            printk("Shmoo tag with %u handle\n",
                   src->MemHandleKey);
            *dst = *src;
        }
        return 0;
    }
    case NvBootArgKey_Display:
    {
        NvBootArgsDisplay *dst = &s_BootArgs.DisplayArgs;
        const NvBootArgsDisplay *src =
            (const NvBootArgsDisplay *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsDisplay))
            printk("Unexpected display tag length!\n");
        else
            *dst = *src;
        return 0;
    }
    case NvBootArgKey_Framebuffer:
    {
        NvBootArgsFramebuffer *dst = &s_BootArgs.FramebufferArgs;
        const NvBootArgsFramebuffer *src =
            (const NvBootArgsFramebuffer *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsFramebuffer))
            printk("Unexpected framebuffer tag length!\n");
        else
        {
            printk("Framebuffer tag with %u handle\n",
                   src->MemHandleKey);
            *dst = *src;
        }
        return 0;
    }
    case NvBootArgKey_Rm:
    {
        NvBootArgsRm *dst = &s_BootArgs.RmArgs;
        const NvBootArgsRm *src =
            (const NvBootArgsRm *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsRm))
            printk("Unexpected RM tag length!\n");
        else
            *dst = *src;
        return 0;
    }
    case NvBootArgKey_ChipShmooPhys:
    {
        NvBootArgsChipShmooPhys *dst = &s_BootArgs.ChipShmooPhysArgs;
        const NvBootArgsChipShmooPhys *src =
            (const NvBootArgsChipShmooPhys *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsChipShmooPhys))
            printk("Unexpected phys shmoo tag length!\n");
        else
        {
            printk("Phys shmoo tag with pointer 0x%X and length %u\n",
                   src->PhysShmooPtr, src->Size);
            *dst = *src;
        }
        return 0;
    }
    case NvBootArgKey_WarmBoot:
    {
        NvBootArgsWarmboot *dst = &s_BootArgs.WarmbootArgs;
        const NvBootArgsWarmboot *src =
            (const NvBootArgsWarmboot *)nvtag->bootarg;

        if (nvtag->bootarg_len != sizeof(NvBootArgsWarmboot))
            printk("Unexpected warmboot tag length!\n");
        else
        {
            printk("Found a warmboot tag!\n");
            *dst = *src;
        }
        return 0;
    }

    default:
        return 0;
    }
}
__tagtable(ATAG_NVIDIA_TEGRA, parse_tegra_tag);

void __init tegra_nvos_kernel_init(void);

void __init tegra_nvos_kernel_init(void)
{
    spin_lock_init(&gs_NvOsSpinLock);
}
#endif
