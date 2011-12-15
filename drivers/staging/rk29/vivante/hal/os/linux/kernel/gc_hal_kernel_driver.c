/****************************************************************************
*  
*    Copyright (C) 2005 - 2011 by Vivante Corp.
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




#include <linux/device.h>
#include <linux/slab.h>
// dkm: add
#include <linux/miscdevice.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include "gc_hal_kernel_linux.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"

#if USE_PLATFORM_DRIVER
#include <linux/platform_device.h>
#endif
// dkm: add
#include <mach/rk29_iomap.h>
#include <mach/cru.h>
#include <mach/pmu.h>

MODULE_DESCRIPTION("Vivante Graphics Driver");
MODULE_LICENSE("GPL");

struct class *gpuClass;

static gckGALDEVICE galDevice;

static int major = 199;
module_param(major, int, 0644);

int irqLine = -1;
module_param(irqLine, int, 0644);

long registerMemBase = 0x80000000;
module_param(registerMemBase, long, 0644);

ulong registerMemSize = 256 << 10;
module_param(registerMemSize, ulong, 0644);

long contiguousSize = 4 << 20;
module_param(contiguousSize, long, 0644);

ulong contiguousBase = 0;
module_param(contiguousBase, ulong, 0644);

// dkm: change to 16 from 32
long bankSize = 16 << 20;
module_param(bankSize, long, 0644);

int fastClear = -1;
module_param(fastClear, int, 0644);

int compression = -1;
module_param(compression, int, 0644);

int signal = 48;
module_param(signal, int, 0644);

ulong baseAddress = 0;
module_param(baseAddress, ulong, 0644);

int showArgs = 0;
module_param(showArgs, int, 0644);

#if ENABLE_GPU_CLOCK_BY_DRIVER
// dkm: change
unsigned long coreClock = 552*1000000;
module_param(coreClock, ulong, 0644);
#endif

uint gpu_dmask = D_ERROR;
module_param(gpu_dmask, uint, 0644);

uint gpuState = 0;
uint regAddress = 0;

// gcdkREPORT_VIDMEM_USAGE add by vv
#if gcdkREPORT_VIDMEM_USAGE
#include <linux/proc_fs.h>

static struct proc_dir_entry *s_gckGPUProc;
static gcsHAL_PRIVATE_DATA_PTR s_gckHalPrivate;

static char * _MemTypes[gcvSURF_NUM_TYPES] =
{
    "UNKNOWN",  /* gcvSURF_TYPE_UNKNOWN       */
    "INDEX",    /* gcvSURF_INDEX              */
    "VERTEX",   /* gcvSURF_VERTEX             */
    "TEXTURE",  /* gcvSURF_TEXTURE            */
    "RT",       /* gcvSURF_RENDER_TARGET      */
    "DEPTH",    /* gcvSURF_DEPTH              */
    "BITMAP",   /* gcvSURF_BITMAP             */
    "TILE_STA", /*  gcvSURF_TILE_STATUS       */
    "MASK",     /* gcvSURF_MASK               */
    "SCISSOR",  /* gcvSURF_SCISSOR            */
    "HZ"        /* gcvSURF_HIERARCHICAL_DEPTH */
};

gctINT gvkGAL_Read_Proc(char *page, char **start, off_t offset, int count, int *eof, void *data)
{
    gctINT len = 0;
    gctUINT type;
    
    len += sprintf(page+len, "------------------------------------\n");
    len += sprintf(page+len, "   Type         Current          Max\n");

    if(NULL == s_gckHalPrivate)
    {
        *eof = 1;
        return len;
    }
    
    for (type = 0; type < gcvSURF_NUM_TYPES; type++)
    {
        len += sprintf(page+len, "[%8s]  %8llu KB  %8llu KB\n", 
               _MemTypes[type],
               s_gckHalPrivate->allocatedMem[type] / 1024,
               s_gckHalPrivate->maxAllocatedMem[type] / 1024);
    }
    
    len += sprintf(page+len, "[   TOTAL]  %8llu KB  %8llu KB\n",
           s_gckHalPrivate->totalAllocatedMem / 1024,
           s_gckHalPrivate->maxTotalAllocatedMem / 1024);

    *eof = 1;
    return len;
}


static gctINT gckDeviceProc_Register(void)
{
    s_gckGPUProc = create_proc_read_entry("graphics/gpu", 0, NULL, gvkGAL_Read_Proc, NULL);
    if(NULL == s_gckGPUProc)
    {
        return -1;
    }

    return 0;
}

static void gckDeviceProc_UnRegister(void)
{
    if(NULL != s_gckGPUProc)
    {
        struct proc_dir_entry *gckGPUPrarentProc = s_gckGPUProc->parent;
        if(NULL == gckGPUPrarentProc)
        {
            return ;    
        }
        
        remove_proc_entry("gpu", gckGPUPrarentProc);
        
        /** no subdir */
        if(NULL == gckGPUPrarentProc->subdir)
        {
            remove_proc_entry("graphics", NULL);
        }
    }
}
#endif

// dkm: add
int shutdown = 0;

static int drv_open(struct inode *inode, struct file *filp);
static int drv_release(struct inode *inode, struct file *filp);
static long drv_ioctl(struct file *filp,
                     unsigned int ioctlCode, unsigned long arg);
static int drv_mmap(struct file * filp, struct vm_area_struct * vma);

