/*
 * arch/arm/mach-tegra/nvos_user.c
 *
 * User-land access to NvOs APIs
 *
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/rwsem.h>
#include <mach/irqs.h>
#include "nvos.h"
#include "linux/nvos_ioctl.h"
#include "nvassert.h"

int nvos_open(struct inode *inode, struct file *file);
int nvos_close(struct inode *inode, struct file *file);
static long nvos_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int nvos_mmap(struct file *file, struct vm_area_struct *vma);
int NvOsSemaphoreWaitInterruptible(NvOsSemaphoreHandle semaphore);

#define DEVICE_NAME "nvos"

static const struct file_operations nvos_fops =
{
    .owner = THIS_MODULE,
    .open = nvos_open,
    .release = nvos_close,
    .unlocked_ioctl = nvos_ioctl,
    .mmap = nvos_mmap
};

static struct miscdevice nvosDevice =
{
    .name = DEVICE_NAME,
    .fops = &nvos_fops,
    .minor = MISC_DYNAMIC_MINOR,
};

typedef struct NvOsIrqListNodeRec
{
    struct list_head list;
    NvOsInterruptHandle h;
} NvOsIrqListNode;

typedef struct NvOsInstanceRec
{
    struct rw_semaphore    RwLock;
    struct vm_area_struct *Vma;
    NvOsMemRangeParams    *MemRange;
    struct task_struct    *tsk;
    spinlock_t             Lock;
    struct list_head       IrqHandles;
    int                    pid;
} NvOsInstance;

static int __init nvos_init( void )
{
    int retVal = 0;

    retVal = misc_register(&nvosDevice);

    if (retVal < 0)
    {
        printk("nvos init failure\n" );
    }

    return retVal;    
}

static void __exit nvos_deinit( void )
{
    misc_deregister (&nvosDevice);
}

int nvos_open(struct inode *inode, struct file *filp)
{
    NvOsInstance *Instance = NULL;

    filp->private_data = NULL;

    Instance = NvOsAlloc(sizeof(NvOsInstance));
    if (!Instance)
    {
        printk(KERN_INFO __FILE__ ": nvos_open failed\n");
        return -ENOMEM;
    }
    init_rwsem(&Instance->RwLock);
    Instance->tsk = current;
    Instance->pid = current->group_leader->pid;
    Instance->MemRange = NULL;
    spin_lock_init(&Instance->Lock);
    INIT_LIST_HEAD(&Instance->IrqHandles);
    filp->private_data = (void*)Instance;

    return 0;
}

int nvos_close(struct inode *inode, struct file *filp)
{
    NvOsIrqListNode *LeakedIrq;

    if (filp->private_data)
    {
        NvOsInstance *Instance = (NvOsInstance *)filp->private_data;
        filp->private_data = NULL;
        while (!list_empty(&Instance->IrqHandles))
        {
            LeakedIrq = list_first_entry(&Instance->IrqHandles, 
                            NvOsIrqListNode, list);
            list_del_init(&LeakedIrq->list);
            printk(__FILE__": leaked NvOsInterruptHandle %p\n",
                LeakedIrq->h);
            NvOsInterruptUnregister(LeakedIrq->h);
            NvOsFree(LeakedIrq);
        }

        if (Instance->MemRange)
            NvOsFree(Instance->MemRange);
        NvOsFree(Instance);
    }

    return 0;
}

extern NvError NvOsInterruptRegisterInternal(
    NvU32 IrqListSize,
    const NvU32 *pIrqList,
    const void *pIrqHandlerList,
    void* context,
    NvOsInterruptHandle *handle,
    NvBool InterruptEnable,
    NvBool IsUser);

static int interrupt_op(
    NvOsInstance *Instance,
    unsigned int cmd,
    unsigned long arg)
{
    NvOsInterruptOpParams p;
    NvOsInterruptOpParams *user = (NvOsInterruptOpParams*)arg;
    NvError e;

    e = NvOsCopyIn(&p, user, sizeof(NvOsInterruptOpParams));
    if (e != NvSuccess)
        return -EINVAL;

    switch(cmd) {
    case NV_IOCTL_INTERRUPT_ENABLE:
        e = NvOsInterruptEnable((NvOsInterruptHandle)p.handle);
        break;
    case NV_IOCTL_INTERRUPT_DONE:
        NvOsInterruptDone((NvOsInterruptHandle)p.handle);
        e = NvSuccess;
        break;
    case NV_IOCTL_INTERRUPT_UNREGISTER:
    {
        NvOsIrqListNode *IrqNode;
        if (Instance)
        {
            e = NvError_CountMismatch;
            spin_lock(&Instance->Lock);
            list_for_each_entry(IrqNode, &Instance->IrqHandles, list)
            {
                if (IrqNode->h == (NvOsInterruptHandle)p.handle)
                {
                    list_del(&IrqNode->list);
                    NvOsInterruptUnregister(IrqNode->h);
                    NvOsFree(IrqNode);
                    e = NvSuccess;
                    break;
                }
            }
            spin_unlock(&Instance->Lock);
        }
        else
        {
            NvOsInterruptUnregister((NvOsInterruptHandle)p.handle);
        }
        e = NvSuccess;
        break;
    }
    case NV_IOCTL_INTERRUPT_MASK:
        NvOsInterruptMask((NvOsInterruptHandle)p.handle, 
            p.arg ? NV_TRUE : NV_FALSE);
        e = NvSuccess;
        break;
    default:
        return -EINVAL;
    }

    if (NvOsCopyOut(&user->errCode, &e, sizeof(e))!=NvSuccess)
        return -EINVAL;
    return 0;
}

static int interrupt_register(
    NvOsInstance *Instance,
    unsigned long arg)
{
    NvOsInterruptRegisterParams k;
    NvOsInterruptHandle h = NULL;
    NvError e;
    NvU32 *irqList = NULL;
    NvOsSemaphoreHandle *semList = NULL;
    NvOsIrqListNode *node = NULL;

    e = NvOsCopyIn(&k, (void *)arg, sizeof(NvOsInterruptRegisterParams));
    if (e!=NvSuccess)
        return -EINVAL;
        
    irqList = NvOsAlloc(k.nIrqs * sizeof(NvU32));
    semList = NvOsAlloc(k.nIrqs * sizeof(NvOsSemaphoreHandle));
    node = NvOsAlloc(sizeof(NvOsIrqListNode));
    if (!node)
    {
        e = NvError_InsufficientMemory;
        goto fail;
    }

    if (!irqList || !semList)
    {
        e = NvError_InsufficientMemory;
        goto fail;
    }
    NV_CHECK_ERROR_CLEANUP(NvOsCopyIn(irqList, k.Irqs, k.nIrqs*sizeof(NvU32)));

    NV_CHECK_ERROR_CLEANUP(
        NvOsCopyIn(semList, k.SemaphoreList, 
            k.nIrqs*sizeof(NvOsSemaphoreHandle))
    );

    /* To ensure that the kernel handle is safely stored in the user-space
     * wrapper before any interrupts are processed, interrupts must be
     * registered and enabled in two separate ioctls.
     */
    e = NvOsInterruptRegisterInternal(k.nIrqs, irqList, 
            (const void*)semList, NULL, &h, NV_FALSE, NV_TRUE);

    if (e==NvSuccess && Instance)
    {
        spin_lock(&Instance->Lock);
        node->h = h;
        list_add_tail(&node->list, &Instance->IrqHandles);
        spin_unlock(&Instance->Lock);
    }