struct file_operations driver_fops =
{
    .open   	= drv_open,
    .release	= drv_release,
    .unlocked_ioctl	= drv_ioctl,
    .mmap   	= drv_mmap,
};

// dkm: gcdENABLE_AUTO_FREQ
#if (1==gcdENABLE_AUTO_FREQ)
#include <linux/timer.h>
struct timer_list gpu_timer;
extern void get_run_idle(u32 *run, u32 *idle);
extern void set_nextfreq(int freq);
int power_cnt = 0;
int last_precent = 0;
int last_freq = 0;
void gputimer_callback(unsigned long arg)
{
    u32 run, idle;
    int precent, freq, diff;
    
	mod_timer(&gpu_timer, jiffies + HZ/4);

    get_run_idle(&run, &idle);
    precent = (int)((run*100)/(run+idle));

    if(precent<90) { 
        power_cnt--; 
    } else if (precent==100){
        power_cnt += 2;
    } else {
        diff = precent - last_precent;
        if(diff>0) {
            if(diff>5)      power_cnt += 2;
            else            power_cnt += 1;
        } else {
            power_cnt--; 
        }
    }
    if(power_cnt<0)     power_cnt = 0;
    if(power_cnt>10)    power_cnt = 10;
    last_precent = precent;

    if(power_cnt<=0)        freq = 360;
    else if(power_cnt>=6)   freq = 552;
    else                    freq = 456;

    if(freq!=last_freq) {
        last_freq = freq;
        //set_nextfreq(freq);
    }

    printk("gpu load : %3d %%\n", precent);
    //printk("%8d /%8d = %3d %%, needfreq = %dM (%d)\n", (int)run, (int)(run+idle), precent, freq, power_cnt);
}
#elif(2==gcdENABLE_AUTO_FREQ)
#include <linux/timer.h>
struct timer_list gpu_timer;
int needhighfreq = 0;
int lowfreq = 300;
int highfreq = 552;
void mod_gpu_timer(void)
{
    mod_timer(&gpu_timer, jiffies + 3*HZ);
}
void gputimer_callback(unsigned long arg)
{
    needhighfreq = 0;
    //printk("needhighfreq = 0! \n");
}
#endif

// dkm: gcdENABLE_DELAY_EARLY_SUSPEND
#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
struct delayed_work suspend_work;
void real_suspend(struct work_struct *work)
{
    gceSTATUS status;

    status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_OFF);

    if (gcmIS_ERROR(status))
    {
        printk("%s fail!\n", __func__);
        return;
    }
}
#endif

int drv_open(struct inode *inode, struct file* filp)
{
    gcsHAL_PRIVATE_DATA_PTR	private;

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "Entering drv_open\n");

    private = kmalloc(sizeof(gcsHAL_PRIVATE_DATA), GFP_KERNEL);

    if (private == gcvNULL)
    {
    	return -ENOTTY;
    }
    
    /* Zero the memory. */
    gckOS_ZeroMemory(private, gcmSIZEOF(gcsHAL_PRIVATE_DATA));

    private->device				= galDevice;
    private->mappedMemory		= gcvNULL;
	private->contiguousLogical	= gcvNULL;

#if gcdkUSE_MEMORY_RECORD
	private->memoryRecordList.prev = &private->memoryRecordList;
	private->memoryRecordList.next = &private->memoryRecordList;
#endif

	/* A process gets attached. */
	gcmkVERIFY_OK(
		gckKERNEL_AttachProcess(galDevice->kernel, gcvTRUE));

    if (galDevice->contiguousSize != 0 
        && !galDevice->contiguousMapped)
    {
    	gcmkVERIFY_OK(gckOS_MapMemory(galDevice->os,
									galDevice->contiguousPhysical,
									galDevice->contiguousSize,
									&private->contiguousLogical));
    }
    
    filp->private_data = private;
// gcdkREPORT_VIDMEM_USAGE add by vv
#if gcdkREPORT_VIDMEM_USAGE
    s_gckHalPrivate = private;
#endif

    return 0;
}

extern void
OnProcessExit(
	IN gckOS Os,
	IN gckKERNEL Kernel
	);

int drv_release(struct inode* inode, struct file* filp)
{
    gcsHAL_PRIVATE_DATA_PTR	private;
    gckGALDEVICE			device;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "Entering drv_close\n");

    private = filp->private_data;
    gcmkASSERT(private != gcvNULL);

    device = private->device;

#ifndef ANDROID
	gcmkVERIFY_OK(gckCOMMAND_Stall(device->kernel->command));
#else
    // dkm: 保留delay的做法
    //gcmkVERIFY_OK(gckOS_Delay(galDevice->os, 1000));
#endif

    gcmkVERIFY_OK(
		gckOS_DestroyAllUserSignals(galDevice->os));

#if gcdkUSE_MEMORY_RECORD
	FreeAllMemoryRecord(galDevice->os, private, &private->memoryRecordList);

#ifndef ANDROID
	gcmkVERIFY_OK(gckCOMMAND_Stall(device->kernel->command));
#endif
#endif

    if (!device->contiguousMapped)
    {
		if (private->contiguousLogical != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_UnmapMemory(galDevice->os,
											galDevice->contiguousPhysical,
											galDevice->contiguousSize,
											private->contiguousLogical));
		}
    }

	/* A process gets detached. */
	gcmkVERIFY_OK(
		gckKERNEL_AttachProcess(galDevice->kernel, gcvFALSE));
// gcdkREPORT_VIDMEM_USAGE add by vv
#if gcdkREPORT_VIDMEM_USAGE
    s_gckHalPrivate = NULL;
#endif

    kfree(private);
    filp->private_data = NULL;

    return 0;
}

long drv_ioctl(struct file *filp,
    	      unsigned int ioctlCode,
	      unsigned long arg)
{
    gcsHAL_INTERFACE iface;
    gctUINT32 copyLen;
    DRIVER_ARGS drvArgs;
    gckGALDEVICE device;
    gceSTATUS status;
    gcsHAL_PRIVATE_DATA_PTR private;
    
    private = filp->private_data;

    // dkm: add
    if(shutdown)
    {
        return -ENOTTY;
    }

    if (private == gcvNULL)
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] drv_ioctl: private_data is NULL\n");

    	return -ENOTTY;
    }
    
    device = private->device;
    
    if (device == gcvNULL)
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] drv_ioctl: device is NULL\n");

    	return -ENOTTY;
    }
	
    if (ioctlCode != IOCTL_GCHAL_INTERFACE
		&& ioctlCode != IOCTL_GCHAL_KERNEL_INTERFACE)
    {
        /* Unknown command. Fail the I/O. */
        return -ENOTTY;
    }

    /* Get the drvArgs to begin with. */
    copyLen = copy_from_user(&drvArgs,
    	    	    	     (void *) arg,
			     sizeof(DRIVER_ARGS));
			     
    if (copyLen != 0)
    {
    	/* The input buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }

    /* Now bring in the gcsHAL_INTERFACE structure. */
    if ((drvArgs.InputBufferSize  != sizeof(gcsHAL_INTERFACE))
    ||  (drvArgs.OutputBufferSize != sizeof(gcsHAL_INTERFACE))
    ) 
    {
    	return -ENOTTY;
    }

    copyLen = copy_from_user(&iface,
    	    	    	     drvArgs.InputBuffer,
			     sizeof(gcsHAL_INTERFACE));
    
    if (copyLen != 0)
    {
        /* The input buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }
	
#if gcdkUSE_MEMORY_RECORD
	if (iface.command == gcvHAL_EVENT_COMMIT)
	{
		MEMORY_RECORD_PTR mr;
		gcsQUEUE_PTR queue = iface.u.Event.queue;
		
		while (queue != gcvNULL)
		{
			gcsQUEUE_PTR record, next;

			/* Map record into kernel memory. */
			gcmkERR_BREAK(gckOS_MapUserPointer(device->os,
											  queue,
											  gcmSIZEOF(gcsQUEUE),
											  (gctPOINTER *) &record));

			switch (record->iface.command)
			{
            case gcvHAL_FREE_NON_PAGED_MEMORY:
		        mr = FindMemoryRecord(device->os,
                                      private,
                                      &private->memoryRecordList,
                                      gcvNON_PAGED_MEMORY,
                                      record->iface.u.FreeNonPagedMemory.bytes,
                                      record->iface.u.FreeNonPagedMemory.physical,
                                      record->iface.u.FreeNonPagedMemory.logical);
        		
		        if (mr != gcvNULL)
		        {
			        DestroyMemoryRecord(device->os, private, mr);
		        }
		        else
		        {
			        gcmkPRINT("*ERROR* Invalid non-paged memory for free");
		        }
                break;

            case gcvHAL_FREE_CONTIGUOUS_MEMORY:
		        mr = FindMemoryRecord(device->os,
                                      private,
                                      &private->memoryRecordList,
                                      gcvCONTIGUOUS_MEMORY,
                                      record->iface.u.FreeContiguousMemory.bytes,
                                      record->iface.u.FreeContiguousMemory.physical,
                                      record->iface.u.FreeContiguousMemory.logical);
        		
		        if (mr != gcvNULL)
		        {
			        DestroyMemoryRecord(device->os, private, mr);
		        }
		        else
		        {
			        gcmkPRINT("*ERROR* Invalid contiguous memory for free");
		        }
                break;

			case gcvHAL_FREE_VIDEO_MEMORY:
				mr = FindVideoMemoryRecord(device->os,
                                           private,
                                           &private->memoryRecordList,
                                           record->iface.u.FreeVideoMemory.node);
				
		        if (mr != gcvNULL)
		        {
			        DestroyVideoMemoryRecord(device->os, private, mr);
		        }
		        else
		        {
			        gcmkPRINT("*ERROR* Invalid video memory for free");
		        }
                break;
				
			default:
				break;
			}

			/* Next record in the queue. */
			next = record->next;

			/* Unmap record from kernel memory. */
			gcmkERR_BREAK(gckOS_UnmapUserPointer(device->os,
												queue,
												gcmSIZEOF(gcsQUEUE),
												(gctPOINTER *) record));
			queue = next;
		}
	}