fail:

    NvOsFree(irqList);
    NvOsFree(semList);
    if (e!=NvSuccess)
    {
        NvOsFree(node);
        h = NULL;
    }

    k.errCode = e;
    k.kernelHandle = (NvUPtr)h;
    e = NvOsCopyOut((void*)arg, &k, sizeof(k));

    return (e==NvSuccess) ? 0 : -EINVAL;
}

static int sem_unmarshal(unsigned long arg)
{
    NvOsSemaphoreUnmarshalParams *p = (NvOsSemaphoreUnmarshalParams *)arg;
    NvOsSemaphoreUnmarshalParams l;
    NvError e;

    l.hNew = NULL;
    e = NvOsCopyIn(&l, p, sizeof(l));
    if (e!=NvSuccess)
        return -EINVAL;

    e = NvOsSemaphoreUnmarshal(l.hOrig, &l.hNew);
    l.Error = e;

    e = NvOsCopyOut(p, &l, sizeof(l));
    if (e!=NvSuccess)
    {
        if (l.hNew)
            NvOsSemaphoreDestroy(l.hNew);
        return -EINVAL;
    }
    return 0;
}

static int sem_clone(unsigned long arg)
{
    NvOsSemaphoreCloneParams *p = (NvOsSemaphoreCloneParams *)arg;
    NvOsSemaphoreCloneParams l;
    NvError e;

    l.hNew = NULL;
    e = NvOsCopyIn(&l, p, sizeof(l));
    if (e!=NvSuccess)
        return -EINVAL;

    e = NvOsSemaphoreClone(l.hOrig, &l.hNew);
    l.Error = e;
    e = NvOsCopyOut(p, &l, sizeof(l));

    if (e!=NvSuccess)
    {
        if (l.hNew)
            NvOsSemaphoreDestroy(l.hNew);
        return -EINVAL;
    }

    return 0;
}