#endif

    dprintk(D_IOCTL, "gckKERNEL_Dispatch(FromUser %d, Cmd %d)\n", (ioctlCode == IOCTL_GCHAL_INTERFACE), iface.command);

    status = gckKERNEL_Dispatch(device->kernel,
		(ioctlCode == IOCTL_GCHAL_INTERFACE) , &iface);
    
    if (gcmIS_ERROR(status))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
	    	      "[galcore] gckKERNEL_Dispatch returned %d.\n",
		      status);
    }

    else if (gcmIS_ERROR(iface.status))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
	    	      "[galcore] IOCTL %d returned %d.\n",
		      iface.command,
		      iface.status);
    }
    
    /* See if this was a LOCK_VIDEO_MEMORY command. */
    else if (iface.command == gcvHAL_LOCK_VIDEO_MEMORY)
    {
    	/* Special case for mapped memory. */
    	if (private->mappedMemory != gcvNULL
			&& iface.u.LockVideoMemory.node->VidMem.memory->object.type
				== gcvOBJ_VIDMEM)
		{
	   		/* Compute offset into mapped memory. */
	    	gctUINT32 offset = (gctUINT8 *) iface.u.LockVideoMemory.memory
	    	    	     	- (gctUINT8 *) device->contiguousBase;
			  
    	    /* Compute offset into user-mapped region. */
    	    iface.u.LockVideoMemory.memory =
	    	(gctUINT8 *)  private->mappedMemory + offset;
		}
    }
#if gcdkUSE_MEMORY_RECORD
	else if (iface.command == gcvHAL_ALLOCATE_NON_PAGED_MEMORY)
	{
		CreateMemoryRecord(device->os,
                           private,
                           &private->memoryRecordList,
                           gcvNON_PAGED_MEMORY,
                           iface.u.AllocateNonPagedMemory.bytes,
                           iface.u.AllocateNonPagedMemory.physical,
                           iface.u.AllocateNonPagedMemory.logical);
    }
	else if (iface.command == gcvHAL_FREE_NON_PAGED_MEMORY)
	{
		MEMORY_RECORD_PTR mr;
		
		mr = FindMemoryRecord(device->os,
                              private,
                              &private->memoryRecordList,
                              gcvNON_PAGED_MEMORY,
                              iface.u.FreeNonPagedMemory.bytes,
                              iface.u.FreeNonPagedMemory.physical,
                              iface.u.FreeNonPagedMemory.logical);
		
		if (mr != gcvNULL)
		{
			DestroyMemoryRecord(device->os, private, mr);
		}
		else
		{
			gcmkPRINT("*ERROR* Invalid non-paged memory for free");
		}
    }
	else if (iface.command == gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY)
	{
		CreateMemoryRecord(device->os,
                           private,
                           &private->memoryRecordList,
                           gcvCONTIGUOUS_MEMORY,
                           iface.u.AllocateContiguousMemory.bytes,
                           iface.u.AllocateContiguousMemory.physical,
                           iface.u.AllocateContiguousMemory.logical);
    }
	else if (iface.command == gcvHAL_FREE_CONTIGUOUS_MEMORY)
	{
		MEMORY_RECORD_PTR mr;
		
		mr = FindMemoryRecord(device->os,
                              private,
                              &private->memoryRecordList,
                              gcvCONTIGUOUS_MEMORY,
                              iface.u.FreeContiguousMemory.bytes,
                              iface.u.FreeContiguousMemory.physical,
                              iface.u.FreeContiguousMemory.logical);
		
		if (mr != gcvNULL)
		{
			DestroyMemoryRecord(device->os, private, mr);
		}
		else
		{
			gcmkPRINT("*ERROR* Invalid contiguous memory for free");
		}
    }
	else if (iface.command == gcvHAL_ALLOCATE_VIDEO_MEMORY)
	{
		gctSIZE_T bytes = (iface.u.AllocateVideoMemory.node->VidMem.memory->object.type == gcvOBJ_VIDMEM) 
						? iface.u.AllocateVideoMemory.node->VidMem.bytes 
						: iface.u.AllocateVideoMemory.node->Virtual.bytes;

		CreateVideoMemoryRecord(device->os,
                                private,
							    &private->memoryRecordList,
							    iface.u.AllocateVideoMemory.node,
							    iface.u.AllocateVideoMemory.type & 0xFF,
							    bytes);
	}
	else if (iface.command == gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY)
	{
		gctSIZE_T bytes = (iface.u.AllocateLinearVideoMemory.node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
						? iface.u.AllocateLinearVideoMemory.node->VidMem.bytes 
						: iface.u.AllocateLinearVideoMemory.node->Virtual.bytes;

		CreateVideoMemoryRecord(device->os,
                                private,
							    &private->memoryRecordList,
							    iface.u.AllocateLinearVideoMemory.node,
							    iface.u.AllocateLinearVideoMemory.type & 0xFF,
							    bytes);
	}
	else if (iface.command == gcvHAL_FREE_VIDEO_MEMORY)
	{
		MEMORY_RECORD_PTR mr;
		
		mr = FindVideoMemoryRecord(device->os,
                                   private,
                                   &private->memoryRecordList,
                                   iface.u.FreeVideoMemory.node);
		
		if (mr != gcvNULL)
		{
			DestroyVideoMemoryRecord(device->os, private, mr);
		}
		else
		{
			gcmkPRINT("*ERROR* Invalid video memory for free");
		}
	}
#endif

    /* Copy data back to the user. */
    copyLen = copy_to_user(drvArgs.OutputBuffer,
    	    	    	   &iface,
			   sizeof(gcsHAL_INTERFACE));

    if (copyLen != 0)
    {
    	/* The output buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }

    return 0;
}

static int drv_mmap(struct file * filp, struct vm_area_struct * vma)
{
    gcsHAL_PRIVATE_DATA_PTR private = filp->private_data;
    gckGALDEVICE device;
    int ret;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (private == gcvNULL)
    {
    	return -ENOTTY;
    }

    device = private->device;

    if (device == gcvNULL)
    {
        return -ENOTTY;
    }
// dkm: gcdENABLE_MEM_CACHE
#if (2==gcdENABLE_MEM_CACHE)
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#elif (1==gcdENABLE_MEM_CACHE)
    // NULL
#else
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
    vma->vm_flags    |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND;
    vma->vm_pgoff     = 0;

    if (device->contiguousMapped)
    {
    	ret = io_remap_pfn_range(vma,
	    	    	    	 vma->vm_start,
    	    	    	    	 (gctUINT32) device->contiguousPhysical >> PAGE_SHIFT,
				 size,
				 vma->vm_page_prot);
						   
    	private->mappedMemory = (ret == 0) ? (gctPOINTER) vma->vm_start : gcvNULL;
						   
    	return ret;
    }
    else
    {
    	return -ENOTTY;
    }
}

// dkm: add
static struct miscdevice miscdev = {
    .name = "galcore",
    .fops = &driver_fops,
    .minor = MISC_DYNAMIC_MINOR,
};

// dkm: 修改drv_init
#if !USE_PLATFORM_DRIVER
static int __init drv_init(void)
#else
static int drv_init(void)
#endif
{
    int ret;
    gckGALDEVICE device;

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
    struct clk * clk_gpu = NULL;
    struct clk * clk_aclk_gpu = NULL;
#endif

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "Entering drv_init\n");

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)

    // set clk_aclk_gpu rate but no enable
    clk_aclk_gpu = clk_get(NULL, "aclk_gpu");
    if (IS_ERR(clk_aclk_gpu))
    {
        int retval = PTR_ERR(clk_aclk_gpu);
        printk("clk_aclk_gpu get error: %d\n", retval);
        return -ENODEV;
    }
    if (clk_set_rate(clk_aclk_gpu, 312000000))  //designed on 300M
    {
       	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Can't set aclk_gpu clock.");
        return -EAGAIN;
    }

    // set clk_gpu rate but no enable
    clk_gpu = clk_get(NULL, "gpu");
    if (IS_ERR(clk_gpu))
    {
        int retval = PTR_ERR(clk_gpu);
        printk("clk_gpu get error: %d\n", retval);
        return -ENODEV;
    }
    /* APMU_GC_156M, APMU_GC_624M, APMU_GC_PLL2, APMU_GC_PLL2_DIV2 currently */
    if (clk_set_rate(clk_gpu, coreClock))  //designed on 500M
    {
       	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Can't set core clock.");
        return -EAGAIN;
    }
    
    // enable ram clock gate
    writel(readl(RK29_GRF_BASE+0xc0) & ~0x100000, RK29_GRF_BASE+0xc0);

#endif

	if (showArgs)
	{
		printk("galcore options:\n");
		printk("  irqLine         = %d\n",      irqLine);
		printk("  registerMemBase = 0x%08lX\n", registerMemBase);
		printk("  contiguousSize  = %ld\n",     contiguousSize);
		printk("  contiguousBase  = 0x%08lX\n", contiguousBase);
		printk("  bankSize        = 0x%08lX\n", bankSize);
		printk("  fastClear       = %d\n",      fastClear);
		printk("  compression     = %d\n",      compression);
		printk("  signal          = %d\n",      signal);
		printk("  baseAddress     = 0x%08lX\n", baseAddress);
#if ENABLE_GPU_CLOCK_BY_DRIVER
        printk("  coreClock       = %lu\n",     coreClock);
#endif
	}

    /* Create the GAL device. */
    gcmkVERIFY_OK(gckGALDEVICE_Construct(irqLine,
    	    	    	    	    	registerMemBase,
					registerMemSize,
					contiguousBase,
					contiguousSize,
					bankSize,
					fastClear,
					compression,
					baseAddress,
					signal,
					&device));
	
    /* Start the GAL device. */
    if (gcmIS_ERROR(gckGALDEVICE_Start(device)))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Can't start the gal device.\n");

    	/* Roll back. */
    	gckGALDEVICE_Stop(device);
    	gckGALDEVICE_Destroy(device);

    	return -1;
    }


#if 1
    ret = misc_register(&miscdev);
    if (ret < 0)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
            	      "[galcore] Could not register misc.\n");

    	/* Roll back. */
    	gckGALDEVICE_Stop(device);
    	gckGALDEVICE_Destroy(device);

        return -1;
    }
    galDevice = device;
#else

    /* Register the character device. */
    ret = register_chrdev(major, DRV_NAME, &driver_fops);
    if (ret < 0)
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Could not allocate major number for mmap.\n");

    	/* Roll back. */
    	gckGALDEVICE_Stop(device);
    	gckGALDEVICE_Destroy(device);

    	return -1;
    }
    else
    {
    	if (major == 0)
    	{
    	    major = ret;
    	}
    }

    galDevice = device;

	gpuClass = class_create(THIS_MODULE, "graphics_class");
	if (IS_ERR(gpuClass)) {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
					  "Failed to create the class.\n");
		return -1;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	device_create(gpuClass, NULL, MKDEV(major, 0), NULL, "galcore");
#else
	device_create(gpuClass, NULL, MKDEV(major, 0), "galcore");