static int sem_create(unsigned long arg)
{
    NvOsSemaphoreIoctlParams *p = (NvOsSemaphoreIoctlParams *)arg;
    NvOsSemaphoreIoctlParams l;

    if (NvOsCopyIn(&l, p, sizeof(l))!=NvSuccess)
        return -EINVAL;

    l.sem = NULL;
    l.error = NvOsSemaphoreCreate(&l.sem, l.value);

    if (NvOsCopyOut(p, &l, sizeof(l))!=NvSuccess)
    {
        if (l.sem)
            NvOsSemaphoreDestroy(l.sem);
        return -EINVAL;
    }

    return 0;
}

static long nvos_ioctl(struct file *filp,
    unsigned int cmd, unsigned long arg) {
    int e = 0;
    NvError err;
    NvOsSemaphoreHandle kernelSem;
    NvOsInstance *Instance = (NvOsInstance *)filp->private_data;

    #define DO_CLEANUP( code ) \
        do { \
            err = code; \
            if( err != NvSuccess ) \
            { \
                e = -EINVAL; \
                goto clean; \
            } \
        } while( 0 )

    switch( cmd ) {
    case NV_IOCTL_SEMAPHORE_CREATE:
        return sem_create(arg);

    case NV_IOCTL_SEMAPHORE_DESTROY:
        DO_CLEANUP(
            NvOsCopyIn( &kernelSem, (void *)arg, sizeof(kernelSem) )
        );

        NvOsSemaphoreDestroy(kernelSem);
        break;
    case NV_IOCTL_SEMAPHORE_CLONE:
        return sem_clone(arg);

    case NV_IOCTL_SEMAPHORE_UNMARSHAL:
        return sem_unmarshal(arg);

    case NV_IOCTL_SEMAPHORE_SIGNAL:
        DO_CLEANUP(
            NvOsCopyIn( &kernelSem, (void *)arg, sizeof(kernelSem) )
        );

        NvOsSemaphoreSignal(kernelSem);
        break;           
    case NV_IOCTL_SEMAPHORE_WAIT:
        DO_CLEANUP(
            NvOsCopyIn( &kernelSem, (void *)arg, sizeof(kernelSem) )
        );
        e = NvOsSemaphoreWaitInterruptible(kernelSem);
        break;
    case NV_IOCTL_SEMAPHORE_WAIT_TIMEOUT:
    {
        NvOsSemaphoreIoctlParams *p = (NvOsSemaphoreIoctlParams *)arg;
        NvOsSemaphoreIoctlParams k;

        DO_CLEANUP(
            NvOsCopyIn( &k, p, sizeof(k) )
        );

        if (k.value == NV_WAIT_INFINITE)
        {
            k.error = NvSuccess;
            e = NvOsSemaphoreWaitInterruptible(kernelSem);
        }
        else
        {
            k.error = NvOsSemaphoreWaitTimeout(k.sem, k.value);
        }

        DO_CLEANUP(
            NvOsCopyOut( &p->error, &k.error, sizeof(k.error) )
        );

        break;
    }
    case NV_IOCTL_INTERRUPT_REGISTER:
        lock_kernel();
        e = interrupt_register(Instance, arg);
        unlock_kernel();
        return e;

    case NV_IOCTL_INTERRUPT_UNREGISTER:
    case NV_IOCTL_INTERRUPT_DONE:
    case NV_IOCTL_INTERRUPT_ENABLE:
    case NV_IOCTL_INTERRUPT_MASK:
        lock_kernel();
        e = interrupt_op(Instance, cmd, arg);
        unlock_kernel();
        return (e) ? -EINVAL : 0;

    case NV_IOCTL_MEMORY_RANGE:
    {
        NvOsMemRangeParams *p;

        p = NvOsAlloc( sizeof(NvOsMemRangeParams) );
        if( !p )
        {
            e = -ENOMEM;
            goto clean;
        }

        DO_CLEANUP(
            NvOsCopyIn( p, (void *)arg, sizeof(NvOsMemRangeParams) );
        );

        if (!Instance)
            printk(KERN_INFO __FILE__"(%d): No instance!\n", __LINE__);

        if (Instance)
        {
            down_write(&Instance->RwLock);
            Instance->MemRange = p;
            up_write(&Instance->RwLock);
        }
        return 0;
    }
    default:
        pr_err("Unknown IOCTL: %x\n", _IOC_NR(cmd));
        e = -1;
    }

    #undef DO_CLEANUP

clean:
    return e;
}