#endif

#endif
	
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] irqLine->%ld, contiguousSize->%lu, memBase->0x%lX\n",
		  irqLine,
		  contiguousSize,
		  registerMemBase);
	
    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "[galcore] driver registered successfully.\n");

    return 0;
}

// dkm: 修改drv_exit
#if !USE_PLATFORM_DRIVER
static void __exit drv_exit(void)
#else
static void drv_exit(void)
#endif
{
#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
    struct clk * clk_gpu = NULL;
    struct clk * clk_aclk_gpu = NULL;
    struct clk * clk_hclk_gpu = NULL;
#endif
    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "[galcore] Entering drv_exit\n");

#if 1
    //misc_deregister(&miscdev);
#else

	device_destroy(gpuClass, MKDEV(major, 0));
	class_destroy(gpuClass);

    unregister_chrdev(major, DRV_NAME);
#endif

    // 去掉,避免关机的时候动态桌面报错
    //shutdown = 1;   

    mdelay(50); 
    gckGALDEVICE_Stop(galDevice);
    mdelay(50); 
    gckGALDEVICE_Destroy(galDevice);
    mdelay(50); 

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)

    printk("%s : gpu clk_disable... ", __func__);
    clk_hclk_gpu = clk_get(NULL, "hclk_gpu");
    if(!IS_ERR(clk_hclk_gpu))    clk_disable(clk_hclk_gpu);

    clk_disable(clk_get(NULL, "aclk_ddr_gpu"));
    
    clk_aclk_gpu = clk_get(NULL, "aclk_gpu");
    if(!IS_ERR(clk_aclk_gpu))    clk_disable(clk_aclk_gpu);   

    clk_gpu = clk_get(NULL, "gpu");
    if(!IS_ERR(clk_gpu))    clk_disable(clk_gpu);
    printk("done!\n");
    mdelay(10);

    printk("%s : gpu power off... ", __func__);
    pmu_set_power_domain(PD_GPU, false);
    printk("done!\n");
    mdelay(10);

    // disable ram clock gate
    writel(readl(RK29_GRF_BASE+0xc0) | 0x100000, RK29_GRF_BASE+0xc0);
    mdelay(10);

#endif
}

#if !USE_PLATFORM_DRIVER
module_init(drv_init);
module_exit(drv_exit);
#else

#ifdef CONFIG_DOVE_GPU
#define DEVICE_NAME "dove_gpu"
#else
#define DEVICE_NAME "galcore"
#endif


// dkm: CONFIG_HAS_EARLYSUSPEND
#if CONFIG_HAS_EARLYSUSPEND
static void gpu_early_suspend(struct early_suspend *h)
{
#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
    schedule_delayed_work(&suspend_work, 5*HZ);
#else
	gceSTATUS status;
	status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_OFF);

	if (gcmIS_ERROR(status))
	{
	    printk("%s fail!\n", __func__);
		return;
	}
#endif
}

static void gpu_early_resume(struct early_suspend *h)
{
	gceSTATUS status;

#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
    cancel_delayed_work_sync(&suspend_work);
#endif
	status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_ON);

	if (gcmIS_ERROR(status))
	{
	    printk("%s fail!\n", __func__);
		return;
	}
}

struct early_suspend gpu_early_suspend_info = {
	.suspend = gpu_early_suspend,
	.resume = gpu_early_resume,
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1,
};
#endif

static int __devinit gpu_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct resource *res;
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "gpu_irq");
	if (!res) {
		printk(KERN_ERR "%s: No irq line supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	irqLine = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpu_base");
	if (!res) {
		printk(KERN_ERR "%s: No register base supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	registerMemBase = res->start;
	registerMemSize = res->end - res->start + ((res->end & 1) ? 1 : 0);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpu_mem");
	if (!res) {
		printk(KERN_ERR "%s: No memory base supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	contiguousBase  = res->start;
	contiguousSize  = res->end - res->start + ((res->end & 1) ? 1 : 0);

	res = platform_get_resource_byname(pdev, IORESOURCE_IO, "gpu_clk");
	if (!res) {
		printk(KERN_ERR "%s: No gpu clk supplied, use default!\n", __FUNCTION__);
	} else {
	    coreClock = res->end * 1000000;
// dkm: gcdENABLE_AUTO_FREQ
#if (2==gcdENABLE_AUTO_FREQ)
        lowfreq = res->start;
        highfreq = res->end;
#endif
    }

// dkm: gcdENABLE_AUTO_FREQ
#if (1==gcdENABLE_AUTO_FREQ)
    init_timer(&gpu_timer);
    gpu_timer.function = gputimer_callback;
    gpu_timer.expires = jiffies + 15*HZ;
    add_timer(&gpu_timer);
#elif(2==gcdENABLE_AUTO_FREQ)
    init_timer(&gpu_timer);
    gpu_timer.function = gputimer_callback;
    gpu_timer.expires = jiffies + 3*HZ;
    add_timer(&gpu_timer);
#endif

// dkm: CONFIG_HAS_EARLYSUSPEND
#if CONFIG_HAS_EARLYSUSPEND
#if (2!=gcdENABLE_DELAY_EARLY_SUSPEND)
    register_early_suspend(&gpu_early_suspend_info);
#endif
#endif

// dkm: gcdENABLE_DELAY_EARLY_SUSPEND
#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
    INIT_DELAYED_WORK(&suspend_work, real_suspend);
#endif

	ret = drv_init();
	if(!ret) {
		platform_set_drvdata(pdev,galDevice);
		return ret;
	}

gpu_probe_fail:	
	printk(KERN_INFO "Failed to register gpu driver.\n");
	return ret;
}

static int __devinit gpu_remove(struct platform_device *pdev)
{
// dkm: gcdENABLE_DELAY_EARLY_SUSPEND
#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
    cancel_delayed_work_sync(&suspend_work);
#endif
	drv_exit();

	return 0;
}

static int __devinit gpu_suspend(struct platform_device *dev, pm_message_t state)
{
	gceSTATUS status;
	gckGALDEVICE device;
    
// dkm: gcdENABLE_DELAY_EARLY_SUSPEND
#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
    cancel_delayed_work_sync(&suspend_work);
#endif
	device = platform_get_drvdata(dev);
	
	status = gckHARDWARE_SetPowerManagementState(device->kernel->hardware, gcvPOWER_OFF);

	if (gcmIS_ERROR(status))
	{
		return -1;
	}

	return 0;
}

static int __devinit gpu_resume(struct platform_device *dev)
{
	gceSTATUS status;
	gckGALDEVICE device;

	device = platform_get_drvdata(dev);
	
	status = gckHARDWARE_SetPowerManagementState(device->kernel->hardware, gcvPOWER_ON);

	if (gcmIS_ERROR(status))
	{
		return -1;
	}

	status = gckHARDWARE_SetPowerManagementState(device->kernel->hardware, gcvPOWER_IDLE_BROADCAST);

	if (gcmIS_ERROR(status))
	{
		return -1;
	}

	return 0;
}

// dkm: add
static void __devinit gpu_shutdown(struct platform_device *dev)
{
#if (1==gcdENABLE_DELAY_EARLY_SUSPEND)
    cancel_delayed_work_sync(&suspend_work);
#endif
    drv_exit();
}


static struct platform_driver gpu_driver = {
	.probe		= gpu_probe,
	.remove		= gpu_remove,
    // dkm add
    .shutdown   = gpu_shutdown,
	.suspend	= gpu_suspend,
	.resume		= gpu_resume,

	.driver		= {
		.name	= DEVICE_NAME,
	}
};

#if 0 // by dkm
#ifndef CONFIG_DOVE_GPU
static struct resource gpu_resources[] = {
    {
        .name   = "gpu_irq",
        .flags  = IORESOURCE_IRQ,
    },
    {
        .name   = "gpu_base",
        .flags  = IORESOURCE_MEM,
    },
    {
        .name   = "gpu_mem",
        .flags  = IORESOURCE_MEM,
    },
};

static struct platform_device * gpu_device;
#endif
#endif

static int __init gpu_init(void)
{
	int ret = 0;

#if 0   //add by dkm
#ifndef CONFIG_DOVE_GPU
	gpu_resources[0].start = gpu_resources[0].end = irqLine;

	gpu_resources[1].start = registerMemBase;
	gpu_resources[1].end   = registerMemBase + registerMemSize - 1;

	gpu_resources[2].start = contiguousBase;
	gpu_resources[2].end   = contiguousBase + contiguousSize - 1;

	/* Allocate device */
	gpu_device = platform_device_alloc(DEVICE_NAME, -1);
	if (!gpu_device)
	{
		printk(KERN_ERR "galcore: platform_device_alloc failed.\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Insert resource */
	ret = platform_device_add_resources(gpu_device, gpu_resources, 3);
	if (ret)
	{
		printk(KERN_ERR "galcore: platform_device_add_resources failed.\n");
		goto put_dev;
	}

	/* Add device */
	ret = platform_device_add(gpu_device);
	if (ret)
	{
		printk(KERN_ERR "galcore: platform_device_add failed.\n");
		goto del_dev;
	}
#endif
#endif
	ret = platform_driver_register(&gpu_driver);
	if (!ret)
	{
// add by vv
#if gcdkREPORT_VIDMEM_USAGE
        gckDeviceProc_Register();
#endif
        
		goto out;
	}

#if 0   //add by dkm
#ifndef CONFIG_DOVE_GPU
del_dev:
	platform_device_del(gpu_device);
put_dev:
	platform_device_put(gpu_device);
#endif
#endif

out:
	return ret;

}

static void __exit gpu_exit(void)
{
	platform_driver_unregister(&gpu_driver);
#if 0   //add by dkm
#ifndef CONFIG_DOVE_GPU
	platform_device_unregister(gpu_device);
#endif
#endif

// add by vv
#if gcdkREPORT_VIDMEM_USAGE
   gckDeviceProc_UnRegister();
#endif
   printk("UnLoad galcore.ko success.\n");
}

module_init(gpu_init);
module_exit(gpu_exit);


#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

struct RegDefine {
    char regname[35];
    uint offset;
};

struct RegDefine reg_def[] =
{
    {"AQHiClockControl",        0x0000},
    {"AQHiIdle",                0x0001},
    {"AQAxiConfig",             0x0002},
    {"AQAxiStatus",             0x0003},
    {"AQIntrAcknowledge",       0x0004},
    {"AQIntrEnbl",              0x0005},
    {"AQIdent",                 0x0006},
    {"GCFeatures",              0x0007},
    {"GCChipId",                0x0008},
    {"GCChipRev",               0x0009},
    {"GCChipDate",              0x000A},
    {"GCChipTime",              0x000B},
    {"GCChipCustomer",          0x000C},
    {"GCMinorFeatures0",        0x000D},
    {"GCCacheControl",          0x000E},
    {"GCResetMemCounters",      0x000F},
    {"gcTotalReads",            0x0010},
    {"gcTotalWrites",           0x0011},
    {"gcChipSpecs",             0x0012},
    {"gcTotalWriteBursts",      0x0013},
    {"gcTotalWriteReqs",        0x0014},
    {"gcTotalWriteLasts",       0x0015},
    {"gcTotalReadBursts",       0x0016},
    {"gcTotalReadReqs",         0x0017},
    {"gcTotalReadLasts",        0x0018},
    {"gcGpOut0",                0x0019},
    {"gcGpOut1",                0x001A},
    {"gcGpOut2",                0x001B},
    {"gcAxiControl",            0x001C},
    {"GCMinorFeatures1",        0x001D},
    {"gcTotalCycles",           0x001E},
    {"gcTotalIdleCycles",       0x001F},
    
    {"AQMemoryFePageTable",     0x0100},
    {"AQMemoryTxPageTable",     0x0101},
    {"AQMemoryPePageTable",     0x0102},
    {"AQMemoryPezPageTable",    0x0103},
    {"AQMemoryRaPageTable",     0x0104},
    {"AQMemoryDebug",           0x0105},
    {"AQMemoryRa",              0x0106},
    {"AQMemoryFe",              0x0107},
    {"AQMemoryTx",              0x0108},
    {"AQMemoryPez",             0x0109},
    {"AQMemoryPec",             0x010A},
    {"AQRegisterTimingControl", 0x010B},
    {"gcMemoryReserved",        0x010C},
    {"gcDisplayPriority",       0x010D},
    {"gcDbgCycleCounter",       0x010E},
    {"gcOutstandingReads0",     0x010F},
    {"gcOutstandingReads1",     0x0110},
    {"gcOutstandingWrites",     0x0111},
    {"gcDebugSignalsRa",        0x0112},
    {"gcDebugSignalsTx",        0x0113},
    {"gcDebugSignalsFe",        0x0114},
    {"gcDebugSignalsPe",        0x0115},
    {"gcDebugSignalsDe",        0x0116},
    {"gcDebugSignalsSh",        0x0117},
    {"gcDebugSignalsPa",        0x0118},
    {"gcDebugSignalsSe",        0x0119},
    {"gcDebugSignalsMc",        0x011A},
    {"gcDebugSignalsHi",        0x011B},
    {"gcDebugControl0",         0x011C},
    {"gcDebugControl1",         0x011D},
    {"gcDebugControl2",         0x011E},
    {"gcDebugControl3",         0x011F},
    {"gcBusControl",            0x0120},
    {"gcregEndianness0",        0x0121},
    {"gcregEndianness1",        0x0122},
    {"gcregEndianness2",        0x0123},
    {"gcregDrawPrimitiveStartTimeStamp",    0x0124},
    {"gcregDrawPrimitiveEndTimeStamp",      0x0125},
    {"gcregReqBankAddrMask",    0x0126},
    {"gcregReqRowAddrMask",     0x0127},

    //{"gcregReqWeight",          0x0025},
    //{"gcregRdReqAgingThresh",   0x0013},
    //{"gcregWrReqAgingThresh",   0x0014},
    
    {"AQCmdBufferAddr",         0x0195},
    {"AQCmdBufferCtrl",         0x0196},
    {"AQFEDebugState",          0x0198},
    {"AQFEDebugCurCmdAdr",      0x0199},
    {"AQFEDebugCmdLowReg",      0x019A},
    {"AQFEDebugCmdHiReg",       0x019B},
    
    {"gcModulePowerControls",       0x0040},
    {"gcModulePowerModuleControl",  0x0041},
    {"gcModulePowerModuleStatus",   0x0042},
};

#define gpu_readl(offset)	readl(regAddress + offset*4)

static int proc_gpu_show(struct seq_file *s, void *v)
{
    int i = 0;

    switch(gpuState) {
        case gcvPOWER_ON:       seq_printf(s, "gpu state: POWER_ON\n");         break;
        case gcvPOWER_OFF:      seq_printf(s, "gpu state: POWER_OFF\n");        break;
        case gcvPOWER_IDLE:     seq_printf(s, "gpu state: POWER_IDLE\n");       break;
        case gcvPOWER_SUSPEND:  seq_printf(s, "gpu state: POWER_SUSPEND\n");    break;
        default:                seq_printf(s, "gpu state: %d\n", gpuState);     break;
    }
    
    seq_printf(s, "gpu regs:\n");

    for(i=0; i<sizeof(reg_def)/sizeof(struct RegDefine); i++) {
        seq_printf(s, "  %-35s : 0x%08x\n", reg_def[i].regname, gpu_readl(reg_def[i].offset));
    }
    
    return 0;
}

static int proc_gpu_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_gpu_show, NULL);
}

static const struct file_operations proc_gpu_fops = {
	.open		= proc_gpu_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init gpu_proc_init(void)
{
	proc_create("gpu", 0, NULL, &proc_gpu_fops);
	return 0;

}
late_initcall(gpu_proc_init);
#endif /* CONFIG_PROC_FS */

#endif