static void nvos_vma_open (struct vm_area_struct *vma)
{
}

static void nvos_vma_close (struct vm_area_struct *vma)
{
}

static struct vm_operations_struct nvos_vm_ops =
{
    .open = nvos_vma_open,
    .close = nvos_vma_close,
};

int nvos_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long addr;
    unsigned long size;
    unsigned long pfn;
    NvOsInstance *Instance = (NvOsInstance *)filp->private_data;

    size = vma->vm_end - vma->vm_start;
    pfn = vma->vm_pgoff;
    addr = pfn << PAGE_SHIFT;

    if (!Instance)
        printk(KERN_INFO __FILE__"(%d): No instance!\n", __LINE__);

    if (Instance)
    {
        down_read(&Instance->RwLock);
        if (Instance->MemRange)
        {
            /* addr is an offset */
            if( size > Instance->MemRange->size )
            {
                printk( "nvos_mmap: size too big for restricted mapping: %lu "
                        "max %lu\n", size,
                        (unsigned long)Instance->MemRange->size );
                up_read(&Instance->RwLock);
                return -EAGAIN;
            }
            addr += Instance->MemRange->base;
            pfn = addr >> PAGE_SHIFT;
        }
        up_read(&Instance->RwLock);
    }

    vma->vm_flags |= (VM_IO | VM_DONTCOPY | VM_DONTEXPAND);

    // FIXME: This is a major hack
#ifdef CONFIG_ARCH_TEGRA_A9
    if (addr < 0x40000000UL)
        vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
    else
#endif
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
    {
        printk( "nvos_mmap failed\n" );
        return -EAGAIN;
    }

    vma->vm_ops = &nvos_vm_ops;
    vma->vm_private_data = Instance;

    return 0;
}

module_init(nvos_init);
module_exit(nvos_deinit);
